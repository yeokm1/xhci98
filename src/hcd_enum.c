/*
 * hcd_enum.c - root ports and enumeration on the controller thread
 * (roadmap-hcd.md tasks 26-A.3 and 26-A.4; design record 13 sections 5.3,
 * 10.2).
 *
 * The thread takes the ports the event DPC marked changed (hcd_dev.c,
 * XhciRootHubPortEvent), reads each one's PORTSC, acknowledges its change
 * bits, and feeds the port's state machine (xhci_enum.c) a connect or a
 * disconnect. Each action the machine asks for is carried out here, waited
 * for, and its outcome fed back as the next event, until the machine asks for
 * nothing. Commands are waited for on the device layer's completion event;
 * EP0 control transfers on the transfer completion the device layer matches
 * by slot and endpoint (hcd_dev.c).
 *
 * The speed is the port's own, carried into the Slot Context and onward:
 * there is no High-Speed lie and no SET_ADDRESS (section 5.3). Two
 * vocabularies, kept apart (Phase 29; the split 27-A.3 makes for the
 * endpoint rules): the raw PSIV the port reported goes into the Slot
 * Context, and the class the controller's PSI table decodes it to - as its
 * default ID - is what the enumeration machine and every rule are given.
 *
 * SuperSpeed root ports (tasks 29-A.1 to 29-A.3): a USB3 protocol port is
 * powered and served like a USB 2.0 one, with its link judged by
 * xhci_link.c - SS.Inactive and Compliance Mode recovered with a bounded
 * number of warm resets and then given up (the device appears on its USB
 * 2.0 companion, 29-A.5's passive fallback, counted), the enumeration's own
 * reset hot from U0 and warm otherwise - and its device addressed at EP0
 * 512 with its BOS descriptor read. A SuperSpeed hub is Phase 30's and is
 * refused here.
 *
 * Timings, section 10.2: attach debounce 100 ms (TATTDB), the reset timed by
 * the xHC and waited for up to 500 ms, reset recovery 10 ms (TRSTRCY),
 * SetAddress recovery 2 ms (TDSETADDR).
 *
 * Descriptors are read into a 4 KB common buffer of the controller's own
 * (hcd_dma.c), the one DMA-visible scratch the enumeration needs, less the
 * hubs' status-change reports at its tail (HCD_SCRATCH_CONTROL_BYTES); a
 * configuration descriptor longer than that is refused as a configuration
 * failure.
 *
 * The ports of the hubs inside the bus (27-A.1, hcd_hub.c) run the same
 * machine: their debounce and reset are the hub's class requests, their
 * devices are placed by the topology graph (HcdHubPlace) and addressed with
 * that place in the Slot Context, and a hub that reaches Present is brought
 * up by the bus and gets no PDO. A hub that leaves takes its subtree with it:
 * every port of it is fed a disconnect, each device's PDOs reported missing
 * and its slot disabled before the hub's own (leaf first, section 10.5), and
 * the port the hub sat on waits until PnP has deleted every PDO of the
 * subtree (hcdPortQuiet).
 *
 * IRQL: PASSIVE_LEVEL (the controller thread), except where a function says
 * otherwise.
 */

#include "hcd.h"
#include "xhci_hw.h"
#include "hcd_svc.h"
#include "xhci_xfer.h"
#include "xhci_enum.h"
#include "xhci_dbg.h"

#define HCD_DEBOUNCE_MS        100UL
#define HCD_RESET_WAIT_MS      500UL
/* A warm reset is LFPS for tens of milliseconds and then link training
 * (USB 3.2 7.5, to verify); twice the hot reset's wait is this driver's
 * margin, not a specification number. */
#define HCD_WARM_RESET_WAIT_MS 1000UL
#define HCD_RESET_RECOVERY_MS  10UL
#define HCD_SETADDRESS_MS      2UL
#define HCD_COMMAND_WAIT_MS    5000UL
#define HCD_TRANSFER_WAIT_MS   5000UL
#define HCD_POLL_STEP_MS       10UL

#define HCD_DESC_DEVICE        1
#define HCD_DESC_CONFIGURATION 2
#define HCD_DESC_BOS           15

/* No RtlCopyBytes: the DDK spells it as memcpy, an import no row admits. */
static VOID hcdCopy(PUCHAR to, const UCHAR *from, ULONG bytes)
{
    ULONG i;

    for (i = 0; i < bytes; i++) {
        to[i] = from[i];
    }
}

/* ----------------------------------------------------------------------- */
/* Waits                                                                    */
/* ----------------------------------------------------------------------- */

static ULONG hcdWaitEvent(PKEVENT event, ULONG milliseconds)
{
    LARGE_INTEGER due;
    NTSTATUS status;

    HcdRelativeMs(&due, milliseconds);
    status = KeWaitForSingleObject(event, Executive, KernelMode, FALSE, &due);
    return status == STATUS_SUCCESS;
}

/* ----------------------------------------------------------------------- */
/* Commands                                                                 */
/* ----------------------------------------------------------------------- */

/* The enumeration stops for the pass once the controller has failed or a
 * timed-out transfer may still DMA into the scratch: the recovery that
 * follows invalidates every slot, and the thread rescans from there. */
static ULONG hcdHalted(PHCD_CONTROLLER hc)
{
    return hc->Hc.ControllerFailed || hc->ScratchTainted;
}

/*
 * Submit one command and wait for its own completion. Returns the completion
 * code, or 0 when it never completed (lost, refused or timed out); *control
 * receives the Command Completion Event's DW3.
 *
 * A completion counts only when its TRB is the one this submit placed
 * (CmdDonePA, from hcd_dev.c): a completion of an earlier command, the
 * start's No Op among them, can land between the clear and the submit
 * (Codex review of batch (b), round 1, finding 7). The event is cleared
 * before the address is read, so a completion arriving in between still
 * leaves it signalled. A command that never answers is the controller's
 * failure: the reset is requested, and the recovery's invalidation settles
 * whatever the command owned.
 */
static ULONG hcdCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb,
                        PULONG control)
{
    ULONG trbPA;
    ULONG answer;
    ULONG rounds;
#if DBG
    HCD_STRICT_SNAP strict;
#endif

    *control = 0;
    if (hcdHalted(hc)) {
        return 0;
    }
#if DBG
    /* Read before the doorbell: the completion rewrites the contexts. */
    HcdStrictBefore(hc, trb, &strict);
#endif
    KeClearEvent(&hc->CmdDoneEvent);
    hc->CmdDoneLost = 0;
    hc->CmdDonePA = 0;
    trbPA = 0;
    answer = XhciCommandSubmit(&hc->Hc, trb, &trbPA, XHCI_ARM_UNLOCKED);
    if (answer != XHCI_CMD_OK) {
        hc->EnumCommandsRefused++;
        return 0;
    }
    for (rounds = 0; rounds < 4; rounds++) {
        KeClearEvent(&hc->CmdDoneEvent);
        if (hc->CmdDoneLost) {
            /* The engine's own watchdog aborted it: what the command did,
             * an Enable Slot's slot among it, is unknown, and only the
             * reset settles that (round 2, finding 5). */
            break;
        }
        if (hc->CmdDonePA == trbPA) {
            *control = hc->CmdDoneControl;
#if DBG
            HcdStrictAfter(hc, &strict, hc->CmdDoneCode, *control);
#endif
            return hc->CmdDoneCode;
        }
        if (!hcdWaitEvent(&hc->CmdDoneEvent, HCD_COMMAND_WAIT_MS)) {
            break;
        }
    }
    hc->EnumCommandsTimedOut++;
    hc->Counters.CommandsGivenUp++;
    HcdSvcRequestReset(&hc->Hc);
    return 0;
}

/* A command from the thread for the configuration code (hcd_cfg.c). */
ULONG HcdThreadCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb,
                       PULONG control)
{
    return hcdCommand(hc, trb, control);
}

/* ----------------------------------------------------------------------- */
/* Device records                                                           */
/* ----------------------------------------------------------------------- */

static PHCD_USB_DEVICE hcdDeviceNew(PHCD_CONTROLLER hc, PHCD_PORT port,
                                    ULONG slotId, ULONG speed)
{
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;
    PUCHAR p;
    ULONG i;

    dev = (PHCD_USB_DEVICE)HcdPoolAlloc(sizeof(HCD_USB_DEVICE));
    if (dev == NULL) {
        return NULL;
    }
    p = (PUCHAR)dev;
    for (i = 0; i < sizeof(HCD_USB_DEVICE); i++) {
        p[i] = 0;
    }
    dev->Controller = hc;
    dev->Port = (port->Hub != NULL && port->Hub->Device != NULL)
                    ? port->Hub->Device->Port
                    : port->PortId;
    dev->Location = port->PortId;
    dev->SlotId = slotId;
    dev->Speed = speed;
    XhciXferQueueInit(&dev->Ep0Queue);
    HcdIoPipeInitEp0(dev);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->SlotDevice[slotId] = dev;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return dev;
}

/* The record leaves the slot table under the controller lock, so the event
 * DPC, which matches EP0 events through SlotDevice[] (hcd_dev.c), never
 * holds a record being freed. IRQL: PASSIVE_LEVEL. */
static VOID hcdDeviceFree(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;
    ULONG kept;

    if (dev == NULL) {
        return;
    }
    /* Its URBs first: the slot is disabled or HCRST has taken it, so the
     * engine's queue can be drained and every IRP completed (hcd_io.c). */
    kept = !HcdIoDeviceGone(hc, dev);
    if (!kept) {
        HcdCfgDeviceGone(hc, dev);
    }
    if (dev->SlotId != 0 && dev->SlotId <= XHCI_MAX_SLOTS) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (hc->SlotDevice[dev->SlotId] == dev) {
            hc->SlotDevice[dev->SlotId] = NULL;
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
    }
    if (kept) {
        /* URBs still pending against a controller that may still DMA:
         * the record and its pipes stay allocated for good. */
        hc->DevicesKept++;
        return;
    }
    HcdPoolFree(dev->Config);
    HcdPoolFree(dev->Bos);
    HcdPoolFree(dev->Selected);
    HcdPoolFree(dev);
}

/* Disable Slot for a slot id. Returns 1 when the controller confirmed it; on
 * any other outcome the reset is requested, since only the HCRST still
 * proves the slot's contexts and rings out of the controller's hands. */
static ULONG hcdDisableSlotId(PHCD_CONTROLLER hc, ULONG slotId)
{
    XHCI_TRB trb;
    ULONG control;

    if (XhciTrbDisableSlot(&trb, slotId) == XHCI_RING_OK &&
        hcdCommand(hc, &trb, &control) == XHCI_CC_SUCCESS) {
        return 1;
    }
    hc->EnumDisableFailures++;
    HcdSvcRequestReset(&hc->Hc);
    return 0;
}

