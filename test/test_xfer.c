/*
 * test_xfer.c - host tests for the control-transfer engine (src/xhci_xfer.c),
 * Phase 6 batch A.
 *
 * Three things are checked here that a VM cannot show and a review cannot
 * settle:
 *
 *   - **The bytes of a control transfer.** A Setup Stage TRB's immediate data,
 *     a Data Stage TRB's direction and TD Size, a Status Stage TRB's opposite
 *     direction: all of them are fields the controller reads and nothing
 *     reports back. A wrong TRT or a wrong Status DIR looks, from the guest,
 *     like a device that does not enumerate.
 *   - **The length arithmetic.** "requested - residual" is right for a
 *     single-TRB TD and wrong for every other one, and the difference only
 *     appears when a short packet lands on a TRB that is not the last. The
 *     vector for that is here, with the number the wrong formula would have
 *     produced named in the check so a future edit cannot quietly restore it.
 *   - **Which event ends a transfer.** A control transfer is two or three TDs
 *     and produces one, two or several events; the rule that picks the one that
 *     completes it is positional, and every off-by-one in it is a double
 *     completion or a leak.
 *
 * Per docs/contributing/design/03-host-unit-tests.md, expected values are transcribed
 * by hand from docs/usb-xhci-info/xhci-data-structures.md section 7 and from the spec pages
 * cited beside them - never produced by the code under test. The TRB control
 * words below are written as literal hexadecimal for that reason.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci_xfer.h"

static int failures;
static int checks;

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s:%d: %s\n", "test_xfer.c", line, what);
    }
}

#define CHECK_EQ(got, want, what) \
    check_eq_impl((unsigned long)(got), (unsigned long)(want), (what), __LINE__)

static void check_eq_impl(unsigned long got, unsigned long want,
                          const char *what, int line)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s:%d: %s (got %lu / 0x%lX, want %lu / 0x%lX)\n",
               "test_xfer.c", line, what, got, got, want, want);
    }
}

#define RING_PA 0x0F001000UL

/* The USBD_STATUS values this driver may produce, hand-typed a second time
 * from C:\NTDDK\inc\usbdi.h - the Windows 2000 DDK header, which is the one
 * both targets' USB stacks were built from and which does *not* define the
 * XACT_ERROR / BABBLE_DETECTED / DATA_BUFFER_ERROR names later WDKs added. */
#define WANT_USBD_SUCCESS            0x00000000UL
#define WANT_USBD_STALL_PID          0xC0000004UL
#define WANT_USBD_DEV_NOT_RESPONDING 0xC0000005UL
#define WANT_USBD_DATA_OVERRUN       0xC0000008UL
#define WANT_USBD_BUFFER_OVERRUN     0xC000000CUL
#define WANT_USBD_INTERNAL_HC_ERROR  0x80000800UL
#define WANT_USBD_CANCELED           0x00010000UL

/* The three Stopped completion codes, transcribed from Table 6-90 rather than
 * taken from the header the code under test uses - the same rule the USBD
 * statuses above follow. */
#define WANT_CC_STOPPED                26UL
#define WANT_CC_STOPPED_LENGTH_INVALID 27UL
#define WANT_CC_STOPPED_SHORT_PACKET   28UL

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/*
 * A scatter/gather list longer than the two elements the ABI struct declares.
 * usbport allocates the real one variable-length; this is the same shape.
 */
typedef struct _SG_BUFFER {
    USBPORT_SCATTER_GATHER_LIST List;
    USBPORT_SCATTER_GATHER_ELEMENT More[XHCI_XFER_MAX_DATA_TRBS];
} SG_BUFFER;

static void sg_init(SG_BUFFER *sg)
{
    ULONG i;
    USBPORT_SCATTER_GATHER_ELEMENT *e;

    sg->List.Flags = 0;
    sg->List.CurrentVa = 0;
    sg->List.MappedSystemVa = NULL;
    sg->List.SgElementCount = 0;
    e = &sg->List.SgElement[0];
    for (i = 0; i < XHCI_XFER_MAX_DATA_TRBS + 2; i++) {
        e[i].SgPhysicalAddressLo = 0;
        e[i].SgPhysicalAddressHi = 0;
        e[i].Reserved1 = 0;
        e[i].SgTransferLength = 0;
        e[i].SgOffset = 0;
        e[i].Reserved2 = 0;
    }
}

static void sg_add(SG_BUFFER *sg, ULONG pa, ULONG length, ULONG offset)
{
    USBPORT_SCATTER_GATHER_ELEMENT *e;

    e = &sg->List.SgElement[sg->List.SgElementCount];
    e->SgPhysicalAddressLo = pa;
    e->SgPhysicalAddressHi = 0;
    e->SgTransferLength = length;
    e->SgOffset = offset;
    sg->List.SgElementCount++;
}

static void request_init(XHCI_CONTROL_REQUEST *req,
                         UCHAR bmRequestType,
                         UCHAR bRequest,
                         USHORT wValue,
                         USHORT wIndex,
                         USHORT wLength,
                         ULONG transferLength,
                         ULONG maxPacketSize,
                         const SG_BUFFER *sg)
{
    req->Setup.bmRequestType = bmRequestType;
    req->Setup.bRequest = bRequest;
    req->Setup.wValue = wValue;
    req->Setup.wIndex = wIndex;
    req->Setup.wLength = wLength;
    req->TransferLength = transferLength;
    req->TransferFlagsIn = (bmRequestType & 0x80) ? 1 : 0;
    req->MaxPacketSize = maxPacketSize;
    req->SgList = (sg != NULL) ? &sg->List : NULL;
}

/* A Transfer Event's DW2 and DW3, assembled from the field positions in
 * docs/usb-xhci-info/xhci-data-structures.md section 7 rather than from a builder. */
static ULONG event_dw2(ULONG completionCode, ULONG residual)
{
    return (residual & 0x00FFFFFFUL) | ((completionCode & 0xFFUL) << 24);
}

static ULONG event_dw3(ULONG slotId, ULONG dci)
{
    return (32UL << 10) | ((dci & 0x1FUL) << 16) | ((slotId & 0xFFUL) << 24);
}

/* ------------------------------------------------------------------ */
/* 1. Completion-code mapping (task 6-A.3)                             */
/* ------------------------------------------------------------------ */

static void expect_code(ULONG completionCode,
                        ULONG class_,
                        ULONG usbdStatus,
                        ULONG residualIsBytes,
                        ULONG fatal,
                        const char *what)
{
    XHCI_XFER_CODE info;

    CHECK_EQ(XhciXferCodeInfo(completionCode, &info), XHCI_XFER_OK, what);
    CHECK_EQ(info.Class, class_, what);
    CHECK_EQ((ULONG)info.UsbdStatus, usbdStatus, what);
    CHECK_EQ(info.ResidualIsBytes, residualIsBytes, what);
    CHECK_EQ(info.Fatal, fatal, what);
}

/* Separate from `expect_code` rather than a seventh parameter on it: the flag
 * is set by exactly one code, so threading it through all forty call sites
 * would state "not this one" thirty-nine times to state it once. */
static void expect_slot_fatal(ULONG completionCode,
                              ULONG slotFatal,
                              const char *what)
{
    XHCI_XFER_CODE info;

    CHECK_EQ(XhciXferCodeInfo(completionCode, &info), XHCI_XFER_OK, what);
    CHECK_EQ(info.SlotFatal, slotFatal, what);
}

static void expect_code_rejected(ULONG completionCode, const char *what)
{
    XHCI_XFER_CODE info;

    CHECK_EQ(XhciXferCodeInfo(completionCode, &info), XHCI_XFER_BAD_PARAM, what);
    /* And the caller that ignores the return still sees a refusal, not a
     * leftover: "unknown or impossible codes fail visibly rather than being
     * treated as success" (roadmap task 6-A.3). */
    CHECK_EQ(info.Class, XHCI_XFER_CC_INVALID, what);
    CHECK_EQ((ULONG)info.UsbdStatus, WANT_USBD_INTERNAL_HC_ERROR, what);
}

static void test_completion_code_mapping(void)
{
    XHCI_XFER_CODE info;

    expect_code(1, XHCI_XFER_CC_SUCCESS, WANT_USBD_SUCCESS, 1, 0, "Success");
    /* Short Packet is a successful completion carrying a length. Whether a
     * short transfer is an error is USBD_SHORT_TRANSFER_OK's question, and
     * that flag lives in an URB this layer never sees. */
    expect_code(13, XHCI_XFER_CC_SHORT, WANT_USBD_SUCCESS, 1, 0, "Short Packet");

    expect_code(6, XHCI_XFER_CC_ERROR, WANT_USBD_STALL_PID, 1, 0, "Stall");
    expect_code(4, XHCI_XFER_CC_ERROR, WANT_USBD_DEV_NOT_RESPONDING, 1, 0,
                "USB Transaction Error");
    expect_code(20, XHCI_XFER_CC_ERROR, WANT_USBD_DEV_NOT_RESPONDING, 1, 0,
                "No Ping Response");
    expect_code(36, XHCI_XFER_CC_ERROR, WANT_USBD_DEV_NOT_RESPONDING, 1, 0,
                "Split Transaction Error");
    expect_code(3, XHCI_XFER_CC_ERROR, WANT_USBD_BUFFER_OVERRUN, 1, 0,
                "Babble Detected");
    expect_code(2, XHCI_XFER_CC_ERROR, WANT_USBD_DATA_OVERRUN, 1, 0,
                "Data Buffer Error");
    expect_code(5, XHCI_XFER_CC_ERROR, WANT_USBD_INTERNAL_HC_ERROR, 1, 0,
                "TRB Error");
    expect_code(22, XHCI_XFER_CC_ERROR, WANT_USBD_INTERNAL_HC_ERROR, 1, 0,
                "Incompatible Device");
    expect_code(34, XHCI_XFER_CC_ERROR, WANT_USBD_INTERNAL_HC_ERROR, 1, 0,
                "Invalid Stream ID");

    /*
     * The controller-level failures. **Event Lost was the only one here until
     * audit round 8**, which found Table 6-90 marking a second in as many words:
     * "An Undefined Error shall be treated as a fatal error by software" (33,
     * p.469). The status handed to usbport is unchanged for both - there is no
     * USB-level equivalent of either - and what changed is the treatment.
     */
    expect_code(32, XHCI_XFER_CC_ERROR, WANT_USBD_INTERNAL_HC_ERROR, 1, 1,
                "Event Lost is fatal");
    expect_code(33, XHCI_XFER_CC_ERROR, WANT_USBD_INTERNAL_HC_ERROR, 1, 1,
                "Undefined Error is fatal, because the table says so");

    /* 26-28: software stopped the ring, so the transfers on it are canceled.
     * Stopped - Short Packet reports the EDTLA in the length field rather than
     * a residual (Table 6-38, p.440), and Stopped - Length Invalid says its
     * length is not usable in its name. */
    expect_code(26, XHCI_XFER_CC_CANCELED, WANT_USBD_CANCELED, 1, 0, "Stopped");
    expect_code(27, XHCI_XFER_CC_CANCELED, WANT_USBD_CANCELED, 0, 0,
                "Stopped - Length Invalid carries no byte count");
    expect_code(28, XHCI_XFER_CC_CANCELED, WANT_USBD_CANCELED, 0, 0,
                "Stopped - Short Packet reports EDTLA, not a residual");

    /*
     * Table 6-90's vendor ranges carry their own default reading, and **audit
     * round 8 found the error range taking half of it**: "If software does not
     * recognize the code, it shall interpret this range of vendor defined values
     * as a Undefined Error condition" (p.470). This driver recognises none of
     * them, so the whole range is that condition - fatal included, or the
     * interpretation stops exactly where it becomes load-bearing.
     */
    expect_code(192, XHCI_XFER_CC_ERROR, WANT_USBD_INTERNAL_HC_ERROR, 1, 1,
                "vendor error floor");
    expect_code(223, XHCI_XFER_CC_ERROR, WANT_USBD_INTERNAL_HC_ERROR, 1, 1,
                "vendor error ceiling");
    expect_code(224, XHCI_XFER_CC_SUCCESS, WANT_USBD_SUCCESS, 1, 0,
                "vendor information floor is Success");
    expect_code(255, XHCI_XFER_CC_SUCCESS, WANT_USBD_SUCCESS, 1, 0,
                "vendor information ceiling is Success");

    /*
     * `SlotFatal` is the other half of round 8's finding and is deliberately
     * *not* `Fatal`: Incompatible Device Error is "fatal as far as the Slot is
     * concerned. Software shall issue a Disable Slot Command to recover" (22,
     * p.468), so it takes one device down and not the controller. Checked with
     * its controls, because a flag that is set for everything says nothing.
     */
    expect_slot_fatal(22, 1, "Incompatible Device is fatal to the slot");
    expect_slot_fatal(33, 0, "Undefined Error is fatal to the controller, "
                             "not to one slot");
    expect_slot_fatal(32, 0, "nor is Event Lost a slot's error");
    expect_slot_fatal(5, 0, "and TRB Error is neither");
    expect_slot_fatal(192, 0, "nor a vendor error");

    /* Everything else: unassigned, isoch-only, command-only, or a code Table
     * 6-90 gives to an event family that is not a Transfer Event. */
    expect_code_rejected(0, "Invalid");
    expect_code_rejected(7, "Resource Error is a command result");
    expect_code_rejected(8, "Bandwidth Error is a command result");
    expect_code_rejected(9, "No Slots Available is a command result");
    expect_code_rejected(11, "Slot Not Enabled is a command result");
    /*
     * **Code 12 is not a command result, and audit round 9 found this line
     * saying it was.** Table 6-90 p.467 asserts it "if a doorbell is rung for an
     * endpoint that is in the Disabled state", and 4.7 p.143 says the xHC
     * "should generate a Transfer Event TRB with the TRB Pointer, TRB Transfer
     * Length, Event Data (ED) fields set to '0'" for it. So it belongs to this
     * family and is refused for the reason 14 and 15 are refused - it carries no
     * usable TRB pointer - rather than for belonging to another one. That it is
     * refused and not acted on *here* is a deviation recorded in
     * docs/contributing/implementation-invariants.md, "Fatal Errors"; recovery
     * is delayed to usbport's timeout rather than absent, which audit round 10
     * corrected.
     */
    expect_code_rejected(12, "Endpoint Not Enabled has no valid TRB pointer "
                             "either - it is a pointerless Transfer Event, not "
                             "a command result");
    expect_code_rejected(14, "Ring Underrun has no valid TRB pointer");
    expect_code_rejected(15, "Ring Overrun has no valid TRB pointer");
    expect_code_rejected(17, "Parameter Error is a command result");
    expect_code_rejected(18, "Bandwidth Overrun is isoch-only");
    expect_code_rejected(19, "Context State Error is a command result");
    expect_code_rejected(21, "Event Ring Full is a Host Controller Event");
    expect_code_rejected(23, "Missed Service is isoch-only");
    expect_code_rejected(24, "Command Ring Stopped belongs to the command ring");
    expect_code_rejected(25, "Command Aborted belongs to the command ring");
    expect_code_rejected(30, "reserved");
    expect_code_rejected(31, "Isoch Buffer Overrun is isoch-only");
    expect_code_rejected(35, "Secondary Bandwidth Error is a command result");
    expect_code_rejected(191, "the gap below the vendor ranges");
    expect_code_rejected(256, "out of the field's range entirely");

    CHECK_EQ(XhciXferCodeInfo(1, NULL), XHCI_XFER_BAD_PARAM, "NULL output");

    /* The set this accepts must be exactly the set the ring layer will accept
     * for an endpoint ring, or one of them classifies an event the other
     * refuses. Checked against XhciRingClassifyEvent rather than asserted. */
    {
        XHCI_TRB mem[8];
        XHCI_TRB td;
        XHCI_TD_COMPLETION completion;
        XHCI_RING ring;
        ULONG code;
        ULONG pa;
        ULONG seen;
        ULONG agree;

        CHECK_EQ(XhciRingInit(&ring, mem, RING_PA, 8, XHCI_RING_KIND_ENDPOINT),
                 XHCI_RING_OK, "ring init");
        XhciTrbClear(&td);
        td.Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL) | XHCI_TRB_IOC;
        CHECK_EQ(XhciRingEnqueue(&ring, &td, &pa), XHCI_RING_OK, "one TRB");

        /*
         * Both halves are asserted *after* the loop, and that is the point
         * rather than tidiness. The disagreement check inside fires only when
         * the two decoders differ, so on its own it cannot tell "they agreed
         * everywhere" from "the loop never ran" - a `CHECK(1, ...)` behind it
         * passes just as green if the body is deleted. Counting the iterations
         * and accumulating the verdict makes the absence of the comparison a
         * failure instead of a silence.
         */
        seen = 0;
        agree = 1;
        for (code = 0; code <= 255; code++) {
            ULONG xferOk;
            ULONG ringOk;

            seen++;
            xferOk = (XhciXferCodeInfo(code, &info) == XHCI_XFER_OK) ? 1 : 0;
            ringOk = (XhciRingClassifyEvent(&ring, pa, code, &completion) !=
                      XHCI_RING_BAD_COMPLETION) ? 1 : 0;
            if (xferOk != ringOk) {
                agree = 0;
                CHECK_EQ(xferOk, ringOk,
                         "the two decoders disagree about a completion code");
            }
        }
        CHECK_EQ(seen, 256, "all 256 completion codes were compared");
        CHECK(agree, "the two decoders agree on all 256 completion codes");
    }
}

/* ------------------------------------------------------------------ */
/* 2. Building a control transfer (task 6-A.1)                         */
/* ------------------------------------------------------------------ */

/*
 * SET_ADDRESS is the no-data shape and the one every enumeration starts with.
 * It is also the transfer that must never reach a ring at all (task 6-B.3
 * intercepts it), which is exactly why the *shape* is pinned here: the
 * interception is a decision made above this layer, and this layer has to build
 * a correct no-data TD for every other zero-length request - SET_CONFIGURATION,
 * SET_INTERFACE, CLEAR_FEATURE.
 */
static void test_build_no_data(void)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_CONTROL_LAYOUT layout;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];

    /* bmRequestType 0x00, bRequest 5, wValue 3, wIndex 0, wLength 0. */
    request_init(&req, 0x00, 0x05, 3, 0, 0, 0, 8, NULL);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_OK, "no-data control transfer built");

    CHECK_EQ(layout.TrbCount, 2, "Setup + Status, no Data Stage TD");
    CHECK_EQ(layout.DataCount, 0, "no data TRBs");
    CHECK_EQ(layout.DataFirst, XHCI_XFER_NO_INDEX, "and no data position");
    CHECK_EQ(layout.TdCount, 2, "two TDs (spec 6.4.1.2)");
    CHECK_EQ(layout.TdLengths[0], 1, "Setup Stage TD is one TRB");
    CHECK_EQ(layout.TdLengths[1], 1, "Status Stage TD is one TRB");

    /* Setup Stage TRB, Figure 6-9 / Tables 6-23..6-26. DW0 is
     * bmRequestType | bRequest << 8 | wValue << 16 = 0x00 | 0x0500 | 0x30000. */
    CHECK_EQ(out[0].Param0, 0x00030500UL, "SETUP bytes 0-3 as immediate data");
    CHECK_EQ(out[0].Param1, 0x00000000UL, "wIndex 0, wLength 0");
    CHECK_EQ(out[0].Status, 8UL, "TRB Transfer Length is always 8");
    /* Type 2 << 10 = 0x800, IDT = 0x40, TRT = 0 (No Data Stage). */
    CHECK_EQ(out[0].Control, 0x00000840UL, "Setup Stage control word");
    CHECK_EQ(out[0].Control & XHCI_TRB_IOC, 0,
             "no IOC: 'The IOC flag should only be set in the Status Stage TRB'");
    CHECK_EQ(out[0].Control & XHCI_TRB_CH, 0,
             "'Each Setup Stage TD shall contain a single Setup Stage TRB'");

    /* Status Stage TRB, Table 6-31. Type 4 << 10 = 0x1000, IOC = 0x20,
     * DIR = IN because there is no data stage (Table 4-7). */
    CHECK_EQ(out[1].Param0, 0, "Status Stage parameter is RsvdZ");
    CHECK_EQ(out[1].Param1, 0, "and its high half too");
    CHECK_EQ(out[1].Status, 0, "DW2 bits 21:0 are RsvdZ");
    CHECK_EQ(out[1].Control, 0x00011020UL, "Status Stage control word");
    CHECK_EQ(out[1].Control & XHCI_TRB_CH, 0, "and it ends its own TD");
}

static void test_build_in_data(void)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_CONTROL_LAYOUT layout;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];
    SG_BUFFER sg;

    /* GET_DESCRIPTOR(Device), 18 bytes, one mapped fragment, EP0 MPS 8 - the
     * first real transfer of every enumeration. */
    sg_init(&sg);
    sg_add(&sg, 0x0A123000UL, 18, 0);
    request_init(&req, 0x80, 0x06, 0x0100, 0, 18, 18, 8, &sg);

    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_OK, "IN control transfer built");
    CHECK_EQ(layout.TrbCount, 3, "Setup + Data + Status");
    CHECK_EQ(layout.DataFirst, 1, "the data stage follows the setup stage");
    CHECK_EQ(layout.DataCount, 1, "one fragment, one TRB");
    CHECK_EQ(layout.TdCount, 3, "three TDs");
    CHECK_EQ(layout.TdLengths[1], 1, "the Data Stage TD is one TRB");

    CHECK_EQ(out[0].Param0, 0x01000680UL, "bmRequestType 80, bRequest 06, wValue 0100");
    CHECK_EQ(out[0].Param1, 0x00120000UL, "wIndex 0, wLength 18");
    /* TRT = 3 (IN Data Stage) << 16 = 0x30000, plus type 2 and IDT. */
    CHECK_EQ(out[0].Control, 0x00030840UL, "Setup Stage declares an IN data stage");

    CHECK_EQ(out[1].Param0, 0x0A123000UL, "the mapped physical address");
    CHECK_EQ(out[1].Param1, 0, "no 64-bit DMA, ever");
    /* 18 bytes, and TD Size 0 because this is the TD's last transfer TRB. */
    CHECK_EQ(out[1].Status, 18UL, "length 18, TD Size 0 on the last TRB");
    /* Type 3 << 10 = 0xC00, ISP = 0x4, DIR = IN = 0x10000. */
    CHECK_EQ(out[1].Control, 0x00010C04UL, "Data Stage control word");
    CHECK_EQ(out[1].Control & XHCI_TRB_IOC, 0, "IOC is the Status TRB's alone");
    CHECK_EQ(out[1].Control & XHCI_TRB_CH, 0,
             "'The Chain bit is always 0 in the last TRB of a Data Stage TD'");

    /* Status direction is the *opposite* of the data stage's (Table 4-7). */
    CHECK_EQ(out[2].Control, 0x00001020UL,
             "an IN data stage takes an OUT status stage");
}

