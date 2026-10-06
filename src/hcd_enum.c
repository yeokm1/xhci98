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
/* Looks at a hub port whose GET_STATUS failed, in a row, before its look
 * is no longer owed (task 33.3): a bus policy number. */
#define HCD_HUB_LOOK_TRIES     3UL
/* A warm reset read still in progress (task 33.3): the passes after which,
 * whatever the clock says, it is the controller's failure, and the mark that
 * its recovery has been asked for. */
#define HCD_LINK_RECOVERY_PASSES 600UL
#define HCD_LINK_RECOVERY_FAILED 0xFFFFFFFFUL
/* The slice the thread's own control transfer waits on its wake event
 * between looks at its one deadline (35-T.3/4): the deadline is a timer the
 * event wait cannot also wait on (no KeWaitForMultipleObjects row), so a
 * timeout is taken at most this late; a wake ends a slice at once. */
#define HCD_WAIT_SLICE_MS      100UL

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
#if defined(XHCI_FLAVOUR_QEMU)
    /* 35-T.9: a command to an endpoint the injection answers for is
     * completed by it, never sent. */
    if (HcdInjCommand(hc, trb, control, &answer)) {
        return answer;
    }
#endif
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
    /* Its slot is the controller's no longer, nor the abandoned TD that
     * held the scratch (35-T.3). */
    if (dev->Ep0Abandoned && hc->ScratchHeld == dev->SlotId) {
        hc->ScratchHeld = 0;
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
        if (dev->Ep0Abandoned) {
            /* Nothing took the abandoned TD back: the scratch stays out of
             * use until the recovery the failure asked for (35-T.3). */
            hc->ScratchTainted = 1;
        }
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
static ULONG hcdCycleResolve(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);

/* An endpoint's state as the controller keeps it in the output Device
 * Context (xHCI 6.2.3, EP State); Disabled when the slot has no context. */
static ULONG hcdEpState(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG dci)
{
    ULONG offset;

    if (XhciEndpointContextOffset(&hc->Hc.Layout, dev->SlotId, dci,
                                  &offset) != XHCI_LAYOUT_OK) {
        return XHCI_EP_STATE_DISABLED;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    /* 35-T.9: Halted or Error, as the injection answers it. */
    return HcdInjEpState(hc, dev->SlotId, dci,
                         XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]));
#else
    return XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]);
