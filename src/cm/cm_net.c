/*
 * cm_net.c - the Thunderbolt networking service's driver half (design
 * record 35 sections 5 and 6; roadmap-usb4.md task 45.59, step S5). See
 * cm_net.h for the PDO, the interface and the locks; the rings' pure half
 * is cm_netr.c, the login and the paths the connection manager's: the
 * software one's (tbt_u4cm.c, record 35 section 3.9) or the firmware
 * one's (tbt_icm.c, section 3.10), either of which calls this file's three
 * operations on the connection-manager thread:
 *
 *   RingsUp    before either path entry: the rings' memory (cm_pool.c, at
 *              the first service after a start), then both rings in Frame
 *              Mode, the transmit ring in one write, the receive ring's E2E
 *              bits in a second (record 35 section 5.3);
 *   Link       up once both entries are written: the service's generation,
 *              the peer's MAC, prtcstns and link, the poll timer armed,
 *              LinkChange(TRUE); down before the entries go: no send
 *              accepted, LinkChange(FALSE), the outstanding reservation
 *              waited for (1 s), the transmit ring drained (500 ms);
 *   RingsDown  after the entries are invalid: section 5.4's rundown (the
 *              closing flag, the timer cancelled, every DPC share waited
 *              out), then both rings disabled and read back, unless the host
 *              interface was lost.
 *
 * Open and Close reach the engine on the thread (CM_WORK_NET, then
 * CmNhiNetWork), which applies TbtU4NetWanted or TbtIcmNetWanted and reads
 * this domain's MAC; both wait for that pass.
 *
 * THE POLL (record 35 section 5.4). The receive service runs in one DPC,
 * queued only by the kick; the kick comes from the one-shot poll timer's
 * own DPC, which the thread arms at Link up and which re-arms itself while
 * the rings are on and not closing. The data ring's interrupt cause is
 * never opened in this step (XhciCmRingInterrupt's arm opens ring 0's
 * alone), so the poll is the service's only clock and its receive
 * descriptors' IE bit raises nothing.
 *
 * IRQL: the PDO's PnP and power, Open, Close and the three operations at
 * PASSIVE_LEVEL; QueryInfo, TxReserve, TxCommit, TxAbort, Log and the two
 * DPCs at <= DISPATCH_LEVEL.
 */

#include "cm.h"

static const GUID cmNetGuid = XHCI_CM_NET_GUID_INIT;
static const WCHAR cmNetId[] = L"XHCICM\\TBNET";
static const WCHAR cmNetInstance[] = L"0";
static const WCHAR cmNetText[] = L"Thunderbolt networking";
static LONG cmNetSerial;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                */
/* ---------------------------------------------------------------------- */

/* Milliseconds since `start` (a cmNetTick), from the tick count: a wait
 * of 1 ms lasts a whole clock tick, so the waits below are bounded by the
 * clock, not by their count. */
static ULONG cmNetTick(VOID)
{
    LARGE_INTEGER t;

    KeQueryTickCount(&t);
    return t.LowPart;
}

static ULONG cmNetSinceMs(ULONG start)
{
    return (cmNetTick() - start) * (KeQueryTimeIncrement() / 10UL) / 1000UL;
}

static VOID cmNetSleepMs(ULONG ms)
{
    LARGE_INTEGER due;

    CmRelativeMs(&due, ms);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
}

/* BAR0 at DISPATCH_LEVEL or below, outside the thread's access class: the
 * liveness gate's ordinary class (cm_live.h), as the ring interrupt's
 * routines ask it. A refused read is all ones; a refused write dropped. */
static ULONG cmNetRd(PCM_NHI nhi, ULONG offset)
{
    if (nhi->BarVa == NULL || offset > nhi->BarLength - 4 ||
        !CmLiveAdmits(&nhi->Live, CM_ACCESS_ORDINARY)) {
        return 0xFFFFFFFFUL;
    }
    return READ_REGISTER_ULONG((PULONG)(nhi->BarVa + offset));
}

static VOID cmNetWr(PCM_NHI nhi, ULONG offset, ULONG value)
{
    if (nhi->BarVa == NULL || offset > nhi->BarLength - 4 ||
        !CmLiveAdmits(&nhi->Live, CM_ACCESS_ORDINARY)) {
        return;
    }
    WRITE_REGISTER_ULONG((PULONG)(nhi->BarVa + offset), value);
}

static PWCHAR cmNetCopyId(const WCHAR *s)
{
    PWCHAR out;
    ULONG n;
    ULONG i;

    n = 0;
    while (s[n] != 0) {
        n++;
    }
    out = (PWCHAR)CmAlloc((n + 2) * sizeof(WCHAR));
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        out[i] = s[i];
    }
    out[n] = 0;
    out[n + 1] = 0;
    return out;
}

