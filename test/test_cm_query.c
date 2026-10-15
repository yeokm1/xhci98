/*
 * test_cm_query.c - the answer of xhcicm.sys's query device
 * (src\cm\cm_query.c; design record 25 section 4.3): the header's golden
 * bytes, the record carried byte for byte, the router extensions under
 * the USB4 connection manager and the ICM, the instance id, the sizing
 * answers (too small, header alone, whole), the router-count edge, a
 * truncated record, a record whose size disagrees with its count, and the
 * pass age's arithmetic.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_statb.h"
#include "../src/cm/cm_qryb.h"
#include "../src/cm/cm_live.h"
#include "test_harness.h"

static TBT_ICM icm;
static TBT_U4CM u4;
static UCHAR rec[CM_ST_MAX_BYTES];
static UCHAR ext[CM_ST_MAX_ROUTERS * CM_Q_EXT_BYTES];
static UCHAR built[CM_Q_BUILD_MAX];
static UCHAR out[CM_Q_BUILD_MAX + 16];

static ULONG get32(const UCHAR *b, ULONG o)
{
    return (ULONG)b[o] | ((ULONG)b[o + 1] << 8) | ((ULONG)b[o + 2] << 16) |
           ((ULONG)b[o + 3] << 24);
}

static void put32(UCHAR *b, ULONG o, ULONG v)
{
    b[o] = (UCHAR)v;
    b[o + 1] = (UCHAR)(v >> 8);
    b[o + 2] = (UCHAR)(v >> 16);
    b[o + 3] = (UCHAR)(v >> 24);
}

static void base_in(CM_STATUS_IN *in, ULONG cm)
{
    memset(in, 0, sizeof(*in));
    in->NhiId = 0x15BF8086UL;
    in->Stage = CM_ST_STAGE_ACTIVE;
    in->Cm = cm;
}

static const char id[] = "PCI\\VEN_8086&DEV_15D2&SUBSYS_00000000&REV_02\\4&1A2B3C&0&0008";

static void q_in(CM_QUERY_IN *q, ULONG recBytes, ULONG extCount)
{
    memset(q, 0, sizeof(*q));
    q->Record = rec;
    q->RecordBytes = recBytes;
    q->Ext = ext;
    q->ExtCount = extCount;
    q->Live = CM_LIVE_ACTIVE;
    q->EpochLo = 0x89ABCDEFUL;
    q->EpochHi = 0x01DC1234UL;
    q->NhiBdf = 0x0500UL;
    q->Id = id;
    q->IdBytes = (ULONG)strlen(id);
}

static void test_layout(void)
{
    CHECK_EQ(CM_Q_IOCTL, (0x22UL << 16) | (0xA40UL << 2), "the control code");
    CHECK_EQ(CM_Q_MAGIC, 'X' | ('C' << 8) | ('M' << 16) | ((ULONG)'Q' << 24),
             "'XCMQ' in byte order");
    CHECK_EQ(CM_Q_O_CONTENT_GEN + 4, CM_Q_HDR_BYTES, "the generation ends the header");
    CHECK_EQ(CM_Q_X_FLAGS + 4 + 8, CM_Q_EXT_BYTES, "8 bytes reserved");
    CHECK(CM_Q_BUILD_MAX <= CM_Q_ASK_BYTES, "one ask holds the largest");
    CHECK_EQ(CM_Q_BUILD_MAX, 64 + 3200 + 768 + 200, "the largest answer");
}

/* The USB4 connection manager: two host ports, one read within its
 * connection, one read in an earlier one; a deeper router. */
