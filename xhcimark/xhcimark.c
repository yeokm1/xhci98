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
#define MK_INVENTORY_MS     60000UL
#define MK_RETRY_MS         60000UL

#define MK_MAX_NODES        2048
#define MK_MAX_NHIS         16
#define MK_MAX_TRIED        2048    /* every entry of every answer, and more */
#define MK_MAX_RECORDS      65536

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
    PFN_CM_REL Parent;
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
    int StatusOk;           /* CM_Get_DevNode_Status succeeded             */
    int ServiceBad;         /* the Service value could not be read (absent
                               is not bad)                                 */
    int IdsBad;             /* a hardware or compatible id list could not
                               be read (absent is not bad)                 */
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
    char NhiId[INST_ID_CHARS];  /* the domain the serial belongs to       */
    INST_U32 EpochLo;
    INST_U32 EpochHi;
    INST_U32 Serial;
    DWORD At;               /* the last write attempt or restart          */
    int Restarted;          /* the restart was made, until seen listed     */
    int Outcome;            /* its outcome was logged                     */
    int Confirmed;          /* the eject confirmation was written         */
    int Gated;              /* a restart gate's refusal was logged        */
} MK_TRIED;

static MK_NODE *nodes;
static int nodeCount;
static int walkBad;
static MK_NHI nhis[MK_MAX_NHIS];
static int nhiCount;
static int discoveryIncomplete;     /* a devnode could be an NHI unseen   */
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
static const int *curProt;      /* this poll's protected nodes (mk_poll)  */
static int scrubOk;             /* the last CDDB scrub completed          */
static int scrubFailLogged;     /* its failure was logged (once per run)  */

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
    api.Parent = (PFN_CM_REL)mk_proc(api.CfgMgr, "CM_Get_Parent", &missing);
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

