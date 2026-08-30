/*
 * test_iso.c - host tests for the isochronous engine (src/xhci_xfer.c), task
 * 9-A.1.
 *
 * The roadmap calls batch 9-A "unusually vector-heavy for its size", and names
 * the reason: **frame-number and MFINDEX wraps are the defect class here, and
 * they are cheap to construct on the host and expensive to reproduce on a
 * device.** A Frame ID that is one frame outside the Valid Frame Window makes an
 * audio stream stutter on real hardware and nothing else; a Frame ID computed by
 * comparing two numbers either side of the 2,048-frame wrap is wrong for half
 * the second and right for the other half. Neither is visible in a guest.
 *
 * Three families are checked here that neither a VM run nor a review can settle:
 *
 *   - **The bytes of an Isoch TRB.** TBC and TLBPC are software's to compute
 *     from a formula in the spec (4.11.2.3 p.197), they sit at DW3 `8:7` and
 *     `19:16` rather than anywhere a Normal TRB has fields, and nothing reports
 *     them back. TBC also appears at a *second* offset when Extended TBC is
 *     enabled, so writing the wrong one is a plausible mistake with no symptom
 *     but audio that does not play.
 *   - **The Valid Frame Window, across the wrap.** Every comparison in it has to
 *     be a distance mod 2048 rather than a magnitude, and the vectors below
 *     construct the wrap in both directions with the bounds sitting exactly on
 *     it.
 *   - **Per-packet completion.** usbport wants a length and a status for every
 *     packet, written into its own block, and the map from "the TRB this event
 *     names" to "which packet that was" is the whole of it. Off by one and an
 *     audio frame's status lands on its neighbour.
 *
 * Per docs/contributing/design/03-host-unit-tests.md, expected values are transcribed by
 * hand from docs/usb-xhci-info/xhci-data-structures.md and from the spec pages cited beside
 * them, never produced by the code under test. The TRB words below are literal
 * hexadecimal for that reason.
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
        printf("FAIL %s:%d: %s\n", "test_iso.c", line, what);
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
               "test_iso.c", line, what, got, got, want, want);
    }
}

#define RING_PA 0x0F002000UL

/*
 * Hand-typed a second time rather than taken from the header under test - the
 * same rule test_xfer.c follows for the same reason.
 *
 * The two isochronous statuses come from C:\NTDDK\inc\usbdi.h, and
 * `DATA_UNDERRUN` is the one usbport's own completion path special-cases: it
 * stores the value, logs it, replaces it with 0 and leaves `URB->ErrorCount`
 * alone (docs/usb-xhci-info/usbport-miniport-abi.md, "Isochronous transfers"). That is why a
 * short isochronous read reports it rather than plain Success.
 */
#define WANT_USBD_SUCCESS            0x00000000UL
#define WANT_USBD_DATA_UNDERRUN      0xC0000009UL
#define WANT_USBD_BUFFER_OVERRUN     0xC000000CUL
#define WANT_USBD_NOT_ACCESSED       0xC000000FUL
#define WANT_USBD_INTERNAL_HC_ERROR  0x80000800UL
#define WANT_USBD_CANCELED           0x00010000UL

/* Completion codes, transcribed from Table 6-90. */
#define WANT_CC_SUCCESS             1UL
#define WANT_CC_TRB_ERROR           5UL
#define WANT_CC_STALL               6UL
#define WANT_CC_INVALID_STREAM_ID  10UL
#define WANT_CC_SHORT_PACKET       13UL
#define WANT_CC_RING_UNDERRUN      14UL
#define WANT_CC_RING_OVERRUN       15UL
#define WANT_CC_BANDWIDTH_OVERRUN  18UL
#define WANT_CC_MISSED_SERVICE     23UL
#define WANT_CC_STOPPED            26UL
#define WANT_CC_ISOCH_BUFFER_OVER  31UL

/* The Isoch TRB's own field positions, from docs/usb-xhci-info/xhci-data-structures.md
 * section 7 - written out here so a vector is checking a transcription rather
 * than the macro it is meant to catch a change in. */
#define ISO_TRB_TYPE        (5UL << 10)
#define NORMAL_TRB_TYPE     (1UL << 10)
#define ISO_ISP             0x00000004UL
#define ISO_CH              0x00000010UL
#define ISO_IOC             0x00000020UL
#define ISO_SIA             0x80000000UL
#define ISO_TBC(n)          (((n) & 3UL) << 7)
#define ISO_TLBPC(n)        (((n) & 0xFUL) << 16)
#define ISO_FRAME_ID(n)     (((n) & 0x7FFUL) << 20)

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/*
 * usbport's block is variable-length; this is the same shape with room for one
 * more packet than the declared cap, so the "too many packets" refusal can be
 * driven with a block that really is that long.
 */
#define ISO_BUF_PACKETS (XHCI_XFER_MAX_ISO_PACKETS + 2UL)

typedef struct _ISO_BUFFER {
    USBPORT_ISO_TRANSFER Block;
    USBPORT_ISO_PACKET More[ISO_BUF_PACKETS];
} ISO_BUFFER;

static void iso_init(ISO_BUFFER *iso)
{
    ULONG i;
    ULONG j;
    ULONG *raw;

    /* Zero-filled, exactly as usbport's allocator leaves it - which is also why
     * `XhciXferIsoFinalise` exists: zero is USBD_STATUS_SUCCESS. */
    raw = (ULONG *)iso;
    for (i = 0; i < sizeof(ISO_BUFFER) / sizeof(ULONG); i++) {
        raw[i] = 0;
    }
    iso->Block.Signature = USBPORT_ISO_SIGNATURE;
    iso->Block.NumberOfPackets = 0;
    for (j = 0; j < ISO_BUF_PACKETS + 1; j++) {
        iso->Block.Packet[j].FragmentCount = 1;
    }
}

/* One packet, one fragment. `frame` is the stamp usbport puts on it, in the
 * driver's published 32-bit frame domain. */
static void iso_add(ISO_BUFFER *iso, ULONG pa, ULONG length, ULONG frame)
{
    USBPORT_ISO_PACKET *p;

    p = &iso->Block.Packet[iso->Block.NumberOfPackets];
    p->Length = length;
    p->FrameNumber = frame;
    p->MicroFrame = 0;
    p->FragmentCount = 1;
    p->Fragment0Length = length;
    p->Fragment0AddressLo = pa;
    iso->Block.NumberOfPackets++;
}

/* One packet split the way usbport splits a buffer that crosses a page
 * boundary: `0x1000 - (PA & 0xFFF)` in the first fragment and the rest in the
 * second (docs/usb-xhci-info/usbport-miniport-abi.md). */
static void iso_add_split(ISO_BUFFER *iso, ULONG pa, ULONG first,
                          ULONG second, ULONG frame)
{
    USBPORT_ISO_PACKET *p;

    p = &iso->Block.Packet[iso->Block.NumberOfPackets];
    p->Length = first + second;
    p->FrameNumber = frame;
    p->MicroFrame = 0;
    p->FragmentCount = 2;
    p->Fragment0Length = first;
    p->Fragment0AddressLo = pa;
    p->Fragment1Length = second;
    p->Fragment1AddressLo = pa + first;
    iso->Block.NumberOfPackets++;
}

static void iso_request(XHCI_ISO_REQUEST *req,
                        const ISO_BUFFER *iso,
                        ULONG directionIn,
                        ULONG maxPacketSize,
                        ULONG maxBurstSize)
{
    req->Iso = &iso->Block;
    req->DirectionIn = directionIn;
    req->MaxPacketSize = maxPacketSize;
    req->MaxBurstSize = maxBurstSize;
    req->MaxEsitPayload = maxPacketSize * (maxBurstSize + 1);
    /* One TD per frame - the Full-Speed cadence, which is what usbport's
     * `StartFrame + i` stamping produces and what every vector below uses unless
     * it says otherwise. `iso_request_hs` is the other one. */
    req->PacketsPerFrame = 1;
    req->Frames.Allowed = 0;
    req->Frames.CurrentFrame = 0;
    req->Frames.IstFrames = 0;
}

static ULONG event_dw2(ULONG completionCode, ULONG residual)
{
    return (residual & 0x00FFFFFFUL) | ((completionCode & 0xFFUL) << 24);
}

static ULONG event_dw3(ULONG slotId, ULONG dci)
{
    return (32UL << 10) | ((dci & 0x1FUL) << 16) | ((slotId & 0xFFUL) << 24);
}

/* ------------------------------------------------------------------ */
/* 1. The isochronous completion-code decoder                          */
/* ------------------------------------------------------------------ */

static void expect_iso_code(ULONG completionCode,
                            ULONG class_,
                            ULONG usbdStatus,
                            ULONG residualIsBytes,
                            const char *what)
{
    XHCI_XFER_CODE info;

    CHECK_EQ(XhciXferIsoCodeInfo(completionCode, &info), XHCI_XFER_OK, what);
    CHECK_EQ(info.Class, class_, what);
    CHECK_EQ((ULONG)info.UsbdStatus, usbdStatus, what);
    CHECK_EQ(info.ResidualIsBytes, residualIsBytes, what);
}

