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
    check_func(&set.Func[1], 2, 0x0C, 2, 0x01, 0x00, 0x20, 66,
               "IAD F1: UAC 2.0 pair, the IAD's triple", __LINE__);
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

    /* Microsoft's composite parent: an IAD does not override a device
     * class of the device's own. */
    dev[4] = 0x02;
    dev[5] = 0x00;
    dev[6] = 0x00;
    CHECK_EQ(XhciFuncSplit(dev, iadCfg, sizeof(iadCfg), &set),
             XHCI_FUNC_NO_SPLIT, "IAD with device class 02: whole");
    CHECK_EQ(set.Count, 0, "no functions");

    dev[4] = 0x00;
    CHECK_EQ(XhciFuncSplit(dev, iadCfg, sizeof(iadCfg), &set), XHCI_FUNC_OK,
             "IAD with device class 00 splits");
    CHECK_EQ(set.Count, 3, "three functions");

    copy_dev(iadDev);
    dev[6] = 0x00;
    CHECK_EQ(XhciFuncSplit(dev, iadCfg, sizeof(iadCfg), &set),
             XHCI_FUNC_NO_SPLIT, "EF/02/00: whole");

    copy_dev(iadDev);
    dev[17] = 2;
    CHECK_EQ(XhciFuncSplit(dev, iadCfg, sizeof(iadCfg), &set),
             XHCI_FUNC_NO_SPLIT, "EF/02/01 with two configurations: whole");
}

/* IAD over if0-1 (CDC), then AudioControl if2 and AudioStreaming if3 with
 * no IAD: the IAD turns the audio rule off for the whole configuration. */
static const UCHAR iadAudioCfg[53] = {
    0x09, 0x02, 0x35, 0x00, 0x04, 0x01, 0x00, 0x80, 0x32,
    /* 9 */  0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x01, 0x00,
    /* 17 */ 0x09, 0x04, 0x00, 0x00, 0x00, 0x02, 0x02, 0x01, 0x00,
    /* 26 */ 0x09, 0x04, 0x01, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
    /* 35 */ 0x09, 0x04, 0x02, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    /* 44 */ 0x09, 0x04, 0x03, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00 };