/* A text property; returns the CONFIGRET (CR_NO_SUCH_VALUE: absent). */
static DWORD mk_prop_text(DWORD dn, ULONG prop, char *out, size_t cap,
                          size_t *bytes)
{
    ULONG type = 0;
    ULONG n = (ULONG)cap - 2;
    DWORD cr;

    memset(out, 0, cap);
    cr = api.GetProp(dn, prop, &type, out, &n, 0);
    if (cr != MK_CR_SUCCESS) {
        memset(out, 0, cap);
        n = 0;
    }
    out[cap - 1] = '\0';
    out[cap - 2] = '\0';
    if (bytes != NULL) {
        *bytes = n;
    }
    return cr;
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
        cr = mk_prop_text(dn, MK_CM_DRP_HARDWAREID, t->Hw, sizeof(t->Hw),
                          &t->HwBytes);
        t->IdsBad = cr != MK_CR_SUCCESS && cr != MK_CR_NO_SUCH_VALUE;
        cr = mk_prop_text(dn, MK_CM_DRP_COMPATIBLEIDS, t->Compat,
                          sizeof(t->Compat), &t->CompatBytes);
        if (cr != MK_CR_SUCCESS && cr != MK_CR_NO_SUCH_VALUE) {
            t->IdsBad = 1;
        }
        cr = mk_prop_text(dn, MK_CM_DRP_SERVICE, t->Service,
                          sizeof(t->Service), NULL);
        t->ServiceBad = cr != MK_CR_SUCCESS && cr != MK_CR_NO_SUCH_VALUE;
        t->HasLoc = mk_prop_dword(dn, MK_CM_DRP_BUSNUMBER, &t->Bus) &&
                    mk_prop_dword(dn, MK_CM_DRP_ADDRESS, &t->Address);
        t->StatusOk = api.Status(&t->Status, &t->Problem, dn, 0) ==
                      MK_CR_SUCCESS;
        if (!t->StatusOk) {
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
    discoveryIncomplete = 0;
    for (i = 0; i < nodeCount; i++) {
        MK_NHI *h;
        int unread;
        if (nodes[i].IdsBad) {
            /* Its ids cannot exclude an NHI of this driver: no decision
             * that needs every domain known is made this poll. */
            discoveryIncomplete = 1;
        }
        if (nhiCount >= MK_MAX_NHIS) {
            discoveryIncomplete = 1;
            break;
        }
        /* An NHI whose Service value cannot be read may be this driver's:
         * it is listed, never answered, so it counts as a domain that
         * cannot be verified rather than vanishing from the evidence. */
        unread = nodes[i].ServiceBad &&
                 inst_tb_is_nhi(nodes[i].Hw, nodes[i].HwBytes,
                                nodes[i].Compat, nodes[i].CompatBytes, "");
        if (!unread &&
            (!inst_eq(nodes[i].Service, MK_NAME) ||
             !inst_tb_is_nhi(nodes[i].Hw, nodes[i].HwBytes, nodes[i].Compat,
                             nodes[i].CompatBytes, nodes[i].Service))) {
            continue;
        }
        h = &nhis[nhiCount++];
        memset(h, 0, sizeof(*h));
        h->Node = i;
        h->DevOk = nodes[i].StatusOk &&
                   (nodes[i].Status & MK_DN_STARTED) != 0 &&
                   (nodes[i].Status & MK_DN_HAS_PROBLEM) == 0;
        (void)mk_prop_text(nodes[i].Dn, MK_CM_DRP_DRIVER, h->Driver,
                           sizeof(h->Driver), NULL);
        h->Answered = !unread && mk_read_nhi(h);
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

/* Whether an attempt may still matter (decision M3: a restart once per
 * serial until the unit is seen listed after it, mk_entries):
 * kept unless there is positive evidence its serial is gone - its NHI's
 * devnode no longer in the tree, its NHI answering under another epoch
 * (a new start), or answering under that epoch without the serial
 * reported. An NHI that cannot be read keeps its attempts. */
static int mk_tried_live(const MK_TRIED *t)
{
    int i;
    INST_U32 j;

    for (i = 0; i < nhiCount; i++) {
        const MK_NHI *h = &nhis[i];
        if (!inst_eq(nodes[h->Node].Id, t->NhiId)) {
            continue;
        }
        if (!h->Answered) {
            return 1;
        }
        if (h->M.EpochLo != t->EpochLo || h->M.EpochHi != t->EpochHi) {
            return 0;
        }
        for (j = 0; j < h->M.Count; j++) {
            if (h->M.E[j].Serial == t->Serial &&
                (h->M.E[j].Flags & MK_EF_REPORTED) != 0) {
                return 1;
            }
        }
        return 0;
    }
    /* Its NHI is not among this poll's NHIs: retired only when a whole walk
     * no longer has its devnode at all (one present but not read as an NHI
     * now, its Service value missing for a moment, keeps it). */
    return walkBad != 0 || mk_find_id(t->NhiId) >= 0;
}

/* The attempt state of a serial, made when `make`; NULL when the table is
 * full of still-reported serials (the caller then acts on nothing this
 * poll rather than forget an earlier attempt). */
static MK_TRIED *mk_tried(const MK_NHI *h, INST_U32 serial, int make)
{
    const MK_MARKS *m = &h->M;
    int i;

    for (i = 0; i < triedCount; i++) {
        if (tried[i].Serial == serial && tried[i].EpochLo == m->EpochLo &&
            tried[i].EpochHi == m->EpochHi &&
            inst_eq(tried[i].NhiId, nodes[h->Node].Id)) {
            return &tried[i];
        }
    }
    if (!make) {
        return NULL;
    }
    if (triedCount >= MK_MAX_TRIED) {
        for (i = 0; i < triedCount; i++) {
            if (!mk_tried_live(&tried[i])) {
                break;
            }
        }
        if (i == triedCount) {
            return NULL;
        }
        memmove(&tried[i], &tried[i + 1],
                sizeof(tried[0]) * (size_t)(triedCount - i - 1));
        triedCount--;
    }
    memset(&tried[triedCount], 0, sizeof(tried[0]));
    inst_copy(tried[triedCount].NhiId, nodes[h->Node].Id,
              sizeof(tried[triedCount].NhiId));
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
    if (!nodes[unit].StatusOk) {
        return MK_W_UNREADABLE;
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

/* A unit's last failed write or replay, so both keep one attempt a
 * minute (section 7.4, "The write"); an entry is kept while its record is
 * outstanding and given back once a write succeeds. */
#define MK_MAX_FAILED 256
static struct {
    char Id[INST_ID_CHARS];
    DWORD At;
} failed[MK_MAX_FAILED];

/* 1 when a write or replay of `id` may be tried now: no deadline of its
 * own pending, and room to keep one should it fail (a full table of
 * unexpired deadlines defers new attempts rather than forget one). */
static int mk_write_due(const char *id)
{
    int i;
    int room = 0;

    for (i = 0; i < MK_MAX_FAILED; i++) {
        if (failed[i].Id[0] != '\0' && inst_eq(failed[i].Id, id)) {
            return GetTickCount() - failed[i].At >= MK_RETRY_MS;
        }
        if (failed[i].Id[0] == '\0' ||
            GetTickCount() - failed[i].At >= MK_RETRY_MS) {
            room = 1;
        }
    }
    return room;
}

static void mk_write_failed(const char *id)
{
    int i;
    int oldest = 0;

    for (i = 0; i < MK_MAX_FAILED; i++) {
        if (failed[i].Id[0] != '\0' && inst_eq(failed[i].Id, id)) {
            failed[i].At = GetTickCount();
            return;
        }
    }
    for (i = 0; i < MK_MAX_FAILED; i++) {
        if (failed[i].Id[0] == '\0') {
            oldest = i;
            break;
        }
        if (GetTickCount() - failed[i].At > GetTickCount() - failed[oldest].At) {
            oldest = i;
        }
    }
    /* mk_write_due left room: the oldest is free or past its deadline. */
    inst_copy(failed[oldest].Id, id, sizeof(failed[oldest].Id));
    failed[oldest].At = GetTickCount();
}

static void mk_write_done(const char *id)
{
    int i;

    for (i = 0; i < MK_MAX_FAILED; i++) {
        if (failed[i].Id[0] != '\0' && inst_eq(failed[i].Id, id)) {
            failed[i].Id[0] = '\0';
        }
    }
}

/* The record first, the value, then the record completed (decision M12). */
static void mk_write(const MK_NHI *h, const MK_ENTRY *e, int unit)
{
    HANDLE m;
    int ok;

    if (!mk_write_due(nodes[unit].Id) || !scrubOk) {
        /* An unproven scrub: a new mark could reach the database unseen. */
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
        mk_write_done(nodes[unit].Id);
        mk_log("mark written: %s (member %04lX, unit %02lX:%02lX.%lX, serial %lu)",
               nodes[unit].Id, e->Member, e->Bus, e->Address >> 16,
               e->Address & 0xFFFFUL, e->Serial);
    } else {
        mk_write_failed(nodes[unit].Id);
        mk_log("mark write failed, retried within a minute: %s (error %lu)",
               nodes[unit].Id, GetLastError());
    }
    mk_unlock(m);
}

/* The restart's fresh reads (section 7.4, "The first plug"): the NHI's
 * devnode still present and started, both answers read again eligible,
 * the same epoch and marks generation as the sample, the member still
 * active, and the entry reading as it did. */
static int mk_fresh_same(const MK_NHI *h, const MK_ENTRY *e)
{
    static MK_NHI f;
    ULONG st;
    ULONG pr;
    INST_U32 i;

    memset(&f, 0, sizeof(f));
    f.Node = h->Node;
    if (api.Status(&st, &pr, nodes[h->Node].Dn, 0) != MK_CR_SUCCESS) {
        return 0;
    }
    f.DevOk = (st & MK_DN_STARTED) != 0 && (st & MK_DN_HAS_PROBLEM) == 0;
    if (!mk_read_nhi(&f) || !mk_verifiable(f.DevOk, &f.Q, &f.M) ||
        f.Q.EpochLo != h->Q.EpochLo || f.Q.EpochHi != h->Q.EpochHi ||
        f.M.Gen != h->M.Gen || !mk_member_active(&f.Q, e->Member)) {
        return 0;
    }
    for (i = 0; i < f.M.Count; i++) {
        if (f.M.E[i].Serial == e->Serial) {
            return f.M.E[i].Flags == e->Flags &&
                   f.M.E[i].Member == e->Member && f.M.E[i].Bus == e->Bus &&
                   f.M.E[i].Address == e->Address;
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

/* The unit's devnode as Windows has it now: located afresh, its parent
 * the member at `member`, its own location the entry's, the mark in its
 * LowerFilters. 1 with *dn when all hold. */
static int mk_fresh_unit(const char *id, const MK_ENTRY *e, DWORD *dn)
{
    DWORD parent;
    ULONG bus;
    ULONG address;
    int has;

    if (api.Locate(dn, (char *)id, MK_CM_LOCATE_NORMAL) != MK_CR_SUCCESS ||
        api.Parent(&parent, *dn, 0) != MK_CR_SUCCESS ||
        !mk_prop_dword(parent, MK_CM_DRP_BUSNUMBER, &bus) ||
        !mk_prop_dword(parent, MK_CM_DRP_ADDRESS, &address) ||
        bus != ((e->Member >> 8) & 0xFFUL) || address != mk_addr_of(e->Member) ||
        !mk_prop_dword(*dn, MK_CM_DRP_BUSNUMBER, &bus) || bus != e->Bus ||
        !mk_prop_dword(*dn, MK_CM_DRP_ADDRESS, &address) ||
        address != e->Address) {
        return 0;
    }
    return mk_filters_has(*dn, &has) && has;
}

/* The first plug's restart (section 7.4, "The first plug"): gated on fresh
 * reads under the tools' eject mutex, once per serial until the unit is
 * seen listed after it (mk_entries re-arms it then), vetoes respected. A
 * departure pending on the entry does not hold it back: a first plug's
 * install removes the BusRelations child after a query-remove and builds
 * the stack again, and the fresh status below (started, no problem) is
 * what tells that from a unit Windows holds for eject or a disabled one. */
static void mk_restart(const MK_NHI *h, const MK_ENTRY *e, int unit)
{
    MK_TRIED *t;
    HANDLE em;
    HDEVINFO set = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA data;
    SP_PROPCHANGE_PARAMS pcp;
    SP_DEVINSTALL_PARAMS_A ip;
    DWORD dn;
    ULONG st;
    ULONG pr;
    BOOL called;
    const char *why = NULL;

    t = mk_tried(h, e->Serial, 1);
    if (t == NULL || t->Restarted || paramRestart == 0) {
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
    } else if (!mk_fresh_unit(nodes[unit].Id, e, &dn)) {
        why = "the unit's identity or mark changed";
    }
    if (why == NULL) {
        set = api.CreateList(NULL, NULL);
        if (set == INVALID_HANDLE_VALUE) {
            why = "no device information set";
        }
    }
    if (why == NULL) {
        memset(&data, 0, sizeof(data));
        data.cbSize = sizeof(data);
        if (!api.OpenInfo(set, nodes[unit].Id, NULL, 0, &data)) {
            why = "the unit could not be opened";
            api.Destroy(set);
        }
    }
    if (why == NULL &&
        (api.Status(&st, &pr, dn, 0) != MK_CR_SUCCESS ||
         (st & MK_DN_STARTED) == 0 || (st & MK_DN_HAS_PROBLEM) != 0)) {
        why = "the unit is not started (held for eject, disabled or installing)";
        api.Destroy(set);
    }
    if (why != NULL) {
        if (!t->Gated) {
            t->Gated = 1;
            mk_log("restart not made yet: %s: %s", nodes[unit].Id, why);
        }
        mk_unlock(em);
        return;
    }
    /* Immediately after the status read: the call itself. */
    t->Restarted = 1;
    t->At = GetTickCount();
    called = FALSE;
    memset(&pcp, 0, sizeof(pcp));
    pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    pcp.ClassInstallHeader.InstallFunction = MK_DIF_PROPERTYCHANGE;
    pcp.StateChange = MK_DICS_PROPCHANGE;
    pcp.Scope = MK_DICS_FLAG_GLOBAL;
    pcp.HwProfile = 0;
    if (api.SetCiParams(set, &data, &pcp.ClassInstallHeader, sizeof(pcp))) {
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
    api.Destroy(set);
    if (called) {
        mk_log("restart made: %s (serial %lu)", nodes[unit].Id, e->Serial);
    } else if (!t->Outcome) {
        mk_log("restart failed: %s (error %lu); the tool's fallback stands",
               nodes[unit].Id, GetLastError());
        t->Outcome = 1;
    }
    mk_unlock(em);
}

/* The confirmation of a unit Windows holds for eject (XP and later), under
 * the tools' eject mutex, which every writer of the value takes around its
 * check and write, so no writer overwrites another's: written only while
 * the value is absent or empty, and again while the departure stays
 * pending after the thread had the time to read it. */
#define MK_CONFIRM_AGAIN_MS 5000UL
static void mk_confirm(const MK_NHI *h, const MK_ENTRY *e, int unit)
{
    MK_TRIED *t;
    unsigned char v[MK_QC_BYTES];
    char path[200];
    HKEY k;
    HANDLE em;
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
    t = mk_tried(h, e->Serial, 1);
    if (t == NULL || h->Driver[0] == '\0' ||
        (t->Confirmed && GetTickCount() - t->At < MK_CONFIRM_AGAIN_MS)) {
        return;
    }
    sprintf(path, "SYSTEM\\CurrentControlSet\\Control\\Class\\%.120s",
            h->Driver);
    em = mk_lock(MK_EJECT_MUTEX, 1000);
    if (em == NULL) {
        return;
    }
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_SET_VALUE,
                      &k) != ERROR_SUCCESS) {
        mk_unlock(em);
        mk_log("confirmation not written: the NHI's driver key did not open");
        return;
    }
    n = sizeof(old);
    r = RegQueryValueExA(k, CM_QC_VALUE, NULL, &type, old, &n);
    if (!(r == ERROR_FILE_NOT_FOUND || (r == ERROR_SUCCESS && n == 0))) {
        /* Another confirmation waits for the thread, or the value cannot
         * be read: nothing is written over it. */
        RegCloseKey(k);
        mk_unlock(em);
        return;
    }
    mk_confirm_build(v, &h->M, e);
    if (RegSetValueExA(k, CM_QC_VALUE, 0, REG_BINARY, v, MK_QC_BYTES) ==
        ERROR_SUCCESS) {
        if (!t->Confirmed) {
            mk_log("eject confirmed: %s held for eject (serial %lu)",
                   nodes[unit].Id, e->Serial);
        }
        t->Confirmed = 1;
        t->At = GetTickCount();
    }
    RegCloseKey(k);
    mk_unlock(em);
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
                t = mk_tried(h, e->Serial, 0);
                if (w != MK_W_NOT_LOCATED && (t == NULL || !t->Outcome)) {
                    t = mk_tried(h, e->Serial, 1);
                    if (t == NULL) {
                        continue;
                    }
                    t->Outcome = 1;
                    mk_log("notice not acted on: member %04lX unit %02lX "
                           "serial %lu: %s", e->Member, e->Bus, e->Serial,
                           mk_w_text(w));
                }
                continue;
            }
            /* The mark is there. */
            t = mk_tried(h, e->Serial, 0);
            if (t != NULL && t->Restarted &&
                (e->Flags & (MK_EF_BUS_ATT | MK_EF_ADD_ATT)) != 0 &&
                (e->Flags & MK_EF_REMOVABLE) != 0) {
                /* Listed after the restart: the restart worked, so a later
                 * loss of the mark is a new event and may have its own one
                 * restart. Windows' driver installation deletes a device's
                 * own LowerFilters value (read on XP: a driver update of a
                 * unit marked and listed left it unmarked, the value
                 * written again after it); a restart Windows vetoed never
                 * reaches here, so it is never repeated. */
                if (!t->Outcome) {
                    mk_log("restarted, now listed: %s", nodes[unit].Id);
                }
                t->Restarted = 0;
                t->Outcome = 0;
                t->Gated = 0;
            }
            if (mk_restart_wanted(e, 1) &&
                (nodes[unit].Status & MK_DN_STARTED) != 0 &&
                (nodes[unit].Status & MK_DN_HAS_PROBLEM) == 0) {
                mk_restart(h, e, unit);
            }
        }
    }
}

/* The domain whose answer lists location `bdf` as a member, or NULL. */
static MK_NHI *mk_owner(INST_U32 bdf)
{
    int i;
    INST_U32 k;

    if (bdf == 0) {
        return NULL;
    }
    for (i = 0; i < nhiCount; i++) {
        if (!nhis[i].Answered) {
            continue;
        }
        for (k = 0; k < nhis[i].Q.Ports; k++) {
            if (nhis[i].Q.Port[k] == bdf) {
                return &nhis[i];
            }
        }
    }
    return NULL;
}

/* A record's devnode against its owning domain (decisions M2 and M11):
 * the domain whose answer lists the devnode's parent as a member (a
 * present devnode) or the record's member (an absent one). Nothing is
 * removed or replayed for a domain that cannot be verified, and a lookup
 * that fails is never read as an absence. */
static void mk_judge(const char *id, const char *text)
{
    INST_U32 member;
    INST_U32 hi;
    INST_U32 lo;
    int state;
    DWORD dn;
    DWORD cr;
    int presence;
    int node = -1;
    int parent;
    INST_U32 pbdf = 0;
    int i;
    int anyVerif = 0;
    int anyUnverif = 0;
    int listed = 0;
    int incomplete = 0;
    int safeOff = 0;
    int memberActive = 0;
    int verdict;
    int w;
    int unit;
    MK_NHI *owner = NULL;
    const MK_ENTRY *le = NULL;
    HANDLE m;

    if (!mk_rec_parse(text, &member, &hi, &lo, &state)) {
        member = 0;
        state = MK_REC_WRITTEN;
    }
    cr = api.Locate(&dn, (char *)id, MK_CM_LOCATE_NORMAL);
    if (cr == MK_CR_SUCCESS) {
        presence = MK_P_PRESENT;
    } else if (cr == MK_CR_NO_SUCH_DEVNODE) {
        cr = api.Locate(&dn, (char *)id, MK_CM_LOCATE_PHANTOM);
        if (cr == MK_CR_SUCCESS) {
            presence = MK_P_ABSENT;
        } else if (cr == MK_CR_NO_SUCH_DEVNODE) {
            /* The devnode itself is gone, and its value with it. */
            m = mk_lock(MK_WRITE_MUTEX, 5000);
            if (m != NULL) {
                mk_rec_delete(id);
                mk_unlock(m);
                mk_log("record deleted: %s no longer exists", id);
            }
            return;
        } else {
            presence = MK_P_UNKNOWN;
        }
    } else {
        presence = MK_P_UNKNOWN;
    }
    if (presence == MK_P_PRESENT) {
        node = mk_find_id(id);
        if (node < 0) {
            /* Present but not in the walk: nothing can be judged. */
            return;
        }
        parent = nodes[node].Parent;
        if (parent >= 0 && !mk_is_pci(&nodes[parent]) &&
            nodes[parent].Id[0] != '\0') {
            /* A parent read and not a PCI function (the root bus, ACPI's
             * PCI bus): the devnode is no child of a tunnel port, so no
             * domain owns it; one that took the name from a
             * CriticalDeviceDatabase copy is removed once every NHI is
             * verifiable. */
            owner = NULL;
        } else if (!nodes[node].HasLoc || parent < 0 ||
                   !nodes[parent].HasLoc) {
            /* A location that cannot be read is no proof the unit is
             * outside every domain. */
            return;
        } else {
            pbdf = (nodes[parent].Bus << 8) |
                   (((nodes[parent].Address >> 16) & 0x1FUL) << 3) |
                   (nodes[parent].Address & 7UL);
            owner = mk_owner(pbdf);
        }
    } else {
        owner = mk_owner(member);
    }
    for (i = 0; i < nhiCount; i++) {
        if (nhis[i].Verifiable) {
            anyVerif = 1;
        } else {
            anyUnverif = 1;
        }
    }
    if (discoveryIncomplete) {
        anyUnverif = 1;
    }
    if (owner != NULL && owner->Verifiable) {
        incomplete = (owner->M.Flags & MK_F_INCOMPLETE) != 0;
        safeOff = (owner->M.Flags & MK_F_SAFE_OFF) != 0;
        memberActive = mk_member_active(&owner->Q,
                                        presence == MK_P_PRESENT ? pbdf : member);
        if (presence == MK_P_PRESENT && memberActive) {
            switch (mk_listed(&owner->M, pbdf, nodes[node].Bus,
                              nodes[node].Address, &le)) {
            case MK_L_LISTED:
                listed = 1;
                break;
            case MK_L_UNSURE:
                /* An entry behind the member that cannot be placed may be
                 * this unit: as an incomplete answer, it proves nothing. */
                incomplete = 1;
                break;
            default:
                break;
            }
        }
    }
    verdict = mk_reconcile(presence, owner != NULL,
                           owner != NULL && owner->Verifiable, anyUnverif,
                           anyVerif, memberActive, listed, incomplete,
                           safeOff);
    if (verdict == MK_R_LEAVE) {
        return;
    }
    if (verdict == MK_R_KEEP) {
        if (state != MK_REC_WRITING || le == NULL || curProt == NULL ||
            !scrubOk) {
            /* A replay writes a mark: not while the scrub is unproven. */
            return;
        }
        /* A replay is a write: the write's own checks first, the unit the
         * one the record names. */
        w = mk_check_entry(owner, le, curProt, &unit);
        if ((w != MK_W_OK && w != MK_W_PRESENT) || unit != node) {
            return;
        }
        if (mk_write_due(id)) {
            m = mk_lock(MK_WRITE_MUTEX, 5000);
            if (m != NULL) {
                if (mk_filters_add(dn)) {
                    (void)mk_rec_set(id, member, hi, lo, MK_REC_WRITTEN);
                    mk_write_done(id);
                    mk_log("record replayed: %s", id);
                } else {
                    mk_write_failed(id);
                    mk_log("record replay failed, retried within a minute: %s",
                           id);
                }
                mk_unlock(m);
            }
        }
        return;
    }
    if (presence == MK_P_PRESENT &&
        api.Locate(&dn, (char *)id, MK_CM_LOCATE_PHANTOM) != MK_CR_SUCCESS) {
        return;
    }
    m = mk_lock(MK_WRITE_MUTEX, 5000);
    if (m == NULL) {
        return;
    }
    if (mk_filters_remove(dn)) {
        mk_rec_delete(id);
        mk_log("mark removed: %s (%s; member %04lX)", id,
               presence == MK_P_ABSENT ? "the unit left" :
               safeOff ? "safe eject is off" :
               !memberActive ? "its member is not an active tunnel port" :
               "not listed by the driver behind its member",
               presence == MK_P_PRESENT ? pbdf : member);
    } else {
        mk_log("mark removal failed, kept for the next poll: %s (error %lu)",
               id, GetLastError());
    }
    mk_unlock(m);
}

/* Every record, read whole before any is judged (a judgement deletes
 * values and so moves the enumeration's indices), and judged once a poll. */
static void mk_reconcile_all(void)
{
    HKEY k;
    DWORD i;
    char name[INST_ID_CHARS + 8];
    char text[96];
    DWORD nn;
    DWORD tn;
    DWORD type;
    DWORD values = 0;
    char (*ids)[INST_ID_CHARS + 8];
    char (*texts)[96];
    int count = 0;
    int c;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, MK_MARKS_KEY, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return;
    }
    if (RegQueryInfoKeyA(k, NULL, NULL, NULL, NULL, NULL, NULL, &values,
                         NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
        RegCloseKey(k);
        return;
    }
    if (values > MK_MAX_RECORDS) {
        values = MK_MAX_RECORDS;
    }
    if (values == 0) {
        RegCloseKey(k);
        return;
    }
    ids = malloc(values * sizeof(*ids));
    texts = malloc(values * sizeof(*texts));
    if (ids == NULL || texts == NULL) {
        free(ids);
        free(texts);
        RegCloseKey(k);
        return;
    }
    for (i = 0; (DWORD)count < values; i++) {
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

/* The CriticalDeviceDatabase scrub (record 21 section 7.4): Windows 2000
 * and XP copy a boot-critical device's own LowerFilters into its class's
 * entry there when they install it (pci#cc_0604 for a bridge,
 * pci#cc_010802 for an NVMe controller, read on 2000 on 2026-10-19), and
 * from then on every new device of that class would get xhcicm as a lower
 * filter, recorded nowhere - against "never on another PCI device". Every
 * poll, under the write mutex, xhcicm is taken out of every entry's
 * LowerFilters, the other names kept, the value deleted when nothing else
 * is in it. The entries' UpperFilters copies of the filter INF are its own
 * and stay while it is installed; the uninstall removes those. Returns how
 * many values it changed; -1 when the key could not be read. */
static int mk_cddb_scrub(int *complete)
{
    HKEY root;
    HKEY k;
    HANDLE m;
    DWORD i;
    DWORD nn;
    DWORD type;
    DWORD n;
    LONG r;
    char name[256];
    char (*names)[256];
    DWORD count = 0;
    DWORD c;
    char f[MK_FILTERS_BYTES];
    char nf[MK_FILTERS_BYTES];
    size_t ob;
    int changed = 0;
    int act;

    *complete = 0;
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_CDDB_KEY, 0, KEY_READ, &root);
    if (r == ERROR_FILE_NOT_FOUND) {
        *complete = 1;
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        mk_log("CriticalDeviceDatabase: the key did not open (error %ld)", r);
        return 0;
    }
    if (RegQueryInfoKeyA(root, NULL, NULL, NULL, &count, NULL, NULL, NULL,
                         NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
        mk_log("CriticalDeviceDatabase: the key could not be read");
        RegCloseKey(root);
        return 0;
    }
    if (count == 0) {
        RegCloseKey(root);
        *complete = 1;
        return 0;
    }
    names = malloc(count * sizeof(*names));
    if (names == NULL) {
        RegCloseKey(root);
        return 0;
    }
    *complete = 1;
    for (i = 0, c = 0; c < count; i++) {
        nn = sizeof(name);
        r = RegEnumKeyExA(root, i, name, &nn, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS) {
            /* A name longer than the buffer or a read that failed: the walk
             * cannot prove every entry clean. */
            *complete = 0;
            if (r != ERROR_MORE_DATA) {
                break;
            }
            continue;
        }
        inst_copy(names[c], name, sizeof(names[c]));
        c++;
    }
    count = c;
    m = NULL;
    for (c = 0; c < count; c++) {
        r = RegOpenKeyExA(root, names[c], 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                          &k);
        if (r != ERROR_SUCCESS) {
            if (r != ERROR_FILE_NOT_FOUND) {
                *complete = 0;
            }
            continue;
        }
        memset(f, 0, sizeof(f));
        n = sizeof(f) - 2;
        r = RegQueryValueExA(k, "LowerFilters", NULL, &type, (LPBYTE)f, &n);
        if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
            *complete = 0;
            if (!scrubFailLogged) {
                mk_log("CriticalDeviceDatabase\\%s: its LowerFilters could "
                       "not be read (error %ld)", names[c], r);
            }
        }
        if (r == ERROR_SUCCESS && (type == REG_MULTI_SZ || type == REG_SZ)) {
            act = inst_filter_drop(f, n + 1, MK_NAME, nf, sizeof(nf), &ob);
            if (act != INST_FD_NONE) {
                if (m == NULL) {
                    m = mk_lock(MK_WRITE_MUTEX, 5000);
                    if (m == NULL) {
                        RegCloseKey(k);
                        *complete = 0;
                        break;
                    }
                }
                /* Read again under the lock: the installer edits it too. */
                memset(f, 0, sizeof(f));
                n = sizeof(f) - 2;
                r = RegQueryValueExA(k, "LowerFilters", NULL, &type,
                                     (LPBYTE)f, &n);
                if (r == ERROR_SUCCESS &&
                    (type == REG_MULTI_SZ || type == REG_SZ)) {
                    act = inst_filter_drop(f, n + 1, MK_NAME, nf, sizeof(nf),
                                           &ob);
                } else {
                    act = INST_FD_NONE;
                    if (r != ERROR_FILE_NOT_FOUND) {
                        /* Not gone, not readable: the entry is unresolved. */
                        *complete = 0;
                        mk_log("CriticalDeviceDatabase\\%s: its LowerFilters "
                               "could not be read again (error %ld)", names[c],
                               r);
                    }
                }
            }
            if (act == INST_FD_SET) {
                r = RegSetValueExA(k, "LowerFilters", 0, REG_MULTI_SZ,
                                   (const BYTE *)nf, (DWORD)ob);
            } else if (act == INST_FD_DELETE) {
                r = RegDeleteValueA(k, "LowerFilters");
            } else if (act == INST_FD_ERROR) {
                r = ERROR_INSUFFICIENT_BUFFER;
            }
            if (act != INST_FD_NONE) {
                if (r != ERROR_SUCCESS) {
                    *complete = 0;
                }
                if (r == ERROR_SUCCESS) {
                    changed++;
                    mk_log("CriticalDeviceDatabase\\%s: %s taken out of its "
                           "LowerFilters (Windows copied it there from a "
                           "marked unit's install)", names[c], MK_NAME);
                } else {
                    mk_log("CriticalDeviceDatabase\\%s: %s could not be taken "
                           "out of its LowerFilters (error %ld)", names[c],
                           MK_NAME, r);
                }
            }
        }
        RegCloseKey(k);
    }
    mk_unlock(m);
    free(names);
    RegCloseKey(root);
    return changed;
}

/* Every PCI devnode, present or not, whose LowerFilters carries the name
 * and which has no record: recorded, then judged at once as any record is -
 * kept only on a unit behind an active member of a verifiable domain,
 * removed from any other device (one that took the name from a
 * CriticalDeviceDatabase copy, say) once its domain can be judged. */
static void mk_inventory(int quiet)
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
            mk_log("inventory: an unrecorded mark on %s, recorded to be "
                   "judged (kept only on a unit behind an active member)", id);
            found++;
        }
    }
    api.Destroy(set);
    if (!quiet || found != 0) {
        mk_log("inventory: %d unrecorded mark(s)", found);
    }
}

/* ======================================================================== */
/* Stor98 on a tunnelled drive (record 21 section 7.5, task 43.59)          */
/* ======================================================================== */

/* On 8, 8.1 and 10 Windows gives a tunnelled NVMe drive first plugged
 * after the install its own stornvme, which outranks the uncatalogued
 * Stor98 on the signature score. With Parameters\Stor98Inf naming the
 * staged Stor98 (the installer writes it on those systems alone), each
 * started stornvme devnode below a verified tunnel port is stopped, given
 * that one INF's driver and started again. Its record under
 * Services\xhcimark\Stor98 is written before each act; a disable is never
 * left behind unrecorded, and a record owing an enable stays until the
 * enable is seen. */

#define MK_S98_KEY_PATH     MK_SVC_KEY "\\" MK_S98_KEY
#define MK_S98_OWN_KEY      "Software\\xhci98\\Stor98"
#define MK_S98_SWITCH_MUTEX "Global\\Xhci98S98Switch"
#define MK_S98_PAUSE_EVENT  "Global\\Xhci98S98Pause"
#define MK_S98_SEEN_MAX     256
#define MK_S98_TODO_MAX     16
#define MK_PROB_DISABLED    0x00000016UL
#define MK_DICS_ENABLE      0x00000001UL
#define MK_DICS_DISABLE     0x00000002UL
#define MK_DICS_FLAG_CONFIGSPECIFIC 0x00000002UL
#define MK_DIF_SELECTBESTCOMPATDRV 0x00000017UL
#define MK_DI_ENUMSINGLEINF 0x00010000UL
#define MK_SPDIT_COMPATDRIVER 0x00000002UL
#define MK_DIIDFLAG_NOFINISHINSTALLUI 0x00000002UL

typedef BOOL (WINAPI *PFN_SETDIPARAMS)(HDEVINFO, PSP_DEVINFO_DATA,
                                       PSP_DEVINSTALL_PARAMS_A);
typedef BOOL (WINAPI *PFN_DRVLIST)(HDEVINFO, PSP_DEVINFO_DATA, DWORD);
typedef DWORD (WINAPI *PFN_HWPROFFLAGS)(LPSTR, ULONG, PULONG, ULONG);
#define MK_CSCONFIGFLAG_DISABLED 0x00000001UL
typedef BOOL (WINAPI *PFN_GETSELDRVW)(HDEVINFO, PSP_DEVINFO_DATA,
                                      PSP_DRVINFO_DATA_W);
typedef BOOL (WINAPI *PFN_DIINSTALL)(HWND, HDEVINFO, PSP_DEVINFO_DATA,
                                     PSP_DRVINFO_DATA_W, DWORD, PBOOL);

/* Looked up at the first switch, so 2000 to 7, which never have the
 * value, never ask for newdev.dll's DiInstallDevice (Vista and later). */
static struct {
    int Tried;
    int Ok;
    HMODULE NewDev;
    PFN_SETDIPARAMS SetDiParams;
    PFN_DRVLIST Build;
    PFN_DRVLIST DestroyDrv;
    PFN_GETSELDRVW GetSelected;
    PFN_DIINSTALL Install;
    PFN_HWPROFFLAGS HwProfFlags;    /* optional: CM_Get_HW_Prof_FlagsA */
} s98api;

/* The devnodes on stornvme seen at whole walks: their arrival. */
static struct {
    char Id[INST_ID_CHARS];
    DWORD Walk;             /* the last whole walk that saw it            */
    int Attempts;           /* in this arrival                            */
    DWORD At;               /* the last attempt                           */
    int Why;                /* the reason last logged, -1 none            */
} s98Seen[MK_S98_SEEN_MAX];
static int s98SeenCount;
static DWORD s98Walk;           /* whole walks so far                     */
static int s98PolicyState = -1; /* the value's last logged state          */
static int s98FirstDone;        /* the first poll's restart records read  */
static DWORD s98Cursor;         /* the record the next recovery reads first */
static char s98EnableLogged[MK_S98_TODO_MAX][INST_ID_CHARS];

static int mk_s98_api(void)
{
    int missing = 0;

    if (s98api.Tried) {
        return s98api.Ok;
    }
    s98api.Tried = 1;
    s98api.NewDev = LoadLibraryA("newdev.dll");
    s98api.SetDiParams = (PFN_SETDIPARAMS)mk_proc(api.SetupApi,
                             "SetupDiSetDeviceInstallParamsA", &missing);
    s98api.Build = (PFN_DRVLIST)mk_proc(api.SetupApi,
                       "SetupDiBuildDriverInfoList", &missing);
    s98api.DestroyDrv = (PFN_DRVLIST)mk_proc(api.SetupApi,
                            "SetupDiDestroyDriverInfoList", &missing);
    s98api.GetSelected = (PFN_GETSELDRVW)mk_proc(api.SetupApi,
                             "SetupDiGetSelectedDriverW", &missing);
    s98api.Install = (PFN_DIINSTALL)mk_proc(s98api.NewDev, "DiInstallDevice",
                                            &missing);
    s98api.HwProfFlags = (PFN_HWPROFFLAGS)GetProcAddress(api.SetupApi,
                             "CM_Get_HW_Prof_FlagsA");
    s98api.Ok = missing == 0;
    if (!s98api.Ok) {
        mk_log("Stor98: the switch's routines are missing; no drive is "
               "switched");
    }
    return s98api.Ok;
}

/* The value, when it names a staged INF that exists: 1 with inf filled. */
static int mk_s98_policy(char *inf, size_t cap)
{
    HKEY k;
    DWORD type;
    DWORD n = (DWORD)cap - 1;
    char windir[MAX_PATH];
    int state = 0;
    DWORD a;

    memset(inf, 0, cap);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, MK_PARAMS_KEY, 0, KEY_READ, &k) ==
        ERROR_SUCCESS) {
        if (RegQueryValueExA(k, MK_S98_VALUE, NULL, &type, (LPBYTE)inf, &n) ==
                ERROR_SUCCESS && type == REG_SZ) {
            inf[cap - 1] = '\0';
            state = 1;
        } else {
            memset(inf, 0, cap);
        }
        RegCloseKey(k);
    }
    if (state == 1) {
        if (GetWindowsDirectoryA(windir, sizeof(windir)) == 0 ||
            !mk_s98_inf_ok(inf, windir)) {
            state = 2;
        } else {
            a = GetFileAttributesA(inf);
            if (a == 0xFFFFFFFFUL || (a & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                state = 3;
            }
        }
    }
    if (state != s98PolicyState) {
        s98PolicyState = state;
        if (state == 1) {
            mk_log("Stor98: tunnelled drives on stornvme are switched to %s",
                   inf);
        } else if (state != 0) {
            mk_log("Stor98: %s names %s, %s; no drive is switched",
                   MK_S98_VALUE, inf, state == 2 ? "not a staged INF"
                                                 : "a file that is not there");
        }
    }
    return state == 1;
}

static int mk_s98_rec_get(const char *id)
{
    HKEY k;
    DWORD type;
    char text[32];
    DWORD n = sizeof(text) - 1;
    LONG r;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, MK_S98_KEY_PATH, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return MK_S98_NONE;
    }
    memset(text, 0, sizeof(text));
    r = RegQueryValueExA(k, id, NULL, &type, (LPBYTE)text, &n);
    RegCloseKey(k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return MK_S98_NONE;
    }
    if (r != ERROR_SUCCESS || type != REG_SZ) {
        return MK_S98_FAILED;
    }
    text[sizeof(text) - 1] = '\0';
    return mk_s98_rec_parse(text);
}

