/*
 * test_trpci.c - the PCI configuration answers and their decoding (design
 * record 25 sections 3.6 and 4.7, revision 12): xhcicm.sys's builder
 * (src\cm\cm_qpci.c) over modelled buses - a dock's chain, a loop, a bus
 * outside the port's range, a chain deeper than the bound, more functions
 * than the answer holds, a refused read, an absent function, Windows 98's
 * "not read" - and its sizing answers; XHCITR's reader of that answer
 * (xhcitr\tr_pci.c) with bad shapes; the decoders (pin and line, MSI,
 * MSI-X, the PCIe link, BARs, bridge buses and windows); the capability
 * walk with a clear Status bit, a loop, a bad pointer, an unaligned
 * pointer, a cut and a capability past the 256 bytes; and that no line of
 * a worst-case text passes 79 columns.
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "../src/cm/cm_qryb.h"
#include "../xhcitr/tr_pci.h"
#include "../xhcitr/tr_log.h"
#include "../src/xhci.h"
#include "test_harness.h"

static void put16(unsigned char *b, unsigned int o, unsigned long v)
{
    b[o] = (unsigned char)(v & 0xFFUL);
    b[o + 1] = (unsigned char)((v >> 8) & 0xFFUL);
}

static void put32(unsigned char *b, unsigned int o, unsigned long v)
{
    put16(b, o, v & 0xFFFFUL);
    put16(b, o + 2, (v >> 16) & 0xFFFFUL);
}

static unsigned long get32(const unsigned char *b, unsigned int o)
{
    return (unsigned long)b[o] | ((unsigned long)b[o + 1] << 8) |
           ((unsigned long)b[o + 2] << 16) | ((unsigned long)b[o + 3] << 24);
}

#define BDF(b, d, f) ((((unsigned long)(b)) << 8) | ((d) << 3) | (f))

/* ---- a modelled machine ---------------------------------------------- */

#define FN_MAX 96

static struct {
    unsigned long Bdf;
    unsigned char Cfg[256];
    int Fail;           /* the read is refused */
} fn[FN_MAX];
static int fnCount;
static int failBus = -1;    /* any read on this bus is refused */
static unsigned long readsMade;

static unsigned char *add_fn(unsigned long bdf, unsigned long id,
                             unsigned long cls)
{
    unsigned char *c = fn[fnCount].Cfg;

    memset(c, 0, 256);
    fn[fnCount].Bdf = bdf;
    fn[fnCount].Fail = 0;
    put32(c, 0, id);
    put32(c, 8, cls << 8);
    fnCount++;
    return c;
}

static unsigned char *add_bridge(unsigned long bdf, unsigned long id,
                                 unsigned int pri, unsigned int sec,
                                 unsigned int sub)
{
    unsigned char *c = add_fn(bdf, id, 0x060400UL);

    c[0x0E] = 1;
    c[0x18] = (unsigned char)pri;
    c[0x19] = (unsigned char)sec;
    c[0x1A] = (unsigned char)sub;
    return c;
}

static ULONG model_read(PVOID ctx, ULONG bdf, UCHAR *buf, ULONG bytes)
{
    int i;

    (void)ctx;
    readsMade++;
    if (failBus >= 0 && (int)((bdf >> 8) & 0xFF) == failBus) {
        return CM_QP_RD_FAIL;
    }
    for (i = 0; i < fnCount; i++) {
        if (fn[i].Bdf == bdf) {
            if (fn[i].Fail) {
                return CM_QP_RD_FAIL;
            }
            memcpy(buf, fn[i].Cfg, bytes);
            return CM_QP_RD_OK;
        }
    }
    return CM_QP_RD_ABSENT;
}

static void reset(void)
{
    fnCount = 0;
    failBus = -1;
    readsMade = 0;
}

static UCHAR built[CM_QP_MAX_BYTES];
static unsigned char outb[CM_QP_MAX_BYTES + 64];
static char text[65536];

static void in_one_port(CM_QPCI_IN *in, unsigned long port)
{
    memset(in, 0, sizeof(*in));
    in->EpochLo = 0x11111111UL;
    in->EpochHi = 0x02222222UL;
    in->NhiBdf = BDF(5, 0, 0);
    in->PortCount = 1;
    in->Ports[0].Bdf = port;
    in->Ports[0].Source = 1;
    in->Ports[0].Flags = 3;
    in->ReadBelow = 1;
    in->Read = model_read;
}

static unsigned long ent(const UCHAR *b, unsigned long i, unsigned int o)
{
    return get32(b, CM_QP_HDR_BYTES + i * CM_QP_ENT_BYTES + o);
}

/* The P14s's shape (record 21 section 6.3) with an ORICO-like chain below
 * the tunnel port: 04:01.0 (06-08), 06:00.0 USP (07-08), 07:00.0 DSP
 * (08-08), 08:00.0 NVMe. */
static void p14s(void)
{
    reset();
    add_fn(BDF(5, 0, 0), 0x15BF8086UL, 0x088000UL);
    add_bridge(BDF(4, 1, 0), 0x15C08086UL, 4, 6, 8);
    add_bridge(BDF(6, 0, 0), 0x24631B21UL, 6, 7, 8);
    add_bridge(BDF(7, 0, 0), 0x24631B21UL, 7, 8, 8);
    add_fn(BDF(8, 0, 0), 0x174A1C5CUL, 0x010802UL);
}

