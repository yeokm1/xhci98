/*
 * test_func.c - host vectors for the pure half of composite splitting
 * (src\xhci_func.c; roadmap-hcd.md tasks 26-A.7 and 26-A.9).
 *
 * The three device vectors are CONSTRUCTED in the shape of the named
 * devices, not read off them: design record 13 section 10.10 records that
 * neither the UAC 1.0 units' interface numbering nor the IAD device's IAD
 * fields have been read. Every expected value was worked by hand from the
 * descriptor bytes (USB 2.0 chapter 9 and the IAD ECN layouts), each
 * filtered length cross-checked by summing descriptor lengths, and the
 * grouping from design record 13 section 10.8's rules, not from the code
 * under test. The id strings follow section 10.7; the instance-id form
 * (decimal root port, then MI_ as two hex digits) is this driver's own
 * policy and tests that policy, not a Microsoft rule.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_func.h"
#include "test_harness.h"

/* C-Media 0D8C:0014 shape: AC if0, AS if1 (alt1 iso OUT), AS if2 (alt1 iso
 * IN), HID if3 (interrupt IN), no IAD. */
static const UCHAR cmDev[18] = {
    0x12, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 0x08,
    0x8C, 0x0D, 0x14, 0x00, 0x00, 0x01, 0x01, 0x02, 0x00, 0x01 };
static const UCHAR cmCfg[218] = {
    0x09, 0x02, 0xDA, 0x00, 0x04, 0x01, 0x00, 0x80, 0x32,
    /* 9: if0 AudioControl */
    0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    0x0A, 0x24, 0x01, 0x00, 0x01, 0x47, 0x00, 0x02, 0x01, 0x02,
    0x0C, 0x24, 0x02, 0x01, 0x01, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x0A, 0x24, 0x06, 0x02, 0x01, 0x01, 0x01, 0x02, 0x02, 0x00,
    0x09, 0x24, 0x03, 0x03, 0x01, 0x03, 0x00, 0x02, 0x00,
    0x0C, 0x24, 0x02, 0x04, 0x01, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x24, 0x06, 0x05, 0x04, 0x01, 0x03, 0x00, 0x00,
    0x09, 0x24, 0x03, 0x06, 0x01, 0x01, 0x00, 0x05, 0x00,
    /* 89: if1 AudioStreaming alt 0, alt 1 */
    0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,
    0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,
    0x07, 0x24, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x0B, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x01, 0x80, 0xBB, 0x00,
    0x09, 0x05, 0x01, 0x09, 0xC8, 0x00, 0x01, 0x00, 0x00,
    0x07, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00,
    /* 141: if2 AudioStreaming alt 0, alt 1 */
    0x09, 0x04, 0x02, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,
    0x09, 0x04, 0x02, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,
    0x07, 0x24, 0x01, 0x06, 0x01, 0x01, 0x00,
    0x0B, 0x24, 0x02, 0x01, 0x01, 0x02, 0x10, 0x01, 0x80, 0xBB, 0x00,
    0x09, 0x05, 0x82, 0x05, 0x64, 0x00, 0x01, 0x00, 0x00,
    0x07, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00,
    /* 193: if3 HID */
    0x09, 0x04, 0x03, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x00, 0x01, 0x00, 0x01, 0x22, 0x3C, 0x00,
    0x07, 0x05, 0x83, 0x03, 0x04, 0x00, 0x20 };

/* Composite keyboard 045E:0750 shape: boot keyboard if0, HID if1. */
static const UCHAR kbDev[18] = {
    0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x08,
    0x5E, 0x04, 0x50, 0x07, 0x00, 0x01, 0x01, 0x02, 0x00, 0x01 };
static const UCHAR kbCfg[59] = {
    0x09, 0x02, 0x3B, 0x00, 0x02, 0x01, 0x00, 0xA0, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x41, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,
    0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x31, 0x00,
    0x07, 0x05, 0x82, 0x03, 0x08, 0x00, 0x0A };

/* IAD device (an X4's shape, synthetic ids 1234:5678): CDC IAD if0-1, UAC
 * 2.0 IAD if2-3, HID if4 alone; device class EF/02/01. */
