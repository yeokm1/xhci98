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
 * 512 with its BOS descriptor read. A SuperSpeed hub is brought up as any
 * hub is (30-A.1): hcd_hub.c takes it, and its USB3 half's class requests
 * are hcd_sshub.c's.
 *
 * Timings, section 10.2: attach debounce 100 ms (TATTDB), the reset timed by
 * the xHC and waited for up to 500 ms, reset recovery 10 ms (TRSTRCY),
 * SetAddress recovery 2 ms (TDSETADDR).
 *
 * Descriptors are read into the first 4 KB of a common buffer of the
 * controller's own (hcd_dma.c), the one DMA-visible scratch the enumeration
 * needs; the hubs' status-change reports sit after it
 * (HCD_SCRATCH_CONTROL_BYTES). A configuration descriptor longer than that
 * is refused as a configuration failure.
 *
 * The ports of the hubs inside the bus (27-A.1, hcd_hub.c) run the same
 * machine: their debounce and reset are the hub's class requests, their
 * devices are placed by the topology graph (HcdHubPlace) and addressed with
 * that place in the Slot Context, and a hub that reaches Present is brought
 * up by the bus and gets no PDO. A device that leaves goes by one teardown
 * whatever the cause (hcdSubtreeGo; section 10.5): a hub takes its subtree
 * with it, every device below frozen and its PDOs reported missing, its
 * running endpoints stopped and its URBs taken off the rings and held on its
 * PDO while its slot is still enabled, then its slot disabled before the
 * hub's own (leaf first), and the port the hub sat on waits until PnP has
 * deleted every PDO of the subtree (hcdPortQuiet).
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
/* Passes a root port's PORTSC may read all ones - at its inspection, or at
 * a pending hold's reads - before the controller is handed to recovery:
 * about five seconds at the thread's 100 ms tick; a bus policy number. */
#define HCD_PORT_UNREADABLE_PASSES 50UL
#define HCD_HOLD_UNREADABLE_PASSES HCD_PORT_UNREADABLE_PASSES
#define HCD_RESET_WAIT_MS      500UL
/* A warm reset is LFPS for tReset, 80 to 120 ms (USB 3.2 Table 6-30, USB
 * 3.2 p.100; verified), then Rx.Detect and link training (7.4.2, p.158); a
 * hub gives one up after 100 to 200 ms in Rx.Detect (tTimeForResetError,
 * Table 10-19, p.460). Twice the hot reset's wait covers both, and is this
 * driver's margin, not a specification number. */
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

/* How a device leaves (hcdSubtreeGo). */
#define HCD_GO_UNPLUG   0UL     /* slots live, powered: endpoints stopped,
                                 * URBs completed, then Disable Slot      */
#define HCD_GO_TAKEN    1UL     /* HCRST took every slot: no command      */
#define HCD_GO_DETACH   2UL     /* the root hub leaves: every machine back
                                 * to Empty, the slot disabled when
                                 * powered and left Abandoned otherwise   */

static ULONG hcdSubtreeGo(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG how,
                          ULONG powered);

/* An endpoint's state as the controller keeps it in the output Device
 * Context (xHCI 6.2.3, EP State); Disabled when the slot has no context. */
static ULONG hcdEpState(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG dci)
{
    ULONG offset;

    if (XhciEndpointContextOffset(&hc->Hc.Layout, dev->SlotId, dci,
                                  &offset) != XHCI_LAYOUT_OK) {
        return XHCI_EP_STATE_DISABLED;
    }
    return XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]);
}