static void test_iad_audio(void)
{
    XHCI_FUNC_SET set;

    copy_dev(kbDev);
    CHECK_EQ(XhciFuncSplit(dev, iadAudioCfg, sizeof(iadAudioCfg), &set),
             XHCI_FUNC_OK, "IAD plus legacy audio splits");
    CHECK_EQ(set.Count, 3, "MI_00, MI_02, MI_03");
    check_func(&set.Func[0], 0, 0x3, 2, 0x02, 0x02, 0x01, 9,
               "IAD + audio F0: the IAD", __LINE__);
    check_func(&set.Func[1], 2, 0x4, 1, 0x01, 0x01, 0x00, 0,
               "IAD + audio F1: AC alone", __LINE__);
    check_func(&set.Func[2], 3, 0x8, 1, 0x01, 0x02, 0x00, 0,
               "IAD + audio F2: AS alone", __LINE__);
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

    /* Several configurations: Microsoft's composite parent leaves the
     * device whole, under the driver that matched USB\VID_v&PID_p. */
    copy_dev(cmDev);
    dev[17] = 2;
    CHECK_EQ(XhciFuncSplit(dev, cmCfg, sizeof(cmCfg), &set),
             XHCI_FUNC_NO_SPLIT, "two configurations: whole");
    CHECK_EQ(set.Count, 0, "no functions");

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
    static const UCHAR uacOwn[8] = { 0xA1, 0x81, 0x00, 0x02,
                                     0x01, 0x02, 0x02, 0x00 };
    static const UCHAR uacSibling[8] = { 0xA1, 0x81, 0x00, 0x02,
                                         0x03, 0x02, 0x02, 0x00 };

    XHCI_FUNC_SET set;

    CHECK_EQ(XhciFuncSetupAllowed(hidReport, 0x7, 0xF), 1,
             "interface 0 is the function's");
    CHECK_EQ(XhciFuncSetupAllowed(hidReport, 0x8, 0xF), 0,
             "interface 0 is a sibling's");
    CHECK_EQ(XhciFuncSetupAllowed(setIdle, 0x8, 0xF), 1, "SET_IDLE on if3");
    CHECK_EQ(XhciFuncSetupAllowed(setIdle, 0x7, 0xF), 0,
             "SET_IDLE, not ours");
    CHECK_EQ(XhciFuncSetupAllowed(vendor, 0x1, 0xFF), 1,
             "vendor to an interface passes");
    CHECK_EQ(XhciFuncSetupAllowed(toEndpoint, 0x1, 0xF), 1,
             "endpoint recipient passes");
    CHECK_EQ(XhciFuncSetupAllowed(device, 0x1, 0xF), 1, "device recipient");
    CHECK_EQ(XhciFuncSetupAllowed(high, 0x1, 0xF), 1,
             "interface 32 is nobody's: the device answers");
    CHECK_EQ(XhciFuncSetupAllowed(setIdle, 0x1, 0x3), 1,
             "interface 3 absent from the device: the device answers");
    CHECK_EQ(XhciFuncSetupAllowed(NULL, 0xFFFFFFFFUL, 0xFFFFFFFFUL), 0,
             "NULL setup");

    /* UAC 1.0 GET_CUR to entity 2: the entity in wIndex's high byte, the
     * interface in its low (c15, Windows 2000 usbaudio.sys). */
    CHECK_EQ(XhciFuncSetupAllowed(uacOwn, 0x7, 0xF), 1,
             "wIndex 0x0201 from the audio function (mask 7)");
    CHECK_EQ(XhciFuncSetupAllowed(uacSibling, 0x7, 0xF), 0,
             "wIndex 0x0203: the HID sibling's interface");

    copy_dev(cmDev);
    (VOID)XhciFuncSplit(dev, cmCfg, sizeof(cmCfg), &set);
    CHECK_EQ(set.Func[0].DeviceMask, 0xF, "C-Media device mask, F0");
    CHECK_EQ(set.Func[1].DeviceMask, 0xF, "C-Media device mask, F1");
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

static void test_iad_ids(void)
{
    static const char compatIds[] =
        "USB\\Class_01&SubClass_00&Prot_20\0"
        "USB\\Class_01&SubClass_00\0"
        "USB\\Class_01\0";
    XHCI_FUNC_SET set;
    char out[128];
    ULONG used;

    copy_dev(iadDev);
    (VOID)XhciFuncSplit(dev, iadCfg, sizeof(iadCfg), &set);
    memset(out, 'x', sizeof(out));
    used = 0;
    CHECK_EQ(XhciFuncId(dev, &set.Func[1], 1, XHCI_FUNC_ID_COMPATIBLE, out,
                        sizeof(out), &used),
             XHCI_FUNC_OK, "IAD compatible ids");
    CHECK_EQ(used, sizeof(compatIds), "IAD compatible ids: length");
    CHECK_EQ(memcmp(out, compatIds, sizeof(compatIds)), 0,
             "IAD compatible ids: the IAD's SubClass 00, not the 01");
}

/*
 * The id builder at its edges, with a function made by hand rather than
 * split (task 26-A.9): hex digits A-F in every field, an interface number
 * past 9, the widest port number, the size query with no buffer, and a
 * buffer too small, which is written to its last byte and not past it.
 */
static void id_is(const XHCI_FUNC *f, ULONG port, ULONG which,
                  const char *want, ULONG wantLength, const char *what)
{
    char out[160];
    ULONG used;

    memset(out, 'x', sizeof(out));
    used = 0;
    CHECK_EQ(XhciFuncId(dev, f, port, which, out, sizeof(out), &used),
             XHCI_FUNC_OK, what);
    CHECK_EQ(used, wantLength, what);
    CHECK_EQ(memcmp(out, want, wantLength), 0, what);
    CHECK_EQ(out[wantLength], 'x', what);
}

static void test_id_edges(void)
{
    static const UCHAR hexDev[18] = {
        0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
        0xCD, 0xAB, 0xEF, 0x00, 0x34, 0x12, 0x01, 0x02, 0x00, 0x01
    };
    static const char devId[] = "USB\\VID_ABCD&PID_00EF&MI_0A\0";
    static const char hwIds[] =
        "USB\\VID_ABCD&PID_00EF&REV_1234&MI_0A\0"
        "USB\\VID_ABCD&PID_00EF&MI_0A\0";
    static const char compatIds[] =
        "USB\\Class_FF&SubClass_0A&Prot_BC\0"
        "USB\\Class_FF&SubClass_0A\0"
        "USB\\Class_FF\0";
    XHCI_FUNC f;
    char small[10];
    ULONG used;

    memset(&f, 0, sizeof(f));
    f.FirstInterface = 10;
    f.InterfaceMask = 1UL << 10;
    f.InterfaceCount = 1;
    f.Class = 0xFF;
    f.SubClass = 0x0A;
    f.Protocol = 0xBC;
    copy_dev(hexDev);

    id_is(&f, 1, XHCI_FUNC_ID_DEVICE, devId, sizeof(devId) - 1,
          "device id: upper-case hex, interface 10 is MI_0A");
    id_is(&f, 1, XHCI_FUNC_ID_HARDWARE, hwIds, sizeof(hwIds),
          "hardware ids: bcdDevice 0x1234 is REV_1234");
    id_is(&f, 1, XHCI_FUNC_ID_COMPATIBLE, compatIds, sizeof(compatIds),
          "compatible ids: FF/0A/BC");
    id_is(&f, 0, XHCI_FUNC_ID_INSTANCE, "00A", 4,
          "instance id: port 0 is one digit, the interface two");
    id_is(&f, 0xFFFFFFFFUL, XHCI_FUNC_ID_INSTANCE, "42949672950A", 13,
          "instance id: the widest port, ten digits");

    used = 0;
    CHECK_EQ(XhciFuncId(dev, &f, 1, XHCI_FUNC_ID_HARDWARE, NULL, 0, &used),
             XHCI_FUNC_TOO_SMALL, "the size query with no buffer");
    CHECK_EQ(used, sizeof(hwIds), "answers the whole multi-string's size");

    memset(small, 'x', sizeof(small));
    CHECK_EQ(XhciFuncId(dev, &f, 1, XHCI_FUNC_ID_DEVICE, small, 9, &used),
             XHCI_FUNC_TOO_SMALL, "nine bytes for a 28-byte id");
    CHECK_EQ(memcmp(small, "USB\\VID_A", 9), 0, "filled to its capacity");
    CHECK_EQ(small[9], 'x', "and not one byte past it");

    CHECK_EQ(XhciFuncId(NULL, &f, 1, XHCI_FUNC_ID_DEVICE, small, 9, &used),
             XHCI_FUNC_BAD_PARAM, "no device descriptor");
    CHECK_EQ(XhciFuncId(dev, NULL, 1, XHCI_FUNC_ID_DEVICE, small, 9, &used),
             XHCI_FUNC_BAD_PARAM, "no function");
    CHECK_EQ(XhciFuncId(dev, &f, 1, XHCI_FUNC_ID_DEVICE, NULL, 9, &used),
             XHCI_FUNC_BAD_PARAM, "a capacity with no buffer");
    CHECK_EQ(XhciFuncId(dev, &f, 1, XHCI_FUNC_ID_DEVICE, small, 9, NULL),
             XHCI_FUNC_BAD_PARAM, "nowhere to say the size");
}

/* A string descriptor of `chars` UTF-16 characters from `text` (ASCII, or
 * the 16-bit values in `wide` when it is not NULL). */
static ULONG make_string(UCHAR *d, const char *text, const USHORT *wide,
                         ULONG chars)
{
    ULONG i;
    ULONG c;

    d[0] = (UCHAR)(2 + chars * 2);
    d[1] = 3;
    for (i = 0; i < chars; i++) {
        c = (wide != NULL) ? wide[i] : (ULONG)(UCHAR)text[i];
        d[2 + i * 2] = (UCHAR)(c & 0xFF);
        d[3 + i * 2] = (UCHAR)(c >> 8);
    }
    return 2 + chars * 2;
}

static void check_serial(const char *text, ULONG expect, const char *what)
{
    UCHAR d[256];
    char out[XHCI_SERIAL_ID_BYTES];
    ULONG bytes;

    memset(out, 'x', sizeof(out));
    bytes = make_string(d, text, NULL, (ULONG)strlen(text));
    CHECK_EQ(XhciFuncSerialId(d, bytes, out, sizeof(out)), expect, what);
    if (expect == XHCI_FUNC_OK) {
        CHECK_EQ(strcmp(out, text), 0, what);
    } else {
        CHECK_EQ(out[0], 0, what);
    }
}

static void check_instance(const char *serial, ULONG location, ULONG mi,
                           const char *want, const char *what)
{
    char out[160];
    ULONG used;

    memset(out, 'x', sizeof(out));
    used = 0;
    CHECK_EQ(XhciFuncInstanceId(serial, location, mi, out, sizeof(out),
                                &used),
             XHCI_FUNC_OK, what);
    CHECK_EQ(used, (ULONG)strlen(want) + 1, what);
    CHECK_EQ(strcmp(out, want), 0, what);
}

static void test_serial_ids(void)
{
    static const USHORT nonAscii[] = { 'A', 0x00E9, 'B' };
    static const USHORT highByte[] = { 'A', 0x0141 };
    static const USHORT nul[] = { 'A', 0x0000, 'B' };
    UCHAR d[256];
    char out[XHCI_SERIAL_ID_BYTES];
    char longest[XHCI_SERIAL_ID_BYTES];
    ULONG bytes;
    ULONG i;

    /* Accepted: printable ASCII 0x21-0x7E, case kept. */
    check_serial("0123456789AB", XHCI_FUNC_OK, "a hex serial");
    check_serial("abcXYZ", XHCI_FUNC_OK, "case is kept");
    check_serial("A!~&#./:_-", XHCI_FUNC_OK, "punctuation, & and ~ pass");
    check_serial("Z", XHCI_FUNC_OK, "one character");

    /* Refused: what an instance id may not carry, so the location. */
    check_serial("", XHCI_FUNC_BAD_SERIAL, "empty string");
    check_serial("AB CD", XHCI_FUNC_BAD_SERIAL, "a space");
    check_serial("AB,CD", XHCI_FUNC_BAD_SERIAL, "a comma");
    check_serial("AB\\CD", XHCI_FUNC_BAD_SERIAL, "a backslash");
    check_serial("AB\x7F", XHCI_FUNC_BAD_SERIAL, "DEL");
    check_serial("AB\x1F", XHCI_FUNC_BAD_SERIAL, "a control character");
    check_serial("AB\t", XHCI_FUNC_BAD_SERIAL, "a tab");
    bytes = make_string(d, NULL, nonAscii, 3);
    CHECK_EQ(XhciFuncSerialId(d, bytes, out, sizeof(out)),
             XHCI_FUNC_BAD_SERIAL, "U+00E9");
    CHECK_EQ(out[0], 0, "U+00E9: nothing left behind");
    bytes = make_string(d, NULL, highByte, 2);
    CHECK_EQ(XhciFuncSerialId(d, bytes, out, sizeof(out)),
             XHCI_FUNC_BAD_SERIAL, "U+0141: a high byte");
    bytes = make_string(d, NULL, nul, 3);
    CHECK_EQ(XhciFuncSerialId(d, bytes, out, sizeof(out)),
             XHCI_FUNC_BAD_SERIAL, "an embedded NUL");

    /* Not a string descriptor. */
    bytes = make_string(d, "ABCD", NULL, 4);
    d[1] = 2;
    CHECK_EQ(XhciFuncSerialId(d, bytes, out, sizeof(out)),
             XHCI_FUNC_MALFORMED, "bDescriptorType 2");
    d[1] = 3;
    CHECK_EQ(XhciFuncSerialId(d, bytes - 1, out, sizeof(out)),
             XHCI_FUNC_MALFORMED, "bLength past the bytes read");
    CHECK_EQ(XhciFuncSerialId(d, 1, out, sizeof(out)),
             XHCI_FUNC_MALFORMED, "one byte read");
    d[0] = 1;
    CHECK_EQ(XhciFuncSerialId(d, bytes, out, sizeof(out)),
             XHCI_FUNC_MALFORMED, "bLength 1");
    d[0] = 7;
    CHECK_EQ(XhciFuncSerialId(d, bytes, out, sizeof(out)), XHCI_FUNC_OK,
             "odd bLength 7");
    CHECK_EQ(strcmp(out, "AB"), 0, "odd bLength: its last byte ignored");

    /* The longest: bLength 254 and 255 both hold 126 characters. */
    for (i = 0; i < XHCI_SERIAL_ID_CHARS; i++) {
        longest[i] = (char)('A' + (i % 26));
    }
    longest[XHCI_SERIAL_ID_CHARS] = 0;
    bytes = make_string(d, longest, NULL, XHCI_SERIAL_ID_CHARS);
    CHECK_EQ(bytes, 254, "126 characters: bLength 254");
    CHECK_EQ(XhciFuncSerialId(d, bytes, out, sizeof(out)), XHCI_FUNC_OK,
             "126 characters");
    CHECK_EQ(strcmp(out, longest), 0, "126 characters, all of them");
    d[0] = 255;
    d[254] = 'Q';
    CHECK_EQ(XhciFuncSerialId(d, 255, out, sizeof(out)), XHCI_FUNC_OK,
             "bLength 255");
    CHECK_EQ(strlen(out), XHCI_SERIAL_ID_CHARS, "bLength 255: 126");
    CHECK_EQ(XhciFuncSerialId(d, 255, out, XHCI_SERIAL_ID_BYTES - 1),
             XHCI_FUNC_BAD_PARAM, "a buffer short of 127");
    CHECK_EQ(XhciFuncSerialId(NULL, 255, out, sizeof(out)),
             XHCI_FUNC_BAD_PARAM, "no descriptor");

    /* Duplicates are found ignoring case; empty is never a duplicate. */
    CHECK_EQ(XhciFuncSerialSame("abc123", "ABC123"), 1, "case ignored");
    CHECK_EQ(XhciFuncSerialSame("ABC123", "ABC123"), 1, "equal");
    CHECK_EQ(XhciFuncSerialSame("ABC123", "ABC1234"), 0, "a prefix");
    CHECK_EQ(XhciFuncSerialSame("ABC1234", "ABC123"), 0, "longer");
    CHECK_EQ(XhciFuncSerialSame("A[", "A{"), 0, "only letters fold");
    CHECK_EQ(XhciFuncSerialSame("", ""), 0, "two empty: no serial");
    CHECK_EQ(XhciFuncSerialSame(NULL, "A"), 0, "NULL");
}

static void test_instance_ids(void)
{
    char longest[XHCI_SERIAL_ID_BYTES];
    char want[XHCI_SERIAL_ID_BYTES + 3];
    char out[8];
    ULONG used;
    ULONG i;

    /* With a serial: the device's own id is the serial alone, as usbhub's;
     * a function keeps its MI_nn after '&'. */
    check_instance("0123456789AB", 3, XHCI_INSTANCE_NO_MI, "0123456789AB",
                   "device, serial");
    check_instance("0123456789AB", 3, 0, "0123456789AB&00",
                   "function MI_00, serial");
    check_instance("0123456789AB", 3, 3, "0123456789AB&03",
                   "function MI_03, serial");
    check_instance("abc", 3, 0x1F, "abc&1F", "function MI_1F, case kept");
    check_instance("0123456789AB", 0x3102, 2, "0123456789AB&02",
                   "behind hubs: the place is not in it");

    /* Without one: section 10.7's location form, unchanged. */
    check_instance(NULL, 3, XHCI_INSTANCE_NO_MI, "3", "device, port 3");
    check_instance("", 3, XHCI_INSTANCE_NO_MI, "3", "empty serial: port 3");
    check_instance(NULL, 3, 3, "303", "function MI_03, port 3");
    check_instance(NULL, 12, 3, "1203", "function MI_03, port 12");
    check_instance(NULL, (0x31UL << 8) | 2UL, XHCI_INSTANCE_NO_MI,
                   "12546", "behind two hubs: route 0x31 over port 2");
    check_instance(NULL, (0x31UL << 8) | 2UL, 1, "1254601",
                   "a function behind two hubs");

    /* The longest serial and a function suffix: 130 with the NUL. */
    for (i = 0; i < XHCI_SERIAL_ID_CHARS; i++) {
        longest[i] = (char)('0' + (i % 10));
    }
    longest[XHCI_SERIAL_ID_CHARS] = 0;
    memcpy(want, longest, XHCI_SERIAL_ID_CHARS);
    memcpy(want + XHCI_SERIAL_ID_CHARS, "&7F", 4);
    check_instance(longest, 1, 0x7F, want, "126 characters and &7F");

    used = 0;
    CHECK_EQ(XhciFuncInstanceId("ABCDEFGH", 1, XHCI_INSTANCE_NO_MI, out,
                                sizeof(out), &used),
             XHCI_FUNC_TOO_SMALL, "nine bytes in eight");
    CHECK_EQ(used, 9, "the size it needs");
    CHECK_EQ(XhciFuncInstanceId(NULL, 1, 0x100, out, sizeof(out), &used),
             XHCI_FUNC_BAD_PARAM, "MI past 0xFF");
    CHECK_EQ(XhciFuncInstanceId(NULL, 1, 0, NULL, 4, &used),
             XHCI_FUNC_BAD_PARAM, "a capacity with no buffer");
    CHECK_EQ(XhciFuncInstanceId(NULL, 1, 0, out, sizeof(out), NULL),
             XHCI_FUNC_BAD_PARAM, "nowhere to say the size");
}

int main(void)
{
    test_cmedia();
    test_keyboard();
    test_iad();
    test_iad_audio();
    test_iad_ids();
    test_no_split();
    test_positional();
    test_malformed();
    test_setup();
    test_ids();
    test_id_edges();
    test_serial_ids();
    test_instance_ids();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
