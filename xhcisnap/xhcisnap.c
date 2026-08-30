/*
 * xhcisnap.c - read xhci98.sys's miniport extension and raw PORTSC array out
 * of a running Windows 98 or Windows 2000 machine, from user mode.
 *
 * WHY THIS EXISTS, and since task 13-L.2 it is not a bench tool. On Windows 98
 * the driver records everything a maintainer needs - the counters and the note
 * ring, both of which live inside XHCI_EXTENSION - and none of it can get out
 * any other way. That is structural rather than unlucky: Windows 98 has exactly
 * two logging families, and Option A locks this driver out of both.
 * USBPORT_RegisterUSBPortDriver takes over IRP_MJ_CREATE, CLOSE and
 * DEVICE_CONTROL on the miniport's driver object, so other Windows 98 drivers
 * can log because they own a driver object and this one runs inside somebody
 * else's; and the ring-0 file sink was retired by task 13-L.2 after never
 * having written a byte outside a virtual machine.
 *
 * What was missing was a way to READ, not a way to record - and the route does
 * not need a driver object of our own, because the port driver we live inside
 * already published one and will forward through it. This is the read side; the
 * kernel side is xhciPassThru in src/xhci_dispatch.c, **in every shipping
 * flavour** and gated by a registry value rather than a build define. See
 * docs/contributing/design/08-build-flavours-and-the-log-channel.md section 13.
 *
 * THIS TOOL IS ALSO HOW THE VALUE GETS SET. `XHCISNAP -verbosity N` writes it
 * from ring 3, finds the driver's per-machine software key itself, and means the
 * published sequence never mentions regedit: set the level, restart, reproduce,
 * XHCISNAP -o C:\NAME. It is the only knob: since the snapshot-value merge there is
 * one value, so a second flag that set it to a level the user had not named
 * would be a tool making the machine's policy decisions for it.
 *
 * AND IT WRITES THE LOG FILE. The ring-0 file sink did not disappear so much as
 * move to ring 3, which is the right place for it and answers every objection
 * task 11-V.7 measured: a user-mode program on Windows 98 writes a file the way
 * any program does, with no ZwCreateFile on a boot path, no path-root probe, no
 * interlock and no import.
 *
 * THE ROUTE, read out of both shipping usbport builds and
 * recorded in docs/usb-xhci-info/usbport-miniport-abi.md under "Debug /
 * single-packet": usbport's vendor escape is IOCTL_USB_USER_REQUEST
 * (0x00220438, METHOD_BUFFERED) with UsbUserRequest = 3, reached through the
 * \DosDevices\HCD<n> symbolic link the HCD FDO's start path always creates.
 * It is not gated: IOCTL_USB_DIAGNOSTIC_MODE_ON is NOT a prerequisite.
 *
 * WINDOWING. usbport refuses ParameterLength > 0x10000 before the miniport is
 * ever reached, and the extension is larger than that (90,272 bytes as this is
 * written), so a dump is several windows and this tool concatenates them. The
 * cost is that the driver may run between windows, so every window carries a
 * tear detector and this tool reports whether they all agreed. A dump whose
 * tear detectors differ is not wrong, but any counter in it may be a mixture,
 * and that has to be said out loud rather than discovered later.
 *
 * TARGETS. Win32 console, built with the in-repo MSVC 6.0 (build.cmd). It must
 * run on Windows 98 SE, so: ANSI only, no API newer than Win95, no C99. The
 * C89 rules are kept by hand rather than by /Za, because these SDK headers do
 * not compile under /Za - see build.cmd.
 *
 * The wire format is duplicated here rather than included from src/xhci.h,
 * which is a kernel header. The duplication is checked at run time instead of
 * at compile time - see the HeaderBytes and Signature checks in take_window(),
 * which refuse a driver whose header is not the one this build knows. **That
 * refusal is a good failure and a wasted download**, which is why
 * make-release.ps1 carries the same staleness throw for this tool that it
 * carries for the DOS qualifier.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

/* ---- usbport's USBUSER escape (from the binaries, not from a header) ---- */

#define IOCTL_USB_USER_REQUEST      0x00220438UL
#define USBUSER_PASS_THRU           3UL

/* Offsets within the IOCTL buffer. */
#define UU_REQUEST                  0x00    /* ULONG UsbUserRequest        */
#define UU_STATUS                   0x04    /* ULONG UsbUserStatusCode     */
#define UU_REQUEST_BYTES            0x08    /* ULONG RequestBufferLength   */
#define UU_ACTUAL_BYTES             0x0C    /* ULONG ActualBufferLength    */
#define UU_GUID                     0x10    /* 16 bytes                    */
#define UU_PARAM_BYTES              0x20    /* ULONG ParameterLength       */
#define UU_PARAMETERS               0x24    /* the miniport's block        */

/* usbport's own error codes, in the order the binaries use them. */
/*
 * ---- output width -------------------------------------------------------
 *
 * **The console this tool is read on is 80 columns wide and has no scrollback
 * worth the name**, and until the snapshot-value merge nothing here knew that. The messages
 * are composed at run time out of C89 adjacent string literals, so a message
 * that is three tidy source lines prints as one 163-column sentence and the DOS
 * box breaks it mid-word - `either the c` / `hannel`. **The defect is invisible
 * in a diff**: no literal in this file is over 79 characters, the longest being
 * a registry path, so four review rounds passed over it and the project owner
 * found it in ten seconds on a guest. That is this file's own rule - *re-read
 * the usage text on a real invocation rather than trusting the diff* - earning
 * itself a second time.
 *
 * Everything prose goes through `say()`. It is deliberately ONE emitter rather
 * than seventy repaired call sites, because the wrap is a property of
 * composition: fixing the observed lines by hand leaves the next message added
 * to arrive broken again.
 *
 * **A word is never split.** A line that is one long word - a registry path, a
 * key name - overflows instead, because a path broken across two lines cannot be
 * typed back in or grepped for, and being able to do that is the whole reason it
 * is on the screen.
 */
/*
 * **The SEVENTH copy of this project's version number, and it is only safe
 * because `make-release.ps1` checks it.** *(Project owner:
 * "xhcisnap should show version and build date and time in the default
 * screen.")* The others are `src\xhci98.inf`'s `DriverVer`, four fields in
 * `src\xhci98.rc`, `xhciqual\qual.h`'s `TOOL_VERSION`, and the opening line of
 * `docs/using/release-notes.md`. A cut moves all of them together; this project
 * has shipped a stale copy before, which is why the release script throws on the
 * qualifier's rather than trusting anyone to remember - **this one is wired into
 * the same check in the same change, because a version copy no gate reads is a
 * version copy that will be wrong.**
 *
 * `__DATE__`/`__TIME__` are the build stamp and they say something the version
 * cannot: **which build of an unreleased version this is.** Between cuts the
 * version does not move, and this tool is rebuilt many times a day - the whole
 * reason the stamp is wanted is to tell a guest's stale copy from the one just
 * staged, which is a trap this batch hit twice in one afternoon.
 *
 * **The version itself is no longer written here** (task 14.1.10): it expands
 * to `src\xhci_version.h`'s, the one place the number is edited, reached by a
 * path relative to this file. The build stamp below stays a literal of the
 * compiler's, because it is the fact the version cannot carry.
 */
#include "../src/xhci_version.h"

#define XHCISNAP_VERSION XHCI_VER_STR
#define XHCISNAP_BUILT   __DATE__ " " __TIME__

#define XHCISNAP_COLS 79

/*
 * `hang` is what continuation lines are indented by, so a numbered item's
 * second line sits under its text rather than under its number. Pass the same
 * string twice for a plain block.
 */
static void put_wrapped_to(FILE *dest, const char *indent, const char *hang,
                           const char *text)
{
    const char *lead;
    const char *p;

    lead = indent;
    p = text;
    for (;;) {
        const char *hard;
        const char *brk;
        size_t width;

        /* An embedded newline is a break the caller asked for; it is honoured
         * wherever it falls and the wrapping restarts after it. */
        hard = p;
        while (*hard != '\0' && *hard != '\n') {
            hard++;
        }

        for (;;) {
            /*
             * A pathological indent must not produce a one-character column;
             * below this floor the text simply overflows, which stays readable
             * where a column of single letters would not.
             */
            width = (strlen(lead) + 24 < XHCISNAP_COLS)
                        ? (XHCISNAP_COLS - strlen(lead)) : 24;
            if ((size_t)(hard - p) <= width) {
                break;
            }
            brk = p + width;
            while (brk > p && *brk != ' ') {
                brk--;
            }
            if (brk == p) {
                brk = p;
                while (*brk != '\0' && *brk != ' ' && *brk != '\n') {
                    brk++;
                }
            }
            fprintf(dest, "%s%.*s\n", lead, (int)(brk - p), p);
            lead = hang;
            p = brk;
            while (*p == ' ') {
                p++;
            }
        }
        fprintf(dest, "%s%.*s\n", lead, (int)(hard - p), p);
        lead = hang;
        if (*hard == '\0') {
            break;
        }
        p = hard + 1;
    }
}

/*
 * `_vsnprintf` rather than `vsprintf`, and the terminator is written by hand
 * because MSVC 6's `_vsnprintf` does not write one when it truncates. An
 * unbounded `sprintf` in this file was a real defect once (`88e0b65`).
 */
static void say(const char *indent, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';

    put_wrapped_to(stdout, indent, indent, buf);
}

/* The same, with a hanging indent - for a numbered item or a labelled line
 * whose continuation should sit under the text and not under the label. */
static void say_hang(const char *indent, const char *hang, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';

    put_wrapped_to(stdout, indent, hang, buf);
}

static const char *uu_status_text(unsigned long code)
{
    switch (code) {
    case 0:  return "success";
    case 1:  return "not supported";
    case 2:  return "invalid request code";
    case 3:  return "feature disabled";
    case 4:  return "invalid header parameter";
    case 5:  return "invalid parameter";
    /*
     * **This is two situations wearing one sentence, and since task 13-L.2 the
     * second is the ORDINARY one on every machine.** A miniport that declines
     * returns exactly 6 whether it has no snapshot support at all or has it and
     * has not been switched on - which is deliberate: a switched-off channel
     * must answer a caller exactly as a binary built without one would, and 6
     * is the only honest nonzero value at that slot (usbport's own root-hub
     * probe retries only on exactly 6). The two are told apart by reading the
     * installed file, never by asking the driver, so the tool names both and
     * says what to do about the second.
     *
     * **The naming moved OUT of this string and it did not get
     * lost.** This text is printed in a status COLUMN, and the sentence made
     * that one row 163 characters wide - it wrapped twice on an 80-column
     * console and shifted every row after it, which is what made four aligned
     * controls read as four ragged paragraphs. Both callers now say it in prose
     * where there is room for it: `probe_route` already enumerated the two
     * situations underneath, and `take_window` gained the same paragraph, which
     * is where a user who ran a DUMP against a shut channel actually reads it.
     * **Do not put an explanation back into a column.**
     */
    case 6:  return "MINIPORT DECLINED";
    case 7:  return "buffer too small";
    default: return "unknown";
    }
}

/* ---- the snapshot wire format (must match src/xhci.h) ------------------ */

/* {34F57942-2D69-4066-9CBD-82148E44BC20} as the four little-endian ULONGs the
 * driver compares. Written this way on purpose: the driver does the same, so
 * the two sides can be diffed by eye. */
static const unsigned long snap_guid[4] = {
    0x34F57942UL, 0x40662D69UL, 0x1482BD9CUL, 0x20BC448EUL
};

#define SNAP_REQUEST_SIGNATURE      0x514E5358UL    /* 'X','S','N','Q' */
#define SNAP_SIGNATURE              0x504E5358UL    /* 'X','S','N','P' */
/*
 * **Schema 3** (task 13-L.2 as amended). Schema 1 was the probe-build
 * instrument's, whose header stopped at BuildFlags; schema 2 added the block
 * below; schema 3 is that block minus `SwitchStatusSnapshot`, which left with
 * the registry value it reported. The refusal on a mismatch is deliberate and is
 * the whole reason the number exists: a dump decoded against the wrong shape is
 * a WRONG reading, not a failed one - and a shrinking header is exactly as much
 * of a decode hazard as a growing one.
 */
#define SNAP_SCHEMA                 3UL

#define SNAP_REGION_EXTENSION       0UL
#define SNAP_REGION_PORTSC          1UL

/*
 * The verbosity ladder, as the driver's `XHCI_LOG_VERBOSITY_*` spells it. This
 * tool duplicates the driver's constants rather than including its headers -
 * the same trade the wire format above is written under - so the two are kept
 * honest by `make-release.ps1`'s schema comparison and by a rebuild of both.
 *
 * **Rung 0 is off outright**: the driver's PassThru slot answers exactly
 * `MP_STATUS_NOT_SUPPORTED`, so at this level there is nothing for this tool to
 * read. Rung 1 engages the channel with the note ring still off, which is the
 * cheapest reading there is and is why the ladder has five rungs rather than
 * four. *(There were four, and a second registry value `XhciLogSnapshot`
 * carrying rung 0's job, until the snapshot-value merge.)*
 */
#define XHCISNAP_LEVEL_OFF          0UL
#define XHCISNAP_LEVEL_COUNTERS     1UL
#define XHCISNAP_LEVEL_LOG          2UL
#define XHCISNAP_LEVEL_PORTSC       3UL
#define XHCISNAP_LEVEL_FULL         4UL
#define XHCISNAP_LEVEL_MAX          4UL

#define SNAP_S_TRUNCATED            0x00000001UL
#define SNAP_S_BAD_REQUEST          0x00000002UL
#define SNAP_S_BAD_EXTENSION        0x00000004UL
#define SNAP_S_BAD_REGION           0x00000008UL
#define SNAP_S_PAST_END             0x00000010UL
#define SNAP_S_NO_MMIO              0x00000020UL

#define SNAP_B_DEBUG                0x00000001UL
#define SNAP_B_DIAGNOSTIC           0x00000002UL

/* Which of the three build flavours produced a window. BuildFlags cannot say:
 * SNAP_B_DEBUG is #if DBG, which is set for BOTH checked builds - and those
 * are exactly the two that must not be confused, since only one of them is
 * ever published. */
#define SNAP_FLAVOUR_UNKNOWN        0UL
#define SNAP_FLAVOUR_RELEASE        1UL
#define SNAP_FLAVOUR_DEBUG          2UL
#define SNAP_FLAVOUR_QEMU           3UL
#define SNAP_FLAVOUR_HOSTTEST       4UL

static const char *flavour_text(unsigned long f)
{
    switch (f) {
    case SNAP_FLAVOUR_RELEASE:  return "release";
    case SNAP_FLAVOUR_DEBUG:    return "debug";
    case SNAP_FLAVOUR_QEMU:     return "qemu (NEVER PUBLISHED)";
    case SNAP_FLAVOUR_HOSTTEST: return "host test build";
    default:                    return "unknown";
    }
}

