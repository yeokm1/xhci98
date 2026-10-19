/*
 * verdict.c - the xHCI verdict and the decisions it rests on, kept free of
 * DOS services so the host runner drives them (test/test_verdict.c): C4's
 * outcome from what the interrupt test saw, the gate that lets C6 and C8
 * run, the per-controller verdict with its wording, and the run's exit
 * code. report.c keeps the output plumbing; this file prints through
 * qprintf only.
 *
 * Task 36.10 (design record 15 section 10, and the owner's addition of
 * 2026-10-09): since the driver can poll, a controller with no working
 * interrupt is no longer refused. A missing line (Interrupt Pin 0, no
 * usable IRQ, PCI Interrupt Disable stuck) or a line that never delivered
 * (the No-Op's completion in the event ring, no interrupt) reads "needs
 * polling" - C4 POLL - when the controller completes the command polled,
 * and such a controller, passing the rest, is QUALIFIED WITH CAVEATS, with
 * its own exit code. The tool's own failures stay failures, and so does a
 * controller that does not complete the command even polled.
 */

#include <stdio.h>
#include <string.h>
#include "qual.h"

const char *verdict_name(int v)
{
    switch (v) {
    case V_PASS: return "PASS";
    case V_WARN: return "WARN";
    case V_FAIL: return "FAIL";
    case V_POLL: return "POLL";
    default:     return "SKIP";
    }
}

/*
 * C4's outcome. Only "the event reached the ring and no interrupt
 * arrived" - or no line to arrive on - reads as needing polling, and only
 * with the completion in hand: without it the controller failed the
 * command whatever the line did. A DPMI step that failed is the tool's,
 * never the controller's polling verdict, and an ISR entry that the
 * controller's status did not own, or one with no completion behind it,
 * is a fault of its own.
 */
int c4_verdict(const C4_SEEN *s)
{
    if (s->tool_failed)
        return V_FAIL;
    if (s->no_line)
        return s->event_seen ? V_POLL : V_FAIL;
    if (!s->ip_seen)
        return s->event_seen ? V_POLL : V_FAIL;
    if (s->foreign)
        return V_FAIL;
    if (s->isr_fired)
        return s->event_seen ? V_PASS : V_FAIL;
    return s->event_seen ? V_POLL : V_FAIL;
}

/* C6 and C8 run after a C4 that passed, one that found the controller
 * needs polling (the hook is gone by then: xhci_irq_source_off ran before
 * qual_irq returned), or a poll-only run. */
int c4_lets_ports(int v_irq, int poll_only)
{
    return (poll_only || v_irq == V_PASS || v_irq == V_POLL) ? 1 : 0;
}

/*
 * The message route the owner asked to be named (2026-10-09): Windows
 * Vista and 7 can take a message interrupt where the controller offers
 * one - the x64 binary directly, the 32-bit one through the xhcihlpr.sys
 * helper (roadmap 36.15, design record 18). The capability list is read
 * by pci_read_static (05h MSI, 11h MSI-X); nothing is enabled.
 */
const char *msi_route_text(const PCIINFO *p, int broken_msi)
{
    if ((p->has_msi || p->has_msix) && broken_msi)
        return "Windows Vista and 7\n"
               "        could, but Linux marks this controller's MSI broken\n"
               "        (XHCI_BROKEN_MSI), so polling is its route";
    if (p->has_msi && p->has_msix)
        return "Windows Vista and 7 can\n"
               "        take a message interrupt, since it offers MSI and "
               "MSI-X (x64\n"
               "        directly; 32-bit with the xhcihlpr.sys helper)";
    if (p->has_msix)
        return "Windows Vista and 7 can\n"
               "        take a message interrupt, since it offers MSI-X (x64\n"
               "        directly; 32-bit with the xhcihlpr.sys helper)";
    if (p->has_msi)
        return "Windows Vista and 7 can\n"
               "        take a message interrupt, since it offers MSI (x64\n"
               "        directly; 32-bit with the xhcihlpr.sys helper)";
    return "Windows Vista and 7\n"
           "        could, but it offers neither MSI nor MSI-X, so polling "
           "is its\n"
           "        only route";
}

/*
 * The caveats, said in full wherever the outcome is reached. Which polling
 * mode serves depends on what C4 saw: the driver's
 * automatic mode polls a controller with no pin or no interrupt resource,
 * and moves a line to polling when it sees IMAN.IP asserted and nothing
 * claimed (record 15 section 4.2) - so a controller whose IP was never seen
 * is told XhciInterruptMode 2. Whether a message interrupt would serve it
 * is not shown by the run: the route is named from the capability list as
 * for every other case, said as untested. An MSI or MSI-X found enabled
 * clears IP itself after each message (xHCI 1.2c 4.17.5), which is a
 * reason IP may not have been seen, and is said too.
 */
