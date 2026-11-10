/*
 * hcd_stage.c - every transfer's data staged through buffers the driver
 * owns (task 48.33; design record 13 section 11.11, record 17 section
 * 4.13.8).
 *
 * The controller never names a client page: every data TRB points at
 * common buffer of this driver's - a record's small buffer, slabs of the
 * shared pool, its pipe's reserve, the controller's control slab or an
 * isochronous share - and the client's bytes are copied in before an OUT
 * is published and out after an IN retires. So a stop that cannot prove
 * the controller stopped mastering keeps only this driver's memory, and
 * completes every client request as a proven stop does.
 *
 * Admission (section 11.11.3). A TD waiting for staging is on StageQueue,
 * in arrival order. One admission pass runs at a time (StagePassBusy; a
 * caller finding one running asks it to go round again): it takes the
 * controller lock and then StageLock, picks the first waiting TD whose
 * pipe is not held back in this pass, and gives it all the staging it
 * needs in one step - its small buffer, else slabs of the shared pool,
 * else its pipe's reserve or the control slab - or holds its pipe back
 * for the rest of the pass, so no later TD of that pipe moves past it.
 * A request that may need several TDs owns its pipe from its start
 * (Exclusive, decided in hcd_io.c from the pipe's reserve, HcdStageMulti);
 * any other is admitted whole or not at all, so it never becomes one. The
 * OUT copy runs after both locks are released, then the TD is published
 * (HcdIoMapped). Passes run at submission and from StageDpc when staging
 * comes back.
 *
 * The copies (section 11.11.4). A URB with a TransferBuffer, or an MDL
 * already mapped or built for non-paged pool, is copied through its
 * system address. Any other MDL is copied through a window of at most
 * 64 KiB: a partial MDL the driver owns (IoBuildPartialMdl over the
 * client's MDL), mapped and unmapped at once; the client's MDL is never
 * changed (Q-STG.1, the owner's answer (a)). A window that will not map
 * is never a reason to go direct: an OUT goes back to the head of the
 * queue, an IN's copy-out waits on StageCopyQueue holding its staging,
 * both retried from StageRetryTimer (10 ms, doubling to 1 s) and ended
 * NO_MEMORY after HCD_STAGE_MAP_GIVEUP_MS.
 *
 * Locks: the controller lock, then StageLock, never the reverse; no copy
 * runs under either. IRQL: <= DISPATCH_LEVEL unless a function says
 * otherwise.
 */

#include <ntddk.h>
#include <usbdi.h>
#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_xfer.h"
#include "xhci_stage.h"
#include "xhci_dbg.h"

#ifndef MDL_MAPPING_CAN_FAIL
#define MDL_MAPPING_CAN_FAIL 0x0040
#endif

/* A mapping that has failed for this long ends its request NO_MEMORY. */
#define HCD_STAGE_MAP_GIVEUP_MS 5000UL
/* The retry timer's first period and its ceiling. */
#define HCD_STAGE_RETRY_FIRST_MS 10UL
#define HCD_STAGE_RETRY_LAST_MS 1000UL
/* Bytes one pass copies before it hands the rest to StageDpc. */
#define HCD_STAGE_PASS_BYTES 0x100000UL
/* A window MDL's pages: 64 KiB from any page offset. */
#define HCD_STAGE_WINDOW_PAGES (HCD_STAGE_WINDOW / PAGE_SIZE + 1UL)

static VOID NTAPI hcdStageDpc(PKDPC Dpc, PVOID Context, PVOID Arg1,
                              PVOID Arg2);
static VOID NTAPI hcdStageTimerDpc(PKDPC Dpc, PVOID Context, PVOID Arg1,
                                   PVOID Arg2);
static VOID hcdStagePass(PHCD_CONTROLLER hc);

/* ----------------------------------------------------------------------- */
/* Byte moves                                                               */
/* ----------------------------------------------------------------------- */

/* No memcpy import is admitted (the import gate). IRQL: any. */
#if defined(_M_AMD64)
void __movsb(unsigned char *dst, unsigned char const *src, size_t n);
#pragma intrinsic(__movsb)

static VOID hcdStageBytes(PUCHAR dst, const UCHAR *src, ULONG n)
{
    __movsb(dst, src, (size_t)n);
}
#else
static VOID hcdStageBytes(PUCHAR dst, const UCHAR *src, ULONG n)
{
    __asm {
        push esi
        push edi
        mov esi, src
        mov edi, dst
        mov ecx, n
        mov edx, ecx
        shr ecx, 2
        cld
        rep movsd
        mov ecx, edx
        and ecx, 3
        rep movsb
        pop edi
        pop esi
    }
}
#endif

/* `n` bytes between the TD's staging at `stageOff` and `client`, in the
 * direction `toStage` (an OUT's fill) or back (an IN's copy-out). */
static VOID hcdStageMove(PHCD_XFER x, ULONG stageOff, PUCHAR client, ULONG n,
                         ULONG toStage)
{
    ULONG seg;
    ULONG within;
    ULONG piece;
    PUCHAR s;

    while (n > 0) {
        seg = stageOff / x->StageSegBytes;
        within = stageOff % x->StageSegBytes;
        piece = x->StageSegBytes - within;
        if (piece > n) {
            piece = n;
        }
        s = x->StageSeg[seg] + within;
        if (toStage) {
            hcdStageBytes(s, client, piece);
        } else {
            hcdStageBytes(client, s, piece);
        }
        stageOff += piece;
        client += piece;
        n -= piece;
    }
}

/* ----------------------------------------------------------------------- */
/* The client's buffer: its system address, or a window                     */
/* ----------------------------------------------------------------------- */

/* The client buffer's own system address, NULL when it must be mapped. */
static PUCHAR hcdStageDirect(PHCD_XFER x)
{
    if (x->SysVa != NULL) {
        return x->SysVa;
    }
    if (x->Mdl != NULL &&
        (x->Mdl->MdlFlags &
         (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL)) != 0) {
        return (PUCHAR)x->Mdl->MappedSystemVa;
    }
    return NULL;
}

#if defined(XHCI_FLAVOUR_QEMU)
/*
 * The qemu flavour's reading of where a copy's client bytes are reached
 * (record 13 section 11.11.11): each source counted, the first addresses
 * of each printed on the 0xE9 channel, and those below 0x80000000 counted,
 * since a copy runs in whatever process context the DPC finds and only a
 * system address is valid in every one. XhciQemuStageFault bit 1 sends an
 * MDL's copy through a window on Windows 98 and ME even when it has a
 * system address (`*direct` cleared), so that MmMapLockedPages and the
 * probe are reached there. IRQL: <= DISPATCH_LEVEL, no lock held.
 */
static VOID hcdStageQemuDirect(PHCD_CONTROLLER hc, PHCD_XFER x,
                               PUCHAR *direct)
{
    if (*direct == NULL) {
        return;
    }
    if (x->SysVa != NULL) {
        (VOID)InterlockedIncrement((PLONG)&hc->StageDirectBuffer);
        XHCI_DBG_VALUE_LIMITED("stage.direct: TransferBuffer", *direct);
    } else {
        (VOID)InterlockedIncrement((PLONG)&hc->StageDirectMdl);
        XHCI_DBG_VALUE_LIMITED("stage.direct: MDL flags", x->Mdl->MdlFlags);
        XHCI_DBG_VALUE_LIMITED("stage.direct: MDL MappedSystemVa", *direct);
    }
#if !defined(_WIN64)
    if ((ULONG)(ULONG_PTR)*direct < 0x80000000UL) {
        (VOID)InterlockedIncrement((PLONG)&hc->StageDirectLow);
        XHCI_DBG_VALUE_LIMITED("stage.direct: LOW address", *direct);
    }
#endif
    if (x->SysVa == NULL && !hc->StageNt &&
        (hc->QemuStageFault & HCD_STAGE_QEMU_WINDOW98) != 0) {
        *direct = NULL;
    }
}
#endif

/* A free window MDL, or -1. */
static LONG hcdStageWinTake(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    LONG w;
    ULONG i;

    w = -1;
    KeAcquireSpinLock(&hc->StageLock, &oldIrql);
    for (i = 0; i < HCD_STAGE_WINDOWS; i++) {
        if (hc->StageWin[i] != NULL && !hc->StageWinBusy[i]) {
            hc->StageWinBusy[i] = 1;
            w = (LONG)i;
            break;
        }
    }
    KeReleaseSpinLock(&hc->StageLock, oldIrql);
    return w;
}

static VOID hcdStageWinGive(PHCD_CONTROLLER hc, LONG w)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->StageLock, &oldIrql);
    hc->StageWinBusy[w] = 0;
    KeReleaseSpinLock(&hc->StageLock, oldIrql);
}

/*
 * The window's system address, NULL when it will not map (Q-STG.1): on
 * amd64 MmMapLockedPagesSpecifyCache, which returns NULL rather than
 * stopping the system when its last but one argument says so; on x86
 * under NT MmMapLockedPages with MDL_MAPPING_CAN_FAIL set on the driver's
 * own window MDL, the WDM 1.0 way to the same; on Windows 98 and ME
 * MmMapLockedPages, whose failure NTKERN's leg reads (record 13 section
 * 11.11.4). IRQL: <= DISPATCH_LEVEL.
 */
static PUCHAR hcdStageMap(PHCD_CONTROLLER hc, PMDL win)
{
#if defined(_WIN64)
    UNREFERENCED_PARAMETER(hc);
    return (PUCHAR)MmMapLockedPagesSpecifyCache(win, KernelMode, MmCached,
                                                NULL, FALSE,
                                                NormalPagePriority);
#else
    PUCHAR va;

    if (hc->StageNt) {
        win->MdlFlags |= MDL_MAPPING_CAN_FAIL;
        va = (PUCHAR)MmMapLockedPages(win, KernelMode);
        win->MdlFlags &= ~MDL_MAPPING_CAN_FAIL;
        return va;
    }
    return (PUCHAR)MmMapLockedPages(win, KernelMode);
#endif
}

