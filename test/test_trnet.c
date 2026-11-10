/*
 * test_trnet.c - XHCITR's "Thunderbolt networking" nodes (design record 35
 * section 17, task 48.18): the reader of xhcicm.sys's networking answer
 * (xhcitr\tr_net.c) over answers its builder (src\cm\cm_qnet.c) made, and
 * over bad ones (a wrong magic or version, a cut answer, offsets, shapes
 * and sizes past the bytes, a client block too short or too long); the
 * far side's kind (Linux, xHCI98, Windows, another, none read); the
 * labels of each state; the 64-bit counts in decimal; and the details
 * text in both forms, every line within 79 columns, the snapshot's form
 * withholding the MACs, UUID, IP address and the peer's texts.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_qnetb.h"
#include "../xhcitr/tr_net.h"
#include "test_harness.h"

static UCHAR ans[CM_QN_MAX_BYTES];
static char textBuf[16384];

static void p32(UCHAR *b, ULONG o, ULONG v)
{
    b[o] = (UCHAR)(v & 0xFF);
    b[o + 1] = (UCHAR)((v >> 8) & 0xFF);
    b[o + 2] = (UCHAR)((v >> 16) & 0xFF);
    b[o + 3] = (UCHAR)((v >> 24) & 0xFF);
}

static ULONG g32x(const UCHAR *b, ULONG o)
{
    return (ULONG)b[o] | ((ULONG)b[o + 1] << 8) | ((ULONG)b[o + 2] << 16) |
           ((ULONG)b[o + 3] << 24);
}

static CM_QN_LINK_IN gl[4];
static ULONG gw[17];
static ULONG gFirst;     /* the first entry of gl given */

static ULONG build(ULONG state, ULONG links, int client, ULONG netValue)
{
    CM_QN_IN in;
    ULONG i;

    memset(&in, 0, sizeof(in));
    in.Flags = CM_QN_F_PDO | CM_QN_F_MAC | (state == CM_QN_STATE_UP ? CM_QN_F_UP : 0);
    in.EpochLo = 1;
    in.NetValue = netValue;
    in.E2EValue = 1;
    in.Cm = 2;
    in.State = state;
    in.Link = 1;
    for (i = 0; i < 6; i++) {
        in.Mac[i] = (UCHAR)(0xA0 + i);
        in.PeerMac[i] = (UCHAR)(0xB0 + i);
    }
    in.E2E = 1;
    in.Mbps = 40000;
    in.Lanes = 2;
    in.MaxFrame = 1514;
    in.RxFrames = 4294967295UL;
    in.TxFrames = 4294967295UL;
    in.TxRefused = 4294967295UL;
    in.RxErrors = 4294967295UL;
    in.RxDropped = 4294967295UL;
    in.LinkUps = 4294967295UL;
    in.LinkDowns = 4294967295UL;
    memset(gl, 0, sizeof(gl));
    gl[0].Link = 1;
    gl[0].Flags = CM_QN_LF_UUID | CM_QN_LF_BLOCK | CM_QN_LF_USB4NET | CM_QN_LF_SERVICE;
    gl[0].DiscState = 6;
    gl[0].NetState = 2;
    gl[0].Uuid[0] = 0x67452301UL;
    gl[0].Uuid[1] = 0xEFCDAB89UL;
    gl[0].VendorId = 0x1D6B;
    gl[0].DeviceId = 4;
    gl[0].DeviceRev = 0x80000100UL;
    gl[0].Prtcstns = 7;
    gl[0].VendorText = "Linux";
    gl[0].DeviceText = "secret-host-name-0123456789abcde";
    gl[0].Mbps = 40000;
    gl[0].Lanes = 2;
    gl[0].Sent = gl[0].Received = gl[0].Resends = gl[0].Malformed = 4294967295UL;
    gl[0].LoginsSent = gl[0].LoginFailures = gl[0].LoginsReceived = gl[0].LoginsRefused = 70000;
    gl[0].ServicesUp = gl[0].ServiceFailures = gl[0].Restarts = 4294967295UL;
    gl[1] = gl[0];
    gl[1].Link = 2;
    gl[1].Flags = CM_QN_LF_UUID | CM_QN_LF_BLOCK | CM_QN_LF_FIRMWARE;
    gl[1].VendorId = 0;
    gl[1].VendorText = "xHCI98";
    gl[1].DeviceText = "xhcicm";
    gl[2] = gl[0];
    gl[2].Link = 3;
    gl[2].VendorId = 0x045E;
    gl[2].VendorText = "";
    gl[2].DeviceText = "";
    gl[3] = gl[0];
    gl[3].Link = 4;
    gl[3].Flags = 0;
    gl[3].DiscState = 1;
    gl[3].NetState = 0;
    in.Links = gl + gFirst;
    in.LinkCount = links;
    if (client) {
        for (i = 0; i < 17; i++) {
            gw[i] = 4294967295UL;
        }
        gw[1] = CM_QN_DRV_NDIS5;
        gw[2] = 65522;
        gw[3] = CM_QN_CF_BYTES32 | CM_QN_CF_ASLEEP;
        in.ClientWords = gw;
        in.ClientWordCount = 17;
    }
    return CmNetQueryBuild(ans, sizeof(ans), &in);
}

