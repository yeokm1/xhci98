/*
 * test_pipe.c - host vectors for the pure half of URB dispatch
 * (src\xhci_pipe.c; roadmap-hcd.md task 26-A.5).
 *
 * Every expected value below was worked from the specification text, not
 * from the code under test: the Endpoint Context rules from xHCI 1.2c
 * sections 4.5.1, 4.6.6, 4.14.1.1, 4.14.2 and 6.2.3 (Tables 6-9, 6-11,
 * 6-12), the SETUP bytes from USB 2.0 chapter 9 and the Windows 2000 DDK's
 * usbdi.h / usb100.h constants, and the status values from that DDK's
 * usbdi.h and ntstatus.h.
 */

#include <stdio.h>
#include "../src/xhci_pipe.h"
#include "test_harness.h"

/* ------------------------------------------------------------------ */
/* Endpoint parameters                                                  */
/* ------------------------------------------------------------------ */

static ULONG ep_params(ULONG address, ULONG attributes, ULONG wMaxPacketSize,
                       ULONG bInterval, ULONG speed, PXHCI_PIPE_EP ep)
{
    UCHAR d[7];

    d[0] = 7;
    d[1] = 5;
    d[2] = (UCHAR)address;
    d[3] = (UCHAR)attributes;
    d[4] = (UCHAR)(wMaxPacketSize & 0xFF);
    d[5] = (UCHAR)(wMaxPacketSize >> 8);
    d[6] = (UCHAR)bInterval;
    return XhciPipeEndpointParams(d, speed, ep);
}

/* The HS interrupt Interval for each bInterval: Table 6-12, bInterval - 1. */
static ULONG hs_interval(ULONG bInterval)
{
    XHCI_PIPE_EP ep;

    ep_params(0x81, 3, 8, bInterval, XHCI_PIPE_SPEED_HIGH, &ep);
    return ep.Interval;
}

static ULONG fs_interval(ULONG bInterval)
{
    XHCI_PIPE_EP ep;

    ep_params(0x81, 3, 8, bInterval, XHCI_PIPE_SPEED_FULL, &ep);
    return ep.Interval;
}

static void test_dci(void)
{
    /* 4.5.1: EP0 = 1 (not this function's), n OUT = 2n, n IN = 2n + 1. */
    CHECK_EQ(XhciPipeDci(0x81), 3, "0x81 is DCI 3");
    CHECK_EQ(XhciPipeDci(0x02), 4, "0x02 is DCI 4");
    CHECK_EQ(XhciPipeDci(0x01), 2, "0x01 is DCI 2");
    CHECK_EQ(XhciPipeDci(0x8F), 31, "0x8F is DCI 31");
    CHECK_EQ(XhciPipeDci(0x0F), 30, "0x0F is DCI 30");
    CHECK_EQ(XhciPipeDci(0x00), 0, "endpoint 0 is no DCI here");
    CHECK_EQ(XhciPipeDci(0x80), 0, "nor is 0x80");
}

