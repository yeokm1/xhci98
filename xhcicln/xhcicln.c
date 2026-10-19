/*
 * xhcicln.c - XHCICLN.EXE (32-bit) and XHCICL64.EXE (x64): list, and on
 * request remove, the devnodes an xhci98.sys before 2.4.0.0 left behind
 * under XHCI98\ (roadmap-hcd.md task 38.8; design record 13 section
 * 10.13.8).
 *
 * WHY IT EXISTS. Until 2.4.0.0 the bus answered its root hub, its hubs and
 * its composite parents under a private enumerator - XHCI98\ROOT_HUB,
 * XHCI98\HUB&VID_vvvv&PID_pppp, XHCI98\HUB30&... and XHCI98\COMPOSITE&... -
 * and from 2.4.0.0 it answers them under USB\ (USB\XHCI98_ROOT_HUB and the
 * rest), because a device id's first component names its enumerator and
 * there is no XHCI98 bus (GitHub issue 17 item 10). A device id decides a
 * devnode, so an update gives each of them a new devnode and the old one
 * stays behind, non-present: inert, hidden by Device Manager's default view,
 * costing nothing at run time. Nothing needs it removed. This tool is for a
 * user who wants it gone, and it is opt-in: nothing runs it, and an install
 * never needs it (owner, 2026-10-13).
 *
 * WHAT IT REMOVES, and only that. A devnode is a candidate when its instance
 * id begins XHCI98\ and its device id is one of the four old forms exactly
 * (cln_classify), and it is conclusively absent: every presence test has
 * three answers, present, absent and unknown, and only absent lets a removal
 * go ahead (cln_presence_nt, cln_presence_9x). It never touches a present or
 * unknown devnode, a current USB\XHCI98_ one (a hub unplugged today is not
 * stale), any device or function devnode (USB\VID_...), a refused storage
 * interface's devnode (task NOX.1), the controller's, or any file, INF,
 * driver-store package or service.
 *
 * THE GLOBAL REFUSALS (decision D10). It removes nothing - and lists what it
 * would have removed, saying why - when any XHCI98\ devnode is present (a
 * binary before 2.4.0.0 runs: before the update's restart, or after going
 * back), when no controller of this project is present, when one is not
 * started with a started USB\XHCI98_ROOT_HUB child, or when its door cannot
 * be opened and proved to be its own. Every such test answering unknown
 * refuses too. On NT only an xhci98.sys before 2.4.0.0 reports an XHCI98\
 * id, every devnode bound to the xhci98 service runs the one loaded image,
 * and another image loads only once every one of those devnodes has been
 * removed or stopped; the tool holds each controller's door open for its
 * whole run, so the query-remove a driver update, a rollback or a disable
 * sends first meets an open handle, and re-checks the controllers around
 * each removal.
 *
 * THE ROUTES. On NT (2000 to 7) the candidates come from SetupAPI with the
 * enumerator XHCI98 and are removed through SetupDiCallClassInstaller with
 * DIF_REMOVE, the route Device Manager's Uninstall takes, with no fallback.
 * On Windows 98 SE and ME this release LISTS ONLY (decision D8, as narrowed
 * by the design's review): those systems check no open handle at a
 * query-remove, so nothing excludes an older image loading during a run, and
 * CM_Uninstall_DevNode, the route that would remove an entry there, is a
 * stub on both, returning success and removing nothing (static, from each
 * system's CFGMGR32.DLL; section 10.13.8). Never a direct registry deletion.
 *
 * Removal on each NT version is enabled by cln_removal_enabled's table,
 * which the legs' reading of the controller's veto (a disable, an update and
 * a rollback refused while the tool holds the door) settled: a veto was read
 * on every NT target, so removal stays enabled on each.
 *
 * TARGETS AND BUILDS. ANSI C89, one file, built with the in-repo MSVC 6.0 as
 * XHCICLN.EXE, which starts on 98 SE, ME, 2000, XP and 32-bit Vista and 7,
 * and with WDK 7.1's amd64 compiler as XHCICL64.EXE (build.cmd). Every
 * SetupAPI and configuration-manager routine is looked up at run time, so the
 * import table names KERNEL32 and ADVAPI32 alone and the tool starts where a
 * routine is missing, saying which; a missing routine a removal needs refuses
 * that removal, never substitutes for it. A 32-bit process on 64-bit Windows
 * cannot run device installation actions (SetupDiCallClassInstaller fails
 * there with ERROR_IN_WOW64, Microsoft's SetupAPI documentation), so the
 * 32-bit build under WOW64 lists and refuses every removal, naming
 * XHCICL64.EXE. The executable carries a manifest asking for elevation
 * (xhcicln.manifest), which 98, ME, 2000 and XP ignore.
 *
 * THE PURE HALF. The id classification, the three-state presence decisions,
 * the service verdict, the door name and the exit code take their inputs as
 * arguments, so -selftest runs them over canned answers with no device and
 * no registry (build-driver.cmd runs it whenever the EXE has been built).
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <string.h>

#include <stdarg.h>
#include <stdlib.h>
#include "../src/xhci_version.h"
#include "../xhciinst/inst_core.h"

#define XHCICLN_VERSION XHCI_VER_STR

/* ---- the console (owner, 2026-10-18: the installer's rule) ---------------- */

/* Every line this tool prints is at most 79 columns (INST_COLUMNS), through
 * the installer's own streaming word wrap (xhciinst\inst_core.c, record 23
 * section 7.6; roadmap-usb4.md task 42-A.3), and every question lists its
 * choices one a line and ends "Enter for [x]". */
static INST_WRAP clnWrap;
static int cln_read_line(char *line, int cap);
static int clnWrapReady;

static void say(const char *fmt, ...)
{
    char buf[4096];
    char wrapped[8192];
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    if (!clnWrapReady) {
        inst_wrap_init(&clnWrap);
        clnWrapReady = 1;
    }
    inst_wrap_feed(&clnWrap, buf, wrapped, sizeof(wrapped));
    fputs(wrapped, stdout);
}

/* What the wrap holds back, printed: before input is read, and at the
 * end; the user's Enter then starts a new line. */
static void say_flush(void)
{
    char wrapped[INST_LINE_CHARS + 128];

    if (!clnWrapReady) {
        return;
    }
    inst_wrap_flush(&clnWrap, wrapped, sizeof(wrapped));
    fputs(wrapped, stdout);
    fflush(stdout);
}

/* ---- sizes ---------------------------------------------------------------- */

/* MAX_DEVICE_ID_LEN is 200; the buffers are wider so that an over-long name
 * is read whole and refused by length, never truncated into a match. */
#define CLN_ID_CHARS        512
#define CLN_TEXT_CHARS      256
#define CLN_MAX_ENTRIES     256
#define CLN_MAX_CTRLS       16

/* ---- the configuration manager's numbers (WDK 7.1 cfgmgr32.h and cfg.h,
 * the same in the Windows 2000 DDK's; static) ----------------------------- */

#define CLN_CR_SUCCESS              0x00000000UL
#define CLN_CR_NO_SUCH_DEVNODE      0x0000000DUL
#define CLN_CR_NO_SUCH_VALUE        0x00000025UL
#define CLN_CM_LOCATE_NORMAL        0x00000000UL
#define CLN_CM_LOCATE_PHANTOM       0x00000001UL
#define CLN_CM_DRP_SERVICE          0x00000005UL
#define CLN_DN_STARTED              0x00000008UL

/* The door's IOCTL_GET_HCD_DRIVERKEY_NAME (design record 13 section 8.10;
 * src/hcd_door.c HCD_IOCTL_HCD_DRIVERKEY): a ULONG of the answer's length in
 * bytes, then the driver key name in UTF-16, NUL-terminated. */
#define CLN_IOCTL_HCD_DRIVERKEY     0x00220424UL

/* CMP_WaitNoPendingInstallEvents' bound (section 10.13.8). */
#define CLN_INSTALL_WAIT_MS         30000UL

/* ---- verdicts ------------------------------------------------------------- */

/* cln_classify's answers. */
#define CLN_FORM_NOT_OURS       0   /* not under XHCI98\ nor a current id   */
#define CLN_FORM_OLD_ROOT_HUB   1   /* XHCI98\ROOT_HUB                      */
#define CLN_FORM_OLD_HUB        2   /* XHCI98\HUB&VID_vvvv&PID_pppp         */
#define CLN_FORM_OLD_HUB30      3   /* XHCI98\HUB30&VID_vvvv&PID_pppp       */
#define CLN_FORM_OLD_COMPOSITE  4   /* XHCI98\COMPOSITE&VID_vvvv&PID_pppp   */
#define CLN_FORM_OTHER_XHCI98   5   /* under XHCI98\, not one of the four   */
#define CLN_FORM_CURRENT        6   /* USB\XHCI98_ROOT_HUB and the rest     */

#define CLN_IS_OLD(f) ((f) >= CLN_FORM_OLD_ROOT_HUB && (f) <= CLN_FORM_OLD_COMPOSITE)

/* Presence: only CLN_ABSENT lets a removal go ahead. */
#define CLN_PRESENT             0
#define CLN_ABSENT              1
#define CLN_UNKNOWN             2

/* A presence cross-check set (NT) or devnode walk (9x): read whole, or not. */
#define CLN_SET_COMPLETE        0
#define CLN_SET_FAILED          1

/* cln_service_verdict's answers. */
#define CLN_SVC_OURS            0
#define CLN_SVC_NOT_OURS        1
#define CLN_SVC_UNKNOWN         2

/* Exit codes (decision D9). */
#define CLN_EXIT_DONE           0   /* nothing to do, all removed, or listed */
#define CLN_EXIT_REFUSED        1   /* refused before any removal            */
#define CLN_EXIT_STOPPED        2   /* stopped part-way                      */
#define CLN_EXIT_RESTART        3   /* all removed, a restart pending        */

/* ========================================================================== */
/* The pure half                                                               */
/* ========================================================================== */

static int cln_upper(int c)
{
    return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

/* Case-insensitive equality of the first n characters (ASCII only: the
 * setup engines compare ids so, and every id here is ASCII). */
static int cln_eqn(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (cln_upper((unsigned char)a[i]) != cln_upper((unsigned char)b[i])) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
    }
    return 1;
}

static int cln_eq(const char *a, const char *b)
{
    size_t n;

    n = strlen(b);
    return strlen(a) == n && cln_eqn(a, b, n);
}