static void test_iso_code_info(void)
{
    XHCI_XFER_CODE info;

    /* Unchanged from the shared decoder - the isoch entry point delegates. */
    expect_iso_code(WANT_CC_SUCCESS, XHCI_XFER_CC_SUCCESS, WANT_USBD_SUCCESS, 1,
                    "Success means the same on an isoch ring");
    expect_iso_code(WANT_CC_STOPPED, XHCI_XFER_CC_CANCELED, WANT_USBD_CANCELED,
                    1, "a Stop Endpoint cancels here too");

    /*
     * **Reinterpreted.** On a bulk pipe a short read is plain Success and the
     * URB's own flags decide whether that is an error; on an isoch pipe it is a
     * per-packet fact usbport has a value for.
     */
    expect_iso_code(WANT_CC_SHORT_PACKET, XHCI_XFER_CC_SHORT,
                    WANT_USBD_DATA_UNDERRUN, 1,
                    "a short isoch packet is DATA_UNDERRUN, the value usbport "
                    "itself special-cases");
    CHECK_EQ(XhciXferCodeInfo(WANT_CC_SHORT_PACKET, &info), XHCI_XFER_OK,
             "(and the shared decoder still says Success)");
    CHECK_EQ((ULONG)info.UsbdStatus, WANT_USBD_SUCCESS,
             "which is the difference the second entry point exists for");

    /*
     * The three isoch-only codes, all of which the shared decoder refuses **on
     * purpose** - on a control, interrupt or bulk ring they are impossible and
     * treating one as meaningful would hide a controller fault.
     */
    expect_iso_code(WANT_CC_MISSED_SERVICE, XHCI_XFER_CC_ERROR,
                    WANT_USBD_NOT_ACCESSED, 0,
                    "a missed packet moved nothing - its length field is not a "
                    "byte count to report (4.10.3.2 p.187)");
    expect_iso_code(WANT_CC_ISOCH_BUFFER_OVER, XHCI_XFER_CC_ERROR,
                    WANT_USBD_BUFFER_OVERRUN, 1,
                    "an isoch buffer overrun reads as a buffer that overran");
    expect_iso_code(WANT_CC_BANDWIDTH_OVERRUN, XHCI_XFER_CC_ERROR,
                    WANT_USBD_INTERNAL_HC_ERROR, 1,
                    "a Bandwidth Overrun is this driver's own arithmetic coming "
                    "back, not something the device did");
    CHECK_EQ(XhciXferCodeInfo(WANT_CC_MISSED_SERVICE, &info),
             XHCI_XFER_BAD_PARAM,
             "and the shared decoder still refuses all three");
    CHECK_EQ(XhciXferCodeInfo(WANT_CC_ISOCH_BUFFER_OVER, &info),
             XHCI_XFER_BAD_PARAM, "(Isoch Buffer Overrun)");
    CHECK_EQ(XhciXferCodeInfo(WANT_CC_BANDWIDTH_OVERRUN, &info),
             XHCI_XFER_BAD_PARAM, "(Bandwidth Overrun)");

    /*
     * Ring Underrun and Ring Overrun name no TD at all - "the TRB referenced by
     * the Dequeue Pointer is not valid" (4.10.3.1 p.185) - so there is nothing
     * for a per-packet decode to be about, and they are refused here rather than
     * given a status nobody consults.
     */
    CHECK_EQ(XhciXferIsoCodeInfo(WANT_CC_RING_UNDERRUN, &info),
             XHCI_XFER_BAD_PARAM, "Ring Underrun has no packet to decode");
    CHECK_EQ(XhciXferIsoCodeInfo(WANT_CC_RING_OVERRUN, &info),
             XHCI_XFER_BAD_PARAM, "nor Ring Overrun");
    CHECK_EQ(XhciXferIsoCodeInfo(0, &info), XHCI_XFER_BAD_PARAM,
             "nor completion code 0, which 4.11.3.1 says to treat as invalid");
    CHECK_EQ(XhciXferIsoCodeInfo(WANT_CC_MISSED_SERVICE, NULL),
             XHCI_XFER_BAD_PARAM, "a NULL output is a caller bug");
}

/* ------------------------------------------------------------------ */
/* 2. The Valid Frame Window, including both wraps                     */
/* ------------------------------------------------------------------ */

static ULONG frame_ok(ULONG current, ULONG ist, ULONG frameNumber,
                      ULONG *frameId)
{
    XHCI_ISO_FRAME_POLICY policy;

    policy.Allowed = 1;
    policy.CurrentFrame = current;
    policy.IstFrames = ist;
    return XhciXferFrameIdUsable(&policy, frameNumber, frameId);
}

static void test_frame_window(void)
{
    XHCI_ISO_FRAME_POLICY policy;
    ULONG id;

    /*
     * The bounds, spec 4.11.2.5 p.199:
     *
     *   Start Frame ID = (current + IST + 1) MOD 2048   "should not" go before
     *   End   Frame ID = (current + 895)     MOD 2048   "shall not" go after
     *
     * With IST = 0 the earliest usable frame is `current + 1` - the current
     * frame itself is already being executed.
     */
    CHECK_EQ(frame_ok(100, 0, 100, &id), 0,
             "the frame the controller is in is not schedulable");
    CHECK_EQ(frame_ok(100, 0, 101, &id), 1, "the next one is");
    CHECK_EQ(id, 101UL, "and its Frame ID is the stamp mod 2048");
    CHECK_EQ(frame_ok(100, 0, 100 + 895, &id), 1, "so is the last of the 895");
    CHECK_EQ(frame_ok(100, 0, 100 + 896, &id), 0, "one past it is not");

    /* IST moves the *start* bound only, and it is in frames by the time it gets
     * here - HCSPARAMS2's microframe encoding is converted once, in
     * XhciDeriveHcInfo, so this function never sees the unit selector. */
    CHECK_EQ(frame_ok(100, 2, 102, &id), 0,
             "IST 2 pushes the start bound out to current + 3");
    CHECK_EQ(frame_ok(100, 2, 103, &id), 1, "and that one is usable");
    CHECK_EQ(frame_ok(100, 2, 100 + 895, &id), 1,
             "while the end bound does not move with IST");

    /*
     * **The wrap, which is what this whole function is shaped by.** At current
     * = 2040 the frames 2041..2047 and 0..7 are all in the near future, and half
     * of them have the *smaller* number - so a driver comparing magnitudes would
     * refuse every schedule that straddled the wrap and accept every one that
     * had already expired past it.
     */
    CHECK_EQ(frame_ok(2040, 0, 2041, &id), 1, "just before the wrap");
    CHECK_EQ(id, 2041UL, "(and keeps its value)");
    CHECK_EQ(frame_ok(2040, 0, 2048, &id), 1, "the stamp that wraps to 0");
    CHECK_EQ(id, 0UL, "reduces to Frame ID 0, which is 8 frames ahead");
    CHECK_EQ(frame_ok(2040, 0, 2055, &id), 1, "and one a little further");
    CHECK_EQ(id, 7UL, "wraps to 7");
    CHECK_EQ(frame_ok(2040, 0, 2040, &id), 0,
             "while the current frame is still refused across the wrap");
    CHECK_EQ(frame_ok(2040, 0, 2039, &id), 0,
             "and one frame in the *past* is refused rather than read as 2,047 "
             "frames in the future");
    CHECK_EQ(frame_ok(2040, 0, 2040 + 895, &id), 1,
             "the end bound holds across the wrap");
    CHECK_EQ(id, (2040UL + 895UL) & 0x7FFUL, "at its wrapped value");
    CHECK_EQ(frame_ok(2040, 0, 2040 + 896, &id), 0, "and one past it does not");

    /*
     * **A stale stamp must not alias into the future**, which is the batch 9-A
     * review's fourth MAJOR and the reason the whole comparison moved out of the
     * Frame ID's own 11 bits.
     *
     * At current frame 5,000 a stamp of 3,000 is two thousand frames in the
     * *past* - a request usbport built against a frame number this driver
     * published a lap ago. Reduced mod 2,048 first, the two are 48 apart and it
     * read as 48 frames ahead: an explicit Frame ID naming a frame that had
     * already gone, which is a Missed Service Error per TD until the pipe
     * resynchronizes. The vector that stood here asserted the alias as correct
     * behaviour.
     */
    CHECK_EQ(frame_ok(5000, 0, 3000, &id), 0,
             "a stamp a whole lap in the past is refused, not read as 48 frames "
             "ahead");
    CHECK_EQ(frame_ok(5000, 0, 5000 - 2048, &id), 0,
             "nor is exactly one lap behind mistaken for the current frame");
    CHECK_EQ(frame_ok(5000, 0, 5048, &id), 1,
             "while a stamp genuinely 48 frames ahead is still accepted");
    CHECK_EQ(id, 5048UL & 0x7FFUL, "at its wrapped Frame ID");

    /*
     * The other wrap, and now the only one this function does in 32 bits: the
     * published frame number itself crossing 2^32. The subtraction wraps with it,
     * so a stamp three frames past the top is three frames ahead.
     */
    CHECK_EQ(frame_ok(0xFFFFFFFEUL, 0, 1UL, &id), 1,
             "a stamp on the far side of the 32-bit wrap is still ahead");
    CHECK_EQ(id, 1UL, "and its Frame ID is the stamp mod 2048");
    CHECK_EQ(frame_ok(0xFFFFFFFEUL, 0, 0xFFFFFFFDUL, &id), 0,
             "while one frame behind it is refused across the same wrap");

    /* Refusals that are about the policy rather than the number. */
    policy.Allowed = 0;
    policy.CurrentFrame = 100;
    policy.IstFrames = 0;
    CHECK_EQ(XhciXferFrameIdUsable(&policy, 101, &id), 0,
             "a policy that is not Allowed refuses everything");
    policy.Allowed = 1;
    policy.CurrentFrame = 100;
    policy.IstFrames = 900;
    CHECK_EQ(XhciXferFrameIdUsable(&policy, 200, &id), 0,
             "an IST that swallows the whole window refuses rather than "
             "producing a start bound above the end bound");
    CHECK_EQ(XhciXferFrameIdUsable(NULL, 101, &id), 0, "NULL policy");
    CHECK_EQ(XhciXferFrameIdUsable(&policy, 101, NULL), 0, "NULL output");
}

/* ------------------------------------------------------------------ */
/* 3. Building the TRBs                                                */
/* ------------------------------------------------------------------ */

static void test_build_one_packet(void)
{
    ISO_BUFFER iso;
    XHCI_ISO_REQUEST req;
    XHCI_ISO_LAYOUT layout;
    XHCI_TRB out[8];

    iso_init(&iso);
    iso_add(&iso, 0x0B100000UL, 192, 500);
    iso_request(&req, &iso, 1, 1024, 0);

    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_OK,
             "a one-packet IN request builds");
    CHECK_EQ(layout.TrbCount, 1UL, "one fragment is one TRB");
    CHECK_EQ(layout.TdCount, 1UL, "and one packet is one TD");
    CHECK_EQ(layout.TdLengths[0], 1UL, "whose extent is that TRB");
    CHECK_EQ(layout.FrameIdsUsed, 0UL,
             "with SIA, because the policy was not Allowed");

    CHECK_EQ(out[0].Param0, 0x0B100000UL, "buffer pointer");
    CHECK_EQ(out[0].Param1, 0UL, "no 64-bit DMA, ever");
    CHECK_EQ(out[0].Status, 192UL,
             "length in DW2 16:0, TD Size 0 on the TD's last TRB");
    /*
     * DW3, assembled by hand from the field positions rather than from the
     * macros: Isoch type 5 at 15:10, ISP because the endpoint is IN, IOC because
     * every isoch TD must produce an event for its packet's status, CH clear
     * because this is the TD's last TRB, TBC 0 and TLBPC 0 for a single-packet
     * TD on a non-bursting endpoint, and SIA.
     */
    CHECK_EQ(out[0].Control,
             ISO_SIA | ISO_TLBPC(0) | ISO_TRB_TYPE | ISO_TBC(0) | ISO_IOC |
                 ISO_ISP,
             "isoch DW3: type 5, ISP, IOC, no chain, TBC/TLBPC 0, SIA");
    CHECK_EQ(out[0].Control & ISO_CH, 0UL,
             "the Chain bit is always 0 in the last TRB of an Isoch TD");

    /* An OUT endpoint carries no ISP - there is no short packet to report on a
     * transmit, and the field would be describing the wrong direction. */
    iso_init(&iso);
    iso_add(&iso, 0x0B100000UL, 192, 500);
    iso_request(&req, &iso, 0, 1024, 0);
    CHECK_EQ(XhciXferBuildIso(&req, 0, out, 8, &layout), XHCI_XFER_OK,
             "a one-packet OUT request builds");
    CHECK_EQ(out[0].Control, ISO_SIA | ISO_TRB_TYPE | ISO_IOC,
             "and carries no ISP");
}

