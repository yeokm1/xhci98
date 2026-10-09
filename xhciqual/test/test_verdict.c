/*
 * test_verdict.c - host-side unit tests for xhciqual/verdict.c: C4's
 * outcome, the C6/C8 gate, the per-controller verdict and the run's exit
 * code (roadmap-hcd.md task 36.10, design record 15 section 10).
 *
 * Why this exists: no QEMU case reaches "needs polling". Every emulated
 * xHCI here reports Interrupt Pin A and delivers its interrupt, so the
 * matrix sees C4 PASS only; the pin-0 and dead-line outcomes, and the
 * QUALIFIED WITH CAVEATS verdict they lead to, run first here. The vectors
 * go through final_verdict, not only the classifier (test_mmiodiag), so
 * the wording, the verdict and the exit code are held together.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework, no DOS dependencies. qprintf is captured here.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../qual.h"

static char out[8192];
static int failures;
static int checks;

void qprintf(const char *fmt, ...)
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

static CTRL ctrl;
static const QUIRK broken_msi = { 0x1B73, 0x1000, QF_BROKEN_MSI, "test" };

/* A controller that passed every gate of a full run, on pin A. */
static CTRL *full_pass(void)
{
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.pci.vid = 0x8086;
    ctrl.pci.did = 0x9CB1;
    ctrl.pci.bar_phys = 0xF7F00000UL;
    ctrl.pci.cmd_effective = PCI_CMD_MSE | PCI_CMD_BME;
    ctrl.pci.ipin = 1;
    ctrl.pci.iline = 11;
    ctrl.mmio_ok = 1;
    ctrl.usb2_ports = 4;
    ctrl.v_handoff = V_PASS;
    ctrl.v_reset = V_PASS;
    ctrl.v_dma = V_PASS;
    ctrl.v_irq = V_PASS;
    ctrl.v_port = V_PASS;
    strcpy(ctrl.irq_note, "one-shot ISR fired on IRQ 11 (level per ELCR)");
    out[0] = '\0';
    return &ctrl;
}

static void test_c4_verdict(void)
{
    C4_SEEN s;

    memset(&s, 0, sizeof(s));
    s.ip_seen = 1;
    s.isr_fired = 1;
    s.event_seen = 1;
    CHECK(c4_verdict(&s) == V_PASS, "IP, the ISR and the event: PASS");
    s.event_seen = 0;
    CHECK(c4_verdict(&s) == V_FAIL, "the ISR with no completion: FAIL");
    s.event_seen = 1;
    s.foreign = 1;
    CHECK(c4_verdict(&s) == V_FAIL, "a foreign entry: FAIL");

    memset(&s, 0, sizeof(s));
    s.ip_seen = 1;
    s.event_seen = 1;
    CHECK(c4_verdict(&s) == V_POLL,
          "IP up, the ISR never entered, the event in the ring: POLL");
    s.event_seen = 0;
    CHECK(c4_verdict(&s) == V_FAIL, "and no event: FAIL");

    memset(&s, 0, sizeof(s));
    s.event_seen = 1;
    CHECK(c4_verdict(&s) == V_POLL,
          "no IP with the line masked, the event polled: POLL");
    s.event_seen = 0;
    CHECK(c4_verdict(&s) == V_FAIL, "no IP and no event: FAIL");

    memset(&s, 0, sizeof(s));
    s.no_line = 1;
    s.event_seen = 1;
    CHECK(c4_verdict(&s) == V_POLL, "no line, completed polled: POLL");
    s.event_seen = 0;
    CHECK(c4_verdict(&s) == V_FAIL, "no line, not even polled: FAIL");

    memset(&s, 0, sizeof(s));
    s.tool_failed = 1;
    s.event_seen = 1;
    s.ip_seen = 1;
    CHECK(c4_verdict(&s) == V_FAIL,
          "a DPMI failure stays a failure, whatever else was seen");
    s.no_line = 1;
    CHECK(c4_verdict(&s) == V_FAIL, "even with no line");

    CHECK(strcmp(verdict_name(V_POLL), "POLL") == 0, "POLL is named");
    CHECK(strcmp(verdict_name(V_PASS), "PASS") == 0, "PASS still is");
    CHECK(strcmp(verdict_name(V_SKIP), "SKIP") == 0, "and SKIP");
}

static void test_gate_and_exit(void)
{
    CHECK(c4_lets_ports(V_PASS, 0), "C6 after a PASS");
    CHECK(c4_lets_ports(V_POLL, 0), "C6 after a POLL: the polled tests");
    CHECK(!c4_lets_ports(V_FAIL, 0), "no C6 after a FAIL");
    CHECK(!c4_lets_ports(V_SKIP, 0), "nor after a SKIP");
    CHECK(c4_lets_ports(V_SKIP, 1), "a poll-only run runs C6");

    CHECK(run_exit_code(0, 0) == 0, "all qualified: 0");
    CHECK(run_exit_code(0, 1) == 3, "one with caveats: 3");
    CHECK(run_exit_code(1, 0) == 1, "one not qualified: 1");
    CHECK(run_exit_code(1, 1) == 1, "not qualified beats caveats");
}

