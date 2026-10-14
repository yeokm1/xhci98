/*
 * test_route.c - host-side unit tests for xhciqual/route.c, the Intel PCH
 * interrupt routing report (roadmap-hcd.md task 36.11).
 *
 * No QEMU guest has these registers: its xHCI is 1B36:000D and its LPC is
 * not an Intel PCH's, so the matrix only ever reaches "not applicable".
 * Every other branch is first run here. The E7250's own reading (issue 12
 * section 3.5: LPC 9CC3, RCBA FED1C001, D20IP 1, D20IR 3210, Windows on
 * GSI 21) is pinned as the mismatch the report exists to show.
 *
 * Task 39-0.6 adds the BIOS $PIR table (route.c's second half): a valid
 * table, 00:1C listed and not, a bad checksum, a truncated table and
 * header, bad sizes and version, no table at all, and the entry cap.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework, no DOS dependencies.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../qual.h"

static char out[16384];
static int failures;
static int checks;

static void cap(const char *fmt, ...)
{
    va_list ap;
    size_t used;

    used = strlen(out);
    va_start(ap, fmt);
    vsprintf(out + used, fmt, ap);
    va_end(ap);
}

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL line %d: %s\n", line, what);
        printf("  output was: %s", out[0] ? out : "(empty)\n");
    }
}

static int has(const char *needle)
{
    return strstr(out, needle) != 0;
}

/* The E7250 as read under XP x64 (issue 12 sections 2 and 3.5). The
 * PIRQx_ROUT bytes are not in that reading; these are a plausible APIC-mode
 * set (every PIRQ's IRQEN set), with PIRQF's routed to IRQ 11 for the PIC
 * vectors. */
static void e7250(ROUTEINFO *r)
{
    memset(r, 0, sizeof(*r));
    r->state = ROUTE_READ;
    r->xhci_id = 0x9CB18086UL;
    r->xhci_pin = 1;
    r->lpc_id = 0x9CC38086UL;
    r->rcba = 0xFED1C001UL;
    r->pirq_ad = 0x80808080UL;
    r->pirq_eh = 0x80800B80UL;
    r->d20ip = 0x00000001UL;
    r->d20ir = 0x3210;
}

static void test_scope(void)
{
    CHECK(route_scope(0x9CB18086UL, 0, 0x14, 0) == ROUTE_READ,
          "E7250 xHCI at 00:14.0 is in scope");
    CHECK(route_scope(0x8C318086UL, 0, 0x14, 0) == ROUTE_READ,
          "8 series xHCI in scope");
    CHECK(route_scope(0x8D318086UL, 0, 0x14, 0) == ROUTE_READ,
          "C610 xHCI in scope");
    CHECK(route_scope(0xA12F8086UL, 0, 0x14, 0) == ROUTE_NA_XHCI,
          "100 series (Sunrise Point) xHCI is not: ITSS, not RCBA");
    CHECK(route_scope(0x0F358086UL, 0, 0x14, 0) == ROUTE_NA_XHCI,
          "Bay Trail xHCI is not: iLB");
    CHECK(route_scope(0x000D1B36UL, 0, 0x04, 0) == ROUTE_NA_XHCI,
          "QEMU's xHCI is not");
    CHECK(route_scope(0x9CB11022UL, 0, 0x14, 0) == ROUTE_NA_XHCI,
          "another vendor with the same device id is not");
    CHECK(route_scope(0x9CB18086UL, 1, 0x14, 0) == ROUTE_NA_SLOT,
          "a reviewed id on another bus is not at the slot");
    CHECK(route_scope(0x9CB18086UL, 0, 0x14, 1) == ROUTE_NA_SLOT,
          "a reviewed id at another function is not at the slot");

    CHECK(route_lpc_listed(0x9CB18086UL, 0x9CC38086UL), "E7250 LPC listed");
    CHECK(!route_lpc_listed(0x9CB18086UL, 0x9CC18086UL),
          "an engineering-sample LPC is not");
    CHECK(!route_lpc_listed(0x9CB18086UL, 0x8CC38086UL),
          "another pair's LPC is not this xHCI's");

    CHECK(route_rcba_valid(0xFED1C001UL), "enabled RCBA valid");
    CHECK(!route_rcba_valid(0xFED1C000UL), "disabled RCBA");
    CHECK(!route_rcba_valid(0x00000001UL), "zero base");
    CHECK(!route_rcba_valid(0xFFFFFFFFUL), "all ones");
}

