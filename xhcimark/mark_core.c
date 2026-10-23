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
    if (rec < CM_Q_HDR_BYTES || rec + CM_ST_HDR_BYTES > bytes) {
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
    if (count > MK_MAX_ENTRIES || entBytes < CM_QM_ENT_BYTES ||
        off < CM_QM_HDR_BYTES || off > bytes ||
        count * entBytes > bytes - off) {
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
    }
    return 1;
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

int mk_restart_wanted(const MK_ENTRY *e, int valuePresent)
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
    return (e->Flags & MK_EF_REMOVABLE) == 0;
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
    if (strcmp(text + 29, "written") == 0) {
        *state = MK_REC_WRITTEN;
    } else if (strcmp(text + 29, "writing") == 0) {
        *state = MK_REC_WRITING;
    } else {
        return 0;
    }
    return 1;
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
    NULL, "disable", "install", "done", "busy", "failed", "enable", "restart"
};

const char *mk_s98_rec_text(int state)
{
    if (state < MK_S98_DISABLE || state > MK_S98_RESTART) {
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
    for (s = MK_S98_DISABLE; s <= MK_S98_RESTART; s++) {
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

int mk_s98_wanted(const MK_S98_IN *in)
{
    if (!in->IsPci || !in->Nvme) {
        return MK_S98_W_CLASS;
    }
    if (!inst_s98_over_inbox(1, in->Service)) {
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
    if (in->Protected) {
        return MK_S98_W_PROTECTED;
    }
    if (!in->StatusOk || !in->Started || in->Problem) {
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
        return "to be switched";
    case MK_S98_W_CLASS:
        return "not a PCI NVMe controller";
    case MK_S98_W_SERVICE:
        return "not on stornvme";
    case MK_S98_W_POLICY:
        return "no staged Stor98 named (Parameters\\Stor98Inf)";
    case MK_S98_W_WALK:
        return "the device tree walk was incomplete";
    case MK_S98_W_TUNNEL:
        return "not below a verified tunnel port";
    case MK_S98_W_PROTECTED:
        return "a protected device";
    case MK_S98_W_STATE:
        return "not started, or with a problem";
    case MK_S98_W_RECORD:
        return "its record allows no attempt now";
    case MK_S98_W_PENDING:
        return "a Windows installation is pending";
    }
    return "?";
}

int mk_s98_recover(int record, int present, int disabled, int s98Started,
                   int firstPoll)
{
    if (!present) {
        return MK_S98_R_NOTHING;
    }
    if (record == MK_S98_DISABLE || record == MK_S98_INSTALL ||
        record == MK_S98_ENABLE) {
        if (disabled) {
            return MK_S98_R_ENABLE;
        }
    } else if (record != MK_S98_RESTART || !firstPoll) {
        return MK_S98_R_NOTHING;
    }
    return s98Started ? MK_S98_R_DONE : MK_S98_R_FAIL;
}

/* ======================================================================== */
/* The self-test                                                            */
/* ======================================================================== */

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
    mk_put32(b, CM_QM_O_COUNT, 2);
    mk_check(mk_marks_parse(b, CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, &m, &need) == 0,
             "a count past the size is refused");
    mk_put32(b, CM_QM_O_COUNT, 1);
    (void)mk_marks_parse(b, CM_QM_HDR_BYTES + CM_QM_ENT_BYTES, &m, &need);

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
    mk_check(mk_restart_wanted(&e, 1), "no attachment with the value: restart");
    mk_check(!mk_restart_wanted(&e, 0), "no value: none");
    e.Flags |= MK_EF_ADD_ATT;
    mk_check(mk_restart_wanted(&e, 1), "attached, Removable not answered: restart");
    e.Flags |= MK_EF_REMOVABLE;
    mk_check(!mk_restart_wanted(&e, 1), "attached and answered: none");
    e.Flags = MK_EF_LOCATED | MK_EF_REPORTED;
    e.Pending = 5;
    mk_check(mk_restart_wanted(&e, 1),
             "a departure pending (a first plug's install) does not stop it");
    e.Pending = 0;
    e.Flags |= MK_EF_SAFE_OFF;
    mk_check(!mk_restart_wanted(&e, 1), "safe eject off: none");

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
             mk_s98_rec_text(MK_S98_NONE) == NULL && mk_s98_rec_text(9) == NULL,
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
             !mk_s98_retry(MK_S98_ENABLE, 0, 0) && !mk_s98_retry(MK_S98_RESTART, 0, 0),
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
        mk_check(mk_s98_wanted(&s) == MK_S98_GO, "a tunnelled stornvme drive: switched");
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
        mk_check(mk_s98_wanted(&s) == MK_S98_W_SERVICE, "no service: left");
        s.Service = "stornvme";
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
                 strcmp(mk_s98_why_text(42), "?") == 0,
                 "the reasons' texts");
    }
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
    return mkFails;
}
