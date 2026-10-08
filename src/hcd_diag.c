/*
 * hcd_diag.c - the diagnostic build's records (2.2.0.2; roadmap-hcd.md task
 * 36.1, issue 12 section 8.4), which the door reads into the snapshot's HCD
 * region (hcd_door.c) and XHCISNAP decodes.
 *
 * Issue 12's dumps showed one bulk transfer outstanding for good after its
 * endpoint stalled, and could not say why: the snapshot held neither the
 * pipes nor the recovery the class driver asked for. Two records are kept
 * here. The timeline: each slow request (RESET_PIPE and its two halves,
 * ABORT_PIPE, RESET_PORT, a select, a streams request) from its queueing to
 * its completion, with what became of CLEAR_FEATURE(ENDPOINT_HALT) and the
 * commands the thread issued for it; each transfer cancel routine's run;
 * and the thread's own cancel, drain and removal aborts of a pipe. The
 * backstop samples: the interrupter's registers each time the lost-
 * interrupt backstop queues a drain. The pipes themselves are read at dump
 * time by the door.
 *
 * Nothing here decides anything: every function records and returns, and
 * no caller reads a record back. The layouts are xhci.h's
 * (XHCI_SNAPSHOT_REC_*, _IRQ_*), the ring arithmetic xhci_snap.c's. Each
 * write is under the controller lock (hcd.h, HCD_DIAG); the system time is
 * read inside it, which KeQuerySystemTime allows at DISPATCH_LEVEL.
 *
 * IRQL: each function carries its own.
 */

#include <ntddk.h>
#include <usbdi.h>
#include <usbioctl.h>
#include "hcd.h"
#include "xhci_hw.h"
#include "xhci98_streams.h"

/* A SlowIrps walk's bound: the list is a few entries in practice, and a
 * corrupted one must not hold the lock for long. */
#define HCD_DIAG_WALK_MAX 1024UL

/* IRQL: PASSIVE_LEVEL, at a start before the interrupt connects; a stamp
 * the ISR or the DPC of the previous start lands meanwhile is harmless. */
VOID HcdDiagStart(PHCD_CONTROLLER hc)
{
    PUCHAR p;
    ULONG i;

    p = (PUCHAR)&hc->Diag;
    for (i = 0; i < sizeof(HCD_DIAG); i++) {
        p[i] = 0;
    }
}

/* The tolerance clock as the records stamp it (XhciSnapTick): never 0,
 * so 0 stays "never" even in the clock's first 100 ms. IRQL: any - the ISR
 * stamps with it. */
ULONG HcdDiagTick(PHCD_CONTROLLER hc)
{
    return XhciSnapTick(HcdTolNow(hc));
}

/* The system time in milliseconds, its low 32 bits. IRQL: <= DISPATCH. */
ULONG HcdDiagMs(VOID)
{
    LARGE_INTEGER now;

    KeQuerySystemTime(&now);
    return XhciSnapMs((ULONG)now.HighPart, now.LowPart);
}

/* What a slow IRP asks for (XHCI_SNAPSHOT_RK_*), as hcd_cfg.c serves it:
 * by the control code first, as hcdCfgUrbOf tells a URB from RESET_PORT.
 * IRQL: any; the IRP is the driver's. */
ULONG HcdDiagSlowKind(PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PURB urb;
    ULONG code;

    stack = IoGetCurrentIrpStackLocation(irp);
    code = stack->Parameters.DeviceIoControl.IoControlCode;
    if (code == IOCTL_XHCI98_OPEN_STREAMS ||
        code == IOCTL_XHCI98_CLOSE_STREAMS) {
        return XHCI_SNAPSHOT_RK_STREAMS;
    }
    if (code != IOCTL_INTERNAL_USB_SUBMIT_URB) {
        return XHCI_SNAPSHOT_RK_RESET_PORT;
    }
    urb = (PURB)stack->Parameters.Others.Argument1;
    switch (urb->UrbHeader.Function) {
    case URB_FUNCTION_SELECT_CONFIGURATION:
        return XHCI_SNAPSHOT_RK_SELECT_CONFIG;
    case URB_FUNCTION_SELECT_INTERFACE:
        return XHCI_SNAPSHOT_RK_SELECT_IFACE;
    case URB_FUNCTION_ABORT_PIPE:
        return XHCI_SNAPSHOT_RK_ABORT_PIPE;
    case URB_FUNCTION_RESET_PIPE:
        return XHCI_SNAPSHOT_RK_RESET_PIPE;
    case XHCI_PIPE_URB_SYNC_RESET_PIPE:
        return XHCI_SNAPSHOT_RK_SYNC_RESET;
    case XHCI_PIPE_URB_SYNC_CLEAR_STALL:
        return XHCI_SNAPSHOT_RK_SYNC_CLEAR;
    default:
        return XHCI_SNAPSHOT_RK_OTHER;
    }
}

