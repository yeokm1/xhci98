/*
 * cm_marks.c - the pure half of the marks answer (cm_marks.h; design
 * record 21 section 7.4, revision 3; task 42-A.5).
 *
 * IRQL: any; no lock (the caller holds the registration list's).
 */

#include "cm_marks.h"

static VOID cmMkPut32(UCHAR *b, ULONG offset, ULONG v)
{
    b[offset] = (UCHAR)(v & 0xFFUL);
    b[offset + 1] = (UCHAR)((v >> 8) & 0xFFUL);
    b[offset + 2] = (UCHAR)((v >> 16) & 0xFFUL);
    b[offset + 3] = (UCHAR)((v >> 24) & 0xFFUL);
}

static ULONG cmMkGet32(const UCHAR *b, ULONG offset)
{
    return (ULONG)b[offset] | ((ULONG)b[offset + 1] << 8) |
           ((ULONG)b[offset + 2] << 16) | ((ULONG)b[offset + 3] << 24);
}

static VOID cmMkZero(PVOID p, ULONG n)
{
    PUCHAR b;
    ULONG i;

    b = (PUCHAR)p;
    for (i = 0; i < n; i++) {
        b[i] = 0;
    }
}

static VOID cmMkBump(PCM_MARKS m)
{
    m->Gen++;
    if (m->Gen == 0) {
        m->Gen = 1;
    }
}

VOID CmMarksReset(PCM_MARKS m)
{
    cmMkZero(m, sizeof(*m));
}

PCM_MARK CmMarksFind(PCM_MARKS m, PVOID pdo)
{
    ULONG i;

    if (pdo == NULL) {
        return NULL;
    }
    for (i = 0; i < CM_QM_MAX_ENTRIES; i++) {
        if (m->E[i].Pdo == pdo && !m->E[i].Tomb) {
            return &m->E[i];
        }
    }
    return NULL;
}

VOID CmMarksTouch(PCM_MARKS m, PCM_MARK e, ULONG now)
{
    cmMkBump(m);
    e->ChangedAt = now;
}

BOOLEAN CmMarksWasSeen(const CM_MARKS *m, ULONG gen)
{
    if (!m->ReadAny) {
        return FALSE;
    }
    return (BOOLEAN)(((m->ReadGen - gen) & 0x80000000UL) == 0);
}

VOID CmMarksSeen(PCM_MARKS m, ULONG gen)
{
    if (!m->ReadAny || ((gen - m->ReadGen) & 0x80000000UL) == 0) {
        m->ReadGen = gen;
        m->ReadAny = 1;
    }
}

static ULONG cmMkCount(const CM_MARKS *m, BOOLEAN tombs)
{
    ULONG i;
    ULONG n;

    n = 0;
    for (i = 0; i < CM_QM_MAX_ENTRIES; i++) {
        if (m->E[i].Pdo != NULL && (m->E[i].Tomb != 0) == (tombs != 0)) {
            n++;
        }
    }
    return n;
}

/* A current entry made a tombstone, evicting the oldest tombstone first
 * when the set is full. */
static VOID cmMkBury(PCM_MARKS m, PCM_MARK e, ULONG now)
{
    ULONG i;
    ULONG oldest;
    BOOLEAN have;

    if (cmMkCount(m, TRUE) >= CM_QM_MAX_TOMBS) {
        have = FALSE;
        oldest = 0;
        for (i = 0; i < CM_QM_MAX_ENTRIES; i++) {
            if (m->E[i].Pdo == NULL || !m->E[i].Tomb) {
                continue;
            }
            if (!have ||
                ((now - m->E[i].TombAt) > (now - m->E[oldest].TombAt))) {
                oldest = i;
                have = TRUE;
            }
        }
        if (have) {
            if (!CmMarksWasSeen(m, m->E[oldest].TombGen)) {
                m->Dropped++;
            }
            cmMkZero(&m->E[oldest], sizeof(CM_MARK));
        }
    }
    e->Tomb = 1;
    e->PendingAt = 0;
    e->PendingGen = 0;
    e->TombAt = now;
    CmMarksTouch(m, e, now);
    e->TombGen = m->Gen;
}

