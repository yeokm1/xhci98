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
 * The service routine (section 4.5.3). IRQL: DIRQL, the interrupt object's
 * lock held; the line may be shared.
 */
static BOOLEAN NTAPI cmRirqIsr(PKINTERRUPT Interrupt, PVOID Context)
{
    PCM_NHI nhi;
    CM_RIRQ_DRV *r;
    ULONG v[CM_RIRQ_MAX_DWORDS];
    ULONG causes;

    UNREFERENCED_PARAMETER(Interrupt);

    nhi = (PCM_NHI)Context;
    r = &nhi->Rirq;
    if (!r->Connected || nhi->BarVa == NULL || r->Geom.Dwords == 0) {
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
    nhi->Rirq.Armed = 1;
    s->Proven = cmRirqMaskNow(nhi, TRUE, &s->Imr);
    if (!s->Proven) {
        nhi->Rirq.Armed = 0;
        (VOID)cmRirqMaskNow(nhi, FALSE, &imr);
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
    KeInitializeDpc(&nhi->Rirq.Dpc, cmRirqDpc, nhi);
}

VOID CmRirqStart(PCM_NHI nhi, PCM_PARTIAL_RESOURCE_LIST translated)
{
    CM_RIRQ_DRV *r;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR d;
    ULONG i;

    r = &nhi->Rirq;
    r->HaveLine = 0;
    r->HaveMessage = 0;
    r->Value = nhi->Values.RingIrq;
    r->Failed = 0;
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
}

static BOOLEAN cmRirqWanted(PCM_NHI nhi)
{
    return CmRirqWanted(nhi->Rirq.Value,
                        (BOOLEAN)(nhi->Rirq.HaveLine != 0),
                        (BOOLEAN)(nhi->Rirq.Failed != 0));
}

VOID CmRirqIntent(PCM_NHI nhi)
{
    if (nhi->Rirq.Connected && !nhi->Rirq.Retained) {
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
VOID CmRirqDown(PCM_NHI nhi, ULONG why)
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
    (VOID)KeSynchronizeExecution(r->Object, cmRirqSyncDisarm, &s);
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
        (VOID)KeSynchronizeExecution(r->Object, cmRirqSyncDisarm, &s);
        proven = s.Proven;
        CmLogNote(nhi, "cm.irq.emergency-mask", proven ? 1UL : 0UL);
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

    r = &nhi->Rirq;
    /* Everything the service routine reads is computed here and published
     * either before the connection or, for a retained interrupt that is
     * live meanwhile, inside the arm's synchronize routine (record 05
     * section 4: the routine must never see a half-written geometry). */
    if (!CmRirqGeometry(nhi->Ring.Paths, &s.Geom)) {
        CmLogNote(nhi, "cm.irq.none", 0x100UL | nhi->Ring.Paths);
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
    if (!r->CommandRead) {
        /* Section 4.5.6.1: bit 10 found set is never cleared here, and
         * INTx cannot be delivered under it: polling. */
        v = CmCfgRead32(nhi, CM_PCI_COMMAND);
        if (v == 0xFFFFFFFFUL ||
            (v & CM_PCI_COMMAND_INTX_DISABLE) != 0) {
            CmLogNote(nhi, "cm.irq.intx.found", v);
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
    if (!r->Connected) {
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
    cmRirqIntxRestore(nhi);
    s.Nhi = nhi;
    s.Write = TRUE;
    s.Open = TRUE;
    s.Proven = FALSE;
    s.Imr = 0;
    s.Ring = 0;
    s.How = 0;
    if (!cmRirqMayWrite(nhi) ||
        !KeSynchronizeExecution(r->Object, cmRirqSyncArm, &s)) {
        r->ArmFailures++;
        CmLogNote(nhi, "cm.irq.arm.fail", s.Imr);
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return;
    }
    nhi->Ring.RxIe = TRUE;
    r->IrqMode = CM_RIRQ_MODE_LINE;
    r->MaintOwed = 1;
    CmRirqWatchStart(&r->Watch, (ULONG)r->Claims0, nhi->Ring.Counters.Rx);
    CmLogNote(nhi, "cm.irq.armed", s.Imr);
    /* A frame that landed between the stale status read and the unmask is
     * not left for the next one. */
    (VOID)CmThrQueue(nhi, CM_WORK_RING, NULL);
    if (r->SelfTestOwed) {
        if (!cmRirqSelfTest(nhi)) {
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
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return;
    }
    if (act == CM_RIRQ_WATCH_STORM) {
        r->Storms++;
        CmLogNote(nhi, "cm.irq.storm", (ULONG)r->Claims0);
        CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
        return;
    }
    if (act == CM_RIRQ_WATCH_SELFTEST) {
        r->Suspects++;
        CmLogNote(nhi, "cm.irq.suspect", (ULONG)r->Claims);
        /* The data cause masked for the test, so only ring 0's own
         * interrupt can pass it, then opened again (record 35 5.5). */
        if (r->DataOpen) {
            CmRirqWatchDataStart(&r->Watch, FALSE, 0, 0);
            if (!cmRirqDataShut(nhi, FALSE)) {
                CmLogNote(nhi, "cm.net.irq.unmasked", r->Geom.DataRing);
                CmRirqDown(nhi, CM_RIRQ_DOWN_FALLBACK);
                return;
            }
        }
        if (!cmRirqSelfTest(nhi)) {
            r->Lost++;
            CmLogNote(nhi, "cm.irq.lost", r->Lost);
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

    r = &nhi->Rirq;
    if (r->Connected && r->StormMasked) {
        r->Storms++;
        CmLogNote(nhi, "cm.irq.storm", (ULONG)r->Claims);
        r->StormMasked = 0;
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
        if (r->Value == CM_RIRQ_VALUE_LINE && !r->Failed && !r->HaveLine) {
            r->Failed = 1;
            CmLogNote(nhi, "cm.irq.none", r->HaveMessage);
        }
        return;
    }
    cmRirqUp(nhi);
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
    (VOID)KeSynchronizeExecution(r->Object, cmRirqSyncDataOpen, &s);
    if (s.How == CM_RIRQ_DATA_POLL) {
        return CM_RIRQ_DATA_POLL;
    }
    cmRirqDataOpened(nhi, s.How);
    if (s.How == CM_RIRQ_DATA_LIVE) {
        CmLogNote(nhi, "cm.net.irq.unmasked", s.Ring);
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
    return KeSynchronizeExecution(r->Object, cmRirqSyncDataClose, &s);
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
    if (r->DataWant != 0 && r->Value == CM_RIRQ_VALUE_LINE && r->Failed) {
        return 2;
    }
    return 0;
}