static void test_qemu_mouse(void)
{
    XHCI_PIPE_EP ep;

    /* QEMU's HS mouse: 0x81 interrupt IN, 4 bytes, bInterval 7 (8 ms). */
    CHECK_EQ(ep_params(0x81, 0x03, 0x0004, 7, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_OK, "the mouse endpoint is accepted");
    CHECK_EQ(ep.Dci, 3, "mouse DCI 3");
    CHECK_EQ(ep.EpType, 7, "mouse EP Type 7, Interrupt IN (Table 6-9)");
    CHECK_EQ(ep.Interval, 6, "HS Interval = bInterval - 1 (Table 6-12)");
    CHECK_EQ(ep.IntervalClamped, 0, "no clamp");
    CHECK_EQ(ep.MaxPacketSize, 4, "mouse MPS 4");
    CHECK_EQ(ep.MaxBurstSize, 0, "no high-bandwidth bits");
    CHECK_EQ(ep.Mult, 0, "Mult 0 for USB 2.0 (Table 6-8)");
    CHECK_EQ(ep.ErrorCount, 3, "CErr 3 (Table 6-9 footnote 112)");
    CHECK_EQ(ep.AverageTrbLength, 1024, "interrupt Average TRB 1 KB (4.14.1.1)");
    CHECK_EQ(ep.MaxEsitPayload, 4, "ESIT = MPS * (burst + 1) (4.14.2)");
    CHECK_EQ(ep.DirectionIn, 1, "IN");
    CHECK_EQ(ep.TransferType, XHCI_PIPE_XFER_INTERRUPT, "interrupt");
    CHECK_EQ(ep.BInterval, 7, "bInterval kept as given");
}

static void test_intervals(void)
{
    XHCI_PIPE_EP ep;

    /* FS/LS interrupt: bInterval ms, rounded down to a power of two in
     * 125 us units (footnote 113): 10 ms = 80 uframes -> 64 = 2^6. */
    CHECK_EQ(ep_params(0x81, 3, 8, 10, XHCI_PIPE_SPEED_FULL, &ep),
             XHCI_PIPE_OK, "FS interrupt accepted");
    CHECK_EQ(ep.Interval, 6, "FS bInterval 10 -> Interval 6");
    CHECK_EQ(ep_params(0x81, 3, 8, 10, XHCI_PIPE_SPEED_LOW, &ep),
             XHCI_PIPE_OK, "LS interrupt accepted");
    CHECK_EQ(ep.Interval, 6, "LS bInterval 10 -> Interval 6");
    CHECK_EQ(ep.MaxBurstSize, 0, "LS burst 0 (6.2.3.4)");

    CHECK_EQ(fs_interval(1), 3, "FS 1 ms -> 8 uframes -> 3");
    CHECK_EQ(fs_interval(2), 4, "FS 2 ms -> 4");
    CHECK_EQ(fs_interval(7), 5, "FS 7 ms -> 56 -> 32 -> 5");
    CHECK_EQ(fs_interval(8), 6, "FS 8 ms -> 6");
    CHECK_EQ(fs_interval(16), 7, "FS 16 ms -> 7");
    CHECK_EQ(fs_interval(32), 8, "FS 32 ms -> 8");
    CHECK_EQ(fs_interval(128), 10, "FS 128 ms -> 10");
    CHECK_EQ(fs_interval(255), 10, "FS 255 ms -> 2040 -> 1024 -> 10, the top");

    ep_params(0x81, 3, 8, 0, XHCI_PIPE_SPEED_FULL, &ep);
    CHECK_EQ(ep.Interval, 3, "FS bInterval 0 clamps to 1 ms");
    CHECK_EQ(ep.IntervalClamped, 1, "and says so");
    ep_params(0x81, 3, 8, 255, XHCI_PIPE_SPEED_FULL, &ep);
    CHECK_EQ(ep.IntervalClamped, 0, "255 is in range for FS interrupt");

    CHECK_EQ(hs_interval(1), 0, "HS 1 -> 0");
    CHECK_EQ(hs_interval(4), 3, "HS 4 -> 3");
    CHECK_EQ(hs_interval(16), 15, "HS 16 -> 15");
    CHECK_EQ(hs_interval(0), 0, "HS 0 clamps to 1 -> 0");
    CHECK_EQ(hs_interval(17), 15, "HS 17 clamps to 16 -> 15");
    CHECK_EQ(hs_interval(255), 15, "HS 255 clamps to 16 -> 15");
    ep_params(0x81, 3, 8, 17, XHCI_PIPE_SPEED_HIGH, &ep);
    CHECK_EQ(ep.IntervalClamped, 1, "HS 17 is reported clamped");
    ep_params(0x81, 3, 8, 16, XHCI_PIPE_SPEED_HIGH, &ep);
    CHECK_EQ(ep.IntervalClamped, 0, "HS 16 is not");

    /* FS isoch: 2^(bInterval-1) ms -> bInterval + 2, 3-18. */
    CHECK_EQ(ep_params(0x01, 0x01, 192, 1, XHCI_PIPE_SPEED_FULL, &ep),
             XHCI_PIPE_OK, "FS isoch OUT accepted");
    CHECK_EQ(ep.Interval, 3, "FS isoch 1 -> 3");
    CHECK_EQ(ep.Dci, 2, "0x01 DCI 2");
    CHECK_EQ(ep.EpType, 1, "Isoch OUT");
    CHECK_EQ(ep.ErrorCount, 0, "isoch CErr 0 (Table 6-9)");
    CHECK_EQ(ep.AverageTrbLength, 3072, "isoch Average TRB 3 KB (4.14.1.1)");
    CHECK_EQ(ep.MaxEsitPayload, 192, "FS isoch ESIT = MPS");
    ep_params(0x01, 0x01, 192, 16, XHCI_PIPE_SPEED_FULL, &ep);
    CHECK_EQ(ep.Interval, 18, "FS isoch 16 -> 18");
    ep_params(0x01, 0x01, 192, 0, XHCI_PIPE_SPEED_FULL, &ep);
    CHECK_EQ(ep.Interval, 3, "FS isoch 0 clamps to 1 -> 3");
    CHECK_EQ(ep.IntervalClamped, 1, "clamped");

    /* HS isoch: bInterval - 1. */
    CHECK_EQ(ep_params(0x82, 0x05, 0x00C0, 4, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_OK, "HS isoch IN (async) accepted");
    CHECK_EQ(ep.Interval, 3, "HS isoch 4 -> 3");
    CHECK_EQ(ep.EpType, 5, "Isoch IN");
    CHECK_EQ(ep.Dci, 5, "0x82 DCI 5");
    CHECK_EQ(ep.ErrorCount, 0, "isoch CErr 0");
}

static void test_bulk(void)
{
    XHCI_PIPE_EP ep;

    CHECK_EQ(ep_params(0x02, 0x02, 512, 0, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_OK, "HS bulk OUT accepted");
    CHECK_EQ(ep.Dci, 4, "0x02 DCI 4");
    CHECK_EQ(ep.EpType, 2, "Bulk OUT");
    CHECK_EQ(ep.MaxPacketSize, 512, "MPS 512");
    CHECK_EQ(ep.Interval, 0, "HS bulk Interval 0");
    CHECK_EQ(ep.MaxBurstSize, 0, "HS bulk burst 0 (6.2.3.4)");
    CHECK_EQ(ep.MaxEsitPayload, 0, "no ESIT for bulk");
    CHECK_EQ(ep.ErrorCount, 3, "bulk CErr 3");
    CHECK_EQ(ep.AverageTrbLength, 3072, "bulk Average TRB 3 KB");

    CHECK_EQ(ep_params(0x81, 0x02, 512, 0, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_OK, "HS bulk IN accepted");
    CHECK_EQ(ep.Dci, 3, "0x81 DCI 3");
    CHECK_EQ(ep.EpType, 6, "Bulk IN");

    /* 6.2.3.4 clears Max Burst for HS bulk whatever 12:11 say. */
    ep_params(0x81, 0x02, 0x1200, 0, XHCI_PIPE_SPEED_HIGH, &ep);
    CHECK_EQ(ep.MaxBurstSize, 0, "HS bulk ignores 12:11");
    CHECK_EQ(ep.MaxPacketSize, 512, "and keeps 10:0");

    /* A NAK-rate bInterval on HS bulk does not reach Interval. */
    ep_params(0x02, 0x02, 512, 255, XHCI_PIPE_SPEED_HIGH, &ep);
    CHECK_EQ(ep.Interval, 0, "HS bulk bInterval 255 still Interval 0");

    CHECK_EQ(ep_params(0x02, 0x02, 64, 0, XHCI_PIPE_SPEED_FULL, &ep),
             XHCI_PIPE_OK, "FS bulk accepted");
    CHECK_EQ(ep.Interval, 0, "FS bulk Interval 0");
}

static void test_high_bandwidth(void)
{
    XHCI_PIPE_EP ep;

    CHECK_EQ(ep_params(0x81, 0x03, 0x1400, 1, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_OK, "0x1400 accepted");
    CHECK_EQ(ep.MaxPacketSize, 1024, "0x1400 MPS 1024");
    CHECK_EQ(ep.MaxBurstSize, 2, "0x1400 burst 2 (12:11 = 10b)");
    CHECK_EQ(ep.MaxEsitPayload, 3072, "0x1400 ESIT 3072");
    CHECK_EQ(ep.Mult, 0, "Mult stays 0");

    ep_params(0x81, 0x03, 0x0C00, 1, XHCI_PIPE_SPEED_HIGH, &ep);
    CHECK_EQ(ep.MaxPacketSize, 1024, "0x0C00 MPS 1024");
    CHECK_EQ(ep.MaxBurstSize, 1, "0x0C00 burst 1");
    CHECK_EQ(ep.MaxEsitPayload, 2048, "0x0C00 ESIT 2048");

    ep_params(0x83, 0x05, 0x1400, 1, XHCI_PIPE_SPEED_HIGH, &ep);
    CHECK_EQ(ep.MaxBurstSize, 2, "HS isoch takes 12:11 too");
    CHECK_EQ(ep.MaxEsitPayload, 3072, "HS isoch ESIT 3072");

    CHECK_EQ(ep_params(0x81, 0x03, 0x1800 | 64, 1, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_MALFORMED, "12:11 = 11b is reserved");

    /* FS: 6.2.3.4 clears the field; the bits are not a fault there. */
    CHECK_EQ(ep_params(0x81, 0x03, 0x1000 | 64, 1, XHCI_PIPE_SPEED_FULL, &ep),
             XHCI_PIPE_OK, "FS with 12:11 set is accepted");
    CHECK_EQ(ep.MaxBurstSize, 0, "FS burst 0");
    CHECK_EQ(ep.MaxEsitPayload, 64, "FS ESIT = MPS");
}

static void test_endpoint_refusals(void)
{
    XHCI_PIPE_EP ep;
    UCHAR d[7];

    ep.Dci = 0xEEEE;
    CHECK_EQ(ep_params(0x81, 0x03, 8, 1, 4, &ep), XHCI_PIPE_UNSUPPORTED,
             "SuperSpeed refused");
    CHECK_EQ(ep.Dci, 0xEEEE, "nothing written on a refusal");
    CHECK_EQ(ep_params(0x81, 0x03, 8, 1, 0, &ep), XHCI_PIPE_UNSUPPORTED,
             "speed 0 refused");
    CHECK_EQ(ep_params(0x81, 0x00, 8, 0, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_UNSUPPORTED, "a non-default control endpoint");
    CHECK_EQ(ep_params(0x81, 0x02, 8, 0, XHCI_PIPE_SPEED_LOW, &ep),
             XHCI_PIPE_UNSUPPORTED, "LS bulk");
    CHECK_EQ(ep_params(0x81, 0x01, 8, 1, XHCI_PIPE_SPEED_LOW, &ep),
             XHCI_PIPE_UNSUPPORTED, "LS isoch");
    CHECK_EQ(ep_params(0x80, 0x03, 8, 1, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_MALFORMED, "endpoint number 0");
    CHECK_EQ(ep_params(0x81, 0x03, 0, 1, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_MALFORMED, "MPS 0");
    CHECK_EQ(ep_params(0x81, 0x03, 0x1800, 1, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_MALFORMED, "MPS 0 with 12:11 = 11b");
    CHECK_EQ(ep_params(0x81, 0x03, 1025, 1, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_MALFORMED, "MPS 1025");
    CHECK_EQ(ep_params(0x81, 0x03, 1024, 1, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_OK, "MPS 1024");

    d[0] = 6;
    d[1] = 5;
    d[2] = 0x81;
    d[3] = 3;
    d[4] = 8;
    d[5] = 0;
    d[6] = 1;
    CHECK_EQ(XhciPipeEndpointParams(d, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_MALFORMED, "bLength 6");
    d[0] = 7;
    d[1] = 4;
    CHECK_EQ(XhciPipeEndpointParams(d, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_MALFORMED, "not an endpoint descriptor");
    d[1] = 5;
    CHECK_EQ(XhciPipeEndpointParams(d, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_OK, "the same bytes as an endpoint descriptor");
    CHECK_EQ(XhciPipeEndpointParams(NULL, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_BAD_PARAM, "NULL descriptor");
    CHECK_EQ(XhciPipeEndpointParams(d, XHCI_PIPE_SPEED_HIGH, NULL),
             XHCI_PIPE_BAD_PARAM, "NULL output");
}

/* ------------------------------------------------------------------ */
/* The configuration-descriptor walk                                   */
/* ------------------------------------------------------------------ */

/* A HID mouse like QEMU's at High Speed: 34 bytes. */
static const UCHAR mouse[34] = {
    0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,
    0x09, 0x21, 0x01, 0x00, 0x00, 0x01, 0x22, 0x34, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x04, 0x00, 0x07
};

/* An audio-shaped composite: interface 0 (control, no endpoints), interface
 * 1 alternates 0 (none) and 1 (one 9-byte isoch OUT endpoint followed by a
 * class-specific endpoint descriptor), interface 2 (a HID interrupt IN). */
static const UCHAR audio[] = {
    0x09, 0x02, 0x00, 0x00, 0x03, 0x01, 0x00, 0x80, 0x32,
    0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,
    0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,
    0x09, 0x04, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,
    0x07, 0x24, 0x01, 0x01, 0x01, 0x01, 0x00,
    0x09, 0x05, 0x01, 0x09, 0xC0, 0x00, 0x01, 0x00, 0x00,
    0x07, 0x25, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x09, 0x04, 0x02, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22, 0x20, 0x00,
    0x07, 0x05, 0x83, 0x03, 0x08, 0x00, 0x0A,
    0x07, 0x05, 0x04, 0x03, 0x08, 0x00, 0x0A
};

static void copy_bytes(UCHAR *to, const UCHAR *from, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        to[i] = from[i];
    }
}

static void set_total(UCHAR *c, ULONG total)
{
    c[2] = (UCHAR)(total & 0xFF);
    c[3] = (UCHAR)(total >> 8);
}

static void test_walk_good(void)
{
    XHCI_PIPE_IFACE f;
    XHCI_PIPE_EP ep;
    UCHAR c[sizeof(audio)];
    UCHAR big[64];

    CHECK_EQ(XhciPipeFindInterface(mouse, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_OK, "the mouse's interface 0 alternate 0");
    CHECK_EQ(f.InterfaceOffset, 9, "interface at 9");
    CHECK_EQ(f.InterfaceClass, 3, "HID");
    CHECK_EQ(f.InterfaceSubClass, 1, "boot");
    CHECK_EQ(f.InterfaceProtocol, 2, "mouse");
    CHECK_EQ(f.EndpointCount, 1, "one endpoint");
    CHECK_EQ(f.EndpointOffset[0], 27, "past the HID descriptor");
    CHECK_EQ(XhciPipeEndpointParams(mouse + f.EndpointOffset[0],
                                    XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_OK, "its endpoint");
    CHECK_EQ(ep.Dci, 3, "DCI 3");
    CHECK_EQ(ep.EpType, 7, "Interrupt IN");
    CHECK_EQ(ep.Interval, 6, "Interval 6");

    CHECK_EQ(XhciPipeFindInterface(mouse, sizeof(mouse), 0, 1, &f),
             XHCI_PIPE_NOT_FOUND, "no alternate 1");
    CHECK_EQ(XhciPipeFindInterface(mouse, sizeof(mouse), 1, 0, &f),
             XHCI_PIPE_NOT_FOUND, "no interface 1");

    /* A buffer longer than wTotalLength: the tail is not walked. */
    copy_bytes(big, mouse, sizeof(mouse));
    big[34] = 0x00;
    big[35] = 0x00;
    CHECK_EQ(XhciPipeFindInterface(big, sizeof(big), 0, 0, &f),
             XHCI_PIPE_OK, "bytes past wTotalLength are not read");

    copy_bytes(c, audio, sizeof(audio));
    set_total(c, sizeof(audio));
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(c), 0, 0, &f), XHCI_PIPE_OK,
             "audio control interface");
    CHECK_EQ(f.EndpointCount, 0, "with no endpoints");
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(c), 1, 0, &f), XHCI_PIPE_OK,
             "streaming alternate 0");
    CHECK_EQ(f.EndpointCount, 0, "zero bandwidth");
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(c), 1, 1, &f), XHCI_PIPE_OK,
             "streaming alternate 1");
    CHECK_EQ(f.EndpointCount, 1, "one isoch endpoint");
    CHECK_EQ(f.EndpointOffset[0], 52, "after the class interface descriptor");
    CHECK_EQ(XhciPipeEndpointParams(c + f.EndpointOffset[0],
                                    XHCI_PIPE_SPEED_FULL, &ep),
             XHCI_PIPE_OK, "a 9-byte audio endpoint descriptor");
    CHECK_EQ(ep.Dci, 2, "0x01 DCI 2");
    CHECK_EQ(ep.EpType, 1, "Isoch OUT");
    CHECK_EQ(ep.MaxPacketSize, 192, "MPS 192");
    CHECK_EQ(ep.Interval, 3, "FS isoch bInterval 1 -> 3");
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(c), 2, 0, &f), XHCI_PIPE_OK,
             "the HID interface");
    CHECK_EQ(f.EndpointCount, 2, "two endpoints");
    CHECK_EQ(XhciPipeDci(c[f.EndpointOffset[0] + 2]), 7, "0x83 DCI 7");
    CHECK_EQ(XhciPipeDci(c[f.EndpointOffset[1] + 2]), 8, "0x04 DCI 8");
}

static void test_walk_malformed(void)
{
    XHCI_PIPE_IFACE f;
    UCHAR c[sizeof(audio)];

    f.EndpointCount = 0xEEEE;
    copy_bytes(c, mouse, sizeof(mouse));
    c[18] = 0;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "bLength 0");
    CHECK_EQ(f.EndpointCount, 0xEEEE, "nothing written on a refusal");
    c[18] = 1;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "bLength 1");

    CHECK_EQ(XhciPipeFindInterface(mouse, sizeof(mouse) - 1, 0, 0, &f),
             XHCI_PIPE_MALFORMED, "wTotalLength past the buffer");
    CHECK_EQ(XhciPipeFindInterface(mouse, 8, 0, 0, &f),
             XHCI_PIPE_MALFORMED, "a buffer shorter than the header");

    copy_bytes(c, mouse, sizeof(mouse));
    set_total(c, 33);
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "an endpoint running past wTotalLength");

    copy_bytes(c, mouse, sizeof(mouse));
    set_total(c, 28);
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "a one-byte tail");

    copy_bytes(c, mouse, sizeof(mouse));
    set_total(c, 8);
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "wTotalLength 8");

    copy_bytes(c, mouse, sizeof(mouse));
    c[1] = 0x01;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "a device descriptor in its place");
    c[1] = 0x02;
    c[0] = 8;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "a configuration bLength of 8");

    copy_bytes(c, mouse, sizeof(mouse));
    c[13] = 2;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "bNumEndpoints 2, one present");
    c[13] = 0;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "bNumEndpoints 0, one present");
    c[13] = 31;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "bNumEndpoints 31");

    copy_bytes(c, mouse, sizeof(mouse));
    c[27] = 6;
    set_total(c, 33);
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "an endpoint descriptor of 6 bytes");

    copy_bytes(c, mouse, sizeof(mouse));
    c[9] = 8;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "an interface descriptor of 8 bytes");

    copy_bytes(c, mouse, sizeof(mouse));
    c[29] = 0x80;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(mouse), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "endpoint number 0 in the interface");

    /* The HID interface's second endpoint renamed to the first's address. */
    copy_bytes(c, audio, sizeof(audio));
    set_total(c, sizeof(audio));
    c[sizeof(audio) - 5] = 0x83;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(c), 2, 0, &f),
             XHCI_PIPE_MALFORMED, "two descriptors for one DCI");
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(c), 1, 1, &f), XHCI_PIPE_OK,
             "which another interface does not see");

    /* A bad length in an interface not asked for still refuses the set. */
    copy_bytes(c, audio, sizeof(audio));
    set_total(c, sizeof(audio));
    c[sizeof(audio) - 7] = 0;
    CHECK_EQ(XhciPipeFindInterface(c, sizeof(c), 0, 0, &f),
             XHCI_PIPE_MALFORMED, "a bad length anywhere refuses the walk");

    CHECK_EQ(XhciPipeFindInterface(NULL, 34, 0, 0, &f), XHCI_PIPE_BAD_PARAM,
             "NULL configuration");
    CHECK_EQ(XhciPipeFindInterface(mouse, 34, 0, 0, NULL), XHCI_PIPE_BAD_PARAM,
             "NULL output");
}