/*
 * Give a slot back: Disable Slot, then its DCBAA entry cleared and its
 * record freed. A Disable Slot the controller did not confirm leaves the
 * record quarantined instead - off the port, still in SlotDevice[], its
 * DCBAA entry and EP0 ring untouched - until the recovery the failure
 * requested invalidates every slot and the thread frees it (Codex review of
 * batch (b), round 1, finding 8).
 */
static VOID hcdDisableRecord(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PXHCI_EXTENSION ext;
    ULONG slotId;
    volatile ULONG *dcbaa;

    ext = &hc->Hc;
    slotId = dev->SlotId;
    if (!hcdDisableSlotId(hc, slotId)) {
        dev->Abandoned = 0;
        return;
    }
    dcbaa = XhciCommonAt(ext, ext->Layout.DcbaaOffset + slotId * 8UL);
    dcbaa[0] = 0;
    dcbaa[1] = 0;
    hcdDeviceFree(hc, dev);
}

static VOID hcdHubLeave(PHCD_CONTROLLER hc, PHCD_PORT p);

/* The port's device gives its slot back; a hub's subtree goes first, so no
 * live slot's TT fields ever name a disabled one (section 10.5 step 4). */
static VOID hcdDisableSlot(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PHCD_USB_DEVICE dev;

    dev = p->Device;
    if (dev == NULL) {
        return;
    }
    if (dev->Hub != NULL) {
        hcdHubLeave(hc, p);
    }
    p->Device = NULL;
    hcdDisableRecord(hc, dev);
}

/* EP0's state as the controller keeps it in the output Device Context
 * (xHCI 6.2.3, EP State); Disabled when the slot has no context. */
static ULONG hcdEp0State(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    ULONG offset;

    if (XhciEndpointContextOffset(&hc->Hc.Layout, dev->SlotId, 1,
                                  &offset) != XHCI_LAYOUT_OK) {
        return XHCI_EP_STATE_DISABLED;
    }
    return XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]);
}

/*
 * EP0 needed recovery on a URB's control transfer (hcd_dev.c set
 * Ep0Halted): repositioned by the state the controller left it in, since
 * not every failure halts it - a TRB Error leaves it in Error (xHCI 4.8.3),
 * and Reset Endpoint anywhere but Halted is a Context State Error (4.6.8):
 *
 *   Halted          Reset Endpoint with TSP 0, then Set TR Dequeue;
 *   Error, Stopped  Set TR Dequeue alone;
 *   Running         Stop Endpoint, then by the state that leaves;
 *   Disabled        no command - the slot is disabled, its record on the
 *                   way out; EP0 stays paused and 0 is returned with no
 *                   reset requested.
 *
 * Set TR Dequeue goes to where the transfer engine already moved the
 * software dequeue - the next TD's head, or the enqueue position -
 * mandatory for a control endpoint after a reset (4.6.8), and the doorbell
 * again if transfers are waiting. A device clears a control endpoint's
 * stall itself at the next SETUP, so no CLEAR_FEATURE is owed
 * (.claude\batch-c-endpoint-steps.md, from the miniport). Returns 0 when a
 * command failed (EP0 then stays paused and the controller reset is
 * requested) or EP0 is Disabled. Thread only, powered.
 */