static int cln_hex4(const char *s)
{
    int i;
    int c;

    for (i = 0; i < 4; i++) {
        c = cln_upper((unsigned char)s[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

/* "<base>&VID_hhhh&PID_hhhh", exactly, in the device id part `dev` of
 * length `len`. */
static int cln_vidpid_form(const char *dev, size_t len, const char *base)
{
    size_t b;

    b = strlen(base);
    if (len != b + 18 || !cln_eqn(dev, base, b)) {
        return 0;
    }
    return cln_eqn(dev + b, "&VID_", 5) && cln_hex4(dev + b + 5) &&
           cln_eqn(dev + b + 9, "&PID_", 5) && cln_hex4(dev + b + 14);
}

/*
 * What a device instance id is, by its device id - the part before its
 * second backslash. An instance id is <enumerator>\<device>\<instance>, so
 * it must have exactly two backslashes and three non-empty parts; one under
 * XHCI98\ that has not is "not this project's form" and left alone. The four
 * old forms are the ones a 2.0.0.0 to 2.3.x bus answered (design record 13
 * section 10.13.1), four upper-case hex digits each; case is not
 * significant, as the setup engines compare ids. Pure.
 */
static int cln_classify(const char *id)
{
    const char *first;
    const char *second;
    const char *dev;
    size_t devLen;

    if (id == NULL) {
        return CLN_FORM_NOT_OURS;
    }
    if (cln_eqn(id, "USB\\XHCI98_", 11)) {
        /* A current id: the four new bases (src/xhci_hub.h), with or without
         * the VID/PID qualifier. Anything else under USB\XHCI98_ - the
         * refused interface's USB\XHCI98_NOXPORT above all - is not ours to
         * name either way, and is NOT_OURS. */
        first = strchr(id, '\\');
        second = strchr(first + 1, '\\');
        dev = first + 1;
        devLen = (second != NULL) ? (size_t)(second - dev) : strlen(dev);
        if ((devLen == 15 && cln_eqn(dev, "XHCI98_ROOT_HUB", 15)) ||
            cln_vidpid_form(dev, devLen, "XHCI98_HUB") ||
            cln_vidpid_form(dev, devLen, "XHCI98_HUB30") ||
            cln_vidpid_form(dev, devLen, "XHCI98_COMPOSITE")) {
            return CLN_FORM_CURRENT;
        }
        return CLN_FORM_NOT_OURS;
    }
    if (!cln_eqn(id, "XHCI98\\", 7)) {
        return CLN_FORM_NOT_OURS;
    }
    dev = id + 7;
    second = strchr(dev, '\\');
    if (second == NULL || second == dev || second[1] == '\0' ||
        strchr(second + 1, '\\') != NULL) {
        return CLN_FORM_OTHER_XHCI98;
    }
    devLen = (size_t)(second - dev);
    if (devLen == 8 && cln_eqn(dev, "ROOT_HUB", 8)) {
        return CLN_FORM_OLD_ROOT_HUB;
    }
    if (cln_vidpid_form(dev, devLen, "HUB")) {
        return CLN_FORM_OLD_HUB;
    }
    if (cln_vidpid_form(dev, devLen, "HUB30")) {
        return CLN_FORM_OLD_HUB30;
    }
    if (cln_vidpid_form(dev, devLen, "COMPOSITE")) {
        return CLN_FORM_OLD_COMPOSITE;
    }
    return CLN_FORM_OTHER_XHCI98;
}

static const char *cln_form_text(int form)
{
    switch (form) {
    case CLN_FORM_OLD_ROOT_HUB:  return "the root hub's id before 2.4.0.0";
    case CLN_FORM_OLD_HUB:       return "a hub's id before 2.4.0.0";
    case CLN_FORM_OLD_HUB30:     return "a USB 3 hub's SuperSpeed id before 2.4.0.0";
    case CLN_FORM_OLD_COMPOSITE: return "a composite device's id before 2.4.0.0";
    case CLN_FORM_OTHER_XHCI98:  return "not this project's form";
    case CLN_FORM_CURRENT:       return "a current id";
    default:                     return "not this project's";
    }
}

/*
 * Presence on NT. `loaded` is whether CM_Locate_DevNode could be called at
 * all; `cr` its answer with CM_LOCATE_DEVNODE_NORMAL; `setState` whether the
 * present set (SetupDiGetClassDevs, the enumerator XHCI98 and DIGCF_PRESENT)
 * was built and walked whole; `inSet` whether the instance id was in it. The
 * two must agree: CR_NO_SUCH_DEVNODE and not in a whole set is absent,
 * CR_SUCCESS and in it is present, and anything else - another return, a
 * missing routine, a failed set, a disagreement - is unknown. Pure.
 */
static int cln_presence_nt(int loaded, unsigned long cr, int setState,
                           int inSet)
{
    if (!loaded || setState != CLN_SET_COMPLETE) {
        return CLN_UNKNOWN;
    }
    if (cr == CLN_CR_NO_SUCH_DEVNODE && !inSet) {
        return CLN_ABSENT;
    }
    if (cr == CLN_CR_SUCCESS && inSet) {
        return CLN_PRESENT;
    }
    return CLN_UNKNOWN;
}

/*
 * Presence on 98 SE and ME: the walk of HKEY_DYN_DATA\Config Manager\Enum
 * completed (every subkey enumerated to ERROR_NO_MORE_ITEMS, every
 * HardWareKey read whole) and no HardWareKey named the entry is absent; a
 * HardWareKey naming it is present; an incomplete walk is unknown for every
 * entry. Pure.
 */
static int cln_presence_9x(int walkState, int named)
{
    if (walkState != CLN_SET_COMPLETE) {
        return CLN_UNKNOWN;
    }
    return named ? CLN_PRESENT : CLN_ABSENT;
}

/*
 * Whether a present PCI devnode is a controller of this project, from its
 * service (CM_Get_DevNode_Registry_Property, CM_DRP_SERVICE): `loaded`
 * whether the routine was there, `cr` its answer, `type` the value's type,
 * `value` the text. CR_NO_SUCH_VALUE - no Service value, an unbound device -
 * is not ours; a REG_SZ naming xhci98 is ours, any other REG_SZ not ours;
 * any other failure, or a value that is not a string, is unknown. Pure.
 */
static int cln_service_verdict(int loaded, unsigned long cr,
                               unsigned long type, const char *value)
{
    if (!loaded) {
        return CLN_SVC_UNKNOWN;
    }
    if (cr == CLN_CR_NO_SUCH_VALUE) {
        return CLN_SVC_NOT_OURS;
    }
    if (cr != CLN_CR_SUCCESS || type != REG_SZ || value == NULL) {
        return CLN_SVC_UNKNOWN;
    }
    return cln_eq(value, "xhci98") ? CLN_SVC_OURS : CLN_SVC_NOT_OURS;
}

/*
 * The door's path from the controller's SymbolicName (written at every start
 * by HcdDoorControllerStart, src/hcd_door.c): \DosDevices\HCDn, or \??\HCDn,
 * rewritten to \\.\HCDn as the property pages do (design record 13 section
 * 8.2). The name after the prefix is HCD and one to three digits, nothing
 * else: the index is never guessed, only read. Returns 1 and fills `out`
 * (at least 16 bytes), or 0. Pure.
 */
static int cln_door_path(const char *symbolic, char *out, size_t outBytes)
{
    const char *name;
    size_t digits;

    if (symbolic == NULL || outBytes < 16) {
        return 0;
    }
    if (cln_eqn(symbolic, "\\DosDevices\\", 12)) {
        name = symbolic + 12;
    } else if (cln_eqn(symbolic, "\\??\\", 4)) {
        name = symbolic + 4;
    } else {
        return 0;
    }
    if (!cln_eqn(name, "HCD", 3)) {
        return 0;
    }
    digits = strlen(name + 3);
    if (digits < 1 || digits > 3 ||
        strspn(name + 3, "0123456789") != digits) {
        return 0;
    }
    sprintf(out, "\\\\.\\HCD%s", name + 3);
    return 1;
}

/*
 * The run's exit code (decision D9). `refused` a refusal before any removal
 * (a global refusal, an unknown, rights, WOW64, a missing routine, the
 * question cancelled); `stopped` a stop after the first removal began (a
 * removal failed, a re-check changed); `removed` how many went, `pending`
 * how many of those wait on a restart. Pure.
 */
static int cln_exit_code(int refused, int stopped, int removed, int pending)
{
    if (stopped) {
        return CLN_EXIT_STOPPED;
    }
    if (refused) {
        return CLN_EXIT_REFUSED;
    }
    (void)removed;
    return (pending > 0) ? CLN_EXIT_RESTART : CLN_EXIT_DONE;
}

/*
 * Which NT versions may remove (section 10.13.8, step 4): those where the
 * legs read the controller's query-remove vetoed while the tool holds its
 * door. Read 2026-10-14 on every NT target of this project, 2000 to 7 and
 * XP x64 among them, and none showed a missing veto, so every one is on; a
 * version where a veto went missing would be set to 0 here, and the tool
 * would then list only there. Any other NT version (Windows 8 and later,
 * which this project does not target) lists only. Pure.
 */
static int cln_removal_enabled(unsigned long major, unsigned long minor)
{
    static const struct {
        unsigned long Major;
        unsigned long Minor;
        int Enabled;
    } table[] = {
        { 5, 0, 1 },    /* Windows 2000                          */
        { 5, 1, 1 },    /* Windows XP                            */
        { 5, 2, 1 },    /* Windows XP x64, Server 2003           */
        { 6, 0, 1 },    /* Windows Vista                         */
        { 6, 1, 1 }     /* Windows 7                             */
    };
    int i;

    for (i = 0; i < (int)(sizeof(table) / sizeof(table[0])); i++) {
        if (table[i].Major == major && table[i].Minor == minor) {
            return table[i].Enabled;
        }
    }
    return 0;
}

/* ========================================================================== */
/* The run's state                                                              */
/* ========================================================================== */

typedef struct {
    char Id[CLN_ID_CHARS];
    char Desc[CLN_TEXT_CHARS];
    char Driver[CLN_TEXT_CHARS];
    int Form;
    int Presence;
    char Why[CLN_TEXT_CHARS];
    SP_DEVINFO_DATA Data;           /* NT: the element in clnSet          */
    int Done;                       /* 1 removed, 2 removed, restart pending */
} CLN_ENTRY;

typedef struct {
    char Id[CLN_ID_CHARS];
    DWORD DevInst;
    char Driver[CLN_TEXT_CHARS];
    char Door[32];
    HANDLE Handle;
} CLN_CTRL;

static CLN_ENTRY clnEntries[CLN_MAX_ENTRIES];
static int clnEntryCount;
static CLN_CTRL clnCtrls[CLN_MAX_CTRLS];
static int clnCtrlCount;

/* ---- SetupAPI and the configuration manager, looked up at run time ------ */

typedef HDEVINFO (WINAPI *PFN_GETCLASSDEVS)(const GUID *, PCSTR, HWND, DWORD);
typedef BOOL (WINAPI *PFN_ENUMINFO)(HDEVINFO, DWORD, PSP_DEVINFO_DATA);
typedef BOOL (WINAPI *PFN_GETINSTID)(HDEVINFO, PSP_DEVINFO_DATA, PSTR, DWORD,
                                     PDWORD);
typedef BOOL (WINAPI *PFN_GETREGPROP)(HDEVINFO, PSP_DEVINFO_DATA, DWORD,
                                      PDWORD, PBYTE, DWORD, PDWORD);
typedef HKEY (WINAPI *PFN_OPENDEVKEY)(HDEVINFO, PSP_DEVINFO_DATA, DWORD,
                                      DWORD, DWORD, REGSAM);
typedef BOOL (WINAPI *PFN_DESTROY)(HDEVINFO);
typedef BOOL (WINAPI *PFN_SETCIPARAMS)(HDEVINFO, PSP_DEVINFO_DATA,
                                       PSP_CLASSINSTALL_HEADER, DWORD);
typedef BOOL (WINAPI *PFN_CALLCI)(DI_FUNCTION, HDEVINFO, PSP_DEVINFO_DATA);
typedef BOOL (WINAPI *PFN_GETDIPARAMS)(HDEVINFO, PSP_DEVINFO_DATA,
                                       PSP_DEVINSTALL_PARAMS_A);
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
    PFN_GETCLASSDEVS GetClassDevs;
    PFN_ENUMINFO EnumInfo;
    PFN_GETINSTID GetInstanceId;
    PFN_GETREGPROP GetRegProp;
    PFN_OPENDEVKEY OpenDevKey;
    PFN_DESTROY Destroy;
    PFN_SETCIPARAMS SetCiParams;
    PFN_CALLCI CallCi;
    PFN_GETDIPARAMS GetDiParams;
    PFN_CM_LOCATE Locate;
    PFN_CM_STATUS Status;
    PFN_CM_REL Child;
    PFN_CM_REL Sibling;
    PFN_CM_ID DeviceId;
    PFN_CM_PROP Property;
    PFN_CM_WAIT WaitInstall;
} api;

static FARPROC cln_proc(HMODULE module, const char *name, int *missing)
{
    FARPROC p;

    p = (module != NULL) ? GetProcAddress(module, name) : NULL;
    if (p == NULL) {
        say("  not available: %s\n", name);
        (*missing)++;
    }
    return p;
}

/* The routines the listing needs on NT; returns how many are missing (each
 * is printed). The removal-only ones are looked up too, and their absence is
 * counted apart in *removalMissing. */
static int cln_load_nt(int *removalMissing)
{
    int missing;
    int rm;

    missing = 0;
    rm = 0;
    api.SetupApi = LoadLibraryA("setupapi.dll");
    api.CfgMgr = LoadLibraryA("cfgmgr32.dll");
    api.GetClassDevs = (PFN_GETCLASSDEVS)cln_proc(api.SetupApi,
                           "SetupDiGetClassDevsA", &missing);
    api.EnumInfo = (PFN_ENUMINFO)cln_proc(api.SetupApi,
                       "SetupDiEnumDeviceInfo", &missing);
    api.GetInstanceId = (PFN_GETINSTID)cln_proc(api.SetupApi,
                            "SetupDiGetDeviceInstanceIdA", &missing);
    api.GetRegProp = (PFN_GETREGPROP)cln_proc(api.SetupApi,
                         "SetupDiGetDeviceRegistryPropertyA", &missing);
    api.OpenDevKey = (PFN_OPENDEVKEY)cln_proc(api.SetupApi,
                         "SetupDiOpenDevRegKey", &missing);
    api.Destroy = (PFN_DESTROY)cln_proc(api.SetupApi,
                      "SetupDiDestroyDeviceInfoList", &missing);
    api.Locate = (PFN_CM_LOCATE)cln_proc(api.CfgMgr, "CM_Locate_DevNodeA",
                                         &missing);
    api.Status = (PFN_CM_STATUS)cln_proc(api.CfgMgr, "CM_Get_DevNode_Status",
                                         &missing);
    api.Child = (PFN_CM_REL)cln_proc(api.CfgMgr, "CM_Get_Child", &missing);
    api.Sibling = (PFN_CM_REL)cln_proc(api.CfgMgr, "CM_Get_Sibling",
                                       &missing);
    api.DeviceId = (PFN_CM_ID)cln_proc(api.CfgMgr, "CM_Get_Device_IDA",
                                       &missing);
    api.Property = (PFN_CM_PROP)cln_proc(api.CfgMgr,
                       "CM_Get_DevNode_Registry_PropertyA", &missing);
    api.SetCiParams = (PFN_SETCIPARAMS)cln_proc(api.SetupApi,
                          "SetupDiSetClassInstallParamsA", &rm);
    api.CallCi = (PFN_CALLCI)cln_proc(api.SetupApi,
                     "SetupDiCallClassInstaller", &rm);
    api.GetDiParams = (PFN_GETDIPARAMS)cln_proc(api.SetupApi,
                          "SetupDiGetDeviceInstallParamsA", &rm);
    api.WaitInstall = (PFN_CM_WAIT)cln_proc(api.CfgMgr,
                          "CMP_WaitNoPendingInstallEvents", &rm);
    *removalMissing = rm;
    return missing;
}

/* ---- small helpers ---------------------------------------------------------- */

static void cln_copy(char *dst, const char *src, size_t dstBytes)
{
    strncpy(dst, src, dstBytes - 1);
    dst[dstBytes - 1] = '\0';
}

/* A REG_SZ property of a set element, or "" with `why` set. */
static void cln_reg_prop(HDEVINFO set, PSP_DEVINFO_DATA data, DWORD prop,
                         char *out, size_t outBytes)
{
    DWORD type;
    DWORD need;

    out[0] = '\0';
    type = 0;
    need = 0;
    if (!api.GetRegProp(set, data, prop, &type, (PBYTE)out,
                        (DWORD)(outBytes - 1), &need) || type != REG_SZ) {
        out[0] = '\0';
        return;
    }
    out[outBytes - 1] = '\0';
}

/* The device id part of an instance id: up to its second backslash. */
static void cln_device_part(const char *id, char *out, size_t outBytes)
{
    const char *first;
    const char *second;
    size_t n;

    first = strchr(id, '\\');
    second = (first != NULL) ? strchr(first + 1, '\\') : NULL;
    n = (second != NULL) ? (size_t)(second - id) : strlen(id);
    if (n >= outBytes) {
        n = outBytes - 1;
    }
    memcpy(out, id, n);
    out[n] = '\0';
}

/* ========================================================================== */
/* NT: the candidates and their presence                                      */
/* ========================================================================== */

/* The XHCI98 enumerator key absent altogether: no devnode under it, ever.
 * Returns 1 when it is conclusively absent, 0 when it exists or cannot be
 * read (the caller then treats a failed set as a failure). */
static int cln_nt_enumerator_absent(void)
{
    HKEY key;
    LONG rc;

    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                       "SYSTEM\\CurrentControlSet\\Enum\\XHCI98", 0,
                       KEY_READ, &key);
    if (rc == ERROR_SUCCESS) {
        RegCloseKey(key);
        return 0;
    }
    return rc == ERROR_FILE_NOT_FOUND;
}

/*
 * Every instance id of a set walked whole into `ids` (up to `max`): returns
 * CLN_SET_COMPLETE, or CLN_SET_FAILED with `why` set. A set that cannot be
 * built because its enumerator has never existed is complete and empty.
 */
static int cln_nt_walk(const char *enumerator, DWORD flags, HDEVINFO *keep,
                       char (*ids)[CLN_ID_CHARS], SP_DEVINFO_DATA *datas,
                       int max, int *count, char *why, size_t whyBytes)
{
    HDEVINFO set;
    SP_DEVINFO_DATA data;
    DWORD index;
    DWORD err;
    DWORD need;
    char id[CLN_ID_CHARS];

    *count = 0;
    if (keep != NULL) {
        *keep = INVALID_HANDLE_VALUE;
    }
    set = api.GetClassDevs(NULL, enumerator, NULL, flags);
    if (set == INVALID_HANDLE_VALUE) {
        err = GetLastError();
        if (strcmp(enumerator, "XHCI98") == 0 && cln_nt_enumerator_absent()) {
            return CLN_SET_COMPLETE;
        }
        _snprintf(why, whyBytes - 1, "the %s device set could not be built "
                  "(error %lu)", enumerator, (unsigned long)err);
        why[whyBytes - 1] = '\0';
        return CLN_SET_FAILED;
    }
    for (index = 0; ; index++) {
        memset(&data, 0, sizeof(data));
        data.cbSize = sizeof(data);
        if (!api.EnumInfo(set, index, &data)) {
            err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) {
                break;
            }
            _snprintf(why, whyBytes - 1, "the %s device set's walk failed at "
                      "%lu (error %lu)", enumerator, (unsigned long)index,
                      (unsigned long)err);
            why[whyBytes - 1] = '\0';
            api.Destroy(set);
            return CLN_SET_FAILED;
        }
        need = 0;
        if (!api.GetInstanceId(set, &data, id, (DWORD)sizeof(id), &need)) {
            _snprintf(why, whyBytes - 1, "an instance id of the %s device set "
                      "could not be read (error %lu)", enumerator,
                      (unsigned long)GetLastError());
            why[whyBytes - 1] = '\0';
            api.Destroy(set);
            return CLN_SET_FAILED;
        }
        if (*count >= max) {
            _snprintf(why, whyBytes - 1, "the %s device set holds more than "
                      "%d devices", enumerator, max);
            why[whyBytes - 1] = '\0';
            api.Destroy(set);
            return CLN_SET_FAILED;
        }
        cln_copy(ids[*count], id, CLN_ID_CHARS);
        if (datas != NULL) {
            datas[*count] = data;
        }
        (*count)++;
    }
    if (keep != NULL) {
        *keep = set;
    } else {
        api.Destroy(set);
    }
    return CLN_SET_COMPLETE;
}

static char clnPresentIds[CLN_MAX_ENTRIES][CLN_ID_CHARS];
static int clnPresentCount;
static int clnPresentState;
static char clnPresentWhy[CLN_TEXT_CHARS];

static void cln_nt_present_set(void)
{
    clnPresentState = cln_nt_walk("XHCI98", DIGCF_ALLCLASSES | DIGCF_PRESENT,
                                  NULL, clnPresentIds, NULL, CLN_MAX_ENTRIES,
                                  &clnPresentCount, clnPresentWhy,
                                  sizeof(clnPresentWhy));
}

static int cln_nt_in_present_set(const char *id)
{
    int i;

    for (i = 0; i < clnPresentCount; i++) {
        if (cln_eq(clnPresentIds[i], id)) {
            return 1;
        }
    }
    return 0;
}

/* One entry's presence, from CM_Locate_DevNode and the present set (which the
 * caller has just taken). */
static int cln_nt_presence(CLN_ENTRY *e)
{
    DWORD dn;
    DWORD cr;
    char id[CLN_ID_CHARS];
    int p;

    cln_copy(id, e->Id, sizeof(id));
    dn = 0;
    cr = api.Locate(&dn, id, CLN_CM_LOCATE_NORMAL);
    p = cln_presence_nt(1, cr, clnPresentState,
                        cln_nt_in_present_set(e->Id));
    e->Why[0] = '\0';
    if (p == CLN_UNKNOWN) {
        if (clnPresentState != CLN_SET_COMPLETE) {
            _snprintf(e->Why, sizeof(e->Why) - 1, "presence unknown: %s",
                      clnPresentWhy);
        } else {
            _snprintf(e->Why, sizeof(e->Why) - 1, "presence unknown: "
                      "CM_Locate_DevNode answered 0x%02lX and the present set "
                      "%s it", (unsigned long)cr,
                      cln_nt_in_present_set(e->Id) ? "holds" : "does not hold");
        }
        e->Why[sizeof(e->Why) - 1] = '\0';
    }
    return p;
}

static HDEVINFO clnSet = INVALID_HANDLE_VALUE;

/* The candidates: every devnode under the enumerator XHCI98, present or not.
 * Returns 0, or -1 with the reason printed. */
static int cln_nt_candidates(void)
{
    static char ids[CLN_MAX_ENTRIES][CLN_ID_CHARS];
    static SP_DEVINFO_DATA datas[CLN_MAX_ENTRIES];
    char why[CLN_TEXT_CHARS];
    int n;
    int i;
    CLN_ENTRY *e;

    if (cln_nt_walk("XHCI98", DIGCF_ALLCLASSES, &clnSet, ids, datas,
                    CLN_MAX_ENTRIES, &n, why, sizeof(why)) !=
        CLN_SET_COMPLETE) {
        say("xhcicln: %s.\n", why);
        return -1;
    }
    cln_nt_present_set();
    clnEntryCount = 0;
    for (i = 0; i < n; i++) {
        e = &clnEntries[clnEntryCount++];
        memset(e, 0, sizeof(*e));
        cln_copy(e->Id, ids[i], sizeof(e->Id));
        e->Data = datas[i];
        e->Form = cln_classify(e->Id);
        cln_reg_prop(clnSet, &e->Data, SPDRP_DEVICEDESC, e->Desc,
                     sizeof(e->Desc));
        cln_reg_prop(clnSet, &e->Data, SPDRP_DRIVER, e->Driver,
                     sizeof(e->Driver));
        e->Presence = cln_nt_presence(e);
    }
    return 0;
}

/* ========================================================================== */
/* NT: the controllers, held for the run                                       */
/* ========================================================================== */

static char clnRefusal[512];

static void cln_refuse(const char *text)
{
    if (clnRefusal[0] == '\0') {
        cln_copy(clnRefusal, text, sizeof(clnRefusal));
    }
}

/* The driver key a door answers (IOCTL_GET_HCD_DRIVERKEY_NAME), as ANSI.
 * Returns 1 and fills `out`, or 0. */
static int cln_door_driverkey(HANDLE h, char *out, size_t outBytes)
{
    unsigned char buf[600];
    DWORD got;
    unsigned long actual;
    int chars;
    int i;
    unsigned int c;

    memset(buf, 0, sizeof(buf));
    got = 0;
    if (!DeviceIoControl(h, CLN_IOCTL_HCD_DRIVERKEY, buf, (DWORD)sizeof(buf),
                         buf, (DWORD)sizeof(buf), &got, NULL)) {
        return 0;
    }
    actual = (unsigned long)buf[0] | ((unsigned long)buf[1] << 8) |
             ((unsigned long)buf[2] << 16) | ((unsigned long)buf[3] << 24);
    if (got < 6 || actual > got || actual < 6 || (actual & 1UL) != 0) {
        return 0;
    }
    chars = (int)((actual - 4) / 2);
    if ((size_t)chars > outBytes) {
        return 0;
    }
    for (i = 0; i < chars; i++) {
        c = (unsigned int)buf[4 + 2 * i] | ((unsigned int)buf[5 + 2 * i] << 8);
        if (c == 0) {
            break;
        }
        if (c > 0x7E || c < 0x20) {
            return 0;
        }
        out[i] = (char)c;
    }
    if (i >= chars) {
        return 0;
    }
    out[i] = '\0';
    return i > 0;
}

/*
 * Step 1 and 2 of the controller set (section 10.13.8): every present PCI
 * devnode whose service is xhci98, each started, each with a started
 * USB\XHCI98_ROOT_HUB child. `ids`/`count` receive the set's instance ids;
 * `devinsts` their devnodes; the SetupAPI set is returned in *setOut for the
 * door step (the caller destroys it). Returns 1, or 0 with the reason given
 * to cln_refuse.
 */
static int cln_nt_controller_set(char (*ids)[CLN_ID_CHARS], DWORD *devinsts,
                                 SP_DEVINFO_DATA *datas, int *count,
                                 HDEVINFO *setOut)
{
    static char all[CLN_MAX_ENTRIES][CLN_ID_CHARS];
    static SP_DEVINFO_DATA allData[CLN_MAX_ENTRIES];
    char why[CLN_TEXT_CHARS + 128];
    char svc[64];
    char childId[CLN_ID_CHARS];
    char childDev[CLN_ID_CHARS];
    HDEVINFO set;
    ULONG type;
    ULONG len;
    ULONG st;
    ULONG prob;
    DWORD cr;
    DWORD child;
    DWORD next;
    int n;
    int i;
    int verdict;
    int rootHubs;

    *count = 0;
    *setOut = INVALID_HANDLE_VALUE;
    if (cln_nt_walk("PCI", DIGCF_ALLCLASSES | DIGCF_PRESENT, &set, all,
                    allData, CLN_MAX_ENTRIES, &n, why, sizeof(why)) !=
        CLN_SET_COMPLETE) {
        cln_refuse(why);
        return 0;
    }
    for (i = 0; i < n; i++) {
        memset(svc, 0, sizeof(svc));
        type = 0;
        len = (ULONG)(sizeof(svc) - 1);
        cr = api.Property(allData[i].DevInst, CLN_CM_DRP_SERVICE, &type, svc,
                          &len, 0);
        svc[sizeof(svc) - 1] = '\0';
        verdict = cln_service_verdict(1, cr, type, svc);
        if (verdict == CLN_SVC_NOT_OURS) {
            continue;
        }
        if (verdict == CLN_SVC_UNKNOWN) {
            _snprintf(why, sizeof(why) - 1, "the service of %s could not be "
                      "read (0x%02lX)", all[i], (unsigned long)cr);
            why[sizeof(why) - 1] = '\0';
            cln_refuse(why);
            api.Destroy(set);
            return 0;
        }
        if (*count >= CLN_MAX_CTRLS) {
            cln_refuse("more project controllers than this tool holds");
            api.Destroy(set);
            return 0;
        }
        cln_copy(ids[*count], all[i], CLN_ID_CHARS);
        devinsts[*count] = allData[i].DevInst;
        datas[*count] = allData[i];
        (*count)++;
    }
    if (*count == 0) {
        cln_refuse("no controller of this project is present and running "
                   "(nothing proves which xhci98.sys is loaded)");
        api.Destroy(set);
        return 0;
    }
    for (i = 0; i < *count; i++) {
        st = 0;
        prob = 0;
        cr = api.Status(&st, &prob, devinsts[i], 0);
        if (cr != CLN_CR_SUCCESS || (st & CLN_DN_STARTED) == 0) {
            _snprintf(why, sizeof(why) - 1, "the controller %s is not started "
                      "(status 0x%08lX, problem %lu, 0x%02lX)", ids[i],
                      (unsigned long)st, (unsigned long)prob,
                      (unsigned long)cr);
            why[sizeof(why) - 1] = '\0';
            cln_refuse(why);
            api.Destroy(set);
            return 0;
        }
        rootHubs = 0;
        child = 0;
        cr = api.Child(&child, devinsts[i], 0);
        while (cr == CLN_CR_SUCCESS) {
            memset(childId, 0, sizeof(childId));
            if (api.DeviceId(child, childId, (ULONG)(sizeof(childId) - 1), 0) ==
                CLN_CR_SUCCESS) {
                cln_device_part(childId, childDev, sizeof(childDev));
                if (cln_eq(childDev, "USB\\XHCI98_ROOT_HUB")) {
                    st = 0;
                    prob = 0;
                    if (api.Status(&st, &prob, child, 0) == CLN_CR_SUCCESS &&
                        (st & CLN_DN_STARTED) != 0) {
                        rootHubs++;
                    }
                }
            }
            next = 0;
            cr = api.Sibling(&next, child, 0);
            child = next;
        }
        if (rootHubs == 0) {
            _snprintf(why, sizeof(why) - 1, "the controller %s has no started "
                      "USB\\XHCI98_ROOT_HUB (a present root hub whose driver "
                      "did not start proves nothing about which xhci98.sys "
                      "runs)", ids[i]);
            why[sizeof(why) - 1] = '\0';
            cln_refuse(why);
            api.Destroy(set);
            return 0;
        }
    }
    *setOut = set;
    return 1;
}

/*
 * Steps 1 to 4: find every project controller, check each, open each one's
 * door from its SymbolicName and prove the handle is that controller's by
 * the driver key it answers. The handles stay open until the tool exits.
 * Returns 1, or 0 with the reason given to cln_refuse.
 */
static int cln_nt_hold_controllers(void)
{
    static char ids[CLN_MAX_CTRLS][CLN_ID_CHARS];
    DWORD devinsts[CLN_MAX_CTRLS];
    SP_DEVINFO_DATA datas[CLN_MAX_CTRLS];
    HDEVINFO set;
    HKEY key;
    DWORD type;
    DWORD bytes;
    char symbolic[128];
    char answered[CLN_TEXT_CHARS];
    char why[CLN_TEXT_CHARS + 128];
    int count;
    int i;
    CLN_CTRL *c;

    if (!cln_nt_controller_set(ids, devinsts, datas, &count, &set)) {
        return 0;
    }
    clnCtrlCount = 0;
    for (i = 0; i < count; i++) {
        c = &clnCtrls[clnCtrlCount];
        memset(c, 0, sizeof(*c));
        c->Handle = INVALID_HANDLE_VALUE;
        cln_copy(c->Id, ids[i], sizeof(c->Id));
        c->DevInst = devinsts[i];
        cln_reg_prop(set, &datas[i], SPDRP_DRIVER, c->Driver,
                     sizeof(c->Driver));
        clnCtrlCount++;
        if (c->Driver[0] == '\0') {
            _snprintf(why, sizeof(why) - 1, "the controller %s's driver key "
                      "could not be read", c->Id);
            why[sizeof(why) - 1] = '\0';
            cln_refuse(why);
            api.Destroy(set);
            return 0;
        }
        key = api.OpenDevKey(set, &datas[i], DICS_FLAG_GLOBAL, 0, DIREG_DEV,
                             KEY_READ);
        memset(symbolic, 0, sizeof(symbolic));
        type = 0;
        bytes = (DWORD)(sizeof(symbolic) - 1);
        if (key == INVALID_HANDLE_VALUE ||
            RegQueryValueExA(key, "SymbolicName", NULL, &type,
                             (LPBYTE)symbolic, &bytes) != ERROR_SUCCESS ||
            type != REG_SZ ||
            !cln_door_path(symbolic, c->Door, sizeof(c->Door))) {
            if (key != INVALID_HANDLE_VALUE) {
                RegCloseKey(key);
            }
            _snprintf(why, sizeof(why) - 1, "the controller %s has no usable "
                      "SymbolicName ('%s'), so its door cannot be found",
                      c->Id, symbolic);
            why[sizeof(why) - 1] = '\0';
            cln_refuse(why);
            api.Destroy(set);
            return 0;
        }
        RegCloseKey(key);
        c->Handle = CreateFileA(c->Door, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, 0, NULL);
        if (c->Handle == INVALID_HANDLE_VALUE) {
            _snprintf(why, sizeof(why) - 1, "the door %s of %s could not be "
                      "opened (error %lu)", c->Door, c->Id,
                      (unsigned long)GetLastError());
            why[sizeof(why) - 1] = '\0';
            cln_refuse(why);
            api.Destroy(set);
            return 0;
        }
        if (!cln_door_driverkey(c->Handle, answered, sizeof(answered)) ||
            !cln_eq(answered, c->Driver)) {
            _snprintf(why, sizeof(why) - 1, "the door %s does not answer as "
                      "%s's (driver key %s)", c->Door, c->Id, c->Driver);
            why[sizeof(why) - 1] = '\0';
            cln_refuse(why);
            api.Destroy(set);
            return 0;
        }
    }
    api.Destroy(set);
    return 1;
}

/* Step 5: the same controllers, by instance id, at least one, all started
 * with their root hubs, each still held by a handle that answers its own
 * driver key. Returns 1, or 0 with `why` set. */
static int cln_nt_recheck(char *why, size_t whyBytes)
{
    static char ids[CLN_MAX_CTRLS][CLN_ID_CHARS];
    DWORD devinsts[CLN_MAX_CTRLS];
    SP_DEVINFO_DATA datas[CLN_MAX_CTRLS];
    char answered[CLN_TEXT_CHARS];
    HDEVINFO set;
    int count;
    int i;
    int j;
    int found;

    clnRefusal[0] = '\0';
    if (!cln_nt_controller_set(ids, devinsts, datas, &count, &set)) {
        _snprintf(why, whyBytes - 1, "%s", clnRefusal);
        why[whyBytes - 1] = '\0';
        return 0;
    }
    api.Destroy(set);
    if (count != clnCtrlCount) {
        _snprintf(why, whyBytes - 1, "the project's controllers changed "
                  "(%d held, %d now)", clnCtrlCount, count);
        why[whyBytes - 1] = '\0';
        return 0;
    }
    for (i = 0; i < clnCtrlCount; i++) {
        found = 0;
        for (j = 0; j < count; j++) {
            if (cln_eq(ids[j], clnCtrls[i].Id)) {
                found = 1;
            }
        }
        if (!found) {
            _snprintf(why, whyBytes - 1, "the controller %s is no longer "
                      "present", clnCtrls[i].Id);
            why[whyBytes - 1] = '\0';
            return 0;
        }
        if (!cln_door_driverkey(clnCtrls[i].Handle, answered,
                                sizeof(answered)) ||
            !cln_eq(answered, clnCtrls[i].Driver)) {
            _snprintf(why, whyBytes - 1, "the door %s no longer answers as "
                      "%s's", clnCtrls[i].Door, clnCtrls[i].Id);
            why[whyBytes - 1] = '\0';
            return 0;
        }
    }
    cln_nt_present_set();
    if (clnPresentState != CLN_SET_COMPLETE) {
        _snprintf(why, whyBytes - 1, "%s", clnPresentWhy);
        why[whyBytes - 1] = '\0';
        return 0;
    }
    if (clnPresentCount > 0) {
        _snprintf(why, whyBytes - 1, "%s is present now (an xhci98.sys before "
                  "2.4.0.0 is running)", clnPresentIds[0]);
        why[whyBytes - 1] = '\0';
        return 0;
    }
    /* Every XHCI98\ entry, of any form and removed or not, conclusively
     * absent: the global rule is about any such devnode, not only the
     * candidates (one removed already answers absent). */
    for (j = 0; j < clnEntryCount; j++) {
        if (cln_nt_presence(&clnEntries[j]) != CLN_ABSENT) {
            _snprintf(why, whyBytes - 1, "%s is not conclusively absent now "
                      "(%s)", clnEntries[j].Id, clnEntries[j].Why[0] ?
                      clnEntries[j].Why : "present");
            why[whyBytes - 1] = '\0';
            return 0;
        }
    }
    return 1;
}

/* ========================================================================== */
/* 98 SE and ME: the entries, listed only                                       */
/* ========================================================================== */

static int clnWalkState;
static char clnWalkWhy[CLN_TEXT_CHARS];
static int clnWalkNoKey;

/* A REG_SZ value of a key, whole, or "" (and 0). */
static int cln_reg_text(HKEY root, const char *path, const char *name,
                        char *out, DWORD outBytes)
{
    HKEY key;
    DWORD type;
    DWORD bytes;
    LONG rc;

    out[0] = '\0';
    if (RegOpenKeyExA(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return 0;
    }
    type = 0;
    bytes = outBytes - 1;
    rc = RegQueryValueExA(key, name, NULL, &type, (LPBYTE)out, &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ) {
        out[0] = '\0';
        return 0;
    }
    out[outBytes - 1] = '\0';
    return 1;
}

/*
 * Whether HKEY_DYN_DATA\Config Manager\Enum names `id` in any HardWareKey,
 * walking it whole each time: sets clnWalkState to CLN_SET_FAILED (and
 * clnWalkWhy) when any subkey or value cannot be read whole. A subkey with no
 * HardWareKey value names nothing and is counted in clnWalkNoKey. Unlike
 * XHCISNAP's walk, which is a report, nothing here is skipped or capped.
 */
static int cln_9x_named(const char *id)
{
    HKEY root;
    HKEY sub;
    DWORD subCount;
    DWORD maxSubLen;
    DWORD i;
    DWORD nameLen;
    DWORD type;
    DWORD bytes;
    LONG rc;
    char name[64];
    char hw[CLN_ID_CHARS];
    int named;

    clnWalkState = CLN_SET_COMPLETE;
    clnWalkNoKey = 0;
    named = 0;
    rc = RegOpenKeyExA(HKEY_DYN_DATA, "Config Manager\\Enum", 0, KEY_READ,
                       &root);
    if (rc != ERROR_SUCCESS) {
        clnWalkState = CLN_SET_FAILED;
        _snprintf(clnWalkWhy, sizeof(clnWalkWhy) - 1, "HKEY_DYN_DATA\\Config "
                  "Manager\\Enum could not be opened (error %ld)", (long)rc);
        return 0;
    }
    if (RegQueryInfoKeyA(root, NULL, NULL, NULL, &subCount, &maxSubLen, NULL,
                         NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS ||
        maxSubLen + 1 > sizeof(name)) {
        clnWalkState = CLN_SET_FAILED;
        _snprintf(clnWalkWhy, sizeof(clnWalkWhy) - 1, "HKEY_DYN_DATA\\Config "
                  "Manager\\Enum could not be sized");
        RegCloseKey(root);
        return 0;
    }
    for (i = 0; ; i++) {
        nameLen = (DWORD)sizeof(name);
        rc = RegEnumKeyExA(root, i, name, &nameLen, NULL, NULL, NULL, NULL);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS) {
            clnWalkState = CLN_SET_FAILED;
            _snprintf(clnWalkWhy, sizeof(clnWalkWhy) - 1, "subkey %lu of "
                      "Config Manager\\Enum could not be read (error %ld)",
                      (unsigned long)i, (long)rc);
            break;
        }
        if (RegOpenKeyExA(root, name, 0, KEY_READ, &sub) != ERROR_SUCCESS) {
            clnWalkState = CLN_SET_FAILED;
            _snprintf(clnWalkWhy, sizeof(clnWalkWhy) - 1, "Config Manager\\"
                      "Enum\\%s could not be opened", name);
            break;
        }
        type = 0;
        bytes = (DWORD)sizeof(hw);
        memset(hw, 0, sizeof(hw));
        rc = RegQueryValueExA(sub, "HardWareKey", NULL, &type, (LPBYTE)hw,
                              &bytes);
        RegCloseKey(sub);
        if (rc == ERROR_FILE_NOT_FOUND) {
            clnWalkNoKey++;
            continue;
        }
        if (rc != ERROR_SUCCESS || type != REG_SZ || bytes == 0 ||
            hw[bytes - 1] != '\0') {
            clnWalkState = CLN_SET_FAILED;
            _snprintf(clnWalkWhy, sizeof(clnWalkWhy) - 1, "the HardWareKey of "
                      "Config Manager\\Enum\\%s could not be read whole", name);
            break;
        }
        if (id != NULL && cln_eq(hw, id)) {
            named = 1;
        }
        if (id == NULL && cln_eqn(hw, "XHCI98\\", 7)) {
            named = 1;
        }
    }
    clnWalkWhy[sizeof(clnWalkWhy) - 1] = '\0';
    RegCloseKey(root);
    (void)subCount;
    return named;
}

/* The entries under HKLM\Enum\XHCI98, two levels, each key name read with a
 * buffer RegQueryInfoKey sized. Returns 0, or -1 with the reason printed. */
static int cln_9x_candidates(void)
{
    HKEY top;
    HKEY dev;
    DWORD maxLen;
    DWORD maxLen2;
    DWORD i;
    DWORD j;
    DWORD len;
    LONG rc;
    char devName[CLN_ID_CHARS];
    char inst[CLN_ID_CHARS];
    char path[CLN_ID_CHARS * 2];
    CLN_ENTRY *e;
    int named;

    clnEntryCount = 0;
    rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum\\XHCI98", 0, KEY_READ, &top);
    if (rc == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (rc != ERROR_SUCCESS) {
        say("xhcicln: HKLM\\Enum\\XHCI98 could not be opened (error %ld).\n",
               (long)rc);
        return -1;
    }
    if (RegQueryInfoKeyA(top, NULL, NULL, NULL, NULL, &maxLen, NULL, NULL,
                         NULL, NULL, NULL, NULL) != ERROR_SUCCESS ||
        maxLen + 1 > sizeof(devName)) {
        say("xhcicln: HKLM\\Enum\\XHCI98 could not be sized.\n");
        RegCloseKey(top);
        return -1;
    }
    for (i = 0; ; i++) {
        len = (DWORD)sizeof(devName);
        rc = RegEnumKeyExA(top, i, devName, &len, NULL, NULL, NULL, NULL);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc != ERROR_SUCCESS) {
            say("xhcicln: an entry under HKLM\\Enum\\XHCI98 could not be "
                   "read whole (error %ld); listed as unreadable.\n", (long)rc);
            continue;
        }
        if (RegOpenKeyExA(top, devName, 0, KEY_READ, &dev) != ERROR_SUCCESS) {
            say("xhcicln: HKLM\\Enum\\XHCI98\\%s could not be opened; "
                   "listed as unreadable.\n", devName);
            continue;
        }
        if (RegQueryInfoKeyA(dev, NULL, NULL, NULL, NULL, &maxLen2, NULL, NULL,
                             NULL, NULL, NULL, NULL) != ERROR_SUCCESS ||
            maxLen2 + 1 > sizeof(inst)) {
            say("xhcicln: HKLM\\Enum\\XHCI98\\%s could not be read whole; "
                   "listed as unreadable.\n", devName);
            RegCloseKey(dev);
            continue;
        }
        for (j = 0; ; j++) {
            len = (DWORD)sizeof(inst);
            rc = RegEnumKeyExA(dev, j, inst, &len, NULL, NULL, NULL, NULL);
            if (rc == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (rc != ERROR_SUCCESS) {
                say("xhcicln: an entry under HKLM\\Enum\\XHCI98\\%s could "
                       "not be read whole; listed as unreadable.\n", devName);
                continue;
            }
            if (clnEntryCount >= CLN_MAX_ENTRIES) {
                say("xhcicln: more than %d entries; the rest are not "
                       "listed.\n", CLN_MAX_ENTRIES);
                break;
            }
            e = &clnEntries[clnEntryCount++];
            memset(e, 0, sizeof(*e));
            _snprintf(e->Id, sizeof(e->Id) - 1, "XHCI98\\%s\\%s", devName,
                      inst);
            e->Form = cln_classify(e->Id);
            _snprintf(path, sizeof(path) - 1, "Enum\\XHCI98\\%s\\%s", devName,
                      inst);
            path[sizeof(path) - 1] = '\0';
            cln_reg_text(HKEY_LOCAL_MACHINE, path, "DeviceDesc", e->Desc,
                         (DWORD)sizeof(e->Desc));
            cln_reg_text(HKEY_LOCAL_MACHINE, path, "Driver", e->Driver,
                         (DWORD)sizeof(e->Driver));
            named = cln_9x_named(e->Id);
            e->Presence = cln_presence_9x(clnWalkState, named);
            if (e->Presence == CLN_UNKNOWN) {
                _snprintf(e->Why, sizeof(e->Why) - 1, "presence unknown: %s",
                          clnWalkWhy);
            }
        }
        RegCloseKey(dev);
    }
    RegCloseKey(top);
    return 0;
}

/* ========================================================================== */
/* The listing                                                                  */
/* ========================================================================== */

static const char *cln_presence_text(int p)
{
    switch (p) {
    case CLN_PRESENT: return "present";
    case CLN_ABSENT:  return "not present";
    default:          return "presence unknown";
    }
}

/* Prints every entry under XHCI98\, candidates first; returns the number of
 * candidates (old form and absent). */
static int cln_list(void)
{
    int i;
    int pass;
    int candidates;
    CLN_ENTRY *e;

    candidates = 0;
    for (i = 0; i < clnEntryCount; i++) {
        if (CLN_IS_OLD(clnEntries[i].Form) &&
            clnEntries[i].Presence == CLN_ABSENT) {
            candidates++;
        }
    }
    if (clnEntryCount == 0) {
        say("No devnode is left under XHCI98\\: nothing to do.\n");
        return 0;
    }
    for (pass = 0; pass < 2; pass++) {
        if (pass == 0) {
            say("Old devnodes of this project, not present (%d):\n",
                   candidates);
        } else if (candidates < clnEntryCount) {
            say("\nLeft alone (%d):\n", clnEntryCount - candidates);
        }
        for (i = 0; i < clnEntryCount; i++) {
            e = &clnEntries[i];
            if ((pass == 0) != (CLN_IS_OLD(e->Form) &&
                                e->Presence == CLN_ABSENT)) {
                continue;
            }
            say("  %s\n", e->Id);
            say("    DeviceDesc  %s\n", e->Desc[0] ? e->Desc : "(none)");
            say("    Driver key  %s\n",
                   e->Driver[0] ? e->Driver : "(none)");
            if (pass == 0) {
                say("    qualifies   %s, %s\n", cln_form_text(e->Form),
                       cln_presence_text(e->Presence));
            } else if (!CLN_IS_OLD(e->Form)) {
                say("    left        %s\n", cln_form_text(e->Form));
            } else if (e->Presence == CLN_PRESENT) {
                say("    left        present: an xhci98.sys before "
                       "2.4.0.0 is running\n");
            } else {
                say("    left        %s\n", e->Why);
            }
        }
    }
    return candidates;
}

/* ========================================================================== */
/* The question                                                                  */
/* ========================================================================== */

/* The installer's form (task 42-A.3): the choices one a line, "Enter for
 * [N]". Y goes on; N, Enter or end of input cancels; another answer is
 * asked again. */
static int cln_ask(int count)
{
    char question[128];
    char text[1024];
    char line[32];
    int r;

    _snprintf(question, sizeof(question) - 1, "Remove the %d old devnode(s) "
              "listed above?", count);
    question[sizeof(question) - 1] = '\0';
    inst_yesno_question(text, sizeof(text), question, "remove them",
                        "cancel: nothing is removed", 0);
    say("\n%s", text);
    for (;;) {
        say_flush();
        inst_wrap_input(&clnWrap);
        if (!cln_read_line(line, sizeof(line))) {
            say("\n");
            return 0;
        }
        r = inst_tb_answer(line, 0);
        if (r >= 0) {
            return r == 1;
        }
        say("  Type Y or N, or Enter for [N]: ");
    }
}

/* ========================================================================== */
/* The runs                                                                      */
/* ========================================================================== */

static int clnRemove;
static int clnYes;

static void cln_close_doors(void)
{
    int i;

    for (i = 0; i < clnCtrlCount; i++) {
        if (clnCtrls[i].Handle != INVALID_HANDLE_VALUE) {
            CloseHandle(clnCtrls[i].Handle);
            clnCtrls[i].Handle = INVALID_HANDLE_VALUE;
        }
    }
}

static int cln_run_9x(void)
{
    int candidates;
    int anyPresent;

    say("Windows 98 SE or ME: this release lists only, and removes "
           "nothing.\n\n");
    if (cln_9x_candidates() != 0) {
        return CLN_EXIT_REFUSED;
    }
    candidates = cln_list();
    anyPresent = cln_9x_named(NULL);
    if (clnWalkState != CLN_SET_COMPLETE) {
        say("\nWhether an XHCI98\\ devnode is present is unknown: %s.\n",
               clnWalkWhy);
    } else if (anyPresent) {
        say("\nAn XHCI98\\ devnode is present: an xhci98.sys before 2.4.0.0 "
               "is running (restart after the update first).\n");
    }
    if (clnWalkNoKey > 0) {
        say("(%d devnode(s) in HKEY_DYN_DATA carry no HardWareKey.)\n",
               clnWalkNoKey);
    }
    if (candidates > 0) {
        say("\nThe entries are inert, and nothing needs them removed. "
               "XHCICLN removes\nnothing on Windows 98 SE and ME in this "
               "release (README.TXT says why).\n");
    }
    if (clnRemove && (anyPresent || clnWalkState != CLN_SET_COMPLETE)) {
        return CLN_EXIT_REFUSED;
    }
    return CLN_EXIT_DONE;
}

/* ========================================================================== */
/* A stale xhcicm filter name (3.1.0.0, record 21 section 7.4)                  */
/* ========================================================================== */

typedef BOOL (WINAPI *PFN_SETREGPROP)(HDEVINFO, PSP_DEVINFO_DATA, DWORD,
                                      const BYTE *, DWORD);

#define CLN_FILTER_NAME "xhcicm"
#define CLN_FILTER_BYTES 1024
#define CLN_SPDRP_UPPERFILTERS 0x00000011UL
#define CLN_SPDRP_LOWERFILTERS 0x00000012UL

/* A line read from the console with its CR and LF taken off; 0 at the end
 * of input. */
static int cln_read_line(char *line, int cap)
{
    size_t n;

    if (fgets(line, cap, stdin) == NULL) {
        return 0;
    }
    n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = '\0';
    }
    return 1;
}

/* 1 when the xhcicm service is registered (or cannot be ruled out). */
static int cln_xhcicm_registered(void)
{
    HKEY k;
    LONG r;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Services\\xhcicm", 0,
                      KEY_READ, &k);
    if (r == ERROR_SUCCESS) {
        RegCloseKey(k);
        return 1;
    }
    return r != ERROR_FILE_NOT_FOUND;
}

/* One filter value's edit: the name taken out, or the value deleted; 1 done.
 * (Unused while the pass lists only; kept with cln_stale_filters' fix path
 * for the day it may write under the installer's lock.) */
static int cln_set_value(HKEY k, const char *name, int act, const char *nf,
                         size_t ob)
{
    LONG r;

    if (act == INST_FD_SET) {
        r = RegSetValueExA(k, name, 0, REG_MULTI_SZ, (const BYTE *)nf,
                           (DWORD)ob);
    } else if (act == INST_FD_DELETE) {
        r = RegDeleteValueA(k, name);
    } else {
        r = ERROR_INSUFFICIENT_BUFFER;
    }
    if (r != ERROR_SUCCESS) {
        say("    could not be removed (error %ld)\n", r);
        return 0;
    }
    say("    removed\n");
    return 1;
}

/* The filter values naming xhcicm with no xhcicm service on the machine:
 * every CriticalDeviceDatabase entry's LowerFilters and UpperFilters
 * (Windows 2000 and XP copy a boot-critical device's own filter values
 * there when they install it, and every new device of that class then gets
 * them) and every PCI devnode's, present or not. A filter named with no
 * service stops a device (Code 39; Code 19 read on 2000 for an NVMe given
 * the name from such a copy), so with fix each is taken out, the other
 * names kept, the value deleted when nothing else is left. A 3.1.0.0 test
 * build's uninstall cleaned the devices only. *found the values naming it
 * (with fix: those that could not be cleaned); returns 1 when every key,
 * entry and value was read (a value or key that is simply absent is no
 * error), 0 when the reading was incomplete. */
static int cln_stale_filters(int fix, int *found)
{
    static const char *const vals[2] = { "LowerFilters", "UpperFilters" };
    static const DWORD props[2] = { CLN_SPDRP_LOWERFILTERS,
                                    CLN_SPDRP_UPPERFILTERS };
    PFN_SETREGPROP setProp;
    HKEY root;
    HKEY k;
    HDEVINFO set;
    SP_DEVINFO_DATA data;
    DWORD i;
    DWORD nn;
    DWORD type;
    DWORD n;
    DWORD e;
    LONG r;
    char name[256];
    char id[CLN_ID_CHARS];
    char f[CLN_FILTER_BYTES];
    char nf[CLN_FILTER_BYTES];
    size_t ob;
    int act;
    int v;
    int complete = 1;

    *found = 0;
    setProp = (PFN_SETREGPROP)GetProcAddress(api.SetupApi,
                                             "SetupDiSetDeviceRegistryPropertyA");
    if (fix && setProp == NULL) {
        say("  not available: SetupDiSetDeviceRegistryPropertyA\n");
        return 0;
    }
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Control\\CriticalDeviceDatabase",
                      0, KEY_READ, &root);
    if (r == ERROR_SUCCESS) {
        for (i = 0;; i++) {
            nn = sizeof(name);
            r = RegEnumKeyExA(root, i, name, &nn, NULL, NULL, NULL, NULL);
            if (r == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (r != ERROR_SUCCESS) {
                say("  The CriticalDeviceDatabase could not be read whole "
                    "(error %ld).\n", r);
                complete = 0;
                if (r == ERROR_MORE_DATA) {
                    continue;
                }
                break;
            }
            r = RegOpenKeyExA(root, name, 0,
                              fix ? KEY_QUERY_VALUE | KEY_SET_VALUE
                                  : KEY_QUERY_VALUE, &k);
            if (r != ERROR_SUCCESS) {
                say("  CriticalDeviceDatabase\\%s could not be opened (error "
                    "%ld).\n", name, r);
                complete = 0;
                continue;
            }
            for (v = 0; v < 2; v++) {
                memset(f, 0, sizeof(f));
                n = sizeof(f) - 2;
                r = RegQueryValueExA(k, vals[v], NULL, &type, (LPBYTE)f, &n);
                if (r == ERROR_FILE_NOT_FOUND) {
                    continue;
                }
                if (r != ERROR_SUCCESS) {
                    say("  CriticalDeviceDatabase\\%s: %s could not be read "
                        "(error %ld).\n", name, vals[v], r);
                    complete = 0;
                    continue;
                }
                if (type != REG_MULTI_SZ && type != REG_SZ) {
                    continue;
                }
                act = inst_filter_drop(f, n + 1, CLN_FILTER_NAME, nf,
                                       sizeof(nf), &ob);
                if (act == INST_FD_NONE) {
                    continue;
                }
                say("  CriticalDeviceDatabase\\%s: %s names %s\n", name,
                    vals[v], CLN_FILTER_NAME);
                if (!fix || !cln_set_value(k, vals[v], act, nf, ob)) {
                    (*found)++;
                }
            }
            RegCloseKey(k);
        }
        RegCloseKey(root);
    } else if (r != ERROR_FILE_NOT_FOUND) {
        say("  The CriticalDeviceDatabase could not be read (error %ld).\n", r);
        complete = 0;
    }
    set = api.GetClassDevs(NULL, "PCI", NULL, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) {
        say("  The PCI device list could not be opened (error %lu).\n",
            (unsigned long)GetLastError());
        return 0;
    }
    for (i = 0;; i++) {
        memset(&data, 0, sizeof(data));
        data.cbSize = sizeof(data);
        if (!api.EnumInfo(set, i, &data)) {
            e = GetLastError();
            if (e != ERROR_NO_MORE_ITEMS) {
                say("  The PCI device list ended early (error %lu).\n",
                    (unsigned long)e);
                complete = 0;
            }
            break;
        }
        if (!api.GetInstanceId(set, &data, id, sizeof(id), NULL)) {
            cln_copy(id, "(a PCI device)", sizeof(id));
        }
        for (v = 0; v < 2; v++) {
            memset(f, 0, sizeof(f));
            n = 0;
            if (!api.GetRegProp(set, &data, props[v], &type, (PBYTE)f,
                                sizeof(f) - 2, &n)) {
                e = GetLastError();
                if (e != ERROR_INVALID_DATA) {
                    say("  %s: %s could not be read (error %lu).\n", id,
                        vals[v], (unsigned long)e);
                    complete = 0;
                }
                continue;
            }
            act = inst_filter_drop(f, n + 1, CLN_FILTER_NAME, nf, sizeof(nf),
                                   &ob);
            if (act == INST_FD_NONE) {
                continue;
            }
            say("  %s: %s names %s\n", id, vals[v], CLN_FILTER_NAME);
            if (!fix) {
                (*found)++;
                continue;
            }
            if (act == INST_FD_ERROR ||
                !setProp(set, &data, props[v],
                         act == INST_FD_SET ? (const BYTE *)nf : NULL,
                         act == INST_FD_SET ? (DWORD)ob : 0)) {
                say("    could not be removed (error %lu)\n",
                    (unsigned long)GetLastError());
                (*found)++;
            } else {
                say("    removed\n");
            }
        }
    }
    api.Destroy(set);
    return complete;
}

/* The stale-name pass of a run: listed only, while xhcicm itself is not
 * installed (while it is, its names are its own). Taking them out is the
 * uninstall's step 0 (XHCIINST /uninstall from a 3.1.0.0 package, which
 * runs it whatever is installed), gated, verified and serialised with the
 * installer and the mark service; a second writer here could not be. */
static void cln_stale_pass(void)
{
    int found;
    int complete;

    if (cln_xhcicm_registered()) {
        return;
    }
    complete = cln_stale_filters(0, &found);
    if (found > 0) {
        say("\nThe Thunderbolt driver xhcicm is not installed, but %d filter "
            "value(s) above still\nname it: a new bridge or NVMe drive would "
            "not start (Code 39 or 19). Run\nINSTALL.BAT /uninstall from the "
            "xhci98 3.1.0.0 package (or XHCIINST /uninstall) to\ntake them "
            "out; XHCICLN only lists them.\n", found);
    }
    if (!complete) {
        say("The filter values could not all be read (above).\n");
    }
}

static int cln_run_nt_devnodes(unsigned long major, unsigned long minor,
                               int wow64)
{
    char why[640];
    int missing;
    int removalMissing;
    int candidates;
    int ok;
    int i;
    int removed;
    int pending;
    int stopped;
    int refused;
    DWORD wait;
    DWORD cr;
    DWORD dn;
    SP_REMOVEDEVICE_PARAMS rdp;
    SP_DEVINSTALL_PARAMS_A dip;
    CLN_ENTRY *e;

    missing = cln_load_nt(&removalMissing);
    if (missing > 0) {
        say("xhcicln: a routine the listing needs is missing (above); "
               "nothing listed or removed.\n");
        return CLN_EXIT_REFUSED;
    }
    if (cln_nt_candidates() != 0) {
        return CLN_EXIT_REFUSED;
    }
    candidates = cln_list();

    /* The global refusals, read in a listing too so it says what -remove
     * would do. An old devnode present refuses even when nothing else
     * qualifies: it means an older xhci98.sys runs (before the update's
     * restart, or after going back), as on 98 SE and ME. */
    clnRefusal[0] = '\0';
    if (clnPresentState != CLN_SET_COMPLETE) {
        _snprintf(why, sizeof(why) - 1, "whether an XHCI98\\ devnode is "
                  "present is unknown: %s", clnPresentWhy);
        why[sizeof(why) - 1] = '\0';
        cln_refuse(why);
    } else if (clnPresentCount > 0) {
        _snprintf(why, sizeof(why) - 1, "%s is present: an xhci98.sys before "
                  "2.4.0.0 is running (restart after the update first, or this "
                  "is a system that went back)", clnPresentIds[0]);
        why[sizeof(why) - 1] = '\0';
        cln_refuse(why);
    }
    /* Any XHCI98\ entry, of any form, whose presence is not conclusively
     * absent: the global rule is about every such devnode. */
    for (i = 0; i < clnEntryCount; i++) {
        if (clnEntries[i].Presence == CLN_UNKNOWN) {
            _snprintf(why, sizeof(why) - 1, "%s: %s", clnEntries[i].Id,
                      clnEntries[i].Why);
            why[sizeof(why) - 1] = '\0';
            cln_refuse(why);
        } else if (clnEntries[i].Presence == CLN_PRESENT) {
            _snprintf(why, sizeof(why) - 1, "%s is present: an xhci98.sys "
                      "before 2.4.0.0 is running", clnEntries[i].Id);
            why[sizeof(why) - 1] = '\0';
            cln_refuse(why);
        }
    }
    if (candidates == 0) {
        /* Nothing would be removed; a refusal still answers -remove with 1
         * (the forced refusal before the update's restart, 10.13.10). */
        if (clnRefusal[0] != '\0') {
            say("\nRemoval refused: %s.\n", clnRefusal);
            return clnRemove ? CLN_EXIT_REFUSED : CLN_EXIT_DONE;
        }
        return CLN_EXIT_DONE;
    }
    if (clnRefusal[0] == '\0') {
        (void)cln_nt_hold_controllers();
    }
    if (clnRefusal[0] == '\0' && wow64) {
        cln_refuse("this is the 32-bit XHCICLN.EXE on 64-bit Windows, where "
                   "a 32-bit program cannot remove a device: run XHCICL64.EXE");
    }
    if (clnRefusal[0] == '\0' && removalMissing > 0) {
        cln_refuse("a routine the removal needs is missing (above)");
    }
    if (clnRefusal[0] == '\0' && !cln_removal_enabled(major, minor)) {
        _snprintf(why, sizeof(why) - 1, "removal is not enabled on Windows "
                  "%lu.%lu in this release; it lists only", major, minor);
        why[sizeof(why) - 1] = '\0';
        cln_refuse(why);
    }
    if (clnRefusal[0] != '\0') {
        say("\nRemoval refused, nothing removed: %s.\n", clnRefusal);
        cln_close_doors();
        return clnRemove ? CLN_EXIT_REFUSED : CLN_EXIT_DONE;
    }
    say("\nEvery check passed: %d project controller(s) held through their "
           "doors.\n", clnCtrlCount);
    if (!clnRemove) {
        say("Run XHCICLN -remove to remove the %d entr%s above.\n",
               candidates, candidates == 1 ? "y" : "ies");
        cln_close_doors();
        return CLN_EXIT_DONE;
    }
    if (!clnYes && !cln_ask(candidates)) {
        say("Cancelled; nothing removed.\n");
        cln_close_doors();
        return CLN_EXIT_REFUSED;
    }
    wait = api.WaitInstall(CLN_INSTALL_WAIT_MS);
    if (wait != WAIT_OBJECT_0) {
        say("Refused, nothing removed: an installation is still pending "
               "after %lu seconds (0x%lX).\n",
               (unsigned long)(CLN_INSTALL_WAIT_MS / 1000UL),
               (unsigned long)wait);
        cln_close_doors();
        return CLN_EXIT_REFUSED;
    }

    removed = 0;
    pending = 0;
    stopped = 0;
    refused = 0;
    for (i = 0; i < clnEntryCount && !stopped && !refused; i++) {
        e = &clnEntries[i];
        if (!CLN_IS_OLD(e->Form) || e->Presence != CLN_ABSENT) {
            continue;
        }
        /* Immediately before: the global tests again (which also retake the
         * present set) and this entry's own presence. */
        why[0] = '\0';
        ok = cln_nt_recheck(why, sizeof(why));
        if (ok && cln_nt_presence(e) != CLN_ABSENT) {
            _snprintf(why, sizeof(why) - 1, "it is no longer conclusively "
                      "absent (%s)", e->Why[0] ? e->Why : "present");
            why[sizeof(why) - 1] = '\0';
            ok = 0;
        }
        if (!ok) {
            say("Stopped before %s: %s.\n", e->Id, why);
            if (removed == 0) {
                refused = 1;
            } else {
                stopped = 1;
            }
            break;
        }
        memset(&rdp, 0, sizeof(rdp));
        rdp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        rdp.ClassInstallHeader.InstallFunction = DIF_REMOVE;
        rdp.Scope = DI_REMOVEDEVICE_GLOBAL;
        rdp.HwProfile = 0;
        if (!api.SetCiParams(clnSet, &e->Data, &rdp.ClassInstallHeader,
                             (DWORD)sizeof(rdp)) ||
            !api.CallCi(DIF_REMOVE, clnSet, &e->Data)) {
            DWORD err = GetLastError();

            say("Removing %s failed (error 0x%lX%s); stopped there.\n",
                   e->Id, (unsigned long)err,
                   (err == ERROR_ACCESS_DENIED) ?
                       ": an administrator is needed" : "");
            stopped = 1;
            break;
        }
        e->Done = 1;
        memset(&dip, 0, sizeof(dip));
        dip.cbSize = sizeof(dip);
        removed++;
        if (!api.GetDiParams(clnSet, &e->Data, &dip)) {
            /* Removed, but whether it waits on a restart is unknown: say
             * so, count it as pending (the safe reading) and stop. */
            e->Done = 2;
            pending++;
            say("Removed %s, but whether a restart is pending could not "
                   "be read (error 0x%lX); restart Windows to be sure. "
                   "Stopped.\n", e->Id, (unsigned long)GetLastError());
            stopped = 1;
            break;
        }
        if ((dip.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) != 0) {
            e->Done = 2;
            pending++;
        }
        say("Removed %s%s.\n", e->Id,
               (e->Done == 2) ? " (a restart is pending)" : "");
        dn = 0;
        cr = api.Locate(&dn, e->Id, CLN_CM_LOCATE_PHANTOM);
        if (cr != CLN_CR_NO_SUCH_DEVNODE) {
            say("But %s is still there (0x%02lX); stopped.\n", e->Id,
                   (unsigned long)cr);
            stopped = 1;
            break;
        }
        if (!cln_nt_recheck(why, sizeof(why))) {
            say("Stopped after %s: %s.\n", e->Id, why);
            stopped = 1;
            break;
        }
    }
    cln_close_doors();
    say("\n%d removed", removed);
    if (pending > 0) {
        say(", %d of them waiting on a restart", pending);
    }
    say(".\n");
    if (pending > 0) {
        say("Restart Windows to finish.\n");
    }
    return cln_exit_code(refused, stopped, removed, pending);
}

/* ========================================================================== */
/* -selftest                                                                     */
/* ========================================================================== */

static int clnChecks;
static int clnFailures;

/* The NT run: the stale xhcicm names listed first (not by the 32-bit build
 * on 64-bit Windows), then the old devnodes. */
static int cln_run_nt(unsigned long major, unsigned long minor, int wow64)
{
    if (!wow64 && api.SetupApi == NULL) {
        int rm;
        if (cln_load_nt(&rm) > 0) {
            return cln_run_nt_devnodes(major, minor, wow64);
        }
    }
    if (!wow64) {
        cln_stale_pass();
    }
    return cln_run_nt_devnodes(major, minor, wow64);
}

static void cln_check(int ok, const char *what)
{
    clnChecks++;
    if (!ok) {
        clnFailures++;
        say("FAIL: %s\n", what);
    }
}

static int cln_selftest(void)
{
    static const struct {
        const char *Id;
        int Form;
    } ids[] = {
        /* each old form */
        { "XHCI98\\ROOT_HUB\\4&1A2B3C4D&0", CLN_FORM_OLD_ROOT_HUB },
        { "XHCI98\\HUB&VID_2109&PID_2813\\5&ABC&0&1", CLN_FORM_OLD_HUB },
        { "XHCI98\\HUB30&VID_2109&PID_0813\\SERIAL0001", CLN_FORM_OLD_HUB30 },
        { "XHCI98\\COMPOSITE&VID_0D8C&PID_0014\\6&11&0&3",
          CLN_FORM_OLD_COMPOSITE },
        /* 98 SE's location form: the parent's path with & for \ */
        { "XHCI98\\ROOT_HUB\\0PCI&VEN_1B36&DEV_000D&SUBSYS_11001AF4&REV_01&BUS_00&DEV_04&FUNC_00",
          CLN_FORM_OLD_ROOT_HUB },
        /* case variants */
        { "xhci98\\root_hub\\0", CLN_FORM_OLD_ROOT_HUB },
        { "Xhci98\\Hub&vid_abcd&pid_ef01\\1", CLN_FORM_OLD_HUB },
        { "XHCI98\\composite&VID_0d8c&PID_0014\\X", CLN_FORM_OLD_COMPOSITE },
        /* each new form: current, never a candidate */
        { "USB\\XHCI98_ROOT_HUB\\4&1A2B3C4D&0", CLN_FORM_CURRENT },
        { "USB\\XHCI98_HUB&VID_2109&PID_2813\\5&ABC&0&1", CLN_FORM_CURRENT },
        { "USB\\XHCI98_HUB30&VID_2109&PID_0813\\SERIAL0001",
          CLN_FORM_CURRENT },
        { "USB\\XHCI98_COMPOSITE&VID_0D8C&PID_0014\\6&11&0&3",
          CLN_FORM_CURRENT },
        { "usb\\xhci98_root_hub\\0", CLN_FORM_CURRENT },
        /* the refused interface's id, and other USB\ ids: not ours */
        { "USB\\XHCI98_NOXPORT&VID_0781&PID_5581&MI_00\\1",
          CLN_FORM_NOT_OURS },
        { "USB\\XHCI98_NOXPORT&VID_0781&PID_5581\\SER", CLN_FORM_NOT_OURS },
        { "USB\\VID_0781&PID_5581\\4C530001", CLN_FORM_NOT_OURS },
        { "USB\\ROOT_HUB30\\4&1&0", CLN_FORM_NOT_OURS },
        { "PCI\\VEN_1B36&DEV_000D\\3&1&0", CLN_FORM_NOT_OURS },
        { "XHCI98", CLN_FORM_NOT_OURS },
        /* a truncated hex field, and other near misses */
        { "XHCI98\\HUB&VID_210&PID_2813\\1", CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\HUB&VID_2109&PID_281\\1", CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\HUB&VID_21G9&PID_2813\\1", CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\HUB&VID_2109&PID_2813&REV_9093\\1",
          CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\HUB&VID_2109\\1", CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\HUB\\1", CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\ROOT_HUB20\\1", CLN_FORM_OTHER_XHCI98 },
        /* an unrelated XHCI98\ id, and malformed instance ids */
        { "XHCI98\\NOXPORT&VID_0781&PID_5581\\1", CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\SOMETHING_ELSE\\1", CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\ROOT_HUB", CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\ROOT_HUB\\", CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\\\0", CLN_FORM_OTHER_XHCI98 },
        { "XHCI98\\ROOT_HUB\\0\\1", CLN_FORM_OTHER_XHCI98 }
    };
    char out[32];
    char msg[256];
    int i;

    say("xhcicln %s self-test\n", XHCICLN_VERSION);

    for (i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++) {
        _snprintf(msg, sizeof(msg) - 1, "classify %s", ids[i].Id);
        msg[sizeof(msg) - 1] = '\0';
        cln_check(cln_classify(ids[i].Id) == ids[i].Form, msg);
    }
    cln_check(cln_classify(NULL) == CLN_FORM_NOT_OURS, "classify NULL");

    /* NT presence: the two answers must agree, and a lookup error, a missing
     * routine or a failed set is unknown. */
    cln_check(cln_presence_nt(1, CLN_CR_NO_SUCH_DEVNODE, CLN_SET_COMPLETE, 0)
              == CLN_ABSENT, "NT: no devnode, not in the set: absent");
    cln_check(cln_presence_nt(1, CLN_CR_SUCCESS, CLN_SET_COMPLETE, 1)
              == CLN_PRESENT, "NT: located and in the set: present");
    cln_check(cln_presence_nt(1, CLN_CR_SUCCESS, CLN_SET_COMPLETE, 0)
              == CLN_UNKNOWN, "NT: located but not in the set: unknown");
    cln_check(cln_presence_nt(1, CLN_CR_NO_SUCH_DEVNODE, CLN_SET_COMPLETE, 1)
              == CLN_UNKNOWN, "NT: not located but in the set: unknown");
    cln_check(cln_presence_nt(1, 0x33UL, CLN_SET_COMPLETE, 0)
              == CLN_UNKNOWN, "NT: access denied: unknown");
    cln_check(cln_presence_nt(1, 0x1FUL, CLN_SET_COMPLETE, 0)
              == CLN_UNKNOWN, "NT: invalid data: unknown");
    cln_check(cln_presence_nt(0, CLN_CR_NO_SUCH_DEVNODE, CLN_SET_COMPLETE, 0)
              == CLN_UNKNOWN, "NT: routine missing: unknown");
    cln_check(cln_presence_nt(1, CLN_CR_NO_SUCH_DEVNODE, CLN_SET_FAILED, 0)
              == CLN_UNKNOWN, "NT: the present set failed: unknown");

    /* 98 SE and ME: an incomplete walk is unknown for every entry. */
    cln_check(cln_presence_9x(CLN_SET_COMPLETE, 0) == CLN_ABSENT,
              "9x: whole walk, not named: absent");
    cln_check(cln_presence_9x(CLN_SET_COMPLETE, 1) == CLN_PRESENT,
              "9x: whole walk, named: present");
    cln_check(cln_presence_9x(CLN_SET_FAILED, 0) == CLN_UNKNOWN,
              "9x: incomplete walk: unknown");
    cln_check(cln_presence_9x(CLN_SET_FAILED, 1) == CLN_UNKNOWN,
              "9x: incomplete walk, named: unknown");

    /* The controller set: a service-less PCI devnode is passed over; any
     * other failure, or a non-string, is unknown. */
    cln_check(cln_service_verdict(1, CLN_CR_SUCCESS, REG_SZ, "xhci98")
              == CLN_SVC_OURS, "service xhci98: ours");
    cln_check(cln_service_verdict(1, CLN_CR_SUCCESS, REG_SZ, "XHCI98")
              == CLN_SVC_OURS, "service XHCI98: ours");
    cln_check(cln_service_verdict(1, CLN_CR_SUCCESS, REG_SZ, "usbxhci")
              == CLN_SVC_NOT_OURS, "service usbxhci: not ours");
    cln_check(cln_service_verdict(1, CLN_CR_SUCCESS, REG_SZ, "xhci98x")
              == CLN_SVC_NOT_OURS, "service xhci98x: not ours");
    cln_check(cln_service_verdict(1, CLN_CR_NO_SUCH_VALUE, 0, NULL)
              == CLN_SVC_NOT_OURS, "service-less PCI devnode: passed over");
    cln_check(cln_service_verdict(1, 0x33UL, 0, NULL)
              == CLN_SVC_UNKNOWN, "service: access denied: unknown");
    cln_check(cln_service_verdict(1, CLN_CR_SUCCESS, REG_BINARY, "xhci98")
              == CLN_SVC_UNKNOWN, "service: not a string: unknown");
    cln_check(cln_service_verdict(0, CLN_CR_SUCCESS, REG_SZ, "xhci98")
              == CLN_SVC_UNKNOWN, "service: routine missing: unknown");

    /* The door: read, never guessed. */
    cln_check(cln_door_path("\\DosDevices\\HCD0", out, sizeof(out)) &&
              strcmp(out, "\\\\.\\HCD0") == 0, "door \\DosDevices\\HCD0");
    cln_check(cln_door_path("\\??\\HCD12", out, sizeof(out)) &&
              strcmp(out, "\\\\.\\HCD12") == 0, "door \\??\\HCD12");
    cln_check(cln_door_path("\\dosdevices\\hcd3", out, sizeof(out)) &&
              strcmp(out, "\\\\.\\HCD3") == 0, "door, case");
    cln_check(!cln_door_path("\\DosDevices\\XHCI98RH1", out, sizeof(out)),
              "door: the root hub's name refused");
    cln_check(!cln_door_path("\\DosDevices\\HCD", out, sizeof(out)),
              "door: no index refused");
    cln_check(!cln_door_path("\\DosDevices\\HCD1234", out, sizeof(out)),
              "door: four digits refused");
    cln_check(!cln_door_path("\\DosDevices\\HCD1x", out, sizeof(out)),
              "door: not digits refused");
    cln_check(!cln_door_path("\\Device\\HCD0", out, sizeof(out)),
              "door: another prefix refused");
    cln_check(!cln_door_path("", out, sizeof(out)), "door: empty refused");
    cln_check(!cln_door_path(NULL, out, sizeof(out)), "door: none refused");

    /* The exit codes. */
    cln_check(cln_exit_code(0, 0, 0, 0) == CLN_EXIT_DONE,
              "exit: nothing to do");
    cln_check(cln_exit_code(0, 0, 3, 0) == CLN_EXIT_DONE,
              "exit: all removed");
    cln_check(cln_exit_code(0, 0, 3, 1) == CLN_EXIT_RESTART,
              "exit: all removed, a restart pending");
    cln_check(cln_exit_code(1, 0, 0, 0) == CLN_EXIT_REFUSED,
              "exit: refused (an empty controller set, an unknown)");
    cln_check(cln_exit_code(0, 1, 1, 1) == CLN_EXIT_STOPPED,
              "exit: stopped part-way, a restart pending kept in the lines");
    cln_check(cln_exit_code(0, 1, 0, 0) == CLN_EXIT_STOPPED,
              "exit: the first removal failed");

    /* The removal table: the five NT targets, nothing else. */
    cln_check(cln_removal_enabled(5, 0) && cln_removal_enabled(5, 1) &&
              cln_removal_enabled(5, 2) && cln_removal_enabled(6, 0) &&
              cln_removal_enabled(6, 1), "removal: the NT targets");
    cln_check(!cln_removal_enabled(6, 2) && !cln_removal_enabled(10, 0) &&
              !cln_removal_enabled(4, 0), "removal: other NT versions list only");

    /* The console's wrap (task 42-A.3): a line made long by an instance id
     * is held to 79 columns, and the question's last line is Enter's. */
    {
        char text[1024];
        char wrapped[2048];
        inst_wrap_text("  qualifies   an old root hub's entry, not present: "
                       "XHCI98\\ROOT_HUB\\0PCI&VEN_1B36&DEV_000D&SUBSYS_"
                       "11001AF4&REV_01&BUS_00&DEV_04&FUNC_00\n", wrapped,
                       sizeof(wrapped));
        cln_check(inst_text_width(wrapped) <= INST_COLUMNS,
                  "the wrap: a long line held to 79 columns");
        inst_yesno_question(text, sizeof(text), "Remove the 12 old "
                            "devnode(s) listed above?", "remove them",
                            "cancel: nothing is removed", 0);
        cln_check(inst_text_width(text) <= INST_COLUMNS &&
                  strstr(text, "Enter for [N]: ") != NULL,
                  "the question: its choices, and Enter for [N]");
    }
    say("%d checks, %d failures\n", clnChecks, clnFailures);
    say_flush();
    return clnFailures == 0 ? 0 : 1;
}

/* ========================================================================== */
/* main                                                                          */
/* ========================================================================== */

static void cln_usage(void)
{
    say(
"Usage: XHCICLN [-remove [-yes]] | -selftest\n"
"\n"
"  (none)    list the devnodes an xhci98.sys before 2.4.0.0 left under\n"
"            XHCI98\\, and say whether -remove would remove them\n"
"  -remove   list, ask once, then remove the old devnodes that are not\n"
"            present (Windows 2000 to 7; on 98 SE and ME it lists only)\n"
"  -yes      with -remove: do not ask\n"
"  -selftest run the tool's own checks; touches nothing\n"
"\n"
"Exit codes: 0 nothing to do, every entry removed, or (98 SE, ME) listed;\n"
"1 refused before any removal; 2 stopped part-way; 3 every entry removed,\n"
"a restart pending.\n");
}

int main(int argc, char **argv)
{
    OSVERSIONINFOA ver;
    PFN_ISWOW64 isWow64;
    BOOL wow;
    int i;

    atexit(say_flush);
    for (i = 1; i < argc; i++) {
        if (cln_eq(argv[i], "-selftest") || cln_eq(argv[i], "/selftest")) {
            if (argc != 2) {
                cln_usage();
                return CLN_EXIT_REFUSED;
            }
            return cln_selftest();
        } else if (cln_eq(argv[i], "-remove") || cln_eq(argv[i], "/remove")) {
            clnRemove = 1;
        } else if (cln_eq(argv[i], "-yes") || cln_eq(argv[i], "/yes")) {
            clnYes = 1;
        } else {
            cln_usage();
            return CLN_EXIT_REFUSED;
        }
    }
    if (clnYes && !clnRemove) {
        say("xhcicln: -yes is accepted only with -remove.\n\n");
        cln_usage();
        return CLN_EXIT_REFUSED;
    }

    say("xhcicln %s - the devnodes xhci98.sys left under XHCI98\\ before "
           "2.4.0.0\n\n", XHCICLN_VERSION);

    memset(&ver, 0, sizeof(ver));
    ver.dwOSVersionInfoSize = sizeof(ver);
    if (!GetVersionExA(&ver)) {
        say("xhcicln: the Windows version could not be read.\n");
        return CLN_EXIT_REFUSED;
    }
    if (ver.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
        return cln_run_9x();
    }
    if (ver.dwPlatformId != VER_PLATFORM_WIN32_NT) {
        say("xhcicln: not a Windows this tool knows.\n");
        return CLN_EXIT_REFUSED;
    }
    wow = FALSE;
    isWow64 = (PFN_ISWOW64)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                          "IsWow64Process");
    if (isWow64 != NULL && !isWow64(GetCurrentProcess(), &wow)) {
        wow = FALSE;
    }
    return cln_run_nt((unsigned long)ver.dwMajorVersion,
                      (unsigned long)ver.dwMinorVersion, wow ? 1 : 0);
}