static void test_full_pass(void)
{
    CHECK(final_verdict(full_pass(), 1) == VERDICT_QUALIFIED,
          "a clean full run is QUALIFIED");
    CHECK(has("==> CONTROLLER QUALIFIED for cross-target"), "said plainly");
    CHECK(!has("CAVEAT"), "with no caveat");
}

/* The issue 12 shape: IP up, the ISR never entered, the event polled. */
static void test_full_dead_line(void)
{
    CTRL *c = full_pass();

    c->v_irq = V_POLL;
    strcpy(c->irq_note, "xHCI IP pending, ISR never fired on IRQ 11; "
                        "event polled");
    CHECK(final_verdict(c, 1) == VERDICT_CAVEATS,
          "a line that never delivers: QUALIFIED WITH CAVEATS");
    CHECK(has("NEEDS POLLING: C4: xHCI IP pending"), "C4's cause said");
    CHECK(has("==> CONTROLLER QUALIFIED WITH CAVEATS"), "the verdict");
    CHECK(!has("(with warnings)"), "not the warnings' wording");
    CHECK(!has("NOT QUALIFIED"), "not a failure");
    CHECK(!has("PROVISIONAL"), "not provisional");
    CHECK(has("CAVEAT: no working line interrupt"), "the caveat");
    CHECK(has("more CPU") && has("poll period"), "polling's costs said");
    CHECK(has("XhciInterruptMode 0") && has("or 2"), "both modes named");
    CHECK(has("MSI-capable OS"), "the MSI route named");
    CHECK(has("neither MSI nor MSI-X, so polling is its"),
          "and this controller has none");
    CHECK(!has("Also with warnings"), "no warnings line");

    /* IP never raised: automatic mode cannot see it, so mode 2 is named
     * and no message route is offered (review round 1). */
    c = full_pass();
    c->v_irq = V_POLL;
    c->irq_force_poll = 1;
    c->pci.has_msix = 1;
    strcpy(c->irq_note, "No xHCI IP while PIC masked; event completed by "
                        "polling");
    CHECK(final_verdict(c, 1) == VERDICT_CAVEATS,
          "no IP, completed polled: still with caveats");
    CHECK(has("Set XhciInterruptMode to 2"), "mode 2 named");
    CHECK(!has("XhciInterruptMode 0, automatic"), "not automatic mode");
    CHECK(has("No message interrupt is offered"), "and no MSI route");
    CHECK(!has("since it offers MSI-X"), "even with MSI-X present");

    c = full_pass();
    c->v_irq = V_POLL;
    c->v_port = V_SKIP;
    c->pci.has_msi = 1;
    CHECK(final_verdict(c, 1) == VERDICT_CAVEATS,
          "caveats and a C6 SKIP: still with caveats");
    CHECK(has("QUALIFIED WITH CAVEATS") && has("Also with warnings"),
          "both shown");
    CHECK(has("since it offers MSI (x64"), "MSI named");
    CHECK(has("xhcimsi.sys"), "with the 32-bit helper");
}

static void test_full_pin0(void)
{
    CTRL *c = full_pass();

    c->pci.ipin = 0;
    c->pci.has_msi = 1;
    c->pci.has_msix = 1;
    c->v_irq = V_POLL;
    strcpy(c->irq_note, "Interrupt Pin = 0: no INTx; No-Op completed polled");
    CHECK(final_verdict(c, 1) == VERDICT_CAVEATS,
          "pin 0 that completes polled: QUALIFIED WITH CAVEATS");
    CHECK(has("NEEDS POLLING: Interrupt Pin = 0"), "the pin said");
    CHECK(!has("NEEDS POLLING: C4"), "once, not twice");
    CHECK(!has("DISQUALIFIED"), "no longer disqualified");
    CHECK(has("MSI and MSI-X"), "both capabilities named");
    CHECK(has("MSI-X (x64") &&
          has("directly; 32-bit with the xhcimsi.sys helper"),
          "and both architectures' routes");

    c = full_pass();
    c->pci.ipin = 0;
    c->pci.has_msix = 1;
    c->quirk = &broken_msi;
    c->v_irq = V_POLL;
    (void)final_verdict(c, 1);
    CHECK(has("marks this controller's MSI broken"),
          "a broken-MSI part is not offered MSI");

    /* Fails even polled: a failure, not a caveat. */
    c = full_pass();
    c->pci.ipin = 0;
    c->v_irq = V_FAIL;
    c->irq_no_event = 1;
    strcpy(c->irq_note, "Interrupt Pin = 0: no INTx; No-Op not completed "
                        "even polled");
    CHECK(final_verdict(c, 1) == VERDICT_NOT,
          "pin 0 and no completion: NOT QUALIFIED");
    CHECK(has("never completed, even polled"), "said as the command's");
    CHECK(!has("WITH CAVEATS"), "no caveat verdict");
}

