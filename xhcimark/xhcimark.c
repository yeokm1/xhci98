/*
 * xhcimark.c - XHCIMARK.EXE (x86) and XHCIMK64.EXE (x64): the safe-removal
 * mark service, xhcimark (design record 21 section 7.4, revision 3;
 * roadmap-usb4.md tasks 42-0.4 and 42-A.5; the owner's decision of
 * 2026-10-18 that the writer of the mark is a privileged NT service).
 *
 * WHAT IT DOES, and nothing else. A device behind a verified tunnel port
 * of xhcicm.sys carries the driver's child filter only while the stack the
 * bridge filter attached it to lasts; a disable and enable, a driver
 * install or update rebuild that stack from the registry and lose it. The
 * mark is a device-specific LowerFilters value naming xhcicm on the unit's
 * own devnode, which the PnP manager reads at every rebuild. This service
 * writes it for each unit the driver reports on an active member, after
 * checking the notice against Windows (the member a devnode at the active
 * member's location, the unit a child devnode of it at the entry's
 * location, never a protected device, the name not yet there), records
 * what it wrote under Services\xhcimark\Marks before it writes, removes
 * its own name when the unit leaves or is no longer behind an active
 * member of a verifiable domain, catches up at its start on departures it
 * missed (and once a day on marks left unrecorded), restarts a first plug
 * the rebuild overtook through the documented, veto-respecting property
 * change (DIF_PROPERTYCHANGE, DICS_PROPCHANGE) once per unit, and writes
 * the driver's eject confirmation for a unit Windows holds for eject
 * (problem 47) on Windows XP and later. It never offers a general registry
 * write and answers nothing but the service controller.
 *
 * A DOMAIN THAT CANNOT BE VERIFIED neither writes nor removes (decision
 * M11): an NHI not answering, inert, contained, stopping, or an epoch
 * that changed between the two reads skips the poll for its units.
 *
 * THE MUTEXES. Global\Xhci98MarkWrite around every record-and-write and
 * every removal, which the installer takes for its own inventory and
 * removals; Global\XhciinstTbAsk, the tools' eject mutex, around the
 * restart's fresh reads and its call, so no restart runs inside an eject
 * of the tools and no eject inside a restart.
 *
 * TARGETS AND BUILDS. Windows 2000 to 7, both architectures, installed by
 * the installer only with the Thunderbolt 3 / USB4 component, its
 * executable in %SystemRoot%\system32 (the native one: the 64-bit build on
 * x64). ANSI C89 built with the in-repo MSVC 6.0 as XHCIMARK.EXE and with
 * WDK 7.1's amd64 compiler as XHCIMK64.EXE (build.cmd); every SetupAPI and
 * configuration-manager routine is looked up at run time, so the import
 * table names KERNEL32 and ADVAPI32 alone. The 32-bit build under WOW64
 * refuses to run: device installation actions from a 32-bit process on
 * 64-bit Windows fail (ERROR_IN_WOW64).
 *
 * SWITCHES. None: run as the service (the service controller starts it).
 * -selftest: the pure half over canned answers. -once: one poll in the
 * console, as the service would make it, for a test guest. -register and
 * -unregister: the service's registration by hand in a test guest, before
 * the installer's hooks (42-A.5's legs); the installer does this in a
 * release. Never on a development host.
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "../src/xhci_version.h"
#include "../src/cm/cm_query.h"
#include "mark_core.h"

#define XHCIMARK_VERSION XHCI_VER_STR

#define MK_SERVICE          "xhcimark"
#define MK_DISPLAY          "xHCI98 Thunderbolt safe-removal mark service"
#define MK_SVC_KEY          "SYSTEM\\CurrentControlSet\\Services\\xhcimark"
#define MK_MARKS_KEY        MK_SVC_KEY "\\Marks"
#define MK_PARAMS_KEY       MK_SVC_KEY "\\Parameters"
#define MK_WRITE_MUTEX      "Global\\Xhci98MarkWrite"
#define MK_EJECT_MUTEX      "Global\\XhciinstTbAsk"
#define MK_LOG_DIR          "\\LogFiles\\xhci98"
#define MK_LOG_NAME         "\\XHCIMARK.LOG"
#define MK_LOG_DEFAULT      262144UL
#define MK_DAY_MS           86400000UL
#define MK_RETRY_MS         60000UL

#define MK_MAX_NODES        2048
#define MK_MAX_NHIS         16
#define MK_MAX_TRIED        128

/* The configuration manager's numbers (WDK 7.1 cfgmgr32.h and cfg.h, the
 * same in the Windows 2000 DDK's). */
#define MK_CR_SUCCESS               0x00000000UL
#define MK_CR_NO_SUCH_DEVNODE       0x0000000DUL
#define MK_CR_BUFFER_SMALL          0x0000001AUL
#define MK_CR_NO_SUCH_VALUE         0x00000025UL
#define MK_CM_LOCATE_NORMAL         0x00000000UL
#define MK_CM_LOCATE_PHANTOM        0x00000001UL
#define MK_CM_DRP_HARDWAREID        0x00000002UL
#define MK_CM_DRP_COMPATIBLEIDS     0x00000003UL
#define MK_CM_DRP_SERVICE           0x00000005UL
#define MK_CM_DRP_DRIVER            0x0000000AUL
#define MK_CM_DRP_LOWERFILTERS      0x00000013UL
#define MK_CM_DRP_BUSNUMBER         0x00000016UL
#define MK_CM_DRP_ADDRESS           0x0000001DUL
#define MK_DN_STARTED               0x00000008UL
#define MK_DN_HAS_PROBLEM           0x00000400UL
#define MK_PROB_HELD_FOR_EJECT      0x0000002FUL
#define MK_SPDRP_LOWERFILTERS       0x00000012UL
#define MK_DIF_PROPERTYCHANGE       0x00000012UL
#define MK_DICS_PROPCHANGE          0x00000003UL
#define MK_DICS_FLAG_GLOBAL         0x00000001UL
#define MK_DI_NEEDRESTART           0x00000080UL
#define MK_DI_NEEDREBOOT            0x00000100UL

/* ======================================================================== */
/* The routines looked up at run time                                       */
/* ======================================================================== */

typedef DWORD (WINAPI *PFN_CM_LOCATE)(DWORD *, char *, ULONG);
typedef DWORD (WINAPI *PFN_CM_STATUS)(ULONG *, ULONG *, DWORD, ULONG);
typedef DWORD (WINAPI *PFN_CM_REL)(DWORD *, DWORD, ULONG);
typedef DWORD (WINAPI *PFN_CM_ID)(DWORD, char *, ULONG, ULONG);
typedef DWORD (WINAPI *PFN_CM_GETPROP)(DWORD, ULONG, ULONG *, void *, ULONG *,
                                       ULONG);
typedef DWORD (WINAPI *PFN_CM_SETPROP)(DWORD, ULONG, void *, ULONG, ULONG);
typedef DWORD (WINAPI *PFN_CM_WAIT)(DWORD);
typedef HDEVINFO (WINAPI *PFN_GETCLASSDEVS)(const GUID *, PCSTR, HWND, DWORD);
typedef BOOL (WINAPI *PFN_ENUMINFO)(HDEVINFO, DWORD, PSP_DEVINFO_DATA);
typedef BOOL (WINAPI *PFN_GETINSTID)(HDEVINFO, PSP_DEVINFO_DATA, PSTR, DWORD,
                                     PDWORD);
typedef BOOL (WINAPI *PFN_GETREGPROP)(HDEVINFO, PSP_DEVINFO_DATA, DWORD,
                                      PDWORD, PBYTE, DWORD, PDWORD);
typedef BOOL (WINAPI *PFN_DESTROY)(HDEVINFO);
typedef HDEVINFO (WINAPI *PFN_CREATELIST)(const GUID *, HWND);
typedef BOOL (WINAPI *PFN_OPENINFO)(HDEVINFO, PCSTR, HWND, DWORD,
                                    PSP_DEVINFO_DATA);