/* XhciSnapClearOf (xhci_snap.c) takes the HCD_CTL_* codes by value. */
typedef char hcdDiagCtlOrder[HCD_CTL_DONE == 0 && HCD_CTL_STALLED == 1 &&
                                     HCD_CTL_FAILED == 2 &&
                                     HCD_CTL_NOT_SENT == 3 &&
                                     HCD_CTL_ABANDONED == 4
                                 ? 1
                                 : -1];

/* XhciSnapHoldOf (xhci_snap.c) takes the HCD_IO_PARK_* codes by value. */
typedef char hcdDiagParkOrder[HCD_IO_PARK_DECLINED == 0 &&
                                      HCD_IO_PARK_HELD == 1 &&
                                      HCD_IO_PARK_COMPLETED == 2
                                  ? 1
                                  : -1];

/* A thread control transfer's outcome (HCD_CTL_*) as REC_CLEAR records it.
 * IRQL: any. */
ULONG HcdDiagClearOf(ULONG outcome)
{
    return XhciSnapClearOf(outcome);
}

/* SlowIrps' length, bounded. Controller lock held. */
static ULONG hcdDiagSlowDepthLocked(PHCD_CONTROLLER hc)
{
    PLIST_ENTRY entry;
    ULONG n;

    n = 0;
    for (entry = hc->SlowIrps.Flink;
         entry != &hc->SlowIrps && n < HCD_DIAG_WALK_MAX;
         entry = entry->Flink) {
        n++;
    }
    return n;
}

/* A new timeline entry, stamped now; its number, never 0. Controller lock
 * held. */
ULONG HcdDiagRecOpenLocked(PHCD_CONTROLLER hc, ULONG kind, ULONG slot,
                           ULONG dci, ULONG detail)
{
    PULONG e;

    e = XhciSnapRingNext(hc->Diag.Recs, XHCI_SNAPSHOT_HCD_RECS_KEPT,
                         XHCI_SNAPSHOT_REC_WORDS, &hc->Diag.RecTotal);
    e[XHCI_SNAPSHOT_REC_KIND] = kind;
    e[XHCI_SNAPSHOT_REC_SLOT] = slot;
    e[XHCI_SNAPSHOT_REC_DCI] = dci;
    e[XHCI_SNAPSHOT_REC_ARRIVE_MS] = HcdDiagMs();
    e[XHCI_SNAPSHOT_REC_DEPTH] = hcdDiagSlowDepthLocked(hc);
    e[XHCI_SNAPSHOT_REC_DETAIL] = detail;
    e[XHCI_SNAPSHOT_REC_TICK] = HcdDiagTick(hc);
    return e[XHCI_SNAPSHOT_REC_SEQ];
}

/*
 * The thread takes up entry `seq` - a slow IRP's, opened when it was
 * queued - or, for its own work (seq 0) or an entry the ring has since
 * overwritten, a new one, whose arrival is then unknown for a slow IRP. It
 * is the thread's current entry until HcdDiagRecEnd: the commands and the
 * clear below land in it. Returns its number. Thread only, no lock held:
 * IRQL PASSIVE_LEVEL.
 */
