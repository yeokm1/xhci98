/*
 * test_tbt_icm.c - host vectors for the firmware connection manager client
 * (src\cm\tbt_icm.c; roadmap-usb4.md task 39-A.3, design record 21
 * sections 5 and 10.1).
 *
 * Alpine Ridge is tested against every frame ICM-PROTOCOL.md quotes as
 * observed: driver ready T:1/T:2, get route T:833/T:834, approve
 * T:34153/T:34154, device connected T:9/T:27886/T:34163 (words 0 to 5 as
 * quoted, the name block built from section 4.4's entry table, the rest of
 * the 220 bytes zero, as that section says), device disconnected T:21087;
 * and against the sequences of its section 6: the rebind with the dock
 * attached (6.2), unplug (6.3), plug and approve (6.4), resume (6.5) and
 * unplug while suspended (6.6). Boot ACL frames (T:7099 to T:7102) are not
 * here: the client sends no boot ACL command.
 *
 * Falcon Ridge, Titan Ridge and Ice Lake are UNCAPTURED: their vectors are
 * built from Linux drivers/thunderbolt tb_msgs.h's structure layouts
 * (struct icm_fr_*, icm_tr_*, icm_icl_*) and icm.c's per-generation hooks,
 * and test the client against those layouts, not against hardware.
 *
 * The firmware is modelled here over tbt_fake.h's host interface, with a
 * virtual clock, so every timeout, retry and the disconnect landing inside
 * a 10 s approve run in microseconds.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/tbt_icm.h"
#include "tbt_fake.h"
#include "test_harness.h"

/* ------------------------------------------------------------------ */
/* The modelled firmware                                              */
/* ------------------------------------------------------------------ */

#define GEN_FR  1
#define GEN_AR  2
#define GEN_TR  3
#define GEN_ICL 4

#define MODE_ANSWER     0
#define MODE_NEVER      1
#define MODE_ERROR      2
#define MODE_MISSING    3   /* topology: packet 5 never sent */
#define MODE_REVERSE    4   /* topology: the last packet first */
#define MODE_SPLIT      5   /* topology: half the packets per request */

static const unsigned long kUuid[4] = {
    0xa0211100UL, 0x003d0efcUL, 0xffffffffUL, 0xffffffffUL  /* ICM 4.4 [obs] */
};
static const unsigned long kUuid2[4] = {
    0x28f88500UL, 0x8086d8dfUL, 0xffffffffUL, 0xffffffffUL  /* ICM 4.6's other ACL entry */
};

typedef struct FW {
    int Gen;
    int ReadyMode;
    unsigned long HoldUs;
    unsigned long HoldUntil;
    int Present;                /* the dock is attached */
    int Approved;               /* its tunnel exists */
    unsigned long ConnId;
    int ApproveMode;
    unsigned long ErrorApproves;    /* the first this many approves fail */
    unsigned long ApproveDelayUs;
    unsigned long DisconnectInApproveUs;
    int RouteMode;
    int TopoMode;
    int TopoShortFirst;     /* a header-only error answer before the real one */
    unsigned long Ready;
    unsigned long Approves;
    unsigned long Routes;
    unsigned long Topos;
    unsigned long Reads;
    unsigned long LastApprove[8];
    unsigned long LastApproveWords;
    unsigned long StopClockAtReady; /* the clock stops at this driver ready */
} FW;

static FW g_fw;
static TBT_RING g_ring;
static TBT_ICM g_icm;

static unsigned long g_linkDowns;
static int g_linkDownInWait;
static unsigned long g_hookCalls;
static BOOLEAN g_hookAnswer;

/* The endpoint name block of ICM 4.4 [obs] T:9, as bytes. */
static unsigned long nameBlock(unsigned long *dw, unsigned long words)
{
    static const unsigned char b[] = {
        0x11, 0x01, 'C', 'a', 'l', 'D', 'i', 'g', 'i', 't', ',', ' ',
        'I', 'n', 'c', '.', 0,
        0x0b, 0x02, 'T', 'S', '3', ' ', 'P', 'l', 'u', 's', 0,
        0x08, 0x10, 0x3d, 0x00, 0x11, 0x00, 0x11, 0x01,
        0x02, 0x12,
        0x00
    };
    unsigned long i;

    memset(dw, 0, 4 * words);
    for (i = 0; i < sizeof(b) && i / 4 < words; i++) {
        dw[i / 4] |= (unsigned long)b[i] << (8 * (i % 4));
    }
    return words;
}

/* Alpine Ridge / Falcon Ridge device connected (ICM 4.4; Linux struct
 * icm_fr_event_device_connected): 61 DWs. Link info bit 11 is set as the
 * capture shows it (R11). */
static void arConnected(unsigned long id, int approved, unsigned long link,
                        unsigned long depth, const unsigned long *uuid,
                        unsigned long delayUs)
{
    unsigned long dw[61];
    unsigned long li;

    dw[0] = 0x0100b103UL;
    memcpy(&dw[1], uuid, 16);
    li = link | (depth << 4) | (approved ? 0x100UL : 0) | 0x800UL;
    dw[5] = 0x10UL | (id << 8) | (li << 16);
    nameBlock(&dw[6], 55);
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 61, delayUs, 0);
}

static void arDisconnected(unsigned long link, unsigned long depth,
                           unsigned long delayUs)
{
    unsigned long dw[2];

    dw[0] = 0x01000004UL;           /* T:21087 */
    dw[1] = (link | (depth << 4)) << 16;
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 2, delayUs, 0);
}

/* Titan Ridge / Ice Lake device connected (Linux struct
 * icm_tr_event_device_connected): 63 DWs, UNCAPTURED. */
static void trConnected(unsigned long id, int approved, unsigned long routeLo,
                        const unsigned long *uuid, unsigned long delayUs,
                        unsigned long index)
{
    unsigned long dw[63];
    unsigned long li;

    dw[0] = 0x0100b103UL | (index << 16);
    memcpy(&dw[1], uuid, 16);
    dw[5] = 0;
    dw[6] = routeLo;
    li = (routeLo & 7) | (1UL << 4) | (approved ? 0x100UL : 0);
    dw[7] = id | (li << 16);
    nameBlock(&dw[8], 55);
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 63, delayUs, 0);
}

static void trDisconnected(unsigned long routeLo, unsigned long delayUs)
{
    unsigned long dw[3];

    dw[0] = 0x01000004UL;
    dw[1] = 0;
    dw[2] = routeLo;
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 3, delayUs, 0);
}

static void fwAnnounce(unsigned long delayUs)
{
    if (g_fw.Gen == GEN_TR || g_fw.Gen == GEN_ICL) {
        trConnected(g_fw.ConnId, g_fw.Approved, 1, kUuid, delayUs, 0);
    } else {
        arConnected(g_fw.ConnId, g_fw.Approved, 1, 1, kUuid, delayUs);
    }
}

/* Falcon Ridge topology (Linux struct icm_fr_pkg_get_topology_response,
 * 38 DWs a packet, ICM_GET_TOPOLOGY_PACKETS of them): packet 0 is the
 * host, its port 1 naming switch 1; switch 1 at route 1 names switch 2
 * at route 0x0301 on its port 3. UNCAPTURED. */
static void fwTopology(int mode)
{
    unsigned long dw[38];
    unsigned long i;
    unsigned long k;
    int error = (mode == MODE_ERROR);

    for (k = 0; k < 14; k++) {
        i = (mode == MODE_REVERSE) ? 13 - k : k;
        if (mode == MODE_MISSING && i == 5) {
            continue;
        }
        if (mode == MODE_SPLIT && ((g_fw.Topos & 1) ? i >= 7 : i < 7)) {
            continue;
        }
        memset(dw, 0, sizeof(dw));
        dw[0] = 0x0e000001UL | (i << 16) | (error ? 0x100UL : 0);
        if (i == 0) {
            dw[3] = 0x00000001UL;
            dw[6 + 1] = (1UL << 24) | 1UL;
        } else if (i == 1) {
            dw[1] = 0x00000001UL;
            dw[3] = 0x01000001UL;
            dw[6 + 3] = (2UL << 24) | 1UL;
        } else if (i == 2) {
            dw[1] = 0x00000301UL;
            dw[3] = 0x02000001UL;
        }
        fakeQueue(TBT_PDF_ICM_RESP, dw, 38, 2000 + 200 * k, 0);
    }
}

static void fwOnTx(FAKE *fk, unsigned long pdf, const unsigned long *dw,
                   unsigned long words)
{
    unsigned long r[64];
    unsigned long i;
    unsigned long code;

    (void)fk;
    if (pdf == TBT_PDF_READ) {
        g_fw.Reads++;
        if (g_fake.NowUs >= g_fw.HoldUntil) {
            r[0] = dw[0] | 0x80000000UL;
            r[1] = dw[1];
            r[2] = dw[2] | (3UL << 19);
            r[3] = 0x15C08086UL;
            fakeQueue(TBT_PDF_READ, r, 4, 100, 0);
        }
        return;
    }
    if (pdf != TBT_PDF_ICM_CMD || words < 1) {
        return;
    }
    code = dw[0] & 0xFF;
    if (code == 0x03) {
        g_fw.Ready++;
        if (g_fw.StopClockAtReady != 0 &&
            g_fw.Ready == g_fw.StopClockAtReady) {
            g_fake.ClockStopped = 1;
        }
        if (g_fw.ReadyMode == MODE_NEVER) {
            return;
        }
        if (g_fw.Gen == GEN_AR) {
            r[0] = 0x0100e203UL;    /* T:2 */
            r[1] = 0x28010b60UL;
            i = 2;
        } else if (g_fw.Gen == GEN_FR) {
            r[0] = 0x01000003UL;    /* icm_fr_pkg_driver_ready_response */
            r[1] = 0x00010b60UL;
            i = 2;
        } else {
            r[0] = 0x01004003UL;    /* icm_tr_pkg_driver_ready_response */
            r[1] = ((1UL | (3UL << 4) | (16UL << 7)) << 16);
            r[2] = 0x00004500UL;
            r[3] = 0x000015EBUL;
            i = 4;
        }
        if (g_fw.ReadyMode == MODE_ERROR) {
            r[0] |= 0x100UL;
        }
        fakeQueue(TBT_PDF_ICM_RESP, r, i, 6000, 0);
        g_fw.HoldUntil = g_fake.NowUs + g_fw.HoldUs;
        if (g_fw.Present) {
            g_fw.ConnId++;
            fwAnnounce(g_fw.HoldUs - 10000UL);
        }
        return;
    }
    if (code == 0x0A) {
        g_fw.Routes++;
        if (g_fw.RouteMode == MODE_NEVER) {
            return;
        }
        r[0] = 0x0100000aUL | (g_fw.RouteMode == MODE_ERROR ? 0x100UL : 0);
        r[1] = dw[1];
        r[2] = 0;
        r[3] = (dw[1] >> 16) & 7;   /* the route of a depth-1 device is its link */
        fakeQueue(TBT_PDF_ICM_RESP, r, 4, 3000, 0);
        return;
    }
    if (code == 0x01) {
        g_fw.Topos++;
        if (g_fw.TopoShortFirst) {
            unsigned long hdr[1];
            hdr[0] = 0x0e000101UL;
            fakeQueue(TBT_PDF_ICM_RESP, hdr, 1, 500, 0);
        }
        if (g_fw.TopoMode != MODE_NEVER) {
            fwTopology(g_fw.TopoMode);
        }
        return;
    }
    if (code == 0x04) {
        g_fw.Approves++;
        memcpy(g_fw.LastApprove, dw, 4 * (words < 8 ? words : 8));
        g_fw.LastApproveWords = words;
        if (g_fw.DisconnectInApproveUs != 0) {
            if (g_fw.Gen == GEN_TR || g_fw.Gen == GEN_ICL) {
                trDisconnected(1, g_fw.DisconnectInApproveUs);
            } else {
                arDisconnected(1, 1, g_fw.DisconnectInApproveUs);
            }
            g_fw.DisconnectInApproveUs = 0;
            g_fw.Present = 0;
        }
        if (g_fw.ApproveMode == MODE_NEVER) {
            return;
        }
        memcpy(r, dw, 4 * words);
        r[0] = 0x01000004UL | (g_fw.ApproveMode == MODE_ERROR ? 0x100UL : 0);
        if (g_fw.Approves <= g_fw.ErrorApproves) {
            r[0] |= 0x100UL;
        }
        if (g_fw.ApproveMode == MODE_ANSWER && !(r[0] & 0x100UL)) {
            g_fw.Approved = 1;
        }
        fakeQueue(TBT_PDF_ICM_RESP, r, words, g_fw.ApproveDelayUs, 0);
        return;
    }
}