static void test_build_zero_length_packet(void)
{
    ISO_BUFFER iso;
    XHCI_ISO_REQUEST req;
    XHCI_ISO_LAYOUT layout;
    XHCI_TRB out[8];

    /*
     * A silent packet in an audio stream. It is **one zero-length TRB and not
     * zero TRBs**: the xHC "shall transmit a zero-length DP to the USB bus
     * regardless bus speed, consuming the Isoch TD for the Service Interval"
     * (4.14.2.1 p.239), so a TD that does not exist is not a silent packet - it
     * is the ring running dry and a Ring Underrun.
     */
    iso_init(&iso);
    iso_add(&iso, 0x0B200000UL, 0, 500);
    iso_request(&req, &iso, 0, 1024, 0);

    CHECK_EQ(XhciXferBuildIso(&req, 0, out, 8, &layout), XHCI_XFER_OK,
             "a zero-length packet builds");
    CHECK_EQ(layout.TrbCount, 1UL, "as one TRB");
    CHECK_EQ(out[0].Status, 0UL, "with a zero length");
    /*
     * And its TDPC is **1**, not 0 - "note that a partial or a zero-length
     * packet increments this count by 1" (4.14.1 p.234). At TDPC 0 the TBC
     * expression is `ROUNDUP(0/1) - 1`, which underflows and masks into the
     * field as 3: a TD claiming four bursts of nothing.
     */
    CHECK_EQ((out[0].Control >> 7) & 3UL, 0UL,
             "TBC 0 - a zero-length packet still moves one packet");
    CHECK_EQ((out[0].Control >> 16) & 0xFUL, 0UL, "and TLBPC 0 beside it");
}

static void test_build_split_packet(void)
{
    ISO_BUFFER iso;
    XHCI_ISO_REQUEST req;
    XHCI_ISO_LAYOUT layout;
    XHCI_TRB out[8];

    /*
     * The two-fragment packet usbport produces when a buffer crosses a page
     * boundary. The TD is "an Isoch TRB chained to zero or more Normal TRBs"
     * (4.11.2.3 p.195) - so the second TRB is a **Normal** TRB and carries none
     * of the scheduling fields, which is the shape a second Isoch TRB would
     * silently get wrong.
     */
    iso_init(&iso);
    iso_add_split(&iso, 0x0B300000UL, 700, 324, 500);
    iso_request(&req, &iso, 1, 1024, 0);

    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_OK,
             "a split packet builds");
    CHECK_EQ(layout.TrbCount, 2UL, "as two TRBs");
    CHECK_EQ(layout.TdCount, 1UL, "in one TD");
    CHECK_EQ(layout.TdLengths[0], 2UL, "whose extent is both of them");

    CHECK_EQ(out[0].Param0, 0x0B300000UL, "first fragment address");
    /*
     * TD Size on the first TRB, spec 4.11.2.4: TD Packet Count is
     * ROUNDUP(1024/1024) = 1, Packets Transferred after 700 bytes is
     * FLOOR(700/1024) = 0, so TD Size is 1 - 0 = 1.
     */
    CHECK_EQ(out[0].Status, 700UL | (1UL << 17),
             "first TRB: 700 bytes with TD Size 1");
    CHECK_EQ(out[0].Control,
             ISO_SIA | ISO_TRB_TYPE | ISO_CH | ISO_ISP,
             "chained, and with no IOC - the event belongs to the TD's tail");

    CHECK_EQ(out[1].Param0, 0x0B300000UL + 700UL, "second fragment address");
    CHECK_EQ(out[1].Status, 324UL,
             "second TRB: the rest, TD Size cleared on a TD's last TRB");
    CHECK_EQ(out[1].Control, NORMAL_TRB_TYPE | ISO_IOC | ISO_ISP,
             "a Normal TRB, unchained, carrying the TD's IOC");
    CHECK_EQ(out[1].Control & ISO_SIA, 0UL,
             "and none of the Isoch TRB's scheduling fields");
}

static void test_build_burst_fields(void)
{
    ISO_BUFFER iso;
    XHCI_ISO_REQUEST req;
    XHCI_ISO_LAYOUT layout;
    XHCI_TRB out[8];

    /*
     * TBC and TLBPC on a high-bandwidth High-Speed endpoint - Max Burst 2, so
     * three packets per service opportunity and a Max ESIT Payload of 3,072.
     * Spec 4.11.2.3 p.197:
     *
     *   TDPC  = ROUNDUP(size / MPS)
     *   TBC   = ROUNDUP(TDPC / (MaxBurst + 1)) - 1
     *   TLBPC = residue == 0 ? MaxBurst : residue - 1
     *
     * Worked by hand below rather than by calling the code under test.
     */
    iso_init(&iso);
    iso_add(&iso, 0x0B400000UL, 3072, 500);       /* TDPC 3, TBC 0, TLBPC 2 */
    iso_add(&iso, 0x0B404000UL, 2048, 501);       /* TDPC 2, TBC 0, TLBPC 1 */
    iso_add(&iso, 0x0B408000UL, 1024, 502);       /* TDPC 1, TBC 0, TLBPC 0 */
    iso_add(&iso, 0x0B40C000UL, 1, 503);          /* TDPC 1, TBC 0, TLBPC 0 */
    iso_request(&req, &iso, 1, 1024, 2);

    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_OK,
             "four packets on a high-bandwidth endpoint build");
    CHECK_EQ(layout.TdCount, 4UL, "as four TDs");
    CHECK_EQ((out[0].Control >> 7) & 3UL, 0UL, "3072 bytes: TBC 0");
    CHECK_EQ((out[0].Control >> 16) & 0xFUL, 2UL, "and TLBPC 2 - a full burst");
    CHECK_EQ((out[1].Control >> 16) & 0xFUL, 1UL,
             "2048 bytes: TLBPC 1 - two packets in the last burst");
    CHECK_EQ((out[2].Control >> 16) & 0xFUL, 0UL, "1024 bytes: TLBPC 0");
    CHECK_EQ((out[3].Control >> 16) & 0xFUL, 0UL,
             "and a single-byte packet is still one packet");

    /*
     * **TBC is at DW3 8:7, not at DW2 21:17.** The second position is TD Size
     * while Extended TBC is disabled, which is this driver's only state - and
     * writing the burst count there instead would put a plausible number in a
     * field the controller reads as something else entirely.
     */
    CHECK_EQ((out[0].Status >> 17) & 0x1FUL, 0UL,
             "DW2 21:17 is TD Size and is 0 on a single-TRB TD, not the TBC");
}

static void test_build_multiple_packets(void)
{
    ISO_BUFFER iso;
    XHCI_ISO_REQUEST req;
    XHCI_ISO_LAYOUT layout;
    XHCI_TRB out[16];
    ULONG i;

    /*
     * **Eight packets are eight TDs**, and this is the property the whole design
     * rests on: "the xHC shall consume one Isoch TD each Interval on an Isoch
     * Transfer Ring" (4.11.2.3 p.196), so building one TD out of the request
     * would ask the controller to move a URB's worth of audio in one service
     * interval and then have nothing for the next.
     */
    iso_init(&iso);
    for (i = 0; i < 8; i++) {
        iso_add(&iso, 0x0B500000UL + i * 0x1000UL, 192, 500 + i);
    }
    iso_request(&req, &iso, 0, 1024, 0);

    CHECK_EQ(XhciXferBuildIso(&req, 0, out, 16, &layout), XHCI_XFER_OK,
             "eight packets build");
    CHECK_EQ(layout.TrbCount, 8UL, "as eight TRBs");
    CHECK_EQ(layout.TdCount, 8UL, "and eight TDs");
    for (i = 0; i < 8; i++) {
        CHECK_EQ(layout.TdLengths[i], 1UL, "each of one TRB");
        CHECK_EQ(out[i].Control & ISO_IOC, ISO_IOC,
                 "every TD carries IOC, because usbport wants a status per "
                 "packet and only an event can supply one");
        CHECK_EQ(out[i].Control & ISO_CH, 0UL, "and none of them is chained");
        CHECK_EQ(out[i].Control & (0x3FUL << 10), ISO_TRB_TYPE,
                 "every TD starts with an Isoch TRB");
    }
}