static void test_u4(void)
{
    CM_STATUS_IN in;
    CM_QUERY_IN q;
    ULONG n;
    ULONG r;
    ULONG e;
    ULONG bytes;
    const UCHAR *x;

    memset(&u4, 0, sizeof(u4));
    u4.PortCount = 2;
    u4.Ports[0].Index = 1;
    u4.Ports[0].State = TBT_U4_UP;
    u4.Ports[0].Dev.Present = TRUE;
    u4.Ports[0].Generation = 3;
    u4.Ports[0].LinkSeen = TRUE;
    u4.Ports[0].LinkGeneration = 3;
    /* CL0 (2), x2, Gen 3: 0x08243000-ish word with other bits set. */
    u4.Ports[0].LinkLane1 = (TBT_LANE_STATE_CL0 << 26) |
                            (TBT_LANE_WIDTH_X2 << 20) |
                            (TBT_LANE_SPEED_GEN3 << 16) | 0x8030UL;
    u4.Ports[1].Index = 2;
    u4.Ports[1].State = TBT_U4_UP;
    u4.Ports[1].Dev.Present = TRUE;
    u4.Ports[1].Generation = 5;
    u4.Ports[1].LinkSeen = TRUE;
    u4.Ports[1].LinkGeneration = 4;
    u4.Ports[1].LinkLane1 = (TBT_LANE_SPEED_GEN2 << 16) |
                            (TBT_LANE_WIDTH_X1 << 20);
    u4.Links[1].Used = TRUE;
    u4.Links[1].Port = 1;
    u4.Links[1].Child.Lo = 0x301;

    base_in(&in, CM_ST_CM_USB4);
    r = CmStatusBuild(rec, &in, NULL, &u4, NULL);
    CHECK_EQ(r, CM_ST_HDR_BYTES + 3 * CM_ST_ENTRY_BYTES, "three routers");
    n = CmQueryExt(ext, CM_ST_MAX_ROUTERS, rec, r, NULL, &u4);
    CHECK_EQ(n, 3, "one extension per router");
    x = ext;
    CHECK_EQ(get32(x, CM_Q_X_RATE), 20, "Gen 3: 20 Gbit/s per lane");
    CHECK_EQ(get32(x, CM_Q_X_WIDTH), 2, "x2");
    CHECK_EQ(get32(x, CM_Q_X_LINK_STATE), TBT_LANE_STATE_CL0, "CL0");
    CHECK_EQ(get32(x, CM_Q_X_GEN_RAW), TBT_LANE_SPEED_GEN3, "the raw code");
    CHECK_EQ(get32(x, CM_Q_X_IDS), 0, "ids: the record has them");
    CHECK_EQ(get32(x, CM_Q_X_FLAGS), CM_Q_XF_RATE_READ, "read");
    CHECK_EQ(get32(x, 24) | get32(x, 28), 0, "reserved 0");
    x = ext + CM_Q_EXT_BYTES;
    CHECK_EQ(get32(x, CM_Q_X_RATE), 0, "an earlier connection's read: unknown");
    CHECK_EQ(get32(x, CM_Q_X_WIDTH), 0, "width unknown");
    CHECK_EQ(get32(x, CM_Q_X_LINK_STATE), CM_Q_UNKNOWN, "state unknown");
    CHECK_EQ(get32(x, CM_Q_X_GEN_RAW), CM_Q_UNKNOWN, "code unknown");
    CHECK_EQ(get32(x, CM_Q_X_FLAGS), 0, "not read");
    x = ext + 2 * CM_Q_EXT_BYTES;
    CHECK_EQ(get32(x, CM_Q_X_RATE), 0, "a deeper router: unknown");
    CHECK_EQ(get32(x, CM_Q_X_LINK_STATE), CM_Q_UNKNOWN, "unknown state");
    u4.Ports[1].LinkGeneration = 5;
    u4.Ports[1].LinkLane1 = (TBT_LANE_SPEED_GEN4 << 16) | (0x3UL << 20) |
                            (TBT_LANE_STATE_CLD << 26);
    (void)CmQueryExt(ext, CM_ST_MAX_ROUTERS, rec, r, NULL, &u4);
    x = ext + CM_Q_EXT_BYTES;
    CHECK_EQ(get32(x, CM_Q_X_RATE), 40, "Gen 4: 40");
    CHECK_EQ(get32(x, CM_Q_X_WIDTH), 0, "a width code not 1 or 2: unknown");
    CHECK_EQ(get32(x, CM_Q_X_LINK_STATE), TBT_LANE_STATE_CLD, "CLd");
    CHECK_EQ(get32(x, CM_Q_X_FLAGS), CM_Q_XF_RATE_READ, "read");
    CHECK_EQ(CmQueryExt(ext, 1, rec, r, NULL, &u4), 1, "bounded by room");

    q_in(&q, r, n);
    bytes = CmQueryBuild(built, &q);
    CHECK_EQ(bytes, 64 + r + 3 * 32 + strlen(id), "the whole answer");
    CHECK_EQ(get32(built, CM_Q_O_MAGIC), CM_Q_MAGIC, "magic");
    CHECK_EQ(get32(built, CM_Q_O_VERSION), 1, "version");
    CHECK_EQ(get32(built, CM_Q_O_BYTES), bytes, "size");
    CHECK_EQ(get32(built, CM_Q_O_PASS), 0, "no pass in the build");
    CHECK_EQ(get32(built, CM_Q_O_PASS_AGE_MS), CM_Q_UNKNOWN, "no age");
    CHECK_EQ(get32(built, CM_Q_O_LIVE), CM_LIVE_ACTIVE, "live");
    CHECK_EQ(get32(built, CM_Q_O_FLAGS), 0, "no flag");
    CHECK_EQ(get32(built, CM_Q_O_REC_OFFSET), 64, "record at 64");
    CHECK_EQ(get32(built, CM_Q_O_EXT_OFFSET), 64 + r, "extensions after it");
    CHECK_EQ(get32(built, CM_Q_O_EXT_SHAPE), 0x00200003UL, "3 x 32");
    CHECK_EQ(get32(built, CM_Q_O_EPOCH_LO), 0x89ABCDEFUL, "epoch lo");
    CHECK_EQ(get32(built, CM_Q_O_EPOCH_HI), 0x01DC1234UL, "epoch hi");
    CHECK_EQ(get32(built, CM_Q_O_NHI_BDF), 0x0500UL, "05:00.0");
    CHECK_EQ(get32(built, CM_Q_O_ID_OFFSET), 64 + r + 96, "id last");
    CHECK_EQ(get32(built, CM_Q_O_ID_BYTES), strlen(id), "id length");
    CHECK_EQ(get32(built, CM_Q_O_CONTENT_GEN), 0, "no content generation yet");
    CHECK(memcmp(built + 64, rec, r) == 0, "the record byte for byte");
    CHECK(memcmp(built + 64 + r, ext, 96) == 0, "the extensions");
    CHECK(memcmp(built + 64 + r + 96, id, strlen(id)) == 0, "the id, no NUL");

    /* Fewer extensions than routers: the rest unknown. */
    q.ExtCount = 1;
    (void)CmQueryBuild(built, &q);
    e = 64 + r + 2 * 32;
    CHECK_EQ(get32(built, e + CM_Q_X_LINK_STATE), CM_Q_UNKNOWN,
             "a missing extension is unknown");
    /* An id with a byte outside printable ASCII, or too long: none. */
    q.ExtCount = n;
    q.IdBytes = CM_Q_ID_MAX + 1;
    CHECK_EQ(CmQueryBuild(built, &q), 64 + r + 96, "an id too long: none");
    CHECK_EQ(get32(built, CM_Q_O_ID_BYTES), 0, "id bytes 0");
    q.Id = "A\tB";
    q.IdBytes = 3;
    (void)CmQueryBuild(built, &q);
    CHECK_EQ(get32(built, CM_Q_O_ID_BYTES), 0, "a control byte: no id");
}