typedef struct _SNAP_HEADER {
    unsigned long Signature;
    unsigned long SchemaVersion;
    unsigned long HeaderBytes;
    unsigned long Status;
    unsigned long Region;
    unsigned long Offset;
    unsigned long RegionBytes;
    unsigned long PayloadBytes;
    unsigned long ExtensionBytes;
    unsigned long PortCount;
    unsigned long TearDetector;
    unsigned long BuildFlags;
    /*
     * ---- schema 2, amended to schema 3 -------------------
     *
     * Everything below is what this tool can print with **no offset table**,
     * and that is the whole reason it is on the wire. The `.BIN` decodes only
     * against an `offsets.txt` regenerated from the driver's own tree - which
     * the maintainer has and the user does not - so it is the right thing to
     * attach and the wrong thing to be the only output. These fields are what
     * the plain-text companion is built out of.
     *
     * *(`SwitchStatusSnapshot` stood between `VerbosityApplied` and
     * `SwitchStatusVerbosity` in schema 2. It left with `XhciLogSnapshot`, and
     * its departure is the whole of what separates schema 3 from schema 2.)*
     */
    unsigned long Flavour;
    unsigned long VerbosityRead;
    unsigned long VerbosityApplied;
    unsigned long SwitchStatusVerbosity;
    unsigned long SwitchStatusDebugView;
    unsigned long SwitchRead;
    unsigned long RingOffset;
    unsigned long RingBytes;
    unsigned long RingHead;
    unsigned long RingUsed;
} SNAP_HEADER;

/*
 * One window's parameter block. 0xF000 is comfortably under usbport's 0x10000
 * refusal and leaves the whole IOCTL buffer well under 64 KB, which keeps this
 * a single allocation on a machine with 64 MB of RAM.
 */
#define SNAP_PARAM_BYTES            0xF000UL
#define SNAP_BUFFER_BYTES           (UU_PARAMETERS + SNAP_PARAM_BYTES)

static unsigned char snap_buffer[SNAP_BUFFER_BYTES];

static void put32(unsigned char *p, unsigned long offset, unsigned long value)
{
    *(unsigned long *)(p + offset) = value;
}

static unsigned long get32(const unsigned char *p, unsigned long offset)
{
    return *(const unsigned long *)(p + offset);
}

/*
 * Take one window. Returns 1 on success with *header and *payload filled in,
 * 0 on a failure it has already explained.
 *
 * Everything usbport enforces before the miniport is reached is satisfied
 * here, and each line says which rule it is paying:
 *   - the input and output lengths must be EQUAL,
 *   - RequestBufferLength must equal that same length,
 *   - the whole buffer must be at least 0x28 bytes,
 *   - ParameterLength must be <= 0x10000.
 */
static int take_window(HANDLE device, unsigned long region,
                       unsigned long offset, SNAP_HEADER *header,
                       const unsigned char **payload)
{
    DWORD returned;
    unsigned char *block;
    unsigned long status;

    memset(snap_buffer, 0, sizeof(snap_buffer));
    put32(snap_buffer, UU_REQUEST, USBUSER_PASS_THRU);
    put32(snap_buffer, UU_STATUS, 0);
    put32(snap_buffer, UU_REQUEST_BYTES, SNAP_BUFFER_BYTES);
    put32(snap_buffer, UU_ACTUAL_BYTES, 0);
    put32(snap_buffer, UU_GUID + 0, snap_guid[0]);
    put32(snap_buffer, UU_GUID + 4, snap_guid[1]);
    put32(snap_buffer, UU_GUID + 8, snap_guid[2]);
    put32(snap_buffer, UU_GUID + 12, snap_guid[3]);
    put32(snap_buffer, UU_PARAM_BYTES, SNAP_PARAM_BYTES);

    block = snap_buffer + UU_PARAMETERS;
    put32(block, 0, SNAP_REQUEST_SIGNATURE);
    put32(block, 4, region);
    put32(block, 8, offset);

    returned = 0;
    if (!DeviceIoControl(device, IOCTL_USB_USER_REQUEST,
                         snap_buffer, SNAP_BUFFER_BYTES,
                         snap_buffer, SNAP_BUFFER_BYTES,
                         &returned, NULL)) {
        printf("  DeviceIoControl failed, error %lu\n",
               (unsigned long)GetLastError());
        return 0;
    }

    /*
     * usbport returns STATUS_SUCCESS for its own refusals too and reports them
     * only in this field, so the IOCTL succeeding says nothing on its own.
     */
    status = get32(snap_buffer, UU_STATUS);
    if (status != 0) {
        printf("  usbport refused the request: %lu (%s)\n",
               status, uu_status_text(status));
        /*
         * The advice that used to travel inside `uu_status_text`'s code-6
         * string, said here in prose instead - see the note there. This is the
         * path a user who typed a DUMP command against a switched-off channel
         * actually lands on, and it is the one that can be fixed from here.
         */
        if (status == 6) {
            say("  ", "Either this driver's channel is switched off - the "
                      "default on every machine - or this is not an xhci98 "
                      "controller. A switched-off channel answers exactly as a "
                      "binary built without one, so this tool cannot tell you "
                      "which. If it is yours: XHCISNAP -verbosity 2, then "
                      "RESTART, reproduce, and dump again.");
        }
        return 0;
    }

    memcpy(header, block, sizeof(SNAP_HEADER));
    if (header->Signature != SNAP_SIGNATURE) {
        printf("  the reply carries no snapshot signature (%08lX) - the "
               "driver answered the GUID but not with a snapshot\n",
               header->Signature);
        return 0;
    }
    if (header->HeaderBytes != (unsigned long)sizeof(SNAP_HEADER) ||
        header->SchemaVersion != SNAP_SCHEMA) {
        printf("  schema mismatch: driver says version %lu / %lu-byte header, "
               "this tool knows version %lu / %u. Rebuild the tool from the "
               "same tree as the driver.\n",
               header->SchemaVersion, header->HeaderBytes,
               SNAP_SCHEMA, (unsigned)sizeof(SNAP_HEADER));
        return 0;
    }
    *payload = block + sizeof(SNAP_HEADER);
    return 1;
}

/*
 * Walk one region, writing every window's payload to `path` in order and, if
 * `sink` is not NULL, keeping the first `sinkBytes` of it in memory as well -
 * the PORTSC array is wanted on screen, not only on disk. Returns the number
 * of bytes written, or (unsigned long)-1 on a failure it has already
 * explained. `tearFirst`/`tearLast`/`tearTorn` accumulate the coherence check.
 *
 * **`tearLast` was added because the report was claiming a change
 * and then printing only the value it started at.** A torn dump said "CHANGED
 * between windows" beside one number, so a single idle tick of usbport's health
 * timer and a thousand-DPC storm mid-dump printed the identical sentence, and
 * the reader could not tell a dump worth retaking from one worth keeping. The
 * detector is monotonic, so the pair is also a magnitude.
 */
/*
 * **A failed dump must not leave a file that looks like a dump.** Until
 * a later review each region was written straight to its final name, opened with
 * CREATE_ALWAYS before the first window was asked for - so `-c` naming the
 * wrong HCD, or a channel that was never switched on, truncated the previous
 * `.BIN` to nothing and walked away, with last week's `.PSC` and `.TXT` still
 * beside it looking like a set. Now a region is written to `<final>.TMP`, a
 * region that fails is deleted, and only after BOTH regions are in hand are
 * the two renamed over their final names (`publish_region`), with the old
 * `.TXT` retired at the same moment. The window that is left is the rename
 * pair itself: if the second rename fails, `main` deletes both finals rather
 * than leave a new `.BIN` beside an old `.PSC`. (Codex round 15.)
 */
static unsigned long abandon_region(HANDLE file, const char *path)
{
    CloseHandle(file);
    DeleteFileA(path);
    return (unsigned long)-1;
}

static int publish_region(const char *tmp, const char *final)
{
    /* MoveFileA, not MoveFileEx: the latter does not exist on Windows 98, and
     * MoveFileA refuses to replace, so the old file goes first. A missing old
     * file makes DeleteFileA fail, which is the expected case on a first run
     * and is not checked. */
    DeleteFileA(final);
    if (!MoveFileA(tmp, final)) {
        printf("  cannot rename %s to %s, error %lu\n", tmp, final,
               (unsigned long)GetLastError());
        return 0;
    }
    return 1;
}

/*
 * **One strict number parser for every numeric argument.** `strtoul` with a
 * leading digit required, the whole argument consumed and no ERANGE; `atoi`
 * accepts anything and says nothing, which is why `-verbosity full` once
 * switched the channel OFF (fixed) and why `-c junk` opened HCD0
 * and `-c 1oops` opened HCD1 until the post-Phase 13 review rounds - on a two-controller machine
 * the second of those dumps the wrong controller while echoing the command
 * back as if it had been honoured.
 */
static int parse_ulong(const char *arg, unsigned long *value)
{
    char *end = NULL;

    if (arg[0] < '0' || arg[0] > '9') {
        return 0;
    }
    errno = 0;
    *value = strtoul(arg, &end, 10);
    if (end == NULL || *end != '\0' || errno == ERANGE) {
        return 0;
    }
    return 1;
}

static unsigned long dump_region(HANDLE device, unsigned long region,
                                 const char *path, SNAP_HEADER *last,
                                 unsigned long *tearFirst,
                                 unsigned long *tearLast, int *tearTorn,
                                 int *tearSeen,
                                 unsigned char *sink, unsigned long sinkBytes)
{
    unsigned long keep;
    SNAP_HEADER header;
    const unsigned char *payload;
    HANDLE file;
    DWORD written;
    unsigned long offset;
    unsigned long total;
    unsigned long windows;

    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        printf("  cannot create %s, error %lu\n", path,
               (unsigned long)GetLastError());
        return (unsigned long)-1;
    }

    offset = 0;
    total = 0;
    windows = 0;
    for (;;) {
        if (!take_window(device, region, offset, &header, &payload)) {
            return abandon_region(file, path);
        }
        windows++;
        *last = header;

        if (!*tearSeen) {
            *tearFirst = header.TearDetector;
            *tearSeen = 1;
        } else if (header.TearDetector != *tearFirst) {
            *tearTorn = 1;
        }
        *tearLast = header.TearDetector;

        if ((header.Status & SNAP_S_BAD_EXTENSION) != 0) {
            printf("  the driver does not recognise its own extension - "
                   "nothing to read\n");
            return abandon_region(file, path);
        }
        if ((header.Status & SNAP_S_BAD_REGION) != 0) {
            printf("  the driver does not know region %lu\n", region);
            return abandon_region(file, path);
        }
        if ((header.Status & SNAP_S_BAD_REQUEST) != 0) {
            printf("  the driver refused the request block\n");
            return abandon_region(file, path);
        }
        if ((header.Status & SNAP_S_NO_MMIO) != 0) {
            printf("  the controller has no usable register mapping, so "
                   "PORTSC was not read\n");
            CloseHandle(file);
            return 0;
        }
        if ((header.Status & SNAP_S_PAST_END) != 0) {
            break;
        }

        /* The driver bounds PayloadBytes to the parameter block; a reply
         * that claims more would send WriteFile past the buffer. Refuse it
         * like every other malformed reply rather than trust the header. */
        if (header.PayloadBytes > SNAP_PARAM_BYTES - sizeof(SNAP_HEADER)) {
            printf("  the driver claims %lu payload bytes in a %lu-byte "
                   "window - refusing the reply\n", header.PayloadBytes,
                   (unsigned long)(SNAP_PARAM_BYTES - sizeof(SNAP_HEADER)));
            return abandon_region(file, path);
        }

        if (header.PayloadBytes != 0) {
            written = 0;
            if (!WriteFile(file, payload, header.PayloadBytes, &written, NULL)
                || written != header.PayloadBytes) {
                printf("  short write to %s, error %lu\n", path,
                       (unsigned long)GetLastError());
                return abandon_region(file, path);
            }
            if (sink != NULL && total < sinkBytes) {
                keep = sinkBytes - total;
                if (keep > header.PayloadBytes) {
                    keep = header.PayloadBytes;
                }
                memcpy(sink + total, payload, keep);
            }
            total += header.PayloadBytes;
            offset += header.PayloadBytes;
        }

        if ((header.Status & SNAP_S_TRUNCATED) == 0) {
            break;
        }
        if (header.PayloadBytes == 0) {
            /* Truncated with nothing copied means the window cannot advance;
             * refuse to spin rather than loop forever on a driver that is
             * behaving unexpectedly. */
            printf("  the driver reports more to come but copied nothing - "
                   "giving up rather than looping\n");
            return abandon_region(file, path);
        }
    }

    CloseHandle(file);
    printf("  %s: %lu bytes in %lu window(s) (region is %lu)\n",
           path, total, windows, last->RegionBytes);
    return total;
}

/* ---- the plain-text companion, which is what a stranger can attach ------ */

/*
 * **The `.BIN` is the wrong thing to be the only output.** It is raw extension
 * bytes and decodes only against an `offsets.txt` regenerated from the driver's
 * own tree - which the maintainer has and the user does not. So it stays the
 * attachment a maintainer decodes, and this file is what a stranger pastes into
 * an issue.
 *
 * **Everything here comes off the wire, with no offset table.** That is what
 * schema 2's header block is for. What it deliberately does NOT contain is the
 * extension's own counter set: putting that on the wire would need a gather
 * table inside the driver duplicating `offsets.txt` - a second copy of the
 * layout, in the binary, drifting - and the design record rejected it.
 *
 * **The verbosity tier decides what goes in**, and that is a *publication* line
 * rather than a transport one: the driver serves both its regions whole at
 * every level once the channel is engaged, and what changes here is what a
 * maintainer may reasonably ask a stranger to paste in public.
 *
 *   always      the header block: build, tier, switch statuses, ring fill
 *   >= 2        the note ring's text
 *   >= 3        the PORTSC table
 *   >= 4        kernel addresses, because the ring may hold them at that level
 *
 * **The address rung is the reason a level-4 .TXT is not automatically safe to
 * paste in public.** XhciLogAppendAddress refuses an address record below
 * XHCI_LOG_VERBOSITY_FULL and admits it at 4, the ring is what this file
 * prints, so at 4 the companion carries whatever addresses the driver logged.
 * Levels 1-3 carry none and the DRIVER enforces that rather than this tool
 * promising it. (This block said "never a kernel address, at any level" until
 * a later review, which contradicted the driver's own gate and the release notes'
 * level-4 row.)
 *
 * *(Each of those thresholds was one lower until the snapshot-value merge, when
 * `XhciLogSnapshot` merged into the ladder and every rung shifted by one. There
 * is no `>= 0` case any more, because level 0 does not answer at all.)*
 *
 * It also retires an operating trap: the PORTSC table used to scroll off a DOS
 * box on real silicon and had to be redirected by hand.
 */