static void onLinkDown(PVOID ctx, const TBT_ICM_DEVICE *dev, BOOLEAN inWait)
{
    (void)ctx;
    (void)dev;
    g_linkDowns++;
    g_linkDownInWait = inWait;
}

static BOOLEAN onApproveHook(PVOID ctx, const TBT_ICM_DEVICE *dev)
{
    (void)ctx;
    (void)dev;
    g_hookCalls++;
    return g_hookAnswer;
}

static const TBT_ICM_PROFILE *rowFor(int gen)
{
    switch (gen) {
    case GEN_FR: return &TbtIcmProfileFalconRidge;
    case GEN_TR: return &TbtIcmProfileTitanRidge;
    case GEN_ICL: return &TbtIcmProfileIceLake;
    default: return &TbtIcmProfileAlpineRidge;
    }
}

static void setup(int gen, int present, int approved)
{
    fakeReset();
    memset(&g_fw, 0, sizeof(g_fw));
    memset(&g_ring, 0, sizeof(g_ring));
    g_fw.Gen = gen;
    g_fw.HoldUs = 580000UL;         /* ICM 6.2 [obs] */
    g_fw.Present = present;
    g_fw.Approved = approved;
    g_fw.ConnId = 4;
    g_fw.ApproveDelayUs = 358000UL; /* ICM 4.3 [obs] */
    g_fake.OnTx = fwOnTx;
    g_linkDowns = 0;
    g_linkDownInWait = -1;
    g_hookCalls = 0;
    TbtIcmInit(&g_icm, rowFor(gen), &g_ring, &g_fakeHost, 0);
    TbtIcmSetLinkDown(&g_icm, onLinkDown, NULL);
}

static LONG start(void)
{
    return TbtIcmStart(&g_icm, fakeMem(), FAKE_PHYS, FAKE_MEM_BYTES);
}

static unsigned long countTx(unsigned long pdf, unsigned long code)
{
    unsigned long i;
    unsigned long n = 0;

    for (i = 0; i < g_fake.TxCount && i < FAKE_TXLOG; i++) {
        if (g_fake.TxLogPdf[i] == pdf &&
            (pdf != TBT_PDF_ICM_CMD || (g_fake.TxLogDw[i][0] & 0xFF) == code)) {
            n++;
        }
    }
    return n;
}

static long findTx(unsigned long pdf, unsigned long code)
{
    unsigned long i;

    for (i = 0; i < g_fake.TxCount && i < FAKE_TXLOG; i++) {
        if (g_fake.TxLogPdf[i] == pdf &&
            (pdf != TBT_PDF_ICM_CMD || (g_fake.TxLogDw[i][0] & 0xFF) == code)) {
            return (long)i;
        }
    }
    return -1;
}

static unsigned long actions(TBT_ICM_ACTION *out, unsigned long max)
{
    unsigned long n = 0;
    TBT_ICM_ACTION a;

    while (TbtIcmNextAction(&g_icm, &a)) {
        if (n < max) {
            out[n] = a;
        }
        n++;
    }
    return n;
}

/* Advance the clock with idle polls, as the thread would. */
static void idle(unsigned long ms)
{
    unsigned long t;

    for (t = 0; t < ms; t += 200) {
        fakeSleep(NULL, 200);
        TbtIcmPoll(&g_icm);
    }
}

/* ------------------------------------------------------------------ */
/* Frames                                                             */
/* ------------------------------------------------------------------ */

static void test_frames_ar(void)
{
    static const unsigned long t2[2] = { 0x0100e203UL, 0x28010b60UL };
    static const unsigned long t21087[2] = { 0x01000004UL, 0x00110000UL };
    unsigned long ev[61];
    TBT_ICM_READY ready;
    TBT_ICM_ANNOUNCE a;
    TBT_ICM_GONE g;
    TBT_ICM_DEVICE dev;
    ULONG req[8];
    ULONG n;

    /* T:2 */
    CHECK(TbtIcmProfileAlpineRidge.DecodeReady(t2, 2, &ready), "T:2 decodes");
    CHECK_EQ(ready.Flags, 0xe2, "flags 0xe2");
    CHECK(ready.Flags & TBT_ICM_READY_RTD3, "RTD3 bit 6");
    CHECK_EQ(ready.RomVersion, 0x60, "ROM version");
    CHECK_EQ(ready.RamVersion, 0x0b, "RAM version");
    CHECK_EQ(ready.SecurityLevel, 1, "security level user");
    CHECK_EQ(ready.BootAclCount, 16, "16 ACL entries");
    CHECK(ready.BootAclSupported, "boot ACL supported");

    /* T:9, T:27886, T:34163: words 0 to 5 and the name block. */
    ev[0] = 0x0100b103UL;
    memcpy(&ev[1], kUuid, 16);
    ev[5] = 0x09110510UL;           /* T:9: approved, id 5, key 0x10 */
    nameBlock(&ev[6], 55);
    CHECK(TbtIcmProfileAlpineRidge.DecodeConnected(ev, 61, &a), "T:9 decodes");
    CHECK_EQ(a.EventFlags, 0xb1, "flags 0xb1");
    CHECK_EQ(TBT_ICM_EVF_SLEVEL(a.EventFlags), 2, "level field 2, as observed");
    CHECK(a.EventFlags & TBT_ICM_EVF_DUAL_LANE, "dual lane");
    CHECK(a.EventFlags & TBT_ICM_EVF_SPEED_20G, "20 Gb/s");
    CHECK_EQ(a.Uuid[0], 0xa0211100UL, "UUID word 1");
    CHECK_EQ(a.Uuid[3], 0xffffffffUL, "UUID word 4");
    CHECK_EQ(a.Key, 0x10, "key 0x10");
    CHECK_EQ(a.ConnectionId, 5, "id 5");
    CHECK_EQ(a.LinkInfo, 0x0911, "link info 0x0911");
    CHECK_EQ(a.Link, 1, "link 1");
    CHECK_EQ(a.Depth, 1, "depth 1");
    CHECK(a.Approved, "approved at T:9");
    CHECK(!a.Rejected, "not rejected");
    CHECK(!a.Boot, "not boot");
    CHECK(!a.RouteValid, "no route in an Alpine Ridge event");
    CHECK_EQ(a.Names.Entries, 4, "four entries before the terminator");
    CHECK(strcmp(a.Names.Vendor, "CalDigit, Inc.") == 0, "vendor name");
    CHECK(strcmp(a.Names.Model, "TS3 Plus") == 0, "model name");
    CHECK_EQ(a.Names.TbVendorId, 0x3d, "Intel entry vendor 0x3d");
    CHECK_EQ(a.Names.TbModelId, 0x11, "model 0x11");
    CHECK_EQ(a.Names.IntelFlags, 0x01, "RTD3 flag");
    CHECK_EQ(a.Names.Present, TBT_ICM_NAMES_VENDOR | TBT_ICM_NAMES_MODEL | TBT_ICM_NAMES_INTEL,
             "all three kinds present, nothing malformed");
    ev[5] = 0x08110610UL;           /* T:27886 */
    CHECK(TbtIcmProfileAlpineRidge.DecodeConnected(ev, 61, &a), "T:27886 decodes");
    CHECK(!a.Approved, "not approved");
    CHECK_EQ(a.ConnectionId, 6, "id 6");
    ev[5] = 0x09110710UL;           /* T:34163 */
    CHECK(TbtIcmProfileAlpineRidge.DecodeConnected(ev, 61, &a), "T:34163 decodes");
    CHECK(a.Approved, "approved again");
    CHECK_EQ(a.ConnectionId, 7, "id 7");
    CHECK(!TbtIcmProfileAlpineRidge.DecodeConnected(ev, 5, &a), "five words: refused");
    CHECK(!TbtIcmProfileAlpineRidge.DecodeConnected(ev, 6, &a), "six words: refused");
    CHECK(!TbtIcmProfileAlpineRidge.DecodeConnected(ev, 60, &a), "sixty words: refused");

    /* T:21087 */
    CHECK(TbtIcmProfileAlpineRidge.DecodeDisconnected(t21087, 2, &g), "T:21087 decodes");
    CHECK_EQ(g.Link, 1, "link 1");
    CHECK_EQ(g.Depth, 1, "depth 1");
    {
        unsigned long bad[2] = { 0x01000004UL, 0x00770000UL };
        CHECK(!TbtIcmProfileAlpineRidge.DecodeDisconnected(bad, 2, &g),
              "depth 7 is past TB_SWITCH_MAX_DEPTH: refused");
    }

    /* T:34153: the approve request, byte for byte. */
    memset(&dev, 0, sizeof(dev));
    memcpy(dev.Uuid, kUuid, 16);
    dev.Key = 0x10;
    dev.ConnectionId = 6;
    n = TbtIcmProfileAlpineRidge.EncodeApprove(&dev, req);
    CHECK_EQ(n, 6, "six words");
    CHECK_EQ(req[0], 0x00000004UL, "T:34153 word 0");
    CHECK_EQ(req[1], 0xa0211100UL, "word 1");
    CHECK_EQ(req[2], 0x003d0efcUL, "word 2");
    CHECK_EQ(req[3], 0xffffffffUL, "word 3");
    CHECK_EQ(req[4], 0xffffffffUL, "word 4");
    CHECK_EQ(req[5], 0x00000610UL, "word 5: key 0x10, id 6");
}

static void test_names(void)
{
    unsigned long blk[4];
    TBT_ICM_NAMES nm;

    /* An entry longer than the block. */
    memset(blk, 0, sizeof(blk));
    blk[0] = 0x00000140UL;          /* length 0x40, type 1 */
    TbtIcmDecodeNames(blk, 4, &nm);
    CHECK(nm.Present & TBT_ICM_NAMES_MALFORMED, "overlong entry: malformed");
    CHECK_EQ(nm.Entries, 0, "nothing taken");
    /* A length of 1 cannot hold its own header. */
    blk[0] = 0x00000101UL;
    TbtIcmDecodeNames(blk, 4, &nm);
    CHECK(nm.Present & TBT_ICM_NAMES_MALFORMED, "length 1: malformed");
    /* Non-printable bytes are kept out of the log. */
    blk[0] = 0x07410106UL;          /* len 6, vendor, 'A', 0x07 */
    blk[1] = 0x00004243UL;          /* 'C', 'B' */
    TbtIcmDecodeNames(blk, 4, &nm);
    CHECK(strcmp(nm.Vendor, "A?CB") == 0, "control byte replaced");
    /* An empty block. */
    memset(blk, 0, sizeof(blk));
    TbtIcmDecodeNames(blk, 4, &nm);
    CHECK_EQ(nm.Present, 0, "zero-length terminator first: nothing");
}

