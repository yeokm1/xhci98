/*
 * sysprobe.c - the DOS half of the system description (roadmap-hcd.md task
 * 36.48): reads the hardware and hands the bytes to sysinfo.c, which is
 * pure and host-tested. Reads only: the BIOS area below 1 MB (directly, the
 * extender's zero-based flat model maps it 1:1), one DPMI 0800h mapping of
 * the SMBIOS structure table when it lies above 1 MB, CPUID, and three
 * real-mode services through DPMI 0300h - INT 15h E820, INT 21h 30h/3306h,
 * INT 2Fh 1600h. No PCI access, no port I/O, nothing written but the
 * 32-byte E820 buffer this file allocates and frees. Three things here are
 * not the system description: sys_dos_creatnew, --log-stamp's exclusive
 * create (task 38.1), which is here because it needs the same real-mode
 * call, and sys_crit_install / sys_crit_remove, the INT 24h chain that
 * turns an Abort into a Fail (DOS-1), which share its DPMI services; and
 * edd_dos_ops, the machine half of --edd (task 47.24, at the end), whose
 * PCI reads come after every INT 13h call it makes.
 *
 * No identifiers: see sysinfo.c and qual.h. Nothing here reads a serial
 * number, UUID, asset tag or SKU; the table bytes go straight to
 * smb_parse_table, which never copies those fields.
 */

#include <i86.h>
#include <stdio.h>
#include <string.h>
#include "qual.h"

#define BIOS_AREA      0xF0000UL
#define BIOS_AREA_LEN  0x10000UL
#define LOW_MEM_END    0x100000UL

/* DPMI 0300h's real-mode call structure (DPMI 0.9 section 3.1.1). */
typedef struct {
    u32 edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    u16 flags, es, ds, fs, gs, ip, cs, sp, ss;
} RMREGS;

/* Simulate a real-mode INT through the DPMI host; SS:SP 0 asks the host
 * for its own stack. 1 if the host made the call (the INT's own CF is in
 * rm->flags bit 0); 0 if it did not, with DPMI's error code in *err. */
static int rm_int_err(u8 intno, RMREGS *rm, u16 *err)
{
    union REGS r;
    struct SREGS s;

    memset(&r, 0, sizeof(r));
    segread(&s);
    r.w.ax = 0x0300;
    r.h.bl = intno;
    r.h.bh = 0;
    r.w.cx = 0;
    r.x.edi = (unsigned int)rm;
    s.es = s.ds;
    int386x(0x31, &r, &r, &s);
    if (err != 0)
        *err = r.w.cflag ? r.w.ax : 0;
    return r.w.cflag == 0;
}

static int rm_int(u8 intno, RMREGS *rm)
{
    return rm_int_err(intno, rm, 0);
}

/* A table straddling 1 MB, assembled from its two halves. */
static u8 straddle[SMB_TABLE_MAX];

