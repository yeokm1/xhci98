/*
 * test_usbdx.c - host vectors for Windows 8's client contract served by the
 * bus, the pure half (src\xhci_usbdx.c; roadmap-usb4 task 43.29; design
 * record 13 section 6.13).
 *
 * Every expected value was worked by hand from section 6.13: the sizes from
 * its 6.13.1 table (read from usbdex and ucx01000.sys), the capability
 * answers from its 6.13.2 table, the URB sizes from usbdlib.h's macros and
 * HcdCompositeSelectDown's rule, not from the code under test. The structure
 * sizes used for the select and isochronous vectors are the DDKs', which the
 * driver half asserts at build time: on x86 USBD_INTERFACE_INFORMATION 0x24
 * (one 0x14 pipe), _URB_SELECT_CONFIGURATION's Interface at 0x18,
 * _URB_SELECT_INTERFACE's at 0x14, _URB_ISOCH_TRANSFER 0x60 with 0xC per
 * packet; on amd64 0x30 (one 0x18 pipe), 0x28, 0x20 and 0x98.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci_usbdx.h"
#include "../src/xhci_stream.h"
#include "test_harness.h"

static const UCHAR gStreams[16] = {
    0x1f, 0x1e, 0x05, 0x09, 0xc9, 0x0d, 0x6b, 0x4e,
    0x8b, 0x12, 0x96, 0x0c, 0xbd, 0x81, 0xaa, 0x8f };
static const UCHAR gSuspend[16] = {
    0x0d, 0x63, 0x5c, 0x75, 0xa0, 0xc8, 0x65, 0x47,
    0x94, 0x6f, 0x90, 0xcc, 0x70, 0x38, 0x56, 0xa4 };
static const UCHAR gChained[16] = {
    0x23, 0xeb, 0xce, 0xf5, 0x90, 0xad, 0x8c, 0x45,
    0x97, 0x9a, 0xd5, 0x9b, 0x3a, 0xd6, 0x88, 0x4f };
static const UCHAR gFunction[16] = {
    0x83, 0x31, 0x56, 0xf4, 0x6e, 0xd6, 0xbd, 0x42,
    0xbd, 0x53, 0x1a, 0xc7, 0xb0, 0x4c, 0xd5, 0x9d };
static const UCHAR gHigh[16] = {
    0xa6, 0x85, 0xa8, 0x81, 0x39, 0xf2, 0xa2, 0x42,
    0x83, 0x97, 0xa5, 0xd9, 0x5b, 0xea, 0x69, 0x53 };
static const UCHAR gSuper[16] = {
    0x6c, 0x77, 0x2f, 0x8a, 0xd0, 0x9b, 0x29, 0x4f,
    0x97, 0x71, 0xc7, 0xa3, 0x88, 0x46, 0xd4, 0xc7 };
static const UCHAR gFull[16] = {
    0x91, 0x31, 0xe4, 0xcd, 0xad, 0x1f, 0x20, 0x41,
    0xa7, 0xab, 0x74, 0x09, 0x56, 0xc6, 0x6f, 0x37 };
static const UCHAR gLow[16] = {
    0x13, 0x2a, 0x6f, 0xad, 0xd1, 0xc4, 0x25, 0x49,
    0x83, 0xcc, 0x96, 0x85, 0x7a, 0x24, 0x31, 0xd9 };
static const UCHAR gClearTt[16] = {
    0x52, 0x6d, 0xb7, 0x09, 0x6d, 0x0a, 0x4f, 0x4e,
    0xa9, 0x11, 0xd0, 0x36, 0xd1, 0x92, 0x94, 0x97 };
/* GUID_USBD_INTERFACE_V1: an interface, not a capability. */
static const UCHAR gV1[16] = {
    0xea, 0x98, 0xee, 0xde, 0xa1, 0xc0, 0xc3, 0x42,
    0x97, 0x38, 0xa0, 0x46, 0x06, 0xc8, 0x4e, 0x93 };

static void test_sizes(void)
{
    CHECK_EQ(XhciUsbdxV1Bytes(4), 0x58, "V1 x86");
    CHECK_EQ(XhciUsbdxV1Bytes(8), 0x98, "V1 x64");
    CHECK_EQ(XhciUsbdxV1Bytes(2), 0, "V1 other width");
    CHECK_EQ(XhciUsbdxCapBlockBytes(4), 0x20, "cap block x86");
    CHECK_EQ(XhciUsbdxCapBlockBytes(8), 0x28, "cap block x64");
    CHECK_EQ(XhciUsbdxCapBlockBytes(16), 0, "cap block other width");
}