static const UCHAR iadDev[18] = {
    0x12, 0x01, 0x00, 0x02, 0xEF, 0x02, 0x01, 0x40,
    0x34, 0x12, 0x78, 0x56, 0x00, 0x01, 0x01, 0x02, 0x03, 0x01 };
static const UCHAR iadCfg[142] = {
    0x09, 0x02, 0x8E, 0x00, 0x05, 0x01, 0x00, 0x80, 0xFA,
    /* 9 */  0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x01, 0x00,
    /* 17 */ 0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x06, 0x00, 0x01,
    0x07, 0x05, 0x83, 0x03, 0x10, 0x00, 0x08,
    /* 43 */ 0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x04, 0x02, 0x40, 0x00, 0x00,
    0x07, 0x05, 0x85, 0x02, 0x40, 0x00, 0x00,
    /* 66 */ 0x08, 0x0B, 0x02, 0x02, 0x01, 0x00, 0x20, 0x05,
    /* 74 */ 0x09, 0x04, 0x02, 0x00, 0x00, 0x01, 0x01, 0x20, 0x00,
    0x09, 0x24, 0x01, 0x00, 0x02, 0x08, 0x09, 0x00, 0x00,
    /* 92 */ 0x09, 0x04, 0x03, 0x00, 0x00, 0x01, 0x02, 0x20, 0x00,
    0x09, 0x04, 0x03, 0x01, 0x01, 0x01, 0x02, 0x20, 0x00,
    0x07, 0x05, 0x06, 0x05, 0xC0, 0x00, 0x01,
    /* 117 */ 0x09, 0x04, 0x04, 0x00, 0x01, 0x03, 0x00, 0x00, 0x07,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x20, 0x00,
    0x07, 0x05, 0x87, 0x03, 0x08, 0x00, 0x0A };

static UCHAR dev[18];
static UCHAR buf[256];

static void copy_dev(const UCHAR *from)
{
    memcpy(dev, from, sizeof(dev));
}

static void check_func(const XHCI_FUNC *f, ULONG first, ULONG mask,
                       ULONG count, ULONG cls, ULONG sub, ULONG prot,
                       ULONG iad, const char *what, int line)
{
    check_eq_impl(f->FirstInterface, first, what, __FILE__, line);
    check_eq_impl(f->InterfaceMask, mask, what, __FILE__, line);
    check_eq_impl(f->InterfaceCount, count, what, __FILE__, line);
    check_eq_impl(f->Class, cls, what, __FILE__, line);
    check_eq_impl(f->SubClass, sub, what, __FILE__, line);
    check_eq_impl(f->Protocol, prot, what, __FILE__, line);
    check_eq_impl(f->IadOffset, iad, what, __FILE__, line);
}

/* The filtered descriptor is the 9-byte header rewritten, then exactly
 * `body` bytes of the device's configuration from `from`. */
static void check_filtered(const UCHAR *cfg, ULONG length,
                           const XHCI_FUNC *f, ULONG total, ULONG ifaces,
                           ULONG from, ULONG body, const char *what,
                           int line)
{
    ULONG got;
    ULONG answer;

    memset(buf, 0xEE, sizeof(buf));
    got = 0;
    answer = XhciFuncConfig(cfg, length, f, buf, sizeof(buf), &got);
    check_eq_impl(answer, XHCI_FUNC_OK, what, __FILE__, line);
    check_eq_impl(got, total, what, __FILE__, line);
    check_eq_impl(9UL + body, total, what, __FILE__, line);
    check_eq_impl(buf[0], cfg[0], what, __FILE__, line);
    check_eq_impl(buf[1], 0x02, what, __FILE__, line);
    check_eq_impl(buf[2], total & 0xFF, what, __FILE__, line);
    check_eq_impl(buf[3], total >> 8, what, __FILE__, line);
    check_eq_impl(buf[4], ifaces, what, __FILE__, line);
    check_eq_impl(memcmp(buf + 5, cfg + 5, 4), 0, what, __FILE__, line);
    check_eq_impl(memcmp(buf + 9, cfg + from, body), 0, what, __FILE__,
                  line);
    check_eq_impl(buf[total], 0xEE, what, __FILE__, line);
}