void report_caveats(const CTRL *c)
{
    const PCIINFO *p = &c->pci;
    int broken;

    broken = (c->quirk != 0 && (c->quirk->flags & QF_BROKEN_MSI) != 0);
    if (c->irq_force_poll) {
        qprintf("      CAVEAT: no working interrupt - the controller's "
                "pending bit (IMAN.IP)\n"
                "        was never seen, and xhci98.sys's automatic mode "
                "moves to polling only\n"
                "        on an IP it sees. Set XhciInterruptMode to 2 so it "
                "polls: more CPU\n"
                "        wake-ups, and an event can wait up to the poll "
                "period (XhciPollIdleMs,\n"
                "        50 ms by default, when idle). Or an MSI-capable OS, "
                "untested by this\n"
                "        run: %s.\n", msi_route_text(p, broken));
        if (p->msi_enabled || p->msix_enabled)
            qprintf("        %s was enabled as found, and an enabled message "
                    "clears IP itself,\n"
                    "        so this run may not have seen it.\n",
                    p->msix_enabled ? "MSI-X" : "MSI");
        return;
    }
    qprintf("      CAVEAT: no working line interrupt, so xhci98.sys polls "
            "this controller\n"
            "        (XhciInterruptMode 0, automatic, or 2): more CPU "
            "wake-ups, and an\n"
            "        event can wait up to the poll period (XhciPollIdleMs, "
            "50 ms by\n"
            "        default, when idle). Or an MSI-capable OS: %s.\n",
            msi_route_text(p, broken));
}