static int mk_s98_rec_set(const char *id, int state)
{
    HKEY k;
    DWORD disp;
    const char *text = mk_s98_rec_text(state);
    LONG r;

    if (text == NULL ||
        RegCreateKeyExA(HKEY_LOCAL_MACHINE, MK_S98_KEY_PATH, 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &k,
                        &disp) != ERROR_SUCCESS) {
        return 0;
    }
    r = RegSetValueExA(k, id, 0, REG_SZ, (const BYTE *)text,
                       (DWORD)strlen(text) + 1);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS) {
        mk_log("Stor98: %s: the record \"%s\" could not be written (error "
               "%ld)", id, text, (long)r);
    }
    return r == ERROR_SUCCESS;
}

/* "done": the drive is also entered under the installer's record of
 * Stor98's devices (record 26 section 6.3), as a device the installer's
 * own call bound is, so the uninstall gives it back to stornvme whatever
 * PreExisting says. Only into a record the installer made. */
static int mk_s98_own(const char *id)
{
    HKEY k;
    HKEY d;
    LONG r;
    static const char text[] = "switched";

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, MK_S98_OWN_KEY, 0, KEY_CREATE_SUB_KEY,
                      &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        mk_log("Stor98: %s is bound to Stor98, and the installer's record of "
               "Stor98 is not there: not entered", id);
        return 1;
    }
    if (r != ERROR_SUCCESS) {
        mk_log("Stor98: the installer's record of Stor98 did not open (error "
               "%ld)", (long)r);
        return 0;
    }
    r = RegCreateKeyExA(k, "Devices", 0, NULL, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, NULL, &d, NULL);
    RegCloseKey(k);
    if (r == ERROR_SUCCESS) {
        r = RegSetValueExA(d, id, 0, REG_SZ, (const BYTE *)text,
                           (DWORD)sizeof(text));
        RegCloseKey(d);
    }
    if (r != ERROR_SUCCESS) {
        mk_log("Stor98: %s could not be entered among Stor98's devices "
               "(error %ld)", id, (long)r);
        return 0;
    }
    return 1;
}

