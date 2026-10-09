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
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework, no DOS dependencies.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../qual.h"

static char out[4096];
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

int main(void)
{
    test_scope();
    test_pirq();
    test_compare();
    test_report();
    printf("test_route: %d checks, %d failures\n", checks, failures);
    return failures;
}
