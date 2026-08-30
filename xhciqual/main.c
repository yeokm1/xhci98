/*
 * main.c - XHCIQUAL: Phase 0 DOS hardware qualification tool.
 *
 * Answers, per controller: "can a PIC-mode OS bring up this USB host
 * controller and receive its interrupts?"
 * See docs/contributing/design/01-hardware-qualification-tool.md.
 *
 * Exit codes: 0 = all controllers qualified, 1 = at least one not
 * qualified (or probe-only), 2 = no selected controller / usage error.
 */

#include <stdio.h>
#include <string.h>
#include "qual.h"

static PCIINFO pcis[MAX_CONTROLLERS];
static CTRL ctrls[MAX_CONTROLLERS];
static LEGACY_CTRL legacy_ctrls[MAX_CONTROLLERS];

/*
 * Task 11-V.8. Running XHCIQUAL with no arguments used to start the **most
 * invasive** mode it has - a FULL active bring-up of all three families, with
 * BIOS handoff, HCRST, DMA, port reset, a 15-second wait for a plug, and device
 * identification. That is the exact opposite of what hardware-testing.md's
 * safety box and the tool's own recommended order tell a first-time user to do,
 * so it was both the least useful answer to "is my machine supported" and the
 * riskiest way to ask. It is now a read-only quick scan.
 *
 * **The trigger is literally `argc == 1`, and that is deliberate.** Every batch
 * wrapper, the QEMU matrix and both bare-metal results directories pass
 * explicit arguments, so keying on "no arguments at all" rather than on "no
 * mode flag" leaves every one of them byte-identical in behaviour - including
 * the invocations that pass only a family selector, which still mean a full
 * active run on that family. `--full` is the spelling that reaches the old
 * default again.
 */
static int opt_quick       = 0;
static int opt_probe_only  = 0;
static int opt_poll_only   = 0;
static int opt_irq_selftest = 0;
static int opt_set_intel   = 0;
static int opt_serial      = 0;
static int opt_no_devid    = 0;
static int opt_no_page     = 0;
static int opt_help        = 0;
static const char *opt_done_flag = 0;
static int opt_wait_secs   = 15;
static int opt_family_mask = HC_MASK_ALL;
static int opt_family_seen = 0;
static const char *opt_log_name = 0;

static int is_family_word(const char *name)
{
    return strcmp(name, "xhci") == 0 || strcmp(name, "ehci") == 0 ||
           strcmp(name, "ohci") == 0 || strcmp(name, "all") == 0;
}

static int select_family(const char *name)
{
    int bit;

    if (strcmp(name, "all") == 0) {
        opt_family_mask = HC_MASK_ALL;
        opt_family_seen = 1;
        return 1;
    }
    if (strcmp(name, "xhci") == 0)
        bit = HC_MASK_XHCI;
    else if (strcmp(name, "ehci") == 0)
        bit = HC_MASK_EHCI;
    else if (strcmp(name, "ohci") == 0)
        bit = HC_MASK_OHCI;
    else
        return 0;
    if (!opt_family_seen)
        opt_family_mask = 0;
    opt_family_seen = 1;
    opt_family_mask |= bit;
    return 1;
}

/* parse_args stops at the first bad argument, so a --done-flag standing after
 * one would never be seen and a stale flag from an earlier run would survive a
 * run that never completed - exactly the false "clean verdict" the flag exists
 * to rule out. Pre-scan for it, using the same filename rules as parse_args so
 * the two always agree on which token is the flag name. */
static void prescan_done_flag(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        while (*a == '-' || *a == '/')
            a++;
        if (strcmp(a, "scan") == 0 || strcmp(a, "controller") == 0) {
            if (i + 1 < argc)
                i++;
        } else if (strcmp(a, "log") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-' &&
                argv[i + 1][0] != '/' && !is_family_word(argv[i + 1]))
                i++;
        } else if (strcmp(a, "done-flag") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-' &&
                argv[i + 1][0] != '/')
                opt_done_flag = argv[++i];
        } else if (strncmp(a, "done-flag=", 10) == 0 && a[10] != '\0') {
            opt_done_flag = a + 10;
        }
    }
}

static void reset_done_flag(void)
{
    if (opt_done_flag != 0)
        remove(opt_done_flag);
}

static int finish_normally(int status)
{
    FILE *f;

    if (opt_done_flag == 0)
        return status;
    f = fopen(opt_done_flag, "w");
    if (f == 0) {
        printf("warning: could not create completion flag %s\n",
               opt_done_flag);
        return status;
    }
    fclose(f);
    return status;
}

