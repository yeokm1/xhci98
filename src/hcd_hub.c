/*
 * hcd_hub.c - the hub class inside the bus (roadmap-hcd.md tasks 27-A.1 and
 * 27-A.2; design record 13 sections 5.2, 10.1 to 10.4).
 *
 * An external hub is an object of the bus, never a PDO. When a port's
 * enumeration reaches Present with a device whose bDeviceClass is 9
 * (hcd_enum.c), the bus brings the hub up itself, in section 10.3's order:
 *
 *   the hub's node in the topology graph (xhci_topo.c), at its root port or
 *     below its parent hub's port - and, at the fifth tier, no further: a
 *     hub whose children would have no Route String is addressed and left
 *     unconfigured with its ports unpowered (counted, too deep);
 *   SET_CONFIGURATION, and SET_INTERFACE alternate 1 on a High-Speed hub of
 *     bDeviceProtocol 2 that has it (the multi-TT interface);
 *   GET_DESCRIPTOR(Hub), folded by the graph (its counters) and parsed here;
 *   one Configure Endpoint carrying the hub marking - Hub, Number of Ports,
 *     and on a High-Speed hub TTT and MTT (XhciTopoHubMark) - with the
 *     status-change endpoint (hcd_cfg.c, HcdCfgHubOpen);
 *   SET_FEATURE(PORT_POWER) on every managed port, the power-good wait, a
 *     port object in Empty per port, a first look at every port, and the
 *     status-change transfer armed.
 *
 * The hub's ports are then ports like the root ports: each has an
 * enumeration machine (xhci_enum.c), and hcd_enum.c carries out the
 * machine's debounce and reset through the hub's class requests here and
 * the rest exactly as for a root port, with the Slot Context a device
 * behind hubs needs (HcdHubPlace, HcdDeviceSlotParams): its Route String,
 * its root port, and for a Full- or Low-Speed device below a High-Speed hub
 * the TT hub's slot and port and its MTT, all answered by the graph.
 *
 * The status-change pipe: one interrupt IN transfer outstanding on it, its
 * report in the scratch buffer's tail (one slice per hub object); its
 * completion is marked in the device record (HcdHubXferRetired, from the
 * event DPC under the controller lock) and wakes the thread, which reads the
 * bitmap, looks at each port it names and re-arms only afterwards (section
 * 10.1). A pipe that fails twice in a row or halts is given up and the hub is
 * polled instead, every HCD_HUB_POLL_PASSES thread passes: a hub that
 * stopped answering has left, and its upstream port says so.
 *
 * Every function here runs on the controller thread at PASSIVE_LEVEL, the
 * one writer of the topology (design record 13 section 5.4, layer 1), so
 * the graph is used without the controller lock its header asks the
 * miniport to hold - the miniport's snoop ran in usbport's contexts, the
 * bus has only this one. Exceptions are marked.
 */

#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_xfer.h"
#include "xhci_pipe.h"
#include "xhci_enum.h"
#include "xhci_dbg.h"

/* Section 10.2: the hub times the reset (TDRST, 10-20 ms on a hub port, to
 * transcribe), polled from 20 ms on, given up at 500 ms - a bus policy
 * number; reset recovery 10 ms (TRSTRCY); attach debounce 100 ms (TATTDB). */
#define HCD_HUB_RESET_FIRST_MS      20UL
#define HCD_HUB_RESET_WAIT_MS       500UL
#define HCD_HUB_POLL_STEP_MS        10UL
#define HCD_HUB_RESET_RECOVERY_MS   10UL
#define HCD_HUB_DEBOUNCE_MS         100UL
/* A polled hub is looked at every this many thread passes (hcd_ctl.c wakes
 * the thread at least every 100 ms). */
#define HCD_HUB_POLL_PASSES         3UL
/* Status-change completions in a row that may fail before the pipe is
 * given up (section 10.1: "a second consecutive failure"). */
#define HCD_HUB_STATUS_TRIES        2UL

static VOID hcdHubDelay(ULONG milliseconds)
{
    LARGE_INTEGER due;

    HcdRelativeMs(&due, milliseconds);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
}

static PUCHAR hcdHubReport(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    return (PUCHAR)hc->ScratchVa + HCD_SCRATCH_CONTROL_BYTES +
           hub->Index * HCD_HUB_STATUS_BYTES;
}

/* ----------------------------------------------------------------------- */
/* The Slot Context of a device behind hubs, and of a hub                   */
/* ----------------------------------------------------------------------- */