/* ------------------------------------------------------------------ */
/* The Configure Endpoint plan                                          */
/* ------------------------------------------------------------------ */

#define B(d) XHCI_PIPE_DCI_BIT(d)

static void test_plan(void)
{
    XHCI_PIPE_PLAN p;

    CHECK_EQ(XhciPipeConfigurePlan(0, B(3), B(2) | B(5), &p), XHCI_PIPE_OK,
             "{3} -> {2,5}");
    CHECK_EQ(p.AddFlags, 0x25, "Add A0 + A2 + A5");
    CHECK_EQ(p.DropFlags, 0x08, "Drop D3");
    CHECK_EQ(p.ContextEntries, 5, "Entries 5");
    CHECK_EQ(p.Enabled, 0x24, "enabled {2,5}");

    CHECK_EQ(XhciPipeConfigurePlan(0, 0, B(3), &p), XHCI_PIPE_OK,
             "the first configuration");
    CHECK_EQ(p.AddFlags, 0x09, "A0 + A3");
    CHECK_EQ(p.DropFlags, 0, "nothing to drop");
    CHECK_EQ(p.ContextEntries, 3, "Entries 3");

    CHECK_EQ(XhciPipeConfigurePlan(0, B(3) | B(4), 0, &p), XHCI_PIPE_OK,
             "an unconfigure by flags");
    CHECK_EQ(p.AddFlags, 0x01, "A0 alone");
    CHECK_EQ(p.DropFlags, 0x18, "D3 + D4");
    CHECK_EQ(p.ContextEntries, 1, "EP0 alone: Entries 1");

    CHECK_EQ(XhciPipeConfigurePlan(0, B(3), B(3), &p), XHCI_PIPE_OK,
             "re-adding the same endpoint");
    CHECK_EQ(p.DropFlags, 0x08, "is dropped (4.6.6 p.106)");
    CHECK_EQ(p.AddFlags, 0x09, "and added");

    /* SELECT_INTERFACE on interface 1 of a device whose interface 0 owns
     * DCI 3: alternate {4} -> {4,6}. */
    CHECK_EQ(XhciPipeConfigurePlan(B(3), B(4), B(4) | B(6), &p),
             XHCI_PIPE_OK, "an alternate change beside a kept endpoint");
    CHECK_EQ(p.AddFlags, 0x51, "A0 + A4 + A6");
    CHECK_EQ(p.DropFlags, 0x10, "D4");
    CHECK_EQ(p.ContextEntries, 6, "Entries 6");
    CHECK_EQ(p.Enabled, 0x58, "{3,4,6}");

    CHECK_EQ(XhciPipeConfigurePlan(B(7), B(4), 0, &p), XHCI_PIPE_OK,
             "an alternate going to zero bandwidth");
    CHECK_EQ(p.ContextEntries, 7, "the kept DCI 7 sets Entries");

    CHECK_EQ(XhciPipeConfigurePlan(0, 0, B(31), &p), XHCI_PIPE_OK,
             "DCI 31");
    CHECK_EQ(p.AddFlags, 0x80000001UL, "A31 + A0");
    CHECK_EQ(p.ContextEntries, 31, "Entries 31");

    p.AddFlags = 0xEEEE;
    CHECK_EQ(XhciPipeConfigurePlan(0, 0, B(1), &p), XHCI_PIPE_BAD_PARAM,
             "EP0 is not Configure Endpoint's (4.6.6 p.104)");
    CHECK_EQ(p.AddFlags, 0xEEEE, "nothing written on a refusal");
    CHECK_EQ(XhciPipeConfigurePlan(0, B(0), 0, &p), XHCI_PIPE_BAD_PARAM,
             "D0 is never set");
    CHECK_EQ(XhciPipeConfigurePlan(B(1), 0, 0, &p), XHCI_PIPE_BAD_PARAM,
             "nor kept");
    CHECK_EQ(XhciPipeConfigurePlan(B(3), B(3), 0, &p), XHCI_PIPE_BAD_PARAM,
             "kept and dropped at once");
    CHECK_EQ(XhciPipeConfigurePlan(B(3), 0, B(3), &p), XHCI_PIPE_BAD_PARAM,
             "kept and added at once");
    CHECK_EQ(XhciPipeConfigurePlan(0, 0, 0, NULL), XHCI_PIPE_BAD_PARAM,
             "NULL plan");
}

