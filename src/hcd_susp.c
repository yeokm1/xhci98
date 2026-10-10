/*
 * hcd_susp.c - selective suspend's thread executor (roadmap-hcd.md task
 * 36.30; design record 14 sections 3, 3.9 and 3.10), and stage B's for
 * SuperSpeed devices (36.38; section 8): the functions' stop, Function
 * Suspend with wake per function, the strict disarm, U3 at a SuperSpeed hub
 * port, the Function Wake notification's latch and its consumers, and the
 * configuration transaction's capability reads.
 *
 * The enum thread's half of selective suspend: the admission gate on a
 * device's pipes and the restart that reopens it (3.2), the suspend and
 * resume sequences at a root port and a USB 2.0 hub port (3.4 to 3.6) as
 * xhci_susp.c's steps, the decision pass over the device's PDO slots with
 * the completions it owes (3.1, 3.9), the wake event's claim (5.4), and
 * the evidence the root port's acknowledgement and the hub port look hand
 * over (3.7, 3.8). Every decision is xhci_susp.c's; this file reads the
 * values under the controller lock, asks, and performs the one step
 * asked. Dispatch's half (D-IRP, WAIT_WAKE and START admission) is
 * hcd_pdo.c's (36.31). Inert while hc->SuspOn is 0.
 *
 * IRQL: PASSIVE_LEVEL, the enum thread only, powered, unless a function
 * says otherwise. Every IRP is completed outside the controller lock.
 */

#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/* USB 2.0 9.4: SET_FEATURE/CLEAR_FEATURE(DEVICE_REMOTE_WAKEUP) to the
 * device. */
#define HCD_SUSP_RT_DEVICE_OUT      0x00U
#define HCD_SUSP_REQ_CLEAR_FEATURE  0x01U
#define HCD_SUSP_REQ_SET_FEATURE    0x03U
#define HCD_SUSP_FEAT_REMOTE_WAKEUP 1U

/* USB 3.2 9.4.9 and Table 9-7 (record 14 section 8.1): SetFeature(
 * FUNCTION_SUSPEND), selector 0, to the function's first interface, its
 * Suspend Options in wIndex 15:8 - bit 0 Low Power Suspend, bit 1 Function
 * Remote Wake - and GetStatus() to that interface (9.4.5): bit 0 Function
 * Remote Wake Capable. */
#define HCD_SUSP_RT_IFACE_OUT       0x01U
#define HCD_SUSP_RT_IFACE_IN        0x81U
#define HCD_SUSP_REQ_GET_STATUS     0x00U
#define HCD_SUSP_FEAT_FUNC_SUSPEND  0U
#define HCD_SUSP_FS_ARM             0x03UL
#define HCD_SUSP_FS_DISARM          0x00UL
#define HCD_SUSP_FWR_CAPABLE        0x01U

/* The susp.fw line's dispositions (record 14 section 8.10). */
#define HCD_SUSP_FW_EVENT           1UL /* the suspension's wake (a)      */
#define HCD_SUSP_FW_OBSERVE         2UL /* a hub port's observation (b)   */
#define HCD_SUSP_FW_REFUSAL         3UL /* a refusal's end (c)            */
#define HCD_SUSP_FW_DROPPED         4UL /* a device not suspended, or an
                                         * end not a refusal's            */
#define HCD_SUSP_FW_CLOSED          5UL /* the window closed at receipt   */
#define HCD_SUSP_FW_UNATTRIBUTED    0x8UL /* or'ed: no PDO's function     */

/* The PDOs of a device's chain: one per function of a split device, at
 * most XHCI_FUNC_MAX (xhci_func.c), else one. Read into the controller's
 * scratch (hc->SuspFuncs), not onto the enum thread's stack (Codex review
 * of 36.30, round 1, finding 4). */
#define HCD_SUSP_MAX_FUNCS          XHCI_FUNC_MAX

/* Decisions one device is given in one pass (3.10). */
#define HCD_SUSP_PASSES             4UL

/* The hub port's poll step (hcd_hub.c's HCD_HUB_POLL_STEP_MS). */
#define HCD_SUSP_HUB_POLL_MS        10UL

/* The pure core's D-states are DEVICE_POWER_STATE's values (3.9). */
XHCI_C_ASSERT(susp_d0, XHCI_SUSP_D0 == (ULONG)PowerDeviceD0);
XHCI_C_ASSERT(susp_d1, XHCI_SUSP_D1 == (ULONG)PowerDeviceD1);
XHCI_C_ASSERT(susp_d2, XHCI_SUSP_D2 == (ULONG)PowerDeviceD2);
XHCI_C_ASSERT(susp_d3, XHCI_SUSP_D3 == (ULONG)PowerDeviceD3);

/* ------------------------------------------------------------------ */
/* Small services                                                      */
/* ------------------------------------------------------------------ */

static ULONG hcdSuspHalted(PHCD_CONTROLLER hc)
{
    return hc->Hc.ControllerFailed || hc->ScratchTainted;
}

/* The bounded log lines (36.33): at most HCD_SUSP_NOTE_BUDGET notes per
 * start, the rest counted in SuspNotesSuppressed, so a device that
 * suspends and wakes all day cannot flood the note ring. IRQL: PASSIVE_LEVEL
 * or DISPATCH_LEVEL with no lock held. */
#define HCD_SUSP_NOTE_BUDGET        64L

static VOID hcdSuspNote(PHCD_CONTROLLER hc, const char *label, ULONG value)
{
    if (InterlockedIncrement(&hc->SuspNotes) <= HCD_SUSP_NOTE_BUDGET) {
        XhciLogNote(&hc->Hc, label, value);
    } else {
        (VOID)InterlockedIncrement(
            (PLONG)&hc->Counters.SuspNotesSuppressed);
    }
}

/* The same, from inside the controller lock (stage B's notification lines,
 * the DPC's among them). IRQL: DISPATCH_LEVEL, controller lock held. */
static VOID hcdSuspNoteLocked(PHCD_CONTROLLER hc, const char *label,
                              ULONG value)
{
    if (InterlockedIncrement(&hc->SuspNotes) <= HCD_SUSP_NOTE_BUDGET) {
        XhciLogNoteLocked(&hc->Hc, label, value);
    } else {
        (VOID)InterlockedIncrement(
            (PLONG)&hc->Counters.SuspNotesSuppressed);
    }
}

/*
 * A wait the machine requires - the dwell, TDRSMDN, TRSMRCY, SP=1's
 * minimum - and only these advance its clock (3.10). Before Windows 8 a
 * relative KeDelayExecutionThread may end up to a clock period early, so
 * a minimum is kept by stalling, in 50 us steps, which never ends early;
 * these waits are tens of milliseconds and rare (Codex review of 36.30,
 * round 3, finding 2).
 */
static VOID hcdSuspHold(PHCD_CONTROLLER hc, ULONG milliseconds)
{
    ULONG steps;

    for (steps = milliseconds * 20UL; steps != 0; steps--) {
        KeStallExecutionProcessor(50);
    }
    hc->SuspClockMs += milliseconds;
}

/* A pause between polls, which bounds nothing and is not counted: the
 * clock only ever under-counts, so every wait computed from it is at
 * least as long as asked. */
static VOID hcdSuspPause(ULONG milliseconds)
{
    LARGE_INTEGER due;

    HcdRelativeMs(&due, milliseconds);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
}

static ULONG hcdSuspEpState(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            ULONG dci)
{
    ULONG offset;

    if (XhciEndpointContextOffset(&hc->Hc.Layout, dev->SlotId, dci,
                                  &offset) != XHCI_LAYOUT_OK) {
        return XHCI_EP_STATE_DISABLED;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    return HcdInjEpState(hc, dev->SlotId, dci,
                         XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]));
#else
    return XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]);
#endif
}

static PHCD_PIPE hcdSuspPipe(PHCD_USB_DEVICE dev, ULONG dci)
{
    return (dci == 1) ? &dev->Ep0Pipe : dev->Pipes[dci];
}

static PHCD_PORT hcdSuspPort(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    if (dev->Location == 0 || dev->Location > HCD_PORT_COUNT) {
        return NULL;
    }
    return &hc->Ports[dev->Location - 1];
}

static PHCD_DEVICE_PDO hcdSuspPdo(PDEVICE_OBJECT object)
{
    return (object != NULL) ? (PHCD_DEVICE_PDO)object->DeviceExtension
                            : NULL;
}

/* The function PDOs on the device's chain (the thread is its writer). A
 * composite parent (task 36.43; design record 13 section 10.12) heads a
 * split device's chain and is no voter: it carries no client, so nothing
 * moves its D-state from D0, and the device's link follows its children
 * alone. Every walk that votes skips it. */
static ULONG hcdSuspChainLength(PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO pdo;
    ULONG n;

    n = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        if (!pdo->Composite) {
            n++;
        }
    }
    return n;
}

/* Whether the machine serves the device: the switch on, a USB 2.0 or
 * (stage B, 8.2) SuperSpeed device that is not a hub, with a PDO (5.1). */
static ULONG hcdSuspServes(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO pdo;

    if (!hc->SuspOn || dev->Gone || dev->Hub != NULL) {
        return 0;
    }
    pdo = hcdSuspPdo(dev->Pdo);
    if (pdo == NULL || pdo->Hub) {
        return 0;
    }
    if (hcdSuspChainLength(dev) > HCD_SUSP_MAX_FUNCS) {
        /* Not reached: a split makes at most XHCI_FUNC_MAX functions. A
         * decision over part of a chain could suspend the device under a
         * function in D0, so a longer one is not served. */
        return 0;
    }
    /* The class decoded at the PDO's creation (hcd_pdo.c). */
    return XhciSuspEligible(1, pdo->SpeedClass, 0);
}

/* A SuperSpeed device's link, stage B's (8.4): its first PDO's class, which
 * every PDO of the device shares. */
static ULONG hcdSuspSs(PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO pdo;

    pdo = hcdSuspPdo(dev->Pdo);
    return (pdo != NULL && pdo->SpeedClass == XHCI_SPEED_SUPER) ? 1UL : 0UL;
}

/* A PDO's ownership mask (8.3). A composite parent owns no interface: it
 * is no function (task 36.43), and its Function of 0 would otherwise read
 * as an unsplit device's, owning every interface. */
static ULONG hcdSuspOwn(PHCD_DEVICE_PDO pdo)
{
    if (pdo->Composite) {
        return 0;
    }
    return XhciFuncOwnership(pdo->Function, pdo->InterfaceMask);
}

/* The union of the device's PDOs' FwArmed, kept in FwArmedAll for the
 * snapshot. Thread; controller lock held for the write. */
static ULONG hcdSuspArmedAll(PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO pdo;
    ULONG all;

    all = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        all |= pdo->FwArmed;
    }
    dev->FwArmedAll = all;
    return all;
}

/* Notifications a closing window dropped (XHCI_SUSP_LINK.FwDropped) into
 * the counter. Controller lock held. */
static VOID hcdSuspFwDrain(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    hc->Counters.FuncWakeIgnored += dev->Susp.FwDropped;
    dev->Susp.FwDropped = 0;
}

/*
 * One consumer's latched notifications (8.7): each interface mapped to the
 * PDO whose ownership mask holds it - the function that woke - counted
 * unattributed when it is no first interface of the configuration, no PDO
 * holds it, or it is past 31, and each logged (susp.fw: slot, interface,
 * link state, PDO index, disposition). Returns how many. The chain stable
 * (the thread); controller lock held.
 */
static ULONG hcdSuspFwNote(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           ULONG bits, ULONG beyond, ULONG disposition)
{
    PHCD_DEVICE_PDO pdo;
    ULONG masks[HCD_SUSP_MAX_FUNCS];
    ULONG count;
    ULONG index;
    ULONG iface;
    ULONG n;
    ULONG d;

    count = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL && count < HCD_SUSP_MAX_FUNCS;
         pdo = pdo->Sibling) {
        if (pdo->Composite) {
            continue;   /* indexed as hcdSuspRead indexes the functions */
        }
        masks[count++] = hcdSuspOwn(pdo);
    }
    n = 0;
    for (iface = 0; iface < 33; iface++) {
        if (iface < 32 ? (bits & (1UL << iface)) == 0 : !beyond) {
            continue;
        }
        n++;
        index = XhciFuncOwnerOf(masks, count, iface);
        d = disposition;
        if (iface >= 32 || (dev->FwFirsts & (1UL << iface)) == 0 ||
            index >= count) {
            hc->Counters.FuncWakeUnattributed++;
            d |= HCD_SUSP_FW_UNATTRIBUTED;
            index = 0xFF;
        }
        hcdSuspNoteLocked(hc, "susp.fw",
                          ((dev->SlotId & 0xFFUL) << 24) |
                              (((iface < 32) ? iface : 0xFFUL) << 16) |
                              ((index & 0xFFUL) << 8) |
                              ((dev->Susp.State & 0xFUL) << 4) | (d & 0xFUL));
    }
    return n;
}

/* The device's function PDOs into funcs[] (at most HCD_SUSP_MAX_FUNCS),
 * pdos[] aligned with it, and their count; a composite parent is skipped
 * (hcdSuspChainLength). Controller lock held. */
static ULONG hcdSuspRead(PHCD_USB_DEVICE dev, PHCD_DEVICE_PDO *pdos,
                         PXHCI_SUSP_FUNC funcs)
{
    PHCD_DEVICE_PDO pdo;
    ULONG n;

    n = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL && n < HCD_SUSP_MAX_FUNCS;
         pdo = pdo->Sibling) {
        if (pdo->Composite) {
            continue;
        }
        pdos[n] = pdo;
        funcs[n].PwrState = pdo->PwrState;
        funcs[n].PwrWanted = pdo->PwrWanted;
        funcs[n].DxPended = (pdo->PwrIrp != NULL &&
                             pdo->PwrWanted != XHCI_SUSP_D0) ? 1UL : 0UL;
        funcs[n].DxEpoch = pdo->PwrDxEpoch;
        funcs[n].D0Pended = (pdo->PwrIrp != NULL &&
                             pdo->PwrWanted == XHCI_SUSP_D0) ? 1UL : 0UL;
        funcs[n].WaitWake = (pdo->WaitWakeIrp != NULL) ? 1UL : 0UL;
        funcs[n].Closed = pdo->PwrClosed;
        funcs[n].Seq = pdo->PwrSeq;
        funcs[n].WokeHold = pdo->WokeHold;
        funcs[n].CompleteDx = 0;
        funcs[n].ToD0 = 0;
        funcs[n].CompleteD0 = 0;
        /* Stage B (8.3, 8.8): a wake function, and armed. Both 0 for a
         * USB 2.0 device, whose FwCapable and FwArmed are never set. */
        funcs[n].WakeFn = (dev->FwCapable & hcdSuspOwn(pdo)) != 0 ? 1UL : 0UL;
        funcs[n].Armed = (pdo->FwArmed != 0) ? 1UL : 0UL;
        funcs[n].ArmNow = 0;
        n++;
    }
    return n;
}

/* A D-IRP the thread claimed, completed SUCCESS (4.2): PoStartNextPowerIrp
 * first, the D0's power state recorded. */
static VOID hcdSuspCompletePower(PHCD_DEVICE_PDO pdo, PIRP irp, ULONG d0)
{
    POWER_STATE state;

    if (d0) {
        pdo->Common.DevicePower = PowerDeviceD0;
        state.DeviceState = PowerDeviceD0;
        (VOID)PoSetPowerState(pdo->Common.Self, DevicePowerState, state);
    }
    PoStartNextPowerIrp(irp);
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    (VOID)InterlockedDecrement(&pdo->PowerPending);
}

/* A WAIT_WAKE the thread claimed, completed with `status`. */
static VOID hcdSuspCompleteWake(PHCD_DEVICE_PDO pdo, PIRP irp,
                                NTSTATUS status)
{
    PoStartNextPowerIrp(irp);
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    (VOID)InterlockedDecrement(&pdo->PowerPending);
}

/*
 * The PDO's power reports, ordered (Codex review of 36.31, round 4): a
 * D-IRP's admission with its report, a START's report and the thread's
 * report of a START-driven D0 each run while this flag is owned, so an
 * older D0 report cannot land after a newer Dx's. A test-and-set by
 * InterlockedExchange, polled at 1 ms, rather than a wait on an event,
 * which Windows 98 can end without satisfying (win98-wdm.md; round 5).
 * Nothing done while owned blocks but the spin locks. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdSuspReportEnter(PHCD_DEVICE_PDO pdo)
{
    LARGE_INTEGER due;

    while (InterlockedExchange((PLONG)&pdo->PwrReport, 1) != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}

static VOID hcdSuspReportLeave(PHCD_DEVICE_PDO pdo)
{
    (VOID)InterlockedExchange((PLONG)&pdo->PwrReport, 0);
}

/* The thread wrote PwrState D0 for a START with no D0 IRP (3.9, 3.11):
 * the power manager is told now that the device is powered, unless a newer
 * D-IRP has moved the slot since. Thread only, PASSIVE_LEVEL. */
static VOID hcdSuspReportD0(PHCD_DEVICE_PDO pdo)
{
    PHCD_CONTROLLER hc;
    POWER_STATE state;
    KIRQL oldIrql;
    ULONG still;

    hc = pdo->Controller;
    if (hc == NULL) {
        return;
    }
    hcdSuspReportEnter(pdo);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    still = (pdo->PwrState == XHCI_SUSP_D0 &&
             pdo->PwrWanted == XHCI_SUSP_D0 && !pdo->PwrClosed) ? 1UL : 0UL;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (still) {
        pdo->Common.DevicePower = PowerDeviceD0;
        state.DeviceState = PowerDeviceD0;
        (VOID)PoSetPowerState(pdo->Common.Self, DevicePowerState, state);
    }
    hcdSuspReportLeave(pdo);
}

/* What one claim took, completed after the hold. */
typedef struct _HCD_SUSP_TAKEN {
    ULONG Count;
    PHCD_DEVICE_PDO Pdo[3 * HCD_SUSP_MAX_FUNCS];
    PIRP Irp[3 * HCD_SUSP_MAX_FUNCS];
    ULONG Kind[3 * HCD_SUSP_MAX_FUNCS];
} HCD_SUSP_TAKEN, *PHCD_SUSP_TAKEN;

#define HCD_SUSP_TAKE_DX        1UL
#define HCD_SUSP_TAKE_D0        2UL
#define HCD_SUSP_TAKE_WAKE_OK   3UL
#define HCD_SUSP_TAKE_WAKE_NS   4UL     /* STATUS_NOT_SUPPORTED */
#define HCD_SUSP_TAKE_REPORT    5UL     /* no IRP: a START's D0 reported */

static VOID hcdSuspTake(PHCD_SUSP_TAKEN t, PHCD_DEVICE_PDO pdo, PIRP irp,
                        ULONG kind)
{
    if (t->Count < 3 * HCD_SUSP_MAX_FUNCS) {
        t->Pdo[t->Count] = pdo;
        t->Irp[t->Count] = irp;
        t->Kind[t->Count] = kind;
        t->Count++;
    }
}

/*
 * The enum thread never completes a power or idle IRP itself (36.35's
 * 98 SE reading: after a remote wake the thread completed the WAIT_WAKE
 * inside its pass, the client's callback asked D0 at once, and the thread
 * never came back to finish the resume - the port left in Resume, the
 * door blocked on the power gate the pass holds). A completion runs the
 * client's code, which may ask for another power IRP and, on Windows 98,
 * wait for it; that IRP needs this very thread. So an IRP the thread (or
 * the power path, holding the gate) claims is completed at PASSIVE_LEVEL
 * by a completion worker, outside every gate and lock: a system thread of
 * the controller's, parked between completions (record 14 section 3.12).
 *
 * Neither the hand-off nor the enum thread creates a thread: the per-IRP
 * thread this replaced was created by the enum thread, and on Windows 98
 * SE its PsCreateSystemThread never returned during the S5 transition
 * (switch 1, a client in D0 asking D3 at shutdown) - "Windows is shutting
 * down" for ever. The start provisions HCD_SUSP_DONE_SPARE workers and a
 * grower while the switch is on, or turns the switch off for that start
 * (HcdSuspDoneStart). A hand-off queues the IRP through its own storage
 * and wakes a parked worker; a worker completes pending IRPs until none
 * is left, then parks. A callback that waits for another IRP holds only
 * its own worker, and the IRP it waits for is completed by another (Codex
 * review of the first fix, rounds 1 to 3: a single worker, then the
 * system work queues, could each be held by the callback waiting on
 * them). When an IRP has waited HCD_SUSP_DONE_STALL_MS with no worker
 * parked, the grower - a thread of its own, holding nothing - makes one
 * more; a creation that does not return then blocks the grower alone.
 * There is no ceiling (a ceiling deadlocks a chain one deeper than it):
 * growth needs a pending IRP and no parked worker, so each worker made
 * was wanted by an IRP no worker could take; the pool keeps what it grew
 * until the stop, as resources allow. Workers and the grower leave
 * only at the stop (HcdSuspDoneDrain), which joins each - on NT by the
 * thread object, on 9x by the flag the thread sets last, as the enum and
 * callback threads are joined there. The IRP keeps its PowerPending or
 * IdlePending count until its completion has returned, which the PDO's
 * REMOVE and rundown wait for. NT is given the same pool: its thread
 * creation is not known to block in a transition, but one path serves
 * both.
 */
