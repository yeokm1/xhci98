/*
 * hcd_sshub.c - the SuperSpeed half of a USB 3 hub inside the bus
 * (roadmap-hcd.md task 30-A.1; xhci-data-structures.md section 11).
 *
 * A USB 3 hub is two hubs on two ports. Its USB 2.0 half on a High-Speed
 * port is a Phase 27 hub and never reaches this file. Its SuperSpeed half on
 * a SuperSpeed port is an object of the bus like any hub (hcd_hub.c owns
 * the object, the topology node, the status-change pipe and the teardown),
 * and hcd_hub.c hands this file the requests that differ at SuperSpeed
 * (hub->Usb3):
 *
 *   bring-up (HcdSsHubConfigure): SET_CONFIGURATION, SET_HUB_DEPTH with
 *     the hub's tier - the Route String nibble it routes by, owed before
 *     any port is read - GET_DESCRIPTOR(SuperSpeed hub, 0x2A), and the one
 *     Configure Endpoint marking the slot Hub = 1 with its port count (no
 *     TT fields: a SuperSpeed hub has none) and opening the status-change
 *     endpoint, a SuperSpeed interrupt endpoint read with its companion;
 *     hcd_hub.c then powers the ports and arms the pipe as for any hub;
 *   a port reset (HcdSsHubPortReset): PORT_RESET from U0 and BH_PORT_RESET
 *     (warm) from a link a hot reset cannot start in, within xhci_link.h's
 *     warm-reset budget per port, waited for on C_PORT_RESET or
 *     C_BH_PORT_RESET, the change bits a warm reset leaves cleared; and on a
 *     SuperSpeedPlus hub the extended port status read for the downstream
 *     link's rate and lane count;
 *   a port look (HcdSsHubPortLook): the SuperSpeed decision
 *     (XhciSsHubPortDecide), its change bits cleared by their own
 *     selectors, and the outcome in xhci_hub.h's decision shape, which
 *     hcd_enum.c feeds the port's machine as for any hub port; a link found
 *     in SS.Inactive or Compliance Mode is marked for recovery, and the
 *     warm reset (HcdSsHubPortRecover) goes on the wire only after
 *     hcd_enum.c has fed the disconnect and the old device and its subtree
 *     are torn down;
 *   the Protocol Speed ID of a device behind it (HcdSsHubPsiv), for
 *     HcdHubPlace: SuperSpeed's, or a SuperSpeedPlus rate's from the
 *     extended status - and after Address Device the controller's own
 *     answer in the output Slot Context (HcdSsHubAdoptSpeed);
 *   the ports above the 14 the bus manages (HcdHubSilence, hcd_hub.c):
 *     unpowered and their changes acknowledged, so they never keep the
 *     status-change pipe busy.
 *
 * The two halves are independent: neither is told of the other, and a
 * counter (HcdSsHubCountPair) is all that notices a likely pair.
 *
 * Every function here runs on the controller thread at PASSIVE_LEVEL, as
 * hcd_hub.c's do.
 */

#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_enum.h"
#include "xhci_dbg.h"

/* The hub times the reset; polled from 20 ms on and given up at 500 ms for
 * a hot reset, 1000 ms for a warm one - as hcd_enum.c's root port, a bus
 * policy and not a specification number; reset recovery 10 ms. */
#define HCD_SSHUB_RESET_FIRST_MS    20UL
#define HCD_SSHUB_HOT_WAIT_MS       500UL
#define HCD_SSHUB_WARM_WAIT_MS      1000UL
#define HCD_SSHUB_POLL_STEP_MS      10UL
#define HCD_SSHUB_RESET_RECOVERY_MS 10UL

static VOID hcdSsHubDelay(ULONG milliseconds)
{
    LARGE_INTEGER due;

    HcdRelativeMs(&due, milliseconds);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
}

static ULONG hcdSsHubFeature(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                             ULONG set, ULONG selector)
{
    ULONG bytes;
    ULONG stalled;

    return HcdHubClassRequest(hc, hub, XHCI_HUB_RT_PORT_OUT,
                              (UCHAR)(set ? XHCI_HUB_REQ_SET_FEATURE
                                          : XHCI_HUB_REQ_CLEAR_FEATURE),
                              (USHORT)selector, (USHORT)n, 0, &bytes,
                              &stalled);
}

