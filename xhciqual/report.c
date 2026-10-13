/*
 * report.c - output plumbing (screen + optional log file + optional COM1
 * for QEMU/serial capture) and the Tier D per-controller report: the fact
 * sheet. The section 8 go/no-go verdict is verdict.c's (task 36.10 moved it
 * there so the host runner drives it). The FACT lines are the seed data for
 * the Phase 4 quirk/parameter tables.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include "qual.h"

static FILE *logf = 0;
/* A DOS handle the caller created exclusively (--log-stamp, INT 21h AH=5Bh
 * in real mode, sys_dos_creatnew) and handed over, written with INT 21h
 * AH=40h; -1 when there is none. */
static int log_handle = -1;
/* Set when a write to log_handle failed or fell short; nothing more is
 * written to it, and report_log_ok answers 0, so the run's last line does
 * not claim a log it did not write (the 38.1 MS-DOS leg's 0-byte logs were
 * announced as written). */
static int log_failed = 0;
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

static void sinks_on(int use_serial, int paginate)
{
    serial_on = use_serial;
    page_on = paginate && !use_serial;   /* serial capture is non-interactive */
    if (serial_on)
        serial_init();
}

/* The log is opened before COM1 is touched: a caller that refuses when the
 * log cannot be opened must not have programmed a UART first. A failure
 * returns 0 with no sink on; the caller decides whether to go on without
 * the file (report_open(0, ...)) or refuse. */
int report_open(const char *logname, int use_serial, int paginate)
{
    if (logname != 0) {
        logf = fopen(logname, "w");
        if (logf == 0)
            return 0;
    }
    sinks_on(use_serial, paginate);
    return 1;
}

/* The log is a handle the caller has already created (and so already knows
 * is writable); this takes ownership of it and closes it in report_close. */
void report_open_handle(int dos_handle, int use_serial, int paginate)
{
    log_handle = dos_handle;
    log_failed = 0;
    sinks_on(use_serial, paginate);
}

/* 1 unless a write to the handed-over log failed or fell short. */
int report_log_ok(void)
{
    return !log_failed;
}

void report_close(void)
{
    if (logf != 0) {
        fclose(logf);
        logf = 0;
    }
    if (log_handle >= 0) {
        (void)_dos_close(log_handle);
        log_handle = -1;
    }
}

/* Text-mode output on a raw DOS handle: LF becomes CR LF, as the CRT's "w"
 * stream does for --log. Each write goes to DOS at once, so a later fault
 * keeps every line already printed, as fflush does for the stream. */
static void handle_put(const char *p, unsigned n)
{
    unsigned wrote = 0;

    if (log_failed)
        return;
    if (_dos_write(log_handle, p, n, &wrote) != 0 || wrote != n)
        log_failed = 1;
}

static void handle_out(const char *buf)
{
    static const char crlf[2] = { '\r', '\n' };
    const char *p = buf;
    const char *run = buf;

    for (;; p++) {
        if (*p == '\n' || *p == '\0') {
            if (p > run)
                handle_put(run, (unsigned)(p - run));
            if (*p == '\0')
                break;
            handle_put(crlf, 2);
            run = p + 1;
        }
    }
}

static void log_out(const char *buf)
{
    if (logf != 0) {
        fputs(buf, logf);
        fflush(logf);   /* durable per line: a later fault keeps this gate */
    } else if (log_handle >= 0) {
        handle_out(buf);
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
    log_out(buf);
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

    if (logf == 0 && log_handle < 0 && !serial_on)
        return;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);   /* bounded: never overrun buf */
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';             /* _vsnprintf may not terminate */

    log_out(buf);
    if (serial_on) {
        for (p = buf; *p != 0; p++) {
            if (*p == '\n')
                serial_putc('\r');
            serial_putc(*p);
        }
    }
}

/*
 * The console alone (task 36.48): the quick scan's two-line system summary,
 * whose full block goes to the log and serial line through qlogprintf, so
 * neither sink carries the same machine twice.
 */
void qconprintf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);   /* bounded: never overrun buf */
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';             /* _vsnprintf may not terminate */
    console_out(buf);
}

/* Is there a durable sink at all? The quick scan asks so that it can say
 * "the full list is in the log" only when the full list really went there. */
int report_logging(void)
{
    return (logf != 0 || log_handle >= 0 || serial_on) ? 1 : 0;
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

void report_controller(CTRL *c, int active_ran)
{
    PCIINFO *p = &c->pci;
    int i;

    qprintf("\n==== Controller %02X:%02X.%X - %04X:%04X rev %02X ====\n",
            p->bus, p->dev, p->fn, p->vid, p->did, p->rev);
    /* The exact row, the vendor-wide row and the HCIVERSION floor note
     * (quirks.c; host-tested by test/test_quirks.c). HCIVERSION is
     * valid only where MMIO answered. */
    quirk_report(p->vid, p->did, c->mmio_ok, c->hciversion, qprintf);

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
    /* Task 36.11: read before any active test, so it is the firmware's
     * routing as found, whatever this run went on to do. */
    report_route(&c->route, c->route_gsi, qprintf);
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

    /* Each protocol with its PSI entries decoded and the issue 11 warning, on
     * screen and in the log (roadmap task 35.1). */
    report_protocols(c, qprintf);
    qprintf("  Port map:\n");
    for (i = 1; i <= c->maxports && i <= MAX_PORTS; i++)
        qprintf("    port %2d: %s\n", i, class_name(c->portclass[i]));
    /* The raw chain behind the lines above, for a host replay vector: the log
     * and serial line only, since it runs to dozens of rows and the screen is
     * 25 (README, "Raw extended-capability dump"). */
    report_xcap_dump(c, qlogprintf);

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