static void test_build_frame_ids(void)
{
    ISO_BUFFER iso;
    XHCI_ISO_REQUEST req;
    XHCI_ISO_LAYOUT layout;
    XHCI_TRB out[8];
    ULONG i;

    /* With the policy Allowed and every packet inside the window, the group
     * carries explicit Frame IDs and SIA is clear. */
    iso_init(&iso);
    for (i = 0; i < 4; i++) {
        iso_add(&iso, 0x0B600000UL + i * 0x1000UL, 192, 600 + i);
    }
    iso_request(&req, &iso, 0, 1024, 0);
    req.Frames.Allowed = 1;
    req.Frames.CurrentFrame = 500;
    req.Frames.IstFrames = 1;

    CHECK_EQ(XhciXferBuildIso(&req, 0, out, 8, &layout), XHCI_XFER_OK,
             "a group inside the window builds");
    CHECK_EQ(layout.FrameIdsUsed, 1UL, "with Frame IDs");
    for (i = 0; i < 4; i++) {
        CHECK_EQ(out[i].Control & ISO_SIA, 0UL, "SIA clear");
        CHECK_EQ((out[i].Control >> 20) & 0x7FFUL, 600UL + i,
                 "and each TD carries its own packet's frame");
    }

    /*
     * **One packet outside the window drops the whole group to SIA.** Mixing is
     * not a compromise: "To induce a gap in the data stream of a Running Isoch
     * endpoint, software simply specifies a gap in the Frame IDs assigned to the
     * TDs of the data stream, and the xHC will pause the data stream until the
     * Frame ID matches" (4.11.2.5 p.199). A group where some TDs carry an ID and
     * some do not is a request for a pause nobody asked for.
     */
    iso.Block.Packet[2].FrameNumber = 400;      /* already in the past */
    CHECK_EQ(XhciXferBuildIso(&req, 0, out, 8, &layout), XHCI_XFER_OK,
             "a group with one late packet still builds");
    CHECK_EQ(layout.FrameIdsUsed, 0UL, "but drops to SIA");
    for (i = 0; i < 4; i++) {
        CHECK_EQ(out[i].Control & ISO_SIA, ISO_SIA,
                 "on every TD, not only the late one");
        CHECK_EQ((out[i].Control >> 20) & 0x7FFUL, 0UL,
                 "and the Frame ID field is left at zero, which SIA makes the "
                 "xHC ignore");
    }

    /*
     * The Frame ID a stamp reduces to is the stamp mod 2048, so a group whose
     * frames straddle the wrap carries the wrapped values - and stays inside the
     * window, because the window is a distance.
     */
    iso_init(&iso);
    for (i = 0; i < 4; i++) {
        iso_add(&iso, 0x0B700000UL + i * 0x1000UL, 192, 2046 + i);
    }
    iso_request(&req, &iso, 0, 1024, 0);
    req.Frames.Allowed = 1;
    req.Frames.CurrentFrame = 2040;
    req.Frames.IstFrames = 0;
    CHECK_EQ(XhciXferBuildIso(&req, 0, out, 8, &layout), XHCI_XFER_OK,
             "a group straddling the Frame ID wrap builds");
    CHECK_EQ(layout.FrameIdsUsed, 1UL, "and keeps its Frame IDs");
    CHECK_EQ((out[0].Control >> 20) & 0x7FFUL, 2046UL, "2046");
    CHECK_EQ((out[1].Control >> 20) & 0x7FFUL, 2047UL, "2047");
    CHECK_EQ((out[2].Control >> 20) & 0x7FFUL, 0UL, "then 0 - the wrap");
    CHECK_EQ((out[3].Control >> 20) & 0x7FFUL, 1UL, "then 1");
}

static void test_build_refusals(void)
{
    ISO_BUFFER iso;
    XHCI_ISO_REQUEST req;
    XHCI_ISO_LAYOUT layout;
    XHCI_TRB out[XHCI_XFER_MAX_ISO_TRBS + 4];
    ULONG i;

    iso_init(&iso);
    iso_add(&iso, 0x0B800000UL, 192, 500);
    iso_request(&req, &iso, 1, 1024, 0);

    /* The signature is what says the pointer usbport handed over is the
     * structure this driver believes it is; every offset below rests on it. */
    iso.Block.Signature = 0x12345678UL;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_ISO_MALFORMED,
             "a block with the wrong signature is refused");
    iso.Block.Signature = USBPORT_ISO_SIGNATURE;

    iso.Block.NumberOfPackets = 0;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_ISO_MALFORMED,
             "a request with no packets is refused");
    iso.Block.NumberOfPackets = 1;

    /* Direction: usbport's flag against the endpoint's, checked and not chosen
     * between - the third statement of a rule the other two builders carry. */
    CHECK_EQ(XhciXferBuildIso(&req, 0, out, 8, &layout),
             XHCI_XFER_DIRECTION_CONFLICT,
             "an OUT flag on an IN endpoint is a malformed request");

    /* "1 or 2; the builder has no path that writes anything else." */
    iso.Block.Packet[0].FragmentCount = 0;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_ISO_MALFORMED,
             "a fragment count of zero is refused");
    iso.Block.Packet[0].FragmentCount = 3;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_ISO_MALFORMED,
             "and so is three");
    iso.Block.Packet[0].FragmentCount = 1;

    /* The fragments have to describe exactly this packet: a shortfall moves
     * fewer bytes than the client asked for while reporting the packet's length
     * back, and an excess reads past what usbport mapped. */
    iso.Block.Packet[0].Fragment0Length = 100;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_ISO_MALFORMED,
             "one fragment that does not cover the packet is refused");
    iso.Block.Packet[0].Fragment0Length = 192;

    iso.Block.Packet[0].FragmentCount = 2;
    iso.Block.Packet[0].Fragment0Length = 100;
    iso.Block.Packet[0].Fragment1Length = 50;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_ISO_MALFORMED,
             "two fragments that do not add up are refused");
    iso.Block.Packet[0].Fragment1Length = 92;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_OK,
             "and are accepted when they do");
    iso.Block.Packet[0].Fragment1Length = 0;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_ISO_MALFORMED,
             "a zero-length second fragment is a count of one written as two");
    iso_init(&iso);
    iso_add(&iso, 0x0B800000UL, 192, 500);

    /* usbport stores whatever the HAL returned and does not mask the high
     * DWORD - the same rule the scatter/gather walk carries. */
    iso.Block.Packet[0].Fragment0AddressHi = 1;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_ISO_MALFORMED,
             "a physical address above 4 GB is checked, never assumed away");
    iso.Block.Packet[0].Fragment0AddressHi = 0;

    /*
     * "Software shall not define a TD Transfer Size for a TD of an Isoch
     * endpoint that exceeds the Max ESIT Payload" (4.14.2.1 p.238). Refused
     * here, because the xHC's own answer is to truncate the transfer and raise a
     * Bandwidth Overrun - to lose part of an audio frame and say so afterwards.
     */
    iso.Block.Packet[0].Length = 1025;
    iso.Block.Packet[0].Fragment0Length = 1025;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_ISO_MALFORMED,
             "a packet above Max ESIT Payload is refused");
    iso.Block.Packet[0].Length = 1024;
    iso.Block.Packet[0].Fragment0Length = 1024;
    CHECK_EQ(XhciXferBuildIso(&req, 1, out, 8, &layout), XHCI_XFER_OK,
             "and one exactly at it is not");

    /*
     * **The packet cap is checked before the first packet is read**, and that
     * position is the point of it: the loops index `Packet[i]` in memory usbport
     * owns and sized, so a count this driver has not agreed to is a read past
     * the end of somebody else's allocation rather than a slow loop.
     */
    iso_init(&iso);
    for (i = 0; i < XHCI_XFER_MAX_ISO_PACKETS; i++) {
        iso_add(&iso, 0x0B900000UL + i * 0x1000UL, 192, 500 + i);
    }
    iso_request(&req, &iso, 0, 1024, 0);
    CHECK_EQ(XhciXferBuildIso(&req, 0, out, XHCI_XFER_MAX_ISO_TRBS, &layout),
             XHCI_XFER_OK, "a request exactly at the packet cap builds");
    CHECK_EQ(layout.TdCount, XHCI_XFER_MAX_ISO_PACKETS,
             "with every packet its own TD");
    iso_add(&iso, 0x0BA00000UL, 192, 900);
    CHECK_EQ(XhciXferBuildIso(&req, 0, out, XHCI_XFER_MAX_ISO_TRBS, &layout),
             XHCI_XFER_ISO_TOO_LARGE, "one more is refused as too large");
    CHECK_EQ(layout.TrbCount, 0UL,
             "and nothing was written - the refusal is not a partial build");

    /*
     * A scratch array smaller than the group needs reads as **too large**, not
     * as the internal `TOO_MANY_TRBS` the emitter raises. The scratch this
     * driver passes is exactly `XHCI_XFER_MAX_ISO_TRBS`, which is the ring's own
     * capacity, so filling it means the request can never be placed on an empty
     * ring either - the same fact as the packet-count bound above, arriving by
     * the other door. The batch 9-A review found it charged to
     * `IsoRefusalsMalformed`, which says something quite different on a target:
     * that this driver and usbport disagree about the block layout.
     */
    iso_init(&iso);
    for (i = 0; i < 8; i++) {
        iso_add(&iso, 0x0BB00000UL + i * 0x1000UL, 192, 500 + i);
    }
    iso_request(&req, &iso, 0, 1024, 0);
    CHECK_EQ(XhciXferBuildIso(&req, 0, out, 4, &layout),
             XHCI_XFER_ISO_TOO_LARGE, "a scratch array that is too small");

    CHECK_EQ(XhciXferBuildIso(NULL, 0, out, 8, &layout), XHCI_XFER_BAD_PARAM,
             "NULL request");
    req.Iso = NULL;
    CHECK_EQ(XhciXferBuildIso(&req, 0, out, 8, &layout), XHCI_XFER_ISO_MALFORMED,
             "a NULL block is checked even though usbport cannot produce one");
}

/* ------------------------------------------------------------------ */
/* 4. Submission and per-packet completion                             */
/* ------------------------------------------------------------------ */

#define ISO_FIX_SLOT 6
#define ISO_FIX_DCI  4

typedef struct _ISO_FIXTURE {
    XHCI_TRB mem[32];
    XHCI_RING ring;
    XHCI_TRANSFER_QUEUE queue;
    XHCI_TRB scratch[XHCI_XFER_MAX_ISO_TRBS];
    XHCI_ISO_LAYOUT layout;
    XHCI_TRANSFER transfers[4];
    ISO_BUFFER iso[4];
    XHCI_ISO_REQUEST req;
} ISO_FIXTURE;

static void iso_fixture_init(ISO_FIXTURE *fix, ULONG trbs)
{
    CHECK_EQ(XhciRingInit(&fix->ring, fix->mem, RING_PA, trbs,
                          XHCI_RING_KIND_ISOCH),
             XHCI_RING_OK, "fixture ring init");
    XhciXferQueueInit(&fix->queue);
}

/* `which` selects both the transfer record and the parameter block, so two
 * outstanding requests never share either - usbport allocates one of each per
 * transfer and never re-offers one the miniport still holds (batch 7a-0). */
static ULONG iso_fixture_submit(ISO_FIXTURE *fix, ULONG which, ULONG packets,
                                ULONG length)
{
    ULONG i;

    iso_init(&fix->iso[which]);
    for (i = 0; i < packets; i++) {
        iso_add(&fix->iso[which], 0x0C000000UL + (which << 20) + i * 0x1000UL,
                length, 700 + i);
    }
    iso_request(&fix->req, &fix->iso[which], 1, 1024, 0);
    return XhciXferSubmitIso(&fix->queue, &fix->ring, &fix->req, 1,
                             &fix->transfers[which],
                             (PVOID)(0x2000UL + which),
                             fix->scratch, XHCI_XFER_MAX_ISO_TRBS,
                             &fix->layout);
}