/* A final state, after the ownership entry when the devnode is bound to
 * Stor98 (its Service value, started or not): an entry that cannot be
 * written leaves the record "enable", so the recovery tries again. */
static void mk_s98_finish(const char *id, int state, int bound)
{
    if (bound && !mk_s98_own(id)) {
        (void)mk_s98_rec_set(id, MK_S98_ENABLE);
        return;
    }
    (void)mk_s98_rec_set(id, state);
}

/* 1 while an installer run holds its pause event open. */
static int mk_s98_paused(void)
{
    HANDLE e = OpenEventA(SYNCHRONIZE, FALSE, MK_S98_PAUSE_EVENT);

    if (e != NULL) {
        CloseHandle(e);
        return 1;
    }
    return GetLastError() != ERROR_FILE_NOT_FOUND;
}

/* The eject mutex, then the switch mutex the installer waits on, with no
 * installer run in progress: 1 with both held. */
static int mk_s98_lock(HANDLE *em, HANDLE *sm)
{
    *em = mk_lock(MK_EJECT_MUTEX, 1000);
    if (*em == NULL) {
        return 0;
    }
    *sm = mk_lock(MK_S98_SWITCH_MUTEX, 1000);
    if (*sm == NULL) {
        mk_unlock(*em);
        return 0;
    }
    if (mk_s98_paused()) {
        mk_unlock(*sm);
        mk_unlock(*em);
        return 0;
    }
    return 1;
}

