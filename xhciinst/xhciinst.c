/*
 * xhciinst.c - XHCIINST.EXE (32-bit) and XHCIIN64.EXE (x64): install,
 * update or uninstall every driver of this package (roadmap-usb4.md tasks
 * 40.5 to 40.7; design record 23).
 *
 * WHY IT EXISTS. Installing the package was Device Manager work, one device
 * at a time, with a different trap per system on an update (Windows 2000
 * keeps the old driver unless Have Disk is used, Vista and 7 keep the old
 * .sys under an identical INF, the root hub needs its own update, 32-bit
 * Vista and 7 need a restart). INSTALL.BAT at the package root runs this
 * tool, which does on Windows 2000 to 7 what the release notes tell the
 * user to do by hand and says what it did; on Windows 98 SE and ME, where
 * no documented interface installs a driver on a device, it puts the
 * package where the hardware wizard searches by itself (DevicePath) and
 * prints the clicks that remain. It never ships or calls devcon, never
 * drives SETUPX, and never changes a boot setting.
 *
 * THE ROUTES. NT: newdev.dll's UpdateDriverForPlugAndPlayDevices with
 * INSTALLFLAG_FORCE on each device's own first hardware id, after the
 * package's INFs are pre-staged with SetupCopyOEMInf; the controllers
 * first, then the root hub, hubs and composite entries, xhciuas.sys's
 * devices, the NHI and the tunnel-port filter (record 23 section 5). 98 SE
 * and ME: a local copy in %windir%\XHCI98 named in DevicePath (section 6).
 * The uninstall: devnodes through DIF_REMOVE, the filter's name out of
 * UpperFilters, the OEM INFs, the services and the files on NT; on 98 SE
 * and ME what the tool itself placed, and the steps printed (section 9).
 *
 * TARGETS AND BUILDS. ANSI C89, built with the in-repo MSVC 6.0 as
 * XHCIINST.EXE and with WDK 7.1's amd64 compiler as XHCIIN64.EXE
 * (build.cmd), as XHCICLN is. Every SetupAPI, newdev and configuration
 * manager routine is looked up at run time, so the import table names
 * KERNEL32 and ADVAPI32 alone (selftest.cmd holds it there) and the image
 * starts on every target; a routine missing that a step needs refuses that
 * step. The 32-bit build under WOW64 refuses and names XHCIIN64.EXE. The
 * pure half is inst_core.c, which the host suite test\test_inst.c links;
 * the port switchover's controller list is the driver's own
 * (src\xhci_psw.c, XhciPswGate).
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <setupapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/xhci_version.h"
#include "inst_core.h"
#include "../src/cm/cm_status.h"

/* The driver's own switchover gate (src\xhci_psw.c), declared here rather
 * than through xhci_psw.h, whose compatibility header is the driver's. */
unsigned long XhciPswGate(unsigned long vendorDevice);

#define XHCIINST_VERSION XHCI_VER_STR

#ifdef _WIN64
#define INST_MY_ARCH        INST_ARCH_AMD64
#define INST_MY_NAME        "XHCIIN64.EXE"
#define INST_OTHER_NAME     "XHCIINST.EXE"
#define INST_DIR_SUFFIX     "-x64"
#define INST_DEFAULT_DIR    "release-x64"
#define INST_PE_MINE        INST_PE_AMD64
#else
#define INST_MY_ARCH        INST_ARCH_X86
#define INST_MY_NAME        "XHCIINST.EXE"
#define INST_OTHER_NAME     "XHCIIN64.EXE"
#define INST_DIR_SUFFIX     "-x86"
#define INST_DEFAULT_DIR    "release-x86"
#define INST_PE_MINE        INST_PE_I386
#endif

/* ---- numbers not in MSVC 6.0's SDK headers (WDK 7.1's newdev.h,
 * setupapi.h and cfgmgr32.h; Microsoft's documentation) ------------------ */

#define INST_INSTALLFLAG_FORCE      0x00000001UL
#define INST_SUOI_FORCEDELETE       0x00000001UL
#define INST_CR_SUCCESS             0x00000000UL
#define INST_CR_NO_SUCH_DEVINST     0x0000000DUL
#define INST_PROB_NEED_RESTART      14UL
#define INST_CM_DRP_DEVICEDESC      0x00000001UL
#define INST_CM_DRP_HARDWAREID      0x00000002UL
#define INST_CM_DRP_COMPATIBLEIDS   0x00000003UL
#define INST_CM_DRP_DRIVER          0x0000000AUL
#define INST_INSTALL_WAIT_MS        600000UL   /* the barrier: a wizard answered */
#define INST_FIRST_WAIT_MS          60000UL    /* before the first change      */

#ifndef ERROR_IN_WOW64
#define ERROR_IN_WOW64 0xE0000235UL
#endif

#define INST_MAX_DEVS       1536
#define INST_MSZ_BYTES      2048
#define INST_PATH_CHARS     520

/* The INF slots: file, service, systems. */
static const char *const instInfFile[INST_INF_COUNT] = {
    "xhci98.inf", "xhciuas.inf", "xhcicm.inf", "xhcicmf.inf"
};
static const char *const instInfService[INST_INF_COUNT] = {
    "xhci98", "xhciuas", "xhcicm", "xhcicm"
};

/* ---- the routines, looked up at run time ------------------------------------- */

typedef HDEVINFO (WINAPI *PFN_GETCLASSDEVS)(const GUID *, PCSTR, HWND, DWORD);
typedef BOOL (WINAPI *PFN_ENUMINFO)(HDEVINFO, DWORD, PSP_DEVINFO_DATA);
typedef BOOL (WINAPI *PFN_GETINSTID)(HDEVINFO, PSP_DEVINFO_DATA, PSTR, DWORD,
                                     PDWORD);
typedef BOOL (WINAPI *PFN_GETREGPROP)(HDEVINFO, PSP_DEVINFO_DATA, DWORD,
                                      PDWORD, PBYTE, DWORD, PDWORD);
typedef BOOL (WINAPI *PFN_SETREGPROP)(HDEVINFO, PSP_DEVINFO_DATA, DWORD,
                                      const BYTE *, DWORD);
typedef BOOL (WINAPI *PFN_DESTROY)(HDEVINFO);
typedef BOOL (WINAPI *PFN_SETCIPARAMS)(HDEVINFO, PSP_DEVINFO_DATA,
                                       PSP_CLASSINSTALL_HEADER, DWORD);
typedef BOOL (WINAPI *PFN_CALLCI)(DI_FUNCTION, HDEVINFO, PSP_DEVINFO_DATA);
typedef BOOL (WINAPI *PFN_GETDIPARAMS)(HDEVINFO, PSP_DEVINFO_DATA,
                                       PSP_DEVINSTALL_PARAMS_A);
typedef BOOL (WINAPI *PFN_COPYOEM)(PCSTR, PCSTR, DWORD, DWORD, PSTR, DWORD,
                                   PDWORD, PSTR *);
typedef BOOL (WINAPI *PFN_UNINSTOEM)(PCSTR, DWORD, PVOID);
typedef BOOL (WINAPI *PFN_STORELOC)(PCSTR, PVOID, PCSTR, PSTR, DWORD,
                                    PDWORD);
typedef BOOL (WINAPI *PFN_UPDATEDRV)(HWND, LPCSTR, LPCSTR, DWORD, PBOOL);
typedef DWORD (WINAPI *PFN_CM_LOCATE)(DWORD *, char *, ULONG);
typedef DWORD (WINAPI *PFN_CM_STATUS)(ULONG *, ULONG *, DWORD, ULONG);
typedef DWORD (WINAPI *PFN_CM_REL)(DWORD *, DWORD, ULONG);
typedef DWORD (WINAPI *PFN_CM_ID)(DWORD, char *, ULONG, ULONG);
typedef DWORD (WINAPI *PFN_CM_PROP)(DWORD, ULONG, ULONG *, void *, ULONG *,
                                    ULONG);
typedef DWORD (WINAPI *PFN_CM_WAIT)(DWORD);
typedef BOOL (WINAPI *PFN_ISWOW64)(HANDLE, PBOOL);

static struct {
    HMODULE SetupApi;
    HMODULE CfgMgr;
    HMODULE NewDev;
    PFN_GETCLASSDEVS GetClassDevs;
    PFN_ENUMINFO EnumInfo;
    PFN_GETINSTID GetInstanceId;
    PFN_GETREGPROP GetRegProp;
    PFN_SETREGPROP SetRegProp;
    PFN_DESTROY Destroy;
    PFN_SETCIPARAMS SetCiParams;
    PFN_CALLCI CallCi;
    PFN_GETDIPARAMS GetDiParams;
    PFN_COPYOEM CopyOem;
    PFN_UNINSTOEM UninstallOem;
    PFN_STORELOC StoreLocation;
    PFN_UPDATEDRV UpdateDriver;
    PFN_CM_LOCATE Locate;
    PFN_CM_STATUS Status;
    PFN_CM_REL Child;
    PFN_CM_REL Sibling;
    PFN_CM_ID DeviceId;
    PFN_CM_PROP Property;
    PFN_CM_WAIT WaitInstall;
} api;

/* ---- state -------------------------------------------------------------------- */

typedef struct _DEVREC {
    char Id[INST_ID_CHARS];
    char Desc[128];
    char Service[64];
    char Driver[128];
    char Hw[INST_MSZ_BYTES];
    DWORD HwBytes;
    char Compat[INST_MSZ_BYTES];
    DWORD CompatBytes;
    char Upper[512];
    DWORD UpperBytes;
    ULONG Status;
    ULONG Problem;
    int Present;
    int VersionState;       /* INST_VS_*                     */
    INST_U32 Version[4];
    char VersionText[64];
    int Inf;                /* the INF that matches, -1 none */
    int Action;
    int Runs;
    int Done;               /* the call was made for it      */
    int Refused;            /* its group's verdict refused it */
    int RegOnly;            /* 98/ME: an Enum entry with no devnode */
    char InfPath[64];       /* the driver key's InfPath       */
    int InfPathState;       /* 1 read, 0 absent, -1 unreadable */
} DEVREC;

static int is_controller(const DEVREC *d);
static const char *first_hw(const DEVREC *d);

/* The service groups of record 23 section 5.4. */
#define INST_GROUPS 3
static const char *const groupService[INST_GROUPS] = {
    "xhci98", "xhciuas", "xhcicm"
};
static int groupVerdict[INST_GROUPS];

/* The devices a step changed, by instance id, for the results check. */
/* The devices this run reached, by instance id, kept across the barriers'
 * fresh scans: an entry is never reached twice in one run, except after
 * a busy answer. State 1: an install call was made for it (changedReboot
 * holds that call's restart answer; 2 once it succeeded, 3 when it failed,
 * 4 when it met Windows' own install of the device and is made again after
 * the next barrier, if still needed); 0: it was left (shared id, another
 * build stored). */
#define INST_MAX_CHANGED INST_MAX_DEVS
static char changedId[INST_MAX_CHANGED][INST_ID_CHARS];
static int changedInf[INST_MAX_CHANGED];
static int changedState[INST_MAX_CHANGED];
static int changedReboot[INST_MAX_CHANGED];
static int changedBusy[INST_MAX_CHANGED];   /* busy answers (0x20) so far */
#define INST_CS_BUSY 4  /* the call met Windows' own install: eligible again */
static int changedCount;
static int walkErrors;      /* 98/ME: entries the scan could not read */
static int copiedNow;       /* this run made a new local copy */
static int tunnelChanged;   /* 2000: a port's first filter install succeeded */
static int tunnelHint;      /* ... and a tunnel device then failed its check */
static int was_changed(const char *id);
static int was_installed(const char *id);
static int result_ok(const DEVREC *d, int inf);
static int idle_filtered_port(const DEVREC *d, int inf);
static int reached(const char *id);

static int testSigningConfirmed;
static int pkgSigned = -1;  /* xhci98.sys signed: 1, unsigned: 0, unread: -1 */
static char trapOem[INST_INF_COUNT][INST_PATH_CHARS];
static char srcDir[INST_PATH_CHARS];
/* Each INF relative to the flavour directory: the NT-only filter INF is in
 * nt\ (39-0.4), where Windows 98's search never meets it. */
static char infRel[INST_INF_COUNT][32];
static int restagePending[INST_INF_COUNT];
static int restageOk[INST_INF_COUNT];
static int restage_listed(const char *id);

static DEVREC *devs;
static int devCount;
static int devTruncated;

static INST_OPTS opts;
static INST_ENV envNow;
static OSVERSIONINFOA osv;
static FILE *logFile;
static char logPath[INST_PATH_CHARS];
static char root[INST_PATH_CHARS];
static char pkgDir[INST_PATH_CHARS];
static char *infText[INST_INF_COUNT];
static INST_IDLIST infIds[INST_INF_COUNT];
static int infTrap[INST_INF_COUNT];
static int trapUnknown[INST_INF_COUNT];  /* the store could not be inspected */
static INST_U32 pkgVersion[4];
static int haveCm;
static int tbInclude = 1;   /* the Thunderbolt question's answer (section 8.5) */
static int tbNhiPresent;
static int tbStaged;        /* NT: xhcicm.inf was staged by this run       */
static int tbTagAll;        /* package_files lists every file, whatever the
                               answer: the package's identity             */
static void tb_question(void);
static void tb_later_note(const char *dir);

static int flagFailed;
static int flagRestart;
static int flagSteps;
static int flagChanged;
static int outQuiet;        /* a reading repeated: its text to the log only */
static int trayDeferred;    /* the USB tree tool goes at the restart        */

/* The registry answers: per value, 1 when it is to be written, and what. */
static int answerSet[16];
static INST_U32 answerValue[16];

/* ======================================================================== */
/* Output and the log                                                        */
/* ======================================================================== */

static void out(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    if (!outQuiet) {
        fputs(buf, stdout);
        fflush(stdout);
    }
    if (logFile != NULL) {
        fputs(buf, logFile);
        fflush(logFile);
    }
}

/* To the log alone: the calls and their results. */
static void logonly(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;

    if (logFile == NULL) {
        return;
    }
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    fputs(buf, logFile);
    fflush(logFile);
}

static void log_open(void)
{
    SYSTEMTIME t;
    char drive[INST_PATH_CHARS];

    if (opts.Log[0] != '\0') {
        inst_copy(logPath, opts.Log, sizeof(logPath));
    } else {
        if (GetWindowsDirectoryA(drive, sizeof(drive)) < 3) {
            inst_copy(drive, "C:\\", sizeof(drive));
        }
        drive[3] = '\0';
        inst_copy(logPath, drive, sizeof(logPath));
        strcat(logPath, "XHCIINST.LOG");
    }
    logFile = fopen(logPath, "a");
    GetLocalTime(&t);
    if (logFile == NULL) {
        printf("The log %s could not be opened; going on without it.\n\n",
               logPath);
        logPath[0] = '\0';
        return;
    }
    logonly("\n==== xhciinst %s, %04u-%02u-%02u %02u:%02u:%02u ====\n",
            XHCIINST_VERSION, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
            t.wSecond);
}

/* One line of input, or 0 at end of input. */
static int read_line(char *buf, size_t cap)
{
    size_t n;

    if (fgets(buf, (int)cap, stdin) == NULL) {
        buf[0] = '\0';
        logonly("(end of input)\n");
        return 0;
    }
    n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    logonly("> %s\n", buf);
    return 1;
}

/* ======================================================================== */
/* The routines                                                              */
/* ======================================================================== */

static FARPROC proc(HMODULE module, const char *dll, const char *name)
{
    FARPROC p = (module != NULL) ? GetProcAddress(module, name) : NULL;

    logonly("routine %s!%s: %s\n", dll, name, p != NULL ? "resolved" : "missing");
    return p;
}

static void load_routines(void)
{
    api.SetupApi = LoadLibraryA("setupapi.dll");
    api.CfgMgr = LoadLibraryA("cfgmgr32.dll");
    api.NewDev = envNow.Nt ? LoadLibraryA("newdev.dll") : NULL;
    api.GetClassDevs = (PFN_GETCLASSDEVS)proc(api.SetupApi, "setupapi", "SetupDiGetClassDevsA");
    api.EnumInfo = (PFN_ENUMINFO)proc(api.SetupApi, "setupapi", "SetupDiEnumDeviceInfo");
    api.GetInstanceId = (PFN_GETINSTID)proc(api.SetupApi, "setupapi", "SetupDiGetDeviceInstanceIdA");
    api.GetRegProp = (PFN_GETREGPROP)proc(api.SetupApi, "setupapi", "SetupDiGetDeviceRegistryPropertyA");
    api.SetRegProp = (PFN_SETREGPROP)proc(api.SetupApi, "setupapi", "SetupDiSetDeviceRegistryPropertyA");
    api.Destroy = (PFN_DESTROY)proc(api.SetupApi, "setupapi", "SetupDiDestroyDeviceInfoList");
    api.SetCiParams = (PFN_SETCIPARAMS)proc(api.SetupApi, "setupapi", "SetupDiSetClassInstallParamsA");
    api.CallCi = (PFN_CALLCI)proc(api.SetupApi, "setupapi", "SetupDiCallClassInstaller");
    api.GetDiParams = (PFN_GETDIPARAMS)proc(api.SetupApi, "setupapi", "SetupDiGetDeviceInstallParamsA");
    api.CopyOem = (PFN_COPYOEM)proc(api.SetupApi, "setupapi", "SetupCopyOEMInfA");
    api.UninstallOem = (PFN_UNINSTOEM)proc(api.SetupApi, "setupapi", "SetupUninstallOEMInfA");
    api.StoreLocation = (PFN_STORELOC)proc(api.SetupApi, "setupapi", "SetupGetInfDriverStoreLocationA");
    api.UpdateDriver = (PFN_UPDATEDRV)proc(api.NewDev, "newdev", "UpdateDriverForPlugAndPlayDevicesA");
    api.Locate = (PFN_CM_LOCATE)proc(api.CfgMgr, "cfgmgr32", "CM_Locate_DevNodeA");
    api.Status = (PFN_CM_STATUS)proc(api.CfgMgr, "cfgmgr32", "CM_Get_DevNode_Status");
    api.Child = (PFN_CM_REL)proc(api.CfgMgr, "cfgmgr32", "CM_Get_Child");
    api.Sibling = (PFN_CM_REL)proc(api.CfgMgr, "cfgmgr32", "CM_Get_Sibling");
    api.DeviceId = (PFN_CM_ID)proc(api.CfgMgr, "cfgmgr32", "CM_Get_Device_IDA");
    api.Property = (PFN_CM_PROP)proc(api.CfgMgr, "cfgmgr32", "CM_Get_DevNode_Registry_PropertyA");
    api.WaitInstall = (PFN_CM_WAIT)proc(api.CfgMgr, "cfgmgr32", "CMP_WaitNoPendingInstallEvents");
    if (api.WaitInstall == NULL) {
        /* Windows 2000 exports it from setupapi.dll. */
        api.WaitInstall = (PFN_CM_WAIT)proc(api.SetupApi, "setupapi", "CMP_WaitNoPendingInstallEvents");
    }
}

static void print_routines(void)
{
    out("Routines this system offers:\n");
    out("  UpdateDriverForPlugAndPlayDevicesA  %s\n", api.UpdateDriver ? "yes" : "no");
    out("  SetupCopyOEMInfA                    %s\n", api.CopyOem ? "yes" : "no");
    out("  SetupUninstallOEMInfA               %s\n", api.UninstallOem ? "yes" : "no");
    out("  SetupGetInfDriverStoreLocationA     %s\n", api.StoreLocation ? "yes" : "no");
    out("  SetupDiCallClassInstaller           %s\n", api.CallCi ? "yes" : "no");
    out("  CMP_WaitNoPendingInstallEvents      %s\n", api.WaitInstall ? "yes" : "no");
    out("  CM_Get_DevNode_Registry_PropertyA   %s\n\n", api.Property ? "yes" : "no");
}

/* ======================================================================== */
/* Files and the registry                                                    */
/* ======================================================================== */

static char *read_file(const char *path, DWORD *size)
{
    HANDLE h;
    DWORD n;
    DWORD got;
    char *buf;

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    n = GetFileSize(h, NULL);
    if (n == 0xFFFFFFFFUL || n > 16UL * 1024UL * 1024UL) {
        CloseHandle(h);
        return NULL;
    }
    buf = (char *)malloc(n + 1);
    if (buf == NULL) {
        CloseHandle(h);
        return NULL;
    }
    if (!ReadFile(h, buf, n, &got, NULL) || got != n) {
        free(buf);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    buf[n] = '\0';
    if (size != NULL) {
        *size = n;
    }
    return buf;
}

static int file_exists(const char *path, DWORD *size)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(path, &fd);

    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    FindClose(h);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        return 0;
    }
    if (size != NULL) {
        *size = fd.nFileSizeLow;
    }
    return 1;
}

static void join(char *outPath, size_t cap, const char *a, const char *b)
{
    inst_copy(outPath, a, cap);
    if (strlen(outPath) > 0 && outPath[strlen(outPath) - 1] != '\\' &&
        strlen(outPath) + 1 < cap) {
        strcat(outPath, "\\");
    }
    if (strlen(outPath) + strlen(b) < cap) {
        strcat(outPath, b);
    }
}

/* 1 the two files hold the same bytes, 0 they differ, -1 one of them
 * cannot be read (which is never "different"). */
static int same_file3(const char *a, const char *b)
{
    DWORD na;
    DWORD nb;
    char *ba = read_file(a, &na);
    char *bb = read_file(b, &nb);
    int r;

    if (ba == NULL || bb == NULL) {
        r = -1;
    } else {
        r = (na == nb && memcmp(ba, bb, na) == 0) ? 1 : 0;
    }
    free(ba);
    free(bb);
    return r;
}

static int same_file(const char *a, const char *b)
{
    return same_file3(a, b) == 1;
}

static int reg_text(HKEY rootKey, const char *path, const char *name,
                    char *outText, DWORD cap, DWORD *type)
{
    HKEY k;
    DWORD t;
    DWORD n = cap - 1;
    LONG r;

    if (RegOpenKeyExA(rootKey, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return 0;
    }
    r = RegQueryValueExA(k, name, NULL, &t, (LPBYTE)outText, &n);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || (t != REG_SZ && t != REG_EXPAND_SZ)) {
        return 0;
    }
    outText[n < cap ? n : cap - 1] = '\0';
    if (type != NULL) {
        *type = t;
    }
    return 1;
}

static int reg_dword(HKEY rootKey, const char *path, const char *name,
                     DWORD *value)
{
    HKEY k;
    DWORD t;
    DWORD n = sizeof(DWORD);
    LONG r;

    if (RegOpenKeyExA(rootKey, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return 0;
    }
    r = RegQueryValueExA(k, name, NULL, &t, (LPBYTE)value, &n);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && t == REG_DWORD && n == sizeof(DWORD);
}

/* A string value: 1 read, 0 absent (the key opened, no such value), -1
 * unreadable (the key did not open, or the value is not a string). */
static int reg_text3(HKEY rootKey, const char *path, const char *name,
                     char *outText, DWORD cap)
{
    HKEY k;
    DWORD t;
    DWORD n = cap - 1;
    LONG r;

    if (RegOpenKeyExA(rootKey, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return -1;
    }
    r = RegQueryValueExA(k, name, NULL, &t, (LPBYTE)outText, &n);
    RegCloseKey(k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS || (t != REG_SZ && t != REG_EXPAND_SZ)) {
        return -1;
    }
    outText[n < cap ? n : cap - 1] = '\0';
    return 1;
}

/* version.dll, looked up at run time like everything else. */
typedef DWORD (WINAPI *PFN_GFVIS)(LPCSTR, LPDWORD);
typedef BOOL (WINAPI *PFN_GFVI)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL (WINAPI *PFN_VQV)(LPCVOID, LPCSTR, LPVOID *, PUINT);

/* A file's version resource: 1 read, 0 the file is absent, -1 unreadable. */
static int file_version(const char *path, INST_U32 v[4])
{
    static HMODULE ver;
    PFN_GFVIS gfvis;
    PFN_GFVI gfvi;
    PFN_VQV vqv;
    DWORD dummy;
    DWORD n;
    void *buf;
    VS_FIXEDFILEINFO *fi;
    UINT len;
    int ok = -1;

    if (GetFileAttributesA(path) == 0xFFFFFFFFUL) {
        /* Only "not found" proves the file absent; access denied or an I/O
         * error leaves its version unknown (record 23 section 5.4). */
        DWORD e = GetLastError();
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? 0 : -1;
    }
    if (ver == NULL) {
        ver = LoadLibraryA("version.dll");
    }
    if (ver == NULL) {
        return -1;
    }
    gfvis = (PFN_GFVIS)GetProcAddress(ver, "GetFileVersionInfoSizeA");
    gfvi = (PFN_GFVI)GetProcAddress(ver, "GetFileVersionInfoA");
    vqv = (PFN_VQV)GetProcAddress(ver, "VerQueryValueA");
    if (gfvis == NULL || gfvi == NULL || vqv == NULL) {
        return -1;
    }
    n = gfvis(path, &dummy);
    if (n == 0) {
        return -1;
    }
    buf = malloc(n);
    if (buf == NULL) {
        return -1;
    }
    if (gfvi(path, 0, n, buf) && vqv(buf, "\\", (LPVOID *)&fi, &len) &&
        len >= sizeof(VS_FIXEDFILEINFO)) {
        v[0] = (INST_U32)(fi->dwFileVersionMS >> 16);
        v[1] = (INST_U32)(fi->dwFileVersionMS & 0xFFFFUL);
        v[2] = (INST_U32)(fi->dwFileVersionLS >> 16);
        v[3] = (INST_U32)(fi->dwFileVersionLS & 0xFFFFUL);
        ok = 1;
    }
    free(buf);
    return ok;
}

/* The driver (software) key of a devnode, relative to HKLM. */
static void driver_key_path(const DEVREC *d, char *path, size_t cap)
{
    if (envNow.Nt) {
        join(path, cap, "SYSTEM\\CurrentControlSet\\Control\\Class", d->Driver);
    } else {
        join(path, cap, "System\\CurrentControlSet\\Services\\Class", d->Driver);
    }
}

/* ======================================================================== */
/* Reading the devices (record 23 section 5.3)                               */
/* ======================================================================== */

static void dev_driver_values(DEVREC *d)
{
    char path[INST_PATH_CHARS];
    char text[128];
    HKEY k;
    int r;

    d->VersionState = INST_VS_ABSENT;
    d->VersionText[0] = '\0';
    if (d->Driver[0] == '\0') {
        return;
    }
    driver_key_path(d, path, sizeof(path));
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        d->VersionState = INST_VS_UNREADABLE;
        d->InfPathState = -1;
        inst_copy(d->VersionText, "(driver key unreadable)",
                  sizeof(d->VersionText));
        if (!envNow.Nt) {
            /* 98 and ME name the driver in that key (NTMPDriver): its
             * owner is unknown, never "no driver" (section 6.3). */
            out("  unreadable: the driver key of %s\n", d->Id);
            walkErrors++;
        }
        return;
    }
    RegCloseKey(k);
    d->InfPathState = reg_text3(HKEY_LOCAL_MACHINE, path, "InfPath",
                                d->InfPath, sizeof(d->InfPath));
    r = reg_text3(HKEY_LOCAL_MACHINE, path, "DriverVersion", text,
                  sizeof(text));
    if (r > 0 && inst_ver_parse(text, d->Version)) {
        d->VersionState = INST_VS_HAVE;
        inst_copy(d->VersionText, text, sizeof(d->VersionText));
    } else if (r != 0) {
        d->VersionState = INST_VS_UNREADABLE;
        inst_copy(d->VersionText, "(version unreadable)",
                  sizeof(d->VersionText));
    } else if (reg_text(HKEY_LOCAL_MACHINE, path, "DriverDate", text,
                        sizeof(text), NULL)) {
        inst_copy(d->VersionText, "date ", sizeof(d->VersionText));
        strncat(d->VersionText, text, sizeof(d->VersionText) - 6);
    }
    if (!envNow.Nt && d->Service[0] == '\0') {
        /* 98 and ME bind a WDM driver by the driver key's NTMPDriver; an
         * unreadable one is unknown ownership, never "no driver", and the
         * scan refuses (record 23 section 6.3). */
        r = reg_text3(HKEY_LOCAL_MACHINE, path, "NTMPDriver", text,
                      sizeof(text));
        if (r > 0) {
            char *dot = strchr(text, '.');
            if (dot != NULL) {
                *dot = '\0';
            }
            inst_copy(d->Service, text, sizeof(d->Service));
        } else if (r < 0) {
            out("  unreadable: the driver of %s\n", d->Id);
            walkErrors++;
        }
    }
}

#define INST_CR_NO_SUCH_VALUE       0x00000025UL

/* A configuration-manager property; returns the CR code (0xFFFFFFFF when
 * the routine is missing). buf is empty unless it was read. */
static DWORD cm_prop(DWORD dn, ULONG prop, char *buf, DWORD cap, DWORD *bytes)
{
    ULONG type;
    ULONG n = cap - 2;
    DWORD cr = 0xFFFFFFFFUL;

    memset(buf, 0, cap);
    if (api.Property != NULL) {
        cr = api.Property(dn, prop, &type, buf, &n, 0);
    }
    if (cr != INST_CR_SUCCESS) {
        memset(buf, 0, cap);
        n = 0;
    }
    if (bytes != NULL) {
        *bytes = n;
    }
    return cr;
}

/* A value of HKLM\Enum\<id> on 98 and ME: 1 read (buf replaced), 0 absent
 * (no such value, or no such key - a devnode the registry does not hold),
 * -1 unreadable; buf is left as it was unless the value was read. */
static int reg9x_value(const char *id, const char *name, char *buf, DWORD cap)
{
    char path[INST_ID_CHARS + 16];
    char tmp[INST_MSZ_BYTES];
    HKEY k;
    LONG e;
    int r;

    inst_copy(path, "Enum\\", sizeof(path));
    if (strlen(path) + strlen(id) >= sizeof(path)) {
        return -1;
    }
    strcat(path, id);
    if (cap - 2 > sizeof(tmp)) {
        return -1;
    }
    e = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k);
    if (e != ERROR_SUCCESS) {
        return e == ERROR_FILE_NOT_FOUND ? 0 : -1;
    }
    RegCloseKey(k);
    r = reg_text3(HKEY_LOCAL_MACHINE, path, name, tmp, cap - 2);
    if (r > 0) {
        memset(buf, 0, cap);
        inst_copy(buf, tmp, cap - 2);
    }
    return r;
}

/* A property of a 98/ME devnode. The registry is the authority: its comma
 * list is the whole list (what the configuration manager answers was read
 * on 98 SE to stop at the first id). The configuration manager's answer is
 * kept only where the registry proves the value absent (no such value, or a
 * devnode with no Enum key of its own); a registry value that cannot be
 * read is an error whatever the configuration manager says. 0 on that
 * error. */
static int prop9x(DWORD dn, const char *id, ULONG prop, const char *name,
                  char *buf, DWORD cap)
{
    DWORD cr = cm_prop(dn, prop, buf, cap, NULL);
    int r = reg9x_value(id, name, buf, cap);

    (void)cr;
    if (r < 0) {
        memset(buf, 0, cap);
        return 0;
    }
    return 1;
}

/* 98 and ME: the device tree. A devnode whose id, status or properties
 * cannot be read, or a child or sibling link that answers anything but
 * "none", counts as an error, and the run refuses on an incomplete picture
 * (record 23 section 6.3). */
static void walk_cm(DWORD dn, int depth)
{
    DWORD child;
    DWORD sib;
    DWORD cr;
    DEVREC *d;
    ULONG status;
    ULONG problem;

    for (;;) {
        if (devCount >= INST_MAX_DEVS) {
            devTruncated = 1;
            return;
        }
        d = &devs[devCount];
        memset(d, 0, sizeof(*d));
        if (api.DeviceId(dn, d->Id, sizeof(d->Id), 0) != INST_CR_SUCCESS) {
            logonly("devnode 0x%08lX: its id cannot be read\n", (unsigned long)dn);
            walkErrors++;
        } else {
            if (!prop9x(dn, d->Id, INST_CM_DRP_HARDWAREID, "HardwareID", d->Hw,
                        sizeof(d->Hw)) ||
                !prop9x(dn, d->Id, INST_CM_DRP_COMPATIBLEIDS, "CompatibleIDs",
                        d->Compat, sizeof(d->Compat)) ||
                !prop9x(dn, d->Id, INST_CM_DRP_DRIVER, "Driver", d->Driver,
                        sizeof(d->Driver))) {
                out("  unreadable: a property of %s\n", d->Id);
                walkErrors++;
            }
            prop9x(dn, d->Id, INST_CM_DRP_DEVICEDESC, "DeviceDesc", d->Desc,
                   sizeof(d->Desc));
            d->HwBytes = (DWORD)inst_ids_split(d->Hw, sizeof(d->Hw));
            d->CompatBytes = (DWORD)inst_ids_split(d->Compat, sizeof(d->Compat));
            if (api.Status(&status, &problem, dn, 0) == INST_CR_SUCCESS) {
                d->Status = status;
                d->Problem = (status & INST_DN_HAS_PROBLEM) ? problem : 0;
            } else {
                out("  unreadable: the state of %s\n", d->Id);
                walkErrors++;
            }
            d->Present = 1;
            d->Inf = -1;
            dev_driver_values(d);
            logonly("devnode %s | %s | %s | %s | %s\n", d->Id, d->Hw, d->Compat, d->Service, d->Driver);
            devCount++;
        }
        cr = api.Child(&child, dn, 0);
        if (cr == INST_CR_SUCCESS) {
            if (depth >= 32) {
                out("  the device tree is deeper than this tool reads.\n");
                walkErrors++;
            } else {
                walk_cm(child, depth + 1);
            }
        } else if (cr != INST_CR_NO_SUCH_DEVINST) {
            out("  the device tree could not be walked (0x%08lX).\n",
                (unsigned long)cr);
            walkErrors++;
        }
        cr = api.Sibling(&sib, dn, 0);
        if (cr != INST_CR_SUCCESS) {
            if (cr != INST_CR_NO_SUCH_DEVINST) {
                out("  the device tree could not be walked (0x%08lX).\n",
                    (unsigned long)cr);
                walkErrors++;
            }
            return;
        }
        dn = sib;
    }
}

/* A property: 1 read, 0 absent (ERROR_INVALID_DATA, SetupAPI's answer for a
 * property the devnode does not have), -1 unreadable (record 23 section 5.3). */
static int sd_prop(HDEVINFO set, PSP_DEVINFO_DATA data, DWORD prop,
                   char *buf, DWORD cap, DWORD *bytes)
{
    DWORD type;
    DWORD n = 0;
    int r = 1;

    memset(buf, 0, cap);
    if (!api.GetRegProp(set, data, prop, &type, (PBYTE)buf, cap - 2, &n)) {
        r = (GetLastError() == ERROR_INVALID_DATA) ? 0 : -1;
        memset(buf, 0, cap);
        n = 0;
    }
    if (bytes != NULL) {
        *bytes = n;
    }
    return r;
}

/* A value of the devnode's own Enum key (record 23 section 5.3): 1 read,
 * with the type expected and terminated; 0 absent (ERROR_FILE_NOT_FOUND,
 * the one answer that proves it); -1 unreadable (the key did not open, the
 * type is another, the data is too long). SetupAPI's ERROR_INVALID_DATA
 * means "absent or invalid", so it is not used for these. */
