/*
 * test_mmiodiag.c - host-side unit tests for xhciqual/mmiodiag.c.
 *
 * Why this exists: none of the code under test is reachable in the QEMU
 * matrix. SeaBIOS leaves Memory Space Enable set and the BAR assigned, so
 * mmio_ok is always true and neither classifier runs; and no emulated USB
 * controller exposes a PM capability, so report_pci_pm() only ever takes its
 * "capability absent" branch. Without this runner the first execution of
 * every branch below would be on a field machine.
 *
 * It is also the cheapest available check on the PCI PM bit positions. The
 * PCI Bus Power Management Interface Specification is not mirrored in
 * docs/references/ the way the xHCI spec is, so the expected strings here
 * were written by hand from the spec's field definitions - the same
 * "transcribe the expectation independently" rule design doc 03 applies to
 * TRB golden vectors. The transcription itself was checked on metal on
 * The ThinkPad E460 run and an lspci -vv of the same function
 * agree field for field (hardware-testing.md, "Cross-checking the PCI block"),
 * and test_pm_e460_field_values() below pins that reading so a future edit
 * cannot silently drift away from the one independently confirmed measurement.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass), per design doc 03 section 5.
 *
 * C89, no framework, no DOS dependencies. qprintf below replaces the real
 * one in report.c, which needs conio/port I/O and cannot build on the host.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../qual.h"

static char out[4096];
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

static void reset_out(void)
{
    out[0] = '\0';
}

/* CHECK reports file/line and counts failures (design doc 03 section 5). */
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

/* A controller with nothing wrong: BAR assigned below 4 GB, MSE set, D0. */
static void base_pci(PCIINFO *p)
{
    memset(p, 0, sizeof(*p));
    p->bar_phys = 0xFEBF0000UL;
    p->bar_hi = 0;
    p->cmd_effective = PCI_CMD_MSE | PCI_CMD_BME;
}

static void test_pm_absent(void)
{
    PCIINFO p;

    base_pci(&p);
    p.has_pm = 0;
    reset_out();
    report_pci_pm(&p);
    CHECK(has("capability absent"), "no PM cap says so");
    CHECK(!has("PME_Support"), "absent cap prints no PME list");
}

/* PMC bits 2:0 = Version, bit 3 = PME_Clock, bit 5 = DSI, bits 8:6 =
 * Aux_Current, bit 9 = D1_Support, bit 10 = D2_Support, bits 15:11 =
 * PME_Support (D0, D1, D2, D3hot, D3cold). PMCSR bits 1:0 = PowerState,
 * bit 3 = No_Soft_Reset, bit 8 = PME_En, bit 15 = PME_Status. */
static void test_pm_typical_intel(void)
{
    PCIINFO p;

    /* PME from D0, D3hot and D3cold; no D1/D2 support; in D0, PME disabled.
     * PME_Support = D0|D3hot|D3cold = bits 11,14,15 = 0xC800. Version 3. */
    base_pci(&p);
    p.has_pm = 1;
    p.pmc = 0xC800 | 0x0003;
    p.pmcsr = 0x0000;
    p.pm_state = 0;
    reset_out();
    report_pci_pm(&p);
    CHECK(has("v3"), "version field rendered raw");
    CHECK(has("state=D0"), "D0 state rendered");
    CHECK(has("D1=0 D2=0"), "D1/D2 unsupported rendered");
    CHECK(has("PME_En=0 PME_Status=0"), "PME control bits rendered");
    CHECK(has("PME_Support: D0, D3hot, D3cold"), "PME_Support list exact");
    CHECK(has("NoSoftRst=0"), "No_Soft_Reset clear rendered");
    CHECK(has("resume must reinitialise"), "NoSoftRst=0 explains the cost");
    CHECK(has("DSI=0 PMEClk=0"), "DSI and PME_Clock clear rendered");
    CHECK(has("Aux=0mA"), "zero Aux_Current rendered");
    CHECK(!has("NOTE: DSI set"), "no DSI note when DSI is clear");
    CHECK(!has("NOTE: not in D0"), "no D0 warning when in D0");
}

/* Golden case: the exact capability the ThinkPad E460's xHCI (8086:9D2F)
 * reported, whose decode was confirmed field for field against
 * lspci -vv on the same machine (xhciqual/results/e460-2026-07-25/README.md).
 * This is the only PM reading in the project validated by an independent
 * decoder, so it is pinned here: a change that breaks it breaks agreement with
 * the one measurement that proved the bit positions right.
 *
 * lspci said:
 *   Flags:  PMEClk- DSI- D1- D2- AuxCurrent=375mA
 *           PME(D0-,D1-,D2-,D3hot+,D3cold+)
 *   Status: D0 NoSoftRst+ PME-Enable- DSel=0 DScale=0 PME-
 *
 * and the qualifier read the raw words straight off the machine:
 *   raw: PMC=C1C2 PMCSR=0008
 *
 * The two agree by construction - PMC 0xC1C2 is PME_Support D3hot|D3cold
 * (bits 14,15) | Aux 111b (bits 8:6) | version 2, and PMCSR 0x0008 is
 * No_Soft_Reset with the device in D0 and PME_En/PME_Status clear - so this
 * case ties a captured hardware reading to an independently decoded one. */
static void test_pm_e460_field_values(void)
{
    PCIINFO p;

    base_pci(&p);
    p.has_pm = 1;
    p.pmc = 0xC1C2;
    p.pmcsr = 0x0008;
    p.pm_state = 0;
    reset_out();
    report_pci_pm(&p);
    CHECK(has("v2"), "E460 reports PM version 2");
    CHECK(has("state=D0"), "E460 found in D0");
    CHECK(has("D1=0 D2=0"), "E460 supports neither D1 nor D2");
    CHECK(has("PME_En=0 PME_Status=0"), "E460 PME disabled and not latched");
    CHECK(has("PME_Support: D3hot, D3cold"), "E460 PME from D3hot and D3cold");
    CHECK(has("NoSoftRst=1"), "E460 retains state across D3hot->D0");
    CHECK(has("keeps state across D3hot->D0"), "NoSoftRst=1 says what it buys");
    CHECK(has("DSI=0 PMEClk=0"), "E460 needs no device-specific init");
    CHECK(has("Aux=375mA"), "E460 Aux_Current matches lspci's 375mA");
    CHECK(has("raw: PMC=C1C2 PMCSR=0008"), "raw words printed for re-decoding");
    CHECK(!has("NOTE: not in D0"), "no D0 warning for a device in D0");
}

static void test_pm_all_states_and_flags(void)
{
    PCIINFO p;

    /* Every PME_Support bit set (bits 15:11 = 0xF800), D1 (bit 9) and D2
     * (bit 10) supported = 0x0600, DSI (bit 5), PME_Clock (bit 3),
     * Aux_Current 011b = 160 mA (bits 8:6), version 2. PMCSR: PME_En (bit 8),
     * PME_Status (bit 15) and No_Soft_Reset (bit 3) set, state D2. */
    base_pci(&p);
    p.has_pm = 1;
    p.pmc = 0xF800 | 0x0600 | 0x0020 | 0x0008 | 0x00C0 | 0x0002;
    p.pmcsr = 0x8100 | 0x0008 | 0x0002;
    p.pm_state = 2;
    reset_out();
    report_pci_pm(&p);
    CHECK(has("state=D2"), "D2 state rendered");
    CHECK(has("D1=1 D2=1"), "D1/D2 support rendered");
    CHECK(has("PME_En=1 PME_Status=1"), "PME_En/Status rendered");
    CHECK(has("PME_Support: D0, D1, D2, D3hot, D3cold"),
          "full PME_Support list in order");
    CHECK(has("NoSoftRst=1"), "No_Soft_Reset set rendered");
    CHECK(has("DSI=1 PMEClk=1"), "DSI and PME_Clock set rendered");
    CHECK(has("Aux=160mA"), "mid-table Aux_Current decoded");
    CHECK(has("NOTE: DSI set"), "DSI set warns that D0 init is not enough");
    CHECK(has("NOTE: not in D0"), "non-D0 warns");
}

/* Aux_Current is a 3-bit table lookup, so walk every entry: an off-by-one in
 * the table would otherwise hide behind the two values the other tests use. */
static void test_pm_aux_current_table(void)
{
    static const char *expect[8] = { "Aux=0mA", "Aux=55mA", "Aux=100mA",
                                     "Aux=160mA", "Aux=220mA", "Aux=270mA",
                                     "Aux=320mA", "Aux=375mA" };
    PCIINFO p;
    int i;

    for (i = 0; i < 8; i++) {
        base_pci(&p);
        p.has_pm = 1;
        p.pmc = (u16)(i << 6);
        p.pmcsr = 0x0000;
        p.pm_state = 0;
        reset_out();
        report_pci_pm(&p);
        CHECK(has(expect[i]), "Aux_Current table entry decoded");
    }
}