VOID CmMarksBegin(PCM_MARKS m)
{
    m->Incomplete = 0;
}

BOOLEAN CmMarksSync(PCM_MARKS m, ULONG member, ULONG memberBdf,
                    PVOID const *snap, ULONG count, BOOLEAN truncated,
                    BOOLEAN active, ULONG now)
{
    ULONG i;
    ULONG k;
    BOOLEAN found;
    BOOLEAN changed;
    PCM_MARK e;

    changed = FALSE;
    if (!active) {
        count = 0;
    }
    if (active && truncated) {
        m->Incomplete = 1;
    }
    for (i = 0; i < CM_QM_MAX_ENTRIES; i++) {
        e = &m->E[i];
        if (e->Pdo == NULL || e->Tomb || e->Member != member) {
            continue;
        }
        found = FALSE;
        for (k = 0; k < count; k++) {
            if (snap[k] == e->Pdo) {
                found = TRUE;
            }
        }
        if (!found || e->MemberBdf != memberBdf) {
            cmMkBury(m, e, now);
            changed = TRUE;
        }
    }
    for (k = 0; k < count; k++) {
        if (snap[k] == NULL || CmMarksFind(m, snap[k]) != NULL) {
            continue;
        }
        if (cmMkCount(m, FALSE) >= CM_QM_MAX_CURRENT) {
            m->Incomplete = 1;
            continue;
        }
        e = NULL;
        for (i = 0; i < CM_QM_MAX_ENTRIES && e == NULL; i++) {
            if (m->E[i].Pdo == NULL) {
                e = &m->E[i];
            }
        }
        if (e == NULL) {
            /* Every slot is a current entry or a tombstone the set's own
             * bound keeps: the oldest tombstone gives way. */
            ULONG oldest;

            oldest = CM_QM_MAX_ENTRIES;
            for (i = 0; i < CM_QM_MAX_ENTRIES; i++) {
                if (m->E[i].Tomb &&
                    (oldest == CM_QM_MAX_ENTRIES ||
                     (now - m->E[i].TombAt) > (now - m->E[oldest].TombAt))) {
                    oldest = i;
                }
            }
            if (oldest == CM_QM_MAX_ENTRIES) {
                m->Incomplete = 1;
                continue;
            }
            if (!CmMarksWasSeen(m, m->E[oldest].TombGen)) {
                m->Dropped++;
            }
            e = &m->E[oldest];
        }
        cmMkZero(e, sizeof(CM_MARK));
        e->Pdo = snap[k];
        e->Member = member;
        e->MemberBdf = memberBdf;
        m->NextSerial++;
        if (m->NextSerial == 0) {
            m->NextSerial = 1;
        }
        e->Serial = m->NextSerial;
        CmMarksTouch(m, e, now);
        changed = TRUE;
    }
    return changed;
}

BOOLEAN CmMarksDropMembers(PCM_MARKS m, ULONG members, ULONG now)
{
    ULONG i;
    BOOLEAN changed;

    changed = FALSE;
    for (i = 0; i < CM_QM_MAX_ENTRIES; i++) {
        if (m->E[i].Pdo != NULL && !m->E[i].Tomb &&
            m->E[i].Member >= members) {
            cmMkBury(m, &m->E[i], now);
            changed = TRUE;
        }
    }
    return changed;
}

VOID CmMarksCount(PCM_MARKS m, PCM_MARK e, ULONG which, ULONG now)
{
    if (which >= CM_MK_COUNTS) {
        return;
    }
    if (e->Count[which] < 0xFFFFUL) {
        e->Count[which]++;
    }
    CmMarksTouch(m, e, now);
}

