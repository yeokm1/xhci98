/*
 * edd.c - XHCIQUAL --edd (roadmap-usb4.md task 47.24): does the firmware
 * name a disk unit by an EDD 3.0 device path, and is that path a USB device
 * on an xHCI? Design record 37 section 4.1 and its question Q1.
 *
 * This file is pure. edd_run is the whole mode, every machine access an
 * operation the caller hands in (EDD_OPS): sysprobe.c supplies the real ones
 * (DPMI 0300h, a DPMI 0100h block, CF8h/CFCh reads), and
 * xhciqual/test/test_edd.c supplies fakes that record the order of the
 * calls, so the order this mode promises - every INT 13h before any PCI
 * access, nothing at all after a refusal - is a test.
 *
 * Layout of function 48h's buffer [spec: T13 project 1386D revision 5
 * section 6.8, the final EDD 3.0 layout; Phoenix BIOS Enhanced Disk Drive
 * Specification 3.0, the earlier layout] [linux: include/uapi/linux/edd.h at
 * c6859eed, struct edd_device_params and the EDD_EXT_* and EDD_INFO_* bit
 * names, the final layout; facts only, nothing copied]:
 *   00h size  02h flags  04h cylinders  08h heads  0Ch sectors per track
 *   10h sectors (8 bytes)  18h bytes per sector  1Ah DPTE offset, segment
 *   1Eh key BEDDh  20h device path length  21h, 22h reserved
 *   24h host bus (4 characters)  28h interface (8 characters)
 *   30h interface path (PCI: bus, device, function, channel, 4 reserved)
 *   38h device path (USB in the final layout: the serial number, 8 reserved)
 *   final (length 2Ch): 48h reserved, 49h checksum over 1Eh-49h
 *   earlier (length 24h): 40h reserved, 41h checksum over 1Eh-41h
 *
 * The size word the firmware returns is printed and never decides: a path
 * is believed on its key, length and checksum, inside the bytes the buffer
 * was given (record 37 section 4.1).
 *
 * No identifiers (the tool's rule, qual.h): the device path at 38h-47h can
 * carry a serial number, so its bytes are never printed, in a decoded line
 * or a dump - each prints as 00 or xx.
 */

#include <stdio.h>
#include <string.h>
#include "qual.h"

#define EDD_KEY          0xBEDDU
#define EDD_LEN_FINAL    0x2C
#define EDD_LEN_EARLY    0x24
#define EDD_ID_FIRST     0x38       /* the device path, never printed   */
#define EDD_ID_LAST      0x47

/* The result of one trial, in rank order: a later one got further. */
#define EDD_R_CALLFAIL   0
#define EDD_R_NOKEY      1
#define EDD_R_BADLEN     2
#define EDD_R_NOFIT      3
#define EDD_R_BADSUM     4
#define EDD_R_NOTUSB     5          /* from here on: a valid path        */
#define EDD_R_NOTPCI     6
#define EDD_R_BADADDR    7
#define EDD_R_NOFUNC     8
#define EDD_R_NOTXHCI    9
#define EDD_R_NAMED      10

static const u16 trial_size[EDD_TRIALS] = { EDD_SIZE_FINAL, EDD_SIZE_EARLY };

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u8 guard_byte(unsigned i)
{
    return (u8)(0xA5 ^ (i & 0xFF));
}

void edd_prepare(u8 *area, unsigned asked)
{
    unsigned i;

    memset(area, 0, asked);
    area[0] = (u8)asked;
    area[1] = (u8)(asked >> 8);
    for (i = asked; i < EDD_AREA; i++)
        area[i] = guard_byte(i);
}

void edd_guard_scan(const u8 *area, unsigned asked, EDD_TRIAL *t)
{
    unsigned i;

    t->guard_changed = 0;
    t->guard_first = 0;
    t->guard_last = 0;
    for (i = asked; i < EDD_AREA; i++) {
        if (area[i] != guard_byte(i)) {
            if (t->guard_changed == 0)
                t->guard_first = i;
            t->guard_last = i;
            t->guard_changed++;
        }
    }
}

/* A field is read only when its last byte lies inside the buffer given. */
static int fits(unsigned at, unsigned len, unsigned asked)
{
    return at + len <= asked;
}