static void test_pm_no_pme_support(void)
{
    PCIINFO p;

    base_pci(&p);
    p.has_pm = 1;
    p.pmc = 0x0000;
    p.pmcsr = 0x0003;
    p.pm_state = 3;
    reset_out();
    report_pci_pm(&p);
    CHECK(has("v0"), "an all-zero PMC reports version 0, not a guess");
    CHECK(has("state=D3hot"), "D3hot state rendered");
    CHECK(has("PME_Support: none"), "empty PME_Support says none");
    CHECK(has("raw: PMC=0000 PMCSR=0003"), "raw words survive an empty cap");
    CHECK(has("NOTE: not in D0"), "D3hot warns");
}

/* The raw line is the fallback when a decode is ever disputed, so it must
 * reproduce both words exactly - upper-case, zero-padded to four digits, and
 * never sign-extended or truncated by the u16 -> unsigned promotion. */
static void test_pm_raw_words(void)
{
    PCIINFO p;

    base_pci(&p);
    p.has_pm = 1;
    p.pmc = 0xFFFF;
    p.pmcsr = 0xFFFF;
    p.pm_state = 3;
    reset_out();
    report_pci_pm(&p);
    CHECK(has("raw: PMC=FFFF PMCSR=FFFF"), "all-ones words print unmangled");

    base_pci(&p);
    p.has_pm = 1;
    p.pmc = 0x000A;
    p.pmcsr = 0x00B0;
    p.pm_state = 0;
    reset_out();
    report_pci_pm(&p);
    CHECK(has("raw: PMC=000A PMCSR=00B0"), "small words stay zero-padded");
}

/* PCI Status error bits (0x06): bit 15 detected parity error, 14 signaled
 * SERR, 13 received master abort, 12 received target abort, 11 signaled target
 * abort, 8 master data parity error. All sticky and RW1C, which is the entire
 * reason this reporter compares two snapshots instead of printing one. */
static void test_status_clean(void)
{
    PCIINFO p;

    /* Nothing set before, nothing after: the block must stay silent rather
     * than printing a reassuring line nobody needs. */
    base_pci(&p);
    p.status_orig = 0x0290;      /* cap list, DEVSEL, fast back-to-back */
    p.status_final = 0x0290;
    p.status_rechecked = 1;
    reset_out();
    report_pci_status(&p);
    CHECK(!has("PCI status"), "a clean before/after prints nothing");
}

static void test_status_preexisting_not_attributed(void)
{
    PCIINFO p;

    /* Master abort already set at probe and still set afterwards. It must be
     * reported as pre-existing and must NOT be claimed as this run's doing. */
    base_pci(&p);
    p.status_orig = 0x2000;
    p.status_final = 0x2000;
    p.status_rechecked = 1;
    reset_out();
    report_pci_status(&p);
    CHECK(has("pre-existing errors"), "a bit set at probe is called out");
    CHECK(has("received master abort"), "the pre-existing bit is named");
    CHECK(has("not attributed"), "pre-existing state is explicitly disowned");
    CHECK(!has("NEW error"), "an unchanged bit is never called new");
}

static void test_status_new_error_attributed(void)
{
    PCIINFO p;

    /* Clean at probe, master abort afterwards: caused by this run. */
    base_pci(&p);
    p.status_orig = 0x0010;
    p.status_final = 0x0010 | 0x2000;
    p.status_rechecked = 1;
    reset_out();
    report_pci_status(&p);
    CHECK(has("NEW error"), "a bit that turned on is called new");
    CHECK(has("received master abort"), "the new bit is named");
    CHECK(has("read with the C3 result"), "new errors point at C3");
    CHECK(!has("pre-existing"), "no pre-existing line when probe was clean");
}

/* The case the two-snapshot design exists for: one error predates the run and
 * a different one appears during it. Both must be reported, in their own
 * categories - collapsing them would either invent a fault or hide one. */
static void test_status_mixed_old_and_new(void)
{
    PCIINFO p;

    base_pci(&p);
    p.status_orig = 0x8000;                     /* parity error, pre-existing */
    p.status_final = 0x8000 | 0x1000;           /* plus a new target abort */
    p.status_rechecked = 1;
    reset_out();
    report_pci_status(&p);
    CHECK(has("pre-existing errors - detected parity error"),
          "the old bit stays in the pre-existing list");
    CHECK(has("NEW error(s) during the active tests - received target abort"),
          "the new bit stays in the new list, alone");
    CHECK(!has("NEW error(s) during the active tests - detected parity"),
          "the pre-existing bit is not re-reported as new");
}

/* Probe-only never re-reads, so there is no "after" to compare against. The
 * reporter must still surface pre-existing state but must claim nothing about
 * a run that generated no traffic. */
static void test_status_no_recheck(void)
{
    PCIINFO p;

    base_pci(&p);
    p.status_orig = 0x2000;
    p.status_final = 0;
    p.status_rechecked = 0;
    reset_out();
    report_pci_status(&p);
    CHECK(has("pre-existing errors"), "probe-only still reports what it saw");
    CHECK(!has("NEW error"),
          "an unread status_final is never treated as all-clear");
}

/* Every error bit must be decoded; a mask typo would otherwise hide behind
 * whichever bits the other tests happen to use. */
static void test_status_all_error_bits(void)
{
    static const struct { u16 mask; const char *name; } all[6] = {
        { 0x8000, "detected parity error" },
        { 0x4000, "signaled SERR" },
        { 0x2000, "received master abort" },
        { 0x1000, "received target abort" },
        { 0x0800, "signaled target abort" },
        { 0x0100, "master data parity error" }
    };
    PCIINFO p;
    int i;

    for (i = 0; i < 6; i++) {
        base_pci(&p);
        p.status_orig = 0x0010;
        p.status_final = (u16)(0x0010 | all[i].mask);
        p.status_rechecked = 1;
        reset_out();
        report_pci_status(&p);
        CHECK(has(all[i].name), "each error bit decodes to its own name");
    }
}

/* Non-error bits share the register and change legitimately - the Interrupt
 * Status bit in particular tracks INTx and moves during a run by design.
 * None of them may be mistaken for a bus error. */
static void test_status_ignores_non_error_bits(void)
{
    PCIINFO p;

    base_pci(&p);
    p.status_orig = 0x0000;
    p.status_final = 0x0008 | 0x0010 | 0x0020 | 0x0080 | 0x0600;
    p.status_rechecked = 1;
    reset_out();
    report_pci_status(&p);
    CHECK(!has("PCI status"), "INTx/cap/DEVSEL changes are not bus errors");
}

/* The ordering rule: a BAR the mapper refuses is the proximate cause, so it
 * must win over a D-state or MSE the mapper never got far enough to care
 * about. Regression guard for the pre-fix ordering. */
static void test_dead_bar_beats_power_state(void)
{
    PCIINFO p;

    base_pci(&p);
    p.bar_hi = 1;
    p.has_pm = 1;
    p.pm_state = 3;
    p.cmd_effective = 0;
    reset_out();
    report_mmio_dead(&p);
    CHECK(has("above 4 GB"), "BAR above 4 GB named first");
    CHECK(!has("not in D0"), "D-state not named when BAR is unmappable");
    CHECK(!has("Memory Space Enable"), "MSE not named when BAR is unmappable");
}

static void test_dead_causes(void)
{
    PCIINFO p;

    base_pci(&p);
    p.bar_phys = 0;
    reset_out();
    report_mmio_dead(&p);
    CHECK(has("unassigned"), "unassigned BAR named");

    base_pci(&p);
    p.has_pm = 1;
    p.pm_state = 3;
    reset_out();
    report_mmio_dead(&p);
    CHECK(has("device is in D3"), "D3 named with the state number");

    base_pci(&p);
    p.cmd_effective = 0;
    reset_out();
    report_mmio_dead(&p);
    CHECK(has("Memory Space Enable is clear"), "MSE clear named");

    /* has_pm clear must not let a stale pm_state leak into the diagnosis. */
    base_pci(&p);
    p.has_pm = 0;
    p.pm_state = 3;
    reset_out();
    report_mmio_dead(&p);
    CHECK(has("undetermined"), "no PM cap means no D-state claim");

    base_pci(&p);
    reset_out();
    report_mmio_dead(&p);
    CHECK(has("undetermined"), "all checks clean reports undetermined");

    /*
     * **A reason the mapper recorded wins over every reconstruction below it**
     * (the 2026-09-16 audit's C5). Three of the mapper's refusals - an
     * I/O-space BAR, a CAPLENGTH of 0 or an HCIVERSION below 0.90, and a
     * register block past the fixed 64 KB window - leave PCI state looking
     * perfectly healthy, so every one of them used to print "undetermined" and
     * be read as dead silicon. They are readings about the tool or about a
     * controller misdescribing itself.
     */
    base_pci(&p);
    p.mmio_reason = "the runtime registers sit outside the window this tool "
                    "maps - a tool limit, not a controller fault";
    reset_out();
    report_mmio_dead(&p);
    CHECK(has("a tool limit"), "a recorded reason is printed");
    CHECK(!has("undetermined"), "and displaces the undetermined fallback");

    /* And it is preferred even where a PCI-state cause could be reconstructed,
     * because it is the reason the code actually took. */
    base_pci(&p);
    p.cmd_effective = 0;
    p.mmio_reason = "BAR0 selects I/O space";
    reset_out();
    report_mmio_dead(&p);
    CHECK(has("I/O space"), "the mapper's own reason is the proximate one");
    CHECK(!has("Memory Space Enable is clear"),
          "and the reconstructed cause is not printed beside it");
}