/* ------------------------------------------------------------------ */
/* Alpine Ridge sequences                                             */
/* ------------------------------------------------------------------ */

static void test_bind_rebind(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;
    long dr;
    long gr;
    const TBT_ICM_DEVICE *d;

    /* ICM 6.2: the dock attached and approved. */
    setup(GEN_AR, 1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    CHECK_EQ(g_fake.MboxCount, 1, "one mailbox command");
    CHECK_EQ(g_fake.MboxLog[0], 0x23, "allow all devices, ICM 2.3 [obs]");
    dr = findTx(TBT_PDF_ICM_CMD, 3);
    CHECK_EQ(dr, 0, "driver ready is the first frame");
    CHECK_EQ(g_fake.TxLogWords[dr], 1, "T:1: one word");
    CHECK_EQ(g_fake.TxLogDw[dr][0], 0x00000003UL, "T:1: 0x00000003");
    CHECK_EQ(g_fake.TxBadCrc, 0, "every trailer right");
    CHECK(g_fw.Reads >= 2, "host polls unanswered during the hold, then answered");
    CHECK_EQ(g_icm.Counters.HostPollAnswered, 1, "answered once the hold ended");
    gr = findTx(TBT_PDF_ICM_CMD, 0x0A);
    CHECK(gr > dr, "get route after driver ready");
    CHECK_EQ(g_fake.TxLogDw[gr][0], 0x0000000aUL, "T:833 word 0");
    CHECK_EQ(g_fake.TxLogDw[gr][1], 0x00110000UL, "T:833 word 1");
    CHECK_EQ(countTx(TBT_PDF_ICM_CMD, 4), 0, "no approve: already approved");
    CHECK_EQ(g_icm.Ready.SecurityLevel, 1, "ready decoded");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL, "entry 0");
    if (d != NULL) {
        CHECK(d->RouteValid, "route from get route");
        CHECK_EQ(d->Route.Lo, 1, "T:834: route 1");
        CHECK_EQ(d->ConnectionId, 5, "id 5");
        CHECK(d->Approved, "approved");
    }
    n = actions(act, 8);
    CHECK_EQ(n, 1, "one action");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_TUNNEL_UP, "tunnel up");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_ANNOUNCED, "announced approved");
    CHECK(TbtIcmActionLive(&g_icm, &act[0]), "still live");
    CHECK_EQ(g_fake.Imr[0], 0, "interrupts stay masked");
}

static void test_plug_approve_unplug(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;
    long ap;
    const TBT_ICM_DEVICE *d;

    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind with nothing attached");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 0, "empty table");
    CHECK_EQ(actions(act, 8), 0, "no actions");

    /* ICM 6.4: plugged, announced unapproved, id 6. */
    g_fw.Present = 1;
    arConnected(6, 0, 1, 1, kUuid, 0);
    idle(200);
    ap = findTx(TBT_PDF_ICM_CMD, 4);
    CHECK(ap >= 0, "approve sent");
    if (ap >= 0) {
        CHECK_EQ(g_fake.TxLogWords[ap], 6, "T:34153: six words");
        CHECK_EQ(g_fake.TxLogDw[ap][5], 0x00000610UL, "T:34153: key 0x10, id 6");
        CHECK_EQ(g_fake.TxLogDw[ap][1], 0xa0211100UL, "UUID");
    }
    CHECK(findTx(TBT_PDF_ICM_CMD, 0x0A) < ap, "get route before approve (ICM 6.4)");
    CHECK_EQ(g_icm.Counters.ApprovalsOk, 1, "T:34154: approved");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && d->Approved, "entry approved");
    n = actions(act, 8);
    CHECK_EQ(n, 1, "one action");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_TUNNEL_UP, "tunnel up");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_APPROVED, "after approve");

    /* ICM 6.3: unplug while active. */
    arDisconnected(1, 1, 0);
    idle(200);
    CHECK_EQ(g_linkDowns, 1, "link-down upcall");
    CHECK_EQ(g_linkDownInWait, 0, "in the idle poll, not a wait");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 0, "entry dropped");
    n = actions(act, 8);
    CHECK_EQ(n, 1, "one action");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_TUNNEL_DOWN, "tunnel down");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_DISCONNECTED, "disconnected");
    CHECK(act[0].WasApproved, "it had a tunnel");
    CHECK(act[0].LinkDownSent, "the upcall ran first");
    CHECK(!TbtIcmActionLive(&g_icm, &act[0]), "no longer live");

    /* A disconnect for nothing in the table (record 21 section 1.4.6):
     * some device of the domain is gone - every port's tunnel-down. */
    arDisconnected(1, 1, 0);
    idle(200);
    CHECK_EQ(g_icm.Counters.DisconnectUnknown, 1, "counted");
    CHECK_EQ(actions(act, 8), 1, "one action");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_RESYNC, "the whole domain");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_UNMATCHED, "unmatched");
}

static void test_policy(void)
{
    TBT_ICM_ACTION act[4];

    setup(GEN_AR, 0, 0);
    TbtIcmSetApprovePolicy(&g_icm, FALSE);
    CHECK_EQ(start(), TBT_OK, "bind");
    arConnected(6, 0, 1, 1, kUuid, 0);
    idle(200);
    CHECK_EQ(countTx(TBT_PDF_ICM_CMD, 4), 0, "XhciCmApprove 0: no approve");
    CHECK_EQ(g_icm.Counters.ApprovalsWithheld, 1, "withheld");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 1, "kept in the table");
    CHECK_EQ(actions(act, 4), 0, "no tunnel");

    /* A pre-tunnelled device is used under policy 0 (record 21 5.4). */
    arDisconnected(1, 1, 0);
    idle(200);
    actions(act, 4);
    arConnected(7, 1, 1, 1, kUuid, 0);
    idle(200);
    CHECK_EQ(actions(act, 4), 1, "approved device: tunnel up");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_ANNOUNCED, "as announced");

    /* The hook decides when set. */
    setup(GEN_AR, 0, 0);
    TbtIcmSetApproveHook(&g_icm, onApproveHook, NULL);
    g_hookAnswer = FALSE;
    CHECK_EQ(start(), TBT_OK, "bind");
    arConnected(6, 0, 1, 1, kUuid, 0);
    idle(200);
    CHECK_EQ(g_hookCalls, 1, "hook asked");
    CHECK_EQ(countTx(TBT_PDF_ICM_CMD, 4), 0, "hook said no");
}

static void test_resume_refresh_and_reconcile(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;
    unsigned long routes;

    /* ICM 6.5: suspended with the dock approved; re-announced id 7. */
    setup(GEN_AR, 1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    actions(act, 8);
    routes = g_fw.Routes;
    TbtIcmSuspend(&g_icm);
    CHECK_EQ(g_fake.TxCtrl, 0, "rings stopped");
    CHECK_EQ(g_fake.MboxCount, 3, "two more mailbox commands");
    CHECK_EQ(g_fake.MboxLog[1], 0x05, "save devices first (record 21 5.6)");
    CHECK_EQ(g_fake.MboxLog[2], 0x07, "then driver unloads");
    CHECK_EQ(TbtIcmResume(&g_icm), TBT_OK, "resume");
    CHECK_EQ(g_fake.MboxCount, 3, "no 0x23 on resume");
    CHECK_EQ(g_fw.Ready, 2, "driver ready again");
    CHECK_EQ(g_fw.Routes, routes, "no get route for a known device (ICM 6.5 [obs])");
    CHECK_EQ(g_icm.Counters.Refreshes, 1, "refreshed");
    CHECK_EQ(TbtIcmDevice(&g_icm, 0)->ConnectionId, 6, "id updated (the model's next id)");
    n = actions(act, 8);
    CHECK_EQ(n, 0, "no tunnel change");

    /* ICM 6.6: unplugged while suspended - no event, reconciled. */
    TbtIcmSuspend(&g_icm);
    g_fw.Present = 0;
    CHECK_EQ(TbtIcmResume(&g_icm), TBT_OK, "resume");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 0, "dropped by the reconciliation");
    n = actions(act, 8);
    CHECK_EQ(n, 1, "one action");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_TUNNEL_DOWN, "tunnel down");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_RECONCILED, "reconciled");
    CHECK_EQ(g_icm.Counters.Reconciled, 1, "counted");
}

static void test_disconnect_during_approve(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;
    unsigned long t0;
    unsigned long late[6];

    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ApproveMode = MODE_NEVER;
    g_fw.DisconnectInApproveUs = 2000000UL;
    arConnected(6, 0, 1, 1, kUuid, 0);
    fakePump();
    t0 = g_fake.NowUs;
    TbtIcmPoll(&g_icm);
    CHECK_EQ(g_fw.Approves, 1, "one approve, never retried");
    CHECK_EQ(g_linkDowns, 1, "link-down upcall");
    CHECK_EQ(g_linkDownInWait, 1, "run inside the approve wait (record 21 4.4)");
    CHECK(g_fake.NowUs - t0 < 2500000UL, "the wait ended at the disconnect, not at 10 s");
    CHECK_EQ(g_icm.Counters.ApprovalsAbandoned, 1, "abandoned");
    CHECK_EQ(g_icm.Counters.ApprovalsOk, 0, "not approved");
    n = actions(act, 8);
    CHECK_EQ(n, 1, "only the tunnel down");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_TUNNEL_DOWN, "down");
    CHECK(!act[0].WasApproved, "it never had a tunnel");

    /* The firmware's late answer to that approve is a stray, not the
     * answer to the next device's approve: the UUID echo differs. */
    late[0] = 0x01000004UL;
    memcpy(&late[1], kUuid, 16);
    late[5] = 0x00000610UL;
    g_fw.ApproveMode = MODE_ANSWER;
    g_fw.ApproveDelayUs = 500000UL;
    arConnected(8, 0, 3, 1, kUuid2, 0);
    fakeQueue(TBT_PDF_ICM_RESP, late, 6, 100000UL, 0);
    idle(200);
    CHECK(g_icm.Counters.Strays >= 1, "the late answer counted as a stray");
    CHECK_EQ(g_icm.Counters.ApprovalsOk, 1, "the second device approved by its own answer");
    n = actions(act, 8);
    CHECK_EQ(n, 1, "its tunnel up");
    CHECK_EQ(act[0].Uuid[0], kUuid2[0], "the right device");
}