static int enum_value(const char *id, const char *name, DWORD wantType,
                      char *buf, DWORD cap, DWORD *bytes)
{
    char path[INST_ID_CHARS + 64];
    HKEY k;
    DWORD type;
    DWORD n = cap - 2;
    LONG r;

    memset(buf, 0, cap);
    if (bytes != NULL) {
        *bytes = 0;
    }
    inst_copy(path, "SYSTEM\\CurrentControlSet\\Enum\\", sizeof(path));
    if (strlen(path) + strlen(id) >= sizeof(path)) {
        return -1;
    }
    strcat(path, id);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &k) !=
        ERROR_SUCCESS) {
        return -1;
    }
    r = RegQueryValueExA(k, name, NULL, &type, (LPBYTE)buf, &n);
    RegCloseKey(k);
    if (r == ERROR_FILE_NOT_FOUND) {
        memset(buf, 0, cap);
        return 0;
    }
    if (r != ERROR_SUCCESS || type != wantType) {
        memset(buf, 0, cap);
        return -1;
    }
    /* Terminated whatever was stored: two NULs follow the data. */
    buf[n < cap - 2 ? n : cap - 2] = '\0';
    buf[(n < cap - 2 ? n : cap - 2) + 1] = '\0';
    if (bytes != NULL) {
        *bytes = n;
    }
    return 1;
}

/* NT: every devnode, present or not (the uninstall needs the phantoms). A
 * set that cannot be walked to its end, an id that cannot be read, or a
 * safety-relevant property or presence answer that cannot be read refuses
 * the run: the plan is never made on a partial picture. */
static int scan_nt(void)
{
    HDEVINFO set;
    SP_DEVINFO_DATA data;
    DWORD i;
    DWORD err;
    ULONG status;
    ULONG problem;
    DWORD cr;
    DEVREC *d;
    int unreadable = 0;

    devCount = 0;
    devTruncated = 0;
    set = api.GetClassDevs(NULL, NULL, NULL, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) {
        out("The device list could not be read (error 0x%08lX).\n",
            (unsigned long)GetLastError());
        return 0;
    }
    for (i = 0;; i++) {
        data.cbSize = sizeof(data);
        if (!api.EnumInfo(set, i, &data)) {
            err = GetLastError();
            if (err != ERROR_NO_MORE_ITEMS) {
                out("The device list stopped at entry %lu (error 0x%08lX).\n",
                    (unsigned long)i, (unsigned long)err);
                api.Destroy(set);
                return 0;
            }
            break;
        }
        if (devCount >= INST_MAX_DEVS) {
            devTruncated = 1;
            break;
        }
        d = &devs[devCount];
        memset(d, 0, sizeof(*d));
        if (!api.GetInstanceId(set, &data, d->Id, sizeof(d->Id), NULL)) {
            out("A device's instance id could not be read (error 0x%08lX).\n",
                (unsigned long)GetLastError());
            api.Destroy(set);
            return 0;
        }
        if (enum_value(d->Id, "HardwareID", REG_MULTI_SZ, d->Hw, sizeof(d->Hw),
                       &d->HwBytes) < 0 ||
            enum_value(d->Id, "CompatibleIDs", REG_MULTI_SZ, d->Compat,
                       sizeof(d->Compat), &d->CompatBytes) < 0 ||
            enum_value(d->Id, "Service", REG_SZ, d->Service, sizeof(d->Service),
                       NULL) < 0 ||
            enum_value(d->Id, "Driver", REG_SZ, d->Driver, sizeof(d->Driver),
                       NULL) < 0 ||
            enum_value(d->Id, "UpperFilters", REG_MULTI_SZ, d->Upper,
                       sizeof(d->Upper), &d->UpperBytes) < 0) {
            out("  unreadable: a property of %s\n", d->Id);
            unreadable++;
        }
        sd_prop(set, &data, SPDRP_DEVICEDESC, d->Desc, sizeof(d->Desc), NULL);
        cr = api.Status(&status, &problem, data.DevInst, 0);
        if (cr == INST_CR_SUCCESS) {
            d->Present = 1;
            d->Status = status;
            d->Problem = (status & INST_DN_HAS_PROBLEM) ? problem : 0;
        } else if (cr != INST_CR_NO_SUCH_DEVINST) {
            out("  unreadable: whether %s is present (0x%08lX)\n", d->Id,
                (unsigned long)cr);
            unreadable++;
        }
        d->Inf = -1;
        dev_driver_values(d);
        devCount++;
    }
    api.Destroy(set);
    if (devTruncated) {
        out("More than %d devices: the list is cut short, and nothing is "
            "changed.\n", INST_MAX_DEVS);
        return 0;
    }
    if (unreadable) {
        out("%d device entries could not be read whole: nothing is changed "
            "(record 23 section 5.3).\n", unreadable);
        return 0;
    }
    return 1;
}
/* 98 and ME: an xHCI controller whose New Hardware wizard was cancelled has
 * no devnode until the next start, only its HKLM\Enum\PCI entry. List such
 * an entry as a controller with no driver, so the printed steps name it;
 * nothing is installed on 98 and ME either way (record 23 section 6). */
static void registry_controllers_9x(void)
{
    HKEY pci;
    HKEY dev;
    char devName[INST_ID_CHARS];
    char instName[INST_ID_CHARS];
    DWORD i;
    DWORD j;
    DWORD n;
    LONG r;
    int k;
    DEVREC *d;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum\\PCI", 0, KEY_READ, &pci);
    if (r != ERROR_SUCCESS) {
        if (r != ERROR_FILE_NOT_FOUND) {
            out("  unreadable: HKLM\\Enum\\PCI (%ld)\n", (long)r);
            walkErrors++;
        }
        return;
    }
    for (i = 0;; i++) {
        n = sizeof(devName);
        r = RegEnumKeyExA(pci, i, devName, &n, NULL, NULL, NULL, NULL);
        if (r != ERROR_SUCCESS) {
            if (r != ERROR_NO_MORE_ITEMS) {
                out("  unreadable: HKLM\\Enum\\PCI entry %lu (%ld)\n",
                    (unsigned long)i, (long)r);
                walkErrors++;
            }
            break;
        }
        if (RegOpenKeyExA(pci, devName, 0, KEY_READ, &dev) != ERROR_SUCCESS) {
            out("  unreadable: HKLM\\Enum\\PCI\\%s\n", devName);
            walkErrors++;
            continue;
        }
        for (j = 0;; j++) {
            int known = 0;
            n = sizeof(instName);
            r = RegEnumKeyExA(dev, j, instName, &n, NULL, NULL, NULL, NULL);
            if (r != ERROR_SUCCESS) {
                if (r != ERROR_NO_MORE_ITEMS) {
                    out("  unreadable: HKLM\\Enum\\PCI\\%s entry %lu (%ld)\n",
                        devName, (unsigned long)j, (long)r);
                    walkErrors++;
                }
                break;
            }
            if (devCount >= INST_MAX_DEVS) {
                devTruncated = 1;
                break;
            }
            d = &devs[devCount];
            memset(d, 0, sizeof(*d));
            _snprintf(d->Id, sizeof(d->Id) - 1, "PCI\\%s\\%s", devName, instName);
            for (k = 0; k < devCount; k++) {
                if (inst_eq(devs[k].Id, d->Id)) {
                    known = 1;
                    break;
                }
            }
            if (known) {
                continue;
            }
            if (reg9x_value(d->Id, "HardwareID", d->Hw, sizeof(d->Hw)) < 0 ||
                reg9x_value(d->Id, "CompatibleIDs", d->Compat, sizeof(d->Compat)) < 0 ||
                reg9x_value(d->Id, "Driver", d->Driver, sizeof(d->Driver)) < 0) {
                out("  unreadable: a value of HKLM\\Enum\\%s\n", d->Id);
                walkErrors++;
                continue;
            }
            reg9x_value(d->Id, "DeviceDesc", d->Desc, sizeof(d->Desc));
            d->HwBytes = (DWORD)inst_ids_split(d->Hw, sizeof(d->Hw));
            d->CompatBytes = (DWORD)inst_ids_split(d->Compat, sizeof(d->Compat));
            if (!is_controller(d) || d->Driver[0] != '\0') {
                continue;
            }
            d->Present = 1;
            d->Inf = -1;
            dev_driver_values(d);
            d->RegOnly = 1;
            logonly("devnode (registry only, no driver) %s\n", d->Id);
            devCount++;
        }
        RegCloseKey(dev);
        if (devTruncated) {
            break;
        }
    }
    RegCloseKey(pci);
}
/* 98 SE and ME (record 23 section 6.3): an Enum\PCI entry with no devnode
 * and no driver whose first hardware id a present controller with a driver
 * carries may be that controller's old slot (it moved between starts, as
 * on the 98 SE and ME legs) or a second, identical controller whose New
 * Hardware wizard was cancelled: the id cannot tell. It stays listed, with
 * what to do in either case, and stays a step; it only does not make the
 * confirmation speak of an install (orchestrator decision 2026-10-15,
 * owner to confirm). 1 when d is such an entry. */
static int old_slot_suspect(const DEVREC *d)
{
    int j;

    if (!d->RegOnly || !d->Present) {
        return 0;
    }
    for (j = 0; j < devCount; j++) {
        const DEVREC *o = &devs[j];
        if (o != d && o->Present && !o->RegOnly && o->Driver[0] != '\0' &&
            is_controller(o) && inst_eq(first_hw(o), first_hw(d))) {
            return 1;
        }
    }
    return 0;
}

static int scan_9x(void)
{
    DWORD rootDn;

    devCount = 0;
    devTruncated = 0;
    walkErrors = 0;
    if (api.Locate == NULL || api.Child == NULL || api.Sibling == NULL ||
        api.DeviceId == NULL || api.Status == NULL) {
        out("The configuration manager's routines are missing; the devices "
            "cannot be read.\n");
        return 0;
    }
    if (api.Locate(&rootDn, NULL, 0) != INST_CR_SUCCESS) {
        out("The device tree's root could not be located.\n");
        return 0;
    }
    walk_cm(rootDn, 0);
    if (!devTruncated) {
        registry_controllers_9x();
    }
    if (devTruncated) {
        out("More than %d devices: the list is cut short, and nothing is "
            "changed.\n", INST_MAX_DEVS);
        return 0;
    }
    if (walkErrors) {
        out("%d device entries could not be read whole: nothing is changed "
            "(record 23 section 6.3).\n", walkErrors);
        return 0;
    }
    return 1;
}

static int scan(void)
{
    return envNow.Nt ? scan_nt() : scan_9x();
}

/* The first hardware id of a devnode. */
static const char *first_hw(const DEVREC *d)
{
    return d->Hw[0] != '\0' ? d->Hw : d->Id;
}

static int is_controller(const DEVREC *d)
{
    return inst_msz_has(d->Compat, d->CompatBytes, "PCI\\CC_0C0330") ||
           inst_msz_has(d->Hw, d->HwBytes, "PCI\\CC_0C0330");
}

/* ======================================================================== */
/* The plan (record 23 section 5.4)                                          */
/* ======================================================================== */

/* The service group a devnode belongs to, or -1: its own service, or for a
 * filter host the filter's. */
static int group_of(const DEVREC *d)
{
    int g;

    for (g = 0; g < INST_GROUPS; g++) {
        if (inst_eq(d->Service, groupService[g])) {
            return g;
        }
    }
    if (inst_msz_has(d->Upper, d->UpperBytes, "xhcicm")) {
        return 2;
    }
    return -1;
}

static int group_of_inf(int inf)
{
    return inf == INST_INF_XHCI ? 0 : inf == INST_INF_UAS ? 1 : 2;
}

/* %windir%\System32\Drivers\<service>.sys, where every INF of the package
 * puts its driver (DestinationDirs 10,System32\Drivers) on every system. */
static void group_binary(int g, char *path, size_t cap)
{
    char win[INST_PATH_CHARS];
    char name[32];

    if (g < 0 || GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        path[0] = '\0';
        return;
    }
    inst_copy(name, groupService[g], sizeof(name));
    strcat(name, ".sys");
    join(path, cap, win, "System32\\Drivers");
    join(path, cap, path, name);
}

/* Record 23 section 5.4: one binary per service, so one verdict per group. */
static void group_verdicts(void)
{
    int g;
    int i;
    int newer;
    int unknown;
    int binary;
    int repair;
    int r;
    INST_U32 v[4];
    char path[INST_PATH_CHARS];

    for (g = 0; g < INST_GROUPS; g++) {
        repair = 0;
        newer = 0;
        unknown = 0;
        if (g == 2 && haveCm && !tbInclude) {
            /* A No to the Thunderbolt question: the installed xhcicm is
             * not part of the run, so no verdict on it (section 8.5). */
            groupVerdict[g] = INST_GROUP_OK;
            continue;
        }
        for (i = 0; i < devCount; i++) {
            const DEVREC *d = &devs[i];
            if (group_of(d) != g) {
                continue;
            }
            if (d->VersionState == INST_VS_UNREADABLE) {
                unknown = 1;
            } else if (d->VersionState == INST_VS_HAVE &&
                       inst_ver_cmp(d->Version, pkgVersion) > 0) {
                newer = 1;
            }
        }
        group_binary(g, path, sizeof(path));
        r = file_version(path, v);
        binary = r == 0 ? -2 : r < 0 ? 2 : inst_ver_cmp(v, pkgVersion);
        if (g == 0 && INST_MY_ARCH == INST_ARCH_X86 && envNow.Nt) {
            /* The controller's INF installs the helper too (section 5.4). */
            int hb;
            char hp[INST_PATH_CHARS];
            char win[INST_PATH_CHARS];
            if (GetWindowsDirectoryA(win, sizeof(win)) != 0) {
                join(hp, sizeof(hp), win, "System32\\Drivers\\xhcihlpr.sys");
                r = file_version(hp, v);
                hb = r == 0 ? -2 : r < 0 ? 2 : inst_ver_cmp(v, pkgVersion);
                if (hb == 2 || (hb == 1 && binary != 2)) {
                    binary = hb;
                } else if (hb < 0) {
                    repair = 1;   /* the helper missing or older */
                }
            }
        }
        groupVerdict[g] = inst_group_verdict(newer, unknown, binary, opts.Force);
        if (binary < 0) {
            repair = 1;   /* the group's driver file missing or older */
        }
        for (i = 0; i < devCount; i++) {
            DEVREC *d = &devs[i];
            if (d->Inf < 0 || group_of_inf(d->Inf) != g) {
                continue;
            }
            d->Refused = 0;
            if (groupVerdict[g] != INST_GROUP_OK && d->Action != INST_ACT_FOREIGN) {
                d->Refused = 1;
                d->Runs = 0;
            } else if (repair && envNow.Nt && d->Present &&
                       d->Action == INST_ACT_CURRENT &&
                       (g != 0 || is_controller(d))) {
                /* Record 23 section 5.4: a current driver key over a missing
                 * or older file is reinstalled, so the INF puts it back. */
                d->Runs = 1;
            }
        }
    }
}

static void plan(void)
{
    int i;
    int k;
    INST_DEV dv;

    envNow.AnyXhci = 0;
    envNow.PswController = 0;
    envNow.CmNhi = 0;
    for (i = 0; i < devCount; i++) {
        DEVREC *d = &devs[i];
        d->Inf = -1;
        d->Runs = 0;
        if (!d->Present) {
            continue;
        }
        if (is_controller(d)) {
            INST_U32 vd;
            envNow.AnyXhci = 1;
            if (inst_pci_vendev(first_hw(d), &vd) && XhciPswGate(vd)) {
                envNow.PswController = 1;
            }
        }
        for (k = 0; k < INST_INF_COUNT; k++) {
            if (infText[k] == NULL || (k == INST_INF_CMF && !envNow.Nt)) {
                continue;
            }
            if (inst_ids_match(d->Hw, d->HwBytes, d->Compat, d->CompatBytes,
                               &infIds[k]) >= 0) {
                d->Inf = k;
                break;
            }
        }
        if (d->Inf < 0) {
            continue;
        }
        if (d->Inf == INST_INF_CM) {
            envNow.CmNhi = 1;
        }
        memset(&dv, 0, sizeof(dv));
        dv.HasService = d->Service[0] != '\0';
        dv.OwnService = inst_eq(d->Service, instInfService[d->Inf]);
        dv.ServiceIsPci = inst_eq(d->Service, "pci");
        dv.FilterInf = (d->Inf == INST_INF_CMF);
        dv.FilterPresent = inst_msz_has(d->Upper, d->UpperBytes, "xhcicm");
        dv.Problem = d->Problem;
        dv.VersionState = d->VersionState;
        memcpy(dv.Version, d->Version, sizeof(dv.Version));
        if (!envNow.Nt && dv.OwnService && d->VersionState == INST_VS_ABSENT) {
            /* 98 and ME keep a DriverDate only: the group's binary speaks
             * for the device (record 23 section 6.3). */
            char p[INST_PATH_CHARS];
            group_binary(group_of(d), p, sizeof(p));
            if (file_version(p, dv.Version) > 0) {
                dv.VersionState = INST_VS_HAVE;
            }
        }
        d->Action = inst_action(&dv, pkgVersion);
        d->Runs = inst_action_runs(d->Action, opts.Force);
        /* /restage: a confirmed binary mismatch reinstalls the INF's devices,
         * whatever their DriverVersion says (record 23 section 5.7). */
        if (((opts.Restage && trapOem[d->Inf][0] != '\0' && restageOk[d->Inf]) ||
             restage_listed(d->Id)) &&
            (d->Action == INST_ACT_CURRENT || d->Action == INST_ACT_UPDATE)) {
            d->Runs = 1;
        }
        /* A device this run already reached is not reached again after a
         * barrier's fresh scan; newly exposed devices stay eligible. */
        {
            int c = reached(d->Id);
            d->Done = c >= 0 && changedState[c] != INST_CS_BUSY;
        }
    }
    group_verdicts();
}

/* USBNTMAP.SYS in System32\Drivers: the upper filter xhciuas.inf names on
 * 98 SE and ME, which only another install places (NUSB's mass-storage
 * component on 98 SE, ME's own usbstor.inf at its first ordinary stick). */
static int ntmap_present(void)
{
    char win[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 1;
    }
    join(path, sizeof(path), win, "System32\\Drivers");
    join(path, sizeof(path), path, "USBNTMAP.SYS");
    return file_exists(path, NULL);
}

/* 98 SE and ME: what a present UAS entry at Code 2 needs (INST_UASFIX_*). */
static int uas_fix(const DEVREC *d)
{
    if (envNow.Nt || !d->Present || d->Inf != INST_INF_UAS ||
        d->Problem != 2UL) {
        return INST_UASFIX_NONE;
    }
    return inst_uas_fix_9x(envNow.Nt, envNow.Minor, 1, d->Problem,
                           ntmap_present());
}

/* lead is four characters: the plan's indent, or a step's "  - ". */
static void print_uas_fix(int fix, const char *lead)
{
    if (fix == INST_UASFIX_ME_STICK) {
        out("%sCode 2: Windows ME has not yet copied its own USB storage "
            "files\n    (USBNTMAP.SYS). Plug in any ordinary USB stick once "
            "and let it install,\n    then unplug this disk and plug it "
            "back in. No restart is needed.\n", lead);
    } else if (fix == INST_UASFIX_NUSB) {
        out("%sCode 2: USBNTMAP.SYS, from NUSB's mass-storage component, is "
            "missing.\n    Install that component (README, \"Installation "
            "Steps\"); the disk starts\n    at the next boot.\n", lead);
    }
}

static void print_plan(void)
{
    int i;
    int g;
    int any = 0;

    out("The devices this package's INFs match:\n");
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (d->Inf < 0) {
            continue;
        }
        any = 1;
        out("  %s\n", d->Desc[0] ? d->Desc : "(no description)");
        out("    %s\n", d->Id);
        out("    INF %s; driver %s%s%s; %s%s\n", instInfFile[d->Inf],
            d->Service[0] ? d->Service : "none",
            d->VersionText[0] ? " " : "", d->VersionText,
            inst_action_text(d->Action),
            (d->Runs && (d->Action == INST_ACT_CURRENT ||
                         d->Action == INST_ACT_NEWER)) ? " - /force: reinstall" : "");
        if (d->Problem != 0) {
            out("    problem code %lu\n", (unsigned long)d->Problem);
            print_uas_fix(uas_fix(d), "    ");
        }
        if (d->Refused) {
            out("    LEFT: %s\n",
                groupVerdict[group_of_inf(d->Inf)] == INST_GROUP_REFUSED_UNKNOWN ?
                "a version of this driver could not be read" :
                "a newer release of this driver is installed (/force goes back)");
        } else if (d->Runs && opts.Restage && trapOem[d->Inf][0] != '\0') {
            out("    /restage: reinstalled from this package\n");
        }
    }
    if (!any) {
        out("  none present. The INFs are still staged for devices that "
            "appear later.\n");
    }
    /* Section 5.4: a group's verdict, and the members not present that it
     * covers - they run the same binary, so a refusal or a /force going
     * back reaches them too. */
    for (g = 0; g < INST_GROUPS; g++) {
        if (groupVerdict[g] == INST_GROUP_OK && !opts.Force) {
            continue;
        }
        if (groupVerdict[g] != INST_GROUP_OK) {
            out("  %s.sys: LEFT - %s.\n", groupService[g],
                groupVerdict[g] == INST_GROUP_REFUSED_UNKNOWN ?
                "a version of this driver (a device's, or the installed file's) "
                "could not be read" :
                "a newer release of this driver is installed (/force goes back)");
        }
        for (i = 0; i < devCount; i++) {
            const DEVREC *d = &devs[i];
            if (d->Present || group_of(d) != g) {
                continue;
            }
            out("    %s %s (not present)%s\n", d->Id,
                d->VersionText[0] ? d->VersionText : "(no version)",
                groupVerdict[g] == INST_GROUP_OK ? " - /force reaches it" : "");
        }
    }
    out("\n");
}

/* The input warning (record 23 section 5.8), before the confirmation. */
static void input_warning(void)
{
    int i;

    for (i = 0; i < devCount; i++) {
        if (devs[i].Present && devs[i].Inf == INST_INF_XHCI &&
            is_controller(&devs[i]) && devs[i].Runs) {
            out("BEFORE YOU GO ON: the USB keyboard and mouse on this "
                "controller stop for a\nmoment while it restarts. From 2.3.x "
                "or earlier on Windows XP, XP x64 and\n98 SE they do not work "
                "until Windows' questions for the new root hub and hubs\nare "
                "answered, so have a keyboard and mouse that do not go through "
                "this driver\n(PS/2, a laptop's built-in ones, or a USB "
                "controller another driver runs).\n\n");
            return;
        }
    }
}

/* ======================================================================== */
/* The package (record 23 section 4)                                         */
/* ======================================================================== */

static int refuse(const char *a, const char *b)
{
    out("REFUSED: %s%s\n", a, b != NULL ? b : "");
    return 0;
}

/* The i-th source file of INF k: its path under base (the package's or the
 * local copy's flavour directory) in path, its name in name. 1 found, 0 no
 * more, -1 an entry that cannot be used or a path that leaves base. */
static int inf_source(int k, int i, const char *base, char *path, size_t cap,
                      char *name, size_t nameCap)
{
    char rel[INST_PATH_CHARS];
    char dir[INST_PATH_CHARS];
    char full[INST_PATH_CHARS];
    char baseFull[INST_PATH_CHARS];
    char *slash;
    char *part;
    int r = inst_inf_source_path(infText[k], i, rel, sizeof(rel));

    if (r == 0) {
        return 0;
    }
    if (r < 0) {
        /* A malformed or too long entry is an error, not the list's end. */
        inst_copy(name, "(an unusable [SourceDisksFiles] entry)", nameCap);
        return -1;
    }
    slash = strrchr(rel, '\\');
    inst_copy(name, slash != NULL ? slash + 1 : rel, nameCap);
    join(dir, sizeof(dir), base, infRel[k]);
    slash = strrchr(dir, '\\');
    if (slash != NULL) {
        *slash = '\0';
    }
    join(dir, sizeof(dir), dir, rel);
    if (GetFullPathNameA(dir, sizeof(full), full, &part) == 0 ||
        GetFullPathNameA(base, sizeof(baseFull), baseFull, &part) == 0) {
        return -1;
    }
    if (strlen(full) <= strlen(baseFull) ||
        !inst_eqn(full, baseFull, strlen(baseFull)) ||
        (full[strlen(baseFull)] != '\\' && baseFull[strlen(baseFull) - 1] != '\\')) {
        return -1;
    }
    inst_copy(path, full, cap);
    return 1;
}

static int check_package(void)
{
    char self[INST_PATH_CHARS];
    char tail[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char name[INST_PATH_CHARS];
    size_t n;
    size_t t;
    int k;
    int i;
    int cmFiles;
    INST_U32 v[4];
    INST_U32 machine;
    INST_U32 fv[4];
    DWORD size;
    char *buf;
    char flavour[16];
    const char *wantFlavour;
    int markers;
    int r;

    if (GetModuleFileNameA(NULL, self, sizeof(self)) == 0) {
        return refuse("the tool's own path could not be read.", NULL);
    }
    inst_copy(tail, "\\tools\\xhciinst\\", sizeof(tail));
    strcat(tail, INST_MY_NAME);
    n = strlen(self);
    t = strlen(tail);
    if (n <= t || !inst_eq(self + n - t, tail)) {
        out("This tool runs from %s.\n", self);
        return refuse("it must run from the package's tools\\xhciinst\\ "
                      "directory. Unzip the WHOLE package (every directory) "
                      "and run INSTALL.BAT from its top level.", NULL);
    }
    inst_copy(root, self, sizeof(root));
    root[n - t] = '\0';
    if (opts.Dir[0] == '\0') {
        inst_copy(opts.Dir, INST_DEFAULT_DIR, sizeof(opts.Dir));
    }
    if (strchr(opts.Dir, '\\') != NULL || strchr(opts.Dir, '/') != NULL ||
        strchr(opts.Dir, ':') != NULL || strstr(opts.Dir, "..") != NULL) {
        return refuse("/dir names a directory of the package: ", opts.Dir);
    }
    n = strlen(opts.Dir);
    if (n < 4 || !inst_eq(opts.Dir + n - 4, INST_DIR_SUFFIX)) {
        out("%s is the %s build; %s is not its architecture's directory.\n",
            INST_MY_NAME, INST_MY_ARCH == INST_ARCH_X86 ? "32-bit" : "x64",
            opts.Dir);
        return refuse("mixing architectures. Run INSTALL.BAT, which picks "
                      "the right program and directory.", NULL);
    }
    if (inst_starts(opts.Dir, "release-")) {
        wantFlavour = "RELEASE";
    } else if (inst_starts(opts.Dir, "debug-")) {
        wantFlavour = "DEBUG";
        out("The debug- directory is for troubleshooting only; install it "
            "only if asked.\n");
    } else {
        return refuse("not a flavour directory of the package: ", opts.Dir);
    }
    join(pkgDir, sizeof(pkgDir), root, opts.Dir);
    out("Package:   %s\n", root);
    out("Directory: %s\n\n", pkgDir);
    /* The bundled Stor98 driver: named here, never installed by this
     * tool. */
    join(path, sizeof(path), pkgDir, "stor98.inf");
    if (file_exists(path, NULL)) {
        out("The NVMe driver Stor98, a separate project, is in %s "
            "(stor98.inf);\nthis tool does not install it (its own readme "
            "says how).\n\n", opts.Dir);
    }

    cmFiles = 0;
    for (k = 0; k < INST_INF_COUNT; k++) {
        inst_copy(infRel[k], instInfFile[k], sizeof(infRel[k]));
        if (k == INST_INF_CMF) {
            join(path, sizeof(path), pkgDir, "nt\\xhcicmf.inf");
            if (file_exists(path, NULL)) {
                inst_copy(infRel[k], "nt\\xhcicmf.inf", sizeof(infRel[k]));
            }
            if (!envNow.Nt) {
                /* 98 and ME never read the NT filter INF. */
                infText[k] = NULL;
                continue;
            }
        }
        join(path, sizeof(path), pkgDir, infRel[k]);
        infText[k] = read_file(path, NULL);
        if (k >= INST_INF_CM) {
            cmFiles += infText[k] != NULL;
        } else if (infText[k] == NULL) {
            return refuse("a file of the package is missing or unreadable: ",
                          path);
        }
    }
    join(path, sizeof(path), pkgDir, "xhcicm.sys");
    cmFiles += file_exists(path, NULL);
    if (!envNow.Nt && cmFiles == 2) {
        cmFiles = 3;
    }
    if (cmFiles != 0 && cmFiles != 3) {
        return refuse("the package holds part of the Thunderbolt driver "
                      "(xhcicm.inf, xhcicmf.inf and xhcicm.sys go together).",
                      NULL);
    }
    haveCm = (cmFiles == 3);
    if (!haveCm) {
        out("This package carries no Thunderbolt or USB4 driver "
            "(xhcicm.sys); going on without it.\n\n");
    }
    if (!inst_ver_parse(XHCIINST_VERSION, pkgVersion)) {
        return refuse("the tool's own version does not parse.", NULL);
    }
    for (k = 0; k < INST_INF_COUNT; k++) {
        if (infText[k] == NULL) {
            continue;
        }
        if (!inst_inf_driverver(infText[k], v)) {
            return refuse("no DriverVer in ", instInfFile[k]);
        }
        if (inst_ver_cmp(v, pkgVersion) != 0) {
            out("%s is %lu.%lu.%lu.%lu; this tool is %s.\n", instInfFile[k],
                (unsigned long)v[0], (unsigned long)v[1], (unsigned long)v[2],
                (unsigned long)v[3], XHCIINST_VERSION);
            return refuse("an INF of another release is in the directory. "
                          "Unzip this release's package afresh.", NULL);
        }
        if (!inst_inf_arch_ok(infText[k], INST_MY_ARCH)) {
            return refuse("the INF is not this architecture's: ",
                          instInfFile[k]);
        }
        for (i = 0; (r = inf_source(k, i, pkgDir, path, sizeof(path), name,
                                    sizeof(name))) != 0; i++) {
            if (r < 0) {
                return refuse("a file the INF names lies outside the package: ",
                              name);
            }
            if (!file_exists(path, &size) || size == 0) {
                return refuse("a file the INF names is missing or empty: ",
                              path);
            }
            n = strlen(name);
            if (n > 4 && inst_eq(name + n - 4, ".sys")) {
                buf = read_file(path, &size);
                if (buf == NULL ||
                    !inst_pe_machine((const unsigned char *)buf, size, &machine) ||
                    machine != INST_PE_MINE) {
                    free(buf);
                    return refuse("a driver is not this architecture's: ",
                                  path);
                }
                markers = inst_flavour_marker((const unsigned char *)buf, size,
                                              flavour, sizeof(flavour));
                if (inst_eq(name, "xhci98.sys")) {
                    pkgSigned = inst_pe_signed((const unsigned char *)buf, size);
                }
                free(buf);
                if ((markers == 0 && (inst_eq(name, "xhci98.sys") ||
                                      inst_eq(name, "xhciuas.sys"))) ||
                    markers > 1 ||
                    (markers == 1 && !inst_eq(flavour, wantFlavour))) {
                    out("%s carries %d flavour marker(s)%s%s; this directory "
                        "is %s.\n", name, markers, markers ? ", the first " : "",
                        flavour, wantFlavour);
                    return refuse("a driver of another build flavour is in "
                                  "the directory. Unzip the package afresh.",
                                  NULL);
                }
                if (file_version(path, fv) <= 0 ||
                    inst_ver_cmp(fv, pkgVersion) != 0) {
                    return refuse("a driver is not this release's (its file "
                                  "version differs): ", path);
                }
            }
        }
    }
    return 1;
}

static void load_ids(void)
{
    int k;

    for (k = 0; k < INST_INF_COUNT; k++) {
        infIds[k].Count = 0;
        if (infText[k] != NULL) {
            inst_inf_models(infText[k], &envNow, &infIds[k]);
        }
    }
}

/* ======================================================================== */
/* Registry values (record 23 section 8)                                     */
/* ======================================================================== */

/* 1 when device d takes value v's writes after this run. */
static int value_target(const DEVREC *d, int v)
{
    int inf = instValues[v].Inf;

    if (!d->Present || d->Inf != inf || d->Refused) {
        return 0;
    }
    if (inf == INST_INF_XHCI && !is_controller(d)) {
        return 0;
    }
    if (!envNow.Nt) {
        return inst_eq(d->Service, instInfService[inf]);
    }
    return d->Action == INST_ACT_INSTALL || d->Action == INST_ACT_UPDATE ||
           d->Action == INST_ACT_CURRENT || d->Action == INST_ACT_NEWER;
}

static int value_now(const DEVREC *d, const char *name, DWORD *value)
{
    char path[INST_PATH_CHARS];

    if (d->Driver[0] == '\0' || !inst_eq(d->Service, instInfService[d->Inf])) {
        return -1;
    }
    driver_key_path(d, path, sizeof(path));
    return reg_dword(HKEY_LOCAL_MACHINE, path, name, value) ? 1 : 0;
}

/* Ask, or take /set and /defaults (record 23 section 8.3). */
static void ask_values(void)
{
    int v;
    int i;
    int k;
    char line[64];
    char mask[64];

    memset(answerSet, 0, sizeof(answerSet));
    for (k = 0; k < opts.SetCount; k++) {
        answerSet[opts.SetIndex[k]] = 1;
        answerValue[opts.SetIndex[k]] = opts.SetValue[k];
    }
    for (v = 0; v < instValueCount; v++) {
        INST_U32 m = inst_value_asked_mask(v, &envNow);
        INST_U32 def;
        int targets = 0;
        int haveDef;
        if (infText[instValues[v].Inf] == NULL) {
            continue;
        }
        haveDef = inst_inf_addreg_default(infText[instValues[v].Inf],
                                          instValues[v].Name, &def);
        for (i = 0; i < devCount; i++) {
            targets += value_target(&devs[i], v);
        }
        if (answerSet[v]) {
            out("%s: set to %lu by /set%s.\n", instValues[v].Name,
                (unsigned long)answerValue[v],
                targets ? "" : " (no device takes it now: nothing to write)");
            if (instValues[v].Caution != NULL &&
                (instValues[v].CautionMask == 0 ||
                 (instValues[v].CautionMask & (1UL << answerValue[v])) != 0)) {
                out("  NOTE: %s\n", instValues[v].Caution);
            }
            out("\n");
            continue;
        }
        if (!inst_value_applies(v, &envNow) || targets == 0 || !haveDef) {
            continue;
        }
        /* /defaults, and /yes (a run nobody answers), ask nothing: the
         * values not named by /set keep what they are. */
        if ((opts.Defaults || opts.Yes) && !opts.List) {
            continue;
        }
        out("%s - %s\n", instValues[v].Name, instValues[v].Meaning);
        for (i = 0; i < devCount; i++) {
            DWORD now;
            int r;
            if (!value_target(&devs[i], v)) {
                continue;
            }
            r = value_now(&devs[i], instValues[v].Name, &now);
            if (r > 0) {
                out("  now: %lu (%s)\n", (unsigned long)now, devs[i].Desc);
            } else if (r == 0) {
                out("  now: not set, the install writes %lu (%s)\n",
                    (unsigned long)def, devs[i].Desc);
            } else {
                out("  now: new install, the default %lu (%s)\n",
                    (unsigned long)def, devs[i].Desc);
            }
        }
        inst_mask_text(m, mask, sizeof(mask));
        out("  default: %lu     allowed: %s\n", (unsigned long)def, mask);
        if (instValues[v].Caution != NULL &&
            (instValues[v].CautionMask == 0 || (instValues[v].CautionMask & m) != 0)) {
            out("  NOTE: %s\n", instValues[v].Caution);
        }
        /* Record 23 section 8.4 (decision D12): the two values whose
         * default approves devices until the restart; not XhciCmSafeEject. */
        if (inst_eq(instValues[v].Name, "XhciCmMode") ||
            inst_eq(instValues[v].Name, "XhciCmApprove")) {
            for (i = 0; i < devCount; i++) {
                if (value_target(&devs[i], v) &&
                    devs[i].Action == INST_ACT_INSTALL) {
                    out("  NOTE: a new install starts with the default; your "
                        "answer applies from the next\n  restart. Until then "
                        "Thunderbolt devices are approved by the default: "
                        "unplug the\n  dock until the restart if you answer "
                        "0.\n");
                    break;
                }
            }
        }
        if (opts.List) {
            out("\n");
            continue;
        }
        for (;;) {
            INST_U32 a;
            int r;
            out("  new value [Enter keeps the value now]: ");
            if (!read_line(line, sizeof(line))) {
                out("\n");
                break;
            }
            r = inst_value_answer(line, m, &a);
            if (r == 0) {
                break;
            }
            if (r > 0) {
                answerSet[v] = 1;
                answerValue[v] = a;
                break;
            }
            out("  %s is not one of: %s\n", line, mask);
        }
        out("\n");
    }
}

#define INST_SILENT_KEY "System\\CurrentControlSet\\Services\\USB\\SilentClasses"

/* silent-on.bat's first half (owner, 2026-10-15): the package's own
 * tools\silent\SILENT.REG - the single source of the list - merged into
 * HKLM\System\CurrentControlSet\Services\USB\SilentClasses, and every value
 * read back as written. 1 when the list is in place; 0 refuses the switch. */
static int silent_list_merge(void)
{
    char path[INST_PATH_CHARS];
    char *text;
    char *p;
    char *nl;
    char name[32];
    unsigned char data[64];
    unsigned char back[64];
    size_t dn;
    DWORD n;
    DWORD bn;
    DWORD type;
    HKEY k;
    int in = 0;
    int values = 0;
    int ok = 1;

    join(path, sizeof(path), root, "tools\\silent\\SILENT.REG");
    text = read_file(path, NULL);
    if (text == NULL) {
        out("%s cannot be read: the silent install is not turned on.\n", path);
        return 0;
    }
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_SILENT_KEY, 0, NULL, 0,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &k, NULL) !=
        ERROR_SUCCESS) {
        out("The SilentClasses list cannot be opened: the silent install is "
            "not turned on.\n");
        free(text);
        return 0;
    }
    for (p = text; *p != '\0' && ok; p = nl) {
        nl = strchr(p, '\n');
        if (nl == NULL) {
            nl = p + strlen(p);
        } else {
            *nl++ = '\0';
        }
        if (strlen(p) > 0 && p[strlen(p) - 1] == '\r') {
            p[strlen(p) - 1] = '\0';
        }
        if (p[0] == '[') {
            in = inst_eq(p, "[HKEY_LOCAL_MACHINE\\" INST_SILENT_KEY "]");
            continue;
        }
        if (!in || p[0] != '"') {
            continue;
        }
        /* "name"=hex:aa,bb,... (inst_core.c, host-tested) */
        if (!inst_reg_hex_line(p, name, sizeof(name), data, sizeof(data),
                               &dn)) {
            ok = 0;
            break;
        }
        n = (DWORD)dn;
        bn = sizeof(back);
        /* A value already as SILENT.REG has it is not written again: an
         * unchanged rerun says "No changes were made." */
        if (RegQueryValueExA(k, name, NULL, &type, back, &bn) == ERROR_SUCCESS &&
            type == REG_BINARY && bn == n && memcmp(back, data, n) == 0) {
            values++;
            continue;
        }
        if (RegSetValueExA(k, name, 0, REG_BINARY, data, n) == ERROR_SUCCESS) {
            flagChanged = 1;    /* a write happened, whatever follows */
        } else {
            ok = 0;
            break;
        }
        bn = sizeof(back);
        if (RegQueryValueExA(k, name, NULL, &type, back, &bn) != ERROR_SUCCESS ||
            type != REG_BINARY || bn != n || memcmp(back, data, n) != 0) {
            ok = 0;
            break;
        }
        values++;
    }
    RegCloseKey(k);
    free(text);
    if (!ok || values == 0) {
        out("The SilentClasses list did not read back as %s: the silent "
            "install is not\nturned on (the list may be merged in part).\n",
            path);
        return 0;
    }
    out("The SilentClasses list of SILENT.REG is in place (%d values).\n",
        values);
    return 1;
}