static void test_layout(void)
{
    CHECK_EQ(CM_QP_IOCTL, (0x22UL << 16) | (0xA42UL << 2), "the control code");
    CHECK_EQ(CM_QP_MAGIC, 'X' | ('C' << 8) | ('M' << 16) | ((ULONG)'P' << 24),
             "'XCMP' in byte order");
    CHECK_EQ(CM_QP_ENT_BYTES, 288, "an entry");
    CHECK_EQ(CM_QP_MAX_ENTRIES, 37, "the most entries");
    CHECK(CM_QP_MAX_BYTES <= CM_QP_ASK_BYTES, "one ask holds the largest");
    CHECK_EQ(CM_QP_O_FAILS + 4 + 16, CM_QP_HDR_BYTES, "16 bytes reserved");
}

static void test_chain(void)
{
    CM_QPCI_IN in;
    ULONG n;

    p14s();
    in_one_port(&in, BDF(4, 1, 0));
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(n, CM_QP_HDR_BYTES + 5 * CM_QP_ENT_BYTES, "five entries");
    CHECK_EQ(get32(built, CM_QP_O_MAGIC), CM_QP_MAGIC, "magic");
    CHECK_EQ(get32(built, CM_QP_O_BYTES), n, "size");
    CHECK_EQ(get32(built, CM_QP_O_FLAGS), 0, "no flag");
    CHECK_EQ(get32(built, CM_QP_O_ENT_SHAPE), 5 | (288UL << 16), "shape");
    CHECK_EQ(get32(built, CM_QP_O_EPOCH_LO), 0x11111111UL, "epoch");
    CHECK_EQ(get32(built, CM_QP_O_PORTS), 1, "ports");
    CHECK_EQ(ent(built, 0, CM_QP_E_ROLE), CM_QP_R_NHI, "NHI first");
    CHECK_EQ(ent(built, 0, CM_QP_E_STATE), CM_QP_S_READ, "NHI read");
    CHECK_EQ(ent(built, 0, CM_QP_EHDR_BYTES), 0x15BF8086UL, "NHI bytes");
    CHECK_EQ(ent(built, 1, CM_QP_E_ROLE), CM_QP_R_PORT, "port");
    CHECK_EQ(ent(built, 1, CM_QP_E_INFO), 0x0301UL, "source and flags");
    CHECK_EQ(ent(built, 2, CM_QP_E_BDF), BDF(6, 0, 0), "USP");
    CHECK_EQ(ent(built, 2, CM_QP_E_PARENT), BDF(4, 1, 0), "under the port");
    CHECK_EQ(ent(built, 2, CM_QP_E_DEPTH), 0, "depth 0");
    CHECK_EQ(ent(built, 3, CM_QP_E_BDF), BDF(7, 0, 0), "DSP");
    CHECK_EQ(ent(built, 3, CM_QP_E_PARENT), BDF(6, 0, 0), "under the USP");
    CHECK_EQ(ent(built, 3, CM_QP_E_DEPTH), 1, "depth 1");
    CHECK_EQ(ent(built, 4, CM_QP_E_BDF), BDF(8, 0, 0), "NVMe");
    CHECK_EQ(ent(built, 4, CM_QP_E_PORT), BDF(4, 1, 0), "its port");
    CHECK_EQ(ent(built, 4, CM_QP_E_DEPTH), 2, "depth 2");
    /* 3 functions + 3 buses x 32 probes + 3 full reads of the found */
    CHECK_EQ(get32(built, CM_QP_O_READS), 2 + 96 + 3, "reads counted");
    CHECK_EQ(get32(built, CM_QP_O_FAILS), 0, "none refused");
}