#define HCD_SUSP_DONE_POWER     1UL     /* a D-IRP; the value its D0 flag */
#define HCD_SUSP_DONE_WAKE      2UL
#define HCD_SUSP_DONE_IDLE      3UL

#define HCD_SUSP_DONE_SPARE     2UL     /* provisioned by the start       */
#define HCD_SUSP_DONE_STALL_MS  500UL
#define HCD_SUSP_DONE_WATCH_MS  100UL   /* the grower's look while stalled */

/* A worker's or the grower's context. */
typedef struct _HCD_SUSP_DONE {
    LIST_ENTRY Link;            /* SuspDoneList (workers), SuspDoneLock  */
    PHCD_CONTROLLER Hc;
    KEVENT Kick;                /* set by whoever unparks or wakes it   */
    ULONG Parked;               /* a worker, under SuspDoneLock         */
    PVOID ThreadObject;         /* NT: referenced; 9x: NULL             */
    HANDLE Handle;              /* NT, while the reference is still owed */
    volatile LONG Exited;       /* set just before PsTerminateSystemThread */
} HCD_SUSP_DONE, *PHCD_SUSP_DONE;

/* The low word of the system time, in 100 ns units. IRQL: any. */
static ULONG hcdSuspDoneNow(VOID)
{
    LARGE_INTEGER now;

    KeQuerySystemTime(&now);
    return now.LowPart;
}

/* One claimed IRP completed: its kind, value and PDO from the hand-off
 * (DriverContext), read before the completion gives the IRP away.
 * IRQL: PASSIVE_LEVEL, a worker. */
static VOID hcdSuspDoneComplete(PIRP irp)
{
    PHCD_DEVICE_PDO pdo;
    ULONG_PTR value;
    ULONG kind;

    kind = (ULONG)(ULONG_PTR)irp->Tail.Overlay.DriverContext[0];
    value = (ULONG_PTR)irp->Tail.Overlay.DriverContext[1];
    pdo = (PHCD_DEVICE_PDO)irp->Tail.Overlay.DriverContext[2];
    irp->Tail.Overlay.DriverContext[0] = NULL;
    irp->Tail.Overlay.DriverContext[1] = NULL;
    irp->Tail.Overlay.DriverContext[2] = NULL;
    switch (kind) {
    case HCD_SUSP_DONE_POWER:
        hcdSuspCompletePower(pdo, irp, (ULONG)value);
        break;
    case HCD_SUSP_DONE_WAKE:
        hcdSuspCompleteWake(pdo, irp, (NTSTATUS)value);
        break;
    default:
        (VOID)HcdCompleteIrp(irp, (NTSTATUS)value, 0);
        (VOID)InterlockedDecrement(&pdo->IdlePending);
        break;
    }
}

/* A parked worker unparked and its event set, if there is one. Returns
 * whether there was. SuspDoneLock held. */
static ULONG hcdSuspDoneKickLocked(PHCD_CONTROLLER hc)
{
    PHCD_SUSP_DONE w;
    PLIST_ENTRY entry;

    if (hc->SuspDoneIdle == 0) {
        return 0;
    }
    for (entry = hc->SuspDoneList.Flink; entry != &hc->SuspDoneList;
         entry = entry->Flink) {
        w = CONTAINING_RECORD(entry, HCD_SUSP_DONE, Link);
        if (w->Parked) {
            w->Parked = 0;
            hc->SuspDoneIdle--;
            (VOID)KeSetEvent(&w->Kick, IO_NO_INCREMENT, FALSE);
            return 1;
        }
    }
    return 0;
}

/* The flag set last and the thread gone. IRQL: PASSIVE_LEVEL. */
static VOID hcdSuspDoneExit(PHCD_SUSP_DONE w)
{
    (VOID)InterlockedExchange((PLONG)&w->Exited, 1);
    (VOID)PsTerminateSystemThread(STATUS_SUCCESS);
}

/*
 * A worker: every pending IRP completed, one after another, then parked
 * until unparked; once the stop has asked and none is pending, gone. A
 * wake not from an unpark (Parked still set) unparks itself, so the count
 * stays true whatever ended the wait. IRQL: PASSIVE_LEVEL (a system
 * thread).
 */
static VOID NTAPI hcdSuspDoneThread(PVOID context)
{
    PHCD_SUSP_DONE w;
    PHCD_CONTROLLER hc;
    PLIST_ENTRY entry;
    KIRQL oldIrql;

    w = (PHCD_SUSP_DONE)context;
    hc = w->Hc;
    for (;;) {
        KeAcquireSpinLock(&hc->SuspDoneLock, &oldIrql);
        if (w->Parked) {
            w->Parked = 0;
            hc->SuspDoneIdle--;
        }
        if (!IsListEmpty(&hc->SuspDonePending)) {
            entry = RemoveHeadList(&hc->SuspDonePending);
            KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
            hcdSuspDoneComplete(
                CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry));
            continue;
        }
        if (hc->SuspDoneStop) {
            KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
            break;
        }
        w->Parked = 1;
        hc->SuspDoneIdle++;
        KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
        (VOID)KeWaitForSingleObject(&w->Kick, Executive, KernelMode, FALSE,
                                    NULL);
    }
    hcdSuspDoneExit(w);
}

/* A context zeroed, for a worker or the grower. IRQL: PASSIVE_LEVEL. */
static PHCD_SUSP_DONE hcdSuspDoneAlloc(PHCD_CONTROLLER hc)
{
    PHCD_SUSP_DONE w;
    PUCHAR p;
    ULONG i;

    w = (PHCD_SUSP_DONE)HcdPoolAlloc(sizeof(HCD_SUSP_DONE));
    if (w == NULL) {
        return NULL;
    }
    p = (PUCHAR)w;
    for (i = 0; i < sizeof(HCD_SUSP_DONE); i++) {
        p[i] = 0;
    }
    w->Hc = hc;
    KeInitializeEvent(&w->Kick, SynchronizationEvent, FALSE);
    return w;
}

/* `w`'s thread created; on NT its object referenced, or the handle kept
 * and the reference retried at the join (Codex review of the first fix,
 * round 4, finding 3). IRQL: PASSIVE_LEVEL, a system thread. */
static NTSTATUS hcdSuspDoneCreate(PHCD_SUSP_DONE w, PKSTART_ROUTINE routine)
{
    HANDLE handle;
    NTSTATUS status;

    status = PsCreateSystemThread(&handle, THREAD_ALL_ACCESS, NULL, NULL,
                                  NULL, routine, w);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        if (NT_SUCCESS(ObReferenceObjectByHandle(handle, THREAD_ALL_ACCESS,
                                                 NULL, KernelMode,
                                                 &w->ThreadObject, NULL))) {
            (VOID)ZwClose(handle);
        } else {
            w->ThreadObject = NULL;
            w->Handle = handle;
        }
    } else {
        (VOID)ZwClose(handle);
    }
    return STATUS_SUCCESS;
}

/*
 * One worker made and put on the list - on the list, counted and not
 * parked, before its thread exists, so the thread finds itself there; a
 * thread not had takes it off again. Returns whether it was made. IRQL:
 * PASSIVE_LEVEL, the start, the grower or the stop - never the enum
 * thread and never a hand-off.
 */
static ULONG hcdSuspDoneNew(PHCD_CONTROLLER hc)
{
    PHCD_SUSP_DONE w;
    KIRQL oldIrql;
    NTSTATUS status;

    w = hcdSuspDoneAlloc(hc);
    if (w == NULL) {
        return 0;
    }
    KeAcquireSpinLock(&hc->SuspDoneLock, &oldIrql);
    InsertTailList(&hc->SuspDoneList, &w->Link);
    hc->SuspDoneCount++;
    KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
    status = hcdSuspDoneCreate(w, hcdSuspDoneThread);
    if (!NT_SUCCESS(status)) {
        KeAcquireSpinLock(&hc->SuspDoneLock, &oldIrql);
        RemoveEntryList(&w->Link);
        hc->SuspDoneCount--;
        KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
        HcdPoolFree(w);
        XHCI_DBG_VALUE("hcd: completion worker not had, status",
                       (ULONG)status);
        return 0;
    }
    return 1;
}

/*
 * One thread joined and its context freed: NT by its thread object; 9x by
 * its flag, then a short grace. The flag is set just before the thread's
 * call into PsTerminateSystemThread, so on 9x it proves the context's last
 * use, not that the thread has left the image: the same standing as the
 * enum thread's and the callback thread's joins (hcd_ctl.c, hcdThreadStart;
 * a Windows 98 stop unloads nothing, and the remove that would has not
 * been measured against that window). IRQL: PASSIVE_LEVEL, the stop.
 */
static VOID hcdSuspDoneJoin(PHCD_SUSP_DONE w)
{
    LARGE_INTEGER due;

    /* NT, the reference still owed: retried until had - never the 9x flag
     * on NT (Codex review of the first fix, round 4, finding 3). */
    while (w->Handle != NULL) {
        if (NT_SUCCESS(ObReferenceObjectByHandle(w->Handle, THREAD_ALL_ACCESS,
                                                 NULL, KernelMode,
                                                 &w->ThreadObject, NULL))) {
            (VOID)ZwClose(w->Handle);
            w->Handle = NULL;
            break;
        }
        HcdRelativeMs(&due, 10);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    if (w->ThreadObject != NULL) {
        (VOID)KeWaitForSingleObject(w->ThreadObject, Executive, KernelMode,
                                    FALSE, NULL);
        ObDereferenceObject(w->ThreadObject);
    } else {
        while (w->Exited == 0) {
            HcdRelativeMs(&due, 1);
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        }
        HcdRelativeMs(&due, 10);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    HcdPoolFree(w);
}

/*
 * The growth rule: one worker more when an IRP is pending with no worker
 * parked and that has lasted HCD_SUSP_DONE_STALL_MS - every worker held,
 * as by a callback waiting for another of this pool's IRPs - or at once
 * with no worker at all. Returns whether a stall is being watched (the
 * grower looks again soon). The system time's low word: a clock jump can
 * make a growth early, which costs a parked thread. IRQL: PASSIVE_LEVEL,
 * the grower or the stop.
 */
static ULONG hcdSuspDoneGrowStep(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG now;
    ULONG count;

    now = hcdSuspDoneNow();
    KeAcquireSpinLock(&hc->SuspDoneLock, &oldIrql);
    if (IsListEmpty(&hc->SuspDonePending) || hc->SuspDoneIdle != 0) {
        hc->SuspDoneStalled = 0;
        KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
        return 0;
    }
    if (hc->SuspDoneCount != 0) {
        if (!hc->SuspDoneStalled) {
            hc->SuspDoneStalled = 1;
            hc->SuspDoneStallSince = now;
            KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
            return 1;
        }
        if (now - hc->SuspDoneStallSince <
            HCD_SUSP_DONE_STALL_MS * 10000UL) {
            KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
            return 1;
        }
    }
    hc->SuspDoneStalled = 0;
    KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
    if (hcdSuspDoneNew(hc)) {
        KeAcquireSpinLock(&hc->SuspDoneLock, &oldIrql);
        count = hc->SuspDoneCount;
        KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
        hcdSuspNote(hc, "susp.done.grow", count);
    }
    return 1;
}

/*
 * The grower: the growth rule applied when woken (a hand-off found no
 * worker parked) and every HCD_SUSP_DONE_WATCH_MS while a stall is
 * watched; gone at the stop's word. It holds no lock, gate or IRP, so a
 * creation that does not return blocks nothing but itself. IRQL:
 * PASSIVE_LEVEL (a system thread).
 */
static VOID NTAPI hcdSuspGrowThread(PVOID context)
{
    PHCD_SUSP_DONE g;
    PHCD_CONTROLLER hc;
    LARGE_INTEGER due;
    ULONG watch;

    g = (PHCD_SUSP_DONE)context;
    hc = g->Hc;
    for (;;) {
        if (hc->SuspGrowStop) {
            break;
        }
        watch = hcdSuspDoneGrowStep(hc);
        if (watch) {
            HcdRelativeMs(&due, HCD_SUSP_DONE_WATCH_MS);
            (VOID)KeWaitForSingleObject(&g->Kick, Executive, KernelMode,
                                        FALSE, &due);
        } else {
            (VOID)KeWaitForSingleObject(&g->Kick, Executive, KernelMode,
                                        FALSE, NULL);
        }
    }
    hcdSuspDoneExit(g);
}

/*
 * A claimed IRP handed over for completion: queued on the controller's
 * pending list through its own storage (Tail.Overlay.ListEntry; the kind,
 * value and PDO in DriverContext), so the hand-off itself needs nothing
 * and cannot fail, and a parked worker unparked. With none parked the
 * grower is woken to watch for the stall: a busy worker takes the IRP when
 * its completion returns. IRQL: <= DISPATCH_LEVEL.
 */
static VOID hcdSuspDefer(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo, PIRP irp,
                         ULONG kind, ULONG_PTR value)
{
    KIRQL oldIrql;

    irp->Tail.Overlay.DriverContext[0] = (PVOID)(ULONG_PTR)kind;
    irp->Tail.Overlay.DriverContext[1] = (PVOID)value;
    irp->Tail.Overlay.DriverContext[2] = pdo;
    KeAcquireSpinLock(&hc->SuspDoneLock, &oldIrql);
    InsertTailList(&hc->SuspDonePending, &irp->Tail.Overlay.ListEntry);
    if (!hcdSuspDoneKickLocked(hc) && hc->SuspGrower != NULL) {
        (VOID)KeSetEvent(&hc->SuspGrower->Kick, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
}

/* The thread's claims, deferred (a START's report, no IRP, made here). */
static VOID hcdSuspFinishT(PHCD_CONTROLLER hc, PHCD_SUSP_TAKEN t)
{
    ULONG i;

    for (i = 0; i < t->Count; i++) {
        switch (t->Kind[i]) {
        case HCD_SUSP_TAKE_DX:
        case HCD_SUSP_TAKE_D0:
            hcdSuspDefer(hc, t->Pdo[i], t->Irp[i], HCD_SUSP_DONE_POWER,
                         (t->Kind[i] == HCD_SUSP_TAKE_D0) ? 1 : 0);
            break;
        case HCD_SUSP_TAKE_WAKE_OK:
            hcdSuspDefer(hc, t->Pdo[i], t->Irp[i], HCD_SUSP_DONE_WAKE,
                         (ULONG_PTR)STATUS_SUCCESS);
            break;
        case HCD_SUSP_TAKE_REPORT:
            hcdSuspReportD0(t->Pdo[i]);
            break;
        default:
            hcdSuspDefer(hc, t->Pdo[i], t->Irp[i], HCD_SUSP_DONE_WAKE,
                         (ULONG_PTR)STATUS_NOT_SUPPORTED);
            break;
        }
    }
    t->Count = 0;
}

static VOID hcdSuspFinish(PHCD_SUSP_TAKEN t)
{
    ULONG i;

    for (i = 0; i < t->Count; i++) {
        switch (t->Kind[i]) {
        case HCD_SUSP_TAKE_DX:
            hcdSuspCompletePower(t->Pdo[i], t->Irp[i], 0);
            break;
        case HCD_SUSP_TAKE_D0:
            hcdSuspCompletePower(t->Pdo[i], t->Irp[i], 1);
            break;
        case HCD_SUSP_TAKE_WAKE_OK:
            hcdSuspCompleteWake(t->Pdo[i], t->Irp[i], STATUS_SUCCESS);
            break;
        case HCD_SUSP_TAKE_REPORT:
            hcdSuspReportD0(t->Pdo[i]);
            break;
        default:
            hcdSuspCompleteWake(t->Pdo[i], t->Irp[i], STATUS_NOT_SUPPORTED);
            break;
        }
    }
    t->Count = 0;
}

/*
 * The WAIT_WAKEs of the device's PDOs claimed by 4.2's handshake: with
 * `watermark` (a wake event) those stamped before it whose function is D1
 * or D2, each given WokeHold (3.9), to complete SUCCESS; without (a STALL's
 * refusal), all of them, to complete NOT_SUPPORTED. A cancel routine
 * already running keeps its IRP. Returns how many were claimed.
 * Controller lock held.
 */
static ULONG hcdSuspClaimWakes(PHCD_USB_DEVICE dev, ULONG useMark,
                               ULONG watermark, PHCD_SUSP_TAKEN t)
{
    PHCD_DEVICE_PDO pdo;
    PIRP irp;
    ULONG claimed;

    claimed = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        irp = pdo->WaitWakeIrp;
        if (irp == NULL) {
            continue;
        }
        if (useMark &&
            !XhciSuspWakeClaims(pdo->WakeStamp, watermark, pdo->PwrState)) {
            continue;
        }
        if (!XhciSuspClaim(1, IoSetCancelRoutine(irp, NULL) != NULL)) {
            continue;
        }
        pdo->WaitWakeIrp = NULL;
        if (useMark) {
            pdo->WokeHold = 1;
        }
        hcdSuspTake(t, pdo, irp,
                    useMark ? HCD_SUSP_TAKE_WAKE_OK : HCD_SUSP_TAKE_WAKE_NS);
        claimed++;
    }
    return claimed;
}

/* The wake event's claim, when the machine says one is due (5.4): in the
 * caller's hold. Controller lock held. */
static VOID hcdSuspClaimDue(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            PHCD_SUSP_TAKEN t)
{
    ULONG claimed;

    if (!dev->Susp.ClaimDue) {
        return;
    }
    claimed = hcdSuspClaimWakes(dev, 1, hc->WakeSeq, t);
    XhciSuspWakeCount(&dev->Susp, claimed);
    hc->Counters.SuspWakeEvents++;
    if (claimed == 0) {
        hc->Counters.SuspWakeUnclaimed++;
    }
    hc->Counters.SuspWaitWakesCompleted += claimed;
}

/* ------------------------------------------------------------------ */
/* The idle notification's settlements (36.32; sections 6.4, 6.5)      */
/* ------------------------------------------------------------------ */

#ifndef STATUS_POWER_STATE_INVALID
#define STATUS_POWER_STATE_INVALID ((NTSTATUS)0xC00002D3L)
#endif

/* What one hold settled of the idle notification, completed after it: an
 * IRP with its status, or (Irp NULL) a callback-queue entry's IdlePending
 * reference let go. One device's chain at most: an IRP and an entry per
 * PDO, and the claim's or the return's own. */
#define HCD_SUSP_ITAKE_MAX      (2 * HCD_SUSP_MAX_FUNCS + 4)

typedef struct _HCD_SUSP_ITAKEN {
    ULONG Count;
    PHCD_DEVICE_PDO Pdo[HCD_SUSP_ITAKE_MAX];
    PIRP Irp[HCD_SUSP_ITAKE_MAX];
    NTSTATUS Status[HCD_SUSP_ITAKE_MAX];
} HCD_SUSP_ITAKEN, *PHCD_SUSP_ITAKEN;

static VOID hcdSuspITake(PHCD_SUSP_ITAKEN t, PHCD_DEVICE_PDO pdo, PIRP irp,
                         NTSTATUS status)
{
    if (t->Count < HCD_SUSP_ITAKE_MAX) {
        t->Pdo[t->Count] = pdo;
        t->Irp[t->Count] = irp;
        t->Status[t->Count] = status;
        t->Count++;
    }
}

/* Each taken idle IRP completed and its IdlePending reference dropped,
 * then each entry's: the reference is the PDO's last touch here (4.2's
 * lifetime counts). Not under any lock: a completion can re-enter the
 * bus. IRQL: <= DISPATCH_LEVEL (an idle IRP is no power IRP). */
static VOID hcdSuspIFinish(PHCD_SUSP_ITAKEN t)
{
    ULONG i;

    for (i = 0; i < t->Count; i++) {
        if (t->Irp[i] != NULL) {
            (VOID)HcdCompleteIrp(t->Irp[i], t->Status[i], 0);
        }
        (VOID)InterlockedDecrement(&t->Pdo[i]->IdlePending);
    }
    t->Count = 0;
}

/* The thread's idle settlements, deferred as hcdSuspFinishT's; an entry's
 * reference is let go here (its IRP keeps its own until completed). */
static VOID hcdSuspIFinishT(PHCD_CONTROLLER hc, PHCD_SUSP_ITAKEN t)
{
    ULONG i;

    for (i = 0; i < t->Count; i++) {
        if (t->Irp[i] != NULL) {
            hcdSuspDefer(hc, t->Pdo[i], t->Irp[i], HCD_SUSP_DONE_IDLE,
                         (ULONG_PTR)t->Status[i]);
        } else {
            (VOID)InterlockedDecrement(&t->Pdo[i]->IdlePending);
        }
    }
    t->Count = 0;
}

static NTSTATUS hcdSuspIdleStatus(ULONG st)
{
    switch (st) {
    case XHCI_SUSP_ST_SUCCESS:
        return STATUS_SUCCESS;
    case XHCI_SUSP_ST_POWER_INVALID:
        return STATUS_POWER_STATE_INVALID;
    default:
        break;
    }
    return STATUS_CANCELLED;
}

/*
 * One settlement trigger (6.5, XHCI_SUSP_SET_*) reaching the PDO's idle
 * IRP: claimed by 4.2's handshake and taken with its status, or - its
 * callback running - owed to the callback's return, the strongest trigger
 * kept (the table's order). A cancel routine already running keeps the
 * IRP. Controller lock held.
 */
static VOID hcdSuspIdleTriggerLocked(PHCD_DEVICE_PDO pdo, ULONG trigger,
                                     PHCD_SUSP_ITAKEN t)
{
    PIRP irp;
    ULONG st;

    irp = pdo->IdleSlot;
    if (irp == NULL) {
        return;
    }
    st = XhciSuspIdleSettle(trigger, pdo->IdleState, pdo->PwrState);
    if (st == XHCI_SUSP_ST_NONE) {
        return;
    }
    if (st == XHCI_SUSP_ST_PENDING) {
        if (pdo->IdleOwed == 0 || trigger < pdo->IdleOwed) {
            pdo->IdleOwed = trigger;
        }
        return;
    }
    if (!XhciSuspClaim(1, IoSetCancelRoutine(irp, NULL) != NULL)) {
        return;
    }
    pdo->IdleSlot = NULL;
    pdo->IdleState = XHCI_SUSP_IDLE_NONE;
    hcdSuspITake(t, pdo, irp, hcdSuspIdleStatus(st));
}

/*
 * The episode ends (6.5): no further callback of its batch runs - every
 * PDO's IdleBatchCur moves to 0, so a queued entry is skipped at its claim
 * - and, the link ACTIVE, the sixth trigger settles every RETURNED sibling
 * in D1 or D2. While a callback of the device runs, that settlement is its
 * return's (hcdSuspCbReturned), "after every RUNNING callback of the batch
 * has returned". The device's chain stable: the thread, or PdoListLock.
 * Controller lock held.
 */
static VOID hcdSuspIdleEndLocked(PHCD_USB_DEVICE dev, PHCD_SUSP_ITAKEN t)
{
    PHCD_DEVICE_PDO pdo;
    ULONG running;

    running = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        pdo->IdleBatchCur = 0;
        if (pdo->IdleState == XHCI_SUSP_IDLE_RUNNING) {
            running = 1;
        }
    }
    if (running || dev->Susp.State != XHCI_SUSP_ACTIVE) {
        return;
    }
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        hcdSuspIdleTriggerLocked(pdo, XHCI_SUSP_SET_EPISODE, t);
    }
}

/* A PDO's PwrState written D0 from a Dx (6.5's fifth trigger, "having been
 * Dx, reaches D0 at ACTIVE"): its idle IRP settled SUCCESS, and its own D0
 * ends the episode. The chain stable; controller lock held. */
static VOID hcdSuspIdleOwnD0Locked(PHCD_USB_DEVICE dev, PHCD_DEVICE_PDO pdo,
                                   PHCD_SUSP_ITAKEN t)
{
    hcdSuspIdleTriggerLocked(pdo, XHCI_SUSP_SET_D0, t);
    if (dev != NULL && XhciSuspEpisodeEnds(XHCI_SUSP_EPI_OWN_D0)) {
        hcdSuspIdleEndLocked(dev, t);
    }
}

/*
 * A PDO's own queued entry taken off the callback queue (teardown's (d),
 * the unlink), its reference to be let go after the hold; the batch left.
 * Controller lock held.
 */
static ULONG hcdSuspIdleDetachLocked(PHCD_DEVICE_PDO pdo)
{
    pdo->IdleBatchCur = 0;
    if (!pdo->IdleQueued) {
        return 0;
    }
    RemoveEntryList(&pdo->IdleEntry);
    pdo->IdleQueued = 0;
    pdo->IdleEntryOwned = 0;
    return 1;
}

/*
 * A batch (6.2): every started function PDO of the device holds an idle
 * IRP in HELD with PwrState D0, the link ACTIVE, every PDO in S0, the
 * controller up and its callback thread running. Each such PDO is queued
 * QUEUED with the new batch number, the epoch and an IdlePending reference
 * for its entry. Returns whether one was formed (the caller wakes the
 * callback thread). Thread only, controller lock held.
 */
static ULONG hcdSuspIdleBatchLocked(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PXHCI_SUSP_IDLE_FUNC f;
    PHCD_DEVICE_PDO pdo;
    ULONG s0;
    ULONG n;

    if (hc->SuspCb == NULL || hc->PowerDown) {
        return 0;
    }
    f = hc->SuspIdle;
    s0 = 1;
    n = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL && n < HCD_SUSP_MAX_FUNCS;
         pdo = pdo->Sibling) {
        if (pdo->Composite) {
            continue;
        }
        f[n].Started = pdo->PwrStarted;
        f[n].Closed = pdo->PwrClosed;
        f[n].IdleState = pdo->IdleState;
        f[n].PwrState = pdo->PwrState;
        if (pdo->Common.SystemPower != PowerSystemWorking) {
            s0 = 0;
        }
        n++;
    }
    if (!XhciSuspIdleBatchReady(f, n, dev->Susp.State, s0)) {
        return 0;
    }
    dev->IdleBatch++;
    if (dev->IdleBatch == 0) {
        dev->IdleBatch = 1;
    }
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        if (pdo->Composite || !pdo->PwrStarted || pdo->PwrClosed) {
            continue;
        }
        pdo->IdleState = XHCI_SUSP_IDLE_QUEUED;
        pdo->IdleBatchCur = dev->IdleBatch;
        pdo->IdleEntryBatch = dev->IdleBatch;
        pdo->IdleEntryEpoch = hc->PowerEpoch;
        pdo->IdleQueued = 1;
        pdo->IdleEntryOwned = 1;
        (VOID)InterlockedIncrement(&pdo->IdlePending);
        InsertTailList(&hc->IdleQueue, &pdo->IdleEntry);
    }
    hc->Counters.IdleBatches++;
    return 1;
}