static void write_values(void)
{
    int v;
    int i;
    char path[INST_PATH_CHARS];
    HKEY k;
    DWORD now;
    DWORD val;
    int r;
    LONG w;

    for (v = 0; v < instValueCount; v++) {
        int listDone = 0;
        if (!answerSet[v]) {
            continue;
        }
        for (i = 0; i < devCount; i++) {
            DEVREC *d = &devs[i];
            if (!d->Present || d->Inf != instValues[v].Inf ||
                (d->Inf == INST_INF_XHCI && !is_controller(d)) ||
                !inst_eq(d->Service, instInfService[d->Inf]) ||
                d->Driver[0] == '\0') {
                continue;
            }
            /* The silent install on: the list first - merged and verified
             * once, even where the switch already reads 1, as silent-on.bat
             * does - then the switch; off is the switch alone, the list
             * left, as silent-off.bat. Never on NT (/set refuses it). */
            if (inst_eq(instValues[v].Name, "XhciSilentInstall")) {
                if (envNow.Nt) {
                    continue;
                }
                if (answerValue[v] == 1 && listDone == 0) {
                    listDone = silent_list_merge() ? 1 : -1;
                }
                if (answerValue[v] == 1 && listDone < 0) {
                    flagFailed = 1;
                    continue;
                }
            }
            r = value_now(d, instValues[v].Name, &now);
            if (r > 0 && now == answerValue[v]) {
                out("%s stays %lu on %s.\n", instValues[v].Name,
                    (unsigned long)now, d->Desc);
                continue;
            }
            driver_key_path(d, path, sizeof(path));
            val = (DWORD)answerValue[v];
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_SET_VALUE, &k)
                != ERROR_SUCCESS) {
                out("%s could not be written on %s (its driver key did not "
                    "open).\n", instValues[v].Name, d->Desc);
                flagFailed = 1;
                continue;
            }
            w = RegSetValueExA(k, instValues[v].Name, 0, REG_DWORD,
                               (const BYTE *)&val, sizeof(val));
            RegCloseKey(k);
            if (w != ERROR_SUCCESS) {
                out("%s could not be written on %s (error %ld).\n",
                    instValues[v].Name, d->Desc, (long)w);
                flagFailed = 1;
                continue;
            }
            flagChanged = 1;
            if (r > 0) {
                out("%s set from %lu to %lu on %s.\n", instValues[v].Name,
                    (unsigned long)now, (unsigned long)val, d->Desc);
            } else {
                out("%s set to %lu on %s.\n", instValues[v].Name,
                    (unsigned long)val, d->Desc);
            }
            logonly("  wrote HKLM\\%s\\%s = %lu\n", path, instValues[v].Name,
                    (unsigned long)val);
            if (inst_eq(instValues[v].Name, "XhciForceBulkOnly")) {
                out("  A disk takes it when it is next connected.\n");
            } else {
                out("  It takes effect when the driver next starts (restart "
                    "Windows, or disable and enable the device).\n");
                flagRestart = 1;
            }
        }
    }
}

/* How many values write_values would write now (a device whose value reads
 * other than the answer, or not at all): the confirmation's wording. */
static int values_to_write(void)
{
    int v;
    int i;
    int n = 0;
    DWORD now;

    for (v = 0; v < instValueCount; v++) {
        if (!answerSet[v] || (envNow.Nt &&
                              inst_eq(instValues[v].Name, "XhciSilentInstall"))) {
            continue;
        }
        for (i = 0; i < devCount; i++) {
            const DEVREC *d = &devs[i];
            if (!value_target(d, v) || d->Driver[0] == '\0' ||
                !inst_eq(d->Service, instInfService[d->Inf])) {
                continue;
            }
            if (value_now(d, instValues[v].Name, &now) <= 0 ||
                now != answerValue[v]) {
                n++;
            }
        }
    }
    return n;
}

/* ======================================================================== */
/* NT: staging, the trap, the installs (record 23 sections 5.5 to 5.9)       */
/* ======================================================================== */

/* The local copies (record 23 section 4.4): %windir%\XHCI98\<version>-<arch>-
 * <flavour>-<content tag>, one immutable directory per package content, each
 * with an XHCIINST.LST naming the files the tool put there. */
static int local_dir(char *dir, size_t cap)
{
    char win[INST_PATH_CHARS];

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    join(dir, cap, win, "XHCI98");
    return 1;
}

/* ======================================================================== */
/* Objects shared with XHCITR, and the tool itself (record 23 section 9.5;   */
/* record 25 sections 6 and 8)                                               */
/* ======================================================================== */

#ifndef MUTEX_MODIFY_STATE
#define MUTEX_MODIFY_STATE 0x0001UL
#endif

#define INST_MAINT_WAIT_MS  60000UL

typedef BOOL (WINAPI *PFN_ALLOCSID)(PSID_IDENTIFIER_AUTHORITY, BYTE, DWORD,
                                    DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                    DWORD, PSID *);
typedef PVOID (WINAPI *PFN_FREESID)(PSID);
typedef BOOL (WINAPI *PFN_INITACL)(PACL, DWORD, DWORD);
typedef BOOL (WINAPI *PFN_ADDACE)(PACL, DWORD, DWORD, PSID);
typedef BOOL (WINAPI *PFN_INITSD)(PSECURITY_DESCRIPTOR, DWORD);
typedef BOOL (WINAPI *PFN_SETDACL)(PSECURITY_DESCRIPTOR, BOOL, PACL, BOOL);
typedef BOOL (WINAPI *PFN_MOVEEX)(LPCSTR, LPCSTR, DWORD);
typedef HWND (WINAPI *PFN_FINDWINDOW)(LPCSTR, LPCSTR);
typedef BOOL (WINAPI *PFN_POSTMESSAGE)(HWND, UINT, WPARAM, LPARAM);

typedef struct _SHARED_SD {
    SECURITY_DESCRIPTOR Sd;
    SECURITY_ATTRIBUTES Sa;
    DWORD Acl[64];
} SHARED_SD;

/* NT: the security of an object XHCIINST and XHCITR share. XHCITR runs as
 * the invoker and XHCIINST as an administrator, so every user (the World
 * SID) gets worldAccess and SYSTEM and the Administrators get fullAccess.
 * The routines are looked up at run time, so 98 SE's and ME's advapi32 is
 * never asked for them (NULL: the default security there). */
static SECURITY_ATTRIBUTES *shared_sd(SHARED_SD *s, DWORD worldAccess,
                                      DWORD fullAccess)
{
    static SID_IDENTIFIER_AUTHORITY world = SECURITY_WORLD_SID_AUTHORITY;
    static SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    HMODULE adv;
    PFN_ALLOCSID allocSid;
    PFN_FREESID freeSid;
    PFN_INITACL initAcl;
    PFN_ADDACE addAce;
    PFN_INITSD initSd;
    PFN_SETDACL setDacl;
    PSID sids[3];
    DWORD access[3];
    SECURITY_ATTRIBUTES *r = NULL;
    int i;

    if (!envNow.Nt) {
        return NULL;
    }
    adv = GetModuleHandleA("advapi32.dll");
    if (adv == NULL) {
        return NULL;
    }
    allocSid = (PFN_ALLOCSID)GetProcAddress(adv, "AllocateAndInitializeSid");
    freeSid = (PFN_FREESID)GetProcAddress(adv, "FreeSid");
    initAcl = (PFN_INITACL)GetProcAddress(adv, "InitializeAcl");
    addAce = (PFN_ADDACE)GetProcAddress(adv, "AddAccessAllowedAce");
    initSd = (PFN_INITSD)GetProcAddress(adv, "InitializeSecurityDescriptor");
    setDacl = (PFN_SETDACL)GetProcAddress(adv, "SetSecurityDescriptorDacl");
    if (allocSid == NULL || freeSid == NULL || initAcl == NULL ||
        addAce == NULL || initSd == NULL || setDacl == NULL) {
        return NULL;
    }
    sids[0] = sids[1] = sids[2] = NULL;
    access[0] = worldAccess;
    access[1] = fullAccess;
    access[2] = fullAccess;
    if (!allocSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0,
                  &sids[0]) ||
        !allocSid(&ntAuth, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0,
                  &sids[1]) ||
        !allocSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sids[2])) {
        goto cleanup;
    }
    if (!initAcl((PACL)s->Acl, sizeof(s->Acl), ACL_REVISION)) {
        goto cleanup;
    }
    for (i = 0; i < 3; i++) {
        if (!addAce((PACL)s->Acl, ACL_REVISION, access[i], sids[i])) {
            goto cleanup;
        }
    }
    if (!initSd(&s->Sd, SECURITY_DESCRIPTOR_REVISION) ||
        !setDacl(&s->Sd, TRUE, (PACL)s->Acl, FALSE)) {
        goto cleanup;
    }
    s->Sa.nLength = sizeof(s->Sa);
    s->Sa.lpSecurityDescriptor = &s->Sd;
    s->Sa.bInheritHandle = FALSE;
    r = &s->Sa;
cleanup:
    for (i = 0; i < 3; i++) {
        if (sids[i] != NULL) {
            freeSid(sids[i]);
        }
    }
    return r;
}

/* A lock shared with XHCITR or another XHCIINST (record 24 section 3.3,
 * record 25 sections 6 and 8): opened when it exists, else created with
 * the shared security; a lost race to create it opens the winner's. */
static HANDLE shared_mutex(const char *name)
{
    SHARED_SD s;
    HANDLE h;

    h = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);
    if (h == NULL) {
        h = CreateMutexA(shared_sd(&s, SYNCHRONIZE | MUTEX_MODIFY_STATE,
                                   MUTEX_ALL_ACCESS), FALSE, name);
    }
    if (h == NULL) {
        h = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);
    }
    return h;
}

/* The maintenance lock (record 25 section 8): one XHCIINST at a time reads
 * the tool's versions, reconciles its pending operations, closes it,
 * copies it and writes its Run value. An abandoned lock is taken over. */
static HANDLE maintMutex;

static int maint_lock(void)
{
    DWORD w;

    maintMutex = shared_mutex(envNow.Nt ? "Global\\XhciinstMaint"
                                        : "XhciinstMaint");
    if (maintMutex == NULL) {
        out("The xHCI98 USB Tree's lock could not be opened (error %lu).\n",
            (unsigned long)GetLastError());
        return 0;
    }
    w = WaitForSingleObject(maintMutex, INST_MAINT_WAIT_MS);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        out("Another XHCIINST has been installing or removing the xHCI98 USB "
            "Tree for a minute:\nit is left as it is; run INSTALL.BAT again "
            "when that one has finished.\n");
        CloseHandle(maintMutex);
        maintMutex = NULL;
        return 0;
    }
    return 1;
}

/* The quit event (record 25 section 7.1), which XHCITR opens once a second
 * and never creates: XHCIINST creates it, sets it, and closes it when its
 * work on the tool ends, so it exists only then. Every user may wait on
 * it; only SYSTEM and the Administrators set it. */
static HANDLE trayQuit;

static void maint_unlock(void)
{
    if (trayQuit != NULL) {
        CloseHandle(trayQuit);
        trayQuit = NULL;
    }
    if (maintMutex != NULL) {
        ReleaseMutex(maintMutex);
        CloseHandle(maintMutex);
        maintMutex = NULL;
    }
}

/* NT: a file or an empty directory deleted at the next restart
 * (MoveFileExA to NULL with MOVEFILE_DELAY_UNTIL_REBOOT, looked up at run
 * time); 98 SE and ME have no such call, and nothing of the USB tree tool
 * is ever left for their restart. */
static int delete_at_restart(const char *path)
{
    PFN_MOVEEX moveEx = (PFN_MOVEEX)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                   "MoveFileExA");

    return envNow.Nt && moveEx != NULL &&
           moveEx(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
}

/* The installed tool's paths: the EXE; beside it the name every new copy
 * is written and checked under first (.TMP), the installed copy kept
 * while 98 SE and ME put a new one in its place (.BAK), and on NT a copy
 * in use renamed at an uninstall for its deletion at the restart (.DEL);
 * and the Run value's data. Nothing else is ever left for a restart
 * (record 25 section 8). */
typedef struct _TRAY_PATHS {
    char Dir[INST_PATH_CHARS];
    char Exe[INST_PATH_CHARS];
    char Tmp[INST_PATH_CHARS];
    char Bak[INST_PATH_CHARS];
    char Del[INST_PATH_CHARS];
    char Run[INST_PATH_CHARS + 16];
} TRAY_PATHS;

static int tray_paths(TRAY_PATHS *t)
{
    size_t n;

    if (!local_dir(t->Dir, sizeof(t->Dir))) {
        return 0;
    }
    join(t->Exe, sizeof(t->Exe), t->Dir, inst_tray_file(INST_MY_ARCH));
    n = strlen(t->Exe);
    if (n < 4 || !inst_eq(t->Exe + n - 4, ".EXE") ||
        !inst_tray_run_data(t->Exe, t->Run, sizeof(t->Run))) {
        return 0;
    }
    inst_copy(t->Tmp, t->Exe, sizeof(t->Tmp));
    inst_copy(t->Bak, t->Exe, sizeof(t->Bak));
    inst_copy(t->Del, t->Exe, sizeof(t->Del));
    strcpy(t->Tmp + n - 4, ".TMP");
    strcpy(t->Bak + n - 4, ".BAK");
    strcpy(t->Del + n - 4, ".DEL");
    return 1;
}

static int delete_file(const char *path)
{
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    return DeleteFileA(path);
}

/* 1 when exe can be replaced now: absent, or opened for writing with no
 * sharing (a running image cannot be). */
static int tray_free(const char *exe)
{
    HANDLE h = CreateFileA(exe, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);

    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
    }
    CloseHandle(h);
    return 1;
}

/* Every running copy asked to quit - through the quit event, which every
 * session's copy sees, and WM_CLOSE to the tray window in this one - and
 * waited for until the file can be replaced, 15 s at most. A copy is never
 * terminated. 1 when the file is free. The event stays set until
 * maint_unlock closes it. */
static int tray_close(const char *exe)
{
    SHARED_SD s;
    HMODULE user32;
    PFN_FINDWINDOW findWindow = NULL;
    PFN_POSTMESSAGE postMessage = NULL;
    HWND w;
    DWORD start;
    int ok;

    SetFileAttributesA(exe, FILE_ATTRIBUTE_NORMAL);
    if (tray_free(exe)) {
        return 1;
    }
    out("Closing the running xHCI98 USB Tree.\n");
    if (trayQuit == NULL) {
        trayQuit = CreateEventA(shared_sd(&s, SYNCHRONIZE, EVENT_ALL_ACCESS),
                                TRUE, FALSE, envNow.Nt ? INST_TRAY_QUIT_NT
                                                       : INST_TRAY_QUIT_9X);
    }
    if (trayQuit != NULL) {
        SetEvent(trayQuit);
    }
    user32 = LoadLibraryA("user32.dll");
    if (user32 != NULL) {
        findWindow = (PFN_FINDWINDOW)GetProcAddress(user32, "FindWindowA");
        postMessage = (PFN_POSTMESSAGE)GetProcAddress(user32, "PostMessageA");
    }
    if (findWindow != NULL && postMessage != NULL) {
        w = findWindow(INST_TRAY_CLASS, NULL);
        if (w != NULL) {
            postMessage(w, WM_CLOSE, 0, 0);
        }
    }
    start = GetTickCount();
    while (!(ok = tray_free(exe)) && GetTickCount() - start < INST_TRAY_CLOSE_MS) {
        Sleep(250);
    }
    if (user32 != NULL) {
        FreeLibrary(user32);
    }
    if (!ok) {
        out("%s is still in use after %lu seconds.\n", exe,
            (unsigned long)(INST_TRAY_CLOSE_MS / 1000UL));
    }
    return ok;
}

/* The Run value: 0 absent, 1 the data wanted, 2 other data (or unreadable). */
static int tray_run_state(const char *data)
{
    char cur[INST_PATH_CHARS + 16];
    int r = reg_text3(HKEY_LOCAL_MACHINE, INST_TRAY_RUN_KEY,
                      INST_TRAY_RUN_VALUE, cur, sizeof(cur));

    if (r == 1) {
        return inst_eq(cur, data) ? 1 : 2;
    }
    return r == 0 ? 0 : 2;
}

/* The Run value deleted: 1 deleted, 0 there was none, -1 failed (said). */
static int tray_run_delete(void)
{
    HKEY k;
    LONG r;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_TRAY_RUN_KEY, 0, KEY_SET_VALUE,
                      &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r == ERROR_SUCCESS) {
        r = RegDeleteValueA(k, INST_TRAY_RUN_VALUE);
        RegCloseKey(k);
        if (r == ERROR_FILE_NOT_FOUND) {
            return 0;
        }
        if (r == ERROR_SUCCESS) {
            flagChanged = 1;
            out("The xHCI98 USB Tree no longer starts at login.\n");
            return 1;
        }
    }
    out("The xHCI98 USB Tree's start at login could not be deleted (error "
        "%lu).\n", (unsigned long)r);
    return -1;
}

/* A leftover deleted: 1, or 0 said, with steps left. */
static int tray_delete_leftover(const char *path)
{
    if (!delete_file(path)) {
        out("%s could not be deleted (error %lu); delete it by hand.\n", path,
            (unsigned long)GetLastError());
        flagSteps = 1;
        return 0;
    }
    flagChanged = 1;
    logonly("%s deleted (a leftover)\n", path);
    return 1;
}

/* Leftovers of an interrupted run, before any version is read (record 25
 * section 8): .TMP deleted; a .BAK with no tool beside it renamed back (it
 * is the tool, caught between two renames on 98 SE or ME), one beside the
 * tool deleted; on NT a .DEL deleted, or, while it still runs, its
 * deletion at the restart asked again rather than assumed. A deletion that
 * fails is said and leaves steps. 1, or 0 when a .BAK could not be renamed
 * back (said: the tool is not touched further). */
static int tray_leftovers(const TRAY_PATHS *t)
{
    if (file_exists(t->Tmp, NULL)) {
        tray_delete_leftover(t->Tmp);
    }
    switch (inst_tray_leftover(file_exists(t->Bak, NULL),
                               file_exists(t->Exe, NULL))) {
    case INST_TRAY_BAK_RESTORE:
        if (!MoveFileA(t->Bak, t->Exe)) {
            out("%s could not be renamed back to %s (error %lu): rename it by "
                "hand, then run\nINSTALL.BAT again.\n", t->Bak, t->Exe,
                (unsigned long)GetLastError());
            return 0;
        }
        flagChanged = 1;
        logonly("%s renamed back to %s\n", t->Bak, t->Exe);
        break;
    case INST_TRAY_BAK_DELETE:
        tray_delete_leftover(t->Bak);
        break;
    default:
        break;
    }
    if (envNow.Nt && file_exists(t->Del, NULL)) {
        if (delete_file(t->Del)) {
            flagChanged = 1;
            logonly("%s deleted (a leftover)\n", t->Del);
        } else if (delete_at_restart(t->Del)) {
            flagChanged = 1;
            flagRestart = 1;
            trayDeferred = 1;
            out("%s is still in use: it is deleted at the restart.\n", t->Del);
        } else {
            out("%s is still in use and could not be scheduled for deletion "
                "(error %lu);\ndelete it after a restart.\n", t->Del,
                (unsigned long)GetLastError());
            flagSteps = 1;
        }
    }
    return 1;
}

/* The package's tool put in place: written as .TMP and compared with the
 * package's first; then, once running copies are closed, NT replaces the
 * tool with a replacing rename, and 98 SE and ME rename the tool .BAK,
 * .TMP to the tool, and delete .BAK, renaming .BAK back on a failure.
 * INST_TRAY_PUT_DONE in place; INST_TRAY_PUT_IN_USE the tool is still
 * running and is left as it is (said); INST_TRAY_PUT_FAILED (said). No
 * .TMP is left behind. */
static int tray_put(const char *from, const TRAY_PATHS *t)
{
    PFN_MOVEEX moveEx;
    int had = file_exists(t->Exe, NULL);

    CreateDirectoryA(t->Dir, NULL);
    SetFileAttributesA(t->Tmp, FILE_ATTRIBUTE_NORMAL);
    if (!CopyFileA(from, t->Tmp, FALSE) || !same_file(from, t->Tmp)) {
        out("The xHCI98 USB Tree could not be copied to %s (error %lu).\n",
            t->Tmp, (unsigned long)GetLastError());
        delete_file(t->Tmp);
        return INST_TRAY_PUT_FAILED;
    }
    SetFileAttributesA(t->Tmp, FILE_ATTRIBUTE_NORMAL);
    if (!tray_close(t->Exe)) {
        goto inUse;
    }
    if (!had) {
        if (!MoveFileA(t->Tmp, t->Exe)) {
            goto failed;
        }
    } else if (envNow.Nt) {
        moveEx = (PFN_MOVEEX)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                            "MoveFileExA");
        if (moveEx == NULL ||
            !moveEx(t->Tmp, t->Exe, MOVEFILE_REPLACE_EXISTING)) {
            if (!tray_free(t->Exe)) {
                goto inUse;
            }
            goto failed;
        }
    } else {
        if (!MoveFileA(t->Exe, t->Bak)) {
            if (!tray_free(t->Exe)) {
                goto inUse;
            }
            goto failed;
        }
        if (!MoveFileA(t->Tmp, t->Exe)) {
            out("%s could not be put in place (error %lu).\n", t->Exe,
                (unsigned long)GetLastError());
            if (!MoveFileA(t->Bak, t->Exe)) {
                out("%s could not be renamed back to %s (error %lu): rename "
                    "it by hand.\n", t->Bak, t->Exe,
                    (unsigned long)GetLastError());
            }
            delete_file(t->Tmp);
            return INST_TRAY_PUT_FAILED;
        }
        /* The replacement stands; a .BAK that stays is said, with steps
         * left, and the next run deletes it. */
        tray_delete_leftover(t->Bak);
    }
    SetFileAttributesA(t->Exe, FILE_ATTRIBUTE_NORMAL);
    flagChanged = 1;
    out("Copied the xHCI98 USB Tree to %s.\n", t->Exe);
    return INST_TRAY_PUT_DONE;

inUse:
    delete_file(t->Tmp);
    out("The xHCI98 USB Tree is still running, so it was not updated. Close "
        "the xHCI98 USB\nTree (or restart Windows) and run INSTALL again to "
        "update it.\n");
    return INST_TRAY_PUT_IN_USE;

failed:
    out("The xHCI98 USB Tree could not be put at %s (error %lu).\n", t->Exe,
        (unsigned long)GetLastError());
    delete_file(t->Tmp);
    return INST_TRAY_PUT_FAILED;
}

/* Record 23 section 9.5: the tool for this machine's architecture into
 * %windir%\XHCI98, and the Run value that starts it at login (none with
 * /notray), under the maintenance lock. Under /list, what would be done. */
static void tray_install(void)
{
    const char *name = inst_tray_file(INST_MY_ARCH);
    char from[INST_PATH_CHARS];
    TRAY_PATHS t;
    INST_U32 pv[4];
    INST_U32 iv[4];
    int pkg;
    int inst;
    int copy;
    int put;
    int action;
    HKEY k = NULL;

    join(from, sizeof(from), root, "tools\\xhcitr");
    join(from, sizeof(from), from, name);
    if (!tray_paths(&t)) {
        out("The xHCI98 USB Tree's place could not be named.\n");
        flagFailed = 1;
        return;
    }
    /* A package without the tool leaves everything of it as it is. */
    if (!file_exists(from, NULL)) {
        out("This package has no tools\\xhcitr\\%s: the xHCI98 USB Tree is "
            "not installed.\n", name);
        return;
    }
    if (!opts.List) {
        if (!maint_lock()) {
            flagFailed = 1;
            return;
        }
        if (!tray_leftovers(&t)) {
            flagSteps = 1;
            goto done;
        }
    }
    pkg = file_version(from, pv);
    inst = file_version(t.Exe, iv);
    copy = inst_tray_copy_needed(inst, iv, pkg, pv);
    if (opts.List) {
        action = inst_tray_run_action(opts.NoTray, tray_run_state(t.Run),
                                      copy || inst != 0);
        out("The xHCI98 USB Tree: %s %s%s\n",
            copy ? "would be copied to" : "is current at", t.Exe,
            action == INST_TRAY_RUN_SET ? ", and started at login." :
            action == INST_TRAY_RUN_DELETE ? "; it would no longer start at "
                                             "login (/notray)." : ".");
        return;
    }
    if (copy) {
        put = tray_put(from, &t);
        if (put == INST_TRAY_PUT_FAILED) {
            flagFailed = 1;
        } else if (put == INST_TRAY_PUT_IN_USE) {
            flagSteps = 1;
        }
    } else {
        out("The xHCI98 USB Tree at %s is current.\n", t.Exe);
    }
    action = inst_tray_run_action(opts.NoTray, tray_run_state(t.Run),
                                  file_exists(t.Exe, NULL));
    if (action == INST_TRAY_RUN_SET) {
        if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_TRAY_RUN_KEY, 0, NULL, 0,
                            KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS &&
            RegSetValueExA(k, INST_TRAY_RUN_VALUE, 0, REG_SZ,
                           (const BYTE *)t.Run, (DWORD)strlen(t.Run) + 1) ==
                ERROR_SUCCESS) {
            flagChanged = 1;
            out("It starts at each login (INSTALL /notray: it does not).\n");
        } else {
            out("Its start at login could not be written.\n");
            flagFailed = 1;
        }
        if (k != NULL) {
            RegCloseKey(k);
        }
    } else if (action == INST_TRAY_RUN_DELETE && tray_run_delete() < 0) {
        flagFailed = 1;
    }
done:
    maint_unlock();
}

/* Record 23 section 9.5: the uninstall's half, under the maintenance lock.
 * The tool, its .TMP, .BAK and .DEL and the Run value are this tool's own,
 * outside every XHCIINST.LST, and go before %windir%\XHCI98 itself. A copy
 * still in use is, on NT, renamed .DEL and deleted at the restart
 * (trayDeferred: the directory with it); on 98 SE and ME it is left and
 * the next uninstall deletes it. */
static void tray_remove(void)
{
    TRAY_PATHS t;

    if (!tray_paths(&t)) {
        return;
    }
    if (opts.List) {
        if (tray_run_state(t.Run) != 0) {
            out("(/list) the xHCI98 USB Tree's start at login would be "
                "deleted.\n");
        }
        if (file_exists(t.Exe, NULL)) {
            out("(/list) %s would be closed and deleted.\n", t.Exe);
        }
        return;
    }
    if (!maint_lock()) {
        flagFailed = 1;
        return;
    }
    if (tray_run_delete() < 0) {
        flagFailed = 1;
    }
    if (!tray_leftovers(&t)) {
        flagSteps = 1;
        goto done;
    }
    if (!file_exists(t.Exe, NULL)) {
        goto done;
    }
    tray_close(t.Exe);
    if (delete_file(t.Exe)) {
        flagChanged = 1;
        out("%s removed.\n", t.Exe);
        goto done;
    }
    if (!envNow.Nt) {
        out("%s is still running, so it stays; it no longer starts at login. "
            "Run\nINSTALL /uninstall again after a restart to delete it.\n",
            t.Exe);
        flagSteps = 1;
        goto done;
    }
    /* A running image can be renamed on its volume: the tool's own name is
     * free at once, and only the renamed file waits for the restart. */
    if (file_exists(t.Del, NULL) || !MoveFileA(t.Exe, t.Del)) {
        out("%s is in use and could not be renamed; run INSTALL /uninstall "
            "again after a\nrestart.\n", t.Exe);
        flagSteps = 1;
        goto done;
    }
    flagChanged = 1;
    trayDeferred = 1;
    if (!delete_at_restart(t.Del)) {
        out("%s could not be scheduled for deletion; run INSTALL /uninstall "
            "again after a\nrestart.\n", t.Del);
        flagSteps = 1;
        goto done;
    }
    flagRestart = 1;
    out("%s is in use: renamed %s, which is deleted at the restart.\n",
        t.Exe, t.Del);
done:
    maint_unlock();
}

/* Delete the files one copy's list names, the list, and the directory if
 * it is then empty. */
static void local_remove(const char *dir)
{
    char lst[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char *text;
    char *p;
    char *nl;
    size_t n;
    int k;

    join(lst, sizeof(lst), dir, "XHCIINST.LST");
    text = read_file(lst, NULL);
    if (text == NULL) {
        /* A copy whose list is gone (an earlier run removed the files):
         * the device installer's precompiled copies of this package's
         * INFs may be all that is left. */
        for (k = 0; k < INST_INF_COUNT; k++) {
            char pnf[64];
            inst_copy(pnf, k == INST_INF_CMF ? "nt\\" : "", sizeof(pnf));
            strncat(pnf, instInfFile[k], sizeof(pnf) - strlen(pnf) - 1);
            n = strlen(pnf);
            if (n > 4) {
                strcpy(pnf + n - 4, ".PNF");
            }
            join(path, sizeof(path), dir, pnf);
            SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileA(path)) {
                flagChanged = 1;
            }
        }
        join(path, sizeof(path), dir, "nt");
        if (RemoveDirectoryA(path)) {
            flagChanged = 1;
        }
    }
    if (text != NULL) {
        for (p = text; *p != '\0'; p = nl) {
            nl = strchr(p, '\n');
            if (nl == NULL) {
                nl = p + strlen(p);
            } else {
                *nl++ = '\0';
            }
            if (strlen(p) > 0 && p[strlen(p) - 1] == '\r') {
                p[strlen(p) - 1] = '\0';
            }
            if (p[0] == '\0' || strcmp(p, "END") == 0 ||
                strchr(p, ':') != NULL || strstr(p, "..") != NULL ||
                (strchr(p, '\\') != NULL && !inst_starts(p, "nt\\")) ||
                strchr(p + (inst_starts(p, "nt\\") ? 3 : 0), '\\') != NULL) {
                continue;
            }
            join(path, sizeof(path), dir, p);
            SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileA(path)) {
                flagChanged = 1;
            }
            /* The device installer writes <name>.PNF beside an INF it
             * reads in place (the 98 SE uninstall leg, 2026-10-15): the
             * listed INF's own precompiled copy goes with it. */
            n = strlen(path);
            if (n > 4 && inst_eq(path + n - 4, ".inf")) {
                strcpy(path + n - 4, ".PNF");
                SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileA(path)) {
                    flagChanged = 1;
                }
            }
        }
        free(text);
        if (DeleteFileA(lst)) {
            flagChanged = 1;
        }
        join(path, sizeof(path), dir, "nt");
        if (RemoveDirectoryA(path)) {
            flagChanged = 1;
        }
    }
    if (RemoveDirectoryA(dir)) {
        out("%s removed.\n", dir);
        flagChanged = 1;
    } else if (GetFileAttributesA(dir) != 0xFFFFFFFFUL) {
        out("%s kept: it holds files this tool did not put there.\n", dir);
    }
}

/* The USB tree tool, every local copy, then the work file, then
 * %windir%\XHCI98 if empty. */