static void test_bounds(void)
{
    CM_QPCI_IN in;
    ULONG n;
    unsigned int d;
    unsigned int b;
    unsigned char *c;

    /* A loop: the chain's last bridge names a bus already walked. */
    p14s();
    fn[3].Cfg[0x19] = 7;
    fn[3].Cfg[0x1A] = 8;
    in_one_port(&in, BDF(4, 1, 0));
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(n, CM_QP_HDR_BYTES + 4 * CM_QP_ENT_BYTES, "a loop is not followed");
    CHECK_EQ(get32(built, CM_QP_O_FLAGS), 0, "and is no bound");

    /* A bridge naming a bus outside the port's range. */
    p14s();
    fn[2].Cfg[0x19] = 0x20;
    fn[2].Cfg[0x1A] = 0x20;
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(n, CM_QP_HDR_BYTES + 3 * CM_QP_ENT_BYTES, "outside the range");

    /* A port with no bus numbers. */
    p14s();
    fn[1].Cfg[0x19] = 0;
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(n, CM_QP_HDR_BYTES + 2 * CM_QP_ENT_BYTES, "no buses");

    /* A chain deeper than the bound: bridges on 10..30, one each. */
    reset();
    add_fn(BDF(5, 0, 0), 0x15BF8086UL, 0x088000UL);
    add_bridge(BDF(4, 1, 0), 0x15C08086UL, 4, 10, 40);
    for (b = 10; b < 30; b++) {
        add_bridge(BDF(b, 0, 0), 0x24631B21UL, b, b + 1, 40);
    }
    in_one_port(&in, BDF(4, 1, 0));
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(n, CM_QP_HDR_BYTES + (2 + CM_QP_MAX_DEPTH) * CM_QP_ENT_BYTES,
             "cut at the depth");
    CHECK(get32(built, CM_QP_O_FLAGS) & CM_QP_F_TRUNCATED, "truncated");

    /* More functions than the answer holds: 8 functions on each of 6
     * devices of one bus. */
    reset();
    add_fn(BDF(5, 0, 0), 0x15BF8086UL, 0x088000UL);
    add_bridge(BDF(4, 1, 0), 0x15C08086UL, 4, 6, 6);
    for (d = 0; d < 6; d++) {
        for (b = 0; b < 8; b++) {
            c = add_fn(BDF(6, d, b), 0x00011234UL, 0x020000UL);
            c[0x0E] = 0x80;
        }
    }
    in_one_port(&in, BDF(4, 1, 0));
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(n, CM_QP_HDR_BYTES + (2 + CM_QP_MAX_BELOW) * CM_QP_ENT_BYTES,
             "cut at the functions");
    CHECK(n <= CM_QP_MAX_BYTES, "within the most");
    CHECK(get32(built, CM_QP_O_FLAGS) & CM_QP_F_TRUNCATED, "truncated 2");

    /* Too many buses: a wide switch, 20 downstream ports on bus 6. */
    reset();
    add_fn(BDF(5, 0, 0), 0x15BF8086UL, 0x088000UL);
    add_bridge(BDF(4, 1, 0), 0x15C08086UL, 4, 6, 60);
    for (d = 0; d < 20; d++) {
        add_bridge(BDF(6, d, 0), 0x24631B21UL, 6, 10 + d, 10 + d);
    }
    in_one_port(&in, BDF(4, 1, 0));
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK(get32(built, CM_QP_O_FLAGS) & CM_QP_F_TRUNCATED, "bus bound");
    CHECK(readsMade <= 200 + 16 * 32, "bounded reads");

    /* More ports than the answer has room for. */
    p14s();
    in_one_port(&in, BDF(4, 1, 0));
    in.PortCount = 9;
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(get32(built, CM_QP_O_PORTS), CM_QP_MAX_PORTS, "ports cut");
    CHECK(get32(built, CM_QP_O_FLAGS) & CM_QP_F_TRUNCATED, "said so");

    /* A buffer under the most. */
    CHECK_EQ(CmQpciBuild(built, CM_QP_MAX_BYTES - 1, &in), 0, "room refused");
}

static void test_states(void)
{
    CM_QPCI_IN in;
    ULONG n;

    /* Windows 98 SE and ME: nothing below is read. */
    p14s();
    in_one_port(&in, BDF(4, 1, 0));
    in.ReadBelow = 0;
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(n, CM_QP_HDR_BYTES + 3 * CM_QP_ENT_BYTES, "one not-read entry");
    CHECK_EQ(ent(built, 2, CM_QP_E_STATE), CM_QP_S_NOT_READ_OS, "not read");
    CHECK_EQ(ent(built, 2, CM_QP_E_BDF), CM_Q_UNKNOWN, "no location");
    CHECK_EQ(ent(built, 2, CM_QP_E_PORT), BDF(4, 1, 0), "its port");
    CHECK(get32(built, CM_QP_O_FLAGS) & CM_QP_F_BELOW_OS, "flag");
    CHECK_EQ(readsMade, 2, "the NHI and the port only");

    /* A refused read and an absent NHI. */
    p14s();
    fn[0].Fail = 1;
    failBus = 7;
    in_one_port(&in, BDF(4, 1, 0));
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(ent(built, 0, CM_QP_E_STATE), CM_QP_S_FAILED, "refused");
    CHECK_EQ(ent(built, 0, CM_QP_EHDR_BYTES), 0, "zeroed");
    CHECK_EQ(n, CM_QP_HDR_BYTES + 3 * CM_QP_ENT_BYTES, "bus 7 not walked");
    CHECK_EQ(get32(built, CM_QP_O_FAILS), 1 + 32, "refusals counted");
    p14s();
    in.NhiBdf = BDF(9, 0, 0);
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(ent(built, 0, CM_QP_E_STATE), CM_QP_S_ABSENT, "absent");
    in.NhiBdf = CM_Q_UNKNOWN;
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(ent(built, 0, CM_QP_E_STATE), CM_QP_S_FAILED, "no location");
    /* No set. */
    in.PortCount = 0;
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(n, CM_QP_HDR_BYTES + CM_QP_ENT_BYTES, "the NHI alone");
    CHECK(get32(built, CM_QP_O_FLAGS) & CM_QP_F_NO_SET, "no set");
    /* Stopping. */
    in.Flags = CM_QP_F_STOPPING | 0x80;
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(get32(built, CM_QP_O_FLAGS) & 0x81, 1, "stopping only");
}

