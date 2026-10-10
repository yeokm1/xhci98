/*
 * test_sysinfo.c - host-side unit tests for xhciqual/sysinfo.c, the system
 * description in the report header (roadmap-hcd.md task 36.48).
 *
 * Vectors: a valid SMBIOS 2.x entry and table, a 3.0 entry (preferred, and
 * passed over when its table is above 4 GB), a legacy DMI entry, bad
 * checksums, a truncated table, a garbled one, missing and out-of-range
 * strings, memory sums, CPUID decoding and the no-SMBIOS fallback. Every
 * table plants a serial number, UUID, asset tag, SKU and part number, and
 * every printed line is checked for each of them: the no-identifiers rule
 * is a test, not a promise.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework, no DOS dependencies.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>     /* VirtualAlloc: the guard page */
#include "../qual.h"

static char out[8192];
static int failures;
static int checks;

static void cap(const char *fmt, ...)
{
    va_list ap;
    size_t used;

    used = strlen(out);
    va_start(ap, fmt);
    vsprintf(out + used, fmt, ap);
    va_end(ap);
}

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL line %d: %s\n", line, what);
        printf("  output was: %s", out[0] ? out : "(empty)\n");
    }
}

static int has(const char *needle)
{
    return strstr(out, needle) != 0;
}

/* The planted identifiers. None may ever reach a printed line. */
static const char *secrets[] = {
    "SER-T1-SECRET", "SKU-T1-SECRET", "SER-T2-SECRET", "ASSET-T2-SECRET",
    "SER-T3-SECRET", "ASSET-T3-SECRET", "SER-T4-SECRET", "ASSET-T4-SECRET",
    "PART-T4-SECRET", "DEADBEEF", "deadbeef", "EF BE AD DE", 0
};

static void check_no_secrets(const char *where)
{
    int i;
    char what[160];

    for (i = 0; secrets[i] != 0; i++) {
        sprintf(what, "%s: no '%s' in the output", where, secrets[i]);
        CHECK(!has(secrets[i]), what);
    }
}

/* ------------------------------------------------------------------ */
/* Table builder                                                       */
/* ------------------------------------------------------------------ */

static u8 tab[0x2000];
static u32 tlen;

static void put8(u8 *f, int at, u8 v) { f[at] = v; }
static void put16(u8 *f, int at, u16 v)
{
    f[at] = (u8)v;
    f[at + 1] = (u8)(v >> 8);
}
static void put32(u8 *f, int at, u32 v)
{
    put16(f, at, (u16)v);
    put16(f, at + 2, (u16)(v >> 16));
}

/* Append a structure: formatted area f[flen] (type and length filled in
 * here), then the strings (NULL-terminated list) and the double NUL. */
/* Where each structure added since the last reset ends. */
static u32 ends[32];
static int nends;

static void add(u8 type, u8 *f, u8 flen, const char **strs)
{
    int i;

    f[0] = type;
    f[1] = flen;
    memcpy(tab + tlen, f, flen);
    tlen += flen;
    if (strs == 0 || strs[0] == 0) {
        tab[tlen++] = 0;
        tab[tlen++] = 0;
    } else {
        for (i = 0; strs[i] != 0; i++) {
            size_t n = strlen(strs[i]);
            memcpy(tab + tlen, strs[i], n + 1);
            tlen += (u32)n + 1;
        }
        tab[tlen++] = 0;
    }
    if (nends < 32)
        ends[nends++] = tlen;
}

/*
 * A window that ends at an inaccessible page (review round 1, P2): any read
 * past the length handed to the parser faults the test instead of passing
 * it. Two committed regions of 64 KB, each followed by a PAGE_NOACCESS page.
 */
#define GUARD_SPAN 0x10000UL
static u8 *guard_base;

static u8 *guard_end(void)
{
    if (guard_base == 0) {
        DWORD old;

        guard_base = (u8 *)VirtualAlloc(0, GUARD_SPAN + 0x1000,
                                        MEM_RESERVE | MEM_COMMIT,
                                        PAGE_READWRITE);
        if (guard_base == 0 ||
            !VirtualProtect(guard_base + GUARD_SPAN, 0x1000, PAGE_NOACCESS,
                            &old)) {
            printf("FAIL: guard page not set up\n");
            exit(100);
        }
    }
    return guard_base + GUARD_SPAN;
}

/* Copy n bytes so they end exactly at the guard page; return the start. */
static const u8 *at_guard(const u8 *src, u32 n)
{
    u8 *p = guard_end() - n;

    memcpy(p, src, n);
    return p;
}

static int ram17_mb;