typedef BOOL (WINAPI *PFN_SETCIPARAMS)(HDEVINFO, PSP_DEVINFO_DATA,
                                       PSP_CLASSINSTALL_HEADER, DWORD);
typedef BOOL (WINAPI *PFN_CALLCI)(DI_FUNCTION, HDEVINFO, PSP_DEVINFO_DATA);
typedef BOOL (WINAPI *PFN_GETDIPARAMS)(HDEVINFO, PSP_DEVINFO_DATA,
                                       PSP_DEVINSTALL_PARAMS_A);
typedef BOOL (WINAPI *PFN_ISWOW64)(HANDLE, PBOOL);

static struct {
    HMODULE SetupApi;
    HMODULE CfgMgr;
    PFN_CM_LOCATE Locate;
    PFN_CM_STATUS Status;
    PFN_CM_REL Child;
    PFN_CM_REL Sibling;
    PFN_CM_ID DeviceId;
    PFN_CM_GETPROP GetProp;
    PFN_CM_SETPROP SetProp;
    PFN_CM_WAIT WaitInstall;
    PFN_GETCLASSDEVS GetClassDevs;
    PFN_ENUMINFO EnumInfo;
    PFN_GETINSTID GetInstanceId;
    PFN_GETREGPROP GetRegProp;
    PFN_DESTROY Destroy;
    PFN_CREATELIST CreateList;
    PFN_OPENINFO OpenInfo;
    PFN_SETCIPARAMS SetCiParams;
    PFN_CALLCI CallCi;
    PFN_GETDIPARAMS GetDiParams;
} api;

/* ======================================================================== */
/* State                                                                    */
/* ======================================================================== */

typedef struct _MK_NODE {
    DWORD Dn;
    int Parent;
    char Id[INST_ID_CHARS];
    char Hw[1024];
    size_t HwBytes;
    char Compat[1024];
    size_t CompatBytes;
    char Service[64];
    ULONG Bus;
    ULONG Address;
    int HasLoc;
    ULONG Status;
    ULONG Problem;
} MK_NODE;

typedef struct _MK_NHI {
    int Node;
    int DevOk;
    int Answered;
    int Verifiable;
    char Driver[128];
    MK_QUERY Q;
    MK_MARKS M;
} MK_NHI;

typedef struct _MK_TRIED {
    INST_U32 EpochLo;
    INST_U32 EpochHi;
    INST_U32 Serial;
    DWORD At;               /* the last write attempt or restart          */
    int Restarted;          /* the restart was made (once per serial)     */
    int Outcome;            /* its outcome was logged                     */
    int Confirmed;          /* the eject confirmation was written         */
    int WriteFailed;
    int Gated;              /* a restart gate's refusal was logged        */
} MK_TRIED;

static MK_NODE *nodes;
static int nodeCount;
static int walkBad;
static MK_NHI nhis[MK_MAX_NHIS];
static int nhiCount;
static MK_TRIED tried[MK_MAX_TRIED];
static int triedCount;

static OSVERSIONINFOA osv;
static DWORD paramRestart = 1;
static DWORD paramLogBytes = MK_LOG_DEFAULT;
static char logPath[MAX_PATH];
static int logConsole;
static HANDLE stopEvent;
static SERVICE_STATUS_HANDLE statusHandle;
static SERVICE_STATUS status;
static DWORD lastInventory;
static int inventoryDone;

/* ======================================================================== */
/* The log                                                                  */
/* ======================================================================== */

