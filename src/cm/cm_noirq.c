/*
 * cm_noirq.c - a Windows 98 SE or ME function with no interrupt, started:
 * the pure half (cm_noirq.h; design record 28, task 42-A.15). Written for
 * this project.
 *
 * THE STEPS of one entry, as cm_noirq.h's operations, in the order the
 * record gives them:
 *
 *   admission (PENDING; read again from the first at every pass of the
 *   window): HEADER (rule 3; a pin-0 NVMe function counted), STATUS
 *   (rule 2), BINDING and the installer's record (rule 4), the numbering's
 *   record or PORT_INTR for a fresh classification (rule 5), LC_LIST (the
 *   existing configurations, "read first"), RD_READ until the list ends
 *   (the source's descriptors) - then ACTIVE with its deadline;
 *
 *   the transaction (ACTIVE): ADD_EMPTY at LCPRI_DISABLED (journalled at
 *   once), for each memory descriptor PLACE then ADD_RD, then STATUS and
 *   BINDING again, PLACE, SET_PRI at LCPRI_SUBOPTIMAL (committed), STATUS
 *   and BINDING again, REPROCESS - then ATTEMPTED; a reprocess-only
 *   attempt (the journal's committed configuration found as a valid
 *   match under a new generation) is STATUS, BINDING and REPROCESS;
 *
 *   the outcome (ATTEMPTED; once a pass): STATUS - started with no problem
 *   is DONE (LINE read for the log); the problem still there after
 *   CM_NI_READY_AFTER_MS is BINDING then READY once; the deadline is
 *   FAILED "timeout".
 *
 * Every submission of an attempt is preceded by the caller's gate
 * (CM_NI_CTX.Gate): closed, the attempt is FAILED "controller". A devnode
 * that answers no such devnode or another instance id at any read is
 * forgotten before any other judgement, its journal retired.
 *
 * IRQL: any; no lock, no DDK call.
 */

#include "cm_noirq.h"

/* Steps (CM_NI_DEV.Step). */
#define NI_AD_HEADER    1UL
#define NI_AD_STATUS    2UL
#define NI_AD_BIND      3UL
#define NI_AD_PORT      4UL
#define NI_AD_LCLIST    5UL
#define NI_AD_RD        6UL
#define NI_TX_ADD       10UL
#define NI_TX_PLACE     11UL
#define NI_TX_ADDRD     12UL
#define NI_TX_C_STATUS  13UL    /* before the commit                      */
#define NI_TX_C_BIND    14UL
#define NI_TX_C_PLACE   15UL
#define NI_TX_SETPRI    16UL
#define NI_TX_R_STATUS  17UL    /* before the reprocess                   */
#define NI_TX_R_BIND    18UL
#define NI_TX_REPROC    19UL
#define NI_AT_STATUS    20UL
#define NI_AT_BIND      21UL
#define NI_AT_READY     22UL
#define NI_AT_LINE      23UL
#define NI_AT_WAIT      24UL    /* read again at the next pass            */

#define NI_NONE         0xFFFFFFFFUL

/* Interval arithmetic on the caller's ms clock (it may wrap). */
static ULONG niSince(ULONG now, ULONG then)
{
    return now - then;
}

static ULONG niPast(ULONG now, ULONG deadline)
{
    return (ULONG)(niSince(now, deadline) < 0x80000000UL);
}

static VOID niZero(PVOID p, ULONG n)
{
    UCHAR *b;

    b = (UCHAR *)p;
    while (n-- != 0) {
        *b++ = 0;
    }
}

static ULONG niGet32(const UCHAR *b)
{
    return (ULONG)b[0] | ((ULONG)b[1] << 8) | ((ULONG)b[2] << 16) |
           ((ULONG)b[3] << 24);
}

static VOID niDiagBump(PCM_NI_MEMBER m)
{
    m->DiagSeq++;
    if (m->DiagSeq == 0) {
        m->DiagSeq = 1;
    }
}

/* ---- the journal ------------------------------------------------------ */

static CM_NI_JRN *niJrn(PCM_NI_MEMBER m, ULONG dn, ULONG hash, ULONG make)
{
    ULONG k;

    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        if (m->Jrn[k].Used && m->Jrn[k].Dn == dn && m->Jrn[k].Hash == hash) {
            return &m->Jrn[k];
        }
    }
    if (!make) {
        return NULL;
    }
    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        if (!m->Jrn[k].Used) {
            niZero(&m->Jrn[k], sizeof(CM_NI_JRN));
            m->Jrn[k].Used = 1;
            m->Jrn[k].Dn = dn;
            m->Jrn[k].Hash = hash;
            return &m->Jrn[k];
        }
    }
    return NULL;
}

/* A devnode gone or replaced: its journal retired, unfreed. */
static VOID niJrnRetire(PCM_NI_MEMBER m, ULONG dn, ULONG hash)
{
    CM_NI_JRN *j;

    j = niJrn(m, dn, hash, 0);
    if (j != NULL) {
        j->Used = 0;
    }
}

/* The journal's last committed configuration not gone, or NULL. */
static CM_NI_JENT *niCommitted(CM_NI_JRN *j)
{
    ULONG k;

    if (j == NULL) {
        return NULL;
    }
    for (k = j->Created; k > 0; k--) {
        if (j->E[k - 1].Committed && !j->E[k - 1].Gone) {
            return &j->E[k - 1];
        }
    }
    return NULL;
}

static ULONG niJournalled(CM_NI_JRN *j, ULONG lc)
{
    ULONG k;

    if (j == NULL) {
        return 0;
    }
    for (k = 0; k < j->Created; k++) {
        if (j->E[k].Lc == lc && !j->E[k].Gone) {
            return 1;
        }
    }
    return 0;
}

/* ---- entries ---------------------------------------------------------- */

static VOID niDiag(PCM_NI_MEMBER m, CM_NI_DEV *d, ULONG status,
                   ULONG problem)
{
    ULONG packed;
    ULONG k;
    ULONG n;

    m->Diag[1] = m->PortBdf;
    m->Diag[2] = d->Bdf;
    m->Diag[3] = (d->Pin & 0xFFUL) | ((d->Line & 0xFFUL) << 8);
    m->Diag[4] = (d->Irq & 0xFFUL) | ((d->State & 0xFFUL) << 8) |
                 ((d->Why & 0xFFUL) << 16);
    m->Diag[5] = d->ProbBefore;
    m->Diag[6] = ((d->LastStep & 0xFFFFUL) << 16) | (d->LastCr & 0xFFFFUL);
    m->Diag[7] = status;
    m->Diag[8] = problem;
    m->Diag[9] = (m->C.Attempts & 0xFFFFUL) |
                 ((m->C.Candidates & 0xFFFFUL) << 16);
    packed = 0;
    for (k = 1; k <= 8; k++) {
        n = m->C.Refused[k];
        if (n > 15) {
            n = 15;
        }
        packed |= n << ((k - 1) * 4);
    }
    m->Diag[10] = packed;
    m->Diag[11] = (m->C.Pin0Seen & 0xFFFFUL) |
                  ((m->C.Pin0Started & 0xFFFFUL) << 16);
    niDiagBump(m);
}

