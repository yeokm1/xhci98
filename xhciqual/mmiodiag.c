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
 * the host runner substitutes its own.
 */

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
    if (p->bar_hi != 0)
        qprintf("    cause: BAR0 is above 4 GB and cannot be mapped by this "
                "32-bit path\n");
    else if (p->bar_phys == 0)
        qprintf("    cause: BAR0 is unassigned - firmware allocated no MMIO "
                "window\n");
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
    if (p->has_pm && p->pm_state != 0)
        return QUICK_CANNOT_SAY;
    if ((p->cmd_effective & PCI_CMD_MSE) == 0)
        return active_requested ? QUICK_DISQUALIFIED : QUICK_CANNOT_SAY;
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
        if (p->has_pm && p->pm_state != 0)
            return "not in D0 - a driver must power it up first";
        if ((p->cmd_effective & PCI_CMD_MSE) == 0)
            return active_requested ? "Memory Space Enable could not be set"
                                    : "Memory Space Enable is clear";
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