/* ------------------------------------------------------------------ */
/* SETUP packets                                                        */
/* ------------------------------------------------------------------ */

static XHCI_PIPE_CONTROL ctl(ULONG function)
{
    XHCI_PIPE_CONTROL c;

    c.Function = function;
    c.Length = 0;
    c.DirectionIn = 0;
    c.ReservedBits = 0;
    c.Request = 0;
    c.Value = 0;
    c.Index = 0;
    c.DescriptorType = 0;
    c.DescriptorIndex = 0;
    c.LanguageId = 0;
    c.FeatureSelector = 0;
    return c;
}

/* Build, then compare all eight bytes and the TRT against the expectation,
 * reporting at the caller's line. */
static void setup_is(XHCI_PIPE_CONTROL c, const char *want, ULONG wantTrt,
                     const char *what, int line)
{
    UCHAR s[8];
    ULONG trt;
    ULONG status;
    ULONG i;
    int same;

    trt = 0xEEEE;
    status = XhciPipeBuildSetup(&c, s, &trt);
    check_eq_impl(status, XHCI_PIPE_OK, what, __FILE__, line);
    same = 1;
    for (i = 0; i < 8; i++) {
        if (s[i] != (UCHAR)want[i]) {
            same = 0;
        }
    }
    if (!same) {
        printf("  got %02X %02X %02X %02X %02X %02X %02X %02X\n",
               s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
    }
    check_impl(same, what, __FILE__, line);
    check_eq_impl(trt, wantTrt, what, __FILE__, line);
}

#define SETUP_IS(c, w, t, what) setup_is((c), (w), (t), (what), __LINE__)

static void test_setup(void)
{
    XHCI_PIPE_CONTROL c;
    UCHAR s[8];
    ULONG trt;
    ULONG i;

    c = ctl(XHCI_PIPE_URB_GET_DESC_DEVICE);
    c.DescriptorType = 1;
    c.Length = 0x12;
    SETUP_IS(c, "\x80\x06\x00\x01\x00\x00\x12\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x0B device descriptor");

    c = ctl(XHCI_PIPE_URB_GET_DESC_DEVICE);
    c.DescriptorType = 3;
    c.DescriptorIndex = 2;
    c.LanguageId = 0x0409;
    c.Length = 0xFF;
    SETUP_IS(c, "\x80\x06\x02\x03\x09\x04\xFF\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x0B string 2, US English");

    c = ctl(XHCI_PIPE_URB_GET_DESC_INTERFACE);
    c.DescriptorType = 0x22;
    c.LanguageId = 1;
    c.Length = 0x34;
    SETUP_IS(c, "\x81\x06\x00\x22\x01\x00\x34\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x28 HID report descriptor of interface 1");

    c = ctl(XHCI_PIPE_URB_GET_DESC_ENDPOINT);
    c.DescriptorType = 0x25;
    c.LanguageId = 0x81;
    c.Length = 7;
    SETUP_IS(c, "\x82\x06\x00\x25\x81\x00\x07\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x24 endpoint recipient");

    c = ctl(XHCI_PIPE_URB_SET_DESC_DEVICE);
    c.DescriptorType = 3;
    c.DescriptorIndex = 4;
    c.Length = 8;
    SETUP_IS(c, "\x00\x07\x04\x03\x00\x00\x08\x00", XHCI_PIPE_TRT_OUT_DATA,
             "0x0C SET_DESCRIPTOR is OUT");

    c = ctl(XHCI_PIPE_URB_GET_DESC_DEVICE);
    c.DescriptorType = 1;
    SETUP_IS(c, "\x80\x06\x00\x01\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "a zero-length GET has no data stage (Table 4-7)");

    c = ctl(XHCI_PIPE_URB_CLASS_INTERFACE);
    c.Request = 0x0A;
    SETUP_IS(c, "\x21\x0A\x00\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x1B SET_IDLE");

    c = ctl(XHCI_PIPE_URB_CLASS_INTERFACE);
    c.DirectionIn = 1;
    c.Request = 0x01;
    c.Value = 0x0100;
    c.Index = 0;
    c.Length = 4;
    SETUP_IS(c, "\xA1\x01\x00\x01\x00\x00\x04\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x1B GET_REPORT");

    c = ctl(XHCI_PIPE_URB_CLASS_INTERFACE);
    c.DirectionIn = 1;
    c.Request = 0xFE;
    c.Length = 1;
    SETUP_IS(c, "\xA1\xFE\x00\x00\x00\x00\x01\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x1B mass-storage GET_MAX_LUN");

    c = ctl(XHCI_PIPE_URB_CLASS_ENDPOINT);
    c.Request = 0x01;
    c.Value = 0x0100;
    c.Index = 0x01;
    c.Length = 3;
    SETUP_IS(c, "\x22\x01\x00\x01\x01\x00\x03\x00", XHCI_PIPE_TRT_OUT_DATA,
             "0x1C audio SET_CUR sampling frequency");

    c = ctl(XHCI_PIPE_URB_CLASS_DEVICE);
    c.DirectionIn = 1;
    c.Request = 0x06;
    c.Value = 0x2900;
    c.Length = 9;
    SETUP_IS(c, "\xA0\x06\x00\x29\x00\x00\x09\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x1A class device");

    c = ctl(XHCI_PIPE_URB_CLASS_OTHER);
    c.Request = 0x03;
    c.Value = 0x0004;
    c.Index = 0x0002;
    SETUP_IS(c, "\x23\x03\x04\x00\x02\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x1F class other");

    c = ctl(XHCI_PIPE_URB_VENDOR_DEVICE);
    c.DirectionIn = 1;
    c.Request = 0x01;
    c.Value = 0x1234;
    c.Index = 0x5678;
    c.Length = 2;
    SETUP_IS(c, "\xC0\x01\x34\x12\x78\x56\x02\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x17 vendor device IN");

    c = ctl(XHCI_PIPE_URB_VENDOR_DEVICE);
    c.Request = 0x10;
    c.Value = 0x0001;
    c.Length = 6;
    SETUP_IS(c, "\x40\x10\x01\x00\x00\x00\x06\x00", XHCI_PIPE_TRT_OUT_DATA,
             "0x17 vendor device OUT");

    c = ctl(XHCI_PIPE_URB_VENDOR_INTERFACE);
    c.Request = 0x02;
    SETUP_IS(c, "\x41\x02\x00\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x18 vendor interface");

    c = ctl(XHCI_PIPE_URB_VENDOR_ENDPOINT);
    c.Request = 0x03;
    c.Index = 0x82;
    SETUP_IS(c, "\x42\x03\x00\x00\x82\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x19 vendor endpoint");

    c = ctl(XHCI_PIPE_URB_VENDOR_OTHER);
    c.Request = 0x04;
    SETUP_IS(c, "\x43\x04\x00\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x20 vendor other");

    c = ctl(XHCI_PIPE_URB_CLASS_INTERFACE);
    c.Request = 0x0A;
    c.ReservedBits = 0x04;
    SETUP_IS(c, "\x25\x0A\x00\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "reserved bits 4:2 pass into bmRequestType");

    c = ctl(XHCI_PIPE_URB_CLEAR_FEATURE_ENDPOINT);
    c.FeatureSelector = 0;
    c.Index = 0x81;
    c.Length = 99;
    SETUP_IS(c, "\x02\x01\x00\x00\x81\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x12 clear halt on 0x81, wLength 0 whatever Length says");

    c = ctl(XHCI_PIPE_URB_SET_FEATURE_DEVICE);
    c.FeatureSelector = 1;
    SETUP_IS(c, "\x00\x03\x01\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x0D remote wakeup");

    c = ctl(XHCI_PIPE_URB_CLEAR_FEATURE_DEVICE);
    c.FeatureSelector = 1;
    SETUP_IS(c, "\x00\x01\x01\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x10 clear remote wakeup");

    c = ctl(XHCI_PIPE_URB_SET_FEATURE_INTERFACE);
    c.FeatureSelector = 2;
    c.Index = 1;
    SETUP_IS(c, "\x01\x03\x02\x00\x01\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x0E set feature interface");

    c = ctl(XHCI_PIPE_URB_SET_FEATURE_ENDPOINT);
    c.Index = 0x02;
    SETUP_IS(c, "\x02\x03\x00\x00\x02\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x0F set halt");

    c = ctl(XHCI_PIPE_URB_SET_FEATURE_OTHER);
    c.FeatureSelector = 8;
    c.Index = 3;
    SETUP_IS(c, "\x03\x03\x08\x00\x03\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x23 set feature other");

    c = ctl(XHCI_PIPE_URB_CLEAR_FEATURE_INTERFACE);
    SETUP_IS(c, "\x01\x01\x00\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x11 clear feature interface");

    c = ctl(XHCI_PIPE_URB_CLEAR_FEATURE_OTHER);
    c.FeatureSelector = 0x10;
    c.Index = 1;
    SETUP_IS(c, "\x03\x01\x10\x00\x01\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "0x22 clear feature other");

    c = ctl(XHCI_PIPE_URB_GET_STATUS_ENDPOINT);
    c.Index = 0x81;
    c.Length = 2;
    SETUP_IS(c, "\x82\x00\x00\x00\x81\x00\x02\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x15 get status endpoint");

    c = ctl(XHCI_PIPE_URB_GET_STATUS_DEVICE);
    c.Length = 64;
    SETUP_IS(c, "\x80\x00\x00\x00\x00\x00\x02\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x13 get status asks 2 bytes from a larger buffer");

    c = ctl(XHCI_PIPE_URB_GET_STATUS_INTERFACE);
    c.Length = 2;
    SETUP_IS(c, "\x81\x00\x00\x00\x00\x00\x02\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x14 get status interface");

    c = ctl(XHCI_PIPE_URB_GET_STATUS_OTHER);
    c.Index = 1;
    c.Length = 4;
    SETUP_IS(c, "\x83\x00\x00\x00\x01\x00\x02\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x21 get status other");

    c = ctl(XHCI_PIPE_URB_GET_CONFIGURATION);
    c.Length = 1;
    SETUP_IS(c, "\x80\x08\x00\x00\x00\x00\x01\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x26 get configuration");

    c = ctl(XHCI_PIPE_URB_GET_INTERFACE);
    c.Index = 1;
    c.Length = 1;
    SETUP_IS(c, "\x81\x0A\x00\x00\x01\x00\x01\x00", XHCI_PIPE_TRT_IN_DATA,
             "0x27 get interface");

    /* Refusals write nothing. */
    for (i = 0; i < 8; i++) {
        s[i] = 0xEE;
    }
    trt = 0xEEEE;
    c = ctl(XHCI_PIPE_URB_CONTROL_TRANSFER);
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_UNSUPPORTED,
             "0x08 carries its own bytes");
    CHECK_EQ(s[0], 0xEE, "the buffer untouched");
    CHECK_EQ(trt, 0xEEEE, "the TRT untouched");
    c = ctl(0x0000);
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_UNSUPPORTED,
             "SELECT_CONFIGURATION is no control request here");
    c = ctl(0x0009);
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_UNSUPPORTED,
             "nor is a bulk transfer");
    c = ctl(0x002A);
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_UNSUPPORTED,
             "0x2A's request code is the device's own");
    c = ctl(0x0016);
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_UNSUPPORTED,
             "the reserved 0x16");

    c = ctl(XHCI_PIPE_URB_VENDOR_DEVICE);
    c.Length = 0x10000;
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_BAD_PARAM,
             "Length past wLength");
    c.Length = 0xFFFF;
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_OK,
             "Length 0xFFFF fits");
    CHECK_EQ(s[6], 0xFF, "wLength 0xFFFF low byte");
    CHECK_EQ(s[7], 0xFF, "wLength 0xFFFF high byte");
    for (i = 0; i < 8; i++) {
        s[i] = 0xEE;
    }
    c = ctl(XHCI_PIPE_URB_VENDOR_DEVICE);
    c.Request = 0x100;
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_BAD_PARAM,
             "bRequest past a byte");
    c = ctl(XHCI_PIPE_URB_VENDOR_DEVICE);
    c.Value = 0x10000;
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_BAD_PARAM,
             "wValue past a word");
    c = ctl(XHCI_PIPE_URB_VENDOR_DEVICE);
    c.ReservedBits = 0x80;
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_BAD_PARAM,
             "reserved bits that would turn the direction");
    c.ReservedBits = 0x01;
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_BAD_PARAM,
             "reserved bits that would turn the recipient");
    c = ctl(XHCI_PIPE_URB_GET_DESC_DEVICE);
    c.DescriptorType = 0x100;
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_BAD_PARAM,
             "a descriptor type past a byte");
    c = ctl(XHCI_PIPE_URB_GET_STATUS_DEVICE);
    c.Length = 1;
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_BAD_PARAM,
             "a status buffer of 1 byte");
    c = ctl(XHCI_PIPE_URB_GET_CONFIGURATION);
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_BAD_PARAM,
             "a configuration buffer of 0 bytes");
    c = ctl(XHCI_PIPE_URB_CLEAR_FEATURE_ENDPOINT);
    c.Index = 0x10000;
    CHECK_EQ(XhciPipeBuildSetup(&c, s, &trt), XHCI_PIPE_BAD_PARAM,
             "a feature wIndex past a word");
    CHECK_EQ(s[0], 0xEE, "and still nothing written");
    CHECK_EQ(XhciPipeBuildSetup(NULL, s, &trt), XHCI_PIPE_BAD_PARAM,
             "NULL control");
}

