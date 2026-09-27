/*
 * test_vhub.c - the virtual USB 2.0 hub's pure core (src/xhci_vhub.c),
 * roadmap sub-task 24.3.2, design record 12.
 *
 * What these vectors are the evidence for, and what they are not. The core is
 * computation over one record per root port, so everything here is a sequence
 * of the events the root-hub callbacks and the hub-class requests will feed it
 * in 24.3.3, with each view's status and change bits read back after every
 * step. What needs the driver around it - the slot and its buffers held until
 * the PED confirmation, the deferred-completion list, `xhciDevByHubPort`
 * across a re-open, the snoops never seeing a virtual address - is 24.3.3's
 * to prove in test_init.c, the suite that runs the driver against a
 * synthetic controller; the owner moved those vectors there on 2026-09-25
 * rather than write them against code that does not exist.
 *
 * The setup packets are built byte for byte. The hub-class ones are the
 * measured ones (design record 02: `GET_DESCRIPTOR(Hub)` with `wValue` 0 and
 * `wLength` 71, `SET_FEATURE(PORT_POWER / PORT_RESET)` with the selector in
 * `wValue`, `GET_STATUS(port)` with `wLength` 4). The standard
 * `GET_DESCRIPTOR(Device)` length usbport sends has **no measurement in this
 * tree**, so the device descriptor is fed 8, 18, 64 and 255, which spans every
 * length a USB 2.0 host is known to use, and says so here rather than
 * claiming one of them.
 *
 * Expected bytes are typed out, never recomputed through the code's own
 * builders - the test_ctx.c rule.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_usbport.h"
#include "../src/xhci_vhub.h"
#include "test_harness.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static XHCI_SETUP_PACKET setupOf(UCHAR bmRequestType, UCHAR bRequest,
                                 USHORT wValue, USHORT wIndex, USHORT wLength)
{
    XHCI_SETUP_PACKET s;

    s.bmRequestType = bmRequestType;
    s.bRequest = bRequest;
    s.wValue = wValue;
    s.wIndex = wIndex;
    s.wLength = wLength;
    return s;
}

static void fill(UCHAR *p, ULONG n, UCHAR v)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        p[i] = v;
    }
}

/* Put an ASCII string into a 14-byte service buffer, either encoding, with
 * the bytes after the terminator left as `scratch`. */
static void idBuffer(UCHAR *buf, const char *text, int utf16, UCHAR scratch)
{
    ULONG n;

    fill(buf, XHCI_VHUB_ID_BUF_BYTES, scratch);
    for (n = 0; text[n] != 0; n++) {
        if (utf16) {
            buf[n * 2] = (UCHAR)text[n];
            buf[n * 2 + 1] = 0;
        } else {
            buf[n] = (UCHAR)text[n];
        }
    }
    if (utf16) {
        buf[n * 2] = 0;
        buf[n * 2 + 1] = 0;
    } else {
        buf[n] = 0;
    }
}

static int bytesEqual(const UCHAR *got, const UCHAR *want, ULONG n)
{
    ULONG i;

    for (i = 0; i < n; i++) {
        if (got[i] != want[i]) {
            return 0;
        }
    }
    return 1;
}

static XHCI_VHUB_IDENTITY identityOf(USHORT vid, USHORT pid)
{
    XHCI_VHUB_IDENTITY id;

    id.Vid = vid;
    id.Pid = pid;
    id.BcdDevice = 0x0111;
    /* The root hub's own, XHCI_RH_POWER_ON_TO_POWER_GOOD: 10, 20 ms. */
    id.PowerOnToPowerGood = 10;
    id.Reserved = 0;
    return id;
}

/* ------------------------------------------------------------------ */
/* The switch and the ids (3.1)                                        */
/* ------------------------------------------------------------------ */

static ULONG parse(const char *text, int utf16, ULONG isVendor, USHORT *id,
                   UCHAR *enc)
{
    UCHAR buf[XHCI_VHUB_ID_BUF_BYTES];

    idBuffer(buf, text, utf16, 0xCC);
    return XhciVhubParseId(buf, XHCI_VHUB_ID_BUF_BYTES, isVendor, id, enc);
}

static void testIdAccepted(void)
{
    static const char *forms[] = { "1209", "0x1209", "0X1209" };
    ULONG f;
    int utf16;
    USHORT id;
    UCHAR enc;

    for (utf16 = 0; utf16 <= 1; utf16++) {
        for (f = 0; f < 3; f++) {
            id = 0;
            CHECK_EQ(parse(forms[f], utf16, 1, &id, &enc), XHCI_VHUB_ID_OK,
                     "an accepted vendor id form");
            CHECK_EQ(id, 0x1209, "parsed as hexadecimal");
            CHECK_EQ(enc, utf16 ? XHCI_VHUB_ENC_UTF16 : XHCI_VHUB_ENC_BYTE,
                     "the encoding decided from byte 1 is recorded");
        }
    }
    CHECK_EQ(parse("abCD", 0, 1, &id, &enc), XHCI_VHUB_ID_OK, "either case");
    CHECK_EQ(id, 0xABCD, "mixed case parsed");
    CHECK_EQ(parse("0xFfFf", 1, 0, &id, &enc), XHCI_VHUB_ID_OK, "0xFFFF");
    CHECK_EQ(id, 0xFFFF, "the top of the range");
    CHECK_EQ(parse("0001", 1, 0, &id, &enc), XHCI_VHUB_ID_OK,
             "the INF's product id");
    CHECK_EQ(id, 0x0001, "0001");
    CHECK_EQ(parse("0000", 0, 0, &id, &enc), XHCI_VHUB_ID_OK,
             "a product id of 0000 is accepted");
    CHECK_EQ(id, 0, "as 0");
}

static void testIdRefused(void)
{
    UCHAR buf[XHCI_VHUB_ID_BUF_BYTES];
    USHORT id;
    UCHAR enc;
    int utf16;

    for (utf16 = 0; utf16 <= 1; utf16++) {
        CHECK_EQ(parse("0000", utf16, 1, &id, &enc), XHCI_VHUB_ID_ZERO_VID,
                 "a vendor id of 0000 is refused");
        CHECK_EQ(parse("0x0000", utf16, 1, &id, &enc), XHCI_VHUB_ID_ZERO_VID,
                 "with the prefix too");
        CHECK_EQ(parse("120", utf16, 1, &id, &enc), XHCI_VHUB_ID_DIGITS,
                 "three digits");
        CHECK_EQ(parse("12090", utf16, 1, &id, &enc), XHCI_VHUB_ID_DIGITS,
                 "five digits");
        CHECK_EQ(parse("0x", utf16, 1, &id, &enc), XHCI_VHUB_ID_DIGITS,
                 "a prefix and nothing");
        CHECK_EQ(parse("12G9", utf16, 1, &id, &enc), XHCI_VHUB_ID_CHAR,
                 "a character that is not hex");
        CHECK_EQ(parse(" 1209", utf16, 1, &id, &enc), XHCI_VHUB_ID_CHAR,
                 "a leading space");
        CHECK_EQ(parse("1209 ", utf16, 1, &id, &enc), XHCI_VHUB_ID_CHAR,
                 "a trailing space");
        CHECK_EQ(parse("+1209", utf16, 1, &id, &enc), XHCI_VHUB_ID_CHAR,
                 "a sign");
        CHECK_EQ(parse("0x0x12", utf16, 1, &id, &enc), XHCI_VHUB_ID_CHAR,
                 "a second prefix");
        CHECK_EQ(id, 0, "a refusal leaves no id");
    }
    CHECK_EQ(parse("", 0, 1, &id, &enc), XHCI_VHUB_ID_DIGITS,
             "an empty string");

    /* The one-byte-wider forms: seven single-byte characters fit the buffer
     * (so the parser sees five digits after the prefix), seven UTF-16 ones
     * would not (the service fails them - see testConfig). */
    CHECK_EQ(parse("0x12090", 0, 1, &id, &enc), XHCI_VHUB_ID_DIGITS,
             "prefix and five digits");

    /* No terminator anywhere in the buffer. */
    fill(buf, sizeof(buf), '1');
    CHECK_EQ(XhciVhubParseId(buf, sizeof(buf), 1, &id, &enc),
             XHCI_VHUB_ID_NO_NUL, "single-byte with no terminator");
    fill(buf, sizeof(buf), 0);
    {
        ULONG i;
        for (i = 0; i < sizeof(buf); i += 2) {
            buf[i] = '1';
        }
    }
    CHECK_EQ(XhciVhubParseId(buf, sizeof(buf), 1, &id, &enc),
             XHCI_VHUB_ID_NO_NUL, "UTF-16 with no terminator");

    /*
     * A value created as a DWORD: its bytes are what the service copies, and
     * the rest of the buffer is the service's scratch. 0x00001209 is
     * `09 12 00 00`; byte 1 is not 0, so it reads single-byte, and 0x09 is not
     * a hex character. 0x31 (`'1'`) reads as UTF-16 `"1"`, one digit.
     */
    fill(buf, sizeof(buf), 0xCC);
    buf[0] = 0x09; buf[1] = 0x12; buf[2] = 0; buf[3] = 0;
    CHECK_EQ(XhciVhubParseId(buf, sizeof(buf), 1, &id, &enc),
             XHCI_VHUB_ID_CHAR, "a DWORD 0x1209 is not a string");
    fill(buf, sizeof(buf), 0xCC);
    buf[0] = 0x31; buf[1] = 0; buf[2] = 0; buf[3] = 0;
    CHECK_EQ(XhciVhubParseId(buf, sizeof(buf), 1, &id, &enc),
             XHCI_VHUB_ID_DIGITS, "a DWORD 0x31 is one character");

    /* UTF-16 with a high byte set is not ASCII. */
    idBuffer(buf, "1209", 1, 0);
    buf[3] = 0x01;
    CHECK_EQ(XhciVhubParseId(buf, sizeof(buf), 1, &id, &enc),
             XHCI_VHUB_ID_CHAR, "a UTF-16 unit above 0xFF");

    /* Bytes after the terminator are never read. */
    idBuffer(buf, "1209", 0, 'Z');
    CHECK_EQ(XhciVhubParseId(buf, sizeof(buf), 1, &id, &enc),
             XHCI_VHUB_ID_OK, "scratch after the NUL ignored");

    CHECK_EQ(XhciVhubParseId(NULL, 14, 1, &id, &enc), XHCI_VHUB_ID_NO_NUL,
             "NULL buffer");
    CHECK_EQ(enc, XHCI_VHUB_ENC_NONE, "and no encoding decided");
    CHECK_EQ(XhciVhubParseId(buf, 1, 1, &id, &enc), XHCI_VHUB_ID_NO_NUL,
             "a one-byte buffer");
    CHECK_EQ(enc, XHCI_VHUB_ENC_NONE, "no encoding from one byte either");

    /*
     * The encoding is decided from byte 1 before anything is refused, so a
     * refusal still says which encoding the value arrived in - the reading
     * XHCISNAP prints for 24.3.4's first question, which a refused id must
     * not hide. The prefix is not a digit: "0x" plus three is three.
     */
    for (utf16 = 0; utf16 <= 1; utf16++) {
        CHECK_EQ(parse("120", utf16, 1, &id, &enc), XHCI_VHUB_ID_DIGITS,
                 "three digits");
        CHECK_EQ(enc, utf16 ? XHCI_VHUB_ENC_UTF16 : XHCI_VHUB_ENC_BYTE,
                 "the encoding is recorded on a refusal");
        CHECK_EQ(parse("0x120", utf16, 1, &id, &enc), XHCI_VHUB_ID_DIGITS,
                 "a prefix and three digits");
        CHECK_EQ(parse("12G9", utf16, 1, &id, &enc), XHCI_VHUB_ID_CHAR,
                 "a bad character");
        CHECK_EQ(enc, utf16 ? XHCI_VHUB_ENC_UTF16 : XHCI_VHUB_ENC_BYTE,
                 "recorded on that refusal too");
    }

    /*
     * Four digits inside a four-byte length, with no terminator in reach:
     * every digit is there and the string is still refused, since nothing
     * in the bytes given says where it ends. One more byte, and it is an id.
     */
    idBuffer(buf, "1209", 0, 0xCC);
    CHECK_EQ(XhciVhubParseId(buf, 4, 1, &id, &enc), XHCI_VHUB_ID_NO_NUL,
             "four single-byte digits in four bytes");
    CHECK_EQ(id, 0, "no id from them");
    CHECK_EQ(XhciVhubParseId(buf, 5, 1, &id, &enc), XHCI_VHUB_ID_OK,
             "the same digits with the terminator in reach");
    CHECK_EQ(id, 0x1209, "are the id");
    idBuffer(buf, "12", 1, 0xCC);
    CHECK_EQ(XhciVhubParseId(buf, 4, 1, &id, &enc), XHCI_VHUB_ID_NO_NUL,
             "two UTF-16 digits in four bytes");
    CHECK_EQ(XhciVhubParseId(buf, 6, 1, &id, &enc), XHCI_VHUB_ID_DIGITS,
             "and a short string once the terminator is in reach");
}