static void test_build_out_data(void)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_CONTROL_LAYOUT layout;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];
    SG_BUFFER sg;

    sg_init(&sg);
    sg_add(&sg, 0x0A200000UL, 64, 0);
    /* A vendor OUT request: bmRequestType 0x40, 64 bytes of payload. */
    request_init(&req, 0x40, 0x09, 0, 0, 64, 64, 64, &sg);

    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_OK, "OUT control transfer built");
    /* TRT = 2 (OUT Data Stage) << 16 = 0x20000. */
    CHECK_EQ(out[0].Control, 0x00020840UL, "Setup Stage declares an OUT data stage");
    /* Type 3 << 10, no DIR, and no ISP: a short packet is something a device
     * signals on an IN, not something the host can detect on an OUT. */
    CHECK_EQ(out[1].Control, 0x00000C00UL, "Data Stage OUT carries neither DIR nor ISP");
    CHECK_EQ(out[2].Control, 0x00011020UL,
             "an OUT data stage takes an IN status stage");
}

/*
 * TD Size is a *packet* count, computed by the formula at spec 4.11.2.4, and it
 * is the one field in a control TD that needs EP0's Max Packet Size. Worked
 * through by hand here: 320 bytes at MPS 64 is a TD Packet Count of 5.
 *
 *   TRB 1: sum 128 -> transferred 2 -> TD Size = 5 - 2 = 3
 *   TRB 2: sum 256 -> transferred 4 -> TD Size = 5 - 4 = 1
 *   TRB 3: the last TRB of the TD -> TD Size = 0
 */
static void test_build_td_size(void)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_CONTROL_LAYOUT layout;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];
    SG_BUFFER sg;

    sg_init(&sg);
    sg_add(&sg, 0x0A300000UL, 128, 0);
    sg_add(&sg, 0x0A400000UL, 128, 128);
    sg_add(&sg, 0x0A500000UL, 64, 256);
    request_init(&req, 0x80, 0x06, 0, 0, 320, 320, 64, &sg);

    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_OK, "three-fragment IN transfer built");
    CHECK_EQ(layout.DataCount, 3, "three data TRBs");
    CHECK_EQ(layout.TdLengths[1], 3, "one Data Stage TD of three TRBs");

    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[1].Status), 128, "TRB 1 length");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[1].Status), 3, "TRB 1 TD Size");
    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[2].Status), 128, "TRB 2 length");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[2].Status), 1, "TRB 2 TD Size");
    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[3].Status), 64, "TRB 3 length");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[3].Status), 0,
             "'The value of the TD Size in the last Transfer TRB of a TD shall "
             "be cleared to 0'");

    /* Chain within the Data Stage TD, and only within it. */
    CHECK_EQ(out[0].Control & XHCI_TRB_CH, 0, "Setup TD ends at its own TRB");
    CHECK_EQ(out[1].Control & XHCI_TRB_CH, XHCI_TRB_CH, "data TRB 1 chains");
    CHECK_EQ(out[2].Control & XHCI_TRB_CH, XHCI_TRB_CH, "data TRB 2 chains");
    CHECK_EQ(out[3].Control & XHCI_TRB_CH, 0, "data TRB 3 ends the TD");
    CHECK_EQ(out[4].Control & XHCI_TRB_CH, 0, "Status TD ends at its own TRB");

    /* The first data TRB is a Data Stage TRB and carries the direction for the
     * whole TD; the rest are Normal TRBs, which inherit it (p.191). */
    CHECK_EQ(XHCI_TRB_GET_TYPE(out[1].Control), 3, "first data TRB is Data Stage");
    CHECK_EQ(XHCI_TRB_GET_TYPE(out[2].Control), 1, "the rest are Normal");
    CHECK_EQ(XHCI_TRB_GET_TYPE(out[3].Control), 1, "the rest are Normal");
    CHECK_EQ(out[2].Control & XHCI_TRB_DIR_IN, 0,
             "a Normal TRB has no DIR field to set");
    CHECK_EQ(out[2].Control & XHCI_TRB_ISP, XHCI_TRB_ISP,
             "but every IN data TRB carries ISP, so a short packet anywhere "
             "reports itself");

    /* And exactly one TRB in the whole transfer carries IOC. */
    {
        ULONG i;
        ULONG ioc;

        ioc = 0;
        for (i = 0; i < layout.TrbCount; i++) {
            if (out[i].Control & XHCI_TRB_IOC) {
                ioc++;
            }
        }
        CHECK_EQ(ioc, 1, "exactly one IOC, on the Status Stage TRB");
        CHECK(out[layout.TrbCount - 1].Control & XHCI_TRB_IOC,
              "and it is the last TRB");
    }
}

/* TD Size saturates at 31 because the field is five bits (p.199). */
static void test_build_td_size_saturates(void)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_CONTROL_LAYOUT layout;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];
    SG_BUFFER sg;
    ULONG i;

    /* 16 pages at MPS 8 = 8192 packets in the TD. */
    sg_init(&sg);
    for (i = 0; i < 16; i++) {
        sg_add(&sg, 0x0B000000UL + i * 0x1000UL, 4096, i * 4096);
    }
    request_init(&req, 0x80, 0x06, 0, 0, 0, 16 * 4096, 8, &sg);

    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_OK, "16-fragment transfer built");
    CHECK_EQ(layout.DataCount, 16, "one TRB per page - no 64 KB split needed");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[1].Status), 31, "saturated at 31");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[15].Status), 31, "still saturated");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[16].Status), 0, "and 0 on the last");
}

/*
 * "If a physical data buffer spans a 64KB boundary, software shall chain
 * multiple TRBs to describe the buffer" (spec 6.4.1 note). Keeping the length
 * under 64 KB is not sufficient - an element may start anywhere.
 */
static void test_build_64k_split(void)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_CONTROL_LAYOUT layout;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];
    SG_BUFFER sg;

    /* One fragment from 0x0001F000 for 0x30000 bytes crosses three boundaries.
     * Split points are 0x20000, 0x30000 and 0x40000, so the pieces are
     * 0x1000, 0x10000, 0x10000 and the remaining 0xF000. */
    sg_init(&sg);
    sg_add(&sg, 0x0001F000UL, 0x30000UL, 0);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 0x30000UL, 64, &sg);

    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_OK, "boundary-crossing fragment built");
    CHECK_EQ(layout.DataCount, 4, "one element, four TRBs");

    CHECK_EQ(out[1].Param0, 0x0001F000UL, "piece 1 address");
    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[1].Status), 0x1000UL, "piece 1 length");
    CHECK_EQ(out[2].Param0, 0x00020000UL, "piece 2 starts on the boundary");
    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[2].Status), 0x10000UL,
             "a full 64 KB fits in the 17-bit length field");
    CHECK_EQ(out[3].Param0, 0x00030000UL, "piece 3 address");
    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[3].Status), 0x10000UL, "piece 3 length");
    CHECK_EQ(out[4].Param0, 0x00040000UL, "piece 4 address");
    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[4].Status), 0x0F000UL, "piece 4 length");

    /* All four are one TD: chained except the last. */
    CHECK_EQ(out[1].Control & XHCI_TRB_CH, XHCI_TRB_CH, "piece 1 chains");
    CHECK_EQ(out[4].Control & XHCI_TRB_CH, 0, "piece 4 ends the TD");

    /* A fragment that ends exactly on a boundary is not split. */
    sg_init(&sg);
    sg_add(&sg, 0x00030000UL, 0x10000UL, 0);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 0x10000UL, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_OK, "aligned 64 KB fragment built");
    CHECK_EQ(layout.DataCount, 1, "a fragment that fills one block is one TRB");
    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[1].Status), 0x10000UL, "and its length");

    /* The last usable 64 KB block: the modular subtraction that computes the
     * distance to the next boundary wraps here, and must still be right. */
    sg_init(&sg);
    sg_add(&sg, 0xFFFF0000UL, 0x10000UL, 0);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 0x10000UL, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_OK, "a fragment ending at 0xFFFFFFFF is legal");
    CHECK_EQ(layout.DataCount, 1, "and needs no split");
    CHECK_EQ(XHCI_TRB_GET_LENGTH(out[1].Status), 0x10000UL, "its length");

    /* One byte more would run off the top of the address space. */
    sg_init(&sg);
    sg_add(&sg, 0xFFFF0000UL, 0x10001UL, 0);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 0x10001UL, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_SG_MISMATCH, "a fragment that wraps 4 GB is refused");
}

/*
 * The whole SG list, in SgOffset order - not in array order. Whether usbport
 * hands the elements over in offset order is precisely what task 6-V.1
 * measures; selecting by offset is the answer that is correct either way.
 */
static void test_build_sg_order(void)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_CONTROL_LAYOUT layout;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];
    SG_BUFFER sg;

    sg_init(&sg);
    sg_add(&sg, 0x0C002000UL, 4096, 8192);      /* third by offset  */
    sg_add(&sg, 0x0C000000UL, 4096, 0);         /* first by offset  */
    sg_add(&sg, 0x0C001000UL, 4096, 4096);      /* second by offset */
    request_init(&req, 0x80, 0x06, 0, 0, 0, 12288, 64, &sg);

    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_OK, "out-of-order SG list built");
    CHECK_EQ(layout.DataCount, 3, "three TRBs");
    CHECK_EQ(out[1].Param0, 0x0C000000UL, "TRB 1 is the offset-0 element");
    CHECK_EQ(out[2].Param0, 0x0C001000UL, "TRB 2 is the offset-4096 element");
    CHECK_EQ(out[3].Param0, 0x0C002000UL, "TRB 3 is the offset-8192 element");
}

static void test_build_refusals(void)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_CONTROL_LAYOUT layout;
    XHCI_TRB out[XHCI_XFER_MAX_CONTROL_TRBS];
    SG_BUFFER sg;
    ULONG i;

    /* A gap: nothing describes bytes 4096..8191. */
    sg_init(&sg);
    sg_add(&sg, 0x0D000000UL, 4096, 0);
    sg_add(&sg, 0x0D002000UL, 4096, 8192);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 12288, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_SG_MISMATCH, "a gap in the offsets");

    /* Two elements claiming the same offset: the second is unreachable, so the
     * consumed count does not add up. */
    sg_init(&sg);
    sg_add(&sg, 0x0D000000UL, 4096, 0);
    sg_add(&sg, 0x0D001000UL, 4096, 0);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 4096, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_SG_MISMATCH, "duplicate offsets");

    /* An element past the end of the buffer. */
    sg_init(&sg);
    sg_add(&sg, 0x0D000000UL, 4096, 0);
    sg_add(&sg, 0x0D001000UL, 4096, 4096);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 4096, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_SG_MISMATCH, "an element the transfer does not cover");

    /* An element longer than the bytes remaining. */
    sg_init(&sg);
    sg_add(&sg, 0x0D000000UL, 8192, 0);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 4096, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_SG_MISMATCH, "an element that overruns the buffer");

    /* A zero-length element cannot advance the cursor, so it is a malformed
     * list rather than a no-op. */
    sg_init(&sg);
    sg_add(&sg, 0x0D000000UL, 0, 0);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 4096, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_SG_MISMATCH, "a zero-length element");

    /* An empty list for a transfer that has bytes to move. */
    sg_init(&sg);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 4096, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_SG_MISMATCH,
             "no elements at all is a list that does not describe the buffer - "
             "the tiling walk answers it, with no special case of its own");

    /* The high DWORD is *checked*, never assumed: usbport stores whatever the
     * HAL returned and does not mask it. */
    sg_init(&sg);
    sg_add(&sg, 0x0D000000UL, 4096, 0);
    sg.List.SgElement[0].SgPhysicalAddressHi = 1;
    request_init(&req, 0x80, 0x06, 0, 0, 0, 4096, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_SG_HIGH_ADDRESS, "a physical address above 4 GB");

    /* Direction: the SETUP bytes say IN, usbport's flag says OUT. */
    sg_init(&sg);
    sg_add(&sg, 0x0D000000UL, 4096, 0);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 4096, 64, &sg);
    req.TransferFlagsIn = 0;
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_DIRECTION_CONFLICT, "SETUP and TransferFlags disagree");
    /* ...and the other way round. */
    request_init(&req, 0x00, 0x09, 0, 0, 0, 4096, 64, &sg);
    req.TransferFlagsIn = 1;
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_DIRECTION_CONFLICT, "and the opposite disagreement");
    /* A zero-length transfer has no data stage, so there is no direction to
     * disagree about and usbport's flag is not consulted. */
    request_init(&req, 0x00, 0x05, 3, 0, 0, 0, 8, NULL);
    req.TransferFlagsIn = 1;
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_OK, "no data stage, no direction conflict");

    /* EP0's Max Packet Size is one of exactly four values (USB2 9.6.1). */
    request_init(&req, 0x00, 0x05, 3, 0, 0, 0, 0, NULL);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_BAD_PARAM, "MPS 0 would divide by zero");
    request_init(&req, 0x00, 0x05, 3, 0, 0, 0, 512, NULL);
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_BAD_PARAM, "512 is not a legal EP0 Max Packet Size");
    for (i = 0; i < 4; i++) {
        static const ULONG legal[4] = { 8, 16, 32, 64 };

        request_init(&req, 0x00, 0x05, 3, 0, 0, 0, legal[i], NULL);
        CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                      &layout),
                 XHCI_XFER_OK, "a legal EP0 Max Packet Size");
    }

    /* Scratch space the caller sized too small. */
    sg_init(&sg);
    sg_add(&sg, 0x0D000000UL, 4096, 0);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 4096, 64, &sg);
    CHECK_EQ(XhciXferBuildControl(&req, out, 2, &layout),
             XHCI_XFER_TOO_MANY_TRBS, "no room for the Status Stage TRB");
    CHECK_EQ(XhciXferBuildControl(&req, out, 1, &layout),
             XHCI_XFER_BAD_PARAM, "a control transfer is never one TRB");

    /*
     * And the capacity check on the *data* path, which the status-stage one
     * above cannot stand in for: without it the walk keeps writing TRBs past the
     * end of the caller's array and only notices when it reaches the status
     * stage, by which time it has already overrun. Both refusals carry the same
     * status, so the sentinel is what tells them apart.
     */
    sg_init(&sg);
    sg_add(&sg, 0x0D000000UL, 4096, 0);
    sg_add(&sg, 0x0D001000UL, 4096, 4096);
    request_init(&req, 0x80, 0x06, 0, 0, 0, 8192, 64, &sg);
    for (i = 0; i < XHCI_XFER_MAX_CONTROL_TRBS; i++) {
        out[i].Param0 = 0xDEADBEEFUL;
    }
    CHECK_EQ(XhciXferBuildControl(&req, out, 2, &layout),
             XHCI_XFER_TOO_MANY_TRBS, "two data TRBs do not fit in two slots");
    CHECK_EQ(out[2].Param0, 0xDEADBEEFUL,
             "and nothing was written past the declared capacity");

    /*
     * More fragments than policy allows. There are two caps - one on the
     * element count, taken before a single element is read, and one on the TRBs
     * emitted - and they return the same status, so the status cannot tell them
     * apart. What can is *when* the refusal happened: the list-level cap exists
     * because the selection scan indexes usbport's array for every i below the
     * count, so a count this driver has not agreed to is a read past the end of
     * somebody else's allocation. Asserting the output is untouched is how that
     * position is pinned; deleting the check leaves 32 TRBs written first.
     */
    sg_init(&sg);
    for (i = 0; i < XHCI_XFER_MAX_DATA_TRBS + 1; i++) {
        sg_add(&sg, 0x0E000000UL + i * 0x1000UL, 4096, i * 4096);
    }
    request_init(&req, 0x80, 0x06, 0, 0, 0,
                 (XHCI_XFER_MAX_DATA_TRBS + 1) * 4096, 64, &sg);
    for (i = 0; i < XHCI_XFER_MAX_CONTROL_TRBS; i++) {
        out[i].Param0 = 0xDEADBEEFUL;
    }
    CHECK_EQ(XhciXferBuildControl(&req, out, XHCI_XFER_MAX_CONTROL_TRBS,
                                  &layout),
             XHCI_XFER_TOO_MANY_TRBS, "more elements than the cap allows");
    CHECK_EQ(out[1].Param0, 0xDEADBEEFUL,
             "and it refused before reading the list at all");
    CHECK_EQ(out[XHCI_XFER_MAX_DATA_TRBS].Param0, 0xDEADBEEFUL,
             "no TRB was written on the way to that refusal");

    /*
     * The other cap, reached with an element count the first one allows: one
     * fragment whose 64 KB splits overrun the TRB budget. It is given a scratch
     * array **larger than XHCI_XFER_MAX_CONTROL_TRBS** on purpose, because with
     * a scratch of exactly that size the caller's own capacity check stops the
     * build first and the policy cap is unreachable - measured, by a mutation
     * that deleted it and failed nothing. The two limits are different things:
     * one is how much room the caller supplied, the other is the number task
     * 6-B.1 has to keep `MaxTransferSize` under.
     */
    {
        XHCI_TRB roomy[XHCI_XFER_MAX_CONTROL_TRBS * 2];

        sg_init(&sg);
        sg_add(&sg, 0UL, (XHCI_XFER_MAX_DATA_TRBS + 1) * 0x10000UL, 0);
        request_init(&req, 0x80, 0x06, 0, 0, 0,
                     (XHCI_XFER_MAX_DATA_TRBS + 1) * 0x10000UL, 64, &sg);
        CHECK_EQ(XhciXferBuildControl(&req, roomy,
                                      XHCI_XFER_MAX_CONTROL_TRBS * 2, &layout),
                 XHCI_XFER_TOO_MANY_TRBS,
                 "policy caps the data TRBs even when the caller has room");

        /* And exactly at the cap it builds, so the limit is the number it says
         * rather than one either side of it. */
        sg_init(&sg);
        sg_add(&sg, 0UL, XHCI_XFER_MAX_DATA_TRBS * 0x10000UL, 0);
        request_init(&req, 0x80, 0x06, 0, 0, 0,
                     XHCI_XFER_MAX_DATA_TRBS * 0x10000UL, 64, &sg);
        CHECK_EQ(XhciXferBuildControl(&req, roomy,
                                      XHCI_XFER_MAX_CONTROL_TRBS * 2, &layout),
                 XHCI_XFER_OK, "exactly at the cap is allowed");
        CHECK_EQ(layout.DataCount, XHCI_XFER_MAX_DATA_TRBS, "and fills it");
        CHECK_EQ(layout.TrbCount, XHCI_XFER_MAX_CONTROL_TRBS,
                 "which is what makes XHCI_XFER_MAX_CONTROL_TRBS the right "
                 "scratch size");
    }

    CHECK_EQ(XhciXferBuildControl(NULL, out, 8, &layout),
             XHCI_XFER_BAD_PARAM, "NULL request");
    request_init(&req, 0x00, 0x05, 3, 0, 0, 0, 8, NULL);
    CHECK_EQ(XhciXferBuildControl(&req, NULL, 8, &layout),
             XHCI_XFER_BAD_PARAM, "NULL output");
    CHECK_EQ(XhciXferBuildControl(&req, out, 8, NULL),
             XHCI_XFER_BAD_PARAM, "NULL layout");
}

/* ------------------------------------------------------------------ */
/* 3. Submission onto a ring                                           */
/* ------------------------------------------------------------------ */

typedef struct _XFER_FIXTURE {
    XHCI_TRB mem[32];
    XHCI_RING ring;
    XHCI_TRANSFER_QUEUE queue;
    XHCI_TRB scratch[XHCI_XFER_MAX_CONTROL_TRBS];
    XHCI_TRANSFER transfers[4];
    SG_BUFFER sg;
    XHCI_CONTROL_REQUEST req;
} XFER_FIXTURE;

#define FIX_SLOT 5
#define FIX_DCI  1

