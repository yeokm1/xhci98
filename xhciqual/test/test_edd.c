/*
 * test_edd.c - host-side unit tests for xhciqual/edd.c, XHCIQUAL --edd
 * (roadmap-usb4.md task 47.24; design record 37 section 4.1 and its Q1).
 *
 * edd_run is driven with fake operations that record every call, so the
 * mode's order is checked as well as its decode: a refusal (or no buffer)
 * makes no INT 13h and no PCI access, every INT 13h is made with the
 * buffer allocated and prepared afresh and after its progress line has been
 * flushed, the buffer is freed once and before the first PCI access, and a
 * unit whose 41h fails gets no 48h. The vectors: SeaBIOS rel-1.17.0's 1Ah
 * answer with no path; a final-layout USB path on PCI 00:14.0 with a good
 * and a bad checksum; the earlier 42h layout; a firmware that writes past
 * the 42h buffer; 41h failures; a failed call holding a valid-looking path;
 * a 4Ah answer that is keyed but invalid beside a valid 42h one; two valid
 * answers that disagree; the size word 1Eh and FFFFh; a final-layout path
 * in a 42h buffer; other interfaces, classes and addresses; the
 * environment refusals. A serial number is planted in the device path of
 * every USB vector and checked absent from every printed line, and the
 * decode is run against a buffer that ends on a guard page.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>     /* VirtualAlloc: the guard page */
#include "../qual.h"

static char out[65536];
static int failures;
static int checks;

static void cap(const char *fmt, ...)
{
    va_list ap;
    size_t used;
    char buf[2048];

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    used = strlen(out);
    if (used + strlen(buf) < sizeof(out))
        strcpy(out + used, buf);
}

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL line %d: %s\n", line, what);
    }
}

static int has(const char *needle)
{
    return strstr(out, needle) != 0;
}

static void check_has(const char *needle, int line)
{
    char what[400];

    sprintf(what, "output has '%.300s'", needle);
    check_impl(has(needle), what, line);
    if (!has(needle))
        printf("  output was:\n%s\n", out);
}
#define HAS(s) check_has((s), __LINE__)

/* Every printed line fits an 80-column screen. */
static void check_widths(void)
{
    const char *p = out;
    const char *nl;
    int ok = 1;

    while (*p) {
        nl = strchr(p, '\n');
        if (nl == 0)
            nl = p + strlen(p);
        if (nl - p > 79) {
            ok = 0;
            printf("  too wide (%d): %.*s\n", (int)(nl - p), (int)(nl - p), p);
        }
        p = (*nl) ? nl + 1 : nl;
    }
    CHECK(ok, "every line at most 79 columns");
}

/* The serial number planted at 38h: 11 22 33 44 55 66 77 88. */
static const u8 serial[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };

static void check_no_serial(void)
{
    CHECK(!has("11 22 33"), "no serial bytes in the dump");
    CHECK(!has("44 55 66"), "no serial bytes in the dump");
    CHECK(!has("1122334455667788"), "no serial as a number");
    CHECK(!has("8877665544332211"), "no serial as a number, reversed");
    CHECK(!has("55667788"), "no serial half");
    CHECK(!has("44332211"), "no serial half, reversed");
}

/* ------------------------------------------------------------------ */
/* Buffers                                                             */
/* ------------------------------------------------------------------ */

static void put16(u8 *b, int at, u16 v)
{
    b[at] = (u8)v;
    b[at + 1] = (u8)(v >> 8);
}

static void put32(u8 *b, int at, u32 v)
{
    put16(b, at, (u16)v);
    put16(b, at + 2, (u16)(v >> 16));
}

/* The 1Ah-byte EDD 1.1 part: a 256 MiB removable stick. */
static void base26(u8 *b, u16 size)
{
    memset(b, 0, 0x4A);
    put16(b, 0x00, size);
    put16(b, 0x02, 0x0006);         /* geometry valid, removable */
    put32(b, 0x04, 0x3FFF);
    put32(b, 0x08, 0);
    put32(b, 0x0C, 0);
    put32(b, 0x10, 0x00080000UL);
    put16(b, 0x18, 512);
    put16(b, 0x1A, 0xFFFF);
    put16(b, 0x1C, 0xFFFF);
}