static void test_answer(void)
{
    CM_QPCI_IN in;
    ULONG n;
    ULONG info;
    TR_QPCI q;

    p14s();
    in_one_port(&in, BDF(4, 1, 0));
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(CmQpciAnswer(outb, 63, built, n, &info), CM_Q_ANS_TOO_SMALL,
             "too small");
    CHECK_EQ(info, 0, "nothing");
    CHECK_EQ(CmQpciAnswer(outb, 64, built, n, &info), CM_Q_ANS_HEADER,
             "header");
    CHECK_EQ(info, 64, "64");
    CHECK_EQ(tr_qpci_parse(outb, info, &q), -1, "the tool asks again");
    CHECK_EQ(q.Bytes, n, "with the size");
    CHECK_EQ(CmQpciAnswer(outb, sizeof(outb), built, n, &info),
             CM_Q_ANS_WHOLE, "whole");
    CHECK_EQ(info, n, "all of it");
    CHECK_EQ(tr_qpci_parse(outb, info, &q), 1, "parsed");
    CHECK_EQ(q.Count, 5, "count");
    CHECK_EQ(q.NhiBdf, BDF(5, 0, 0), "NHI");

    /* Bad shapes. */
    CHECK_EQ(tr_qpci_parse(outb, info - 1, &q), -1, "short: ask again");
    put32(outb, CM_QP_O_ENT_SHAPE, 6 | (288UL << 16));
    CHECK_EQ(tr_qpci_parse(outb, info, &q), 0, "count past the bytes");
    put32(outb, CM_QP_O_ENT_SHAPE, 5 | (256UL << 16));
    CHECK_EQ(tr_qpci_parse(outb, info, &q), 0, "entry size");
    put32(outb, CM_QP_O_ENT_SHAPE, 5 | (288UL << 16));
    put32(outb, CM_QP_O_ENT_OFFSET, 32);
    CHECK_EQ(tr_qpci_parse(outb, info, &q), 0, "offset in the header");
    put32(outb, CM_QP_O_ENT_OFFSET, 64);
    put32(outb, CM_QP_O_VERSION, 2);
    CHECK_EQ(tr_qpci_parse(outb, info, &q), 0, "version");
    put32(outb, CM_QP_O_VERSION, 1);
    put32(outb, CM_QP_O_BYTES, CM_QP_MAX_BYTES + 4);
    CHECK_EQ(tr_qpci_parse(outb, info, &q), 0, "size past the most");
    put32(outb, CM_QP_O_BYTES, info);
    put32(outb, CM_QP_O_MAGIC, 0);
    CHECK_EQ(tr_qpci_parse(outb, info, &q), 0, "magic");
    CHECK_EQ(tr_qpci_parse(NULL, 0, &q), 0, "nothing");
}

/* ---- decoding --------------------------------------------------------- */

static void test_short(void)
{
    char s[96];
    unsigned char c[256];

    tr_pci_pin(1, 10, s, sizeof(s));
    CHECK(strcmp(s, "pin INTA, line 10") == 0, "pin A line 10");
    tr_pci_pin(4, 0xFF, s, sizeof(s));
    CHECK(strcmp(s, "pin INTD, line not assigned (FFh)") == 0, "pin D");
    tr_pci_pin(0, 0, s, sizeof(s));
    CHECK(strcmp(s, "no pin (pin 0), line 0") == 0, "no pin");
    tr_pci_pin(9, 3, s, sizeof(s));
    CHECK(strcmp(s, "pin invalid (9), line 3") == 0, "bad pin");

    memset(c, 0, sizeof(c));
    c[0x80] = 0x05;
    put16(c, 0x82, 0x0000);
    tr_pci_msi(c, 0x80, s, sizeof(s));
    CHECK(strcmp(s, "MSI: present, disabled (1 vector)") == 0, "MSI off");
    put16(c, 0x82, 0x00A7);
    tr_pci_msi(c, 0x80, s, sizeof(s));
    CHECK(strcmp(s, "MSI: present, enabled (4 of 8 vectors), 64-bit") == 0,
          "MSI on");
    put16(c, 0x82, 0x007E);
    tr_pci_msi(c, 0x80, s, sizeof(s));
    CHECK(strcmp(s, "MSI: present, disabled (32 vectors)") == 0,
          "reserved counts held to 32");
    tr_pci_msi(c, 0xF8, s, sizeof(s));
    CHECK(strstr(s, "past the 256 bytes") != NULL, "MSI past the end");

    put16(c, 0x92, 0x8007);
    tr_pci_msix(c, 0x90, s, sizeof(s));
    CHECK(strcmp(s, "MSI-X: present, enabled (table size 8)") == 0, "MSI-X");
    put16(c, 0x92, 0x47FF);
    tr_pci_msix(c, 0x90, s, sizeof(s));
    CHECK(strcmp(s, "MSI-X: present, disabled (table size 2048), all masked")
              == 0, "MSI-X masked");

    put32(c, 0xA0 + 0x0C, 0x00000043UL);
    put16(c, 0xA0 + 0x12, 0x2043);
    tr_pci_link(c, 0xA0, s, sizeof(s));
    CHECK(strcmp(s, "Gen 3 x4 (capable Gen 3 x4)") == 0, "link");
    put16(c, 0xA0 + 0x12, 0x0011);
    tr_pci_link(c, 0xA0, s, sizeof(s));
    CHECK(strcmp(s, "Gen 1 x1 (capable Gen 3 x4)") == 0, "trained lower");
    put16(c, 0xA0 + 0x12, 0x0001);
    tr_pci_link(c, 0xA0, s, sizeof(s));
    CHECK(strcmp(s, "down (capable Gen 3 x4)") == 0, "link down");
    tr_pci_link(c, 0xF0, s, sizeof(s));
    CHECK(strstr(s, "past the 256 bytes") != NULL, "link past the end");
    CHECK(strcmp(tr_pci_port_type(6), "switch downstream port") == 0, "type");
    CHECK(strcmp(tr_pci_class_text(0x0C0340UL), "USB4 host interface") == 0,
          "class");
}