static void mk_log(const char *fmt, ...)
{
    char line[1024];
    char stamp[64];
    SYSTEMTIME t;
    va_list ap;
    HANDLE h;
    DWORD size;
    DWORD wrote;
    size_t n;

    GetLocalTime(&t);
    sprintf(stamp, "%04u-%02u-%02u %02u:%02u:%02u ", t.wYear, t.wMonth, t.wDay,
            t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    _vsnprintf(line, sizeof(line) - 3, fmt, ap);
    va_end(ap);
    line[sizeof(line) - 3] = '\0';
    n = strlen(line);
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = '\0';
    if (logConsole) {
        printf("%s%s", stamp, line);
    }
    if (logPath[0] == '\0') {
        return;
    }
    h = CreateFileA(logPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    size = GetFileSize(h, NULL);
    if (size == 0xFFFFFFFFUL ||
        size + strlen(stamp) + n > paramLogBytes) {
        /* Full: rewritten from the top. */
        SetFilePointer(h, 0, NULL, FILE_BEGIN);
        SetEndOfFile(h);
    } else {
        SetFilePointer(h, 0, NULL, FILE_END);
    }
    WriteFile(h, stamp, (DWORD)strlen(stamp), &wrote, NULL);
    WriteFile(h, line, (DWORD)n, &wrote, NULL);
    CloseHandle(h);
}

static void mk_log_open(void)
{
    char dir[MAX_PATH];
    UINT n;

    n = GetSystemDirectoryA(dir, MAX_PATH - 64);
    if (n == 0 || n >= MAX_PATH - 64) {
        return;
    }
    strcat(dir, "\\LogFiles");
    CreateDirectoryA(dir, NULL);
    strcat(dir, "\\xhci98");
    CreateDirectoryA(dir, NULL);
    sprintf(logPath, "%s%s", dir, MK_LOG_NAME);
}

static DWORD mk_reg_dword(HKEY root, const char *path, const char *name,
                          DWORD dflt)
{
    HKEY k;
    DWORD type;
    DWORD v;
    DWORD n = sizeof(v);

    if (RegOpenKeyExA(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return dflt;
    }
    if (RegQueryValueExA(k, name, NULL, &type, (LPBYTE)&v, &n) !=
            ERROR_SUCCESS || type != REG_DWORD || n != sizeof(v)) {
        v = dflt;
    }
    RegCloseKey(k);
    return v;
}

static void mk_params(void)
{
    paramRestart = mk_reg_dword(HKEY_LOCAL_MACHINE, MK_PARAMS_KEY, "Restart",
                                1);
    paramLogBytes = mk_reg_dword(HKEY_LOCAL_MACHINE, MK_PARAMS_KEY,
                                 "LogBytes", MK_LOG_DEFAULT);
    if (paramLogBytes < 4096UL) {
        paramLogBytes = 4096UL;
    }
}

/* ======================================================================== */
/* The routines                                                             */
/* ======================================================================== */

static FARPROC mk_proc(HMODULE m, const char *name, int *missing)
{
    FARPROC p = (m != NULL) ? GetProcAddress(m, name) : NULL;

    if (p == NULL) {
        mk_log("not available: %s", name);
        (*missing)++;
    }
    return p;
}

/* How many routines the service needs and cannot have (each logged).
 * CMP_WaitNoPendingInstallEvents is optional: without it the restart is
 * never made ("cannot tell"). */
static int mk_load(void)
{
    int missing = 0;
    int optional = 0;

    api.SetupApi = LoadLibraryA("setupapi.dll");
    api.CfgMgr = LoadLibraryA("cfgmgr32.dll");
    api.Locate = (PFN_CM_LOCATE)mk_proc(api.CfgMgr, "CM_Locate_DevNodeA", &missing);
    api.Status = (PFN_CM_STATUS)mk_proc(api.CfgMgr, "CM_Get_DevNode_Status", &missing);
    api.Child = (PFN_CM_REL)mk_proc(api.CfgMgr, "CM_Get_Child", &missing);
    api.Sibling = (PFN_CM_REL)mk_proc(api.CfgMgr, "CM_Get_Sibling", &missing);
    api.DeviceId = (PFN_CM_ID)mk_proc(api.CfgMgr, "CM_Get_Device_IDA", &missing);
    api.GetProp = (PFN_CM_GETPROP)mk_proc(api.CfgMgr,
                      "CM_Get_DevNode_Registry_PropertyA", &missing);
    api.SetProp = (PFN_CM_SETPROP)mk_proc(api.CfgMgr,
                      "CM_Set_DevNode_Registry_PropertyA", &missing);
    api.WaitInstall = (PFN_CM_WAIT)mk_proc(api.CfgMgr,
                          "CMP_WaitNoPendingInstallEvents", &optional);
    api.GetClassDevs = (PFN_GETCLASSDEVS)mk_proc(api.SetupApi,
                           "SetupDiGetClassDevsA", &missing);
    api.EnumInfo = (PFN_ENUMINFO)mk_proc(api.SetupApi, "SetupDiEnumDeviceInfo",
                                         &missing);
    api.GetInstanceId = (PFN_GETINSTID)mk_proc(api.SetupApi,
                            "SetupDiGetDeviceInstanceIdA", &missing);
    api.GetRegProp = (PFN_GETREGPROP)mk_proc(api.SetupApi,
                         "SetupDiGetDeviceRegistryPropertyA", &missing);
    api.Destroy = (PFN_DESTROY)mk_proc(api.SetupApi,
                      "SetupDiDestroyDeviceInfoList", &missing);
    api.CreateList = (PFN_CREATELIST)mk_proc(api.SetupApi,
                         "SetupDiCreateDeviceInfoList", &missing);
    api.OpenInfo = (PFN_OPENINFO)mk_proc(api.SetupApi, "SetupDiOpenDeviceInfoA",
                                         &missing);
    api.SetCiParams = (PFN_SETCIPARAMS)mk_proc(api.SetupApi,
                          "SetupDiSetClassInstallParamsA", &missing);
    api.CallCi = (PFN_CALLCI)mk_proc(api.SetupApi, "SetupDiCallClassInstaller",
                                     &missing);
    api.GetDiParams = (PFN_GETDIPARAMS)mk_proc(api.SetupApi,
                          "SetupDiGetDeviceInstallParamsA", &missing);
    return missing;
}

/* ======================================================================== */
/* The tree                                                                 */
/* ======================================================================== */

static void mk_prop_text(DWORD dn, ULONG prop, char *out, size_t cap,
                         size_t *bytes)
{
    ULONG type = 0;
    ULONG n = (ULONG)cap - 2;

    memset(out, 0, cap);
    if (api.GetProp(dn, prop, &type, out, &n, 0) != MK_CR_SUCCESS) {
        memset(out, 0, cap);
        n = 0;
    }
    out[cap - 1] = '\0';
    out[cap - 2] = '\0';
    if (bytes != NULL) {
        *bytes = n;
    }
}

static int mk_prop_dword(DWORD dn, ULONG prop, ULONG *v)
{
    ULONG type = 0;
    ULONG n = sizeof(*v);

    return api.GetProp(dn, prop, &type, v, &n, 0) == MK_CR_SUCCESS &&
           n == sizeof(*v);
}

static void mk_walk_from(DWORD dn, int parent, int depth)
{
    DWORD child;
    DWORD sib;
    DWORD cr;
    MK_NODE *t;
    int me;

    for (;;) {
        if (nodeCount >= MK_MAX_NODES || depth > 64) {
            walkBad++;
            return;
        }
        me = nodeCount++;
        t = &nodes[me];
        memset(t, 0, sizeof(*t));
        t->Dn = dn;
        t->Parent = parent;
        if (api.DeviceId(dn, t->Id, sizeof(t->Id) - 1, 0) != MK_CR_SUCCESS) {
            t->Id[0] = '\0';
            walkBad++;
        }
        mk_prop_text(dn, MK_CM_DRP_HARDWAREID, t->Hw, sizeof(t->Hw),
                     &t->HwBytes);
        mk_prop_text(dn, MK_CM_DRP_COMPATIBLEIDS, t->Compat,
                     sizeof(t->Compat), &t->CompatBytes);
        mk_prop_text(dn, MK_CM_DRP_SERVICE, t->Service, sizeof(t->Service),
                     NULL);
        t->HasLoc = mk_prop_dword(dn, MK_CM_DRP_BUSNUMBER, &t->Bus) &&
                    mk_prop_dword(dn, MK_CM_DRP_ADDRESS, &t->Address);
        if (api.Status(&t->Status, &t->Problem, dn, 0) != MK_CR_SUCCESS) {
            t->Status = 0;
            t->Problem = 0;
        }
        cr = api.Child(&child, dn, 0);
        if (cr == MK_CR_SUCCESS) {
            mk_walk_from(child, me, depth + 1);
        } else if (cr != MK_CR_NO_SUCH_DEVNODE) {
            walkBad++;
        }
        cr = api.Sibling(&sib, dn, 0);
        if (cr != MK_CR_SUCCESS) {
            if (cr != MK_CR_NO_SUCH_DEVNODE) {
                walkBad++;
            }
            return;
        }
        dn = sib;
    }
}

static int mk_walk(void)
{
    DWORD root;

    nodeCount = 0;
    walkBad = 0;
    if (api.Locate(&root, NULL, MK_CM_LOCATE_NORMAL) != MK_CR_SUCCESS) {
        return 0;
    }
    mk_walk_from(root, -1, 0);
    return 1;
}

static int mk_find_id(const char *id)
{
    int i;

    for (i = 0; i < nodeCount; i++) {
        if (inst_eq(nodes[i].Id, id)) {
            return i;
        }
    }
    return -1;
}

static int mk_is_pci(const MK_NODE *t)
{
    return inst_starts(t->Id, "PCI\\");
}

static ULONG mk_addr_of(INST_U32 bdf)
{
    return (((bdf >> 3) & 0x1FUL) << 16) | (bdf & 7UL);
}

/* The one PCI devnode at `bdf`, or -1 for none or two. */
static int mk_find_loc(INST_U32 bdf)
{
    int i;
    int found = -1;

    for (i = 0; i < nodeCount; i++) {
        if (mk_is_pci(&nodes[i]) && nodes[i].HasLoc &&
            nodes[i].Bus == ((bdf >> 8) & 0xFFUL) &&
            nodes[i].Address == mk_addr_of(bdf)) {
            if (found >= 0) {
                return -1;
            }
            found = i;
        }
    }
    return found;
}

/* The one child devnode of `port` at bus/address, or -1. */
static int mk_find_unit(int port, ULONG bus, ULONG address)
{
    int i;
    int found = -1;

    for (i = 0; i < nodeCount; i++) {
        if (nodes[i].Parent == port && nodes[i].HasLoc &&
            nodes[i].Bus == bus && nodes[i].Address == address) {
            if (found >= 0) {
                return -1;
            }
            found = i;
        }
    }
    return found;
}

static int *mk_protect(void)
{
    static int parent[MK_MAX_NODES];
    static int isNhi[MK_MAX_NODES];
    static int isUsbHc[MK_MAX_NODES];
    static int prot[MK_MAX_NODES];
    int i;

    for (i = 0; i < nodeCount; i++) {
        parent[i] = nodes[i].Parent;
        isNhi[i] = inst_tb_is_nhi(nodes[i].Hw, nodes[i].HwBytes,
                                  nodes[i].Compat, nodes[i].CompatBytes,
                                  nodes[i].Service);
        isUsbHc[i] = inst_tb_is_usbhc(nodes[i].Hw, nodes[i].HwBytes,
                                      nodes[i].Compat, nodes[i].CompatBytes,
                                      nodes[i].Service);
    }
    inst_tb_protect(parent, isNhi, isUsbHc, nodeCount, prot);
    return prot;
}

/* ======================================================================== */
/* The driver's answers                                                     */
/* ======================================================================== */

static int mk_ioctl(HANDLE h, DWORD code, unsigned char **buf, DWORD *got,
                    int marks)
{
    DWORD size = marks ? (DWORD)CM_QM_MAX_BYTES : (DWORD)CM_Q_ASK_BYTES;
    int tries;
    INST_U32 need;
    int r;

    for (tries = 0; tries < 3; tries++) {
        unsigned char *b = (unsigned char *)realloc(*buf, size);
        if (b == NULL) {
            return 0;
        }
        *buf = b;
        *got = 0;
        if (!DeviceIoControl(h, code, NULL, 0, b, size, got, NULL) ||
            *got > size) {
            return 0;
        }
        if (marks) {
            MK_MARKS tmp;
            r = mk_marks_parse(b, *got, &tmp, &need);
        } else {
            MK_QUERY tmp;
            r = mk_query_parse(b, *got, &tmp, &need);
        }
        if (r == -1 && need > size && need <= CM_Q_MAX_BYTES) {
            size = need;
            continue;
        }
        return r == 1;
    }
    return 0;
}

/* Both answers of one NHI, the query's instance id checked against the
 * devnode's. */
static int mk_read_nhi(MK_NHI *h)
{
    char path[INST_ID_CHARS + 96];
    char name[64];
    char dev[80];
    HKEY k;
    DWORD type;
    DWORD n;
    HANDLE dh;
    unsigned char *b = NULL;
    DWORD got;
    INST_U32 need;
    int ok = 0;

    memset(&h->Q, 0, sizeof(h->Q));
    memset(&h->M, 0, sizeof(h->M));
    sprintf(path, "SYSTEM\\CurrentControlSet\\Enum\\%s\\Device Parameters",
            nodes[h->Node].Id);
    name[0] = '\0';
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k) ==
        ERROR_SUCCESS) {
        n = sizeof(name) - 1;
        if (RegQueryValueExA(k, CM_Q_NAME_VALUE, NULL, &type, (LPBYTE)name,
                             &n) != ERROR_SUCCESS || type != REG_SZ) {
            name[0] = '\0';
        }
        name[sizeof(name) - 1] = '\0';
        RegCloseKey(k);
    }
    if (name[0] == '\0' || strlen(name) > 16) {
        return 0;
    }
    sprintf(dev, "\\\\.\\%s", name);
    dh = CreateFileA(dev, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                     OPEN_EXISTING, 0, NULL);
    if (dh == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (mk_ioctl(dh, CM_Q_IOCTL, &b, &got, 0) &&
        mk_query_parse(b, got, &h->Q, &need) == 1 &&
        inst_eq(h->Q.Id, nodes[h->Node].Id) &&
        mk_ioctl(dh, CM_QM_IOCTL, &b, &got, 1) &&
        mk_marks_parse(b, got, &h->M, &need) == 1) {
        ok = 1;
    }
    CloseHandle(dh);
    free(b);
    if (!ok) {
        memset(&h->Q, 0, sizeof(h->Q));
        memset(&h->M, 0, sizeof(h->M));
    }
    return ok;
}

/* Every NHI bound to xhcicm, read; 1 when any answered. */
static int mk_read_nhis(void)
{
    int i;
    int any = 0;

    nhiCount = 0;
    for (i = 0; i < nodeCount && nhiCount < MK_MAX_NHIS; i++) {
        MK_NHI *h;
        if (!inst_eq(nodes[i].Service, MK_NAME) ||
            !inst_tb_is_nhi(nodes[i].Hw, nodes[i].HwBytes, nodes[i].Compat,
                            nodes[i].CompatBytes, nodes[i].Service)) {
            continue;
        }
        h = &nhis[nhiCount++];
        memset(h, 0, sizeof(*h));
        h->Node = i;
        h->DevOk = (nodes[i].Status & MK_DN_STARTED) != 0 &&
                   (nodes[i].Status & MK_DN_HAS_PROBLEM) == 0;
        mk_prop_text(nodes[i].Dn, MK_CM_DRP_DRIVER, h->Driver,
                     sizeof(h->Driver), NULL);
        h->Answered = mk_read_nhi(h);
        h->Verifiable = h->Answered && mk_verifiable(h->DevOk, &h->Q, &h->M);
        if (h->Answered) {
            any = 1;
        }
    }
    return any;
}

/* ======================================================================== */
/* The records                                                              */
/* ======================================================================== */

static int mk_rec_set(const char *id, INST_U32 member, INST_U32 hi,
                      INST_U32 lo, int state)
{
    HKEY k;
    char text[64];
    LONG r;

    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, MK_MARKS_KEY, 0, NULL, 0,
                        KEY_READ | KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) {
        return 0;
    }
    mk_rec_format(text, sizeof(text), member, hi, lo, state);
    r = RegSetValueExA(k, id, 0, REG_SZ, (const BYTE *)text,
                       (DWORD)strlen(text) + 1);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static int mk_rec_get(const char *id, char *text, size_t cap)
{
    HKEY k;
    DWORD type;
    DWORD n = (DWORD)cap - 1;
    LONG r;

    text[0] = '\0';
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, MK_MARKS_KEY, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return 0;
    }
    r = RegQueryValueExA(k, id, NULL, &type, (LPBYTE)text, &n);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_SZ) {
        text[0] = '\0';
        return 0;
    }
    text[n < cap ? n : cap - 1] = '\0';
    return 1;
}