/* Each XHCI_SSHUB_C_* bit of `bits` cleared with its own selector. */
static VOID hcdSsHubClear(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                          ULONG bits)
{
    ULONG bit;
    ULONG selector;

    for (bit = 1; bit <= XHCI_SSHUB_C_PORT_CONFIG_ERROR; bit <<= 1) {
        selector = XhciSsHubClearSelector(bit);
        if ((bits & bit) != 0 && selector != 0) {
            (VOID)hcdSsHubFeature(hc, hub, n, 0, selector);
        }
    }
}

/* ----------------------------------------------------------------------- */
/* Bring-up                                                                 */
/* ----------------------------------------------------------------------- */

/* The configuration's first interface number, the hub's one interface. */
static ULONG hcdSsHubInterface(PHCD_USB_DEVICE dev, PULONG number)
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
 * GET_DESCRIPTOR(SuperSpeed hub): wValue 0x2A00, 12 bytes. Folded by the
 * graph (its counters; it reads bNbrPorts and wHubCharacteristics at the
 * offsets both descriptor types share, and records the 0x2A type byte
 * without requiring 0x29) and parsed here; a reply the graph folded but the
 * bus cannot serve is counted malformed here instead, so each reply counts
 * once. The shared shape (hub->Desc) is filled from it.
 */
static ULONG hcdSsHubDescriptor(PHCD_CONTROLLER hc, PHCD_HUB hub)
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

    topo = &hc->Hc.Topology;
    s = (PUCHAR)hc->ScratchVa;
    setup.bmRequestType = XHCI_HUB_RT_HUB_IN;
    setup.bRequest = XHCI_HUB_REQ_GET_DESCRIPTOR;
    setup.wValue = XHCI_SSHUB_DESC_VALUE;
    setup.wIndex = 0;
    setup.wLength = (USHORT)XHCI_SSHUB_DESC_BYTES;
    XhciTopoObserveSetup(topo, hub->SlotId, &setup, &snoop);
    if (!HcdThreadControlEx(hc, hub->Device, setup.bmRequestType,
                            setup.bRequest, setup.wValue, 0,
                            XHCI_SSHUB_DESC_BYTES, &bytes, &stalled)) {
        XHCI_DBG_VALUE("hcd: SS hub descriptor not read, slot/stalled",
                       (hub->SlotId << 8) | stalled);
        return 0;
    }
    folded = topo->Descriptors;
    bad = topo->DescriptorsBad;
    (VOID)XhciTopoObserveReply(topo, &snoop, s, bytes, &gone);
    hc->Counters.TopoDescriptors += topo->Descriptors - folded;
    hc->Counters.TopoDescriptorsBad += topo->DescriptorsBad - bad;
    if (XhciSsHubParseDescriptor(s, bytes, &hub->SsDesc) != XHCI_SSHUB_OK) {
        if (topo->DescriptorsBad == bad) {
            hc->Counters.TopoDescriptorsBad++;
        }
        XHCI_DBG_VALUE("hcd: SS hub descriptor malformed, bytes/type/ports",
                       (bytes << 16) | ((bytes > 1 ? (ULONG)s[1] : 0) << 8) |
                           (bytes > 2 ? (ULONG)s[2] : 0));
        return 0;
    }
    hub->Desc.Ports = hub->SsDesc.Ports;
    hub->Desc.Characteristics = hub->SsDesc.Characteristics;
    hub->Desc.PowerGoodMs = hub->SsDesc.PowerGoodMs;
    hub->Desc.ControllerCurrent = hub->SsDesc.ControllerCurrent;
    hub->Desc.ThinkTime = 0;
    hub->Desc.Managed = hub->SsDesc.Managed;
    hub->Ports = hub->SsDesc.Managed;
    XHCI_DBG_VALUE("hcd: SS hub descriptor, ports/characteristics",
                   (hub->SsDesc.Ports << 16) | hub->SsDesc.Characteristics);
    XHCI_DBG_VALUE("hcd: SS hub descriptor, hdr dec lat/hub delay ns",
                   (hub->SsDesc.HeaderDecodeLatency << 16) |
                       hub->SsDesc.HubDelayNs);
    return 1;
}

/*
 * The SuperSpeed half's bring-up up to the Configure Endpoint (the file
 * header); hcd_hub.c's HcdHubStart powers the ports and arms the pipe after
 * it. Returns 0 when the hub cannot be served; the caller undoes the
 * object and the node.
 */
