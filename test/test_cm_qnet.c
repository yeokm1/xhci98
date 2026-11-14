/*
 * test_cm_qnet.c - the query device's networking answer (design record 35
 * section 17, task 48.18): xhcicm.sys's builder and its request's answer
 * (src\cm\cm_qnet.c) - each state, zero to four links and a cut, the
 * peer's texts made printable and bounded, the MACs and counters at their
 * offsets, the client block absent, smaller and larger than the known
 * words, and the sizing rules (under the header, under the answer,
 * whole), the pass, its age and the stopping flag.
 */

#include <stdio.h>
#include <string.h>
#include "../src/cm/cm_qnetb.h"
#include "test_harness.h"

static ULONG g32(const UCHAR *b, ULONG o)
{
    return (ULONG)b[o] | ((ULONG)b[o + 1] << 8) | ((ULONG)b[o + 2] << 16) |
           ((ULONG)b[o + 3] << 24);
}

static UCHAR out[CM_QN_MAX_BYTES + 64];
static UCHAR ans[CM_QN_MAX_BYTES + 64];

static void base(CM_QN_IN *in)
{
    ULONG i;

    memset(in, 0, sizeof(*in));
    in->Flags = CM_QN_F_PDO | CM_QN_F_OPEN | CM_QN_F_UP | CM_QN_F_MAC;
    in->EpochLo = 0x11111111UL;
    in->EpochHi = 0x22222222UL;
    in->NetValue = 1;
    in->E2EValue = 1;
    in->Cm = 2;
    in->State = CM_QN_STATE_UP;
    in->Link = 1;
    for (i = 0; i < 6; i++) {
        in->Mac[i] = (UCHAR)(0x10 + i);
        in->PeerMac[i] = (UCHAR)(0x20 + i);
    }
    in->PeerStns = 7;
    in->OurStns = 3;
    in->E2E = 1;
    in->Mbps = 40000;
    in->Lanes = 2;
    in->MaxFrame = 1514;
    in->TxDesc = 256;
    in->RxDesc = 256;
    in->Generation = 9;
    in->RxFrames = 100;
    in->RxErrors = 1;
    in->RxDropped = 2;
    in->TxFrames = 90;
    in->TxRefused = 3;
    in->LinkUps = 4;
    in->LinkDowns = 5;
    in->RingsFailed = 6;
    in->IrqState = CM_QN_IRQ_OPEN;
    in->IrqKicks = 7;
    in->IrqKind = CM_QN_KIND_MESSAGE;
}

static void test_service(void)
{
    CM_QN_IN in;
    ULONG n;
    const UCHAR *s;

    base(&in);
    n = CmNetQueryBuild(out, CM_QN_MAX_BYTES - 1, &in);
    CHECK_EQ(n, 0UL, "a buffer under the most refused");
    n = CmNetQueryBuild(out, sizeof(out), &in);
    CHECK_EQ(n, CM_QN_HDR_BYTES + CM_QN_SVC_BYTES, "header and service, no link, no client");
    CHECK_EQ(g32(out, CM_QN_O_MAGIC), CM_QN_MAGIC, "magic");
    CHECK_EQ(g32(out, CM_QN_O_VERSION), CM_QN_VERSION, "version");
    CHECK_EQ(g32(out, CM_QN_O_BYTES), n, "bytes");
    CHECK_EQ(g32(out, CM_QN_O_FLAGS), CM_QN_F_PDO | CM_QN_F_OPEN | CM_QN_F_UP | CM_QN_F_MAC,
             "flags as given");
    CHECK_EQ(g32(out, CM_QN_O_PASS_AGE_MS), CM_QN_UNKNOWN, "no age before an answer");
    CHECK_EQ(g32(out, CM_QN_O_EPOCH_HI), 0x22222222UL, "epoch");
    CHECK_EQ(g32(out, CM_QN_O_SVC_OFFSET), CM_QN_HDR_BYTES, "service offset");
    CHECK_EQ(g32(out, CM_QN_O_LINK_SHAPE), CM_QN_LINK_BYTES << 16, "no link");
    CHECK_EQ(g32(out, CM_QN_O_CLIENT_OFFSET), 0UL, "no client");
    s = out + CM_QN_HDR_BYTES;
    CHECK_EQ(g32(s, CM_QN_S_STATE), CM_QN_STATE_UP, "state");
    CHECK_EQ(s[CM_QN_S_MAC + 5], 0x15, "MAC");
    CHECK_EQ(s[CM_QN_S_PEER_MAC], 0x20, "peer MAC");
    CHECK_EQ(s[CM_QN_S_MAC + 6] | s[CM_QN_S_MAC + 7], 0, "MAC padding 0");
    CHECK_EQ(g32(s, CM_QN_S_MBPS), 40000UL, "rate");
    CHECK_EQ(g32(s, CM_QN_S_RINGS_FAILED), 6UL, "rings failed");
    CHECK_EQ(g32(s, CM_QN_S_IRQ_STATE), CM_QN_IRQ_OPEN,
             "the data ring's interrupt (record 35 5.5), word 92");
    CHECK_EQ(g32(s, CM_QN_S_IRQ_KICKS), 7UL, "its kicks, word 96");
    CHECK_EQ(g32(s, CM_QN_S_IRQ_KIND), CM_QN_KIND_MESSAGE,
             "how it is delivered, word 100 (record 21 4.5.12)");
    CHECK_EQ(g32(out, CM_QN_O_SVC_BYTES), 104UL, "the block grown to 104");
    CHECK_EQ(CM_QN_SVC_MIN_BYTES, 96UL, "a driver before 48.38 wrote 96");
    /* The stopping flag is the answer's alone. */
    in.Flags |= CM_QN_F_STOPPING | CM_QN_F_CLIENT | CM_QN_F_LINKS_CUT;
    n = CmNetQueryBuild(out, sizeof(out), &in);
    CHECK_EQ(g32(out, CM_QN_O_FLAGS) & (CM_QN_F_STOPPING | CM_QN_F_CLIENT | CM_QN_F_LINKS_CUT),
             0UL, "the builder's own flags only");
}

