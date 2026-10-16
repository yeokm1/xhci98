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
    CHECK_EQ(CmQueryAgeMs(0x7FFFFFFFUL, 0, 156250), CM_Q_UNKNOWN - 1UL,
             "held below unknown");
    CHECK_EQ(CmQueryAgeMs(10, 0, 0), 0, "no period");
    CHECK_EQ(CmQueryAgeMs(7810, 0, 550000), 429550,
             "55 ms ticks after a long stall: no 32-bit wrap");
    CHECK_EQ(CmQueryAgeMs(256, 0, 549254), 14060,
             "a tick's fraction kept: 256 ticks of 54.9 ms");
    CHECK_EQ(CmQueryAgeMs(3, 0, 10000), 3, "1 ms ticks");
    /* Coarse periods (10 to 55 ms): no overflow in r x incr. */
    CHECK_EQ(CmQueryAgeMs(9999, 0, 550000), 549945UL, "55 ms ticks, r large");
    CHECK_EQ(CmQueryAgeMs(10000, 0, 550000), 550000UL, "55 ms, 10^4 ticks");
    CHECK_EQ(CmQueryAgeMs(20003, 0, 100144), 200318UL,
             "10.0144 ms ticks, rounded down");
    CHECK_EQ(CmQueryAgeMs(78090314UL, 0, 550000), 4294967270UL,
             "55 ms: the last value below the hold");
    CHECK_EQ(CmQueryAgeMs(78090315UL, 0, 550000), CM_Q_UNKNOWN - 1UL,
             "55 ms: the first one held");
    /* The clock read before the publication it measures: 0, not 49 days. */
    CHECK_EQ(CmQueryAgeMs(100, 101, 156250), 0, "now before then: 0");
    CHECK_EQ(CmQueryAgeMs(0xFFFFFFF0UL, 5, 156250), 0, "before, across the wrap");
    CHECK_EQ(CmQueryAgeMs(5, 0xFFFFFFF0UL, 100000), 210, "after, across the wrap");
}

/* The IOCTL's order: the copy under the lock with the age unknown, then
 * the clock read and the age written; the stopping flag's generation. */
static void test_order_and_bump(void)
{
    CM_STATUS_IN in;
    CM_QUERY_IN q;
    ULONG r;
    ULONG b;
    ULONG info;

    memset(&u4, 0, sizeof(u4));
    base_in(&in, CM_ST_CM_USB4);
    r = CmStatusBuild(rec, &in, NULL, &u4, NULL);
    q_in(&q, r, 0);
    b = CmQueryBuild(built, &q);
    (void)CmQueryContentGen(built, b, NULL, 0);
    CHECK_EQ(CmQueryAnswer(out, sizeof(out), built, b, 9, CM_Q_UNKNOWN, FALSE,
                           &info),
             CM_Q_ANS_WHOLE, "whole");
    CHECK_EQ(get32(out, CM_Q_O_PASS_AGE_MS), CM_Q_UNKNOWN,
             "no age under the lock");
    CmQuerySetAge(out, info, 9, CmQueryAgeMs(110, 100, 100000));
    CHECK_EQ(get32(out, CM_Q_O_PASS_AGE_MS), 100, "the age after it");
    CmQuerySetAge(out, info, 0, 100);
    CHECK_EQ(get32(out, CM_Q_O_PASS_AGE_MS), CM_Q_UNKNOWN, "no pass: unknown");
    memset(out, 0xCC, 64);
    CmQuerySetAge(out, 0, 9, 100);
    CHECK_EQ(out[CM_Q_O_PASS_AGE_MS], 0xCC, "nothing returned: nothing written");

    /* Stopping: one past the thread's sequence; a later publication of
     * unchanged content, bumped the same way, does not go back. */
    CHECK_EQ(get32(built, CM_Q_O_CONTENT_GEN), 1, "the thread's 1");
    memcpy(out, built, b);
    CHECK_EQ(CmQueryBumpGen(out, b), 2, "stopping: 2");
    CHECK_EQ(CmQueryContentGen(built, b, built, b), 1, "the thread's next: 1");
    memcpy(out, built, b);
    CHECK_EQ(CmQueryBumpGen(out, b), 2, "published while stopping: still 2");
    put32(out, CM_Q_O_CONTENT_GEN, 0xFFFFFFFFUL);
    CHECK_EQ(CmQueryBumpGen(out, b), 1, "past 0 to 1");
    CHECK_EQ(CmQueryBumpGen(out, 63), 0, "no header: nothing");
}