#if defined(XHCI_FLAVOUR_QEMU)
/*
 * XhciQemuStageMapProbe (qemu only; record 13 section 11.11.4): how many
 * mappings of a window the system holds at once before its mapping call
 * fails, which no leg reaches on its own. Each of the probe's own window
 * MDLs is built over the same span of the client's MDL and mapped once,
 * none unmapped, until a mapping returns NULL or the MDLs run out; the
 * count and the outcome go to the 0xE9 channel and the log, and each MDL
 * that holds a mapping is unmapped through itself, once, before the copy
 * goes on. Once per start, from the first window copy. IRQL:
 * <= DISPATCH_LEVEL, no lock held.
 */
static VOID hcdStageMapProbe(PHCD_CONTROLLER hc, PMDL client, PVOID base,
                             ULONG n)
{
    PUCHAR va;
    ULONG count;
    ULONG outcome;

    if (hc->StageProbeVa == NULL ||
        InterlockedExchange((PLONG)&hc->StageProbeArmed, 0) == 0) {
        return;
    }
    count = 0;
    outcome = 2;
    XHCI_DBG_VALUE("stage.probe: window bytes", n);
    XHCI_DBG_VALUE("stage.probe: MDLs", hc->StageProbeMdls);
    while (count < hc->StageProbeMdls) {
        IoBuildPartialMdl(client, hc->StageProbeMdl[count], base, n);
        if (!hc->StageNt &&
            (hc->QemuStageFault & HCD_STAGE_QEMU_WINDOW98) != 0) {
            hc->StageProbeMdl[count]->MdlFlags &=
                ~(MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL);
        }
        va = hcdStageMap(hc, hc->StageProbeMdl[count]);
        if (va == NULL) {
            outcome = 1;
            break;
        }
        hc->StageProbeVa[count] = va;
        count++;
    }
    XHCI_DBG_VALUE("stage.probe: maps held", count);
    XHCI_DBG_VALUE("stage.probe: outcome (1 NULL, 2 MDLs ran out)",
                   outcome);
    XHCI_DBG_VALUE("stage.probe: first",
                   count != 0 ? hc->StageProbeVa[0] : NULL);
    XHCI_DBG_VALUE("stage.probe: last",
                   count != 0 ? hc->StageProbeVa[count - 1] : NULL);
    XhciLogNote(&hc->Hc, "stage.probe.maps", count);
    XhciLogNote(&hc->Hc, "stage.probe.outcome", outcome);
    while (count > 0) {
        count--;
        MmUnmapLockedPages(hc->StageProbeVa[count], hc->StageProbeMdl[count]);
    }
    XHCI_DBG_TEXT("stage.probe: every mapping held unmapped");
}

/* The probe's MDLs and address table at a start (none when the switch is
 * 0 or the tables cannot be had), and back at the stop. IRQL:
 * PASSIVE_LEVEL. */
static VOID hcdStageProbeOpen(PHCD_CONTROLLER hc)
{
    ULONG i;

    if (hc->QemuMapProbe == 0) {
        return;
    }
    hc->StageProbeMdl = (PMDL *)HcdPoolAlloc(hc->QemuMapProbe *
                                             (ULONG)sizeof(PMDL));
    hc->StageProbeVa = (PVOID *)HcdPoolAlloc(hc->QemuMapProbe *
                                             (ULONG)sizeof(PVOID));
    if (hc->StageProbeMdl == NULL || hc->StageProbeVa == NULL) {
        HcdPoolFree(hc->StageProbeMdl);
        HcdPoolFree(hc->StageProbeVa);
        hc->StageProbeMdl = NULL;
        hc->StageProbeVa = NULL;
        return;
    }
    for (i = 0; i < hc->QemuMapProbe; i++) {
        hc->StageProbeMdl[i] = HcdPoolWindowMdl(HCD_STAGE_WINDOW_PAGES *
                                                PAGE_SIZE);
        if (hc->StageProbeMdl[i] == NULL) {
            break;
        }
    }
    hc->StageProbeMdls = i;
    (VOID)InterlockedExchange((PLONG)&hc->StageProbeArmed,
                              i != 0 ? 1L : 0L);
}

static VOID hcdStageProbeClose(PHCD_CONTROLLER hc)
{
    ULONG i;

    (VOID)InterlockedExchange((PLONG)&hc->StageProbeArmed, 0);
    for (i = 0; hc->StageProbeMdl != NULL && i < hc->StageProbeMdls; i++) {
        HcdPoolWindowMdlFree(hc->StageProbeMdl[i]);
    }
    HcdPoolFree(hc->StageProbeMdl);
    HcdPoolFree(hc->StageProbeVa);
    hc->StageProbeMdl = NULL;
    hc->StageProbeVa = NULL;
    hc->StageProbeMdls = 0;
}
#endif

/*
 * Move `bytes` between the client's buffer at `clientOff` and the TD's
 * staging at `stageOff`. Returns 1, or 0 when a window was not free or
 * would not map: what was moved before stays moved, and the caller
 * retries the whole copy. The caller is counted in StageOwners. IRQL:
 * <= DISPATCH_LEVEL, no lock held.
 */
static ULONG hcdStageCopy(PHCD_CONTROLLER hc, PHCD_XFER x, ULONG toStage,
                          ULONG clientOff, ULONG stageOff, ULONG bytes)
{
    PUCHAR direct;
    PUCHAR va;
    PUCHAR base;
    KIRQL oldIrql;
    ULONG lo0;
    ULONG hi0;
    ULONG lo1;
    ULONG hi1;
    ULONG ticks;
    ULONG moved;
    ULONG n;
    ULONG ok;
    LONG w;

    if (bytes == 0) {
        return 1;
    }
    HcdTscRead(&lo0, &hi0);
    direct = hcdStageDirect(x);
#if defined(XHCI_FLAVOUR_QEMU)
    hcdStageQemuDirect(hc, x, &direct);
#endif
    moved = 0;
    ok = 1;
    while (moved < bytes) {
        if (direct != NULL) {
            hcdStageMove(x, stageOff + moved, direct + clientOff + moved,
                         bytes - moved, toStage);
            moved = bytes;
            break;
        }
        n = bytes - moved;
        if (n > HCD_STAGE_WINDOW) {
            n = HCD_STAGE_WINDOW;
        }
        w = hcdStageWinTake(hc);
        if (w < 0) {
            ok = 0;
            break;
        }
        va = NULL;
#if defined(XHCI_FLAVOUR_QEMU)
        if (hc->StageMapInject != 0) {
            /* XhciQemuStageFault bits 15:8: the next mappings fail (255
             * every one). */
            if (hc->StageMapInject != 0xFFUL) {
                hc->StageMapInject--;
            }
            hcdStageWinGive(hc, w);
            ok = 0;
            break;
        }
#endif
        base = (PUCHAR)MmGetMdlVirtualAddress(x->Mdl) + clientOff + moved;
#if defined(XHCI_FLAVOUR_QEMU)
        hcdStageMapProbe(hc, x->Mdl, base, n);
#endif
        IoBuildPartialMdl(x->Mdl, hc->StageWin[w], base, n);
#if defined(XHCI_FLAVOUR_QEMU)
        /* XhciQemuStageFault bit 1: the window mapped through its pages,
         * not through a system address the client's MDL carried. */
        if (!hc->StageNt &&
            (hc->QemuStageFault & HCD_STAGE_QEMU_WINDOW98) != 0) {
            hc->StageWin[w]->MdlFlags &=
                ~(MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL);
        }
#endif
        va = hcdStageMap(hc, hc->StageWin[w]);
        if (va == NULL) {
            hcdStageWinGive(hc, w);
            ok = 0;
            break;
        }
        hcdStageMove(x, stageOff + moved, va, n, toStage);
        MmUnmapLockedPages(va, hc->StageWin[w]);
        hcdStageWinGive(hc, w);
        KeAcquireSpinLock(&hc->StageLock, &oldIrql);
        hc->StageWindowMaps++;
        hc->StageRetryMs = HCD_STAGE_RETRY_FIRST_MS;
        KeReleaseSpinLock(&hc->StageLock, oldIrql);
        moved += n;
    }
    HcdTscRead(&lo1, &hi1);
    /* In units of 1024 counts; a pair across a wrap of the high word is
     * dropped (HcdTscRead's counts are per processor). */
    ticks = (hi1 == hi0 && lo1 >= lo0) ? ((lo1 - lo0) >> 10) : 0UL;
    KeAcquireSpinLock(&hc->StageLock, &oldIrql);
    if (moved != 0) {
        hc->StageCopyBytesLo += moved;
        if (hc->StageCopyBytesLo < moved) {
            hc->StageCopyBytesHi++;
        }
    }
    hc->StageCopyTicks += ticks;
    if (ticks > hc->StageCopyLongest) {
        hc->StageCopyLongest = ticks;
    }
    if (!ok) {
        hc->StageMapFails++;
    }
    KeReleaseSpinLock(&hc->StageLock, oldIrql);
    /* What moved before a failure counts against a pass's bound too. */
    x->StageMoved += moved;
    return ok;
}

/* ----------------------------------------------------------------------- */
/* The DPC and the retry timer                                              */
/* ----------------------------------------------------------------------- */

/* An admission pass from StageDpc, counted at queue time (the stop's
 * drain waits the count out). */
static VOID hcdStageKick(PHCD_CONTROLLER hc)
{
    (VOID)InterlockedIncrement((PLONG)&hc->StageDpcsInFlight);
    if (!KeInsertQueueDpc(&hc->StageDpc, NULL, NULL)) {
        (VOID)InterlockedDecrement((PLONG)&hc->StageDpcsInFlight);
    }
}