/* PENDING again, from the first read (a new pass of the window or a new
 * generation). */
static VOID niPending(CM_NI_DEV *d)
{
    d->State = CM_NI_S_PENDING;
    d->Why = CM_NI_W_NONE;
    d->Step = NI_AD_HEADER;
}

static VOID niExclude(PCM_NI_MEMBER m, CM_NI_DEV *d, ULONG why)
{
    d->State = CM_NI_S_EXCLUDED;
    d->Why = why;
    d->Step = 0;
    if (why < CM_NI_W_COUNT) {
        m->C.Refused[why]++;
    }
    /* A refusal is the last attempt too: the diagnostic says which entry
     * and why, and its packed counts move. */
    niDiag(m, d, 0, 0);
}

/* The admission window still open at `now` (a result that comes back
 * after it closed admits nothing). */
static ULONG niWindowLive(const CM_NI_MEMBER *m, ULONG now)
{
    return (ULONG)(m->WindowOpen &&
                   niSince(now, m->WindowFrom) < CM_NOIRQ_WINDOW_MS);
}

/* An attempt ended FAILED: an uncommitted configuration it added stays a
 * disabled leftover (counted), a committed one stays (section 4). */
static VOID niFail(PCM_NI_MEMBER m, CM_NI_DEV *d, ULONG why, ULONG cr,
                   ULONG status, ULONG problem)
{
    if (d->Alt != 0 && !d->AltDone) {
        m->C.Leftovers++;
    }
    if (why == CM_NI_W_TIMEOUT) {
        m->C.Timeouts++;
        if (d->State == CM_NI_S_ATTEMPTED) {
            m->C.Unstarted++;
        }
    }
    d->State = CM_NI_S_FAILED;
    d->Why = why;
    d->Step = 0;
    d->LastCr = cr;
    if (why < CM_NI_W_COUNT) {
        m->C.Refused[why]++;
    }
    niDiag(m, d, status, problem);
}

/* Gone, or answering another instance id: forgotten, journal retired. */
static VOID niForget(PCM_NI_MEMBER m, CM_NI_DEV *d)
{
    if (d->Alt != 0 && !d->AltDone) {
        m->C.Leftovers++;
    }
    niJrnRetire(m, d->Dn, d->Hash);
    niZero(d, sizeof(CM_NI_DEV));
}

VOID CmNiInit(PCM_NI_MEMBER m, ULONG portBdf)
{
    niZero(m, sizeof(CM_NI_MEMBER));
    m->PortBdf = portBdf;
    m->Cur = CM_NOIRQ_DEVICES;
    m->NextId = 1;
}

VOID CmNiOpenWindow(PCM_NI_MEMBER m, ULONG gen, ULONG now)
{
    ULONG k;
    CM_NI_DEV *d;

    m->WindowOpen = 1;
    m->WindowFrom = now;
    if (m->HaveGen && m->Gen == gen) {
        return;
    }
    m->Gen = gen;
    m->HaveGen = 1;
    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        d = &m->Dev[k];
        if (!d->Used || d->State == CM_NI_S_ACTIVE ||
            d->State == CM_NI_S_ATTEMPTED) {
            continue;
        }
        niPending(d);
        d->Gen = gen;
        d->Txn++;
    }
}

VOID CmNiIrqFromBrnum(const XHCI_TBT_BRNUM *r, PCM_NI_IRQ irq)
{
    ULONG k;

    niZero(irq, sizeof(CM_NI_IRQ));
    if (r->Outcome != XHCI_TBT_BRNUM_DONE &&
        r->Outcome != XHCI_TBT_BRNUM_REOPENED) {
        return;
    }
    if (r->IrqOutcome == XHCI_TBT_BRIRQ_PORT_NONE) {
        /* A port that read all ones (absent, or not answering) proves
         * nothing about its line: no record, a fresh reading instead. */
        if (r->IrqPin > 4 || (r->IrqLine == 0xFFUL && r->IrqPin == 0xFFUL)) {
            return;
        }
        irq->Valid = 1;
        irq->PortNoLine = 1;
        return;
    }
    if (r->IrqOutcome != XHCI_TBT_BRIRQ_DONE &&
        r->IrqOutcome != XHCI_TBT_BRIRQ_ACCESS) {
        return;
    }
    irq->Valid = 1;
    for (k = 0; k < r->IrqFnCount && k < XHCI_TBT_BRNUM_MAX_LINES; k++) {
        irq->Bdf[k] = r->IrqFnBdf[k];
        switch (r->IrqFnOut[k]) {
        case XHCI_TBT_BRFN_WRITTEN:
            irq->Out[k] = CM_NI_IRQ_WRITTEN;
            break;
        case XHCI_TBT_BRFN_ALREADY:
            irq->Out[k] = CM_NI_IRQ_ALREADY;
            break;
        case XHCI_TBT_BRFN_OTHER:
            irq->Out[k] = CM_NI_IRQ_UNROUTABLE;
            break;
        case XHCI_TBT_BRFN_NOT_HELD:
            irq->Out[k] = CM_NI_IRQ_UNKNOWN_HELD;
            break;
        default:
            irq->Out[k] = CM_NI_IRQ_UNKNOWN_READ;
            break;
        }
    }
    irq->Count = k;
}

VOID CmNiSetIrq(PCM_NI_MEMBER m, ULONG gen, const CM_NI_IRQ *irq)
{
    m->Irq = *irq;
    m->IrqGen = gen;
}

ULONG CmNiWindow(const CM_NI_MEMBER *m)
{
    return m->WindowOpen;
}

ULONG CmNiInTransaction(const CM_NI_MEMBER *m)
{
    ULONG k;

    if (m->Out.Id != 0) {
        return 1;
    }
    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        if (m->Dev[k].Used && (m->Dev[k].State == CM_NI_S_ACTIVE ||
                               m->Dev[k].State == CM_NI_S_ATTEMPTED)) {
            return 1;
        }
    }
    return 0;
}