static FILE *companion;

/* The companion file is what a stranger pastes into a public issue, so it is
 * wrapped for the same reason the console is - a 230-column line in a GitHub
 * comment is read through a horizontal scrollbar. Data rows (the note ring, the
 * PORTSC table) do NOT come through here: wrapping a record would corrupt it. */
static void comp_wrapped(const char *indent, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';

    /* File when there is one, screen when there is not - see `comp`. */
    put_wrapped_to((companion != NULL) ? companion : stdout,
                   indent, indent, buf);
}

/*
 * **The report goes to the FILE, and to the screen only when there is no file.**
 * *(Project owner: "when xhcisnap is run without command line
 * arguments, I see a wall of text.")* It went to both until then, which put the
 * whole companion - header, tier notes, the note ring's entire text, the
 * coherence paragraph - through a 25-row console with no scrollback. **A
 * 981-byte ring printed to a console that cannot scroll is not output, it is
 * noise that pushes the readable part off the top**, and it was being written to
 * `.TXT` at the same time, on the same machine, where it can be read with TYPE
 * or MORE.
 *
 * What the screen keeps is what the screen is good for: the progress lines, the
 * PORTSC decode (which the bench reads on the spot, and which was never in this
 * path - it is `printf` and enters the file only at level 3), and the closing
 * summary.
 *
 * **The fallback is what makes this safe, and it also makes an existing message
 * true**: when the `.TXT` cannot be created the tool already says "the report
 * will be on screen only", which was a false statement while the report went to
 * the screen unconditionally. Now it is a description.
 */
static void comp(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (companion != NULL) {
        vfprintf(companion, fmt, ap);
    } else {
        vprintf(fmt, ap);
    }
    va_end(ap);
}

static const char *mpstatus_text(unsigned long s)
{
    /* usbport collapses every registry failure to one code, so this says "read
     * or not" and never why - which is still the difference between "the value
     * never arrived" and "the value was zero". */
    return (s == 0) ? "read" : "NOT read";
}

static void write_companion_header(const SNAP_HEADER *h)
{
    comp("\nxhci98 snapshot - the part a maintainer can read without an offset "
         "table\n");
    /* **The report names the tool that produced it.** This file is pasted into
     * a public issue and read weeks later by somebody who has to know whether
     * the reader and the driver came from the same tree - the schema check
     * proves they agree, and this says WHICH agreement it was. The build stamp
     * carries what the version cannot between cuts: which build. */
    comp("  tool               xhcisnap %s, built %s\n",
         XHCISNAP_VERSION, XHCISNAP_BUILT);
    comp("  schema             %lu, %lu-byte header\n",
         h->SchemaVersion, h->HeaderBytes);
    comp("  build flavour      %s\n", flavour_text(h->Flavour));
    comp("  build flags        %08lX%s%s\n", h->BuildFlags,
         (h->BuildFlags & SNAP_B_DEBUG) ? "  checked" : "  free",
         (h->BuildFlags & SNAP_B_DIAGNOSTIC) ? " DIAGNOSTIC-DO-NOT-DEPLOY" : "");
    comp("  ExtensionBytes     %lu   (the key the .BIN must be decoded "
         "against)\n", h->ExtensionBytes);
    comp("  ports              %lu\n", h->PortCount);
    comp("  tear detector      %lu\n", h->TearDetector);

    comp("\n  registry values, as the driver read them at its last start:\n");
    if (h->SwitchRead == 0) {
        /*
         * **This is NOT the "nothing was configured" case, and this branch
         * said it was until the post-Phase 13 review rounds.** The driver sets SwitchRead on the
         * first line of its registry reader, before the registry service is
         * even tested, so a machine with no INF, a hand-copied driver and a
         * packet carrying no registry service all reach the reader and all
         * report 1. Those are told apart by the per-value statuses below.
         *
         * Zero here means the start path never reached the reader at all -
         * which is close to unreachable through a dump, because serving is
         * gated on the verbosity that reader produces. If it ever shows up it
         * is a statement about the start path, so say that and nothing more.
         */
        comp("    *** the driver never reached its registry read. This is a "
             "statement about\n"
             "        the START, not about the registry: the values were "
             "never looked for.\n");
    } else {
        comp("    XhciLogVerbosity   %s, value %lu\n",
             mpstatus_text(h->SwitchStatusVerbosity), h->VerbosityRead);
        comp("    XhciLogDebugView   %s\n",
             mpstatus_text(h->SwitchStatusDebugView));
        if (h->SwitchStatusVerbosity != 0) {
            /*
             * The distinction that actually matters to a user who believes
             * they set a value: usbport collapses absent, unreadable and
             * key-would-not-open into one failure code, so a nonzero status
             * beside a zero value means the value NEVER ARRIVED - not that it
             * was read as zero. A success beside a zero is a real zero.
             */
            comp("        ^ nonzero status: this value never arrived. Not "
                 "'it was zero' - the\n"
                 "          driver looked and found nothing. No INF has run "
                 "for this device,\n"
                 "          the value is absent, or usbport refused the "
                 "read.\n");
        }
    }

    comp("\n  verbosity tier     read %lu, APPLIED %lu\n",
         h->VerbosityRead, h->VerbosityApplied);
    if (h->VerbosityRead != h->VerbosityApplied) {
        /*
         * **This is not reachable through a dump and it is printed anyway.**
         * A refused level falls back to the default, the default is 0, and at 0
         * the driver declines - so a header carrying a refusal cannot come back
         * through this path. It stays because the header is the record of what
         * the driver applied, and a field that is only ever printed when it
         * cannot happen is exactly the kind of claim a later change makes
         * reachable without anyone noticing.
         */
        comp("    *** the level read was REFUSED, not clamped: it is outside "
             "0-4, so the\n"
             "        driver fell back to its default. A dump never reports a "
             "tier nobody asked for.\n");
    }
    if (h->VerbosityApplied < XHCISNAP_LEVEL_LOG) {
        comp("    This level records nothing into the note ring, ON PURPOSE - "
             "the counters\n"
             "    are the whole report here. To capture the ring, run  "
             "XHCISNAP -verbosity 2,\n"
             "    restart, reproduce, and dump again.\n");
    }

    /* Two lines: the fill is what a reader wants and the offsets are what a
     * decoder wants, and together they were 83 columns. */
    comp("\n  note ring          %lu of %lu bytes used\n",
         h->RingUsed, h->RingBytes);
    comp("                     head %lu, at +%lu in the extension\n",
         h->RingHead, h->RingOffset);
}

/*
 * Print the note ring's text out of the raw extension image. The ring is a byte
 * ring with a head, so the oldest byte is `(head - used)` masked to its size -
 * and printing it from offset 0 would give a rotated file, which is why the
 * head travels in the header.
 *
 * The bytes are records the driver formatted: label, optional `=XXXXXXXX`, CRLF.
 * They are ASCII by construction, but this is a dump off a machine that may be
 * misbehaving, so anything outside printable ASCII and CRLF is shown as `.`
 * rather than sent to a console.
 */
static void write_companion_ring(const SNAP_HEADER *h, const unsigned char *ext,
                                 unsigned long extBytes)
{
    unsigned long mask;
    unsigned long start;
    unsigned long i;
    unsigned long at;
    unsigned char c;

    if (h->RingBytes == 0 || h->RingUsed == 0) {
        comp("\nnote ring: empty.\n");
        return;
    }
    if (h->RingOffset + h->RingBytes > extBytes) {
        comp("\nnote ring: the driver says it is at +%lu for %lu bytes, which "
             "is past the\n  end of the %lu bytes that came back. Not printing "
             "it - a ring read from the\n  wrong place is a wrong reading "
             "rather than a failed one.\n",
             h->RingOffset, h->RingBytes, extBytes);
        return;
    }

    /* The ring's size is a power of two, so the wrap is a mask - the same
     * arithmetic the driver uses. */
    mask = h->RingBytes - 1;
    start = (h->RingHead - h->RingUsed) & mask;

    comp("\nnote ring, oldest record first (%lu bytes):\n", h->RingUsed);
    comp("----------------------------------------------------------------\n");
    for (i = 0; i < h->RingUsed; i++) {
        at = (start + i) & mask;
        c = ext[h->RingOffset + at];
        if (c == '\r' || c == '\n') {
            comp("%c", c);
        } else if (c < 0x20 || c > 0x7E) {
            comp(".");
        } else {
            comp("%c", c);
        }
    }
    comp("\n----------------------------------------------------------------\n");
}

/* ---- the PORTSC decode, which is the headline at the bench -------------- */

#define PORTSC_CCS  0x00000001UL
#define PORTSC_PED  0x00000002UL
#define PORTSC_OCA  0x00000008UL
#define PORTSC_PR   0x00000010UL
#define PORTSC_PLS  0x000001E0UL
#define PORTSC_PP   0x00000200UL
#define PORTSC_SPD  0x00003C00UL
#define PORTSC_CSC  0x00020000UL
#define PORTSC_PEC  0x00040000UL
#define PORTSC_WRC  0x00080000UL
#define PORTSC_OCC  0x00100000UL
#define PORTSC_PRC  0x00200000UL
#define PORTSC_PLC  0x00400000UL
#define PORTSC_CEC  0x00800000UL

static void print_portsc(const unsigned char *values, unsigned long ports)
{
    unsigned long i;
    unsigned long v;
    unsigned long unpowered;
    unsigned long connected;
    unsigned long unpoweredWithSomething;

    comp("\n  port  PORTSC    PP CCS PED  PR PLS spd | CSC PEC WRC OCC PRC "
         "PLC CEC\n");
    unpowered = 0;
    connected = 0;
    unpoweredWithSomething = 0;
    for (i = 0; i < ports; i++) {
        v = ((const unsigned long *)values)[i];
        if (v == 0xFFFFFFFFUL) {
            comp("  %4lu  %08lX  <all ones - the read was refused or the "
                 "controller is gone>\n", i + 1, v);
            continue;
        }
        if ((v & PORTSC_PP) == 0) {
            unpowered++;
            /*
             * A port that is unpowered AND has a device on it is the reading
             * that matters: the connect is what the hardware still reports,
             * and PP = 0 is the bus being dead at the connector.
             */
            if ((v & PORTSC_CCS) != 0) {
                unpoweredWithSomething++;
            }
        }
        if ((v & PORTSC_CCS) != 0) {
            connected++;
        }
        comp("  %4lu  %08lX   %lu   %lu   %lu   %lu  %2lu  %2lu |  %lu   %lu"
             "   %lu   %lu   %lu   %lu   %lu\n",
             i + 1, v,
             (v & PORTSC_PP) ? 1UL : 0UL,
             (v & PORTSC_CCS) ? 1UL : 0UL,
             (v & PORTSC_PED) ? 1UL : 0UL,
             (v & PORTSC_PR) ? 1UL : 0UL,
             (v & PORTSC_PLS) >> 5,
             (v & PORTSC_SPD) >> 10,
             (v & PORTSC_CSC) ? 1UL : 0UL,
             (v & PORTSC_PEC) ? 1UL : 0UL,
             (v & PORTSC_WRC) ? 1UL : 0UL,
             (v & PORTSC_OCC) ? 1UL : 0UL,
             (v & PORTSC_PRC) ? 1UL : 0UL,
             (v & PORTSC_PLC) ? 1UL : 0UL,
             (v & PORTSC_CEC) ? 1UL : 0UL);
    }

    /*
     * **This headline was miscalibrated and is fixed here** (recorded as the
     * tool's one known defect in
     * `docs/contributing/passthru-snapshot-instrument.md` section 8, and
     * repaired by task 13-L.2 rather than rediscovered at a bench).
     *
     * It used to announce the unpowered case only when EVERY port read
     * `PP = 0`, which **cannot happen on a controller with SuperSpeed ports** -
     * they leave six of the E460's eighteen unpowered by design, because this
     * driver manages USB 2.0 protocol ports only and deliberately leaves the
     * SuperSpeed ones alone. So on real silicon the headline stayed silent
     * while the per-port table said the thing it was meant to shout.
     *
     * The test that survives that is per port and not over all of them: **a
     * port with a device on it and no power** is Finding Q read off the
     * register, whatever the other seventeen say. The all-ports case is kept as
     * a second line because on a controller that really did lose power
     * everywhere it is the more useful sentence.
     */
    if (unpoweredWithSomething != 0) {
        comp("\n  *** %lu port(s) report a DEVICE CONNECTED with PP CLEAR - the "
             "bus is unpowered\n      at the connector while the controller "
             "still sees the device. This is\n      Finding Q read off the "
             "register.\n", unpoweredWithSomething);
    }
    if (ports != 0 && unpowered == ports) {
        comp("\n  *** PP is CLEAR on EVERY port.\n");
    } else if (unpowered != 0) {
        comp("\n  (PP is clear on %lu of %lu ports. On a controller with "
             "SuperSpeed ports that\n   is EXPECTED: this driver manages USB "
             "2.0 protocol ports only and leaves the\n   SuperSpeed ones "
             "unpowered by design. It is not a fault on its own.)\n",
             unpowered, ports);
    }
    comp("  (%lu of %lu ports report a device connected.)\n", connected, ports);
}

/* ---- -probe: is the ROUTE alive, independent of this driver? ------------ */

/*
 * Four controls, each aimed at one clause of the contract that was derived by
 * disassembly. They matter because "nothing came back" has two very different
 * causes - the route does not work here, or it works and this driver has no
 * snapshot support - and at a bench those need separating in one command
 * rather than in an afternoon.
 *
 * usbport's own status codes are what discriminate: reaching the miniport at
 * all produces 6 from a driver that declines, while a header it did not like
 * produces 4 or 7 and a request code it does not know produces 2. Getting the
 * EXPECTED code from each control is what says the dispatcher parsed our
 * buffer rather than rejecting it wholesale.
 */
static unsigned long probe_call(HANDLE device, unsigned long request,
                                unsigned long declaredBytes,
                                unsigned long actualBytes)
{
    DWORD returned;

    memset(snap_buffer, 0, sizeof(snap_buffer));
    put32(snap_buffer, UU_REQUEST, request);
    put32(snap_buffer, UU_REQUEST_BYTES, declaredBytes);
    put32(snap_buffer, UU_GUID + 0, snap_guid[0]);
    put32(snap_buffer, UU_GUID + 4, snap_guid[1]);
    put32(snap_buffer, UU_GUID + 8, snap_guid[2]);
    put32(snap_buffer, UU_GUID + 12, snap_guid[3]);
    if (actualBytes >= UU_PARAMETERS + 4) {
        put32(snap_buffer, UU_PARAM_BYTES, actualBytes - UU_PARAMETERS);
    }

    returned = 0;
    if (!DeviceIoControl(device, IOCTL_USB_USER_REQUEST,
                         snap_buffer, actualBytes,
                         snap_buffer, actualBytes, &returned, NULL)) {
        printf("    DeviceIoControl failed, error %lu\n",
               (unsigned long)GetLastError());
        return 0xFFFFFFFFUL;
    }
    return get32(snap_buffer, UU_STATUS);
}