static ULONG iso_event(ISO_FIXTURE *fix, ULONG trbIndex, ULONG code,
                       ULONG residual, PXHCI_XFER_EVENT_RESULT result)
{
    return XhciXferIsoEvent(&fix->queue, &fix->ring, ISO_FIX_SLOT, ISO_FIX_DCI,
                            RING_PA + trbIndex * 16UL,
                            event_dw2(code, residual),
                            event_dw3(ISO_FIX_SLOT, ISO_FIX_DCI), result);
}

static void test_submit_and_complete(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    PUSBPORT_ISO_PACKET packet;

    iso_fixture_init(&fix, 32);
    CHECK_EQ(iso_fixture_submit(&fix, 0, 3, 192), XHCI_XFER_OK,
             "a three-packet request is placed");
    CHECK_EQ(fix.queue.Count, 1UL, "one transfer on the queue");
    CHECK_EQ(fix.queue.IsoPackets, 3UL, "counted by packets as well");
    CHECK_EQ(fix.transfers[0].IsoPacketCount, 3UL,
             "the record remembers how many packets it had");
    CHECK_EQ(fix.transfers[0].RequestedLength, 3UL * 192UL,
             "and their total length");
    CHECK_EQ(fix.transfers[0].Flags & XHCI_XFER_FLAG_ISOCH,
             XHCI_XFER_FLAG_ISOCH,
             "marked isochronous, which is what selects the completion service");
    CHECK(fix.transfers[0].IsoParams == &fix.iso[0].Block,
          "and holds the block, which is the fourth argument of that service");

    packet = fix.iso[0].Block.Packet;

    /* Packet 0 completes normally. Nothing is retired: the group is not over. */
    CHECK_EQ(iso_event(&fix, 0, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "packet 0's event is accepted");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "and completes nothing yet");
    CHECK_EQ(packet[0].LengthTransferred, 192UL,
             "but its length is written into usbport's block");
    CHECK_EQ((ULONG)packet[0].Status, WANT_USBD_SUCCESS, "with its status");
    CHECK_EQ(fix.queue.Count, 1UL, "the transfer is still queued");

    /* Packet 1 comes up short. The measured length is the packet's own, not a
     * sum across the group - a sum would fold packet 0's 192 bytes into it. */
    CHECK_EQ(iso_event(&fix, 1, WANT_CC_SHORT_PACKET, 50, &result),
             XHCI_XFER_OK, "packet 1's short event is accepted");
    CHECK_EQ(packet[1].LengthTransferred, 142UL,
             "192 - 50, measured against this packet alone");
    CHECK_EQ((ULONG)packet[1].Status, WANT_USBD_DATA_UNDERRUN,
             "and reported as the underrun usbport special-cases");
    CHECK_EQ(fix.queue.ShortPackets, 1UL, "counted as a short packet");

    /* Packet 2 is the group's last TRB, so this event ends the request. */
    CHECK_EQ(iso_event(&fix, 2, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "packet 2's event is accepted");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "and completes it");
    CHECK_EQ(result.CompletedCount, 1UL, "one transfer");
    CHECK(result.Completed == &fix.transfers[0], "and it is the right one");
    CHECK_EQ(fix.queue.Count, 0UL, "the queue is empty");
    CHECK_EQ(fix.ring.Dequeue, 3UL,
             "and the ring has retired all three TDs in one step");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 192UL + 142UL + 192UL,
             "the request's total is the sum of its packets");
    CHECK_EQ(fix.queue.IsoPacketsAnswered, 3UL, "all three packets answered");
    CHECK_EQ(fix.queue.IsoGroupsAwaitingTail, 0UL,
             "and the ordinary positional rule is what ended it");
}

static void test_missed_service_and_skips(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    PUSBPORT_ISO_PACKET packet;

    iso_fixture_init(&fix, 32);
    CHECK_EQ(iso_fixture_submit(&fix, 0, 4, 192), XHCI_XFER_OK,
             "a four-packet request is placed");
    packet = fix.iso[0].Block.Packet;

    /*
     * A Missed Service Error on packet 0. "The data associated with the TD in
     * error shall be lost, however for the next ESIT the xHC shall advance to
     * the next Isoch TD and attempt to execute it" (4.10.3.2 p.187) - so the
     * pipe carries on and this is not a recovery case.
     */
    CHECK_EQ(iso_event(&fix, 0, WANT_CC_MISSED_SERVICE, 100, &result),
             XHCI_XFER_OK, "a Missed Service event is accepted");
    CHECK_EQ(result.NeedsRecovery, 0UL,
             "an isoch endpoint never halts, so nothing is owed");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "and the group runs on");
    CHECK_EQ((ULONG)packet[0].Status, WANT_USBD_NOT_ACCESSED,
             "the packet is reported as never serviced");
    CHECK_EQ(packet[0].LengthTransferred, 0UL,
             "with no bytes - the event's length field is a residue in a buffer "
             "whose data the spec says is lost");
    CHECK_EQ(fix.queue.IsoMissedService, 1UL, "counted apart from an error");

    /*
     * **A skipped packet.** The controller "may not generate a Missed Service
     * Error for each Isochronous deadline missed, e.g. if the Event Ring is
     * full" (4.11.2.3 p.196), so packet 1 can produce no event at all and the
     * next one to arrive is packet 2's. Packet 1 has to be given a status here:
     * usbport's block is zero-filled and zero is Success, so leaving it would
     * report a packet that never went on the wire as having succeeded with no
     * data.
     */
    CHECK_EQ(iso_event(&fix, 2, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "packet 2's event arrives with packet 1's missing");
    CHECK_EQ((ULONG)packet[1].Status, WANT_USBD_NOT_ACCESSED,
             "the skipped packet is stamped as never accessed");
    CHECK_EQ(packet[1].LengthTransferred, 0UL, "with no bytes");
    CHECK_EQ((ULONG)packet[2].Status, WANT_USBD_SUCCESS,
             "and packet 2 keeps its own status");
    CHECK_EQ(fix.transfers[0].IsoPacketsAnswered, 3UL,
             "the position has moved past both");

    /*
     * A duplicate event for a packet already behind the position changes
     * nothing - not even the byte total, which a second measurement of the same
     * packet would otherwise double.
     */
    CHECK_EQ(iso_event(&fix, 2, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "a duplicate event is accepted");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 192UL,
             "and adds nothing to the total");
    CHECK_EQ(fix.queue.UnmatchedEvents, 1UL, "it is counted as unmatched");

    CHECK_EQ(iso_event(&fix, 3, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "the last packet's event ends the request");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "completing it");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 384UL,
             "with only the two packets that moved bytes counted");
}

static void test_group_waits_for_its_tail(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    /*
     * **The mid-TD event, and the batch 9-A review's first MAJOR.** The final
     * packet is split across two fragments and an error lands on its *first*
     * TRB, so every packet of the group has an answer while the group's last TRB
     * has produced no event of its own.
     *
     * The first version of this engine retired there, on the reasoning that the
     * controller had advanced to the next ESIT and nothing further was coming.
     * The second half of that is false and this repository had already
     * transcribed why: the xHC "shall not drop Events associated with TRBs as it
     * attempts to resynchronize an Isoch pipe, e.g. ... if IOC = `1` in an Event
     * Data or Normal TRB then it returns Missed Service Error" (p.201), and
     * every TD's last TRB carries IOC here. p.188 states the consequence for
     * exactly this case - if the event "does not point to last TRB of the Isoch
     * TD ... software will have to wait until the next IOC flag is encountered
     * by the endpoint before it can reclaim" it.
     *
     * So the group is **deferred**, and the vector that stood here asserted the
     * opposite: a completion, and a dequeue pointer past a TRB the xHC still
     * owned. It passed for the wrong reason, which is the whole point of writing
     * the corrected one down.
     */
    iso_fixture_init(&fix, 32);
    iso_init(&fix.iso[0]);
    iso_add(&fix.iso[0], 0x0C100000UL, 192, 700);
    iso_add_split(&fix.iso[0], 0x0C101000UL, 700, 324, 701);
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    CHECK_EQ(XhciXferSubmitIso(&fix.queue, &fix.ring, &fix.req, 1,
                               &fix.transfers[0], (PVOID)0x2000UL,
                               fix.scratch, XHCI_XFER_MAX_ISO_TRBS,
                               &fix.layout),
             XHCI_XFER_OK, "a two-packet request with a split tail is placed");
    CHECK_EQ(fix.layout.TrbCount, 3UL, "three TRBs");
    CHECK_EQ(fix.transfers[0].LastIndex, 2UL, "the group's last TRB is index 2");

    CHECK_EQ(iso_event(&fix, 0, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "packet 0 completes");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "and does not end the group");

    /* The event names TRB 1 - the split packet's *first* fragment - not the
     * group's last TRB. */
    CHECK_EQ(iso_event(&fix, 1, WANT_CC_MISSED_SERVICE, 0, &result),
             XHCI_XFER_OK, "packet 1's TD is missed on its first fragment");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE,
             "and the group is NOT retired, though every packet is answered");
    CHECK_EQ(fix.queue.IsoGroupsAwaitingTail, 1UL,
             "counted as the deferral it is");
    CHECK_EQ(fix.ring.Dequeue, 0UL,
             "the dequeue pointer has not moved - the xHC still owns the tail");
    CHECK_EQ(fix.queue.Count, 1UL, "and the request is still queued");
    CHECK_EQ((ULONG)fix.iso[0].Block.Packet[1].Status, WANT_USBD_NOT_ACCESSED,
             "the packet's own status was still written by the event that "
             "measured it");

    /*
     * **The tail p.201 promises.** It repeats the original condition code and
     * names the group's last TRB, so it is a duplicate as a *measurement* - it
     * must not answer packet 1 twice - and the proof of ownership as a
     * *position*, which is what ends the group.
     */
    CHECK_EQ(iso_event(&fix, 2, WANT_CC_MISSED_SERVICE, 0, &result),
             XHCI_XFER_OK, "the tail event arrives");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "and ends the group");
    CHECK_EQ(fix.queue.UnmatchedEvents, 1UL,
             "counted as the duplicate measurement it also is");
    CHECK_EQ(fix.queue.IsoPacketsAnswered, 2UL,
             "with each packet answered exactly once");
    CHECK_EQ(fix.ring.Dequeue, 3UL,
             "and now the whole group's TRBs come back, tail included");
    CHECK_EQ(fix.queue.Count, 0UL, "nothing left queued");
}