static void test_links(void)
{
    CM_QN_IN in;
    CM_QN_LINK_IN l[6];
    ULONG n;
    ULONG i;
    const UCHAR *e;
    char longText[64];

    base(&in);
    memset(l, 0, sizeof(l));
    for (i = 0; i < 6; i++) {
        l[i].Link = i + 1;
        l[i].Flags = CM_QN_LF_UUID | CM_QN_LF_BLOCK | CM_QN_LF_USB4NET;
        l[i].Uuid[0] = 0xA0000000UL + i;
        l[i].Uuid[3] = 0x0000000BUL;
        l[i].VendorId = 0x1D6B;
        l[i].DeviceId = 4;
        l[i].VendorText = "Linux";
        l[i].DeviceText = "host";
        l[i].LoginsSent = 0x12345UL;    /* held at FFFFh */
        l[i].LoginFailures = 2;
        l[i].LoginsReceived = 3;
        l[i].LoginsRefused = 0x10000UL;
        l[i].Restarts = 7;
    }
    /* Bytes outside 0x20..0x7E, and a text longer than 32. */
    memset(longText, 'x', sizeof(longText));
    longText[0] = (char)0x01;
    longText[1] = (char)0xC3;
    longText[sizeof(longText) - 1] = '\0';
    l[1].DeviceText = longText;
    l[2].Flags = 0;     /* nothing read yet */

    in.Links = l;
    in.LinkCount = 2;
    n = CmNetQueryBuild(out, sizeof(out), &in);
    CHECK_EQ(n, CM_QN_HDR_BYTES + CM_QN_SVC_BYTES + 2 * CM_QN_LINK_BYTES, "two links");
    CHECK_EQ(g32(out, CM_QN_O_LINK_SHAPE), 2UL | (CM_QN_LINK_BYTES << 16), "shape");
    e = out + g32(out, CM_QN_O_LINK_OFFSET);
    CHECK_EQ(g32(e, CM_QN_L_LINK), 1UL, "first link");
    CHECK_EQ(g32(e, CM_QN_L_UUID), 0xA0000000UL, "UUID word 0");
    CHECK_EQ(g32(e, CM_QN_L_UUID + 12), 0x0BUL, "UUID word 3");
    CHECK(memcmp(e + CM_QN_L_VENDOR_TEXT, "Linux\0\0", 7) == 0, "vendor text, NUL-padded");
    CHECK_EQ(g32(e, CM_QN_L_LOGINS), 0xFFFFUL | (2UL << 16), "logins held, failures");
    CHECK_EQ(g32(e, CM_QN_L_PEER_LOGINS), 3UL | (0xFFFFUL << 16), "peer logins, refused held");
    CHECK_EQ(g32(e, CM_QN_L_RESTARTS), 7UL, "last counter");
    CHECK_EQ(e[114] | e[115], 0, "reserved 0");
    e += CM_QN_LINK_BYTES;
    CHECK_EQ(e[CM_QN_L_DEVICE_TEXT], '?', "a control byte written ?");
    CHECK_EQ(e[CM_QN_L_DEVICE_TEXT + 1], '?', "a high byte written ?");
    CHECK_EQ(e[CM_QN_L_DEVICE_TEXT + 31], 'x', "32 characters kept");
    CHECK_EQ(e[CM_QN_L_DEVICE_TEXT + 32], 0, "then a NUL");

    in.LinkCount = 3;
    n = CmNetQueryBuild(out, sizeof(out), &in);
    e = out + g32(out, CM_QN_O_LINK_OFFSET) + 2 * CM_QN_LINK_BYTES;
    CHECK_EQ(g32(e, CM_QN_L_VENDOR_ID), 0UL, "no block: no ids");
    CHECK_EQ(g32(e, CM_QN_L_UUID), 0UL, "no UUID: zeros");
    CHECK_EQ(e[CM_QN_L_VENDOR_TEXT], 0, "no block: no text");

    in.LinkCount = 6;
    n = CmNetQueryBuild(out, sizeof(out), &in);
    CHECK_EQ(g32(out, CM_QN_O_LINK_SHAPE) & 0xFFFFUL, CM_QN_MAX_LINKS, "four at most");
    CHECK(g32(out, CM_QN_O_FLAGS) & CM_QN_F_LINKS_CUT, "the cut said");
    CHECK_EQ(n, CM_QN_HDR_BYTES + CM_QN_SVC_BYTES + CM_QN_MAX_LINKS * CM_QN_LINK_BYTES,
             "four links' bytes");
}