static void test_raw_setup(void)
{
    ULONG trt;

    trt = 0xEEEE;
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x00\x05\x07\x00\x00\x00\x00\x00",
                                   &trt),
             XHCI_PIPE_REFUSED, "a raw SET_ADDRESS");
    CHECK_EQ(trt, 0xEEEE, "no TRT for a refusal");
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x00\x09\x01\x00\x00\x00\x00\x00",
                                   &trt),
             XHCI_PIPE_REFUSED, "a raw SET_CONFIGURATION");
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x01\x09\x01\x00\x00\x00\x00\x00",
                                   &trt),
             XHCI_PIPE_REFUSED, "at any recipient");
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x21\x09\x00\x02\x00\x00\x01\x00",
                                   &trt),
             XHCI_PIPE_OK, "HID SET_REPORT is class request 9, not standard");
    CHECK_EQ(trt, XHCI_PIPE_TRT_OUT_DATA, "OUT data");
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x40\x05\x00\x00\x00\x00\x00\x00",
                                   &trt),
             XHCI_PIPE_OK, "a vendor request 5 is not SET_ADDRESS");
    CHECK_EQ(trt, XHCI_PIPE_TRT_NO_DATA, "no data");
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x80\x06\x00\x01\x00\x00\x40\x00",
                                   &trt),
             XHCI_PIPE_OK, "a raw GET_DESCRIPTOR");
    CHECK_EQ(trt, XHCI_PIPE_TRT_IN_DATA, "IN data");
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x80\x00\x00\x00\x00\x00\x00\x00",
                                   &trt),
             XHCI_PIPE_OK, "an IN request with wLength 0");
    CHECK_EQ(trt, XHCI_PIPE_TRT_NO_DATA, "no data stage (Table 4-7)");
    CHECK_EQ(XhciPipeCheckRawSetup(NULL, &trt), XHCI_PIPE_BAD_PARAM,
             "NULL setup");
}