static void test_walk(void)
{
    unsigned char c[256];
    TR_PCICAP l[TR_PCI_CAP_MAX];
    int how;
    unsigned int bad;
    int n;

    memset(c, 0, sizeof(c));
    c[0x34] = 0x80;
    c[0x80] = 0x01;
    c[0x81] = 0x90;
    c[0x90] = 0x05;
    c[0x91] = 0xA0;
    c[0xA0] = 0x10;
    c[0xA1] = 0x00;
    n = tr_pci_caps(c, l, TR_PCI_CAP_MAX, &how, &bad);
    CHECK_EQ(n, 0, "Status bit 4 clear: no walk");
    CHECK_EQ(how, TR_PCI_WALK_NONE, "said so");
    put16(c, 0x06, 0x0010);
    n = tr_pci_caps(c, l, TR_PCI_CAP_MAX, &how, &bad);
    CHECK_EQ(n, 3, "three");
    CHECK_EQ(how, 0, "ended");
    CHECK_EQ(l[2].Id, 0x10, "PCIe last");
    CHECK_EQ(l[2].At, 0xA0, "at A0");
    /* Unaligned pointers are masked. */
    c[0x34] = 0x83;
    c[0x81] = 0x92;
    n = tr_pci_caps(c, l, TR_PCI_CAP_MAX, &how, &bad);
    CHECK_EQ(n, 3, "masked");
    /* A loop. */
    c[0xA1] = 0x90;
    n = tr_pci_caps(c, l, TR_PCI_CAP_MAX, &how, &bad);
    CHECK_EQ(n, 3, "a loop stops");
    CHECK_EQ(how, TR_PCI_WALK_LOOP, "loop");
    CHECK_EQ(bad, 0x90, "where");
    /* A self loop. */
    c[0x81] = 0x80;
    n = tr_pci_caps(c, l, TR_PCI_CAP_MAX, &how, &bad);
    CHECK_EQ(n, 1, "self loop");
    CHECK_EQ(how, TR_PCI_WALK_LOOP, "self loop said");
    /* A pointer into the header. */
    c[0x81] = 0x20;
    n = tr_pci_caps(c, l, TR_PCI_CAP_MAX, &how, &bad);
    CHECK_EQ(n, 1, "bad pointer");
    CHECK_EQ(how, TR_PCI_WALK_BAD_PTR, "bad pointer said");
    CHECK_EQ(bad, 0x20, "which");
    c[0x34] = 0x3C;
    n = tr_pci_caps(c, l, TR_PCI_CAP_MAX, &how, &bad);
    CHECK_EQ(n, 0, "a first pointer in the header");
    CHECK_EQ(how, TR_PCI_WALK_BAD_PTR, "said");
    /* The last DWORD's capability: its next pointer is at FDh. */
    c[0x34] = 0xFF;
    c[0xFC] = 0x09;
    c[0xFD] = 0x00;
    n = tr_pci_caps(c, l, TR_PCI_CAP_MAX, &how, &bad);
    CHECK_EQ(n, 1, "at FCh");
    CHECK_EQ(how, 0, "ended at FCh");
    /* Every DWORD from 40h: 48 entries, then a cut at a smaller bound. */
    {
        unsigned int a;
        for (a = 0x40; a <= 0xFC; a += 4) {
            c[a] = 0x09;
            c[a + 1] = (unsigned char)(a == 0xFC ? 0 : a + 4);
        }
        c[0x34] = 0x40;
        n = tr_pci_caps(c, l, TR_PCI_CAP_MAX, &how, &bad);
        CHECK_EQ(n, 48, "the whole space");
        CHECK_EQ(how, 0, "no cut at 48");
        n = tr_pci_caps(c, l, 5, &how, &bad);
        CHECK_EQ(n, 5, "cut");
        CHECK_EQ(how, TR_PCI_WALK_CUT, "cut said");
    }
}

/* The longest line of text, in columns. */
static size_t widest(const char *s)
{
    size_t w = 0;
    size_t col = 0;

    for (; *s; s++) {
        if (*s == '\r') {
            continue;
        }
        if (*s == '\n') {
            col = 0;
            continue;
        }
        col++;
        if (col > w) {
            w = col;
        }
    }
    return w;
}