/* A path from 1Eh: layout final (2Ch) or earlier (24h). */
static void path(u8 *b, u8 len, const char *bus, const char *ifc, u8 pbus,
                 u8 pdev, u8 pfn, int good_sum)
{
    unsigned i, last;
    u8 sum;

    put16(b, 0x1E, 0xBEDD);
    b[0x20] = len;
    memcpy(b + 0x24, bus, 4);
    memcpy(b + 0x28, ifc, 8);
    b[0x30] = pbus;
    b[0x31] = pdev;
    b[0x32] = pfn;
    memcpy(b + 0x38, serial, 8);
    last = (len == 0x2C) ? 0x49 : 0x41;
    b[last] = 0;
    sum = 0;
    for (i = 0x1E; i < last; i++)
        sum = (u8)(sum + b[i]);
    b[last] = (u8)(0x100 - sum);
    if (!good_sum)
        b[last] = (u8)(b[last] + 1);
}

/* ------------------------------------------------------------------ */
/* The fake machine                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int made;
    u16 ah;                     /* status */
    u16 fl;
    const u8 *data;
    unsigned n;                 /* bytes written into the area */
} FTRIAL;

typedef struct {
    int made41;
    u16 ax41, bx41, cx41, fl41;
    FTRIAL t[2];
} FUNIT;

typedef struct {
    u8 bus, dev, fn;
    u16 vid, did;
    u32 cls;
} FFUNC;

static struct {
    int win_made;
    u16 win_ax;
    int dos_made;
    u16 dos_ax, dos_bx;
    int dpmi_made;
    u16 dpmi_ax, dpmi_bx;
    int open_fail;
    FUNIT u[EDD_UNIT_COUNT];
    FFUNC f[4];
    int nf;
    char seq[1024];
    int is_open;
    int opens, closes;
    int bad_prepare, int13_closed, unflushed;
    size_t flushed_at;          /* strlen(out) at the last flush, +1 */
    char flushed_tail[64];      /* the progress text it ended with  */
    u32 writes;
} fk;

static u8 area[EDD_AREA];

static void seq(const char *s)
{
    if (strlen(fk.seq) + strlen(s) < sizeof(fk.seq))
        strcat(fk.seq, s);
}

static int f_win1600(void *c, u16 *ax)
{
    (void)c;
    seq("W");
    if (!fk.win_made)
        return 0;
    *ax = fk.win_ax;
    return 1;
}

static int f_dos3306(void *c, u16 *ax, u16 *bx)
{
    (void)c;
    seq("D");
    if (!fk.dos_made)
        return 0;
    *ax = fk.dos_ax;
    *bx = fk.dos_bx;
    return 1;
}

static int f_dpmi0400(void *c, u16 *ax, u16 *bx)
{
    (void)c;
    seq("V");
    if (!fk.dpmi_made)
        return 0;
    *ax = fk.dpmi_ax;
    *bx = fk.dpmi_bx;
    return 1;
}

static unsigned f_bda(void *c)
{
    (void)c;
    seq("B");
    return 3;
}

static void f_sysinfo(void *c)
{
    (void)c;
    seq("S");
}

static u8 *f_open(void *c)
{
    (void)c;
    seq("O");
    fk.opens++;
    if (fk.open_fail)
        return 0;
    fk.is_open = 1;
    memset(area, 0xEE, sizeof(area));
    return area;
}

static void f_close(void *c)
{
    (void)c;
    seq("C");
    fk.closes++;
    fk.is_open = 0;
}

/* What was printed when the flush came: the call's own progress text,
 * then nothing more until the call. */
static void f_flush(void *c)
{
    size_t n;
    const char *nl;

    (void)c;
    n = strlen(out);
    fk.flushed_at = n + 1;
    nl = strrchr(out, '\n');
    nl = (nl == 0) ? out : nl + 1;
    strncpy(fk.flushed_tail, nl, sizeof(fk.flushed_tail) - 1);
    fk.flushed_tail[sizeof(fk.flushed_tail) - 1] = '\0';
}