ULONG CmNiBusy(const CM_NI_MEMBER *m)
{
    ULONG k;

    if (m->WindowOpen || m->Out.Id != 0) {
        return 1;
    }
    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        if (m->Dev[k].Used && (m->Dev[k].State == CM_NI_S_ACTIVE ||
                               m->Dev[k].State == CM_NI_S_ATTEMPTED)) {
            return 1;
        }
    }
    return 0;
}

VOID CmNiWalkBegin(PCM_NI_MEMBER m)
{
    m->WalkSeq++;
    if (m->WalkSeq == 0) {
        m->WalkSeq = 1;
    }
}

VOID CmNiWalkDone(PCM_NI_MEMBER m, ULONG complete)
{
    ULONG k;
    CM_NI_DEV *d;

    if (!complete) {
        return;
    }
    /* A complete walk proves the others gone: forgotten, their journals
     * retired, nothing of theirs freed. An attempt in progress is left to
     * its own reads, which see the disappearance. */
    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        d = &m->Dev[k];
        if (d->Used && d->WalkSeq != m->WalkSeq &&
            d->State != CM_NI_S_ACTIVE && d->State != CM_NI_S_ATTEMPTED &&
            (m->Out.Id == 0 || m->Out.Dev != k)) {
            niForget(m, d);
        }
    }
}

VOID CmNiSeen(PCM_NI_MEMBER m, ULONG dn, ULONG hash, ULONG bdf, ULONG rot)
{
    ULONG k;
    ULONG free;
    CM_NI_DEV *d;

    if (!m->WindowOpen || dn == 0) {
        return;
    }
    free = NI_NONE;
    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        d = &m->Dev[k];
        if (!d->Used) {
            if (free == NI_NONE) {
                free = k;
            }
            continue;
        }
        if (d->Dn != dn) {
            continue;
        }
        if (d->Hash == hash) {
            d->Bdf = bdf;
            d->Rot = rot;
            d->WalkSeq = m->WalkSeq;
            return;
        }
        /* The handle reused (record 21 section 7.4): a new device. An
         * attempt in progress on the old one is the old one's; its
         * outstanding result, if any, is accounted by id. */
        niForget(m, d);
        if (free == NI_NONE) {
            free = k;
        }
    }
    if (free == NI_NONE) {
        /* Full: an entry refused by its header (a bridge, another class)
         * makes room, its devnode judged again if it is met again. */
        for (k = 0; k < CM_NOIRQ_DEVICES && free == NI_NONE; k++) {
            d = &m->Dev[k];
            if (d->State == CM_NI_S_EXCLUDED && d->Why == CM_NI_W_HEADER &&
                d->WalkSeq != m->WalkSeq &&
                niJrn(m, d->Dn, d->Hash, 0) == NULL &&
                (m->Out.Id == 0 || m->Out.Dev != k)) {
                niForget(m, d);
                free = k;
            }
        }
    }
    if (free == NI_NONE) {
        m->C.NoRoom++;
        return;
    }
    d = &m->Dev[free];
    niZero(d, sizeof(CM_NI_DEV));
    d->Used = 1;
    d->Dn = dn;
    d->Hash = hash;
    d->Bdf = bdf;
    d->Rot = rot;
    d->WalkSeq = m->WalkSeq;
    d->Gen = m->Gen;
    d->Txn = 1;
    niPending(d);
}

VOID CmNiUnresolved(PCM_NI_MEMBER m)
{
    m->C.Unresolved++;
}

VOID CmNiPass(PCM_NI_MEMBER m, const CM_NI_CTX *ctx)
{
    ULONG k;
    CM_NI_DEV *d;
    ULONG closing;

    m->Pass++;
    m->Cur = CM_NOIRQ_DEVICES;
    closing = (ULONG)(m->WindowOpen &&
                      niSince(ctx->Now, m->WindowFrom) >= CM_NOIRQ_WINDOW_MS);
    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        d = &m->Dev[k];
        if (!d->Used) {
            continue;
        }
        if ((d->State == CM_NI_S_ACTIVE || d->State == CM_NI_S_ATTEMPTED) &&
            niPast(ctx->Now, d->Deadline)) {
            niFail(m, d, CM_NI_W_TIMEOUT, 0, 0, 0);
            d->Txn++;
            continue;
        }
        /* An attempt that ended under an older generation is judged
         * again under the new one while the window is open (rule 1). */
        if (m->WindowOpen && !closing && d->Gen != m->Gen &&
            d->State != CM_NI_S_ACTIVE && d->State != CM_NI_S_ATTEMPTED) {
            niPending(d);
            d->Gen = m->Gen;
            d->Txn++;
        }
        if (closing && d->State == CM_NI_S_PENDING) {
            niExclude(m, d, CM_NI_W_NOT_IN_TIME);
            d->Txn++;
        } else if (d->State == CM_NI_S_PENDING) {
            d->Step = NI_AD_HEADER;
        }
    }
    if (closing) {
        m->WindowOpen = 0;
    }
}

/* ---- the readings ----------------------------------------------------- */

static ULONG niLower(ULONG c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }
    return c;
}

static ULONG niIs(const char *s, ULONG bytes, const char *want)
{
    ULONG i;

    for (i = 0; want[i] != '\0'; i++) {
        if (i >= bytes || niLower((UCHAR)s[i]) != niLower((UCHAR)want[i])) {
            return 0;
        }
    }
    return (ULONG)(i == bytes || s[i] == '\0');
}

ULONG CmNiBindFlags(ULONG dlFound, const char *dl, ULONG dlBytes,
                    ULONG pdFound, const char *pd, ULONG pdBytes)
{
    ULONG f;

    f = 0;
    if (dlFound) {
        f |= CM_NI_BIND_DL;
        if (dl != NULL && niIs(dl, dlBytes, "*IOS")) {
            f |= CM_NI_BIND_DL_IOS;
        }
    }
    if (pdFound) {
        f |= CM_NI_BIND_PD;
        if (pd != NULL && niIs(pd, pdBytes, "stor98.mpd")) {
            f |= CM_NI_BIND_PD_STOR98;
        }
    }
    return f;
}

ULONG CmNiStor98Version(const char *text, ULONG bytes)
{
    ULONG part[4];
    ULONG n;
    ULONG i;
    ULONG digits;
    ULONG c;

    if (text == NULL) {
        return CM_NI_STOR98_OLD;
    }
    n = 0;
    part[0] = 0;
    digits = 0;
    for (i = 0; i < bytes && text[i] != '\0'; i++) {
        c = (UCHAR)text[i];
        if (c >= '0' && c <= '9') {
            if (part[n] > 65535UL) {
                return CM_NI_STOR98_OLD;
            }
            part[n] = part[n] * 10UL + (c - '0');
            digits++;
        } else if (c == '.' && digits != 0 && n < 3) {
            n++;
            part[n] = 0;
            digits = 0;
        } else {
            return CM_NI_STOR98_OLD;
        }
    }
    if (n != 3 || digits == 0) {
        return CM_NI_STOR98_OLD;
    }
    if (part[0] != 0) {
        return CM_NI_STOR98_OK;
    }
    return part[1] >= 4 ? CM_NI_STOR98_OK : CM_NI_STOR98_OLD;
}