static void fixture_init(XFER_FIXTURE *fix, ULONG trbs)
{
    CHECK_EQ(XhciRingInit(&fix->ring, fix->mem, RING_PA, trbs,
                          XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "fixture ring init");
    XhciXferQueueInit(&fix->queue);
}

/* An 18-byte IN control transfer, the enumeration workhorse. */
static ULONG fixture_submit_in(XFER_FIXTURE *fix, ULONG which, ULONG length)
{
    sg_init(&fix->sg);
    if (length > 0) {
        sg_add(&fix->sg, 0x0A123000UL, length, 0);
    }
    request_init(&fix->req, 0x80, 0x06, 0x0100, 0, (USHORT)length, length, 8,
                 &fix->sg);
    return XhciXferSubmitControl(&fix->queue, &fix->ring, &fix->req,
                                 &fix->transfers[which],
                                 (PVOID)(0x1000UL + which),
                                 fix->scratch, XHCI_XFER_MAX_CONTROL_TRBS);
}

static void test_submit(void)
{
    XFER_FIXTURE fix;
    PXHCI_TRANSFER t;
    ULONG head;
    ULONG tail;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");

    t = &fix.transfers[0];
    CHECK_EQ(t->Signature, XHCI_TRANSFER_SIGNATURE, "record signed");
    CHECK_EQ(t->Token, 1, "tokens start at 1, never 0");
    CHECK_EQ((ULONG)t->TransferParameters, 0x1000UL,
             "the pointer usbport handed us is kept for the completion call");
    CHECK_EQ(t->FirstIndex, 0, "group starts at index 0");
    CHECK_EQ(t->LastIndex, 2, "and ends on the Status Stage TRB");
    CHECK_EQ(t->TrbCount, 3, "three TRBs");
    CHECK_EQ(t->DataFirstIndex, 1, "the data stage is at index 1");
    CHECK_EQ(t->DataTrbCount, 1, "one data TRB");
    CHECK_EQ(t->RequestedLength, 18, "18 bytes asked for");
    CHECK_EQ(t->BytesTransferred, 0, "and none reported yet");
    CHECK_EQ(t->Flags, 0, "no latches set");

    CHECK_EQ(fix.queue.Count, 1, "one transfer queued");
    CHECK_EQ(fix.queue.Head, t, "at the head");
    CHECK_EQ(fix.queue.Tail, t, "and the tail");
    CHECK_EQ(fix.queue.Submitted, 1, "counted");

    CHECK_EQ(fix.ring.Enqueue, 3, "the ring advanced by the whole group");
    /* Published: the head TRB carries the producer cycle, which is the single
     * store that makes the transfer exist. */
    CHECK_EQ(fix.mem[0].Control & XHCI_TRB_CYCLE, 1, "head published");
    CHECK_EQ(fix.mem[1].Control & XHCI_TRB_CYCLE, 1, "data TRB visible");
    CHECK_EQ(fix.mem[2].Control & XHCI_TRB_CYCLE, 1, "status TRB visible");

    /* Three TDs on the ring, each recoverable on its own. */
    CHECK_EQ(XhciRingTdBounds(&fix.ring, 0, &head, &tail), XHCI_RING_OK, "TD 0");
    CHECK_EQ(head, 0, "setup TD");
    CHECK_EQ(tail, 0, "setup TD is one TRB");
    CHECK_EQ(XhciRingTdBounds(&fix.ring, 1, &head, &tail), XHCI_RING_OK, "TD 1");
    CHECK_EQ(head, 1, "data TD");
    CHECK_EQ(tail, 1, "data TD is one TRB");
    CHECK_EQ(XhciRingTdBounds(&fix.ring, 2, &head, &tail), XHCI_RING_OK, "TD 2");
    CHECK_EQ(head, 2, "status TD");

    /* A second transfer queues behind the first, in ring order. */
    CHECK_EQ(fixture_submit_in(&fix, 1, 0), XHCI_XFER_OK, "second submitted");
    CHECK_EQ(fix.queue.Count, 2, "two queued");
    CHECK_EQ(fix.transfers[1].Token, 2, "tokens advance");
    CHECK_EQ(fix.transfers[1].FirstIndex, 3, "and it starts where the first ended");
    CHECK_EQ(fix.transfers[1].DataFirstIndex, XHCI_XFER_NO_INDEX,
             "a zero-length transfer has no data stage at all");
    CHECK_EQ(fix.queue.Head->Next, &fix.transfers[1], "threaded in order");
}

/*
 * A transfer that does not fit writes nothing and reports busy, so usbport
 * requeues it. The ring must be exactly as it was.
 */
static void test_submit_ring_full(void)
{
    XFER_FIXTURE fix;
    ULONG enqueueBefore;
    ULONG freeBefore;
    ULONG i;

    /* 8 TRBs is 6 usable slots: two 3-TRB transfers fit, the third cannot. */
    fixture_init(&fix, 8);
    for (i = 0; i < 2; i++) {
        CHECK_EQ(fixture_submit_in(&fix, i, 18), XHCI_XFER_OK, "fits");
    }
    enqueueBefore = fix.ring.Enqueue;
    freeBefore = XhciRingFree(&fix.ring);
    CHECK_EQ(freeBefore, 0, "the ring is full");

    CHECK_EQ(fixture_submit_in(&fix, 2, 18), XHCI_XFER_BUSY,
             "the third transfer is refused");
    CHECK_EQ(fix.ring.Enqueue, enqueueBefore, "nothing was written");
    CHECK_EQ(XhciRingFree(&fix.ring), freeBefore, "and nothing consumed");
    CHECK_EQ(fix.queue.Count, 2, "and it was not queued");
    CHECK_EQ(fix.queue.Submitted, 2, "nor counted as submitted");
}

/* ------------------------------------------------------------------ */
/* 3a. Building an interrupt transfer (task 7a-A.2)                    */
/* ------------------------------------------------------------------ */

/*
 * An interrupt TD is one TD of Normal TRBs, and the three things that make it
 * different from a control transfer's data stage are all encoded here rather
 * than argued:
 *
 *   - the first TRB is a **Normal** TRB, not a Data Stage TRB, so no direction
 *     bit is written anywhere - the direction is the Endpoint Context's;
 *   - **IOC is on the last TRB of this TD**, because there is no Status Stage
 *     TRB to carry it;
 *   - a zero-length transfer is **one** zero-length TRB, not none.
 *
 * Expected control words are written as literals, transcribed from
 * docs/usb-xhci-info/xhci-data-structures.md section 7: Normal is type 1, so
 * XHCI_TRB_TYPE(1) = 0x400; ISP = 0x4, CH = 0x10, IOC = 0x20.
 */
static void normal_request_init(XHCI_NORMAL_REQUEST *req,
                                ULONG length,
                                ULONG directionIn,
                                ULONG mps,
                                const SG_BUFFER *sg)
{
    req->TransferLength = length;
    req->DirectionIn = directionIn;
    req->MaxPacketSize = mps;
    req->SgList = (sg != NULL) ? &sg->List : NULL;
}

static void test_build_normal_single(void)
{
    XHCI_NORMAL_REQUEST req;
    XHCI_TRB out[XHCI_XFER_MAX_DATA_TRBS];
    SG_BUFFER sg;
    ULONG count;

    /* The HID shape: an 8-byte interrupt IN report, one mapped fragment, an
     * 8-byte Max Packet Size. */
    sg_init(&sg);
    sg_add(&sg, 0x0B456000UL, 8, 0);
    normal_request_init(&req, 8, 1, 8, &sg);

    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_OK, "interrupt IN transfer built");
    CHECK_EQ(count, 1, "one fragment, one TRB, one TD");
    CHECK_EQ(out[0].Param0, 0x0B456000UL, "the buffer's physical address");
    CHECK_EQ(out[0].Param1, 0, "no 64-bit DMA");
    /* TRB Transfer Length 8, TD Size 0 (last TRB of the TD), interrupter 0. */
    CHECK_EQ(out[0].Status, 8UL, "length 8, TD Size cleared on the last TRB");
    /* Normal (0x400) | ISP (0x4) | IOC (0x20), and CH cleared because this is
     * the last TRB of the TD. */
    CHECK_EQ(out[0].Control, 0x00000424UL, "Normal + ISP + IOC, not chained");
    CHECK_EQ(out[0].Control & XHCI_TRB_DIR_IN, 0,
             "a Normal TRB has no direction field - Table 6-22");
    CHECK_EQ(XHCI_TRB_GET_TYPE(out[0].Control), XHCI_TRB_TYPE_NORMAL,
             "and it is a Normal TRB, not a Data Stage TRB");
}

static void test_build_normal_out_has_no_isp(void)
{
    XHCI_NORMAL_REQUEST req;
    XHCI_TRB out[XHCI_XFER_MAX_DATA_TRBS];
    SG_BUFFER sg;
    ULONG count;

    /* An interrupt OUT - the direction usbport uses for a HID output report.
     * ISP is meaningless on an OUT endpoint: the host decides how much it
     * sends, so there is no short packet to report. */
    sg_init(&sg);
    sg_add(&sg, 0x0B457000UL, 4, 0);
    normal_request_init(&req, 4, 0, 8, &sg);

    CHECK_EQ(XhciXferBuildNormal(&req, 0, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_OK, "interrupt OUT transfer built");
    CHECK_EQ(count, 1, "one TRB");
    CHECK_EQ(out[0].Control, 0x00000420UL, "Normal + IOC, no ISP on an OUT");
    CHECK_EQ(out[0].Control & XHCI_TRB_ISP, 0, "ISP is an IN-only flag here");
}

static void test_build_normal_zero_length(void)
{
    XHCI_NORMAL_REQUEST req;
    XHCI_TRB out[XHCI_XFER_MAX_DATA_TRBS];
    ULONG count;

    /*
     * **One TRB, not none.** A zero-length transfer is still a transfer usbport
     * is waiting on, and a TD with no TRBs would never be executed and never
     * complete - the transfer would hang rather than answer. The SG list is
     * empty for these (batch 6-0) and must not be consulted, which is why NULL
     * is passed: a build that indexed it would fault here.
     */
    normal_request_init(&req, 0, 0, 64, NULL);
    CHECK_EQ(XhciXferBuildNormal(&req, 0, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_OK, "zero-length interrupt transfer built");
    CHECK_EQ(count, 1, "exactly one zero-length Normal TRB");
    CHECK_EQ(out[0].Param0, 0, "no buffer");
    CHECK_EQ(out[0].Status, 0, "length 0 and TD Size 0");
    CHECK_EQ(out[0].Control, 0x00000420UL, "Normal + IOC");
}

static void test_build_normal_multi_trb(void)
{
    XHCI_NORMAL_REQUEST req;
    XHCI_TRB out[XHCI_XFER_MAX_DATA_TRBS];
    SG_BUFFER sg;
    ULONG count;

    /*
     * Three fragments, 160 bytes, Max Packet Size 64 - the multi-element SG case
     * Phase 6 could never produce, because the default pipe is capped at one
     * page. TD Size by spec 4.11.2.4:
     *   TD Packet Count      = ceil(160 / 64) = 3
     *   after TRB 1 (64 B):  transferred 1, TD Size = 3 - 1 = 2
     *   after TRB 2 (128 B): transferred 2, TD Size = 3 - 2 = 1
     *   TRB 3 is the last:   TD Size = 0 (p.198)
     */
    sg_init(&sg);
    sg_add(&sg, 0x0C000000UL, 64, 0);
    sg_add(&sg, 0x0C400000UL, 64, 64);
    sg_add(&sg, 0x0C800000UL, 32, 128);
    normal_request_init(&req, 160, 1, 64, &sg);

    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_OK, "three-fragment interrupt transfer built");
    CHECK_EQ(count, 3, "one TRB per fragment");

    CHECK_EQ(out[0].Param0, 0x0C000000UL, "fragment 1 address");
    CHECK_EQ(out[0].Status, 64UL | (2UL << 17), "length 64, TD Size 2");
    CHECK_EQ(out[0].Control, 0x00000414UL, "Normal + ISP + CH, no IOC");

    CHECK_EQ(out[1].Param0, 0x0C400000UL, "fragment 2 address");
    CHECK_EQ(out[1].Status, 64UL | (1UL << 17), "length 64, TD Size 1");
    CHECK_EQ(out[1].Control, 0x00000414UL, "still chained, still no IOC");

    CHECK_EQ(out[2].Param0, 0x0C800000UL, "fragment 3 address");
    CHECK_EQ(out[2].Status, 32UL, "length 32, TD Size cleared on the last TRB");
    CHECK_EQ(out[2].Control, 0x00000424UL, "chain cleared, IOC set");

    /*
     * **IOC on exactly one TRB, and it is the last.** Two would complete the
     * transfer twice; none would never complete it at all - and the control
     * builder puts IOC somewhere else entirely, so this cannot be inherited from
     * that vector.
     */
    CHECK_EQ((out[0].Control & XHCI_TRB_IOC) ? 1 : 0, 0, "no IOC on TRB 1");
    CHECK_EQ((out[1].Control & XHCI_TRB_IOC) ? 1 : 0, 0, "no IOC on TRB 2");
    CHECK_EQ((out[2].Control & XHCI_TRB_IOC) ? 1 : 0, 1, "IOC on the last TRB");
    /*
     * **ISP on every IN TRB.** On the intermediate ones it is required
     * (4.10.1.1.2 - a short packet terminates the TD wherever it lands, so
     * without ISP there is no event and the IOC on the last TRB is never
     * reached); on the last one it is permitted and redundant beside IOC
     * (4.10.1.1). Pinned on all three because the builder sets one rule rather
     * than a rule with an exception - not because the last one is obliged.
     */
    CHECK_EQ((out[0].Control & XHCI_TRB_ISP) ? 1 : 0, 1, "ISP on TRB 1");
    CHECK_EQ((out[1].Control & XHCI_TRB_ISP) ? 1 : 0, 1, "ISP on TRB 2");
    CHECK_EQ((out[2].Control & XHCI_TRB_ISP) ? 1 : 0, 1, "ISP on the last too");
}

static void test_build_normal_refusals(void)
{
    XHCI_NORMAL_REQUEST req;
    XHCI_TRB out[XHCI_XFER_MAX_DATA_TRBS];
    SG_BUFFER sg;
    ULONG count;

    sg_init(&sg);
    sg_add(&sg, 0x0B456000UL, 8, 0);

    /*
     * usbport's direction against the endpoint's. This is the check that would
     * otherwise be free to disagree: the Endpoint Context's EP Type says IN and
     * usbport's TransferFlags say OUT, and moving the bytes either way is wrong.
     */
    normal_request_init(&req, 8, 1, 8, &sg);
    CHECK_EQ(XhciXferBuildNormal(&req, 0, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_DIRECTION_CONFLICT,
             "an IN endpoint with an OUT transfer flag is refused");
    normal_request_init(&req, 8, 0, 8, &sg);
    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_DIRECTION_CONFLICT, "and the other way round");

    /* A zero Max Packet Size would divide the TD Size arithmetic by zero. */
    normal_request_init(&req, 8, 1, 0, &sg);
    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_BAD_PARAM, "Max Packet Size 0 refused");
    /* wMaxPacketSize is eleven bits, so 2048 is not a USB 2.0 endpoint. */
    normal_request_init(&req, 8, 1, 2048, &sg);
    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_BAD_PARAM, "Max Packet Size above 0x7FF refused");

    /* An SG list that does not tile the buffer - here it is short. */
    normal_request_init(&req, 64, 1, 8, &sg);
    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_SG_MISMATCH, "a list that does not cover the buffer");

    normal_request_init(&req, 8, 1, 8, &sg);
    CHECK_EQ(XhciXferBuildNormal(NULL, 1, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_BAD_PARAM, "NULL request");
    CHECK_EQ(XhciXferBuildNormal(&req, 1, NULL, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_BAD_PARAM, "NULL output");
    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, XHCI_XFER_MAX_DATA_TRBS, NULL),
             XHCI_XFER_BAD_PARAM, "NULL count");
    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, 0, &count),
             XHCI_XFER_BAD_PARAM, "no room for even one TRB");
}

/* An 8-byte interrupt IN transfer onto the fixture's ring. */
static ULONG fixture_submit_interrupt(XFER_FIXTURE *fix,
                                      ULONG which,
                                      ULONG length)
{
    XHCI_NORMAL_REQUEST req;

    sg_init(&fix->sg);
    if (length > 0) {
        sg_add(&fix->sg, 0x0B456000UL, length, 0);
    }
    normal_request_init(&req, length, 1, 8, &fix->sg);
    return XhciXferSubmitNormal(&fix->queue, &fix->ring, &req, 1,
                                &fix->transfers[which],
                                (PVOID)(0x2000UL + which),
                                fix->scratch, XHCI_XFER_MAX_CONTROL_TRBS);
}

static void test_submit_normal(void)
{
    XFER_FIXTURE fix;
    PXHCI_TRANSFER t;
    XHCI_XFER_EVENT_RESULT result;
    ULONG head;
    ULONG tail;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt(&fix, 0, 8), XHCI_XFER_OK, "submitted");

    t = &fix.transfers[0];
    CHECK_EQ(t->Signature, XHCI_TRANSFER_SIGNATURE, "record signed");
    CHECK_EQ(t->Token, 1, "tokens start at 1");
    CHECK_EQ(t->FirstIndex, 0, "starts at index 0");
    CHECK_EQ(t->LastIndex, 0, "one TRB, so it ends there too");
    CHECK_EQ(t->TrbCount, 1, "one TRB");
    /*
     * **The data range is the whole TD**, unlike a control transfer where it is
     * the middle of three. The residual arithmetic keys on this range, so a
     * DataFirstIndex left at the control path's `FirstIndex + 1` would attribute
     * a short packet to a TRB that is not in this transfer.
     */
    CHECK_EQ(t->DataFirstIndex, 0, "the data range starts at the TD's head");
    CHECK_EQ(t->DataTrbCount, 1, "and covers every TRB of it");
    CHECK_EQ(t->RequestedLength, 8, "8 bytes asked for");

    CHECK_EQ(fix.queue.Count, 1, "one transfer queued");
    CHECK_EQ(fix.queue.Submitted, 1, "counted");
    CHECK_EQ(fix.ring.Enqueue, 1, "the ring advanced by one TRB");
    CHECK_EQ(fix.mem[0].Control & XHCI_TRB_CYCLE, 1, "published");

    /* One TD, recoverable on its own. */
    CHECK_EQ(XhciRingTdBounds(&fix.ring, 0, &head, &tail), XHCI_RING_OK, "TD 0");
    CHECK_EQ(head, 0, "starts at 0");
    CHECK_EQ(tail, 0, "and is one TRB long");

    /*
     * **Several outstanding transfers on one interrupt endpoint** - the clause
     * task 7a-A.2 names, and the thing a HID stack actually does: it keeps a
     * read posted at all times and posts the next one from the completion.
     */
    CHECK_EQ(fixture_submit_interrupt(&fix, 1, 8), XHCI_XFER_OK, "second");
    CHECK_EQ(fixture_submit_interrupt(&fix, 2, 8), XHCI_XFER_OK, "third");
    CHECK_EQ(fix.queue.Count, 3, "three outstanding on one endpoint");
    CHECK_EQ(fix.transfers[1].FirstIndex, 1, "each on its own TRB");
    CHECK_EQ(fix.transfers[2].FirstIndex, 2, "in ring order");
    CHECK_EQ(fix.queue.Head, &fix.transfers[0], "oldest at the head");
    CHECK_EQ(fix.queue.Head->Next, &fix.transfers[1], "threaded in order");

    /* And they complete oldest-first, one event each. */
    CHECK_EQ(XhciXferEvent(&fix.queue, &fix.ring, FIX_SLOT, FIX_DCI,
                           XhciRingTrbPA(&fix.ring, 0),
                           event_dw2(XHCI_CC_SUCCESS, 0),
                           event_dw3(FIX_SLOT, FIX_DCI), &result),
             XHCI_XFER_OK, "first event");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "first completed");
    CHECK_EQ(result.CompletedCount, 1, "exactly one, not the whole queue");
    CHECK_EQ(result.Completed, &fix.transfers[0], "and it is the oldest");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 8, "all 8 bytes moved");
    CHECK_EQ(fix.queue.Count, 2, "two still outstanding");
}

/* Section 4's event-delivery helper, used by the two vectors below as well.
 * Declared rather than moved, because it belongs beside the event tests it was
 * written for. */
static ULONG deliver(XFER_FIXTURE *fix,
                     ULONG index,
                     ULONG completionCode,
                     ULONG residual,
                     PXHCI_XFER_EVENT_RESULT result);

/*
 * Task 9-0.2's other half: the end of a drain pass that found the event ring
 * empty. `XhciXferEvent` no longer completes a mid-TD short packet - it defers -
 * so a vector that delivers a short event and stops is asserting the *deferral*,
 * and one that wants the completion has to call this.
 *
 * **Reusing one transfer record across submissions now requires it.** A deferred
 * transfer is still on the queue, so re-submitting the same `fix.transfers[]`
 * entry links the record to itself and every queue walk after that never ends.
 * That is a property of the fixture (usbport allocates a distinct transfer
 * extension per transfer and never re-offers one the miniport still holds -
 * batch 7a-0), not of the driver, but it is why the loops below settle or take
 * the tail before posting again.
 */
static ULONG settle(XFER_FIXTURE *fix, PXHCI_XFER_EVENT_RESULT result)
{
    return XhciXferDrainSettled(&fix->queue, &fix->ring, result);
}

/* Three fragments, 160 bytes, MPS 64 - the same shape as the builder vector,
 * but published onto a ring so the *event* path sees a multi-TRB Normal TD. */
static ULONG fixture_submit_interrupt_multi(XFER_FIXTURE *fix, ULONG which)
{
    XHCI_NORMAL_REQUEST req;

    sg_init(&fix->sg);
    sg_add(&fix->sg, 0x0C000000UL, 64, 0);
    sg_add(&fix->sg, 0x0C400000UL, 64, 64);
    sg_add(&fix->sg, 0x0C800000UL, 32, 128);
    normal_request_init(&req, 160, 1, 64, &fix->sg);
    return XhciXferSubmitNormal(&fix->queue, &fix->ring, &req, 1,
                                &fix->transfers[which],
                                (PVOID)(0x3000UL + which),
                                fix->scratch, XHCI_XFER_MAX_CONTROL_TRBS);
}