static void test_timeouts_and_errors(void)
{
    TBT_ICM_ACTION act[4];
    unsigned long t0;
    const TBT_ICM_DEVICE *d;

    /* Approve never answered: four tries of 10 s. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ApproveMode = MODE_NEVER;
    arConnected(6, 0, 1, 1, kUuid, 0);
    fakePump();
    t0 = g_fake.NowUs;
    TbtIcmPoll(&g_icm);
    CHECK_EQ(g_fw.Approves, 4, "one try and three retries (ICM 3 [inf])");
    CHECK(g_fake.NowUs - t0 >= 40000000UL, "four 10 s waits");
    CHECK(g_fake.NowUs - t0 < 41000000UL, "and no more");
    CHECK_EQ(g_icm.Counters.ApprovalsUnknown, 1, "outcome unknown (record 21 1.4.6)");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && !d->Approved && d->ApproveUnknown, "kept, unknown");
    CHECK_EQ(actions(act, 4), 1, "the tunnel-up sequence runs anyway");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_APPROVE_UNKNOWN, "for an unknown outcome");
    /* Settled by the next announcement: approved = 1, a tunnel up, and no
     * second approve. */
    arConnected(7, 1, 1, 1, kUuid, 0);
    idle(200);
    CHECK_EQ(g_fw.Approves, 4, "no further approve");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && d->Approved && !d->ApproveUnknown, "settled approved");
    CHECK_EQ(actions(act, 4), 1, "a tunnel up");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_ANNOUNCED, "as announced");

    /* The error flag: not retried. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ApproveMode = MODE_ERROR;
    arConnected(6, 0, 1, 1, kUuid, 0);
    idle(600);
    CHECK_EQ(g_fw.Approves, 1, "one approve");
    CHECK_EQ(g_icm.Counters.ErrorFlags, 1, "error flag counted");
    CHECK_EQ(g_icm.Counters.ApprovalsFailed, 1, "failed");
    CHECK_EQ(actions(act, 4), 0, "no tunnel");

    /* A stale approve: the firmware cancelled the connection's pending
     * approval (an unplug, a re-plug or mailbox 0x06) and answers the
     * approve with the error flag, then reports the disconnect. Benign:
     * one approve, no retry, logged once, the instance not failed, the
     * entry gone with the disconnect and nothing sent after it. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ApproveMode = MODE_ERROR;
    arConnected(6, 0, 1, 1, kUuid, 0);
    idle(600);
    arDisconnected(1, 1, 0);
    idle(600);
    CHECK_EQ(g_fw.Approves, 1, "stale approve: one approve, no retry");
    CHECK_EQ(g_icm.Counters.ApprovalsFailed, 1, "stale approve: counted once");
    CHECK(!g_icm.Failed, "stale approve: the instance not failed");
    CHECK(TbtIcmDevice(&g_icm, 0) == NULL, "stale approve: entry gone");
    CHECK_EQ(actions(act, 4), 1, "stale approve: the tunnel down only");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_TUNNEL_DOWN, "stale approve: down");

    /* Get route never answered: the entry stays, unrouted and unapproved
     * (record 21 section 5.5); the next announcement retries the route and
     * then the approval. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.RouteMode = MODE_NEVER;
    arConnected(6, 0, 1, 1, kUuid, 0);
    fakePump();
    t0 = g_fake.NowUs;
    TbtIcmPoll(&g_icm);
    CHECK_EQ(g_fw.Routes, 4, "four get routes");
    CHECK(g_fake.NowUs - t0 >= 20000000UL, "four 5 s waits");
    CHECK_EQ(g_icm.Counters.RouteFailures, 1, "route failure counted");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && !d->RouteValid, "kept without a route");
    CHECK_EQ(g_fw.Approves, 0, "not approved while unrouted");
    CHECK_EQ(actions(act, 4), 0, "no tunnel");
    g_fw.RouteMode = MODE_ANSWER;
    arConnected(7, 0, 1, 1, kUuid, 0);
    idle(600);
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && d->RouteValid, "routed on the next announcement");
    CHECK_EQ(g_icm.Counters.ApprovalsOk, 1, "then approved");
    CHECK_EQ(actions(act, 4), 1, "tunnel up");

    /* An announced-approved device whose route fails keeps its tunnel. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.RouteMode = MODE_ERROR;
    arConnected(6, 1, 1, 1, kUuid, 0);
    idle(200);
    CHECK_EQ(actions(act, 4), 1, "tunnel up without a route");

    /* Driver ready never answered: a failure after activation (record 21
     * section 1.4.2) - rings stopped at once, 0x07 at the stop since driver
     * ready was sent. */
    setup(GEN_AR, 0, 0);
    g_fw.ReadyMode = MODE_NEVER;
    t0 = g_fake.NowUs;
    CHECK_EQ(start(), TBT_E_TIMEOUT, "start refused");
    CHECK_EQ(g_fw.Ready, 4, "four driver readys");
    CHECK(g_fake.NowUs - t0 >= 20000000UL, "5 s each");
    CHECK_EQ(g_fake.TxCtrl, 0, "TX stopped");
    CHECK_EQ(g_fake.RxCtrl, 0, "RX stopped");
    CHECK_EQ(g_fake.MboxCount, 1, "0x23 only, so far");
    CHECK(g_icm.Failed, "failed after activation");
    CHECK_EQ(TbtIcmResume(&g_icm), TBT_E_STOPPED, "a failed instance stays idle");
    TbtIcmUnbind(&g_icm);
    CHECK_EQ(g_fake.MboxCount, 2, "0x07 at the stop");
    CHECK_EQ(g_fake.MboxLog[1], 0x07, "driver unloads");
    TbtIcmUnbind(&g_icm);
    CHECK_EQ(g_fake.MboxCount, 2, "a second unbind sends nothing more");

    /* Driver ready answered with the error flag. */
    setup(GEN_AR, 0, 0);
    g_fw.ReadyMode = MODE_ERROR;
    CHECK_EQ(start(), TBT_E_ERROR_FLAG, "start refused");
    CHECK_EQ(g_fw.Ready, 1, "not retried");

    /* Mailbox 0x23 failing: refused before any session. */
    setup(GEN_AR, 0, 0);
    g_fake.MboxMode = FAKE_MBOX_ERROR;
    CHECK_EQ(start(), TBT_E_ERROR_FLAG, "error bit");
    CHECK_EQ(g_fake.MboxCount, 1, "no 0x07: no session was opened");
    CHECK_EQ(g_fake.TxCtrl, 0, "rings stopped");
    CHECK_EQ(g_fw.Ready, 0, "no driver ready");
    setup(GEN_AR, 0, 0);
    g_fake.MboxMode = FAKE_MBOX_HANG;
    t0 = g_fake.NowUs;
    CHECK_EQ(start(), TBT_E_TIMEOUT, "request bit never clears");
    CHECK(g_fake.NowUs - t0 >= 500000UL, "after 500 ms (ICM 2.3 [inf])");
    CHECK(g_fake.NowUs - t0 < 600000UL, "and not much more");
}

static void test_wrong_layout(void)
{
    unsigned long ev[63];
    TBT_ICM_ANNOUNCE a;
    TBT_ICM_GONE g;

    /* Record 21 section 1.4.6: an all-zero UUID, a zero route, a name
     * block running past its frame - each a frame read with the wrong
     * layout, dropped. */
    memset(ev, 0, sizeof(ev));
    ev[0] = 0x0100b103UL;
    ev[5] = 0x08110510UL;
    CHECK(!TbtIcmProfileAlpineRidge.DecodeConnected(ev, 61, &a), "zero UUID: refused");
    memcpy(&ev[1], kUuid, 16);
    ev[6] = 0x000001F0UL;           /* an entry of 240 bytes in the 220-byte block */
    CHECK(!TbtIcmProfileAlpineRidge.DecodeConnected(ev, 61, &a), "name block past the frame: refused");
    ev[6] = 0;
    CHECK(TbtIcmProfileAlpineRidge.DecodeConnected(ev, 61, &a), "the same frame, empty names: read");
    memset(ev, 0, sizeof(ev));
    ev[0] = 0x0100b103UL;
    memcpy(&ev[1], kUuid, 16);
    CHECK(!TbtIcmProfileTitanRidge.DecodeConnected(ev, 63, &a), "zero route: refused");
    ev[6] = 1;
    CHECK(TbtIcmProfileTitanRidge.DecodeConnected(ev, 63, &a), "route 1: read");
    ev[1] = 0;
    ev[2] = 0;
    CHECK(!TbtIcmProfileTitanRidge.DecodeDisconnected(ev, 3, &g), "zero route disconnect: refused");

    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    memset(ev, 0, sizeof(ev));
    ev[0] = 0x0100b103UL;
    ev[5] = 0x08110510UL;
    fakeQueue(TBT_PDF_ICM_EVENT, ev, 61, 0, 0);
    g_fake.LogCount = 0;
    idle(200);
    CHECK_EQ(g_icm.Counters.EventsMalformed, 1, "counted");
    CHECK(g_fake.LogCount >= 62, "logged with every raw word");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 0, "nothing tracked");
    CHECK_EQ(g_fw.Approves, 0, "nothing approved");
}

static void test_odd_frames(void)
{
    unsigned long dw[4];
    TBT_ICM_ACTION act[4];

    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    dw[0] = 0x01000033UL;
    dw[1] = 0;
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 2, 0, 0);
    dw[0] = 0x0100000aUL;
    fakeQueue(TBT_PDF_ICM_RESP, dw, 2, 0, 0);
    dw[0] = 0x01000003UL;
    fakeQueue(TBT_PDF_ICM_EVENT, dw, 3, 0, 0);     /* connected, too short */
    idle(200);
    CHECK_EQ(g_icm.Counters.EventsUnknown, 1, "unknown event code counted");
    CHECK_EQ(g_icm.Counters.Strays, 1, "a response nobody waited for");
    CHECK_EQ(g_icm.Counters.EventsMalformed, 1, "short event counted");

    /* Rejected: ignored. */
    {
        unsigned long ev[61];
        memset(ev, 0, sizeof(ev));
        ev[0] = 0x0100b103UL;
        memcpy(&ev[1], kUuid, 16);
        ev[5] = 0x02110510UL;
        fakeQueue(TBT_PDF_ICM_EVENT, ev, 61, 0, 0);
    }
    idle(200);
    CHECK_EQ(g_icm.Counters.Rejected, 1, "rejected counted");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 0, "not in the table");
    CHECK_EQ(actions(act, 4), 0, "no action");

    /* A second packet of an announcement is ignored (Linux icm.c
     * __icm_tr_device_connected). */
    trConnected(5, 1, 1, kUuid, 0, 1);
    idle(200);
    CHECK_EQ(g_icm.Counters.EventsIgnored, 1, "extra packet ignored");
}

static void test_event_queued_in_wait(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;

    /* A second device announced while the first one's approve waits: run
     * after it, not dropped. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    arConnected(6, 0, 1, 1, kUuid, 0);
    arConnected(2, 0, 3, 1, kUuid2, 100000UL);
    idle(1400);
    CHECK_EQ(g_icm.Counters.EventsQueued, 1, "one queued inside the wait");
    CHECK_EQ(g_fw.Approves, 2, "both approved");
    n = actions(act, 8);
    CHECK_EQ(n, 2, "two tunnels up");
    CHECK_EQ(act[0].Uuid[0], kUuid[0], "first device first");
    CHECK_EQ(act[1].Uuid[0], kUuid2[0], "then the queued one");

    /* A queued announcement whose device leaves before it runs is purged. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    arConnected(6, 0, 1, 1, kUuid, 0);
    arConnected(2, 0, 3, 1, kUuid2, 100000UL);
    arDisconnected(3, 1, 200000UL);
    idle(1400);
    CHECK_EQ(g_fw.Approves, 1, "only the first approved");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 1, "only the first in the table");
    CHECK_EQ(g_icm.Counters.DisconnectUnknown, 1, "the disconnect found no entry");
}

static void test_dual_link_and_replace(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;

    /* Re-announced through the port's other link (Linux icm.c
     * icm_fr_device_connected): a refresh, with the route asked again. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    arConnected(5, 1, 1, 1, kUuid, 0);
    idle(200);
    actions(act, 8);
    arConnected(6, 1, 2, 1, kUuid, 0);
    idle(200);
    CHECK_EQ(g_icm.Counters.Refreshes, 1, "same physical port: refresh");
    CHECK_EQ(TbtIcmDevice(&g_icm, 0)->Link, 2, "link updated");
    CHECK_EQ(g_fw.Routes, 2, "route asked again");
    CHECK_EQ(actions(act, 8), 0, "no tunnel change");

    /* The same UUID announced unapproved where it was approved: the old
     * entry is gone (replug missed), the new one approved. */
    arConnected(7, 0, 2, 1, kUuid, 0);
    idle(600);
    n = actions(act, 8);
    CHECK_EQ(n, 2, "down for the old, up for the new");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_TUNNEL_DOWN, "down first");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_REPLACED, "replaced");
    CHECK_EQ(act[1].Why, TBT_ICM_WHY_APPROVED, "then approved");
}