static void f_int13(void *c, EDD_REGS *r)
{
    int i, k;
    unsigned size, j;
    FUNIT *u;
    FTRIAL *t;

    char want[64];

    (void)c;
    seq("I");
    if (!fk.is_open)
        fk.int13_closed++;
    if ((r->ax >> 8) == 0x41)
        sprintf(want, "unit %02Xh, function 41h... ", r->dx & 0xFF);
    else
        sprintf(want, "unit %02Xh, function 48h, %02Xh buffer... ",
                r->dx & 0xFF, area[0]);
    if (fk.flushed_at != strlen(out) + 1 || strcmp(fk.flushed_tail, want) != 0)
        fk.unflushed++;
    fk.flushed_at = 0;
    i = (r->dx & 0xFF) - EDD_UNIT_FIRST;
    if (i < 0 || i >= EDD_UNIT_COUNT)
        return;
    u = &fk.u[i];
    if ((r->ax >> 8) == 0x41) {
        if (r->bx != 0x55AA)
            fk.bad_prepare++;
        r->made = u->made41;
        r->dpmi_err = u->made41 ? 0 : 0x8024;
        if (!u->made41)
            return;
        r->ax = u->ax41;
        r->bx = u->bx41;
        r->cx = u->cx41;
        r->flags = u->fl41;
        return;
    }
    seq("8");
    size = area[0] | (area[1] << 8);
    k = (size == EDD_SIZE_FINAL) ? 0 : 1;
    if (size != EDD_SIZE_FINAL && size != EDD_SIZE_EARLY)
        fk.bad_prepare++;
    for (j = 2; j < size; j++)
        if (area[j] != 0)
            fk.bad_prepare++;
    for (j = size; j < EDD_AREA; j++)
        if (area[j] != (u8)(0xA5 ^ (j & 0xFF)))
            fk.bad_prepare++;
    t = &u->t[k];
    r->made = t->made;
    r->dpmi_err = t->made ? 0 : 0x8022;
    if (!t->made)
        return;
    if (t->data != 0)
        memcpy(area, t->data, t->n);
    r->ax = (u16)(t->ah << 8);
    r->flags = t->fl;
}

static int f_scan(void *c, EDD_XHCI *o, int max)
{
    int i, n = 0;

    (void)c;
    seq("P");
    for (i = 0; i < fk.nf && n < max; i++) {
        if (fk.f[i].cls != PCI_CLASS_XHCI)
            continue;
        o[n].bus = fk.f[i].bus;
        o[n].dev = fk.f[i].dev;
        o[n].fn = fk.f[i].fn;
        o[n].vid = fk.f[i].vid;
        o[n].did = fk.f[i].did;
        n++;
    }
    return n;
}

static int f_id(void *c, u8 bus, u8 dev, u8 fn, u16 *vid, u16 *did, u32 *cls)
{
    int i;

    (void)c;
    seq("p");
    for (i = 0; i < fk.nf; i++) {
        if (fk.f[i].bus == bus && fk.f[i].dev == dev && fk.f[i].fn == fn) {
            *vid = fk.f[i].vid;
            *did = fk.f[i].did;
            *cls = fk.f[i].cls;
            return 1;
        }
    }
    *vid = 0xFFFF;
    return 0;
}

static u32 f_writes(void *c)
{
    (void)c;
    seq("w");
    return fk.writes;
}

static EDD_OPS ops = {
    0, f_win1600, f_dos3306, f_dpmi0400, f_bda, f_sysinfo, f_open, f_close,
    f_int13, f_scan, f_id, f_writes, f_flush
};

static EDD_UNIT units[EDD_UNIT_COUNT];

/* The pure DOS of a SeaBIOS machine: no Windows, MS-DOS 7.10, DPMI 0.90
 * returning to real mode; units 80h-87h absent (41h CF=1 AH=01h). */
static void machine(void)
{
    int i;

    memset(&fk, 0, sizeof(fk));
    fk.win_made = 1;
    fk.win_ax = 0x1600;
    fk.dos_made = 1;
    fk.dos_ax = 0x3306;
    fk.dos_bx = 0x0A07;
    fk.dpmi_made = 1;
    fk.dpmi_ax = 0x005A;
    fk.dpmi_bx = 0x0003;
    for (i = 0; i < EDD_UNIT_COUNT; i++) {
        fk.u[i].made41 = 1;
        fk.u[i].ax41 = 0x0100;
        fk.u[i].bx41 = 0x55AA;
        fk.u[i].fl41 = 0x0003;
    }
}