/* ------------------------------------------------------------------ */
/* The buffer split                                                     */
/* ------------------------------------------------------------------ */

/* Every element stays in its page, the pages run 0..n-1, only the first
 * starts mid-page, and the lengths add up to the chunk. */
static void split_sound(const XHCI_PIPE_ELEMENT *e, ULONG n, ULONG first,
                        ULONG chunk, int line)
{
    ULONG i;
    ULONG sum;
    int ok;

    sum = 0;
    ok = 1;
    for (i = 0; i < n; i++) {
        if (e[i].Page != i || e[i].Length == 0 ||
            e[i].Offset + e[i].Length > XHCI_PIPE_PAGE_SIZE ||
            e[i].Offset != (i == 0 ? first : 0)) {
            ok = 0;
        }
        if (i + 1 < n && e[i].Offset + e[i].Length != XHCI_PIPE_PAGE_SIZE) {
            ok = 0;
        }
        sum += e[i].Length;
    }
    check_impl(ok, "elements tile their pages", __FILE__, line);
    check_eq_impl(sum, chunk, "elements add up to the chunk", __FILE__, line);
}

static void test_split(void)
{
    XHCI_PIPE_ELEMENT e[34];
    ULONG n;
    ULONG chunk;

    CHECK_EQ(XhciPipeSplit(0xF00, 0x300, 34, 34, e, &n, &chunk),
             XHCI_PIPE_OK, "0xF00 + 0x300");
    CHECK_EQ(n, 2, "two elements");
    CHECK_EQ(chunk, 0x300, "all of it");
    CHECK_EQ(e[0].Page, 0, "first in page 0");
    CHECK_EQ(e[0].Offset, 0xF00, "at 0xF00");
    CHECK_EQ(e[0].Length, 0x100, "to the page's end");
    CHECK_EQ(e[1].Page, 1, "second in page 1");
    CHECK_EQ(e[1].Offset, 0, "from its start");
    CHECK_EQ(e[1].Length, 0x200, "the rest");

    XhciPipeSplit(0, 0x1000, 34, 34, e, &n, &chunk);
    CHECK_EQ(n, 1, "one aligned page is one element");
    XhciPipeSplit(0, 0x1001, 34, 34, e, &n, &chunk);
    CHECK_EQ(n, 2, "one byte more is two");
    CHECK_EQ(e[1].Length, 1, "of one byte");
    XhciPipeSplit(0xFFF, 2, 34, 34, e, &n, &chunk);
    CHECK_EQ(n, 2, "two bytes across a page edge");
    split_sound(e, n, 0xFFF, chunk, __LINE__);

    CHECK_EQ(XhciPipeSplit(0x123, 0, 0, 0, NULL, &n, &chunk), XHCI_PIPE_OK,
             "a zero-length transfer");
    CHECK_EQ(n, 0, "no element");
    CHECK_EQ(chunk, 0, "no bytes");

    XhciPipeSplit(0, 0x100000, 34, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x20000, "an aligned 1 MB stops at 128 KB");
    CHECK_EQ(n, 32, "in 32 elements");
    split_sound(e, n, 0, chunk, __LINE__);

    XhciPipeSplit(0x10, 0x100000, 34, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x1FFF0, "unaligned, 32 pages hold 128 KB - 0x10");
    CHECK_EQ(n, 32, "still 32 elements");
    split_sound(e, n, 0x10, chunk, __LINE__);

    XhciPipeSplit(0x800, 0x3000, 2, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x1800, "two map registers hold 0x1800 from 0x800");
    CHECK_EQ(n, 2, "in two elements");
    split_sound(e, n, 0x800, chunk, __LINE__);

    XhciPipeSplit(0x800, 0x3000, 34, 1, e, &n, &chunk);
    CHECK_EQ(chunk, 0x800, "one element holds the rest of the first page");
    CHECK_EQ(n, 1, "in one element");

    XhciPipeSplit(0, 0x2000, 1, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x1000, "one map register, one page");
    XhciPipeSplit(0, 0x1800, 1, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x1000, "the next chunk then starts at offset 0");
    XhciPipeSplit(0, 0x800, 1, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x800, "and the last one is short");

    n = 0xEEEE;
    CHECK_EQ(XhciPipeSplit(0x1000, 1, 34, 34, e, &n, &chunk),
             XHCI_PIPE_BAD_PARAM, "an offset of a whole page");
    CHECK_EQ(n, 0xEEEE, "nothing written on a refusal");
    CHECK_EQ(XhciPipeSplit(0, 1, 0, 34, e, &n, &chunk), XHCI_PIPE_BAD_PARAM,
             "no map register");
    CHECK_EQ(XhciPipeSplit(0, 1, 34, 0, e, &n, &chunk), XHCI_PIPE_BAD_PARAM,
             "no element");
    CHECK_EQ(XhciPipeSplit(0, 1, 34, 34, NULL, &n, &chunk),
             XHCI_PIPE_BAD_PARAM, "no element array");
}