static void test_client(void)
{
    CM_QN_IN in;
    ULONG w[40];
    ULONG n;
    ULONG i;
    const UCHAR *c;

    base(&in);
    for (i = 0; i < 40; i++) {
        w[i] = 0x100UL + i;
    }
    in.ClientWords = w;
    in.ClientWordCount = CM_QN_C_KNOWN_BYTES / 4;
    n = CmNetQueryBuild(out, sizeof(out), &in);
    CHECK(g32(out, CM_QN_O_FLAGS) & CM_QN_F_CLIENT, "client flag");
    c = out + g32(out, CM_QN_O_CLIENT_OFFSET);
    CHECK_EQ(g32(out, CM_QN_O_CLIENT_OFFSET), CM_QN_HDR_BYTES + CM_QN_SVC_BYTES, "after the service");
    CHECK_EQ(g32(c, CM_QN_C_BYTES), CM_QN_C_KNOWN_BYTES, "its bytes, not the caller's word 0");
    CHECK_EQ(g32(c, CM_QN_C_TX_FULL_WAITS), 0x110UL, "word 16");
    CHECK_EQ(n, CM_QN_HDR_BYTES + CM_QN_SVC_BYTES + CM_QN_C_KNOWN_BYTES, "answer bytes");

    in.ClientWordCount = 40;
    n = CmNetQueryBuild(out, sizeof(out), &in);
    c = out + g32(out, CM_QN_O_CLIENT_OFFSET);
    CHECK_EQ(g32(c, CM_QN_C_BYTES), CM_QN_CLIENT_MAX, "held to the most");
    CHECK_EQ(n, (ULONG)CM_QN_MAX_BYTES - CM_QN_MAX_LINKS * CM_QN_LINK_BYTES, "no link, full client");

    in.ClientWordCount = 1;
    n = CmNetQueryBuild(out, sizeof(out), &in);
    c = out + g32(out, CM_QN_O_CLIENT_OFFSET);
    CHECK_EQ(g32(c, CM_QN_C_BYTES), 4UL, "a Size word alone");
}

static void test_answer(void)
{
    CM_QN_IN in;
    ULONG n;
    ULONG info;
    ULONG rc;

    base(&in);
    n = CmNetQueryBuild(out, sizeof(out), &in);
    rc = CmNetQueryAnswer(ans, CM_QN_HDR_BYTES - 1, out, n, 5, FALSE, &info);
    CHECK_EQ(rc, 2UL, "under the header: too small");
    CHECK_EQ(info, 0UL, "nothing written");
    rc = CmNetQueryAnswer(ans, CM_QN_HDR_BYTES, out, n, 5, FALSE, &info);
    CHECK_EQ(rc, 1UL, "the header alone");
    CHECK_EQ(info, CM_QN_HDR_BYTES, "64 bytes");
    CHECK_EQ(g32(ans, CM_QN_O_BYTES), n, "the size to ask for");
    CHECK_EQ(g32(ans, CM_QN_O_PASS), 5UL, "the pass");
    rc = CmNetQueryAnswer(ans, sizeof(ans), out, n, 6, TRUE, &info);
    CHECK_EQ(rc, 0UL, "whole");
    CHECK_EQ(info, n, "all of it");
    CHECK(g32(ans, CM_QN_O_FLAGS) & CM_QN_F_STOPPING, "stopping set");
    CHECK(!(g32(out, CM_QN_O_FLAGS) & CM_QN_F_STOPPING), "the built answer untouched");
    CmNetQuerySetAge(ans, info, 6, 123);
    CHECK_EQ(g32(ans, CM_QN_O_PASS_AGE_MS), 123UL, "age");
    CmNetQuerySetAge(ans, info, 0, 123);
    CHECK_EQ(g32(ans, CM_QN_O_PASS_AGE_MS), CM_QN_UNKNOWN, "no pass: unknown");
    rc = CmNetQueryAnswer(ans, sizeof(ans), out, CM_QN_HDR_BYTES - 1, 1, FALSE, &info);
    CHECK_EQ(rc, 2UL, "a built answer under the header refused");
}

int main(void)
{
    test_service();
    test_links();
    test_client();
    test_answer();
    printf("test_cm_qnet: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