static void probe_smbios(SYSINFO *si)
{
    const u8 *t;
    u32 len, base, off, size;
    void *map;

    if (smb_scan((const u8 *)BIOS_AREA, BIOS_AREA_LEN, BIOS_AREA, si) == 0 ||
        si->smb_state != SMB_OK) {
        /* No usable SMBIOS: the ROM's own date and model byte, which every
         * PC BIOS since the PC/XT keeps at F000:FFF5 and F000:FFFE. */
        rom_fill(si, (const u8 *)0xFFFF5UL, *(const u8 *)0xFFFFEUL);
        return;
    }

    len = si->table_len;
    if (len > SMB_TABLE_MAX)
        len = SMB_TABLE_MAX;
    if (si->table_lo + len <= LOW_MEM_END) {
        /* Below 1 MB (old firmware puts it in the E- or F-segment):
         * linear == physical, no mapping. */
        smb_parse_table((const u8 *)si->table_lo, len, si->table_count, si);
        return;
    }
    if (si->table_lo < LOW_MEM_END) {
        /* Straddling 1 MB: DPMI 0800h refuses an
         * address below 1 MB, so the low part is copied directly and only
         * the part from 100000h is mapped, into one bounded buffer. */
        u32 low;

        low = LOW_MEM_END - si->table_lo;
        map = dpmi_map_phys(LOW_MEM_END, (len - low + 0xFFFUL) & ~0xFFFUL);
        if (map == 0) {
            si->smb_state = SMB_MAP_FAILED;
            rom_fill(si, (const u8 *)0xFFFF5UL, *(const u8 *)0xFFFFEUL);
            return;
        }
        memcpy(straddle, (const u8 *)si->table_lo, low);
        memcpy(straddle + low, map, len - low);
        dpmi_unmap_phys(map);
        smb_parse_table(straddle, len, si->table_count, si);
        return;
    }
    /* Above 1 MB: one page-aligned DPMI mapping, as for BAR0. */
    base = si->table_lo & ~0xFFFUL;
    off = si->table_lo - base;
    size = (off + len + 0xFFFUL) & ~0xFFFUL;
    map = dpmi_map_phys(base, size);
    if (map == 0) {
        si->smb_state = SMB_MAP_FAILED;
        rom_fill(si, (const u8 *)0xFFFF5UL, *(const u8 *)0xFFFFEUL);
        return;
    }
    t = (const u8 *)map + off;
    smb_parse_table(t, len, si->table_count, si);
    dpmi_unmap_phys(map);
}

static void probe_cpu(SYSINFO *si)
{
    u32 r[4];
    u32 brand[12];
    u32 maxleaf, maxext;
    int i;

    if (!sys_has_cpuid())
        return;
    si->has_cpuid = 1;
    sys_cpuid(0, r);
    maxleaf = r[0];
    cpu_fill_vendor(si, r);
    if (maxleaf >= 1) {
        sys_cpuid(1, r);
        cpu_fill_signature(si, r[0]);
    }
    sys_cpuid(0x80000000UL, r);
    maxext = r[0];
    /* A CPU without the extended range answers with basic-leaf data, so
     * the value must look like an extended leaf number to be believed. */
    if (maxext >= 0x80000004UL && maxext <= 0x8000FFFFUL) {
        for (i = 0; i < 3; i++)
            sys_cpuid(0x80000002UL + (u32)i, &brand[i * 4]);
        cpu_fill_brand(si, brand);
    }
}

/* INT 15h E820 into a 32-byte conventional block (DPMI 0100h), summing the
 * type 1 (usable) ranges. Bounded at 128 calls. */
static void probe_e820(SYSINFO *si)
{
    union REGS r;
    RMREGS rm;
    u16 seg, sel;
    const u8 *buf;
    u32 cont;
    int calls;

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0100;
    r.w.bx = 2;                         /* paragraphs */
    int386(0x31, &r, &r);
    if (r.w.cflag)
        return;
    seg = r.w.ax;
    sel = r.w.dx;
    buf = (const u8 *)((u32)seg << 4);

    cont = 0;
    for (calls = 0; calls < 128; calls++) {
        memset(&rm, 0, sizeof(rm));
        rm.eax = 0xE820UL;
        rm.edx = 0x534D4150UL;          /* 'SMAP' */
        rm.ecx = 20;
        rm.ebx = cont;
        rm.es = seg;
        rm.edi = 0;
        memset((void *)buf, 0, 20);
        if (!rm_int(0x15, &rm) || (rm.flags & 1) != 0 ||
            rm.eax != 0x534D4150UL || rm.ecx < 20)
            break;
        if (buf[16] == 1 && buf[17] == 0 && buf[18] == 0 && buf[19] == 0) {
            u32 lo, hi, kb;

            lo = (u32)buf[8] | ((u32)buf[9] << 8) | ((u32)buf[10] << 16) |
                 ((u32)buf[11] << 24);
            hi = (u32)buf[12] | ((u32)buf[13] << 8) | ((u32)buf[14] << 16) |
                 ((u32)buf[15] << 24);
            kb = (hi >= 0x400UL) ? 0xFFFFFFFFUL : (hi << 22) | (lo >> 10);
            si->e820_kb = (si->e820_kb + kb < si->e820_kb) ?
                          0xFFFFFFFFUL : si->e820_kb + kb;
            si->e820_n++;
        }
        cont = rm.ebx;
        if (cont == 0)
            break;
    }

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0101;
    r.w.dx = sel;
    int386(0x31, &r, &r);
}