static void test_icm(void)
{
    CM_STATUS_IN in;
    ULONG r;
    ULONG n;

    memset(&icm, 0, sizeof(icm));
    icm.Devices[2].InUse = TRUE;
    icm.Devices[2].Approved = TRUE;
    icm.Devices[2].EventFlags = TBT_ICM_EVF_DUAL_LANE | TBT_ICM_EVF_SPEED_20G;
    icm.Devices[4].InUse = TRUE;
    icm.Devices[4].RouteValid = TRUE;
    icm.Devices[4].Route.Lo = 0x301;
    base_in(&in, CM_ST_CM_ICM);
    r = CmStatusBuild(rec, &in, &icm, NULL, NULL);
    n = CmQueryExt(ext, CM_ST_MAX_ROUTERS, rec, r, &icm, NULL);
    CHECK_EQ(n, 2, "two ICM devices");
    CHECK_EQ(get32(ext, CM_Q_X_RATE), 20, "the event's 20 Gb/s");
    CHECK_EQ(get32(ext, CM_Q_X_WIDTH), 2, "dual lane");
    CHECK_EQ(get32(ext, CM_Q_X_FLAGS), 0, "the firmware's, not read here");
    CHECK_EQ(get32(ext, CM_Q_X_LINK_STATE), CM_Q_UNKNOWN, "no lane state");
    CHECK_EQ(get32(ext, CM_Q_X_GEN_RAW), CM_Q_UNKNOWN, "no speed code");
    CHECK_EQ(get32(ext + 32, CM_Q_X_RATE), 10, "flags clear: 10 Gb/s");
    CHECK_EQ(get32(ext + 32, CM_Q_X_WIDTH), 1, "single lane");
    /* The table changed under a stale record: the route disagrees. */
    icm.Devices[4].Route.Lo = 0x501;
    (void)CmQueryExt(ext, CM_ST_MAX_ROUTERS, rec, r, &icm, NULL);
    CHECK_EQ(get32(ext + 32, CM_Q_X_RATE), 0, "a mismatched entry: unknown");
    /* The ICM's tables are not asked under the USB4 record, nor u4's under
     * the ICM's. */
    (void)CmQueryExt(ext, CM_ST_MAX_ROUTERS, rec, r, NULL, &u4);
    CHECK_EQ(get32(ext, CM_Q_X_RATE), 0, "no ICM table: unknown");
}