static void print_usage(void)
{
    printf("XHCIQUAL %s - USB host-controller qualification\n"
           "usage: XHCIQUAL [xhci|ehci|ohci|all] [options]\n"
           "  (no arguments)     read-only quick scan, one screen\n"
           "  --quick            ask for that quick scan explicitly\n"
           "  --full             the full active run across all families\n"
           "  --scan TYPE        scan xhci, ehci, ohci, or all; "
           "repeat to combine\n"
           "  --xhci/--ehci/--ohci  shorthand family selectors\n"
           "  --probe-only       read-only; Tier B only if MMIO is already "
           "enabled\n"
           "  --no-active        alias for --probe-only\n"
           "  --poll-only        active C1/C2/C3/C6 by polling; no ISR "
           "(C4 SKIP)\n"
           "  --irq-selftest     isolated xHCI C4 one-shot; no C6/C8\n"
           "  --set-intel-ports  xHCI C7: try USB2 routing; verify readback\n"
           "  --no-wait          do not wait for a device plug in C6\n"
           "  --no-devid         skip xHCI C8 device identification\n"
           "  --no-page          do not pause the screen every page\n"
           "  --serial           mirror output to COM1 115200 8N1\n"
           "  --log [filename]   save output (default: XHCIQUAL.LOG)\n"
           "  --done-flag FILE   create FILE only on normal completion\n"
           "  --help, -h, /?     detailed standalone help\n",
           TOOL_VERSION);
}