/* ------------------------------------------------------------------ */
/* The other rows (UNCAPTURED, Linux layouts)                         */
/* ------------------------------------------------------------------ */

static void test_titan_ridge(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;
    long ap;
    unsigned long t0;
    unsigned long ev[3];
    TBT_ICM_GONE g;

    setup(GEN_TR, 1, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    CHECK_EQ(g_fake.MboxLog[0], 0x23, "Titan Ridge reads the mode: allow all sent");
    CHECK_EQ(g_icm.Ready.Words, 4, "four-word driver ready");
    CHECK_EQ(g_icm.Ready.SecurityLevel, 1, "info level 2:0");
    CHECK_EQ(g_icm.Ready.ProtocolVersion, 3, "protocol version 6:4");
    CHECK_EQ(g_icm.Ready.BootAclCount, 16, "ACL count 12:7");
    CHECK_EQ(g_icm.Ready.NvmVersion, 0x4500, "NVM version word");
    CHECK_EQ(g_icm.Ready.DeviceId, 0x15EB, "device id");
    CHECK_EQ(g_fw.Routes, 0, "no get route: the route is in the event");
    ap = findTx(TBT_PDF_ICM_CMD, 4);
    CHECK(ap >= 0, "approve sent");
    if (ap >= 0) {
        CHECK_EQ(g_fake.TxLogWords[ap], 8, "icm_tr_pkg_approve_device: 8 words");
        CHECK_EQ(g_fake.TxLogDw[ap][1], kUuid[0], "UUID");
        CHECK_EQ(g_fake.TxLogDw[ap][5], 0, "route high");
        CHECK_EQ(g_fake.TxLogDw[ap][6], 1, "route low");
        CHECK_EQ(g_fake.TxLogDw[ap][7], 5, "connection id in bits 7:0");
    }
    n = actions(act, 8);
    CHECK_EQ(n, 1, "tunnel up");
    CHECK(act[0].RouteValid && act[0].Route.Lo == 1, "the action carries the route");
    CHECK_EQ(act[0].Link, 1, "link from the route's first level");
    CHECK_EQ(act[0].Depth, 1, "depth from the route's length");

    /* Disconnect by route. */
    trDisconnected(1, 0);
    idle(200);
    n = actions(act, 8);
    CHECK_EQ(n, 1, "tunnel down");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_DISCONNECTED, "by route");
    ev[0] = 0x01000004UL;
    ev[1] = 0;
    CHECK(!TbtIcmProfileTitanRidge.DecodeDisconnected(ev, 2, &g), "two words: too short for a route");

    /* Tolerance: the layout guessed wrong shows as an error flag or as no
     * answer; either leaves the device unapproved and the thread free. */
    setup(GEN_TR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ApproveMode = MODE_ERROR;
    trConnected(6, 0, 1, kUuid, 0, 0);
    idle(600);
    CHECK_EQ(g_icm.Counters.ApprovalsFailed, 1, "error flag: unapproved");
    CHECK_EQ(actions(act, 8), 0, "no tunnel");
    setup(GEN_TR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ApproveMode = MODE_NEVER;
    trConnected(6, 0, 1, kUuid, 0, 0);
    fakePump();
    t0 = g_fake.NowUs;
    TbtIcmPoll(&g_icm);
    CHECK_EQ(g_fw.Approves, 4, "bounded retries");
    CHECK(g_fake.NowUs - t0 < 41000000UL, "bounded time");
    CHECK_EQ(g_icm.Counters.ApprovalsUnknown, 1, "timeout: outcome unknown, nothing hung");
    {
        unsigned long shortEv[7];
        memset(shortEv, 0, sizeof(shortEv));
        shortEv[0] = 0x0100b103UL;
        fakeQueue(TBT_PDF_ICM_EVENT, shortEv, 7, 0, 0);
        idle(200);
        CHECK_EQ(g_icm.Counters.EventsMalformed, 1, "seven words: too short, ignored");
    }

    /* Driver ready: 250 ms with ten retries (icm_tr_driver_ready). */
    setup(GEN_TR, 0, 0);
    g_fw.ReadyMode = MODE_NEVER;
    t0 = g_fake.NowUs;
    CHECK_EQ(start(), TBT_E_TIMEOUT, "refused");
    CHECK_EQ(g_fw.Ready, 11, "eleven tries");
    CHECK(g_fake.NowUs - t0 < 3500000UL, "about 2.75 s");

    /* Suspend: no save devices on Titan Ridge, driver unloads only. */
    setup(GEN_TR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmSuspend(&g_icm);
    CHECK_EQ(g_fake.MboxCount, 2, "0x23, then one at suspend");
    CHECK_EQ(g_fake.MboxLog[1], 0x07, "driver unloads only");
}

static void test_ice_lake(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;
    unsigned long veto[2];

    setup(GEN_ICL, 1, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    CHECK_EQ(g_fake.MboxCount, 0, "no mode query on Ice Lake: no 0x23");
    CHECK_EQ(countTx(TBT_PDF_ICM_CMD, 4), 0, "no approve on Ice Lake (icm_icl_ops)");
    CHECK_EQ(g_icm.Counters.ApprovalsUnsupported, 1, "counted");
    CHECK_EQ(actions(act, 8), 0, "no tunnel for an unapproved device");

    setup(GEN_ICL, 1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    CHECK_EQ(g_icm.Ready.ProtocolVersion, 3, "protocol version read");
    CHECK_EQ(g_icm.Ready.SecurityLevel, 0, "no security level on this row");
    CHECK_EQ(g_icm.Ready.BootAclCount, 0, "no boot ACL count either");
    n = actions(act, 8);
    CHECK_EQ(n, 1, "an approved device: tunnel up");

    veto[0] = 0x0100000aUL;
    veto[1] = 0x00000001UL;
    fakeQueue(TBT_PDF_ICM_EVENT, veto, 2, 0, 0);
    idle(200);
    CHECK(g_icm.VetoActive, "RTD3 veto recorded");
    veto[1] = 0;
    fakeQueue(TBT_PDF_ICM_EVENT, veto, 2, 0, 0);
    idle(200);
    CHECK(!g_icm.VetoActive, "veto ended");

    TbtIcmSuspend(&g_icm);
    CHECK_EQ(g_fake.MboxCount, 0, "no mailbox at suspend (icm_icl_ops has no suspend)");
    CHECK_EQ(g_fake.TxCtrl, 0, "rings stopped");
    TbtIcmUnbind(&g_icm);
    CHECK_EQ(g_fake.MboxCount, 1, "unbind closes the session (icm_stop)");
    CHECK_EQ(g_fake.MboxLog[0], 0x07, "driver unloads");

    /* 20 s driver ready (icm_icl_driver_ready). */
    CHECK_EQ(TbtIcmProfileIceLake.ReadyTimeoutMs, 20000, "20 s");
}

static void test_falcon_ridge(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;
    const TBT_ICM_DEVICE *d;

    setup(GEN_FR, 1, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    CHECK_EQ(g_fake.MboxCount, 0, "no mode query on Falcon Ridge: no 0x23");
    CHECK_EQ(g_icm.Ready.SecurityLevel, 1, "security level from the u16");
    CHECK_EQ(g_fw.Topos, 1, "topology asked once");
    CHECK_EQ(g_fw.Routes, 0, "no get route");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && d->RouteValid, "route from the topology");
    if (d != NULL) {
        CHECK_EQ(d->Route.Lo, 1, "switch 1's route");
    }
    CHECK_EQ(g_fw.Approves, 1, "approved with key and id");
    n = actions(act, 8);
    CHECK_EQ(n, 1, "tunnel up");

    /* A depth-2 device: the walk follows switch 1's port to switch 2. */
    arConnected(9, 1, 1, 2, kUuid2, 0);
    idle(200);
    d = TbtIcmDevice(&g_icm, 1);
    CHECK(d != NULL && d->RouteValid && d->Route.Lo == 0x0301, "depth 2: route 0x0301");

    /* The topology with the error flag: unrouted, so not approved. */
    setup(GEN_FR, 1, 0);
    g_fw.TopoMode = MODE_ERROR;
    CHECK_EQ(start(), TBT_OK, "bind");
    CHECK_EQ(g_icm.Counters.RouteFailures, 1, "route failure");
    CHECK_EQ(g_fw.Approves, 0, "no approve while unrouted");

    TbtIcmSuspend(&g_icm);
    CHECK_EQ(g_fake.MboxCount, 2, "save devices and driver unloads");
    CHECK_EQ(g_fake.MboxLog[0], 0x05, "0x05");
    CHECK_EQ(g_fake.MboxLog[1], 0x07, "0x07");
}

/* ------------------------------------------------------------------ */
/* Races, subtrees, overflow, session boundaries                      */
/* ------------------------------------------------------------------ */

static void test_late_reply_same_device(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long old[6];

    /* Approve of connection 6 never answered; the device leaves inside the
     * wait and comes back as connection 7, whose approve fails - while the
     * firmware's answer to connection 6 arrives inside that second wait. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ApproveMode = MODE_NEVER;
    g_fw.DisconnectInApproveUs = 1000000UL;
    arConnected(6, 0, 1, 1, kUuid, 0);
    idle(1400);
    CHECK_EQ(g_icm.Counters.ApprovalsAbandoned, 1, "first approve abandoned");
    actions(act, 8);

    g_fw.ApproveMode = MODE_ERROR;
    g_fw.ApproveDelayUs = 2000000UL;
    old[0] = 0x01000004UL;
    memcpy(&old[1], kUuid, 16);
    old[5] = 0x00000610UL;          /* connection 6's words */
    arConnected(7, 0, 1, 1, kUuid, 0);
    fakeQueue(TBT_PDF_ICM_RESP, old, 6, 500000UL, 0);
    idle(2600);
    CHECK_EQ(g_icm.Counters.ApprovalsOk, 0, "the old answer approves nothing");
    CHECK_EQ(g_icm.Counters.ApprovalsFailed, 1, "connection 7 failed on its own answer");
    CHECK(g_icm.Counters.Strays >= 1, "the old answer was a stray");
    CHECK(TbtIcmDevice(&g_icm, 0) != NULL && !TbtIcmDevice(&g_icm, 0)->Approved,
          "left unapproved");
    CHECK_EQ(actions(act, 8), 0, "no tunnel up");
}

static void test_subtree_disconnect(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;

    /* Alpine Ridge: a chain on link 1 (depths 1 and 2) and a device on
     * link 3; the depth-1 device leaves. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    arConnected(5, 1, 1, 1, kUuid, 0);
    arConnected(6, 1, 1, 2, kUuid2, 0);
    {
        unsigned long u3[4] = { 0x11111111UL, 0x22222222UL, 0xffffffffUL, 0xffffffffUL };
        arConnected(7, 1, 3, 1, u3, 0);
    }
    idle(200);
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 3, "three devices");
    actions(act, 8);
    arDisconnected(1, 1, 0);
    idle(200);
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 1, "the chain below went with it");
    CHECK_EQ(g_linkDowns, 2, "a link-down for each");
    n = actions(act, 8);
    CHECK_EQ(n, 2, "two tunnels down");
    CHECK_EQ(act[0].Depth, 2, "deepest first");
    CHECK_EQ(act[1].Depth, 1, "then the device named");
    CHECK_EQ(g_icm.Counters.DisconnectUnknown, 0, "nothing unknown");

    /* A parent replaced (another device announced at its address) takes
     * its child with it. */
    arConnected(5, 1, 1, 1, kUuid, 0);
    arConnected(6, 1, 1, 2, kUuid2, 0);
    idle(200);
    actions(act, 8);
    {
        unsigned long u4[4] = { 0x44444444UL, 0x55555555UL, 0xffffffffUL, 0xffffffffUL };
        arConnected(8, 1, 1, 1, u4, 0);
    }
    idle(200);
    n = actions(act, 8);
    CHECK_EQ(n, 3, "the old parent and its child down, the new one up");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_REPLACED, "child replaced");
    CHECK_EQ(act[0].Depth, 2, "deepest first");
    CHECK_EQ(act[1].Why, TBT_ICM_WHY_REPLACED, "parent replaced");
    CHECK_EQ(act[2].Kind, TBT_ICM_ACT_TUNNEL_UP, "the new device up");

    /* Titan Ridge: route 0x0301 lies below route 1. */
    setup(GEN_TR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    trConnected(5, 1, 1, kUuid, 0, 0);
    trConnected(6, 1, 0x0301, kUuid2, 0, 0);
    idle(200);
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 2, "two devices");
    actions(act, 8);
    trDisconnected(1, 0);
    idle(200);
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 0, "both gone");
    n = actions(act, 8);
    CHECK_EQ(n, 2, "two tunnels down");
    CHECK_EQ(act[0].Route.Lo, 0x0301, "the deeper one first");
}

static void test_queue_overflow_and_coalescing(void)
{
    TBT_ICM_ACTION act[64];
    unsigned long i;
    unsigned long u[4];
    const TBT_ICM_DEVICE *d;
    int found;

    /* Device X's approve fails after 3 s; inside that wait the firmware
     * announces kUuid2 twice and then sixteen more devices, the last of
     * them the dock (kUuid, unapproved): seventeen distinct devices for a
     * queue of sixteen, so the dock's announcement is lost. The next driver
     * ready makes the firmware announce the dock again (the model announces
     * it on every driver ready), and it is approved. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ErrorApproves = 1;
    g_fw.ApproveDelayUs = 3000000UL;
    u[2] = 0xffffffffUL;
    u[3] = 0xffffffffUL;
    u[0] = 0x7777UL;
    u[1] = 0x8888UL;
    arConnected(6, 0, 2, 1, u, 0);
    arConnected(2, 1, 3, 1, kUuid2, 100000UL);
    arConnected(3, 1, 3, 1, kUuid2, 200000UL);
    for (i = 0; i < 15; i++) {
        u[0] = 0x5000UL + i;
        u[1] = 0x6000UL + i;
        arConnected(10 + i, 1, 3, 2 + (i % 4), u, 300000UL + 10000UL * i);
    }
    arConnected(5, 0, 1, 1, kUuid, 600000UL);
    g_fw.Present = 1;
    g_fw.Approved = 0;
    fakePump();
    TbtIcmPoll(&g_icm);
    CHECK_EQ(g_icm.Counters.QueueOverflows, 1, "one did not fit");
    CHECK_EQ(g_icm.Counters.EventsQueued, 18, "seventeen queued in the wait, the re-announcement in the second host poll");
    CHECK_EQ(g_icm.Counters.Rediscoveries, 1, "driver ready sent again");
    CHECK_EQ(g_fw.Ready, 2, "the firmware saw a second driver ready");
    CHECK_EQ(g_fake.MboxCount, 1, "no 0x23 and no ring restart for it");
    found = 0;
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        d = TbtIcmDevice(&g_icm, i);
        if (d != NULL && d->Uuid[0] == kUuid[0]) {
            found = 1;
            CHECK(d->Approved, "the lost device recovered and approved");
        }
    }
    CHECK(found, "the lost device is in the table");
    CHECK_EQ(g_fw.LastApprove[1], kUuid[0], "approved by an approve of its own UUID");
    CHECK_EQ(g_fw.LastApprove[5] >> 8, g_fw.ConnId, "with the connection id of the re-announcement");
    CHECK(!g_icm.RediscoverPending, "nothing left pending");
    actions(act, 64);

    /* Coalescing keeps the order of what lies between: A@1, B@1, A@1 again
     * must end with A at link 1, not B. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ErrorApproves = 1;
    g_fw.ApproveDelayUs = 1000000UL;
    u[0] = 0x7777UL;
    u[1] = 0x8888UL;
    arConnected(6, 0, 2, 1, u, 0);
    arConnected(7, 1, 1, 1, kUuid, 100000UL);
    arConnected(8, 1, 1, 1, kUuid2, 200000UL);
    arConnected(9, 1, 1, 1, kUuid, 300000UL);
    fakePump();
    TbtIcmPoll(&g_icm);
    found = 0;
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        d = TbtIcmDevice(&g_icm, i);
        if (d != NULL && d->Link == 1) {
            CHECK_EQ(d->Uuid[0], kUuid[0], "the latest occupant of link 1 wins");
            CHECK_EQ(d->ConnectionId, 9, "with its latest connection id");
            found++;
        }
    }
    CHECK_EQ(found, 1, "one device at link 1");
}

static void test_action_overflow(void)
{
    TBT_ICM_ACTION a;
    unsigned long i;
    unsigned long downs;
    unsigned long ups;
    int resync;

    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    for (i = 0; i < TBT_ICM_ACTION_QUEUE + 1; i++) {
        arConnected(5 + i, 1, 1, 1, kUuid, 0);
        arDisconnected(1, 1, 1000);
        idle(200);
    }
    CHECK_EQ(g_icm.Counters.ActionOverflows, 1, "the forty-ninth down did not fit");
    downs = 0;
    ups = 0;
    resync = 0;
    while (TbtIcmNextAction(&g_icm, &a)) {
        if (a.Kind == TBT_ICM_ACT_TUNNEL_DOWN) {
            downs++;
        } else if (a.Kind == TBT_ICM_ACT_TUNNEL_UP) {
            ups++;
        } else if (a.Kind == TBT_ICM_ACT_RESYNC) {
            resync++;
        }
    }
    CHECK_EQ(ups, 0, "stale tunnel-ups were given up first");
    CHECK_EQ(downs, TBT_ICM_ACTION_QUEUE, "every tunnel-down that fitted kept");
    CHECK_EQ(resync, 1, "the lost one became a resync");
}

static void test_session_boundary(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long i;
    int found;

    /* An announcement drained by the driver's own wait before a suspend is
     * not processed after the resume. */
    setup(GEN_AR, 1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    actions(act, 8);
    arConnected(9, 1, 3, 1, kUuid2, 0);
    fakePump();
    TbtIcmService(&g_icm);
    CHECK_EQ(g_icm.QueueCount, 1, "queued by the service drain");
    TbtIcmSuspend(&g_icm);
    CHECK_EQ(g_icm.QueueCount, 0, "retired at the suspend");
    CHECK_EQ(TbtIcmResume(&g_icm), TBT_OK, "resume");
    found = 0;
    for (i = 0; i < TBT_ICM_MAX_DEVICES; i++) {
        if (TbtIcmDevice(&g_icm, i) != NULL &&
            TbtIcmDevice(&g_icm, i)->Uuid[0] == kUuid2[0]) {
            found = 1;
        }
    }
    CHECK(!found, "no phantom device");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 1, "the dock, re-announced, stays");
}

static void test_refresh_reapplies_policy(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;

    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ApproveMode = MODE_ERROR;
    arConnected(6, 0, 1, 1, kUuid, 0);
    idle(600);
    CHECK_EQ(g_icm.Counters.ApprovalsFailed, 1, "first approve failed");
    g_fw.ApproveMode = MODE_ANSWER;
    arConnected(7, 0, 1, 1, kUuid, 0);
    idle(800);
    CHECK_EQ(g_icm.Counters.Refreshes, 1, "a refresh");
    CHECK_EQ(g_fw.Approves, 2, "and the policy ran again");
    CHECK_EQ(g_fw.LastApprove[5], 0x00000710UL, "with the new connection id");
    CHECK_EQ(g_icm.Counters.ApprovalsOk, 1, "approved");
    n = actions(act, 8);
    CHECK_EQ(n, 1, "tunnel up");
}

static void test_reconcile_bounded(void)
{
    unsigned long dw[61];

    /* Resume with a stopped clock and the firmware announcing without
     * end: the reconciliation's windows are finite. */
    setup(GEN_AR, 1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmSuspend(&g_icm);
    g_fake.ClockStopped = 1;
    g_fw.HoldUs = 0;
    dw[0] = 0x0100b103UL;
    memcpy(&dw[1], kUuid, 16);
    dw[5] = 0x09110710UL;
    nameBlock(&dw[6], 55);
    memcpy(g_fake.FloodDw, dw, sizeof(dw));
    g_fake.FloodWords = 61;
    g_fake.FloodPdf = TBT_PDF_ICM_EVENT;
    CHECK_EQ(TbtIcmResume(&g_icm), TBT_OK, "resume returns");
    g_fake.FloodPdf = 0;
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 1, "the dock kept");
}


static void test_round3(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;
    unsigned long t0;
    const TBT_ICM_DEVICE *d;

    /* A driver-ready answer shorter than the row's structure is not taken:
     * the wait goes on and ends in its bound. */
    setup(GEN_TR, 0, 0);
    g_fw.ReadyMode = MODE_NEVER;
    {
        unsigned long one[1] = { 0x01004003UL };
        fakeQueue(TBT_PDF_ICM_RESP, one, 1, 1000, 0);
    }
    CHECK_EQ(start(), TBT_E_TIMEOUT, "a one-word TR driver ready is not an answer");
    CHECK(g_icm.Counters.EventsMalformed >= 1, "counted as malformed");

    /* FR and AR driver ready follow XhciCmCmdTimeoutMs. */
    setup(GEN_AR, 0, 0);
    TbtIcmSetTimeouts(&g_icm, 2000, 0);
    g_fw.ReadyMode = MODE_NEVER;
    t0 = g_fake.NowUs;
    CHECK_EQ(start(), TBT_E_TIMEOUT, "refused");
    CHECK(g_fake.NowUs - t0 >= 8000000UL && g_fake.NowUs - t0 < 9000000UL,
          "four 2 s attempts");

    /* Falcon Ridge topology: one packet missing - abandoned whole. */
    setup(GEN_FR, 1, 0);
    g_fw.TopoMode = MODE_MISSING;
    CHECK_EQ(start(), TBT_OK, "bind");
    CHECK_EQ(g_fw.Topos, 4, "each attempt timed out incomplete");
    CHECK_EQ(g_icm.Counters.RouteFailures, 1, "route failure");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && !d->RouteValid, "unrouted");
    /* The last packet first: still complete. */
    setup(GEN_FR, 1, 0);
    g_fw.TopoMode = MODE_REVERSE;
    CHECK_EQ(start(), TBT_OK, "bind");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && d->RouteValid && d->Route.Lo == 1, "out of order: assembled");
    /* Two incomplete answers never combine. */
    setup(GEN_FR, 1, 0);
    g_fw.TopoMode = MODE_SPLIT;
    CHECK_EQ(start(), TBT_OK, "bind");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && !d->RouteValid, "halves of two answers: not a route");

    /* Approved and unrouted, then announced unapproved: the re-plug is a
     * replacement and the new connection goes to the policy. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.RouteMode = MODE_NEVER;
    arConnected(5, 1, 1, 1, kUuid, 0);
    idle(200);
    actions(act, 8);
    g_fw.RouteMode = MODE_ANSWER;
    arConnected(6, 0, 1, 1, kUuid, 0);
    idle(600);
    CHECK_EQ(g_icm.Counters.ApprovalsOk, 1, "the new connection approved");
    n = actions(act, 8);
    CHECK_EQ(n, 2, "the old tunnel down, the new one up");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_TUNNEL_DOWN, "down");
    CHECK_EQ(act[1].Why, TBT_ICM_WHY_APPROVED, "up after approve");

    /* Unrouted and unapproved, then announced approved: recovered in
     * place, the child below it kept. */
    setup(GEN_AR, 0, 0);
    TbtIcmSetApprovePolicy(&g_icm, FALSE);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.RouteMode = MODE_NEVER;
    arConnected(5, 0, 1, 1, kUuid, 0);
    idle(200);
    g_fw.RouteMode = MODE_ANSWER;
    arConnected(6, 1, 1, 2, kUuid2, 0);
    idle(200);
    actions(act, 8);
    arConnected(7, 1, 1, 1, kUuid, 0);
    idle(200);
    n = actions(act, 8);
    CHECK_EQ(n, 1, "one action: the parent's tunnel up, nothing down");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_TUNNEL_UP, "up");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 2, "the child kept");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && d->RouteValid && d->Approved, "routed and approved");

    /* Short responses are dropped before anything is read from them: a
     * header-only error answer to the topology query, then the real one. */
    setup(GEN_FR, 1, 0);
    g_fw.TopoShortFirst = 1;
    CHECK_EQ(start(), TBT_OK, "bind");
    d = TbtIcmDevice(&g_icm, 0);
    CHECK(d != NULL && d->RouteValid, "the short error answer did not end the query");
    /* A truncated approve answer is logged as malformed, not a stray. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fw.ApproveMode = MODE_NEVER;
    {
        unsigned long tr[3] = { 0x01000004UL, 0xa0211100UL, 0x003d0efcUL };
        fakeQueue(TBT_PDF_ICM_RESP, tr, 3, 1000000UL, 0);
    }
    arConnected(6, 0, 1, 1, kUuid, 0);
    fakePump();
    t0 = g_icm.Counters.EventsMalformed;
    TbtIcmPoll(&g_icm);
    CHECK(g_icm.Counters.EventsMalformed > t0, "truncated approve answer: malformed");

    /* A ring start that wrote registers and then refused: failed. */
    setup(GEN_AR, 0, 0);
    g_fake.TxIndexOverride = 0x00050003UL;
    CHECK_EQ(start(), TBT_E_HW, "ring refused after programming");
    CHECK(g_icm.Failed, "a failure after activation");
    g_fake.TxIndexOverride = 0;
    CHECK_EQ(TbtIcmResume(&g_icm), TBT_E_STOPPED, "no second activation");
    CHECK_EQ(g_fake.MboxCount, 0, "no mailbox: no session");

    /* A failed resume and an unbind retire queued work. */
    setup(GEN_AR, 1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    CHECK_EQ(g_icm.ActionCount, 1, "a tunnel-up queued, not yet taken");
    TbtIcmSuspend(&g_icm);
    g_fw.ReadyMode = MODE_NEVER;
    CHECK(TbtIcmResume(&g_icm) != TBT_OK, "resume fails");
    CHECK(!TbtIcmNextAction(&g_icm, &act[0]), "nothing of the old session runs");
    setup(GEN_AR, 1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmUnbind(&g_icm);
    CHECK(!TbtIcmNextAction(&g_icm, &act[0]), "unbind retires the queue");
}

static void test_settings(void)
{
    setup(GEN_AR, 0, 0);
    CHECK_EQ(g_icm.CmdTimeoutMs, 5000, "default command timeout");
    CHECK_EQ(g_icm.ApproveTimeoutMs, 10000, "default approve timeout");
    CHECK(g_icm.ApproveAll, "XhciCmApprove defaults to 1");
    TbtIcmSetTimeouts(&g_icm, 10, 999999);
    CHECK_EQ(g_icm.CmdTimeoutMs, 1000, "held to 1000");
    CHECK_EQ(g_icm.ApproveTimeoutMs, 60000, "held to 60000");
    TbtIcmSetTimeouts(&g_icm, 0, 0);
    CHECK_EQ(g_icm.CmdTimeoutMs, 5000, "0 is the default");
    TbtIcmPoll(&g_icm);
    CHECK_EQ(g_icm.Counters.IdlePolls, 0, "no poll before a bind");
    TbtIcmUnbind(&g_icm);
    CHECK_EQ(g_fake.Writes, 0, "an unbind before any start touches nothing");
}

/* ------------------------------------------------------------------ */
/* The host interface lost, cancelled and recovered (cm_live.h)       */
/* ------------------------------------------------------------------ */

static int g_ones;
static int g_cancel;
static TBT_HOST g_liveHost;

static ULONG liveRead32(PVOID ctx, ULONG off)
{
    if (g_ones) {
        return 0xFFFFFFFFUL;
    }
    return fakeRead32(ctx, off);
}

static BOOLEAN liveCancelled(PVOID ctx)
{
    (void)ctx;
    return g_cancel ? TRUE : FALSE;
}

static void setupLive(int present, int approved)
{
    setup(GEN_AR, present, approved);
    g_ones = 0;
    g_cancel = 0;
    g_liveHost = g_fakeHost;
    g_liveHost.Read32 = liveRead32;
    g_liveHost.Cancelled = liveCancelled;
    TbtIcmInit(&g_icm, rowFor(GEN_AR), &g_ring, &g_liveHost, 0);
    TbtIcmSetLinkDown(&g_icm, onLinkDown, NULL);
}

static void test_lost_and_recover(void)
{
    TBT_ICM_ACTION act[8];
    unsigned long n;
    unsigned long before;
    unsigned long stale[2];
    unsigned long t0;
    TBT_FRAME f;

    /* Lost: every entry leaves as UNAVAILABLE, a RESYNC follows, nothing
     * is written to the host interface. */
    setupLive(1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    CHECK(g_ring.DmaMayRun, "a started ring may DMA");
    actions(act, 8);
    before = g_fake.Writes;
    TbtIcmLost(&g_icm);
    CHECK_EQ(g_fake.Writes, before, "lost writes nothing");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 0, "table emptied");
    n = actions(act, 8);
    CHECK_EQ(n, 2, "a tunnel-down and a resync");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_TUNNEL_DOWN, "tunnel down first");
    CHECK_EQ(act[0].Why, TBT_ICM_WHY_UNAVAILABLE, "why unavailable");
    CHECK(act[0].WasApproved, "it carried a tunnel");
    CHECK_EQ(act[1].Kind, TBT_ICM_ACT_RESYNC, "then the domain");
    CHECK_EQ(act[1].Why, TBT_ICM_WHY_UNAVAILABLE, "resync unavailable");
    TbtIcmLost(&g_icm);
    n = actions(act, 8);
    CHECK_EQ(n, 1, "an empty table still resyncs");
    CHECK_EQ(act[0].Kind, TBT_ICM_ACT_RESYNC, "resync alone");
    CHECK_EQ(g_icm.Counters.Lost, 2, "counted");

    /* Light: a stale frame of the old session is drained unread, driver
     * ready goes out again, no 0x23, the device comes back announced. */
    stale[0] = 0x01000003UL;
    stale[1] = 0x00000000UL;
    CHECK(TbtRingIntact(&g_ring), "both rings as programmed");
    g_fake.RxCtrl = 0;
    CHECK(!TbtRingIntact(&g_ring), "an RX control cleared is noticed");
    g_fake.RxCtrl = 0xC0000000UL;
    g_fake.TxBaseHi = 1;
    CHECK(!TbtRingIntact(&g_ring), "a TX high base is noticed");
    g_fake.TxBaseHi = 0;
    CHECK(TbtRingIntact(&g_ring), "intact again");
    g_fake.TxCtrl |= 0x00000002UL;
    CHECK(!TbtRingIntact(&g_ring), "a control bit the start never set is noticed");
    g_fake.TxCtrl &= ~0x00000002UL;
    before = g_fake.RxPdf;
    g_fake.RxPdf = 0;
    CHECK(!TbtRingIntact(&g_ring), "a changed RX PDF mask is noticed");
    g_fake.RxPdf = before;
    CHECK(TbtRingIntact(&g_ring), "intact once more");
    fakeQueue(TBT_PDF_ICM_RESP, stale, 2, 300000UL, 0);
    CHECK_EQ(TbtIcmRecover(&g_icm, FALSE), TBT_E_STATE,
             "a frame arriving in the quiet period refuses the light recovery");
    fakeQueue(TBT_PDF_ICM_RESP, stale, 2, 0, 0);
    fakePump();
    CHECK_EQ(TbtIcmRecover(&g_icm, FALSE), TBT_OK, "light recovery");
    CHECK_EQ(g_fw.Ready, 2, "driver ready again");
    CHECK_EQ(g_fake.MboxCount, 1, "no 0x23 on a light recovery");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 1, "re-announced");
    n = actions(act, 8);
    CHECK(n >= 1 && act[0].Kind == TBT_ICM_ACT_TUNNEL_UP, "its tunnel up again");

    /* Full, after a power cycle reset the rings: restarted on the same
     * buffer, 0x23 again, the session rebuilt. */
    g_fake.TxCtrl = 0;
    g_fake.RxCtrl = 0;
    g_fake.TxBaseLo = 0;
    g_fake.RxSizeReg = 0;
    CHECK_EQ(TbtIcmRecover(&g_icm, TRUE), TBT_OK, "full recovery");
    CHECK_EQ(g_fake.MboxLog[g_fake.MboxCount - 1], 0x23, "allow-all on the reset ICM");
    CHECK_EQ(g_fake.TxBaseLo, FAKE_PHYS, "TX base programmed again");
    CHECK(g_icm.Bound, "bound");
    CHECK_EQ(g_icm.Counters.Recoveries, 3, "three recoveries, one refused");

    /* An unanswered command makes the next light recovery a full one. */
    before = g_fake.MboxCount;
    g_icm.Ambiguous = TRUE;
    CHECK_EQ(TbtIcmRecover(&g_icm, FALSE), TBT_OK, "escalated recovery");
    CHECK_EQ(g_fake.MboxCount, before + 1, "the ring restart sent 0x23");
    CHECK(!g_icm.Ambiguous, "a restarted ring clears the ambiguity");

    /* Lost drops a queued TUNNEL_UP (the recovery's, undelivered) and
     * keeps the DOWNs in order. */
    actions(act, 8);
    g_fw.Present = 0;
    arDisconnected(1, 1, 0);
    idle(200);
    actions(act, 8);
    g_fw.Present = 1;
    arConnected(7, 0, 1, 1, kUuid, 0);
    idle(200);
    CHECK(g_icm.ActionCount >= 1, "an approve queued an UP");
    TbtIcmLost(&g_icm);
    n = actions(act, 8);
    CHECK(n >= 1 && act[n - 1].Kind == TBT_ICM_ACT_RESYNC, "resync last");
    for (before = 0; before < n; before++) {
        CHECK(act[before].Kind != TBT_ICM_ACT_TUNNEL_UP, "no UP after a loss");
    }

    /* A verified stop clears DmaMayRun; an unverified one keeps it, and a
     * full recovery then refuses to reuse the buffer. */
    TbtIcmUnbind(&g_icm);
    CHECK(!g_ring.DmaMayRun, "verified stop");
    setupLive(1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_ones = 1;
    TbtIcmUnbind(&g_icm);
    CHECK(g_ring.DmaMayRun, "unanswered read-back keeps DmaMayRun");
    CHECK_EQ(g_ring.Counters.StopUnverified, 1, "counted");
    g_fake.TxBaseLo = 0x1234UL;
    CHECK_EQ(TbtIcmRecover(&g_icm, TRUE), TBT_E_STATE, "full recovery refused");
    CHECK_EQ(g_fake.TxBaseLo, 0x1234UL, "no base reprogrammed");
    g_ones = 0;
    CHECK(TbtIcmResume(&g_icm) != TBT_OK, "nor a resume on the same buffer");
    CHECK_EQ(g_fake.TxBaseLo, 0x1234UL, "still nothing reprogrammed");

    /* A retry that succeeds still leaves the first attempt's answer
     * possible: ambiguous. */
    setupLive(0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    CHECK(!g_icm.Ambiguous, "a fresh session is not ambiguous");
    g_fw.ApproveDelayUs = 10500000UL;
    g_fw.Present = 1;
    arConnected(6, 0, 1, 1, kUuid, 0);
    idle(25000);
    CHECK_EQ(g_icm.Counters.ApprovalsOk, 1, "approved on the retry");
    CHECK(g_icm.Ambiguous, "the timed-out attempt left it ambiguous");

    /* An escalated recovery reconciles: a device gone in the outage, with
     * no event, leaves the table. */
    setupLive(1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    actions(act, 8);
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 1, "one device");
    g_fw.Present = 0;
    g_icm.Ambiguous = TRUE;
    CHECK_EQ(TbtIcmRecover(&g_icm, FALSE), TBT_OK, "escalated to full");
    CHECK_EQ(TbtIcmDeviceCount(&g_icm), 0, "the vanished device reconciled");
    n = actions(act, 8);
    CHECK(n >= 1 && act[0].Kind == TBT_ICM_ACT_TUNNEL_DOWN &&
          act[0].Why == TBT_ICM_WHY_RECONCILED, "its tunnel-down");

    /* Cancelled: a mailbox wait ends at once, the receive takes nothing. */
    setupLive(0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    g_fake.MboxMode = FAKE_MBOX_HANG;
    g_cancel = 1;
    t0 = g_fake.NowUs;
    CHECK_EQ(TbtIcmMailbox(&g_icm, 0x07, 0), TBT_E_TIMEOUT, "cancelled mailbox");
    CHECK(g_fake.NowUs - t0 < 100000UL, "within 100 ms");
    fakeQueue(TBT_PDF_ICM_RESP, stale, 2, 0, 0);
    fakePump();
    CHECK(!TbtRingReceive(&g_ring, &f), "no descriptor taken when cancelled");
    g_cancel = 0;
    CHECK(TbtRingReceive(&g_ring, &f), "taken once admitted");
}

/*
 * The resume's budget (record 21 section 6.3; TBT_ICM_RESUME_BUDGET_MS):
 * every wait clipped to what is left of it, the drain stopped when it is
 * spent, the resume then abandoned whole.
 */
static void test_resume_budget(void)
{
    TBT_ICM_ACTION act[4];
    unsigned long t0;
    unsigned long ms;
    LONG rc;

    CHECK_EQ(TBT_ICM_RESUME_BUDGET_MS, 270000UL,
             "270 s, 30 s inside the bridges' fixed 5-minute hold");

    /* Clipping: driver ready never answered, a 3 s budget - the first
     * 5 s attempt is cut at 3 s and no retry follows. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmSuspend(&g_icm);
    g_fw.ReadyMode = MODE_NEVER;
    TbtIcmSetResumeBudget(&g_icm, 3000);
    t0 = g_fake.NowUs;
    rc = TbtIcmResume(&g_icm);
    ms = (g_fake.NowUs - t0) / 1000UL;
    CHECK_EQ(rc, TBT_E_TIMEOUT, "the resume ran out of its budget");
    CHECK(ms >= 3000UL, "it waited the budget");
    CHECK(ms < 3200UL, "and no more (the 5 s attempt clipped)");
    CHECK_EQ(g_fw.Ready, 2, "one driver ready on resume, not retried past the budget");
    CHECK_EQ(g_icm.Counters.ResumeBudgetExpired, 1, "counted");
    CHECK(g_icm.Failed, "the instance failed (record 21 section 1.4.2)");
    CHECK(!g_icm.BudgetOn, "the budget runs only inside a resume");
    CHECK_EQ(TbtIcmResume(&g_icm), TBT_E_STOPPED, "and stays idle");

    /* Under the default budget the same silent firmware fails on its own
     * bound (four 5 s attempts), not on the budget. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmSuspend(&g_icm);
    g_fw.ReadyMode = MODE_NEVER;
    t0 = g_fake.NowUs;
    rc = TbtIcmResume(&g_icm);
    ms = (g_fake.NowUs - t0) / 1000UL;
    CHECK(rc != TBT_OK, "the resume fails");
    CHECK(ms >= 20000UL && ms < 21000UL, "after its four 5 s attempts");
    CHECK_EQ(g_icm.Counters.ResumeBudgetExpired, 0, "not on the budget");

    /* Expiry mid-drain: a device plugged during the sleep is announced on
     * resume; its approve is never answered (10 s attempts) and the 15 s
     * budget ends during the second - the drain stops, the resume is
     * abandoned, its actions retired. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmSuspend(&g_icm);
    g_fw.Present = 1;
    g_fw.Approved = 0;
    g_fw.ApproveMode = MODE_NEVER;
    TbtIcmSetResumeBudget(&g_icm, 15000);
    t0 = g_fake.NowUs;
    rc = TbtIcmResume(&g_icm);
    ms = (g_fake.NowUs - t0) / 1000UL;
    CHECK_EQ(rc, TBT_E_TIMEOUT, "abandoned at the budget");
    CHECK(ms >= 15000UL && ms < 15300UL, "at 15 s, not after four 10 s approves");
    CHECK_EQ(g_fw.Approves, 2, "the second approve attempt was the one cut");
    CHECK_EQ(g_icm.Counters.ResumeBudgetExpired, 1, "counted");
    CHECK_EQ(actions(act, 4), 0, "no action of the abandoned resume runs");

    /* The budget ending in the last approve attempt, with nothing queued
     * after it: still abandoned (no check follows the wait otherwise). */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmSuspend(&g_icm);
    g_fw.Present = 1;
    g_fw.Approved = 0;
    g_fw.ApproveMode = MODE_NEVER;
    TbtIcmSetResumeBudget(&g_icm, 39500);
    rc = TbtIcmResume(&g_icm);
    CHECK_EQ(g_fw.Approves, 4, "the fourth approve attempt ran, clipped");
    CHECK_EQ(rc, TBT_E_TIMEOUT, "and the resume is abandoned all the same");
    CHECK_EQ(actions(act, 4), 0, "its approve-unknown action retired");

    /* A clock that does not move: the budget still ends, by the waits
     * granted under it. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmSuspend(&g_icm);
    g_fw.ReadyMode = MODE_NEVER;
    g_fake.ClockStopped = 1;
    TbtIcmSetResumeBudget(&g_icm, 3000);
    rc = TbtIcmResume(&g_icm);
    g_fake.ClockStopped = 0;
    CHECK_EQ(rc, TBT_E_TIMEOUT, "a stopped clock still ends the budget");
    CHECK_EQ(g_fw.Ready, 2, "after one clipped driver-ready attempt");

    /* A clock that stops partway: 5 s by the clock, then 5 s and a
     * clipped 3 s by the waits made - the two shares add up, and the
     * fourth attempt never comes. */
    setup(GEN_AR, 0, 0);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmSuspend(&g_icm);
    g_fw.ReadyMode = MODE_NEVER;
    g_fw.StopClockAtReady = 3;
    TbtIcmSetResumeBudget(&g_icm, 13000);
    t0 = g_fake.AskedUs;
    rc = TbtIcmResume(&g_icm);
    ms = (g_fake.AskedUs - t0) / 1000UL;
    g_fake.ClockStopped = 0;
    CHECK(ms >= 13000UL && ms < 13200UL,
          "13 s of waits asked for in all: the third attempt clipped");
    CHECK_EQ(rc, TBT_E_TIMEOUT, "the clock's share and the waits' add up");
    CHECK_EQ(g_fw.Ready, 4, "three driver-ready attempts, the third clipped");
    CHECK_EQ(g_icm.Counters.ResumeBudgetExpired, 1, "on the budget");

    /* Host polls cut short by a flood of frames (each read ends at its
     * frame budget, having waited little): the budget is charged what
     * was waited, not each read's timeout. */
    setup(GEN_AR, 1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmSuspend(&g_icm);
    g_fw.HoldUs = 3000000UL;
    g_fake.FloodPdf = TBT_PDF_ICM_EVENT;
    g_fake.FloodWords = 0;
    t0 = g_fake.NowUs;
    rc = TbtIcmResume(&g_icm);
    ms = (g_fake.NowUs - t0) / 1000UL;
    g_fake.FloodPdf = 0;
    CHECK(g_icm.Counters.HostPollTries >= 2, "host polls ran in the flood");
    CHECK_EQ(g_icm.Counters.ResumeBudgetExpired, 0, "the budget not spent");
    CHECK(g_icm.BudgetUsedMs <= ms + 1UL,
          "charged no more than the time that passed");
    CHECK_EQ(rc, TBT_OK, "and the resume completes");

    /* A resume inside its budget is unchanged (the earlier legs), and an
     * idle poll after a bind is never clipped. */
    setup(GEN_AR, 1, 1);
    CHECK_EQ(start(), TBT_OK, "bind");
    TbtIcmSuspend(&g_icm);
    TbtIcmSetResumeBudget(&g_icm, 0);
    CHECK_EQ(g_icm.ResumeBudgetMs, TBT_ICM_RESUME_BUDGET_MS, "0 is the default");
    CHECK_EQ(TbtIcmResume(&g_icm), TBT_OK, "resume");
    CHECK_EQ(g_icm.Counters.ResumeBudgetExpired, 0, "within its budget");
}

int main(void)
{
    test_frames_ar();
    test_names();
    test_bind_rebind();
    test_plug_approve_unplug();
    test_policy();
    test_resume_refresh_and_reconcile();
    test_disconnect_during_approve();
    test_timeouts_and_errors();
    test_wrong_layout();
    test_odd_frames();
    test_event_queued_in_wait();
    test_dual_link_and_replace();
    test_titan_ridge();
    test_ice_lake();
    test_falcon_ridge();
    test_late_reply_same_device();
    test_subtree_disconnect();
    test_queue_overflow_and_coalescing();
    test_action_overflow();
    test_session_boundary();
    test_refresh_reapplies_policy();
    test_reconcile_bounded();
    test_round3();
    test_settings();
    test_resume_budget();
    test_lost_and_recover();
    printf("test_tbt_icm: %d checks, %d failures\n", checks, failures);
    return failures;
}