static ULONG hcdResetEp0(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG control;
    ULONG code;
    ULONG state;
    ULONG pa;
    ULONG dcs;
    ULONG waiting;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->Ep0Halted = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hc->Ep0Resets++;
    /* EP0's pipe paused for the commands: a URB's doorbell meanwhile would
     * run on the endpoint being repositioned (round 3, finding 2). */
    HcdIoPipePause(hc, &dev->Ep0Pipe);
    state = hcdEp0State(hc, dev);
    if (state == XHCI_EP_STATE_RUNNING) {
        code = 0;
        if (XhciTrbStopEndpoint(&trb, dev->SlotId, 1, 0) == XHCI_RING_OK) {
            code = hcdCommand(hc, &trb, &control);
        }
        /* A Context State Error: it halted or stopped meanwhile, which the
         * state read again tells. */
        if (code != XHCI_CC_SUCCESS && code != XHCI_CC_CONTEXT_STATE_ERROR) {
            HcdSvcRequestReset(&hc->Hc);
            return 0;
        }
        state = hcdEp0State(hc, dev);
    }
    if (state == XHCI_EP_STATE_DISABLED) {
        /* No doorbell may reach a disabled context: EP0 stays paused, and
         * the record's freeing completes what waits on it. */
        XHCI_DBG_VALUE("hcd: EP0 recovery skipped, disabled, slot",
                       dev->SlotId);
        return 0;
    }
    if (state == XHCI_EP_STATE_HALTED &&
        (XhciTrbResetEndpoint(&trb, dev->SlotId, 1, 0) != XHCI_RING_OK ||
         hcdCommand(hc, &trb, &control) != XHCI_CC_SUCCESS)) {
        HcdSvcRequestReset(&hc->Hc);
        return 0;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pa = XhciRingDequeuePA(&dev->Ep0);
    dcs = XhciRingDequeueCycle(&dev->Ep0);
    waiting = dev->Ep0Queue.Count != 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (XhciTrbSetTrDequeue(&trb, dev->SlotId, 1, pa, dcs) !=
            XHCI_RING_OK ||
        hcdCommand(hc, &trb, &control) != XHCI_CC_SUCCESS) {
        /* EP0 stays paused until the reset the failure asked for
         * invalidates the device (round 5, finding 1). */
        HcdSvcRequestReset(&hc->Hc);
        return 0;
    }
    if (waiting) {
        XhciWriteDoorbell(&hc->Hc, dev->SlotId, 1);
    }
    HcdIoPipeResume(hc, &dev->Ep0Pipe);
    XHCI_DBG_VALUE("hcd: EP0 recovered, slot/state",
                   (dev->SlotId << 8) | state);
    return 1;
}

/*
 * The records an unpowered root-hub detach left Abandoned - off their
 * ports, their slots still enabled - get their Disable Slot at the first
 * powered pass, so a restore that kept the slots does not strand them
 * (Codex review of batch (b), round 2, finding 3). A failure quarantines the
 * record as hcdDisableSlot's does. Thread only, powered.
 */
static VOID hcdSweepAbandoned(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;
    ULONG i;

    hc->SlotSweep = 0;
    for (i = 1; i <= XHCI_MAX_SLOTS && !hcdHalted(hc); i++) {
        dev = hc->SlotDevice[i];
        if (dev != NULL && dev->Abandoned) {
            hcdDisableRecord(hc, dev);
        }
    }
}

/* ----------------------------------------------------------------------- */
/* Address Device and Evaluate Context                                      */
/* ----------------------------------------------------------------------- */

static VOID hcdZeroCommon(PXHCI_EXTENSION ext, ULONG offset, ULONG bytes)
{
    volatile ULONG *w;
    ULONG i;

    w = XhciCommonAt(ext, offset);
    for (i = 0; i < bytes / 4UL; i++) {
        w[i] = 0;
    }
}

/* The input context for EP0 at `mps`, with the Slot Context too when
 * `withSlot` (Address Device) and EP0 alone otherwise (Evaluate Context). */
static ULONG hcdBuildEp0Input(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              ULONG mps, ULONG withSlot)
{
    PXHCI_EXTENSION ext;
    PXHCI_HC_LAYOUT layout;
    XHCI_SLOT_PARAMS sp;
    XHCI_EP_PARAMS ep;
    ULONG icc;
    ULONG isc;
    ULONG iep;

    ext = &hc->Hc;
    layout = &ext->Layout;
    if (XhciInputControlContextOffset(layout, &icc) != XHCI_LAYOUT_OK ||
        XhciInputSlotContextOffset(layout, &isc) != XHCI_LAYOUT_OK ||
        XhciInputEndpointContextOffset(layout, 1, &iep) != XHCI_LAYOUT_OK) {
        return 0;
    }
    hcdZeroCommon(ext, layout->InputContextOffset, layout->InputContextBytes);

    if (XhciBuildInputControlContext(
            XhciCommonAt(ext, icc),
            withSlot ? 0x3UL : 0x2UL, 0) != 0) {
        return 0;
    }
    if (withSlot) {
        /* Its place behind hubs too; never a hub's marking, which only a
         * Configure Endpoint may set (hcd_cfg.c). */
        HcdDeviceSlotParams(dev, 0, &sp);
        if (XhciBuildSlotContext(
                XhciCommonAt(ext, isc),
                &sp) != 0) {
            return 0;
        }
    }
    if (XhciBuildEp0Params(mps, dev->Ep0.BasePA, 1, &ep) != 0) {
        return 0;
    }
    if (XhciBuildEndpointContext(
            XhciCommonAt(ext, iep),
            &ep) != 0) {
        return 0;
    }
    return 1;
}

/* One speed class into one of a counter set (Super, High, Full, Low); any
 * other class counts in none, which the matrix reads as a wrong speed. */
static VOID hcdCountSpeed(ULONG speedClass, PULONG super, PULONG high,
                          PULONG full, PULONG low)
{
    if (speedClass == XHCI_SPEED_SUPER) {
        (*super)++;
    } else if (speedClass == XHCI_SPEED_HIGH) {
        (*high)++;
    } else if (speedClass == XHCI_SPEED_FULL) {
        (*full)++;
    } else if (speedClass == XHCI_SPEED_LOW) {
        (*low)++;
    }
}

/*
 * An enumeration's Address Device succeeded: the matrix's speed counters.
 * The port's speed is the PORTSC value the reset left, decoded; the slot's
 * is read back out of the Input Slot Context the command carried, decoded
 * the same way, so a builder that wrote another PSIV shows as a
 * disagreement rather than as the speed it was given.
 */
static VOID hcdCountAddressed(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PXHCI_EXTENSION ext;
    PXHCIHC_COUNTERS c;
    volatile ULONG *slot;
    ULONG portClass;
    ULONG slotClass;
    ULONG isc;

    ext = &hc->Hc;
    c = &hc->Counters;
    c->DevicesAddressed++;
    portClass = XHCI_SPEED_UNKNOWN;
    slotClass = XHCI_SPEED_UNKNOWN;
    (VOID)XhciPortSpeedClass(&ext->PortMap, dev->Port, dev->Speed,
                             &portClass);
    if (XhciInputSlotContextOffset(&ext->Layout, &isc) == XHCI_LAYOUT_OK) {
        slot = XhciCommonAt(ext, isc);
        (VOID)XhciPortSpeedClass(&ext->PortMap, dev->Port,
                                 (slot[0] & XHCI_SLOT_SPEED_MASK) >>
                                     XHCI_SLOT_SPEED_SHIFT,
                                 &slotClass);
    }
    hcdCountSpeed(portClass, &c->PortSpeedSuper, &c->PortSpeedHigh,
                  &c->PortSpeedFull, &c->PortSpeedLow);
    hcdCountSpeed(slotClass, &c->SlotSpeedSuper, &c->SlotSpeedHigh,
                  &c->SlotSpeedFull, &c->SlotSpeedLow);
    if (portClass != slotClass || portClass == XHCI_SPEED_UNKNOWN) {
        c->SpeedDisagreements++;
    }
    /* The rate beside the class (29-A.1): a SuperSpeedPlus link is
     * accepted at its trained rate and counted by it. */
    dev->RateKbps = 0;
    dev->Plus = 0;
    (VOID)XhciPortRate(&ext->PortMap, dev->Port, dev->Speed, &dev->RateKbps,
                       &dev->Plus);
    if (portClass == XHCI_SPEED_SUPER && dev->Plus) {
        c->PortSpeedSuperPlus++;
    }
}

/* Address Device with BSR = 0 for the slot the port just enabled: the EP0
 * ring at the slot's carved place, the device context in the DCBAA. */
static ULONG hcdAddress(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG mps)
{
    PXHCI_EXTENSION ext;
    PXHCI_HC_LAYOUT layout;
    PHCD_USB_DEVICE dev;
    volatile ULONG *dcbaa;
    ULONG ringOffset;
    ULONG ctxOffset;
    XHCI_TRB trb;
    ULONG control;
    LARGE_INTEGER due;

    ext = &hc->Hc;
    layout = &ext->Layout;
    dev = p->Device;

    if (XhciEp0RingOffset(layout, dev->SlotId, &ringOffset) !=
            XHCI_LAYOUT_OK ||
        XhciDeviceContextOffset(layout, dev->SlotId, &ctxOffset) !=
            XHCI_LAYOUT_OK) {
        return 0;
    }
    hcdZeroCommon(ext, ringOffset, layout->Ep0RingTrbs * XHCI_TRB_BYTES);
    if (XhciRingInit(&dev->Ep0, (volatile XHCI_TRB *)XhciCommonAt(ext,
                                                                  ringOffset),
                     XhciCommonPA(ext, ringOffset), layout->Ep0RingTrbs,
                     XHCI_RING_KIND_ENDPOINT) != XHCI_RING_OK) {
        return 0;
    }

    hcdZeroCommon(ext, ctxOffset, layout->DeviceContextBytes);
    dcbaa = XhciCommonAt(ext, layout->DcbaaOffset + dev->SlotId * 8UL);
    dcbaa[0] = XhciCommonPA(ext, ctxOffset);
    dcbaa[1] = 0;

    if (!hcdBuildEp0Input(hc, dev, mps, 1)) {
        return 0;
    }
    if (XhciTrbAddressDevice(&trb, dev->SlotId,
                             XhciCommonPA(ext, layout->InputContextOffset),
                             0) != XHCI_RING_OK) {
        return 0;
    }
    if (hcdCommand(hc, &trb, &control) != XHCI_CC_SUCCESS) {
        return 0;
    }
    hcdCountAddressed(hc, dev);
    if (dev->Tier != 0) {
        hc->Counters.TopoBehindHubAddressed++;
    }
    if (dev->TtSlot != 0) {
        hc->Counters.TopoTtProgrammed++;
    }
    dev->Mps0 = mps;
    HcdRelativeMs(&due, HCD_SETADDRESS_MS);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    return 1;
}

static ULONG hcdEvaluate(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG mps)
{
    PXHCI_EXTENSION ext;
    XHCI_TRB trb;
    ULONG control;

    ext = &hc->Hc;
    if (!hcdBuildEp0Input(hc, p->Device, mps, 0)) {
        return 0;
    }
    if (XhciTrbEvaluateContext(&trb, p->Device->SlotId,
                               XhciCommonPA(ext,
                                            ext->Layout.InputContextOffset)) !=
        XHCI_RING_OK) {
        return 0;
    }
    if (hcdCommand(hc, &trb, &control) != XHCI_CC_SUCCESS) {
        return 0;
    }
    p->Device->Mps0 = mps;
    return 1;
}

/* ----------------------------------------------------------------------- */
/* EP0 control transfers                                                    */
/* ----------------------------------------------------------------------- */

/*
 * One IN control transfer of `length` bytes into the scratch buffer,
 * through the transfer engine (xhci_xfer.c) on the device's EP0 queue: the
 * engine builds the TD group, publishes it whole or not at all, matches its
 * events by TRB address, and latches the bytes moved and the USBD status
 * (26-A.5's first step: the enumeration and the URB path share one ring and
 * one queue). `requestType` is the SETUP bmRequestType: a nonzero `length`
 * is an IN data stage into the scratch (the thread sends no OUT data), a
 * zero `length` no data stage at all (SET_CONFIGURATION, CLEAR_FEATURE).
 * Returns 1 with *bytes the count received, 0 on any failure. Thread only.
 */
/*
 * Until EP0 carries no client transfer and is not Halted: each retired one
 * is waited for (the queue's count), a STALL recovered as it is found. The
 * caller has EP0's pipe paused, which hcdResetEp0's own pause and resume
 * nest inside. Returns 0 when a recovery command failed (EP0 then stays
 * paused for the reset it requested), EP0 is Disabled, or the wait ran
 * out. Thread only.
 */
static ULONG hcdEp0Quiet(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KTIMER deadline;
    LARGE_INTEGER due;
    LARGE_INTEGER now;
    KIRQL oldIrql;
    ULONG halted;
    ULONG count;
    ULONG ok;

    /* An elapsed-time bound, not a count of 1 ms sleeps, whose length the
     * clock's resolution decides (round 13, finding 3): a timer, polled
     * with a zero wait - no clock read is on the import allowlist. */
    KeInitializeTimer(&deadline);
    HcdRelativeMs(&due, HCD_TRANSFER_WAIT_MS);
    (VOID)KeSetTimer(&deadline, due, NULL);
    now.QuadPart = 0;
    ok = 0;
    for (;;) {
        /* Both under the lock the retirement and the halt are recorded
         * under, so the last client request's STALL cannot fall between
         * the two reads (round 13, finding 1). */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        halted = dev->Ep0Halted;
        count = dev->Ep0Queue.Count;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (halted) {
            if (!hcdResetEp0(hc, dev)) {
                break;
            }
            continue;
        }
        if (count == 0) {
            ok = 1;
            break;
        }
        if (KeWaitForSingleObject(&deadline, Executive, KernelMode, FALSE,
                                  &now) == STATUS_SUCCESS) {
            hc->EnumTransfersTimedOut++;
            break;
        }
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        HcdIoDeferred(hc);
    }
    (VOID)KeCancelTimer(&deadline);
    return ok;
}

static ULONG hcdThreadControlQuiet(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                   UCHAR requestType, UCHAR request,
                                   USHORT value, USHORT index, ULONG length,
                                   PULONG bytes, PULONG stalled);

ULONG HcdThreadControl(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                       UCHAR requestType, UCHAR request, USHORT value,
                       USHORT index, ULONG length, PULONG bytes)
{
    ULONG stalled;

    return HcdThreadControlEx(hc, dev, requestType, request, value, index,
                              length, bytes, &stalled);
}

/* HcdThreadControl, and *stalled 1 only when the request reached the device
 * and the device answered it with a STALL - not when it never went out, or
 * failed any other way (Codex review of batch (c), round 19, finding 2).
 * Thread only. */
ULONG HcdThreadControlEx(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                         UCHAR requestType, UCHAR request, USHORT value,
                         USHORT index, ULONG length, PULONG bytes,
                         PULONG stalled)
{
    ULONG ok;

    *bytes = 0;
    *stalled = 0;
    if (dev->Ep0Stuck) {
        /* Its record is still queued from a timeout: reusing it would
         * relink the engine's queue (round 2, finding 10). */
        return 0;
    }
    if (length > HCD_SCRATCH_CONTROL_BYTES ||
        (length != 0 && (requestType & 0x80) == 0)) {
        return 0;
    }
    /*
     * EP0 to itself: client URBs are held at the pipe's gate, those already
     * on the ring are let finish, and a STALL among them is recovered here,
     * before this SETUP - a control transfer queued behind a Halted EP0
     * would only time out into a controller reset (Codex review of batch
     * (c), round 11, finding 2, and round 12, finding 1). A client transfer
     * that never ends within the transfer wait fails this request instead,
     * without a reset.
     */
    HcdIoPipePause(hc, &dev->Ep0Pipe);
    if (!hcdEp0Quiet(hc, dev)) {
        HcdIoPipeResume(hc, &dev->Ep0Pipe);
        return 0;
    }
    ok = hcdThreadControlQuiet(hc, dev, requestType, request, value, index,
                               length, bytes, stalled);
    if (!dev->Ep0Stuck) {
        /* A timed-out one leaves EP0 paused for the reset it requested. */
        HcdIoPipeResume(hc, &dev->Ep0Pipe);
    }
    return ok;
}

/* The transfer itself, on a quiet EP0. Thread only. */
static ULONG hcdThreadControlQuiet(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                   UCHAR requestType, UCHAR request,
                                   USHORT value, USHORT index, ULONG length,
                                   PULONG bytes, PULONG stalled)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_TRB trbs[XHCI_XFER_MAX_CONTROL_TRBS];
    USBPORT_SCATTER_GATHER_LIST sg;
    KIRQL oldIrql;
    PUCHAR b;
    ULONG answer;
    ULONG done;
    ULONG i;

    b = (PUCHAR)&sg;
    for (i = 0; i < sizeof(sg); i++) {
        b[i] = 0;
    }
    sg.SgElementCount = (length != 0) ? 1 : 0;
    sg.SgElement[0].SgPhysicalAddressLo = hc->ScratchPa.LowPart;
    sg.SgElement[0].SgTransferLength = length;
    sg.SgElement[0].SgOffset = 0;

    b = (PUCHAR)&req;
    for (i = 0; i < sizeof(req); i++) {
        b[i] = 0;
    }
    req.Setup.bmRequestType = requestType;
    req.Setup.bRequest = request;
    req.Setup.wValue = value;
    req.Setup.wIndex = index;
    req.Setup.wLength = (USHORT)length;
    req.TransferLength = length;
    req.TransferFlagsIn = (requestType & 0x80) != 0;
    req.MaxPacketSize = dev->Mps0;
    req.SgList = &sg;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->Ep0Done = 0;
    KeClearEvent(&hc->XferDoneEvent);
    answer = XhciXferSubmitControl(&dev->Ep0Queue, &dev->Ep0, &req,
                                   &dev->Ep0Xfer, dev, trbs,
                                   XHCI_XFER_MAX_CONTROL_TRBS);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (answer != XHCI_XFER_OK) {
        return 0;
    }
    XhciWriteDoorbell(&hc->Hc, dev->SlotId, 1);

    done = hcdWaitEvent(&hc->XferDoneEvent, HCD_TRANSFER_WAIT_MS) &&
           dev->Ep0Done;
    if (!done) {
        /* The TD is still on the ring and may yet DMA into the scratch:
         * nothing reuses it until the recovery's HCRST has taken every slot
         * (Codex review of batch (b), round 1, finding 8). The record stays
         * on the queue; the invalidation frees the device with it. */
        hc->EnumTransfersTimedOut++;
        hc->ScratchTainted = 1;
        dev->Ep0Stuck = 1;
        HcdSvcRequestReset(&hc->Hc);
        return 0;
    }
    if (dev->Ep0Xfer.UsbdStatus != XHCI_USBD_STATUS_SUCCESS) {
        *stalled = dev->Ep0Xfer.UsbdStatus == XHCI_USBD_STATUS_STALL_PID;
        return 0;
    }
    *bytes = dev->Ep0Xfer.BytesTransferred;
    return 1;
}