static void test_pirq(void)
{
    ROUTEINFO r;

    e7250(&r);
    CHECK(route_pirq(&r) == 0, "E7250 INTA routes to PIRQA");
    r.d20ir = 0x3215;
    CHECK(route_pirq(&r) == 5, "after the live write INTA routes to PIRQF");
    r.d20ip = 2;
    r.d20ir = 0x3270;
    CHECK(route_pirq(&r) == 7, "INTB's field is 6:4");
    r.d20ip = 4;
    r.d20ir = 0x6000;
    CHECK(route_pirq(&r) == 6, "INTD's field is 14:12");
    r.d20ip = 0;
    CHECK(route_pirq(&r) == -1, "no pin, no PIRQ");
    r.d20ip = 9;
    CHECK(route_pirq(&r) == -1, "a pin outside 1-4, no PIRQ");
    e7250(&r);
    r.state = ROUTE_RCBA_INVALID;
    CHECK(route_pirq(&r) == -1, "nothing read, no PIRQ");
}

static void test_compare(void)
{
    ROUTEINFO r;

    e7250(&r);
    CHECK(route_compare(&r, -1) == ROUTE_CMP_NONE, "no GSI, no comparison");
    CHECK(route_compare(&r, 21) == ROUTE_CMP_MISMATCH,
          "the E7250's mismatch: PIRQA against GSI 21");
    CHECK(route_compare(&r, 16) == ROUTE_CMP_MATCH, "GSI 16 is PIRQA");
    r.d20ir = 0x3215;
    CHECK(route_compare(&r, 21) == ROUTE_CMP_MATCH,
          "after the live write GSI 21 matches");

    /* PIC: PIRQF's ROUT routes IRQ 11. */
    CHECK(route_compare(&r, 11) == ROUTE_CMP_MATCH,
          "PIRQF routed to IRQ 11 matches IRQ 11");
    CHECK(route_compare(&r, 10) == ROUTE_CMP_MISMATCH,
          "PIRQF routed to IRQ 11 does not deliver IRQ 10");
    r.pirq_eh = 0x80808B80UL;
    CHECK(route_compare(&r, 11) == ROUTE_CMP_MISMATCH,
          "IRQEN set: not routed to the PIC at all");
    CHECK(route_compare(&r, 13) == ROUTE_CMP_NOT_COMPARED,
          "IRQ 13 is a reserved encoding, not compared");
    CHECK(route_compare(&r, 24) == ROUTE_CMP_NOT_COMPARED,
          "GSI 24 is in neither range");
    r.d20ip = 0;
    CHECK(route_compare(&r, 21) == ROUTE_CMP_NOT_COMPARED,
          "no PIRQ, nothing compared");
}