static void testConfig(void)
{
    XHCI_VHUB_CONFIG c;
    UCHAR vid[XHCI_VHUB_ID_BUF_BYTES];
    UCHAR pid[XHCI_VHUB_ID_BUF_BYTES];
    ULONG v;

    idBuffer(vid, "1209", 1, 0xCC);
    idBuffer(pid, "0001", 1, 0xCC);

    /* Absent: 0 and not an error, and the ids are never asked for. */
    CHECK_EQ(XhciVhubConfigSwitch(&c, MP_STATUS_FAILURE, 7), 0,
             "absent: ids not wanted");
    CHECK_EQ(c.Applied, XHCI_VHUB_MODE_OFF, "absent applies 0");
    CHECK_EQ(c.Refused, XHCI_VHUB_WHY_NONE, "absent is not a refusal");
    CHECK_EQ(c.SwitchStatus, MP_STATUS_FAILURE, "the status is kept");
    CHECK_EQ(c.SwitchValue, 0, "and no value is recorded as read");
    CHECK_EQ(XhciVhubConfigIds(&c, MP_STATUS_SUCCESS, vid,
                               MP_STATUS_SUCCESS, pid, sizeof(vid)),
             XHCI_VHUB_MODE_OFF, "the ids cannot switch it on");
    CHECK_EQ(c.VidResult, XHCI_VHUB_ID_UNREAD, "the vendor id not consulted");
    CHECK_EQ(c.PidResult, XHCI_VHUB_ID_UNREAD, "the product id not consulted");
    CHECK_EQ(c.VidStatus, 0, "no id status recorded");

    /* 0 read: off, and the ids untouched. */
    CHECK_EQ(XhciVhubConfigSwitch(&c, MP_STATUS_SUCCESS, 0), 0, "0 read");
    CHECK_EQ(c.SwitchStatus, MP_STATUS_SUCCESS, "a 0 somebody set");
    CHECK_EQ(XhciVhubConfigIds(&c, MP_STATUS_SUCCESS, vid,
                               MP_STATUS_SUCCESS, pid, sizeof(vid)),
             XHCI_VHUB_MODE_OFF, "0 stays off");
    CHECK_EQ(c.VidResult, XHCI_VHUB_ID_UNREAD, "and never reads an id");

    /* Refused values: applied as 0, not clamped, and recorded. */
    for (v = 3; v < 6; v++) {
        CHECK_EQ(XhciVhubConfigSwitch(&c, MP_STATUS_SUCCESS, v), 0,
                 "a refused value wants no ids");
        CHECK_EQ(c.Applied, XHCI_VHUB_MODE_OFF, "applied as 0");
        CHECK_EQ(c.Refused, XHCI_VHUB_WHY_SWITCH, "recorded as refused");
        CHECK_EQ(c.SwitchValue, v, "with the value read");
        CHECK_EQ(XhciVhubConfigIds(&c, MP_STATUS_SUCCESS, vid,
                                   MP_STATUS_SUCCESS, pid, sizeof(vid)),
                 XHCI_VHUB_MODE_OFF, "and the ids cannot apply it");
        CHECK_EQ(c.VidResult, XHCI_VHUB_ID_UNREAD, "nor are they consulted");
        CHECK_EQ(c.Refused, XHCI_VHUB_WHY_SWITCH, "(the refusal stands)");
    }
    CHECK_EQ(XhciVhubConfigSwitch(&c, MP_STATUS_SUCCESS, 0xFFFFFFFFUL), 0,
             "all ones refused");
    CHECK_EQ(c.Refused, XHCI_VHUB_WHY_SWITCH, "not clamped to 2");

    /* 1 and 2 with both ids valid. */
    for (v = 1; v <= 2; v++) {
        CHECK_EQ(XhciVhubConfigSwitch(&c, MP_STATUS_SUCCESS, v), 1,
                 "1 or 2 wants the ids");
        CHECK_EQ(c.Applied, XHCI_VHUB_MODE_OFF, "nothing applied before them");
        CHECK_EQ(XhciVhubConfigIds(&c, MP_STATUS_SUCCESS, vid,
                                   MP_STATUS_SUCCESS, pid, sizeof(vid)), v,
                 "applied once both ids parse");
        CHECK_EQ(c.Applied, v, "recorded");
        CHECK_EQ(c.Vid, 0x1209, "the vendor id");
        CHECK_EQ(c.Pid, 0x0001, "the product id");
        CHECK_EQ(c.VidResult, XHCI_VHUB_ID_OK, "vendor ok");
        CHECK_EQ(c.PidResult, XHCI_VHUB_ID_OK, "product ok");
        CHECK_EQ(c.VidEncoding, XHCI_VHUB_ENC_UTF16, "encoding recorded");
        CHECK_EQ(c.Refused, XHCI_VHUB_WHY_NONE, "no refusal");
        CHECK_EQ(XhciVhubConfigIds(&c, MP_STATUS_SUCCESS, vid,
                                   MP_STATUS_SUCCESS, pid, sizeof(vid)), v,
                 "a second call changes nothing");
    }

    /* Each refusal applies 0 and records which id and why. */
    XhciVhubConfigSwitch(&c, MP_STATUS_SUCCESS, 1);
    CHECK_EQ(XhciVhubConfigIds(&c, MP_STATUS_FAILURE, vid,
                               MP_STATUS_SUCCESS, pid, sizeof(vid)),
             XHCI_VHUB_MODE_OFF, "vendor id missing");
    CHECK_EQ(c.Refused, XHCI_VHUB_WHY_VID, "named");
    CHECK_EQ(c.VidResult, XHCI_VHUB_ID_MISSING, "as missing");
    CHECK_EQ(c.VidStatus, MP_STATUS_FAILURE, "with the service's status");
    CHECK_EQ(c.PidResult, XHCI_VHUB_ID_OK, "the product id still parsed");
    CHECK_EQ(c.SwitchValue, 1, "the switch's read kept");
    CHECK_EQ(XhciVhubConfigIds(&c, MP_STATUS_SUCCESS, vid,
                               MP_STATUS_SUCCESS, pid, sizeof(vid)),
             XHCI_VHUB_MODE_OFF, "a second read after a refusal");
    CHECK_EQ(c.VidResult, XHCI_VHUB_ID_MISSING, "changes nothing");
    CHECK_EQ(c.Refused, XHCI_VHUB_WHY_VID, "(the refusal stands)");

    XhciVhubConfigSwitch(&c, MP_STATUS_SUCCESS, 2);
    idBuffer(pid, "00001", 0, 0);
    CHECK_EQ(XhciVhubConfigIds(&c, MP_STATUS_SUCCESS, vid,
                               MP_STATUS_SUCCESS, pid, sizeof(vid)),
             XHCI_VHUB_MODE_OFF, "product id too long");
    CHECK_EQ(c.Refused, XHCI_VHUB_WHY_PID, "the product id named");
    CHECK_EQ(c.PidResult, XHCI_VHUB_ID_DIGITS, "for its digit count");
    CHECK_EQ(c.PidEncoding, XHCI_VHUB_ENC_BYTE, "read single-byte");

    /* A UTF-16 value longer than the buffer: the service's one failure code,
     * whatever its bytes would have said. */
    XhciVhubConfigSwitch(&c, MP_STATUS_SUCCESS, 1);
    idBuffer(pid, "0001", 1, 0);
    CHECK_EQ(XhciVhubConfigIds(&c, MP_STATUS_SUCCESS, vid,
                               MP_STATUS_FAILURE, pid, sizeof(vid)),
             XHCI_VHUB_MODE_OFF, "a too-long product id");
    CHECK_EQ(c.PidResult, XHCI_VHUB_ID_MISSING, "reads as missing");

    XhciVhubConfigSwitch(&c, MP_STATUS_SUCCESS, 1);
    idBuffer(vid, "0000", 1, 0);
    idBuffer(pid, "zzzz", 1, 0);
    CHECK_EQ(XhciVhubConfigIds(&c, MP_STATUS_SUCCESS, vid,
                               MP_STATUS_SUCCESS, pid, sizeof(vid)),
             XHCI_VHUB_MODE_OFF, "both bad");
    CHECK_EQ(c.Refused, XHCI_VHUB_WHY_VID, "the vendor id is named first");
    CHECK_EQ(c.VidResult, XHCI_VHUB_ID_ZERO_VID, "for 0000");
    CHECK_EQ(c.PidResult, XHCI_VHUB_ID_CHAR, "and both are recorded");

    CHECK_EQ(XhciVhubConfigSwitch(NULL, MP_STATUS_SUCCESS, 1), 0, "NULL");
    CHECK_EQ(XhciVhubConfigIds(NULL, 0, vid, 0, pid, 14), XHCI_VHUB_MODE_OFF,
             "NULL");
}

/* ------------------------------------------------------------------ */
/* The request table (3.3)                                             */
/* ------------------------------------------------------------------ */

static const UCHAR wantDevice[18] = {
    0x12, 0x01, 0x00, 0x02, 0x09, 0x00, 0x01, 0x40,
    0x09, 0x12, 0x01, 0x00, 0x11, 0x01, 0x00, 0x01, 0x00, 0x01
};

static const UCHAR wantConfig[25] = {
    0x09, 0x02, 0x19, 0x00, 0x01, 0x01, 0x00, 0xC0, 0x00,
    0x09, 0x04, 0x00, 0x00, 0x01, 0x09, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x01, 0x00, 0x0C
};

static const UCHAR wantHub[9] = {
    0x09, 0x29, 0x01, 0x09, 0x00, 0x0A, 0x00, 0x00, 0xFF
};

static const UCHAR wantLangid[4] = { 0x04, 0x03, 0x09, 0x04 };

static const UCHAR wantProduct[44] = {
    0x2C, 0x03,
    'x', 0, 'h', 0, 'c', 0, 'i', 0, '9', 0, '8', 0, ' ', 0,
    'v', 0, 'i', 0, 'r', 0, 't', 0, 'u', 0, 'a', 0, 'l', 0, ' ', 0,
    'H', 0, 'S', 0, ' ', 0, 'h', 0, 'u', 0, 'b', 0
};

static ULONG ask(const XHCI_SETUP_PACKET *s, UCHAR *reply, ULONG *len,
                 ULONG *arg)
{
    XHCI_VHUB_IDENTITY id;

    id = identityOf(0x1209, 0x0001);
    fill(reply, XHCI_VHUB_REPLY_MAX, 0xEE);
    return XhciVhubRequest(&id, s, reply, len, arg);
}

/* A DATA answer of `want` truncated to wLength, and nothing past it written. */
static void checkData(UCHAR rt, UCHAR req, USHORT wValue, USHORT wIndex,
                      USHORT wLength, const UCHAR *want, ULONG wantLen,
                      const char *what)
{
    XHCI_SETUP_PACKET s;
    UCHAR reply[XHCI_VHUB_REPLY_MAX];
    ULONG len;
    ULONG arg;
    ULONG n;

    s = setupOf(rt, req, wValue, wIndex, wLength);
    check_eq_impl(ask(&s, reply, &len, &arg), XHCI_VHUB_REQ_DATA, what,
                  __FILE__, __LINE__);
    n = wantLen < wLength ? wantLen : wLength;
    check_eq_impl(len, n, what, __FILE__, __LINE__);
    check_impl(bytesEqual(reply, want, n), what, __FILE__, __LINE__);
    check_impl(n == XHCI_VHUB_REPLY_MAX || reply[n] == 0xEE,
               "nothing written past the answer", __FILE__, __LINE__);
}

static ULONG verdictOf(UCHAR rt, UCHAR req, USHORT wValue, USHORT wIndex,
                       USHORT wLength, ULONG *arg)
{
    XHCI_SETUP_PACKET s;
    UCHAR reply[XHCI_VHUB_REPLY_MAX];
    ULONG len;
    ULONG a;
    ULONG v;

    s = setupOf(rt, req, wValue, wIndex, wLength);
    v = ask(&s, reply, &len, &a);
    if (arg != NULL) {
        *arg = a;
    }
    check_eq_impl(len, 0, "a verdict carries no data", __FILE__, __LINE__);
    return v;
}

static void testDescriptors(void)
{
    static const USHORT lengths[] = { 8, 18, 64, 255, 2, 0 };
    ULONG i;

    /* GET_DESCRIPTOR(Device) at every length: see the file header. */
    for (i = 0; i < 6; i++) {
        checkData(0x80, 6, 0x0100, 0, lengths[i], wantDevice, 18,
                  "device descriptor truncated to wLength");
    }
    checkData(0x80, 6, 0x0200, 0, 9, wantConfig, 25,
              "configuration header alone");
    checkData(0x80, 6, 0x0200, 0, 255, wantConfig, 25,
              "whole configuration");
    checkData(0x80, 6, 0x0200, 0, 25, wantConfig, 25, "exactly 25");

    /* GET_DESCRIPTOR(Hub) as measured - wValue 0, wLength 71 - and as
     * specified, 0x2900. */
    checkData(0xA0, 6, 0x0000, 0, 71, wantHub, 9, "hub descriptor, measured");
    checkData(0xA0, 6, 0x2900, 0, 71, wantHub, 9, "hub descriptor, 0x2900");
    checkData(0xA0, 6, 0x0000, 0, 7, wantHub, 9, "hub descriptor short");

    /* Strings: the language table and index 1, each truncated, whatever
     * LANGID wIndex names. */
    checkData(0x80, 6, 0x0300, 0, 255, wantLangid, 4, "language table");
    checkData(0x80, 6, 0x0300, 0, 2, wantLangid, 4, "language table, 2");
    checkData(0x80, 6, 0x0301, 0x0409, 255, wantProduct, 44, "product");
    checkData(0x80, 6, 0x0301, 0x0409, 2, wantProduct, 44, "product, 2");
    checkData(0x80, 6, 0x0301, 0x0407, 44, wantProduct, 44,
              "product under another LANGID");
    checkData(0x80, 6, 0x0301, 0, 43, wantProduct, 44, "product, 43");
    CHECK_EQ(verdictOf(0x80, 6, 0x0302, 0x0409, 255, NULL),
             XHCI_VHUB_REQ_STALL, "string index 2 stalls");
    CHECK_EQ(verdictOf(0x80, 6, 0x03EE, 0x0409, 255, NULL),
             XHCI_VHUB_REQ_STALL, "string index 0xEE (a Microsoft OS query) "
                                  "stalls");
    CHECK_EQ(verdictOf(0x80, 6, 0x0101, 0, 18, NULL), XHCI_VHUB_REQ_STALL,
             "device descriptor index 1");
    CHECK_EQ(verdictOf(0x80, 6, 0x0201, 0, 9, NULL), XHCI_VHUB_REQ_STALL,
             "configuration index 1");
    CHECK_EQ(verdictOf(0x80, 6, 0x0600, 0, 10, NULL), XHCI_VHUB_REQ_STALL,
             "device qualifier stalls");
    CHECK_EQ(verdictOf(0x80, 6, 0x0700, 0, 255, NULL), XHCI_VHUB_REQ_STALL,
             "other-speed configuration stalls");
    CHECK_EQ(verdictOf(0xA0, 6, 0x2901, 0, 71, NULL), XHCI_VHUB_REQ_STALL,
             "hub descriptor index 1");

    /* The ids and the version reach the device descriptor. */
    {
        XHCI_VHUB_IDENTITY id;
        XHCI_SETUP_PACKET s;
        UCHAR reply[XHCI_VHUB_REPLY_MAX];
        ULONG len;
        ULONG arg;

        id = identityOf(0xABCD, 0x1234);
        id.BcdDevice = 0x0200;
        s = setupOf(0x80, 6, 0x0100, 0, 18);
        CHECK_EQ(XhciVhubRequest(&id, &s, reply, &len, &arg),
                 XHCI_VHUB_REQ_DATA, "(device descriptor)");
        CHECK_EQ(reply[8] | (reply[9] << 8), 0xABCD, "idVendor");
        CHECK_EQ(reply[10] | (reply[11] << 8), 0x1234, "idProduct");
        CHECK_EQ(reply[12] | (reply[13] << 8), 0x0200, "bcdDevice");
        CHECK_EQ(reply[16], 0, "never a serial number");

        id.PowerOnToPowerGood = 50;
        s = setupOf(0xA0, 6, 0, 0, 71);
        XhciVhubRequest(&id, &s, reply, &len, &arg);
        CHECK_EQ(reply[5], 50, "bPwrOn2PwrGood is the caller's");
        CHECK_EQ(len, 9, "and the ids are in no other answer");
    }
}