/*
 * **A recorded TOOL limit must reach the verdict, not just the cause line.**
 * The window-size refusal is about this tool mapping a fixed 64 KB, on a
 * controller whose BAR is assigned below 4 GB, which is in D0 and has MSE
 * set - so every reconstruction says "nothing wrong" and the fallback used to
 * be DISQUALIFIED. Naming the cause while still disqualifying the part is the
 * contradiction the cause line was added to remove, so the two are checked
 * together here (Codex review of the 2026-09-16 audit's C5 fix).
 */
static void test_tool_limit_is_not_a_disqualification(void)
{
    PCIINFO p;

    base_pci(&p);
    p.mmio_tool_limit = 1;
    p.mmio_reason = "the runtime registers sit outside the window this tool "
                    "maps - a tool limit, not a controller fault";

    CHECK(quick_classify_mmio(&p, 1) == QUICK_CANNOT_SAY,
          "an active run cannot say, rather than disqualifying");
    CHECK(quick_classify_mmio(&p, 0) == QUICK_CANNOT_SAY,
          "and neither can a probe-only one");

    reset_out();
    CHECK(report_mmio_unavailable(&p, 1) == 0,
          "so it is not reported as a hard disqualifier");
    CHECK(has("NOT QUALIFIED"), "the verdict says NOT QUALIFIED");
    CHECK(!has("DISQUALIFIED"), "and not DISQUALIFIED");
    CHECK(has("No controller fault inferred"),
          "with the same footnote the other tool-limited causes carry");

    /* And the two refusals that ARE about the controller keep disqualifying,
     * so the flag is not a way to soften every mapping failure. */
    base_pci(&p);
    p.mmio_reason = "BAR0 selects I/O space, and xHCI 5.2.1 requires a memory "
                    "BAR";
    CHECK(quick_classify_mmio(&p, 1) == QUICK_DISQUALIFIED,
          "a controller misdescribing its own BAR is still disqualified");
    reset_out();
    CHECK(report_mmio_unavailable(&p, 1) == 1, "and reported as one");

    /*
     * **The quick scan's REASON has to agree with the quick scan's VERDICT.**
     * `quick_reason` reconstructs from PCI state exactly as the classifier
     * does, so it reached the same "MMIO is dead" fallback the classifier
     * reached - and printed it beside a CANNOT SAY. Two answers to one
     * question, in the one place a user reads both on the same line.
     */
    /* `ipin` non-zero, because Interrupt Pin = 0 is answered before the MMIO
     * question and would mask what this is about. The mapper sets the reason
     * and the flag together, so the vectors do too. */
    base_pci(&p);
    p.ipin = 1;
    p.mmio_tool_limit = 1;
    p.mmio_reason = "the registers sit outside the window this tool maps - a "
                    "tool limit, not a controller fault";
    CHECK(strstr(quick_reason(&p, 0, 4, 1), "tool limit") != 0,
          "the quick reason names the tool limit");
    CHECK(strstr(quick_reason(&p, 0, 4, 1), "dead") == 0,
          "and does not call the window dead beside a CANNOT SAY");

    /* A recorded reason that is about the CONTROLLER is carried through too.
     * The verdict stays DISQUALIFIED, and "BAR0 MMIO is dead" would be the
     * wrong reason for it: the window decoded and the registers in it are
     * wrong, which is the distinction the reader needs. */
    base_pci(&p);
    p.ipin = 1;
    p.mmio_reason = "RTSOFF or DBOFF is zero, which places the runtime or "
                    "doorbell registers on top of the capability registers";
    CHECK(strstr(quick_reason(&p, 0, 4, 1), "RTSOFF") != 0,
          "a layout fault is named rather than called a dead window");
    CHECK(quick_classify_mmio(&p, 1) == QUICK_DISQUALIFIED,
          "and it is still a disqualification");

    base_pci(&p);
    p.ipin = 1;
    CHECK(strstr(quick_reason(&p, 0, 4, 1), "dead") != 0,
          "a window that really is dead, with no recorded reason, still says so");
}

/*
 * **An I/O-space BAR0 outranks a temporary power or configuration state.**
 * xHCI 5.2.1 requires a memory BAR and the mapper refuses one before it
 * touches anything, so the refusal needs neither D0 nor MSE to be
 * established - and it stays true when somebody powers the part up, which the
 * D-state and MSE readings do not. Asked after them, a controller that is
 * both in D3 and misdescribing its BAR type came back CANNOT SAY on the
 * D-state and the definitive refusal was never reported (Codex review round
 * 4 of the 2026-09-16 audit).
 */
static void test_io_bar_outranks_power_and_mse(void)
{
    PCIINFO p;

    /* MSE clear, BAR0 = 0xE001: an I/O BAR on a controller whose memory
     * decoding is off. Both readings are true; only one of them is final. */
    base_pci(&p);
    p.ipin = 1;
    p.bar_lo = 0x0000E001UL;
    p.bar_phys = 0x0000E000UL;
    p.cmd_effective = 0;
    CHECK(quick_classify_mmio(&p, 0) == QUICK_DISQUALIFIED,
          "an I/O BAR disqualifies even with MSE clear on a probe-only run");
    CHECK(strstr(quick_reason(&p, 0, 4, 0), "I/O space") != 0,
          "and the reason names the BAR rather than the MSE bit");
    reset_out();
    CHECK(report_mmio_unavailable(&p, 0) == 1,
          "the long-form verdict disqualifies it on a probe-only run too");
    CHECK(!has("No controller fault inferred"),
          "with no reassurance under a disqualification");

    /* And out of D0, which is the other branch that used to mask it. */
    base_pci(&p);
    p.ipin = 1;
    p.bar_lo = 0x0000E001UL;
    p.bar_phys = 0x0000E000UL;
    p.has_pm = 1;
    p.pm_state = 3;
    CHECK(quick_classify_mmio(&p, 1) == QUICK_DISQUALIFIED,
          "an I/O BAR disqualifies even in D3");
    CHECK(strstr(quick_reason(&p, 0, 4, 1), "I/O space") != 0,
          "and the reason names the BAR rather than the D-state");
    reset_out();
    report_mmio_dead(&p);
    CHECK(has("I/O space"), "the long-form cause agrees, with no reason recorded");

    /* And the long-form VERDICT, which is the third function that asks this
     * and the one the first cut of the fix missed: it returned 1 - a hard
     * disqualifier - while printing the D-state reassurance and advice about
     * powering the part up. */
    reset_out();
    CHECK(report_mmio_unavailable(&p, 1) == 1,
          "the long-form verdict disqualifies an I/O BAR in D3");
    CHECK(has("I/O space"), "and says so");
    CHECK(!has("No controller fault inferred"),
          "without the reassurance that contradicts its own return value");
    CHECK(!has("transition it to D0"),
          "and without advice that would not help");

    /* A memory BAR with MSE clear is unchanged: that IS the MSE reading. */
    base_pci(&p);
    p.ipin = 1;
    p.cmd_effective = 0;
    CHECK(quick_classify_mmio(&p, 0) == QUICK_CANNOT_SAY,
          "a memory BAR with MSE clear still cannot say on a probe-only run");
    CHECK(strstr(quick_reason(&p, 0, 4, 0), "Memory Space Enable") != 0,
          "and still names the MSE bit");
}

/* The verdict classifier: only genuine hardware/platform blockers may return
 * a disqualification. A temporary power or configuration state must not be
 * turned into a silicon verdict. */
static void test_unavailable_hard_disqualifiers(void)
{
    PCIINFO p;

    base_pci(&p);
    p.bar_hi = 1;
    reset_out();
    CHECK(report_mmio_unavailable(&p, 1) == 1, "BAR above 4 GB disqualifies");
    CHECK(has("DISQUALIFIED"), "BAR above 4 GB prints DISQUALIFIED");

    base_pci(&p);
    p.bar_phys = 0;
    reset_out();
    CHECK(report_mmio_unavailable(&p, 1) == 1, "unassigned BAR disqualifies");

    /* Active run: the tool tried to set MSE and it did not stick. */
    base_pci(&p);
    p.cmd_effective = 0;
    reset_out();
    CHECK(report_mmio_unavailable(&p, 1) == 1, "MSE unsettable disqualifies");
    CHECK(has("could not be set"), "active MSE failure worded as a failure");

    /* Nothing explains it: that is the controller's problem. */
    base_pci(&p);
    reset_out();
    CHECK(report_mmio_unavailable(&p, 1) == 1,
          "dead window with no excuse disqualifies");
}