ULONG HcdSsHubConfigure(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    PXHCI_TOPOLOGY topo;
    PHCD_USB_DEVICE dev;
    const XHCI_TOPO_NODE *node;
    XHCI_TOPO_HUBMARK mark;
    XHCI_PIPE_IFACE iface;
    XHCI_PIPE_EP ep;
    PHCD_PORT q;
    PUCHAR b;
    ULONG number;
    ULONG bytes;
    ULONG bcd;
    ULONG n;
    ULONG i;

    topo = &hc->Hc.Topology;
    dev = hub->Device;
    if (dev->Config == NULL || dev->ConfigLength < XHCI_PIPE_CONFIG_BYTES ||
        !hcdSsHubInterface(dev, &number) || number >= 32) {
        return 0;
    }
    if (!HcdThreadControl(hc, dev, 0x00, XHCI_HUB_REQ_SET_CONFIG,
                          (USHORT)dev->Config[5], 0, 0, &bytes)) {
        XHCI_DBG_VALUE("hcd: SS hub SET_CONFIGURATION failed, slot",
                       dev->SlotId);
        return 0;
    }
    dev->ConfigValue = dev->Config[5];
    hub->Alternate = 0;
    dev->Alternate[number] = 0;

    /* The depth is the hub's tier: 0 on a root port (USB 3.2 10.16.2.9, to
     * verify), so the hub routes by Route String nibble `tier`. A hub that
     * refuses it would route by a nibble nobody chose: not served. */
    if (!HcdThreadControl(hc, dev, XHCI_HUB_RT_HUB_OUT,
                          XHCI_SSHUB_REQ_SET_HUB_DEPTH, (USHORT)hub->Tier, 0,
                          0, &bytes)) {
        hc->Counters.SsHubDepthRefused++;
        XHCI_DBG_VALUE("hcd: SS hub SET_HUB_DEPTH refused, slot/depth",
                       (dev->SlotId << 8) | hub->Tier);
        return 0;
    }

    if (!hcdSsHubDescriptor(hc, hub)) {
        return 0;
    }
    node = XhciTopoFind(topo, hub->SlotId);
    if (!XhciTopoHubMark(node, XHCI_SPEED_SUPER, &mark)) {
        XHCI_DBG_VALUE("hcd: SS hub not markable, slot", dev->SlotId);
        return 0;
    }
    if (XhciPipeFindInterface(dev->Config, dev->ConfigLength, number, 0,
                              &iface) != XHCI_PIPE_OK ||
        iface.EndpointCount < 1 ||
        XhciPipeEndpointParamsAt(dev->Config, dev->ConfigLength,
                                 iface.EndpointOffset[0],
                                 dev->Plus ? XHCI_PIPE_SPEED_SUPER_PLUS
                                           : XHCI_PIPE_SPEED_SUPER,
                                 hc->Hc.HcInfo.Lec, &ep) != XHCI_PIPE_OK ||
        ep.TransferType != XHCI_PIPE_XFER_INTERRUPT || !ep.DirectionIn) {
        XHCI_DBG_VALUE("hcd: SS hub has no status-change endpoint, slot",
                       dev->SlotId);
        return 0;
    }
    dev->HubPorts = mark.NumberOfPorts;
    dev->HubTtt = 0;
    dev->HubMtt = 0;
    dev->HubMarked = 1;
    hub->Status = HcdCfgHubOpen(hc, dev, &ep, number);
    if (hub->Status == NULL) {
        dev->HubMarked = 0;
        return 0;
    }
    hc->Counters.TopoHubSlotsMarked++;
    hub->StatusBytes = XhciHubStatusBytes(hub->SsDesc.Ports);
    bcd = (ULONG)dev->DeviceDesc[2] | ((ULONG)dev->DeviceDesc[3] << 8);
    hub->ExtStatus = XhciSsHubHasExtStatus(bcd, dev->Bos != NULL
                                                    ? &dev->BosInfo
                                                    : NULL);
    for (n = 1; n <= hub->Ports; n++) {
        q = HcdHubPort(hc, hub, n);
        XhciLinkInit(&q->Link);
        q->HubSsRecover = 0;
        b = (PUCHAR)&q->HubSsLink;
        for (i = 0; i < sizeof(q->HubSsLink); i++) {
            b[i] = 0;
        }
    }
    hc->Counters.SsHubsStarted++;
    XHCI_DBG_VALUE("hcd: SS hub slot marked, slot/depth/ports/ext",
                   (dev->SlotId << 24) | (hub->Tier << 16) |
                       (mark.NumberOfPorts << 8) | hub->ExtStatus);
    return 1;
}