static ULONG hcdGetDescriptor(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              UCHAR type, ULONG length, PULONG bytes)
{
    return HcdThreadControl(hc, dev, 0x80, 6, (USHORT)((ULONG)type << 8), 0, length,
                        bytes);
}

/* ----------------------------------------------------------------------- */
/* Root ports                                                               */
/* ----------------------------------------------------------------------- */

/*
 * The SuperSpeed half of a root port reset (29-A.2): which reset the link's
 * state allows - hot from U0, warm from any state a hot one cannot start in,
 * none from a link with nothing trained or one held Disabled - written, and
 * counted. Returns the PORTSC value written, or 0 when no reset is owed;
 * *wait the time to give it.
 */
static ULONG hcdUsb3ResetWrite(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG portsc,
                               PULONG wait)
{
    XHCI_LINK_ACTION act;

    switch (XhciLinkDecide(&p->Link, portsc, XHCI_LINK_WANT_RESET, &act)) {
    case XHCI_LINK_ACT_HOT_RESET:
        *wait = HCD_RESET_WAIT_MS;
        return XhciPortscReset(portsc);
    case XHCI_LINK_ACT_WARM_RESET:
        /* A warm reset this driver's policy chose for a link not in U0;
         * the xHC's own conversions are counted from WRC (hcdResetPort). */
        hc->Counters.SsWarmResets++;
        XHCI_DBG_VALUE("hcd: warm reset, port/policy",
                       (p->PortId << 8) | act.Converted);
        *wait = HCD_WARM_RESET_WAIT_MS;
        return XhciPortscWarmReset(portsc);
    case XHCI_LINK_ACT_GIVE_UP:
        hc->Counters.SsLinksGivenUp++;
        XHCI_DBG_VALUE("hcd: SuperSpeed link given up, port", p->PortId);
        return 0;
    default:
        return 0;
    }
}

/* Reset a root port and wait for the xHC to finish it; the speed is read
 * from PORTSC once it has. A USB3 protocol port's reset is hot or warm by
 * its link's state (hcdUsb3ResetWrite), and completes with PRC - and WRC
 * when warm - and the link in U0. Returns 1 when the port came back
 * enabled. */
static ULONG hcdResetPort(PHCD_CONTROLLER hc, PHCD_PORT p, PULONG speed)
{
    PXHCI_EXTENSION ext;
    LARGE_INTEGER due;
    ULONG portsc;
    ULONG waited;
    ULONG wait;
    ULONG write;
    ULONG usb3;
    ULONG warm;
    ULONG port;

    ext = &hc->Hc;
    port = p->PortId;
    usb3 = XhciPortIsUsb3(&ext->PortMap, port);
    portsc = XhciReadPortsc(ext, port);
    if (portsc == 0xFFFFFFFFUL ||
        (!usb3 && (portsc & XHCI_PORTSC_CCS) == 0)) {
        return 0;
    }
    wait = HCD_RESET_WAIT_MS;
    if (usb3) {
        write = hcdUsb3ResetWrite(hc, p, portsc, &wait);
        if (write == 0) {
            return 0;
        }
    } else {
        write = XhciPortscReset(portsc);
    }
    XhciWritePortsc(ext, port, write);
    for (waited = 0; waited < wait; waited += HCD_POLL_STEP_MS) {
        HcdRelativeMs(&due, HCD_POLL_STEP_MS);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        portsc = XhciReadPortsc(ext, port);
        if (portsc == 0xFFFFFFFFUL) {
            return 0;
        }
        if ((portsc & XHCI_PORTSC_PRC) != 0) {
            break;
        }
    }
    XhciWritePortsc(ext, port,
                    XhciPortscClearChanges(portsc, XHCI_PORTSC_PRC |
                                                       (usb3 ? XHCI_PORTSC_WRC
                                                             : 0)));
    if (usb3) {
        warm = 0;
        if (!XhciLinkResetDone(portsc, &warm)) {
            XHCI_DBG_VALUE("hcd: SuperSpeed reset failed, PORTSC", portsc);
            return 0;
        }
        /* A hot reset asked for that the xHC carried out warm - after a
         * failed hot-reset handshake (4.19.5.1) - shows as WRC; counted
         * from what was observed, not from what was written (Codex review
         * of Phase 29, round 1, finding 5). */
        if (warm && p->Link.LastReset == XHCI_LINK_ACT_HOT_RESET) {
            hc->Counters.SsResetsConverted++;
            XHCI_DBG_VALUE("hcd: hot reset converted to warm, port", port);
        }
    } else if ((portsc & XHCI_PORTSC_PRC) == 0 ||
               (portsc & XHCI_PORTSC_PED) == 0) {
        return 0;
    }
    *speed = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
    HcdRelativeMs(&due, HCD_RESET_RECOVERY_MS);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    return 1;
}

static ULONG hcdPortConnected(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    ULONG portsc;
    ULONG status;
    ULONG change;

    if (p->Hub != NULL) {
        return HcdHubPortStatus(hc, p->Hub, p->Number, &status, &change) &&
               (status & XHCI_HUB_PORT_CONNECTION) != 0;
    }
    portsc = XhciReadPortsc(&hc->Hc, p->PortId);
    return portsc != 0xFFFFFFFFUL && (portsc & XHCI_PORTSC_CCS) != 0;
}

/* Reset the port the device is on - its root port, or its hub's port - and
 * read the speed class it came back at. */
static ULONG hcdPortReset(PHCD_CONTROLLER hc, PHCD_PORT p,
                          PULONG speedClass)
{
    ULONG speed;

    if (p->Hub != NULL) {
        return HcdHubPortReset(hc, p->Hub, p->Number, speedClass);
    }
    speed = 0;
    *speedClass = XHCI_SPEED_UNKNOWN;
    if (!hcdResetPort(hc, p, &speed)) {
        return 0;
    }
    (VOID)XhciPortSpeedClass(&hc->Hc.PortMap, p->PortId, speed, speedClass);
    return 1;
}

/*
 * RESET_PORT's hardware half (hcd_cfg.c), on a slot that is kept. The order
 * is the spec's: the Reset Device Command "is used by software to inform the
 * xHC that the USB Device associated with a Device Slot has been Reset (by
 * ... setting the Root Hub port PR flag ...)", and "Undefined behavior may
 * occur if this command is executed and the device associated with it is not
 * successfully reset" (xHCI 1.2 section 4.6.11; xhci-data-structures.md,
 * "Which Slot State each command requires"). So the port reset comes first;
 * Reset Device then takes the slot from Addressed or Configured to Default,
 * with USB address 0, Context Entries 1 and every endpoint but EP0 Disabled;
 * and Address Device with BSR = 0, legal from Default (4.6.5), gives the
 * device its address again on an EP0 ring started afresh. The Output Device
 * Context and its DCBAA entry stay as they are: unlike hcdAddress's slot,
 * this one is the controller's already. The caller has every endpoint at
 * rest and its pipe paused ("Software should stop all endpoint activity
 * before issuing a Reset Device Command", 4.6.11), EP0's queue empty. Returns
 * 0 on any failure; a command that never answered has requested the
 * controller reset. Thread only, powered.
 */
ULONG HcdThreadReaddress(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PXHCI_EXTENSION ext;
    PXHCI_HC_LAYOUT layout;
    LARGE_INTEGER due;
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG ringOffset;
    ULONG control;
    ULONG speed;
    ULONG was;
    ULONG code;
    ULONG ok;

    ext = &hc->Hc;
    layout = &ext->Layout;
    speed = XHCI_SPEED_UNKNOWN;
    was = XHCI_SPEED_UNKNOWN;
    (VOID)XhciPortSpeedClass(&ext->PortMap, dev->Port, dev->Speed, &was);
    if (hcdHalted(hc) || dev->Ep0Stuck || dev->Location == 0 ||
        dev->Location > HCD_PORT_COUNT ||
        !hcdPortReset(hc, &hc->Ports[dev->Location - 1], &speed) ||
        speed != was) {
        XHCI_DBG_VALUE("hcd: reset port, port reset failed, speed", speed);
        return 0;
    }
    code = 0;
    if (XhciTrbResetDevice(&trb, dev->SlotId) == XHCI_RING_OK) {
        code = hcdCommand(hc, &trb, &control);
    }
    if (code != XHCI_CC_SUCCESS) {
        XHCI_DBG_VALUE("hcd: reset port, Reset Device code", code);
        return 0;
    }
    if (XhciEp0RingOffset(layout, dev->SlotId, &ringOffset) !=
        XHCI_LAYOUT_OK) {
        return 0;
    }
    hcdZeroCommon(ext, ringOffset, layout->Ep0RingTrbs * XHCI_TRB_BYTES);
    /* Under the lock the event DPC reads EP0's ring and halt flag under;
     * the queue is empty, so no event of the old ring is still owed. */
    XhciControllerLockAcquire(ext, &oldIrql);
    ok = XhciRingInit(&dev->Ep0,
                      (volatile XHCI_TRB *)XhciCommonAt(ext, ringOffset),
                      XhciCommonPA(ext, ringOffset), layout->Ep0RingTrbs,
                      XHCI_RING_KIND_ENDPOINT) == XHCI_RING_OK &&
         dev->Ep0Queue.Count == 0;
    dev->Ep0Halted = 0;
    XhciControllerLockRelease(ext, oldIrql);
    if (!ok || !hcdBuildEp0Input(hc, dev, dev->Mps0, 1) ||
        XhciTrbAddressDevice(&trb, dev->SlotId,
                             XhciCommonPA(ext, layout->InputContextOffset),
                             0) != XHCI_RING_OK) {
        return 0;
    }
    code = hcdCommand(hc, &trb, &control);
    if (code != XHCI_CC_SUCCESS) {
        XHCI_DBG_VALUE("hcd: reset port, Address Device code", code);
        return 0;
    }
    HcdRelativeMs(&due, HCD_SETADDRESS_MS);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    return 1;
}

/*
 * CYCLE_PORT, or a RESET_PORT that failed: ask the thread to drop the device
 * on `port` - what an unplug does - if it is still the one PDO `serial`
 * stands for, so a request that arrives after the device has already gone and
 * come back does not drop its successor. IRQL: <= DISPATCH_LEVEL.
 */
