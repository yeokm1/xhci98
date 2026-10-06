/*
 * mmiodiag.c - pure PCI power/decode diagnosis: PCIINFO in, report text out.
 *
 * Split out of report.c so it can be compiled and tested on the build host.
 * QEMU cannot reach any of this: SeaBIOS leaves Memory Space Enable set and
 * the BAR assigned, and no emulated USB controller exposes a PM capability,
 * so every matrix case takes the "capability absent" / mmio_ok path and none
 * of the branches below ever execute in the automated regression. The host
 * runner (xhciqual/test/test_mmiodiag.c) is the only coverage they get before
 * metal - which also makes it the cheapest check on the PCI PM bit positions,
 * since that specification is not mirrored in docs/references.
 *
 * The bit positions themselves were confirmed in the field: an
 * lspci -vv cross-check on the ThinkPad E460 agreed with this decoder on every
 * field, hardwired capability bits included, and the raw PMC/PMCSR words this
 * file prints match that decode (xhciqual/results/e460-2026-07-25/README.md).
 * test_pm_e460_field_values() in the host runner locks that reading in as a
 * golden case.
 *
 * The rule that keeps this file testable is design doc 03's: nothing here may
 * touch MMIO, port I/O, or DOS services. qprintf is the single dependency and
 * the host runner substitutes its own. The speed-table and extended-capability
 * printers at the end (roadmap task 35.1) keep the rule: they print what
 * xhci_read_caps() stored and take their printer as an argument.
 */

#include <stdio.h>
#include "qual.h"

/* A4: contents of the PCI Power Management capability. Read-only - the state
 * is reported, never changed. Win98 barely exercises power management, but
 * Win2000 SP4 issues real D-state transitions and acts on PME, so these are
 * the facts its Phase 8 stability work needs (docs/usb-xhci-info/xhci-programming.md). */
void report_pci_pm(const PCIINFO *p)
{
    static const char *dname[4] = { "D0", "D1", "D2", "D3hot" };
    /* PMC[8:6] Aux_Current encoding. Transcribed from the specification's
     * field table; lspci decodes the same eight values, so the bare-metal
     * cross-check covers this table as well as the bit positions. */
    static const char *aux[8] = { "0mA", "55mA", "100mA", "160mA",
                                  "220mA", "270mA", "320mA", "375mA" };
    u16 pme;
    int first;
    int i;

    if (!p->has_pm) {
        qprintf("  PCI PM: capability absent (no D-state or PME support)\n");
        return;
    }

    /* Version is PMC[2:0], reported as the raw field exactly as lspci does
     * ("Power Management version 2"). Deliberately not translated to a spec
     * revision string: that mapping is not in docs/references/, and printing
     * the raw number keeps the bare-metal lspci cross-check a literal
     * comparison instead of one mediated by a table nothing here can check. */
    qprintf("  PCI PM: v%u  state=%s  D1=%d D2=%d  PME_En=%d PME_Status=%d\n",
            (unsigned)(p->pmc & 0x0007),
            dname[p->pm_state],
            (p->pmc & 0x0200) ? 1 : 0,
            (p->pmc & 0x0400) ? 1 : 0,
            (p->pmcsr & 0x0100) ? 1 : 0,
            (p->pmcsr & 0x8000) ? 1 : 0);

    pme = (u16)(p->pmc >> 11);
    qprintf("    PME_Support:");
    if (pme == 0) {
        qprintf(" none");
    } else {
        first = 1;
        for (i = 0; i < 5; i++) {
            if ((pme & (1 << i)) == 0)
                continue;
            qprintf("%s %s", first ? "" : ",",
                    (i < 4) ? dname[i] : "D3cold");
            first = 0;
        }
    }
    qprintf("\n");

    /* PMCSR bit 3, No_Soft_Reset. Set means the device does NOT reset itself
     * on a D3hot -> D0 transition and keeps its internal state; clear means it
     * does, and whatever brings it back must reinitialise it. Reserved before
     * PM version 2, which is why the version is printed above rather than
     * hidden - read the two together. A Win2000 resume-path input, not a
     * qualification input: nothing here changes any verdict. */
    if (p->pmcsr & 0x0008)
        qprintf("    NoSoftRst=1 - keeps state across D3hot->D0\n");
    else
        qprintf("    NoSoftRst=0 - resets on D3hot->D0; resume must "
                "reinitialise\n");

    /* DSI (PMC bit 5) is the other directly actionable bit: set means the
     * device needs device-specific initialisation after it reaches D0, i.e.
     * restoring config space is not enough. PME_Clock (bit 3) says PME#
     * generation depends on the bus clock - always 0 on the PCIe-era silicon
     * this project targets, so a 1 is worth seeing precisely because it would
     * be unexpected. Aux_Current (bits 8:6) is the D3cold auxiliary supply the
     * device needs to signal PME, and is meaningful only when PME_Support
     * includes D3cold; it is a platform power-budget fact rather than a driver
     * one, reported to complete the lspci comparison. */
    qprintf("    flags: DSI=%d PMEClk=%d  Aux=%s\n",
            (p->pmc & 0x0020) ? 1 : 0,
            (p->pmc & 0x0008) ? 1 : 0,
            aux[(p->pmc >> 6) & 0x7]);

    if (p->pmc & 0x0020)
        qprintf("    NOTE: DSI set - device-specific init is required after "
                "reaching D0.\n");

    /* Raw words last. Every field above is a decode of these two, so printing
     * them makes any future lspci cross-check exact instead of a comparison
     * between two interpretations - and lets a disagreement be re-decoded from
     * the log without another trip to the machine. The PCI Bus PM
     * specification is not mirrored in docs/references/, which is exactly why
     * the evidence should survive the tool's own decoder. */
    qprintf("    raw: PMC=%04X PMCSR=%04X\n",
            (unsigned)p->pmc, (unsigned)p->pmcsr);

    if (p->pm_state != 0)
        qprintf("    NOTE: not in D0 - the driver must transition it to D0 "
                "before use.\n");
}