/* A downstream port's bytes, every field the text names. */
static void dsp_bytes(unsigned char *c)
{
    memset(c, 0, 256);
    put32(c, 0x00, 0x15C08086UL);
    put16(c, 0x04, 0x0407);
    put16(c, 0x06, 0xF918);
    c[0x08] = 0x02;
    put32(c, 0x08, 0x06040002UL);
    c[0x0E] = 0x81;
    put32(c, 0x10, 0xF7E00008UL);
    put32(c, 0x14, 0x00000001UL);
    c[0x18] = 0x06;
    c[0x19] = 0x07;
    c[0x1A] = 0x08;
    c[0x1C] = 0x21;
    c[0x1D] = 0x31;
    put16(c, 0x20, 0xE000);
    put16(c, 0x22, 0xE000);
    put16(c, 0x24, 0xFFF1);
    put16(c, 0x26, 0x0001);
    put16(c, 0x30, 0x0001);
    put16(c, 0x32, 0x0001);
    c[0x34] = 0x80;
    c[0x3C] = 10;
    c[0x3D] = 1;
    c[0x80] = 0x01;
    c[0x81] = 0x88;
    put16(c, 0x82, 0xC803);
    put16(c, 0x84, 0x0100);
    c[0x88] = 0x05;
    c[0x89] = 0xC0;
    put16(c, 0x8A, 0x0080);
    c[0xC0] = 0x10;
    c[0xC1] = 0xA0;
    put16(c, 0xC2, 0x0162);
    put32(c, 0xC0 + 0x0C, 0x00000043UL);
    put16(c, 0xC0 + 0x12, 0x2043);
    c[0xA0] = 0x11;
    c[0xA1] = 0xE0;
    put16(c, 0xA2, 0x0007);
    put32(c, 0xA4, 0x00002000UL);
    put32(c, 0xA8, 0x00003000UL);
    c[0xE0] = 0x0D;
    c[0xE1] = 0xF8;
    put32(c, 0xE4, 0x505217AAUL);
    c[0xF8] = 0x10;
    c[0xF9] = 0x00;
}

static void test_text(void)
{
    TR_TEXT t;
    unsigned char c[256];

    dsp_bytes(c);
    tr_text_init(&t, text, sizeof(text));
    tr_pci_text(&t, c, 2);
    CHECK(!t.Overflow, "fits");
    CHECK(strstr(text, "Vendor:device       8086:15C0, revision 02") != NULL,
          "ids");
    CHECK(strstr(text, "Class               060400 (PCI-to-PCI bridge)")
              != NULL, "class");
    CHECK(strstr(text, "Command             0407 (I/O, memory, bus master, "
                       "INTx disabled)") != NULL, "command");
    CHECK(strstr(text, "Status              F918 (capability list, "
                       "INTx pending,") != NULL, "status");
    CHECK(strstr(text, "Interrupt           pin INTA, line 10") != NULL,
          "pin and line");
    CHECK(strstr(text, "BAR0                F7E00008 (memory, 32-bit, "
                       "prefetchable)") != NULL, "BAR0");
    CHECK(strstr(text, "BAR1                00000001 (I/O at 0)") != NULL,
          "BAR1");
    CHECK(strstr(text, "Buses               primary 06, Bus 07-08") != NULL,
          "buses");
    CHECK(strstr(text, "Memory window       Window E0000000-E00FFFFF") != NULL,
          "memory window");
    CHECK(strstr(text, "I/O window          Window 00012000-00013FFF") != NULL,
          "I/O window, 32-bit");
    CHECK(strstr(text, "Prefetchable window none") != NULL, "pf none");
    CHECK(strstr(text, "01h at 80h  Power management v3, state D0, PME "
                       "capable, PME enabled") != NULL, "PM");
    CHECK(strstr(text, "05h at 88h  MSI: present, disabled (1 vector), "
                       "64-bit") != NULL, "MSI");
    CHECK(strstr(text, "10h at C0h  PCI Express v2, switch downstream port, "
                       "slot") != NULL, "PCIe");
    CHECK(strstr(text, "PCIe link: Gen 3 x4 (capable Gen 3 x4), link active")
              != NULL, "link");
    CHECK(strstr(text, "11h at A0h  MSI-X: present, disabled (table size 8)")
              != NULL, "MSI-X");
    CHECK(strstr(text, "table BAR0+2000h, pending bits BAR0+3000h") != NULL,
          "MSI-X table");
    CHECK(strstr(text, "0Dh at E0h  Subsystem 17AA:5052") != NULL, "SSVID");
    CHECK(strstr(text, "10h at F8h  PCI Express, past the 256 bytes read")
              != NULL, "past the end");
    CHECK(strstr(text, "MSI: not present") == NULL, "MSI found");
    CHECK(widest(text) <= 79, "79 columns");

    /* A 64-bit prefetchable window and BAR. */
    put16(c, 0x24, 0x0001);
    put16(c, 0x26, 0xFFF1);
    put32(c, 0x28, 0x00000004UL);
    put32(c, 0x2C, 0x00000004UL);
    put32(c, 0x10, 0x0000000CUL);
    put32(c, 0x14, 0x00000060UL);
    tr_text_init(&t, text, sizeof(text));
    tr_pci_text(&t, c, 2);
    CHECK(strstr(text, "Prefetchable window Window 0000000400000000-"
                       "00000004FFFFFFFF") != NULL, "pf 64");
    CHECK(strstr(text, "BAR0-1              000000600000000C (memory, 64-bit,"
                       " prefetchable)") != NULL, "BAR 64");

    /* No capability list. */
    put16(c, 0x06, 0x0000);
    tr_text_init(&t, text, sizeof(text));
    tr_pci_text(&t, c, 2);
    CHECK(strstr(text, "Capabilities        none (Status bit 4 clear)")
              != NULL, "none");
}