VOID HcdEnumCycle(PHCD_CONTROLLER hc, ULONG port, ULONG serial)
{
    KIRQL oldIrql;

    if (port == 0 || port > HCD_PORT_COUNT || serial == 0) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->PortCycle[(port - 1) / 32UL] |= 1UL << ((port - 1) % 32UL);
    hc->PortCycleSerial[port - 1] = serial;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdThreadWake(hc);
}

/* Whether the device on the port is still the one that PDO group stands
 * for (a lone device PDO's group is its serial). Thread only (dev->Pdo's
 * writer). */
static ULONG hcdCycleOwns(PHCD_PORT p, ULONG serial)
{
    return p->Device != NULL && p->Device->Pdo != NULL &&
           p->Device->PdoGroup == serial;
}

/* ----------------------------------------------------------------------- */
/* The executor                                                             */
/* ----------------------------------------------------------------------- */

/* A decoded class as the machine's speed: its default ID (xhci_enum.h).
 * 0 for a class the machine does not take, which fails the reset as a
 * speed failure. */
static ULONG hcdEnumSpeedOf(ULONG speedClass)
{
    switch (speedClass) {
    case XHCI_SPEED_LOW:
        return XHCI_ENUM_SPEED_LOW;
    case XHCI_SPEED_FULL:
        return XHCI_ENUM_SPEED_FULL;
    case XHCI_SPEED_HIGH:
        return XHCI_ENUM_SPEED_HIGH;
    case XHCI_SPEED_SUPER:
        return XHCI_ENUM_SPEED_SUPER;
    default:
        return 0;
    }
}

/*
 * A SuperSpeed-capable device that enumerated on a root port's USB 2.0 half:
 * its SuperSpeed link did not train, or was given up, and it fell back
 * (29-A.5, the passive case). bcdUSB is no evidence - such a device reports
 * 0210h on its USB 2.0 connection, and so do USB 2.0 devices with LPM
 * (Codex review of Phase 29, round 1, finding 4) - so the evidence is its
 * BOS descriptor's SuperSpeed USB Device Capability, read here for a device
 * at bcdUSB 0210h or above on a USB 2.0 companion port only. A failed or
 * stalled read counts nothing and changes nothing: the enumeration goes on
 * from the device descriptor it already has, and a STALL is recovered
 * before the next control transfer (hcdEp0Quiet). The scratch is
 * overwritten; the caller has copied what it needed out of it. Thread only.
 */
static VOID hcdProbeFallback(PHCD_CONTROLLER hc, PHCD_PORT p,
                             PHCD_USB_DEVICE dev)
{
    XHCI_PIPE_BOS bos;
    ULONG bytes;
    ULONG total;
    PUCHAR s;

    if (p->Hub != NULL || dev == NULL ||
        XhciPortClass(&hc->Hc.PortMap, p->PortId) !=
            XHCI_PORT_CLASS_USB2_COMPANION ||
        ((ULONG)dev->DeviceDesc[2] | ((ULONG)dev->DeviceDesc[3] << 8)) <
            0x0210UL) {
        return;
    }
    s = (PUCHAR)hc->ScratchVa;
    bytes = 0;
    if (!hcdGetDescriptor(hc, dev, HCD_DESC_BOS, XHCI_ENUM_BOS_HEAD_BYTES,
                          &bytes) ||
        bytes < XHCI_ENUM_BOS_HEAD_BYTES) {
        return;
    }
    total = (ULONG)s[2] | ((ULONG)s[3] << 8);
    if (total < XHCI_ENUM_BOS_HEAD_BYTES ||
        total > HCD_SCRATCH_CONTROL_BYTES ||
        !hcdGetDescriptor(hc, dev, HCD_DESC_BOS, total, &bytes) ||
        bytes != total ||
        XhciPipeParseBos(s, bytes, &bos) != XHCI_PIPE_OK ||
        !bos.HasSuperSpeed) {
        return;
    }
    hc->Counters.SsDevicesOnUsb2++;
    XHCI_DBG_VALUE("hcd: SuperSpeed-capable device on its USB 2.0 path, port",
                   p->PortId);
}

static VOID hcdEventInit(PXHCI_ENUM_EVENT e, ULONG kind, ULONG ok)
{
    e->Kind = kind;
    e->Ok = ok;
    e->Speed = 0;
    e->SlotId = 0;
    e->Bytes = 0;
    e->Value = 0;
}

/*
 * Carry out one action and describe its outcome as the next event.
 * Returns 0 when the action has no outcome to feed back.
 */
static ULONG hcdPerform(PHCD_CONTROLLER hc, PHCD_PORT p,
                        const XHCI_ENUM_ACTION *act, PXHCI_ENUM_EVENT next)
{
    XHCI_ENUM_ACTION none;
    LARGE_INTEGER due;
    XHCI_TRB trb;
    ULONG control;
    ULONG code;
    ULONG speed;
    ULONG speedClass;
    ULONG bytes;
    ULONG ok;
    PUCHAR s;

    s = (PUCHAR)hc->ScratchVa;
    switch (act->Kind) {
    case XHCI_ENUM_ACT_DEBOUNCE:
        if (p->Hub != NULL) {
            hcdEventInit(next, XHCI_ENUM_EV_DEBOUNCED,
                         HcdHubPortDebounce(hc, p->Hub, p->Number));
            return 1;
        }
        HcdRelativeMs(&due, HCD_DEBOUNCE_MS);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        hcdEventInit(next, XHCI_ENUM_EV_DEBOUNCED, hcdPortConnected(hc, p));
        return 1;

    case XHCI_ENUM_ACT_RESET:
        speed = 0;
        if (p->Hub != NULL) {
            /* The machine takes the default ID of the class the hub
             * reported; the slot's own ID is the root port protocol's,
             * looked up at Enable Slot (HcdHubPlace). */
            ok = HcdHubPortReset(hc, p->Hub, p->Number, &p->HubSpeedClass);
            speed = ok ? XhciHubEnumSpeed(p->HubSpeedClass) : 0;
        } else {
            /* The raw PSIV is kept for the Slot Context; the machine is
             * given the class the controller's PSI table decodes it to. */
            ok = hcdResetPort(hc, p, &speed);
            p->LinkPsiv = ok ? speed : 0;
            speedClass = XHCI_SPEED_UNKNOWN;
            if (ok) {
                (VOID)XhciPortSpeedClass(&hc->Hc.PortMap, p->PortId, speed,
                                         &speedClass);
            }
            speed = hcdEnumSpeedOf(speedClass);
        }
        hcdEventInit(next, XHCI_ENUM_EV_RESET_DONE, ok);
        next->Speed = speed;
        XHCI_DBG_VALUE("hcd: port reset, location/ok/speed",
                       (p->PortId << 16) | (ok << 8) | speed);
        return 1;

    case XHCI_ENUM_ACT_ENABLE_SLOT:
        hcdEventInit(next, XHCI_ENUM_EV_COMMAND_DONE, 0);
        if (XhciTrbEnableSlot(&trb, 0) != XHCI_RING_OK) {
            return 1;
        }
        code = hcdCommand(hc, &trb, &control);
        if (code == XHCI_CC_SUCCESS) {
            next->SlotId = XHCI_TRB_GET_SLOT_ID(control);
            if (next->SlotId == 0 || next->SlotId > XHCI_MAX_SLOTS ||
                hc->SlotDevice[next->SlotId] != NULL) {
                /* A slot id no record can take, or one a quarantined
                 * record still holds: the controller and this driver
                 * disagree, which only its reset settles. */
                next->SlotId = 0;
                HcdSvcRequestReset(&hc->Hc);
                return 1;
            }
            if (hcdDeviceNew(hc, p, next->SlotId,
                             p->Hub == NULL ? p->LinkPsiv : p->Enum.Speed) ==
                NULL) {
                /* The slot is enabled but unowned: give it back here, the
                 * machine never learns of it (round 1, finding 11). */
                (VOID)hcdDisableSlotId(hc, next->SlotId);
                next->SlotId = 0;
                return 1;
            }
            p->Device = hc->SlotDevice[next->SlotId];
            if (p->Hub != NULL &&
                !HcdHubPlace(hc, p, p->HubSpeedClass, p->Device)) {
                /* No place to address it at: the slot goes back, and the
                 * machine fails as for no slot. */
                hcdDisableSlot(hc, p);
                next->SlotId = 0;
                return 1;
            }
            hc->Counters.SlotsEnabled++;
            next->Ok = 1;
        }
        XHCI_DBG_VALUE("hcd: enable slot, code/slot", (code << 8) |
                                                          next->SlotId);
        return 1;

    case XHCI_ENUM_ACT_ADDRESS:
        ok = hcdAddress(hc, p, act->Mps0);
        hcdEventInit(next, XHCI_ENUM_EV_COMMAND_DONE, ok);
        XHCI_DBG_VALUE("hcd: address device, ok", ok);
        return 1;

    case XHCI_ENUM_ACT_EVALUATE:
        ok = hcdEvaluate(hc, p, act->Mps0);
        hcdEventInit(next, XHCI_ENUM_EV_COMMAND_DONE, ok);
        return 1;

    case XHCI_ENUM_ACT_GET_DEVICE:
        bytes = 0;
        ok = hcdGetDescriptor(hc, p->Device, HCD_DESC_DEVICE, act->Length,
                              &bytes);
        hcdEventInit(next, XHCI_ENUM_EV_TRANSFER_DONE, ok);
        next->Bytes = bytes;
        if (ok && bytes >= 8) {
            next->Value = s[7];
        }
        if (ok && bytes == XHCI_ENUM_DEVICE_DESC_BYTES) {
            hcdCopy(p->Device->DeviceDesc, s,
                         XHCI_ENUM_DEVICE_DESC_BYTES);
            XHCI_DBG_VALUE("hcd: device descriptor, idVendor/idProduct",
                           ((ULONG)s[9] << 24) | ((ULONG)s[8] << 16) |
                               ((ULONG)s[11] << 8) | (ULONG)s[10]);
            hcdProbeFallback(hc, p, p->Device);
        }
        return 1;

    case XHCI_ENUM_ACT_GET_CONFIG:
        bytes = 0;
        ok = hcdGetDescriptor(hc, p->Device, HCD_DESC_CONFIGURATION,
                              act->Length, &bytes);
        hcdEventInit(next, XHCI_ENUM_EV_TRANSFER_DONE, ok);
        next->Bytes = bytes;
        if (ok && act->Length == XHCI_ENUM_CONFIG_HEAD_BYTES && bytes >= 4) {
            next->Value = (ULONG)s[2] | ((ULONG)s[3] << 8);
        }
        if (ok && act->Length != XHCI_ENUM_CONFIG_HEAD_BYTES &&
            bytes == act->Length) {
            p->Device->Config = (PUCHAR)HcdPoolAlloc(bytes);
            if (p->Device->Config == NULL) {
                next->Ok = 0;
                return 1;
            }
            hcdCopy(p->Device->Config, s, bytes);
            p->Device->ConfigLength = bytes;
            XHCI_DBG_VALUE("hcd: configuration descriptor, bytes", bytes);
        }
        return 1;

    case XHCI_ENUM_ACT_GET_BOS:
        /* SuperSpeed (29-A.3): the head for wTotalLength, then the whole,
         * kept with what XhciPipeParseBos made of it. A failure of either
         * read is counted and the machine goes on without it. */
        bytes = 0;
        ok = hcdGetDescriptor(hc, p->Device, HCD_DESC_BOS, act->Length,
                              &bytes);
        hcdEventInit(next, XHCI_ENUM_EV_TRANSFER_DONE, ok);
        next->Bytes = bytes;
        if (ok && act->Length == XHCI_ENUM_BOS_HEAD_BYTES && bytes >= 4) {
            next->Value = (ULONG)s[2] | ((ULONG)s[3] << 8);
        }
        if (act->Length == XHCI_ENUM_BOS_HEAD_BYTES) {
            if (!ok || bytes < XHCI_ENUM_BOS_HEAD_BYTES ||
                next->Value < XHCI_ENUM_BOS_HEAD_BYTES) {
                hc->Counters.SsBosMissing++;
            }
            return 1;
        }
        if (!ok || bytes != act->Length ||
            XhciPipeParseBos(s, bytes, &p->Device->BosInfo) !=
                XHCI_PIPE_OK) {
            hc->Counters.SsBosMissing++;
            return 1;
        }
        HcdPoolFree(p->Device->Bos);
        p->Device->Bos = (PUCHAR)HcdPoolAlloc(bytes);
        p->Device->BosLength = 0;
        if (p->Device->Bos != NULL) {
            hcdCopy(p->Device->Bos, s, bytes);
            p->Device->BosLength = bytes;
        }
        XHCI_DBG_VALUE("hcd: BOS, bytes/capabilities",
                       (bytes << 8) | p->Device->BosInfo.Capabilities);
        return 1;

    case XHCI_ENUM_ACT_CREATE_PDO:
        speedClass = XHCI_SPEED_UNKNOWN;
        (VOID)XhciPortSpeedClass(&hc->Hc.PortMap, p->Device->Port,
                                 p->Device->Speed, &speedClass);
        if (p->Device->DeviceDesc[4] == XHCI_HUB_CLASS &&
            speedClass == XHCI_SPEED_SUPER) {
            /* A SuperSpeed hub is Phase 30's: its hub class, descriptor
             * and Route String handling are not this bus's yet. Its USB
             * 2.0 half on the companion port is served as before. */
            XHCI_DBG_VALUE("hcd: SuperSpeed hub refused (Phase 30), port",
                           p->PortId);
            hcdEventInit(next, XHCI_ENUM_EV_PDO_CREATED, 0);
            return 1;
        }
        if (p->Device->DeviceDesc[4] == XHCI_HUB_CLASS) {
            /* A hub is the bus's and never a PDO (section 10.3): brought
             * up here, it is Present with nothing for PnP to start, so the
             * machine is told its PDO exists and has started at once. */
            ok = HcdHubStart(hc, p, p->Device);
            hcdEventInit(next, XHCI_ENUM_EV_PDO_CREATED, ok);
            if (!ok) {
                return 1;
            }
            (VOID)XhciEnumStep(&p->Enum, next, &none);
            hcdEventInit(next, XHCI_ENUM_EV_PDO_STARTED, 1);
            XHCI_DBG_VALUE("hcd: hub enumerated at location", p->PortId);
            return 1;
        }
        hcdEventInit(next, XHCI_ENUM_EV_PDO_CREATED,
                     NT_SUCCESS(HcdDevicePdoCreate(hc, p->Device)));
        XHCI_DBG_VALUE("hcd: device enumerated at location", p->PortId);
        return 1;

    case XHCI_ENUM_ACT_DISABLE_SLOT:
        hcdDisableSlot(hc, p);
        return 0;

    case XHCI_ENUM_ACT_REPORT_GONE:
        /* The machine waits in Gone for that PDO's deletion (AwaitSerial,
         * checked when PortPdoRemoved says to look) unless it went at
         * once. */
        p->AwaitSerial = HcdDevicePdoGone(hc, p->Device);
        hcdDisableSlot(hc, p);
        if (p->AwaitSerial != 0 || p->AwaitHub != NULL) {
            return 0;
        }
        hcdEventInit(next, XHCI_ENUM_EV_PDO_REMOVED, 1);
        return 1;

    default:
        return 0;
    }
}