/* The sizing answers, golden header bytes included. */
static void test_answer(void)
{
    CM_STATUS_IN in;
    CM_QUERY_IN q;
    ULONG r;
    ULONG bytes;
    ULONG info;
    static const UCHAR golden[64] = {
        'X', 'C', 'M', 'Q', 1, 0, 0, 0,
        0, 0, 0, 0, 7, 0, 0, 0,             /* bytes patched below; pass 7 */
        0xE8, 0x03, 0, 0, 1, 0, 0, 0,       /* 1000 ms; ACTIVE */
        0x05, 0, 0, 0, 64, 0, 0, 0,         /* INERT | STOPPING; record */
        192, 0, 0, 0, 0, 0, 0, 0,           /* ext at 64 + 128; 0 x 32 */
        0xEF, 0xCD, 0xAB, 0x89, 0x34, 0x12, 0xDC, 0x01,
        0x00, 0x05, 0, 0, 192, 0, 0, 0,     /* 05:00.0; id at 192 */
        0, 0, 0, 0, 0, 0, 0, 0              /* id bytes patched; content gen */
    };
    UCHAR want[64];

    memset(&u4, 0, sizeof(u4));
    base_in(&in, CM_ST_CM_USB4);
    r = CmStatusBuild(rec, &in, NULL, &u4, NULL);
    CHECK_EQ(r, 128, "no router: the header alone");
    q_in(&q, r, 0);
    q.Inert = 1;
    bytes = CmQueryBuild(built, &q);
    CHECK_EQ(bytes, 64 + 128 + strlen(id), "no extension");
    memcpy(want, golden, 64);
    put32(want, CM_Q_O_BYTES, bytes);
    want[CM_Q_O_EXT_SHAPE + 2] = 32;
    put32(want, CM_Q_O_ID_BYTES, (ULONG)strlen(id));

    memset(out, 0xCC, sizeof(out));
    CHECK_EQ(CmQueryAnswer(out, 63, built, bytes, 7, 1000, TRUE, &info),
             CM_Q_ANS_TOO_SMALL, "63 bytes: too small");
    CHECK_EQ(info, 0, "nothing returned");
    CHECK_EQ(out[0], 0xCC, "nothing written");
    CHECK_EQ(CmQueryAnswer(out, 64, built, bytes, 7, 1000, TRUE, &info),
             CM_Q_ANS_HEADER, "64 bytes: the header");
    CHECK_EQ(info, 64, "64 returned");
    CHECK(memcmp(out, want, 64) == 0, "the golden header");
    CHECK_EQ(out[64], 0xCC, "nothing past it");
    CHECK_EQ(CmQueryAnswer(out, bytes - 1, built, bytes, 7, 1000, TRUE, &info),
             CM_Q_ANS_HEADER, "one short: the header");
    CHECK_EQ(get32(out, CM_Q_O_BYTES), bytes, "the size needed");
    CHECK_EQ(CmQueryAnswer(out, bytes, built, bytes, 7, 1000, TRUE, &info),
             CM_Q_ANS_WHOLE, "exactly: whole");
    CHECK_EQ(info, bytes, "all of it");
    CHECK(memcmp(out, want, 64) == 0, "the same header");
    CHECK(memcmp(out + 64, rec, 128) == 0, "the record");
    CHECK_EQ(CmQueryAnswer(out, sizeof(out), built, bytes, 0, 1000, FALSE,
                           &info),
             CM_Q_ANS_WHOLE, "a larger buffer: whole");
    CHECK_EQ(get32(out, CM_Q_O_PASS_AGE_MS), CM_Q_UNKNOWN,
             "before the first pass the age is unknown");
    CHECK_EQ(get32(out, CM_Q_O_FLAGS), CM_Q_F_INERT, "not stopping");
    CHECK_EQ(get32(built, CM_Q_O_PASS), 0, "the build is not changed");
}