/* A2: the PCI Status register's error bits (0x06). These are the bus's own
 * record of something having gone wrong - a master abort means a transaction
 * went unclaimed, a target abort that the target rejected one, and the parity
 * and SERR bits speak for themselves.
 *
 * The reporting rule follows from the bits being *sticky* and RW1C: firmware,
 * a previous OS, or a prior run of this tool can have set them long before
 * now, so a bit that is already set at probe says something about the machine
 * and nothing about this run. Only a bit that turns on *across* the active
 * tests was caused by the tool's own traffic, and only that is attributed.
 * The distinction is the whole point - reporting "master abort" for a bit
 * firmware set at boot would be exactly the kind of stale-state-as-verdict
 * mistake the D-state and MSE classifiers above were fixed to avoid.
 *
 * Verdict-neutral by design. A new abort bit is real evidence and is printed
 * loudly, but it is not made a disqualifier on its own: this tool cannot tell
 * a controller that faulted the bus from one whose neighbour did while sharing
 * it. Pair it with the C3 outcome. */
void report_pci_status(const PCIINFO *p)
{
    static const struct { u16 mask; const char *name; } errbit[6] = {
        { 0x8000, "detected parity error" },
        { 0x4000, "signaled SERR" },
        { 0x2000, "received master abort" },
        { 0x1000, "received target abort" },
        { 0x0800, "signaled target abort" },
        { 0x0100, "master data parity error" }
    };
    u16 pre, post, fresh;
    int first;
    int i;

    pre = (u16)(p->status_orig & 0xF900);
    if (pre != 0) {
        qprintf("  PCI status: pre-existing errors -");
        first = 1;
        for (i = 0; i < 6; i++) {
            if ((pre & errbit[i].mask) == 0)
                continue;
            qprintf("%s %s", first ? "" : ",", errbit[i].name);
            first = 0;
        }
        qprintf("\n");
        qprintf("    (sticky bits, set before this run - not attributed to "
                "it)\n");
    }

    if (!p->status_rechecked)
        return;

    post = (u16)(p->status_final & 0xF900);
    fresh = (u16)(post & ~pre);
    if (fresh == 0)
        return;

    qprintf("  PCI status: NEW error(s) during the active tests -");
    first = 1;
    for (i = 0; i < 6; i++) {
        if ((fresh & errbit[i].mask) == 0)
            continue;
        qprintf("%s %s", first ? "" : ",", errbit[i].name);
        first = 0;
    }
    qprintf("\n");
    qprintf("    caused by this run's traffic; read with the C3 result. Bits "
            "left set (RW1C).\n");
}