/* The retry timer, counted from its setting; not once admission is
 * closed. StageLock held. */
static VOID hcdStageArmLocked(PHCD_CONTROLLER hc)
{
    LARGE_INTEGER due;

    if (hc->StageClosed || hc->StageTimerArmed) {
        return;
    }
    hc->StageTimerArmed = 1;
    (VOID)InterlockedIncrement((PLONG)&hc->StageTimerSet);
    HcdRelativeMs(&due, hc->StageRetryMs);
    (VOID)KeSetTimer(&hc->StageTimer, due, &hc->StageTimerDpc);
    if (hc->StageRetryMs < HCD_STAGE_RETRY_LAST_MS) {
        hc->StageRetryMs *= 2UL;
        if (hc->StageRetryMs > HCD_STAGE_RETRY_LAST_MS) {
            hc->StageRetryMs = HCD_STAGE_RETRY_LAST_MS;
        }
    }
}

/* Whether a record's mappings have failed for HCD_STAGE_MAP_GIVEUP_MS,
 * stamping its first failure: measured on the controller's tolerance clock
 * (hcd_svc.c, HcdTolNow; XHCI_TOL_TICK_MS a tick), which counts elapsed
 * time no wall-clock change moves, the difference wrap-safe. */
static ULONG hcdStageGiveUp(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    ULONG now;

    now = HcdTolNow(hc);
    if (x->StageMapFails++ == 0) {
        x->StageFailAt = now;
        return 0;
    }
    return ((now - x->StageFailAt) * XHCI_TOL_TICK_MS >=
            HCD_STAGE_MAP_GIVEUP_MS) ? 1UL : 0UL;
}

/*
 * A record whose copy failed: whether it must end now instead of waiting
 * for a window - admission closed, its device gone, its pipe closed, its
 * IRP cancelled or the shutdown latched, any of which no retry outlives,
 * or, for a copy that failed (`failed`), the give-up reached - and the
 * status it ends with. Decided under the
 * controller lock, which every drain and cancel holds while it marks a
 * record and walks the pipe's records (HcdIoWideStop): either the record
 * is queued before that walk, which then finds it, or this sees the mark.
 * Controller lock and StageLock held.
 */
static ULONG hcdStageMustEnd(PHCD_CONTROLLER hc, PHCD_XFER x, ULONG failed,
                             PLONG usbd)
{
    PHCD_PIPE pipe;

    pipe = x->Pipe;
    if (pipe->Device->Gone) {
        *usbd = HCD_USBD_DEVICE_GONE;
    } else if (x->CancelRequested) {
        *usbd = HCD_XFER_CANCEL_STATUS(x);
    } else if (pipe->Closed) {
        *usbd = HCD_USBD_CANCELED;
    } else if (hc->ShutLatched || hc->StageClosed) {
        *usbd = XHCI_USBD_STATUS_DEV_NOT_RESPONDING;
    } else if (failed && hcdStageGiveUp(hc, x)) {
        hc->StageGiveups++;
        *usbd = HCD_USBD_NO_MEMORY;
    } else {
        return 0;
    }
    return 1;
}

/* ----------------------------------------------------------------------- */
/* Taking and giving staging                                                */
/* ----------------------------------------------------------------------- */

/* The record's list rebuilt from its segments for `chunk` bytes. */
static ULONG hcdStageSg(PHCD_XFER x, ULONG chunk)
{
    ULONG n;
    ULONG off;
    ULONG piece;
    ULONG i;

    n = 0;
    off = 0;
    for (i = 0; i < x->StageCount && off < chunk; i++) {
        piece = chunk - off;
        if (piece > x->StageSegBytes) {
            piece = x->StageSegBytes;
        }
        if (!XhciStageRuns(x->Sg.List.SgElement, &n, HCD_SG_ELEMENTS,
                           x->StageSegPa[i], piece, off)) {
            return 0;
        }
        off += piece;
    }
    x->Sg.List.SgElementCount = n;
    return off == chunk;
}

/* One contiguous run of staging as the TD's one segment. */
static VOID hcdStageOne(PHCD_XFER x, ULONG kind, PUCHAR va, ULONG pa,
                        ULONG bytes)
{
    x->StageKind = kind;
    x->StageCount = 1;
    x->StageSegBytes = bytes;
    x->StageSeg[0] = va;
    x->StageSegPa[0] = pa;
}

/*
 * The staging a TD needs, all in one step (section 11.11.3): its small
 * buffer; else slabs of the shared pool; else its pipe's reserve, or for
 * a control data stage longer than its reserve the control slab. A
 * request that does not own its pipe is admitted whole or not at all; a
 * control transfer is never split. Sets Chunk and the list. Returns 0
 * when nothing fits. Controller lock and StageLock held.
 */
static ULONG hcdStageTake(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    PHCD_PIPE pipe;
    ULONG remaining;
    ULONG want;
    ULONG need;
    ULONG take;
    ULONG chunk;
    ULONG excl;
    ULONG mps;
    ULONG used;
    ULONG i;
    ULONG k;

    pipe = x->Pipe;
    remaining = x->Length - x->Offset;
    excl = (pipe->Exclusive == x) ? 1UL : 0UL;
    mps = pipe->MaxPacketSize;
    want = (remaining < XHCI_STAGE_TD_BYTES || x->Control)
               ? remaining
               : XHCI_STAGE_TD_BYTES;

    /* 0. Nothing to move (a zero-length request queued for its place in
     * its pipe's order): admitted with no staging. */
    if (remaining == 0) {
        x->StageKind = HCD_STAGE_K_NONE;
        x->StageCount = 0;
        x->Chunk = 0;
        x->Sg.List.SgElementCount = 0;
        return 1;
    }

    /* 1. The record's own small buffer. */
    if (remaining <= XHCI_STAGE_SMALL && x->SmallVa != NULL) {
        hcdStageOne(x, HCD_STAGE_K_SMALL, x->SmallVa, x->SmallPa,
                    XHCI_STAGE_SMALL);
        x->Chunk = remaining;
        hc->StageSmallTakes++;
        return hcdStageSg(x, x->Chunk);
    }

    /* 2. Slabs of the shared pool: all it needs, or - a request that owns
     * its pipe or fits anyway - what is free. */
    need = (want + XHCI_STAGE_SLAB - 1UL) / XHCI_STAGE_SLAB;
    if (need > HCD_STAGE_TD_SLABS) {
        need = HCD_STAGE_TD_SLABS;
    }
    take = (need <= hc->StageSlabsFree) ? need : hc->StageSlabsFree;
    if (x->Control && take < need) {
        take = 0;
    }
    chunk = (take != 0)
                ? XhciStageChunk(remaining, take * XHCI_STAGE_SLAB, mps,
                                 x->Control)
                : 0;
    if (chunk != 0 && (chunk == remaining || excl) &&
        hc->StageSlabs != NULL) {
        k = 0;
        for (i = 0; i < hc->StageSlabCount && k < take; i++) {
            if (hc->StageSlabs[i].State == HCD_STAGE_SLAB_FREE) {
                hc->StageSlabs[i].State = HCD_STAGE_SLAB_TD;
                x->StageSlab[k] = (USHORT)i;
                x->StageSeg[k] = hc->StageSlabs[i].Va;
                x->StageSegPa[k] = hc->StageSlabs[i].Pa.LowPart;
                k++;
            }
        }
        x->StageKind = HCD_STAGE_K_SHARED;
        x->StageCount = k;
        x->StageSegBytes = XHCI_STAGE_SLAB;
        hc->StageSlabsFree -= k;
        used = hc->StageSlabCount - hc->StageSlabsFree;
        if (used > hc->StageSlabsHigh) {
            hc->StageSlabsHigh = used;
        }
        hc->StageSharedTakes++;
        x->Chunk = XhciStageChunk(remaining, k * XHCI_STAGE_SLAB, mps,
                                  x->Control);
        if (x->Chunk == 0 || !hcdStageSg(x, x->Chunk)) {
            return 0;
        }
        return 1;
    }

    /* 3. The pipe's reserve, or the control slab. */
    if (x->Control && remaining > pipe->ReserveBytes) {
        if (hc->StageCtlVa == NULL || hc->StageCtlBusy ||
            remaining > XHCI_STAGE_SLAB) {
            return 0;
        }
        hc->StageCtlBusy = 1;
        hcdStageOne(x, HCD_STAGE_K_CONTROL, hc->StageCtlVa,
                    hc->StageCtlPa.LowPart, XHCI_STAGE_SLAB);
        x->Chunk = remaining;
        hc->StageReserveTakes++;
        return hcdStageSg(x, x->Chunk);
    }
    if (pipe->ReserveKind == HCD_STAGE_RES_NONE || pipe->ReserveBusy) {
        return 0;
    }
    chunk = XhciStageChunk(remaining, pipe->ReserveBytes, mps, x->Control);
    if (chunk == 0 || (chunk != remaining && !excl)) {
        return 0;
    }
    pipe->ReserveBusy = 1;
    hcdStageOne(x, HCD_STAGE_K_RESERVE, pipe->ReserveVa, pipe->ReservePa,
                pipe->ReserveBytes);
    x->Chunk = chunk;
    hc->StageReserveTakes++;
    return hcdStageSg(x, x->Chunk);
}

/* The TD's staging back. Returns 1 when staging another TD may be waiting
 * for came back. StageLock held. */