/* Every line of t within 79 columns; the count of lines. */
static int lines_ok(const char *s, int *count)
{
    int col = 0;
    int ok = 1;

    *count = 0;
    for (; *s != '\0'; s++) {
        if (*s == '\r') {
            continue;
        }
        if (*s == '\n') {
            (*count)++;
            col = 0;
            continue;
        }
        if (++col > 79) {
            if (ok) {
                printf("long line ends: %.40s\n", s);
            }
            ok = 0;
        }
    }
    return ok;
}

static void test_parse(void)
{
    TR_QNET q;
    ULONG n;
    UCHAR bad[CM_QN_MAX_BYTES];

    n = build(CM_QN_STATE_UP, 4, 1, 1);
    CHECK_EQ(tr_qnet_parse(ans, n, &q), 1, "a whole answer read");
    CHECK_EQ(q.LinkCount, 4, "four links");
    CHECK(q.Client != NULL && q.ClientBytes == CM_QN_C_KNOWN_BYTES, "the client block");
    CHECK_EQ(tr_qnet_parse(ans, CM_QN_HDR_BYTES, &q), -1, "a header alone: ask again");
    CHECK_EQ(q.Bytes, n, "with the size");
    CHECK_EQ(tr_qnet_parse(ans, CM_QN_HDR_BYTES - 1, &q), 0, "under the header");
    memcpy(bad, ans, n);
    p32(bad, CM_QN_O_MAGIC, 0);
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 0, "wrong magic");
    memcpy(bad, ans, n);
    p32(bad, CM_QN_O_VERSION, 2);
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 0, "a version not known");
    memcpy(bad, ans, n);
    p32(bad, CM_QN_O_SVC_BYTES, CM_QN_SVC_MIN_BYTES - 4);
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 0, "a short service block");
    memcpy(bad, ans, n);
    p32(bad, CM_QN_O_SVC_BYTES, CM_QN_SVC_MIN_BYTES);
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 1, "a driver before 48.38: 96 bytes");
    CHECK_EQ(q.SvcBytes, CM_QN_SVC_MIN_BYTES, "its size kept");
    memcpy(bad, ans, n);
    p32(bad, CM_QN_O_SVC_OFFSET, n - 8);
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 0, "a service block past the end");
    memcpy(bad, ans, n);
    p32(bad, CM_QN_O_LINK_SHAPE, 5UL | (CM_QN_LINK_BYTES << 16));
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 0, "five links");
    memcpy(bad, ans, n);
    p32(bad, CM_QN_O_LINK_SHAPE, 4UL | ((CM_QN_LINK_BYTES - 4) << 16));
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 0, "short entries");
    memcpy(bad, ans, n);
    p32(bad, CM_QN_O_LINK_OFFSET, n - CM_QN_LINK_BYTES);
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 0, "links past the end");
    memcpy(bad, ans, n);
    p32(bad, g32x(bad, CM_QN_O_CLIENT_OFFSET), 2);
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 0, "a client block under 4 bytes");
    memcpy(bad, ans, n);
    p32(bad, g32x(bad, CM_QN_O_CLIENT_OFFSET), CM_QN_C_KNOWN_BYTES + 4);
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 0, "a client block past the answer");
    memcpy(bad, ans, n);
    p32(bad, CM_QN_O_CLIENT_OFFSET, n);
    CHECK_EQ(tr_qnet_parse(bad, n, &q), 0, "a client offset at the end");
    /* A larger entry and service block than this reader knows: read. */
    n = build(CM_QN_STATE_UP, 0, 0, 1);
    CHECK_EQ(tr_qnet_parse(ans, n, &q), 1, "no link, no client");
    CHECK(q.Client == NULL && q.LinkCount == 0, "none of either");
}