/* The full machine: types 0, 1, 2, 3, 4, 17 (two modules), 19, 127. */
static void build_table(int with_end)
{
    u8 f[0x40];
    static const char *s0[] = { "Test BIOS Vendor", "1.23  ", "01/02/2026",
                                0 };
    static const char *s1[] = { "Test Maker", "Model  X", "Rev A",
                                "SER-T1-SECRET", "SKU-T1-SECRET",
                                "Test Family", 0 };
    static const char *s2[] = { "Board Maker", "Board 7", "v1",
                                "SER-T2-SECRET", "ASSET-T2-SECRET", 0 };
    static const char *s3[] = { "Chassis Maker", "SER-T3-SECRET",
                                "ASSET-T3-SECRET", 0 };
    static const char *s4[] = { "U3E1", "Intel(R) Corporation",
                                "Test CPU Version", "SER-T4-SECRET",
                                "ASSET-T4-SECRET", "PART-T4-SECRET", 0 };
    int i;

    tlen = 0;
    nends = 0;
    memset(tab, 0, sizeof(tab));

    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x05, 2); put8(f, 0x08, 3);
    add(0, f, 0x18, s0);

    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x05, 2); put8(f, 0x06, 3);
    put8(f, 0x07, 4);                       /* serial */
    for (i = 0; i < 16; i++)                /* UUID: EF BE AD DE ... */
        f[0x08 + i] = (u8)((i & 3) == 0 ? 0xEF : (i & 3) == 1 ? 0xBE :
                           (i & 3) == 2 ? 0xAD : 0xDE);
    put8(f, 0x19, 5);                       /* SKU */
    put8(f, 0x1A, 6);                       /* family */
    add(1, f, 0x1B, s1);

    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x05, 2); put8(f, 0x06, 3);
    put8(f, 0x07, 4); put8(f, 0x08, 5);
    add(2, f, 0x0F, s2);

    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x07, 2); put8(f, 0x08, 3);
    add(3, f, 0x15, s3);

    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x07, 2); put8(f, 0x10, 3);
    put8(f, 0x18, 0x41);                    /* populated, enabled */
    put8(f, 0x20, 4); put8(f, 0x21, 5); put8(f, 0x22, 6);
    add(4, f, 0x2A, s4);

    memset(f, 0, sizeof(f));
    put16(f, 0x0C, 4096);                   /* 4096 MB */
    add(17, f, 0x22, 0);
    memset(f, 0, sizeof(f));
    put16(f, 0x0C, 0x7FFF);                 /* extended: 8192 MB */
    put32(f, 0x1C, 8192);
    add(17, f, 0x22, 0);
    memset(f, 0, sizeof(f));
    put16(f, 0x0C, 0);                      /* empty slot */
    add(17, f, 0x22, 0);
    ram17_mb = 12288;

    memset(f, 0, sizeof(f));
    put32(f, 0x04, 0);
    put32(f, 0x08, 0x00FFFFFFUL);           /* 16 GB - 1 KB, in KB */
    add(19, f, 0x0F, 0);

    if (with_end) {
        memset(f, 0, sizeof(f));
        add(127, f, 4, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

static u8 area[0x10000];

static void fix_sum(u8 *p, int n, int at)
{
    int i;
    u8 s;

    p[at] = 0;
    s = 0;
    for (i = 0; i < n; i++)
        s = (u8)(s + p[i]);
    p[at] = (u8)(0x100 - s);
}

static void put_ep2(u32 off, u8 major, u8 minor, u32 addr, u16 len,
                    u16 count)
{
    u8 *p = area + off;

    memset(p, 0, 0x20);
    memcpy(p, "_SM_", 4);
    p[5] = 0x1F;
    p[6] = major;
    p[7] = minor;
    memcpy(p + 0x10, "_DMI_", 5);
    put16(p, 0x16, len);
    put32(p, 0x18, addr);
    put16(p, 0x1C, count);
    p[0x1E] = (u8)((major << 4) | minor);
    fix_sum(p + 0x10, 0x0F, 0x05);
    fix_sum(p, 0x1F, 0x04);
}

static void put_ep3(u32 off, u8 major, u8 minor, u32 lo, u32 hi, u32 len)
{
    u8 *p = area + off;

    memset(p, 0, 0x18);
    memcpy(p, "_SM3_", 5);
    p[6] = 0x18;
    p[7] = major;
    p[8] = minor;
    p[0x0A] = 1;
    put32(p, 0x0C, len);
    put32(p, 0x10, lo);
    put32(p, 0x14, hi);
    fix_sum(p, 0x18, 0x05);
}

static void put_dmi(u32 off, u32 addr, u16 len, u16 count, u8 bcd)
{
    u8 *p = area + off;

    memset(p, 0, 0x10);
    memcpy(p, "_DMI_", 5);
    put16(p, 6, len);
    put32(p, 8, addr);
    put16(p, 0x0C, count);
    p[0x0E] = bcd;
    fix_sum(p, 0x0F, 0x05);
}

static void fill_cpu_dos(SYSINFO *si)
{
    u32 leaf0[4];
    u32 brand[12];
    static const char b[49] =
        "      Intel(R) Core(TM) i5-6200U CPU @ 2.30GHz  ";
    int i;

    leaf0[0] = 0x16;
    leaf0[1] = 0x756E6547UL;    /* "Genu" */
    leaf0[3] = 0x49656E69UL;    /* "ineI" */
    leaf0[2] = 0x6C65746EUL;    /* "ntel" */
    si->has_cpuid = 1;
    cpu_fill_vendor(si, leaf0);
    cpu_fill_signature(si, 0x000406E3UL);   /* Skylake-U, 06_4Eh step 3 */
    for (i = 0; i < 12; i++)
        brand[i] = (u32)(u8)b[i * 4] | ((u32)(u8)b[i * 4 + 1] << 8) |
                   ((u32)(u8)b[i * 4 + 2] << 16) |
                   ((u32)(u8)b[i * 4 + 3] << 24);
    cpu_fill_brand(si, brand);
    si->e820_kb = 8000UL * 1024UL;
    si->e820_n = 6;
    si->dos_read = 1;
    si->dos_major = 7;
    si->dos_minor = 10;
    si->dos_oem = 0xFF;
}

static void render(const SYSINFO *si)
{
    out[0] = '\0';
    report_sysinfo(si, cap);
}

static void render_short(const SYSINFO *si)
{
    char *p, *nl;
    int ok;

    out[0] = '\0';
    report_sysinfo_short(si, cap);
    ok = 1;
    for (p = out; *p != '\0'; p = nl + 1) {
        nl = strchr(p, '\n');
        if (nl == 0)
            break;
        if (nl - p > 79)
            ok = 0;
    }
    CHECK(ok, "short lines at most 79 characters");
}

/* ------------------------------------------------------------------ */

static void test_valid_2x(void)
{
    SYSINFO si;

    memset(&si, 0, sizeof(si));
    memset(area, 0xFF, sizeof(area));
    build_table(1);
    put_ep2(0x5A30, 2, 8, 0x000F6000UL, (u16)tlen, 10);
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == SMB_EP_32,
          "2.x: _SM_ found");
    CHECK(si.smb_state == SMB_OK, "2.x: state OK");
    CHECK(si.smb_ep_phys == 0xF5A30UL, "2.x: entry address");
    CHECK(si.smb_major == 2 && si.smb_minor == 8, "2.x: version 2.8");
    CHECK(si.table_lo == 0xF6000UL && si.table_len == tlen &&
          si.table_count == 10, "2.x: table address, length, count");
    smb_parse_table(tab, tlen, si.table_count, &si);
    CHECK(si.nstructs == 10 && !si.broken, "2.x: ten structures, intact");
    CHECK(strcmp(si.bios_vendor, "Test BIOS Vendor") == 0, "T0 vendor");
    CHECK(strcmp(si.bios_version, "1.23") == 0, "T0 version trimmed");
    CHECK(strcmp(si.bios_date, "01/02/2026") == 0, "T0 date");
    CHECK(strcmp(si.sys_mfr, "Test Maker") == 0, "T1 maker");
    CHECK(strcmp(si.sys_product, "Model X") == 0, "T1 product, spaces run");
    CHECK(strcmp(si.sys_version, "Rev A") == 0, "T1 version");
    CHECK(strcmp(si.sys_family, "Test Family") == 0, "T1 family");
    CHECK(strcmp(si.board_mfr, "Board Maker") == 0 &&
          strcmp(si.board_product, "Board 7") == 0, "T2 maker, product");
    CHECK(strcmp(si.cpu_socket, "U3E1") == 0 &&
          strcmp(si.cpu_version, "Test CPU Version") == 0,
          "T4 socket, version");
    CHECK(si.mem17_n == 2 && (si.mem17_kb >> 10) == (u32)ram17_mb,
          "T17: two modules, 12288 MB (empty slot skipped)");
    CHECK(si.mem19_n == 1 && (si.mem19_kb >> 10) == 16384UL,
          "T19: 16384 MB mapped");

    fill_cpu_dos(&si);
    render(&si);
    CHECK(has("System:  Test Maker Model X, version Rev A, family Test "
              "Family\n"), "System line");
    CHECK(has("Board:   Board Maker Board 7\n"), "Board line");
    CHECK(has("BIOS:    Test BIOS Vendor 1.23, 01/02/2026; SMBIOS 2.8 "
              "(32-bit entry at F5A30h, 10 structures)\n"), "BIOS line");
    CHECK(has("CPU:     Intel(R) Core(TM) i5-6200U CPU @ 2.30GHz "
              "(GenuineIntel family 06h model 4Eh stepping 3), socket "
              "U3E1\n"), "CPU line: brand trimmed, family/model/stepping");
    CHECK(has("RAM:     8000 MB usable (INT 15h E820, 6 ranges)\n"),
          "RAM from E820");
    CHECK(has("DOS:     7.10 (Microsoft); not under Windows\n"), "DOS line");
    CHECK(!has("malformed"), "no damage reported");
    check_no_secrets("2.x full");
    render_short(&si);
    CHECK(has("System: Test Maker Model X; BIOS 1.23 01/02/2026\n"),
          "short system line");
    CHECK(has("CPU: Intel(R) Core(TM) i5-6200U CPU @ 2.30GHz; RAM 8000 MB; "
              "DOS 7.10\n"), "short CPU line");
    check_no_secrets("2.x short");

    /* The RAM fallbacks, and saying which. */
    si.e820_n = 0;
    render(&si);
    CHECK(has("RAM:     12288 MB in 2 module(s) (SMBIOS Type 17; no E820)"),
          "RAM from Type 17");
    si.mem17_n = 0;
    render(&si);
    CHECK(has("RAM:     16384 MB mapped (SMBIOS Type 19; no E820)"),
          "RAM from Type 19");
    si.mem19_n = 0;
    render(&si);
    CHECK(has("RAM:     unknown"), "RAM unknown");

    /* A Windows 98 DOS box, and SETVER's lie. */
    si.win_state = 2;
    si.win_major = 4;
    si.win_minor = 10;
    si.dos_true = 1;
    si.dos_true_major = 7;
    si.dos_true_minor = 10;
    render(&si);
    CHECK(has("DOS:     7.10 (Microsoft); in a Windows 4.10 DOS box "
              "(Windows 98)\n"), "Windows 98 DOS box");
    si.dos_major = 6;
    si.dos_minor = 22;
    render(&si);
    CHECK(has("6.22 (Microsoft), true version 7.10;"), "SETVER version");
    render_short(&si);
    CHECK(has("; DOS 6.22 in Windows 4.10"), "short: Windows");
}

static void test_valid_30(void)
{
    SYSINFO si;

    /* Both entries present: the 64-bit one wins. */
    memset(&si, 0, sizeof(si));
    memset(area, 0, sizeof(area));
    build_table(1);
    put_ep2(0x1000, 2, 8, 0x000F6000UL, (u16)tlen, 10);
    put_ep3(0x2000, 3, 2, 0x7FFE0000UL, 0, tlen);
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == SMB_EP_64,
          "3.0: _SM3_ preferred");
    CHECK(si.smb_state == SMB_OK && si.smb_major == 3 && si.smb_minor == 2,
          "3.0: version 3.2");
    CHECK(si.table_lo == 0x7FFE0000UL && si.table_count == 0,
          "3.0: 64-bit address, no count");
    smb_parse_table(tab, tlen, 0, &si);
    CHECK(si.nstructs == 10 && !si.broken, "3.0: walk ends at type 127");
    fill_cpu_dos(&si);
    render(&si);
    CHECK(has("SMBIOS 3.2 (64-bit entry at F2000h, 10 structures)"),
          "3.0: BIOS line");
    check_no_secrets("3.0");

    /* Its table above 4 GB: the 32-bit entry is taken instead. */
    memset(&si, 0, sizeof(si));
    put_ep3(0x2000, 3, 2, 0x00000000UL, 1, tlen);
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == SMB_EP_32 &&
          si.smb_state == SMB_OK, "3.0 above 4 GB: 2.x entry used");
    /* ...and with no 32-bit entry, said so. */
    memset(&si, 0, sizeof(si));
    memset(area + 0x1000, 0, 0x20);
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == SMB_EP_64 &&
          si.smb_state == SMB_ABOVE_4G, "3.0 above 4 GB alone");
    rom_fill(&si, (const u8 *)"08/22/98", 0xFC);
    render(&si);
    CHECK(has("BIOS:    SMBIOS 3.2 table above 4 GB, not read; ROM BIOS "
              "date 08/22/98, model byte FCh\n"), "above 4 GB line");

    /* A 3.0 entry at the very end of the window is not read past it. */
    memset(&si, 0, sizeof(si));
    memset(area, 0, sizeof(area));
    memcpy(area + 0xFFF0, "_SM3_", 5);
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == 0 &&
          si.other_ep_bad, "_SM3_ in the last paragraph: rejected");
}