/* ---- The log answer (record 25 section 4.5) ---------------------------- */

static UCHAR ring[CM_QL_LOG_MAX];
static UCHAR logOut[CM_QL_MAX_BYTES + 16];
static UCHAR ctrText[CM_QL_CTR_MAX];

/* Appends text to the modelled ring as XhciLogAppend's byte put does. */
static void ring_put(ULONG *head, ULONG *used, ULONG *dropped,
                     const char *text)
{
    while (*text != 0) {
        ring[*head] = (UCHAR)*text++;
        *head = (*head + 1) & (CM_QL_LOG_MAX - 1);
        if (*used < CM_QL_LOG_MAX) {
            (*used)++;
        } else {
            (*dropped)++;
        }
    }
}

static void log_in(CM_QLOG_IN *in, ULONG head, ULONG used, ULONG dropped)
{
    memset(in, 0, sizeof(*in));
    in->Flags = CM_QL_F_RECORDING;
    in->Verbosity = 2;
    in->VerbosityRead = 2;
    in->EpochLo = 0x89ABCDEFUL;
    in->EpochHi = 0x01DC1234UL;
    in->Ctr = ctrText;
    in->Ring = ring;
    in->RingBytes = CM_QL_LOG_MAX;
    in->Head = head;
    in->Used = used;
    in->Dropped = dropped;
    in->Appends = 7;
    in->Pass = 5;
}

static void test_log_layout(void)
{
    CHECK_EQ(CM_QL_IOCTL, (0x22UL << 16) | (0xA41UL << 2), "the log code");
    CHECK_EQ(CM_QL_MAGIC, 'X' | ('C' << 8) | ('M' << 16) | ((ULONG)'L' << 24),
             "'XCML' in byte order");
    CHECK_EQ(CM_QL_O_PASS_AGE_MS + 4, CM_QL_HDR_BYTES, "the age ends it");
    CHECK_EQ(CM_QL_MAX_BYTES, 64 + 8192 + 16384, "the largest log answer");
    CHECK(CM_QL_MAX_BYTES <= CM_QL_ASK_BYTES, "one ask holds the largest");
    CHECK(CM_QL_ASK_BYTES <= CM_Q_MAX_BYTES, "within the reader's bound");
}

static void test_log_lines(void)
{
    static const char *const names[] = { "cm.passes", "cm.polls", "x y" };
    static const ULONG values[] = { 0x1FUL, 0xDEADBEEFUL, 1 };
    UCHAR buf[64];
    ULONG at = 0;
    ULONG cut = 0;

    memset(buf, 0xEE, sizeof(buf));
    CHECK_EQ(CmQueryLines(buf, sizeof(buf), &at, names, values, 3, &cut), 3,
             "three lines");
    CHECK_EQ(cut, 0, "not cut");
    CHECK(memcmp(buf, "cm.passes=0000001F\r\ncm.polls=DEADBEEF\r\nx.y=00000001\r\n",
                 at) == 0, "the format; a space written '.'");
    CHECK_EQ(at, 20 + 19 + 14, "the length");
    /* Room for one line and part of the next: the next is not written. */
    at = 0;
    CHECK_EQ(CmQueryLines(buf, 25, &at, names, values, 3, &cut), 1,
             "one fits");
    CHECK_EQ(cut, 1, "cut");
    CHECK_EQ(at, 20, "nothing of the second");
    /* Once cut, a shorter name after it is not written either. */
    CHECK_EQ(CmQueryLines(buf, 64, &at, names + 2, values + 2, 1, &cut), 0,
             "nothing after a cut");
    CHECK_EQ(at, 20, "still");
    at = 30;
    cut = 0;
    CHECK_EQ(CmQueryLines(buf, 20, &at, names, values, 1, &cut), 0,
             "at past room");
    CHECK_EQ(cut, 1, "cut");
}