static ULONG hcdEp0State(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    return hcdEpState(hc, dev, 1);
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
    if (state == XHCI_EP_STATE_HALTED) {
        /* Before the doorbell: behind a TT, the halted control transfer
         * may still sit in the TT's buffer (xHCI 4.6.8 p.116; 27-A.3). */
        HcdHubClearTt(hc, dev, 0, XHCI_HUB_TT_EP_CONTROL, 0);
    }
    /* Not for a device being torn down: its freeze set Gone under this
     * lock (Codex review of d54eef0, finding 1). */
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (waiting && !dev->Gone) {
        XhciWriteDoorbell(&hc->Hc, dev->SlotId, 1);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdIoPipeResume(hc, &dev->Ep0Pipe);
    XHCI_DBG_VALUE("hcd: EP0 recovered, slot/state",
                   (dev->SlotId << 8) | state);
    return 1;
}

/*
 * The records an unpowered root-hub detach left Abandoned - off their
 * ports, their slots still enabled - get their Disable Slot at the first
 * powered pass, so a restore that kept the slots does not strand them
 * (Codex review of batch (b), round 2, finding 3). Deepest tier first, as
 * every other teardown goes (hcdSubtreeGo), never by slot ID: a hub may
 * hold a higher or lower slot than the devices behind it, and a device's
 * Parent Hub Slot ID - a TT's, or since 29-0 a higher-rank SuperSpeed
 * hub's (xHCI Table 6-6) - must not name a slot already disabled. A
 * failure quarantines the record as a departing device's does, and ends
 * the sweep: the reset that failure requested invalidates every slot left,
 * so no hub is disabled under a child the controller still holds (Codex
 * review of b6e569e, finding 2). Thread only, powered.
 */
static VOID hcdSweepAbandoned(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;
    ULONG tier;
    ULONG at;
    ULONG i;

    hc->SlotSweep = 0;
    tier = XHCI_TOPO_MAX_TIER + 1UL;
    while (tier-- != 0) {
        for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
            if (hcdHalted(hc)) {
                return;
            }
            dev = hc->SlotDevice[i];
            if (dev == NULL || !dev->Abandoned) {
                continue;
            }
            at = dev->Tier < XHCI_TOPO_MAX_TIER ? dev->Tier
                                                : XHCI_TOPO_MAX_TIER;
            if (at != tier) {
                continue;
            }
            hcdDisableRecord(hc, dev);
            if (hc->SlotDevice[i] == dev) {
                /* Not confirmed: quarantined, and the reset it requested
                 * owns every slot left. */
                return;
            }
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
    /* A root port device's own link rank, from that rate and the port's
     * PORTLI lane count, read now while CCS = 1 as RLC needs (Table 5-31,
     * p.385). What a SuperSpeed hub on this port is ranked by when a device
     * is placed behind it (HcdHubPlace, xHCI Table 6-6). A device behind a
     * hub was ranked when it was placed, and is not touched here. */
    if (dev->Tier == 0) {
        dev->SsLinkRank = XHCI_SS_RANK_UNKNOWN;
        if (portClass == XHCI_SPEED_SUPER &&
            XhciPortIsUsb3(&ext->PortMap, dev->Port)) {
            dev->SsLinkRank = XhciSsRootRank(
                dev->RateKbps, XhciReadOp(ext, XHCI_OP_PORTLI(dev->Port)));
            XHCI_DBG_VALUE("hcd: root port SS link rank, port/rank",
                           (dev->Port << 8) | dev->SsLinkRank);
        }
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
    ULONG code;
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
    code = hcdCommand(hc, &trb, &control);
    if (code != XHCI_CC_SUCCESS) {
        if (code == XHCI_CC_USB_TRANSACTION_ERROR) {
            /* Behind a TT, its buffer is cleared before the machine's
             * next attempt (xHCI p.102; section 10.2 step 7): the device
             * still answers at address 0, on its default control
             * endpoint. */
            HcdHubClearTt(hc, dev, 0, XHCI_HUB_TT_EP_CONTROL, 1);
        }
        return 0;
    }
    HcdSsHubAdoptSpeed(hc, p, dev);
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
                                   PULONG bytes);

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
    ULONG outcome;

    outcome = HcdThreadControlOutcome(hc, dev, requestType, request, value,
                                      index, length, bytes);
    *stalled = outcome == HCD_CTL_STALLED;
    return outcome == HCD_CTL_DONE;
}

/* The same, saying what became of it (HCD_CTL_*): a request the device
 * refused - STALLED, or FAILED with another completion - apart from one
 * that never reached it (NOT_SENT: EP0 still busy with a client's transfer,
 * the device going, or a timeout), which says nothing about the device
 * (Codex review of c4ec1c3, finding 1). Thread only. */
ULONG HcdThreadControlOutcome(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              UCHAR requestType, UCHAR request,
                              USHORT value, USHORT index, ULONG length,
                              PULONG bytes)
{
    ULONG outcome;

    *bytes = 0;
    if (dev->Ep0Stuck) {
        /* Its record is still queued from a timeout: reusing it would
         * relink the engine's queue (round 2, finding 10). */
        return HCD_CTL_NOT_SENT;
    }
    if (length > HCD_SCRATCH_CONTROL_BYTES ||
        (length != 0 && (requestType & 0x80) == 0)) {
        return HCD_CTL_NOT_SENT;
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
        return HCD_CTL_NOT_SENT;
    }
    outcome = hcdThreadControlQuiet(hc, dev, requestType, request, value,
                                    index, length, bytes);
    if (!dev->Ep0Stuck) {
        /* A timed-out one leaves EP0 paused for the reset it requested. */
        HcdIoPipeResume(hc, &dev->Ep0Pipe);
    }
    return outcome;
}

/* The transfer itself, on a quiet EP0; HCD_CTL_*. Thread only. */
static ULONG hcdThreadControlQuiet(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                   UCHAR requestType, UCHAR request,
                                   USHORT value, USHORT index, ULONG length,
                                   PULONG bytes)
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

    /* Published and rung under the lock, and never for a device being
     * torn down (Gone; Codex review of d54eef0, finding 1). */
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->Ep0Done = 0;
    KeClearEvent(&hc->XferDoneEvent);
    answer = XHCI_XFER_BUSY;
    if (!dev->Gone) {
        answer = XhciXferSubmitControl(&dev->Ep0Queue, &dev->Ep0, &req,
                                       &dev->Ep0Xfer, dev, trbs,
                                       XHCI_XFER_MAX_CONTROL_TRBS);
        if (answer == XHCI_XFER_OK) {
            XhciWriteDoorbell(&hc->Hc, dev->SlotId, 1);
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (answer != XHCI_XFER_OK) {
        return HCD_CTL_NOT_SENT;
    }

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
        return HCD_CTL_NOT_SENT;
    }
    if (dev->Ep0Xfer.UsbdStatus != XHCI_USBD_STATUS_SUCCESS) {
        return dev->Ep0Xfer.UsbdStatus == XHCI_USBD_STATUS_STALL_PID
                   ? HCD_CTL_STALLED
                   : HCD_CTL_FAILED;
    }
    *bytes = dev->Ep0Xfer.BytesTransferred;
    return HCD_CTL_DONE;
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
    ULONG ok;
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
        ok = XhciLinkResetDone(portsc, &warm);
        /* A hot reset asked for that the xHC carried out warm - after a
         * failed hot-reset handshake (4.19.5.1) - shows as WRC; counted
         * from what was observed, not from what was written (Codex review
         * of Phase 29, round 1, finding 5), and whether or not the link
         * then trained: WRC has just been acknowledged, so this is the one
         * chance to see it (round 2, finding 3). */
        if (warm && p->Link.LastReset == XHCI_LINK_ACT_HOT_RESET) {
            hc->Counters.SsResetsConverted++;
            XHCI_DBG_VALUE("hcd: hot reset converted to warm, port/ok",
                           (port << 8) | ok);
        }
        if (!ok) {
            XHCI_DBG_VALUE("hcd: SuperSpeed reset failed, PORTSC", portsc);
            return 0;
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
 * A SuperSpeed device's link rank as the port's latest reset left it: on a
 * root port from PORTSC's speed, its PSI rate and PORTLI (XhciSsRootRank),
 * behind a SuperSpeed hub from the extended status that reset re-read
 * (XhciSsHubChildRank); XHCI_SS_RANK_UNKNOWN for a USB 2.0 device or path.
 * Thread only, powered.
 */
static ULONG hcdLinkRankNow(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PXHCI_EXTENSION ext;
    PHCD_PORT p;
    ULONG psiv;
    ULONG cls;
    ULONG kbps;
    ULONG plus;

    ext = &hc->Hc;
    if (dev->Location == 0 || dev->Location > HCD_PORT_COUNT) {
        return XHCI_SS_RANK_UNKNOWN;
    }
    p = &hc->Ports[dev->Location - 1];
    if (p->Hub != NULL) {
        if (!p->Hub->Usb3 || p->Hub->Device == NULL) {
            return XHCI_SS_RANK_UNKNOWN;
        }
        return XhciSsHubChildRank(p->Hub->Device->BosInfo.HasSuperSpeedPlus,
                                  &p->HubSsLink);
    }
    if (!XhciPortIsUsb3(&ext->PortMap, dev->Port)) {
        return XHCI_SS_RANK_UNKNOWN;
    }
    psiv = XHCI_PORTSC_GET_SPEED(XhciReadPortsc(ext, dev->Port));
    cls = XHCI_SPEED_UNKNOWN;
    kbps = 0;
    plus = 0;
    if (XhciPortSpeedClass(&ext->PortMap, dev->Port, psiv, &cls) !=
            XHCI_CAPS_OK ||
        cls != XHCI_SPEED_SUPER ||
        XhciPortRate(&ext->PortMap, dev->Port, psiv, &kbps, &plus) !=
            XHCI_CAPS_OK) {
        return XHCI_SS_RANK_UNKNOWN;
    }
    return XhciSsRootRank(kbps, XhciReadOp(ext, XHCI_OP_PORTLI(dev->Port)));
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
    /* A SuperSpeed link can retrain to another rank on this reset - a warm
     * one above all - and the slot's Speed, Parent Hub Slot ID and Parent
     * Port Number (xHCI Table 6-6; HcdHubPlace) were placed for the old
     * one. Readdressing would carry them over stale, so a changed rank
     * fails the RESET_PORT, which drops the device to be enumerated afresh
     * at the link it now has (Codex review of b6e569e, finding 3). */
    if (was == XHCI_SPEED_SUPER) {
        speed = hcdLinkRankNow(hc, dev);
        if (speed != dev->SsLinkRank) {
            XHCI_DBG_VALUE("hcd: reset port, SS link rank changed, was/now",
                           (dev->SsLinkRank << 8) | speed);
            return 0;
        }
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

/* ----------------------------------------------------------------------- */
/* 29-A.5's hold: lookups and the identity read                             */
/* ----------------------------------------------------------------------- */

/* The active hold on a SuperSpeed root port, or NULL. A Pending one is not
 * active yet: its port is served until the thread acts on it. Thread. */
static PHCD_HOLD hcdHoldOf(PHCD_CONTROLLER hc, ULONG port)
{
    ULONG i;

    for (i = 0; i < HCD_MAX_HOLDS; i++) {
        if (hc->Holds[i].Used && !hc->Holds[i].Pending &&
            hc->Holds[i].Port == port) {
            return &hc->Holds[i];
        }
    }
    return NULL;
}

/* The active hold whose USB 2.0 companion is `port`, or NULL. Thread. */
static PHCD_HOLD hcdHoldCompanionOf(PHCD_CONTROLLER hc, ULONG port)
{
    ULONG i;

    for (i = 0; i < HCD_MAX_HOLDS; i++) {
        if (hc->Holds[i].Used && !hc->Holds[i].Pending &&
            hc->Holds[i].Hold.Kind != XHCI_HOLD_NONE &&
            hc->Holds[i].Hold.Companion == port) {
            return &hc->Holds[i];
        }
    }
    return NULL;
}

/*
 * A device's identity for 29-A.5 (xhci_link.h): vendor and product id from
 * the device descriptor the enumeration kept, and the serial string, read
 * with the first language id string descriptor 0 lists (0409h when that
 * read fails). No serial index is a valid identity with no serial - an
 * unidentified hold; a failed serial read is an invalid one - likewise. The
 * reads are the thread's control transfers on the device's EP0 into the
 * scratch, which the caller has done with. Thread only, powered.
 */
static VOID hcdReadIdentity(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            PXHCI_LINK_IDENTITY id)
{
    PUCHAR s;
    ULONG langid;
    ULONG bytes;
    ULONG index;
    ULONG n;
    ULONG i;

    id->Valid = 0;
    id->Vendor = 0;
    id->Product = 0;
    id->SerialLength = 0;
    for (i = 0; i < XHCI_LINK_SERIAL_CHARS; i++) {
        id->Serial[i] = 0;
    }
    if (dev == NULL) {
        return;
    }
    id->Vendor = (ULONG)dev->DeviceDesc[8] | ((ULONG)dev->DeviceDesc[9] << 8);
    id->Product = (ULONG)dev->DeviceDesc[10] |
                  ((ULONG)dev->DeviceDesc[11] << 8);
    index = (ULONG)dev->DeviceDesc[16];
    if (index == 0) {
        id->Valid = 1;
        return;
    }
    s = (PUCHAR)hc->ScratchVa;
    langid = 0x0409UL;
    bytes = 0;
    if (HcdThreadControl(hc, dev, 0x80, 6, (USHORT)0x0300, 0, 4, &bytes) &&
        bytes >= 4 && s[1] == 3 && s[0] >= 4) {
        langid = (ULONG)s[2] | ((ULONG)s[3] << 8);
    }
    bytes = 0;
    if (!HcdThreadControl(hc, dev, 0x80, 6, (USHORT)(0x0300UL | index),
                          (USHORT)langid, 255, &bytes) ||
        bytes < 2 || s[1] != 3 || (ULONG)s[0] > bytes || s[0] < 2) {
        return;
    }
    n = ((ULONG)s[0] - 2UL) / 2UL;
    id->SerialLength = n;
    for (i = 0; i < n && i < XHCI_LINK_SERIAL_CHARS; i++) {
        id->Serial[i] = (USHORT)((ULONG)s[2 + i * 2] |
                                 ((ULONG)s[3 + i * 2] << 8));
    }
    id->Valid = 1;
}

/*
 * The serial number string as an instance id (roadmap-hcd.md task 33.2;
 * design record 13 section 10.7), once per enumeration, before the
 * device's first PDO is built: dev->SerialState and dev->SerialId. No
 * iSerialNumber is NONE, with no request sent. Otherwise each try reads
 * string descriptor 0 for the first language id (0409h when the device
 * STALLs it or lists none) and then the serial string; a serial string
 * that arrives is OK when XhciFuncSerialId takes it and REFUSED when it
 * does not - both the device's own answer, the same at every plug. A try
 * that does not get the string (a STALL, an error, a request not sent) is
 * made again, HCD_SERIAL_READ_TRIES in all, and only when every try failed
 * is the device FAILED - counted and traced, never silent - and named by
 * its location: a device whose read fails at one plug and not at the next
 * changes devnode, and three fails in a row is what it takes. (The other
 * way is a duplicate: a serial id a present PDO already carries leaves
 * the newcomer on the location form, hcd_pdo.c.) A timeout is not retried: it left EP0's record queued
 * (dev->Ep0Stuck) and requested the controller reset, which takes the
 * device; 0 is returned and no PDO is made from it, so no location id is
 * ever given for want of a read the reset will repeat. Every other return
 * is 1. The reads use the scratch, which the caller has done with. Thread
 * only, powered.
 */
#define HCD_SERIAL_READ_TRIES 3UL

ULONG HcdDeviceReadSerial(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PUCHAR s;
    ULONG langid;
    ULONG outcome;
    ULONG bytes;
    ULONG index;
    ULONG tries;

    if (dev->SerialState != HCD_SERIAL_UNREAD) {
        return 1;
    }
    dev->SerialId[0] = 0;
    index = (ULONG)dev->DeviceDesc[16];
    if (index == 0) {
        dev->SerialState = HCD_SERIAL_NONE;
        return 1;
    }
    s = (PUCHAR)hc->ScratchVa;
    for (tries = 0; tries < HCD_SERIAL_READ_TRIES; tries++) {
        langid = 0x0409UL;
        bytes = 0;
        outcome = HcdThreadControlOutcome(hc, dev, 0x80, 6, (USHORT)0x0300,
                                          0, 4, &bytes);
        if (dev->Ep0Stuck) {
            return 0;
        }
        if (outcome == HCD_CTL_DONE) {
            if (bytes >= 4 && s[1] == 3 && s[0] >= 4) {
                langid = (ULONG)s[2] | ((ULONG)s[3] << 8);
            }
        } else if (outcome != HCD_CTL_STALLED) {
            continue;
        }
        bytes = 0;
        outcome = HcdThreadControlOutcome(hc, dev, 0x80, 6,
                                          (USHORT)(0x0300UL | index),
                                          (USHORT)langid, 255, &bytes);
        if (dev->Ep0Stuck) {
            return 0;
        }
        if (outcome != HCD_CTL_DONE) {
            continue;
        }
        if (XhciFuncSerialId(s, bytes, dev->SerialId,
                             sizeof(dev->SerialId)) == XHCI_FUNC_OK) {
            dev->SerialState = HCD_SERIAL_OK;
            hc->SerialIdsTaken++;
        } else {
            dev->SerialId[0] = 0;
            dev->SerialState = HCD_SERIAL_REFUSED;
            hc->SerialIdsRefused++;
            XHCI_DBG_VALUE("hcd: serial string is no instance id, port",
                           dev->Port);
        }
        return 1;
    }
    dev->SerialState = HCD_SERIAL_FAILED;
    hc->SerialReadsFailed++;
    XHCI_DBG_VALUE("hcd: serial string not read, location id, port",
                   dev->Port);
    return 1;
}

/*
 * A PDO's device text (roadmap-hcd.md task 33.6; design record 13 section
 * 10.7) into `out` (XHCI_TEXT_WCHARS): the first of the `count` string
 * indexes (XhciFuncTextIndexes) whose string XhciFuncText takes, in the
 * device's first language id, as the serial read takes it (0409h when
 * string descriptor 0 STALLs, lists none or is not read); `out` empty when
 * none does, which is "USB Device". A name is not worth a slow enumeration:
 * a STALL or a string with nothing to show is final, any other failure is
 * tried HCD_TEXT_READ_TRIES times in all, and an index that gave nothing
 * is not asked again for the device's other PDOs (`state`, zeroed by the
 * caller once per enumeration). A timeout returns 0, as the serial read's
 * does: the reset it requested takes the device. Every other return is 1.
 * The reads use the scratch. Thread only, powered.
 */
#define HCD_TEXT_READ_TRIES 2UL

ULONG HcdDeviceReadText(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                        const ULONG *indexes, ULONG count, ULONG flags,
                        PHCD_TEXT_READ state, WCHAR *out)
{
    PUCHAR s;
    ULONG outcome;
    ULONG answer;
    ULONG bytes;
    ULONG chars;
    ULONG index;
    ULONG tries;
    ULONG k;

    out[0] = 0;
    s = (PUCHAR)hc->ScratchVa;
    for (k = 0; k < count; k++) {
        index = indexes[k];
        if (index == 0 || index > 0xFFUL ||
            (state->Failed[index >> 5] & (1UL << (index & 31UL))) != 0) {
            continue;
        }
        if (!state->LangidRead) {
            state->Langid = 0x0409UL;
            for (tries = 0; tries < HCD_TEXT_READ_TRIES; tries++) {
                bytes = 0;
                outcome = HcdThreadControlOutcome(hc, dev, 0x80, 6,
                                                  (USHORT)0x0300, 0, 4,
                                                  &bytes);
                if (dev->Ep0Stuck) {
                    return 0;
                }
                if (outcome == HCD_CTL_STALLED) {
                    break;
                }
                if (outcome != HCD_CTL_DONE || bytes < 2 || s[1] != 3 ||
                    s[0] < 2) {
                    continue;
                }
                /* An empty list is the device's answer; a first id that
                 * did not arrive is a failed read and tried again. */
                if (s[0] < 4) {
                    break;
                }
                if (bytes >= 4) {
                    state->Langid = (ULONG)s[2] | ((ULONG)s[3] << 8);
                    break;
                }
            }
            state->LangidRead = 1;
        }
        for (tries = 0; tries < HCD_TEXT_READ_TRIES; tries++) {
            bytes = 0;
            outcome = HcdThreadControlOutcome(hc, dev, 0x80, 6,
                                              (USHORT)(0x0300UL | index),
                                              (USHORT)state->Langid, 255,
                                              &bytes);
            if (dev->Ep0Stuck) {
                return 0;
            }
            if (outcome == HCD_CTL_STALLED) {
                break;
            }
            if (outcome != HCD_CTL_DONE) {
                continue;
            }
            /* A descriptor that did not arrive whole is a failed read and
             * tried again; one with nothing to show is the device's own
             * answer. */
            answer = XhciFuncText(s, bytes, flags, out, XHCI_TEXT_WCHARS,
                                  &chars);
            if (answer == XHCI_FUNC_OK) {
                return 1;
            }
            if (answer != XHCI_FUNC_MALFORMED) {
                break;
            }
        }
        state->Failed[index >> 5] |= 1UL << (index & 31UL);
        XHCI_DBG_VALUE("hcd: no device text from string, port/index",
                       (dev->Port << 16) | index);
    }
    return 1;
}

/*
 * A device enumerating on the USB 2.0 companion of a held port, its device
 * descriptor just read: told to the hold, which matches it against the held
 * identity. Only an identified hold that has seen a companion connect asks,
 * and only a device with the held vendor and product id costs a serial read
 * - the one extra request on the USB 2.0 path, made only while such a hold
 * exists. The held device itself is 29-A.5's passive fallback seen from the
 * USB 2.0 side, counted as SsDevicesOnUsb2 (Codex review of Phase 29, round
 * 2, finding 1, which withdrew the BOS probe that counted it before). Thread.
 */
static VOID hcdHoldCompanionSaw(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    XHCI_LINK_IDENTITY id;
    PHCD_HOLD h;
    ULONG others;
    ULONG i;

    if (p->Hub != NULL || p->Device == NULL) {
        return;
    }
    h = hcdHoldCompanionOf(hc, p->PortId);
    if (h == NULL || h->Hold.Kind != XHCI_HOLD_PAIRED ||
        !h->Hold.ConnectSeen || h->Hold.MatchSeen) {
        return;
    }
    id.Valid = 1;
    id.Vendor = (ULONG)p->Device->DeviceDesc[8] |
                ((ULONG)p->Device->DeviceDesc[9] << 8);
    id.Product = (ULONG)p->Device->DeviceDesc[10] |
                 ((ULONG)p->Device->DeviceDesc[11] << 8);
    id.SerialLength = 0;
    for (i = 0; i < XHCI_LINK_SERIAL_CHARS; i++) {
        id.Serial[i] = 0;
    }
    if (id.Vendor == h->Hold.Held.Vendor &&
        id.Product == h->Hold.Held.Product) {
        hcdReadIdentity(hc, p->Device, &id);
    }
    others = h->Hold.OthersSeen;
    XhciHoldCompanionIdentity(&h->Hold, &id);
    if (h->Hold.OthersSeen != others) {
        hc->Counters.HoldCompanionOthers++;
        XHCI_DBG_VALUE("hcd: hold, another device on the companion, port",
                       p->PortId);
    }
    if (h->Hold.MatchSeen) {
        hc->Counters.SsDevicesOnUsb2++;
        XHCI_DBG_VALUE("hcd: hold, the held device on its USB 2.0 path, port",
                       p->PortId);
    }
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
                (VOID)hcdSubtreeGo(hc, p, HCD_GO_UNPLUG, 1);
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
            hcdHoldCompanionSaw(hc, p);
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
        if (p->Device->DeviceDesc[4] == XHCI_HUB_CLASS) {
            /* A hub is the bus's (section 10.3): brought up here, it is
             * Present with nothing PnP must start before the bus serves
             * it, so the machine is told its PDO exists and has started at
             * once. A SuperSpeed hub is one too (30-A.1); its USB 2.0 half
             * is a separate hub on the companion port. Since task 33.4
             * (section 10.11) each half is also presented as a devnode, a
             * PDO bound to this driver as a hub FDO, created now - after
             * the hub's own bring-up and before any port of it is looked
             * at, so every device behind it names it as its parent. A PDO
             * that cannot be created costs only the presentation: the
             * devices behind it are presented under the next hub up. */
            ok = HcdHubStart(hc, p, p->Device);
            hcdEventInit(next, XHCI_ENUM_EV_PDO_CREATED, ok);
            if (!ok) {
                return 1;
            }
            if (!NT_SUCCESS(HcdDevicePdoCreate(hc, p->Device))) {
                XHCI_DBG_VALUE("hcd: hub PDO not created, location",
                               p->PortId);
            }
            /* The port enumerated: a later give-up of it starts its re-arm
             * waits from the first (hcd_hub.c; Codex review of the Phase
             * 28-31 integration, round 3, finding 2). */
            p->HubSsRearms = 0;
            (VOID)XhciEnumStep(&p->Enum, next, &none);
            hcdEventInit(next, XHCI_ENUM_EV_PDO_STARTED, 1);
            XHCI_DBG_VALUE("hcd: hub enumerated at location", p->PortId);
            return 1;
        }
        hcdEventInit(next, XHCI_ENUM_EV_PDO_CREATED,
                     NT_SUCCESS(HcdDevicePdoCreate(hc, p->Device)));
        XHCI_DBG_VALUE("hcd: device enumerated at location", p->PortId);
        if (next->Ok) {
            p->HubSsRearms = 0;
        }
        /* A device 31-A.3 asked 29-A.5 to send back (hcd_pdo.c,
         * HoldAsked) has no PDO yet: the machine waits in Present for the
         * hold service - whose disconnect takes it as an unplug, or whose
         * refusal creates its PDOs and so starts the real handshake
         * (hcdHoldResolve). */
        return 1;

    case XHCI_ENUM_ACT_DISABLE_SLOT:
        (VOID)hcdSubtreeGo(hc, p, HCD_GO_UNPLUG, 1);
        return 0;

    case XHCI_ENUM_ACT_REPORT_GONE:
        /* The machine waits in Gone for that PDO's deletion (AwaitSerial,
         * checked when PortPdoRemoved says to look), and for a departed
         * hub's subtree (AwaitHub), unless both went at once. */
        p->AwaitSerial = hcdSubtreeGo(hc, p, HCD_GO_UNPLUG, 1);
        if (p->AwaitSerial != 0 || p->AwaitHub != NULL) {
            return 0;
        }
        hcdEventInit(next, XHCI_ENUM_EV_PDO_REMOVED, 1);
        return 1;

    default:
        return 0;
    }
}

/* Run the machine from one event until it asks for nothing, retrying from
 * Reset after a failure while the port still reads connected - once on a
 * root port, and on a hub's port until XHCI_HUB_PORT_ATTEMPTS attempts have
 * failed, when the port is disabled at the hub and left so until its next
 * connect change (section 10.2 step 7). A halted controller (hcdHalted)
 * ends the run where it stands; the invalidation that follows the recovery
 * settles the port from any state. */
static VOID hcdRun(PHCD_CONTROLLER hc, PHCD_PORT p, XHCI_ENUM_EVENT event)
{
    XHCI_ENUM_ACTION act;
    ULONG retries;
    ULONG guard;

    retries = (p->Hub != NULL) ? XHCI_HUB_PORT_ATTEMPTS - 1UL
                               : XHCI_ENUM_RETRIES;

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
            (VOID)XhciEnumRetryUpTo(&p->Enum, retries, &act);
            if (act.Kind == XHCI_ENUM_ACT_NONE) {
                XHCI_DBG_VALUE("hcd: enumeration failed, port/cause",
                               (p->PortId << 16) | p->Enum.FailCause);
                if (p->Hub == NULL && p->HoldRecoverFails != 0) {
                    /* Refused in place after a failed recreation and now
                     * failed for good: the same give-up (hcdHoldResolve). */
                    hc->HoldRecoverGiveUps++;
                }
                if (p->Hub != NULL && !p->Hub->Draining &&
                    p->Hub->Device != NULL) {
                    HcdHubPortDisable(hc, p->Hub, p->Number);
                }
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

static VOID hcdHoldResolve(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG portsc);

/* Root port p owed an inspection at the next pass (its PortChange bit),
 * whatever raised it. Thread. */
static VOID hcdPortInspectAgain(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->PortChange[(p->PortId - 1) / 32UL] |= 1UL << ((p->PortId - 1) % 32UL);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/*
 * An inspection of root port p that read PORTSC as all ones, at any of its
 * reads. Unreadable says nothing: whatever the inspection was owed - a
 * change, a refused send-back to settle, a re-inspection asked for - is
 * owed again at the next pass, until an inspection completes on readable
 * reads; a port that stays unreadable hands the controller to recovery,
 * whose invalidation settles every port. An unreadable PORTSC never drops
 * pending port work (Codex review of the Phase 28-31 integration, rounds 5
 * and 6). Thread.
 */
static VOID hcdPortUnreadable(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    if (++p->Unreadable == HCD_PORT_UNREADABLE_PASSES) {
        XHCI_DBG_VALUE("hcd: PORTSC unreadable, recovery, port", p->PortId);
        HcdSvcRequestReset(&hc->Hc);
    }
    hcdPortInspectAgain(hc, p);
}

/*
 * A hold released (29-A.5): the SuperSpeed port re-armed with PLS =
 * RxDetect and LWS - the Disabled state's exit to Disconnected - and never
 * a warm reset, which does not act on a Disabled port, nor a power cycle.
 * Its link trains again if the device comes back to it, and that arrives as
 * an ordinary change. Thread only, powered.
 */
static VOID hcdHoldRelease(PHCD_CONTROLLER hc, PHCD_HOLD h)
{
    XHCI_LINK_ACTION act;
    PHCD_PORT q;
    ULONG portsc;

    q = &hc->Ports[h->Port - 1];
    portsc = XhciReadPortsc(&hc->Hc, h->Port);
    if (portsc != 0xFFFFFFFFUL &&
        XhciLinkDecide(&q->Link, portsc, XHCI_LINK_WANT_RELEASE, &act) ==
            XHCI_LINK_ACT_RX_DETECT) {
        XhciWritePortsc(&hc->Hc, h->Port, XhciPortscRxDetect(portsc));
    }
    hc->Counters.HoldsReleased++;
    XHCI_DBG_VALUE("hcd: hold released, port/PLS",
                   (h->Port << 8) | XHCI_PORTSC_GET_PLS(portsc));
    h->Hold.Kind = XHCI_HOLD_NONE;
    h->Used = 0;
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
    PHCD_HOLD h;
    ULONG portsc;
    ULONG changes;
    ULONG feed;

    ext = &hc->Hc;
    portsc = XhciReadPortsc(ext, p->PortId);
    if (portsc == 0xFFFFFFFFUL) {
        hcdPortUnreadable(hc, p);
        return;
    }
    changes = portsc & XHCI_PORTSC_CHANGE_MASK;
    if (changes != 0) {
        XhciWritePortsc(ext, p->PortId,
                        XhciPortscClearChanges(portsc, changes));
    }
    if ((portsc & XHCI_PORTSC_CCS) == 0 ||
        (changes & XHCI_PORTSC_CSC) != 0) {
        /* Physically empty, or a connection change the hardware reported
         * - a device that may not be the one before: the next device's
         * refused send-back gets its own recreation budget
         * (hcdHoldResolve). The resolver's own disconnect is software
         * only, raises no CSC and keeps the count (Codex review of the
         * Phase 28-31 integration, round 4, finding 2). */
        p->HoldRecoverFails = 0;
    }
    /* A held SuperSpeed port is not served: its link is Disabled and stays
     * so until the hold is released (29-A.5). */
    if (hcdHoldOf(hc, p->PortId) != NULL) {
        p->Unreadable = 0;
        return;
    }
    /* The companion of a held port: its departures and arrivals are the
     * hold's before they are the enumeration's, so the arrival is recorded
     * before the device's descriptor is read. */
    h = hcdHoldCompanionOf(hc, p->PortId);
    if (h != NULL &&
        XhciHoldCompanionPortsc(&h->Hold, portsc) == XHCI_HOLD_RELEASE) {
        hcdHoldRelease(hc, h);
    }
    feed = XhciLinkPortFeed(&p->Link, XhciPortIsUsb3(&ext->PortMap, p->PortId),
                            portsc, p->Enum.State, &act);
    if ((feed & XHCI_LINK_FEED_DISCONNECT) != 0) {
        hcdFeed(hc, p, XHCI_ENUM_EV_DISCONNECT);
    }
    /* The disconnect may have torn a subtree down and had a Stop Endpoint
     * fail, which hands the controller to recovery: no link action on its
     * old slots' port then, as the hub-port path decides (Codex review of
     * the Phase 28-31 integration, finding 4). */
    if (hcdHalted(hc)) {
        return;
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
    /* The resolver reads the port as it is now, through the same rule: an
     * unreadable read owes the whole inspection again, and the budget is
     * charged until an inspection completes (Codex review of the Phase
     * 28-31 integration, round 6, finding 1). */
    portsc = XhciReadPortsc(ext, p->PortId);
    if (portsc == 0xFFFFFFFFUL) {
        hcdPortUnreadable(hc, p);
        return;
    }
    p->Unreadable = 0;
    hcdHoldResolve(hc, p, portsc);
}

/* ----------------------------------------------------------------------- */
/* 29-A.5's send-back                                                       */
/* ----------------------------------------------------------------------- */

BOOLEAN HcdHoldRequestUsb2(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           ULONG reason)
{
    PXHCI_PORT_MAP map;
    KIRQL oldIrql;
    PHCD_HOLD slot;
    ULONG port;
    ULONG taken;
    ULONG i;

    map = &hc->Hc.PortMap;
    port = (dev != NULL) ? dev->Location : 0;
    if (dev == NULL || port == 0 || port > XHCI_MAX_ROOT_PORTS ||
        dev->Port != port || !XhciPortIsUsb3(map, port) ||
        map->Companion[port - 1] == XHCI_PORT_NO_COMPANION) {
        InterlockedIncrement((PLONG)&hc->Counters.HoldRequestsRefused);
        return FALSE;
    }
    slot = NULL;
    taken = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (i = 0; i < HCD_MAX_HOLDS; i++) {
        if (hc->Holds[i].Used && hc->Holds[i].Port == port) {
            taken = 1;
        } else if (!hc->Holds[i].Used && slot == NULL) {
            slot = &hc->Holds[i];
        }
    }
    if (!taken && slot != NULL) {
        slot->Used = 1;
        slot->Pending = 1;
        slot->Reason = reason;
        slot->Port = port;
        slot->Unreadable = 0;
        slot->Hold.Kind = XHCI_HOLD_NONE;
        /* The request is this device's: the service acts only while the
         * port still holds it (hcdHoldService). */
        dev->HoldAsked = 1;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (taken || slot == NULL) {
        InterlockedIncrement((PLONG)&hc->Counters.HoldRequestsRefused);
        return FALSE;
    }
    HcdThreadWake(hc);
    return TRUE;
}

/* Whether a send-back for root port `port` is queued and not yet acted on.
 * Thread. */
static ULONG hcdHoldPendingOn(PHCD_CONTROLLER hc, ULONG port)
{
    KIRQL oldIrql;
    ULONG pending;
    ULONG i;

    pending = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (i = 0; i < HCD_MAX_HOLDS; i++) {
        if (hc->Holds[i].Used && hc->Holds[i].Pending &&
            hc->Holds[i].Port == port) {
            pending = 1;
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return pending;
}

/*
 * The one way out of Present-with-no-PDO on a root port: a device whose
 * PDO creation listed nothing because a send-back was accepted for it
 * (hcd_pdo.c), once no send-back is pending for its port any more - the
 * service refused it, or it was forgotten (Codex review of the Phase 28-31
 * integration, finding 2, and round 2, finding D). Called with a readable
 * PORTSC: by the hold service on a refusal, and by every inspection of the
 * root port (hcdPortChanged), so no other path can leave it stuck. A device
 * still connected is refused in place: HoldAsked cleared, HoldRefused set
 * so its PDO creation does not ask again, and its PDOs created now - the
 * real start handshake (PortPdoStarted) then takes it to Bound. One that
 * reads disconnected is fed the disconnect, the ordinary departure. A PDO
 * creation that fails is the port's failure: a disconnect, and the port
 * marked changed so a device still there is enumerated afresh - within
 * the port's HCD_HOLD_RECOVER_TRIES (HoldRecoverFails). Thread only,
 * powered.
 */
static VOID hcdHoldResolve(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG portsc)
{
    PHCD_USB_DEVICE dev;

    dev = p->Device;
    if (p->Hub != NULL || dev == NULL || dev->Pdo != NULL ||
        dev->Hub != NULL || p->Enum.State != XHCI_ENUM_PRESENT ||
        portsc == 0xFFFFFFFFUL || hcdHalted(hc) ||
        p->HoldRecoverFails >= HCD_HOLD_RECOVER_TRIES ||
        hcdHoldPendingOn(hc, p->PortId)) {
        return;
    }
    dev->HoldAsked = 0;
    dev->HoldRefused = 1;
    if ((portsc & XHCI_PORTSC_CCS) == 0) {
        XHCI_DBG_VALUE("hcd: hold refused, device gone, port", p->PortId);
        hcdFeed(hc, p, XHCI_ENUM_EV_DISCONNECT);
        return;
    }
    XHCI_DBG_VALUE("hcd: hold refused, refused in place, port", p->PortId);
    if (NT_SUCCESS(HcdDevicePdoCreate(hc, dev)) && dev->Pdo != NULL) {
        return;
    }
    /* A failed recreation is counted on the port, across the disconnect
     * below and the re-enumeration it brings, which asks for no send-back
     * while the count is nonzero (hcd_pdo.c); after
     * HCD_HOLD_RECOVER_TRIES the device is left refused, with no PDO and
     * nothing more tried, until the port reads physically disconnected
     * (Codex review of the Phase 28-31 integration, round 3, finding 3). */
    if (++p->HoldRecoverFails >= HCD_HOLD_RECOVER_TRIES) {
        hc->HoldRecoverGiveUps++;
        XHCI_DBG_VALUE("hcd: hold refused, PDOs not created, left, port",
                       p->PortId);
        return;
    }
    hcdFeed(hc, p, XHCI_ENUM_EV_DISCONNECT);
    hcdPortInspectAgain(hc, p);
}

/*
 * The send-backs asked for since the last pass (29-A.5), each in order:
 * the link must still be trained (else the request is refused, counted);
 * the device's identity is read on the SuperSpeed port while it is there
 * to answer; PORTSC is read again and, readable, PED is written, which
 * takes the link to SS.Disabled and withdraws its terminations, so the
 * device looks for its USB 2.0 path; only then does the hold begin,
 * counted by kind; and the port's machine is fed a disconnect, which
 * reports the PDOs missing and disables the slot as an unplug does. From
 * then on the port is not served (hcdPortChanged) until a release. An
 * unreadable PORTSC at either read keeps the request pending, and a
 * controller failure leaves it to the recovery. Thread only, powered.
 */
static VOID hcdHoldService(PHCD_CONTROLLER hc)
{
    XHCI_LINK_IDENTITY id;
    XHCI_LINK_ACTION act;
    PHCD_HOLD h;
    PHCD_PORT p;
    KIRQL oldIrql;
    ULONG pending;
    ULONG portsc;
    ULONG kind;
    ULONG i;

    for (i = 0; i < HCD_MAX_HOLDS && !hcdHalted(hc); i++) {
        h = &hc->Holds[i];
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        pending = h->Used && h->Pending;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (!pending) {
            continue;
        }
        p = &hc->Ports[h->Port - 1];
        portsc = XhciReadPortsc(&hc->Hc, h->Port);
        if (portsc == 0xFFFFFFFFUL && p->Device != NULL &&
            p->Device->HoldAsked) {
            /* Unreadable says nothing about the device: the request stays
             * pending and is looked at again next pass, and a port that
             * stays unreadable hands the controller to recovery, whose
             * invalidation drops the device and forgets the hold (Codex
             * review of the Phase 28-31 integration, round 2, finding D). */
            if (++h->Unreadable >= HCD_HOLD_UNREADABLE_PASSES) {
                XHCI_DBG_VALUE("hcd: hold, PORTSC unreadable, recovery, port",
                               h->Port);
                HcdSvcRequestReset(&hc->Hc);
            }
            continue;
        }
        if (p->Device == NULL || !p->Device->HoldAsked ||
            portsc == 0xFFFFFFFFUL ||
            XhciLinkDecide(&p->Link, portsc, XHCI_LINK_WANT_HOLD, &act) !=
                XHCI_LINK_ACT_DISABLE) {
            /* The device that asked left - its disconnect took it as an
             * unplug - or its link is no longer trained: nothing to send
             * back, and the request ends here (hcdHoldResolve). */
            hc->Counters.HoldRequestsRefused++;
            XHCI_DBG_VALUE("hcd: hold refused, link not trained, port",
                           h->Port);
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            h->Used = 0;
            h->Pending = 0;
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            hcdHoldResolve(hc, p, portsc);
            hcdPortInspectAgain(hc, p);
            continue;
        }
        hcdReadIdentity(hc, p->Device, &id);
        /* The identity transfers may have met a controller failure: the
         * request stays pending and the recovery's invalidation forgets it
         * with the device (Codex review of the Phase 28-31 integration,
         * round 3, finding 1). */
        if (hcdHalted(hc)) {
            break;
        }
        /* They may also have taken a while: the PED write is made on what
         * the port says now, through the same neutral base - and only on a
         * readable PORTSC. An unreadable one leaves the request pending,
         * on the same budget as the first read, and nothing is committed:
         * no active hold, no disconnect (round 3, finding 1). */
        portsc = XhciReadPortsc(&hc->Hc, h->Port);
        if (portsc == 0xFFFFFFFFUL) {
            if (++h->Unreadable >= HCD_HOLD_UNREADABLE_PASSES) {
                XHCI_DBG_VALUE("hcd: hold, PORTSC unreadable, recovery, port",
                               h->Port);
                HcdSvcRequestReset(&hc->Hc);
            }
            continue;
        }
        /* And the device must still be the one the identity was read
         * from, on a link the hold can disable: connected, no connection
         * change since (a CSC may be a replacement), the requester still
         * on the port, and the fresh status still one XhciLinkDecide
         * would disable. Otherwise the request ends, never as an active
         * hold: the port is marked changed and this pass's ordinary
         * inspection (hcdPortChanged) feeds the departure or the new
         * connection and settles the device (hcdHoldResolve) (Codex
         * review of the Phase 28-31 integration, round 4, finding 1). */
        if ((portsc & XHCI_PORTSC_CCS) == 0 ||
            (portsc & XHCI_PORTSC_CSC) != 0 || p->Device == NULL ||
            !p->Device->HoldAsked ||
            XhciLinkDecide(&p->Link, portsc, XHCI_LINK_WANT_HOLD, &act) !=
                XHCI_LINK_ACT_DISABLE) {
            hc->Counters.HoldRequestsRefused++;
            XHCI_DBG_VALUE("hcd: hold refused, port changed meanwhile, "
                           "PORTSC", portsc);
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            h->Used = 0;
            h->Pending = 0;
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            hcdPortInspectAgain(hc, p);
            continue;
        }
        kind = XhciHoldBegin(&h->Hold, h->Port,
                             hc->Hc.PortMap.Companion[h->Port - 1], &id);
        XhciWritePortsc(&hc->Hc, h->Port, XhciPortscDisable(portsc));
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        h->Pending = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (kind == XHCI_HOLD_PAIRED) {
            hc->Counters.HoldsPaired++;
        } else if (kind == XHCI_HOLD_ORPHAN) {
            hc->Counters.HoldsOrphan++;
        } else {
            hc->Counters.HoldsUnidentified++;
        }
        XHCI_DBG_VALUE("hcd: hold begun, port/kind/reason",
                       (h->Port << 16) | (kind << 8) | h->Reason);
        if (p->Enum.State != XHCI_ENUM_EMPTY &&
            p->Enum.State != XHCI_ENUM_GONE) {
            hcdFeed(hc, p, XHCI_ENUM_EV_DISCONNECT);
        }
    }
}

/* Every hold forgotten: at a start, and after a controller reset, whose
 * HCRST re-powers the ports and takes each held link out of SS.Disabled -
 * the device then comes back at SuperSpeed and whoever asked for the
 * send-back meets it again. An active hold dropped so is counted. */
static VOID hcdHoldsForget(PHCD_CONTROLLER hc, ULONG count)
{
    KIRQL oldIrql;
    ULONG i;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (i = 0; i < HCD_MAX_HOLDS; i++) {
        if (count && hc->Holds[i].Used && !hc->Holds[i].Pending) {
            hc->Counters.HoldsDropped++;
        }
        hc->Holds[i].Used = 0;
        hc->Holds[i].Pending = 0;
        hc->Holds[i].Hold.Kind = XHCI_HOLD_NONE;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* ----------------------------------------------------------------------- */
/* Waiting in Gone                                                          */
/* ----------------------------------------------------------------------- */

static ULONG hcdHubQuiet(PHCD_CONTROLLER hc, PHCD_HUB hub);

/*
 * Whether a port in Gone may leave it: the PDO group it reported has been
 * reported missing to PnP (or deleted) - that group, by serial, not
 * whichever PDO last named its location (Codex review of batch (b), round
 * 2, finding 2) - and, where a hub left from it, every port of that hub's
 * subtree has settled the same way, the departed hub object then freed. A
 * PDO reported missing no longer holds the place: its REMOVE may never
 * come on Windows ME (hcd_pdo.c, the lifecycle), and a new device there
 * gets a new PDO, with the same location instance id, while the old one
 * waits for it apart, as usbport's children do (the owner's ruling,
 * 2026-10-04). Thread, or the start and stop with the thread not running.
 */
static ULONG hcdPortQuiet(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    /* A port still holding its record has not run its teardown - its
     * machine reached Gone with the action cut short by a halt (hcdRun) -
     * and no PDO serial says so: it waits for the invalidation's drop
     * (Codex review of 23e7715, finding 1). */
    if (p->Device != NULL) {
        return 0;
    }
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
 * may, and holds no record - so the object may be freed, which forgets its
 * ports' Device pointers (HcdHubFree). The recursion is bounded by the hub
 * depth (XHCI_TOPO_MAX_TIER). */
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
        if (q->Device != NULL) {
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
/* A device leaving, and the subtree below a hub (27-A.3)                   */
/* ----------------------------------------------------------------------- */

/* Step 1 for one device: from here on its URBs are refused and a mapped
 * record is held on its PDO rather than reach the ring (hcd_io.c), a
 * hub's status-change transfer is not armed again (HcdHubRearm), and no
 * doorbell is rung for it - every doorbell of a device's endpoint tests
 * Gone under this same lock: a publish (HcdIoMapped, HcdHubRearm, the
 * thread's EP0 transfer), an isochronous underrun's or overrun's restart
 * (hcd_dev.c, hcdIsoEvent), EP0 recovery and a cancel's restart - so an
 * endpoint the teardown stops stays stopped until its slot is disabled
 * (Codex review of d54eef0, finding 1). */
static VOID hcdDeviceFreeze(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->Gone = 1;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/*
 * Step 3 for one device, its slot still enabled (section 10.5): every
 * endpoint with a TD on its ring that is Running is stopped (Stop Endpoint),
 * so the controller owns none of its TRBs - an endpoint Stopped, Halted or
 * in Error runs nothing already - and everything the device holds, bulk,
 * interrupt, isochronous and control alike, is then taken off the rings
 * (HcdIoDeviceDrain) and held on its PDO rather than completed (hcd_io.c,
 * HcdIoPark). So no URB is left on a ring whose slot is about to go.
 * Nothing reaches a ring after the freeze, so an endpoint with an empty
 * queue needs no command. A Stop Endpoint that fails requests the reset and
 * leaves the URBs where they are: the invalidation drains them once HCRST
 * has taken the slot.
 */
static VOID hcdDeviceStop(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_PIPE pipe;
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG queued;
    ULONG control;
    ULONG code;
    ULONG dci;
    ULONG sid;

    for (dci = 1; dci < 32; dci++) {
        if (hcdHalted(hc)) {
            return;
        }
        pipe = (dci == 1) ? &dev->Ep0Pipe : dev->Pipes[dci];
        if (pipe == NULL) {
            continue;
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        queued = pipe->Queue->Count != 0;
        /* A streams endpoint's TDs are on its streams' rings (31-A.1). */
        for (sid = 1; pipe->Streams != NULL && sid <= pipe->Streams->Count;
             sid++) {
            if (pipe->Streams->Pipe[sid]->Queue->Count != 0) {
                queued = 1;
            }
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (!queued || hcdEpState(hc, dev, dci) != XHCI_EP_STATE_RUNNING) {
            continue;
        }
        code = 0;
        if (XhciTrbStopEndpoint(&trb, dev->SlotId, dci, 0) == XHCI_RING_OK) {
            code = hcdCommand(hc, &trb, &control);
        }
        /* A Context State Error: it halted or stopped meanwhile, and runs
         * nothing either way. */
        if (code != XHCI_CC_SUCCESS && code != XHCI_CC_CONTEXT_STATE_ERROR) {
            hc->TeardownStopFailures++;
            HcdSvcRequestReset(&hc->Hc);
            return;
        }
        hc->TeardownStops++;
    }
    (VOID)HcdIoDeviceDrain(hc, dev);
}

/*
 * Steps 3 and 4 for the device on port q: its PDOs reported missing (PnP is
 * told; NT then sends SURPRISE_REMOVAL and REMOVE, Windows 98 its
 * out-of-sequence REMOVE alone, each PDO deleted at its own REMOVE), its slot
 * given back as `how` says, its record freed - or quarantined when the
 * Disable Slot was not confirmed - and the port's Device cleared. Returns the
 * PDO group serial the port waits for, 0 for none.
 */
static ULONG hcdDeviceGo(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG how,
                         ULONG powered)
{
    PHCD_USB_DEVICE dev;
    ULONG serial;

    dev = q->Device;
    /* Reported already, as the teardown froze it (hcdReportGone): the
     * serial waited for is the one that report left on the port. */
    serial = HcdDevicePdoGone(hc, dev);
    if (serial == 0) {
        serial = q->AwaitSerial;
    }
    q->Device = NULL;
    if (how == HCD_GO_TAKEN) {
        hcdDeviceFree(hc, dev);
    } else if (how == HCD_GO_UNPLUG || (powered && !hcdHalted(hc))) {
        hcdDisableRecord(hc, dev);
    } else {
        dev->Abandoned = 1;
        hc->SlotSweep = 1;
    }
    return serial;
}

/*
 * Step 2, taken with the freeze: the device's PDOs reported missing, and the
 * group serial its port will wait for kept on the port, before any of its
 * URBs leaves the ring. Windows 98 SE's hidclass.sys answers a read completing
 * DEVICE_NOT_CONNECTED by failing every client read and resubmitting at
 * once while its device is still started (hcd_io.c, hcdRefusedDpc), and its
 * clients' retries then keep the processor busy enough that the
 * configuration manager does not act on a relations change raised only
 * afterwards: the REMOVE never came (27-V.1, a USB keyboard unplugged from
 * a root port, 2026-10-04; Phase 26, which reported the PDO first and
 * completed the URBs at the Disable Slot after it, did not stall). So the
 * relations change goes first, as it did there, and GET_PORT_STATUS reads
 * the device disconnected by the time any URB of it is touched (hcd_urb.c,
 * hcdPortStatus) - design record 13 section 10.5's step 2. Reporting first
 * did not by itself end the stall; holding the URBs instead of failing them
 * did (step 3, hcd_io.c, HcdIoPark). The hardware order, the URBs off the
 * rings before the Disable Slot, is kept.
 */
static VOID hcdReportGone(PHCD_CONTROLLER hc, PHCD_PORT q)
{
    q->AwaitSerial = HcdDevicePdoGone(hc, q->Device);
}

/* A port below the departing device, once its own device has gone: where a
 * disconnect puts its machine - waiting in Gone, by serial, for PDOs PnP
 * still holds, and for a departed hub's subtree - or, as the root hub
 * leaves, back to Empty with nothing awaited. The machine's own action for
 * the disconnect is not run: hcdDeviceGo has done it. */
static VOID hcdPortGone(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG how,
                        ULONG serial)
{
    XHCI_ENUM_EVENT event;
    XHCI_ENUM_ACTION act;

    if (how == HCD_GO_DETACH) {
        q->AwaitSerial = 0;
        q->AwaitHub = NULL;
        XhciEnumReset(&q->Enum);
        return;
    }
    q->AwaitSerial = serial;
    hcdEventInit(&event, XHCI_ENUM_EV_DISCONNECT, 1);
    (VOID)XhciEnumStep(&q->Enum, &event, &act);
    (VOID)hcdSettleGone(hc, q);
}

/*
 * Every hub object's parent as XhciHubReleaseOrder takes it: the hub whose
 * port it sits on, or none for a root port; a hub not in the live tree -
 * unused, departing, or no longer the device on the port it came up on - is
 * detached. `top` is in the tree whatever its object says.
 */
static VOID hcdHubParents(PHCD_CONTROLLER hc, ULONG top, PULONG parent)
{
    PHCD_HUB hub;
    PHCD_PORT u;
    ULONG i;

    for (i = 0; i < HCD_MAX_HUBS; i++) {
        hub = &hc->Hubs[i];
        u = hub->Upstream;
        if (!hub->Used || hub->Draining || hub->Device == NULL || u == NULL ||
            u->Device != hub->Device) {
            parent[i] = XHCI_HUB_DETACHED;
        } else if (u->Hub == NULL) {
            parent[i] = XHCI_HUB_NO_PARENT;
        } else {
            parent[i] = u->Hub->Index;
        }
    }
    if (parent[top] == XHCI_HUB_DETACHED) {
        parent[top] = XHCI_HUB_NO_PARENT;
    }
}

/*
 * One hub of a departing subtree, every hub below it already gone: the
 * devices on its ports leave (steps 3 and 4) and their ports settle, its
 * node leaves the graph with whatever is left below it (step 5; before its
 * own record goes, which the hub object names), and the object is freed once
 * every port of it has settled - as the root hub leaves, at once - or kept,
 * Draining, with the port it sat on waiting for it (AwaitHub). Then its own
 * device leaves that port, unless that port is the teardown's top, whose
 * device and machine are the caller's.
 */
static VOID hcdHubGo(PHCD_CONTROLLER hc, PHCD_HUB hub, PHCD_PORT top,
                     ULONG how, ULONG powered)
{
    PHCD_PORT u;
    PHCD_PORT q;
    ULONG serial;
    ULONG n;

    u = hub->Upstream;
    for (n = 1; n <= HCD_HUB_MAX_PORTS; n++) {
        q = HcdHubPort(hc, hub, n);
        if (q->Device != NULL) {
            serial = hcdDeviceGo(hc, q, how, powered);
            hcdPortGone(hc, q, how, serial);
        }
    }
    HcdHubForget(hc, hub);
    if (how == HCD_GO_DETACH || u == NULL || hcdHubQuiet(hc, hub)) {
        HcdHubFree(hc, hub);
    } else {
        u->AwaitHub = hub;
    }
    if (u != NULL && u != top && u->Device != NULL) {
        serial = hcdDeviceGo(hc, u, how, powered);
        hcdPortGone(hc, u, how, serial);
    }
}

/*
 * The one teardown (design record 13 section 10.5): the device on port p
 * leaves, and when it is a hub, every device below it first. Every way a
 * device leaves comes here - a disconnect its root port or its parent hub
 * reports, the enumeration's own failure, a CYCLE_PORT (HCD_GO_UNPLUG); an
 * HCRST that took every slot, after a controller failure or at a stop
 * (HCD_GO_TAKEN); the root hub's removal (HCD_GO_DETACH):
 *
 *   1 freeze: every hub of the subtree stops serving its ports and
 *     re-arming its status-change pipe (Draining), and every device refuses
 *     new work and rings no doorbell;
 *   2 with it, each device's PDOs reported missing, before any URB of it
 *     leaves the ring (hcdReportGone has the Windows 98 reason);
 *   3 with the slots still enabled, leaf first, each device's running
 *     endpoints stopped and its URBs taken off the rings and held on its
 *     PDO - not completed - until the client cancels them or aborts their
 *     pipe, or the PDO is stopped or removed (hcd_io.c, HcdIoPark;
 *     HCD_GO_UNPLUG only - a taken slot runs nothing, and its URBs are
 *     drained as its record is freed, and held the same way);
 *   4 leaf first: each device's slot given back, the devices on a hub's
 *     ports before the hub's own, so no enabled slot's Parent Hub Slot ID
 *     or TT names a disabled one;
 *   5 each hub's node pruned as it goes.
 *
 * The hubs are taken deepest first (XhciHubReleaseOrder), the order the host
 * suite checks. A command that fails on the way requests the controller
 * reset and the teardown goes on without commands where it must: every PDO
 * is still reported missing and every port below still settles, and a slot
 * not confirmed disabled stays quarantined for the invalidation. The ports
 * below p are left where a disconnect puts them; p's own machine is the
 * caller's. Returns the PDO group serial p waits for; p->AwaitHub is set
 * when a departed hub's subtree is still held by PnP.
 */
static ULONG hcdSubtreeGo(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG how,
                          ULONG powered)
{
    ULONG parent[HCD_MAX_HUBS];
    ULONG order[HCD_MAX_HUBS];
    PHCD_USB_DEVICE dev;
    PHCD_HUB hub;
    PHCD_PORT q;
    ULONG count;
    ULONG k;
    ULONG n;

    dev = p->Device;
    if (dev == NULL) {
        return 0;
    }
    count = 0;
    if (dev->Hub != NULL) {
        hcdHubParents(hc, dev->Hub->Index, parent);
        count = XhciHubReleaseOrder(parent, HCD_MAX_HUBS, dev->Hub->Index,
                                    order);
        XHCI_DBG_VALUE("hcd: hub leaving, location/slot/hubs",
                       (p->PortId << 16) | (dev->SlotId << 8) | count);
    }

    for (k = 0; k < count; k++) {
        hub = &hc->Hubs[order[k]];
        hub->Draining = 1;
        for (n = 1; n <= HCD_HUB_MAX_PORTS; n++) {
            q = HcdHubPort(hc, hub, n);
            if (q->Device != NULL) {
                hcdDeviceFreeze(hc, q->Device);
                hcdReportGone(hc, q);
            }
        }
    }
    hcdDeviceFreeze(hc, dev);
    hcdReportGone(hc, p);

    if (how == HCD_GO_UNPLUG) {
        for (k = 0; k < count; k++) {
            hub = &hc->Hubs[order[k]];
            for (n = 1; n <= HCD_HUB_MAX_PORTS; n++) {
                q = HcdHubPort(hc, hub, n);
                if (q->Device != NULL && q->Device->Hub == NULL) {
                    hcdDeviceStop(hc, q->Device);
                }
            }
            if (hub->Device != NULL) {
                hcdDeviceStop(hc, hub->Device);
            }
        }
        if (dev->Hub == NULL) {
            hcdDeviceStop(hc, dev);
        }
    }

    for (k = 0; k < count; k++) {
        hcdHubGo(hc, &hc->Hubs[order[k]], p, how, powered);
    }
    return hcdDeviceGo(hc, p, how, powered);
}

/* ----------------------------------------------------------------------- */
/* Dropping and detaching                                                   */
/* ----------------------------------------------------------------------- */

/* A port whose slot HCRST took: its record goes without a command - a hub's
 * subtree first (hcdSubtreeGo) - its PDO off the bus, and its machine where
 * a disconnect would put it, waiting in Gone, by serial, for a PDO PnP still
 * holds. */
static VOID hcdDropPort(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    XHCI_ENUM_EVENT event;
    XHCI_ENUM_ACTION act;

    if (p->Device != NULL) {
        p->AwaitSerial = hcdSubtreeGo(hc, p, HCD_GO_TAKEN, 0);
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
        /* A quarantined record's PDOs went with its teardown; any other
         * left here never had its PDOs reported missing, and a PDO must
         * not keep naming a freed record (finding 1). */
        if (hc->SlotDevice[i] != NULL) {
            (VOID)HcdDevicePdoGone(hc, hc->SlotDevice[i]);
        }
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
    hcdHoldsForget(hc, 1);
    hcdDropAll(hc);
    hc->ScratchTainted = 0;
}

/* One port as the root hub's removal leaves it: its device - a hub's
 * subtree first (hcdSubtreeGo) - off the bus, its slot disabled when powered
 * and otherwise left Abandoned for the first powered pass, its machine in
 * Empty. */
static VOID hcdDetachPort(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG powered)
{
    if (p->Device != NULL) {
        (VOID)hcdSubtreeGo(hc, p, HCD_GO_DETACH, powered);
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
    if (q->HubSsRecover) {
        /* A SuperSpeed hub port's link recovery (30-A.1): the warm reset
         * only now, with the device it held and that device's subtree torn
         * down by the disconnect just fed (Codex review of 034a119,
         * finding 1). */
        d.Connect = !hcdHalted(hc) && HcdSsHubPortRecover(hc, hub, n);
        q->HubSsRecover = 0;
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
    if (!hcdHalted(hc)) {
        /* 29-A.5's send-backs, after the URBs that may have asked for
         * them. */
        hcdHoldService(hc);
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
        p->Unreadable = 0;
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
    /* A hold lasts until the controller's next start, and this is it. */
    hcdHoldsForget(hc, 0);
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
    /* PDOs PnP itself stopped first - Windows 98 SE's and ME's disable of
     * the controller - stay listed for their devices to come back to
     * (hcd_pdo.c, HcdDevicePdoDormantAll); the drop then finds them
     * detached and leaves their ports Empty, not waiting in Gone. */
    if (hc->StopPreserve) {
        HcdDevicePdoDormantAll(hc);
    }
    hcdDropAll(hc);
}