ULONG CmNiClassify(ULONG pin, ULONG dev, ULONG rot, ULONG portIntr)
{
    ULONG portPin;
    ULONG portLine;

    portLine = portIntr & 0xFFUL;
    portPin = (portIntr >> 8) & 0xFFUL;
    if (portPin < 1 || portPin > 4 || portLine == 0 || portLine >= 16) {
        return CM_NI_IRQ_PORT_NO_LINE;
    }
    if (((pin - 1UL + dev + rot) & 3UL) == portPin - 1UL) {
        return CM_NI_IRQ_ROUTABLE;
    }
    return CM_NI_IRQ_UNROUTABLE;
}

/* The numbering's outcome for a function in this generation's record. */
static ULONG niRecorded(PCM_NI_MEMBER m, ULONG bdf)
{
    ULONG k;

    if (!m->Irq.Valid || m->IrqGen != m->Gen) {
        return CM_NI_IRQ_NONE;
    }
    if (m->Irq.PortNoLine) {
        return CM_NI_IRQ_PORT_NO_LINE;
    }
    for (k = 0; k < m->Irq.Count; k++) {
        if (m->Irq.Bdf[k] == bdf) {
            return m->Irq.Out[k];
        }
    }
    return CM_NI_IRQ_NONE;
}

/* Rule 2's reading: 1 a configuration problem (admitted), 0 PENDING, 2
 * EXCLUDED. */
static ULONG niProblem(ULONG status, ULONG problem)
{
    if ((status & CM_NI_DN_HAS_PROBLEM) != 0) {
        if (problem == 0x1DUL || problem == 0x0CUL || problem == 0x10UL ||
            problem == 0x1BUL) {
            return (status & CM_NI_DN_STARTED) != 0 ? 2UL : 1UL;
        }
        if (problem == 0x01UL || problem == 0x08UL || problem == 0x12UL ||
            problem == 0x1CUL) {
            return 0;
        }
        return 2;
    }
    if ((status & CM_NI_DN_STARTED) != 0) {
        return 2;
    }
    return 0;
}

/* A devnode read gone or replaced: TRUE, the entry forgotten. */
static ULONG niGone(PCM_NI_MEMBER m, CM_NI_DEV *d, const CM_NI_RES *res)
{
    if (res->Cr == CM_NI_CR_NO_SUCH_DEVNODE ||
        res->Cr == CM_NI_CR_INVALID_DEVNODE ||
        (res->Cr == CM_NI_CR_SUCCESS && res->Hash != d->Hash)) {
        niForget(m, d);
        return 1;
    }
    return 0;
}

/* ---- the next operation ----------------------------------------------- */

static VOID niOp(PCM_NI_MEMBER m, CM_NI_DEV *d, ULONG kind, PCM_NI_OP op)
{
    niZero(op, sizeof(CM_NI_OP));
    op->Id = m->NextId++;
    if (m->NextId == 0) {
        m->NextId = 1;
    }
    op->Kind = kind;
    op->Dev = (ULONG)(d - m->Dev);
    op->Txn = d->Txn;
    op->Dn = d->Dn;
    op->Hash = d->Hash;
    op->Bdf = d->Bdf;
    op->LcKind = CM_NI_BASIC_LOG_CONF;
}

/* The resource type of each descriptor-reading phase. */
static ULONG niPhaseType(ULONG phase)
{
    switch (phase) {
    case 0:
        return CM_NI_RES_MEM;
    case 1:
        return CM_NI_RES_IRQ;
    case 2:
        return CM_NI_RES_IO;
    case 3:
        return CM_NI_RES_DMA;
    default:
        return CM_NI_RES_ALL;
    }
}

/* The operation entry d's step wants now, or 0 (nothing this pass). */
static ULONG niWant(PCM_NI_MEMBER m, CM_NI_DEV *d, const CM_NI_CTX *ctx,
                    PCM_NI_OP op)
{
    ULONG attempt;

    attempt = (ULONG)(d->State == CM_NI_S_ACTIVE ||
                      d->State == CM_NI_S_ATTEMPTED);
    if (d->State == CM_NI_S_PENDING && !ctx->Switch) {
        niExclude(m, d, CM_NI_W_OFF);
        return 0;
    }
    if (!attempt && d->State != CM_NI_S_PENDING) {
        return 0;
    }
    if (!ctx->Gate) {
        if (attempt) {
            niFail(m, d, CM_NI_W_CONTROLLER, 0, 0, 0);
            d->Txn++;
        }
        return 0;
    }
    switch (d->Step) {
    case NI_AD_HEADER:
        niOp(m, d, CM_NI_OP_HEADER, op);
        return 1;
    case NI_AD_STATUS:
    case NI_TX_C_STATUS:
    case NI_TX_R_STATUS:
    case NI_AT_STATUS:
        niOp(m, d, CM_NI_OP_STATUS, op);
        return 1;
    case NI_AD_BIND:
    case NI_TX_C_BIND:
    case NI_TX_R_BIND:
    case NI_AT_BIND:
        niOp(m, d, CM_NI_OP_BINDING, op);
        return 1;
    case NI_AD_PORT:
        niOp(m, d, CM_NI_OP_PORT_INTR, op);
        op->Bdf = m->PortBdf;
        return 1;
    case NI_AD_LCLIST:
        niOp(m, d, CM_NI_OP_LC_LIST, op);
        return 1;
    case NI_AD_RD:
        niOp(m, d, CM_NI_OP_RD_READ, op);
        op->Lc = d->RdAt;
        op->ResType = niPhaseType(d->RdPhase);
        /* Room for the next memory descriptor; none once the buffer is
         * full (the descriptor is then read for its type and size alone,
         * and a tenth memory one refuses the attempt). */
        if (d->MemCount < CM_WK_NOIRQ_MEM && d->RdPhase < 4) {
            op->Off = d->MemCount * CM_NI_DESC_BYTES;
            op->Len = CM_NI_DESC_BYTES;
        }
        return 1;
    case NI_TX_ADD:
        niOp(m, d, CM_NI_OP_ADD_EMPTY, op);
        return 1;
    case NI_TX_PLACE:
    case NI_TX_C_PLACE:
        niOp(m, d, CM_NI_OP_PLACE, op);
        op->Lc = d->Alt;
        return 1;
    case NI_TX_ADDRD:
        niOp(m, d, CM_NI_OP_ADD_RD, op);
        op->Lc = d->Alt;
        op->Off = d->MemOff[d->MemAdded];
        op->Len = d->MemLen[d->MemAdded];
        return 1;
    case NI_TX_SETPRI:
        niOp(m, d, CM_NI_OP_SET_PRI, op);
        op->Lc = d->Alt;
        op->Pri = CM_NI_LCPRI_SUBOPTIMAL;
        return 1;
    case NI_TX_REPROC:
        niOp(m, d, CM_NI_OP_REPROCESS, op);
        return 1;
    case NI_AT_READY:
        niOp(m, d, CM_NI_OP_READY, op);
        return 1;
    case NI_AT_LINE:
        niOp(m, d, CM_NI_OP_LINE, op);
        return 1;
    default:
        return 0;
    }
}

