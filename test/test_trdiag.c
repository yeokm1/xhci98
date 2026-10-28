/*
 * test_trdiag.c - XHCITR's PCI and bridge diagnostics (design record 25
 * section 20, revision 23; roadmap-usb4 task 44.62), over
 * xhcitr\tr_diag.c: XHCIPFW's value table against design record 32
 * sections 6 and 11.2; its values decoded on 98 SE and on NT, present and
 * absent, the result bits with an unknown one, each bridge action, a
 * 64-bit window and a closed one, a value of the wrong type, a length
 * that is not a whole number of records and a table past its bound;
 * XhciCmBridgeDiag for each outcome, the two "not attempted" flags and a
 * short value; XhciCmNoIrqDiag with a refusal mix and a short value; task
 * 44.53's records picked from a log with a cut first line and other
 * records; and no line over 79 columns on a worst case.
 */

#include <stdio.h>
#include <string.h>
#include "../xhcitr/tr_diag.h"
#include "test_harness.h"

static char text[65536];

static void put32(unsigned char *b, unsigned long v)
{
    b[0] = (unsigned char)(v & 0xFFUL);
    b[1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[3] = (unsigned char)((v >> 24) & 0xFFUL);
}

static void dv_dword(TR_DVAL *v, unsigned long x)
{
    memset(v, 0, sizeof(*v));
    v->State = TR_DV_OK;
    v->Type = TR_REG_DWORD;
    v->Len = 4;
    put32(v->Data, x);
}

static void dv_bin(TR_DVAL *v, const unsigned char *b, unsigned long n)
{
    memset(v, 0, sizeof(*v));
    v->State = TR_DV_OK;
    v->Type = TR_REG_BINARY;
    v->Len = n;
    memcpy(v->Data, b, n);
}

static void dv_sz(TR_DVAL *v, const char *s)
{
    memset(v, 0, sizeof(*v));
    v->State = TR_DV_OK;
    v->Type = TR_REG_SZ;
    v->Len = (unsigned long)strlen(s) + 1;
    memcpy(v->Data, s, v->Len);
}

static int has(const char *s)
{
    return strstr(text, s) != NULL;
}

/* No line of the text over 79 columns (CR LF ends each). */
static int cols_ok(void)
{
    const char *p = text;
    int col = 0;

    for (; *p; p++) {
        if (*p == '\r' || *p == '\n') {
            col = 0;
            continue;
        }
        if (++col > 79) {
            return 0;
        }
    }
    return 1;
}

static void test_names(void)
{
    static const char *const want[TR_PFW_COUNT] = {
        "XhciPfwVersion", "XhciPfwSkipped", "XhciPfwMode",
        "XhciPfwHoldTicks", "XhciPfwTickHz", "XhciPfwResult",
        "XhciPfwClosed", "XhciPfwRefused", "XhciPfwParked",
        "XhciPfwAcross", "XhciPfwBarsInside", "XhciPfwFunctions",
        "XhciPfwXhciHigh", "XhciPfwBridges", "XhciPfwBars", "XhciPfwXhci"
    };
    int i;

    for (i = 0; i < TR_PFW_COUNT; i++) {
        CHECK(tr_pfw_name(i) != NULL && strcmp(tr_pfw_name(i), want[i]) == 0,
              "names: record 32's");
    }
    CHECK(tr_pfw_name(TR_PFW_COUNT) == NULL, "names: the table's end");
    CHECK(tr_pfw_name(-1) == NULL, "names: below");
    CHECK_EQ(tr_pfw_type(TR_PFW_VERSION), TR_REG_SZ, "types: version");
    CHECK_EQ(tr_pfw_type(TR_PFW_BRIDGES), TR_REG_BINARY, "types: bridges");
    CHECK_EQ(tr_pfw_type(TR_PFW_CLOSED), TR_REG_DWORD, "types: closed");
    CHECK(tr_pfw_nt_only(TR_PFW_SKIPPED) && tr_pfw_nt_only(TR_PFW_TICKHZ) &&
          !tr_pfw_nt_only(TR_PFW_RESULT) && !tr_pfw_nt_only(TR_PFW_VERSION),
          "names: NT only");
    CHECK(strcmp(tr_irq_label(0), "cm.chain.irq") == 0, "labels: first");
    CHECK(tr_irq_label(7) == NULL && tr_irq_label(6) != NULL,
          "labels: seven");
}

static void test_shapes(void)
{
    char s[64];

    tr_diag_bdf(0x0508UL | 1UL, s, sizeof(s));
    CHECK(strcmp(s, "PCI 05:01.1") == 0, "bdf");
    tr_diag_bdf(0xFFFFFFFFUL, s, sizeof(s));
    CHECK(strcmp(s, "none") == 0, "bdf: none");
    tr_diag_memwin(0xE000E000UL, s, sizeof(s));
    CHECK(strcmp(s, "E0000000-E00FFFFF") == 0, "memwin");
    tr_diag_memwin(0x0000FFF0UL, s, sizeof(s));
    CHECK(strcmp(s, "closed") == 0, "memwin: closed");
    tr_diag_pfwin(0x0001FFF1UL, 0x00000001UL, 0x00000040UL, s, sizeof(s));
    CHECK(strcmp(s, "00000001_FFF00000-00000040_000FFFFF") == 0,
          "pfwin: 64-bit");
    tr_diag_pfwin(0x0000FFF0UL, 0, 0, s, sizeof(s));
    CHECK(strcmp(s, "closed") == 0, "pfwin: closed");
    tr_diag_pfwin(0x0000FFF1UL, 0xFFFFFFFFUL, 0, s, sizeof(s));
    CHECK(strcmp(s, "closed") == 0, "pfwin: closed by the upper base");
    /* A 32-bit window ignores 28h and 2Ch. */
    tr_diag_pfwin(0xC010C000UL, 5, 5, s, sizeof(s));
    CHECK(strcmp(s, "00000000_C0000000-00000000_C01FFFFF") == 0,
          "pfwin: 32-bit");
    CHECK(strcmp(tr_brdiag_outcome(12), tr_brdiag_outcome(99)) != 0,
          "outcome 12 known");
    CHECK(strstr(tr_brdiag_outcome(99), "not one") != NULL, "outcome: unknown");
    CHECK(strstr(tr_ni_why(16), "not one") != NULL, "why: unknown");
    CHECK(strstr(tr_ni_op(13), "line") != NULL, "op 13");
}

static void pfw_set(TR_DVAL *v, int nt)
{
    unsigned char br[3 * 28];
    unsigned char bar[8];
    unsigned char xh[16];
    int i;

    memset(v, 0, sizeof(TR_DVAL) * TR_PFW_COUNT);
    dv_sz(&v[TR_PFW_VERSION], nt ? "XHCIPFW.SYS 3.3.0.0" : "XHCIPFW.VXD 3.3.0.0");
    if (nt) {
        dv_dword(&v[TR_PFW_SKIPPED], 0);
        dv_dword(&v[TR_PFW_MODE], 2);
        dv_dword(&v[TR_PFW_HOLDTICKS], 35795);
        dv_dword(&v[TR_PFW_TICKHZ], 3579545);
    }
    dv_dword(&v[TR_PFW_RESULT], 0x51UL | 0x80UL);
    for (i = TR_PFW_CLOSED; i <= TR_PFW_XHCIHIGH; i++) {
        dv_dword(&v[i], (unsigned long)i);
    }
    memset(br, 0, sizeof(br));
    br[0] = 1; br[1] = 0; br[2] = 1;
    put32(br + 4, 0x0001FFF1UL); put32(br + 8, 1); put32(br + 12, 0x40);
    put32(br + 16, 0x0000FFF0UL);
    br[28] = 2; br[29] = (3 << 3) | 1; br[30] = 2;
    put32(br + 32, 0x0001FFF1UL); put32(br + 40, 2);
    put32(br + 44, 0x0001FFF1UL); put32(br + 52, 2);
    br[56] = 3; br[57] = 0; br[58] = 9;
    dv_bin(&v[TR_PFW_BRIDGES], br, sizeof(br));
    memset(bar, 0, sizeof(bar));
    bar[0] = 4; bar[1] = 0; bar[2] = 0x10;
    put32(bar + 4, 0x00000041UL);
    dv_bin(&v[TR_PFW_BARS], bar, sizeof(bar));
    memset(xh, 0, sizeof(xh));
    xh[0] = 5; xh[1] = 0; xh[2] = 0x10;
    put32(xh + 4, 0x24261B21UL); put32(xh + 8, 0x00000041UL);
    put32(xh + 12, 0xF0000000UL);
    dv_bin(&v[TR_PFW_XHCI], xh, sizeof(xh));
}

static void test_pfw(void)
{
    static TR_DVAL v[TR_PFW_COUNT];
    TR_TEXT t;

    pfw_set(v, 0);
    tr_text_init(&t, text, sizeof(text));
    tr_pfw_text(&t, TR_DK_OPEN, 0, v, 0);
    CHECK(!t.Overflow && cols_ok(), "pfw 98: fits, 79 columns");
    CHECK(has("Services\\VxD\\XHCIPFW"), "pfw 98: the key");
    CHECK(has("\"XHCIPFW.VXD 3.3.0.0\""), "pfw 98: version");
    CHECK(!has("XhciPfwSkipped"), "pfw 98: no NT values");
    CHECK(has("XhciPfwResult      000000D1"), "pfw 98: result raw");
    CHECK(has("bit 0: the scan finished"), "pfw 98: bit 0");
    CHECK(has("bit 4: a window across 4 GB"), "pfw 98: bit 4");
    CHECK(has("bit 6: an xHCI controller's BAR"), "pfw 98: bit 6");
    CHECK(has("bit 7: not one this tool knows"), "pfw 98: unknown bit");
    CHECK(has("XhciPfwClosed      00000006  windows closed: 6"),
          "pfw 98: a count");
    CHECK(has("XhciPfwXhciHigh    0000000C  xHCI BARs above 4 GB: 12"),
          "pfw 98: the last count");
    CHECK(has("XhciPfwBridges     3 records (84 bytes)"), "pfw 98: records");
    CHECK(has("PCI 01:00.0: closed"), "pfw 98: closed");
    CHECK(has("PCI 02:03.1: refused (undone)"), "pfw 98: refused");
    CHECK(has("PCI 03:00.0: action 9, not one this tool knows"),
          "pfw 98: unknown action");
    CHECK(has("prefetchable window 00000001_FFF00000-00000040_000FFFFF"),
          "pfw 98: window before");
    CHECK(has("prefetchable window closed"), "pfw 98: window after");
    CHECK(has("PCI 04:00.0 BAR 10h, upper dword 00000041"), "pfw 98: bar");
    CHECK(has("PCI 05:00.0 1B21:2426 BAR 10h at 00000041_F0000000"),
          "pfw 98: xhci");

    pfw_set(v, 1);
    tr_text_init(&t, text, sizeof(text));
    tr_pfw_text(&t, TR_DK_OPEN, 0, v, 1);
    CHECK(!t.Overflow && cols_ok(), "pfw nt: fits, 79 columns");
    CHECK(has("Services\\xhcipfw\\Parameters"), "pfw nt: the key");
    CHECK(has("XhciPfwSkipped     00000000  it ran"), "pfw nt: skipped");
    CHECK(has("run inside KeIpiGenericCall"), "pfw nt: mode");
    CHECK(has("about 10000 microseconds"), "pfw nt: the hold");

    dv_dword(&v[TR_PFW_SKIPPED], 0x12UL);
    memset(&v[TR_PFW_TICKHZ], 0, sizeof(TR_DVAL));
    tr_text_init(&t, text, sizeof(text));
    tr_pfw_text(&t, TR_DK_OPEN, 0, v, 1);
    CHECK(has("it did nothing"), "pfw nt skipped: said");
    CHECK(has("bit 1: a Safe Mode start"), "pfw nt skipped: bit 1");
    CHECK(has("bit 4: not one this tool knows"), "pfw nt skipped: unknown");
    CHECK(has("XhciPfwTickHz      not recorded"), "pfw nt: absent value");
    CHECK(!has("microseconds"), "pfw nt: no hold without the frequency");

    /* Bad shapes: a dword of another type, records not whole, too many. */
    v[TR_PFW_CLOSED].Type = TR_REG_BINARY;
    v[TR_PFW_BARS].Len = 12;
    v[TR_PFW_XHCI].Len = 9 * 16;
    v[TR_PFW_BRIDGES].State = TR_DV_LONG;
    tr_text_init(&t, text, sizeof(text));
    tr_pfw_text(&t, TR_DK_OPEN, 0, v, 1);
    CHECK(has("type 3, 4 bytes: not a layout this tool reads"),
          "pfw: wrong type");
    CHECK(has("type 3, 12 bytes: not a layout"), "pfw: not whole");
    CHECK(has("type 3, 144 bytes: not a layout"), "pfw: past the bound");
    CHECK(has("longer than 512 bytes"), "pfw: too long");
    CHECK(!t.Overflow && cols_ok(), "pfw bad: 79 columns");

    dv_sz(&v[TR_PFW_VERSION],
          "XHCIPFW.VXD 3.3.0.0 abcdefghijklmnopqrstuvwxyz0123456789");
    tr_text_init(&t, text, sizeof(text));
    tr_pfw_text(&t, TR_DK_OPEN, 0, v, 1);
    CHECK(cols_ok(), "pfw: a 56-character version within 79 columns");
    CHECK(has("XhciPfwVersion\r\n    \"XHCIPFW.VXD"), "pfw: long version");

    tr_text_init(&t, text, sizeof(text));
    tr_pfw_text(&t, TR_DK_ABSENT, 2, v, 0);
    CHECK(has("Not recorded: XHCIPFW is not installed"), "pfw: no key");
    CHECK(!has("XhciPfwResult"), "pfw: no key, no values");
    tr_text_init(&t, text, sizeof(text));
    tr_pfw_text(&t, TR_DK_ERROR, 5, v, 1);
    CHECK(has("could not be read (error 5)"), "pfw: key error");

    memset(v, 0, sizeof(v));
    tr_text_init(&t, text, sizeof(text));
    tr_pfw_text(&t, TR_DK_OPEN, 0, v, 0);
    CHECK(has("XhciPfwResult      not recorded"), "pfw: empty key");
    CHECK(has("XhciPfwBridges     not recorded"), "pfw: empty key records");
}

static void brdiag(unsigned char *b, unsigned long outcome,
                   unsigned long flags)
{
    memset(b, 0, 80);
    put32(b, 7);
    put32(b + 4, 0x0508UL);
    put32(b + 8, outcome);
    put32(b + 12, (2UL << 16) | (2UL << 8) | 1UL);
    put32(b + 16, 0x00080605UL);
    put32(b + 20, 1);
    put32(b + 24, 0xE010D410UL);
    put32(b + 28, 0x00200000UL);
    put32(b + 32, 0xFFFFFFFFUL);
    put32(b + 36, (1UL << 16) | 2UL);
    put32(b + 40, 0x0600UL);
    put32(b + 44, 0x0700UL);
    put32(b + 48, 0x00080706UL);
    put32(b + 52, 0x00080807UL);
    put32(b + 56, 0xE000E000UL);
    put32(b + 60, 0xE010E010UL);
    put32(b + 64, flags);
    put32(b + 68, 0x15EB8086UL);
    put32(b + 72, 1UL | (11UL << 8) | (1UL << 16));
    put32(b + 76, 1UL | (1UL << 8));
}

static void test_brdiag(void)
{
    static TR_DVAL v;
    unsigned char b[80];
    TR_TEXT t;
    unsigned long o;

    for (o = 0; o <= 13; o++) {
        brdiag(b, o, 4);
        dv_bin(&v, b, 80);
        tr_text_init(&t, text, sizeof(text));
        tr_brdiag_text(&t, &v, 0);
        CHECK(!t.Overflow && cols_ok(), "brdiag: 79 columns");
        CHECK(strstr(text, tr_brdiag_outcome(o)) != NULL, "brdiag: outcome");
    }
    brdiag(b, 1, 4);
    dv_bin(&v, b, 80);
    tr_text_init(&t, text, sizeof(text));
    tr_brdiag_text(&t, &v, 0);
    CHECK(has("Attempt 7, at the tunnel port PCI 05:01.0"), "brdiag: head");
    CHECK(has("Seen: 2 functions on the port's bus, 2 bridges, 1 try"),
          "brdiag: seen");
    CHECK(has("primary 05, secondary 06,\r\n      subordinate 08"),
          "brdiag: buses");
    CHECK(has("(20h E010D410): D4100000-E01FFFFF"), "brdiag: port window");
    CHECK(has("the port's line 11"), "brdiag: the line, the stage run");
    CHECK(has("needed 00200000 bytes"), "brdiag: need");
    CHECK(has("Where it stopped: none"), "brdiag: where");
    CHECK(has("written back: 2; could not be: 1"), "brdiag: undone");
    CHECK(has("Bridge 2, PCI 07:00.0: 18h 00080807, 20h E010E010"),
          "brdiag: bridge 2");
    CHECK(has("buses 06/07/08, memory window E0000000-E00FFFFF"),
          "brdiag: bridge 1's window");
    CHECK(has("the port's line 11, pin INTA"), "brdiag: irq");
    CHECK(has("given the port's line 1, already set 0"), "brdiag: irq counts");
    CHECK(has("The port read up: yes"), "brdiag: up");
    CHECK(has(" 0: 00000007 00000508"), "brdiag: raw");
    CHECK(has("15: E010E010 00000004 15EB8086"), "brdiag: raw, last line");

    /* The window and line stages not run (outcome 0 each): their unread
     * registers not shown. */
    brdiag(b, 0, 4);
    put32(b + 20, 0);
    put32(b + 72, 0);
    dv_bin(&v, b, 80);
    tr_text_init(&t, text, sizeof(text));
    tr_brdiag_text(&t, &v, 0);
    CHECK(has("Windows 0: not asked"), "brdiag: windows not asked");
    CHECK(!has("memory window (20h"), "brdiag: no unread window");
    CHECK(!has("the port's line"), "brdiag: no unread line");

    /* A failed read (5 and 3): the fields it may have left unread are
     * said as not recorded; the port closed (2): its window, no need. */
    brdiag(b, 7, 4);
    put32(b + 20, 5);
    put32(b + 72, 3);
    dv_bin(&v, b, 80);
    tr_text_init(&t, text, sizeof(text));
    tr_brdiag_text(&t, &v, 0);
    CHECK(has("window and the need: not recorded"), "brdiag: window access");
    CHECK(has("line and the counts: not recorded"), "brdiag: irq access");
    CHECK(!has("memory window (20h") && !has("needed ") &&
          !has("the port's line 11"), "brdiag: access, nothing unread");
    CHECK(cols_ok(), "brdiag: access, 79 columns");
    brdiag(b, 1, 4);
    put32(b + 20, 2);
    put32(b + 72, 2UL | (0xFFUL << 8));
    dv_bin(&v, b, 80);
    tr_text_init(&t, text, sizeof(text));
    tr_brdiag_text(&t, &v, 0);
    CHECK(has("memory window (20h E010D410)") && !has("needed ") &&
          !has("the need"), "brdiag: port closed, no need");
    for (o = 3; o <= 6; o++) {
        if (o == 5) {
            continue;
        }
        put32(b + 20, o);
        dv_bin(&v, b, 80);
        tr_text_init(&t, text, sizeof(text));
        tr_brdiag_text(&t, &v, 0);
        CHECK(has("memory window (20h E010D410)") &&
              has("the need: not recorded whole") && !has("needed "),
              "brdiag: a refusal's need not given as whole");
    }
    CHECK(has("the port's line unset (FFh), pin none") &&
          !has("functions with a pin"), "brdiag: port has no line");

    brdiag(b, 0, 1);
    dv_bin(&v, b, 80);
    tr_text_init(&t, text, sizeof(text));
    tr_brdiag_text(&t, &v, 0);
    CHECK(has("Not attempted: a device below the port"), "brdiag: held");
    CHECK(!has("Outcome"), "brdiag: held, no outcome");
    brdiag(b, 0, 2);
    dv_bin(&v, b, 80);
    tr_text_init(&t, text, sizeof(text));
    tr_brdiag_text(&t, &v, 0);
    CHECK(has("failed its check (read\r\n    15EB8086)"), "brdiag: guard");

    dv_bin(&v, b, 72);
    tr_text_init(&t, text, sizeof(text));
    tr_brdiag_text(&t, &v, 0);
    CHECK(has("type 3, 72 bytes: not a layout this tool reads"),
          "brdiag: short");
    memset(&v, 0, sizeof(v));
    tr_text_init(&t, text, sizeof(text));
    tr_brdiag_text(&t, &v, 1);
    CHECK(has("Not recorded (written on Windows 98 SE and ME only)"),
          "brdiag: NT");
    tr_text_init(&t, text, sizeof(text));
    tr_brdiag_text(&t, &v, 0);
    CHECK(has("Not recorded: no attempt"), "brdiag: 98, none");
}

static void test_nidiag(void)
{
    static TR_DVAL v;
    unsigned char b[48];
    TR_TEXT t;

    memset(b, 0, sizeof(b));
    put32(b, 3);
    put32(b + 4, 0x0508UL);
    put32(b + 8, 0x0700UL);
    put32(b + 12, 1UL | (0xFFUL << 8));
    put32(b + 16, 7UL | (5UL << 8) | (0UL << 16));
    put32(b + 20, 12);
    put32(b + 24, (12UL << 16) | 0UL);
    put32(b + 28, 0x0000000AUL);
    put32(b + 32, 0);
    put32(b + 36, 2UL | (3UL << 16));
    put32(b + 40, 0x1UL | (0xFUL << 4) | (2UL << 28));
    put32(b + 44, 1UL | (1UL << 16));
    dv_bin(&v, b, 48);
    tr_text_init(&t, text, sizeof(text));
    tr_nidiag_text(&t, &v, 0);
    CHECK(!t.Overflow && cols_ok(), "nidiag: 79 columns");
    CHECK(has("Record 3, the tunnel port PCI 05:01.0, the function PCI 07:00.0"),
          "nidiag: head");
    CHECK(has("Its pin INTA, line unset (FFh)"), "nidiag: pin");
    CHECK(has("Interrupt 7: routable"), "nidiag: irq");
    CHECK(has("State 5: done (started); reason 0: none"), "nidiag: state");
    CHECK(has("Problem before: 12"), "nidiag: before");
    CHECK(has("Step reached 12: Setup_DevNode, CONFIGRET 0"), "nidiag: step");
    CHECK(has("status 0000000A (started), problem 0"), "nidiag: after");
    CHECK(has("created 2; candidates 3"), "nidiag: counts");
    CHECK(has("1, XhciCmNoIrqStart is 0: 1"), "nidiag: refusal 1");
    CHECK(has("2, started, or another problem: 15"), "nidiag: refusal 2");
    CHECK(has("8, attempts used up: 2"), "nidiag: refusal 8");
    CHECK(has("no pin: 1 seen, 1 started"), "nidiag: pin 0");
    CHECK(has("10: 200000F1 00010001"), "nidiag: raw");

    put32(b + 40, 0);
    dv_bin(&v, b, 48);
    tr_text_init(&t, text, sizeof(text));
    tr_nidiag_text(&t, &v, 0);
    CHECK(has("held at 15): none"), "nidiag: no refusals");
    dv_bin(&v, b, 44);
    tr_text_init(&t, text, sizeof(text));
    tr_nidiag_text(&t, &v, 0);
    CHECK(has("44 bytes: not a layout"), "nidiag: short");
    /* An exclusion: no status after recorded. */
    put32(b + 28, 0);
    put32(b + 32, 0);
    put32(b + 16, 0UL | (2UL << 8) | (2UL << 16));
    dv_bin(&v, b, 48);
    tr_text_init(&t, text, sizeof(text));
    tr_nidiag_text(&t, &v, 0);
    CHECK(has("State 2: excluded; reason 2: started, or another problem"),
          "nidiag: excluded");
    CHECK(has("After: not recorded"), "nidiag: exclusion, no status after");
    CHECK(!has("not started"), "nidiag: exclusion not called not started");
    v.Type = TR_REG_DWORD;
    v.Len = 48;
    tr_text_init(&t, text, sizeof(text));
    tr_nidiag_text(&t, &v, 0);
    CHECK(has("type 4, 48 bytes: not a layout"), "nidiag: wrong type");
}

static void test_irq(void)
{
    static const char log[] =
        "irq=000B0700\r\n"
        "cm.chain.attach=00000001\r\n"
        "cm.chain.irq=000B0700\r\n"
        "cm.chain.irqx=000B0700\r\n"
        "cm.chain.irq.no=00030708\r\n"
        "cm.chain.irq.filter=000A0700\r\n"
        "cm.chain.irq.nomem=00000700\r\n"
        "cm.chain.irq.start.refused=00000700\r\n"
        "cm.chain.irq.start.nomem=00000700\r\n"
        "cm.chain.irq.start.stripped=000B0700\r\n"
        "cm.chain.irq=0B07\r\n"
        "cm.chain.irq=000B07";
    TR_TEXT t;
    int n;

    tr_text_init(&t, text, sizeof(text));
    n = tr_irq_lines(&t, (const unsigned char *)log,
                     (TR_U32)(sizeof(log) - 1), 4, TR_IRQ_SHOWN);
    CHECK_EQ(n, 7, "irq: seven records");
    CHECK(!t.Overflow && cols_ok(), "irq: 79 columns");
    CHECK(has("    cm.chain.irq=000B0700\r\n      PCI 07:00.0: line 11, added"),
          "irq: added");
    CHECK(has("PCI 07:01.0: no interrupt added:\r\n        the port's line is "
              "not usable"), "irq: no, why");
    CHECK(has("PCI 07:00.0: line 10, added at the filter request"),
          "irq: filter");
    CHECK(has("its start refused"), "irq: refused");
    CHECK(has("line 11, the added interrupt taken out of its start"),
          "irq: stripped");
    CHECK(!has("irqx"), "irq: another label left");
    CHECK(!has("=0B07\r"), "irq: a short value left");
    tr_text_init(&t, text, sizeof(text));
    CHECK_EQ(tr_irq_lines(&t, (const unsigned char *)"", 0, 4, TR_IRQ_SHOWN),
             0, "irq: empty log");
}

/* Every reason at the deepest indent, and a ring full of records: the
 * last TR_IRQ_SHOWN shown, the count said, the text bounded. */
static void test_irq_full(void)
{
    static char ring[16384];
    char line[48];
    TR_TEXT t;
    size_t at = 0;
    unsigned long k;
    int n;

    for (k = 0; at + 40 < sizeof(ring); k++) {
        tr_sprintf(line, sizeof(line), "cm.chain.irq.no=%04lX%04lX\r\n",
                   k % 10UL, k & 0xFFFFUL);
        memcpy(ring + at, line, strlen(line));
        at += strlen(line);
    }
    tr_text_init(&t, text, sizeof(text));
    n = tr_irq_lines(&t, (const unsigned char *)ring, (TR_U32)at, 4,
                     TR_IRQ_SHOWN);
    CHECK_EQ((unsigned long)n, k, "irq full: every record counted");
    CHECK(!t.Overflow && cols_ok(), "irq full: fits, 79 columns");
    tr_sprintf(line, sizeof(line), "(the last 16 of %lu records)", k);
    CHECK(has(line), "irq full: the count said");
    CHECK(strlen(text) < 4096, "irq full: bounded");
    for (k = 0; k <= 9; k++) {
        tr_sprintf(line, sizeof(line), "cm.chain.irq.no=%04lX0700", k);
        tr_text_init(&t, text, sizeof(text));
        (void)tr_irq_lines(&t, (const unsigned char *)line,
                           (TR_U32)strlen(line), 4, TR_IRQ_SHOWN);
        CHECK(cols_ok(), "irq: each reason within 79 columns");
    }
}

static void test_node(void)
{
    TR_TEXT t;

    tr_text_init(&t, text, sizeof(text));
    tr_diag_head(&t);
    tr_diag_nhi(&t, "A Thunderbolt or USB4 host interface whose description "
                    "runs well past the width of one line of the pane",
                "PCI\\VEN_8086&DEV_15EB&SUBSYS_00000000&REV_06\\"
                "4&2D2BB2E9&0&00E0&AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    CHECK(!t.Overflow && cols_ok(), "node: 79 columns");
    CHECK(has("Edit > Copy with nothing selected"), "node: the copy hint");
    CHECK(has("    PCI\\VEN_8086"), "node: the id");
}

int main(void)
{
    test_names();
    test_shapes();
    test_pfw();
    test_brdiag();
    test_nidiag();
    test_irq();
    test_irq_full();
    test_node();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