static void test_node_text(void)
{
    CM_QPCI_IN in;
    ULONG n;
    ULONG info;
    TR_QPCI q;
    TR_TEXT t;
    unsigned char c[256];
    int i;

    /* Every function a worst case, three bridges deep and more. */
    dsp_bytes(c);
    put16(c, 0x06, 0xFFFF);
    put16(c, 0x04, 0xFFFF);
    p14s();
    for (i = 0; i < fnCount; i++) {
        unsigned char sec = fn[i].Cfg[0x19];
        unsigned char sub = fn[i].Cfg[0x1A];
        unsigned char pri = fn[i].Cfg[0x18];
        unsigned char hdr = fn[i].Cfg[0x0E];
        unsigned long id = get32(fn[i].Cfg, 0);
        memcpy(fn[i].Cfg, c, 256);
        put32(fn[i].Cfg, 0, id);
        fn[i].Cfg[0x0E] = hdr;
        fn[i].Cfg[0x18] = pri;
        fn[i].Cfg[0x19] = sec;
        fn[i].Cfg[0x1A] = sub;
    }
    in_one_port(&in, BDF(4, 1, 0));
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(CmQpciAnswer(outb, sizeof(outb), built, n, &info),
             CM_Q_ANS_WHOLE, "whole");
    CHECK_EQ(tr_qpci_parse(outb, info, &q), 1, "parsed");
    tr_text_init(&t, text, sizeof(text));
    tr_qpci_text(&t, &q, 1);
    CHECK(!t.Overflow, "fits");
    CHECK(strstr(text, "Host interface (NHI), PCI 05:00.0") != NULL, "NHI");
    CHECK(strstr(text, "Tunnel port, PCI 04:01.0 (switch, active)") != NULL,
          "port");
    CHECK(strstr(text, "      PCI 08:00.0") != NULL, "NVMe, indented");
    CHECK(widest(text) <= 79, "79 columns, worst case");

    in.ReadBelow = 0;
    n = CmQpciBuild(built, sizeof(built), &in);
    (void)CmQpciAnswer(outb, sizeof(outb), built, n, &info);
    CHECK_EQ(tr_qpci_parse(outb, info, &q), 1, "parsed 9x");
    tr_text_init(&t, text, sizeof(text));
    tr_qpci_text(&t, &q, 0);
    CHECK(strstr(text, "Not read on this system") != NULL, "9x");
    CHECK(strstr(text, "PCI 08:00.0") == NULL, "nothing below");
    CHECK(widest(text) <= 79, "79 columns, 9x");
}

/* xhci98's PCI region: decoded, not served (old driver, either way), not
 * read, and shapes this tool refuses. */
static void test_hcd(void)
{
    TR_TEXT t;
    unsigned char c[256];

    dsp_bytes(c);
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_hcd_pci_text(&t, TR_SW_OK, 0, 4, 0, 256, 256, c), 1,
             "decoded");
    CHECK(strstr(text, "Vendor:device       8086:15C0") != NULL, "ids");
    CHECK(widest(text) <= 79, "79 columns");
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_hcd_pci_text(&t, TR_SW_OK, TR_SNAP_S_BAD_REGION, 4, 0, 0, 0,
                             c), 0, "old driver");
    CHECK(strstr(text, "Not served by this xhci98.sys") != NULL, "said");
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_hcd_pci_text(&t, TR_SW_OFF, 0, 0, 0, 0, 0, NULL), 0,
             "old driver, log off");
    CHECK(strstr(text, "Not served by this xhci98.sys") != NULL, "said 2");
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_hcd_pci_text(&t, TR_SW_OK, TR_SNAP_S_NOT_READ, 4, 0, 0, 0,
                             c), 0, "not read");
    CHECK(strstr(text, "could not read") != NULL, "said 3");
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_hcd_pci_text(&t, TR_SW_OK, 0, 4, 0, 256, 128, c), 0,
             "short payload");
    CHECK_EQ(tr_hcd_pci_text(&t, TR_SW_OK, TR_SNAP_S_TRUNCATED, 4, 0, 256,
                             256, c), 0, "truncated");
    CHECK_EQ(tr_hcd_pci_text(&t, TR_SW_OK, 0, 3, 0, 256, 256, c), 0,
             "another region");
    CHECK_EQ(tr_hcd_pci_text(&t, TR_SW_BAD, 0, 4, 0, 256, 256, c), 0, "bad");
    CHECK_EQ(TR_SNAP_REGION_PCI, XHCI_SNAPSHOT_REGION_PCI, "region held to xhci.h");
    CHECK_EQ(TR_SNAP_PCI_BYTES, XHCI_SNAPSHOT_PCI_BYTES, "bytes held to xhci.h");
    CHECK_EQ(TR_SNAP_S_NOT_READ, XHCI_SNAPSHOT_S_NOT_READ, "bit held to xhci.h");
}