static void probe_one(HANDLE device, const char *what, unsigned long got,
                      unsigned long want, const char *means)
{
    printf("  %-34s status %2lu (%s)\n", what, got, uu_status_text(got));
    if (got == want) {
        say_hang("    ", "      ", "as derived: %s", means);
    } else {
        say_hang("    ", "      ", "*** EXPECTED %lu (%s) - %s", want,
                 uu_status_text(want), means);
    }
}

static int probe_route(HANDLE device)
{
    unsigned long passthru;

    printf("\nroute probe - IOCTL 0x%08lX on this controller:\n",
           IOCTL_USB_USER_REQUEST);

    passthru = probe_call(device, USBUSER_PASS_THRU,
                          SNAP_BUFFER_BYTES, SNAP_BUFFER_BYTES);
    printf("  %-34s status %2lu (%s)\n", "PassThru, our GUID", passthru,
           uu_status_text(passthru));
    if (passthru == 0) {
        printf("    the miniport ANSWERED - the channel is live, take the "
               "dump\n");
    } else if (passthru == 6) {
        /*
         * **Two situations wearing one sentence, and since task 13-L.2 the
         * second is the ordinary case on every machine.** A switched-off
         * channel answers a caller exactly as a binary built without one
         * would - deliberately, because 6 is the only honest nonzero value at
         * this slot - so this probe structurally cannot separate them, and
         * saying which it is would be a guess. What it can do is name both and
         * say what to do about the one that is fixable from here.
         */
        say("    ", "the request reached a miniport and it DECLINED. The ROUTE "
                    "WORKS.");
        say("    ", "Two things look exactly like this and the driver cannot "
                    "tell you which:");
        say_hang("      ", "         ", "1. an xhci98 whose channel is switched off - the default "
                      "on every machine. Fix: xhcisnap -verbosity 2, then "
                      "RESTART, reproduce, and dump.");
        say_hang("      ", "         ", "2. some other miniport on this HCD index, or an xhci98 "
                      "from before the channel shipped. Try -c 1, -c 2, or "
                      "check the installed xhci98.sys.");
    } else {
        printf("    *** the request did not reach a miniport\n");
    }

    probe_one(device, "unknown request code 15",
              probe_call(device, 15UL, SNAP_BUFFER_BYTES, SNAP_BUFFER_BYTES),
              2UL, "the dispatcher's request table has 8 entries and 15 is "
                   "not one of them");
    probe_one(device, "RequestBufferLength disagreeing",
              probe_call(device, USBUSER_PASS_THRU,
                         SNAP_BUFFER_BYTES - 4UL, SNAP_BUFFER_BYTES),
              4UL, "the header's own length must equal the IOCTL's input "
                   "length exactly");
    probe_one(device, "a 0x20-byte buffer",
              probe_call(device, USBUSER_PASS_THRU, 0x20UL, 0x20UL),
              7UL, "PassThru's floor is 0x28 - a 0x10 header plus a 0x18 "
                   "body");

    return (passthru == 0 || passthru == 6) ? 0 : 1;
}

/* ---- the driver's own software key, and setting values in it ----------- */

/*
 * **WHY THE TOOL WRITES THE REGISTRY AT ALL, and why the driver does not.**
 *
 * The driver reads two values at `StartController` - `XhciLogVerbosity` and
 * `XhciLogDebugView` - through usbport's
 * `UsbPortGetMiniportRegistryKeyValue`. *(Three until the snapshot-value merge, when
 * `XhciLogSnapshot` became rung 0 of the verbosity ladder.)* usbport's sixteen-service table has
 * exactly one registry entry and it is a READ - there is no `Set` counterpart
 * at any offset in any shipping build - so a driver-side write would have to be
 * an import of its own, and every import is a new way for the load to fail
 * silently on Windows 98. Writing at the next start could not work either:
 * usbport zeroes the miniport extension before *every* start, so there would be
 * nothing left to write, and it would have looked like a working channel
 * reporting a clean run.
 *
 * None of that reaches a ring-3 program. This is an ordinary `RegSetValueEx`.
 *
 * **The key is per machine, which is why no ready-made `.reg` can ship.** The
 * driver's software key is a numbered subkey of the USB class key, and which
 * number depends on what else was ever installed. So the tool finds it, and the
 * user never opens `regedit`.
 *
 * Both targets, and the paths differ:
 *
 *   Windows 2000   HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-...}
 *   Windows 98 SE  HKLM\System\CurrentControlSet\Services\Class\USB
 *
 * **A key is ours if it carries ANY value only this driver's INF writes.** The
 * INF places two, so their presence is what an installed xhci98 looks like on
 * either system, without this tool having to know anything about device IDs.
 *
 * **Either of them rather than just the newest** - and that matters more than it
 * sounds. The ordinary way a guest or a bench machine gets a new driver is a
 * `.sys` swapped into place, which does NOT re-run the INF: the software key
 * still holds whatever the last INSTALL wrote. So a machine running this build
 * can easily have only `XhciLogDebugView`, which has been there since task
 * 11-V.9, and a tool that recognised its key by the newest value alone would
 * report "no driver key found" on exactly the machines a maintainer is most
 * likely to be sitting at. **That is not a hypothetical**: the 2a guest's three
 * keys were of three different vintages, and one of them
 * (`Class\USB\0009`) carried no `XhciLogDebugView` at all.
 *
 * *(The two retired names were in this list until the snapshot-value merge for the same
 * reason. They are not any more - see the list itself for why the argument
 * stops at values that have actually shipped.)*
 *
 * `NTMPDriver` is accepted as well: it is the 9x path's own statement of which
 * driver binds, and it identifies a key whose values were all removed by hand.
 * It has no Windows 2000 counterpart - the NT path binds through a service -
 * which is the other reason the value list above has to be generous.
 *
 * **Both controllers get it on a machine with two.** Setting one and reporting
 * success would be the worst outcome: the user restarts, reproduces on the
 * other controller, and dumps an empty ring.
 */

#define XHCI_CLASS_NT   "SYSTEM\\CurrentControlSet\\Control\\Class\\{36FC9E60-C465-11CF-8056-444553540000}"
#define XHCI_CLASS_9X   "System\\CurrentControlSet\\Services\\Class\\USB"

#define MAX_DRIVER_KEYS 16

static char driverKeys[MAX_DRIVER_KEYS][MAX_PATH];
/* What made each key a match, so the operator can see the tool's reasoning
 * rather than only its conclusion, and HOW SURE that answer is. The pointers
 * are into ourValues[] or to a literal - all static storage, so there is
 * nothing here to own. */
static const char *driverKeyWhy[MAX_DRIVER_KEYS];
static int driverKeyStrength[MAX_DRIVER_KEYS];
static int driverKeyCount;

/*
 * ---- identifying the driver's own key, and the three strengths of answer ---
 *
 * **The problem this shape exists for.** The key is a numbered subkey of the
 * USB class key and its number is per machine, so the tool has to recognise it
 * by content. The obvious content is a value only this driver's INF writes -
 * but that is a NAME-only test, the names begin `XhciLog` rather than
 * `Xhci98Log`, and an unrelated USB-class driver using one of them would be
 * claimed and then written to. No such collision has ever been observed and
 * these names are this project's own inventions; the risk is small, and it is
 * still not one to leave sitting under a `set_dword`.
 *
 * So a match now has a STRENGTH, and there are two decisive signals:
 *
 *   NTMPDriver = xhci98.sys   the 9x install's own statement of which driver
 *                             binds. It names a FILE rather than a setting, so
 *                             nothing else can wear it by accident. There is no
 *                             NT counterpart - that path binds through a
 *                             service - which is why one signal is not enough.
 *   InfSection = Xhci.Dev*    written by BOTH setup engines from this INF's own
 *                             install-section name, and - the property that
 *                             matters here - it comes from the last INSTALL, so
 *                             **it survives a `.sys` swap**, which is exactly
 *                             the case that broke this function two commits ago.
 *
 * and one weak one, which is the `XhciLog*` name list, kept because a key whose
 * identifying values were stripped by hand is still the key to write to.
 *
 * **A contradictory NTMPDriver refuses the key outright**, whatever else it
 * carries. That is what "decisive" has to mean to be worth writing down: the
 * previous version called it decisive and then fell through to the name list
 * anyway, so a Windows 98 key explicitly bound to `other.sys` was still claimed
 * if it happened to carry a colliding name. (Codex review of `d3e4d43`.)
 *
 * **A weak match is kept and listed, and then not written to unless `-force`
 * says so.** Dropping the weak ones whenever any decisive match existed was an
 * earlier shape and it was wrong at the wrong granularity: a machine with two
 * controllers can legitimately have one key the setup engine wrote and one an
 * operator has edited, and that machine lost its second controller silently.
 * The decision is per key - see `may_write` - and the run says how many keys
 * it is treating that way before it writes anything.
 */

#define MATCH_NONE      0
#define MATCH_NAME      1   /* an XhciLog* value, and nothing better        */
#define MATCH_DECISIVE  2   /* NTMPDriver or InfSection - ours, not a guess */

/*
 * Value names that only this driver's INF ever writes. The retired ones are in
 * the list deliberately - see the comment beside them.
 */
/*
 * **The retired names are NOT in this list**, and that is a decision rather than
 * an omission. *(Project owner: "I think don't even need to mention
 * xhcilogsnapshot in the code at all.")* `XhciLogSnapshot` left the INF on
 * at the merge and `XhciLogFile`, and **neither has ever shipped in
 * a release** - both were retired before the next cut published anything, so no
 * machine outside this project's own guests can carry one. Carrying their names here to catch that population is
 * the same mechanism-with-no-population this batch already declined once, when
 * it turned down a review's version-marker and migration path for exactly that
 * reason; keeping one and refusing the other was inconsistent.
 *
 * **What is given up is small and worth naming**: a key carrying a retired value
 * and NO decisive signal is now invisible to the name-only fallback. Both setup
 * engines write a decisive signal at install (`NTMPDriver` on 9x, `InfSection`
 * on both), so such a key would have to have been hand-edited into that exact
 * shape.
 */
static const char *ourValues[] = {
    "XhciLogVerbosity",
    "XhciLogDebugView"
};

/*
 * Does this value exist under `key`? Name only - nothing here reads it.
 *
 * **A real buffer is passed rather than NULL, and that is a Windows 98
 * precaution.** The documented Win32 behaviour of `RegQueryValueEx` with
 * `lpData == NULL` is to answer `ERROR_SUCCESS` and report the size, and that
 * is what the first version relied on - but this code path has to work on
 * Windows 9x's registry implementation, whose handling of that form is not
 * something this project has measured, and a false negative here means
 * `XHCISNAP -verbosity N` reporting "no driver key found" on a machine that has
 * one.
 * Asking for one byte cannot fail that way: a value longer than the buffer
 * answers `ERROR_MORE_DATA`, which **is** the existence this function is
 * asking about, so both codes count as yes.
 */
static int value_exists(HKEY key, const char *name)
{
    DWORD bytes;
    BYTE small[1];
    LONG rc;

    bytes = sizeof(small);
    rc = RegQueryValueExA(key, name, NULL, NULL, small, &bytes);
    return (rc == ERROR_SUCCESS || rc == ERROR_MORE_DATA) ? 1 : 0;
}

/*
 * Read a REG_SZ into `out` and NUL-terminate it. Returns one of STR_ABSENT,
 * STR_OK or STR_UNREADABLE - three answers and not two, for the reason given
 * where those are defined: a caller that cannot tell "not there" from "there
 * and I could not read it" will look elsewhere for permission it has already
 * been refused.
 *
 * **The termination and the type check are both load-bearing and neither was
 * here before `d3e4d43`.** `RegQueryValueEx` fills only as many bytes as the
 * value holds, so a short value that carries no NUL of its own left the caller
 * reading whatever was on the stack past it - terminating the last byte of the
 * buffer does not help when the data stopped at byte 3. And without the type
 * check, any value of any type whose bytes happened to spell what the caller
 * was looking for would match. Neither is reachable from a registry this
 * project's own INF wrote; both are reachable from a hand-edited one, which is
 * precisely the machine a maintainer is standing at when they run this.
 */
/*
 * **Three answers, not two, and the third is the one that matters.**
 *
 * This returned a plain yes/no, so "the value is not there" and "the value is
 * there and I could not read it" were the same answer - and the caller treats
 * absence as permission to look elsewhere. A contradictory `NTMPDriver` longer
 * than the buffer, or of an unexpected type, therefore looked absent and let
 * the key be claimed by a weaker signal further down. That is the one direction
 * this must never fail in, because the whole point of reading `NTMPDriver` is
 * to be told NO. (Codex review of `dcdc5ac`.)
 */
#define STR_ABSENT      0   /* no such value                                */
#define STR_OK          1   /* read, terminated, in `out`                   */
#define STR_UNREADABLE  2   /* it is there; wrong type, or longer than `out` */

static int read_string(HKEY key, const char *name, char *out, DWORD outBytes)
{
    DWORD type;
    DWORD bytes;
    LONG rc;

    if (outBytes == 0) {
        /* Unreachable from this file's callers and guarded anyway: the
         * subtraction below would wrap to 4 GB and hand RegQueryValueExA a
         * capacity that does not exist. A helper is not safe because its
         * callers happen to be. */
        return STR_UNREADABLE;
    }

    type = 0;
    bytes = outBytes - 1;
    rc = RegQueryValueExA(key, name, NULL, &type, (LPBYTE)out, &bytes);
    if (rc == ERROR_MORE_DATA) {
        /* It exists and is longer than this buffer. */
        return STR_UNREADABLE;
    }
    if (rc != ERROR_SUCCESS) {
        return STR_ABSENT;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        return STR_UNREADABLE;
    }
    if (bytes >= outBytes) {
        return STR_UNREADABLE;
    }
    /* Terminate at what was actually returned, not at the end of the buffer. */
    out[bytes] = '\0';
    return STR_OK;
}