static void test_full_failures_stand(void)
{
    CTRL *c = full_pass();

    c->v_irq = V_FAIL;
    strcpy(c->irq_note, "DPMI IRQ install failed (AX=8001)");
    CHECK(final_verdict(c, 1) == VERDICT_NOT, "a tool failure: NOT");
    CHECK(has("no interrupt on legacy IRQ (C4): DPMI"), "worded as before");

    c = full_pass();
    c->v_irq = V_FAIL;
    c->irq_no_event = 1;
    strcpy(c->irq_note, "No xHCI IP or No-Op event while PIC masked");
    CHECK(final_verdict(c, 1) == VERDICT_NOT, "no IP, no event: NOT");

    c = full_pass();
    c->v_irq = V_POLL;
    c->v_reset = V_FAIL;
    CHECK(final_verdict(c, 1) == VERDICT_NOT, "a C2 failure wins");
    CHECK(!has("WITH CAVEATS"), "and no caveat verdict");

    c = full_pass();
    c->v_irq = V_POLL;
    c->v_dma = V_FAIL;
    CHECK(final_verdict(c, 1) == VERDICT_NOT, "a C3 failure wins");

    c = full_pass();
    c->v_irq = V_POLL;
    c->v_dma = V_SKIP;
    strcpy(c->dma_note, "no conventional memory");
    CHECK(final_verdict(c, 1) == VERDICT_NOT, "a C3 tool limit wins");

    c = full_pass();
    c->v_irq = V_POLL;
    c->cleanup_failed = 1;
    strcpy(c->cleanup_note, "HCH never set");
    CHECK(final_verdict(c, 1) == VERDICT_NOT, "a cleanup failure wins");

    c = full_pass();
    c->pci.ipin = 0;
    c->usb2_ports = 0;
    c->v_irq = V_POLL;
    CHECK(final_verdict(c, 1) == VERDICT_NOT,
          "pin 0 and no USB2 ports: the ports disqualify");

    c = full_pass();
    c->pci.ipin = 0;
    c->pci.bar_hi = 1;
    c->mmio_ok = 0;
    CHECK(final_verdict(c, 1) == VERDICT_NOT,
          "pin 0 and a BAR above 4 GB: the BAR disqualifies");
}

static void test_other_modes(void)
{
    CTRL *c = full_pass();

    c->pci.ipin = 0;
    CHECK(final_verdict(c, 0) == VERDICT_NOT,
          "a probe-only run reaches no verdict");
    CHECK(has("no disqualifiers so far") && has("CAVEAT"),
          "but says the pin needs polling");

    c = full_pass();
    c->pci.ipin = 0;
    c->poll_only = 1;
    c->v_irq = V_SKIP;
    CHECK(final_verdict(c, 1) == VERDICT_NOT, "poll-only stays provisional");
    CHECK(has("PROVISIONAL") && has("CAVEAT"), "with the caveat");

    c = full_pass();
    c->irq_selftest = 1;
    c->v_irq = V_POLL;
    strcpy(c->irq_note, "No xHCI IP while PIC masked; event completed by "
                        "polling");
    CHECK(final_verdict(c, 1) == VERDICT_CAVEATS,
          "the self-test's needs-polling: caveats");
    CHECK(has("IRQ SELF-TEST: NEEDS POLLING") &&
          has("NO WORKING INTERRUPT"), "said as such");
    CHECK(!has("IRQ SELF-TEST FAILURE"), "not a failure");

    c = full_pass();
    c->irq_selftest = 1;
    c->v_irq = V_FAIL;
    CHECK(final_verdict(c, 1) == VERDICT_NOT, "the self-test's FAIL");

    c = full_pass();
    c->irq_selftest = 1;
    CHECK(final_verdict(c, 1) == VERDICT_QUALIFIED, "the self-test's PASS");
    CHECK(has("IRQ SELF-TEST PASS"), "said");
}

int main(void)
{
    test_c4_verdict();
    test_gate_and_exit();
    test_full_pass();
    test_full_dead_line();
    test_full_pin0();
    test_full_failures_stand();
    test_other_modes();
    printf("test_verdict: %d checks, %d failures\n", checks, failures);
    return failures;
}