/*
 * Everything a Slot Context says about where a device is and what it is
 * (design record 13 section 10.4), from its record: the Route String and
 * root port, the speed, the TT fields of a Full- or Low-Speed device below a
 * High-Speed hub, and - `withHub`, for a Configure Endpoint, which alone may
 * set them (xhci-data-structures.md, "Which command may set which Slot
 * Context field") - a hub's own Hub, Number of Ports, TTT and MTT. MTT is
 * the device's TT's for a device behind a multi-TT hub, OR'd with a
 * High-Speed hub's own. ContextEntries is the caller's. IRQL: any.
 */
VOID HcdDeviceSlotParams(PHCD_USB_DEVICE dev, ULONG withHub,
                         PXHCI_SLOT_PARAMS sp)
{
    PUCHAR b;
    ULONG i;

    b = (PUCHAR)sp;
    for (i = 0; i < sizeof(*sp); i++) {
        b[i] = 0;
    }
    sp->RouteString = dev->Route;
    sp->Psiv = dev->Speed;
    sp->RootHubPort = dev->Port;
    sp->ContextEntries = 1;
    sp->ParentSlotId = dev->TtSlot;
    sp->ParentPortNumber = dev->TtPort;
    sp->MultiTt = dev->TtMulti;
    if (withHub && dev->HubMarked) {
        sp->Hub = 1;
        sp->NumberOfPorts = dev->HubPorts;
        sp->TtThinkTime = dev->HubTtt;
        sp->MultiTt |= dev->HubMtt;
    }
}

/*
 * A device just given a slot on a hub's port: its place, from the graph
 * (27-A.2) - the Route String and tier below the hub's node, the root port
 * the path starts at, the Protocol Speed ID this controller uses for the
 * speed the hub reported, and, for a Full- or Low-Speed device, the TT it
 * sits behind: the nearest High-Speed hub above it and the port on that hub
 * its subtree hangs from (XhciTopoTtFor). An all-Full-Speed path has no TT
 * and every TT field stays 0. Returns 0 when the place cannot be given -
 * a parent the graph does not hold, a path past the Route String's five
 * tiers (counted), a speed the root port's protocol has no ID for - and the
 * caller gives the slot back. Counts the open of a behind-hub position.
 */
ULONG HcdHubPlace(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG speedClass,
                  PHCD_USB_DEVICE dev)
{
    PXHCI_TOPOLOGY topo;
    XHCI_TOPO_CHILD child;
    XHCI_TOPO_TT tt;
    PHCD_HUB hub;
    ULONG psiv;

    hub = p->Hub;
    topo = &hc->Hc.Topology;
    if (hub == NULL || hub->Device == NULL ||
        !XhciTopoChildOf(topo, hub->SlotId, p->Number, &child)) {
        XHCI_DBG_VALUE("hcd: hub port has no place, location", p->PortId);
        return 0;
    }
    if (child.TooDeep) {
        hc->Counters.TopoBehindHubTooDeep++;
        XHCI_DBG_VALUE("hcd: behind-hub device too deep, location",
                       p->PortId);
        return 0;
    }
    psiv = 0;
    if (hub->Usb3 ? !HcdSsHubPsiv(hc, p, child.RootPort, &psiv)
                  : XhciPortPsivForSpeed(&hc->Hc.PortMap, child.RootPort,
                                         speedClass, &psiv) != XHCI_CAPS_OK) {
        XHCI_DBG_VALUE("hcd: no speed ID behind hub, class", speedClass);
        return 0;
    }
    dev->Port = child.RootPort;
    dev->Route = child.Route;
    dev->Tier = child.Tier;
    dev->Speed = psiv;
    dev->TtSlot = 0;
    dev->TtPort = 0;
    dev->TtMulti = 0;
    if (!hub->Usb3 && speedClass != XHCI_SPEED_HIGH &&
        XhciTopoTtFor(topo, hub->SlotId, p->Number, &tt)) {
        dev->TtSlot = tt.HubAddress;
        dev->TtPort = tt.HubPort;
        dev->TtMulti = tt.MultiTt;
    }
    hc->Counters.TopoBehindHubOpens++;
    XHCI_DBG_VALUE("hcd: behind hub, root port/route",
                   (dev->Port << 24) | dev->Route);
    XHCI_DBG_VALUE("hcd: behind hub, TT slot/port/MTT",
                   (dev->TtSlot << 16) | (dev->TtPort << 8) | dev->TtMulti);
    return 1;
}

/* ----------------------------------------------------------------------- */
/* Hub class requests                                                       */
/* ----------------------------------------------------------------------- */

/* One hub-class request as the graph sees it, and its reply folded.
 * `length` 0 is a request with no data stage. Returns 1 with *bytes
 * received. */