static void testStatusRequests(void)
{
    static const UCHAR selfPowered[2] = { 0x01, 0x00 };
    static const UCHAR zero4[4] = { 0, 0, 0, 0 };
    UCHAR reply[4];

    checkData(0x80, 0, 0, 0, 2, selfPowered, 2, "GET_STATUS(Device)");
    checkData(0x80, 0, 0, 0, 1, selfPowered, 2, "GET_STATUS(Device), 1");
    checkData(0x80, 0, 0, 0, 0, selfPowered, 2, "GET_STATUS(Device), 0");
    checkData(0x80, 0, 0, 0, 64, selfPowered, 2, "GET_STATUS(Device), 64");
    checkData(0x81, 0, 0, 0, 2, zero4, 2, "GET_STATUS(Interface 0)");
    checkData(0x82, 0, 0, 0x00, 2, zero4, 2, "GET_STATUS(EP0)");
    checkData(0x82, 0, 0, 0x81, 2, zero4, 2, "GET_STATUS(EP 0x81)");
    checkData(0x82, 0, 0, 0x81, 1, zero4, 2, "GET_STATUS(EP 0x81), 1");
    CHECK_EQ(verdictOf(0x81, 0, 0, 1, 2, NULL), XHCI_VHUB_REQ_STALL,
             "GET_STATUS(Interface 1)");
    CHECK_EQ(verdictOf(0x82, 0, 0, 0x02, 2, NULL), XHCI_VHUB_REQ_STALL,
             "GET_STATUS(EP 0x02)");
    checkData(0xA0, 0, 0, 0, 4, zero4, 4, "GET_HUB_STATUS");
    checkData(0xA0, 0, 0, 0, 2, zero4, 4, "GET_HUB_STATUS, 2");

    /* GET_PORT_STATUS(1) is the port's; the bytes are XhciVhubStatusBytes'. */
    CHECK_EQ(verdictOf(0xA3, 0, 0, 1, 4, NULL), XHCI_VHUB_REQ_PORT_STATUS,
             "GET_PORT_STATUS(1)");
    CHECK_EQ(verdictOf(0xA3, 0, 0, 2, 4, NULL), XHCI_VHUB_REQ_STALL,
             "GET_PORT_STATUS(2) on a one-port hub");
    CHECK_EQ(verdictOf(0xA3, 0, 0, 0, 4, NULL), XHCI_VHUB_REQ_STALL,
             "GET_PORT_STATUS(0)");
    fill(reply, 4, 0xEE);
    CHECK_EQ(XhciVhubStatusBytes(0x0503, 0x0011, 4, reply), 4, "4 bytes");
    CHECK_EQ(reply[0], 0x03, "wPortStatus low");
    CHECK_EQ(reply[1], 0x05, "wPortStatus high");
    CHECK_EQ(reply[2], 0x11, "wPortChange low");
    CHECK_EQ(reply[3], 0x00, "wPortChange high");
    fill(reply, 4, 0xEE);
    CHECK_EQ(XhciVhubStatusBytes(0x0503, 0x0011, 2, reply), 2, "truncated");
    CHECK_EQ(reply[2], 0xEE, "nothing past wLength");
}

static void testStandardAndClassVerdicts(void)
{
    ULONG arg;
    ULONG sel;

    CHECK_EQ(verdictOf(0x00, 5, 7, 0, 0, &arg), XHCI_VHUB_REQ_SET_ADDRESS,
             "SET_ADDRESS");
    CHECK_EQ(arg, 7, "with its address");
    CHECK_EQ(verdictOf(0x00, 5, 127, 0, 0, &arg), XHCI_VHUB_REQ_SET_ADDRESS,
             "address 127");
    CHECK_EQ(verdictOf(0x00, 5, 0, 0, 0, &arg), XHCI_VHUB_REQ_STALL,
             "address 0");
    CHECK_EQ(verdictOf(0x00, 5, 128, 0, 0, &arg), XHCI_VHUB_REQ_STALL,
             "address 128");
    CHECK_EQ(verdictOf(0x00, 9, 1, 0, 0, &arg), XHCI_VHUB_REQ_SET_CONFIG,
             "SET_CONFIGURATION(1)");
    CHECK_EQ(arg, 1, "value 1");
    CHECK_EQ(verdictOf(0x00, 9, 0, 0, 0, &arg), XHCI_VHUB_REQ_SET_CONFIG,
             "SET_CONFIGURATION(0)");
    CHECK_EQ(arg, 0, "value 0");
    CHECK_EQ(verdictOf(0x00, 9, 2, 0, 0, &arg), XHCI_VHUB_REQ_STALL,
             "SET_CONFIGURATION(2)");
    CHECK_EQ(verdictOf(0x01, 11, 0, 0, 0, &arg), XHCI_VHUB_REQ_OK,
             "SET_INTERFACE(alt 0)");
    CHECK_EQ(verdictOf(0x01, 11, 1, 0, 0, &arg), XHCI_VHUB_REQ_STALL,
             "SET_INTERFACE(alt 1): a single-TT hub has none");
    CHECK_EQ(verdictOf(0x02, 1, 0, 0x81, 0, &arg), XHCI_VHUB_REQ_OK,
             "CLEAR_FEATURE(ENDPOINT_HALT) on 0x81");
    CHECK_EQ(verdictOf(0x02, 1, 0, 0x00, 0, &arg), XHCI_VHUB_REQ_OK,
             "CLEAR_FEATURE(ENDPOINT_HALT) on EP0");
    CHECK_EQ(verdictOf(0x02, 1, 0, 0x01, 0, &arg), XHCI_VHUB_REQ_STALL,
             "an endpoint that does not exist");

    /* The TT requests succeed: no translator on a root port to clear. */
    CHECK_EQ(verdictOf(0x23, 8, 0, 1, 0, &arg), XHCI_VHUB_REQ_OK,
             "CLEAR_TT_BUFFER");
    CHECK_EQ(verdictOf(0x23, 9, 0, 1, 0, &arg), XHCI_VHUB_REQ_OK, "RESET_TT");
    CHECK_EQ(verdictOf(0x23, 11, 0, 1, 0, &arg), XHCI_VHUB_REQ_OK, "STOP_TT");
    CHECK_EQ(verdictOf(0xA3, 10, 0, 1, 0, &arg), XHCI_VHUB_REQ_OK,
             "GET_TT_STATE");

    /* Port features, measured shapes: the selector in wValue, the port in
     * the low byte of wIndex. */
    CHECK_EQ(verdictOf(0x23, 3, 8, 1, 0, &arg), XHCI_VHUB_REQ_PORT_SET,
             "SET_FEATURE(PORT_POWER), as measured at hub start");
    CHECK_EQ(arg, 8, "selector 8");
    CHECK_EQ(verdictOf(0x23, 3, 4, 1, 0, &arg), XHCI_VHUB_REQ_PORT_SET,
             "SET_FEATURE(PORT_RESET)");
    CHECK_EQ(arg, 4, "selector 4");
    CHECK_EQ(verdictOf(0x23, 3, 2, 1, 0, &arg), XHCI_VHUB_REQ_PORT_SET,
             "SET_FEATURE(PORT_SUSPEND)");
    CHECK_EQ(verdictOf(0x23, 3, 1, 1, 0, &arg), XHCI_VHUB_REQ_STALL,
             "SET_FEATURE(PORT_ENABLE) is not a hub request");
    CHECK_EQ(verdictOf(0x23, 3, 21, 0x0101, 0, &arg), XHCI_VHUB_REQ_STALL,
             "PORT_TEST");
    CHECK_EQ(verdictOf(0x23, 3, 22, 0x0101, 0, &arg), XHCI_VHUB_REQ_STALL,
             "PORT_INDICATOR");
    CHECK_EQ(verdictOf(0x23, 3, 8, 2, 0, &arg), XHCI_VHUB_REQ_STALL,
             "port 2");
    for (sel = 16; sel <= 20; sel++) {
        CHECK_EQ(verdictOf(0x23, 1, (USHORT)sel, 1, 0, &arg),
                 XHCI_VHUB_REQ_PORT_CLEAR, "CLEAR_FEATURE(C_PORT_*)");
        CHECK_EQ(arg, sel, "the change selector");
    }
    CHECK_EQ(verdictOf(0x23, 1, 1, 1, 0, &arg), XHCI_VHUB_REQ_PORT_CLEAR,
             "CLEAR_FEATURE(PORT_ENABLE)");
    CHECK_EQ(verdictOf(0x23, 1, 2, 1, 0, &arg), XHCI_VHUB_REQ_PORT_CLEAR,
             "CLEAR_FEATURE(PORT_SUSPEND)");
    CHECK_EQ(verdictOf(0x23, 1, 8, 1, 0, &arg), XHCI_VHUB_REQ_PORT_CLEAR,
             "CLEAR_FEATURE(PORT_POWER)");
    CHECK_EQ(verdictOf(0x23, 1, 4, 1, 0, &arg), XHCI_VHUB_REQ_STALL,
             "CLEAR_FEATURE(PORT_RESET) does not exist");
    CHECK_EQ(verdictOf(0x23, 1, 21, 1, 0, &arg), XHCI_VHUB_REQ_STALL,
             "selector 21");

    /* Anything else stalls. */
    CHECK_EQ(verdictOf(0x80, 8, 0, 0, 1, &arg), XHCI_VHUB_REQ_STALL,
             "GET_CONFIGURATION is not in the table");
    CHECK_EQ(verdictOf(0x20, 1, 0, 0, 0, &arg), XHCI_VHUB_REQ_STALL,
             "CLEAR_HUB_FEATURE");
    CHECK_EQ(verdictOf(0x21, 0x0A, 0, 0, 0, &arg), XHCI_VHUB_REQ_STALL,
             "a class request to an interface (HID SET_IDLE's shape)");
    CHECK_EQ(verdictOf(0xC0, 0x01, 0, 0, 4, &arg), XHCI_VHUB_REQ_STALL,
             "a vendor request");

    /*
     * The corners of the table's own rows. A port request names port 1 or
     * nothing; the device feature requests are not in the table, and the
     * configuration descriptor says no remote wakeup, so usbhub has no
     * reason to send one; the one interface has no GET_INTERFACE row and
     * SET_INTERFACE reaches only interface 0.
     */
    CHECK_EQ(verdictOf(0x23, 1, 16, 2, 0, &arg), XHCI_VHUB_REQ_STALL,
             "CLEAR_FEATURE(C_PORT_CONNECTION) on port 2");
    CHECK_EQ(verdictOf(0x23, 1, 1, 0, 0, &arg), XHCI_VHUB_REQ_STALL,
             "CLEAR_FEATURE(PORT_ENABLE) on port 0");
    CHECK_EQ(verdictOf(0x00, 3, 1, 0, 0, &arg), XHCI_VHUB_REQ_STALL,
             "SET_FEATURE(DEVICE_REMOTE_WAKEUP)");
    CHECK_EQ(verdictOf(0x00, 1, 1, 0, 0, &arg), XHCI_VHUB_REQ_STALL,
             "CLEAR_FEATURE(DEVICE_REMOTE_WAKEUP)");
    CHECK_EQ(verdictOf(0x81, 10, 0, 0, 1, &arg), XHCI_VHUB_REQ_STALL,
             "GET_INTERFACE is not in the table");
    CHECK_EQ(verdictOf(0x01, 11, 0, 1, 0, &arg), XHCI_VHUB_REQ_STALL,
             "SET_INTERFACE on interface 1");

    {
        XHCI_VHUB_IDENTITY id;
        XHCI_SETUP_PACKET s;
        UCHAR reply[XHCI_VHUB_REPLY_MAX];
        ULONG len;

        id = identityOf(1, 1);
        s = setupOf(0x80, 6, 0x0100, 0, 18);
        CHECK_EQ(XhciVhubRequest(NULL, &s, reply, &len, &arg),
                 XHCI_VHUB_REQ_STALL, "NULL identity");
        CHECK_EQ(XhciVhubRequest(&id, NULL, reply, &len, &arg),
                 XHCI_VHUB_REQ_STALL, "NULL setup");
        CHECK_EQ(XhciVhubRequest(&id, &s, NULL, &len, &arg),
                 XHCI_VHUB_REQ_STALL, "NULL reply");
        CHECK_EQ(XhciVhubRequest(&id, &s, reply, NULL, &arg),
                 XHCI_VHUB_REQ_STALL, "NULL reply length");
        CHECK_EQ(XhciVhubRequest(&id, &s, reply, &len, NULL),
                 XHCI_VHUB_REQ_STALL, "NULL arg");
        CHECK_EQ(len, 0, "a refusal leaves no length");

        /* A descriptor asked for at wLength 0 is answered with nothing,
         * as a real device's empty data stage is: not a stall. */
        s = setupOf(0xA0, 6, 0, 0, 0);
        fill(reply, sizeof(reply), 0xEE);
        CHECK_EQ(XhciVhubRequest(&id, &s, reply, &len, &arg),
                 XHCI_VHUB_REQ_DATA, "GET_DESCRIPTOR(Hub) at wLength 0");
        CHECK_EQ(len, 0, "answers zero bytes");
        CHECK_EQ(reply[0], 0xEE, "and writes none");
    }
}

/* ------------------------------------------------------------------ */
/* The per-port state                                                  */
/* ------------------------------------------------------------------ */

/*
 * Physical status words as XhciPortShadowReport gives them today: a
 * connected root port always carries the High-Speed override, whatever the
 * device decoded.
 */
#define PS_EMPTY    (XHCI_HUB_PORT_POWER)
#define PS_CONN     (XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_CONNECTION | \
                     XHCI_HUB_PORT_HIGH_SPEED)
#define PS_EN       (PS_CONN | XHCI_HUB_PORT_ENABLE)
#define PS_RESETTING (PS_CONN | XHCI_HUB_PORT_RESET)
#define PS_SUSP     (PS_EN | XHCI_HUB_PORT_SUSPEND)

#define ON_DEMAND   XHCI_VHUB_MODE_ON_DEMAND
#define ALWAYS      XHCI_VHUB_MODE_ALWAYS

static ULONG gen;