/*
 * **A short packet on a whole-data TD defers the transfer, and the second event
 * the spec promises is what completes it** - the conforming controller's whole
 * path after task 9-0.2.
 *
 * The history is worth keeping because this vector has now asserted three
 * different things. It first asserted the spec: 4.10.1.1.2 p.175 says software
 * "shall not interpret a Short Packet Event as indicating that the TD ... is
 * complete, unless the TRB Pointer field ... references the last TRB of the TD",
 * and promises "two events shall be generated". Batch 8-V.2 then measured a
 * controller that sends one - QEMU's xHC on a passed-through ASIX AX88772,
 * `s 0x0d000476`, residual 1142 on a 1504-byte first TRB, no second event ever -
 * so the TD was never retired, the receive never completed, and the vendor
 * driver never posted another one. The fix completed the transfer on the first
 * event, and this vector asserted that.
 *
 * Task 9-0.2 keeps the departure and moves *when* it is taken, because retiring
 * on the first event re-lets TRBs a delayed tail is then matched against (repo
 * audit finding 21). So the first event now only defers, and a conforming
 * controller never reaches the departure at all: its tail arrives first and ends
 * the transfer positionally. That is what is asserted here, and
 * `test_event_normal_short_packet_tail_withheld` is the other controller.
 *
 * The one thing that has survived all three revisions is the *length*: 32, not
 * 160 - 32 = 128, established by the first event and not moved by the second.
 */
static void test_event_normal_short_packet_terminates(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    PXHCI_TRANSFER t;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "submitted");
    t = &fix.transfers[0];
    CHECK_EQ(t->TrbCount, 3, "(three TRBs)");
    CHECK_EQ(t->DataFirstIndex, 0, "the data range starts at the head");
    CHECK_EQ(t->DataTrbCount, 3, "and covers all three");

    /* The device delivers 32 of the first TRB's 64 bytes and stops. */
    CHECK_EQ(deliver(&fix, 0, XHCI_CC_SHORT_PACKET, 32, &result), XHCI_XFER_OK,
             "the short packet event");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "deferred, not completed - the tail p.175 promises may still be in "
             "the event ring, and retiring here is what finding 21 named");
    CHECK_EQ(result.CompletedCount, 0, "nothing completed");
    CHECK_EQ(fix.queue.MidTdDeferrals, 1, "one deferral armed");
    CHECK_EQ(fix.queue.Count, 1, "and the transfer is still queued");
    CHECK_EQ(XhciXferDeferralsArmed(&fix.queue), 1, "and still armed");
    CHECK_EQ(t->BytesTransferred, 32, "64 described, 32 residual, 32 moved");
    CHECK_EQ(fix.queue.ShortPackets, 1, "one short packet");
    CHECK_EQ(fix.queue.MidTdShortRetires, 0, "nothing retired early");
    CHECK_EQ(fix.ring.Dequeue, 0, "and the dequeue pointer has not moved");

    /*
     * The conforming controller's second event, on the IOC TRB. Because the
     * transfer is still queued and still owns those TRBs, the owner search finds
     * it and the ordinary positional rule ends it - no departure taken, nothing
     * retired early, no tail record made and so nothing to censor later.
     */
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SHORT_PACKET, 32, &result), XHCI_XFER_OK,
             "the tail event on the TD's last TRB");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "which completes it");
    CHECK_EQ(result.CompletedCount, 1, "exactly one transfer");
    CHECK_EQ(result.Completed, t, "and it is ours");
    CHECK_EQ((ULONG)t->UsbdStatus, WANT_USBD_SUCCESS,
             "a short packet is a successful completion carrying a length");
    CHECK_EQ(t->BytesTransferred, 32,
             "the length the *first* event measured, not the repeat's 128");
    CHECK_EQ(fix.queue.ShortPackets, 1,
             "one short *condition*, not one per event reporting it");
    CHECK_EQ(result.NeedsRecovery, 0, "the endpoint is not halted");
    CHECK_EQ(fix.queue.OrphanedGroups, 0,
             "and the TRBs were reclaimed, so nothing is orphaned");
    CHECK_EQ(XhciRingFree(&fix.ring), XhciRingCapacity(&fix.ring),
             "every TRB of the TD is back - which is what lets the next receive "
             "be posted, and is what the 8-V.2 hang could not do");

    /*
     * **The verdict, and it needs no qualification.** Before 9-0.2 both kinds of
     * controller incremented `MidTdShortRetires` identically and the reading was
     * `MidTdShortTails` against it, valid only while nothing was dropped or
     * censored. Now the two machines take different code paths, and the counter
     * that separates them is this one.
     */
    CHECK_EQ(fix.queue.MidTdDeferralsTailed, 1,
             "the promised tail arrived in-band - the conforming signature");
    CHECK_EQ(fix.queue.MidTdDeferralsTailed, fix.queue.MidTdDeferrals,
             "every deferral was answered by its tail");
    CHECK_EQ(fix.queue.MidTdShortRetires, 0,
             "so the departure was never taken");
    CHECK_EQ(fix.queue.MidTdShortTails, 0,
             "and no tail record was ever made, because none was needed");
    CHECK_EQ(fix.queue.MidTdTailCount, 0, "nothing outstanding");
    CHECK_EQ(fix.queue.MidTdTailsCensored, 0,
             "and nothing censored - the state the roadmap says says the fix "
             "worked, on the very load that made censoring the common case");
    CHECK_EQ(fix.queue.MidTdDeferralsLost, 0, "and nothing lost");
    CHECK_EQ(XhciXferDeferralsArmed(&fix.queue), 0, "none still armed");

    /* A settle now has nothing to do, which is the shape a conforming
     * controller presents to every drain pass. */
    CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "a settle pass runs");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "and finds nothing");
    CHECK_EQ(fix.queue.MidTdShortRetires, 0, "still nothing retired early");
}

/*
 * The tail record is consumed, so a controller that repeats the tail event
 * cannot inflate the conformance reading into claiming more tails than retires.
 * Without the consume, `MidTdShortTails > MidTdShortRetires` would be
 * constructible - a reading no controller can legitimately produce.
 *
 * After task 9-0.2 the record only exists at all when a **settle** took the
 * departure, so the settle is what this vector has to go through to reach the
 * mechanism it is about. A tail arriving before the settle would be answered
 * in-band and no record would be made - which is the vector above.
 */
static void test_event_mid_td_tail_counted_once(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "submitted");
    CHECK_EQ(deliver(&fix, 0, XHCI_CC_SHORT_PACKET, 32, &result), XHCI_XFER_OK,
             "the short packet event");
    CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "the drain pass empties");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "and settles it");
    CHECK_EQ(fix.queue.MidTdShortRetires, 1, "one early retire");

    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SHORT_PACKET, 32, &result), XHCI_XFER_OK,
             "the tail event, arriving after the settle");
    CHECK_EQ(fix.queue.MidTdShortTails, 1, "counted");
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SHORT_PACKET, 32, &result), XHCI_XFER_OK,
             "the same tail event a second time");
    CHECK_EQ(fix.queue.MidTdShortTails, 1, "still one - the record was consumed");
    CHECK_EQ(fix.queue.UnmatchedEvents, 2,
             "both repeats are still counted as unowned, which is the difference "
             "between the two readings");
}

/*
 * More early retires outstanding than the slot ring holds. The oldest record is
 * evicted, and that is **counted** - because a tail count short of the retire
 * count would otherwise read as "the controller withheld them" when the driver
 * simply stopped remembering. A nonconformance verdict requires this at 0, and
 * this vector is what makes that requirement real rather than stated.
 */
static void test_event_mid_td_tail_eviction_is_counted(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    ULONG i;

    /* 32 TRBs is the fixture's whole backing array, and five three-TRB TDs fit
     * inside one lap - a wrap here would retract records rather than evict
     * them, which is the distinction this vector exists to make. */
    fixture_init(&fix, 32);

    /* One more than the ring of records holds, each retired early and none of
     * them given its tail event. */
    for (i = 0; i < XHCI_XFER_MID_TD_TAIL + 1; i++) {
        CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK,
                 "a receive is posted");
        CHECK_EQ(deliver(&fix, fix.transfers[0].FirstIndex,
                         XHCI_CC_SHORT_PACKET, 40, &result),
                 XHCI_XFER_OK, "short, and no tail event follows");
        /* The pass ends with the ring empty, which is what licenses the
         * departure and is what makes a record - and it has to happen before
         * the record is re-submitted, since a deferred transfer is still on
         * the queue (see `settle`). */
        CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "the drain empties");
        CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "and settles it");
    }
    CHECK_EQ(fix.queue.MidTdShortRetires, XHCI_XFER_MID_TD_TAIL + 1,
             "every one of them retired early");
    CHECK_EQ(fix.queue.MidTdShortTails, 0, "and none of them was answered");
    CHECK_EQ(fix.queue.MidTdTailsDropped, 1,
             "the one the slot ring could not keep is counted, not lost - "
             "so the shortfall above is attributable");
}

/*
 * **A record is displaced only when every slot is genuinely outstanding.**
 *
 * The first implementation wrote to the next *physical* slot whichever way the
 * cursor happened to be pointing, so a queue holding one waiting record and
 * three empty slots could evict the waiting one and count a drop (repo audit
 * round 2, finding 2). The count stayed arithmetically consistent, which is
 * what makes it worth a vector: the number was believable and wrong.
 *
 * Fill the ring of records, answer all but one, then record more. Nothing may
 * be dropped while a slot is free, and the record left waiting must still be
 * claimable at the end.
 */
static void test_event_mid_td_tail_reuses_free_slots(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    ULONG tails[XHCI_XFER_MID_TD_TAIL];
    ULONG i;

    /* 32 is the fixture's whole backing array (`XHCI_TRB mem[32]`), and seven
     * three-TRB TDs fit inside one lap of it - which is what this vector needs,
     * since a wrap would retract records for a reason it is not about. */
    fixture_init(&fix, 32);

    /* Four early retires, so every record slot is live. Each TD's tail index is
     * kept here rather than recomputed - the transfer record is reused for each
     * submission, so after the loop it describes only the last one. */
    for (i = 0; i < XHCI_XFER_MID_TD_TAIL; i++) {
        CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "posted");
        tails[i] = fix.transfers[0].LastIndex;
        CHECK_EQ(deliver(&fix, fix.transfers[0].FirstIndex,
                         XHCI_CC_SHORT_PACKET, 40, &result),
                 XHCI_XFER_OK, "short, no tail yet");
        CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "the drain empties");
    }
    CHECK_EQ(fix.queue.MidTdTailCount, XHCI_XFER_MID_TD_TAIL, "all four held");
    CHECK_EQ(fix.queue.MidTdTailsDropped, 0, "and none dropped getting there");

    /* Answer the last three, leaving the first waiting and three slots free. */
    for (i = 1; i < XHCI_XFER_MID_TD_TAIL; i++) {
        CHECK_EQ(deliver(&fix, tails[i], XHCI_CC_SHORT_PACKET, 40, &result),
                 XHCI_XFER_OK, "a tail event");
    }
    CHECK_EQ(fix.queue.MidTdShortTails, XHCI_XFER_MID_TD_TAIL - 1,
             "three of the four were answered");
    CHECK_EQ(fix.queue.MidTdTailCount, 1, "leaving one still waiting");

    /* Three more early retires must fit in the freed slots. The cursor version
     * evicted the record still waiting instead, while three slots stood empty. */
    for (i = 0; i < XHCI_XFER_MID_TD_TAIL - 1; i++) {
        CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "posted");
        CHECK_EQ(deliver(&fix, fix.transfers[0].FirstIndex,
                         XHCI_CC_SHORT_PACKET, 40, &result),
                 XHCI_XFER_OK, "short, no tail yet");
        CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "the drain empties");
    }
    CHECK_EQ(fix.queue.MidTdTailsDropped, 0,
             "nothing was dropped - there were free slots the whole time");
    CHECK_EQ(fix.queue.MidTdTailCount, XHCI_XFER_MID_TD_TAIL, "full again");

    /* And the record left waiting from the first round is still claimable. */
    CHECK_EQ(deliver(&fix, tails[0], XHCI_CC_SHORT_PACKET, 40, &result),
             XHCI_XFER_OK, "the long-delayed tail event for the first TD");
    CHECK_EQ(fix.queue.MidTdShortTails, XHCI_XFER_MID_TD_TAIL,
             "claimed - it was never evicted to make room");
}

/*
 * **The hazard finding 21 named, and the half of it task 9-0.2 closes.**
 *
 * A Transfer Event names a TRB address, not a generation. Once a retired TD's
 * TRBs are re-let, a tail event that was sent but not yet consumed names a TRB
 * the *new* TD owns, and the owner search matches it to that transfer - a
 * truncated bulk IN reported as success. This vector used to assert that
 * happening, deliberately, as a defect pinned in executable form.
 *
 * **What the fix changes is the ordering that made it reachable.** The path
 * finding 21 walked was: the drain stops at `XHCI_DPC_MAX_EVENTS` with the
 * tail still unread, the short TD is retired and completed anyway,
 * `XhciSlotDeferredWork` hands it to usbport, usbport reposts from inside that
 * call over the very TRBs the tail names, and the next pass then delivers the
 * tail into a live transfer. Every step after the first depended on the
 * transfer being completed while its tail was still in the ring.
 *
 * So this vector now asserts that it is not. The short event defers; the
 * transfer stays queued and keeps its TRBs; `XhciRingHasRoom` will not let a
 * later TD past the unmoved dequeue pointer, so nothing can be re-let and
 * there is nothing to misattribute; and when the tail arrives it is matched to
 * its own transfer by position. The whole hazard is gone from this path, and
 * the counters that used to report it read zero rather than being consulted.
 *
 * The window that survives is the vector below, which is not this one.
 */
static void test_event_delayed_tail_after_reuse_is_misattributed(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    PXHCI_TRANSFER t;
    ULONG tail;
    ULONG freeAfterShort;

    /* The same 8-TRB ring the old vector used, which holds two 3-TRB TDs - so
     * the lap that made the hazard reachable is one placement away, and the
     * refusal below is the ring saying no rather than the ring being big. */
    fixture_init(&fix, 8);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "posted");
    t = &fix.transfers[0];
    tail = t->LastIndex;

    CHECK_EQ(deliver(&fix, t->FirstIndex, XHCI_CC_SHORT_PACKET, 40, &result),
             XHCI_XFER_OK, "short mid-TD");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "deferred - the drain has not proved the ring empty");
    CHECK_EQ(fix.queue.Count, 1, "the transfer is still queued");
    CHECK_EQ(fix.queue.MidTdTailCount, 0,
             "and no tail record was made, because nothing was retired");
    freeAfterShort = XhciRingFree(&fix.ring);

    /*
     * **The re-let cannot happen, and that is the fix.** Post until the ring
     * refuses. The second TD goes onto fresh TRBs - it has to, since
     * `XhciRingHasRoom` will not pass the dequeue pointer - and the third is
     * refused outright, which is exactly the placement the old vector made in
     * one step to lap onto the recorded tail.
     */
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 1), XHCI_XFER_OK,
             "a second TD fits, on TRBs the deferred one does not own");
    CHECK(fix.transfers[1].FirstIndex != t->FirstIndex,
          "at a different position, which is the whole point");
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 2), XHCI_XFER_BUSY,
             "and the third - the one that would lap onto the deferred TD's "
             "TRBs - is refused");
    CHECK_EQ(XhciRingFree(&fix.ring), freeAfterShort - t->TrbCount,
             "with nothing written for it: a refused placement writes nothing, "
             "so the only TRBs consumed are the second TD's");

    /* Now the tail, which in finding 21's scenario was the event sitting unread
     * in the ring while the retire and the repost happened. */
    CHECK_EQ(fix.queue.Count, 2, "(its own transfer still owns those TRBs)");
    CHECK_EQ(deliver(&fix, tail, XHCI_CC_SHORT_PACKET, 40, &result),
             XHCI_XFER_OK, "the tail event");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completes a transfer");
    CHECK_EQ(result.CompletedCount, 1,
             "exactly one - the second TD is untouched");
    CHECK_EQ(result.Completed, t,
             "and it is the transfer the event belongs to - the misattribution "
             "this vector used to record no longer happens");
    CHECK_EQ(t->BytesTransferred, 24, "64 - 40, the length its own short "
             "packet measured");
    CHECK_EQ(fix.queue.Count, 1, "the second is still outstanding");
    CHECK_EQ((ULONG)fix.transfers[1].UsbdStatus, WANT_USBD_SUCCESS,
             "and was not touched by an event that was never its own");

    CHECK_EQ(fix.queue.MidTdDeferralsTailed, 1, "answered in-band");
    CHECK_EQ(fix.queue.MidTdShortRetires, 0, "nothing retired early");
    CHECK_EQ(fix.queue.MidTdTailsCensored, 0,
             "and nothing censored - the diagnostic that used to be the only "
             "available mitigation is not consulted, because there is no "
             "ambiguity left on this path");
    CHECK_EQ(fix.queue.MidTdDeferralsLost, 0, "and nothing lost");
    CHECK_EQ(XhciXferDeferralsArmed(&fix.queue), 0, "none still armed");
}

/*
 * **The window task 9-0.2 does not close, kept executable for the same reason
 * the vector above used to exist.**
 *
 * The settle runs when a drain pass has read the event ring empty. The xHC may
 * write the tail immediately after that read - there is no barrier between a
 * software read of the ring and a hardware write to it - and by then the TD has
 * been retired, completed to usbport, and its TRBs re-let. The tail then names
 * a TRB a live transfer owns and is matched to it, exactly as before.
 *
 * Closing this needs event identity, which means Event Data TRBs, which this
 * driver deliberately places none of. So it is **asserted as observed
 * behaviour**, not blessed: `MidTdShortTails` and `MidTdTailsCensored` are what
 * report it, and they are reports rather than a mitigation. Do not read a green
 * suite here as the hazard being gone - it is the hazard being bounded to one
 * ordering, where before it was reachable from every bounded drain exit and
 * from every tail still unread in the ring.
 */
static void test_event_tail_after_the_settle_is_still_misattributed(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    ULONG tail;
    ULONG laps;
    ULONG i;
    ULONG stillRecorded;

    fixture_init(&fix, 8);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "posted");
    tail = fix.transfers[0].LastIndex;
    CHECK_EQ(deliver(&fix, fix.transfers[0].FirstIndex,
                     XHCI_CC_SHORT_PACKET, 40, &result),
             XHCI_XFER_OK, "short mid-TD, deferred");
    CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK,
             "the pass reads the ring empty and retires it early");
    CHECK_EQ(fix.queue.MidTdTailCount, 1, "one tail outstanding");

    /* Lap the ring so a later TD owns the recorded tail's TRBs, settling each
     * so the record can be re-submitted (see `settle`). */
    stillRecorded = 1;
    for (laps = 0; laps < 8 && stillRecorded; laps++) {
        CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK,
                 "a later TD is placed");
        stillRecorded = 0;
        for (i = 0; i < fix.queue.MidTdTailCount; i++) {
            if (fix.queue.MidTdTailIndex[i] == tail) { stillRecorded = 1; }
        }
        if (stillRecorded) {
            CHECK_EQ(deliver(&fix, fix.transfers[0].FirstIndex,
                             XHCI_CC_SHORT_PACKET, 40, &result),
                     XHCI_XFER_OK, "which also ends short");
            CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "and settles");
        }
    }
    CHECK_EQ(stillRecorded, 0, "the record was retracted by the reuse");
    CHECK_EQ(fix.queue.MidTdTailsCensored, 1, "and counted as censored");

    /* The long-delayed tail event for the *first* TD, naming a TRB the current
     * transfer owns. */
    CHECK_EQ(fix.queue.Count, 1, "(a transfer is outstanding on those TRBs)");
    CHECK_EQ(deliver(&fix, tail, XHCI_CC_SHORT_PACKET, 40, &result),
             XHCI_XFER_OK, "the delayed tail event, naming a re-let TRB");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "it defers the live transfer rather than completing it - which is "
             "still a misattribution, just a differently shaped one: the event "
             "belonged to a transfer that ended long ago");
    CHECK(fix.queue.MidTdTailsCensored > 0,
          "and the run is marked as one whose mid-TD accounting cannot be "
          "trusted, which is the whole of the available mitigation");
}
/*
 * **A tail record is retracted when its TRBs are re-let, not left to match.**
 *
 * The conformance counter's no-false-match argument was that an unowned index
 * cannot be re-let. That holds only until the ring laps: the TRB *is* reused,
 * and if the transfer occupying it later ends and produces its own unowned
 * event, a stale record would claim it and report a tail the controller never
 * sent - inflating the reading in the direction that says "conforming" (repo
 * audit round 2, finding 1).
 *
 * A tiny ring makes the lap cheap. Retire one TD early, publish over the same
 * TRBs, and confirm the record is gone rather than waiting to be matched.
 */
