/*
 * mark_core.c - the pure half of the mark service (mark_core.h).
 */

#include <stdio.h>
#include <string.h>
#include "mark_core.h"
#include "../src/cm/cm_query.h"
#include "../src/cm/cm_status.h"

static INST_U32 mk_get32(const unsigned char *b, size_t o)
{
    return (INST_U32)b[o] | ((INST_U32)b[o + 1] << 8) |
           ((INST_U32)b[o + 2] << 16) | ((INST_U32)b[o + 3] << 24);
}

static void mk_put32(unsigned char *b, size_t o, INST_U32 v)
{
    b[o] = (unsigned char)(v & 0xFFUL);
    b[o + 1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[o + 2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[o + 3] = (unsigned char)((v >> 24) & 0xFFUL);
}

int mk_query_parse(const unsigned char *b, size_t n, MK_QUERY *q,
                   INST_U32 *need)
{
    INST_U32 bytes;
    INST_U32 rec;
    INST_U32 idOff;
    INST_U32 idBytes;
    INST_U32 recBytes;
    INST_U32 i;

    memset(q, 0, sizeof(*q));
    *need = 0;
    if (b == NULL || n < CM_Q_HDR_BYTES ||
        mk_get32(b, CM_Q_O_MAGIC) != CM_Q_MAGIC ||
        (mk_get32(b, CM_Q_O_VERSION) != CM_Q_VERSION &&
         mk_get32(b, CM_Q_O_VERSION) != CM_Q_VERSION_1)) {
        return 0;
    }
    bytes = mk_get32(b, CM_Q_O_BYTES);
    if (bytes < CM_Q_HDR_BYTES || bytes > CM_Q_MAX_BYTES) {
        return 0;
    }
    if (bytes > n) {
        *need = bytes;
        return -1;
    }
    rec = mk_get32(b, CM_Q_O_REC_OFFSET);
    /* Subtraction, not rec + CM_ST_HDR_BYTES: an offset near 0xFFFFFFFF
     * would wrap the sum past the check. */
    if (rec < CM_Q_HDR_BYTES || rec > bytes || CM_ST_HDR_BYTES > bytes - rec) {
        return 0;
    }
    recBytes = mk_get32(b, rec + CM_ST_O_BYTES);
    if (recBytes > bytes - rec || !inst_st_check(b + rec, recBytes)) {
        return 0;
    }
    idOff = mk_get32(b, CM_Q_O_ID_OFFSET);
    idBytes = mk_get32(b, CM_Q_O_ID_BYTES);
    if (idBytes == 0 || idBytes >= sizeof(q->Id) || idOff > bytes ||
        idBytes > bytes - idOff) {
        return 0;
    }
    for (i = 0; i < idBytes; i++) {
        if (b[idOff + i] < 0x20 || b[idOff + i] > 0x7E) {
            return 0;
        }
        q->Id[i] = (char)b[idOff + i];
    }
    q->Id[idBytes] = '\0';
    q->Bytes = bytes;
    q->Live = mk_get32(b, CM_Q_O_LIVE);
    q->Flags = mk_get32(b, CM_Q_O_FLAGS);
    q->EpochLo = mk_get32(b, CM_Q_O_EPOCH_LO);
    q->EpochHi = mk_get32(b, CM_Q_O_EPOCH_HI);
    q->Stage = mk_get32(b, rec + CM_ST_O_STAGE);
    q->Ports = mk_get32(b, rec + CM_ST_O_PORT_COUNT);
    if (q->Ports > 4) {
        q->Ports = 4;
    }
    q->Active = mk_get32(b, rec + CM_ST_O_PORT_ACTIVE);
    for (i = 0; i < q->Ports; i++) {
        q->Port[i] = mk_get32(b, rec + CM_ST_O_PORTS + 4 * i);
    }
    return 1;
}

/* The bridge inventory (cm_query.h CM_QB_*), read only when flagged and
 * laid out exactly: after the entries, its length CM_QB_BYTES, inside
 * the answer. Anything else leaves HasBridges 0 (every bridge unknown). */
static void mk_marks_bridges(const unsigned char *b, INST_U32 bytes,
                             INST_U32 entEnd, MK_MARKS *m)
{
    INST_U32 at;
    INST_U32 len;
    INST_U32 i;
    INST_U32 j;
    INST_U32 w;
    const unsigned char *s;

    if ((mk_get32(b, CM_QM_O_FLAGS) & CM_QM_F_BRIDGES) == 0) {
        return;
    }
    at = mk_get32(b, CM_QM_O_BR_OFFSET);
    len = mk_get32(b, CM_QM_O_BR_BYTES);
    if (len != CM_QB_BYTES || at < entEnd || at > bytes ||
        len > bytes - at) {
        return;
    }
    for (i = 0; i < MK_QB_MEMBERS; i++) {
        s = b + at + i * CM_QB_SEC_BYTES;
        w = mk_get32(s, 0);
        if (((w >> 24) & CM_QB_F_MEMBER) == 0) {
            continue;
        }
        m->Br[i].Valid = 1;
        m->Br[i].Member = w & 0xFFFFUL;
        m->Br[i].Count = (w >> 16) & 0xFFUL;
        m->Br[i].Complete = ((w >> 24) & CM_QB_F_COMPLETE) != 0;
        if (m->Br[i].Count > MK_QB_MAX) {
            m->Br[i].Count = MK_QB_MAX;
            m->Br[i].Complete = 0;
        }
        for (j = 0; j < m->Br[i].Count; j++) {
            m->Br[i].Loc[j] = mk_get32(s, 4 + 4 * j) & 0xFFFFUL;
        }
    }
    m->HasBridges = 1;
}

int mk_marks_parse(const unsigned char *b, size_t n, MK_MARKS *m,
                   INST_U32 *need)
{
    INST_U32 bytes;
    INST_U32 count;
    INST_U32 entBytes;
    INST_U32 off;
    INST_U32 i;
    const unsigned char *e;

    memset(m, 0, sizeof(*m));
    *need = 0;
    if (b == NULL || n < CM_QM_HDR_BYTES ||
        mk_get32(b, CM_QM_O_MAGIC) != CM_QM_MAGIC ||
        mk_get32(b, CM_QM_O_VERSION) != CM_QM_VERSION) {
        return 0;
    }
    bytes = mk_get32(b, CM_QM_O_BYTES);
    if (bytes < CM_QM_HDR_BYTES || bytes > CM_QM_MAX_BYTES) {
        return 0;
    }
    if (bytes > n) {
        *need = bytes;
        return -1;
    }
    count = mk_get32(b, CM_QM_O_COUNT);
    entBytes = mk_get32(b, CM_QM_O_ENT_BYTES);
    off = mk_get32(b, CM_QM_O_ENT_OFFSET);
    /* A division, not count * entBytes: an entry size of 0x01000000 or
     * more times 256 entries would wrap the product past the check. */
    if (count > MK_MAX_ENTRIES || entBytes < CM_QM_ENT_BYTES ||
        off < CM_QM_HDR_BYTES || off > bytes ||
        (count != 0 && entBytes > (bytes - off) / count)) {
        return 0;
    }
    m->Bytes = bytes;
    m->EpochLo = mk_get32(b, CM_QM_O_EPOCH_LO);
    m->EpochHi = mk_get32(b, CM_QM_O_EPOCH_HI);
    m->Gen = mk_get32(b, CM_QM_O_GEN);
    m->Flags = mk_get32(b, CM_QM_O_FLAGS);
    m->Dropped = mk_get32(b, CM_QM_O_DROPPED);
    m->Count = count;
    for (i = 0; i < count; i++) {
        e = b + off + i * entBytes;
        m->E[i].Member = mk_get32(e, CM_QM_E_MEMBER);
        m->E[i].Bus = mk_get32(e, CM_QM_E_BUS);
        m->E[i].Address = mk_get32(e, CM_QM_E_ADDRESS);
        m->E[i].Serial = mk_get32(e, CM_QM_E_SERIAL);
        m->E[i].Flags = mk_get32(e, CM_QM_E_FLAGS);
        m->E[i].Pending = mk_get32(e, CM_QM_E_PENDING);
        m->E[i].PendingGen = mk_get32(e, CM_QM_E_PENDING_GEN);
        m->E[i].Landing = 0;
        if (entBytes >= CM_QM_E_LANDING + 4 &&
            (m->E[i].Flags & MK_EF_CHAINED) != 0) {
            m->E[i].Landing = mk_get32(e, CM_QM_E_LANDING) & 0xFFFFUL;
        }
    }
    mk_marks_bridges(b, bytes, off + count * entBytes, m);
    return 1;
}

int mk_descends(const int *parent, int count, int node, int ancestor,
                int hops)
{
    int up;
    int k;

    if (parent == NULL || node < 0 || node >= count || ancestor < 0) {
        return 0;
    }
    up = parent[node];
    for (k = 0; k < hops && up >= 0 && up < count; k++) {
        if (up == ancestor) {
            return 1;
        }
        up = parent[up];
    }
    return 0;
}

int mk_verifiable(int devOk, const MK_QUERY *q, const MK_MARKS *m)
{
    if (!devOk || q == NULL || m == NULL || q->Bytes == 0 || m->Bytes == 0) {
        return 0;
    }
    if (q->Stage != CM_ST_STAGE_ACTIVE ||
        (q->Flags & (CM_Q_F_INERT | CM_Q_F_STOPPING)) != 0 ||
        (m->Flags & MK_F_STOPPING) != 0 || q->Live != 1UL /* CM_LIVE_ACTIVE */) {
        return 0;
    }
    return q->EpochLo == m->EpochLo && q->EpochHi == m->EpochHi;
}

int mk_member_active(const MK_QUERY *q, INST_U32 bdf)
{
    INST_U32 i;

    for (i = 0; i < q->Ports; i++) {
        if (q->Port[i] == bdf && (q->Active & (1UL << i)) != 0) {
            return 1;
        }
    }
    return 0;
}

const char *mk_w_text(int w)
{
    switch (w) {
    case MK_W_OK:           return "checks passed";
    case MK_W_NOT_LOCATED:  return "location not known yet";
    case MK_W_NOT_REPORTED: return "not reported by its bridge";
    case MK_W_SAFE_OFF:     return "safe eject is off for the domain";
    case MK_W_NOT_ACTIVE:   return "member is not an active tunnel port";
    case MK_W_NO_PORT:      return "no single devnode at the member's location";
    case MK_W_NO_UNIT:      return "no single child devnode of the port at the unit's location";
    case MK_W_PROTECTED:    return "protected device (NHI, NHI ancestor or host USB controller)";
    case MK_W_PRESENT:      return "mark already present";
    case MK_W_UNREADABLE:   return "its status or LowerFilters could not be read";
    default:                return "unknown";
    }
}

int mk_restart_wanted(const MK_ENTRY *e, int valuePresent, int winRecorded)
{
    INST_U32 att;

    if (!valuePresent || (e->Flags & MK_EF_REPORTED) == 0 ||
        (e->Flags & MK_EF_LOCATED) == 0 || (e->Flags & MK_EF_SAFE_OFF) != 0) {
        return 0;
    }
    att = e->Flags & (MK_EF_BUS_ATT | MK_EF_ADD_ATT);
    if (att == 0) {
        return 1;
    }
    if ((e->Flags & MK_EF_REMOVABLE) == 0) {
        return 1;
    }
    /* Answered, yet Windows recorded no Removable: the answer went to a
     * query Windows does not keep (record 21 section 7.7). */
    return winRecorded == 0;
}

#define MK_DEVCAP_REMOVABLE         0x00000004UL
#define MK_DEVCAP_SURPRISEREMOVALOK 0x00000080UL

int mk_win_recorded(int have, unsigned long caps)
{
    if (!have) {
        return -1;
    }
    if ((caps & MK_DEVCAP_REMOVABLE) != 0) {
        return 1;
    }
    if ((caps & MK_DEVCAP_SURPRISEREMOVALOK) != 0) {
        return -1;
    }
    return 0;
}

int mk_reconcile(int presence, int ownerKnown, int ownerVerif,
                 int anyUnverif, int anyVerif, int memberActive, int listed,
                 int incomplete, int safeOff)
{
    if (presence != MK_P_ABSENT && presence != MK_P_PRESENT) {
        return MK_R_LEAVE;
    }
    if (ownerKnown ? !ownerVerif : (!anyVerif || anyUnverif)) {
        return MK_R_LEAVE;
    }
    if (presence == MK_P_ABSENT) {
        return MK_R_REMOVE;
    }
    if (ownerKnown && (safeOff || !memberActive)) {
        return MK_R_REMOVE;
    }
    if (listed) {
        return MK_R_KEEP;
    }
    if (incomplete) {
        return MK_R_LEAVE;
    }
    return MK_R_REMOVE;
}

int mk_listed(const MK_MARKS *m, INST_U32 member, INST_U32 bus,
              INST_U32 address, const MK_ENTRY **entry)
{
    INST_U32 j;
    int unsure = 0;

    *entry = NULL;
    for (j = 0; j < m->Count; j++) {
        const MK_ENTRY *e = &m->E[j];
        if ((e->Flags & MK_EF_REPORTED) == 0 || e->Member != member) {
            continue;
        }
        if ((e->Flags & MK_EF_LOCATED) == 0) {
            unsure = 1;
            continue;
        }
        if (e->Bus == bus && e->Address == address) {
            *entry = e;
            return MK_L_LISTED;
        }
    }
    return unsure ? MK_L_UNSURE : MK_L_NOT;
}

size_t mk_msz_append(const char *in, size_t inBytes, char *out, size_t cap)
{
    size_t off = 0;
    size_t used = 0;
    size_t name = strlen(MK_NAME);

    while (in != NULL && off < inBytes && in[off] != '\0') {
        size_t n = 0;
        while (off + n < inBytes && in[off + n] != '\0') {
            n++;
        }
        if (used + n + 1 > cap) {
            return 0;
        }
        memcpy(out + used, in + off, n);
        used += n;
        out[used++] = '\0';
        off += n + 1;
    }
    if (used + name + 2 > cap) {
        return 0;
    }
    memcpy(out + used, MK_NAME, name);
    used += name;
    out[used++] = '\0';
    out[used++] = '\0';
    return used;
}

void mk_rec_format(char *out, size_t cap, INST_U32 member, INST_U32 epochHi,
                   INST_U32 epochLo, int state)
{
    char tmp[64];

    sprintf(tmp, "m=%04lX e=%08lX:%08lX s=%s", member & 0xFFFFUL, epochHi,
            epochLo, state == MK_REC_WRITTEN ? "written" : "writing");
    inst_copy(out, tmp, cap);
}

static int mk_hex(const char *s, int digits, INST_U32 *v)
{
    int i;
    INST_U32 r = 0;

    for (i = 0; i < digits; i++) {
        int c = inst_upper((unsigned char)s[i]);
        if (c >= '0' && c <= '9') {
            r = (r << 4) | (INST_U32)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            r = (r << 4) | (INST_U32)(c - 'A' + 10);
        } else {
            return 0;
        }
    }
    *v = r;
    return 1;
}

int mk_rec_parse(const char *text, INST_U32 *member, INST_U32 *epochHi,
                 INST_U32 *epochLo, int *state)
{
    if (text == NULL || strlen(text) < 30 || strncmp(text, "m=", 2) != 0 ||
        !mk_hex(text + 2, 4, member) || strncmp(text + 6, " e=", 3) != 0 ||
        !mk_hex(text + 9, 8, epochHi) || text[17] != ':' ||
        !mk_hex(text + 18, 8, epochLo) || strncmp(text + 26, " s=", 3) != 0) {
        return 0;
    }
    if (strncmp(text + 29, "written", 7) == 0) {
        *state = MK_REC_WRITTEN;
    } else if (strncmp(text + 29, "writing", 7) == 0) {
        *state = MK_REC_WRITING;
    } else {
        return 0;
    }
    return text[36] == '\0' || strcmp(text + 36, MK_REC_BRIDGE) == 0;
}

void mk_rec_format_bridge(char *out, size_t cap, INST_U32 member,
                          INST_U32 epochHi, INST_U32 epochLo, int state)
{
    char tmp[64];

    mk_rec_format(tmp, sizeof(tmp), member, epochHi, epochLo, state);
    strcat(tmp, MK_REC_BRIDGE);
    inst_copy(out, tmp, cap);
}

int mk_rec_is_bridge(const char *text)
{
    INST_U32 member;
    INST_U32 hi;
    INST_U32 lo;
    int state;

    return mk_rec_parse(text, &member, &hi, &lo, &state) &&
           text[36] != '\0';
}

int mk_bridge_attached(const MK_MARKS *m, INST_U32 member, INST_U32 bdf)
{
    int i;
    INST_U32 j;

    if (m == NULL || !m->HasBridges) {
        return MK_BA_UNKNOWN;
    }
    for (i = 0; i < MK_QB_MEMBERS; i++) {
        if (!m->Br[i].Valid || m->Br[i].Member != (member & 0xFFFFUL)) {
            continue;
        }
        for (j = 0; j < m->Br[i].Count; j++) {
            if (m->Br[i].Loc[j] == (bdf & 0xFFFFUL)) {
                return MK_BA_ATTACHED;
            }
        }
        return m->Br[i].Complete ? MK_BA_MISSING : MK_BA_UNKNOWN;
    }
    return MK_BA_UNKNOWN;
}

int mk_bridge_pending(int hasDriver, int flagsRead, INST_U32 flags)
{
    return !hasDriver || !flagsRead ||
           (flags & (MK_CONFIGFLAG_REINSTALL |
                     MK_CONFIGFLAG_FINISH_INSTALL)) != 0;
}

int mk_bridge_needs(int statusOk, int started, int hasProblem,
                    INST_U32 problem, int attached)
{
    if (!statusOk) {
        return 0;
    }
    if (hasProblem && problem == MK_PROB_NORMAL_CONFLICT) {
        return 1;
    }
    return started && !hasProblem && attached == MK_BA_MISSING;
}

int mk_bridge_gate_refuses(int statusOk, int started, int classRead,
                           const char *classGuid)
{
    if (!statusOk) {
        return 1;
    }
    if (!started) {
        return 0;
    }
    if (!classRead || classGuid == NULL) {
        return 1;
    }
    return inst_eq(classGuid, MK_CLASS_DISK) ||
           inst_eq(classGuid, MK_CLASS_CDROM) ||
           inst_eq(classGuid, MK_CLASS_VOLUME);
}

int mk_bridge_wait(int allMarked, INST_U32 sinceArrivalMs)
{
    return !allMarked && sinceArrivalMs < MK_BRIDGE_WAIT;
}

void mk_confirm_build(unsigned char *out, const MK_MARKS *m,
                      const MK_ENTRY *e)
{
    memset(out, 0, MK_QC_BYTES);
    mk_put32(out, CM_QC_O_EPOCH_LO, m->EpochLo);
    mk_put32(out, CM_QC_O_EPOCH_HI, m->EpochHi);
    mk_put32(out, CM_QC_O_MEMBER, e->Member);
    mk_put32(out, CM_QC_O_BUS, e->Bus);
    mk_put32(out, CM_QC_O_ADDRESS, e->Address);
    mk_put32(out, CM_QC_O_SERIAL, e->Serial);
    mk_put32(out, CM_QC_O_GEN, e->PendingGen);
}

INST_U32 mk_poll_ms(int anyAnswered)
{
    return anyAnswered ? 1000UL : 10000UL;
}

/* ======================================================================== */
/* Stor98 on a tunnelled drive (record 21 section 7.5)                      */
/* ======================================================================== */

static int mk_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* 1 when s[0..n) equals t[0..n), case ignored. */
static int mk_same_n(const char *s, const char *t, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (s[i] == '\0' || t[i] == '\0' || mk_lower(s[i]) != mk_lower(t[i])) {
            return 0;
        }
    }
    return 1;
}

int mk_s98_inf_ok(const char *inf, const char *windir)
{
    size_t w;
    size_t i;
    const char *p;

    if (inf == NULL || windir == NULL) {
        return 0;
    }
    w = strlen(windir);
    while (w > 0 && windir[w - 1] == '\\') {
        w--;
    }
    if (w == 0 || strlen(inf) <= w || !mk_same_n(inf, windir, w)) {
        return 0;
    }
    p = inf + w;
    if (!mk_same_n(p, "\\INF\\oem", 8)) {
        return 0;
    }
    p += 8;
    for (i = 0; p[i] >= '0' && p[i] <= '9'; i++) {
    }
    if (i == 0 || i > 5) {
        return 0;
    }
    p += i;
    return strlen(p) == 4 && mk_same_n(p, ".inf", 4);
}

static const char *const mkS98Text[] = {
    NULL, "disable", "install", "done", "busy", "failed", "enable", "restart",
    "give", "give-restart"
};

const char *mk_s98_rec_text(int state)
{
    if (state < MK_S98_DISABLE || state > MK_S98_GIVE_RESTART) {
        return NULL;
    }
    return mkS98Text[state];
}

int mk_s98_rec_parse(const char *text)
{
    int s;

    if (text == NULL) {
        return MK_S98_NONE;
    }
    for (s = MK_S98_DISABLE; s <= MK_S98_GIVE_RESTART; s++) {
        if (strcmp(text, mkS98Text[s]) == 0) {
            return s;
        }
    }
    return MK_S98_FAILED;
}

int mk_s98_retry(int record, int attempts, INST_U32 sinceMs)
{
    if (record == MK_S98_NONE) {
        return attempts == 0;
    }
    if (record != MK_S98_BUSY) {
        return 0;
    }
    return attempts == 0 ||
           (attempts < MK_S98_ATTEMPTS && sinceMs >= MK_S98_RETRY_MS);
}

int mk_s98_below(const int *parent, int n, int node, const int *member)
{
    int p;
    int steps;

    if (parent == NULL || member == NULL || node < 0 || node >= n) {
        return 0;
    }
    for (p = parent[node], steps = 0; p >= 0 && p < n && steps < 64;
         p = parent[p], steps++) {
        if (member[p]) {
            return 1;
        }
    }
    return 0;
}

int mk_s98_os(INST_U32 major, INST_U32 minor, INST_U32 build)
{
    if (major == 5UL || (major == 6UL && minor <= 2UL)) {
        return MK_S98_OS_LOW;
    }
    if (major == 6UL && minor == 3UL) {
        return build < 10000UL ? MK_S98_OS_81 : MK_S98_OS_10;
    }
    if ((major == 6UL && minor > 3UL) || major >= 10UL) {
        return MK_S98_OS_10;
    }
    return MK_S98_OS_NONE;
}

int mk_s98_os_both(int a, int b)
{
    return a == b ? a : MK_S98_OS_NONE;
}

int mk_s98_inbox(const char *service)
{
    return service != NULL && inst_eq(service, "stornvme");
}

int mk_s98_noid_why(int hasId, int portFilter, int depth)
{
    if (hasId) {
        return MK_S98_ID_HAS;
    }
    if (portFilter < 0) {
        return MK_S98_ID_UNKNOWN;
    }
    if (portFilter == 0) {
        return MK_S98_ID_NOFILTER;
    }
    if (depth > MK_S98_DEPTH_MAX - 1) {
        return MK_S98_ID_DEPTH;
    }
    return MK_S98_ID_EARLY;
}

const char *mk_s98_noid_text(int why)
{
    switch (why) {
    case MK_S98_ID_HAS:
        return "it carries the tunnel id; Windows put no driver or another one on it";
    case MK_S98_ID_NOFILTER:
        return "no id: its tunnel port carries no filter of this driver's";
    case MK_S98_ID_DEPTH:
        return "no id: below the filter chain's depth";
    case MK_S98_ID_EARLY:
        return "no id: the filter chain did not reach its first enumeration (a never-seen enclosure's bridges)";
    case MK_S98_ID_UNKNOWN:
        return "no id: its tunnel port's filters could not be read";
    case MK_S98_ID_OS:
        return "no id on Windows 10, by design";
    }
    return "?";
}

int mk_s98_displaces(const MK_S98_IN *in)
{
    return in->Os == MK_S98_OS_81 && mk_s98_inbox(in->Service);
}

int mk_s98_wanted(const MK_S98_IN *in)
{
    int none;

    if (!in->IsPci || !in->Nvme) {
        return MK_S98_W_CLASS;
    }
    if (in->Os != MK_S98_OS_LOW && in->Os != MK_S98_OS_81) {
        return MK_S98_W_OS;
    }
    if (in->Service == NULL) {
        return MK_S98_W_SERVICE;
    }
    none = in->Service[0] == '\0';
    if (!none && !mk_s98_displaces(in)) {
        return MK_S98_W_SERVICE;
    }
    if (!in->PolicyOk) {
        return MK_S98_W_POLICY;
    }
    if (!in->WalkWhole) {
        return MK_S98_W_WALK;
    }
    if (!in->Below) {
        return MK_S98_W_TUNNEL;
    }
    if (in->Os == MK_S98_OS_81 && !in->TunnelId) {
        return MK_S98_W_NOID;
    }
    if (in->Protected) {
        return MK_S98_W_PROTECTED;
    }
    if (!in->StatusOk) {
        return MK_S98_W_STATE;
    }
    if (none ? in->Started ||
                   (in->Problem && in->ProblemCode == MK_S98_PROB_DISABLED)
             : !in->Started || in->Problem) {
        return MK_S98_W_STATE;
    }
    if (!mk_s98_retry(in->Record, in->Attempts, in->SinceMs)) {
        return MK_S98_W_RECORD;
    }
    if (in->InstallPending) {
        return MK_S98_W_PENDING;
    }
    return MK_S98_GO;
}

const char *mk_s98_why_text(int why)
{
    switch (why) {
    case MK_S98_GO:
        return "to be given Stor98";
    case MK_S98_W_CLASS:
        return "not a PCI NVMe controller";
    case MK_S98_W_SERVICE:
        return "it has a driver of its own";
    case MK_S98_W_OS:
        return "not Windows 2000 to 8.1";
    case MK_S98_W_POLICY:
        return "no staged Stor98 named (Parameters\\Stor98Inf)";
    case MK_S98_W_WALK:
        return "the device tree walk was incomplete";
    case MK_S98_W_TUNNEL:
        return "not below a verified tunnel port";
    case MK_S98_W_PROTECTED:
        return "a protected device";
    case MK_S98_W_STATE:
        return "its state does not allow it (disabled, or on stornvme and not "
               "running)";
    case MK_S98_W_RECORD:
        return "its record allows no attempt now";
    case MK_S98_W_PENDING:
        return "a Windows installation is pending";
    case MK_S98_W_NOID:
        return "no tunnel id (Windows 8.1 matches Stor98 by it)";
    }
    return "?";
}

int mk_s98_driverless(int started, int problem, INST_U32 code)
{
    if (started) {
        return 0;
    }
    if (!problem) {
        return 1;
    }
    return code == 0x01UL || code == 0x1CUL || code == 0x12UL;
}

int mk_s98_rerun_flags(INST_U32 cfg, INST_U32 *out)
{
    *out = cfg;
    if ((cfg & MK_CONFIGFLAG_FAILEDINSTALL) == 0 &&
        (cfg & MK_CONFIGFLAG_REINSTALL) != 0) {
        return 0;
    }
    *out = (cfg & ~(INST_U32)MK_CONFIGFLAG_FAILEDINSTALL) |
           MK_CONFIGFLAG_REINSTALL;
    return 1;
}

int mk_s98_mark_sets(const MK_S98_MF *in)
{
    return in->Nt5 && in->PolicyOk && in->Below && in->Nvme && !in->HasId &&
           in->Service != NULL && in->Service[0] == '\0' && !in->Declined &&
           !in->Protected && in->StatusOk &&
           mk_s98_driverless(in->Started, in->Problem, in->ProblemCode);
}

int mk_s98_recover(int record, int present, int disabled, int s98Started,
                   int firstPoll)
{
    if (!present) {
        return MK_S98_R_NOTHING;
    }
    if (record == MK_S98_GIVE) {
        return s98Started ? MK_S98_R_DONE : MK_S98_R_FAIL;
    }
    if (record == MK_S98_DISABLE || record == MK_S98_INSTALL ||
        record == MK_S98_ENABLE) {
        if (disabled) {
            return MK_S98_R_ENABLE;
        }
    } else if ((record != MK_S98_RESTART && record != MK_S98_GIVE_RESTART) ||
               !firstPoll) {
        return MK_S98_R_NOTHING;
    }
    return s98Started ? MK_S98_R_DONE : MK_S98_R_FAIL;
}

/* ======================================================================== */
/* The self-test                                                            */
/* ======================================================================== */

static int mk_s98_parse_round(const char *text)
{
    return strcmp(mk_s98_rec_text(mk_s98_rec_parse(text)), text) == 0;
}

static int mkFails;
static void (*mkReport)(int ok, const char *what);

static void mk_check(int ok, const char *what)
{
    if (!ok) {
        mkFails++;
    }
    mkReport(ok, what);
}

/* A query answer with one record: stage, live, flags, epoch, one member. */
static size_t mk_fake_query(unsigned char *b, INST_U32 stage, INST_U32 live,
                            INST_U32 flags, INST_U32 epoch, INST_U32 port,
                            INST_U32 active)
{
    size_t rec = CM_Q_HDR_BYTES;
    size_t recBytes = CM_ST_HDR_BYTES;
    size_t id;
    const char *text = "PCI\\VEN_8086&DEV_15BF\\3&11583659&0&00";

    memset(b, 0, 1024);
    id = rec + recBytes;
    mk_put32(b, CM_Q_O_MAGIC, CM_Q_MAGIC);
    /* Version 1's layout, the record at 64 (no removal block). */
    mk_put32(b, CM_Q_O_VERSION, CM_Q_VERSION_1);
    mk_put32(b, CM_Q_O_BYTES, (INST_U32)(id + strlen(text)));
    mk_put32(b, CM_Q_O_LIVE, live);
    mk_put32(b, CM_Q_O_FLAGS, flags);
    mk_put32(b, CM_Q_O_REC_OFFSET, (INST_U32)rec);
    mk_put32(b, CM_Q_O_EPOCH_LO, epoch);
    mk_put32(b, CM_Q_O_EPOCH_HI, 1);
    mk_put32(b, CM_Q_O_ID_OFFSET, (INST_U32)id);
    mk_put32(b, CM_Q_O_ID_BYTES, (INST_U32)strlen(text));
    mk_put32(b + rec, CM_ST_O_MAGIC, CM_ST_MAGIC);
    mk_put32(b + rec, CM_ST_O_VERSION, CM_ST_VERSION);
    mk_put32(b + rec, CM_ST_O_BYTES, (INST_U32)recBytes);
    mk_put32(b + rec, CM_ST_O_STAGE, stage);
    mk_put32(b + rec, CM_ST_O_PORT_COUNT, 1);
    mk_put32(b + rec, CM_ST_O_PORT_ACTIVE, active);
    mk_put32(b + rec, CM_ST_O_PORTS, port);
    mk_put32(b + rec, CM_ST_O_ENTRY_BYTES, CM_ST_ENTRY_BYTES);
    memcpy(b + id, text, strlen(text));
    return id + strlen(text);
}

/* ---- a hot-added controller waiting for a restart (record 21 7.6) -------- */

#define MK_CTL_DN_HAS_PROBLEM   0x00000400UL

int mk_ctl_ready_why(const MK_CTL_IN *in)
{
    if (!in->Restart) {
        return MK_CTL_OFF;
    }
    if (!in->Nt6x86) {
        return MK_CTL_SYSTEM;
    }
    if (!in->Pci || in->Service == NULL || !inst_eq(in->Service, "xhci98") ||
        !in->XhciCompat) {
        return MK_CTL_NOT_OURS;
    }
    if (!in->DrvRead || in->InfSection == NULL || in->MatchingId == NULL ||
        !inst_eq(in->InfSection, MK_CTL_SECTION) ||
        !inst_eq(in->MatchingId, MK_CTL_MATCH)) {
        return MK_CTL_DRIVER_KEY;
    }
    if (!in->FlagsRead ||
        (in->Flags & (MK_CONFIGFLAG_REINSTALL | MK_CONFIGFLAG_FAILEDINSTALL |
                      MK_CONFIGFLAG_FINISH_INSTALL)) != 0) {
        return MK_CTL_FLAGS;
    }
    if (!in->StatusOk || (in->Status & MK_CTL_DN_HAS_PROBLEM) == 0 ||
        (in->Status & MK_DN_NEED_RESTART) == 0 ||
        in->Problem != MK_PROB_NOT_CONFIGURED) {
        return MK_CTL_NOT_WAITING;
    }
    if (!in->Behind) {
        return MK_CTL_NOT_BEHIND;
    }
    if (in->Protected) {
        return MK_CTL_PROTECTED;
    }
    if (!in->HelperRunning) {
        return MK_CTL_HELPER;
    }
    if (!in->InstallIdle) {
        return MK_CTL_INSTALLING;
    }
    if (in->Tried) {
        return MK_CTL_TRIED;
    }
    return MK_CTL_GO;
}

const char *mk_ctl_why_text(int why)
{
    switch (why) {
    case MK_CTL_GO:          return "to be started";
    case MK_CTL_OFF:         return "the service's Restart value is 0";
    case MK_CTL_SYSTEM:      return "not 32-bit Windows Vista or 7";
    case MK_CTL_NOT_OURS:    return "not an xHCI98 controller";
    case MK_CTL_DRIVER_KEY:  return "not installed by xhci98.inf's Xhci.Dev6, or its driver key unread";
    case MK_CTL_FLAGS:       return "its install is not finished (or its flags unread)";
    case MK_CTL_NOT_WAITING: return "not waiting for a restart (problem 1 with DN_NEED_RESTART)";
    case MK_CTL_NOT_BEHIND:  return "not behind a verified tunnel port";
    case MK_CTL_PROTECTED:   return "a protected device";
    case MK_CTL_HELPER:      return "the helper xhcihlpr is not running";
    case MK_CTL_INSTALLING:  return "an installation is pending (or cannot be told)";
    case MK_CTL_TRIED:       return "started once this arrival already";
    default:                 return "?";
    }
}

int mk_flush_wants(INST_U32 event)
{
    return event == MK_PBT_APMQUERYSUSPEND || event == MK_PBT_APMSUSPEND;
}

int mk_flush_behind(const INST_U32 *chain, int n, const INST_U32 *members, int m)
{
    int i;
    int j;

    for (i = 0; i < n; i++) {
        for (j = 0; j < m; j++) {
            if (chain[i] == members[j]) {
                return 1;
            }
        }
    }
    return 0;
}

/* ---- the location kept per instance (record 21 section 7.8.3) ----------- */

#define MK_PBT_APMRESUMECRITICAL    0x0006UL
#define MK_PBT_APMRESUMESUSPEND     0x0007UL
#define MK_PBT_APMRESUMEAUTOMATIC   0x0012UL

void mk_loc_reset(MK_LOCS *c)
{
    c->Count = 0;
    c->Cursor = 0;
}

/* The walk reaches the devnodes in the same order at every poll, so the
 * search begins after the last one found. */
static int mk_loc_find(MK_LOCS *c, const char *id)
{
    int k;
    int i;

    for (k = 0; k < c->Count; k++) {
        i = (c->Cursor + k) % c->Count;
        if (inst_eq(c->E[i].Id, id)) {
            c->Cursor = i + 1 < c->Count ? i + 1 : 0;
            return i;
        }
    }
    return -1;
}

static int mk_loc_due(INST_U32 now, INST_U32 at)
{
    return (INST_U32)(now - at) < 0x80000000UL;
}

int mk_loc_get(MK_LOCS *c, const char *id, int started, INST_U32 now,
               INST_U32 *bus, INST_U32 *address)
{
    int i = mk_loc_find(c, id);
    MK_LOC *e = i >= 0 ? &c->E[i] : NULL;

    if (e != NULL) {
        e->Seen = 1;
        if (!started) {
            e->Stale = 1;
        }
    }
    if (started && (e == NULL || e->Stale ||
                    (e->Failed && mk_loc_due(now, e->RetryAt)))) {
        return MK_LOC_READ;
    }
    if (e == NULL || !e->Has) {
        return MK_LOC_NONE;
    }
    *bus = e->Bus;
    *address = e->Address;
    return MK_LOC_KEPT;
}

void mk_loc_put(MK_LOCS *c, const char *id, int result, INST_U32 bus,
                INST_U32 address, INST_U32 now)
{
    int i = mk_loc_find(c, id);
    MK_LOC *e;

    if (i < 0) {
        if (c->Count >= MK_LOC_MAX) {
            return;
        }
        i = c->Count++;
        inst_copy(c->E[i].Id, id, sizeof(c->E[i].Id));
    }
    e = &c->E[i];
    e->Has = result == MK_LOC_R_OK;
    e->Bus = e->Has ? bus : 0;
    e->Address = e->Has ? address : 0;
    e->Failed = result == MK_LOC_R_FAILED;
    e->RetryAt = now + MK_LOC_RETRY_MS;
    e->Stale = 0;
    e->Seen = 1;
}

void mk_loc_walk_begin(MK_LOCS *c)
{
    int i;

    for (i = 0; i < c->Count; i++) {
        c->E[i].Seen = 0;
    }
}

void mk_loc_walk_end(MK_LOCS *c, int whole)
{
    int i;
    int n = 0;

    if (!whole) {
        return;
    }
    for (i = 0; i < c->Count; i++) {
        if (c->E[i].Seen) {
            if (n != i) {
                c->E[n] = c->E[i];
            }
            n++;
        }
    }
    c->Count = n;
    c->Cursor = 0;
}

void mk_loc_stale_all(MK_LOCS *c)
{
    int i;

    for (i = 0; i < c->Count; i++) {
        c->E[i].Stale = 1;
    }
}

int mk_loc_resume_event(INST_U32 event)
{
    return event == MK_PBT_APMRESUMECRITICAL ||
           event == MK_PBT_APMRESUMESUSPEND ||
           event == MK_PBT_APMRESUMEAUTOMATIC;
}

/* ---- the installer's hold (record 21 section 7.8.4) ---------------------- */

int mk_hold_step(MK_HOLD *h, int exists, INST_U32 gen, INST_U32 now,
                 int *event)
{
    *event = MK_HOLD_EV_NONE;
    h->AckDue = 0;
    if (!exists) {
        if (h->Held) {
            *event = MK_HOLD_EV_ENDED;
        }
        h->Held = 0;
        h->Expired = 0;
        return 1;
    }
    if (!h->Held || gen != h->Gen) {
        h->Held = 1;
        h->Expired = 0;
        h->Since = now;
        h->Gen = gen;
        *event = MK_HOLD_EV_BEGAN;
    } else if (h->Expired) {
        return 1;
    } else if ((INST_U32)(now - h->Since) >= MK_HOLD_MAX_MS) {
        h->Expired = 1;
        *event = MK_HOLD_EV_EXPIRED;
        return 1;
    }
    h->AckDue = h->AckedGen != h->Gen;
    return 0;
}

static MK_LOCS mkTestLocs;

int mk_selftest(void (*report)(int ok, const char *what))
{
    unsigned char b[1024];
    unsigned char c[MK_QC_BYTES];
    MK_QUERY q;
    MK_MARKS m;
    MK_ENTRY e;
    INST_U32 need;
    INST_U32 member;
    INST_U32 hi;
    INST_U32 lo;
    int state;
    size_t n;
    char out[64];
    char rec[64];

    mkFails = 0;
    mkReport = report;

    n = mk_fake_query(b, CM_ST_STAGE_ACTIVE, 1, 0, 0x1234, 0x0518, 1);
    mk_check(mk_query_parse(b, n, &q, &need) == 1, "a whole query answer parses");
    mk_check(q.Ports == 1 && q.Port[0] == 0x0518 && q.EpochLo == 0x1234,
             "its member and epoch");
    mk_check(strcmp(q.Id, "PCI\\VEN_8086&DEV_15BF\\3&11583659&0&00") == 0,
             "its instance id");
    mk_check(mk_member_active(&q, 0x0518) && !mk_member_active(&q, 0x0519),
             "the active member, and no other");
    mk_check(mk_query_parse(b, CM_Q_HDR_BYTES, &q, &need) == -1 && need == n,
             "a header alone asks for the whole");
    b[0] ^= 1;
    mk_check(mk_query_parse(b, n, &q, &need) == 0, "a bad magic is refused");

    memset(b, 0, sizeof(b));
    mk_put32(b, CM_QM_O_MAGIC, CM_QM_MAGIC);
    mk_put32(b, CM_QM_O_VERSION, CM_QM_VERSION);
    mk_put32(b, CM_QM_O_BYTES, CM_QM_HDR_BYTES + CM_QM_ENT_BYTES);
    mk_put32(b, CM_QM_O_EPOCH_LO, 0x1234);
    mk_put32(b, CM_QM_O_EPOCH_HI, 1);
    mk_put32(b, CM_QM_O_GEN, 7);
    mk_put32(b, CM_QM_O_COUNT, 1);
    mk_put32(b, CM_QM_O_ENT_BYTES, CM_QM_ENT_BYTES);
    mk_put32(b, CM_QM_O_ENT_OFFSET, CM_QM_HDR_BYTES);
    mk_put32(b, CM_QM_HDR_BYTES + CM_QM_E_MEMBER, 0x0518);
    mk_put32(b, CM_QM_HDR_BYTES + CM_QM_E_BUS, 6);
    mk_put32(b, CM_QM_HDR_BYTES + CM_QM_E_SERIAL, 3);
    mk_put32(b, CM_QM_HDR_BYTES + CM_QM_E_FLAGS, MK_EF_LOCATED | MK_EF_REPORTED);
    mk_check(mk_marks_parse(b, CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, &m, &need) == 1,
             "a marks answer parses");
    mk_check(m.Count == 1 && m.E[0].Bus == 6 && m.E[0].Serial == 3 && m.Gen == 7,
             "its entry");
    mk_check(m.E[0].Landing == 0, "a member's own entry: no landing");
    /* Design record 34 section 3.12 item 13: a chained entry's landing. */
    mk_put32(b, CM_QM_HDR_BYTES + CM_QM_E_FLAGS,
             MK_EF_LOCATED | MK_EF_REPORTED | MK_EF_CHAINED);
    mk_put32(b, CM_QM_HDR_BYTES + CM_QM_E_LANDING, 0x10529UL);
    mk_check(mk_marks_parse(b, CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, &m, &need) == 1 &&
                 m.E[0].Landing == 0x0529UL, "a chained entry's landing");
    mk_put32(b, CM_QM_HDR_BYTES + CM_QM_E_FLAGS, MK_EF_LOCATED | MK_EF_REPORTED);
    mk_check(mk_marks_parse(b, CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, &m, &need) == 1 &&
                 m.E[0].Landing == 0, "the landing read only with the flag");
    mk_put32(b, CM_QM_HDR_BYTES + CM_QM_E_LANDING, 0);
    {
        /* 0 the member's port, 1 the dock's upstream port, 2 its chain
         * port, 3 the chained unit; 4 another port's child. */
        static const int par[5] = { -1, 0, 1, 2, -1 };

        mk_check(mk_descends(par, 5, 2, 0, 8), "the chain port below its member");
        mk_check(mk_descends(par, 5, 3, 2, 8), "the unit below the chain port");
        mk_check(!mk_descends(par, 5, 4, 0, 8), "another root's node: not below");
        mk_check(!mk_descends(par, 5, 2, 0, 1), "past the hops allowed: not");
        mk_check(!mk_descends(par, 5, 0, 0, 8), "a node is not below itself");
    }
    mk_put32(b, CM_QM_O_COUNT, 2);
    mk_check(mk_marks_parse(b, CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, &m, &need) == 0,
             "a count past the size is refused");
    /* count * entBytes wraps 32 bits to a small product: refused. */
    mk_put32(b, CM_QM_O_COUNT, 256);
    mk_put32(b, CM_QM_O_ENT_BYTES, 0x01000000UL);
    mk_check(mk_marks_parse(b, CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, &m, &need) == 0,
             "an entry size whose product with the count wraps is refused");
    mk_put32(b, CM_QM_O_ENT_BYTES, CM_QM_ENT_BYTES);
    mk_put32(b, CM_QM_O_COUNT, 1);
    (void)mk_marks_parse(b, CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, &m, &need);

    /* A record offset whose sum with the record's header wraps: refused. */
    n = mk_fake_query(b, CM_ST_STAGE_ACTIVE, 1, 0, 0x1234, 0x0518, 1);
    mk_put32(b, CM_Q_O_REC_OFFSET, 0xFFFFFFF0UL);
    mk_check(mk_query_parse(b, n, &q, &need) == 0,
             "a record offset that wraps the bound is refused");

    n = mk_fake_query(b, CM_ST_STAGE_ACTIVE, 1, 0, 0x1234, 0x0518, 1);
    (void)mk_query_parse(b, n, &q, &need);
    mk_check(mk_verifiable(1, &q, &m), "active, same epoch: verifiable");
    mk_check(!mk_verifiable(0, &q, &m), "the devnode not started: not");
    m.EpochLo = 0x1235;
    mk_check(!mk_verifiable(1, &q, &m), "an epoch changed between the reads: not");
    m.EpochLo = 0x1234;
    n = mk_fake_query(b, CM_ST_STAGE_ACTIVE, 7, 0, 0x1234, 0x0518, 1);
    (void)mk_query_parse(b, n, &q, &need);
    mk_check(!mk_verifiable(1, &q, &m), "contained: not");
    n = mk_fake_query(b, CM_ST_STAGE_INERT, 1, 0, 0x1234, 0x0518, 1);
    (void)mk_query_parse(b, n, &q, &need);
    mk_check(!mk_verifiable(1, &q, &m), "inert: not");
    n = mk_fake_query(b, CM_ST_STAGE_ACTIVE, 1, CM_Q_F_STOPPING, 0x1234, 0x0518, 1);
    (void)mk_query_parse(b, n, &q, &need);
    mk_check(!mk_verifiable(1, &q, &m), "stopping: not");

    memset(&e, 0, sizeof(e));
    e.Flags = MK_EF_LOCATED | MK_EF_REPORTED;
    mk_check(mk_restart_wanted(&e, 1, -1), "no attachment with the value: restart");
    mk_check(!mk_restart_wanted(&e, 0, -1), "no value: none");
    mk_check(!mk_restart_wanted(&e, 0, 0), "no value, not recorded: none");
    e.Flags |= MK_EF_ADD_ATT;
    mk_check(mk_restart_wanted(&e, 1, -1), "attached, Removable not answered: restart");
    mk_check(mk_restart_wanted(&e, 1, 1),
             "attached, not answered, recorded: restart as before");
    e.Flags |= MK_EF_REMOVABLE;
    mk_check(!mk_restart_wanted(&e, 1, -1), "attached and answered: none");
    mk_check(!mk_restart_wanted(&e, 1, 1), "answered and recorded: none");
    mk_check(mk_restart_wanted(&e, 1, 0),
             "answered, Windows recorded no Removable: restart (45.62)");
    e.Flags |= MK_EF_SAFE_OFF;
    mk_check(!mk_restart_wanted(&e, 1, 0), "safe eject off, not recorded: none");
    e.Flags = MK_EF_LOCATED | MK_EF_BUS_ATT | MK_EF_REMOVABLE;
    mk_check(!mk_restart_wanted(&e, 1, 0), "not reported, not recorded: none");
    mk_check(mk_win_recorded(0, 0x4) == -1, "capabilities not read: -1");
    mk_check(mk_win_recorded(1, 0x4) == 1, "Removable recorded: 1");
    mk_check(mk_win_recorded(1, 0x84) == 1, "Removable and SurpriseRemovalOK: 1");
    mk_check(mk_win_recorded(1, 0x80) == -1, "SurpriseRemovalOK alone: -1");
    mk_check(mk_win_recorded(1, 0x0) == 0, "neither: 0");
    mk_check(mk_win_recorded(1, 0x3) == 0, "lock and eject bits alone: 0");
    e.Flags = MK_EF_LOCATED | MK_EF_REPORTED;
    e.Pending = 5;
    mk_check(mk_restart_wanted(&e, 1, -1),
             "a departure pending (a first plug's install) does not stop it");
    e.Pending = 0;
    e.Flags |= MK_EF_SAFE_OFF;
    mk_check(!mk_restart_wanted(&e, 1, -1), "safe eject off: none");

    mk_check(mk_reconcile(MK_P_ABSENT, 1, 1, 0, 1, 1, 0, 0, 0) == MK_R_REMOVE,
             "absent, its domain verifiable: removed");
    mk_check(mk_reconcile(MK_P_ABSENT, 1, 0, 1, 0, 1, 0, 0, 0) == MK_R_LEAVE,
             "absent, its domain not verifiable (the NHI stopped): left alone");
    mk_check(mk_reconcile(MK_P_ABSENT, 0, 0, 0, 1, 0, 0, 0, 0) == MK_R_REMOVE,
             "absent, no owner listed, every NHI verifiable: removed");
    mk_check(mk_reconcile(MK_P_ABSENT, 0, 0, 1, 1, 0, 0, 0, 0) == MK_R_LEAVE,
             "absent, no owner listed, an NHI not verifiable: left alone");
    mk_check(mk_reconcile(MK_P_ABSENT, 0, 0, 0, 0, 0, 0, 0, 0) == MK_R_LEAVE,
             "absent with no NHI at all (the driver not running): left alone");
    mk_check(mk_reconcile(MK_P_UNKNOWN, 1, 1, 0, 1, 1, 0, 0, 0) == MK_R_LEAVE,
             "a lookup that failed is never an absence");
    mk_check(mk_reconcile(MK_P_PRESENT, 1, 1, 0, 1, 1, 1, 0, 0) == MK_R_KEEP, "listed: kept");
    mk_check(mk_reconcile(MK_P_PRESENT, 1, 1, 0, 1, 1, 1, 0, 1) == MK_R_REMOVE,
             "listed, safe eject off: removed");
    mk_check(mk_reconcile(MK_P_PRESENT, 1, 0, 1, 1, 1, 0, 0, 0) == MK_R_LEAVE,
             "its domain not verifiable: left alone");
    mk_check(mk_reconcile(MK_P_PRESENT, 1, 1, 0, 1, 1, 0, 1, 0) == MK_R_LEAVE,
             "an incomplete answer: left alone");
    mk_check(mk_reconcile(MK_P_PRESENT, 1, 1, 0, 1, 0, 0, 1, 0) == MK_R_REMOVE,
             "incomplete, but the member is no longer active: removed");
    mk_check(mk_reconcile(MK_P_PRESENT, 1, 1, 0, 1, 1, 0, 1, 1) == MK_R_REMOVE,
             "incomplete, but safe eject is off: removed");
    mk_check(mk_reconcile(MK_P_PRESENT, 1, 1, 1, 1, 1, 0, 0, 0) == MK_R_REMOVE,
             "its domain verifiable and complete, not listed: removed (another domain's state aside)");
    mk_check(mk_reconcile(MK_P_PRESENT, 0, 0, 0, 1, 0, 0, 0, 0) == MK_R_REMOVE,
             "behind no member, every NHI verifiable: removed");
    /* Windows 2000, 2026-10-19: a never-seen chain's downstream port and
     * NVMe took xhcicm from a CriticalDeviceDatabase copy, with no record
     * (the inventory records them to be judged): neither is a unit behind
     * an active member, so each is removed once every NHI is verifiable. */
    mk_check(mk_reconcile(MK_P_PRESENT, 0, 0, 0, 1, 0, 0, 0, 0) == MK_R_REMOVE,
             "an unrecorded mark below a unit (a CDDB copy): removed");
    mk_check(mk_reconcile(MK_P_PRESENT, 1, 1, 0, 1, 1, 0, 0, 0) == MK_R_REMOVE,
             "an unrecorded mark on a device behind the member that the driver does not report: removed");

    /* The 7 x86 reading of 2026-10-19 (p42-tr's leg): a unit at Code 28
     * behind an active member, its entry reported with no location known
     * (no attachment of the driver left to read it), was read as gone and
     * its mark removed a second after it was written. An entry behind the
     * member that cannot be placed is no proof of absence. */
    {
        MK_MARKS lm;
        const MK_ENTRY *le;

        memset(&lm, 0, sizeof(lm));
        lm.Count = 2;
        lm.E[0].Member = 0x0028;
        lm.E[0].Flags = MK_EF_REPORTED;
        lm.E[0].Serial = 1;
        lm.E[1].Member = 0x0030;
        lm.E[1].Flags = MK_EF_REPORTED | MK_EF_LOCATED;
        lm.E[1].Bus = 2;
        mk_check(mk_listed(&lm, 0x0028, 1, 0x00010000UL, &le) == MK_L_UNSURE &&
                 le == NULL, "an unlocated entry behind the member: unsure");
        mk_check(mk_reconcile(MK_P_PRESENT, 1, 1, 0, 1, 1, 0, 1, 0) == MK_R_LEAVE,
                 "and unsure is left alone, as an incomplete answer is");
        mk_check(mk_listed(&lm, 0x0030, 1, 0, &le) == MK_L_NOT,
                 "another member's located unit elsewhere: not listed");
        lm.E[0].Flags |= MK_EF_LOCATED;
        lm.E[0].Bus = 1;
        lm.E[0].Address = 0x00010000UL;
        mk_check(mk_listed(&lm, 0x0028, 1, 0x00010000UL, &le) == MK_L_LISTED &&
                 le == &lm.E[0], "located: listed");
        lm.E[0].Flags &= ~MK_EF_REPORTED;
        mk_check(mk_listed(&lm, 0x0028, 1, 0x00010000UL, &le) == MK_L_NOT,
                 "a tombstone is not listed");
    }

    n = mk_msz_append(NULL, 0, out, sizeof(out));
    mk_check(n == 8 && memcmp(out, "xhcicm\0\0", 8) == 0, "an absent value becomes the one name");
    n = mk_msz_append("\0", 1, out, sizeof(out));
    mk_check(n == 8 && memcmp(out, "xhcicm\0\0", 8) == 0, "an empty value too");
    n = mk_msz_append("other\0second\0\0", 14, out, sizeof(out));
    mk_check(n == 21 && memcmp(out, "other\0second\0xhcicm\0\0", 21) == 0,
             "others kept in order, the name appended last");
    mk_check(mk_msz_append("other\0\0", 7, out, 10) == 0, "too small: refused");
    mk_check(inst_msz_has(out, n, "xhcicm") && inst_msz_has(out, n, "XHCICM"),
             "the name found, case ignored");

    /* The bridge marks (design record 33 section 4.3, task 45.12). */
    {
        static unsigned char ab[CM_QM_MAX_BYTES];
        static MK_MARKS mm;
        size_t at;
        size_t k;

        memset(ab, 0, sizeof(ab));
        mk_put32(ab, CM_QM_O_MAGIC, CM_QM_MAGIC);
        mk_put32(ab, CM_QM_O_VERSION, CM_QM_VERSION);
        mk_put32(ab, CM_QM_O_ENT_BYTES, CM_QM_ENT_BYTES);
        mk_put32(ab, CM_QM_O_ENT_OFFSET, CM_QM_HDR_BYTES);
        mk_put32(ab, CM_QM_O_COUNT, 1);
        at = CM_QM_HDR_BYTES + CM_QM_ENT_BYTES;
        mk_put32(ab, CM_QM_O_BYTES, (INST_U32)at);
        mk_check(mk_marks_parse(ab, at, &mm, &need) == 1 && !mm.HasBridges &&
                 mk_bridge_attached(&mm, 0x0218, 0x0300) == MK_BA_UNKNOWN,
                 "a driver without the inventory: every bridge unknown");
        /* Member 02:03.0 complete with 03:00.0 and 04:01.0; member
         * 02:04.0 incomplete with 05:00.0. */
        mk_put32(ab, at, 0x0218UL | (2UL << 16) |
                             ((CM_QB_F_MEMBER | CM_QB_F_COMPLETE) << 24));
        mk_put32(ab, at + 4, 0x0300);
        mk_put32(ab, at + 8, 0x0408);
        mk_put32(ab, at + CM_QB_SEC_BYTES, 0x0220UL | (1UL << 16) |
                                               (CM_QB_F_MEMBER << 24));
        mk_put32(ab, at + CM_QB_SEC_BYTES + 4, 0x0500);
        mk_put32(ab, CM_QM_O_BR_OFFSET, (INST_U32)at);
        mk_put32(ab, CM_QM_O_BR_BYTES, CM_QB_BYTES);
        mk_put32(ab, CM_QM_O_FLAGS, CM_QM_F_BRIDGES);
        mk_put32(ab, CM_QM_O_BYTES, (INST_U32)(at + CM_QB_BYTES));
        mk_check(mk_marks_parse(ab, at + CM_QB_BYTES, &mm, &need) == 1 &&
                 mm.HasBridges && mm.Count == 1,
                 "the inventory parses beside the entries");
        mk_check(mk_bridge_attached(&mm, 0x0218, 0x0408) == MK_BA_ATTACHED,
                 "a nested bridge listed: attached (its child never answers "
                 "Removable)");
        mk_check(mk_bridge_attached(&mm, 0x0218, 0x0410) == MK_BA_MISSING,
                 "a complete list without it: missing");
        mk_check(mk_bridge_attached(&mm, 0x0220, 0x0500) == MK_BA_ATTACHED &&
                 mk_bridge_attached(&mm, 0x0220, 0x0508) == MK_BA_UNKNOWN,
                 "an incomplete list: listed attached, the rest unknown");
        mk_check(mk_bridge_attached(&mm, 0x0228, 0x0600) == MK_BA_UNKNOWN,
                 "no section for the member: unknown");
        /* A section of CM_QB_MAX_BRIDGES flagged incomplete. */
        mk_put32(ab, at, 0x0218UL | (16UL << 16) | (CM_QB_F_MEMBER << 24));
        for (k = 0; k < 16; k++) {
            mk_put32(ab, at + 4 + 4 * k, (INST_U32)(0x0300 + 8 * k));
        }
        mk_check(mk_marks_parse(ab, at + CM_QB_BYTES, &mm, &need) == 1 &&
                 mk_bridge_attached(&mm, 0x0218, 0x0FF0) == MK_BA_UNKNOWN,
                 "a full section flagged incomplete: unknown");
        mk_put32(ab, CM_QM_O_BR_BYTES, CM_QB_BYTES - 4);
        mk_check(mk_marks_parse(ab, at + CM_QB_BYTES, &mm, &need) == 1 &&
                 !mm.HasBridges, "an inventory of another length: ignored");
        mk_put32(ab, CM_QM_O_BR_BYTES, CM_QB_BYTES);
        mk_put32(ab, CM_QM_O_BR_OFFSET, CM_QM_HDR_BYTES);
        mk_check(mk_marks_parse(ab, at + CM_QB_BYTES, &mm, &need) == 1 &&
                 !mm.HasBridges, "an inventory over the entries: ignored");
        mk_check(mk_bridge_pending(1, 1, 0) == 0 &&
                 mk_bridge_pending(0, 1, 0) == 1 &&
                 mk_bridge_pending(1, 0, 0) == 1 &&
                 mk_bridge_pending(1, 1, MK_CONFIGFLAG_REINSTALL) == 1 &&
                 mk_bridge_pending(1, 1, MK_CONFIGFLAG_FINISH_INSTALL) == 1 &&
                 mk_bridge_pending(1, 1, 0x40) == 0,
                 "a bridge's install: done only with a driver and no install "
                 "flag");
        mk_check(mk_bridge_needs(1, 0, 1, 12, MK_BA_UNKNOWN) == 1 &&
                 mk_bridge_needs(1, 1, 0, 0, MK_BA_MISSING) == 1 &&
                 mk_bridge_needs(1, 1, 0, 0, MK_BA_ATTACHED) == 0 &&
                 mk_bridge_needs(1, 1, 0, 0, MK_BA_UNKNOWN) == 0 &&
                 mk_bridge_needs(0, 0, 1, 12, MK_BA_MISSING) == 0 &&
                 mk_bridge_needs(1, 0, 1, 28, MK_BA_MISSING) == 0,
                 "the restart's trigger: problem 12, or placed and missing");
        mk_check(mk_bridge_gate_refuses(1, 1, 1, MK_CLASS_DISK) == 1 &&
                 mk_bridge_gate_refuses(1, 1, 1,
                     "{71a27cdd-812a-11d0-bec7-08002be2092f}") == 1 &&
                 mk_bridge_gate_refuses(1, 0, 1, MK_CLASS_DISK) == 0 &&
                 mk_bridge_gate_refuses(1, 1, 1,
                     "{36FC9E60-C465-11CF-8056-444553540000}") == 0 &&
                 mk_bridge_gate_refuses(1, 1, 1,
                     "{4D36E972-E325-11CE-BFC1-08002BE10318}") == 0 &&
                 mk_bridge_gate_refuses(1, 1, 0, NULL) == 1 &&
                 mk_bridge_gate_refuses(0, 0, 1, "") == 1,
                 "the gate: a started disk or volume refuses, a started xHCI "
                 "or NIC does not");
        mk_check(mk_bridge_wait(0, 0) && mk_bridge_wait(0, 59999) &&
                 !mk_bridge_wait(0, 60000) && !mk_bridge_wait(1, 0),
                 "the other restarts wait for the bridge marks, 60 s at most");
    }

    mk_rec_format(rec, sizeof(rec), 0x0518, 0x01D9ABCD, 0x12345678, MK_REC_WRITING);
    mk_check(strcmp(rec, "m=0518 e=01D9ABCD:12345678 s=writing") == 0, "the record's form");
    mk_check(mk_rec_parse(rec, &member, &hi, &lo, &state) && member == 0x0518 &&
             hi == 0x01D9ABCD && lo == 0x12345678 && state == MK_REC_WRITING,
             "and back");
    mk_rec_format(rec, sizeof(rec), 0x0518, 1, 2, MK_REC_WRITTEN);
    mk_check(mk_rec_parse(rec, &member, &hi, &lo, &state) && state == MK_REC_WRITTEN,
             "written");
    mk_check(!mk_rec_parse("m=0518 e=1:2 s=written", &member, &hi, &lo, &state),
             "a malformed record refused");
    mk_check(!mk_rec_is_bridge(rec), "a unit's record is no bridge mark's");
    mk_rec_format_bridge(rec, sizeof(rec), 0x0518, 1, 2, MK_REC_WRITING);
    mk_check(strcmp(rec, "m=0518 e=00000001:00000002 s=writing k=bridge") == 0 &&
             mk_rec_parse(rec, &member, &hi, &lo, &state) &&
             state == MK_REC_WRITING && member == 0x0518 &&
             mk_rec_is_bridge(rec),
             "a bridge mark's record: the same form and its suffix");
    mk_check(!mk_rec_parse("m=0518 e=00000001:00000002 s=writtenX", &member,
                           &hi, &lo, &state) &&
             !mk_rec_parse("m=0518 e=00000001:00000002 s=written k=other",
                           &member, &hi, &lo, &state),
             "another suffix refused");

    m.EpochLo = 0x1234;
    m.EpochHi = 1;
    e.Member = 0x0518;
    e.Bus = 6;
    e.Address = 0x00010000UL;
    e.Serial = 3;
    e.PendingGen = 9;
    mk_confirm_build(c, &m, &e);
    mk_check(mk_get32(c, CM_QC_O_EPOCH_LO) == 0x1234 && mk_get32(c, CM_QC_O_SERIAL) == 3 &&
             mk_get32(c, CM_QC_O_ADDRESS) == 0x00010000UL && mk_get32(c, CM_QC_O_GEN) == 9,
             "the confirmation's layout");
    mk_check(mk_poll_ms(1) == 1000 && mk_poll_ms(0) == 10000, "the cadence");
    /* Record 23 section 5.14: xhcicm's start type put back at each poll. */
    mk_check(inst_cm_start_fix(1, INST_REG_DWORD_TYPE, 0UL) &&
             !inst_cm_start_fix(1, INST_REG_DWORD_TYPE, 3UL) &&
             !inst_cm_start_fix(0, INST_REG_DWORD_TYPE, 0UL),
             "xhcicm's start type: boot set back, demand left");

    mk_check(mk_s98_inf_ok("C:\\WINDOWS\\INF\\oem12.inf", "C:\\Windows") &&
             mk_s98_inf_ok("c:\\windows\\inf\\OEM0.INF", "C:\\WINDOWS\\") &&
             mk_s98_inf_ok("D:\\WIN\\INF\\oem99999.inf", "D:\\WIN"),
             "Stor98Inf: a published INF of the store");
    mk_check(!mk_s98_inf_ok("C:\\WINDOWS\\INF\\stor98.inf", "C:\\WINDOWS") &&
             !mk_s98_inf_ok("C:\\WINDOWS\\INF\\oem.inf", "C:\\WINDOWS") &&
             !mk_s98_inf_ok("C:\\WINDOWS\\INF\\oem123456.inf", "C:\\WINDOWS") &&
             !mk_s98_inf_ok("C:\\WINDOWS\\INF\\oem1.inf.bak", "C:\\WINDOWS") &&
             !mk_s98_inf_ok("C:\\WINDOWS\\INF\\sub\\oem1.inf", "C:\\WINDOWS") &&
             !mk_s98_inf_ok("C:\\WINDOWSX\\INF\\oem1.inf", "C:\\WINDOWS") &&
             !mk_s98_inf_ok("E:\\pkg\\stor98.inf", "C:\\WINDOWS") &&
             !mk_s98_inf_ok("", "C:\\WINDOWS") && !mk_s98_inf_ok(NULL, "C:\\") &&
             !mk_s98_inf_ok("C:\\INF\\oem1.inf", "") &&
             !mk_s98_inf_ok("C:\\INF\\oem1.inf", "\\"),
             "Stor98Inf: anything else refused");
    mk_check(strcmp(mk_s98_rec_text(MK_S98_DISABLE), "disable") == 0 &&
             strcmp(mk_s98_rec_text(MK_S98_DONE), "done") == 0 &&
             strcmp(mk_s98_rec_text(MK_S98_FAILED), "failed") == 0 &&
             strcmp(mk_s98_rec_text(MK_S98_ENABLE), "enable") == 0 &&
             strcmp(mk_s98_rec_text(MK_S98_RESTART), "restart") == 0 &&
             mk_s98_rec_text(MK_S98_NONE) == NULL &&
             strcmp(mk_s98_rec_text(MK_S98_GIVE_RESTART), "give-restart") == 0 &&
             mk_s98_rec_text(MK_S98_GIVE_RESTART + 1) == NULL,
             "the switch record's texts");
    mk_check(mk_s98_rec_parse(NULL) == MK_S98_NONE &&
             mk_s98_rec_parse("install") == MK_S98_INSTALL &&
             mk_s98_rec_parse("busy") == MK_S98_BUSY &&
             mk_s98_rec_parse("Done") == MK_S98_FAILED &&
             mk_s98_rec_parse("") == MK_S98_FAILED,
             "and back; an unknown text is final");
    mk_check(mk_s98_retry(MK_S98_NONE, 0, 0) && !mk_s98_retry(MK_S98_NONE, 1, 99999UL),
             "no record: the first attempt only");
    mk_check(mk_s98_retry(MK_S98_BUSY, 0, 0) && !mk_s98_retry(MK_S98_BUSY, 1, 9999UL) &&
             mk_s98_retry(MK_S98_BUSY, 1, 10000UL) && mk_s98_retry(MK_S98_BUSY, 2, 10000UL) &&
             !mk_s98_retry(MK_S98_BUSY, 3, 99999UL),
             "busy: a new arrival, or two retries 10 s apart");
    mk_check(!mk_s98_retry(MK_S98_DONE, 0, 0) && !mk_s98_retry(MK_S98_FAILED, 0, 0) &&
             !mk_s98_retry(MK_S98_DISABLE, 0, 0) && !mk_s98_retry(MK_S98_INSTALL, 0, 0) &&
             !mk_s98_retry(MK_S98_ENABLE, 0, 0) && !mk_s98_retry(MK_S98_RESTART, 0, 0) &&
             !mk_s98_retry(MK_S98_GIVE, 0, 0) &&
             !mk_s98_retry(MK_S98_GIVE_RESTART, 0, 0),
             "done, failed, restart and a switch's own states: never");
    {
        /* 0 root, 1 the tunnel port (a member), 2 a USP, 3 a DSP, 4 the
         * drive behind them, 5 a root port, 6 an internal drive. */
        static const int par[7] = { -1, 0, 1, 2, 3, 0, 5 };
        static const int mem[7] = { 0, 1, 0, 0, 0, 0, 0 };
        static const int loop[2] = { 1, 0 };
        static const int none[2] = { 0, 0 };
        mk_check(mk_s98_below(par, 7, 4, mem) && mk_s98_below(par, 7, 2, mem),
                 "below the tunnel port, at any depth");
        mk_check(!mk_s98_below(par, 7, 6, mem) && !mk_s98_below(par, 7, 1, mem) &&
                 !mk_s98_below(par, 7, 0, mem),
                 "an internal drive, the port itself and the root are not");
        mk_check(!mk_s98_below(par, 7, 7, mem) && !mk_s98_below(par, 7, -1, mem) &&
                 !mk_s98_below(NULL, 7, 4, mem) && !mk_s98_below(loop, 2, 0, none),
                 "out of range, and a loop, end");
    }
    {
        MK_S98_IN s;
        memset(&s, 0, sizeof(s));
        s.PolicyOk = 1;
        s.WalkWhole = 1;
        s.IsPci = 1;
        s.Nvme = 1;
        s.Service = "stornvme";
        s.StatusOk = 1;
        s.Started = 1;
        s.Below = 1;
        s.TunnelId = 1;
        s.Os = MK_S98_OS_81;
        mk_check(mk_s98_wanted(&s) == MK_S98_GO && mk_s98_displaces(&s),
                 "8.1: a tunnelled stornvme drive: the 8.1 arm");
        s.TunnelId = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_NOID,
                 "8.1: without the tunnel id: not taken (record 31 section 4.10)");
        s.Os = MK_S98_OS_LOW;
        s.Service = "";
        s.Started = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_GO,
                 "2000 to 8: a driverless drive without the id: taken (by model)");
        s.Service = "stornvme";
        s.Started = 1;
        s.Os = MK_S98_OS_81;
        s.TunnelId = 1;
        s.Service = "StorNVMe";
        mk_check(mk_s98_wanted(&s) == MK_S98_GO, "the service in any case");
        s.Below = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_TUNNEL, "an internal drive: never");
        s.Below = 1;
        s.Service = "stor98";
        mk_check(mk_s98_wanted(&s) == MK_S98_W_SERVICE, "Stor98 already: not");
        s.Service = "secnvme";
        mk_check(mk_s98_wanted(&s) == MK_S98_W_SERVICE, "a vendor's driver: left");
        s.Service = "stornvme2";
        mk_check(mk_s98_wanted(&s) == MK_S98_W_SERVICE, "a longer name: left");
        s.Service = NULL;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_SERVICE, "an unread service: left");
        s.Service = "stornvme";
        s.Os = MK_S98_OS_LOW;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_SERVICE && !mk_s98_displaces(&s),
                 "2000 to 8: stornvme is never displaced");
        s.Os = MK_S98_OS_10;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_OS, "10: stornvme kept");
        s.Os = MK_S98_OS_NONE;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_OS, "a system not told: nothing");
        s.Service = "";
        s.Started = 0;
        s.Problem = 1;
        s.ProblemCode = 28;
        s.Os = MK_S98_OS_LOW;
        mk_check(mk_s98_wanted(&s) == MK_S98_GO && !mk_s98_displaces(&s),
                 "2000 to 8: no driver (Code 28): installed over nothing");
        s.Os = MK_S98_OS_81;
        mk_check(mk_s98_wanted(&s) == MK_S98_GO && !mk_s98_displaces(&s),
                 "8.1: no driver: installed over nothing");
        s.TunnelId = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_NOID,
                 "8.1: no driver and no id: not taken");
        s.TunnelId = 1;
        s.Os = MK_S98_OS_10;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_OS, "10: no driver: nothing");
        s.Os = MK_S98_OS_LOW;
        s.Problem = 0;
        s.ProblemCode = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_GO, "no driver, no problem: installed");
        s.Problem = 1;
        s.ProblemCode = MK_S98_PROB_DISABLED;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_STATE, "no driver, disabled: left");
        s.ProblemCode = 28;
        s.Started = 1;
        s.Problem = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_STATE, "no driver and started: left");
        s.Started = 0;
        s.Problem = 1;
        s.Below = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_TUNNEL, "no driver, internal: never");
        s.Below = 1;
        s.Protected = 1;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_PROTECTED, "no driver, protected: never");
        s.Protected = 0;
        s.InstallPending = 1;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_PENDING, "no driver, wizard open: wait");
        s.InstallPending = 0;
        s.Record = MK_S98_FAILED;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_RECORD, "no driver, failed once: not again");
        s.Record = MK_S98_NONE;
        s.PolicyOk = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_POLICY, "no driver, no Stor98Inf: nothing");
        s.PolicyOk = 1;
        s.Service = "stor98";
        mk_check(mk_s98_wanted(&s) == MK_S98_W_SERVICE, "on Stor98, not started: left");
        s.Service = "stornvme";
        s.Os = MK_S98_OS_81;
        s.Started = 1;
        s.Problem = 0;
        s.ProblemCode = 0;
        s.Nvme = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_CLASS, "not NVMe: not");
        s.Nvme = 1;
        s.IsPci = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_CLASS, "not PCI: not");
        s.IsPci = 1;
        s.PolicyOk = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_POLICY, "no Stor98Inf: nothing");
        s.PolicyOk = 1;
        s.WalkWhole = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_WALK, "an incomplete walk: nothing");
        s.WalkWhole = 1;
        s.Protected = 1;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_PROTECTED, "a protected node: never");
        s.Protected = 0;
        s.Started = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_STATE, "not started: not yet");
        s.Started = 1;
        s.Problem = 1;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_STATE, "a problem: not");
        s.Problem = 0;
        s.StatusOk = 0;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_STATE, "an unread status: not");
        s.StatusOk = 1;
        s.Record = MK_S98_DONE;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_RECORD, "switched once: never again");
        s.Record = MK_S98_BUSY;
        s.Attempts = 1;
        s.SinceMs = 500;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_RECORD, "busy: not before 10 s");
        s.SinceMs = 10000;
        mk_check(mk_s98_wanted(&s) == MK_S98_GO, "busy: again after 10 s");
        s.InstallPending = 1;
        mk_check(mk_s98_wanted(&s) == MK_S98_W_PENDING, "an installation pending: wait");
        mk_check(strcmp(mk_s98_why_text(MK_S98_W_TUNNEL),
                        "not below a verified tunnel port") == 0 &&
                 strcmp(mk_s98_why_text(MK_S98_W_OS),
                        "not Windows 2000 to 8.1") == 0 &&
                 strcmp(mk_s98_why_text(42), "?") == 0,
                 "the reasons' texts");
    }
    mk_check(mk_s98_os(5, 0, 2195) == MK_S98_OS_LOW &&
             mk_s98_os(5, 1, 2600) == MK_S98_OS_LOW &&
             mk_s98_os(5, 2, 3790) == MK_S98_OS_LOW &&
             mk_s98_os(6, 0, 6002) == MK_S98_OS_LOW &&
             mk_s98_os(6, 1, 7601) == MK_S98_OS_LOW &&
             mk_s98_os(6, 2, 9200) == MK_S98_OS_LOW,
             "the systems: 2000, XP, 2003, Vista, 7 and 8 the install over nothing");
    mk_check(mk_s98_os(6, 3, 9600) == MK_S98_OS_81 &&
             mk_s98_os(6, 3, 10240) == MK_S98_OS_10 &&
             mk_s98_os(6, 4, 9841) == MK_S98_OS_10 &&
             mk_s98_os(10, 0, 19045) == MK_S98_OS_10 &&
             mk_s98_os(10, 0, 22000) == MK_S98_OS_10,
             "the systems: 8.1 its own; 10 and later nothing");
    mk_check(mk_s98_os(4, 0, 1381) == MK_S98_OS_NONE &&
             mk_s98_os(0, 0, 0) == MK_S98_OS_NONE &&
             mk_s98_os_both(MK_S98_OS_81, MK_S98_OS_81) == MK_S98_OS_81 &&
             mk_s98_os_both(MK_S98_OS_LOW, MK_S98_OS_81) == MK_S98_OS_NONE,
             "the systems: NT 4 none; two readings that disagree none");
    mk_check(mk_s98_inbox("stornvme") && mk_s98_inbox("STORNVME") &&
             !mk_s98_inbox("stor98") && !mk_s98_inbox("") && !mk_s98_inbox(NULL),
             "stornvme read as the in-box driver");
    mk_check(mk_s98_noid_why(1, 0, 9) == MK_S98_ID_HAS &&
             mk_s98_noid_why(0, -1, 1) == MK_S98_ID_UNKNOWN &&
             mk_s98_noid_why(0, 0, 1) == MK_S98_ID_NOFILTER &&
             mk_s98_noid_why(0, 1, 5) == MK_S98_ID_EARLY &&
             mk_s98_noid_why(0, 1, 6) == MK_S98_ID_DEPTH &&
             strcmp(mk_s98_noid_text(MK_S98_ID_NOFILTER),
                    "no id: its tunnel port carries no filter of this "
                    "driver's") == 0 &&
             strcmp(mk_s98_noid_text(9), "?") == 0,
             "why the id was absent");
    mk_check(mk_s98_recover(MK_S98_DISABLE, 1, 1, 0, 0) == MK_S98_R_ENABLE &&
             mk_s98_recover(MK_S98_INSTALL, 1, 1, 1, 1) == MK_S98_R_ENABLE &&
             mk_s98_recover(MK_S98_ENABLE, 1, 1, 0, 0) == MK_S98_R_ENABLE,
             "a switch cut short, or an enable not held: enabled once present");
    mk_check(mk_s98_recover(MK_S98_DISABLE, 0, 1, 0, 1) == MK_S98_R_NOTHING &&
             mk_s98_recover(MK_S98_ENABLE, 0, 1, 0, 1) == MK_S98_R_NOTHING,
             "absent: the obligation kept");
    mk_check(mk_s98_recover(MK_S98_INSTALL, 1, 0, 0, 0) == MK_S98_R_FAIL &&
             mk_s98_recover(MK_S98_ENABLE, 1, 0, 1, 0) == MK_S98_R_DONE &&
             mk_s98_recover(MK_S98_DISABLE, 1, 0, 0, 1) == MK_S98_R_FAIL,
             "enabled: done on Stor98, failed otherwise");
    mk_check(mk_s98_recover(MK_S98_GIVE, 1, 1, 0, 0) == MK_S98_R_FAIL &&
             mk_s98_recover(MK_S98_GIVE, 1, 0, 1, 0) == MK_S98_R_DONE &&
             mk_s98_recover(MK_S98_GIVE, 0, 0, 0, 1) == MK_S98_R_NOTHING &&
             strcmp(mk_s98_rec_text(MK_S98_GIVE), "give") == 0 &&
             mk_s98_rec_parse("give") == MK_S98_GIVE,
             "a cut-short install over no driver: done or failed once present, never an enable");
    mk_check(mk_s98_recover(MK_S98_GIVE_RESTART, 1, 1, 1, 0) == MK_S98_R_NOTHING &&
             mk_s98_recover(MK_S98_GIVE_RESTART, 1, 1, 1, 1) == MK_S98_R_DONE &&
             mk_s98_recover(MK_S98_GIVE_RESTART, 1, 0, 0, 1) == MK_S98_R_FAIL &&
             mk_s98_parse_round("give-restart"),
             "an install over no driver that asked for a restart: decided at the first poll, never an enable");
    mk_check(mk_s98_recover(MK_S98_RESTART, 1, 0, 1, 0) == MK_S98_R_NOTHING &&
             mk_s98_recover(MK_S98_RESTART, 1, 0, 1, 1) == MK_S98_R_DONE &&
             mk_s98_recover(MK_S98_RESTART, 1, 0, 0, 1) == MK_S98_R_FAIL &&
             mk_s98_recover(MK_S98_RESTART, 0, 0, 0, 1) == MK_S98_R_NOTHING,
             "restart: decided at the first poll after a start");
    mk_check(mk_s98_recover(MK_S98_DONE, 1, 1, 0, 1) == MK_S98_R_NOTHING &&
             mk_s98_recover(MK_S98_BUSY, 1, 1, 0, 1) == MK_S98_R_NOTHING &&
             mk_s98_recover(MK_S98_FAILED, 1, 1, 0, 1) == MK_S98_R_NOTHING &&
             mk_s98_recover(MK_S98_NONE, 1, 1, 0, 1) == MK_S98_R_NOTHING,
             "done, busy, failed and no record: nothing");
    {
        MK_S98_MF f;
        MK_S98_MF g;
        INST_U32 v;
        memset(&f, 0, sizeof(f));
        f.Nt5 = 1;
        f.PolicyOk = 1;
        f.Below = 1;
        f.Nvme = 1;
        f.Service = "";
        f.StatusOk = 1;
        f.Problem = 1;
        f.ProblemCode = 0x1CUL;
        mk_check(mk_s98_mark_sets(&f),
                 "the mark's restart: a plain driverless drive, failed install, set to run again");
        g = f;
        g.Problem = 0;
        mk_check(mk_s98_mark_sets(&g), "not configured yet (no problem): set too");
        g.ProblemCode = 0x01UL;
        g.Problem = 1;
        mk_check(mk_s98_mark_sets(&g), "problem 1: set too");
        g.ProblemCode = 0x12UL;
        mk_check(mk_s98_mark_sets(&g), "problem 18: set too");
        g = f;
        g.Service = "stor98";
        mk_check(!mk_s98_mark_sets(&g), "a drive with a driver: never");
        g.Service = "stornvme";
        mk_check(!mk_s98_mark_sets(&g), "a drive on another driver: never");
        g.Service = NULL;
        mk_check(!mk_s98_mark_sets(&g), "a Service unread: never");
        g = f;
        g.Started = 1;
        g.Problem = 0;
        mk_check(!mk_s98_mark_sets(&g), "a started function: never");
        g = f;
        g.ProblemCode = MK_S98_PROB_DISABLED;
        mk_check(!mk_s98_mark_sets(&g), "a disabled drive: never");
        g.ProblemCode = 0x0AUL;
        mk_check(!mk_s98_mark_sets(&g), "another problem: never");
        g = f;
        g.HasId = 1;
        mk_check(!mk_s98_mark_sets(&g), "a drive with the id (offered Stor98): left");
        g = f;
        g.Declined = 1;
        mk_check(!mk_s98_mark_sets(&g), "declined in this arrival: left");
        g = f;
        g.Nt5 = 0;
        mk_check(!mk_s98_mark_sets(&g), "Vista and later: no step");
        g = f;
        g.PolicyOk = 0;
        mk_check(!mk_s98_mark_sets(&g), "Stor98 not staged: no step");
        g = f;
        g.Below = 0;
        mk_check(!mk_s98_mark_sets(&g), "not below the unit: never");
        g = f;
        g.Nvme = 0;
        mk_check(!mk_s98_mark_sets(&g), "not a drive (a dock's xHCI or network function): never");
        g = f;
        g.Protected = 1;
        mk_check(!mk_s98_mark_sets(&g), "a protected function: never");
        g = f;
        g.StatusOk = 0;
        mk_check(!mk_s98_mark_sets(&g), "a status unread: never");
        mk_check(mk_s98_rerun_flags(0x40UL, &v) == 1 && v == 0x20UL &&
                 mk_s98_rerun_flags(0x440UL, &v) == 1 && v == 0x420UL &&
                 mk_s98_rerun_flags(0x0UL, &v) == 1 && v == 0x20UL &&
                 mk_s98_rerun_flags(0x60UL, &v) == 1 && v == 0x20UL,
                 "the flag step: failed cleared, reinstall set, the rest kept");
        mk_check(mk_s98_rerun_flags(0x20UL, &v) == 0 && v == 0x20UL &&
                 mk_s98_rerun_flags(0x420UL, &v) == 0 && v == 0x420UL,
                 "the flag step: already so, nothing written");
        mk_check(mk_s98_driverless(0, 0, 0) && mk_s98_driverless(0, 1, 0x1CUL) &&
                 !mk_s98_driverless(1, 0, 0) &&
                 !mk_s98_driverless(0, 1, MK_S98_PROB_DISABLED),
                 "the driverless states the restarts admit");
    }
    {
        /* A hot-added controller waiting for a restart (record 21 7.6). */
        MK_CTL_IN f;
        MK_CTL_IN g;

        memset(&f, 0, sizeof(f));
        f.Restart = 1;
        f.Nt6x86 = 1;
        f.Pci = 1;
        f.Service = "xhci98";
        f.XhciCompat = 1;
        f.DrvRead = 1;
        f.InfSection = "Xhci.Dev6";
        f.MatchingId = "pci\\cc_0c0330";
        f.FlagsRead = 1;
        f.Flags = 0;
        f.StatusOk = 1;
        f.Status = 0x01802500UL;
        f.Problem = 1;
        f.Behind = 1;
        f.Protected = 0;
        f.HelperRunning = 1;
        f.InstallIdle = 1;
        f.Tried = 0;
        mk_check(mk_ctl_ready_why(&f) == MK_CTL_GO,
                 "the dock's xHCI as read on 7 (0x01802500, problem 1): started");
        g = f; g.Restart = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_OFF, "the switch off: never");
        g = f; g.Nt6x86 = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_SYSTEM,
                 "XP, 8 or a 64-bit system: never");
        g = f; g.Pci = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_NOT_OURS,
                 "a root hub or hub (USB\\, service xhci98): never");
        g = f; g.Service = "usbxhci";
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_NOT_OURS, "another driver: never");
        g = f; g.Service = NULL;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_NOT_OURS, "the service unread: never");
        g = f; g.XhciCompat = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_NOT_OURS, "not an xHCI: never");
        g = f; g.InfSection = "Xhci.Dev";
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_DRIVER_KEY,
                 "another section with service xhci98: never");
        g = f; g.MatchingId = "pci\\ven_1b36&dev_000d";
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_DRIVER_KEY,
                 "another INF's id row: never");
        g = f; g.DrvRead = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_DRIVER_KEY,
                 "the driver key unread: never");
        g = f; g.Flags = 0x20UL;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_FLAGS, "reinstall pending: never");
        g = f; g.Flags = 0x40UL;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_FLAGS, "a failed install: never");
        g = f; g.Flags = 0x400UL;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_FLAGS, "an unfinished install: never");
        g = f; g.FlagsRead = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_FLAGS, "the flags unread: never");
        g = f; g.Status = 0x01802400UL;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_NOT_WAITING,
                 "problem 1 without DN_NEED_RESTART: never");
        g = f; g.Problem = 10;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_NOT_WAITING,
                 "DN_NEED_RESTART with another problem: never");
        g = f; g.Status = 0x0180200AUL; g.Problem = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_NOT_WAITING, "started: nothing to do");
        g = f; g.StatusOk = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_NOT_WAITING, "the status unread: never");
        g = f; g.Behind = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_NOT_BEHIND,
                 "behind no verified tunnel port: never");
        g = f; g.Protected = 1;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_PROTECTED, "a protected device: never");
        g = f; g.HelperRunning = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_HELPER,
                 "the helper stopped, absent or unread: never");
        g = f; g.InstallIdle = 0;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_INSTALLING, "an install pending: not now");
        g = f; g.Tried = 1;
        mk_check(mk_ctl_ready_why(&g) == MK_CTL_TRIED,
                 "a second attempt in the same arrival: never");
        mk_check(strcmp(mk_ctl_why_text(MK_CTL_HELPER),
                        "the helper xhcihlpr is not running") == 0,
                 "a reason's text");
    }
    {
        /* Record 22 section 11.1.11 Q2 (task 48.35): the flush's events
         * and its choice of volume. */
        INST_U32 chain[3];
        INST_U32 mem[2];

        mk_check(mk_flush_wants(MK_PBT_APMSUSPEND) && mk_flush_wants(MK_PBT_APMQUERYSUSPEND),
                 "flush: at the suspend and its query");
        mk_check(!mk_flush_wants(7UL) && !mk_flush_wants(0x12UL),
                 "flush: never at a resume");
        chain[0] = 0x0600;
        chain[1] = 0x0518;
        chain[2] = 0x0008;
        mem[0] = 0x0510;
        mem[1] = 0x0518;
        mk_check(mk_flush_behind(chain, 3, mem, 2), "flush: a disk behind a member");
        mem[1] = 0x0519;
        mk_check(!mk_flush_behind(chain, 3, mem, 2), "flush: no member above it");
        mk_check(!mk_flush_behind(chain, 3, mem, 0), "flush: no member at all");
    }
    {
        /* Record 21 section 7.8.3 (task 49.26): the location kept per
         * instance, never read while the device is not started. */
        MK_LOCS *lc = &mkTestLocs;
        const char *a = "PCI\\VEN_8086&DEV_15D3\\3&11583659&0&08";
        const char *u = "PCI\\VEN_1B21&DEV_2464\\4&2B8260C0&0&0010";
        INST_U32 bus = 0;
        INST_U32 ad = 0;
        int r;
        int k;

        mk_loc_reset(lc);
        mk_check(mk_loc_get(lc, u, 0, 1000, &bus, &ad) == MK_LOC_NONE,
                 "loc: never started, nothing kept: no read");
        mk_check(mk_loc_get(lc, u, 0, 1000, &bus, &ad) == MK_LOC_NONE &&
                 lc->Count == 0, "loc: and nothing is kept for it");
        mk_check(mk_loc_get(lc, a, 1, 1000, &bus, &ad) == MK_LOC_READ,
                 "loc: started, nothing kept: read");
        mk_loc_put(lc, a, MK_LOC_R_OK, 5, 0x00010000UL, 1000);
        bus = 0;
        ad = 0;
        mk_check(mk_loc_get(lc, a, 1, 2000, &bus, &ad) == MK_LOC_KEPT &&
                 bus == 5 && ad == 0x00010000UL,
                 "loc: started again: the kept values, no read");
        bus = 0;
        mk_check(mk_loc_get(lc, a, 0, 3000, &bus, &ad) == MK_LOC_KEPT &&
                 bus == 5, "loc: not started: the kept values still serve");
        mk_check(mk_loc_get(lc, a, 0, 3100, &bus, &ad) == MK_LOC_KEPT,
                 "loc: still not started: no read");
        mk_check(mk_loc_get(lc, a, 1, 4000, &bus, &ad) == MK_LOC_READ,
                 "loc: started after a stop: read once again");
        mk_loc_put(lc, a, MK_LOC_R_OK, 6, 0x00010000UL, 4000);
        mk_check(mk_loc_get(lc, a, 1, 4100, &bus, &ad) == MK_LOC_KEPT &&
                 bus == 6, "loc: the new values kept");
        mk_check(mk_loc_get(lc, "ROOT\\LEGACY_BEEP\\0000", 1, 5000, &bus,
                            &ad) == MK_LOC_READ, "loc: a root device read once");
        mk_loc_put(lc, "ROOT\\LEGACY_BEEP\\0000", MK_LOC_R_NOVALUE, 0, 0, 5000);
        mk_check(mk_loc_get(lc, "ROOT\\LEGACY_BEEP\\0000", 1, 5000 + 2 * MK_LOC_RETRY_MS,
                            &bus, &ad) == MK_LOC_NONE,
                 "loc: no such value: kept as none, never read again");
        mk_check(mk_loc_get(lc, u, 1, 6000, &bus, &ad) == MK_LOC_READ,
                 "loc: the unit started: read");
        mk_loc_put(lc, u, MK_LOC_R_FAILED, 0, 0, 6000);
        r = 1;
        for (k = 0; k < 5; k++) {
            if (mk_loc_get(lc, u, 1, 6000 + (INST_U32)k * 10000UL, &bus,
                           &ad) != MK_LOC_NONE) {
                r = 0;
            }
        }
        mk_check(r, "loc: a failed read, asked again and again: no read "
                    "before its retry time");
        mk_check(mk_loc_get(lc, u, 1, 6000 + MK_LOC_RETRY_MS, &bus, &ad) ==
                 MK_LOC_READ, "loc: read again at its retry time");
        mk_loc_put(lc, u, MK_LOC_R_OK, 7, 0, 6000 + MK_LOC_RETRY_MS);
        mk_check(mk_loc_get(lc, u, 0, 0xFFFFFFF0UL, &bus, &ad) == MK_LOC_KEPT,
                 "loc: held for eject (not started): its location still known");
        mk_check(mk_loc_get(lc, u, 1, 0x00000010UL, &bus, &ad) == MK_LOC_READ,
                 "loc: and read again once it starts");
        mk_loc_put(lc, u, MK_LOC_R_FAILED, 0, 0, 0xFFFFFFF0UL);
        mk_check(mk_loc_get(lc, u, 1, 0x00000010UL, &bus, &ad) == MK_LOC_NONE &&
                 mk_loc_get(lc, u, 1, MK_LOC_RETRY_MS - 0x10UL, &bus,
                            &ad) == MK_LOC_READ,
                 "loc: the retry time across the tick's wrap");
        mk_loc_put(lc, u, MK_LOC_R_OK, 7, 0, 0);
        mk_loc_stale_all(lc);
        mk_check(mk_loc_get(lc, a, 1, 7000, &bus, &ad) == MK_LOC_READ &&
                 mk_loc_get(lc, u, 0, 7000, &bus, &ad) == MK_LOC_KEPT,
                 "loc: after a resume: a started device read again, one not "
                 "started kept as it was");
        mk_loc_put(lc, a, MK_LOC_R_OK, 6, 0x00010000UL, 7000);
        mk_loc_walk_begin(lc);
        (void)mk_loc_get(lc, a, 1, 8000, &bus, &ad);
        mk_loc_walk_end(lc, 0);
        mk_check(mk_loc_get(lc, u, 0, 8000, &bus, &ad) == MK_LOC_KEPT,
                 "loc: an incomplete walk drops nothing");
        mk_loc_walk_begin(lc);
        (void)mk_loc_get(lc, a, 1, 9000, &bus, &ad);
        mk_loc_walk_end(lc, 1);
        mk_check(mk_loc_get(lc, u, 1, 9000, &bus, &ad) == MK_LOC_READ &&
                 mk_loc_get(lc, a, 1, 9000, &bus, &ad) == MK_LOC_KEPT,
                 "loc: a whole walk that did not reach it drops it (read "
                 "anew), the rest kept");
        mk_check(mk_loc_resume_event(6UL) && mk_loc_resume_event(7UL) &&
                 mk_loc_resume_event(0x12UL) &&
                 !mk_loc_resume_event(MK_PBT_APMSUSPEND) &&
                 !mk_loc_resume_event(MK_PBT_APMQUERYSUSPEND) &&
                 !mk_loc_resume_event(0x0AUL),
                 "loc: stale at the three resume notices alone");
    }
    {
        /* Record 21 section 7.8.4: the installer's hold. */
        MK_HOLD h;
        int ev;

        memset(&h, 0, sizeof(h));
        mk_check(mk_hold_step(&h, 0, 0, 100, &ev) == 1 &&
                 ev == MK_HOLD_EV_NONE && !h.AckDue,
                 "hold: no event: the poll runs");
        mk_check(mk_hold_step(&h, 1, 7, 200, &ev) == 0 &&
                 ev == MK_HOLD_EV_BEGAN && h.AckDue && h.Gen == 7,
                 "hold: the event: skipped, the hold begins, its number due "
                 "for acknowledgement");
        h.AckedGen = h.Gen;
        mk_check(mk_hold_step(&h, 1, 7, 200 + MK_HOLD_MAX_MS - 1, &ev) == 0 &&
                 ev == MK_HOLD_EV_NONE && !h.AckDue,
                 "hold: still held within its bound, acknowledged once");
        mk_check(mk_hold_step(&h, 1, 7, 200 + MK_HOLD_MAX_MS, &ev) == 1 &&
                 ev == MK_HOLD_EV_EXPIRED && !h.AckDue,
                 "hold: at its bound the poll runs");
        mk_check(mk_hold_step(&h, 1, 7, 300 + MK_HOLD_MAX_MS, &ev) == 1 &&
                 ev == MK_HOLD_EV_NONE,
                 "hold: and keeps running while the old event stays");
        mk_check(mk_hold_step(&h, 0, 7, 400 + MK_HOLD_MAX_MS, &ev) == 1 &&
                 ev == MK_HOLD_EV_ENDED, "hold: the event gone: ended");
        mk_check(mk_hold_step(&h, 1, 8, 500 + MK_HOLD_MAX_MS, &ev) == 0 &&
                 ev == MK_HOLD_EV_BEGAN && h.AckDue,
                 "hold: a new event holds again");
        memset(&h, 0, sizeof(h));
        (void)mk_hold_step(&h, 1, 1, 0xFFFFFF00UL, &ev);
        mk_check(mk_hold_step(&h, 1, 1, 0x00000100UL, &ev) == 0,
                 "hold: the tick's wrap does not end it early");

        /* Two runs back to back, the gap between them entirely between two
         * polls (the event seen at every poll): the second run's number
         * begins its own hold, whether the first had expired or was at 14
         * minutes, and is acknowledged; an expired hold is never
         * acknowledged for a number it did not hold for. */
        memset(&h, 0, sizeof(h));
        (void)mk_hold_step(&h, 1, 3, 1000, &ev);
        h.AckedGen = h.Gen;
        (void)mk_hold_step(&h, 1, 3, 1000 + MK_HOLD_MAX_MS, &ev);
        mk_check(h.Expired && ev == MK_HOLD_EV_EXPIRED,
                 "hold, back to back: the first run's hold expired");
        mk_check(mk_hold_step(&h, 1, 3, 2000 + MK_HOLD_MAX_MS, &ev) == 1 &&
                 !h.AckDue,
                 "hold, back to back: an expired hold with the same number "
                 "runs and is not acknowledged");
        mk_check(mk_hold_step(&h, 1, 4, 3000 + MK_HOLD_MAX_MS, &ev) == 0 &&
                 ev == MK_HOLD_EV_BEGAN && h.AckDue && h.Gen == 4 &&
                 h.Since == 3000 + MK_HOLD_MAX_MS && !h.Expired,
                 "hold, back to back after an expired one: the new run's "
                 "number holds, from now, due for acknowledgement");
        h.AckedGen = h.Gen;
        mk_check(mk_hold_step(&h, 1, 4,
                              3000 + MK_HOLD_MAX_MS + MK_HOLD_MAX_MS - 1,
                              &ev) == 0,
                 "hold, back to back after an expired one: held for its own "
                 "full bound");
        memset(&h, 0, sizeof(h));
        (void)mk_hold_step(&h, 1, 5, 1000, &ev);
        h.AckedGen = h.Gen;
        mk_check(mk_hold_step(&h, 1, 5, 1000 + 14UL * 60000UL, &ev) == 0,
                 "hold, back to back: the first run's hold at 14 minutes");
        mk_check(mk_hold_step(&h, 1, 6, 1000 + 14UL * 60000UL + 1000, &ev) ==
                     0 && ev == MK_HOLD_EV_BEGAN && h.AckDue &&
                 h.Since == 1000 + 14UL * 60000UL + 1000,
                 "hold, back to back at 14 minutes: the new run's hold begins "
                 "again");
        mk_check(mk_hold_step(&h, 1, 6,
                              1000 + 14UL * 60000UL + 1000 + 2UL * 60000UL,
                              &ev) == 0,
                 "hold, back to back at 14 minutes: not ended a minute after "
                 "the first run's bound");
        memset(&h, 0, sizeof(h));
        mk_check(mk_hold_step(&h, 1, 0, 1000, &ev) == 0 && !h.AckDue,
                 "hold: a number not read (0) holds and is never "
                 "acknowledged (no run's number is 0)");
    }
    {
        /* Record 21 section 7.8.4: the owners each named object is
         * trusted with. */
        mk_check(inst_mk_owner_ok(INST_OWNER_SYSTEM, 1) &&
                 !inst_mk_owner_ok(INST_OWNER_ADMINS, 1) &&
                 !inst_mk_owner_ok(INST_OWNER_OTHER, 1) &&
                 inst_mk_owner_ok(INST_OWNER_SYSTEM, 0) &&
                 inst_mk_owner_ok(INST_OWNER_ADMINS, 0) &&
                 !inst_mk_owner_ok(INST_OWNER_OTHER, 0),
                 "named objects: the idle event SYSTEM's alone, the hold "
                 "event SYSTEM's or the Administrators'");
    }
    return mkFails;
}