static int recordIsZero(const XHCI_VHUB *h)
{
    const UCHAR *p;
    ULONG i;

    p = (const UCHAR *)h;
    for (i = 0; i < sizeof(*h); i++) {
        if (p[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void rootReport(const XHCI_VHUB *h, ULONG applied, ULONG todayStatus,
                       ULONG todayChange, ULONG *s, ULONG *c)
{
    XhciVhubRootReport(h, applied, todayStatus, todayChange, s, c);
}

static ULONG p1Status(const XHCI_VHUB *h, ULONG phys, ULONG speed)
{
    ULONG s;
    ULONG c;

    XhciVhubPort1Report(h, phys, speed, &s, &c);
    return s;
}

static ULONG p1Change(const XHCI_VHUB *h)
{
    ULONG s;
    ULONG c;

    XhciVhubPort1Report(h, PS_EN, XHCI_SPEED_FULL, &s, &c);
    return c;
}

/*
 * Value 1: a slower device on the root port, its first reset decoding
 * `speed`, which stands the hub up. Returns the done verdict.
 */
static ULONG v1Plug(PXHCI_VHUB h, ULONG speed)
{
    XhciVhubStart(h, ON_DEMAND, 1);
    gen++;
    CHECK_EQ(XhciVhubRootReset(h, ON_DEMAND, gen, 0, PS_CONN),
             XHCI_VHUB_DO_PHYS_RESET, "(the first reset is today's)");
    return XhciVhubResetDone(h, ON_DEMAND, gen, 0, speed, PS_EN);
}

/*
 * usbhub's enumeration of the hub itself, as design record 02 measured it for
 * any device: reset (done by the caller), address-0 open, GET_DESCRIPTOR,
 * reset again, SET_ADDRESS through the pipe it already opened, then
 * SET_CONFIGURATION.
 */
static void enumerateHub(PXHCI_VHUB h, ULONG applied, ULONG address)
{
    ULONG v;

    CHECK_EQ(XhciVhubClaimOpen(h), 1, "(the hub's address-0 open)");
    gen++;
    v = XhciVhubRootReset(h, applied, gen, 0, PS_EN);
    if (applied == ON_DEMAND) {
        CHECK_EQ(v, XHCI_VHUB_DO_PHYS_RESET, "(second reset, physical at 1)");
        v = XhciVhubResetDone(h, applied, gen, 0, XHCI_SPEED_FULL, PS_EN);
    }
    CHECK_EQ(v & XHCI_VHUB_DO_ARM_HUB, 0, "the second reset arms nothing");
    CHECK_EQ(XhciVhubClaimOpen(h), 0, "and leaves no claim armed");
    CHECK_EQ(h->Ep0Bound, 1, "the binding kept");
    XhciVhubSetAddress(h, address);
    XhciVhubSetConfig(h, 1);
    CHECK_EQ(h->DevState, XHCI_VHUB_DEV_CONFIGURED, "(configured)");
}

/* A port-1 reset that completes on an enabled port. */
static void resetPort1(PXHCI_VHUB h, ULONG applied)
{
    gen++;
    CHECK_EQ(XhciVhubPort1Feature(h, 1, XHCI_VHUB_SEL_PORT_RESET, gen, 0,
                                  PS_CONN),
             XHCI_VHUB_DO_PHYS_RESET | XHCI_VHUB_DO_ARM_DEVICE,
             "(port-1 reset)");
    CHECK_EQ(XhciVhubResetDone(h, applied, gen, 0, XHCI_SPEED_FULL, PS_EN),
             XHCI_VHUB_DO_PIPE, "(port-1 reset done)");
    XhciVhubPort1Feature(h, 0, XHCI_VHUB_SEL_C_PORT_RESET, 0, 0, PS_EN);
    XhciVhubPort1Feature(h, 0, XHCI_VHUB_SEL_C_PORT_CONNECTION, 0, 0, PS_EN);
}

/*
 * **Rule 2**: with the switch applied as 0 nothing is created, every report
 * is today's bit for bit, and every reset is today's.
 */
static void testOffState(void)
{
    static const ULONG statuses[] = { 0, PS_EMPTY, PS_CONN, PS_EN, PS_SUSP,
                                      PS_RESETTING, 0x071F };
    XHCI_VHUB h;
    ULONG i;
    ULONG s;
    ULONG c;
    ULONG strip;
    UCHAR reply[4];

    CHECK_EQ(XhciVhubStart(&h, XHCI_VHUB_MODE_OFF, 1), XHCI_VHUB_DO_NONE,
             "start at 0");
    CHECK(recordIsZero(&h), "no record at 0");
    for (i = 0; i < 7; i++) {
        rootReport(&h, XHCI_VHUB_MODE_OFF, statuses[i], 0x1F, &s, &c);
        CHECK_EQ(s, statuses[i], "today's status, bit for bit");
        CHECK_EQ(c, 0x1F, "today's changes, bit for bit");
        rootReport(NULL, XHCI_VHUB_MODE_OFF, statuses[i], 0x05, &s, &c);
        CHECK_EQ(s, statuses[i], "(and with no record)");
    }
    CHECK_EQ(XhciVhubRootReset(&h, XHCI_VHUB_MODE_OFF, 1, 1, PS_EN),
             XHCI_VHUB_DO_PHYS_RESET, "today's reset, unheld");
    CHECK_EQ(XhciVhubResetDone(&h, XHCI_VHUB_MODE_OFF, 1, 0,
                               XHCI_SPEED_FULL, PS_EN),
             XHCI_VHUB_DO_NONE, "no decision at 0");
    CHECK_EQ(XhciVhubAbsorb(&h, XHCI_VHUB_MODE_OFF, 0x1F, PS_EN, &strip),
             XHCI_VHUB_DO_NONE, "absorbs nothing");
    CHECK_EQ(strip, 0, "strips nothing from the shadow");
    CHECK_EQ(XhciVhubRootDisable(&h, XHCI_VHUB_MODE_OFF, PS_EN),
             XHCI_VHUB_DO_NONE, "disable is today's");
    CHECK_EQ(XhciVhubRootPower(&h, XHCI_VHUB_MODE_OFF, 0, PS_EN),
             XHCI_VHUB_DO_NONE, "power is today's");
    CHECK_EQ(XhciVhubRootSuspend(&h, XHCI_VHUB_MODE_OFF, 1, PS_EN),
             XHCI_VHUB_DO_NONE, "suspend is today's");
    CHECK_EQ(XhciVhubDisownCollected(&h, XHCI_VHUB_MODE_OFF, 3),
             XHCI_VHUB_DO_NONE, "nothing held");
    CHECK_EQ(XhciVhubReinit(&h, XHCI_VHUB_MODE_OFF, 1, 0), XHCI_VHUB_DO_NONE,
             "reinit is today's");
    XhciVhubRootClearChange(&h, XHCI_VHUB_MODE_OFF, 0x1F);
    CHECK_EQ(XhciVhubClaimOpen(&h), 0, "no address-0 open is the hub's");
    CHECK_EQ(XhciVhubPipeByte(&h), 0, "no pipe");
    CHECK_EQ(XhciVhubFindAddress(&h, 1, 1), 0, "no virtual address");
    CHECK(recordIsZero(&h), "and the record is still all zero");
    fill(reply, 4, 0xEE);
    CHECK_EQ(XhciVhubStatusBytes(0, 0, 4, reply), 4, "(bytes)");
}

/* Value 1's decision (3.2), taken at every root-port reset's end. */
static void testDecision(void)
{
    XHCI_VHUB h;
    ULONG s;
    ULONG c;
    ULONG v;

    /* Full and Low Speed stand a hub up; High Speed and the rest do not. */
    v = v1Plug(&h, XHCI_SPEED_FULL);
    CHECK_EQ(v, XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_ARM_HUB,
             "a Full-Speed device gets a hub");
    CHECK_EQ(h.Present, 1, "(present)");
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_HUB, "virtual-hub mode");
    CHECK_EQ(h.DevState, XHCI_VHUB_DEV_DEFAULT, "the hub in Default");
    CHECK_EQ(h.P1Changes, XHCI_HUB_C_PORT_CONNECTION,
             "port 1's connect latched once at creation");
    rootReport(&h, ON_DEMAND, PS_EN, XHCI_HUB_C_PORT_RESET, &s, &c);
    CHECK_EQ(s, PS_EN, "the root port reads as today after its reset");
    CHECK_EQ(c, XHCI_HUB_C_PORT_RESET, "its C_PORT_RESET the upstream's");

    v = v1Plug(&h, XHCI_SPEED_LOW);
    CHECK_EQ(v, XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_ARM_HUB,
             "a Low-Speed device gets a hub");

    v = v1Plug(&h, XHCI_SPEED_HIGH);
    CHECK_EQ(v, XHCI_VHUB_DO_NONE, "High Speed stays direct");
    CHECK_EQ(h.Present, 0, "no hub");
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_DIRECT, "direct");
    rootReport(&h, ON_DEMAND, PS_EN, XHCI_HUB_C_PORT_RESET, &s, &c);
    CHECK_EQ(s, PS_EN, "a direct port's report is today's");
    CHECK_EQ(c, XHCI_HUB_C_PORT_RESET, "bit for bit");

    v = v1Plug(&h, XHCI_SPEED_SUPER);
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_DIRECT, "a SuperSpeed decode");
    v = v1Plug(&h, XHCI_SPEED_UNKNOWN);
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_DIRECT, "no decode");

    /* The timeout path has decoded nothing, and stays direct. */
    XhciVhubStart(&h, ON_DEMAND, 1);
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_CONN);
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 1, XHCI_SPEED_FULL, PS_CONN),
             XHCI_VHUB_DO_NONE, "a timed-out reset");
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_DIRECT, "is direct");
    CHECK_EQ(h.Present, 0, "with no hub");

    /* A completion under another generation is not this record's. */
    XhciVhubStart(&h, ON_DEMAND, 1);
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_CONN);
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen + 100, 0, XHCI_SPEED_FULL,
                               PS_EN),
             XHCI_VHUB_DO_NONE, "a stale generation");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_ROOT, "leaves the reset owned");
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_FULL, PS_EN),
             XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_ARM_HUB,
             "and its own completion decides");
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_FULL, PS_EN),
             XHCI_VHUB_DO_NONE, "once");
}

/* The decision re-taken at every root-port reset, and its forced connect. */
static void testRedecision(void)
{
    XHCI_VHUB h;
    ULONG v;
    ULONG strip;

    /* The same slower device on a re-enumeration: the hub re-created. */
    v1Plug(&h, XHCI_SPEED_FULL);
    enumerateHub(&h, ON_DEMAND, 5);
    gen++;
    CHECK_EQ(XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_EN),
             XHCI_VHUB_DO_PHYS_RESET, "a re-enumeration reset");
    v = XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_FULL, PS_EN);
    CHECK_EQ(v, XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_ARM_HUB,
             "re-creates the hub from address 0");
    CHECK_EQ(h.Address, 0, "(address 0)");
    CHECK_EQ(h.DevState, XHCI_VHUB_DEV_DEFAULT, "(Default)");
    CHECK_EQ(h.P1Changes, XHCI_HUB_C_PORT_CONNECTION,
             "the device a connect change on port 1 again");

    /* A High-Speed device where the slower one was (a swap across a
     * resume): the forced connect change, and the hub dropped. */
    enumerateHub(&h, ON_DEMAND, 5);
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_EN);
    v = XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_HIGH, PS_EN);
    CHECK_EQ(v, XHCI_VHUB_DO_FORCE_CONNECT | XHCI_VHUB_DO_DROP,
             "hub to direct forces a connect change");
    CHECK(recordIsZero(&h), "and nothing is decided until the next reset");
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_CONN);
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_HIGH, PS_EN),
             XHCI_VHUB_DO_NONE, "which decides afresh: direct");

    /* The reverse: direct, then a slower device found at a later reset. */
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_CONN);
    v = XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_LOW, PS_EN);
    CHECK_EQ(v, XHCI_VHUB_DO_FORCE_CONNECT, "direct to hub forces it too");
    CHECK_EQ(h.Present, 0, "with no hub yet");
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_CONN);
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_LOW, PS_EN),
             XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_ARM_HUB,
             "the next reset stands the hub up");

    /* A timeout on a port in virtual-hub mode is a flip (24.3.2 amendment). */
    enumerateHub(&h, ON_DEMAND, 6);
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_EN);
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 1, XHCI_SPEED_UNKNOWN,
                               PS_CONN),
             XHCI_VHUB_DO_FORCE_CONNECT | XHCI_VHUB_DO_DROP,
             "a timed-out re-decision tears the hub down");

    /*
     * A disconnect retires the hub with its device (task 24.3.4: an NT 6.x
     * usbhub removes it without the disable 3.2 waited for), forgets the
     * decision, and the next device decides afresh with no forced change.
     */
    v1Plug(&h, XHCI_SPEED_FULL);
    CHECK_EQ(XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_CONNECTION,
                            PS_EMPTY, &strip),
             XHCI_VHUB_DO_DROP, "an unplug retires the hub");
    CHECK_EQ(strip, 0, "and the root port reports the whole reading");
    CHECK(recordIsZero(&h), "(gone, its address and decision with it)");
    CHECK_EQ(XhciVhubRootDisable(&h, ON_DEMAND, PS_EMPTY), XHCI_VHUB_DO_NONE,
             "a disable that follows finds no hub");
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_CONN);
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_HIGH, PS_EN),
             XHCI_VHUB_DO_NONE, "a High-Speed device after it: direct");

    /*
     * The same rule on a direct port, which holds nothing but its decision:
     * a High-Speed device unplugged and a slower one plugged into the same
     * port is the ordinary swap, not a flip, so no connect change is forced
     * and the first reset stands the hub up. (Until this vector the decision
     * survived the unplug and the swap cost usbhub a forced re-enumeration.)
     */
    v1Plug(&h, XHCI_SPEED_HIGH);
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_DIRECT, "(direct, no hub)");
    CHECK_EQ(XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_CONNECTION,
                            PS_EMPTY, &strip),
             XHCI_VHUB_DO_NONE, "an unplug on a direct port");
    CHECK_EQ(strip, 0, "strips nothing");
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_NONE, "and forgets the decision");
    CHECK_EQ(XhciVhubRootDisable(&h, ON_DEMAND, PS_EMPTY), XHCI_VHUB_DO_NONE,
             "usbport's disable of a direct port is today's alone");
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_CONN);
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_FULL, PS_EN),
             XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_ARM_HUB,
             "a Full-Speed device next gets its hub at the first reset");

    /* A connect change with the port still connected is a device that may
     * have been swapped between two readings: forgotten on the same terms. */
    v1Plug(&h, XHCI_SPEED_HIGH);
    CHECK_EQ(XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_CONNECTION,
                            PS_CONN, &strip),
             XHCI_VHUB_DO_NONE, "a connect change, still connected");
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_NONE, "forgets it too");
    v1Plug(&h, XHCI_SPEED_HIGH);
    CHECK_EQ(XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_OVER_CURRENT,
                            PS_EN, &strip),
             XHCI_VHUB_DO_NONE, "any other change on a direct port");
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_DIRECT, "keeps the decision");

    /* usbport's disable and power-off of a direct port forget it as well; a
     * power-on changes nothing at 1; and with a hub, a power-off is the drop
     * a disable is. */
    v1Plug(&h, XHCI_SPEED_HIGH);
    CHECK_EQ(XhciVhubRootDisable(&h, ON_DEMAND, PS_EN), XHCI_VHUB_DO_NONE,
             "a direct port's disable");
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_NONE, "forgets the decision");
    v1Plug(&h, XHCI_SPEED_HIGH);
    CHECK_EQ(XhciVhubRootPower(&h, ON_DEMAND, 0, PS_EN), XHCI_VHUB_DO_NONE,
             "a direct port's power-off");
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_NONE, "forgets the decision");
    v1Plug(&h, XHCI_SPEED_HIGH);
    CHECK_EQ(XhciVhubRootPower(&h, ON_DEMAND, 1, PS_EN), XHCI_VHUB_DO_NONE,
             "a power-on at 1 is today's alone");
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_DIRECT, "and forgets nothing");
    v1Plug(&h, XHCI_SPEED_FULL);
    CHECK_EQ(XhciVhubRootPower(&h, ON_DEMAND, 0, PS_EN), XHCI_VHUB_DO_DROP,
             "with a hub, a power-off drops it");
    CHECK(recordIsZero(&h), "(gone)");
    v1Plug(&h, XHCI_SPEED_FULL);
    CHECK_EQ(XhciVhubRootPower(&h, ON_DEMAND, 1, PS_EN), XHCI_VHUB_DO_NONE,
             "and a power-on leaves it");
    CHECK_EQ(h.Present, 1, "(still there)");
}