static void test_unavailable_inconclusive(void)
{
    PCIINFO p;
    int d;

    /* Powered down is not defective, in either mode. */
    for (d = 1; d <= 3; d++) {
        base_pci(&p);
        p.has_pm = 1;
        p.pm_state = (u8)d;
        reset_out();
        CHECK(report_mmio_unavailable(&p, 1) == 0,
              "D-state is not a controller fault (active)");
        CHECK(has("No controller fault inferred"),
              "D-state says no fault inferred");
        reset_out();
        CHECK(report_mmio_unavailable(&p, 0) == 0,
              "D-state is not a controller fault (probe)");
    }

    /* Probe-only leaves MSE alone by contract, so finding it clear says
     * nothing about the silicon. */
    base_pci(&p);
    p.cmd_effective = 0;
    reset_out();
    CHECK(report_mmio_unavailable(&p, 0) == 0,
          "MSE clear in a read-only probe is not a fault");
    CHECK(has("read-only probe left it unchanged"),
          "probe MSE wording explains why");
    CHECK(!has("DISQUALIFIED"), "probe MSE prints no disqualification");
}

/*
 * C7 Intel mux verdict. None of this is reachable in QEMU - no emulated
 * controller carries a 7/8-series VID/DID, so qual_intel_ports() returns
 * before its first config read - which makes these the only checks these
 * branches ever get: no machine in this project has an Intel 7/8-series mux,
 * so C7 has never executed on hardware.
 *
 * The geometry is the one documented in docs/usb-xhci-info/xhci-programming.md: a
 * 7-series PCH with four switchable USB2 ports. XUSB2PRM says which ports may
 * be switched; XUSB2PR says where each currently points.
 */
static void set_mux(PCIINFO *p, u32 pr, u32 prm)
{
    base_pci(p);
    p->xusb2pr = pr;
    p->xusb2prm = prm;
    p->usb3_pssen = 0;
    p->usb3prm = 0x0000000FUL;
}

static void test_xusb2pr_routed_and_not(void)
{
    PCIINFO p;

    /* Every switchable port already pointed at xHCI. */
    set_mux(&p, 0x0000000FUL, 0x0000000FUL);
    reset_out();
    CHECK(report_xusb2pr(&p) == XUSB2PR_ROUTED, "full mask routed = ROUTED");
    CHECK(has("already routed to xHCI"), "ROUTED says so");
    CHECK(has("XUSB2PR=0000000F"), "raw words always printed");

    /* Firmware default on a dual-stack machine: ports still on EHCI. */
    set_mux(&p, 0x00000000UL, 0x0000000FUL);
    reset_out();
    CHECK(report_xusb2pr(&p) == XUSB2PR_NOT_ROUTED,
          "nothing routed = NOT_ROUTED");
    CHECK(!has("already routed"), "NOT_ROUTED does not claim routed");

    /* Partial routing - the case where a firmware setting moves only some of
     * the switchable connectors. Still NOT_ROUTED: a switchable port on EHCI is exactly
     * the state that makes that port report no connect. */
    set_mux(&p, 0x00000003UL, 0x0000000FUL);
    reset_out();
    CHECK(report_xusb2pr(&p) == XUSB2PR_NOT_ROUTED,
          "partial routing is not full routing");

    /* Bits set outside the switchable mask must not satisfy it. */
    set_mux(&p, 0x000000F0UL, 0x0000000FUL);
    reset_out();
    CHECK(report_xusb2pr(&p) == XUSB2PR_NOT_ROUTED,
          "bits outside the mask do not satisfy it");
}

/*
 * The false-negative guards. "(pr & prm) == prm" is vacuously true for a zero
 * mask and for all-ones words, so without these the tool reports "already
 * routed to xHCI" about a machine with nothing routed - retiring the routing
 * question with the one answer that ends the investigation.
 */
static void test_xusb2pr_unusable_evidence(void)
{
    PCIINFO p;

    /* Zero mask: vacuously "all routed". */
    set_mux(&p, 0x00000000UL, 0x00000000UL);
    reset_out();
    CHECK(report_xusb2pr(&p) == XUSB2PR_UNDETERMINED,
          "zero mask is not proof of routing");
    CHECK(has("UNDETERMINED"), "zero mask says UNDETERMINED");
    CHECK(has("not the same as correct"), "and says what that is not");
    CHECK(!has("already routed to xHCI"), "zero mask never claims routed");

    /* Undecoded config space, both words. */
    set_mux(&p, 0xFFFFFFFFUL, 0xFFFFFFFFUL);
    reset_out();
    CHECK(report_xusb2pr(&p) == XUSB2PR_UNDETERMINED,
          "all-ones mask is not proof of routing");
    CHECK(!has("already routed to xHCI"), "all ones never claims routed");

    /* All-ones XUSB2PR against a legal mask satisfies the bit test and means
     * nothing. This is the case a mask-only guard would still get wrong. */
    set_mux(&p, 0xFFFFFFFFUL, 0x0000000FUL);
    reset_out();
    CHECK(report_xusb2pr(&p) == XUSB2PR_UNDETERMINED,
          "all-ones XUSB2PR with a usable mask is not proof");
    CHECK(has("not usable evidence"),
          "all-ones value is called unusable, not diagnosed");
    CHECK(has("Candidate causes"),
          "candidate causes are offered without choosing one");
    CHECK(!has("is not decoding"),
          "no single cause asserted for an all-ones read");
    CHECK(!has("already routed to xHCI"),
          "all-ones value never claims routed");
}

/* ------------------------------------------------------------------ */
/* Task 11-V.8 - the read-only three-outcome classifier                 */
/* ------------------------------------------------------------------ */

/*
 * The no-argument quick scan is now the common path, so these branches are the
 * first thing a first-time user meets - and the QEMU matrix can reach almost
 * none of them: SeaBIOS always leaves MSE set and the BAR assigned, no emulated
 * controller has a PM capability, and none reports Interrupt Pin = 0. This
 * runner is the only coverage they get before metal, exactly as it is for the
 * classifier that lives beside them.
 */
static void test_quick_healthy(void)
{
    PCIINFO p;

    base_pci(&p);
    p.ipin = 1;
    CHECK(quick_classify(&p, 1, 4, 0) == QUICK_LOOKS_OK,
          "a mapped controller with USB2 ports looks qualified");
    CHECK(quick_reason(&p, 1, 4, 0)[0] == '\0',
          "and a healthy controller has no reason string to print");
}

/*
 * The unconditional one. Phase 0's checkpoint makes Interrupt Pin = 0 a
 * disqualifier whatever else is true, because neither target's USB stack has an
 * MSI path - so it must beat a perfectly healthy window rather than being
 * checked after it.
 */
static void test_quick_no_interrupt_pin(void)
{
    PCIINFO p;

    base_pci(&p);
    p.ipin = 0;
    CHECK(quick_classify(&p, 1, 4, 0) == QUICK_DISQUALIFIED,
          "Interrupt Pin = 0 disqualifies a controller that is otherwise fine");
    CHECK(strstr(quick_reason(&p, 1, 4, 0), "Interrupt Pin") != 0,
          "and says so");

    /* And it is not softened by an active run, nor by a dead window on top. */
    CHECK(quick_classify(&p, 1, 4, 1) == QUICK_DISQUALIFIED,
          "an active run does not soften it");
    CHECK(quick_classify(&p, 0, 0, 0) == QUICK_DISQUALIFIED,
          "and it still decides when the window is dead too");
}

/* No USB2 protocol ports: the driver manages only those, so a controller with
 * none has nothing for it to do. */
static void test_quick_no_usb2_ports(void)
{
    PCIINFO p;

    base_pci(&p);
    p.ipin = 1;
    CHECK(quick_classify(&p, 1, 0, 0) == QUICK_DISQUALIFIED,
          "no USB2 protocol ports disqualifies");
    CHECK(strstr(quick_reason(&p, 1, 0, 0), "USB2") != 0, "and says so");
}

/*
 * The third outcome, and the reason there are three. A read-only pass may not
 * power a device up or set Memory Space Enable, so neither state is evidence
 * about the silicon - "cannot say" is the honest answer and a two-way verdict
 * would have to turn one of them into a fault.
 */
static void test_quick_cannot_say(void)
{
    PCIINFO p;
    int d;

    for (d = 1; d <= 3; d++) {
        base_pci(&p);
        p.ipin = 1;
        p.has_pm = 1;
        p.pm_state = (u8)d;
        CHECK(quick_classify(&p, 0, 0, 0) == QUICK_CANNOT_SAY,
              "a controller not in D0 is not a fault a probe can name");
        CHECK(strstr(quick_reason(&p, 0, 0, 0), "D0") != 0,
              "and the reason names D0");
    }

    base_pci(&p);
    p.ipin = 1;
    p.cmd_effective = 0;
    CHECK(quick_classify(&p, 0, 0, 0) == QUICK_CANNOT_SAY,
          "MSE clear on a read-only pass is a state, not a fault");
    /* The same observation on an ACTIVE run is a fault, because the tool tried
     * to set the bit and it did not stick. One observation, two readings, and
     * the parameter is what separates them. */
    CHECK(quick_classify(&p, 0, 0, 1) == QUICK_DISQUALIFIED,
          "the same bit clear after an active attempt is a disqualifier");
    CHECK(strstr(quick_reason(&p, 0, 0, 1), "could not be set") != 0,
          "and is worded as a failure rather than as a state");
}