static void local_remove_all(void)
{
    char rootDir[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char sub[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    if (!local_dir(rootDir, sizeof(rootDir))) {
        return;
    }
    tray_remove();
    join(pattern, sizeof(pattern), rootDir, "*.*");
    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                join(sub, sizeof(sub), rootDir, fd.cFileName);
                local_remove(sub);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    join(sub, sizeof(sub), rootDir, "RESTAGE.LST");
    if (DeleteFileA(sub)) {
        flagChanged = 1;
    }
    if (RemoveDirectoryA(rootDir)) {
        flagChanged = 1;
        out("%s removed.\n", rootDir);
    } else if (trayDeferred) {
        /* The USB tree tool, still in use, is what keeps it now. */
        if (delete_at_restart(rootDir)) {
            out("%s is removed at the restart.\n", rootDir);
        } else {
            out("%s can be deleted after the restart.\n", rootDir);
        }
    }
}

/* The content tag: FNV-1a of each file's upper-cased name and bytes,
 * combined by XOR so the directory listing's order does not matter. */
/* The files the local copy takes, as names relative to the flavour
 * directory: its own files, and on NT those of nt\ (the filter INF). 98 and
 * ME never get an nt\ file, nor a filter INF wherever it lies, so their
 * hardware wizards cannot offer it for a bridge (39-0.4). Calls each(rel). */
typedef int (*PKG_EACH)(const char *rel, void *ctx);

static int package_files(PKG_EACH each, void *ctx)
{
    char pattern[INST_PATH_CHARS];
    char rel[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        if (pass == 1 && !envNow.Nt) {
            break;
        }
        join(pattern, sizeof(pattern), pkgDir, pass == 0 ? "*.*" : "nt\\*.*");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            /* Only an absent nt\ is "nothing there"; any other failure,
             * and any failure of the flavour directory, is an error. */
            DWORD e = GetLastError();
            /* nt\ may be absent only when the validated package has no
             * INF there (the filter INF's place, 39-0.4). */
            if (pass == 1 &&
                (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) &&
                !(infText[INST_INF_CMF] != NULL &&
                  inst_starts(infRel[INST_INF_CMF], "nt\\"))) {
                continue;
            }
            out("%s could not be listed (error %lu).\n", pattern,
                (unsigned long)e);
            return 0;
        }
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            if (!envNow.Nt && inst_eq(fd.cFileName, "xhcicmf.inf")) {
                continue;
            }
            /* The bundled Stor98 driver is not this tool's to install, and
             * a copy of its INF on DevicePath would install it unasked. */
            if (inst_bundled_file(fd.cFileName)) {
                continue;
            }
            /* A No to the Thunderbolt question: none of its files in the
             * copy, so no wizard and no store offers them (section 8.5). */
            if (!tbInclude && !tbTagAll &&
                (inst_eq(fd.cFileName, "xhcicm.inf") ||
                               inst_eq(fd.cFileName, "xhcicmf.inf") ||
                               inst_eq(fd.cFileName, "xhcicm.sys"))) {
                continue;
            }
            inst_copy(rel, pass == 0 ? "" : "nt\\", sizeof(rel));
            strncat(rel, fd.cFileName, sizeof(rel) - strlen(rel) - 1);
            if (!each(rel, ctx)) {
                FindClose(h);
                return 0;
            }
        } while (FindNextFileA(h, &fd));
        if (GetLastError() != ERROR_NO_MORE_FILES) {
            out("%s could not be listed to its end.\n", pattern);
            FindClose(h);
            return 0;
        }
        FindClose(h);
    }
    return 1;
}

static int tag_one(const char *rel, void *ctx)
{
    INST_U32 *x = (INST_U32 *)ctx;
    char path[INST_PATH_CHARS];
    char name[INST_PATH_CHARS];
    DWORD n;
    char *buf;
    INST_U32 one;
    size_t i;

    inst_copy(name, rel, sizeof(name));
    for (i = 0; name[i] != '\0'; i++) {
        name[i] = (char)inst_upper((unsigned char)name[i]);
    }
    join(path, sizeof(path), pkgDir, rel);
    buf = read_file(path, &n);
    if (buf == NULL) {
        return 0;
    }
    one = inst_fnv1a(INST_FNV_START, (const unsigned char *)name, strlen(name));
    one = inst_fnv1a(one, (const unsigned char *)buf, n);
    free(buf);
    *x ^= one;
    return 1;
}

/* The content tag: FNV-1a of each file's upper-cased relative name and
 * bytes, combined by XOR so the listing's order does not matter. */
static int package_tag(INST_U32 *tag)
{
    INST_U32 x = 0;

    if (!package_files(tag_one, &x)) {
        return 0;
    }
    *tag = x;
    return 1;
}

static void local_copy_name(INST_U32 tag, char *name, size_t cap)
{
    /* Eight hex digits: an 8.3 name, which the 98 SE path box takes as typed
     * (the version is in the INFs; the tag names the content). */
    _snprintf(name, cap - 1, "%08lX", (unsigned long)tag);
    name[cap - 1] = '\0';
}

static int check_one(const char *rel, void *ctx)
{
    char from[INST_PATH_CHARS];
    char to[INST_PATH_CHARS];

    (void)ctx;
    join(from, sizeof(from), pkgDir, rel);
    join(to, sizeof(to), srcDir, rel);
    if (!same_file(from, to)) {
        out("%s differs from this package: refused.\n", to);
        return 0;
    }
    return 1;
}

typedef struct _COPY_CTX {
    FILE *List;
    const char *Dir;
    int Count;
} COPY_CTX;

static int copy_one(const char *rel, void *ctx)
{
    COPY_CTX *c = (COPY_CTX *)ctx;
    char from[INST_PATH_CHARS];
    char to[INST_PATH_CHARS];

    join(from, sizeof(from), pkgDir, rel);
    join(to, sizeof(to), c->Dir, rel);
    if (inst_starts(rel, "nt\\")) {
        char sub[INST_PATH_CHARS];
        join(sub, sizeof(sub), c->Dir, "nt");
        CreateDirectoryA(sub, NULL);
    }
    /* Listed before it is copied, so a cleanup finds every file. */
    if (fprintf(c->List, "%s\n", rel) < 0 || fflush(c->List) != 0) {
        out("The copy's list could not be written.\n");
        return 0;
    }
    if (!CopyFileA(from, to, FALSE)) {
        out("%s could not be copied (error %lu).\n", rel,
            (unsigned long)GetLastError());
        return 0;
    }
    SetFileAttributesA(to, FILE_ATTRIBUTE_NORMAL);
    c->Count++;
    return 1;
}

/* A copy's state: 1 complete (its list ends with the END line
 * make_local_copy writes last), 0 incomplete (no directory, no list, or a
 * list without END - proven, not guessed), -1 unknown (the directory or the
 * list cannot be read): never removed on an unknown. */
static int local_committed(const char *dir)
{
    char lst[INST_PATH_CHARS];
    char *text;
    size_t n;
    int r;
    DWORD e;

    join(lst, sizeof(lst), dir, "XHCIINST.LST");
    if (GetFileAttributesA(lst) == 0xFFFFFFFFUL) {
        e = GetLastError();
        return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? 0 : -1;
    }
    text = read_file(lst, NULL);
    if (text == NULL) {
        return -1;
    }
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) {
        n--;
    }
    text[n] = '\0';
    r = (n == 3 && strcmp(text, "END") == 0) ||
        (n > 3 && strcmp(text + n - 4, "\nEND") == 0);
    free(text);
    return r;
}

/* Record 23 section 4.4: the copy is built in <name>.NEW and published
 * under <name> only once every file and the list are written, so a failed
 * copy never passes for a finished, immutable one; an unfinished one (from
 * an interrupted run, or an earlier build of this tool) is removed and
 * built again. */
static int make_local_copy(void)
{
    char rootDir[INST_PATH_CHARS];
    char name[INST_PATH_CHARS];
    char lst[INST_PATH_CHARS];
    char work[INST_PATH_CHARS];
    COPY_CTX c;
    INST_U32 tag;
    int ok;
    int committed;

    if (!local_dir(rootDir, sizeof(rootDir)) || !package_tag(&tag)) {
        out("The local copy could not be prepared.\n");
        return 0;
    }
    CreateDirectoryA(rootDir, NULL);
    local_copy_name(tag, name, sizeof(name));
    join(srcDir, sizeof(srcDir), rootDir, name);
    committed = local_committed(srcDir);
    if (committed < 0) {
        out("%s cannot be read: refused, nothing changed.\n", srcDir);
        return 0;
    }
    if (committed > 0) {
        /* The same content is already there: never rewritten, only checked. */
        if (!package_files(check_one, NULL)) {
            return 0;
        }
        out("The local copy %s is this package already.\n", srcDir);
        return 1;
    }
    if (GetFileAttributesA(srcDir) != 0xFFFFFFFFUL) {
        out("An unfinished local copy is removed first.\n");
        local_remove(srcDir);
        if (GetFileAttributesA(srcDir) != 0xFFFFFFFFUL) {
            out("%s could not be removed.\n", srcDir);
            return 0;
        }
    }
    inst_copy(work, srcDir, sizeof(work));
    if (strlen(work) + 4 >= sizeof(work)) {
        return 0;
    }
    strcat(work, ".NEW");
    if (GetFileAttributesA(work) != 0xFFFFFFFFUL) {
        local_remove(work);
    }
    CreateDirectoryA(work, NULL);
    join(lst, sizeof(lst), work, "XHCIINST.LST");
    c.List = fopen(lst, "w");
    c.Dir = work;
    c.Count = 0;
    if (c.List == NULL) {
        out("%s could not be written.\n", lst);
        return 0;
    }
    ok = package_files(copy_one, &c);
    if (ok && (fprintf(c.List, "END\n") < 0 || fflush(c.List) != 0)) {
        ok = 0;
    }
    if (fclose(c.List) != 0) {
        ok = 0;
    }
    if (!ok) {
        out("The local copy was not completed; it is removed.\n");
        local_remove(work);
        return 0;
    }
    if (!MoveFileA(work, srcDir)) {
        out("The local copy could not be put in place (error %lu).\n",
            (unsigned long)GetLastError());
        local_remove(work);
        return 0;
    }
    flagChanged = 1;
    copiedNow = 1;
    out("Copied %d files to %s; the install takes them from there.\n", c.Count,
        srcDir);
    return 1;
}
/* The /restage work file (record 23 section 5.7), %windir%\XHCI98\
 * RESTAGE.LST, written before a stored package is removed:
 *   PKG <the local copy's directory name>
 *   INF <slot> <the removed oemNN.inf>
 *   DEV <slot> <instance id>      one per device that named it
 *   NEW <slot> <oemNN.inf>        the replacement, once staged and its stored
 *                                 files checked against this package
 * A run resumes only the same package; the entries go as each device is
 * seen reinstalled from this package. */
#define INST_MAX_RDEV 64
static char restagePkg[INST_PATH_CHARS];
static char restageOem[INST_INF_COUNT][64];
static char restageNew[INST_INF_COUNT][64];
static int restageDevInf[INST_MAX_RDEV];
static char restageDevId[INST_MAX_RDEV][INST_ID_CHARS];
static int restageDevCount;

static void restage_path(char *path, size_t cap)
{
    char rootDir[INST_PATH_CHARS];

    if (!local_dir(rootDir, sizeof(rootDir))) {
        path[0] = '\0';
        return;
    }
    join(path, cap, rootDir, "RESTAGE.LST");
}

/* 1 read (or no file), 0 unreadable or malformed: the run refuses. */
static int restage_read(void)
{
    char path[INST_PATH_CHARS];
    char *text;
    char *p;
    char *nl;
    int k;
    int i;
    int infLines = 0;
    int ended = 0;
    int endInf = -1;
    int endDev = -1;
    int bad = 0;

    memset(restagePending, 0, sizeof(restagePending));
    memset(restageOem, 0, sizeof(restageOem));
    memset(restageNew, 0, sizeof(restageNew));
    restagePkg[0] = '\0';
    restageDevCount = 0;
    restage_path(path, sizeof(path));
    if (path[0] == '\0') {
        out("The Windows directory cannot be read.\n");
        return 0;
    }
    if (GetFileAttributesA(path) == 0xFFFFFFFFUL) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
            return 1;
        }
        out("%s cannot be read (error %lu).\n", path, (unsigned long)e);
        return 0;
    }
    text = read_file(path, NULL);
    if (text == NULL) {
        out("%s cannot be read.\n", path);
        return 0;
    }
    for (p = text; *p != '\0' && !bad; p = nl) {
        nl = strchr(p, '\n');
        if (nl == NULL) {
            nl = p + strlen(p);
        } else {
            *nl++ = '\0';
        }
        if (strlen(p) > 0 && p[strlen(p) - 1] == '\r') {
            p[strlen(p) - 1] = '\0';
        }
        if (p[0] == '\0') {
            continue;
        }
        if (ended) {
            bad = 1;    /* nothing follows END */
        } else if (strncmp(p, "PKG ", 4) == 0 && restagePkg[0] == '\0') {
            inst_copy(restagePkg, p + 4, sizeof(restagePkg));
        } else if (strncmp(p, "INF ", 4) == 0 && p[4] >= '0' &&
                   p[4] < '0' + INST_INF_COUNT && p[5] == ' ' &&
                   p[6] != '\0' && !restagePending[p[4] - '0']) {
            k = p[4] - '0';
            restagePending[k] = 1;
            inst_copy(restageOem[k], p + 6, sizeof(restageOem[k]));
            infLines++;
        } else if (strncmp(p, "NEW ", 4) == 0 && p[4] >= '0' &&
                   p[4] < '0' + INST_INF_COUNT && p[5] == ' ' &&
                   p[6] != '\0' && restagePending[p[4] - '0'] &&
                   restageNew[p[4] - '0'][0] == '\0') {
            inst_copy(restageNew[p[4] - '0'], p + 6, sizeof(restageNew[0]));
        } else if (strncmp(p, "DEV ", 4) == 0 && p[4] >= '0' &&
                   p[4] < '0' + INST_INF_COUNT && p[5] == ' ' &&
                   p[6] != '\0' && restagePending[p[4] - '0'] &&
                   restageDevCount < INST_MAX_RDEV) {
            restageDevInf[restageDevCount] = p[4] - '0';
            inst_copy(restageDevId[restageDevCount], p + 6, INST_ID_CHARS);
            restageDevCount++;
        } else if (strncmp(p, "END ", 4) == 0 &&
                   sscanf(p + 4, "%d %d", &endInf, &endDev) == 2) {
            ended = 1;
        } else {
            bad = 1;
        }
    }
    free(text);
    /* Complete only with its END line, whose counts match what was read:
     * a cut-short file never passes for a smaller piece of work. */
    if (bad || !ended || endInf != infLines || endDev != restageDevCount ||
        restagePkg[0] == '\0') {
        out("%s is not complete, or not in the form this tool writes: "
            "nothing is changed.\n", path);
        restagePkg[0] = '\0';
        restageDevCount = 0;
        memset(restagePending, 0, sizeof(restagePending));
        return 0;
    }
    for (i = 0; i < restageDevCount; i++) {
        if (!restagePending[restageDevInf[i]]) {
            out("%s names a device of no recorded INF.\n", path);
            return 0;
        }
    }
    for (k = 0; k < INST_INF_COUNT; k++) {
        if (restagePending[k]) {
            out("Unfinished /restage work of %s: %s was removed from the driver "
                "store, and its\ndevices are reinstalled from that package.\n",
                restagePkg, restageOem[k]);
        }
    }
    return 1;
}

static int restage_put(char *buf, size_t cap, const char *a, int k,
                       const char *b)
{
    char line[INST_PATH_CHARS + 32];

    if (k >= 0) {
        _snprintf(line, sizeof(line) - 1, "%s %d %s\r\n", a, k, b);
    } else {
        _snprintf(line, sizeof(line) - 1, "%s %s\r\n", a, b);
    }
    line[sizeof(line) - 1] = '\0';
    if (strlen(buf) + strlen(line) >= cap) {
        return 0;
    }
    strcat(buf, line);
    return 1;
}

/* The journal is published whole or not at all: written to RESTAGE.TMP,
 * flushed to the disk, then moved over RESTAGE.LST in one step, so an
 * interrupted write leaves the previous complete journal; its END line
 * carries the counts the reader checks (record 23 section 5.7). */
static int restage_write(void)
{
    static char buf[(INST_MAX_RDEV + 3 * INST_INF_COUNT + 4) *
                    (INST_ID_CHARS + 32)];
    char rootDir[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char tmp[INST_PATH_CHARS];
    char end[32];
    HANDLE h;
    DWORD put;
    int k;
    int i;
    int any = 0;
    int infs = 0;
    int devsN = 0;
    int ok = 1;

    restage_path(path, sizeof(path));
    if (path[0] == '\0') {
        return 0;
    }
    for (k = 0; k < INST_INF_COUNT; k++) {
        any |= restagePending[k];
    }
    if (!any) {
        if (GetFileAttributesA(path) != 0xFFFFFFFFUL && !DeleteFileA(path)) {
            return 0;
        }
        return 1;
    }
    if (!local_dir(rootDir, sizeof(rootDir))) {
        return 0;
    }
    CreateDirectoryA(rootDir, NULL);
    buf[0] = '\0';
    ok &= restage_put(buf, sizeof(buf), "PKG", -1, restagePkg);
    for (k = 0; k < INST_INF_COUNT; k++) {
        if (restagePending[k]) {
            ok &= restage_put(buf, sizeof(buf), "INF", k, restageOem[k]);
            infs++;
            if (restageNew[k][0] != '\0') {
                ok &= restage_put(buf, sizeof(buf), "NEW", k, restageNew[k]);
            }
        }
    }
    for (i = 0; i < restageDevCount; i++) {
        if (restageDevId[i][0] != '\0') {
            ok &= restage_put(buf, sizeof(buf), "DEV", restageDevInf[i],
                              restageDevId[i]);
            devsN++;
        }
    }
    _snprintf(end, sizeof(end) - 1, "%d %d", infs, devsN);
    end[sizeof(end) - 1] = '\0';
    ok &= restage_put(buf, sizeof(buf), "END", -1, end);
    if (!ok) {
        return 0;
    }
    join(tmp, sizeof(tmp), rootDir, "RESTAGE.TMP");
    h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!WriteFile(h, buf, (DWORD)strlen(buf), &put, NULL) ||
        put != (DWORD)strlen(buf) || !FlushFileBuffers(h)) {
        CloseHandle(h);
        DeleteFileA(tmp);
        return 0;
    }
    CloseHandle(h);
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING |
                                MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp);
        return 0;
    }
    return 1;
}

static int restage_listed(const char *id)
{
    int i;

    for (i = 0; i < restageDevCount; i++) {
        if (restageDevId[i][0] != '\0' && inst_eq(restageDevId[i], id)) {
            return 1;
        }
    }
    return 0;
}

/* After the installs: an entry goes when its device is present, no longer
 * names the removed INF, runs the package's version with no problem; an INF
 * goes when none of its entries is left. Entries of absent devices stay. */
static void restage_settle(void)
{
    int i;
    int j;
    int k;
    int left;

    for (i = 0; i < restageDevCount; i++) {
        const DEVREC *d = NULL;
        if (restageDevId[i][0] == '\0') {
            continue;
        }
        for (j = 0; j < devCount; j++) {
            if (inst_eq(devs[j].Id, restageDevId[i])) {
                d = &devs[j];
                break;
            }
        }
        if (d == NULL || !d->Present) {
            out("  %s is not present: its reinstall is still owed (connect it "
                "and run INSTALL.BAT).\n", restageDevId[i]);
            flagSteps = 1;
            continue;
        }
        /* Complete only on the verified replacement, whatever its name - it
         * may reuse the removed one's - and only for a device this run
         * reinstalled with a call that succeeded: an unchanged device can
         * already read the same name and version (record 23 section 5.7). */
        if (was_installed(d->Id) && result_ok(d, restageDevInf[i]) &&
            d->InfPathState > 0 && restageNew[restageDevInf[i]][0] != '\0' &&
            inst_eq(d->InfPath, restageNew[restageDevInf[i]])) {
            restageDevId[i][0] = '\0';
        }
    }
    for (k = 0; k < INST_INF_COUNT; k++) {
        if (!restagePending[k]) {
            continue;
        }
        left = 0;
        for (i = 0; i < restageDevCount; i++) {
            if (restageDevId[i][0] != '\0' && restageDevInf[i] == k) {
                left++;
            }
        }
        if (left == 0) {
            restagePending[k] = 0;
        } else {
            flagSteps = 1;
        }
    }
}

/* /restage's precondition (record 23 section 5.7): every devnode that names
 * the stored INF is present, so each can be moved to the new package now.
 * 0 refuses the replacement of that INF. */
static int restage_movable(int k)
{
    int i;
    int n = 0;

    for (i = 0; i < devCount; i++) {
        if (devs[i].InfPathState < 0) {
            out("/restage: %s's driver key cannot be read: %s is not "
                "replaced.\n", devs[i].Id, trapOem[k]);
            return 0;
        }
        if (devs[i].InfPathState > 0 && inst_eq(devs[i].InfPath, trapOem[k])) {
            if (!devs[i].Present) {
                out("/restage: %s names %s and is not connected: connect it "
                    "first, or it would be left\non a removed package. %s is "
                    "not replaced.\n", devs[i].Id, trapOem[k], trapOem[k]);
                return 0;
            }
            n++;
        }
    }
    return 1;
}

static int restage_record(int k)
{
    int i;
    char *slash = strrchr(srcDir, '\\');

    inst_copy(restagePkg, slash != NULL ? slash + 1 : srcDir, sizeof(restagePkg));
    restagePending[k] = 1;
    inst_copy(restageOem[k], trapOem[k], sizeof(restageOem[k]));
    for (i = 0; i < devCount; i++) {
        if (devs[i].InfPathState > 0 && inst_eq(devs[i].InfPath, trapOem[k]) &&
            !restage_listed(devs[i].Id)) {
            if (restageDevCount >= INST_MAX_RDEV) {
                return 0;
            }
            restageDevInf[restageDevCount] = k;
            inst_copy(restageDevId[restageDevCount], devs[i].Id, INST_ID_CHARS);
            restageDevCount++;
        }
    }
    return restage_write();
}
/* Vista and 7, read only (record 23 section 5.7): an oem*.inf byte-identical
 * to the package's INF whose stored package's files differ from ours. */
static void trap_scan(void)
{
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char store[INST_PATH_CHARS];
    char name[INST_PATH_CHARS];
    char a[INST_PATH_CHARS];
    char b[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    DWORD e;
    char *text;
    char *slash;
    int k;
    int i;
    int all = 0;

    memset(trapOem, 0, sizeof(trapOem));
    memset(trapUnknown, 0, sizeof(trapUnknown));
    if (!envNow.Nt || envNow.Major < 6) {
        return;
    }
    /* "No trap" is a verified answer only: a store that cannot be inspected
     * blocks the INFs it may hold another build of (record 23 section 5.7). */
    if (api.StoreLocation == NULL || GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        out("The driver store cannot be inspected (%s).\n",
            api.StoreLocation == NULL ? "SetupGetInfDriverStoreLocationA is "
                                        "missing" : "no Windows directory");
        all = 1;
    } else {
        join(pattern, sizeof(pattern), win, "inf\\oem*.inf");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND) {
                out("%%windir%%\\inf cannot be listed (error %lu).\n",
                    (unsigned long)e);
                all = 1;
            }
        } else {
            do {
                join(path, sizeof(path), win, "inf");
                join(path, sizeof(path), path, fd.cFileName);
                text = read_file(path, NULL);
                if (text == NULL) {
                    out("%s cannot be read.\n", path);
                    all = 1;
                    continue;
                }
                for (k = 0; k < INST_INF_COUNT; k++) {
                    int differs = 0;
                    int unread = 0;
                    int s;
                    int r;
                    if (infText[k] == NULL || strcmp(text, infText[k]) != 0) {
                        continue;
                    }
                    if (!api.StoreLocation(fd.cFileName, NULL, NULL, store,
                                           sizeof(store), NULL)) {
                        out("The stored package of %s cannot be located "
                            "(0x%08lX).\n", fd.cFileName,
                            (unsigned long)GetLastError());
                        trapUnknown[k] = 1;
                        continue;
                    }
                    slash = strrchr(store, '\\');
                    if (slash != NULL) {
                        *slash = '\0';
                    }
                    for (i = 0; (r = inf_source(k, i, pkgDir, a, sizeof(a), name,
                                                sizeof(name))) > 0; i++) {
                        char rel[INST_PATH_CHARS];
                        inst_inf_source_path(infText[k], i, rel, sizeof(rel));
                        join(b, sizeof(b), store, rel);
                        s = same_file3(a, b);
                        if (s == 0) {
                            logonly("%s differs from %s\n", a, b);
                            differs = 1;
                        } else if (s < 0) {
                            out("%s or %s cannot be read.\n", a, b);
                            unread = 1;
                        }
                    }
                    /* Only a verified difference is the trap; what could
                     * not be read is never replaced (section 5.7). */
                    if (r < 0 || unread) {
                        trapUnknown[k] = 1;
                    } else if (differs) {
                        inst_copy(trapOem[k], fd.cFileName, sizeof(trapOem[k]));
                    }
                }
                free(text);
            } while (FindNextFileA(h, &fd));
            e = GetLastError();
            FindClose(h);
            if (e != ERROR_NO_MORE_FILES) {
                out("%%windir%%\\inf could not be listed to its end.\n");
                all = 1;
            }
        }
    }
    for (k = 0; k < INST_INF_COUNT; k++) {
        if (all && infText[k] != NULL) {
            trapUnknown[k] = 1;
        }
        if (trapUnknown[k]) {
            out("Whether the driver store holds another build of %s cannot be "
                "verified: its\ndevices are left.\n\n", instInfFile[k]);
            continue;
        }
        if (trapOem[k][0] == '\0') {
            continue;
        }
        out("The driver store holds a DIFFERENT build of %s under an identical "
            "INF (%s,\nthe same DriverVer): Windows would install the stored "
            "files, not this package's.\n", instInfFile[k], trapOem[k]);
        if (opts.Restage) {
            out("/restage: that package is replaced, and every device of %s "
                "reinstalled.\n\n", instInfFile[k]);
        } else {
            out("Its devices are left; run INSTALL.BAT /restage to replace the "
                "stored package.\n\n");
        }
    }
}

/* Vista and 7: 1 when the stored package of oemName holds exactly this
 * package's files, 0 when not, -1 when it cannot be asked. */
static int store_matches(int k, const char *oemName)
{
    char store[INST_PATH_CHARS];
    char name[INST_PATH_CHARS];
    char a[INST_PATH_CHARS];
    char b[INST_PATH_CHARS];
    char *slash;
    int i;
    int r;
    int s;

    if (api.StoreLocation == NULL ||
        !api.StoreLocation(oemName, NULL, NULL, store, sizeof(store), NULL)) {
        return -1;
    }
    slash = strrchr(store, '\\');
    if (slash != NULL) {
        *slash = '\0';
    }
    for (i = 0; (r = inf_source(k, i, pkgDir, a, sizeof(a), name,
                                sizeof(name))) > 0; i++) {
        char rel[INST_PATH_CHARS];
        inst_inf_source_path(infText[k], i, rel, sizeof(rel));
        join(b, sizeof(b), store, rel);
        s = same_file3(a, b);
        if (s <= 0) {
            return s;
        }
    }
    return r < 0 ? -1 : 1;
}