/*
 * The install sections this INF actually has, matched EXACTLY.
 *
 * This was `starts_with_ci(text, "Xhci.Dev")` for one commit, which is an
 * over-generalisation with a cost: `Xhci.Device` and `Xhci.Developer` would
 * have been read as decisive. There is no reason to accept a family here - the
 * INF is in this repository and has exactly two install sections, so the test
 * can be the two names. A third section added to the INF must be added here
 * too, and that is the right kind of coupling: it fails closed, by refusing to
 * recognise a key, rather than by claiming one.
 *
 * **The phrase printed for a match lives beside the name it belongs to**, so
 * the two cannot drift apart: a section added to one is a compile error away
 * from being added to the other, rather than a silently missing message.
 *
 * *(It was one shared string, "InfSection = this INF's own install section",
 * until the snapshot-value merge. That ran the per-key line to 83 columns on the 2b guest -
 * the only line in the tool that still overflowed after the readability pass -
 * and naming the section is both SHORTER and strictly more useful: it says
 * which of the two engines installed this key, which matters on a machine
 * carrying keys from both.)*
 *
 * **MEASURED, within minutes of that change, and it is a trap:
 * Windows 2000 records the UNDECORATED name.** Both driver keys on the 2b guest
 * read `InfSection = Xhci.Dev`, not `Xhci.Dev.NTx86`, even though this INF
 * carries both sections and the NT engine installs from the decorated one. So
 * **`Xhci.Dev` is not "the 9x section" - it is what BOTH targets record**, and
 * an obvious-looking tidy-up here (matching only `Xhci.Dev.NTx86` on the NT
 * path, or dropping the undecorated name as 9x-only) would stop this tool
 * recognising Windows 2000 keys altogether. Keeping both names is what saved
 * it, for a reason the original comment did not know.
 *
 * *(What is measured is the value in the key. The likely mechanism - that the
 * engine records the section as NAMED IN `[XhciModels]`, with the platform
 * decoration applied when it is processed - is an explanation and has not been
 * checked, so do not write it down as one.)*
 */
static const struct {
    const char *section;
    const char *why;
} ourSections[] = {
    { "Xhci.Dev",       "InfSection = Xhci.Dev" },
    { "Xhci.Dev.NTx86", "InfSection = Xhci.Dev.NTx86" }
};

/*
 * Is this class subkey this driver's, and how sure is the answer? Returns one
 * of MATCH_*, with `why` pointed at a short phrase naming what decided it -
 * which the caller prints, so a wrong match is something the operator can see
 * rather than something that happens quietly.
 */
static int key_match(HKEY parent, const char *name, const char **why)
{
    HKEY key;
    char text[80];
    int got;
    int i;

    *why = "";
    if (RegOpenKeyExA(parent, name, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return MATCH_NONE;
    }

    /*
     * **A contradiction refuses the key outright**, before anything else is
     * looked at. This is the half that makes "decisive" mean something: a key
     * whose 9x install says another driver binds is not ours, however many
     * familiar-looking value names it carries.
     */
    got = read_string(key, "NTMPDriver", text, sizeof(text));
    if (got != STR_ABSENT) {
        /*
         * **Present and not provably ours is a contradiction**, whatever the
         * reason. Our own value is the ten characters `xhci98.sys`, so a
         * `NTMPDriver` this function could not read is a `NTMPDriver` that is
         * not ours - too long, or not a string at all - and the key belongs to
         * something else. Treating unreadable as absent is what let a long one
         * fall through to a weaker signal below.
         */
        if (got != STR_OK || _stricmp(text, "xhci98.sys") != 0) {
            RegCloseKey(key);
            return MATCH_NONE;
        }
        RegCloseKey(key);
        *why = "NTMPDriver = xhci98.sys";
        return MATCH_DECISIVE;
    }

    /*
     * Both engines write the install section they used, and this INF's are
     * `Xhci.Dev` and `Xhci.Dev.NTx86`. It survives a `.sys` swap because it
     * records the last INSTALL, which is what makes it the useful signal on the
     * NT path - there is no NTMPDriver there.
     */
    if (read_string(key, "InfSection", text, sizeof(text)) == STR_OK) {
        for (i = 0; i < (int)(sizeof(ourSections) / sizeof(ourSections[0]));
             i++) {
            if (_stricmp(text, ourSections[i].section) == 0) {
                RegCloseKey(key);
                *why = ourSections[i].why;
                return MATCH_DECISIVE;
            }
        }
    }

    for (i = 0; i < (int)(sizeof(ourValues) / sizeof(ourValues[0])); i++) {
        if (value_exists(key, ourValues[i])) {
            RegCloseKey(key);
            *why = ourValues[i];
            return MATCH_NAME;
        }
    }

    RegCloseKey(key);
    return MATCH_NONE;
}

/*
 * `<classPath>\<name>` into `out`, or 0 if it does not fit.
 *
 * **`name` comes from `RegEnumKeyEx` and is only bounded by MAX_PATH itself**,
 * so `classPath` plus a separator plus `name` can be longer than the MAX_PATH
 * buffer it was being `sprintf`ed into. A subkey name that long is not
 * something either setup engine produces - the keys here are four digits - but
 * "no caller does that today" is not a bound, and this walks whatever is in the
 * class key rather than whatever this project put there. (Codex review of
 * `b213e47`.)
 */
static int compose_key_path(char *out, DWORD outBytes,
                            const char *classPath, const char *name)
{
    size_t need;

    need = strlen(classPath) + 1 + strlen(name) + 1;
    if (need > (size_t)outBytes) {
        return 0;
    }
    sprintf(out, "%s\\%s", classPath, name);
    return 1;
}

/*
 * Take one match into the arrays. Returns TAKE_*.
 *
 * **A full array gives its place to a decisive match**, which is what makes the
 * bound safe rather than merely bounded. Two earlier shapes were not: collecting
 * everything and filtering afterwards lost the real key behind sixteen weak ones
 * in enumeration order, and collecting one strength per pass lost the SECOND
 * controller on a machine where one key is decisive and one has been hand-edited
 * - which is exactly the machine this tool must not get half right, since
 * setting one controller and reporting success sends the user off to reproduce
 * on the other and dump an empty ring.
 */
#define TAKE_OK        0
#define TAKE_FULL      1   /* no room, and nothing here to displace         */
#define TAKE_TOO_LONG  2   /* the composed path does not fit                */

/*
 * **Nonzero when a key this driver could own was found and NOT recorded.**
 *
 * Both dropping paths used to be silent about their consequence, and both of
 * them can leave `driverKeyCount` at 0 or short while the machine really does
 * have a controller to set - at which point "no xhci98 driver key found - the
 * INF has never run here" is a false statement, and a run that set fifteen of
 * sixteen keys reported success. A tool whose whole purpose is to spare the
 * user a second reproduction must not be the thing that costs them one.
 */
static int droppedMatches;

static int take_match(const char *classPath, const char *name,
                      const char *why, int strength)
{
    char path[MAX_PATH];
    int i;

    if (!compose_key_path(path, sizeof(path), classPath, name)) {
        droppedMatches++;
        return TAKE_TOO_LONG;
    }

    if (driverKeyCount < MAX_DRIVER_KEYS) {
        strcpy(driverKeys[driverKeyCount], path);
        driverKeyWhy[driverKeyCount] = why;
        driverKeyStrength[driverKeyCount] = strength;
        driverKeyCount++;
        return TAKE_OK;
    }

    if (strength != MATCH_DECISIVE) {
        droppedMatches++;
        return TAKE_FULL;
    }
    for (i = 0; i < driverKeyCount; i++) {
        if (driverKeyStrength[i] == MATCH_NAME) {
            strcpy(driverKeys[i], path);
            driverKeyWhy[i] = why;
            driverKeyStrength[i] = strength;
            /*
             * **The key that was here is now gone, and that is a drop.** It
             * was found, recorded, and then evicted to make room for a better
             * answer - which is the right trade, but not a free one: it is no
             * longer in the list the run prints, so a machine with seventeen
             * candidates would have shown sixteen and claimed nothing was
             * missed. Counting only the arrival that could not be stored, and
             * not the entry it displaced, was this function's own version of
             * the silence the counter exists to end.
             */
            droppedMatches++;
            return TAKE_OK;
        }
    }
    /* Sixteen decisive matches already, and this is a seventeenth. Nothing to
     * displace and nothing honest to do but say so. */
    droppedMatches++;
    return TAKE_FULL;
}

/* Collect every key under `classPath` this driver could own, at any strength. */
static void find_driver_keys_under(const char *classPath)
{
    HKEY parent;
    char name[MAX_PATH];
    const char *why;
    int strength;
    int took;
    int complained;
    DWORD nameLen;
    DWORD index;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, classPath, 0, KEY_ENUMERATE_SUB_KEYS,
                      &parent) != ERROR_SUCCESS) {
        return;
    }

    index = 0;
    complained = 0;
    for (;;) {
        nameLen = sizeof(name);
        if (RegEnumKeyExA(parent, index, name, &nameLen, NULL, NULL, NULL, NULL)
            != ERROR_SUCCESS) {
            break;
        }
        index++;
        strength = key_match(parent, name, &why);
        if (strength == MATCH_NONE) {
            continue;
        }

        took = take_match(classPath, name, why, strength);
        if (took == TAKE_TOO_LONG) {
            printf("  skipping a subkey whose name is too long to record.\n");
            continue;
        }
        if (took == TAKE_FULL) {
            /*
             * **Keep going.** Stopping here was the bug: a decisive key later
             * in enumeration order could no longer arrive to displace one of
             * the weak entries already stored, so a full array of weak matches
             * permanently hid the real one. The message is printed once so a
             * class key full of candidates does not produce sixteen lines.
             */
            if (!complained) {
                printf("  more than %d driver keys here - keeping the ones "
                       "identified most strongly. That is not a machine this "
                       "tool was written for; check the list below.\n",
                       MAX_DRIVER_KEYS);
                complained = 1;
            }
            continue;
        }
    }

    RegCloseKey(parent);
}

/*
 * Every key this driver could own, at either strength.
 *
 * **Both strengths in one sweep, and the filtering is per key rather than per
 * run.** A machine with two controllers can legitimately have one key the setup
 * engine wrote and one an operator has edited down to a bare `XhciLog*` value,
 * and a run that collected only the stronger kind would silently leave the
 * second controller alone - which is the failure mode this tool exists to
 * remove, not to have. Both are found, both are listed, and what differs is
 * whether writing one needs `-force`.
 *
 * Both roots are searched on both systems rather than branching on the OS
 * version. One of them simply is not there, and a tool that guessed wrong about
 * which would report "no driver" on a machine that has one.
 */
static int find_driver_keys(void)
{
    driverKeyCount = 0;
    droppedMatches = 0;
    find_driver_keys_under(XHCI_CLASS_NT);
    find_driver_keys_under(XHCI_CLASS_9X);
    return driverKeyCount;
}

/*
 * Say so, and mean it: a run that could not record every key it found has not
 * done what the user asked, whatever it managed. Returns nonzero when the
 * caller should fail.
 */
static int report_dropped_matches(void)
{
    if (droppedMatches == 0) {
        return 0;
    }
    printf("\n*** %d key(s) that could be this driver's were found and NOT\n"
           "    recorded - either more than %d of them, or one whose name is\n"
           "    too long to hold. This tool was not written for a machine like\n"
           "    that, and it will not pretend it covered it.\n",
           droppedMatches, MAX_DRIVER_KEYS);
    if (driverKeyCount > 0) {
        printf("    Set the values by hand on any controller missing from the\n"
               "    list above.\n");
    } else {
        /* Nothing was listed, so there is no "above" to point at - and this is
         * the branch where the caller would otherwise have said the INF never
         * ran, which would have been false. */
        printf("    Nothing could be listed at all, so set the values by hand\n"
               "    on every xHCI controller this machine has.\n");
    }
    return 1;
}

/* How many of the keys found were matched by NAME alone. */
static int weak_match_count(void)
{
    int i;
    int n;

    n = 0;
    for (i = 0; i < driverKeyCount; i++) {
        if (driverKeyStrength[i] == MATCH_NAME) {
            n++;
        }
    }
    return n;
}

/*
 * The leaf of a driver key path, for the one-line-per-key report. The parent is
 * printed once in the header, so repeating a 47-character class path on every
 * line would spend the width the readings need.
 */
static const char *key_leaf(const char *path)
{
    const char *slash;

    slash = strrchr(path, '\\');
    return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

/*
 * The parent all the keys found share, or NULL when they do not - a machine can
 * legitimately have keys under both the NT class path and the 9x one, and in
 * that case each line has to carry its own full path rather than be filed under
 * a heading that is true of only some of them.
 */
static char keyParent[MAX_PATH];

static const char *common_key_parent(void)
{
    int i;
    const char *slash;
    size_t n;

    if (driverKeyCount <= 0) {
        return NULL;
    }
    slash = strrchr(driverKeys[0], '\\');
    if (slash == NULL) {
        return NULL;
    }
    n = (size_t)(slash - driverKeys[0]);
    if (n == 0 || n >= sizeof(keyParent)) {
        return NULL;
    }
    for (i = 1; i < driverKeyCount; i++) {
        if (strlen(driverKeys[i]) <= n ||
            driverKeys[i][n] != '\\' ||
            strncmp(driverKeys[i], driverKeys[0], n) != 0) {
            return NULL;
        }
    }
    memcpy(keyParent, driverKeys[0], n);
    keyParent[n] = '\0';
    return keyParent;
}

/*
 * **The heading, and then ONE LINE PER KEY from the write loop itself.**
 *
 * This used to print a two-line paragraph per key here and a three-line
 * paragraph per key again while writing, plus blank lines between all of them:
 * on the stage L3 2a guest, which has THREE driver keys, that was thirty-one
 * lines into a twenty-five-line box, and the two lines with the most diagnostic
 * value in the whole tool - which key, and what identified it - were the ones
 * that scrolled away. *(Project owner, seeing it: "a wall of text ... perhaps a
 * shorter single screen text will suffice.")*
 *
 * **The rule is that the ordinary case is one line and only an anomaly earns a
 * paragraph.** A key identified decisively, holding a readable in-range value,
 * and taking the write is one line; a name-only match, an out-of-range or
 * unreadable value, a key that will not open, or a refused write each buy back
 * as much prose as they need, because those are the cases where a stranger has
 * to read something. **No reading was dropped to make it fit** - which key, what
 * identified it, what it held, what it holds now, and that it takes effect at
 * the next start are all still on the screen.
 */
static void print_key_header(void)
{
    const char *parent;

    parent = common_key_parent();
    if (parent != NULL) {
        say("", "\n%d xhci98 driver key(s) under HKLM\\%s:",
            driverKeyCount, parent);
    } else {
        say("", "\n%d xhci98 driver key(s), under more than one class path:",
            driverKeyCount);
    }
}

/* The per-key line's trailing column: what identified this key. */
static const char *key_why_short(int i)
{
    return (driverKeyStrength[i] == MATCH_NAME) ? "name only" : driverKeyWhy[i];
}

/* The leaf, or the whole path when the keys share no parent to head them with. */
static const char *key_label(int i)
{
    return (common_key_parent() != NULL) ? key_leaf(driverKeys[i])
                                         : driverKeys[i];
}

/* The rung's name, for the one line that spells the ladder out at a DOS prompt.
 * The wording matches the usage text's ladder; two spellings of one rung is how
 * a reader ends up believing there are two rungs. */
static const char *level_name(unsigned long level)
{
    switch (level) {
    case 0:  return "OFF";
    case 1:  return "counters only";
    case 2:  return "counters + note ring";
    case 3:  return "+ the PORTSC table";
    case 4:  return "+ everything, addresses included";
    default: return "OUT OF RANGE";
    }
}

/*
 * **A name-only match is not written to unless it is asked for by name, and the
 * decision is PER KEY.**
 *
 * Printing a warning and then writing anyway was the first attempt, and it is
 * not a repair: making a wrong match visible is not the same as not making it.
 * Refusing the whole run was the second, and it is wrong in the other
 * direction - a machine with two controllers can legitimately have one key the
 * setup engine wrote and one an operator has edited, and refusing both because
 * of the second leaves the first unset for no reason.
 *
 * Refusing outright was not available at all until `InfSection` became a
 * decisive signal: it is written by both setup engines, survives a `.sys` swap
 * because it records the last INSTALL, and is what the value-setting path's key
 * bug needed (`d27a718`, when that path was still spelled `-enable`).
 * `NTMPDriver` covers the 9x path. **So every key this driver is properly
 * installed against has a decisive signal**, and a name-only one means that key
 * has been hand-edited - which is worth stopping for, on that key.
 *
 * `-force` is the escape hatch, and it is the project's own polarity rule
 * applied to a registry write: the safe configuration is the default and the
 * convenience is the thing you ask for by name.
 *
 * Returns nonzero when key `i` may be written.
 */
static int may_write(int i, int force)
{
    if (driverKeyStrength[i] != MATCH_NAME) {
        return 1;
    }
    return force ? 1 : 0;
}

/* Said once, after the listing and before the writes, and only when one of the
 * keys found is going to be treated differently from the others. */
static void explain_weak(int force)
{
    int weak;

    weak = weak_match_count();
    if (weak == 0) {
        return;
    }

    say("", "\n*** %d of the %d key(s) above was identified by a value NAME "
            "alone. A key this driver installed carries NTMPDriver (Windows 98) "
            "or InfSection (both systems); those carry neither, so what "
            "identified them was a name - and a name is not proof of whose key "
            "it is. Most likely such a key has been edited by hand.",
        weak, driverKeyCount);

    if (force) {
        say("    ", "-force given: writing to them anyway.");
    } else {
        printf("\n    They are being SKIPPED. If one of them is the controller\n"
               "    you mean, run the same command again with -force:\n\n"
               "      XHCISNAP -force ...\n");
    }
}

static int set_dword(const char *keyPath, const char *value, DWORD data)
{
    HKEY key;
    LONG rc;

    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_SET_VALUE, &key);
    if (rc != ERROR_SUCCESS) {
        printf("  cannot open HKLM\\%s for writing, error %ld\n", keyPath, rc);
        return 0;
    }
    rc = RegSetValueExA(key, value, 0, REG_DWORD, (const BYTE *)&data,
                        sizeof(data));
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        printf("  cannot write %s, error %ld\n", value, rc);
        return 0;
    }
    return 1;
}

