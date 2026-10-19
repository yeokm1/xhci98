/*
 * sysprobe.c - the DOS half of the system description (roadmap-hcd.md task
 * 36.48): reads the hardware and hands the bytes to sysinfo.c, which is
 * pure and host-tested. Reads only: the BIOS area below 1 MB (directly, the
 * extender's zero-based flat model maps it 1:1), one DPMI 0800h mapping of
 * the SMBIOS structure table when it lies above 1 MB, CPUID, and three
 * real-mode services through DPMI 0300h - INT 15h E820, INT 21h 30h/3306h,
 * INT 2Fh 1600h. No PCI access, no port I/O, nothing written but the
 * 32-byte E820 buffer this file allocates and frees. Two things here are
 * not the system description: sys_dos_creatnew, --log-stamp's exclusive
 * create (task 38.1), which is here because it needs the same real-mode
 * call, and sys_crit_install / sys_crit_remove, the INT 24h chain that
 * turns an Abort into a Fail (DOS-1), which share its DPMI services.
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
 * rm->flags bit 0). */
static int rm_int(u8 intno, RMREGS *rm)
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
    return r.w.cflag == 0;
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