static void test_event_mid_td_tail_retracted_on_reuse(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    ULONG tail;
    ULONG stillRecorded;
    ULONG droppedWhenRetracted;
    ULONG laps;
    ULONG i;

    /* The lap has to happen in **fewer than XHCI_XFER_MID_TD_TAIL TDs**, or the
     * record is evicted for want of a slot before its TRBs are ever re-let, and
     * the vector would prove nothing about retraction. Eight TRBs holds two
     * three-TRB TDs before the third wraps onto the first one's. (Sixteen does
     * not: five TDs fit, and the fifth evicts.) */
    fixture_init(&fix, 8);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "posted");
    tail = fix.transfers[0].LastIndex;
    CHECK_EQ(deliver(&fix, fix.transfers[0].FirstIndex,
                     XHCI_CC_SHORT_PACKET, 40, &result),
             XHCI_XFER_OK, "short, deferred");
    CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK,
             "and the drain pass that empties the ring retires it early");
    CHECK_EQ(fix.queue.MidTdShortRetires, 1, "one early retire");
    CHECK_EQ(fix.queue.MidTdTailCount, 1, "and one tail outstanding");

    /* Keep posting and retiring early until a new TD is published over the
     * recorded tail. Bounded, and the bound is asserted rather than silently
     * ending the loop. */
    stillRecorded = 1;
    droppedWhenRetracted = 0;
    for (laps = 0; laps < 8 && stillRecorded; laps++) {
        ULONG covered;

        CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "the drain empties");
        CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK,
                 "another TD is placed");
        /*
         * **Whether this placement covers the recorded tail is decided here,
         * before the record is looked at** - so the vector asserts that the
         * record disappears exactly when its TRBs are re-let, and survives
         * every placement that does not touch them. Without this an over-broad
         * retraction - one that dropped records on any submission - would pass
         * the "it eventually went away" test just as well (repo audit round 3,
         * finding 2).
         */
        covered = 0;
        for (i = 0; i < fix.transfers[0].TrbCount; i++) {
            ULONG idx = fix.transfers[0].FirstIndex;
            ULONG step;
            for (step = 0; step < i; step++) {
                idx = XhciRingNextIndex(&fix.ring, idx);
            }
            if (idx == tail) { covered = 1; }
        }

        stillRecorded = 0;
        for (i = 0; i < fix.queue.MidTdTailCount; i++) {
            if (fix.queue.MidTdTailIndex[i] == tail) { stillRecorded = 1; }
        }
        CHECK_EQ(stillRecorded, covered ? 0 : 1,
                 "the record is retracted by the placement that re-lets its "
                 "TRBs, and by no other");

        droppedWhenRetracted = fix.queue.MidTdTailsDropped;
        if (covered) { break; }

        CHECK_EQ(deliver(&fix, fix.transfers[0].FirstIndex,
                         XHCI_CC_SHORT_PACKET, 40, &result),
                 XHCI_XFER_OK, "which also ends short");
    }
    CHECK_EQ(stillRecorded, 0,
             "the record is gone - its TRBs were re-let to a later TD, so the "
             "tail it was waiting for can no longer be told from that TD's own");
    CHECK(laps < XHCI_XFER_MID_TD_TAIL,
          "and the ring lapped before the record slots could fill, which is "
          "what keeps this vector about retraction rather than eviction");
    CHECK_EQ(droppedWhenRetracted, 0,
             "nothing was evicted");
    CHECK_EQ(fix.queue.MidTdTailsCensored, 1,
             "the lost observation is counted as censored - it is not evidence "
             "the controller withheld anything, and an uncounted retraction "
             "would have made it look like exactly that");

    /* The invariant the whole conformance reading rests on. Weak on its own -
     * it holds trivially here - but it is the property a stale index would
     * break, so it is asserted where a stale index is most likely. */
    CHECK(fix.queue.MidTdShortTails <= fix.queue.MidTdShortRetires,
          "tails never exceed retires");
}

/*
 * The withheld-tail-event case on its own, and the thing the field failure
 * actually was: **the receive completes, and the repost is what has to work**.
 *
 * One short packet mid-TD, no second event ever, then the next receive posted
 * onto the same ring - the sequence a NIC driver runs forever. Before the fix
 * the first TD stayed outstanding, so `XhciRingFree` never recovered and the
 * transfer never reached usbport for the repost to be triggered at all.
 */
static void test_event_normal_short_packet_tail_withheld(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    ULONG i;

    fixture_init(&fix, 32);

    for (i = 0; i < 3; i++) {
        CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK,
                 "a receive is posted");
        CHECK_EQ(fix.queue.Count, 1, "one outstanding");

        /* Short on the first of three TRBs, and that is the only event the
         * controller ever sends for this TD. */
        CHECK_EQ(deliver(&fix, fix.transfers[0].FirstIndex,
                         XHCI_CC_SHORT_PACKET, 40, &result),
                 XHCI_XFER_OK, "the only event this TD produces");
        CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
                 "which defers rather than completing");
        CHECK_EQ(fix.queue.Count, 1, "so it is still queued");

        /*
         * And the drain pass then empties without producing the tail. That is
         * what proves the tail is not coming - the whole content of task 9-0.2 -
         * and it is the only path on which the departure is now taken.
         */
        CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "the pass empties");
        CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "it completes");
        CHECK_EQ(result.CompletedCount, 1, "one transfer");
        CHECK_EQ(result.Completed, &fix.transfers[0], "and it is ours");
        CHECK_EQ(result.NeedsRecovery, 0, "with no recovery asked for");
        CHECK_EQ(fix.transfers[0].BytesTransferred, 24, "64 - 40");
        CHECK_EQ(fix.queue.Count, 0, "and leaves the queue empty");
        CHECK_EQ(XhciRingFree(&fix.ring), XhciRingCapacity(&fix.ring),
                 "with the whole ring back for the next one");
    }
    CHECK_EQ(fix.queue.MidTdShortRetires, 3, "three receives, three retires");
    CHECK_EQ(fix.queue.Completed, 3, "and three completions to usbport");
    CHECK_EQ(fix.queue.OrphanedGroups, 0, "nothing orphaned");
    /*
     * **The verdict, and after task 9-0.2 the primary half of it is the first
     * two lines rather than the last two.** A conforming controller would have
     * answered every deferral in-band and reached no settle at all
     * (`test_event_normal_short_packet_terminates`); this one reaches the settle
     * three times out of three and answers none.
     */
    CHECK_EQ(fix.queue.MidTdDeferrals, 3, "three deferrals armed");
    CHECK_EQ(fix.queue.MidTdDeferralsTailed, 0,
             "and not one of them was answered by its tail - the one-event "
             "controller, which is what batch 8-V.2 measured");
    CHECK_EQ(fix.queue.MidTdShortTails, 0,
             "no tail event ever arrived, in-band or after");
    CHECK_EQ(fix.queue.MidTdTailsDropped, 0,
             "and each was answered-or-not before the next, so nothing evicted");
    CHECK_EQ(fix.queue.MidTdDeferralsLost, 0, "nothing lost");
    CHECK_EQ(fix.queue.MidTdDeferrals,
             fix.queue.MidTdDeferralsTailed +
             fix.queue.MidTdDeferralsTailedSpurious +
             fix.queue.MidTdShortRetires +
             fix.queue.MidTdDeferralsLost + XhciXferDeferralsArmed(&fix.queue),
             "and the partition holds - the identity XhciSlotDrainSettled "
             "checks at controller level");
}

/*
 * **Each half of the whole-TD-is-data predicate, on its own.**
 *
 * `XhciXferEvent` takes the early-retire departure only when the data range
 * *is* the whole placement - `DataFirstIndex == FirstIndex` **and**
 * `DataTrbCount == TrbCount`. Every shape this driver builds today differs in
 * both at once (a control transfer's data starts one TRB in and never covers
 * the Setup and Status TRBs), so a mutation dropping either conjunct failed
 * zero checks; `xhci_xfer.c` says so in a comment and the repo audit
 * finding 3 called that in as the test gap it is.
 *
 * These build the two half-mismatched records directly. Neither is a shape the
 * builder emits, which is the point: the predicate has to refuse them before
 * Phase 9's isochronous TDs arrive and make one of them reachable.
 */
static void test_event_half_data_range_refuses_early_retire(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    PXHCI_TRANSFER t;

    /* Half one: the data range starts at the head but stops short of the tail,
     * so something follows it - exactly the Status Stage's position. */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "submitted");
    t = &fix.transfers[0];
    CHECK_EQ(t->DataFirstIndex, t->FirstIndex, "(first conjunct holds)");
    t->DataTrbCount = t->TrbCount - 1;

    CHECK_EQ(deliver(&fix, 0, XHCI_CC_SHORT_PACKET, 32, &result), XHCI_XFER_OK,
             "the short packet event, mid-TD");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "not terminal - the data does not reach the end of the placement");
    CHECK_EQ(fix.queue.MidTdShortRetires, 0, "so nothing was retired early");
    CHECK_EQ(fix.queue.MidTdShortTails, 0, "and no tail is owed");
    CHECK_EQ(fix.queue.MidTdDeferrals, 0,
             "and no deferral was armed either - after task 9-0.2 that is the "
             "half a wrong shape has to be refused at, since the retire is "
             "downstream of it");
    CHECK_EQ(fix.ring.Dequeue, 0, "the dequeue pointer did not move");

    /* Half two: the count matches, but the range does not start at the head - a
     * TRB precedes the data, which is the Setup Stage's position. The count is
     * left equal on purpose: it is what makes the *second* conjunct hold, so
     * this vector fails only if the **first** one is missing. A first draft set
     * both fields and therefore isolated neither - it passed with the first
     * conjunct deleted, which is the very mutation finding 3 asked about. */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "submitted");
    t = &fix.transfers[0];
    t->DataFirstIndex = t->FirstIndex + 1;
    CHECK_EQ(t->DataTrbCount, t->TrbCount,
             "(the second conjunct still holds, so only the first can refuse)");

    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SHORT_PACKET, 32, &result), XHCI_XFER_OK,
             "the short packet event, inside the data range");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "not terminal - the placement begins before the data does");
    CHECK_EQ(fix.queue.MidTdShortRetires, 0, "so nothing was retired early");
    CHECK_EQ(fix.queue.MidTdShortTails, 0, "and no tail is owed");
    CHECK_EQ(fix.queue.MidTdDeferrals, 0,
             "and no deferral was armed either - after task 9-0.2 that is the "
             "half a wrong shape has to be refused at, since the retire is "
             "downstream of it");
    CHECK_EQ(fix.ring.Dequeue, 0, "the dequeue pointer did not move");
}

/*
 * **A deferral answered by a later TD's event, which is a stronger answer than
 * the settle's and had to be built when task 9-0.2 made it reachable.**
 *
 * An endpoint that keeps two receives posted can have the first end short and
 * the second complete normally before any drain pass empties. The second one's
 * retire jumps the dequeue pointer past both TDs, so the first is swept - and
 * the sweep's existing rule is that a swept transfer is a dropped event and is
 * failed with `USBD_STATUS_INTERNAL_HC_ERROR`.
 *
 * That is wrong for a deferred transfer, and it is a regression the deferral
 * would have introduced: the receive really did arrive, was measured, and would
 * have been reported as a controller error on every endpoint keeping more than
 * one transfer posted. Before the deferral it could not happen, because the
 * short one was retired where it landed and never sat behind anything.
 *
 * Being swept is also **proof the tail was never sent**, and better proof than
 * the settle has: the drain is FIFO, so a tail for the earlier TD would be
 * ahead of this event in the ring and would already have been consumed. So it
 * is counted as the early retire it is, with no tail record - there is no
 * window left for one to arrive in.
 */
static void test_event_mid_td_deferral_swept_by_a_later_td(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    PXHCI_TRANSFER first;
    PXHCI_TRANSFER second;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "first");
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 1), XHCI_XFER_OK, "second");
    first = &fix.transfers[0];
    second = &fix.transfers[1];
    CHECK_EQ(fix.queue.Count, 2, "two receives posted at once");

    /* The first ends short mid-TD and defers. */
    CHECK_EQ(deliver(&fix, first->FirstIndex, XHCI_CC_SHORT_PACKET, 40,
                     &result),
             XHCI_XFER_OK, "the first ends short");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "deferred");
    CHECK_EQ(fix.queue.MidTdDeferrals, 1, "one deferral armed");

    /* The second completes normally, on its own last TRB, in the same pass. */
    CHECK_EQ(deliver(&fix, second->LastIndex, XHCI_CC_SUCCESS, 0, &result),
             XHCI_XFER_OK, "the second completes");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "and it completes");
    CHECK_EQ(result.CompletedCount, 2,
             "taking the swept first transfer with it");
    CHECK_EQ(result.Completed, first, "oldest first");

    /* **The half that would have been the regression.** */
    CHECK_EQ((ULONG)first->UsbdStatus, WANT_USBD_SUCCESS,
             "the swept receive is a success carrying its measured length, "
             "not a controller error");
    CHECK_EQ(first->BytesTransferred, 24, "64 - 40, what it measured");
    CHECK_EQ(fix.queue.SweptTransfers, 0,
             "and it is not counted as a dropped event, because it is not one");

    CHECK_EQ(fix.queue.MidTdShortRetires, 1,
             "it is counted as the early retire it is");
    CHECK_EQ(fix.queue.MidTdDeferralsTailed, 0, "no tail answered it");
    CHECK_EQ(fix.queue.MidTdDeferralsLost, 0, "and it was not lost");
    CHECK_EQ(fix.queue.MidTdTailCount, 0,
             "no tail record, because FIFO ordering already proved the tail "
             "was never sent - there is no window for one to arrive in");
    CHECK_EQ(XhciXferDeferralsArmed(&fix.queue), 0, "none still armed");
    CHECK_EQ(fix.queue.MidTdDeferrals,
             fix.queue.MidTdDeferralsTailed +
             fix.queue.MidTdDeferralsTailedSpurious +
             fix.queue.MidTdShortRetires +
             fix.queue.MidTdDeferralsLost + XhciXferDeferralsArmed(&fix.queue),
             "and the partition holds");

    /* Nothing is left for a settle to find. */
    CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "a settle runs");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "and finds nothing");
}

/*
 * **A deferral the queue loses rather than answers.** A teardown drain and an
 * `AbortTransfer` both take a transfer off the queue without any statement
 * about the tail it was waiting for, so the observation ends unanswered - and
 * it is counted there rather than nowhere, because the partition
 *
 *     deferrals == tailed + tailedSpurious + retired + lost + still armed
 *
 * is what makes `MidTdDeferralsTailed` readable as a verdict. Counting the loss
 * in nothing is the shape repo audit finding 17 removed from the censored
 * accounting once already: it makes this driver's inability to look
 * indistinguishable from the controller's answer.
 *
 * The accounting lives at the **unlink**, not at each way a transfer can end,
 * which is why this vector exercises the two entry points rather than the four
 * callers: `XhciXferQueueDrain` and `XhciXferQueueRemove` are the whole set.
 */
static void test_event_mid_td_deferral_lost_on_unlink(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    PXHCI_TRANSFER list;
    ULONG count;

    /* One: the teardown drain. */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "posted");
    CHECK_EQ(deliver(&fix, fix.transfers[0].FirstIndex,
                     XHCI_CC_SHORT_PACKET, 40, &result),
             XHCI_XFER_OK, "short mid-TD, deferred");
    CHECK_EQ(XhciXferDeferralsArmed(&fix.queue), 1, "armed");

    count = 0;
    list = XhciXferQueueDrain(&fix.queue, XHCI_USBD_STATUS_CANCELED, &count);
    CHECK_EQ(count, 1, "the drain takes it");
    CHECK_EQ(list, &fix.transfers[0], "and hands it back");
    CHECK_EQ(fix.queue.MidTdDeferralsLost, 1,
             "the observation is counted as lost, not as either verdict");
    CHECK_EQ(fix.queue.MidTdShortRetires, 0, "nothing was retired");
    CHECK_EQ(fix.queue.MidTdDeferralsTailed, 0, "and nothing was answered");
    CHECK_EQ(XhciXferDeferralsArmed(&fix.queue), 0, "and none is still armed");
    CHECK_EQ(fix.queue.MidTdDeferrals,
             fix.queue.MidTdDeferralsTailed +
             fix.queue.MidTdDeferralsTailedSpurious +
             fix.queue.MidTdShortRetires +
             fix.queue.MidTdDeferralsLost + XhciXferDeferralsArmed(&fix.queue),
             "the partition holds");

    /* A settle afterwards must find nothing - the flag was cleared at the
     * unlink, so a stale one would make the walk chase a transfer usbport has
     * already been handed back. */
    CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "a settle runs");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "and finds nothing");

    /* Two: the abort's single-transfer removal. */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "posted");
    CHECK_EQ(deliver(&fix, fix.transfers[0].FirstIndex,
                     XHCI_CC_SHORT_PACKET, 40, &result),
             XHCI_XFER_OK, "short mid-TD, deferred");
    CHECK_EQ(XhciXferQueueRemove(&fix.queue, &fix.transfers[0]), 1,
             "the abort removes it");
    CHECK_EQ(fix.queue.MidTdDeferralsLost, 1, "counted as lost");
    CHECK_EQ(XhciXferDeferralsArmed(&fix.queue), 0, "none still armed");
    CHECK_EQ(fix.queue.MidTdDeferrals,
             fix.queue.MidTdDeferralsTailed +
             fix.queue.MidTdDeferralsTailedSpurious +
             fix.queue.MidTdShortRetires +
             fix.queue.MidTdDeferralsLost + XhciXferDeferralsArmed(&fix.queue),
             "the partition holds here too");
}
/*
 * **The settle asks the ring again, and refuses when the answer has changed.**
 *
 * The arm happens at the event; the retire happens at the end of the pass.
 * Between the two the ring can move - a Set TR Dequeue from a recovery, or a
 * record and a ring that have drifted apart - so the settle re-runs
 * `XhciRingClassifyEvent` on **the TRB the short event named** and refuses
 * unless the ring's own chain walk still ends that TD where the record says
 * it does. Classifying from `LastIndex` instead would ask the ring whether
 * `LastIndex` is `LastIndex`.
 *
 * Without the re-check the retire jumps the dequeue pointer to whatever the
 * corrupted chain calls the tail, which here is inside the *next* transfer's
 * TRBs - reclaiming a live TD. The mutation that drops it is caught by the
 * three assertions below.
 *
 * The `!completion.CanRetire` conjunct beside it is **not independently
 * reachable** and is documented as consistency rather than claimed: a
 * classification that makes the short TRB a TD tail also makes `TailIndex`
 * that TRB, which the `LastIndex` test already refuses, because the arm site
 * only fires when the event did *not* name the last TRB.
 */
static void test_settle_refuses_when_the_ring_moved(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "first");
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 1), XHCI_XFER_OK, "second");
    CHECK_EQ(fix.transfers[0].LastIndex, 2, "(the record says the TD ends at 2)");
    CHECK_EQ(fix.transfers[1].FirstIndex, 3, "(and the next one starts at 3)");

    /* The ring and the record agree at the moment of the event, so the
     * deferral is armed. */
    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SHORT_PACKET, 32, &result),
             XHCI_XFER_OK, "the short packet event, mid-TD");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "deferred");
    CHECK_EQ(fix.queue.MidTdDeferrals, 1, "one deferral armed");

    /* And now they do not: chaining TRB 2 runs the ring's walk on into the
     * following transfer's TRBs, so the ring's tail for this TD becomes 5. */
    fix.mem[2].Control |= XHCI_TRB_CH;

    CHECK_EQ(settle(&fix, &result), XHCI_XFER_OK, "the pass empties");
    /*
     * **The transfer is NOT ended here**, and that assertion used to say the
     * opposite. The batch-9-0 review round 2 found why it matters: this branch's
     * whole premise is that nothing halted the endpoint, so it is *Running* -
     * and completing the transfer hands its mapped buffer back to usbport while
     * the xHC may still own or have prefetched the TRBs that name it. The
     * endpoint has to be stopped first, which is the caller's job and not this
     * layer's, so the transfer stays queued and `NeedsRecovery` is the signal.
     */
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "nothing is completed - the endpoint is still running and still "
             "owns these TRBs");
    CHECK_EQ(result.CompletedCount, 0, "and nothing is handed back");
    CHECK_EQ(fix.queue.MidTdShortRetires, 0,
             "nothing was retired either: the ring's tail would have taken the "
             "next transfer's TRBs with it");
    CHECK_EQ(fix.queue.MidTdTailCount, 0,
             "and no tail record was made, because no retire waited for one");
    /* The *request*, which is all this layer can see: the transfer core has no
     * endpoint, no quiesce and no command engine, so it cannot say what the
     * slot layer does with it. "The endpoint is stopped" was a claim about code
     * this vector never executes - and that gap is why the slot layer's
     * response to this refusal went unchecked long enough for the batch-9-0
     * review to find two different defects in it. The property is pinned by
     * `test_slot_bulk_settle_refusal_does_not_halt_the_endpoint` in
     * `test_init.c`. */
    CHECK_EQ(result.NeedsRecovery, 1,
             "and recovery is requested rather than quietly carrying on");
    CHECK_EQ(fix.queue.Count, 2,
             "both transfers are still outstanding, the refused one included");
    CHECK_EQ(XhciXferDeferralsArmed(&fix.queue), 0,
             "but the deferral is disarmed, so no later pass re-examines it");
    /*
     * **This assertion used to read `== 0`**, on the reasoning that the settle
     * had answered the deferral and merely answered "the ring will not have
     * it". That reasoning is wrong in the way that matters: the flag is cleared
     * here and no retire happened, so counting it nowhere left the deferral
     * outside every term of the partition, and `XhciSlotDrainSettled`'s
     * identity came up one short - setting the sticky
     * `MidTdDeferAccountingBroken` that voids every mid-TD counter. A vector
     * written for a different defect found it; nothing at this layer had ever
     * checked the partition across a refusal.
     *
     * `Lost` is the right term: a record/ring divergence says nothing about
     * whether the promised tail was ever sent, which is precisely what Lost
     * means.
     */
    CHECK_EQ(fix.queue.MidTdDeferralsLost, 1,
             "the deferral is counted as an observation lost - the settle "
             "ended it without learning anything about the tail");
    CHECK_EQ(XhciXferDeferralsArmed(&fix.queue), 0, "none still armed");
}
/*
 * The early retire asks the **ring** where the TD ends and refuses if the ring
 * and the transfer record disagree.
 *
 * The shape test above is read off the transfer record alone. The ring's own
 * answer comes from the Chain flags the hardware reads, and the two are
 * independent statements of where this TD ends - so if they have drifted apart,
 * jumping the dequeue pointer on the strength of the record would reclaim TRBs
 * whose ownership nothing has established. The divergence is manufactured here
 * by clearing a Chain bit behind the engine's back, which is what a stale TRB
 * or memory written under the driver looks like from this side; the queue
 * already carries `SumFailures` for the same class of fault.
 */