/* ----------------------------------------------------------------------- */
/* Ports                                                                    */
/* ----------------------------------------------------------------------- */

/* A warm reset is owed and the port's budget has room: spend one, counted;
 * otherwise the link is given up, counted, and the device left to the
 * hub's USB 2.0 half, where it appears as on a root port (29-A.5's passive
 * fallback, one tier down). */
static ULONG hcdSsHubWarmAllowed(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n)
{
    PHCD_PORT q;

    q = HcdHubPort(hc, hub, n);
    if (q->Link.WarmResets >= XHCI_LINK_MAX_WARM_RESETS) {
        if (!q->Link.GaveUp) {
            q->Link.GaveUp = 1;
            hc->Counters.SsHubLinksGivenUp++;
            XHCI_DBG_VALUE("hcd: SS hub port link given up, hub/port",
                           (hub->Index << 8) | n);
        }
        return 0;
    }
    q->Link.WarmResets++;
    hc->Counters.SsHubWarmResets++;
    return 1;
}

/* Wait out a reset already asked: the last status and change seen, and the
 * progress they say (XHCI_HUB_RESET_*), or PENDING when the wait ran out. */
static ULONG hcdSsHubResetWait(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                               ULONG wait, PULONG status, PULONG change,
                               PULONG warmSeen)
{
    ULONG waited;
    ULONG progress;

    hcdSsHubDelay(HCD_SSHUB_RESET_FIRST_MS);
    waited = HCD_SSHUB_RESET_FIRST_MS;
    for (;;) {
        if (!HcdHubPortStatus(hc, hub, n, status, change)) {
            return XHCI_HUB_RESET_FAILED;
        }
        progress = XhciSsHubResetProgress(*status, *change, warmSeen);
        if (progress != XHCI_HUB_RESET_PENDING || waited >= wait) {
            return progress;
        }
        hcdSsHubDelay(HCD_SSHUB_POLL_STEP_MS);
        waited += HCD_SSHUB_POLL_STEP_MS;
    }
}

/* A SuperSpeedPlus hub's extended port status: dwExtPortStatus, decoded
 * against the hub's own sublink attributes into the port's link. Left zero
 * - a Gen 1 link, as far as the Slot Context is told - when the hub does
 * not answer or names a sublink its capability does not list. */
static VOID hcdSsHubReadExt(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                            PHCD_PORT q)
{
    PUCHAR s;
    ULONG bytes;
    ULONG stalled;
    ULONG ext;

    if (!hub->ExtStatus || hub->Device == NULL) {
        return;
    }
    if (!HcdHubClassRequest(hc, hub, XHCI_HUB_RT_PORT_IN,
                            XHCI_HUB_REQ_GET_STATUS,
                            XHCI_SSHUB_STATUS_EXT, (USHORT)n,
                            XHCI_SSHUB_STATUS_EXT_BYTES, &bytes, &stalled) ||
        bytes < XHCI_SSHUB_STATUS_EXT_BYTES) {
        XHCI_DBG_VALUE("hcd: SS hub extended status not read, hub/port",
                       (hub->Index << 8) | n);
        return;
    }
    s = (PUCHAR)hc->ScratchVa;
    ext = (ULONG)s[4] | ((ULONG)s[5] << 8) | ((ULONG)s[6] << 16) |
          ((ULONG)s[7] << 24);
    if (XhciSsHubDownstream(&hub->Device->BosInfo, ext, &q->HubSsLink) !=
        XHCI_SSHUB_OK) {
        XHCI_DBG_VALUE("hcd: SS hub sublink not in its capability, ext",
                       ext);
        return;
    }
    XHCI_DBG_VALUE("hcd: SS hub downstream, Mbit/s lane/lanes",
                   ((q->HubSsLink.LaneKbps / 1000UL) << 8) |
                       q->HubSsLink.Lanes);
}