static void unit_edd30(int i)
{
    fk.u[i].ax41 = 0x3000;
    fk.u[i].bx41 = 0xAA55;
    fk.u[i].cx41 = 0x0007;
    fk.u[i].fl41 = 0x0002;
}

static void trial(int i, int k, int made, u16 ah, u16 fl, const u8 *d,
                  unsigned n)
{
    fk.u[i].t[k].made = made;
    fk.u[i].t[k].ah = ah;
    fk.u[i].t[k].fl = fl;
    fk.u[i].t[k].data = d;
    fk.u[i].t[k].n = n;
}

static void xhci_at(u8 bus, u8 dev, u8 fn, u32 cls)
{
    fk.f[fk.nf].bus = bus;
    fk.f[fk.nf].dev = dev;
    fk.f[fk.nf].fn = fn;
    fk.f[fk.nf].vid = 0x8086;
    fk.f[fk.nf].did = 0x02ED;
    fk.f[fk.nf].cls = cls;
    fk.nf++;
}

/* The order every run that reached the disks must keep. */
static void check_order(void)
{
    const char *s = fk.seq;
    const char *o, *c, *i, *p;
    const char *last_i;

    o = strchr(s, 'O');
    c = strchr(s, 'C');
    i = strchr(s, 'I');
    p = strpbrk(s, "Pp");
    last_i = strrchr(s, 'I');
    CHECK(strncmp(s, "WDVSBO", 6) == 0, "environment, then the buffer");
    CHECK(fk.opens == 1 && fk.closes == 1, "the buffer opened and freed once");
    CHECK(o != 0 && i != 0 && o < i, "the buffer before the first INT 13h");
    CHECK(c != 0 && last_i != 0 && last_i < c,
          "every INT 13h before the buffer is freed");
    CHECK(fk.int13_closed == 0, "no INT 13h without the buffer");
    CHECK(p == 0 || (c != 0 && c < p), "the buffer freed before any PCI");
    CHECK(p == 0 || last_i < p, "every INT 13h before any PCI");
    CHECK(fk.unflushed == 0, "every INT 13h right after its own progress "
          "text was flushed");
    CHECK(fk.bad_prepare == 0, "every buffer prepared afresh");
}

static int run(void)
{
    int rc;

    out[0] = '\0';
    rc = edd_run(&ops, units, cap, cap);
    check_widths();
    return rc;
}

/* ------------------------------------------------------------------ */
/* Cases                                                               */
/* ------------------------------------------------------------------ */

static u8 b0[0x60], b1[0x60];

static void t_seabios(void)
{
    int i;

    machine();
    base26(b0, 0x1A);
    for (i = 0; i < 3; i++) {
        unit_edd30(i);
        trial(i, 0, 1, 0, 0x0002, b0, 0x1A);
        trial(i, 1, 1, 0, 0x0002, b0, 0x1A);
    }
    CHECK(run() == 1, "seabios: exit 1");
    check_order();
    HAS("unit 80h, function 41h... AH=30h BX=AA55h CX=0007h\n");
    HAS("unit 80h, function 48h, 4Ah buffer... CF=0 AH=00h size word 001Ah\n");
    HAS("unit 80h, function 48h, 42h buffer... CF=0 AH=00h size word 001Ah\n");
    HAS("unit 83h, function 41h... CF=1 AH=01h BX=55AAh: absent or "
        "unsupported\n");
    HAS("unit 80h: no device path (size word 001Ah, no key BEDDh): not named");
    HAS("unit 82h: no device path (size word 001Ah, no key BEDDh): not named");
    HAS("unit 87h: 41h CF=1 AH=01h BX=55AAh: absent or unsupported, 48h not "
        "asked");
    HAS("key 0000h at 1Eh, not BEDDh: no device path");
    HAS("flags 0006h: geometry valid, removable");
    HAS("BIOS data area: 3 hard disk(s).");
    HAS("not asked:\n  floppy units 00h and 01h, CD units E0h and up.");
    HAS("PCI: no xHCI function (class 0C0330) found.");
    HAS("Probe safety: PASS - no PCI configuration writes.");
    HAS("guard: unchanged (1B6h bytes after the buffer)");
    HAS("    00: 1A 00 06 00 FF 3F 00 00");
    CHECK(strstr(fk.seq, "II8I8II8I8II8I8IIIII") != 0,
          "seabios: 48h twice for 80h-82h, none for 83h-87h");
}

