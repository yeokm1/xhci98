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

    printf("test_mmiodiag: %d checks, %d failed\n", checks, failures);
    return failures;
}