static void test_report(void)
{
    ROUTEINFO r;

    out[0] = '\0';
    e7250(&r);
    report_route(&r, 21, cap);
    CHECK(has("LPC 00:1F.0 8086:9CC3"), "LPC identity");
    CHECK(has("RCBA (F0h)=FED1C001: base FED1C000, enabled"), "RCBA");
    CHECK(has("D20IP (RCBA+3128h)=00000001: xHCI pin INTA"), "D20IP");
    CHECK(has(", agrees"), "pin agreement");
    CHECK(has("D20IR (RCBA+3160h)=3210"), "D20IR");
    CHECK(has("INTA -> PIRQA (GSI 16 in APIC mode)"), "selected PIRQ");
    CHECK(has("PIRQA_ROUT=80: IRQEN set, not routed to the PIC"),
          "PIC destination and its disable bit");
    CHECK(has("GSI 21 given: MISMATCH - the pin reaches PIRQA (GSI 16), "
              "and GSI 21 is PIRQF"), "the mismatch named");

    out[0] = '\0';
    report_route(&r, -1, cap);
    CHECK(has("no GSI given"), "no GSI: said, nothing flagged");
    CHECK(!has("MISMATCH"), "no GSI: no mismatch");

    out[0] = '\0';
    r.d20ir = 0x3215;
    report_route(&r, 11, cap);
    CHECK(has("PIRQF_ROUT=0B: routed to the PIC, IRQ 11"), "routed ROUT");
    CHECK(has("IRQ 11 given: MATCH on the 8259 path - PIRQF_ROUT routes it "
              "to IRQ 11, as DOS found it"), "PIC match, scoped to DOS");
    CHECK(has("proves nothing for one"),
          "a low IRQ says nothing for an NT APIC HAL");
    CHECK(strcmp(route_summary(&r, 11),
                 "routing MATCHES the IRQ given on the 8259 path, as DOS "
                 "found it") == 0, "summary, PIC match scoped");

    out[0] = '\0';
    report_route(&r, 10, cap);
    CHECK(has("IRQ 10 given: MISMATCH on the 8259 path - PIRQF_ROUT does "
              "not route it to IRQ 10, as DOS found it"), "PIC mismatch");
    CHECK(strcmp(route_summary(&r, 10),
                 "routing MISMATCH against the IRQ given on the 8259 path, "
                 "as DOS found it") == 0, "summary, PIC mismatch scoped");

    out[0] = '\0';
    report_route(&r, 13, cap);
    CHECK(has("GSI 13 given: not compared"), "a reserved IRQ, not compared");

    out[0] = '\0';
    r.xhci_pin = 2;
    report_route(&r, -1, cap);
    CHECK(has("DIFFERS"), "D20IP against the xHCI's own pin");

    out[0] = '\0';
    memset(&r, 0, sizeof(r));
    r.state = ROUTE_NA_XHCI;
    report_route(&r, 21, cap);
    CHECK(has("Routing: registers not applicable - not a reviewed Intel"),
          "not applicable");
    CHECK(!has("GSI"), "not applicable: nothing compared");
    CHECK(strcmp(route_summary(&r, 21), "routing registers not applicable")
              == 0, "summary not applicable");

    out[0] = '\0';
    r.state = ROUTE_NA_SLOT;
    report_route(&r, -1, cap);
    CHECK(has("not at 00:14.0; nothing read"), "slot");

    out[0] = '\0';
    e7250(&r);
    r.state = ROUTE_NA_LPC;
    r.lpc_id = 0x9CC18086UL;
    report_route(&r, 21, cap);
    CHECK(has("LPC 00:1F.0 8086:9CC1"), "unlisted LPC named");
    CHECK(has("not this xHCI's reviewed LPC"), "unlisted LPC");
    CHECK(!has("RCBA"), "unlisted LPC: RCBA not reported");

    out[0] = '\0';
    e7250(&r);
    r.state = ROUTE_RCBA_INVALID;
    r.rcba = 0xFED1C000UL;
    report_route(&r, 21, cap);
    CHECK(has("base FED1C000, disabled"), "disabled RCBA shown");
    CHECK(has("D20IP and D20IR not read"), "disabled RCBA: no further");
    CHECK(!has("D20IR (RCBA"), "disabled RCBA: no D20IR line");

    out[0] = '\0';
    e7250(&r);
    r.state = ROUTE_MAP_FAILED;
    report_route(&r, 21, cap);
    CHECK(has("could not be mapped"), "map failure");

    e7250(&r);
    CHECK(strcmp(route_summary(&r, 21),
                 "routing MISMATCH against the GSI given") == 0,
          "summary mismatch");
    CHECK(strcmp(route_summary(&r, 16), "routing MATCHES the GSI given") == 0,
          "summary match");

    out[0] = '\0';
    memset(&r, 0, sizeof(r));
    report_route(&r, 21, cap);
    CHECK(out[0] == '\0', "not read: nothing printed");
}

