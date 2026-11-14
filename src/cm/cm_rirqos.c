/*
 * cm_rirqos.c - the NHI's ring interrupt, its driver half (design record
 * 21 section 4.5, record 22 section 5.4; roadmap-usb4.md task 45.22): the
 * line interrupt of the start's resources, connected on NT once the rings
 * run and the client polls, armed on RX ring 0's Receive Data Buffer cause
 * alone, proven by a self-test, watched at each maintenance pass, and
 * taken down before every ring stop and wherever the client stops polling.
 * Polling stays the fallback: no resource, a connection, arm or self-test
 * that fails, a lost interrupt or a storm leaves the activation polling as
 * it did before, with the idle period unchanged (owner, Q4).
 *
 * THE MESSAGE (sections 4.5.12 and 4.5.14, task 49.27). With
 * XhciCmRingInterrupt 2 on 2000, XP and XP x64 the same cause is delivered
 * by a message msi4nt connects on a claimed IRQ (cm_rirqnt.c). Its routine
 * is this file's, gated by Reach; the connection is kept across a sleep
 * and re-armed at the next Up, and ended at a stop, a fall back and a
 * containment. On 98 SE and ME the value is not tried (CmRirqHow).
 *
 * THE THREE CONTEXTS (section 4.5.3). The service routine (DIRQL, the
 * interrupt object's lock held) reads the status, claims and acknowledges
 * this function's cause, stops a storm by masking the cause itself, and
 * queues the DPC only while armed; it takes no lock of the driver's and
 * touches no ring. The DPC (DISPATCH_LEVEL) raises its sequence, queues
 * CM_WORK_RING through CmThrQueue (WorkLock, never inside a synchronize
 * routine) and lowers the count of DPCs owed as its last access. The
 * thread (PASSIVE_LEVEL) does everything else; its arm, mask and the
 * emergency mask run inside KeSynchronizeExecution, so they never
 * interleave with the routine's own read-modify-write of the mask.
 *
 * THE GATE. Every register write here passes the liveness gate the BAR
 * accessors pass (cm_live.h) - the arm, the self-test's Status Set, the
 * ordinary mask - but two retirement accesses of section 4.5.6: the
 * emergency mask of a D3 and PCI Interrupt Disable (cm_thr.c,
 * CmCfgIntxRetire), each refused once the function is revoked or BAR0
 * unmapped. The service routine reads the status whatever the gate says:
 * the function is in D0 whenever it is connected, and a read that finds
 * nothing (all ones) claims nothing.
 *
 * IRQL: as each function says; CmRirqInit and CmRirqStart from the PnP
 * path, the rest on the connection-manager thread.
 */

#include "cm.h"
#include "cm_nvlink.h"

/* Command bit 10, Interrupt Disable (PCI 2.3). */
#define CM_PCI_COMMAND_INTX_DISABLE 0x0400U

/* The message flag of a translated interrupt descriptor (WDK 7.1's
 * CM_RESOURCE_INTERRUPT_MESSAGE; the Windows 2000 DDK has no name for it). */
#define CM_RIRQ_FLAG_MESSAGE 0x0002U

typedef struct _CM_RIRQ_SYNC {
    PCM_NHI Nhi;
    BOOLEAN Write;          /* the mask may be written                     */
    BOOLEAN Open;           /* unmask (the arm) or mask                    */
    BOOLEAN Proven;         /* the read-back showed it                     */
    ULONG Imr;              /* the mask DW read back                       */
    /* The arm's: what the service routine reads, published under the
     * interrupt object's lock (a retained interrupt is live meanwhile). */
    CM_RIRQ_GEOM Geom;
    ULONG Intel;
    ULONG AutoClearOff;
    ULONG Ring;             /* the data ring to open                       */
    ULONG How;              /* its opening: CM_RIRQ_DATA_*, or _LIVE       */
} CM_RIRQ_SYNC;

/* An opening whose mask was read back neither open nor masked again: the
 * cause may be enabled unwatched, so the whole interrupt falls back. */
#define CM_RIRQ_DATA_LIVE 3UL

static VOID cmRirqDataOpened(PCM_NHI nhi, ULONG how);
static ULONG cmRirqDataTry(PCM_NHI nhi);
static BOOLEAN cmRirqDataShut(PCM_NHI nhi, BOOLEAN lost);
static VOID cmRirqMsgDown(PCM_NHI nhi, ULONG why, BOOLEAN proven);

static ULONG cmRirqRd(PCM_NHI nhi, ULONG offset)
{
    return READ_REGISTER_ULONG((PULONG)(nhi->BarVa + offset));
}

static VOID cmRirqWr(PCM_NHI nhi, ULONG offset, ULONG value)
{
    WRITE_REGISTER_ULONG((PULONG)(nhi->BarVa + offset), value);
}

/* Every status DW read (the host raises no new interrupt until all are,
 * section 4.5.2), into v; where Disable ISR Auto-Clear was found set, the
 * bits read written back to Status Clear when `clear`. IRQL: DIRQL (the
 * routine, a synchronize routine). */
static VOID cmRirqStatus(PCM_NHI nhi, PULONG v, BOOLEAN clear)
{
    CM_RIRQ_DRV *r;
    CM_RIRQ_WRITE w[CM_RIRQ_MAX_DWORDS];
    ULONG i;
    ULONG n;

    r = &nhi->Rirq;
    for (i = 0; i < r->Geom.Dwords && i < CM_RIRQ_MAX_DWORDS; i++) {
        v[i] = cmRirqRd(nhi, TBT_HI_ISR + 4UL * i);
    }
    if (!clear) {
        return;
    }
    n = CmRirqAckWrites(&r->Geom, (BOOLEAN)(r->AutoClearOff != 0), v, w,
                        CM_RIRQ_MAX_DWORDS);
    for (i = 0; i < n; i++) {
        cmRirqWr(nhi, w[i].Offset, w[i].Value);
    }
}

/* Both causes masked by the host's contract (ring 0's alone with no data
 * cause in the geometry), each mask DW read back: TRUE when every one is
 * proven masked (record 35 section 5.5). IRQL: DIRQL. */
static BOOLEAN cmRirqMaskBothNow(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;
    CM_RIRQ_WRITE w[2];
    ULONG imr0;
    ULONG imrData;
    ULONG i;
    ULONG n;

    r = &nhi->Rirq;
    imr0 = 0;
    imrData = 0;
    if (r->Intel) {
        imr0 = cmRirqRd(nhi, TBT_HI_IMR + 4UL * r->Geom.CauseDw);
        imrData = r->Geom.DataDw == r->Geom.CauseDw
                      ? imr0
                      : cmRirqRd(nhi, TBT_HI_IMR + 4UL * r->Geom.DataDw);
    }
    n = CmRirqMaskBoth(&r->Geom, (BOOLEAN)(r->Intel != 0), imr0, imrData, w);
    for (i = 0; i < n; i++) {
        cmRirqWr(nhi, w[i].Offset, w[i].Value);
    }
    imr0 = cmRirqRd(nhi, TBT_HI_IMR + 4UL * r->Geom.CauseDw);
    imrData = r->Geom.DataBit != 0
                  ? cmRirqRd(nhi, TBT_HI_IMR + 4UL * r->Geom.DataDw)
                  : imr0;
    return CmRirqBothMasked(&r->Geom, imr0, imrData);
}

/* The data cause's mask written by the host's contract and read back:
 * TRUE when proven as asked. IRQL: DIRQL. */
static BOOLEAN cmRirqDataMaskNow(PCM_NHI nhi, BOOLEAN open)
{
    CM_RIRQ_DRV *r;
    CM_RIRQ_WRITE w;
    ULONG imr;

    r = &nhi->Rirq;
    imr = 0;
    if (r->Intel) {
        imr = cmRirqRd(nhi, TBT_HI_IMR + 4UL * r->Geom.DataDw);
    }
    CmRirqDataMaskWrite(&r->Geom, (BOOLEAN)(r->Intel != 0), open, imr, &w);
    if (w.Write) {
        cmRirqWr(nhi, w.Offset, w.Value);
    }
    imr = cmRirqRd(nhi, TBT_HI_IMR + 4UL * r->Geom.DataDw);
    return (BOOLEAN)(w.Write && CmRirqDataMaskProven(&r->Geom, open, imr));
}

/* The data cause opened inside an armed interrupt (the thread's
 * CmRirqDataOpen, or the arm): the bit put in the geometry the routine
 * reads, ITR[0] 128 us, the mask written and read back: CM_RIRQ_DATA_OPEN.
 * Not proven, masked again: CM_RIRQ_DATA_UNPROVEN where that mask is read
 * back, else CM_RIRQ_DATA_LIVE with the bit left in the geometry so the
 * routine still claims it. No such ring: CM_RIRQ_DATA_POLL. IRQL: DIRQL. */
static ULONG cmRirqDataOpenNow(PCM_NHI nhi, ULONG ring)
{
    CM_RIRQ_DRV *r;
    CM_RIRQ_GEOM g;

    r = &nhi->Rirq;
    g = r->Geom;
    if (!CmRirqGeomData(&g, ring)) {
        return CM_RIRQ_DATA_POLL;
    }
    r->Geom = g;
    cmRirqWr(nhi, TBT_HI_ITR0, CM_RIRQ_ITR_DATA);
    if (cmRirqDataMaskNow(nhi, TRUE)) {
        r->DataOpen = 1;
        return CM_RIRQ_DATA_OPEN;
    }
    cmRirqWr(nhi, TBT_HI_ITR0, 0);
    if (cmRirqDataMaskNow(nhi, FALSE)) {
        (VOID)CmRirqGeomData(&r->Geom, 0);
        return CM_RIRQ_DATA_UNPROVEN;
    }
    return CM_RIRQ_DATA_LIVE;
}

/* The cause masked by the host's contract, and the mask DW read back.
 * IRQL: DIRQL. */
static BOOLEAN cmRirqMaskNow(PCM_NHI nhi, BOOLEAN open, PULONG imrOut)
{
    CM_RIRQ_DRV *r;
    CM_RIRQ_WRITE w;
    ULONG imr;

    r = &nhi->Rirq;
    imr = 0;
    if (r->Intel) {
        imr = cmRirqRd(nhi, TBT_HI_IMR + 4UL * r->Geom.CauseDw);
    }
    CmRirqMaskWrite(&r->Geom, (BOOLEAN)(r->Intel != 0), open, imr, &w);
    if (w.Write) {
        cmRirqWr(nhi, w.Offset, w.Value);
    }
    imr = cmRirqRd(nhi, TBT_HI_IMR + 4UL * r->Geom.CauseDw);
    *imrOut = imr;
    return (BOOLEAN)(w.Write && CmRirqMaskProven(&r->Geom, open, imr));
}