/*
 * What a level read found. **Four failures that used to be one**, because a
 * report that flattens them into "0" makes a false statement on exactly the
 * machine this report exists for - a key somebody has edited by hand.
 */
#define LEVEL_READ_OK           0   /* r->Value holds the level               */
#define LEVEL_READ_ABSENT       1   /* ERROR_FILE_NOT_FOUND: really not there */
#define LEVEL_READ_NOKEY        2   /* the key itself would not open          */
#define LEVEL_READ_UNREADABLE   3   /* there, and not a four-byte REG_DWORD   */
#define LEVEL_READ_ERROR        4   /* the query failed some OTHER way        */

/*
 * The evidence behind whichever of those came back, so the report can name what
 * it found instead of describing it. A struct rather than a fourth and fifth
 * out-parameter: each fix in this area has needed one more piece of evidence,
 * and this is the shape that stops the signature growing again.
 */
typedef struct {
    DWORD Value;    /* the level - untouched unless LEVEL_READ_OK          */
    DWORD Type;     /* the type actually found; 0 if nothing was found     */
    DWORD Bytes;    /* how many bytes it holds; 0 if nothing was found     */
    LONG  Error;    /* the failing code; ERROR_SUCCESS when there is none  */
} LEVEL_READ;

/*
 * Read the level out of `keyPath`. `r->Value` is left alone unless
 * LEVEL_READ_OK is returned, so a caller that seeded it with a default keeps
 * the default.
 *
 * **The type and the length are checked here and NEITHER is checked by the
 * driver, so an unreadable value must never be reported as an off one.**
 * usbport's `UsbPortGetMiniportRegistryKeyValue` calls `ZwQueryValueKey` with
 * `KeyValueFullInformation` and copies the requested byte count out of the data
 * **unconditionally - without looking at the type, and without clamping to
 * `DataLength`** (docs/usb-xhci-info/usbport-miniport-abi.md, the task 11-V.7
 * box). So a `REG_BINARY` holding `02 00 00 00` reads as level 2 and OPENS the
 * channel, and a two-byte `REG_DWORD` has four bytes taken out of it either
 * way, while this tool refuses to decode either. Saying such a key was "not
 * set" would be the one wrong answer that matters here: it reports a shut door
 * on a machine whose door is open.
 *
 * **Only `ERROR_FILE_NOT_FOUND` is absence.** Every other query failure - a key
 * marked for deletion between the open and the query is the reachable one - is
 * `LEVEL_READ_ERROR` and carries its code out in `r->Error`, because "the read
 * failed" and "there is nothing there" are the same distinction this function
 * exists to make, one level down. Answering the first with the second would
 * recreate the defect in the place the fix was applied.
 *
 * (This is `get_dword` widened. The type check it carried is unchanged and is
 * the same trap `read_string` was given one for; what changed is that its ways
 * of failing now reach the caller instead of being answered with a 0.)
 */
static int read_level(const char *keyPath, const char *value, LEVEL_READ *r)
{
    HKEY key;
    DWORD type;
    DWORD data;
    DWORD bytes;
    LONG rc;

    r->Type = 0;
    r->Bytes = 0;
    r->Error = ERROR_SUCCESS;

    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_QUERY_VALUE, &key);
    if (rc != ERROR_SUCCESS) {
        r->Error = rc;
        return LEVEL_READ_NOKEY;
    }
    type = 0;
    data = 0;
    bytes = sizeof(data);
    rc = RegQueryValueExA(key, value, NULL, &type, (BYTE *)&data, &bytes);
    RegCloseKey(key);
    /*
     * `ERROR_MORE_DATA` is a value too long for four bytes. That is PRESENT and
     * undecodable, not absent - the same reading `value_exists` takes of it,
     * and the one that matters, since usbport would copy four bytes out of it
     * regardless.
     */
    if (rc != ERROR_SUCCESS && rc != ERROR_MORE_DATA) {
        r->Error = rc;
        return (rc == ERROR_FILE_NOT_FOUND) ? LEVEL_READ_ABSENT
                                            : LEVEL_READ_ERROR;
    }
    r->Type = type;
    r->Bytes = bytes;
    if (rc != ERROR_SUCCESS || type != REG_DWORD || bytes != sizeof(data)) {
        /*
         * **A `REG_DWORD` of the wrong LENGTH lands here too**, which is why
         * the caller's message says "not a four-byte REG_DWORD" rather than
         * "not a REG_DWORD" - the latter contradicts the `type 4` printed
         * beside it. Both halves are reported, so the reader can see which
         * of the two it is.
         */
        return LEVEL_READ_UNREADABLE;
    }
    r->Value = data;
    return LEVEL_READ_OK;
}

/*
 * **One command, one value, one knob** - all three structural since the
 * snapshot-value merge rather than requirements on this function. There is only
 * `XhciLogVerbosity` to set, and `-verbosity N` is the only thing that sets it.
 * The published sequence is four steps and `regedit` appears nowhere in it: set
 * the level, restart, reproduce, `XHCISNAP -o C:\NAME`.
 *
 * **`-enable` was retired, because it wrote a level the user had
 * not named.** It existed to spare a user setting two values in one command;
 * the merge deleted the second value, and what was left was a flag that raised
 * a 0 to 2, rewrote an out-of-range value to 2, and never lowered. The rewrite
 * was the weaker half: it overwrote a value this tool did not set, on the
 * strength of this tool's own hard-coded copy of the driver's range. The tool
 * already duplicates the driver's WIRE FORMAT for want of a shared header, and
 * that made the driver's POLICY a second thing that can drift - **a reported
 * out-of-range value cannot drift; a silently corrected one can.**
 *
 * **So the range knowledge survives as a MESSAGE and not as a decision.** The
 * loop below reads each key's current level and prints it beside the write,
 * naming an out-of-range one as the wasted boot it would otherwise cause. The
 * overwrite that follows is one the user asked for by name.
 *
 * **What survives of the per-key decision is `may_write` plus that per-key
 * report - not a never-lowers rule under a new name.** `-verbosity N` sets
 * every writable key to exactly N, up or down: a two-controller machine sitting
 * at 4 and 0 becomes 2 and 2, which is correct because the user named 2. The
 * prior-value print is what makes the drop visible rather than silent.
 *
 * `-verbosity 0` reaches this function too, and it is `-disable` spelled
 * differently - see the aftercare paragraph at the end.
 */
static int set_verbosity(unsigned long verbosity, int force)
{
    int i;
    int ok;
    int written;
    int dropped;
    int anyOn;

    if (find_driver_keys() == 0) {
        if (report_dropped_matches()) {
            /* Not "no key" - keys were found and could not be held. Saying the
             * INF never ran would be a false statement about the machine. */
            return 1;
        }
        /*
         * **`HKLM\` is in the heading and not on the path lines**, because the
         * NT class path plus that prefix is 84 characters and a GUID is one
         * unbreakable token: the wrapper will not split a path - a path split
         * across two lines cannot be typed back in - so those five characters
         * are the difference between a line that fits and one the console
         * breaks in the middle of the GUID.
         */
        printf("\nNo xhci98 driver key found under either of these, in HKLM:\n");
        printf("  %s\n", XHCI_CLASS_NT);
        printf("  %s\n", XHCI_CLASS_9X);
        say("", "That means this driver's INF has never run on this machine - a "
                "binary copied in by hand has no software key to read values "
                "from, and the driver starts with everything off, which is "
                "correct behaviour rather than a fault. Install from the media.");
        return 1;
    }

    print_key_header();
    /*
     * **Reported here rather than at the end, so it runs on every path.** It
     * used to be called only on the success return, which meant a run that also
     * failed to write - or found nothing eligible - exited without ever
     * mentioning the keys it could not hold. Its verdict is kept for the exit
     * status; printing it beside the listing is also what makes "missing from
     * the list above" true.
     */
    dropped = report_dropped_matches();

    ok = 1;
    written = 0;
    anyOn = 0;
    for (i = 0; i < driverKeyCount; i++) {
        LEVEL_READ prior;
        char was[64];
        const char *detail;
        int showErr;
        int thisOk;

        if (!may_write(i, force)) {
            say_hang("  ", "             ",
                     "%-8s SKIPPED - identified by a value NAME alone, so it "
                     "is not written without -force.", key_label(i));
            continue;
        }

        /*
         * **The prior value is read and reported, on every key and before every
         * write**, and the failures are told apart rather than answered with a
         * 0. Each reading says something different:
         *
         *   - a drop (4 to 2) is what the user asked for, and should still be
         *     visible rather than silent;
         *   - an OUT OF RANGE value is a level the driver will refuse rather
         *     than clamp, falling back to its default, which is OFF - so the
         *     next start off this key would answer nothing;
         *   - an UNREADABLE value is the dangerous one, and it is the reason
         *     these are several answers and not one. See read_level: usbport
         *     does not check the type, so a value this tool refuses to decode
         *     can still read as a level and OPEN the channel. Reporting it as
         *     "not set" would say the door was shut on a machine whose door is
         *     open.
         *
         * **Every one of these is phrased about the NEXT start, never about
         * what a running driver did.** The value is read once per start, so a
         * key edited since the last boot has been read by nothing, and a
         * running controller may be sitting at a level this value no longer
         * says. The registry cannot tell those apart and this report must not
         * pretend otherwise - the applied level travels in a dump's header,
         * which is the only place it is actually known.
         *
         * Saying so is all this tool does about any of them: the value that
         * goes in afterwards is the one the command named.
         */
        prior.Value = 0;
        prior.Error = 0;
        detail = NULL;
        showErr = 0;
        switch (read_level(driverKeys[i], "XhciLogVerbosity", &prior)) {
        case LEVEL_READ_OK:
            if ((unsigned long)prior.Value > XHCISNAP_LEVEL_MAX) {
                sprintf(was, "was %lu OUT OF RANGE",
                        (unsigned long)prior.Value);
                detail = "The driver refuses an out-of-range level rather than "
                         "clamping it, so a start reading that value applies "
                         "the default, which is OFF. Whether any start has read "
                         "it yet is not knowable from the registry - it is read "
                         "once per start.";
            } else {
                sprintf(was, "was %lu", (unsigned long)prior.Value);
            }
            break;
        case LEVEL_READ_ABSENT:
            strcpy(was, "was unset");
            break;
        case LEVEL_READ_NOKEY:
            strcpy(was, "*** UNREADABLE");
            showErr = 1;
            detail = "This key would not open to be read, though it opened a "
                     "moment ago when it was identified. What it held is NOT "
                     "known.";
            break;
        case LEVEL_READ_ERROR:
            strcpy(was, "*** UNREADABLE");
            showErr = 1;
            detail = "XhciLogVerbosity could not be read. Whether it is set, "
                     "and to what, is NOT known - which is not the same as its "
                     "being absent.";
            break;
        default:
            /* The type and the length are the reading here, so they go on the
             * key's own line rather than into the prose under it - a review
             * round put them there and compressing the report must not spend
             * them. */
            sprintf(was, "*** type %lu, %lu bytes",
                    (unsigned long)prior.Type, (unsigned long)prior.Bytes);
            detail = "That is not a four-byte REG_DWORD. What it held is NOT "
                     "known and this tool will not guess: "
                     "the driver's reader checks neither the type nor the "
                     "length and takes four bytes either way, so a value like "
                     "this can read as a level and OPEN the channel. The write "
                     "replaces it with a proper REG_DWORD, which ends the "
                     "ambiguity.";
            break;
        }

        /*
         * **One line, and the write's outcome is on it.** The line is built
         * after the write rather than before it so it can say what happened
         * instead of what was attempted.
         */
        thisOk = set_dword(driverKeys[i], "XhciLogVerbosity",
                           (DWORD)verbosity) ? 1 : 0;

        if (!thisOk) {
            ok = 0;
            say_hang("  ", "             ", "%-8s %-19s -> WRITE FAILED   %s",
                     key_label(i), was, key_why_short(i));
        } else {
            say_hang("  ", "             ", "%-8s %-19s -> %lu   %s",
                     key_label(i), was, verbosity, key_why_short(i));
            if (verbosity != XHCISNAP_LEVEL_OFF) {
                anyOn = 1;
            }
        }
        if (detail != NULL) {
            say("      ", "%s", detail);
        }
        if (showErr) {
            say("      ", "(error %ld)", prior.Error);
        }
        written++;
    }

    /* After the lines rather than before them: it refers to keys the reader has
     * now seen, and on a healthy machine it prints nothing at all. */
    explain_weak(force);

    if (!ok) {
        /*
         * **Switching OFF gets the stronger sentence**, because the two
         * failures are not equally bad: a level that did not go up is a
         * diagnostic you do not get, and a level that did not go down is a
         * channel left open to anyone using the machine. Since the merge, this
         * value is also the access control.
         */
        if (verbosity == XHCISNAP_LEVEL_OFF) {
            say("", "\n*** NOT switched off. The value could not be written on "
                    "some key, so the driver will go on reading it as it is - "
                    "and while it is nonzero the read channel stays open to "
                    "anyone using this machine. Administrator rights are needed "
                    "on Windows 2000; Windows 98 needs none.");
        } else {
            say("", "\nSome values could not be written. Administrator rights "
                    "are needed on Windows 2000; Windows 98 needs none.");
        }
        return 1;
    }
    if (written == 0) {
        printf("\nNothing was written - every key found needs -force. See "
               "above.\n");
        return 1;
    }

    /*
     * Two lines rather than one: the rung and the ladder together are 91
     * characters, which wrapped to a twelve-character orphan - `+everything)`
     * alone on a line. Wrapping correctly is not the same as reading well, and
     * where a line is a hair too long the fix is the line, not the wrapper.
     */
    say("", "\nlevel %lu = %s", verbosity, level_name(verbosity));
    say("  ", "ladder: 0 off, 1 counters, 2 +note ring, 3 +PORTSC, "
              "4 +everything");
    say("", "\n%d key(s) set. RESTART THE MACHINE, or disable and re-enable the "
            "controller in Device Manager: the driver reads this once per start "
            "and nothing re-reads it while it is running.", written);
    /*
     * **`-verbosity 0` reaches this function too**, and it is `-disable` spelled
     * differently - so everything after the restart is conditional on the run
     * having left something ON. Telling somebody who has just switched the
     * channel off to go and reproduce a problem and then switch it off would
     * read as this tool not knowing what it had just done.
     */
    if (anyOn) {
        printf("Then reproduce and run:  XHCISNAP -o C:\\NAME\n");
        printf("Afterwards:              XHCISNAP -disable\n");
        say("  ", "While the channel is on, anyone using this machine can read "
                  "this driver's diagnostic state through it. Windows' USB port "
                  "driver owns that door and opens it to anyone, so this value "
                  "IS the lock.");
    } else {
        /* **Not "and what -disable does"**, which is what this said until the
         * two spellings became one path: a user who typed `-disable` was being
         * told that -disable does what -disable does. It explains the level,
         * not the flag. */
        say("", "The channel is now OFF on every key set above - the driver "
                "answers this tool exactly as a build without the channel "
                "would, which is what the INF ships.");
    }
    /* A run that covered part of the machine did not do what it was asked, so
     * it does not exit as though it had. */
    return dropped ? 1 : 0;
}