static void test_cmedia(void)
{
    XHCI_FUNC_SET set;
    ULONG got;

    copy_dev(cmDev);
    CHECK_EQ(XhciFuncSplit(dev, cmCfg, sizeof(cmCfg), &set), XHCI_FUNC_OK,
             "C-Media splits");
    CHECK_EQ(set.Count, 2, "C-Media: audio and HID");
    check_func(&set.Func[0], 0, 0x07, 3, 0x01, 0x01, 0x00, 0,
               "C-Media F0: AC + two AS, positional", __LINE__);
    check_func(&set.Func[1], 3, 0x08, 1, 0x03, 0x00, 0x00, 0,
               "C-Media F1: the HID", __LINE__);
    check_filtered(cmCfg, sizeof(cmCfg), &set.Func[0], 193, 3, 9, 184,
                   "C-Media F0 descriptor", __LINE__);
    check_filtered(cmCfg, sizeof(cmCfg), &set.Func[1], 34, 1, 193, 25,
                   "C-Media F1 descriptor", __LINE__);

    /* usbaudio's first read is 9 bytes: the rewritten header alone. */
    memset(buf, 0xEE, sizeof(buf));
    CHECK_EQ(XhciFuncConfig(cmCfg, sizeof(cmCfg), &set.Func[0], buf, 9,
                            &got),
             XHCI_FUNC_OK, "9-byte read");
    CHECK_EQ(got, 193, "9-byte read: whole length reported");
    CHECK_EQ(buf[2], 0xC1, "wTotalLength low 193");
    CHECK_EQ(buf[3], 0x00, "wTotalLength high");
    CHECK_EQ(buf[4], 0x03, "bNumInterfaces 3, not the device's 4");
    CHECK_EQ(buf[5], 0x01, "bConfigurationValue copied");
    CHECK_EQ(buf[7], 0x80, "bmAttributes copied");
    CHECK_EQ(buf[8], 0x32, "bMaxPower copied");
    CHECK_EQ(buf[9], 0xEE, "nothing past the capacity");

    CHECK_EQ(XhciFuncConfig(cmCfg, sizeof(cmCfg), &set.Func[1], NULL, 0,
                            &got),
             XHCI_FUNC_OK, "size query with no buffer");
    CHECK_EQ(got, 34, "size query answer");
}

static void test_keyboard(void)
{
    XHCI_FUNC_SET set;

    copy_dev(kbDev);
    CHECK_EQ(XhciFuncSplit(dev, kbCfg, sizeof(kbCfg), &set), XHCI_FUNC_OK,
             "keyboard splits");
    CHECK_EQ(set.Count, 2, "keyboard: two HID functions");
    check_func(&set.Func[0], 0, 0x1, 1, 0x03, 0x01, 0x01, 0,
               "keyboard F0: boot keyboard", __LINE__);
    check_func(&set.Func[1], 1, 0x2, 1, 0x03, 0x00, 0x00, 0,
               "keyboard F1", __LINE__);
    check_filtered(kbCfg, sizeof(kbCfg), &set.Func[0], 34, 1, 9, 25,
                   "keyboard F0 descriptor", __LINE__);
    check_filtered(kbCfg, sizeof(kbCfg), &set.Func[1], 34, 1, 34, 25,
                   "keyboard F1 descriptor", __LINE__);
}