/*
 * Task 39-0.6: the BIOS $PIR table. The valid vector is shaped as the one
 * SeaBIOS rel-1.17.0 builds for QEMU's i440FX (usb4-emu docs/WIN98-TEST.md,
 * "Hot-added NVMe gets no IRQ behind 00:1c": router 00:01.0 8086:122E, six
 * entries, bus 0 devices 1 to 6, links 60-63 rotated per slot, bitmap
 * DEF8), placed at F55A0h where that run read it.
 */
static u8 area[0x10000];
static PIRINFO pir;

#define PIR_AT 0x55A0UL

static void put16(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)(v >> 8);
}

static void pir_sum(u8 *t, u16 size)
{
    u16 i;
    u8 s;

    t[31] = 0;
    s = 0;
    for (i = 0; i < size; i++)
        s = (u8)(s + t[i]);
    t[31] = (u8)(0 - s);
}

static void pir_entry(u8 *e, u8 bus, u8 devfn, u8 link0, u8 slot)
{
    int pin;

    memset(e, 0, PIR_SLOT_LEN);
    e[0] = bus;
    e[1] = devfn;
    for (pin = 0; pin < 4; pin++) {
        e[2 + pin * 3] = (u8)(0x60 + ((link0 - 0x60 + pin) & 3));
        put16(e + 3 + pin * 3, 0xDEF8);
    }
    e[14] = slot;
}

/* A table of n entries at area + at; devices 1..n on bus 0. */
static u8 *pir_build(u32 at, int n)
{
    u8 *t;
    int i;
    u16 size;

    memset(area, 0, sizeof(area));
    t = area + at;
    size = (u16)(PIR_HDR_LEN + PIR_SLOT_LEN * n);
    memcpy(t, "$PIR", 4);
    put16(t + 4, 0x0100);
    put16(t + 6, size);
    t[8] = 0;
    t[9] = 0x08;                        /* 00:01.0 */
    put16(t + 10, 0);
    put16(t + 12, 0x8086);
    put16(t + 14, 0x122E);
    for (i = 0; i < n; i++)
        pir_entry(t + PIR_HDR_LEN + i * PIR_SLOT_LEN, 0, (u8)((i + 1) << 3),
                  (u8)(0x60 + (i & 3)), (u8)i);
    pir_sum(t, size);
    return t;
}

static int pir_scan_area(void)
{
    memset(&pir, 0, sizeof(pir));
    return pir_scan(area, sizeof(area), 0xF0000UL, &pir);
}

static void test_pir_valid(void)
{
    int n;

    pir_build(PIR_AT, 6);
    CHECK(pir_scan_area() == PIR_OK, "SeaBIOS-shaped table parses");
    CHECK(pir.phys == 0xF55A0UL, "found at F55A0h");
    CHECK(pir.version == 0x0100 && pir.size == 128, "version and size");
    CHECK(pir.nslots == 6 && pir.kept == 6, "six entries");
    CHECK(pir.candidates == 1, "one signature");
    CHECK(pir_find(&pir, 0, 1, &n) == 0 && n == 1, "00:01 is entry 1");
    CHECK(pir_find(&pir, 0, 6, &n) == 5 && n == 1, "00:06 is entry 6");
    CHECK(pir_find(&pir, 0, 0x1C, &n) == -1 && n == 0, "no 00:1C");
    CHECK(pir_find(&pir, 1, 1, 0) == -1, "bus 1 is not bus 0");

    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("PCI IRQ routing table ($PIR, read only): at F55A0h, "
              "version 1.0, 128 bytes"), "header line");
    CHECK(has("6 slot entries, checksum OK; router 00:01.0, compatible "
              "8086:122E"), "router line");
    CHECK(has("exclusive PCI IRQs 0000: none; miniport data 00000000"),
          "exclusive IRQs");
    CHECK(has("  1   00:01   0    60/DEF8 61/DEF8 62/DEF8 63/DEF8\n"),
          "entry 1 dumped");
    CHECK(has("  6   00:06   5    61/DEF8 62/DEF8 63/DEF8 60/DEF8\n"),
          "entry 6 dumped, links rotated");
    CHECK(has("00:1C: NO ENTRY - a function behind a root port"),
          "00:1C named as absent");
    CHECK(!has("in the entries kept"), "every entry kept");
    CHECK(!has("no function at"), "configuration space not read: silent");

    out[0] = '\0';
    report_pir_short(&pir, cap);
    CHECK(strcmp(out, "IRQ table: $PIR at F55A0h, 6 entries; 00:1C NOT "
                      "LISTED\n") == 0, "short line, not listed");
}