static void test_log_answer(void)
{
    CM_QLOG_IN in;
    ULONG head = 0;
    ULONG used = 0;
    ULONG dropped = 0;
    ULONG info;
    ULONG whole;
    const char *recs = "cm.start=00000001\r\ncm.stage=00000003\r\n";

    memcpy(ctrText, "cm.passes=00000005\r\n", 20);
    ring_put(&head, &used, &dropped, recs);
    log_in(&in, head, used, dropped);
    in.CtrBytes = 20;
    whole = 64 + 20 + 38;
    CHECK_EQ(CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info),
             CM_Q_ANS_WHOLE, "whole");
    CHECK_EQ(info, whole, "its length");
    CHECK_EQ(get32(logOut, CM_QL_O_MAGIC), CM_QL_MAGIC, "magic");
    CHECK_EQ(get32(logOut, CM_QL_O_VERSION), 1, "version");
    CHECK_EQ(get32(logOut, CM_QL_O_BYTES), whole, "size");
    CHECK_EQ(get32(logOut, CM_QL_O_FLAGS), CM_QL_F_RECORDING, "flags");
    CHECK_EQ(get32(logOut, CM_QL_O_VERBOSITY), 2, "verbosity");
    CHECK_EQ(get32(logOut, CM_QL_O_EPOCH_LO), 0x89ABCDEFUL, "epoch lo");
    CHECK_EQ(get32(logOut, CM_QL_O_EPOCH_HI), 0x01DC1234UL, "epoch hi");
    CHECK_EQ(get32(logOut, CM_QL_O_CTR_OFFSET), 64, "counters at 64");
    CHECK_EQ(get32(logOut, CM_QL_O_CTR_BYTES), 20, "counters' length");
    CHECK_EQ(get32(logOut, CM_QL_O_LOG_OFFSET), 84, "the log after them");
    CHECK_EQ(get32(logOut, CM_QL_O_LOG_BYTES), 38, "the log's length");
    CHECK_EQ(get32(logOut, CM_QL_O_APPENDS), 7, "appends");
    CHECK_EQ(get32(logOut, CM_QL_O_PASS), 5, "pass");
    CHECK_EQ(get32(logOut, CM_QL_O_PASS_AGE_MS), CM_Q_UNKNOWN, "age unset");
    CHECK(memcmp(logOut + 64, ctrText, 20) == 0, "the counters");
    CHECK(memcmp(logOut + 84, recs, 38) == 0, "the log, oldest first");
    CmQueryLogSetAge(logOut, info, 5, 1234);
    CHECK_EQ(get32(logOut, CM_QL_O_PASS_AGE_MS), 1234, "age");
    CmQueryLogSetAge(logOut, info, 0, 1234);
    CHECK_EQ(get32(logOut, CM_QL_O_PASS_AGE_MS), CM_Q_UNKNOWN, "no pass");

    /* Sizing. */
    CHECK_EQ(CmQueryLogAnswer(logOut, 63, &in, &info), CM_Q_ANS_TOO_SMALL,
             "under the header");
    CHECK_EQ(info, 0, "nothing");
    memset(logOut, 0xEE, sizeof(logOut));
    CHECK_EQ(CmQueryLogAnswer(logOut, whole - 1, &in, &info),
             CM_Q_ANS_HEADER, "one short: the header");
    CHECK_EQ(info, 64, "the header alone");
    CHECK_EQ(get32(logOut, CM_QL_O_BYTES), whole, "the size to ask");
    CHECK_EQ(logOut[64], 0xEE, "nothing past it");

    /* The ring is left as it was: a second answer is the same. */
    CHECK_EQ(CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info),
             CM_Q_ANS_WHOLE, "again");
    CHECK(memcmp(logOut + 84, recs, 38) == 0, "the same log");

    /* Flags passed through; the builder's own not taken from the input. */
    in.Flags = CM_QL_F_RECORDING | CM_QL_F_REFUSED | CM_QL_F_STOPPING |
               CM_QL_F_INERT | CM_QL_F_WITHHELD | CM_QL_F_LOG_CUT;
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK_EQ(get32(logOut, CM_QL_O_FLAGS), CM_QL_F_RECORDING |
             CM_QL_F_REFUSED | CM_QL_F_STOPPING | CM_QL_F_INERT,
             "input flags; withheld and cut are the builder's");
    in.CtrCut = 1;
    in.Flags = 0;
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK_EQ(get32(logOut, CM_QL_O_FLAGS), CM_QL_F_CTR_CUT, "counters cut");
    in.CtrBytes = CM_QL_CTR_MAX + 1;
    in.CtrCut = 0;
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK_EQ(get32(logOut, CM_QL_O_CTR_BYTES), 0, "too long: none");
    CHECK_EQ(get32(logOut, CM_QL_O_FLAGS), CM_QL_F_CTR_CUT, "and cut");

    /* A ring of an unknown shape is an empty log. */
    log_in(&in, head, used, dropped);
    in.RingBytes = 1000;
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK_EQ(get32(logOut, CM_QL_O_LOG_BYTES), 0, "not a power of two");
    in.RingBytes = CM_QL_LOG_MAX * 2;
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK_EQ(get32(logOut, CM_QL_O_LOG_BYTES), 0, "too large");
    log_in(&in, head, CM_QL_LOG_MAX + 1, dropped);
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK_EQ(get32(logOut, CM_QL_O_LOG_BYTES), 0, "used past the size");
    log_in(&in, head, used, dropped);
    in.Ring = NULL;
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK_EQ(get32(logOut, CM_QL_O_LOG_BYTES), 0, "no ring");
    CHECK_EQ(info, 64, "the header alone, whole");
}