/* Run the machine from one event until it asks for nothing, retrying once
 * from Reset after a failure while the port still reads connected. A halted
 * controller (hcdHalted) ends the run where it stands; the invalidation that
 * follows the recovery settles the port from any state. */
static VOID hcdRun(PHCD_CONTROLLER hc, PHCD_PORT p, XHCI_ENUM_EVENT event)
{
    XHCI_ENUM_ACTION act;
    ULONG guard;

    for (guard = 0; guard < 64; guard++) {
        /* An outcome is always fed back before the run can stop: a halt is
         * looked for only between the machine's step and the next action,
         * so a PDO just created is recorded (PdoExists) and the
         * invalidation that follows takes the port to Gone, not Empty
         * (Codex review of batch (b), round 3, finding 1). */
        (VOID)XhciEnumStep(&p->Enum, &event, &act);
        if (act.Kind != XHCI_ENUM_ACT_NONE && hcdHalted(hc)) {
            break;
        }
        if (act.Kind == XHCI_ENUM_ACT_NONE ||
            !hcdPerform(hc, p, &act, &event)) {
            if (p->Enum.State != XHCI_ENUM_FAILED || hcdHalted(hc) ||
                !hcdPortConnected(hc, p)) {
                break;
            }
            (VOID)XhciEnumRetry(&p->Enum, &act);
            if (act.Kind == XHCI_ENUM_ACT_NONE) {
                XHCI_DBG_VALUE("hcd: enumeration failed, port/cause",
                               (p->PortId << 16) | p->Enum.FailCause);
                break;
            }
            if (!hcdPerform(hc, p, &act, &event)) {
                break;
            }
        }
    }
}

static VOID hcdFeed(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG kind)
{
    XHCI_ENUM_EVENT event;

    hcdEventInit(&event, kind, 1);
    hcdRun(hc, p, event);
}

/*
 * One root port marked changed: read it, acknowledge the change bits this
 * read saw - only those, so a change arriving after the read stays for the
 * next pass (Codex review of batch (b), round 1, finding 10) - and feed its
 * machine what XhciLinkPortFeed decides (xhci_link.c), which is design
 * record 13 section 5.3's connect rule (finding 9) for a USB 2.0 port and a
 * usable USB3 link:
 *
 *   CSC set     the device left, or left and came back: whatever the port
 *               held goes (DISCONNECT), and a connection now is a new device
 *               (CONNECT);
 *   CSC clear   a change of another kind (the reset's own PRC among them):
 *               a port that reads disconnected goes, and an Empty one that
 *               reads connected starts - a Failed port waits for a new
 *               connection rather than retrying for ever.
 *
 * A USB3 link in SS.Inactive, Compliance Mode or Cold Attach (29-A.2) is fed
 * a disconnect from any state that holds something, Failed included, and is
 * warm-reset within its budget or given up; the trained link's reset
 * completion then finds the machine Empty and starts it (Codex review of
 * Phase 29, round 1, findings 2 and 3).
 */
static VOID hcdPortChanged(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PXHCI_EXTENSION ext;
    XHCI_LINK_ACTION act;
    ULONG portsc;
    ULONG changes;
    ULONG feed;

    ext = &hc->Hc;
    portsc = XhciReadPortsc(ext, p->PortId);
    if (portsc == 0xFFFFFFFFUL) {
        return;
    }
    changes = portsc & XHCI_PORTSC_CHANGE_MASK;
    if (changes != 0) {
        XhciWritePortsc(ext, p->PortId,
                        XhciPortscClearChanges(portsc, changes));
    }
    feed = XhciLinkPortFeed(&p->Link, XhciPortIsUsb3(&ext->PortMap, p->PortId),
                            portsc, p->Enum.State, &act);
    if ((feed & XHCI_LINK_FEED_DISCONNECT) != 0) {
        hcdFeed(hc, p, XHCI_ENUM_EV_DISCONNECT);
    }
    switch (act.Kind) {
    case XHCI_LINK_ACT_WARM_RESET:
        hc->Counters.SsWarmResets++;
        XHCI_DBG_VALUE("hcd: SuperSpeed link recovery, port/PLS",
                       (p->PortId << 8) | XHCI_PORTSC_GET_PLS(portsc));
        XhciWritePortsc(ext, p->PortId, XhciPortscWarmReset(portsc));
        break;
    case XHCI_LINK_ACT_GIVE_UP:
        hc->Counters.SsLinksGivenUp++;
        XHCI_DBG_VALUE("hcd: SuperSpeed link given up, port", p->PortId);
        break;
    default:
        break;
    }
    if ((feed & XHCI_LINK_FEED_CONNECT) != 0 && !hcdHalted(hc)) {
        hcdFeed(hc, p, XHCI_ENUM_EV_CONNECT);
    }
}

/* ----------------------------------------------------------------------- */
/* Waiting in Gone                                                          */
/* ----------------------------------------------------------------------- */

static ULONG hcdHubQuiet(PHCD_CONTROLLER hc, PHCD_HUB hub);

/*
 * Whether a port in Gone may leave it: the PDO group it reported is deleted -
 * that group, by serial, not whichever PDO last named its location (Codex
 * review of batch (b), round 2, finding 2) - and, where a hub left from it,
 * every port of that hub's subtree has settled the same way, the departed
 * hub object then freed. Until then nothing is enumerated at the place, so
 * no device PDO is created beside one PnP still holds under the same
 * instance id. Thread, or the start and stop with the thread not running.
 */
static ULONG hcdPortQuiet(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    if (p->AwaitSerial != 0 && HcdDevicePdoExists(hc, p->AwaitSerial)) {
        return 0;
    }
    p->AwaitSerial = 0;
    if (p->AwaitHub != NULL) {
        if (!hcdHubQuiet(hc, p->AwaitHub)) {
            return 0;
        }
        HcdHubFree(hc, p->AwaitHub);
        p->AwaitHub = NULL;
    }
    return 1;
}

/* A port in Gone leaves it once quiet. Pure transitions, no action run.
 * Returns 1 when it left. */
static ULONG hcdSettleGone(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    XHCI_ENUM_EVENT event;
    XHCI_ENUM_ACTION act;

    if (p->Enum.State != XHCI_ENUM_GONE || !hcdPortQuiet(hc, p)) {
        return 0;
    }
    hcdEventInit(&event, XHCI_ENUM_EV_PDO_REMOVED, 1);
    (VOID)XhciEnumStep(&p->Enum, &event, &act);
    return 1;
}