static void mk_rec_delete(const char *id)
{
    HKEY k;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, MK_MARKS_KEY, 0, KEY_WRITE, &k) ==
        ERROR_SUCCESS) {
        RegDeleteValueA(k, id);
        RegCloseKey(k);
    }
}

/* ======================================================================== */
/* The mutexes                                                              */
/* ======================================================================== */

typedef struct _MK_SA {
    SECURITY_ATTRIBUTES Sa;
    SECURITY_DESCRIPTOR Sd;
    BYTE Acl[512];
    int Ok;
} MK_SA;

/* SYNCHRONIZE and MUTEX_MODIFY_STATE to everyone, full access to SYSTEM
 * and the Administrators (record 25 section 6.3's shared mutex). */
static void mk_make_sa(MK_SA *s, DWORD everyone)
{
    SID_IDENTIFIER_AUTHORITY world = SECURITY_WORLD_SID_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID sWorld = NULL;
    PSID sSystem = NULL;
    PSID sAdmins = NULL;

    memset(s, 0, sizeof(*s));
    if (AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0,
                                 0, 0, 0, &sWorld) &&
        AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0,
                                 0, 0, 0, 0, &sSystem) &&
        AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &sAdmins) &&
        InitializeAcl((PACL)s->Acl, sizeof(s->Acl), ACL_REVISION) &&
        AddAccessAllowedAce((PACL)s->Acl, ACL_REVISION, everyone, sWorld) &&
        AddAccessAllowedAce((PACL)s->Acl, ACL_REVISION, GENERIC_ALL, sSystem) &&
        AddAccessAllowedAce((PACL)s->Acl, ACL_REVISION, GENERIC_ALL, sAdmins) &&
        InitializeSecurityDescriptor(&s->Sd, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorDacl(&s->Sd, TRUE, (PACL)s->Acl, FALSE)) {
        s->Sa.nLength = sizeof(s->Sa);
        s->Sa.lpSecurityDescriptor = &s->Sd;
        s->Sa.bInheritHandle = FALSE;
        s->Ok = 1;
    }
    if (sWorld != NULL) {
        FreeSid(sWorld);
    }
    if (sSystem != NULL) {
        FreeSid(sSystem);
    }
    if (sAdmins != NULL) {
        FreeSid(sAdmins);
    }
}

#define MK_MUTEX_RIGHTS (SYNCHRONIZE | 0x0001)

/* The named mutex taken, or NULL past `ms`. */
static HANDLE mk_lock(const char *name, DWORD ms)
{
    MK_SA sa;
    HANDLE m;
    DWORD r;

    mk_make_sa(&sa, MK_MUTEX_RIGHTS);
    m = CreateMutexA(sa.Ok ? &sa.Sa : NULL, FALSE, name);
    if (m == NULL) {
        m = OpenMutexA(MK_MUTEX_RIGHTS, FALSE, name);
    }
    if (m == NULL) {
        return NULL;
    }
    r = WaitForSingleObject(m, ms);
    if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) {
        return m;
    }
    CloseHandle(m);
    return NULL;
}