static void test_iad(void)
{
    XHCI_FUNC_SET set;

    copy_dev(iadDev);
    CHECK_EQ(XhciFuncSplit(dev, iadCfg, sizeof(iadCfg), &set), XHCI_FUNC_OK,
             "IAD device splits");
    CHECK_EQ(set.Count, 3, "IAD device: CDC, audio, HID");
    check_func(&set.Func[0], 0, 0x03, 2, 0x02, 0x02, 0x01, 9,
               "IAD F0: CDC pair", __LINE__);
    check_func(&set.Func[1], 2, 0x0C, 2, 0x01, 0x01, 0x20, 66,
               "IAD F1: UAC 2.0 pair", __LINE__);
    check_func(&set.Func[2], 4, 0x10, 1, 0x03, 0x00, 0x00, 0,
               "IAD F2: HID alone", __LINE__);
    CHECK_EQ(set.Func[1].StringIndex, 5, "iFunction wins over iInterface");
    CHECK_EQ(set.Func[2].StringIndex, 7, "iInterface without an IAD");
    CHECK_EQ(set.Func[0].StringIndex, 0, "neither string: 0");
    check_filtered(iadCfg, sizeof(iadCfg), &set.Func[0], 66, 2, 9, 57,
                   "IAD F0 descriptor, IAD included", __LINE__);
    check_filtered(iadCfg, sizeof(iadCfg), &set.Func[1], 60, 2, 66, 51,
                   "IAD F1 descriptor, IAD included", __LINE__);
    check_filtered(iadCfg, sizeof(iadCfg), &set.Func[2], 34, 1, 117, 25,
                   "IAD F2 descriptor, no IAD", __LINE__);

    /* An IAD forces the split whatever the device class says (section
     * 10.8, "or the configuration carries IADs"). */
    dev[4] = 0x02;
    dev[5] = 0x00;
    dev[6] = 0x00;
    CHECK_EQ(XhciFuncSplit(dev, iadCfg, sizeof(iadCfg), &set), XHCI_FUNC_OK,
             "IAD with device class 02 still splits");
    CHECK_EQ(set.Count, 3, "still three functions");
}

static void test_no_split(void)
{
    XHCI_FUNC_SET set;
    UCHAR one[34];

    copy_dev(cmDev);
    dev[4] = 0xFF;
    CHECK_EQ(XhciFuncSplit(dev, cmCfg, sizeof(cmCfg), &set),
             XHCI_FUNC_NO_SPLIT, "vendor device class: whole");
    CHECK_EQ(set.Count, 0, "no functions");

    copy_dev(kbDev);
    dev[4] = 0x09;
    CHECK_EQ(XhciFuncSplit(dev, kbCfg, sizeof(kbCfg), &set),
             XHCI_FUNC_NO_SPLIT, "hub: whole");

    copy_dev(kbDev);
    dev[4] = 0x02;
    CHECK_EQ(XhciFuncSplit(dev, kbCfg, sizeof(kbCfg), &set),
             XHCI_FUNC_NO_SPLIT, "CDC device class, no IAD: whole");

    copy_dev(cmDev);
    dev[17] = 0;
    CHECK_EQ(XhciFuncSplit(dev, cmCfg, sizeof(cmCfg), &set),
             XHCI_FUNC_NO_SPLIT, "no configuration at all: whole");

    /* Several configurations: split by the first, the one the bus selects
     * (design record 13 section 10.8; coordinator, 2026-10-03). */
    copy_dev(cmDev);
    dev[17] = 2;
    CHECK_EQ(XhciFuncSplit(dev, cmCfg, sizeof(cmCfg), &set), XHCI_FUNC_OK,
             "two configurations: split by the first");
    CHECK_EQ(set.Count, 2, "the same two functions");

    memcpy(one, kbCfg, 34);
    one[2] = 34;
    one[4] = 1;
    copy_dev(kbDev);
    CHECK_EQ(XhciFuncSplit(dev, one, sizeof(one), &set), XHCI_FUNC_NO_SPLIT,
             "one interface: whole");
}

/* A 9-byte header and `n` interface descriptors (no endpoints), each class
 * cls[i] / subclass sub[i], numbered 0..n-1. */
static ULONG build_ifaces(UCHAR *out, ULONG n, const UCHAR *cls,
                          const UCHAR *sub)
{
    ULONG i;
    ULONG at;

    memset(out, 0, 9 + 9 * n);
    out[0] = 9;
    out[1] = 2;
    out[2] = (UCHAR)(9 + 9 * n);
    out[4] = (UCHAR)n;
    out[5] = 1;
    for (i = 0; i < n; i++) {
        at = 9 + 9 * i;
        out[at] = 9;
        out[at + 1] = 4;
        out[at + 2] = (UCHAR)i;
        out[at + 5] = cls[i];
        out[at + 6] = sub[i];
    }
    return 9 + 9 * n;
}