/* The router-count edge: 24 routers, a record flagged truncated, and a
 * record whose size disagrees with its count. */
static void test_edges(void)
{
    CM_STATUS_IN in;
    CM_QUERY_IN q;
    ULONG r;
    ULONG i;
    ULONG n;
    ULONG bytes;
    ULONG info;

    memset(&icm, 0, sizeof(icm));
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        icm.Devices[i].InUse = TRUE;
        icm.Devices[i].EventFlags = TBT_ICM_EVF_SPEED_20G;
    }
    base_in(&in, CM_ST_CM_ICM);
    r = CmStatusBuild(rec, &in, &icm, NULL, NULL);
    /* Fill the record to the maximum by hand, flagged truncated. */
    for (i = r; i < CM_ST_MAX_BYTES; i++) {
        rec[i] = (UCHAR)i;
    }
    put32(rec, CM_ST_O_ROUTER_COUNT, CM_ST_MAX_ROUTERS);
    put32(rec, CM_ST_O_BYTES, CM_ST_MAX_BYTES);
    put32(rec, CM_ST_O_FLAGS, CM_ST_F_TRUNCATED);
    n = CmQueryExt(ext, CM_ST_MAX_ROUTERS, rec, CM_ST_MAX_BYTES, &icm, NULL);
    CHECK_EQ(n, CM_ST_MAX_ROUTERS, "24 extensions");
    q_in(&q, CM_ST_MAX_BYTES, n);
    q.IdBytes = CM_Q_ID_MAX;
    q.Id = "PCI\\VEN_8086&DEV_15D2&SUBSYS_00000000&REV_02\\4&1A2B3C&0&0008"
           "0123456789012345678901234567890123456789012345678901234567890123"
           "4567890123456789012345678901234567890123456789012345678901234567"
           "890123456789";
    CHECK_EQ(strlen(q.Id), CM_Q_ID_MAX, "a 200-byte id");
    bytes = CmQueryBuild(built, &q);
    CHECK_EQ(bytes, CM_Q_BUILD_MAX, "the largest answer");
    CHECK_EQ(get32(built, CM_Q_O_FLAGS), CM_Q_F_TRUNCATED, "truncated");
    CHECK_EQ(get32(built, CM_Q_O_EXT_SHAPE), 0x00200018UL, "24 x 32");
    CHECK(memcmp(built + 64, rec, CM_ST_MAX_BYTES) == 0, "the whole record");
    CHECK_EQ(CmQueryAnswer(out, CM_Q_ASK_BYTES, built, bytes, 1, 0, FALSE,
                           &info),
             CM_Q_ANS_WHOLE, "the first ask holds it");

    /* The size field disagrees with the count: the header alone. */
    put32(rec, CM_ST_O_ROUTER_COUNT, 3);
    CHECK_EQ(CmQueryExt(ext, CM_ST_MAX_ROUTERS, rec, CM_ST_MAX_BYTES, &icm,
                        NULL),
             0, "no extension for a malformed record");
    q_in(&q, CM_ST_MAX_BYTES, n);
    bytes = CmQueryBuild(built, &q);
    CHECK_EQ(bytes, 64 + 128 + strlen(id), "cut to the header");
    CHECK_EQ(get32(built + 64, CM_ST_O_ROUTER_COUNT), 0, "count 0");
    CHECK_EQ(get32(built + 64, CM_ST_O_BYTES), 128, "its size 128");
    CHECK_EQ(get32(built, CM_Q_O_EXT_SHAPE), 0x00200000UL, "no extension");
    /* No record at all. */
    q.Record = NULL;
    bytes = CmQueryBuild(built, &q);
    CHECK_EQ(bytes, 64 + strlen(id), "the header and the id");
    CHECK_EQ(get32(built, CM_Q_O_REC_OFFSET), 0, "no record offset");
}