static void test_event_normal_short_ring_record_disagree(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "first");
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 1), XHCI_XFER_OK, "second");
    CHECK_EQ(fix.transfers[0].LastIndex, 2, "(the record says the TD ends at 2)");
    CHECK_EQ(fix.transfers[1].FirstIndex, 3, "(and the next one starts at 3)");

    /* The ring now says otherwise: chaining TRB 2 runs the walk on into the
     * following transfer's TRBs, so the ring's tail for this TD is 5. */
    fix.mem[2].Control |= XHCI_TRB_CH;

    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SHORT_PACKET, 32, &result), XHCI_XFER_OK,
             "the short packet event, mid-TD by either reading");
    CHECK_EQ(fix.queue.MidTdShortRetires, 0,
             "no early retire on a TD the ring and the record disagree about - "
             "the ring's tail would have taken the next transfer's TRBs with it");
    /*
     * **And nothing is completed either**, which is the batch-9-0 review round
     * 5 finding. This used to fall through to `xhciXferFinishGroup`: the
     * `wholeTdIsData && SHORT` clause made it terminal, so the transfer was
     * detached and handed up with `OrphanedGroups` and a plain `NeedsRecovery`
     * - and the slot layer's recovery for a Short Packet code **halts the
     * endpoint**. That is the same defect the settle-time refusal had, reached
     * a pass earlier; only the settle-time route had been fixed.
     *
     * The endpoint is Running, so its TRBs are still the xHC's: the transfer
     * stays queued and the caller is told to stop it first.
     */
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "nothing is completed");
    CHECK_EQ(result.CompletedCount, 0, "and nothing is handed back");
    CHECK_EQ(fix.queue.OrphanedGroups, 1,
             "and it is still counted as the unanticipated state it is - the "
             "counter says a group ended with nothing reclaimed, which is true "
             "whether or not the transfer is answered here");
    CHECK_EQ(result.RefusedRetire, 1,
             "the caller is asked for a stop-then-drain specifically, not for "
             "the generic recovery that reads the completion code and halts");
    CHECK_EQ(result.NeedsRecovery, 1, "with recovery owed either way");
    CHECK_EQ(fix.queue.Count, 2,
             "both transfers are still outstanding, this one included");
    /* Nothing is placed either: the placement only happens for a group that
     * ended, and this one has not. The dequeue pointer must not move while the
     * endpoint may still be executing from it. */
    CHECK_EQ(fix.queue.PlacementFailures, 0,
             "no placement was attempted on a ring the endpoint may still be "
             "reading");
    CHECK_EQ(fix.ring.Dequeue, 0, "leaving the dequeue pointer where it was");
}

/*
 * **The divergence with the event naming the record's LAST TRB**, which the
 * first version of the interception missed entirely.
 *
 * That version tested the *shape* - a whole-data TD ending short somewhere
 * other than its last TRB. Here the short names `LastIndex` itself, so it is
 * terminal by position, while the ring says that index is not the TD's tail
 * because a `CH` bit merges this TD into the next one. The arm refuses (the
 * ring's tail is later), the shape test does not match, the retire cannot
 * happen - and the transfer was completed and the endpoint halted, which is the
 * defect three earlier rounds had each fixed one route of.
 *
 * The test is now about the **outcome**: the group ended, nothing was
 * reclaimed, and the completion code halted nothing. That covers every shape,
 * including this one and any future one.
 */
static void test_event_short_on_last_trb_ring_disagrees(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "first");
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 1), XHCI_XFER_OK, "second");
    CHECK_EQ(fix.transfers[0].LastIndex, 2, "(the record says the TD ends at 2)");

    /* The ring says TRB 2 is not a tail at all - it chains on into the next
     * transfer's TRBs. */
    fix.mem[2].Control |= XHCI_TRB_CH;

    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SHORT_PACKET, 32, &result), XHCI_XFER_OK,
             "a short packet naming the record's own last TRB");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "nothing is completed - the retire could not happen and a Short "
             "Packet leaves the endpoint Running");
    CHECK_EQ(result.CompletedCount, 0, "and nothing is handed back");
    CHECK_EQ(result.RefusedRetire, 1,
             "it is reported as a refused retire, so the caller stops the "
             "endpoint rather than running the halt recovery a Short Packet "
             "code would otherwise select");
    CHECK_EQ(result.NeedsRecovery, 1, "with recovery owed");
    CHECK_EQ(fix.queue.Count, 2, "both transfers are still queued");
    CHECK_EQ(fix.queue.OrphanedGroups, 1,
             "and the unanticipated state is still counted");
    CHECK_EQ(fix.ring.Dequeue, 0, "the dequeue pointer has not moved");
}

/*
 * **The first measurement still wins**, on the one path a Normal TD can still
 * reach it: a spurious Success carrying a residual is *not* terminal mid-TD, so
 * the TD's own tail event arrives with the transfer still live and must not
 * move the length.
 *
 * The short-packet pair used to be this property's only Normal-TD vector, and
 * the fix above took that path away - so it is re-established here rather than
 * left resting on the control transfers, whose Status Stage TRB has no length
 * field and therefore never had anything to overwrite with.
 */
static void test_event_normal_first_measurement_wins(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    PXHCI_TRANSFER t;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "submitted");
    t = &fix.transfers[0];

    /* The quirk: code Success on the first TRB, 32 of its 64 bytes missing. */
    CHECK_EQ(deliver(&fix, 0, XHCI_CC_SUCCESS, 32, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "not terminal - Success is not the short-packet departure");
    CHECK_EQ(t->BytesTransferred, 32, "64 - 32");
    CHECK_EQ(fix.queue.ShortSuccesses, 1, "counted as the quirk it is");
    CHECK_EQ(fix.queue.MidTdShortRetires, 0, "and not as a short packet");

    /* The TD's own tail event, whose sum runs to the end of the TD. */
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "this one completes it");
    CHECK_EQ(t->BytesTransferred, 32,
             "still 32 - not 160, which would hand usbport 128 bytes of buffer "
             "the device never wrote");
}

/*
 * `IntermediateEvents` is documented as zero on every conforming controller -
 * it counts a Success on a data TRB that is not the group's last, which this
 * driver gives no controller a reason to emit. An interrupt TD's last TRB *is* a
 * data TRB and carries IOC, so without the terminal test every ordinary HID
 * report would be counted and the diagnostic would be worthless from the first
 * transfer onward.
 */
static void test_event_normal_is_not_intermediate(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt(&fix, 0, 8), XHCI_XFER_OK, "submitted");
    CHECK_EQ(deliver(&fix, 0, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completed");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 8, "all eight bytes");
    CHECK_EQ(fix.queue.IntermediateEvents, 0,
             "an ordinary interrupt completion is not an intermediate event");
    CHECK_EQ(fix.queue.ShortSuccesses, 0, "nor a spurious success");

    /* The multi-TRB case likewise: only the terminal event arrives. */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "submitted");
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 160, "all 160 bytes");
    CHECK_EQ(fix.queue.IntermediateEvents, 0, "still not intermediate");

    /*
     * And a Success on a TRB that really is *not* the last still counts, so the
     * terminal test narrowed the counter rather than disabling it. This is the
     * shape a controller emitting an unasked-for IOC would produce.
     */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_interrupt_multi(&fix, 0), XHCI_XFER_OK, "submitted");
    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "not terminal");
    CHECK_EQ(fix.queue.IntermediateEvents, 1,
             "a Success on a non-final data TRB is still counted");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 128,
             "and still fixes the length at what it measured");
}

static void test_submit_normal_ring_full(void)
{
    XFER_FIXTURE fix;
    ULONG enqueueBefore;
    ULONG i;

    /* 8 TRBs is 6 usable slots, and each transfer here is one TRB. */
    fixture_init(&fix, 8);
    for (i = 0; i < 4; i++) {
        CHECK_EQ(fixture_submit_interrupt(&fix, i % 4, 8), XHCI_XFER_OK,
                 "fits");
    }
    /* Fill the remaining two slots with the same transfer records - the queue
     * bookkeeping is not what this checks, the ring's refusal is. */
    (VOID)fixture_submit_interrupt(&fix, 0, 8);
    (VOID)fixture_submit_interrupt(&fix, 1, 8);
    enqueueBefore = fix.ring.Enqueue;
    CHECK_EQ(XhciRingFree(&fix.ring), 0, "the ring is full");

    CHECK_EQ(fixture_submit_interrupt(&fix, 2, 8), XHCI_XFER_BUSY,
             "the next transfer is refused");
    CHECK_EQ(fix.ring.Enqueue, enqueueBefore, "nothing was written");
}

/* ------------------------------------------------------------------ */
/* 3b. Task 8-A.1: long TDs, the cap, and the wrap                     */
/* ------------------------------------------------------------------ */

/*
 * The policy cap on a Normal TD, checked at the boundary in both directions.
 *
 * `XHCI_XFER_MAX_DATA_TRBS` is what `XHCI_MAX_TRANSFER` and the ring
 * capacity are both sized against, so the number that matters is not "large" but
 * "exactly this, and one more is refused rather than truncated". A builder that
 * silently emitted 32 TRBs for a 33-element list would move the first 32 pages of
 * the buffer and tell usbport the whole transfer succeeded.
 */
static void test_build_normal_at_the_trb_cap(void)
{
    XHCI_NORMAL_REQUEST req;
    XHCI_TRB out[XHCI_XFER_MAX_DATA_TRBS];
    SG_BUFFER sg;
    ULONG count;
    ULONG i;
    ULONG chained;

    sg_init(&sg);
    for (i = 0; i < XHCI_XFER_MAX_DATA_TRBS; i++) {
        sg_add(&sg, 0x0D000000UL + i * 0x1000UL, 4096, i * 4096);
    }
    normal_request_init(&req, XHCI_XFER_MAX_DATA_TRBS * 4096UL, 1, 512, &sg);

    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_OK, "a TD at the policy cap builds");
    CHECK_EQ(count, XHCI_XFER_MAX_DATA_TRBS, "as one TRB per element");

    chained = 0;
    for (i = 0; i < count; i++) {
        if (out[i].Control & XHCI_TRB_CH) {
            chained++;
        }
    }
    CHECK_EQ(chained, XHCI_XFER_MAX_DATA_TRBS - 1,
             "chained end to end but for the last - one TD, not thirty-two");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[0].Status), 31,
             "TD Size saturates: 256 packets remain and the field holds five "
             "bits (p.199)");
    CHECK_EQ(XHCI_TRB_GET_TD_SIZE(out[count - 1].Status), 0,
             "and is cleared on the last TRB");

    /* One element more than the cap. Refused, and refused **before the list is
     * walked** - the count check is what stops a list this driver never agreed
     * to from being indexed in memory usbport sized. */
    sg_add(&sg, 0x0D000000UL + XHCI_XFER_MAX_DATA_TRBS * 0x1000UL, 4096,
           XHCI_XFER_MAX_DATA_TRBS * 4096UL);
    normal_request_init(&req, (XHCI_XFER_MAX_DATA_TRBS + 1) * 4096UL, 1, 512,
                        &sg);
    CHECK_EQ(XhciXferBuildNormal(&req, 1, out, XHCI_XFER_MAX_DATA_TRBS, &count),
             XHCI_XFER_TOO_MANY_TRBS, "one element past the cap is refused");
    CHECK_EQ(count, 0, "with nothing reported as built");
}

/*
 * A long Normal TD **spanning the Link TRB**, which is the shape task 8-A.1 calls
 * a long wrap and which no transfer before bulk could produce: a control group
 * crosses the link *between* its TDs (test_event_across_the_link), and a HID TD
 * is one TRB and cannot straddle anything.
 *
 * The thing that has to be right here is the Link TRB's own Chain bit. The link
 * is not part of the TD, but `XhciRingTdBounds` walks Chain flags to find a TD's
 * extent and the hardware follows them the same way, so a link left unchained
 * mid-TD ends the TD at the top of the ring - which would hand the xHC half a
 * transfer and leave the rest as a TD nothing owns.
 */
static void test_submit_normal_across_the_link(void)
{
    struct {
        XHCI_TRB mem[64];
        XHCI_RING ring;
        XHCI_TRANSFER_QUEUE queue;
        XHCI_TRB scratch[XHCI_XFER_MAX_CONTROL_TRBS];
        XHCI_TRANSFER transfer;
        SG_BUFFER sg;
    } big;
    XHCI_NORMAL_REQUEST req;
    XHCI_XFER_EVENT_RESULT result;
    ULONG head;
    ULONG tail;
    ULONG i;
    ULONG chained;
    ULONG index;

    CHECK_EQ(XhciRingInit(&big.ring, big.mem, RING_PA, 64,
                          XHCI_RING_KIND_ENDPOINT),
             XHCI_RING_OK, "a 64-TRB ring");
    XhciXferQueueInit(&big.queue);

    /*
     * Park the enqueue pointer at 55, so a 16-TRB TD covers 55..62, steps over
     * the Link TRB at 63, and continues at 0..7.
     */
    sg_init(&big.sg);
    sg_add(&big.sg, 0x0E000000UL, 8, 0);
    normal_request_init(&req, 8, 1, 512, &big.sg);
    for (i = 0; i < 55; i++) {
        XHCI_TD_COMPLETION completion;
        ULONG at;

        CHECK_EQ(XhciXferSubmitNormal(&big.queue, &big.ring, &req, 1,
                                      &big.transfer, (PVOID)0x4000UL,
                                      big.scratch, XHCI_XFER_MAX_CONTROL_TRBS),
                 XHCI_XFER_OK, "filler");
        at = big.transfer.LastIndex;
        CHECK_EQ(XhciRingClassifyEvent(&big.ring,
                                       XhciRingTrbPA(&big.ring, at),
                                       XHCI_CC_SUCCESS, &completion),
                 XHCI_RING_OK, "filler classified");
        CHECK_EQ(XhciRingRetireTd(&big.ring, &completion), XHCI_RING_OK,
                 "filler retired");
        XhciXferQueueInit(&big.queue);
    }
    CHECK_EQ(big.ring.Enqueue, 55, "parked two TRBs short of the wrap");

    sg_init(&big.sg);
    for (i = 0; i < 16; i++) {
        sg_add(&big.sg, 0x0E100000UL + i * 0x1000UL, 4096, i * 4096);
    }
    normal_request_init(&req, 16 * 4096UL, 1, 512, &big.sg);
    CHECK_EQ(XhciXferSubmitNormal(&big.queue, &big.ring, &req, 1, &big.transfer,
                                  (PVOID)0x5000UL, big.scratch,
                                  XHCI_XFER_MAX_CONTROL_TRBS),
             XHCI_XFER_OK, "a 16-TRB TD spanning the link is placed");
    CHECK_EQ(big.transfer.FirstIndex, 55, "starting where the ring was parked");
    CHECK_EQ(big.transfer.LastIndex, 7,
             "and ending eight TRBs past the wrap - the Link TRB is stepped "
             "over, not counted");
    CHECK_EQ(big.transfer.TrbCount, 16, "sixteen TRBs, none of them the link");
    CHECK_EQ(big.ring.Enqueue, 8, "the enqueue pointer follows");
    CHECK_EQ(big.ring.Cycle, 0, "and the producer cycle toggled at the link");

    CHECK(big.mem[63].Control & XHCI_TRB_CH,
          "the Link TRB carries Chain, because the TD continues past it");
    CHECK_EQ(XHCI_TRB_GET_TYPE(big.mem[63].Control), XHCI_TRB_TYPE_LINK,
             "(and is still a Link TRB)");

    /* The TD's extent, walked the way the driver and the hardware both walk it:
     * from any TRB in it, across the link, to the one that ends it. */
    CHECK_EQ(XhciRingTdBounds(&big.ring, 60, &head, &tail), XHCI_RING_OK,
             "bounds from inside the TD");
    CHECK_EQ(head, 55, "the head is before the wrap");
    CHECK_EQ(tail, 7, "the tail after it");

    chained = 0;
    index = 55;
    for (i = 0; i < 16; i++) {
        if (big.mem[index].Control & XHCI_TRB_CH) {
            chained++;
        }
        CHECK_EQ(big.mem[index].Param0, 0x0E100000UL + i * 0x1000UL,
                 "each TRB describes its own page, in order across the wrap");
        index = XhciRingNextIndex(&big.ring, index);
    }
    CHECK_EQ(chained, 15, "Chain on fifteen of the sixteen");

    /*
     * And the completion, whose event names a TRB on the far side of the wrap.
     * The length arithmetic sums TRB lengths by walking the ring, so this is
     * where a sum that used index comparison rather than the ring's own step
     * would produce a number from the wrong TRBs.
     */
    CHECK_EQ(XhciXferEvent(&big.queue, &big.ring, FIX_SLOT, FIX_DCI,
                           XhciRingTrbPA(&big.ring, 7),
                           event_dw2(XHCI_CC_SHORT_PACKET, 96),
                           event_dw3(FIX_SLOT, FIX_DCI), &result),
             XHCI_XFER_OK, "the tail event, short by 96 bytes");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completes the transfer");
    CHECK_EQ(result.Completed->BytesTransferred, 16 * 4096UL - 96UL,
             "with the whole TD summed across the wrap, less the residual");
    CHECK_EQ(XhciRingFree(&big.ring), XhciRingCapacity(&big.ring),
             "and every TRB of it reclaimed, the link included");
}

/* ------------------------------------------------------------------ */
/* 4. Events (task 6-A.2)                                              */
/* ------------------------------------------------------------------ */

static ULONG deliver(XFER_FIXTURE *fix,
                     ULONG index,
                     ULONG completionCode,
                     ULONG residual,
                     PXHCI_XFER_EVENT_RESULT result)
{
    return XhciXferEvent(&fix->queue, &fix->ring, FIX_SLOT, FIX_DCI,
                         XhciRingTrbPA(&fix->ring, index),
                         event_dw2(completionCode, residual),
                         event_dw3(FIX_SLOT, FIX_DCI),
                         result);
}

static void test_event_success(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");

    /* The ordinary path: one event, on the Status Stage TRB, code Success.
     * The Status TRB has no length field, so a fully successful transfer moved
     * everything it asked for. */
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "transfer completed");
    CHECK_EQ(result.CompletedCount, 1, "exactly one");
    CHECK_EQ(result.Completed, &fix.transfers[0], "and it is ours");
    CHECK_EQ(result.Completed->Next, NULL, "list terminated");
    CHECK_EQ((ULONG)result.Completed->UsbdStatus, WANT_USBD_SUCCESS, "success");
    CHECK_EQ(result.Completed->BytesTransferred, 18, "all 18 bytes");
    CHECK_EQ(result.NeedsRecovery, 0, "the endpoint is still running");
    CHECK_EQ(result.Fatal, 0, "and the controller is fine");

    CHECK_EQ(fix.queue.Count, 0, "the queue is empty");
    CHECK_EQ(fix.queue.Head, NULL, "head cleared");
    CHECK_EQ(fix.queue.Tail, NULL, "tail cleared");
    CHECK_EQ(fix.queue.Completed, 1, "counted");
    CHECK_EQ(fix.queue.Errors, 0, "no errors");
    CHECK_EQ(XhciRingFree(&fix.ring), XhciRingCapacity(&fix.ring),
             "one event retired all three TDs");
    CHECK_EQ(fix.queue.OrphanedGroups, 0, "and nothing was orphaned");

    /* A trailing event for a transfer that is already gone is the expected
     * case, not an error (4.11.3.1). */
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "nothing to do");
    CHECK_EQ(fix.queue.UnmatchedEvents + fix.queue.ForeignEvents, 1,
             "counted once, as unowned");
}

/*
 * A zero-length control transfer - SET_CONFIGURATION and every no-data request.
 * There is no data stage, so the completion carries zero bytes and no residual
 * arithmetic runs at all.
 */
static void test_event_zero_length(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 0), XHCI_XFER_OK, "submitted");
    CHECK_EQ(fix.transfers[0].LastIndex, 1, "Status Stage TRB is at index 1");

    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completed");
    CHECK_EQ(result.Completed->BytesTransferred, 0, "zero bytes moved");
    CHECK_EQ((ULONG)result.Completed->UsbdStatus, WANT_USBD_SUCCESS, "success");
    CHECK_EQ(fix.queue.ResidualIgnored, 0, "and no residual was seen");
}

/*
 * Short packet on a single-TRB data stage: the device answered with less than
 * was asked for. Two events arrive - one for the data TRB carrying the residual
 * (ISP), one for the Status Stage TRB (IOC) - and only the second completes the
 * transfer, because "the xHC shall advance to the Status Stage TD" (p.433).
 */
static void test_event_short_packet(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");

    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SHORT_PACKET, 10, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "a short packet on the data stage does not end the transfer");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 8, "18 asked, 10 not delivered");
    CHECK(fix.transfers[0].Flags & XHCI_XFER_FLAG_LENGTH_FIXED,
          "and the length is now fixed");
    CHECK_EQ(fix.queue.ShortPackets, 1, "counted");
    CHECK_EQ(XhciRingFree(&fix.ring), XhciRingCapacity(&fix.ring) - 3,
             "nothing is retired until the transfer ends");

    /* p.175: the second event repeats the Short Packet code and "the TRB
     * Transfer Length should be set to the same value that was reported by the
     * initial Short Packet Event". It must not be added to the length, and on
     * the Status Stage TRB - which has no length field - it is not a byte
     * count at all. */
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SHORT_PACKET, 10, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "the status event ends it");
    CHECK_EQ(result.Completed->BytesTransferred, 8, "still 8 bytes");
    CHECK_EQ((ULONG)result.Completed->UsbdStatus, WANT_USBD_SUCCESS,
             "a short transfer is a success with a length");
    CHECK_EQ(fix.queue.ResidualIgnored, 1,
             "the repeat's length was recognised as not a residual here");
    CHECK_EQ(result.NeedsRecovery, 0, "a short packet halts nothing");
    CHECK_EQ(XhciRingFree(&fix.ring), XhciRingCapacity(&fix.ring), "retired");
}