/*
 * The hub's own two-reset enumeration (3.6), at both values, with the bound
 * of one suppressed reset per claim.
 */
static void testHubEnumeration(void)
{
    XHCI_VHUB h;
    ULONG v;

    v1Plug(&h, XHCI_SPEED_FULL);
    enumerateHub(&h, ON_DEMAND, 9);
    CHECK_EQ(h.Address, 9, "SET_ADDRESS recorded");
    CHECK_EQ(XhciVhubFindAddress(&h, 1, 9), 1, "and found by address");

    /* Abandoned mid-enumeration: EP0 open, no address, the second reset
     * spent. A third reset is not suppressed. */
    v1Plug(&h, XHCI_SPEED_FULL);
    CHECK_EQ(XhciVhubClaimOpen(&h), 1, "(open)");
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_EN);
    v = XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_FULL, PS_EN);
    CHECK_EQ(v & XHCI_VHUB_DO_ARM_HUB, 0, "(suppressed)");
    gen++;
    XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_EN);
    v = XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_FULL, PS_EN);
    CHECK_EQ(v & XHCI_VHUB_DO_ARM_HUB, XHCI_VHUB_DO_ARM_HUB,
             "a third reset arms again: the bound is one per claim");
    CHECK_EQ(XhciVhubClaimOpen(&h), 1, "a fresh open");

    /* Value 2: the same bracket, synthetic resets. */
    XhciVhubStart(&h, ALWAYS, 0);
    gen++;
    v = XhciVhubRootReset(&h, ALWAYS, gen, 0, PS_EMPTY);
    CHECK_EQ(v, XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_ARM_HUB |
                    XHCI_VHUB_DO_DISABLE,
             "the first reset arms the hub's open, and asks for the disable "
             "body whether or not the port reads enabled - its software "
             "half is what gives a child's address back");
    enumerateHub(&h, ALWAYS, 3);

    /* EP0 closed and re-opened across a re-enumeration. */
    XhciVhubEp0Closed(&h);
    CHECK_EQ(h.Ep0Bound, 0, "closed");
    gen++;
    v = XhciVhubRootReset(&h, ALWAYS, gen, 0, PS_EMPTY);
    CHECK_EQ(v & XHCI_VHUB_DO_ARM_HUB, XHCI_VHUB_DO_ARM_HUB,
             "an addressed hub's reset is a re-enumeration: it arms");

    /* SET_CONFIGURATION(0) returns to Addressed, and port 1 reads disabled. */
    enumerateHub(&h, ALWAYS, 3);
    XhciVhubSetConfig(&h, 0);
    CHECK_EQ(h.DevState, XHCI_VHUB_DEV_ADDRESSED, "unconfigured");
    XhciVhubSetAddress(&h, 0);
    CHECK_EQ(h.Address, 3, "address 0 is not a SET_ADDRESS the table passes");
    XhciVhubSetAddress(&h, 128);
    CHECK_EQ(h.Address, 3, "nor 128");
}

/*
 * Value 1's root report on a port in virtual-hub mode (record 12 3.2 as
 * amended by 24.3.2): connection, power, over-current and the High-Speed bit
 * are today's; enable, reset and suspend are the upstream view's; so port-1
 * resets and disables on the same physical port never reach the root port.
 */
static void testRootReportV1(void)
{
    XHCI_VHUB h;
    ULONG s;
    ULONG c;
    ULONG strip;
    ULONG v;

    v1Plug(&h, XHCI_SPEED_FULL);
    XhciVhubRootClearChange(&h, ON_DEMAND, XHCI_HUB_C_PORT_RESET);
    enumerateHub(&h, ON_DEMAND, 4);
    XhciVhubRootClearChange(&h, ON_DEMAND, XHCI_HUB_C_PORT_RESET);

    /* The port-1 reset: PR set, then PRC. usbhub clears the connect change
     * first, in record 02's measured order. */
    CHECK_EQ(p1Change(&h), XHCI_HUB_C_PORT_CONNECTION, "(the device's connect)");
    XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_CONNECTION, 0, 0, PS_EN);
    gen++;
    CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen, 0,
                                  PS_EN),
             XHCI_VHUB_DO_PHYS_RESET | XHCI_VHUB_DO_ARM_DEVICE,
             "port 1's reset is physical and arms the device");
    rootReport(&h, ON_DEMAND, PS_RESETTING, 0, &s, &c);
    CHECK_EQ(s, PS_EN, "the root port shows no reset and stays enabled");
    CHECK_EQ(p1Status(&h, PS_RESETTING, XHCI_SPEED_FULL) &
             XHCI_HUB_PORT_RESET, XHCI_HUB_PORT_RESET, "port 1 shows it");

    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_FULL, PS_EN),
             XHCI_VHUB_DO_PIPE, "its completion is port 1's");
    v = XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_RESET, PS_EN, &strip);
    CHECK_EQ(strip, XHCI_HUB_C_PORT_RESET, "the PRC latch leaves the shadow");
    CHECK_EQ(v, XHCI_VHUB_DO_NONE, "(nothing more for port 1)");
    rootReport(&h, ON_DEMAND, PS_EN, 0, &s, &c);
    CHECK_EQ(c, 0, "no unsolicited C_PORT_RESET on the root port");
    CHECK_EQ(p1Change(&h), XHCI_HUB_C_PORT_RESET, "port 1's C_PORT_RESET");
    CHECK_EQ(p1Status(&h, PS_EN, XHCI_SPEED_FULL),
             XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_CONNECTION |
             XHCI_HUB_PORT_ENABLE, "port 1 enabled, Full Speed: no speed bit");

    /* Even if the caller forgot to strip, the report ignores the shadow's
     * enable/reset/suspend changes. */
    rootReport(&h, ON_DEMAND, PS_EN, XHCI_HUB_C_PORT_RESET |
               XHCI_HUB_C_PORT_ENABLE | XHCI_HUB_C_PORT_SUSPEND, &s, &c);
    CHECK_EQ(c, 0, "the shadow's owned groups never reach the root report");

    /* The port-1 disable: the PED write. The upstream stays enabled. */
    CHECK_EQ(XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_PORT_ENABLE, 0, 0,
                                  PS_EN),
             XHCI_VHUB_DO_DISABLE, "port 1's disable is the disable body");
    CHECK_EQ(XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_PORT_ENABLE, 0, 0,
                                  PS_CONN),
             XHCI_VHUB_DO_DISABLE,
             "asked for on a port PED already left too, for the body's "
             "software half");
    CHECK_EQ(XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_PORT_ENABLE, 0, 1,
                                  PS_EN),
             XHCI_VHUB_DO_NONE, "unless a disable is already owed");
    rootReport(&h, ON_DEMAND, PS_CONN, 0, &s, &c);
    CHECK_EQ(s, PS_EN, "and the root port still reads enabled");
    CHECK_EQ(p1Status(&h, PS_CONN, XHCI_SPEED_FULL) & XHCI_HUB_PORT_ENABLE,
             0, "port 1 does not");

    /* A hardware disable (PEC) is port 1's change. */
    resetPort1(&h, ON_DEMAND);
    v = XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_ENABLE, PS_CONN, &strip);
    CHECK_EQ(v, XHCI_VHUB_DO_PIPE, "PEC reaches port 1");
    CHECK_EQ(strip, XHCI_HUB_C_PORT_ENABLE, "and leaves the shadow");
    CHECK_EQ(p1Change(&h), XHCI_HUB_C_PORT_ENABLE, "C_PORT_ENABLE on port 1");
    rootReport(&h, ON_DEMAND, PS_CONN, 0, &s, &c);
    CHECK_EQ(s & XHCI_HUB_PORT_ENABLE, XHCI_HUB_PORT_ENABLE,
             "the upstream is untouched");
    XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_ENABLE, 0, 0, PS_CONN);

    /* Over-current reaches both views, each with its own latch. */
    v = XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_OVER_CURRENT,
                       PS_CONN | XHCI_HUB_PORT_OVER_CURRENT, &strip);
    CHECK_EQ(strip, 0, "the root port keeps its C_PORT_OVER_CURRENT");
    CHECK_EQ(v, XHCI_VHUB_DO_PIPE, "and port 1 latches its own");
    rootReport(&h, ON_DEMAND, PS_CONN | XHCI_HUB_PORT_OVER_CURRENT,
               XHCI_HUB_C_PORT_OVER_CURRENT, &s, &c);
    CHECK_EQ(c, XHCI_HUB_C_PORT_OVER_CURRENT, "root: today's");
    CHECK_EQ(p1Change(&h), XHCI_HUB_C_PORT_OVER_CURRENT, "port 1: its own");
    XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_OVER_CURRENT, 0, 0,
                         PS_CONN);
    CHECK_EQ(p1Change(&h), 0, "clearing port 1's");
    rootReport(&h, ON_DEMAND, PS_CONN, XHCI_HUB_C_PORT_OVER_CURRENT, &s, &c);
    CHECK_EQ(c, XHCI_HUB_C_PORT_OVER_CURRENT, "leaves the root port's");

    /* The unplug: today's connect change, and the upstream down with it. */
    v = XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_CONNECTION, PS_EMPTY,
                       &strip);
    rootReport(&h, ON_DEMAND, PS_EMPTY, XHCI_HUB_C_PORT_CONNECTION, &s, &c);
    CHECK_EQ(s, PS_EMPTY, "the root port reads empty, as today");
    CHECK_EQ(c, XHCI_HUB_C_PORT_CONNECTION, "with today's connect change");
}

/*
 * A configured hub with an enabled device on port 1, all changes cleared, at
 * either value. What the suspend and power vectors start from.
 */
static void readyHub(PXHCI_VHUB h, ULONG applied)
{
    if (applied == ON_DEMAND) {
        v1Plug(h, XHCI_SPEED_FULL);
    } else {
        XhciVhubStart(h, ALWAYS, 1);
        gen++;
        XhciVhubRootReset(h, ALWAYS, gen, 0, PS_CONN);
    }
    enumerateHub(h, applied, 2);
    XhciVhubPort1Feature(h, 0, XHCI_VHUB_SEL_C_PORT_CONNECTION, 0, 0, PS_EN);
    resetPort1(h, applied);
    h->UpChanges = 0;
    CHECK_EQ(p1Status(h, PS_EN, XHCI_SPEED_FULL) & XHCI_HUB_PORT_ENABLE,
             XHCI_HUB_PORT_ENABLE, "(ready: port 1 enabled)");
}

/* Both views' suspend bits and changes, and at 1 the root report's pair read
 * from the upstream view whatever the physical port shows. */
static void checkViews(const XHCI_VHUB *h, ULONG applied, ULONG upSusp,
                       ULONG upChange, ULONG p1Susp, ULONG p1Chg,
                       const char *what)
{
    ULONG s;
    ULONG c;
    ULONG phys;

    /* The physical port reads suspended exactly when either view is. */
    phys = (h->PhysSuspended) ? PS_SUSP : PS_EN;
    XhciVhubRootReport(h, applied, phys, XHCI_HUB_C_PORT_SUSPEND, &s, &c);
    check_eq_impl(s & XHCI_HUB_PORT_SUSPEND,
                  upSusp ? XHCI_HUB_PORT_SUSPEND : 0, what, __FILE__,
                  __LINE__);
    check_eq_impl(c & XHCI_HUB_C_PORT_SUSPEND,
                  upChange ? XHCI_HUB_C_PORT_SUSPEND : 0, what, __FILE__,
                  __LINE__);
    XhciVhubPort1Report(h, phys, XHCI_SPEED_FULL, &s, &c);
    check_eq_impl(s & XHCI_HUB_PORT_SUSPEND,
                  p1Susp ? XHCI_HUB_PORT_SUSPEND : 0, what, __FILE__,
                  __LINE__);
    check_eq_impl(c & XHCI_HUB_C_PORT_SUSPEND,
                  p1Chg ? XHCI_HUB_C_PORT_SUSPEND : 0, what, __FILE__,
                  __LINE__);
}

static ULONG p1Suspend(PXHCI_VHUB h, ULONG set, ULONG phys)
{
    return XhciVhubPort1Feature(h, set, XHCI_VHUB_SEL_PORT_SUSPEND, 0, 0,
                                phys);
}