static void test_v1_check(void)
{
    CHECK_EQ(XhciUsbdxV1Check(0x58, 0x602, 4, 1, 0x70736175UL),
             XHCI_USBDX_OK, "x86 answered");
    CHECK_EQ(XhciUsbdxV1Check(0x98, 0x602, 8, 1, 0x70736175UL),
             XHCI_USBDX_OK, "x64 answered");
    CHECK_EQ(XhciUsbdxV1Check(0x98, 0x602, 4, 1, 1),
             XHCI_USBDX_NOT_OURS, "x64 size on x86");
    CHECK_EQ(XhciUsbdxV1Check(0x58, 0x602, 8, 1, 1),
             XHCI_USBDX_NOT_OURS, "x86 size on x64");
    CHECK_EQ(XhciUsbdxV1Check(0x57, 0x602, 4, 1, 1),
             XHCI_USBDX_NOT_OURS, "short");
    CHECK_EQ(XhciUsbdxV1Check(0x58, 0x600, 4, 1, 1),
             XHCI_USBDX_NOT_OURS, "0x600");
    CHECK_EQ(XhciUsbdxV1Check(0x58, 0x603, 4, 1, 1),
             XHCI_USBDX_NOT_OURS, "0x603");
    CHECK_EQ(XhciUsbdxV1Check(0x58, 0x602, 4, 0, 1),
             XHCI_USBDX_INVALID, "no device object");
    CHECK_EQ(XhciUsbdxV1Check(0x58, 0x602, 4, 1, 0),
             XHCI_USBDX_INVALID, "no pool tag");
    /* The size and version are judged first: a foreign query is never
     * refused as invalid. */
    CHECK_EQ(XhciUsbdxV1Check(0x40, 0x602, 4, 0, 0),
             XHCI_USBDX_NOT_OURS, "foreign before invalid");
}

static void test_block_check(void)
{
    CHECK_EQ(XhciUsbdxCapBlockCheck(1, 0x20, 4, 1, 2, 1), XHCI_USBDX_OK,
             "x86 streams block");
    CHECK_EQ(XhciUsbdxCapBlockCheck(1, 0x28, 8, 1, 0, 0), XHCI_USBDX_OK,
             "x64 no output");
    CHECK_EQ(XhciUsbdxCapBlockCheck(2, 0x28, 8, 1, 0, 0), XHCI_USBDX_INVALID,
             "version 2");
    CHECK_EQ(XhciUsbdxCapBlockCheck(1, 0x20, 8, 1, 0, 0), XHCI_USBDX_INVALID,
             "x86 size on x64");
    CHECK_EQ(XhciUsbdxCapBlockCheck(1, 0x28, 8, 0, 0, 0), XHCI_USBDX_INVALID,
             "no context");
    CHECK_EQ(XhciUsbdxCapBlockCheck(1, 0x28, 8, 1, 2, 0), XHCI_USBDX_INVALID,
             "length without buffer");
    CHECK_EQ(XhciUsbdxCapBlockCheck(1, 0x28, 8, 1, 0, 1), XHCI_USBDX_INVALID,
             "buffer without length");
}

static void test_ids(void)
{
    UCHAR g[16];

    CHECK_EQ(XhciUsbdxCapabilityId(gSuspend),
             XHCI_USBDX_CAP_SELECTIVE_SUSPEND, "selective suspend");
    CHECK_EQ(XhciUsbdxCapabilityId(gChained), XHCI_USBDX_CAP_CHAINED_MDLS,
             "chained MDLs");
    CHECK_EQ(XhciUsbdxCapabilityId(gStreams), XHCI_USBDX_CAP_STATIC_STREAMS,
             "static streams");
    CHECK_EQ(XhciUsbdxCapabilityId(gFunction),
             XHCI_USBDX_CAP_FUNCTION_SUSPEND, "function suspend");
    CHECK_EQ(XhciUsbdxCapabilityId(gHigh), XHCI_USBDX_CAP_HIGH_SPEED,
             "high speed");
    CHECK_EQ(XhciUsbdxCapabilityId(gSuper), XHCI_USBDX_CAP_SUPER_SPEED,
             "super speed");
    CHECK_EQ(XhciUsbdxCapabilityId(gFull), XHCI_USBDX_CAP_FULL_SPEED,
             "full speed");
    CHECK_EQ(XhciUsbdxCapabilityId(gLow), XHCI_USBDX_CAP_LOW_SPEED,
             "low speed");
    CHECK_EQ(XhciUsbdxCapabilityId(gClearTt), XHCI_USBDX_CAP_CLEAR_TT,
             "clear TT");
    CHECK_EQ(XhciUsbdxCapabilityId(gV1), XHCI_USBDX_CAP_UNKNOWN,
             "the interface GUID is no capability");
    CHECK_EQ(XhciUsbdxCapabilityId(NULL), XHCI_USBDX_CAP_UNKNOWN, "NULL");
    /* One byte off, at each end. */
    memcpy(g, gStreams, 16);
    g[15] ^= 1;
    CHECK_EQ(XhciUsbdxCapabilityId(g), XHCI_USBDX_CAP_UNKNOWN, "last byte");
    memcpy(g, gStreams, 16);
    g[0] ^= 0x80;
    CHECK_EQ(XhciUsbdxCapabilityId(g), XHCI_USBDX_CAP_UNKNOWN, "first byte");
}