static void mk_unlock(HANDLE m)
{
    if (m != NULL) {
        ReleaseMutex(m);
        CloseHandle(m);
    }
}

/* ======================================================================== */
/* The value                                                                */
/* ======================================================================== */

/* LowerFilters of devnode dn into out: 1 read (possibly absent: *bytes
 * 0), 0 unreadable. */
static int mk_filters_get(DWORD dn, char *out, size_t cap, size_t *bytes)
{
    ULONG type = 0;
    ULONG n = (ULONG)cap - 2;
    DWORD cr;

    memset(out, 0, cap);
    cr = api.GetProp(dn, MK_CM_DRP_LOWERFILTERS, &type, out, &n, 0);
    if (cr == MK_CR_NO_SUCH_VALUE) {
        *bytes = 0;
        return 1;
    }
    if (cr != MK_CR_SUCCESS || (type != REG_MULTI_SZ && type != REG_SZ)) {
        return 0;
    }
    *bytes = n;
    return 1;
}

static int mk_filters_has(DWORD dn, int *has)
{
    char f[MK_FILTERS_BYTES];
    size_t n;

    if (!mk_filters_get(dn, f, sizeof(f), &n)) {
        return 0;
    }
    *has = inst_msz_has(f, n, MK_NAME);
    return 1;
}

/* xhcicm appended; 1 when the value now carries it (read back). */
static int mk_filters_add(DWORD dn)
{
    char f[MK_FILTERS_BYTES];
    char out[MK_FILTERS_BYTES + 16];
    size_t n;
    size_t m;
    int has;

    if (!mk_filters_get(dn, f, sizeof(f), &n)) {
        return 0;
    }
    if (inst_msz_has(f, n, MK_NAME)) {
        return 1;
    }
    m = mk_msz_append(f, n, out, sizeof(out));
    if (m == 0 ||
        api.SetProp(dn, MK_CM_DRP_LOWERFILTERS, out, (ULONG)m, 0) !=
            MK_CR_SUCCESS) {
        return 0;
    }
    return mk_filters_has(dn, &has) && has;
}

/* xhcicm removed, the others kept in order (the value deleted when nothing
 * else is in it); 1 when the value no longer carries it (read back). */
static int mk_filters_remove(DWORD dn)
{
    char f[MK_FILTERS_BYTES];
    char out[MK_FILTERS_BYTES];
    size_t n;
    size_t m;
    int removed;
    int has;
    DWORD cr;

    if (!mk_filters_get(dn, f, sizeof(f), &n)) {
        return 0;
    }
    if (!inst_msz_has(f, n, MK_NAME)) {
        return 1;
    }
    m = inst_msz_remove(f, n, MK_NAME, out, sizeof(out), &removed);
    if (m == (size_t)-1) {
        return 0;
    }
    if (m == 0) {
        cr = api.SetProp(dn, MK_CM_DRP_LOWERFILTERS, NULL, 0, 0);
    } else {
        cr = api.SetProp(dn, MK_CM_DRP_LOWERFILTERS, out, (ULONG)m, 0);
    }
    if (cr != MK_CR_SUCCESS) {
        return 0;
    }
    return mk_filters_has(dn, &has) && !has;
}

/* ======================================================================== */
/* Per entry                                                                */
/* ======================================================================== */

static MK_TRIED *mk_tried(const MK_MARKS *m, INST_U32 serial, int make)
{
    int i;

    for (i = 0; i < triedCount; i++) {
        if (tried[i].Serial == serial && tried[i].EpochLo == m->EpochLo &&
            tried[i].EpochHi == m->EpochHi) {
            return &tried[i];
        }
    }
    if (!make) {
        return NULL;
    }
    if (triedCount >= MK_MAX_TRIED) {
        memmove(&tried[0], &tried[1], sizeof(tried[0]) * (MK_MAX_TRIED - 1));
        triedCount = MK_MAX_TRIED - 1;
    }
    memset(&tried[triedCount], 0, sizeof(tried[0]));
    tried[triedCount].Serial = serial;
    tried[triedCount].EpochLo = m->EpochLo;
    tried[triedCount].EpochHi = m->EpochHi;
    return &tried[triedCount++];
}

/* The write's checks (section 7.4, "The write"); the unit's node in
 * *unitOut on MK_W_OK and MK_W_PRESENT. */
static int mk_check_entry(const MK_NHI *h, const MK_ENTRY *e,
                          const int *prot, int *unitOut)
{
    int port;
    int unit;
    int has;

    *unitOut = -1;
    if ((e->Flags & MK_EF_LOCATED) == 0) {
        return MK_W_NOT_LOCATED;
    }
    if ((e->Flags & MK_EF_REPORTED) == 0) {
        return MK_W_NOT_REPORTED;
    }
    if ((e->Flags & MK_EF_SAFE_OFF) != 0 || (h->M.Flags & MK_F_SAFE_OFF) != 0) {
        return MK_W_SAFE_OFF;
    }
    if (!mk_member_active(&h->Q, e->Member)) {
        return MK_W_NOT_ACTIVE;
    }
    port = mk_find_loc(e->Member);
    if (port < 0) {
        return MK_W_NO_PORT;
    }
    unit = mk_find_unit(port, e->Bus, e->Address);
    if (unit < 0) {
        return MK_W_NO_UNIT;
    }
    if (prot[unit] || prot[port]) {
        return MK_W_PROTECTED;
    }
    *unitOut = unit;
    if (!mk_filters_has(nodes[unit].Dn, &has)) {
        return MK_W_UNREADABLE;
    }
    return has ? MK_W_PRESENT : MK_W_OK;
}

/* The record first, the value, then the record completed (decision M12). */
static void mk_write(const MK_NHI *h, const MK_ENTRY *e, int unit)
{
    HANDLE m;
    MK_TRIED *t;
    int ok;

    t = mk_tried(&h->M, e->Serial, 1);
    if (t->WriteFailed && GetTickCount() - t->At < MK_RETRY_MS) {
        return;
    }
    m = mk_lock(MK_WRITE_MUTEX, 5000);
    if (m == NULL) {
        mk_log("write deferred: the write mutex is held (%s)", nodes[unit].Id);
        return;
    }
    ok = mk_rec_set(nodes[unit].Id, e->Member, h->M.EpochHi, h->M.EpochLo,
                    MK_REC_WRITING) &&
         mk_filters_add(nodes[unit].Dn);
    if (ok) {
        (void)mk_rec_set(nodes[unit].Id, e->Member, h->M.EpochHi,
                         h->M.EpochLo, MK_REC_WRITTEN);
        t->WriteFailed = 0;
        mk_log("mark written: %s (member %04lX, unit %02lX:%02lX.%lX, serial %lu)",
               nodes[unit].Id, e->Member, e->Bus, e->Address >> 16,
               e->Address & 0xFFFFUL, e->Serial);
    } else {
        t->WriteFailed = 1;
        t->At = GetTickCount();
        mk_log("mark write failed, retried within a minute: %s (error %lu)",
               nodes[unit].Id, GetLastError());
    }
    mk_unlock(m);
}

/* The entry still reads as the sample did, in a fresh read of its domain. */
static int mk_fresh_same(const MK_NHI *h, const MK_ENTRY *e)
{
    MK_NHI f;
    INST_U32 i;

    memset(&f, 0, sizeof(f));
    f.Node = h->Node;
    if (!mk_read_nhi(&f) || f.Q.EpochLo != h->Q.EpochLo ||
        f.Q.EpochHi != h->Q.EpochHi || f.M.Gen != h->M.Gen) {
        return 0;
    }
    for (i = 0; i < f.M.Count; i++) {
        if (f.M.E[i].Serial == e->Serial) {
            return f.M.E[i].Flags == e->Flags && f.M.E[i].Pending == 0;
        }
    }
    return 0;
}