/* The name of an %windir%\inf\oem*.inf byte-identical to INF k, or 0. */
static int identical_oem(int k, char *name, size_t cap)
{
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char *text;
    int found = 0;

    name[0] = '\0';
    if (infText[k] == NULL || GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    join(pattern, sizeof(pattern), win, "inf\\oem*.inf");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        join(path, sizeof(path), win, "inf");
        join(path, sizeof(path), path, fd.cFileName);
        text = read_file(path, NULL);
        if (text != NULL && strcmp(text, infText[k]) == 0) {
            inst_copy(name, fd.cFileName, cap);
            found = 1;
        }
        free(text);
    } while (!found && FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

static int stage_nt(void)
{
    char before[INST_PATH_CHARS];
    int had;
    int k;
    char media[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char dest[INST_PATH_CHARS];
    char *component;

    if (api.CopyOem == NULL) {
        out("SetupCopyOEMInfA is missing: nothing can be staged.\n");
        return 0;
    }
    for (k = 0; k < INST_INF_COUNT; k++) {
        if (infText[k] == NULL) {
            continue;
        }
        if (groupVerdict[group_of_inf(k)] != INST_GROUP_OK) {
            out("%s is not staged: its driver's installed version is newer or "
                "unknown.\n", instInfFile[k]);
            continue;
        }
        if (trapUnknown[k]) {
            infTrap[k] = 1;
            flagSteps = 1;
            continue;
        }
        if (trapOem[k][0] != '\0') {
            if (!opts.Restage || !restageOk[k]) {
                infTrap[k] = 1;
                flagSteps = 1;
                continue;
            }
            /* The reinstall owed is recorded before the stored package goes,
             * so an interrupted run resumes it (record 23 section 5.7). */
            if (!restage_record(k)) {
                out("/restage: the work file could not be written; nothing "
                    "removed.\n");
                return 0;
            }
            if (api.UninstallOem == NULL ||
                !api.UninstallOem(trapOem[k], INST_SUOI_FORCEDELETE, NULL)) {
                out("/restage: the stored %s could not be removed (error "
                    "0x%08lX).\n", trapOem[k], (unsigned long)GetLastError());
                return 0;
            }
            out("/restage: the stored %s was removed.\n", trapOem[k]);
            flagChanged = 1;
        }
        join(path, sizeof(path), srcDir, infRel[k]);
        dest[0] = '\0';
        component = NULL;
        inst_copy(media, path, sizeof(media));
        if (strrchr(media, '\\') != NULL) {
            *strrchr(media, '\\') = '\0';
        }
        /* Already staged, byte for byte, from this same local copy: the
         * call reuses it, and that is no change (an unchanged rerun says
         * "No changes were made."). */
        had = identical_oem(k, before, sizeof(before));
        logonly("SetupCopyOEMInf(\"%s\", \"%s\", SPOST_PATH, 0)\n", path, media);
        if (!api.CopyOem(path, media, SPOST_PATH, 0, dest, sizeof(dest), NULL,
                         &component)) {
            out("%s could not be staged (error 0x%08lX).%s\n", instInfFile[k],
                (unsigned long)GetLastError(),
                trapOem[k][0] ? " Run INSTALL.BAT again: it stages from the "
                                "same local copy." : "");
            return 0;
        }
        if (k == INST_INF_CM) {
            tbStaged = 1;
        }
        if (inst_staged_again(had, copiedNow, before,
                              component ? component : dest)) {
            out("%s is staged already, as %s.\n", instInfFile[k], before);
        } else {
            out("Staged %s as %s.\n", instInfFile[k],
                component ? component : dest);
            flagChanged = 1;
        }
        if (restagePending[k]) {
            const char *nm = component ? component : dest;
            if (store_matches(k, nm) != 1) {
                out("/restage: the staged %s does not hold this package's files; "
                    "the reinstall is not\nstarted. Run INSTALL.BAT /restage "
                    "again.\n", nm);
                return 0;
            }
            inst_copy(restageNew[k], nm, sizeof(restageNew[k]));
            if (!restage_write()) {
                out("/restage: the work file could not be updated.\n");
                return 0;
            }
        }
    }
    out("\n");
    return 1;
}

/* 1 when every present device carrying hwid is one this step installs. */
static int hwid_clear(const char *hwid, int inf)
{
    int i;

    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (!d->Present) {
            continue;
        }
        if (!inst_msz_has(d->Hw, d->HwBytes, hwid) &&
            !inst_msz_has(d->Compat, d->CompatBytes, hwid)) {
            continue;
        }
        /* The ports of one Thunderbolt switch share one id: a healthy port
         * that already carries this release's filter and needs nothing is
         * no other owner's; the call giving the filter to its siblings may
         * reach it again with the same driver. */
        if (idle_filtered_port(d, inf)) {
            continue;
        }
        if (d->Inf != inf || !d->Runs) {
            out("  %s is shared with %s (%s), which this run leaves;\n"
                "  neither is changed.%s\n", hwid, d->Id,
                inst_action_text(d->Action),
                (d->Inf == inf && (d->Action == INST_ACT_CURRENT ||
                                   d->Action == INST_ACT_NEWER)) ?
                " INSTALL.BAT /force includes it." :
                " Update them in Device Manager, one by one.");
            return 0;
        }
    }
    return 1;
}

/* A tunnel port that already runs this release's filter, healthy, and that
 * this run does not reinstall: it does not block a call that reaches its
 * siblings, and it is never counted as installed by that call. */
static int idle_filtered_port(const DEVREC *d, int inf)
{
    return inf == INST_INF_CMF && d->Inf == inf &&
           d->Action == INST_ACT_CURRENT && !d->Runs && result_ok(d, inf);
}

/* The entry of a device in the reached list, added when new; -1 when the
 * list is full (it holds as many entries as the scan admits devices). */
static int note_changed(const DEVREC *d, int state)
{
    int c;

    for (c = 0; c < changedCount; c++) {
        if (inst_eq(changedId[c], d->Id)) {
            if (state) {
                changedState[c] = 1;
                changedInf[c] = d->Inf;
            }
            return c;
        }
    }
    if (changedCount >= INST_MAX_CHANGED) {
        return -1;
    }
    inst_copy(changedId[changedCount], d->Id, INST_ID_CHARS);
    changedInf[changedCount] = d->Inf;
    changedState[changedCount] = state;
    changedReboot[changedCount] = 0;
    changedBusy[changedCount] = 0;
    return changedCount++;
}

/* The flags of the install call for a device of INF inf: forced, except
 * the tunnel-port filter's first install on Windows 2000, unforced - the
 * route task 39-0.4 read there (2000 takes the unsigned filter INF over
 * machine.inf by itself; XP onward rank it below and need the force;
 * record 23 section 5.5, owner 2026-10-15). */
static DWORD call_flags(const DEVREC *x, int inf)
{
    return (inf == INST_INF_CMF && x->Action == INST_ACT_FILTER &&
            envNow.Major == 5 && envNow.Minor == 0) ?
           0 : INST_INSTALLFLAG_FORCE;
}

/* Install or update the planned devices of one INF; controllers selects the
 * xhci98 controllers (1), the rest of xhci98's devices (0), or every device
 * (-1). Returns 0 when a failure leaves the order meaningless. */
static int install_inf(int inf, int controllers)
{
    static int callIdx[INST_MAX_DEVS];
    static int busyTries[INST_MAX_DEVS];
    char path[INST_PATH_CHARS];
    char hwid[INST_ID_CHARS];
    int i;
    int j;
    int n;
    int full;
    BOOL reboot;
    DWORD err;
    DWORD flags;
    int mixed;
    int again;
    int ok = 1;

    join(path, sizeof(path), srcDir, infRel[inf]);
    for (i = 0; i < devCount; i++) {
        DEVREC *d = &devs[i];
        if (!d->Present || d->Inf != inf || !d->Runs || d->Done) {
            continue;
        }
        if (controllers == 1 && !is_controller(d)) {
            continue;
        }
        if (controllers == 0 && is_controller(d)) {
            continue;
        }
        if (infTrap[inf]) {
            out("Left (the driver store holds another build, or it cannot be "
                "inspected): %s\n", d->Desc);
            d->Done = 1;
            note_changed(d, 0);
            continue;
        }
        inst_copy(hwid, first_hw(d), sizeof(hwid));
        if (!hwid_clear(hwid, inf)) {
            d->Done = 1;
            note_changed(d, 0);
            flagSteps = 1;
            continue;
        }
        if (api.UpdateDriver == NULL) {
            out("UpdateDriverForPlugAndPlayDevicesA is missing.\n");
            flagFailed = 1;
            return 0;
        }
        /* One call reaches every device carrying the id, with one set of
         * flags: a group whose devices need different flags (on 2000 a
         * port's first filter install, unforced, beside a filter update,
         * forced) is left, and said (record 23 section 5.5). */
        flags = call_flags(d, inf);
        mixed = 0;
        for (j = 0; j < devCount; j++) {
            if (devs[j].Present && devs[j].Inf == inf &&
                (inst_msz_has(devs[j].Hw, devs[j].HwBytes, hwid) ||
                 inst_msz_has(devs[j].Compat, devs[j].CompatBytes, hwid)) &&
                !idle_filtered_port(&devs[j], inf) &&
                call_flags(&devs[j], inf) != flags) {
                mixed = 1;
            }
        }
        if (mixed) {
            out("Left: %s - devices sharing it need a first filter install "
                "and an update at once,\n  which one call cannot do on "
                "Windows 2000. In Device Manager install the filter on\n"
                "  EVERY port with this id - those without it too - "
                "(Update Driver, Have Disk,\n  the package's nt\\ "
                "directory), then run INSTALL.BAT again.\n", hwid);
            for (j = 0; j < devCount; j++) {
                if (devs[j].Present && devs[j].Inf == inf &&
                    (inst_msz_has(devs[j].Hw, devs[j].HwBytes, hwid) ||
                     inst_msz_has(devs[j].Compat, devs[j].CompatBytes, hwid))) {
                    devs[j].Done = 1;
                    note_changed(&devs[j], 0);
                }
            }
            flagSteps = 1;
            continue;
        }
        /* A device of the id whose busy tries are spent - an earlier
         * member - blocks the call before anything is noted or called, so
         * a member that joined later cannot buy it another try. */
        n = 0;
        for (j = 0; j < devCount; j++) {
            if (devs[j].Present && devs[j].Inf == inf &&
                (inst_msz_has(devs[j].Hw, devs[j].HwBytes, hwid) ||
                 inst_msz_has(devs[j].Compat, devs[j].CompatBytes, hwid))) {
                int c = reached(devs[j].Id);
                busyTries[n++] = c >= 0 ? changedBusy[c] : 0;
            }
        }
        if (inst_busy_spent(busyTries, n)) {
            out("Left: %s - Windows was installing a device of this id each "
                "time the tool\n  tried (%d times). Run INSTALL.BAT again once "
                "Windows has finished.\n", hwid, INST_BUSY_TRIES);
            for (j = 0; j < devCount; j++) {
                if (devs[j].Present && devs[j].Inf == inf &&
                    (inst_msz_has(devs[j].Hw, devs[j].HwBytes, hwid) ||
                     inst_msz_has(devs[j].Compat, devs[j].CompatBytes, hwid))) {
                    devs[j].Done = 1;
                }
            }
            flagFailed = 1;
            continue;
        }
        /* Each device the call reaches is tracked for the results check,
         * or the call is not made. */
        n = 0;
        full = 0;
        for (j = 0; j < devCount; j++) {
            if (devs[j].Present && devs[j].Inf == inf &&
                (inst_msz_has(devs[j].Hw, devs[j].HwBytes, hwid) ||
                 inst_msz_has(devs[j].Compat, devs[j].CompatBytes, hwid))) {
                int c;
                if (flags == 0 && idle_filtered_port(&devs[j], inf)) {
                    /* An unforced call may leave it as it is: reached, but
                     * no evidence of an install. A forced call reinstalls
                     * it, so it stays a target and is checked. */
                    devs[j].Done = 1;
                    note_changed(&devs[j], 0);
                    continue;
                }
                c = note_changed(&devs[j], 1);
                devs[j].Done = 1;
                if (c < 0) {
                    full = 1;
                } else {
                    callIdx[n++] = c;
                }
            }
        }
        if (full) {
            out("Too many devices to follow: %s is not changed.\n", hwid);
            flagFailed = 1;
            continue;
        }
        out("%s %s ... ", d->Action == INST_ACT_INSTALL ? "Installing" :
            d->Action == INST_ACT_FILTER ? "Adding the filter to" : "Updating",
            d->Desc[0] ? d->Desc : d->Id);
        reboot = FALSE;
        logonly("\nUpdateDriverForPlugAndPlayDevices(\"%s\", \"%s\", %s)\n",
                hwid, path, flags ? "FORCE" : "0");
        if (api.UpdateDriver(NULL, hwid, path, flags, &reboot)) {
            out("done%s.\n", reboot ? ", restart needed" : "");
            flagChanged = 1;
            if (flags == 0) {
                /* Only the filter's first install on Windows 2000 goes
                 * unforced (call_flags): the case read on the 2000 leg. */
                tunnelChanged = 1;
            }
            for (j = 0; j < n; j++) {
                changedState[callIdx[j]] = 2;   /* the call succeeded */
                if (reboot) {
                    changedReboot[callIdx[j]] = 1;
                }
            }
            if (reboot) {
                flagRestart = 1;
            }
            continue;
        }
        err = GetLastError();
        /* Windows' own installer had the device at that moment (2000 leg,
         * 2026-10-15: the new root hub, 0x20, while its Found New Hardware
         * ran): the call is made again after the next barrier's settle, if
         * the device still needs it then - never over Windows' install. */
        again = 0;
        if (err == ERROR_SHARING_VIOLATION && n > 0) {
            /* One call reaches the whole id group: it is made again only
             * while every device it reaches has tries left. */
            again = 1;
            for (j = 0; j < n; j++) {
                if (!inst_busy_retry(err, ++changedBusy[callIdx[j]])) {
                    again = 0;
                }
            }
        }
        for (j = 0; j < n; j++) {
            changedState[callIdx[j]] = again ? INST_CS_BUSY : 3;
        }
        if (again) {
            out("busy (error 0x%08lX).\n  Windows is installing it at the "
                "same moment. The tool lets Windows finish,\n  reads the "
                "devices again and, if it still needs its driver, tries "
                "again.\n", (unsigned long)err);
            continue;
        }
        out("FAILED (error 0x%08lX).\n", (unsigned long)err);
        if (flags == 0 && err == ERROR_NO_MORE_ITEMS) {
            out("  Windows declined the filter: it ranks its own driver for "
                "the port higher, and\n  on Windows 2000 the filter's first "
                "install is never forced (/force does not\n  change that). "
                "The port keeps Windows' driver without the filter; Device\n"
                "  Manager's Have Disk on the package's nt\\ directory "
                "installs it by hand.\n");
        } else if (err == ERROR_IN_WOW64) {
            out("  A 32-bit program cannot install drivers on 64-bit Windows; "
                "run %s.\n", INST_OTHER_NAME);
        } else if (err == ERROR_CANCELLED) {
            out("  The installation was cancelled (the signature question "
                "answered no?).\n");
        }
        flagFailed = 1;
        if (controllers == 1) {
            ok = 0;
            break;
        }
    }
    return ok;
}

static int root_hub_below(const char *ctrlId);

static INST_U32 sig_text(INST_U32 h, const char *s)
{
    return inst_fnv1a(h, (const unsigned char *)s, strlen(s) + 1);
}

static INST_U32 sig_number(INST_U32 h, INST_U32 v)
{
    unsigned char b[4];

    b[0] = (unsigned char)(v & 0xFFUL);
    b[1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[3] = (unsigned char)((v >> 24) & 0xFFUL);
    return inst_fnv1a(h, b, sizeof(b));
}

/* The configuration manager's tree routines for inst_tree_sig. */
static int tree_answer(DWORD cr)
{
    return cr == INST_CR_SUCCESS ? INST_TREE_OK :
           (cr == INST_CR_NO_SUCH_DEVINST ? INST_TREE_NONE : INST_TREE_ERROR);
}

static int tree_child(void *ctx, INST_U32 dn, INST_U32 *child)
{
    DWORD c;
    int r = tree_answer(api.Child(&c, (DWORD)dn, 0));

    (void)ctx;
    *child = (INST_U32)c;
    return r;
}

static int tree_sibling(void *ctx, INST_U32 dn, INST_U32 *sibling)
{
    DWORD c;
    int r = tree_answer(api.Sibling(&c, (DWORD)dn, 0));

    (void)ctx;
    *sibling = (INST_U32)c;
    return r;
}

static int tree_id(void *ctx, INST_U32 dn, char *id, size_t cap)
{
    (void)ctx;
    return api.DeviceId((DWORD)dn, id, (ULONG)cap, 0) == INST_CR_SUCCESS ?
           INST_TREE_OK : INST_TREE_ERROR;
}

static int tree_status(void *ctx, INST_U32 dn, INST_U32 *status,
                       INST_U32 *problem)
{
    ULONG s = 0;
    ULONG p = 0;

    (void)ctx;
    if (api.Status(&s, &p, (DWORD)dn, 0) != INST_CR_SUCCESS) {
        return INST_TREE_ERROR;
    }
    *status = (INST_U32)s;
    *problem = (INST_U32)p;
    return INST_TREE_OK;
}

/* The signature of the trees below this package's controllers, whether it
 * was read whole, and whether a device is expected: a controller this run
 * updated is started but shows no root hub yet, or a device of the bus
 * waits for its install (inst_tree_sig). */
static int settle_signature(INST_U32 *sig, int *expect)
{
    INST_TREE_OPS ops;
    INST_U32 h = INST_FNV_START;
    DWORD dn;
    ULONG status;
    ULONG problem;
    char id[INST_ID_CHARS];
    int nodes = 0;
    int whole = 1;
    int below;
    int i;

    ops.Ctx = NULL;
    ops.Child = tree_child;
    ops.Sibling = tree_sibling;
    ops.Id = tree_id;
    ops.Status = tree_status;
    *expect = 0;
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (!d->Present || d->Inf != INST_INF_XHCI || !is_controller(d)) {
            continue;
        }
        inst_copy(id, d->Id, sizeof(id));
        h = sig_text(h, id);
        if (api.Locate(&dn, id, 0) != INST_CR_SUCCESS) {
            /* Gone for the moment (restarting): a reading of its own. */
            h = sig_text(h, "gone");
            continue;
        }
        if (api.Status(&status, &problem, dn, 0) != INST_CR_SUCCESS) {
            whole = 0;
            continue;
        }
        problem = (ULONG)inst_dn_problem((INST_U32)status, (INST_U32)problem);
        h = sig_number(h, status & (INST_DN_STARTED | INST_DN_HAS_PROBLEM));
        h = sig_number(h, problem);
        if (was_installed(d->Id) && (status & INST_DN_STARTED) && problem == 0) {
            below = root_hub_below(d->Id);
            if (below < 0) {
                whole = 0;
            } else if (below == 0) {
                *expect = 1;
            }
        }
        if (!inst_tree_sig(&ops, (INST_U32)dn, 0, &h, expect, &nodes)) {
            whole = 0;
        }
    }
    *sig = h;
    return whole;
}

/* Wait until Windows' own installer has nothing pending and the devices
 * below the controllers stand still (record 23 section 5.8). 2 settled
 * after something happened (pending seen, devices moved), 1 settled with
 * nothing happening; 0 still pending at the limit; -1 the question failed;
 * -2 not pending, but the devices never stood still (or could not be read
 * whole) up to the limit. */
static int settle(INST_U32 limitMs, INST_U32 tellMs, INST_U32 expectMs,
                  int first)
{
    INST_SETTLE s;
    DWORD t0;
    DWORD w;
    INST_U32 sig;
    int whole;
    int expect;
    int r;
    int told = 0;

    if (api.WaitInstall == NULL) {
        return 1;
    }
    if (api.Locate == NULL || api.Child == NULL || api.Sibling == NULL ||
        api.DeviceId == NULL || api.Status == NULL) {
        /* No tree to read: the pending question alone, as before. */
        w = api.WaitInstall(limitMs);
        return w == WAIT_OBJECT_0 ? 1 : (w == WAIT_TIMEOUT ? 0 : -1);
    }
    inst_settle_init(&s, limitMs, tellMs, expectMs);
    t0 = GetTickCount();
    for (;;) {
        w = api.WaitInstall(0);
        whole = settle_signature(&sig, &expect);
        /* GetTickCount wraps after 49.7 days; the unsigned difference
         * stays right across one wrap, and a settle lasts minutes. */
        r = inst_settle_step(&s, (INST_U32)(GetTickCount() - t0),
                             w == WAIT_OBJECT_0 ? 0 :
                             (w == WAIT_TIMEOUT ? 1 : -1), sig, whole, expect);
        if (r == INST_SETTLE_DONE) {
            if (told) {
                out("Windows has finished; going on.\n");
            }
            logonly("[settled after %lu ms%s]\n",
                    (unsigned long)(GetTickCount() - t0),
                    s.Moved ? ", devices moved" : "");
            return s.Moved ? 2 : 1;
        }
        if (r == INST_SETTLE_TIMEOUT) {
            return 0;
        }
        if (r == INST_SETTLE_FAILED) {
            return -1;
        }
        if (r == INST_SETTLE_UNSTEADY) {
            return -2;
        }
        if (r == INST_SETTLE_TELL) {
            told = 1;
            if (first) {
                out("\nWindows is installing something; waiting up to a minute "
                    "for it to finish ...\n");
            } else {
                out("\nWindows is installing a device itself, usually with a "
                    "Found New Hardware\nwindow - it may be behind this one. "
                    "Answer it: the driver is staged, so let it\nsearch, "
                    "answer Yes or Continue to its questions, then Finish. "
                    "This tool waits\n(up to 10 minutes) and goes on by itself; "
                    "it never installs over Windows' own\ninstall.\n");
            }
        }
        Sleep(INST_SETTLE_POLL_MS);
    }
}

/* What a settle that did not end settled says. */
static void settle_failed(int r, const char *bound)
{
    if (r == -2) {
        out("the devices kept changing, or could not be read whole, for %s.\n",
            bound);
        out("\nRestart Windows, then run INSTALL.BAT again: it goes on from "
            "what it finds.\n");
        flagSteps = 1;
        return;
    }
    out(r == 0 ? "still busy after %s.\n" : "the wait failed.\n", bound);
    out("\nWindows is still installing something - usually a Found New "
        "Hardware window\nwaiting for an answer. Finish or cancel those "
        "windows, then run INSTALL.BAT\nagain: it goes on from here. (The "
        "tool does not install under a wizard.) If no such\nwindow is open "
        "(Windows 2000 keeps a cancelled one pending until the next\nstart), "
        "restart Windows first.\n");
    flagSteps = 1;
}

/* The barrier's wait: 1 settled; 0 stops the run (exit 4). */
static int wait_installs(void)
{
    int r;

    out("Waiting for Windows to finish installing what appeared ... ");
    r = settle(INST_INSTALL_WAIT_MS, 20000UL, 20000UL, 0);
    if (r > 0) {
        out("done.\n");
        return 1;
    }
    settle_failed(r, "10 minutes");
    return 0;
}

/* 1 when the INF a driver key names (in %windir%\inf) is this package's, 0
 * when it is not, -1 when it cannot be read. */
static int inf_path_is_ours(const char *infPath)
{
    char win[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char *text;
    int r;

    if (infPath[0] == '\0' || GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return -1;
    }
    join(path, sizeof(path), win, "inf");
    join(path, sizeof(path), path, infPath);
    text = read_file(path, NULL);
    if (text == NULL) {
        return -1;
    }
    r = inst_inf_is_ours(text);
    free(text);
    return r;
}

/* The device's entry in the reached list, or -1. */
static int reached(const char *id)
{
    int c;

    for (c = 0; c < changedCount; c++) {
        if (inst_eq(changedId[c], id)) {
            return c;
        }
    }
    return -1;
}

/* 1 when an install call was made for the device in this run. */
static int was_changed(const char *id)
{
    int c = reached(id);

    return c >= 0 && changedState[c] != 0;
}

/* Record 23 section 5.9's postcondition for a device installed from INF
 * inf: bound (the filter: pci with xhcicm above it and our INF in its
 * driver key), at the package version, started, no problem. The results
 * check and the /restage settlement share it. */
static int result_ok(const DEVREC *d, int inf)
{
    return (inf == INST_INF_CMF ?
            (inst_eq(d->Service, "pci") &&
             inst_msz_has(d->Upper, d->UpperBytes, "xhcicm") &&
             d->InfPathState > 0 && inf_path_is_ours(d->InfPath) == 1) :
            inst_eq(d->Service, instInfService[inf])) &&
           d->VersionState == INST_VS_HAVE &&
           inst_ver_cmp(d->Version, pkgVersion) == 0 &&
           (d->Status & INST_DN_STARTED) != 0 && d->Problem == 0;
}

/* 1 when an install call for the device succeeded in this run. */
static int was_installed(const char *id)
{
    int c = reached(id);

    return c >= 0 && changedState[c] == 2;
}

/* NT: whether a root hub of this driver is a present child of the
 * controller: 1 yes, 0 no, -1 the tree cannot be read. */
static int root_hub_below(const char *ctrlId)
{
    DWORD dn;
    DWORD child;
    DWORD cr;
    ULONG status;
    ULONG problem;
    char id[INST_ID_CHARS];
    char want[INST_ID_CHARS];

    if (api.Locate == NULL || api.Child == NULL || api.Sibling == NULL ||
        api.DeviceId == NULL || api.Status == NULL) {
        return -1;
    }
    inst_copy(want, ctrlId, sizeof(want));
    if (api.Locate(&dn, want, 0) != INST_CR_SUCCESS) {
        return -1;
    }
    cr = api.Child(&child, dn, 0);
    if (cr == INST_CR_NO_SUCH_DEVINST) {
        return 0;
    }
    if (cr != INST_CR_SUCCESS) {
        return -1;
    }
    for (;;) {
        if (api.DeviceId(child, id, sizeof(id), 0) != INST_CR_SUCCESS) {
            return -1;
        }
        if (inst_bus_id(id) == 1 && inst_starts(id, "USB\\XHCI98_ROOT_HUB") &&
            api.Status(&status, &problem, child, 0) == INST_CR_SUCCESS) {
            return 1;
        }
        cr = api.Sibling(&child, child, 0);
        if (cr == INST_CR_NO_SUCH_DEVINST) {
            return 0;
        }
        if (cr != INST_CR_SUCCESS) {
            return -1;
        }
    }
}

/* Read on the 2000 leg (2026-10-15): after a port's first filter install
 * the upstream port showed Code 12 and the switch's other devices were gone
 * until the restart, all started after it. The owner (2026-10-15): the
 * filter goes on both ports and the run asks for that restart; Code 12 or
 * a device gone there is the restart's to clear (record 23 section 5.9). */
static void tunnel_restart_hint(void)
{
    out("  On Windows 2000 a tunnel port's first filter install shows Code 12 "
        "on the\n  upstream port, and takes the Thunderbolt switch's devices "
        "away, until a\n  restart. Restart Windows, then run INSTALL.BAT again: "
        "it reads them again.\n");
    flagRestart = 1;
}

/* Record 23 section 5.9: each changed device bound, at the package version,
 * started, with no problem; otherwise the reason decides the flag. The
 * restart exceptions are each device's own: the restart its install call
 * answered, never another device's. */
static void check_results(void)
{
    int c;
    int i;
    char state[160];
    int win2000 = envNow.Nt && envNow.Major == 5 && envNow.Minor == 0;

    out("\nAfterwards:\n");
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (!d->Present || d->Inf < 0) {
            continue;
        }
        inst_state_text(state, sizeof(state), d->Service, d->VersionText,
                        (d->Status & INST_DN_STARTED) != 0, d->Problem);
        out("  %s: %s\n", d->Desc[0] ? d->Desc : d->Id, state);
    }
    for (c = 0; c < changedCount; c++) {
        const DEVREC *d = NULL;
        int ok;
        int ctrl;
        if (!changedState[c]) {
            continue;
        }
        for (i = 0; i < devCount; i++) {
            if (inst_eq(devs[i].Id, changedId[c])) {
                d = &devs[i];
                break;
            }
        }
        if (d == NULL || !d->Present) {
            if (inst_tunnel_restart(win2000, tunnelChanged, changedInf[c], 1,
                                    0)) {
                out("  %s: gone until the restart.\n", changedId[c]);
                tunnelHint = 1;
                continue;
            }
            out("  %s: gone after its install.\n", changedId[c]);
            flagFailed = 1;
            tunnelHint |= tunnelChanged &&
                (changedInf[c] == INST_INF_CM || changedInf[c] == INST_INF_CMF);
            continue;
        }
        ctrl = changedInf[c] == INST_INF_XHCI && is_controller(d);
        ok = result_ok(d, changedInf[c]);
        if (ok) {
            if (ctrl && !changedReboot[c]) {
                /* Section 5.8: a started controller shows its root hub. */
                int r = root_hub_below(d->Id);
                if (r == 0) {
                    out("  %s started, but no root hub of this driver is "
                        "present below it.\n", d->Desc);
                    flagFailed = 1;
                } else if (r < 0) {
                    out("  Whether %s shows its root hub cannot be read.\n",
                        d->Desc);
                    flagFailed = 1;
                }
            }
            continue;
        }
        if (d->Problem == INST_PROB_NEED_RESTART) {
            flagRestart = 1;
        } else if (envNow.Major == 6 && INST_MY_ARCH == INST_ARCH_X86 && ctrl &&
                   (d->Problem == 28UL || d->Problem == 1UL || d->Problem == 0) &&
                   changedReboot[c]) {
            /* 32-bit Vista and 7 hold the controller until the helper's
             * restart (release notes, "Installing", step 3). */
        } else if (envNow.Major == 6 && INST_MY_ARCH == INST_ARCH_AMD64 &&
                   (d->Problem == 52UL || d->Problem == 39UL) &&
                   !testSigningConfirmed) {
            out("  %s does not load: test-signing mode is needed (see the "
                "note above).\n", d->Desc);
            flagSteps = 1;
        } else if (changedReboot[c] && d->Problem == 0) {
            /* The old release runs until the restart its install asked for. */
        } else if (inst_tunnel_restart(win2000, tunnelChanged, changedInf[c],
                                       0, d->Problem)) {
            out("  %s: Code 12 until the restart.\n", d->Desc[0] ? d->Desc :
                d->Id);
            tunnelHint = 1;
        } else {
            inst_state_text(state, sizeof(state), d->Service, d->VersionText,
                            (d->Status & INST_DN_STARTED) != 0, d->Problem);
            out("  %s did not end as installed: %s.\n", d->Desc[0] ? d->Desc :
                d->Id, state);
            flagFailed = 1;
            tunnelHint |= tunnelChanged &&
                (changedInf[c] == INST_INF_CM || changedInf[c] == INST_INF_CMF);
        }
    }
    if (tunnelHint) {
        tunnel_restart_hint();
    }
    /* The devices this run did not change are read too: a current device
     * already in trouble is not a success to report. */
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (!d->Present || d->Inf < 0 || d->Problem == 0 || was_changed(d->Id) ||
            group_of(d) < 0) {
            continue;
        }
        if (d->Problem == 22UL) {
            out("  %s is disabled in Device Manager.\n", d->Desc);
        } else if (inst_tunnel_restart(win2000, tunnelChanged, d->Inf, 0,
                                       d->Problem)) {
            out("  %s: Code 12 until the restart.\n", d->Desc);
            if (!tunnelHint) {
                tunnel_restart_hint();
                tunnelHint = 1;
            }
        } else {
            out("  %s already had problem code %lu, and still has.\n",
                d->Desc, (unsigned long)d->Problem);
            flagSteps = 1;
        }
    }
}

static int old_bus_devnodes(void)
{
    int i;
    int n = 0;

    for (i = 0; i < devCount; i++) {
        if (inst_bus_id(devs[i].Id) == 2) {
            n++;
        }
    }
    return n;
}

/* Record 23 section 5.11. */
static void note_old_entries(void)
{
    int n = old_bus_devnodes();

    /* Owner, 2026-10-15: the installer never runs XHCICLN; it only says
     * the separate tool exists (record 23 section 5.11). */
    if (n == 0) {
        return;
    }
    out("\n%d old XHCI98\\ entries from 2.3.x remain; harmless - "
        "tools\\xhcicln\\ removes them if you want.\n", n);
}

static void testsigning_note(void)
{
    char opt[512];

    testSigningConfirmed = 0;
    if (INST_MY_ARCH != INST_ARCH_AMD64 || envNow.Major < 6) {
        return;
    }
    if (reg_text(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control",
                 "SystemStartOptions", opt, sizeof(opt), NULL)) {
        logonly("SystemStartOptions: %s\n", opt);
        if (inst_testsigning(opt)) {
            testSigningConfirmed = 1;
        }
    }
    if (pkgSigned < 0) {
        testSigningConfirmed = 0;
        out("NOTE: whether the x64 drivers carry a signature could not be read. "
            "Test-signed\nones load in test-signing mode (bcdedit /set "
            "testsigning on, then restart);\nunsigned ones only on a start "
            "where F8 chose \"Disable Driver Signature\nEnforcement\". "
            "Otherwise the controller shows Code 52 on 7, Code 39 on Vista\n"
            "(the package's readme.txt, \"WINDOWS VISTA X64 AND WINDOWS 7 X64: "
            "TEST-SIGNING\nMODE\"). If this start is an F8 start, go on.\n\n");
        return;
    }
    if (pkgSigned == 0) {
        /* A build without the project's key (a clone): nothing signed. */
        testSigningConfirmed = 0;
        out("NOTE: the x64 drivers in this package carry no signature (built "
            "without the\nproject's test-signing key). Vista x64 and 7 x64 "
            "load them only on a start where\nF8 chose \"Disable Driver "
            "Signature Enforcement\"; test-signing mode is not\nenough. "
            "Otherwise the controller shows Code 52 on 7, Code 39 on Vista. If "
            "this\nstart is such an F8 start, go on.\n\n");
        return;
    }
    if (testSigningConfirmed) {
        out("Test-signing mode is on: the test-signed x64 drivers load.\n\n");
        return;
    }
    out("NOTE: test-signing mode could not be confirmed. The x64 drivers are "
        "test-signed:\nthey load in test-signing mode (an elevated Command "
        "Prompt:  bcdedit /set\ntestsigning on  then restart), or on a start "
        "where F8 chose \"Disable Driver\nSignature Enforcement\". Otherwise the "
        "controller shows Code 52 on 7, Code 39 on\nVista. This tool does not "
        "change it (the package's readme.txt, \"WINDOWS VISTA\nX64 AND "
        "WINDOWS 7 X64: TEST-SIGNING MODE\"). If this start is an F8 start, "
        "go on.\n\n");
}

static int confirm(const char *what)
{
    char line[16];

    if (opts.Yes) {
        out("%s - going on (/yes).\n\n", what);
        return 1;
    }
    out("%s? [Y/N]: ", what);
    if (!read_line(line, sizeof(line))) {
        out("\nNo answer: cancelled, nothing changed.\n");
        return 0;
    }
    if ((line[0] == 'y' || line[0] == 'Y') && line[1] == '\0') {
        out("\n");
        return 1;
    }
    out("Cancelled, nothing changed.\n");
    return 0;
}

/* Record 23 section 5.1: no installation in progress before the first
 * change. 1 clear and nothing happened while waiting; 2 clear after
 * Windows did something (the devices are to be read again); 0 stop
 * (pending, unsteady, or the answer unknown). */
static int nothing_pending(void)
{
    int r;

    if (api.WaitInstall == NULL) {
        out("Whether Windows is installing something cannot be asked "
            "(CMP_WaitNoPendingInstallEvents is missing): nothing is changed.\n");
        flagSteps = 1;
        return 0;
    }
    /* Windows' own installs are let finish first, for a minute at most. */
    r = settle(INST_FIRST_WAIT_MS, 5000UL, 0UL, 1);
    if (r > 0) {
        return r;
    }
    if (r == -2) {
        out("The devices kept changing, or could not be read whole, for a "
            "minute. Restart\nWindows, then run INSTALL.BAT again. Nothing was "
            "changed.\n");
        flagSteps = 1;
        return 0;
    }
    out("Windows is %s - usually a Found New Hardware window\nwaiting for an "
        "answer. Finish or cancel it, then run INSTALL.BAT again.\nIf no such "
        "window is open (Windows 2000 keeps a cancelled one pending until\n"
        "the next start), restart Windows first. Nothing was changed.\n",
        r == 0 ? "still installing something after a minute" :
                 "perhaps installing something (the question failed)");
    flagSteps = 1;
    return 0;
}

/* A refused group leaves work undone whether or not one of its devices is
 * present (record 23 sections 5.4 and 7.4): every plan, the first and each
 * barrier's, sets the steps flag for it, and a refusal first seen after a
 * barrier - a member newly exposed or connected - is reported with the
 * member responsible. */
static int refusalSeen[INST_GROUPS];

static void note_refusals(int first)
{
    int g;
    int i;

    for (g = 0; g < INST_GROUPS; g++) {
        if (groupVerdict[g] == INST_GROUP_OK) {
            continue;
        }
        flagSteps = 1;
        if (refusalSeen[g]) {
            continue;
        }
        refusalSeen[g] = 1;
        if (first) {
            continue;   /* print_plan has said it */
        }
        out("%s.sys is now LEFT: %s.\n", groupService[g],
            groupVerdict[g] == INST_GROUP_REFUSED_UNKNOWN ?
            "a version of this driver could not be read" :
            "a newer release of this driver is installed");
        for (i = 0; i < devCount; i++) {
            const DEVREC *d = &devs[i];
            if (group_of(d) == g &&
                (d->VersionState == INST_VS_UNREADABLE ||
                 (d->VersionState == INST_VS_HAVE &&
                  inst_ver_cmp(d->Version, pkgVersion) > 0))) {
                out("  %s %s%s\n", d->Id, d->VersionText,
                    d->Present ? "" : " (not present)");
            }
        }
    }
}

/* The barrier between dependent phases (record 23 sections 5.1 and 5.8):
 * nothing pending, then the devices read and planned again. 0 stops. */
static int barrier(void)
{
    if (!wait_installs()) {
        return 0;
    }
    if (!scan()) {
        flagFailed = 1;
        return 0;
    }
    plan();
    note_refusals(0);
    return 1;
}

/* What the confirmed plan rests on (record 23 section 5.1), read after
 * plan(): each present device of this package with its driver, version,
 * problem, filters, action and run decision, and every device - present or
 * not - whose driver key names an INF the trap holds (/restage's
 * precondition). */
static INST_U32 devs_signature(void)
{
    INST_U32 h = INST_FNV_START;
    int i;
    int k;

    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        int trapRef = 0;
        for (k = 0; k < INST_INF_COUNT; k++) {
            if (trapOem[k][0] != '\0' && d->InfPathState > 0 &&
                inst_eq(d->InfPath, trapOem[k])) {
                trapRef = 1;
            }
        }
        if (trapRef || d->InfPathState < 0) {
            h = sig_text(h, d->Id);
            h = sig_number(h, (INST_U32)d->Present);
            h = sig_number(h, (INST_U32)(d->InfPathState + 1));
            h = sig_text(h, d->InfPath);
        }
        if (!d->Present || d->Inf < 0) {
            continue;
        }
        h = sig_text(h, d->Id);
        h = sig_text(h, d->Service);
        h = sig_text(h, d->VersionText);
        h = sig_number(h, (INST_U32)d->Problem);
        h = sig_number(h, (INST_U32)d->Action);
        h = sig_number(h, (INST_U32)d->Runs);
        h = inst_fnv1a(h, (const unsigned char *)d->Upper, d->UpperBytes);
    }
    return h;
}

/* After the first wait - every time, since Windows may have changed a
 * device while the question was open - the devices, the driver store's
 * trap and /restage's precondition are read again, and a plan they no
 * longer match is not carried out. 1 go on; 0 stop, nothing changed. */
static int plan_still_holds(INST_U32 before)
{
    static char oemBefore[INST_INF_COUNT][INST_PATH_CHARS];
    int unknownBefore[INST_INF_COUNT];
    int k;
    int same = 1;

    memcpy(oemBefore, trapOem, sizeof(oemBefore));
    memcpy(unknownBefore, trapUnknown, sizeof(unknownBefore));
    if (!scan()) {
        out("The devices could not be read again: nothing was changed.\n");
        flagFailed = 1;
        return 0;
    }
    logonly("[the devices read again after the first wait]\n");
    outQuiet = 1;   /* the plan's text was shown; the repeat goes to the log */
    trap_scan();
    for (k = 0; k < INST_INF_COUNT; k++) {
        if (!inst_eq(oemBefore[k], trapOem[k]) ||
            unknownBefore[k] != trapUnknown[k]) {
            same = 0;
        }
        /* /restage: a store the plan was to replace must still be movable;
         * one that was not stays left. */
        if (restageOk[k] && !restage_movable(k)) {
            same = 0;
        }
    }
    outQuiet = 0;
    plan();
    if (!same || devs_signature() != before) {
        out("Windows changed the devices while the tool waited, so the plan "
            "shown is out of\ndate. Nothing was changed: run INSTALL.BAT again "
            "to see the new plan.\n");
        flagSteps = 1;
        return 0;
    }
    return 1;
}

static int any_runs(int inf, int controllers)
{
    int i;

    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (d->Present && d->Inf == inf && d->Runs && !d->Done &&
            !infTrap[inf] && (controllers < 0 || is_controller(d) == controllers)) {
            return 1;
        }
    }
    return 0;
}

static int run_install_nt(void)
{
    int k;
    int runs = 0;
    int i;
    int pass;
    INST_U32 before;
    INST_U32 tag;
    char name[INST_PATH_CHARS];

    if (!scan()) {
        return INST_EXIT_REFUSED;
    }
    tb_question();
    if (!restage_read()) {
        out("REFUSED: the /restage work file is unreadable; nothing is "
            "changed.\n");
        return INST_EXIT_REFUSED;
    }
    if (restagePkg[0] != '\0') {
        if (!package_tag(&tag)) {
            return INST_EXIT_REFUSED;
        }
        local_copy_name(tag, name, sizeof(name));
        if (!inst_eq(name, restagePkg)) {
            /* The same package copied with the other Thunderbolt answer
             * (section 8.5) is the same package. */
            tbTagAll = 1;
            if (package_tag(&tag)) {
                local_copy_name(tag, name, sizeof(name));
            }
            tbTagAll = 0;
            tbInclude = !tbInclude;
            if (!inst_eq(name, restagePkg) && package_tag(&tag)) {
                local_copy_name(tag, name, sizeof(name));
            }
            tbInclude = !tbInclude;
        }
        if (!inst_eq(name, restagePkg)) {
            out("REFUSED: an unfinished /restage of another package (%s) is "
                "recorded. Run that\npackage's INSTALL.BAT to finish it; this "
                "one is %s.\n", restagePkg, name);
            return INST_EXIT_REFUSED;
        }
        if (haveCm && !tbInclude &&
            (restagePending[INST_INF_CM] || restagePending[INST_INF_CMF])) {
            out("REFUSED: the unfinished /restage includes the Thunderbolt "
                "driver. Answer Y to the\nThunderbolt question (or use "
                "/thunderbolt) to finish it.\n");
            return INST_EXIT_REFUSED;
        }
    }
    trap_scan();
    memset(restageOk, 0, sizeof(restageOk));
    for (k = 0; k < INST_INF_COUNT; k++) {
        if (opts.Restage && trapOem[k][0] != '\0') {
            restageOk[k] = restage_movable(k);
        }
    }
    plan();
    print_plan();
    for (i = 0; i < devCount; i++) {
        runs += devs[i].Runs;
    }
    memset(refusalSeen, 0, sizeof(refusalSeen));
    note_refusals(1);
    testsigning_note();
    ask_values();
    if (opts.List) {
        tray_install();
        out("/list: nothing was changed.\n");
        return INST_EXIT_DONE;
    }
    input_warning();
    if (!confirm(inst_confirm_text(runs, values_to_write(), 1, 0))) {
        return INST_EXIT_REFUSED;
    }
    before = devs_signature();
    if (!nothing_pending() || !plan_still_holds(before)) {
        return inst_exit_code(0, flagFailed, flagRestart, flagSteps);
    }
    if (!make_local_copy() || !stage_nt()) {
        flagFailed = 1;
        return inst_exit_code(0, 1, flagRestart, flagSteps);
    }
    /* The controllers; one that met Windows' own install (busy) is tried
     * again after a barrier, as section 5.8's devices are. */
    for (pass = 0; pass < INST_BUSY_TRIES; pass++) {
        if (pass > 0 && !barrier()) {
            goto stopped;
        }
        if (!any_runs(INST_INF_XHCI, 1)) {
            break;
        }
        if (!install_inf(INST_INF_XHCI, 1)) {
            out("A controller's update failed: the rest is not attempted.\n");
            return inst_exit_code(0, 1, flagRestart, flagSteps);
        }
    }
    /* After a 2.3.x update the root hub and the hubs are new devnodes; an
     * installed root hub or hub shows the hubs behind it; a UAS disk or an
     * NHI may show children too. Each phase waits and reads again. */
    for (k = 0; k < INST_INF_COUNT; k++) {
        if (infText[k] == NULL) {
            continue;
        }
        for (pass = 0; pass < 4; pass++) {
            if (!barrier()) {
                goto stopped;
            }
            if (!any_runs(k, k == INST_INF_XHCI ? 0 : -1)) {
                break;
            }
            install_inf(k, k == INST_INF_XHCI ? 0 : -1);
        }
    }
    if (!barrier()) {
        goto stopped;
    }
    for (i = 0; i < devCount; i++) {
        if (devs[i].Present && devs[i].Inf >= 0 && devs[i].Runs &&
            !devs[i].Done && !devs[i].Refused && !infTrap[devs[i].Inf]) {
            out("Not finished: %s (%s) still needs its driver.\n",
                devs[i].Desc[0] ? devs[i].Desc : devs[i].Id,
                inst_action_text(devs[i].Action));
            flagSteps = 1;
            pass = -1;
        }
    }
    if (pass == -1) {
        out("Run INSTALL.BAT again to go on.\n");
    }
    write_values();
    check_results();
    restage_settle();
    if (!restage_write()) {
        out("The /restage work file could not be updated.\n");
        flagFailed = 1;
    }
    tray_install();
    if (flagRestart) {
        /* The second sentence only where it can apply (the 2000 leg read
         * it after a settings-only run). */
        out("\nRESTART Windows to finish.%s\n",
            (envNow.Major == 6 && INST_MY_ARCH == INST_ARCH_X86) ?
            " If the controller was updated from 2.3.x on 32-bit\nVista or "
            "7, run INSTALL.BAT again after the restart for the root hub and "
            "hubs." : "");
    }
    tb_later_note(srcDir);
    note_old_entries();
    return inst_exit_code(0, flagFailed, flagRestart, flagSteps);

stopped:
    if (scan()) {
        plan();
        check_results();
    }
    out("\nStopped before every device was done: run INSTALL.BAT again to go "
        "on.\n");
    return inst_exit_code(0, flagFailed, flagRestart, flagSteps);
}
/* ======================================================================== */
/* 98 SE and ME (record 23 section 6)                                        */
/* ======================================================================== */