static ULONG hcdStageGiveLocked(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    ULONG back;
    ULONG i;
    ULONG s;

    back = 0;
    switch (x->StageKind) {
    case HCD_STAGE_K_SHARED:
        for (i = 0; i < x->StageCount; i++) {
            s = x->StageSlab[i];
            if (hc->StageSlabs != NULL && s < hc->StageSlabCount &&
                hc->StageSlabs[s].State == HCD_STAGE_SLAB_TD) {
                hc->StageSlabs[s].State = HCD_STAGE_SLAB_FREE;
                hc->StageSlabsFree++;
            }
        }
        back = 1;
        break;
    case HCD_STAGE_K_RESERVE:
        x->Pipe->ReserveBusy = 0;
        back = 1;
        break;
    case HCD_STAGE_K_CONTROL:
        hc->StageCtlBusy = 0;
        back = 1;
        break;
    default:
        break;
    }
    x->StageKind = HCD_STAGE_K_NONE;
    x->StageCount = 0;
    return back && !IsListEmpty(&hc->StageQueue);
}

/* ----------------------------------------------------------------------- */
/* Admission                                                                */
/* ----------------------------------------------------------------------- */

/* Whether a request may need several TDs, so owns its pipe from its start
 * (hcd_io.c, hcdGo): longer than its pipe's reserve and its small buffer
 * both, which is all it is sure of. */
ULONG HcdStageMulti(PHCD_XFER x)
{
    ULONG room;

    if (x->Control || x->Isoch || x->Length == 0) {
        return 0;
    }
    room = x->Pipe->ReserveBytes;
    if (x->SmallVa != NULL && room < XHCI_STAGE_SMALL) {
        room = XHCI_STAGE_SMALL;
    }
    return (x->Length > room) ? 1UL : 0UL;
}

/*
 * The next TD to admit, taken off StageQueue with its staging, or one to
 * end without it (*end: its device or pipe gone, its IRP cancelled, the
 * shutdown latched, or admission closed). A TD behind its pipe's owner
 * goes to the pipe's held list without staging, as hcdGo holds one
 * (HcdIoPipeRelease starts it). NULL: nothing can move in this pass.
 * IRQL: <= DISPATCH_LEVEL, no lock held.
 */
static PHCD_XFER hcdStagePick(PHCD_CONTROLLER hc, ULONG pass, PULONG end)
{
    KIRQL stageIrql;
    PLIST_ENTRY entry;
    PLIST_ENTRY next;
    PHCD_XFER x;
    PHCD_XFER owner;
    PHCD_PIPE pipe;
    KIRQL oldIrql;

    *end = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    KeAcquireSpinLock(&hc->StageLock, &stageIrql);
    for (entry = hc->StageQueue.Flink; entry != &hc->StageQueue;
         entry = next) {
        next = entry->Flink;
        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        pipe = x->Pipe;
        if (hc->StageClosed || pipe->Device->Gone || pipe->Closed ||
            x->CancelRequested || hc->ShutLatched) {
            RemoveEntryList(entry);
            x->StageState = HCD_STAGE_S_NONE;
            *end = 1;
            goto out;
        }
        if (pipe->StageBlockPass == pass) {
            continue;
        }
        owner = pipe->Exclusive;
        if (owner != NULL && owner != x && x->Seq > owner->Seq) {
            RemoveEntryList(entry);
            x->StageState = HCD_STAGE_S_NONE;
            x->State = HCD_XFER_HELD;
            x->Mapped = 0;
            InsertTailList(&pipe->Held, &x->Link);
            continue;
        }
        if (!hcdStageTake(hc, x)) {
            /* Whatever a failed take was given goes back with it. */
            (VOID)hcdStageGiveLocked(hc, x);
            pipe->StageBlockPass = pass;
            if (!x->StageWaited) {
                x->StageWaited = 1;
                hc->StageWaits++;
                if (x->Control) {
                    hc->StageControlWaits++;
                }
            }
            continue;
        }
        RemoveEntryList(entry);
        x->StageCopied = 0;
        x->StageEvent = 0;
        x->StageState = x->In ? HCD_STAGE_S_READY : HCD_STAGE_S_FILLING;
        if (x->Chunk > hc->StageTdBytesMax) {
            hc->StageTdBytesMax = x->Chunk;
        }
        if (x->Sg.List.SgElementCount > hc->StageTdElementsMax) {
            hc->StageTdElementsMax = x->Sg.List.SgElementCount;
        }
        goto out;
    }
    x = NULL;
out:
    KeReleaseSpinLock(&hc->StageLock, stageIrql);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return x;
}

/*
 * An admitted OUT's bytes into its staging. On a window that would not
 * map, the TD gives its staging back and goes back to the head of the
 * queue, its pipe held back for the rest of the pass and the retry timer
 * set; it ends instead, x->Status set, when it must (hcdStageMustEnd:
 * past the give-up NO_MEMORY). Returns 1 when the TD is ready to publish,
 * 0 when it went back (*ended: it ends instead).
 */
static ULONG hcdStageFill(PHCD_CONTROLLER hc, PHCD_XFER x, ULONG pass,
                          PULONG ended, PULONG moved)
{
    KIRQL stageIrql;
    KIRQL oldIrql;
    ULONG first;
    ULONG ok;
    LONG usbd;

    *ended = 0;
    x->StageMoved = 0;
    ok = x->In || hcdStageCopy(hc, x, 1, x->Offset, 0, x->Chunk);
    *moved = x->StageMoved;
    if (ok) {
        KeAcquireSpinLock(&hc->StageLock, &oldIrql);
        x->StageState = HCD_STAGE_S_READY;
        x->StageMapFails = 0;
        KeReleaseSpinLock(&hc->StageLock, oldIrql);
        return 1;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    KeAcquireSpinLock(&hc->StageLock, &stageIrql);
    (VOID)hcdStageGiveLocked(hc, x);
    usbd = XHCI_USBD_STATUS_SUCCESS;
    if (hcdStageMustEnd(hc, x, 1, &usbd)) {
        x->StageState = HCD_STAGE_S_NONE;
        x->Status = usbd;
        *ended = 1;
    }
    /* Read before the record is queued again: from then on a cancel or
     * a drain may end it and its pipe may go, so x is not touched after
     * the locks are released on that path. */
    first = (x->StageMapFails == 1) ? 1UL : 0UL;
    if (!*ended) {
        x->StageState = HCD_STAGE_S_WAITING;
        InsertHeadList(&hc->StageQueue, &x->Link);
        x->Pipe->StageBlockPass = pass;
        hcdStageArmLocked(hc);
    }
    KeReleaseSpinLock(&hc->StageLock, stageIrql);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (*ended && usbd == HCD_USBD_NO_MEMORY) {
        XhciLogNote(&hc->Hc, "xfer.stage.giveup", hc->StageGiveups);
    } else if (first) {
        XhciLogNote(&hc->Hc, "xfer.stage.mapfail", hc->StageMapFails);
    }
    return 0;
}

/*
 * One admission pass, as many TDs as can move, publishing each. One runs
 * at a time; a caller finding one running asks it to go round again.
 * After HCD_STAGE_PASS_BYTES copied the rest goes to StageDpc. IRQL:
 * <= DISPATCH_LEVEL, no lock held.
 */
static VOID hcdStagePass(PHCD_CONTROLLER hc)
{
    PHCD_XFER x;
    KIRQL oldIrql;
    ULONG pass;
    ULONG copied;
    ULONG end;
    ULONG ended;
    ULONG moved;
    ULONG more;

    KeAcquireSpinLock(&hc->StageLock, &oldIrql);
    if (hc->StagePassBusy) {
        hc->StagePassAgain = 1;
        KeReleaseSpinLock(&hc->StageLock, oldIrql);
        return;
    }
    hc->StagePassBusy = 1;
    (VOID)InterlockedIncrement((PLONG)&hc->StageOwners);
    KeReleaseSpinLock(&hc->StageLock, oldIrql);

    copied = 0;
    more = 0;
    for (;;) {
        KeAcquireSpinLock(&hc->StageLock, &oldIrql);
        hc->StagePassAgain = 0;
        pass = ++hc->StagePassNo;
        KeReleaseSpinLock(&hc->StageLock, oldIrql);
        for (;;) {
            x = hcdStagePick(hc, pass, &end);
            if (x == NULL) {
                break;
            }
            if (end) {
                /* HcdIoMapped's refusal names the reason (Gone, the
                 * cancel, the pipe closed, the latch). */
                if (x->Status == XHCI_USBD_STATUS_SUCCESS && hc->StageClosed &&
                    !x->Pipe->Device->Gone && !x->Pipe->Closed &&
                    !x->CancelRequested && !hc->ShutLatched) {
                    x->Status = XHCI_USBD_STATUS_DEV_NOT_RESPONDING;
                }
                HcdIoMapped(hc, x, 0);
                continue;
            }
            if (!hcdStageFill(hc, x, pass, &ended, &moved)) {
                copied += moved;
                if (ended) {
                    HcdIoMapped(hc, x, 0);
                }
                if (copied >= HCD_STAGE_PASS_BYTES) {
                    more = 1;
                    break;
                }
                continue;
            }
            copied += moved;
            HcdIoMapped(hc, x, 1);
            if (copied >= HCD_STAGE_PASS_BYTES) {
                more = 1;
                break;
            }
        }
        KeAcquireSpinLock(&hc->StageLock, &oldIrql);
        if (more || !hc->StagePassAgain) {
            hc->StagePassBusy = 0;
            KeReleaseSpinLock(&hc->StageLock, oldIrql);
            break;
        }
        KeReleaseSpinLock(&hc->StageLock, oldIrql);
    }
    if (more) {
        hcdStageKick(hc);
    }
    (VOID)InterlockedDecrement((PLONG)&hc->StageOwners);
}

/* A TD of `x` wants staging: queued in arrival order, and a pass run.
 * IRQL: <= DISPATCH_LEVEL, no lock held. */
VOID HcdStageSubmit(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->StageLock, &oldIrql);
    x->StageState = HCD_STAGE_S_WAITING;
    x->StageKind = HCD_STAGE_K_NONE;
    x->StageCopied = 0;
    x->StageAbandon = 0;
    x->StageWaited = 0;
    x->StageMapFails = 0;
    InsertTailList(&hc->StageQueue, &x->Link);
    KeReleaseSpinLock(&hc->StageLock, oldIrql);
    hcdStagePass(hc);
}