VOID CmMarksEvict(PCM_MARKS m, ULONG now)
{
    ULONG i;

    for (i = 0; i < CM_QM_MAX_ENTRIES; i++) {
        if (m->E[i].Pdo != NULL && m->E[i].Tomb &&
            now - m->E[i].TombAt >= CM_QM_TOMB_MS &&
            CmMarksWasSeen(m, m->E[i].TombGen)) {
            cmMkZero(&m->E[i], sizeof(CM_MARK));
        }
    }
}

static VOID cmMkEntry(UCHAR *b, const CM_MARK *e, ULONG safeOff)
{
    ULONG flags;

    cmMkZero(b, CM_QM_ENT_BYTES);
    flags = e->Flags & (CM_QM_EF_LOCATED | CM_QM_EF_BUS_ATT |
                        CM_QM_EF_ADD_ATT | CM_QM_EF_REMOVABLE);
    if (!e->Tomb) {
        flags |= CM_QM_EF_REPORTED;
    }
    if (safeOff) {
        flags |= CM_QM_EF_SAFE_EJECT_OFF;
    }
    cmMkPut32(b, CM_QM_E_MEMBER, e->MemberBdf);
    if (e->Flags & CM_QM_EF_LOCATED) {
        cmMkPut32(b, CM_QM_E_BUS, e->Bus);
        cmMkPut32(b, CM_QM_E_ADDRESS, e->Address);
    }
    cmMkPut32(b, CM_QM_E_SERIAL, e->Serial);
    cmMkPut32(b, CM_QM_E_FLAGS, flags);
    cmMkPut32(b, CM_QM_E_COUNTS0, (e->Count[CM_MK_QUERY_REMOVE] & 0xFFFFUL) |
                                  (e->Count[CM_MK_CANCEL_REMOVE] << 16));
    cmMkPut32(b, CM_QM_E_COUNTS1, (e->Count[CM_MK_SURPRISE] & 0xFFFFUL) |
                                  (e->Count[CM_MK_REMOVE_AFTER_QR] << 16));
    cmMkPut32(b, CM_QM_E_COUNTS2, (e->Count[CM_MK_REMOVE_NO_QR] & 0xFFFFUL) |
                                  (e->Count[CM_MK_START_FAILED] << 16));
    if (!e->Tomb && e->PendingAt != 0) {
        cmMkPut32(b, CM_QM_E_PENDING, e->PendingAt);
        cmMkPut32(b, CM_QM_E_PENDING_GEN, e->PendingGen);
    }
    cmMkPut32(b, CM_QM_E_CHANGED, e->ChangedAt);
}

ULONG CmMarksBuild(UCHAR *out, const CM_MARKS *m, const CM_QM_IN *in)
{
    ULONG at;
    ULONG n;
    ULONG cur;
    ULONG pass;
    ULONG i;
    ULONG flags;

    cmMkZero(out, CM_QM_HDR_BYTES);
    at = CM_QM_HDR_BYTES;
    n = 0;
    cur = 0;
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < CM_QM_MAX_ENTRIES; i++) {
            if (m->E[i].Pdo == NULL || (m->E[i].Tomb != 0) != (pass != 0)) {
                continue;
            }
            cmMkEntry(out + at, &m->E[i], in->SafeEjectOff);
            at += CM_QM_ENT_BYTES;
            n++;
            if (pass == 0) {
                cur++;
            }
        }
    }
    flags = 0;
    if (m->Incomplete) {
        flags |= CM_QM_F_INCOMPLETE;
    }
    if (in->SafeEjectOff) {
        flags |= CM_QM_F_SAFE_EJECT_OFF;
    }
    cmMkPut32(out, CM_QM_O_MAGIC, CM_QM_MAGIC);
    cmMkPut32(out, CM_QM_O_VERSION, CM_QM_VERSION);
    cmMkPut32(out, CM_QM_O_BYTES, at);
    cmMkPut32(out, CM_QM_O_EPOCH_LO, in->EpochLo);
    cmMkPut32(out, CM_QM_O_EPOCH_HI, in->EpochHi);
    cmMkPut32(out, CM_QM_O_GEN, m->Gen);
    cmMkPut32(out, CM_QM_O_COUNT, n);
    cmMkPut32(out, CM_QM_O_ENT_BYTES, CM_QM_ENT_BYTES);
    cmMkPut32(out, CM_QM_O_ENT_OFFSET, CM_QM_HDR_BYTES);
    cmMkPut32(out, CM_QM_O_FLAGS, flags);
    cmMkPut32(out, CM_QM_O_DROPPED, m->Dropped);
    cmMkPut32(out, CM_QM_O_NHI_BDF, in->NhiBdf);
    cmMkPut32(out, CM_QM_O_CURRENT, cur);
    return at;
}