static void test_log_text(void)
{
    CM_QLOG_IN in;
    ULONG head = 0;
    ULONG used = 0;
    ULONG dropped = 0;
    ULONG info;
    ULONG n;
    ULONG i;
    const UCHAR *t;
    char line[32];

    /* Bytes outside printable ASCII, CR and LF are written '.'. */
    ring_put(&head, &used, &dropped, "a\tb\x01\x7F\x80=00000001\r\n");
    log_in(&in, head, used, dropped);
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK(memcmp(logOut + 64, "a.b...=00000001\r\n", 17) == 0,
          "control and high bytes replaced");

    /* An address record's value withheld, its neighbours not; a label
     * that only begins with one is not withheld. */
    head = used = dropped = 0;
    ring_put(&head, &used, &dropped,
             "cm.live.state=00000101\r\ncm.live.bar-changed=E0000004\r\n"
             "cm.reenum.9x.gone.superseded=C1234560\r\n"
             "cm.reenum.9x.gone.id-changed=C1234570\r\n"
             "cm.live.bar-changed.x=00000002\r\n");
    log_in(&in, head, used, dropped);
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    t = logOut + 64;
    CHECK(memcmp(t, "cm.live.state=00000101\r\ncm.live.bar-changed=--------\r\n"
                    "cm.reenum.9x.gone.superseded=--------\r\n"
                    "cm.reenum.9x.gone.id-changed=--------\r\n"
                    "cm.live.bar-changed.x=00000002\r\n", used) == 0,
          "the three address records withheld");
    CHECK(get32(logOut, CM_QL_O_FLAGS) & CM_QL_F_WITHHELD, "withheld flag");
    CHECK_EQ(get32(logOut, CM_QL_O_LOG_BYTES), used, "same length");
    CHECK(CmQueryLogAddress((const UCHAR *)"cm.live.bar-changed", 19),
          "the label");
    CHECK(!CmQueryLogAddress((const UCHAR *)"cm.live.bar-change", 18),
          "a prefix of it");
    CHECK(!CmQueryLogAddress((const UCHAR *)"cm.live.bar-changedX", 20),
          "longer");

    /* Wrapped: the ring full, bytes dropped; the oldest bytes are a tail
     * ("...ar-changed=E0000004") whose label was overwritten - cut. */
    head = used = dropped = 0;
    for (i = 0; used < CM_QL_LOG_MAX || dropped < 40; i++) {
        sprintf(line, "cm.live.bar-changed=%08lX\r\n", (unsigned long)i);
        ring_put(&head, &used, &dropped, line);
    }
    log_in(&in, head, used, dropped);
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    n = get32(logOut, CM_QL_O_LOG_BYTES);
    t = logOut + 64;
    CHECK(get32(logOut, CM_QL_O_FLAGS) & CM_QL_F_LOG_CUT, "cut flag");
    CHECK(n < CM_QL_LOG_MAX && n > CM_QL_LOG_MAX - 32, "one partial dropped");
    CHECK(memcmp(t, "cm.live.bar-changed=--------\r\n", 30) == 0,
          "starts at a whole record, withheld");
    for (i = 0; i < n; i++) {
        if (t[i] >= '0' && t[i] <= '9') {
            break;
        }
    }
    CHECK_EQ(i, n, "no digit of any address left");
    CHECK_EQ(t[n - 1], '\n', "ends with a whole record");

    /* A checked flavour's drain: a chunk may end inside a record, so the
     * first line is dropped though nothing was overwritten. */
    head = used = dropped = 0;
    ring_put(&head, &used, &dropped, "nged=E0000004\r\ncm.stop=00000000\r\n");
    log_in(&in, head, used, dropped);
    in.Flags |= CM_QL_F_EMITTED;
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK_EQ(get32(logOut, CM_QL_O_LOG_BYTES), 18, "the tail dropped");
    CHECK(memcmp(logOut + 64, "cm.stop=00000000\r\n", 18) == 0, "the rest");
    CHECK_EQ(get32(logOut, CM_QL_O_FLAGS),
             CM_QL_F_RECORDING | CM_QL_F_EMITTED | CM_QL_F_LOG_CUT, "flags");
    /* No LF at all: nothing shown. */
    head = used = dropped = 0;
    ring_put(&head, &used, &dropped, "nged=E0000004");
    log_in(&in, head, used, dropped);
    in.Flags |= CM_QL_F_EMITTED;
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK_EQ(get32(logOut, CM_QL_O_LOG_BYTES), 0, "a lone tail: none");
    /* An empty ring. */
    log_in(&in, 0, 0, 0);
    (VOID)CmQueryLogAnswer(logOut, sizeof(logOut), &in, &info);
    CHECK_EQ(get32(logOut, CM_QL_O_LOG_BYTES), 0, "empty");
    CHECK_EQ(info, 64, "the header");
}