static void probe_dos(SYSINFO *si)
{
    RMREGS rm;
    u8 al;

    memset(&rm, 0, sizeof(rm));
    rm.eax = 0x3000;
    if (rm_int(0x21, &rm)) {
        si->dos_read = 1;
        si->dos_major = (u8)rm.eax;
        si->dos_minor = (u8)(rm.eax >> 8);
        si->dos_oem = (u8)(rm.ebx >> 8);
    }
    /* The true version (DOS 5+), which SETVER cannot change. */
    memset(&rm, 0, sizeof(rm));
    rm.eax = 0x3306;
    if (rm_int(0x21, &rm) && (u8)rm.eax != 0xFF && (u8)rm.ebx >= 5) {
        si->dos_true = 1;
        si->dos_true_major = (u8)rm.ebx;
        si->dos_true_minor = (u8)(rm.ebx >> 8);
    }
    /* Windows enhanced mode installation check: AL 00h/80h none, 01h/FFh
     * Windows/386 2.x, else AL.AH is the version (4.10 is Windows 98). */
    memset(&rm, 0, sizeof(rm));
    rm.eax = 0x1600;
    if (rm_int(0x2F, &rm)) {
        al = (u8)rm.eax;
        if (al == 0x01 || al == 0xFF) {
            si->win_state = 1;
        } else if (al != 0x00 && al != 0x80) {
            si->win_state = 2;
            si->win_major = al;
            si->win_minor = (u8)(rm.eax >> 8);
        }
    }
}

void sysinfo_probe(SYSINFO *si)
{
    memset(si, 0, sizeof(*si));
    probe_smbios(si);
    probe_cpu(si);
    probe_e820(si);
    probe_dos(si);
    sysinfo_seal(si);
}

/*
 * Task 38.1, --log-stamp: INT 21h AH=5Bh ("create new file"), which fails
 * with 50h where AH=3Ch would truncate, made in real mode.
 *
 * Not _dos_creatnew: that issues AH=5Bh in protected mode, and DOS/32A
 * 9.1.2 (the stub wlink embeds) creates the file but hands EAX back as it
 * went in, 5B00h, not the handle DOS returned. Measured 2026-10-13 in QEMU
 * on MS-DOS 7.1: _dos_creatnew answered 0 with handle 5B00h, every write to
 * it failed with error 6 (invalid handle), and the log stayed 0 bytes,
 * while the real handle stayed open until exit. Through DPMI 0300h the
 * real-mode AX comes back as DOS left it. The name has to be in
 * conventional memory for a real-mode DS:DX, so it goes in a DPMI 0100h
 * block, freed before return. Writes and the close are protected-mode
 * AH=40h and AH=3Eh (_dos_write, _dos_close), which DOS/32A passes back
 * correctly. A write-protected disk raises DOS's Abort, Retry, Fail here
 * as it would in protected mode: Fail returns an error, Abort ends the
 * program with exit code 0.
 */
unsigned sys_dos_creatnew(const char *name, int *handle)
{
    union REGS r;
    RMREGS rm;
    size_t len;
    u16 seg, sel;
    unsigned rc;

    len = strlen(name);
    if (len > 127)
        return 0x03;                    /* path not found */
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0100;
    r.w.bx = (u16)((len + 1 + 15) / 16);   /* paragraphs */
    int386(0x31, &r, &r);
    if (r.w.cflag)
        return 0x08;                    /* insufficient memory */
    seg = r.w.ax;
    sel = r.w.dx;
    memcpy((void *)((u32)seg << 4), name, len + 1);

    memset(&rm, 0, sizeof(rm));
    rm.eax = 0x5B00;
    rm.ecx = 0;                         /* normal attributes */
    rm.ds = seg;
    rm.edx = 0;
    if (!rm_int(0x21, &rm)) {
        rc = 0x1F;                      /* general failure */
    } else if (rm.flags & 1) {
        rc = (u16)rm.eax;
    } else {
        *handle = (int)(u16)rm.eax;
        rc = 0;
    }

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0101;
    r.w.dx = sel;
    int386(0x31, &r, &r);
    return rc;
}