static void cap(ULONG id, ULONG speed, ULONG max, ULONG len, ULONG want,
                ULONG wantCount, ULONG wantBytes, const char *what)
{
    ULONG count;
    ULONG bytes;

    count = 0xDEAD;
    bytes = 0xBEEF;
    check_eq_impl(XhciUsbdxCapability(id, speed, max, len, &count, &bytes),
                  want, what, __FILE__, __LINE__);
    check_eq_impl(count, wantCount, what, __FILE__, __LINE__);
    check_eq_impl(bytes, wantBytes, what, __FILE__, __LINE__);
}

static void test_capability(void)
{
    ULONG s;

    /* Selective suspend: every speed, no output. */
    for (s = XHCI_SPEED_LOW; s <= XHCI_SPEED_SUPER + 1; s++) {
        cap(XHCI_USBDX_CAP_SELECTIVE_SUSPEND, s, 31, 0, XHCI_USBDX_OK, 0, 0,
            "suspend");
    }
    cap(XHCI_USBDX_CAP_SELECTIVE_SUSPEND, XHCI_SPEED_HIGH, 31, 2,
        XHCI_USBDX_INVALID, 0, 0, "suspend with output");

    /* High Speed compatible: High Speed and faster. */
    cap(XHCI_USBDX_CAP_HIGH_SPEED, XHCI_SPEED_LOW, 0, 0,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "HS at LS");
    cap(XHCI_USBDX_CAP_HIGH_SPEED, XHCI_SPEED_FULL, 0, 0,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "HS at FS");
    cap(XHCI_USBDX_CAP_HIGH_SPEED, XHCI_SPEED_HIGH, 0, 0, XHCI_USBDX_OK, 0,
        0, "HS at HS");
    cap(XHCI_USBDX_CAP_HIGH_SPEED, XHCI_SPEED_SUPER, 0, 0, XHCI_USBDX_OK, 0,
        0, "HS at SS");
    cap(XHCI_USBDX_CAP_HIGH_SPEED, XHCI_SPEED_SUPER + 1, 0, 0, XHCI_USBDX_OK,
        0, 0, "HS at SSP");

    /* SuperSpeed compatible: SuperSpeed and faster. */
    cap(XHCI_USBDX_CAP_SUPER_SPEED, XHCI_SPEED_HIGH, 31, 0,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "SS at HS");
    cap(XHCI_USBDX_CAP_SUPER_SPEED, XHCI_SPEED_SUPER, 31, 0, XHCI_USBDX_OK,
        0, 0, "SS at SS");
    cap(XHCI_USBDX_CAP_SUPER_SPEED, XHCI_SPEED_SUPER + 1, 31, 0,
        XHCI_USBDX_OK, 0, 0, "SS at SSP");

    /* Static streams: a USHORT, SuperSpeed on a streaming controller. */
    cap(XHCI_USBDX_CAP_STATIC_STREAMS, XHCI_SPEED_SUPER, 31, 2,
        XHCI_USBDX_OK, 31, 2, "streams SS 31");
    cap(XHCI_USBDX_CAP_STATIC_STREAMS, XHCI_SPEED_SUPER + 1, 3, 2,
        XHCI_USBDX_OK, 3, 2, "streams SSP 3");
    cap(XHCI_USBDX_CAP_STATIC_STREAMS, XHCI_SPEED_HIGH, 31, 2,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "streams at HS");
    cap(XHCI_USBDX_CAP_STATIC_STREAMS, XHCI_SPEED_SUPER, 0, 2,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "streams, streamless controller");
    cap(XHCI_USBDX_CAP_STATIC_STREAMS, XHCI_SPEED_SUPER, 31, 4,
        XHCI_USBDX_INVALID, 0, 0, "streams, a ULONG buffer");
    cap(XHCI_USBDX_CAP_STATIC_STREAMS, XHCI_SPEED_SUPER, 31, 0,
        XHCI_USBDX_INVALID, 0, 0, "streams, no buffer");
    /* The length is judged before the speed. */
    cap(XHCI_USBDX_CAP_STATIC_STREAMS, XHCI_SPEED_HIGH, 0, 1,
        XHCI_USBDX_INVALID, 0, 0, "streams, a byte");

    /* Refused whatever the speed. */
    cap(XHCI_USBDX_CAP_CHAINED_MDLS, XHCI_SPEED_SUPER, 31, 0,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "chained MDLs");
    cap(XHCI_USBDX_CAP_FUNCTION_SUSPEND, XHCI_SPEED_SUPER, 31, 0,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "function suspend");
    cap(XHCI_USBDX_CAP_FULL_SPEED, XHCI_SPEED_FULL, 31, 0,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "full speed");
    cap(XHCI_USBDX_CAP_LOW_SPEED, XHCI_SPEED_LOW, 31, 0,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "low speed");
    cap(XHCI_USBDX_CAP_CLEAR_TT, XHCI_SPEED_HIGH, 31, 0,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "clear TT");
    cap(XHCI_USBDX_CAP_UNKNOWN, XHCI_SPEED_SUPER, 31, 0,
        XHCI_USBDX_NOT_SUPPORTED, 0, 0, "unknown");
    cap(XHCI_USBDX_CAP_UNKNOWN, XHCI_SPEED_SUPER, 31, 8,
        XHCI_USBDX_INVALID, 0, 0, "unknown with output");
}

