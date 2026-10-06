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
 * Removal (27-A.3) is hcd_enum.c's one teardown (hcdSubtreeGo); this file
 * adds what the hub owes its ports' devices on the way: CLEAR_TT_BUFFER for
 * a halted control or bulk endpoint behind a TT and after an Address Device
 * transaction error there (HcdHubClearTt), and CLEAR_FEATURE(PORT_ENABLE)
 * on a port given up after its attempts (HcdHubPortDisable).
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
/* The debounce reads the port every 25 ms and gives up on a connection
 * that has not held still for TATTDB within 1.5 s - bus policy numbers. */
#define HCD_HUB_DEBOUNCE_STEP_MS    25UL
#define HCD_HUB_DEBOUNCE_LIMIT_MS   1500UL
/* A polled hub is looked at every this many thread passes (hcd_ctl.c wakes
 * the thread at least every 100 ms). */
#define HCD_HUB_POLL_PASSES         3UL
/* Status-change completions in a row that may fail before the pipe is
 * given up (section 10.1: "a second consecutive failure"). */
#define HCD_HUB_STATUS_TRIES        2UL
/* A given-up SuperSpeed hub port's re-arm (HcdHubPortDisable): one tick a
 * second, the first re-arm after 2 ticks, doubling with each re-arm in a
 * row, never more than 64 apart - bus policy numbers. */
#define HCD_SSHUB_REARM_TICK_MS     1000UL
#define HCD_SSHUB_REARM_FIRST       2UL
#define HCD_SSHUB_REARM_CAP         64UL

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
    /* Table 6-6's two fields: a TT's for an LS/FS device behind a
     * High-Speed hub, or a higher-rank SuperSpeed hub's for an SS/SSP
     * device (HcdHubPlace); never both, since one device is of one kind. */
    sp->ParentSlotId = dev->TtSlot != 0 ? dev->TtSlot : dev->SsParentSlot;
    sp->ParentPortNumber = dev->TtSlot != 0 ? dev->TtPort
                                            : dev->SsParentPort;
    sp->MultiTt = dev->TtMulti;
    if (withHub && dev->HubMarked) {
        sp->Hub = 1;
        sp->NumberOfPorts = dev->HubPorts;
        sp->TtThinkTime = dev->HubTtt;
        sp->MultiTt |= dev->HubMtt;
    }
}

/*
 * The device's speed in the fixed encoding the endpoint policy takes
 * (XHCI_PIPE_SPEED_*, xhci_pipe.h), decoded from the Protocol Speed ID its
 * Slot Context carries: a controller may advertise its own PSIVs (xHCI
 * 7.2.1), so dev->Speed is not that encoding, and only the Slot Context may
 * take it raw (Codex review of 23e7715, finding 4). SuperSpeedPlus apart
 * from SuperSpeed by the trained rate (dev->Plus, 29-A.1, 29-A.6). 0, which
 * every endpoint refuses, for a speed the root port's protocol does not
 * name. The one decode for every endpoint rule (Phase 29's hcdCfgPipeSpeed
 * folded in here). IRQL: any.
 */