ULONG HcdDiagRecBegin(PHCD_CONTROLLER hc, ULONG seq, ULONG kind, ULONG slot,
                      ULONG dci)
{
    PULONG e;
    KIRQL oldIrql;
    ULONG now;
    ULONG cur;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    now = HcdDiagMs();
    e = XhciSnapRingFind(hc->Diag.Recs, XHCI_SNAPSHOT_HCD_RECS_KEPT,
                         XHCI_SNAPSHOT_REC_WORDS, seq);
    if (e == NULL) {
        e = XhciSnapRingFind(hc->Diag.Recs, XHCI_SNAPSHOT_HCD_RECS_KEPT,
                             XHCI_SNAPSHOT_REC_WORDS,
                             HcdDiagRecOpenLocked(hc, kind, slot, dci, 0));
        if (seq != 0) {
            e[XHCI_SNAPSHOT_REC_ARRIVE_MS] = 0;
            e[XHCI_SNAPSHOT_REC_DEPTH] = 0;
        }
    }
    if (seq != 0) {
        hc->Diag.SlowServed++;
    }
    e[XHCI_SNAPSHOT_REC_START_MS] = (now != 0) ? now : 1UL;
    cur = e[XHCI_SNAPSHOT_REC_SEQ];
    hc->Diag.RecCur = cur;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return cur;
}

/* CLEAR_FEATURE(ENDPOINT_HALT)'s outcome (XHCI_SNAPSHOT_CLEAR_*) into the
 * current entry. Thread only, no lock held. */