static void test_pir_rp(void)
{
    u8 *t;
    int n;

    /* A P14s-like shape: an entry for 00:1C (devfn E0h) with links 60-63,
     * root ports at 1C.0 and 1C.4 behind a multi-function function 0. */
    t = pir_build(PIR_AT, 6);
    pir_entry(t + PIR_HDR_LEN + 5 * PIR_SLOT_LEN, 0, 0xE0, 0x60, 0);
    put16(t + 10, 0x0C00);
    pir_sum(t, 128);
    CHECK(pir_scan_area() == PIR_OK, "00:1C table parses");
    pir.rp_read = 1;
    for (n = 0; n < 8; n++)
        pir.rp_id[n] = 0xFFFFFFFFUL;
    pir.rp_id[0] = 0x02B88086UL;
    pir.rp_hdr[0] = 0x81;
    pir.rp_pin[0] = 1;
    pir.rp_line[0] = 11;
    pir.rp_id[4] = 0x02BC8086UL;
    pir.rp_hdr[4] = 0x01;
    pir.rp_pin[4] = 2;
    pir.rp_line[4] = 10;
    CHECK(pir_find(&pir, 0, 0x1C, &n) == 5 && n == 1, "00:1C is entry 6");

    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("exclusive PCI IRQs 0C00: IRQ 10 11;"), "exclusive decoded");
    CHECK(has("  6   00:1C   0    60/DEF8 61/DEF8 62/DEF8 63/DEF8\n"),
          "00:1C entry dumped");
    CHECK(has("  00:1C: listed, entry 6\n"), "00:1C named as listed");
    CHECK(has("    00:1C.0 8086:02B8 bridge, pin INTA line 11: 60/DEF8\n"),
          "1C.0's pin and its link");
    CHECK(has("    00:1C.4 8086:02BC bridge, pin INTB line 10: 61/DEF8\n"),
          "1C.4 named, INTB's link");
    CHECK(!has("00:1C.1"), "absent functions not printed");

    out[0] = '\0';
    report_pir_short(&pir, cap);
    CHECK(strcmp(out, "IRQ table: $PIR at F55A0h, 6 entries; 00:1C "
                      "listed\n") == 0, "short line, listed");

    /* Listed twice. */
    pir_entry(t + PIR_HDR_LEN + 4 * PIR_SLOT_LEN, 0, 0xE0, 0x61, 3);
    pir_sum(t, 128);
    CHECK(pir_scan_area() == PIR_OK, "duplicate table parses");
    CHECK(pir_find(&pir, 0, 0x1C, &n) == 4 && n == 2, "two entries");
    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("00:1C: listed 2 times, first at entry 5"), "duplicate named");

    /* The P14s question with the answer no: an entry-less table, and the
     * functions as found. Function 0 not multi-function: no others read. */
    pir_build(PIR_AT, 6);
    CHECK(pir_scan_area() == PIR_OK, "no-1C table parses");
    pir.rp_read = 1;
    for (n = 0; n < 8; n++)
        pir.rp_id[n] = 0xFFFFFFFFUL;
    pir.rp_id[0] = 0x02B88086UL;
    pir.rp_hdr[0] = 0x01;
    pir.rp_pin[0] = 1;
    pir.rp_line[0] = 11;
    pir.rp_id[4] = 0x02BC8086UL;
    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("00:1C: NO ENTRY"), "absent");
    CHECK(has("    00:1C.0 8086:02B8 bridge, pin INTA line 11: no entry\n"),
          "function shown, no entry");
    CHECK(!has("00:1C.4"), "single-function: nothing past function 0");

    pir.rp_id[0] = 0xFFFFFFFFUL;
    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("no function at 00:1C.0 in configuration space"),
          "no function present");
}