/* The content generation: the first is 1; the registry-only fields of
 * the record, the pass and its age move nothing; a router change, the
 * live state or a flag does. */
static UCHAR prevAns[CM_Q_BUILD_MAX];

static void test_content_gen(void)
{
    CM_STATUS_IN in;
    CM_QUERY_IN q;
    ULONG r;
    ULONG b;
    ULONG pb;

    memset(&u4, 0, sizeof(u4));
    u4.PortCount = 1;
    u4.Ports[0].Index = 1;
    u4.Ports[0].State = TBT_U4_UP;
    u4.Ports[0].Dev.Present = TRUE;
    base_in(&in, CM_ST_CM_USB4);
    r = CmStatusBuild(rec, &in, NULL, &u4, NULL);
    q_in(&q, r, 0);
    pb = CmQueryBuild(prevAns, &q);
    CHECK_EQ(get32(prevAns, CM_Q_O_CONTENT_GEN), 0, "the build leaves it 0");
    CHECK_EQ(CmQueryContentGen(prevAns, pb, NULL, 0), 1, "the first is 1");
    CHECK_EQ(get32(prevAns, CM_Q_O_CONTENT_GEN), 1, "written");

    CmStatusStamp(rec, 9, 1234, 0x11111111UL, 0x01DC0000UL);
    put32(rec, CM_ST_O_ECHO, 0x5EED);
    b = CmQueryBuild(built, &q);
    put32(built, CM_Q_O_PASS, 5);
    put32(built, CM_Q_O_PASS_AGE_MS, 77);
    CHECK_EQ(CmQueryContentGen(built, b, prevAns, pb), 1,
             "stamp, echo, pass and age: no change");

    q.Live = CM_LIVE_SUSPECT;
    b = CmQueryBuild(built, &q);
    CHECK_EQ(CmQueryContentGen(built, b, prevAns, pb), 2, "the live state");
    memcpy(prevAns, built, b);
    pb = b;
    q.Inert = 1;
    b = CmQueryBuild(built, &q);
    CHECK_EQ(CmQueryContentGen(built, b, prevAns, pb), 3, "a flag");
    memcpy(prevAns, built, b);
    pb = b;
    u4.Ports[0].State = TBT_U4_HELD;
    r = CmStatusBuild(rec, &in, NULL, &u4, NULL);
    q_in(&q, r, 0);
    q.Live = CM_LIVE_SUSPECT;
    q.Inert = 1;
    b = CmQueryBuild(built, &q);
    CHECK_EQ(CmQueryContentGen(built, b, prevAns, pb), 4, "a router entry");
    memcpy(prevAns, built, b);
    pb = b;
    u4.PortCount = 0;
    r = CmStatusBuild(rec, &in, NULL, &u4, NULL);
    q_in(&q, r, 0);
    b = CmQueryBuild(built, &q);
    CHECK_EQ(CmQueryContentGen(built, b, prevAns, pb), 5, "a router left");
    put32(prevAns, CM_Q_O_CONTENT_GEN, 0xFFFFFFFFUL);
    CHECK_EQ(CmQueryContentGen(built, b, prevAns, pb), 1, "past 0 to 1");
}