ULONG HcdDevicePipeSpeed(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    ULONG cls;

    cls = XHCI_SPEED_UNKNOWN;
    (VOID)XhciPortSpeedClass(&hc->Hc.PortMap, dev->Port, dev->Speed, &cls);
    if (cls == XHCI_SPEED_SUPER) {
        /* The class alone cannot tell SuperSpeedPlus from SuperSpeed
         * (XhciPipeSpeedFromClass answers neither): the trained rate
         * does. */
        return dev->Plus ? XHCI_PIPE_SPEED_SUPER_PLUS : XHCI_PIPE_SPEED_SUPER;
    }
    return XhciPipeSpeedFromClass(cls);
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
    /* Behind a SuperSpeed hub: the device's link rank from the hub's
     * extended port status (read at the port's reset, hcd_sshub.c), and
     * xHCI Table 6-6's Parent Hub Slot ID and Parent Port Number when the
     * hub's own upstream link (hub->Device->SsLinkRank) outranks it - a
     * Gen 1x1 device behind a Gen 1x2 hub - or the hub's own pair when the
     * device ranks the same as the hub, the boundary being further up.
     * Either rank unknown leaves both 0, a best-effort fallback
     * (XhciSsParentNeeded says why). Implemented, host vectors only: no
     * SuperSpeedPlus hub is held (xhci-data-structures.md sections 10.5
     * and 11.8). A USB 2.0 hub decides nothing here; its TT is below. */
    dev->SsLinkRank = 0;
    dev->SsParentSlot = 0;
    dev->SsParentPort = 0;
    (VOID)XhciSsHubParentOf(hub->Usb3,
                            hub->Device->BosInfo.HasSuperSpeedPlus,
                            hub->Device->SsLinkRank,
                            hub->Device->SsParentSlot,
                            hub->Device->SsParentPort, &p->HubSsLink,
                            hub->SlotId, p->Number, &dev->SsLinkRank,
                            &dev->SsParentSlot, &dev->SsParentPort);
    XHCI_DBG_VALUE("hcd: behind hub, SS rank hub/device, parent slot/port",
                   (hub->Device->SsLinkRank << 24) | (dev->SsLinkRank << 16) |
                       (dev->SsParentSlot << 8) | dev->SsParentPort);
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

/*
 * The attach debounce on a hub port (section 10.2 step 2): the connection
 * must hold, unchanged, for TATTDB. The port is read every
 * HCD_HUB_DEBOUNCE_STEP_MS; a connect change seen meanwhile is a bounce,
 * cleared here - so it does not start the device over once it is
 * enumerated - and starts the interval again, as does a read that finds the
 * port disconnected (Codex review of 23e7715, finding 7). Returns 1 once
 * connected and stable; 0 once stably disconnected, once
 * HCD_HUB_DEBOUNCE_LIMIT_MS have elapsed without a stable connection, or
 * when the hub does not answer. The limit is soft: it is checked between
 * reads, and a GET_STATUS already sent can run to the control transfer's
 * own 5 s time-out (HCD_TRANSFER_WAIT_MS) before the check comes round.
 */
ULONG HcdHubPortDebounce(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n)
{
    KTIMER deadline;
    LARGE_INTEGER due;
    LARGE_INTEGER now;
    ULONG status;
    ULONG change;
    ULONG stable;
    ULONG connected;
    ULONG was;
    ULONG answer;

    /* The give-up bound is elapsed time, the class requests and the
     * scheduling included, not a sum of the sleeps asked for: a timer,
     * polled with a zero wait, as hcdEp0Quiet bounds its wait (Codex review
     * of d54eef0, finding 4). */
    KeInitializeTimer(&deadline);
    HcdRelativeMs(&due, HCD_HUB_DEBOUNCE_LIMIT_MS);
    (VOID)KeSetTimer(&deadline, due, NULL);
    now.QuadPart = 0;
    stable = 0;
    was = 1;
    answer = 0;
    for (;;) {
        if (KeWaitForSingleObject(&deadline, Executive, KernelMode, FALSE,
                                  &now) == STATUS_SUCCESS) {
            XHCI_DBG_VALUE("hcd: hub port never stable, hub/port",
                           (hub->Index << 8) | n);
            break;
        }
        hcdHubDelay(HCD_HUB_DEBOUNCE_STEP_MS);
        if (!HcdHubPortStatus(hc, hub, n, &status, &change)) {
            break;
        }
        connected = (status & XHCI_HUB_PORT_CONNECTION) != 0;
        if ((change & XHCI_HUB_C_PORT_CONNECTION) != 0) {
            (VOID)hcdHubFeature(hc, hub, n, 0,
                                XHCI_HUB_FEAT_C_PORT_CONNECTION);
            stable = 0;
        } else if (connected != was) {
            stable = 0;
        } else {
            stable += HCD_HUB_DEBOUNCE_STEP_MS;
        }
        was = connected;
        if (stable >= HCD_HUB_DEBOUNCE_MS) {
            answer = connected;
            break;
        }
    }
    (VOID)KeCancelTimer(&deadline);
    return answer;
}

/* An endpoint's state in the output Device Context (xHCI 6.2.3). */
static ULONG hcdHubEpState(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           ULONG dci)
{
    ULONG offset;

    if (XhciEndpointContextOffset(&hc->Hc.Layout, dev->SlotId, dci,
                                  &offset) != XHCI_LAYOUT_OK) {
        return XHCI_EP_STATE_DISABLED;
    }
    return XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]);
}

/* Whether a device record's path to the root passes hub port q: on q
 * itself, or below a hub somewhere beneath it. */
static ULONG hcdHubBelow(PHCD_CONTROLLER hc, PHCD_PORT q, PHCD_USB_DEVICE dev)
{
    PHCD_PORT p;
    ULONG hops;

    if (dev->Location == 0 || dev->Location > HCD_PORT_COUNT) {
        return 0;
    }
    p = &hc->Ports[dev->Location - 1];
    if (p->Device != dev) {
        return 0;
    }
    for (hops = 0; p != NULL && hops <= XHCI_TOPO_MAX_TIER + 1UL; hops++) {
        if (p == q) {
            return 1;
        }
        if (p->Hub == NULL) {
            return 0;
        }
        p = p->Hub->Upstream;
    }
    return 0;
}

/* Whether endpoint `pipe` has work on its ring: its own, or, with streams
 * open (31-A.1), any stream's - an endpoint with streams takes no transfer
 * itself, and a Stop Endpoint stops every stream. Controller lock held. */
