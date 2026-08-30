/*
 * report.c - output plumbing (screen + optional log file + optional COM1
 * for QEMU/serial capture) and the Tier D per-controller report: fact sheet
 * plus the section 8 go/no-go verdict. The FACT lines are the seed data
 * for the Phase 4 quirk/parameter tables.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <conio.h>
#include "qual.h"

static FILE *logf = 0;
static int serial_on = 0;

/* console pager: pause every screenful so the report can be read on bare
 * metal (the log/serial copies are never paginated). 25-row screen, two
 * rows reserved for the prompt. */
#define PAGE_ROWS 23
static int page_on = 0;
static int page_row = 0;
static int page_col = 0;

#define COM1_BASE 0x3F8

static void serial_init(void)
{
    outp(COM1_BASE + 3, 0x80);          /* DLAB */
    outp(COM1_BASE + 0, 1);             /* 115200 baud */
    outp(COM1_BASE + 1, 0);
    outp(COM1_BASE + 3, 0x03);          /* 8N1 */
    outp(COM1_BASE + 1, 0);             /* no UART interrupts */
    outp(COM1_BASE + 4, 0x03);          /* DTR + RTS */
}

static void serial_putc(char ch)
{
    unsigned guard;

    for (guard = 0; guard < 60000U; guard++) {
        if (inp(COM1_BASE + 5) & 0x20)
            break;
    }
    outp(COM1_BASE + 0, (u8)ch);
}

int report_open(const char *logname, int use_serial, int paginate)
{
    serial_on = use_serial;
    page_on = paginate && !use_serial;   /* serial capture is non-interactive */
    if (serial_on)
        serial_init();
    if (logname != 0) {
        logf = fopen(logname, "w");
        if (logf == 0)
            return 0;
    }
    return 1;
}

void report_close(void)
{
    if (logf != 0) {
        fclose(logf);
        logf = 0;
    }
}

/* Force every sink to durable storage. Called at each gate checkpoint so a
 * CPU protection fault before cleanup still leaves the completed gates on
 * screen, in the log file, and on the serial line. */
void report_flush(void)
{
    fflush(stdout);
    if (logf != 0)
        fflush(logf);
}

/* pause after a screenful; ESC turns paging off for the rest of the run */
static void page_pause(void)
{
    int ch;

    fflush(stdout);
    fputs("-- More -- (any key, ESC = no more pauses) --", stdout);
    fflush(stdout);
    ch = getch();
    fputs("\r                                             \r", stdout);
    fflush(stdout);
    if (ch == 27)
        page_on = 0;
    page_row = 0;
    page_col = 0;
}

/* write to the console, counting screen rows (incl. wrap) for the pager */
static void console_out(const char *buf)
{
    const char *p;

    if (!page_on) {
        fputs(buf, stdout);
        return;
    }
    for (p = buf; *p != 0; p++) {
        putchar(*p);
        if (*p == '\n') {
            page_row++;
            page_col = 0;
        } else if (*p == '\r') {
            page_col = 0;
        } else if (++page_col >= 80) {
            page_row++;
            page_col = 0;
        }
        if (page_on && page_row >= PAGE_ROWS)
            page_pause();
    }
}

void qprintf(const char *fmt, ...)
{
    char buf[1024];
    const char *p;
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);   /* bounded: never overrun buf */
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';             /* _vsnprintf may not terminate */

    console_out(buf);
    if (logf != 0) {
        fputs(buf, logf);
        fflush(logf);   /* durable per line: a later fault keeps this gate */
    }
    if (serial_on) {
        for (p = buf; *p != 0; p++) {
            if (*p == '\n')
                serial_putc('\r');
            serial_putc(*p);
        }
    }
}

/*
 * The same line, to the durable sinks only (repo audit D1).
 *
 * The quick scan bounds what it puts on a 25-row screen, and before this the
 * bound truncated the *log* identically - so "run it again with logging" could
 * not answer the question the truncation raised. A file and a serial line have
 * no such bound, and a controller the screen had no room for is exactly the one
 * a reader went to the log for.
 */
void qlogprintf(const char *fmt, ...)
{
    char buf[1024];
    const char *p;
    va_list ap;

    if (logf == 0 && !serial_on)
        return;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);   /* bounded: never overrun buf */
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';             /* _vsnprintf may not terminate */

    if (logf != 0) {
        fputs(buf, logf);
        fflush(logf);
    }
    if (serial_on) {
        for (p = buf; *p != 0; p++) {
            if (*p == '\n')
                serial_putc('\r');
            serial_putc(*p);
        }
    }
}

/* Is there a durable sink at all? The quick scan asks so that it can say
 * "the full list is in the log" only when the full list really went there. */