static ULONG wide(USHORT *w, const char *a)
{
    ULONG i;

    for (i = 0; a[i] != 0; i++) {
        w[i] = (USHORT)(unsigned char)a[i];
    }
    return i;
}

static void test_id_from_link(void)
{
    USHORT w[160];
    char got[CM_Q_ID_MAX];
    ULONG n;
    const char *want =
        "PCI\\VEN_1B36&DEV_00F4&SUBSYS_11001AF4&REV_01\\3&13C0B0C5&0&20";
    const char *want98 = "PCI\\VEN_8086&DEV_15D2\\BUS_05&DEV_00&FUNC_00";

    n = CmQueryIdFromLink(w, wide(w, "\\?\?\\PCI#VEN_1B36&DEV_00F4&SUBSYS_11001AF4&REV_01#3&13C0B0C5&0&20#{e3e4ef80-9744-4323-a489-066e78ad2fa9}"),
                          got, sizeof(got));
    CHECK_EQ(n, strlen(want), "the instance id's length");
    CHECK(memcmp(got, want, n) == 0, "the instance id, # read as \\");
    n = CmQueryIdFromLink(w, wide(w, "\\\\?\\PCI#VEN_8086&DEV_15D2#BUS_05&DEV_00&FUNC_00#{x}"),
                          got, sizeof(got));
    CHECK(n == strlen(want98) && memcmp(got, want98, n) == 0,
          "another prefix");
    CHECK_EQ(CmQueryIdFromLink(w, wide(w, "\\?\?\\PCI"), got, sizeof(got)), 0,
             "no #: none");
    CHECK_EQ(CmQueryIdFromLink(w, wide(w, "\\?\?\\PCI#{x}"), got, sizeof(got)),
             0, "one #: none");
    CHECK_EQ(CmQueryIdFromLink(w, wide(w, "\\?\?\\PCI#VEN#1#{x}"), got, 5), 0,
             "longer than room: none");
    n = wide(w, "\\?\?\\PCI#VEN#1#{x}");
    w[9] = 0xE9;
    CHECK_EQ(CmQueryIdFromLink(w, n, got, sizeof(got)), 0,
             "a character outside ASCII: none");
}

static void test_age(void)
{
    CHECK_EQ(CmQueryAgeMs(100, 100, 156250), 0, "same tick");
    CHECK_EQ(CmQueryAgeMs(164, 100, 156250), 1000, "64 ticks of 15.625 ms");
    CHECK_EQ(CmQueryAgeMs(5, 0xFFFFFFFBUL, 100000), 100, "across the wrap");
    CHECK_EQ(CmQueryAgeMs(0xFFFFFFFFUL, 0, 156250), CM_Q_UNKNOWN - 1UL,
             "held below unknown");
    CHECK_EQ(CmQueryAgeMs(10, 0, 0), 0, "no period");
    CHECK_EQ(CmQueryAgeMs(3, 0, 10000), 3, "1 ms ticks");
}

int main(void)
{
    test_layout();
    test_u4();
    test_icm();
    test_answer();
    test_edges();
    test_content_gen();
    test_id_from_link();
    test_age();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
