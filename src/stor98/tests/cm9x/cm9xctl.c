/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, Yeo Kheng Meng
 * Same licence conditions as cm9x.c (BSD-3-Clause).
 *
 * cm9xctl - Win32 console helper for the 9x qualification runs (stor98
 * docs/gates/Q-9x.md). Windows 98 SE and Me. Builds with MSVC 6.0.
 *
 *   cm9xctl eject [D: [P]]    flush drive D: (level-0 lock/unlock through
 *                             VWIN32) if given ("-" for none), then ask
 *                             cm9x.sys for an orderly eject of tunnel port P
 *                             (0 or 1; EjectRequest[1] + 1) and wait for its
 *                             result
 *   cm9xctl dump              ask cm9x.sys to dump the table to debugcon
 *   cm9xctl unload            try to remove cm9x.sys's own device (usb4-host
 *                             devnode) with CFGMGR32 CM_Query_Remove_SubTree,
 *                             then CM_Remove_SubTree (interface 3 R4 test)
 *   cm9xctl flush D:          only the flush
 *   cm9xctl set q9|cm Name N  write a DWORD option (Software\stor98\Q9x|Cm9x)
 *   cm9xctl write F N SEED    write N KiB of a SEED pattern to file F, commit
 *   cm9xctl verify F N SEED   check it
 *   cm9xctl bench F N [K]     write N MiB to F in K KiB writes (default 64),
 *                             commit, report time; then read it back
 *   cm9xctl hold F [S]        open F for writing and keep it open (S seconds; veto test)
 *   cm9xctl install           make cm9x.sys the WDM driver of the usb4-host
 *                             devnode(s) (HKLM\Enum\PCI\VEN_1B36&DEV_00F4*),
 *                             as an INF would: Driver -> a System class key
 *                             with DevLoader=*ntkern, NTMPDriver=cm9x.sys
 *
 * Output goes to the console and, as "ctl ..." lines, to the QEMU debugcon
 * (I/O port 0xE9), which Windows 9x does not virtualise for Win32 programs.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static void con(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    const char *p;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    fputs(buf, stdout);
    fputs("\n", stdout);
    fflush(stdout);
    for (p = "ctl "; *p; p++) _outp(0xE9, *p);
    for (p = buf; *p; p++) _outp(0xE9, *p);
    _outp(0xE9, '\n');
}

/* usb4-emu debug port stage code with a TSC marker (0xFA-0xFD: bench phases). */
static void stage(DWORD code, DWORD arg)
{
    DWORD lo, hi;
    __asm {
        _emit 0x0F
        _emit 0x31
        mov lo, eax
        mov hi, edx
    }
    _outpd(0x5A4, arg);
    _outpd(0x5A0, code);
    _outpd(0x5A8, lo);
    _outpd(0x5AC, hi);
}

typedef struct {
    DWORD reg_EBX, reg_EDX, reg_ECX, reg_EAX, reg_EDI, reg_ESI, reg_Flags;
} DIOC_REGISTERS;
#define VWIN32_DIOC_DOS_IOCTL 1

