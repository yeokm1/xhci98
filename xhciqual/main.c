/*
 * main.c - XHCIQUAL: Phase 0 DOS hardware qualification tool.
 *
 * Answers, per controller: "can a PIC-mode OS bring up this USB host
 * controller and receive its interrupts?"
 * See docs/contributing/design/01-hardware-qualification-tool.md.
 *
 * Exit codes: 0 = all controllers qualified, 1 = at least one not
 * qualified (or probe-only), 2 = no selected controller / usage error,
 * 3 = none not qualified and at least one QUALIFIED WITH CAVEATS (no
 * working interrupt: the driver polls it - task 36.10), 4 = --log-stamp
 * refused before any hardware access: the DOS clock is not a date and time,
 * the minute's 26 names are all taken, or the log cannot be created (task
 * 38.1).
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>
#include "qual.h"
#include "stamp.h"

static PCIINFO pcis[MAX_CONTROLLERS];
static CTRL ctrls[MAX_CONTROLLERS];
static LEGACY_CTRL legacy_ctrls[MAX_CONTROLLERS];
/* Task 36.48: the machine, named in the header (never a serial or UUID). */
static SYSINFO sysinfo;

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
/* Task 36.48 (owner): skip the system description's probing entirely. */
static int opt_no_sysinfo  = 0;
static int opt_no_page     = 0;
static int opt_help        = 0;
static const char *opt_done_flag = 0;
static int opt_wait_secs   = 15;
static int opt_family_mask = HC_MASK_ALL;
static int opt_family_seen = 0;
static const char *opt_log_name = 0;
/* Task 38.1 (owner, 2026-10-13): the log named from the DOS clock, in the
 * current directory, never overwriting an earlier one, because the field
 * report's batch cannot build such a name itself. Exclusive with --log. */
static int opt_log_stamp   = 0;
static int opt_log_plain   = 0;
static char stamp_buf[STAMP_NAME_MAX];
static int stamp_handle = -1;   /* the stamped log, created exclusively */
static unsigned stamp_year, stamp_month, stamp_day;
static unsigned stamp_hour, stamp_minute, stamp_second;
/* Task 36.11: the GSI to compare the Intel PCH routing against, -1 none. */
static long opt_gsi         = -1;

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
        /* --gsi takes the next token whatever it is, as parse_args does, so
         * an operand spelled like an option is never read as one here. */
        if (strcmp(a, "scan") == 0 || strcmp(a, "controller") == 0 ||
            strcmp(a, "gsi") == 0) {
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
           "  --no-sysinfo       do not read SMBIOS, CPUID, memory or DOS\n"
           "  --no-page          do not pause the screen every page\n"
           "  --serial           mirror output to COM1 115200 8N1\n"
           "  --log [filename]   save output (default: XHCIQUAL.LOG)\n"
           "  --log-stamp        save output to a new log named from the DOS\n"
           "                     clock (mDDhhmmS.LOG); never overwrites\n"
           "  --done-flag FILE   create FILE only on normal completion\n"
           "  --gsi N            with --quick or --probe-only: compare the\n"
           "                     Intel PCH routing against GSI/IRQ N\n"
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
            "  --no-sysinfo      do not read the system description (SMBIOS,\n"
            "                    CPUID, E820, DOS) - for a machine where that\n"
            "                    probing itself misbehaves\n"
            "  --no-page         disable screen pagination\n"
            "  --serial          mirror output to COM1 115200 8N1\n"
            "  --log [FILE]      save report; default XHCIQUAL.LOG\n"
            "  --log-stamp       save report to a NEW log in the current\n"
            "                    directory named from the DOS clock:\n"
            "                    mDDhhmmS.LOG - month 1-9/A/B/C, day, hour,\n"
            "                    minute, then the first free letter A-Z.\n"
            "                    Never overwrites; not with --log. Exit 4,\n"
            "                    before any hardware access, if the clock\n"
            "                    is not a date, all 26 names are taken, or\n"
            "                    the log cannot be created (QUALREP.BAT)\n"
            "  --done-flag FILE  delete FILE at start, recreate it only if\n"
            "                    the run finishes normally (crash-safe check\n"
            "                    for batch wrappers; no FIND.EXE needed)\n"
            "  --gsi N           with --quick or --probe-only only: compare\n"
            "                    the xHCI's Intel PCH interrupt routing\n"
            "                    (D20IR, PIRQx_ROUT) against N, the GSI or\n"
            "                    IRQ Windows reports for the controller\n"
            "                    (16-23 an APIC input; below 16 a PIC IRQ,\n"
            "                    compared with the routing DOS found only).\n"
            "                    The routing itself is reported on every\n"
            "                    run, read only, on the reviewed Intel\n"
            "                    7/8/9-series and C610 xHCIs alone\n"
            "  --help, -h, /?    show this help and return success\n\n");
    qprintf("OUTPUT AND EXIT CODES\n"
            "  FACT lines describe controllers; xHCI DEV lines describe USB.\n"
            "  0 = active criteria met (OHCI may have documented C4 WARN)\n"
            "  1 = not qualified, or probe-only run\n"
            "  2 = usage error, or no selected controller found\n"
            "  3 = QUALIFIED WITH CAVEATS: no working interrupt, so\n"
            "      xhci98.sys polls it (or an MSI-capable OS)\n"
            "  4 = --log-stamp refused; nothing was read\n\n"
            "Keep all LOG files, BIOS settings, device model/speed, and a\n"
            "photo plus last printed line for any fault or freeze.\n");
}