static void test_bad_entries(void)
{
    SYSINFO si;

    build_table(1);
    /* A bad entry checksum. */
    memset(&si, 0, sizeof(si));
    memset(area, 0, sizeof(area));
    put_ep2(0x100, 2, 8, 0xF6000UL, (u16)tlen, 10);
    area[0x100 + 0x08] ^= 1;          /* outside the _DMI_ checksum */
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == SMB_EP_DMI,
          "bad _SM_ checksum: falls to its _DMI_ anchor");
    /* Both checksums bad: nothing. */
    memset(&si, 0, sizeof(si));
    area[0x100 + 0x16] ^= 1;
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == 0 &&
          si.smb_state == SMB_NONE && si.other_ep_bad,
          "both checksums bad: no SMBIOS, said so");
    rom_fill(&si, (const u8 *)"12/31/99", 0xFC);
    fill_cpu_dos(&si);
    render(&si);
    CHECK(has("BIOS:    no SMBIOS (an entry point failed its checksum); "
              "ROM BIOS date 12/31/99, model byte FCh\n"),
          "bad checksum line");
    /* An unaligned anchor is not looked at. */
    memset(&si, 0, sizeof(si));
    memset(area, 0, sizeof(area));
    put_ep2(0x108, 2, 8, 0xF6000UL, (u16)tlen, 10);
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == 0,
          "unaligned anchor ignored");
    /* Legacy DMI 2.0 alone. */
    memset(&si, 0, sizeof(si));
    memset(area, 0, sizeof(area));
    put_dmi(0x400, 0xE0000UL, (u16)tlen, 9, 0x20);
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == SMB_EP_DMI &&
          si.smb_major == 2 && si.smb_minor == 0 &&
          si.table_lo == 0xE0000UL, "legacy _DMI_");
    /* An empty table. */
    memset(&si, 0, sizeof(si));
    memset(area, 0, sizeof(area));
    put_ep2(0x100, 2, 3, 0xF6000UL, 0, 0);
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == SMB_EP_32 &&
          si.smb_state == SMB_EMPTY, "empty table");
    /* A table wrapping past 4 GB. */
    memset(&si, 0, sizeof(si));
    put_ep2(0x100, 2, 3, 0xFFFFF000UL, 0x2000, 1);
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == SMB_EP_32 &&
          si.smb_state == SMB_ABOVE_4G, "wrapping table refused");
    /* No SMBIOS at all, and the ROM's date. */
    memset(&si, 0, sizeof(si));
    memset(area, 0, sizeof(area));
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == 0 &&
          !si.other_ep_bad, "no entry at all");
    rom_fill(&si, (const u8 *)"06/10\x01" "92", 0xFC);
    render(&si);
    CHECK(has("BIOS:    no SMBIOS; ROM BIOS date 06/10?92, model byte "
              "FCh\n"), "no SMBIOS: ROM date, control byte replaced");
    render_short(&si);
    CHECK(has("System: no SMBIOS; ROM BIOS date 06/10?92, model byte FCh\n"),
          "no SMBIOS short line");
}