static int mk_no_install_pending(void)
{
    if (api.WaitInstall == NULL) {
        return 0;
    }
    if (api.WaitInstall(0) != WAIT_OBJECT_0) {
        return 0;
    }
    Sleep(500);
    return api.WaitInstall(0) == WAIT_OBJECT_0;
}

/* The first plug's restart (section 7.4, "The first plug"): gated on fresh
 * reads under the tools' eject mutex, once per serial, vetoes respected. */
static void mk_restart(const MK_NHI *h, const MK_ENTRY *e, int unit)
{
    MK_TRIED *t;
    HANDLE em;
    HDEVINFO set;
    SP_DEVINFO_DATA data;
    SP_PROPCHANGE_PARAMS pcp;
    SP_DEVINSTALL_PARAMS_A ip;
    DWORD dn;
    ULONG st;
    ULONG pr;
    int has;
    int port;
    BOOL called;
    const char *why = NULL;

    t = mk_tried(&h->M, e->Serial, 1);
    if (t->Restarted || paramRestart == 0) {
        return;
    }
    em = mk_lock(MK_EJECT_MUTEX, 1000);
    if (em == NULL) {
        return;
    }
    if (!mk_no_install_pending()) {
        why = "an installation is pending (or cannot be told)";
    } else if (!mk_fresh_same(h, e)) {
        why = "the driver's answer changed since the sample";
    } else if (api.Locate(&dn, nodes[unit].Id, MK_CM_LOCATE_NORMAL) !=
                   MK_CR_SUCCESS ||
               api.Status(&st, &pr, dn, 0) != MK_CR_SUCCESS ||
               (st & MK_DN_STARTED) == 0 || (st & MK_DN_HAS_PROBLEM) != 0) {
        why = "the unit is not started (held for eject, disabled or installing)";
    } else if (!mk_filters_has(dn, &has) || !has) {
        why = "the mark is not in its LowerFilters";
    } else {
        port = nodes[unit].Parent;
        if (port < 0 || mk_find_loc(e->Member) != port ||
            !mk_prop_dword(dn, MK_CM_DRP_BUSNUMBER, &st) || st != e->Bus ||
            !mk_prop_dword(dn, MK_CM_DRP_ADDRESS, &pr) || pr != e->Address) {
            why = "the unit's identity changed";
        }
    }
    if (why != NULL) {
        if (!t->Gated) {
            t->Gated = 1;
            mk_log("restart not made yet: %s: %s", nodes[unit].Id, why);
        }
        mk_unlock(em);
        return;
    }
    t->Restarted = 1;
    t->At = GetTickCount();
    called = FALSE;
    set = api.CreateList(NULL, NULL);
    if (set != INVALID_HANDLE_VALUE) {
        memset(&data, 0, sizeof(data));
        data.cbSize = sizeof(data);
        if (api.OpenInfo(set, nodes[unit].Id, NULL, 0, &data)) {
            memset(&pcp, 0, sizeof(pcp));
            pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
            pcp.ClassInstallHeader.InstallFunction = MK_DIF_PROPERTYCHANGE;
            pcp.StateChange = MK_DICS_PROPCHANGE;
            pcp.Scope = MK_DICS_FLAG_GLOBAL;
            pcp.HwProfile = 0;
            if (api.SetCiParams(set, &data, &pcp.ClassInstallHeader,
                                sizeof(pcp))) {
                called = api.CallCi(MK_DIF_PROPERTYCHANGE, set, &data);
                memset(&ip, 0, sizeof(ip));
                ip.cbSize = sizeof(ip);
                if (called && api.GetDiParams(set, &data, &ip) &&
                    (ip.Flags & (MK_DI_NEEDREBOOT | MK_DI_NEEDRESTART)) != 0) {
                    mk_log("restart not made now (vetoed or needs a restart of "
                           "Windows): %s; the tool's fallback stands until its "
                           "next start", nodes[unit].Id);
                    called = FALSE;
                    t->Outcome = 1;
                }
            }
        }
        api.Destroy(set);
    }
    if (called) {
        mk_log("restart made: %s (serial %lu)", nodes[unit].Id, e->Serial);
    } else if (!t->Outcome) {
        mk_log("restart failed: %s (error %lu); the tool's fallback stands",
               nodes[unit].Id, GetLastError());
        t->Outcome = 1;
    }
    mk_unlock(em);
}

/* The confirmation of a unit Windows holds for eject (XP and later). */
static void mk_confirm(const MK_NHI *h, const MK_ENTRY *e, int unit)
{
    MK_TRIED *t;
    unsigned char v[MK_QC_BYTES];
    char path[200];
    HKEY k;
    DWORD type;
    DWORD n;
    LONG r;
    BYTE old[64];

    if (osv.dwMajorVersion < 5 ||
        (osv.dwMajorVersion == 5 && osv.dwMinorVersion < 1)) {
        return;     /* 2000: problem 24 is no confirmation */
    }
    if ((nodes[unit].Status & MK_DN_HAS_PROBLEM) == 0 ||
        nodes[unit].Problem != MK_PROB_HELD_FOR_EJECT) {
        return;
    }
    t = mk_tried(&h->M, e->Serial, 1);
    if (t->Confirmed || h->Driver[0] == '\0') {
        return;
    }
    sprintf(path, "SYSTEM\\CurrentControlSet\\Control\\Class\\%.120s",
            h->Driver);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_SET_VALUE,
                      &k) != ERROR_SUCCESS) {
        mk_log("confirmation not written: the NHI's driver key did not open");
        return;
    }
    n = sizeof(old);
    r = RegQueryValueExA(k, CM_QC_VALUE, NULL, &type, old, &n);
    if (r == ERROR_SUCCESS && n != 0) {
        RegCloseKey(k);
        return;     /* another confirmation waits for the thread */
    }
    mk_confirm_build(v, &h->M, e);
    if (RegSetValueExA(k, CM_QC_VALUE, 0, REG_BINARY, v, MK_QC_BYTES) ==
        ERROR_SUCCESS) {
        t->Confirmed = 1;
        mk_log("eject confirmed: %s held for eject (serial %lu)",
               nodes[unit].Id, e->Serial);
    }
    RegCloseKey(k);
}

/* ======================================================================== */
/* The poll                                                                 */
/* ======================================================================== */

static void mk_entries(const int *prot)
{
    int i;
    INST_U32 j;
    int w;
    int unit;
    int has;

    for (i = 0; i < nhiCount; i++) {
        MK_NHI *h = &nhis[i];
        if (!h->Verifiable) {
            continue;
        }
        for (j = 0; j < h->M.Count; j++) {
            MK_ENTRY *e = &h->M.E[j];
            MK_TRIED *t;
            if ((e->Flags & MK_EF_REPORTED) == 0) {
                continue;
            }
            w = mk_check_entry(h, e, prot, &unit);
            if ((w == MK_W_OK || w == MK_W_PRESENT) && e->Pending != 0) {
                /* A departure after a query-remove: confirmed only when
                 * Windows holds the unit for eject, marked or not. */
                mk_confirm(h, e, unit);
            }
            if (w == MK_W_OK) {
                mk_write(h, e, unit);
                continue;
            }
            if (w != MK_W_PRESENT) {
                t = mk_tried(&h->M, e->Serial, 0);
                if (w != MK_W_NOT_LOCATED && (t == NULL || !t->Outcome)) {
                    t = mk_tried(&h->M, e->Serial, 1);
                    t->Outcome = 1;
                    mk_log("notice not acted on: member %04lX unit %02lX "
                           "serial %lu: %s", e->Member, e->Bus, e->Serial,
                           mk_w_text(w));
                }
                continue;
            }
            /* The mark is there. */
            t = mk_tried(&h->M, e->Serial, 0);
            if (t != NULL && t->Restarted && !t->Outcome &&
                (e->Flags & (MK_EF_BUS_ATT | MK_EF_ADD_ATT)) != 0 &&
                (e->Flags & MK_EF_REMOVABLE) != 0) {
                t->Outcome = 1;
                mk_log("restarted, now listed: %s", nodes[unit].Id);
            }
            if (e->Pending != 0) {
                continue;
            }
            has = 1;
            if (mk_restart_wanted(e, has)) {
                mk_restart(h, e, unit);
            }
        }
    }
}