/* A switch with two downstream ports: the walk finds both bridges before
 * either's function, and the text draws each function under its own. */
static void test_branch(void)
{
    CM_QPCI_IN in;
    ULONG n;
    ULONG info;
    TR_QPCI q;
    TR_TEXT t;
    const char *a;
    const char *b;
    const char *c;

    reset();
    add_fn(BDF(5, 0, 0), 0x15BF8086UL, 0x088000UL);
    add_bridge(BDF(4, 1, 0), 0x15C08086UL, 4, 6, 10);
    add_bridge(BDF(6, 0, 0), 0x24631B21UL, 6, 7, 10);
    add_bridge(BDF(7, 0, 0), 0x24631B21UL, 7, 8, 8);
    add_bridge(BDF(7, 1, 0), 0x24631B21UL, 7, 9, 10);
    add_fn(BDF(8, 0, 0), 0x174A1C5CUL, 0x010802UL);
    add_fn(BDF(9, 0, 0), 0x15B68086UL, 0x0C0330UL);
    in_one_port(&in, BDF(4, 1, 0));
    n = CmQpciBuild(built, sizeof(built), &in);
    CHECK_EQ(n, CM_QP_HDR_BYTES + 7 * CM_QP_ENT_BYTES, "seven");
    CHECK_EQ(ent(built, 4, CM_QP_E_BDF), BDF(7, 1, 0), "both bridges first");
    CHECK_EQ(ent(built, 5, CM_QP_E_BDF), BDF(8, 0, 0), "then bus 8");
    (void)CmQpciAnswer(outb, sizeof(outb), built, n, &info);
    CHECK_EQ(tr_qpci_parse(outb, info, &q), 1, "parsed");
    tr_text_init(&t, text, sizeof(text));
    tr_qpci_text(&t, &q, 1);
    a = strstr(text, "PCI 07:00.0");
    b = strstr(text, "PCI 08:00.0");
    c = strstr(text, "PCI 07:01.0");
    CHECK(a != NULL && b != NULL && c != NULL, "all three");
    CHECK(a < b && b < c, "the NVMe under its own bridge");
    CHECK(strstr(text, "        PCI 08:00.0") != NULL, "indented two deep");
    CHECK(strstr(text, "Behind a bridge not listed") == NULL, "no orphan");

    /* An entry whose parent is not in the answer is still shown. */
    put32(outb, CM_QP_HDR_BYTES + 5 * CM_QP_ENT_BYTES + CM_QP_E_PARENT,
          BDF(0x30, 0, 0));
    tr_text_init(&t, text, sizeof(text));
    tr_qpci_text(&t, &q, 1);
    CHECK(strstr(text, "Behind a bridge not listed") != NULL, "orphan said");
    CHECK(strstr(text, "PCI 08:00.0") != NULL, "orphan shown");
    /* Parents that loop end. */
    put32(outb, CM_QP_HDR_BYTES + 5 * CM_QP_ENT_BYTES + CM_QP_E_PARENT,
          BDF(9, 0, 0));
    put32(outb, CM_QP_HDR_BYTES + 6 * CM_QP_ENT_BYTES + CM_QP_E_PARENT,
          BDF(8, 0, 0));
    tr_text_init(&t, text, sizeof(text));
    tr_qpci_text(&t, &q, 1);
    CHECK(!t.Overflow, "a loop of parents ends");
}

/* A reserved link speed code at the deepest indent keeps to 79 columns. */
static void test_reserved_speed(void)
{
    char s[96];
    unsigned char c[256];
    TR_TEXT t;

    memset(c, 0, sizeof(c));
    put32(c, 0xA0 + 0x0C, 0x000003FFUL);
    put16(c, 0xA0 + 0x12, 0x23FF);
    tr_pci_link(c, 0xA0, s, sizeof(s));
    CHECK(strcmp(s, "code 15 x63 (capable code 15 x63)") == 0, "reserved");
    dsp_bytes(c);
    put32(c, 0xC0 + 0x0C, 0x000003FFUL);
    put16(c, 0xC0 + 0x12, 0x23FF);
    put16(c, 0x06, 0xFFFF);
    tr_text_init(&t, text, sizeof(text));
    tr_pci_text(&t, c, 10);
    CHECK(widest(text) <= 79, "79 columns at the deepest indent");
}

int main(void)
{
    test_hcd();
    test_branch();
    test_reserved_speed();
    test_layout();
    test_chain();
    test_bounds();
    test_states();
    test_answer();
    test_short();
    test_walk();
    test_text();
    test_node_text();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