static void test_broken_tables(void)
{
    SYSINFO si;
    u32 full;
    u8 save;

    build_table(1);
    full = tlen;

    /* Truncated mid-way through the type 4 structure. */
    memset(&si, 0, sizeof(si));
    si.smb_state = SMB_OK;
    smb_parse_table(tab, 0x140, 10, &si);
    CHECK(si.broken && si.nstructs == 4 && si.seen_t0 && si.seen_t2,
          "truncated: earlier structures kept, damage flagged");
    CHECK(!si.seen_t4, "truncated: the cut structure not read");
    render(&si);
    CHECK(has("SMBIOS table malformed at offset"), "truncated: said so");
    check_no_secrets("truncated");

    /* Every truncation length: no read past the end (the bound is the
     * walk's, so this checks it terminates and never reports a structure
     * it could not have finished). */
    {
        u32 l;
        int ok = 1;

        int k, done, at_end;

        /* Each window ends at the guard page, so a read past l faults;
         * the structures counted are exactly those that end within l,
         * and damage is reported unless l falls on a boundary. */
        for (l = 0; l <= full; l++) {
            done = 0;
            at_end = (l == 0);
            for (k = 0; k < nends; k++) {
                if (ends[k] <= l)
                    done++;
                if (ends[k] == l)
                    at_end = 1;
            }
            memset(&si, 0, sizeof(si));
            smb_parse_table(at_guard(tab, l), l, 0, &si);
            if (si.nstructs != done || si.broken != !at_end ||
                si.seen_t1 != (l >= ends[1]))
                ok = 0;
        }
        CHECK(nends == 10, "ten structure ends recorded");
        CHECK(ok, "every truncation length: exact structures, no overread");
    }

    /* A structure length below 4. */
    memset(&si, 0, sizeof(si));
    save = tab[1];
    tab[1] = 2;
    smb_parse_table(tab, full, 0, &si);
    CHECK(si.broken && si.broken_at == 0 && si.nstructs == 0,
          "length 2: broken at once");
    tab[1] = save;

    /* A structure length past the end of the table. */
    memset(&si, 0, sizeof(si));
    save = tab[1];
    tab[1] = 0xFF;
    smb_parse_table(tab, 0x40, 0, &si);
    CHECK(si.broken && si.nstructs == 0, "length past the end: broken");
    tab[1] = save;

    /* Strings with no double NUL before the end. */
    memset(&si, 0, sizeof(si));
    {
        static u8 t[64];
        memset(t, 'A', sizeof(t));
        t[0] = 0; t[1] = 0x12; t[4] = 1;
        smb_parse_table(t, sizeof(t), 0, &si);
        CHECK(si.broken && !si.seen_t0, "no string terminator: broken");
    }

    /* Garbled: random-looking bytes. */
    {
        static u8 t[4096];
        u32 i, x = 12345;
        int ok = 1;
        int round;

        for (round = 0; round < 200; round++) {
            for (i = 0; i < sizeof(t); i++) {
                x = x * 1103515245UL + 12345UL;
                t[i] = (u8)(x >> 16);
            }
            memset(&si, 0, sizeof(si));
            smb_parse_table(t, sizeof(t), (u16)(round & 1 ? 0 : 50), &si);
            if (strlen(si.sys_mfr) >= SI_STR || strlen(si.bios_vendor) >=
                SI_STR)
                ok = 0;
            out[0] = '\0';
            si.smb_state = SMB_OK;
            report_sysinfo(&si, cap);
            report_sysinfo_short(&si, cap);
        }
        CHECK(ok, "garbled tables: bounded strings, no hang");
    }

    /* A table that really reaches the cap: 256 structures of 256 bytes
     * (Type 126, inactive) fill 64 KB exactly, ending at the guard page,
     * and the entry point claims twice that. The walk must stop at the
     * cap without touching the guard. */
    {
        static u8 big[SMB_TABLE_MAX];
        u32 o;

        memset(big, 0, sizeof(big));
        for (o = 0; o < SMB_TABLE_MAX; o += 256) {
            big[o] = 126;
            big[o + 1] = 254;           /* + "\0\0" = 256 */
        }
        memset(&si, 0, sizeof(si));
        smb_parse_table(at_guard(big, SMB_TABLE_MAX), 0x20000UL, 0, &si);
        CHECK(si.used_len == SMB_TABLE_MAX && si.nstructs == 256 &&
              !si.broken, "cap: 256 structures to the 64 KB boundary");
        /* The last structure cut by the cap: damage, not an overread. */
        big[SMB_TABLE_MAX - 256 + 1] = 255;
        memset(&si, 0, sizeof(si));
        smb_parse_table(at_guard(big, SMB_TABLE_MAX), 0x20000UL, 0, &si);
        CHECK(si.nstructs == 255 && si.broken &&
              si.broken_at == SMB_TABLE_MAX - 256,
              "cap: a structure crossing it is damage");
    }

    /* Entry-point anchors in the last paragraphs of a window that ends at
     * the guard page. */
    {
        static u8 w[GUARD_SPAN];

        memset(w, 0, sizeof(w));
        memcpy(w + GUARD_SPAN - 16, "_SM_", 4);
        memcpy(w + GUARD_SPAN - 32, "_SM3_", 5);
        w[GUARD_SPAN - 32 + 6] = 0xFF;  /* a length past the window */
        memset(&si, 0, sizeof(si));
        CHECK(smb_scan(at_guard(w, GUARD_SPAN), GUARD_SPAN, 0xF0000UL, &si)
              == 0 && si.other_ep_bad, "anchors at the window's end");
        memset(w + GUARD_SPAN - 32, 0, 32);
        memcpy(w + GUARD_SPAN - 16, "_DMI_", 5);
        memset(&si, 0, sizeof(si));
        (void)smb_scan(at_guard(w, GUARD_SPAN), GUARD_SPAN, 0xF0000UL, &si);
        CHECK(1, "_DMI_ in the last paragraph: no overread");
    }

    /* Zero padding after the last structure (no type 127) is not damage. */
    build_table(0);
    memset(&si, 0, sizeof(si));
    smb_parse_table(tab, tlen + 64, 0, &si);
    CHECK(!si.broken && si.nstructs == 9, "zero padding: not broken");
}