#define INST_REG_CV "Software\\Microsoft\\Windows\\CurrentVersion"

/* DevicePath (record 23 section 6.2): every entry under %windir%\XHCI98\ -
 * an earlier copy - taken out, then addDir appended when it is not NULL. No
 * other entry is touched, and the value keeps its type. */
static int devicepath_update(const char *addDir)
{
    char list[2048];
    char mid[2048];
    char newList[2048];
    char rootDir[INST_PATH_CHARS];
    char prefix[INST_PATH_CHARS];
    DWORD type = REG_SZ;
    HKEY k;
    int r;
    LONG w;

    if (!local_dir(rootDir, sizeof(rootDir)) ||
        !reg_text(HKEY_LOCAL_MACHINE, INST_REG_CV, "DevicePath", list,
                  sizeof(list), &type)) {
        out("DevicePath could not be read.\n");
        return 0;
    }
    logonly("DevicePath was: %s\n", list);
    join(prefix, sizeof(prefix), rootDir, "");
    if (inst_path_remove_prefix(list, prefix, mid, sizeof(mid)) < 0) {
        out("DevicePath is too long to change.\n");
        return 0;
    }
    if (addDir != NULL) {
        r = inst_path_append(mid, addDir, newList, sizeof(newList));
        if (r < 0) {
            out("DevicePath is too long to change.\n");
            return 0;
        }
    } else {
        inst_copy(newList, mid, sizeof(newList));
    }
    if (strcmp(newList, list) == 0) {
        out("DevicePath needs no change.\n");
        return 1;
    }
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_REG_CV, 0, KEY_SET_VALUE, &k)
        != ERROR_SUCCESS) {
        out("DevicePath could not be opened for writing.\n");
        return 0;
    }
    w = RegSetValueExA(k, "DevicePath", 0, type, (const BYTE *)newList,
                       (DWORD)strlen(newList) + 1);
    RegCloseKey(k);
    if (w != ERROR_SUCCESS) {
        out("DevicePath could not be written (error %ld).\n", (long)w);
        return 0;
    }
    logonly("DevicePath now: %s\n", newList);
    out("DevicePath now reads: %s\n", newList);
    flagChanged = 1;
    return 1;
}
static void steps_9x(const char *dir)
{
    int i;
    int any = 0;
    int old23;
    INST_U32 bv[4];
    char bin[INST_PATH_CHARS];
    static const INST_U32 v24[4] = { 2, 4, 0, 0 };

    /* The 2.3.x keyboard-and-mouse advice: 98 SE only, and only over an
     * installed xhci98.sys older than 2.4.0.0. */
    group_binary(0, bin, sizeof(bin));
    old23 = envNow.Minor == 10 && file_version(bin, bv) > 0 &&
            inst_ver_cmp(bv, v24) < 0;

    out("\nWhat to do now:\n");
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        int fix;
        if (!d->Present || d->Inf < 0) {
            continue;
        }
        fix = uas_fix(d);
        /* The controller and, for an update, its root hub: the release
         * notes' update is Update Driver on both (the 9x leg, 2026-10-15:
         * the root hub otherwise stays at the old release). Hubs and
         * composite entries follow by themselves. */
        if (d->Inf == INST_INF_XHCI && !is_controller(d) &&
            !(d->Action == INST_ACT_UPDATE && inst_bus_id(d->Id) == 1 &&
              inst_starts(d->Id, "USB\\XHCI98_ROOT_HUB"))) {
            continue;
        }
        any = 1;
        out("\n  %s (%s)\n", d->Desc, d->Id);
        if (d->RegOnly) {
            out("  - Recorded, but not running now: if Device Manager lists it under "
                "\"Other devices\",\n    install it as below; if it does not, it "
                "is an old entry (the device moved)\n    and needs nothing. After "
                "a cancelled New Hardware wizard, restart: the\n    wizard comes "
                "back and finds the driver by itself.\n");
        }
        if (d->Refused) {
            out("  - Left: its version could not be compared.\n");
            print_uas_fix(fix, "  - ");
            continue;
        }
        switch (d->Action) {
        case INST_ACT_INSTALL:
            out("  - Device Manager: this device (under \"Other devices\" if "
                "it has no driver),\n    Properties, Driver, Update Driver, "
                "Next, \"Search for the best driver\", Next\n    with NO box "
                "ticked, Next, Finish. If it finds nothing: \"Specify a\n    "
                "location\" and type %s.\n"
                "  - When Windows asks for the \"xhci98 xHCI Host Controller Disk\", "
                "type\n    %s under \"Copy files from\" and OK. For its own "
                "files (usbd.sys) it asks\n    for the Windows CD.\n"
                "  - Restart when asked; on ME restart anyway.\n", dir, dir);
            break;
        case INST_ACT_UPDATE:
            if (d->Inf == INST_INF_UAS) {
                out("  - Device Manager: this disk, Properties, Driver, Update "
                    "Driver, Next, \"Display\n    a list of all the drivers in "
                    "a specific location\", Have Disk, type %s.\n    (A search "
                    "keeps an xhciuas.sys of the same DriverVer.)\n"
                    "  - When Windows asks for the \"xhci98 xHCI Host Controller "
                    "Disk\", type the same\n    folder under \"Copy files "
                    "from\".\n", dir);
                break;
            }
            out("  - Device Manager: this device, Properties, Driver, Update "
                "Driver, Next,\n    \"Search for a better driver\", Next with "
                "NO box ticked, Next, Finish; if it\n    keeps the old one, "
                "\"Specify a location\" and type %s.\n  - When Windows asks for "
                "the \"xhci98 xHCI Host Controller Disk\", type the same\n    "
                "folder under \"Copy files from\".\n  - Restart afterwards, "
                "although Windows does not ask.\n", dir);
            if (envNow.Minor == 90 && !is_controller(d)) {
                out("  - On ME a \"Select Other Driver\" list may name this "
                    "driver twice (one copy\n    under INF\\OTHER): either "
                    "is the same driver.\n");
            }
            if (d->Inf == INST_INF_XHCI && is_controller(d) && old23) {
                out("  - From 2.3.x or earlier: have a keyboard and mouse that "
                    "do not go through this\n    driver first (release notes, "
                    "\"From 2.3.x or earlier\").\n");
            }
            break;
        case INST_ACT_NEWER:
            out("  - A newer release of this driver is installed; nothing to "
                "do.\n");
            break;
        case INST_ACT_CURRENT:
            if (opts.Force) {
                out("  - /force: Device Manager, this device, Properties, "
                    "Driver, Update Driver, Next,\n    \"Display a list of all "
                    "the drivers in a specific location\", Have Disk, type\n"
                    "    %s. (A search keeps a driver of the same DriverVer.)\n",
                    dir);
            } else if (fix == INST_UASFIX_NONE) {
                out("  - Nothing: this release runs it.\n");
            }
            break;
        default:
            out("  - Another driver runs it; this tool leaves it.\n");
            break;
        }
        print_uas_fix(fix, "  - ");
    }
    if (!any) {
        out("  No device of this package is present.\n");
    }
    /* The Thunderbolt half only when this run copied its INF (section 8.5). */
    out("\nA UAS disk%s found later finds its driver in %s too.\nOn Windows "
        "98 SE any disk, UAS included, needs NUSB's mass-storage component\n"
        "(README, \"Installation Steps\"). Run INSTALL.BAT again after the "
        "restart to see\nthe result and set the driver's options.\n",
        (haveCm && tbInclude) ? " or a Thunderbolt controller" : "", dir);
    if (envNow.Minor == 90 && !ntmap_present()) {
        out("On this Windows ME, plug in an ordinary USB stick once before the "
            "first UAS\ndisk: ME copies its own USB storage files "
            "(USBNTMAP.SYS) only then, and a\nUAS disk plugged in first shows "
            "Code 2 until it is unplugged and plugged\nback in.\n");
    }
}

static int run_install_9x(void)
{
    char dir[INST_PATH_CHARS];
    int i;
    int runs;
    int steps;

    if (!scan()) {
        out("The device tree could not be read whole: nothing is changed.\n");
        return INST_EXIT_REFUSED;
    }
    tb_question();
    plan();
    print_plan();
    if (!local_dir(dir, sizeof(dir))) {
        return INST_EXIT_REFUSED;
    }
    out("Windows 98 SE and ME have no routine that installs a driver on a "
        "device.\nThis tool copies the package to %s and adds that directory "
        "to\nDevicePath, the list of places the hardware wizards search by "
        "themselves.\n\n", dir);
    ask_values();
    if (opts.List) {
        tray_install();
        out("/list: nothing was changed.\n");
        return INST_EXIT_DONE;
    }
    for (i = 0; i < devCount; i++) {
        INST_U32 bv[4];
        char bin[INST_PATH_CHARS];
        static const INST_U32 v24[4] = { 2, 4, 0, 0 };
        /* Only an update from 2.3.x or earlier, and only on 98 SE (the
         * installed binary's version tells; 98 keeps no DriverVersion). */
        group_binary(0, bin, sizeof(bin));
        if (envNow.Minor == 10 && devs[i].Present &&
            devs[i].Inf == INST_INF_XHCI &&
            is_controller(&devs[i]) && devs[i].Action == INST_ACT_UPDATE &&
            file_version(bin, bv) > 0 && inst_ver_cmp(bv, v24) < 0) {
            out("BEFORE THE UPDATE: from 2.3.x or earlier on 98 SE the USB "
                "keyboard and mouse\nbehind this controller do not answer the "
                "update wizard's last page or the\nwizards after the restart; "
                "have a keyboard and mouse that do not go through\nthis "
                "driver (PS/2, a laptop's built-in ones).\n\n");
            break;
        }
    }
    runs = 0;
    steps = 0;
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (d->Present && d->Inf >= 0 && (d->Runs || d->Refused)) {
            steps = 1;
            if (!old_slot_suspect(d)) {
                runs = 1;
            }
        }
    }
    if (!confirm(inst_confirm_text(runs, values_to_write(), 0,
                                   steps && !runs))) {
        return INST_EXIT_REFUSED;
    }
    if (!make_local_copy() || !devicepath_update(srcDir)) {
        return inst_exit_code(0, 1, 0, 1);
    }
    write_values();
    tray_install();
    steps_9x(srcDir);
    tb_later_note(srcDir);
    note_old_entries();
    /* Steps are left only where a device needs one (Update Driver, Have
     * Disk, a refused version, an entry that may be a cancelled wizard's
     * controller), or the USB tree tool does (record 23 section 9.5): a
     * current machine ends without them. */
    return inst_exit_code(0, flagFailed, flagRestart, steps || flagSteps);
}

/* ======================================================================== */
/* Uninstall (record 23 section 9)                                           */
/* ======================================================================== */

static int warn_uninstall(void)
{
    char line[32];

    out("*** This removes the xhci98 drivers from Windows. ***\n"
        "On a machine whose only USB controller is xHCI, the USB keyboard and "
        "mouse\nstop working when the controller's driver is removed, and stay "
        "dead until\nanother driver is installed. Have a PS/2 keyboard, a "
        "laptop's built-in one,\nor a USB controller another driver runs, "
        "before you go on.\n");
    if (opts.List) {
        out("(/list: nothing will be changed.)\n\n");
        return 1;
    }
    if (opts.Yes) {
        out("/yes given: going on without the typed word.\n\n");
        return 1;
    }
    out("Type REMOVE and press Enter to go on; anything else cancels: ");
    if (!read_line(line, sizeof(line))) {
        out("\nNo answer: refused. (/yes is the way for a script.)\n");
        return 0;
    }
    if (strcmp(line, "REMOVE") != 0) {
        out("Cancelled, nothing changed.\n");
        return 0;
    }
    out("\n");
    return 1;
}

/* The devinfo element of an instance id in an all-devnode set. */
static int find_in_set(HDEVINFO set, const char *id, SP_DEVINFO_DATA *data)
{
    DWORD i;
    char got[INST_ID_CHARS];

    for (i = 0;; i++) {
        data->cbSize = sizeof(*data);
        if (!api.EnumInfo(set, i, data)) {
            return 0;
        }
        if (api.GetInstanceId(set, data, got, sizeof(got), NULL) &&
            inst_eq(got, id)) {
            return 1;
        }
    }
}

/* A tunnel port this package's filter is on, by its UpperFilters or by its
 * driver key naming this package's filter INF (record 23 section 9.2). */
static int is_filter_host(const DEVREC *d)
{
    if (inst_own_service(d->Service)) {
        return 0;
    }
    if (inst_msz_has(d->Upper, d->UpperBytes, "xhcicm")) {
        return 1;
    }
    return inst_eq(d->Service, "pci") && d->InfPathState > 0 &&
           inf_path_is_ours(d->InfPath) == 1;
}

/* 1 when the devnode is in the uninstall's set (record 23 section 9.2). */
static int in_remove_set(const DEVREC *d)
{
    return inst_remove_rank(d->Id, d->Service, 0) >= 0;
}

/* The per-device installation route (SetupDiBuildDriverInfoList,
 * SetupDiSetSelectedDriver, DIF_INSTALLDEVICE), looked up at run time. */
typedef BOOL (WINAPI *PFN_BUILDDRV)(HDEVINFO, PSP_DEVINFO_DATA, DWORD);
typedef BOOL (WINAPI *PFN_ENUMDRV)(HDEVINFO, PSP_DEVINFO_DATA, DWORD, DWORD,
                                   PSP_DRVINFO_DATA_A);
typedef BOOL (WINAPI *PFN_DRVDETAIL)(HDEVINFO, PSP_DEVINFO_DATA,
                                     PSP_DRVINFO_DATA_A,
                                     PSP_DRVINFO_DETAIL_DATA_A, DWORD, PDWORD);
typedef BOOL (WINAPI *PFN_DRVPARAMS)(HDEVINFO, PSP_DEVINFO_DATA,
                                     PSP_DRVINFO_DATA_A, PSP_DRVINSTALL_PARAMS);
typedef BOOL (WINAPI *PFN_SETSELDRV)(HDEVINFO, PSP_DEVINFO_DATA,
                                     PSP_DRVINFO_DATA_A);
typedef BOOL (WINAPI *PFN_DESTROYDRV)(HDEVINFO, PSP_DEVINFO_DATA, DWORD);
typedef BOOL (WINAPI *PFN_SETDIPARAMS)(HDEVINFO, PSP_DEVINFO_DATA,
                                       PSP_DEVINSTALL_PARAMS_A);
#define INST_DI_FLAGSEX_ALLOWEXCLUDEDDRVS 0x00000800UL

/* Give a present port back to the best-ranked driver node that is not this
 * package's (Windows' own machine.inf, or a vendor's), per device. */
static int reinstall_without_ours(HDEVINFO set, PSP_DEVINFO_DATA data)
{
    PFN_BUILDDRV build = (PFN_BUILDDRV)proc(api.SetupApi, "setupapi", "SetupDiBuildDriverInfoList");
    PFN_ENUMDRV enumDrv = (PFN_ENUMDRV)proc(api.SetupApi, "setupapi", "SetupDiEnumDriverInfoA");
    PFN_DRVDETAIL detail = (PFN_DRVDETAIL)proc(api.SetupApi, "setupapi", "SetupDiGetDriverInfoDetailA");
    PFN_DRVPARAMS params = (PFN_DRVPARAMS)proc(api.SetupApi, "setupapi", "SetupDiGetDriverInstallParamsA");
    PFN_SETSELDRV select = (PFN_SETSELDRV)proc(api.SetupApi, "setupapi", "SetupDiSetSelectedDriverA");
    PFN_DESTROYDRV destroy = (PFN_DESTROYDRV)proc(api.SetupApi, "setupapi", "SetupDiDestroyDriverInfoList");
    SP_DRVINFO_DATA_A drv;
    SP_DRVINFO_DATA_A best;
    SP_DRVINSTALL_PARAMS dp;
    SP_DEVINSTALL_PARAMS_A ip;
    unsigned char detailBuf[4096];
    PSP_DRVINFO_DETAIL_DATA_A det = (PSP_DRVINFO_DETAIL_DATA_A)detailBuf;
    DWORD bestRank = 0xFFFFFFFFUL;
    DWORD i;
    int found = 0;
    char *text;

    PFN_SETDIPARAMS setDi = (PFN_SETDIPARAMS)proc(api.SetupApi, "setupapi", "SetupDiSetDeviceInstallParamsA");

    if (build == NULL || enumDrv == NULL || detail == NULL || params == NULL ||
        select == NULL || destroy == NULL || setDi == NULL) {
        out("the per-device installation routines are missing.\n");
        return 0;
    }
    /* Windows' own bridge node may be an excluded driver: let the list carry
     * it (DI_FLAGSEX_ALLOWEXCLUDEDDRVS, Microsoft's driver-list remarks). */
    memset(&ip, 0, sizeof(ip));
    ip.cbSize = sizeof(ip);
    if (!api.GetDiParams(set, data, &ip)) {
        out("its install parameters cannot be read.\n");
        return 0;
    }
    ip.FlagsEx |= INST_DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
    if (!setDi(set, data, &ip)) {
        out("its install parameters cannot be set.\n");
        return 0;
    }
    if (!build(set, data, SPDIT_COMPATDRIVER)) {
        out("no driver list (error 0x%08lX).\n", (unsigned long)GetLastError());
        return 0;
    }
    for (i = 0;; i++) {
        memset(&drv, 0, sizeof(drv));
        drv.cbSize = sizeof(drv);
        if (!enumDrv(set, data, SPDIT_COMPATDRIVER, i, &drv)) {
            break;
        }
        memset(detailBuf, 0, sizeof(detailBuf));
        det->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_A);
        if (!detail(set, data, &drv, det, sizeof(detailBuf), NULL)) {
            continue;
        }
        text = read_file(det->InfFileName, NULL);
        if (text == NULL) {
            continue;
        }
        if (inst_inf_is_ours(text)) {
            free(text);
            continue;
        }
        free(text);
        memset(&dp, 0, sizeof(dp));
        dp.cbSize = sizeof(dp);
        if (!params(set, data, &drv, &dp)) {
            continue;
        }
        if (!found || dp.Rank < bestRank) {
            found = 1;
            bestRank = dp.Rank;
            best = drv;
            logonly("  candidate %s rank 0x%08lX\n", det->InfFileName,
                    (unsigned long)dp.Rank);
        }
    }
    if (!found) {
        destroy(set, data, SPDIT_COMPATDRIVER);
        out("no driver of Windows' own or a vendor's matches it.\n");
        return 0;
    }
    if (!select(set, data, &best) ||
        !api.CallCi(DIF_INSTALLDEVICE, set, data)) {
        out("FAILED (error 0x%08lX).\n", (unsigned long)GetLastError());
        destroy(set, data, SPDIT_COMPATDRIVER);
        return 0;
    }
    memset(&ip, 0, sizeof(ip));
    ip.cbSize = sizeof(ip);
    if (!api.GetDiParams(set, data, &ip)) {
        out("done, but whether it needs a restart cannot be read.\n");
        destroy(set, data, SPDIT_COMPATDRIVER);
        return 0;
    }
    if ((ip.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0) {
        out("done, restart needed.\n");
        flagRestart = 1;
    } else {
        out("done.\n");
    }
    destroy(set, data, SPDIT_COMPATDRIVER);
    return 1;
}

/* The port's state after it was given back (record 23 section 9.2 step 1):
 * the same function driver, the other filters kept, no INF of ours. */
static int unfilter_verify(HDEVINFO set, PSP_DEVINFO_DATA data,
                           const DEVREC *d, const char *msz, size_t mszBytes)
{
    char svc[64];
    char upper[512];
    DWORD upperBytes;
    char drv[128];
    char path[INST_PATH_CHARS];
    char infPath[64];
    int r;

    if (enum_value(d->Id, "Service", REG_SZ, svc, sizeof(svc), NULL) < 0 ||
        enum_value(d->Id, "UpperFilters", REG_MULTI_SZ, upper, sizeof(upper),
                   &upperBytes) < 0 ||
        enum_value(d->Id, "Driver", REG_SZ, drv, sizeof(drv), NULL) <= 0) {
        out("  the port cannot be read back.\n");
        return 0;
    }
    if (!inst_eq(svc, d->Service)) {
        out("  the port's function driver is now %s, not %s.\n", svc, d->Service);
        return 0;
    }
    if (inst_msz_has(upper, upperBytes, "xhcicm") ||
        (mszBytes > 0 && (upperBytes < mszBytes || memcmp(upper, msz, mszBytes) != 0)) ||
        (mszBytes == 0 && upper[0] != '\0')) {
        out("  its filters changed with the reinstall: set back to the others.\n");
        if (!api.SetRegProp(set, data, SPDRP_UPPERFILTERS,
                            mszBytes ? (const BYTE *)msz : NULL, (DWORD)mszBytes)) {
            out("  FAILED (error 0x%08lX).\n", (unsigned long)GetLastError());
            return 0;
        }
        flagRestart = 1;
    }
    join(path, sizeof(path), "SYSTEM\\CurrentControlSet\\Control\\Class", drv);
    r = reg_text3(HKEY_LOCAL_MACHINE, path, "InfPath", infPath, sizeof(infPath));
    /* A readable InfPath that is not this package's: absence proves nothing
     * (record 23 section 9.2 step 1). */
    if (r <= 0 || inf_path_is_ours(infPath) != 0) {
        out("  its driver key still names this package's INF, names none, or "
            "cannot be read.\n");
        return 0;
    }
    return 1;
}

/* Section 9.2 step 1: the name out of UpperFilters, the port reinstalled
 * from a driver node that is not this package's. 0 when the run must stop. */
static int unfilter_nt(HDEVINFO set, const DEVREC *d)
{
    SP_DEVINFO_DATA data;
    char msz[512];
    size_t n;
    int removed;

    out("Taking xhcicm off the tunnel port %s ... ", d->Id);
    if (opts.List) {
        out("(/list)\n");
        return 1;
    }
    if (!find_in_set(set, d->Id, &data)) {
        out("not found.\n");
        return 0;
    }
    n = inst_msz_remove(d->Upper, d->UpperBytes, "xhcicm", msz, sizeof(msz),
                        &removed);
    if (n == (size_t)-1) {
        out("FAILED (the value is too long).\n");
        return 0;
    }
    if (removed && !api.SetRegProp(set, &data, SPDRP_UPPERFILTERS,
                                   n ? (const BYTE *)msz : NULL, (DWORD)n)) {
        out("FAILED (error 0x%08lX).\n", (unsigned long)GetLastError());
        return 0;
    }
    flagChanged = 1;
    out(removed ? "done.\n" : "not in its filters.\n");
    if (!d->Present) {
        out("  The port is not connected, so its driver cannot be given back "
            "now: connect\n  the dock and run INSTALL.BAT /uninstall again.\n");
        flagSteps = 1;
        return 0;
    }
    out("Giving the port back to Windows' own driver ... ");
    if (!reinstall_without_ours(set, &data)) {
        return 0;
    }
    return unfilter_verify(set, &data, d, msz, n);
}

/* Section 9.2 step 2. 0 when the run must stop (failed or deferred). */
static int remove_nt(HDEVINFO set, const DEVREC *d)
{
    SP_DEVINFO_DATA data;
    SP_REMOVEDEVICE_PARAMS rp;
    SP_DEVINSTALL_PARAMS_A ip;

    out("Removing %s%s ... ", d->Id, d->Present ? "" : " (not present)");
    if (opts.List) {
        out("(/list)\n");
        return 1;
    }
    if (!find_in_set(set, d->Id, &data)) {
        out("not found.\n");
        return 0;
    }
    memset(&rp, 0, sizeof(rp));
    rp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    rp.ClassInstallHeader.InstallFunction = DIF_REMOVE;
    rp.Scope = DI_REMOVEDEVICE_GLOBAL;
    if (!api.SetCiParams(set, &data, &rp.ClassInstallHeader, sizeof(rp)) ||
        !api.CallCi(DIF_REMOVE, set, &data)) {
        out("FAILED (error 0x%08lX).\n", (unsigned long)GetLastError());
        return 0;
    }
    flagChanged = 1;
    memset(&ip, 0, sizeof(ip));
    ip.cbSize = sizeof(ip);
    if (!api.GetDiParams(set, &data, &ip)) {
        out("removed, but whether a restart is pending cannot be read "
            "(error 0x%08lX).\n", (unsigned long)GetLastError());
        flagFailed = 1;
        return 0;
    }
    if ((ip.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0) {
        out("removed, restart pending.\n");
        flagRestart = 1;
        return 0;
    }
    out("removed.\n");
    return 1;
}

/* 1 when no devnode's driver key names the INF file, and every devnode's
 * InfPath could be read (the manual route's prerequisite, KB 813449). */
static int inf_unused(const char *name)
{
    int i;

    for (i = 0; i < devCount; i++) {
        if (devs[i].InfPathState < 0) {
            out("  %s's driver key could not be read: %s is kept.\n",
                devs[i].Id, name);
            return 0;
        }
        if (devs[i].InfPathState > 0 && inst_eq(devs[i].InfPath, name)) {
            out("  %s still names %s: it is kept.\n", devs[i].Id, name);
            return 0;
        }
    }
    return 1;
}

/* Section 9.2 step 4: this package's INFs in %windir%\inf - the staged
 * oemNN.inf copies and the plain copies its DefaultInstall sections make.
 * 0 when one could not be removed. */
static int remove_own_infs(void)
{
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char pnf[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char *text;
    size_t n;
    int ok = 1;
    int oem;

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    join(pattern, sizeof(pattern), win, "inf\\*.inf");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        /* An empty directory is the only "nothing to do" (section 9.2). */
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return 1;
        }
        out("%s could not be listed.\n", pattern);
        return 0;
    }
    do {
        join(path, sizeof(path), win, "inf");
        join(path, sizeof(path), path, fd.cFileName);
        text = read_file(path, NULL);
        if (text == NULL) {
            /* Whose it is cannot be established: the run stops before the
             * services and files. */
            out("%s cannot be read: whether it is this package's is "
                "unknown.\n", path);
            ok = 0;
            continue;
        }
        if (!inst_inf_is_ours(text)) {
            free(text);
            continue;
        }
        free(text);
        oem = inst_starts(fd.cFileName, "oem");
        out("Removing the INF %s ... ", fd.cFileName);
        if (opts.List) {
            out("(/list)\n");
            continue;
        }
        if (oem && api.UninstallOem != NULL) {
            if (api.UninstallOem(fd.cFileName, 0, NULL)) {
                out("done.\n");
                flagChanged = 1;
            } else {
                out("FAILED (error 0x%08lX).\n", (unsigned long)GetLastError());
                ok = 0;
            }
            continue;
        }
        /* Windows 2000, and a plain copy: KB 813449's order - nothing uses
         * it, then the .pnf, then the .inf. */
        out("\n");
        if (!inf_unused(fd.cFileName)) {
            ok = 0;
            continue;
        }
        inst_copy(pnf, path, sizeof(pnf));
        n = strlen(pnf);
        if (n > 4) {
            strcpy(pnf + n - 4, ".pnf");
        }
        if (GetFileAttributesA(pnf) != 0xFFFFFFFFUL && !DeleteFileA(pnf)) {
            out("  FAILED (the .pnf, error %lu).\n", (unsigned long)GetLastError());
            ok = 0;
            continue;
        }
        if (DeleteFileA(path)) {
            out("  deleted (its .pnf first).\n");
            flagChanged = 1;
        } else {
            out("  FAILED (error %lu).\n", (unsigned long)GetLastError());
            ok = 0;
        }
    } while (FindNextFileA(h, &fd));
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        out("%s could not be listed to its end.\n", pattern);
        ok = 0;
    }
    FindClose(h);
    return ok;
}
static const char *const instServices[] = {
    "xhci98", "xhciuas", "xhcicm", "xhcihlpr", "xhcimsi"
};
static const char *const instFiles[] = {
    "xhci98.sys", "xhciuas.sys", "xhcicm.sys", "xhcihlpr.sys", "xhcimsi.sys"
};
#define INST_SVC_COUNT 5