static void test_max_streams(void)
{
    CHECK_EQ(XhciUsbdxMaxStreams(0), 0, "no array");
    CHECK_EQ(XhciUsbdxMaxStreams(2), 0, "below the smallest array");
    CHECK_EQ(XhciUsbdxMaxStreams(4), 3, "4 entries");
    CHECK_EQ(XhciUsbdxMaxStreams(8), 7, "8 entries");
    CHECK_EQ(XhciUsbdxMaxStreams(16), 15, "16 entries");
    CHECK_EQ(XhciUsbdxMaxStreams(32), 31, "32 entries");
    CHECK_EQ(XhciUsbdxMaxStreams(65536), 31, "the largest array");
}

static void test_isoch(void)
{
    ULONG b;

    b = 0;
    CHECK_EQ(XhciUsbdxIsochBytes(1, 0x60, 0xC, &b), XHCI_USBDX_OK, "x86 1");
    CHECK_EQ(b, 0x6C, "x86 1 bytes");
    CHECK_EQ(XhciUsbdxIsochBytes(8, 0x98, 0xC, &b), XHCI_USBDX_OK, "x64 8");
    CHECK_EQ(b, 0x98 + 8 * 0xC, "x64 8 bytes");
    /* The largest that fits a USHORT Length: (0xFFFF - 0x60) / 0xC. */
    CHECK_EQ(XhciUsbdxIsochBytes(5453, 0x60, 0xC, &b), XHCI_USBDX_OK,
             "x86 most");
    CHECK_EQ(b, 0xFFFC, "x86 most bytes");
    b = 7;
    CHECK_EQ(XhciUsbdxIsochBytes(5454, 0x60, 0xC, &b), XHCI_USBDX_INVALID,
             "x86 one too many");
    CHECK_EQ(b, 7, "nothing written on a refusal");
    CHECK_EQ(XhciUsbdxIsochBytes(0, 0x60, 0xC, &b), XHCI_USBDX_INVALID,
             "no packets");
    CHECK_EQ(XhciUsbdxIsochBytes(0xFFFFFFFFUL, 0x98, 0xC, &b),
             XHCI_USBDX_INVALID, "a count that would wrap");
}