static void test_kind_label(void)
{
    TR_QNET q;
    ULONG n;
    char kind[64];
    char name[80];
    char lab[200];

    n = build(CM_QN_STATE_UP, 4, 1, 1);
    (void)tr_qnet_parse(ans, n, &q);
    CHECK_EQ(tr_qnet_kind(q.Link[0], kind, sizeof(kind), name, sizeof(name)), TR_NETK_LINUX,
             "Linux");
    CHECK(strcmp(name, "secret-host-name-0123456789abcde") == 0, "its host name");
    CHECK_EQ(tr_qnet_kind(q.Link[1], kind, sizeof(kind), name, sizeof(name)), TR_NETK_XHCI98,
             "xHCI98");
    CHECK(name[0] == '\0', "no name");
    CHECK_EQ(tr_qnet_kind(q.Link[2], kind, sizeof(kind), name, sizeof(name)), TR_NETK_WINDOWS,
             "Windows by 045Eh");
    CHECK_EQ(tr_qnet_kind(q.Link[3], kind, sizeof(kind), name, sizeof(name)), TR_NETK_NONE,
             "no block read");
    tr_qnet_label(&q, lab, sizeof(lab));
    CHECK(strcmp(lab, "Thunderbolt networking - up, 40 Gb/s") == 0, "label up");
    tr_qnet_link_label(&q, 0, lab, sizeof(lab));
    CHECK(strcmp(lab, "Link on port 1 - connected: secret-host-name-0123456789abcde") == 0,
          "link label");
    tr_qnet_link_label(&q, 1, lab, sizeof(lab));
    CHECK(strstr(lab, "a computer with xHCI98") != NULL, "xHCI98 link label");
    tr_qnet_link_label(&q, 3, lab, sizeof(lab));
    CHECK(strcmp(lab, "Link on port 4 - asking for its UUID") == 0, "discovering");
    CHECK(tr_qnet_show(&q), "shown");

    n = build(CM_QN_STATE_WAITING, 0, 0, 1);
    (void)tr_qnet_parse(ans, n, &q);
    tr_qnet_label(&q, lab, sizeof(lab));
    CHECK(strcmp(lab, "Thunderbolt networking - no other computer connected") == 0, "waiting");
    n = build(CM_QN_STATE_WAITING, 1, 0, 1);
    (void)tr_qnet_parse(ans, n, &q);
    tr_qnet_label(&q, lab, sizeof(lab));
    CHECK(strcmp(lab, "Thunderbolt networking - connecting") == 0, "connecting");
    n = build(CM_QN_STATE_NO_DRIVER, 0, 0, 1);
    (void)tr_qnet_parse(ans, n, &q);
    tr_qnet_label(&q, lab, sizeof(lab));
    CHECK(strcmp(lab, "Thunderbolt networking - no network driver") == 0, "no driver");
    n = build(CM_QN_STATE_OFF, 0, 0, 0);
    (void)tr_qnet_parse(ans, n, &q);
    CHECK(!tr_qnet_show(&q), "off and no link: not shown");
}