static ULONG hcdHubRequest(PHCD_CONTROLLER hc, PHCD_HUB hub, UCHAR type,
                           UCHAR request, USHORT value, USHORT index,
                           ULONG length, PULONG bytes, PULONG stalled)
{
    XHCI_SETUP_PACKET setup;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_GONE gone;
    ULONG ok;

    *bytes = 0;
    *stalled = 0;
    if (hub->Device == NULL) {
        return 0;
    }
    setup.bmRequestType = type;
    setup.bRequest = request;
    setup.wValue = value;
    setup.wIndex = index;
    setup.wLength = (USHORT)length;
    XhciTopoObserveSetup(&hc->Hc.Topology, hub->SlotId, &setup, &snoop);
    ok = HcdThreadControlEx(hc, hub->Device, type, request, value, index,
                            length, bytes, stalled);
    if (ok && snoop.Reply == XHCI_TOPO_REPLY_PORT_STATUS) {
        (VOID)XhciTopoObserveReply(&hc->Hc.Topology, &snoop,
                                   (const UCHAR *)hc->ScratchVa, *bytes,
                                   &gone);
    }
    return ok;
}

static ULONG hcdHubFeature(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                           ULONG set, ULONG selector)
{
    ULONG bytes;
    ULONG stalled;

    return hcdHubRequest(hc, hub, XHCI_HUB_RT_PORT_OUT,
                         (UCHAR)(set ? XHCI_HUB_REQ_SET_FEATURE
                                     : XHCI_HUB_REQ_CLEAR_FEATURE),
                         (USHORT)selector, (USHORT)n, 0, &bytes, &stalled);
}

/* GET_STATUS(port n): wPortStatus and wPortChange. */
ULONG HcdHubPortStatus(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                       PULONG status, PULONG change)
{
    PUCHAR s;
    ULONG bytes;
    ULONG stalled;

    *status = 0;
    *change = 0;
    if (!hcdHubRequest(hc, hub, XHCI_HUB_RT_PORT_IN,
                       XHCI_HUB_REQ_GET_STATUS, 0, (USHORT)n, 4, &bytes,
                       &stalled) ||
        bytes < 4) {
        return 0;
    }
    s = (PUCHAR)hc->ScratchVa;
    *status = (ULONG)s[0] | ((ULONG)s[1] << 8);
    *change = (ULONG)s[2] | ((ULONG)s[3] << 8);
    return 1;
}

/* The attach debounce on a hub port (section 10.2 step 2): still connected
 * after it. A connect change raised meanwhile is a bounce, cleared here so
 * it does not start the device over once it is enumerated. */
ULONG HcdHubPortDebounce(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n)
{
    ULONG status;
    ULONG change;

    hcdHubDelay(HCD_HUB_DEBOUNCE_MS);
    if (!HcdHubPortStatus(hc, hub, n, &status, &change)) {
        return 0;
    }
    if ((change & XHCI_HUB_C_PORT_CONNECTION) != 0) {
        (VOID)hcdHubFeature(hc, hub, n, 0, XHCI_HUB_FEAT_C_PORT_CONNECTION);
    }
    return (status & XHCI_HUB_PORT_CONNECTION) != 0;
}

/*
 * SET_FEATURE(PORT_RESET) on hub port n, waited for (section 10.2 step 4):
 * C_PORT_RESET cleared, the port required enabled, the speed read from
 * wPortStatus, then the reset recovery. One device is between its reset
 * and its Address Device on the whole controller at a time (step 3) because
 * the thread runs one enumeration to its end before the next. Returns 1
 * with *speedClass when the port came back enabled.
 */
ULONG HcdHubPortReset(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                      PULONG speedClass)
{
    ULONG status;
    ULONG change;
    ULONG waited;
    ULONG progress;

    if (hub->Usb3) {
        return HcdSsHubPortReset(hc, hub, n, speedClass);
    }
    *speedClass = XHCI_SPEED_UNKNOWN;
    if (!HcdHubPortStatus(hc, hub, n, &status, &change) ||
        (status & XHCI_HUB_PORT_CONNECTION) == 0 ||
        !hcdHubFeature(hc, hub, n, 1, XHCI_HUB_FEAT_PORT_RESET)) {
        return 0;
    }
    hcdHubDelay(HCD_HUB_RESET_FIRST_MS);
    waited = HCD_HUB_RESET_FIRST_MS;
    for (;;) {
        if (!HcdHubPortStatus(hc, hub, n, &status, &change)) {
            return 0;
        }
        progress = XhciHubResetProgress(status, change);
        if (progress != XHCI_HUB_RESET_PENDING) {
            break;
        }
        if (waited >= HCD_HUB_RESET_WAIT_MS) {
            XHCI_DBG_VALUE("hcd: hub port reset timed out, port", n);
            return 0;
        }
        hcdHubDelay(HCD_HUB_POLL_STEP_MS);
        waited += HCD_HUB_POLL_STEP_MS;
    }
    if ((change & XHCI_HUB_C_PORT_RESET) != 0) {
        (VOID)hcdHubFeature(hc, hub, n, 0, XHCI_HUB_FEAT_C_PORT_RESET);
    }
    if (progress != XHCI_HUB_RESET_ENABLED) {
        return 0;
    }
    *speedClass = XhciHubPortSpeedClass(status);
    hcdHubDelay(HCD_HUB_RESET_RECOVERY_MS);
    return 1;
}