/* DIF_PROPERTYCHANGE on the one devnode; 1 when made with no restart asked
 * (*vetoed 1 when Windows answered DI_NEEDREBOOT or DI_NEEDRESTART). */
static int mk_s98_propchange(HDEVINFO set, SP_DEVINFO_DATA *data,
                             DWORD change, int *vetoed)
{
    SP_PROPCHANGE_PARAMS pcp;
    SP_DEVINSTALL_PARAMS_A ip;

    *vetoed = 0;
    /* The restart flags stay in the element's install parameters from an
     * earlier call (a refused stop's): cleared first, so only this call's
     * own answer is read. */
    memset(&ip, 0, sizeof(ip));
    ip.cbSize = sizeof(ip);
    if (s98api.SetDiParams != NULL && api.GetDiParams(set, data, &ip) &&
        (ip.Flags & (MK_DI_NEEDREBOOT | MK_DI_NEEDRESTART)) != 0) {
        ip.Flags &= ~(DWORD)(MK_DI_NEEDREBOOT | MK_DI_NEEDRESTART);
        (void)s98api.SetDiParams(set, data, &ip);
    }
    memset(&pcp, 0, sizeof(pcp));
    pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    pcp.ClassInstallHeader.InstallFunction = MK_DIF_PROPERTYCHANGE;
    pcp.StateChange = change;
    pcp.Scope = MK_DICS_FLAG_CONFIGSPECIFIC;
    pcp.HwProfile = 0;
    if (!api.SetCiParams(set, data, &pcp.ClassInstallHeader, sizeof(pcp)) ||
        !api.CallCi(MK_DIF_PROPERTYCHANGE, set, data)) {
        return 0;
    }
    memset(&ip, 0, sizeof(ip));
    ip.cbSize = sizeof(ip);
    if (api.GetDiParams(set, data, &ip) &&
        (ip.Flags & (MK_DI_NEEDREBOOT | MK_DI_NEEDRESTART)) != 0) {
        *vetoed = 1;
        return 0;
    }
    return 1;
}

