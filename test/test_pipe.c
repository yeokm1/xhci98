/*
 * test_pipe.c - host vectors for the pure half of URB dispatch
 * (src\xhci_pipe.c; roadmap-hcd.md task 26-A.5).
 *
 * Every expected value below was worked by hand, not taken from the code
 * under test. Most come from a specification: the Endpoint Context rules
 * from xHCI 1.2c sections 4.5.1, 4.6.6, 4.14.1.1, 4.14.2 and 6.2.3 (Tables
 * 6-9, 6-11, 6-12), the SETUP bytes, the Max Packet Size limits and the
 * packet-boundary rule of the split from USB 2.0 chapters 5 and 9 and the
 * Windows 2000 DDK's usbdi.h / usb100.h constants, and the status values
 * from that DDK's usbdi.h and ntstatus.h. The USB 2.0 specification is not
 * in docs\references\, so its rules are restated from the published text,
 * not from a copy this repository can check.
 *
 * Some are this module's own policy, chosen where the specification is
 * silent, and test that policy rather than a spec rule: clamping an
 * out-of-range bInterval and reporting it in IntervalClamped; Interval 0 for
 * bulk; the Average TRB
 * Length values; the RequestTypeReservedBits rule; the whole
 * USBD_STATUS -> NTSTATUS table in XhciPipeNtStatus, apart from the DDK's
 * numeric values themselves; the fixed wLength of GET_STATUS /
 * GET_CONFIGURATION / GET_INTERFACE; the strict configuration walk; and the
 * 32-element / 128 KB chunk caps, which are the batch plan's.
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

static void test_interrupt_out(void)
{
    XHCI_PIPE_EP ep;

    /* Table 6-9: Interrupt OUT is EP Type 3. */
    CHECK_EQ(ep_params(0x02, 0x03, 8, 1, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_OK, "HS interrupt OUT accepted");
    CHECK_EQ(ep.EpType, 3, "Interrupt OUT is EP Type 3");
    CHECK_EQ(ep.Dci, 4, "0x02 DCI 4");
    CHECK_EQ(ep.DirectionIn, 0, "OUT");
    CHECK_EQ(ep.Interval, 0, "HS bInterval 1 -> 0");
    CHECK_EQ(ep_params(0x04, 0x03, 8, 10, XHCI_PIPE_SPEED_FULL, &ep),
             XHCI_PIPE_OK, "FS interrupt OUT accepted");
    CHECK_EQ(ep.EpType, 3, "FS Interrupt OUT is EP Type 3 too");
    CHECK_EQ(ep.Dci, 8, "0x04 DCI 8");
    CHECK_EQ(ep_params(0x01, 0x03, 8, 10, XHCI_PIPE_SPEED_LOW, &ep),
             XHCI_PIPE_OK, "LS interrupt OUT accepted");
    CHECK_EQ(ep.EpType, 3, "LS Interrupt OUT is EP Type 3");
}

/* The status of one endpoint descriptor, for the boundary tables below. */
static ULONG mps_status(ULONG attributes, ULONG wMaxPacketSize, ULONG speed)
{
    XHCI_PIPE_EP ep;

    return ep_params(0x81, attributes, wMaxPacketSize, 1, speed, &ep);
}

