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
        mk_get32(b, CM_Q_O_VERSION) != CM_Q_VERSION) {
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
    mk_put32(b, CM_Q_O_VERSION, CM_Q_VERSION);
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
    return mkFails;
}