static void test_positional(void)
{
    static const UCHAR c1[3] = { 0x01, 0x03, 0x01 };
    static const UCHAR s1[3] = { 0x01, 0x00, 0x02 };
    static const UCHAR c2[4] = { 0x01, 0x01, 0x01, 0x01 };
    static const UCHAR s2[4] = { 0x01, 0x02, 0x01, 0x02 };
    static const UCHAR c3[3] = { 0x01, 0x0D, 0x03 };
    static const UCHAR s3[3] = { 0x01, 0x00, 0x00 };
    static const UCHAR c4[4] = { 0x01, 0x01, 0x01, 0x01 };
    static const UCHAR s4[4] = { 0x01, 0x02, 0x02, 0x03 };
    UCHAR cfg[64];
    XHCI_FUNC_SET set;
    ULONG len;
    ULONG got;

    copy_dev(kbDev);

    len = build_ifaces(cfg, 3, c1, s1);
    CHECK_EQ(XhciFuncSplit(dev, cfg, len, &set), XHCI_FUNC_OK, "AC HID AS");
    CHECK_EQ(set.Count, 3, "an HID between strands the AS (10.8)");
    CHECK_EQ(set.Func[0].InterfaceMask, 0x1, "AC alone");
    CHECK_EQ(set.Func[1].InterfaceMask, 0x2, "HID alone");
    CHECK_EQ(set.Func[2].InterfaceMask, 0x4, "AS alone");

    len = build_ifaces(cfg, 4, c2, s2);
    CHECK_EQ(XhciFuncSplit(dev, cfg, len, &set), XHCI_FUNC_OK,
             "AC AS AC AS");
    CHECK_EQ(set.Count, 2, "a second AC starts a function");
    CHECK_EQ(set.Func[0].InterfaceMask, 0x3, "first pair");
    CHECK_EQ(set.Func[1].InterfaceMask, 0xC, "second pair");
    CHECK_EQ(set.Func[1].FirstInterface, 2, "MI_02");

    len = build_ifaces(cfg, 3, c3, s3);
    CHECK_EQ(XhciFuncSplit(dev, cfg, len, &set), XHCI_FUNC_OK,
             "AC CS HID");
    CHECK_EQ(set.Count, 2, "Content Security belongs to none");
    CHECK_EQ(set.Func[0].InterfaceMask, 0x1, "AC; the CS ends its run");
    CHECK_EQ(set.Func[1].InterfaceMask, 0x4, "HID");
    CHECK_EQ(XhciFuncConfig(cfg, len, &set.Func[0], NULL, 0, &got),
             XHCI_FUNC_OK, "AC filtered");
    CHECK_EQ(got, 18, "the CS interface is in no filtered descriptor");

    len = build_ifaces(cfg, 4, c4, s4);
    CHECK_EQ(XhciFuncSplit(dev, cfg, len, &set), XHCI_FUNC_OK,
             "AC AS AS MIDI");
    CHECK_EQ(set.Count, 1, "one audio function");
    CHECK_EQ(set.Func[0].InterfaceMask, 0xF, "all four");
    CHECK_EQ(set.Func[0].InterfaceCount, 4, "bNumInterfaces 4");
}