static void test_select(void)
{
    ULONG pipes[33];
    ULONG off[33];
    ULONG total;
    ULONG i;

    /* A UAS bridge's one interface of four pipes, x86: header 0x18, one
     * USBD_INTERFACE_INFORMATION 0x24 plus three pipes of 0x14. */
    pipes[0] = 4;
    total = 0;
    CHECK_EQ(XhciUsbdxSelectLayout(pipes, 1, 0x18, 0x24, 0x14, off, &total),
             XHCI_USBDX_OK, "x86 one interface");
    CHECK_EQ(off[0], 0x18, "x86 first offset");
    CHECK_EQ(total, 0x18 + 0x24 + 3 * 0x14, "x86 total");

    /* An audio device on amd64: control (one interrupt pipe), streaming
     * (zero pipes at alternate 0), a second streaming (two pipes). */
    pipes[0] = 1;
    pipes[1] = 0;
    pipes[2] = 2;
    CHECK_EQ(XhciUsbdxSelectLayout(pipes, 3, 0x28, 0x30, 0x18, off, &total),
             XHCI_USBDX_OK, "x64 three interfaces");
    CHECK_EQ(off[0], 0x28, "x64 offset 0");
    CHECK_EQ(off[1], 0x58, "x64 offset 1");
    CHECK_EQ(off[2], 0x88, "x64 offset 2 (no pipes: a whole entry)");
    CHECK_EQ(total, 0xD0, "x64 total");

    /* SELECT_INTERFACE's header: one interface, no pipes. */
    pipes[0] = 0;
    CHECK_EQ(XhciUsbdxSelectLayout(pipes, 1, 0x20, 0x30, 0x18, off, &total),
             XHCI_USBDX_OK, "x64 select interface");
    CHECK_EQ(total, 0x50, "x64 select interface total");
    CHECK_EQ(XhciUsbdxInterfaceBytes(0, 0x24, 0x14), 0x24, "0 pipes");
    CHECK_EQ(XhciUsbdxInterfaceBytes(1, 0x24, 0x14), 0x24, "1 pipe");
    CHECK_EQ(XhciUsbdxInterfaceBytes(30, 0x24, 0x14), 0x24 + 29 * 0x14,
             "30 pipes");

    /* The bounds. */
    for (i = 0; i < 33; i++) {
        pipes[i] = 30;
    }
    CHECK_EQ(XhciUsbdxSelectLayout(pipes, 32, 0x28, 0x30, 0x18, off, &total),
             XHCI_USBDX_OK, "32 interfaces of 30 pipes");
    CHECK_EQ(total, 0x28 + 32 * (0x30 + 29 * 0x18), "32 x 30 total");
    CHECK_EQ(off[31], 0x28 + 31 * (0x30 + 29 * 0x18), "32 x 30 last");
    CHECK_EQ(XhciUsbdxSelectLayout(pipes, 33, 0x28, 0x30, 0x18, off, &total),
             XHCI_USBDX_INVALID, "33 interfaces");
    CHECK_EQ(XhciUsbdxSelectLayout(pipes, 0, 0x28, 0x30, 0x18, off, &total),
             XHCI_USBDX_INVALID, "no interface");
    pipes[0] = 31;
    CHECK_EQ(XhciUsbdxSelectLayout(pipes, 1, 0x28, 0x30, 0x18, off, &total),
             XHCI_USBDX_INVALID, "31 pipes");
    /* Above a USHORT: no real structure is this large, so a pipe size of
     * 0x800 stands in to cross the bound at the second interface. */
    pipes[0] = 30;
    pipes[1] = 30;
    total = 5;
    CHECK_EQ(XhciUsbdxSelectLayout(pipes, 2, 0x28, 0x30, 0x800, off, &total),
             XHCI_USBDX_INVALID, "past 0xFFFF");
    CHECK_EQ(total, 5, "nothing written on a refusal");
}