/* Whether every port of a departed hub has left Gone, settling those that
 * may. The recursion is bounded by the hub depth (XHCI_TOPO_MAX_TIER). */
static ULONG hcdHubQuiet(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    PHCD_PORT q;
    ULONG quiet;
    ULONG n;

    quiet = 1;
    for (n = 1; n <= HCD_HUB_MAX_PORTS; n++) {
        q = HcdHubPort(hc, hub, n);
        if (q->Enum.State == XHCI_ENUM_GONE && !hcdSettleGone(hc, q)) {
            quiet = 0;
        }
    }
    return quiet;
}

/* A port to be looked at on the next pass: a root port's change bit, or
 * its live hub's Changed bit. */
static VOID hcdMarkChanged(PHCD_PORT p, PULONG changed)
{
    PHCD_HUB hub;

    hub = p->Hub;
    if (hub == NULL) {
        changed[(p->PortId - 1) / 32UL] |= 1UL << ((p->PortId - 1) % 32UL);
    } else if (hub->Used && !hub->Draining && hub->Device != NULL) {
        hub->Changed |= 1UL << p->Number;
    }
}

/* A PDO was deleted somewhere: every port waiting on a departed hub, and
 * every waiting port of a live hub, gets its chance to settle, and one that
 * does is looked at again - a device on it now is enumerated afresh. */
static VOID hcdSettleWaiting(PHCD_CONTROLLER hc, PULONG changed)
{
    PHCD_HUB hub;
    PHCD_PORT p;
    ULONG i;
    ULONG n;

    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        p = &hc->Ports[i];
        if (p->AwaitHub != NULL && hcdSettleGone(hc, p)) {
            hcdMarkChanged(p, changed);
        }
    }
    for (i = 0; i < HCD_MAX_HUBS; i++) {
        hub = &hc->Hubs[i];
        if (!hub->Used || hub->Draining) {
            continue;
        }
        for (n = 1; n <= hub->Ports; n++) {
            p = HcdHubPort(hc, hub, n);
            if (hcdSettleGone(hc, p)) {
                hcdMarkChanged(p, changed);
            }
        }
    }
}

/* ----------------------------------------------------------------------- */
/* A hub leaving                                                            */
/* ----------------------------------------------------------------------- */

/*
 * The hub on port p is leaving with its controller powered (a disconnect
 * upstream, a failed upstream port; design record 13 section 10.5): every
 * port of it is fed a disconnect, so each device below reports its PDOs
 * missing and gives its slot back - a hub below by this same function,
 * first - before the caller disables the hub's own slot, leaf first. The
 * hub's node leaves the graph with its subtree's, and the hub object stays,
 * Draining, until every port of it has settled; p waits for it.
 *
 * 27-A.3's removal adds to this what a hub pulled mid-transfer needs (the
 * subtree's endpoints stopped before the slots go) and the orderly path; a
 * hub whose children's commands fail here leaves them to the recovery the
 * failure requested, which drops what is left (hcdDropAll).
 */
static VOID hcdHubLeave(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PHCD_HUB hub;
    PHCD_PORT q;
    ULONG n;

    hub = p->Device->Hub;
    XHCI_DBG_VALUE("hcd: hub leaving, location/slot",
                   (p->PortId << 8) | hub->SlotId);
    hub->Draining = 1;
    for (n = 1; n <= hub->Ports; n++) {
        q = HcdHubPort(hc, hub, n);
        if (q->Enum.State != XHCI_ENUM_EMPTY &&
            q->Enum.State != XHCI_ENUM_GONE) {
            hcdFeed(hc, q, XHCI_ENUM_EV_DISCONNECT);
        }
    }
    HcdHubForget(hc, hub);
    if (hcdHubQuiet(hc, hub)) {
        HcdHubFree(hc, hub);
    } else {
        p->AwaitHub = hub;
    }
}

/* The same with no command: HCRST took every slot, or the controller
 * stopped. Every port of the hub is dropped as a root port is
 * (hcdDropPort), the hub's own record left to the caller. */
static VOID hcdDropPort(PHCD_CONTROLLER hc, PHCD_PORT p);

static VOID hcdHubDrop(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PHCD_HUB hub;
    ULONG n;

    hub = p->Device->Hub;
    hub->Draining = 1;
    for (n = 1; n <= HCD_HUB_MAX_PORTS; n++) {
        hcdDropPort(hc, HcdHubPort(hc, hub, n));
    }
    HcdHubForget(hc, hub);
    if (hcdHubQuiet(hc, hub)) {
        HcdHubFree(hc, hub);
    } else {
        p->AwaitHub = hub;
    }
}

/* ----------------------------------------------------------------------- */
/* Dropping and detaching                                                   */
/* ----------------------------------------------------------------------- */

/* A port whose slot HCRST took: its record goes without a command - a hub's
 * subtree first - its PDO off the bus, and its machine where a disconnect
 * would put it, waiting in Gone, by serial, for a PDO PnP still holds. */
static VOID hcdDropPort(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    XHCI_ENUM_EVENT event;
    XHCI_ENUM_ACTION act;

    if (p->Device != NULL) {
        if (p->Device->Hub != NULL) {
            hcdHubDrop(hc, p);
        }
        p->AwaitSerial = HcdDevicePdoGone(hc, p->Device);
        hcdDeviceFree(hc, p->Device);
        p->Device = NULL;
    }
    hcdEventInit(&event, XHCI_ENUM_EV_DISCONNECT, 1);
    (VOID)XhciEnumStep(&p->Enum, &event, &act);
    (VOID)hcdSettleGone(hc, p);
}

/* Every device's URBs drained before any one device is waited out
 * (HcdIoDeviceDrain; round 5, finding 3). The slot is gone for each. */
static VOID hcdDrainAll(PHCD_CONTROLLER hc)
{
    ULONG i;

    for (i = 0; i < HCD_PORT_COUNT; i++) {
        if (hc->Ports[i].Device != NULL) {
            (VOID)HcdIoDeviceDrain(hc, hc->Ports[i].Device);
        }
    }
    for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
        if (hc->SlotDevice[i] != NULL) {
            (VOID)HcdIoDeviceDrain(hc, hc->SlotDevice[i]);
        }
    }
}

/*
 * HCRST took every slot (an invalidation), or the controller stops: drop
 * every record without a command - the quarantined ones in SlotDevice[]
 * with the ports' - take each port's PDO off the bus, and put each machine
 * where a disconnect would, with the slot already gone. The root ports take
 * their hubs' subtrees with them; a port left holding a device in a hub
 * already departing (a teardown a halt cut short) is dropped after them. A
 * port whose PDO PnP still holds waits in Gone for its removal; the rescan
 * that follows finds what is connected now (Codex review of batch (b),
 * round 1, finding 6).
 */
static VOID hcdDropAll(PHCD_CONTROLLER hc)
{
    PHCD_HUB hub;
    ULONG i;
    ULONG n;

    hcdDrainAll(hc);
    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        hcdDropPort(hc, &hc->Ports[i]);
    }
    for (i = 0; i < HCD_MAX_HUBS; i++) {
        hub = &hc->Hubs[i];
        if (!hub->Used) {
            continue;
        }
        for (n = 1; n <= HCD_HUB_MAX_PORTS; n++) {
            hcdDropPort(hc, HcdHubPort(hc, hub, n));
        }
    }
    for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
        hcdDeviceFree(hc, hc->SlotDevice[i]);
    }
#if DBG
    HcdStrictForgetSlots(hc);
#endif
    hc->SlotSweep = 0;
}

static VOID hcdInvalidate(PHCD_CONTROLLER hc)
{
    XHCI_DBG_TEXT("hcd: slots invalidated, dropping every device");
    hcdDropAll(hc);
    hc->ScratchTainted = 0;
}

/* One port as the root hub's removal leaves it: its device - a hub's
 * subtree first - off the bus, its slot disabled when powered and otherwise
 * left Abandoned for the first powered pass, its machine in Empty. */
static VOID hcdDetachPort(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG powered);

static VOID hcdHubDetach(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG powered)
{
    PHCD_HUB hub;
    ULONG n;

    hub = p->Device->Hub;
    hub->Draining = 1;
    for (n = 1; n <= HCD_HUB_MAX_PORTS; n++) {
        hcdDetachPort(hc, HcdHubPort(hc, hub, n), powered);
    }
    HcdHubForget(hc, hub);
    HcdHubFree(hc, hub);
}

static VOID hcdDetachPort(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG powered)
{
    if (p->Device != NULL) {
        if (p->Device->Hub != NULL) {
            hcdHubDetach(hc, p, powered);
        }
        (VOID)HcdDevicePdoGone(hc, p->Device);
        if (powered && !hcdHalted(hc)) {
            hcdDisableSlot(hc, p);
        } else {
            p->Device->Abandoned = 1;
            p->Device = NULL;
            hc->SlotSweep = 1;
        }
    }
    p->AwaitSerial = 0;
    p->AwaitHub = NULL;
    XhciEnumReset(&p->Enum);
}

/*
 * The root hub is going (HcdEnumDetach): every device leaves the bus and
 * every machine starts again from Empty, so a root hub started again
 * enumerates afresh (Codex review of batch (b), round 1, finding 1) - the
 * hubs' subtrees with the root ports', every hub object freed, a departing
 * one's leftovers too. With the controller unpowered no command can run: the
 * records are left Abandoned, and the first powered pass disables their
 * slots (hcdSweepAbandoned; round 2, finding 3) unless an invalidation has
 * dropped them first.
 */
static VOID hcdDetach(PHCD_CONTROLLER hc, ULONG powered)
{
    PHCD_HUB hub;
    KIRQL oldIrql;
    ULONG i;
    ULONG n;

    XHCI_DBG_TEXT("hcd: root hub detaching, dropping every device");
    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        hcdDetachPort(hc, &hc->Ports[i], powered);
    }
    for (i = 0; i < HCD_MAX_HUBS; i++) {
        hub = &hc->Hubs[i];
        if (!hub->Used) {
            continue;
        }
        for (n = 1; n <= HCD_HUB_MAX_PORTS; n++) {
            hcdDetachPort(hc, HcdHubPort(hc, hub, n), powered);
        }
        HcdHubForget(hc, hub);
        HcdHubFree(hc, hub);
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (i = 0; i < HCD_PORT_WORDS; i++) {
        hc->PortPdoStarted[i] = 0;
        hc->PortPdoRemoved[i] = 0;
        hc->PortCycle[i] = 0;
    }
    hc->EnumDetachRequested = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    (VOID)KeSetEvent(&hc->EnumDetachDone, IO_NO_INCREMENT, FALSE);
}

/* ----------------------------------------------------------------------- */
/* The hubs' ports                                                          */
/* ----------------------------------------------------------------------- */

/* One hub port to look at: its GET_STATUS decided (hcd_hub.c) and the
 * decision fed to its machine, as hcdPortChanged feeds a root port's. */