/* Called where MMIO reads back as absent. The tool cannot always tell these
 * apart, but PMCSR and the Command register narrow it to one line instead of
 * leaving "NOT ACCESSIBLE" as a dead end.
 *
 * Order matters and must match the mapping code: xhci_map() and
 * legacy_map_and_read() both refuse a BAR above 4 GB or an unassigned BAR
 * before they touch MMIO at all, so those causes are proximate and come
 * first. Reporting a D-state for a window the tool never tried to map would
 * name a cause that was never reached. */
void report_mmio_dead(const PCIINFO *p)
{
    /* What the mapper actually refused on, when it recorded one. It is more
     * proximate than anything derivable from PCI state below, because it is
     * the reason the code took rather than a reason the reader reconstructs. */
    if (p->mmio_reason != 0)
        qprintf("    cause: %s\n", p->mmio_reason);
    else if (p->bar_hi != 0)
        qprintf("    cause: BAR0 is above 4 GB and cannot be mapped by this "
                "32-bit path\n");
    else if (p->bar_phys == 0)
        qprintf("    cause: BAR0 is unassigned - firmware allocated no MMIO "
                "window\n");
    else if ((p->bar_lo & 1) != 0)
        qprintf("    cause: BAR0 selects I/O space, and xHCI 5.2.1 requires a "
                "memory BAR - this is true whatever the power state and "
                "Memory Space Enable say\n");
    else if (p->has_pm && p->pm_state != 0)
        qprintf("    cause: device is in D%d, not D0 - it decodes no MMIO "
                "until powered up\n", p->pm_state);
    else if ((p->cmd_effective & PCI_CMD_MSE) == 0)
        qprintf("    cause: PCI Memory Space Enable is clear in the Command "
                "register\n");
    else
        qprintf("    cause: undetermined - MSE set, BAR assigned below 4 GB, "
                "device in D0\n");
}

/* Classify a dead MMIO window without turning a temporary power/configuration
 * state into a silicon verdict. Returns nonzero only for a hard controller or
 * platform disqualifier.
 *
 * cmd_effective, not cmd_orig: in an active run the tool has already tried to
 * set MSE, so the post-enable read-back is what says whether the bit stuck.
 * Testing the pre-enable snapshot would report "MSE is clear" for a
 * controller whose MSE was set successfully but whose window is dead for some
 * other reason. */
int report_mmio_unavailable(const PCIINFO *p, int active_requested)
{
    /* The verdict is quick_classify_mmio's, in every branch. Everything below
     * chooses the *wording*; nothing below decides. Task 11-V.8's "one verdict
     * logic, printed two ways" is this line.
     *
     * The MMIO half only - this function classifies a dead window and nothing
     * else. Folding the Interrupt Pin check in here would make it answer a
     * question it was not asked, which is exactly what the first draft did and
     * what test_mmiodiag.c's D-state vectors caught. */
    int quick = quick_classify_mmio(p, active_requested);

    if (p->bar_hi != 0) {
        qprintf("  DISQUALIFIED: BAR0 above 4 GB (the 32-bit driver cannot "
                "address it)\n");
    } else if (p->bar_phys == 0) {
        qprintf("  DISQUALIFIED: BAR0 is unassigned\n");
    } else if ((p->bar_lo & 1) != 0) {
        /* The third function that has to ask this before the D-state and MSE
         * branches, and the one the fix missed: `quick` above already answers
         * DISQUALIFIED for it, so without this the run returned 1 - a hard
         * disqualifier - while printing "No controller fault inferred" and
         * advice about powering the part up or enabling MSE. The verdict and
         * the words under it have to be one answer (Codex review round 5). */
        qprintf("  DISQUALIFIED: BAR0 selects I/O space, and xHCI 5.2.1 "
                "requires a memory BAR\n");
        qprintf("      True whatever the power state and Memory Space Enable "
                "say; powering it up will not change it.\n");
    } else if (p->has_pm && p->pm_state != 0) {
        qprintf("  NOT QUALIFIED: controller is in D%d, not D0; MMIO and "
                "active tests are unavailable\n", p->pm_state);
        qprintf("      No controller fault inferred; a target driver must "
                "transition it to D0 before use.\n");
    } else if ((p->cmd_effective & PCI_CMD_MSE) == 0) {
        if (active_requested) {
            qprintf("  DISQUALIFIED: PCI Memory Space Enable could not be "
                    "set\n");
        } else {
            qprintf("  NOT QUALIFIED: PCI Memory Space Enable was clear; "
                    "read-only probe left it unchanged\n");
            qprintf("      No controller fault inferred; use an active mode "
                    "to test whether MSE can be enabled.\n");
        }
    } else if (p->mmio_tool_limit) {
        /* The mapper refused for a reason that is about this tool. Saying
         * DISQUALIFIED here would name dead silicon for a controller that may
         * be sound, which is what report_mmio_dead's recorded reason exists to
         * prevent - and the two must agree. */
        qprintf("  NOT QUALIFIED: this tool could not map the controller's "
                "register block\n");
        qprintf("      No controller fault inferred; see the cause above.\n");
    } else {
        qprintf("  DISQUALIFIED: BAR0 MMIO not accessible with MSE set, BAR "
                "assigned below 4 GB, and device in D0\n");
    }

    return (quick == QUICK_DISQUALIFIED) ? 1 : 0;
}