static void test_strings(void)
{
    SYSINFO si;
    u8 f[0x20];
    static const char *one[] = { "Only One", 0 };
    static const char *lng[] = {
        "0123456789012345678901234567890123456789012345678901234567890123"
        "456789-TOO-LONG", 0 };

    /* String numbers past the set, and 0. */
    tlen = 0;
    memset(tab, 0, sizeof(tab));
    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x05, 2); put8(f, 0x06, 0); put8(f, 0x1A, 9);
    add(1, f, 0x1B, one);
    /* A short (2.0) Type 1: family not in its formatted area. */
    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1);
    add(2, f, 0x05, lng);
    /* No strings at all. */
    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x05, 2); put8(f, 0x08, 3);
    add(0, f, 0x12, 0);
    memset(f, 0, sizeof(f));
    add(127, f, 4, 0);

    memset(&si, 0, sizeof(si));
    si.smb_state = SMB_OK;
    si.smb_kind = SMB_EP_32;
    si.smb_major = 2;
    si.smb_minor = 0;
    smb_parse_table(tab, tlen, 0, &si);
    CHECK(!si.broken && si.nstructs == 4, "string vectors: intact");
    CHECK(strcmp(si.sys_mfr, "Only One") == 0, "string 1 found");
    CHECK(si.sys_product[0] == '\0', "string 2 past the set: empty");
    CHECK(si.sys_version[0] == '\0', "string 0: empty");
    CHECK(si.sys_family[0] == '\0', "string 9: empty");
    CHECK(strlen(si.board_mfr) == SI_STR - 1 &&
          strncmp(si.board_mfr, "0123456789", 10) == 0,
          "long string cut at 64");
    CHECK(si.board_product[0] == '\0', "field past a short structure");
    CHECK(si.seen_t0 && si.bios_vendor[0] == '\0' &&
          si.bios_date[0] == '\0', "no strings: all empty");
    render(&si);
    CHECK(has("System:  Only One\n"), "System line, missing product");
    CHECK(has("BIOS:    (not given); SMBIOS 2.0"), "BIOS (not given)");
    CHECK(!has("TOO-LONG"), "the long string's tail not printed");
    render_short(&si);
    CHECK(has("System: Only One\n"), "short: no BIOS words");

    /* No Type 1 or 0 at all. */
    tlen = 0;
    memset(f, 0, sizeof(f));
    add(127, f, 4, 0);
    memset(&si, 0, sizeof(si));
    si.smb_state = SMB_OK;
    smb_parse_table(tab, tlen, 0, &si);
    render(&si);
    CHECK(has("System:  (no SMBIOS system structure)"), "no Type 1");
    CHECK(has("BIOS:    (no SMBIOS BIOS structure)"), "no Type 0");
    CHECK(has("CPU:     unknown (no CPUID)\n"), "no CPUID");
    CHECK(has("DOS:     version not read\n"), "no DOS");

    /* An unpopulated socket is passed over for the next. */
    tlen = 0;
    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x10, 2); put8(f, 0x18, 0x00);
    {
        static const char *e[] = { "EMPTY SOCKET", "nothing", 0 };
        add(4, f, 0x1A, e);
    }
    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x10, 2); put8(f, 0x18, 0x41);
    {
        static const char *p[] = { "CPU1", "Old CPU 486", 0 };
        add(4, f, 0x1A, p);
    }
    memset(&si, 0, sizeof(si));
    si.smb_state = SMB_OK;
    smb_parse_table(tab, tlen, 0, &si);
    CHECK(strcmp(si.cpu_socket, "CPU1") == 0, "unpopulated socket skipped");
    /* No brand string: Type 4's version stands in. */
    {
        u32 leaf0[4] = { 1, 0x756E6547UL, 0x6C65746EUL, 0x49656E69UL };
        si.has_cpuid = 1;
        cpu_fill_vendor(&si, leaf0);
        cpu_fill_signature(&si, 0x00000543UL);
    }
    render(&si);
    CHECK(has("CPU:     Old CPU 486 (GenuineIntel family 05h model 04h "
              "stepping 3), socket CPU1\n"), "Type 4 version for the brand");
}