/*
 * The service routine (section 4.5.3), the line's and the message's
 * (section 4.5.14, cm_rirqnt.c). IRQL: DIRQL, the interrupt object's lock
 * held, the line maybe shared; for the message nothing is touched while
 * Reach is clear.
 */
BOOLEAN CmRirqIsrBody(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;
    ULONG v[CM_RIRQ_MAX_DWORDS];
    ULONG causes;

    r = &nhi->Rirq;
    /* Section 4.5.18: the hardware gate first - nothing is read of a
     * function leaving or out of D0, stopped or gone. */
    if (!CmRirqIsrAdmits((BOOLEAN)(r->Connected != 0),
                         (BOOLEAN)(r->Message != 0), (BOOLEAN)(r->Reach != 0),
                         (ULONG)r->HwGate, (BOOLEAN)(nhi->BarVa != NULL),
                         r->Geom.Dwords)) {
        return FALSE;
    }
    v[0] = 0;
    v[CM_RIRQ_MAX_DWORDS - 1] = 0;
    cmRirqStatus(nhi, v, FALSE);
    switch (CmRirqClassify(&r->Geom, v)) {
    case CM_RIRQ_ISR_ONES:
        (VOID)InterlockedIncrement(&r->Ones);
        return FALSE;
    case CM_RIRQ_ISR_OTHER:
        return FALSE;
    default:
        break;
    }
    causes = CmRirqCauses(&r->Geom, v);
    if (r->AutoClearOff) {
        /* Every DW read written back to Status Clear, the data ring's bit
         * with ring 0's (record 35 section 5.5). */
        CM_RIRQ_WRITE w[CM_RIRQ_MAX_DWORDS];
        ULONG i;
        ULONG n;

        n = CmRirqAckWrites(&r->Geom, TRUE, v, w, CM_RIRQ_MAX_DWORDS);
        for (i = 0; i < n; i++) {
            cmRirqWr(nhi, w[i].Offset, w[i].Value);
        }
    }
#if defined(XHCI_FLAVOUR_QEMU)
    if (r->QemuDead ||
        (r->QemuDeadAfter != 0 && (ULONG)r->Claims >= r->QemuDeadAfter)) {
        /* The lost-interrupt leg: acknowledged, never announced. */
        r->QemuDead = 1;
        (VOID)InterlockedIncrement(&r->QemuDropped);
        return TRUE;
    }
#endif
    (VOID)InterlockedIncrement(&r->Claims);
    if (causes & CM_RIRQ_CAUSE_RING0) {
        (VOID)InterlockedIncrement(&r->Claims0);
    }
    if (causes & CM_RIRQ_CAUSE_DATA) {
        (VOID)InterlockedIncrement(&r->ClaimsData);
    }
    if (CmRirqBurstClaim(&r->Burst, r->DpcSeq)) {
        /* A level line asserted again at once after each acknowledgement,
         * with no DPC between: masked here, where the thread may never run
         * (one processor) - both causes, whichever stormed (record 35
         * section 5.5). Masking only lowers an enable. */
        (VOID)cmRirqMaskBothNow(nhi);
        r->Armed = 0;
        r->DataOpen = 0;
        r->StormMasked = 1;
        return TRUE;
    }
    if (r->Armed) {
        (VOID)InterlockedIncrement(&r->DpcsOwed);
        if (!KeInsertQueueDpc(&r->Dpc, NULL, NULL)) {
            (VOID)InterlockedDecrement(&r->DpcsOwed);
        }
    }
    return TRUE;
}

static BOOLEAN NTAPI cmRirqIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    UNREFERENCED_PARAMETER(Interrupt);

    return CmRirqIsrBody((PCM_NHI)Context);
}

/*
 * Every synchronize routine below, under the routine's exclusion:
 * KeSynchronizeExecution on the connection's interrupt object. IRQL:
 * <= DIRQL, never from the routine.
 */
static BOOLEAN cmRirqSync(PCM_NHI nhi, PKSYNCHRONIZE_ROUTINE routine,
                          PVOID context)
{
    return KeSynchronizeExecution(nhi->Rirq.Object, routine, context);
}

/* The DPC (section 4.5.3). IRQL: DISPATCH_LEVEL. It never touches the
 * interrupt object; the count's decrement is its last access. */
static VOID NTAPI cmRirqDpc(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2)
{
    PCM_NHI nhi;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    nhi = (PCM_NHI)Context;
    (VOID)InterlockedIncrement(&nhi->Rirq.DpcSeq);
    (VOID)CmThrQueue(nhi, CM_WORK_RING, NULL);
    /* The network service kicked on every claim while its cause is open
     * (record 35 section 5.5); its descriptors decide, as the poll's. */
    if (nhi->Rirq.DataOpen) {
        CmNetIrqKick(nhi);
    }
    (VOID)InterlockedDecrement(&nhi->Rirq.DpcsOwed);
}

/* The arm (section 4.5.2): the stale status read, ITR[0] 0, the cause
 * unmasked and read back, IrqArmed - or the cause masked again and nothing
 * armed. IRQL: DIRQL (KeSynchronizeExecution). */
static BOOLEAN NTAPI cmRirqSyncArm(PVOID Context)
{
    CM_RIRQ_SYNC *s;
    PCM_NHI nhi;
    ULONG v[CM_RIRQ_MAX_DWORDS];
    ULONG imr;

    s = (CM_RIRQ_SYNC *)Context;
    nhi = s->Nhi;
    s->Proven = FALSE;
    nhi->Rirq.Geom = s->Geom;
    nhi->Rirq.Intel = s->Intel;
    nhi->Rirq.AutoClearOff = s->AutoClearOff;
    cmRirqStatus(nhi, v, TRUE);
    cmRirqWr(nhi, TBT_HI_ITR0, 0);
    /* A data cause carried from before (its mask never proven, record 35
     * section 5.5) is masked and proven first, and leaves the geometry
     * only then; not proven, nothing is armed and the routine still
     * claims it. */
    if (nhi->Rirq.Geom.DataBit != 0) {
        if (!cmRirqDataMaskNow(nhi, FALSE)) {
            return FALSE;
        }
        (VOID)CmRirqGeomData(&nhi->Rirq.Geom, 0);
    }
    CmRirqBurstReset(&nhi->Rirq.Burst, nhi->Rirq.DpcSeq);
    nhi->Rirq.StormMasked = 0;
    nhi->Rirq.DataOpen = 0;
    /* The message's routine reads the function from here (section
     * 4.5.12): the function in D0, BAR0 mapped, the cause being armed. */
    nhi->Rirq.Reach = 1;
    nhi->Rirq.Armed = 1;
    s->Proven = cmRirqMaskNow(nhi, TRUE, &s->Imr);
    if (!s->Proven) {
        nhi->Rirq.Armed = 0;
        /* Reach cleared only once the cause is proven masked again
         * (section 4.5.16): a cause that may be open keeps being
         * acknowledged rather than answered FALSE on a shared vector. */
        if (cmRirqMaskNow(nhi, FALSE, &imr)) {
            nhi->Rirq.Reach = 0;
        }
        return FALSE;
    }
    /* The data ring's cause is opened by the thread after the self-test
     * (cmRirqDataTry), so its interrupts never pass ring 0's test. */
    return TRUE;
}

/* The thread's CmRirqDataOpen inside the interrupt object's lock. IRQL:
 * DIRQL (KeSynchronizeExecution). */
static BOOLEAN NTAPI cmRirqSyncDataOpen(PVOID Context)
{
    CM_RIRQ_SYNC *s;

    s = (CM_RIRQ_SYNC *)Context;
    if (!s->Nhi->Rirq.Armed) {
        s->How = CM_RIRQ_DATA_POLL;
    } else if (s->Nhi->Rirq.DataOpen) {
        s->How = CM_RIRQ_DATA_OPEN;
    } else {
        s->How = cmRirqDataOpenNow(s->Nhi, s->Ring);
    }
    s->Proven = (BOOLEAN)(s->How == CM_RIRQ_DATA_OPEN);
    return s->Proven;
}

/* The data cause masked (the network service's rundown), ITR[0] back to
 * 0, its bit out of the geometry once proven masked. IRQL: DIRQL
 * (KeSynchronizeExecution). */
static BOOLEAN NTAPI cmRirqSyncDataClose(PVOID Context)
{
    CM_RIRQ_SYNC *s;
    PCM_NHI nhi;

    s = (CM_RIRQ_SYNC *)Context;
    nhi = s->Nhi;
    nhi->Rirq.DataOpen = 0;
    s->Proven = TRUE;
    if (nhi->Rirq.Geom.DataBit == 0) {
        return TRUE;
    }
    if (!s->Write) {
        /* The host interface lost: nothing written, the bit kept so a
         * routine still claims what it reads (record 35 section 5.5). */
        return TRUE;
    }
    s->Proven = cmRirqDataMaskNow(nhi, FALSE);
    if (s->Proven) {
        (VOID)CmRirqGeomData(&nhi->Rirq.Geom, 0);
    }
    cmRirqWr(nhi, TBT_HI_ITR0, 0);
    return s->Proven;
}

/* The order down's step 1 (section 4.5.6): IrqArmed cleared, and where the
 * gate admits it the cause masked, read back, and the status read so that
 * a level held by it falls. IRQL: DIRQL (KeSynchronizeExecution). */
static BOOLEAN NTAPI cmRirqSyncDisarm(PVOID Context)
{
    CM_RIRQ_SYNC *s;
    PCM_NHI nhi;
    ULONG v[CM_RIRQ_MAX_DWORDS];

    s = (CM_RIRQ_SYNC *)Context;
    nhi = s->Nhi;
    nhi->Rirq.Armed = 0;
    nhi->Rirq.DataOpen = 0;
    s->Proven = FALSE;
    if (s->Write && nhi->BarVa != NULL && nhi->Rirq.Geom.Dwords != 0) {
        /* Both causes (record 35 section 5.5); the data bit leaves the
         * geometry once proven masked, else the routine still claims it. */
        s->Proven = cmRirqMaskBothNow(nhi);
        s->Imr = cmRirqRd(nhi, TBT_HI_IMR + 4UL * nhi->Rirq.Geom.CauseDw);
        if (s->Proven && nhi->Rirq.Geom.DataBit != 0) {
            (VOID)CmRirqGeomData(&nhi->Rirq.Geom, 0);
            cmRirqWr(nhi, TBT_HI_ITR0, 0);
        }
        cmRirqStatus(nhi, v, TRUE);
    }
    /* The causes masked before Reach is cleared (section 4.5.14). */
    nhi->Rirq.Reach = 0;
    return s->Proven;
}