/*
 * The vector this suite exists for. A short packet on the *second* of three
 * data TRBs: the residual applies to that TRB's buffer alone.
 *
 *   spec p.175: total received = sum of the TRB Transfer Length fields up to
 *   and including the reporting TRB, minus the residue.
 *
 * Here that is (128 + 128) - 100 = 156. The formula this driver used to carry -
 * "requested - residual" - would answer 320 - 100 = 220, and nothing downstream
 * could tell the difference until 64 bytes of a descriptor were wrong.
 */
static void test_event_short_packet_multi_trb(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    sg_init(&fix.sg);
    sg_add(&fix.sg, 0x0A300000UL, 128, 0);
    sg_add(&fix.sg, 0x0A400000UL, 128, 128);
    sg_add(&fix.sg, 0x0A500000UL, 64, 256);
    request_init(&fix.req, 0x80, 0x06, 0, 0, 320, 320, 64, &fix.sg);
    CHECK_EQ(XhciXferSubmitControl(&fix.queue, &fix.ring, &fix.req,
                                   &fix.transfers[0], (PVOID)0x2000UL,
                                   fix.scratch, XHCI_XFER_MAX_CONTROL_TRBS),
             XHCI_XFER_OK, "three-TRB data stage submitted");
    CHECK_EQ(fix.transfers[0].DataFirstIndex, 1, "data starts at index 1");
    CHECK_EQ(fix.transfers[0].DataTrbCount, 3, "three data TRBs");
    CHECK_EQ(fix.transfers[0].LastIndex, 4, "status TRB at index 4");

    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SHORT_PACKET, 100, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "deferred");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 156,
             "(128 + 128) - 100, not 320 - 100");

    CHECK_EQ(deliver(&fix, 4, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completed");
    CHECK_EQ(result.Completed->BytesTransferred, 156, "and the length survived");
    CHECK_EQ((ULONG)result.Completed->UsbdStatus, WANT_USBD_SUCCESS, "success");

    /* A short packet on the *first* data TRB of the same shape reports only
     * that TRB's buffer. */
    fixture_init(&fix, 32);
    sg_init(&fix.sg);
    sg_add(&fix.sg, 0x0A300000UL, 128, 0);
    sg_add(&fix.sg, 0x0A400000UL, 128, 128);
    sg_add(&fix.sg, 0x0A500000UL, 64, 256);
    request_init(&fix.req, 0x80, 0x06, 0, 0, 320, 320, 64, &fix.sg);
    CHECK_EQ(XhciXferSubmitControl(&fix.queue, &fix.ring, &fix.req,
                                   &fix.transfers[0], (PVOID)0x2000UL,
                                   fix.scratch, XHCI_XFER_MAX_CONTROL_TRBS),
             XHCI_XFER_OK, "resubmitted");
    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SHORT_PACKET, 8, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 120, "128 - 8");
}

/*
 * The spurious-success quirk (`docs/usb-xhci-info/xhci-programming.md`): some NEC uPD720200,
 * Fresco Logic FL1000 and early VIA VL800 revisions report completion code 1
 * for a transfer that delivered fewer bytes than requested, with the length
 * field still telling the truth.
 *
 * The failure this guards is silent and it is an *overreport*: the length is
 * computed correctly from the residual, nothing fixes it because the code says
 * Success, and the terminal Status Stage event then overwrites it with the full
 * requested length. usbport hands the URB back claiming bytes that never
 * arrived. Found by the stop-time review, not by this suite - which is why the
 * negative control below is here too.
 */
static void test_event_spurious_success(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");

    /* Success, on the data stage, with 10 bytes not delivered. */
    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SUCCESS, 10, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "it is still the Status Stage event that ends the transfer");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 8, "18 asked, 10 not delivered");
    CHECK(fix.transfers[0].Flags & XHCI_XFER_FLAG_LENGTH_FIXED,
          "and the measurement is latched against the status event");
    CHECK_EQ(fix.queue.ShortSuccesses, 1, "counted as the quirk it is");
    CHECK_EQ(fix.queue.ShortPackets, 0,
             "and not as a controller that reported code 13 properly");

    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completed");
    CHECK_EQ(result.Completed->BytesTransferred, 8,
             "8 bytes, not the 18 a Success code alone would have claimed");
    CHECK_EQ((ULONG)result.Completed->UsbdStatus, WANT_USBD_SUCCESS,
             "a short transfer is still a success - the length carries it");

    /* The same on a multi-TRB data stage, where the measurement is a sum. */
    fixture_init(&fix, 32);
    sg_init(&fix.sg);
    sg_add(&fix.sg, 0x0A300000UL, 128, 0);
    sg_add(&fix.sg, 0x0A400000UL, 128, 128);
    sg_add(&fix.sg, 0x0A500000UL, 64, 256);
    request_init(&fix.req, 0x80, 0x06, 0, 0, 320, 320, 64, &fix.sg);
    CHECK_EQ(XhciXferSubmitControl(&fix.queue, &fix.ring, &fix.req,
                                   &fix.transfers[0], (PVOID)0x2000UL,
                                   fix.scratch, XHCI_XFER_MAX_CONTROL_TRBS),
             XHCI_XFER_OK, "submitted");
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 100, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 156, "(128 + 128) - 100");
    CHECK_EQ(deliver(&fix, 4, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Completed->BytesTransferred, 156, "and it survived");

    /*
     * The zero-residual case, which the first version of this suite got exactly
     * backwards - it asserted that the *assumption* (the full requested length)
     * should win over the *measurement* (the bytes so far), which is the same
     * overreport the paragraph above exists to stop, one step removed.
     *
     * A Success naming a **non-final** data TRB with a residual of zero measures
     * the bytes moved up to that TRB. If the controller then went to the Status
     * Stage instead of on to the next data TRB, the transfer really ended there,
     * and reporting `RequestedLength` claims bytes that never arrived. Nothing
     * distinguishes the two from here, so the measurement wins: an underreport
     * is a short descriptor that usbport and usbhub retry or fail on, and an
     * overreport is an unwritten buffer tail read as valid data.
     *
     * Nothing legitimate is lost, and that is checkable rather than asserted:
     * this driver sets IOC on the Status Stage TRB alone (p.430) and ISP
     * produces code 13, so no data TRB has any reason to raise a Success event
     * at all. `IntermediateEvents` is what says one did.
     */
    fixture_init(&fix, 32);
    sg_init(&fix.sg);
    sg_add(&fix.sg, 0x0A300000UL, 128, 0);
    sg_add(&fix.sg, 0x0A400000UL, 192, 128);
    request_init(&fix.req, 0x80, 0x06, 0, 0, 320, 320, 64, &fix.sg);
    CHECK_EQ(XhciXferSubmitControl(&fix.queue, &fix.ring, &fix.req,
                                   &fix.transfers[0], (PVOID)0x2000UL,
                                   fix.scratch, XHCI_XFER_MAX_CONTROL_TRBS),
             XHCI_XFER_OK, "submitted");
    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "it does not end the transfer");
    CHECK_EQ(fix.queue.ShortSuccesses, 0, "a zero residual is not the quirk");
    CHECK_EQ(fix.queue.IntermediateEvents, 1,
             "but it is an event no TRB of ours asked for, and is counted");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 128, "128 bytes measured");
    CHECK(fix.transfers[0].Flags & XHCI_XFER_FLAG_LENGTH_FIXED,
          "and a measurement fixes the length, whatever the code says");
    CHECK_EQ(deliver(&fix, 3, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Completed->BytesTransferred, 128,
             "the 128 that were measured, not the 320 that were assumed");

    /*
     * And the case that must not change: **no** data-stage event at all, which
     * is what a conforming controller produces. Then nothing was measured, the
     * Status Stage event's Success is the only evidence there is, and it means
     * the whole control transfer completed.
     */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Completed->BytesTransferred, 18,
             "with nothing measured, a clean Success means everything moved");
    CHECK_EQ(fix.queue.IntermediateEvents, 0, "and no unexpected event occurred");
    CHECK_EQ(fix.queue.ShortSuccesses, 0, "nor the quirk");
}

/*
 * A residual larger than the bytes it applies to is not a number to clamp - it
 * is an answer the controller cannot have meant. Reporting `sum - residual` as
 * an unsigned subtraction would hand usbport a length near 4 GB.
 */
static void test_event_residual_rejected(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");

    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SHORT_PACKET, 19, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "an impossible residual does not decide when a transfer ends");
    CHECK_EQ(fix.queue.ResidualRejects, 1, "counted as a bad residual");
    CHECK_EQ(fix.queue.LengthOverruns, 0, "and not as anything else");
    CHECK_EQ(fix.queue.SumFailures, 0, "nor as a sum that could not be taken");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 0, "no length is reported");
    CHECK(fix.transfers[0].Flags & XHCI_XFER_FLAG_FAILED, "and it failed");

    /*
     * A later event that *does* measure something must not put a byte count
     * back on a transfer already failed. The controller has contradicted itself
     * once on this transfer; a second reading from it is not evidence, and
     * reporting a length beside a failure status is how a caller ends up
     * trusting one.
     */
    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SUCCESS, 4, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 0,
             "a failed transfer keeps the length it had when it failed");
    CHECK(fix.transfers[0].Flags & XHCI_XFER_FLAG_FAILED, "still failed");

    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "the status event ends it");
    CHECK_EQ((ULONG)result.Completed->UsbdStatus, WANT_USBD_INTERNAL_HC_ERROR,
             "reported as a controller error, not as a short success");
    CHECK_EQ(result.Completed->BytesTransferred, 0, "with no length");
    CHECK_EQ(fix.queue.Errors, 1, "counted as an error");

    /*
     * The three ways the arithmetic can fail have to be *separately* reachable,
     * and that is not a presentational point: with one shared counter, deleting
     * the `residual > sum` test failed no check in this suite at all, because
     * an unsigned wrap makes the result exceed the buffer bound and the *next*
     * test caught the same vector. Found by mutation, not by reading it.
     *
     * Here the TRB's length is larger than the transfer's own, so the sum
     * overruns the mapped buffer with a residual that is perfectly legal
     * against the TRB it names.
     */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");
    fix.mem[1].Status = (fix.mem[1].Status & ~XHCI_TRB_LENGTH_MASK) | 4096UL;
    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SHORT_PACKET, 100, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(fix.queue.LengthOverruns, 1, "counted as a length past the buffer");
    CHECK_EQ(fix.queue.ResidualRejects, 0, "the residual itself was legal");
    CHECK_EQ(fix.queue.SumFailures, 0, "and the sum was taken");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 0, "and no length reported");

    /*
     * And the third: the transfer record names TRBs the ring no longer holds.
     * Reachable only by moving the ring under the record, which is what a
     * divergence between the two would look like.
     */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");
    CHECK_EQ(XhciRingSetDequeue(&fix.ring, XhciRingTrbPA(&fix.ring,
                                                         fix.ring.Enqueue)),
             XHCI_RING_OK, "the ring is emptied behind the record's back");
    CHECK_EQ(deliver(&fix, 1, XHCI_CC_SHORT_PACKET, 4, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(fix.queue.SumFailures, 1, "counted as a sum that could not be taken");
    CHECK_EQ(fix.queue.ResidualRejects, 0, "not as a bad residual");
    CHECK_EQ(fix.queue.LengthOverruns, 0, "nor as a length past the buffer");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 0, "and no length reported");
}

/*
 * Any error halts a control endpoint - "All Transfer Ring error conditions
 * force the state of the associated endpoint to Halted and require system
 * software intervention to recover" (p.176) - so the Status Stage never runs
 * and the error event is the transfer's last. The ring must be left somewhere
 * the caller can program into Set TR Dequeue Pointer.
 */