int report_logging(void)
{
    return (logf != 0 || serial_on) ? 1 : 0;
}

const char *verdict_name(int v)
{
    switch (v) {
    case V_PASS: return "PASS";
    case V_WARN: return "WARN";
    case V_FAIL: return "FAIL";
    default:     return "SKIP";
    }
}

static const char *class_name(int pc)
{
    switch (pc) {
    case PC_USB2_ONLY:   return "USB2-only (managed)";
    case PC_USB2_PAIRED: return "USB2 companion (managed)";
    case PC_USB3_PAIRED: return "USB3 companion (unmanaged)";
    case PC_USB3_ORPHAN: return "USB3 orphan (unmanaged)";
    default:             return "unclassified";
    }
}

static void print_quirks(const QUIRK *q)
{
    if (q == 0) {
        qprintf("  quirks: none known for this VID/DID\n");
        return;
    }
    qprintf("  quirks: %s\n", q->name);
    if (q->flags & QF_XUSB2PR)
        qprintf("    - EHCI<->xHCI port mux (XUSB2PR): run C7 before "
                "judging dead ports\n");
    if (q->flags & QF_FW_UPLOAD)
        qprintf("    - needs driver firmware upload if the card is "
                "ROM-less\n");
    if (q->flags & QF_FW_SPI)
        qprintf("    - firmware from on-card SPI flash (no upload)\n");
    if (q->flags & QF_SPURIOUS)
        qprintf("    - spurious-success: always use residual length\n");
    if (q->flags & QF_BEI)
        qprintf("    - never set BEI in isoch TRBs\n");
    if (q->flags & QF_PME_STUCK)
        qprintf("    - PME wake latch bug (matters on Win2000 power "
                "transitions)\n");
    if (q->flags & QF_BULK64K)
        qprintf("    - keep bulk TRB chains under 64 KB\n");
    if (q->flags & QF_CMD_RETRY)
        qprintf("    - retry TRB Error command completions once\n");
    if (q->flags & QF_BROKEN_MSI)
        qprintf("    - broken MSI (harmless: this project uses INTx)\n");
    if (q->flags & QF_AVOID)
        qprintf("    - known-unreliable silicon: avoid as test platform\n");
}