/*
 * The callback thread's context (4.4): its own nonpaged allocation, apart
 * from the controller extension, so a thread that teardown abandoned in a
 * callback never reaches the controller again. Phase changes only by a
 * compare-and-move under the context's own spin lock (no compare-exchange
 * import is admitted; a spin lock gives the same single winner). Hc is
 * read only in a phase the thread won (CLAIMING, RETURNING). The running
 * callback's PDO, IRP, batch and the callback itself are the thread's. An
 * abandoned context goes on a driver-wide list that the unload waits out.
 */
typedef VOID (NTAPI *HCD_SUSP_IDLE_CALLBACK)(PVOID context);

typedef struct _HCD_SUSP_CB {
    KSPIN_LOCK Lock;
    ULONG Phase;                /* XHCI_SUSP_PH_*, under Lock            */
    PHCD_CONTROLLER Hc;
    KEVENT Wake;                /* synchronization: work, or EXITING     */
    KEVENT Exited;              /* notification: the thread leaves       */
    volatile LONG ExitFlag;     /* set just before PsTerminateSystemThread:
                                 * what a 9x join polls, since a wait there
                                 * may return unsatisfied (win98-wdm.md)  */
    PVOID ThreadObject;         /* NT: referenced; 9x: NULL (Exited)     */
    PHCD_DEVICE_PDO Pdo;
    PIRP Irp;
    ULONG Batch;
    HCD_SUSP_IDLE_CALLBACK Callback;
    PVOID Context;
    struct _HCD_SUSP_CB *Next;  /* the abandoned list                    */
} HCD_SUSP_CB, *PHCD_SUSP_CB;

/* The callback thread woken: a batch queued. IRQL: <= DISPATCH_LEVEL. */
static VOID hcdSuspCbKick(PHCD_CONTROLLER hc)
{
    if (hc->SuspCb != NULL) {
        (VOID)KeSetEvent(&hc->SuspCb->Wake, IO_NO_INCREMENT, FALSE);
    }
}

/* ------------------------------------------------------------------ */
/* The gate and the restart (3.2)                                      */
/* ------------------------------------------------------------------ */