/*
 * Task 11-V.8's classifier - the one place a read-only observation becomes a
 * verdict.
 *
 * The order is the same one report_mmio_dead documents and must stay that way:
 * the mapping code refuses a BAR above 4 GB or an unassigned BAR before it
 * touches MMIO at all, so those causes are proximate. Naming a D-state for a
 * window the tool never tried to map would name a cause that was never reached.
 *
 * `Interrupt Pin = 0` comes first and is unconditional. It is the one
 * disqualifier that needs no MMIO at all, and Phase 0's checkpoint makes it
 * absolute: neither target's USB stack has an MSI path, so a controller that
 * can only signal MSI delivers no interrupts to either of them however healthy
 * the rest of it is.
 */
int quick_classify_mmio(const PCIINFO *p, int active_requested)
{
    if (p->bar_hi != 0)
        return QUICK_DISQUALIFIED;
    if (p->bar_phys == 0)
        return QUICK_DISQUALIFIED;
    /*
     * **An I/O-space BAR0 is disqualifying whatever the power state or MSE
     * says, and it has to be asked before both of them.** xHCI 5.2.1 requires
     * a memory BAR, and the mapper refuses this one before it touches
     * anything - so it needs neither D0 nor MSE to be established. Asked after
     * them, a controller that is both in D3 and misdescribing its BAR type
     * came back CANNOT SAY on the D-state, and the definitive refusal - the
     * one that stays true when somebody powers it up - was never reported.
     * A PCI-state fact rather than a `mmio_reason` test, so it holds on a path
     * where the mapper never ran.
     */
    if ((p->bar_lo & 1) != 0)
        return QUICK_DISQUALIFIED;
    if (p->has_pm && p->pm_state != 0)
        return QUICK_CANNOT_SAY;
    if ((p->cmd_effective & PCI_CMD_MSE) == 0)
        return active_requested ? QUICK_DISQUALIFIED : QUICK_CANNOT_SAY;
    /* A recorded tool limit is the last thing asked, because everything above
     * it is a reading about the machine and this one is a reading about the
     * tool: the window decoded, the device is in D0 with MSE set, and what
     * failed is that this tool maps a fixed 64 KB. CANNOT SAY, not
     * DISQUALIFIED - a verdict about silicon needs evidence about silicon. */
    if (p->mmio_tool_limit)
        return QUICK_CANNOT_SAY;
    return QUICK_DISQUALIFIED;
}

int quick_classify(const PCIINFO *p, int mmio_ok, int usb2_ports,
                   int active_requested)
{
    if (p->ipin == 0)
        return QUICK_DISQUALIFIED;

    if (!mmio_ok)
        return quick_classify_mmio(p, active_requested);

    if (usb2_ports == 0)
        return QUICK_DISQUALIFIED;

    return QUICK_LOOKS_OK;
}