void report_controller(CTRL *c, int active_ran)
{
    PCIINFO *p = &c->pci;
    int i;

    qprintf("\n==== Controller %02X:%02X.%X - %04X:%04X rev %02X ====\n",
            p->bus, p->dev, p->fn, p->vid, p->did, p->rev);
    print_quirks(c->quirk);

    qprintf("  PCI: cmd=%04X (MSE=%d BME=%d INTxDis=%d)  pin=INT%c#  "
            "line=IRQ %u\n",
            p->cmd_orig,
            (p->cmd_orig & PCI_CMD_MSE) ? 1 : 0,
            (p->cmd_orig & PCI_CMD_BME) ? 1 : 0,
            (p->cmd_orig & PCI_CMD_INTX_OFF) ? 1 : 0,
            (p->ipin >= 1 && p->ipin <= 4) ? ('A' + p->ipin - 1) : '?',
            p->iline);
    qprintf("  PCI subsys: %04X:%04X\n", p->subsys_vid, p->subsys_did);
    qprintf("  PCI caps: PM=%d MSI=%d(en=%d) MSI-X=%d PCIe=%d\n",
            p->has_pm, p->has_msi, p->msi_enabled, p->has_msix, p->has_pcie);
    report_pci_status(p);
    report_pci_pm(p);
    qprintf("  BAR0: %08lX%s%s (raw lo=%08lX hi=%08lX)\n",
            p->bar_phys,
            p->bar_is64 ? " 64-bit" : " 32-bit",
            p->bar_pref ? " prefetchable" : "",
            p->bar_lo, p->bar_hi);

    if (!c->mmio_ok) {
        qprintf("  MMIO: NOT ACCESSIBLE - Tier B/C skipped\n");
        report_mmio_dead(p);
        return;
    }

    qprintf("  HCIVERSION %X.%02X  CAPLENGTH %u  ContextSize %d  AC64=%d  "
            "PPC=%d\n",
            c->hciversion >> 8, c->hciversion & 0xFF, c->caplength,
            c->csz, c->ac64, c->ppc);
    qprintf("  MaxSlots %d  MaxIntrs %d  MaxPorts %d  Scratchpad %d  "
            "PAGESIZE %08lX\n",
            c->maxslots, c->maxintrs, c->maxports, c->spbufs, c->pagesize);
    /*
     * HCCPARAMS2 - here for FSC, which decides whether xhci98 may take a Save
     * State at all: with FSC = 0 the spec requires Stop Endpoint on every Idle
     * *and* Busy Running endpoint before the save (4.23.2 p.313), the driver
     * issues neither on that path, and so it declines the save and every resume
     * reinitialises the bus instead of restoring it. That is a real difference
     * in what a machine does after standby, and until this line existed the only
     * way to guess it was from HCIVERSION - which is not what the bit says.
     */
    if (c->hcc2_ok) {
        qprintf("  HCCPARAMS2 %08lX  FSC=%d U3C=%d CMC=%d%s\n",
                c->hcc2, c->fsc, c->u3c, c->cmc,
                c->fsc ? "" : "  (FSC=0: suspend/resume re-enumerates)");
    } else if ((u32)c->caplength < XCAP_HCCPARAMS2_END) {
        qprintf("  HCCPARAMS2: absent - CAPLENGTH %u ends before it\n",
                c->caplength);
    } else {
        qprintf("  HCCPARAMS2: unreadable - all ones\n");
    }
    qprintf("  USBLEGSUP: %s\n",
            c->legsup_off ? "present" : "absent (no BIOS handoff needed)");

    for (i = 0; i < c->nproto; i++) {
        PROTOCAP *pr = &c->proto[i];
        if (pr->portcnt == 0)   /* seen on qemu-xhci with p3=0 */
            qprintf("  Protocol USB %X.%X: no ports, slot type %d, "
                    "PSIC %d\n",
                    pr->major, pr->minor, pr->slottype, pr->psic);
        else
            qprintf("  Protocol USB %X.%X: ports %d-%d, slot type %d, "
                    "PSIC %d\n",
                    pr->major, pr->minor, pr->portoff,
                    pr->portoff + pr->portcnt - 1, pr->slottype, pr->psic);
    }
    qprintf("  Port map:\n");
    for (i = 1; i <= c->maxports && i <= MAX_PORTS; i++)
        qprintf("    port %2d: %s\n", i, class_name(c->portclass[i]));

    if (active_ran) {
        qprintf("  --- Tier C results ---\n");
        qprintf("  C1 handoff:  %-4s  %s\n",
                verdict_name(c->v_handoff), c->handoff_note);
        qprintf("  C2 reset:    %-4s  HCRST+CNR in %lu ms\n",
                verdict_name(c->v_reset), c->reset_ms);
        qprintf("  C3 DMA:      %-4s  %s\n",
                verdict_name(c->v_dma),
                c->dma_note[0] ? c->dma_note : "not run");
        qprintf("  C4 IRQ:      %-4s  %s (ISR count %lu)\n",
                verdict_name(c->v_irq), c->irq_note, c->irq_isr_count);
        if (c->irq_foreign_count != 0)
            qprintf("               (%lu foreign interrupt(s) on this "
                    "vector - line masked for safety)\n",
                    c->irq_foreign_count);
        qprintf("  C6 ports:    %-4s  %s\n",
                verdict_name(c->v_port), c->port_note);
        qprintf("  C8 devices:  %-4s  %s (informational)\n",
                verdict_name(c->v_dev),
                c->dev_note[0] ? c->dev_note : "not run");
        if (c->cleanup_failed)
            qprintf("  Cleanup:     FAIL  %s\n", c->cleanup_note);
        for (i = 0; i < c->ndevs; i++) {
            DEVINFO *d = &c->devs[i];

            if (!d->ok) {
                qprintf("    port %2u: NOT IDENTIFIED - %s\n",
                        d->port, d->note);
                continue;
            }
            qprintf("    port %2u: %04X:%04X rev %X.%02X  USB %X.%02X  "
                    "%s  addr %u  MPS0 %u\n",
                    d->port, d->vid, d->pid,
                    d->bcddev >> 8, d->bcddev & 0xFF,
                    d->bcdusb >> 8, d->bcdusb & 0xFF,
                    speed_name_port(c, d->port, d->speed), d->addr, d->mps0);
            qprintf("             class %02X/%02X/%02X (%s)",
                    d->cls, d->sub, d->protocol,
                    usb_class_name(d->cls));
            if (d->nifc > 0)
                qprintf("  %d iface(s), first %02X/%02X/%02X (%s)",
                        d->nifc, d->icls, d->isub, d->iproto,
                        usb_class_name(d->icls));
            qprintf("\n");
            if (d->manuf[0] || d->product[0])
                qprintf("             \"%s\" \"%s\"\n",
                        d->manuf, d->product);
        }
    }

    /* machine-readable seed for the Phase 4 parameter/quirk tables */
    {
        /*
         * "Could not be read" is a third answer and not a 0, in both fields: a
         * consumer that read `hcc2=00000000 fsc=0` off an unreachable register
         * would record a controller as declining Save State when nothing said
         * so. So the word is `unread` and the bit is `-1`, neither of which
         * parses as a value.
         */
        char hcc2Text[16];

        if (c->hcc2_ok) {
            sprintf(hcc2Text, "%08lX", c->hcc2);
        } else {
            strcpy(hcc2Text, "unread");
        }
        qprintf("  FACT id=%04X:%04X rev=%02X bar=%08lX irq=%u pin=%u "
                "hciver=%04X csz=%d ac64=%d ppc=%d slots=%d intrs=%d "
                "ports=%d scratch=%d usb2ports=%d legsup=%d hcc2=%s fsc=%d\n",
                p->vid, p->did, p->rev, p->bar_phys, p->iline, p->ipin,
                c->hciversion, c->csz, c->ac64, c->ppc, c->maxslots,
                c->maxintrs, c->maxports, c->spbufs, c->usb2_ports,
                c->legsup_off ? 1 : 0, hcc2Text, c->hcc2_ok ? c->fsc : -1);
    }
    for (i = 0; i < c->ndevs; i++) {
        DEVINFO *d = &c->devs[i];

        if (d->ok)
            qprintf("  DEV port=%u speed=%u vid=%04X pid=%04X "
                    "class=%02X.%02X.%02X iface=%02X.%02X.%02X mps0=%u\n",
                    d->port, d->speed, d->vid, d->pid,
                    d->cls, d->sub, d->protocol,
                    d->icls, d->isub, d->iproto, d->mps0);
    }
}