void edd_decode(const u8 *raw, unsigned asked, EDD_PATH *p)
{
    unsigned i, end;
    u8 sum;

    memset(p, 0, sizeof(*p));
    if (asked > EDD_SIZE_FINAL)
        asked = EDD_SIZE_FINAL;
    if (fits(0x00, 2, asked))
        p->size = rd16(raw + 0x00);
    if (fits(0x02, 2, asked))
        p->flags = rd16(raw + 0x02);
    if (fits(0x04, 12, asked)) {
        p->base_ok = 1;
        p->cyls = rd32(raw + 0x04);
        p->heads = rd32(raw + 0x08);
        p->spt = rd32(raw + 0x0C);
    }
    if (fits(0x10, 8, asked)) {
        p->sec_lo = rd32(raw + 0x10);
        p->sec_hi = rd32(raw + 0x14);
    }
    if (fits(0x18, 2, asked))
        p->bps = rd16(raw + 0x18);
    if (fits(0x1A, 4, asked)) {
        p->dpte_off = rd16(raw + 0x1A);
        p->dpte_seg = rd16(raw + 0x1C);
    }
    if (!fits(0x1E, 2, asked))
        return;
    p->key = rd16(raw + 0x1E);
    if (p->key != EDD_KEY)
        return;
    p->has_key = 1;
    if (!fits(0x20, 1, asked))
        return;
    p->path_len = raw[0x20];
    if (p->path_len == EDD_LEN_FINAL) {
        p->layout = EDD_LAYOUT_FINAL;
        p->cks_at = 0x49;
    } else if (p->path_len == EDD_LEN_EARLY) {
        p->layout = EDD_LAYOUT_EARLY;
        p->cks_at = 0x41;
    } else {
        p->layout = EDD_LAYOUT_UNKNOWN;
        return;
    }
    if (!fits(p->cks_at, 1, asked)) {
        p->nofit = 1;
        return;
    }
    for (i = 0; i < 4; i++)
        p->host_bus[i] = (char)raw[0x24 + i];
    p->host_bus[4] = '\0';
    for (i = 0; i < 8; i++)
        p->iface[i] = (char)raw[0x28 + i];
    p->iface[8] = '\0';
    for (i = 0; i < 8; i++)
        p->ipath[i] = raw[0x30 + i];
    end = (p->layout == EDD_LAYOUT_FINAL) ? 0x47 : 0x3F;
    for (i = 0x38; i <= end; i++) {
        if (raw[i] != 0) {
            if (i < 0x40)
                p->dpath_lo_nz = 1;
            else
                p->dpath_hi_nz = 1;
        }
    }
    sum = 0;
    for (i = 0x1E; i <= p->cks_at; i++)
        sum = (u8)(sum + raw[i]);
    p->cks_byte = raw[p->cks_at];
    p->cks_sum = sum;
    p->cks_ok = (sum == 0);
    p->is_pci = (memcmp(p->host_bus, "PCI ", 4) == 0);
    p->is_pcix = (memcmp(p->host_bus, "PCIX", 4) == 0);
    p->is_usb = (memcmp(p->iface, "USB     ", 8) == 0);
}

/* The trial's own buffer, decoded; 0 when the call itself failed. */
static int trial_call_ok(const EDD_TRIAL *t)
{
    return t->asked && t->r.made && (t->r.flags & 1) == 0 &&
           (u8)(t->r.ax >> 8) == 0;
}

static int addr_ok(const EDD_PATH *p)
{
    return p->ipath[1] < 32 && p->ipath[2] < 8;
}

int edd_trial_result(const EDD_TRIAL *t, EDD_PATH *p)
{
    edd_decode(t->raw, t->size, p);
    if (!trial_call_ok(t))
        return EDD_R_CALLFAIL;
    if (!p->has_key)
        return EDD_R_NOKEY;
    if (p->layout == EDD_LAYOUT_UNKNOWN)
        return EDD_R_BADLEN;
    if (p->nofit)
        return EDD_R_NOFIT;
    if (!p->cks_ok)
        return EDD_R_BADSUM;
    if (!p->is_usb)
        return EDD_R_NOTUSB;
    if (!p->is_pci)
        return EDD_R_NOTPCI;
    if (!addr_ok(p))
        return EDD_R_BADADDR;
    if (!t->pci_present)
        return EDD_R_NOFUNC;
    if (t->pci_class != PCI_CLASS_XHCI)
        return EDD_R_NOTXHCI;
    return EDD_R_NAMED;
}