static void test_deferred_group_swept_by_a_later_one(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    /*
     * **What happens when the promised tail never arrives**, which an Event Ring
     * Full condition produces (p.197). The deferred group is not leaked: the
     * next group's retire jumps the dequeue pointer past it, and the sweep
     * completes it with `NOT_ACCESSED` - the same backstop every other ring kind
     * in this driver has, reached here without any isochronous-specific machine.
     */
    iso_fixture_init(&fix, 32);
    iso_init(&fix.iso[0]);
    iso_add(&fix.iso[0], 0x0C100000UL, 192, 700);
    iso_add_split(&fix.iso[0], 0x0C101000UL, 700, 324, 701);
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    CHECK_EQ(XhciXferSubmitIso(&fix.queue, &fix.ring, &fix.req, 1,
                               &fix.transfers[0], (PVOID)0x2000UL,
                               fix.scratch, XHCI_XFER_MAX_ISO_TRBS,
                               &fix.layout),
             XHCI_XFER_OK, "the first request is placed");

    iso_init(&fix.iso[1]);
    iso_add(&fix.iso[1], 0x0C102000UL, 192, 702);
    iso_request(&fix.req, &fix.iso[1], 1, 1024, 0);
    CHECK_EQ(XhciXferSubmitIso(&fix.queue, &fix.ring, &fix.req, 1,
                               &fix.transfers[1], (PVOID)0x2001UL,
                               fix.scratch, XHCI_XFER_MAX_ISO_TRBS,
                               &fix.layout),
             XHCI_XFER_OK, "and a second behind it");

    CHECK_EQ(iso_event(&fix, 1, WANT_CC_MISSED_SERVICE, 0, &result),
             XHCI_XFER_OK, "the first group's last packet is missed mid-TD");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "so it defers");

    /* Its tail is never sent. The second group's own tail is. */
    CHECK_EQ(iso_event(&fix, 3, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "the second group completes normally");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "and retires");
    CHECK_EQ(result.CompletedCount, 2UL,
             "sweeping the deferred group ahead of it rather than leaking it");
    CHECK_EQ(fix.queue.SweptTransfers, 1UL, "counted as a sweep");
    CHECK_EQ((LONG)fix.transfers[0].UsbdStatus,
             (LONG)XHCI_USBD_STATUS_NOT_ACCESSED,
             "and the swept request says its packets were not accessed");
    CHECK_EQ(fix.queue.Count, 0UL, "the queue is empty");
}

static void test_sweeps_and_finalise(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    PXHCI_TRANSFER swept;

    /*
     * Two requests outstanding, and the controller reports only the second. The
     * first had its TRBs reclaimed by the same store, so it is completed too -
     * the rule docs/contributing/implementation-invariants.md, "Completion Matching" states
     * for every ring - and on an isoch endpoint it means the controller skipped
     * its TDs without reporting them.
     */
    iso_fixture_init(&fix, 32);
    CHECK_EQ(iso_fixture_submit(&fix, 0, 2, 192), XHCI_XFER_OK, "first placed");
    CHECK_EQ(iso_fixture_submit(&fix, 1, 2, 192), XHCI_XFER_OK, "second placed");
    CHECK_EQ(fix.queue.Count, 2UL, "both queued");

    CHECK_EQ(iso_event(&fix, 2, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "the second request's first packet completes");
    CHECK_EQ(iso_event(&fix, 3, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "and its last");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "ending it");
    CHECK_EQ(result.CompletedCount, 2UL, "with the earlier request swept along");
    CHECK_EQ(fix.queue.SweptTransfers, 1UL, "counted as a sweep");

    swept = result.Completed;
    CHECK(swept == &fix.transfers[0], "oldest first, as usbport expects");
    CHECK_EQ((ULONG)swept->UsbdStatus, WANT_USBD_NOT_ACCESSED,
             "and the swept request says its packets never ran");

    /*
     * **`XhciXferIsoFinalise` is what makes that visible per packet.** Until it
     * runs, the swept request's block still reads zero everywhere - and zero is
     * `USBD_STATUS_SUCCESS`, so the client driver would be told two packets
     * succeeded with no data rather than that they were never serviced.
     */
    CHECK_EQ((ULONG)fix.iso[0].Block.Packet[0].Status, WANT_USBD_SUCCESS,
             "(before the finalise, the zero-filled block reads as success)");
    XhciXferIsoFinalise(swept);
    CHECK_EQ((ULONG)fix.iso[0].Block.Packet[0].Status, WANT_USBD_NOT_ACCESSED,
             "afterwards every unanswered packet carries the request's status");
    CHECK_EQ((ULONG)fix.iso[0].Block.Packet[1].Status, WANT_USBD_NOT_ACCESSED,
             "all of them");
    CHECK_EQ(fix.iso[0].Block.Packet[0].LengthTransferred, 0UL,
             "with no bytes claimed");
    CHECK_EQ(swept->IsoPacketsAnswered, swept->IsoPacketCount,
             "and the position is at the end, so a second call changes nothing");
    XhciXferIsoFinalise(swept);
    CHECK_EQ((ULONG)fix.iso[0].Block.Packet[0].Status, WANT_USBD_NOT_ACCESSED,
             "which makes it idempotent");

    /* A transfer that is not isochronous is left alone - the drain calls this on
     * every completion, so it has to be safe on all of them. */
    fix.transfers[2].Flags = 0;
    fix.transfers[2].IsoParams = NULL;
    fix.transfers[2].IsoPacketCount = 0;
    fix.transfers[2].IsoPacketsAnswered = 0;
    XhciXferIsoFinalise(&fix.transfers[2]);
    CHECK_EQ(fix.transfers[2].IsoPacketsAnswered, 0UL,
             "a non-isochronous transfer is untouched");
    XhciXferIsoFinalise(NULL);
}

static void test_submit_refusals(void)
{
    ISO_FIXTURE fix;
    ULONG i;

    /*
     * A ring too small for the request. `XHCI_XFER_ISO_TOO_LARGE` and
     * `XHCI_XFER_BUSY` are deliberately different answers: one is a request that
     * can never fit and has to be failed, the other is the same ring being full
     * right now and is answered by waiting. Reporting the first as the second
     * leaves usbport resubmitting for ever.
     */
    iso_fixture_init(&fix, 8);              /* capacity 6 */
    CHECK_EQ(iso_fixture_submit(&fix, 0, 8, 192), XHCI_XFER_ISO_TOO_LARGE,
             "eight packets cannot fit a six-TRB ring, ever");
    CHECK_EQ(fix.queue.Count, 0UL, "and nothing was queued");
    CHECK_EQ(fix.ring.Enqueue, 0UL, "nor placed");

    CHECK_EQ(iso_fixture_submit(&fix, 0, 4, 192), XHCI_XFER_OK,
             "four packets do fit");
    CHECK_EQ(iso_fixture_submit(&fix, 1, 4, 192), XHCI_XFER_BUSY,
             "a second four does not fit *now*");
    CHECK_EQ(fix.queue.Count, 1UL, "and the second request was not queued");

    /*
     * The ring-full answer has to be all-or-nothing: a partial group on the ring
     * is not something the hardware can be told to ignore later.
     */
    CHECK_EQ(fix.ring.Enqueue, 4UL, "the enqueue pointer did not move");

    /* And a request the builder refuses is neither of those. */
    iso_fixture_init(&fix, 32);
    iso_init(&fix.iso[0]);
    for (i = 0; i < 2; i++) {
        iso_add(&fix.iso[0], 0x0C200000UL + i * 0x1000UL, 192, 700 + i);
    }
    fix.iso[0].Block.Signature = 0;
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    CHECK_EQ(XhciXferSubmitIso(&fix.queue, &fix.ring, &fix.req, 1,
                               &fix.transfers[0], (PVOID)0x2000UL,
                               fix.scratch, XHCI_XFER_MAX_ISO_TRBS,
                               &fix.layout),
             XHCI_XFER_ISO_MALFORMED, "a malformed block is its own refusal");
    CHECK_EQ(fix.queue.Count, 0UL, "with nothing queued");

    /*
     * **A size refusal arriving by the other door**, which the batch 9-A review
     * found reported as a malformed block. The packet-count bound is one TRB per
     * packet, so a request of *fewer* packets each split across two fragments
     * fills the scratch first - and the scratch is exactly the ring's capacity,
     * so it is the same "can never be placed" fact. Answering it as
     * `ISO_MALFORMED` charged it to the counter that says this driver and
     * usbport disagree about the block layout, and left the counter that says
     * the pooled ring is too small reading zero.
     */
    iso_fixture_init(&fix, 8);              /* capacity 6 */
    iso_init(&fix.iso[0]);
    for (i = 0; i < 4; i++) {
        iso_add_split(&fix.iso[0], 0x0C300000UL + i * 0x2000UL, 700, 324,
                      700 + i);
    }
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    CHECK_EQ(XhciXferSubmitIso(&fix.queue, &fix.ring, &fix.req, 1,
                               &fix.transfers[0], (PVOID)0x2000UL,
                               fix.scratch, 6UL, &fix.layout),
             XHCI_XFER_ISO_TOO_LARGE,
             "four two-TRB packets need eight TRBs, and that is too large "
             "rather than malformed");
    CHECK_EQ(fix.queue.Count, 0UL, "with nothing queued");
}

static void test_multi_trb_packet_length(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    /*
     * **The residual names one TRB, not the TD** - the batch 9-A review's sixth
     * MAJOR, and batch 6-A's lesson arriving at a third builder.
     *
     * Table 6-39 p.441: "For multi-TRB TDs, if ED = `0`, the TRB Transfer Length
     * only reflects the number of bytes transferred for the buffer associated
     * with the Transfer TRB pointed to by the Transfer Event, **not the total
     * bytes transferred for the TD**." So for an event on the *first* TRB of a
     * 700+324 split, the bytes moved are 700 - residual, and the arithmetic that
     * stood here - the packet's whole `Length` minus the residual - reported
     * 1024 - residual instead: an entire fragment of audio that never arrived,
     * handed to the client as data.
     */
    iso_fixture_init(&fix, 32);
    iso_init(&fix.iso[0]);
    iso_add_split(&fix.iso[0], 0x0C400000UL, 700, 324, 700);
    iso_add(&fix.iso[0], 0x0C402000UL, 192, 701);
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    CHECK_EQ(XhciXferSubmitIso(&fix.queue, &fix.ring, &fix.req, 1,
                               &fix.transfers[0], (PVOID)0x2000UL,
                               fix.scratch, XHCI_XFER_MAX_ISO_TRBS,
                               &fix.layout),
             XHCI_XFER_OK, "a split first packet is placed");
    CHECK_EQ(fix.layout.TrbCount, 3UL, "as three TRBs");

    CHECK_EQ(iso_event(&fix, 0, WANT_CC_SHORT_PACKET, 100, &result),
             XHCI_XFER_OK, "a short read lands on the first fragment");
    CHECK_EQ(fix.iso[0].Block.Packet[0].LengthTransferred, 600UL,
             "600 bytes moved - the first TRB's 700 less its 100 residual, not "
             "the packet's 1024 less 100");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 600UL,
             "and the request's total says the same");

    /*
     * The tail of that TD repeats the code (p.175) and is a duplicate
     * measurement, so it must not add a second 600 - and it is not the group's
     * last TRB either, so it does not end anything.
     */
    /* The residual is the *original* one: "the second [event] for the last TRB
     * with the IOC flag set" repeats the first's length rather than measuring its
     * own TRB (p.175). A draft of this vector invented 324 here, which is the
     * tail TRB's own length and not a shape the spec produces. */
    CHECK_EQ(iso_event(&fix, 1, WANT_CC_SHORT_PACKET, 100, &result),
             XHCI_XFER_OK, "the TD's own tail repeats it");
    CHECK_EQ(fix.transfers[0].BytesTransferred, 600UL, "adding nothing");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "and ending nothing");

    /* A residual larger than the range summed so far is impossible and is
     * refused rather than turned into a plausible number. */
    iso_fixture_init(&fix, 32);
    iso_init(&fix.iso[0]);
    iso_add_split(&fix.iso[0], 0x0C400000UL, 700, 324, 700);
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    CHECK_EQ(XhciXferSubmitIso(&fix.queue, &fix.ring, &fix.req, 1,
                               &fix.transfers[0], (PVOID)0x2000UL,
                               fix.scratch, XHCI_XFER_MAX_ISO_TRBS,
                               &fix.layout),
             XHCI_XFER_OK, "placed again");
    CHECK_EQ(iso_event(&fix, 0, WANT_CC_SHORT_PACKET, 800, &result),
             XHCI_XFER_OK, "a residual of 800 against a 700-byte TRB");
    CHECK_EQ(fix.queue.ResidualRejects, 1UL, "is rejected");
    CHECK_EQ(fix.iso[0].Block.Packet[0].LengthTransferred, 0UL,
             "with no bytes claimed");
    CHECK_EQ((ULONG)fix.iso[0].Block.Packet[0].Status,
             WANT_USBD_INTERNAL_HC_ERROR, "and reported as a host error");
}