/* The DebugView emission's line rule (cm_log.c): below level 4 an
 * address record's value is withheld; at 4 the line is as recorded. */
static void test_log_line(void)
{
    UCHAR l[64];

    memcpy(l, "cm.live.bar-changed=E0000004\r\n", 30);
    CHECK_EQ(CmQueryLogLine(l, 30, 2), 1, "level 2: withheld");
    CHECK(memcmp(l, "cm.live.bar-changed=--------\r\n", 30) == 0, "dashes");
    memcpy(l, "cm.reenum.9x.gone.id-changed=C1234570\r\n", 39);
    CHECK_EQ(CmQueryLogLine(l, 39, 3), 1, "level 3: withheld");
    CHECK(memcmp(l, "cm.reenum.9x.gone.id-changed=--------\r\n", 39) == 0,
          "dashes");
    memcpy(l, "cm.reenum.9x.gone.superseded=C1234560\r\n", 39);
    CHECK_EQ(CmQueryLogLine(l, 39, 2), 1, "superseded: withheld");
    CHECK(memcmp(l, "cm.reenum.9x.gone.superseded=--------\r\n", 39) == 0,
          "dashes");
    memcpy(l, "cm.live.bar-changed=E0000004\r\n", 30);
    CHECK_EQ(CmQueryLogLine(l, 30, 4), 0, "level 4: as recorded");
    CHECK(memcmp(l, "cm.live.bar-changed=E0000004\r\n", 30) == 0, "kept");
    memcpy(l, "cm.reenum.9x.gone.superseded=C1234560\r\n", 39);
    CHECK_EQ(CmQueryLogLine(l, 39, 4), 0, "level 4: superseded kept");
    CHECK(memcmp(l, "cm.reenum.9x.gone.superseded=C1234560\r\n", 39) == 0,
          "kept");
    memcpy(l, "cm.reenum.9x.gone.id-changed=C1234570\r\n", 39);
    CHECK_EQ(CmQueryLogLine(l, 39, 4), 0, "level 4: id-changed kept");
    CHECK(memcmp(l, "cm.reenum.9x.gone.id-changed=C1234570\r\n", 39) == 0,
          "kept");
    memcpy(l, "cm.passes=00000005\r\n", 20);
    CHECK_EQ(CmQueryLogLine(l, 20, 2), 0, "another record");
    CHECK(memcmp(l, "cm.passes=00000005\r\n", 20) == 0, "untouched");
    memcpy(l, "cm.live.bar-changed=E00", 23);
    CHECK_EQ(CmQueryLogLine(l, 23, 0), 1, "a short tail still withheld");
    CHECK(memcmp(l, "cm.live.bar-changed=---", 23) == 0, "within it");
}