/* A record's devnode against the domains (decision M2 and M11). */
static void mk_judge(const char *id, const char *text)
{
    INST_U32 member;
    INST_U32 hi;
    INST_U32 lo;
    int state;
    DWORD dn;
    int present;
    int node;
    int parent;
    INST_U32 pbdf = 0;
    int i;
    INST_U32 j;
    int memberOfVerif = 0;
    int anyVerif = 0;
    int anyUnverif = 0;
    int listed = 0;
    int incomplete = 0;
    int safeOff = 0;
    int verdict;
    HANDLE m;

    if (!mk_rec_parse(text, &member, &hi, &lo, &state)) {
        member = 0;
        state = MK_REC_WRITTEN;
    }
    present = api.Locate(&dn, (char *)id, MK_CM_LOCATE_NORMAL) == MK_CR_SUCCESS;
    node = present ? mk_find_id(id) : -1;
    if (present && node < 0) {
        mk_log("record kept: %s is present but not in the walk", id);
        return;
    }
    parent = node >= 0 ? nodes[node].Parent : -1;
    if (parent >= 0 && nodes[parent].HasLoc) {
        pbdf = (nodes[parent].Bus << 8) |
               (((nodes[parent].Address >> 16) & 0x1FUL) << 3) |
               (nodes[parent].Address & 7UL);
    }
    for (i = 0; i < nhiCount; i++) {
        MK_NHI *h = &nhis[i];
        INST_U32 k;
        if (!h->Verifiable) {
            anyUnverif = 1;
            continue;
        }
        anyVerif = 1;
        for (k = 0; k < h->Q.Ports; k++) {
            if (parent >= 0 && h->Q.Port[k] == pbdf) {
                memberOfVerif = 1;
                if (h->M.Flags & MK_F_INCOMPLETE) {
                    incomplete = 1;
                }
                if (h->M.Flags & MK_F_SAFE_OFF) {
                    safeOff = 1;
                }
                for (j = 0; j < h->M.Count && mk_member_active(&h->Q, pbdf);
                     j++) {
                    const MK_ENTRY *e = &h->M.E[j];
                    if ((e->Flags & MK_EF_REPORTED) && e->Member == pbdf &&
                        (e->Flags & MK_EF_LOCATED) && node >= 0 &&
                        e->Bus == nodes[node].Bus &&
                        e->Address == nodes[node].Address) {
                        listed = 1;
                    }
                }
            }
        }
    }
    verdict = mk_reconcile(present, memberOfVerif ? 0 : anyUnverif, anyVerif,
                           listed, incomplete, safeOff);
    if (verdict == MK_R_LEAVE) {
        return;
    }
    if (verdict == MK_R_KEEP) {
        if (state == MK_REC_WRITING) {
            m = mk_lock(MK_WRITE_MUTEX, 5000);
            if (m != NULL) {
                if (mk_filters_add(dn)) {
                    (void)mk_rec_set(id, member, hi, lo, MK_REC_WRITTEN);
                    mk_log("record replayed: %s", id);
                }
                mk_unlock(m);
            }
        }
        return;
    }
    /* Removed: the devnode may be absent, so it is located as a phantom. */
    if (api.Locate(&dn, (char *)id, MK_CM_LOCATE_PHANTOM) != MK_CR_SUCCESS) {
        mk_log("record kept: %s cannot be located to remove its mark", id);
        return;
    }
    m = mk_lock(MK_WRITE_MUTEX, 5000);
    if (m == NULL) {
        return;
    }
    if (mk_filters_remove(dn)) {
        mk_rec_delete(id);
        mk_log("mark removed: %s (%s)", id,
               !present ? "the unit left" :
               safeOff ? "safe eject is off" : "no longer behind an active member");
    } else {
        mk_log("mark removal failed, kept for the next poll: %s (error %lu)",
               id, GetLastError());
    }
    mk_unlock(m);
}

static void mk_reconcile_all(void)
{
    HKEY k;
    DWORD i;
    char name[INST_ID_CHARS + 8];
    char text[96];
    DWORD nn;
    DWORD tn;
    DWORD type;
    char (*ids)[INST_ID_CHARS + 8];
    char (*texts)[96];
    int count = 0;
    int c;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, MK_MARKS_KEY, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return;
    }
    ids = malloc(256 * sizeof(*ids));
    texts = malloc(256 * sizeof(*texts));
    if (ids == NULL || texts == NULL) {
        free(ids);
        free(texts);
        RegCloseKey(k);
        return;
    }
    for (i = 0; count < 256; i++) {
        nn = sizeof(name) - 1;
        tn = sizeof(text) - 1;
        if (RegEnumValueA(k, i, name, &nn, NULL, &type, (LPBYTE)text, &tn) !=
            ERROR_SUCCESS) {
            break;
        }
        name[nn < sizeof(name) ? nn : sizeof(name) - 1] = '\0';
        text[tn < sizeof(text) ? tn : sizeof(text) - 1] = '\0';
        if (type != REG_SZ) {
            text[0] = '\0';
        }
        inst_copy(ids[count], name, sizeof(ids[count]));
        inst_copy(texts[count], text, sizeof(texts[count]));
        count++;
    }
    RegCloseKey(k);
    for (c = 0; c < count; c++) {
        mk_judge(ids[c], texts[c]);
    }
    free(ids);
    free(texts);
}

/* Every PCI devnode, present or not, whose LowerFilters carries the name
 * and which has no record: recorded, then judged as any record is. */
static void mk_inventory(void)
{
    HDEVINFO set;
    SP_DEVINFO_DATA data;
    DWORD i;
    char id[INST_ID_CHARS];
    char f[MK_FILTERS_BYTES];
    char text[96];
    DWORD type;
    DWORD n;
    int found = 0;

    set = api.GetClassDevs(NULL, "PCI", NULL, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) {
        mk_log("inventory failed: no PCI device set (error %lu)", GetLastError());
        return;
    }
    for (i = 0;; i++) {
        memset(&data, 0, sizeof(data));
        data.cbSize = sizeof(data);
        if (!api.EnumInfo(set, i, &data)) {
            break;
        }
        if (!api.GetInstanceId(set, &data, id, sizeof(id), NULL)) {
            continue;
        }
        memset(f, 0, sizeof(f));
        n = 0;
        if (!api.GetRegProp(set, &data, MK_SPDRP_LOWERFILTERS, &type,
                            (PBYTE)f, sizeof(f) - 2, &n)) {
            continue;
        }
        if (inst_msz_has(f, n, MK_NAME) && !mk_rec_get(id, text, sizeof(text))) {
            (void)mk_rec_set(id, 0, 0, 0, MK_REC_WRITTEN);
            mk_log("inventory: an unrecorded mark on %s, now recorded", id);
            found++;
        }
    }
    api.Destroy(set);
    mk_log("inventory: %d unrecorded mark(s)", found);
}

/* One poll; returns the milliseconds to the next. */
static DWORD mk_poll(void)
{
    int any;
    int *prot;

    if (!mk_walk()) {
        mk_log("the device tree could not be walked");
        return 10000;
    }
    any = mk_read_nhis();
    if (!inventoryDone || GetTickCount() - lastInventory >= MK_DAY_MS) {
        mk_inventory();
        inventoryDone = 1;
        lastInventory = GetTickCount();
    }
    prot = mk_protect();
    if (walkBad != 0) {
        /* An incomplete walk proves no parent and no absence. */
        mk_log("the device tree walk was incomplete (%d); poll skipped",
               walkBad);
        return mk_poll_ms(any);
    }
    mk_entries(prot);
    mk_reconcile_all();
    return mk_poll_ms(any);
}

/* ======================================================================== */
/* The service                                                              */
/* ======================================================================== */