/* A dead window with nothing to explain it is the controller's problem, in
 * either mode - the branch that stops "cannot say" from swallowing real
 * faults. */
static void test_quick_dead_window_with_no_excuse(void)
{
    PCIINFO p;

    base_pci(&p);
    p.ipin = 1;
    CHECK(quick_classify(&p, 0, 0, 0) == QUICK_DISQUALIFIED,
          "a dead window with MSE set, a BAR below 4 GB and D0 disqualifies");

    base_pci(&p);
    p.ipin = 1;
    p.bar_hi = 1;
    CHECK(quick_classify(&p, 0, 0, 0) == QUICK_DISQUALIFIED,
          "BAR0 above 4 GB disqualifies on a read-only pass too");

    base_pci(&p);
    p.ipin = 1;
    p.bar_phys = 0;
    CHECK(quick_classify(&p, 0, 0, 0) == QUICK_DISQUALIFIED,
          "an unassigned BAR0 disqualifies");
    CHECK(strstr(quick_reason(&p, 0, 0, 0), "unassigned") != 0,
          "and says which");
}

/*
 * **The one that matters most: the quick scan and the full run must never
 * disagree.** A quick scan that said LOOKS QUALIFIED where a full run finds a
 * read-only disqualifier would be worse than no quick scan at all, which is why
 * there is one classifier and `report_mmio_unavailable` consults it rather than
 * repeating the conditions. This vector is what holds the two together.
 */