/* The ordinary write path open: BAR0 mapped, the function in D0, the gate
 * admitting the class the thread runs under, as cmBarWrite asks. */
static BOOLEAN cmRirqMayWrite(PCM_NHI nhi)
{
    return (BOOLEAN)(nhi->BarVa != NULL &&
                     nhi->DevicePower == PowerDeviceD0 &&
                     nhi->AccessClass != CM_ACCESS_PROBE &&
                     nhi->AccessClass != CM_ACCESS_PREPARE &&
                     CmLiveAdmits(&nhi->Live, nhi->AccessClass));
}

/* The retirement accesses' rule (section 4.5.6 step 4): never once
 * revoked, never with BAR0 unmapped, never outside D0. */
static BOOLEAN cmRirqMayRetire(PCM_NHI nhi)
{
    return (BOOLEAN)(!nhi->Live.Revoked &&
                     nhi->Live.State != CM_LIVE_REVOKED &&
                     nhi->BarVa != NULL &&
                     nhi->DevicePower == PowerDeviceD0);
}

static ULONG cmRirqNow(PCM_NHI nhi)
{
    return nhi->Host.NowMs(nhi->Host.Context);
}

static VOID cmRirqSleep1(PCM_NHI nhi)
{
    nhi->Host.Sleep(nhi->Host.Context, 1);
}

VOID CmRirqInit(PCM_NHI nhi)
{
    CmZero(&nhi->Rirq, sizeof(nhi->Rirq));
    nhi->Rirq.HwGate = (LONG)(CM_RIRQ_GATE_POWER | CM_RIRQ_GATE_GONE);
    KeInitializeDpc(&nhi->Rirq.Dpc, cmRirqDpc, nhi);
}

/* ---------------------------------------------------------------------- */
/* The routine's hardware gate (section 4.5.18)                           */
/* ---------------------------------------------------------------------- */

VOID CmRirqHwGateCloseLocked(PCM_NHI nhi, ULONG why)
{
    nhi->Rirq.HwGate = (LONG)CmRirqGateClose((ULONG)nhi->Rirq.HwGate, why);
}

VOID CmRirqHwGateClose(PCM_NHI nhi, ULONG why)
{
    KIRQL irql;

    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    CmRirqHwGateCloseLocked(nhi, why);
    KeReleaseSpinLock(&nhi->WorkLock, irql);
}

/* Nothing but the exclusion itself: no routine that read the gate open is
 * still running when it returns. IRQL: DIRQL. */
static BOOLEAN NTAPI cmRirqSyncFence(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return TRUE;
}

/* A closing reason added, then the fence where a connection exists. The
 * thread alone (it connects and disconnects: the interrupt object stays
 * while used), never with WorkLock held. PASSIVE_LEVEL. */
static VOID cmRirqGateShut(PCM_NHI nhi, ULONG why)
{
    CM_RIRQ_SYNC s;

    CmRirqHwGateClose(nhi, why);
    if (!nhi->Rirq.Connected) {
        return;
    }
    CmZero(&s, sizeof(s));
    s.Nhi = nhi;
    (VOID)cmRirqSync(nhi, cmRirqSyncFence, &s);
}

VOID CmRirqHwGateOpen(PCM_NHI nhi)
{
    KIRQL irql;
    LONG was;

    if (((ULONG)nhi->Rirq.HwGate & CM_RIRQ_GATE_POWER) == 0) {
        return;
    }
    KeAcquireSpinLock(&nhi->WorkLock, &irql);
    was = nhi->Rirq.HwGate;
    if (CmRirqGateMayOpen((BOOLEAN)(nhi->DevicePower == PowerDeviceD0),
                          (BOOLEAN)(nhi->BarVa != NULL),
                          (BOOLEAN)(nhi->Suspended != 0),
                          (BOOLEAN)(nhi->SystemPower == PowerSystemWorking),
                          (BOOLEAN)(nhi->Live.Revoked != 0))) {
        nhi->Rirq.HwGate = (LONG)CmRirqGateOpenPower((ULONG)was);
    }
    KeReleaseSpinLock(&nhi->WorkLock, irql);
    if (nhi->Rirq.HwGate != was) {
        CmLogNote(nhi, "cm.irq.gate", (ULONG)nhi->Rirq.HwGate);
    }
}

VOID CmRirqStart(PCM_NHI nhi, PCM_PARTIAL_RESOURCE_LIST translated)
{
    CM_RIRQ_DRV *r;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR d;
    ULONG i;

    r = &nhi->Rirq;
    /* Section 4.5.18: nothing connected yet; GONE cleared by a start
     * alone, POWER unless the function is in D0 with BAR0 mapped. */
    {
        KIRQL irql;

        KeAcquireSpinLock(&nhi->WorkLock, &irql);
        r->HwGate = (LONG)CmRirqGateAtStart(
            (BOOLEAN)(nhi->DevicePower == PowerDeviceD0),
            (BOOLEAN)(nhi->BarVa != NULL));
        KeReleaseSpinLock(&nhi->WorkLock, irql);
    }
    r->HaveLine = 0;
    r->HaveMessage = 0;
    r->Value = nhi->Values.RingIrq;
    /* Section 4.5.14: which library serves value 2, if any. */
    r->Sys = !IoIsWdmVersionAvailable(1, 0x10)
                 ? CM_RIRQ_SYS_9X
                 : (IoIsWdmVersionAvailable(6, 0) ? CM_RIRQ_SYS_NT6
                                                  : CM_RIRQ_SYS_NT5);
    r->Yield = (r->Sys == CM_RIRQ_SYS_NT5 &&
                nhi->Values.UsbInterruptMode == 4UL)
                   ? 1UL
                   : 0UL;
    r->Failed = 0;
    r->Why = CM_Q_IRQ_WHY_NONE;
    r->SelfTestOwed = 1;
    r->IrqMode = CM_RIRQ_MODE_POLL;
    r->Intel = 0;
    r->AutoClearOff = 0;
    r->CommandRead = 0;
    r->IntxOurs = 0;
    r->Retained = 0;
    r->Armed = 0;
    r->StormMasked = 0;
    r->QemuDeadAfter = nhi->Values.QemuIrqDead;
    r->QemuDead = 0;
    r->QemuDropped = 0;
    r->DataWant = 0;
    r->DataOpen = 0;
    CmZero(&r->Geom, sizeof(r->Geom));
    CmZero(&r->Watch, sizeof(r->Watch));
    r->Connects = 0;
    r->ConnectFailures = 0;
    r->ArmFailures = 0;
    r->Wakes = 0;
    r->SelfTests = 0;
    r->SelfTestFailures = 0;
    r->Suspects = 0;
    r->Lost = 0;
    r->Storms = 0;
    r->Downs = 0;
    r->Unretired = 0;
    r->Disconnects = 0;
    r->DpcSlow = 0;
    /* Section 4.5.12. Every stop ends a message connection, so none is
     * left from the last activation. */
    r->Message = 0;
    r->MsgInfo = NULL;
    r->ReArmOwed = 0;
    r->MsgBdf = CM_RIRQ_NO_BDF;
    r->MsgVector = 0;
    r->Reach = 0;
    r->MsgConnects = 0;
    r->MsgConnectFailures = 0;
    r->MsgRearms = 0;
    r->MsgRearmFailures = 0;
    r->MsgForced = 0;
    r->MsgHandoffFailures = 0;
    r->MsgStatus = 0;
    r->MsgMaskWatch = 0;
    r->MsgBraked = 0;
    r->MsgClaimHeld = 0;
    r->MsgIrq = 0;
    r->Removal = 0;
    r->MsgIdSet = 0;
    r->ProofRead = 0;
    for (i = 0; translated != NULL && i < translated->Count; i++) {
        d = &translated->PartialDescriptors[i];
        if (d->Type != CmResourceTypeInterrupt) {
            continue;
        }
        if ((d->Flags & CM_RIRQ_FLAG_MESSAGE) != 0) {
            r->HaveMessage = 1;
            break;
        }
        r->Vector = d->u.Interrupt.Vector;
        r->Irql = (KIRQL)d->u.Interrupt.Level;
        r->Affinity = d->u.Interrupt.Affinity;
        r->Mode = (d->Flags & CM_RESOURCE_INTERRUPT_LATCHED) != 0
                      ? Latched
                      : LevelSensitive;
        r->Shared = (BOOLEAN)(d->ShareDisposition == CmResourceShareShared);
        r->HaveLine = 1;
        break;
    }
    CmLogNote(nhi, "cm.irq.value", r->Value);
    if (r->QemuDeadAfter != 0) {
        CmLogNote(nhi, "cm.irq.qemu.dead-after", r->QemuDeadAfter);
    }
    CmLogNote(nhi, "cm.irq.resource",
              (r->HaveLine ? 1UL : 0UL) | (r->HaveMessage ? 2UL : 0UL) |
                  ((ULONG)r->Irql << 8) | (r->Shared ? 0x10000UL : 0UL) |
                  (r->Mode == Latched ? 0x20000UL : 0UL));
    r->How = CmRirqHow(r->Value, r->Sys, (BOOLEAN)(r->Yield != 0),
                       (BOOLEAN)(r->HaveLine != 0), FALSE);
    if (r->Value == CM_RIRQ_VALUE_MESSAGE && r->How != CM_RIRQ_HOW_MESSAGE) {
        /* Section 4.5.14: 2 runs as 1 on Vista and later, and on 2000 and
         * XP yielding to a machine-wide mode 4; on 98 SE and ME it is not
         * tried. */
        CmLogNote(nhi, r->Yield ? "cm.irq.msg.yield" : "cm.irq.msg.system",
                  (r->Sys << 8) | r->How);
    }
}

/* How this activation tries the interrupt, the latch read now. */
static ULONG cmRirqHowNow(PCM_NHI nhi)
{
    return CmRirqHow(nhi->Rirq.Value, nhi->Rirq.Sys,
                     (BOOLEAN)(nhi->Rirq.Yield != 0),
                     (BOOLEAN)(nhi->Rirq.HaveLine != 0),
                     (BOOLEAN)(nhi->Rirq.Failed != 0));
}