static void test_cpu(void)
{
    SYSINFO si;

    memset(&si, 0, sizeof(si));
    cpu_fill_signature(&si, 0x00A20F12UL);  /* Zen 3: family 19h */
    CHECK(si.cpu_family == 0x19 && si.cpu_model == 0x21 &&
          si.cpu_stepping == 2, "family 0Fh + extended family");
    cpu_fill_signature(&si, 0x00000F29UL);  /* Pentium 4 */
    CHECK(si.cpu_family == 0x0F && si.cpu_model == 2 &&
          si.cpu_stepping == 9, "family 0Fh, no extension");
    cpu_fill_signature(&si, 0x00050543UL);  /* family 5: no ext model */
    CHECK(si.cpu_family == 5 && si.cpu_model == 4,
          "family 5 ignores the extended model");
    {
        u32 b[12];
        memset(b, 0, sizeof(b));
        cpu_fill_brand(&si, b);
        CHECK(si.cpu_brand[0] == '\0', "zero brand: empty");
        memset(b, 0x41, sizeof(b));         /* no terminator */
        cpu_fill_brand(&si, b);
        CHECK(strlen(si.cpu_brand) == 48, "unterminated brand: 48");
    }
    si.has_cpuid = 1;
    si.cpu_brand[0] = '\0';
    render_short(&si);
    CHECK(has("CPU: CPUID 05h/04h/3\n"), "short: no vendor, no brand");
}

/* Review round 1, P1: an allowed field naming the same string as an
 * identifier, or carrying an identifier's text, prints "(withheld)". */