static void test_frame_cadence_gate(void)
{
    ISO_FIXTURE fix;
    ULONG i;

    /*
     * **usbport's packet stamps against the Interval this driver programmed** -
     * the batch 9-A review's fourth MAJOR, and the only runtime measurement this
     * driver has of an isochronous endpoint's real service interval.
     *
     * usbport stamps packet `i` with `StartFrame + (i >> 3)` on High Speed and
     * `StartFrame + i` otherwise, whatever the endpoint descriptor says. The
     * reachable disagreement is Phase 5 task 7's: a Full-Speed device on a root
     * port is reported as High Speed, so usbport stamps eight packets per frame
     * while the Endpoint Context - built from the device's true speed - consumes
     * one per frame. Eight TDs in a row would then carry the *same* Frame ID,
     * and only the first would name the frame it runs in.
     */
    iso_fixture_init(&fix, 32);
    iso_init(&fix.iso[0]);
    for (i = 0; i < 8; i++) {
        iso_add(&fix.iso[0], 0x0C500000UL + i * 0x1000UL, 192, 700 + (i >> 3));
    }
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);   /* one TD per frame */
    fix.req.Frames.Allowed = 1;
    fix.req.Frames.CurrentFrame = 690;
    CHECK_EQ(XhciXferBuildIso(&fix.req, 1, fix.scratch,
                              XHCI_XFER_MAX_ISO_TRBS, &fix.layout),
             XHCI_XFER_OK, "the request still builds");
    CHECK_EQ(fix.layout.CadenceMismatch, 1UL,
             "the High-Speed stamping is seen not to match a Full-Speed ESIT");
    CHECK_EQ(fix.layout.FrameIdsUsed, 0UL,
             "so the whole request drops to SIA rather than repeating one "
             "Frame ID eight times");
    CHECK_EQ(fix.scratch[0].Control & ISO_SIA, ISO_SIA, "TRB 0 carries SIA");
    CHECK_EQ(fix.scratch[7].Control & ISO_SIA, ISO_SIA, "and so does TRB 7");

    /* The same eight packets stamped at the cadence the endpoint really has. */
    iso_init(&fix.iso[0]);
    for (i = 0; i < 8; i++) {
        iso_add(&fix.iso[0], 0x0C500000UL + i * 0x1000UL, 192, 700 + i);
    }
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    fix.req.Frames.Allowed = 1;
    fix.req.Frames.CurrentFrame = 690;
    CHECK_EQ(XhciXferBuildIso(&fix.req, 1, fix.scratch,
                              XHCI_XFER_MAX_ISO_TRBS, &fix.layout),
             XHCI_XFER_OK, "builds");
    CHECK_EQ(fix.layout.CadenceMismatch, 0UL, "no mismatch");
    CHECK_EQ(fix.layout.FrameIdsUsed, 1UL, "and the Frame IDs are used");
    CHECK_EQ(fix.scratch[0].Control & ISO_FRAME_ID(0x7FF),
             ISO_FRAME_ID(700),
             "each naming its own frame");
    CHECK_EQ(fix.scratch[7].Control & ISO_FRAME_ID(0x7FF),
             ISO_FRAME_ID(707),
             "one frame apart, packet by packet");

    /*
     * A High-Speed endpoint really does put eight TDs in one frame, and there
     * the repeated Frame ID is correct - so the gate is on the *agreement*, not
     * on the repetition.
     */
    iso_init(&fix.iso[0]);
    for (i = 0; i < 8; i++) {
        iso_add(&fix.iso[0], 0x0C500000UL + i * 0x1000UL, 192, 700 + (i >> 3));
    }
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    fix.req.PacketsPerFrame = 8;             /* High Speed, Interval 0 */
    fix.req.Frames.Allowed = 1;
    fix.req.Frames.CurrentFrame = 690;
    CHECK_EQ(XhciXferBuildIso(&fix.req, 1, fix.scratch,
                              XHCI_XFER_MAX_ISO_TRBS, &fix.layout),
             XHCI_XFER_OK, "builds");
    CHECK_EQ(fix.layout.CadenceMismatch, 0UL,
             "eight packets in one frame agree with a 125 us ESIT");
    CHECK_EQ(fix.layout.FrameIdsUsed, 1UL, "so Frame IDs are used");
    CHECK_EQ(fix.scratch[7].Control & ISO_FRAME_ID(0x7FF),
             ISO_FRAME_ID(700),
             "all eight naming the one frame they are executed in");

    /*
     * A cadence this driver cannot state refuses every Frame ID rather than
     * guessing one. Interval above 3 is frames *per TD* rather than TDs per
     * frame, and this driver programs no such endpoint.
     */
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    fix.req.PacketsPerFrame = 0;
    fix.req.Frames.Allowed = 1;
    fix.req.Frames.CurrentFrame = 690;
    CHECK_EQ(XhciXferBuildIso(&fix.req, 1, fix.scratch,
                              XHCI_XFER_MAX_ISO_TRBS, &fix.layout),
             XHCI_XFER_OK, "builds");
    CHECK_EQ(fix.layout.CadenceMismatch, 1UL, "an unstateable cadence mismatches");
    CHECK_EQ(fix.layout.FrameIdsUsed, 0UL, "and refuses every Frame ID");

    /* One packet cannot show a cadence, and must not be called a mismatch. */
    iso_init(&fix.iso[0]);
    iso_add(&fix.iso[0], 0x0C500000UL, 192, 700);
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    fix.req.Frames.Allowed = 1;
    fix.req.Frames.CurrentFrame = 690;
    CHECK_EQ(XhciXferBuildIso(&fix.req, 1, fix.scratch,
                              XHCI_XFER_MAX_ISO_TRBS, &fix.layout),
             XHCI_XFER_OK, "a one-packet request builds");
    CHECK_EQ(fix.layout.CadenceMismatch, 0UL, "with no mismatch to report");
    CHECK_EQ(fix.layout.FrameIdsUsed, 1UL, "and its Frame ID is used");
}

static void test_codes_illegal_on_an_isoch_ring(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    XHCI_XFER_CODE info;

    /*
     * **The ring kind's own list, applied on the per-packet path** - the second
     * review round's fifth MAJOR. This decoder used to fall through to the shared
     * one, which serves the *endpoint* kind and therefore accepts Stall Error and
     * Invalid Stream ID. Neither can happen on an isochronous ring: an isoch pipe
     * "never halts because there is no handshake to report a halt condition"
     * (p.177), and this driver implements no streams. One arriving is a
     * controller fault or a misattributed event, and decoding it wrote
     * `STALL_PID` into usbport's block, retired the group and handed its mapped
     * buffer back - which is precisely what the ring Kind exists to prevent.
     */
    CHECK_EQ(XhciXferIsoCodeInfo(WANT_CC_STALL, &info), XHCI_XFER_BAD_PARAM,
             "a Stall is not a code an isochronous ring can carry");
    CHECK_EQ(XhciXferIsoCodeInfo(WANT_CC_INVALID_STREAM_ID, &info),
             XHCI_XFER_BAD_PARAM, "nor an Invalid Stream ID");
    /* And the contrast that makes it a statement about the *kind* rather than
     * about the codes: the endpoint ring accepts both. */
    CHECK_EQ(XhciXferCodeInfo(WANT_CC_STALL, &info), XHCI_XFER_OK,
             "while the shared decoder still accepts a Stall, which is what this "
             "path used to reach");

    /* End to end: the event is refused before it can retire anything. */
    iso_fixture_init(&fix, 32);
    CHECK_EQ(iso_fixture_submit(&fix, 0, 2, 192), XHCI_XFER_OK, "placed");
    CHECK_EQ(iso_event(&fix, 1, WANT_CC_STALL, 0, &result), XHCI_XFER_OK,
             "a Stall naming the group's last TRB is accepted as an event");
    CHECK_EQ(fix.queue.BadCodes, 1UL, "and counted as a bad code");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "retiring nothing");
    CHECK_EQ(fix.queue.Count, 1UL, "with the request still queued");
    CHECK_EQ(fix.ring.Dequeue, 0UL,
             "and the mapped buffer still this driver's to hold");
}