static void test_pir_bad(void)
{
    u8 *t;

    /* No table at all. */
    memset(area, 0, sizeof(area));
    CHECK(pir_scan_area() == PIR_NONE, "no signature");
    CHECK(pir.candidates == 0, "no candidates");
    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(strcmp(out, "PCI IRQ routing table ($PIR, read only): none in "
                      "F0000h-FFFFFh\n") == 0, "none reported");
    out[0] = '\0';
    report_pir_short(&pir, cap);
    CHECK(strcmp(out, "IRQ table: no $PIR in F0000h-FFFFFh\n") == 0,
          "none, short");
    CHECK(pir_find(&pir, 0, 1, 0) == -1, "nothing found in no table");

    /* Off a 16-byte boundary is not a table. */
    pir_build(PIR_AT, 6);
    memmove(area + PIR_AT + 8, area + PIR_AT, 128);
    memset(area + PIR_AT, 0, 8);
    CHECK(pir_scan_area() == PIR_NONE, "unaligned signature ignored");

    /* Bad checksum: parsed, dumped, flagged. */
    t = pir_build(PIR_AT, 6);
    t[31] = (u8)(t[31] + 1);
    CHECK(pir_scan_area() == PIR_BAD_CHECKSUM, "bad checksum");
    CHECK(pir.nslots == 6 && pir.kept == 6, "bad checksum still parsed");
    CHECK(pir_find(&pir, 0, 2, 0) == 1, "bad checksum: entries searchable");
    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("6 slot entries, checksum FAILS;"), "checksum failure named");
    CHECK(has("the entries are shown as found"), "entries flagged");
    CHECK(has("  1   00:01   0    60/DEF8"), "entries dumped anyway");
    out[0] = '\0';
    report_pir_short(&pir, cap);
    CHECK(has(", checksum FAILS; 00:1C NOT LISTED"), "short, bad sum");

    /* Truncated: the size runs past the end of the area. */
    pir_build(0x10000UL - 64, 2);
    CHECK(pir_scan_area() == PIR_OK, "a table ending at FFFFFh is whole");
    t = area + 0x10000UL - 64;
    put16(t + 6, 80);
    pir_sum(t, 64);
    CHECK(pir_scan_area() == PIR_TRUNCATED, "size past FFFFFh");
    CHECK(pir.phys == 0xFFFC0UL && pir.size == 80, "truncated: where, size");
    CHECK(pir.kept == 0, "truncated: nothing kept");
    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("at FFFC0h, UNUSABLE"), "unusable named");
    CHECK(has("version 1.0, size 80: the table runs past FFFFFh; nothing "
              "parsed"), "truncation named");
    out[0] = '\0';
    report_pir_short(&pir, cap);
    CHECK(strcmp(out, "IRQ table: $PIR at FFFC0h unusable (truncated)\n")
              == 0, "truncated, short");

    /* A header cut off by the end of the area. */
    memset(area, 0, sizeof(area));
    memcpy(area + 0x10000UL - 16, "$PIR", 4);
    CHECK(pir_scan_area() == PIR_TRUNCATED, "header past the end");

    /* Sizes. */
    t = pir_build(PIR_AT, 6);
    put16(t + 6, 120);
    CHECK(pir_scan_area() == PIR_BAD_SIZE, "size not a multiple of 16");
    put16(t + 6, 32);
    CHECK(pir_scan_area() == PIR_BAD_SIZE, "a header and no entries");
    put16(t + 6, 0);
    CHECK(pir_scan_area() == PIR_BAD_SIZE, "size 0");
    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("size 0: table size is not above 32"), "size named");

    /* Version. */
    t = pir_build(PIR_AT, 6);
    put16(t + 4, 0x0200);
    pir_sum(t, 128);
    CHECK(pir_scan_area() == PIR_BAD_VERSION, "version 2.0");
    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("version 2.0, size 128: version is not 1.x"), "version named");

    /* A bad candidate before a good one: the good one wins. */
    pir_build(PIR_AT, 6);
    memcpy(area + 0x100, "$PIR", 4);
    CHECK(pir_scan_area() == PIR_OK, "a later valid table wins");
    CHECK(pir.phys == 0xF55A0UL && pir.candidates == 2, "second candidate");

    /* Only bad candidates: the first one's failure is kept. */
    t = pir_build(PIR_AT, 6);
    t[31] = (u8)(t[31] + 1);
    memcpy(area + 0x100, "$PIR", 4);
    put16(area + 0x104, 0x0100);
    put16(area + 0x106, 40);
    CHECK(pir_scan_area() == PIR_BAD_SIZE, "first failure kept");
    CHECK(pir.phys == 0xF0100UL, "first candidate's address");

    /* Not read: nothing printed. */
    memset(&pir, 0, sizeof(pir));
    out[0] = '\0';
    report_pir(&pir, cap);
    report_pir_short(&pir, cap);
    CHECK(out[0] == '\0', "not read: nothing printed");
}