static void test_malformed(void)
{
    UCHAR cfg[64];
    XHCI_FUNC_SET set;
    XHCI_FUNC f;
    ULONG got;

    copy_dev(kbDev);

    memcpy(cfg, kbCfg, sizeof(kbCfg));
    cfg[18] = 0;
    CHECK_EQ(XhciFuncSplit(dev, cfg, sizeof(kbCfg), &set),
             XHCI_FUNC_MALFORMED, "a bLength of 0");

    memcpy(cfg, kbCfg, sizeof(kbCfg));
    cfg[52] = 0x09;
    CHECK_EQ(XhciFuncSplit(dev, cfg, sizeof(kbCfg), &set),
             XHCI_FUNC_MALFORMED, "the last descriptor runs past the end");

    CHECK_EQ(XhciFuncSplit(dev, kbCfg, sizeof(kbCfg) - 1, &set),
             XHCI_FUNC_MALFORMED, "wTotalLength above the length read");

    memcpy(cfg, kbCfg, sizeof(kbCfg));
    cfg[36] = 0x00;
    CHECK_EQ(XhciFuncSplit(dev, cfg, sizeof(kbCfg), &set),
             XHCI_FUNC_MALFORMED, "two alternate-0 descriptors of if0");

    memcpy(cfg, kbCfg, sizeof(kbCfg));
    cfg[1] = 0x04;
    CHECK_EQ(XhciFuncSplit(dev, cfg, sizeof(kbCfg), &set),
             XHCI_FUNC_MALFORMED, "not a configuration descriptor");

    /* The IAD shortened to 7 bytes and a 2-byte class-specific filler
     * after it, so the walk stays aligned and only the IAD rule refuses. */
    memcpy(cfg, iadCfg, 17);
    cfg[2] = 18;
    cfg[9] = 7;
    cfg[16] = 0x02;
    cfg[17] = 0x24;
    CHECK_EQ(XhciFuncSplit(iadDev, cfg, 18, &set), XHCI_FUNC_MALFORMED,
             "an IAD of 7 bytes");
    cfg[9] = 8;
    cfg[16] = 0x00;
    cfg[17] = 0x00;
    cfg[2] = 17;
    CHECK_EQ(XhciFuncSplit(iadDev, cfg, 17, &set), XHCI_FUNC_NO_SPLIT,
             "the same IAD at 8 bytes: sound, no interface to split");

    memcpy(cfg, kbCfg, sizeof(kbCfg));
    cfg[9] = 8;
    cfg[17] = 0x02;
    CHECK_EQ(XhciFuncSplit(dev, cfg, sizeof(kbCfg), &set),
             XHCI_FUNC_MALFORMED, "an interface descriptor of 8 bytes");

    CHECK_EQ(XhciFuncSplit(NULL, kbCfg, sizeof(kbCfg), &set),
             XHCI_FUNC_BAD_PARAM, "NULL device");
    memset(&f, 0, sizeof(f));
    CHECK_EQ(XhciFuncConfig(kbCfg, sizeof(kbCfg), &f, NULL, 4, &got),
             XHCI_FUNC_BAD_PARAM, "NULL buffer with a capacity");
}

static void test_setup(void)
{
    static const UCHAR hidReport[8] = { 0x81, 0x06, 0x00, 0x22,
                                        0x00, 0x00, 0x41, 0x00 };
    static const UCHAR setIdle[8] = { 0x21, 0x0A, 0x00, 0x00,
                                      0x03, 0x00, 0x00, 0x00 };
    static const UCHAR vendor[8] = { 0x41, 0x01, 0x00, 0x00,
                                     0x05, 0x00, 0x00, 0x00 };
    static const UCHAR toEndpoint[8] = { 0x22, 0x01, 0x00, 0x01,
                                         0x82, 0x00, 0x03, 0x00 };
    static const UCHAR device[8] = { 0x80, 0x06, 0x00, 0x02,
                                     0x00, 0x00, 0xFF, 0x00 };
    static const UCHAR high[8] = { 0x81, 0x0A, 0x00, 0x00,
                                   0x20, 0x00, 0x01, 0x00 };

    CHECK_EQ(XhciFuncSetupAllowed(hidReport, 0x7), 1,
             "interface 0 is the function's");
    CHECK_EQ(XhciFuncSetupAllowed(hidReport, 0x8), 0,
             "interface 0 is a sibling's");
    CHECK_EQ(XhciFuncSetupAllowed(setIdle, 0x8), 1, "SET_IDLE on if3");
    CHECK_EQ(XhciFuncSetupAllowed(setIdle, 0x7), 0, "SET_IDLE, not ours");
    CHECK_EQ(XhciFuncSetupAllowed(vendor, 0x1), 1,
             "vendor to an interface passes");
    CHECK_EQ(XhciFuncSetupAllowed(toEndpoint, 0x1), 1,
             "endpoint recipient passes");
    CHECK_EQ(XhciFuncSetupAllowed(device, 0x1), 1, "device recipient");
    CHECK_EQ(XhciFuncSetupAllowed(high, 0xFFFFFFFFUL), 0,
             "interface 32 is nobody's");
    CHECK_EQ(XhciFuncSetupAllowed(NULL, 0xFFFFFFFFUL), 0, "NULL setup");
}