static void t_usb_final(int good)
{
    machine();
    unit_edd30(2);
    base26(b0, 0x4A);
    path(b0, 0x2C, "PCI ", "USB     ", 0x00, 0x14, 0x0, good);
    trial(2, 0, 1, 0, 0x0002, b0, 0x4A);
    base26(b1, 0x1A);
    trial(2, 1, 1, 0, 0x0002, b1, 0x1A);
    xhci_at(0x00, 0x14, 0x0, PCI_CLASS_XHCI);
    CHECK(run() == 1, "usb: exit 1");
    check_order();
    check_no_serial();
    HAS("PCI: xHCI 00:14.0 8086:02ED");
    HAS("key BEDDh, path length 2Ch (the final EDD 3.0 layout), checksum at "
        "49h");
    HAS("host bus \"PCI \" 00:14.0 channel 00h: 8086:02ED 0C0330 (xHCI, in the "
        "scan)");
    HAS("interface \"USB     \"");
    HAS("device path 38h-3Fh nonzero (withheld), 40h-47h zero\n"
        "      (for USB: the serial number, then reserved)");
    HAS("    30: 00 14 00 00 00 00 00 00 xx xx xx xx xx xx xx xx");
    if (good) {
        HAS("bytes 1Eh-49h sum to 00h: PASS");
        HAS("unit 82h: named as USB on the xHCI at 00:14.0\n");
    } else {
        HAS("bytes 1Eh-49h sum to 01h: FAIL");
        HAS("unit 82h: device path checksum fails (sum 01h): not named");
    }
}

static void t_early(void)
{
    machine();
    unit_edd30(1);
    base26(b0, 0x4A);
    trial(1, 0, 1, 0x01, 0x0003, b0, 0x1A);     /* 4Ah refused */
    base26(b1, 0x42);
    path(b1, 0x24, "PCI ", "USB     ", 0x00, 0x14, 0x0, 1);
    trial(1, 1, 1, 0, 0x0002, b1, 0x42);
    xhci_at(0x00, 0x14, 0x0, PCI_CLASS_XHCI);
    CHECK(run() == 1, "early: exit 1");
    check_order();
    check_no_serial();
    HAS("unit 81h, function 48h, 4Ah buffer... CF=1 AH=01h size word 004Ah\n");
    HAS("4Ah buffer: CF=1 AH=01h, size word 004Ah (call failed: not used)");
    HAS("path length 24h (the earlier EDD 3.0 layout), checksum at 41h");
    HAS("device path 38h-3Fh nonzero (withheld)\n");
    HAS("bytes 1Eh-41h sum to 00h: PASS");
    HAS("unit 81h: named as USB on the xHCI at 00:14.0\n");
}

static void t_overrun(void)
{
    machine();
    unit_edd30(0);
    base26(b0, 0x1A);
    trial(0, 0, 1, 0, 0x0002, b0, 0x1A);
    base26(b1, 0x4A);
    path(b1, 0x2C, "PCI ", "USB     ", 0x00, 0x14, 0x0, 1);
    trial(0, 1, 1, 0, 0x0002, b1, 0x4A);        /* 4Ah bytes into 42h */
    xhci_at(0x00, 0x14, 0x0, PCI_CLASS_XHCI);
    CHECK(run() == 1, "overrun: exit 1");
    check_order();
    check_no_serial();
    HAS("42h buffer... CF=0 AH=00h size word 004Ah\n"
        "  8 byte(s) written past the buffer\n");
    HAS("guard: 8 byte(s) CHANGED from 42h to 49h - the firmware wrote past\n"
        "      the 42h bytes it was given (only 1BEh bytes after it are "
        "watched)");
    HAS("path length 2Ch: its checksum at 49h is past this buffer");
    HAS("unit 80h: path length 2Ch does not fit the 42h buffer: not named");
}

static void t_41h_fail(void)
{
    machine();
    fk.u[0].made41 = 0;
    CHECK(run() == 1, "41h host failure: exit 1");
    check_order();
    HAS("unit 80h, function 41h... DPMI error 8024h: not asked\n");
    HAS("unit 80h: DPMI error 8024h on 41h: not asked");
    machine();
    unit_edd30(0);
    fk.u[0].bx41 = 0x55AA;                      /* CF clear, BX unchanged */
    CHECK(run() == 1, "41h no signature: exit 1");
    HAS("unit 80h, function 41h... CF=0 AH=30h BX=55AAh: absent or "
        "unsupported\n");
    CHECK(strchr(fk.seq, '8') == 0, "no 48h after a failed 41h");
}