static void test_match(void)
{
    TR_QNET q;
    TR_QNET w;
    UCHAR was[CM_QN_MAX_BYTES];
    ULONG n;

    n = build(CM_QN_STATE_UP, 4, 0, 1);
    memcpy(was, ans, n);
    (void)tr_qnet_parse(was, n, &w);
    /* The answer with link 1 gone: the others move down. */
    gFirst = 1;
    n = build(CM_QN_STATE_UP, 2, 0, 1);
    gFirst = 0;
    (void)tr_qnet_parse(ans, n, &q);
    CHECK_EQ(tr_qnet_link_match(&q, w.Link[1]), 0, "link 2 found at its new index");
    CHECK_EQ(tr_qnet_link_match(&q, w.Link[0]), -1, "link 1 gone");
    CHECK_EQ(tr_qnet_link_match(&q, w.Link[3]), -1, "link 4 not listed");
    n = build(CM_QN_STATE_UP, 4, 0, 1);
    (void)tr_qnet_parse(ans, n, &q);
    p32(ans, g32x(ans, CM_QN_O_LINK_OFFSET) + CM_QN_L_UUID, 0x11111111UL);
    CHECK_EQ(tr_qnet_link_match(&q, w.Link[0]), -1, "another peer on link 1");
    CHECK_EQ(tr_qnet_link_match(&q, w.Link[3]), 3, "no UUID known: by link");
}

static void test_u64(void)
{
    char s[32];

    tr_qnet_u64(0, 0, s, sizeof(s));
    CHECK(strcmp(s, "0") == 0, "0");
    tr_qnet_u64(4294967295UL, 0, s, sizeof(s));
    CHECK(strcmp(s, "4294967295") == 0, "2^32 - 1");
    tr_qnet_u64(0, 1, s, sizeof(s));
    CHECK(strcmp(s, "4294967296") == 0, "2^32");
    tr_qnet_u64(4294967295UL, 4294967295UL, s, sizeof(s));
    CHECK(strcmp(s, "18446744073709551615") == 0, "2^64 - 1");
    tr_qnet_u64(0xA7640000UL, 0x0DE0B6B3UL, s, sizeof(s));
    CHECK(strcmp(s, "1000000000000000000") == 0, "10^18");
}

static void test_text(void)
{
    TR_QNET q;
    ULONG n;
    TR_TEXT t;
    int count;
    int i;

    n = build(CM_QN_STATE_UP, 4, 1, 1);
    (void)tr_qnet_parse(ans, n, &q);
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, -1,
                 "169.254.211.195 (255.255.0.0), 169.254.211.196 (255.255.0.0)", 0);
    CHECK(!t.Overflow, "fits");
    CHECK(lines_ok(textBuf, &count), "within 79 columns (window)");
    CHECK(count > 40, "the service and four links");
    CHECK(strstr(textBuf, "A0-A1-A2-A3-A4-A5") != NULL, "the MAC");
    CHECK(strstr(textBuf, "01234567-89AB-CDEF") != NULL, "the UUID, canonical");
    CHECK(strstr(textBuf, "secret-host-name") != NULL, "the peer's name");
    CHECK(strstr(textBuf, "18446744073709551615 bytes") != NULL, "64-bit bytes");
    CHECK(strstr(textBuf, "start again at 4 GB") != NULL, "the NDIS 5 note");
    CHECK(strstr(textBuf, "(asleep)") != NULL, "asleep");
    CHECK(strstr(textBuf, "end-to-end flow control, frame IDs, large frames") != NULL,
          "prtcstns");
    CHECK(strstr(textBuf, "not known (the firmware runs the link)") != NULL, "firmware speed");

    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, -1, "169.254.211.195 (255.255.0.0)", 1);
    CHECK(lines_ok(textBuf, &count), "within 79 columns (snapshot)");
    CHECK(strstr(textBuf, "A0-A1") == NULL, "no MAC in the snapshot");
    CHECK(strstr(textBuf, "B0-B1") == NULL, "no peer MAC in the snapshot");
    CHECK(strstr(textBuf, "169.254") == NULL, "no IP in the snapshot");
    CHECK(strstr(textBuf, "secret-host-name") == NULL, "no peer name in the snapshot");
    CHECK(strstr(textBuf, "01234567") == NULL, "no UUID in the snapshot");
    CHECK(strstr(textBuf, "Linux (thunderbolt_net)") != NULL, "the kind still");

    for (i = 0; i < 4; i++) {
        tr_text_init(&t, textBuf, sizeof(textBuf));
        tr_qnet_text(&t, &q, i, NULL, 0);
        CHECK(lines_ok(textBuf, &count) && count > 5, "a link's text");
    }
    n = build(CM_QN_STATE_WAITING, 0, 0, 1);
    (void)tr_qnet_parse(ans, n, &q);
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, -1, NULL, 0);
    CHECK(strstr(textBuf, "not given by the network driver") != NULL, "no client block");
    CHECK(strstr(textBuf, "No other computer is connected.") != NULL, "no link");
}