static void test_mps_limits(void)
{
    XHCI_PIPE_EP ep;

    /* USB 2.0 5.7.3: LS interrupt at most 8, FS interrupt at most 64. */
    CHECK_EQ(mps_status(0x03, 8, XHCI_PIPE_SPEED_LOW), XHCI_PIPE_OK,
             "LS interrupt 8");
    CHECK_EQ(mps_status(0x03, 9, XHCI_PIPE_SPEED_LOW), XHCI_PIPE_MALFORMED,
             "LS interrupt 9");
    CHECK_EQ(mps_status(0x03, 64, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_OK,
             "FS interrupt 64");
    CHECK_EQ(mps_status(0x03, 65, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_MALFORMED,
             "FS interrupt 65");

    /* 5.8.3: FS bulk is exactly 8, 16, 32 or 64. */
    CHECK_EQ(mps_status(0x02, 8, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_OK,
             "FS bulk 8");
    CHECK_EQ(mps_status(0x02, 16, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_OK,
             "FS bulk 16");
    CHECK_EQ(mps_status(0x02, 32, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_OK,
             "FS bulk 32");
    CHECK_EQ(mps_status(0x02, 64, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_OK,
             "FS bulk 64");
    CHECK_EQ(mps_status(0x02, 7, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_MALFORMED,
             "FS bulk 7");
    CHECK_EQ(mps_status(0x02, 9, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_MALFORMED,
             "FS bulk 9");
    CHECK_EQ(mps_status(0x02, 24, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_MALFORMED,
             "FS bulk 24, inside the range but not a listed size");
    CHECK_EQ(mps_status(0x02, 63, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_MALFORMED,
             "FS bulk 63");
    CHECK_EQ(mps_status(0x02, 65, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_MALFORMED,
             "FS bulk 65");
    CHECK_EQ(mps_status(0x02, 512, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_MALFORMED,
             "FS bulk 512, the HS size");

    /* 5.6.3: FS isochronous at most 1023. */
    CHECK_EQ(mps_status(0x01, 1023, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_OK,
             "FS isoch 1023");
    CHECK_EQ(mps_status(0x01, 1024, XHCI_PIPE_SPEED_FULL), XHCI_PIPE_MALFORMED,
             "FS isoch 1024");

    /* 5.8.3: HS bulk is exactly 512. */
    CHECK_EQ(mps_status(0x02, 512, XHCI_PIPE_SPEED_HIGH), XHCI_PIPE_OK,
             "HS bulk 512");
    CHECK_EQ(mps_status(0x02, 511, XHCI_PIPE_SPEED_HIGH), XHCI_PIPE_MALFORMED,
             "HS bulk 511");
    CHECK_EQ(mps_status(0x02, 513, XHCI_PIPE_SPEED_HIGH), XHCI_PIPE_MALFORMED,
             "HS bulk 513");
    CHECK_EQ(mps_status(0x02, 64, XHCI_PIPE_SPEED_HIGH), XHCI_PIPE_MALFORMED,
             "HS bulk 64, the FS size");
    CHECK_EQ(mps_status(0x02, 0x0800 | 512, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_OK, "HS bulk 512 with 12:11 set: bits not read");

    /* Table 9-14, HS interrupt: 12:11 = 0 -> 1-1024; 1 -> 513-1024;
     * 2 -> 683-1024. */
    CHECK_EQ(mps_status(0x03, 1, XHCI_PIPE_SPEED_HIGH), XHCI_PIPE_OK,
             "HS interrupt 1, one transaction");
    CHECK_EQ(mps_status(0x03, 0x0800 | 513, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_OK, "HS interrupt 513, two transactions");
    CHECK_EQ(mps_status(0x03, 0x0800 | 512, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_MALFORMED, "HS interrupt 512, two transactions");
    CHECK_EQ(mps_status(0x03, 0x0800 | 1024, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_OK, "HS interrupt 1024, two transactions");
    CHECK_EQ(mps_status(0x03, 0x0800 | 1025, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_MALFORMED, "HS interrupt 1025, two transactions");
    CHECK_EQ(mps_status(0x03, 0x1000 | 683, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_OK, "HS interrupt 683, three transactions");
    CHECK_EQ(mps_status(0x03, 0x1000 | 682, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_MALFORMED, "HS interrupt 682, three transactions");
    CHECK_EQ(mps_status(0x03, 0x1800 | 1024, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_MALFORMED, "HS interrupt 12:11 = 3");

    /* The same table for HS isochronous. */
    CHECK_EQ(mps_status(0x05, 1024, XHCI_PIPE_SPEED_HIGH), XHCI_PIPE_OK,
             "HS isoch 1024, one transaction");
    CHECK_EQ(mps_status(0x05, 1, XHCI_PIPE_SPEED_HIGH), XHCI_PIPE_OK,
             "HS isoch 1, one transaction");
    CHECK_EQ(mps_status(0x05, 0x0800 | 513, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_OK, "HS isoch 513, two transactions");
    CHECK_EQ(mps_status(0x05, 0x0800 | 512, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_MALFORMED, "HS isoch 512, two transactions");
    CHECK_EQ(mps_status(0x05, 0x1000 | 683, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_OK, "HS isoch 683, three transactions");
    CHECK_EQ(mps_status(0x05, 0x1000 | 682, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_MALFORMED, "HS isoch 682, three transactions");
    CHECK_EQ(mps_status(0x05, 0x1800 | 1024, XHCI_PIPE_SPEED_HIGH),
             XHCI_PIPE_MALFORMED, "HS isoch 12:11 = 3");

    /* A size refusal writes nothing either. */
    ep.Dci = 0xEEEE;
    CHECK_EQ(ep_params(0x02, 0x02, 64, 0, XHCI_PIPE_SPEED_HIGH, &ep),
             XHCI_PIPE_MALFORMED, "HS bulk 64 again");
    CHECK_EQ(ep.Dci, 0xEEEE, "nothing written on a size refusal");
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

/* The mouse with an 8-byte interface descriptor: 33 bytes, wTotalLength 33,
 * every other length intact. */
static const UCHAR short_interface[33] = {
    0x09, 0x02, 0x21, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,
    0x08, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x02,
    0x09, 0x21, 0x01, 0x00, 0x00, 0x01, 0x22, 0x34, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x04, 0x00, 0x07
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

    /* The mouse with its interface descriptor cut to 8 bytes (iInterface
     * dropped) and every later length still consistent, so the walk would
     * succeed but for the 9-byte minimum. */
    CHECK_EQ(XhciPipeFindInterface(short_interface, sizeof(short_interface),
                                   0, 0, &f),
             XHCI_PIPE_MALFORMED, "an interface descriptor of 8 bytes");
    CHECK_EQ(XhciPipeFindInterface(short_interface, sizeof(short_interface),
                                   1, 0, &f),
             XHCI_PIPE_MALFORMED, "refused whichever interface is asked for");

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
    SETUP_IS(c, "\x21\x0A\x00\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "reserved bits 4:2 cleared, as usbport clears them");

    c = ctl(XHCI_PIPE_URB_CLASS_INTERFACE);
    c.Request = 0x0A;
    c.ReservedBits = 0x22;
    SETUP_IS(c, "\x21\x0A\x00\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "98 SE hidusb's SET_IDLE with 0x22 in the reserved byte");

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
    SETUP_IS(c, "\x40\x00\x00\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "reserved bits cannot turn the direction");
    c.ReservedBits = 0x01;
    SETUP_IS(c, "\x40\x00\x00\x00\x00\x00\x00\x00", XHCI_PIPE_TRT_NO_DATA,
             "reserved bits cannot turn the recipient");
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
    trt = 0xEEEE;
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x01\x0B\x01\x00\x01\x00\x00\x00",
                                   &trt),
             XHCI_PIPE_REFUSED, "a raw SET_INTERFACE (interface 1, alt 1)");
    CHECK_EQ(trt, 0xEEEE, "no TRT for it either");
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x00\x0B\x00\x00\x00\x00\x00\x00",
                                   &trt),
             XHCI_PIPE_REFUSED, "SET_INTERFACE at a device recipient");
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x21\x0B\x00\x00\x00\x00\x00\x00",
                                   &trt),
             XHCI_PIPE_OK, "HID SET_PROTOCOL is class request 11");
    CHECK_EQ(trt, XHCI_PIPE_TRT_NO_DATA, "no data");
    CHECK_EQ(XhciPipeCheckRawSetup((const UCHAR *)"\x81\x0A\x00\x00\x01\x00\x01\x00",
                                   &trt),
             XHCI_PIPE_OK, "GET_INTERFACE passes");
    CHECK_EQ(trt, XHCI_PIPE_TRT_IN_DATA, "IN data");
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

    CHECK_EQ(XhciPipeSplit(0xF00, 0x300, 512, 34, 34, e, &n, &chunk),
             XHCI_PIPE_OK, "0xF00 + 0x300");
    CHECK_EQ(n, 2, "two elements");
    CHECK_EQ(chunk, 0x300, "all of it: a final chunk may end short");
    CHECK_EQ(e[0].Page, 0, "first in page 0");
    CHECK_EQ(e[0].Offset, 0xF00, "at 0xF00");
    CHECK_EQ(e[0].Length, 0x100, "to the page's end");
    CHECK_EQ(e[1].Page, 1, "second in page 1");
    CHECK_EQ(e[1].Offset, 0, "from its start");
    CHECK_EQ(e[1].Length, 0x200, "the rest");

    XhciPipeSplit(0, 0x1000, 512, 34, 34, e, &n, &chunk);
    CHECK_EQ(n, 1, "one aligned page is one element");
    XhciPipeSplit(0, 0x1001, 512, 34, 34, e, &n, &chunk);
    CHECK_EQ(n, 2, "one byte more is two");
    CHECK_EQ(e[1].Length, 1, "of one byte");
    XhciPipeSplit(0xFFF, 2, 64, 34, 34, e, &n, &chunk);
    CHECK_EQ(n, 2, "two bytes across a page edge");
    split_sound(e, n, 0xFFF, chunk, __LINE__);

    CHECK_EQ(XhciPipeSplit(0x123, 0, 64, 0, 0, NULL, &n, &chunk),
             XHCI_PIPE_OK, "a zero-length transfer");
    CHECK_EQ(n, 0, "no element");
    CHECK_EQ(chunk, 0, "no bytes");

    XhciPipeSplit(0, 0x100000, 512, 34, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x20000, "an aligned 1 MB stops at 128 KB");
    CHECK_EQ(n, 32, "in 32 elements");
    split_sound(e, n, 0, chunk, __LINE__);

    /* 32 pages from 0x10 hold 0x1FFF0, which is not a whole number of
     * 512-byte packets: the chunk stops at 0xFF packets, 0x1FE00, so the
     * device does not see a short packet mid-transfer (USB 2.0 5.8.3). */
    CHECK_EQ(XhciPipeSplit(0x10, 0x100000, 512, 34, 34, e, &n, &chunk),
             XHCI_PIPE_OK, "unaligned 1 MB");
    CHECK_EQ(chunk, 0x1FE00, "rounded down to a multiple of 512");
    CHECK_EQ(n, 32, "still 32 elements");
    CHECK_EQ(e[31].Length, 0xE10, "the last one ends mid-page");
    split_sound(e, n, 0x10, chunk, __LINE__);

    /* The caller's next chunk starts at (0x10 + 0x1FE00) mod 4 KB = 0xE10,
     * not at offset 0. */
    CHECK_EQ(XhciPipeSplit(0xE10, 0x100000 - 0x1FE00, 512, 34, 34, e, &n,
                           &chunk),
             XHCI_PIPE_OK, "the continuation of the unaligned 1 MB");
    CHECK_EQ(chunk, 0x1F000, "0x20000 - 0xE10 rounded down to 512");
    CHECK_EQ(n, 32, "32 elements again");
    split_sound(e, n, 0xE10, chunk, __LINE__);

    /* Not a power of two: 8 KB rounds down to 8 packets of 1000. */
    CHECK_EQ(XhciPipeSplit(0, 0x3000, 1000, 2, 34, e, &n, &chunk),
             XHCI_PIPE_OK, "Max Packet Size 1000");
    CHECK_EQ(chunk, 8000, "8 packets of 1000");
    CHECK_EQ(n, 2, "in two elements");
    CHECK_EQ(e[1].Length, 8000 - 0x1000, "the second ends mid-page");
    split_sound(e, n, 0, chunk, __LINE__);

    XhciPipeSplit(0x800, 0x3000, 512, 2, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x1800, "two map registers hold 0x1800 from 0x800");
    CHECK_EQ(n, 2, "in two elements");
    split_sound(e, n, 0x800, chunk, __LINE__);

    XhciPipeSplit(0x800, 0x3000, 512, 34, 1, e, &n, &chunk);
    CHECK_EQ(chunk, 0x800, "one element holds the rest of the first page");
    CHECK_EQ(n, 1, "in one element");

    XhciPipeSplit(0, 0x2000, 512, 1, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x1000, "one map register, one page");
    XhciPipeSplit(0, 0x1800, 512, 1, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x1000, "a page-aligned buffer's next chunk, at offset 0");
    XhciPipeSplit(0, 0x800, 512, 1, 34, e, &n, &chunk);
    CHECK_EQ(chunk, 0x800, "and the last one is short");

    n = 0xEEEE;
    CHECK_EQ(XhciPipeSplit(0x1000, 1, 512, 34, 34, e, &n, &chunk),
             XHCI_PIPE_BAD_PARAM, "an offset of a whole page");
    CHECK_EQ(n, 0xEEEE, "nothing written on a refusal");
    CHECK_EQ(XhciPipeSplit(0, 1, 512, 0, 34, e, &n, &chunk),
             XHCI_PIPE_BAD_PARAM, "no map register");
    CHECK_EQ(XhciPipeSplit(0, 1, 512, 34, 0, e, &n, &chunk),
             XHCI_PIPE_BAD_PARAM, "no element");
    CHECK_EQ(XhciPipeSplit(0, 1, 512, 34, 34, NULL, &n, &chunk),
             XHCI_PIPE_BAD_PARAM, "no element array");
    CHECK_EQ(XhciPipeSplit(0, 1, 0, 34, 34, e, &n, &chunk),
             XHCI_PIPE_BAD_PARAM, "Max Packet Size 0");
    CHECK_EQ(n, 0xEEEE, "nothing written for it");
    /* One element from 0xF00 holds 0x100 bytes, less than one 512-byte
     * packet, with more to follow: no chunk can be made. */
    chunk = 0xEEEE;
    CHECK_EQ(XhciPipeSplit(0xF00, 0x1000, 512, 34, 1, e, &n, &chunk),
             XHCI_PIPE_BAD_PARAM, "less than a packet fits, more to follow");
    CHECK_EQ(n, 0xEEEE, "nothing written");
    CHECK_EQ(chunk, 0xEEEE, "not even the chunk size");
    CHECK_EQ(XhciPipeSplit(0xF00, 0x100, 512, 34, 1, e, &n, &chunk),
             XHCI_PIPE_OK, "the same 0x100 as the whole transfer is fine");
    CHECK_EQ(chunk, 0x100, "a final short packet");
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
    CHECK_EQ(XhciPipeNtStatus(0xC0007000UL), 0xC000009DUL,
             "DEVICE_GONE is DEVICE_NOT_CONNECTED (design record 13 10.5)");
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

/* ------------------------------------------------------------------ */
/* Isochronous URBs                                                     */
/* ------------------------------------------------------------------ */

static void iso_table(XHCI_PIPE_ISO_PACKET *p, ULONG a, ULONG b, ULONG c)
{
    ULONG i;

    for (i = 0; i < 3; i++) {
        p[i].Length = 0xDEADBEEFUL;
        p[i].Status = 0xDEADBEEFUL;
    }
    p[0].Offset = a;
    p[1].Offset = b;
    p[2].Offset = c;
}

static void test_iso_check(void)
{
    XHCI_PIPE_ISO_PACKET p[63];
    ULONG i;

    iso_table(p, 0, 192, 384);
    CHECK_EQ(XhciPipeIsoCheck(p, 3, 576, 62, 0), XHCI_PIPE_OK,
             "10 ms of 48 kHz stereo, three packets");
    CHECK_EQ(XhciPipeIsoCheck(p, 3, 384, 62, 0), XHCI_PIPE_OK,
             "the last packet empty");
    CHECK_EQ(XhciPipeIsoCheck(p, 3, 383, 62, 0), XHCI_PIPE_BAD_PARAM,
             "an offset past the buffer");
    CHECK_EQ(XhciPipeIsoCheck(p, 3, 576, 62, 191), XHCI_PIPE_BAD_PARAM,
             "a packet above the Max ESIT Payload");
    CHECK_EQ(XhciPipeIsoCheck(p, 3, 576, 62, 192), XHCI_PIPE_OK,
             "every packet at the Max ESIT Payload");
    CHECK_EQ(XhciPipeIsoCheck(p, 0, 576, 62, 0), XHCI_PIPE_BAD_PARAM,
             "no packet");
    CHECK_EQ(XhciPipeIsoCheck(p, 3, 0, 62, 0), XHCI_PIPE_BAD_PARAM,
             "no buffer");
    CHECK_EQ(XhciPipeIsoCheck(NULL, 3, 576, 62, 0), XHCI_PIPE_BAD_PARAM,
             "no table");
    iso_table(p, 0, 200, 100);
    CHECK_EQ(XhciPipeIsoCheck(p, 3, 576, 62, 0), XHCI_PIPE_BAD_PARAM,
             "offsets decreasing");
    iso_table(p, 0, 0, 0);
    CHECK_EQ(XhciPipeIsoCheck(p, 3, 192, 62, 0), XHCI_PIPE_OK,
             "two empty packets, the last 192");
    for (i = 0; i < 63; i++) {
        p[i].Offset = i;
    }
    CHECK_EQ(XhciPipeIsoCheck(p, 63, 63, 62, 0), XHCI_PIPE_BAD_PARAM,
             "63 packets against a limit of 62");
    CHECK_EQ(XhciPipeIsoCheck(p, 62, 62, 62, 0), XHCI_PIPE_OK,
             "62 packets at the limit");

    iso_table(p, 0, 192, 384);
    CHECK_EQ(XhciPipeIsoLength(p, 3, 576, 0), 192, "length of packet 0");
    CHECK_EQ(XhciPipeIsoLength(p, 3, 576, 2), 192, "the last to the end");
    iso_table(p, 0, 100, 1000);
    CHECK_EQ(XhciPipeIsoLength(p, 3, 1000, 2), 0, "the last one empty");
    CHECK_EQ(XhciPipeIsoLength(p, 3, 1000, 1), 900, "a middle one");
}

static void frags_are(ULONG pageOffset, ULONG offset, ULONG length,
                      ULONG wantCount, ULONG want0, ULONG want1,
                      const char *what)
{
    ULONG lengths[2];
    ULONG n;

    lengths[0] = 0xDEADBEEFUL;
    lengths[1] = 0xDEADBEEFUL;
    n = XhciPipeIsoFragments(pageOffset, offset, length, lengths);
    CHECK_EQ(n, wantCount, what);
    if (n != 0 && wantCount != 0) {
        CHECK_EQ(lengths[0], want0, what);
        CHECK_EQ(lengths[1], want1, what);
    }
}

static void test_iso_fragments(void)
{
    frags_are(0, 0, 192, 1, 192, 0, "one page, one piece");
    frags_are(0xF80, 0, 192, 2, 128, 64, "the buffer starts near a page end");
    frags_are(0, 4000, 192, 2, 96, 96, "the packet crosses a page");
    frags_are(0, 3904, 192, 1, 192, 0, "ends exactly on the page");
    frags_are(0, 0, 4096, 1, 4096, 0, "a whole page");
    frags_are(1, 0, 4096, 2, 4095, 1, "a page's worth, one byte in");
    frags_are(0, 0, 4097, 0, 0, 0, "longer than a page");
    frags_are(4096, 0, 1, 0, 0, 0, "a page offset of a whole page");
    frags_are(0x10, 4064, 32, 2, 16, 16,
              "the buffer's page offset moves the cut");
    frags_are(0x10, 8176, 32, 1, 32, 0, "starts exactly on a page");
    frags_are(0, 0, 0, 1, 0, 0, "an empty packet is one empty piece");
    CHECK_EQ(XhciPipeIsoFragments(0, 0, 1, NULL), 0, "no output");
}

static void test_iso_frames(void)
{
    CHECK_EQ(XhciPipeIsoFrameOf(0, 3), 0, "FS 1 ms, packet 0");
    CHECK_EQ(XhciPipeIsoFrameOf(1, 3), 1, "FS 1 ms, packet 1");
    CHECK_EQ(XhciPipeIsoFrameOf(9, 3), 9, "FS 1 ms, packet 9");
    CHECK_EQ(XhciPipeIsoFrameOf(0, 0), 0, "HS 125 us, packet 0");
    CHECK_EQ(XhciPipeIsoFrameOf(7, 0), 0, "HS 125 us, packet 7");
    CHECK_EQ(XhciPipeIsoFrameOf(8, 0), 1, "HS 125 us, packet 8");
    CHECK_EQ(XhciPipeIsoFrameOf(15, 0), 1, "HS 125 us, packet 15");
    CHECK_EQ(XhciPipeIsoFrameOf(16, 0), 2, "HS 125 us, packet 16");
    CHECK_EQ(XhciPipeIsoFrameOf(3, 4), 6, "2 ms, packet 3");
    CHECK_EQ(XhciPipeIsoFrameOf(2, 6), 16, "FS bInterval 4, packet 2");

    CHECK_EQ(XhciPipeIsoFrames(10, 3), 10, "ten FS packets");
    CHECK_EQ(XhciPipeIsoFrames(8, 0), 1, "eight HS packets, one frame");
    CHECK_EQ(XhciPipeIsoFrames(9, 0), 2, "nine HS packets, two frames");
    CHECK_EQ(XhciPipeIsoFrames(1, 0), 1, "one HS packet, rounded up");
    CHECK_EQ(XhciPipeIsoFrames(3, 4), 6, "three 2 ms packets");
    CHECK_EQ(XhciPipeIsoFrames(62, 15), 253952, "the extremes");

    CHECK_EQ(XhciPipeIsoStartOk(1000, 1000, 895), XHCI_PIPE_OK, "now");
    CHECK_EQ(XhciPipeIsoStartOk(1895, 1000, 895), XHCI_PIPE_OK,
             "895 ahead, the window's end");
    CHECK_EQ(XhciPipeIsoStartOk(1896, 1000, 895), XHCI_PIPE_REFUSED,
             "896 ahead");
    CHECK_EQ(XhciPipeIsoStartOk(0, 1000, 895), XHCI_PIPE_OK, "1000 back");
    CHECK_EQ(XhciPipeIsoStartOk(0xFFFFFFF0UL, 1000, 895), XHCI_PIPE_OK,
             "1016 back, across the wrap");
    CHECK_EQ(XhciPipeIsoStartOk(0xFFFFFFE7UL, 1000, 895), XHCI_PIPE_REFUSED,
             "1025 back");
    CHECK_EQ(XhciPipeIsoStartOk(0x10, 0xFFFFFF00UL, 895), XHCI_PIPE_OK,
             "272 ahead, across the wrap");
    CHECK_EQ(XhciPipeIsoStartOk(0xFFFFFC05UL, 5, 895), XHCI_PIPE_OK,
             "1024 back, the inclusive edge");
}

static XHCI_SEQ64 seq64(ULONG hi, ULONG lo)
{
    XHCI_SEQ64 s;

    s.Hi = hi;
    s.Lo = lo;
    return s;
}

/* Submission sequences and abort horizons in the 64-bit space (hcd_io.c;
 * Codex reviews of ed025d2 and 09ed9d1): the count, the reconstruction of
 * a stamp from its low word, the order, the bound. */
static void test_seq(void)
{
    XHCI_SEQ64 c;
    XHCI_SEQ64 h;
    XHCI_SEQ64 f;

    c = seq64(0, 0xFFFFFFFEUL);
    XhciSeqNext(&c);
    CHECK(c.Hi == 0 && c.Lo == 0xFFFFFFFFUL, "count up");
    XhciSeqNext(&c);
    CHECK(c.Hi == 1 && c.Lo == 1, "the low word wraps and skips 0");

    c = seq64(3, 500);
    XhciSeqFromStamp(&c, 400, &f);
    CHECK(f.Hi == 3 && f.Lo == 400, "a stamp in this lap");
    XhciSeqFromStamp(&c, 500, &f);
    CHECK(f.Hi == 3 && f.Lo == 500, "the current value itself");
    XhciSeqFromStamp(&c, 0xFFFFFFFEUL, &f);
    CHECK(f.Hi == 2 && f.Lo == 0xFFFFFFFEUL, "a stamp from the lap before");

    /* Codex's first case: a request stamped 0xFFFFFFFE, then the low word
     * wraps and the abort takes 2. */
    c = seq64(1, 5);
    XhciSeqFromStamp(&c, 2, &h);
    CHECK(XhciSeqCovers(&h, 0xFFFFFFFEUL, &c),
          "a pre-wrap request is older than a post-wrap abort");
    h = seq64(0, 0xFFFFFFFEUL);
    CHECK(!XhciSeqCovers(&h, 2, &c),
          "a post-wrap request is newer than a pre-wrap abort");

    /* Codex's second case: horizon 100 kept through a full lap with no
     * other abort, then a request stamped 90 in the new lap, the count at
     * 110 - the request is newer and must not be covered. */
    h = seq64(0, 100);
    c = seq64(1, 110);
    CHECK(!XhciSeqCovers(&h, 90, &c),
          "an old horizon does not come back after a full lap");
    c = seq64(0, 110);
    CHECK(XhciSeqCovers(&h, 90, &c),
          "the same stamp in the horizon's own lap is covered");

    /* The bound: a lap is 2^32 - 1 values (a low word of 0 skipped). A
     * stamp of 0:1 followed by 0xFFFFFFFE submissions (count 0:FFFFFFFF)
     * still rebuilds exactly; followed by 0xFFFFFFFF (count 1:1) it
     * aliases into the latest lap and rebuilds as 1:1. */
    c = seq64(0, 1);
    XhciSeqNext(&c);
    CHECK(c.Hi == 0 && c.Lo == 2, "one submission later");
    c = seq64(0, 0xFFFFFFFFUL);
    XhciSeqFromStamp(&c, 1, &f);
    CHECK(f.Hi == 0 && f.Lo == 1,
          "0xFFFFFFFE submissions later: still exact");
    XhciSeqNext(&c);
    CHECK(c.Hi == 1 && c.Lo == 1,
          "0xFFFFFFFF submissions after 0:1 the count is 1:1");
    XhciSeqFromStamp(&c, 1, &f);
    CHECK(f.Hi == 1 && f.Lo == 1,
          "the boundary: the stamp aliases into the latest lap");

    /* Past the bound a request reads newer, so an earlier abort no longer
     * covers it; a later abort of its pipe still does. */
    h = seq64(5, 10);
    c = seq64(6, 20);
    CHECK(!XhciSeqCovers(&h, 5, &c),
          "past the bound an earlier abort no longer covers it");
    h = seq64(6, 15);
    CHECK(XhciSeqCovers(&h, 5, &c), "a later abort still covers it");

    h = seq64(0, 0);
    CHECK(!XhciSeqCovers(&h, 5, &c), "no horizon");
    h = seq64(6, 1);
    CHECK(!XhciSeqCovers(&h, 0, &c), "unstamped");

    h = seq64(0, 0);
    f = seq64(2, 7);
    XhciSeqLatest(&h, &f);
    CHECK(h.Hi == 2 && h.Lo == 7, "latest from none");
    f = seq64(1, 0xFFFFFFFFUL);
    XhciSeqLatest(&h, &f);
    CHECK(h.Hi == 2 && h.Lo == 7, "an earlier one does not lower it");
    f = seq64(3, 1);
    XhciSeqLatest(&h, &f);
    CHECK(h.Hi == 3 && h.Lo == 1, "a later one raises it");
    f = seq64(0, 0);
    XhciSeqLatest(&h, &f);
    CHECK(h.Hi == 3 && h.Lo == 1, "none changes nothing");
}

int main(void)
{
    test_seq();
    test_dci();
    test_qemu_mouse();
    test_intervals();
    test_bulk();
    test_high_bandwidth();
    test_interrupt_out();
    test_mps_limits();
    test_endpoint_refusals();
    test_walk_good();
    test_walk_malformed();
    test_plan();
    test_setup();
    test_raw_setup();
    test_split();
    test_status();
    test_iso_check();
    test_iso_fragments();
    test_iso_frames();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