static void test_quick_agrees_with_the_full_run(void)
{
    PCIINFO p;
    int d;
    int active;

    for (active = 0; active <= 1; active++) {
        base_pci(&p);
        p.ipin = 1;
        reset_out();
        CHECK(report_mmio_unavailable(&p, active) ==
                  (quick_classify(&p, 0, 1, active) == QUICK_DISQUALIFIED),
              "dead window with no excuse: both paths agree");

        base_pci(&p);
        p.ipin = 1;
        p.bar_hi = 1;
        reset_out();
        CHECK(report_mmio_unavailable(&p, active) ==
                  (quick_classify(&p, 0, 1, active) == QUICK_DISQUALIFIED),
              "BAR above 4 GB: both paths agree");

        base_pci(&p);
        p.ipin = 1;
        p.cmd_effective = 0;
        reset_out();
        CHECK(report_mmio_unavailable(&p, active) ==
                  (quick_classify(&p, 0, 1, active) == QUICK_DISQUALIFIED),
              "MSE clear: both paths agree, in both modes");

        for (d = 1; d <= 3; d++) {
            base_pci(&p);
            p.ipin = 1;
            p.has_pm = 1;
            p.pm_state = (u8)d;
            reset_out();
            CHECK(report_mmio_unavailable(&p, active) ==
                      (quick_classify(&p, 0, 1, active) == QUICK_DISQUALIFIED),
                  "a D-state: both paths agree");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Roadmap task 35.1: the speed tables and the raw xECP chain          */
/* ------------------------------------------------------------------ */

/* CTRL is large (device records, DMA pointers); keep it off the stack. */
static CTRL tc;

static void add_proto(u8 major, u8 portoff, u8 portcnt, u8 psic,
                      const u32 *psi)
{
    PROTOCAP *pr = &tc.proto[tc.nproto++];
    int k;

    pr->major = major;
    pr->minor = 0;
    pr->portoff = portoff;
    pr->portcnt = portcnt;
    pr->slottype = 0;
    pr->psic = psic;
    pr->npsi = psic;
    for (k = 0; k < (int)psic; k++)
        pr->psi[k] = psi[k];
}

/*
 * The E460's (8086:9D2F) USB 3 protocol as read on 2026-10-06 (roadmap 35.0,
 * issue 11): PSIC 3, Intel's SSIC rates 1248/2496/4992 Mb/s on IDs 1-3 and no
 * ID 4. Expected strings transcribed by hand from xHCI 1.2c 7.2.2.1.2: PSIV
 * 3:0, PSIE 5:4 (2 = Mb/s), PLT 7:6, PFD 8, LP 15:14, PSIM 31:16.
 */
static void test_psi_e460_usb3_ssic_table(void)
{
    static const u32 u3[3] = { 0x04E00121UL, 0x09C00122UL, 0x13800123UL };

    memset(&tc, 0, sizeof(tc));
    add_proto(3, 13, 6, 3, u3);
    reset_out();
    report_protocols(&tc, qprintf);
    CHECK(has("  Protocol USB 3.0 @0000: ports 13-18, slot type 0, "
              "PSIC 3\n"), "E460 USB3: protocol line");
    CHECK(has("    PSI 04E00121  PSIV  1   1248 Mb/s  symmetric  PFD 1  "
              "LP 0 (SuperSpeed)\n"), "E460 USB3: SSIC 1248 Mb/s entry");
    CHECK(has("    PSI 09C00122  PSIV  2   2496 Mb/s  symmetric  PFD 1  "
              "LP 0 (SuperSpeed)\n"), "E460 USB3: SSIC 2496 Mb/s entry");
    CHECK(has("    PSI 13800123  PSIV  3   4992 Mb/s  symmetric  PFD 1  "
              "LP 0 (SuperSpeed)\n"), "E460 USB3: SSIC 4992 Mb/s entry");
    CHECK(has("  WARNING: USB 3 PSI table does not list PSIV 4 (default "
              "SuperSpeed). If\n"
              "    PORTSC reports ID 4 for a 5 Gb/s device, as Sunrise Point "
              "does, a driver\n"
              "    that trusts the table strictly cannot decode it "
              "(issue 11)\n"), "E460 USB3: the issue 11 warning");
    CHECK(psi_usb3_lacks_ss(&tc.proto[0]) == 1, "E460 USB3: lacks PSIV 4");
    {
        const char *s;

        for (s = out; *s != '\0'; ) {
            const char *eol = strchr(s, '\n');
            size_t len = eol ? (size_t)(eol - s) : strlen(s);

            CHECK(len <= 79, "E460 USB3: every line fits 80 columns");
            s += len + (eol ? 1 : 0);
        }
    }

    /* A table only partly read (the rest past the mapped window) might hold
     * ID 4 among the unread entries: no omission is declared. */
    tc.proto[0].npsi = 2;
    reset_out();
    report_protocols(&tc, qprintf);
    CHECK(psi_usb3_lacks_ss(&tc.proto[0]) == 0, "partial table: no verdict");
    CHECK(!has("WARNING") && has("(2 of PSIC 3 entries read"),
          "partial table: the shortfall, not the warning");
}

/* No warning where it does not apply: a USB 3 table that lists ID 4, a USB 3
 * protocol with PSIC 0 (QEMU's), and a USB 2 table, whose LP is reserved. */
static void test_psi_no_warning_cases(void)
{
    static const u32 u3ss[4] = {
        0x04E00121UL, 0x09C00122UL, 0x13800123UL,
        0x00050134UL    /* PSIV 4, 5 Gb/s, PFD 1, LP 0 */
    };
    static const u32 u2[3] = {
        0x000C0021UL,   /* PSIV 1, 12 Mb/s */
        0x05DC0012UL,   /* PSIV 2, 1500 Kb/s */
        0x01E00023UL    /* PSIV 3, 480 Mb/s */
    };

    memset(&tc, 0, sizeof(tc));
    add_proto(2, 1, 12, 3, u2);
    add_proto(3, 13, 6, 4, u3ss);
    add_proto(3, 0, 0, 0, u3ss);
    reset_out();
    report_protocols(&tc, qprintf);
    CHECK(!has("WARNING"), "PSIV 4 listed, PSIC 0, USB2: no warning");
    CHECK(has("    PSI 00050134  PSIV  4      5 Gb/s  symmetric  PFD 1  "
              "LP 0 (SuperSpeed)\n"), "USB3 5 Gb/s entry");
    CHECK(has("    PSI 000C0021  PSIV  1     12 Mb/s  symmetric  PFD 0  "
              "LP 0\n"), "USB2 Full Speed entry, no LP name");
    CHECK(has("    PSI 05DC0012  PSIV  2   1500 Kb/s  symmetric  PFD 0  "
              "LP 0\n"), "USB2 Low Speed entry in Kb/s");
    CHECK(has("    PSI 01E00023  PSIV  3    480 Mb/s  symmetric  PFD 0  "
              "LP 0\n"), "USB2 High Speed entry");
    CHECK(has("  Protocol USB 3.0 @0000: no ports, slot type 0, PSIC 0\n"),
          "PSIC 0 protocol line kept");
    CHECK(psi_usb3_lacks_ss(&tc.proto[0]) == 0, "USB2: never warns");
    CHECK(psi_usb3_lacks_ss(&tc.proto[1]) == 0, "USB3 with 4: no warning");
    CHECK(psi_usb3_lacks_ss(&tc.proto[2]) == 0, "USB3 PSIC 0: no warning");
}

/* The remaining fields: LP 1 (SuperSpeedPlus), both asymmetric PLTs, the
 * reserved PLT and LP, and b/s. */
static void test_psi_field_decode(void)
{
    static const u32 u3[5] = {
        0x000A4135UL,   /* PSIV 5, 10 Gb/s, PFD 1, LP 1 */
        0x000A01B6UL,   /* PSIV 6, 10 Gb/s, PLT 2 asym RX, PFD 1 */
        0x000A01F7UL,   /* PSIV 7, 10 Gb/s, PLT 3 asym TX, PFD 1 */
        0x0001C078UL,   /* PSIV 8, 1 Gb/s, PLT 1, LP 3 */
        0x03E80009UL    /* PSIV 9, 1000 b/s */
    };

    memset(&tc, 0, sizeof(tc));
    add_proto(3, 1, 2, 5, u3);
    reset_out();
    report_protocols(&tc, qprintf);
    CHECK(has("    PSI 000A4135  PSIV  5     10 Gb/s  symmetric  PFD 1  "
              "LP 1 (SuperSpeedPlus)\n"), "LP 1 is SuperSpeedPlus");
    CHECK(has("    PSI 000A01B6  PSIV  6     10 Gb/s  asym RX    PFD 1  "
              "LP 0 (SuperSpeed)\n"), "PLT 2 asym RX");
    CHECK(has("    PSI 000A01F7  PSIV  7     10 Gb/s  asym TX    PFD 1  "
              "LP 0 (SuperSpeed)\n"), "PLT 3 asym TX");
    CHECK(has("    PSI 0001C078  PSIV  8      1 Gb/s  reserved   PFD 0  "
              "LP 3 (reserved)\n"), "PLT 1 and LP 3 reserved");
    CHECK(has("    PSI 03E80009  PSIV  9   1000 b/s   symmetric  PFD 0  "
              "LP 0 (SuperSpeed)\n"), "PSIE 0 is b/s");
    CHECK(has("WARNING: USB 3 PSI table does not list PSIV 4"),
          "a USB 3 table of 5-9 still lacks 4 (the warning says \"if\")");
}

/* The raw chain: rows of four with their BAR0 offsets, a capability cut at
 * the per-capability bound, and each reason a record ends. */
static void test_xcap_dump(void)
{
    int k;

    memset(&tc, 0, sizeof(tc));
    reset_out();
    report_xcap_dump(&tc, qprintf);
    CHECK(has("  xECP: 0 - no extended capabilities\n"), "no xECP");

    tc.xecp_off = 0x8000;
    tc.nxcap = 2;
    tc.xcap[0].off = 0x8000;
    tc.xcap[0].id = 1;
    tc.xcap[0].next = 4;
    tc.xcap[0].first = 0;
    tc.xcap[0].ndw = 4;
    tc.xcap[0].want = 4;
    tc.xdump[0] = 0x00000401UL;
    tc.xdump[1] = 0xE0000000UL;
    tc.xdump[2] = 0x11111111UL;
    tc.xdump[3] = 0x22222222UL;
    tc.xcap[1].off = 0x8010;
    tc.xcap[1].id = 2;
    tc.xcap[1].next = 0;
    tc.xcap[1].first = 4;
    tc.xcap[1].ndw = 7;
    tc.xcap[1].want = 7;
    tc.xdump[4] = 0x03000002UL;
    tc.xdump[5] = 0x20425355UL;
    tc.xdump[6] = 0x30000613UL;
    tc.xdump[7] = 0x00000000UL;
    tc.xdump[8] = 0x04E00121UL;
    tc.xdump[9] = 0x09C00122UL;
    tc.xdump[10] = 0x13800123UL;
    tc.nxdump = 11;
    tc.xcap_stop = XCAP_STOP_END;
    reset_out();
    report_xcap_dump(&tc, qprintf);
    CHECK(has("  Extended capabilities, raw dwords (xECP 8000):\n"),
          "dump header");
    CHECK(has("    cap 8000 ID   1 next   4  USB Legacy Support, 4 dwords\n"
              "      8000: 00000401 E0000000 11111111 22222222\n"),
          "legacy support cap, one row");
    CHECK(has("    cap 8010 ID   2 next   0  Supported Protocol, 7 dwords\n"
              "      8010: 03000002 20425355 30000613 00000000\n"
              "      8020: 04E00121 09C00122 13800123\n"),
          "protocol cap with its PSI dwords, short last row");
    CHECK(!has("(a header") && !has("(dump bound") && !has("(the walk"),
          "chain ended by its own next pointer: no note");

    /* a vendor capability cut at the per-capability bound */
    tc.nxcap = 1;
    tc.xcap[0].id = 192;
    tc.xcap[0].next = 255;
    tc.xcap[0].ndw = XCAP_DUMP_PER_CAP;
    tc.xcap[0].want = 255;
    for (k = 0; k < XCAP_DUMP_PER_CAP; k++)
        tc.xdump[k] = (u32)k;
    tc.xcap_stop = XCAP_STOP_FULL;
    tc.xcap_full = 1;
    reset_out();
    report_xcap_dump(&tc, qprintf);
    CHECK(has("    cap 8000 ID 192 next 255  vendor defined, 32 of 255 "
              "dwords\n"), "per-capability bound named");
    CHECK(has("      8070: 0000001C 0000001D 0000001E 0000001F\n"),
          "eighth row at +70h");
    CHECK(has("(dump bound reached, 32 capabilities / 256 dwords"),
          "total bound named");

    tc.xcap[0].ndw = 1;
    tc.xcap[0].want = 1;
    tc.xdump[0] = 0xFFFFFFFFUL;
    tc.xcap_stop = XCAP_STOP_ONES;
    tc.xcap_full = 0;
    reset_out();
    report_xcap_dump(&tc, qprintf);
    CHECK(has("      8000: FFFFFFFF\n") &&
          has("(a header read all ones: raw recording stops here)"),
          "all-ones header stops the record");

    tc.xcap_stop = XCAP_STOP_WALK;
    reset_out();
    report_xcap_dump(&tc, qprintf);
    CHECK(has("(the walk stopped at the mapped window or its 64-step guard)"),
          "walk bound named");
}

/*
 * The production walk and its raw recorder (xcap_walk / xcap_record in
 * mmiodiag.c, the code XHCIQUAL runs against MMIO) driven through a checked
 * fake BAR. Every read is counted, and one that is unaligned or reaches past
 * the window is a failure in itself (fbad), whatever the walk then does.
 */
static u32 fbar[0x4000];      /* 64 KB, the tool's own BAR_MAP_SIZE */
static u32 fwindow;
static int freads;
static int fbad;

static u32 fake_rd(void *ctx, u32 off)
{
    (void)ctx;
    freads++;
    if ((off & 3UL) != 0 || off + 4 < off || off + 4 > fwindow ||
        off + 4 > sizeof(fbar)) {
        fbad++;
        return 0xFFFFFFFFUL;
    }
    return fbar[off >> 2];
}

static void fake_reset(u32 window, u32 fill)
{
    u32 i;

    for (i = 0; i < sizeof(fbar) / sizeof(fbar[0]); i++)
        fbar[i] = fill;
    fwindow = window;
    freads = 0;
    fbad = 0;
    memset(&tc, 0, sizeof(tc));
    reset_out();
}

static void fput(u32 off, u32 v)
{
    fbar[off >> 2] = v;
}

/* a Supported Protocol capability: header, name "USB ", ports, slot, PSI */
static void fput_proto(u32 off, u32 major, u32 next, u32 portoff,
                       u32 portcnt, u32 psic, const u32 *psi)
{
    u32 k;

    fput(off, (major << 24) | (next << 8) | XECP_ID_PROTO);
    fput(off + 4, 0x20425355UL);
    fput(off + 8, (psic << 28) | (portcnt << 8) | portoff);
    fput(off + 0x0C, 0);
    for (k = 0; k < psic; k++)
        fput(off + 0x10 + k * 4, psi[k]);
}

/* The E460's chain as the 35.0 words describe it: legacy support, USB 2
 * (PSIC 3), USB 3 (PSIC 3, SSIC only), terminal next 0. */
static void test_walk_e460_chain(void)
{
    static const u32 u2[3] = { 0x000C0021UL, 0x05DC0012UL, 0x01E00023UL };
    static const u32 u3[3] = { 0x04E00121UL, 0x09C00122UL, 0x13800123UL };
    int k;

    fake_reset(0x10000UL, 0);
    fput(0x600, 0x00000201UL);                /* LEGSUP, next 2 */
    fput_proto(0x608, 2, 8, 1, 12, 3, u2);
    fput_proto(0x628, 3, 0, 13, 6, 3, u3);
    xcap_walk(&tc, 0x600, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "E460 walk: every read inside the window");
    CHECK(tc.xecp_off == 0x600 && tc.nxcap == 3 &&
          tc.xcap_stop == XCAP_STOP_END, "E460 walk: three caps, chain end");
    CHECK(tc.xcap[0].off == 0x600 && tc.xcap[0].want == 2 &&
          tc.xcap[0].ndw == 2, "E460 walk: legacy support span 2");
    CHECK(tc.xcap[1].off == 0x608 && tc.xcap[1].want == 8 &&
          tc.xcap[1].ndw == 8, "E460 walk: USB2 span to next");
    CHECK(tc.xcap[2].off == 0x628 && tc.xcap[2].next == 0 &&
          tc.xcap[2].want == 7 && tc.xcap[2].ndw == 7,
          "E460 walk: terminal USB3 takes 4 + PSIC");
    for (k = 0; k < 3; k++)
        CHECK(tc.xdump[tc.xcap[2].first + 4 + k] == u3[k],
              "E460 walk: USB3 PSI dwords recorded raw");
    CHECK(tc.nxdump == 17, "E460 walk: 2 + 8 + 7 dwords");
    CHECK(tc.legsup_off == 0x600 && tc.nproto == 2 &&
          tc.proto[1].portoff == 13 && tc.proto[1].portcnt == 6 &&
          tc.proto[1].npsi == 3 && tc.proto[1].psi[2] == u3[2],
          "E460 walk: the decoded facts unchanged");
    CHECK(!has("NOTE"), "E460 walk: no early-stop note");
    report_protocols(&tc, qprintf);
    CHECK(has("WARNING: USB 3 PSI table does not list PSIV 4"),
          "E460 walk: end to end, the warning");
}

/* PSIC 15 on the last capability: 19 dwords, all 15 PSI entries. */
static void test_walk_psic15(void)
{
    u32 psi[15];
    u32 k;

    for (k = 0; k < 15; k++)
        psi[k] = 0x00050130UL | (k + 1);
    fake_reset(0x10000UL, 0);
    fput_proto(0x100, 3, 0, 1, 2, 15, psi);
    xcap_walk(&tc, 0x100, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "PSIC 15: reads inside the window");
    CHECK(tc.nxcap == 1 && tc.xcap[0].want == 19 && tc.xcap[0].ndw == 19,
          "PSIC 15: 4 + 15 dwords");
    CHECK(tc.xdump[18] == psi[14], "PSIC 15: last PSI dword recorded");
    CHECK(tc.proto[0].npsi == 15, "PSIC 15: all entries decoded");
    CHECK(psi_usb3_lacks_ss(&tc.proto[0]) == 0, "PSIC 15: lists PSIV 4");
}

/* A span shorter than the capability's known size: a legacy support cap with
 * next 1 is recorded as 1 dword; a protocol cap whose next lands inside its
 * own PSI dwords still records them all, and the overlapping capability that
 * header names is recorded from there. */
static void test_walk_short_and_overlapping_spans(void)
{
    static const u32 u2[3] = { 0x000C0021UL, 0x05DC0012UL, 0x01E00023UL };

    fake_reset(0x10000UL, 0);
    fput(0x0FC, 0x00000101UL);                /* LEGSUP, next 1 */
    fput_proto(0x100, 2, 6, 1, 4, 3, u2);     /* next lands on PSI[2] */
    xcap_walk(&tc, 0x0FC, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "short spans: reads inside the window");
    CHECK(tc.nxcap == 3, "short spans: three records");
    CHECK(tc.xcap[0].want == 1 && tc.xcap[0].ndw == 1,
          "short spans: legacy support cut to its span");
    CHECK(tc.xcap[1].want == 7 && tc.xcap[1].ndw == 7,
          "short spans: protocol keeps 4 + PSIC past a short next");
    CHECK(tc.xcap[2].off == 0x118 && tc.xcap[2].id == 0x23 &&
          tc.xcap[2].next == 0 && tc.xcap[2].want == 4 &&
          tc.xcap[2].ndw == 4, "short spans: the overlapping header");
    CHECK(tc.xcap_stop == XCAP_STOP_END, "short spans: chain end");
}

/* The window's edge: a capability running past it is cut there, and a
 * protocol table past it is read only in part (no PSIV 4 verdict). */
static void test_walk_window_edge(void)
{
    static const u32 psi[15] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                 13, 14, 15 };

    fake_reset(0x400UL, 0);
    fput(0x3BC, 0x000020C0UL);                /* vendor, next 32 dwords */
    xcap_walk(&tc, 0x3BC, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "window edge: no read past the window");
    CHECK(tc.nxcap == 1 && tc.xcap[0].want == 32 && tc.xcap[0].ndw == 17,
          "window edge: vendor cap cut at the window");
    CHECK(tc.xcap_stop == XCAP_STOP_WALK, "window edge: walk stop named");
    CHECK(has("NOTE: extended capability list not walked to its end "
              "(next at 0000043C)"), "window edge: the walk's own note");

    fake_reset(0x400UL, 0);
    fput_proto(0x3BC, 3, 0, 1, 2, 15, psi);
    xcap_walk(&tc, 0x3BC, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "window edge, protocol: no read past the window");
    CHECK(tc.xcap[0].want == 19 && tc.xcap[0].ndw == 17,
          "window edge, protocol: record cut at the window");
    CHECK(tc.proto[0].psic == 15 && tc.proto[0].npsi == 13,
          "window edge, protocol: 13 of 15 PSI entries read");
    CHECK(psi_usb3_lacks_ss(&tc.proto[0]) == 0,
          "window edge, protocol: partial table, no verdict");
}

/* The per-capability bound: a 64-dword span records 32. */
static void test_walk_per_cap_bound(void)
{
    fake_reset(0x10000UL, 0);
    fput(0x100, 0x000040C0UL);                /* vendor, next 64 dwords */
    fput(0x200, 0x000000C1UL);                /* vendor, terminal */
    xcap_walk(&tc, 0x100, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "per-cap bound: reads inside the window");
    CHECK(tc.nxcap == 2 && tc.xcap[0].want == 64 &&
          tc.xcap[0].ndw == XCAP_DUMP_PER_CAP,
          "per-cap bound: 32 of 64 dwords");
    CHECK(tc.xcap[1].off == 0x200 && tc.xcap[1].first == XCAP_DUMP_PER_CAP,
          "per-cap bound: next record follows");
    CHECK(tc.xcap_stop == XCAP_STOP_END, "per-cap bound: chain end");
}

/* The capability-count and total-dword bounds; the walk itself goes on. */
static void test_walk_exhaustion(void)
{
    u32 off;
    int i;

    /* 40 one-dword caps, the last a terminal LEGSUP */
    fake_reset(0x10000UL, 0);
    for (i = 0, off = 0x100; i < 39; i++, off += 4)
        fput(off, 0x000001C0UL);
    fput(off, 0x00000001UL);
    xcap_walk(&tc, 0x100, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "32 caps: reads inside the window");
    CHECK(tc.nxcap == XCAP_DUMP_CAPS + 1 &&
          tc.nxdump == XCAP_DUMP_CAPS + 2 &&
          tc.xcap_stop == XCAP_STOP_FULL, "32 caps: count bound");
    CHECK(tc.xcap[XCAP_DUMP_CAPS].off == off &&
          tc.xcap[XCAP_DUMP_CAPS].ndw == 2,
          "32 caps: the terminal USBLEGSUP is still recorded, two dwords");
    CHECK(tc.legsup_off == off, "32 caps: the walk still reaches the end");
    CHECK(!has("NOTE"), "32 caps: the walk ended on its own");

    /* nine 30-dword caps then a terminal: 8 x 30 + 16 = 256 */
    fake_reset(0x10000UL, 0);
    for (i = 0, off = 0x100; i < 9; i++, off += 30 * 4)
        fput(off, 0x00001EC0UL);
    fput(off, 0x000000C1UL);
    xcap_walk(&tc, 0x100, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "256 dwords: reads inside the window");
    CHECK(tc.nxcap == 9 && tc.xcap[8].want == 30 && tc.xcap[8].ndw == 16,
          "256 dwords: ninth cap cut by the total");
    CHECK(tc.nxdump == XCAP_DUMP_TOTAL && tc.xcap_stop == XCAP_STOP_FULL,
          "256 dwords: total bound");
}

/*
 * The HP EliteBook 850 G5's 8086:15DB (xhciqual/results/hp850g5-2026-10-06):
 * a USB 2 protocol, vendor capabilities that spend the 256-dword bound, then
 * the USB 3.1 protocol the dump is for. It is recorded from the reserve, with
 * its defined dwords only, and the decoded line names where it sits.
 */
static void test_walk_protocol_past_the_bound(void)
{
    static const u32 u2[3] = { 0x000C0021UL, 0x05DC0012UL, 0x01E00023UL };
    static const u32 u31[2] = { 0x00050134UL, 0x000A4135UL };
    u32 off;
    int i;

    fake_reset(0x10000UL, 0);
    fput_proto(0x8000, 2, 0x1C, 1, 2, 3, u2);
    for (i = 0, off = 0x8070; i < 9; i++, off += 0x100)
        fput(off, 0x000040C0UL);              /* vendor, next 64 dwords */
    fput_proto(off, 3, 20, 3, 2, 2, u31);
    fput(off, fbar[off >> 2] | (0x01UL << 16));   /* minor 01h, as Intel's */
    fput(off + 20 * 4, 0x000000C1UL);         /* vendor, terminal */
    xcap_walk(&tc, 0x8000, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "past the bound: reads inside the window");
    CHECK(tc.xcap_stop == XCAP_STOP_FULL, "past the bound: the bound was hit");
    CHECK(tc.nxcap == 10 && tc.xcap[9].off == off && tc.xcap[9].id == 2,
          "past the bound: the USB 3.1 protocol is recorded, the vendor "
          "capability after it is not");
    CHECK(tc.xcap[9].want == 20 && tc.xcap[9].ndw == 6,
          "past the bound: header and its two PSI dwords only");
    CHECK(tc.xdump[tc.xcap[9].first + 4] == 0x00050134UL &&
          tc.xdump[tc.xcap[9].first + 5] == 0x000A4135UL,
          "past the bound: the PSI words as read");
    CHECK(tc.nproto == 2 && tc.proto[1].off == off,
          "past the bound: decoded with its offset");
    reset_out();
    report_protocols(&tc, qprintf);
    CHECK(has("  Protocol USB 3.1 @8970: ports 3-4, slot type 0, "
              "PSIC 2\n"), "past the bound: protocol line");
    reset_out();
    report_xcap_dump(&tc, qprintf);
    CHECK(has("    cap 8970 ID   2 next  20  Supported Protocol, 6 of 20 "
              "dwords\n"), "past the bound: dump names the kept capability");
    CHECK(has("except Supported Protocol and USB Legacy Support"),
          "past the bound: the bound line says what is kept");
}

/*
 * A protocol capability that would cross the general bound: 255 dwords
 * spent, then a terminal one of 4 + PSIC 2. Its defined dwords all come from
 * the reserve, and the record ends marked as bound-reached.
 */
static void test_walk_protocol_across_the_bound(void)
{
    static const u32 u31[2] = { 0x00050134UL, 0x000A4135UL };
    u32 off;
    int i;

    fake_reset(0x10000UL, 0);
    for (i = 0, off = 0x100; i < 7; i++, off += 32 * 4)
        fput(off, 0x000020C0UL);              /* vendor, next 32 dwords */
    fput(off, 0x00001FC0UL);                  /* vendor, next 31 dwords */
    off += 31 * 4;
    fput_proto(off, 3, 0, 3, 2, 2, u31);      /* terminal */
    xcap_walk(&tc, 0x100, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "across the bound: reads inside the window");
    CHECK(tc.nxcap == 9 && tc.xcap[8].off == off && tc.xcap[8].ndw == 6,
          "across the bound: the protocol's six dwords, not its header alone");
    CHECK(tc.xdump[tc.xcap[8].first + 5] == 0x000A4135UL,
          "across the bound: its last PSI word as read");
    CHECK(tc.nxdump == 261 && tc.xcap_stop == XCAP_STOP_FULL &&
          tc.xcap_full, "across the bound: marked bound-reached");
}

/*
 * An all-ones header after the bound stops the reserve too: the USB 3.1
 * protocol behind it (the walk follows the FF next field, as it always has)
 * is decoded but not recorded, and both reasons are printed.
 */
static void test_walk_all_ones_after_the_bound(void)
{
    static const u32 u2[3] = { 0x000C0021UL, 0x05DC0012UL, 0x01E00023UL };
    static const u32 u31[2] = { 0x00050134UL, 0x000A4135UL };
    u32 off;
    int i;

    fake_reset(0x10000UL, 0);
    fput_proto(0x8000, 2, 0x1C, 1, 2, 3, u2);
    for (i = 0, off = 0x8070; i < 9; i++, off += 0x100)
        fput(off, 0x000040C0UL);              /* vendor, next 64 dwords */
    fput(off, 0xFFFFFFFFUL);                  /* dead header at 8970 */
    off += 0xFF * 4;
    fput_proto(off, 3, 0, 3, 2, 2, u31);      /* behind it, terminal */
    xcap_walk(&tc, 0x8000, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "ones after the bound: reads inside the window");
    CHECK(tc.nxcap == 9 && tc.xcap_stop == XCAP_STOP_ONES && tc.xcap_full,
          "ones after the bound: nothing more recorded, both facts kept");
    CHECK(tc.nproto == 2 && tc.proto[1].off == off,
          "ones after the bound: the protocol behind it still decoded");
    reset_out();
    report_xcap_dump(&tc, qprintf);
    CHECK(has("(dump bound reached, 32 capabilities / 256 dwords") &&
          has("(a header read all ones: raw recording stops here)"),
          "ones after the bound: both lines printed");
}

/* All ones: recording stops at the dead header; the walker, unchanged,
 * follows its FF next field until the window stops it, inside the window. */
static void test_walk_all_ones(void)
{
    fake_reset(0x10000UL, 0xFFFFFFFFUL);
    fput(0x100, 0x00000101UL);                /* LEGSUP, next 1 */
    xcap_walk(&tc, 0x100, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "all ones: every read inside the window");
    CHECK(tc.nxcap == 2 && tc.xcap[1].off == 0x104 &&
          tc.xcap[1].ndw == 1 && tc.xdump[tc.xcap[1].first] == 0xFFFFFFFFUL,
          "all ones: the dead header recorded once");
    CHECK(tc.xcap_stop == XCAP_STOP_ONES, "all ones: stop reason kept");
    CHECK(freads > 10, "all ones: the walk went on past it");
    CHECK(has("NOTE: extended capability list not walked to its end"),
          "all ones: the walk's own note");
    report_xcap_dump(&tc, qprintf);
    CHECK(has("(a header read all ones: raw recording stops here)"),
          "all ones: the dump says recording stopped");
}

/* The 64-step guard, and xECP 0. */
static void test_walk_guard_and_none(void)
{
    u32 off;
    int i;

    fake_reset(0x10000UL, 0);
    for (i = 0, off = 0x100; i < 70; i++, off += 4)
        fput(off, 0x000001C0UL);
    xcap_walk(&tc, 0x100, fwindow, fake_rd, 0);
    CHECK(fbad == 0, "guard: reads inside the window");
    CHECK(has("NOTE: extended capability list not walked to its end "
              "(next at 00000200)"), "guard: stops after 64 steps");
    CHECK(tc.xcap_stop == XCAP_STOP_FULL, "guard: the count bound first");

    fake_reset(0x10000UL, 0);
    xcap_walk(&tc, 0, fwindow, fake_rd, 0);
    CHECK(freads == 0 && tc.nxcap == 0 && tc.nproto == 0 &&
          tc.xcap_stop == XCAP_STOP_END, "xECP 0: nothing read");
}

int main(void)
{
    test_pm_absent();
    test_pm_typical_intel();
    test_pm_e460_field_values();
    test_pm_all_states_and_flags();
    test_pm_aux_current_table();
    test_pm_no_pme_support();
    test_pm_raw_words();
    test_status_clean();
    test_status_preexisting_not_attributed();
    test_status_new_error_attributed();
    test_status_mixed_old_and_new();
    test_status_no_recheck();
    test_status_all_error_bits();
    test_status_ignores_non_error_bits();
    test_dead_bar_beats_power_state();
    test_dead_causes();
    test_tool_limit_is_not_a_disqualification();
    test_io_bar_outranks_power_and_mse();
    test_unavailable_hard_disqualifiers();
    test_unavailable_inconclusive();
    test_xusb2pr_routed_and_not();
    test_xusb2pr_unusable_evidence();
    test_quick_healthy();
    test_quick_no_interrupt_pin();
    test_quick_no_usb2_ports();
    test_quick_cannot_say();
    test_quick_dead_window_with_no_excuse();
    test_quick_agrees_with_the_full_run();
    test_psi_e460_usb3_ssic_table();
    test_psi_no_warning_cases();
    test_psi_field_decode();
    test_xcap_dump();
    test_walk_e460_chain();
    test_walk_psic15();
    test_walk_short_and_overlapping_spans();
    test_walk_window_edge();
    test_walk_per_cap_bound();
    test_walk_exhaustion();
    test_walk_protocol_past_the_bound();
    test_walk_protocol_across_the_bound();
    test_walk_all_ones_after_the_bound();
    test_walk_all_ones();
    test_walk_guard_and_none();

    printf("test_mmiodiag: %d checks, %d failed\n", checks, failures);
    return failures;
}