static void test_aliasing(void)
{
    SYSINFO si;
    u8 f[0x40];
    static const char *s1[] = { "SER-T1-SECRET", "Model Y", "SKU-T1-SECRET",
                                0 };
    static const char *s2[] = { "Board SER-T3-SECRET rev", "SER-T2-SECRET",
                                0 };
    static const char *s3[] = { "SER-T3-SECRET", 0 };
    static const char *s4[] = { "PART-T4-SECRET", "SER-T4-SECRET", 0 };
    static const char *s17[] = { "DIMM0", "Test BIOS 9", 0 };
    static const char *s0[] = { "Test BIOS 9", "1.0", 0 };

    tlen = 0;
    nends = 0;
    memset(tab, 0, sizeof(tab));
    /* Type 0 whose vendor is a Type 17 serial (text, another structure) */
    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x05, 2);
    add(0, f, 0x18, s0);
    /* Type 1: manufacturer and serial both string 1; family is the SKU */
    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x05, 2); put8(f, 0x07, 1);
    put8(f, 0x19, 3); put8(f, 0x1A, 3);
    add(1, f, 0x1B, s1);
    /* Type 2: product contains Type 3's serial; asset tag is its own */
    memset(f, 0, sizeof(f));
    put8(f, 0x04, 2); put8(f, 0x05, 1); put8(f, 0x07, 2); put8(f, 0x08, 2);
    add(2, f, 0x0F, s2);
    memset(f, 0, sizeof(f));
    put8(f, 0x07, 1);
    add(3, f, 0x15, s3);
    /* Type 4: version is its part number's string, socket its serial */
    memset(f, 0, sizeof(f));
    put8(f, 0x04, 2); put8(f, 0x10, 1); put8(f, 0x18, 0x41);
    put8(f, 0x20, 2); put8(f, 0x22, 1);
    add(4, f, 0x2A, s4);
    memset(f, 0, sizeof(f));
    put16(f, 0x0C, 1024);
    put8(f, 0x10, 1); put8(f, 0x18, 2);
    add(17, f, 0x22, s17);
    memset(f, 0, sizeof(f));
    add(127, f, 4, 0);

    memset(&si, 0, sizeof(si));
    si.smb_state = SMB_OK;
    smb_parse_table(tab, tlen, 0, &si);
    CHECK(strcmp(si.sys_mfr, "(withheld)") == 0, "T1 maker = serial index");
    CHECK(strcmp(si.sys_product, "Model Y") == 0, "T1 product kept");
    CHECK(strcmp(si.sys_family, "(withheld)") == 0, "T1 family = SKU");
    CHECK(strcmp(si.board_product, "(withheld)") == 0,
          "T2 product contains T3 serial");
    CHECK(strcmp(si.board_mfr, "(withheld)") == 0, "T2 maker = its serial");
    CHECK(strcmp(si.cpu_version, "(withheld)") == 0, "T4 version = part");
    CHECK(strcmp(si.cpu_socket, "(withheld)") == 0, "T4 socket = serial");
    CHECK(strcmp(si.bios_vendor, "(withheld)") == 0, "T0 vendor = T17 serial");
    CHECK(strcmp(si.bios_version, "1.0") == 0, "T0 version kept");
    render(&si);
    check_no_secrets("aliased");
    CHECK(!has("Test BIOS 9"), "aliased: the DIMM serial not printed");
    render_short(&si);
    check_no_secrets("aliased short");
    CHECK(!has("Test BIOS 9"), "aliased short: the DIMM serial");
}

/* Review round 2: the UUID as text, short identifiers, and a walk that
 * cannot finish. */
static void build_t1(u8 serial_idx, const char **strs, const u8 *uuid)
{
    u8 f[0x40];

    memset(f, 0, sizeof(f));
    put8(f, 0x04, 1); put8(f, 0x05, 2); put8(f, 0x06, 3);
    put8(f, 0x07, serial_idx);
    if (uuid != 0)
        memcpy(f + 0x08, uuid, 16);
    add(1, f, 0x1B, strs);
}