static void print_help(void)
{
    qprintf("XHCIQUAL %s - DOS USB host-controller qualification\n\n",
            TOOL_VERSION);
    qprintf("PURPOSE\n"
            "  Qualifies xHCI, EHCI, and OHCI hardware for the Win98 SE and\n"
            "  Win2000 SP4 driver targets, tested via a PIC-mode OS (DOS).\n"
            "  C4 proves the device asserts INTx and the PIC path delivers\n"
            "  it: exact for Win98 and Win2000 PIC HALs. Win2000 APIC HALs\n"
            "  (both uniprocessor and multiprocessor) route through the\n"
            "  IOAPIC, which this tool does not test.\n"
            "  With no family selector, all three families are scanned.\n\n"
            "USAGE\n"
            "  XHCIQUAL                      read-only quick scan\n"
            "  XHCIQUAL [xhci|ehci|ohci|all] [options]\n"
            "  XHCIQUAL --scan TYPE [--scan TYPE ...] [options]\n"
            "  XHCIQUAL --help [--no-page]\n\n");
    qprintf("THE NO-ARGUMENT DEFAULT\n"
            "  With no arguments at all this tool does a READ-ONLY quick\n"
            "  scan: no PCI configuration writes, no ownership taken, one\n"
            "  screen. It has three honest outcomes, because a read-only\n"
            "  pass cannot observe C2, C3 or C4:\n"
            "    LOOKS QUALIFIED  subject to the active tests\n"
            "    DISQUALIFIED     a read-only pass genuinely sees this\n"
            "    CANNOT SAY       a state this pass may not change is in\n"
            "                     the way (not in D0, or MSE clear)\n"
            "  Every other invocation is unchanged. --full is the old\n"
            "  no-argument behaviour: the full active run, all families;\n"
            "  --quick asks for the scan explicitly and can be combined\n"
            "  with --serial, --log and --no-page like any other mode.\n\n");
    qprintf("SAFETY - READ BEFORE AN ACTIVE RUN\n"
            "  * Boot clean MS-DOS 7.1 (Win98): no EMM386, V86, or paging.\n"
            "  * Use PS/2 input; USB input may stop after ownership transfer.\n"
            "  * Do not boot/log through the controller being tested.\n"
            "  * Disconnect valuable storage; controller/port resets occur.\n"
            "  * If the machine freezes, power off and cold boot.\n\n");
    qprintf("RECOMMENDED TEST ORDER\n"
            "  0. XHCIQUAL                 (read-only, one screen)\n"
            "  1. XHCIQUAL --probe-only --no-page --log PROBE.LOG\n"
            "  2. Poll:  XHCIQUAL xhci --poll-only --no-wait --log XPOLL.LOG\n"
            "  3. IRQ:   XHCIQUAL xhci --irq-selftest --log XIRQ.LOG\n"
            "  4. Empty: XHCIQUAL TYPE --no-wait --log EMPTY.LOG\n"
            "  5. Device: XHCIQUAL TYPE --log DEVICE.LOG\n"
            "  6. All: XHCIQUAL --log ALLDEV.LOG\n"
            "  TYPE is xhci, ehci, or ohci. Selectors may be repeated.\n"
            "  Step 2 is the safe active probe: it resets, DMAs, and resets\n"
            "  ports without installing an interrupt handler.\n\n");
    qprintf("RECOMMENDED DEVICES\n"
            "  xHCI: expendable USB 2.0 flash drive; then wired HID/hub.\n"
            "  EHCI: known High-Speed USB 2.0 flash drive/card reader.\n"
            "  OHCI: simple wired Full-/Low-Speed mouse or keyboard.\n"
            "  Test direct ports before hubs or unusual devices.\n\n");
    qprintf("EXPECTED RESULTS\n"
            "  Empty port: C6 SKIP is normal; program must reach Done.\n"
            "  xHCI: C2/C3/C4 PASS; C6 PASS with USB2 device; C8 info.\n"
            "  EHCI: C2/C3/C4 PASS; C6 PASS with High-Speed device.\n"
            "  OHCI: C2/C3 PASS; C4 WARN is expected; C6 PASS with HID.\n"
            "  OHCI WARN proves SOF; its note says if PCI INTx was seen.\n\n");
    qprintf("FAMILY SELECTION\n"
            "  xhci|ehci|ohci    scan one family\n"
            "  all               restore the default all-family scan\n"
            "  --xhci/--ehci/--ohci  shorthand selectors\n"
            "  --scan TYPE       repeat to combine families\n\n");
    qprintf("OPTIONS\n"
            "  --quick           read-only quick scan (the no-argument\n"
            "                    default), one screen, three outcomes\n"
            "  --full            the full active run across all families -\n"
            "                    what no arguments used to mean\n"
            "  --probe-only      read-only discovery; leaves PCI MSE/INTx "
            "unchanged\n"
            "  --no-active       alias for --probe-only\n");
    qprintf("  --poll-only       active bring-up (C1/C2/C3/C6) with NO "
            "hardware\n"
            "                    interrupt handler installed; C4 is reported\n"
            "                    SKIP. Safe next step after a DOS/32A IRQ "
            "fault:\n"
            "                    it exercises reset, DMA, and port reset "
            "without\n"
            "                    the protected-mode ISR that can fault under\n"
            "                    the extender. Not a full qualification.\n");
    qprintf("  --irq-selftest    xHCI-only C1/C2/C3 plus an isolated, locked\n"
            "                    one-shot C4 ISR test. C6/C8 are not run.\n");
    qprintf("  --set-intel-ports try Intel USB2 routing; verify readback\n"
            "                    (writes PCI; an unusable readback is not\n"
            "                    reported as success or failure)\n"
            "  --no-wait         do not wait 15 seconds for a C6 plug\n"
            "  --no-devid        skip informational xHCI C8 identification\n"
            "  --no-page         disable screen pagination\n"
            "  --serial          mirror output to COM1 115200 8N1\n"
            "  --log [FILE]      save report; default XHCIQUAL.LOG\n"
            "  --done-flag FILE  delete FILE at start, recreate it only if\n"
            "                    the run finishes normally (crash-safe check\n"
            "                    for batch wrappers; no FIND.EXE needed)\n"
            "  --help, -h, /?    show this help and return success\n\n");
    qprintf("OUTPUT AND EXIT CODES\n"
            "  FACT lines describe controllers; xHCI DEV lines describe USB.\n"
            "  0 = active criteria met (OHCI may have documented C4 WARN)\n"
            "  1 = not qualified, or probe-only run\n"
            "  2 = usage error, or no selected controller found\n\n"
            "Keep all LOG files, BIOS settings, device model/speed, and a\n"
            "photo plus last printed line for any fault or freeze.\n");
}