/*
 * DOS-1 (38.1 MS-DOS leg): an Abort answer to DOS's Abort, Retry, Fail
 * killed DOS/32A's next start in the same boot. Abort makes DOS end the
 * process inside its own critical-error code (MS-DOS 4.0 CTRLC.ASM,
 * DoAbort: "Transfer exit_inner", an internal jump), never through the
 * INT 21h vector, so DOS/32A's real-mode INT 21h hook never sees an AH=4Ch
 * and its exit (text/kernel/exit.asm, int21h_pm) is skipped: the real-mode
 * INT 21h, 15h, 1Bh, 1Ch, 23h, 24h and 2Fh vectors stay on the freed
 * extender, and its XMS block and A20 state are kept. The extender guards
 * Ctrl-C (its client installs a protected-mode INT 23h, which only sets a
 * flag) but not INT 24h, which stays COMMAND.COM's.
 *
 * So the real-mode INT 24h is chained: COMMAND.COM still asks (Retry
 * still retries), and an Abort comes back to DOS as Fail, which DOS
 * returns to the call as an error the tool reports itself. Real mode,
 * not a protected-mode INT 24h, because every DOS call the tool makes -
 * protected-mode INT 21h too - runs DOS in real mode, so DOS raises the
 * INT 24h there either way. Where DOS does not allow Fail for that error
 * the Abort is left alone: DOS would turn a Fail into an Abort anyway.
 * DOS/32A's exit puts back the INT 24h it saved at start, so a run that
 * faults before sys_crit_remove leaves no vector on the freed block.
 */
static const u8 crit_stub[] = {
    0x55,                       /* push bp                         */
    0x50,                       /* push ax       (AH: allowed set) */
    0x9C,                       /* pushf                           */
    0x2E, 0xFF, 0x1E, 0x1B, 0x00, /* call far cs:[001Bh]           */
    0x89, 0xE5,                 /* mov bp,sp                       */
    0x3C, 0x02,                 /* cmp al,2      (Abort?)          */
    0x75, 0x08,                 /* jne 16h                         */
    0xF6, 0x46, 0x01, 0x08,     /* test byte [bp+1],8 (Fail ok?)   */
    0x74, 0x02,                 /* jz 16h                          */
    0xB0, 0x03,                 /* mov al,3      (Fail)            */
    0x83, 0xC4, 0x02,           /* 16h: add sp,2                   */
    0x5D,                       /* pop bp                          */
    0xCF                        /* iret                            */
};
#define CRIT_OLD_VECTOR 0x1B

static u16 crit_sel;
static u16 crit_old_seg, crit_old_off;

int sys_crit_install(void)
{
    union REGS r;
    u16 seg;
    u8 *stub;

    if (crit_sel != 0)
        return 1;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0200;
    r.h.bl = 0x24;
    int386(0x31, &r, &r);
    if (r.w.cflag)
        return 0;
    crit_old_seg = r.w.cx;
    crit_old_off = r.w.dx;

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0100;
    r.w.bx = (u16)((CRIT_OLD_VECTOR + 4 + 15) / 16);
    int386(0x31, &r, &r);
    if (r.w.cflag)
        return 0;
    seg = r.w.ax;
    crit_sel = r.w.dx;
    stub = (u8 *)((u32)seg << 4);
    memcpy(stub, crit_stub, sizeof(crit_stub));
    stub[CRIT_OLD_VECTOR + 0] = (u8)crit_old_off;
    stub[CRIT_OLD_VECTOR + 1] = (u8)(crit_old_off >> 8);
    stub[CRIT_OLD_VECTOR + 2] = (u8)crit_old_seg;
    stub[CRIT_OLD_VECTOR + 3] = (u8)(crit_old_seg >> 8);

    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0201;
    r.h.bl = 0x24;
    r.w.cx = seg;
    r.w.dx = 0;
    int386(0x31, &r, &r);
    if (r.w.cflag) {
        memset(&r, 0, sizeof(r));
        r.w.ax = 0x0101;
        r.w.dx = crit_sel;
        int386(0x31, &r, &r);
        crit_sel = 0;
        return 0;
    }
    return 1;
}