/* The wording for the outcome above, in one line. Deliberately a table of
 * literals rather than a formatted string: the quick scan's whole claim is that
 * it fits on one screen, and a reason that can grow with the machine is how
 * that claim stops being true. */
const char *quick_reason(const PCIINFO *p, int mmio_ok, int usb2_ports,
                         int active_requested)
{
    if (p->ipin == 0)
        return "Interrupt Pin = 0, MSI only - neither target has an MSI path";

    if (!mmio_ok) {
        if (p->bar_hi != 0)
            return "BAR0 is above 4 GB";
        if (p->bar_phys == 0)
            return "BAR0 is unassigned";
        /* Before the power and MSE questions, for the reason
         * quick_classify_mmio gives at the same position: this one is true
         * whatever those two say, and the verdict is DISQUALIFIED. */
        if ((p->bar_lo & 1) != 0)
            return "BAR0 selects I/O space, and xHCI requires a memory BAR";
        if (p->has_pm && p->pm_state != 0)
            return "not in D0 - a driver must power it up first";
        if ((p->cmd_effective & PCI_CMD_MSE) == 0)
            return active_requested ? "Memory Space Enable could not be set"
                                    : "Memory Space Enable is clear";
        /*
         * **Whatever the mapper recorded beats the fallback below**, on the
         * same terms report_mmio_dead takes it: it is the reason the code
         * actually stopped on, where the fallback is a reconstruction from PCI
         * state that by this point has ruled everything out. It matters for
         * both kinds. A tool limit printed as "dead" contradicts the CANNOT
         * SAY beside it. A controller misdescribing its own layout - a zero
         * RTSOFF or DBOFF, a CAPLENGTH of 0 - is correctly DISQUALIFIED, but
         * "BAR0 MMIO is dead" is the wrong reason for it: the window decoded
         * perfectly and the registers in it are wrong, which is what the
         * reader needs to be told apart.
         */
        if (p->mmio_reason != 0)
            return p->mmio_reason;
        return "BAR0 MMIO is dead with MSE set and the device in D0";
    }

    if (usb2_ports == 0)
        return "no USB2 protocol ports";

    (void)active_requested;
    return "";
}

/*
 * C7 verdict: given the four Intel mux words, say what the routing is - and
 * refuse to call it correct on evidence that cannot support that claim.
 * XUSB2PRM is the mask of *switchable* ports, so "every switchable port is
 * routed" is vacuously true when the mask reads 0, and an undecoded config
 * read yields 0 or all ones. Without the guards below, either would print
 * "all switchable USB2 ports already routed to xHCI" about a machine with
 * nothing routed at all - retiring the one question C7 exists to answer with
 * a false negative, on the single symptom (ports power, no connect ever
 * arrives) that this project has no other detector for.
 *
 * This lives here rather than in bringup.c because the QEMU matrix cannot
 * reach C7 at all: no emulated controller carries an Intel 7/8-series
 * VID/DID, so qual_intel_ports() returns before its first read. That makes
 * xhciqual/test/test_mmiodiag.c the only coverage these branches get before
 * metal - the same reason the rest of this file was split out.
 */
int report_xusb2pr(const PCIINFO *p)
{
    qprintf("  C7: XUSB2PR=%08lX XUSB2PRM=%08lX USB3_PSSEN=%08lX "
            "USB3PRM=%08lX\n",
            p->xusb2pr, p->xusb2prm, p->usb3_pssen, p->usb3prm);

    if (p->xusb2prm == 0 || p->xusb2prm == 0xFFFFFFFFUL) {
        qprintf("  C7: XUSB2PRM=%08lX is not usable evidence - either this "
                "PCH exposes no switchable USB2 ports, or the config read did "
                "not decode.\n"
                "      Routing UNDETERMINED, which is not the same as "
                "correct. Record both words.\n", p->xusb2prm);
        return XUSB2PR_UNDETERMINED;
    }
    if (p->xusb2pr == 0xFFFFFFFFUL) {
        qprintf("  C7: XUSB2PR reads all ones against a usable mask, which is "
                "not usable evidence.\n"
                "      Candidate causes, not distinguished here: config space "
                "not decoding, or a function that stopped responding.\n"
                "      Routing UNDETERMINED, which is not the same as "
                "correct.\n");
        return XUSB2PR_UNDETERMINED;
    }
    if ((p->xusb2pr & p->xusb2prm) == p->xusb2prm) {
        qprintf("  C7: all switchable USB2 ports already routed to xHCI\n");
        return XUSB2PR_ROUTED;
    }
    return XUSB2PR_NOT_ROUTED;
}