static int dos_flush(char drive)
{
    HANDLE h = CreateFileA("\\\\.\\vwin32", 0, 0, NULL, 0, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    DIOC_REGISTERS r;
    DWORD cb = 0;
    BOOL ok;
    DWORD t0 = GetTickCount();

    if (h == INVALID_HANDLE_VALUE) {
        con("flush: cannot open VWIN32 (%lu)", GetLastError());
        return 1;
    }
    /* Int 21h 710Dh (Reset Drive) is not passed by VWIN32_DIOC_DOS_IOCTL
     * (carry set, AX=1, measured on 98 SE). The DOS IOCTL it does pass is
     * Int 21h 440Dh: a level-0 Lock Logical Volume (CX=084Ah) makes the
     * file system write back what it caches for the volume, and is refused
     * while files are open on it; Unlock Logical Volume (CX=086Ah) follows. */
    memset(&r, 0, sizeof(r));
    r.reg_EAX = 0x440D;
    r.reg_EBX = (DWORD)((drive | 0x20) - 'a' + 1);   /* BH = 0: lock level 0 */
    r.reg_ECX = 0x084A;
    r.reg_EDX = 0;
    r.reg_Flags = 1;
    ok = DeviceIoControl(h, VWIN32_DIOC_DOS_IOCTL, &r, sizeof(r), &r, sizeof(r), &cb, NULL);
    con("flush %c: level-0 Lock Logical Volume ok=%d carry=%lu ax=%04lX in %lu ms", drive, ok, r.reg_Flags & 1,
        r.reg_EAX & 0xFFFF, GetTickCount() - t0);
    if (ok && !(r.reg_Flags & 1)) {
        memset(&r, 0, sizeof(r));
        r.reg_EAX = 0x440D;
        r.reg_EBX = (DWORD)((drive | 0x20) - 'a' + 1);
        r.reg_ECX = 0x086A;
        r.reg_Flags = 1;
        DeviceIoControl(h, VWIN32_DIOC_DOS_IOCTL, &r, sizeof(r), &r, sizeof(r), &cb, NULL);
        con("flush %c: Unlock Logical Volume carry=%lu", drive, r.reg_Flags & 1);
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    return 1;
}

static HKEY open_key(const char *which)
{
    HKEY hk = NULL;
    const char *path = (_stricmp(which, "q9") == 0) ? "Software\\stor98\\Q9x" : "Software\\stor98\\Cm9x";
    RegCreateKeyA(HKEY_LOCAL_MACHINE, path, &hk);
    return hk;
}

static DWORD get_dword(HKEY hk, const char *name, DWORD def)
{
    DWORD v = def, type = 0, cb = sizeof(v);
    if (RegQueryValueExA(hk, name, NULL, &type, (BYTE *)&v, &cb) != ERROR_SUCCESS) {
        return def;
    }
    return v;
}

static void put_dword(HKEY hk, const char *name, DWORD v)
{
    RegSetValueExA(hk, name, 0, REG_DWORD, (BYTE *)&v, sizeof(v));
}

static int do_eject(int argc, char **argv)
{
    HKEY hk = open_key("cm");
    DWORD req, res = 0, t0;
    int port = argc > 3 ? atoi(argv[3]) : 0;
    const char *rq = port ? "EjectRequest1" : "EjectRequest";
    const char *rs = port ? "EjectResult1" : "EjectResult";

    if (argc > 2 && argv[2][0] && argv[2][0] != '-') {
        dos_flush(argv[2][0]);
    }
    put_dword(hk, rs, 0);
    req = get_dword(hk, rq, 0) + 1;
    put_dword(hk, rq, req);
    RegFlushKey(hk);
    con("eject request %lu written for port %d", req, port);
    t0 = GetTickCount();
    while (GetTickCount() - t0 < 60000) {
        res = get_dword(hk, rs, 0);
        if (res) {
            break;
        }
        Sleep(100);
    }
    con("eject result %04lX after %lu ms (C1EA clean; E1xx vetoed (cr); E2xx isolated not clean, D2 recovery; E3xx failed or no ack (FF), CM isolates; E5xx Remove_SubTree failing; E0 nothing to eject; 0 no answer)",
        res, GetTickCount() - t0);
    RegCloseKey(hk);
    return res == 0xC1EA ? 0 : 1;
}

static void fill(unsigned char *b, DWORD n, DWORD seed, DWORD off)
{
    DWORD i, x = seed * 2654435761UL + off;
    for (i = 0; i < n; i++) {
        x = x * 1103515245UL + 12345UL;
        b[i] = (unsigned char)(x >> 16);
    }
}

static int do_write(const char *f, DWORD kib, DWORD seed, int verify)
{
    HANDLE h;
    unsigned char *b = (unsigned char *)malloc(65536), *c = (unsigned char *)malloc(65536);
    DWORD done = 0, n, got, bad = 0;

    h = CreateFileA(f, verify ? GENERIC_READ : GENERIC_WRITE, 0, NULL, verify ? OPEN_EXISTING : CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        con("%s %s: open failed (%lu)", verify ? "verify" : "write", f, GetLastError());
        return 1;
    }
    while (done < kib) {
        n = kib - done > 64 ? 64 : kib - done;
        fill(b, n * 1024, seed, done);
        if (verify) {
            if (!ReadFile(h, c, n * 1024, &got, NULL) || got != n * 1024 || memcmp(b, c, n * 1024)) {
                bad++;
                break;
            }
        } else if (!WriteFile(h, b, n * 1024, &got, NULL) || got != n * 1024) {
            con("write %s: failed at %lu KiB (%lu)", f, done, GetLastError());
            CloseHandle(h);
            return 1;
        }
        done += n;
    }
    if (!verify) {
        FlushFileBuffers(h);
    }
    CloseHandle(h);
    con("%s %s: %lu KiB seed %lu %s", verify ? "verify" : "write", f, done, seed,
        verify ? (bad ? "MISMATCH" : "OK") : "written and committed");
    return bad ? 1 : 0;
}

static int do_bench(const char *f, DWORD mib, DWORD kib)
{
    HANDLE h;
    unsigned char *b;
    DWORD i, n = mib * 1024 / kib, got, t0, tw, tf, tr, maxw = 0, t;

    b = (unsigned char *)malloc(kib * 1024);
    fill(b, kib * 1024, 7, 0);
    h = CreateFileA(f, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        con("bench: open failed (%lu)", GetLastError());
        return 1;
    }
    t0 = GetTickCount();
    stage(0xFA, mib);
    for (i = 0; i < n; i++) {
        t = GetTickCount();
        if (!WriteFile(h, b, kib * 1024, &got, NULL) || got != kib * 1024) {
            con("bench: write failed at %lu (%lu)", i, GetLastError());
            break;
        }
        t = GetTickCount() - t;
        if (t > maxw) maxw = t;
    }
    tw = GetTickCount() - t0;
    stage(0xFB, tw);
    FlushFileBuffers(h);
    CloseHandle(h);
    tf = GetTickCount() - t0;
    stage(0xFC, tf);
    h = CreateFileA(f, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    t0 = GetTickCount();
    for (i = 0; i < n; i++) {
        if (!ReadFile(h, b, kib * 1024, &got, NULL) || got != kib * 1024) {
            break;
        }
    }
    tr = GetTickCount() - t0;
    stage(0xFD, tr);
    CloseHandle(h);
    con("bench %s: %lu MiB in %lu KiB writes: writes %lu ms, +commit %lu ms (%lu KiB/s), max write %lu ms; read %lu ms",
        f, mib, kib, tw, tf, tf ? (mib * 1024 * 1000) / tf : 0, maxw, tr);
    return 0;
}

static int do_install(void)
{
    HKEY pci, dev, inst, cls;
    char name[256], iname[256], path[512], drv[64];
    DWORD i, j, n, k, zero = 0;
    int done = 0;

    if (RegOpenKeyA(HKEY_LOCAL_MACHINE, "Enum\\PCI", &pci) != ERROR_SUCCESS) {
        con("install: no HKLM\\Enum\\PCI");
        return 1;
    }
    for (k = 77; k < 200; k++) {
        sprintf(drv, "System\\%04lu", k);
        sprintf(path, "System\\CurrentControlSet\\Services\\Class\\%s", drv);
        if (RegOpenKeyA(HKEY_LOCAL_MACHINE, path, &cls) != ERROR_SUCCESS) {
            break;
        }
        RegCloseKey(cls);
    }
    RegCreateKeyA(HKEY_LOCAL_MACHINE, path, &cls);
    RegSetValueExA(cls, "DevLoader", 0, REG_SZ, (BYTE *)"*ntkern", 8);
    RegSetValueExA(cls, "NTMPDriver", 0, REG_SZ, (BYTE *)"cm9x.sys", 9);
    RegSetValueExA(cls, "DriverDesc", 0, REG_SZ, (BYTE *)"stor98 Q-9x CM stand-in", 24);
    RegCloseKey(cls);
    for (i = 0; RegEnumKeyA(pci, i, name, sizeof(name)) == ERROR_SUCCESS; i++) {
        if (_strnicmp(name, "VEN_1B36&DEV_00F4", 17)) {
            continue;
        }
        if (RegOpenKeyA(pci, name, &dev) != ERROR_SUCCESS) {
            continue;
        }
        for (j = 0; RegEnumKeyA(dev, j, iname, sizeof(iname)) == ERROR_SUCCESS; j++) {
            if (RegOpenKeyA(dev, iname, &inst) != ERROR_SUCCESS) {
                continue;
            }
            n = (DWORD)strlen(drv) + 1;
            RegSetValueExA(inst, "Class", 0, REG_SZ, (BYTE *)"System", 7);
            RegSetValueExA(inst, "ClassGUID", 0, REG_SZ, (BYTE *)"{4d36e97d-e325-11ce-bfc1-08002be10318}", 39);
            RegSetValueExA(inst, "Driver", 0, REG_SZ, (BYTE *)drv, n);
            RegSetValueExA(inst, "Mfg", 0, REG_SZ, (BYTE *)"stor98 test", 12);
            RegSetValueExA(inst, "DeviceDesc", 0, REG_SZ, (BYTE *)"USB4 host (stor98 CM stand-in)", 31);
            RegSetValueExA(inst, "ConfigFlags", 0, REG_BINARY, (BYTE *)&zero, 4);
            RegCloseKey(inst);
            con("install: Enum\\PCI\\%s\\%s -> %s (DevLoader *ntkern, NTMPDriver cm9x.sys)", name, iname, drv);
            done++;
        }
        RegCloseKey(dev);
    }
    RegCloseKey(pci);
    if (!done) {
        con("install: no usb4-host devnode found");
    }
    return done ? 0 : 1;
}

typedef DWORD (WINAPI *CM_LOCATE)(DWORD *, char *, ULONG);
typedef DWORD (WINAPI *CM_SUBTREE)(DWORD, ULONG);

/* R4 test: try to remove the CM's own device through CFGMGR32. */
static int do_unload(void)
{
    HKEY pci, dev;
    char name[256], iname[256], id[600];
    DWORD i, dn = 0, cr;
    HMODULE m = LoadLibraryA("CFGMGR32.DLL");
    CM_LOCATE locate = m ? (CM_LOCATE)GetProcAddress(m, "CM_Locate_DevNodeA") : NULL;
    CM_SUBTREE query = m ? (CM_SUBTREE)GetProcAddress(m, "CM_Query_Remove_SubTree") : NULL;
    CM_SUBTREE removal = m ? (CM_SUBTREE)GetProcAddress(m, "CM_Remove_SubTree") : NULL;

    if (!locate || !query || !removal) {
        con("unload: CFGMGR32 functions missing (%p %p %p)", locate, query, removal);
        return 1;
    }
    id[0] = 0;
    if (RegOpenKeyA(HKEY_LOCAL_MACHINE, "Enum\\PCI", &pci) == ERROR_SUCCESS) {
        for (i = 0; !id[0] && RegEnumKeyA(pci, i, name, sizeof(name)) == ERROR_SUCCESS; i++) {
            if (_strnicmp(name, "VEN_1B36&DEV_00F4", 17) || RegOpenKeyA(pci, name, &dev) != ERROR_SUCCESS) {
                continue;
            }
            if (RegEnumKeyA(dev, 0, iname, sizeof(iname)) == ERROR_SUCCESS) {
                sprintf(id, "PCI\\%s\\%s", name, iname);
            }
            RegCloseKey(dev);
        }
        RegCloseKey(pci);
    }
    cr = locate(&dn, id, 0);
    con("unload: CM_Locate_DevNode(%s) cr=%lu dn=%08lX", id, cr, dn);
    if (cr) {
        return 1;
    }
    cr = query(dn, 0);
    con("unload: CM_Query_Remove_SubTree cr=%lu (nonzero: refused)", cr);
    cr = removal(dn, 0);
    con("unload: CM_Remove_SubTree cr=%lu", cr);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: cm9xctl eject [D:|- [port]] | dump | unload | flush D: | set q9|cm Name N | write F KiB SEED | verify F KiB SEED |"
               " bench F MiB [KiB] | hold F\n");
        return 2;
    }
    if (!_stricmp(argv[1], "install")) {
        return do_install();
    }
    if (!_stricmp(argv[1], "eject")) {
        return do_eject(argc, argv);
    }
    if (!_stricmp(argv[1], "unload")) {
        return do_unload();
    }
    if (!_stricmp(argv[1], "dump")) {
        HKEY hk = open_key("cm");
        put_dword(hk, "DumpRequest", get_dword(hk, "DumpRequest", 0) + 1);
        RegFlushKey(hk);
        RegCloseKey(hk);
        con("dump requested");
        return 0;
    }
    if (!_stricmp(argv[1], "flush") && argc > 2) {
        return dos_flush(argv[2][0]);
    }
    if (!_stricmp(argv[1], "set") && argc > 4) {
        HKEY hk = open_key(argv[2]);
        put_dword(hk, argv[3], strtoul(argv[4], NULL, 0));
        RegFlushKey(hk);
        con("set %s %s = %lu", argv[2], argv[3], strtoul(argv[4], NULL, 0));
        RegCloseKey(hk);
        return 0;
    }
    if ((!_stricmp(argv[1], "write") || !_stricmp(argv[1], "verify")) && argc > 4) {
        return do_write(argv[2], strtoul(argv[3], NULL, 0), strtoul(argv[4], NULL, 0), !_stricmp(argv[1], "verify"));
    }
    if (!_stricmp(argv[1], "bench") && argc > 3) {
        return do_bench(argv[2], strtoul(argv[3], NULL, 0), argc > 4 ? strtoul(argv[4], NULL, 0) : 64);
    }
    if (!_stricmp(argv[1], "hold") && argc > 2) {
        HANDLE h = CreateFileA(argv[2], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD got;
        if (h == INVALID_HANDLE_VALUE) {
            con("hold: open failed (%lu)", GetLastError());
            return 1;
        }
        WriteFile(h, "held open\r\n", 11, &got, NULL);
        if (argc > 3) {
            con("hold: %s open for %lu s", argv[2], strtoul(argv[3], NULL, 0));
            Sleep(strtoul(argv[3], NULL, 0) * 1000);
            CloseHandle(h);
            con("hold: %s closed", argv[2]);
            return 0;
        }
        con("hold: %s open; close this window to release it", argv[2]);
        for (;;) {
            Sleep(60000);
        }
    }
    printf("bad arguments\n");
    return 2;
}