/* 1 when the current hardware profile's flags of `id` read and do not
 * disable it: what a DICS_ENABLE with DICS_FLAG_CONFIGSPECIFIC writes. 0
 * when they disable it or cannot be read. */
static int mk_s98_profile_enabled(const char *id)
{
    char buf[INST_ID_CHARS];
    ULONG flags = 0;

    if (s98api.HwProfFlags == NULL) {
        return 0;
    }
    inst_copy(buf, id, sizeof(buf));
    if (s98api.HwProfFlags(buf, 0, &flags, 0) != MK_CR_SUCCESS) {
        return 0;
    }
    return (flags & MK_CSCONFIGFLAG_DISABLED) == 0;
}

/* Opens the one devnode into a set of its own; INVALID_HANDLE_VALUE when
 * it cannot. */
static HDEVINFO mk_s98_open(const char *id, SP_DEVINFO_DATA *data)
{
    HDEVINFO set = api.CreateList(NULL, NULL);

    if (set == INVALID_HANDLE_VALUE) {
        return set;
    }
    memset(data, 0, sizeof(*data));
    data->cbSize = sizeof(*data);
    if (!api.OpenInfo(set, id, NULL, 0, data)) {
        api.Destroy(set);
        return INVALID_HANDLE_VALUE;
    }
    return set;
}

/* The devnode's fresh state: present, its service, started, its problem. */
static int mk_s98_fresh(const char *id, char *service, size_t cap,
                        ULONG *st, ULONG *pr, DWORD *dn)
{
    memset(service, 0, cap);
    *st = 0;
    *pr = 0;
    if (api.Locate(dn, (char *)id, MK_CM_LOCATE_NORMAL) != MK_CR_SUCCESS) {
        return 0;
    }
    (void)mk_prop_text(*dn, MK_CM_DRP_SERVICE, service, cap, NULL);
    if (api.Status(st, pr, *dn, 0) != MK_CR_SUCCESS) {
        *st = 0;
        *pr = 0;
        return 0;
    }
    return 1;
}

static int mk_s98_disabled(ULONG st, ULONG pr)
{
    return (st & MK_DN_HAS_PROBLEM) != 0 && pr == MK_PROB_DISABLED;
}

static int mk_s98_runs(const char *service, ULONG st)
{
    return inst_eq(service, "stor98") && (st & MK_DN_STARTED) != 0 &&
           (st & MK_DN_HAS_PROBLEM) == 0;
}

/* 1 when the fresh devnode's ancestors still reach member location `bdf`. */
static int mk_s98_still_below(DWORD dn, INST_U32 bdf)
{
    DWORD p;
    ULONG bus;
    ULONG address;
    int steps;

    for (steps = 0; steps < 64; steps++) {
        if (api.Parent(&p, dn, 0) != MK_CR_SUCCESS) {
            return 0;
        }
        if (mk_prop_dword(p, MK_CM_DRP_BUSNUMBER, &bus) &&
            mk_prop_dword(p, MK_CM_DRP_ADDRESS, &address) &&
            bus == ((bdf >> 8) & 0xFFUL) && address == mk_addr_of(bdf)) {
            return 1;
        }
        dn = p;
    }
    return 0;
}

/* The member's domain read again: its NHI started, verifiable, the epoch
 * the poll sampled, the member still active (as mk_fresh_same reads it). */
static int mk_s98_domain_fresh(const MK_NHI *h, INST_U32 member)
{
    static MK_NHI f;
    ULONG st;
    ULONG pr;

    memset(&f, 0, sizeof(f));
    f.Node = h->Node;
    if (api.Status(&st, &pr, nodes[h->Node].Dn, 0) != MK_CR_SUCCESS) {
        return 0;
    }
    f.DevOk = (st & MK_DN_STARTED) != 0 && (st & MK_DN_HAS_PROBLEM) == 0;
    return mk_read_nhi(&f) && mk_verifiable(f.DevOk, &f.Q, &f.M) &&
           f.Q.EpochLo == h->Q.EpochLo && f.Q.EpochHi == h->Q.EpochHi &&
           mk_member_active(&f.Q, member);
}

/* The switch of one devnode (section 7.5.3, "The switch"), under the tools'
 * eject mutex and the switch mutex, with no installer pause. Returns the
 * state recorded, or MK_S98_NONE when nothing was done (a lock held, the
 * pause on, a fresh read changed). */