static VOID hcdSuspGateClose(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    ULONG dci;
    ULONG mine;

    for (dci = 1; dci < 32; dci++) {
        pipe = hcdSuspPipe(dev, dci);
        if (pipe == NULL) {
            continue;
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        mine = pipe->SuspendPause ? 0UL : 1UL;
        pipe->SuspendPause = 1;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (mine) {
            HcdIoPipePause(hc, pipe);
        }
    }
}

/* Every endpoint the context reads Stopped, not Halted, with TDs on its
 * ring and no pause but the machine's, rung - each stream with work by
 * its Stream ID - then the gate opened. Under the lock the teardown's
 * freeze sets Gone under, as hcd_hub.c's release. */
static VOID hcdSuspGateOpen(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            ULONG restart)
{
    PHCD_PIPE pipe;
    PHCD_PIPE p;
    KIRQL oldIrql;
    ULONG state;
    ULONG dci;
    ULONG id;
    ULONG mine;

    for (dci = 1; dci < 32; dci++) {
        pipe = hcdSuspPipe(dev, dci);
        if (pipe == NULL) {
            continue;
        }
        state = restart ? hcdSuspEpState(hc, dev, dci)
                        : XHCI_EP_STATE_DISABLED;
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        mine = pipe->SuspendPause;
        if (mine && restart && !dev->Gone &&
            state == XHCI_EP_STATE_STOPPED && !pipe->Halted &&
            pipe->Paused == 1) {
            if (pipe->Queue->Count != 0) {
                pipe->SeqUsed = 1;
                XhciWriteDoorbell(&hc->Hc, dev->SlotId, dci);
            }
            for (id = 1;
                 pipe->Streams != NULL && id <= pipe->Streams->Count; id++) {
                p = pipe->Streams->Pipe[id];
                if (p != NULL && p->Queue->Count != 0) {
                    pipe->SeqUsed = 1;
                    XhciWriteDoorbell(&hc->Hc, dev->SlotId,
                                      XhciStreamDoorbell(dci, id));
                }
            }
        }
        pipe->SuspendPause = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (mine) {
            HcdIoPipeResume(hc, pipe);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The steps                                                           */
/* ------------------------------------------------------------------ */

/* One request through the thread's control path, its outcome as a step
 * result (3.10). */
static ULONG hcdSuspRequest(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            UCHAR type, UCHAR request, USHORT value,
                            USHORT index)
{
    ULONG bytes;
    ULONG outcome;

    outcome = HcdThreadControlOutcome(hc, dev, type, request, value, index,
                                      0, &bytes);
    switch (outcome) {
    case HCD_CTL_DONE:
        return XHCI_SUSP_RES_OK;
    case HCD_CTL_STALLED:
        return XHCI_SUSP_RES_STALL;
    case HCD_CTL_ABANDONED:
        return XHCI_SUSP_RES_TIMEOUT;
    default:
        break;
    }
    if (dev->Ep0Stuck || hcdSuspHalted(hc)) {
        return XHCI_SUSP_RES_TIMEOUT;
    }
    return (outcome == HCD_CTL_NOT_SENT) ? XHCI_SUSP_RES_NOT_SENT
                                         : XHCI_SUSP_RES_FAILED;
}

/* SET/CLEAR_FEATURE(DEVICE_REMOTE_WAKEUP), stage A's arm and disarm. */
static ULONG hcdSuspControl(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            UCHAR request)
{
    return hcdSuspRequest(hc, dev, HCD_SUSP_RT_DEVICE_OUT, request,
                          HCD_SUSP_FEAT_REMOTE_WAKEUP, 0);
}

/* EP0 busy for the arm: a client's transfer queued, or a stall whose
 * recovery (hcdEp0Quiet's Reset Endpoint) would run past the arm's bound -
 * that recovery is the next pass's, the device ACTIVE again after the
 * refusal (Codex review of 36.30, round 2, finding 3). */
static ULONG hcdSuspEp0Queued(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;
    ULONG busy;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    busy = (dev->Ep0Queue.Count != 0 || dev->Ep0Halted) ? 1UL : 0UL;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return busy;
}

/* The arm's EP0 (3.4 (2)): a first attempt is not sent behind a client's
 * transfer; a repeated one waits for EP0's queue to drain within `bound`
 * milliseconds (3.9). OK when the arm may be sent, else NOT_SENT. */
static ULONG hcdSuspArmDrain(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                             ULONG bound)
{
    KTIMER deadline;
    LARGE_INTEGER due;
    LARGE_INTEGER now;
    ULONG drained;

    if (hcdSuspEp0Queued(hc, dev) != 0 && bound == 0) {
        return XHCI_SUSP_RES_NOT_SENT;
    }
    /* An elapsed-time bound, a timer polled with a zero wait, as
     * hcdEp0Quiet's (Codex review of 36.30, round 1, finding 6). */
    KeInitializeTimer(&deadline);
    HcdRelativeMs(&due, bound);
    (VOID)KeSetTimer(&deadline, due, NULL);
    now.QuadPart = 0;
    drained = 0;
    for (;;) {
        if (hcdSuspEp0Queued(hc, dev) == 0) {
            drained = 1;
            break;
        }
        if (KeWaitForSingleObject(&deadline, Executive, KernelMode, FALSE,
                                  &now) == STATUS_SUCCESS) {
            break;
        }
        hcdSuspPause(1);
        HcdIoDeferred(hc);
    }
    (VOID)KeCancelTimer(&deadline);
    return drained ? XHCI_SUSP_RES_OK : XHCI_SUSP_RES_NOT_SENT;
}

/* Stage A's arm: DEVICE_REMOTE_WAKEUP. */
static ULONG hcdSuspArm(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG bound)
{
    ULONG result;

    result = hcdSuspArmDrain(hc, dev, bound);
    if (result != XHCI_SUSP_RES_OK) {
        return result;
    }
    /* EP0 drained and its gate closed, so the request's own quiet wait
     * finds nothing to wait for. */
    return hcdSuspControl(hc, dev, HCD_SUSP_REQ_SET_FEATURE);
}

/*
 * After the stop, an endpoint whose context's TR Dequeue Pointer has moved
 * past a TD this stop's own Stopped event shows untouched (hcd_dev.c,
 * hcdDevStopUntouched: a bulk or interrupt TD, the event at its first TRB
 * with nothing moved) is rewound to the ring's software dequeue (that TD's
 * first TRB) by Set TR Dequeue Pointer (36.35's legs on 2000 and XP: an
 * interrupt IN read pending across a suspend never completed after the
 * wake or the host resume, while a read posted after it did). The xHCI
 * stop keeps the stopped TRB as the dequeue (4.6.9), so on such a
 * controller the two agree and nothing is sent; QEMU's stop drops a TD in
 * flight and leaves the dequeue past it ("data might be lost"), so the
 * restart's doorbell would find no TD. Without the evidence - control,
 * isochronous, a TD already measured, an event elsewhere or none - the
 * endpoint is left as the controller has it (Codex review of the rewind,
 * round 1, finding 1). Returns 0 when a rewind was due and refused: the
 * TD would be stranded again, so the stop reports the uncertain outcome
 * and the device is cycled (finding 2). Thread only.
 */
static ULONG hcdSuspRewind(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG dci)
{
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    ULONG offset;
    ULONG ctxPa;
    ULONG rewind;

    pipe = hcdSuspPipe(dev, dci);
    if (pipe == NULL) {
        return 1;
    }
    rewind = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (pipe->SuspStopUntouched && pipe->Streams == NULL && dci != 1 &&
        pipe->Queue->Count != 0 && pipe->Queue->Head != NULL &&
        XhciEndpointContextOffset(&hc->Hc.Layout, dev->SlotId, dci,
                                  &offset) == XHCI_LAYOUT_OK) {
        /* Endpoint Context dword 2: TR Dequeue Pointer low, DCS in bit 0. */
        ctxPa = XhciCommonAt(&hc->Hc, offset)[2] & ~0xFUL;
        rewind = (ctxPa != XhciRingDequeuePA(pipe->Ring)) ? 1UL : 0UL;
    }
    pipe->SuspStopWatch = 0;
    pipe->SuspStopUntouched = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (!rewind) {
        return 1;
    }
    XHCI_DBG_VALUE("hcd: suspend stop rewound, slot/dci",
                   (dev->SlotId << 8) | dci);
    if (!HcdCfgRewind(hc, dev, pipe)) {
        XHCI_DBG_VALUE("hcd: suspend stop rewind refused, slot/dci",
                       (dev->SlotId << 8) | dci);
        return 0;
    }
    return 1;
}

/* The pipe's Stopped event watched for while its stop is out, and the
 * watch ended. Thread only. */
static VOID hcdSuspStopWatch(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                             ULONG dci)
{
    PHCD_PIPE pipe;
    KIRQL oldIrql;

    pipe = hcdSuspPipe(dev, dci);
    if (pipe == NULL) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe->SuspStopWatch = 1;
    pipe->SuspStopUntouched = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

static VOID hcdSuspStopUnwatch(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                               ULONG dci)
{
    PHCD_PIPE pipe;
    KIRQL oldIrql;

    pipe = hcdSuspPipe(dev, dci);
    if (pipe == NULL) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe->SuspStopWatch = 0;
    pipe->SuspStopUntouched = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* SetFeature(FUNCTION_SUSPEND) to first interface `iface` with `options`
 * (8.1), logged. */
static ULONG hcdSuspFuncSuspend(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                ULONG iface, ULONG options)
{
    ULONG result;

    result = hcdSuspRequest(hc, dev, HCD_SUSP_RT_IFACE_OUT,
                            HCD_SUSP_REQ_SET_FEATURE,
                            HCD_SUSP_FEAT_FUNC_SUSPEND,
                            (USHORT)(iface | (options << 8)));
    hcdSuspNote(hc, (options != HCD_SUSP_FS_DISARM) ? "susp.ss.arm"
                                                    : "susp.ss.disarm",
                ((dev->SlotId & 0xFFUL) << 24) | (iface << 16) | result);
    return result;
}

/*
 * Stage B's arm (8.4 step 3): EP0 as stage A's arm, then for each PDO the
 * decision marked ArmNow, each of its wake functions not yet armed, in
 * ascending interface order: SetFeature(FUNCTION_SUSPEND) with options
 * 03h. Each the device takes sets its bit in the PDO's FwArmed; the first
 * not taken ends the step with its result. The notification window opens
 * in a hold just before the first request (8.7). Nothing to send is OK:
 * the report of FwArmed (XhciSuspSsArmed) then leaves it unarmed.
 */
static ULONG hcdSuspSsArm(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                          ULONG bound)
{
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;
    ULONG result;
    ULONG want;
    ULONG iface;
    ULONG opened;

    result = hcdSuspArmDrain(hc, dev, bound);
    if (result != XHCI_SUSP_RES_OK) {
        return result;
    }
    opened = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        if (!pdo->ArmNow) {
            continue;
        }
        want = dev->FwCapable & dev->FwFirsts & hcdSuspOwn(pdo) &
               ~pdo->FwArmed;
        for (iface = 0; iface < 32; iface++) {
            if ((want & (1UL << iface)) == 0) {
                continue;
            }
            if (!opened) {
                XhciControllerLockAcquire(&hc->Hc, &oldIrql);
                XhciSuspFwOpen(&dev->Susp);
                XhciControllerLockRelease(&hc->Hc, oldIrql);
                opened = 1;
            }
            result = hcdSuspFuncSuspend(hc, dev, iface, HCD_SUSP_FS_ARM);
            if (result != XHCI_SUSP_RES_OK) {
                hc->Counters.SsSuspArmRefused++;
                /* A request that reached the device and failed otherwise
                 * than by STALL (a status stage's transaction error among
                 * them) may have been applied: the function may be in
                 * Function Suspend with no FwArmed bit to disarm it by, so
                 * it is uncertain and the device is cycled. STALL and
                 * NOT_SENT stay refusals (Codex review of 36.38, round 1,
                 * finding 1). */
                return (result == XHCI_SUSP_RES_FAILED)
                           ? XHCI_SUSP_RES_TIMEOUT
                           : result;
            }
            pdo->FwArmed |= 1UL << iface;
            hc->Counters.SsSuspArms++;
        }
    }
    return XHCI_SUSP_RES_OK;
}

/*
 * Stage B's disarm (8.5): for every PDO with FwArmed, each armed function,
 * SetFeature(FUNCTION_SUSPEND) with options 0; a request the device takes
 * clears its bit. The first not taken ends the step: strict, the machine
 * makes it uncertain and the device is cycled.
 */
static ULONG hcdSuspSsDisarm(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO pdo;
    ULONG result;
    ULONG iface;

    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        for (iface = 0; iface < 32; iface++) {
            if ((pdo->FwArmed & (1UL << iface)) == 0) {
                continue;
            }
            result = hcdSuspFuncSuspend(hc, dev, iface, HCD_SUSP_FS_DISARM);
            if (result != XHCI_SUSP_RES_OK) {
                hc->Counters.SsSuspDisarmFailed++;
                return result;
            }
            pdo->FwArmed &= ~(1UL << iface);
            hc->Counters.SsSuspDisarms++;
        }
    }
    return XHCI_SUSP_RES_OK;
}

/* Stop Endpoint with SP=1 on every endpoint from DCI `first` the context
 * reads Running (3.4 (3)): 1 for every one, 2 for stage B's STOP_FUNCS,
 * every one but EP0 (8.4); each rewound where its stop lost a TD
 * (hcdSuspRewind). */
static ULONG hcdSuspStop(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG first)
{
    XHCI_TRB trb;
    ULONG control;
    ULONG code;
    ULONG dci;

    for (dci = first; dci < 32; dci++) {
        if (hcdSuspPipe(dev, dci) == NULL ||
            hcdSuspEpState(hc, dev, dci) != XHCI_EP_STATE_RUNNING) {
            continue;
        }
        code = 0;
        hcdSuspStopWatch(hc, dev, dci);
        if (XhciTrbStopEndpoint(&trb, dev->SlotId, dci, 1) == XHCI_RING_OK) {
            code = HcdThreadCommand(hc, &trb, &control);
        }
        if (code == XHCI_CC_SUCCESS) {
            if (!hcdSuspRewind(hc, dev, dci)) {
                return XHCI_SUSP_RES_TIMEOUT;
            }
            continue;
        }
        hcdSuspStopUnwatch(hc, dev, dci);
        if (code == XHCI_CC_CONTEXT_STATE_ERROR) {
            continue;
        }
        return hcdSuspHalted(hc) ? XHCI_SUSP_RES_TIMEOUT
                                 : XHCI_SUSP_RES_FAILED;
    }
    HcdIoDeferred(hc);
    return XHCI_SUSP_RES_OK;
}

/* A hub port request's failure as a step result. */
static ULONG hcdSuspHubFailed(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    if (hcdSuspHalted(hc) || hub->Device == NULL || hub->Device->Ep0Stuck) {
        return XHCI_SUSP_RES_TIMEOUT;
    }
    return XHCI_SUSP_RES_FAILED;
}

/* Stage B's window opened just before the U3 request when no arm request
 * opened it (8.7): after the stale change is acknowledged, so nothing of an
 * earlier suspension read meanwhile can be latched (Codex review of 36.38,
 * round 2, finding 1). */
static VOID hcdSuspFwOpenNow(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    XhciSuspFwOpen(&dev->Susp);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* The U3 request (3.4 (4)), its stale change acknowledged first. */
static ULONG hcdSuspU3(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                       PHCD_PORT q)
{
    ULONG portsc;
    ULONG status;
    ULONG change;

    if (q->Hub != NULL && dev->Susp.Ss) {
        /* A SuperSpeed hub port (8.6): a stale C_PORT_LINK_STATE cleared,
         * then SetPortFeature(PORT_LINK_STATE) with U3 in wIndex 15:8. */
        if (!HcdHubPortStatus(hc, q->Hub, q->Number, &status, &change)) {
            return hcdSuspHubFailed(hc, q->Hub);
        }
        if ((change & XHCI_SSHUB_C_PORT_LINK_STATE) != 0 &&
            !HcdHubPortFeature(hc, q->Hub, q->Number, 0,
                               XHCI_SSHUB_FEAT_C_PORT_LINK_STATE)) {
            return hcdSuspHubFailed(hc, q->Hub);
        }
        hcdSuspFwOpenNow(hc, dev);
        XhciSuspU3Issued(&dev->Susp);
        if (!HcdHubPortFeature(hc, q->Hub,
                               q->Number | (XHCI_SSHUB_LINK_U3 << 8), 1,
                               XHCI_SSHUB_FEAT_PORT_LINK_STATE)) {
            /* May have taken effect: uncertain, as stage A's. */
            return XHCI_SUSP_RES_TIMEOUT;
        }
        return XHCI_SUSP_RES_OK;
    }
    if (q->Hub == NULL) {
        portsc = XhciReadPortsc(&hc->Hc, q->PortId);
        if (portsc == 0xFFFFFFFFUL) {
            return XHCI_SUSP_RES_TIMEOUT;
        }
        if ((portsc & XHCI_PORTSC_PLC) != 0) {
            XhciWritePortsc(&hc->Hc, q->PortId,
                            XhciPortscClearChanges(portsc, XHCI_PORTSC_PLC));
            portsc = XhciReadPortsc(&hc->Hc, q->PortId);
        }
        if (dev->Susp.Ss) {
            hcdSuspFwOpenNow(hc, dev);
        }
        XhciWritePortsc(&hc->Hc, q->PortId, XhciPortscSuspend(portsc));
        XhciSuspU3Issued(&dev->Susp);
        return XHCI_SUSP_RES_OK;
    }
    if (!HcdHubPortStatus(hc, q->Hub, q->Number, &status, &change)) {
        return hcdSuspHubFailed(hc, q->Hub);
    }
    if ((change & XHCI_HUB_C_PORT_SUSPEND) != 0 &&
        !HcdHubPortFeature(hc, q->Hub, q->Number, 0,
                           XHCI_HUB_FEAT_C_PORT_SUSPEND)) {
        return hcdSuspHubFailed(hc, q->Hub);
    }
    XhciSuspU3Issued(&dev->Susp);
    if (!HcdHubPortFeature(hc, q->Hub, q->Number, 1,
                           XHCI_HUB_FEAT_PORT_SUSPEND)) {
        /* The request may have taken effect though its transfer failed:
         * nothing proves the link still U0, so it is uncertain, never a
         * refusal that would reopen the gate (Codex review of 36.30,
         * round 2, finding 1). */
        return XHCI_SUSP_RES_TIMEOUT;
    }
    return XHCI_SUSP_RES_OK;
}

/* The observation (3.4 (5)) or the verification (3.6), polled within
 * `bound` milliseconds by an elapsed-time timer. */
static ULONG hcdSuspPoll(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                         PHCD_PORT q, ULONG verify, ULONG bound)
{
    KTIMER deadline;
    LARGE_INTEGER due;
    LARGE_INTEGER now;
    KIRQL oldIrql;
    ULONG answer;
    ULONG portsc;
    ULONG status;
    ULONG change;
    ULONG bits;
    ULONG beyond;

    KeInitializeTimer(&deadline);
    HcdRelativeMs(&due, bound);
    (VOID)KeSetTimer(&deadline, due, NULL);
    now.QuadPart = 0;
    for (;;) {
        if (q->Hub == NULL) {
            portsc = XhciReadPortsc(&hc->Hc, q->PortId);
            if (portsc == 0xFFFFFFFFUL) {
                answer = XHCI_SUSP_RES_TIMEOUT;
                break;
            }
            answer = verify ? XhciSuspVerifyRoot(portsc)
                            : XhciSuspObserveRoot(portsc);
        } else if (dev->Susp.Ss) {
            /* A SuperSpeed hub port (8.6): a C_PORT_LINK_STATE read is
             * cleared, its clear failing a timeout; a wake is read only
             * from the latch, emptied at each poll of the observation
             * (8.7 (b)), a disconnect ahead of it. */
            if (!HcdHubPortStatus(hc, q->Hub, q->Number, &status, &change)) {
                answer = XHCI_SUSP_RES_TIMEOUT;
                break;
            }
            if ((change & XHCI_SSHUB_C_PORT_LINK_STATE) != 0 &&
                !HcdHubPortFeature(hc, q->Hub, q->Number, 0,
                                   XHCI_SSHUB_FEAT_C_PORT_LINK_STATE)) {
                answer = XHCI_SUSP_RES_TIMEOUT;
                break;
            }
            answer = verify ? XhciSuspVerifySsHub(status, change)
                            : XhciSuspObserveSsHub(status, change);
            if (!verify && answer != XHCI_SUSP_RES_DISCONNECT) {
                XhciControllerLockAcquire(&hc->Hc, &oldIrql);
                if (XhciSuspFwTake(&dev->Susp, &bits, &beyond)) {
                    hc->Counters.FuncWakeInSequence +=
                        hcdSuspFwNote(hc, dev, bits, beyond,
                                      HCD_SUSP_FW_OBSERVE);
                    answer = XHCI_SUSP_RES_WAKE;
                }
                XhciControllerLockRelease(&hc->Hc, oldIrql);
            }
            if (answer != XHCI_SUSP_POLL_AGAIN) {
                hcdSuspNote(hc, "susp.ss.hub",
                            ((q->Hub->Index & 0xFFUL) << 24) |
                                ((q->Number & 0xFFUL) << 16) |
                                ((XhciSsHubLinkState(status) & 0xFUL) << 12) |
                                ((verify ? XHCI_SUSP_STEP_VERIFY
                                         : XHCI_SUSP_STEP_OBSERVE)
                                 << 4) |
                                (answer & 0xFUL));
                if (answer == XHCI_SUSP_RES_OK) {
                    if (verify) {
                        hc->Counters.SsSuspHubResumes++;
                    } else {
                        hc->Counters.SsSuspHubU3++;
                    }
                }
            }
        } else {
            if (!HcdHubPortStatus(hc, q->Hub, q->Number, &status, &change)) {
                answer = hcdSuspHubFailed(hc, q->Hub);
                if (answer == XHCI_SUSP_RES_FAILED) {
                    answer = XHCI_SUSP_RES_TIMEOUT;
                }
                break;
            }
            if ((change & XHCI_HUB_C_PORT_SUSPEND) != 0 &&
                !HcdHubPortFeature(hc, q->Hub, q->Number, 0,
                                   XHCI_HUB_FEAT_C_PORT_SUSPEND)) {
                /* A transport failure stays uncertain whatever the sample
                 * said (round 1, finding 7). */
                answer = XHCI_SUSP_RES_TIMEOUT;
                break;
            }
            answer = verify ? XhciSuspVerifyHub(status, change)
                            : XhciSuspObserveHub(status, change);
        }
        if (answer != XHCI_SUSP_POLL_AGAIN) {
            break;
        }
        if (KeWaitForSingleObject(&deadline, Executive, KernelMode, FALSE,
                                  &now) == STATUS_SUCCESS) {
            answer = XHCI_SUSP_RES_EXPIRED;
            break;
        }
        hcdSuspPause((q->Hub == NULL) ? 1UL : HCD_SUSP_HUB_POLL_MS);
    }
    (VOID)KeCancelTimer(&deadline);
    return answer;
}

/* The resume's signal (3.6): PLS=Resume at a root port, or
 * ClearPortFeature(PORT_SUSPEND) at a hub port. */
static ULONG hcdSuspSignal(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           PHCD_PORT q)
{
    XHCI_LINK_ACTION act;
    ULONG portsc;

    if (dev->Susp.Ss) {
        /* 8.5, 8.6: the one U0 write. At a root port for a link in U3 (a
         * host resume) or in Resume (a device's exit the xHC holds); a port
         * in neither is left to VERIFY, which reads U0 or not. At a
         * SuperSpeed hub port SetPortFeature(PORT_LINK_STATE) with U0,
         * which the hub ignores on a link already in U0. */
        if (q->Hub == NULL) {
            portsc = XhciReadPortsc(&hc->Hc, q->PortId);
            if (portsc == 0xFFFFFFFFUL) {
                return XHCI_SUSP_RES_TIMEOUT;
            }
            if (XhciLinkDecide(&q->Link, portsc, XHCI_LINK_WANT_RESUME,
                               &act) == XHCI_LINK_ACT_SET_U0) {
                XhciWritePortsc(&hc->Hc, q->PortId,
                                XhciPortscResumeDone(portsc));
            }
            return XHCI_SUSP_RES_OK;
        }
        if (!HcdHubPortFeature(hc, q->Hub,
                               q->Number | (XHCI_SSHUB_LINK_U0 << 8), 1,
                               XHCI_SSHUB_FEAT_PORT_LINK_STATE)) {
            return hcdSuspHubFailed(hc, q->Hub);
        }
        hcdSuspNote(hc, "susp.ss.hub",
                    ((q->Hub->Index & 0xFFUL) << 24) |
                        ((q->Number & 0xFFUL) << 16) |
                        (XHCI_SUSP_STEP_SIGNAL << 4));
        return XHCI_SUSP_RES_OK;
    }
    if (q->Hub == NULL) {
        portsc = XhciReadPortsc(&hc->Hc, q->PortId);
        if (portsc == 0xFFFFFFFFUL) {
            return XHCI_SUSP_RES_TIMEOUT;
        }
        XhciWritePortsc(&hc->Hc, q->PortId, XhciPortscResumeSignal(portsc));
        return XHCI_SUSP_RES_OK;
    }
    if (!HcdHubPortFeature(hc, q->Hub, q->Number, 0,
                           XHCI_HUB_FEAT_PORT_SUSPEND)) {
        return hcdSuspHubFailed(hc, q->Hub);
    }
    return XHCI_SUSP_RES_OK;
}

static ULONG hcdSuspU0(PHCD_CONTROLLER hc, PHCD_PORT q)
{
    ULONG portsc;

    portsc = XhciReadPortsc(&hc->Hc, q->PortId);
    if (portsc == 0xFFFFFFFFUL) {
        return XHCI_SUSP_RES_TIMEOUT;
    }
    XhciWritePortsc(&hc->Hc, q->PortId, XhciPortscResumeDone(portsc));
    return XHCI_SUSP_RES_OK;
}

/* ------------------------------------------------------------------ */
/* A sequence                                                          */
/* ------------------------------------------------------------------ */

/* The sequence's end, in the hold that makes its state visible: a STALL's
 * WAIT_WAKEs claimed, then the refusal's latch (XhciSuspLatch's
 * contract), then a due wake claim; a refusal or an uncertain outcome that
 * ended ACTIVE ends the idle episode (6.5). Controller lock held. */
static VOID hcdSuspEndLocked(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                             PHCD_SUSP_TAKEN t, PHCD_SUSP_ITAKEN it)
{
    ULONG claimed;
    ULONG bits;
    ULONG beyond;
    ULONG n;

    /* 8.4, 8.7 (c): what the window latched, first. A refusal's latch is
     * the wake's claim as 5.4's, ahead of a STALL's claim and the latch;
     * any other end that is not SUSPENDED drops it. */
    if (dev->Susp.State != XHCI_SUSP_SUSPENDED &&
        XhciSuspFwTake(&dev->Susp, &bits, &beyond)) {
        if (dev->Susp.State == XHCI_SUSP_ACTIVE &&
            dev->Susp.Outcome == XHCI_SUSP_OUT_REFUSED) {
            hc->Counters.FuncWakeInSequence +=
                hcdSuspFwNote(hc, dev, bits, beyond, HCD_SUSP_FW_REFUSAL);
            claimed = hcdSuspClaimWakes(dev, 1, hc->WakeSeq, t);
            hc->Counters.SuspWakeEvents++;
            if (claimed == 0) {
                hc->Counters.SuspWakeUnclaimed++;
            }
            hc->Counters.SuspWaitWakesCompleted += claimed;
        } else {
            hc->Counters.FuncWakeIgnored +=
                hcdSuspFwNote(hc, dev, bits, beyond, HCD_SUSP_FW_DROPPED);
        }
    }
    hcdSuspFwDrain(hc, dev);
    if (dev->Susp.State == XHCI_SUSP_ACTIVE &&
        dev->Susp.Outcome == XHCI_SUSP_OUT_REFUSED &&
        dev->Susp.Reason == XHCI_SUSP_WHY_STALL) {
        (VOID)hcdSuspClaimWakes(dev, 0, 0, t);
    }
    if (dev->Susp.LatchDue) {
        n = hcdSuspRead(dev, hc->SuspPdos, hc->SuspFuncs);
        XhciSuspLatch(&dev->Susp, hc->SuspFuncs, n);
    }
    hcdSuspClaimDue(hc, dev, t);
    if (dev->Susp.State == XHCI_SUSP_ACTIVE &&
        (dev->Susp.Outcome == XHCI_SUSP_OUT_REFUSED ||
         dev->Susp.Outcome == XHCI_SUSP_OUT_UNCERTAIN) &&
        XhciSuspEpisodeEnds(XHCI_SUSP_EPI_REFUSED)) {
        hcdSuspIdleEndLocked(dev, it);
    }
}

/*
 * Run the sequence whose first step `step` (with its wait `wait`) the
 * caller began from link state `old` (read before XhciSuspBegin, for the
 * log line), to its end. Returns the outcome. The IRPs a claim takes
 * are completed between steps, outside the lock.
 */
static ULONG hcdSuspRun(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG step,
                        ULONG wait, ULONG old)
{
    HCD_SUSP_TAKEN taken;
    HCD_SUSP_ITAKEN itaken;
    PHCD_PORT q;
    KIRQL oldIrql;
    ULONG result;
    ULONG group;

    taken.Count = 0;
    itaken.Count = 0;
    q = hcdSuspPort(hc, dev);
    while (step != XHCI_SUSP_STEP_END) {
        result = XHCI_SUSP_RES_TIMEOUT;
        if (q == NULL || hcdSuspHalted(hc)) {
            result = XHCI_SUSP_RES_TIMEOUT;
        } else {
            switch (step) {
            case XHCI_SUSP_STEP_GATE:
                hcdSuspGateClose(hc, dev);
                result = XHCI_SUSP_RES_OK;
                break;
            case XHCI_SUSP_STEP_STOP_FUNCS:
                result = hcdSuspStop(hc, dev, 2);
                break;
            case XHCI_SUSP_STEP_ARM:
                if (dev->Susp.Ss) {
                    result = hcdSuspSsArm(hc, dev, wait);
                    break;
                }
                result = hcdSuspArm(hc, dev, wait);
                if (result == XHCI_SUSP_RES_OK) {
                    hc->Counters.SuspArms++;
                }
                break;
            case XHCI_SUSP_STEP_STOP:
                result = hcdSuspStop(hc, dev, 1);
                break;
            case XHCI_SUSP_STEP_U3:
                result = hcdSuspU3(hc, dev, q);
                break;
            case XHCI_SUSP_STEP_OBSERVE:
                result = hcdSuspPoll(hc, dev, q, 0, wait);
                break;
            case XHCI_SUSP_STEP_SIGNAL:
                hcdSuspHold(hc, wait);
                result = hcdSuspSignal(hc, dev, q);
                break;
            case XHCI_SUSP_STEP_U0:
                hcdSuspHold(hc, wait);
                result = hcdSuspU0(hc, q);
                break;
            case XHCI_SUSP_STEP_VERIFY:
                result = hcdSuspPoll(hc, dev, q, 1, wait);
                break;
            case XHCI_SUSP_STEP_RECOVERY:
                hcdSuspHold(hc, wait);
                result = XHCI_SUSP_RES_OK;
                break;
            case XHCI_SUSP_STEP_DISARM:
                if (dev->Susp.Ss) {
                    /* The refusal's disarm waits out SP=1's minimum first,
                     * since it rings EP0 (8.4); the resume's wait is 0. */
                    hcdSuspHold(hc, wait);
                    result = hcdSuspSsDisarm(hc, dev);
                    break;
                }
                result = hcdSuspControl(hc, dev, HCD_SUSP_REQ_CLEAR_FEATURE);
                if (result == XHCI_SUSP_RES_OK) {
                    hc->Counters.SuspDisarms++;
                }
                break;
            case XHCI_SUSP_STEP_RESTART:
                hcdSuspHold(hc, wait);
                hcdSuspGateOpen(hc, dev, 1);
                result = XHCI_SUSP_RES_OK;
                break;
            default:
                break;
            }
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (dev->Susp.Ss) {
            /* 8.4: at SuperSpeed Armed is the PDOs' FwArmed, reported
             * before the step's result, whatever the step. */
            XhciSuspSsArmed(&dev->Susp, hcdSuspArmedAll(dev) != 0);
        }
        step = XhciSuspAdvance(&dev->Susp, result, hc->SuspClockMs, &wait);
        if (step == XHCI_SUSP_STEP_END) {
            hcdSuspEndLocked(hc, dev, &taken, &itaken);
        } else {
            hcdSuspClaimDue(hc, dev, &taken);
            hcdSuspFwDrain(hc, dev);
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        hcdSuspFinishT(hc, &taken);
        hcdSuspIFinishT(hc, &itaken);
    }
    XHCI_DBG_VALUE("hcd: suspend sequence, slot/kind/outcome/state",
                   (dev->SlotId << 24) | (dev->Susp.Kind << 16) |
                       (dev->Susp.Outcome << 8) | dev->Susp.State);
    switch (dev->Susp.Outcome) {
    case XHCI_SUSP_OUT_SUSPENDED:
        hc->Counters.SuspSuspended++;
        break;
    case XHCI_SUSP_OUT_ACTIVE:
        hc->Counters.SuspActive++;
        break;
    case XHCI_SUSP_OUT_REFUSED:
        hc->Counters.SuspRefused++;
        hc->Counters.SuspRefusedReason = dev->Susp.Reason;
        break;
    case XHCI_SUSP_OUT_UNCERTAIN:
        hc->Counters.SuspUncertain++;
        break;
    case XHCI_SUSP_OUT_GONE:
        hc->Counters.SuspGone++;
        break;
    default:
        break;
    }
    /* 36.33's line: the PDO group, the old and new link state, the kind,
     * the outcome and the reason, then the controller's epoch. */
    hcdSuspNote(hc, "susp.seq",
                ((hcdSuspPdo(dev->Pdo) != NULL
                      ? hcdSuspPdo(dev->Pdo)->Group & 0xFFFUL
                      : 0UL) << 20) |
                    ((old & 0xFUL) << 16) | ((dev->Susp.State & 0xFUL) << 12) |
                    ((dev->Susp.Kind & 0xFUL) << 8) |
                    ((dev->Susp.Outcome & 0xFUL) << 4) |
                    (dev->Susp.Reason & 0xFUL));
    hcdSuspNote(hc, "susp.seq.epoch", hc->PowerEpoch);
    switch (dev->Susp.State) {
    case XHCI_SUSP_ACTIVE:
        HcdCfgUndefer(hc, dev);
        break;
    case XHCI_SUSP_RECOVERING:
    case XHCI_SUSP_GONE:
        if (!hcdSuspHalted(hc)) {
            /* The device cycle re-establishes it, or takes a device the
             * machine saw leave off its port when the port's own
             * processing would not (a PED loss with CCS kept): either way
             * the old device is dropped and its slots settled at the
             * unlink (3.10; round 1, finding 3). Named by its PDO group,
             * so a replacement is not touched. */
            group = hcdSuspPdo(dev->Pdo) != NULL
                        ? hcdSuspPdo(dev->Pdo)->Group
                        : 0;
            HcdEnumCycle(hc, dev->Location, group);
        }
        break;
    default:
        break;
    }
    return dev->Susp.Outcome;
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

/*
 * One decision for one device, and what it begins, in one hold (3.9):
 * the slots copied, decided, written back, the IRPs to complete claimed,
 * and a suspend begun. Returns whether a sequence ran.
 */
static ULONG hcdSuspDecideOne(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO *pdos;
    PXHCI_SUSP_FUNC funcs;
    XHCI_SUSP_DECISION d;
    HCD_SUSP_TAKEN taken;
    HCD_SUSP_ITAKEN itaken;
    PHCD_PORT q;
    KIRQL oldIrql;
    ULONG step;
    ULONG wait;
    ULONG kind;
    ULONG batch;
    ULONG old;
    ULONG bits;
    ULONG beyond;
    ULONG n;
    ULONG i;

    pdos = hc->SuspPdos;
    funcs = hc->SuspFuncs;
    taken.Count = 0;
    itaken.Count = 0;
    batch = 0;
    step = XHCI_SUSP_STEP_END;
    wait = 0;
    kind = 0;
    q = hcdSuspPort(hc, dev);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->Susp.Ss = hcdSuspSs(dev);
    /* 8.7 (a): the latch first, before the slots are read. Bits for a
     * SUSPENDED device are its wake event, claimed in this hold, and the
     * wake begins below in the same hold, so a D0 that arrived meanwhile
     * cannot begin a resume that would close the window over it. */
    if (XhciSuspFwTake(&dev->Susp, &bits, &beyond)) {
        if (dev->Susp.State == XHCI_SUSP_SUSPENDED &&
            XhciSuspEvidence(&dev->Susp, XHCI_SUSP_EV_WAKE)) {
            hc->Counters.FuncWakeAsEvent +=
                hcdSuspFwNote(hc, dev, bits, beyond, HCD_SUSP_FW_EVENT);
            hcdSuspClaimDue(hc, dev, &taken);
            dev->SuspWakeDue = 1;
        } else {
            hc->Counters.FuncWakeIgnored +=
                hcdSuspFwNote(hc, dev, bits, beyond, HCD_SUSP_FW_DROPPED);
        }
    }
    hcdSuspFwDrain(hc, dev);
    if (dev->SuspWakeDue && dev->Susp.State == XHCI_SUSP_SUSPENDED) {
        /* A wake event taken at the acknowledgement or the hub look. */
        dev->SuspWakeDue = 0;
        kind = XHCI_SUSP_RUN_WAKE;
        hc->Counters.SuspWakesBegun++;
        old = dev->Susp.State;
        step = XhciSuspBegin(&dev->Susp, kind, 0, 0, hc->SuspClockMs, &wait);
        hcdSuspFwDrain(hc, dev);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        hcdSuspFinishT(hc, &taken);
        (VOID)hcdSuspRun(hc, dev, step, wait, old);
        return 1;
    }
    dev->SuspWakeDue = 0;
    n = hcdSuspRead(dev, pdos, funcs);
    (VOID)XhciSuspDecide(&dev->Susp, funcs, n, hc->PowerEpoch,
                         hc->PowerDown, &d);
    for (i = 0; i < n; i++) {
        pdos[i]->WokeHold = funcs[i].WokeHold;
        pdos[i]->ArmNow = funcs[i].ArmNow;
        if (funcs[i].ToD0) {
            if (pdos[i]->PwrState != XHCI_SUSP_D0) {
                /* 6.5: having been Dx, D0 at ACTIVE. */
                hcdSuspIdleOwnD0Locked(dev, pdos[i], &itaken);
            }
            pdos[i]->PwrState = XHCI_SUSP_D0;
            if (!funcs[i].CompleteD0) {
                /* A START's D0, with no IRP to carry its report. */
                hcdSuspTake(&taken, pdos[i], NULL, HCD_SUSP_TAKE_REPORT);
            }
        }
        if ((funcs[i].CompleteDx || funcs[i].CompleteD0) &&
            pdos[i]->PwrIrp != NULL) {
            hcdSuspTake(&taken, pdos[i], pdos[i]->PwrIrp,
                        funcs[i].CompleteD0 ? HCD_SUSP_TAKE_D0
                                            : HCD_SUSP_TAKE_DX);
            pdos[i]->PwrIrp = NULL;
        }
    }
    old = dev->Susp.State;
    if (q != NULL && d.Do != XHCI_SUSP_DO_NONE) {
        dev->Susp.Generation = q->ConnectGen;
        dev->Susp.Root = (q->Hub == NULL) ? 1UL : 0UL;
        if (d.Do == XHCI_SUSP_DO_SUSPEND) {
            kind = XHCI_SUSP_RUN_SUSPEND;
            hc->Counters.SuspSuspendsBegun++;
            step = XhciSuspBegin(&dev->Susp, kind, d.ArmIntent, 0,
                                 hc->SuspClockMs, &wait);
        } else {
            kind = XHCI_SUSP_RUN_RESUME;
            hc->Counters.SuspResumesBegun++;
            step = XhciSuspBegin(&dev->Susp, kind, 0,
                                 d.Do == XHCI_SUSP_DO_REARM ? 1UL : 0UL,
                                 hc->SuspClockMs, &wait);
        }
    }
    if (kind == 0) {
        /* No sequence: a batch of idle callbacks, when 6.2's readiness
         * holds (it asks the link ACTIVE). */
        batch = hcdSuspIdleBatchLocked(hc, dev);
    }
    hcdSuspFwDrain(hc, dev);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdSuspFinishT(hc, &taken);
    hcdSuspIFinishT(hc, &itaken);
    if (batch) {
        hcdSuspCbKick(hc);
    }
    if (kind == 0) {
        return 0;
    }
    (VOID)hcdSuspRun(hc, dev, step, wait, old);
    return 1;
}

/* The pass over every device (3.10). Thread only, powered. */
VOID HcdSuspService(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;
    ULONG slot;
    ULONG i;

    if (!hc->SuspOn || !hc->RootHubStarted) {
        return;
    }
    for (slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        for (i = 0; i < HCD_SUSP_PASSES; i++) {
            if (hcdSuspHalted(hc)) {
                return;
            }
            dev = hc->SlotDevice[slot];
            if (dev == NULL || !hcdSuspServes(hc, dev)) {
                break;
            }
            if (!hcdSuspDecideOne(hc, dev)) {
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Evidence (3.7, 3.8)                                                 */
/* ------------------------------------------------------------------ */

/* Whether the machine owns the device on port q now (any state but
 * ACTIVE and GONE). Thread only. */
static PHCD_USB_DEVICE hcdSuspOwner(PHCD_CONTROLLER hc, PHCD_PORT q)
{
    PHCD_USB_DEVICE dev;

    dev = q->Device;
    if (!hc->SuspOn || dev == NULL || dev->Gone ||
        dev->Susp.State == XHCI_SUSP_ACTIVE ||
        dev->Susp.State == XHCI_SUSP_GONE) {
        return NULL;
    }
    return dev;
}

ULONG HcdSuspOwnsPort(PHCD_CONTROLLER hc, PHCD_PORT q)
{
    return hcdSuspOwner(hc, q) != NULL;
}

/* One classified sample to the machine; a wake event's claim in the same
 * hold, its sequence owed to the pass. */
static VOID hcdSuspSample(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                          ULONG event)
{
    HCD_SUSP_TAKEN taken;
    KIRQL oldIrql;

    taken.Count = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (event == XHCI_SUSP_EV_STALE) {
        hc->Counters.SuspStaleSamples++;
    }
    if (XhciSuspEvidence(&dev->Susp, event)) {
        hcdSuspClaimDue(hc, dev, &taken);
        dev->SuspWakeDue = 1;
    }
    hcdSuspFwDrain(hc, dev);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdSuspFinishT(hc, &taken);
    if (event == XHCI_SUSP_EV_DISCONNECT &&
        dev->Susp.State == XHCI_SUSP_GONE && hcdSuspPdo(dev->Pdo) != NULL) {
        /* Gone to the machine: dropped through the cycle, which an unplug
         * the port's processing also sees makes no difference to, and
         * which a PED loss with CCS kept needs (round 1, finding 3). */
        HcdEnumCycle(hc, dev->Location, hcdSuspPdo(dev->Pdo)->Group);
    }
    if (dev->SuspWakeDue) {
        HcdThreadWake(hc);
    }
}

/* The root port's acknowledgement hands its sample over (3.7): PORTSC as
 * read, the change bits acknowledged. Connection processing goes on
 * unchanged after it. */
VOID HcdSuspRootSample(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG portsc,
                       ULONG acked)
{
    PHCD_USB_DEVICE dev;

    dev = hcdSuspOwner(hc, q);
    if (dev == NULL) {
        return;
    }
    hcdSuspSample(hc, dev,
                  XhciSuspRootEvidence(&dev->Susp, portsc, acked,
                                       q->ConnectGen));
}

/* A USB 2.0 hub port's finished resume handed over (3.8). `acked` the
 * change bits whose clear the hub took; when C_PORT_SUSPEND was seen and
 * its clear failed (`lost`), the hub may have taken it and report no more:
 * the wake would be stranded, so the device is uncertain and cycled, its
 * gate kept closed (Codex review of 36.30, round 3, finding 1). */
VOID HcdSuspHubSample(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG status,
                      ULONG acked, ULONG lost)
{
    PHCD_USB_DEVICE dev;
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;

    dev = hcdSuspOwner(hc, q);
    if (dev == NULL) {
        return;
    }
    if (lost) {
        /* RECOVERING first, in the hold the dispatch reads it under, so
         * nothing resumes it or opens its gate before the cycle drops it
         * (Codex review of 36.30, round 4, finding 1). */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        XhciSuspUncertain(&dev->Susp);
        dev->SuspWakeDue = 0;
        hcdSuspFwDrain(hc, dev);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        pdo = hcdSuspPdo(dev->Pdo);
        if (pdo != NULL && !hcdSuspHalted(hc)) {
            HcdEnumCycle(hc, dev->Location, pdo->Group);
        }
        return;
    }
    hcdSuspSample(hc, dev,
                  XhciSuspHubEvidence(&dev->Susp, status, acked,
                                      q->ConnectGen));
}

/* A SuperSpeed hub port's sample handed over by its look (8.6): `acked`
 * the change bits whose clear the hub took. A finished U3 exit is never a
 * wake here; a C_PORT_LINK_STATE whose clear failed (`lost`) leaves the
 * link's state unknown - uncertain at once, then the cycle - as stage A's
 * lost C_PORT_SUSPEND. */
VOID HcdSuspSsHubSample(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG status,
                        ULONG acked, ULONG lost)
{
    PHCD_USB_DEVICE dev;
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;

    dev = hcdSuspOwner(hc, q);
    if (dev == NULL) {
        return;
    }
    if (lost) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        XhciSuspUncertain(&dev->Susp);
        dev->SuspWakeDue = 0;
        hcdSuspFwDrain(hc, dev);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        pdo = hcdSuspPdo(dev->Pdo);
        if (pdo != NULL && !hcdSuspHalted(hc)) {
            HcdEnumCycle(hc, dev->Location, pdo->Group);
        }
        return;
    }
    hcdSuspSample(hc, dev,
                  XhciSuspSsHubEvidence(&dev->Susp, status, acked,
                                        q->ConnectGen));
}

/*
 * A Device Notification Event for slot `slotId` (8.7), from the DPC hook
 * (hcd_dev.c) with the switch on: a FUNCTION_WAKE for a device record is
 * latched while its window is open and the thread woken, or counted as
 * closed at receipt and dropped; another type, or a slot with no record,
 * is dropped. The record is the one SlotDevice[] names now, under this
 * lock: a record leaves it under the lock before it is freed, and a new
 * device's is entered only after its Enable Slot (8.7, slot reuse).
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
VOID HcdSuspNotificationLocked(PHCD_CONTROLLER hc, ULONG slotId, ULONG type,
                               ULONG iface)
{
    PHCD_USB_DEVICE dev;

    dev = NULL;
    if (slotId >= 1 && slotId <= XHCI_MAX_SLOTS &&
        slotId <= hc->Hc.Layout.MaxSlotsEn) {
        dev = hc->SlotDevice[slotId];
    }
    if (type != XHCI_DN_TYPE_FUNCTION_WAKE || dev == NULL) {
        hc->Counters.DevNotifyDropped++;
        return;
    }
    hc->Counters.FuncWakeEvents++;
    if (XhciSuspFwReceive(&dev->Susp, iface)) {
        HcdThreadWake(hc);
        return;
    }
    hc->Counters.FuncWakeClosed++;
    hcdSuspNoteLocked(hc, "susp.fw",
                      ((slotId & 0xFFUL) << 24) | ((iface & 0xFFUL) << 16) |
                          (0xFFUL << 8) | ((dev->Susp.State & 0xFUL) << 4) |
                          HCD_SUSP_FW_CLOSED);
}

/* ------------------------------------------------------------------ */
/* For the rest of the bus                                             */
/* ------------------------------------------------------------------ */

/* A new device's record: ACTIVE, its deferral list empty. */
VOID HcdSuspDeviceInit(PHCD_USB_DEVICE dev, PHCD_PORT port)
{
    XhciSuspLinkInit(&dev->Susp, port->Hub == NULL, port->ConnectGen);
    InitializeListHead(&dev->SlowDeferred);
    dev->SuspWakeDue = 0;
    dev->FwFirsts = 0;
    dev->FwCapable = 0;
    dev->FwArmedAll = 0;
}

/* Whether the device's link is ACTIVE: the survivor restart's doorbell and
 * the slow-IRP service ask. Controller lock held or thread. */
ULONG HcdSuspLinkActive(PHCD_USB_DEVICE dev)
{
    return dev->Susp.State == XHCI_SUSP_ACTIVE;
}

/* Whether the door forwards a descriptor request to the device (36.45;
 * design record 13 section 8.11): its link ACTIVE. Thread only. */
ULONG HcdSuspDoorServes(PHCD_USB_DEVICE dev)
{
    return XhciSuspDoorServes(dev->Susp.State);
}

/* Whether a slow IRP for the device waits (3.3): the link not ACTIVE -
 * SUSPENDED, or RECOVERING, whose device the cycle drops - and the request
 * not a host-side one. `abortOnly` the request is ABORT_PIPE. Thread
 * only. */
ULONG HcdSuspDefers(PHCD_USB_DEVICE dev, ULONG abortOnly)
{
    return dev->Susp.State != XHCI_SUSP_ACTIVE && !abortOnly;
}

/* A device leaving: the machine GONE (its slots are settled at the
 * unlink, 36.31). Thread only. */
VOID HcdSuspDeviceGone(PHCD_USB_DEVICE dev)
{
    XhciSuspGone(&dev->Susp);
    dev->SuspWakeDue = 0;
    /* What the closing window dropped is counted now: the device may be
     * freed with no further pass (Codex review of 36.38, round 2, finding
     * 2). The callers hold the controller lock. */
    if (dev->Controller != NULL) {
        hcdSuspFwDrain(dev->Controller, dev);
    }
}

/* ------------------------------------------------------------------ */
/* Stage B's configuration transaction (36.38; record 14 section 8.3)  */
/* ------------------------------------------------------------------ */

/* The bits set in a mask. */
static ULONG hcdSuspCount(ULONG mask)
{
    ULONG n;

    n = 0;
    while (mask != 0) {
        mask &= mask - 1;
        n++;
    }
    return n;
}

/* Whether a configuration change of the device is stage B's: the switch
 * on, a SuperSpeed device that is not a hub - its class decoded as its
 * PDOs' will be, since the bus's own configuration of a split device comes
 * before any PDO. Thread only. */
static ULONG hcdSuspCfgServes(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    ULONG cls;

    if (!hc->SuspOn || dev->Gone || dev->Hub != NULL) {
        return 0;
    }
    cls = HcdDeviceSpeedClass(hc, dev);
    return (cls == XHCI_SPEED_SUPER) ? 1UL : 0UL;
}

/* The entry (8.3): both masks cleared under the controller lock, so a
 * WAIT_WAKE that arrives while the configuration changes is refused. With
 * the switch at 0, or for a device not SuperSpeed, nothing. Thread only. */
VOID HcdSuspCfgEnter(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;

    if (!hcdSuspCfgServes(hc, dev)) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->FwFirsts = 0;
    dev->FwCapable = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/*
 * The exit, from every return (8.3). `ok` the transaction configured the
 * device with the nonzero configuration `config` (`length` bytes, the one
 * SET_CONFIGURATION sent): the first interface of each of its USB 3
 * functions is read with one GetStatus(interface) - the device ACTIVE, a
 * configuration change being a device-side slow IRP - and the capable ones
 * kept. Any other exit leaves both masks 0. Either way, in the hold that
 * writes them, every PDO of the device that holds a WAIT_WAKE and is left
 * with no wake function has it claimed (4.2's handshake) and completed
 * STATUS_NOT_SUPPORTED after the hold - a machine completion, no PwrSeq.
 * Thread only, PASSIVE_LEVEL.
 */
VOID HcdSuspCfgExit(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                    const UCHAR *config, ULONG length, ULONG ok)
{
    HCD_SUSP_TAKEN taken;
    PHCD_DEVICE_PDO pdo;
    PUCHAR s;
    KIRQL oldIrql;
    PIRP irp;
    ULONG firsts;
    ULONG beyond;
    ULONG capable;
    ULONG failed;
    ULONG reads;
    ULONG bytes;
    ULONG outcome;
    ULONG iface;

    if (!hcdSuspCfgServes(hc, dev)) {
        return;
    }
    taken.Count = 0;
    firsts = 0;
    beyond = 0;
    capable = 0;
    failed = 0;
    reads = 0;
    if (ok && config != NULL &&
        XhciFuncFirsts(config, length, &firsts, &beyond) != XHCI_FUNC_OK) {
        firsts = 0;
        beyond = 0;
    }
    if (!ok) {
        firsts = 0;
        beyond = 0;
    }
    if (beyond) {
        hc->Counters.SsSuspFarInterfaces++;
    }
    s = (PUCHAR)hc->ScratchVa;
    for (iface = 0; iface < 32; iface++) {
        if ((firsts & (1UL << iface)) == 0) {
            continue;
        }
        if (dev->Ep0Stuck || hcdSuspHalted(hc)) {
            /* A timeout follows the thread's ordinary EP0 rules: nothing
             * more is sent, and what was not read is not capable. */
            failed++;
            continue;
        }
        bytes = 0;
        outcome = HcdThreadControlOutcome(hc, dev, HCD_SUSP_RT_IFACE_IN,
                                          HCD_SUSP_REQ_GET_STATUS, 0,
                                          (USHORT)iface, 2, &bytes);
        if (outcome == HCD_CTL_DONE && bytes >= 2) {
            reads++;
            if ((s[0] & HCD_SUSP_FWR_CAPABLE) != 0) {
                capable |= 1UL << iface;
            }
        } else {
            /* A STALL, a failure or a short answer: not capable. */
            failed++;
        }
    }
    hc->Counters.SsSuspCapReads += reads;
    hc->Counters.SsSuspCapFailed += failed;
    hc->Counters.SsSuspCapable += hcdSuspCount(capable);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->FwFirsts = firsts;
    dev->FwCapable = capable;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        irp = pdo->WaitWakeIrp;
        if (irp == NULL || (capable & hcdSuspOwn(pdo)) != 0) {
            continue;
        }
        if (!XhciSuspClaim(1, IoSetCancelRoutine(irp, NULL) != NULL)) {
            continue;
        }
        pdo->WaitWakeIrp = NULL;
        hcdSuspTake(&taken, pdo, irp, HCD_SUSP_TAKE_WAKE_NS);
        hc->Counters.SsSuspCapSettled++;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdSuspFinishT(hc, &taken);
    hcdSuspNote(hc, "susp.ss.cap",
                ((dev->SlotId & 0xFFUL) << 24) |
                    ((hcdSuspCount(firsts) & 0xFFUL) << 16) |
                    ((hcdSuspCount(capable) & 0xFFUL) << 8) |
                    (failed & 0xFFUL));
}

/* ------------------------------------------------------------------ */
/* Dispatch's half (36.31; design record 14 sections 4 and 5, 3.11)    */
/* ------------------------------------------------------------------ */

/* The configuration's bmAttributes bit 5, remote wakeup (5.1): the
 * function's filtered configuration descriptor carries the device's
 * header. */
static ULONG hcdSuspRemoteWake(PHCD_DEVICE_PDO pdo)
{
    return (pdo->Config != NULL && pdo->ConfigLength >= 9 &&
            (pdo->Config[7] & 0x20) != 0) ? 1UL : 0UL;
}

/* Whether a PDO is stage A's, its controller's switch on (5.1). A hub PDO
 * and a composite parent never are: the parent keeps today's capabilities,
 * its WAIT_WAKE is refused and its D-IRPs are recorded and completed at
 * once (task 36.43). */
static ULONG hcdSuspPdoEligible(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo)
{
    if (hc == NULL || !hc->SuspOn || pdo->Hub || pdo->Composite) {
        return 0;
    }
    return XhciSuspEligible(1, pdo->SpeedClass, 0);
}

/* Whether a PDO whose controller is released was stage A's: the switch
 * as it was at its creation or last START (4.2's released controller). */
static ULONG hcdSuspPdoWasEligible(PHCD_DEVICE_PDO pdo)
{
    if (pdo->Composite) {
        return 0;
    }
    return XhciSuspEligible(pdo->PwrSwitch, pdo->SpeedClass, pdo->Hub);
}

/* Windows 98 SE and ME: whether wake is disabled there (5.5). Not read
 * yet: 36.35's test client reads the WAIT_WAKE completion defect on both;
 * until then wake is offered. */
static ULONG hcdSuspWakeOff9x(VOID)
{
    return 0;
}

/* Another function PDO of the device has PwrState D0 (4.3); a composite
 * parent, D0 for ever, is not one. PdoListLock and the controller lock
 * held. */
static ULONG hcdSuspSiblingD0(PHCD_USB_DEVICE dev, PHCD_DEVICE_PDO self)
{
    PHCD_DEVICE_PDO pdo;

    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        if (pdo != self && !pdo->Composite && pdo->PwrState == XHCI_SUSP_D0) {
            return 1;
        }
    }
    return 0;
}

/* A WAIT_WAKE completed at PASSIVE_LEVEL, as every power IRP is on
 * Windows 98 (win98-wdm.md), from the system work queue its cancel routine
 * hands it to: the item lives in the IRP's DriverContext, unused once the
 * routine owns it. */
static VOID NTAPI hcdSuspWakeCancelWork(PVOID context)
{
    PIRP irp;
    PHCD_DEVICE_PDO pdo;

    irp = (PIRP)context;
    pdo = (PHCD_DEVICE_PDO)IoGetCurrentIrpStackLocation(irp)
              ->DeviceObject->DeviceExtension;
    hcdSuspCompleteWake(pdo, irp, STATUS_CANCELLED);
}

XHCI_C_ASSERT(susp_wake_item,
              sizeof(WORK_QUEUE_ITEM) <=
                  sizeof(((PIRP)0)->Tail.Overlay.DriverContext));

/*
 * The WAIT_WAKE's cancel routine (4.2): it always completes the IRP. It
 * releases the cancel spin lock first, reaches the controller under the
 * PDO's Busy count, clears the slot if it still names the IRP, and hands
 * the completion to PASSIVE_LEVEL. IRQL: DISPATCH_LEVEL.
 */
static VOID NTAPI hcdSuspWakeCancel(PDEVICE_OBJECT DeviceObject, PIRP irp)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_CONTROLLER hc;
    PWORK_QUEUE_ITEM item;
    KIRQL oldIrql;

    pdo = (PHCD_DEVICE_PDO)DeviceObject->DeviceExtension;
    IoReleaseCancelSpinLock(irp->CancelIrql);
    (VOID)InterlockedIncrement(&pdo->Busy);
    hc = pdo->Controller;
    if (hc != NULL) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (pdo->WaitWakeIrp == irp) {
            pdo->WaitWakeIrp = NULL;
            pdo->PwrSeq++;
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        HcdThreadWake(hc);
    } else if (pdo->WaitWakeIrp == irp) {
        pdo->WaitWakeIrp = NULL;
    }
    (VOID)InterlockedDecrement(&pdo->Busy);
    item = (PWORK_QUEUE_ITEM)&irp->Tail.Overlay.DriverContext[0];
    ExInitializeWorkItem(item, hcdSuspWakeCancelWork, irp);
    ExQueueWorkItem(item, DelayedWorkQueue);
}

/*
 * IRP_MN_SET_POWER for a device state at a device PDO (4.2 to 4.4):
 * admitted by XhciSuspAdmit. Returns 0 when the switch is off or the PDO
 * is not stage A's - the caller then does today's - else 1 with *status.
 * The caller holds the PDO's Busy count across the call, the completion
 * included, so a REMOVE cannot pass while this dispatch still runs (Codex
 * review of 36.31, round 1, finding 1). IRQL: PASSIVE_LEVEL.
 */
ULONG HcdSuspPdoSetPower(PHCD_DEVICE_PDO pdo, PIRP irp, NTSTATUS *status)
{
    PIO_STACK_LOCATION stack;
    PHCD_CONTROLLER hc;
    PHCD_USB_DEVICE dev;
    XHCI_SUSP_ADMIT_IN in;
    HCD_SUSP_ITAKEN itaken;
    KIRQL listIrql;
    KIRQL oldIrql;
    ULONG answer;
    ULONG prev;
    ULONG ns;

    itaken.Count = 0;
    stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->Parameters.Power.State.DeviceState < PowerDeviceD0 ||
        stack->Parameters.Power.State.DeviceState > PowerDeviceD3) {
        return 0;
    }
    hc = pdo->Controller;
    if (!hcdSuspPdoEligible(hc, pdo)) {
        return 0;
    }
    in.Eligible = 1;
    in.Requested = (ULONG)stack->Parameters.Power.State.DeviceState;
    hcdSuspReportEnter(pdo);
    if (in.Requested != XHCI_SUSP_D0) {
        /* A Dx is effective at once (4.1), whatever the admission: its
         * record made before the IRP can be claimed, so no completion
         * overtakes it (Codex review of 36.31, round 1, finding 2). */
        pdo->Common.DevicePower = stack->Parameters.Power.State.DeviceState;
        (VOID)PoSetPowerState(pdo->Common.Self, DevicePowerState,
                              stack->Parameters.Power.State);
    }
    KeAcquireSpinLock(&hc->PdoListLock, &listIrql);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev = pdo->Device;
    in.Closed = (pdo->PwrClosed || dev == NULL) ? 1UL : 0UL;
    in.PowerDown = hc->PowerDown;
    in.Link = (dev != NULL) ? dev->Susp.State : XHCI_SUSP_GONE;
    in.SiblingD0 = (dev != NULL) ? hcdSuspSiblingD0(dev, pdo) : 0;
    prev = pdo->PwrState;
    answer = XhciSuspAdmit(&in, pdo->PwrState, &ns);
    pdo->PwrWanted = in.Requested;
    pdo->PwrState = ns;
    pdo->PwrSeq++;
    if (ns == XHCI_SUSP_D0) {
        /* 3.9: a D0 written here ends the PDO's hold after a wake. */
        pdo->WokeHold = 0;
    }
    /* The idle notification's triggers (6.5): its PDO accepting D3
     * settles its idle IRP POWER_STATE_INVALID at acceptance; a D0
     * written here from a Dx, on an ACTIVE link, settles it SUCCESS and
     * ends the episode. Completed after the report flag is let go, since
     * a completion routine may send a D-IRP here at once. */
    if (in.Requested == XHCI_SUSP_D3) {
        hcdSuspIdleTriggerLocked(pdo, XHCI_SUSP_SET_D3, &itaken);
    } else if (ns == XHCI_SUSP_D0 && prev != XHCI_SUSP_D0) {
        hcdSuspIdleOwnD0Locked(dev, pdo, &itaken);
    }
    if (answer == XHCI_SUSP_ADMIT_PEND) {
        hc->Counters.SuspDxPended++;
        pdo->PwrIrp = irp;
        pdo->PwrDxEpoch = hc->PowerEpoch;
        (VOID)InterlockedIncrement(&pdo->PowerPending);
        IoMarkIrpPending(irp);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    KeReleaseSpinLock(&hc->PdoListLock, listIrql);
    if (answer == XHCI_SUSP_ADMIT_PEND) {
        hcdSuspReportLeave(pdo);
        hcdSuspIFinish(&itaken);
        HcdThreadWake(hc);
        *status = STATUS_PENDING;
        return 1;
    }
    if (in.Requested == XHCI_SUSP_D0) {
        pdo->Common.DevicePower = PowerDeviceD0;
        (VOID)PoSetPowerState(pdo->Common.Self, DevicePowerState,
                              stack->Parameters.Power.State);
    }
    hcdSuspReportLeave(pdo);
    hcdSuspIFinish(&itaken);
    PoStartNextPowerIrp(irp);
    *status = HcdCompleteIrp(irp, STATUS_SUCCESS, irp->IoStatus.Information);
    return 1;
}

/*
 * IRP_MN_WAIT_WAKE at a device PDO (5.3): refused, or pended with a cancel
 * routine and stamped with hc->WakeSeq in the hold that fills the slot.
 * Returns 0 when the switch was off at the PDO's creation or the PDO is
 * not stage A's (today's STATUS_NOT_SUPPORTED), else 1 with *status; a
 * released controller refuses NO_SUCH_DEVICE (4.2). The caller holds the
 * PDO's Busy count across the call. IRQL: PASSIVE_LEVEL.
 */
ULONG HcdSuspPdoWaitWake(PHCD_DEVICE_PDO pdo, PIRP irp, NTSTATUS *status)
{
    PIO_STACK_LOCATION stack;
    PHCD_CONTROLLER hc;
    PHCD_USB_DEVICE dev;
    XHCI_SUSP_WW_IN in;
    KIRQL listIrql;
    KIRQL oldIrql;
    ULONG answer;
    ULONG back;
    NTSTATUS refusal;

    stack = IoGetCurrentIrpStackLocation(irp);
    hc = pdo->Controller;
    back = 0;
    in.Released = (hc == NULL) ? 1UL : 0UL;
    if (in.Released ? !hcdSuspPdoWasEligible(pdo)
                    : !hcdSuspPdoEligible(hc, pdo)) {
        return 0;
    }
    in.Eligible = 1;
    in.RemoteWake = hcdSuspRemoteWake(pdo);
    in.WakeOff9x = hcdSuspWakeOff9x();
    in.SystemState = (ULONG)stack->Parameters.WaitWake.PowerState;
    in.Closed = 1;
    in.PwrState = XHCI_SUSP_D3;
    in.Held = 0;
    if (in.Released) {
        answer = XhciSuspWaitWakeAdmit(&in);
    } else {
        /* PdoListLock too, so pdo->Device and its record cannot be
         * unlinked while a SuperSpeed PDO's wake functions are read
         * (4.1's order). */
        KeAcquireSpinLock(&hc->PdoListLock, &listIrql);
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (pdo->SpeedClass == XHCI_SPEED_SUPER) {
            /* 8.3: a wake function in place of the configuration's D5 - 0
             * for a device unconfigured, mid-configuration (the masks are
             * cleared at its entry) or with no capable function. */
            dev = pdo->Device;
            in.RemoteWake = (dev != NULL &&
                             (dev->FwCapable & hcdSuspOwn(pdo)) != 0)
                                ? 1UL
                                : 0UL;
        }
        in.Closed = pdo->PwrClosed;
        in.PwrState = pdo->PwrState;
        in.Held = (pdo->WaitWakeIrp != NULL) ? 1UL : 0UL;
        answer = XhciSuspWaitWakeAdmit(&in);
        if (answer == XHCI_SUSP_ST_PENDING) {
            pdo->WaitWakeIrp = irp;
            pdo->WakeStamp = hc->WakeSeq++;
            pdo->PwrSeq++;
            (VOID)InterlockedIncrement(&pdo->PowerPending);
            IoMarkIrpPending(irp);
            (VOID)IoSetCancelRoutine(irp, hcdSuspWakeCancel);
            if (XhciSuspPublish(irp->Cancel,
                                irp->Cancel &&
                                    IoSetCancelRoutine(irp, NULL) != NULL) ==
                XHCI_SUSP_PUB_TAKE_BACK) {
                pdo->WaitWakeIrp = NULL;
                back = 1;
            }
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        KeReleaseSpinLock(&hc->PdoListLock, listIrql);
    }
    if (answer == XHCI_SUSP_ST_PENDING) {
        HcdThreadWake(hc);
        if (back) {
            hcdSuspCompleteWake(pdo, irp, STATUS_CANCELLED);
        }
        *status = STATUS_PENDING;
        return 1;
    }
    switch (answer) {
    case XHCI_SUSP_ST_INVALID_STATE:
        refusal = STATUS_INVALID_DEVICE_STATE;
        break;
    case XHCI_SUSP_ST_BUSY:
        refusal = STATUS_DEVICE_BUSY;
        break;
    case XHCI_SUSP_ST_NO_DEVICE:
        refusal = STATUS_NO_SUCH_DEVICE;
        break;
    default:
        refusal = STATUS_NOT_SUPPORTED;
        break;
    }
    PoStartNextPowerIrp(irp);
    *status = HcdCompleteIrp(irp, refusal, 0);
    return 1;
}

/* QUERY_CAPABILITIES' power half (5.2), over today's values: changed only
 * for a stage A PDO. IRQL: PASSIVE_LEVEL. */
VOID HcdSuspPdoCaps(PHCD_DEVICE_PDO pdo, PDEVICE_CAPABILITIES caps)
{
    XHCI_SUSP_CAPS c;
    PHCD_CONTROLLER hc;

    (VOID)InterlockedIncrement(&pdo->Busy);
    hc = pdo->Controller;
    XhciSuspCaps(hcdSuspPdoEligible(hc, pdo), hcdSuspRemoteWake(pdo),
                 hcdSuspWakeOff9x(), &c);
    (VOID)InterlockedDecrement(&pdo->Busy);
    if (!c.Changed) {
        return;
    }
    caps->DeviceD1 = c.DeviceD1 ? TRUE : FALSE;
    caps->DeviceD2 = c.DeviceD2 ? TRUE : FALSE;
    caps->WakeFromD0 = FALSE;
    caps->WakeFromD1 = c.WakeFromD1 ? TRUE : FALSE;
    caps->WakeFromD2 = c.WakeFromD2 ? TRUE : FALSE;
    caps->WakeFromD3 = c.WakeFromD3 ? TRUE : FALSE;
    caps->DeviceWake = c.DeviceWake ? (DEVICE_POWER_STATE)c.DeviceWake
                                    : PowerDeviceUnspecified;
    caps->SystemWake = c.SystemWakeS0 ? PowerSystemWorking
                                      : PowerSystemUnspecified;
}

/* The slot's claims, completed after the hold: the D-IRP SUCCESS (a D0's
 * power state recorded), the WAIT_WAKE with `wakeStatus`, the idle IRP
 * CANCELLED and a detached entry's reference let go. */
static VOID hcdSuspSlotFinish(PHCD_SUSP_SETTLE s, NTSTATUS wakeStatus)
{
    if (s->Pwr != NULL) {
        hcdSuspCompletePower(s->Pdo, s->Pwr, s->PwrD0);
    }
    if (s->Wake != NULL) {
        hcdSuspCompleteWake(s->Pdo, s->Wake, wakeStatus);
    }
    if (s->Idle != NULL) {
        (VOID)HcdCompleteIrp(s->Idle, STATUS_CANCELLED, 0);
        (VOID)InterlockedDecrement(&s->Pdo->IdlePending);
    }
    if (s->IdleEntry) {
        (VOID)InterlockedDecrement(&s->Pdo->IdlePending);
    }
}

/* The slot's IRPs claimed and admission closed (4.4 (a), (c), (d)), in the
 * caller's hold: the D-IRP has no cancel routine; the WAIT_WAKE and the
 * idle IRP are the claimer's only if their routine is cleared here; an
 * idle IRP whose callback runs is owed CANCELLED to its return (6.5's
 * second trigger), and the PDO's own queued entry is detached.
 * Controller lock held. */
static VOID hcdSuspSlotCloseLocked(PHCD_DEVICE_PDO pdo, PHCD_SUSP_SETTLE s)
{
    HCD_SUSP_ITAKEN it;
    PIRP irp;

    s->Pdo = pdo;
    pdo->PwrClosed = 1;
    pdo->PwrStarted = 0;
    pdo->PwrSeq++;
    pdo->WokeHold = 0;
    s->Pwr = pdo->PwrIrp;
    s->PwrD0 = (pdo->PwrWanted == XHCI_SUSP_D0) ? 1UL : 0UL;
    pdo->PwrIrp = NULL;
    s->Wake = NULL;
    irp = pdo->WaitWakeIrp;
    if (irp != NULL &&
        XhciSuspClaim(1, IoSetCancelRoutine(irp, NULL) != NULL)) {
        pdo->WaitWakeIrp = NULL;
        s->Wake = irp;
    }
    s->IdleEntry = hcdSuspIdleDetachLocked(pdo);
    it.Count = 0;
    hcdSuspIdleTriggerLocked(pdo, XHCI_SUSP_SET_GONE, &it);
    s->Idle = (it.Count != 0) ? it.Irp[0] : NULL;
}

/*
 * A PDO's teardown (4.4 (a) and (c)): STOP, SURPRISE_REMOVAL, REMOVE. A
 * later D-IRP then completes at once and a WAIT_WAKE is refused; the
 * pended D-IRP completes SUCCESS, the WAIT_WAKE CANCELLED at a stop and
 * NO_SUCH_DEVICE at a removal. IRQL: PASSIVE_LEVEL.
 */
VOID HcdSuspPdoClose(PHCD_DEVICE_PDO pdo, ULONG removal)
{
    PHCD_CONTROLLER hc;
    HCD_SUSP_SETTLE s;
    KIRQL oldIrql;

    (VOID)InterlockedIncrement(&pdo->Busy);
    hc = pdo->Controller;
    if (hc != NULL) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        hcdSuspSlotCloseLocked(pdo, &s);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        HcdThreadWake(hc);
    } else {
        /* The controller is released: no other party reaches the slot
         * but a cancel routine, which the claim excludes; the callback
         * queue went with the controller's stop (HcdSuspCbStop). */
        hcdSuspSlotCloseLocked(pdo, &s);
    }
    (VOID)InterlockedDecrement(&pdo->Busy);
    hcdSuspSlotFinish(&s, removal ? STATUS_NO_SUCH_DEVICE : STATUS_CANCELLED);
}

/* At REMOVE: every D-IRP and WAIT_WAKE completion has returned - a cancel
 * routine's work item included - before the client may unload (4.2's
 * PowerPending). IRQL: PASSIVE_LEVEL. */
VOID HcdSuspPdoRundown(PHCD_DEVICE_PDO pdo)
{
    LARGE_INTEGER due;

    while (pdo->PowerPending != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}

/*
 * A successful START reopens admission (4.4, 3.9): PwrClosed 0 and D0
 * asked; PwrState D0 at once on a powered ACTIVE link, else left to the
 * thread, which resumes for it. Returns 0, reopening nothing, for a PDO
 * unlinked meanwhile: the START is then refused. The caller holds no
 * lock. IRQL: PASSIVE_LEVEL.
 */
ULONG HcdSuspPdoReopen(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo)
{
    PHCD_USB_DEVICE dev;
    POWER_STATE state;
    LARGE_INTEGER due;
    KIRQL listIrql;
    KIRQL oldIrql;
    ULONG report;

    /* 4.4: once an earlier teardown's power rundown has finished - a
     * WAIT_WAKE its stop left to a cancel routine completes first (Codex
     * review of 36.31, round 1, finding 3). Only after a teardown, whose
     * closed admission adds nothing: a START with admission open (the
     * first, or one with no STOP before it) may have a live WAIT_WAKE,
     * which nothing would end (round 2, finding 1). */
    while (pdo->PwrClosed && pdo->PowerPending != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    /* The idle rundown too (4.4, 36.32): a quarantined PDO is never
     * started again. */
    if (pdo->IdleQuarantined ||
        (pdo->PwrClosed && !HcdSuspIdleRundown(pdo, 1))) {
        /* Sticky: never started again, whatever its count reads now
         * (Codex review of 36.32, round 1, finding 5). */
        return 0;
    }
    hcdSuspReportEnter(pdo);
    KeAcquireSpinLock(&hc->PdoListLock, &listIrql);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev = pdo->Device;
    if (!pdo->Listed || dev == NULL) {
        /* Unlinked since the caller's check: its slot was settled and
         * closed there, and stays closed; the START is refused (round 2,
         * finding 2). */
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        KeReleaseSpinLock(&hc->PdoListLock, listIrql);
        hcdSuspReportLeave(pdo);
        return 0;
    }
    report = 0;
    pdo->PwrSwitch = hc->SuspOn;
    pdo->PwrClosed = 0;
    pdo->PwrStarted = 1;
    if (pdo->IdleSlot == NULL) {
        /* 4.4: IdleState NONE and no batch; after a teardown the rundown
         * has emptied the slot, so nothing of an earlier device carries
         * over. A START with no teardown before it keeps a held IRP. */
        pdo->IdleState = XHCI_SUSP_IDLE_NONE;
        pdo->IdleBatchCur = 0;
        pdo->IdleOwed = 0;
    }
    pdo->PwrWanted = XHCI_SUSP_D0;
    pdo->PwrSeq++;
    pdo->WokeHold = 0;
    if (dev->Susp.State == XHCI_SUSP_ACTIVE && !hc->PowerDown) {
        pdo->PwrState = XHCI_SUSP_D0;
        report = hcdSuspPdoEligible(hc, pdo);
    }
    /* A hub's or a composite parent's PDO is a logical power object
     * whatever its link: started is D0, reported here, with the switch at
     * 0 too (design record 13 section 10.12). */
    if (HcdPdoIsParent(pdo)) {
        report = 1;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    KeReleaseSpinLock(&hc->PdoListLock, listIrql);
    if (report) {
        /* The bus reports a started stage A PDO's power itself: a START
         * owes no D0 IRP (Codex review of 36.31, round 3). Only once the
         * device is powered - on a suspended link the thread reports it
         * when its resume reaches ACTIVE (hcdSuspReportD0) - and, inside
         * the PDO's report mutex, never after a newer Dx's report (round
         * 4). A parent PDO is reported above; a SuperSpeed PDO and the
         * switch at 0 keep today's START, which reports nothing. */
        state.DeviceState = PowerDeviceD0;
        pdo->Common.DevicePower = PowerDeviceD0;
        (VOID)PoSetPowerState(pdo->Common.Self, DevicePowerState, state);
    }
    hcdSuspReportLeave(pdo);
    HcdThreadWake(hc);
    return 1;
}

/*
 * The device's PDO chain unlinked (hcd_pdo.c, HcdDevicePdoGone; 4.4 "GONE
 * settles at the unlink"): each PDO's slot claimed and closed in the
 * caller's PdoListLock hold. Up to `max` claims go into the arrays; the
 * caller completes them after the hold with HcdSuspUnlinkFinish.
 * IRQL: DISPATCH_LEVEL, PdoListLock held.
 */
ULONG HcdSuspUnlinkLocked(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo,
                          PHCD_SUSP_SETTLE out)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hcdSuspSlotCloseLocked(pdo, out);
    /* 8.5: the hardware's wake enable did not survive the device. */
    pdo->FwArmed = 0;
    pdo->ArmNow = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return (out->Pwr != NULL || out->Wake != NULL || out->Idle != NULL ||
            out->IdleEntry)
               ? 1UL
               : 0UL;
}

/* The unlink's claims, deferred: the thread (or the stop's device drop)
 * completes nothing itself. IRQL: PASSIVE_LEVEL. */
VOID HcdSuspUnlinkFinish(PHCD_CONTROLLER hc, PHCD_SUSP_SETTLE s)
{
    if (s->Pwr != NULL) {
        hcdSuspDefer(hc, s->Pdo, s->Pwr, HCD_SUSP_DONE_POWER, s->PwrD0);
    }
    if (s->Wake != NULL) {
        hcdSuspDefer(hc, s->Pdo, s->Wake, HCD_SUSP_DONE_WAKE,
                     (ULONG_PTR)STATUS_NO_SUCH_DEVICE);
    }
    if (s->Idle != NULL) {
        hcdSuspDefer(hc, s->Pdo, s->Idle, HCD_SUSP_DONE_IDLE,
                     (ULONG_PTR)STATUS_CANCELLED);
    }
    if (s->IdleEntry) {
        (VOID)InterlockedDecrement(&s->Pdo->IdlePending);
    }
}

/*
 * The pool provisioned (the PnP start, before DNCTRL is chosen): the lists
 * and counts initialized and, while the switch is on, the grower and
 * HCD_SUSP_DONE_SPARE workers made - here, so that no completion and no
 * enum pass ever waits on a thread's creation (record 14 section 3.12).
 * One not had turns selective suspend off for this start, every thread
 * already made joined: with the switch off nothing is claimed, so nothing
 * needs a worker. IRQL: PASSIVE_LEVEL, the start.
 */
VOID HcdSuspDoneStart(PHCD_CONTROLLER hc)
{
    PHCD_SUSP_DONE g;
    ULONG made;

    KeInitializeSpinLock(&hc->SuspDoneLock);
    InitializeListHead(&hc->SuspDoneList);
    InitializeListHead(&hc->SuspDonePending);
    hc->SuspDoneCount = 0;
    hc->SuspDoneIdle = 0;
    hc->SuspDoneStop = 0;
    hc->SuspDoneStalled = 0;
    hc->SuspDoneStallSince = 0;
    hc->SuspGrower = NULL;
    hc->SuspGrowStop = 0;
    hc->SuspDoneHolds = 0;
    hc->SuspDoneClosed = 0;
    if (!hc->SuspOn) {
        return;
    }
    made = 0;
    g = hcdSuspDoneAlloc(hc);
    if (g != NULL) {
        if (NT_SUCCESS(hcdSuspDoneCreate(g, hcdSuspGrowThread))) {
            hc->SuspGrower = g;
        } else {
            HcdPoolFree(g);
        }
    }
    if (hc->SuspGrower != NULL) {
        while (made < HCD_SUSP_DONE_SPARE && hcdSuspDoneNew(hc)) {
            made++;
        }
    }
    XhciLogNote(&hc->Hc, "susp.done.pool", made);
    if (made < HCD_SUSP_DONE_SPARE) {
        HcdSuspDoneDrain(hc);
        hc->SuspOn = 0;
        hc->Counters.SuspSwitch = 0;
        XhciLogNote(&hc->Hc, "susp.switch", 0);
    }
}

/*
 * Every completion worker and the grower gone (the stop, before the
 * controller's storage can go and before the last device can let the
 * image unload; a refused start after HcdSuspDoneStart). First the
 * producers' rundown: the enum thread has been joined and the devices
 * dropped, each slot's SlotDevice cleared at its unlink, so the only hand-
 * off still to come is from HcdSuspSettleStale, the power path's, which
 * claims and hands off inside one SuspDoneHolds count. SuspDoneClosed set,
 * then the count waited to 0: a settle that counted itself before the
 * close is waited for; one after it sees the close and claims nothing
 * (and would find no device). Each side reads after its own locked
 * operation, a full barrier on x86 and amd64, so one always sees the
 * other (Codex review of the pool, round 1, finding 3; round 2, finding
 * 1: the power gate's event wait, which may return unsatisfied on 9x, is
 * not relied on).
 * Then the grower stopped and joined, every worker told to leave once none
 * is pending and the parked ones unparked; while IRPs still wait, the pool
 * grown by the same rule from here (the PnP manager's system thread, no
 * transition); then each worker joined. IRQL: PASSIVE_LEVEL.
 */
VOID HcdSuspDoneDrain(PHCD_CONTROLLER hc)
{
    PHCD_SUSP_DONE w;
    PLIST_ENTRY entry;
    LARGE_INTEGER due;
    KIRQL oldIrql;
    ULONG pending;

    if (hc->SuspDoneList.Flink == NULL) {
        return;                 /* never initialized: no start ran */
    }
    (VOID)InterlockedExchange((PLONG)&hc->SuspDoneClosed, 1);
    while (hc->SuspDoneHolds != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    KeAcquireSpinLock(&hc->SuspDoneLock, &oldIrql);
    w = hc->SuspGrower;
    hc->SuspGrower = NULL;
    hc->SuspGrowStop = 1;
    if (w != NULL) {
        (VOID)KeSetEvent(&w->Kick, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
    if (w != NULL) {
        hcdSuspDoneJoin(w);
    }
    KeAcquireSpinLock(&hc->SuspDoneLock, &oldIrql);
    hc->SuspDoneStop = 1;
    while (hcdSuspDoneKickLocked(hc)) {
    }
    KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
    for (;;) {
        KeAcquireSpinLock(&hc->SuspDoneLock, &oldIrql);
        pending = IsListEmpty(&hc->SuspDonePending) ? 0UL : 1UL;
        KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
        if (!pending) {
            break;
        }
        (VOID)hcdSuspDoneGrowStep(hc);
        HcdRelativeMs(&due, 10);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    for (;;) {
        KeAcquireSpinLock(&hc->SuspDoneLock, &oldIrql);
        if (IsListEmpty(&hc->SuspDoneList)) {
            KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
            break;
        }
        entry = RemoveHeadList(&hc->SuspDoneList);
        hc->SuspDoneCount--;
        KeReleaseSpinLock(&hc->SuspDoneLock, oldIrql);
        w = CONTAINING_RECORD(entry, HCD_SUSP_DONE, Link);
        hcdSuspDoneJoin(w);
    }
}

/* ------------------------------------------------------------------ */
/* The controller's transitions (4.4)                                  */
/* ------------------------------------------------------------------ */

/* The controller leaving D0, stopping, recovering in place or resetting:
 * in one controller-lock hold the epoch moves and admission completes
 * every Dx at once until the thread serves a powered controller again.
 * Controller lock held; any IRQL the lock allows. */
VOID HcdSuspControllerDownLocked(PHCD_CONTROLLER hc)
{
    hc->PowerEpoch++;
    hc->PowerDown = 1;
}

/* The thread's powered service has begun: admission back to normal.
 * Thread only. */
VOID HcdSuspControllerUp(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;

    if (!hc->PowerDown) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->PowerDown = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/*
 * The pended Dx of an older epoch completed SUCCESS (4.4): by the power
 * path once the controller is quiesced, at PASSIVE_LEVEL, since the enum
 * service does not run unpowered. One device at a time, its chain read
 * under PdoListLock and the controller lock. The claims and their hand-
 * offs inside one SuspDoneHolds count, closed by the stop's drain
 * (HcdSuspDoneDrain). IRQL: PASSIVE_LEVEL, the power gate held (the
 * thread is not in a pass).
 */
VOID HcdSuspSettleStale(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;
    PHCD_DEVICE_PDO pdo;
    HCD_SUSP_TAKEN taken;
    KIRQL listIrql;
    KIRQL oldIrql;
    ULONG slot;

    if (hc->SuspDoneList.Flink == NULL) {
        return;                 /* never initialized: no start ran */
    }
    (VOID)InterlockedIncrement((PLONG)&hc->SuspDoneHolds);
    if (hc->SuspDoneClosed != 0) {
        (VOID)InterlockedDecrement((PLONG)&hc->SuspDoneHolds);
        return;
    }
    for (slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        taken.Count = 0;
        KeAcquireSpinLock(&hc->PdoListLock, &listIrql);
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        dev = hc->SlotDevice[slot];
        for (pdo = (dev != NULL) ? hcdSuspPdo(dev->Pdo) : NULL; pdo != NULL;
             pdo = pdo->Sibling) {
            if (pdo->PwrIrp != NULL && pdo->PwrWanted != XHCI_SUSP_D0 &&
                XhciSuspDxStale(pdo->PwrDxEpoch, hc->PowerEpoch)) {
                hc->Counters.SuspDxStale++;
                hcdSuspTake(&taken, pdo, pdo->PwrIrp, HCD_SUSP_TAKE_DX);
                pdo->PwrIrp = NULL;
            }
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        KeReleaseSpinLock(&hc->PdoListLock, listIrql);
        /* Under the power gate: completed by the workers, as the thread's
         * (Codex review of the fix, round 1, finding 2). */
        hcdSuspFinishT(hc, &taken);
    }
    (VOID)InterlockedDecrement((PLONG)&hc->SuspDoneHolds);
}

/* ------------------------------------------------------------------ */
/* The idle notification (36.32; design record 14 sections 4.4, 6, 3.12) */
/* ------------------------------------------------------------------ */

/*
 * The idle IRP's cancel routine (4.2): it always completes the IRP. It
 * releases the cancel spin lock first, reaches the controller under the
 * PDO's Busy count, clears the slot if it still names the IRP - a cancel
 * of a QUEUED or RETURNED IRP ends the episode (6.4) - and completes it
 * CANCELLED, then what the episode's end settled. IRQL: DISPATCH_LEVEL.
 */
static VOID NTAPI hcdSuspIdleCancel(PDEVICE_OBJECT DeviceObject, PIRP irp)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_CONTROLLER hc;
    HCD_SUSP_ITAKEN taken;
    KIRQL listIrql;
    KIRQL oldIrql;
    ULONG prev;

    pdo = (PHCD_DEVICE_PDO)DeviceObject->DeviceExtension;
    IoReleaseCancelSpinLock(irp->CancelIrql);
    taken.Count = 0;
    (VOID)InterlockedIncrement(&pdo->Busy);
    hc = pdo->Controller;
    if (hc != NULL) {
        KeAcquireSpinLock(&hc->PdoListLock, &listIrql);
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (pdo->IdleSlot == irp) {
            prev = pdo->IdleState;
            pdo->IdleSlot = NULL;
            pdo->IdleState = XHCI_SUSP_IDLE_NONE;
            hc->Counters.IdleCancels++;
            if ((prev == XHCI_SUSP_IDLE_QUEUED ||
                 prev == XHCI_SUSP_IDLE_RETURNED) &&
                pdo->Device != NULL &&
                XhciSuspEpisodeEnds(XHCI_SUSP_EPI_CANCEL)) {
                hcdSuspIdleEndLocked(pdo->Device, &taken);
            }
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        KeReleaseSpinLock(&hc->PdoListLock, listIrql);
    } else if (pdo->IdleSlot == irp) {
        pdo->IdleSlot = NULL;
        pdo->IdleState = XHCI_SUSP_IDLE_NONE;
    }
    (VOID)InterlockedDecrement(&pdo->Busy);
    (VOID)HcdCompleteIrp(irp, STATUS_CANCELLED, 0);
    (VOID)InterlockedDecrement(&pdo->IdlePending);
    hcdSuspIFinish(&taken);
}

/*
 * IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION at a stage A PDO (6.1 to
 * 6.4): refused, or held in HELD with its cancel routine, the callback
 * and its context copied. Returns 0 when the PDO is not stage A's - a
 * hub, SuperSpeed, or the switch at 0: today's hold (hcd_urb.c), its
 * callback never called. The caller holds the PDO's Busy count and has
 * checked the controller and the callback. IRQL: <= DISPATCH_LEVEL.
 */
ULONG HcdSuspIdleSubmit(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc, PIRP irp,
                        PVOID callback, PVOID context, NTSTATUS *status)
{
    KIRQL oldIrql;
    ULONG answer;
    ULONG back;

    if (!hcdSuspPdoEligible(hc, pdo)) {
        return 0;
    }
    back = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    answer = XhciSuspIdleSubmit(0, 1, pdo->PwrClosed,
                                (pdo->IdleSlot != NULL ||
                                 pdo->IdleEntryOwned ||
                                 pdo->IdleIrp != NULL) ? 1UL : 0UL);
    if (answer == XHCI_SUSP_ST_PENDING) {
        pdo->IdleSlot = irp;
        pdo->IdleState = XHCI_SUSP_IDLE_HELD;
        pdo->IdleCb = callback;
        pdo->IdleCbContext = context;
        pdo->IdleOwed = 0;
        (VOID)InterlockedIncrement(&pdo->IdlePending);
        IoMarkIrpPending(irp);
        (VOID)IoSetCancelRoutine(irp, hcdSuspIdleCancel);
        if (XhciSuspPublish(irp->Cancel,
                            irp->Cancel &&
                                IoSetCancelRoutine(irp, NULL) != NULL) ==
            XHCI_SUSP_PUB_TAKE_BACK) {
            pdo->IdleSlot = NULL;
            pdo->IdleState = XHCI_SUSP_IDLE_NONE;
            back = 1;
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (answer == XHCI_SUSP_ST_PENDING) {
        XHCI_DBG_VALUE("hcd: idle notification held for stage A, port",
                       pdo->Port);
        HcdThreadWake(hc);
        if (back) {
            (VOID)HcdCompleteIrp(irp, STATUS_CANCELLED, 0);
            (VOID)InterlockedDecrement(&pdo->IdlePending);
        }
        *status = STATUS_PENDING;
        return 1;
    }
    *status = HcdCompleteIrp(irp,
                             (answer == XHCI_SUSP_ST_BUSY)
                                 ? STATUS_DEVICE_BUSY
                                 : STATUS_NO_SUCH_DEVICE,
                             0);
    return 1;
}

/* A system power IRP at the PDO: its state published under the
 * controller lock, the lock batch formation and the claim read it under,
 * and - a state other than S0 (6.5's third trigger) - in the same hold its
 * idle IRP settled CANCELLED and the device's episode ended, so no fresh
 * idle IRP is batched or called on the old S0 (Codex review of 36.32,
 * round 1, finding 4). Returns 0, publishing nothing, when the controller
 * is released (the caller then writes the state). The caller holds the
 * PDO's Busy count. IRQL: PASSIVE_LEVEL. */
ULONG HcdSuspIdleSystem(PHCD_DEVICE_PDO pdo, SYSTEM_POWER_STATE state)
{
    PHCD_CONTROLLER hc;
    HCD_SUSP_ITAKEN taken;
    KIRQL listIrql;
    KIRQL oldIrql;

    hc = pdo->Controller;
    if (hc == NULL) {
        return 0;
    }
    taken.Count = 0;
    KeAcquireSpinLock(&hc->PdoListLock, &listIrql);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pdo->Common.SystemPower = state;
    if (state != PowerSystemWorking) {
        hcdSuspIdleTriggerLocked(pdo, XHCI_SUSP_SET_SYSTEM, &taken);
        if (pdo->Device != NULL &&
            XhciSuspEpisodeEnds(XHCI_SUSP_EPI_SYSTEM)) {
            hcdSuspIdleEndLocked(pdo->Device, &taken);
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    KeReleaseSpinLock(&hc->PdoListLock, listIrql);
    hcdSuspIFinish(&taken);
    return 1;
}

/*
 * Teardown's (e): every idle IRP and entry of the PDO's has let go of it
 * (IdlePending 0), after (a) to (d) have settled what is the bus's to
 * settle. What is left but a running callback is a completion already
 * under way - today's idle IRP's included - and is waited out as before. A
 * PnP caller (`mayWaitCallback`) waits for a running callback too, which
 * with the DMA-halt proof has nothing left in the bus to wait on - an
 * abandoned one, its controller released, included; a deletion does not
 * (it follows a REMOVE whose rundown finished, or is of a PDO PnP never
 * saw, which had no client to submit one). A running callback without the
 * proof (CommonBufferPinned now, or at the stop that abandoned it), or one
 * a deletion will not wait for: quarantine - 0 is returned, the PDO marked
 * and counted, and the caller keeps the object (4.4). The client cannot
 * be unloading under it then: a client lets go of its idle IRP before it
 * passes its REMOVE down, and that IRP completes only at the callback's
 * return (Codex review of 36.32, round 1, finding 2). IRQL:
 * PASSIVE_LEVEL.
 */
ULONG HcdSuspIdleRundown(PHCD_DEVICE_PDO pdo, ULONG mayWaitCallback)
{
    PHCD_CONTROLLER hc;
    LARGE_INTEGER due;
    ULONG keep;

    while (pdo->IdlePending != 0) {
        (VOID)InterlockedIncrement(&pdo->Busy);
        hc = pdo->Controller;
        keep = (pdo->IdleRunning &&
                ((hc != NULL && hc->CommonBufferPinned) || pdo->IdlePinned ||
                 !mayWaitCallback))
                   ? 1UL
                   : 0UL;
        if (keep && !pdo->IdleQuarantined) {
            pdo->IdleQuarantined = 1;
            if (hc != NULL) {
                (VOID)InterlockedIncrement(
                    (PLONG)&hc->Counters.IdleQuarantined);
                hcdSuspNote(hc, "idle.quarantine", pdo->Port);
            }
            XHCI_DBG_VALUE("hcd: idle rundown unfinished, PDO kept, port",
                           pdo->Port);
        }
        (VOID)InterlockedDecrement(&pdo->Busy);
        if (keep) {
            return 0;
        }
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    return 1;
}

/* The phase moved `from` -> `to` by `actor`, if it still reads `from`
 * and 4.4 allows the move. Returns whether it moved. IRQL: <=
 * DISPATCH_LEVEL. */
static ULONG hcdSuspCbMove(PHCD_SUSP_CB cb, ULONG actor, ULONG from,
                           ULONG to)
{
    KIRQL oldIrql;
    ULONG moved;

    KeAcquireSpinLock(&cb->Lock, &oldIrql);
    moved = (cb->Phase == from && XhciSuspPhaseAllowed(actor, from, to))
                ? 1UL
                : 0UL;
    if (moved) {
        cb->Phase = to;
    }
    KeReleaseSpinLock(&cb->Lock, oldIrql);
    return moved;
}

/* Teardown's IN_CALLBACK -> ABANDONED, with the stop's DMA-halt verdict
 * recorded on the callback's PDO in the same hold of the context's lock:
 * the thread's IN_CALLBACK -> RETURNING takes that lock, so the PDO's two
 * references hold it while it is written (Codex review of 36.32, round
 * 2, finding 1). IRQL: PASSIVE_LEVEL. */
static ULONG hcdSuspCbAbandon(PHCD_SUSP_CB cb, ULONG pinned)
{
    KIRQL oldIrql;
    ULONG moved;

    KeAcquireSpinLock(&cb->Lock, &oldIrql);
    moved = (cb->Phase == XHCI_SUSP_PH_IN_CALLBACK &&
             XhciSuspPhaseAllowed(XHCI_SUSP_ACTOR_TEARDOWN,
                                  XHCI_SUSP_PH_IN_CALLBACK,
                                  XHCI_SUSP_PH_ABANDONED))
                ? 1UL
                : 0UL;
    if (moved) {
        if (pinned) {
            cb->Pdo->IdlePinned = 1;
        }
        cb->Phase = XHCI_SUSP_PH_ABANDONED;
    }
    KeReleaseSpinLock(&cb->Lock, oldIrql);
    return moved;
}

static ULONG hcdSuspCbPhase(PHCD_SUSP_CB cb)
{
    KIRQL oldIrql;
    ULONG phase;

    KeAcquireSpinLock(&cb->Lock, &oldIrql);
    phase = cb->Phase;
    KeReleaseSpinLock(&cb->Lock, oldIrql);
    return phase;
}

/*
 * CLAIMING (6.3): the queue's head entry taken and claimed by the three-way
 * claim. Returns 0 for an empty queue, else the claim's answer: RUN with
 * the callback copied into the context; SKIP with the IRP taken CANCELLED
 * and the episode ended; LET_GO. The entry's reference is taken to drop
 * unless the callback runs. PdoListLock then the controller lock, for the
 * device's chain. IRQL: PASSIVE_LEVEL, the callback thread.
 */
static ULONG hcdSuspCbClaim(PHCD_CONTROLLER hc, PHCD_SUSP_CB cb,
                            PHCD_SUSP_ITAKEN t)
{
    XHCI_SUSP_CLAIM_IN in;
    PHCD_DEVICE_PDO pdo;
    PLIST_ENTRY entry;
    KIRQL listIrql;
    KIRQL oldIrql;
    PIRP irp;
    ULONG answer;

    KeAcquireSpinLock(&hc->PdoListLock, &listIrql);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (IsListEmpty(&hc->IdleQueue)) {
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        KeReleaseSpinLock(&hc->PdoListLock, listIrql);
        return 0;
    }
    entry = RemoveHeadList(&hc->IdleQueue);
    pdo = CONTAINING_RECORD(entry, HCD_DEVICE_PDO, IdleEntry);
    pdo->IdleQueued = 0;
    irp = pdo->IdleSlot;
    in.IdleState = pdo->IdleState;
    in.EntryBatch = pdo->IdleEntryBatch;
    in.BatchCur = pdo->IdleBatchCur;
    in.Started = pdo->PwrStarted;
    in.Closed = pdo->PwrClosed;
    in.PwrState = pdo->PwrState;
    in.SystemS0 = (pdo->Common.SystemPower == PowerSystemWorking) ? 1UL : 0UL;
    in.EntryEpoch = pdo->IdleEntryEpoch;
    in.Epoch = hc->PowerEpoch;
    in.SlotFull = (irp != NULL) ? 1UL : 0UL;
    in.Owned = (irp != NULL && IoSetCancelRoutine(irp, NULL) != NULL)
                   ? 1UL
                   : 0UL;
    answer = XhciSuspIdleClaim(&in);
    if (answer == XHCI_SUSP_CLAIM_RUN) {
        pdo->IdleState = XHCI_SUSP_IDLE_RUNNING;
        pdo->IdleRunning = 1;
        pdo->IdleOwed = 0;
        cb->Pdo = pdo;
        cb->Irp = irp;
        cb->Batch = pdo->IdleEntryBatch;
        cb->Callback = (HCD_SUSP_IDLE_CALLBACK)pdo->IdleCb;
        cb->Context = pdo->IdleCbContext;
        hc->Counters.IdleCalls++;
        hc->SuspNoteCallPort = pdo->Port;
    } else {
        pdo->IdleEntryOwned = 0;
        if (answer == XHCI_SUSP_CLAIM_SKIP) {
            pdo->IdleSlot = NULL;
            pdo->IdleState = XHCI_SUSP_IDLE_NONE;
            hc->Counters.IdleSkips++;
            hcdSuspITake(t, pdo, irp, STATUS_CANCELLED);
            if (pdo->Device != NULL &&
                XhciSuspEpisodeEnds(XHCI_SUSP_EPI_SKIP)) {
                hcdSuspIdleEndLocked(pdo->Device, t);
            }
        }
        hcdSuspITake(t, pdo, NULL, STATUS_SUCCESS);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    KeReleaseSpinLock(&hc->PdoListLock, listIrql);
    return answer;
}

/*
 * RETURNING (6.4, 6.5): the callback has returned. The IRP the thread owns
 * is settled by what reached it meanwhile - its cancel first, then the
 * strongest owed trigger - or else becomes RETURNED and is re-published
 * by 4.2's handshake, irp->Cancel tested. An episode that ended while it
 * ran settles its RETURNED siblings in D1 or D2 now (the link ACTIVE),
 * and a cancel found now ends it. The entry's reference is taken to drop.
 * IRQL: PASSIVE_LEVEL, the callback thread.
 */
static VOID hcdSuspCbReturned(PHCD_CONTROLLER hc, PHCD_SUSP_CB cb,
                              PHCD_SUSP_ITAKEN t)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_DEVICE_PDO sib;
    PHCD_USB_DEVICE dev;
    KIRQL listIrql;
    KIRQL oldIrql;
    PIRP irp;
    ULONG trigger;
    ULONG st;
    ULONG ended;

    pdo = cb->Pdo;
    irp = cb->Irp;
    ended = 0;
    KeAcquireSpinLock(&hc->PdoListLock, &listIrql);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->Counters.IdleReturns++;
    pdo->IdleRunning = 0;
    pdo->IdleEntryOwned = 0;
    trigger = irp->Cancel ? XHCI_SUSP_SET_CANCEL : pdo->IdleOwed;
    pdo->IdleOwed = 0;
    st = (trigger != 0)
             ? XhciSuspIdleSettle(trigger, XHCI_SUSP_IDLE_RETURNED,
                                  pdo->PwrState)
             : XHCI_SUSP_ST_NONE;
    if (st != XHCI_SUSP_ST_NONE) {
        pdo->IdleSlot = NULL;
        pdo->IdleState = XHCI_SUSP_IDLE_NONE;
        hcdSuspITake(t, pdo, irp, hcdSuspIdleStatus(st));
        ended = (trigger == XHCI_SUSP_SET_CANCEL) ? 1UL : 0UL;
    } else {
        pdo->IdleState = XHCI_SUSP_IDLE_RETURNED;
        (VOID)IoSetCancelRoutine(irp, hcdSuspIdleCancel);
        if (XhciSuspPublish(irp->Cancel,
                            irp->Cancel &&
                                IoSetCancelRoutine(irp, NULL) != NULL) ==
            XHCI_SUSP_PUB_TAKE_BACK) {
            pdo->IdleSlot = NULL;
            pdo->IdleState = XHCI_SUSP_IDLE_NONE;
            hcdSuspITake(t, pdo, irp, STATUS_CANCELLED);
            ended = 1;
        }
    }
    dev = pdo->Device;
    if (dev != NULL) {
        if (ended && XhciSuspEpisodeEnds(XHCI_SUSP_EPI_CANCEL)) {
            hcdSuspIdleEndLocked(dev, t);
        } else if (pdo->IdleBatchCur != cb->Batch &&
                   dev->Susp.State == XHCI_SUSP_ACTIVE) {
            /* Ended while the callback ran: its siblings' settlement was
             * left to this return (hcdSuspIdleEndLocked). */
            for (sib = hcdSuspPdo(dev->Pdo); sib != NULL;
                 sib = sib->Sibling) {
                hcdSuspIdleTriggerLocked(sib, XHCI_SUSP_SET_EPISODE, t);
            }
        }
    }
    hcdSuspITake(t, pdo, NULL, STATUS_SUCCESS);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    KeReleaseSpinLock(&hc->PdoListLock, listIrql);
}

/*
 * The callback thread (6.1, 4.4): one per controller while the switch is
 * on, calling one callback at a time at PASSIVE_LEVEL with no bus lock or
 * gate held. It dequeues and claims only in CLAIMING, which it wins from
 * IDLE before it looks at the queue; failing that (EXITING), it leaves.
 * Abandoned by a teardown in a callback (its IN_CALLBACK -> RETURNING
 * fails), it touches nothing of the controller: it completes the IRP it
 * owns CANCELLED, lets go of the PDO's two references and leaves; the
 * unload waits for it and frees its context (HcdSuspDriverUnload).
 * IRQL: PASSIVE_LEVEL (a system thread).
 */
static VOID NTAPI hcdSuspCbThread(PVOID context)
{
    PHCD_SUSP_CB cb;
    PHCD_CONTROLLER hc;
    HCD_SUSP_ITAKEN taken;
    PHCD_DEVICE_PDO pdo;
    ULONG answer;
    ULONG more;

    cb = (PHCD_SUSP_CB)context;
    more = 0;
    taken.Count = 0;
    for (;;) {
        if (!more) {
            (VOID)KeWaitForSingleObject(&cb->Wake, Executive, KernelMode,
                                        FALSE, NULL);
        }
        more = 0;
        if (!hcdSuspCbMove(cb, XHCI_SUSP_ACTOR_THREAD, XHCI_SUSP_PH_IDLE,
                           XHCI_SUSP_PH_CLAIMING)) {
            break;
        }
        hc = cb->Hc;
        answer = hcdSuspCbClaim(hc, cb, &taken);
        if (answer != XHCI_SUSP_CLAIM_RUN) {
            (VOID)hcdSuspCbMove(cb, XHCI_SUSP_ACTOR_THREAD,
                                XHCI_SUSP_PH_CLAIMING, XHCI_SUSP_PH_IDLE);
            hcdSuspIFinish(&taken);
            more = (answer != 0) ? 1UL : 0UL;
            continue;
        }
        hcdSuspNote(hc, "idle.call", hc->SuspNoteCallPort);
        (VOID)hcdSuspCbMove(cb, XHCI_SUSP_ACTOR_THREAD,
                            XHCI_SUSP_PH_CLAIMING, XHCI_SUSP_PH_IN_CALLBACK);
        cb->Callback(cb->Context);
        if (!hcdSuspCbMove(cb, XHCI_SUSP_ACTOR_THREAD,
                           XHCI_SUSP_PH_IN_CALLBACK,
                           XHCI_SUSP_PH_RETURNING)) {
            /* ABANDONED (4.4): nothing of the controller is touched. The
             * callback has returned, so the rundown waits for the
             * completion below rather than quarantine past it (Codex
             * review of 36.32, round 2, finding 2). */
            pdo = cb->Pdo;
            pdo->IdleRunning = 0;
            (VOID)HcdCompleteIrp(cb->Irp, STATUS_CANCELLED, 0);
            (VOID)InterlockedDecrement(&pdo->IdlePending);
            (VOID)InterlockedDecrement(&pdo->IdlePending);
            break;
        }
        hcdSuspCbReturned(hc, cb, &taken);
        (VOID)hcdSuspCbMove(cb, XHCI_SUSP_ACTOR_THREAD,
                            XHCI_SUSP_PH_RETURNING, XHCI_SUSP_PH_IDLE);
        hcdSuspIFinish(&taken);
        more = 1;
    }
    (VOID)KeSetEvent(&cb->Exited, IO_NO_INCREMENT, FALSE);
    (VOID)InterlockedExchange((PLONG)&cb->ExitFlag, 1);
    (VOID)PsTerminateSystemThread(STATUS_SUCCESS);
}

/* The thread's exit waited for (Codex review of 36.32, round 1, finding
 * 3): on NT its object; on 9x, where a thread-object wait faults and an
 * event wait may return 0xFFFFFFFF unsatisfied (win98-wdm.md), the flag
 * the thread sets last, polled at 1 ms - as the controller thread's stop
 * leaves only the thread's call into PsTerminateSystemThread after it.
 * IRQL: PASSIVE_LEVEL. */
static VOID hcdSuspCbExitWait(PHCD_SUSP_CB cb)
{
    LARGE_INTEGER due;

    if (cb->ThreadObject != NULL) {
        while (KeWaitForSingleObject(cb->ThreadObject, Executive, KernelMode,
                                     FALSE, NULL) != STATUS_SUCCESS) {
            HcdRelativeMs(&due, 1);
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        }
        ObDereferenceObject(cb->ThreadObject);
        cb->ThreadObject = NULL;
        return;
    }
    while (cb->ExitFlag == 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}

/* Contexts whose thread a teardown abandoned in a callback, each with its
 * thread object on NT: the unload waits each thread out before the image
 * goes, then frees the context (4.4's driver-global list). */
static KSPIN_LOCK hcdSuspAbandonLock;
static PHCD_SUSP_CB hcdSuspAbandoned;

/* IRQL: PASSIVE_LEVEL (DriverEntry). */
VOID HcdSuspDriverInit(VOID)
{
    KeInitializeSpinLock(&hcdSuspAbandonLock);
    hcdSuspAbandoned = NULL;
}

/* IRQL: PASSIVE_LEVEL (the driver's unload, every device object gone). */
VOID HcdSuspDriverUnload(VOID)
{
    PHCD_SUSP_CB cb;
    KIRQL oldIrql;

    for (;;) {
        KeAcquireSpinLock(&hcdSuspAbandonLock, &oldIrql);
        cb = hcdSuspAbandoned;
        if (cb != NULL) {
            hcdSuspAbandoned = cb->Next;
        }
        KeReleaseSpinLock(&hcdSuspAbandonLock, oldIrql);
        if (cb == NULL) {
            return;
        }
        hcdSuspCbExitWait(cb);
        HcdPoolFree(cb);
    }
}

/* The thread joined as the controller thread is (hcd_ctl.c: its object
 * on NT, the event on 9x), then the context freed. IRQL: PASSIVE_LEVEL. */
static VOID hcdSuspCbJoin(PHCD_SUSP_CB cb)
{
    (VOID)KeSetEvent(&cb->Wake, IO_NO_INCREMENT, FALSE);
    hcdSuspCbExitWait(cb);
    HcdPoolFree(cb);
}

/*
 * Teardown's step (1) (4.4): every unclaimed entry taken off the queue,
 * its IRP settled by the three-way claim (claimed: CANCELLED; cancel-owned
 * or empty: left) and its reference let go, so no PDO waits on an entry
 * behind a callback that does not return. IRQL: PASSIVE_LEVEL.
 */
static VOID hcdSuspCbDetach(PHCD_CONTROLLER hc)
{
    HCD_SUSP_ITAKEN taken;
    PHCD_DEVICE_PDO pdo;
    PLIST_ENTRY entry;
    KIRQL oldIrql;
    PIRP irp;
    ULONG more;

    do {
        taken.Count = 0;
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        while (!IsListEmpty(&hc->IdleQueue) &&
               taken.Count + 2 <= HCD_SUSP_ITAKE_MAX) {
            entry = RemoveHeadList(&hc->IdleQueue);
            pdo = CONTAINING_RECORD(entry, HCD_DEVICE_PDO, IdleEntry);
            pdo->IdleQueued = 0;
            pdo->IdleEntryOwned = 0;
            pdo->IdleBatchCur = 0;
            irp = pdo->IdleSlot;
            if (irp != NULL && pdo->IdleState == XHCI_SUSP_IDLE_QUEUED &&
                XhciSuspClaim(1, IoSetCancelRoutine(irp, NULL) != NULL)) {
                pdo->IdleSlot = NULL;
                pdo->IdleState = XHCI_SUSP_IDLE_NONE;
                hcdSuspITake(&taken, pdo, irp, STATUS_CANCELLED);
            }
            hcdSuspITake(&taken, pdo, NULL, STATUS_SUCCESS);
        }
        more = IsListEmpty(&hc->IdleQueue) ? 0UL : 1UL;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        hcdSuspIFinish(&taken);
    } while (more);
}

/*
 * The callback thread started (6.1): only while the switch is on, so the
 * switch at 0 creates nothing. Referenced on NT as the controller thread
 * is (hcd_ctl.c). Without it no batch is formed (hcdSuspIdleBatchLocked)
 * and a stage A idle IRP stays HELD until its own settlement. IRQL:
 * PASSIVE_LEVEL, the start.
 */
NTSTATUS HcdSuspCbStart(PHCD_CONTROLLER hc)
{
    PHCD_SUSP_CB cb;
    HANDLE handle;
    NTSTATUS status;
    PUCHAR p;
    ULONG i;

    InitializeListHead(&hc->IdleQueue);
    hc->SuspCb = NULL;
    hc->SuspNotes = 0;
    if (!hc->SuspOn) {
        return STATUS_SUCCESS;
    }
    cb = (PHCD_SUSP_CB)HcdPoolAlloc(sizeof(HCD_SUSP_CB));
    if (cb == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    p = (PUCHAR)cb;
    for (i = 0; i < sizeof(HCD_SUSP_CB); i++) {
        p[i] = 0;
    }
    KeInitializeSpinLock(&cb->Lock);
    KeInitializeEvent(&cb->Wake, SynchronizationEvent, FALSE);
    KeInitializeEvent(&cb->Exited, NotificationEvent, FALSE);
    cb->Phase = XHCI_SUSP_PH_IDLE;
    cb->Hc = hc;
    status = PsCreateSystemThread(&handle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                                  hcdSuspCbThread, cb);
    if (!NT_SUCCESS(status)) {
        HcdPoolFree(cb);
        return status;
    }
    status = STATUS_SUCCESS;
    if (IoIsWdmVersionAvailable(1, 0x10)) {
        status = ObReferenceObjectByHandle(handle, THREAD_ALL_ACCESS, NULL,
                                           KernelMode, &cb->ThreadObject,
                                           NULL);
    }
    (VOID)ZwClose(handle);
    if (!NT_SUCCESS(status)) {
        /* As the controller thread's start (hcd_ctl.c): told to leave and
         * its event waited for. */
        cb->ThreadObject = NULL;
        (VOID)hcdSuspCbMove(cb, XHCI_SUSP_ACTOR_TEARDOWN, XHCI_SUSP_PH_IDLE,
                            XHCI_SUSP_PH_EXITING);
        hcdSuspCbJoin(cb);
        return status;
    }
    hc->SuspCb = cb;
    return STATUS_SUCCESS;
}

/*
 * The callback thread stopped by teardown's handoff (4.4), once the enum
 * thread has exited and the devices have gone (their slots settled at the
 * unlink): (1) the unclaimed entries detached; (2) IDLE -> EXITING won:
 * the thread joined and its context freed; (3) IN_CALLBACK -> ABANDONED
 * won: the context and its thread go to the abandoned list and the
 * controller lets go of them; (4) CLAIMING or RETURNING, which never
 * block: waited out, then from (1) again; (5) after (2) or (3), (1) once
 * more. IRQL: PASSIVE_LEVEL, the stop.
 */
VOID HcdSuspCbStop(PHCD_CONTROLLER hc)
{
    PHCD_SUSP_CB cb;
    LARGE_INTEGER due;
    KIRQL oldIrql;
    ULONG phase;

    cb = hc->SuspCb;
    if (cb == NULL) {
        return;
    }
    for (;;) {
        hcdSuspCbDetach(hc);
        phase = hcdSuspCbPhase(cb);
        switch (XhciSuspHandoff(phase)) {
        case XHCI_SUSP_HAND_EXIT:
            if (!hcdSuspCbMove(cb, XHCI_SUSP_ACTOR_TEARDOWN, phase,
                               XHCI_SUSP_PH_EXITING)) {
                continue;
            }
            hcdSuspCbDetach(hc);
            hc->SuspCb = NULL;
            hcdSuspCbJoin(cb);
            return;
        case XHCI_SUSP_HAND_ABANDON:
            if (!hcdSuspCbAbandon(cb, hc->CommonBufferPinned)) {
                continue;
            }
            hcdSuspCbDetach(hc);
            hc->SuspCb = NULL;
            hc->Counters.IdleAbandoned++;
            hcdSuspNote(hc, "idle.abandon", hc->Counters.IdleAbandoned);
            KeAcquireSpinLock(&hcdSuspAbandonLock, &oldIrql);
            cb->Next = hcdSuspAbandoned;
            hcdSuspAbandoned = cb;
            KeReleaseSpinLock(&hcdSuspAbandonLock, oldIrql);
            return;
        case XHCI_SUSP_HAND_WAIT:
            HcdRelativeMs(&due, 1);
            (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
            continue;
        default:
            /* Not reached: only this teardown moves the phase to EXITING
             * or ABANDONED. */
            hc->SuspCb = NULL;
            return;
        }
    }
}