static int parse_args(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        while (*a == '-' || *a == '/')
            a++;
        if (strcmp(a, "help") == 0 || strcmp(a, "h") == 0 ||
            strcmp(a, "?") == 0) {
            opt_help = 1;
        } else if (is_family_word(a)) {
            if (!select_family(a))
                return 0;
        } else if (strcmp(a, "scan") == 0 ||
                   strcmp(a, "controller") == 0) {
            if (i + 1 >= argc || !select_family(argv[++i])) {
                print_usage();
                return 0;
            }
        } else if (strncmp(a, "scan=", 5) == 0 ||
                   strncmp(a, "controller=", 11) == 0) {
            const char *name;

            name = (a[0] == 's') ? a + 5 : a + 11;
            if (!select_family(name)) {
                print_usage();
                return 0;
            }
        } else if (strcmp(a, "probe-only") == 0 ||
                   strcmp(a, "no-active") == 0) {
            opt_probe_only = 1;
        } else if (strcmp(a, "full") == 0) {
            /* The old no-argument behaviour, spelled out. It selects nothing
             * on its own - every default it names is already the default - so
             * its whole job is to be an argument, which is what takes the run
             * off the quick-scan path. */
            opt_quick = 0;
        } else if (strcmp(a, "quick") == 0) {
            /* The quick scan, asked for rather than defaulted into. Two things
             * need this and neither is cosmetic: the QEMU matrix, which reads
             * its verdict off the serial line and therefore has to pass
             * --serial, and any script that wants the read-only screen without
             * depending on "no arguments" staying the way to get it. */
            opt_quick = 1;
        } else if (strcmp(a, "poll-only") == 0) {
            opt_poll_only = 1;
        } else if (strcmp(a, "irq-selftest") == 0) {
            opt_irq_selftest = 1;
        } else if (strcmp(a, "set-intel-ports") == 0) {
            opt_set_intel = 1;
        } else if (strcmp(a, "serial") == 0) {
            opt_serial = 1;
        } else if (strcmp(a, "log") == 0) {
            opt_log_name = "XHCIQUAL.LOG";
            /* a family word after --log is a selector, not a filename */
            if (i + 1 < argc && argv[i + 1][0] != '-' &&
                argv[i + 1][0] != '/' && !is_family_word(argv[i + 1]))
                opt_log_name = argv[++i];
        } else if (strncmp(a, "log=", 4) == 0 && a[4] != '\0') {
            opt_log_name = a + 4;
        } else if (strcmp(a, "done-flag") == 0) {
            /* filename required; created only on normal completion so a
             * batch can tell a clean run from a DOS/32A crash with IF EXIST */
            if (i + 1 >= argc || argv[i + 1][0] == '-' ||
                argv[i + 1][0] == '/') {
                print_usage();
                return 0;
            }
            opt_done_flag = argv[++i];
        } else if (strncmp(a, "done-flag=", 10) == 0 && a[10] != '\0') {
            opt_done_flag = a + 10;
        } else if (strcmp(a, "no-devid") == 0) {
            opt_no_devid = 1;
        } else if (strcmp(a, "no-page") == 0) {
            opt_no_page = 1;
        } else if (strcmp(a, "no-wait") == 0) {
            opt_wait_secs = 0;
        } else {
            print_usage();
            return 0;
        }
    }
    if (opt_irq_selftest) {
        if (opt_probe_only || opt_poll_only || opt_set_intel ||
            (opt_family_seen && opt_family_mask != HC_MASK_XHCI)) {
            print_usage();
            return 0;
        }
        opt_family_mask = HC_MASK_XHCI;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Task 11-V.8 - the no-argument quick scan                            */
/* ------------------------------------------------------------------ */

/*
 * How many controllers get a line before the tail takes over.
 *
 * The claim this whole mode makes is "one screen", so per-controller output has
 * to be a fixed small number of lines rather than output that grows with the
 * machine. The pager already works to 23 usable rows (PAGE_ROWS in report.c)
 * and the extender's own startup output plus the DOS prompt come off the top
 * before this program prints anything, so the budget is smaller than 23 by an
 * amount that has to be **counted on a real console** rather than recalled -
 * a bench re-run on a multi-controller machine was where that would have
 * happened, and no machine this project has left carries more than one USB
 * controller, so it closed unobserved and is published as a limitation.
 *
 * Six is sized against a multi-controller machine with room to spare: every
 * fleet machine has a single xHCI function, but a machine carrying xHCI plus
 * two EHCI is the overflow case, and six leaves the fixed header and footer
 * inside 23 even if the extender takes several rows. No such machine is
 * available to this project, so the budget is checked in QEMU only.
 */
#define QUICK_MAX_LINES 6

/* Defined below, beside the full run that is its other caller. Declared here so
 * the quick scan reaches the same "effective Command register" answer rather
 * than reading cmd_orig itself. */
static void enable_pci(PCIINFO *p, int active);

static const char *quick_word(int outcome)
{
    if (outcome == QUICK_DISQUALIFIED)
        return "DISQUALIFIED";
    if (outcome == QUICK_CANNOT_SAY)
        return "CANNOT SAY";
    return "LOOKS QUALIFIED";
}

/*
 * The read-only pass, printed as a screen rather than as a report.
 *
 * Everything here is `--probe-only`'s contract - no PCI configuration writes,
 * no ownership taken - and the `Probe safety: PASS` line is the proof that it
 * held. It is printed rather than suppressed for brevity: a quick scan that
 * dropped the one line proving it was safe would be advertising the property it
 * stopped demonstrating.
 *
 * Returns the process exit code.
 */
static int run_quick_scan(void)
{
    int n, i, shown, hidden;
    int worst;

    qprintf("XHCIQUAL %s - quick scan (read-only)\n", TOOL_VERSION);

    n = pci_scan_usb(pcis, MAX_CONTROLLERS, HC_MASK_ALL);
    if (n == 0) {
        /*
         * No CC_0C0330 function is one of the disqualifiers a read-only pass
         * genuinely sees, and for this project's purpose it is the decisive
         * one - so it gets the verdict rather than the "no controller found"
         * usage error the other modes return.
         */
        qprintf("\nNo USB host controller found on this machine.\n");
        qprintf("DISQUALIFIED: with no xHCI function there is nothing for\n"
                "  xhci98.sys to bind to.\n");
        qprintf("Done.\n");
        return 1;
    }

    qprintf("Found %d USB host controller(s).\n\n", n);

    worst = QUICK_LOOKS_OK;
    shown = 0;
    hidden = 0;
    for (i = 0; i < n; i++) {
        PCIINFO *p;
        int outcome;
        int mmio_ok;
        int usb2_ports;

        p = &pcis[i];
        pci_read_static(p);
        /* Read-only: cmd_effective is the register as found. enable_pci() with
         * active = 0 does exactly this, and going through it keeps the two
         * paths agreeing about what "effective" means on a probe. */
        enable_pci(p, 0);

        mmio_ok = 0;
        usb2_ports = 0;
        if (p->hctype == HC_XHCI) {
            CTRL *c;

            c = &ctrls[0];
            memset(c, 0, sizeof(*c));
            c->pci = *p;
            c->quirk = quirk_find(p->vid, p->did);
            if (xhci_map(c)) {
                xhci_read_caps(c);
                mmio_ok = c->mmio_ok;
                usb2_ports = c->usb2_ports;
            }
            *p = c->pci;
        } else {
            LEGACY_CTRL *lc;

            lc = &legacy_ctrls[0];
            memset(lc, 0, sizeof(*lc));
            lc->pci = *p;
            if (legacy_map_and_read(lc))
                mmio_ok = lc->mmio_ok;
            /* A legacy controller has no Supported Protocol list to count USB2
             * ports from, and its ports are USB 2.0 or slower by construction.
             * Answering 1 keeps the shared classifier from reporting a
             * disqualifier that does not apply to it. */
            usb2_ports = 1;
            *p = lc->pci;
        }

        outcome = quick_classify(p, mmio_ok, usb2_ports, 0);
        if (outcome > worst)
            worst = outcome;

        {
            const char *why;

            why = quick_reason(p, mmio_ok, usb2_ports, 0);
            if (shown < QUICK_MAX_LINES) {
                qprintf("%-4s %04X:%04X %02X:%02X.%X  %s%s%s\n",
                        hc_name(p->hctype), p->vid, p->did,
                        p->bus, p->dev, p->fn, quick_word(outcome),
                        (why[0] != '\0') ? " - " : "", why);
                shown++;
            } else {
                /*
                 * Off the bottom of the screen but **not** out of the log (repo
                 * audit D1). `QUICK_MAX_LINES` bounds a 25-row console; a file
                 * and a serial line have no such bound, and truncating them
                 * identically is what made "run it again with logging" answer
                 * nothing.
                 */
                qlogprintf("%-4s %04X:%04X %02X:%02X.%X  %s%s%s\n",
                           hc_name(p->hctype), p->vid, p->did,
                           p->bus, p->dev, p->fn, quick_word(outcome),
                           (why[0] != '\0') ? " - " : "", why);
                hidden++;
            }
        }
    }

    if (hidden > 0) {
        /*
         * **The advice used to be `--log`, and `--log` is not a read-only
         * mode** (repo audit D1). Any argument makes `argc != 1`, so `opt_quick`
         * stays 0 and `XHCIQUAL --log` performs the full active bring-up - BIOS
         * handoff, HCRST, DMA and port resets on every controller - which is
         * precisely what the quick scan exists to shield a first-time user from
         * (task 11-V.8). The read-only spelling is the one the verdict footer
         * below already uses.
         */
        if (report_logging())
            qprintf("%d more controller(s) - the full list is in the log.\n",
                    hidden);
        else
            qprintf("%d more controller(s) - for the full list run:\n"
                    "  XHCIQUAL --probe-only --no-page --log PROBE.LOG\n",
                    hidden);
    }

    /*
     * The safety proof, and it is a measurement of this run rather than a
     * restatement of the mode's intent.
     */
    if (pci_config_write_count == 0) {
        qprintf("\nProbe safety: PASS - no PCI configuration writes.\n");
    } else {
        qprintf("\nProbe safety: INTERNAL FAILURE - %lu PCI configuration "
                "write(s).\n", pci_config_write_count);
        worst = QUICK_DISQUALIFIED;
    }

    /*
     * End with the next command to run, so the screen is an instruction rather
     * than just a verdict - and make it depend on the outcome, because "run the
     * active tests" is the wrong advice for a machine that is already out.
     */
    if (worst == QUICK_DISQUALIFIED) {
        qprintf("Verdict: DISQUALIFIED - at least one controller cannot "
                "work on either target.\n");
        qprintf("Next: XHCIQUAL --probe-only --no-page --log PROBE.LOG   "
                "(the detail behind it)\n");
    } else if (worst == QUICK_CANNOT_SAY) {
        qprintf("Verdict: CANNOT SAY - a state this read-only pass may not "
                "change is in the way.\n");
        qprintf("Next: XHCIQUAL --probe-only --no-page --log PROBE.LOG   "
                "(the detail behind it)\n");
    } else {
        qprintf("Verdict: LOOKS QUALIFIED, subject to the active tests "
                "(C2/C3/C4), which this\n"
                "  read-only pass cannot observe.\n");
        qprintf("Next: XHCIQUAL xhci --poll-only --no-wait --log XPOLL.LOG\n");
        qprintf("Then: XHCIQUAL --full --log ALLDEV.LOG\n");
    }

    /* The same completion marker every other mode prints, and it is not
     * cosmetic: the QEMU matrix's timeout guard reads it to tell a finished run
     * from a hung one, and its absence is what made the first quick-scan case
     * fail with "qualifier timed out" against perfectly correct output. */
    qprintf("Done.\n");

    /* Same convention as every other read-only mode: 1, because no
     * qualification verdict was reached. Only a full active run returns 0. */
    return 1;
}

/* enable MMIO decode (+ bus mastering for active runs); A2 verdict */
static void enable_pci(PCIINFO *p, int active)
{
    u16 want;
    u16 got;

    if (!active) {
        p->cmd_effective = p->cmd_orig;
        return;
    }

    want = p->cmd_orig | PCI_CMD_MSE | PCI_CMD_BME;
    want &= (u16)~PCI_CMD_INTX_OFF;      /* make sure INTx is not gated */
    if (want != p->cmd_orig)
        pci_write16(p->bus, p->dev, p->fn, 0x04, want);
    got = pci_read16(p->bus, p->dev, p->fn, 0x04);
    p->cmd_effective = got;
    p->bme_ok = (got & PCI_CMD_BME) ? 1 : 0;
}

int main(int argc, char **argv)
{
    int n, i, nx, nl, all_qualified, active;
    const char *mode;

    prescan_done_flag(argc, argv);
    reset_done_flag();
    /* Task 11-V.8: no arguments at all means the read-only quick scan. Set
     * before parse_args so a `--full` inside the loop can clear it, which is
     * unreachable at argc == 1 and is the belt to that brace. */
    opt_quick = (argc == 1);
    if (!parse_args(argc, argv))
        return finish_normally(2);
    if (opt_help) {
        if (!report_open(opt_log_name, opt_serial, !opt_no_page))
            printf("warning: could not open %s (read-only media?), "
                   "continuing\n", opt_log_name);
        print_help();
        report_close();
        return finish_normally(0);
    }
    active = !opt_probe_only;

    if (opt_quick) {
        int rc;

        /*
         * The same three output knobs as every other mode. In the default
         * no-argument case none of them is set, so this is the plain paged
         * screen with no file - and the pager staying on is the point, since a
         * quick scan that scrolled off the top would have failed at the one
         * thing it exists to do.
         */
        if (opt_log_name != 0 &&
            !report_open(opt_log_name, opt_serial, !opt_no_page)) {
            printf("warning: could not open %s (read-only media?), "
                   "continuing\n", opt_log_name);
            opt_log_name = 0;
        } else if (opt_log_name == 0) {
            (void)report_open(0, opt_serial, !opt_no_page);
        }
        rc = run_quick_scan();
        if (opt_log_name != 0)
            qprintf("Report copied to %s.\n", opt_log_name);
        report_close();
        return finish_normally(rc);
    }

    if (!report_open(opt_log_name, opt_serial, !opt_no_page)) {
        printf("warning: could not open %s (read-only media?), "
               "continuing\n", opt_log_name);
        opt_log_name = 0;   /* nothing was written: do not claim a copy */
    }

    qprintf("XHCIQUAL %s (build %s) - Win98/Win2000 USB qualification\n",
            TOOL_VERSION, TOOL_BUILD);
    if (!active)
        mode = "PROBE-ONLY (read-only)";
    else if (opt_poll_only)
        mode = "POLL-ONLY (active bring-up, no ISR)";
    else if (opt_irq_selftest)
        mode = "IRQ-SELFTEST (isolated locked one-shot C4)";
    else
        mode = "FULL (active bring-up tests)";
    qprintf("Mode: %s\n", mode);
    qprintf("Families: %s%s%s\n",
            (opt_family_mask & HC_MASK_XHCI) ? "xHCI " : "",
            (opt_family_mask & HC_MASK_EHCI) ? "EHCI " : "",
            (opt_family_mask & HC_MASK_OHCI) ? "OHCI" : "");

    n = pci_scan_usb(pcis, MAX_CONTROLLERS, opt_family_mask);
    if (n == 0) {
        qprintf("No selected USB host controller found on this machine.\n");
        report_close();
        return finish_normally(2);
    }
    qprintf("Found %d selected USB host controller(s).\n", n);

    all_qualified = 1;
    nx = 0;
    nl = 0;
    for (i = 0; i < n; i++) {
        if (pcis[i].hctype == HC_XHCI) {
            CTRL *c;

            c = &ctrls[nx++];
            memset(c, 0, sizeof(*c));
            c->pci = pcis[i];
            c->poll_only = opt_poll_only;
            c->irq_selftest = opt_irq_selftest;
            if (opt_irq_selftest) {
                strcpy(c->port_note, "isolated IRQ self-test; C6 not run");
                strcpy(c->dev_note, "isolated IRQ self-test; C8 not run");
            }
            pci_read_static(&c->pci);
            c->quirk = quirk_find(c->pci.vid, c->pci.did);
            enable_pci(&c->pci, active);
            if (!xhci_map(c)) {
                qprintf("\n%s controller %04X:%04X at %02X:%02X.%X: "
                        "BAR0 unusable.\n", hc_name(c->pci.hctype),
                        c->pci.vid, c->pci.did, c->pci.bus, c->pci.dev,
                        c->pci.fn);
            } else {
                xhci_read_caps(c);
            }
            if (active && c->mmio_ok) {
                /* Print and flush every gate the instant it finishes, so a
                 * fault before the end-of-run report still leaves the
                 * completed C1-C4 outcomes on screen/log/serial. */
                qprintf("\nRunning %s on xHCI %04X:%04X...\n",
                        opt_poll_only ? "poll-only tests" :
                        opt_irq_selftest ? "isolated IRQ self-test" :
                                           "active tests",
                        c->pci.vid, c->pci.did);
                report_flush();
                qual_handoff(c);
                qprintf("  C1 handoff: %-4s  %s\n",
                        verdict_name(c->v_handoff), c->handoff_note);
                report_flush();
                if (qual_reset(c)) {
                    qprintf("  C2 reset:   PASS  HCRST+CNR in %lu ms\n",
                            c->reset_ms);
                    report_flush();
                    if (qual_dma(c)) {
                        qprintf("  C3 DMA:     %-4s  %s\n",
                                verdict_name(c->v_dma), c->dma_note);
                        report_flush();
                        if (opt_poll_only) {
                            c->v_irq = V_SKIP;
                            strcpy(c->irq_note, "poll-only run: no ISR "
                                   "installed; CPU/PIC delivery not tested");
                            qprintf("  C4 IRQ:     SKIP  %s\n", c->irq_note);
                        } else {
                            qual_irq(c);
                            qprintf("  C4 IRQ:     %-4s  %s (ISR count "
                                    "%lu)\n", verdict_name(c->v_irq),
                                    c->irq_note, c->irq_isr_count);
                        }
                        report_flush();
                        if (opt_irq_selftest) {
                            qprintf("  C6 ports:   SKIP  %s\n", c->port_note);
                        } else if (opt_poll_only || c->v_irq == V_PASS) {
                            qual_intel_ports(c, opt_set_intel);
                            qual_ports(c, opt_wait_secs);
                            qprintf("  C6 ports:   %-4s  %s\n",
                                    verdict_name(c->v_port), c->port_note);
                            report_flush();
                            if (c->v_port == V_PASS && !opt_no_devid)
                                qual_devid(c);
                        } else {
                            c->v_port = V_SKIP;
                            strcpy(c->port_note, "C4 failed; use --poll-only "
                                   "before running C6");
                            qprintf("  C6 ports:   SKIP  %s\n", c->port_note);
                        }
                        report_flush();
                    } else {
                        c->v_irq = V_SKIP;
                        strcpy(c->irq_note, "C3 did not complete; C4 not run");
                        c->v_port = V_SKIP;
                        strcpy(c->port_note, "C3 did not complete; C6 not run");
                        qprintf("  C3 DMA:     %-4s  %s - skipping C4/C6\n",
                                verdict_name(c->v_dma), c->dma_note);
                    }
                } else {
                    c->v_dma = V_SKIP;
                    strcpy(c->dma_note, "C2 failed; C3 not run");
                    c->v_irq = V_SKIP;
                    strcpy(c->irq_note, "C2 failed; C4 not run");
                    c->v_port = V_SKIP;
                    strcpy(c->port_note, "C2 failed; C6 not run");
                    qprintf("  C2 reset:   FAIL - skipping C3-C6\n");
                }
                qual_cleanup(c);
                /* After cleanup, so the snapshot covers every transaction the
                 * run generated including teardown. Compared against the
                 * pre-run status in report_pci_status(). */
                pci_recheck_status(&c->pci);
            } else if (active) {
                pci_write16(c->pci.bus, c->pci.dev, c->pci.fn, 0x04,
                            c->pci.cmd_orig);
            }
            report_controller(c, active && c->mmio_ok);
            if (!final_verdict(c, active))
                all_qualified = 0;
        } else {
            LEGACY_CTRL *c;

            c = &legacy_ctrls[nl++];
            memset(c, 0, sizeof(*c));
            c->pci = pcis[i];
            c->poll_only = opt_poll_only;
            pci_read_static(&c->pci);
            enable_pci(&c->pci, active);
            if (!legacy_map_and_read(c)) {
                qprintf("\n%s controller %04X:%04X at %02X:%02X.%X: "
                        "BAR0 unusable.\n", hc_name(c->pci.hctype),
                        c->pci.vid, c->pci.did, c->pci.bus, c->pci.dev,
                        c->pci.fn);
            } else if (active) {
                qprintf("\nRunning active tests on %s %04X:%04X...\n",
                        hc_name(c->pci.hctype), c->pci.vid, c->pci.did);
                legacy_run(c, opt_wait_secs);
                legacy_cleanup(c);
                pci_recheck_status(&c->pci);
            }
            if (active && !c->mmio_ok) {
                pci_write16(c->pci.bus, c->pci.dev, c->pci.fn, 0x04,
                            c->pci.cmd_orig);
            }
            report_legacy_controller(c, active && c->mmio_ok);
            if (!legacy_final_verdict(c, active))
                all_qualified = 0;
        }
    }

    if (!active) {
        if (pci_config_write_count == 0) {
            qprintf("\nProbe safety: PASS - no PCI configuration writes.\n");
        } else {
            qprintf("\nProbe safety: INTERNAL FAILURE - %lu PCI "
                    "configuration write(s).\n", pci_config_write_count);
            all_qualified = 0;
        }
    }

    if (opt_log_name != 0)
        qprintf("\nDone. Report copied to %s.\n", opt_log_name);
    else
        qprintf("\nDone.\n");
    report_close();

    return finish_normally(all_qualified ? 0 : 1);
}