static void test_aliasing2(void)
{
    SYSINFO si;
    u8 f[0x40];
    static const u8 uuid[16] = { 0x5a, 0x5a, 0x5a, 0x5a, 0x12, 0x34, 0x43,
                                 0x21, 0x87, 0x65, 0x01, 0x23, 0x45, 0x67,
                                 0x89, 0xab };
    /* SMBIOS 2.6 text order: 5A5A5A5A-3412-2143-8765-0123456789AB */
    static const char *u1[] = { "Maker", "{5a5a5a5a-3412-2143-8765-0123456789ab}",
                                "5A5A5A5A12344321876501234567 89AB", 0 };
    static const char *s1[] = { "Maker", "Model A12", "ModelA12x", "A12",
                                0 };
    static const char *z1[] = { "Maker 0", "01/02/2026", "1.0", "0", 0 };
    static const char *s3[] = { "SER-T3-SECRET", 0 };
    static const char *p1[] = { "Maker", "Product SER-T3-SECRET", "v", 0 };

    /* The UUID in both byte orders, with braces, dashes, a space, case. */
    tlen = 0;
    memset(tab, 0, sizeof(tab));
    build_t1(0, u1, uuid);
    memset(f, 0, sizeof(f));
    add(127, f, 4, 0);
    memset(&si, 0, sizeof(si));
    smb_parse_table(tab, tlen, 0, &si);
    CHECK(strcmp(si.sys_mfr, "Maker") == 0, "uuid: maker kept");
    CHECK(strcmp(si.sys_product, "(withheld)") == 0,
          "uuid: wire-order text withheld");
    CHECK(strcmp(si.sys_version, "(withheld)") == 0,
          "uuid: raw-order text withheld");

    /* A three-character serial: as a word withheld, inside a word not. */
    tlen = 0;
    memset(tab, 0, sizeof(tab));
    {
        u8 g[0x40];
        memset(g, 0, sizeof(g));
        put8(g, 0x04, 1); put8(g, 0x05, 2); put8(g, 0x06, 3);
        put8(g, 0x07, 4);
        add(1, g, 0x1B, s1);
    }
    memset(f, 0, sizeof(f));
    add(127, f, 4, 0);
    memset(&si, 0, sizeof(si));
    smb_parse_table(tab, tlen, 0, &si);
    CHECK(strcmp(si.sys_product, "(withheld)") == 0, "short serial as word");
    CHECK(strcmp(si.sys_version, "ModelA12x") == 0,
          "short serial inside a word: kept");
    render(&si);
    CHECK(!has("Model A12"), "short serial: not printed");

    /* A one-character placeholder serial "0" does not eat dates. */
    tlen = 0;
    memset(tab, 0, sizeof(tab));
    {
        u8 g[0x40];
        memset(g, 0, sizeof(g));
        put8(g, 0x04, 1); put8(g, 0x05, 2); put8(g, 0x06, 3);
        put8(g, 0x07, 4);
        add(1, g, 0x1B, z1);
    }
    memset(f, 0, sizeof(f));
    add(127, f, 4, 0);
    memset(&si, 0, sizeof(si));
    smb_parse_table(tab, tlen, 0, &si);
    CHECK(strcmp(si.sys_mfr, "(withheld)") == 0, "\"0\" as a word: withheld");
    CHECK(strcmp(si.sys_product, "01/02/2026") == 0 &&
          strcmp(si.sys_version, "1.0") == 0, "\"0\" inside words: kept");

    /* A cross-structure alias whose identifier sits in a structure the
     * walk cannot finish: everything SMBIOS is withheld. */
    tlen = 0;
    memset(tab, 0, sizeof(tab));
    build_t1(0, p1, 0);
    memset(f, 0, sizeof(f));
    put8(f, 0x07, 1);
    add(3, f, 0x15, s3);
    memset(&si, 0, sizeof(si));
    smb_parse_table(tab, tlen - 1, 0, &si);     /* Type 3 lacks its NUL */
    CHECK(si.broken && si.ids_unchecked, "unfinished walk: flagged");
    CHECK(strcmp(si.sys_product, "(withheld)") == 0 &&
          strcmp(si.sys_mfr, "(withheld)") == 0, "unfinished walk: withheld");
    si.smb_state = SMB_OK;
    render(&si);
    check_no_secrets("unfinished walk");
    CHECK(has("strings withheld: identifiers in the unread part"),
          "unfinished walk: said so");
    render_short(&si);
    check_no_secrets("unfinished walk short");

    /* A table longer than the cap: the walk ends at the cap, incomplete. */
    {
        static u8 big[SMB_TABLE_MAX];
        u32 o;

        memset(big, 0, sizeof(big));
        build_table(1);
        memcpy(big, tab, tlen - 6);             /* drop the End-of-Table */
        for (o = tlen - 6; o + 256 <= SMB_TABLE_MAX; o += 256) {
            big[o] = 126;
            big[o + 1] = 254;
        }
        memset(&si, 0, sizeof(si));
        si.table_len = 0x20000UL;
        smb_parse_table(at_guard(big, SMB_TABLE_MAX), 0x20000UL, 0, &si);
        CHECK(si.ids_unchecked && !si.broken &&
              strcmp(si.sys_mfr, "(withheld)") == 0, "capped walk: withheld");
        si.smb_state = SMB_OK;
        render(&si);
        CHECK(has("SMBIOS table longer than 64 KB"), "capped: said so");
        check_no_secrets("capped");
    }
}

/* --no-sysinfo (owner, 2026-10-10): one line, nothing else, even over a
 * SYSINFO that held data before. */
static void test_skip(void)
{
    SYSINFO si;

    memset(&si, 0, sizeof(si));
    fill_cpu_dos(&si);
    strcpy(si.sys_mfr, "Should Not Print");
    sysinfo_skip(&si);
    CHECK(si.skipped && si.has_cpuid == 0 && si.sys_mfr[0] == '\0' &&
          si.e820_n == 0 && si.dos_read == 0, "skip: everything cleared");
    render(&si);
    CHECK(strcmp(out, "System:  not read - skipped by --no-sysinfo (no "
                      "SMBIOS, CPUID, E820)\n") == 0, "skip: one line");
    render_short(&si);
    CHECK(strcmp(out, "System: not read (--no-sysinfo)\n") == 0,
          "skip: one short line");
}

/* Review round 1, P3: an unusable 3.0 entry does not hide a legacy one. */
static void test_ep_order(void)
{
    SYSINFO si;

    memset(area, 0, sizeof(area));
    put_ep3(0x2000, 3, 2, 0, 1, 0x100);
    put_dmi(0x3000, 0xE0000UL, 0x100, 3, 0x20);
    memset(&si, 0, sizeof(si));
    CHECK(smb_scan(area, sizeof(area), 0xF0000UL, &si) == SMB_EP_DMI &&
          si.smb_state == SMB_OK, "legacy DMI before an above-4-GB 3.0");
}

int main(void)
{
    test_aliasing();
    test_aliasing2();
    test_skip();
    test_ep_order();
    test_valid_2x();
    test_valid_30();
    test_bad_entries();
    test_broken_tables();
    test_strings();
    test_cpu();
    printf("test_sysinfo: %d checks, %d failed\n", checks, failures);
    return failures;
}