ULONG CmNiNext(PCM_NI_MEMBER m, const CM_NI_CTX *ctx, PCM_NI_OP op)
{
    CM_NI_DEV *d;
    ULONG k;

    if (m->Out.Id != 0) {
        return 0;
    }
    /* The entry being stepped first: one entry's steps follow each other
     * in the same pass until it waits (record 28: devices of one member
     * advance one at a time). */
    if (m->Cur < CM_NOIRQ_DEVICES) {
        d = &m->Dev[m->Cur];
        if (d->Used && d->Pass == m->Pass && niWant(m, d, ctx, op)) {
            m->Out = *op;
            return 1;
        }
        m->Cur = CM_NOIRQ_DEVICES;
    }
    for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
        d = &m->Dev[k];
        if (!d->Used || d->Pass == m->Pass) {
            continue;
        }
        d->Pass = m->Pass;
        if (d->State == CM_NI_S_ATTEMPTED) {
            d->Step = NI_AT_STATUS;
        }
        if (niWant(m, d, ctx, op)) {
            m->Cur = k;
            m->Out = *op;
            return 1;
        }
    }
    return 0;
}

/* ---- the results ------------------------------------------------------ */

/* A result whose attempt has ended: accounted, never acted on. */
static VOID niLate(PCM_NI_MEMBER m, const CM_NI_OP *op, const CM_NI_RES *res)
{
    CM_NI_JRN *j;
    ULONG k;

    m->C.Late++;
    if (res->Cr != CM_NI_CR_SUCCESS) {
        return;
    }
    if (op->Kind == CM_NI_OP_ADD_EMPTY) {
        /* Journalled only while its devnode is still an entry: one
         * forgotten took its configurations with it. */
        j = NULL;
        for (k = 0; k < CM_NOIRQ_DEVICES; k++) {
            if (m->Dev[k].Used && m->Dev[k].Dn == op->Dn &&
                m->Dev[k].Hash == op->Hash) {
                j = niJrn(m, op->Dn, op->Hash, 1);
            }
        }
        if (j != NULL && j->Created < CM_NOIRQ_ATTEMPTS) {
            j->E[j->Created].Lc = res->Handle;
            j->E[j->Created].Kind = op->LcKind;
            j->E[j->Created].Gen = m->Gen;
            j->E[j->Created].Committed = 0;
            j->E[j->Created].Gone = 0;
            j->Created++;
        }
        m->C.Attempts++;
        m->C.Leftovers++;
    } else if (op->Kind == CM_NI_OP_SET_PRI) {
        j = niJrn(m, op->Dn, op->Hash, 0);
        if (j != NULL) {
            for (k = 0; k < j->Created; k++) {
                if (j->E[k].Lc == op->Lc && !j->E[k].Gone) {
                    j->E[k].Committed = 1;
                    /* No longer a leftover: complete and eligible. */
                    if (m->C.Leftovers != 0) {
                        m->C.Leftovers--;
                    }
                }
            }
        }
    }
}

/* The admission's rules 1 to 5 passed: the gate over the existing
 * configurations (LC_LIST's result). */
static VOID niGate(PCM_NI_MEMBER m, CM_NI_DEV *d, const CM_NI_CTX *ctx,
                   const CM_NI_RES *res)
{
    CM_NI_JRN *j;
    CM_NI_JENT *c;
    ULONG match;
    ULONG k;
    ULONG pri;

    if (res->Cr != CM_NI_CR_SUCCESS && res->Cr != CM_NI_CR_NO_MORE_LOG_CONF) {
        d->Step = NI_AT_WAIT;           /* PENDING: read again */
        return;
    }
    if (res->Cut) {
        niExclude(m, d, CM_NI_W_CONFIG);
        return;
    }
    j = niJrn(m, d->Dn, d->Hash, 0);
    c = niCommitted(j);
    match = 0;
    if (c != NULL) {
        for (k = 0; k < res->Count; k++) {
            if (res->Lc[k] == c->Lc) {
                if (res->Pri[k] == CM_NI_LCPRI_SUBOPTIMAL) {
                    match = 1;
                }
            }
        }
        if (!match) {
            /* Not found, or at another priority: no longer this
             * driver's (rule 1). */
            c->Gone = 1;
            m->C.Gone++;
            c = NULL;
        }
    }
    d->Source = 0;
    for (k = 0; k < res->Count; k++) {
        pri = res->Pri[k];
        if (c != NULL && res->Lc[k] == c->Lc) {
            continue;
        }
        if (pri == CM_NI_LCPRI_DISABLED) {
            continue;
        }
        if (pri >= CM_NI_LCPRI_SUBOPTIMAL) {
            if (pri == CM_NI_LCPRI_SUBOPTIMAL) {
                m->C.LeftOver++;
                niExclude(m, d, CM_NI_W_LEFTOVER);
            } else {
                niExclude(m, d, CM_NI_W_CONFIG);
            }
            return;
        }
        if (d->Source == 0 && !niJournalled(j, res->Lc[k])) {
            d->Source = res->Lc[k];
        }
    }
    if (!niWindowLive(m, ctx->Now)) {
        niExclude(m, d, CM_NI_W_NOT_IN_TIME);
        return;
    }
    if (c != NULL) {
        /* One reprocess per generation, whatever the attempts count. */
        if (j->ReprocGen == m->Gen + 1) {
            niExclude(m, d, CM_NI_W_ATTEMPTS);
            return;
        }
        j->ReprocGen = m->Gen + 1;
        m->C.Candidates++;
        m->C.Reprocesses++;
        d->State = CM_NI_S_ACTIVE;
        d->Deadline = ctx->Now + CM_NOIRQ_TXN_MS;
        d->ReprocOnly = 1;
        d->Alt = c->Lc;
        d->AltDone = 1;
        d->Step = NI_TX_R_STATUS;
        niDiag(m, d, 0, d->ProbBefore);
        return;
    }
    if (d->Source == 0) {
        niExclude(m, d, CM_NI_W_CONFIG);
        return;
    }
    /* The creation bound is tested once the descriptors say a
     * configuration is to be created (niDescriptor): the memory-only form
     * creates none. */
    d->HaveIrq = 0;
    d->MemCount = 0;
    d->MemAdded = 0;
    d->RdAt = d->Source;
    d->RdPhase = 0;
    d->RdAllSeen = 0;
    d->RdAllIrq = 0;
    d->Step = NI_AD_RD;
}