/*
 * An isochronous record into its own share (section 11.11.5): its packets
 * back to back from the share's byte 0, which is packet 0's offset in the
 * client's buffer (StageIsoBase) - the packets follow one another there
 * (hcd_urb.c and hcdIsoAdmit check it), so each lands at its client offset
 * less the base. Never queued, never retried. Returns 0 when the share
 * cannot hold it or its OUT copy cannot map at once: refused NO_MEMORY,
 * counted. IRQL: <= DISPATCH_LEVEL, no lock held.
 */
ULONG HcdStageIsoFill(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    PHCD_PIPE pipe;
    ULONG index;
    ULONG bytes;
    ULONG pa;
    ULONG n;
    ULONG ok;

    pipe = x->Pipe;
    index = (ULONG)(x - pipe->Xfers);
    if (hc->StageClosed || pipe->StageVa == NULL ||
        pipe->IsoShareBytes == 0 || x->StageIsoBase > x->Length ||
        x->Length - x->StageIsoBase > pipe->IsoShareBytes) {
        (VOID)InterlockedIncrement((PLONG)&hc->IsoStageRefused);
        return 0;
    }
    bytes = x->Length - x->StageIsoBase;
    pa = pipe->StagePa.LowPart + index * pipe->IsoShareBytes;
    hcdStageOne(x, HCD_STAGE_K_ISO, pipe->StageVa + index *
                                         pipe->IsoShareBytes,
                pa, pipe->IsoShareBytes);
    x->StagePage = pa & (PAGE_SIZE - 1);
    x->Offset = 0;
    x->Chunk = bytes;
    n = 0;
    /* A URB of zero-length packets alone still names an address inside the
     * share for their empty TRBs (hcdIsoFill), so at least one byte. */
    if (!XhciStageRuns(x->Sg.List.SgElement, &n, HCD_SG_ELEMENTS, pa,
                       bytes != 0 ? bytes : 1UL, 0)) {
        (VOID)InterlockedIncrement((PLONG)&hc->IsoStageRefused);
        return 0;
    }
    x->Sg.List.SgElementCount = n;
    ok = 1;
    if (!x->In) {
        (VOID)InterlockedIncrement((PLONG)&hc->StageOwners);
        ok = hcdStageCopy(hc, x, 1, x->StageIsoBase, 0, bytes);
        (VOID)InterlockedDecrement((PLONG)&hc->StageOwners);
    }
    if (!ok) {
        (VOID)InterlockedIncrement((PLONG)&hc->IsoStageRefused);
        return 0;
    }
    x->StageCopied = 0;
    x->StageAbandon = 0;
    x->StageEvent = 0;
    x->StageState = HCD_STAGE_S_READY;
    return 1;
}

/* ----------------------------------------------------------------------- */
/* Publication, completion and cancellation                                 */
/* ----------------------------------------------------------------------- */

/* The TD is on the ring. In the qemu flavour every element it names is
 * checked inside its own staging (StageForeignAddress stays 0).
 * Controller lock held. */
VOID HcdStagePublishedLocked(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    KIRQL stageIrql;
#if defined(XHCI_FLAVOUR_QEMU)
    PUSBPORT_SCATTER_GATHER_ELEMENT e;
    ULONG found;
    ULONG i;
    ULONG s;

    for (i = 0; x->Chunk != 0 && i < x->Sg.List.SgElementCount; i++) {
        e = &x->Sg.List.SgElement[i];
        found = 0;
        for (s = 0; s < x->StageCount; s++) {
            if (e->SgPhysicalAddressLo >= x->StageSegPa[s] &&
                e->SgPhysicalAddressLo - x->StageSegPa[s] <
                    x->StageSegBytes &&
                e->SgTransferLength <=
                    x->StageSegBytes -
                        (e->SgPhysicalAddressLo - x->StageSegPa[s])) {
                found = 1;
            }
        }
        if (!found) {
            hc->StageForeignAddress++;
        }
    }
#endif
    KeAcquireSpinLock(&hc->StageLock, &stageIrql);
    if (x->StageState == HCD_STAGE_S_READY) {
        x->StageState = HCD_STAGE_S_PUBLISHED;
    }
    KeReleaseSpinLock(&hc->StageLock, stageIrql);
}

/*
 * An IN's bytes back to its client (section 11.11.4): the TD's own count,
 * or each isochronous packet's that the controller reported good (section
 * 11.11.5: a failed or unreported packet leaves the client's bytes
 * untouched and reports none). Only a TD that retired through its
 * Transfer Event is copied when the controller may still be mastering
 * (CommonBufferPinned): one taken off then reports no bytes, its packets
 * none. Returns 0 when a window would not map.
 */
static ULONG hcdStageCopyOut(PHCD_CONTROLLER hc, PHCD_XFER x)
{
    struct _URB_ISOCH_TRANSFER *it;
    PUSBPORT_ISO_TRANSFER blk;
    ULONG allowed;
    ULONG bytes;
    ULONG len;
    ULONG off;
    ULONG n;
    ULONG i;
    ULONG ok;

    allowed = (x->StageEvent || !hc->CommonBufferPinned) ? 1UL : 0UL;
    ok = 1;
    (VOID)InterlockedIncrement((PLONG)&hc->StageOwners);
    if (x->Isoch) {
        it = &((PURB)x->Urb)->UrbIsochronousTransfer;
        blk = XhciXferIsoBlockAt(x->Pipe->Iso, (ULONG)(x - x->Pipe->Xfers),
                                 x->Pipe->IsoBlockBytes);
        XhciXferIsoFinalise(&x->Xfer);
        n = it->NumberOfPackets;
        for (i = 0; i < n && ok; i++) {
            len = blk->Packet[i].LengthTransferred;
            off = it->IsoPacket[i].Offset;
            if (!allowed ||
                (blk->Packet[i].Status != XHCI_USBD_STATUS_SUCCESS &&
                 blk->Packet[i].Status != XHCI_USBD_STATUS_DATA_UNDERRUN)) {
                blk->Packet[i].LengthTransferred = 0;
                continue;
            }
            if (len != 0 && off >= x->StageIsoBase && off < x->Length &&
                len <= x->Length - off) {
                ok = hcdStageCopy(hc, x, 0, off, off - x->StageIsoBase, len);
            }
        }
    } else {
        bytes = x->Xfer.BytesTransferred;
        if (bytes > x->Chunk) {
            bytes = x->Chunk;
        }
        if (!allowed) {
            x->Xfer.BytesTransferred = 0;
        } else {
            ok = hcdStageCopy(hc, x, 0, x->Offset, 0, bytes);
        }
    }
    (VOID)InterlockedDecrement((PLONG)&hc->StageOwners);
    return ok;
}

/*
/*
 * A copy-out that waits rather than runs now - its window would not map
 * (`failed`), or this pass has copied its bound - put on StageCopyQueue,
 * the retry timer set for a failure and StageDpc kicked for the bound; or
 * ended, its data dropped, when it must (hcdStageMustEnd). Decided under
 * the controller lock and StageLock (see hcdStageMustEnd). Returns 1 when
 * it was queued: the caller touches the record no further. IRQL:
 * <= DISPATCH_LEVEL, no lock held.
 */
static ULONG hcdStageCopyWait(PHCD_CONTROLLER hc, PHCD_XFER x, ULONG failed)
{
    KIRQL stageIrql;
    KIRQL oldIrql;
    LONG usbd;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    KeAcquireSpinLock(&hc->StageLock, &stageIrql);
    if (!hcdStageMustEnd(hc, x, failed, &usbd)) {
        x->StageState = HCD_STAGE_S_COPY_PENDING;
        if (failed) {
            InsertTailList(&hc->StageCopyQueue, &x->Link);
            hcdStageArmLocked(hc);
        } else {
            InsertHeadList(&hc->StageCopyQueue, &x->Link);
        }
        KeReleaseSpinLock(&hc->StageLock, stageIrql);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (!failed) {
            hcdStageKick(hc);
        }
        return 1;
    }
    /* Ended: its data is lost, as a transfer error loses it. */
    KeReleaseSpinLock(&hc->StageLock, stageIrql);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (usbd == HCD_USBD_NO_MEMORY) {
        XhciLogNote(&hc->Hc, "xfer.stage.giveup", hc->StageGiveups);
    }
    x->StageAbandon = 1;
    x->Engine = 0;
    x->Status = usbd;
    return 0;
}

/*
 * A TD off the ring, on its way to complete or to its next chunk
 * (hcd_io.c, HcdIoDeferred, before it reads the TD's outcome): an IN's
 * copy-out, then the staging back. `*budget` counts the bytes this pass
 * of the deferred work has copied; past HCD_STAGE_PASS_BYTES a copy-out
 * waits for StageDpc instead (section 11.11.4's bound). Returns 0 when the
 * record waits on StageCopyQueue holding its staging - its window would
 * not map, or the bound was reached - and the retry puts it back on the
 * done list. IRQL: <= DISPATCH_LEVEL, no lock held.
 */