static void test_event_stall(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");

    CHECK_EQ(deliver(&fix, 1, XHCI_CC_STALL, 18, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE,
             "an error on the data stage ends the transfer");
    CHECK_EQ((ULONG)result.Completed->UsbdStatus, WANT_USBD_STALL_PID, "stalled");
    CHECK_EQ(result.Completed->BytesTransferred, 0, "18 asked, 18 not delivered");
    CHECK_EQ(result.NeedsRecovery, 1, "and the endpoint is halted");
    CHECK_EQ(fix.queue.Recoveries, 1, "counted");
    CHECK_EQ(fix.queue.Errors, 1, "counted as an error");
    CHECK_EQ(fix.queue.OrphanedGroups, 0,
             "the halt is an anticipated path, not an unexplained one");

    /* Nothing else is queued, so the dequeue pointer is placed on the enqueue
     * position - the only legal target on an empty ring (p.172). */
    CHECK_EQ(fix.ring.Dequeue, fix.ring.Enqueue, "ring emptied");
    CHECK_EQ(XhciRingFree(&fix.ring), XhciRingCapacity(&fix.ring),
             "and its slots are back");
    CHECK_EQ(fix.queue.PlacementFailures, 0, "the position was accepted");

    /* With a transfer queued behind, the pointer goes to *its* Setup Stage
     * TRB: "software shall use a Set TR Dequeue Pointer Command to advance the
     * Transfer Ring to the next TD" (p.172). */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "first");
    CHECK_EQ(fixture_submit_in(&fix, 1, 18), XHCI_XFER_OK, "second");
    CHECK_EQ(deliver(&fix, 1, XHCI_CC_STALL, 18, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.CompletedCount, 1, "only the stalled transfer completes");
    CHECK_EQ(fix.queue.Count, 1, "the queued one stays queued");
    CHECK_EQ(fix.ring.Dequeue, fix.transfers[1].FirstIndex,
             "and the ring is advanced to its first TD");
    CHECK_EQ(XhciRingFree(&fix.ring), XhciRingCapacity(&fix.ring) - 3,
             "with only its three TRBs outstanding");
}

/* An error on the Status Stage TRB is both the transfer's end and a halt: the
 * ring layer retires it *and* asks for recovery. */
static void test_event_error_on_status(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");

    CHECK_EQ(deliver(&fix, 2, XHCI_CC_USB_TRANSACTION_ERROR, 0, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completed");
    CHECK_EQ((ULONG)result.Completed->UsbdStatus, WANT_USBD_DEV_NOT_RESPONDING,
             "the device did not answer");
    CHECK_EQ(result.NeedsRecovery, 1, "and the endpoint halted");
    CHECK_EQ(XhciRingFree(&fix.ring), XhciRingCapacity(&fix.ring),
             "a tail error retires the TD as well as halting the endpoint");
    CHECK_EQ(fix.queue.PlacementFailures, 0, "no explicit placement was needed");
    CHECK_EQ(fix.queue.OrphanedGroups, 0, "nothing unexplained");
}

/* Codes 26-28 arrive after a Stop Endpoint: software stopped the ring, and
 * what that means for the queued transfers is the slot layer's decision
 * (`XhciXferQueueStopped`), so this layer refuses them outright. Completing
 * the owner as cancelled and sweeping the transfers ahead of it would be wrong
 * for every stop but a drain. */
static void test_event_canceled(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    ULONG code;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");
    CHECK_EQ(fixture_submit_in(&fix, 1, 18), XHCI_XFER_OK, "and one behind it");

    for (code = WANT_CC_STOPPED; code <= WANT_CC_STOPPED_SHORT_PACKET; code++) {
        CHECK_EQ(deliver(&fix, 1, code, 12, &result), XHCI_XFER_OK, "ok");
        CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
                 "a Stopped code completes nothing here");
        CHECK_EQ(result.CompletedCount, 0, "and lists nothing");
        CHECK_EQ(result.NeedsRecovery, 0, "and asks for no recovery");
        CHECK_EQ(fix.queue.StoppedRefused, code - WANT_CC_STOPPED + 1,
                 "counted as refused");
    }
    CHECK_EQ(fix.queue.Count, 2, "both transfers are still queued");
    CHECK_EQ(fix.transfers[0].Flags & XHCI_XFER_FLAG_FAILED, 0,
             "the owner is not failed");
    CHECK_EQ(fix.transfers[1].Flags & XHCI_XFER_FLAG_FAILED, 0,
             "and nothing was swept");
    CHECK_EQ(fix.queue.Completed, 0, "nothing was completed");
    CHECK_EQ(fix.queue.UnmatchedEvents, 0, "and nothing was matched at all");
}

/*
 * A retire jumps the dequeue pointer past the matched TD, reclaiming everything
 * behind it in the same store. Any transfer still queued there has to be
 * completed here or it is leaked - and it is failed rather than reported as the
 * success its position implies, because reaching this state means an event this
 * driver depends on was dropped.
 */
static void test_event_sweeps_earlier_transfers(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "first");
    CHECK_EQ(fixture_submit_in(&fix, 1, 18), XHCI_XFER_OK, "second");
    CHECK_EQ(fix.queue.Count, 2, "two queued");

    /* The second transfer's Status Stage TRB completes, with no event ever
     * having arrived for the first. */
    CHECK_EQ(deliver(&fix, 5, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completed");
    CHECK_EQ(result.CompletedCount, 2, "both transfers come back");
    CHECK_EQ(result.Completed, &fix.transfers[0], "oldest first");
    CHECK_EQ(result.Completed->Next, &fix.transfers[1], "then the matched one");
    CHECK_EQ(result.Completed->Next->Next, NULL, "list terminated");

    CHECK_EQ((ULONG)fix.transfers[0].UsbdStatus, WANT_USBD_INTERNAL_HC_ERROR,
             "the swept transfer is failed, not silently succeeded");
    CHECK_EQ((ULONG)fix.transfers[1].UsbdStatus, WANT_USBD_SUCCESS,
             "the matched one is what the event said");
    CHECK_EQ(fix.transfers[1].BytesTransferred, 18, "with its length");
    CHECK_EQ(fix.queue.SweptTransfers, 1, "counted apart from ordinary errors");
    CHECK_EQ(fix.queue.Count, 0, "the queue is empty");
    CHECK_EQ(XhciRingFree(&fix.ring), XhciRingCapacity(&fix.ring),
             "and both groups are off the ring");
}

/* Transfers complete in order when each gets its own event. */
static void test_event_two_transfers_in_order(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "first");
    CHECK_EQ(fixture_submit_in(&fix, 1, 18), XHCI_XFER_OK, "second");

    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.CompletedCount, 1, "only the first");
    CHECK_EQ(result.Completed, &fix.transfers[0], "and it is the first");
    CHECK_EQ(fix.queue.Count, 1, "one still queued");
    CHECK_EQ(fix.queue.Head, &fix.transfers[1], "the second is now the head");
    CHECK_EQ(fix.queue.SweptTransfers, 0, "nothing was swept");

    CHECK_EQ(deliver(&fix, 5, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.CompletedCount, 1, "then the second");
    CHECK_EQ(result.Completed, &fix.transfers[1], "in order");
    CHECK_EQ(fix.queue.Count, 0, "queue empty");
    CHECK_EQ(fix.queue.Completed, 2, "both counted");
}

static void test_event_rejections(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");

    /* ED = 1: the parameter is Event Data, not an address. This driver places
     * no Event Data TRBs, so such an event is unexpected input. */
    CHECK_EQ(XhciXferEvent(&fix.queue, &fix.ring, FIX_SLOT, FIX_DCI,
                           XhciRingTrbPA(&fix.ring, 2),
                           event_dw2(XHCI_CC_SUCCESS, 0),
                           event_dw3(FIX_SLOT, FIX_DCI) | XHCI_TRB_ED,
                           &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "discarded");
    CHECK_EQ(fix.queue.EventDataEvents, 1, "counted as its own thing");
    CHECK_EQ(fix.queue.Count, 1, "and the transfer is untouched");

    /* An event for another endpoint of the same slot. */
    CHECK_EQ(XhciXferEvent(&fix.queue, &fix.ring, FIX_SLOT, FIX_DCI,
                           XhciRingTrbPA(&fix.ring, 2),
                           event_dw2(XHCI_CC_SUCCESS, 0),
                           event_dw3(FIX_SLOT, 3), &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "discarded");
    CHECK_EQ(fix.queue.ForeignEvents, 1, "wrong DCI");

    /* And for another slot. */
    CHECK_EQ(XhciXferEvent(&fix.queue, &fix.ring, FIX_SLOT, FIX_DCI,
                           XhciRingTrbPA(&fix.ring, 2),
                           event_dw2(XHCI_CC_SUCCESS, 0),
                           event_dw3(FIX_SLOT + 1, FIX_DCI), &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(fix.queue.ForeignEvents, 2, "wrong slot");

    /* A TRB pointer of zero: "Several transfer related errors may be detected
     * that cannot be attributed to a specific TRB ... the xHC shall set the TRB
     * Pointer to 0 and software shall treat it as invalid" (4.11.3.1). */
    CHECK_EQ(XhciXferEvent(&fix.queue, &fix.ring, FIX_SLOT, FIX_DCI, 0,
                           event_dw2(XHCI_CC_USB_TRANSACTION_ERROR, 0),
                           event_dw3(FIX_SLOT, FIX_DCI), &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "not attributed to a TRB");
    CHECK_EQ(fix.queue.ForeignEvents, 3, "counted");
    CHECK_EQ(fix.queue.Count, 1, "and no transfer was failed on it");

    /* An address on another ring. */
    CHECK_EQ(XhciXferEvent(&fix.queue, &fix.ring, FIX_SLOT, FIX_DCI,
                           RING_PA + 0x40000UL,
                           event_dw2(XHCI_CC_SUCCESS, 0),
                           event_dw3(FIX_SLOT, FIX_DCI), &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(fix.queue.ForeignEvents, 4, "off this ring");

    /* A code no Transfer Event on this ring may carry. Nothing here knows what
     * the controller did with the TRBs, so nothing is retired or completed. */
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_COMMAND_ABORTED, 0, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "not acted on");
    CHECK_EQ(fix.queue.BadCodes, 1, "counted, which is the visible failure");
    CHECK_EQ(fix.queue.Count, 1, "and the transfer stays outstanding");

    /* An event naming a TRB on this ring that no transfer owns. */
    CHECK_EQ(deliver(&fix, 10, XHCI_CC_SUCCESS, 0, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "not ours");
    CHECK_EQ(fix.queue.UnmatchedEvents, 1, "counted");

    CHECK_EQ(XhciXferEvent(NULL, &fix.ring, FIX_SLOT, FIX_DCI, 0, 0, 0, &result),
             XHCI_XFER_BAD_PARAM, "NULL queue");
    CHECK_EQ(XhciXferEvent(&fix.queue, NULL, FIX_SLOT, FIX_DCI, 0, 0, 0, &result),
             XHCI_XFER_BAD_PARAM, "NULL ring");
    CHECK_EQ(XhciXferEvent(&fix.queue, &fix.ring, FIX_SLOT, FIX_DCI, 0, 0, 0,
                           NULL),
             XHCI_XFER_BAD_PARAM, "NULL result");

    /* Through all of that, the transfer is still there and still intact. */
    CHECK_EQ(fix.queue.Count, 1, "nothing was completed by a rejected event");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 0, "no length was invented");
    CHECK_EQ(fix.transfers[0].Flags, 0, "and nothing was latched");
}

/* Event Lost is a controller-level failure, not this transfer's error. */
static void test_event_lost_is_fatal(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");

    CHECK_EQ(deliver(&fix, 2, XHCI_CC_EVENT_LOST, 0, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Fatal, 1, "escalated");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE,
             "and the transfer is still completed rather than left pending");
    CHECK_EQ((ULONG)result.Completed->UsbdStatus, WANT_USBD_INTERNAL_HC_ERROR,
             "as a controller error");
}

/* The teardown path: everything queued comes back with one status and the ring
 * is not touched, because by then the controller may not be running. */
static void test_queue_drain(void)
{
    XFER_FIXTURE fix;
    PXHCI_TRANSFER list;
    ULONG count;
    ULONG enqueueBefore;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "first");
    CHECK_EQ(fixture_submit_in(&fix, 1, 18), XHCI_XFER_OK, "second");
    enqueueBefore = fix.ring.Enqueue;

    list = XhciXferQueueDrain(&fix.queue, (LONG)WANT_USBD_CANCELED, &count);
    CHECK_EQ(count, 2, "both came back");
    CHECK_EQ(list, &fix.transfers[0], "oldest first");
    CHECK_EQ(list->Next, &fix.transfers[1], "then the second");
    CHECK_EQ(list->Next->Next, NULL, "list terminated");
    CHECK_EQ((ULONG)list->UsbdStatus, WANT_USBD_CANCELED, "with the status given");
    CHECK_EQ(fix.queue.Count, 0, "queue empty");
    CHECK_EQ(fix.queue.Head, NULL, "head cleared");
    CHECK_EQ(fix.queue.Tail, NULL, "tail cleared");
    CHECK_EQ(fix.ring.Enqueue, enqueueBefore, "the ring was not touched");
    CHECK_EQ(fix.ring.Dequeue, 0, "not even its dequeue pointer");

    list = XhciXferQueueDrain(&fix.queue, (LONG)WANT_USBD_CANCELED, &count);
    CHECK_EQ(count, 0, "draining an empty queue is not an error");
    CHECK(list == NULL, "and returns nothing");
}

/*
 * Which TRBs on a stopped ring still belong to somebody - batch 7a-B.2's only
 * question, and the reason there is no stored list of cancelled ranges beside
 * the queue.
 */
static void test_queue_owns_index(void)
{
    XFER_FIXTURE fix;
    ULONG first;
    ULONG last;
    ULONG i;
    ULONG owned;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "first");
    CHECK_EQ(fixture_submit_in(&fix, 1, 18), XHCI_XFER_OK, "second");

    first = fix.transfers[0].FirstIndex;
    last = fix.transfers[1].LastIndex;
    CHECK_EQ(first, 0, "(the first transfer starts at index 0)");

    for (i = first; i <= last; i++) {
        CHECK_EQ(XhciXferQueueOwnsIndex(&fix.queue, &fix.ring, i), 1,
                 "every TRB of both transfers is owned");
    }
    CHECK_EQ(XhciXferQueueOwnsIndex(&fix.queue, &fix.ring, last + 1), 0,
             "and the slot past the last is not");

    /* Cancel the first. Its TRBs stay on the ring and stop being owned, which is
     * exactly what tells the placement which ones to rewrite as No Ops. */
    CHECK_EQ(XhciXferQueueRemove(&fix.queue, &fix.transfers[0]), 1,
             "the first transfer is taken off");
    owned = 0;
    for (i = first; i <= last; i++) {
        owned += XhciXferQueueOwnsIndex(&fix.queue, &fix.ring, i);
    }
    CHECK_EQ(owned, fix.transfers[1].TrbCount,
             "only the survivor's TRBs are still owned");
    CHECK_EQ(XhciXferQueueOwnsIndex(&fix.queue, &fix.ring,
                                    fix.transfers[0].FirstIndex),
             0, "the cancelled transfer's head is a leftover");
    CHECK_EQ(XhciXferQueueOwnsIndex(&fix.queue, &fix.ring,
                                    fix.transfers[1].FirstIndex),
             1, "and the survivor's head is not");

    CHECK_EQ(XhciXferQueueOwnsIndex(NULL, &fix.ring, 0), 0, "null queue");
    CHECK_EQ(XhciXferQueueOwnsIndex(&fix.queue, NULL, 0), 0, "null ring");
}

/*
 * What a forced Stopped Transfer Event measures, latched onto its transfer
 * without completing or retiring anything - the only reading a cancelled
 * transfer ever gets, because a stopped TD produces no completion of its own.
 *
 * The three Stopped codes carry three different things, and getting that wrong
 * is a byte count reported to usbport that nothing measured.
 */
static void test_queue_stopped_latches_length(void)
{
    XFER_FIXTURE fix;
    ULONG pa;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "a transfer");
    pa = XhciRingTrbPA(&fix.ring, fix.transfers[0].DataFirstIndex);

    /* Code 26 carries a residual, so the length is the spec's own arithmetic:
     * the sum of the TRB lengths up to the one named, minus the residual. */
    CHECK_EQ(XhciXferQueueStopped(&fix.queue, &fix.ring, pa,
                                  (WANT_CC_STOPPED << 24) | 5UL),
             1, "a Stopped event latches");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 13, "18 asked for, 5 residual");
    CHECK_EQ(fix.transfers[0].Flags & 1UL, 0UL,
             "and does *not* fix the length - the transfer may still be resumed "
             "by a doorbell, and its real completion has to report the total");
    CHECK_EQ(fix.queue.Count, 1, "with the transfer still queued");
    CHECK_EQ(fix.ring.Dequeue, 0, "and the ring untouched");

    /*
     * Code 27 says the *field* is invalid, not the length: "software shall
     * ignore the TRB Transfer Length field of the Transfer Event, and simply sum
     * of the TRB Transfer Length fields of all Transfer TRBs in the TD executed
     * **prior to** the TRB referenced" (4.6.9 p.122). Named against the TD's
     * first data TRB, that sum is empty - nothing was transferred - and the
     * event's own 5 is ignored.
     */
    fix.transfers[0].BytesTransferred = 0;
    CHECK_EQ(XhciXferQueueStopped(&fix.queue, &fix.ring, pa,
                                  (WANT_CC_STOPPED_LENGTH_INVALID << 24) | 5UL),
             1, "Stopped - Length Invalid derives its own length");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 0,
             "which is zero on the TD's first TRB, and not the 5 it reported");

    /*
     * Code 28 carries the **EDTLA**, which is a running total of the bytes the
     * TD has moved rather than a residual - "the xHC maintains an internal
     * 24-bit Event Data Transfer Length Accumulator (EDTLA) for each endpoint"
     * (4.11.5.2 p.209), maintained whether or not software ever posts an Event
     * Data TRB. So it is taken directly: 5 means five bytes moved, where the
     * residual reading would have said thirteen.
     */
    CHECK_EQ(XhciXferQueueStopped(&fix.queue, &fix.ring, pa,
                                  (WANT_CC_STOPPED_SHORT_PACKET << 24) | 5UL),
             1, "Stopped - Short Packet latches its EDTLA");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 5,
             "as a total, not as a residual");

    /* An EDTLA larger than the buffer is not a length this driver may report. */
    fix.transfers[0].BytesTransferred = 0;
    CHECK_EQ(XhciXferQueueStopped(&fix.queue, &fix.ring, pa,
                                  (WANT_CC_STOPPED_SHORT_PACKET << 24) | 99UL),
             0, "an EDTLA past the buffer is refused");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 0, "with nothing reported");

    /* Nothing at all for an event that names no queued transfer, and for a code
     * that is not one of the three. */
    CHECK_EQ(XhciXferQueueStopped(&fix.queue, &fix.ring,
                                  XhciRingTrbPA(&fix.ring, 20),
                                  (WANT_CC_STOPPED << 24) | 1UL),
             0, "an event for no queued transfer latches nothing");
    CHECK_EQ(XhciXferQueueStopped(&fix.queue, &fix.ring, pa, 1UL << 24),
             0, "and neither does a Success");
    CHECK_EQ(XhciXferQueueStopped(NULL, &fix.ring, pa, 0), 0, "null queue");
    CHECK_EQ(XhciXferQueueStopped(&fix.queue, NULL, pa, 0), 0, "null ring");

    /*
     * Code 27 on a **multi-TRB TD**, which is the only shape where its sum is
     * not zero and therefore the only one that distinguishes "prior to the
     * referenced TRB" from "up to and including it".
     */
    {
        XFER_FIXTURE multi;

        fixture_init(&multi, 32);
        sg_init(&multi.sg);
        sg_add(&multi.sg, 0x0A300000UL, 128, 0);
        sg_add(&multi.sg, 0x0A400000UL, 128, 128);
        sg_add(&multi.sg, 0x0A500000UL, 64, 256);
        request_init(&multi.req, 0x80, 0x06, 0, 0, 320, 320, 64, &multi.sg);
        CHECK_EQ(XhciXferSubmitControl(&multi.queue, &multi.ring, &multi.req,
                                       &multi.transfers[0], (PVOID)0x2000UL,
                                       multi.scratch,
                                       XHCI_XFER_MAX_CONTROL_TRBS),
                 XHCI_XFER_OK, "(a three-TRB data stage)");

        /* Stopped on the third data TRB: the two before it moved 128 + 128. */
        CHECK_EQ(XhciXferQueueStopped(&multi.queue, &multi.ring,
                                      XhciRingTrbPA(&multi.ring, 3),
                                      (WANT_CC_STOPPED_LENGTH_INVALID << 24) |
                                          77UL),
                 1, "a length is derived");
        CHECK_EQ(multi.transfers[0].BytesTransferred, 256,
                 "128 + 128 - the TRBs prior to the one named, and not the 320 "
                 "that including it would give");

        /*
         * And named against a TRB of the transfer that is **not** in its data
         * range - the Setup Stage at index 0, or the Status Stage at 4. Neither
         * carries a length the sum may include, so the walk finds no
         * predecessor for it and nothing is latched.
         */
        multi.transfers[0].Flags &= ~1UL;        /* clear LENGTH_FIXED */
        multi.transfers[0].BytesTransferred = 0;
        CHECK_EQ(XhciXferQueueStopped(&multi.queue, &multi.ring,
                                      XhciRingTrbPA(&multi.ring, 0),
                                      (WANT_CC_STOPPED_LENGTH_INVALID << 24)),
                 0, "the Setup Stage TRB derives nothing");
        CHECK_EQ(XhciXferQueueStopped(&multi.queue, &multi.ring,
                                      XhciRingTrbPA(&multi.ring, 4),
                                      (WANT_CC_STOPPED_LENGTH_INVALID << 24)),
                 0, "and neither does the Status Stage TRB");
        CHECK_EQ(multi.transfers[0].BytesTransferred, 0, "with no length set");
    }
}

/*
 * A transfer whose group spans the wrap-back Link TRB. Every index the engine
 * keeps - the range walk, the data-stage position, the length sum - has to step
 * the way the ring does, or it lands on the Link TRB and reads a segment
 * pointer as a transfer length.
 */
static void test_event_across_the_link(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    XHCI_TRB one;
    ULONG pa;
    ULONG i;

    fixture_init(&fix, 8);
    /* Park the enqueue pointer at index 5, two slots before the Link TRB. */
    XhciTrbClear(&one);
    one.Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL) | XHCI_TRB_IOC;
    for (i = 0; i < 5; i++) {
        XHCI_TD_COMPLETION completion;

        CHECK_EQ(XhciRingEnqueue(&fix.ring, &one, &pa), XHCI_RING_OK, "filler");
        CHECK_EQ(XhciRingClassifyEvent(&fix.ring, pa, XHCI_CC_SUCCESS,
                                       &completion),
                 XHCI_RING_OK, "classified");
        CHECK_EQ(XhciRingRetireTd(&fix.ring, &completion), XHCI_RING_OK,
                 "retired");
    }
    CHECK_EQ(fix.ring.Enqueue, 5, "parked");

    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");
    CHECK_EQ(fix.transfers[0].FirstIndex, 5, "setup TRB before the link");
    CHECK_EQ(fix.transfers[0].DataFirstIndex, 6, "data TRB before the link");
    CHECK_EQ(fix.transfers[0].LastIndex, 0,
             "status TRB after the wrap - the Link TRB was stepped over");
    CHECK_EQ(fix.mem[7].Control & XHCI_TRB_CH, 0,
             "the crossing falls between the Data and Status TDs");

    /* An event on the data TRB, whose length sum crosses nothing yet. */
    CHECK_EQ(deliver(&fix, 6, XHCI_CC_SHORT_PACKET, 6, &result),
             XHCI_XFER_OK, "ok");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 12, "18 - 6");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "deferred");

    /* And the completing event, on the far side of the wrap. */
    CHECK_EQ(deliver(&fix, 0, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completed");
    CHECK_EQ(result.Completed->BytesTransferred, 12, "length survived the wrap");
    CHECK_EQ(XhciRingFree(&fix.ring), XhciRingCapacity(&fix.ring), "retired");
}

/*
 * Token 0 is never issued, so a zeroed transfer record matches no outstanding
 * transfer. usbport `RtlZeroMemory`s the extensions it owns, which is what
 * makes that worth a guard rather than an accident of counting from 1.
 */
static void test_token_never_zero(void)
{
    XFER_FIXTURE fix;

    fixture_init(&fix, 32);
    fix.queue.NextToken = 0xFFFFFFFFUL;
    CHECK_EQ(fixture_submit_in(&fix, 0, 0), XHCI_XFER_OK, "submitted");
    CHECK_EQ(fix.transfers[0].Token, 0xFFFFFFFFUL, "the last token before wrap");
    CHECK_EQ(fix.queue.NextToken, 1, "and the counter skips 0 on the wrap");
}

/*
 * The state this engine has no path to: a transfer ends, its TRBs are not
 * reclaimed, and the ring did not ask for recovery either. Every anticipated
 * ending is one or the other, so reaching it means this driver's model of the
 * hardware is incomplete - which is why it is counted apart from an ordinary
 * halt.
 *
 * Produced here by moving the ring out from under the transfer record, which is
 * what a divergence between the two would look like from this side.
 *
 * **What it does about it changed in the batch-9-0 review round 6.** This used
 * to complete the transfer and ask for recovery - and the completion code here
 * is `Success`, which halts nothing, so the slot layer's generic recovery
 * halted a **Running** endpoint and the completion handed a mapped buffer back
 * while that endpoint may still have owned the TRBs naming it. The unretired
 * ending is now reported as `RefusedRetire`: the transfer stays queued, nothing
 * is placed, and the caller owes a Stop Endpoint with a drain continuation
 * before either becomes legal. Only an error or a cancellation - which do leave
 * the endpoint Halted or Stopped - still completes here.
 */
static void test_orphaned_group(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");
    CHECK_EQ(XhciRingSetDequeue(&fix.ring, XhciRingTrbPA(&fix.ring,
                                                         fix.ring.Enqueue)),
             XHCI_RING_OK, "the ring is emptied behind the record's back");

    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "the transfer does NOT end here - a Success halts nothing, so the "
             "endpoint is still running and still owns these TRBs");
    CHECK_EQ(result.CompletedCount, 0, "and nothing is handed back");
    CHECK_EQ(fix.queue.Count, 1, "it stays queued");
    CHECK_EQ(fix.queue.OrphanedGroups, 1, "counted as unexplained");
    CHECK_EQ(fix.queue.Recoveries, 0,
             "and not as a halt, which has the opposite diagnosis");
    CHECK_EQ(result.RefusedRetire, 1,
             "the safe reading: stop the endpoint first, then drain it");
    CHECK_EQ(result.NeedsRecovery, 1, "with recovery owed");
    CHECK_EQ(fix.queue.PlacementFailures, 0,
             "and nothing is placed on a ring the endpoint may still be reading");

    /*
     * The same with a transfer queued behind it. Nothing is completed and
     * nothing is placed there either - the placement only belongs to a group
     * that actually ended, and this one has not.
     */
    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "first");
    CHECK_EQ(fixture_submit_in(&fix, 1, 18), XHCI_XFER_OK, "second");
    CHECK_EQ(XhciRingSetDequeue(&fix.ring, XhciRingTrbPA(&fix.ring,
                                                         fix.ring.Enqueue)),
             XHCI_RING_OK, "ring emptied");
    CHECK_EQ(deliver(&fix, 2, XHCI_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "ok");
    CHECK_EQ(result.CompletedCount, 0, "neither transfer completes");
    CHECK_EQ(fix.queue.Count, 2, "both stay queued");
    CHECK_EQ(fix.queue.PlacementFailures, 0,
             "and no position is forced onto a ring that cannot take one");
}

/*
 * **The orphan backstop, which only an error can reach now.**
 *
 * `xhciXferFinishGroup` still counts "the group ended and nothing was
 * reclaimed" and forces `NeedsRecovery`. Since the batch-9-0 review round 6,
 * every *non-halting* way of reaching that is intercepted earlier and leaves
 * the transfer queued instead - so the backstop is reachable only by an error
 * or a cancellation, where the endpoint really is Halted or Stopped and
 * completing is legal.
 *
 * Round 7 found that no vector reached it any more: `test_orphaned_group` used
 * to, and now returns at the interception. Without this one, deleting the
 * backstop would leave an endpoint halted by hardware with the slot layer never
 * asked to recover it, and the suite would stay green.
 */
static void test_orphan_backstop_on_error(void)
{
    XFER_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    fixture_init(&fix, 32);
    CHECK_EQ(fixture_submit_in(&fix, 0, 18), XHCI_XFER_OK, "submitted");
    CHECK_EQ(XhciRingSetDequeue(&fix.ring, XhciRingTrbPA(&fix.ring,
                                                         fix.ring.Enqueue)),
             XHCI_RING_OK, "the ring is emptied behind the record's back");

    /* A halting error rather than a Success: the endpoint really is Halted, so
     * the interception deliberately does not apply and the transfer must be
     * completed and recovery demanded. */
    CHECK_EQ(deliver(&fix, 1, XHCI_CC_STALL, 0, &result), XHCI_XFER_OK,
             "a stall mid-TD, on a ring that no longer holds the TD");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE,
             "the transfer DOES end here - a stall halts the endpoint, so it is "
             "not executing and its buffer is safe to hand back");
    CHECK_EQ(result.RefusedRetire, 0,
             "and it is not a refused retire: the endpoint needs the halt "
             "recovery, not a Stop Endpoint");
    CHECK_EQ(result.NeedsRecovery, 1, "recovery is demanded");
    CHECK_EQ(fix.queue.OrphanedGroups, 1,
             "and the backstop counted the unanticipated state - this is the "
             "assertion that keeps that branch alive");
    CHECK_EQ(fix.queue.Count, 0, "the queue is empty");
}

int main(void)
{
    test_completion_code_mapping();
    test_build_no_data();
    test_build_in_data();
    test_build_out_data();
    test_build_td_size();
    test_build_td_size_saturates();
    test_build_64k_split();
    test_build_sg_order();
    test_build_refusals();
    test_submit();
    test_submit_ring_full();
    test_build_normal_single();
    test_build_normal_out_has_no_isp();
    test_build_normal_zero_length();
    test_build_normal_multi_trb();
    test_build_normal_refusals();
    test_submit_normal();
    test_event_normal_short_packet_terminates();
    test_event_mid_td_tail_counted_once();
    test_event_mid_td_tail_eviction_is_counted();
    test_event_mid_td_tail_reuses_free_slots();
    test_event_mid_td_tail_retracted_on_reuse();
    test_event_delayed_tail_after_reuse_is_misattributed();
    test_event_normal_short_packet_tail_withheld();
    test_event_half_data_range_refuses_early_retire();
    test_event_mid_td_deferral_swept_by_a_later_td();
    test_event_mid_td_deferral_lost_on_unlink();
    test_event_tail_after_the_settle_is_still_misattributed();
    test_event_normal_short_ring_record_disagree();
    test_event_short_on_last_trb_ring_disagrees();
    test_settle_refuses_when_the_ring_moved();
    test_event_normal_first_measurement_wins();
    test_event_normal_is_not_intermediate();
    test_submit_normal_ring_full();
    test_build_normal_at_the_trb_cap();
    test_submit_normal_across_the_link();
    test_event_success();
    test_event_zero_length();
    test_event_short_packet();
    test_event_short_packet_multi_trb();
    test_event_spurious_success();
    test_event_residual_rejected();
    test_event_stall();
    test_event_error_on_status();
    test_event_canceled();
    test_event_sweeps_earlier_transfers();
    test_event_two_transfers_in_order();
    test_event_rejections();
    test_event_lost_is_fatal();
    test_queue_drain();
    test_queue_owns_index();
    test_queue_stopped_latches_length();
    test_event_across_the_link();
    test_token_never_zero();
    test_orphaned_group();
    test_orphan_backstop_on_error();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