static void t_call_failed_path(void)
{
    machine();
    unit_edd30(2);
    base26(b0, 0x4A);
    path(b0, 0x2C, "PCI ", "USB     ", 0x00, 0x14, 0x0, 1);
    trial(2, 0, 1, 0x00, 0x0003, b0, 0x4A);     /* CF set, AH 0 */
    trial(2, 1, 1, 0x01, 0x0002, b0, 0x4A);     /* CF clear, AH 1 */
    xhci_at(0x00, 0x14, 0x0, PCI_CLASS_XHCI);
    run();
    check_no_serial();
    HAS("unit 82h: 48h failed (CF=1 AH=00h): no usable answer, not named");
    CHECK(!has("named as USB"), "a failed call never names");
    machine();
    unit_edd30(2);
    trial(2, 0, 0, 0, 0, 0, 0);
    trial(2, 1, 0, 0, 0, 0, 0);
    run();
    HAS("unit 82h, function 48h, 4Ah buffer... DPMI error 8022h\n");
    HAS("unit 82h: DPMI error 8022h on 48h: no usable answer, not named");
}

static void t_keyed_invalid_then_valid(void)
{
    machine();
    unit_edd30(2);
    base26(b0, 0x4A);
    path(b0, 0x2C, "PCI ", "USB     ", 0x00, 0x14, 0x0, 1);
    b0[0x20] = 0x30;                            /* unknown length */
    trial(2, 0, 1, 0, 0x0002, b0, 0x4A);
    base26(b1, 0x42);
    path(b1, 0x24, "PCI ", "USB     ", 0x00, 0x14, 0x0, 1);
    trial(2, 1, 1, 0, 0x0002, b1, 0x42);
    xhci_at(0x00, 0x14, 0x0, PCI_CLASS_XHCI);
    run();
    HAS("key BEDDh, path length 30h: not 2Ch or 24h, not read further");
    HAS("unit 82h: named as USB on the xHCI at 00:14.0\n");
}

static void t_disagree(void)
{
    machine();
    unit_edd30(2);
    base26(b0, 0x4A);
    path(b0, 0x2C, "PCI ", "USB     ", 0x00, 0x14, 0x0, 1);
    trial(2, 0, 1, 0, 0x0002, b0, 0x4A);
    base26(b1, 0x42);
    path(b1, 0x24, "PCI ", "USB     ", 0x00, 0x15, 0x0, 1);
    trial(2, 1, 1, 0, 0x0002, b1, 0x42);
    xhci_at(0x00, 0x14, 0x0, PCI_CLASS_XHCI);
    xhci_at(0x00, 0x15, 0x0, PCI_CLASS_XHCI);
    run();
    HAS("unit 82h: the 4Ah and 42h answers disagree: not named");
}

static void t_size_word(u16 sz)
{
    char want[96];

    machine();
    unit_edd30(2);
    base26(b0, sz);
    path(b0, 0x2C, "PCI ", "USB     ", 0x00, 0x14, 0x0, 1);
    trial(2, 0, 1, 0, 0x0002, b0, 0x4A);
    trial(2, 1, 1, 0, 0x0002, b0, 0x42);
    xhci_at(0x00, 0x14, 0x0, PCI_CLASS_XHCI);
    run();
    sprintf(want, "4Ah buffer... CF=0 AH=00h size word %04Xh\n", sz);
    HAS(want);
    HAS("unit 82h: named as USB on the xHCI at 00:14.0\n");
}