/* The emission of a drained ring (CmQueryLogEmitLines). */
static UCHAR emitted[1024];
static ULONG emittedBytes;
static ULONG emitCalls;
static ULONG emitLen[8];

static VOID emit_collect(PVOID context, const UCHAR *line, ULONG n)
{
    (VOID)context;
    memcpy(emitted + emittedBytes, line, n);
    emittedBytes += n;
    if (emitCalls < 8) {
        emitLen[emitCalls] = n;
    }
    emitCalls++;
}

static ULONG emit_run(const char *text, ULONG partial, ULONG verbosity)
{
    static UCHAR buf[1024];
    ULONG n = (ULONG)strlen(text);

    memcpy(buf, text, n);
    emittedBytes = 0;
    emitCalls = 0;
    memset(emitted, 0, sizeof(emitted));
    return CmQueryLogEmitLines(buf, n, partial, verbosity, emit_collect, NULL);
}

static void test_log_emit(void)
{
    const char *recs = "cm.start=00000001\r\ncm.live.bar-changed=E0000004\r\n"
                       "cm.reenum.9x.gone.superseded=C1234560\r\n"
                       "cm.reenum.9x.gone.id-changed=C1234570\r\n";
    UCHAR big[300];
    ULONG i;

    CHECK_EQ(emit_run(recs, 0, 2), 4, "four lines");
    CHECK_EQ(emitCalls, 4, "one call a line");
    CHECK_EQ(emittedBytes, strlen(recs), "every byte, same length");
    CHECK(memcmp(emitted, "cm.start=00000001\r\ncm.live.bar-changed=--------\r\n"
                          "cm.reenum.9x.gone.superseded=--------\r\n"
                          "cm.reenum.9x.gone.id-changed=--------\r\n",
                 emittedBytes) == 0, "level 2: the three withheld");
    CHECK(strstr((const char *)emitted, "E0000004") == NULL, "no address");
    CHECK_EQ(emit_run(recs, 0, 4), 4, "level 4");
    CHECK_EQ(emittedBytes, strlen(recs), "level 4: every byte");
    CHECK(memcmp(emitted, recs, emittedBytes) == 0, "level 4: as recorded");
    /* A wrapped ring: the first line is a tail with its label cut. */
    CHECK_EQ(emit_run("nged=E0000004\r\ncm.stop=00000000\r\n", 1, 2), 1,
             "the tail dropped");
    CHECK(memcmp(emitted, "cm.stop=00000000\r\n", 18) == 0 && emittedBytes == 18,
          "only the whole record");
    CHECK_EQ(emit_run("nged=E0000004", 1, 2), 0, "a lone tail: nothing");
    CHECK_EQ(emittedBytes, 0, "no byte of it");
    CHECK_EQ(emit_run("cm.a=00000001\r\ncm.b=00000002", 0, 2), 2,
             "the unterminated end is a line");
    CHECK_EQ(emittedBytes, 28, "every byte of both");
    CHECK(memcmp(emitted, "cm.a=00000001\r\ncm.b=00000002", 28) == 0,
          "as they were");
    /* A line past the print buffer goes in pieces, in order. */
    for (i = 0; i < 299; i++) {
        big[i] = (UCHAR)('A' + i % 26);
    }
    big[299] = 0;
    CHECK_EQ(emit_run((const char *)big, 0, 2), 1, "one long line");
    CHECK_EQ(emitCalls, 3, "in three pieces");
    CHECK_EQ(emitLen[0], 128, "128");
    CHECK_EQ(emitLen[1], 128, "128");
    CHECK_EQ(emitLen[2], 43, "43");
    CHECK_EQ(emittedBytes, 299, "every byte");
    CHECK(memcmp(emitted, big, 299) == 0, "in order");
}

int main(void)
{
    test_layout();
    test_log_line();
    test_log_emit();
    test_log_layout();
    test_log_lines();
    test_log_answer();
    test_log_text();
    test_u4();
    test_icm();
    test_answer();
    test_edges();
    test_content_gen();
    test_id_from_link();
    test_age();
    test_order_and_bump();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