VOID HcdDiagRecClear(PHCD_CONTROLLER hc, ULONG clear)
{
    PULONG e;
    KIRQL oldIrql;

    if (hc->Diag.RecCur == 0) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    e = XhciSnapRingFind(hc->Diag.Recs, XHCI_SNAPSHOT_HCD_RECS_KEPT,
                         XHCI_SNAPSHOT_REC_WORDS, hc->Diag.RecCur);
    if (e != NULL) {
        e[XHCI_SNAPSHOT_REC_CLEAR] = clear;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* A command the thread issued, and its completion code (0: refused, lost
 * or timed out), into the current entry. Thread only, no lock held. */
VOID HcdDiagRecCommand(PHCD_CONTROLLER hc, ULONG trbType, ULONG code)
{
    PULONG e;
    KIRQL oldIrql;

    if (hc->Diag.RecCur == 0) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    e = XhciSnapRingFind(hc->Diag.Recs, XHCI_SNAPSHOT_HCD_RECS_KEPT,
                         XHCI_SNAPSHOT_REC_WORDS, hc->Diag.RecCur);
    if (e != NULL) {
        XhciSnapRecCommand(e, trbType, code);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* Entry `seq` completed with `usbd`; `detail` replaces its detail word
 * when not 0, and it is no longer current. Thread only, no lock held. */
VOID HcdDiagRecEnd(PHCD_CONTROLLER hc, ULONG seq, LONG usbd, ULONG detail)
{
    PULONG e;
    KIRQL oldIrql;
    ULONG now;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    now = HcdDiagMs();
    e = XhciSnapRingFind(hc->Diag.Recs, XHCI_SNAPSHOT_HCD_RECS_KEPT,
                         XHCI_SNAPSHOT_REC_WORDS, seq);
    if (e != NULL) {
        e[XHCI_SNAPSHOT_REC_END_MS] = (now != 0) ? now : 1UL;
        e[XHCI_SNAPSHOT_REC_USBD] = (ULONG)usbd;
        if (detail != 0) {
            e[XHCI_SNAPSHOT_REC_DETAIL] = detail;
        }
    }
    if (seq != 0 && hc->Diag.RecCur == seq) {
        hc->Diag.RecCur = 0;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* A slow request's entry left open: held on its PDO, or put back for the
 * recovery (XHCI_SNAPSHOT_RD_*), and no longer current. Thread only, no
 * lock held. */
VOID HcdDiagRecHold(PHCD_CONTROLLER hc, ULONG seq, ULONG detail)
{
    PULONG e;
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    e = XhciSnapRingFind(hc->Diag.Recs, XHCI_SNAPSHOT_HCD_RECS_KEPT,
                         XHCI_SNAPSHOT_REC_WORDS, seq);
    if (e != NULL) {
        e[XHCI_SNAPSHOT_REC_DETAIL] = detail;
    }
    if (seq != 0 && hc->Diag.RecCur == seq) {
        hc->Diag.RecCur = 0;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* A backstop sample taken into `sample` (XHCI_SNAPSHOT_IRQ_WORDS, its
 * number left 0): `regs` USBCMD, USBSTS, IMAN, IMOD and ERDP as the
 * backstop read them, the event ring's software dequeue (index | Ccs <<
 * 31) and the DW3 of the TRB there. Kept only by HcdDiagIrqCommitLocked,
 * once the drain was queued. Controller lock held. */
VOID HcdDiagIrqTakeLocked(PHCD_CONTROLLER hc, PULONG sample,
                          const ULONG *regs, ULONG dequeue, ULONG dw3)
{
    PULONG e;

    e = sample;
    e[XHCI_SNAPSHOT_IRQ_SEQ] = 0;
    e[XHCI_SNAPSHOT_IRQ_MS] = HcdDiagMs();
    e[XHCI_SNAPSHOT_IRQ_TICK] = HcdDiagTick(hc);
    e[XHCI_SNAPSHOT_IRQ_USBCMD] = regs[0];
    e[XHCI_SNAPSHOT_IRQ_USBSTS] = regs[1];
    e[XHCI_SNAPSHOT_IRQ_IMAN] = regs[2];
    e[XHCI_SNAPSHOT_IRQ_IMOD] = regs[3];
    e[XHCI_SNAPSHOT_IRQ_ERDP] = regs[4];
    e[XHCI_SNAPSHOT_IRQ_EVT_DEQUEUE] = dequeue;
    e[XHCI_SNAPSHOT_IRQ_EVT_DW3] = dw3;
    e[XHCI_SNAPSHOT_IRQ_ISR_TICK] = hc->Diag.IsrTick;
    e[XHCI_SNAPSHOT_IRQ_CLAIM_TICK] = hc->Diag.ClaimTick;
    e[XHCI_SNAPSHOT_IRQ_DPC_TICK] = hc->Diag.DpcTick;
    e[XHCI_SNAPSHOT_IRQ_INTERRUPTS] = hc->Hc.InterruptCount;
    e[XHCI_SNAPSHOT_IRQ_CLAIMED] = hc->Hc.InterruptsClaimed;
    e[XHCI_SNAPSHOT_IRQ_DPCS] = hc->Hc.DpcCount;
}

/* A sample kept in the ring, numbered there. Controller lock held. */
VOID HcdDiagIrqCommitLocked(PHCD_CONTROLLER hc, const ULONG *sample)
{
    PULONG e;
    ULONG i;

    e = XhciSnapRingNext(hc->Diag.Irqs, XHCI_SNAPSHOT_HCD_IRQS_KEPT,
                         XHCI_SNAPSHOT_IRQ_WORDS, &hc->Diag.IrqTotal);
    for (i = 1; i < XHCI_SNAPSHOT_IRQ_WORDS; i++) {
        e[i] = sample[i];
    }
}

/* The slow queue as the dump finds it: its length, and the first IRP's kind
 * and timeline entry (HcdCfgQueue keeps the entry's number in its
 * DriverContext[3] while it is queued). Controller lock held. */
VOID HcdDiagSlowLook(PHCD_CONTROLLER hc, PULONG depth, PULONG kind,
                     PULONG seq)
{
    PIRP irp;

    *depth = hcdDiagSlowDepthLocked(hc);
    *kind = 0;
    *seq = 0;
    if (!IsListEmpty(&hc->SlowIrps)) {
        irp = CONTAINING_RECORD(hc->SlowIrps.Flink, IRP,
                                Tail.Overlay.ListEntry);
        *kind = HcdDiagSlowKind(irp);
        *seq = (ULONG)(ULONG_PTR)irp->Tail.Overlay.DriverContext[3];
    }
}

/* The device PDOs: how many are listed, and the URBs parked and pending on
 * every PDO not yet deleted. PdoListLock is taken here, so the caller holds
 * no lock. IRQL: <= DISPATCH_LEVEL. */
VOID HcdDiagPdos(PHCD_CONTROLLER hc, PULONG pdos, PULONG parked,
                 PULONG pending)
{
    PHCD_DEVICE_PDO lists[3];
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;
    ULONG i;

    *pdos = 0;
    *parked = 0;
    *pending = 0;
    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    lists[0] = hc->DevicePdos;
    lists[1] = hc->GonePdos;
    lists[2] = hc->RemovedPdos;
    for (i = 0; i < 3; i++) {
        for (pdo = lists[i]; pdo != NULL; pdo = pdo->Next) {
            if (i == 0) {
                (*pdos)++;
            }
            *parked += pdo->ParkedCount;
            *pending += (ULONG)pdo->UrbsPending;
        }
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
}