/* The memory-only form (record 28 section 12): PCI.VXD's own
 * configuration already holds the memory alone and the problem is the 29 it
 * set once at the devnode's creation, so no configuration is added; the
 * attempt is the reprocess-only one, once a generation (the journal's
 * reprocess mark, with no configuration in it). */
static VOID niMemOnly(PCM_NI_MEMBER m, CM_NI_DEV *d, const CM_NI_CTX *ctx)
{
    CM_NI_JRN *j;

    j = niJrn(m, d->Dn, d->Hash, 1);
    if (j == NULL || j->ReprocGen == m->Gen + 1) {
        niExclude(m, d, CM_NI_W_ATTEMPTS);
        return;
    }
    if (!niWindowLive(m, ctx->Now)) {
        niExclude(m, d, CM_NI_W_NOT_IN_TIME);
        return;
    }
    j->ReprocGen = m->Gen + 1;
    m->C.Candidates++;
    m->C.Reprocesses++;
    m->C.MemOnly++;
    d->State = CM_NI_S_ACTIVE;
    d->Deadline = ctx->Now + CM_NOIRQ_TXN_MS;
    d->ReprocOnly = 1;
    d->Alt = 0;
    d->AltDone = 0;
    d->Step = NI_TX_R_STATUS;
    niDiag(m, d, 0, d->ProbBefore);
}

/* One of the source's descriptors (RD_READ's result). */
static VOID niDescriptor(PCM_NI_MEMBER m, CM_NI_DEV *d, const CM_NI_CTX *ctx,
                         const CM_NI_RES *res)
{
    CM_NI_JRN *j;

    if (res->Cr == CM_NI_CR_NO_MORE_RES_DES && d->RdPhase < 4) {
        /* This type read to its end: the next, from the configuration;
         * the last phase reads every type, for any other one. */
        d->RdPhase++;
        d->RdAt = d->Source;
        return;
    }
    if (res->Cr == CM_NI_CR_NO_MORE_RES_DES) {
        /* The every-type read must have met each descriptor the typed
         * reads found; fewer leaves the absence of any other type
         * unproven, and the attempt is refused. */
        if (d->RdAllSeen != d->MemCount + d->HaveIrq ||
            d->RdAllIrq != d->HaveIrq) {
            niExclude(m, d, CM_NI_W_CONFIG);
            return;
        }
        if (!d->HaveIrq && d->MemCount != 0 &&
            d->ProbBefore == CM_NI_PROB_HW_DISABLED) {
            niMemOnly(m, d, ctx);
            return;
        }
        if (!d->HaveIrq || d->MemCount == 0) {
            niExclude(m, d, CM_NI_W_CONFIG);
            return;
        }
        if (!niWindowLive(m, ctx->Now)) {
            niExclude(m, d, CM_NI_W_NOT_IN_TIME);
            return;
        }
        j = niJrn(m, d->Dn, d->Hash, 0);
        if (j != NULL && j->Created >= CM_NOIRQ_ATTEMPTS) {
            niExclude(m, d, CM_NI_W_ATTEMPTS);
            return;
        }
        /* Every rule passed: ACTIVE, on its own deadline. */
        m->C.Candidates++;
        d->State = CM_NI_S_ACTIVE;
        d->Deadline = ctx->Now + CM_NOIRQ_TXN_MS;
        d->ReprocOnly = 0;
        d->Alt = 0;
        d->AltDone = 0;
        d->Step = NI_TX_ADD;
        niDiag(m, d, 0, d->ProbBefore);
        return;
    }
    if (res->Cr != CM_NI_CR_SUCCESS) {
        d->Step = NI_AT_WAIT;
        return;
    }
    d->RdAt = res->Rd;
    if (d->RdPhase == 4) {
        /* The every-type read: memory and IRQ were taken by their own
         * phases; anything else (an OEM or ignored type) refuses. */
        if (res->Type != CM_NI_RES_MEM && res->Type != CM_NI_RES_IRQ) {
            niExclude(m, d, CM_NI_W_CONFIG);
            return;
        }
        d->RdAllSeen++;
        if (res->Type == CM_NI_RES_IRQ) {
            d->RdAllIrq++;
        }
        return;
    }
    if (res->Type == CM_NI_RES_IRQ) {
        /* More than one IRQ descriptor is not this record's form. */
        if (d->HaveIrq) {
            niExclude(m, d, CM_NI_W_CONFIG);
            return;
        }
        d->HaveIrq = 1;
        return;
    }
    if (res->Type != CM_NI_RES_MEM || d->MemCount >= CM_WK_NOIRQ_MEM ||
        res->Size == 0 || res->Size > CM_NI_DESC_BYTES) {
        niExclude(m, d, CM_NI_W_CONFIG);
        return;
    }
    d->MemOff[d->MemCount] = d->MemCount * CM_NI_DESC_BYTES;
    d->MemLen[d->MemCount] = res->Size;
    d->MemCount++;
}

/* HEADER's result (rule 3). */
static VOID niHeader(PCM_NI_MEMBER m, CM_NI_DEV *d, const CM_NI_RES *res)
{
    ULONG id;
    ULONG bar;
    ULONG k;

    if (res->Cr != CM_NI_CR_SUCCESS) {
        d->Step = NI_AT_WAIT;
        return;
    }
    id = res->Val[0];
    if (id == 0xFFFFFFFFUL || (id & 0xFFFFUL) == 0x0001UL ||
        (id & 0xFFFFUL) == 0xFFFFUL) {
        /* Not answering, or Retry Status: read again. */
        d->Step = NI_AT_WAIT;
        return;
    }
    d->Pin = (res->Val[15] >> 8) & 0xFFUL;
    d->Line = res->Val[15] & 0xFFUL;
    if ((res->Val[2] >> 8) != 0x010802UL ||
        ((res->Val[3] >> 16) & 0x7FUL) != 0) {
        niExclude(m, d, CM_NI_W_HEADER);
        return;
    }
    if (d->Pin == 0) {
        /* Section 5.4: not a candidate; counted with its status. */
        d->Step = NI_AD_STATUS;
        return;
    }
    if (d->Pin > 4 || (d->Line != 0 && d->Line != 0xFFUL)) {
        niExclude(m, d, CM_NI_W_HEADER);
        return;
    }
    for (k = 0; k < 6; k++) {
        bar = res->Val[4 + k];
        if ((bar & 1UL) != 0) {
            niExclude(m, d, CM_NI_W_HEADER);
            return;
        }
        if ((bar & 0x6UL) == 0x4UL) {
            k++;                        /* a 64-bit BAR's upper half */
        }
    }
    d->Step = NI_AD_STATUS;
}