/*
 * The reset an enumeration owes SuperSpeed hub port n (the file header).
 * Returns 1 with *speedClass SuperSpeed when the port came back enabled in
 * U0; the port's link (HubSsLink) is then what the extended status said.
 */
ULONG HcdSsHubPortReset(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                        PULONG speedClass)
{
    PHCD_PORT q;
    PUCHAR b;
    ULONG status;
    ULONG change;
    ULONG converted;
    ULONG warmSeen;
    ULONG progress;
    ULONG kind;
    ULONG i;

    *speedClass = XHCI_SPEED_UNKNOWN;
    q = HcdHubPort(hc, hub, n);
    b = (PUCHAR)&q->HubSsLink;
    for (i = 0; i < sizeof(q->HubSsLink); i++) {
        b[i] = 0;
    }
    if (!HcdHubPortStatus(hc, hub, n, &status, &change)) {
        return 0;
    }
    converted = 0;
    kind = XhciSsHubResetKind(status, &converted);
    if (kind == XHCI_SSHUB_RESET_NONE) {
        return 0;
    }
    if (kind == XHCI_SSHUB_RESET_WARM && !hcdSsHubWarmAllowed(hc, hub, n)) {
        return 0;
    }
    XHCI_DBG_VALUE("hcd: SS hub port reset, hub/port/warm/link",
                   (hub->Index << 24) | (n << 16) | (kind << 8) |
                       XhciSsHubLinkState(status));
    if (!hcdSsHubFeature(hc, hub, n, 1,
                         kind == XHCI_SSHUB_RESET_WARM
                             ? XHCI_SSHUB_FEAT_BH_PORT_RESET
                             : XHCI_HUB_FEAT_PORT_RESET)) {
        return 0;
    }
    warmSeen = 0;
    progress = hcdSsHubResetWait(hc, hub, n,
                                 kind == XHCI_SSHUB_RESET_WARM
                                     ? HCD_SSHUB_WARM_WAIT_MS
                                     : HCD_SSHUB_HOT_WAIT_MS,
                                 &status, &change, &warmSeen);
    hcdSsHubClear(hc, hub, n,
                  XhciSsHubResetClears(change,
                                       kind == XHCI_SSHUB_RESET_WARM ||
                                           warmSeen));
    if (progress != XHCI_HUB_RESET_ENABLED) {
        XHCI_DBG_VALUE("hcd: SS hub port reset failed, port/status",
                       (n << 16) | status);
        return 0;
    }
    q->Link.WarmResets = 0;
    q->Link.GaveUp = 0;
    *speedClass = XHCI_SPEED_SUPER;
    hcdSsHubReadExt(hc, hub, n, q);
    hcdSsHubDelay(HCD_SSHUB_RESET_RECOVERY_MS);
    return 1;
}

/*
 * One SuperSpeed hub port the status-change report (or a poll, or the first
 * look) named: GET_STATUS, the SuperSpeed decision, every change bit it saw
 * cleared, a port that lost its power to over-current powered again, and
 * the outcome in *d for hcd_enum.c. A link in SS.Inactive or Compliance
 * Mode is NOT reset here: the port is marked HubSsRecover and *d asks only
 * for the disconnect of whatever it held, so hcd_enum.c tears the old
 * device and its subtree down - frozen, its transfers ended, its slot
 * disabled - before HcdSsHubPortRecover puts the warm reset on the wire
 * (Codex review of 034a119, finding 1). Returns 0 when the hub did not
 * answer.
 */