/*
 * Roadmap task 35.1, first step: read the speed tables. Everything below
 * prints what xhci_read_caps() stored during its one read-only walk and reads
 * no register, so it is pure in the sense of this file and the host runner
 * covers it with the E460's real PSI words (issue 11).
 */

/* PSI dword fields, xHCI 1.2c 7.2.2.1.2 */
#define PSI_PSIV(d)  ((d) & 0xFUL)
#define PSI_PSIE(d)  (((d) >> 4) & 0x3UL)
#define PSI_PLT(d)   (((d) >> 6) & 0x3UL)
#define PSI_PFD(d)   (((d) >> 8) & 0x1UL)
#define PSI_LP(d)    (((d) >> 14) & 0x3UL)
#define PSI_PSIM(d)  (((d) >> 16) & 0xFFFFUL)

static const char *psi_unit(u32 d)
{
    static const char *const units[4] = { "b/s", "Kb/s", "Mb/s", "Gb/s" };

    return units[PSI_PSIE(d)];
}

static const char *psi_plt(u32 d)
{
    static const char *const plt[4] = {
        "symmetric", "reserved", "asym RX", "asym TX"
    };

    return plt[PSI_PLT(d)];
}

/* LP is defined for USB 3.x only; on USB 2 the field is reserved (0). */
static const char *psi_lp(const PROTOCAP *pr, u32 d)
{
    if (pr->major < 3)
        return "";
    switch (PSI_LP(d)) {
    case 0:  return " (SuperSpeed)";
    case 1:  return " (SuperSpeedPlus)";
    default: return " (reserved)";
    }
}

/*
 * Issue 11: a complete USB 3 table that lists entries but not PSIV 4. That is
 * not wrong in itself - a valid table may advertise 5 Gb/s under another ID,
 * and PORTSC then reports that ID, which decodes fine. It matters only if
 * PORTSC still reports the default ID 4 for a 5 Gb/s device, as Sunrise
 * Point-LP does: a reader that treats a non-empty table as replacing the
 * default IDs - this tool's C8 and the driver up to 2.1.1.0 - cannot name it.
 * Without a device the tool cannot see PORTSC, so the warning says "if". Only
 * a table read in full (npsi == psic) can be said to omit an ID.
 * Informational: it feeds no verdict and no exit code.
 */
int psi_usb3_lacks_ss(const PROTOCAP *pr)
{
    int k;

    if (pr->major != 3 || pr->psic == 0 || pr->npsi != pr->psic)
        return 0;
    for (k = 0; k < (int)pr->npsi; k++) {
        if (PSI_PSIV(pr->psi[k]) == 4)
            return 0;
    }
    return 1;
}

void report_protocols(const CTRL *c, QPRINTF_FN pf)
{
    int i, k;

    for (i = 0; i < c->nproto; i++) {
        const PROTOCAP *pr = &c->proto[i];

        if (pr->portcnt == 0)   /* seen on qemu-xhci with p3=0 */
            pf("  Protocol USB %X.%X: no ports, slot type %d, PSIC %d\n",
               pr->major, pr->minor, pr->slottype, pr->psic);
        else
            pf("  Protocol USB %X.%X: ports %d-%d, slot type %d, PSIC %d\n",
               pr->major, pr->minor, pr->portoff,
               pr->portoff + pr->portcnt - 1, pr->slottype, pr->psic);
        for (k = 0; k < (int)pr->npsi; k++) {
            u32 d = pr->psi[k];

            pf("    PSI %08lX  PSIV %2lu  %5lu %-4s  %-9s  PFD %lu  LP %lu%s\n",
               d, PSI_PSIV(d), PSI_PSIM(d), psi_unit(d), psi_plt(d),
               PSI_PFD(d), PSI_LP(d), psi_lp(pr, d));
        }
        if ((int)pr->npsi < (int)pr->psic)
            pf("    (%d of PSIC %d entries read: the rest lie past the "
               "mapped window)\n", pr->npsi, pr->psic);
        if (psi_usb3_lacks_ss(pr))
            pf("  WARNING: USB 3 PSI table does not list PSIV 4 (default "
               "SuperSpeed). If\n"
               "    PORTSC reports ID 4 for a 5 Gb/s device, as Sunrise Point "
               "does, a driver\n"
               "    that trusts the table strictly cannot decode it "
               "(issue 11)\n");
    }
}