/* Record 35 section 5.5: the "Ring interrupt:" line. */
static void test_irq(void)
{
    TR_QNET q;
    ULONG n;
    TR_TEXT t;
    int count;
    UCHAR *s;

    n = build(CM_QN_STATE_UP, 1, 1, 1);
    s = ans + CM_QN_HDR_BYTES;
    p32(s, CM_QN_S_IRQ_STATE, CM_QN_IRQ_OPEN);
    p32(s, CM_QN_S_IRQ_KICKS, 1234);
    (void)tr_qnet_parse(ans, n, &q);
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, -1, NULL, 0);
    CHECK(lines_ok(textBuf, &count), "within 79 columns");
    CHECK(strstr(textBuf, "  Ring interrupt:       on (1234 wake-ups)") != NULL,
          "open, with its kicks");
    p32(s, CM_QN_S_IRQ_STATE, CM_QN_IRQ_LATCHED);
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, -1, NULL, 1);
    CHECK(strstr(textBuf, "  Ring interrupt:       off (fell back to checking every "
                          "clock tick)") != NULL,
          "latched to polling, in the snapshot too");
    p32(s, CM_QN_S_IRQ_STATE, CM_QN_IRQ_POLLED);
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, -1, NULL, 0);
    CHECK(strstr(textBuf, "  Ring interrupt:       off (checked every clock tick)") != NULL,
          "polled");
    /* A driver before 48.38: 96 bytes, word 92 reserved 0. */
    p32(ans, CM_QN_O_SVC_BYTES, CM_QN_SVC_MIN_BYTES);
    p32(s, CM_QN_S_IRQ_KICKS, 0);
    CHECK_EQ(tr_qnet_parse(ans, n, &q), 1, "an older driver's answer");
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, -1, NULL, 0);
    CHECK(strstr(textBuf, "off (checked every clock tick)") != NULL,
          "an older driver: the poll");
    p32(s, CM_QN_S_IRQ_STATE, CM_QN_IRQ_OPEN);
    p32(s, CM_QN_S_IRQ_KICKS, 99);
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, -1, NULL, 0);
    CHECK(strstr(textBuf, "on (0 wake-ups)") != NULL,
          "open in a 96-byte block: the kicks past it never read");
}

/* Record 35 section 3.16: a firmware CM's record made with no
 * announcement - found, not connected, and why. */
static void test_unannounced(void)
{
    TR_QNET q;
    ULONG n;
    ULONG lo;
    TR_TEXT t;
    int count;
    char lab[160];

    n = build(CM_QN_STATE_WAITING, 1, 0, 1);
    lo = g32x(ans, CM_QN_O_LINK_OFFSET);
    p32(ans, lo + CM_QN_L_FLAGS, CM_QN_LF_UUID | CM_QN_LF_BLOCK | CM_QN_LF_USB4NET |
                                 CM_QN_LF_FIRMWARE | CM_QN_LF_UNANNOUNCED);
    p32(ans, lo + CM_QN_L_NET_STATE, 0);
    (void)tr_qnet_parse(ans, n, &q);
    tr_qnet_label(&q, lab, sizeof(lab));
    CHECK(strcmp(lab, "Thunderbolt networking - not connected") == 0, "label");
    tr_qnet_link_label(&q, 0, lab, sizeof(lab));
    CHECK(strcmp(lab, "Link on port 1 - found, not connected: "
                      "secret-host-name-0123456789abcde") == 0, "link label");
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, 0, NULL, 0);
    CHECK(lines_ok(textBuf, &count), "within 79 columns");
    CHECK(strstr(textBuf, "Not connected:") != NULL, "the reason");
    CHECK(strstr(textBuf, "Restart this computer with the cable in.") != NULL,
          "what to do");
    /* Without the flag: as before. */
    p32(ans, lo + CM_QN_L_FLAGS, CM_QN_LF_UUID | CM_QN_LF_BLOCK | CM_QN_LF_USB4NET |
                                 CM_QN_LF_FIRMWARE);
    (void)tr_qnet_parse(ans, n, &q);
    tr_qnet_label(&q, lab, sizeof(lab));
    CHECK(strcmp(lab, "Thunderbolt networking - connecting") == 0, "connecting");
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, 0, NULL, 0);
    CHECK(strstr(textBuf, "Not connected:") == NULL, "no reason line");
}