/* ------------------------------------------------------------------ */
/* Status                                                               */
/* ------------------------------------------------------------------ */

static void test_status(void)
{
    /* The DDK numbers, restated here from inc\usbdi.h and inc\ntstatus.h so
     * a typo in the header's table cannot agree with itself. */
    CHECK_EQ(XHCI_PIPE_USBD_CANCELED, 0x00010000UL,
             "USBD_STATUS_CANCELED is success-class in the 2000 DDK");
    CHECK_EQ(XHCI_PIPE_USBD_INVALID_URB_FUNCTION, 0x80000200UL,
             "INVALID_URB_FUNCTION");
    CHECK_EQ(XHCI_PIPE_NT_CANCELLED, 0xC0000120UL, "STATUS_CANCELLED");
    CHECK_EQ(XHCI_PIPE_NT_INVALID_PARAMETER, 0xC000000DUL,
             "STATUS_INVALID_PARAMETER");

    CHECK_EQ(XhciPipeNtStatus(0x00000000UL), 0x00000000UL, "SUCCESS");
    CHECK_EQ(XhciPipeNtStatus(0x40000000UL), 0x00000103UL, "PENDING");
    CHECK_EQ(XhciPipeNtStatus(0x00010000UL), 0xC0000120UL, "CANCELED");
    CHECK_EQ(XhciPipeNtStatus(0x80000200UL), 0xC000000DUL,
             "INVALID_URB_FUNCTION");
    CHECK_EQ(XhciPipeNtStatus(0x80000300UL), 0xC000000DUL,
             "INVALID_PARAMETER");
    CHECK_EQ(XhciPipeNtStatus(0x80000600UL), 0xC000000DUL,
             "INVALID_PIPE_HANDLE");
    CHECK_EQ(XhciPipeNtStatus(0xC0000A00UL), 0xC000000DUL, "BAD_START_FRAME");
    CHECK_EQ(XhciPipeNtStatus(0x80000100UL), 0xC000009AUL, "NO_MEMORY");
    CHECK_EQ(XhciPipeNtStatus(0x80000700UL), 0xC000009AUL, "NO_BANDWIDTH");
    CHECK_EQ(XhciPipeNtStatus(0x80000400UL), 0x80000011UL, "ERROR_BUSY");
    CHECK_EQ(XhciPipeNtStatus(0xC0000004UL), 0xC0000001UL, "STALL_PID");
    CHECK_EQ(XhciPipeNtStatus(0xC0000005UL), 0xC0000001UL,
             "DEV_NOT_RESPONDING");
    CHECK_EQ(XhciPipeNtStatus(0xC0000030UL), 0xC0000001UL, "ENDPOINT_HALTED");
    CHECK_EQ(XhciPipeNtStatus(0x80000800UL), 0xC0000001UL, "INTERNAL_HC_ERROR");
    CHECK_EQ(XhciPipeNtStatus(0x80000900UL), 0xC0000001UL,
             "ERROR_SHORT_TRANSFER");
    CHECK_EQ(XhciPipeNtStatus(0xC0000B00UL), 0xC0000001UL,
             "ISOCH_REQUEST_FAILED");
    CHECK_EQ(XhciPipeNtStatus(0xC0007000UL), 0xC0000001UL,
             "usbport's own DEVICE_GONE value, which this header lacks");
    CHECK_EQ(XhciPipeNtStatus(0x00020000UL), 0x00000000UL,
             "CANCELING, not an error");

    CHECK_EQ(XhciPipeConfigureUsbdStatus(1), 0x00000000UL, "Success");
    CHECK_EQ(XhciPipeConfigureUsbdStatus(7), 0x80000700UL, "Resource Error");
    CHECK_EQ(XhciPipeConfigureUsbdStatus(8), 0x80000700UL, "Bandwidth Error");
    CHECK_EQ(XhciPipeConfigureUsbdStatus(35), 0x80000700UL,
             "Secondary Bandwidth Error");
    CHECK_EQ(XhciPipeConfigureUsbdStatus(17), 0x80000800UL, "Parameter Error");
    CHECK_EQ(XhciPipeConfigureUsbdStatus(19), 0x80000800UL,
             "Context State Error");
}

int main(void)
{
    test_dci();
    test_qemu_mouse();
    test_intervals();
    test_bulk();
    test_high_bandwidth();
    test_endpoint_refusals();
    test_walk_good();
    test_walk_malformed();
    test_plan();
    test_setup();
    test_raw_setup();
    test_split();
    test_status();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