ULONG HcdStageComplete(PHCD_CONTROLLER hc, PHCD_XFER x, PULONG budget)
{
    KIRQL oldIrql;
    ULONG back;
    ULONG ok;

    if (x->StageKind == HCD_STAGE_K_NONE) {
        x->StageState = HCD_STAGE_S_NONE;
        return 1;
    }
    if (x->In && x->Engine && !x->StageCopied && !x->StageAbandon) {
        KeAcquireSpinLock(&hc->StageLock, &oldIrql);
        x->StageState = HCD_STAGE_S_DRAINING;
        KeReleaseSpinLock(&hc->StageLock, oldIrql);
        if (*budget >= HCD_STAGE_PASS_BYTES) {
            if (hcdStageCopyWait(hc, x, 0)) {
                return 0;
            }
        } else {
            x->StageMoved = 0;
            ok = hcdStageCopyOut(hc, x);
            *budget += x->StageMoved;
            if (!ok && hcdStageCopyWait(hc, x, 1)) {
                return 0;
            }
        }
        x->StageCopied = 1;
    }
    KeAcquireSpinLock(&hc->StageLock, &oldIrql);
    back = hcdStageGiveLocked(hc, x);
    x->StageState = HCD_STAGE_S_NONE;
    KeReleaseSpinLock(&hc->StageLock, oldIrql);
    if (back) {
        hcdStageKick(hc);
    }
    return 1;
}

/*
 * The copy-outs waiting on StageCopyQueue, each tried once, until this
 * pass has copied HCD_STAGE_PASS_BYTES: one that maps goes back to the
 * done list; one that fails, or is left for the next pass, waits again
 * unless it must end (hcdStageCopyWait), when it goes to the done list
 * with its status, its data dropped. IRQL: DISPATCH_LEVEL, from StageDpc
 * or the retry timer's DPC.
 */
static VOID hcdStageRetryCopies(PHCD_CONTROLLER hc)
{
    LIST_ENTRY mine;
    LIST_ENTRY done;
    PLIST_ENTRY entry;
    PHCD_XFER x;
    KIRQL oldIrql;
    ULONG copied;
    ULONG moved;
    ULONG ok;

    InitializeListHead(&mine);
    InitializeListHead(&done);
    KeAcquireSpinLock(&hc->StageLock, &oldIrql);
    while (!IsListEmpty(&hc->StageCopyQueue)) {
        entry = RemoveHeadList(&hc->StageCopyQueue);
        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        x->StageState = HCD_STAGE_S_DRAINING;
        InsertTailList(&mine, entry);
    }
    (VOID)InterlockedIncrement((PLONG)&hc->StageOwners);
    KeReleaseSpinLock(&hc->StageLock, oldIrql);
    copied = 0;
    moved = 0;
    while (!IsListEmpty(&mine)) {
        entry = RemoveHeadList(&mine);
        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        if (copied < HCD_STAGE_PASS_BYTES) {
            x->StageMoved = 0;
            ok = hcdStageCopyOut(hc, x);
            copied += x->StageMoved;
            if (ok) {
                x->StageCopied = 1;
                InsertTailList(&done, entry);
                continue;
            }
            if (!hcdStageCopyWait(hc, x, 1)) {
                InsertTailList(&done, entry);
            }
            continue;
        }
        if (!hcdStageCopyWait(hc, x, 0)) {
            InsertTailList(&done, entry);
        }
    }
    if (!IsListEmpty(&done)) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        while (!IsListEmpty(&done)) {
            entry = RemoveHeadList(&done);
            x = CONTAINING_RECORD(entry, HCD_XFER, Link);
            x->State = HCD_XFER_DONE;
            InsertTailList(&hc->DoneList, entry);
            moved++;
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
    }
    if (moved != 0) {
        HcdIoDeferred(hc);
    }
    (VOID)InterlockedDecrement((PLONG)&hc->StageOwners);
}

/*
 * A cancel, an abort or a drain reaching a record (hcd_io.c, from
 * HcdIoWideStop): one waiting for staging, or a copy-out waiting for a
 * window, is taken off its queue - needing neither the controller thread
 * nor staging to come back - and put on the done list with `usbd`, its
 * data dropped; the caller runs the deferred work once it has released
 * the lock. A record whose copy runs is finished by its owner. Controller
 * lock held.
 */
VOID HcdStageCancelLocked(PHCD_CONTROLLER hc, PHCD_XFER x, LONG usbd)
{
    KIRQL stageIrql;
    ULONG moved;

    moved = 0;
    KeAcquireSpinLock(&hc->StageLock, &stageIrql);
    if (x->StageState == HCD_STAGE_S_WAITING) {
        RemoveEntryList(&x->Link);
        x->StageState = HCD_STAGE_S_NONE;
        moved = 1;
    } else if (x->StageState == HCD_STAGE_S_COPY_PENDING) {
        RemoveEntryList(&x->Link);
        x->StageState = HCD_STAGE_S_DRAINING;
        x->StageAbandon = 1;
        moved = 1;
    }
    KeReleaseSpinLock(&hc->StageLock, stageIrql);
    if (moved) {
        x->Engine = 0;
        x->Status = usbd;
        x->State = HCD_XFER_DONE;
        InsertTailList(&hc->DoneList, &x->Link);
    }
}

/* ----------------------------------------------------------------------- */
/* DPCs                                                                     */
/* ----------------------------------------------------------------------- */

static VOID NTAPI hcdStageDpc(PKDPC Dpc, PVOID Context, PVOID Arg1,
                              PVOID Arg2)
{
    PHCD_CONTROLLER hc;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    hc = (PHCD_CONTROLLER)Context;
    /* Closed or not: a pass with admission closed ends what waits. The
     * copy-outs a bound left, and any waiting for a window, first. */
    hcdStageRetryCopies(hc);
    hcdStagePass(hc);
    (VOID)InterlockedDecrement((PLONG)&hc->StageDpcsInFlight);
}

static VOID NTAPI hcdStageTimerDpc(PKDPC Dpc, PVOID Context, PVOID Arg1,
                                   PVOID Arg2)
{
    PHCD_CONTROLLER hc;
    KIRQL oldIrql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    hc = (PHCD_CONTROLLER)Context;
    KeAcquireSpinLock(&hc->StageLock, &oldIrql);
    hc->StageTimerArmed = 0;
    KeReleaseSpinLock(&hc->StageLock, oldIrql);
    hcdStageRetryCopies(hc);
    hcdStagePass(hc);
    (VOID)InterlockedDecrement((PLONG)&hc->StageTimerSet);
}

/* ----------------------------------------------------------------------- */
/* Pipes                                                                    */
/* ----------------------------------------------------------------------- */

/* A slab reserve: one more allocation, else a free slab of the pool
 * moved to it. IRQL: PASSIVE_LEVEL. */
static ULONG hcdStageSlabReserve(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    KIRQL oldIrql;
    ULONG i;

    pipe->ReserveSlab = 0xFFFFFFFFUL;
    pipe->ReserveOwnVa = (PUCHAR)HcdDmaStreamAlloc(hc, XHCI_STAGE_SLAB,
                                                   &pipe->ReserveOwnPa);
    if (pipe->ReserveOwnVa != NULL) {
        pipe->ReserveVa = pipe->ReserveOwnVa;
        pipe->ReservePa = pipe->ReserveOwnPa.LowPart;
    } else {
        KeAcquireSpinLock(&hc->StageLock, &oldIrql);
        for (i = 0; hc->StageSlabs != NULL && i < hc->StageSlabCount; i++) {
            if (hc->StageSlabs[i].State == HCD_STAGE_SLAB_FREE) {
                hc->StageSlabs[i].State = HCD_STAGE_SLAB_RESERVE;
                hc->StageSlabsFree--;
                pipe->ReserveSlab = i;
                pipe->ReserveVa = hc->StageSlabs[i].Va;
                pipe->ReservePa = hc->StageSlabs[i].Pa.LowPart;
                break;
            }
        }
        KeReleaseSpinLock(&hc->StageLock, oldIrql);
        if (pipe->ReserveSlab == 0xFFFFFFFFUL) {
            return 0;
        }
    }
    pipe->ReserveKind = HCD_STAGE_RES_SLAB;
    pipe->ReserveBytes = XHCI_STAGE_SLAB;
    return 1;
}

/* The records' small buffers from `va` and `pa`, 1 KiB apart. */
static VOID hcdStageSmalls(PHCD_PIPE pipe, PUCHAR va, ULONG pa)
{
    ULONG i;

    for (i = 0; i < pipe->XferCount; i++) {
        pipe->Xfers[i].SmallVa = va + i * XHCI_STAGE_SMALL;
        pipe->Xfers[i].SmallPa = pa + i * XHCI_STAGE_SMALL;
    }
}

/*
 * A pipe's staging, taken before it is exposed (section 11.11.1): its
 * records' small buffers and a reserve - a slab for a bulk pipe, a page
 * for an interrupt or control pipe - or an isochronous pipe's shares. A
 * stream endpoint's streams get theirs with the streams
 * (HcdStageStreamsOpen). Returns USBD success, or NO_MEMORY: the pipe
 * does not open (StageNoReserve). IRQL: PASSIVE_LEVEL.
 */