#endif
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
    ULONG marked;
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
        marked = XhciTolMarkPending(&dev->CycleMark);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        /* A device marked for a cycle (35-T.3/4) is not waited out: its
         * client transfer may never end, and the cycle ends it. */
        if (marked && hcdCycleResolve(hc, dev) != XHCI_TOL_CYCLE_NONE) {
            break;
        }
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
    if (dev->CycleAbandon) {
        /* Given up for a cycle (35-T.3): nothing more goes to it. */
        return HCD_CTL_ABANDONED;
    }
    if (dev->Ep0Stuck || hc->ScratchHeld != 0) {
        /* Its record is still queued from a timeout: reusing it would
         * relink the engine's queue (round 2, finding 10). Or another
         * device's abandoned TD may still write the scratch until that
         * slot is taken back (35-T.3). */
        return HCD_CTL_NOT_SENT;
    }
    if (length > HCD_SCRATCH_CONTROL_BYTES ||
        (length != 0 && (requestType & 0x80) == 0)) {
        return HCD_CTL_NOT_SENT;
    }
    if (hcdCycleResolve(hc, dev) != XHCI_TOL_CYCLE_NONE) {
        /* Marked for a cycle (35-T.3/4): the cycle answers the device, so
         * no request is begun on it. */
        dev->CycleAbandon = 1;
        dev->Ep0Stuck = 1;
        return HCD_CTL_ABANDONED;
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
        if (hcdCycleResolve(hc, dev) != XHCI_TOL_CYCLE_NONE) {
            dev->CycleAbandon = 1;
            dev->Ep0Stuck = 1;
            return HCD_CTL_ABANDONED;
        }
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
    KTIMER deadline;
    LARGE_INTEGER due;
    LARGE_INTEGER zero;
    ULONG answer;
    ULONG done;
    ULONG expired;
    ULONG marked;
    ULONG step;
    ULONG reason;
    ULONG abandoned;
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

    /* The transfer's one deadline, HCD_TRANSFER_WAIT_MS, kept across every
     * wake (35-T.4: a stale mark costs its context read, not the wait): a
     * relative timer polled with a zero wait, as hcdEp0Quiet's. */
    KeInitializeTimer(&deadline);
    HcdRelativeMs(&due, HCD_TRANSFER_WAIT_MS);
    (VOID)KeSetTimer(&deadline, due, NULL);
    zero.QuadPart = 0;

    /* Published and rung under the lock, and never for a device being
     * torn down (Gone; Codex review of d54eef0, finding 1). In the same
     * hold the thread registers as this device's waiter and looks for a
     * cycle mark (35-T.3/4): one made after hcdEp0Quiet's last look and
     * before this hold is read here, before anything goes out, and one
     * made after it finds the waiter registered (HcdTolCycleMark). */
    for (;;) {
        expired = KeWaitForSingleObject(&deadline, Executive, KernelMode,
                                        FALSE, &zero) == STATUS_SUCCESS;
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        marked = XhciTolMarkPending(&dev->CycleMark);
        answer = XHCI_XFER_BUSY;
        if (!marked && !expired && !dev->Gone) {
            dev->Ep0Done = 0;
            KeClearEvent(&hc->XferDoneEvent);
            answer = XhciXferSubmitControl(&dev->Ep0Queue, &dev->Ep0, &req,
                                           &dev->Ep0Xfer, dev, trbs,
                                           XHCI_XFER_MAX_CONTROL_TRBS);
            if (answer == XHCI_XFER_OK) {
#if defined(XHCI_FLAVOUR_QEMU)
                /* 35-T.9's EP0 faults withhold this doorbell. */
                HcdInjEp0Doorbell(hc, dev);
#else
                XhciWriteDoorbell(&hc->Hc, dev->SlotId, 1);
#endif
                hc->ThreadEp0Dev = dev;
            }
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (!marked) {
            break;
        }
        step = XhciTolWaitResolved(hcdCycleResolve(hc, dev), 0, expired);
        if (step == XHCI_TOL_WAIT_ABANDON) {
            /* Nothing went out: the record and the scratch are free. */
            (VOID)KeCancelTimer(&deadline);
            dev->CycleAbandon = 1;
            dev->Ep0Stuck = 1;
            return HCD_CTL_ABANDONED;
        }
        if (step == XHCI_TOL_WAIT_TIMEOUT) {
            /* Stale marks past the deadline: nothing was sent. */
            (VOID)KeCancelTimer(&deadline);
            return HCD_CTL_NOT_SENT;
        }
    }
    if (answer != XHCI_XFER_OK) {
        (VOID)KeCancelTimer(&deadline);
        return HCD_CTL_NOT_SENT;
    }

    /*
     * A cycle mark for this device (35-T.3/4, HcdTolCycleMark) ends the
     * wait rather than letting it time out into the controller reset: the
     * mark's reason confirmed, the transfer is abandoned for the cycle. A
     * halt the context shows stale is no reason, and the wait goes on to
     * the same deadline. Each look (XhciTolWaitStep) is taken under the
     * lock the event path sets Ep0Done, the mark and the wake event under,
     * and the event is cleared only in a hold that saw neither, so no wake
     * is lost; a pending mark is read before a timeout is taken.
     */
    done = 0;
    abandoned = 0;
    for (;;) {
        expired = KeWaitForSingleObject(&deadline, Executive, KernelMode,
                                        FALSE, &zero) == STATUS_SUCCESS;
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        step = XhciTolWaitStep(dev->Ep0Done,
                               XhciTolMarkPending(&dev->CycleMark), expired);
        if (step == XHCI_TOL_WAIT_SLEEP) {
            KeClearEvent(&hc->XferDoneEvent);
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (step == XHCI_TOL_WAIT_DONE) {
            done = 1;
            break;
        }
        if (step == XHCI_TOL_WAIT_TIMEOUT) {
            break;
        }
        if (step == XHCI_TOL_WAIT_RESOLVE) {
            /* Decided with this look's deadline and a fresh look at the
             * completion: a stale mark at an expired deadline ends the wait
             * here, so marks installed one after another on another
             * processor cannot hold the thread past it. */
            reason = hcdCycleResolve(hc, dev);
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            done = dev->Ep0Done;
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            step = XhciTolWaitResolved(reason, done, expired);
            if (step == XHCI_TOL_WAIT_ABANDON) {
                abandoned = 1;
                done = 0;
                break;
            }
            if (step == XHCI_TOL_WAIT_DONE || step == XHCI_TOL_WAIT_TIMEOUT) {
                break;
            }
            continue;
        }
        (VOID)hcdWaitEvent(&hc->XferDoneEvent, HCD_WAIT_SLICE_MS);
    }
    (VOID)KeCancelTimer(&deadline);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->ThreadEp0Dev = NULL;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (abandoned) {
        /* The controller may still own the TD and write the scratch:
         * neither the record nor the scratch is used again until the
         * cycle's Disable Slot takes the slot back (hcdDeviceFree), and a
         * Disable Slot that fails taints the scratch (hcdDisableRecord). */
        dev->Ep0Abandoned = 1;
        dev->CycleAbandon = 1;
        dev->Ep0Stuck = 1;
        hc->ScratchHeld = dev->SlotId;
        XhciLogNote(&hc->Hc, "tol.cycle.wait", dev->SlotId);
        return HCD_CTL_ABANDONED;
    }
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
 * enabled; *after is the last PORTSC read, for task 35.3's note. */
static ULONG hcdResetPort(PHCD_CONTROLLER hc, PHCD_PORT p, PULONG speed,
                          PULONG after)
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
    *after = portsc;
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
        *after = portsc;
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
    ULONG after;

    if (p->Hub != NULL) {
        return HcdHubPortReset(hc, p->Hub, p->Number, speedClass);
    }
    speed = 0;
    *speedClass = XHCI_SPEED_UNKNOWN;
    if (!hcdResetPort(hc, p, &speed, &after)) {
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

/* hcd.h. The generation is read here, under the lock its writer takes
 * (hcdConnectGen), so the thread can tell a mark made for this device at
 * this location from one that outlived a connect or a disconnect. */
VOID HcdTolCycleMark(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG reason,
                     ULONG dci)
{
    ULONG gen;

    if (dev == NULL || !hc->Hc.Tol.Stats.Tolerance) {
        return;
    }
    gen = (dev->Location != 0 && dev->Location <= HCD_PORT_COUNT)
              ? hc->Ports[dev->Location - 1].ConnectGen
              : 0;
    if (XhciTolMarkSet(&dev->CycleMark, reason, dci, gen)) {
        XhciLogNoteLocked(&hc->Hc, "tol.cycle.mark",
                          (dev->SlotId << 16) | (dci << 8) | reason);
    }
    if (hc->ThreadEp0Dev == dev) {
        (VOID)KeSetEvent(&hc->XferDoneEvent, IO_NO_INCREMENT, FALSE);
    }
    HcdThreadWake(hc);
}

/* A connect or a disconnect fed at the location: its connect generation
 * moves, under the lock the cycle mark reads it under. Thread only. */
static VOID hcdConnectGen(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    p->ConnectGen++;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/*
 * The thread's reading of a device's cycle mark (record 17 section 4.3): a
 * halt with no TD confirmed only by the endpoint's context read Halted or
 * Error - one read per endpoint named, counted, a stale one clearing the
 * mark - and a refused code taken as it stands. Returns the reason the
 * device is to be cycled for, XHCI_TOL_CYCLE_NONE for none; the mark is
 * left for the cycle to take. The context is in the common buffer, read
 * under the lock the mark is set under. Thread only.
 */
static ULONG hcdCycleResolve(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PXHCI_EXTENSION ext;
    KIRQL oldIrql;
    ULONG halted;
    ULONG reason;
    ULONG retryHead;
    ULONG token;
    ULONG dci;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &oldIrql);
    if (!XhciTolMarkPending(&dev->CycleMark)) {
        XhciControllerLockRelease(ext, oldIrql);
        return XHCI_TOL_CYCLE_NONE;
    }
    halted = 0;
    for (dci = 1; dci < 32; dci++) {
        if ((dev->CycleMark.HaltDcis & (1UL << dci)) == 0) {
            continue;
        }
        ext->Tol.Stats.HaltReads++;
        /* Halted beside a live soft retry whose deferred TD is still the
         * head (35-T.2) is that TD's Transaction Error, the retry's to
         * recover; Error, or a retry request that outlived its TD, is not
         * (XhciTolHaltOwner). */
        retryHead = 0;
        if (dci >= 2 && dev->Pipes[dci] != NULL &&
            XhciXferRetryPending(dev->Pipes[dci]->Queue, &token, NULL)) {
            retryHead = XhciXferRetryHeadIs(dev->Pipes[dci]->Queue, token);
        }
        if (XhciTolHaltOwner(hcdEpState(hc, dev, dci), retryHead) ==
            XHCI_TOL_HALT_CONFIRMED) {
            halted |= 1UL << dci;
        } else {
            ext->Tol.Stats.HaltStale++;
        }
    }
    reason = XhciTolMarkResolve(&dev->CycleMark, halted);
    XhciControllerLockRelease(ext, oldIrql);
    return reason;
}

/* hcd.h: the soft retry's service reads the mark through the same rule. */
ULONG HcdTolCycleResolve(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    return hcdCycleResolve(hc, dev);
}

/* The mark taken off the device as its cycle begins: the reason, the
 * generation it was made at; the mark cleared. Thread only. */
static ULONG hcdCycleTake(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                          PULONG gen)
{
    KIRQL oldIrql;
    ULONG reason;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    reason = XhciTolMarkResolve(&dev->CycleMark, 0);
    *gen = dev->CycleMark.Gen;
    XhciTolMarkInit(&dev->CycleMark);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return reason;
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

/* ----------------------------------------------------------------------- */
/* Task 35.3: a root port's enumeration notes (xhci_enum.h has the labels,  */
/* their packing and the budget). Each is called for a root port only.      */
/* Thread.                                                                  */
/* ----------------------------------------------------------------------- */

static VOID hcdNoteQuiet(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG charged)
{
    if (charged == XHCI_ENUM_NOTE_QUIET) {
        XhciLogNote(&hc->Hc, "enum.port.quiet", XhciEnumNoteQuiet(p->PortId));
    }
}

static VOID hcdNoteLook(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG value)
{
    ULONG charged;

    charged = XhciEnumNoteCharge(&p->Notes);
    if (charged == XHCI_ENUM_NOTE_YES) {
        XhciLogNote(&hc->Hc, "enum.port.look", value);
    }
    hcdNoteQuiet(hc, p, charged);
}

/* A reset's result, and on success the raw speed ID, its class, where its
 * meaning came from and its rate: what 35.0's dump could not say. */
static VOID hcdNoteReset(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG ok,
                         ULONG portsc, ULONG speedClass)
{
    PXHCI_EXTENSION ext;
    ULONG kbps;
    ULONG plus;

    ext = &hc->Hc;
    hcdNoteQuiet(hc, p, XhciEnumNoteBegin(&p->Notes));
    if (!XhciEnumNoteOn(&p->Notes)) {
        return;
    }
    XhciLogNote(ext, "enum.port.reset",
                XhciEnumNoteReset(p->PortId, ok, p->Enum.Retries, portsc));
    if (!ok) {
        return;
    }
    XhciLogNote(ext, "enum.port.speed",
                XhciEnumNoteSpeed(p->PortId, p->LinkPsiv, speedClass,
                                  XhciPortSpeedSource(&ext->PortMap,
                                                      p->PortId,
                                                      p->LinkPsiv)));
    kbps = 0;
    plus = 0;
    (VOID)XhciPortRate(&ext->PortMap, p->PortId, p->LinkPsiv, &kbps, &plus);
    XhciLogNote(ext, "enum.port.rate",
                XhciEnumNoteRate(p->PortId, kbps, plus));
}

static VOID hcdNoteFail(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG cause,
                        ULONG attempt, ULONG final)
{
    if (p->Hub == NULL && XhciEnumNoteOn(&p->Notes)) {
        XhciLogNote(&hc->Hc, "enum.port.fail",
                    XhciEnumNoteFail(p->PortId, cause, attempt, final));
    }
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
    ULONG after;
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
            after = 0;
            ok = hcdResetPort(hc, p, &speed, &after);
            p->LinkPsiv = ok ? speed : 0;
            speedClass = XHCI_SPEED_UNKNOWN;
            if (ok) {
                (VOID)XhciPortSpeedClass(&hc->Hc.PortMap, p->PortId, speed,
                                         &speedClass);
            }
            hcdNoteReset(hc, p, ok, after, speedClass);
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
        if (p->Hub == NULL && XhciEnumNoteOn(&p->Notes)) {
            /* The controller's answer, before this driver's own checks of
             * it: a slot refused below shows as the fail note's cause. */
            XhciLogNote(&hc->Hc, "enum.port.slot",
                        XhciEnumNoteSlot(p->PortId, code, p->Enum.Retries,
                                         code == XHCI_CC_SUCCESS
                                             ? XHCI_TRB_GET_SLOT_ID(control)
                                             : 0UL));
        }
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
            /* Task 34.2: the Power tab cannot show a SuperSpeed device's
             * current above 510 mA, so XHCISNAP's log carries the exact
             * one, after the device's ids. */
            speedClass = XHCI_SPEED_UNKNOWN;
            (VOID)XhciPortSpeedClass(&hc->Hc.PortMap, p->Device->Port,
                                     p->Device->Speed, &speedClass);
            if (speedClass == XHCI_SPEED_SUPER &&
                bytes > XHCI_DESC_OFF_MAX_POWER) {
                XhciLogNote(&hc->Hc, "dev.ss.vidpid",
                            ((ULONG)p->Device->DeviceDesc[9] << 24) |
                                ((ULONG)p->Device->DeviceDesc[8] << 16) |
                                ((ULONG)p->Device->DeviceDesc[11] << 8) |
                                (ULONG)p->Device->DeviceDesc[10]);
                XhciLogNote(&hc->Hc, "dev.ss.maxpower.ma",
                            XhciDescMaxPowerMa(s[XHCI_DESC_OFF_MAX_POWER],
                                               speedClass));
            }
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
            XhciEnumNoteRefill(&p->Notes);
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
            /* Task 35.3: an enumerated port's notes have their budget
             * back (meaningless on a hub's port, which writes none). */
            XhciEnumNoteRefill(&p->Notes);
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

static VOID hcdPortInspectAgain(PHCD_CONTROLLER hc, PHCD_PORT p);
static ULONG hcdTimerFired(PKTIMER t);

/* A hub port deferred for the first answer, remembered by its physical
 * path - the hub's root port and Route String and the port's number - so
 * the recovery a halt brings, which frees the hub object and builds it
 * again, keeps it deferred (HcdHubStart; Codex review of 33.3, round 1,
 * finding 1). A full table defers nothing more by path. Thread only. */
static VOID hcdSettleRememberHubPort(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PHCD_USB_DEVICE hubDev;
    ULONG i;

    if (p->Hub == NULL || p->Hub->Device == NULL) {
        return;
    }
    hubDev = p->Hub->Device;
    if (HcdEnumSettleDeferredAt(hc, hubDev->Port, hubDev->Route, p->Number)) {
        return;
    }
    for (i = 0; i < HCD_SETTLE_DEFER_HUB; i++) {
        if (hc->SettleDeferHub[i].RootPort == 0) {
            hc->SettleDeferHub[i].RootPort = hubDev->Port;
            hc->SettleDeferHub[i].Route = hubDev->Route;
            hc->SettleDeferHub[i].Number = p->Number;
            return;
        }
    }
    /* The table is full: every hub port a rebuild numbers is taken as
     * deferred until the settle, rather than this one forgotten (round 2,
     * finding 2). */
    hc->SettleDeferHubFull = 1;
}

/* Whether the hub port at that path is deferred for the first answer. A
 * hub's bring-up asks it of each port it numbers (HcdHubStart). Thread
 * only. */
ULONG HcdEnumSettleDeferredAt(PHCD_CONTROLLER hc, ULONG rootPort, ULONG route,
                              ULONG number)
{
    ULONG i;

    if (hc->SettleDeferHubFull) {
        return 1;
    }
    for (i = 0; i < HCD_SETTLE_DEFER_HUB; i++) {
        if (hc->SettleDeferHub[i].RootPort != 0 &&
            hc->SettleDeferHub[i].RootPort == rootPort &&
            hc->SettleDeferHub[i].Route == route &&
            hc->SettleDeferHub[i].Number == number) {
            return 1;
        }
    }
    return 0;
}
/* Whether a first answer waits for the settle (task 33.3): a generation
 * asked for and not yet settled. IRQL: <= DISPATCH_LEVEL, controller lock
 * released. */
static ULONG hcdSettleOutstanding(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG outstanding;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    outstanding = !XhciEnumSettleReached(hc->SettleDone, hc->SettleAsked);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return outstanding;
}

/* Run the machine from one event until it asks for nothing, retrying from
 * Reset after a failure while the port still reads connected - once on a
 * root port, and on a hub's port until XHCI_HUB_PORT_ATTEMPTS attempts have
 * failed, when the port is disabled at the hub and left so until its next
 * connect change (section 10.2 step 7). A halted controller (hcdHalted)
 * ends the run where it stands; the invalidation that follows the recovery
 * settles the port from any state.
 *
 * An attempt abandoned for a device cycle (35-T.3/4; design record 17
 * section 4.3) - a control transfer that returned HCD_CTL_ABANDONED, seen as
 * the device's CycleAbandon before it has a PDO, or the run fed
 * XHCI_ENUM_EV_ABANDONED by the cycle service - is not a failure: the
 * outcome is replaced by XHCI_ENUM_EV_ABANDONED, so no continuation and no
 * retry follows, and the slot goes back by the subtree teardown the machine's
 * DISABLE_SLOT runs. Then, once per run - the run is the attempt, and
 * `abandoned` its done-once mark - the pre-PDO cycle's handler
 * (hcdCycleAfter): the location charged once and, when the budget allows
 * and the location still reads connected, 1 returned for the caller to feed
 * a CONNECT to the now-Empty machine. 0 otherwise.
 */
static ULONG hcdCycleAfter(PHCD_CONTROLLER hc, PHCD_PORT p);

/* The device the port's attempt holds gave its control transfers up for a
 * cycle before it had a PDO. Thread only. */
static ULONG hcdRunAbandoned(PHCD_PORT p)
{
    return p->Device != NULL && p->Device->CycleAbandon &&
           p->Device->Pdo == NULL;
}

static ULONG hcdRun(PHCD_CONTROLLER hc, PHCD_PORT p, XHCI_ENUM_EVENT event)
{
    XHCI_ENUM_ACTION act;
    ULONG retries;
    KTIMER budget;
    LARGE_INTEGER due;
    ULONG guard;
    ULONG armed;
    ULONG deferring;
    ULONG cause;
    ULONG attempt;
    ULONG abandoned;
    ULONG reconnect;
    ULONG gen;

    retries = (p->Hub != NULL) ? XHCI_HUB_PORT_ATTEMPTS - 1UL
                               : XHCI_ENUM_RETRIES;
    abandoned = (event.Kind == XHCI_ENUM_EV_ABANDONED) ? 1UL : 0UL;
    /* The per-port budget (task 33.3): a relative timer, as the answer's
     * deadline, unaffected by a change of the system time. */
    armed = 0;
    if (hc->SettlePortMs != 0) {
        KeInitializeTimer(&budget);
        HcdRelativeMs(&due, hc->SettlePortMs);
        (VOID)KeSetTimer(&budget, due, NULL);
        armed = 1;
    }
    deferring = 0;

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
        if (!deferring && act.Kind != XHCI_ENUM_ACT_NONE &&
            act.Kind != XHCI_ENUM_ACT_CREATE_PDO &&
            act.Kind != XHCI_ENUM_ACT_DISABLE_SLOT &&
            act.Kind != XHCI_ENUM_ACT_REPORT_GONE &&
            armed && hcdTimerFired(&budget) && hcdSettleOutstanding(hc)) {
            /* Past its budget while a first answer waits (task 33.3): the
             * attempt is given up as an unplug gives it up - the slot
             * back, the machine to Empty - and the port looked at again
             * once no first answer waits. Checked between steps only, so
             * one step's own timeout can carry a port past it. */
            XHCI_DBG_VALUE("hcd: settle, port over its budget, location",
                           p->PortId);
            deferring = 1;
            hcdEventInit(&event, XHCI_ENUM_EV_DISCONNECT, 1);
            continue;
        }
        if (act.Kind == XHCI_ENUM_ACT_NONE ||
            !hcdPerform(hc, p, &act, &event)) {
            if (p->Enum.State != XHCI_ENUM_FAILED) {
                break;
            }
            cause = p->Enum.FailCause;
            attempt = p->Enum.Retries;
            if (hcdHalted(hc) || !hcdPortConnected(hc, p)) {
                hcdNoteFail(hc, p, cause, attempt, 1);
                break;
            }
            (VOID)XhciEnumRetryUpTo(&p->Enum, retries, &act);
            hcdNoteFail(hc, p, cause, attempt,
                        act.Kind == XHCI_ENUM_ACT_NONE);
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
        if (!abandoned && hcdRunAbandoned(p)) {
            /* The outcome just produced is not fed: the attempt was
             * abandoned for a cycle, and the mark is the cycle's now. */
            abandoned = 1;
            p->CycleReason = hcdCycleTake(hc, p->Device, &gen);
            hcdEventInit(&event, XHCI_ENUM_EV_ABANDONED, 1);
        }
    }
    reconnect = 0;
    if (abandoned) {
        reconnect = hcdCycleAfter(hc, p);
    }
    if (deferring || (hcdHalted(hc) && !p->SettleDeferred &&
                      hcdSettleOutstanding(hc))) {
        /* Fairness for the first answer (task 33.3): a port past its
         * budget, or one whose command or transfer never completed - a 5 s
         * wait and a recovery already, which looked at first again after
         * the rescan would cost them again before any port after it - waits
         * until no first answer does (HcdEnumService). Its look is owed
         * meanwhile: a root port's in PortChange (the recovery's
         * invalidation owes every one), a hub port's in its hub's Changed. */
        if (deferring) {
            if (p->Hub == NULL) {
                hcdPortInspectAgain(hc, p);
            } else {
                p->Hub->Changed |= 1UL << p->Number;
            }
        }
        hcdSettleRememberHubPort(hc, p);
        p->SettleDeferred = 1;
        hc->SettleDeferredNow = 1;
        hc->SettleDeferrals++;
        XHCI_DBG_VALUE("hcd: settle, port deferred, location", p->PortId);
    }
    if (armed) {
        (VOID)KeCancelTimer(&budget);
    }
    if (p->Hub == NULL) {
        if (XhciEnumNoteOn(&p->Notes)) {
            XhciLogNote(&hc->Hc, "enum.port.end",
                        XhciEnumNoteEnd(p->PortId, p->Enum.State,
                                        p->Enum.FailCause, p->Enum.Retries));
        }
        XhciEnumNoteFinish(&p->Notes);
    }
    return reconnect;
}

/* Feed the port's machine one event and run it. A disconnect, a cycle's
 * abandonment, and a connect that starts an enumeration (from Empty or
 * Failed; one the machine ignores leaves its device as it was), move the
 * location's connect
 * generation (35-T.3/4). A pre-PDO cycle's CONNECT is fed here, after the
 * run that cycled, so no run nests in another; each one was charged to the
 * location's budget, which bounds them before the guard does. */
static VOID hcdFeed(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG kind)
{
    XHCI_ENUM_EVENT event;
    ULONG guard;

    if (kind == XHCI_ENUM_EV_DISCONNECT || kind == XHCI_ENUM_EV_ABANDONED ||
        (kind == XHCI_ENUM_EV_CONNECT &&
         (p->Enum.State == XHCI_ENUM_EMPTY ||
          p->Enum.State == XHCI_ENUM_FAILED))) {
        hcdConnectGen(hc, p);
    }
    hcdEventInit(&event, kind, 1);
    for (guard = 0; hcdRun(hc, p, event) && guard < XHCI_TOL_REENUMS;
         guard++) {
        XHCI_DBG_VALUE("hcd: pre-PDO cycle, connect fed again, location",
                       p->PortId);
        hcdConnectGen(hc, p);
        hcdEventInit(&event, XHCI_ENUM_EV_CONNECT, 1);
    }
}

/*
 * The pre-PDO cycle's handler (design record 17 section 4.3), once per
 * attempt (hcdRun), after XHCI_ENUM_EV_ABANDONED's teardown - the slot
 * disabled, the device record and its DCBAA entry freed, as an ordinary
 * failure's teardown does - left the location's own machine Empty with its
 * slot and device cleared (or Gone, for a hub whose devnode exists): the
 * location charged once, counted by the mark's reason, and 1 returned when
 * a CONNECT is to be fed - the charge allowed, the machine Empty and the
 * location still reading connected as hcdPortConnected reads it (CCS at a
 * root port, the hub's port status at a hub's), with no over-current
 * episode or send-back hold at a root port. A refused charge holds the
 * location: the device stays gone. Thread only.
 */
static ULONG hcdCycleAfter(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PXHCI_EXTENSION ext;
    ULONG reason;
    ULONG charged;
    ULONG connected;
    ULONG empty;

    ext = &hc->Hc;
    reason = p->CycleReason;
    p->CycleReason = XHCI_TOL_CYCLE_NONE;
    if (reason >= XHCI_TOL_CYCLE_REASONS) {
        reason = XHCI_TOL_CYCLE_NONE;
    }
    charged = HcdTolLocCharge(hc, p, XHCI_TOL_CHARGE_REENUM);
    if (charged) {
        ext->Tol.Stats.Cycles[reason]++;
        ext->Tol.Stats.CyclesPrePdo++;
    } else {
        ext->Tol.Stats.CyclesRefused++;
    }
    XhciLogNote(ext, "tol.cycle.prepdo",
                (p->PortId << 16) | (charged << 8) | reason);
    empty = (p->Enum.State == XHCI_ENUM_EMPTY && p->Device == NULL) ? 1UL
                                                                    : 0UL;
    connected = 0;
    if (charged && empty && !hcdHalted(hc) &&
        (p->Hub != NULL ||
         (p->TolOc.Phase == XHCI_TOL_OC_NONE &&
          hcdHoldOf(hc, p->PortId) == NULL))) {
        connected = hcdPortConnected(hc, p);
    }
    return XhciTolCycleReconnect(charged, empty, connected);
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

/* ----------------------------------------------------------------------- */
/* 35-T.5: the location the controller gave up on                          */
/* ----------------------------------------------------------------------- */

/* The extension's hub-port budgets are one per hub port object. */
typedef char hcdTolHubLocs[XHCI_TOL_HUB_LOCS ==
                           HCD_MAX_HUBS * HCD_HUB_MAX_PORTS ? 1 : -1];

/*
 * A location's budget (design record 17 sections 4.5 and 4.9), in the
 * extension, where the snapshot and the dump read it: a root port's by its
 * number, a hub port's by its port object. NULL for a port object that is
 * neither. Thread only.
 */
static PXHCI_TOL_LOC hcdTolLoc(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    if (p->Hub == NULL) {
        if (p->PortId < 1 || p->PortId > XHCI_MAX_ROOT_PORTS) {
            return NULL;
        }
        return &hc->Hc.Tol.RootLoc[p->PortId - 1];
    }
    if (p->PortId <= XHCI_MAX_ROOT_PORTS ||
        p->PortId > XHCI_MAX_ROOT_PORTS + XHCI_TOL_HUB_LOCS) {
        return NULL;
    }
    return &hc->Hc.Tol.HubLoc[p->PortId - XHCI_MAX_ROOT_PORTS - 1];
}

/* The location newly held, or its hold changed: counted once and recorded
 * in the note ring, location and reason, beside the hold the dump shows.
 * Thread only. */
static VOID hcdTolHeld(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG was,
                       const XHCI_TOL_LOC *loc)
{
    if (loc->Hold == was) {
        return;
    }
    if (was == XHCI_TOL_HOLD_NONE) {
        hc->Hc.Tol.Stats.Holds++;
    }
    XHCI_DBG_VALUE("hcd: location held, location/reason",
                   (p->PortId << 8) | loc->Hold);
    XhciLogNote(&hc->Hc, "tol.loc.hold", (p->PortId << 8) | loc->Hold);
}

/* Hold the location for reason. Thread only. */
static VOID hcdTolHold(PHCD_CONTROLLER hc, PHCD_PORT p, PXHCI_TOL_LOC loc,
                       ULONG reason)
{
    ULONG was;

    was = loc->Hold;
    XhciTolLocHold(loc, reason);
    hcdTolHeld(hc, p, was, loc);
}

/* HcdTolLocCharge, or with always set the charge made at XhciTolerance 0
 * too: a slot-fatal teardown's re-enumeration, bounded at every value
 * (hcdSlotFatalService). Thread only. */
static ULONG hcdTolLocCharge(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG kind,
                             ULONG always)
{
    PXHCI_TOL_LOC loc;
    ULONG was;

    loc = hcdTolLoc(hc, p);
    if ((!hc->Hc.Tol.Stats.Tolerance && !always) || loc == NULL) {
        return 1;
    }
    was = loc->Hold;
    if (XhciTolLocCharge(loc, kind, HcdTolNow(hc))) {
        /* Completions before the charge are not progress after it. */
        p->TolCompletionsSeen = *(volatile ULONG *)&p->TolCompletions;
        return 1;
    }
    hcdTolHeld(hc, p, was, loc);
    return 0;
}

/* Thread only. */
ULONG HcdTolLocCharge(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG kind)
{
    return hcdTolLocCharge(hc, p, kind, 0);
}

/*
 * One look at the location's port: connected and powered as the port reads
 * them (CCS and PP, or the hub's port status), and changed when a connection
 * change was seen since the last (CSC, or C_PORT_CONNECTION). Only a
 * location with something to re-arm, release or finish is timed, so a
 * location never charged costs nothing. A root port is in a fault the
 * driver's own recovery causes while an over-current episode runs, while
 * its link is in the warm reset an inspection began, and while it is held
 * for 29-A.5's send-back. At XhciTolerance 0 only a location a slot-fatal
 * teardown charged is looked at (XhciTolLocActive), so its hold re-arms
 * by a stable disconnect as at any other value. Thread only.
 */
VOID HcdTolLocObserve(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG connected,
                      ULONG powered, ULONG changed)
{
    PXHCI_TOL_LOC loc;
    ULONG fault;
    ULONG was;

    loc = hcdTolLoc(hc, p);
    if (!XhciTolLocActive(hc->Hc.Tol.Stats.Tolerance, loc) ||
        (!loc->Charged && loc->Hold == XHCI_TOL_HOLD_NONE &&
         !loc->RecoveryDisc)) {
        return;
    }
    fault = (p->Hub == NULL &&
             (p->TolOc.Phase != XHCI_TOL_OC_NONE || p->LinkRecovering != 0 ||
              hcdHoldOf(hc, p->PortId) != NULL))
                ? 1UL
                : 0UL;
    was = loc->Hold;
    if (XhciTolLocObserveChange(loc, connected, powered, fault, changed,
                                HcdTolNow(hc))) {
        XhciLogNote(&hc->Hc, "tol.loc.rearm", (p->PortId << 8) | was);
    }
}

/* A recovery of the driver's own is about to take the location's
 * connection away (an over-current, a warm reset): neither that disconnect
 * nor the reconnect after it is evidence. Thread only. */
VOID HcdTolLocRecovery(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PXHCI_TOL_LOC loc;

    loc = hcdTolLoc(hc, p);
    if (XhciTolLocActive(hc->Hc.Tol.Stats.Tolerance, loc)) {
        XhciTolLocRecovery(loc);
    }
}

/* The location's stable progress: its device's completions since the last
 * pass, while it is bound and the location charged and not held. Thread
 * only. */
static VOID hcdTolProgress(PHCD_CONTROLLER hc, PHCD_PORT p, PXHCI_TOL_LOC loc,
                           ULONG now)
{
    ULONG seen;

    seen = *(volatile ULONG *)&p->TolCompletions;
    if (seen == p->TolCompletionsSeen) {
        return;
    }
    p->TolCompletionsSeen = seen;
    if (!loc->Charged || loc->Hold != XHCI_TOL_HOLD_NONE ||
        p->Enum.State != XHCI_ENUM_BOUND) {
        return;
    }
    if (XhciTolLocProgress(loc, now)) {
        XhciLogNote(&hc->Hc, "tol.loc.rearm", p->PortId << 8);
    }
}

/* A root port held unpowered: PP taken off if the port has power control
 * and it still reads set, so the hold is what the dump says. Never from an
 * unreadable PORTSC, whose bits mean nothing. Thread only. */
static VOID hcdTolUnpower(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG portsc)
{
    if (hc->Hc.HcInfo.Ppc && portsc != 0xFFFFFFFFUL &&
        (portsc & XHCI_PORTSC_PP) != 0) {
        XhciWritePortsc(&hc->Hc, p->PortId, XhciPortscPower(portsc, 0));
    }
}

/*
 * One pass of a root port's over-current episode (record 17 section 4.5):
 * OCA read; once it has read clear for the settle interval the repower is
 * charged and PP set, and once the power-on interval has passed the port
 * is inspected afresh, where a connection is looked for. An OCA that does
 * not clear within the over-current wait, or a repower the budget refuses,
 * holds the port unpowered until a controller start. Thread only, powered.
 */
static VOID hcdTolOcPass(PHCD_CONTROLLER hc, PHCD_PORT p, PXHCI_TOL_LOC loc,
                         ULONG now)
{
    PXHCI_EXTENSION ext;
    ULONG portsc;
    ULONG oca;

    ext = &hc->Hc;
    portsc = XhciReadPortsc(ext, p->PortId);
    if (portsc == 0xFFFFFFFFUL) {
        /* Unreadable says nothing, least of all that OCA is clear: it
         * restarts the settle, the episode's wait still runs to its end,
         * and the read goes to the bounded escalation every inspection's
         * unreadable PORTSC goes to. */
        hcdPortUnreadable(hc, p);
        oca = 1;
    } else {
        oca = (portsc & XHCI_PORTSC_OCA) != 0;
    }
    switch (XhciTolOcStep(&p->TolOc, oca, now)) {
    case XHCI_TOL_OC_ACT_REPOWER:
        if (HcdTolLocCharge(hc, p, XHCI_TOL_CHARGE_REPOWER)) {
            ext->Tol.Stats.Repowers++;
            XHCI_DBG_VALUE("hcd: over-current settled, repower, port",
                           p->PortId);
            XhciWritePortsc(ext, p->PortId, XhciPortscPower(portsc, 1));
            break;
        }
        XhciTolOcInit(&p->TolOc);
        /* A powered hold the over-current found is unpowered now. */
        hcdTolHold(hc, p, loc, XHCI_TOL_HOLD_REPOWERS);
        hcdTolUnpower(hc, p, portsc);
        break;
    case XHCI_TOL_OC_ACT_GIVE_UP:
        hcdTolHold(hc, p, loc, XHCI_TOL_HOLD_OC_WAIT);
        hcdTolUnpower(hc, p, portsc);
        break;
    case XHCI_TOL_OC_ACT_INSPECT:
        hcdPortInspectAgain(hc, p);
        break;
    default:
        break;
    }
}

/*
 * What a root port's inspection feeds, under 35-T.5 (record 17 section
 * 4.5), from what XhciLinkPortFeed decided and the PORTSC it read:
 *
 *   over-current  OCC, or PP clear while the driver's own state says it
 *                 powered the port: the episode starts, the device is fed
 *                 a disconnect, and no connect until the episode's own
 *                 inspection;
 *   PED cleared   a USB 2.0 port with PEC set, PED clear and CCS set under
 *                 a device: the re-enumeration is charged and the device
 *                 fed a disconnect; the inspection the teardown's end
 *                 brings (the PDO's removal, or the one asked for here)
 *                 reads PORTSC afresh and feeds the connect. Charged or
 *                 refused, the device goes: a refusal holds the location;
 *   held          the bus enumerates nothing at the location, and nothing
 *                 at a port whose PP reads clear.
 *
 * At XhciTolerance 0 the feed is returned as it came, but for a location a
 * slot-fatal teardown held. Thread only.
 */
static ULONG hcdTolPortFeed(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG portsc,
                            ULONG feed)
{
    PXHCI_EXTENSION ext;
    PXHCI_TOL_LOC loc;
    ULONG tol;
    ULONG now;
    ULONG powered;

    ext = &hc->Hc;
    tol = ext->Tol.Stats.Tolerance;
    loc = hcdTolLoc(hc, p);
    if (loc == NULL) {
        return feed;
    }
    if (!tol) {
        /* Record 17 section 4.11: off, nothing acts, but a dump taken for
         * the comparison still counts what was seen. OCC and PEC are
         * change bits, and a power loss counts once until PP returns. */
        ext->Tol.Stats.OcFaults +=
            XhciTolOffOcCount((portsc & XHCI_PORTSC_OCC) != 0,
                              (portsc & XHCI_PORTSC_PP) != 0,
                              &p->TolOffPpLost);
        if (XhciTolPedFault(1, !XhciPortIsUsb3(&ext->PortMap, p->PortId),
                            (portsc & XHCI_PORTSC_PEC) != 0,
                            (portsc & XHCI_PORTSC_PED) != 0,
                            (portsc & XHCI_PORTSC_CCS) != 0,
                            p->Device != NULL &&
                                (p->Enum.State == XHCI_ENUM_PRESENT ||
                                 p->Enum.State == XHCI_ENUM_BOUND))) {
            ext->Tol.Stats.PedFaults++;
        }
        /* A location a slot-fatal teardown charged is still observed
         * and its hold still enforced (hcdSlotFatalService). */
        if (XhciTolLocActive(tol, loc)) {
            HcdTolLocObserve(hc, p, (portsc & XHCI_PORTSC_CCS) != 0,
                             (portsc & XHCI_PORTSC_PP) != 0,
                             (portsc & XHCI_PORTSC_CSC) != 0);
            if (XhciTolLocHeld(tol, loc)) {
                feed &= ~XHCI_LINK_FEED_CONNECT;
            }
        }
        return feed;
    }
    now = HcdTolNow(hc);
    HcdTolLocObserve(hc, p, (portsc & XHCI_PORTSC_CCS) != 0,
                     (portsc & XHCI_PORTSC_PP) != 0,
                     (portsc & XHCI_PORTSC_CSC) != 0);
    /* Not while a repower's PP may still be on its way: the power-on
     * interval's own inspection reads it. */
    powered = !XhciTolLocUnpowered(loc) &&
              p->TolOc.Phase == XHCI_TOL_OC_NONE;
    if (XhciTolOcFault(tol, (portsc & XHCI_PORTSC_OCC) != 0,
                       (portsc & XHCI_PORTSC_PP) != 0, powered)) {
        ext->Tol.Stats.OcFaults++;
        XHCI_DBG_VALUE("hcd: root port over-current, port/PORTSC low",
                       (p->PortId << 16) | (portsc & 0xFFFFUL));
        XhciLogNote(ext, "tol.port.oc", p->PortId);
        XhciTolOcBegin(&p->TolOc, now);
        /* The power it lost, and the device's absence until it is back,
         * are the recovery's, never an unplug. */
        XhciTolLocRecovery(loc);
        feed |= XHCI_LINK_FEED_DISCONNECT;
    }
    if (XhciTolPedFault(tol, !XhciPortIsUsb3(&ext->PortMap, p->PortId),
                        (portsc & XHCI_PORTSC_PEC) != 0,
                        (portsc & XHCI_PORTSC_PED) != 0,
                        (portsc & XHCI_PORTSC_CCS) != 0,
                        p->Device != NULL &&
                            (p->Enum.State == XHCI_ENUM_PRESENT ||
                             p->Enum.State == XHCI_ENUM_BOUND)) &&
        p->TolOc.Phase == XHCI_TOL_OC_NONE) {
        ext->Tol.Stats.PedFaults++;
        XHCI_DBG_VALUE("hcd: root port disabled under its device, port",
                       p->PortId);
        XhciLogNote(ext, "tol.port.ped", p->PortId);
        if (HcdTolLocCharge(hc, p, XHCI_TOL_CHARGE_REENUM)) {
            ext->Tol.Stats.Cycles[XHCI_TOL_CYCLE_PED]++;
        } else {
            ext->Tol.Stats.CyclesRefused++;
        }
        feed |= XHCI_LINK_FEED_DISCONNECT;
        hcdPortInspectAgain(hc, p);
    }
    if (p->TolOc.Phase != XHCI_TOL_OC_NONE ||
        loc->Hold != XHCI_TOL_HOLD_NONE ||
        (portsc & XHCI_PORTSC_PP) == 0) {
        feed &= ~XHCI_LINK_FEED_CONNECT;
    }
    return feed;
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
    LARGE_INTEGER due;
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
    /* A warm reset this driver began (task 33.3) is in flight while the
     * port still reads it in progress: the inspection is owed again and
     * nothing is acknowledged or fed until it reads otherwise, so neither a
     * first answer nor the machine takes a link still training for settled
     * (Codex review of 33.3, round 2, finding 1). A reset still in progress
     * past twice its own wait (HCD_WARM_RESET_WAIT_MS, by a relative timer,
     * and by passes as a fallback) is the controller's
     * failure, as an unreadable port is: its recovery is asked for once and
     * the inspection stays owed - never an ordinary inspection of a port
     * still in reset (round 3). */
    if (p->LinkRecovering != 0 && (portsc & XHCI_PORTSC_PR) != 0) {
        if (p->LinkRecovering != HCD_LINK_RECOVERY_FAILED) {
            p->LinkRecovering++;
            if (hcdTimerFired(&hc->LinkRecoverTimer) ||
                p->LinkRecovering >= HCD_LINK_RECOVERY_PASSES) {
                p->LinkRecovering = HCD_LINK_RECOVERY_FAILED;
                XHCI_DBG_VALUE("hcd: warm reset never ended, recovery, port",
                               p->PortId);
                HcdSvcRequestReset(&hc->Hc);
            }
        }
        hcdPortInspectAgain(hc, p);
        return;
    }    p->LinkRecovering = 0;
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
    feed = hcdTolPortFeed(hc, p, portsc, feed);
    if (XhciEnumNoteWantLook(&p->Notes, p->Enum.State, feed,
                             act.Kind == XHCI_LINK_ACT_WARM_RESET ||
                                 act.Kind == XHCI_LINK_ACT_GIVE_UP)) {
        hcdNoteLook(hc, p,
                    XhciEnumNoteLook(p->PortId, p->Enum.State, act.Kind,
                                     feed, portsc));
    }
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
    /* Its outcome is read at the next inspection, which the reset's own
     * change brings; until then the first answer counts it in flight
     * (task 33.3; Codex review of 33.3, round 1, finding 2). */
    p->LinkRecovering = (act.Kind == XHCI_LINK_ACT_WARM_RESET) ? 1UL : 0UL;
    if (p->LinkRecovering) {
        /* The link's absence through its warm reset is the driver's
         * (35-T.5). */
        HcdTolLocRecovery(hc, p);
        /* One relative timer for every root link in recovery, re-armed by
         * each warm reset (a later one extends the others' bound), armed
         * only here and cancelled when the thread leaves (hcd_ctl.c), so
         * none outlives the controller's storage (round 4). */
        HcdRelativeMs(&due, 2UL * HCD_WARM_RESET_WAIT_MS);
        (VOID)KeSetTimer(&hc->LinkRecoverTimer, due, NULL);
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
        slot->SettleGen = hc->SettleAsked;
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

    hcdConnectGen(hc, q);
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

    hcdConnectGen(hc, p);
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
    hc->ScratchHeld = 0;
}

/*
 * 35-T.6's third step (design record 17 section 4.6), once the controller is
 * latched failed and Unreadable and the thread has read Bus Master Enable
 * back clear or failed to: with that proof, every device's transfers are
 * drained before any device is waited out and every device then leaves
 * through the existing departure (hcdDropAll, as an invalidation drops
 * them); without it the caller has pinned the common buffer, and the drain
 * marks each device Gone and keeps every transfer under the pinned-buffer
 * rule. The slot is not given back either way: no command reaches a failed
 * controller. Only the controller thread calls it, the power gate held.
 * IRQL: PASSIVE_LEVEL.
 */
VOID HcdEnumContain(PHCD_CONTROLLER hc, ULONG proof)
{
    if (proof) {
        hcdDropAll(hc);
        return;
    }
    hcdDrainAll(hc);
}

/* One port as the root hub's removal leaves it: its device - a hub's
 * subtree first (hcdSubtreeGo) - off the bus, its slot disabled when powered
 * and otherwise left Abandoned for the first powered pass, its machine in
 * Empty. */
static VOID hcdDetachPort(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG powered)
{
    hcdConnectGen(hc, p);
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
        /* A look that read nothing is owed again, a bounded number of
         * times, so a first answer does not take the port as looked at
         * (task 33.3; Codex review of 33.3, round 1, finding 3). */
        if (++q->LookFails < HCD_HUB_LOOK_TRIES) {
            hub->Changed |= 1UL << n;
        }
        return;
    }
    q->LookFails = 0;
    if (d.Disabled && d.Disconnect && hc->Hc.Tol.Stats.Tolerance) {
        /* The hub disabled the port under its device (35-T.5): the
         * re-enumeration is charged to the location, and a refusal holds
         * it, the device still let go. */
        if (HcdTolLocCharge(hc, q, XHCI_TOL_CHARGE_REENUM)) {
            hc->Hc.Tol.Stats.Cycles[XHCI_TOL_CYCLE_PED]++;
        } else {
            hc->Hc.Tol.Stats.CyclesRefused++;
        }
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
    /* A held location enumerates nothing (35-T.5, and a slot-fatal
     * teardown's hold at any XhciTolerance value). */
    if (XhciTolLocHeld(hc->Hc.Tol.Stats.Tolerance, hcdTolLoc(hc, q))) {
        d.Connect = 0;
    }
    if (d.Connect && !hcdHalted(hc)) {
        hcdFeed(hc, q, XHCI_ENUM_EV_CONNECT);
    }
}

/* Every live hub, in object order: its news collected, each port it names
 * looked at, and its status-change pipe re-armed only after them (section
 * 10.1). A hub brought up during the pass is served in it if its object
 * comes later, at the next pass otherwise. */
static VOID hcdHubService(PHCD_CONTROLLER hc, ULONG outstanding)
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
            if (outstanding && HcdHubPort(hc, hub, n)->SettleDeferred) {
                /* Kept for when no first answer waits (task 33.3). */
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
 * 35-T.5's pass (design record 17 section 4.5): each root port's
 * over-current episode stepped; a root port held unpowered read and its PP
 * taken off again if anything - a recovery's or a resume's
 * reinitialization - put it back; a root port with something to re-arm, a
 * powered hold to release or a recovery's disconnect to finish looked at,
 * and inspected again when the hold is released; a hub port whose stable
 * disconnect is due looked at (its hub's Changed bit, served by
 * hcdHubService in this pass), so the evidence comes from a look made when
 * it was due and never from a late one; every location's stable progress.
 * A location never charged and never held costs no register read and no
 * hub request. At XhciTolerance 0 the pass runs too, for the locations a
 * slot-fatal teardown charged (hcdSlotFatalService): no over-current
 * episode or unpowered hold exists there, and every other location is
 * neither charged nor held, so nothing else is read. Thread only, powered,
 * root hub started.
 */
static VOID hcdTolService(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    PXHCI_TOL_LOC loc;
    PHCD_HUB hub;
    PHCD_PORT p;
    ULONG portsc;
    ULONG now;
    ULONG was;
    ULONG port;
    ULONG i;
    ULONG n;

    ext = &hc->Hc;
    now = HcdTolNow(hc);
    for (port = 1; port <= ext->PortMap.PortCount &&
                   port <= XHCI_MAX_ROOT_PORTS && !hcdHalted(hc); port++) {
        if (!XhciPortIsManaged(&ext->PortMap, port)) {
            continue;
        }
        p = &hc->Ports[port - 1];
        loc = &ext->Tol.RootLoc[port - 1];
        if (p->TolOc.Phase != XHCI_TOL_OC_NONE) {
            hcdTolOcPass(hc, p, loc, now);
        } else if (XhciTolLocUnpowered(loc)) {
            hcdTolUnpower(hc, p, XhciReadPortsc(ext, port));
        } else if (loc->Charged || loc->Hold != XHCI_TOL_HOLD_NONE ||
                   loc->RecoveryDisc) {
            portsc = XhciReadPortsc(ext, port);
            if (portsc != 0xFFFFFFFFUL) {
                was = loc->Hold;
                /* A CSC not yet acknowledged is a connection change since
                 * the last look; the inspection it raised acknowledges it. */
                HcdTolLocObserve(hc, p, (portsc & XHCI_PORTSC_CCS) != 0,
                                 (portsc & XHCI_PORTSC_PP) != 0,
                                 (portsc & XHCI_PORTSC_CSC) != 0);
                if (was != XHCI_TOL_HOLD_NONE &&
                    loc->Hold == XHCI_TOL_HOLD_NONE) {
                    hcdPortInspectAgain(hc, p);
                }
            }
        }
        hcdTolProgress(hc, p, loc, now);
    }
    for (i = 0; i < HCD_MAX_HUBS; i++) {
        hub = &hc->Hubs[i];
        if (!hub->Used || hub->Draining || hub->Device == NULL ||
            hub->Refused) {
            continue;
        }
        for (n = 1; n <= hub->Ports; n++) {
            p = HcdHubPort(hc, hub, n);
            loc = hcdTolLoc(hc, p);
            if (loc == NULL) {
                continue;
            }
            if (XhciTolLocDiscDue(loc, now)) {
                hub->Changed |= 1UL << n;
            }
            hcdTolProgress(hc, p, loc, now);
        }
    }
}

/*
 * The first answer's settle (task 33.3; design record 13 section 5.7), at
 * the end of a pass that enumerated: when nothing is owed and nothing is in
 * flight - no root port change (the reset's own change included, so one
 * more pass looks), no hub port look, no machine between Empty and its rest,
 * no send-back asked for in the window still on its way to the companion -
 * the generation the pass began under is settled. A deferred port's owed
 * look does not count: it waits for the settle. Thread only, powered.
 */
static VOID hcdSettleCheck(PHCD_CONTROLLER hc, ULONG asked)
{
    PXHCI_EXTENSION ext;
    PHCD_HUB hub;
    PHCD_PORT q;
    PHCD_HOLD h;
    KIRQL oldIrql;
    ULONG rootPending;
    ULONG hubPending;
    ULONG inFlight;
    ULONG settled;
    ULONG port;
    ULONG i;
    ULONG n;

    ext = &hc->Hc;
    rootPending = 0;
    hubPending = 0;
    inFlight = 0;
    for (port = 1; port <= ext->PortMap.PortCount &&
                   port <= XHCI_MAX_ROOT_PORTS; port++) {
        if (!XhciPortIsManaged(&ext->PortMap, port)) {
            continue;
        }
        q = &hc->Ports[port - 1];
        if (!XhciEnumAtRest(q->Enum.State) ||
            (q->LinkRecovering && !q->SettleDeferred)) {
            inFlight = 1;
        }
    }
    for (i = 0; i < HCD_MAX_HUBS; i++) {
        hub = &hc->Hubs[i];
        if (!hub->Used || hub->Draining || hub->Device == NULL ||
            hub->Refused) {
            continue;
        }
        for (n = 1; n <= hub->Ports; n++) {
            q = HcdHubPort(hc, hub, n);
            if ((hub->Changed & (1UL << n)) != 0 && !q->SettleDeferred) {
                hubPending = 1;
            }
            if (!XhciEnumAtRest(q->Enum.State)) {
                inFlight = 1;
            }
        }
    }

    settled = 0;
    XhciControllerLockAcquire(ext, &oldIrql);
    for (port = 1; port <= ext->PortMap.PortCount &&
                   port <= XHCI_MAX_ROOT_PORTS; port++) {
        if ((hc->PortChange[(port - 1) / 32UL] &
             (1UL << ((port - 1) % 32UL))) != 0 &&
            XhciPortIsManaged(&ext->PortMap, port) &&
            !hc->Ports[port - 1].SettleDeferred) {
            rootPending = 1;
        }
    }
    for (i = 0; i < HCD_MAX_HOLDS; i++) {
        h = &hc->Holds[i];
        if (h->Used && !XhciEnumSettleReached(hc->SettleDone, h->SettleGen) &&
            XhciEnumHoldInFlight(h->Pending, h->Hold.Kind, h->Hold.Companion,
                                 h->Hold.ConnectSeen)) {
            inFlight = 1;
        }
    }
    if (XhciEnumSettleQuiet(1, rootPending, hubPending, inFlight) &&
        !XhciEnumSettleReached(hc->SettleDone, asked)) {
        hc->SettleDone = asked;
        settled = 1;
    }
    XhciControllerLockRelease(ext, oldIrql);
    if (settled) {
        XHCI_DBG_VALUE("hcd: settle, generation settled", asked);
    }
}

/* Every deferred port given back to its ordinary service once no first
 * answer waits: a root port's look was kept owed in PortChange and a hub
 * port's in its hub's Changed, so the pass that follows takes them. Thread
 * only. */
static VOID hcdSettleUndefer(PHCD_CONTROLLER hc)
{
    ULONG i;

    if (!hc->SettleDeferredNow) {
        return;
    }
    hc->SettleDeferredNow = 0;
    for (i = 0; i < HCD_PORT_COUNT; i++) {
        hc->Ports[i].SettleDeferred = 0;
    }
    for (i = 0; i < HCD_SETTLE_DEFER_HUB; i++) {
        hc->SettleDeferHub[i].RootPort = 0;
    }
    hc->SettleDeferHubFull = 0;
    HcdThreadWake(hc);
}

/*
 * The devices an event left unusable (hcd_dev.c, hcdSlotFatalMark): Table
 * 6-90's recovery for Incompatible Device Error is a Disable Slot (p.468),
 * so a published device leaves as CYCLE_PORT takes it - teardown, Disable
 * Slot, PDO reported missing - and enumerates afresh, through HcdEnumCycle,
 * whose request the CYCLE_PORT step of this same pass takes. A device not
 * yet published is the enumeration's own: the step the code failed already
 * disables its slot before any retry, a bounded number of times. A
 * device already departing is dropped by that departure. The automatic
 * re-enumeration is charged to the location's budget at every
 * XhciTolerance value, 0 included, or a device whose every Configure
 * Endpoint answers the code would be cycled for ever: once the budget is
 * spent the device still goes - the Disable Slot is the specification's
 * and never skipped - but the location is held, and enumerates again only
 * after a stable disconnect re-arms it or a controller start releases it
 * (XhciTolLocActive keeps that observation running at 0). Thread only,
 * powered, root hub started.
 */
static VOID hcdSlotFatalService(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    PHCD_USB_DEVICE dev;
    PHCD_PORT p;
    KIRQL oldIrql;
    ULONG charged;
    ULONG fatal;
    ULONG i;

    ext = &hc->Hc;
    for (i = 1; i <= XHCI_MAX_SLOTS && !hcdHalted(hc); i++) {
        dev = hc->SlotDevice[i];
        if (dev == NULL) {
            continue;
        }
        XhciControllerLockAcquire(ext, &oldIrql);
        fatal = dev->SlotFatal;
        dev->SlotFatal = 0;
        XhciControllerLockRelease(ext, oldIrql);
        if (!fatal) {
            continue;
        }
        p = (dev->Location != 0 && dev->Location <= HCD_PORT_COUNT)
                ? &hc->Ports[dev->Location - 1]
                : NULL;
        if (p == NULL || p->Device != dev || dev->Gone || dev->Abandoned ||
            dev->Pdo == NULL || dev->PdoGroup == 0) {
            continue;
        }
        charged = hcdTolLocCharge(hc, p, XHCI_TOL_CHARGE_REENUM, 1);
        ext->IncompatibleDeviceTeardowns++;
        XhciLogNote(ext, "slot.fatal.cycle",
                    (p->PortId << 16) | (charged << 8) | i);
        XHCI_DBG_VALUE("hcd: slot-fatal code, device cycled, "
                       "location/charged", (p->PortId << 8) | charged);
        HcdEnumCycle(hc, p->PortId, dev->PdoGroup);
    }
}

/*
 * 35-T.3 and 35-T.4's thread step (design record 17 section 4.3): each
 * device the event path marked (HcdTolCycleMark), its mark read - a halt
 * with no TD confirmed by its endpoint's context, a stale one costing that
 * read and nothing more - and then taken. A mark whose device is no longer
 * the one at its location, or whose location saw a connect or a disconnect
 * since, is dropped and counted. Otherwise the cycle is charged to the
 * location's budget (HcdTolLocCharge) by one of two paths: a published
 * device - its PDO and its group's serial, HcdEnumCycle's own condition -
 * through HcdEnumCycle, whose request the CYCLE_PORT step of this same pass
 * takes; a device not yet published through the pre-PDO cycle, the port's
 * machine fed XHCI_ENUM_EV_ABANDONED, whose handler charges it
 * (hcdCycleAfter). Charged or refused, the device goes: a refusal holds the
 * location, as 35-T.5's PED reconnect does. Thread only, powered, root hub
 * started.
 */
static VOID hcdCycleService(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    PHCD_USB_DEVICE dev;
    PHCD_PORT p;
    KIRQL oldIrql;
    ULONG marked;
    ULONG reason;
    ULONG charged;
    ULONG gen;
    ULONG same;
    ULONG i;

    ext = &hc->Hc;
    if (!ext->Tol.Stats.Tolerance) {
        return;
    }
    for (i = 1; i <= XHCI_MAX_SLOTS && !hcdHalted(hc); i++) {
        dev = hc->SlotDevice[i];
        if (dev == NULL) {
            continue;
        }
        XhciControllerLockAcquire(ext, &oldIrql);
        marked = XhciTolMarkPending(&dev->CycleMark);
        XhciControllerLockRelease(ext, oldIrql);
        if (!marked || hcdCycleResolve(hc, dev) == XHCI_TOL_CYCLE_NONE) {
            continue;
        }
        reason = hcdCycleTake(hc, dev, &gen);
        p = (dev->Location != 0 && dev->Location <= HCD_PORT_COUNT)
                ? &hc->Ports[dev->Location - 1]
                : NULL;
        same = (p != NULL && p->Device == dev && !dev->Gone &&
                !dev->Abandoned) ? 1UL : 0UL;
        switch (XhciTolCycleAct(reason, same, gen,
                                p != NULL ? p->ConnectGen : gen + 1UL,
                                dev->Pdo != NULL && dev->PdoGroup != 0)) {
        case XHCI_TOL_CYCLE_ACT_DROP:
            ext->Tol.Stats.CyclesDropped++;
            XhciLogNote(ext, "tol.cycle.dropped", (i << 8) | reason);
            break;
        case XHCI_TOL_CYCLE_ACT_PUBLISHED:
            charged = HcdTolLocCharge(hc, p, XHCI_TOL_CHARGE_REENUM);
            if (charged) {
                ext->Tol.Stats.Cycles[reason]++;
            } else {
                ext->Tol.Stats.CyclesRefused++;
            }
            XhciLogNote(ext, "tol.cycle",
                        (p->PortId << 16) | (charged << 8) | reason);
            XHCI_DBG_VALUE("hcd: device cycled, location/reason",
                           (p->PortId << 8) | reason);
            HcdEnumCycle(hc, p->PortId, dev->PdoGroup);
            break;
        case XHCI_TOL_CYCLE_ACT_PRE_PDO:
            XHCI_DBG_VALUE("hcd: device cycled pre-PDO, location/reason",
                           (p->PortId << 8) | reason);
            p->CycleReason = reason;
            hcdFeed(hc, p, XHCI_ENUM_EV_ABANDONED);
            break;
        default:
            break;
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
    ULONG unproven;
    ULONG detach;
    ULONG asked;
    ULONG outstanding;
    ULONG anyRemoved;
    ULONG word;
    ULONG bit;
    ULONG port;
    ULONG i;
    PHCD_PORT p;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &oldIrql);
    detach = hc->EnumDetachRequested;
    /* The settle generation this pass may settle (task 33.3), read before
     * the ports' change bits below, so a pass that reads it has the bits a
     * root hub's start set with it. */
    asked = hc->SettleAsked;
    outstanding = !XhciEnumSettleReached(hc->SettleDone, asked);
    invalidated = 0;
    unproven = 0;
    if (powered) {
        invalidated = hc->SlotsInvalidated;
        unproven = hc->SlotsUnproven;
        hc->SlotsInvalidated = 0;
    }
    XhciControllerLockRelease(ext, oldIrql);

    /* An invalidation first: what HCRST took needs no Disable Slot, which a
     * detach after it would otherwise send and, refused, turn into another
     * reset (round 3, finding 2). */
    if (invalidated) {
        /* Raised by a recovery or a resume before its halt, which then
         * never completed an HCRST (the halt timed out, or an earlier step
         * refused): the controller may still be following its rings and
         * still be mastering into the clients' buffers, so the drain
         * needs 35-T.6's proof first (design record 17 section 4.6) and,
         * without it, keeps every transfer and mapping on a pinned buffer.
         * A DMA safety rule, not a tolerance behaviour: it applies at every
         * XhciTolerance value, 0 included. */
        if (unproven && !HcdCtlProveDmaStopped(ext)) {
            XhciLogNote(ext, "slots.unproven.pinned", 1);
            HcdSvcDmaNotStopped(ext);
            HcdEnumContain(hc, 0);
        } else {
            hcdInvalidate(hc);
        }
    }
    if (detach) {
        hcdDetach(hc, powered);
    }
    if (!powered) {
        return;
    }
    if (!outstanding) {
        hcdSettleUndefer(hc);
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
#if defined(XHCI_FLAVOUR_QEMU)
        /* 35-T.9's endpoint faults, before the soft retry's service, which
         * then decides an injected error in the same pass. */
        HcdInjService(hc);
#endif
        /* 35-T.2's decisions first: one that yields to a cancel, an abort
         * or a reset applies today's outcome before that operation runs. */
        HcdCfgRetryService(hc);
        HcdCfgCancelService(hc);
        /* The URBs that need commands (hcd_cfg.c). */
        HcdCfgService(hc);
    }
    if (!hcdHalted(hc)) {
        /* 29-A.5's send-backs, after the URBs that may have asked for
         * them. */
        hcdHoldService(hc);
    }
    if (!hcdHalted(hc) && hc->RootHubStarted && hc->ScratchVa != NULL) {
        /* The slot-fatal teardowns and 35-T.3/4's cycles, before the
         * CYCLE_PORT requests are taken below, so a published device's
         * cycle runs in this pass. */
        hcdSlotFatalService(hc);
        hcdCycleService(hc);
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
        if (outstanding && hc->Ports[port - 1].SettleDeferred) {
            /* Owed again, for when no first answer waits (task 33.3). */
            hcdPortInspectAgain(hc, &hc->Ports[port - 1]);
            continue;
        }
        hcdPortChanged(hc, &hc->Ports[port - 1]);
        if (hcdHalted(hc)) {
            return;
        }
    }
    hcdTolService(hc);
    hcdHubService(hc, outstanding);
    if (outstanding && !hcdHalted(hc)) {
        hcdSettleCheck(hc, asked);
    }
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

static ULONG hcdSettleAskLocked(PHCD_CONTROLLER hc);

/*
 * The root hub's start (hcd_rh.c): PDOs may be created, and every port is
 * looked at; returns the settle generation its first answer waits for
 * (task 33.3), 0 for none. IRQL: PASSIVE_LEVEL.
 */
ULONG HcdEnumAttach(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG target;
    ULONG i;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->RootHubStarted = 1;
    for (i = 0; i < HCD_PORT_WORDS; i++) {
        hc->PortChange[i] = 0xFFFFFFFFUL;
    }
    /* The settle the root hub's first answer waits for (task 33.3), asked
     * in the same hold as the ports are marked, so the pass that reads it
     * looks at every port. */
    target = hcdSettleAskLocked(hc);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdThreadWake(hc);
    return target;
}

/* ----------------------------------------------------------------------- */
/* The first answer's settle (task 33.3)                                    */
/* ----------------------------------------------------------------------- */

/*
 * The waiter's half, design record 13 section 5.7. A hub FDO's start asks
 * for a settle generation (HcdEnumAttach for the root hub, HcdEnumSettleAsk
 * for an external hub's FDO); its first BusRelations answer then looks
 * (HcdEnumSettleStep) and sleeps, outside every lock and outside the
 * controller - which it re-enters for each look - until the thread has
 * settled that generation, the root hub or the thread has gone, or its
 * deadline has passed. It never waits on PnP: the thread it waits for
 * creates PDOs and invalidates relations but waits on no IRP of PnP's.
 * IRQL: PASSIVE_LEVEL for the sleeps; the functions below say their own.
 */

/* The low word of the system time, in 100 ns units. IRQL: <= DISPATCH. */
ULONG HcdEnumSettleClock(VOID)
{
    LARGE_INTEGER now;

    KeQuerySystemTime(&now);
    return now.LowPart;
}

/* A new generation, 0 when the wait is off. Never 0 otherwise, so 0 can
 * mean "nothing asked". Controller lock held. */
static ULONG hcdSettleAskLocked(PHCD_CONTROLLER hc)
{
    if (hc->SettleCapMs == 0) {
        return 0;
    }
    hc->SettleAsked++;
    if (hc->SettleAsked == 0) {
        hc->SettleAsked++;
    }
    return hc->SettleAsked;
}

/* An external hub FDO's start (hcd_hubfdo.c): a generation its first
 * answer waits for, 0 for none. IRQL: <= DISPATCH_LEVEL. */
ULONG HcdEnumSettleAsk(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG target;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    target = hcdSettleAskLocked(hc);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (target != 0) {
        HcdThreadWake(hc);
    }
    return target;
}

/* The answer's deadline: a relative timer, unaffected by a change of the
 * system time, polled with a zero wait as HcdHubPortDebounce polls its own;
 * the waiter cancels it before its stack frame goes. 0 when the wait is
 * off. IRQL: <= DISPATCH_LEVEL. */
ULONG HcdEnumSettleArm(PHCD_CONTROLLER hc, PKTIMER deadline)
{
    LARGE_INTEGER due;

    if (hc->SettleCapMs == 0) {
        return 0;
    }
    KeInitializeTimer(deadline);
    HcdRelativeMs(&due, hc->SettleCapMs);
    (VOID)KeSetTimer(deadline, due, NULL);
    return 1;
}

/* Whether a relative timer has expired. IRQL: <= DISPATCH_LEVEL. */
static ULONG hcdTimerFired(PKTIMER t)
{
    LARGE_INTEGER zero;

    zero.QuadPart = 0;
    return KeWaitForSingleObject(t, Executive, KernelMode, FALSE, &zero) ==
           STATUS_SUCCESS;
}

/*
 * One look: how long to sleep before the next, or 0 to answer now, with
 * *why saying which - the generation settled (HCD_SETTLE_DONE), nothing
 * left that could settle it: the root hub not started or detaching, the
 * thread not running (HCD_SETTLE_TORNDOWN), or the deadline passed
 * (HCD_SETTLE_DEADLINE). IRQL: <= DISPATCH_LEVEL.
 */
ULONG HcdEnumSettleStep(PHCD_CONTROLLER hc, ULONG target, PKTIMER deadline,
                        PULONG why)
{
    KIRQL oldIrql;
    ULONG done;
    ULONG gone;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    done = target == 0 || XhciEnumSettleReached(hc->SettleDone, target);
    gone = !hc->RootHubStarted || hc->EnumDetachRequested ||
           !hc->ThreadRunning;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (done) {
        *why = HCD_SETTLE_DONE;
        return 0;
    }
    if (gone) {
        *why = HCD_SETTLE_TORNDOWN;
        return 0;
    }
    if (hcdTimerFired(deadline)) {
        *why = HCD_SETTLE_DEADLINE;
        return 0;
    }
    return XHCI_ENUM_SETTLE_STEP_MS;
}

/*
 * The answer is going. A generation not settled is retired now by the
 * waiter - SettleDone only moves forward - so the ports deferred for it are
 * given back and a send-back still on its way no longer holds a later
 * answer; what is still enumerating finishes after the answer and is
 * reported by the invalidation its PDO's creation makes, as before 33.3.
 * A deadline is counted apart from a teardown, and the time waited, read
 * off the system clock for the trace alone, is traced. IRQL: <=
 * DISPATCH_LEVEL.
 */
VOID HcdEnumSettleEnd(PHCD_CONTROLLER hc, ULONG target, ULONG startLow,
                      ULONG why)
{
    KIRQL oldIrql;
    ULONG waited;
    ULONG retired;

    if (target == 0) {
        return;
    }
    waited = XhciEnumElapsedMs(startLow, HcdEnumSettleClock());
    retired = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (!XhciEnumSettleReached(hc->SettleDone, target)) {
        hc->SettleDone = target;
        retired = 1;
    }
    if (why == HCD_SETTLE_DEADLINE) {
        hc->SettleTimeouts++;
    } else if (why == HCD_SETTLE_TORNDOWN) {
        hc->SettleAborts++;
    }
    hc->SettleWaits++;
    hc->SettleLastMs = waited;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    XHCI_DBG_VALUE("hcd: settle, first answer after ms (bits 30-31: why)",
                   (waited & 0x3FFFFFFFUL) | (why << 30));
    if (retired) {
        HcdThreadWake(hc);
    }
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
        p->LinkRecovering = 0;
        XhciLinkInit(&p->Link);
        XhciEnumNotesInit(&p->Notes);
        if (p->Enum.State == XHCI_ENUM_GONE && !hcdPortQuiet(hc, p)) {
            continue;
        }
        XhciEnumReset(&p->Enum);
        p->AwaitSerial = 0;
        p->AwaitHub = NULL;
    }
    for (i = 0; i < HCD_PORT_COUNT; i++) {
        hc->Ports[i].SettleDeferred = 0;
        /* 35-T.5's per-port state, set by the start as the budgets are
         * (XhciTolStart, HcdHubStart). */
        XhciTolOcInit(&hc->Ports[i].TolOc);
        hc->Ports[i].TolCompletions = 0;
        hc->Ports[i].TolOffPpLost = 0;
        hc->Ports[i].TolCompletionsSeen = 0;
        /* 35-T.3/4's: no device record outlives the stop, nor its mark. */
        hc->Ports[i].ConnectGen = 0;
        hc->Ports[i].CycleReason = XHCI_TOL_CYCLE_NONE;
    }
    hc->SettleDeferredNow = 0;
    for (i = 0; i < HCD_SETTLE_DEFER_HUB; i++) {
        hc->SettleDeferHub[i].RootPort = 0;
    }
    hc->SettleDeferHubFull = 0;
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
    hc->SlotsUnproven = 0;
    /* 35-T.2: the queues that held requests went with their devices. */
    hc->RetryWork = 0;
    hc->EnumDetachRequested = 0;
    hc->ScratchTainted = 0;
    hc->ScratchHeld = 0;
    hc->ThreadEp0Dev = NULL;
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