/* Section 9.2 steps 5 and 6. */
static void remove_services_and_files(void)
{
    SC_HANDLE scm = NULL;
    SC_HANDLE s;
    SERVICE_STATUS st;
    char sys[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    int i;
    int fileOk;

    if (!opts.List) {
        scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (scm == NULL) {
            out("The service manager could not be opened (error %lu): the "
                "services and files stay.\n", (unsigned long)GetLastError());
            flagFailed = 1;
            return;
        }
    }
    if (GetSystemDirectoryA(sys, sizeof(sys)) == 0) {
        sys[0] = '\0';
    }
    for (i = 0; i < INST_SVC_COUNT; i++) {
        fileOk = 1;
        if (scm != NULL) {
            s = OpenServiceA(scm, instServices[i], DELETE | SERVICE_QUERY_STATUS);
            if (s == NULL) {
                /* Only "no such service" lets its file go (section 9.2
                 * steps 5 and 6); any other error keeps it. */
                DWORD e = GetLastError();
                if (e != ERROR_SERVICE_DOES_NOT_EXIST) {
                    out("The service %s could not be opened (error %lu): its "
                        "file stays.\n", instServices[i], (unsigned long)e);
                    flagFailed = 1;
                    fileOk = 0;
                }
            } else {
                /* A state that cannot be read is taken as running: the
                 * deletion then waits for the restart. */
                int known = QueryServiceStatus(s, &st);
                int running = !known || st.dwCurrentState != SERVICE_STOPPED;
                out("Removing the service %s (%s) ... ", instServices[i],
                    !known ? "state unreadable" : running ? "running" : "stopped");
                if (!DeleteService(s)) {
                    DWORD e = GetLastError();
                    if (e == ERROR_SERVICE_MARKED_FOR_DELETE) {
                        out("already marked for deletion.\n");
                        flagRestart = 1;
                    } else {
                        out("FAILED (error %lu): its file stays.\n",
                            (unsigned long)e);
                        flagFailed = 1;
                        fileOk = 0;
                    }
                } else if (running) {
                    out("marked for deletion; it goes at the restart.\n");
                    flagRestart = 1;
                    flagChanged = 1;
                } else {
                    out("done.\n");
                    flagChanged = 1;
                }
                CloseServiceHandle(s);
            }
        } else if (opts.List) {
            out("The service %s, if present, would be removed.\n",
                instServices[i]);
        }
        if (!fileOk || sys[0] == '\0') {
            continue;
        }
        join(path, sizeof(path), sys, "drivers");
        join(path, sizeof(path), path, instFiles[i]);
        if (!file_exists(path, NULL)) {
            continue;
        }
        out("Deleting %s ... ", path);
        if (opts.List) {
            out("(/list)\n");
            continue;
        }
        if (DeleteFileA(path)) {
            out("done.\n");
            flagChanged = 1;
        } else if (MoveFileExA(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) {
            out("in use; deleted at the restart.\n");
            flagRestart = 1;
            flagChanged = 1;
        } else {
            out("FAILED (error %lu).\n", (unsigned long)GetLastError());
            flagFailed = 1;
        }
    }
    if (scm != NULL) {
        CloseServiceHandle(scm);
    }
}

static int stop_uninstall(const char *why)
{
    out("\nSTOPPED: %s\nNothing after this step was done. Restart Windows if "
        "it asked, then run\nINSTALL.BAT /uninstall again: it goes on from "
        "what is left.\n", why);
    flagSteps = 1;
    return inst_exit_code(0, flagFailed, flagRestart, flagSteps);
}

static int run_uninstall_nt(void)
{
    HDEVINFO set;
    int rank;
    int i;
    int ok;
    int restartBefore;

    if (!warn_uninstall()) {
        return INST_EXIT_REFUSED;
    }
    /* Windows' own installs end before the devices are read, so the
     * removal works from a reading taken after them. */
    if (!opts.List && !nothing_pending()) {
        return inst_exit_code(0, flagFailed, flagRestart, flagSteps);
    }
    if (!scan()) {
        return INST_EXIT_REFUSED;
    }
    if (api.SetCiParams == NULL || api.CallCi == NULL || api.GetDiParams == NULL ||
        api.SetRegProp == NULL) {
        out("The device-removal routines are missing.\n");
        return INST_EXIT_REFUSED;
    }
    out("The OS's own usbd.sys and usbui.dll stay: Windows supplied them, "
        "and other USB\ndrivers use them. Devices of other drivers (a mouse's, "
        "a stick's) stay too.\n\n");
    set = api.GetClassDevs(NULL, NULL, NULL, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) {
        out("The device list could not be opened.\n");
        return INST_EXIT_REFUSED;
    }
    ok = 1;
    restartBefore = flagRestart;
    for (i = 0; i < devCount && ok; i++) {
        if (is_filter_host(&devs[i])) {
            ok = unfilter_nt(set, &devs[i]);
        }
    }
    if (!ok) {
        api.Destroy(set);
        if (!flagSteps) {
            flagFailed = 1;
        }
        return stop_uninstall("a tunnel port could not be given back to "
                              "Windows' own driver.");
    }
    if (flagRestart && !restartBefore) {
        /* A deferred phase stops the destructive phases after it (record 23
         * section 9.2); the next run resumes from a fresh scan. */
        api.Destroy(set);
        return stop_uninstall("a tunnel port's driver change waits for a "
                              "restart.");
    }
    for (rank = 0; rank <= 5 && ok; rank++) {
        for (i = 0; i < devCount && ok; i++) {
            if (inst_remove_rank(devs[i].Id, devs[i].Service, 0) == rank) {
                ok = remove_nt(set, &devs[i]);
            }
        }
    }
    api.Destroy(set);
    if (!ok) {
        if (!flagRestart) {
            flagFailed = 1;
        }
        return stop_uninstall("a device could not be removed, or its removal "
                              "waits for a restart.");
    }
    if (opts.List) {
        remove_own_infs();
        remove_services_and_files();
        tray_remove();
        out("\n/list: nothing was changed.\n");
        return INST_EXIT_DONE;
    }
    /* Step 3: nothing of the package is left among the devnodes. */
    if (!scan()) {
        flagFailed = 1;
        return stop_uninstall("the device list could not be read again.");
    }
    for (i = 0; i < devCount; i++) {
        if (inst_own_service(devs[i].Service) || in_remove_set(&devs[i]) ||
            is_filter_host(&devs[i])) {
            out("Still there: %s (%s)\n", devs[i].Id, devs[i].Service);
            if (devs[i].Present) {
                out("Windows found the device again and installed this "
                    "package's driver on it.\nRemove it in Device Manager "
                    "(on Vista and 7 tick \"Delete the driver software\"),\nthen "
                    "run INSTALL.BAT /uninstall again.\n");
            }
            return stop_uninstall("a device of this package is still "
                                  "recorded.");
        }
    }
    if (!remove_own_infs()) {
        flagFailed = 1;
        return stop_uninstall("an INF of this package could not be removed.");
    }
    remove_services_and_files();
    local_remove_all();
    if (!flagChanged) {
        out("Nothing of this package was found.\n");
    }
    if (flagRestart) {
        out("\nRESTART Windows to finish the removal.\n");
    }
    out("\nTo install the package again, run INSTALL.BAT.\n");
    return inst_exit_code(0, flagFailed, flagRestart, flagSteps);
}

static int run_uninstall_9x(void)
{
    char dir[INST_PATH_CHARS];
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char *text;
    int i;

    if (!warn_uninstall()) {
        return INST_EXIT_REFUSED;
    }
    if (!local_dir(dir, sizeof(dir)) ||
        GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return INST_EXIT_REFUSED;
    }
    out("Windows 98 SE and ME have no documented routine that removes a "
        "device, so this\ntool removes what it placed itself and lists the "
        "rest.\n\n");
    if (!opts.List) {
        /* The copies go only once DevicePath no longer names them: a search
         * path must never point at a removed source. */
        if (!devicepath_update(NULL)) {
            out("DevicePath could not be updated: the copies in %s stay.\n",
                dir);
            flagFailed = 1;
            return inst_exit_code(0, flagFailed, flagRestart, 1);
        }
        local_remove_all();
    } else {
        out("(/list) the copies in %s and their DevicePath entry would be "
            "removed.\n", dir);
        tray_remove();
    }
    out("\nThe INF copies Windows keeps for this package:\n");
    for (i = 0; i < 2; i++) {
        const char *sub = i == 0 ? "INF" : "INF\\OTHER";
        join(pattern, sizeof(pattern), win, sub);
        join(pattern, sizeof(pattern), pattern, "*.INF");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            join(path, sizeof(path), win, sub);
            join(path, sizeof(path), path, fd.cFileName);
            text = read_file(path, NULL);
            if (text != NULL && inst_inf_is_ours(text)) {
                out("  %s\n", path);
            }
            free(text);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    out("The driver files:\n");
    for (i = 0; i < INST_SVC_COUNT; i++) {
        join(path, sizeof(path), win, "SYSTEM32\\DRIVERS");
        join(path, sizeof(path), path, instFiles[i]);
        if (file_exists(path, NULL)) {
            out("  %s\n", path);
        }
    }
    out("\nThen, by hand, in this order:\n"
        "  1. Device Manager: remove \"xHCI98 USB Attached SCSI Storage\" and "
        "an\n     \"xHCI98 Thunderbolt 2 Controller\", \"xHCI98 Thunderbolt 3 "
        "Controller\" or\n     \"xHCI98 USB4 Host Router\" if there is one; "
        "then \"xHCI98 USB 3.x\n     eXtensible Host Controller\" (it removes "
        "the root hub and everything\n     beneath it).\n"
        "  2. Delete the INF files listed above, so the restart cannot find "
        "the driver\n     again.\n"
        "  3. Restart. If the New Hardware wizard offers the controller, "
        "Cancel it.\n"
        "  4. Delete the driver files listed above, and restart once more.\n");
    /* The steps above are always left; a failure (the USB tree tool's Run
     * value, record 23 section 9.5) outranks them. */
    return opts.List ? INST_EXIT_DONE
                     : inst_exit_code(0, flagFailed, flagRestart, 1);
}

/* ======================================================================== */
/* The self-test (the pure half's own suite is test\test_inst.c)             */
/* ======================================================================== */

static int stChecks;
static int stFailures;

static void st(int ok, const char *what)
{
    stChecks++;
    if (!ok) {
        stFailures++;
        printf("FAIL: %s\n", what);
    }
}

static int selftest(void)
{
    INST_ENV e;
    char out1[128];
    INST_U32 v[4];
    char *argv1[3];
    INST_OPTS o;
    char err[128];

    memset(&e, 0, sizeof(e));
    e.Nt = 1;
    e.Major = 6;
    e.Minor = 1;
    e.Arch = INST_MY_ARCH;
    inst_models_section("M,NTx86.6.0,NTamd64.6.0", &e, out1, sizeof(out1));
    st(inst_eq(out1, INST_MY_ARCH == INST_ARCH_X86 ? "M.NTx86.6.0" :
                                                     "M.NTamd64.6.0"),
       "models section for this build's architecture");
    st(inst_ver_parse(XHCIINST_VERSION, v), "the tool's version parses");
    st(XhciPswGate(0x8C318086UL) == 1, "the switchover gate: Lynx Point");
    st(XhciPswGate(0x000D1B36UL) == 0, "the switchover gate: QEMU");
    argv1[0] = "x";
    argv1[1] = "/list";
    argv1[2] = "/yes";
    st(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1, "/list /yes refused");
    st(inst_exit_code(0, 0, 1, 0) == 3, "exit code 3");
    st(inst_value_find("XhciIntelPortSwitch") >= 0, "the value table");
    argv1[1] = "/eject";
    argv1[2] = "/yes";
    st(inst_parse_args(3, argv1, &o, err, sizeof(err)) == 1,
       "/eject /yes without id= refused");
    st(inst_tb_candidate("PCI\\VEN_8086&DEV_15C0&CC_0604\0", 31),
       "a Thunderbolt port id");
    st(inst_eject_code(INST_EJ_VETO) == INST_EXIT_STEPS, "a veto is exit 4");
    printf("%d checks, %d failures\n", stChecks, stFailures);
    return stFailures == 0 ? 0 : 1;
}

/* ======================================================================== */
/* The Thunderbolt and USB4 device viewer and eject (design record 24)      */
/* ======================================================================== */

#define INST_CM_DRP_SERVICE         0x00000005UL
#define INST_CM_DRP_CAPABILITIES    0x00000010UL
#define INST_CM_DRP_BUSNUMBER       0x00000016UL
#define INST_CM_DRP_ADDRESS         0x0000001DUL
#define INST_CM_DEVCAP_REMOVABLE    0x00000004UL
#define INST_CM_DEVCAP_SURPRISEOK   0x00000080UL
#define INST_DN_REMOVABLE           0x00004000UL
#define INST_UI_NOT_OK              0x00000001UL
#define INST_TB_MAX_NHI             4
#define INST_TB_ASK_WAIT_MS         3000UL
#define INST_TB_TUNNEL_WAIT_MS      10000UL

typedef DWORD (WINAPI *PFN_CM_QREMOVE)(DWORD, ULONG);
typedef DWORD (WINAPI *PFN_CM_EJECT)(DWORD, PULONG, LPSTR, ULONG, ULONG);
typedef DWORD (WINAPI *PFN_CM_QANDR_W)(DWORD, PULONG, LPWSTR, ULONG, ULONG);

typedef struct _TBNODE {
    DWORD Dn;
    int Parent;
    DEVREC R;
    ULONG Caps;
    int HasCaps;
    ULONG Bus;
    ULONG Addr;
    int HasLoc;
    int IsNhi;
    int IsUsbHc;
    int Candidate;
    int Verified;           /* a verified tunnel port */
    int VNhi;               /* ... of this NHI record */
    int VPort;              /* ... its port index     */
    int Prot;
    int UnitOf;
    int Number;             /* the /eject number, 0 for none */
    char Shown[64];         /* the driver as shown: the service, or on 98
                               and ME a port driver or device loader */
} TBNODE;

typedef struct _TBNHI {
    char Id[INST_ID_CHARS];
    int Node;               /* in the current reading, -1 when gone */
    unsigned char Rec[CM_ST_MAX_BYTES];
    DWORD Bytes;
    int Valid;              /* the record checks                   */
    int Confirmed;          /* it echoes this run's ask            */
    int Asked;              /* the ask was written                 */
    INST_U32 Ask;
    INST_U32 Gen;           /* the generation it was confirmed at  */
    char DrvPath[INST_PATH_CHARS];
} TBNHI;

static TBNODE *tbn;
static int tbnCount;
static int tbnBad;          /* reads that failed, or a cut-short walk */
static TBNHI tbh[INST_TB_MAX_NHI];
static int tbhCount;
static int tbUnits;
static int tbChanging;      /* a record moved during the reading      */
static const char *devLast; /* the device modes' last line            */
static PFN_CM_QREMOVE tbQueryRemove;
static PFN_CM_QREMOVE tbRemove;
static PFN_CM_EJECT tbEject;
static PFN_CM_QANDR_W tbQueryAndRemove;  /* NT */

static void tb_routines(void)
{
    tbQueryRemove = (PFN_CM_QREMOVE)proc(api.CfgMgr, "cfgmgr32", "CM_Query_Remove_SubTree");
    tbRemove = (PFN_CM_QREMOVE)proc(api.CfgMgr, "cfgmgr32", "CM_Remove_SubTree");
    tbEject = (PFN_CM_EJECT)proc(api.CfgMgr, "cfgmgr32", "CM_Request_Device_EjectA");
    if (tbEject == NULL) {
        /* Windows 2000 exports it from setupapi.dll alone (record 24
         * section 7.2). */
        tbEject = (PFN_CM_EJECT)proc(api.SetupApi, "setupapi", "CM_Request_Device_EjectA");
    }
    if (envNow.Nt) {
        /* The W form: Windows 2000's CM_Query_And_Remove_SubTree_ExA, which
         * the A form calls, answers CR_CALL_NOT_IMPLEMENTED and does
         * nothing else (record 24 section 7.2). */
        tbQueryAndRemove = (PFN_CM_QANDR_W)proc(api.SetupApi, "setupapi",
                                                "CM_Query_And_Remove_SubTreeW");
        if (tbQueryAndRemove == NULL) {
            tbQueryAndRemove = (PFN_CM_QANDR_W)proc(
                api.CfgMgr, "cfgmgr32", "CM_Query_And_Remove_SubTreeW");
        }
    }
}

/* NT: the fallback for a unit not marked removable (record 24 section
 * 7.2), its veto name in the ANSI code page. The flags as
 * inst_removal_retry says. */
static DWORD tb_query_and_remove(DWORD dn, ULONG *veto, char *name, int cap)
{
    WCHAR wide[MAX_PATH];
    ULONG flags = INST_REMOVE_UI_NOT_OK | INST_REMOVE_NO_RESTART;
    DWORD cr;

    for (;;) {
        *veto = 0;
        wide[0] = 0;
        cr = tbQueryAndRemove(dn, veto, wide, MAX_PATH, flags);
        logonly("CM_Query_And_Remove_SubTreeW(flags 0x%lX) = 0x%08lX, veto "
                "%lu\n", (unsigned long)flags, (unsigned long)cr,
                (unsigned long)*veto);
        if (!inst_removal_retry(cr, flags)) {
            break;
        }
        flags = INST_REMOVE_UI_NOT_OK;
    }
    wide[MAX_PATH - 1] = 0;
    name[0] = '\0';
    if (WideCharToMultiByte(CP_ACP, 0, wide, -1, name, cap, NULL, NULL) == 0) {
        name[0] = '\0';
    }
    name[cap - 1] = '\0';
    return cr;
}

static DWORD tb_dword_prop(DWORD dn, ULONG prop, ULONG *value)
{
    ULONG type;
    ULONG n = sizeof(ULONG);

    if (api.Property == NULL) {
        return 0xFFFFFFFFUL;
    }
    return api.Property(dn, prop, &type, value, &n, 0);
}

/* A property read or absent (CR_NO_SUCH_VALUE). */
static int tb_prop_ok(DWORD cr)
{
    if (cr != INST_CR_SUCCESS && cr != INST_CR_NO_SUCH_VALUE) {
        logonly("a property read answered 0x%08lX\n", (unsigned long)cr);
        return 0;
    }
    return 1;
}

/* One devnode read; 0 when a reading failed. */
static int tb_read(TBNODE *t)
{
    DEVREC *d = &t->R;
    ULONG status;
    ULONG problem;
    DWORD bytes;
    int before = walkErrors;
    int ok = 1;

    memset(d, 0, sizeof(*d));
    if (api.DeviceId(t->Dn, d->Id, sizeof(d->Id), 0) != INST_CR_SUCCESS) {
        return 0;
    }
    if (envNow.Nt) {
        /* What classifies a devnode must be read, or be absent: any other
         * answer makes the reading incomplete (record 24 section 4.1). */
        if (!tb_prop_ok(cm_prop(t->Dn, INST_CM_DRP_HARDWAREID, d->Hw,
                                sizeof(d->Hw), &bytes))) {
            ok = 0;
        }
        d->HwBytes = bytes;
        if (!tb_prop_ok(cm_prop(t->Dn, INST_CM_DRP_COMPATIBLEIDS, d->Compat,
                                sizeof(d->Compat), &bytes))) {
            ok = 0;
        }
        d->CompatBytes = bytes;
        if (!tb_prop_ok(cm_prop(t->Dn, INST_CM_DRP_DRIVER, d->Driver,
                                sizeof(d->Driver), NULL)) ||
            !tb_prop_ok(cm_prop(t->Dn, INST_CM_DRP_SERVICE, d->Service,
                                sizeof(d->Service), NULL))) {
            ok = 0;
        }
        cm_prop(t->Dn, INST_CM_DRP_DEVICEDESC, d->Desc, sizeof(d->Desc), NULL);
        t->HasCaps = tb_dword_prop(t->Dn, INST_CM_DRP_CAPABILITIES,
                                   &t->Caps) == INST_CR_SUCCESS;
        t->HasLoc = tb_dword_prop(t->Dn, INST_CM_DRP_BUSNUMBER, &t->Bus) ==
                        INST_CR_SUCCESS &&
                    tb_dword_prop(t->Dn, INST_CM_DRP_ADDRESS, &t->Addr) ==
                        INST_CR_SUCCESS;
    } else {
        if (!prop9x(t->Dn, d->Id, INST_CM_DRP_HARDWAREID, "HardwareID", d->Hw,
                    sizeof(d->Hw)) ||
            !prop9x(t->Dn, d->Id, INST_CM_DRP_COMPATIBLEIDS, "CompatibleIDs",
                    d->Compat, sizeof(d->Compat)) ||
            !prop9x(t->Dn, d->Id, INST_CM_DRP_DRIVER, "Driver", d->Driver,
                    sizeof(d->Driver))) {
            ok = 0;
        }
        prop9x(t->Dn, d->Id, INST_CM_DRP_DEVICEDESC, "DeviceDesc", d->Desc,
               sizeof(d->Desc));
        d->HwBytes = (DWORD)inst_ids_split(d->Hw, sizeof(d->Hw));
        d->CompatBytes = (DWORD)inst_ids_split(d->Compat, sizeof(d->Compat));
    }
    if (api.Status(&status, &problem, t->Dn, 0) == INST_CR_SUCCESS) {
        d->Status = status;
        d->Problem = (status & INST_DN_HAS_PROBLEM) ? problem : 0;
    } else {
        ok = 0;
    }
    d->Present = 1;
    outQuiet = 1;
    dev_driver_values(d);
    outQuiet = 0;
    if (walkErrors != before) {
        walkErrors = before;
        ok = 0;
    }
    inst_copy(t->Shown, d->Service, sizeof(t->Shown));
    if (!envNow.Nt && t->Shown[0] == '\0' && d->Driver[0] != '\0') {
        /* 98 and ME: an IOS miniport (stor98.mpd) is named by PortDriver,
         * a disk by its DevLoader; shown only, never a classification. */
        char path[INST_PATH_CHARS];
        driver_key_path(d, path, sizeof(path));
        if (reg_text3(HKEY_LOCAL_MACHINE, path, "PortDriver", t->Shown,
                      sizeof(t->Shown)) <= 0 &&
            reg_text3(HKEY_LOCAL_MACHINE, path, "DevLoader", t->Shown,
                      sizeof(t->Shown)) <= 0) {
            t->Shown[0] = '\0';
        }
    }
    return ok;
}

static void tb_walk(DWORD dn, int parent, int depth)
{
    DWORD child;
    DWORD sib;
    DWORD cr;
    int me;

    for (;;) {
        if (tbnCount >= INST_MAX_DEVS) {
            tbnBad++;
            return;
        }
        me = tbnCount++;
        memset(&tbn[me], 0, sizeof(tbn[me]));
        tbn[me].Dn = dn;
        tbn[me].Parent = parent;
        tbn[me].UnitOf = -1;
        tbn[me].VNhi = -1;
        if (!tb_read(&tbn[me])) {
            logonly("devnode 0x%08lX: not read whole\n", (unsigned long)dn);
            /* The tree's root (HTREE\ROOT\0 on NT) has no registry entry
             * of its own to read, and is neither a port nor a unit. */
            if (parent >= 0 || tbn[me].R.Id[0] == '\0') {
                tbnBad++;
            }
        }
        cr = api.Child(&child, dn, 0);
        if (cr == INST_CR_SUCCESS) {
            if (depth >= 32) {
                tbnBad++;
            } else {
                tb_walk(child, me, depth + 1);
            }
        } else if (cr != INST_CR_NO_SUCH_DEVINST) {
            tbnBad++;
        }
        cr = api.Sibling(&sib, dn, 0);
        if (cr != INST_CR_SUCCESS) {
            if (cr != INST_CR_NO_SUCH_DEVINST) {
                tbnBad++;
            }
            return;
        }
        dn = sib;
    }
}

static int tb_started(const TBNODE *t)
{
    return (t->R.Status & INST_DN_STARTED) != 0 && t->R.Problem == 0;
}

/* The record of an NHI: its hardware key's value XhciCmStatus. */
static int tb_read_record(TBNHI *h)
{
    char path[INST_PATH_CHARS];
    char base[INST_PATH_CHARS];
    HKEY k;
    DWORD type;
    DWORD n = sizeof(h->Rec);

    h->Valid = 0;
    h->Bytes = 0;
    if (h->Node < 0) {
        return 0;
    }
    if (envNow.Nt) {
        join(base, sizeof(base), "SYSTEM\\CurrentControlSet\\Enum",
             tbn[h->Node].R.Id);
        join(path, sizeof(path), base, "Device Parameters");
    } else {
        join(path, sizeof(path), "Enum", tbn[h->Node].R.Id);
    }
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return 0;
    }
    if (RegQueryValueExA(k, "XhciCmStatus", NULL, &type, h->Rec, &n) ==
            ERROR_SUCCESS &&
        type == REG_BINARY) {
        h->Bytes = n;
        h->Valid = inst_st_check(h->Rec, n);
    }
    RegCloseKey(k);
    return h->Valid;
}

/* The ask (record 24 section 3.3): a fresh nonzero value in the NHI's
 * driver key; the record is confirmed once it echoes it. */
static int tb_read_record(TBNHI *h);

static void tb_ask(TBNHI *h)
{
    HKEY k;
    DWORD v;
    static INST_U32 seq;

    INST_U32 old = h->Ask;
    INST_U32 echo = 0;

    driver_key_path(&tbn[h->Node].R, h->DrvPath, sizeof(h->DrvPath));
    if (tb_read_record(h)) {
        echo = inst_get32(h->Rec, CM_ST_O_ECHO);
    }
    do {
        seq++;
        h->Ask = (GetTickCount() ^ (GetCurrentProcessId() << 12) ^
                  (seq << 24) ^ (seq * 0x9E3779B1UL)) | 1UL;
    } while (h->Ask == old || h->Ask == echo);
    h->Asked = 0;
    h->Confirmed = 0;
    v = h->Ask;
    if (tbn[h->Node].R.Driver[0] == '\0' ||
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, h->DrvPath, 0, KEY_SET_VALUE, &k) !=
            ERROR_SUCCESS) {
        logonly("ask: the driver key %s is not writable\n", h->DrvPath);
        return;
    }
    h->Asked = RegSetValueExA(k, "XhciCmStatusAsk", 0, REG_DWORD,
                              (const BYTE *)&v, sizeof(v)) == ERROR_SUCCESS;
    RegCloseKey(k);
    logonly("ask 0x%08lX to %s: %s\n", (unsigned long)v, h->DrvPath,
            h->Asked ? "written" : "refused");
}

static void tb_unask(void)
{
    HKEY k;
    int i;

    for (i = 0; i < tbhCount; i++) {
        if (tbh[i].Asked &&
            RegOpenKeyExA(HKEY_LOCAL_MACHINE, tbh[i].DrvPath, 0, KEY_SET_VALUE,
                          &k) == ERROR_SUCCESS) {
            RegDeleteValueA(k, "XhciCmStatusAsk");
            RegCloseKey(k);
        }
    }
}

/* The verified ports, the protection and the units of the reading. */
static int tb_derive(void)
{
    int *buf;
    int *parent;
    int *isNhi;
    int *isHc;
    int *verified;
    int *prot;
    int *units;
    int i;
    int k;
    INST_U32 j;

    for (k = 0; k < tbhCount; k++) {
        INST_U32 count;
        INST_U32 active;
        if (!tbh[k].Confirmed) {
            continue;
        }
        count = inst_get32(tbh[k].Rec, CM_ST_O_PORT_COUNT);
        active = inst_get32(tbh[k].Rec, CM_ST_O_PORT_ACTIVE);
        for (j = 0; j < count && j < CM_ST_MAX_PORTS; j++) {
            INST_U32 bdf;
            const unsigned char *path;
            if ((active & (1UL << j)) == 0) {
                continue;
            }
            bdf = inst_get32(tbh[k].Rec, CM_ST_O_PORTS + 4 * j);
            path = tbh[k].Rec + CM_ST_O_PATHS + CM_ST_PATH_BYTES * j;
            for (i = 0; i < tbnCount; i++) {
                int match;
                if (!inst_starts(tbn[i].R.Id, "PCI\\")) {
                    continue;
                }
                if (envNow.Nt) {
                    match = tbn[i].HasLoc &&
                            tbn[i].Bus == CM_ST_BDF_BUS(bdf) &&
                            tbn[i].Addr == ((CM_ST_BDF_DEV(bdf) << 16) |
                                            CM_ST_BDF_FN(bdf));
                } else {
                    match = inst_tb_loc_match(tbn[i].R.Id, bdf, path + 1,
                                              (int)path[0]);
                }
                if (match) {
                    tbn[i].Verified = 1;
                    tbn[i].VNhi = k;
                    tbn[i].VPort = (int)j;
                }
            }
        }
    }
    buf = (int *)calloc((size_t)(tbnCount + 1) * 6, sizeof(int));
    if (buf == NULL) {
        out("Out of memory.\n");
        return 0;
    }
    parent = buf;
    isNhi = parent + tbnCount + 1;
    isHc = isNhi + tbnCount + 1;
    verified = isHc + tbnCount + 1;
    prot = verified + tbnCount + 1;
    units = prot + tbnCount + 1;
    for (i = 0; i < tbnCount; i++) {
        parent[i] = tbn[i].Parent;
        isNhi[i] = tbn[i].IsNhi;
        isHc[i] = tbn[i].IsUsbHc;
        verified[i] = tbn[i].Verified;
    }
    inst_tb_protect(parent, isNhi, isHc, tbnCount, prot);
    inst_tb_units(parent, verified, tbnCount, units);
    tbUnits = 0;
    for (i = 0; i < tbnCount; i++) {
        tbn[i].Prot = prot[i];
        tbn[i].UnitOf = units[i];
        tbn[i].Number = 0;
    }
    for (i = 0; i < tbnCount; i++) {
        if (units[i] == i &&
            !inst_tb_unit_blocked(units, prot, tbnCount, i)) {
            tbn[i].Number = ++tbUnits;
        }
    }
    free(buf);
    return 1;
}

/* A full reading. `first`: the NHIs are found, asked and waited for;
 * otherwise (a later reading in the same run) each is found again by its
 * instance id and confirmed only by the same ask at the same generation.
 * 0 when the routines are missing. */
static int tb_scan(int first)
{
    DWORD root;
    DWORD start;
    int i;
    int k;
    int waiting;

    tbnCount = 0;
    tbnBad = 0;
    tbChanging = 0;
    if (api.Locate == NULL || api.Child == NULL || api.Sibling == NULL ||
        api.DeviceId == NULL || api.Status == NULL) {
        out("The configuration manager's routines are missing; the devices "
            "cannot be read.\n");
        return 0;
    }
    if (api.Locate(&root, NULL, 0) != INST_CR_SUCCESS) {
        out("The device tree's root could not be located.\n");
        return 0;
    }
    tb_walk(root, -1, 0);
    for (i = 0; i < tbnCount; i++) {
        TBNODE *t = &tbn[i];
        t->IsNhi = inst_tb_is_nhi(t->R.Hw, t->R.HwBytes, t->R.Compat,
                                  t->R.CompatBytes, t->R.Service);
        t->IsUsbHc = inst_tb_is_usbhc(t->R.Hw, t->R.HwBytes, t->R.Compat,
                                      t->R.CompatBytes, t->R.Service);
        t->Candidate = inst_tb_candidate(t->R.Hw, t->R.HwBytes);
    }
    if (first) {
        tbhCount = 0;
        for (i = 0; i < tbnCount && tbhCount < INST_TB_MAX_NHI; i++) {
            if (inst_eq(tbn[i].R.Service, "xhcicm") &&
                inst_starts(tbn[i].R.Id, "PCI\\")) {
                memset(&tbh[tbhCount], 0, sizeof(tbh[tbhCount]));
                tbh[tbhCount].Node = i;
                inst_copy(tbh[tbhCount].Id, tbn[i].R.Id,
                          sizeof(tbh[tbhCount].Id));
                if (tb_started(&tbn[i])) {
                    tb_ask(&tbh[tbhCount]);
                }
                tbhCount++;
            }
        }
        start = GetTickCount();
        for (;;) {
            waiting = 0;
            for (k = 0; k < tbhCount; k++) {
                if (tbh[k].Confirmed) {
                    continue;
                }
                if (tb_read_record(&tbh[k]) && tbh[k].Asked &&
                    inst_st_confirmed(tbh[k].Rec, tbh[k].Ask)) {
                    tbh[k].Confirmed = 1;
                    tbh[k].Gen = inst_get32(tbh[k].Rec, CM_ST_O_GENERATION);
                } else if (tbh[k].Asked) {
                    waiting = 1;
                }
            }
            if (!waiting || GetTickCount() - start >= INST_TB_ASK_WAIT_MS) {
                break;
            }
            Sleep(100);
        }
    } else {
        for (k = 0; k < tbhCount; k++) {
            int wasConfirmed = tbh[k].Confirmed;
            tbh[k].Node = -1;
            tbh[k].Confirmed = 0;
            for (i = 0; i < tbnCount; i++) {
                if (inst_eq(tbn[i].R.Id, tbh[k].Id) && tb_started(&tbn[i]) &&
                    inst_eq(tbn[i].R.Service, "xhcicm")) {
                    tbh[k].Node = i;
                }
            }
            if (wasConfirmed && tbh[k].Node >= 0 && tb_read_record(&tbh[k]) &&
                inst_st_confirmed(tbh[k].Rec, tbh[k].Ask)) {
                if (inst_get32(tbh[k].Rec, CM_ST_O_GENERATION) == tbh[k].Gen) {
                    tbh[k].Confirmed = 1;
                } else {
                    tbChanging = 1;
                }
            } else if (wasConfirmed) {
                tbChanging = 1;
            }
        }
    }
    return tb_derive();
}

/* The whole reading of record 24 section 3.3: asked and confirmed, then
 * walked and the records read again; a record that moved during the walk
 * is confirmed at its new generation and the walk taken again, at most
 * three times, after which nothing is taken as confirmed. */
static int tb_read_all(void)
{
    int tries;
    int k;

    if (!tb_scan(1)) {
        return 0;
    }
    for (tries = 0; tries < 3; tries++) {
        if (!tb_scan(0)) {
            return 0;
        }
        if (!tbChanging) {
            return 1;
        }
        for (k = 0; k < tbhCount; k++) {
            if (tbh[k].Valid && tbh[k].Asked &&
                inst_st_confirmed(tbh[k].Rec, tbh[k].Ask)) {
                tbh[k].Confirmed = 1;
                tbh[k].Gen = inst_get32(tbh[k].Rec, CM_ST_O_GENERATION);
            }
        }
    }
    tbChanging = 1;
    for (k = 0; k < tbhCount; k++) {
        tbh[k].Confirmed = 0;
    }
    (void)tb_derive();
    return 1;
}

/* A fresh ask to every confirmed NHI, waited for: the revalidation's
 * proof that the running driver still publishes (record 24 section 7.3).
 * 1 when every one answered. */
static int tb_reconfirm(void)
{
    DWORD start;
    int k;
    int waiting;
    int all = 1;

    for (k = 0; k < tbhCount; k++) {
        if (tbh[k].Confirmed && tbh[k].Node >= 0) {
            tb_ask(&tbh[k]);
            tbh[k].Gen = 0;
            if (!tbh[k].Asked) {
                all = 0;
            }
        }
    }
    start = GetTickCount();
    for (;;) {
        waiting = 0;
        for (k = 0; k < tbhCount; k++) {
            if (!tbh[k].Asked || tbh[k].Confirmed) {
                continue;
            }
            if (tb_read_record(&tbh[k]) &&
                inst_st_confirmed(tbh[k].Rec, tbh[k].Ask)) {
                tbh[k].Confirmed = 1;
                tbh[k].Gen = inst_get32(tbh[k].Rec, CM_ST_O_GENERATION);
            } else {
                waiting = 1;
            }
        }
        if (!waiting) {
            break;
        }
        if (GetTickCount() - start >= INST_TB_ASK_WAIT_MS) {
            all = 0;
            break;
        }
        Sleep(100);
    }
    return all;
}

/* ---- the viewer ---------------------------------------------------------- */

static const char *tb_tun_text(INST_U32 v)
{
    switch (v & 0xFUL) {
    case CM_ST_TUN_NONE:
        return "none";
    case CM_ST_TUN_UP:
        return "up";
    case CM_ST_TUN_PRESERVED:
        return "up (found at start)";
    case CM_ST_TUN_FAILED:
        return "failed";
    case CM_ST_TUN_EJECTED:
        return "closed after an eject";
    default:
        return "not known";
    }
}

static const char *tb_appr_text(INST_U32 v)
{
    switch (v) {
    case CM_ST_APPR_APPROVED:
        return "approved";
    case CM_ST_APPR_BOOT:
        return "approved before Windows started";
    case CM_ST_APPR_FAILED:
        return "approval refused by the controller";
    case CM_ST_APPR_UNANSWERED:
        return "approval not answered";
    case CM_ST_APPR_PENDING:
        return "not approved";
    case CM_ST_APPR_HELD:
        return "connection held or failed";
    default:
        return "approval not known";
    }
}

static void tb_name(const unsigned char *e, size_t off, char *out1, size_t cap)
{
    size_t i;

    for (i = 0; i + 1 < cap && i < CM_ST_NAME_BYTES - 1 && e[off + i] != 0; i++) {
        out1[i] = (char)e[off + i];
    }
    out1[i] = '\0';
}

static void tb_print_unit(int u, int indent)
{
    const TBNODE *t = &tbn[u];
    int i;
    char state[96];

    inst_state_text(state, sizeof(state),
                    t->Shown[0] ? t->Shown : NULL, NULL,
                    tb_started(t), t->R.Problem);
    if (t->Number) {
        out("%*s[%d] %s  -  %s\n", indent, "", t->Number,
            t->R.Desc[0] ? t->R.Desc : "(no name)", state);
    } else {
        out("%*s- %s  -  %s\n", indent, "",
            t->R.Desc[0] ? t->R.Desc : "(no name)", state);
    }
    out("%*s    %s\n", indent, "", first_hw(&t->R));
    if (envNow.Nt && t->Number &&
        inst_unit_not_marked(t->HasCaps, (INST_U32)t->Caps)) {
        /* Whatever the cause (the first plug, a rebuilt stack: EJRB.1). */
        out("%*s    not marked removable: Safely Remove Hardware may not list "
            "it;\n%*s    /eject stops it when run as an administrator\n",
            indent, "", indent, "");
    } else if (envNow.Nt) {
        out("%*s    removable: %s; surprise removal OK: %s\n", indent, "",
            t->HasCaps ? ((t->Caps & INST_CM_DEVCAP_REMOVABLE) ? "yes" : "no")
                       : "not known",
            t->HasCaps ? ((t->Caps & INST_CM_DEVCAP_SURPRISEOK) ? "yes" : "no")
                       : "not known");
        if (t->HasCaps && (t->Caps & INST_CM_DEVCAP_REMOVABLE) &&
            !(t->Caps & INST_CM_DEVCAP_SURPRISEOK)) {
            out("%*s    listed in Safely Remove Hardware\n", indent, "");
        } else if (t->Number) {
            out("%*s    not listed in Safely Remove Hardware; /eject asks "
                "Windows to stop it\n", indent, "");
        }
    } else {
        out("%*s    %s\n", indent, "",
            (t->R.Status & INST_DN_REMOVABLE)
                ? "marked removable by the Thunderbolt driver"
                : "not marked removable");
    }
    if (t->Number) {
        out("%*s    instance %s\n", indent, "", t->R.Id);
    } else if (t->UnitOf == u) {
        out("%*s    not offered: it holds the Thunderbolt controller's own "
            "functions\n", indent, "");
    }
    for (i = u + 1; i < tbnCount; i++) {
        if (tbn[i].UnitOf == u && i != u) {
            inst_state_text(state, sizeof(state),
                            tbn[i].Shown[0] ? tbn[i].Shown : NULL, NULL,
                            tb_started(&tbn[i]), tbn[i].R.Problem);
            out("%*s      %s  -  %s\n", indent, "",
                tbn[i].R.Desc[0] ? tbn[i].R.Desc : first_hw(&tbn[i].R), state);
        }
    }
}

static void tb_print_children_of(int port, int indent)
{
    int i;
    int any = 0;

    for (i = port + 1; i < tbnCount; i++) {
        if (tbn[i].Parent == port) {
            tb_print_unit(i, indent);
            any = 1;
        }
    }
    if (!any) {
        out("%*s(nothing behind it now)\n", indent, "");
    }
}

static void tb_print_routers(const TBNHI *h)
{
    INST_U32 n = inst_get32(h->Rec, CM_ST_O_ROUTER_COUNT);
    INST_U32 r;
    char vendor[CM_ST_NAME_BYTES];
    char model[CM_ST_NAME_BYTES];

    if (n == 0) {
        out("  Nothing is connected to its Thunderbolt or USB4 ports.\n");
        return;
    }
    for (r = 0; r < n; r++) {
        const unsigned char *e = h->Rec + CM_ST_HDR_BYTES + r * CM_ST_ENTRY_BYTES;
        INST_U32 kind = inst_get32(e, CM_ST_E_KIND);
        INST_U32 place = inst_get32(e, CM_ST_E_PLACE);
        INST_U32 gen = inst_get32(e, CM_ST_E_GEN);
        INST_U32 tun = inst_get32(e, CM_ST_E_TUNNELS);
        INST_U32 lanes = inst_get32(e, CM_ST_E_LANES);
        INST_U32 sid = inst_get32(e, CM_ST_E_SHORT_ID);
        INST_U32 ids = inst_get32(e, CM_ST_E_IDS);
        INST_U32 bridge = inst_get32(e, CM_ST_E_BRIDGE);

        tb_name(e, CM_ST_E_VENDOR, vendor, sizeof(vendor));
        tb_name(e, CM_ST_E_MODEL, model, sizeof(model));
        out("\n  Device: %s%s%s", vendor[0] ? vendor : "",
            vendor[0] && model[0] ? " " : "",
            model[0] ? model : (vendor[0] ? "" : "(no name given)"));
        if (sid != CM_ST_UNKNOWN) {
            out("  (id ending %04lx)", (unsigned long)sid);
        }
        out("\n");
        out("    %s", (gen & 0xFFUL) == CM_ST_GEN_TBT3 ? "Thunderbolt 3"
                      : (gen & 0xFFUL) == CM_ST_GEN_USB4 ? "USB4"
                                                        : "generation not known");
        if ((gen & 0xFFUL) == CM_ST_GEN_USB4 && ((gen >> 8) & 0xFFUL) != 0) {
            /* ROUTER_CS_4's USB4 Version: major 7:5, minor 4:0. */
            out(" (version %lu.%lu)", (unsigned long)((gen >> 13) & 0x7UL),
                (unsigned long)((gen >> 8) & 0x1FUL));
        }
        out(", %s %lu", kind == CM_ST_KIND_ICM ? "link" : "port",
            (unsigned long)(place & 0xFFUL));
        if (kind != CM_ST_KIND_USB4_LINK) {
            out(", depth %lu", (unsigned long)((place >> 8) & 0xFFUL));
        } else {
            out(", behind another device");
        }
        out(", %s\n", tb_appr_text(inst_get32(e, CM_ST_E_APPROVAL)));
        if (ids != CM_ST_UNKNOWN) {
            out("    %s %04lX:%04lX\n",
                kind == CM_ST_KIND_ICM ? "Thunderbolt vendor and model"
                                       : "router",
                (unsigned long)(ids & 0xFFFFUL), (unsigned long)(ids >> 16));
        }
        out("    link: %s; speed not known\n",
            lanes == CM_ST_LANES_BONDED ? "two lanes bonded"
            : lanes == CM_ST_LANES_SINGLE ? "one lane" : "lanes not known");
        out("    connections: PCIe %s, USB3 %s, DisplayPort %s\n",
            tb_tun_text(tun), tb_tun_text(tun >> 4), tb_tun_text(tun >> 8));
        if (bridge != CM_ST_UNKNOWN) {
            out("    its PCIe connection reaches the port at PCI %02lX:%02lX.%lu\n",
                (unsigned long)CM_ST_BDF_BUS(bridge),
                (unsigned long)CM_ST_BDF_DEV(bridge),
                (unsigned long)CM_ST_BDF_FN(bridge));
        }
    }
}

static void tb_print(void)
{
    int k;
    int i;
    int shownCandidates = 0;

    if (tbhCount == 0) {
        out("No Thunderbolt or USB4 controller driven by this package was "
            "found.\n");
    }
    for (k = 0; k < tbhCount; k++) {
        const TBNHI *h = &tbh[k];
        const TBNODE *t = h->Node >= 0 ? &tbn[h->Node] : NULL;
        INST_U32 cm;

        if (t == NULL) {
            continue;
        }
        out("Controller: %s\n", t->R.Desc[0] ? t->R.Desc : first_hw(&t->R));
        out("  %s\n", first_hw(&t->R));
        if (!tb_started(t)) {
            out("  not started (problem code %lu)\n", (unsigned long)t->R.Problem);
        }
        if (!h->Valid) {
            out("  The Thunderbolt driver has not published its device list "
                "(it is older than\n  this package, not started, or still "
                "starting).\n\n");
            continue;
        }
        cm = inst_get32(h->Rec, CM_ST_O_CM);
        out("  %s",
            cm == CM_ST_CM_ICM ? "firmware connection manager"
            : cm == CM_ST_CM_USB4 ? "USB4 software connection manager"
            : inst_get32(h->Rec, CM_ST_O_STAGE) == CM_ST_STAGE_INERT
                ? "no connection manager running (the driver stays idle)"
                : "no connection manager running");
        if (inst_get32(h->Rec, CM_ST_O_SECURITY) != CM_ST_UNKNOWN) {
            out(", security level %lu",
                (unsigned long)inst_get32(h->Rec, CM_ST_O_SECURITY));
        }
        out("\n");
        if (h->Confirmed) {
            out("  information from the driver: confirmed now (changed %lu "
                "times since it started)\n",
                (unsigned long)inst_get32(h->Rec, CM_ST_O_GENERATION));
        } else {
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            out("  information from the driver: NOT confirmed by the running "
                "driver%s%s\n",
                h->Asked ? "" : envNow.Nt ? " (run as an administrator to "
                                            "confirm it)" : "",
                inst_st_fresh(inst_get32(h->Rec, CM_ST_O_UPTIME_S),
                              inst_get32(h->Rec, CM_ST_O_SYSTIME_LO),
                              inst_get32(h->Rec, CM_ST_O_SYSTIME_HI),
                              ft.dwLowDateTime, ft.dwHighDateTime,
                              GetTickCount())
                    ? ""
                    : "; it is from an earlier start of Windows");
        }
        tb_print_routers(h);
        out("\n");
        for (i = 0; i < tbnCount; i++) {
            if (tbn[i].Verified && tbn[i].VNhi == k) {
                out("  Behind the Thunderbolt port %s:\n",
                    tbn[i].R.Desc[0] ? tbn[i].R.Desc : first_hw(&tbn[i].R));
                out("    PCI %02lX:%02lX.%lu  %s\n",
                    (unsigned long)CM_ST_BDF_BUS(inst_get32(
                        h->Rec, CM_ST_O_PORTS + 4 * tbn[i].VPort)),
                    (unsigned long)CM_ST_BDF_DEV(inst_get32(
                        h->Rec, CM_ST_O_PORTS + 4 * tbn[i].VPort)),
                    (unsigned long)CM_ST_BDF_FN(inst_get32(
                        h->Rec, CM_ST_O_PORTS + 4 * tbn[i].VPort)),
                    tbn[i].R.Id);
                tb_print_children_of(i, 4);
            }
        }
        out("\n");
    }
    for (i = 0; i < tbnCount; i++) {
        int c;
        int hasChild = 0;
        if (!tbn[i].Candidate || tbn[i].Verified || tbn[i].Prot) {
            continue;
        }
        for (c = i + 1; c < tbnCount; c++) {
            if (tbn[c].Parent == i && !tbn[c].Candidate && !tbn[c].Prot) {
                hasChild = 1;
            }
        }
        if (!hasChild) {
            continue;
        }
        if (!shownCandidates) {
            out("Other Thunderbolt or USB4 ports (not confirmed as carrying a "
                "connection;\nshown only):\n");
            shownCandidates = 1;
        }
        out("  %s\n    %s\n", tbn[i].R.Desc[0] ? tbn[i].R.Desc : "(no name)",
            tbn[i].R.Id);
        for (c = i + 1; c < tbnCount; c++) {
            if (tbn[c].Parent == i && !tbn[c].Prot) {
                tb_print_unit(c, 4);
            }
        }
    }
    if (tbnBad) {
        out("\nSome device entries could not be read whole (%d); the list may "
            "be incomplete.\n", tbnBad);
    }
    if (tbChanging) {
        out("\nDevices were changing while this was read.\n");
    }
    if (tbUnits > 0) {
        for (i = 0; i < tbnCount; i++) {
            if (tbn[i].Number == 1) {
                out("\nTo eject: INSTALL /eject 1   (or XHCIINST /eject 1)\n");
                out(envNow.Nt ? "  in a script: INSTALL /eject \"id=%s\" /yes\n"
                              : "  in a script: INSTALL /eject id=%s /yes\n",
                    tbn[i].R.Id);
            }
        }
    }
}

/* One device-mode run or XHCITR eject at a time on the machine: the ask in
 * each NHI's driver key is shared (record 24 section 3.3), and XHCITR, which
 * runs as the invoker, takes the same lock (record 25 section 6). An
 * abandoned lock is taken over: its holder is gone. */
static HANDLE tbMutex;

static int tb_lock(void)
{
    DWORD w;

    tbMutex = shared_mutex(envNow.Nt ? "Global\\XhciinstTbAsk"
                                     : "XhciinstTbAsk");
    if (tbMutex == NULL) {
        out("The lock shared with the xHCI98 USB Tree could not be opened "
            "(error %lu).\n", (unsigned long)GetLastError());
        return 0;
    }
    w = WaitForSingleObject(tbMutex, 10000);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        out("Another XHCIINST /devices or /eject, or an eject in the xHCI98 "
            "USB Tree, is\nrunning; try again when it has finished.\n");
        return 0;
    }
    return 1;
}