void sys_crit_remove(void)
{
    union REGS r;

    if (crit_sel == 0)
        return;
    /* atexit runs before the runtime flushes the streams, and a redirected
     * stdout's last write can raise INT 24h: it must still find the chain
     * (task 38.1). */
    fflush(stdout);
    fflush(stderr);
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0201;
    r.h.bl = 0x24;
    r.w.cx = crit_old_seg;
    r.w.dx = crit_old_off;
    int386(0x31, &r, &r);
    /* The block is freed only once nothing points at it. */
    if (r.w.cflag)
        return;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0101;
    r.w.dx = crit_sel;
    int386(0x31, &r, &r);
    crit_sel = 0;
}

/*
 * Task 47.24, --edd: the operations edd_run (edd.c) makes, in the order it
 * makes them. INT 13h 41h and 48h only, through DPMI 0300h, with a real-mode
 * stack and buffer of this file's own in one DPMI 0100h block: 4 KiB of
 * stack (SS = the block, SP = 1000h; the host's default real-mode stack is
 * small, and the firmware's disk code is not), then EDD_AREA bytes, the
 * buffer at offset 0 of segment block + 100h (DS:SI as the BIOS wants it,
 * never a protected-mode pointer) and its guard bytes after it. The PCI
 * reads (pci.c, CF8h/CFCh, reads only) come after the block is freed.
 */
#define EDD_STACK_PARAS 0x100U
#define EDD_BLOCK_PARAS (EDD_STACK_PARAS + EDD_AREA / 16)

static u16 edd_seg, edd_sel;
static SYSINFO *edd_si;
static int edd_skip_si;
static QPRINTF_FN edd_pf, edd_detail;

static int op_win1600(void *ctx, u16 *ax)
{
    RMREGS rm;

    (void)ctx;
    memset(&rm, 0, sizeof(rm));
    rm.eax = 0x1600;
    if (!rm_int(0x2F, &rm))
        return 0;
    *ax = (u16)rm.eax;
    return 1;
}

static int op_dos3306(void *ctx, u16 *ax, u16 *bx)
{
    RMREGS rm;

    (void)ctx;
    memset(&rm, 0, sizeof(rm));
    rm.eax = 0x3306;
    if (!rm_int(0x21, &rm))
        return 0;
    *ax = (u16)rm.eax;
    *bx = (u16)rm.ebx;
    return 1;
}

static int op_dpmi0400(void *ctx, u16 *ax, u16 *bx)
{
    union REGS r;

    (void)ctx;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0400;
    int386(0x31, &r, &r);
    if (r.w.cflag)
        return 0;
    *ax = r.w.ax;
    *bx = r.w.bx;
    return 1;
}

/* The BIOS data area's hard disk count, 0040:0075h (the flat model maps
 * the first megabyte 1:1). */
static unsigned op_bda_disks(void *ctx)
{
    (void)ctx;
    return *(const u8 *)0x475UL;
}

static void op_sysinfo(void *ctx)
{
    (void)ctx;
    if (edd_skip_si)
        sysinfo_skip(edd_si);
    else
        sysinfo_probe(edd_si);
    /* With a log, two lines on the screen and the block in the log, as the
     * quick scan does; without one, the block on the screen. */
    if (edd_detail != edd_pf)
        report_sysinfo_short(edd_si, qconprintf);
    report_sysinfo(edd_si, edd_detail);
}