static VOID hcdHubPortChanged(PHCD_CONTROLLER hc, PHCD_HUB hub, ULONG n)
{
    XHCI_HUB_PORT_DECISION d;
    PHCD_PORT q;

    q = HcdHubPort(hc, hub, n);
    if (!HcdHubPortLook(hc, hub, n, q->Enum.State, &d)) {
        return;
    }
    if (d.Disconnect) {
        hcdFeed(hc, q, XHCI_ENUM_EV_DISCONNECT);
    }
    if (d.Connect && !hcdHalted(hc)) {
        hcdFeed(hc, q, XHCI_ENUM_EV_CONNECT);
    }
}

/* Every live hub, in object order: its news collected, each port it names
 * looked at, and its status-change pipe re-armed only after them (section
 * 10.1). A hub brought up during the pass is served in it if its object
 * comes later, at the next pass otherwise. */
static VOID hcdHubService(PHCD_CONTROLLER hc)
{
    PHCD_HUB hub;
    ULONG i;
    ULONG n;

    for (i = 0; i < HCD_MAX_HUBS && !hcdHalted(hc); i++) {
        hub = &hc->Hubs[i];
        if (!hub->Used || hub->Draining || hub->Device == NULL ||
            hub->Refused) {
            continue;
        }
        HcdHubCollect(hc, hub);
        for (n = 1; n <= hub->Ports && !hcdHalted(hc); n++) {
            if (hub->Draining || hub->Device == NULL) {
                break;
            }
            if ((hub->Changed & (1UL << n)) == 0) {
                continue;
            }
            hub->Changed &= ~(1UL << n);
            hcdHubPortChanged(hc, hub, n);
        }
        if (!hub->Draining && hub->Device != NULL && !hcdHalted(hc)) {
            HcdHubRearm(hc, hub);
        }
    }
}

/*
 * The thread's port service, under the power gate; `powered` is the
 * controller's state read under it (Codex review of batch (b), round 1,
 * finding 5). In order: an invalidation HCRST left, a detach the root hub
 * asked for, the abandoned slots' sweep, the PDO handshake (design record 13
 * section 5.3: PDO_REMOVED takes a Gone port to Empty and a connected one on
 * to a new enumeration, PDO_STARTED takes Present to Bound) at every port
 * location, then every root port marked changed, then every live hub's
 * ports. Unpowered, only a detach runs; the rest stays marked. Only the
 * controller thread calls it.
 */
VOID HcdEnumService(PHCD_CONTROLLER hc, ULONG powered)
{
    PXHCI_EXTENSION ext;
    ULONG changed[HCD_PORT_WORDS];
    ULONG started[HCD_PORT_WORDS];
    ULONG removed[HCD_PORT_WORDS];
    ULONG cycle[HCD_PORT_WORDS];
    KIRQL oldIrql;
    ULONG serial;
    ULONG invalidated;
    ULONG detach;
    ULONG anyRemoved;
    ULONG word;
    ULONG bit;
    ULONG port;
    ULONG i;
    PHCD_PORT p;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &oldIrql);
    detach = hc->EnumDetachRequested;
    invalidated = 0;
    if (powered) {
        invalidated = hc->SlotsInvalidated;
        hc->SlotsInvalidated = 0;
    }
    XhciControllerLockRelease(ext, oldIrql);

    /* An invalidation first: what HCRST took needs no Disable Slot, which a
     * detach after it would otherwise send and, refused, turn into another
     * reset (round 3, finding 2). */
    if (invalidated) {
        hcdInvalidate(hc);
    }
    if (detach) {
        hcdDetach(hc, powered);
    }
    if (!powered) {
        return;
    }
    if (hc->SlotSweep && !hcdHalted(hc)) {
        hcdSweepAbandoned(hc);
    }
    for (i = 1; i <= XHCI_MAX_SLOTS && !hcdHalted(hc); i++) {
        if (hc->SlotDevice[i] != NULL && hc->SlotDevice[i]->Ep0Halted) {
            hcdResetEp0(hc, hc->SlotDevice[i]);
        }
    }
    if (!hcdHalted(hc)) {
        HcdCfgCancelService(hc);
        /* The URBs that need commands (hcd_cfg.c). */
        HcdCfgService(hc);
    }

    XhciControllerLockAcquire(ext, &oldIrql);
    for (i = 0; i < HCD_PORT_WORDS; i++) {
        changed[i] = hc->PortChange[i];
        started[i] = hc->PortPdoStarted[i];
        removed[i] = hc->PortPdoRemoved[i];
        cycle[i] = hc->PortCycle[i];
        hc->PortChange[i] = 0;
        hc->PortPdoStarted[i] = 0;
        hc->PortPdoRemoved[i] = 0;
        hc->PortCycle[i] = 0;
        if (invalidated) {
            changed[i] = 0xFFFFFFFFUL;
        }
    }
    XhciControllerLockRelease(ext, oldIrql);

    anyRemoved = 0;
    for (port = 1; port <= HCD_PORT_COUNT; port++) {
        word = (port - 1) / 32UL;
        bit = 1UL << ((port - 1) % 32UL);
        p = &hc->Ports[port - 1];
        if ((removed[word] & bit) != 0) {
            anyRemoved = 1;
            if (p->Enum.State == XHCI_ENUM_GONE) {
                /* The bit says when to look; the serial says whether the
                 * PDO this port waits for is the one that went. */
                (VOID)hcdSettleGone(hc, p);
                hcdMarkChanged(p, changed);
            }
        }
        if ((started[word] & bit) != 0) {
            hcdFeed(hc, p, XHCI_ENUM_EV_PDO_STARTED);
        }
        if ((cycle[word] & bit) != 0) {
            XhciControllerLockAcquire(ext, &oldIrql);
            serial = hc->PortCycleSerial[port - 1];
            XhciControllerLockRelease(ext, oldIrql);
            if (hcdCycleOwns(p, serial) && !hcdHalted(hc)) {
                /* An unplug as the machine sees one: the PDO goes and the
                 * slot with it, the port waits in Gone for the PDO's
                 * deletion (hcdSettleGone), and an Empty port that reads
                 * connected is then a new device (hcdPortChanged). */
                XHCI_DBG_VALUE("hcd: cycling port, location", port);
                hcdFeed(hc, p, XHCI_ENUM_EV_DISCONNECT);
                hcdMarkChanged(p, changed);
            }
        }
    }
    if (anyRemoved) {
        hcdSettleWaiting(hc, changed);
    }

    if (!hc->RootHubStarted || hc->ScratchVa == NULL || hcdHalted(hc)) {
        /* Nothing is enumerated without a started root hub to report to;
         * its start marks every port again. A halted controller's recovery
         * does the same through the invalidation. */
        return;
    }
    for (port = 1; port <= ext->PortMap.PortCount &&
                   port <= XHCI_MAX_ROOT_PORTS; port++) {
        if ((changed[(port - 1) / 32UL] & (1UL << ((port - 1) % 32UL))) ==
                0 ||
            !XhciPortIsManaged(&ext->PortMap, port)) {
            continue;
        }
        hcdPortChanged(hc, &hc->Ports[port - 1]);
        if (hcdHalted(hc)) {
            return;
        }
    }
    hcdHubService(hc);
}

/*
 * The root hub's removal (hcd_rh.c): no PDO is created from here on, and the
 * thread drops every device and resets every machine before this returns, so
 * the caller can release the PDOs with nothing listing more. Without a
 * running thread the stop has dropped them already (HcdEnumDrop).
 * IRQL: PASSIVE_LEVEL.
 */
VOID HcdEnumDetach(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG wait;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->RootHubStarted = 0;
    wait = hc->ThreadRunning;
    if (wait) {
        hc->EnumDetachRequested = 1;
        KeClearEvent(&hc->EnumDetachDone);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (!wait) {
        return;
    }
    HcdThreadWake(hc);
    (VOID)KeWaitForSingleObject(&hc->EnumDetachDone, Executive, KernelMode,
                                FALSE, NULL);
}

/*
 * The root hub's start (hcd_rh.c): PDOs may be created, and every port is
 * looked at. IRQL: PASSIVE_LEVEL.
 */
VOID HcdEnumAttach(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG i;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->RootHubStarted = 1;
    for (i = 0; i < HCD_PORT_WORDS; i++) {
        hc->PortChange[i] = 0xFFFFFFFFUL;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdThreadWake(hc);
}

/* At a start: every port empty, every port numbered, the topology graph
 * empty, no request pending - except a port still waiting in Gone for a PDO
 * PnP holds across the controller's stop, or for a departed hub's subtree,
 * which keeps waiting (round 2, finding 2); the hub objects it waits on stay
 * with it. RootHubStarted is kept: a root hub started before a controller
 * restart is still started. IRQL: PASSIVE_LEVEL, the thread not yet
 * running. */
VOID HcdEnumInit(PHCD_CONTROLLER hc)
{
    PHCD_PORT p;
    ULONG i;

    XhciTopoReset(&hc->Hc.Topology);
    for (i = 0; i < HCD_MAX_HUBS; i++) {
        hc->Hubs[i].Index = i;
    }
    for (i = XHCI_MAX_ROOT_PORTS; i < HCD_PORT_COUNT; i++) {
        hc->Ports[i].PortId = i + 1;
    }
    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        p = &hc->Ports[i];
        p->PortId = i + 1;
        p->Number = i + 1;
        p->Hub = NULL;
        p->Device = NULL;
        p->LinkPsiv = 0;
        XhciLinkInit(&p->Link);
        if (p->Enum.State == XHCI_ENUM_GONE && !hcdPortQuiet(hc, p)) {
            continue;
        }
        XhciEnumReset(&p->Enum);
        p->AwaitSerial = 0;
        p->AwaitHub = NULL;
    }
    for (i = 0; i <= XHCI_MAX_SLOTS; i++) {
        hc->SlotDevice[i] = NULL;
    }
    for (i = 0; i < HCD_PORT_WORDS; i++) {
        hc->PortChange[i] = 0xFFFFFFFFUL;
        hc->PortPdoStarted[i] = 0;
        hc->PortPdoRemoved[i] = 0;
        hc->PortCycle[i] = 0;
    }
    hc->SlotsInvalidated = 0;
    hc->EnumDetachRequested = 0;
    hc->ScratchTainted = 0;
    (VOID)KeSetEvent(&hc->EnumDetachDone, IO_NO_INCREMENT, FALSE);
}

/* At a stop: the HCRST that follows takes every slot, so the records go
 * without commands, the quarantined and abandoned ones with the rest, and
 * each port is dropped as an invalidation drops it - a port whose PDO PnP
 * still holds waits for it in Gone (round 2, finding 2). IRQL:
 * PASSIVE_LEVEL, the thread stopped. */
VOID HcdEnumDrop(PHCD_CONTROLLER hc)
{
    hcdDropAll(hc);
}