/* 3.5 in every order it lists, at both values. */
static void testSuspendMerge(void)
{
    XHCI_VHUB h;
    ULONG applied;

    for (applied = ON_DEMAND; applied <= ALWAYS; applied++) {
        /* The root port alone: entry latches nothing; the completed resume
         * latches one C_PORT_SUSPEND. */
        readyHub(&h, applied);
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 1, PS_EN),
                 XHCI_VHUB_DO_SUSPEND, "root suspend suspends the port");
        checkViews(&h, applied, 1, 0, 0, 0, "root suspended, no change");
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 1, PS_SUSP),
                 XHCI_VHUB_DO_NONE, "a redundant suspend does nothing");
        checkViews(&h, applied, 1, 0, 0, 0, "and latches nothing");
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 0, PS_SUSP),
                 XHCI_VHUB_DO_RESUME, "root resume resumes the port");
        checkViews(&h, applied, 0, 0, 0, 0, "no change until it finishes");
        CHECK_EQ(XhciVhubResumeDone(&h), XHCI_VHUB_DO_ROOT_CHANGE,
                 "the resume finished");
        checkViews(&h, applied, 0, 1, 0, 0, "one C_PORT_SUSPEND, root's");
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 0, PS_EN),
                 XHCI_VHUB_DO_NONE, "a redundant resume does nothing");
        XhciVhubRootClearChange(&h, applied, XHCI_HUB_C_PORT_SUSPEND);
        checkViews(&h, applied, 0, 0, 0, 0, "cleared, and not re-latched");

        /* Port 1 alone. */
        CHECK_EQ(p1Suspend(&h, 1, PS_EN), XHCI_VHUB_DO_SUSPEND,
                 "port-1 suspend suspends the port");
        checkViews(&h, applied, 0, 0, 1, 0, "port 1 suspended, no change");
        CHECK_EQ(p1Suspend(&h, 1, PS_SUSP), XHCI_VHUB_DO_NONE, "redundant");
        CHECK_EQ(p1Suspend(&h, 0, PS_SUSP), XHCI_VHUB_DO_RESUME,
                 "port-1 resume resumes the port");
        CHECK_EQ(XhciVhubResumeDone(&h), XHCI_VHUB_DO_PIPE, "(finished)");
        checkViews(&h, applied, 0, 0, 0, 1, "one C_PORT_SUSPEND, port 1's");
        XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_SUSPEND, 0, 0, PS_EN);
        CHECK_EQ(p1Suspend(&h, 0, PS_EN), XHCI_VHUB_DO_NONE, "redundant");
        checkViews(&h, applied, 0, 0, 0, 0, "(clear)");

        /* The ordinary order: port 1 then the root port, both suspended,
         * and usbhub resuming the root port first. */
        CHECK_EQ(p1Suspend(&h, 1, PS_EN), XHCI_VHUB_DO_SUSPEND, "port 1 first");
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 1, PS_SUSP),
                 XHCI_VHUB_DO_NONE, "the root port adds nothing physical");
        checkViews(&h, applied, 1, 0, 1, 0, "both suspended");
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 0, PS_SUSP),
                 XHCI_VHUB_DO_ROOT_CHANGE,
                 "the root resume completes at once, without the port");
        CHECK_EQ(h.PhysSuspended, 1, "the port stays suspended for port 1");
        checkViews(&h, applied, 0, 1, 1, 0, "upstream running, port 1 not");
        XhciVhubRootClearChange(&h, applied, XHCI_HUB_C_PORT_SUSPEND);
        CHECK_EQ(p1Suspend(&h, 0, PS_SUSP), XHCI_VHUB_DO_RESUME,
                 "then port 1's resume resumes the port");
        checkViews(&h, applied, 0, 0, 0, 0, "(resume signalling)");
        CHECK_EQ(XhciVhubResumeDone(&h), XHCI_VHUB_DO_PIPE, "(finished)");
        checkViews(&h, applied, 0, 0, 0, 1, "port 1's change only");
        XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_SUSPEND, 0, 0, PS_EN);

        /* The root port first, then port 1 (unreachable by usbhub, since a
         * suspended hub takes no request; held to the same rule anyway). */
        XhciVhubRootSuspend(&h, applied, 1, PS_EN);
        CHECK_EQ(p1Suspend(&h, 1, PS_SUSP), XHCI_VHUB_DO_NONE,
                 "port 1 under a suspended upstream: nothing physical");
        CHECK_EQ(p1Suspend(&h, 0, PS_SUSP), XHCI_VHUB_DO_PIPE,
                 "and its resume completes at once");
        checkViews(&h, applied, 1, 0, 0, 1, "(port 1 running in software)");
        XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_SUSPEND, 0, 0,
                             PS_SUSP);
        XhciVhubRootSuspend(&h, applied, 0, PS_SUSP);
        XhciVhubResumeDone(&h);
        XhciVhubRootClearChange(&h, applied, XHCI_HUB_C_PORT_SUSPEND);

        /* The other two orders: both suspended, and the view that did not
         * suspend the port resuming first. Port 1 first, port 1 resumes
         * first: its resume is software alone, and the root's is what resumes
         * the port. */
        CHECK_EQ(p1Suspend(&h, 1, PS_EN), XHCI_VHUB_DO_SUSPEND, "(port 1)");
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 1, PS_SUSP),
                 XHCI_VHUB_DO_NONE, "(then the root)");
        CHECK_EQ(p1Suspend(&h, 0, PS_SUSP), XHCI_VHUB_DO_PIPE,
                 "port 1 resuming first completes at once");
        checkViews(&h, applied, 1, 0, 0, 1, "upstream still suspended");
        CHECK_EQ(h.PhysSuspended, 1, "and so is the port");
        XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_SUSPEND, 0, 0,
                             PS_SUSP);
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 0, PS_SUSP),
                 XHCI_VHUB_DO_RESUME, "the root's resume resumes the port");
        CHECK_EQ(XhciVhubResumeDone(&h), XHCI_VHUB_DO_ROOT_CHANGE,
                 "(finished)");
        checkViews(&h, applied, 0, 1, 0, 0, "the root's change only");
        XhciVhubRootClearChange(&h, applied, XHCI_HUB_C_PORT_SUSPEND);

        /* The root first, then port 1, and the root resuming first. */
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 1, PS_EN),
                 XHCI_VHUB_DO_SUSPEND, "(the root)");
        CHECK_EQ(p1Suspend(&h, 1, PS_SUSP), XHCI_VHUB_DO_NONE, "(then port 1)");
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 0, PS_SUSP),
                 XHCI_VHUB_DO_ROOT_CHANGE,
                 "the root resuming first completes at once");
        checkViews(&h, applied, 0, 1, 1, 0, "port 1 still suspended");
        CHECK_EQ(h.PhysSuspended, 1, "and so is the port");
        XhciVhubRootClearChange(&h, applied, XHCI_HUB_C_PORT_SUSPEND);
        CHECK_EQ(p1Suspend(&h, 0, PS_SUSP), XHCI_VHUB_DO_RESUME,
                 "port 1's resume resumes the port");
        CHECK_EQ(XhciVhubResumeDone(&h), XHCI_VHUB_DO_PIPE, "(finished)");
        checkViews(&h, applied, 0, 0, 0, 1, "port 1's change only");
        XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_SUSPEND, 0, 0, PS_EN);

        /* A remote wake with the upstream alone suspended, and with neither;
         * and one that arrives while a resume is already owed. */
        XhciVhubRootSuspend(&h, applied, 1, PS_EN);
        CHECK_EQ(XhciVhubRemoteWake(&h), XHCI_VHUB_DO_ROOT_CHANGE,
                 "a remote wake with the upstream alone suspended");
        checkViews(&h, applied, 0, 1, 0, 0, "only the root latches");
        XhciVhubRootClearChange(&h, applied, XHCI_HUB_C_PORT_SUSPEND);
        CHECK_EQ(XhciVhubRemoteWake(&h), XHCI_VHUB_DO_NONE,
                 "with neither suspended, nothing");
        checkViews(&h, applied, 0, 0, 0, 0, "(nothing latched)");
        XhciVhubRootSuspend(&h, applied, 1, PS_EN);
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 0, PS_SUSP),
                 XHCI_VHUB_DO_RESUME, "(a resume in flight)");
        CHECK_EQ(XhciVhubRemoteWake(&h), XHCI_VHUB_DO_ROOT_CHANGE,
                 "a wake read while it is owed completes it once");
        checkViews(&h, applied, 0, 1, 0, 0, "one change");
        CHECK_EQ(h.UpResumeOwed, 0, "(nothing owed)");
        XhciVhubRootClearChange(&h, applied, XHCI_HUB_C_PORT_SUSPEND);

        /* A remote wake: both bits clear, a change in each view that was
         * suspended. */
        p1Suspend(&h, 1, PS_EN);
        XhciVhubRootSuspend(&h, applied, 1, PS_SUSP);
        CHECK_EQ(XhciVhubRemoteWake(&h),
                 XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_PIPE,
                 "a remote wake with both suspended");
        checkViews(&h, applied, 0, 1, 0, 1, "both latch C_PORT_SUSPEND");
        XhciVhubRootClearChange(&h, applied, XHCI_HUB_C_PORT_SUSPEND);
        XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_SUSPEND, 0, 0, PS_EN);
        p1Suspend(&h, 1, PS_EN);
        CHECK_EQ(XhciVhubRemoteWake(&h), XHCI_VHUB_DO_PIPE,
                 "a remote wake with port 1 alone suspended");
        checkViews(&h, applied, 0, 0, 0, 1, "only port 1 latches");
        XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_SUSPEND, 0, 0, PS_EN);

        /* The empty physical port: value 2's empty hub, and at 1 a port whose
         * port 1 usbhub has disabled. */
        CHECK_EQ(XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_PORT_ENABLE, 0, 0,
                                      PS_EN),
                 XHCI_VHUB_DO_DISABLE, "(port 1 disabled)");
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 1, PS_CONN),
                 XHCI_VHUB_DO_NONE,
                 "a root suspend with nothing enabled is software alone");
        checkViews(&h, applied, 1, 0, 0, 0, "the bit set, no change");
        CHECK_EQ(p1Suspend(&h, 1, PS_CONN), XHCI_VHUB_DO_NONE,
                 "a disabled port 1 cannot be suspended");
        checkViews(&h, applied, 1, 0, 0, 0, "and its bit stays clear");
        CHECK_EQ(XhciVhubRootSuspend(&h, applied, 0, PS_CONN),
                 XHCI_VHUB_DO_ROOT_CHANGE, "its resume completes at once");
        checkViews(&h, applied, 0, 1, 0, 0, "one change");
    }

    /* Value 2's hub with nothing plugged in. */
    XhciVhubStart(&h, ALWAYS, 0);
    gen++;
    XhciVhubRootReset(&h, ALWAYS, gen, 0, PS_EMPTY);
    enumerateHub(&h, ALWAYS, 2);
    h.UpChanges = 0;
    CHECK_EQ(XhciVhubRootSuspend(&h, ALWAYS, 1, PS_EMPTY), XHCI_VHUB_DO_NONE,
             "an empty hub suspends in software");
    CHECK_EQ(p1Suspend(&h, 1, PS_EMPTY), XHCI_VHUB_DO_NONE,
             "port 1 with nothing on it");
    CHECK_EQ(h.P1Suspend, 0, "stays running");
}

/*
 * Port 1's power bit is its own (3.3): CLEAR runs the disable body and leaves
 * PORTSC.PP alone, SET latches a connect change for a device still there, and
 * the device enumerates again - at both values, and with the PED confirmation
 * late so the next reset is held until it is collected.
 */
static void testPort1Power(void)
{
    XHCI_VHUB h;
    ULONG applied;
    ULONG s;
    ULONG c;
    ULONG v;

    for (applied = ON_DEMAND; applied <= ALWAYS; applied++) {
        readyHub(&h, applied);
        v = XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_PORT_POWER, 0, 0, PS_EN);
        CHECK_EQ(v, XHCI_VHUB_DO_DISABLE,
                 "CLEAR PORT_POWER is the disable body, never POWER_OFF");
        CHECK_EQ(p1Status(&h, PS_EN, XHCI_SPEED_FULL), 0,
                 "port 1 unpowered reads no power, no connection, disabled");
        rootReport(&h, applied, PS_CONN, 0, &s, &c);
        CHECK_EQ(s & (XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_ENABLE),
                 XHCI_HUB_PORT_POWER | XHCI_HUB_PORT_ENABLE,
                 "the root port keeps power and stays enabled");

        /* The confirmation is late: DisownPending is still set. */
        CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_POWER, 0, 1,
                                      PS_CONN),
                 XHCI_VHUB_DO_PIPE, "SET PORT_POWER with the device there");
        CHECK_EQ(p1Change(&h), XHCI_HUB_C_PORT_CONNECTION,
                 "latches its connect change");
        CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_POWER, 0, 1,
                                      PS_CONN),
                 XHCI_VHUB_DO_NONE, "a second SET latches nothing");
        XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_CONNECTION, 0, 1,
                             PS_CONN);
        gen++;
        CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen, 1,
                                      PS_CONN),
                 XHCI_VHUB_DO_RESET_HELD, "the reset is held while owed");
        CHECK_EQ(h.P1Resetting, 0, "nothing started");
        gen++;
        CHECK_EQ(XhciVhubDisownCollected(&h, applied, gen),
                 XHCI_VHUB_DO_PHYS_RESET | XHCI_VHUB_DO_ARM_DEVICE,
                 "the collected confirmation releases the reset");
        CHECK_EQ(XhciVhubDisownCollected(&h, applied, gen), XHCI_VHUB_DO_NONE,
                 "once");
        CHECK_EQ(XhciVhubResetDone(&h, applied, gen, 0, XHCI_SPEED_FULL,
                                   PS_EN),
                 XHCI_VHUB_DO_PIPE, "the device reset");
        CHECK_EQ(p1Status(&h, PS_EN, XHCI_SPEED_FULL) & XHCI_HUB_PORT_ENABLE,
                 XHCI_HUB_PORT_ENABLE, "and enabled again behind the hub");

        /* An unpowered port 1 takes no reset. */
        XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_PORT_POWER, 0, 0, PS_EN);
        CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, 1, 0,
                                      PS_CONN),
                 XHCI_VHUB_DO_NONE, "no reset without port power");
        CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_POWER, 0, 0,
                                      PS_EMPTY),
                 XHCI_VHUB_DO_NONE, "powered with nothing there: no change");
    }

    /* A port-1 reset that runs out its deadline still latches C_PORT_RESET,
     * as the root port's does today, and leaves port 1 disabled. */
    readyHub(&h, ON_DEMAND);
    gen++;
    XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen, 0, PS_EN);
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 1, XHCI_SPEED_FULL, PS_CONN),
             XHCI_VHUB_DO_PIPE, "a timed-out port-1 reset");
    CHECK_EQ(p1Change(&h), XHCI_HUB_C_PORT_RESET, "latches C_PORT_RESET");
    CHECK_EQ(p1Status(&h, PS_CONN, XHCI_SPEED_FULL) & XHCI_HUB_PORT_ENABLE, 0,
             "and port 1 is not enabled");
    CHECK_EQ(h.Decision, XHCI_VHUB_DECIDED_HUB,
             "the root port's decision untouched by port 1's reset");
}

/*
 * A port-1 reset held for an owed disable, then port 1 disabled or powered
 * off before the confirmation (task 24.3.4, Codex's first round): usbhub has
 * abandoned the reset, so the confirmation must not start it. It ends at the
 * disable, reported as a preempted root-port reset is, with no second
 * disable issued while one is owed and nothing about the hub torn down.
 */