/* The interrupt rule (rule 5), from the record or the port's reading. */
static VOID niInterrupt(PCM_NI_MEMBER m, CM_NI_DEV *d, ULONG out)
{
    d->Irq = out;
    if (out == CM_NI_IRQ_UNROUTABLE || out == CM_NI_IRQ_PORT_NO_LINE) {
        d->Step = NI_AD_LCLIST;
        return;
    }
    niExclude(m, d, CM_NI_W_INTERRUPT);
}

VOID CmNiTake(PCM_NI_MEMBER m, const CM_NI_CTX *ctx, const CM_NI_RES *res)
{
    CM_NI_OP op;
    CM_NI_DEV *d;
    CM_NI_JRN *j;
    ULONG rule;
    ULONG out;

    if (m->Out.Id == 0 || res->Id != m->Out.Id) {
        m->C.Dropped++;
        return;
    }
    op = m->Out;
    niZero(&m->Out, sizeof(CM_NI_OP));
    d = op.Dev < CM_NOIRQ_DEVICES ? &m->Dev[op.Dev] : NULL;
    if (d == NULL || !d->Used || d->Txn != op.Txn || d->Dn != op.Dn ||
        d->Hash != op.Hash ||
        (d->State != CM_NI_S_PENDING && d->State != CM_NI_S_ACTIVE &&
         d->State != CM_NI_S_ATTEMPTED)) {
        niLate(m, &op, res);
        return;
    }
    /* The disappearance first, whatever else (record 28 step 5): a
     * devnode a read shows gone or replaced is forgotten. */
    if ((op.Kind == CM_NI_OP_STATUS && niGone(m, d, res)) ||
        (op.Kind == CM_NI_OP_PLACE && (res->Place == CM_NI_PLACE_GONE ||
                                       res->Place == CM_NI_PLACE_OTHER)) ||
        (op.Kind == CM_NI_OP_READY &&
         (res->Cr == CM_NI_CR_NO_SUCH_DEVNODE ||
          res->Cr == CM_NI_CR_INVALID_DEVNODE))) {
        if (d->Used) {
            niForget(m, d);
        }
        return;
    }
    /* A result past the attempt's deadline, or taken with the gate
     * closed: the attempt is over, the result accounted (record 28,
     * "When" and "the controller's loss"). */
    if ((d->State == CM_NI_S_ACTIVE || d->State == CM_NI_S_ATTEMPTED) &&
        !ctx->Gate) {
        niFail(m, d, CM_NI_W_CONTROLLER, 0, 0, 0);
        d->Txn++;
        niLate(m, &op, res);
        return;
    }
    if ((d->State == CM_NI_S_ACTIVE || d->State == CM_NI_S_ATTEMPTED) &&
        niPast(ctx->Now, d->Deadline)) {
        niFail(m, d, CM_NI_W_TIMEOUT, 0, 0, 0);
        d->Txn++;
        niLate(m, &op, res);
        return;
    }
    d->LastStep = op.Kind;
    d->LastCr = res->Cr;
    switch (op.Kind) {
    case CM_NI_OP_HEADER:
        niHeader(m, d, res);
        break;
    case CM_NI_OP_STATUS:
        if (res->Cr != CM_NI_CR_SUCCESS) {
            if (d->State == CM_NI_S_PENDING || d->State == CM_NI_S_ATTEMPTED) {
                d->Step = NI_AT_WAIT;
            } else {
                niFail(m, d, CM_NI_W_ADMISSION, res->Cr, 0, 0);
                d->Txn++;
            }
            break;
        }
        rule = niProblem(res->Status, res->Problem);
        if (d->Step == NI_AD_STATUS) {
            if (d->Pin == 0) {
                if (d->Pin0Gen != m->Gen + 1) {
                    d->Pin0Gen = m->Gen + 1;
                    m->C.Pin0Seen++;
                    if ((res->Status & CM_NI_DN_STARTED) != 0 &&
                        (res->Status & CM_NI_DN_HAS_PROBLEM) == 0) {
                        m->C.Pin0Started++;
                    }
                    niDiagBump(m);
                    m->Diag[11] = (m->C.Pin0Seen & 0xFFFFUL) |
                                  ((m->C.Pin0Started & 0xFFFFUL) << 16);
                }
                niExclude(m, d, CM_NI_W_NO_PIN);
                break;
            }
            if (rule == 0) {
                d->Step = NI_AT_WAIT;
            } else if (rule == 2) {
                niExclude(m, d, CM_NI_W_NOT_CONFIG);
            } else {
                d->ProbBefore = res->Problem;
                d->Step = NI_AD_BIND;
            }
        } else if (d->Step == NI_AT_STATUS) {
            if ((res->Status & CM_NI_DN_STARTED) != 0 &&
                (res->Status & CM_NI_DN_HAS_PROBLEM) == 0) {
                d->Step = NI_AT_LINE;
            } else if (!d->ReadyTried && rule == 1 &&
                       niSince(ctx->Now, d->AttemptedAt) >=
                           CM_NI_READY_AFTER_MS) {
                d->Step = NI_AT_BIND;
            } else {
                d->Step = NI_AT_WAIT;
            }
        } else if (rule != 1) {
            /* Before the commit or the reprocess: the device the user
             * disabled meanwhile, or started, is not made ready. */
            niFail(m, d, CM_NI_W_ADMISSION, 0, res->Status, res->Problem);
            d->Txn++;
        } else {
            d->Step = d->Step == NI_TX_C_STATUS ? NI_TX_C_BIND : NI_TX_R_BIND;
        }
        break;
    case CM_NI_OP_BINDING:
        if (d->Step == NI_AD_BIND) {
            if (res->Cr != CM_NI_CR_SUCCESS || res->Bind == 0 ||
                (res->Bind & (CM_NI_BIND_DL | CM_NI_BIND_PD)) !=
                    (CM_NI_BIND_DL | CM_NI_BIND_PD)) {
                d->Step = NI_AT_WAIT;   /* not bound yet, or being bound */
                break;
            }
            if (res->Bind != CM_NI_BIND_OK) {
                niExclude(m, d, CM_NI_W_NOT_STOR98);
                break;
            }
            if (ctx->Stor98 == CM_NI_STOR98_FAILED) {
                d->Step = NI_AT_WAIT;
                break;
            }
            if (ctx->Stor98 != CM_NI_STOR98_OK) {
                niExclude(m, d, CM_NI_W_NOT_STOR98);
                break;
            }
            out = niRecorded(m, d->Bdf);
            if (out == CM_NI_IRQ_NONE || out == CM_NI_IRQ_UNKNOWN_READ) {
                d->Step = NI_AD_PORT;   /* classified afresh */
            } else {
                niInterrupt(m, d, out);
            }
            break;
        }
        if (res->Cr != CM_NI_CR_SUCCESS || res->Bind != CM_NI_BIND_OK) {
            if (d->Step == NI_AT_BIND) {
                d->ReadyTried = 1;
                d->Step = NI_AT_WAIT;
            } else {
                niFail(m, d, CM_NI_W_ADMISSION, res->Cr, 0, 0);
                d->Txn++;
            }
            break;
        }
        if (d->Step == NI_TX_C_BIND) {
            d->Step = NI_TX_C_PLACE;
        } else if (d->Step == NI_TX_R_BIND) {
            d->Step = NI_TX_REPROC;
        } else {
            d->Step = NI_AT_READY;
        }
        break;
    case CM_NI_OP_PORT_INTR:
        /* Val[1] the port's id: one not answering, or answering Retry
         * Status, proves nothing about its line. */
        if (res->Cr != CM_NI_CR_SUCCESS || res->Val[0] == 0xFFFFFFFFUL ||
            res->Val[1] == 0xFFFFFFFFUL ||
            (res->Val[1] & 0xFFFFUL) == 0x0001UL ||
            (res->Val[1] & 0xFFFFUL) == 0xFFFFUL) {
            d->Step = NI_AT_WAIT;
            break;
        }
        niInterrupt(m, d, CmNiClassify(d->Pin, (d->Bdf >> 3) & 0x1FUL,
                                       d->Rot, res->Val[0]));
        break;
    case CM_NI_OP_LC_LIST:
        niGate(m, d, ctx, res);
        break;
    case CM_NI_OP_RD_READ:
        niDescriptor(m, d, ctx, res);
        break;
    case CM_NI_OP_ADD_EMPTY:
        if (res->Cr != CM_NI_CR_SUCCESS || res->Handle == 0) {
            niFail(m, d, CM_NI_W_STEP, res->Cr, 0, 0);
            d->Txn++;
            break;
        }
        /* Journalled at once, before any further call. */
        j = niJrn(m, d->Dn, d->Hash, 1);
        if (j != NULL && j->Created < CM_NOIRQ_ATTEMPTS) {
            j->E[j->Created].Lc = res->Handle;
            j->E[j->Created].Kind = CM_NI_BASIC_LOG_CONF;
            j->E[j->Created].Gen = m->Gen;
            j->E[j->Created].Committed = 0;
            j->E[j->Created].Gone = 0;
            j->Created++;
        }
        m->C.Attempts++;
        d->Alt = res->Handle;
        d->Step = NI_TX_PLACE;
        break;
    case CM_NI_OP_PLACE:
        if (res->Place == CM_NI_PLACE_GONE || res->Place == CM_NI_PLACE_OTHER) {
            niForget(m, d);
            break;
        }
        if (res->Place != CM_NI_PLACE_AT ||
            res->PlacePri != CM_NI_LCPRI_DISABLED) {
            niFail(m, d, CM_NI_W_HANDLE, res->Cr, 0, 0);
            d->Txn++;
            break;
        }
        d->Step = d->Step == NI_TX_PLACE ? NI_TX_ADDRD : NI_TX_SETPRI;
        break;
    case CM_NI_OP_ADD_RD:
        if (res->Cr != CM_NI_CR_SUCCESS) {
            niFail(m, d, CM_NI_W_STEP, res->Cr, 0, 0);
            d->Txn++;
            break;
        }
        d->MemAdded++;
        d->Step = d->MemAdded < d->MemCount ? NI_TX_PLACE : NI_TX_C_STATUS;
        break;
    case CM_NI_OP_SET_PRI:
        if (res->Cr != CM_NI_CR_SUCCESS) {
            niFail(m, d, CM_NI_W_STEP, res->Cr, 0, 0);
            d->Txn++;
            break;
        }
        j = niJrn(m, d->Dn, d->Hash, 0);
        if (j != NULL && j->Created != 0 && j->E[j->Created - 1].Lc == d->Alt) {
            j->E[j->Created - 1].Committed = 1;
        }
        d->AltDone = 1;
        d->Step = NI_TX_R_STATUS;
        break;
    case CM_NI_OP_REPROCESS:
        if (res->Cr != CM_NI_CR_SUCCESS) {
            niFail(m, d, CM_NI_W_STEP, res->Cr, 0, 0);
            d->Txn++;
            break;
        }
        d->State = CM_NI_S_ATTEMPTED;
        d->AttemptedAt = ctx->Now;
        d->ReadyTried = 0;
        d->Step = NI_AT_WAIT;
        niDiag(m, d, 0, d->ProbBefore);
        break;
    case CM_NI_OP_READY:
        d->ReadyTried = 1;
        if (res->Cr == CM_NI_CR_NO_SUCH_DEVNODE ||
            res->Cr == CM_NI_CR_INVALID_DEVNODE) {
            niForget(m, d);
            break;
        }
        if (res->Cr != CM_NI_CR_SUCCESS) {
            niFail(m, d, CM_NI_W_STEP, res->Cr, 0, 0);
            d->Txn++;
            break;
        }
        d->Step = NI_AT_WAIT;
        break;
    case CM_NI_OP_LINE:
        /* Started (the status read before it said so); the line only
         * where it was read - the diagnostic's step word says which. */
        if (res->Cr == CM_NI_CR_SUCCESS) {
            d->Line = res->Val[0] & 0xFFUL;
        }
        d->State = CM_NI_S_DONE;
        d->Step = 0;
        m->C.Started++;
        niDiag(m, d, CM_NI_DN_STARTED, 0);
        break;
    default:
        break;
    }
}

VOID CmNiDiag(const CM_NI_MEMBER *m, PULONG diag)
{
    ULONG k;

    for (k = 0; k < CM_NI_DIAG_WORDS; k++) {
        diag[k] = m->Diag[k];
    }
    diag[0] = m->DiagSeq;
}