static void test_ring_record_divergence_asks_for_a_stop(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    /*
     * **A divergence between the ring's chain and this record's packet count.**
     * The caller has already placed the event inside this transfer's range, so a
     * failed packet walk means the two disagree - and the event that found it may
     * be the group's only tail. Dropping it as merely unmatched strands the
     * request until something else cancels it; the ordinary transfer path answers
     * the same condition with a Stop Endpoint and a drain continuation, the one
     * sequence that makes a completion and a repositioning legal on a Running
     * endpoint.
     *
     * The divergence is constructed by clearing the Chain bit that joins a split
     * packet's two TRBs, which makes the second TRB a TD of its own that no
     * recorded packet owns.
     */
    iso_fixture_init(&fix, 32);
    iso_init(&fix.iso[0]);
    iso_add_split(&fix.iso[0], 0x0C600000UL, 700, 324, 700);
    iso_request(&fix.req, &fix.iso[0], 1, 1024, 0);
    CHECK_EQ(XhciXferSubmitIso(&fix.queue, &fix.ring, &fix.req, 1,
                               &fix.transfers[0], (PVOID)0x2000UL,
                               fix.scratch, XHCI_XFER_MAX_ISO_TRBS,
                               &fix.layout),
             XHCI_XFER_OK, "a one-packet, two-TRB request is placed");

    fix.ring.Base[0].Control &= ~ISO_CH;

    CHECK_EQ(iso_event(&fix, 1, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "the tail event cannot be mapped to a packet any more");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_NONE, "so nothing is retired");
    CHECK_EQ(result.RefusedRetire, 1UL,
             "and it is reported as the divergence it is");
    CHECK_EQ(result.NeedsRecovery, 1UL,
             "asking for the stop that makes a repositioning legal");
    CHECK_EQ(fix.queue.Count, 1UL, "with the request still queued");
}

static void test_trb_error_needs_recovery(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    /*
     * **The one isochronous error software must act on** - the batch 9-A
     * review's fifth MAJOR. An isoch endpoint "never halts because there is no
     * handshake to report a halt condition" (p.177), which is why this engine
     * asks for no recovery in general; but 4.8.3 p.149 puts a TRB Error
     * somewhere else entirely - it "should cause a Running Endpoint to
     * transition to the Error state. A Set TR Dequeue Pointer Command shall be
     * used to transition the endpoint to the Stopped state" - with no
     * qualification by endpoint type. An endpoint left in Error runs nothing,
     * and the doorbell this driver keeps ringing means nothing to it.
     *
     * `xhciEventNeedsRecovery` already encodes exactly this for the ring
     * classifier; the isochronous event path does not call the classifier, so it
     * is the second site that has to obey the same rule.
     */
    iso_fixture_init(&fix, 32);
    CHECK_EQ(iso_fixture_submit(&fix, 0, 2, 192), XHCI_XFER_OK, "placed");

    CHECK_EQ(iso_event(&fix, 0, WANT_CC_TRB_ERROR, 0, &result), XHCI_XFER_OK,
             "a TRB Error on the first packet");
    CHECK_EQ(result.NeedsRecovery, 1UL, "asks for recovery");
    CHECK_EQ((ULONG)fix.iso[0].Block.Packet[0].Status,
             WANT_USBD_INTERNAL_HC_ERROR, "and reports the packet as failed");

    /* Every other isochronous code leaves the endpoint Running and asks for
     * nothing - a Missed Service is the pipe resynchronizing itself. */
    iso_fixture_init(&fix, 32);
    CHECK_EQ(iso_fixture_submit(&fix, 0, 2, 192), XHCI_XFER_OK, "placed");
    CHECK_EQ(iso_event(&fix, 0, WANT_CC_MISSED_SERVICE, 0, &result),
             XHCI_XFER_OK, "a Missed Service");
    CHECK_EQ(result.NeedsRecovery, 0UL, "asks for no recovery");
    CHECK_EQ(iso_event(&fix, 1, WANT_CC_ISOCH_BUFFER_OVER, 0, &result),
             XHCI_XFER_OK, "nor does an Isoch Buffer Overrun");
    CHECK_EQ(result.NeedsRecovery, 0UL, "on a pipe that cannot halt");
}

static void test_event_rejections(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;

    iso_fixture_init(&fix, 32);
    CHECK_EQ(iso_fixture_submit(&fix, 0, 2, 192), XHCI_XFER_OK, "placed");

    /* An event for another endpoint. The DCI is the cheap and exact
     * discriminator, and it is checked before the pointer is resolved. */
    CHECK_EQ(XhciXferIsoEvent(&fix.queue, &fix.ring, ISO_FIX_SLOT,
                              ISO_FIX_DCI, RING_PA,
                              event_dw2(WANT_CC_SUCCESS, 0),
                              event_dw3(ISO_FIX_SLOT, ISO_FIX_DCI + 1),
                              &result),
             XHCI_XFER_OK, "an event for another DCI is accepted and dropped");
    CHECK_EQ(fix.queue.ForeignEvents, 1UL, "and counted as foreign");
    CHECK_EQ(fix.queue.Count, 1UL, "with nothing completed");

    /* A TRB pointer on no ring this driver owns - including the zero the xHC
     * uses for an error it cannot attribute to a TRB (4.11.3.1). */
    CHECK_EQ(XhciXferIsoEvent(&fix.queue, &fix.ring, ISO_FIX_SLOT, ISO_FIX_DCI,
                              0, event_dw2(WANT_CC_SUCCESS, 0),
                              event_dw3(ISO_FIX_SLOT, ISO_FIX_DCI), &result),
             XHCI_XFER_OK, "a zero TRB pointer is dropped");
    CHECK_EQ(fix.queue.ForeignEvents, 2UL, "and counted");

    /*
     * An unassigned completion code - 100, which Table 6-90 gives to nothing.
     * **Not one of the vendor ranges**: 192-223 and 224-255 are assigned, carry
     * their own default reading, and are decoded successfully, so a vector using
     * one would be checking that a legal code is legal.
     */
    CHECK_EQ(iso_event(&fix, 0, 100, 0, &result), XHCI_XFER_OK,
             "an unassigned code is accepted and dropped");
    CHECK_EQ(fix.queue.BadCodes, 1UL, "and counted");
    CHECK_EQ(fix.queue.Count, 1UL, "with the transfer still outstanding");

    /*
     * A residual larger than the packet it applies to is an impossible answer.
     * It is refused rather than clamped: the unsigned subtraction would hand
     * usbport a length near 4 GB and clamping to zero would report a plausible
     * number nothing measured.
     */
    CHECK_EQ(iso_event(&fix, 0, WANT_CC_SUCCESS, 500, &result), XHCI_XFER_OK,
             "an impossible residual is accepted");
    CHECK_EQ(fix.queue.ResidualRejects, 1UL, "and counted as a reject");
    CHECK_EQ((ULONG)fix.iso[0].Block.Packet[0].Status,
             WANT_USBD_INTERNAL_HC_ERROR,
             "with the packet failed rather than given a made-up length");
    CHECK_EQ(fix.iso[0].Block.Packet[0].LengthTransferred, 0UL,
             "and no bytes claimed for it");

    CHECK_EQ(XhciXferIsoEvent(NULL, &fix.ring, ISO_FIX_SLOT, ISO_FIX_DCI,
                              RING_PA, 0, 0, &result),
             XHCI_XFER_BAD_PARAM, "a NULL queue is a caller bug");
    CHECK_EQ(XhciXferIsoEvent(&fix.queue, &fix.ring, ISO_FIX_SLOT, ISO_FIX_DCI,
                              RING_PA, 0, 0, NULL),
             XHCI_XFER_BAD_PARAM, "and so is a NULL result");
}

static void test_submit_across_the_link(void)
{
    ISO_FIXTURE fix;
    XHCI_XFER_EVENT_RESULT result;
    ULONG i;

    /*
     * A group that spans the ring's wrap-back Link TRB. The Chain bit on that
     * Link has to follow the *preceding* TRB's - set when a TD continues past it
     * and clear between two TDs (p.208) - and an isochronous group is the shape
     * that exercises both, because it is many TDs of one or two TRBs.
     */
    iso_fixture_init(&fix, 8);              /* 8 TRBs: 7 usable, capacity 6 */
    CHECK_EQ(iso_fixture_submit(&fix, 0, 4, 192), XHCI_XFER_OK, "first group");
    for (i = 0; i < 4; i++) {
        CHECK_EQ(iso_event(&fix, i, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
                 "drained");
    }
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE, "and completed");
    CHECK_EQ(fix.ring.Dequeue, 4UL, "the ring is empty at index 4");

    /* This one starts at 4 and runs past the Link at index 7. */
    CHECK_EQ(iso_fixture_submit(&fix, 1, 4, 192), XHCI_XFER_OK,
             "a group placed across the Link TRB");
    CHECK_EQ(fix.transfers[1].FirstIndex, 4UL, "starting at 4");
    CHECK_EQ(fix.transfers[1].LastIndex, 0UL,
             "and ending at index 0 - the enqueue pointer never rests on the "
             "Link TRB at index 7, so the fourth TRB lands after the wrap");
    CHECK_EQ(fix.mem[7].Control & ISO_CH, 0UL,
             "the Link TRB is between two TDs here, so its Chain bit is clear");

    CHECK_EQ(iso_event(&fix, 4, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "0");
    CHECK_EQ(iso_event(&fix, 5, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "1");
    CHECK_EQ(iso_event(&fix, 6, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK, "2");
    CHECK_EQ(iso_event(&fix, 0, WANT_CC_SUCCESS, 0, &result), XHCI_XFER_OK,
             "and the packet after the wrap");
    CHECK_EQ(result.Action, XHCI_XFER_ACTION_COMPLETE,
             "completes the group across the Link");
    CHECK_EQ(fix.transfers[1].IsoPacketsAnswered, 4UL,
             "with every packet matched to its own TD, wrap included");
}

int main(void)
{
    test_iso_code_info();
    test_frame_window();
    test_build_one_packet();
    test_build_zero_length_packet();
    test_build_split_packet();
    test_build_burst_fields();
    test_build_multiple_packets();
    test_build_frame_ids();
    test_build_refusals();
    test_submit_and_complete();
    test_missed_service_and_skips();
    test_group_waits_for_its_tail();
    test_deferred_group_swept_by_a_later_one();
    test_multi_trb_packet_length();
    test_frame_cadence_gate();
    test_trb_error_needs_recovery();
    test_codes_illegal_on_an_isoch_ring();
    test_ring_record_divergence_asks_for_a_stop();
    test_sweeps_and_finalise();
    test_submit_refusals();
    test_event_rejections();
    test_submit_across_the_link();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