int edd_unit_answered41(const EDD_UNIT *u)
{
    return u->r41.made && (u->r41.flags & 1) == 0 && u->r41.bx == 0xAA55U;
}

static int edd30(const EDD_UNIT *u)
{
    return (u8)(u->r41.ax >> 8) >= 0x30 && (u->r41.cx & 0x0004) != 0;
}

static void trim_right(char *s)
{
    int n;

    n = (int)strlen(s);
    while (n > 0 && s[n - 1] == ' ')
        s[--n] = '\0';
}

/* A fixed-width text field as printed: a byte outside 20h-7Eh, NUL
 * included, is '?'. */
static void printable(const char *s, int n, char *out)
{
    int i;

    for (i = 0; i < n; i++)
        out[i] = (s[i] >= 0x20 && s[i] <= 0x7E) ? s[i] : '?';
    out[n] = '\0';
}

void edd_conclude(const EDD_UNIT *u, char *out)
{
    EDD_PATH p[EDD_TRIALS];
    int res[EDD_TRIALS];
    int best, i;
    const EDD_TRIAL *t;
    const EDD_PATH *bp;
    char a[9], b[9];

    if (!u->r41.made) {
        sprintf(out, "unit %02Xh: DPMI error %04Xh on 41h: not asked", u->unit,
                u->r41.dpmi_err);
        return;
    }
    if (!edd_unit_answered41(u)) {
        sprintf(out, "unit %02Xh: 41h CF=%u AH=%02Xh BX=%04Xh: absent or "
                "unsupported, 48h not asked", u->unit, u->r41.flags & 1,
                (u8)(u->r41.ax >> 8), u->r41.bx);
        return;
    }
    for (i = 0; i < EDD_TRIALS; i++)
        res[i] = edd_trial_result(&u->t[i], &p[i]);
    best = (res[1] > res[0]) ? 1 : 0;
    t = &u->t[best];
    bp = &p[best];
    if (res[0] >= EDD_R_NOTUSB && res[1] >= EDD_R_NOTUSB &&
        (memcmp(p[0].host_bus, p[1].host_bus, 4) != 0 ||
         memcmp(p[0].iface, p[1].iface, 8) != 0 ||
         memcmp(p[0].ipath, p[1].ipath, 3) != 0)) {
        sprintf(out, "unit %02Xh: the 4Ah and 42h answers disagree: not named",
                u->unit);
        return;
    }
    switch (res[best]) {
    case EDD_R_CALLFAIL:
        if (!t->r.made)
            sprintf(out, "unit %02Xh: DPMI error %04Xh on 48h: no usable "
                    "answer, not named", u->unit, t->r.dpmi_err);
        else
            sprintf(out, "unit %02Xh: 48h failed (CF=%u AH=%02Xh): no usable "
                    "answer, not named", u->unit, t->r.flags & 1,
                    (u8)(t->r.ax >> 8));
        break;
    case EDD_R_NOKEY:
        sprintf(out, "unit %02Xh: no device path (size word %04Xh, no key "
                "BEDDh): not named", u->unit, bp->size);
        break;
    case EDD_R_BADLEN:
        sprintf(out, "unit %02Xh: device path of unknown length %02Xh: not "
                "named", u->unit, bp->path_len);
        break;
    case EDD_R_NOFIT:
        sprintf(out, "unit %02Xh: path length %02Xh does not fit the %02Xh "
                "buffer: not named", u->unit, bp->path_len, t->size);
        break;
    case EDD_R_BADSUM:
        sprintf(out, "unit %02Xh: device path checksum fails (sum %02Xh): not "
                "named", u->unit, bp->cks_sum);
        break;
    case EDD_R_NOTUSB:
        printable(bp->iface, 8, a);
        trim_right(a);
        printable(bp->host_bus, 4, b);
        if (bp->is_pci || bp->is_pcix)
            sprintf(out, addr_ok(bp) ?
                    "unit %02Xh: not USB: interface \"%s\", host bus \"%s\" "
                    "%02X:%02X.%X" :
                    "unit %02Xh: not USB: interface \"%s\", host bus \"%s\" "
                    "%02X:%02X.%02X", u->unit, a, b, bp->ipath[0],
                    bp->ipath[1], bp->ipath[2]);
        else
            sprintf(out, "unit %02Xh: not USB: interface \"%s\", host bus "
                    "\"%s\"", u->unit, a, b);
        break;
    case EDD_R_NOTPCI:
        printable(bp->host_bus, 4, b);
        sprintf(out, "unit %02Xh: USB, but host bus \"%s\", not \"PCI \": not "
                "named", u->unit, b);
        break;
    case EDD_R_BADADDR:
        sprintf(out, "unit %02Xh: USB on PCI %02X:%02X.%02X, not a valid "
                "address: not named", u->unit, bp->ipath[0], bp->ipath[1],
                bp->ipath[2]);
        break;
    case EDD_R_NOFUNC:
        sprintf(out, "unit %02Xh: USB on PCI %02X:%02X.%X, no function there: "
                "not named", u->unit, bp->ipath[0], bp->ipath[1],
                bp->ipath[2]);
        break;
    case EDD_R_NOTXHCI:
        sprintf(out, "unit %02Xh: USB on PCI %02X:%02X.%X, class %06lX, not "
                "an xHCI: not named", u->unit, bp->ipath[0], bp->ipath[1],
                bp->ipath[2], t->pci_class);
        break;
    default:
        if (!edd30(u))
            sprintf(out, "unit %02Xh: USB on the xHCI at %02X:%02X.%X, but 41h "
                    "AH=%02Xh CX=%04Xh is not EDD 3.0", u->unit, bp->ipath[0],
                    bp->ipath[1], bp->ipath[2], (u8)(u->r41.ax >> 8),
                    u->r41.cx);
        else
            sprintf(out, "unit %02Xh: named as USB on the xHCI at %02X:%02X.%X",
                    u->unit, bp->ipath[0], bp->ipath[1], bp->ipath[2]);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* The detail, per unit                                                */
/* ------------------------------------------------------------------ */

static const char *const info_bit[8] = {
    "DMA boundary errors handled", "geometry valid", "removable",
    "write with verify", "media change notification", "lockable",
    "no media", "use 50h"
};

/* The flags named, wrapped to fit 79 columns. */
static void flags_line(u16 flags, QPRINTF_FN pf)
{
    char line[96], name[40];
    int i, n, any;

    n = sprintf(line, "    flags %04Xh:", flags);
    any = 0;
    for (i = 0; i < 9; i++) {
        if (i < 8) {
            if ((flags & (1U << i)) == 0)
                continue;
            strcpy(name, info_bit[i]);
        } else {
            if ((flags & 0xFF00U) == 0)
                continue;
            sprintf(name, "bits 8-15 %02Xh", (flags >> 8) & 0xFF);
        }
        if (any)
            n += sprintf(line + n, ",");
        if (n + 1 + (int)strlen(name) > 78) {
            pf("%s\n", line);
            n = sprintf(line, "     ");
        }
        n += sprintf(line + n, " %s", name);
        any = 1;
    }
    pf("%s%s\n", line, any ? "" : " none");
}

static void dump(const u8 *raw, unsigned asked, QPRINTF_FN pf)
{
    unsigned row, i;
    char line[80];
    char *q;

    for (row = 0; row < asked; row += 16) {
        q = line;
        q += sprintf(q, "    %02X:", row);
        for (i = row; i < row + 16 && i < asked; i++) {
            if (i >= EDD_ID_FIRST && i <= EDD_ID_LAST)
                q += sprintf(q, raw[i] ? " xx" : " 00");
            else
                q += sprintf(q, " %02X", raw[i]);
        }
        pf("%s\n", line);
    }
}

static void report_trial(const EDD_TRIAL *t, QPRINTF_FN pf)
{
    EDD_PATH p;
    char a[9], b[9];

    edd_decode(t->raw, t->size, &p);
    if (!t->r.made) {
        pf("  48h, %02Xh buffer: DPMI error %04Xh, the call was not made\n",
           t->size, t->r.dpmi_err);
        return;
    }
    pf("  48h, %02Xh buffer: CF=%u AH=%02Xh, size word %04Xh%s\n", t->size,
       t->r.flags & 1, (u8)(t->r.ax >> 8), p.size,
       trial_call_ok(t) ? "" : " (call failed: not used)");
    flags_line(p.flags, pf);
    pf("    cylinders %08lXh  heads %08lXh  sectors/track %08lXh\n", p.cyls,
       p.heads, p.spt);
    pf("    sectors %08lX%08lXh  bytes/sector %u  DPTE %04X:%04X\n", p.sec_hi,
       p.sec_lo, p.bps, p.dpte_seg, p.dpte_off);
    if (!p.has_key) {
        pf("    key %04Xh at 1Eh, not BEDDh: no device path\n", p.key);
    } else if (p.layout == EDD_LAYOUT_UNKNOWN) {
        pf("    key BEDDh, path length %02Xh: not 2Ch or 24h, not read "
           "further\n", p.path_len);
    } else if (p.nofit) {
        pf("    key BEDDh, path length %02Xh: its checksum at %02Xh is past "
           "this buffer\n", p.path_len, p.cks_at);
    } else {
        pf("    key BEDDh, path length %02Xh (%s layout), checksum at %02Xh\n",
           p.path_len, p.layout == EDD_LAYOUT_FINAL ? "the final EDD 3.0" :
           "the earlier EDD 3.0", p.cks_at);
        printable(p.host_bus, 4, b);
        if (p.is_pci || p.is_pcix) {
            pf(addr_ok(&p) ? "    host bus \"%s\" %02X:%02X.%X channel %02Xh" :
               "    host bus \"%s\" %02X:%02X.%02X channel %02Xh", b,
               p.ipath[0], p.ipath[1], p.ipath[2], p.ipath[3]);
            if (!addr_ok(&p))
                pf(": not a valid address\n");
            else if (!t->pci_checked)
                pf("\n");
            else if (!t->pci_present)
                pf(": no function there\n");
            else
                pf(": %04X:%04X %06lX (%s%s)\n", t->pci_vid, t->pci_did,
                   t->pci_class, t->pci_class == PCI_CLASS_XHCI ? "xHCI" :
                   "not an xHCI", t->pci_in_scan ? ", in the scan" : "");
            pf("    interface path reserved %02X %02X %02X %02X\n", p.ipath[4],
               p.ipath[5], p.ipath[6], p.ipath[7]);
        } else {
            pf("    host bus \"%s\", interface path %02X %02X %02X %02X %02X "
               "%02X %02X %02X\n", b, p.ipath[0], p.ipath[1], p.ipath[2],
               p.ipath[3], p.ipath[4], p.ipath[5], p.ipath[6], p.ipath[7]);
        }
        printable(p.iface, 8, a);
        pf("    interface \"%s\"\n", a);
        if (p.layout == EDD_LAYOUT_FINAL)
            pf("    device path 38h-3Fh %s, 40h-47h %s%s\n",
               p.dpath_lo_nz ? "nonzero (withheld)" : "zero",
               p.dpath_hi_nz ? "nonzero (withheld)" : "zero",
               p.is_usb ? "\n      (for USB: the serial number, then "
               "reserved)" : "");
        else
            pf("    device path 38h-3Fh %s\n",
               p.dpath_lo_nz ? "nonzero (withheld)" : "zero");
        pf("    checksum byte %02Xh, bytes 1Eh-%02Xh sum to %02Xh: %s\n",
           p.cks_byte, p.cks_at, p.cks_sum, p.cks_ok ? "PASS" : "FAIL");
    }
    if (t->guard_changed == 0)
        pf("    guard: unchanged (%Xh bytes after the buffer)\n",
           EDD_AREA - t->size);
    else
        pf("    guard: %u byte(s) CHANGED from %02Xh to %02Xh - the firmware "
           "wrote past\n      the %02Xh bytes it was given (only %Xh bytes "
           "after it are watched)\n", t->guard_changed, t->guard_first,
           t->guard_last, t->size, EDD_AREA - t->size);
    dump(t->raw, t->size, pf);
}

void report_edd_unit(const EDD_UNIT *u, QPRINTF_FN pf)
{
    char line[96];
    int i;

    pf("\n==== Unit %02Xh ====\n", u->unit);
    if (!u->r41.made) {
        pf("  41h: DPMI error %04Xh, the call was not made\n", u->r41.dpmi_err);
    } else {
        pf("  41h: CF=%u AH=%02Xh BX=%04Xh CX=%04Xh DX=%04Xh\n",
           u->r41.flags & 1, (u8)(u->r41.ax >> 8), u->r41.bx, u->r41.cx,
           u->r41.dx);
        if (edd_unit_answered41(u))
            pf("       version %02Xh; CX: fixed disk access %s, locking and "
               "ejecting %s,\n       EDD %s, 64-bit extensions %s\n",
               (u8)(u->r41.ax >> 8), (u->r41.cx & 1) ? "yes" : "no",
               (u->r41.cx & 2) ? "yes" : "no", (u->r41.cx & 4) ? "yes" : "no",
               (u->r41.cx & 8) ? "yes" : "no");
    }
    for (i = 0; i < EDD_TRIALS; i++) {
        if (u->t[i].asked)
            report_trial(&u->t[i], pf);
    }
    edd_conclude(u, line);
    pf("  %s\n", line);
}

/* ------------------------------------------------------------------ */
/* The mode                                                            */
/* ------------------------------------------------------------------ */

static void call13(const EDD_OPS *ops, EDD_REGS *r, u16 ax, u16 bx, u8 dl)
{
    r->ax = ax;
    r->bx = bx;
    r->cx = 0;
    r->dx = dl;
    r->flags = 0;
    r->made = 0;
    r->dpmi_err = 0;
    ops->int13(ops->ctx, r);
}

static int refuse_env(const EDD_OPS *ops, QPRINTF_FN pf)
{
    u16 ax, bx;

    if (!ops->win1600(ops->ctx, &ax)) {
        pf("XHCIQUAL --edd refused: DPMI could not ask whether Windows is\n"
           "  running (INT 2Fh 1600h). Nothing was asked of the disks (exit "
           "2).\n");
        return 1;
    }
    if ((ax & 0xFF) != 0x00 && (ax & 0xFF) != 0x80) {
        pf("XHCIQUAL --edd refused: Windows is running, and in its MS-DOS "
           "Prompt\n"
           "  INT 13h is Windows' own. Shut down, start the computer again, "
           "choose\n"
           "  \"Command prompt only\" in the Startup Menu and run it there.\n"
           "  Nothing was asked of the disks (exit 2).\n");
        return 1;
    }
    if (!ops->dos3306(ops->ctx, &ax, &bx)) {
        pf("XHCIQUAL --edd refused: DPMI could not ask the DOS version (INT "
           "21h\n"
           "  3306h). Nothing was asked of the disks (exit 2).\n");
        return 1;
    }
    if ((ax & 0xFF) != 0xFF && bx == 0x3205) {
        pf("XHCIQUAL --edd refused: this is Windows NT's MS-DOS (version "
           "5.50),\n"
           "  where INT 13h is Windows' own. Start MS-DOS itself and run it "
           "there.\n"
           "  Nothing was asked of the disks (exit 2).\n");
        return 1;
    }
    return 0;
}

int edd_run(const EDD_OPS *ops, EDD_UNIT *units, QPRINTF_FN pf,
            QPRINTF_FN detail)
{
    u16 ax, bx;
    u8 *area;
    int i, k, n, nx;
    EDD_XHCI xs[EDD_MAX_XHCI];
    char line[96];
    u32 writes;

    memset(units, 0, sizeof(EDD_UNIT) * EDD_UNIT_COUNT);
    if (refuse_env(ops, pf))
        return 2;

    if (!ops->dpmi0400(ops->ctx, &ax, &bx)) {
        pf("warning: DPMI 0400h did not answer, so whether interrupts run in\n"
           "  real or virtual-8086 mode is not known.\n");
    } else {
        detail("DPMI %u.%02u, interrupts reflected in %s mode.\n",
               (ax >> 8) & 0xFF, ax & 0xFF,
               (bx & 2) ? "real" : "virtual-8086");
        if ((bx & 2) == 0)
            pf("warning: interrupts run in virtual-8086 mode (EMM386 or "
               "another memory\n"
               "  manager is loaded). Start without it; HIMEM is fine.\n");
    }
    ops->sysinfo(ops->ctx);
    pf("BIOS data area: %u hard disk(s). Asked: units 80h to 87h; not asked:\n"
       "  floppy units 00h and 01h, CD units E0h and up.\n",
       ops->bda_disks(ops->ctx));

    area = ops->buf_open(ops->ctx);
    if (area == 0) {
        pf("No conventional memory for the buffer (DPMI 0100h): nothing was\n"
           "  asked of the disks.\n");
        return 1;
    }
    for (i = 0; i < EDD_UNIT_COUNT; i++) {
        EDD_UNIT *u = &units[i];

        u->unit = (u8)(EDD_UNIT_FIRST + i);
        pf("unit %02Xh, function 41h... ", u->unit);
        ops->flush(ops->ctx);
        call13(ops, &u->r41, 0x4100, 0x55AA, u->unit);
        if (!u->r41.made) {
            pf("DPMI error %04Xh: not asked\n", u->r41.dpmi_err);
            continue;
        }
        if (!edd_unit_answered41(u)) {
            pf("CF=%u AH=%02Xh BX=%04Xh: absent or unsupported\n",
               u->r41.flags & 1, (u8)(u->r41.ax >> 8), u->r41.bx);
            continue;
        }
        pf("AH=%02Xh BX=%04Xh CX=%04Xh\n", (u8)(u->r41.ax >> 8), u->r41.bx,
           u->r41.cx);
        for (k = 0; k < EDD_TRIALS; k++) {
            EDD_TRIAL *t = &u->t[k];

            t->asked = 1;
            t->size = trial_size[k];
            edd_prepare(area, t->size);
            pf("unit %02Xh, function 48h, %02Xh buffer... ", u->unit,
               t->size);
            ops->flush(ops->ctx);
            call13(ops, &t->r, 0x4800, 0, u->unit);
            memcpy(t->raw, area, t->size);
            edd_guard_scan(area, t->size, t);
            if (!t->r.made) {
                pf("DPMI error %04Xh\n", t->r.dpmi_err);
                continue;
            }
            pf("CF=%u AH=%02Xh size word %04Xh", t->r.flags & 1,
               (u8)(t->r.ax >> 8), rd16(t->raw));
            pf("\n");
            if (t->guard_changed != 0)
                pf("  %u byte(s) written past the buffer\n",
                   t->guard_changed);
        }
    }
    ops->buf_close(ops->ctx);

    nx = ops->pci_scan_xhci(ops->ctx, xs, EDD_MAX_XHCI);
    if (nx == 0)
        pf("PCI: no xHCI function (class 0C0330) found.\n");
    for (n = 0; n < nx; n++)
        pf("PCI: xHCI %02X:%02X.%X %04X:%04X\n", xs[n].bus, xs[n].dev,
           xs[n].fn, xs[n].vid, xs[n].did);
    for (i = 0; i < EDD_UNIT_COUNT; i++) {
        for (k = 0; k < EDD_TRIALS; k++) {
            EDD_TRIAL *t = &units[i].t[k];
            EDD_PATH p;

            if (!t->asked)
                continue;
            edd_decode(t->raw, t->size, &p);
            if (!p.has_key || p.layout == EDD_LAYOUT_UNKNOWN || p.nofit ||
                !(p.is_pci || p.is_pcix) || !addr_ok(&p))
                continue;
            t->pci_checked = 1;
            t->pci_present = ops->pci_id(ops->ctx, p.ipath[0], p.ipath[1],
                                         p.ipath[2], &t->pci_vid, &t->pci_did,
                                         &t->pci_class);
            for (n = 0; n < nx; n++) {
                if (xs[n].bus == p.ipath[0] && xs[n].dev == p.ipath[1] &&
                    xs[n].fn == p.ipath[2])
                    t->pci_in_scan = 1;
            }
        }
    }
    writes = ops->pci_writes(ops->ctx);
    if (writes == 0)
        pf("Probe safety: PASS - no PCI configuration writes.\n");
    else
        pf("Probe safety: INTERNAL FAILURE - %lu PCI configuration "
           "write(s).\n", writes);

    for (i = 0; i < EDD_UNIT_COUNT; i++)
        report_edd_unit(&units[i], detail);
    pf("\nEDD device path, per unit:\n");
    for (i = 0; i < EDD_UNIT_COUNT; i++) {
        edd_conclude(&units[i], line);
        pf("  %s\n", line);
    }
    return 1;
}