static void t_others(void)
{
    /* NVMe on PCI 01:00.0: a path, not USB. */
    machine();
    unit_edd30(0);
    base26(b0, 0x4A);
    path(b0, 0x2C, "PCI ", "NVME    ", 0x01, 0x00, 0x0, 1);
    trial(0, 0, 1, 0, 0x0002, b0, 0x4A);
    trial(0, 1, 1, 0, 0x0002, b0, 0x42);
    run();
    check_no_serial();
    HAS("unit 80h: not USB: interface \"NVME\", host bus \"PCI \" 01:00.0");
    HAS("01:00.0 channel 00h: no function there");
    /* USB on an EHCI. */
    machine();
    unit_edd30(1);
    base26(b0, 0x4A);
    path(b0, 0x2C, "PCI ", "USB     ", 0x00, 0x1D, 0x0, 1);
    trial(1, 0, 1, 0, 0x0002, b0, 0x4A);
    xhci_at(0x00, 0x1D, 0x0, 0x0C0320UL);
    run();
    HAS("unit 81h: USB on PCI 00:1D.0, class 0C0320, not an xHCI: not named");
    /* USB where no function answers. */
    machine();
    unit_edd30(1);
    base26(b0, 0x4A);
    path(b0, 0x2C, "PCI ", "USB     ", 0x00, 0x14, 0x0, 1);
    trial(1, 0, 1, 0, 0x0002, b0, 0x4A);
    run();
    HAS("unit 81h: USB on PCI 00:14.0, no function there: not named");
    /* An address no PCI function can have: never read. */
    machine();
    unit_edd30(1);
    base26(b0, 0x4A);
    path(b0, 0x2C, "PCI ", "USB     ", 0x00, 0x20, 0x9, 1);
    trial(1, 0, 1, 0, 0x0002, b0, 0x4A);
    run();
    HAS("unit 81h: USB on PCI 00:20.09, not a valid address: not named");
    CHECK(strchr(fk.seq, 'p') == 0, "an invalid address is never read");
    /* A USB path on another host bus. */
    machine();
    unit_edd30(1);
    base26(b0, 0x4A);
    path(b0, 0x2C, "ISA ", "USB     ", 0x00, 0x14, 0x0, 1);
    trial(1, 0, 1, 0, 0x0002, b0, 0x4A);
    run();
    HAS("unit 81h: USB, but host bus \"ISA \", not \"PCI \": not named");
    /* Named, but 41h short of EDD 3.0. */
    machine();
    unit_edd30(2);
    fk.u[2].ax41 = 0x2100;
    fk.u[2].cx41 = 0x0001;
    base26(b0, 0x4A);
    path(b0, 0x2C, "PCI ", "USB     ", 0x00, 0x14, 0x0, 1);
    trial(2, 0, 1, 0, 0x0002, b0, 0x4A);
    xhci_at(0x00, 0x14, 0x0, PCI_CLASS_XHCI);
    run();
    HAS("unit 82h: USB on the xHCI at 00:14.0, but 41h AH=21h CX=0001h is not "
        "EDD 3.0");
    /* Every flag set: the names wrap inside 79 columns. */
    machine();
    unit_edd30(1);
    base26(b0, 0x1A);
    put16(b0, 0x02, 0xFFFF);
    trial(1, 0, 1, 0, 0x0002, b0, 0x1A);
    run();
    HAS("flags FFFFh: DMA boundary errors handled, geometry valid, removable,\n"
        "      write with verify, media change notification, lockable, no "
        "media,\n      use 50h, bits 8-15 FFh\n");
    /* Control bytes in a text field print as '?'. */
    machine();
    unit_edd30(1);
    base26(b0, 0x4A);
    path(b0, 0x2C, "PCI ", "AB\001\377    ", 0x00, 0x14, 0x0, 1);
    trial(1, 0, 1, 0, 0x0002, b0, 0x4A);
    run();
    HAS("interface \"AB??    \"");
}