ULONG HcdSsHubPortLook(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n,
                       ULONG state, PXHCI_HUB_PORT_DECISION d)
{
    XHCI_SSHUB_PORT_DECISION sd;
    PHCD_PORT q;
    ULONG status;
    ULONG change;

    d->Clear = 0;
    d->Disconnect = 0;
    d->Connect = 0;
    d->OverCurrent = 0;
    d->Repower = 0;
    d->Suspended = 0;
    if (!HcdHubPortStatus(hc, hub, n, &status, &change)) {
        return 0;
    }
    q = HcdHubPort(hc, hub, n);
    XhciSsHubPortDecide(state, status, change, &sd);
    hcdSsHubClear(hc, hub, n, sd.Clear);
    if ((status & XHCI_SSHUB_PORT_CONNECTION) != 0) {
        q->HubSsSeen = 1;
    }
    if ((status & XHCI_SSHUB_PORT_CONNECTION) == 0 &&
        XhciSsHubLinkState(status) == XHCI_SSHUB_LINK_RX_DETECT) {
        /* Nothing on the port: the next device starts with a full warm
         * budget. The re-arm waits restart only when a device was seen
         * since the last re-arm and has gone - not on the empty read that
         * follows the driver's own RxDetect while the receiver is being
         * detected (Codex review of the Phase 28-31 integration, round 2,
         * finding C). */
        q->Link.WarmResets = 0;
        q->Link.GaveUp = 0;
        if (q->HubSsSeen) {
            q->HubSsRearms = 0;
            q->HubSsSeen = 0;
        }
    }
    if (sd.OverCurrent) {
        XHCI_DBG_VALUE("hcd: SS hub port over-current, hub/port/status",
                       (hub->Index << 24) | (n << 16) | status);
    }
    if (sd.Repower) {
        (VOID)hcdSsHubFeature(hc, hub, n, 1, XHCI_HUB_FEAT_PORT_POWER);
    }
    if (sd.ConfigError) {
        hc->Counters.SsHubConfigErrors++;
        XHCI_DBG_VALUE("hcd: SS hub port config error, hub/port",
                       (hub->Index << 8) | n);
    }
    if (sd.LinkChange) {
        XHCI_DBG_VALUE("hcd: SS hub port link change, port/status",
                       (n << 16) | status);
    }
    d->Clear = sd.Clear;
    d->Disconnect = sd.Disconnect;
    d->Connect = sd.Connect;
    d->OverCurrent = sd.OverCurrent;
    d->Repower = sd.Repower;
    q->HubSsRecover = sd.WarmReset;
    if (sd.WarmReset) {
        XHCI_DBG_VALUE("hcd: SS hub port link error, port/link",
                       (n << 8) | XhciSsHubLinkState(status));
    }
    return 1;
}

/*
 * The second half of a link recovery HcdSsHubPortLook marked: the warm
 * reset, issued only once the port holds no device - hcd_enum.c has fed
 * the disconnect and the teardown has run to its end, so no slot, ring or
 * URB of the old device or its subtree is live while the hub retrains the
 * link. A port that still holds a device (a teardown that could not
 * finish) is not reset; it is looked at again next pass. Within the
 * port's warm-reset budget; the port is then decided again as the Empty
 * port the disconnect left. Returns 1 when that decision is a connect.
 * Thread only, powered, controller not halted (the caller's test).
 */
ULONG HcdSsHubPortRecover(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n)
{
    XHCI_SSHUB_PORT_DECISION after;
    PHCD_PORT q;
    ULONG status;
    ULONG change;
    ULONG warmSeen;
    ULONG progress;

    q = HcdHubPort(hc, hub, n);
    if (!q->HubSsRecover) {
        return 0;
    }
    q->HubSsRecover = 0;
    if (hub->Device == NULL || hub->Draining) {
        return 0;
    }
    if (q->Device != NULL) {
        XHCI_DBG_VALUE("hcd: SS hub port still held, warm reset deferred",
                       (hub->Index << 8) | n);
        hub->Changed |= 1UL << n;
        return 0;
    }
    if (!hcdSsHubWarmAllowed(hc, hub, n) ||
        !hcdSsHubFeature(hc, hub, n, 1, XHCI_SSHUB_FEAT_BH_PORT_RESET)) {
        return 0;
    }
    warmSeen = 0;
    progress = hcdSsHubResetWait(hc, hub, n, HCD_SSHUB_WARM_WAIT_MS,
                                 &status, &change, &warmSeen);
    hcdSsHubClear(hc, hub, n, XhciSsHubResetClears(change, 1));
    if (progress == XHCI_HUB_RESET_ENABLED) {
        q->Link.WarmResets = 0;
        q->Link.GaveUp = 0;
    }
    XhciSsHubPortDecide(XHCI_ENUM_EMPTY, status, 0, &after);
    return after.Connect;
}

/*
 * The Protocol Speed ID for the device on SuperSpeed hub port p, behind
 * root port `rootPort` (HcdHubPlace): SuperSpeed's, or for a SuperSpeedPlus
 * link the ID the root port's protocol names for its rate - counted, and
 * counted apart when no ID matched and SuperSpeed's was given. Returns 0
 * when the root port's protocol has no SuperSpeed ID at all.
 */