static void testHeldResetEndsAtPort1Disable(void)
{
    static const ULONG selectors[2] = { XHCI_VHUB_SEL_PORT_ENABLE,
                                        XHCI_VHUB_SEL_PORT_POWER };
    XHCI_VHUB h;
    ULONG applied;
    ULONG selector;
    ULONG i;

    for (applied = ON_DEMAND; applied <= ALWAYS; applied++) {
        for (i = 0; i < 2; i++) {
            selector = selectors[i];
            readyHub(&h, applied);
            XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_PORT_ENABLE, 0, 0, PS_EN);
            gen++;
            CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen,
                                          1, PS_EN),
                     XHCI_VHUB_DO_RESET_HELD, "(the reset held while owed)");
            h.P1Changes = 0;
            CHECK_EQ(XhciVhubPort1Feature(&h, 0, selector, 0, 1, PS_EN),
                     XHCI_VHUB_DO_PIPE,
                     "the disable ends the held reset, and issues no second "
                     "disable while one is owed");
            CHECK_EQ(h.ResetHeld, XHCI_VHUB_OWNER_NONE, "nothing held");
            CHECK_EQ(p1Change(&h), XHCI_HUB_C_PORT_RESET,
                     "port 1's C_PORT_RESET reports the reset ended");
            CHECK_EQ(p1Status(&h, PS_EN, XHCI_SPEED_FULL) &
                         (XHCI_HUB_PORT_ENABLE | XHCI_HUB_PORT_RESET),
                     0, "with port 1 disabled and not resetting");
            gen++;
            CHECK_EQ(XhciVhubDisownCollected(&h, applied, gen),
                     XHCI_VHUB_DO_NONE,
                     "the confirmation starts nothing usbhub abandoned");
            CHECK_EQ(h.Address, 2, "nothing about the hub torn down");
            CHECK_EQ(h.DevState, XHCI_VHUB_DEV_CONFIGURED,
                     "(still configured)");
            if (selector == XHCI_VHUB_SEL_PORT_POWER) {
                XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_POWER, 0, 0,
                                     PS_CONN);
            }
            gen++;
            CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen,
                                          0, PS_CONN),
                     XHCI_VHUB_DO_PHYS_RESET | XHCI_VHUB_DO_ARM_DEVICE,
                     "and a reset asked for afterwards runs");
        }
    }
}

/*
 * At 1 the hub is retired by any reading that says its device is gone (task
 * 24.3.4): a connect change with the port still connected is a device swapped
 * between two readings, and the hub belonged to the one that left. A port-1
 * reset running when the device goes stays recognised, owned by nobody, so
 * its end claims nothing; at 2 the hub stays and the change is port 1's.
 */
static void testUnplugRetiresV1(void)
{
    XHCI_VHUB h;
    ULONG strip;
    ULONG resetGen;

    v1Plug(&h, XHCI_SPEED_FULL);
    enumerateHub(&h, ON_DEMAND, 6);
    CHECK_EQ(XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_CONNECTION,
                            PS_CONN, &strip),
             XHCI_VHUB_DO_DROP, "a swap between readings retires the hub");
    CHECK(recordIsZero(&h), "(its address with it)");

    readyHub(&h, ON_DEMAND);
    gen++;
    resetGen = gen;
    CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, resetGen, 0,
                                  PS_EN),
             XHCI_VHUB_DO_PHYS_RESET | XHCI_VHUB_DO_ARM_DEVICE,
             "(a port-1 reset running)");
    CHECK_EQ(XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_CONNECTION,
                            PS_EMPTY, &strip),
             XHCI_VHUB_DO_DROP, "the device leaves mid-reset");
    CHECK_EQ(h.Present, 0, "the hub retired");
    CHECK_EQ(h.Address, 0, "with its address");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_SUPERSEDED,
             "the running reset owned by nobody");
    CHECK_EQ(h.ResetGeneration, resetGen, "and still recognised");
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, resetGen, 0, XHCI_SPEED_FULL,
                               PS_EMPTY),
             XHCI_VHUB_DO_NONE, "its end decides nothing");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_NONE, "and frees the port");

    readyHub(&h, ALWAYS);
    CHECK_EQ(XhciVhubAbsorb(&h, ALWAYS, XHCI_HUB_C_PORT_CONNECTION, PS_EMPTY,
                            &strip),
             XHCI_VHUB_DO_PIPE, "at 2 an unplug is port 1's change");
    CHECK_EQ(h.Present, 1, "and the hub stays");
}

/* At 1, a root reset on a port holding a hub is held while a disable is owed
 * (3.2), as a port-1 reset is. */
static void testRootResetHeldV1(void)
{
    XHCI_VHUB h;

    readyHub(&h, ON_DEMAND);
    XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_PORT_ENABLE, 0, 0, PS_EN);
    gen++;
    CHECK_EQ(XhciVhubRootReset(&h, ON_DEMAND, gen, 1, PS_CONN),
             XHCI_VHUB_DO_RESET_HELD, "held while the disable is owed");
    CHECK_EQ(h.UpResetting, 0, "not started");
    gen++;
    CHECK_EQ(XhciVhubDisownCollected(&h, ON_DEMAND, gen),
             XHCI_VHUB_DO_PHYS_RESET, "released once collected");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_ROOT, "owned by the root view");
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 0, XHCI_SPEED_FULL, PS_EN),
             XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_ARM_HUB,
             "and re-creates the hub");

    /* A port with no hub is today's: never held here. */
    XhciVhubStart(&h, ON_DEMAND, 1);
    gen++;
    CHECK_EQ(XhciVhubRootReset(&h, ON_DEMAND, gen, 1, PS_CONN),
             XHCI_VHUB_DO_PHYS_RESET, "a direct port's reset is not held");
}

/* Value 2 (3.8): the root port reports the hub, from start to stop. */
static void testValue2Lifecycle(void)
{
    XHCI_VHUB h;
    ULONG s;
    ULONG c;
    ULONG v;
    ULONG strip;
    const ULONG hubUp = XHCI_HUB_PORT_CONNECTION | XHCI_HUB_PORT_POWER |
                        XHCI_HUB_PORT_HIGH_SPEED;

    /* Start, nothing plugged in: a hub, connected, not enabled, one change. */
    CHECK_EQ(XhciVhubStart(&h, ALWAYS, 0), XHCI_VHUB_DO_ROOT_CHANGE,
             "a hub at start");
    rootReport(&h, ALWAYS, PS_EMPTY, 0, &s, &c);
    CHECK_EQ(s, hubUp, "connected, powered, High Speed, not enabled");
    CHECK_EQ(c, XHCI_HUB_C_PORT_CONNECTION, "C_PORT_CONNECTION once, at start");
    CHECK_EQ(p1Change(&h), 0, "an empty port 1 has no change");
    CHECK_EQ(p1Status(&h, PS_EMPTY, XHCI_SPEED_UNKNOWN), XHCI_HUB_PORT_POWER,
             "port 1 powered and empty");
    XhciVhubRootClearChange(&h, ALWAYS, XHCI_HUB_C_PORT_CONNECTION);

    /* Start with a device: port 1's connect latched too. */
    XhciVhubStart(&h, ALWAYS, 1);
    CHECK_EQ(p1Change(&h), XHCI_HUB_C_PORT_CONNECTION,
             "a device at start is port 1's change");
    XhciVhubRootClearChange(&h, ALWAYS, XHCI_HUB_C_PORT_CONNECTION);

    /* Every speed on port 1, High Speed included, at its decoded speed. */
    CHECK_EQ(p1Status(&h, PS_CONN, XHCI_SPEED_HIGH) & 0x0600,
             XHCI_HUB_PORT_HIGH_SPEED, "a High-Speed device on port 1");
    CHECK_EQ(p1Status(&h, PS_CONN, XHCI_SPEED_LOW) & 0x0600,
             XHCI_HUB_PORT_LOW_SPEED, "a Low-Speed device");
    CHECK_EQ(p1Status(&h, PS_CONN, XHCI_SPEED_FULL) & 0x0600, 0,
             "Full Speed carries neither bit");
    rootReport(&h, ALWAYS, PS_EN, XHCI_HUB_C_PORT_RESET, &s, &c);
    CHECK_EQ(s, hubUp, "the root port is the hub whatever PORTSC says");
    CHECK_EQ(c, 0, "and none of the shadow's changes");

    /* The root reset is synthetic; with a device enabled, the disable body. */
    readyHub(&h, ALWAYS);
    gen++;
    v = XhciVhubRootReset(&h, ALWAYS, gen, 0, PS_EN);
    CHECK_EQ(v, XHCI_VHUB_DO_ROOT_CHANGE | XHCI_VHUB_DO_ARM_HUB |
                XHCI_VHUB_DO_DISABLE,
             "a root reset with a device attached: hub back to Default, "
             "device out of service through the disable body");
    CHECK_EQ(v & XHCI_VHUB_DO_PHYS_RESET, 0, "PR is never set");
    rootReport(&h, ALWAYS, PS_EN, 0, &s, &c);
    CHECK_EQ(s, hubUp | XHCI_HUB_PORT_ENABLE, "the upstream enabled");
    CHECK_EQ(c, XHCI_HUB_C_PORT_RESET,
             "C_PORT_RESET now, not after the confirmation");
    CHECK_EQ(p1Status(&h, PS_EN, XHCI_SPEED_FULL) & XHCI_HUB_PORT_ENABLE, 0,
             "port 1 reads disabled");
    CHECK_EQ(h.Ep0Bound, 1, "the hub's EP0 binding is kept");
    CHECK_EQ(p1Change(&h), XHCI_HUB_C_PORT_CONNECTION,
             "the device is a connect change on port 1 again");
    CHECK_EQ(XhciVhubPipeByte(&h), 0,
             "the held transfer carries nothing until the hub is configured");

    /* The device is reached again only through a held port-1 reset. */
    enumerateHub(&h, ALWAYS, 2);
    CHECK_EQ(XhciVhubPipeByte(&h), 0x02, "configured: the change is sent");
    XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_CONNECTION, 0, 1, PS_CONN);
    CHECK_EQ(XhciVhubPipeByte(&h), 0, "nothing to send: the transfer waits");
    gen++;
    CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen, 1,
                                  PS_CONN),
             XHCI_VHUB_DO_RESET_HELD, "the port-1 reset waits on the PED "
                                      "confirmation");
    gen++;
    CHECK_EQ(XhciVhubDisownCollected(&h, ALWAYS, gen),
             XHCI_VHUB_DO_PHYS_RESET | XHCI_VHUB_DO_ARM_DEVICE, "released");
    XhciVhubResetDone(&h, ALWAYS, gen, 0, XHCI_SPEED_HIGH, PS_EN);

    /* A root reset with a disable already owed issues no second one. */
    gen++;
    v = XhciVhubRootReset(&h, ALWAYS, gen, 1, PS_EN);
    CHECK_EQ(v & XHCI_VHUB_DO_DISABLE, 0, "no second disable");

    /* Plug and unplug go through port 1; the root port never changes. */
    readyHub(&h, ALWAYS);
    v = XhciVhubAbsorb(&h, ALWAYS, XHCI_HUB_C_PORT_CONNECTION, PS_EMPTY,
                       &strip);
    CHECK_EQ(v, XHCI_VHUB_DO_PIPE, "an unplug is port 1's");
    CHECK_EQ(strip, XHCI_HUB_C_PORT_CONNECTION, "and leaves the shadow");
    rootReport(&h, ALWAYS, PS_EMPTY, 0, &s, &c);
    CHECK_EQ(s, hubUp | XHCI_HUB_PORT_ENABLE, "the hub stays");
    CHECK_EQ(c, 0, "with no root change");
    CHECK_EQ(p1Status(&h, PS_EMPTY, XHCI_SPEED_UNKNOWN),
             XHCI_HUB_PORT_POWER, "port 1 empty");
    CHECK_EQ(XhciVhubPipeByte(&h), 0x02, "and usbhub is told through the hub");
    XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_CONNECTION, 0, 0,
                         PS_EMPTY);
    v = XhciVhubAbsorb(&h, ALWAYS, XHCI_HUB_C_PORT_CONNECTION, PS_CONN,
                       &strip);
    CHECK_EQ(v, XHCI_VHUB_DO_PIPE, "a plug is port 1's");
    CHECK_EQ(p1Status(&h, PS_CONN, XHCI_SPEED_FULL) & XHCI_HUB_PORT_ENABLE, 0,
             "not enabled until usbhub resets it");
    v = XhciVhubAbsorb(&h, ALWAYS, XHCI_HUB_C_PORT_OVER_CURRENT,
                       PS_CONN | XHCI_HUB_PORT_OVER_CURRENT, &strip);
    CHECK_EQ(strip, XHCI_HUB_C_PORT_OVER_CURRENT,
             "at 2 over-current is port 1's alone");
    rootReport(&h, ALWAYS, PS_CONN | XHCI_HUB_PORT_OVER_CURRENT, 0, &s, &c);
    CHECK_EQ(s & XHCI_HUB_PORT_OVER_CURRENT, 0, "(not the root port's)");

    /* A cancelled hub install: usbport lets go of the hub. */
    readyHub(&h, ALWAYS);
    v = XhciVhubRootDisable(&h, ALWAYS, PS_EN);
    CHECK_EQ(v, XHCI_VHUB_DO_CANCEL_PIPE | XHCI_VHUB_DO_DISABLE,
             "disable: held transfer cancelled, device out of service");
    CHECK_EQ(h.Present, 1, "the record stays");
    CHECK_EQ(h.Address, 0, "with no address");
    CHECK_EQ(h.Ep0Bound, 0, "and no bindings");
    rootReport(&h, ALWAYS, PS_CONN, 0, &s, &c);
    CHECK_EQ(s, hubUp, "connected and not enabled");
    gen++;
    CHECK_EQ(XhciVhubRootReset(&h, ALWAYS, gen, 1, PS_CONN) &
             XHCI_VHUB_DO_ARM_HUB, XHCI_VHUB_DO_ARM_HUB,
             "the next root reset enumerates the hub again, no restart");
    enumerateHub(&h, ALWAYS, 7);
    CHECK_EQ(XhciVhubRootDisable(&h, ALWAYS, PS_CONN),
             XHCI_VHUB_DO_CANCEL_PIPE | XHCI_VHUB_DO_DISABLE,
             "nothing enabled: the disable body still, for a child the port "
             "left enabled without letting go of");

    /* Power off and on: the hub gone from usbport's view and back. */
    readyHub(&h, ALWAYS);
    v = XhciVhubRootPower(&h, ALWAYS, 0, PS_EN);
    CHECK_EQ(v, XHCI_VHUB_DO_CANCEL_PIPE | XHCI_VHUB_DO_POWER_OFF,
             "power-off: today's body, which takes the device down");
    rootReport(&h, ALWAYS, 0, 0, &s, &c);
    CHECK_EQ(s, 0, "neither powered nor connected");
    CHECK_EQ(c, 0, "(no change yet)");
    v = XhciVhubRootPower(&h, ALWAYS, 1, 0);
    CHECK_EQ(v, XHCI_VHUB_DO_POWER_ON | XHCI_VHUB_DO_ROOT_CHANGE,
             "power-on: today's body and a connect change");
    rootReport(&h, ALWAYS, PS_EMPTY, 0, &s, &c);
    CHECK_EQ(s, hubUp, "connected again");
    CHECK_EQ(c, XHCI_HUB_C_PORT_CONNECTION, "so usbhub enumerates it afresh");
    CHECK_EQ(XhciVhubRootPower(&h, ALWAYS, 1, PS_EMPTY),
             XHCI_VHUB_DO_POWER_ON, "a redundant power-on latches nothing");
    gen++;
    CHECK_EQ(XhciVhubRootReset(&h, ALWAYS, gen, 0, PS_CONN) &
             XHCI_VHUB_DO_ARM_HUB, XHCI_VHUB_DO_ARM_HUB, "enumerated again");

    /* Resume and recovery keep the hubs. */
    readyHub(&h, ALWAYS);
    CHECK_EQ(XhciVhubReinit(&h, ALWAYS, 0, 0), XHCI_VHUB_DO_NONE,
             "a restoring resume with the device still there: nothing");
    CHECK_EQ(XhciVhubReinit(&h, ALWAYS, 1, 0), XHCI_VHUB_DO_PIPE,
             "a reinitialisation that took the device");
    CHECK_EQ(h.Address, 2, "the hub keeps its address");
    CHECK_EQ(h.DevState, XHCI_VHUB_DEV_CONFIGURED, "and its configuration");
    CHECK_EQ(p1Change(&h), XHCI_HUB_C_PORT_CONNECTION,
             "the device is a connect change on port 1");
    rootReport(&h, ALWAYS, PS_CONN, 0, &s, &c);
    CHECK_EQ(c, 0, "and nothing but the start latches a root connect");
    CHECK_EQ(XhciVhubPipeByte(&h), 0x02, "the held transfer completes");

    /* Value 1 latches nothing across a reinit - today's path - but a reset
     * in flight or held is over at either value, since the shadows a held
     * one waited on are rebuilt and nothing else would ever release it. */
    readyHub(&h, ON_DEMAND);
    h.ResetOwner = (UCHAR)XHCI_VHUB_OWNER_ROOT;
    h.UpResetting = 1;
    h.ResetHeld = (UCHAR)XHCI_VHUB_OWNER_PORT1;
    CHECK_EQ(XhciVhubReinit(&h, ON_DEMAND, 1, 0), XHCI_VHUB_DO_NONE,
             "at 1 a reinit latches nothing");
    CHECK_EQ(h.Present, 1, "and keeps the hub");
    CHECK_EQ(h.ResetHeld, XHCI_VHUB_OWNER_NONE, "but releases a held reset");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_NONE, "and ends one in flight");
    CHECK_EQ(h.UpResetting, 0, "(the upstream no longer resetting)");
}