LONG HcdStagePipeOpen(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    ULONG share;
    ULONG bytes;
    ULONG page;

    pipe->StageVa = NULL;
    pipe->StageBytes = 0;
    pipe->StageOwn = 0;
    pipe->ReserveKind = HCD_STAGE_RES_NONE;
    pipe->ReserveVa = NULL;
    pipe->ReserveBytes = 0;
    pipe->ReserveSlab = 0xFFFFFFFFUL;
    pipe->ReserveOwnVa = NULL;
    pipe->ReserveBusy = 0;
    pipe->StageBlockPass = 0;
    pipe->IsoShareBytes = 0;
    if (!hc->Staging) {
        /* XhciDmaStaging 0: the map pump maps the client's buffer. */
        return XHCI_USBD_STATUS_SUCCESS;
    }
    if (pipe->TransferType == XHCI_PIPE_XFER_ISOCH) {
        if (pipe->Ep.MaxEsitPayload == 0) {
            /* A zero-bandwidth endpoint moves nothing (hcdIsoAdmit). */
            return XHCI_USBD_STATUS_SUCCESS;
        }
        share = XhciStageIsoShare(pipe->IsoMaxPackets,
                                  pipe->Ep.MaxEsitPayload);
        if (share == 0) {
            hc->StageNoReserve++;
            return HCD_USBD_NO_MEMORY;
        }
        bytes = XhciStagePages(pipe->XferCount * share);
        pipe->StageVa = (PUCHAR)HcdDmaStreamAlloc(hc, bytes,
                                                  &pipe->StagePa);
        if (pipe->StageVa == NULL) {
            hc->StageNoReserve++;
            return HCD_USBD_NO_MEMORY;
        }
        pipe->StageBytes = bytes;
        pipe->StageOwn = 1;
        pipe->IsoShareBytes = share;
        return XHCI_USBD_STATUS_SUCCESS;
    }
    page = (pipe->TransferType == XHCI_PIPE_XFER_BULK) ? 0UL
                                                        : XHCI_STAGE_PAGE;
    bytes = XhciStagePages(page + pipe->XferCount * XHCI_STAGE_SMALL);
    pipe->StageVa = (PUCHAR)HcdDmaStreamAlloc(hc, bytes, &pipe->StagePa);
    if (pipe->StageVa == NULL) {
        hc->StageNoReserve++;
        return HCD_USBD_NO_MEMORY;
    }
    pipe->StageBytes = bytes;
    pipe->StageOwn = 1;
    hcdStageSmalls(pipe, pipe->StageVa + page, pipe->StagePa.LowPart + page);
    if (page != 0) {
        pipe->ReserveKind = HCD_STAGE_RES_PAGE;
        pipe->ReserveVa = pipe->StageVa;
        pipe->ReservePa = pipe->StagePa.LowPart;
        pipe->ReserveBytes = XHCI_STAGE_PAGE;
        return XHCI_USBD_STATUS_SUCCESS;
    }
    if (!hcdStageSlabReserve(hc, pipe)) {
        HcdStagePipeClose(hc, pipe);
        hc->StageNoReserve++;
        return HCD_USBD_NO_MEMORY;
    }
    return XHCI_USBD_STATUS_SUCCESS;
}

/* Its staging back, with its pipe, every record free: a slab reserve of
 * the pool to the pool, the rest freed - or kept while the controller may
 * still be mastering (HcdDmaStreamFree). IRQL: PASSIVE_LEVEL. */
VOID HcdStagePipeClose(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    KIRQL oldIrql;
    ULONG s;
    ULONG i;

    if (pipe->ReserveOwnVa != NULL) {
        HcdDmaStreamFree(hc, XHCI_STAGE_SLAB, pipe->ReserveOwnPa,
                         pipe->ReserveOwnVa);
        pipe->ReserveOwnVa = NULL;
    }
    s = pipe->ReserveSlab;
    if (pipe->ReserveKind == HCD_STAGE_RES_SLAB && s != 0xFFFFFFFFUL) {
        KeAcquireSpinLock(&hc->StageLock, &oldIrql);
        if (hc->StageSlabs != NULL && s < hc->StageSlabCount &&
            hc->StageSlabs[s].State == HCD_STAGE_SLAB_RESERVE) {
            hc->StageSlabs[s].State = HCD_STAGE_SLAB_FREE;
            hc->StageSlabsFree++;
        }
        KeReleaseSpinLock(&hc->StageLock, oldIrql);
        pipe->ReserveSlab = 0xFFFFFFFFUL;
    }
    if (pipe->StageOwn && pipe->StageVa != NULL) {
        HcdDmaStreamFree(hc, pipe->StageBytes, pipe->StagePa,
                         pipe->StageVa);
    }
    pipe->StageVa = NULL;
    pipe->StageBytes = 0;
    pipe->StageOwn = 0;
    pipe->ReserveKind = HCD_STAGE_RES_NONE;
    pipe->ReserveVa = NULL;
    pipe->ReserveBytes = 0;
    for (i = 0; i < pipe->XferCount; i++) {
        pipe->Xfers[i].SmallVa = NULL;
        pipe->Xfers[i].SmallPa = 0;
    }
}

/*
 * A stream endpoint's streams' staging, one block (section 11.11.1):
 * every stream's page reserve, then its records' small buffers. Returns
 * 0 when it cannot be had: the streams do not open. IRQL: PASSIVE_LEVEL.
 */
ULONG HcdStageStreamsOpen(PHCD_CONTROLLER hc, PHCD_STREAMS st)
{
    PHCD_PIPE p;
    ULONG smalls;
    ULONG bytes;
    ULONG id;

    st->StageVa = NULL;
    st->StageBytes = 0;
    if (st->Count == 0 || !hc->Staging) {
        return 1;
    }
    smalls = st->Count * XHCI_STAGE_PAGE;
    bytes = XhciStagePages(smalls +
                           st->Count * HCD_STREAM_XFERS * XHCI_STAGE_SMALL);
    st->StageVa = (PUCHAR)HcdDmaStreamAlloc(hc, bytes, &st->StagePa);
    if (st->StageVa == NULL) {
        hc->StageNoReserve++;
        return 0;
    }
    st->StageBytes = bytes;
    for (id = 1; id <= st->Count; id++) {
        p = st->Pipe[id];
        if (p == NULL) {
            continue;
        }
        p->StageVa = NULL;
        p->StageOwn = 0;
        p->ReserveKind = HCD_STAGE_RES_PAGE;
        p->ReserveVa = st->StageVa + (id - 1UL) * XHCI_STAGE_PAGE;
        p->ReservePa = st->StagePa.LowPart + (id - 1UL) * XHCI_STAGE_PAGE;
        p->ReserveBytes = XHCI_STAGE_PAGE;
        p->ReserveSlab = 0xFFFFFFFFUL;
        p->ReserveOwnVa = NULL;
        p->ReserveBusy = 0;
        hcdStageSmalls(p,
                       st->StageVa + smalls +
                           (id - 1UL) * HCD_STREAM_XFERS * XHCI_STAGE_SMALL,
                       st->StagePa.LowPart + smalls +
                           (id - 1UL) * HCD_STREAM_XFERS * XHCI_STAGE_SMALL);
    }
    return 1;
}

/* The streams' block back with them (every record free). IRQL:
 * PASSIVE_LEVEL. */
VOID HcdStageStreamsClose(PHCD_CONTROLLER hc, PHCD_STREAMS st)
{
    if (st == NULL || st->StageVa == NULL) {
        return;
    }
    HcdDmaStreamFree(hc, st->StageBytes, st->StagePa, st->StageVa);
    st->StageVa = NULL;
    st->StageBytes = 0;
}

/* ----------------------------------------------------------------------- */
/* The controller                                                           */
/* ----------------------------------------------------------------------- */

/* Once, beside the controller's other objects (AddDevice). IRQL:
 * PASSIVE_LEVEL. */
VOID HcdStageInitObjects(PHCD_CONTROLLER hc)
{
    KeInitializeSpinLock(&hc->StageLock);
    InitializeListHead(&hc->StageQueue);
    InitializeListHead(&hc->StageCopyQueue);
    KeInitializeDpc(&hc->StageDpc, hcdStageDpc, hc);
    KeInitializeTimer(&hc->StageTimer);
    KeInitializeDpc(&hc->StageTimerDpc, hcdStageTimerDpc, hc);
    hc->StageClosed = 1;
    hc->StageDpcsInFlight = 0;
    hc->StageTimerSet = 0;
    hc->StageOwners = 0;
}

/*
 * At a start, after the adapter and the controller's block (section
 * 11.11.1): the shared pool's slabs, one allocation each up to the budget,
 * its capacity what was granted (StageSlabsShort counts the rest); the
 * control slab, mandatory as every reserve is - a control data stage
 * longer than its pipe's page has nowhere else it is sure of; the window
 * MDLs. Fails, the start refused, without the control slab or with no
 * window MDL at all. IRQL: PASSIVE_LEVEL.
 */