ULONG HcdSsHubPsiv(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG rootPort,
                   PULONG psiv)
{
    const XHCI_SSHUB_LINK *link;
    ULONG matched;

    link = (p->HubSsLink.Kbps != 0) ? &p->HubSsLink : NULL;
    matched = 0;
    if (XhciSsHubPsiv(&hc->Hc.PortMap, rootPort, link, psiv, &matched) !=
        XHCI_SSHUB_OK) {
        XHCI_DBG_VALUE("hcd: no SuperSpeed ID behind SS hub, root port",
                       rootPort);
        return 0;
    }
    if (link != NULL && link->Plus) {
        if (matched) {
            hc->Counters.SsHubDevicesPlus++;
        } else {
            hc->Counters.SsHubRateUnmatched++;
            XHCI_DBG_VALUE("hcd: SS hub downstream rate has no ID, Mbit/s",
                           link->Kbps / 1000UL);
        }
    }
    return 1;
}

/*
 * A hub just started: if it and another live hub look like the two halves
 * of one unit (XhciSsHubLooksPaired), count it once. Nothing else follows
 * from it: the halves stay independent hubs (30-A.1).
 */
VOID HcdSsHubCountPair(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    PHCD_HUB other;
    PHCD_HUB ss;
    PHCD_HUB hs;
    ULONG companion;
    ULONG i;

    if (hub->Device == NULL) {
        return;
    }
    for (i = 0; i < HCD_MAX_HUBS; i++) {
        other = &hc->Hubs[i];
        if (other == hub || !other->Used || other->Draining ||
            other->Device == NULL || other->Refused ||
            other->Usb3 == hub->Usb3) {
            continue;
        }
        ss = hub->Usb3 ? hub : other;
        hs = hub->Usb3 ? other : hub;
        companion = 0;
        if (ss->Device->Port >= 1 &&
            ss->Device->Port <= XHCI_MAX_ROOT_PORTS) {
            companion = hc->Hc.PortMap.Companion[ss->Device->Port - 1];
        }
        if (XhciSsHubLooksPaired(
                (ULONG)ss->Device->DeviceDesc[8] |
                    ((ULONG)ss->Device->DeviceDesc[9] << 8),
                ss->Device->Port, ss->Tier, ss->Device->Route,
                (ULONG)hs->Device->DeviceDesc[8] |
                    ((ULONG)hs->Device->DeviceDesc[9] << 8),
                hs->Device->Port, hs->Tier, hs->Device->Route, companion)) {
            hc->Counters.SsHubPairs++;
            XHCI_DBG_VALUE("hcd: hub halves look paired, SS slot/HS slot",
                           (ss->SlotId << 8) | hs->SlotId);
            return;
        }
    }
}

/*
 * Address Device has succeeded for the device on SuperSpeed hub port p: the
 * speed the controller wrote into its output Slot Context is authoritative
 * where it differs from the one the bus asked for and names a SuperSpeed
 * rate (XhciSsHubAdoptSpeed) - so a SuperSpeedPlus rate the bus could only
 * guess at is replaced by the controller's, and every later Slot Context
 * carries it (Codex review of 034a119, finding 2). Thread only, powered;
 * before hcdCountAddressed reads the rate.
 */
VOID HcdSsHubAdoptSpeed(PHCD_CONTROLLER hc, PHCD_PORT p,
                        PHCD_USB_DEVICE dev)
{
    ULONG offset;
    ULONG output;
    ULONG keep;

    if (p->Hub == NULL || !p->Hub->Usb3 ||
        XhciSlotContextOffset(&hc->Hc.Layout, dev->SlotId, &offset) !=
            XHCI_LAYOUT_OK) {
        return;
    }
    output = (XhciCommonAt(&hc->Hc, offset)[0] & XHCI_SLOT_SPEED_MASK) >>
             XHCI_SLOT_SPEED_SHIFT;
    keep = XhciSsHubAdoptSpeed(&hc->Hc.PortMap, dev->Port, dev->Speed,
                               output);
    if (keep != dev->Speed) {
        XHCI_DBG_VALUE("hcd: SS hub device speed from the controller, "
                       "asked/output", (dev->Speed << 8) | keep);
        dev->Speed = keep;
    }
}