/*
 * A root reset while port 1's is still running on the physical port. At 2
 * the root reset is synthetic and overtakes it: the running reset's end is
 * then nobody's - not the device's claim, which would spend the hub's open
 * the root reset armed, and not port 1's change on a hub back in Default.
 * At 1 the root reset is physical and the port is armed with port 1's, so
 * the write is asked for and refused as busy, with nothing changing hands.
 */
static void testRootResetOverPort1Reset(void)
{
    XHCI_VHUB h;
    ULONG first;
    ULONG v;

    readyHub(&h, ALWAYS);
    gen++;
    first = gen;
    CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen, 0,
                                  PS_EN),
             XHCI_VHUB_DO_PHYS_RESET | XHCI_VHUB_DO_ARM_DEVICE,
             "(port 1's reset starts)");
    gen++;
    v = XhciVhubRootReset(&h, ALWAYS, gen, 0, PS_RESETTING);
    CHECK_EQ(v & XHCI_VHUB_DO_ARM_HUB, XHCI_VHUB_DO_ARM_HUB,
             "a root reset at 2 arms the hub's open");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_SUPERSEDED,
             "and overtakes the running reset");
    CHECK_EQ(h.ResetGeneration, first, "under its own generation still");
    CHECK_EQ(h.P1Resetting, 1, "(still running)");
    CHECK_EQ(XhciVhubResetDone(&h, ALWAYS, first, 0, XHCI_SPEED_FULL, PS_EN),
             XHCI_VHUB_DO_NONE, "its end is nobody's");
    CHECK_EQ(h.P1Resetting, 0, "the port free again");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_NONE, "(no reset owned)");
    CHECK_EQ(p1Change(&h) & XHCI_HUB_C_PORT_RESET, 0,
             "and no reset change on port 1");
    CHECK_EQ(XhciVhubClaimOpen(&h), 1, "the hub's open still armed");

    /* The same end as a deadline. */
    readyHub(&h, ALWAYS);
    gen++;
    first = gen;
    XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen, 0, PS_EN);
    gen++;
    XhciVhubRootReset(&h, ALWAYS, gen, 0, PS_RESETTING);
    CHECK_EQ(XhciVhubResetDone(&h, ALWAYS, first, 1, XHCI_SPEED_UNKNOWN,
                               PS_CONN),
             XHCI_VHUB_DO_NONE, "a deadline on the overtaken reset: nothing");
    CHECK_EQ(h.P1Resetting, 0, "(ended)");

    readyHub(&h, ON_DEMAND);
    gen++;
    first = gen;
    CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen, 0,
                                  PS_EN),
             XHCI_VHUB_DO_PHYS_RESET | XHCI_VHUB_DO_ARM_DEVICE,
             "(port 1's reset starts at 1)");
    gen++;
    CHECK_EQ(XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_RESETTING),
             XHCI_VHUB_DO_PHYS_RESET,
             "a root reset at 1 is asked for, so the busy port refuses it");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_PORT1, "with nothing changing hands");
    CHECK_EQ(h.ResetGeneration, first, "(port 1's generation)");
    CHECK_EQ(h.UpResetting, 0, "(the upstream not resetting)");
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 1, XHCI_SPEED_UNKNOWN,
                               PS_RESETTING),
             XHCI_VHUB_DO_NONE, "the refusal's own end matches no reset");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_PORT1, "(port 1's still owned)");
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, first, 0, XHCI_SPEED_FULL,
                               PS_EN),
             XHCI_VHUB_DO_PIPE, "and port 1's reset ends as its own");
    CHECK_EQ(h.P1Enabled, 1, "port 1 enabled by it");

    /* A second reset of the same view while the first runs: asked for
     * untaken, so the busy port refuses it, and the first keeps its
     * ownership - its end is where the decision is taken. (A first build
     * overwrote the ownership before the refusal, and the first reset's
     * end then decided nothing.) */
    XhciVhubStart(&h, ON_DEMAND, 0);
    gen++;
    first = gen;
    CHECK_EQ(XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_CONN),
             XHCI_VHUB_DO_PHYS_RESET, "(a device's first root reset)");
    gen++;
    CHECK_EQ(XhciVhubRootReset(&h, ON_DEMAND, gen, 0, PS_RESETTING),
             XHCI_VHUB_DO_PHYS_RESET,
             "a second root reset while it runs is asked for untaken");
    CHECK_EQ(h.ResetGeneration, first, "the first keeps its ownership");
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 1, XHCI_SPEED_UNKNOWN,
                               PS_RESETTING),
             XHCI_VHUB_DO_NONE, "the refusal's end matches nothing");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_ROOT, "(still owned)");
    v = XhciVhubResetDone(&h, ON_DEMAND, first, 0, XHCI_SPEED_FULL, PS_EN);
    CHECK_EQ(v & XHCI_VHUB_DO_ARM_HUB, XHCI_VHUB_DO_ARM_HUB,
             "and the first reset's end stands the hub up");
    CHECK_EQ(h.Present, 1, "(present)");

    readyHub(&h, ON_DEMAND);
    gen++;
    first = gen;
    XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen, 0, PS_EN);
    gen++;
    CHECK_EQ(XhciVhubPort1Feature(&h, 1, XHCI_VHUB_SEL_PORT_RESET, gen, 0,
                                  PS_RESETTING),
             XHCI_VHUB_DO_PHYS_RESET,
             "a second port-1 reset while the first runs, untaken too");
    CHECK_EQ(h.ResetGeneration, first, "the first keeps its ownership");
    CHECK_EQ(h.ResetOwner, XHCI_VHUB_OWNER_PORT1, "(port 1's)");
    CHECK_EQ(h.P1Resetting, 1, "(running)");
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, gen, 1, XHCI_SPEED_UNKNOWN,
                               PS_RESETTING),
             XHCI_VHUB_DO_NONE, "the refusal's end matches nothing");
    CHECK_EQ(XhciVhubResetDone(&h, ON_DEMAND, first, 0, XHCI_SPEED_FULL,
                               PS_EN),
             XHCI_VHUB_DO_PIPE, "and the first ends as port 1's");
    CHECK_EQ(h.P1Enabled, 1, "port 1 enabled by it");
}

/*
 * The virtual records live outside the graph (3.3): more than eight of them
 * cost the graph no node, a real hub still gets one, and the TT a device
 * behind a real hub behind a virtual one is given is the real hub's (3.6).
 */
static void testArrayAndGraph(void)
{
    XHCI_VHUB hubs[12];
    XHCI_TOPOLOGY topo;
    XHCI_TOPO_TT tt;
    XHCI_TOPO_CHILD child;
    ULONG i;

    for (i = 0; i < 12; i++) {
        XhciVhubStart(&hubs[i], ALWAYS, 0);
        gen++;
        XhciVhubRootReset(&hubs[i], ALWAYS, gen, 0, PS_EMPTY);
        enumerateHub(&hubs[i], ALWAYS, 10 + i);
    }
    CHECK_EQ(XhciVhubFindAddress(hubs, 12, 10), 1, "the first");
    CHECK_EQ(XhciVhubFindAddress(hubs, 12, 21), 12, "the twelfth");
    CHECK_EQ(XhciVhubFindAddress(hubs, 12, 30), 0, "a real device's address");
    CHECK_EQ(XhciVhubFindAddress(hubs, 12, 0), 0, "address 0 is nobody's");
    CHECK_EQ(XhciVhubFindAddress(NULL, 12, 10), 0, "NULL array");

    /*
     * usbport's HubAddr for a Full-Speed device behind virtual hub 3 names
     * address 12: a virtual address, so the TT comparison counts the pair as
     * agreed, since the graph's answer is "no TT" for a root-port device.
     */
    CHECK(XhciVhubFindAddress(hubs, 12, 12) != 0,
          "usbport's HubAddr is recognised as a virtual hub");

    /* The graph is untouched by all twelve: a real High-Speed hub on root
     * port 3 (behind virtual hub 3 at 2) still takes a node. */
    XhciTopoReset(&topo);
    CHECK_EQ(XhciTopoAttachRoot(&topo, 40, 3, 1, XHCI_SPEED_HIGH), 1,
             "a real hub still gets a graph node");
    CHECK_EQ(XhciTopoTtFor(&topo, 40, 2, &tt), 1,
             "a Full or Low Speed device behind it has a TT");
    CHECK_EQ(tt.HubAddress, 40, "the real hub's, not the virtual one's");
    CHECK_EQ(tt.HubPort, 2, "on the real hub's port");
    CHECK_EQ(tt.MultiTt, 0, "single TT");
    CHECK_EQ(XhciVhubFindAddress(hubs, 12, tt.HubAddress), 0,
             "and the TT hub is never a virtual address");

    /* Multi TT: the real hub's own alternate setting, as today. */
    {
        XHCI_SETUP_PACKET s;
        XHCI_TOPO_SNOOP snoop;

        s = setupOf(0x23, 3, 8, 1, 0);
        XhciTopoObserveSetup(&topo, 40, &s, &snoop);
        XhciTopoApplySetInterface(&topo, 40, 1);
        CHECK_EQ(XhciTopoTtFor(&topo, 40, 2, &tt), 1, "(still a TT)");
        CHECK_EQ(tt.MultiTt, 1, "multi TT from the real hub");
        CHECK_EQ(tt.HubAddress, 40, "still the real hub");
    }

    /* A Full-Speed device on a root port is a root-port device to the
     * graph: whatever usbport names, the graph has no TT for it. */
    CHECK_EQ(XhciTopoTtFor(&topo, 12, 1, &tt), 0,
             "the virtual hub is not in the graph, so no TT");
    CHECK_EQ(XhciTopoChildOf(&topo, 40, 1, &child), 1,
             "(the real hub places its children as today)");
    CHECK_EQ(child.RootPort, 3, "on root port 3");
}

/* The status-change pipe's byte (3.4). */
static void testPipeByte(void)
{
    XHCI_VHUB h;
    ULONG strip;

    CHECK_EQ(XhciVhubPipeByte(NULL), 0, "NULL");
    v1Plug(&h, XHCI_SPEED_FULL);
    CHECK_EQ(XhciVhubPipeByte(&h), 0,
             "a change latched before the hub is configured is held");
    enumerateHub(&h, ON_DEMAND, 3);
    CHECK_EQ(XhciVhubPipeByte(&h), 0x02,
             "and sent once it is: bit 1, port 1");
    XhciVhubPort1Feature(&h, 0, XHCI_VHUB_SEL_C_PORT_CONNECTION, 0, 0, PS_EN);
    CHECK_EQ(XhciVhubPipeByte(&h), 0, "no change: the transfer stays pending");
    XhciVhubAbsorb(&h, ON_DEMAND, XHCI_HUB_C_PORT_OVER_CURRENT,
                   PS_CONN | XHCI_HUB_PORT_OVER_CURRENT, &strip);
    CHECK_EQ(XhciVhubPipeByte(&h), 0x02, "an over-current is a change");
}

int main(void)
{
    testIdAccepted();
    testIdRefused();
    testConfig();
    testDescriptors();
    testStatusRequests();
    testStandardAndClassVerdicts();
    testOffState();
    testDecision();
    testRedecision();
    testHubEnumeration();
    testRootReportV1();
    testSuspendMerge();
    testPort1Power();
    testHeldResetEndsAtPort1Disable();
    testUnplugRetiresV1();
    testRootResetHeldV1();
    testValue2Lifecycle();
    testRootResetOverPort1Reset();
    testArrayAndGraph();
    testPipeByte();

    printf("%d checks, %d failures\n", checks, failures);
    return failures;
}