static ULONG get16(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

static ULONG get32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static void test_fill(void)
{
    UCHAR urb[0x200];
    ULONG pipes[3];
    UCHAR numbers[3];
    UCHAR alts[3];
    ULONG off[3];
    ULONG bytes;
    ULONG i;

    /* x86, SELECT_CONFIGURATION, one UAS interface (number 0, alternate 1)
     * of four pipes: 0x18 + 0x24 + 3 * 0x14 = 0x78. */
    pipes[0] = 4;
    numbers[0] = 0;
    alts[0] = 1;
    CHECK_EQ(XhciUsbdxSelectBytes(XHCI_USBDX_URB_SELECT_CONFIG, 4, pipes, 1,
                                  &bytes), XHCI_USBDX_OK, "x86 cfg bytes");
    CHECK_EQ(bytes, 0x78, "x86 cfg size");
    memset(urb, 0, sizeof(urb));
    CHECK_EQ(XhciUsbdxSelectFill(urb, 0x78, XHCI_USBDX_URB_SELECT_CONFIG, 4,
                                 pipes, numbers, alts, 1, 0x1000, off),
             XHCI_USBDX_OK, "x86 cfg fill");
    CHECK_EQ(get16(urb), 0x78, "x86 cfg Hdr.Length");
    CHECK_EQ(get16(urb + 2), 0x0000, "x86 cfg Hdr.Function");
    CHECK_EQ(off[0], 0x18, "x86 cfg Interface");
    CHECK_EQ(get16(urb + 0x18), 0x60, "x86 cfg Interface.Length");
    CHECK_EQ(urb[0x1A], 0, "x86 cfg InterfaceNumber");
    CHECK_EQ(urb[0x1B], 1, "x86 cfg AlternateSetting");
    CHECK_EQ(get32(urb + 0x24), 4, "x86 cfg NumberOfPipes");
    for (i = 0; i < 4; i++) {
        CHECK_EQ(get32(urb + 0x28 + i * 0x14 + 0x0C), 0x1000,
                 "x86 cfg MaximumTransferSize");
        CHECK_EQ(get32(urb + 0x28 + i * 0x14 + 0x10), 0,
                 "x86 cfg PipeFlags left 0");
    }
    CHECK_EQ(get32(urb + 0x1C), 0, "x86 cfg InterfaceHandle left 0");
    CHECK_EQ(get32(urb + 0x10), 0, "x86 cfg ConfigurationDescriptor left 0");

    /* amd64, SELECT_CONFIGURATION, two interfaces: number 0 with no pipes
     * (a whole entry), number 1 alternate 2 with one pipe. */
    pipes[0] = 0;
    pipes[1] = 1;
    numbers[0] = 0;
    numbers[1] = 1;
    alts[0] = 0;
    alts[1] = 2;
    CHECK_EQ(XhciUsbdxSelectBytes(XHCI_USBDX_URB_SELECT_CONFIG, 8, pipes, 2,
                                  &bytes), XHCI_USBDX_OK, "x64 cfg bytes");
    CHECK_EQ(bytes, 0x28 + 0x30 + 0x30, "x64 cfg size");
    memset(urb, 0xAA, sizeof(urb));
    memset(urb, 0, bytes);
    CHECK_EQ(XhciUsbdxSelectFill(urb, bytes, XHCI_USBDX_URB_SELECT_CONFIG, 8,
                                 pipes, numbers, alts, 2, 0xFFFFFFFFUL, off),
             XHCI_USBDX_OK, "x64 cfg fill");
    CHECK_EQ(off[1], 0x58, "x64 cfg second Interface");
    CHECK_EQ(get16(urb + 0x28), 0x30, "x64 cfg first Length");
    CHECK_EQ(get32(urb + 0x28 + 0x10), 0, "x64 cfg first NumberOfPipes");
    CHECK_EQ(get32(urb + 0x28 + 0x18 + 0x10), 0,
             "x64 cfg no pipe: MaximumTransferSize left 0");
    CHECK_EQ(get16(urb + 0x58), 0x30, "x64 cfg second Length");
    CHECK_EQ(urb[0x5A], 1, "x64 cfg second InterfaceNumber");
    CHECK_EQ(urb[0x5B], 2, "x64 cfg second AlternateSetting");
    CHECK_EQ(get32(urb + 0x58 + 0x10), 1, "x64 cfg second NumberOfPipes");
    CHECK_EQ(get32(urb + 0x58 + 0x18 + 0x10), 0xFFFFFFFFUL,
             "x64 cfg second MaximumTransferSize");
    CHECK_EQ(urb[bytes], 0xAA, "x64 cfg nothing past the URB");

    /* amd64, SELECT_INTERFACE, interface 3 alternate 1, two pipes:
     * 0x20 + 0x30 + 0x18 = 0x68. */
    pipes[0] = 2;
    numbers[0] = 3;
    alts[0] = 1;
    memset(urb, 0, sizeof(urb));
    CHECK_EQ(XhciUsbdxSelectFill(urb, 0x68, XHCI_USBDX_URB_SELECT_IFACE, 8,
                                 pipes, numbers, alts, 1, 0x1000, off),
             XHCI_USBDX_OK, "x64 if fill");
    CHECK_EQ(get16(urb), 0x68, "x64 if Hdr.Length");
    CHECK_EQ(get16(urb + 2), 0x0001, "x64 if Hdr.Function");
    CHECK_EQ(off[0], 0x20, "x64 if Interface");
    CHECK_EQ(get16(urb + 0x20), 0x48, "x64 if Interface.Length");
    CHECK_EQ(urb[0x22], 3, "x64 if InterfaceNumber");
    CHECK_EQ(urb[0x23], 1, "x64 if AlternateSetting");
    CHECK_EQ(get32(urb + 0x30), 2, "x64 if NumberOfPipes");
    CHECK_EQ(get32(urb + 0x48), 0x1000, "x64 if pipe 0");
    CHECK_EQ(get32(urb + 0x60), 0x1000, "x64 if pipe 1");

    /* x86 SELECT_INTERFACE: 0x14 + 0x24 = 0x38 for one pipe. */
    pipes[0] = 1;
    CHECK_EQ(XhciUsbdxSelectBytes(XHCI_USBDX_URB_SELECT_IFACE, 4, pipes, 1,
                                  &bytes), XHCI_USBDX_OK, "x86 if bytes");
    CHECK_EQ(bytes, 0x38, "x86 if size");

    /* Refusals, with nothing written. */
    memset(urb, 0x55, sizeof(urb));
    CHECK_EQ(XhciUsbdxSelectFill(urb, 0x67, XHCI_USBDX_URB_SELECT_IFACE, 8,
                                 pipes, numbers, alts, 1, 0x1000, off),
             XHCI_USBDX_INVALID, "a size that is not the total");
    pipes[1] = 1;
    CHECK_EQ(XhciUsbdxSelectBytes(XHCI_USBDX_URB_SELECT_IFACE, 8, pipes, 2,
                                  &bytes), XHCI_USBDX_INVALID,
             "select-interface of two");
    CHECK_EQ(XhciUsbdxSelectBytes(2, 8, pipes, 1, &bytes),
             XHCI_USBDX_INVALID, "another function");
    CHECK_EQ(XhciUsbdxSelectBytes(XHCI_USBDX_URB_SELECT_CONFIG, 2, pipes, 1,
                                  &bytes), XHCI_USBDX_INVALID,
             "another width");
    CHECK_EQ(urb[0], 0x55, "untouched");
}

static void test_open(void)
{
    CHECK_EQ(XhciUsbdxOpenCheck(0x20, 0x20, 0x100, 0x10, 0x10, 31, 1),
             XHCI_USBDX_USBD_SUCCESS, "x86 31");
    CHECK_EQ(XhciUsbdxOpenCheck(0x30, 0x30, 0x100, 0x18, 0x18, 1, 1),
             XHCI_USBDX_USBD_SUCCESS, "x64 1");
    CHECK_EQ(XhciUsbdxOpenCheck(0x40, 0x30, 0x100, 0x18, 0x18, 1, 1),
             XHCI_USBDX_USBD_SUCCESS, "a longer URB");
    CHECK_EQ(XhciUsbdxOpenCheck(0x2F, 0x30, 0x100, 0x18, 0x18, 1, 1),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "short");
    CHECK_EQ(XhciUsbdxOpenCheck(0x30, 0x30, 0x101, 0x18, 0x18, 1, 1),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "version");
    CHECK_EQ(XhciUsbdxOpenCheck(0x30, 0x30, 0x100, 0x10, 0x18, 1, 1),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "x86 record on x64");
    CHECK_EQ(XhciUsbdxOpenCheck(0x30, 0x30, 0x100, 0x18, 0x18, 0, 1),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "no streams");
    CHECK_EQ(XhciUsbdxOpenCheck(0x30, 0x30, 0x100, 0x18, 0x18, 32, 1),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "32 streams");
    CHECK_EQ(XhciUsbdxOpenCheck(0x30, 0x30, 0x100, 0x18, 0x18, 4, 0),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "no array");
}

static void test_close(void)
{
    CHECK_EQ(XhciUsbdxCloseCheck(0x18, 0x18), XHCI_USBDX_USBD_SUCCESS,
             "x86 exact");
    CHECK_EQ(XhciUsbdxCloseCheck(0x17, 0x18),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "x86 short");
    CHECK_EQ(XhciUsbdxCloseCheck(0x28, 0x28), XHCI_USBDX_USBD_SUCCESS,
             "x64 exact");
    CHECK_EQ(XhciUsbdxCloseCheck(0x18, 0x28),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "x86 length on x64");
    CHECK_EQ(XhciUsbdxCloseCheck(0x98, 0x28), XHCI_USBDX_USBD_SUCCESS,
             "a whole URB");
}

/* The grant over XhciStreamPlan's own answers: an endpoint whose companion
 * says 2^4 = 16 streams on a controller whose array takes 32 entries. */
static void test_grant(void)
{
    XHCI_STREAM_PLAN plan;

    CHECK_EQ(XhciStreamPlan(31, 4, 32, 32, &plan), XHCI_STREAM_OK,
             "plan 31 of 16");
    CHECK_EQ(plan.Streams, 16, "plan grants 16");
    CHECK_EQ(XhciUsbdxGrantCheck(31, plan.Streams, 1), XHCI_USBDX_INVALID,
             "public: 31 asked, 16 planned, refused");
    CHECK_EQ(XhciUsbdxGrantCheck(31, plan.Streams, 0), XHCI_USBDX_OK,
             "private: 31 asked, 16 granted");

    CHECK_EQ(XhciStreamPlan(16, 4, 32, 32, &plan), XHCI_STREAM_OK,
             "plan 16 of 16");
    CHECK_EQ(plan.Streams, 16, "plan grants 16 of 16");
    CHECK_EQ(XhciUsbdxGrantCheck(16, plan.Streams, 1), XHCI_USBDX_OK,
             "public: exact");

    /* uaspstor's count is min(endpoint, capability): with the capability
     * 31 (32 entries) and an endpoint of 2^5, it asks 31, and gets them. */
    CHECK_EQ(XhciStreamPlan(XhciUsbdxMaxStreams(32), 5, 32, 32, &plan),
             XHCI_STREAM_OK, "plan 31 of 32");
    CHECK_EQ(XhciUsbdxGrantCheck(31, plan.Streams, 1), XHCI_USBDX_OK,
             "public: the capability's count is granted whole");

    /* A controller of 4 entries: the capability says 3, and 3 are granted
     * on an endpoint of 2^16. */
    CHECK_EQ(XhciStreamPlan(XhciUsbdxMaxStreams(4), 16, 4, 32, &plan),
             XHCI_STREAM_OK, "plan 3 of 4 entries");
    CHECK_EQ(XhciUsbdxGrantCheck(3, plan.Streams, 1), XHCI_USBDX_OK,
             "public: 3 of 3");

    CHECK_EQ(XhciUsbdxGrantCheck(4, 0, 0), XHCI_USBDX_INVALID, "none");
    CHECK_EQ(XhciUsbdxGrantCheck(4, 5, 0), XHCI_USBDX_INVALID, "more");
}

static void test_answers(void)
{
    CHECK_EQ(XhciUsbdxStreamsUsbd(XHCI_USBDX_SA_SUCCESS, 0),
             XHCI_USBDX_USBD_SUCCESS, "success");
    CHECK_EQ(XhciUsbdxStreamsUsbd(XHCI_USBDX_SA_NO_CONTROLLER, 0),
             XHCI_USBDX_USBD_NOT_SUPPORTED, "no controller");
    CHECK_EQ(XhciUsbdxStreamsUsbd(XHCI_USBDX_SA_NO_ENDPOINT, 0),
             XHCI_USBDX_USBD_NOT_SUPPORTED, "no endpoint");
    CHECK_EQ(XhciUsbdxStreamsUsbd(XHCI_USBDX_SA_INVALID_PIPE, 0),
             XHCI_USBDX_USBD_INVALID_PIPE, "invalid pipe");
    CHECK_EQ(XhciUsbdxStreamsUsbd(XHCI_USBDX_SA_INVALID_REQUEST, 0),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "invalid request");
    CHECK_EQ(XhciUsbdxStreamsUsbd(XHCI_USBDX_SA_ALREADY_OPEN, 0),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "already open");
    CHECK_EQ(XhciUsbdxStreamsUsbd(XHCI_USBDX_SA_NOT_OPEN, 0),
             XHCI_USBDX_USBD_INVALID_PARAMETER, "not open");
    CHECK_EQ(XhciUsbdxStreamsUsbd(XHCI_USBDX_SA_NO_RESOURCES, 0),
             XHCI_USBDX_USBD_NO_RESOURCES, "no resources");
    CHECK_EQ(XhciUsbdxStreamsUsbd(XHCI_USBDX_SA_FAILED, 0),
             XHCI_USBDX_USBD_INTERNAL_HC_ERROR, "failed");
    CHECK_EQ(XhciUsbdxStreamsUsbd(XHCI_USBDX_SA_FAILED, 1),
             XHCI_USBDX_USBD_DEVICE_GONE, "failed, gone");
    CHECK_EQ(XhciUsbdxStreamsUsbd(99, 0), XHCI_USBDX_USBD_INVALID_PARAMETER,
             "an unknown answer");
}

static void test_alias(void)
{
    CHECK_EQ(XhciUsbdxIoctlAlias(0x00220FB3UL), 0x00220007UL, "0x220FB3");
    CHECK_EQ(XhciUsbdxIoctlAlias(0x00220007UL), 0x00220007UL, "RESET_PORT");
    CHECK_EQ(XhciUsbdxIoctlAlias(0x0049104BUL), 0x0049104BUL, "capability");
    CHECK_EQ(XhciUsbdxIoctlAlias(0x00220FB7UL), 0x00220FB7UL, "a neighbour");
}

int main(void)
{
    test_sizes();
    test_v1_check();
    test_block_check();
    test_ids();
    test_capability();
    test_max_streams();
    test_isoch();
    test_select();
    test_fill();
    test_open();
    test_close();
    test_grant();
    test_answers();
    test_alias();
    printf("test_usbdx: %d checks, %d failures\n", checks, failures);
    return failures;
}