int final_verdict(CTRL *c, int active_requested)
{
    int qualified = 1;
    int warned = 0;
    int disqualified = 0;
    int tool_limited = 0;
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

    if (c->pci.ipin == 0) {
        qprintf("  DISQUALIFIED: Interrupt Pin = 0 - MSI-only; neither "
                "target's line-based stack gets interrupts\n");
    }
    if (!c->mmio_ok) {
        (void)report_mmio_unavailable(&c->pci, active_requested);
        if (!active_requested)
            qprintf("  Probe-only run: active tests (C1-C7) NOT run.\n");
        qprintf("  ==> NOT QUALIFIED for cross-target Win98/Win2000 use\n");
        if (tool_limited && !disqualified)
            qprintf("      No controller fault was inferred from the "
                    "temporary state above.\n");
        return 0;
    }
    if (c->usb2_ports == 0) {
        qprintf("  DISQUALIFIED: no USB2 protocol ports (B7 empty)\n");
    }

    if (!active_requested) {
        qprintf("  Probe-only run: %s so far. Active tests (C1-C7) NOT "
                "run - no qualification verdict.\n",
                qualified ? "no disqualifiers" : "disqualified");
        return 0;
    }

    if (c->irq_selftest) {
        if (c->v_handoff == V_WARN)
            warned = 1;
        if (c->v_reset != V_PASS) {
            qprintf("  IRQ SELF-TEST FAILURE: halt/reset (C2) failed\n");
            qualified = 0;
        } else if (c->v_dma != V_PASS) {
            qprintf("  IRQ SELF-TEST FAILURE: DMA round-trip (C3) was %s - "
                    "%s\n", verdict_name(c->v_dma), c->dma_note);
            qualified = 0;
        }
        if (c->v_reset == V_PASS &&
            (c->v_dma == V_PASS || c->v_dma == V_WARN) &&
            c->v_irq != V_PASS) {
            qprintf("  IRQ SELF-TEST FAILURE: C4: %s\n", c->irq_note);
            qualified = 0;
        }
        if (c->cleanup_failed) {
            qprintf("  IRQ SELF-TEST FAILURE: cleanup: %s\n",
                    c->cleanup_note);
            qualified = 0;
        }
        if (qualified) {
            qprintf("  ==> IRQ SELF-TEST PASS%s - locked one-shot ISR and "
                    "teardown completed.\n",
                    warned ? " WITH HANDOFF WARNING" : "");
        } else if (c->cleanup_failed) {
            qprintf("  ==> IRQ SELF-TEST FAILED - cleanup could not safely "
                    "quiesce the controller.\n");
        } else {
            qprintf("  ==> IRQ SELF-TEST FAILED - controller is not "
                    "disqualified by a tool/DPMI failure.\n");
        }
        qprintf("      C6/C8 and the full qualification verdict were not "
                "run.\n");
        return qualified;
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
        }
        return 0;
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
            c->v_irq != V_PASS) {
            if (c->v_irq == V_FAIL) {
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
    } else {
        qprintf("  ==> NOT QUALIFIED for cross-target Win98/Win2000 use\n");
        if (tool_limited && !disqualified)
            qprintf("      No controller fault was inferred from the tool "
                    "limit above.\n");
    }
    return qualified;
}