/* The whole texts (revision 23): Windows 11's device text is 35
 * characters, cut at 32 before ("Connection Mana", the owner's bench,
 * round 7); a driver before it (entries of 160 bytes, no flag) still
 * read; the flag in too short an entry refused. */
static void test_long_text(void)
{
    static const char longText[] = "Windows USB4(TM) Connection Manager";
    static const char longest[] =
        "0123456789012345678901234567890123456789012345678901234567890123";
    TR_QNET q;
    ULONG n;
    ULONG lo;
    TR_TEXT t;
    int count;
    char lab[200];
    char kind[40];
    char name[CM_QN_TEXT_L_BYTES * 2 + 4];

    n = build(CM_QN_STATE_UP, 1, 0, 1);
    lo = g32x(ans, CM_QN_O_LINK_OFFSET);
    CHECK(g32x(ans, lo + CM_QN_L_FLAGS) & CM_QN_LF_TEXT_L, "the builder sets the flag");
    memset(ans + lo + CM_QN_L_DEVICE_TEXT_L, 0, CM_QN_TEXT_L_BYTES);
    memcpy(ans + lo + CM_QN_L_DEVICE_TEXT_L, longText, sizeof(longText) - 1);
    CHECK_EQ(tr_qnet_parse(ans, n, &q), 1, "read");
    (void)tr_qnet_kind(q.Link[0], kind, sizeof(kind), name, sizeof(name));
    CHECK(strcmp(name, longText) == 0, "the whole name");
    tr_qnet_link_label(&q, 0, lab, sizeof(lab));
    CHECK(strstr(lab, "Connection Manager") != NULL, "the whole name in the label");
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, 0, NULL, 0);
    CHECK(strstr(textBuf, "\"Windows USB4(TM) Connection Manager\"") != NULL,
          "the whole device text");
    CHECK(lines_ok(textBuf, &count), "within 79 columns");
    /* 64 characters: the line wraps. */
    memcpy(ans + lo + CM_QN_L_DEVICE_TEXT_L, longest, 64);
    (void)tr_qnet_parse(ans, n, &q);
    tr_text_init(&t, textBuf, sizeof(textBuf));
    tr_qnet_text(&t, &q, 0, NULL, 0);
    CHECK(strstr(textBuf, longest) != NULL, "all 64 characters");
    CHECK(lines_ok(textBuf, &count), "still within 79 columns");
    /* A driver before revision 23: 160-byte entries, its 32 characters. */
    p32(ans, CM_QN_O_LINK_SHAPE, 1UL | (CM_QN_LINK_MIN_BYTES << 16));
    p32(ans, lo + CM_QN_L_FLAGS, g32x(ans, lo + CM_QN_L_FLAGS) & ~CM_QN_LF_TEXT_L);
    CHECK_EQ(tr_qnet_parse(ans, n, &q), 1, "an older driver's answer read");
    (void)tr_qnet_kind(q.Link[0], kind, sizeof(kind), name, sizeof(name));
    CHECK(strcmp(name, "secret-host-name-0123456789abcde") == 0, "its 32 characters");
    /* The flag in a 160-byte entry: refused. */
    p32(ans, lo + CM_QN_L_FLAGS, g32x(ans, lo + CM_QN_L_FLAGS) | CM_QN_LF_TEXT_L);
    CHECK_EQ(tr_qnet_parse(ans, n, &q), 0, "the flag without the room refused");
}

int main(void)
{
    test_parse();
    test_kind_label();
    test_match();
    test_u64();
    test_text();
    test_unannounced();
    test_irq();
    test_long_text();
    printf("test_trnet: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