/*
 * One port the status-change report (or a poll, or the first look) named:
 * GET_STATUS, every change bit it saw cleared, a port that lost its power
 * to over-current powered again, and the decision for its machine in *d
 * (XhciHubPortDecide), which hcd_enum.c carries out. Returns 0 when the
 * hub did not answer.
 */
ULONG HcdHubPortLook(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                     ULONG state, PXHCI_HUB_PORT_DECISION d)
{
    ULONG status;
    ULONG change;
    ULONG bit;
    ULONG selector;

    if (hub->Usb3) {
        return HcdSsHubPortLook(hc, hub, n, state, d);
    }
    if (!HcdHubPortStatus(hc, hub, n, &status, &change)) {
        return 0;
    }
    XhciHubPortDecide(state, status, change, d);
    for (bit = 1; bit <= XHCI_HUB_C_PORT_RESET; bit <<= 1) {
        selector = XhciHubClearSelector(bit);
        if ((d->Clear & bit) != 0 && selector != 0) {
            (VOID)hcdHubFeature(hc, hub, n, 0, selector);
        }
    }
    if (d->OverCurrent) {
        XHCI_DBG_VALUE("hcd: hub port over-current, hub/port/status",
                       (hub->Index << 24) | (n << 16) | status);
    }
    if (d->Repower) {
        (VOID)hcdHubFeature(hc, hub, n, 1, XHCI_HUB_FEAT_PORT_POWER);
    }
    if (d->Suspended) {
        XHCI_DBG_VALUE("hcd: hub port resumed, hub/port",
                       (hub->Index << 8) | n);
    }
    return 1;
}

/* The hub's own status change (report bit 0): GET_STATUS(hub), and each of
 * C_HUB_LOCAL_POWER and C_HUB_OVER_CURRENT cleared (section 10.1). */
static VOID hcdHubSelf(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    PUCHAR s;
    ULONG bytes;
    ULONG stalled;
    ULONG status;
    ULONG change;

    if (!hcdHubRequest(hc, hub, XHCI_HUB_RT_HUB_IN, XHCI_HUB_REQ_GET_STATUS,
                       0, 0, 4, &bytes, &stalled) ||
        bytes < 4) {
        return;
    }
    s = (PUCHAR)hc->ScratchVa;
    status = (ULONG)s[0] | ((ULONG)s[1] << 8);
    change = (ULONG)s[2] | ((ULONG)s[3] << 8);
    if ((change & XHCI_HUB_C_LOCAL_POWER) != 0) {
        (VOID)hcdHubRequest(hc, hub, XHCI_HUB_RT_HUB_OUT,
                            XHCI_HUB_REQ_CLEAR_FEATURE,
                            (USHORT)XHCI_HUB_FEAT_C_HUB_LOCAL_POWER, 0, 0,
                            &bytes, &stalled);
    }
    if ((change & XHCI_HUB_C_OVER_CURRENT) != 0) {
        (VOID)hcdHubRequest(hc, hub, XHCI_HUB_RT_HUB_OUT,
                            XHCI_HUB_REQ_CLEAR_FEATURE,
                            (USHORT)XHCI_HUB_FEAT_C_HUB_OVER_CURRENT, 0, 0,
                            &bytes, &stalled);
    }
    if (change != 0) {
        XHCI_DBG_VALUE("hcd: hub status change, hub/status/change",
                       (hub->Index << 24) | (status << 8) | change);
    }
}

/* ----------------------------------------------------------------------- */
/* The status-change pipe                                                   */
/* ----------------------------------------------------------------------- */

/* The status-change record retired - completed, or drained when the pipe
 * closed (hcd_dev.c, hcd_io.c). IRQL: DISPATCH_LEVEL, controller lock
 * held. */
VOID HcdHubXferRetired(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    dev->HubXferArmed = 0;
    dev->HubXferDone = 1;
    HcdThreadWake(hc);
}

/*
 * One interrupt IN transfer on the status-change pipe, unless one is out
 * already: the report's slice of the scratch zeroed, the TD published under
 * the controller lock and its doorbell rung there, as hcd_io.c publishes
 * (Codex review of batch (c), round 3, finding 2). A pipe that will not
 * take it is given up for polling.
 */