static u8 *op_buf_open(void *ctx)
{
    union REGS r;

    (void)ctx;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0100;
    r.w.bx = EDD_BLOCK_PARAS;
    int386(0x31, &r, &r);
    if (r.w.cflag)
        return 0;
    edd_seg = r.w.ax;
    edd_sel = r.w.dx;
    return (u8 *)(((u32)edd_seg + EDD_STACK_PARAS) << 4);
}

static void op_buf_close(void *ctx)
{
    union REGS r;

    (void)ctx;
    if (edd_sel == 0)
        return;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0101;
    r.w.dx = edd_sel;
    int386(0x31, &r, &r);
    edd_sel = 0;
    edd_seg = 0;
}

static void op_int13(void *ctx, EDD_REGS *e)
{
    RMREGS rm;
    u16 err;

    (void)ctx;
    memset(&rm, 0, sizeof(rm));
    rm.eax = e->ax;
    rm.ebx = e->bx;
    rm.ecx = e->cx;
    rm.edx = e->dx;
    rm.flags = 0x0202;                  /* IF set in the IRET frame */
    rm.ss = edd_seg;
    rm.sp = (u16)(EDD_STACK_PARAS * 16);
    rm.ds = (u16)(edd_seg + EDD_STACK_PARAS);
    rm.esi = 0;
    err = 0;
    e->made = rm_int_err(0x13, &rm, &err);
    e->dpmi_err = err;
    if (!e->made)
        return;
    e->ax = (u16)rm.eax;
    e->bx = (u16)rm.ebx;
    e->cx = (u16)rm.ecx;
    e->dx = (u16)rm.edx;
    e->flags = rm.flags;
}

static PCIINFO edd_pcis[EDD_MAX_XHCI];

static int op_pci_scan_xhci(void *ctx, EDD_XHCI *out, int max)
{
    int n, i;

    (void)ctx;
    if (max > EDD_MAX_XHCI)
        max = EDD_MAX_XHCI;
    n = pci_scan_usb(edd_pcis, max, HC_MASK_XHCI);
    for (i = 0; i < n; i++) {
        out[i].bus = edd_pcis[i].bus;
        out[i].dev = edd_pcis[i].dev;
        out[i].fn = edd_pcis[i].fn;
        out[i].vid = edd_pcis[i].vid;
        out[i].did = edd_pcis[i].did;
    }
    return n;
}

static int op_pci_id(void *ctx, u8 bus, u8 dev, u8 fn, u16 *vid, u16 *did,
                     u32 *classcode)
{
    (void)ctx;
    *vid = pci_read16(bus, dev, fn, 0x00);
    if (*vid == 0xFFFF)
        return 0;
    *did = pci_read16(bus, dev, fn, 0x02);
    *classcode = pci_read32(bus, dev, fn, 0x08) >> 8;
    return 1;
}

static u32 op_pci_writes(void *ctx)
{
    (void)ctx;
    return pci_config_write_count;
}

static void op_flush(void *ctx)
{
    (void)ctx;
    report_flush();
}

void edd_dos_ops(EDD_OPS *ops, SYSINFO *si, int skip_sysinfo,
                 QPRINTF_FN pf, QPRINTF_FN detail)
{
    edd_si = si;
    edd_skip_si = skip_sysinfo;
    edd_pf = pf;
    edd_detail = detail;
    ops->ctx = 0;
    ops->win1600 = op_win1600;
    ops->dos3306 = op_dos3306;
    ops->dpmi0400 = op_dpmi0400;
    ops->bda_disks = op_bda_disks;
    ops->sysinfo = op_sysinfo;
    ops->buf_open = op_buf_open;
    ops->buf_close = op_buf_close;
    ops->int13 = op_int13;
    ops->pci_scan_xhci = op_pci_scan_xhci;
    ops->pci_id = op_pci_id;
    ops->pci_writes = op_pci_writes;
    ops->flush = op_flush;
}