/* ---- main --------------------------------------------------------------- */

/* HCSPARAMS1.MaxPorts is eight bits, so this is the whole address space. */
#define MAX_PORTS 255

static unsigned char portsc_values[MAX_PORTS * 4];

/*
 * The whole extension, kept in memory as well as written to the `.BIN`, so the
 * companion can print the note ring out of it. 128 KB is comfortably past the
 * 90,272 bytes this was built against and still a single static allocation on a
 * machine with 64 MB of RAM - which is what a Windows 98 SE target is. A driver
 * whose extension outgrows it is reported rather than truncated: an image cut
 * short would give a rotated ring, which is a wrong reading and not a failed
 * one.
 */
#define EXT_IMAGE_MAX 131072UL

static unsigned char ext_image[EXT_IMAGE_MAX];

/*
 * **The short usage is 22 lines and that is a hard budget, not a target.**
 * *(Project owner: "since Win98 console cannot scroll, I think we
 * need to make best effort to keep everything within one vertical screen.
 * Especially for the initial screen if no or wrong command line arguments are
 * specified.")*
 *
 * This was 58 lines. A Windows 98 DOS box is 25 rows with no scrollback worth
 * the name, so **the top 34 rows were unreachable** - and what was unreachable
 * was the syntax, which is precisely what somebody who has just typed a wrong
 * flag needs. What survived on screen was the tail. The long form is not
 * deleted, it moves behind `-help`, which a user asks for deliberately and can
 * pipe through MORE.
 *
 * **The budget is 22 rather than 25** so that the command the user typed, the
 * blank line after it and the returning prompt all fit with the text - a screen
 * that fits only when invoked from a script is not one that fits.
 */
static void usage(void)
{
    printf("xhcisnap %s   built %s\n", XHCISNAP_VERSION, XHCISNAP_BUILT);
    printf(
"read xhci98.sys's own log off a running machine, from user mode.\n"
"\n"
"  xhcisnap -dump [-c N] [-o BASE]   dump: BASE.TXT (send this), .BIN, .PSC\n"
"  xhcisnap -verbosity N      set the level 0-4 on every xhci98 key, RESTART\n"
"  xhcisnap -disable          exactly -verbosity 0.  There is no -enable.\n"
"  xhcisnap -probe            check the ROUTE only, dump nothing\n"
"  xhcisnap -help             the long version - add | MORE, it is a screenful\n"
"\n"
"  -c N     controller index, opens \\\\.\\HCDN (default 0)\n"
"  -o BASE  output basename (default XHCISNAP)\n"
"  -force   write to a key matched by value NAME alone; the tool asks first\n"
"\n"
"THE FOUR STEPS, and none of them is regedit:\n"
"  1. xhcisnap -verbosity 2     0 off, 1 counters, 2 +note ring THE LOG,\n"
"  2. restart the machine       3 +PORTSC table, 4 +everything\n"
"  3. reproduce the problem\n"
"  4. xhcisnap -o C:\\MYDUMP     then send MYDUMP.TXT\n"
"\n"
"The channel ships OFF in every build, so step 1 is not optional.  Run\n"
"xhcisnap -disable once you have sent the capture: while it is on, anyone\n"
"using this machine can read this driver's diagnostic state through it.\n");
}

/*
 * Everything the short usage had to give up. Reached only by `-help`, so it may
 * run past a screen - and it says so at the top of the short one rather than
 * leaving a user to discover it.
 */
static void usage_long(void)
{
    printf("xhcisnap %s   built %s\n", XHCISNAP_VERSION, XHCISNAP_BUILT);
    printf(
"read xhci98.sys's own log and PORTSC off a running machine, from user mode,\n"
"through usbport's PassThru escape.\n"
"\n"
"EVERY ARGUMENT.  With none at all you get the short usage, not a dump - a\n"
"program's name typed to find out what it is should not write three files.\n"
"\n"
"  -dump         take a dump with the defaults.  Implied by -c and -o, so it\n"
"                is only needed when neither is given.  Writes BASE.BIN,\n"
"                BASE.PSC and BASE.TXT, and prints the RESOLVED absolute path\n"
"                they went to.  Read it: a dump taken onto a QEMU transfer\n"
"                volume is gone at power-off, and the path is what tells you\n"
"                that before you go looking for the file.\n"
"  -c N          controller index; opens \\\\.\\HCDN.  Default 0.  usbport\n"
"                publishes its link at a fixed index with no retry, so a\n"
"                machine whose own USB stack already owns HCD0 leaves this\n"
"                driver at 1 or 2 - try those before concluding anything.\n"
"  -o BASENAME   output basename.  Default XHCISNAP, in the CURRENT\n"
"                directory - give a full path (-o C:\\MYDUMP) and you always\n"
"                know where it went.\n"
"  -help, -?, /? this text.  Add | MORE - it is longer than a screen, which\n"
"                the short usage is deliberately not.\n"
"  -verbosity N  set XhciLogVerbosity to exactly N, 0-4, up or down, in the\n"
"                driver's software key on every xhci98 controller this machine\n"
"                has.  regedit is not needed.  The ladder is:\n"
"                  0  OFF - the driver does not answer this tool at all\n"
"                  1  counters only     (the channel, at its cheapest)\n"
"                  2  + the note ring   THE LOG.  USE THIS ONE.\n"
"                  3  + the PORTSC table in BASENAME.TXT\n"
"                  4  + everything, internal addresses included\n"
"                Each key's PREVIOUS level is printed beside the write, so a\n"
"                level you did not expect - or one out of range, which the\n"
"                driver refuses outright and reads as OFF - is visible.\n"
"                The report is about the NEXT start: the driver reads this\n"
"                value once per start, so a key edited since the last boot\n"
"                has been read by nothing.  The level actually in force\n"
"                travels in a dump's header, which is where to read it.\n"
"  -disable      exactly -verbosity 0: put XhciLogVerbosity back to 0, which\n"
"                is what the INF ships.  THERE IS NO -enable: 'off' means one\n"
"                thing and 'on' means four, so you name the level you want.\n"
"  -force        write to a driver key that was identified by a value NAME\n"
"                alone.  Only needed on a machine whose key has been edited\n"
"                by hand; the tool says so when it wants this.\n"
"  -probe        four controls on the route, for when nothing came back.\n"
"\n"
"WHAT A DUMP WRITES:\n"
"  BASENAME.BIN  the raw extension image.  Send this to the maintainer - it\n"
"                decodes only against an offsets.txt from the driver's own\n"
"                tree, which is why it is an attachment and not the report.\n"
"  BASENAME.PSC  the raw PORTSC array.\n"
"  BASENAME.TXT  the plain-text report.  THIS is the one to paste into an\n"
"                issue: it is built entirely from what the driver puts on the\n"
"                wire, so it needs no offset table.  Below verbosity 4 it\n"
"                carries no internal addresses, because the driver refuses to\n"
"                record one at those levels.\n"
"\n"
"WHY step 1 is not optional: the channel is in EVERY build of this driver and\n"
"is switched OFF by default.  A driver that has it switched off answers\n"
"exactly as one built without it - that is deliberate, and it is why -probe\n"
"cannot tell you which of the two you have.\n"
"\n"
"AND A FIFTH STEP THAT IS NOT ONE OF THE FOUR:  xhcisnap -disable , once you\n"
"have sent the capture.  While the channel is on, anyone using this machine\n"
"can read this driver's own diagnostic state through it.  This driver cannot\n"
"put a lock on that door - it belongs to Windows' USB port driver, which opens\n"
"it to anyone - so the value you set IS the lock.\n");
}