static const char *xcap_name(u32 id)
{
    switch (id) {
    case 1:  return "USB Legacy Support";
    case 2:  return "Supported Protocol";
    case 3:  return "Extended Power Mgmt";
    case 4:  return "I/O Virtualization";
    case 5:  return "Message Interrupt";
    case 6:  return "Local Memory";
    case 10: return "USB Debug Capability";
    case 17: return "Extended Message Interrupt";
    default: return (id >= 192) ? "vendor defined" : "reserved";
    }
}

/*
 * The raw chain, four dwords to a row with each row's BAR0 offset, so a log
 * line can be pasted into a host replay vector word for word. xhcicap.c's
 * xcap_record() decides the bounds; this prints them and says why the record
 * ends where it does.
 */
void report_xcap_dump(const CTRL *c, QPRINTF_FN pf)
{
    char line[80];
    int i, j;

    if (c->xecp_off == 0) {
        pf("  xECP: 0 - no extended capabilities\n");
        return;
    }
    pf("  Extended capabilities, raw dwords (xECP %04lX):\n", c->xecp_off);
    for (i = 0; i < c->nxcap; i++) {
        const XCAPREC *r = &c->xcap[i];

        if (r->ndw < r->want)
            pf("    cap %04lX ID %3u next %3u  %s, %u of %u dwords\n",
               r->off, r->id, r->next, xcap_name(r->id), r->ndw, r->want);
        else
            pf("    cap %04lX ID %3u next %3u  %s, %u dwords\n",
               r->off, r->id, r->next, xcap_name(r->id), r->ndw);
        for (j = 0; j < (int)r->ndw; j += 4) {
            int n, m;

            n = sprintf(line, "      %04lX:", r->off + (u32)j * 4);
            for (m = j; m < j + 4 && m < (int)r->ndw; m++)
                n += sprintf(line + n, " %08lX", c->xdump[r->first + m]);
            pf("%s\n", line);
        }
    }
    switch (c->xcap_stop) {
    case XCAP_STOP_ONES:
        pf("    (a header read all ones: raw recording stops here)\n");
        break;
    case XCAP_STOP_FULL:
        pf("    (dump bound reached, %d capabilities / %d dwords: the rest "
           "not recorded)\n", XCAP_DUMP_CAPS, XCAP_DUMP_TOTAL);
        break;
    case XCAP_STOP_WALK:
        pf("    (the walk stopped at the mapped window or its 64-step "
           "guard)\n");
        break;
    default:
        break;
    }
}

/*
 * Record one extended capability for the raw dump (roadmap task 35.1, the
 * README's "Raw extended-capability dump"). Read-only: it reads dwords inside
 * the capability's own span and inside the mapped window, and a register read
 * has no side effect here (the RW1C/RW1S bits of these registers act on writes,
 * spec 5.1). The span is the distance to the next capability; the last one,
 * whose span the chain does not give, takes its known size, 4 dwords if
 * unknown. A Supported Protocol capability always includes its PSI dwords.
 */