static int mk_s98_switch(const char *id, const MK_NHI *h, INST_U32 member,
                         const char *inf)
{
    HANDLE em;
    HANDLE sm;
    HDEVINFO set = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA data;
    SP_DEVINSTALL_PARAMS_A ip;
    SP_DRVINFO_DATA_W drv;
    char service[64];
    ULONG st;
    ULONG pr;
    DWORD dn;
    BOOL reboot = FALSE;
    int vetoed;
    int result = MK_S98_NONE;
    int built = 0;
    int installed = 0;
    int enabled;
    int fresh;
    DWORD err = 0;

    /* An installer run keeps its pause event open from its plan to its
     * end: no switch beside its reads and Stor98 jobs. */
    if (!mk_s98_lock(&em, &sm)) {
        return MK_S98_NONE;
    }
    if (!mk_no_install_pending() ||
        !mk_s98_fresh(id, service, sizeof(service), &st, &pr, &dn) ||
        !inst_s98_over_inbox(1, service) || (st & MK_DN_STARTED) == 0 ||
        (st & MK_DN_HAS_PROBLEM) != 0 || !mk_s98_still_below(dn, member) ||
        !mk_s98_domain_fresh(h, member)) {
        mk_log("Stor98: %s or its domain changed before its switch; looked "
               "at again", id);
        goto out;
    }
    set = mk_s98_open(id, &data);
    if (set == INVALID_HANDLE_VALUE) {
        mk_log("Stor98: %s could not be opened (error %lu)", id,
               GetLastError());
        goto out;
    }
    if (!mk_s98_rec_set(id, MK_S98_DISABLE)) {
        goto out;
    }
    mk_log("Stor98: switching %s from stornvme (member %04lX)", id, member);
    if (!mk_s98_propchange(set, &data, MK_DICS_DISABLE, &vetoed)) {
        int refused = vetoed;
        err = GetLastError();
        /* A refused stop asked for a restart; the enable withdraws it. Its
         * own answer is a restart too while the devnode runs on (Windows 10
         * x86, 2026-10-22): then the profile's flag the enable clears is
         * what shows it held. */
        st = 0;
        pr = 0;
        enabled = mk_s98_propchange(set, &data, MK_DICS_ENABLE, &vetoed);
        fresh = mk_s98_fresh(id, service, sizeof(service), &st, &pr, &dn);
        if (!enabled && vetoed && fresh && (st & MK_DN_STARTED) != 0 &&
            !mk_s98_disabled(st, pr) && mk_s98_profile_enabled(id)) {
            enabled = 1;
        }
        if (!enabled || !fresh || mk_s98_disabled(st, pr)) {
            mk_log("Stor98: %s: the stop was %s (error %lu), and its enable "
                   "is not seen (it answered %s; status %08lX problem %lu); "
                   "it is owed", id, refused ? "refused" : "not made", err,
                   enabled ? "done" : vetoed ? "a restart" : "a failure", st,
                   pr);
            result = MK_S98_ENABLE;
        } else {
            result = refused ? MK_S98_BUSY : MK_S98_FAILED;
            mk_log("Stor98: %s: the stop was %s (error %lu); left on "
                   "stornvme", id, refused ? "refused" : "not made", err);
        }
        goto record;
    }
    if (!mk_s98_fresh(id, service, sizeof(service), &st, &pr, &dn) ||
        (st & MK_DN_STARTED) != 0 || !mk_s98_disabled(st, pr)) {
        mk_log("Stor98: %s was not left stopped (status %08lX problem %lu)",
               id, st, pr);
        result = MK_S98_FAILED;
    } else if (!mk_s98_rec_set(id, MK_S98_INSTALL)) {
        result = MK_S98_FAILED;
    } else {
        memset(&ip, 0, sizeof(ip));
        ip.cbSize = sizeof(ip);
        if (api.GetDiParams(set, &data, &ip)) {
            ip.Flags |= MK_DI_ENUMSINGLEINF;
            inst_copy(ip.DriverPath, inf, sizeof(ip.DriverPath));
            if (s98api.SetDiParams(set, &data, &ip) &&
                s98api.Build(set, &data, MK_SPDIT_COMPATDRIVER)) {
                built = 1;
                memset(&drv, 0, sizeof(drv));
                drv.cbSize = sizeof(drv);
                if (api.CallCi(MK_DIF_SELECTBESTCOMPATDRV, set, &data) &&
                    s98api.GetSelected(set, &data, &drv)) {
                    installed = s98api.Install(NULL, set, &data, &drv,
                                               MK_DIIDFLAG_NOFINISHINSTALLUI,
                                               &reboot) ? 1 : 0;
                }
            }
        }
        if (!installed) {
            err = GetLastError();
            mk_log("Stor98: %s: Stor98 was not installed (error %lu)", id,
                   err);
        } else if (reboot) {
            mk_log("Stor98: %s: the install asked for a restart of Windows",
                   id);
        }
        if (built) {
            (void)s98api.DestroyDrv(set, &data, MK_SPDIT_COMPATDRIVER);
        }
    }
    enabled = mk_s98_propchange(set, &data, MK_DICS_ENABLE, &vetoed);
    if (!enabled) {
        mk_log("Stor98: %s: the enable answered %s (error %lu)", id,
               vetoed ? "a restart" : "a failure", GetLastError());
    }
    if (!mk_s98_fresh(id, service, sizeof(service), &st, &pr, &dn) ||
        mk_s98_disabled(st, pr)) {
        mk_log("Stor98: %s: its enable is not seen; it is owed", id);
        result = MK_S98_ENABLE;
    } else if (mk_s98_runs(service, st)) {
        result = MK_S98_DONE;
        mk_log("Stor98: %s now runs Stor98", id);
    } else {
        if (result == MK_S98_NONE) {
            result = installed && reboot ? MK_S98_RESTART : MK_S98_FAILED;
        }
        mk_log("Stor98: %s reads service %s, status %08lX, problem %lu after "
               "the switch (%s)", id, service[0] ? service : "(none)", st, pr,
               mk_s98_rec_text(result));
    }
record:
    if (result == MK_S98_ENABLE || result == MK_S98_BUSY) {
        (void)mk_s98_rec_set(id, result);
    } else {
        mk_s98_finish(id, result, inst_eq(service, "stor98"));
    }
out:
    if (set != INVALID_HANDLE_VALUE) {
        api.Destroy(set);
    }
    mk_unlock(sm);
    mk_unlock(em);
    return result;
}

/* 1 when the enable owed for `id` has been logged this run (so it is
 * logged once); `mark` records it. */
static int mk_s98_enable_logged(const char *id, int mark)
{
    int i;

    for (i = 0; i < MK_S98_TODO_MAX; i++) {
        if (inst_eq(s98EnableLogged[i], id)) {
            return 1;
        }
    }
    if (mark) {
        for (i = 0; i < MK_S98_TODO_MAX; i++) {
            if (s98EnableLogged[i][0] == '\0') {
                inst_copy(s98EnableLogged[i], id, sizeof(s98EnableLogged[i]));
                break;
            }
        }
    }
    return 0;
}

/* Every record outside a switch (mk_s98_recover): an enable owed is made
 * once the devnode is present, the record kept until it is seen; a
 * "restart" is decided at the first poll after the service's start. At most
 * MK_S98_TODO_MAX records a poll, from a cursor that goes on where the last
 * poll stopped, so absent drives owing an enable never starve the others;
 * "first" lasts until the first pass has read every record. */
static void mk_s98_recover_all(void)
{
    HKEY k;
    DWORD i;
    char id[INST_ID_CHARS];
    char text[32];
    DWORD in;
    DWORD tn;
    DWORD type;
    char service[64];
    ULONG st;
    ULONG pr;
    DWORD dn;
    int present;
    int a;
    int r;
    int vetoed;
    int ok;
    HANDLE em;
    HANDLE sm;
    HDEVINFO set;
    SP_DEVINFO_DATA data;
    static char todo[MK_S98_TODO_MAX][INST_ID_CHARS];
    int nTodo = 0;
    int j;
    int first;
    DWORD count = 0;
    DWORD step;
    DWORD next;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, MK_S98_KEY_PATH, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        s98FirstDone = 1;
        s98Cursor = 0;
        return;
    }
    if (RegQueryInfoKeyA(k, NULL, NULL, NULL, NULL, NULL, NULL, &count, NULL,
                         NULL, NULL, NULL) != ERROR_SUCCESS) {
        count = 0;
    }
    if (s98Cursor >= count) {
        s98Cursor = 0;
    }
    next = 0;                   /* 0: this pass reached the list's end */
    for (step = 0; step < count; step++) {
        i = (s98Cursor + step) % count;
        if (nTodo == MK_S98_TODO_MAX) {
            next = i;
            break;
        }
        in = sizeof(id) - 1;
        tn = sizeof(text) - 1;
        memset(id, 0, sizeof(id));
        memset(text, 0, sizeof(text));
        if (RegEnumValueA(k, i, id, &in, NULL, &type, (LPBYTE)text, &tn) !=
            ERROR_SUCCESS) {
            continue;
        }
        id[sizeof(id) - 1] = '\0';
        r = type == REG_SZ ? mk_s98_rec_parse(text) : MK_S98_FAILED;
        if (r == MK_S98_DISABLE || r == MK_S98_INSTALL ||
            r == MK_S98_ENABLE || (r == MK_S98_RESTART && !s98FirstDone)) {
            inst_copy(todo[nTodo++], id, sizeof(todo[0]));
        }
    }
    RegCloseKey(k);
    if (nTodo == 0) {
        s98FirstDone = 1;
        s98Cursor = 0;
        return;
    }
    /* The same locks as a switch: nothing beside an installer run. The
     * first poll's "restart" records wait for the first poll that runs. */
    if (!mk_s98_lock(&em, &sm)) {
        return;
    }
    first = !s98FirstDone;
    if (next == 0) {
        s98FirstDone = 1;
    }
    s98Cursor = next;
    for (j = 0; j < nTodo; j++) {
        r = mk_s98_rec_get(todo[j]);
        if (r == MK_S98_RESTART && !first) {
            continue;
        }
        present = mk_s98_fresh(todo[j], service, sizeof(service), &st, &pr,
                               &dn);
        a = mk_s98_recover(r, present, mk_s98_disabled(st, pr),
                           mk_s98_runs(service, st), first);
        if (a == MK_S98_R_ENABLE) {
            ok = 0;
            set = mk_s98_open(todo[j], &data);
            if (set != INVALID_HANDLE_VALUE) {
                (void)mk_s98_propchange(set, &data, MK_DICS_ENABLE, &vetoed);
                api.Destroy(set);
                ok = mk_s98_fresh(todo[j], service, sizeof(service), &st,
                                  &pr, &dn) && !mk_s98_disabled(st, pr);
            }
            if (!ok) {
                if (!mk_s98_enable_logged(todo[j], 1)) {
                    mk_log("Stor98: %s, left disabled by a switch, could not "
                           "be enabled yet (error %lu); tried again each poll",
                           todo[j], GetLastError());
                }
                if (r != MK_S98_ENABLE) {
                    (void)mk_s98_rec_set(todo[j], MK_S98_ENABLE);
                }
                continue;
            }
            mk_log("Stor98: %s, left disabled by a switch, enabled again",
                   todo[j]);
            a = mk_s98_recover(MK_S98_ENABLE, 1, 0, mk_s98_runs(service, st),
                               first);
        }
        if (a == MK_S98_R_DONE || a == MK_S98_R_FAIL) {
            mk_s98_finish(todo[j], a == MK_S98_R_DONE ? MK_S98_DONE
                                                      : MK_S98_FAILED,
                          inst_eq(service, "stor98"));
            mk_log("Stor98: %s recorded %s (service %s)", todo[j],
                   a == MK_S98_R_DONE ? "done" : "failed",
                   service[0] ? service : "(none)");
        }
    }
    mk_unlock(sm);
    mk_unlock(em);
}

/* The records whose devnode Windows no longer has, even as a phantom. A
 * record owing an enable stays: its devnode's settings would come back
 * with it. */