static int run_devices(void)
{
    if (!tb_read_all()) {
        tb_unask();
        return INST_EXIT_REFUSED;
    }
    tb_print();
    tb_unask();
    return INST_EXIT_DONE;
}

/* ---- the eject ------------------------------------------------------------ */

static int tb_find_unit(const char *id)
{
    int i;

    for (i = 0; i < tbnCount; i++) {
        if (tbn[i].Number != 0 && inst_eq(tbn[i].R.Id, id)) {
            return i;
        }
    }
    return -1;
}

/* After an accepted eject: the port read again, and section 5.4's advice.
 * The port and the router are found again by location in the fresh
 * reading; anything not established gives the qualified advice. */
static int tb_after(const char *portId, const char *unitId, int nhiK,
                    INST_U32 portBdf, int single)
{
    int i;
    int port = -1;
    int others = 0;
    INST_U32 usb3 = CM_ST_TUN_UNKNOWN;
    INST_U32 r;
    INST_U32 n;
    INST_U32 j;
    INST_U32 count;
    INST_U32 active;
    int whole;
    int portKnown = 0;
    int routers = 0;
    int tries;
    int walked = 0;

    /* A stable reading after the eject: the record's generation taken as
     * the baseline, the tree walked, and the walk repeated while the
     * record moved during it (the eject itself may move it once). */
    whole = 0;
    for (tries = 0; tries < 3 && !whole; tries++) {
        if (nhiK < 0 || tbh[nhiK].Node < 0 || !tb_read_record(&tbh[nhiK]) ||
            !inst_st_confirmed(tbh[nhiK].Rec, tbh[nhiK].Ask)) {
            break;
        }
        tbh[nhiK].Confirmed = 1;
        tbh[nhiK].Gen = inst_get32(tbh[nhiK].Rec, CM_ST_O_GENERATION);
        walked = tb_scan(0);
        whole = walked && tbnBad == 0 && !tbChanging && tbh[nhiK].Confirmed;
    }
    if (!walked) {
        /* No reading after the eject: tbn is still the one before it. */
        return INST_UNPLUG_CHECK;
    }
    for (i = 0; i < tbnCount; i++) {
        if (inst_eq(tbn[i].R.Id, portId)) {
            port = i;
        }
    }
    if (port >= 0) {
        for (i = port + 1; i < tbnCount; i++) {
            /* The ejected unit, held by Windows until the unplug, is not
             * another device; a device still started is. */
            if (tbn[i].Parent == port && !inst_eq(tbn[i].R.Id, unitId) &&
                tb_started(&tbn[i])) {
                others = 1;
            }
            if (tbn[i].Parent == port && inst_eq(tbn[i].R.Id, unitId) &&
                tb_started(&tbn[i])) {
                /* Started again: it has to be stopped again. */
                return INST_UNPLUG_RESTARTED;
            }
        }
    } else {
        whole = 0;
    }
    /* The record the stable walk was checked against, not a later read. */
    if (whole && nhiK >= 0 && tbh[nhiK].Confirmed &&
        inst_st_confirmed(tbh[nhiK].Rec, tbh[nhiK].Ask)) {
        count = inst_get32(tbh[nhiK].Rec, CM_ST_O_PORT_COUNT);
        active = inst_get32(tbh[nhiK].Rec, CM_ST_O_PORT_ACTIVE);
        for (j = 0; j < count && j < CM_ST_MAX_PORTS; j++) {
            if ((active & (1UL << j)) != 0 &&
                inst_get32(tbh[nhiK].Rec, CM_ST_O_PORTS + 4 * j) == portBdf) {
                portKnown = 1;
            }
        }
        n = inst_get32(tbh[nhiK].Rec, CM_ST_O_ROUTER_COUNT);
        for (r = 0; portKnown && r < n; r++) {
            const unsigned char *e =
                tbh[nhiK].Rec + CM_ST_HDR_BYTES + r * CM_ST_ENTRY_BYTES;
            if (inst_get32(e, CM_ST_E_BRIDGE) == portBdf) {
                routers++;
                usb3 = (inst_get32(e, CM_ST_E_TUNNELS) >> 4) & 0xFUL;
            }
        }
        if (routers != 1) {
            usb3 = CM_ST_TUN_UNKNOWN;   /* none, or ambiguous */
        }
    }
    return inst_unplug_kind(whole, others, single, usb3);
}

/* A storage function with no PCI function below it, neither a bridge nor
 * a USB controller (the single-function shape of record 24 section 5.4). */
static int tb_single(int u)
{
    int i;
    char cls[8];

    if (tbn[u].IsUsbHc || tbn[u].Candidate ||
        inst_msz_has_class(tbn[u].R.Hw, tbn[u].R.HwBytes, "0604") ||
        inst_msz_has_class(tbn[u].R.Compat, tbn[u].R.CompatBytes, "0604")) {
        return 0;
    }
    inst_copy(cls, "01", sizeof(cls));
    if (!inst_msz_has_class(tbn[u].R.Hw, tbn[u].R.HwBytes, cls) &&
        !inst_msz_has_class(tbn[u].R.Compat, tbn[u].R.CompatBytes, cls)) {
        return 0;
    }
    for (i = u + 1; i < tbnCount; i++) {
        if (tbn[i].UnitOf == u && inst_starts(tbn[i].R.Id, "PCI\\")) {
            return 0;
        }
    }
    return 1;
}

/* Under the USB4 profile, the record watched for the router's PCIe tunnel
 * closed after the eject (record 24 section 5.2). */
static void tb_watch_tunnel(int nhiK, INST_U32 bdf)
{
    DWORD start = GetTickCount();
    INST_U32 r;
    INST_U32 n;

    if (nhiK < 0 || inst_get32(tbh[nhiK].Rec, CM_ST_O_CM) != CM_ST_CM_USB4) {
        return;
    }
    for (;;) {
        if (tb_read_record(&tbh[nhiK]) &&
            inst_st_confirmed(tbh[nhiK].Rec, tbh[nhiK].Ask)) {
            n = inst_get32(tbh[nhiK].Rec, CM_ST_O_ROUTER_COUNT);
            for (r = 0; r < n; r++) {
                const unsigned char *e =
                    tbh[nhiK].Rec + CM_ST_HDR_BYTES + r * CM_ST_ENTRY_BYTES;
                if (inst_get32(e, CM_ST_E_BRIDGE) == bdf &&
                    (inst_get32(e, CM_ST_E_TUNNELS) & 0xFUL) ==
                        CM_ST_TUN_EJECTED) {
                    out("The PCIe tunnel was closed.\n");
                    return;
                }
            }
        }
        if (GetTickCount() - start >= INST_TB_TUNNEL_WAIT_MS) {
            out("The Thunderbolt driver has not confirmed that the PCIe tunnel "
                "was closed.\n");
            return;
        }
        Sleep(250);
    }
}

static int run_eject(void)
{
    char line[64];
    char id[INST_ID_CHARS];
    char portId[INST_ID_CHARS];
    char desc[128];
    int u;
    int i;
    int nhiK;
    int portIndex;
    INST_U32 portBdf;
    int single;
    int outcome;
    const char *text;
    INST_U32 n;
    DWORD cr;

    if (envNow.Nt ? tbEject == NULL
                  : (tbQueryRemove == NULL || tbRemove == NULL)) {
        out("REFUSED: %s is missing on this Windows.\n",
            envNow.Nt ? "CM_Request_Device_EjectA"
                      : (tbQueryRemove == NULL ? "CM_Query_Remove_SubTree"
                                               : "CM_Remove_SubTree"));
        return INST_EXIT_REFUSED;
    }
    if (!tb_read_all()) {
        tb_unask();
        return INST_EXIT_REFUSED;
    }
    tb_print();
    if (tbnBad != 0 || tbChanging) {
        out("\nThe device list could not be read whole and steady, so nothing "
            "is ejected.\n");
        tb_unask();
        return INST_EXIT_REFUSED;
    }
    if (tbUnits == 0) {
        for (i = 0; i < tbhCount; i++) {
            if (!tbh[i].Confirmed && tbh[i].Node >= 0) {
                out("\nThe Thunderbolt driver has not confirmed which ports "
                    "carry tunnels, so nothing\ncan be ejected safely from "
                    "here.%s\n",
                    (envNow.Nt && !tbh[i].Asked)
                        ? " Run it as an administrator."
                        : "");
                tb_unask();
                return INST_EXIT_REFUSED;
            }
        }
        out("\nThere is nothing to eject.\n");
        tb_unask();
        return INST_EXIT_REFUSED;
    }
    u = -1;
    if (opts.EjectId[0] != '\0') {
        u = tb_find_unit(opts.EjectId);
        if (u < 0) {
            out("\nNo device behind a Thunderbolt port has the instance id "
                "%s.\n", opts.EjectId);
            tb_unask();
            return INST_EXIT_REFUSED;
        }
    } else {
        n = opts.EjectNum;
        if (n == 0) {
            out("\nWhich device? Type its number and Enter (Enter alone "
                "cancels): ");
            if (!read_line(line, sizeof(line)) || line[0] == '\0') {
                out("\nCancelled.\n");
                tb_unask();
                return INST_EXIT_REFUSED;
            }
            n = (INST_U32)strtoul(line, NULL, 10);
        }
        for (i = 0; i < tbnCount; i++) {
            if (tbn[i].Number != 0 && (INST_U32)tbn[i].Number == n) {
                u = i;
            }
        }
        if (u < 0) {
            out("\nThere is no device number %lu.\n", (unsigned long)n);
            tb_unask();
            return INST_EXIT_REFUSED;
        }
    }
    inst_copy(id, tbn[u].R.Id, sizeof(id));
    inst_copy(desc, tbn[u].R.Desc[0] ? tbn[u].R.Desc : first_hw(&tbn[u].R),
              sizeof(desc));
    inst_copy(portId, tbn[tbn[u].Parent].R.Id, sizeof(portId));
    nhiK = tbn[tbn[u].Parent].VNhi;
    portIndex = tbn[tbn[u].Parent].VPort;
    portBdf = inst_get32(tbh[nhiK].Rec, CM_ST_O_PORTS + 4 * portIndex);
    out("\nDevice %d: %s\n  %s\n", tbn[u].Number, desc, id);
    if (!(opts.Yes && opts.EjectId[0] != '\0')) {
        out("Eject it now? Type Y and Enter (anything else cancels): ");
        if (!read_line(line, sizeof(line)) ||
            !(line[0] == 'Y' || line[0] == 'y') || line[1] != '\0') {
            out("\nCancelled.\n");
            tb_unask();
            return INST_EXIT_REFUSED;
        }
    }
    /* Revalidation (record 24 section 7.3), immediately before the call:
     * a fresh ask answered, then the tree read again at that generation. */
    if (!tb_reconfirm() || !tb_scan(0) || tbnBad != 0 || tbChanging) {
        out("\nThe device list changed; nothing was ejected.\n");
        tb_unask();
        return INST_EXIT_REFUSED;
    }
    u = tb_find_unit(id);
    if (u < 0 || !inst_eq(tbn[tbn[u].Parent].R.Id, portId) ||
        tbn[tbn[u].Parent].VNhi != nhiK ||
        inst_get32(tbh[nhiK].Rec, CM_ST_O_PORTS +
                                      4 * tbn[tbn[u].Parent].VPort) != portBdf) {
        out("\nThe device list changed; nothing was ejected.\n");
        tb_unask();
        return INST_EXIT_REFUSED;
    }
    single = tb_single(u);
    out("\n");
    if (envNow.Nt) {
        ULONG veto = 0;
        char vetoName[MAX_PATH];
        int withName;
        int notRemovable = 0;
        int step;
        vetoName[0] = '\0';
        cr = tbEject(tbn[u].Dn, &veto, vetoName, MAX_PATH, 0);
        vetoName[MAX_PATH - 1] = '\0';
        logonly("CM_Request_Device_EjectA(%s) = 0x%08lX, veto %lu %s\n", id,
                (unsigned long)cr, (unsigned long)veto, vetoName);
        cr = inst_eject_cr(cr, veto);
        step = inst_eject_nt_step(cr, veto,
                                  inst_unit_not_marked(tbn[u].HasCaps,
                                                       tbn[u].Caps), 1);
        if (step == INST_NT_FALLBACK) {
            /* Not marked removable (record 24 section 7.2): the same
             * query-remove and remove an eject runs, by the routine that
             * makes them for any device; a veto still stops it. */
            if (tbQueryAndRemove != NULL) {
                cr = tb_query_and_remove(tbn[u].Dn, &veto, vetoName, MAX_PATH);
                logonly("... for %s, veto name %s\n", id, vetoName);
                cr = inst_eject_cr(cr, veto);
            } else {
                cr = INST_CR_CALL_NOT_IMPLEMENTED;
                veto = 0;
            }
            notRemovable = 1;
            outcome = inst_eject_nt_removal(cr, veto, &text, &withName);
        } else if (step == INST_NT_ADMIN) {
            outcome = INST_EJ_RIGHTS;
            text = inst_admin_text();
            withName = 0;
        } else {
            outcome = inst_eject_nt(cr, veto, &text, &withName);
        }
        if (outcome != INST_EJ_DONE) {
            if (withName) {
                char name[MAX_PATH + 16];
                if (veto == 0 || vetoName[0] == '\0') {
                    _snprintf(name, sizeof(name) - 1, "veto %lu",
                              (unsigned long)veto);
                    name[sizeof(name) - 1] = '\0';
                } else {
                    inst_copy(name, vetoName, sizeof(name));
                }
                out(text, name);
                out("\n");
            } else {
                out("%s\n", text);
            }
            if (outcome == INST_EJ_FAILED) {
                out("(code 0x%08lX)\n", (unsigned long)cr);
                devLast = text;
            }
            tb_unask();
            return inst_eject_code(outcome);
        }
        out("Windows has stopped %s.\n", desc);
        if (notRemovable) {
            out("Until you unplug it, Windows may start it again when it next "
                "looks for new\nhardware.\n");
        }
        tb_watch_tunnel(nhiK, portBdf);
    } else {
        cr = tbQueryRemove(tbn[u].Dn, INST_UI_NOT_OK);
        logonly("CM_Query_Remove_SubTree(%s) = 0x%08lX\n", id, (unsigned long)cr);
        outcome = inst_eject9x_query(cr, &text);
        if (outcome != INST_EJ_DONE) {
            out("%s\n", text);
            if (cr != INST_CR_REMOVE_VETOED && outcome == INST_EJ_VETO) {
                out("(code 0x%08lX)\n", (unsigned long)cr);
            }
            tb_unask();
            return inst_eject_code(outcome);
        }
        /* Accepted: the removal always follows (record 24 section 7.1). */
        cr = tbRemove(tbn[u].Dn, INST_UI_NOT_OK);
        logonly("CM_Remove_SubTree(%s) = 0x%08lX\n", id, (unsigned long)cr);
        outcome = inst_eject9x_remove(cr, &text);
        if (outcome != INST_EJ_DONE) {
            out("%s\n(code 0x%08lX)\n", text, (unsigned long)cr);
            tb_unask();
            devLast = text;
            return inst_eject_code(outcome);
        }
        out("Windows has stopped %s.\n", desc);
        out("Until you unplug it, Device Manager's Refresh, or the "
            "Thunderbolt driver looking\nat the port again, can start it "
            "again.\n");
    }
    i = tb_after(portId, id, nhiK, portBdf, single);
    devLast = inst_unplug_text(i);
    tb_unask();
    return i == INST_UNPLUG_RESTARTED ? INST_EXIT_STEPS : INST_EXIT_DONE;
}

/* ======================================================================== */
/* The Thunderbolt 3 / USB4 question (record 23 section 8.5)                 */
/* ======================================================================== */

/* Asked after the first scan, before the plan is shown. A No takes the
 * Thunderbolt INFs out of this run: their devices are not planned, not
 * staged, not copied and not asked about; a Thunderbolt driver already
 * installed is left exactly as it is. */
static void tb_question(void)
{
    int ask;
    int def;
    int r;
    int k;
    int i;
    int installed = 0;
    char line[16];

    plan();
    tbNhiPresent = envNow.CmNhi;
    tbInclude = inst_tb_decide(haveCm, tbNhiPresent, opts.Yes,
                               opts.Thunderbolt, opts.List, &ask, &def);
    if (!haveCm) {
        tbInclude = 0;
        return;
    }
    for (i = 0; i < devCount; i++) {
        if (inst_eq(devs[i].Service, "xhcicm") ||
            inst_msz_has(devs[i].Upper, devs[i].UpperBytes, "xhcicm")) {
            installed = 1;
        }
    }
    out("Thunderbolt 3 / USB4 support\n");
    if (tbNhiPresent) {
        out("  A Thunderbolt or USB4 controller is present.\n");
    } else {
        out("  No Thunderbolt or USB4 controller was found. Some computers show "
            "theirs only\n  while a device is plugged in, or only with a BIOS "
            "setting: answer Y if this\n  computer has a Thunderbolt or USB4 "
            "port.\n");
    }
    out("  With it, a device plugged into that port is approved automatically "
        "and can\n  reach the computer's memory (the setting XhciCmApprove; "
        "see tools\\xhciinst\\readme.txt).\n");
    if (installed) {
        out("  Answering N keeps the Thunderbolt driver already installed as it "
            "is.\n");
    }
    if (ask) {
        for (;;) {
            out("Install Thunderbolt 3 / USB4 support? [%s]: ",
                def ? "Y/n" : "y/N");
            if (!read_line(line, sizeof(line))) {
                r = def;
                out("\n");
                break;
            }
            r = inst_tb_answer(line, def);
            if (r >= 0) {
                break;
            }
            out("  Type Y or N, or Enter for %s.\n", def ? "Y" : "N");
        }
        tbInclude = r;
    } else if (opts.Thunderbolt) {
        out("  /thunderbolt: installed without asking.\n");
    } else if (opts.List) {
        out("  /list: it would be asked; Enter would answer %s.\n",
            def ? "Y" : "N");
    } else {
        out("  /yes: %s.\n", tbInclude ? "Y, a controller is present"
                                       : "N, none was found (/thunderbolt "
                                         "installs it anyway)");
    }
    out("\n");
    logonly("thunderbolt: present %d, installed %d, asked %d, default %d, "
            "include %d\n", tbNhiPresent, installed, ask, def, tbInclude);
    if (!tbInclude) {
        for (k = INST_INF_CM; k <= INST_INF_CMF; k++) {
            free(infText[k]);
            infText[k] = NULL;
        }
        plan();
    }
}

/* After a Yes with no controller present (section 8.5). */
static void tb_later_note(const char *dir)
{
    if (!haveCm || !tbInclude || tbNhiPresent || opts.List) {
        return;
    }
    if (envNow.Nt && !tbStaged) {
        out("\nThe Thunderbolt driver was not staged (see above): run "
            "INSTALL.BAT again once the\ncontroller appears.\n");
    } else if (envNow.Nt) {
        out("\nThe Thunderbolt driver is staged: when the controller appears, "
            "Windows installs\nit by itself. Run INSTALL.BAT again then, so "
            "the Thunderbolt ports get their\nfilter too.\n");
    } else {
        out("\nThe Thunderbolt driver is in %s: when the controller appears, the "
            "hardware\nwizard finds xhcicm.inf there by itself (\"Search for "
            "the best driver\", no box\nticked). When Windows then asks for "
            "the \"xhci98 xHCI Host Controller Disk\", type\n%s under "
            "\"Copy files from\" and OK.\n", dir, dir);
    }
}

/* ======================================================================== */
/* main                                                                      */
/* ======================================================================== */

static void usage(void)
{
    printf(
"Usage: INSTALL.BAT [switches]   (or %s [/dir release-...] [switches])\n"
"\n"
"  (none)        install or update the package's drivers, after one question\n"
"  /list         show what would be done; change nothing\n"
"  /yes          ask nothing: go on, keep every value /set does not name\n"
"  /defaults     ask no option question; keep every value\n"
"  /set Name=Value   set one of the driver's options (repeatable)\n"
"  /force        reinstall this release, or install it over a newer one\n"
"  /thunderbolt  install the Thunderbolt 3 / USB4 support without asking\n"
"  /notray      do not start the xHCI98 USB Tree at login (and stop doing so)\n"
"  /restage      Vista and 7: replace a stored build with the same DriverVer\n"
"  /uninstall    remove the drivers (asks for the word REMOVE)\n"
"  /debug        (INSTALL.BAT) the debug- directory, for troubleshooting\n"
"  /nopause      do not wait for Enter at the end\n"
"  /log path     write the log there (default XHCIINST.LOG on the system "
"drive)\n"
"  /devices      show the connected Thunderbolt and USB4 devices\n"
"  /eject [N]    remove one of them safely (/eject id=<instance id> for "
"scripts)\n"
"\n"
"Exit codes: 0 done, nothing needed a change; 1 refused or cancelled, nothing\n"
"changed; 2 failed part-way (see the log); 3 done, restart needed; 4 done,\n"
"steps left for you.\n",
        INST_MY_NAME);
}

static int finish(int code)
{
    char line[8];
    int last;

    /* Owner, 2026-10-15: a restart after every change, on every system,
     * whatever Windows itself says. The last line and the exit code agree
     * (record 23 section 7.4, orchestrator decision 2026-10-15): a finished
     * run that changed anything asks for the restart and exits 3; a run
     * that stopped short says to finish the steps above first. */
    if (opts.Devices || opts.Eject) {
        /* The device modes' exception (record 23 section 7.4, record 24
         * section 8.2): the eject's own last line, no restart. */
        out("\n%s\n", devLast != NULL ? devLast : "No changes were made.");
    } else if (!opts.SelfTest) {
        code = inst_final(flagChanged, opts.List, code, &last);
        out("\n%s\n", inst_final_text(last));
    }
    out("\nExit code %d. The log is %s.\n", code, logPath[0] ? logPath : "(none)");
    logonly("exit %d\n", code);
    if (logFile != NULL) {
        fclose(logFile);
        logFile = NULL;
    }
    if (!opts.NoPause && !opts.List && !opts.Devices) {
        printf("Press Enter to close this window.");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
        }
    }
    return code;
}

int main(int argc, char **argv)
{
    char err[256];
    PFN_ISWOW64 isWow64;
    BOOL wow = FALSE;
    SC_HANDLE scm;
    int i;

    if (inst_parse_args(argc, argv, &opts, err, sizeof(err)) != 0) {
        printf("xhciinst: %s\n\n", err);
        usage();
        return INST_EXIT_REFUSED;
    }
    if (opts.SelfTest) {
        return selftest();
    }
    if ((!opts.List && !opts.Devices) || opts.Log[0] != '\0') {
        log_open();
    }
    out("xhciinst %s - %s\n", XHCIINST_VERSION,
        (opts.Devices || opts.Eject)
            ? "Thunderbolt and USB4 devices"
            : "install, update or uninstall the xhci98 package");
    out("Log: %s\n", logPath[0] ? logPath : "none (/list and /devices write none unless /log names one)");
    for (i = 0; i < argc; i++) {
        logonly("argv[%d] = %s\n", i, argv[i]);
    }

    memset(&osv, 0, sizeof(osv));
    osv.dwOSVersionInfoSize = sizeof(osv);
    if (!GetVersionExA(&osv)) {
        out("The Windows version could not be read.\n");
        return finish(INST_EXIT_REFUSED);
    }
    out("Windows %lu.%lu.%lu %s\n\n", (unsigned long)osv.dwMajorVersion,
        (unsigned long)osv.dwMinorVersion,
        (unsigned long)(osv.dwBuildNumber & 0xFFFFUL), osv.szCSDVersion);
    memset(&envNow, 0, sizeof(envNow));
    envNow.Arch = INST_MY_ARCH;
    envNow.Major = osv.dwMajorVersion;
    envNow.Minor = osv.dwMinorVersion;
    if (osv.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
        envNow.Nt = 0;
        /* 98 SE is 4.10.2222, ME 4.90.3000; 98 (4.10.1998) and 95 are not
         * supported (record 23 section 4.3). */
        if (!(osv.dwMajorVersion == 4 &&
              ((osv.dwMinorVersion == 10 &&
                (osv.dwBuildNumber & 0xFFFFUL) >= 2222UL) ||
               osv.dwMinorVersion == 90))) {
            out("This is not Windows 98 SE or ME.\n");
            return finish(INST_EXIT_REFUSED);
        }
    } else if (osv.dwPlatformId == VER_PLATFORM_WIN32_NT) {
        envNow.Nt = 1;
        if (osv.dwMajorVersion < 5 ||
            (osv.dwMajorVersion == 6 && osv.dwMinorVersion > 1) ||
            osv.dwMajorVersion > 6) {
            out("This Windows is not one this package supports (2000, XP, "
                "XP x64, Vista, 7).\n");
            return finish(INST_EXIT_REFUSED);
        }
        isWow64 = (PFN_ISWOW64)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                              "IsWow64Process");
        if (isWow64 != NULL && !isWow64(GetCurrentProcess(), &wow)) {
            wow = FALSE;
        }
        if (wow) {
            out("This is 64-bit Windows: the 32-bit tool cannot %s here.\n",
                (opts.Devices || opts.Eject) ? "read the devices"
                                             : "install drivers");
            out("REFUSED: run INSTALL.BAT, which runs %s.\n", INST_OTHER_NAME);
            return finish(INST_EXIT_REFUSED);
        }
    } else {
        out("Not a Windows this tool knows.\n");
        return finish(INST_EXIT_REFUSED);
    }
    if (!envNow.Nt && INST_MY_ARCH != INST_ARCH_X86) {
        return finish(INST_EXIT_REFUSED);
    }
    for (i = 0; i < opts.SetCount; i++) {
        if ((inst_value_set_mask(opts.SetIndex[i], &envNow) &
             (1UL << opts.SetValue[i])) == 0) {
            out("REFUSED: /set %s=%lu does not work on this Windows.\n",
                instValues[opts.SetIndex[i]].Name,
                (unsigned long)opts.SetValue[i]);
            return finish(INST_EXIT_REFUSED);
        }
    }

    devs = (DEVREC *)calloc(INST_MAX_DEVS, sizeof(DEVREC));
    if (devs == NULL) {
        out("Out of memory.\n");
        return finish(INST_EXIT_REFUSED);
    }
    if (opts.Devices || opts.Eject) {
        /* Record 24: no package is read, and no administrator test is made
         * first (each call's answer is reported). */
        free(devs);
        devs = NULL;
        tbn = (TBNODE *)calloc(INST_MAX_DEVS, sizeof(TBNODE));
        if (tbn == NULL) {
            out("Out of memory.\n");
            return finish(INST_EXIT_REFUSED);
        }
        load_routines();
        tb_routines();
        if (!tb_lock()) {
            return finish(INST_EXIT_REFUSED);
        }
        return finish(opts.Devices ? run_devices() : run_eject());
    }
    if (!check_package()) {
        return finish(INST_EXIT_REFUSED);
    }
    load_routines();
    load_ids();
    if (opts.List) {
        print_routines();
    }
    if (envNow.Nt && !opts.List) {
        scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (scm == NULL) {
            out("REFUSED: this needs an administrator (error %lu). Run it as "
                "an administrator.\n", (unsigned long)GetLastError());
            return finish(INST_EXIT_REFUSED);
        }
        CloseServiceHandle(scm);
    }
    if (envNow.Nt && (api.GetClassDevs == NULL || api.EnumInfo == NULL ||
                      api.GetInstanceId == NULL || api.GetRegProp == NULL ||
                      api.Destroy == NULL || api.Status == NULL)) {
        out("REFUSED: the device routines this needs are missing (the log "
            "names them).\n");
        return finish(INST_EXIT_REFUSED);
    }
    if (opts.Uninstall) {
        return finish(envNow.Nt ? run_uninstall_nt() : run_uninstall_9x());
    }
    if (envNow.Nt && !opts.List && api.UpdateDriver == NULL) {
        out("REFUSED: UpdateDriverForPlugAndPlayDevicesA is missing.\n");
        return finish(INST_EXIT_REFUSED);
    }
    return finish(envNow.Nt ? run_install_nt() : run_install_9x());
}