static BOOLEAN cmRirqWanted(PCM_NHI nhi)
{
    return (BOOLEAN)(cmRirqHowNow(nhi) != CM_RIRQ_HOW_NONE);
}

VOID CmRirqIntent(PCM_NHI nhi)
{
    if (nhi->Rirq.Connected && !nhi->Rirq.Retained &&
        !(nhi->Rirq.Message && nhi->Rirq.ReArmOwed && !nhi->Rirq.Armed)) {
        /* (A message kept across a sleep, disarmed and masked, waits for
         * its re-arm at the pass's Up: section 4.5.12.) */
        /* Every ring start masks every cause (TbtRingStartEx): an armed
         * interrupt is taken down first - a full recovery can run and
         * return to polling inside one pass - so the pass's Up re-arms
         * it. A retained one stays acknowledge-only through the start. */
        CmRirqDown(nhi, CM_RIRQ_DOWN_CONTAIN);
    }
    nhi->Ring.RxIe = cmRirqWanted(nhi);
}

BOOLEAN CmRirqTimed(PCM_NHI nhi)
{
    return (BOOLEAN)(nhi->Rirq.Armed != 0);
}

/* The maintenance deadline is the last pass plus the period in force now,
 * so a period that shrinks (a Stor98 entry reading UP) brings it forward
 * at once and one that grows defers it, the elapsed time kept. */
ULONG CmRirqLeft(PCM_NHI nhi, ULONG periodMs)
{
    if (nhi->Rirq.MaintOwed) {
        return 0;
    }
    return CmRirqLeftMs(cmRirqNow(nhi), nhi->Rirq.MaintLast + periodMs,
                        periodMs);
}

BOOLEAN CmRirqMaintenance(PCM_NHI nhi, ULONG periodMs)
{
    if (!nhi->Rirq.Armed || nhi->Rirq.MaintOwed) {
        return TRUE;
    }
    return CmRirqDue(cmRirqNow(nhi), nhi->Rirq.MaintLast + periodMs);
}

/* PCI Interrupt Disable cleared where this driver set it (section
 * 4.5.6.1), through the ordinary path, at an Up. */
static VOID cmRirqIntxRestore(PCM_NHI nhi)
{
    ULONG v;

    if (!nhi->Rirq.IntxOurs) {
        return;
    }
    v = CmCfgRead32(nhi, CM_PCI_COMMAND);
    if (v == 0xFFFFFFFFUL) {
        return;
    }
    if ((v & CM_PCI_COMMAND_INTX_DISABLE) == 0 ||
        CmCfgWrite16(nhi, CM_PCI_COMMAND,
                     (USHORT)((v & 0xFFFFUL) & ~CM_PCI_COMMAND_INTX_DISABLE))) {
        nhi->Rirq.IntxOurs = 0;
        CmLogNote(nhi, "cm.irq.intx.restored", v & 0xFFFFUL);
    }
}

/*
 * The order down (section 4.5.6): disarm and mask, the DPCs drained while
 * still connected, the source proven quiet by a read-back - the mask, an
 * emergency mask in a D3, or for a line PCI Interrupt Disable - then
 * disconnected, retained, or disconnected unproven, and the failure latch
 * where the end asks for it. RX descriptors are posted without IE from
 * here. PASSIVE_LEVEL, the thread.
 */
static VOID cmRirqDownBody(PCM_NHI nhi, ULONG why);

/* The order down, then section 4.5.18's gate for a D3 (POWER) or a stop
 * (GONE), whatever the order down's outcome: a connection kept or held
 * answers "not mine" from here with nothing read. */
VOID CmRirqDown(PCM_NHI nhi, ULONG why)
{
    cmRirqDownBody(nhi, why);
    if (why == CM_RIRQ_DOWN_D3) {
        cmRirqGateShut(nhi, CM_RIRQ_GATE_POWER);
    } else if (why == CM_RIRQ_DOWN_STOP) {
        cmRirqGateShut(nhi, CM_RIRQ_GATE_GONE);
    }
}