/* A decimal GSI, 0 to 255; anything else is a usage error. */
static int parse_gsi(const char *s)
{
    long v;

    if (s == 0 || *s == '\0')
        return 0;
    v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        if (v > 255)
            return 0;
        s++;
    }
    if (*s != '\0')
        return 0;
    opt_gsi = v;
    return 1;
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
        } else if (strcmp(a, "log-stamp") == 0) {
            opt_log_stamp = 1;
        } else if (strcmp(a, "log") == 0) {
            opt_log_plain = 1;
            opt_log_name = "XHCIQUAL.LOG";
            /* a family word after --log is a selector, not a filename */
            if (i + 1 < argc && argv[i + 1][0] != '-' &&
                argv[i + 1][0] != '/' && !is_family_word(argv[i + 1]))
                opt_log_name = argv[++i];
        } else if (strncmp(a, "log=", 4) == 0 && a[4] != '\0') {
            opt_log_plain = 1;
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
        } else if (strcmp(a, "gsi") == 0) {
            if (i + 1 >= argc || !parse_gsi(argv[++i])) {
                print_usage();
                return 0;
            }
        } else if (strncmp(a, "gsi=", 4) == 0) {
            if (!parse_gsi(a + 4)) {
                print_usage();
                return 0;
            }
        } else if (strcmp(a, "no-devid") == 0) {
            opt_no_devid = 1;
        } else if (strcmp(a, "no-sysinfo") == 0) {
            opt_no_sysinfo = 1;
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
    /* The quick scan is a whole mode, not a modifier: main() takes its
     * branch before any of these four is looked at, so a run that named one
     * of them beside --quick used to get the scan and no word about the
     * other (the 2026-09-17 audit's C5). */
    if (opt_quick && (opt_probe_only || opt_poll_only || opt_irq_selftest ||
                      opt_set_intel)) {
        print_usage();
        return 0;
    }
    /* Task 36.11: the comparison is read-only work, and naming it must
     * never be what starts an active run - `XHCIQUAL --gsi 21` alone would
     * otherwise take the full run, since only no arguments at all means
     * the quick scan. So it is refused unless a read-only mode is named. */
    if (opt_gsi >= 0 && !opt_quick && !opt_probe_only) {
        print_usage();
        return 0;
    }
    /* Task 38.1: one log, named one way. */
    if (opt_log_stamp && opt_log_plain) {
        print_usage();
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Task 38.1 - the log named from the DOS clock                        */
/* ------------------------------------------------------------------ */

/* Anything of that name - a file, hidden or read-only, or a directory -
 * takes the name: the attribute call answers for all of them. */
static int dos_name_exists(const char *name, void *ctx)
{
    unsigned attr;

    (void)ctx;
    return _dos_getfileattr(name, &attr) == 0;
}

/* INT 21h AH=5Bh, "create new file": fails with error 50h (file exists)
 * where AH=3Ch, and so fopen "w", would truncate. The handle is kept and
 * becomes the log, so the name is never opened a second time. */
#define DOS_ERR_FILE_EXISTS 0x50

static int dos_create_new(const char *name, void *ctx)
{
    int h;
    unsigned rc;

    (void)ctx;
    rc = _dos_creatnew(name, _A_NORMAL, &h);
    if (rc == 0) {
        stamp_handle = h;
        return STAMP_CREATE_OK;
    }
    return (rc == DOS_ERR_FILE_EXISTS) ? STAMP_CREATE_EXISTS
                                       : STAMP_CREATE_FAIL;
}

/* INT 21h AH=2Ah and AH=2Ch. The date is read again after the time, and
 * the time again if the date moved, so a run started on the stroke of
 * midnight is not named with one day's date and the next day's time. */
static void read_dos_clock(void)
{
    struct dosdate_t d1, d2;
    struct dostime_t t;

    _dos_getdate(&d1);
    _dos_gettime(&t);
    _dos_getdate(&d2);
    if (d2.day != d1.day || d2.month != d1.month || d2.year != d1.year) {
        _dos_gettime(&t);
        d1 = d2;
    }
    stamp_year = d1.year;
    stamp_month = d1.month;
    stamp_day = d1.day;
    stamp_hour = t.hour;
    stamp_minute = t.minute;
    stamp_second = t.second;
}

/* Chooses the name and creates the file, or refuses. Runs before any sink
 * is opened (COM1 included) and before anything reads the machine, so an
 * unwritable directory is refused having touched nothing. Returns 1 with
 * opt_log_name set and stamp_handle open, 0 refused. */
static int resolve_log_stamp(void)
{
    char prefix[8];
    int rc;

    read_dos_clock();
    rc = stamp_create(stamp_month, stamp_day, stamp_hour, stamp_minute,
                      "LOG", dos_name_exists, dos_create_new, 0, stamp_buf);
    if (rc == STAMP_OK) {
        opt_log_name = stamp_buf;
        return 1;
    }
    if (rc == STAMP_CREATE_FAILED) {
        printf("XHCIQUAL: --log-stamp refused: could not create %s in this\n"
               "  directory (read-only media, a full disk, or a full root\n"
               "  directory?). Nothing was read (exit 4).\n", stamp_buf);
        return 0;
    }
    if (rc == STAMP_ALL_TAKEN &&
        stamp_prefix(stamp_month, stamp_day, stamp_hour, stamp_minute,
                     prefix)) {
        printf("XHCIQUAL: --log-stamp refused: %sA.LOG to %sZ.LOG all\n"
               "  exist in this directory. Nothing was read and nothing was\n"
               "  overwritten. Wait for the next minute, or move the old logs\n"
               "  elsewhere, then run again (exit 4).\n", prefix, prefix);
    } else {
        printf("XHCIQUAL: --log-stamp refused: the DOS clock reads\n"
               "  %04u-%02u-%02u %02u:%02u, which is not a date and time a\n"
               "  log can be named from. Set it with DATE and TIME, then run\n"
               "  again. Nothing was read (exit 4).\n",
               stamp_year, stamp_month, stamp_day, stamp_hour,
               stamp_minute);
    }
    return 0;
}

/* The log's first lines carry what its name cannot: the year, and the
 * second. */
static void print_stamp_header(void)
{
    if (!opt_log_stamp)
        return;
    qprintf("Log: %s, begun %04u-%02u-%02u %02u:%02u:%02u by the DOS "
            "clock\n", stamp_buf, stamp_year, stamp_month, stamp_day,
            stamp_hour, stamp_minute, stamp_second);
}

static void print_stamp_tail(void)
{
    if (opt_log_stamp && opt_log_name != 0)
        qprintf("Log written: %s - attach this file\n", opt_log_name);
}

/*
 * Opens the report's sinks. A plain --log that cannot be opened is warned
 * about and the run goes on without a file, as it always has; a --log-stamp
 * run exists to leave a log, so its refusal is resolve_log_stamp's, before
 * any sink is opened, and by here the file is already created. Returns 1 to
 * go on (kept for a refusal a later sink might need).
 */
static int open_report(void)
{
    if (opt_log_stamp) {
        /* resolve_log_stamp created it, or the run was refused there. */
        report_open_handle(stamp_handle, opt_serial, !opt_no_page);
        stamp_handle = -1;
        return 1;
    }
    if (opt_log_name == 0) {
        (void)report_open(0, opt_serial, !opt_no_page);
        return 1;
    }
    if (report_open(opt_log_name, opt_serial, !opt_no_page))
        return 1;
    printf("warning: could not open %s (read-only media?), continuing\n",
           opt_log_name);
    opt_log_name = 0;   /* nothing was written: do not claim a copy */
    (void)report_open(0, opt_serial, !opt_no_page);
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
    if (outcome == QUICK_CAVEATS)
        return "LOOKS QUALIFIED WITH CAVEATS";
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
    print_stamp_header();
    /* Task 36.48: two lines on the screen, the full block in the log and
     * on the serial line. Reads only, like everything in this pass. */
    if (opt_no_sysinfo)
        sysinfo_skip(&sysinfo);
    else
        sysinfo_probe(&sysinfo);
    report_sysinfo_short(&sysinfo, qconprintf);
    report_sysinfo(&sysinfo, qlogprintf);

    n = pci_scan_usb(pcis, MAX_CONTROLLERS, HC_MASK_ALL);
    if (n == 0) {
        /*
         * No USB host controller of any family at all.
         */
        qprintf("\nNo USB host controller found on this machine.\n");
        qprintf("DISQUALIFIED: with no xHCI function there is nothing for\n"
                "  xhci98.sys to bind to.\n");
        qprintf("Done.\n");
        return 1;
    }

    /*
     * **AND NO xHCI FUNCTION IS THE SAME ANSWER**, which it was not until the
     * 2026-09-07 audit's I1.
     *
     * The comment above has always said what this test is for - "no CC_0C0330
     * function ... for this project's purpose it is the decisive one" - and
     * the code tested something else: `pci_scan_usb` with `HC_MASK_ALL`
     * answers nonzero for an EHCI-only or OHCI-only machine, so a laptop with
     * two EHCI controllers and no xHCI reached the loop below, found nothing
     * disqualifying about controllers this driver will never bind to, and
     * printed LOOKS QUALIFIED with an invitation to run the active poll.
     *
     * That is the tool's headline question answered backwards, on the one
     * kind of machine a user is most likely to be asking it about - and
     * `xhciqual/README.md` and the shipped readme both promise the opposite.
     * The other families are still scanned and still reported, because a
     * machine's other controllers are part of the picture; what they may not
     * do is carry the verdict.
     */
    {
        int xhci_found = 0;
        for (i = 0; i < n; i++) {
            if (pcis[i].hctype == HC_XHCI)
                xhci_found = 1;
        }
        if (!xhci_found) {
            qprintf("Found %d USB host controller(s), none of them xHCI.\n\n", n);
            qprintf("DISQUALIFIED: this machine has no xHCI (PCI class 0C0330)\n"
                    "  function, so there is nothing for xhci98.sys to bind to.\n");
            qprintf("  The controllers it does have already have drivers on\n"
                    "  both target systems; this driver is for machines that\n"
                    "  have xHCI and nothing else.\n");
            qprintf("Done.\n");
            return 1;
        }
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
            route_read(p, &c->route);
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
             * disqualifier that does not apply to it.
             *
             * **So the "one classifier, so the quick scan and the full run
             * cannot disagree" claim holds for xHCI only** - the 2026-09-07
             * audit's I5. On a legacy controller this hands the classifier a
             * port count it did not measure, while a full run disqualifies a
             * legacy controller that really reports zero root ports. The
             * classifier is still one function; what differs is what is fed
             * to it. Since audit I1 the quick scan disqualifies a machine
             * with no xHCI outright, so a legacy controller no longer carries
             * a verdict at all and the divergence is unreachable from here -
             * but the input is still a substitute rather than a reading, and
             * a future caller that gives the legacy families a verdict again
             * has to count their ports first. `xhciqual/README.md` states the
             * qualification. */
            usb2_ports = 1;
            *p = lc->pci;
        }

        outcome = quick_classify(p, mmio_ok, usb2_ports, 0);
        if (quick_rank(outcome) > quick_rank(worst))
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
        /* Roadmap task 35.1: the speed tables (PSI entries decoded, the
         * issue 11 warning) and the raw extended-capability chain, in the log
         * and on the serial line only - the screen keeps its one line per
         * controller. ctrls[0] still holds this controller's walk. */
        if (p->hctype == HC_XHCI && mmio_ok) {
            report_protocols(&ctrls[0], qlogprintf);
            report_xcap_dump(&ctrls[0], qlogprintf);
        }
        /* Task 36.11: the routing block in the log and on the serial line;
         * the screen gets one phrase, and only when a GSI was asked about
         * and the controller's own line is on it. */
        if (p->hctype == HC_XHCI) {
            report_route(&ctrls[0].route, opt_gsi, qlogprintf);
            if (opt_gsi >= 0 && hidden == 0)
                qprintf("     %s\n", route_summary(&ctrls[0].route, opt_gsi));
        }
    }

    if (hidden > 0) {
        /* The screen reports a result, not the next command: the owner's
         * decision of 2026-09-16. */
        if (report_logging())
            qprintf("%d more controller(s) - the full list is in the log.\n",
                    hidden);
        else
            qprintf("%d more controller(s) not shown.\n", hidden);
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
     * The verdict alone, with no next command after it: the owner's decision of
     * 2026-09-16, because whoever runs the tool knows what to run next and
     * `--help` lists the sequence.
     */
    if (worst == QUICK_DISQUALIFIED) {
        qprintf("Verdict: DISQUALIFIED - at least one controller cannot "
                "work on either target.\n");
    } else if (worst == QUICK_CANNOT_SAY) {
        qprintf("Verdict: CANNOT SAY - a state this read-only pass may not "
                "change is in the way.\n");
    } else if (worst == QUICK_CAVEATS) {
        /* Task 36.10: a controller with no line interrupt is not refused;
         * the driver polls it. The active run says the rest. */
        qprintf("Verdict: LOOKS QUALIFIED WITH CAVEATS, subject to the "
                "active tests (C2/C3/C4):\n"
                "  no line interrupt, so xhci98.sys polls (or an "
                "MSI-capable OS).\n");
    } else {
        qprintf("Verdict: LOOKS QUALIFIED, subject to the active tests "
                "(C2/C3/C4), which this\n"
                "  read-only pass cannot observe.\n");
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
    int n, i, nx, nl, all_qualified, any_caveats, active;
    const char *mode;

    prescan_done_flag(argc, argv);
    reset_done_flag();
    /* Task 11-V.8: no arguments at all means the read-only quick scan. Set
     * before parse_args so a `--full` inside the loop can clear it, which is
     * unreachable at argc == 1 and is the belt to that brace. */
    opt_quick = (argc == 1);
    if (!parse_args(argc, argv))
        return finish_normally(2);
    /* Task 38.1: the name, or the refusal, before anything is read. */
    if (opt_log_stamp && !resolve_log_stamp())
        return finish_normally(4);
    if (opt_help) {
        if (!open_report())
            return finish_normally(4);
        print_help();
        print_stamp_tail();
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
        if (!open_report())
            return finish_normally(4);
        rc = run_quick_scan();
        if (opt_log_stamp)
            print_stamp_tail();
        else if (opt_log_name != 0)
            qprintf("Report copied to %s.\n", opt_log_name);
        report_close();
        return finish_normally(rc);
    }

    if (!open_report())
        return finish_normally(4);

    qprintf("XHCIQUAL %s (build %s) - USB host controller qualification\n",
            TOOL_VERSION, TOOL_BUILD);
    print_stamp_header();
    /* Task 36.48: which machine this report came from. Reads only (the
     * BIOS area, CPUID, E820, DOS), before any controller is touched. */
    if (opt_no_sysinfo)
        sysinfo_skip(&sysinfo);
    else
        sysinfo_probe(&sysinfo);
    report_sysinfo(&sysinfo, qprintf);
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
        print_stamp_tail();
        report_close();
        return finish_normally(2);
    }
    qprintf("Found %d selected USB host controller(s).\n", n);

    all_qualified = 1;
    any_caveats = 0;
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
            /* Before anything active: reads only (task 36.11). */
            route_read(&c->pci, &c->route);
            c->route_gsi = opt_gsi;
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
                        } else if (c4_lets_ports(c->v_irq, opt_poll_only)) {
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
            {
                int v = final_verdict(c, active);

                if (v == VERDICT_NOT)
                    all_qualified = 0;
                else if (v == VERDICT_CAVEATS)
                    any_caveats = 1;
            }
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

    if (opt_log_stamp) {
        qprintf("\nDone.\n");
        print_stamp_tail();
    } else if (opt_log_name != 0) {
        qprintf("\nDone. Report copied to %s.\n", opt_log_name);
    } else {
        qprintf("\nDone.\n");
    }
    report_close();

    return finish_normally(run_exit_code(!all_qualified, any_caveats));
}