static void test_pir_edges(void)
{
    u8 *t;
    int n;

    /* Link 0 (not connected) and function bits in the device byte. */
    t = pir_build(PIR_AT, 2);
    t[PIR_HDR_LEN + 2 + 9] = 0;          /* entry 1 INTD: not connected */
    t[PIR_HDR_LEN + PIR_SLOT_LEN + 1] = 0x13; /* 00:02, function bits 3 */
    pir_sum(t, 64);
    CHECK(pir_scan_area() == PIR_OK, "edge table parses");
    CHECK(pir_find(&pir, 0, 2, 0) == 1, "device from bits 7:3 only");
    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("  1   00:01   0    60/DEF8 61/DEF8 62/DEF8 --/----\n"),
          "link 0 shown as not connected");
    CHECK(has("  2   00:02   1    61/DEF8 62/DEF8 63/DEF8 60/DEF8 fn bits "
              "set\n"), "function bits flagged");

    /* More entries than are kept. */
    t = pir_build(0x1000, PIR_MAX_SLOTS + 2);
    CHECK(pir_scan_area() == PIR_OK, "a long table parses");
    CHECK(pir.nslots == PIR_MAX_SLOTS + 2 && pir.kept == PIR_MAX_SLOTS,
          "kept capped");
    /* Devices 1-130 wrap at 32, so device 1Ch is entries 28, 60, 92 and
     * 124, all kept. */
    CHECK(pir_find(&pir, 0, 0x1C, &n) == 27 && n == 4,
          "every kept duplicate counted");
    out[0] = '\0';
    report_pir(&pir, cap);
    CHECK(has("  2 more entries not shown\n"), "the rest counted");
    CHECK(has("  128 00:00   127  "), "the last kept entry dumped");
    CHECK(!has("  129 "), "nothing past the cap dumped");
    CHECK(has("00:1C: listed 4 times, first at entry 28"), "00:1C listed");
    (void)t;
}

int main(void)
{
    test_scope();
    test_pirq();
    test_compare();
    test_report();
    test_pir_valid();
    test_pir_rp();
    test_pir_bad();
    test_pir_edges();
    printf("test_route: %d checks, %d failures\n", checks, failures);
    return failures;
}