static ULONG hcdHubPipeQueued(PHCD_PIPE pipe)
{
    ULONG id;

    if (pipe->Queue->Count != 0) {
        return 1;
    }
    for (id = 1; pipe->Streams != NULL && id <= pipe->Streams->Count;
         id++) {
        if (pipe->Streams->Pipe[id] != NULL &&
            pipe->Streams->Pipe[id]->Queue->Count != 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * No traffic reaches the devices on hub port q - the device there and, when
 * it is a hub, every device below it - from before the bus resumes the port
 * until after the resume recovery: USB 2.0 7.1.7.7 allows no device access
 * on the resumed segment during TRSMRCY (to transcribe; Codex review of the
 * Phase 27 integration, round 4, finding 1). Every pipe of each such device
 * is paused, so nothing is published, and every endpoint with work on its
 * ring that is Running is stopped (Stop Endpoint), so nothing already
 * published runs; the devices go into devs[]. A Stop Endpoint that fails
 * requests the controller reset, whose invalidation settles the devices.
 * Returns how many devices were quiesced. Thread only, powered.
 */
static ULONG hcdHubQuiesceBelow(PHCD_CONTROLLER hc, PHCD_PORT q,
                                PHCD_USB_DEVICE *devs)
{
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG count;
    ULONG queued;
    ULONG control;
    ULONG code;
    ULONG dci;
    ULONG i;

    count = 0;
    for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
        dev = hc->SlotDevice[i];
        if (dev == NULL || dev->Gone || !hcdHubBelow(hc, q, dev)) {
            continue;
        }
        devs[count++] = dev;
        for (dci = 1; dci < 32; dci++) {
            pipe = (dci == 1) ? &dev->Ep0Pipe : dev->Pipes[dci];
            if (pipe != NULL) {
                HcdIoPipePause(hc, pipe);
            }
        }
        for (dci = 1; dci < 32; dci++) {
            pipe = (dci == 1) ? &dev->Ep0Pipe : dev->Pipes[dci];
            if (pipe == NULL || hc->Hc.ControllerFailed) {
                continue;
            }
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            queued = hcdHubPipeQueued(pipe);
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            if (!queued ||
                hcdHubEpState(hc, dev, dci) != XHCI_EP_STATE_RUNNING) {
                continue;
            }
            code = 0;
            if (XhciTrbStopEndpoint(&trb, dev->SlotId, dci, 0) ==
                XHCI_RING_OK) {
                code = HcdThreadCommand(hc, &trb, &control);
            }
            if (code != XHCI_CC_SUCCESS &&
                code != XHCI_CC_CONTEXT_STATE_ERROR) {
                HcdSvcRequestReset(&hc->Hc);
            }
        }
    }
    return count;
}

/* The devices hcdHubQuiesceBelow held, let go after the recovery: each
 * stopped endpoint with work on its ring is rung again - under the lock the
 * teardown's freeze sets Gone under, so a device leaving meanwhile is not -
 * and every pipe resumed, which publishes what was held. An endpoint with
 * streams open is rung once per stream with work, by its Stream ID (31-A.1;
 * Stream ID 0 rings none of them); a ring of surviving TDs moves the
 * device's sequence (SeqUsed, as hcd_cfg.c's restarts). Thread only. */
static VOID hcdHubReleaseBelow(PHCD_CONTROLLER hc, PHCD_USB_DEVICE *devs,
                               ULONG count)
{
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    PHCD_PIPE p;
    KIRQL oldIrql;
    ULONG state;
    ULONG dci;
    ULONG id;
    ULONG i;

    for (i = 0; i < count; i++) {
        dev = devs[i];
        for (dci = 1; dci < 32; dci++) {
            pipe = (dci == 1) ? &dev->Ep0Pipe : dev->Pipes[dci];
            if (pipe == NULL) {
                continue;
            }
            state = hcdHubEpState(hc, dev, dci);
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            if (!dev->Gone && state == XHCI_EP_STATE_STOPPED) {
                if (pipe->Queue->Count != 0) {
                    pipe->SeqUsed = 1;
                    XhciWriteDoorbell(&hc->Hc, dev->SlotId, dci);
                }
                for (id = 1;
                     pipe->Streams != NULL && id <= pipe->Streams->Count;
                     id++) {
                    p = pipe->Streams->Pipe[id];
                    if (p != NULL && p->Queue->Count != 0) {
                        pipe->SeqUsed = 1;
                        XhciWriteDoorbell(&hc->Hc, dev->SlotId,
                                          XhciStreamDoorbell(dci, id));
                    }
                }
            }
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            HcdIoPipeResume(hc, pipe);
        }
    }
}

/*
 * A suspended hub port resumed (xhci_hub.h, "handled, never initiated"):
 * the devices on it quiesced (hcdHubQuiesceBelow), ClearPortFeature
 * (PORT_SUSPEND), the hub's resume signalling waited out - polled from
 * TDRSMDN on until an elapsed deadline of XHCI_HUB_RESUME_WAIT_MS, soft as
 * the debounce's is (a GET_STATUS already sent may run to the control
 * transfer's own time-out) - its C_PORT_SUSPEND cleared, the resume
 * recovery TRSMRCY waited, and only then the devices let go. Returns the
 * outcome (XHCI_HUB_RESUME_*): DONE, DISABLED or GONE by the port's status
 * (XhciHubResumeProgress), STUCK when a request failed, the deadline
 * passed, or the controller needs its recovery. Only DONE lets the held
 * devices go; on any other outcome they stay held, *held says how many,
 * and the caller's teardown frees them with their pipes still paused (or
 * the invalidation does, once the controller has failed): a resume that
 * may still finish must never meet their traffic before its recovery, and
 * nothing more is asked of a controller whose recovery is due (Codex
 * review of the Phase 27 integration, round 5, finding 1).
 *
 * On a SuperSpeed hub's port the same resume brings a link found in U3 back
 * to U0: SetPortFeature(PORT_LINK_STATE) with U0 in wIndex 15:8 in place of
 * ClearPortFeature(PORT_SUSPEND), its progress read by
 * XhciSsHubResumeProgress, and C_PORT_LINK_STATE cleared in place of
 * C_PORT_SUSPEND; the quiesce, the deadline, the recovery wait and the
 * outcome are the USB 2.0 port's (USB 3.2 r1.1 10.16.2.6 and 10.16.2.10,
 * printed pp.446-454, read in the merge's Codex review; the
 * Phase 27 and Phase 30 merge, p28-31-int). No SuperSpeed port is resumed
 * before a reset: a warm reset may start from U3 (XhciSsHubResetKind).
 * Thread only, powered.
 */
static ULONG hcdHubPortResume(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                              PULONG heldOut)
{
    PHCD_USB_DEVICE devs[XHCI_MAX_SLOTS];
    KTIMER deadline;
    LARGE_INTEGER due;
    LARGE_INTEGER now;
    ULONG status;
    ULONG change;
    ULONG progress;
    ULONG held;
    ULONG asked;

    held = hcdHubQuiesceBelow(hc, HcdHubPort(hc, hub, n), devs);
    progress = XHCI_HUB_RESUME_STUCK;
    if (hc->Hc.ControllerFailed || hc->ScratchTainted) {
        goto done;
    }
    if (hub->Usb3) {
        asked = hcdHubFeature(hc, hub, n | (XHCI_SSHUB_LINK_U0 << 8), 1,
                              XHCI_SSHUB_FEAT_PORT_LINK_STATE);
    } else {
        asked = hcdHubFeature(hc, hub, n, 0, XHCI_HUB_FEAT_PORT_SUSPEND);
    }
    if (!asked) {
        goto done;
    }
    hc->HubResumes++;
    KeInitializeTimer(&deadline);
    HcdRelativeMs(&due, XHCI_HUB_RESUME_WAIT_MS);
    (VOID)KeSetTimer(&deadline, due, NULL);
    now.QuadPart = 0;
    hcdHubDelay(XHCI_HUB_RESUME_FIRST_MS);
    change = 0;
    for (;;) {
        if (!HcdHubPortStatus(hc, hub, n, &status, &change)) {
            progress = XHCI_HUB_RESUME_STUCK;
            break;
        }
        progress = hub->Usb3 ? XhciSsHubResumeProgress(status)
                             : XhciHubResumeProgress(status);
        if (progress != XHCI_HUB_RESUME_PENDING) {
            break;
        }
        if (KeWaitForSingleObject(&deadline, Executive, KernelMode, FALSE,
                                  &now) == STATUS_SUCCESS) {
            progress = XHCI_HUB_RESUME_STUCK;
            break;
        }
        hcdHubDelay(HCD_HUB_POLL_STEP_MS);
    }
    (VOID)KeCancelTimer(&deadline);
    if (hub->Usb3) {
        if ((change & XHCI_SSHUB_C_PORT_LINK_STATE) != 0) {
            (VOID)hcdHubFeature(hc, hub, n, 0,
                                XHCI_SSHUB_FEAT_C_PORT_LINK_STATE);
        }
    } else if ((change & XHCI_HUB_C_PORT_SUSPEND) != 0) {
        (VOID)hcdHubFeature(hc, hub, n, 0, XHCI_HUB_FEAT_C_PORT_SUSPEND);
    }
    /* A connect change seen during the resume is a replaced device: it is
     * enumerated afresh through this outcome (DISABLED), and its change is
     * cleared here so the next look does not do it twice. */
    if (progress != XHCI_HUB_RESUME_STUCK &&
        (change & XHCI_HUB_C_PORT_CONNECTION) != 0) {
        (VOID)hcdHubFeature(hc, hub, n, 0, XHCI_HUB_FEAT_C_PORT_CONNECTION);
    }
    progress = XhciHubResumeSettle(progress, change);
    if (progress == XHCI_HUB_RESUME_DONE) {
        hcdHubDelay(XHCI_HUB_RESUME_RECOVERY_MS);
    }
done:
    XHCI_DBG_VALUE("hcd: hub port resumed by the bus, hub/port/outcome",
                   (hub->Index << 16) | (n << 8) | progress);
    if (progress == XHCI_HUB_RESUME_DONE && !hc->Hc.ControllerFailed &&
        !hc->ScratchTainted) {
        hcdHubReleaseBelow(hc, devs, held);
        held = 0;
    }
    *heldOut = held;
    return progress;
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
    ULONG held;
    ULONG status;
    ULONG change;
    ULONG waited;
    ULONG progress;

    if (hub->Usb3) {
        return HcdSsHubPortReset(hc, hub, n, speedClass);
    }
    *speedClass = XHCI_SPEED_UNKNOWN;
    if (!HcdHubPortStatus(hc, hub, n, &status, &change) ||
        (status & XHCI_HUB_PORT_CONNECTION) == 0) {
        return 0;
    }
    /* A suspended port is resumed before it is reset (xhci_hub.h). */
    /* A device the port still holds - a RESET_PORT's - and failing to
     * resume stays held: the failed reset cycles it, and its teardown frees
     * it (hcd_enum.c, HcdEnumCycle). A resume that finishes clears the
     * port's resume debt (round 5, finding 3). */
    if (XhciHubResumeBeforeReset(status)) {
        if (hcdHubPortResume(hc, hub, n, &held) != XHCI_HUB_RESUME_DONE ||
            !HcdHubPortStatus(hc, hub, n, &status, &change)) {
            return 0;
        }
        HcdHubPort(hc, hub, n)->ResumeTries = 0;
        HcdHubPort(hc, hub, n)->ResumePending = 0;
    }
    /* An older reset's change cleared first, so the one that ends this
     * reset is this reset's (XhciHubResetProgress). */
    if ((change & XHCI_HUB_C_PORT_RESET) != 0 &&
        !hcdHubFeature(hc, hub, n, 0, XHCI_HUB_FEAT_C_PORT_RESET)) {
        return 0;
    }
    if (!hcdHubFeature(hc, hub, n, 1, XHCI_HUB_FEAT_PORT_RESET)) {
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
    PHCD_USB_DEVICE devs[XHCI_MAX_SLOTS];
    PHCD_PORT q;
    ULONG outcome;
    ULONG held;
    ULONG status;
    ULONG change;
    ULONG bit;
    ULONG selector;

    q = HcdHubPort(hc, hub, n);
    if (hub->Usb3) {
        /* A SuperSpeed hub's port is decided by its own rules (hcd_sshub.c),
         * which name a link found in U3 under a held device (Resume) and a
         * finished U3 exit (Suspended) in this decision's shape; both are
         * then carried out below exactly as a USB 2.0 hub port's are
         * (Phase 27 and Phase 30 merged, p28-31-int). */
        if (!HcdSsHubPortLook(hc, hub, n, state, d)) {
            if (!q->ResumePending) {
                return 0;
            }
            d->Disconnect = 0;
            d->Connect = 0;
            XhciHubResumeOutcome(state, XHCI_HUB_RESUME_STUCK, 0,
                                 &q->ResumeTries, d);
            goto resumed;
        }
    } else {
        if (!HcdHubPortStatus(hc, hub, n, &status, &change)) {
            if (!q->ResumePending) {
                return 0;
            }
            /* A resume owed from an earlier look is not lost with this
             * look's GET_STATUS: it counts as one more that did not
             * finish, and is retried or given up as one (Codex review of
             * the Phase 27 integration, round 5, finding 2). Nothing was
             * held: a retried resume holds nothing. */
            XhciHubPortDecide(state, 0, 0, d);
            d->Disconnect = 0;
            d->Connect = 0;
            XhciHubResumeOutcome(state, XHCI_HUB_RESUME_STUCK, 0,
                                 &q->ResumeTries, d);
            goto resumed;
        }
        XhciHubPortDecide(state, status, change, d);
        HcdTolLocObserve(hc, q, (status & XHCI_HUB_PORT_CONNECTION) != 0,
                         (status & XHCI_HUB_PORT_POWER) != 0,
                         (change & XHCI_HUB_C_PORT_CONNECTION) != 0);
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
            /* The device's absence until it is seen again is the
             * over-current's and this repower's, not an unplug: it must
             * not re-arm a spent budget or release a hold (35-T.5). */
            HcdTolLocRecovery(hc, q);
            (VOID)hcdHubFeature(hc, hub, n, 1, XHCI_HUB_FEAT_PORT_POWER);
        }
    }
    if (!d->Resume) {
        /* Not suspended now - resumed between looks, or the device gone or
         * new: whatever resume was owed is settled (round 5, finding 3). */
        q->ResumeTries = 0;
        q->ResumePending = 0;
    }
    if (d->Resume) {
        /* Reported suspended: resumed before anything else is asked of the
         * device (handled, never initiated; xhci_hub.h). Its outcome
         * decides the port again; one that did not finish keeps its bit
         * for the next pass, until it has failed XHCI_HUB_RESUME_TRIES
         * times and the port is enumerated afresh (Codex review of the
         * Phase 27 integration, round 4, findings 2 and 3). */
        outcome = hcdHubPortResume(hc, hub, n, &held);
        XhciHubResumeOutcome(state, outcome, held, &q->ResumeTries, d);
        goto resumed;
    } else if (d->Suspended) {
        /* A resume finished - at USB 2.0 a device's remote wake among them
         * (at SuperSpeed a remote wake raises no change bit): the
         * device stays as it is, untouched until the resume recovery has
         * passed, and not let go at all once the controller needs its
         * recovery (the invalidation settles it). */
        held = hcdHubQuiesceBelow(hc, q, devs);
        hcdHubDelay(XHCI_HUB_RESUME_RECOVERY_MS);
        if (!hc->Hc.ControllerFailed && !hc->ScratchTainted) {
            hcdHubReleaseBelow(hc, devs, held);
        }
        XHCI_DBG_VALUE("hcd: hub port resumed, hub/port",
                       (hub->Index << 8) | n);
    }
    return 1;

resumed:
    q->ResumePending = d->Retry;
    if (d->Retry) {
        hub->Changed |= 1UL << n;
    }
    if (d->GaveUp) {
        q->ResumePending = 0;
        hc->HubResumesFailed++;
        XHCI_DBG_VALUE("hcd: hub port resume given up, hub/port",
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

/*
 * Whether a failure on the device on port q may be counted: 1 unless some
 * port on its path confirms the device has left (Codex review of d54eef0,
 * finding 2). Each hub port above it is read with GET_STATUS; a reply that
 * says disconnected, disabled, or with a connect or enable change raised
 * is a confirmed departure, as is a hub already departing (Draining, or no
 * device). A hub that does not answer confirms nothing for its port - its
 * own departure, if that is why, is confirmed further up - and neither does
 * a pending status-change report or Changed bit, which may name another
 * port. The root port the path starts at confirms a departure when it reads
 * disconnected or with a connect change; a PORTSC of all ones could not be
 * read and confirms nothing (Codex review round 25). The reads clear no
 * change bit - the hub service acts on each - but a GET_STATUS reply is
 * folded into the topology graph as every hub request's is (hcdHubRequest).
 * Once the controller has failed or a timed-out transfer may still DMA into
 * the scratch (a GET_STATUS here can be what requested that recovery), no
 * further hub is asked: the probe stops and the failure is counted, since
 * nothing more was learnt (Codex review of the Phase 27 integration, round
 * 3). Thread only, powered.
 */
ULONG HcdHubPathPresent(PHCD_CONTROLLER hc, PHCD_PORT q)
{
    PHCD_HUB hub;
    ULONG status;
    ULONG change;
    ULONG portsc;
    ULONG hops;

    for (hops = 0; q != NULL && q->Hub != NULL; hops++) {
        hub = q->Hub;
        if (hops > XHCI_TOPO_MAX_TIER) {
            return 1;
        }
        if (hub->Draining || hub->Device == NULL) {
            return 0;
        }
        if (hc->Hc.ControllerFailed || hc->ScratchTainted) {
            return 1;
        }
        if (HcdHubPortStatus(hc, hub, q->Number, &status, &change) &&
            ((status & XHCI_HUB_PORT_CONNECTION) == 0 ||
             (status & XHCI_HUB_PORT_ENABLE) == 0 ||
             (change & (XHCI_HUB_C_PORT_CONNECTION |
                        XHCI_HUB_C_PORT_ENABLE)) != 0)) {
            return 0;
        }
        q = hub->Upstream;
    }
    if (q == NULL) {
        return 1;
    }
    portsc = XhciReadPortsc(&hc->Hc, q->PortId);
    return portsc == 0xFFFFFFFFUL ||
           ((portsc & XHCI_PORTSC_CCS) != 0 &&
            (portsc & XHCI_PORTSC_CSC) == 0);
}

/* The hub's re-arm timer cancelled and every port's pending re-arm
 * forgotten: whenever the hub stops being served (HcdHubForget, and
 * HcdHubFree), whether or not its object is kept for PnP's removals - a
 * queued KTIMER must never outlive the controller extension that holds it
 * (Codex review of the Phase 28-31 integration, round 2, finding A). */
static VOID hcdHubRearmStop(PHCD_HUB hub)
{
    if (hub->RearmArmed) {
        (VOID)KeCancelTimer(&hub->RearmTimer);
        hub->RearmArmed = 0;
    }
    hub->RearmPorts = 0;
}

/* A port's next re-arm wait: HCD_SSHUB_REARM_FIRST ticks doubled with each
 * re-arm or failed re-arm in a row, never more than HCD_SSHUB_REARM_CAP. */
static VOID hcdHubRearmBackoff(PHCD_PORT q)
{
    q->HubSsRearmWait = HCD_SSHUB_REARM_FIRST << q->HubSsRearms;
    if (q->HubSsRearmWait >= HCD_SSHUB_REARM_CAP) {
        q->HubSsRearmWait = HCD_SSHUB_REARM_CAP;
    } else {
        q->HubSsRearms++;
    }
}

/* The hub's re-arm timer set for one tick, unless it is set already. */
static VOID hcdHubRearmTick(PHCD_HUB hub)
{
    LARGE_INTEGER due;

    if (hub->RearmArmed) {
        return;
    }
    KeInitializeTimer(&hub->RearmTimer);
    HcdRelativeMs(&due, HCD_SSHUB_REARM_TICK_MS);
    (VOID)KeSetTimer(&hub->RearmTimer, due, NULL);
    hub->RearmArmed = 1;
}

/*
 * The given-up SuperSpeed hub ports whose wait has run out, put back to
 * RxDetect - SET_FEATURE(PORT_LINK_STATE) with RxDetect (5) in wIndex bits
 * 15:8, the exit from SS.Disabled: valid only in DSPORT.Disabled, to
 * DSPORT.Disconnected (USB 3.2 10.16.2.10, USB 3.2 p.454, and 10.3.1.2,
 * p.387; verified); never BH_PORT_RESET, which a port in DSPORT.Disabled
 * ignores (10.3.1.6, p.388) - and each one
 * looked at in this pass, as a change (Codex review of the Phase 28-31
 * integration, finding 1). A port stays pending until its request
 * succeeds: a failed one is tried again after the next, longer wait (round
 * 2, finding B). Nothing here restarts the waits - a port read empty just
 * after RxDetect may only be detecting its receiver; HcdSsHubPortLook does
 * it on an independent departure, and hcd_enum.c on an enumeration that
 * reached Present (round 2, finding C). Called once a pass for each live
 * hub, before its ports are looked at; the timer is polled with a zero
 * wait, as the debounce polls its own. Thread only, powered.
 */
static VOID hcdHubRearmPorts(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    LARGE_INTEGER now;
    PHCD_PORT q;
    ULONG bytes;
    ULONG stalled;
    ULONG n;

    if (!hub->RearmArmed || hub->Draining || hub->Device == NULL) {
        return;
    }
    now.QuadPart = 0;
    if (KeWaitForSingleObject(&hub->RearmTimer, Executive, KernelMode, FALSE,
                              &now) != STATUS_SUCCESS) {
        return;
    }
    hub->RearmArmed = 0;
    for (n = 1; n <= hub->Ports; n++) {
        if ((hub->RearmPorts & (1UL << n)) == 0) {
            continue;
        }
        q = HcdHubPort(hc, hub, n);
        if (q->HubSsRearmWait > 1) {
            q->HubSsRearmWait--;
            continue;
        }
        if (!hcdHubRequest(hc, hub, XHCI_HUB_RT_PORT_OUT,
                           XHCI_HUB_REQ_SET_FEATURE,
                           (USHORT)XHCI_SSHUB_FEAT_PORT_LINK_STATE,
                           (USHORT)(n | (XHCI_SSHUB_LINK_RX_DETECT << 8)), 0,
                           &bytes, &stalled)) {
            XHCI_DBG_VALUE("hcd: SS hub port not re-armed, retried, hub/port",
                           (hub->Index << 8) | n);
            hcdHubRearmBackoff(q);
            continue;
        }
        hub->RearmPorts &= ~(1UL << n);
        q->HubSsSeen = 0;
        hc->SsHubPortsRearmed++;
        hub->Changed |= 1UL << n;
        XHCI_DBG_VALUE("hcd: SS hub port re-armed, hub/port",
                       (hub->Index << 8) | n);
    }
    if (hub->RearmPorts != 0) {
        hcdHubRearmTick(hub);
    }
}

/* A hub port given up after its attempts (section 10.2 step 7): disabled,
 * and left so until its next connect change - the machine waits in Failed
 * for a connect, which the hub's own disable does not raise. A SuperSpeed
 * hub has no PORT_ENABLE feature to clear (USB 3.2 Table 10-9): its port is
 * disabled by SET_FEATURE(PORT_LINK_STATE) to SS.Disabled, the link state
 * in wIndex bits 15:8 (integration of Phases 27 and 30) - and SS.Disabled
 * detects nothing, so no connect change would ever come: the port is
 * scheduled for its re-arm to RxDetect (hcdHubRearmPorts), after a wait
 * that doubles with each give-up in a row. */
VOID HcdHubPortDisable(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n)
{
    PHCD_PORT q;
    ULONG ok;
    ULONG bytes;
    ULONG stalled;

    hc->HubPortsGivenUp++;
    if (hub->Usb3) {
        ok = hcdHubRequest(hc, hub, XHCI_HUB_RT_PORT_OUT,
                           XHCI_HUB_REQ_SET_FEATURE,
                           (USHORT)XHCI_SSHUB_FEAT_PORT_LINK_STATE,
                           (USHORT)(n | (XHCI_SSHUB_LINK_DISABLED << 8)), 0,
                           &bytes, &stalled);
    } else {
        ok = hcdHubFeature(hc, hub, n, 0, XHCI_HUB_FEAT_PORT_ENABLE);
    }
    if (!ok) {
        XHCI_DBG_VALUE("hcd: hub port not disabled, hub/port",
                       (hub->Index << 8) | n);
        return;
    }
    XHCI_DBG_VALUE("hcd: hub port given up, disabled, hub/port",
                   (hub->Index << 8) | n);
    if (hub->Usb3) {
        q = HcdHubPort(hc, hub, n);
        hcdHubRearmBackoff(q);
        hub->RearmPorts |= 1UL << n;
        hcdHubRearmTick(hub);
    }
}

/*
 * CLEAR_TT_BUFFER to the TT a Full- or Low-Speed device sits behind (section
 * 10.4; xhci_hub.h): after a control or bulk endpoint of it halted and was
 * reset (xHCI 4.6.8 p.116), and after its Address Device failed with a USB
 * Transaction Error (p.102), where the device still answers at address 0
 * (`addressZero`). `type` is XHCI_PIPE_XFER_*; any but control and bulk
 * clears nothing. The TT hub is the slot the device's TT fields name, which
 * the leaf-first teardown keeps enabled for as long as the device's is.
 * A buffer left busy is not reused by the TT (USB 2.0 11.17.5, to
 * transcribe), so the endpoint's later split transactions may go on failing
 * until it is cleared: a request that failed short of a STALL - the hub's
 * own refusal - is sent once more, and one that still fails is counted
 * (TtBufferClearFailures) and logged, and the endpoint's next halt asks
 * again (Codex review of d54eef0, the TT note). Thread only, powered.
 */
VOID HcdHubClearTt(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                   ULONG endpointAddress, ULONG type, ULONG addressZero)
{
    PHCD_USB_DEVICE ttDev;
    PHCD_HUB hub;
    ULONG address;
    ULONG offset;
    ULONG value;
    ULONG bytes;
    ULONG stalled;
    ULONG tries;
    ULONG ok;

    if (dev->TtSlot == 0 || dev->TtSlot > XHCI_MAX_SLOTS) {
        return;
    }
    ttDev = hc->SlotDevice[dev->TtSlot];
    hub = (ttDev != NULL) ? ttDev->Hub : NULL;
    if (hub == NULL || hub->Draining || hub->Device != ttDev || hub->Refused) {
        return;
    }
    address = 0;
    if (!addressZero) {
        /* The USB Device Address the xHC assigned: output Slot Context DW3
         * bits 7:0 (xhci-data-structures.md, the Slot Context table). */
        if (XhciSlotContextOffset(&hc->Hc.Layout, dev->SlotId, &offset) !=
            XHCI_LAYOUT_OK) {
            return;
        }
        address = XhciCommonAt(&hc->Hc, offset)[3] & 0xFFUL;
    }
    if (!XhciHubClearTtValue(address, endpointAddress & 0x0FUL, type,
                             type != XHCI_HUB_TT_EP_CONTROL &&
                                 (endpointAddress & 0x80UL) != 0,
                             &value)) {
        return;
    }
    ok = 0;
    for (tries = 0; tries < 2; tries++) {
        ok = hcdHubRequest(hc, hub, XHCI_HUB_RT_PORT_OUT,
                           XHCI_HUB_REQ_CLEAR_TT_BUFFER, (USHORT)value,
                           (USHORT)XhciHubClearTtPort(dev->TtMulti,
                                                      dev->TtPort),
                           0, &bytes, &stalled);
        if (ok || stalled || hub->Draining || hub->Device != ttDev) {
            break;
        }
    }
    if (!ok) {
        hc->TtBufferClearFailures++;
        XHCI_DBG_VALUE("hcd: CLEAR_TT_BUFFER refused, TT slot/wValue",
                       (dev->TtSlot << 16) | value);
        return;
    }
    hc->TtBufferClears++;
    XHCI_DBG_VALUE("hcd: CLEAR_TT_BUFFER, TT slot/wValue",
                   (dev->TtSlot << 16) | value);
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
 * A port above the ones the bus manages (XHCI_HUB_MAX_PORTS: a USB 2.0 hub
 * past 14, a SuperSpeed hub's fifteenth) is never enumerated, but its
 * change bits are still the hub's to report: left set, they would complete
 * every status-change transfer at once with the same bit for as long as the
 * hub is up (Codex review of 034a119, finding 3). So such a port is
 * unpowered at bring-up, and each change it reports is read and cleared
 * here - by the SuperSpeed selectors on a SuperSpeed hub, by USB 2.0's
 * otherwise - and nothing else is done with it.
 */
VOID HcdHubSilence(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n)
{
    ULONG status;
    ULONG change;
    ULONG bit;
    ULONG selector;

    if (!HcdHubPortStatus(hc, hub, n, &status, &change)) {
        return;
    }
    for (bit = 1; bit <= 0x80UL; bit <<= 1) {
        if ((change & bit) == 0) {
            continue;
        }
        selector = hub->Usb3 ? XhciSsHubClearSelector(bit)
                             : XhciHubClearSelector(bit);
        if (selector != 0) {
            (VOID)hcdHubFeature(hc, hub, n, 0, selector);
        }
    }
}

/* Every unmanaged port a status-change report names, silenced. */
static VOID hcdHubSilenceReported(PHCD_CONTROLLER hc, PHCD_HUB hub,
                                  const UCHAR *report, ULONG bytes)
{
    ULONG n;

    for (n = hub->Ports + 1; n <= hub->Desc.Ports; n++) {
        if (XhciHubReportHas(report, bytes, n)) {
            HcdHubSilence(hc, hub, n);
        }
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
    hcdHubRearmPorts(hc, hub);
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
            hcdHubSilenceReported(hc, hub, hcdHubReport(hc, hub), bytes);
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
                               HcdDevicePipeSpeed(hc, dev), &ep) != XHCI_PIPE_OK ||
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
    hcdHubRearmStop(hub);
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
        q->ResumeTries = 0;
        q->ResumePending = 0;
        q->Hub = NULL;
        q->Number = n;
        /* A SuperSpeed hub's port state (30-A.1) is not the next hub's. */
        q->HubSsRecover = 0;
        q->HubSsRearmWait = 0;
        q->HubSsRearms = 0;
        q->HubSsSeen = 0;
        XhciLinkInit(&q->Link);
        b = (PUCHAR)&q->HubSsLink;
        for (i = 0; i < sizeof(q->HubSsLink); i++) {
            b[i] = 0;
        }
    }
    /* A set timer is in the kernel's queue: cancelled before the object
     * that holds it is cleared. */
    hcdHubRearmStop(hub);
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
    /* Its ports' location budgets (35-T.5), in the extension so the dump
     * shows them: each port a new location while this object lives. */
    for (n = 0; n < HCD_HUB_MAX_PORTS; n++) {
        XhciTolLocInit(&hc->Hc.Tol.HubLoc[hub->Index * HCD_HUB_MAX_PORTS +
                                          n]);
    }

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
        q->ResumeTries = 0;
        q->ResumePending = 0;
        /* Kept deferred across the recovery that rebuilt this hub (task
        * 33.3). */
        q->SettleDeferred = HcdEnumSettleDeferredAt(hc, dev->Port, dev->Route,
                                                    n);
        q->LookFails = 0;
        q->Hub = hub;
        q->Number = n;
        if (!hcdHubFeature(hc, hub, n, 1, XHCI_HUB_FEAT_PORT_POWER)) {
            XHCI_DBG_VALUE("hcd: hub port not powered, hub/port",
                           (hub->Index << 8) | n);
        }
    }
    for (n = hub->Ports + 1; n <= hub->Desc.Ports; n++) {
        (VOID)hcdHubFeature(hc, hub, n, 0, XHCI_HUB_FEAT_PORT_POWER);
        HcdHubSilence(hc, hub, n);
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