static void xcap_record(CTRL *c, u32 off, u32 dw, u32 window,
                        XCAP_RD_FN rd, void *ctx)
{
    XCAPREC *r;
    u32 want, known, k;

    if (c->nxcap >= XCAP_DUMP_CAPS || c->nxdump >= XCAP_DUMP_TOTAL) {
        c->xcap_stop = XCAP_STOP_FULL;
        return;
    }
    r = &c->xcap[c->nxcap++];
    r->off = off;
    r->id = (u8)(dw & 0xFF);
    r->next = (u8)((dw >> 8) & 0xFF);
    r->first = (u16)c->nxdump;
    if (dw == 0xFFFFFFFFUL) {
        /* A dead or vanished function. Recording stops here; the walk below
         * is not changed by it and still follows the FF next field. */
        c->xdump[c->nxdump++] = dw;
        r->ndw = 1;
        r->want = 1;
        c->xcap_stop = XCAP_STOP_ONES;
        return;
    }
    switch (r->id) {
    case XECP_ID_LEGSUP: known = 2; break;     /* USBLEGSUP, USBLEGCTLSTS */
    case XECP_ID_PROTO:
        known = (off + 0x0CUL < window) ?
                4 + ((rd(ctx, off + 0x08) >> 28) & 0xF) : 1;
        break;
    case 3:  known = 2;  break;                 /* Extended Power Management */
    case 10: known = 16; break;                 /* Debug Capability, 00h-3Ch */
    default: known = 4;  break;
    }
    want = known;
    if (r->next != 0) {
        want = r->next;
        if (r->id == XECP_ID_PROTO && known > want)
            want = known;
    }
    r->want = (u16)want;
    for (k = 0; k < want && k < XCAP_DUMP_PER_CAP &&
                c->nxdump < XCAP_DUMP_TOTAL &&
                off + k * 4 + 4 <= window; k++)
        c->xdump[c->nxdump++] = (k == 0) ? dw : rd(ctx, off + k * 4);
    r->ndw = (u16)k;
}

void xcap_walk(CTRL *c, u32 xecp_off, u32 window, XCAP_RD_FN rd, void *ctx)
{
    u32 off, dw;
    int guard;

    c->legsup_off = 0;
    c->nproto = 0;
    off = xecp_off;
    c->xecp_off = off;
    c->nxcap = 0;
    c->nxdump = 0;
    c->xcap_stop = XCAP_STOP_END;
    for (guard = 0; off != 0 && off < window - 0x40UL && guard < 64;
         guard++) {
        u32 next;

        dw = rd(ctx, off);
        if (c->xcap_stop == XCAP_STOP_END)
            xcap_record(c, off, dw, window, rd, ctx);
        switch (dw & 0xFF) {
        case XECP_ID_LEGSUP:
            if (c->legsup_off == 0)
                c->legsup_off = off;
            break;
        case XECP_ID_PROTO:
            if (c->nproto < MAX_PROTO) {
                PROTOCAP *pr = &c->proto[c->nproto];
                u32 dw2 = rd(ctx, off + 0x08);
                int k;

                pr->major   = (u8)(dw >> 24);
                pr->minor   = (u8)(dw >> 16);
                pr->portoff = (u8)(dw2 & 0xFF);
                pr->portcnt = (u8)((dw2 >> 8) & 0xFF);
                pr->psic    = (u8)((dw2 >> 28) & 0xF);
                pr->slottype = (u8)(rd(ctx, off + 0x0C) & 0x1F);
                /* PSIC > 0 means this cap redefines the speed IDs; the
                 * default 1=FS/2=LS/3=HS/4=SS mapping does not apply
                 * unless the table says so (spec 7.2, 7.2.2.1.2). */
                pr->npsi = 0;
                for (k = 0; k < (int)pr->psic && k < MAX_PSI; k++) {
                    if (off + 0x10UL + (u32)k * 4 >= window)
                        break;
                    pr->psi[k] = rd(ctx, off + 0x10 + k * 4);
                    pr->npsi++;
                }
                c->nproto++;
            }
            break;
        case 10:
            c->saw_debug_cap = 1;
            break;
        default:
            break;
        }
        next = (dw >> 8) & 0xFF;
        if (next == 0)
            break;
        off += next << 2;
    }
    /* A walk that ran off the mapped window (or past the 64-entry guard)
     * may have missed USBLEGSUP, and "no USBLEGSUP" then passes C1 with no
     * handoff performed. Say so rather than end silently. */
    if (off >= window - 0x40UL || guard >= 64) {
        if (c->xcap_stop == XCAP_STOP_END)
            c->xcap_stop = XCAP_STOP_WALK;
        qprintf("  NOTE: extended capability list not walked to its end "
                "(next at %08lX); a USBLEGSUP beyond it was not seen\n",
                (unsigned long)off);
    }
}