static void mk_set_status(DWORD state, DWORD exitCode)
{
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted = state == SERVICE_RUNNING ?
        (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
    status.dwWin32ExitCode = exitCode;
    status.dwServiceSpecificExitCode = 0;
    status.dwCheckPoint = 0;
    status.dwWaitHint = state == SERVICE_RUNNING ? 0 : 30000;
    if (statusHandle != (SERVICE_STATUS_HANDLE)0) {
        SetServiceStatus(statusHandle, &status);
    }
}

static VOID WINAPI mk_handler(DWORD control)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        mk_set_status(SERVICE_STOP_PENDING, NO_ERROR);
        SetEvent(stopEvent);
        return;
    }
    if (statusHandle != (SERVICE_STATUS_HANDLE)0) {
        SetServiceStatus(statusHandle, &status);
    }
}

static int mk_wow64(void)
{
    PFN_ISWOW64 is;
    BOOL w = FALSE;

    is = (PFN_ISWOW64)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                     "IsWow64Process");
    return is != NULL && is(GetCurrentProcess(), &w) && w;
}

/* Common to the service and -once: 0 when the poll may run. */
static int mk_prepare(void)
{
    osv.dwOSVersionInfoSize = sizeof(osv);
    GetVersionExA(&osv);
    mk_log_open();
    mk_params();
    if (osv.dwPlatformId != VER_PLATFORM_WIN32_NT) {
        mk_log("not on Windows 98 SE or ME: this service is NT only");
        return 1;
    }
    if (mk_wow64()) {
        mk_log("refused: the 32-bit build on 64-bit Windows; XHCIMK64.EXE is "
               "this system's");
        return 1;
    }
    if (mk_load() != 0) {
        mk_log("refused: a routine the service needs is missing");
        return 1;
    }
    nodes = (MK_NODE *)calloc(MK_MAX_NODES, sizeof(MK_NODE));
    if (nodes == NULL) {
        mk_log("refused: no memory for the device tree");
        return 1;
    }
    return 0;
}

static VOID WINAPI mk_service_main(DWORD argc, LPSTR *argv)
{
    DWORD wait;

    (void)argc;
    (void)argv;
    statusHandle = RegisterServiceCtrlHandlerA(MK_SERVICE, mk_handler);
    if (statusHandle == (SERVICE_STATUS_HANDLE)0) {
        return;
    }
    mk_set_status(SERVICE_START_PENDING, NO_ERROR);
    stopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (stopEvent == NULL || mk_prepare() != 0) {
        mk_set_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }
    mk_log("started: xhcimark %s, Windows %lu.%lu build %lu, Restart %lu",
           XHCIMARK_VERSION, osv.dwMajorVersion, osv.dwMinorVersion,
           osv.dwBuildNumber, paramRestart);
    mk_set_status(SERVICE_RUNNING, NO_ERROR);
    wait = 0;
    while (WaitForSingleObject(stopEvent, wait) == WAIT_TIMEOUT) {
        wait = mk_poll();
    }
    mk_log("stopped");
    mk_set_status(SERVICE_STOPPED, NO_ERROR);
}

/* ======================================================================== */
/* The hand registration of a test guest                                    */
/* ======================================================================== */

static int mk_register(void)
{
    char self[MAX_PATH];
    char sys[MAX_PATH];
    char quoted[MAX_PATH + 4];
    SC_HANDLE scm;
    SC_HANDLE svc;

    if (GetModuleFileNameA(NULL, self, sizeof(self)) == 0 ||
        GetSystemDirectoryA(sys, sizeof(sys)) == 0) {
        printf("ERROR: the paths could not be read\n");
        return 1;
    }
    if (_strnicmp(self, sys, strlen(sys)) != 0 || self[strlen(sys)] != '\\' ||
        strchr(self + strlen(sys) + 1, '\\') != NULL) {
        printf("ERROR: %s is not in %s: a LocalSystem service runs only from "
               "the system directory (record 21 section 7.4, decision M8)\n",
               self, sys);
        return 1;
    }
    sprintf(quoted, "\"%s\"", self);
    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (scm == NULL) {
        printf("ERROR: the service controller did not open (error %lu)\n",
               GetLastError());
        return 1;
    }
    svc = CreateServiceA(scm, MK_SERVICE, MK_DISPLAY, SERVICE_ALL_ACCESS,
                         SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                         SERVICE_ERROR_NORMAL, quoted, NULL, NULL, NULL, NULL,
                         NULL);
    if (svc == NULL) {
        printf("ERROR: CreateService failed (error %lu)\n", GetLastError());
        CloseServiceHandle(scm);
        return 1;
    }
    if (!StartServiceA(svc, 0, NULL)) {
        printf("registered, but the start failed (error %lu)\n", GetLastError());
    } else {
        printf("registered and started: %s\n", quoted);
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

static int mk_unregister(void)
{
    SC_HANDLE scm;
    SC_HANDLE svc;
    SERVICE_STATUS st;
    int i;

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        printf("ERROR: the service controller did not open (error %lu)\n",
               GetLastError());
        return 1;
    }
    svc = OpenServiceA(scm, MK_SERVICE, SERVICE_STOP | SERVICE_QUERY_STATUS |
                                        DELETE);
    if (svc == NULL) {
        printf("ERROR: no %s service (error %lu)\n", MK_SERVICE, GetLastError());
        CloseServiceHandle(scm);
        return 1;
    }
    (void)ControlService(svc, SERVICE_CONTROL_STOP, &st);
    for (i = 0; i < 30; i++) {
        if (!QueryServiceStatus(svc, &st) ||
            st.dwCurrentState == SERVICE_STOPPED) {
            break;
        }
        Sleep(1000);
    }
    if (!DeleteService(svc)) {
        printf("ERROR: DeleteService failed (error %lu)\n", GetLastError());
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return 1;
    }
    printf("stopped and deleted; the marks it wrote stay until the installer "
           "removes them\n");
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

/* ======================================================================== */
/* main                                                                     */
/* ======================================================================== */

static int stFails;
static int stChecks;

static void mk_report(int ok, const char *what)
{
    stChecks++;
    if (!ok) {
        stFails++;
    }
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void mk_usage(void)
{
    printf("XHCIMARK %s - the xHCI98 Thunderbolt safe-removal mark service\n",
           XHCIMARK_VERSION);
    printf("Started by the service controller; the installer registers it.\n");
    printf("  -selftest    the pure half over canned answers\n");
    printf("  -once        one poll in this console (an administrator, a test guest)\n");
    printf("  -register    register and start it from the system directory (a test guest)\n");
    printf("  -unregister  stop and delete it (a test guest)\n");
}

int main(int argc, char **argv)
{
    static SERVICE_TABLE_ENTRYA table[] = {
        { MK_SERVICE, mk_service_main },
        { NULL, NULL }
    };
    if (argc == 2 && (inst_eq(argv[1], "-selftest") ||
                      inst_eq(argv[1], "/selftest"))) {
        printf("XHCIMARK %s self-test\n", XHCIMARK_VERSION);
        stFails = 0;
        stChecks = 0;
        (void)mk_selftest(mk_report);
        printf("\n%d checks, %d failures\n", stChecks, stFails);
        return stFails == 0 ? 0 : 1;
    }
    if (argc == 2 && (inst_eq(argv[1], "-once") || inst_eq(argv[1], "/once"))) {
        logConsole = 1;
        if (mk_prepare() != 0) {
            return 1;
        }
        (void)mk_poll();
        return 0;
    }
    if (argc == 2 && (inst_eq(argv[1], "-register") ||
                      inst_eq(argv[1], "/register"))) {
        return mk_register();
    }
    if (argc == 2 && (inst_eq(argv[1], "-unregister") ||
                      inst_eq(argv[1], "/unregister"))) {
        return mk_unregister();
    }
    if (argc != 1) {
        mk_usage();
        return 1;
    }
    if (!StartServiceCtrlDispatcherA(table)) {
        mk_usage();
        return 1;
    }
    return 0;
}