static void t_env(void)
{
    machine();
    fk.win_made = 0;
    CHECK(run() == 2, "1600h not made: refused");
    CHECK(strcmp(fk.seq, "W") == 0, "nothing after a failed 1600h");
    HAS("XHCIQUAL --edd refused: DPMI could not ask whether Windows is\n");
    machine();
    fk.win_ax = 0x0A04;
    CHECK(run() == 2, "Windows 98: refused");
    CHECK(strcmp(fk.seq, "W") == 0, "nothing after Windows is found");
    HAS("XHCIQUAL --edd refused: Windows is running, and in its MS-DOS Prompt\n");
    HAS("\"Command prompt only\"");
    machine();
    fk.win_ax = 0x00FF;
    CHECK(run() == 2, "Windows/386 2.x: refused");
    machine();
    fk.win_ax = 0x0080;
    CHECK(run() == 1, "AL 80h: no Windows");
    machine();
    fk.dos_made = 0;
    CHECK(run() == 2, "3306h not made: refused");
    CHECK(strcmp(fk.seq, "WD") == 0, "nothing after a failed 3306h");
    machine();
    fk.dos_bx = 0x3205;
    CHECK(run() == 2, "NTVDM: refused");
    CHECK(strcmp(fk.seq, "WD") == 0, "nothing after NTVDM is found");
    HAS("Windows NT's MS-DOS (version 5.50)");
    machine();
    fk.dos_ax = 0x33FF;
    fk.dos_bx = 0x3205;
    CHECK(run() == 1, "3306h unsupported (AL FFh): not NTVDM");
    machine();
    fk.dpmi_bx = 0x0001;
    CHECK(run() == 1, "V86: warned, run");
    HAS("warning: interrupts run in virtual-8086 mode (EMM386 or another "
        "memory\n");
    HAS("DPMI 0.90, interrupts reflected in virtual-8086 mode.");
    machine();
    fk.dpmi_made = 0;
    CHECK(run() == 1, "0400h not made: warned, run");
    HAS("warning: DPMI 0400h did not answer");
    machine();
    fk.open_fail = 1;
    CHECK(run() == 1, "no buffer: exit 1");
    CHECK(strcmp(fk.seq, "WDVSBO") == 0, "no INT 13h and no PCI without a "
          "buffer");
    HAS("No conventional memory for the buffer (DPMI 0100h)");
    machine();
    fk.writes = 1;
    run();
    HAS("Probe safety: INTERNAL FAILURE - 1 PCI configuration write(s).");
}

/* The decode reads nothing past the bytes it is given. */
static void t_guard_page(void)
{
    SYSTEM_INFO si;
    u8 *mem;
    DWORD old;
    EDD_PATH p;
    static const unsigned sizes[3] = { 0x4A, 0x42, 0x1A };
    int i;

    GetSystemInfo(&si);
    mem = (u8 *)VirtualAlloc(0, si.dwPageSize * 2, MEM_COMMIT | MEM_RESERVE,
                             PAGE_READWRITE);
    CHECK(mem != 0, "guard page allocated");
    if (mem == 0)
        return;
    VirtualProtect(mem + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old);
    for (i = 0; i < 3; i++) {
        u8 *at = mem + si.dwPageSize - sizes[i];

        base26(b0, 0x4A);
        path(b0, 0x2C, "PCI ", "USB     ", 0x00, 0x14, 0x0, 1);
        memcpy(at, b0, sizes[i]);
        edd_decode(at, sizes[i], &p);
        CHECK(sizes[i] != 0x4A || (p.cks_ok && p.is_usb),
              "4Ah: decoded at the page end");
        CHECK(sizes[i] != 0x42 || p.nofit, "42h: a 2Ch path does not fit");
        CHECK(sizes[i] != 0x1A || !p.has_key, "1Ah: no key read");
    }
    VirtualFree(mem, 0, MEM_RELEASE);
}

static void t_prepare(void)
{
    EDD_TRIAL t;
    unsigned j;
    int ok = 1;

    memset(area, 0x77, sizeof(area));
    edd_prepare(area, 0x42);
    if (area[0] != 0x42 || area[1] != 0)
        ok = 0;
    for (j = 2; j < 0x42; j++)
        if (area[j] != 0)
            ok = 0;
    edd_guard_scan(area, 0x42, &t);
    CHECK(ok && t.guard_changed == 0, "prepare: cleared, size word, guard");
    area[0x1FF] ^= 1;
    edd_guard_scan(area, 0x42, &t);
    CHECK(t.guard_changed == 1 && t.guard_first == 0x1FF &&
          t.guard_last == 0x1FF, "guard: the last byte seen");
}

int main(void)
{
    t_prepare();
    t_seabios();
    t_usb_final(1);
    t_usb_final(0);
    t_early();
    t_overrun();
    t_41h_fail();
    t_call_failed_path();
    t_keyed_invalid_then_valid();
    t_disagree();
    t_size_word(0x001E);
    t_size_word(0xFFFF);
    t_others();
    t_env();
    t_guard_page();
    if (failures == 0)
        printf("test_edd: %d checks passed\n", checks);
    else
        printf("test_edd: %d of %d checks FAILED\n", failures, checks);
    return failures;
}