static BOOLEAN cmNetIsGuid(const GUID *g)
{
    ULONG i;

    if (g == NULL || g->Data1 != cmNetGuid.Data1 || g->Data2 != cmNetGuid.Data2 ||
        g->Data3 != cmNetGuid.Data3) {
        return FALSE;
    }
    for (i = 0; i < 8; i++) {
        if (g->Data4[i] != cmNetGuid.Data4[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

/* The prtcstns this host offers (record 35 section 3.7's table): bit 1
 * always, bit 0 unless XhciCmNetE2E is 0, bit 2 for a frame above 1514. */
static ULONG cmNetStns(PCM_NHI nhi, ULONG maxFrame)
{
    ULONG s;

    s = XHCI_CM_NET_STNS_FRAME_ID;
    if (nhi->Values.Value[CM_VAL_NET_E2E] != 0) {
        s |= XHCI_CM_NET_STNS_E2E;
    }
    if (maxFrame > XHCI_CM_NET_FRAME_STANDARD) {
        s |= XHCI_CM_NET_STNS_64K;
    }
    return s;
}

static VOID cmNetInFlightDown(PCM_NET net)
{
    if (InterlockedDecrement((PLONG)&net->InFlight) == 0) {
        (VOID)KeSetEvent(&net->Idle, IO_NO_INCREMENT, FALSE);
    }
}

/* The client's callbacks, taken under Lock for one call: FALSE when none
 * is open. cmNetCbLeave after the call. */
static BOOLEAN cmNetCbEnter(PCM_NET net, XHCI_CM_NET_CALLBACKS *cb, PVOID *ctx)
{
    if (!net->Opened) {
        return FALSE;
    }
    *cb = net->Cb;
    *ctx = net->ClientContext;
    (VOID)InterlockedIncrement(&net->CbActive);
    return TRUE;
}

static VOID cmNetCbLeave(PCM_NET net)
{
    (VOID)InterlockedDecrement(&net->CbActive);
}

/* ---------------------------------------------------------------------- */
/* The receive service and the poll (record 35 section 5.4)               */
/* ---------------------------------------------------------------------- */

/* One pass: the receive ring's completed descriptors handed on in ring
 * order, then reposted; the transmit ring reaped. DISPATCH_LEVEL, the
 * service's owner. */
static VOID cmNetServicePass(PCM_NHI nhi)
{
    PCM_NET net;
    KIRQL irql;
    XHCI_CM_NET_CALLBACKS cb;
    PVOID ctx;
    BOOLEAN deliver;
    ULONG head;
    ULONG n;
    ULONG k;
    ULONG len;
    ULONG flags;
    ULONG kind;
    ULONG reaped;
    ULONG slot;

    net = &nhi->Net;
    KeAcquireSpinLock(&net->Lock, &irql);
    if (!net->RingsOn) {
        KeReleaseSpinLock(&net->Lock, irql);
        return;
    }
    net->Counters.Passes++;
    head = net->Rx.Head;
    n = 0;
    while (n < net->Rx.Size - 1UL &&
           CmNetrRxTake(&net->Rx, head + n, &len, &flags) != CM_NETR_RX_NONE) {
        n++;
    }
    net->RxTaken += n;
    reaped = CmNetrTxReap(&net->Tx);
    net->Counters.TxReaped += reaped;
    deliver = (BOOLEAN)(net->Up && cmNetCbEnter(net, &cb, &ctx));
    KeReleaseSpinLock(&net->Lock, irql);

    /* A completed descriptor is the host interface's no more until it is
     * reposted and the Consumer Index passes it, so it is read unlocked;
     * the callback copies (record 35 section 6.3). */
    for (k = 0; k < n; k++) {
        slot = (head + k) % net->Rx.Size;
        kind = CmNetrRxTake(&net->Rx, slot, &len, &flags);
        if (kind == CM_NETR_RX_BAD_PDF) {
            net->Counters.RxBadPdf++;
            if (!net->BadPdfLogged) {
                net->BadPdfLogged = 1;
                CmLogNote(nhi, "cm.net.rx.pdf", len | (flags << 16));
            }
        } else if (!deliver) {
            net->Counters.RxUnheld++;
        } else {
            net->Counters.RxFrames++;
            if (flags != 0) {
                net->Counters.RxErrors++;
            }
            cb.Receive(ctx, net->RxBufVa[slot], len, flags);
        }
    }
    if (deliver) {
        if (reaped != 0 && cb.TxDone != NULL) {
            cb.TxDone(ctx, reaped);
        }
        cmNetCbLeave(net);
    }
    if (n == 0) {
        return;
    }
    KeAcquireSpinLock(&net->Lock, &irql);
    if (net->RingsOn) {
        for (k = 0; k < n; k++) {
            CmNetrRxRepost(&net->Rx, head + k, TRUE);
        }
        net->Rx.Head = (head + n) % net->Rx.Size;
        cmNetWr(nhi, TBT_RX_RING(net->Ring) + TBT_RING_INDEX,
                CmNetrRxConsumer(&net->Rx));
    }
    KeReleaseSpinLock(&net->Lock, irql);
}

/* The service DPC: one owner at a time (the owner word), passes until one
 * ends with nothing pending; its in-flight share lowered last. */
static VOID NTAPI cmNetServiceDpc(PKDPC dpc, PVOID context, PVOID a1, PVOID a2)
{
    PCM_NHI nhi;
    PCM_NET net;
    KIRQL irql;

    UNREFERENCED_PARAMETER(dpc);
    UNREFERENCED_PARAMETER(a1);
    UNREFERENCED_PARAMETER(a2);
    nhi = (PCM_NHI)context;
    net = &nhi->Net;
    KeAcquireSpinLock(&net->OwnerLock, &irql);
    net->Pending = 1;
    if (net->Owner) {
        net->Counters.Contended++;
        KeReleaseSpinLock(&net->OwnerLock, irql);
        cmNetInFlightDown(net);
        return;
    }
    net->Owner = 1;
    for (;;) {
        net->Pending = 0;
        KeReleaseSpinLock(&net->OwnerLock, irql);
        cmNetServicePass(nhi);
        KeAcquireSpinLock(&net->OwnerLock, &irql);
        if (!net->Pending) {
            break;
        }
    }
    net->Owner = 0;
    KeReleaseSpinLock(&net->OwnerLock, irql);
    cmNetInFlightDown(net);
}

/* The kick: the service's share raised before it is queued, lowered again
 * when it was queued already (the queued run sees the frames). */
static VOID cmNetKick(PCM_NET net)
{
    net->Counters.Kicks++;
    (VOID)InterlockedIncrement((PLONG)&net->InFlight);
    if (!KeInsertQueueDpc(&net->ServiceDpc, NULL, NULL)) {
        net->Counters.Coalesced++;
        cmNetInFlightDown(net);
    }
}

VOID CmNetIrqKick(PCM_NHI nhi)
{
    PCM_NET net;
    KIRQL irql;

    net = &nhi->Net;
    KeAcquireSpinLock(&net->Lock, &irql);
    if (net->RingsOn && !net->Closing) {
        net->Counters.IrqKicks++;
        cmNetKick(net);
    }
    KeReleaseSpinLock(&net->Lock, irql);
}

static VOID cmNetArmLocked(PCM_NET net)
{
    LARGE_INTEGER due;

    (VOID)InterlockedIncrement((PLONG)&net->InFlight);
    net->TimerOn = 1;
    CmRelativeMs(&due, net->PollMs);
    (VOID)KeSetTimer(&net->Timer, due, &net->TimerDpc);
}

/* The poll timer's DPC: the kick, then the timer set again while the rings
 * are on and not closing, both under Lock so the rundown's flag and cancel
 * see a consistent timer; its own share lowered last. */
static VOID NTAPI cmNetTimerDpc(PKDPC dpc, PVOID context, PVOID a1, PVOID a2)
{
    PCM_NHI nhi;
    PCM_NET net;
    KIRQL irql;

    UNREFERENCED_PARAMETER(dpc);
    UNREFERENCED_PARAMETER(a1);
    UNREFERENCED_PARAMETER(a2);
    nhi = (PCM_NHI)context;
    net = &nhi->Net;
    KeAcquireSpinLock(&net->Lock, &irql);
    net->TimerOn = 0;
    net->Counters.Timers++;
    if (net->RingsOn && !net->Closing) {
        cmNetKick(net);
        cmNetArmLocked(net);
    }
    KeReleaseSpinLock(&net->Lock, irql);
    cmNetInFlightDown(net);
}

/* ---------------------------------------------------------------------- */
/* The rings' memory (record 35 section 5.3)                              */
/* ---------------------------------------------------------------------- */

static VOID cmNetMemFree(PCM_NHI nhi)
{
    PCM_NET net;
    ULONG i;

    net = &nhi->Net;
    for (i = 0; i < CM_NETR_MAX_FRAMES; i++) {
        if (net->RxBufVa[i] != NULL) {
            CmDmaCommonFree(nhi, CM_NETR_BUFFER_BYTES, net->RxBufPa[i], net->RxBufVa[i]);
            net->RxBufVa[i] = NULL;
        }
        if (net->TxBufVa[i] != NULL) {
            CmDmaCommonFree(nhi, CM_NETR_BUFFER_BYTES, net->TxBufPa[i], net->TxBufVa[i]);
            net->TxBufVa[i] = NULL;
        }
    }
    if (net->RxDescVa != NULL) {
        CmDmaCommonFree(nhi, net->RxFrames * TBT_DESC_BYTES, net->RxDescPa, net->RxDescVa);
        net->RxDescVa = NULL;
    }
    if (net->TxDescVa != NULL) {
        CmDmaCommonFree(nhi, net->TxFrames * TBT_DESC_BYTES, net->TxDescPa, net->TxDescVa);
        net->TxDescVa = NULL;
    }
    net->MemOn = 0;
}

/* At the first service after a start: both descriptor blocks and one page
 * per descriptor, each its own common buffer, so a fragmented machine can
 * still start a service. FALSE (everything given back) on a failure. */
static BOOLEAN cmNetMemAlloc(PCM_NHI nhi)
{
    PCM_NET net;
    ULONG i;

    net = &nhi->Net;
    if (net->MemOn) {
        return TRUE;
    }
    if (nhi->Dma == NULL) {
        return FALSE;
    }
    net->RxFrames = nhi->Values.Value[CM_VAL_NET_RX_FRAMES];
    net->TxFrames = nhi->Values.Value[CM_VAL_NET_TX_FRAMES];
    if (net->RxFrames < CM_NETR_MIN_FRAMES || net->RxFrames > CM_NETR_MAX_FRAMES ||
        net->TxFrames < CM_NETR_MIN_FRAMES || net->TxFrames > CM_NETR_MAX_FRAMES) {
        return FALSE;
    }
    net->RxDescVa = CmDmaCommonAlloc(nhi, net->RxFrames * TBT_DESC_BYTES, &net->RxDescPa);
    net->TxDescVa = CmDmaCommonAlloc(nhi, net->TxFrames * TBT_DESC_BYTES, &net->TxDescPa);
    if (net->RxDescVa == NULL || net->TxDescVa == NULL) {
        cmNetMemFree(nhi);
        return FALSE;
    }
    for (i = 0; i < net->RxFrames; i++) {
        net->RxBufVa[i] = CmDmaCommonAlloc(nhi, CM_NETR_BUFFER_BYTES, &net->RxBufPa[i]);
        if (net->RxBufVa[i] == NULL) {
            cmNetMemFree(nhi);
            return FALSE;
        }
    }
    for (i = 0; i < net->TxFrames; i++) {
        net->TxBufVa[i] = CmDmaCommonAlloc(nhi, CM_NETR_BUFFER_BYTES, &net->TxBufPa[i]);
        if (net->TxBufVa[i] == NULL) {
            cmNetMemFree(nhi);
            return FALSE;
        }
    }
    net->MemOn = 1;
    return TRUE;
}

/* ---------------------------------------------------------------------- */
/* The engine's operations (TBT_U4_NET), on the thread                    */
/* ---------------------------------------------------------------------- */

#define CM_NET_RD(nhi, off) ((nhi)->Host.Read32((nhi)->Host.Context, (off)))
#define CM_NET_WR(nhi, off, v) ((nhi)->Host.Write32((nhi)->Host.Context, (off), (v)))

/* Both rings disabled (Guide 5.7.3: Ring Control to its default, then the
 * base and size), and read back: TRUE when both read Valid clear. */
static BOOLEAN cmNetRingsOff(PCM_NHI nhi, ULONG n)
{
    ULONG tx;
    ULONG rx;

    CM_NET_WR(nhi, TBT_TX_CTRL(n), 0);
    CM_NET_WR(nhi, TBT_RX_CTRL(n), 0);
    CM_NET_WR(nhi, TBT_RX_RING(n) + TBT_RING_INDEX, 0);
    CM_NET_WR(nhi, TBT_TX_RING(n) + TBT_RING_BASE_LO, 0);
    CM_NET_WR(nhi, TBT_TX_RING(n) + TBT_RING_BASE_HI, 0);
    CM_NET_WR(nhi, TBT_TX_RING(n) + TBT_RING_SIZE_REG, 0);
    CM_NET_WR(nhi, TBT_TX_RING(n) + TBT_RING_INDEX, 0);
    CM_NET_WR(nhi, TBT_RX_RING(n) + TBT_RING_BASE_LO, 0);
    CM_NET_WR(nhi, TBT_RX_RING(n) + TBT_RING_BASE_HI, 0);
    CM_NET_WR(nhi, TBT_RX_RING(n) + TBT_RING_SIZE_REG, 0);
    cmNetSleepMs(1);
    tx = CM_NET_RD(nhi, TBT_TX_CTRL(n));
    rx = CM_NET_RD(nhi, TBT_RX_CTRL(n));
    return (BOOLEAN)(tx != 0xFFFFFFFFUL && rx != 0xFFFFFFFFUL &&
                     (tx & TBT_RING_CTRL_VALID) == 0 && (rx & TBT_RING_CTRL_VALID) == 0);
}

static LONG cmNetRingsUp(PVOID context, ULONG port, ULONG ring, BOOLEAN e2e)
{
    PCM_NHI nhi;
    PCM_NET net;
    KIRQL irql;
    ULONG idx;
    ULONG prod;

    nhi = (PCM_NHI)context;
    net = &nhi->Net;
    if (net->RingsOn || ring == 0 || ring >= TBT_HI_CAPS_PATHS_MAX) {
        return TBT_E_STATE;
    }
    if (net->MemMayRun) {
        /* An earlier service's ring was not read back stopped (or the host
         * interface was lost): its memory is not reused while it may still
         * be written. No service until the next start. */
        CmLogNote(nhi, "cm.net.mem.busy", ring);
        return TBT_E_STATE;
    }
    if (!cmNetMemAlloc(nhi)) {
        net->Counters.NoMem++;
        CmLogNote(nhi, "cm.net.nomem", (net->RxFrames << 16) | net->TxFrames);
        return TBT_E_STATE;
    }
    /* Ring Control first to its default (a predecessor's ring), read back,
     * before any descriptor of this memory is written. */
    if (!cmNetRingsOff(nhi, ring)) {
        CmLogNote(nhi, "cm.net.ring.busy", ring);
        net->Counters.RingsFailed++;
        return TBT_E_HW;
    }
    KeAcquireSpinLock(&net->Lock, &irql);
    CmNetrInit(&net->Tx, (volatile UCHAR *)net->TxDescVa, net->TxFrames, net->TxBufPa);
    CmNetrInit(&net->Rx, (volatile UCHAR *)net->RxDescVa, net->RxFrames, net->RxBufPa);
    CmNetrRxPostAll(&net->Rx, TRUE);
    net->Res.Held = FALSE;
    KeReleaseSpinLock(&net->Lock, irql);

    /* The transmit ring's base and size, then its control in one write
     * (Spec Table 12-21's note), Frame Mode, No Snoop 0. */
    CM_NET_WR(nhi, TBT_TX_RING(ring) + TBT_RING_BASE_LO, net->TxDescPa);
    CM_NET_WR(nhi, TBT_TX_RING(ring) + TBT_RING_BASE_HI, 0);
    CM_NET_WR(nhi, TBT_TX_RING(ring) + TBT_RING_SIZE_REG, net->TxFrames);
    net->MemMayRun = 1;
    CM_NET_WR(nhi, TBT_TX_CTRL(ring), TBT_RING_CTRL_VALID | (e2e ? TBT_RING_CTRL_E2E : 0UL));
    /* The receive ring: Data Buffer Size 0 (4096), the PDF masks of a
     * USB4NET frame, then Valid, then its E2E bits in a second write. */
    CM_NET_WR(nhi, TBT_RX_RING(ring) + TBT_RING_BASE_LO, net->RxDescPa);
    CM_NET_WR(nhi, TBT_RX_RING(ring) + TBT_RING_BASE_HI, 0);
    CM_NET_WR(nhi, TBT_RX_RING(ring) + TBT_RING_SIZE_REG, net->RxFrames);
    CM_NET_WR(nhi, TBT_RX_PDF(ring), TBT_RX_PDF_NET);
    /* As ring 0's start: a Producer Index the base write did not move to
     * 0 is where the next frame lands. */
    idx = CM_NET_RD(nhi, TBT_RX_RING(ring) + TBT_RING_INDEX);
    prod = TBT_RING_PROD(idx);
    if (idx == 0xFFFFFFFFUL || prod >= net->RxFrames) {
        CmLogNote(nhi, "cm.net.rx-index", idx);
        if (cmNetRingsOff(nhi, ring)) {
            net->MemMayRun = 0;
        }
        net->Counters.RingsFailed++;
        return TBT_E_HW;
    }
    net->Rx.Head = prod;
    CM_NET_WR(nhi, TBT_RX_CTRL(ring), TBT_RING_CTRL_VALID);
    if (e2e) {
        CM_NET_WR(nhi, TBT_RX_CTRL(ring),
                  TBT_RING_CTRL_VALID | TBT_RING_CTRL_E2E |
                      ((ring << TBT_RX_CTRL_E2E_HOP_SHIFT) & TBT_RX_CTRL_E2E_HOP_MASK));
    }
    CM_NET_WR(nhi, TBT_RX_RING(ring) + TBT_RING_INDEX, CmNetrRxConsumer(&net->Rx));
    idx = CM_NET_RD(nhi, TBT_TX_RING(ring) + TBT_RING_INDEX);
    if (idx == 0xFFFFFFFFUL || TBT_RING_PROD(idx) != TBT_RING_CONS(idx) ||
        TBT_RING_PROD(idx) >= net->TxFrames) {
        CmLogNote(nhi, "cm.net.tx-index", idx);
        if (cmNetRingsOff(nhi, ring)) {
            net->MemMayRun = 0;
        }
        net->Counters.RingsFailed++;
        return TBT_E_HW;
    }

    KeAcquireSpinLock(&net->Lock, &irql);
    net->Tx.Head = TBT_RING_PROD(idx);
    net->Tx.Tail = net->Tx.Head;
    net->Ring = ring;
    net->Port = port;
    net->E2E = e2e ? 1UL : 0UL;
    net->Closing = 0;
    net->Up = 0;
    net->PollMs = nhi->Values.Value[CM_VAL_NET_POLL_MS];
    if (net->PollMs == 0) {
        net->PollMs = 1;
    }
    /* The rings' own share of InFlight, dropped by the rundown. */
    KeClearEvent(&net->Idle);
    net->InFlight = 1;
    net->RingsOn = 1;
    KeReleaseSpinLock(&net->Lock, irql);
    net->Counters.Services++;
    CmLogNote(nhi, "cm.net.rings",
              (port << 24) | (ring << 16) | (net->E2E << 15) | (net->Rx.Head & 0x7FFFUL));
    /* Record 35 section 5.5: the receive ring's cause opened beside ring
     * 0's where the ring interrupt is armed (else at its next arm), then
     * one kick for a frame that landed before. */
    if (nhi->Values.Value[CM_VAL_NET_IRQ] != 0 &&
        CmRirqDataOpen(nhi, ring, FALSE) == CM_RIRQ_DATA_OPEN) {
        KeAcquireSpinLock(&net->Lock, &irql);
        if (net->RingsOn && !net->Closing) {
            cmNetKick(net);
        }
        KeReleaseSpinLock(&net->Lock, irql);
    }
    return TBT_OK;
}

/* Record 22's link: the whole rate in Mb/s and the lanes, from the host
 * lane 0's LANE_ADP_CS_1 as the engine last read it in this connection.
 * Under the firmware connection manager the lanes are the firmware's and
 * unread (record 35 section 3.10): unknown, 0. */
static VOID cmNetLinkRate(PCM_NHI nhi, ULONG port, PULONG mbps, PULONG lanes)
{
    const TBT_U4_PORT *p;
    ULONG rate;
    ULONG width;

    *mbps = 0;
    *lanes = 0;
    if (nhi->Net.Fw) {
        return;
    }
    p = TbtU4Port(&nhi->U4, port);
    if (p == NULL || !p->LinkSeen || p->LinkGeneration != p->Generation) {
        return;
    }
    switch (TBT_LANE1_SPEED(p->LinkLane1)) {
    case TBT_LANE_SPEED_GEN4:
        rate = 40000;
        break;
    case TBT_LANE_SPEED_GEN3:
        rate = 20000;
        break;
    case TBT_LANE_SPEED_GEN2:
        rate = 10000;
        break;
    default:
        rate = 0;
        break;
    }
    switch (TBT_LANE1_NLW(p->LinkLane1)) {
    case TBT_LANE_WIDTH_X1:
        width = 1;
        break;
    case TBT_LANE_WIDTH_X2:
        width = 2;
        break;
    default:
        width = 0;
        break;
    }
    *mbps = rate * width;
    *lanes = width;
}

/* The outstanding reservation waited for until its commit or abort
 * releases it: its sender may still be copying into the buffers, so it is
 * never taken back (record 35 section 5.4). One still held after 1 s is
 * logged once and waited for still. PASSIVE_LEVEL. */
static VOID cmNetResWait(PCM_NHI nhi)
{
    PCM_NET net;
    KIRQL irql;
    ULONG start;
    BOOLEAN logged;
    BOOLEAN held;

    net = &nhi->Net;
    start = cmNetTick();
    logged = FALSE;
    for (;;) {
        KeAcquireSpinLock(&net->Lock, &irql);
        held = net->Res.Held;
        KeReleaseSpinLock(&net->Lock, irql);
        if (!held) {
            return;
        }
        if (!logged && cmNetSinceMs(start) >= CM_NET_RES_WAIT_MS) {
            logged = TRUE;
            net->Counters.ResSlow++;
            CmLogNote(nhi, "cm.net.res.slow", net->Res.Count);
        }
        cmNetSleepMs(1);
    }
}

static VOID cmNetLink(PVOID context, ULONG port, BOOLEAN up, const UCHAR *peerMac6)
{
    PCM_NHI nhi;
    PCM_NET net;
    KIRQL irql;
    XHCI_CM_NET_CALLBACKS cb;
    PVOID ctx;
    BOOLEAN call;
    const TBT_XDP_PEER *peer;
    ULONG stns;
    ULONG mbps;
    ULONG lanes;
    ULONG i;
    ULONG waited;

    nhi = (PCM_NHI)context;
    net = &nhi->Net;
    if (up) {
        /* `port` is the software connection manager's port, or the
         * firmware connection manager's peer record's slot + 1. */
        peer = net->Fw ? TbtIcmPeer(&nhi->Icm, port - 1) : TbtU4Peer(&nhi->U4, port);
        stns = peer != NULL ? peer->RemoteInfo.Prtcstns : 0;
        cmNetLinkRate(nhi, port, &mbps, &lanes);
        if (!net->Pinned) {
            /* Record 35 section 5.4: the image pinned at the first arming
             * of a poll timer, never released, so a removal that sees
             * InFlight at 0 cannot unload code a DPC is returning through. */
            ObReferenceObject(CmDriverObject);
            net->Pinned = 1;
        }
        KeAcquireSpinLock(&net->Lock, &irql);
        net->Generation++;
        net->Up = 1;
        for (i = 0; i < 6; i++) {
            net->PeerMac[i] = peerMac6 != NULL ? peerMac6[i] : 0;
        }
        net->PeerStns = stns;
        net->LinkMbps = mbps;
        net->Lanes = lanes;
        if (net->RingsOn && !net->Closing && !net->TimerOn) {
            cmNetArmLocked(net);
        }
        call = cmNetCbEnter(net, &cb, &ctx);
        KeReleaseSpinLock(&net->Lock, irql);
        net->Counters.LinkUps++;
        CmLogNote(nhi, "cm.net.link.up", (port << 24) | (stns << 16) | (mbps / 1000UL));
        CmLogNote(nhi, "cm.net.peer.mac",
                  ((ULONG)net->PeerMac[2] << 24) | ((ULONG)net->PeerMac[3] << 16) |
                      ((ULONG)net->PeerMac[4] << 8) | net->PeerMac[5]);
        if (call) {
            cb.LinkChange(ctx, TRUE);
            cmNetCbLeave(net);
        }
        return;
    }

    /* Down: no send accepted from here (Up 0 and a new generation, so a
     * reservation made before is refused at its commit). */
    KeAcquireSpinLock(&net->Lock, &irql);
    net->Up = 0;
    net->Generation++;
    net->PeerStns = 0;
    net->LinkMbps = 0;
    net->Lanes = 0;
    for (i = 0; i < 6; i++) {
        net->PeerMac[i] = 0;
    }
    call = cmNetCbEnter(net, &cb, &ctx);
    KeReleaseSpinLock(&net->Lock, irql);
    net->Counters.LinkDowns++;
    CmLogNote(nhi, "cm.net.link.down", port);
    if (call) {
        cb.LinkChange(ctx, FALSE);
        cmNetCbLeave(net);
    }
    cmNetResWait(nhi);
    /* The transmit ring drained by the poll's reaping, at most 500 ms
     * (record 35 section 5.1). */
    waited = cmNetTick();
    for (;;) {
        KeAcquireSpinLock(&net->Lock, &irql);
        if (!net->RingsOn) {
            KeReleaseSpinLock(&net->Lock, irql);
            break;
        }
        net->Counters.TxReaped += CmNetrTxReap(&net->Tx);
        i = CmNetrTxUsed(&net->Tx);
        KeReleaseSpinLock(&net->Lock, irql);
        if (i == 0) {
            break;
        }
        if (cmNetSinceMs(waited) >= CM_NET_TX_DRAIN_MS) {
            net->Counters.TxDrainLeft += i;
            CmLogNote(nhi, "cm.net.tx.left", i);
            break;
        }
        cmNetSleepMs(1);
    }
}

/* Section 5.4's rundown, then the rings disabled: producers stopped (the
 * closing flag, the timer cancelled), every DPC share waited out, and only
 * then the ring registers. `lost`: the host interface lost, nothing
 * written, the memory kept for the stop to retain. */
static VOID cmNetRundown(PCM_NHI nhi, BOOLEAN lost)
{
    PCM_NET net;
    KIRQL irql;
    ULONG ring;

    net = &nhi->Net;
    KeAcquireSpinLock(&net->Lock, &irql);
    if (!net->RingsOn) {
        KeReleaseSpinLock(&net->Lock, irql);
        return;
    }
    net->Closing = 1;
    net->Up = 0;
    ring = net->Ring;
    KeReleaseSpinLock(&net->Lock, irql);
    /* Record 35 section 5.5: the data cause masked first, proven before
     * the rings are disabled; not proven, the whole interrupt falls back
     * (record 21 section 4.5.6 then retires the line). */
    if (!CmRirqDataClose(nhi, lost)) {
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
    }
    KeAcquireSpinLock(&net->Lock, &irql);
    if (net->TimerOn && KeCancelTimer(&net->Timer)) {
        /* Removed before it fired: its share goes with it. */
        net->TimerOn = 0;
        (VOID)InterlockedDecrement((PLONG)&net->InFlight);
    }
    KeReleaseSpinLock(&net->Lock, irql);
    cmNetInFlightDown(net);     /* the rings' own share */
    /* Satisfied only: the disable and the stop's free of the buffers
     * follow, and a share's DPC may still be inside them. */
    (VOID)CmWaitEvent(&net->Idle, 0);
    if (!lost) {
        if (cmNetRingsOff(nhi, ring)) {
            net->MemMayRun = 0;
        } else {
            net->Counters.StopUnverified++;
            CmLogNote(nhi, "cm.net.stop-unverified", ring);
        }
    }
    KeAcquireSpinLock(&net->Lock, &irql);
    net->RingsOn = 0;
    net->Closing = 0;
    net->Generation++;
    KeReleaseSpinLock(&net->Lock, irql);
}

static VOID cmNetRingsDown(PVOID context, ULONG port, BOOLEAN lost)
{
    PCM_NHI nhi;

    nhi = (PCM_NHI)context;
    CmLogNote(nhi, "cm.net.rings.down", (port << 8) | (lost ? 1UL : 0UL));
    cmNetRundown(nhi, lost);
}

/* ---------------------------------------------------------------------- */
/* The thread's share                                                     */
/* ---------------------------------------------------------------------- */

VOID CmNetInit(PCM_NHI nhi)
{
    PCM_NET net;

    net = &nhi->Net;
    KeInitializeSpinLock(&net->Lock);
    KeInitializeSpinLock(&net->OwnerLock);
    KeInitializeSpinLock(&net->PdoLock);
    KeInitializeEvent(&net->Ack, NotificationEvent, FALSE);
    KeInitializeEvent(&net->Idle, NotificationEvent, TRUE);
    KeInitializeDpc(&net->ServiceDpc, cmNetServiceDpc, nhi);
    KeInitializeDpc(&net->TimerDpc, cmNetTimerDpc, nhi);
    KeInitializeTimer(&net->Timer);
}

/*
 * The NHI's PCI Express Device Serial Number, for its MAC under the
 * firmware connection manager (record 35 sections 3.10 and 4.4): the PCI
 * Express Base Specification's Device Serial Number Extended Capability
 * (id 0003h), its serial's low DW at +4 and high DW at +8, found in the
 * extended capability list from 100h (each header's next offset in bits
 * 31:20, dword aligned, 0 at the end). At most 64 entries are read. None
 * where the list cannot be read: a 100h that reads all ones, 0, or the
 * same DW as offset 0 (a configuration path that reaches only the first
 * 256 bytes and wraps, or ignores the high offset bits), a next offset
 * below 100h, or a serial of all ones or of zero. PASSIVE_LEVEL.
 */
static BOOLEAN cmNetDsn(PCM_NHI nhi, PULONG lo, PULONG hi)
{
    ULONG id;
    ULONG off;
    ULONG hdr;
    ULONG n;

    id = CmCfgRead32(nhi, 0);
    off = 0x100UL;
    for (n = 0; n < 64; n++) {
        hdr = CmCfgRead32(nhi, off);
        if (hdr == 0 || hdr == 0xFFFFFFFFUL || (off == 0x100UL && hdr == id)) {
            return FALSE;
        }
        if ((hdr & 0xFFFFUL) == 0x0003UL) {
            *lo = CmCfgRead32(nhi, off + 4);
            *hi = CmCfgRead32(nhi, off + 8);
            return (BOOLEAN)(!(*lo == 0xFFFFFFFFUL && *hi == 0xFFFFFFFFUL) &&
                             !(*lo == 0 && *hi == 0));
        }
        off = (hdr >> 20) & 0xFFCUL;
        if (off < 0x100UL) {
            return FALSE;
        }
    }
    return FALSE;
}

/* At the activation of a connection manager, after its init and before
 * its start: the operations given to the engine (cmU4Activate's
 * TbtU4SetNet, `fw` FALSE; the firmware connection manager's TbtIcmSetNet
 * and the NHI's serial, `fw` TRUE), the client's wish to apply again.
 * PASSIVE_LEVEL. */
VOID CmNetBindOps(PCM_NHI nhi, BOOLEAN fw)
{
    TBT_U4_NET ops;
    TBT_ICM_NET iops;
    ULONG lo;
    ULONG hi;

    nhi->Net.Applied = 0;
    nhi->Net.AppliedStns = 0;
    nhi->Net.Fw = fw ? 1UL : 0UL;
    /* This start's identity: a router whose UUID read unusable gets a new
     * one at each start (tbtU4XdIdentity), and its MAC with it. */
    nhi->Net.MacValid = 0;
    if (nhi->Values.Value[CM_VAL_NET] == 0) {
        if (fw) {
            TbtIcmSetNet(&nhi->Icm, NULL);
        } else {
            TbtU4SetNet(&nhi->U4, NULL);
        }
        return;
    }
    if (fw) {
        iops.Context = nhi;
        iops.RingsUp = cmNetRingsUp;
        iops.RingsDown = cmNetRingsDown;
        iops.Link = cmNetLink;
        TbtIcmSetNet(&nhi->Icm, &iops);
        if (cmNetDsn(nhi, &lo, &hi)) {
            TbtIcmSetNetSerial(&nhi->Icm, lo, hi);
            CmLogNote(nhi, "cm.net.dsn", hi);
        } else {
            CmLogNote(nhi, "cm.net.dsn.none", 0);
        }
        return;
    }
    ops.Context = nhi;
    ops.RingsUp = cmNetRingsUp;
    ops.RingsDown = cmNetRingsDown;
    ops.Link = cmNetLink;
    TbtU4SetNet(&nhi->U4, &ops);
}

/* On the thread, with a connection manager running: this domain's MAC
 * read once, and the network driver's Open or Close given to the engine
 * (the firmware connection manager's where CmNetBindOps gave it the
 * operations, else the software one's). */
VOID CmNetPass(PCM_NHI nhi)
{
    PCM_NET net;
    KIRQL irql;
    UCHAR mac[6];
    ULONG wanted;
    ULONG stns;
    ULONG i;

    net = &nhi->Net;
    if (nhi->Values.Value[CM_VAL_NET] == 0) {
        return;
    }
    if (!net->MacValid && (net->Fw ? TbtIcmNetIdentity(&nhi->Icm, mac)
                                   : TbtU4NetIdentity(&nhi->U4, mac))) {
        KeAcquireSpinLock(&net->Lock, &irql);
        for (i = 0; i < 6; i++) {
            net->Mac[i] = mac[i];
        }
        net->MacValid = 1;
        KeReleaseSpinLock(&net->Lock, irql);
        CmLogNote(nhi, "cm.net.mac",
                  ((ULONG)mac[2] << 24) | ((ULONG)mac[3] << 16) | ((ULONG)mac[4] << 8) |
                      mac[5]);
    }
    KeAcquireSpinLock(&net->Lock, &irql);
    wanted = net->Wanted;
    stns = net->WantedStns;
    KeReleaseSpinLock(&net->Lock, irql);
    if (wanted != net->Applied || (wanted && stns != net->AppliedStns)) {
        net->Applied = wanted;
        net->AppliedStns = stns;
        CmLogNote(nhi, "cm.net.wanted", (wanted << 16) | stns);
        if (net->Fw) {
            TbtIcmNetWanted(&nhi->Icm, (BOOLEAN)(wanted != 0), stns);
        } else {
            TbtU4NetWanted(&nhi->U4, (BOOLEAN)(wanted != 0), stns);
        }
    }
}

VOID CmNetAck(PCM_NHI nhi)
{
    (VOID)KeSetEvent(&nhi->Net.Ack, IO_NO_INCREMENT, FALSE);
}

/* The stop, on the thread after the unbind (whose TbtU4Stop ended every
 * service through RingsDown) and before CmDmaClose: a service the engine
 * did not end run down here, then the memory given back, or kept with
 * the adapter where a ring may still write it. */
VOID CmNetStopWork(PCM_NHI nhi)
{
    PCM_NET net;

    net = &nhi->Net;
    if (net->RingsOn) {
        CmLogNote(nhi, "cm.net.stop.rings", net->Ring);
        cmNetRundown(nhi, (BOOLEAN)!CmNhiHwAccess(nhi));
    }
    if (net->MemOn) {
        if (net->MemMayRun) {
            /* Kept with its adapter until the next boot (cm_pool.c), and
             * forgotten here, so no later start frees or reuses it. */
            net->Counters.MemKept++;
            net->MemKept = 1;
            net->MemOn = 0;
            CmLogNote(nhi, "cm.net.mem.kept", net->RxFrames + net->TxFrames);
            CmZero(net->RxBufVa, sizeof(net->RxBufVa));
            CmZero(net->TxBufVa, sizeof(net->TxBufVa));
            net->RxDescVa = NULL;
            net->TxDescVa = NULL;
        } else {
            cmNetMemFree(nhi);
        }
    }
    /* A new adapter at the next start: nothing of this one may still run. */
    net->MemMayRun = 0;
}

static const char *const cmNetCounterNames[] = {
    "cm.net.opens",        "cm.net.closes",     "cm.net.services",
    "cm.net.rings.failed", "cm.net.link.ups",   "cm.net.link.downs",
    "cm.net.rx.frames",    "cm.net.rx.errors",  "cm.net.rx.badpdf",
    "cm.net.rx.unheld",    "cm.net.tx.frames",  "cm.net.tx.reaped",
    "cm.net.tx.refused",   "cm.net.tx.stale",   "cm.net.tx.drainleft",
    "cm.net.res.slow.n"  ,  "cm.net.passes",     "cm.net.kicks",
    "cm.net.coalesced",    "cm.net.contended",  "cm.net.timers",
    "cm.net.stop.unverified.n", "cm.net.mem.kept.n", "cm.net.nomem.n",
    "cm.net.irq.kicks"
};

/* The shared header writes the fallback's code out (its host build has no
 * CTL_CODE). */
XHCI_C_ASSERT(cm_net_ioctl_code,
              CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA59, METHOD_NEITHER, FILE_ANY_ACCESS) ==
                  XHCI_CM_NET_IOCTL_INTERFACE);

XHCI_C_ASSERT(cm_net_counter_names,
              sizeof(cmNetCounterNames) / sizeof(cmNetCounterNames[0]) ==
                  sizeof(CM_NET_COUNTERS) / sizeof(ULONG));

VOID CmNetLogCounters(PCM_NHI nhi)
{
    const ULONG *v;
    ULONG i;

    if (nhi->Values.Value[CM_VAL_NET] == 0 && nhi->Net.Pdo == NULL) {
        return;
    }
    v = (const ULONG *)&nhi->Net.Counters;
    for (i = 0; i < sizeof(cmNetCounterNames) / sizeof(cmNetCounterNames[0]); i++) {
        CmLogNote(nhi, cmNetCounterNames[i], v[i]);
    }
}

/* ---------------------------------------------------------------------- */
/* The status query (record 35 section 17, task 48.18)                    */
/* ---------------------------------------------------------------------- */

/* One peer record as a link entry. The record is the thread's own: read
 * with no lock. */
static VOID cmNetQLink(PCM_NHI nhi, const TBT_XDP_PEER *peer, ULONG link,
                       ULONG serviceKey, ULONG flags, PCM_QN_LINK_IN l)
{
    PCM_NET net;
    ULONG i;

    net = &nhi->Net;
    CmZero(l, sizeof(*l));
    l->Link = link;
    l->Flags = flags;
    l->DiscState = peer->State;
    l->NetState = peer->NetState;
    if (peer->HaveRemote) {
        l->Flags |= CM_QN_LF_UUID;
        for (i = 0; i < 4; i++) {
            l->Uuid[i] = peer->Remote.Dw[i];
        }
    }
    if (peer->RemoteValid) {
        l->Flags |= CM_QN_LF_BLOCK;
        l->VendorId = peer->RemoteInfo.VendorId;
        l->DeviceId = peer->RemoteInfo.DeviceId;
        l->DeviceRev = peer->RemoteInfo.DeviceRevision;
        l->VendorText = peer->RemoteInfo.VendorText;
        l->DeviceText = peer->RemoteInfo.DeviceText;
        if (peer->RemoteInfo.Network) {
            l->Flags |= CM_QN_LF_USB4NET;
            l->Prtcstns = peer->RemoteInfo.Prtcstns;
        }
    }
    if (net->Up && net->Port == serviceKey) {
        l->Flags |= CM_QN_LF_SERVICE;
    }
    if (!net->Fw) {
        cmNetLinkRate(nhi, link, &l->Mbps, &l->Lanes);
        if (l->Mbps != 0) {
            l->Flags |= CM_QN_LF_WIDTH_READ;
        }
    }
    l->Sent = peer->Counters.Sent;
    l->Received = peer->Counters.Received;
    l->Resends = peer->Counters.Resends;
    l->Malformed = peer->Counters.Malformed;
    l->LoginsSent = peer->Counters.LoginsSent;
    l->LoginFailures = peer->Counters.LoginFailures;
    l->LoginsReceived = peer->Counters.LoginsReceived;
    l->LoginsRefused = peer->Counters.LoginsRefused;
    l->ServicesUp = peer->Counters.ServicesUp;
    l->ServiceFailures = peer->Counters.ServiceFailures;
    l->Restarts = peer->Counters.Restarts;
}

ULONG CmNetQueryFill(PCM_NHI nhi)
{
    PCM_NET net;
    CM_QN_IN in;
    CM_QN_LINK_IN links[CM_QN_MAX_LINKS];
    XHCI_CM_NET_STATS st;
    XHCI_CM_NET_CALLBACKS cb;
    PVOID ctx;
    const TBT_XDP_PEER *peer;
    KIRQL irql;
    BOOLEAN call;
    ULONG pdoReported;
    ULONG n;
    ULONG i;
    ULONG fwLink;

    net = &nhi->Net;
    CmZero(&in, sizeof(in));
    in.EpochLo = nhi->QueryEpochLo;
    in.EpochHi = nhi->QueryEpochHi;
    in.NetValue = nhi->Values.Value[CM_VAL_NET];
    in.E2EValue = nhi->Values.Value[CM_VAL_NET_E2E];
    in.Cm = CM_ST_CM_NONE;
    if (nhi->Stage == CM_STAGE_ACTIVE || nhi->Stage == CM_STAGE_FAILED) {
        in.Cm = net->Fw ? CM_ST_CM_ICM : nhi->U4On ? CM_ST_CM_USB4 : CM_ST_CM_NONE;
    }

    /* The links: the running connection manager's peer records, in their
     * own order. */
    n = 0;
    fwLink = 0;
    if (in.Cm == CM_ST_CM_ICM) {
        for (i = 0; i < TBT_ICM_MAX_XD && n < CM_QN_MAX_LINKS; i++) {
            if (!nhi->Icm.Xd[i].Used) {
                continue;
            }
            if (net->Port == i + 1) {
                fwLink = nhi->Icm.Xd[i].Addr.Link;
            }
            cmNetQLink(nhi, &nhi->Icm.Xd[i].Peer, nhi->Icm.Xd[i].Addr.Link, i + 1,
                       CM_QN_LF_FIRMWARE |
                       (nhi->Icm.Xd[i].Adopted ? CM_QN_LF_UNANNOUNCED : 0UL),
                       &links[n]);
            n++;
        }
    } else if (in.Cm == CM_ST_CM_USB4) {
        for (i = 1; i <= TBT_U4_MAX_PORTS && n < CM_QN_MAX_LINKS; i++) {
            peer = TbtU4Peer(&nhi->U4, i);
            if (peer == NULL) {
                continue;
            }
            cmNetQLink(nhi, peer, i, i, 0, &links[n]);
            n++;
        }
    }
    in.Links = links;
    in.LinkCount = n;

    /* The service, under its lock for the copy alone. */
    KeAcquireSpinLock(&net->PdoLock, &irql);
    pdoReported = (ULONG)(net->Pdo != NULL &&
                          ((PCM_NETPDO)net->Pdo->DeviceExtension)->Reported);
    KeReleaseSpinLock(&net->PdoLock, irql);
    KeAcquireSpinLock(&net->Lock, &irql);
    if (pdoReported) {
        in.Flags |= CM_QN_F_PDO;
    }
    if (net->Opened) {
        in.Flags |= CM_QN_F_OPEN;
    }
    if (net->Up) {
        in.Flags |= CM_QN_F_UP;
    }
    if (net->MacValid) {
        in.Flags |= CM_QN_F_MAC;
        for (i = 0; i < 6; i++) {
            in.Mac[i] = net->Mac[i];
        }
    }
    if (in.NetValue == 0 || !pdoReported) {
        in.State = CM_QN_STATE_OFF;
    } else if (!net->Opened) {
        in.State = CM_QN_STATE_NO_DRIVER;
    } else if (!net->Up) {
        in.State = CM_QN_STATE_WAITING;
    } else {
        in.State = CM_QN_STATE_UP;
    }
    if (net->Up) {
        in.Link = net->Fw ? fwLink : net->Port;
        for (i = 0; i < 6; i++) {
            in.PeerMac[i] = net->PeerMac[i];
        }
        in.PeerStns = net->PeerStns;
        in.E2E = net->E2E;
        in.Mbps = net->LinkMbps;
        in.Lanes = net->Lanes;
        in.TxDesc = net->Tx.Size;
        in.RxDesc = net->Rx.Size;
    }
    in.OurStns = net->Opened ? net->WantedStns : 0;
    in.MaxFrame = net->MaxFrame != 0 ? net->MaxFrame : XHCI_CM_NET_FRAME_STANDARD;
    if (net->Up && (net->PeerStns & XHCI_CM_NET_STNS_64K) == 0 &&
        in.MaxFrame > XHCI_CM_NET_FRAME_STANDARD) {
        in.MaxFrame = XHCI_CM_NET_FRAME_STANDARD;
    }
    in.Generation = net->Generation;
    in.RxFrames = net->Counters.RxFrames;
    in.RxErrors = net->Counters.RxErrors;
    in.RxDropped = net->Counters.RxBadPdf + net->Counters.RxUnheld;
    in.TxFrames = net->Counters.TxFrames;
    in.TxRefused = net->Counters.TxRefused + net->Counters.TxStale;
    in.LinkUps = net->Counters.LinkUps;
    in.LinkDowns = net->Counters.LinkDowns;
    in.RingsFailed = net->Counters.RingsFailed;
    in.IrqKicks = net->Counters.IrqKicks;
    in.IrqState = CmRirqDataState(nhi);
    call = (BOOLEAN)(net->Cb.Stats != NULL && cmNetCbEnter(net, &cb, &ctx));
    KeReleaseSpinLock(&net->Lock, irql);

    /* The network driver's counters, no lock held (xhci_cmnet.h). */
    if (call) {
        CmZero(&st, sizeof(st));
        st.Size = sizeof(st);
        cb.Stats(ctx, &st);
        cmNetCbLeave(net);
        if (st.Size > sizeof(st)) {
            st.Size = sizeof(st);
        }
        in.ClientWords = (const ULONG *)&st;
        in.ClientWordCount = st.Size / sizeof(ULONG);
    }
    return CmNetQueryBuild(nhi->NetQBuf, sizeof(nhi->NetQBuf), &in);
}

/* ---------------------------------------------------------------------- */
/* The interface (xhci_cmnet.h), Context the PDO's extension              */
/* ---------------------------------------------------------------------- */

static PCM_NHI cmNetEnter(PCM_NETPDO pdo)
{
    PCM_NHI nhi;
    KIRQL irql;

    KeAcquireSpinLock(&pdo->Lock, &irql);
    nhi = (PCM_NHI)CmNetrGateEnter(&pdo->Gate);
    KeReleaseSpinLock(&pdo->Lock, irql);
    return nhi;
}

static VOID cmNetLeave(PCM_NETPDO pdo)
{
    KIRQL irql;

    KeAcquireSpinLock(&pdo->Lock, &irql);
    CmNetrGateLeave(&pdo->Gate);
    KeReleaseSpinLock(&pdo->Lock, irql);
}

static VOID cmNetRef(PVOID context)
{
    (VOID)InterlockedIncrement(&((PCM_NETPDO)context)->Refs);
}

static VOID cmNetDeref(PVOID context)
{
    (VOID)InterlockedDecrement(&((PCM_NETPDO)context)->Refs);
}

/* Open's and Close's wish given to the thread and its pass waited for (at
 * most 5 s; with no thread, the next start applies it). */
static VOID cmNetApply(PCM_NHI nhi)
{
    KeClearEvent(&nhi->Net.Ack);
    if (!CmThrQueue(nhi, CM_WORK_NET, NULL)) {
        return;
    }
    (VOID)CmWaitEventMs(&nhi->Net.Ack, CM_NET_ACK_MS);
}

static LONG NTAPI cmNetOpen(PVOID context, const XHCI_CM_NET_CALLBACKS *callbacks,
                            PVOID clientContext, const XHCI_CM_NET_CONFIG *config)
{
    PCM_NETPDO pdo;
    PCM_NHI nhi;
    PCM_NET net;
    KIRQL irql;
    LONG rc;

    pdo = (PCM_NETPDO)context;
    if (callbacks == NULL || config == NULL ||
        callbacks->Size < XHCI_CM_NET_CALLBACKS_V1_BYTES ||
        config->Size < sizeof(XHCI_CM_NET_CONFIG) || callbacks->LinkChange == NULL ||
        callbacks->Receive == NULL || callbacks->TxDone == NULL ||
        config->MaxFrame < XHCI_CM_NET_FRAME_MIN || config->MaxFrame > XHCI_CM_NET_FRAME_MAX) {
        return XHCI_CM_NET_E_PARAM;
    }
    nhi = cmNetEnter(pdo);
    if (nhi == NULL) {
        return XHCI_CM_NET_E_STATE;
    }
    net = &nhi->Net;
    rc = XHCI_CM_NET_OK;
    KeAcquireSpinLock(&net->Lock, &irql);
    if (!pdo->Started) {
        rc = XHCI_CM_NET_E_STATE;
    } else if (net->Opened || net->CbActive != 0) {
        rc = XHCI_CM_NET_E_BUSY;
    } else {
        /* Field by field: a driver from before task 48.18 passes the three
         * callbacks alone, and Stats is taken only where its Size covers
         * it (xhci_cmnet.h, record 35 section 17.3). */
        net->Cb.Size = sizeof(net->Cb);
        net->Cb.LinkChange = callbacks->LinkChange;
        net->Cb.Receive = callbacks->Receive;
        net->Cb.TxDone = callbacks->TxDone;
        net->Cb.Stats = callbacks->Size >= sizeof(XHCI_CM_NET_CALLBACKS)
                            ? callbacks->Stats : NULL;
        net->ClientContext = clientContext;
        net->MaxFrame = config->MaxFrame;
        net->Opened = 1;
        net->WantedStns = cmNetStns(nhi, config->MaxFrame);
        net->Wanted = 1;
        net->Counters.Opens++;
    }
    KeReleaseSpinLock(&net->Lock, irql);
    if (rc == XHCI_CM_NET_OK) {
        CmLogNote(nhi, "cm.net.open", config->MaxFrame);
        cmNetApply(nhi);
    }
    cmNetLeave(pdo);
    return rc;
}

static VOID NTAPI cmNetClose(PVOID context)
{
    PCM_NETPDO pdo;
    PCM_NHI nhi;
    PCM_NET net;
    KIRQL irql;
    ULONG waited;
    BOOLEAN logged;
    BOOLEAN was;

    pdo = (PCM_NETPDO)context;
    nhi = cmNetEnter(pdo);
    if (nhi == NULL) {
        return;
    }
    net = &nhi->Net;
    KeAcquireSpinLock(&net->Lock, &irql);
    was = (BOOLEAN)(net->Opened != 0);
    net->Opened = 0;
    if (was) {
        /* No reservation is made after this (TxReserve asks Opened), and
         * a token of one made before is refused at its commit. */
        net->Generation++;
        net->Wanted = 0;
        net->Counters.Closes++;
    }
    KeReleaseSpinLock(&net->Lock, irql);
    if (was) {
        CmLogNote(nhi, "cm.net.close", 0);
        cmNetApply(nhi);
        /* Close's rundown (xhci_cmnet.h): the reservation released by its
         * sender, and every callback ended, however long either takes. */
        cmNetResWait(nhi);
        waited = cmNetTick();
        logged = FALSE;
        while (net->CbActive != 0) {
            if (!logged && cmNetSinceMs(waited) >= CM_NET_CB_WAIT_MS) {
                logged = TRUE;
                CmLogNote(nhi, "cm.net.close.busy", (ULONG)net->CbActive);
            }
            cmNetSleepMs(1);
        }
        KeAcquireSpinLock(&net->Lock, &irql);
        if (!net->Opened) {
            CmZero(&net->Cb, sizeof(net->Cb));
            net->ClientContext = NULL;
        }
        KeReleaseSpinLock(&net->Lock, irql);
    }
    cmNetLeave(pdo);
}

static LONG NTAPI cmNetQueryInfo(PVOID context, PXHCI_CM_NET_INFO info)
{
    PCM_NETPDO pdo;
    PCM_NHI nhi;
    PCM_NET net;
    KIRQL irql;
    ULONG i;

    pdo = (PCM_NETPDO)context;
    if (info == NULL || info->Size < sizeof(XHCI_CM_NET_INFO)) {
        return XHCI_CM_NET_E_PARAM;
    }
    nhi = cmNetEnter(pdo);
    if (nhi == NULL) {
        return XHCI_CM_NET_E_STATE;
    }
    net = &nhi->Net;
    KeAcquireSpinLock(&net->Lock, &irql);
    if (!net->MacValid) {
        /* This domain's MAC is not read yet (the bind still running, or
         * its read failed): nothing to report. */
        KeReleaseSpinLock(&net->Lock, irql);
        cmNetLeave(pdo);
        return XHCI_CM_NET_E_STATE;
    }
    info->LinkUp = (BOOLEAN)(net->Up != 0);
    for (i = 0; i < 6; i++) {
        info->Mac[i] = net->Mac[i];
        info->PeerMac[i] = net->Up ? net->PeerMac[i] : 0;
    }
    info->PeerPrtcstns = net->Up ? net->PeerStns : 0;
    info->LinkMbps = net->Up ? net->LinkMbps : 0;
    info->Lanes = net->Up ? net->Lanes : 0;
    info->MaxFrame = net->MaxFrame != 0 ? net->MaxFrame : XHCI_CM_NET_FRAME_STANDARD;
    if (net->Up && (net->PeerStns & XHCI_CM_NET_STNS_64K) == 0 &&
        info->MaxFrame > XHCI_CM_NET_FRAME_STANDARD) {
        info->MaxFrame = XHCI_CM_NET_FRAME_STANDARD;
    }
    info->TxDescriptors = net->Up ? net->Tx.Size : 0;
    info->RxDescriptors = net->Up ? net->Rx.Size : 0;
    KeReleaseSpinLock(&net->Lock, irql);
    cmNetLeave(pdo);
    return XHCI_CM_NET_OK;
}

static ULONG NTAPI cmNetTxReserve(PVOID context, ULONG count, PVOID *buffers,
                                  PXHCI_CM_NET_TOKEN token)
{
    PCM_NETPDO pdo;
    PCM_NHI nhi;
    PCM_NET net;
    KIRQL irql;
    ULONG got;
    ULONG first;
    ULONG i;

    pdo = (PCM_NETPDO)context;
    if (buffers == NULL || token == NULL) {
        return 0;
    }
    nhi = cmNetEnter(pdo);
    if (nhi == NULL) {
        return 0;
    }
    net = &nhi->Net;
    got = 0;
    KeAcquireSpinLock(&net->Lock, &irql);
    if (net->Opened && net->Up && net->RingsOn && !net->Closing) {
        net->Counters.TxReaped += CmNetrTxReap(&net->Tx);
        got = CmNetrReserve(&net->Tx, &net->Res, net->Generation, count, &first);
        if (got != 0) {
            for (i = 0; i < got; i++) {
                buffers[i] = net->TxBufVa[(first + i) % net->Tx.Size];
            }
            token->Generation = net->Generation;
            token->First = first;
            token->Count = got;
        }
    }
    if (got == 0) {
        net->Counters.TxRefused++;
    }
    KeReleaseSpinLock(&net->Lock, irql);
    cmNetLeave(pdo);
    return got;
}

static LONG NTAPI cmNetTxCommit(PVOID context, const XHCI_CM_NET_TOKEN *token, ULONG count,
                                const ULONG *lengths)
{
    PCM_NETPDO pdo;
    PCM_NHI nhi;
    PCM_NET net;
    KIRQL irql;
    ULONG rel;
    ULONG i;
    BOOLEAN bad;
    LONG rc;

    pdo = (PCM_NETPDO)context;
    if (token == NULL) {
        return XHCI_CM_NET_E_PARAM;
    }
    nhi = cmNetEnter(pdo);
    if (nhi == NULL) {
        return XHCI_CM_NET_E_STATE;
    }
    net = &nhi->Net;
    bad = (BOOLEAN)(lengths == NULL || count != token->Count);
    for (i = 0; !bad && i < count; i++) {
        if (lengths[i] < 12 || lengths[i] > XHCI_CM_NET_BUFFER_BYTES) {
            bad = TRUE;
        }
    }
    KeAcquireSpinLock(&net->Lock, &irql);
    rel = CmNetrRelease(&net->Res, net->Generation, token->Generation, token->First,
                        token->Count);
    if (rel == CM_NETR_REL_POST && !bad && net->RingsOn && !net->Closing) {
        for (i = 0; i < count; i++) {
            CmNetrTxPost(&net->Tx, token->First + i, lengths[i]);
        }
        net->Tx.Head = (token->First + count) % net->Tx.Size;
        net->Counters.TxFrames += count;
        cmNetWr(nhi, TBT_TX_RING(net->Ring) + TBT_RING_INDEX,
                net->Tx.Head << TBT_RING_PROD_SHIFT);
        rc = XHCI_CM_NET_OK;
    } else if (rel == CM_NETR_REL_POST && bad) {
        rc = XHCI_CM_NET_E_PARAM;
    } else {
        net->Counters.TxStale++;
        rc = XHCI_CM_NET_E_STATE;
    }
    KeReleaseSpinLock(&net->Lock, irql);
    cmNetLeave(pdo);
    return rc;
}

static VOID NTAPI cmNetTxAbort(PVOID context, const XHCI_CM_NET_TOKEN *token)
{
    PCM_NETPDO pdo;
    PCM_NHI nhi;
    KIRQL irql;

    pdo = (PCM_NETPDO)context;
    if (token == NULL) {
        return;
    }
    nhi = cmNetEnter(pdo);
    if (nhi == NULL) {
        return;
    }
    KeAcquireSpinLock(&nhi->Net.Lock, &irql);
    (VOID)CmNetrRelease(&nhi->Net.Res, nhi->Net.Generation, token->Generation, token->First,
                        token->Count);
    KeReleaseSpinLock(&nhi->Net.Lock, irql);
    cmNetLeave(pdo);
}

static VOID NTAPI cmNetLog(PVOID context, const char *label, ULONG value)
{
    PCM_NETPDO pdo;
    PCM_NHI nhi;

    pdo = (PCM_NETPDO)context;
    if (label == NULL) {
        return;
    }
    nhi = cmNetEnter(pdo);
    if (nhi == NULL) {
        return;
    }
    CmLogNote(nhi, label, value);
    cmNetLeave(pdo);
}

/* The table, with the one reference the caller then owns. */
static VOID cmNetFill(PCM_NETPDO pdo, PXHCI_CM_NET_INTERFACE out)
{
    out->Header.Size = (USHORT)sizeof(XHCI_CM_NET_INTERFACE);
    out->Header.Version = XHCI_CM_NET_VERSION;
    out->Header.Context = (PVOID)pdo;
    out->Header.InterfaceReference = cmNetRef;
    out->Header.InterfaceDereference = cmNetDeref;
    out->Open = cmNetOpen;
    out->Close = cmNetClose;
    out->QueryInfo = cmNetQueryInfo;
    out->TxReserve = cmNetTxReserve;
    out->TxCommit = cmNetTxCommit;
    out->TxAbort = cmNetTxAbort;
    out->Log = cmNetLog;
    cmNetRef(pdo);
}

/* ---------------------------------------------------------------------- */
/* The PDO, from the NHI's side                                           */
/* ---------------------------------------------------------------------- */

/* "\Device\XhciCmNet" and a decimal serial: PDOs are named on NT, and
 * FILE_AUTOGENERATED_DEVICE_NAME is not a Windows 98 promise (hcd_rh.c). */
static VOID cmNetName(PWCHAR buffer, ULONG serial)
{
    static const WCHAR prefix[] = L"\\Device\\XhciCmNet";
    WCHAR digits[12];
    ULONG n;
    ULONG i;
    ULONG d;

    n = 0;
    for (i = 0; prefix[i] != 0; i++) {
        buffer[n++] = prefix[i];
    }
    d = 0;
    do {
        digits[d++] = (WCHAR)(L'0' + (serial % 10UL));
        serial /= 10UL;
    } while (serial != 0 && d < 11);
    while (d > 0) {
        buffer[n++] = digits[--d];
    }
    buffer[n] = 0;
}

/* A network PDO made, initialised and not yet given to the NHI; NULL where
 * IoCreateDevice failed. PASSIVE_LEVEL, no lock held. */
static PDEVICE_OBJECT cmNetPdoCreate(PCM_NHI nhi)
{
    WCHAR nameBuffer[40];
    UNICODE_STRING name;
    PDEVICE_OBJECT obj;
    PCM_NETPDO pdo;
    ULONG serial;
    ULONG n;
    NTSTATUS status;

    serial = (ULONG)InterlockedIncrement(&cmNetSerial);
    cmNetName(nameBuffer, serial);
    for (n = 0; nameBuffer[n] != 0; n++) {
    }
    name.Buffer = nameBuffer;
    name.Length = (USHORT)(n * sizeof(WCHAR));
    name.MaximumLength = (USHORT)((n + 1) * sizeof(WCHAR));
    status = IoCreateDevice(CmDriverObject, sizeof(CM_NETPDO), &name,
                            FILE_DEVICE_BUS_EXTENDER, 0, FALSE, &obj);
    if (!NT_SUCCESS(status)) {
        CmLogNote(nhi, "cm.net.pdo.fail", (ULONG)status);
        return NULL;
    }
    pdo = (PCM_NETPDO)obj->DeviceExtension;
    CmZero(pdo, sizeof(CM_NETPDO));
    pdo->Common.Kind = CM_KIND_NETPDO;
    pdo->Common.Self = obj;
    KeInitializeSpinLock(&pdo->Lock);
    pdo->Gate.Nhi = nhi;
    obj->Flags |= DO_POWER_PAGABLE;
    obj->Flags &= ~DO_DEVICE_INITIALIZING;
    CmLogNote(nhi, "cm.net.pdo", serial);
    return obj;
}

/* A PDO made where the NHI has none and none is retired (record 35 section
 * 6.6): one of an instance at a time, so a fresh one never meets an old
 * one the PnP manager is still removing. FALSE where one was wanted and
 * could not be made. PASSIVE_LEVEL, PdoLock not held. */
static BOOLEAN cmNetPdoEnsure(PCM_NHI nhi)
{
    PCM_NET net;
    PDEVICE_OBJECT obj;
    KIRQL irql;
    BOOLEAN need;

    net = &nhi->Net;
    KeAcquireSpinLock(&net->PdoLock, &irql);
    need = (BOOLEAN)(net->Pdo == NULL && net->Retired == NULL);
    KeReleaseSpinLock(&net->PdoLock, irql);
    if (!need) {
        return TRUE;
    }
    obj = cmNetPdoCreate(nhi);
    if (obj == NULL) {
        return FALSE;
    }
    KeAcquireSpinLock(&net->PdoLock, &irql);
    if (net->Pdo == NULL && net->Retired == NULL) {
        net->Pdo = obj;
        obj = NULL;
    }
    KeReleaseSpinLock(&net->PdoLock, irql);
    if (obj != NULL) {
        /* Another path installed one meanwhile (not while the NHI's own
         * IRPs are serial): this one was never reported. */
        IoDeleteDevice(obj);
    }
    return TRUE;
}

/* After a start's PnP half returned: the PDO created at the first start
 * that wants it, and reported while XhciCmNet is 1 under either
 * connection manager (record 35 section 6.1); the PnP manager asked again
 * when that changes. With a retired PDO not yet removed, none is made
 * here: that one's removal asks again (section 6.6). PASSIVE_LEVEL. */
VOID CmNetStarted(PCM_NHI nhi)
{
    PCM_NET net;
    KIRQL irql;
    ULONG want;
    BOOLEAN ask;

    net = &nhi->Net;
    want = (ULONG)(nhi->Values.Value[CM_VAL_NET] != 0 && (nhi->U4On || net->Fw) &&
                   nhi->Stage == CM_STAGE_ACTIVE);
    if (want && !cmNetPdoEnsure(nhi)) {
        want = 0;
    }
    KeAcquireSpinLock(&net->PdoLock, &irql);
    ask = (BOOLEAN)(want != net->Report ||
                    (want && net->Pdo != NULL &&
                     !((PCM_NETPDO)net->Pdo->DeviceExtension)->Reported));
    net->Report = want;
    KeReleaseSpinLock(&net->PdoLock, irql);
    if (ask) {
        IoInvalidateDeviceRelations(nhi->Common.Pdo, BusRelations);
    }
}

/* A surprise removal of the NHI: the PDO is reported no more. */
VOID CmNetGone(PCM_NHI nhi)
{
    KIRQL irql;

    KeAcquireSpinLock(&nhi->Net.PdoLock, &irql);
    nhi->Net.Report = 0;
    KeReleaseSpinLock(&nhi->Net.PdoLock, irql);
}

#define CM_NET_REL_NONE     0UL     /* no PDO: the IRP passed on as it is */
#define CM_NET_REL_ADD      1UL     /* the PDO added, referenced          */
#define CM_NET_REL_OMIT     2UL     /* reported no more: left out         */
#define CM_NET_REL_NOMEM    3UL     /* to add, and no list for it         */

/* IRP_MN_QUERY_DEVICE_RELATIONS (BusRelations) at the NHI's FDO: what a
 * filter above may have built, plus the network PDO while reported,
 * referenced as PnP requires; the IRP then goes down. An answer that
 * leaves out a PDO an earlier one reported retires it (record 35 section
 * 6.6): that object is never reported again, and no fresh one is made
 * until its removal. PASSIVE_LEVEL. */
NTSTATUS CmNetBusRelations(PCM_NHI nhi, PIRP irp)
{
    PCM_NET net;
    PDEVICE_RELATIONS old;
    PDEVICE_RELATIONS rel;
    PDEVICE_OBJECT obj;
    KIRQL irql;
    ULONG count;
    ULONG action;
    ULONG i;

    net = &nhi->Net;
    KeAcquireSpinLock(&net->PdoLock, &irql);
    i = (ULONG)(net->Report && net->Pdo == NULL && net->Retired == NULL);
    KeReleaseSpinLock(&net->PdoLock, irql);
    if (i) {
        /* Wanted again after a retired PDO's removal: a fresh one. */
        (VOID)cmNetPdoEnsure(nhi);
    }
    old = (PDEVICE_RELATIONS)irp->IoStatus.Information;
    count = old != NULL ? old->Count : 0;
    /* Room for the incoming list and one more, allocated before the lock;
     * DEVICE_RELATIONS holds one object of its own. */
    rel = (PDEVICE_RELATIONS)CmAlloc(sizeof(DEVICE_RELATIONS) + count * sizeof(PDEVICE_OBJECT));
    obj = NULL;
    KeAcquireSpinLock(&net->PdoLock, &irql);
    if (net->Pdo == NULL) {
        action = CM_NET_REL_NONE;
    } else if (!net->Report) {
        action = CM_NET_REL_OMIT;
        if (((PCM_NETPDO)net->Pdo->DeviceExtension)->Reported) {
            /* To retire once this answer has succeeded (below). */
            obj = net->Pdo;
        }
    } else if (rel == NULL) {
        action = CM_NET_REL_NOMEM;
    } else {
        action = CM_NET_REL_ADD;
        obj = net->Pdo;
        ObReferenceObject(obj);
    }
    KeReleaseSpinLock(&net->PdoLock, irql);

    if (action == CM_NET_REL_NONE) {
        if (rel != NULL) {
            CmFree(rel);
        }
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(nhi->Common.Lower, irp);
    }
    if (action == CM_NET_REL_OMIT) {
        NTSTATUS status;

        /* Withdrawn (a start with XhciCmNet 0, a surprise removal): a
         * successful answer without it, the incoming list kept or an
         * empty one, so the PnP manager removes the child. */
        if (irp->IoStatus.Information == 0) {
            if (rel == NULL) {
                return CmCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            }
            rel->Count = 0;
            irp->IoStatus.Information = (ULONG_PTR)rel;
        } else if (rel != NULL) {
            CmFree(rel);
        }
        irp->IoStatus.Status = STATUS_SUCCESS;
        if (obj == NULL) {
            IoSkipCurrentIrpStackLocation(irp);
            return IoCallDriver(nhi->Common.Lower, irp);
        }
        /* Record 35 section 6.6: the PnP manager learns it is gone only
         * from an answer that succeeds, so the PDO is retired once this
         * one has; one that fails below leaves it for the next answer. */
        status = CmForwardSync(&nhi->Common, irp);
        if (NT_SUCCESS(status)) {
            KeAcquireSpinLock(&net->PdoLock, &irql);
            if (net->Pdo == obj && !net->Report) {
                ((PCM_NETPDO)obj->DeviceExtension)->Retired = 1;
                net->Retired = obj;
                net->Pdo = NULL;
            } else {
                obj = NULL;
            }
            KeReleaseSpinLock(&net->PdoLock, irql);
            if (obj != NULL) {
                CmLogNote(nhi, "cm.net.pdo.retired", 0);
            }
        }
        return CmCompleteIrp(irp, status, irp->IoStatus.Information);
    }
    if (action == CM_NET_REL_NOMEM) {
        /* The list a filter above built given back, with its references. */
        for (i = 0; i < count; i++) {
            ObDereferenceObject(old->Objects[i]);
        }
        if (old != NULL) {
            CmFree(old);
        }
        return CmCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    for (i = 0; i < count; i++) {
        rel->Objects[i] = old->Objects[i];
    }
    rel->Objects[count] = obj;
    rel->Count = count + 1;
    if (old != NULL) {
        CmFree(old);
    }
    irp->IoStatus.Information = (ULONG_PTR)rel;
    irp->IoStatus.Status = STATUS_SUCCESS;
    {
        NTSTATUS status;

        /* Reported only once the answer has succeeded (record 35 section
         * 6.6): a PDO the PnP manager never learned of is never retired
         * to wait for a removal that cannot come. */
        status = CmForwardSync(&nhi->Common, irp);
        if (NT_SUCCESS(status)) {
            KeAcquireSpinLock(&net->PdoLock, &irql);
            if (net->Pdo == obj) {
                ((PCM_NETPDO)obj->DeviceExtension)->Reported = 1;
            }
            KeReleaseSpinLock(&net->PdoLock, irql);
        }
        return CmCompleteIrp(irp, status, irp->IoStatus.Information);
    }
}

/* The PDO's holds waited out (at most 2 s): FALSE when one is still held,
 * and the object is then left allocated rather than freed under it. */
static BOOLEAN cmNetRefsGone(PCM_NETPDO pdo)
{
    ULONG waited;

    waited = cmNetTick();
    while ((pdo->Refs != 0 || !CmNetrGateIdle(&pdo->Gate)) &&
           cmNetSinceMs(waited) < CM_NET_REF_WAIT_MS) {
        cmNetSleepMs(1);
    }
    return (BOOLEAN)(pdo->Refs == 0 && CmNetrGateIdle(&pdo->Gate));
}

/* IoDeleteDevice once, by whichever of the PDO's removal and the NHI's
 * wins the claim (record 35 section 6.6); each holds a reference on the
 * object while it looks, so the loser never reads a freed extension. */
static VOID cmNetDelete(PCM_NETPDO pdo)
{
    if (InterlockedIncrement(&pdo->DeleteClaim) == 1) {
        pdo->Deleted = 1;
        IoDeleteDevice(pdo->Common.Self);
    }
}

/* One PDO of the NHI's at its removal: orphaned (no way to the NHI from
 * now, every call inside waited out), then deleted when the PnP manager
 * has removed it or never learned of it; otherwise it deletes itself at
 * its own removal. The caller holds a reference on obj. PASSIVE_LEVEL. */
static VOID cmNetOrphan(PDEVICE_OBJECT obj)
{
    PCM_NETPDO pdo;
    KIRQL irql;

    pdo = (PCM_NETPDO)obj->DeviceExtension;
    KeAcquireSpinLock(&pdo->Lock, &irql);
    CmNetrGateClose(&pdo->Gate, FALSE);
    KeReleaseSpinLock(&pdo->Lock, irql);
    while (!CmNetrGateIdle(&pdo->Gate)) {
        cmNetSleepMs(1);
    }
    if ((pdo->RemoveReceived || !pdo->Reported) && !pdo->Deleted) {
        if (cmNetRefsGone(pdo)) {
            cmNetDelete(pdo);
        }
    }
}

/* The NHI's removal: its PDO, and a retired one whose removal has not
 * come, orphaned (record 35 sections 6.1 and 6.6). PASSIVE_LEVEL. */
VOID CmNetRemove(PCM_NHI nhi)
{
    PCM_NET net;
    PDEVICE_OBJECT cur;
    PDEVICE_OBJECT retired;
    KIRQL irql;

    net = &nhi->Net;
    KeAcquireSpinLock(&net->PdoLock, &irql);
    cur = net->Pdo;
    retired = net->Retired;
    net->Pdo = NULL;
    net->Retired = NULL;
    net->Report = 0;
    if (cur != NULL) {
        ObReferenceObject(cur);
    }
    if (retired != NULL) {
        ObReferenceObject(retired);
    }
    KeReleaseSpinLock(&net->PdoLock, irql);
    if (cur != NULL) {
        cmNetOrphan(cur);
        ObDereferenceObject(cur);
    }
    if (retired != NULL) {
        cmNetOrphan(retired);
        ObDereferenceObject(retired);
    }
    /* A PDO's removal that unlinked itself and has not yet seen its calls
     * out, or still asks the NHI for a new answer (record 35 section 6.6). */
    while (net->PdoCalls != 0) {
        cmNetSleepMs(1);
    }
}

/* ---------------------------------------------------------------------- */
/* The PDO's own PnP and power (record 35 section 6.1)                    */
/* ---------------------------------------------------------------------- */

static NTSTATUS cmNetQueryId(PIRP irp, BUS_QUERY_ID_TYPE type)
{
    PWCHAR id;

    switch (type) {
    case BusQueryDeviceID:
    case BusQueryHardwareIDs:
        id = cmNetCopyId(cmNetId);
        break;
    case BusQueryInstanceID:
        id = cmNetCopyId(cmNetInstance);
        break;
    default:
        /* No compatible id (record 35 section 6.2). */
        return CmCompleteIrp(irp, irp->IoStatus.Status, irp->IoStatus.Information);
    }
    if (id == NULL) {
        return CmCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    return CmCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)id);
}

/* hcd_rh.c's rule: every field written precedes D1Latency; a caller that
 * sets neither Version nor Size (98 SE's class drivers) passes a whole
 * structure. */
static NTSTATUS cmNetCapabilities(PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PDEVICE_CAPABILITIES caps;
    ULONG i;

    stack = IoGetCurrentIrpStackLocation(irp);
    caps = stack->Parameters.DeviceCapabilities.Capabilities;
    if (!(caps->Version == 0 && caps->Size == 0) &&
        (caps->Version < 1 || caps->Size < FIELD_OFFSET(DEVICE_CAPABILITIES, D1Latency))) {
        return CmCompleteIrp(irp, STATUS_UNSUCCESSFUL, 0);
    }
    caps->DeviceD1 = FALSE;
    caps->DeviceD2 = FALSE;
    caps->LockSupported = FALSE;
    caps->EjectSupported = FALSE;
    caps->Removable = FALSE;
    caps->DockDevice = FALSE;
    caps->UniqueID = FALSE;
    caps->SilentInstall = FALSE;
    caps->RawDeviceOK = FALSE;
    caps->SurpriseRemovalOK = TRUE;
    caps->Address = 0;
    caps->UINumber = 0;
    caps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    for (i = PowerSystemSleeping1; i < PowerSystemMaximum; i++) {
        caps->DeviceState[i] = PowerDeviceD3;
    }
    caps->SystemWake = PowerSystemUnspecified;
    caps->DeviceWake = PowerDeviceUnspecified;
    return CmCompleteIrp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS cmNetQueryInterface(PCM_NETPDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;

    stack = IoGetCurrentIrpStackLocation(irp);
    if (!cmNetIsGuid(stack->Parameters.QueryInterface.InterfaceType)) {
        return CmCompleteIrp(irp, irp->IoStatus.Status, irp->IoStatus.Information);
    }
    if (stack->Parameters.QueryInterface.Interface == NULL ||
        stack->Parameters.QueryInterface.Version != XHCI_CM_NET_VERSION ||
        stack->Parameters.QueryInterface.Size < sizeof(XHCI_CM_NET_INTERFACE)) {
        return CmCompleteIrp(irp, STATUS_NOT_SUPPORTED, 0);
    }
    cmNetFill(pdo, (PXHCI_CM_NET_INTERFACE)stack->Parameters.QueryInterface.Interface);
    return CmCompleteIrp(irp, STATUS_SUCCESS, irp->IoStatus.Information);
}

NTSTATUS CmNetPdoPnp(PCM_NETPDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PWCHAR text;
    PDEVICE_RELATIONS rel;
    PCM_NHI nhi;

    stack = IoGetCurrentIrpStackLocation(irp);
    nhi = cmNetEnter(pdo);
    if (nhi != NULL) {
        if (stack->MinorFunction <= IRP_MN_CANCEL_STOP_DEVICE ||
            stack->MinorFunction == IRP_MN_SURPRISE_REMOVAL) {
            CmLogNote(nhi, "cm.net.pnp", stack->MinorFunction);
        }
        cmNetLeave(pdo);
    }
    switch (stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
        pdo->Started = 1;
        pdo->RemoveReceived = 0;
        return CmCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_QUERY_STOP_DEVICE:
    case IRP_MN_QUERY_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
    case IRP_MN_CANCEL_REMOVE_DEVICE:
        return CmCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_STOP_DEVICE:
    case IRP_MN_SURPRISE_REMOVAL:
        pdo->Started = 0;
        return CmCompleteIrp(irp, STATUS_SUCCESS, 0);

    case IRP_MN_REMOVE_DEVICE:
        pdo->Started = 0;
        if (pdo->Deleted) {
            return CmCompleteIrp(irp, STATUS_SUCCESS, 0);
        }
        pdo->RemoveReceived = 1;
        /* Its own reference while it looks (section 6.6): the NHI's
         * removal may delete it meanwhile. */
        ObReferenceObject(pdo->Common.Self);
        nhi = cmNetEnter(pdo);
        if (nhi != NULL) {
            KIRQL irql;
            BOOLEAN keep;
            BOOLEAN ask;

            /* The NHI held before either link is cleared (record 35
             * section 6.6): once unlinked, this PDO is not one the NHI's
             * removal finds and waits out, so that removal waits for this
             * hold instead, until every call that entered the gate before
             * it closed has left. */
            (VOID)InterlockedIncrement(&nhi->Net.PdoCalls);
            /* Decided under PdoLock against the NHI's start and
             * BusRelations, whatever their order. */
            keep = FALSE;
            ask = FALSE;
            KeAcquireSpinLock(&nhi->Net.PdoLock, &irql);
            if (nhi->Net.Pdo == pdo->Common.Self) {
                if (nhi->Net.Report) {
                    /* Still reported (a disable): kept, the bus-driver
                     * rule. */
                    keep = TRUE;
                } else {
                    /* Reported no more and never left out of an answer
                     * (the NHI's surprise removal): retired now. */
                    nhi->Net.Pdo = NULL;
                }
            } else if (nhi->Net.Retired == pdo->Common.Self) {
                /* Left out of an answer: gone, and a fresh one may be made
                 * if the NHI wants one again. */
                nhi->Net.Retired = NULL;
                ask = (BOOLEAN)(nhi->Net.Report && nhi->Net.Pdo == NULL);
            }
            KeReleaseSpinLock(&nhi->Net.PdoLock, irql);
            if (keep) {
                cmNetLeave(pdo);
                (VOID)InterlockedDecrement(&nhi->Net.PdoCalls);
                ObDereferenceObject(pdo->Common.Self);
                return CmCompleteIrp(irp, STATUS_SUCCESS, 0);
            }
            /* No way to the NHI from now, and this call out with the same
             * step; the calls already inside waited out under the hold. */
            KeAcquireSpinLock(&pdo->Lock, &irql);
            CmNetrGateClose(&pdo->Gate, TRUE);
            KeReleaseSpinLock(&pdo->Lock, irql);
            while (!CmNetrGateIdle(&pdo->Gate)) {
                cmNetSleepMs(1);
            }
            if (ask) {
                CmLogNote(nhi, "cm.net.pdo.again", 0);
                IoInvalidateDeviceRelations(nhi->Common.Pdo, BusRelations);
            }
            /* The last use of the NHI here. */
            (VOID)InterlockedDecrement(&nhi->Net.PdoCalls);
        }
        /* Orphaned by the NHI's removal. On 98 SE and ME more IRPs may
         * follow a REMOVE (hcd_pdo.c), so the object stays allocated. */
        (VOID)CmCompleteIrp(irp, STATUS_SUCCESS, 0);
        if (IoIsWdmVersionAvailable(1, 0x10) && cmNetRefsGone(pdo)) {
            cmNetDelete(pdo);
        }
        ObDereferenceObject(pdo->Common.Self);
        return STATUS_SUCCESS;

    case IRP_MN_QUERY_ID:
        return cmNetQueryId(irp, stack->Parameters.QueryId.IdType);

    case IRP_MN_QUERY_DEVICE_TEXT:
        if (stack->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription) {
            text = cmNetCopyId(cmNetText);
            if (text == NULL) {
                return CmCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            }
            return CmCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)text);
        }
        return CmCompleteIrp(irp, irp->IoStatus.Status, irp->IoStatus.Information);

    case IRP_MN_QUERY_CAPABILITIES:
        return cmNetCapabilities(irp);

    case IRP_MN_QUERY_INTERFACE:
        return cmNetQueryInterface(pdo, irp);

    case IRP_MN_QUERY_PNP_DEVICE_STATE:
        return CmCompleteIrp(irp, STATUS_SUCCESS, irp->IoStatus.Information);

    case IRP_MN_QUERY_DEVICE_RELATIONS:
        if (stack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation) {
            rel = (PDEVICE_RELATIONS)CmAlloc(sizeof(DEVICE_RELATIONS));
            if (rel == NULL) {
                return CmCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            }
            rel->Count = 1;
            rel->Objects[0] = pdo->Common.Self;
            ObReferenceObject(pdo->Common.Self);
            return CmCompleteIrp(irp, STATUS_SUCCESS, (ULONG_PTR)rel);
        }
        return CmCompleteIrp(irp, irp->IoStatus.Status, irp->IoStatus.Information);

    default:
        /* Resources, requirements, bus information and the rest: none. */
        return CmCompleteIrp(irp, irp->IoStatus.Status, irp->IoStatus.Information);
    }
}

NTSTATUS CmNetPdoPower(PCM_NETPDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status;

    stack = IoGetCurrentIrpStackLocation(irp);
    status = irp->IoStatus.Status;
    if (stack->MinorFunction == IRP_MN_SET_POWER) {
        if (stack->Parameters.Power.Type == DevicePowerState) {
            (VOID)PoSetPowerState(pdo->Common.Self, DevicePowerState,
                                  stack->Parameters.Power.State);
        }
        status = STATUS_SUCCESS;
    } else if (stack->MinorFunction == IRP_MN_QUERY_POWER) {
        status = STATUS_SUCCESS;
    } else if (stack->MinorFunction == IRP_MN_WAIT_WAKE) {
        status = STATUS_NOT_SUPPORTED;
    }
    PoStartNextPowerIrp(irp);
    return CmCompleteIrp(irp, status, irp->IoStatus.Information);
}

/* Every other request at the PDO: the 98 SE fallback's private code
 * answered with the table; create and close succeed; WMI keeps its status;
 * the rest is refused. */
NTSTATUS CmNetPdoOther(PCM_NETPDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PXHCI_CM_NET_INTERFACE out;

    stack = IoGetCurrentIrpStackLocation(irp);
    switch (stack->MajorFunction) {
    case IRP_MJ_INTERNAL_DEVICE_CONTROL:
        if (stack->Parameters.DeviceIoControl.IoControlCode == CM_NET_IOCTL_INTERFACE) {
            out = (PXHCI_CM_NET_INTERFACE)stack->Parameters.Others.Argument1;
            if (out == NULL || out->Header.Version != XHCI_CM_NET_VERSION ||
                out->Header.Size < sizeof(XHCI_CM_NET_INTERFACE)) {
                return CmCompleteIrp(irp, STATUS_NOT_SUPPORTED, 0);
            }
            cmNetFill(pdo, out);
            return CmCompleteIrp(irp, STATUS_SUCCESS, 0);
        }
        return CmCompleteIrp(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    case IRP_MJ_CREATE:
    case IRP_MJ_CLOSE:
    case IRP_MJ_CLEANUP:
        return CmCompleteIrp(irp, STATUS_SUCCESS, 0);
    case IRP_MJ_SYSTEM_CONTROL:
        return CmCompleteIrp(irp, irp->IoStatus.Status, irp->IoStatus.Information);
    default:
        return CmCompleteIrp(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}