static VOID cmRirqDownBody(PCM_NHI nhi, ULONG why)
{
    CM_RIRQ_DRV *r;
    CM_RIRQ_SYNC s;
    ULONG start;
    ULONG end;
    ULONG wasOurs;
    BOOLEAN slow;
    BOOLEAN proven;

    r = &nhi->Rirq;
    nhi->Ring.RxIe = FALSE;
    if (!r->Connected) {
        r->Armed = 0;
        r->DataOpen = 0;
        if (why == CM_RIRQ_DOWN_FALLBACK) {
            r->Failed = 1;
        }
        return;
    }
    r->Downs++;
    if (r->DataOpen && why == CM_RIRQ_DOWN_FALLBACK) {
        /* The data ring's cause latched to polling with ring 0's (record
         * 35 section 5.5): the service's poll carries it from here. */
        CmLogNote(nhi, "cm.net.irq.latched", r->Geom.DataRing);
    }
    CmRirqWatchDataStart(&r->Watch, FALSE, 0, 0);
    s.Nhi = nhi;
    s.Write = cmRirqMayWrite(nhi);
    s.Open = FALSE;
    s.Proven = FALSE;
    s.Imr = 0;
    s.Ring = 0;
    s.How = 0;
    (VOID)cmRirqSync(nhi, cmRirqSyncDisarm, &s);
    proven = s.Proven;
    /* 2: the DPCs queued before the disarm, drained while the interrupt
     * is still connected; no new one can be queued now. */
    start = cmRirqNow(nhi);
    slow = FALSE;
    while (r->DpcsOwed != 0) {
        cmRirqSleep1(nhi);
        if (!slow && cmRirqNow(nhi) - start > CM_RIRQ_DPC_SLOW_MS) {
            slow = TRUE;
            r->DpcSlow++;
            CmLogNote(nhi, "cm.irq.dpc.slow", (ULONG)r->DpcsOwed);
        }
    }
    /* 3 and 4: a mask refused (the gate) or not read back. */
    if (!proven && why == CM_RIRQ_DOWN_D3 && cmRirqMayRetire(nhi)) {
        s.Write = TRUE;
        (VOID)cmRirqSync(nhi, cmRirqSyncDisarm, &s);
        proven = s.Proven;
        CmLogNote(nhi, "cm.irq.emergency-mask", proven ? 1UL : 0UL);
    }
    if (r->Message) {
        /* Section 4.5.14: Interrupt Disable suppresses no message; the
         * library's disconnect is this connection's retirement. */
        cmRirqMsgDown(nhi, why, proven);
        return;
    }
    if (!proven && r->Mode == LevelSensitive && cmRirqMayRetire(nhi)) {
        wasOurs = r->IntxOurs;
        proven = CmCfgIntxRetire(nhi, &r->IntxOurs);
        CmLogNote(nhi, "cm.irq.intx.disable",
                  (proven ? 1UL : 0UL) |
                      ((r->IntxOurs && !wasOurs) ? 2UL : 0UL));
    }
    end = CmRirqDownEnd(why, proven);
    CmLogNote(nhi, "cm.irq.down", (why << 8) | (end << 4) |
                                      (proven ? 1UL : 0UL));
    if (end == CM_RIRQ_END_RETAIN) {
        r->Retained = 1;
        r->Unretired++;
        r->RetryDue = cmRirqNow(nhi) +
                      CmNvlinkIdleMs(nhi->Values.Value[CM_VAL_POLL_IDLE_MS]);
        CmLogNote(nhi, "cm.irq.unretired", why);
    } else {
        if (end == CM_RIRQ_END_UNPROVEN) {
            r->Unretired++;
            CmLogNote(nhi, "cm.irq.unretired", why);
        }
        IoDisconnectInterrupt(r->Object);
        r->Connected = 0;
        r->Object = NULL;
        r->Retained = 0;
        r->Disconnects++;
    }
    r->IrqMode = CM_RIRQ_MODE_POLL;
    if (CmRirqDownFails(why, end)) {
        r->Failed = 1;
        if (why != CM_RIRQ_DOWN_FALLBACK) {
            /* Section 4.5.17: an order down that could not prove the
             * function silent; a fall back's caller said why. */
            r->Why = CM_Q_IRQ_WHY_TEARDOWN;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* The message (sections 4.5.12 and 4.5.14, task 49.27)                  */
/* ---------------------------------------------------------------------- */

/* PCI 3.0 6.8: the MSI and MSI-X capability ids, and the Enable bit of
 * each one's Message Control word. */
#define CM_CAP_MSI          0x05UL
#define CM_CAP_MSIX         0x11UL
#define CM_MSI_ENABLE       0x0001UL
#define CM_MSIX_ENABLE      0x8000UL

/*
 * The handoff check through this driver's own configuration route: the
 * capability list from 34h, no pointer below 40h, at most 48 entries, an
 * all-ones read unknown; MSI-X Enable (and, unless `msixOnly`, MSI Enable)
 * looked at. With `write` (only on a function the library has just
 * connected for this driver, section 4.5.12 step 5) one found set is
 * cleared and read back; without it nothing is written, since another
 * connection could own the function. TRUE when every such Enable reads
 * clear; *found the Enables found set (1 MSI, 2 MSI-X). PASSIVE_LEVEL,
 * the thread.
 */
static BOOLEAN cmRirqHandoff(PCM_NHI nhi, BOOLEAN msixOnly, BOOLEAN write,
                             PULONG found)
{
    ULONG v;
    ULONG ptr;
    ULONG hdr;
    ULONG bit;
    ULONG guard;

    *found = 0;
    v = CmCfgRead32(nhi, CM_PCI_COMMAND);
    if (v == 0xFFFFFFFFUL) {
        return FALSE;
    }
    if ((v & (0x10UL << 16)) == 0) {
        return TRUE;                    /* no capability list: no MSI */
    }
    ptr = CmCfgRead32(nhi, 0x34);
    if (ptr == 0xFFFFFFFFUL) {
        return FALSE;
    }
    ptr &= 0xFCUL;
    for (guard = 0;; guard++) {
        if (ptr == 0) {
            return TRUE;
        }
        if (ptr < 0x40UL || guard >= 48UL) {
            return FALSE;
        }
        hdr = CmCfgRead32(nhi, ptr);
        if (hdr == 0xFFFFFFFFUL) {
            return FALSE;
        }
        bit = 0;
        if ((hdr & 0xFFUL) == CM_CAP_MSI && !msixOnly) {
            bit = CM_MSI_ENABLE;
        } else if ((hdr & 0xFFUL) == CM_CAP_MSIX) {
            bit = CM_MSIX_ENABLE;
        }
        if (bit != 0 && ((hdr >> 16) & bit) != 0) {
            *found |= (bit == CM_MSI_ENABLE) ? 1UL : 2UL;
            if (!write) {
                return FALSE;
            }
            if (!CmCfgWrite16(nhi, ptr + 2,
                              (USHORT)(((hdr >> 16) & 0xFFFFUL) & ~bit))) {
                return FALSE;
            }
            hdr = CmCfgRead32(nhi, ptr);
            if (hdr == 0xFFFFFFFFUL || ((hdr >> 16) & bit) != 0) {
                return FALSE;
            }
        }
        ptr = (hdr >> 8) & 0xFCUL;
    }
}

/* Every Interrupt Mask DW read back 0 (all 3N causes masked, none all
 * ones): TRUE. IRQL: PASSIVE_LEVEL, the thread. */
static BOOLEAN cmRirqMasksZero(PCM_NHI nhi, PULONG first)
{
    ULONG i;
    ULONG n;
    ULONG v;

    *first = 0;
    n = TBT_HI_IMR_DWORDS(nhi->Ring.Paths);
    if (nhi->Ring.Paths == 0 || n > CM_RIRQ_MAX_DWORDS) {
        return FALSE;
    }
    for (i = 0; i < n; i++) {
        v = nhi->Host.Read32(nhi->Host.Context, TBT_HI_IMR + 4UL * i);
        if (v != 0) {
            *first = v;
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * "When the library says no" (section 4.5.12): the activation runs polled
 * with every cause masked, proven by the mask read back; where it does not
 * read back masked the masks are written once more by the host's contract
 * (section 4.3) and read again, and the read-back is then repeated once a
 * pass (MsgMaskWatch). PASSIVE_LEVEL, the thread.
 */
static VOID cmRirqMsgSilent(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;
    ULONG first;
    ULONG i;
    ULONG n;
    BOOLEAN intel;

    r = &nhi->Rirq;
    if (cmRirqMasksZero(nhi, &first)) {
        CmLogNote(nhi, "cm.irq.msg.masked", 1);
        return;
    }
    if (cmRirqMayWrite(nhi) && nhi->Ring.Paths != 0) {
        n = TBT_HI_IMR_DWORDS(nhi->Ring.Paths);
        intel = (BOOLEAN)(nhi->Hw != NULL &&
                          (nhi->Hw->Flags & TBT_HW_INTEL_INTERRUPTS) != 0);
        for (i = 0; i < n && i < CM_RIRQ_MAX_DWORDS; i++) {
            nhi->Host.Write32(nhi->Host.Context,
                              (intel ? TBT_HI_IMR : TBT_HI_IMC) + 4UL * i,
                              intel ? 0UL : 0xFFFFFFFFUL);
        }
        if (cmRirqMasksZero(nhi, &first)) {
            CmLogNote(nhi, "cm.irq.msg.masked", 2);
            return;
        }
    }
    r->MsgMaskWatch = first | 1UL;
    CmLogNote(nhi, "cm.irq.msg.unmasked", first);
}

/* No message for this activation, nothing registered: the latch, then
 * the handoff check the contract asks of a client that falls back, read
 * only (the library may not have checked who owns the function), and where
 * it does not read both Enables clear the masks proven. PASSIVE_LEVEL,
 * the thread. */
static VOID cmRirqMsgNone(PCM_NHI nhi, BOOLEAN handoff)
{
    CM_RIRQ_DRV *r;
    ULONG found;

    r = &nhi->Rirq;
    CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
    found = 0;
    if (handoff && cmRirqHandoff(nhi, FALSE, FALSE, &found)) {
        if (found != 0) {
            CmLogNote(nhi, "cm.irq.msg.handoff", found);
        }
        return;
    }
    if (handoff) {
        r->MsgHandoffFailures++;
        CmLogNote(nhi, "cm.irq.msg.handoff", 0x80000000UL | found);
    }
    cmRirqMsgSilent(nhi);
}

/* A claim the library kept (CLAIM_KEPT, from the claim or a release;
 * section 4.5.14 item 2): the function refused until the restart (the
 * image is resident since before the claim). */
static VOID cmRirqMsgKept(PCM_NHI nhi, ULONG status)
{
    if (status == CM_MN_CLAIM_KEPT) {
        nhi->Rirq.Failed = 1;
        nhi->Rirq.Why = CM_Q_IRQ_WHY_REFUSED;
        CmLogNote(nhi, CmRirqRefuse(nhi, nhi->Rirq.MsgBdf)
                           ? "cm.irq.msg.kept"
                           : "cm.irq.msg.table-full",
                  nhi->Rirq.MsgBdf);
    } else if (status != CM_MB_OK) {
        CmLogNote(nhi, "cm.irq.msg.release", status);
    }
}

/* NT's location (section 4.5.14 item 1): the PDO's bus number and
 * address, the function's identity DW read through its own route and
 * matched with the start's. One found, or none. */
static VOID cmRirqNtWhere(PCM_NHI nhi, PULONG found, PULONG at,
                          PBOOLEAN busKnown, PULONG bus)
{
    ULONG b;
    ULONG address;
    ULONG bytes;
    ULONG id;

    *found = 0;
    *at = CM_RIRQ_NO_BDF;
    *busKnown = FALSE;
    *bus = 0;
    b = 0;
    address = 0;
    if (!NT_SUCCESS(IoGetDeviceProperty(nhi->Common.Pdo,
                                        DevicePropertyBusNumber, sizeof(b),
                                        &b, &bytes)) ||
        b > 0xFFUL ||
        !NT_SUCCESS(IoGetDeviceProperty(nhi->Common.Pdo,
                                        DevicePropertyAddress,
                                        sizeof(address), &address, &bytes)) ||
        (address >> 16) > 0x1FUL || (address & 0xFFFFUL) > 7UL) {
        return;
    }
    id = CmCfgRead32(nhi, 0);
    if (id != ((nhi->Device << 16) | nhi->Vendor)) {
        return;
    }
    *found = 1;
    *at = XHCI_TBT_BDF(b, address >> 16, address & 0xFFFFUL);
    *busKnown = TRUE;
    *bus = b;
}

/* The Bus Master bit cleared and read back before an NT quarantine
 * (msi4nt's contract; hcd_msgnt.c's order), through the narrow
 * retirement class that passes the gate's recoverable states. The
 * quarantine is made whatever this proves - the connection must end
 * before the extension it points at goes - and an unproven clear is
 * logged as what it is (section 4.5.14 item 4's residual). */
static VOID cmRirqMsgBusMasterOff(PCM_NHI nhi)
{
    BOOLEAN proven;

    proven = CmCfgBusMasterRetire(nhi);
    CmLogNote(nhi, proven ? "cm.irq.msg.bm" : "cm.irq.msg.bm.unproven",
              nhi->Live.State);
}

/* A braked connection (section 4.5.16; msi4nt's MSGIRQ_STATUS_BRAKED): never connected again in this boot - the
 * function entered in the image's refusal table - then the caller's
 * disconnect, the causes masked, and the poll. */
static VOID cmRirqMsgBraked(PCM_NHI nhi, ULONG status)
{
    if (status == CM_MB_BRAKED) {
        nhi->Rirq.MsgBraked++;
        nhi->Rirq.Why = CM_Q_IRQ_WHY_BRAKED;
        CmLogNote(nhi, CmRirqRefuse(nhi, nhi->Rirq.MsgBdf)
                           ? "cm.irq.msg.braked"
                           : "cm.irq.msg.table-full",
                  nhi->Rirq.MsgBdf);
    }
}

/*
 * The connection (section 4.5.12 steps 1 to 6). TRUE when connected with
 * MSI-X Enable proven clear; otherwise the activation is polling, latched,
 * every connection ended. PASSIVE_LEVEL, the thread, nothing connected.
 */
static BOOLEAN cmRirqMsgConnect(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;
    ULONG st;
    ULONG found;
    ULONG at;
    ULONG bus;
    ULONG bdf;
    ULONG data;
    BOOLEAN busKnown;

    r = &nhi->Rirq;
    st = CmRirqNtInit(nhi);
    if (st != CM_MB_OK) {
        r->MsgStatus = st;
        r->MsgConnectFailures++;
        r->Why = CmRirqWhyOfStatus(st);
        CmLogNote(nhi, "cm.irq.msg.init", st);
        cmRirqMsgNone(nhi, TRUE);
        return FALSE;
    }
    cmRirqNtWhere(nhi, &found, &at, &busKnown, &bus);
    bdf = CmRirqMsgWhere(found, at, busKnown, bus);
    if (bdf == CM_RIRQ_NO_BDF) {
        r->MsgConnectFailures++;
        CmLogNote(nhi, "cm.irq.msg.where",
                  0x80000000UL | (found << 16) | (busKnown ? 0x100UL : 0) |
                      (bus & 0xFFUL));
        /* Nothing was asked of the library: the function as the ring start
         * left it, every cause masked. */
        r->Why = CM_Q_IRQ_WHY_NO_MSI;
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return FALSE;
    }
    CmLogNote(nhi, "cm.irq.msg.where", bdf);
    if (CmRirqRefused(nhi, bdf)) {
        CmLogNote(nhi, "cm.irq.msg.refused", bdf);
        r->Why = CM_Q_IRQ_WHY_REFUSED;
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return FALSE;
    }
    r->MsgBdf = bdf;
    /* Section 4.5.14: resident before the first claim, so no path to a
     * kept claim can unload the image. */
    CmDriverObject->DriverUnload = NULL;
    st = CmRirqNtClaim(nhi, &r->MsgIrq);
    CmLogNote(nhi, "cm.irq.msg.claim", (r->MsgIrq << 16) | st);
    if (st != CM_MB_OK) {
        r->MsgStatus = st;
        r->MsgConnectFailures++;
        r->Why = CmRirqWhyOfStatus(st);
        cmRirqMsgKept(nhi, st);
        cmRirqMsgNone(nhi, TRUE);
        return FALSE;
    }
    r->Reach = 0;
    r->Message = 1;
    r->Connected = 1;
    st = CmRirqNtConnect(nhi);
    r->MsgStatus = st;
    if (st == CM_MB_OK) {
        if (!cmRirqHandoff(nhi, TRUE, TRUE, &found)) {
            r->MsgHandoffFailures++;
            CmLogNote(nhi, "cm.irq.msg.handoff", 0x80000000UL | found);
            r->Why = CM_Q_IRQ_WHY_HANDOFF;
            CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
            cmRirqMsgSilent(nhi);
            return FALSE;
        }
        if (found != 0) {
            CmLogNote(nhi, "cm.irq.msg.handoff", found);
        }
        r->MsgConnects++;
        r->Connects++;
        /* Section 4.5.6: resident until the restart once an interrupt was
         * connected; the refusal table lives as long. */
        CmDriverObject->DriverUnload = NULL;
        /* The absence proof's condition (section 4.5.14 item 4): Interrupt
         * Disable as the library's arm left it. */
        data = CmCfgRead32(nhi, CM_PCI_COMMAND);
        r->MsgIdSet = (data != 0xFFFFFFFFUL &&
                       (data & CM_PCI_COMMAND_INTX_DISABLE) != 0)
                          ? 1UL
                          : 0UL;
        r->MsgVector = CmRirqNtVector(nhi, &data);
        CmLogNote(nhi, "cm.irq.msg.connected",
                  (r->MsgVector & 0xFFFFUL) | ((data & 0xFFUL) << 16));
        return TRUE;
    }
    r->MsgConnectFailures++;
    r->Why = CmRirqWhyOfStatus(st);
    CmLogNote(nhi, "cm.irq.msg.connect.fail", st);
    if (r->MsgInfo != NULL) {
        /* CONFIG_ACCESS with a live block: ended as a fall back ends it
         * (the rings keep running polled). */
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        r->Failed = 1;
        cmRirqMsgSilent(nhi);
        return FALSE;
    }
    r->Connected = 0;
    r->Message = 0;
    r->Object = NULL;
    cmRirqMsgKept(nhi, CmRirqNtRelease(nhi));
    if (st == CM_MB_IN_USE || st == CM_MB_CONFIG_ACCESS) {
        /* No fallback in the contract's sense: polled, the masks proven. */
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        cmRirqMsgSilent(nhi);
        return FALSE;
    }
    cmRirqMsgNone(nhi, TRUE);
    return FALSE;
}

/* A connection kept across a sleep, re-armed before the arm: the re-arm,
 * once more after 10 ms on a status neither OK nor BRAKED (as mode 4's,
 * record 15 section 4.5.17), then MSI-X Enable proven clear. Either failing
 * ends it as at a stop, latches the activation and proves the masks.
 * PASSIVE_LEVEL, the thread, every cause masked. */
static BOOLEAN cmRirqMsgReArm(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;
    ULONG st;
    ULONG st2;
    ULONG found;

    r = &nhi->Rirq;
    st = CmRirqNtReArm(nhi);
    st2 = st;
    if (st != CM_MB_OK && st != CM_MB_BRAKED) {
        nhi->Host.Sleep(nhi->Host.Context, 10);
        st2 = CmRirqNtReArm(nhi);
    }
    r->MsgRearms++;
    r->MsgStatus = st2;
    found = 0;
    CmLogNote(nhi, "cm.irq.msg.rearm", st | (st2 << 8));
    if (st2 == CM_MB_OK && cmRirqHandoff(nhi, TRUE, TRUE, &found)) {
        r->ReArmOwed = 0;
        if (found != 0) {
            CmLogNote(nhi, "cm.irq.msg.handoff", found);
        }
        return TRUE;
    }
    r->MsgRearmFailures++;
    /* Section 4.5.17: the general cause, then the particular one. */
    r->Why = CM_Q_IRQ_WHY_REARM;
    if (st2 == CM_MB_OK) {
        r->MsgHandoffFailures++;
        CmLogNote(nhi, "cm.irq.msg.handoff", 0x80000000UL | found);
        r->Why = CM_Q_IRQ_WHY_HANDOFF;
    }
    cmRirqMsgBraked(nhi, st2);
    r->ReArmOwed = 0;
    CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
    r->Failed = 1;
    /* The disconnect clears MSI Enable alone: the masks proven. */
    cmRirqMsgSilent(nhi);
    return FALSE;
}

/*
 * The message's end of the order down, after its steps 1 and 2 (section
 * 4.5.12, "The lifetime"): kept across a sleep; otherwise the library's
 * disconnect, once more after 10 ms, then the absence proof at a removal
 * or the quarantine (and the refusal table but at a shutdown), or kept
 * masked where the function is in D0 and the mask was proven.
 * PASSIVE_LEVEL, the thread.
 */
static VOID cmRirqMsgDown(PCM_NHI nhi, ULONG why, BOOLEAN proven)
{
    CM_RIRQ_DRV *r;
    BOOLEAN shutdown;
    ULONG st;
    ULONG st2;
    ULONG end;
    ULONG form;
    BOOLEAN absent;
    ULONG asked;

    r = &nhi->Rirq;
    shutdown = (BOOLEAN)(nhi->SystemPower == PowerSystemShutdown);
    r->IrqMode = CM_RIRQ_MODE_POLL;
    asked = why;
    if (why == CM_RIRQ_DOWN_D3 && !shutdown && !proven) {
        /* Section 4.5.16: a sleep keeps a connection only with its causes
         * proven masked; otherwise it ends as a fall back does, MSI Enable
         * cleared by the disconnect being the proof. */
        why = CM_RIRQ_DOWN_FALLBACK;
    }
    if (CmRirqMsgEnd(why, shutdown, FALSE) == CM_RIRQ_MSG_KEEP) {
        r->ReArmOwed = 1;
        CmLogNote(nhi, "cm.irq.down", (why << 8) | (CM_RIRQ_MSG_KEEP << 4) |
                                          (proven ? 1UL : 0UL));
        return;
    }
    form = 0;
    st = CmRirqNtDisconnect(nhi, 0);
    st2 = st;
    if (st == CM_MB_CONFIG_ACCESS) {
        nhi->Host.Sleep(nhi->Host.Context, 10);
        st2 = CmRirqNtDisconnect(nhi, 0);
        form = 1;
    }
    absent = FALSE;
    if (st2 != CM_MB_OK && r->Removal && r->MsgIdSet &&
        (why == CM_RIRQ_DOWN_STOP || why == CM_RIRQ_DOWN_D3)) {
        /* Section 4.5.14 item 4: the absence proof at a removal Windows
         * reported, the route admitting its reads at offset 0 alone. */
        r->ProofRead = 1;
        st2 = CmRirqNtDisconnect(nhi, 1);
        r->ProofRead = 0;
        absent = (BOOLEAN)(st2 == CM_MB_OK);
        form = 4;
    }
    end = CmRirqMsgEnd(why, shutdown, (BOOLEAN)(st2 == CM_MB_OK));
    /* A quarantine needs Bus Master off first (section 4.5.14 item 4a). */
    if (end == CM_RIRQ_MSG_FORCE) {
        cmRirqMsgBusMasterOff(nhi);
        (VOID)CmRirqNtDisconnect(nhi, 2);
        r->MsgForced++;
        form = 2;
        if (CmRirqMsgRefuse(end, shutdown) && r->MsgBdf != CM_RIRQ_NO_BDF) {
            CmLogNote(nhi, CmRirqRefuse(nhi, r->MsgBdf)
                               ? "cm.irq.msg.quarantine"
                               : "cm.irq.msg.table-full",
                      r->MsgBdf);
        }
    }
    if (end != CM_RIRQ_MSG_HOLD) {
        /* The claim given back (a quarantined one is the library's). */
        cmRirqMsgKept(nhi, CmRirqNtRelease(nhi));
    }
    UNREFERENCED_PARAMETER(absent);
    r->MsgStatus = st2;
    if (end == CM_RIRQ_MSG_HOLD) {
        form = 3;
        r->Retained = 1;
        r->Unretired++;
        r->RetryDue = cmRirqNow(nhi) +
                      CmNvlinkIdleMs(nhi->Values.Value[CM_VAL_POLL_IDLE_MS]);
        CmLogNote(nhi, "cm.irq.unretired", why);
    } else {
        r->Connected = 0;
        r->Message = 0;
        r->Object = NULL;
        r->Retained = 0;
        r->ReArmOwed = 0;
        r->Reach = 0;
        r->Disconnects++;
    }
    CmLogNote(nhi, "cm.irq.msg.disconnect", form | (st << 8) | (st2 << 16));
    CmLogNote(nhi, "cm.irq.down", (why << 8) | (end << 4) |
                                      (proven ? 1UL : 0UL));
    if (CmRirqMsgFails(why, end)) {
        r->Failed = 1;
        if (asked != CM_RIRQ_DOWN_FALLBACK) {
            /* Section 4.5.17: a D3 whose mask was not proven, or an
             * order down whose disconnect held or was forced. */
            r->Why = CM_Q_IRQ_WHY_TEARDOWN;
        }
    }
}

/*
 * The self-test (section 4.5.5): the cause's status set through Interrupt
 * Status Set, then up to CM_RIRQ_SELFTEST_MS on the clock for a claim and
 * the DPC it queues. TRUE when both came. PASSIVE_LEVEL, the thread.
 */
static BOOLEAN cmRirqSelfTest(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;
    LONG claims;
    LONG seq;
    ULONG start;
    ULONG n;

    r = &nhi->Rirq;
    r->SelfTests++;
    /* Ring 0's own claims (record 35 section 5.5): a data ring's claim
     * proves nothing of ring 0's cause. */
    claims = r->Claims0;
    seq = r->DpcSeq;
    if (!cmRirqMayWrite(nhi)) {
        return FALSE;
    }
    nhi->Host.Write32(nhi->Host.Context, TBT_HI_ISS + 4UL * r->Geom.CauseDw,
                      r->Geom.CauseBit);
    start = cmRirqNow(nhi);
    for (n = 0; n <= CM_RIRQ_SELFTEST_MS; n++) {
        if (r->Claims0 != claims && r->DpcSeq != seq) {
            CmLogNote(nhi, "cm.irq.selftest", cmRirqNow(nhi) - start);
            return TRUE;
        }
        if (cmRirqNow(nhi) - start > CM_RIRQ_SELFTEST_MS) {
            break;
        }
        cmRirqSleep1(nhi);
    }
    r->SelfTestFailures++;
    CmLogNote(nhi, "cm.irq.selftest.fail", (ULONG)(r->Claims0 - claims));
    return FALSE;
}

/*
 * The order up (section 4.5.6): connected (or the retained one reused),
 * PCI Interrupt Disable restored where this driver set it, the arm, IE in
 * every RX descriptor posted from now, a drain, the maintenance due at
 * once, and at an activation the self-test. Any failure falls back to
 * polling for the activation. PASSIVE_LEVEL, the thread, the client
 * polling.
 */
static VOID cmRirqUp(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;
    CM_RIRQ_SYNC s;
    NTSTATUS status;
    ULONG v;
    BOOLEAN message;

    r = &nhi->Rirq;
    /* Section 4.5.14: XhciCmRingInterrupt 2 on 2000, XP and XP x64. */
    message = (BOOLEAN)(cmRirqHowNow(nhi) == CM_RIRQ_HOW_MESSAGE);
    /* Everything the service routine reads is computed here and published
     * either before the connection or, for a retained interrupt that is
     * live meanwhile, inside the arm's synchronize routine (record 05
     * section 4: the routine must never see a half-written geometry). */
    if (!CmRirqGeometry(nhi->Ring.Paths, &s.Geom)) {
        CmLogNote(nhi, "cm.irq.none", 0x100UL | nhi->Ring.Paths);
        r->Why = CM_Q_IRQ_WHY_RINGS;
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return;
    }
    if (r->Geom.DataBit != 0 && r->Geom.Paths == s.Geom.Paths) {
        /* A data cause whose mask was never proven (record 35 section
         * 5.5) stays recognised through the reconnect and the arm, which
         * proves it masked before anything is unmasked. */
        s.Geom.DataRing = r->Geom.DataRing;
        s.Geom.DataDw = r->Geom.DataDw;
        s.Geom.DataBit = r->Geom.DataBit;
    }
    if (!message && !r->CommandRead) {
        /* Section 4.5.6.1: bit 10 found set is never cleared here, and
         * INTx cannot be delivered under it: polling. A message is not
         * suppressed by it (section 4.5.12). */
        v = CmCfgRead32(nhi, CM_PCI_COMMAND);
        if (v == 0xFFFFFFFFUL ||
            (v & CM_PCI_COMMAND_INTX_DISABLE) != 0) {
            CmLogNote(nhi, "cm.irq.intx.found", v);
            r->Why = CM_Q_IRQ_WHY_INTX;
            CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
            return;
        }
        r->CommandRead = 1;
    }
    s.Intel = (nhi->Hw != NULL &&
               (nhi->Hw->Flags & TBT_HW_INTEL_INTERRUPTS) != 0)
                  ? 1UL
                  : 0UL;
    v = nhi->Host.Read32(nhi->Host.Context, TBT_HI_CONTROL);
    s.AutoClearOff = (v != 0xFFFFFFFFUL &&
                      (v & TBT_HI_CONTROL_NO_AUTOCLEAR) != 0)
                         ? 1UL
                         : 0UL;
    CmLogNote(nhi, "cm.irq.hi-control", v);
    if (!r->Connected && message) {
        /* Nothing is connected: no routine can read these yet (and the
         * message's reads nothing until Reach is set by the arm). */
        r->Geom = s.Geom;
        r->Intel = s.Intel;
        r->AutoClearOff = s.AutoClearOff;
        if (!cmRirqMsgConnect(nhi)) {
            return;
        }
    } else if (r->Connected && r->Message && r->ReArmOwed) {
        if (!cmRirqMsgReArm(nhi)) {
            return;
        }
    } else if (!r->Connected) {
        /* Nothing is connected: no routine can read these yet. */
        r->Geom = s.Geom;
        r->Intel = s.Intel;
        r->AutoClearOff = s.AutoClearOff;
        r->Connected = 1;
        status = IoConnectInterrupt(&r->Object, cmRirqIsr, nhi, NULL,
                                    r->Vector, r->Irql, r->Irql, r->Mode,
                                    r->Shared, r->Affinity, FALSE);
        if (!NT_SUCCESS(status)) {
            r->Connected = 0;
            r->Object = NULL;
            r->ConnectFailures++;
            CmLogNote(nhi, "cm.irq.connect.fail", (ULONG)status);
            r->Why = CM_Q_IRQ_WHY_CONNECT;
            CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
            return;
        }
        r->Connects++;
        /* Section 4.5.6: the image outlives every DPC - resident until the
         * restart once an interrupt was connected. */
        CmDriverObject->DriverUnload = NULL;
        CmLogNote(nhi, "cm.irq.connected", r->Vector);
    }
    r->Retained = 0;
    if (!r->Message) {
        cmRirqIntxRestore(nhi);
    }
    s.Nhi = nhi;
    s.Write = TRUE;
    s.Open = TRUE;
    s.Proven = FALSE;
    s.Imr = 0;
    s.Ring = 0;
    s.How = 0;
    if (!cmRirqMayWrite(nhi) || !cmRirqSync(nhi, cmRirqSyncArm, &s)) {
        r->ArmFailures++;
        CmLogNote(nhi, "cm.irq.arm.fail", s.Imr);
        r->Why = CM_Q_IRQ_WHY_ARM;
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return;
    }
    nhi->Ring.RxIe = TRUE;
    r->IrqMode = r->Message ? CM_RIRQ_MODE_MESSAGE : CM_RIRQ_MODE_LINE;
    r->Why = CM_Q_IRQ_WHY_NONE;
    r->MaintOwed = 1;
    CmRirqWatchStart(&r->Watch, (ULONG)r->Claims0, nhi->Ring.Counters.Rx);
    CmLogNote(nhi, "cm.irq.armed", s.Imr);
    /* A frame that landed between the stale status read and the unmask is
     * not left for the next one. */
    (VOID)CmThrQueue(nhi, CM_WORK_RING, NULL);
    if (r->SelfTestOwed) {
        if (!cmRirqSelfTest(nhi)) {
            r->Why = CM_Q_IRQ_WHY_SELFTEST;
            CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
            return;
        }
        r->SelfTestOwed = 0;
        CmRirqWatchStart(&r->Watch, (ULONG)r->Claims0, nhi->Ring.Counters.Rx);
    }
    /* Ring 0 proven: the data ring's cause where it is asked for (record
     * 35 section 5.5). */
    (VOID)cmRirqDataTry(nhi);
}

VOID CmRirqBeforeMaint(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;
    ULONG act;
    ULONG dataRing;
    BOOLEAN dataWas;

    r = &nhi->Rirq;
    if (!r->Armed) {
        return;
    }
    act = CmRirqWatchPass(&r->Watch, (ULONG)r->Claims0, nhi->Ring.Counters.Rx,
                          TbtRingRxPending(&nhi->Ring));
    /* Per cause (record 35 section 5.5): the data ring's claims against
     * the frames its service took, whatever ring 0 did. */
    if (CmRirqWatchData(&r->Watch, (ULONG)r->ClaimsData, nhi->Net.RxTaken)) {
        r->Storms++;
        r->DataStorms++;
        CmLogNote(nhi, "cm.irq.storm", 0x80000000UL | (ULONG)r->ClaimsData);
        r->Why = CM_Q_IRQ_WHY_STORM;
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return;
    }
    if (act == CM_RIRQ_WATCH_STORM) {
        r->Storms++;
        CmLogNote(nhi, "cm.irq.storm", (ULONG)r->Claims0);
        r->Why = CM_Q_IRQ_WHY_STORM;
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return;
    }
    if (act == CM_RIRQ_WATCH_SELFTEST) {
        r->Suspects++;
        CmLogNote(nhi, "cm.irq.suspect", (ULONG)r->Claims);
        /* Whether the data cause was open before the test's shut, which
         * clears DataOpen: a lost interrupt latches it too (record 35
         * section 5.5), and CmRirqDown's note no longer sees it. */
        dataWas = (BOOLEAN)(r->DataOpen != 0);
        dataRing = r->Geom.DataRing;
        /* The data cause masked for the test, so only ring 0's own
         * interrupt can pass it, then opened again (record 35 5.5). */
        if (r->DataOpen) {
            CmRirqWatchDataStart(&r->Watch, FALSE, 0, 0);
            if (!cmRirqDataShut(nhi, FALSE)) {
                CmLogNote(nhi, "cm.net.irq.unmasked", r->Geom.DataRing);
                r->Why = CM_Q_IRQ_WHY_ARM;
                CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
                return;
            }
        }
        if (!cmRirqSelfTest(nhi)) {
            r->Why = CM_Q_IRQ_WHY_LOST;
            r->Lost++;
            CmLogNote(nhi, "cm.irq.lost", r->Lost);
            if (dataWas) {
                CmLogNote(nhi, "cm.net.irq.latched", dataRing);
            }
            CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
            return;
        }
        CmRirqWatchStart(&r->Watch, (ULONG)r->Claims0, nhi->Ring.Counters.Rx);
        (VOID)cmRirqDataTry(nhi);
    }
}

VOID CmRirqAfterMaint(PCM_NHI nhi, ULONG periodMs)
{
    CM_RIRQ_DRV *r;

    r = &nhi->Rirq;
    if (!r->Armed) {
        return;
    }
    UNREFERENCED_PARAMETER(periodMs);
    CmRirqWatchDrained(&r->Watch, (ULONG)r->Claims0);
    r->MaintLast = cmRirqNow(nhi);
    r->MaintOwed = 0;
}

VOID CmRirqWake(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;

    r = &nhi->Rirq;
    r->Wakes++;
    if (nhi->U4On) {
        TbtU4RingWake(&nhi->U4);
    } else {
        TbtIcmRingWake(&nhi->Icm);
    }
    CmNhiDrainActions(nhi);
    if (nhi->U4On && nhi->U4.Failed && nhi->Stage == CM_STAGE_ACTIVE) {
        /* A failure after activation inside the wake's port check, as
         * CmNhiPollPass marks one inside the tick (record 22 section
         * 3.5). */
        nhi->Stage = CM_STAGE_FAILED;
        CmLogNote(nhi, "u4.failed", nhi->U4.InertReason);
    }
    if (r->Armed) {
        CmRirqWatchDrained(&r->Watch, (ULONG)r->Claims0);
    }
}

VOID CmRirqService(PCM_NHI nhi, BOOLEAN polling, ULONG periodMs)
{
    CM_RIRQ_DRV *r;
    ULONG first;

    r = &nhi->Rirq;
    /* Section 4.5.18: the gate opened again once the function is in D0
     * with BAR0 mapped in a working system. */
    CmRirqHwGateOpen(nhi);
    if (r->MsgMaskWatch && !r->Connected && polling) {
        /* Section 4.5.12, "When the library says no": the masks read back
         * once a pass, logged when the reading changes. */
        if (cmRirqMasksZero(nhi, &first)) {
            r->MsgMaskWatch = 0;
            CmLogNote(nhi, "cm.irq.msg.masked", 3);
        } else if ((first | 1UL) != r->MsgMaskWatch) {
            r->MsgMaskWatch = first | 1UL;
            CmLogNote(nhi, "cm.irq.msg.unmasked", first);
        }
    }
    if (r->Connected && r->StormMasked) {
        r->Storms++;
        CmLogNote(nhi, "cm.irq.storm", (ULONG)r->Claims);
        r->StormMasked = 0;
        r->Why = CM_Q_IRQ_WHY_STORM;
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return;
    }
    if (r->Connected && r->Armed) {
        if (!polling) {
            CmRirqDown(nhi, CM_RIRQ_DOWN_CONTAIN);
        }
        return;
    }
    if (r->Connected && r->Retained) {
        if (polling && cmRirqWanted(nhi)) {
            cmRirqUp(nhi);
        } else if (CmRirqDue(cmRirqNow(nhi), r->RetryDue)) {
            /* A later down order's attempt, once a period. */
            r->RetryDue = cmRirqNow(nhi) + periodMs;
            CmRirqDown(nhi, r->Failed ? CM_RIRQ_DOWN_FALLBACK
                                      : CM_RIRQ_DOWN_CONTAIN);
        }
        return;
    }
    if (!polling) {
        return;
    }
    if (!cmRirqWanted(nhi)) {
        if (r->Value != CM_RIRQ_VALUE_OFF && !r->Failed &&
            r->How == CM_RIRQ_HOW_NONE && !r->HaveLine) {
            r->Failed = 1;
            CmLogNote(nhi, "cm.irq.none", r->HaveMessage);
        }
        return;
    }
    cmRirqUp(nhi);
}

/* CM_Q_RM_IRQ's word (section 4.5.17), at each query build on the
 * thread; each field one aligned word. */
ULONG CmRirqQueryWord(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;
    BOOLEAN nt;

    r = &nhi->Rirq;
    nt = (BOOLEAN)(r->Sys == CM_RIRQ_SYS_NT5);
    return CmRirqShowWord(r->Value, (BOOLEAN)(nhi->Values.RingIrqRefused != 0),
                          r->Sys, (BOOLEAN)(r->Yield != 0),
                          (BOOLEAN)(r->HaveLine != 0), r->IrqMode, r->Why,
                          nt ? r->MsgIrq : r->MsgVector, nt);
}

VOID CmRirqLogCounters(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;

    r = &nhi->Rirq;
    if (r->Value == CM_RIRQ_VALUE_OFF && r->Connects == 0) {
        return;
    }
    CmLogNote(nhi, "cm.irq.mode", r->IrqMode);
    CmLogNote(nhi, "cm.irq.failed", r->Failed);
    CmLogNote(nhi, "cm.irq.claims", (ULONG)r->Claims);
    CmLogNote(nhi, "cm.irq.ones", (ULONG)r->Ones);
    CmLogNote(nhi, "cm.irq.dpcs", (ULONG)r->DpcSeq);
    CmLogNote(nhi, "cm.irq.wakes", r->Wakes);
    CmLogNote(nhi, "cm.irq.connects", r->Connects);
    CmLogNote(nhi, "cm.irq.connect-failures", r->ConnectFailures);
    CmLogNote(nhi, "cm.irq.arm-failures", r->ArmFailures);
    CmLogNote(nhi, "cm.irq.selftests", r->SelfTests);
    CmLogNote(nhi, "cm.irq.selftest-failures", r->SelfTestFailures);
    CmLogNote(nhi, "cm.irq.suspects", r->Suspects);
    CmLogNote(nhi, "cm.irq.lost-count", r->Lost);
    CmLogNote(nhi, "cm.irq.storms", r->Storms);
    CmLogNote(nhi, "cm.irq.downs", r->Downs);
    CmLogNote(nhi, "cm.irq.unretired-count", r->Unretired);
    CmLogNote(nhi, "cm.irq.disconnects", r->Disconnects);
    CmLogNote(nhi, "cm.irq.dpc-slow", r->DpcSlow);
    if (r->QemuDeadAfter != 0) {
        CmLogNote(nhi, "cm.irq.qemu.dropped", (ULONG)r->QemuDropped);
    }
    CmLogNote(nhi, "cm.irq.claims.ring0", (ULONG)r->Claims0);
    CmLogNote(nhi, "cm.net.irq.claims", (ULONG)r->ClaimsData);
    CmLogNote(nhi, "cm.net.irq.opens", r->DataOpens);
    CmLogNote(nhi, "cm.net.irq.unproven.n", r->DataUnproven);
    CmLogNote(nhi, "cm.net.irq.storms", r->DataStorms);
    if (r->Value == CM_RIRQ_VALUE_MESSAGE) {
        /* Section 4.5.12. */
        CmLogNote(nhi, "cm.irq.msg.connects", r->MsgConnects);
        CmLogNote(nhi, "cm.irq.msg.connect-failures", r->MsgConnectFailures);
        CmLogNote(nhi, "cm.irq.msg.rearms", r->MsgRearms);
        CmLogNote(nhi, "cm.irq.msg.rearm-failures", r->MsgRearmFailures);
        CmLogNote(nhi, "cm.irq.msg.forced", r->MsgForced);
        CmLogNote(nhi, "cm.irq.msg.handoff-failures", r->MsgHandoffFailures);
        CmLogNote(nhi, "cm.irq.msg.status", r->MsgStatus);
        CmLogNote(nhi, "cm.irq.msg.vector", r->MsgVector);
        CmLogNote(nhi, "cm.irq.msg.irq", r->MsgIrq);
        CmLogNote(nhi, "cm.irq.msg.braked-count", r->MsgBraked);
    }
}

/* ---------------------------------------------------------------------- */
/* The networking data ring's cause (record 35 section 5.5, task 48.38)    */
/* ---------------------------------------------------------------------- */

/* After an opening, by the thread's call or an arm: counted and logged,
 * and the data cause watched from now where open. */
static VOID cmRirqDataOpened(PCM_NHI nhi, ULONG how)
{
    CM_RIRQ_DRV *r;

    r = &nhi->Rirq;
    if (how == CM_RIRQ_DATA_OPEN) {
        r->DataOpens++;
        CmRirqWatchDataStart(&r->Watch, TRUE, (ULONG)r->ClaimsData,
                             nhi->Net.RxTaken);
        CmLogNote(nhi, "cm.net.irq.open", r->Geom.DataRing);
    } else {
        r->DataUnproven++;
        CmLogNote(nhi, "cm.net.irq.unproven", (how << 8) | r->DataWant);
    }
}

/* The data cause opened where it is asked for and the interrupt is armed
 * and proven (no self-test owed): CM_RIRQ_DATA_*. An opening left neither
 * open nor proven masked takes the whole interrupt down to polling, where
 * record 21 section 4.5.6 retires the line. PASSIVE_LEVEL, the thread. */
static ULONG cmRirqDataTry(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;
    CM_RIRQ_SYNC s;

    r = &nhi->Rirq;
    if (r->DataWant == 0 || !r->Connected || !r->Armed || r->Retained ||
        r->SelfTestOwed || !cmRirqMayWrite(nhi)) {
        return CM_RIRQ_DATA_POLL;
    }
    if (r->DataOpen) {
        return CM_RIRQ_DATA_OPEN;
    }
    s.Nhi = nhi;
    s.Write = TRUE;
    s.Open = TRUE;
    s.Proven = FALSE;
    s.Imr = 0;
    s.Ring = r->DataWant;
    s.How = CM_RIRQ_DATA_POLL;
    (VOID)cmRirqSync(nhi, cmRirqSyncDataOpen, &s);
    if (s.How == CM_RIRQ_DATA_POLL) {
        return CM_RIRQ_DATA_POLL;
    }
    cmRirqDataOpened(nhi, s.How);
    if (s.How == CM_RIRQ_DATA_LIVE) {
        CmLogNote(nhi, "cm.net.irq.unmasked", s.Ring);
        r->Why = CM_Q_IRQ_WHY_ARM;
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return CM_RIRQ_DATA_UNPROVEN;
    }
    return s.How;
}

/* The data cause masked, ITR[0] 0, the ask kept: TRUE when proven masked
 * or never open. PASSIVE_LEVEL, the thread. */
static BOOLEAN cmRirqDataShut(PCM_NHI nhi, BOOLEAN lost)
{
    CM_RIRQ_DRV *r;
    CM_RIRQ_SYNC s;

    r = &nhi->Rirq;
    if (!r->Connected) {
        r->DataOpen = 0;
        return TRUE;
    }
    s.Nhi = nhi;
    s.Write = (BOOLEAN)(!lost && cmRirqMayWrite(nhi));
    s.Open = FALSE;
    s.Proven = FALSE;
    s.Imr = 0;
    s.Ring = 0;
    s.How = 0;
    return cmRirqSync(nhi, cmRirqSyncDataClose, &s);
}

ULONG CmRirqDataOpen(PCM_NHI nhi, ULONG ring, BOOLEAN lost)
{
    nhi->Rirq.DataWant = ring;
    if (lost) {
        return CM_RIRQ_DATA_POLL;
    }
    return cmRirqDataTry(nhi);
}

BOOLEAN CmRirqDataClose(PCM_NHI nhi, BOOLEAN lost)
{
    CM_RIRQ_DRV *r;

    r = &nhi->Rirq;
    r->DataWant = 0;
    CmRirqWatchDataStart(&r->Watch, FALSE, 0, 0);
    if (!cmRirqDataShut(nhi, lost)) {
        CmLogNote(nhi, "cm.net.irq.unmasked", r->Geom.DataRing);
        /* Section 4.5.17: the caller's fall back (cm_net.c) says no more. */
        r->Why = CM_Q_IRQ_WHY_ARM;
        return FALSE;
    }
    return TRUE;
}

ULONG CmRirqDataState(PCM_NHI nhi)
{
    CM_RIRQ_DRV *r;

    r = &nhi->Rirq;
    if (r->DataOpen) {
        return 1;
    }
    if (r->DataWant != 0 && r->Value != CM_RIRQ_VALUE_OFF && r->Failed) {
        return 2;
    }
    return 0;
}