NTSTATUS HcdStageOpen(PHCD_CONTROLLER hc)
{
    PHYSICAL_ADDRESS pa;
    PUCHAR va;
    ULONG wanted;
    ULONG windows;
    ULONG i;

    hc->StageSlabs = NULL;
    hc->StageSlabCount = 0;
    hc->StageSlabsFree = 0;
    hc->StageCtlVa = NULL;
    hc->StageCtlBusy = 0;
    hc->StagePassBusy = 0;
    hc->StagePassAgain = 0;
    hc->StageTimerArmed = 0;
    hc->StageRetryMs = HCD_STAGE_RETRY_FIRST_MS;
    hc->StageSlabsShort = 0;
    hc->StageSlabsHigh = 0;
    hc->StageNoReserve = 0;
    hc->StageWaits = 0;
    hc->StageControlWaits = 0;
    hc->StageReserveTakes = 0;
    hc->StageSmallTakes = 0;
    hc->StageSharedTakes = 0;
    hc->StageMapFails = 0;
    hc->StageGiveups = 0;
    hc->StageWindowMaps = 0;
    hc->StageDirectBuffer = 0;
    hc->StageDirectMdl = 0;
    hc->StageDirectLow = 0;
    hc->StageCopyBytesLo = 0;
    hc->StageCopyBytesHi = 0;
    hc->StageCopyTicks = 0;
    hc->StageCopyLongest = 0;
    hc->StageTdBytesMax = 0;
    hc->StageTdElementsMax = 0;
    hc->StageForeignAddress = 0;
    hc->IsoStageRefused = 0;
    hc->StageMapInject = 0;
    hc->StageProbeMdl = NULL;
    hc->StageProbeVa = NULL;
    hc->StageProbeMdls = 0;
    (VOID)InterlockedExchange((PLONG)&hc->StageProbeArmed, 0);
    InitializeListHead(&hc->StageQueue);
    InitializeListHead(&hc->StageCopyQueue);
    hc->StageNt = IoIsWdmVersionAvailable(1, 0x10) ? 1UL : 0UL;
    if (!hc->Staging) {
        /* XhciDmaStaging 0 (owner, 2026-11-09): no pool, no window and
         * admission left closed; every transfer goes through the map
         * pump as in 3.4.2.0 (hcd_dma.c). */
        (VOID)InterlockedExchange((PLONG)&hc->StageClosed, 1);
        XhciLogNote(&hc->Hc, "stage.pool.slabs", 0);
        return STATUS_SUCCESS;
    }
    wanted = XhciStagePoolSlabs(hc->StagePoolFound, hc->StagePoolValue);
#if defined(XHCI_FLAVOUR_QEMU)
    if (hc->QemuStagePool != HCD_STAGE_QEMU_POOL_NONE &&
        hc->QemuStagePool < wanted) {
        wanted = hc->QemuStagePool;
    }
    hc->StageMapInject = HCD_STAGE_QEMU_MAPFAIL(hc->QemuStageFault);
    hcdStageProbeOpen(hc);
#endif
    if (wanted != 0) {
        hc->StageSlabs = (PHCD_STAGE_SLAB)HcdPoolAlloc(
            wanted * (ULONG)sizeof(HCD_STAGE_SLAB));
        if (hc->StageSlabs == NULL) {
            hc->StageSlabsShort = wanted;
            wanted = 0;
        }
    }
    for (i = 0; i < wanted; i++) {
        va = (PUCHAR)HcdDmaStreamAlloc(hc, XHCI_STAGE_SLAB, &pa);
        if (va == NULL) {
            hc->StageSlabsShort = wanted - i;
            break;
        }
        hc->StageSlabs[i].Va = va;
        hc->StageSlabs[i].Pa = pa;
        hc->StageSlabs[i].State = HCD_STAGE_SLAB_FREE;
        hc->StageSlabCount++;
    }
    hc->StageSlabsFree = hc->StageSlabCount;
    hc->StageCtlVa = (PUCHAR)HcdDmaStreamAlloc(hc, XHCI_STAGE_SLAB,
                                               &hc->StageCtlPa);
    windows = 0;
    for (i = 0; i < HCD_STAGE_WINDOWS; i++) {
        hc->StageWin[i] = HcdPoolWindowMdl(HCD_STAGE_WINDOW_PAGES *
                                           PAGE_SIZE);
        hc->StageWinBusy[i] = 0;
        if (hc->StageWin[i] != NULL) {
            windows++;
        }
    }
    XhciLogNote(&hc->Hc, "stage.pool.value",
                hc->StagePoolFound ? hc->StagePoolValue : 0xFFFFFFFFUL);
    XhciLogNote(&hc->Hc, "stage.pool.slabs", hc->StageSlabCount);
    XhciLogNote(&hc->Hc, "stage.pool.short", hc->StageSlabsShort);
    XhciLogNote(&hc->Hc, "stage.control", hc->StageCtlVa != NULL ? 1UL
                                                                 : 0UL);
    XhciLogNote(&hc->Hc, "stage.windows", windows);
#if defined(XHCI_FLAVOUR_QEMU)
    XhciLogNote(&hc->Hc, "qemu.stage.pool", hc->QemuStagePool);
    XhciLogNote(&hc->Hc, "qemu.stage.fault", hc->QemuStageFault);
    XhciLogNote(&hc->Hc, "qemu.stage.probe", hc->StageProbeMdls);
#endif
    if (windows == 0 || hc->StageCtlVa == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    (VOID)InterlockedExchange((PLONG)&hc->StageClosed, 0);
    return STATUS_SUCCESS;
}

/* No more admission, from a stop that cannot prove the controller
 * stopped (hcd_svc.c, HcdSvcDmaNotStopped) or a stop's drain; a pass is
 * kicked so waiting TDs end now. IRQL: <= DISPATCH_LEVEL. */
VOID HcdStageCloseAdmission(PHCD_CONTROLLER hc)
{
    if (InterlockedExchange((PLONG)&hc->StageClosed, 1) == 0) {
        hcdStageKick(hc);
    }
}

/*
 * At a stop, before anything staging uses is freed (section 11.11.6, step
 * 3): admission closed, the retry timer cancelled, then until no DPC, no
 * timer and no owner - an admission pass, a copy, a publication - is
 * left. A wait on processor work alone. IRQL: PASSIVE_LEVEL.
 */
VOID HcdStageDrain(PHCD_CONTROLLER hc)
{
    LARGE_INTEGER due;
    KIRQL oldIrql;

    (VOID)InterlockedExchange((PLONG)&hc->StageClosed, 1);
    KeAcquireSpinLock(&hc->StageLock, &oldIrql);
    if (hc->StageTimerArmed && KeCancelTimer(&hc->StageTimer)) {
        hc->StageTimerArmed = 0;
        (VOID)InterlockedDecrement((PLONG)&hc->StageTimerSet);
    }
    KeReleaseSpinLock(&hc->StageLock, oldIrql);
    while (hc->StageDpcsInFlight != 0 || hc->StageTimerSet != 0 ||
           hc->StageOwners != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}

/* The readings (record 13 section 11.11.9), at a stop. IRQL:
 * PASSIVE_LEVEL. */
VOID HcdStageNotes(PHCD_CONTROLLER hc)
{
    XhciLogNote(&hc->Hc, "stage.slabs.high", hc->StageSlabsHigh);
    XhciLogNote(&hc->Hc, "stage.takes.small", hc->StageSmallTakes);
    XhciLogNote(&hc->Hc, "stage.takes.shared", hc->StageSharedTakes);
    XhciLogNote(&hc->Hc, "stage.takes.reserve", hc->StageReserveTakes);
    XhciLogNote(&hc->Hc, "stage.waits", hc->StageWaits);
    XhciLogNote(&hc->Hc, "stage.waits.control", hc->StageControlWaits);
    XhciLogNote(&hc->Hc, "stage.noreserve", hc->StageNoReserve);
    XhciLogNote(&hc->Hc, "stage.td.bytes.max", hc->StageTdBytesMax);
    XhciLogNote(&hc->Hc, "stage.td.elements.max", hc->StageTdElementsMax);
    XhciLogNote(&hc->Hc, "stage.copy.bytes.lo", hc->StageCopyBytesLo);
    XhciLogNote(&hc->Hc, "stage.copy.bytes.hi", hc->StageCopyBytesHi);
    XhciLogNote(&hc->Hc, "stage.copy.ticks", hc->StageCopyTicks);
    XhciLogNote(&hc->Hc, "stage.copy.longest", hc->StageCopyLongest);
    XhciLogNote(&hc->Hc, "stage.window.maps", hc->StageWindowMaps);
    XhciLogNote(&hc->Hc, "stage.map.fails", hc->StageMapFails);
    XhciLogNote(&hc->Hc, "stage.giveups", hc->StageGiveups);
    XhciLogNote(&hc->Hc, "stage.iso.refused", hc->IsoStageRefused);
#if defined(XHCI_FLAVOUR_QEMU)
    XhciLogNote(&hc->Hc, "stage.foreign", hc->StageForeignAddress);
    XhciLogNote(&hc->Hc, "stage.direct.buffer", hc->StageDirectBuffer);
    XhciLogNote(&hc->Hc, "stage.direct.mdl", hc->StageDirectMdl);
    XhciLogNote(&hc->Hc, "stage.direct.low", hc->StageDirectLow);
    XHCI_DBG_VALUE("stage: copies through a TransferBuffer",
                   hc->StageDirectBuffer);
    XHCI_DBG_VALUE("stage: copies through an MDL's system address",
                   hc->StageDirectMdl);
    XHCI_DBG_VALUE("stage: copies through an address below 0x80000000",
                   hc->StageDirectLow);
    XHCI_DBG_VALUE("stage: window maps", hc->StageWindowMaps);
    XHCI_DBG_VALUE("stage: window map failures", hc->StageMapFails);
#endif
}

/*
 * At a stop, after HcdStageDrain and every device's going (hcd_ctl.c,
 * hcdRelease): the pool's slabs and the control slab freed - or kept for
 * the boot while the controller may still be mastering (section 11.11.6,
 * step 5; HcdDmaStreamFree) - and the processor-only memory, the slab
 * table and the window MDLs, freed. IRQL: PASSIVE_LEVEL.
 */
VOID HcdStageClose(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    PHCD_STAGE_SLAB slabs;
    ULONG count;
    ULONG i;

    KeAcquireSpinLock(&hc->StageLock, &oldIrql);
    slabs = hc->StageSlabs;
    count = hc->StageSlabCount;
    hc->StageSlabs = NULL;
    hc->StageSlabCount = 0;
    hc->StageSlabsFree = 0;
    KeReleaseSpinLock(&hc->StageLock, oldIrql);
    for (i = 0; slabs != NULL && i < count; i++) {
        HcdDmaStreamFree(hc, XHCI_STAGE_SLAB, slabs[i].Pa, slabs[i].Va);
    }
    HcdPoolFree(slabs);
    if (hc->StageCtlVa != NULL) {
        HcdDmaStreamFree(hc, XHCI_STAGE_SLAB, hc->StageCtlPa,
                         hc->StageCtlVa);
        hc->StageCtlVa = NULL;
    }
    for (i = 0; i < HCD_STAGE_WINDOWS; i++) {
        HcdPoolWindowMdlFree(hc->StageWin[i]);
        hc->StageWin[i] = NULL;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    hcdStageProbeClose(hc);
#endif
}