static void mk_s98_prune(void)
{
    HKEY k;
    DWORD i;
    DWORD dn;
    DWORD type;
    char id[INST_ID_CHARS];
    char text[32];
    DWORD in;
    DWORD tn;
    int r;
    static char gone[MK_S98_TODO_MAX][INST_ID_CHARS];
    int nGone = 0;
    int j;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, MK_S98_KEY_PATH, 0,
                      KEY_READ | KEY_SET_VALUE, &k) != ERROR_SUCCESS) {
        return;
    }
    for (i = 0; nGone < MK_S98_TODO_MAX; i++) {
        in = sizeof(id) - 1;
        tn = sizeof(text) - 1;
        memset(text, 0, sizeof(text));
        if (RegEnumValueA(k, i, id, &in, NULL, &type, (LPBYTE)text, &tn) !=
            ERROR_SUCCESS) {
            break;
        }
        id[sizeof(id) - 1] = '\0';
        r = type == REG_SZ ? mk_s98_rec_parse(text) : MK_S98_FAILED;
        if (r != MK_S98_DISABLE && r != MK_S98_INSTALL &&
            r != MK_S98_ENABLE &&
            api.Locate(&dn, id, MK_CM_LOCATE_PHANTOM) ==
                MK_CR_NO_SUCH_DEVNODE) {
            inst_copy(gone[nGone++], id, sizeof(gone[0]));
        }
    }
    for (j = 0; j < nGone; j++) {
        if (RegDeleteValueA(k, gone[j]) == ERROR_SUCCESS) {
            mk_log("Stor98: the record of %s, which Windows no longer has, "
                   "deleted", gone[j]);
        }
    }
    RegCloseKey(k);
}

/* The arrival entry of `id` at this whole walk: a devnode not seen at the
 * walk before starts a new arrival. With `create` 0, only an entry already
 * there is kept: mk_s98_poll does that for every devnode present before it
 * looks at the candidates, so an entry not seen at this walk is a devnode
 * gone, the one a full table gives up. With none to give up (more than
 * MK_S98_SEEN_MAX devnodes on stornvme at once), -1, and that devnode is
 * not tried at this walk. -1 too with `create` 0 and no entry. */
static int mk_s98_seen(const char *id, int create)
{
    int i;
    int old = 0;

    for (i = 0; i < s98SeenCount; i++) {
        if (inst_eq(s98Seen[i].Id, id)) {
            if (s98Seen[i].Walk != s98Walk && s98Seen[i].Walk + 1 != s98Walk) {
                s98Seen[i].Attempts = 0;
                s98Seen[i].Why = -1;
            }
            s98Seen[i].Walk = s98Walk;
            return i;
        }
        if (s98Seen[i].Walk < s98Seen[old].Walk) {
            old = i;
        }
    }
    if (!create) {
        return -1;
    }
    if (s98SeenCount < MK_S98_SEEN_MAX) {
        i = s98SeenCount++;
    } else if (s98Seen[old].Walk != s98Walk) {
        i = old;
    } else {
        return -1;
    }
    memset(&s98Seen[i], 0, sizeof(s98Seen[i]));
    inst_copy(s98Seen[i].Id, id, sizeof(s98Seen[i].Id));
    s98Seen[i].Walk = s98Walk;
    s98Seen[i].Why = -1;
    return i;
}

/* The nearest ancestor of `node` marked a member: its index, or -1. */
static int mk_s98_member_node(int node, const int *member)
{
    int p;
    int steps;

    for (p = nodes[node].Parent, steps = 0; p >= 0 && steps < 64;
         p = nodes[p].Parent, steps++) {
        if (member[p]) {
            return p;
        }
    }
    return -1;
}

/* One poll's switch: records settled first, then at most one devnode
 * switched; every candidate's arrival entry is kept at every walk, the
 * ones after the switched one too, so no retry budget or delay restarts
 * for a drive that stayed. */
static void mk_s98_poll(const int *prot)
{
    static int parent[MK_MAX_NODES];
    static int member[MK_MAX_NODES];
    static int owner[MK_MAX_NODES];
    static INST_U32 bdf[MK_MAX_NODES];
    char inf[MAX_PATH];
    int policy;
    int i;
    int k;
    int m;
    INST_U32 p;
    int done = 0;

    s98Walk++;
    policy = mk_s98_policy(inf, sizeof(inf));
    /* An enable owed is made whatever the value says now. */
    mk_s98_recover_all();
    for (i = 0; i < nodeCount; i++) {
        parent[i] = nodes[i].Parent;
        member[i] = 0;
        owner[i] = -1;
        bdf[i] = 0;
    }
    for (i = 0; i < nhiCount; i++) {
        if (!nhis[i].Verifiable) {
            continue;
        }
        for (p = 0; p < nhis[i].Q.Ports && p < 4; p++) {
            if (mk_member_active(&nhis[i].Q, nhis[i].Q.Port[p])) {
                k = mk_find_loc(nhis[i].Q.Port[p]);
                if (k >= 0) {
                    member[k] = 1;
                    owner[k] = i;
                    bdf[k] = nhis[i].Q.Port[p];
                }
            }
        }
    }
    /* Every devnode present keeps its arrival, a candidate or not (its
     * properties unreadable at this walk, say). */
    for (i = 0; i < nodeCount && s98SeenCount > 0; i++) {
        (void)mk_s98_seen(nodes[i].Id, 0);
    }
    for (i = 0; i < nodeCount; i++) {
        MK_S98_IN in;
        int s;
        int w;
        int r;
        memset(&in, 0, sizeof(in));
        in.IsPci = mk_is_pci(&nodes[i]);
        in.Nvme = inst_msz_has(nodes[i].Compat, nodes[i].CompatBytes,
                               MK_S98_CC);
        in.Service = nodes[i].ServiceBad ? NULL : nodes[i].Service;
        w = mk_s98_wanted(&in);
        if (w == MK_S98_W_CLASS || w == MK_S98_W_SERVICE) {
            continue;
        }
        s = mk_s98_seen(nodes[i].Id, 1);
        if (s < 0 || done) {
            continue;
        }
        in.PolicyOk = policy;
        in.WalkWhole = walkBad == 0;
        in.StatusOk = nodes[i].StatusOk;
        in.Started = (nodes[i].Status & MK_DN_STARTED) != 0;
        in.Problem = (nodes[i].Status & MK_DN_HAS_PROBLEM) != 0;
        in.Below = mk_s98_below(parent, nodeCount, i, member);
        in.Protected = prot[i];
        in.Record = mk_s98_rec_get(nodes[i].Id);
        in.Attempts = s98Seen[s].Attempts;
        in.SinceMs = GetTickCount() - s98Seen[s].At;
        in.InstallPending = 0;
        w = mk_s98_wanted(&in);
        if (w == MK_S98_GO) {
            in.InstallPending = !mk_no_install_pending();
            w = mk_s98_wanted(&in);
        }
        m = w == MK_S98_GO ? mk_s98_member_node(i, member) : -1;
        if (w == MK_S98_GO && (m < 0 || owner[m] < 0)) {
            w = MK_S98_W_TUNNEL;
        }
        if (w != MK_S98_GO) {
            if (w != s98Seen[s].Why && w != MK_S98_W_POLICY) {
                mk_log("Stor98: %s on stornvme: %s", nodes[i].Id,
                       mk_s98_why_text(w));
            }
            s98Seen[s].Why = w;
            continue;
        }
        if (!mk_s98_api()) {
            return;
        }
        s98Seen[s].Attempts++;
        s98Seen[s].At = GetTickCount();
        s98Seen[s].Why = -1;
        r = mk_s98_switch(nodes[i].Id, &nhis[owner[m]], bdf[m], inf);
        if (r == MK_S98_NONE) {
            s98Seen[s].Attempts--;  /* nothing was attempted */
        }
        done = 1;
    }
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
    /* The scrub every poll; when it found a copy, or every
     * MK_INVENTORY_MS, the inventory too, since a device Windows found
     * meanwhile may have taken the name from it. */
    {
        int complete;
        int hits = mk_cddb_scrub(&complete);

        if (!complete && !scrubFailLogged) {
            mk_log("CriticalDeviceDatabase: the scrub did not complete; no "
                   "new mark is written until it does");
            scrubFailLogged = 1;
        } else if (complete && scrubFailLogged) {
            mk_log("CriticalDeviceDatabase: the scrub completes again");
            scrubFailLogged = 0;
        }
        scrubOk = complete;
        if (hits > 0) {
            lastInventory = GetTickCount() - MK_INVENTORY_MS;
        }
    }
    if (!inventoryDone || GetTickCount() - lastInventory >= MK_INVENTORY_MS) {
        mk_inventory(inventoryDone);
        mk_s98_prune();
        inventoryDone = 1;
        lastInventory = GetTickCount();
    }
    prot = mk_protect();
    curProt = prot;
    if (walkBad != 0) {
        /* An incomplete walk proves no parent and no absence. */
        mk_log("the device tree walk was incomplete (%d); poll skipped",
               walkBad);
        return mk_poll_ms(any);
    }
    mk_entries(prot);
    mk_reconcile_all();
    mk_s98_poll(prot);
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