int main(int argc, char **argv)
{
    /*
     * **Two headers, and they are two on purpose.** `extLast` is the extension
     * region's final window and `portscLast` is the PORTSC region's. The first
     * version kept one and let the PORTSC call overwrite it - so the companion
     * unwrapped the SAVED extension bytes using the LATER request's `RingHead`
     * and `RingUsed`, and any record appended between the two calls rotated the
     * printed ring or ran it past what was captured. The ring's geometry has to
     * come from the same window its bytes did.
     */
    SNAP_HEADER extLast;
    SNAP_HEADER portscLast;
    int companionWasWritten;
    HANDLE device;
    char devicePath[32];
    char extPath[MAX_PATH];
    char portscPath[MAX_PATH];
    char textPath[MAX_PATH];
    char extTmpPath[MAX_PATH];
    char portscTmpPath[MAX_PATH];
    unsigned long controller;
    unsigned long tearFirst;
    unsigned long tearLast;
    unsigned long extBytes;
    unsigned long portscBytes;
    unsigned long verbosity;
    const char *base;
    int tearTorn;
    int tearSeen;
    int probeOnly;
    int doSetLevel;
    int doDisable;
    int force;
    int i;

    controller = 0;
    base = "XHCISNAP";
    probeOnly = 0;
    doSetLevel = 0;
    doDisable = 0;
    force = 0;
    /* Never used unless `doSetLevel` says a level was named; seeded to the
     * shut door rather than to a sentinel, since there is no longer a
     * "set it to something" form for a sentinel to mean. */
    /*
     * **A bare invocation prints the usage rather than taking a dump.**
     * *(Project owner: "when xhcisnap is run without command line
     * arguments, I see a wall of text. Perhaps it should be the same screen as
     * wrong command line argument.")* Two reasons, and the second is the one
     * that would otherwise be discovered by somebody it annoyed:
     *
     *   1. "no idea what you want" and "wrong idea" deserve the same answer,
     *      and that answer is the syntax.
     *   2. **Typing a program's name to find out what it is should not write
     *      three files into the current directory.** That is what it did.
     *
     * Nothing published moves: the four-step sequence is `-o C:\MYDUMP`, which
     * carries an argument. `-dump` is what a dump with every default is called
     * now, so the one-word form still exists - it just has to be asked for.
     */
    if (argc == 1) {
        usage();
        return 2;
    }

    verbosity = XHCISNAP_LEVEL_OFF;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            if (!parse_ulong(argv[++i], &controller)) {
                printf("-c takes a controller number and nothing else. '%s' "
                       "is not one, and this\ntool will not guess: on a "
                       "machine with more than one controller a misread\n"
                       "number dumps the wrong one while looking honoured.\n",
                       argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            base = argv[++i];
            /*
             * **Bounded before it is used, because three fixed buffers are
             * built out of it.** `sprintf` into a MAX_PATH stack local is a
             * stack overflow waiting for a long argument, and a command line
             * can carry one on Windows 2000 - which would corrupt the stack
             * before a single window had been taken. `- 12` leaves room for
             * the longest name this tool builds - `.BIN.TMP`, eight characters
             * - plus its NUL with margin (it was `- 8` while the longest was
             * `.BIN`).
             */
            if (strlen(base) > (size_t)(MAX_PATH - 12)) {
                printf("the output basename is too long - at most %d "
                       "characters, and this tool adds .BIN, .PSC and .TXT to "
                       "it (and .BIN.TMP while a dump is being taken).\n",
                       MAX_PATH - 12);
                return 2;
            }
        } else if (strcmp(argv[i], "-dump") == 0) {
            /* No state: taking a dump is what this program does when it is not
             * asked for anything else. The flag exists so that a dump with
             * every default can still be ASKED for - see the argc test in
             * main, which stopped a bare invocation from writing three files
             * as the side effect of somebody finding out what this is. */
        } else if (strcmp(argv[i], "-help") == 0 ||
                   strcmp(argv[i], "-?") == 0 ||
                   strcmp(argv[i], "/?") == 0) {
            /* `/?` too, because that is what a DOS user types first. It exits
             * 0: asking for help and getting it is not an error. */
            usage_long();
            return 0;
        } else if (strcmp(argv[i], "-probe") == 0) {
            probeOnly = 1;
        } else if (strcmp(argv[i], "-disable") == 0) {
            doDisable = 1;
        } else if (strcmp(argv[i], "-force") == 0) {
            /* Only ever consulted for a key matched by value NAME alone -
             * see may_write(). It is not a general "do it anyway". */
            force = 1;
        } else if (strcmp(argv[i], "-verbosity") == 0 && i + 1 < argc) {
            /*
             * **This is the only knob** (since the merge, and `-enable` retired the
             * same day). It used to set a second value, `XhciLogSnapshot`,
             * since a ladder set on a channel nobody opened would have recorded
             * faithfully into a ring nothing could read. That value is now rung
             * 0 of this one, so setting the level IS opening the channel - and
             * `-verbosity 0` is `-disable` spelled differently rather than an
             * enable that shuts the door behind itself.
             *
             * `doSetLevel` means "a level was named", and it is what selects
             * `set_verbosity`'s key-finding and write path.
             *
             * A negative argument arrives as a large unsigned value and is
             * refused by the same bound, which is why the compare is against
             * the ladder's top rather than a signed range.
             */
            /*
             * **Parsed strictly, and `atoi` is not strict enough for what this
             * value now is.** `atoi` stops at the first character it cannot use
             * and reports nothing about it, so `-verbosity full` became 0 and
             * silently switched the channel OFF while the user believed they
             * had switched it on, and `-verbosity 4oops` became 4 and opened
             * the address-bearing tier. That was tolerable while this was a
             * depth knob; it is not now that the same value is the channel's
             * access control. A whole-argument parse with at least one digit
             * and nothing after it is the only reading that cannot mean
             * something the user did not type. (Codex review of `29c311f`.)
             * `parse_ulong` is that reading, and since the post-Phase 13 review rounds `-c` takes
             * the same one - see the parser's own comment for what `atoi`
             * did there.
             */
            if (!parse_ulong(argv[++i], &verbosity)) {
                printf("-verbosity takes a number and nothing else, 0-%lu. "
                       "'%s' is not one, and this tool\nwill not guess: the "
                       "value it sets is what opens the channel as well as "
                       "what sets\nthe level, so a misread argument would "
                       "switch a diagnostic on or off silently.\n",
                       XHCISNAP_LEVEL_MAX, argv[i]);
                return 2;
            }
            if (verbosity > XHCISNAP_LEVEL_MAX) {
                printf("the ladder is 0-%lu. The driver REFUSES anything else "
                       "rather than clamping it,\nso a 7 here would not become "
                       "a %lu - it would apply the default, which is OFF.\n",
                       XHCISNAP_LEVEL_MAX, XHCISNAP_LEVEL_MAX);
                return 2;
            }
            doSetLevel = 1;
        } else {
            usage();
            return 2;
        }
    }

    if (doSetLevel && doDisable) {
        printf("-verbosity and -disable are opposites. Pick one.\n");
        return 2;
    }

    /*
     * The registry half runs without opening the device at all, and that is
     * what makes the bootstrap non-circular: a machine whose channel is off
     * cannot be read, but it can always be switched on and restarted.
     */
    /*
     * **`-disable` IS `-verbosity 0`** - one write path, two spellings, since
     * the merge. They were two functions with half the behaviour each until
     * then; see the merge note in `set_verbosity`. The alias is kept rather
     * than retired alongside `-enable` because the two are not mirrors: `-enable`
     * had to guess which of four "on" levels the user meant, and **"off" has
     * exactly one meaning**, so the objection that retired it does not reach
     * this. It is also the aftercare step the security posture depends on
     * somebody actually running, and a name is more likely to be run than a
     * number.
     */
    if (doDisable) {
        return set_verbosity(XHCISNAP_LEVEL_OFF, force);
    }
    if (doSetLevel) {
        return set_verbosity(verbosity, force);
    }

    sprintf(devicePath, "\\\\.\\HCD%lu", controller);
    sprintf(extPath, "%s.BIN", base);
    sprintf(portscPath, "%s.PSC", base);
    sprintf(textPath, "%s.TXT", base);
    sprintf(extTmpPath, "%s.TMP", extPath);
    sprintf(portscTmpPath, "%s.TMP", portscPath);

    printf("xhcisnap: opening %s\n", devicePath);
    /*
     * FILE_ANY_ACCESS, and usbport completes IRP_MJ_CREATE with no work, so no
     * particular access mask is needed - but ask for read/write anyway, since
     * that is what every other HCD-opening tool of the era did and there is
     * nothing to be gained by being the unusual one.
     */
    device = CreateFileA(devicePath, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        printf("  cannot open %s, error %lu\n", devicePath,
               (unsigned long)GetLastError());
        printf("  (that name is created by usbport's HCD FDO; if it does not\n"
               "   exist, no xHCI or EHCI controller is started on this "
               "machine)\n");
        return 1;
    }

    if (probeOnly) {
        i = probe_route(device);
        CloseHandle(device);
        return i;
    }

    memset(&extLast, 0, sizeof(extLast));
    memset(&portscLast, 0, sizeof(portscLast));
    tearFirst = 0;
    tearLast = 0;
    tearTorn = 0;
    tearSeen = 0;

    printf("\nextension:\n");
    extBytes = dump_region(device, SNAP_REGION_EXTENSION, extTmpPath,
                           &extLast,
                           &tearFirst, &tearLast, &tearTorn, &tearSeen,
                           ext_image, sizeof(ext_image));
    if (extBytes == (unsigned long)-1) {
        /* The partial .BIN.TMP is already gone; the previous .BIN, .PSC and
         * .TXT are untouched and still a set. */
        CloseHandle(device);
        return 1;
    }
    if (extBytes != extLast.ExtensionBytes) {
        printf("  *** WARNING: %lu bytes came back but the driver says the "
               "extension is %lu. Do not decode this dump.\n",
               extBytes, extLast.ExtensionBytes);
    }

    printf("\nPORTSC:\n");
    portscBytes = dump_region(device, SNAP_REGION_PORTSC, portscTmpPath,
                              &portscLast,
                              &tearFirst, &tearLast, &tearTorn, &tearSeen,
                              portsc_values, sizeof(portsc_values));
    CloseHandle(device);
    if (portscBytes == (unsigned long)-1) {
        DeleteFileA(extTmpPath);
        return 1;
    }

    /*
     * Both regions are in hand: publish them as a set. If either rename fails
     * neither final is left standing, so what remains is loud (no dump) rather
     * than mixed (a new .BIN beside an old .PSC). The old .TXT goes at the
     * same moment - the report for the previous set must not sit beside this
     * one's raw files, which it would if the fopen below failed.
     */
    if (!publish_region(extTmpPath, extPath)
        || !publish_region(portscTmpPath, portscPath)) {
        DeleteFileA(extTmpPath);
        DeleteFileA(portscTmpPath);
        DeleteFileA(extPath);
        DeleteFileA(portscPath);
        printf("  no dump was published\n");
        return 1;
    }
    DeleteFileA(textPath);

    /*
     * The companion is opened here, after both regions are published, so a
     * run that failed halfway leaves no half-written report to be pasted into
     * an issue. Everything `comp()` writes from now on goes to the FILE when
     * there is one and to the screen only when there is not - it is one
     * destination, not both *(this comment said "to the screen and to the
     * file at once" until the post-Phase 13 review rounds, which `comp()` never did)*. The one
     * thing the bench must see whatever the level is the PORTSC table, and
     * that is printed to the screen explicitly below.
     */
    companion = fopen(textPath, "w");
    if (companion == NULL) {
        printf("\n  *** cannot create %s - the report will be on screen only, "
               "and on real\n      silicon it will scroll off. Name somewhere "
               "writable with -o.\n", textPath);
        if (GetFileAttributesA(textPath) != 0xFFFFFFFFUL) {
            printf("      an OLDER %s is still there and could not be removed "
                   "- it is not\n      this dump's report; do not send it as "
                   "one.\n", textPath);
        }
    }

    /*
     * The EXTENSION window's header, not the PORTSC one: the ring geometry
     * below has to describe the bytes actually saved in `ext_image`.
     */
    write_companion_header(&extLast);

    /*
     * **The tier is a publication line, not a transport one.** The driver
     * served both regions whole whatever the level says - one gate, one owner -
     * and what the level decides is what a maintainer may reasonably ask a
     * stranger to paste into a public issue. The `.BIN` beside this file has
     * everything either way.
     */
    if (extLast.VerbosityApplied >= XHCISNAP_LEVEL_LOG) {
        if (extBytes > sizeof(ext_image)) {
            comp("\nnote ring: the extension is %lu bytes and this build keeps "
                 "%lu, so the image\n  in memory is short and the ring is not "
                 "printed. The .BIN is complete.\n",
                 extBytes, (unsigned long)sizeof(ext_image));
        } else {
            write_companion_ring(&extLast, ext_image, extBytes);
        }
    } else {
        comp("\nnote ring: not printed - the driver was at verbosity %lu, "
             "which records\n  nothing into it. That is a real report, not a "
             "failure; -verbosity 2 fills it.\n", extLast.VerbosityApplied);
    }

    if (portscBytes != 0) {
        /*
         * **The table reaches the screen at every level**, because that is
         * what the bench reads on the spot - `comp()` writes to one
         * destination, so it is printed once with the companion set aside,
         * and then, from the PORTSC rung up, once more into the file. Below
         * that rung it stays out of the file: it carries no kernel address,
         * so this is a judgement about how much a stranger should be asked to
         * paste rather than a safety rule. *(Until a later review the level-3-and-
         * up branch called `print_portsc` with the companion open, so on a
         * real console the table went into the file and never appeared on the
         * screen - the opposite of what this comment, the README and the
         * instrument document all said.)*
         */
        FILE *saved = companion;

        companion = NULL;
        print_portsc(portsc_values, portscBytes / 4);
        companion = saved;
        if (companion != NULL) {
            if (extLast.VerbosityApplied >= XHCISNAP_LEVEL_PORTSC) {
                print_portsc(portsc_values, portscBytes / 4);
            } else {
                comp("\nPORTSC table: on screen only - it goes into this file "
                     "at verbosity 3 and above.\n");
            }
        }
    }

    /*
     * Said last and said plainly. The extension needs several windows and the
     * driver runs between them, so a dump can be internally inconsistent; that
     * has to reach the operator at the bench rather than be discovered by
     * whoever decodes the file a week later.
     */
    /*
     * **What equality proves and what it does not**, said in the file rather
     * than left to a reader's assumption. The detector is a sum of four
     * monotonic counters - usbport's health check, the interrupt DPC, and both
     * halves of the log's producer accounting - so it moves for everything that
     * ordinarily runs. It is not a counter behind every field, so:
     *
     *   unequal  PROVES the dump is torn.
     *   equal    is strong evidence and not a proof.
     *
     * The earlier wording said "the dump is coherent" flatly, which claimed the
     * second as if it were the first.
     */
    if (tearTorn) {
        comp_wrapped("", "\ncoherence: tear detector %lu -> %lu (+%lu), *** "
                         "CHANGED between windows - the controller was serviced "
                         "while this dump was taken, so any counter in it may "
                         "be a mixture. The detector counts health-check calls, "
                         "interrupt DPCs and both halves of the log's producer "
                         "accounting, so the size of that step is how much ran.",
                     tearFirst, tearLast, tearLast - tearFirst);
    } else {
        comp_wrapped("", "\ncoherence: tear detector %lu, unchanged across "
                         "every window. Nothing this driver counts moved while "
                         "the dump was taken, which is strong evidence the dump "
                         "is coherent - it is not a proof, because there is no "
                         "counter behind every field.",
                     tearFirst);
    }

    companionWasWritten = (companion != NULL) ? 1 : 0;
    if (companion != NULL) {
        fclose(companion);
        companion = NULL;
    }

    /*
     * **The last six lines are the only ones guaranteed to be on the screen**,
     * and that is why this summary exists at all. A dump prints its header,
     * then the ring, then the PORTSC decode - and on a real controller the
     * PORTSC table alone is one row per port, so on the E460 the header block
     * scrolls off a 25-row DOS box before the command finishes. In a console
     * with no scrollback the tail is the whole of what a user can read, so what
     * a maintainer will ask them first has to be IN the tail: which build, what
     * level was actually applied, whether the ring has anything in it, and what
     * to send. It is a repeat rather than a move, because the bench reads the
     * header in place when it can.
     *
     * It is `printf` and not `comp`: it goes to the screen only, and since
     * the merge the report itself goes to the file only. The companion has
     * all of this above,
     * where a file's reader can scroll.
     */
    printf("\n--- summary ------------------------------------------------\n");
    printf("  tool       xhcisnap %s, built %s\n",
           XHCISNAP_VERSION, XHCISNAP_BUILT);
    printf("  driver     %s, extension %lu bytes, schema %lu\n",
           flavour_text(extLast.Flavour), extLast.ExtensionBytes,
           extLast.SchemaVersion);
    printf("  verbosity  read %lu, APPLIED %lu\n",
           extLast.VerbosityRead, extLast.VerbosityApplied);
    printf("  note ring  %lu of %lu bytes%s\n", extLast.RingUsed,
           extLast.RingBytes,
           (extLast.RingUsed == 0) ? "   EMPTY - the report says why" : "");
    printf("  coherence  %s\n",
           tearTorn ? "*** TORN - counters may be a mixture" : "no tearing");
    if (companionWasWritten) {
        /*
         * **The path is resolved, not echoed.** With no `-o` the basename is
         * bare, so the files land in whatever the current directory happens to
         * be - and the tool was reporting the bare name back, which tells a
         * user nothing about where to look. On these targets that is not
         * cosmetic: a dump taken while sitting on the QEMU transfer disk goes
         * to a volume mounted `snapshot=on` and is gone at power-off, with
         * every message still saying it was written. Print where it actually
         * is.
         *
         * **A drive-kind check stood here and was deliberately removed** (task
         * 13-L.6): vvfat presents as a fixed disk, so `GetDriveType` was silent
         * on the one case the warning existed for, and it would have fired on a
         * perfectly sensible dump to a USB stick. The resolved absolute path is
         * the fix that works. Do not add the check back; the help text said it
         * was here until the snapshot-value merge and that was the defect, not its absence.
         */
        char full[MAX_PATH];
        char fullBin[MAX_PATH];
        char *namePart;

        if (GetFullPathNameA(textPath, sizeof(full), full, &namePart) == 0) {
            strcpy(full, textPath);
        }
        if (GetFullPathNameA(extPath, sizeof(fullBin), fullBin, &namePart) == 0) {
            strcpy(fullBin, extPath);
        }
        printf("  send       %s\n", full);
        printf("             attach %s beside it\n", fullBin);
    }
    return 0;
}