VOID HcdHubRearm(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    USBPORT_SCATTER_GATHER_LIST sg;
    XHCI_NORMAL_REQUEST nreq;
    XHCI_TRB trbs[XHCI_XFER_MAX_CONTROL_TRBS];
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    PUCHAR b;
    ULONG offset;
    ULONG answer;
    ULONG armed;
    ULONG i;

    dev = hub->Device;
    pipe = hub->Status;
    if (dev == NULL || pipe == NULL || hub->Refused || hub->Polled) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    armed = dev->HubXferArmed || dev->HubXferDone;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (armed) {
        return;
    }
    b = hcdHubReport(hc, hub);
    for (i = 0; i < HCD_HUB_STATUS_BYTES; i++) {
        b[i] = 0;
    }
    offset = HCD_SCRATCH_CONTROL_BYTES + hub->Index * HCD_HUB_STATUS_BYTES;
    b = (PUCHAR)&sg;
    for (i = 0; i < sizeof(sg); i++) {
        b[i] = 0;
    }
    sg.SgElementCount = 1;
    sg.SgElement[0].SgPhysicalAddressLo = hc->ScratchPa.LowPart + offset;
    sg.SgElement[0].SgTransferLength = hub->StatusBytes;
    sg.SgElement[0].SgOffset = 0;
    b = (PUCHAR)&nreq;
    for (i = 0; i < sizeof(nreq); i++) {
        b[i] = 0;
    }
    nreq.TransferLength = hub->StatusBytes;
    nreq.DirectionIn = 1;
    nreq.MaxPacketSize = pipe->MaxPacketSize;
    nreq.SgList = &sg;

    answer = XHCI_XFER_BUSY;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (!dev->Gone && !pipe->Closed && !pipe->Halted) {
        answer = XhciXferSubmitNormal(pipe->Queue, pipe->Ring, &nreq, 1,
                                      &dev->HubXfer, dev, trbs,
                                      XHCI_XFER_MAX_CONTROL_TRBS);
        if (answer == XHCI_XFER_OK) {
            dev->HubXferArmed = 1;
            XhciWriteDoorbell(&hc->Hc, dev->SlotId, pipe->Dci);
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (answer != XHCI_XFER_OK) {
        hub->Polled = 1;
        XHCI_DBG_VALUE("hcd: hub status pipe refused, polling, hub",
                       hub->Index);
    }
}

/*
 * What the hub has to say since the last pass, into hub->Changed: the
 * status-change report if one completed (a failed one counted towards the
 * pipe's giving-up), every port when the hub is polled and its turn has
 * come, and the hub's own change looked at here. Its ports are hcd_enum.c's
 * to look at, and the pipe is re-armed after them (HcdHubRearm).
 */
VOID HcdHubCollect(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;
    ULONG done;
    ULONG bytes;
    ULONG halted;
    LONG usbd;

    dev = hub->Device;
    if (dev == NULL || hub->Refused) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    done = dev->HubXferDone;
    dev->HubXferDone = 0;
    usbd = dev->HubXfer.UsbdStatus;
    bytes = dev->HubXfer.BytesTransferred;
    halted = (hub->Status != NULL) ? hub->Status->Halted : 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (done) {
        if (usbd == XHCI_USBD_STATUS_SUCCESS) {
            hub->StatusFailures = 0;
            hub->Changed |= XhciHubStatusBitmap(hcdHubReport(hc, hub),
                                                bytes, hub->Ports);
        } else {
            hub->StatusFailures++;
            XHCI_DBG_VALUE("hcd: hub status transfer failed, hub/status",
                           (hub->Index << 24) |
                               ((ULONG)usbd & 0x00FFFFFFUL));
            if (halted || hub->StatusFailures >= HCD_HUB_STATUS_TRIES) {
                hub->Polled = 1;
                XHCI_DBG_VALUE("hcd: hub status pipe given up, polling, hub",
                               hub->Index);
            }
            /* Whatever the report would have said is lost: look at all. */
            hub->Changed |= XhciHubAllBits(hub->Ports);
        }
    }
    if (hub->Polled && ++hub->PollPasses >= HCD_HUB_POLL_PASSES) {
        hub->PollPasses = 0;
        hub->Changed |= XhciHubAllBits(hub->Ports);
    }
    if ((hub->Changed & 1UL) != 0) {
        hub->Changed &= ~1UL;
        hcdHubSelf(hc, hub);
    }
}

/* ----------------------------------------------------------------------- */
/* Bring-up                                                                 */
/* ----------------------------------------------------------------------- */

/* The configuration's first interface number, the hub's one interface. */
static ULONG hcdHubInterface(PHCD_USB_DEVICE dev, PULONG number)
{
    ULONG at;
    ULONG len;

    at = 0;
    while (at + 2 <= dev->ConfigLength) {
        len = dev->Config[at];
        if (len < 2 || at + len > dev->ConfigLength) {
            return 0;
        }
        if (dev->Config[at + 1] == 4 && len >= 9) {
            *number = dev->Config[at + 2];
            return 1;
        }
        at += len;
    }
    return 0;
}

/*
 * GET_DESCRIPTOR(Hub): wValue 0x2900, and once more with 0x0000 - what both
 * Windows hub drivers send - after a STALL (section 10.1). The reply is
 * folded by the graph, whose fold counts it folded or malformed, and parsed
 * here; a reply the graph folded but the bus cannot serve (another type
 * byte, no ports) is counted malformed here instead, so each reply counts
 * once.
 */
static ULONG hcdHubDescriptor(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    PXHCI_TOPOLOGY topo;
    XHCI_SETUP_PACKET setup;
    XHCI_TOPO_SNOOP snoop;
    XHCI_TOPO_GONE gone;
    PUCHAR s;
    ULONG folded;
    ULONG bad;
    ULONG bytes;
    ULONG stalled;
    ULONG ok;

    topo = &hc->Hc.Topology;
    s = (PUCHAR)hc->ScratchVa;
    setup.bmRequestType = XHCI_HUB_RT_HUB_IN;
    setup.bRequest = XHCI_HUB_REQ_GET_DESCRIPTOR;
    setup.wValue = XHCI_HUB_DESC_VALUE;
    setup.wIndex = 0;
    setup.wLength = (USHORT)XHCI_HUB_DESC_REQUEST_BYTES;
    XhciTopoObserveSetup(topo, hub->SlotId, &setup, &snoop);
    ok = HcdThreadControlEx(hc, hub->Device, setup.bmRequestType,
                            setup.bRequest, setup.wValue, 0,
                            XHCI_HUB_DESC_REQUEST_BYTES, &bytes, &stalled);
    if (!ok && stalled) {
        setup.wValue = XHCI_HUB_DESC_VALUE_WINDOWS;
        XhciTopoObserveSetup(topo, hub->SlotId, &setup, &snoop);
        ok = HcdThreadControlEx(hc, hub->Device, setup.bmRequestType,
                                setup.bRequest, setup.wValue, 0,
                                XHCI_HUB_DESC_REQUEST_BYTES, &bytes,
                                &stalled);
    }
    if (!ok) {
        XHCI_DBG_VALUE("hcd: hub descriptor not read, slot", hub->SlotId);
        return 0;
    }
    folded = topo->Descriptors;
    bad = topo->DescriptorsBad;
    (VOID)XhciTopoObserveReply(topo, &snoop, s, bytes, &gone);
    hc->Counters.TopoDescriptors += topo->Descriptors - folded;
    hc->Counters.TopoDescriptorsBad += topo->DescriptorsBad - bad;
    if (XhciHubParseDescriptor(s, bytes, &hub->Desc) != XHCI_HUB_OK) {
        if (topo->DescriptorsBad == bad) {
            hc->Counters.TopoDescriptorsBad++;
        }
        XHCI_DBG_VALUE("hcd: hub descriptor malformed, bytes/type/ports",
                       (bytes << 16) | ((bytes > 1 ? (ULONG)s[1] : 0) << 8) |
                           (bytes > 2 ? (ULONG)s[2] : 0));
        return 0;
    }
    hub->Ports = hub->Desc.Managed;
    XHCI_DBG_VALUE("hcd: hub descriptor, ports/characteristics",
                   (hub->Desc.Ports << 16) | hub->Desc.Characteristics);
    return 1;
}

/*
 * Section 10.3 steps 1 to 3: SET_CONFIGURATION, the multi-TT alternate where
 * the hub has one, the hub descriptor, and the one Configure Endpoint that
 * marks the hub's slot and opens its status-change endpoint together.
 */
static ULONG hcdHubConfigure(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    PXHCI_TOPOLOGY topo;
    PHCD_USB_DEVICE dev;
    const XHCI_TOPO_NODE *node;
    XHCI_TOPO_HUBMARK mark;
    XHCI_PIPE_IFACE iface;
    XHCI_PIPE_EP ep;
    ULONG number;
    ULONG bytes;
    ULONG hasAlt1;

    topo = &hc->Hc.Topology;
    dev = hub->Device;
    if (dev->Config == NULL || dev->ConfigLength < XHCI_PIPE_CONFIG_BYTES ||
        !hcdHubInterface(dev, &number) || number >= 32) {
        return 0;
    }
    if (!HcdThreadControl(hc, dev, 0x00, XHCI_HUB_REQ_SET_CONFIG,
                          (USHORT)dev->Config[5], 0, 0, &bytes)) {
        XHCI_DBG_VALUE("hcd: hub SET_CONFIGURATION failed, slot",
                       dev->SlotId);
        return 0;
    }
    dev->ConfigValue = dev->Config[5];
    hasAlt1 = XhciPipeFindInterface(dev->Config, dev->ConfigLength, number,
                                    1, &iface) == XHCI_PIPE_OK;
    hub->Alternate = 0;
    if (XhciHubWantsMultiTt(hub->SpeedClass, dev->DeviceDesc[6], hasAlt1)) {
        if (HcdThreadControl(hc, dev, XHCI_HUB_RT_INTERFACE,
                             XHCI_HUB_REQ_SET_INTERFACE, 1, (USHORT)number,
                             0, &bytes)) {
            hub->Alternate = 1;
            XhciTopoApplySetInterface(topo, hub->SlotId, 1);
        } else {
            /* Refused: the hub runs its single TT, and the graph is told
             * nothing, so its MTT reads never enabled. */
            XHCI_DBG_VALUE("hcd: hub multi-TT alternate refused, slot",
                           dev->SlotId);
        }
    }
    dev->Alternate[number] = (UCHAR)hub->Alternate;

    if (!hcdHubDescriptor(hc, hub)) {
        return 0;
    }
    node = XhciTopoFind(topo, hub->SlotId);
    if (!XhciTopoHubMark(node, hub->SpeedClass, &mark)) {
        XHCI_DBG_VALUE("hcd: hub not markable, slot", dev->SlotId);
        return 0;
    }
    if (XhciPipeFindInterface(dev->Config, dev->ConfigLength, number,
                              hub->Alternate, &iface) != XHCI_PIPE_OK ||
        iface.EndpointCount < 1 ||
        XhciPipeEndpointParams(dev->Config + iface.EndpointOffset[0],
                               dev->Speed, &ep) != XHCI_PIPE_OK ||
        ep.TransferType != XHCI_PIPE_XFER_INTERRUPT || !ep.DirectionIn) {
        XHCI_DBG_VALUE("hcd: hub has no status-change endpoint, slot",
                       dev->SlotId);
        return 0;
    }
    dev->HubPorts = mark.NumberOfPorts;
    dev->HubTtt = mark.TtThinkTime;
    dev->HubMtt = mark.MultiTt;
    dev->HubMarked = 1;
    hub->Status = HcdCfgHubOpen(hc, dev, &ep, number);
    if (hub->Status == NULL) {
        dev->HubMarked = 0;
        return 0;
    }
    hc->Counters.TopoHubSlotsMarked++;
    hub->StatusBytes = XhciHubStatusBytes(hub->Desc.Ports);
    XHCI_DBG_VALUE("hcd: hub slot marked, slot/MTT/ports/TTT",
                   (dev->SlotId << 24) | (mark.MultiTt << 16) |
                       (mark.NumberOfPorts << 8) | mark.TtThinkTime);
    return 1;
}

/* The hub's node goes from the graph, with every node below it, and the
 * hub object forgets its device. The device keeps nothing of the object.
 * The caller decides when the object itself is freed (HcdHubFree). */
VOID HcdHubForget(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    if (hub->SlotId != 0) {
        XhciTopoDetach(&hc->Hc.Topology, hub->SlotId);
    }
    if (hub->Device != NULL && hub->Device->Hub == hub) {
        hub->Device->Hub = NULL;
    }
    hub->Device = NULL;
    hub->SlotId = 0;
    hub->Status = NULL;
}

/* The object and its port objects back to unused. Its ports hold nothing
 * by now: the caller has dropped or settled every one. */
VOID HcdHubFree(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    PHCD_PORT q;
    PUCHAR b;
    ULONG index;
    ULONG n;
    ULONG i;

    for (n = 1; n <= HCD_HUB_MAX_PORTS; n++) {
        q = HcdHubPort(hc, hub, n);
        XhciEnumReset(&q->Enum);
        q->Device = NULL;
        q->AwaitSerial = 0;
        q->AwaitHub = NULL;
        q->Hub = NULL;
        q->Number = n;
    }
    index = hub->Index;
    b = (PUCHAR)hub;
    for (i = 0; i < sizeof(*hub); i++) {
        b[i] = 0;
    }
    hub->Index = index;
}

/*
 * A hub reached Present on port p (hcd_enum.c): bring it up (the file
 * header). Returns 1 when the hub is the bus's - served, or refused as too
 * deep and left addressed and unconfigured, which is not a failure of the
 * port: the machine goes on to Bound with no PDO. Returns 0, with the hub's
 * node and object undone, when the hub could not be brought up; the
 * machine then fails and gives the slot back.
 */
ULONG HcdHubStart(PHCD_CONTROLLER hc, PHCD_PORT p, PHCD_USB_DEVICE dev)
{
    PXHCI_TOPOLOGY topo;
    const XHCI_TOPO_NODE *node;
    XHCI_TOPO_CHILD child;
    PHCD_HUB hub;
    PHCD_PORT q;
    ULONG dropped;
    ULONG cls;
    ULONG ok;
    ULONG n;
    ULONG i;

    topo = &hc->Hc.Topology;
    cls = XHCI_SPEED_UNKNOWN;
    (VOID)XhciPortSpeedClass(&hc->Hc.PortMap, dev->Port, dev->Speed, &cls);
    dropped = topo->Dropped;
    if (p->Hub == NULL) {
        ok = XhciTopoAttachRoot(topo, dev->SlotId, dev->Port, 0, cls);
    } else {
        ok = p->Hub->Device != NULL &&
             XhciTopoChildOf(topo, p->Hub->SlotId, p->Number, &child) &&
             XhciTopoAttachChild(topo, dev->SlotId, &child, cls);
    }
    if (topo->Dropped != dropped) {
        hc->Counters.TopoNodesDropped += topo->Dropped - dropped;
    }
    if (!ok) {
        XHCI_DBG_VALUE("hcd: hub not in the graph, slot", dev->SlotId);
        return 0;
    }
    hub = NULL;
    for (i = 0; i < HCD_MAX_HUBS; i++) {
        if (!hc->Hubs[i].Used) {
            hub = &hc->Hubs[i];
            break;
        }
    }
    if (hub == NULL) {
        /* Every object is held - some by departed hubs whose subtrees PnP
         * has not yet let go of: a node the bus cannot keep, as a full
         * graph is. */
        hc->Counters.TopoNodesDropped++;
        XhciTopoDetach(topo, dev->SlotId);
        return 0;
    }
    node = XhciTopoFind(topo, dev->SlotId);
    hub->Used = 1;
    hub->Index = (ULONG)(hub - hc->Hubs);
    hub->Device = dev;
    hub->Upstream = p;
    hub->SlotId = dev->SlotId;
    hub->SpeedClass = cls;
    hub->Tier = (node != NULL) ? node->Tier : 0;
    dev->Hub = hub;

    if (!XhciHubTierServable(hub->Tier)) {
        hub->Refused = 1;
        hc->Counters.TopoBehindHubTooDeep++;
        XHCI_DBG_VALUE("hcd: hub too deep, not configured, slot/tier",
                       (dev->SlotId << 8) | hub->Tier);
        return 1;
    }
    hub->Usb3 = cls == XHCI_SPEED_SUPER;
    if (hub->Usb3 ? !HcdSsHubConfigure(hc, hub) : !hcdHubConfigure(hc, hub)) {
        HcdHubForget(hc, hub);
        HcdHubFree(hc, hub);
        return 0;
    }
    for (n = 1; n <= hub->Ports; n++) {
        q = HcdHubPort(hc, hub, n);
        XhciEnumReset(&q->Enum);
        q->Device = NULL;
        q->AwaitSerial = 0;
        q->AwaitHub = NULL;
        q->Hub = hub;
        q->Number = n;
        if (!hcdHubFeature(hc, hub, n, 1, XHCI_HUB_FEAT_PORT_POWER)) {
            XHCI_DBG_VALUE("hcd: hub port not powered, hub/port",
                           (hub->Index << 8) | n);
        }
    }
    hcdHubDelay(XhciHubPowerWaitMs(hub->Desc.PowerGoodMs));
    /* Every port looked at once: a device present at power-on need raise
     * no change (section 10.3 step 4). */
    hub->Changed = XhciHubAllBits(hub->Ports);
    HcdHubRearm(hc, hub);
    hc->Counters.HubsStarted++;
    HcdSsHubCountPair(hc, hub);
    XHCI_DBG_VALUE("hcd: hub started, slot/tier/ports",
                   (dev->SlotId << 16) | (hub->Tier << 8) | hub->Ports);
    return 1;
}

/* hcdHubRequest for the SuperSpeed half (hcd_sshub.c): one hub-class
 * request on the hub's default pipe, seen by the graph. */
ULONG HcdHubClassRequest(PHCD_CONTROLLER hc, PHCD_HUB hub, UCHAR type,
                         UCHAR request, USHORT value, USHORT index,
                         ULONG length, PULONG bytes, PULONG stalled)
{
    return hcdHubRequest(hc, hub, type, request, value, index, length, bytes,
                         stalled);
}