ULONG CmMarksAnswer(UCHAR *out, ULONG room, const UCHAR *built, ULONG bytes,
                    ULONG pass, BOOLEAN stopping, PULONG info)
{
    ULONG i;
    ULONG n;

    *info = 0;
    if (out == NULL || room < CM_QM_HDR_BYTES || bytes < CM_QM_HDR_BYTES) {
        return 2UL;     /* CM_Q_ANS_TOO_SMALL */
    }
    n = room < bytes ? CM_QM_HDR_BYTES : bytes;
    for (i = 0; i < n; i++) {
        out[i] = built[i];
    }
    cmMkPut32(out, CM_QM_O_PASS, pass);
    if (stopping) {
        cmMkPut32(out, CM_QM_O_FLAGS,
                  cmMkGet32(out, CM_QM_O_FLAGS) | CM_QM_F_STOPPING);
    }
    *info = n;
    return room < bytes ? 1UL : 0UL;    /* CM_Q_ANS_HEADER : _WHOLE */
}

PCM_MARK CmMarksConfirm(PCM_MARKS m, const UCHAR *v, ULONG bytes,
                        ULONG epochLo, ULONG epochHi, BOOLEAN checkGen,
                        const ULONG *portGen, ULONG ports, PULONG why)
{
    ULONG serial;
    ULONG i;
    PCM_MARK e;

    *why = CM_MK_CONF_SHORT;
    if (v == NULL || bytes < CM_QC_BYTES) {
        return NULL;
    }
    if (cmMkGet32(v, CM_QC_O_EPOCH_LO) != epochLo ||
        cmMkGet32(v, CM_QC_O_EPOCH_HI) != epochHi) {
        *why = CM_MK_CONF_EPOCH;
        return NULL;
    }
    serial = cmMkGet32(v, CM_QC_O_SERIAL);
    e = NULL;
    for (i = 0; i < CM_QM_MAX_ENTRIES && serial != 0; i++) {
        if (m->E[i].Pdo != NULL && !m->E[i].Tomb &&
            m->E[i].Serial == serial) {
            e = &m->E[i];
        }
    }
    if (e == NULL) {
        *why = CM_MK_CONF_SERIAL;
        return NULL;
    }
    if ((e->Flags & CM_QM_EF_LOCATED) == 0 ||
        cmMkGet32(v, CM_QC_O_MEMBER) != e->MemberBdf ||
        cmMkGet32(v, CM_QC_O_BUS) != e->Bus ||
        cmMkGet32(v, CM_QC_O_ADDRESS) != e->Address) {
        *why = CM_MK_CONF_IDENTITY;
        return NULL;
    }
    if (e->PendingAt == 0) {
        *why = CM_MK_CONF_NOT_PENDING;
        return NULL;
    }
    if (checkGen &&
        (cmMkGet32(v, CM_QC_O_GEN) != e->PendingGen || e->Member >= ports ||
         portGen[e->Member] != e->PendingGen)) {
        *why = CM_MK_CONF_GEN;
        return NULL;
    }
    *why = CM_MK_CONF_OK;
    return e;
}