static void check_id(ULONG which, const char *want, ULONG wantLength,
                     const char *what, int line)
{
    XHCI_FUNC_SET set;
    char out[128];
    ULONG used;

    copy_dev(cmDev);
    (VOID)XhciFuncSplit(dev, cmCfg, sizeof(cmCfg), &set);
    memset(out, 'x', sizeof(out));
    used = 0;
    check_eq_impl(XhciFuncId(dev, &set.Func[1], 3, which, out, sizeof(out),
                             &used),
                  XHCI_FUNC_OK, what, __FILE__, line);
    check_eq_impl(used, wantLength, what, __FILE__, line);
    check_eq_impl(memcmp(out, want, wantLength), 0, what, __FILE__, line);
    check_eq_impl(out[wantLength], 'x', what, __FILE__, line);
}

static void test_ids(void)
{
    static const char devId[] = "USB\\VID_0D8C&PID_0014&MI_03\0";
    static const char hwIds[] =
        "USB\\VID_0D8C&PID_0014&REV_0100&MI_03\0"
        "USB\\VID_0D8C&PID_0014&MI_03\0";
    static const char compatIds[] =
        "USB\\Class_03&SubClass_00&Prot_00\0"
        "USB\\Class_03&SubClass_00\0"
        "USB\\Class_03\0";
    static const char instance[] = "303";
    XHCI_FUNC_SET set;
    char out[4];
    ULONG used;

    check_id(XHCI_FUNC_ID_DEVICE, devId, sizeof(devId) - 1,
             "device id", __LINE__);
    CHECK_EQ(sizeof(devId) - 1, 28, "device id: 28 with its NUL");
    check_id(XHCI_FUNC_ID_HARDWARE, hwIds, sizeof(hwIds), "hardware ids",
             __LINE__);
    CHECK_EQ(sizeof(hwIds), 66, "hardware ids: 66 with both NULs");
    check_id(XHCI_FUNC_ID_COMPATIBLE, compatIds, sizeof(compatIds),
             "compatible ids", __LINE__);
    CHECK_EQ(sizeof(compatIds), 72, "compatible ids: 72");
    check_id(XHCI_FUNC_ID_INSTANCE, instance, sizeof(instance),
             "instance id: port 3, MI_03", __LINE__);

    copy_dev(cmDev);
    (VOID)XhciFuncSplit(dev, cmCfg, sizeof(cmCfg), &set);
    used = 0;
    CHECK_EQ(XhciFuncId(dev, &set.Func[1], 3, XHCI_FUNC_ID_INSTANCE, out, 3,
                        &used),
             XHCI_FUNC_TOO_SMALL, "instance id in 3 bytes");
    CHECK_EQ(used, 4, "the size it needs");
    CHECK_EQ(XhciFuncId(dev, &set.Func[1], 12, XHCI_FUNC_ID_INSTANCE, out,
                        sizeof(out), &used),
             XHCI_FUNC_TOO_SMALL, "port 12: five bytes");
    CHECK_EQ(used, 5, "1203 and its NUL");
    CHECK_EQ(XhciFuncId(dev, &set.Func[1], 3, 9, out, sizeof(out), &used),
             XHCI_FUNC_BAD_PARAM, "no such id");
}

int main(void)
{
    test_cmedia();
    test_keyboard();
    test_iad();
    test_no_split();
    test_positional();
    test_malformed();
    test_setup();
    test_ids();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