int final_verdict(CTRL *c, int active_requested)
{
    int qualified = 1;
    int warned = 0;
    int disqualified = 0;
    int tool_limited = 0;
    int caveats = 0;
    int st_ok = 1;
    int quick;

    qprintf("\n---- Verdict for %04X:%04X ----\n", c->pci.vid, c->pci.did);

    /*
     * **The read-only half of this verdict is quick_classify's, not this
     * function's** - task 11-V.8's "one verdict logic, printed two ways". Every
     * condition below chooses *wording*; the decision that a controller is
     * disqualified before any active test has run is made once, in
     * xhciqual/mmiodiag.c, and both this function and the quick scan read it
     * from there. A quick scan that said QUALIFIED where a full run did not
     * would be worse than no quick scan at all, and two copies of these
     * conditions is exactly how that happens.
     */
    quick = quick_classify(&c->pci, c->mmio_ok, c->usb2_ports,
                           active_requested);
    if (quick == QUICK_DISQUALIFIED) {
        qualified = 0;
        disqualified = 1;
    } else if (quick == QUICK_CANNOT_SAY) {
        qualified = 0;
        tool_limited = 1;
    }

    /* Task 36.10: Interrupt Pin 0 no longer disqualifies; it needs
     * polling, whatever else the run finds. */
    if (c->pci.ipin == 0) {
        qprintf("  NEEDS POLLING: Interrupt Pin = 0 - no line interrupt "
                "(INTx)\n");
        caveats = 1;
    }
    if (!c->mmio_ok) {
        (void)report_mmio_unavailable(&c->pci, active_requested);
        if (!active_requested)
            qprintf("  Probe-only run: active tests (C1-C7) NOT run.\n");
        qprintf("  ==> NOT QUALIFIED for cross-target Win98/Win2000 use\n");
        if (tool_limited && !disqualified)
            qprintf("      No controller fault was inferred from the "
                    "temporary state above.\n");
        return VERDICT_NOT;
    }
    if (c->usb2_ports == 0) {
        qprintf("  DISQUALIFIED: no USB2 protocol ports (B7 empty)\n");
    }

    if (!active_requested) {
        qprintf("  Probe-only run: %s so far. Active tests (C1-C7) NOT "
                "run - no qualification verdict.\n",
                qualified ? "no disqualifiers" : "disqualified");
        if (qualified && caveats)
            report_caveats(c);
        return VERDICT_NOT;
    }

    if (c->irq_selftest) {
        if (c->v_handoff == V_WARN)
            warned = 1;
        if (c->v_reset != V_PASS) {
            qprintf("  IRQ SELF-TEST FAILURE: halt/reset (C2) failed\n");
            st_ok = 0;
        } else if (c->v_dma == V_WARN) {
            /* The full run treats a No Op that completed with a non-success
             * code as warned-only; the self-test used to turn the same
             * observation into a failure (roadmap Phase 20, smaller items). */
            qprintf("  IRQ SELF-TEST WARNING: DMA round-trip (C3) was %s - "
                    "%s\n", verdict_name(c->v_dma), c->dma_note);
            warned = 1;
        } else if (c->v_dma != V_PASS) {
            qprintf("  IRQ SELF-TEST FAILURE: DMA round-trip (C3) was %s - "
                    "%s\n", verdict_name(c->v_dma), c->dma_note);
            st_ok = 0;
        }
        if (c->v_reset == V_PASS &&
            (c->v_dma == V_PASS || c->v_dma == V_WARN)) {
            if (c->v_irq == V_POLL) {
                qprintf("  IRQ SELF-TEST: NEEDS POLLING - C4: %s\n",
                        c->irq_note);
                caveats = 1;
            } else if (c->v_irq != V_PASS) {
                qprintf("  IRQ SELF-TEST FAILURE: C4: %s\n", c->irq_note);
                st_ok = 0;
            }
        }
        if (c->cleanup_failed) {
            qprintf("  IRQ SELF-TEST FAILURE: cleanup: %s\n",
                    c->cleanup_note);
            st_ok = 0;
        }
        if (!st_ok)
            qualified = 0;
        if (qualified && caveats) {
            qprintf("  ==> IRQ SELF-TEST: NO WORKING INTERRUPT%s - the "
                    "controller completed the\n"
                    "      command polled, so it needs polling; teardown "
                    "completed.\n",
                    warned ? ", AND A HANDOFF WARNING" : "");
            report_caveats(c);
        } else if (qualified) {
            qprintf("  ==> IRQ SELF-TEST PASS%s - locked one-shot ISR and "
                    "teardown completed.\n",
                    warned ? " WITH HANDOFF WARNING" : "");
        } else if (c->cleanup_failed) {
            qprintf("  ==> IRQ SELF-TEST FAILED - cleanup could not safely "
                    "quiesce the controller.\n");
        } else if (st_ok) {
            /* Every self-test gate passed: what withholds the verdict is
             * the read-only classification above, and FAILED would blame
             * the interrupt path for it. */
            qprintf("  ==> NOT QUALIFIED - every IRQ self-test gate passed, "
                    "but\n"
                    "      %s above withholds the verdict.\n",
                    disqualified ? "a static disqualifier" : "a tool limit");
        } else {
            qprintf("  ==> IRQ SELF-TEST FAILED - controller is not "
                    "disqualified by a tool/DPMI failure.\n");
        }
        qprintf("      C6/C8 and the full qualification verdict were not "
                "run.\n");
        if (!qualified)
            return VERDICT_NOT;
        return caveats ? VERDICT_CAVEATS : VERDICT_QUALIFIED;
    }

    if (c->poll_only) {
        /* Poll-only exercised C1/C2/C3/C6 with no ISR. Separate a genuine
         * controller failure (C2/C3) from the fact that the tool chose not
         * to test C4 - the latter is a provisional non-verdict, not a
         * disqualification. */
        qprintf("  Poll-only run: no interrupt handler was installed.\n");
        if (c->v_reset != V_PASS) {
            qprintf("  CONTROLLER FAILURE: halt/reset (C2) failed\n");
            qualified = 0;
            disqualified = 1;
        } else {
            if (c->v_dma == V_FAIL) {
                qprintf("  CONTROLLER FAILURE: DMA round-trip (C3) never "
                        "completed - bus-mastering broken\n");
                qualified = 0;
                disqualified = 1;
            } else if (c->v_dma == V_SKIP) {
                qprintf("  TOOL LIMIT: C3 could not run - %s\n",
                        c->dma_note);
                qualified = 0;
                tool_limited = 1;
            }
        }
        if (c->cleanup_failed) {
            qprintf("  CONTROLLER FAILURE: cleanup: %s\n",
                    c->cleanup_note);
            qualified = 0;
            disqualified = 1;
        }
        if (!qualified) {
            if (disqualified)
                qprintf("  ==> NOT QUALIFIED - a controller/static "
                        "disqualifier above was hit (C4 was not tested)\n");
            else if (tool_limited)
                qprintf("  ==> NOT QUALIFIED - the tool could not complete "
                        "C3 (C4 was not tested)\n");
        } else {
            qprintf("  ==> PROVISIONAL - C2/C3%s completed with no ISR and "
                    "no DOS/32A fault.\n"
                    "      C4 interrupt delivery NOT tested; re-run without "
                    "--poll-only to earn a verdict.\n",
                    c->v_port == V_PASS ? "/C6 port reset" : "");
            if (caveats)
                report_caveats(c);
        }
        return VERDICT_NOT;
    }

    if (c->v_handoff == V_WARN)
        warned = 1;
    if (c->v_reset != V_PASS) {
        qprintf("  DISQUALIFIED: halt/reset (C2) failed\n");
        qualified = 0;
        disqualified = 1;
    } else {
        if (c->v_dma != V_PASS) {
            if (c->v_dma == V_WARN) {
                warned = 1;
            } else if (c->v_dma == V_SKIP) {
                /* The tool could not run C3 (memory, PAGESIZE) - that is not
                 * evidence against the controller, but it is not a pass. */
                qprintf("  NOT QUALIFIED: C3 could not run - %s\n",
                        c->dma_note);
                qualified = 0;
                tool_limited = 1;
            } else {
                qprintf("  DISQUALIFIED: DMA round-trip (C3) never "
                        "completed - bus-mastering broken\n");
                qualified = 0;
                disqualified = 1;
            }
        }
        if ((c->v_dma == V_PASS || c->v_dma == V_WARN) &&
            c->v_irq == V_POLL) {
            /* Task 36.10: no working interrupt, the command completed
             * polled. Not a fault; a caveat on the verdict. The pin-0 line
             * above already said why for that case. */
            if (c->pci.ipin != 0)
                qprintf("  NEEDS POLLING: C4: %s\n", c->irq_note);
            caveats = 1;
        } else if ((c->v_dma == V_PASS || c->v_dma == V_WARN) &&
                   c->v_irq != V_PASS) {
            if (c->v_irq == V_FAIL && c->irq_no_event) {
                /* Not an interrupt question: the command never completed,
                 * so polling would not serve it either (36.10). */
                qprintf("  DISQUALIFIED: C4's No-Op never completed, even "
                        "polled: %s\n", c->irq_note);
                disqualified = 1;
            } else if (c->v_irq == V_FAIL) {
                qprintf("  DISQUALIFIED: no interrupt on legacy IRQ (C4): "
                        "%s\n", c->irq_note);
                qprintf("      Win98/PIC-HAL routing failed; Win2000 "
                        "APIC-HAL routing remains inconclusive.\n");
                disqualified = 1;
            } else {
                qprintf("  NOT QUALIFIED: C4 interrupt test was not run\n");
                tool_limited = 1;
            }
            qualified = 0;
        }
    }
    if (c->cleanup_failed) {
        qprintf("  DISQUALIFIED: cleanup could not quiesce the controller: "
                "%s\n", c->cleanup_note);
        qualified = 0;
        disqualified = 1;
    }
    /* Any C6 outcome short of PASS is a warning: SKIP means connect/reset
     * was never exercised, FAIL means a connected port refused to reset.
     * Neither is disqualifying, but neither may print a clean QUALIFIED. */
    if (c->v_port != V_PASS)
        warned = 1;

    if (qualified && caveats) {
        /* The owner's verdict (2026-10-09): not plain QUALIFIED, not the
         * warnings' wording, not a failure, not provisional. Warnings, when
         * there are any, are said beside it. */
        qprintf("  ==> CONTROLLER QUALIFIED WITH CAVEATS for cross-target "
                "driver development\n");
        report_caveats(c);
        if (warned)
            qprintf("      Also with warnings - review WARN/SKIP lines "
                    "above.\n");
        return VERDICT_CAVEATS;
    }
    if (qualified) {
        /* No APIC-HAL caveat here on purpose: it said the same thing on every
         * machine this tool has ever qualified, which made it the one line in
         * a per-machine report that carried no per-machine information. It
         * lives in XHCIQUAL\readme.txt, xhciqual\README.md and
         * xhciqual\hardware-testing.md instead. The FAIL branch's
         * "remains inconclusive" sibling is conditional and stays. */
        qprintf("  ==> CONTROLLER %s for cross-target driver development%s\n",
                warned ? "QUALIFIED (with warnings)" : "QUALIFIED",
                warned ? " - review WARN/SKIP lines above" : "");
        return VERDICT_QUALIFIED;
    }
    qprintf("  ==> NOT QUALIFIED for cross-target Win98/Win2000 use\n");
    if (tool_limited && !disqualified)
        qprintf("      No controller fault was inferred from the tool "
                "limit above.\n");
    return VERDICT_NOT;
}

/*
 * The run's exit code from its controllers' verdicts: 1 if any controller
 * was not qualified (a probe-only run included), else 3 if any was
 * qualified with caveats, else 0. 2 (usage, no controller) is main's.
 */
int run_exit_code(int any_not, int any_caveats)
{
    if (any_not)
        return 1;
    return any_caveats ? 3 : 0;
}
