/*
 * inst_eng.c - the engine of the install, update and uninstall tool: the
 * system half of design record 23 behind inst_eng.h's front-end table, so
 * the console tool XHCIINST.EXE / XHCIIN64.EXE (xhciinst.c) and the window
 * XHCIWIZ.EXE / XHCIWZ64.EXE (xhciwiz.c) run one code (design record 26
 * section 3; roadmap-usb4.md tasks 40.4 to 40.6, 42-A.2 and 42-A.3).
 *
 * THE FRONT END. Every line the run reports goes to INST_FRONT's Line,
 * every question to its Ask as data (with the console's wording in it),
 * every step and wait to Progress and Waiting. The engine writes the log
 * itself, opens no console and no dialog, and is single threaded.
 *
 * WINDOWS 98 SE AND ME (record 26 section 7, record 27). After the copy
 * and DevicePath, each device of the plan is installed by the 16-bit
 * helper XHCIDI.EXE, one job per start, its verdict read from a result
 * file; a device the helper cannot do keeps the printed steps. The
 * uninstall removes the devnodes through the helper too.
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
#include "inst_eng.h"
#include "../src/cm/cm_status.h"
#include "../src/cm/cm_query.h"

/* The driver's own switchover gate (src\xhci_psw.c), declared here rather
 * than through xhci_psw.h, whose compatibility header is the driver's. */
unsigned long XhciPswGate(unsigned long vendorDevice);

#define XHCIINST_VERSION XHCI_VER_STR

#ifdef _WIN64
#define INST_MY_ARCH        INST_ARCH_AMD64
#define INST_DIR_SUFFIX     "-x64"
#define INST_DEFAULT_DIR    "release-x64"
#define INST_PE_MINE        INST_PE_AMD64
#else
#define INST_MY_ARCH        INST_ARCH_X86
#define INST_DIR_SUFFIX     "-x86"
#define INST_DEFAULT_DIR    "release-x86"
#define INST_PE_MINE        INST_PE_I386
#endif

/* The program this engine runs in (inst_eng_init): its own file name,
 * which the package check reads from its path, and the x64 counterpart a
 * 32-bit run under WOW64 names. */
static const char *instSelf = "XHCIINST.EXE";
static const char *instOther = "XHCIIN64.EXE";
static int instWindow;
#define INST_MY_NAME        instSelf
#define INST_OTHER_NAME     instOther

/* The front end (inst_eng.h). */
static INST_FRONT front;

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
#define INST_REAPPEAR_MS            15000UL    /* a device a parent's job hid */

#ifndef ERROR_IN_WOW64
#define ERROR_IN_WOW64 0xE0000235UL
#endif
#ifndef ERROR_NO_SUCH_DEVINST
#define ERROR_NO_SUCH_DEVINST 0xE000020BUL
#endif

#define INST_MAX_DEVS       1536
#define INST_MSZ_BYTES      2048
#define INST_PATH_CHARS     520

/* The INF slots: file, service, systems. */
static const char *const instInfFile[INST_INF_COUNT] = {
    "xhci98.inf", "xhciuas.inf", "xhcicm.inf", "xhcicmf.inf", "stor98.inf"
};
static const char *const instInfService[INST_INF_COUNT] = {
    "xhci98", "xhciuas", "xhcicm", "xhcicm", "stor98"
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
/* ntdll's RtlGetVersion (record 23 section 4.3, revision 13): the version
 * GetVersionEx gives a program with no compatibility manifest stops at
 * 6.2 from Windows 8.1 on. */
typedef LONG (WINAPI *PFN_RTLGETVERSION)(OSVERSIONINFOW *);

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
    PFN_CM_REL Parent;
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
    int S98Place;           /* Stor98: INST_S98_AT_*          */
    int S98Why;             /* Stor98: INST_S98_ELIGIBLE or the
                               reason it is left             */
} DEVREC;

static int is_controller(const DEVREC *d);
static const char *first_hw(const DEVREC *d);

/* The service groups of record 23 section 5.4. */
#define INST_GROUPS 4
static const char *const groupService[INST_GROUPS] = {
    "xhci98", "xhciuas", "xhcicm", "stor98"
};
#define INST_GROUP_S98 3
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

/* 98 SE and ME: the helper XHCIDI.EXE's state this run (record 27): 0 not
 * tried, 1 usable, -1 not used (absent, its probe failed, or a process
 * that did not end). */
static int helperState;
static char helperPath[INST_PATH_CHARS];    /* its short path, no space   */
static char helperResult[INST_PATH_CHARS];  /* the result file            */
static char helperJobFile[INST_PATH_CHARS]; /* the job file (section 4.1) */
static int helperRan;       /* a device job ran: the steps are per device  */
static int helperLastOut;   /* the last job's INST_DI_OUT_*, -1 none made  */
static int parentJobs;      /* parents' jobs (or NT calls) that reached
                               their device this run: inst_absent_retry */
static int helperStop;      /* a settle inside a helper job failed: the
                               passes stop (said by settle_failed)      */

/* The devices the helper could not do, by id, with the line said for
 * each: the steps page's (record 26 section 7), and the steps as text. */
#define INST_MAX_STEPDEVS 64
static char stepsId[INST_MAX_STEPDEVS][INST_ID_CHARS];
static char stepsWhy[INST_MAX_STEPDEVS][192];
static int stepsCount;
static const char *steps_why(const char *id);
#define INST_STEPS_TEXT 16384
static char stepsText[INST_STEPS_TEXT];
static int stepsCapture;    /* out() also appends to stepsText            */

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
static char *infKeep[INST_INF_COUNT];    /* the Thunderbolt INFs, kept while
                                           left out of the run (tb_apply) */
static INST_IDLIST infIds[INST_INF_COUNT];
static int infTrap[INST_INF_COUNT];
static int trapUnknown[INST_INF_COUNT];  /* the store could not be inspected */
static INST_U32 pkgVersion[4];

/* Stor98, the bundled NVMe driver (record 26 sections 5.6 and 6.3): an INF
 * slot of its own, in the run only when ticked (s98Include), at its own
 * version. */
static char *s98Text;       /* stor98.inf, read when the package has it   */
static int haveS98;         /* the flavour directory carries Stor98       */
static int s98Include;      /* ticked, or /stor98, with the Thunderbolt
                               component in the run                       */
static int s98Want;         /* ticked, or /stor98                          */
static int s98TopoFresh;    /* the places were read since the last scan   */
static int s98Share;        /* INST_S98_SHARE_*, the latest plan's        */
static int s98Shown;        /* the plan was printed: later reasons are said */
static int s98HoldRun;      /* the copy was made held: held all the run   */
static void s98_places(void);
static INST_U32 s98Version[4];
static char s98VersionText[32];
static const INST_U32 *inf_version(int k);
static const INST_U32 *group_version(int g);
static void s98_apply(int include);
static int s98_loader_9x(const DEVREC *d);

/* The install record (record 26 section 6.3, "The record"; owner,
 * 2026-10-18: the uninstall removes the bundled Stor98 only when this
 * package's installer installed it). */
#define INST_S98_KEY        "Software\\xhci98\\Stor98"
#define INST_S98_SVC_NT     "SYSTEM\\CurrentControlSet\\Services\\stor98"
#define INST_S98_PARAM_NT   INST_S98_SVC_NT "\\Parameters\\Device"
#define INST_S98_SVC_9X     "System\\CurrentControlSet\\Services\\stor98"
#define INST_S98_CLASS_9X   "System\\CurrentControlSet\\Services\\Class\\SCSIAdapter"
/* The id lists hold as many devices as a reading admits. */
#define INST_S98_MAX_DEV    INST_MAX_DEVS
#define INST_S98_MAX_INF    2048
#define INST_S98_RECONNECT_MS 120000UL

#define INST_S98_PARAM_CHARS 2048
/* The baseline: every devnode stor98.inf's models match, present or not,
 * with what ran it then (0 no driver, 1 another, 2 Stor98). */
typedef struct _S98BASE {
    char Id[INST_ID_CHARS];
    int State;
} S98BASE;
static S98BASE *s98Base;
static int s98BaseCount;
static char s98TunId[INST_S98_MAX_DEV][INST_ID_CHARS];
static int s98TunCount;     /* NT: updates of drives behind a tunnel port  */
static char s98NewId[INST_S98_MAX_DEV][INST_ID_CHARS];
static int s98NewCount;     /* devices recorded in the step just done      */

static char *s98OemNames;   /* NT: %windir%\inf\oem*.inf before staging    */
static int s98OemOk;
static int s98Baseline;     /* the baseline was taken in this run          */
static int s98Pre;          /* the record's PreExisting                    */
static int s98PreOwed;      /* PreExisting read, written at the first change */
static int s98Cold;         /* 98 SE and ME: Stor98's file was replaced    */
static int s98Left;         /* the uninstall left a Stor98 it did not install */
static int s98FileUpd;      /* 98 SE and ME: the plan replaces stor98.mpd  */
static int s98FileOk;       /* ... and the user confirmed it (the box, Y)  */
static int s98FileDone;     /* ... and the replacement is queued           */
static int s98Declined;     /* ... not confirmed: Stor98 out of the run    */
static int s98InfRefresh;   /* 98 SE and ME: the INF copies made the bundled
                               one's (the file is it, or replaced next)   */
static char s98AwaitId[INST_S98_MAX_DEV][INST_ID_CHARS];
static int s98AwaitCount;   /* NT: updated, awaiting reconnection          */
static int s98Set[INST_S98_COUNT];
static char s98Val[INST_S98_COUNT][32];

/* 98 SE and ME: %windir%\INF and INF\OTHER, listed (record 26 section 6.3:
 * a copy the setup engine made during one of this installer's installs or
 * a settle of this run, told from one that was there before). */
typedef struct _S98INF {
    char Name[48];          /* "X.INF" or "OTHER\X.INF"                    */
    DWORD Size;
    FILETIME Write;
    char Hash[65];          /* SHA-256, where it was read                  */
} S98INF;
static S98INF *s98Inf[2];
static int s98InfCount[2];
static int s98InfOk[2];

static int s98_baseline(void);
static void s98_record_devices(void);
static void s98_final_unknown(void);
static void s98_record_version(void);
static void s98_write_values(void);
static void s98_ask_values(void);
static void s98_ask_unused(void);
static void s98_file_update_9x(void);
static void s98_wininit_cancel(const char *bin);
static int s98_pre_commit(void);
static void s98_question(void);
static void s98_reconnect_nt(void);
static int s98_awaiting(const char *id);
static void s98_inf_take(int slot, int hash);
static int s98_inf_changed(int other, const S98INF *x);
static void s98_inf_after(const char *infPath, int settle);
static int s98_stray_ours(const char *name);
static int s98_other_9x(void);
static void s98_inf_sync(void);
static int s98_base_9x(void);
static int s98_param_read(char *param, size_t cap);
static void s98_tunnel_mark(void);
static int s98_un_devices(HDEVINFO set);
static int s98_un_verify(void);
static int s98_un_rest(void);
static int s98_rec_devices_any(int present);
static void s98_oem_before(void);
static int s98_oem_was(const char *name);
static int s98_reread_9x(const char *id, char *infPath, size_t cap);

static int s98_base_state(const char *id);
static int s98_rec_text(const char *sub, const char *name, const char *text);
static int s98_rec_has(const char *sub, const char *name);
static int s98_pending_resolve(int uninstall);
static int s98_pending_mark(const char *id, const char *state);
static int file_state(const char *path);

static int haveCm;
/* The connection manager alone (record 23 section 4.5, revision 13): the
 * system's kind, and the USB drivers in this run or out of it. */
static int osKind = INST_OS_FULL;
static int usbInclude = 1;
static int usbOffered;
static int uasInclude = 1;  /* xhciuas.inf in the run, with the USB drivers */
/* The USB storage drivers on Windows 98 SE (record 26 section 6.5). */
static int storHere;        /* 98 SE: the row is shown                   */
static int storTools;       /* any other system: the row is shown, and a
                               tick copies the seven to the tools folder
                               only (record 26 section 5.3.1 case 1)     */
static int storToolsInclude;
static int storPack;        /* 1 the download's seven verified, 0 absent,
                               -1 not this release's                    */
static int storInclude;     /* the row ticked                            */
static int storCopyOk;      /* the seven go into this run's package copy */
static char storPackDir[INST_PATH_CHARS];
static int storPlanCase;      /* the case the plan showed (defined below) */
static void pkg_src(const char *rel, char *path, size_t cap);
static void own_variant(const char *path, const char *ext, char *out,
                        size_t cap);
static void stor_read(void);
static void stor_question(void);
static void stor_print_plan(void);
static int stor_run_begin(void);
static void stor_copy_made(void);
static int stor_stage(int kind, int *first);
static void stor_record(void);
static void stor_run_end(void);
static int stor_any(int kind);
static int stor_covered(void);
static void stor_tools_copy(void);
static int stor_tools_remove(void);
static void stor_silent_merge(void);
static int stor_silent_remove(void);
static int silent_gate_on(void);
static int controller_listed(void);
static int stor_un_devices(void);
static int stor_un_adopt(int record);
static int stor_un_rest(void);
static int stor_forget(void);
static int stor_rec_devices_any(void);
static void usb_apply(int include);
static int usb_decide(void);
static int pkgCmSigned = -1;    /* xhcicm.sys signed: as pkgSigned */
static int tbInclude = 1;   /* the Thunderbolt question's answer (section 8.5) */
static int tbNhiPresent;
static char tbNhiName[INST_ID_CHARS];   /* the first controller's name   */
static int tbStaged;        /* NT: xhcicm.inf was staged by this run       */
static int tbTagAll;        /* package_files lists every file, whatever the
                               answer: the package's identity             */
static void own_install(void);
static int own_remove(void);
static int own_remove_in(const char *dir, int window);
static int own_any_in(const char *dir);
static void arp_write(int incomplete);
static int arp_remove(void);
static int mw_values_remove(void);
static void tb_question(void);
static int tb_decide(int *asked, int *def, int *installed);
static void tb_apply(int include);
static void tb_later_note(const char *dir);

static int flagFailed;
static int flagRestart;
static int flagSteps;
static int flagChanged;
static int outQuiet;        /* a reading repeated: its text to the log only */
static int trayDeferred;    /* the USB tree tool goes at the restart        */

/* The registry answers: per value, 1 when it is to be written, and what;
 * answerChosen: given on the window's settings pages rather than by /set
 * (record 26 section 5.4). */
static int answerSet[INST_MAX_VALUES];
static INST_U32 answerValue[INST_MAX_VALUES];
static int answerChosen[INST_MAX_VALUES];

/* ======================================================================== */
/* Output, the questions and the log (record 26 section 3.2)                */
/* ======================================================================== */

/* The self-test's screens (selftest): printed through the engine's own
 * screens front end, which wraps and measures, shown only when
 * XHCIINST_SCREENS is set. */
static int stScreens;
static int stCmOnly;        /* the screens: the machine has the connection
                               manager alone (the uninstall's warning) */
static int stShow;
static const char *const *stInput;     /* the self-test's answers */

/* Report text to the front end; under outQuiet (a reading repeated) to
 * the log alone. The front end puts what it shows in the log
 * (inst_eng_log_text). */
static void out(const char *fmt, ...)
{
    char buf[8192];
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    if (outQuiet) {
        if (logFile != NULL) {
            fputs(buf, logFile);
            fflush(logFile);
        }
        return;
    }
    if (stepsCapture &&
        strlen(stepsText) + strlen(buf) + 1 < sizeof(stepsText)) {
        strcat(stepsText, buf);
    }
    if (front.Line != NULL) {
        front.Line(front.Ctx, buf);
    }
}

/* What the console's wrap holds back (a prompt's last word), shown: before
 * a wait, and at the end. An empty Line is the flush. */
static void out_flush(void)
{
    if (front.Line != NULL) {
        front.Line(front.Ctx, "");
    }
}

/* To the log alone: the calls and their results. */
static void logonly(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    if (logFile != NULL) {
        fputs(buf, logFile);
        fflush(logFile);
    }
    if (front.Log != NULL) {
        front.Log(front.Ctx, buf);
    }
}

void inst_eng_log_text(const char *text)
{
    if (logFile != NULL && text != NULL && text[0] != '\0') {
        fputs(text, logFile);
        fflush(logFile);
    }
}

const char *inst_eng_log_path(void)
{
    return logPath;
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
        out("The log %s could not be opened; going on without it.\n\n",
            logPath);
        logPath[0] = '\0';
        return;
    }
    logonly("\n==== %s %s, %04u-%02u-%02u %02u:%02u:%02u ====\n",
            instWindow ? "xhciwiz" : "xhciinst", XHCIINST_VERSION, t.wYear,
            t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

/* One question through the front end (inst_eng.h, INST_ASK): 1 with the
 * answer, which the log records as the console's answers are; 0 for no
 * answer (the console's end of input, the window's cancel). retry: the
 * same question again after an answer that was not one, text then being
 * the reminder. */
static int ask(int kind, const char *text, int retry, int value,
               int defaultYes, const char *def, char *buf, size_t cap)
{
    INST_ASK q;

    memset(&q, 0, sizeof(q));
    q.Kind = kind;
    q.Retry = retry;
    q.Text = text;
    q.Value = value;
    q.DefaultYes = defaultYes;
    q.Default = def;
    buf[0] = '\0';
    if (front.Ask == NULL || !front.Ask(front.Ctx, &q, buf, cap)) {
        buf[0] = '\0';
        logonly("(no answer)\n");
        return 0;
    }
    buf[cap - 1] = '\0';
    logonly("> %s\n", buf);
    return 1;
}

/* A step of record 23's order to the front end (the window's Progress
 * page); the text is the console's too, so nothing is said twice. */
static void step(int state, const char *fmt, ...)
{
    char buf[512];
    INST_STEP s;
    va_list ap;

    if (front.Progress == NULL) {
        return;
    }
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    s.State = state;
    s.Text = buf;
    front.Progress(front.Ctx, &s);
}

/* A wait's tick, or a step boundary, to the front end: 0 when the user
 * asked the run to stop (the window's Cancel). */
static int waiting(int kind, DWORD elapsed, DWORD bound, const char *what)
{
    INST_WAIT w;

    if (front.Waiting == NULL) {
        return 1;
    }
    w.Kind = kind;
    w.ElapsedMs = elapsed;
    w.BoundMs = bound;
    w.What = what;
    return front.Waiting(front.Ctx, &w);
}

/* A step boundary (record 26 section 5.8): 0 when the user cancelled, so
 * the run stops before the next step, said once and left with steps (a
 * step that started has finished). */
static int runCancelled;

static int go_on(void)
{
    if (runCancelled) {
        return 0;
    }
    if (waiting(INST_WAIT_POINT, 0, 0, NULL)) {
        return 1;
    }
    runCancelled = 1;
    flagSteps = 1;
    out("\nCancelled: the run stops here. What was done stays done; run the "
        "installer\nagain to go on.\n");
    return 0;
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
    api.Parent = (PFN_CM_REL)proc(api.CfgMgr, "cfgmgr32", "CM_Get_Parent");
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
        } else if (reg_text3(HKEY_LOCAL_MACHINE, path, "PortDriver", text,
                             sizeof(text)) > 0) {
            /* An IOS port driver (Stor98's stor98.mpd, an IDE controller's
             * ESDI_506.PDR) runs the device: named by its file. */
            char *dot = strchr(text, '.');
            if (dot != NULL) {
                *dot = '\0';
            }
            inst_copy(d->Service, text, sizeof(d->Service));
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
    s98TopoFresh = 0;
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
    return inf == INST_INF_XHCI ? 0 : inf == INST_INF_UAS ? 1 :
           inf == INST_INF_S98 ? INST_GROUP_S98 : 2;
}

/* The version an INF's devices are measured against: the package's for
 * its own, the bundled Stor98's for stor98.inf. */
static const INST_U32 *inf_version(int k)
{
    return k == INST_INF_S98 ? s98Version : pkgVersion;
}

static const INST_U32 *group_version(int g)
{
    return g == INST_GROUP_S98 ? s98Version : pkgVersion;
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
    if (g == INST_GROUP_S98 && !envNow.Nt) {
        /* stor98.inf's 98 SE and ME section: DIRID 12, System\IoSubsys. */
        if (GetSystemDirectoryA(win, sizeof(win)) == 0) {
            path[0] = '\0';
            return;
        }
        join(path, cap, win, "IOSUBSYS\\stor98.mpd");
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
        if ((g == 0 || g == 1) && !usbInclude) {
            /* The connection manager alone: an installed xhci98 or xhciuas
             * is not part of the run (section 4.5). */
            groupVerdict[g] = INST_GROUP_OK;
            continue;
        }
        if (g == 1 && !uasInclude) {
            /* The UAS driver left out (owner, 2026-10-19): an installed
             * xhciuas is not part of the run either. */
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
                       inst_ver_cmp(d->Version, group_version(g)) > 0) {
                newer = 1;
            }
        }
        if (g == INST_GROUP_S98 && !s98Include) {
            /* Unticked: an installed Stor98 is never touched (record 26
             * section 6.3), so no verdict on it. */
            groupVerdict[g] = INST_GROUP_OK;
            continue;
        }
        group_binary(g, path, sizeof(path));
        r = file_version(path, v);
        binary = r == 0 ? -2 : r < 0 ? 2 : inst_ver_cmp(v, group_version(g));
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
            if (groupVerdict[g] != INST_GROUP_OK && d->Action != INST_ACT_FOREIGN &&
                d->Action != INST_ACT_WINDOWS) {
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
    if (infText[INST_INF_CM] != NULL) {
        /* Read again only while the connection manager's INF is in the
         * run; the name found stays for the unticked box's note. */
        tbNhiName[0] = '\0';
    }
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
            if (!envNow.CmNhi) {
                inst_copy(tbNhiName, d->Desc[0] ? d->Desc : d->Id,
                          sizeof(tbNhiName));
            }
            envNow.CmNhi = 1;
        }
        memset(&dv, 0, sizeof(dv));
        dv.HasService = d->Service[0] != '\0';
        if (d->Inf == INST_INF_S98) {
            /* Stor98 installs only where no driver is (decision D3 as for
             * the package's own devices): on 98 SE and ME a device loader
             * of any kind is a driver; a device whose drivers are not
             * installed (problem 28) has none, whatever it names. */
            if (!envNow.Nt && !dv.HasService && d->Driver[0] != '\0' &&
                s98_loader_9x(d)) {
                dv.HasService = 1;
            }
            if (d->Problem == 28UL && !inst_eq(d->Service, "stor98")) {
                dv.HasService = 0;
            }
        }
        /* Windows 8 and 8.1, the box ticked (record 23 decision D3 as
         * revision 26 amends it, record 26 section 5.3.1 cases 3a and
         * 3b): Windows' own in-box USBXHCI on a controller, and stornvme
         * on a drive, are installed over; every other driver stays. */
        if (osKind == INST_OS_CMONLY && dv.HasService &&
            ((d->Inf == INST_INF_XHCI && inst_eq(d->Service, "USBXHCI")) ||
             (d->Inf == INST_INF_S98 && inst_eq(d->Service, "stornvme")))) {
            dv.HasService = 0;
        }
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
        d->Action = inst_action(&dv, inf_version(d->Inf));
        if (d->Inf == INST_INF_UAS) {
            d->Action = inst_uas_nt6_action(osKind, d->Action);
        }
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
    s98_places();
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
        out("%sCode 2: USBNTMAP.SYS, one of the USB storage drivers, is "
            "missing.\n    Install them (this installer's row for them, or "
            "NUSB's mass-storage\n    component); the disk starts once "
            "Windows is shut down and started again.\n", lead);
    }
}

static void s98_share_note(int first);

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
        if (d->Inf == INST_INF_S98 && d->Present &&
            d->S98Why != INST_S98_ELIGIBLE) {
            out("    LEFT: %s\n", inst_s98_skip_text(d->S98Why, 0));
        }
    }
    s98_share_note(1);
    s98Shown = 1;
    if (!any) {
        out("  none present. The INFs are still staged for devices that "
            "appear later.\n");
        if (!envNow.Nt && !controller_listed()) {
            out("  No USB 3 controller is listed by Windows now. If its New "
                "Hardware wizard was\n  cancelled at this start, go on: after "
                "the restart the wizard comes back;\n  finish it (Next until "
                "Finish; where it asks for the xHCI98 driver disk,\n  type the "
                "folder the tools go to, and where it asks for a Windows "
                "file,\n  the Windows CD or the folder of its setup files), "
                "then run the installer\n  again.\n");
        }
    }
    if (!usbInclude) {
        for (i = 0; i < devCount; i++) {
            if (inst_eq(devs[i].Service, "xhci98") ||
                inst_eq(devs[i].Service, "xhciuas")) {
                out("  The xHCI98 USB drivers already installed stay as they "
                    "are (INSTALL.BAT\n  /uninstall removes the whole "
                    "package).\n");
                break;
            }
        }
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

/* 98 SE and ME: 1 when the devnode's driver key names a device loader
 * (DevLoader) or an IOS port driver (PortDriver): a driver, whoever made
 * it. */
static int s98_loader_9x(const DEVREC *d)
{
    char path[INST_PATH_CHARS];
    char text[64];

    driver_key_path(d, path, sizeof(path));
    return reg_text3(HKEY_LOCAL_MACHINE, path, "PortDriver", text,
                     sizeof(text)) != 0 ||
           reg_text3(HKEY_LOCAL_MACHINE, path, "DevLoader", text,
                     sizeof(text)) != 0;
}

/* The bundled Stor98 (record 26 section 6.3): its INF and the files it
 * names for this system, present, non-empty, of this architecture, at the
 * INF's DriverVer. Its absence is no error (a package may bundle none);
 * a part of it, or one that fails a check, refuses. */
static int s98_check_package(void)
{
    char path[INST_PATH_CHARS];
    char name[INST_PATH_CHARS];
    char *buf;
    DWORD size;
    INST_U32 machine;
    INST_U32 fv[4];
    size_t n;
    int i;
    int r;
    int saved;

    haveS98 = 0;
    free(s98Text);
    s98Text = NULL;
    join(path, sizeof(path), pkgDir, "stor98.inf");
    if (!file_exists(path, NULL)) {
        return 1;
    }
    s98Text = read_file(path, NULL);
    if (s98Text == NULL) {
        return refuse("a file of the package is unreadable: ", path);
    }
    if (!inst_inf_driverver(s98Text, s98Version) ||
        !inst_inf_arch_ok(s98Text, INST_MY_ARCH)) {
        return refuse("the bundled stor98.inf has no DriverVer, or is not "
                      "this architecture's.", NULL);
    }
    _snprintf(s98VersionText, sizeof(s98VersionText) - 1, "%lu.%lu.%lu.%lu",
              (unsigned long)s98Version[0], (unsigned long)s98Version[1],
              (unsigned long)s98Version[2], (unsigned long)s98Version[3]);
    s98VersionText[sizeof(s98VersionText) - 1] = '\0';
    /* The files its INF names, read through the common source walk. */
    inst_copy(infRel[INST_INF_S98], "stor98.inf", sizeof(infRel[0]));
    saved = 0;
    infText[INST_INF_S98] = s98Text;
    for (i = 0; (r = inf_source(INST_INF_S98, i, pkgDir, path, sizeof(path),
                                name, sizeof(name))) != 0; i++) {
        if (r < 0) {
            infText[INST_INF_S98] = NULL;
            return refuse("a file stor98.inf names lies outside the package: ",
                          name);
        }
        n = strlen(name);
        if (!file_exists(path, &size) || size == 0) {
            infText[INST_INF_S98] = NULL;
            return refuse("a file stor98.inf names is missing or empty: ",
                          path);
        }
        if (n > 4 && inst_eq(name + n - 4, ".sys")) {
            buf = read_file(path, &size);
            if (buf == NULL ||
                !inst_pe_machine((const unsigned char *)buf, size, &machine) ||
                machine != INST_PE_MINE) {
                free(buf);
                infText[INST_INF_S98] = NULL;
                return refuse("the bundled Stor98 driver is not this "
                              "architecture's: ", path);
            }
            free(buf);
        }
        if ((n > 4 && inst_eq(name + n - 4, ".sys")) ||
            (n > 4 && inst_eq(name + n - 4, ".mpd"))) {
            if (file_version(path, fv) <= 0 ||
                inst_ver_cmp(fv, s98Version) != 0) {
                infText[INST_INF_S98] = NULL;
                return refuse("a file of the bundled Stor98 is not its INF's "
                              "version: ", path);
            }
            saved++;
        }
    }
    infText[INST_INF_S98] = NULL;
    if (saved == 0) {
        return refuse("the bundled stor98.inf names no driver.", NULL);
    }
    haveS98 = 1;
    logonly("stor98: bundled %s\n", s98VersionText);
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
        /* The window takes no /dir: its own architecture's release- (or,
         * with /debug, debug-) directory (record 26 section 2). */
        inst_copy(opts.Dir, opts.Debug ? "debug" INST_DIR_SUFFIX
                                       : INST_DEFAULT_DIR, sizeof(opts.Dir));
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
    if (!s98_check_package()) {
        return 0;
    }

    cmFiles = 0;
    for (k = 0; k < INST_INF_COUNT; k++) {
        inst_copy(infRel[k], instInfFile[k], sizeof(infRel[k]));
        if (k == INST_INF_S98) {
            /* Checked by s98_check_package; in the run only when ticked. */
            infText[k] = NULL;
            continue;
        }
        if (k == INST_INF_CMF) {
            /* nt\ in the x86 package, the flavour directory in the x64
             * one (inst_cmf_rel). */
            join(path, sizeof(path), pkgDir, inst_cmf_rel(INST_MY_ARCH));
            if (file_exists(path, NULL)) {
                inst_copy(infRel[k], inst_cmf_rel(INST_MY_ARCH),
                          sizeof(infRel[k]));
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
                if (inst_eq(name, "xhcicm.sys") && k == INST_INF_CM) {
                    pkgCmSigned = inst_pe_signed((const unsigned char *)buf,
                                                 size);
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

    if (stScreens) {
        /* The self-test's devices: the first holds XhciInterruptMode 2. */
        if (d == &devs[0] && inst_eq(name, "XhciInterruptMode")) {
            *value = 2;
            return 1;
        }
        return d->Driver[0] != '\0' ? 0 : -1;
    }
    if (d->Driver[0] == '\0' || !inst_eq(d->Service, instInfService[d->Inf])) {
        return -1;
    }
    driver_key_path(d, path, sizeof(path));
    return reg_dword(HKEY_LOCAL_MACHINE, path, name, value) ? 1 : 0;
}

#define INST_MAX_NOW 16

static const char *value_inf(int v);

/* The machine-wide value (record 26 section 5.4; task 42-A.17, owner
 * 2026-10-18): HKLM\System\CurrentControlSet\Services\USB, which both
 * drivers read before a controller's own key, by their rule
 * (inst_mw_reading): present in any type wins, a zero-length REG_NONE is
 * absent. 1 set (*value the number in force), 0 not set (the key or the
 * value absent), -1 unreadable or not a DWORD: it wins with the driver's
 * own fallback, which the installer does not know, so it is shown as
 * unreadable and a value chosen for it is always written over it. */
static int mw_value(int v, DWORD *value)
{
    HKEY k;
    DWORD t = REG_NONE;
    BYTE data[64];
    DWORD n = sizeof(data);
    INST_U32 in;
    int got;
    LONG r;

    *value = 0;
    if (stScreens) {
        return 0;
    }
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_MW_KEY, 0, KEY_QUERY_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    memset(data, 0, sizeof(data));
    r = RegQueryValueExA(k, instValues[v].Name, NULL, &t, data, &n);
    RegCloseKey(k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r == ERROR_MORE_DATA) {
        n = sizeof(data) + 1;   /* present, and longer than any DWORD */
    } else if (r != ERROR_SUCCESS) {
        return -1;
    }
    got = inst_mw_reading(1, (INST_U32)t, (INST_U32)n, inst_get32(data, 0), 0,
                          &in);
    if (got == 0) {
        return 0;
    }
    if (got == 2) {
        logonly("machine-wide %s is not a DWORD (type %lu, %lu bytes): it "
                "wins with the driver's own fallback\n", instValues[v].Name,
                (unsigned long)t, (unsigned long)n);
        return -1;
    }
    *value = (DWORD)in;
    return 1;
}

/* The INF a value's default comes from, NULL when that INF is not in this
 * run (the Thunderbolt support left out). */
static const char *value_inf(int v)
{
    return infText[instValues[v].Inf];
}

/* The devices value v applies to, with what each holds now (up to cap of
 * them in vnow, *nNow of them), and whether a new install of its driver
 * is among them. Returns the number of targets. */
static int value_targets(int v, INST_VNOW *vnow, int cap, int *nNow,
                         int *cmNew)
{
    int i;
    int targets = 0;

    *nNow = 0;
    *cmNew = 0;
    for (i = 0; i < devCount; i++) {
        DWORD now;
        int r;
        if (!value_target(&devs[i], v)) {
            continue;
        }
        targets++;
        if (devs[i].Action == INST_ACT_INSTALL) {
            *cmNew = 1;
        }
        if (*nNow >= cap) {
            continue;
        }
        r = value_now(&devs[i], instValues[v].Name, &now);
        vnow[*nNow].State = r > 0 ? 1 : r;
        vnow[*nNow].Value = r > 0 ? (INST_U32)now : 0;
        vnow[*nNow].Desc = devs[i].Desc[0] ? devs[i].Desc : devs[i].Id;
        (*nNow)++;
    }
    return targets;
}

/* The window's settings pages (record 26 section 5.4): a value chosen
 * there is answered as /set answers it, and written only where it differs
 * from the value in force. */
static int chosenSet[INST_MAX_VALUES];
static INST_U32 chosenValue[INST_MAX_VALUES];

void inst_eng_choose_value(int value, int set, INST_U32 v)
{
    if (value < 0 || value >= instValueCount || value >= INST_MAX_VALUES) {
        return;
    }
    /* A greyed row's value is for another system: never written. */
    chosenSet[value] = set && !inst_value_greyed(value, &envNow) ? 1 : 0;
    chosenValue[value] = v;
}

/* The caution of a value set without its question, when it is about the
 * value set. */
static void set_caution(int v, INST_U32 value)
{
    char text[1024];

    if (instValues[v].Caution == NULL || value > 31UL ||
        (instValues[v].CautionMask != 0 &&
         (instValues[v].CautionMask & (1UL << value)) == 0)) {
        return;
    }
    inst_caution_text(v, 1UL << value, text, sizeof(text));
    out("  NOTE: %s\n", text);
}

/* Ask, or take /set, the window's choices and /defaults (record 23
 * section 8.3; record 26 section 5.4: the machine-wide value shown beside
 * each device's own, and the one written). */
static void ask_values(void)
{
    int v;
    int k;
    char line[64];
    char mask[64];
    char question[8192];
    INST_VNOW vnow[INST_MAX_NOW];
    int nNow;
    int cmNew;
    INST_U32 enterValue;
    int same;
    int mw;
    DWORD mwv;

    memset(answerSet, 0, sizeof(answerSet));
    memset(answerChosen, 0, sizeof(answerChosen));
    for (k = 0; k < opts.SetCount; k++) {
        answerSet[opts.SetIndex[k]] = 1;
        answerValue[opts.SetIndex[k]] = opts.SetValue[k];
    }
    for (v = 0; v < instValueCount && v < INST_MAX_VALUES; v++) {
        if (chosenSet[v] && !answerSet[v]) {
            answerSet[v] = 1;
            answerChosen[v] = 1;
            answerValue[v] = chosenValue[v];
        }
    }

    for (v = 0; v < instValueCount && v < INST_MAX_VALUES; v++) {
        INST_U32 m = inst_value_asked_mask(v, &envNow);
        INST_U32 def;
        int targets;
        int haveDef;
        if (value_inf(v) == NULL) {
            continue;
        }
        haveDef = inst_value_default(v, value_inf(v), &def);
        targets = value_targets(v, vnow, INST_MAX_NOW, &nNow, &cmNew);
        if (answerSet[v]) {
            out("%s: set to %lu %s%s.\n", instValues[v].Name,
                (unsigned long)answerValue[v],
                answerChosen[v] ? "on the settings page" : "by /set",
                targets ? "" : " (machine-wide: no device takes it now)");
            set_caution(v, answerValue[v]);
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
        /* Owner, 2026-10-16: each choice on its own line with its meaning,
         * and "Enter for [x]", x what Enter keeps (inst_value_question). */
        mw = mw_value(v, &mwv);
        same = inst_value_question_mw(question, sizeof(question), v, m, def,
                                      vnow, nNow, mw, (INST_U32)mwv, cmNew,
                                      opts.List, &enterValue);
        if (opts.List) {
            out("%s\n", question);
            continue;
        }
        inst_mask_text(m, mask, sizeof(mask));
        for (k = 0;; k++) {
            INST_U32 a;
            int r;
            char again[128];
            if (k == 0) {
                if (!ask(INST_Q_VALUE, question, 0, v, 0, NULL, line,
                         sizeof(line))) {
                    break;
                }
            } else {
                if (same) {
                    _snprintf(again, sizeof(again) - 1, "  %s is not one of: "
                              "%s\n  Enter for [%lu]: ", line, mask,
                              (unsigned long)enterValue);
                } else {
                    _snprintf(again, sizeof(again) - 1, "  %s is not one of: "
                              "%s\n  Enter for [each keeps its own]: ", line,
                              mask);
                }
                again[sizeof(again) - 1] = '\0';
                if (!ask(INST_Q_VALUE, again, 1, v, 0, NULL, line,
                         sizeof(line))) {
                    break;
                }
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
        }
        out("\n");
    }
}

/* The window's rows (record 26 sections 5.4 and 5.5): every value of the
 * table that concerns this machine, asked or behind Advanced, with what
 * is in force. */
int inst_eng_rows(INST_ROW *rows, int cap)
{
    int v;
    int n = 0;

    for (v = 0; v < instValueCount && v < INST_MAX_VALUES && n < cap; v++) {
        INST_ROW *r = &rows[n];
        INST_VNOW vnow[INST_MAX_NOW];
        int nNow;
        int cmNew;
        int targets;
        int i;
        DWORD mwv = 0;
        int asked;
        int cmRow;
        int greyed;
        if (value_inf(v) == NULL ||
            !inst_value_default(v, value_inf(v), &r->Def)) {
            continue;
        }
        /* A Thunderbolt value with no controller present is still shown
         * when the support is ticked, and written machine-wide for the
         * controller to come (record 26 section 5.5). */
        /* A value of another system is shown greyed where it would be
         * asked or behind Advanced there (owner, 2026-10-19). */
        greyed = inst_value_greyed(v, &envNow);
        cmRow = instValues[v].Inf == INST_INF_CM &&
                (instValues[v].Apply == INST_APPLY_CM ||
                 instValues[v].Apply == INST_APPLY_9X_CM);
        asked = inst_value_applies(v, &envNow) ||
                (cmRow && !greyed && instValues[v].Asked != 0) ||
                (greyed && instValues[v].Asked != 0);
        if (!asked && !cmRow && !greyed && !inst_value_relevant(v, &envNow)) {
            continue;
        }
        targets = value_targets(v, vnow, INST_MAX_NOW, &nNow, &cmNew);
        if (targets == 0 && !cmRow && !greyed) {
            continue;
        }
        r->Value = v;
        r->Asked = asked;
        r->Greyed = greyed;
        /* Every value /set may give, so a setting in force is never
         * shown as another (its caution is in the row's note); a greyed
         * row lists its own system's. */
        r->Mask = greyed ? instValues[v].Settable
                         : inst_value_set_mask(v, &envNow);
        r->MwState = mw_value(v, &mwv);
        r->MwValue = (INST_U32)mwv;
        r->CmNew = cmNew;
        r->NowCount = nNow < INST_ROW_NOW_MAX ? nNow : INST_ROW_NOW_MAX;
        for (i = 0; i < r->NowCount; i++) {
            r->Now[i] = vnow[i];
        }
        if (r->MwState == 1) {
            r->Same = 1;
            r->InForce = r->MwValue;
        } else {
            r->Same = 1;
            r->InForce = r->Def;
            for (i = 0; i < nNow; i++) {
                INST_U32 eff = vnow[i].State > 0 ? vnow[i].Value : r->Def;
                if (i == 0) {
                    r->InForce = eff;
                } else if (eff != r->InForce) {
                    r->Same = 0;
                }
            }
            /* A machine-wide value that is not a number wins with a
             * fallback not known here: no value is shown as in force, the
             * row starts in the mixed state (writing nothing) and any
             * value chosen is written over it. */
            if (r->MwState == -1) {
                r->Same = 0;
            }
        }
        n++;
    }
    return n;
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

/* What an answered value writes (record 26 section 5.4): nothing when
 * every device runs with the answer already, else the machine-wide value.
 * 1 a write is needed. */
static int value_needs_write(int v)
{
    INST_VNOW vnow[INST_MAX_NOW];
    int nNow;
    int cmNew;
    INST_U32 def = 0;
    DWORD mwv = 0;
    int mw;

    if (!answerSet[v] ||
        (envNow.Nt && (instValues[v].Apply == INST_APPLY_9X_XHCI ||
                       instValues[v].Apply == INST_APPLY_9X_CM)) ||
        value_inf(v) == NULL) {
        return 0;
    }

    inst_value_default(v, value_inf(v), &def);
    value_targets(v, vnow, INST_MAX_NOW, &nNow, &cmNew);
    mw = mw_value(v, &mwv);
    if (mw < 0) {
        return 1;   /* a value there that is not a number wins: replaced */
    }
    return inst_mw_write_needed(mw > 0 ? 1 : 0, (INST_U32)mwv, vnow, nNow,
                                def, answerValue[v]);
}

static void write_values(void)
{
    int v;
    HKEY k;
    DWORD val;
    DWORD old;
    DWORD disp = 0;
    int had;
    LONG w;

    /* The machine-wide key made where it is absent (task 42-A.17: 2000
     * has none until something makes it), so a value can be set there by
     * hand or by the helpers. */
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_MW_KEY, 0, NULL, 0,
                        KEY_QUERY_VALUE, NULL, &k, &disp) == ERROR_SUCCESS) {
        RegCloseKey(k);
        if (disp == REG_CREATED_NEW_KEY) {
            logonly("made HKLM\\%s\n", INST_MW_KEY);
            flagChanged = 1;
        }
    } else {
        out("The machine-wide key HKLM\\%s could not be made.\n",
            INST_MW_KEY);
        flagFailed = 1;
    }
    for (v = 0; v < instValueCount && v < INST_MAX_VALUES; v++) {
        if (!answerSet[v] || value_inf(v) == NULL) {
            continue;
        }
        /* The silent install on: the list first - merged and verified once,
         * even where the switch already reads 1, as silent-on.bat does -
         * then the switch; off is the switch alone, the list left, as
         * silent-off.bat. Never on NT (/set refuses it). */
        if (inst_eq(instValues[v].Name, "XhciSilentInstall")) {
            if (envNow.Nt) {
                continue;
            }
            if (answerValue[v] == 1 && !silent_list_merge()) {
                flagFailed = 1;
                continue;
            }
        }
        if (envNow.Nt && instValues[v].Apply == INST_APPLY_9X_CM) {
            continue;
        }
        if (!value_needs_write(v)) {
            out("%s stays %lu: every device runs with it already.\n",
                instValues[v].Name, (unsigned long)answerValue[v]);
            continue;
        }
        had = mw_value(v, &old) > 0;
        val = (DWORD)answerValue[v];
        if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_MW_KEY, 0, NULL, 0,
                            KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) {
            out("%s could not be written (the key HKLM\\%s did not open).\n",
                instValues[v].Name, INST_MW_KEY);
            flagFailed = 1;
            continue;
        }
        w = RegSetValueExA(k, instValues[v].Name, 0, REG_DWORD,
                           (const BYTE *)&val, sizeof(val));
        RegCloseKey(k);
        if (w != ERROR_SUCCESS) {
            out("%s could not be written (error %ld).\n", instValues[v].Name,
                (long)w);
            flagFailed = 1;
            continue;
        }
        flagChanged = 1;
        step(INST_STEP_DONE, "%s set to %lu", instValues[v].Name,
             (unsigned long)val);
        if (had) {
            out("%s set from %lu to %lu machine-wide (every controller uses "
                "it).\n", instValues[v].Name, (unsigned long)old,
                (unsigned long)val);
        } else {
            out("%s set to %lu machine-wide (every controller uses it).\n",
                instValues[v].Name, (unsigned long)val);
        }
        logonly("  wrote HKLM\\%s\\%s = %lu (was %s%lu)\n", INST_MW_KEY,
                instValues[v].Name, (unsigned long)val, had ? "" : "absent ",
                (unsigned long)(had ? old : 0));
        if (inst_eq(instValues[v].Name, "XhciForceBulkOnly")) {
            out("  A disk takes it when it is next connected.\n");
        } else {
            out("  It takes effect when the driver next starts (restart "
                "Windows, or disable and enable the device).\n");
            flagRestart = 1;
        }
    }
}

/* How many values write_values would write now: the confirmation's
 * wording. */
static int values_to_write(void)
{
    int v;
    int n = 0;

    for (v = 0; v < instValueCount && v < INST_MAX_VALUES; v++) {
        n += value_needs_write(v);
    }
    return n;
}

/* ======================================================================== */
/* NT: staging, the trap, the installs (record 23 sections 5.5 to 5.9)       */
/* ======================================================================== */

/* %windir%\XHCI98: where the earlier builds put everything, and where the
 * NT route still keeps its local copies (record 23 sections 4.4, 9.6). */
static int legacy_dir(char *dir, size_t cap)
{
    char win[INST_PATH_CHARS];

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    join(dir, cap, win, "XHCI98");
    return 1;
}

/* The install directory (record 23 section 9.6; owner, 2026-10-16): the
 * xHCI98 tools, and on 98 SE and ME the local copies DevicePath names.
 * toolDir is this run's, resolved by choose_dir (an install) or
 * earlier_dir (an uninstall); oldToolDir is where an earlier install put
 * them, "" for none. */
static char toolDir[INST_PATH_CHARS];
static char oldToolDir[INST_PATH_CHARS];
static int movePending;     /* oldToolDir is an unfinished move's (MoveFrom) */
static char seenRecord[INST_PATH_CHARS];    /* the records as the question */
static char seenMoveFrom[INST_PATH_CHARS];  /* saw them (begin_move)       */
static int seenValid;       /* an install run that read them */
static int records_unchanged(void);

/* The local copies (record 23 section 4.4): <8 hex digits>, one immutable
 * directory per package content, each with an XHCIINST.LST naming the
 * files the tool put there. On NT they stay in %windir%\XHCI98, where the
 * staged INFs of 2000 and XP name them as their source; on 98 SE and ME
 * they are in the install directory, which is what is typed into the
 * hardware wizards. */
static int local_dir(char *dir, size_t cap)
{
    if (!envNow.Nt && toolDir[0] != '\0') {
        inst_copy(dir, toolDir, cap);
        return 1;
    }
    return legacy_dir(dir, cap);
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

/* MoveFileExA, looked up at run time like every other NT-only call here:
 * a static import would load on 98 SE and ME only because their KERNEL32
 * happens to export a failing stub, and the import check reads module
 * names, not functions. NULL when the system has none. */
static PFN_MOVEEX move_ex(void)
{
    return (PFN_MOVEEX)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                      "MoveFileExA");
}

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
            "Tree for a\nminute: it is left as it is; run INSTALL.BAT again "
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

static int tray_paths_in(const char *dir, TRAY_PATHS *t)
{
    size_t n;

    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }
    inst_copy(t->Dir, dir, sizeof(t->Dir));
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

/* The tool in this run's install directory. */
static int tray_paths(TRAY_PATHS *t)
{
    return tray_paths_in(toolDir, t);
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
        "the xHCI98\nUSB Tree (or restart Windows) and run INSTALL again to "
        "update it.\n");
    return INST_TRAY_PUT_IN_USE;

failed:
    out("The xHCI98 USB Tree could not be put at %s (error %lu).\n", t->Exe,
        (unsigned long)GetLastError());
    delete_file(t->Tmp);
    return INST_TRAY_PUT_FAILED;
}

/* ---- the install directory (record 23 section 9.6) ---------------------- */

static int package_tag(INST_U32 *tag);
static void local_copy_name(INST_U32 tag, char *name, size_t cap);
static void local_remove(const char *dir);
static int flat_remove(const char *dir);
static void movefrom_get(char *out, size_t cap);

/* Every missing directory of path made, from the top down. 1 when path is a
 * directory afterwards. */
static int make_dirs(const char *path)
{
    char p[INST_PATH_CHARS];
    size_t i;
    DWORD a;

    inst_copy(p, path, sizeof(p));
    for (i = 3; p[0] != '\0' && i < strlen(p); i++) {
        if (p[i] == '\\') {
            p[i] = '\0';
            CreateDirectoryA(p, NULL);
            p[i] = '\\';
        }
    }
    CreateDirectoryA(p, NULL);
    a = GetFileAttributesA(p);
    return a != 0xFFFFFFFFUL && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/* What the system says of a directory whose text inst_dir_check accepted:
 * NULL when it can be used (a local hard disk, and the path a directory or
 * absent below a directory), else why not. */
static const char *dir_usable(const char *path)
{
    char p[INST_PATH_CHARS];
    char drive[4];
    char *slash;
    DWORD a;

    drive[0] = path[0];
    drive[1] = ':';
    drive[2] = '\\';
    drive[3] = '\0';
    if (GetDriveTypeA(drive) != DRIVE_FIXED) {
        return "not a local hard disk";
    }
    inst_copy(p, path, sizeof(p));
    for (;;) {
        a = GetFileAttributesA(p);
        if (a != 0xFFFFFFFFUL) {
            return (a & FILE_ATTRIBUTE_DIRECTORY) ? NULL
                                                  : "a file of that name is in the way";
        }
        slash = strrchr(p, '\\');
        if (slash == NULL || slash <= p + 2) {
            return NULL;
        }
        *slash = '\0';
    }
}

/* The directory an earlier run recorded, checked as text, or "". */
static void recorded_dir(char *out, size_t cap)
{
    char v[INST_PATH_CHARS];
    char win[INST_PATH_CHARS];
    char ok[INST_PATH_CHARS];

    out[0] = '\0';
    if (reg_text3(HKEY_LOCAL_MACHINE, INST_DIR_KEY, INST_DIR_VALUE, v,
                  sizeof(v)) != 1) {
        return;
    }
    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        win[0] = '\0';
    }
    /* The 8.3 rule is the question's: a recorded value is taken as it is. */
    if (inst_dir_check(v, 1, win, NULL, v, ok, sizeof(ok)) == INST_DIR_OK) {
        inst_copy(out, ok, cap);
    } else {
        logonly("recorded install directory unusable: %s\n", v);
    }
}

/* An earlier build's install, which recorded no directory: its tool in
 * %windir%\XHCI98, or on 98 SE and ME a package copy there (a <hash>
 * directory with its list, or the list in the directory itself). */
static int legacy_install(void)
{
    char dir[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char lst[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int found = 0;

    if (!legacy_dir(dir, sizeof(dir))) {
        return 0;
    }
    join(path, sizeof(path), dir, inst_tray_file(INST_MY_ARCH));
    if (file_exists(path, NULL)) {
        return 1;
    }
    if (envNow.Nt) {
        return 0;   /* NT's copies are kept there whatever the directory */
    }
    join(path, sizeof(path), dir, "XHCIINST.LST");
    if (file_exists(path, NULL)) {
        return 1;
    }
    join(path, sizeof(path), dir, "*.*");
    h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            inst_copy_name(fd.cFileName)) {
            join(path, sizeof(path), dir, fd.cFileName);
            join(lst, sizeof(lst), path, "XHCIINST.LST");
            if (file_exists(lst, NULL)) {
                found = 1;
            }
        }
    } while (!found && FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

/* Where an earlier install put the tools: the recorded directory, else an
 * earlier build's %windir%\XHCI98, else "". */
static void earlier_dir(char *out, size_t cap)
{
    recorded_dir(out, cap);
    if (out[0] == '\0' && legacy_install()) {
        legacy_dir(out, cap);
    }
}

/* 1 when a and b name the same directory: the same text, or, where both
 * exist, the same short path (C:\PROGRA~1\X and C:\Program Files\X). */
static int same_dir(const char *a, const char *b)
{
    char sa[INST_PATH_CHARS];
    char sb[INST_PATH_CHARS];
    char pa[INST_PATH_CHARS];
    char pb[INST_PATH_CHARS];
    char mark[32];
    char got[32];
    DWORD na;
    DWORD nb;
    DWORD n;
    HANDLE h;
    int same = 0;

    if (inst_eq(a, b)) {
        return 1;
    }
    na = GetShortPathNameA(a, sa, sizeof(sa));
    nb = GetShortPathNameA(b, sb, sizeof(sb));
    if (na != 0 && na < sizeof(sa) && nb != 0 && nb < sizeof(sb) &&
        inst_eq(sa, sb)) {
        return 1;
    }
    /* Both exist: a file made in one is looked for in the other, which
     * also sees through a junction or a substituted drive. */
    if (na == 0 || nb == 0 || opts.List) {
        return 0;   /* /list writes nothing, not even a probe */
    }
    /* A name of this run's own, made new: no file of the user's is ever
     * opened, truncated or deleted by it. */
    _snprintf(mark, sizeof(mark) - 1, "XI%06lX.TMP",
              (unsigned long)((GetTickCount() ^ GetCurrentProcessId()) &
                              0xFFFFFFUL));
    mark[sizeof(mark) - 1] = '\0';
    join(pa, sizeof(pa), a, mark);
    join(pb, sizeof(pb), b, mark);
    h = CreateFileA(pa, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    WriteFile(h, mark, (DWORD)strlen(mark), &n, NULL);
    CloseHandle(h);
    h = CreateFileA(pb, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        memset(got, 0, sizeof(got));
        if (ReadFile(h, got, sizeof(got) - 1, &n, NULL) &&
            strcmp(got, mark) == 0) {
            same = 1;
        }
        CloseHandle(h);
    }
    DeleteFileA(pa);
    return same;
}

/* path with its longest existing part in its short form, the rest as it
 * is: two spellings of one place compare alike. */
static void short_form(const char *path, char *out, size_t cap)
{
    char p[INST_PATH_CHARS];
    char s[INST_PATH_CHARS];
    char *slash;
    DWORD n;

    inst_copy(p, path, sizeof(p));
    for (;;) {
        n = GetShortPathNameA(p, s, sizeof(s));
        if (n != 0 && n < sizeof(s)) {
            inst_copy(out, s, cap);
            strncat(out, path + strlen(p), cap - strlen(out) - 1);
            return;
        }
        slash = strrchr(p, '\\');
        if (slash == NULL || slash <= p + 2) {
            inst_copy(out, path, cap);
            return;
        }
        *slash = '\0';
    }
}

static int moving(void)
{
    return oldToolDir[0] != '\0' && toolDir[0] != '\0' &&
           !same_dir(oldToolDir, toolDir);
}

/* Why a directory cannot be the install directory, or NULL: the text's
 * checks, then the same containment checks on the short forms (a package
 * under C:\LongName reached as C:\LONGNA~1 is still the package), not
 * inside or around the directory the tools are in now (a move between
 * them would clean one into the other), and the system's. */
static const char *dir_why(const char *path, const char *win, char *checked,
                           size_t cap)
{
    char sp[INST_PATH_CHARS];
    char sw[INST_PATH_CHARS];
    char sr[INST_PATH_CHARS];
    char so[INST_PATH_CHARS];
    int r = inst_dir_check(path, envNow.Nt, win, root, oldToolDir, checked,
                           cap);

    if (r != INST_DIR_OK) {
        return inst_dir_reason(r);
    }
    short_form(checked, sp, sizeof(sp));
    short_form(win, sw, sizeof(sw));
    short_form(root, sr, sizeof(sr));
    if (win[0] != '\0' && inst_path_within(sp, sw) &&
        !(oldToolDir[0] != '\0' && same_dir(checked, oldToolDir))) {
        return inst_dir_reason(INST_DIR_WINDOWS);
    }
    if (root[0] != '\0' && inst_path_within(sp, sr)) {
        return inst_dir_reason(INST_DIR_PACKAGE);
    }
    if (oldToolDir[0] != '\0' && !same_dir(checked, oldToolDir)) {
        short_form(oldToolDir, so, sizeof(so));
        if (inst_path_within(sp, so) || inst_path_within(so, sp)) {
            return "not inside, or around, the directory the tools are in "
                   "now";
        }
    }
    return dir_usable(checked);
}

/* Record 23 section 9.6: the install directory - given with /to, or asked
 * (Enter: where an earlier install put the tools, else C:\XHCI98 on the
 * system's drive), or under /yes and /defaults that default unasked. NT
 * asks only when the package carries the USB tree tool, the one thing it
 * puts there. The default is checked as a typed answer is. 1, or 0
 * refused (said). */
static int choose_dir(void)
{
    char win[INST_PATH_CHARS];
    char sysDrive[16];
    char def[INST_PATH_CHARS];
    char defChecked[INST_PATH_CHARS];
    char checked[INST_PATH_CHARS];
    char line[INST_PATH_CHARS];
    char from[INST_PATH_CHARS];
    char moveFrom[INST_PATH_CHARS];
    char text[2048];
    const char *why;
    const char *defWhy;
    int toolHere;
    int setupHere;
    int k;

    toolDir[0] = '\0';
    moveFrom[0] = '\0';
    join(from, sizeof(from), root, "tools\\xhcitr");
    join(from, sizeof(from), from, inst_tray_file(INST_MY_ARCH));
    toolHere = file_exists(from, NULL);
    /* The installer's own files go there too (record 26 section 6.2): a
     * directory is chosen whenever the package carries them. */
    join(from, sizeof(from), root, "tools\\xhciinst");
    join(from, sizeof(from), from, inst_self_file(INST_MY_ARCH,
                                                  INST_OWN_WINDOW));
    setupHere = file_exists(from, NULL);
    if (stScreens) {
        toolHere = 1;   /* the self-test sets oldToolDir itself */
        setupHere = 1;
    } else {
        earlier_dir(oldToolDir, sizeof(oldToolDir));
        movefrom_get(moveFrom, sizeof(moveFrom));
        recorded_dir(seenRecord, sizeof(seenRecord));
        inst_copy(seenMoveFrom, moveFrom, sizeof(seenMoveFrom));
        seenValid = 1;
    }
    if (envNow.Nt && !toolHere && !setupHere) {
        return 1;
    }
    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        win[0] = '\0';
    }
    sysDrive[0] = '\0';
    if (envNow.Nt) {
        DWORD n = GetEnvironmentVariableA("SystemDrive", sysDrive,
                                          sizeof(sysDrive));
        if (n == 0 || n >= sizeof(sysDrive)) {
            sysDrive[0] = '\0';
        }
    }
    if (oldToolDir[0] != '\0') {
        inst_copy(def, oldToolDir, sizeof(def));
    } else {
        inst_dir_default(sysDrive, win, def, sizeof(def));
    }
    defWhy = dir_why(def, win, defChecked, sizeof(defChecked));
    logonly("install directory: earlier \"%s\", default %s (%s), move from "
            "\"%s\"\n", oldToolDir, def, defWhy != NULL ? defWhy : "usable",
            moveFrom);
    if (opts.To[0] != '\0') {
        why = dir_why(opts.To, win, checked, sizeof(checked));
        if (why != NULL) {
            out("REFUSED: /to %s:\n  %s.\n", opts.To, why);
            return 0;
        }
        inst_copy(toolDir, checked, sizeof(toolDir));
        out("Install directory: %s (%s)\n", toolDir,
            instWindow ? "chosen on the Components page" : "/to");
    } else if (opts.Yes || opts.Defaults) {
        if (defWhy != NULL) {
            out("REFUSED: the install directory %s cannot be used:\n  %s.\n"
                "Name another with /to <path>.\n", def, defWhy);
            return 0;
        }
        inst_copy(toolDir, defChecked, sizeof(toolDir));
        out("Install directory: %s (%s)\n", toolDir,
            oldToolDir[0] != '\0' ? "where it is now" : "the default");
    } else {
        inst_dir_question(text, sizeof(text),
                          toolHere ? inst_tray_file(INST_MY_ARCH) : NULL,
                          setupHere ? inst_self_file(INST_MY_ARCH,
                                                     INST_OWN_WINDOW) : NULL,
                          envNow.Nt ? NULL : "*.INF, *.SYS",
                          oldToolDir, def, envNow.Nt, opts.List);
        if (opts.List) {
            out("%s", text);
            if (defWhy != NULL) {
                out("  (that one cannot be used: %s)\n", defWhy);
            }
            inst_copy(toolDir, def, sizeof(toolDir));
            out("\n");
            return 1;
        }
        for (k = 0;; k++) {
            if (!ask(INST_Q_DIR, text, k > 0, 0, 0, defChecked, line,
                     sizeof(line))) {
                if (defWhy != NULL) {
                    out("\nREFUSED: no answer, and %s cannot be used: %s.\n",
                        def, defWhy);
                    return 0;
                }
                inst_copy(toolDir, defChecked, sizeof(toolDir));
                out("\n");
                break;
            }
            if (line[strspn(line, " \t")] == '\0') {
                why = defWhy;
                inst_copy(checked, defChecked, sizeof(checked));
            } else {
                why = dir_why(line, win, checked, sizeof(checked));
            }
            if (why == NULL) {
                inst_copy(toolDir, checked, sizeof(toolDir));
                break;
            }
            _snprintf(text, sizeof(text) - 1, "  Not that one: %s.\n  Type "
                      "another%s: ", why,
                      defWhy == NULL ? ", or Enter for the one above" : "");
            text[sizeof(text) - 1] = '\0';
        }
        out("Install directory: %s\n", toolDir);
    }
    /* A move an earlier run could not finish is finished now; another
     * move waits until it is, so no directory is ever lost track of. */
    if (moveFrom[0] != '\0' && oldToolDir[0] != '\0' &&
        !same_dir(moveFrom, oldToolDir) && !same_dir(toolDir, oldToolDir)) {
        out("REFUSED: an earlier move from %s to %s is not finished. Run "
            "INSTALL.BAT once\nmore with %s first.\n", moveFrom, oldToolDir,
            oldToolDir);
        return 0;
    }
    movePending = 0;
    if (moveFrom[0] != '\0' && !same_dir(moveFrom, toolDir) &&
        (oldToolDir[0] == '\0' || same_dir(oldToolDir, toolDir))) {
        inst_copy(oldToolDir, moveFrom, sizeof(oldToolDir));
        movePending = 1;
    }
    if (moving()) {
        out("  The xHCI98 tools move there from %s.\n", oldToolDir);
    }
    out("\n");
    return 1;
}

/* The install directory recorded (HKLM\Software\xhci98, InstallDir), once
 * what goes there is in place. 1 recorded (or already), 0 failed (said). */
static int record_dir(void)
{
    char cur[INST_PATH_CHARS];
    HKEY k;
    LONG w;

    if (toolDir[0] == '\0' || opts.List) {
        return 1;
    }
    if (reg_text3(HKEY_LOCAL_MACHINE, INST_DIR_KEY, INST_DIR_VALUE, cur,
                  sizeof(cur)) == 1 && strcmp(cur, toolDir) == 0) {
        return 1;
    }
    if (!records_unchanged()) {
        return 0;
    }
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_DIR_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) {
        out("The install directory could not be recorded in the registry.\n");
        flagFailed = 1;
        return 0;
    }
    w = RegSetValueExA(k, INST_DIR_VALUE, 0, REG_SZ, (const BYTE *)toolDir,
                       (DWORD)strlen(toolDir) + 1);
    RegCloseKey(k);
    if (w != ERROR_SUCCESS) {
        out("The install directory could not be recorded in the registry "
            "(error %ld).\n", (long)w);
        flagFailed = 1;
        return 0;
    }
    flagChanged = 1;
    logonly("wrote HKLM\\%s\\%s = %s\n", INST_DIR_KEY, INST_DIR_VALUE, toolDir);
    recorded_dir(seenRecord, sizeof(seenRecord));
    return 1;
}

/* The uninstall's: the record deleted, and its key when nothing else is in
 * it (98 SE's RegDeleteKey would take the rest with it). */
/* 1 when the records are gone (or were never there); 0 said, failed. */
static int forget_dir(void)
{
    HKEY k;
    DWORD subKeys = 0;
    DWORD values = 0;
    LONG r;

    /* The Stor98 record goes too, also when Stor98 itself stays (record
     * 26 section 9.3 step 6, draft 16): nothing of this package is left
     * for it to describe. */
    if (!stor_forget()) {
        return 0;
    }
    if (!opts.List) {
        LONG w;
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_S98_KEY "\\Staged");
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_S98_KEY "\\Devices");
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_S98_KEY "\\Pending");
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_S98_KEY "\\Settings");
        w = RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_S98_KEY);
        if (w == ERROR_SUCCESS) {
            logonly("deleted HKLM\\%s\n", INST_S98_KEY);
        } else if (w != ERROR_FILE_NOT_FOUND) {
            out("Stor98's install record could not be removed (error %ld): "
                "delete HKLM\\%s\nwith regedit.\n", (long)w, INST_S98_KEY);
            flagFailed = 1;
            return 0;
        }
    }
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_DIR_KEY, 0,
                      KEY_SET_VALUE | KEY_QUERY_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 1;
    }
    if (r != ERROR_SUCCESS) {
        out("HKLM\\%s could not be opened (error %ld).\n", INST_DIR_KEY,
            (long)r);
        flagFailed = 1;
        return 0;
    }
    r = RegDeleteValueA(k, INST_DIR_MOVEFROM);
    if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
        out("The install directory's move record could not be deleted "
            "(error %ld).\n", (long)r);
        flagFailed = 1;
        RegCloseKey(k);
        return 0;
    }
    r = RegDeleteValueA(k, INST_DIR_VALUE);
    if (r == ERROR_SUCCESS) {
        flagChanged = 1;
        logonly("deleted HKLM\\%s\\%s\n", INST_DIR_KEY, INST_DIR_VALUE);
    } else if (r != ERROR_FILE_NOT_FOUND) {
        out("The install directory's record could not be deleted (error "
            "%ld).\n", (long)r);
        flagFailed = 1;
        RegCloseKey(k);
        return 0;
    }
    if (RegQueryInfoKeyA(k, NULL, NULL, NULL, &subKeys, NULL, NULL, &values,
                         NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
        subKeys = 1;
    }
    RegCloseKey(k);
    if (subKeys == 0 && values == 0 &&
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_DIR_KEY) == ERROR_SUCCESS) {
        logonly("deleted HKLM\\%s\n", INST_DIR_KEY);
    }
    return 1;
}



/* Before a move (record 23 section 9.6), before anything is copied: the
 * tool running from the old directory is closed. One that does not close
 * keeps everything where it is this time, with steps left, so the tools
 * and the copies are never split between two directories. */
static void move_ready(void)
{
    TRAY_PATHS o;
    int ok;

    if (opts.List || !moving() || !tray_paths_in(oldToolDir, &o) ||
        !file_exists(o.Exe, NULL)) {
        return;
    }
    ok = maint_lock();
    if (ok) {
        ok = tray_close(o.Exe);
        maint_unlock();
    }
    if (!ok) {
        out("The xHCI98 USB Tree is still running from %s, so nothing moves "
            "this time.\nClose it (or restart Windows), then run INSTALL.BAT "
            "again to move it to\n%s.\n\n", oldToolDir, toolDir);
        if (movePending) {
            /* The records already name both: this run works in the
             * recorded directory and leaves the unfinished move's source,
             * and its MoveFrom, for a later run. */
            oldToolDir[0] = '\0';
        } else {
            inst_copy(toolDir, oldToolDir, sizeof(toolDir));
        }
        flagSteps = 1;
    }
}

/* The old copy of the tool removed after a move, once the new one is in
 * place and the Run value names it. */
static void tray_remove_old(void)
{
    TRAY_PATHS o;

    if (!tray_paths_in(oldToolDir, &o)) {
        return;
    }
    if (!tray_leftovers(&o)) {
        flagSteps = 1;
        return;
    }
    if (!file_exists(o.Exe, NULL)) {
        return;
    }
    tray_close(o.Exe);
    if (delete_file(o.Exe)) {
        flagChanged = 1;
        out("The old copy %s was removed.\n", o.Exe);
        return;
    }
    if (envNow.Nt && !file_exists(o.Del, NULL) && MoveFileA(o.Exe, o.Del) &&
        delete_at_restart(o.Del)) {
        flagChanged = 1;
        flagRestart = 1;
        out("The old copy %s is in use: renamed %s, which is deleted at the "
            "restart.\n", o.Exe, o.Del);
        return;
    }
    out("The old copy %s could not be deleted (error %lu): close the xHCI98 "
        "USB Tree, then delete it.\n", o.Exe, (unsigned long)GetLastError());
    flagSteps = 1;
}

/* The local copies under dir removed (each by its own list); all = 0 takes
 * only directories named as copies are, so a directory the user chose
 * keeps whatever else is in it. */
static void copies_remove(const char *dir, int all)
{
    char pattern[INST_PATH_CHARS];
    char sub[INST_PATH_CHARS];
    char lst[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    join(pattern, sizeof(pattern), dir, "*.*");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0 ||
            inst_eq(fd.cFileName, "XHCIINST.NEW")) {
            continue;
        }
        join(sub, sizeof(sub), dir, fd.cFileName);
        join(lst, sizeof(lst), sub, "XHCIINST.LST");
        /* In a directory the user chose, a copy is one named as a copy is
         * and holding this tool's list: the name alone proves nothing. */
        if (all || (inst_copy_name(fd.cFileName) &&
                    GetFileAttributesA(lst) != 0xFFFFFFFFUL)) {
            local_remove(sub);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* The earlier layouts' copies on 98 SE and ME, once DevicePath no longer
 * names them (record 23 section 9.6): every <hash> directory of
 * %windir%\XHCI98, and those an earlier run of this build made in the
 * install directory. */
static void hashed_cleanup(void)
{
    char legacy[INST_PATH_CHARS];

    if (envNow.Nt || opts.List || !legacy_dir(legacy, sizeof(legacy))) {
        return;
    }
    copies_remove(legacy, 1);
    if (!inst_eq(toolDir, legacy)) {
        copies_remove(toolDir, 0);
        if (!inst_eq(oldToolDir, legacy) && oldToolDir[0] != '\0') {
            copies_remove(oldToolDir, 0);
        }
        if (RemoveDirectoryA(legacy)) {
            flagChanged = 1;
            out("%s removed.\n", legacy);
        }
    }
}

/* The record of a directory a move could not empty (HKLM\Software\xhci98,
 * MoveFrom): the next install finishes the move, the uninstall cleans it. */
static int movefrom_set(const char *dir)
{
    HKEY k;
    int ok = 1;

    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_DIR_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) {
        return 0;
    }
    if (dir != NULL) {
        ok = RegSetValueExA(k, INST_DIR_MOVEFROM, 0, REG_SZ,
                            (const BYTE *)dir, (DWORD)strlen(dir) + 1) ==
             ERROR_SUCCESS;
        logonly("wrote HKLM\\%s\\%s = %s (%s)\n", INST_DIR_KEY,
                INST_DIR_MOVEFROM, dir, ok ? "ok" : "failed");
    } else if (RegDeleteValueA(k, INST_DIR_MOVEFROM) == ERROR_SUCCESS) {
        logonly("deleted HKLM\\%s\\%s\n", INST_DIR_KEY, INST_DIR_MOVEFROM);
    }
    RegCloseKey(k);
    return ok;
}

static void movefrom_get(char *out, size_t cap)
{
    char v[INST_PATH_CHARS];
    char ok[INST_PATH_CHARS];

    out[0] = '\0';
    if (reg_text3(HKEY_LOCAL_MACHINE, INST_DIR_KEY, INST_DIR_MOVEFROM, v,
                  sizeof(v)) == 1 &&
        inst_dir_check(v, 1, NULL, NULL, NULL, ok, sizeof(ok)) == INST_DIR_OK) {
        inst_copy(out, ok, cap);
    }
}

/* 1 when the directory records are still as this run last saw or wrote
 * them (seenRecord, seenMoveFrom); 0, said, when another run has changed
 * them meanwhile - this run then leaves the tools and the records alone.
 * Called with the maintenance lock held, before each write that rests on
 * them. */
static int records_unchanged(void)
{
    char rec[INST_PATH_CHARS];
    char mf[INST_PATH_CHARS];

    if (!seenValid || stScreens) {
        return 1;
    }
    recorded_dir(rec, sizeof(rec));
    movefrom_get(mf, sizeof(mf));
    if (strcmp(rec, seenRecord) == 0 && strcmp(mf, seenMoveFrom) == 0) {
        return 1;
    }
    out("Another run of INSTALL.BAT changed the install directory "
        "meanwhile: this run\nleaves the xHCI98 tools as they are. Run "
        "INSTALL.BAT again.\n");
    flagSteps = 1;
    return 0;
}

/* A move recorded before anything moves (record 23 section 9.6): the old
 * directory as MoveFrom, then the new one as InstallDir, so whatever stops
 * the move part-way, the next run and the uninstall know both. Every run,
 * moving or not, first checks the records are as the question saw them,
 * and checks again before each later write (records_unchanged). 1, or 0
 * when the registry cannot take it or another run changed it (said;
 * nothing has changed). */
static int begin_move(void)
{
    int ok;

    if (opts.List || toolDir[0] == '\0' || stScreens) {
        return 1;
    }
    if (!maint_lock()) {
        flagFailed = 1;
        return 0;
    }
    if (!records_unchanged()) {
        maint_unlock();
        return 0;
    }
    ok = 1;
    if (moving()) {
        ok = movefrom_set(oldToolDir);
        if (ok) {
            movefrom_get(seenMoveFrom, sizeof(seenMoveFrom));
        }
        ok = ok && record_dir();
    }
    maint_unlock();
    if (!ok) {
        out("The move to %s could not be recorded in the registry: nothing "
            "moves.\n", toolDir);
        flagFailed = 1;
        return 0;
    }
    return 1;
}

/* After a move, the old directory: on 98 SE and ME its package copy and
 * its earlier copies (DevicePath no longer names them), then the
 * directory itself if it is empty. NT's %windir%\XHCI98 keeps its copies,
 * and so stays. What is left of the tool there is recorded (MoveFrom) for
 * the next run; a move finished deletes the record. */
static void old_dir_cleanup(void)
{
    char legacy[INST_PATH_CHARS];
    TRAY_PATHS o;
    int toolLeft;
    int flatLeft = 0;
    int ownLeft;

    if (opts.List || !moving()) {
        return;
    }
    if (!legacy_dir(legacy, sizeof(legacy))) {
        legacy[0] = '\0';
    }
    if (!envNow.Nt) {
        flatLeft = !flat_remove(oldToolDir);
        copies_remove(oldToolDir, inst_eq(oldToolDir, legacy));
    }
    /* The installer's own files move with it (record 26 section 6.2):
     * the old ones go once the new directory holds the window. */
    {
        char win[INST_PATH_CHARS];
        join(win, sizeof(win), toolDir,
             inst_self_file(INST_MY_ARCH, INST_OWN_WINDOW));
        if (file_exists(win, NULL)) {
            ownLeft = !own_remove_in(oldToolDir, 1);
        } else {
            ownLeft = own_any_in(oldToolDir);
        }
    }
    toolLeft = (tray_paths_in(oldToolDir, &o) &&
                (file_exists(o.Exe, NULL) || file_exists(o.Bak, NULL) ||
                 file_exists(o.Del, NULL))) ||
               flatLeft || ownLeft;
    if (RemoveDirectoryA(oldToolDir)) {
        flagChanged = 1;
        out("%s removed.\n", oldToolDir);
    } else if (GetFileAttributesA(oldToolDir) != 0xFFFFFFFFUL &&
               !toolLeft && !(envNow.Nt && inst_eq(oldToolDir, legacy))) {
        out("%s kept: it holds files this tool did not put there.\n",
            oldToolDir);
    }
    movefrom_set(toolLeft ? oldToolDir : NULL);
}

/* Record 23 section 9.5: the tool for this machine's architecture into the
 * install directory (section 9.6), and the Run value that starts it at
 * login (none with /notray), under the maintenance lock; after a move, the
 * old copy removed. Under /list, what would be done. */
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
    /* A package without the tool leaves everything of it as it is. */
    if (!file_exists(from, NULL)) {
        out("This package has no tools\\xhcitr\\%s: the xHCI98 USB Tree is "
            "not installed.\n", name);
        return;
    }
    if (!tray_paths(&t)) {
        out("The xHCI98 USB Tree's place could not be named.\n");
        flagFailed = 1;
        return;
    }
    if (!opts.List) {
        if (!maint_lock()) {
            flagFailed = 1;
            return;
        }
        if (!records_unchanged()) {
            goto done;
        }
        if (!make_dirs(t.Dir)) {
            out("%s could not be made (error %lu).\n", t.Dir,
                (unsigned long)GetLastError());
            flagFailed = 1;
            goto done;
        }
        if (!tray_leftovers(&t)) {
            flagSteps = 1;
            goto done;
        }
    }
    pkg = file_version(from, pv);
    inst = file_version(t.Exe, iv);
    /* A move carries a newer tool along rather than put an older one in
     * its place: the copy in the old directory is the source then. */
    if (moving()) {
        TRAY_PATHS o;
        INST_U32 ov[4];
        /* An interrupted replacement there is put right first, so a
         * newer tool left as .BAK is the one compared. */
        if (!opts.List && tray_paths_in(oldToolDir, &o) &&
            !tray_leftovers(&o)) {
            /* The old directory's .BAK could not be put back: the tool's
             * part stops, as for the install directory's own. */
            flagSteps = 1;
            goto done;
        }
        if (tray_paths_in(oldToolDir, &o) &&
            file_version(o.Exe, ov) > 0 &&
            (pkg <= 0 || inst_ver_cmp(ov, pv) > 0)) {
            inst_copy(from, o.Exe, sizeof(from));
            memcpy(pv, ov, sizeof(pv));
            pkg = 1;
            logonly("the tool moved from %s (newer than the package's)\n",
                    o.Exe);
        }
    }
    copy = inst_tray_copy_needed(inst, iv, pkg, pv);
    if (opts.List) {
        int runState = tray_run_state(t.Run);
        action = inst_tray_run_action(opts.NoTray, runState,
                                      copy || inst != 0);
        out("The xHCI98 USB Tree: %s\n  %s%s\n",
            copy ? "would be copied to" : "is current at", t.Exe,
            inst_tray_list_text(runState, action));
        if (moving()) {
            out("  The copy in %s would be removed.\n", oldToolDir);
        }
        return;
    }
    if (copy) {
        put = tray_put(from, &t);
        if (put == INST_TRAY_PUT_FAILED) {
            flagFailed = 1;
        } else if (put == INST_TRAY_PUT_IN_USE) {
            flagSteps = 1;
        }
        /* A move goes on only with the tool it carries in place: the
         * source stays, and the Run value with it, until then. */
        if (moving() && put != INST_TRAY_PUT_DONE) {
            goto done;
        }
    } else {
        out("The xHCI98 USB Tree at %s is current.\n", t.Exe);
    }
    /* A move records the new directory before the Run value names it: a
     * record that cannot be written leaves the old one in charge, and the
     * copy just made goes again. */
    if (moving() && file_exists(t.Exe, NULL) && !record_dir()) {
        if (copy && delete_file(t.Exe)) {
            logonly("%s deleted again: the move is not recorded\n", t.Exe);
        }
        goto done;
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
    /* In place: the directory is recorded, and after a move the old copy
     * goes - the Run value names the new one already (or none, /notray). */
    if (file_exists(t.Exe, NULL) && record_dir() && moving() &&
        tray_run_state(t.Run) == (opts.NoTray ? 0 : 1)) {
        tray_remove_old();
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
static char trayDeferredDir[INST_PATH_CHARS];

/* One directory's copy of the tool removed, under the maintenance lock. */
static void tray_remove_in(const TRAY_PATHS *t)
{
    if (!tray_leftovers(t)) {
        flagSteps = 1;
        return;
    }
    if (!file_exists(t->Exe, NULL)) {
        return;
    }
    tray_close(t->Exe);
    if (delete_file(t->Exe)) {
        flagChanged = 1;
        out("%s removed.\n", t->Exe);
        return;
    }
    if (!envNow.Nt) {
        out("%s is still running, so it stays; it no longer starts at login. "
            "Run\nINSTALL /uninstall again after a restart to delete it.\n",
            t->Exe);
        flagSteps = 1;
        return;
    }
    /* A running image can be renamed on its volume: the tool's own name is
     * free at once, and only the renamed file waits for the restart. */
    if (file_exists(t->Del, NULL) || !MoveFileA(t->Exe, t->Del)) {
        out("%s is in use and could not be renamed; run INSTALL /uninstall "
            "again after a\nrestart.\n", t->Exe);
        flagSteps = 1;
        return;
    }
    flagChanged = 1;
    trayDeferred = 1;
    inst_copy(trayDeferredDir, t->Dir, sizeof(trayDeferredDir));
    if (!delete_at_restart(t->Del)) {
        out("%s could not be scheduled for deletion; run INSTALL /uninstall "
            "again after a\nrestart.\n", t->Del);
        flagSteps = 1;
        return;
    }
    flagRestart = 1;
    out("%s is in use: renamed %s, which is deleted at the restart.\n",
        t->Exe, t->Del);
}

/* The tool's copies: in the install directory, in an earlier build's
 * %windir%\XHCI98, and in a directory a move could not empty (oldToolDir,
 * from MoveFrom), each once (record 23 section 9.6). */
static void tray_remove(void)
{
    TRAY_PATHS t[3];
    char legacy[INST_PATH_CHARS];
    const char *cand[3];
    int n = 0;
    int i;
    int j;
    int dup;

    if (!legacy_dir(legacy, sizeof(legacy))) {
        legacy[0] = '\0';
    }
    cand[0] = toolDir;
    cand[1] = legacy;
    cand[2] = oldToolDir;
    for (i = 0; i < 3; i++) {
        if (cand[i][0] == '\0') {
            continue;
        }
        dup = 0;
        for (j = 0; j < n; j++) {
            dup |= same_dir(cand[i], t[j].Dir);
        }
        if (!dup && tray_paths_in(cand[i], &t[n])) {
            n++;
        }
    }
    if (opts.List) {
        if (tray_run_state("") != 0) {
            out("(/list) the xHCI98 USB Tree's start at login would be "
                "deleted.\n");
        }
        for (i = 0; i < n; i++) {
            if (file_exists(t[i].Exe, NULL)) {
                out("(/list) %s would be closed and deleted.\n", t[i].Exe);
            }
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
    for (i = 0; i < n; i++) {
        tray_remove_in(&t[i]);
    }
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
            if (k == INST_INF_CMF) {
                inst_copy(pnf, inst_cmf_pnf(INST_MY_ARCH), sizeof(pnf));
            } else {
                inst_copy(pnf, instInfFile[k], sizeof(pnf));
                n = strlen(pnf);
                if (n > 4) {
                    strcpy(pnf + n - 4, ".PNF");
                }
            }
            join(path, sizeof(path), dir, pnf);
            SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileA(path)) {
                flagChanged = 1;
            }
        }
        if (INST_MY_ARCH == INST_ARCH_AMD64) {
            /* A copy of a 3.0.0.0 x64 package, whose filter INF was in
             * nt\ as the x86 package's is. */
            join(path, sizeof(path), dir, inst_cmf_pnf(INST_ARCH_X86));
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
            if (p[0] == '\0' || p[0] == '#' || strcmp(p, "END") == 0 ||
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

/* One directory of this tool's removed once it is empty; the one the USB
 * tree tool still in use keeps is scheduled with it (NT). */
static void dir_remove(const char *dir)
{
    if (RemoveDirectoryA(dir)) {
        flagChanged = 1;
        out("%s removed.\n", dir);
    } else if (trayDeferred && inst_eq(dir, trayDeferredDir)) {
        /* The USB tree tool, still in use, is what keeps it now. */
        if (delete_at_restart(dir)) {
            out("%s is removed at the restart.\n", dir);
        } else {
            out("%s can be deleted after the restart.\n", dir);
        }
    } else if (GetFileAttributesA(dir) != 0xFFFFFFFFUL) {
        out("%s kept: it holds files this tool did not put there.\n", dir);
    }
}

/* Every local copy of the package and the /restage work file (record 23
 * sections 4.4 and 9.2 step 6; on 98 SE and ME the copy in the install
 * directory, by its lists). 0 when a 98 SE or ME copy was not removed
 * whole. */
static int copies_remove_all(void)
{
    char legacy[INST_PATH_CHARS];
    char sub[INST_PATH_CHARS];
    int i;
    int flatLeft = 0;

    if (!legacy_dir(legacy, sizeof(legacy))) {
        return 0;
    }
    /* %windir%\XHCI98 is this tool's own: every directory in it goes by
     * its list. A chosen directory gives up only what its lists name and
     * the <hash> directories that hold this tool's list. */
    copies_remove(legacy, 1);
    if (!envNow.Nt && !flat_remove(legacy)) {
        flatLeft = 1;
    }
    join(sub, sizeof(sub), legacy, "RESTAGE.LST");
    if (DeleteFileA(sub)) {
        flagChanged = 1;
    }
    for (i = 0; i < 2; i++) {
        const char *d = i == 0 ? toolDir : oldToolDir;
        if (d[0] == '\0' || same_dir(d, legacy) ||
            (i == 1 && toolDir[0] != '\0' && same_dir(d, toolDir))) {
            continue;
        }
        if (!envNow.Nt) {
            if (!flat_remove(d)) {
                flatLeft = 1;
            }
            copies_remove(d, 0);
        }
    }
    return !flatLeft;
}

/* Record 26 section 9.3 steps 5 to 7, once steps 1 to 4 left nothing for a
 * later run: the USB tree tool and its start at login, the installer's
 * own files, the records (the machine-wide values the table names, the
 * install directory's, the Add/Remove Programs entry), then the
 * directories. A tool or a file left on 98 SE or ME keeps the installer,
 * the records and the entry for the next run (incomplete). */
static void finish_remove(void)
{
    char legacy[INST_PATH_CHARS];
    TRAY_PATHS t;
    const char *d;
    int i;
    int left = 0;
    int failedBefore = flagFailed;
    int stepsBefore = flagSteps;

    if (!legacy_dir(legacy, sizeof(legacy))) {
        legacy[0] = '\0';
    }
    if (opts.List) {
        tray_remove();
        own_remove();
        mw_values_remove();
        return;
    }
    tray_remove();
    for (i = 0; i < 3; i++) {
        d = i == 0 ? toolDir : i == 1 ? legacy : oldToolDir;
        if (d[0] != '\0' && tray_paths_in(d, &t) &&
            (file_exists(t.Exe, NULL) || file_exists(t.Bak, NULL) ||
             file_exists(t.Tmp, NULL) ||
             (!envNow.Nt && file_exists(t.Del, NULL)))) {
            left = 1;
        }
    }
    /* Anything the tool's removal could not finish (its Run value, a
     * leftover) keeps the installer and its records for the next run. */
    if (left || flagFailed != failedBefore || flagSteps != stepsBefore) {
        arp_write(1);
        flagSteps = 1;
        return;
    }
    if (!mw_values_remove()) {
        arp_write(1);
        return;
    }
    /* The installer's files of a move an earlier run could not finish
     * too, then this directory's but its window; the records; the window
     * last, its record put back if it cannot go, so the entry always has
     * its program and its directory. */
    if ((oldToolDir[0] != '\0' && !own_remove_in(oldToolDir, 1)) ||
        !own_remove_in(toolDir, 0)) {
        arp_write(1);
        flagSteps = 1;
        return;
    }
    /* The entry, then the window, then the records: a window that cannot
     * go gets its entry back, its directory's record still in place; the
     * records go only once nothing they lead to is left. */
    if (!arp_remove()) {
        return;
    }
    if (!own_remove_in(toolDir, 1)) {
        arp_write(1);
        flagSteps = 1;
        return;
    }
    if (!forget_dir()) {
        out("Nothing of this package is left but its values %s and %s under "
            "HKLM\\%s;\ndelete those two with regedit (and the key only if "
            "nothing else is in it).\n", INST_DIR_VALUE, INST_DIR_MOVEFROM,
            INST_DIR_KEY);
        return;
    }
    for (i = 0; i < 2; i++) {
        d = i == 0 ? toolDir : oldToolDir;
        if (d[0] == '\0' || same_dir(d, legacy) ||
            (i == 1 && toolDir[0] != '\0' && same_dir(d, toolDir))) {
            continue;
        }
        if (GetFileAttributesA(d) != 0xFFFFFFFFUL) {
            dir_remove(d);
        }
    }
    if (legacy[0] != '\0' && GetFileAttributesA(legacy) != 0xFFFFFFFFUL) {
        dir_remove(legacy);
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
            /* The bundled Stor98 driver goes into the copy only when ticked
             * (record 26 section 6.3): a copy of its INF on DevicePath, or
             * in the store, would install it unasked. */
            if (inst_bundled_file(fd.cFileName) &&
                (!s98Include || s98Share == INST_S98_SHARE_HOLD)) {
                if (s98Include) {
                    /* Held for the whole run: a later reading that lifts
                     * the hold finds no Stor98 in the copy. */
                    s98HoldRun = 1;
                }
                continue;
            }
            /* The UAS driver left out (owner, 2026-10-19): on 98 SE and ME
             * the copy is on DevicePath, where its INF would install it
             * unasked; on NT it is simply not staged. */
            if (!envNow.Nt && !uasInclude &&
                (inst_eq(fd.cFileName, "xhciuas.inf") ||
                 inst_eq(fd.cFileName, "xhciuas.sys"))) {
                continue;
            }
            /* The connection manager alone on 98 SE and ME (record 26
             * section 5.3.1 case 2): no wizard may offer the USB drivers
             * from the copy on DevicePath. */
            if (!envNow.Nt && !usbInclude &&
                (inst_eq(fd.cFileName, "xhci98.inf") ||
                 inst_eq(fd.cFileName, "xhci98.sys") ||
                 inst_eq(fd.cFileName, "xhciuas.inf") ||
                 inst_eq(fd.cFileName, "xhciuas.sys"))) {
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
    /* 98 SE: the USB storage drivers' seven, from the download's NONFREE\
     * folder, once their files are in place (record 26 section 6.5.4). */
    if (!envNow.Nt && storCopyOk) {
        int i;
        /* USBNTMAP.INF before USBSTOR.INF, so a copy cut short never holds
         * USBSTOR.INF alone (record 26 section 6.5.5). */
        for (i = 0; i < INST_STOR_COUNT; i++) {
            if (!each(instStorFile[instStorCopyOrder[i]].Name, ctx)) {
                return 0;
            }
        }
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
    pkg_src(rel, path, sizeof(path));
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
    pkg_src(rel, from, sizeof(from));
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

    pkg_src(rel, from, sizeof(from));
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
 * built again. 98 SE and ME copy into the install directory itself
 * (make_flat_copy). */
static int make_flat_copy(void);
static int devicepath_update(const char *addDir);

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

    if (!envNow.Nt) {
        return make_flat_copy();
    }
    if (!local_dir(rootDir, sizeof(rootDir)) || !package_tag(&tag)) {
        out("The local copy could not be prepared.\n");
        return 0;
    }
    if (!make_dirs(rootDir)) {
        out("%s could not be made (error %lu).\n", rootDir,
            (unsigned long)GetLastError());
        return 0;
    }
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
/* ---- 98 SE and ME: the package copy in the install directory itself ---- */

/* Owner, 2026-10-16 (record 23 section 9.6): on 98 SE and ME the package's
 * drivers go straight into the install directory, which DevicePath names
 * and the user types into the hardware wizards - no subfolder. What the
 * hashed, immutable subfolders gave is kept by the order of an update: the
 * new files are first staged and checked in XHCIINST.NEW below it, then
 * DevicePath stops naming the directory, then the old list is renamed
 * XHCIINST.OLD and its files deleted, the new files moved in and the new
 * list put in place last, and only then does DevicePath name it again. So
 * no wizard searches a mixture of two packages, and a run cut short leaves
 * the directory off DevicePath with every file it holds named by one of
 * the three lists, for the next run to finish. A file in the way that no
 * list names refuses the copy: nothing the user put there is overwritten
 * or deleted. */

#define INST_FLAT_LIST  "XHCIINST.LST"
#define INST_FLAT_OLD   "XHCIINST.OLD"
#define INST_FLAT_WORK  "XHCIINST.NEW"

/* 1 when name is a line of dir\lst (without case), 0 not or no list. */
static int flat_listed(const char *dir, const char *lst, const char *name)
{
    char path[INST_PATH_CHARS];
    char *text;
    char *p;
    char *nl;
    int found = 0;

    join(path, sizeof(path), dir, lst);
    text = read_file(path, NULL);
    if (text == NULL) {
        return 0;
    }
    for (p = text; *p != '\0' && !found; p = nl) {
        nl = strchr(p, '\n');
        if (nl == NULL) {
            nl = p + strlen(p);
        } else {
            *nl++ = '\0';
        }
        if (strlen(p) > 0 && p[strlen(p) - 1] == '\r') {
            p[strlen(p) - 1] = '\0';
        }
        found = inst_eq(p, name);
    }
    free(text);
    return found;
}

/* One file of a list deleted, an .inf with its .PNF. 1 gone; 0 it stayed
 * (said). */
static int flat_delete(char *file)
{
    size_t n;
    int gone;

    SetFileAttributesA(file, FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileA(file)) {
        flagChanged = 1;
    }
    gone = GetFileAttributesA(file) == 0xFFFFFFFFUL;
    if (!gone) {
        out("%s could not be deleted (error %lu).\n", file,
            (unsigned long)GetLastError());
    }
    n = strlen(file);
    if (n > 4 && inst_eq(file + n - 4, ".inf")) {
        strcpy(file + n - 4, ".PNF");
        SetFileAttributesA(file, FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileA(file)) {
            flagChanged = 1;
        }
    }
    return gone;
}

/* The files dir\lst names (each .inf with its .PNF) deleted, then lst -
 * only when every one of them is gone, so a file that would not go stays
 * named for the next run. Only plain names: a list never reaches outside
 * dir. 1 when nothing listed is left (or there was no list), 0 when a
 * file stayed (said). */
static int flat_remove_list(const char *dir, const char *lst)
{
    char path[INST_PATH_CHARS];
    char file[INST_PATH_CHARS];
    char *text;
    char *p;
    char *nl;
    int left = 0;
    int late = 0;

    join(path, sizeof(path), dir, lst);
    text = read_file(path, NULL);
    if (text == NULL) {
        return GetFileAttributesA(path) == 0xFFFFFFFFUL;
    }
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
        if (p[0] == '\0' || p[0] == '#' || strcmp(p, "END") == 0 ||
            strchr(p, ':') != NULL || strchr(p, '\\') != NULL ||
            strchr(p, '/') != NULL || strstr(p, "..") != NULL ||
            inst_eq(p, inst_tray_file(INST_MY_ARCH))) {
            continue;
        }
        /* USBNTMAP.INF after the rest, once USBSTOR.INF is gone (record
         * 26 section 6.5.5). */
        if (inst_stor_rm_late(p, NULL)) {
            late = 1;
            continue;
        }
        join(file, sizeof(file), dir, p);
        if (!flat_delete(file)) {
            left = 1;
        }
    }
    free(text);
    if (late) {
        join(file, sizeof(file), dir,
             instStorFile[INST_STOR_IDX_USBSTOR_INF].Name);
        if (GetFileAttributesA(file) != 0xFFFFFFFFUL) {
            out("%s stays while %s is there.\n",
                instStorFile[INST_STOR_IDX_USBNTMAP_INF].Name, file);
            left = 1;
        } else {
            join(file, sizeof(file), dir,
                 instStorFile[INST_STOR_IDX_USBNTMAP_INF].Name);
            if (!flat_delete(file)) {
                left = 1;
            }
        }
    }
    if (left) {
        flagSteps = 1;
        return 0;
    }
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileA(path)) {
        flagChanged = 1;
    }
    return 1;
}

/* A staging directory left by a run cut short: when the swap had begun (no
 * list in dir), the files it already moved in are named by the staging
 * list and go; the staging directory goes in any case. */
static int flat_drop_work(const char *dir)
{
    char work[INST_PATH_CHARS];
    char lst[INST_PATH_CHARS];
    char *text;
    char *p;
    char *nl;
    char file[INST_PATH_CHARS];
    int left = 0;

    join(work, sizeof(work), dir, INST_FLAT_WORK);
    if (GetFileAttributesA(work) == 0xFFFFFFFFUL) {
        return 1;
    }
    join(lst, sizeof(lst), dir, INST_FLAT_LIST);
    if (GetFileAttributesA(lst) == 0xFFFFFFFFUL) {
        join(lst, sizeof(lst), work, INST_FLAT_LIST);
        text = read_file(lst, NULL);
        for (p = text; p != NULL && *p != '\0'; p = nl) {
            nl = strchr(p, '\n');
            if (nl == NULL) {
                nl = p + strlen(p);
            } else {
                *nl++ = '\0';
            }
            if (strlen(p) > 0 && p[strlen(p) - 1] == '\r') {
                p[strlen(p) - 1] = '\0';
            }
            if (p[0] == '\0' || p[0] == '#' || strcmp(p, "END") == 0 ||
                strchr(p, ':') != NULL || strchr(p, '\\') != NULL ||
                strstr(p, "..") != NULL ||
                inst_eq(p, inst_tray_file(INST_MY_ARCH))) {
                continue;
            }
            join(file, sizeof(file), dir, p);
            SetFileAttributesA(file, FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileA(file)) {
                flagChanged = 1;
            }
            if (GetFileAttributesA(file) != 0xFFFFFFFFUL) {
                out("%s could not be deleted (error %lu).\n", file,
                    (unsigned long)GetLastError());
                left = 1;
            }
        }
        free(text);
    }
    /* A file that stayed keeps the staging list that names it. */
    if (left) {
        flagSteps = 1;
        return 0;
    }
    if (!flat_remove_list(work, INST_FLAT_LIST)) {
        return 0;
    }
    if (RemoveDirectoryA(work)) {
        flagChanged = 1;
    }
    return 1;
}

/* Everything of the package copy in dir removed (the uninstall, and the
 * old directory after a move): a staging directory, the old list's files,
 * the list's files. The tool and anything unlisted stay. */
static int flat_remove(const char *dir)
{
    int a;
    int b;
    int w;

    w = flat_drop_work(dir);
    a = flat_remove_list(dir, INST_FLAT_OLD);
    b = flat_remove_list(dir, INST_FLAT_LIST);
    return a && b && w;
}

/* The "#TAG xxxxxxxx" line of dir's list, or 0 with none. */
static int flat_tag(const char *dir, INST_U32 *tag)
{
    char path[INST_PATH_CHARS];
    char *text;
    char *p;
    int ok = 0;

    join(path, sizeof(path), dir, INST_FLAT_LIST);
    text = read_file(path, NULL);
    if (text == NULL) {
        return 0;
    }
    p = strstr(text, "#TAG ");
    if (p == text || (p != NULL && p[-1] == '\n')) {
        *tag = (INST_U32)strtoul(p + 5, NULL, 16);
        ok = 1;
    }
    free(text);
    return ok;
}

static int flat_check_one(const char *rel, void *ctx)
{
    char from[INST_PATH_CHARS];
    char to[INST_PATH_CHARS];

    (void)ctx;
    pkg_src(rel, from, sizeof(from));
    join(to, sizeof(to), toolDir, rel);
    return same_file(from, to);
}

/* A package file's name in the install directory: free, or one of the
 * lists' (this tool's). Anything else is the user's, and refuses. */
static int flat_free_one(const char *rel, void *ctx)
{
    char path[INST_PATH_CHARS];
    char work[INST_PATH_CHARS];

    (void)ctx;
    join(path, sizeof(path), toolDir, rel);
    join(work, sizeof(work), toolDir, INST_FLAT_WORK);
    if (GetFileAttributesA(path) == 0xFFFFFFFFUL ||
        flat_listed(toolDir, INST_FLAT_LIST, rel) ||
        flat_listed(toolDir, INST_FLAT_OLD, rel) ||
        flat_listed(work, INST_FLAT_LIST, rel)) {
        return 1;
    }
    out("%s is in the install directory, and this tool did not put it "
        "there: refused.\nMove it away, or choose another directory.\n", path);
    return 0;
}

/* Each file of the staging list moved into the directory. 1, or 0 said. */
static int flat_move_in(const char *work)
{
    char lst[INST_PATH_CHARS];
    char from[INST_PATH_CHARS];
    char to[INST_PATH_CHARS];
    char *text;
    char *p;
    char *nl;
    int ok = 1;

    join(lst, sizeof(lst), work, INST_FLAT_LIST);
    text = read_file(lst, NULL);
    if (text == NULL) {
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
        if (p[0] == '\0' || p[0] == '#' || strcmp(p, "END") == 0 ||
            strchr(p, '\\') != NULL || strchr(p, ':') != NULL) {
            continue;
        }
        join(from, sizeof(from), work, p);
        join(to, sizeof(to), toolDir, p);
        SetFileAttributesA(to, FILE_ATTRIBUTE_NORMAL);
        DeleteFileA(to);
        if (!MoveFileA(from, to)) {
            out("%s could not be put in place (error %lu).\n", to,
                (unsigned long)GetLastError());
            ok = 0;
        }
    }
    free(text);
    return ok;
}

static int make_flat_copy(void)
{
    char work[INST_PATH_CHARS];
    char lst[INST_PATH_CHARS];
    char old[INST_PATH_CHARS];
    char line[32];
    COPY_CTX c;
    INST_U32 tag;
    INST_U32 have;
    int committed;
    int ok;

    if (toolDir[0] == '\0' || !package_tag(&tag)) {
        out("The copy of the package could not be prepared.\n");
        return 0;
    }
    if (!make_dirs(toolDir)) {
        out("%s could not be made (error %lu).\n", toolDir,
            (unsigned long)GetLastError());
        return 0;
    }
    inst_copy(srcDir, toolDir, sizeof(srcDir));
    committed = local_committed(toolDir);
    if (committed < 0) {
        out("%s\\%s cannot be read: refused, nothing changed.\n", toolDir,
            INST_FLAT_LIST);
        return 0;
    }
    join(work, sizeof(work), toolDir, INST_FLAT_WORK);
    if (committed > 0 && flat_tag(toolDir, &have) && have == tag &&
        GetFileAttributesA(work) == 0xFFFFFFFFUL &&
        package_files(flat_check_one, NULL)) {
        out("The copy of this package in %s is current.\n", toolDir);
        return 1;
    }
    if (!package_files(flat_free_one, NULL)) {
        return 0;
    }
    /* Staged and checked below the directory first. */
    flat_drop_work(toolDir);
    if (GetFileAttributesA(work) != 0xFFFFFFFFUL ||
        !CreateDirectoryA(work, NULL)) {
        out("%s could not be made (error %lu).\n", work,
            (unsigned long)GetLastError());
        return 0;
    }
    join(lst, sizeof(lst), work, INST_FLAT_LIST);
    c.List = fopen(lst, "w");
    c.Dir = work;
    c.Count = 0;
    if (c.List == NULL) {
        out("%s could not be written.\n", lst);
        return 0;
    }
    _snprintf(line, sizeof(line) - 1, "#TAG %08lX\n", (unsigned long)tag);
    line[sizeof(line) - 1] = '\0';
    ok = fputs(line, c.List) >= 0 && package_files(copy_one, &c);
    if (ok && (fprintf(c.List, "END\n") < 0 || fflush(c.List) != 0)) {
        ok = 0;
    }
    if (fclose(c.List) != 0) {
        ok = 0;
    }
    if (!ok) {
        out("The copy of the package was not completed; it is removed.\n");
        flat_drop_work(toolDir);
        return 0;
    }
    /* DevicePath stops naming the directory while its files change. */
    if (!devicepath_update(NULL)) {
        flat_drop_work(toolDir);
        return 0;
    }
    join(lst, sizeof(lst), toolDir, INST_FLAT_LIST);
    join(old, sizeof(old), toolDir, INST_FLAT_OLD);
    if (GetFileAttributesA(lst) != 0xFFFFFFFFUL) {
        if (!flat_remove_list(toolDir, INST_FLAT_OLD) || !MoveFileA(lst, old)) {
            out("%s could not be renamed (error %lu).\n", lst,
                (unsigned long)GetLastError());
            return 0;
        }
    }
    if (!flat_remove_list(toolDir, INST_FLAT_OLD)) {
        out("The earlier copy's files could not all be deleted, so the new "
            "ones were not\nput in place; the directory is off DevicePath "
            "meanwhile. Run INSTALL.BAT again.\n");
        return 0;
    }
    if (!flat_move_in(work)) {
        out("The copy of the package in %s is not complete; it is not on "
            "DevicePath.\nRun INSTALL.BAT again to finish it.\n", toolDir);
        return 0;
    }
    join(old, sizeof(old), work, INST_FLAT_LIST);
    if (!MoveFileA(old, lst)) {
        out("%s could not be put in place (error %lu).\n", lst,
            (unsigned long)GetLastError());
        return 0;
    }
    RemoveDirectoryA(work);
    flagChanged = 1;
    copiedNow = 1;
    out("Copied %d files to %s; the hardware wizards take them from "
        "there.\n", c.Count, toolDir);
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
    PFN_MOVEEX moveEx;

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
    moveEx = move_ex();
    if (moveEx == NULL ||
        !moveEx(tmp, path, MOVEFILE_REPLACE_EXISTING |
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
            "files, not this\npackage's.\n", instInfFile[k], trapOem[k]);
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
        if (k == INST_INF_S98 && s98Share == INST_S98_SHARE_HOLD) {
            out("%s is not staged: a Stor98 the installer did not put on a "
                "tunnel drive runs\nanother release (see above).\n",
                instInfFile[k]);
            continue;
        }
        if (k == INST_INF_S98 && s98Share != INST_S98_SHARE_FREE &&
            (trapOem[k][0] != '\0' || trapUnknown[k])) {
            /* /restage would take out a stored Stor98 package a drive the
             * installer leaves may still name. */
            out("%s is not staged: another build of it is in the driver "
                "store, and a Stor98\nthe installer did not put on a tunnel "
                "drive may name it. Nothing of Stor98 is\nchanged.\n",
                instInfFile[k]);
            infTrap[k] = 1;
            flagSteps = 1;
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
        if (k == INST_INF_S98) {
            s98_oem_before();
        }
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
        if (k == INST_INF_S98 && s98Baseline) {
            /* A new copy the setup engine made for this installer (record
             * 26 section 6.3): a name not in %windir%\inf before the call;
             * one it answered with an INF already there is left. */
            const char *nm = component ? component : dest;
            const char *base = strrchr(nm, '\\');
            base = base != NULL ? base + 1 : nm;
            if (s98_oem_was(base)) {
                logonly("stor98: %s was in the store before: not recorded\n",
                        base);
            } else {
                s98_rec_text("Staged", base, s98VersionText);
            }
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
        if (inf == INST_INF_S98 && d->Inf == inf && !d->Runs &&
            d->S98Why != INST_S98_ELIGIBLE) {
            /* NT's call reaches every device with the id: a drive the
             * installer leaves keeps the tunnel drive from it too. */
            out("  %s is shared with %s, which this run leaves (%s);\n"
                "  neither is changed. Install Stor98 on the tunnel drive "
                "in Device Manager\n  (Update Driver, Have Disk, this "
                "package's directory) if you want it.\n", hwid, d->Id,
                inst_s98_skip_text(d->S98Why, 1));
            return 0;
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
    /* Stor98's first install on a device with no driver goes unforced
     * (record 26 section 6.3); an update of a Stor98 it runs, or /force's
     * reinstall, takes record 23's forced route. */
    if (inf == INST_INF_S98) {
        /* Over Windows' own stornvme (8.1, record 26 section 5.3.1 case
         * 3b): its signed in-box INF outranks Stor98's, so forced. */
        if (x->Action == INST_ACT_INSTALL && inst_eq(x->Service, "stornvme")) {
            return INST_INSTALLFLAG_FORCE;
        }
        return x->Action == INST_ACT_INSTALL ? 0 : INST_INSTALLFLAG_FORCE;
    }
    return (inf == INST_INF_CMF && x->Action == INST_ACT_FILTER &&
            envNow.Major == 5 && envNow.Minor == 0) ?
           0 : INST_INSTALLFLAG_FORCE;
}

/* Install or update the planned devices of one INF; controllers selects the
 * xhci98 controllers (1), the rest of xhci98's devices (0), or every device
 * (-1). Returns 0 when a failure leaves the order meaningless. */
static int settle(INST_U32 limitMs, INST_U32 tellMs, INST_U32 expectMs,
                  int first);
static void settle_failed(int r, const char *bound);

/* 1 when device i's call is not the last install_inf makes: a device
 * after it in the list is one the same call of install_inf (inf,
 * controllers) still takes. */
static int nt_more_after(int i, int inf, int controllers)
{
    int j;

    for (j = i + 1; j < devCount; j++) {
        const DEVREC *d = &devs[j];
        if (!d->Present || d->Inf != inf || !d->Runs || d->Done) {
            continue;
        }
        if ((controllers == 1 && !is_controller(d)) ||
            (controllers == 0 && is_controller(d))) {
            continue;
        }
        return 1;
    }
    return 0;
}

/* Up to INST_REAPPEAR_MS for a device a parent's job took away (it comes
 * back as the parent restarts), then the settle with nothing expected.
 * 1 settled (whether or not it came back) and not cancelled; 0 the settle
 * failed or the run was cancelled (said): the caller stops. */
static int reappear_settle(const char *id)
{
    DWORD t0 = GetTickCount();
    DWORD dn;
    char want[INST_ID_CHARS];
    int r;

    inst_copy(want, id, sizeof(want));
    out("Waiting for the devices to settle ... ");
    out_flush();
    while (api.Locate != NULL &&
           api.Locate(&dn, want, 0) != INST_CR_SUCCESS &&
           (INST_U32)(GetTickCount() - t0) < INST_REAPPEAR_MS) {
        if (!waiting(INST_WAIT_SETTLE, (unsigned long)(GetTickCount() - t0),
                     INST_REAPPEAR_MS, "a device above it is restarting")) {
            runCancelled = 1;
            flagSteps = 1;
            settle_failed(-3, "10 minutes");
            return 0;
        }
        Sleep(INST_SETTLE_POLL_MS);
    }
    r = settle(INST_INSTALL_WAIT_MS, 20000UL, 0UL, 0);
    if (r <= 0) {
        settle_failed(r, "10 minutes");
        return 0;
    }
    out("done.\n");
    /* A cancel during the settle's last poll is seen here, before the
     * next call. */
    return go_on();
}

/* 1 done, 0 a controller's call failed (the rest is not attempted), -1 a
 * settle inside it failed or was cancelled: the run stops (said). */
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
    int retried;
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
        if (mixed && inf == INST_INF_S98) {
            out("Left: %s - drives sharing it need a first Stor98 install and "
                "an update at\n  once, which one call cannot do. Update the "
                "one Stor98 runs in Device Manager\n  (Update Driver, Have "
                "Disk, this package's directory), then run INSTALL.BAT\n"
                "  again.\n", hwid);
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
        if (mixed) {
            out("Left: %s - devices sharing it need a first filter install "
                "and an update at\n  once, which one call cannot do on "
                "Windows 2000. In Device Manager install\n  the filter on "
                "EVERY port with this id - those without it too - (Update\n"
                "  Driver, Have Disk, %s), then run "
                "INSTALL.BAT again.\n", hwid, inst_cmf_where(INST_MY_ARCH));
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
                    if (inf == INST_INF_S98 &&
                        inst_eq(devs[j].Service, "stornvme") &&
                        !s98_pending_mark(devs[j].Id, "attempt")) {
                        /* Its ownership survives a reconnection that comes
                         * only in a later run (record 26 section 5.3.1 case
                         * 3b): attempted now, done once the call says so -
                         * and no replacement without that record. */
                        full = 1;
                    }
                }
            }
        }
        if (full) {
            out("Too many devices to follow, or a record could not be "
                "written: %s is not\nchanged.\n", hwid);
            flagFailed = 1;
            for (j = 0; j < n; j++) {
                changedState[callIdx[j]] = 0;
            }
            continue;
        }
        out("%s %s ... ", d->Action == INST_ACT_INSTALL ? "Installing" :
            d->Action == INST_ACT_FILTER ? "Adding the filter to" : "Updating",
            d->Desc[0] ? d->Desc : d->Id);
        retried = 0;
call:
        reboot = FALSE;
        logonly("\nUpdateDriverForPlugAndPlayDevices(\"%s\", \"%s\", %s)\n",
                hwid, path, flags ? "FORCE" : "0");
        if (api.UpdateDriver(front.Owner, hwid, path, flags, &reboot)) {
            out("done%s.\n", reboot ? ", restart needed" : "");
            if (inst_parent_kind(d->Id, is_controller(d))) {
                parentJobs++;
            }
            flagChanged = 1;
            if (flags == 0 && inf == INST_INF_CMF) {
                /* The filter's first install on Windows 2000 goes
                 * unforced (call_flags): the case read on the 2000 leg. */
                tunnelChanged = 1;
            }
            for (j = 0; j < n; j++) {
                changedState[callIdx[j]] = 2;   /* the call succeeded */
                if (reboot) {
                    changedReboot[callIdx[j]] = 1;
                }
                if (inf == INST_INF_S98) {
                    s98_pending_mark(changedId[callIdx[j]], "done");
                }
            }
            if (reboot) {
                flagRestart = 1;
            }
            /* A root hub's or hub's update restarts it and takes the
             * devices below it away for seconds: the next call waits for
             * them (98 SE leg, 2026-10-19; record 23 section 5.8). */
            if (controllers != 1 &&
                inst_settle_after_job(inst_parent_kind(d->Id, 0),
                                      INST_DI_OUT_OK,
                                      nt_more_after(i, inf, controllers))) {
                if (!reappear_settle(d->Id)) {
                    return -1;
                }
            }
            continue;
        }
        err = GetLastError();
        if (inf == INST_INF_S98) {
            /* The call failed or was cancelled: no replacement to own. */
            for (j = 0; j < n; j++) {
                s98_pending_mark(changedId[callIdx[j]], NULL);
            }
        }
        /* Not Stor98: its call reaches every device of the id, and its gate
         * (the place, the shared file, the shared id) is read again only by
         * a fresh scan and plan, so a Stor98 drive waits for the next pass. */
        if (err == ERROR_NO_SUCH_DEVINST && inf != INST_INF_S98 &&
            inst_absent_retry(INST_DI_OUT_ABSENT, parentJobs, retried)) {
            out("not present at the moment.\n");
            retried = 1;
            if (!reappear_settle(d->Id)) {
                for (j = 0; j < n; j++) {
                    changedState[callIdx[j]] = 0;
                }
                return -1;
            }
            out("%s %s again ... ", d->Action == INST_ACT_INSTALL ?
                "Installing" : d->Action == INST_ACT_FILTER ?
                "Adding the filter to" : "Updating",
                d->Desc[0] ? d->Desc : d->Id);
            goto call;
        }
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
        if (inf == INST_INF_S98 && err == ERROR_NO_MORE_ITEMS) {
            out("  Windows ranks another driver it has for this drive higher "
                "than Stor98: it\n  keeps that one.\n");
        } else if (flags == 0 && err == ERROR_NO_MORE_ITEMS) {
            out("  Windows declined the filter: it ranks its own driver for "
                "the port higher, and\n  on Windows 2000 the filter's first "
                "install is never forced (/force does not\n  change that). "
                "The port keeps Windows' driver without the filter; Device\n"
                "  Manager's Have Disk on %s "
                "installs it by hand.\n", inst_cmf_where(INST_MY_ARCH));
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
        /* This package's controllers: the INF's (an install), or those
         * xhci98 runs (an uninstall, which reads no package). */
        if (!d->Present || !is_controller(d) ||
            (d->Inf != INST_INF_XHCI && !inst_eq(d->Service, "xhci98"))) {
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

/* The folder of Windows' own setup files on its CD, as 98 SE's and ME's
 * prompt names it for a D: drive (the prompt's default stays D:\WIN98
 * whatever the INF says). */
static const char *cd_folder(void)
{
    return envNow.Minor == 90 ? "D:\\WIN9X" : "D:\\WIN98";
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

    if (api.Locate == NULL || api.Child == NULL || api.Sibling == NULL ||
        api.DeviceId == NULL || api.Status == NULL) {
        if (api.WaitInstall == NULL) {
            return 1;
        }
        /* No tree to read: the pending question alone, as before. */
        w = api.WaitInstall(limitMs);
        return w == WAIT_OBJECT_0 ? 1 : (w == WAIT_TIMEOUT ? 0 : -1);
    }
    if (api.WaitInstall == NULL && envNow.Nt) {
        return 1;
    }
    inst_settle_init(&s, limitMs, tellMs, expectMs);
    t0 = GetTickCount();
    for (;;) {
        /* 98 SE and ME have no pending question to ask (record 27 section
         * 6): the tree alone, read until it stands still. */
        w = api.WaitInstall != NULL ? api.WaitInstall(0) : WAIT_OBJECT_0;
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
            } else if (!envNow.Nt) {
                out("\nWindows is installing the devices the controller shows; "
                    "answer its windows:\nNext, Next, Finish. Finish each one, "
                    "even when it finds no driver; never\ncancel one (a "
                    "cancelled one hides its device until Windows restarts).\n"
                    "Where one asks for a folder or for the \"xhci98 xHCI Host "
                    "Controller Disk\",\ntype %s\n", srcDir[0] ? srcDir :
                    toolDir);
                out("Where one asks for the Windows CD, insert it and type "
                    "%s (your CD drive's\nletter for D:), or the folder your "
                    "PC keeps Windows' setup files in (often\n"
                    "C:\\WINDOWS\\OPTIONS\\CABS).\n", cd_folder());
            } else {
                out("\nWindows is installing a device itself, usually with a "
                    "Found New Hardware\nwindow - it may be behind this one. "
                    "Answer it: the driver is staged, so let it\nsearch, "
                    "answer Yes or Continue to its questions, then Finish. "
                    "This tool waits\n(up to 10 minutes) and goes on by itself; "
                    "it never installs over Windows' own\ninstall.\n");
            }
        }
        if (!waiting(INST_WAIT_SETTLE, (unsigned long)(GetTickCount() - t0),
                     limitMs, envNow.Nt ? "Windows is installing a device it "
                     "found; answer the window it opened" : "Windows is "
                     "installing the devices the controller shows; answer "
                     "its windows")) {
            runCancelled = 1;
            flagSteps = 1;
            return -3;
        }
        Sleep(INST_SETTLE_POLL_MS);
    }
}

/* What a settle that did not end settled says. */
static void settle_failed(int r, const char *bound)
{
    if (r == -3) {
        out("cancelled.\n\nCancelled: the run stops here. What was done "
            "stays done; run the installer\nagain to go on.\n");
        flagSteps = 1;
        return;
    }
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
        "Hardware window\nwaiting for an answer. Finish those windows (Next "
        "through Finish, even when\nthey find no driver), then run "
        "INSTALL.BAT again: it goes on from here. (The\ntool does not install "
        "under a wizard.) If no such window is open (Windows\n2000 keeps a "
        "cancelled one pending until the next start), restart Windows\n"
        "first.\n");
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
           inst_ver_cmp(d->Version, inf_version(inf)) == 0 &&
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
        if ((d == NULL || !d->Present) && s98_awaiting(changedId[c])) {
            out("  %s: Stor98 updated, awaiting reconnection - plug the drive "
                "in again, then\n  run the installer again to verify it.\n",
                changedId[c]);
            flagSteps = 1;
            continue;
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
        if (s98_awaiting(changedId[c])) {
            /* Record 26 section 6.3: neither a success nor a failure until
             * the drive has been unplugged and plugged in again. */
            out("  %s: Stor98 updated, awaiting reconnection - unplug the "
                "drive, plug it in\n  again, then run the installer again to "
                "verify it.\n", d->Desc[0] ? d->Desc : d->Id);
            flagSteps = 1;
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

static void testsigning_note0(void);
static void testsigning_note_cm(void);

/* Record 26 section 6.3: with Stor98 ticked, its x64 driver is signed in
 * the package's one signing step, by the same certificate as the drivers
 * above (since 3.1.0.0; Stor98 0.4.0.0 and earlier carried a test
 * signature of their own), so it loads as they do. No certificate is
 * imported. */
static void testsigning_note(void)
{
    testsigning_note0();
    if (INST_MY_ARCH == INST_ARCH_AMD64 && envNow.Major >= 6 && s98Include) {
        out("stor98.sys, the bundled Stor98, is signed (or not) with the "
            "drivers above, by\nthe same certificate: it loads as they "
            "do.\n\n");
    }
}

static void testsigning_note0(void)
{
    char opt[512];

    testSigningConfirmed = 0;
    if (INST_MY_ARCH != INST_ARCH_AMD64 || envNow.Major < 6) {
        return;
    }
    if (!usbInclude) {
        /* No kernel driver in a tools-only run (record 26 section 5.3.1
         * case 5): nothing to sign. */
        if (haveCm && tbInclude) {
            testsigning_note_cm();
        }
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
            "load them only on a start\nwhere F8 chose \"Disable Driver "
            "Signature Enforcement\"; test-signing mode is\nnot enough. "
            "Otherwise the controller shows Code 52 on 7, Code 39 on Vista. "
            "If\nthis start is such an F8 start, go on.\n\n");
        return;
    }
    if (testSigningConfirmed) {
        out("Test-signing mode is on: the test-signed x64 drivers load.\n\n");
        return;
    }
    out("NOTE: test-signing mode could not be confirmed. The x64 drivers are\n"
        "test-signed: they load in test-signing mode (an elevated Command "
        "Prompt:\nbcdedit /set testsigning on  then restart), or on a start "
        "where F8 chose\n\"Disable Driver Signature Enforcement\". Otherwise the "
        "controller shows Code 52\non 7, Code 39 on Vista. This tool does not "
        "change it (the package's readme.txt,\n\"WINDOWS VISTA X64 AND "
        "WINDOWS 7 X64: TEST-SIGNING MODE\"). If this start is\n"
        "an F8 start, go on.\n\n");
}

/* The note for the connection manager alone (record 23 section 5.10,
 * revision 13): the binary read is xhcicm.sys, the device that shows the
 * code the NHI, and on 8 and 8.1 the Secure Boot condition. */
static void testsigning_note_cm(void)
{
    char opt[512];
    int w8 = envNow.Major == 6 && envNow.Minor >= 2;

    if (!stScreens &&
        reg_text(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control",
                 "SystemStartOptions", opt, sizeof(opt), NULL)) {
        logonly("SystemStartOptions: %s\n", opt);
        if (inst_testsigning(opt)) {
            testSigningConfirmed = 1;
        }
    }
    if (pkgCmSigned < 0) {
        testSigningConfirmed = 0;
        out("NOTE: whether the x64 connection manager (xhcicm.sys) carries "
            "a\nsignature could not be read. 64-bit Windows loads a test-signed "
            "driver\nonly in test-signing mode, and an unsigned one only on a "
            "start with\ndriver signature enforcement disabled.\n");
    } else if (pkgCmSigned == 0) {
        testSigningConfirmed = 0;
        out("NOTE: the x64 connection manager (xhcicm.sys) in this package "
            "carries no\nsignature (built without the project's test-signing "
            "key). 64-bit Windows\nloads it only on a start with driver "
            "signature enforcement disabled;\ntest-signing mode is not "
            "enough.\n");
    } else if (testSigningConfirmed) {
        out("Test-signing mode is on: the test-signed x64 connection manager "
            "loads.\n\n");
        return;
    } else {
        out("NOTE: test-signing mode could not be confirmed. The x64 "
            "connection manager\n(xhcicm.sys) is test-signed: it loads in "
            "test-signing mode (an elevated\nCommand Prompt: bcdedit /set "
            "testsigning on  then restart), or on a start\nwith driver "
            "signature enforcement disabled. This tool does not change "
            "it.\n");
    }
    if (w8) {
        out("Windows 8 and 8.1: test-signing mode can be turned on only with "
            "Secure Boot\noff in the computer's firmware setup. The start "
            "without signature enforcement\nis Advanced startup, Troubleshoot, "
            "Startup Settings, \"Disable driver\nsignature enforcement\", at "
            "every start.\n");
    }
    out("Otherwise the Thunderbolt or USB4 controller shows a problem code "
        "(52 on 7).\nIf this start is one without signature enforcement, go "
        "on.\n\n");
}

static int confirm(const char *what)
{
    char line[16];
    char question[1024];
    char text[2048];
    int r;
    int k;

    if (opts.Yes) {
        out("%s - going on (/yes).\n\n", what);
        return 1;
    }
    /* Owner, 2026-10-16: Enter goes on. End of input (no console to
     * answer) still cancels. */
    _snprintf(question, sizeof(question) - 1, "%s?", what);
    question[sizeof(question) - 1] = '\0';
    inst_yesno_question(text, sizeof(text), question, "go on",
                        "cancel: nothing is changed", 1);
    for (k = 0;; k++) {
        if (!ask(INST_Q_CONFIRM, k == 0 ? text : "  Type Y or N, or Enter "
                 "for [Y]: ", k > 0, 0, 1, NULL, line, sizeof(line))) {
            out("\nNo answer: cancelled, nothing changed.\n");
            return 0;
        }
        r = inst_tb_answer(line, 1);
        if (r >= 0) {
            break;
        }
    }
    if (r == 1) {
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
    if (r == -3) {
        out("Cancelled: nothing was changed.\n");
        return 0;
    }
    if (r == -2) {
        out("The devices kept changing, or could not be read whole, for a "
            "minute. Restart\nWindows, then run INSTALL.BAT again. Nothing was "
            "changed.\n");
        flagSteps = 1;
        return 0;
    }
    out("Windows is %s - usually a Found New Hardware window\nwaiting for an "
        "answer. Finish it (Next through Finish), then run INSTALL.BAT\n"
        "again. If no such window is open (Windows 2000 keeps a cancelled one "
        "pending\nuntil the next start), restart Windows first. Nothing was "
        "changed.\n",
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
                  inst_ver_cmp(d->Version, group_version(g)) > 0))) {
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
        /* 98 SE and ME: a Stor98 drive no helper job touches may be
         * unplugged after the plan, as the file update's confirmation
         * asks (record 26 section 6.3, draft 16); the plan made again
         * reads the gate again. */
        if (!envNow.Nt && d->Inf == INST_INF_S98 && !d->Runs) {
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

/* INST_PH_PLAN on NT (record 26 section 3.3): the /restage journal, the
 * store trap read afresh, the plan for the choices as they stand, and
 * its text. -1 to go on, or the refusal's code. */
static int plan_nt(void)
{
    int k;
    INST_U32 tag;
    char name[INST_PATH_CHARS];

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
        if (usbInclude && !uasInclude && restagePending[INST_INF_UAS]) {
            out("REFUSED: the unfinished /restage includes the UAS disk "
                "driver.\n%s\n", instWindow ? "Tick it on the Components "
                "page to finish it." : "Run it again without /nouas to "
                "finish it.");
            return INST_EXIT_REFUSED;
        }
        if (!usbInclude &&
            (restagePending[INST_INF_XHCI] || restagePending[INST_INF_UAS])) {
            out("REFUSED: the unfinished /restage includes the USB "
                "drivers.\n%s\n", instWindow ? "Tick the xHCI98 USB 3.x "
                "drivers on the Components page to finish it." : "Run it "
                "again without /cmonly to finish it.");
            return INST_EXIT_REFUSED;
        }
        if (haveCm && !tbInclude &&
            (restagePending[INST_INF_CM] || restagePending[INST_INF_CMF])) {
            out("REFUSED: the unfinished /restage includes the Thunderbolt "
                "driver. %s\n", instWindow ?
                "Tick Thunderbolt 3 / USB4\nsupport on the Components page "
                "to finish it." : "Answer Y to\nthe Thunderbolt question (or "
                "use /thunderbolt) to finish it.");
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
    memset(refusalSeen, 0, sizeof(refusalSeen));
    note_refusals(1);
    testsigning_note();
    return -1;
}

static int planned_runs(void)
{
    int i;
    int runs = 0;

    for (i = 0; i < devCount; i++) {
        runs += devs[i].Runs;
    }
    return runs + (s98FileUpd ? 1 : 0);
}

/* INST_PH_RUN on NT, after the confirmation. */
static int run_install_nt(void)
{
    int k;
    int i;
    int pass;
    INST_U32 before;

    before = devs_signature();
    if (!nothing_pending() || !plan_still_holds(before)) {
        return inst_exit_code(0, flagFailed, flagRestart, flagSteps);
    }
    if (!s98_baseline()) {
        return inst_exit_code(0, 1, flagRestart, flagSteps);
    }
    step(INST_STEP_BEGIN, "Copying the package to %%windir%%\\XHCI98");
    /* The record's PreExisting goes with its first entry (a staged copy,
     * a device, a setting; s98_rec_put): no record in a run that makes no
     * Stor98 change (record 26 section 6.3, draft 16). */
    if (!make_local_copy() || !stage_nt()) {
        step(INST_STEP_FAILED, "The package's copy and staging");
        flagFailed = 1;
        return inst_exit_code(0, 1, flagRestart, flagSteps);
    }
    step(INST_STEP_DONE, "The package is copied and staged");
    /* The controllers; one that met Windows' own install (busy) is tried
     * again after a barrier, as section 5.8's devices are. */
    for (pass = 0; pass < INST_BUSY_TRIES; pass++) {
        if (pass > 0 && !barrier()) {
            goto stopped;
        }
        if (!any_runs(INST_INF_XHCI, 1)) {
            break;
        }
        if (!go_on()) {
            goto stopped;
        }
        i = install_inf(INST_INF_XHCI, 1);
        if (i < 0) {
            goto stopped;
        }
        if (i == 0) {
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
            if (!go_on()) {
                goto stopped;
            }
            if (k == INST_INF_S98) {
                /* Before every Stor98 pass: a drive a settle brought is
                 * read too. */
                s98_tunnel_mark();
            }
            if (install_inf(k, k == INST_INF_XHCI ? 0 : -1) < 0) {
                goto stopped;
            }
        }
    }
    s98_reconnect_nt();
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
    s98_write_values();
    check_results();
    s98_record_devices();
    s98_final_unknown();
    s98_record_version();
    restage_settle();
    if (!restage_write()) {
        out("The /restage work file could not be updated.\n");
        flagFailed = 1;
    }
    move_ready();
    if (begin_move()) {
        tray_install();
        own_install();
        stor_tools_copy();
        old_dir_cleanup();
        arp_write(0);
    }
    if (flagRestart) {
        /* The second sentence only where it can apply (the 2000 leg read
         * it after a settings-only run). */
        out("\nRESTART Windows to finish.%s\n",
            (usbInclude && envNow.Major == 6 &&
             INST_MY_ARCH == INST_ARCH_X86) ?
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
        s98_record_devices();
    }
    out("\nStopped before every device was done: run INSTALL.BAT again to go "
        "on.\n");
    return inst_exit_code(0, flagFailed, flagRestart, flagSteps);
}
/* ======================================================================== */
/* 98 SE and ME (record 23 section 6)                                        */
/* ======================================================================== */

#define INST_REG_CV "Software\\Microsoft\\Windows\\CurrentVersion"

/* DevicePath (record 23 sections 6.2 and 9.6): every entry under
 * %windir%\XHCI98\ - an earlier build's copy - and every copy directly in
 * this run's or the earlier install directory taken out, then addDir
 * appended when it is not NULL. No other entry is touched, and the value
 * keeps its type. */
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
    int i;
    LONG w;

    if (!legacy_dir(rootDir, sizeof(rootDir)) ||
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
    for (i = 0; i < 2; i++) {
        const char *d = i == 0 ? toolDir : oldToolDir;
        if (d[0] == '\0') {
            continue;
        }
        inst_copy(newList, mid, sizeof(newList));
        if (!inst_eq(d, rootDir) &&
            inst_path_remove_copies(newList, d, mid, sizeof(mid)) < 0) {
            out("DevicePath is too long to change.\n");
            return 0;
        }
        /* The install directory itself, where 98 SE and ME keep the copy
         * since the owner's decision of 2026-10-16. */
        inst_copy(newList, mid, sizeof(newList));
        if (inst_path_remove(newList, d, mid, sizeof(mid)) < 0) {
            out("DevicePath is too long to change.\n");
            return 0;
        }
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
    out("DevicePath now reads (one entry a line):\n");
    {
        char *p = newList;
        while (*p != '\0') {
            char *semi = strchr(p, ';');
            if (semi != NULL) {
                *semi = '\0';
            }
            if (*p != '\0') {
                out("  %s\n", p);
            }
            if (semi == NULL) {
                break;
            }
            p = semi + 1;
        }
    }
    flagChanged = 1;
    return 1;
}
/* 1 when the reading holds an xHCI controller: a devnode, or an Enum\PCI
 * entry with no driver (record 23 section 6.3). */
static int controller_listed(void)
{
    int i;

    for (i = 0; i < devCount; i++) {
        if (is_controller(&devs[i])) {
            return 1;
        }
    }
    return 0;
}

/* The hand steps on 98 SE and ME (record 23 section 6.4; owner,
 * 2026-10-16): Windows there cannot be asked to put the driver on a
 * device, so each device that needs it gets a short numbered list, one
 * action a line, with the folder to type spelled out. Returns 1 when a
 * device needs Update Driver (the Device Manager question follows). */
static int steps_9x(const char *dir)
{
    int i;
    int any = 0;
    int needs = 0;
    int old23;
    INST_U32 bv[4];
    char bin[INST_PATH_CHARS];
    static const INST_U32 v24[4] = { 2, 4, 0, 0 };

    /* The 2.3.x keyboard-and-mouse advice: 98 SE only, and only over an
     * installed xhci98.sys older than 2.4.0.0. */
    group_binary(0, bin, sizeof(bin));
    old23 = envNow.Minor == 10 && file_version(bin, bv) > 0 &&
            inst_ver_cmp(bv, v24) < 0;

    if ((s98FileUpd || s98Declined) && !s98FileDone && !opts.List) {
        out("\nStor98\n  Its driver file was not updated in this run. "
            "Disconnect every external NVMe\n  drive, run the installer "
            "again and confirm it; never use Update Driver on\n  a drive "
            "Stor98 runs.\n");
    }
    if (helperRan && stepsCount == 0) {
        /* The helper did every device (record 26 section 7): only the
         * storage advice stays. */
        if (envNow.Minor == 10 && !stor_covered()) {
            out("\nOn Windows 98 SE any disk, UAS included, needs the USB "
                "storage drivers: the\ninstaller's row for them, or NUSB's "
                "mass-storage component.\n");
        }
        if (envNow.Minor == 90 && !ntmap_present()) {
            out("On this Windows ME, plug in an ordinary USB stick once before "
                "the first UAS\ndisk: ME copies its own USB storage files "
                "(USBNTMAP.SYS) only then.\n");
        }
        return 0;
    }
    stepsText[0] = '\0';
    stepsCapture = 1;
    out("\nWhat to do now\n");
    out("  The folder to type when Windows asks:  %s\n", dir);
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        int fix;
        if (!d->Present || d->Inf < 0) {
            continue;
        }
        /* After the helper, only the devices it could not do. */
        if (helperRan && steps_why(d->Id) == NULL) {
            continue;
        }
        if (d->Inf == INST_INF_S98 && d->S98Why != INST_S98_ELIGIBLE) {
            continue;   /* left by the gate: its reason is said */
        }
        if (d->Inf == INST_INF_S98 && inst_eq(d->Service, "stor98")) {
            /* Never Update Driver over a running Stor98 here (the fatal
             * exception 0E of 2026-10-19): its file is the installer's
             * to replace, said below. A job of this run cut short keeps
             * its steps. */
            if (helperRan && steps_why(d->Id) != NULL) {
                any = 1;
                out("\n%s\n  The helper could not install it: %s.\n"
                    "  Shut down Windows and start it again, then run the "
                    "installer again; never\n  use Update Driver on a drive "
                    "Stor98 runs.\n", d->Desc[0] ? d->Desc : d->Id,
                    steps_why(d->Id));
            }
            continue;
        }
        fix = uas_fix(d);
        /* The controller and, for an update, its root hub: the release
         * notes' update is Update Driver on both (the 9x leg, 2026-10-15:
         * the root hub otherwise stays at the old release). Hubs and
         * composite entries follow by themselves - unless the helper ran
         * and could not install one, which then has its own steps. */
        if (!helperRan && d->Inf == INST_INF_XHCI && !is_controller(d) &&
            !(d->Action == INST_ACT_UPDATE && inst_bus_id(d->Id) == 1 &&
              inst_starts(d->Id, "USB\\XHCI98_ROOT_HUB"))) {
            continue;
        }
        any = 1;
        out("\n%s\n", d->Desc[0] ? d->Desc : d->Id);
        if (helperRan) {
            out("  The helper could not install it: %s.\n", steps_why(d->Id));
        }
        if (d->RegOnly) {
            out("  Recorded, but not running now. If Device Manager lists it "
                "under \"Other\n  devices\", do the steps below; if not, it is "
                "an old entry (the device\n  moved) and needs nothing. After a "
                "cancelled New Hardware wizard, restart:\n  the wizard comes "
                "back and finds the driver by itself; finish it (Next\n  "
                "through Finish, never Cancel).\n");
        }
        if (d->Refused) {
            out("  Left: its version could not be compared.\n");
            print_uas_fix(fix, "  - ");
            continue;
        }
        if (helperRan && (d->Action == INST_ACT_CURRENT ||
                          d->Action == INST_ACT_NEWER) &&
            inst_starts(steps_why(d->Id),
                        inst_di_verdict_text(INST_DI_V_NONE))) {
            /* Its install was cut short: what the reading shows now is
             * not a finished install (98 SE update, 2026-10-19). */
            needs = 1;
            out("  Its install did not finish. Shut down Windows and start it "
                "again, then run\n  the installer again: it checks this "
                "device and finishes what is left.\n");
            continue;
        }
        switch (d->Action) {
        case INST_ACT_INSTALL:
            needs = 1;
            out("  1. Device Manager: double-click it (under \"Other devices\" "
                "if it has no\n     driver), Driver tab, Update Driver, Next.\n"
                "  2. \"Search for the best driver\", Next.\n"
                "  3. Untick every box, Next, Next, Finish.\n"
                "  4. If it finds nothing: \"Specify a location\", type %s\n"
                "  5. Asked for the \"xhci98 xHCI Host Controller Disk\": "
                "type %s\n     under \"Copy files from\", OK.\n"
                "  6. Asked for the Windows CD (Windows' own files, usbd.sys): "
                "insert it and\n     type %s (your CD drive's letter for D:), "
                "or the folder your PC\n     keeps Windows' setup files in "
                "(often C:\\WINDOWS\\OPTIONS\\CABS), OK.\n"
                "  7. Restart when asked; on ME restart anyway.\n", dir, dir,
                cd_folder());
            break;
        case INST_ACT_UPDATE:
            needs = 1;
            if (d->Inf == INST_INF_UAS) {
                /* A search keeps an xhciuas.sys of the same DriverVer. */
                out("  1. Device Manager: double-click this disk, Driver tab, "
                    "Update Driver, Next.\n"
                    "  2. \"Display a list of all the drivers in a specific "
                    "location\", Next.\n"
                    "  3. Have Disk, type %s, OK.\n"
                    "  4. Asked for the \"xhci98 xHCI Host Controller Disk\": "
                    "type %s\n     under \"Copy files from\", OK.\n"
                    "  5. Restart afterwards, even if Windows does not ask.\n",
                    dir, dir);
                break;
            }
            out("  1. Device Manager: double-click it, Driver tab, Update "
                "Driver, Next.\n"
                "  2. \"Search for a better driver\", Next.\n"
                "  3. Untick every box, Next, Next, Finish.\n"
                "  4. If it keeps the old driver: \"Specify a location\", "
                "type %s\n"
                "  5. Asked for the \"xhci98 xHCI Host Controller Disk\": "
                "type %s\n     under \"Copy files from\", OK.\n"
                "  6. Restart afterwards, even if Windows does not ask.\n",
                dir, dir);
            if (envNow.Minor == 90 && !is_controller(d)) {
                out("  NOTE: on ME a \"Select Other Driver\" list may name this "
                    "driver twice (one\n  copy under INF\\OTHER): either is "
                    "the same driver.\n");
            }
            if (d->Inf == INST_INF_XHCI && is_controller(d) && old23) {
                out("  NOTE: from 2.3.x or earlier, have a keyboard and mouse "
                    "that do not go\n  through this driver first (release "
                    "notes, \"From 2.3.x or earlier\").\n");
            }
            break;
        case INST_ACT_NEWER:
            out("  Nothing to do: a newer release of this driver is "
                "installed.\n");
            break;
        case INST_ACT_CURRENT:
            if (opts.Force) {
                needs = 1;
                /* A search keeps a driver of the same DriverVer. */
                out("  /force:\n"
                    "  1. Device Manager: double-click it, Driver tab, Update "
                    "Driver, Next.\n"
                    "  2. \"Display a list of all the drivers in a specific "
                    "location\", Next.\n"
                    "  3. Have Disk, type %s, OK.\n"
                    "  4. Restart afterwards, even if Windows does not ask.\n",
                    dir);
            } else if (fix == INST_UASFIX_NONE) {
                out("  Nothing to do: this release runs it.\n");
            }
            break;
        default:
            out("  Another driver runs it; this tool leaves it.\n");
            break;
        }
        print_uas_fix(fix, "  - ");
    }
    /* A device the helper reached that is not present now (record 26
     * section 7): its own line, with what to do. */
    for (i = 0; helperRan && i < stepsCount; i++) {
        int j;
        int seen = 0;
        for (j = 0; j < devCount; j++) {
            if (devs[j].Present && inst_eq(devs[j].Id, stepsId[i])) {
                seen = 1;
            }
        }
        if (!seen) {
            any = 1;
            out("\n%s\n  %s. Connect it again or restart Windows, then run "
                "the installer again.\n", stepsId[i], stepsWhy[i]);
        }
    }
    if (!any) {
        out("  No device of this package is present.\n");
        if (!controller_listed()) {
            /* 43.5's first leg: the boot's wizard for the controller was
             * cancelled, and the reading had no entry of it at all (record
             * 26 section 7, draft 19). */
            out("  No USB 3 controller is listed by Windows now. If its New "
                "Hardware wizard was\n  cancelled at this start, restart: "
                "the wizard comes back; finish it (Next\n  until Finish; "
                "where it asks for the xHCI98 driver disk, type %s,\n  and "
                "where it asks for a Windows file, the Windows CD or the "
                "folder of its\n  setup files), then run the installer "
                "again.\n", dir);
        }
    }
    /* The Thunderbolt half only when this run copied its INF (section 8.5). */
    out("\nThen run INSTALL.BAT again, to check the result and set the "
        "driver's options.\n"
        "A UAS disk%s found later finds its driver\nin the same folder by "
        "itself.\n",
        (haveCm && tbInclude) ? " or a Thunderbolt controller" : "");
    if (envNow.Minor == 10 && !stor_covered()) {
        out("On Windows 98 SE any disk, UAS included, needs the USB storage "
            "drivers: the\ninstaller's row for them, or NUSB's mass-storage "
            "component.\n");
    }
    if (envNow.Minor == 90 && !ntmap_present()) {
        out("On this Windows ME, plug in an ordinary USB stick once before the "
            "first UAS\ndisk: ME copies its own USB storage files "
            "(USBNTMAP.SYS) only then, and a\nUAS disk plugged in first shows "
            "Code 2 until it is unplugged and plugged\nback in.\n");
    }
    stepsCapture = 0;
    return needs;
}

/* After the steps (owner, 2026-10-16): System Properties' Device Manager
 * tab opened, if the user wants it, without waiting for it. Never under
 * /yes, /defaults, /list or /nopause, which are for runs nobody watches. */
static void open_device_manager(int needed)
{
    char line[16];
    char text[1024];
    char cmd[64];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    int r;
    int k;

    if (!needed || envNow.Nt || opts.Yes || opts.Defaults || opts.List ||
        opts.NoPause) {
        return;
    }
    out("\n");
    inst_yesno_question(text, sizeof(text), "Open Device Manager now?",
                        "open it (System Properties, Device Manager tab)",
                        "not now", 1);
    for (k = 0;; k++) {
        if (!ask(INST_Q_DEVMGR, k == 0 ? text : "  Type Y or N, or Enter "
                 "for [Y]: ", k > 0, 0, 1, NULL, line, sizeof(line))) {
            out("\n");
            return;
        }
        r = inst_tb_answer(line, 1);
        if (r >= 0) {
            break;
        }
    }
    if (r == 0) {
        return;
    }
    if (stScreens) {
        out("(self-test: Device Manager not opened)\n");
        return;
    }
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    inst_copy(cmd, "control.exe sysdm.cpl,,1", sizeof(cmd));
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        logonly("started: %s\n", cmd);
        out("Device Manager is opening. Do the steps above there.\n");
    } else {
        out("Device Manager could not be opened (error %lu): Control Panel, "
            "System,\nDevice Manager tab.\n", (unsigned long)GetLastError());
    }
}

/* ======================================================================== */
/* 98 SE and ME: the helper XHCIDI.EXE (record 26 section 7, record 27)      */
/* ======================================================================== */


static void steps_add_id(const char *id, const char *why)
{
    int i;

    for (i = 0; i < stepsCount; i++) {
        if (inst_eq(stepsId[i], id)) {
            return;
        }
    }
    if (stepsCount >= INST_MAX_STEPDEVS) {
        return;
    }
    inst_copy(stepsId[stepsCount], id, INST_ID_CHARS);
    inst_copy(stepsWhy[stepsCount], why, sizeof(stepsWhy[0]));
    stepsCount++;
}

static void steps_add(const DEVREC *d, const char *why)
{
    steps_add_id(d->Id, why);
}

/* The why of a device on the steps list, or NULL. */
static const char *steps_why(const char *id)
{
    int i;

    for (i = 0; i < stepsCount; i++) {
        if (inst_eq(stepsId[i], id)) {
            return stepsWhy[i];
        }
    }
    return NULL;
}

/* The helper's process ended (WaitForSingleObject's answer) or not, the
 * result file read at each tick (record 27 section 4.1). */
static char *helper_file(void)
{
    return read_file(helperResult, NULL);
}

/* One job (record 27 section 4.1): the result file made empty, the
 * helper started with the job on its command line, and one wait loop -
 * the process handle with a one-second timeout and the file's END line
 * at each tick. Returns the verdict (r filled); INST_DI_V_NONE when the
 * helper did not finish. A process still alive 60 s after its END line
 * ends the helper's part of this run (helperState -1). */
static int helper_job(int job, const DEVREC *d, const char *inf, int force,
                      INST_DI_RESULT *r)
{
    char tail[INST_LINE_CHARS];
    char cmd[INST_LINE_CHARS + INST_PATH_CHARS];
    char jobText[INST_DI_JOB_BYTES + 1];
    unsigned long owner;
    DWORD wrote = 0;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE h;
    DWORD t0;
    DWORD endAt = 0;
    DWORD w = WAIT_TIMEOUT;
    DWORD code = 0;
    char *text = NULL;
    char what[256];
    int ended = 0;
    int stuck = 0;
    int waitFailed = 0;
    int verdict;
    const char *id = d != NULL ? d->Id : NULL;

    memset(r, 0, sizeof(*r));
    /* The window's handle owns the helper's dialogs; on 9x a window handle
     * is 16 bits, and anything else is passed as 0, no owner. */
    owner = (unsigned long)(size_t)front.Owner;
    if (owner > 65535UL) {
        owner = 0;
    }
    if (!inst_di_jobfile(jobText, sizeof(jobText), job, helperResult, owner,
                         id, inf, force, 1) ||
        !inst_di_cmdline(tail, sizeof(tail), XHCIINST_VERSION,
                         helperJobFile)) {
        logonly("helper: the job cannot be written (%s %s, result %s, job "
                "file %s)\n", id != NULL ? id : "probe",
                inf != NULL ? inf : "", helperResult, helperJobFile);
        r->Verdict = INST_DI_V_NONE;
        inst_copy(r->Detail, "the job could not be written",
                  sizeof(r->Detail));
        return INST_DI_V_NONE;
    }
    h = CreateFileA(helperJobFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE ||
        !WriteFile(h, jobText, (DWORD)strlen(jobText), &wrote, NULL) ||
        wrote != (DWORD)strlen(jobText)) {
        out("The helper's job file %s could not be written (error %lu).\n",
            helperJobFile, (unsigned long)GetLastError());
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            DeleteFileA(helperJobFile);
        }
        inst_copy(r->Detail, "no job file", sizeof(r->Detail));
        return INST_DI_V_NONE;
    }
    CloseHandle(h);
    logonly("helper's job file %s:\n%s", helperJobFile, jobText);
    h = CreateFileA(helperResult, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        out("The helper's result file %s could not be made (error %lu).\n",
            helperResult, (unsigned long)GetLastError());
        DeleteFileA(helperJobFile);
        inst_copy(r->Detail, "no result file", sizeof(r->Detail));
        return INST_DI_V_NONE;
    }
    CloseHandle(h);
    _snprintf(cmd, sizeof(cmd) - 1, "%s %s", helperPath, tail);
    cmd[sizeof(cmd) - 1] = '\0';
    logonly("helper: %s\n", cmd);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si,
                        &pi)) {
        out("The helper %s could not be started (error %lu).\n", helperPath,
            (unsigned long)GetLastError());
        DeleteFileA(helperJobFile);
        inst_copy(r->Detail, "it did not start", sizeof(r->Detail));
        return INST_DI_V_NONE;
    }
    CloseHandle(pi.hThread);
    _snprintf(what, sizeof(what) - 1, "Windows' device installer is %s %s; a "
              "Windows dialog may be waiting behind this window",
              job == INST_DI_REMOVE ? "removing" :
              job == INST_DI_INSTALL ? "installing" : "starting for",
              d != NULL ? (d->Desc[0] ? d->Desc : d->Id) : "this system");
    what[sizeof(what) - 1] = '\0';
    t0 = GetTickCount();
    for (;;) {
        /* The engine runs on the window's worker thread, which owns no
         * window: a plain wait with a timeout keeps the window's own
         * thread free to draw (record 27 section 4.1's loop). */
        w = WaitForSingleObject(pi.hProcess, 1000);
        free(text);
        text = helper_file();
        ended = text != NULL && inst_di_ended(text);
        if (w != WAIT_TIMEOUT) {
            break;
        }
        if (ended) {
            if (endAt == 0) {
                endAt = GetTickCount();
            } else if (GetTickCount() - endAt > 60000UL) {
                stuck = 1;
                break;
            }
        }
        /* A started job finishes: a cancel is heard at the next step. */
        if (!waiting(INST_WAIT_HELPER, (unsigned long)(GetTickCount() - t0),
                     0, what) && !runCancelled) {
            runCancelled = 1;
            flagSteps = 1;
            out("Cancel heard: the device under way is finished first.\n");
        }
    }
    if (w == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code)) {
        logonly("helper: ended, exit code %lu\n", (unsigned long)code);
    } else if (stuck) {
        logonly("helper: its END line has been there 60 s and the process "
                "has not ended\n");
    } else {
        logonly("helper: the wait ended with %lu (error %lu)\n",
                (unsigned long)w, (unsigned long)GetLastError());
    }
    if (w != WAIT_OBJECT_0 && !stuck) {
        /* Only the handle's signal shows the helper ended: no further job
         * is started, and the job is not taken as done. */
        stuck = 1;
        waitFailed = 1;
    }
    CloseHandle(pi.hProcess);
    if (!stuck) {
        /* The helper has ended: its job file is done with. A helper still
         * running keeps it, and the run's end deletes it. */
        DeleteFileA(helperJobFile);
    }
    if (text != NULL) {
        logonly("helper's result file:\n%s%s", text,
                (strlen(text) > 0 && text[strlen(text) - 1] == '\n') ? "" :
                "\n");
    }
    verdict = inst_di_parse(text, XHCIINST_VERSION, job, id, inf, r);
    free(text);
    if (waitFailed) {
        verdict = INST_DI_V_NONE;
        r->Verdict = verdict;
        inst_copy(r->Detail, "the wait for the helper failed",
                  sizeof(r->Detail));
    }
    if (stuck) {
        /* No job is started while the end of the one before cannot be
         * seen (record 27 section 4.1). */
        helperState = -1;
        out("The helper's process did not end after its work was done: no "
            "further device\nis given to it in this run.\n");
    } else if (w == WAIT_OBJECT_0 && !ended) {
        verdict = INST_DI_V_NONE;
    }
    return verdict;
}

/* The helper found and probed once per run (record 27 section 4.1): 1
 * usable. A package without it, a probe that fails or a process handle
 * that does not signal keep it out of this run, said once; every device
 * then gets the printed steps. */
static int helper_ready(void)
{
    char self[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char *slash;
    INST_DI_RESULT r;
    DWORD n;
    int v;

    if (helperState != 0) {
        return helperState > 0;
    }
    helperState = -1;
    if (stScreens) {
        return 0;
    }
    if (GetModuleFileNameA(NULL, self, sizeof(self)) == 0) {
        return 0;
    }
    slash = strrchr(self, '\\');
    if (slash == NULL) {
        return 0;
    }
    slash[1] = '\0';
    join(path, sizeof(path), self, INST_DI_NAME);
    if (!file_exists(path, NULL)) {
        out("This package has no %s, the helper that installs each device: "
            "the steps for\neach device are printed instead.\n", INST_DI_NAME);
        return 0;
    }
    n = GetShortPathNameA(path, helperPath, sizeof(helperPath));
    if (n == 0 || n >= sizeof(helperPath) || strchr(helperPath, ' ') != NULL) {
        out("The helper's path %s has no short form without spaces: the "
            "steps for each\ndevice are printed instead.\n", path);
        return 0;
    }
    /* The result and job files beside the installer, by the directory's
     * short name: the helper is 16-bit and reads 8.3 paths only. */
    n = GetShortPathNameA(toolDir[0] ? toolDir : srcDir, path, sizeof(path));
    if (n == 0 || n >= sizeof(path)) {
        inst_copy(path, toolDir[0] ? toolDir : srcDir, sizeof(path));
    }
    join(helperResult, sizeof(helperResult), path, INST_DI_RESULT_NAME);
    if (!inst_di_path(helperResult) ||
        !inst_di_jobpath(helperJobFile, sizeof(helperJobFile), path)) {
        out("The directory %s has no short form the helper can read: the "
            "steps for each\ndevice are printed instead.\n", path);
        helperResult[0] = '\0';
        helperJobFile[0] = '\0';
        return 0;
    }
    out("Starting the helper %s to check it runs here ...\n", INST_DI_NAME);
    out_flush();
    helperState = 1;
    v = helper_job(INST_DI_PROBE, NULL, NULL, 0, &r);
    if (v != INST_DI_V_PROBED || helperState < 0) {
        helperState = -1;
        out("The helper does not run here (%s%s%s): the steps for each device "
            "are\nprinted instead.\n", inst_di_verdict_text(v),
            r.Detail[0] ? ": " : "", r.Detail);
        return 0;
    }
    out("The helper runs; it installs each device through Windows' own "
        "device installer.\n");
    return 1;
}

/* The gate before a pass (record 27 section 6): the tree settled, then
 * the user's word that no hardware wizard is open. 1 go on, 0 stop. */
static int gate_9x(int first)
{
    char line[16];
    int r;
    int k;

    if (!first) {
        out("Waiting for the devices to settle ... ");
        r = settle(INST_INSTALL_WAIT_MS, 20000UL, 20000UL, 0);
        if (r <= 0) {
            settle_failed(r, "10 minutes");
            return 0;
        }
        out("done.\n");
    }
    if (opts.Yes) {
        return go_on();
    }
    for (k = 0;; k++) {
        if (!ask(INST_Q_GATE, k == 0 ?
                 "\nIf Windows has a hardware wizard open (Found New Hardware, "
                 "Add New Hardware),\nfinish it first: Next through Finish, "
                 "even when it finds no driver. Do not\ncancel it: a cancelled "
                 "wizard hides its device until Windows restarts.\n"
                 "Go on?\n  Y  go on: no hardware wizard is open\n"
                 "  N  stop here (run the installer again later)\n"
                 "  Enter for [Y]: " : "  Type Y or N, or Enter for [Y]: ",
                 k > 0, 0, 1, NULL, line, sizeof(line))) {
            r = 0;
            break;
        }
        r = inst_tb_answer(line, 1);
        if (r >= 0) {
            break;
        }
    }
    if (r != 1) {
        runCancelled = 1;
        flagSteps = 1;
        out("Stopped before the next device: run the installer again to go "
            "on.\n");
        return 0;
    }
    return go_on();
}

/* One device by the helper: its verdict noted (record 27 section 5.4),
 * the device marked as reached, and a device the helper cannot do put on
 * the steps list. 0 when it was a controller that failed (the devices
 * below it are not attempted). */
static int helper_install(DEVREC *d)
{
    char inf[INST_PATH_CHARS];
    INST_DI_RESULT r;
    int force;
    int out9x;
    int v;
    int c;
    char why[96];

    join(inf, sizeof(inf), srcDir, instInfFile[d->Inf]);
    force = d->Action == INST_ACT_CURRENT || d->Action == INST_ACT_NEWER;
    if (d->Inf == INST_INF_S98) {
        /* Unforced, as on NT (record 26 section 6.3). Only a device with
         * no driver comes here: one Stor98 runs is never reinstalled on
         * 98 SE and ME (s98_places; the file update replaces its file). */
        force = 0;
        if (!s98_pre_commit()) {
            steps_add(d, "Stor98's install record could not be written");
            flagFailed = 1;
            d->Done = 1;
            return 1;
        }
    }
    helperLastOut = -1;
    out("Installing %s ...\n", d->Desc[0] ? d->Desc : d->Id);
    step(INST_STEP_BEGIN, "Installing %s", d->Desc[0] ? d->Desc : d->Id);
    out_flush();
    helperRan = 1;
    if (helperState < 0) {
        steps_add(d, "the helper was not used for it");
        flagSteps = 1;
        step(INST_STEP_LEFT, "%s: steps left for you",
             d->Desc[0] ? d->Desc : d->Id);
        d->Done = 1;
        return 1;
    }
    /* Every helper install is bracketed by the INF listings (record 26
     * section 6.3): a copy made in it is that device's step. */
    s98_inf_sync();
    v = helper_job(INST_DI_INSTALL, d, inf, force, &r);
    if (d->Inf != INST_INF_S98 && helperState >= 0 &&
        inst_absent_retry(inst_di_outcome(INST_DI_INSTALL, v), parentJobs, 0)) {
        /* A parent's job of this run may have restarted it: the tree
         * settled, and the job made once more (98 SE update over 3.0.0.0,
         * 2026-10-19: the hub after its root hub's reinstall). */
        out("  not present at the moment.\n");
        if (!reappear_settle(d->Id)) {
            /* The absent verdict stands for it; the passes stop. */
            helperStop = 1;
        } else {
            out("Installing %s again ...\n", d->Desc[0] ? d->Desc : d->Id);
            out_flush();
            v = helper_job(INST_DI_INSTALL, d, inf, force, &r);
        }
    }
    c = note_changed(d, 1);
    d->Done = 1;
    if (d->Inf != INST_INF_S98) {
        s98_inf_after(NULL, 0);
    } else {
        char infPath[64];
        s98_reread_9x(d->Id, infPath, sizeof(infPath));
        s98_inf_after(infPath, 0);
    }
    out9x = inst_di_outcome(INST_DI_INSTALL, v);
    helperLastOut = out9x;
    if (inst_settle_after_job(inst_parent_kind(d->Id, is_controller(d)),
                              out9x, 1)) {
        parentJobs++;
    }
    _snprintf(why, sizeof(why) - 1, "%s%s%s", inst_di_verdict_text(v),
              r.Error[0] ? ": " : (r.Detail[0] && v == INST_DI_V_NONE ? ": " :
                                   ""),
              r.Error[0] ? r.Error : (v == INST_DI_V_NONE ? r.Detail : ""));
    why[sizeof(why) - 1] = '\0';
    out("  %s.\n", why);
    switch (out9x) {
    case INST_DI_OUT_OK:
    case INST_DI_OUT_RESTART:
        if (c >= 0) {
            changedState[c] = 2;
            changedReboot[c] = out9x == INST_DI_OUT_RESTART;
        }
        if (out9x == INST_DI_OUT_RESTART) {
            flagRestart = 1;
        }
        flagChanged = 1;
        step(INST_STEP_DONE, "%s: %s", d->Desc[0] ? d->Desc : d->Id, why);
        return 1;
    case INST_DI_OUT_ABSENT:
        if (c >= 0) {
            changedState[c] = 0;
        }
        /* 98 SE: a cancelled Found New Hardware wizard takes the devnode
         * away until the next start; the helper then finds nothing. */
        steps_add(d, "not present now (a cancelled wizard hides it until the "
                  "next start): restart, finish its Found New Hardware "
                  "wizard (Next to Finish, never Cancel), then run the "
                  "installer again");
        flagSteps = 1;
        step(INST_STEP_LEFT, "%s: not present", d->Desc[0] ? d->Desc : d->Id);
        return 1;
    case INST_DI_OUT_STEPS:
        if (c >= 0) {
            changedState[c] = 0;
        }
        steps_add(d, why);
        flagSteps = 1;
        step(INST_STEP_LEFT, "%s: %s", d->Desc[0] ? d->Desc : d->Id, why);
        return 1;
    case INST_DI_OUT_REFUSED:
        if (c >= 0) {
            changedState[c] = 0;
        }
        out("The helper refused (%s): the other devices get their steps "
            "printed.\n", r.Detail[0] ? r.Detail : "no reason given");
        helperState = -1;
        steps_add(d, why);
        flagFailed = 1;
        step(INST_STEP_FAILED, "%s: %s", d->Desc[0] ? d->Desc : d->Id, why);
        return !is_controller(d);
    default:
        if (c >= 0) {
            changedState[c] = 3;
        }
        steps_add(d, why);
        if (inst_di_unfinished(v)) {
            /* The helper did not finish: the install is unfinished, with
             * steps, never a failure of the device (98 SE update,
             * 2026-10-19). */
            flagSteps = 1;
            step(INST_STEP_LEFT, "%s: %s", d->Desc[0] ? d->Desc : d->Id, why);
        } else {
            flagFailed = 1;
            step(INST_STEP_FAILED, "%s: %s", d->Desc[0] ? d->Desc : d->Id,
                 why);
        }
        return !is_controller(d);
    }
}

/* 1 when device d is one the pass for INF inf takes now. */
static int pass_takes(const DEVREC *d, int inf, int controllers)
{
    return d->Present && d->Inf == inf && d->Runs && !d->Done &&
           !d->Refused && !d->RegOnly && !old_slot_suspect(d) &&
           (controllers < 0 || is_controller(d) == controllers);
}

static int pass_any(int inf, int controllers)
{
    int i;

    for (i = 0; i < devCount; i++) {
        if (pass_takes(&devs[i], inf, controllers)) {
            return 1;
        }
    }
    return 0;
}

/* The devices, in record 27 section 6's order: the controllers; after the
 * settle the root hub, hubs and composite entries that still need their
 * driver, at most four passes; the UAS devices; the host interface. Each
 * pass behind the gate. Every device of the plan that is left goes to the
 * steps. */
static void helper_installs(void)
{
    int k;
    int pass;
    int i;
    int first = 1;
    int controllerFailed = 0;

    if (!pass_any(INST_INF_XHCI, -1) && !pass_any(INST_INF_UAS, -1) &&
        !pass_any(INST_INF_CM, -1) && !pass_any(INST_INF_S98, -1) &&
        !(storCopyOk && (stor_any(INST_STOR_KIND_BULK) ||
                         stor_any(INST_STOR_KIND_UNIT)))) {
        return;
    }
    if (!helper_ready()) {
        return;
    }
    if (pass_any(INST_INF_XHCI, 1)) {
        if (!gate_9x(1)) {
            goto left;
        }
        first = 0;
        for (i = 0; i < devCount && !controllerFailed; i++) {
            if (pass_takes(&devs[i], INST_INF_XHCI, 1)) {
                if (!go_on()) {
                    goto left;
                }
                if (!helper_install(&devs[i])) {
                    controllerFailed = 1;
                }
                if (helperStop) {
                    goto left;
                }
            }
        }
        if (controllerFailed) {
            out("A controller's install failed: the devices below it are not "
                "attempted.\n");
            goto left;
        }
    }
    for (k = 0; k < INST_INF_COUNT && !runCancelled; k++) {
        /* 98 SE, the USB storage drivers (record 27 section 6, steps 3a
         * and 4a): the Bulk-Only devices before the UAS step, the units
         * after it. */
        if (k == INST_INF_UAS && !stor_stage(INST_STOR_KIND_BULK, &first)) {
            goto left;
        }
        if (k == INST_INF_CM && !stor_stage(INST_STOR_KIND_UNIT, &first)) {
            goto left;
        }
        if (infText[k] == NULL || k == INST_INF_CMF) {
            continue;
        }
        for (pass = 0; pass < 4; pass++) {
            if (!first) {
                /* The settle, and the tree read again: a wizard may have
                 * installed some of them already. */
                s98_inf_sync();
                out("Waiting for the devices to settle ... ");
                i = settle(INST_INSTALL_WAIT_MS, 20000UL, 20000UL, 0);
                if (i <= 0) {
                    settle_failed(i, "10 minutes");
                    goto left;
                }
                out("done.\n");
                if (!scan()) {
                    flagFailed = 1;
                    goto left;
                }
                plan();
                s98_record_devices();
                stor_record();
                s98_inf_after(NULL, 1);
            }
            if (!pass_any(k, k == INST_INF_XHCI ? 0 : -1)) {
                break;
            }
            if (!gate_9x(1)) {
                goto left;
            }
            first = 0;
            for (i = 0; i < devCount; i++) {
                if (pass_takes(&devs[i], k, k == INST_INF_XHCI ? 0 : -1)) {
                    int j;
                    int more = 0;
                    if (!go_on()) {
                        goto left;
                    }
                    helperLastOut = -1;
                    helper_install(&devs[i]);
                    if (helperStop) {
                        goto left;
                    }
                    for (j = i + 1; j < devCount && !more; j++) {
                        more = pass_takes(&devs[j], k,
                                          k == INST_INF_XHCI ? 0 : -1);
                    }
                    /* A root hub's or hub's reinstall restarts it and
                     * takes the devices below it away for seconds (98 SE
                     * leg, 2026-10-19): the next job waits for them. */
                    if (helperLastOut >= 0 &&
                        inst_settle_after_job(
                            inst_parent_kind(devs[i].Id,
                                             is_controller(&devs[i])),
                            helperLastOut, more) &&
                        !reappear_settle(devs[i].Id)) {
                        goto left;
                    }
                }
            }
        }
    }
    if (!first) {
        s98_inf_sync();
        out("Waiting for the devices to settle ... ");
        i = settle(INST_INSTALL_WAIT_MS, 20000UL, 20000UL, 0);
        if (i <= 0) {
            settle_failed(i, "10 minutes");
        } else {
            out("done.\n");
            if (scan()) {
                plan();
                s98_record_devices();
                stor_record();
                s98_inf_after(NULL, 1);
            }
        }
    }
    /* A unit that appeared in a later settle is taken the same way before
     * the run ends (record 26 section 6.5.5). */
    if (storCopyOk && !stor_stage(INST_STOR_KIND_UNIT, &first)) {
        goto left;
    }
left:
    if (scan()) {
        plan();
        /* Every exit, a settle that failed too: what Windows bound and the
         * copies it made are read and recorded by the rules. */
        s98_record_devices();
        stor_record();
        s98_inf_after(NULL, 1);
    } else {
        /* No fresh reading: the listing is taken and logged, nothing is
         * attributed. */
        s98_inf_after(NULL, -1);
        flagFailed = 1;
    }
    for (i = 0; i < devCount; i++) {
        DEVREC *d = &devs[i];
        if (d->Present && d->Inf >= 0 && d->Inf != INST_INF_CMF && d->Runs &&
            !d->Refused && !d->RegOnly && !old_slot_suspect(d) &&
            !was_installed(d->Id) && steps_why(d->Id) == NULL) {
            steps_add(d, "not reached in this run");
            flagSteps = 1;
        }
    }
}

/* 98 SE and ME: the setup engine cannot replace a driver binary that is
 * loaded, so an update over a running driver copies the new file beside
 * it and asks for the swap in WININIT.INI's [rename] section, done at the
 * next start ("target=source", read on the 98 SE update leg of
 * 2026-10-19: xhci98.sys=...\xhci98.tmp). 1 when such an entry renames a
 * file of version `want` over `bin`. */
static int wininit_pending(const char *bin, const INST_U32 *want)
{
    char win[INST_PATH_CHARS];
    char ini[INST_PATH_CHARS];
    char *sec;
    char *p;
    char sb[INST_PATH_CHARS];
    char st[INST_PATH_CHARS];
    INST_U32 v[4];
    DWORD n;
    int hit = 0;

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    join(ini, sizeof(ini), win, "WININIT.INI");
    sec = (char *)calloc(1, 32768);
    if (sec == NULL) {
        return 0;
    }
    n = GetPrivateProfileSectionA("rename", sec, 32766, ini);
    n = GetShortPathNameA(bin, sb, sizeof(sb));
    if (n == 0 || n >= sizeof(sb)) {
        inst_copy(sb, bin, sizeof(sb));
    }
    for (p = sec; *p != '\0' && !hit; p += strlen(p) + 1) {
        char *eq = strchr(p, '=');
        if (eq == NULL || eq == p) {
            continue;
        }
        *eq = '\0';
        n = GetShortPathNameA(p, st, sizeof(st));
        if (n == 0 || n >= sizeof(st)) {
            inst_copy(st, p, sizeof(st));
        }
        if ((inst_eq(p, bin) || inst_eq(st, sb)) &&
            file_version(eq + 1, v) > 0 && inst_ver_cmp(v, want) == 0) {
            hit = 1;
        }
        *eq = '=';
    }
    free(sec);
    logonly("WININIT.INI: %s %s\n", bin,
            hit ? "is replaced by this package's at the next start"
                : "has no pending replacement");
    return hit;
}

/* Record 26 section 8: each device the helper installed read again - its
 * driver key's NTMPDriver naming the group's .sys, its version the
 * package's (DriverVersion, or the group's binary), the devnode present
 * with no problem code; a device gone, or left with a problem code, is a
 * failure, except one whose install asked for the restart, and one whose
 * binary the next start replaces (wininit_pending). */
static void check_results_9x(void)
{
    int c;
    int i;
    char state[160];
    char bin[INST_PATH_CHARS];
    INST_U32 v[4];

    if (!helperRan) {
        return;
    }
    out("\nAfterwards:\n");
    for (c = 0; c < changedCount; c++) {
        const DEVREC *d = NULL;
        int ok;
        int swapped = 0;
        if (changedState[c] != 2) {
            continue;
        }
        for (i = 0; i < devCount; i++) {
            if (inst_eq(devs[i].Id, changedId[c])) {
                d = &devs[i];
                break;
            }
        }
        if (d == NULL || !d->Present) {
            if (changedReboot[c]) {
                out("  %s: installed, restart pending.\n", changedId[c]);
                continue;
            }
            out("  %s: gone after its install.\n", changedId[c]);
            flagFailed = 1;
            steps_add_id(changedId[c], "gone after its install");
            continue;
        }
        ok = inst_eq(d->Service, instInfService[changedInf[c]]) &&
             (d->Status & INST_DN_STARTED) != 0 && d->Problem == 0;
        if (ok && d->VersionState == INST_VS_HAVE) {
            ok = inst_ver_cmp(d->Version, inf_version(changedInf[c])) == 0;
        } else if (ok) {
            group_binary(group_of_inf(changedInf[c]), bin, sizeof(bin));
            ok = file_version(bin, v) > 0 &&
                 inst_ver_cmp(v, inf_version(changedInf[c])) == 0;
            if (!ok && wininit_pending(bin, inf_version(changedInf[c]))) {
                swapped = 1;
            }
        }
        inst_state_text(state, sizeof(state), d->Service, d->VersionText,
                        (d->Status & INST_DN_STARTED) != 0, d->Problem);
        if (ok) {
            out("  %s: %s\n", d->Desc[0] ? d->Desc : d->Id, state);
        } else if (swapped) {
            out("  %s: updated; the new driver file replaces the running one "
                "at the next start.\n", d->Desc[0] ? d->Desc : d->Id);
            flagRestart = 1;
        } else if (changedReboot[c]) {
            out("  %s: installed, restart pending (%s)\n",
                d->Desc[0] ? d->Desc : d->Id, state);
        } else {
            out("  %s did not end as installed: %s.\n",
                d->Desc[0] ? d->Desc : d->Id, state);
            flagFailed = 1;
            steps_add(d, "it did not end as installed");
        }
    }
}

/* INST_PH_PLAN on 98 SE and ME. */
static int plan_9x(void)
{
    plan();
    print_plan();
    stor_print_plan();
    out("On Windows 98 SE and ME this installer copies the package into the "
        "install\ndirectory, adds that copy to DevicePath (where the hardware "
        "wizards search),\nand installs each device's driver through Windows' "
        "own device installer.\n\n");
    if (s98FileUpd) {
        out("Stor98: the installed stor98.mpd is an older release. This run "
            "replaces it with\n%s as a file when Windows next starts (a drive "
            "Stor98 runs is never\nreinstalled); Windows is then shut down and "
            "started again.\n\n", s98VersionText);
    }
    return -1;
}

/* INST_PH_ASK's part on 98 SE and ME: the warning and the confirmation. */
static int ask_9x(void)
{
    int i;
    int runs = 0;
    int steps = 0;

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
    input_warning();
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (d->Present && d->Inf >= 0 && (d->Runs || d->Refused)) {
            steps = 1;
            if (!old_slot_suspect(d)) {
                runs = 1;
            }
        }
    }
    /* The USB storage drivers' copies are a change of their own (record
     * 26 section 6.5.4). */
    if (storHere && storInclude && storPack > 0 &&
        inst_stor_route(storPlanCase)) {
        runs = 1;
    }
    if (!confirm(inst_confirm_text(runs, values_to_write(), 0,
                                   steps && !runs))) {
        return INST_EXIT_REFUSED;
    }
    return -1;
}

/* INST_PH_RUN on 98 SE and ME (record 23 section 6, record 26 section 7). */
static int run_install_9x(void)
{
    int i;
    int steps;
    INST_U32 before;

    /* Before the first change, as on NT (record 23 section 5.1 step 5):
     * the tree settled, read again and planned again; a plan that no
     * longer matches the one confirmed is not carried out. */
    before = devs_signature();
    i = settle(INST_FIRST_WAIT_MS, 5000UL, 0UL, 1);
    if (i <= 0) {
        settle_failed(i, "a minute");
        return inst_exit_code(0, flagFailed, flagRestart, 1);
    }
    if (!scan()) {
        out("The devices could not be read again: nothing was changed.\n");
        return inst_exit_code(0, 1, flagRestart, flagSteps);
    }
    plan();
    if (devs_signature() != before) {
        out("Windows changed the devices while the installer waited, so the "
            "plan shown is\nout of date. Nothing was changed: run the "
            "installer again to see the new plan.\n");
        return inst_exit_code(0, flagFailed, flagRestart, 1);
    }
    /* The file update not confirmed (the Summary's box, the console's
     * question): Stor98 is left for a later run, out of this one whole -
     * no copy of it, no job, no setting, no record (record 26 section
     * 6.3, draft 16). */
    if (s98Include && s98FileUpd && !s98FileOk) {
        s98Declined = 1;
        s98_apply(0);
        logonly("stor98: left out of the run, the file update not "
                "confirmed\n");
    }
    if (!s98_baseline()) {
        return inst_exit_code(0, 1, flagRestart, 1);
    }
    move_ready();
    /* The USB storage drivers' copies before the package's copy publishes
     * their INFs, and the withdrawal of an earlier deployment before any
     * step of it that can fail (record 26 section 6.5.4). */
    if (!stor_run_begin()) {
        step(INST_STEP_FAILED, "The USB storage drivers' withdrawal");
        return inst_exit_code(0, 1, flagRestart, 1);
    }
    step(INST_STEP_BEGIN, "Copying the package to %s", toolDir);
    /* The record's PreExisting once the copy that carries Stor98 is made
     * and before DevicePath names it, the first Stor98 change Windows
     * can see (record 26 section 6.3, draft 16). */
    if (!begin_move() || !make_local_copy() ||
        (copiedNow && s98Include && !s98HoldRun &&
         s98Share != INST_S98_SHARE_HOLD && !s98_pre_commit()) ||
        !devicepath_update(srcDir)) {
        step(INST_STEP_FAILED, "The package's copy and DevicePath");
        return inst_exit_code(0, 1, 0, 1);
    }
    stor_copy_made();
    if (!record_dir()) {
        /* Unrecorded, the copy is not one the uninstall would find: the
         * earlier layouts and the old directory are kept as they are. */
        step(INST_STEP_FAILED, "The package's copy, not recorded");
        return inst_exit_code(0, 1, 0, 1);
    }
    step(INST_STEP_DONE, "The package is in %s and on DevicePath", srcDir);
    hashed_cleanup();
    helper_installs();
    s98_file_update_9x();
    write_values();
    s98_write_values();
    tray_install();
    own_install();
    stor_tools_copy();
    old_dir_cleanup();
    arp_write(0);
    check_results_9x();
    s98_record_devices();
    s98_final_unknown();
    s98_record_version();
    stor_run_end();
    steps = 0;
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (d->Present && d->Inf >= 0 && (d->Runs || d->Refused) &&
            (!helperRan || steps_why(d->Id) != NULL)) {
            steps = 1;
        }
    }
    i = steps_9x(srcDir);
    tb_later_note(srcDir);
    note_old_entries();
    open_device_manager(i);
    /* Steps are left only where a device needs one (the helper could not
     * do it, a refused version, an entry that may be a cancelled wizard's
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
    int cmOnly = envNow.Nt && inst_eng_cm_only_machine();

    if (cmOnly) {
        /* The connection manager alone (record 23 section 9.1, revision
         * 13): no USB driver of this package is registered. */
        out("*** This removes the xHCI98 Thunderbolt 3 / USB4 connection "
            "manager. ***\nDevices connected through a Thunderbolt or USB4 "
            "port stop working when it\nis removed. Close every program that "
            "uses one, and save your work, before\nyou go on.\n");
    } else {
        out("*** This removes the xhci98 drivers from Windows. ***\n"
            "On a machine whose only USB controller is xHCI, the USB keyboard "
            "and mouse\nstop working when the controller's driver is removed, "
            "and stay dead until\nanother driver is installed. Have a PS/2 "
            "keyboard, a laptop's built-in one,\nor a USB controller another "
            "driver runs, before you go on.\n");
    }
    if (opts.List) {
        out("(/list: nothing will be changed.)\n\n");
        return 1;
    }
    if (opts.Yes) {
        out("/yes given: going on without the typed word.\n\n");
        return 1;
    }
    if (!ask(INST_Q_REMOVE, cmOnly ? "Remove the connection manager?\n"
             "  REMOVE  go on (type the word in capitals)\n"
             "  N       cancel: nothing is changed (so does anything else)\n"
             "  Enter for [N]: " : "Remove the drivers?\n"
             "  REMOVE  go on (type the word in capitals)\n"
             "  N       cancel: nothing is changed (so does anything else)\n"
             "  Enter for [N]: ", 0, 0, 0, NULL, line, sizeof(line))) {
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

/* The package's services this run removed or found absent (the
 * CriticalDeviceDatabase entries naming them go: cddb_service_scrub). */
static const char *svcGone[8];
static int svcGoneCount;
static const char *const cddbOwn[] = {
    "xhci98", "xhciuas", "xhcicm", "xhcihlpr", "xhcimsi", "stor98"
};
static int svc_state(const char *name);

static void svc_gone(const char *name)
{
    int i;

    for (i = 0; i < svcGoneCount; i++) {
        if (inst_eq(svcGone[i], name)) {
            return;
        }
    }
    if (svcGoneCount < (int)(sizeof(svcGone) / sizeof(svcGone[0]))) {
        svcGone[svcGoneCount++] = name;
    }
}

/* Record 23 section 9.2 step 6a: every CriticalDeviceDatabase entry whose
 * Service names a service of this package the run removed or found absent
 * is deleted, and read back gone (inst_cddb_service_gone); an entry naming
 * another driver's service is never touched. 1 done. */
static int cddb_service_scrub(void)
{
    HKEY root;
    HKEY k;
    DWORD i;
    DWORD nn;
    DWORD type;
    DWORD n;
    LONG r;
    char name[256];
    char svc[256];
    char (*hit)[256] = NULL;
    int hits = 0;
    int cap = 0;
    int ok = 1;
    int j;

    /* A service of the package absent now (or marked for deletion) is gone
     * whoever removed it: a retried uninstall, Stor98 removed by an earlier
     * run, xhcimsi retired by an update's INF. */
    for (j = 0; j < (int)(sizeof(cddbOwn) / sizeof(cddbOwn[0])); j++) {
        if (svc_state(cddbOwn[j]) == 0) {
            svc_gone(cddbOwn[j]);
        }
    }
    if (svcGoneCount == 0) {
        return 1;
    }
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_CDDB_KEY, 0,
                      KEY_READ | DELETE, &root);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 1;
    }
    if (r != ERROR_SUCCESS) {
        out("The CriticalDeviceDatabase could not be opened (error %ld).\n", r);
        return 0;
    }
    for (i = 0;; i++) {
        nn = sizeof(name);
        r = RegEnumKeyExA(root, i, name, &nn, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS) {
            out("The CriticalDeviceDatabase ended early (error %ld).\n", r);
            ok = 0;
            break;
        }
        r = RegOpenKeyExA(root, name, 0, KEY_QUERY_VALUE, &k);
        if (r != ERROR_SUCCESS) {
            if (r != ERROR_FILE_NOT_FOUND) {
                out("CriticalDeviceDatabase\\%s could not be read (error "
                    "%ld).\n", name, r);
                ok = 0;
            }
            continue;
        }
        memset(svc, 0, sizeof(svc));
        n = sizeof(svc) - 1;
        r = RegQueryValueExA(k, "Service", NULL, &type, (LPBYTE)svc, &n);
        RegCloseKey(k);
        if (r == ERROR_FILE_NOT_FOUND) {
            continue;
        }
        if (r != ERROR_SUCCESS) {
            /* ERROR_MORE_DATA: longer than any service name of ours, but
             * not proven another driver's either. */
            out("CriticalDeviceDatabase\\%s's Service could not be read "
                "(error %ld).\n", name, r);
            ok = 0;
            continue;
        }
        if ((type != REG_SZ && type != REG_EXPAND_SZ) ||
            !inst_cddb_service_gone(svc, svcGone, svcGoneCount)) {
            continue;
        }
        if (hits == cap) {
            char (*more)[256];
            cap = cap ? cap * 2 : 8;
            more = realloc(hit, (size_t)cap * sizeof(*hit));
            if (more == NULL) {
                ok = 0;
                break;
            }
            hit = more;
        }
        inst_copy(hit[hits], name, sizeof(hit[hits]));
        hits++;
    }
    for (j = 0; j < hits; j++) {
        if (opts.List) {
            out("(/list) CriticalDeviceDatabase\\%s would be deleted (its "
                "service is this package's).\n", hit[j]);
            continue;
        }
        r = RegDeleteKeyA(root, hit[j]);
        if (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND) {
            r = RegOpenKeyExA(root, hit[j], 0, KEY_QUERY_VALUE, &k);
            if (r == ERROR_SUCCESS) {
                RegCloseKey(k);
                r = ERROR_INVALID_DATA;
            } else if (r == ERROR_FILE_NOT_FOUND) {
                r = ERROR_SUCCESS;
            }
        }
        if (r == ERROR_SUCCESS) {
            out("CriticalDeviceDatabase\\%s was deleted (it named a service "
                "of this package).\n", hit[j]);
            flagChanged = 1;
        } else {
            out("CriticalDeviceDatabase\\%s could not be deleted (error "
                "%ld): a device of that id\nwould not start (Code 19).\n",
                hit[j], r);
            ok = 0;
        }
    }
    free(hit);
    RegCloseKey(root);
    return ok;
}
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
    PFN_MOVEEX moveEx = move_ex();

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
                } else {
                    svc_gone(instServices[i]);
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
                        svc_gone(instServices[i]);
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
                    svc_gone(instServices[i]);
                } else {
                    out("done.\n");
                    flagChanged = 1;
                    svc_gone(instServices[i]);
                }
                CloseServiceHandle(s);
            }
        } else if (opts.List) {
            out("The service %s, if present, would be removed.\n",
                instServices[i]);
            svc_gone(instServices[i]);
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
        } else if (moveEx != NULL &&
                   moveEx(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) {
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

/* ======================================================================== */
/* The safe-removal mark service and its marks (record 21 section 7.4,      */
/* revision 3; task 42-A.5)                                                 */
/* ======================================================================== */

#define INST_MK_SERVICE     "xhcimark"
#define INST_MK_DISPLAY     "xHCI98 Thunderbolt safe-removal mark service"
#define INST_MK_MARKS_KEY   "SYSTEM\\CurrentControlSet\\Services\\xhcimark\\Marks"
#define INST_MK_MUTEX       "Global\\Xhci98MarkWrite"
#define INST_MK_NAME        "xhcicm"
#define INST_MK_STOP_MS     30000UL
#define INST_MK_FILTERS     1024
#define INST_MK_LOG_DIR     "LogFiles\\xhci98"

typedef SC_HANDLE (WINAPI *PFN_CREATESVC)(SC_HANDLE, LPCSTR, LPCSTR, DWORD,
                                          DWORD, DWORD, DWORD, LPCSTR, LPCSTR,
                                          LPDWORD, LPCSTR, LPCSTR, LPCSTR);
typedef BOOL (WINAPI *PFN_CHANGESVC)(SC_HANDLE, DWORD, DWORD, DWORD, LPCSTR,
                                     LPCSTR, LPDWORD, LPCSTR, LPCSTR, LPCSTR,
                                     LPCSTR);
typedef BOOL (WINAPI *PFN_STARTSVC)(SC_HANDLE, DWORD, LPCSTR *);
typedef BOOL (WINAPI *PFN_CONTROLSVC)(SC_HANDLE, DWORD, LPSERVICE_STATUS);

/* The service routines this file does not import, looked up at run time
 * as record 21 section 7.4 has them (98 SE and ME never get here). */
static FARPROC adv_proc(const char *name)
{
    HMODULE adv = GetModuleHandleA("advapi32.dll");

    return adv != NULL ? GetProcAddress(adv, name) : NULL;
}

static const char *mk_exe(void)
{
    return INST_MY_ARCH == INST_ARCH_X86 ? "XHCIMARK.EXE" : "XHCIMK64.EXE";
}

/* A service's state: 1 registered, 0 not (or marked for deletion, which
 * leaves it gone at the restart), -1 unreadable. OpenService still opens
 * a service marked for deletion, so a change that changes nothing asks:
 * ChangeServiceConfig refuses such a service with its own error. */
static int svc_state(const char *name)
{
    PFN_CHANGESVC changeSvc = (PFN_CHANGESVC)adv_proc("ChangeServiceConfigA");
    SC_HANDLE scm;
    SC_HANDLE s;
    DWORD e;
    int r;

    if (changeSvc == NULL) {
        return -1;
    }
    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        return -1;
    }
    s = OpenServiceA(scm, name, SERVICE_QUERY_STATUS | SERVICE_CHANGE_CONFIG);
    e = GetLastError();
    if (s != NULL) {
        if (changeSvc(s, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE,
                      SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL,
                      NULL)) {
            r = 1;
        } else {
            r = GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE ? 0 : -1;
        }
        CloseServiceHandle(s);
    } else {
        r = (e == ERROR_SERVICE_DOES_NOT_EXIST ||
             e == ERROR_SERVICE_MARKED_FOR_DELETE) ? 0 : -1;
    }
    CloseServiceHandle(scm);
    return r;
}

/* The mark service stopped: SERVICE_STOPPED within 30 s. 1 stopped, or
 * not registered (*had 0); 0 it could not be stopped or read. */
static int mk_stop(SC_HANDLE scm, int *had)
{
    PFN_CONTROLSVC controlSvc = (PFN_CONTROLSVC)adv_proc("ControlService");
    SC_HANDLE s;
    SERVICE_STATUS st;
    DWORD t0;
    DWORD e;

    *had = 0;
    s = OpenServiceA(scm, INST_MK_SERVICE, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (s == NULL) {
        e = GetLastError();
        if (e == ERROR_SERVICE_DOES_NOT_EXIST ||
            e == ERROR_SERVICE_MARKED_FOR_DELETE) {
            return 1;
        }
        out("The service %s could not be opened (error %lu).\n",
            INST_MK_SERVICE, (unsigned long)e);
        return 0;
    }
    *had = 1;
    if (!QueryServiceStatus(s, &st)) {
        out("The state of the service %s could not be read (error %lu).\n",
            INST_MK_SERVICE, (unsigned long)GetLastError());
        CloseServiceHandle(s);
        return 0;
    }
    if (st.dwCurrentState != SERVICE_STOPPED) {
        if (controlSvc == NULL) {
            out("ControlService is missing: the service %s cannot be "
                "stopped.\n", INST_MK_SERVICE);
            CloseServiceHandle(s);
            return 0;
        }
        out("Stopping the service %s ... ", INST_MK_SERVICE);
        if (st.dwCurrentState != SERVICE_STOP_PENDING) {
            (void)controlSvc(s, SERVICE_CONTROL_STOP, &st);
        }
        t0 = GetTickCount();
        while (QueryServiceStatus(s, &st) &&
               st.dwCurrentState != SERVICE_STOPPED &&
               GetTickCount() - t0 < INST_MK_STOP_MS) {
            Sleep(250);
        }
        if (st.dwCurrentState != SERVICE_STOPPED) {
            out("it did not stop within 30 seconds.\n");
            CloseServiceHandle(s);
            return 0;
        }
        out("stopped.\n");
    }
    CloseServiceHandle(s);
    return 1;
}

/* The CriticalDeviceDatabase's copies (record 21 section 7.4, step 0):
 * Windows 2000 and XP copy a boot-critical device's own filter values into
 * its class's entry there when they install it - a marked unit's
 * LowerFilters, and the filter INF's UpperFilters of a tunnel port - and
 * every new device of that class then gets them; with xhcicm gone, a new
 * bridge or NVMe controller (a boot disk among them) would name a missing
 * filter. With fix, xhcicm is taken out of every entry's LowerFilters and
 * UpperFilters, the other names kept in order, the value deleted when
 * nothing else is in it, and each read back. 1 when the key was read whole
 * (and, with fix, every change read back); *left the values still naming
 * xhcicm. */
/* 0 while the marks are removed with the xhcicm service staying (safe eject
 * off): the filter INF's UpperFilters copies are the tunnel-port filter's,
 * not a mark's, and stay with it. */
static int cddbUpper = 1;

static int cddb_scrub(int fix, int *left)
{
    static const char *const vals[2] = { "LowerFilters", "UpperFilters" };
    HKEY root;
    HKEY k;
    DWORD i;
    DWORD nn;
    DWORD type;
    DWORD n;
    LONG r;
    char name[256];
    char f[INST_MK_FILTERS];
    char nf[INST_MK_FILTERS];
    size_t ob;
    int act;
    int v;
    int ok = 1;

    *left = 0;
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_CDDB_KEY, 0, KEY_READ, &root);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 1;
    }
    if (r != ERROR_SUCCESS) {
        out("The CriticalDeviceDatabase could not be read (error %ld).\n", r);
        return 0;
    }
    for (i = 0;; i++) {
        nn = sizeof(name);
        r = RegEnumKeyExA(root, i, name, &nn, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS) {
            out("The CriticalDeviceDatabase ended early (error %ld).\n", r);
            ok = 0;
            break;
        }
        if (RegOpenKeyExA(root, name, 0,
                          fix ? KEY_QUERY_VALUE | KEY_SET_VALUE : KEY_QUERY_VALUE,
                          &k) != ERROR_SUCCESS) {
            out("CriticalDeviceDatabase\\%s could not be opened.\n", name);
            ok = 0;
            continue;
        }
        for (v = 0; v < (cddbUpper ? 2 : 1); v++) {
            memset(f, 0, sizeof(f));
            n = sizeof(f) - 2;
            r = RegQueryValueExA(k, vals[v], NULL, &type, (LPBYTE)f, &n);
            if (r == ERROR_FILE_NOT_FOUND) {
                continue;
            }
            if (r != ERROR_SUCCESS) {
                out("CriticalDeviceDatabase\\%s's %s could not be read.\n",
                    name, vals[v]);
                ok = 0;
                continue;
            }
            if (type != REG_MULTI_SZ && type != REG_SZ) {
                continue;
            }
            act = inst_filter_drop(f, n + 1, INST_MK_NAME, nf, sizeof(nf), &ob);
            if (act == INST_FD_NONE) {
                continue;
            }
            logonly("cddb: %s names %s in its %s\n", name, INST_MK_NAME,
                    vals[v]);
            if (!fix) {
                (*left)++;
                continue;
            }
            if (act == INST_FD_SET) {
                r = RegSetValueExA(k, vals[v], 0, REG_MULTI_SZ,
                                   (const BYTE *)nf, (DWORD)ob);
            } else if (act == INST_FD_DELETE) {
                r = RegDeleteValueA(k, vals[v]);
            } else {
                r = ERROR_INSUFFICIENT_BUFFER;
            }
            /* Read back. */
            memset(f, 0, sizeof(f));
            n = sizeof(f) - 2;
            if (r == ERROR_SUCCESS) {
                r = RegQueryValueExA(k, vals[v], NULL, &type, (LPBYTE)f, &n);
                if (r == ERROR_FILE_NOT_FOUND) {
                    r = ERROR_SUCCESS;
                } else if (r == ERROR_SUCCESS &&
                           inst_filter_drop(f, n + 1, INST_MK_NAME, nf,
                                            sizeof(nf), &ob) != INST_FD_NONE) {
                    r = ERROR_INVALID_DATA;
                }
            }
            if (r != ERROR_SUCCESS) {
                out("%s could not be removed from CriticalDeviceDatabase\\%s's "
                    "%s (error %ld).\n", INST_MK_NAME, name, vals[v], r);
                (*left)++;
                ok = 0;
                continue;
            }
            flagChanged = 1;
            out("%s was removed from CriticalDeviceDatabase\\%s's %s (a copy "
                "Windows made at a device's install).\n", INST_MK_NAME, name,
                vals[v]);
        }
        RegCloseKey(k);
    }
    RegCloseKey(root);
    return ok;
}

/* Every PCI devnode, present or not, its LowerFilters read (record 21
 * section 7.4, step 0), and the CriticalDeviceDatabase's copies
 * (cddb_scrub). With fix, xhcicm is removed from each value that names it,
 * the other names kept in order, the value deleted when nothing else is in
 * it, and each read back. 1 when the enumeration reached its end and every
 * value was read (and, with fix, every removal read back); *left the values
 * still naming xhcicm. */
static int mk_inventory(int fix, int *left)
{
    HDEVINFO set;
    SP_DEVINFO_DATA data;
    DWORD i;
    DWORD type;
    DWORD n;
    DWORD e;
    char id[INST_ID_CHARS];
    char f[INST_MK_FILTERS];
    char nf[INST_MK_FILTERS];
    size_t m;
    int removed;
    int ok = 1;
    BOOL w;

    *left = 0;
    if (api.GetClassDevs == NULL || api.EnumInfo == NULL ||
        api.GetRegProp == NULL || api.GetInstanceId == NULL ||
        api.Destroy == NULL || (fix && api.SetRegProp == NULL)) {
        out("The device-property routines are missing: the Thunderbolt "
            "safe-removal marks\ncannot be read.\n");
        return 0;
    }
    /* The CriticalDeviceDatabase first: it is the source new devices take
     * the name from, so the devices are read after it is clean. */
    {
        int cleft;

        if (!cddb_scrub(fix, &cleft)) {
            ok = 0;
        }
        *left += cleft;
    }
    set = api.GetClassDevs(NULL, "PCI", NULL, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) {
        out("The PCI device list could not be opened (error %lu): the "
            "Thunderbolt\nsafe-removal marks cannot be read.\n",
            (unsigned long)GetLastError());
        return 0;
    }
    for (i = 0;; i++) {
        memset(&data, 0, sizeof(data));
        data.cbSize = sizeof(data);
        if (!api.EnumInfo(set, i, &data)) {
            e = GetLastError();
            if (e != ERROR_NO_MORE_ITEMS) {
                out("The PCI device list ended early (error %lu).\n",
                    (unsigned long)e);
                ok = 0;
            }
            break;
        }
        if (!api.GetInstanceId(set, &data, id, sizeof(id), NULL)) {
            inst_copy(id, "(a PCI device)", sizeof(id));
        }
        memset(f, 0, sizeof(f));
        n = 0;
        if (!api.GetRegProp(set, &data, SPDRP_LOWERFILTERS, &type, (PBYTE)f,
                            sizeof(f) - 2, &n)) {
            e = GetLastError();
            if (e == ERROR_INVALID_DATA) {
                continue;   /* no LowerFilters value */
            }
            out("The LowerFilters of %s could not be read (error %lu).\n", id,
                (unsigned long)e);
            ok = 0;
            continue;
        }
        if (!inst_msz_has(f, n, INST_MK_NAME)) {
            continue;
        }
        logonly("mark: %s names %s in its LowerFilters\n", id, INST_MK_NAME);
        if (!fix) {
            (*left)++;
            continue;
        }
        m = inst_msz_remove(f, n, INST_MK_NAME, nf, sizeof(nf), &removed);
        if (m == (size_t)-1) {
            out("The LowerFilters of %s could not be edited.\n", id);
            (*left)++;
            ok = 0;
            continue;
        }
        w = m == 0 ? api.SetRegProp(set, &data, SPDRP_LOWERFILTERS, NULL, 0)
                   : api.SetRegProp(set, &data, SPDRP_LOWERFILTERS,
                                    (const BYTE *)nf, (DWORD)m);
        memset(f, 0, sizeof(f));
        n = 0;
        if (!w ||
            (api.GetRegProp(set, &data, SPDRP_LOWERFILTERS, &type, (PBYTE)f,
                            sizeof(f) - 2, &n)
                 ? inst_msz_has(f, n, INST_MK_NAME)
                 : GetLastError() != ERROR_INVALID_DATA)) {
            out("%s could not be removed from the LowerFilters of %s.\n",
                INST_MK_NAME, id);
            (*left)++;
            ok = 0;
            continue;
        }
        flagChanged = 1;
        out("The safe-removal mark (%s in LowerFilters) was removed from %s.\n",
            INST_MK_NAME, id);
    }
    api.Destroy(set);
    return ok;
}

/* The mark service's system32 copy deleted. One in use is renamed to
 * <name>.DEL and that name deleted at the restart, as the tray tool's
 * copy is (record 23 section 9.5), so a later install of the same name
 * before the restart is not deleted with it. 1 gone or renamed; 0 it
 * stays (the service's registration then stays too). */
static int mk_delete_copy(void)
{
    char sys[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char del[INST_PATH_CHARS];
    PFN_MOVEEX moveEx = move_ex();

    if (GetSystemDirectoryA(sys, sizeof(sys)) == 0) {
        out("The system directory could not be read.\n");
        return 0;
    }
    join(path, sizeof(path), sys, mk_exe());
    inst_copy(del, path, sizeof(del));
    del[strlen(del) - 4] = '\0';
    inst_copy(del + strlen(del), ".DEL", sizeof(del) - strlen(del));
    /* An earlier run's .DEL: deleted now, or its deletion at the restart
     * asked again; one that is neither keeps the registration. */
    if (file_state(del) != 0) {
        SetFileAttributesA(del, FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileA(del) &&
            (moveEx == NULL || !moveEx(del, NULL, MOVEFILE_DELAY_UNTIL_REBOOT))) {
            out("%s could not be deleted (error %lu).\n", del,
                (unsigned long)GetLastError());
            return 0;
        }
        flagChanged = 1;
        if (file_state(del) != 0) {
            flagRestart = 1;
        }
    }
    switch (file_state(path)) {
    case 0:
        return 1;
    case 1:
        break;
    default:
        out("%s could not be read (error %lu).\n", path,
            (unsigned long)GetLastError());
        return 0;
    }
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileA(path)) {
        out("Deleted %s.\n", path);
        flagChanged = 1;
        return 1;
    }
    if (moveEx != NULL && moveEx(path, del, MOVEFILE_REPLACE_EXISTING)) {
        if (moveEx(del, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) {
            out("%s is in use; it is deleted at the restart.\n", path);
            flagChanged = 1;
            flagRestart = 1;
            return 1;
        }
        /* Nothing would delete it: its own name back, and the step
         * fails with the registration kept. */
        (void)moveEx(del, path, 0);
    }
    out("%s could not be deleted (error %lu).\n", path,
        (unsigned long)GetLastError());
    return 0;
}

/* Step 0 of the uninstall (decision M14), and the end of any run that
 * leaves no xhcicm service (decision M13): the mark service stopped, the
 * write mutex taken, every mark removed and read back, then the service's
 * records, the service and its system32 copy deleted. 1 done; 0 failed,
 * nothing after the marks touched (the records, the service and its
 * registration stay for the next run). */
static int mk_remove(void)
{
    SC_HANDLE scm;
    SC_HANDLE s;
    HANDLE mx;
    DWORD w;
    int had;
    int left;
    int ok;

    if (opts.List) {
        ok = mk_inventory(0, &left);
        if (left > 0) {
            out("(/list) %s would be removed from %d filter value(s) (devices' "
                "LowerFilters,\nthe CriticalDeviceDatabase's LowerFilters and "
                "UpperFilters).\n",
                INST_MK_NAME, left);
        }
        if (svc_state(INST_MK_SERVICE) == 1) {
            out("(/list) The service %s would be stopped and removed.\n",
                INST_MK_SERVICE);
        }
        return ok;
    }
    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm == NULL) {
        out("The service manager could not be opened (error %lu): the "
            "Thunderbolt\nsafe-removal marks stay.\n",
            (unsigned long)GetLastError());
        return 0;
    }
    if (!mk_stop(scm, &had)) {
        CloseServiceHandle(scm);
        return 0;
    }
    mx = shared_mutex(INST_MK_MUTEX);
    w = mx != NULL ? WaitForSingleObject(mx, INST_MK_STOP_MS) : WAIT_FAILED;
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        out("The marks' write lock %s could not be taken.\n", INST_MK_MUTEX);
        if (mx != NULL) {
            CloseHandle(mx);
        }
        CloseServiceHandle(scm);
        return 0;
    }
    ok = mk_inventory(1, &left) && left == 0;
    if (ok) {
        /* Read once more, both the database and the devices: Windows does
         * not take the write lock, and a device it found during the
         * removal may have taken the name from a copy. */
        int again;

        ok = mk_inventory(0, &again) && again == 0;
        if (!ok) {
            out("A Thunderbolt safe-removal mark (xhcicm) is still named after "
                "its removal.\n");
        }
    }
    if (ok) {
        LONG r = RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_MK_MARKS_KEY);
        if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
            out("The mark service's records could not be deleted (error "
                "%ld).\n", (long)r);
            ok = 0;
        }
    }
    /* The copy before the registration: a copy that stays keeps the
     * service registered for the next run. */
    if (ok && !mk_delete_copy()) {
        ok = 0;
    }
    if (ok && had) {
        s = OpenServiceA(scm, INST_MK_SERVICE, DELETE);
        if (s == NULL || !DeleteService(s)) {
            DWORD e = GetLastError();
            if (e != ERROR_SERVICE_MARKED_FOR_DELETE &&
                e != ERROR_SERVICE_DOES_NOT_EXIST) {
                out("The service %s could not be removed (error %lu).\n",
                    INST_MK_SERVICE, (unsigned long)e);
                ok = 0;
            }
        } else {
            out("The service %s was removed.\n", INST_MK_SERVICE);
            flagChanged = 1;
        }
        if (s != NULL) {
            CloseServiceHandle(s);
        }
    }
    ReleaseMutex(mx);
    CloseHandle(mx);
    CloseServiceHandle(scm);
    return ok;
}

/* StartService returns before the service has prepared itself: 1 once it
 * reads SERVICE_RUNNING within 30 s, 0 stopped or still starting. The
 * handle needs SERVICE_QUERY_STATUS. */
static int mk_running(SC_HANDLE s)
{
    SERVICE_STATUS st;
    DWORD t0 = GetTickCount();

    for (;;) {
        if (!QueryServiceStatus(s, &st)) {
            return 0;
        }
        if (st.dwCurrentState == SERVICE_RUNNING) {
            return 1;
        }
        if (st.dwCurrentState == SERVICE_STOPPED ||
            GetTickCount() - t0 >= INST_MK_STOP_MS) {
            return 0;
        }
        Sleep(250);
    }
}

/* The service installed or updated and started (decision M8): the
 * package's copy put into system32 through a .TMP and compared byte for
 * byte, registered with its quoted full path (its own process, automatic
 * start, LocalSystem), the log's directory made, then started. */
static void mk_install(void)
{
    char src[INST_PATH_CHARS];
    char sys[INST_PATH_CHARS];
    char dst[INST_PATH_CHARS];
    char tmp[INST_PATH_CHARS];
    char dir[INST_PATH_CHARS];
    char quoted[INST_PATH_CHARS + 4];
    PFN_CREATESVC createSvc = (PFN_CREATESVC)adv_proc("CreateServiceA");
    PFN_CHANGESVC changeSvc = (PFN_CHANGESVC)adv_proc("ChangeServiceConfigA");
    PFN_STARTSVC startSvc = (PFN_STARTSVC)adv_proc("StartServiceA");
    PFN_MOVEEX moveEx = move_ex();
    SC_HANDLE scm;
    SC_HANDLE s = NULL;
    int had;
    int put = 1;
    int running = 0;

    join(src, sizeof(src), root, "tools\\xhcimark");
    join(src, sizeof(src), src, mk_exe());
    if (!file_exists(src, NULL)) {
        out("This package has no tools\\xhcimark\\%s: the safe-removal mark "
            "service is not\ninstalled, and a Thunderbolt device's mark is lost "
            "when its drivers are rebuilt.\n", mk_exe());
        return;
    }
    if (createSvc == NULL || changeSvc == NULL || startSvc == NULL ||
        moveEx == NULL || GetSystemDirectoryA(sys, sizeof(sys)) == 0) {
        out("The service routines are missing: the safe-removal mark service "
            "is not\ninstalled.\n");
        flagFailed = 1;
        return;
    }
    join(dst, sizeof(dst), sys, mk_exe());
    inst_copy(tmp, dst, sizeof(tmp));
    tmp[strlen(tmp) - 4] = '\0';
    inst_copy(tmp + strlen(tmp), ".DEL", sizeof(tmp) - strlen(tmp));
    /* An earlier uninstall's copy, renamed while in use, if it is free
     * now; one still waiting for the restart is a name of its own. */
    if (file_exists(tmp, NULL)) {
        SetFileAttributesA(tmp, FILE_ATTRIBUTE_NORMAL);
        DeleteFileA(tmp);
    }
    tmp[strlen(tmp) - 4] = '\0';
    inst_copy(tmp + strlen(tmp), ".TMP", sizeof(tmp) - strlen(tmp));
    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm == NULL) {
        out("The service manager could not be opened (error %lu): the "
            "safe-removal mark\nservice is not installed.\n",
            (unsigned long)GetLastError());
        flagFailed = 1;
        return;
    }
    step(INST_STEP_BEGIN, "Installing the Thunderbolt safe-removal mark "
         "service");
    if (!mk_stop(scm, &had)) {
        out("The safe-removal mark service is left as it is.\n");
        flagFailed = 1;
        goto done;
    }
    if (same_file3(src, dst) != 1) {
        SetFileAttributesA(tmp, FILE_ATTRIBUTE_NORMAL);
        DeleteFileA(tmp);
        if (!CopyFileA(src, tmp, FALSE) || same_file3(src, tmp) != 1 ||
            !moveEx(tmp, dst, MOVEFILE_REPLACE_EXISTING)) {
            out("%s could not be put into %s (error %lu).\n", mk_exe(), sys,
                (unsigned long)GetLastError());
            DeleteFileA(tmp);
            flagFailed = 1;
            put = 0;
        } else {
            flagChanged = 1;
            logonly("mark service: %s copied to %s\n", src, dst);
        }
    }
    if (put && same_file3(src, dst) != 1) {
        out("REFUSED: %s is not the copy this package carries: the service is "
            "not\nregistered from it.\n", dst);
        flagFailed = 1;
        put = 0;
    }
    if (!put) {
        if (had) {
            /* The earlier copy stays; so does its service, started again. */
            s = OpenServiceA(scm, INST_MK_SERVICE,
                             SERVICE_START | SERVICE_QUERY_STATUS);
        }
        goto start;
    }
    _snprintf(quoted, sizeof(quoted) - 1, "\"%s\"", dst);
    quoted[sizeof(quoted) - 1] = '\0';
    if (!had) {
        s = createSvc(scm, INST_MK_SERVICE, INST_MK_DISPLAY, SERVICE_ALL_ACCESS,
                      SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                      SERVICE_ERROR_NORMAL, quoted, NULL, NULL, NULL, NULL,
                      NULL);
        if (s != NULL) {
            flagChanged = 1;
        }
    } else {
        s = OpenServiceA(scm, INST_MK_SERVICE, SERVICE_ALL_ACCESS);
        if (s != NULL &&
            !changeSvc(s, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                       SERVICE_ERROR_NORMAL, quoted, NULL, NULL, NULL,
                       "LocalSystem", "", INST_MK_DISPLAY)) {
            out("The service %s could not be set up (error %lu).\n",
                INST_MK_SERVICE, (unsigned long)GetLastError());
            flagFailed = 1;
        }
    }
    if (s == NULL) {
        out("The service %s could not be registered (error %lu).\n",
            INST_MK_SERVICE, (unsigned long)GetLastError());
        flagFailed = 1;
        goto done;
    }
    join(dir, sizeof(dir), sys, INST_MK_LOG_DIR);
    if (!make_dirs(dir)) {
        out("The service's log directory %s could not be made.\n", dir);
    }
start:
    if (s == NULL) {
        goto done;
    }
    if (!startSvc(s, 0, NULL) &&
        GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        out("The service %s did not start (error %lu).\n", INST_MK_SERVICE,
            (unsigned long)GetLastError());
        flagFailed = 1;
    } else if (!mk_running(s)) {
        out("The service %s did not reach the running state within 30 "
            "seconds\n(its log: system32\\LogFiles\\xhci98\\XHCIMARK.LOG).\n",
            INST_MK_SERVICE);
        flagFailed = 1;
    } else if (put) {
        out("The Thunderbolt safe-removal mark service %s is %s and running:\n"
            "  %s\n", INST_MK_SERVICE, had ? "updated" : "installed", quoted);
        step(INST_STEP_DONE, "The safe-removal mark service is running");
        running = 1;
    }
done:
    /* Every begun step ends: the window's row says how. */
    if (!running) {
        step(INST_STEP_FAILED, "The safe-removal mark service");
    }
    if (s != NULL) {
        CloseServiceHandle(s);
    }
    CloseServiceHandle(scm);
}

/* mk_remove at a run's end: with safe eject off and xhcicm staying, the
 * database's UpperFilters (the tunnel-port filter's) are left. */
static int mk_remove_safe_off(int safe, int cmService)
{
    int ok;

    /* cmService is the state the decision read (inst_mk_end_action gives
     * REMOVE for 0, and for safe eject off only with 1 registered). */
    cddbUpper = (safe || cmService != 1) ? 1 : 0;
    ok = mk_remove();
    cddbUpper = 1;
    return ok;
}

/* The XhciCmSafeEject in force after the run (inst_mk_safe_eject): the
 * machine-wide value, else the own value of every present connection
 * manager device, whether or not the Thunderbolt component is in this run
 * and with no limit on their number (a reading that cannot be had is the
 * default 1, which keeps the service). */
static int mk_safe_eject_now(void)
{
    INST_VNOW *vnow;
    int nNow = 0;
    int v;
    int i;
    int mw;
    int r;
    int safe;
    DWORD mwv = 0;
    DWORD now;

    for (v = 0; v < instValueCount; v++) {
        if (inst_eq(instValues[v].Name, "XhciCmSafeEject")) {
            break;
        }
    }
    if (v >= instValueCount) {
        return 1;
    }
    mw = mw_value(v, &mwv);
    if (mw != 0) {
        return inst_mk_safe_eject(mw, (INST_U32)mwv, NULL, 0);
    }
    vnow = (INST_VNOW *)malloc((devCount > 0 ? devCount : 1) *
                               sizeof(INST_VNOW));
    if (vnow == NULL) {
        return 1;
    }
    for (i = 0; i < devCount; i++) {
        char path[INST_PATH_CHARS];

        /* Every present device the connection manager runs on now, by its
         * service, whatever this run's plan for it. */
        if (!devs[i].Present || !inst_eq(devs[i].Service, INST_MK_NAME)) {
            continue;
        }
        r = -1;
        if (devs[i].Driver[0] != '\0') {
            driver_key_path(&devs[i], path, sizeof(path));
            r = reg_dword(HKEY_LOCAL_MACHINE, path, instValues[v].Name, &now)
                    ? 1 : 0;
        }
        vnow[nNow].State = r > 0 ? 1 : r;
        vnow[nNow].Value = r > 0 ? (INST_U32)now : 0;
        vnow[nNow].Desc = devs[i].Id;
        nNow++;
    }
    safe = inst_mk_safe_eject(0, 0, vnow, nNow);
    free(vnow);
    return safe;
}

/* The end of an install or update run (decision M13): with xhcicm
 * registered and the Thunderbolt component in the run, the service
 * installed or updated; with no xhcicm service, every mark removed, a
 * LowerFilters naming a filter with no service being what stops a device
 * at its next start (Code 39). */
static void mk_end_of_run(void)
{
    int a;
    int safe;
    int cm;

    if (!envNow.Nt) {
        return;
    }
    safe = mk_safe_eject_now();
    cm = svc_state(INST_MK_NAME);
    a = inst_mk_end_action(cm, haveCm && tbInclude, inst_mk_system(&envNow),
                           safe);
    logonly("mark service: end-of-run action %d (XhciCmSafeEject in force "
            "%d)\n", a, safe);
    if (a == INST_MK_REMOVE && !safe) {
        out("XhciCmSafeEject is 0: the safe-removal mark service is not "
            "used; it is removed\nwith every mark where it is installed.\n");
    }
    if (a == INST_MK_INSTALL) {
        mk_install();
    } else if (a == INST_MK_REMOVE && !mk_remove_safe_off(safe, cm)) {
        out("A Thunderbolt safe-removal mark could not be removed%s. "
            "Restart Windows and\nrun INSTALL.BAT again.\n",
            safe ? ", and the xhcicm service is not installed: a\ndevice "
                   "naming it would not start (Code 39)"
                 : " (XhciCmSafeEject is 0)");
        flagFailed = 1;
    }
}

/* An uninstall that stopped at a gate (record 26 section 9.3): the
 * installer's own files, the records and the Add/Remove Programs entry
 * stay, the entry's name saying so, so the next run has its entry point
 * once the package is gone. */
static int stop_uninstall(const char *why)
{
    out("\nSTOPPED: %s\nNothing after this step was done. Restart Windows if "
        "it asked, then run\nthe uninstall again (Add/Remove Programs, or "
        "INSTALL /console /uninstall):\nit goes on from what is left.\n", why);
    flagSteps = 1;
    if (!opts.List) {
        arp_write(1);
    }
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
    /* Step 0 (record 21 section 7.4, decision M14): the mark service
     * stopped and every safe-removal mark removed before anything of
     * xhcicm goes, since a LowerFilters naming a filter with no service
     * stops the device (Code 39). A failure keeps everything after it. */
    if (!mk_remove()) {
        api.Destroy(set);
        return stop_uninstall("a Thunderbolt safe-removal mark (xhcicm in a "
                              "device's LowerFilters) could not\nbe removed, "
                              "so xhcicm and the mark service stay.");
    }
    /* The bundled Stor98's recorded devices first: children before the
     * NVMe device's parent port, a filter host that stays (record 26
     * section 9.3 step 3); then its staged INF and, when nothing else
     * depends on them, its service and files. */
    if (!s98_un_devices(set) || !s98_un_verify() || !s98_un_rest()) {
        api.Destroy(set);
        if (!flagSteps && !flagRestart) {
            flagFailed = 1;
        }
        return stop_uninstall("Stor98's recorded devices, its INF or its "
                              "service could not be removed.");
    }
    if (!opts.List && !scan()) {
        api.Destroy(set);
        return stop_uninstall("the device list could not be read again.");
    }
    ok = 1;
    restartBefore = flagRestart;
    for (i = 0; i < devCount && ok; i++) {
        if (is_filter_host(&devs[i])) {
            /* Record 26 section 5.8: a cancel stops at the next step. */
            if (!opts.List && !go_on()) {
                api.Destroy(set);
                return stop_uninstall("cancelled.");
            }
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
                if (!opts.List && !go_on()) {
                    api.Destroy(set);
                    return stop_uninstall("cancelled.");
                }
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
        cddb_service_scrub();
        stor_tools_remove();
        finish_remove();
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
    if (!go_on()) {
        return stop_uninstall("cancelled.");
    }
    if (!remove_own_infs()) {
        flagFailed = 1;
        return stop_uninstall("an INF of this package could not be removed.");
    }
    if (!go_on()) {
        return stop_uninstall("cancelled.");
    }
    /* The inventory once more before xhcicm's service and file go
     * (decision M14). */
    {
        int left;
        if (!mk_inventory(0, &left) || left != 0) {
            return stop_uninstall("a device or a CriticalDeviceDatabase entry "
                                  "still names xhcicm in its filters,\nso "
                                  "xhcicm's service and file stay.");
        }
    }
    remove_services_and_files();
    if (!go_on()) {
        return stop_uninstall("cancelled.");
    }
    copies_remove_all();
    if (!cddb_service_scrub()) {
        flagFailed = 1;
    }
    if (!stor_tools_remove()) {
        return stop_uninstall("a USB storage file in the tools folder could "
                              "not be removed.");
    }
    if (flagFailed || flagSteps) {
        return stop_uninstall("a service or a file of this package could not "
                              "be removed.");
    }
    if (!go_on()) {
        return stop_uninstall("cancelled.");
    }
    finish_remove();
    if (!flagChanged) {
        out("Nothing of this package was found.\n");
    }
    if (flagRestart) {
        out("\nRESTART Windows to finish the removal.\n");
    }
    out("\nTo install the package again, run INSTALL.BAT.\n");
    return inst_exit_code(0, flagFailed, flagRestart, flagSteps);
}

static void uninstall_steps_9x(void)
{
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
}

/* 98 SE and ME: the package's Enum entries, present or not (record 26
 * section 9.3 step 4, record 27 section 9): every HKLM\Enum\<enumerator>\
 * <device>\<instance> whose driver key's NTMPDriver names one of the
 * package's drivers, and the bus's own ids. */
typedef struct _UN9X {
    char Id[INST_ID_CHARS];
    char Desc[128];
    char Service[64];
    int Present;
} UN9X;

#define INST_MAX_UN9X 256
static UN9X *un9x;
static int un9xCount;

/* 1 the entry HKLM\Enum\<id> exists, 0 it does not, -1 unreadable. */
static int enum9x_exists(const char *id)
{
    char path[INST_ID_CHARS + 16];
    HKEY k;
    LONG e;

    inst_copy(path, "Enum\\", sizeof(path));
    if (strlen(path) + strlen(id) >= sizeof(path)) {
        return -1;
    }
    strcat(path, id);
    e = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k);
    if (e == ERROR_SUCCESS) {
        RegCloseKey(k);
        return 1;
    }
    return e == ERROR_FILE_NOT_FOUND ? 0 : -1;
}

/* One entry looked at: added when it is the package's. */
static void un9x_consider(const char *id)
{
    DEVREC d;
    UN9X *u;
    DWORD dn;

    memset(&d, 0, sizeof(d));
    inst_copy(d.Id, id, sizeof(d.Id));
    if (reg9x_value(id, "Driver", d.Driver, sizeof(d.Driver)) < 0) {
        out("  unreadable: the driver of %s\n", id);
        walkErrors++;
        return;
    }
    dev_driver_values(&d);
    if (!inst_own_service(d.Service) && inst_bus_id(id) == 0) {
        return;
    }
    if (un9xCount >= INST_MAX_UN9X) {
        devTruncated = 1;
        return;
    }
    u = &un9x[un9xCount++];
    memset(u, 0, sizeof(*u));
    inst_copy(u->Id, id, sizeof(u->Id));
    inst_copy(u->Service, d.Service, sizeof(u->Service));
    reg9x_value(id, "DeviceDesc", u->Desc, sizeof(u->Desc));
    u->Present = api.Locate != NULL &&
                 api.Locate(&dn, (char *)id, 0) == INST_CR_SUCCESS;
}

/* The walk of HKLM\Enum, three levels. 1 when read whole. */
static int un9x_scan(void)
{
    HKEY e1;
    HKEY e2;
    HKEY e3;
    char n1[64];
    char n2[INST_ID_CHARS];
    char n3[INST_ID_CHARS];
    char id[INST_ID_CHARS * 2];
    DWORD i;
    DWORD j;
    DWORD l;
    DWORD n;
    LONG r;

    un9xCount = 0;
    walkErrors = 0;
    devTruncated = 0;
    if (un9x == NULL) {
        un9x = (UN9X *)calloc(INST_MAX_UN9X, sizeof(UN9X));
        if (un9x == NULL) {
            return 0;
        }
    }
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum", 0, KEY_READ, &e1) !=
        ERROR_SUCCESS) {
        out("HKLM\\Enum could not be read.\n");
        return 0;
    }
    for (i = 0;; i++) {
        n = sizeof(n1);
        r = RegEnumKeyExA(e1, i, n1, &n, NULL, NULL, NULL, NULL);
        if (r != ERROR_SUCCESS) {
            if (r != ERROR_NO_MORE_ITEMS) {
                walkErrors++;
            }
            break;
        }
        if (RegOpenKeyExA(e1, n1, 0, KEY_READ, &e2) != ERROR_SUCCESS) {
            walkErrors++;
            continue;
        }
        for (j = 0;; j++) {
            n = sizeof(n2);
            r = RegEnumKeyExA(e2, j, n2, &n, NULL, NULL, NULL, NULL);
            if (r != ERROR_SUCCESS) {
                if (r != ERROR_NO_MORE_ITEMS) {
                    walkErrors++;
                }
                break;
            }
            if (RegOpenKeyExA(e2, n2, 0, KEY_READ, &e3) != ERROR_SUCCESS) {
                walkErrors++;
                continue;
            }
            for (l = 0;; l++) {
                n = sizeof(n3);
                r = RegEnumKeyExA(e3, l, n3, &n, NULL, NULL, NULL, NULL);
                if (r != ERROR_SUCCESS) {
                    if (r != ERROR_NO_MORE_ITEMS) {
                        walkErrors++;
                    }
                    break;
                }
                _snprintf(id, sizeof(id) - 1, "%s\\%s\\%s", n1, n2, n3);
                id[sizeof(id) - 1] = '\0';
                if (strlen(id) < INST_ID_CHARS) {
                    un9x_consider(id);
                }
            }
            RegCloseKey(e3);
        }
        RegCloseKey(e2);
    }
    RegCloseKey(e1);
    if (devTruncated || walkErrors) {
        out("The device entries could not be read whole: nothing is "
            "removed.\n");
        return 0;
    }
    return 1;
}

/* Record 27 section 9: each entry of the package removed through the
 * helper, children first, the tree's Enum entry read again after each; an
 * entry left after its removal, a removal cancelled or failed stops the
 * destructive steps. 1 every entry gone; 0 stopped; -1 the helper is not
 * usable here (the printed steps instead). */
static int remove_9x_devnodes(void)
{
    INST_DI_RESULT r;
    DEVREC d;
    int adopt;
    int rank;
    int i;
    int v;
    int o;

    if (!un9x_scan()) {
        flagFailed = 1;
        return 0;
    }
    /* The USB storage devices Windows bound from this installer's copy
     * after the run (record 26 section 6.5.8 step 1; owner, 2026-10-20):
     * counted here, adopted after the gate, before anything is removed. */
    adopt = stor_un_adopt(0);
    if (adopt < 0) {
        flagFailed = 1;
        return 0;
    }
    if (un9xCount == 0 && !s98_rec_devices_any(1) &&
        !stor_rec_devices_any() && adopt == 0) {
        /* Nothing left that needs the helper; recorded Stor98 devices
         * already gone are cleared without it. */
        return s98_un_devices(NULL) && s98_un_verify();
    }
    out("The devices of this package, present or not:\n");
    for (i = 0; i < un9xCount; i++) {
        out("  %s (%s%s)\n", un9x[i].Desc[0] ? un9x[i].Desc : un9x[i].Id,
            un9x[i].Service[0] ? un9x[i].Service : "the bus's",
            un9x[i].Present ? "" : ", not present");
    }
    if (opts.List) {
        out("(/list) each would be removed through Windows' own device "
            "installer.\n\n");
        stor_un_adopt(1);
        stor_un_devices();
        s98_un_devices(NULL);
        return 1;
    }
    if (!helper_ready()) {
        return -1;
    }
    /* Record 27 section 6's gate, before the first removal too: the tree
     * read and settled below this package's controllers, no hardware
     * wizard open, and the removals' list read again after it. */
    if (!scan()) {
        flagFailed = 1;
        return 0;
    }
    out("Waiting for the devices to settle ... ");
    v = settle(INST_FIRST_WAIT_MS, 20000UL, 0UL, 0);
    if (v <= 0) {
        settle_failed(v, "a minute");
        return 0;
    }
    out("done.\n");
    if (!gate_9x(1)) {
        return 0;
    }
    if (!un9x_scan()) {
        flagFailed = 1;
        return 0;
    }
    /* The USB storage drivers' devices first, adopted now that the tree
     * is settled and no wizard is open, then the recorded ones removed,
     * units then Bulk-Only devices (record 26 section 6.5.8). */
    if (stor_un_adopt(1) < 0 || !stor_un_devices()) {
        return 0;
    }
    for (rank = 0; rank <= 5; rank++) {
        /* After the UAS devices, the bundled Stor98's recorded devices,
         * before the NHI and the parents (record 26 section 9.3 step 4). */
        if (rank == 1 && !s98_un_devices(NULL)) {
            return 0;
        }
        for (i = 0; i < un9xCount; i++) {
            UN9X *u = &un9x[i];
            if (inst_remove_rank(u->Id, u->Service, 0) != rank) {
                continue;
            }
            /* Gone with its parent: not an error (record 27 section 9). */
            if (enum9x_exists(u->Id) == 0) {
                continue;
            }
            if (!go_on()) {
                return 0;
            }
            memset(&d, 0, sizeof(d));
            inst_copy(d.Id, u->Id, sizeof(d.Id));
            inst_copy(d.Desc, u->Desc, sizeof(d.Desc));
            out("Removing %s ...\n", u->Desc[0] ? u->Desc : u->Id);
            step(INST_STEP_BEGIN, "Removing %s", u->Desc[0] ? u->Desc : u->Id);
            if (helperState < 0) {
                step(INST_STEP_FAILED, "%s: the helper is not running",
                     u->Desc[0] ? u->Desc : u->Id);
                return -1;
            }
            v = helper_job(INST_DI_REMOVE, &d, NULL, 0, &r);
            o = inst_di_outcome(INST_DI_REMOVE, v);
            out("  %s%s%s.\n", inst_di_verdict_text(v), r.Error[0] ? ": " : "",
                r.Error);
            if (o == INST_DI_OUT_STEPS) {
                step(INST_STEP_LEFT, "%s: cancelled", u->Desc[0] ? u->Desc :
                     u->Id);
                flagSteps = 1;
                return 0;
            }
            if (o == INST_DI_OUT_FAILED || o == INST_DI_OUT_REFUSED) {
                step(INST_STEP_FAILED, "%s: %s", u->Desc[0] ? u->Desc : u->Id,
                     inst_di_verdict_text(v));
                flagFailed = 1;
                return 0;
            }
            if (o == INST_DI_OUT_RESTART) {
                flagRestart = 1;
            }
            flagChanged = 1;
            v = enum9x_exists(u->Id);
            if (v != 0) {
                /* Record 27 section 9: an entry left is an incomplete
                 * uninstall (exit 4); an Enum that cannot be read, a
                 * failure. */
                out("  %s is %s after its removal.\n", u->Id,
                    v > 0 ? "still recorded" : "unreadable");
                step(INST_STEP_FAILED, "%s: still recorded", u->Desc[0] ?
                     u->Desc : u->Id);
                if (v > 0) {
                    flagSteps = 1;
                } else {
                    flagFailed = 1;
                }
                return 0;
            }
            step(INST_STEP_DONE, "%s removed", u->Desc[0] ? u->Desc : u->Id);
        }
    }
    /* A helper whose process did not end after the last job leaves this
     * run's removals unconfirmed (record 27 section 4.1). */
    if (helperState < 0) {
        flagSteps = 1;
        return 0;
    }
    /* Stor98's removed devices, read again after every other removal:
     * one Windows made again stops the uninstall with its record kept. */
    if (!s98_un_verify()) {
        return 0;
    }
    /* Windows may have made an entry again while the others went: a
     * fresh, whole reading must find none before anything else goes. */
    if (!un9x_scan()) {
        flagFailed = 1;
        return 0;
    }
    if (un9xCount != 0) {
        out("Still recorded after the removals:\n");
        for (i = 0; i < un9xCount; i++) {
            out("  %s\n", un9x[i].Id);
        }
        flagSteps = 1;
        return 0;
    }
    return 1;
}

/* 98 SE and ME: the setup engine's record of an INF copy the uninstall
 * deleted (its OEM Name value, record 23 section 9.2 step 4), removed;
 * only a value named by that copy's path or short alias. */
static void oem_name_forget(const char *path, const char *shortPath)
{
    char name[INST_PATH_CHARS];
    HKEY k;
    DWORD i = 0;
    DWORD n;
    LONG r;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_OEM_NAME_KEY, 0,
                      KEY_QUERY_VALUE | KEY_SET_VALUE, &k) != ERROR_SUCCESS) {
        return;
    }
    for (;;) {
        n = sizeof(name);
        r = RegEnumValueA(k, i, name, &n, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r == ERROR_SUCCESS && inst_oem_name_ours(name, path, shortPath) &&
            RegDeleteValueA(k, name) == ERROR_SUCCESS) {
            /* The values after it move down one: the same index again. */
            out("Removed the setup engine's record of it (SetupX OEM Name).\n");
            continue;
        }
        i++;
    }
    RegCloseKey(k);
}

/* The INF copies the device installer keeps for this package (record 23
 * section 9.2 step 4's rule), in INF and INF\OTHER, each with its .PNF:
 * listed (list 1), or deleted once no entry of the package is left. 0
 * when one could not be deleted. */
static int infs_9x(int list)
{
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char pnf[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char *text;
    size_t n;
    int i;
    int ok = 1;
    DWORD e;

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        const char *sub = i == 0 ? "INF" : "INF\\OTHER";
        join(pattern, sizeof(pattern), win, sub);
        join(pattern, sizeof(pattern), pattern, "*.INF");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            /* Only an empty or absent directory is "nothing there". */
            e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND &&
                e != ERROR_NO_MORE_FILES) {
                out("%s could not be listed (error %lu).\n", pattern,
                    (unsigned long)e);
                ok = 0;
            }
            continue;
        }
        do {
            join(path, sizeof(path), win, sub);
            join(path, sizeof(path), path, fd.cFileName);
            text = read_file(path, NULL);
            if (text == NULL) {
                /* Whose it is cannot be established: the steps after it
                 * wait (record 23 section 9.2 step 4's rule). */
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
            if (list) {
                out("  %s\n", path);
                continue;
            }
            inst_copy(pnf, path, sizeof(pnf));
            n = strlen(pnf);
            if (n > 4) {
                strcpy(pnf + n - 4, ".PNF");
            }
            SetFileAttributesA(pnf, FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileA(pnf)) {
                e = GetLastError();
                if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) {
                    out("%s could not be deleted (error %lu).\n", pnf,
                        (unsigned long)e);
                    ok = 0;
                    continue;
                }
            }
            SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileA(path)) {
                out("%s could not be deleted (error %lu).\n", path,
                    (unsigned long)GetLastError());
                ok = 0;
                continue;
            }
            flagChanged = 1;
            out("Deleted %s (and its .PNF).\n", path);
            join(pnf, sizeof(pnf), win, sub);
            if (fd.cAlternateFileName[0] != '\0') {
                join(pnf, sizeof(pnf), pnf, fd.cAlternateFileName);
            } else {
                pnf[0] = '\0';
            }
            oem_name_forget(path, pnf);
        } while (FindNextFileA(h, &fd));
        e = GetLastError();
        FindClose(h);
        if (e != ERROR_NO_MORE_FILES) {
            out("%s could not be listed to its end (error %lu).\n", pattern,
                (unsigned long)e);
            ok = 0;
        }
    }
    return ok;
}

/* The driver files in SYSTEM32\DRIVERS: listed, or deleted where not in
 * use; one still held is left, with a step and the restart. */
static void files_9x(int list)
{
    char win[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    int i;

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return;
    }
    for (i = 0; i < INST_SVC_COUNT; i++) {
        join(path, sizeof(path), win, "SYSTEM32\\DRIVERS");
        join(path, sizeof(path), path, instFiles[i]);
        if (!file_exists(path, NULL)) {
            continue;
        }
        if (list) {
            out("  %s\n", path);
            continue;
        }
        SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileA(path)) {
            flagChanged = 1;
            out("Deleted %s.\n", path);
        } else {
            out("%s is still in use: restart Windows, then run the uninstall "
                "again to delete it.\n", path);
            flagRestart = 1;
            flagSteps = 1;
        }
    }
}

static int run_uninstall_9x(void)
{
    char dir[INST_PATH_CHARS];
    char win[INST_PATH_CHARS];
    int r;

    if (!warn_uninstall()) {
        return INST_EXIT_REFUSED;
    }
    if (!local_dir(dir, sizeof(dir)) ||
        GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return INST_EXIT_REFUSED;
    }
    r = remove_9x_devnodes();
    if (opts.List) {
        s98_un_rest();
        out("(/list) the copies in %s and their DevicePath entry would be "
            "removed.\n", dir);
        out("\nThe INF copies Windows keeps for this package:\n");
        infs_9x(1);
        out("The driver files:\n");
        files_9x(1);
        stor_un_rest();
        stor_tools_remove();
        stor_silent_remove();
        finish_remove();
        return INST_EXIT_DONE;
    }
    if (r == 0) {
        return stop_uninstall("a device of this package could not be removed, "
                              "or is still recorded.");
    }
    if (r < 0) {
        /* No helper here: the devices are removed by hand first, and
         * nothing else goes before they are (record 26 section 9.3); the
         * next run, finding none, removes the rest. */
        out("\nRemove these devices by hand, in this order:\n"
            "  1. Device Manager: remove \"xHCI98 USB Attached SCSI Storage\" "
            "and an\n     \"xHCI98 Thunderbolt 2 Controller\", \"xHCI98 "
            "Thunderbolt 3 Controller\" or\n     \"xHCI98 USB4 Host Router\" "
            "if there is one; then \"xHCI98 USB 3.x\n     eXtensible Host "
            "Controller\" (it removes the root hub and everything\n     "
            "beneath it).\n"
            "  2. Without restarting, run the uninstall again: it removes the "
            "rest.\n");
        return stop_uninstall("the devices are left for the steps above.");
    }
    if (!go_on()) {
        return stop_uninstall("cancelled.");
    }
    /* The copies go only once DevicePath no longer names them: a search
     * path must never point at a removed source. */
    if (!devicepath_update(NULL)) {
        out("DevicePath could not be updated: the copies in %s stay.\n", dir);
        flagFailed = 1;
        return stop_uninstall("DevicePath could not be updated.");
    }
    flagChanged = 1;
    if (!copies_remove_all()) {
        flagSteps = 1;
        return stop_uninstall("the package copy could not be removed whole.");
    }
    if (!infs_9x(0)) {
        flagFailed = 1;
        return stop_uninstall("an INF copy of this package could not be "
                              "deleted.");
    }
    if (!s98_un_rest()) {
        return stop_uninstall("Stor98's INF copies or its driver could not be "
                              "removed.");
    }
    files_9x(0);
    if (!stor_un_rest()) {
        return stop_uninstall("a USB storage file is still in use, or its "
                              "record could not be read.");
    }
    if (!stor_tools_remove()) {
        return stop_uninstall("a USB storage file in the tools folder could "
                              "not be removed.");
    }
    if (!stor_silent_remove()) {
        return stop_uninstall("a SilentClasses entry of the USB storage "
                              "drivers could not be removed.");
    }
    if (flagSteps || flagFailed) {
        return stop_uninstall("a driver file is still in use.");
    }
    finish_remove();
    out("\nWindows finds the controller again at the next start and has no "
        "driver for it:\non a machine whose only USB controller is xHCI, the "
        "USB keyboard and mouse stay\ndead until another driver is "
        "installed.\n");
    return inst_exit_code(0, flagFailed, flagRestart, flagSteps);
}

/* ======================================================================== */
/* The self-test (the pure half's own suite is test\test_inst.c)             */
/* ======================================================================== */

static int stChecks;
static int stFailures;
static int selftest_screens(void);

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
    st(selftest_screens(), "every screen within 79 columns");
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
static int tbnFull;         /* tbn holds INST_MAX_DEVS nodes          */
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
            out("  information from the driver: confirmed now (%lu changes "
                "since it started)\n",
                (unsigned long)inst_get32(h->Rec, CM_ST_O_GENERATION));
        } else {
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            out("  information from the driver: NOT confirmed by the running "
                "driver\n%s%s",
                h->Asked ? "" : envNow.Nt ? "  (run as an administrator to "
                                            "confirm it)\n" : "",
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
                out("  Behind the Thunderbolt port\n    %s:\n",
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
                out("\nTo eject: INSTALL /console /eject 1   (or XHCIINST /eject 1)\n");
                out(envNow.Nt ? "  in a script:\n    INSTALL /console /eject \"id=%s\" /yes\n"
                              : "  in a script:\n    INSTALL /console /eject id=%s /yes\n",
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

/* NT: this run's own successful CM_Request_Device_EjectA confirmed to the
 * driver (record 21 section 7.4, decision M16; on Windows 2000 nothing
 * else confirms an eject): the unit's entry in the marks answer whose
 * departure is pending, written as XhciCmEjectConfirm (cm_query.h CM_QC_*)
 * into the NHI's driver key, only while the value is absent or empty. The
 * run holds the eject lock (tb_lock) every writer of the value takes. */
static void tb_confirm(int nhiK, int u, INST_U32 member)
{
    char path[INST_PATH_CHARS];
    char name[64];
    char dev[80];
    unsigned char c[INST_QC_BYTES];
    unsigned char old[64];
    unsigned char *b;
    HKEY k;
    HANDLE dh;
    DWORD type;
    DWORD n;
    DWORD got;
    DWORD t0;
    INST_U32 need;
    LONG w;
    int r = 0;

    if (nhiK < 0 || tbh[nhiK].Node < 0 || !tbn[u].HasLoc ||
        tbh[nhiK].DrvPath[0] == '\0') {
        logonly("eject confirmation: no location or driver key\n");
        return;
    }
    join(path, sizeof(path), "SYSTEM\\CurrentControlSet\\Enum",
         tbn[tbh[nhiK].Node].R.Id);
    join(path, sizeof(path), path, "Device Parameters");
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
        logonly("eject confirmation: the NHI names no query device\n");
        return;
    }
    _snprintf(dev, sizeof(dev) - 1, "\\\\.\\%s", name);
    dev[sizeof(dev) - 1] = '\0';
    b = (unsigned char *)malloc(CM_QM_MAX_BYTES);
    dh = CreateFileA(dev, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                     OPEN_EXISTING, 0, NULL);
    if (b == NULL || dh == INVALID_HANDLE_VALUE) {
        logonly("eject confirmation: %s did not open\n", dev);
        free(b);
        if (dh != INVALID_HANDLE_VALUE) {
            CloseHandle(dh);
        }
        return;
    }
    /* The departure is recorded as Windows removes the unit's stack; a
     * moment is allowed for the answer to show it. */
    t0 = GetTickCount();
    for (;;) {
        got = 0;
        r = DeviceIoControl(dh, CM_QM_IOCTL, NULL, 0, b, CM_QM_MAX_BYTES, &got,
                            NULL)
                ? inst_qc_build(b, got, member, tbn[u].Bus, tbn[u].Addr, c,
                                &need)
                : 0;
        if (r != 0 || GetTickCount() - t0 >= INST_TB_ASK_WAIT_MS) {
            break;
        }
        Sleep(250);
    }
    CloseHandle(dh);
    free(b);
    if (r != 1) {
        logonly("eject confirmation: no single pending departure at bus %lu "
                "address 0x%08lX (%d)\n", (unsigned long)tbn[u].Bus,
                (unsigned long)tbn[u].Addr, r);
        return;
    }
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, tbh[nhiK].DrvPath, 0,
                      KEY_QUERY_VALUE | KEY_SET_VALUE, &k) != ERROR_SUCCESS) {
        logonly("eject confirmation: the driver key %s is not writable\n",
                tbh[nhiK].DrvPath);
        return;
    }
    n = sizeof(old);
    w = RegQueryValueExA(k, CM_QC_VALUE, NULL, &type, old, &n);
    if (w == ERROR_FILE_NOT_FOUND || (w == ERROR_SUCCESS && n == 0)) {
        w = RegSetValueExA(k, CM_QC_VALUE, 0, REG_BINARY, c, INST_QC_BYTES);
        logonly("eject confirmation: %s (serial %lu) %s\n", CM_QC_VALUE,
                (unsigned long)inst_get32(c, CM_QC_O_SERIAL),
                w == ERROR_SUCCESS ? "written" : "refused");
    } else {
        logonly("eject confirmation: another confirmation waits; nothing "
                "written\n");
    }
    RegCloseKey(k);
}

/* The device to eject, one choice a line (owner, 2026-10-16). 0 when
 * cancelled (Enter, or end of input). */
static int eject_choose(INST_U32 *n)
{
    char line[64];
    int i;

    out("\nWhich device?\n");
    for (i = 0; i < tbnCount; i++) {
        if (tbn[i].Number != 0) {
            out("  %-2d  %s\n", tbn[i].Number,
                tbn[i].R.Desc[0] ? tbn[i].R.Desc : first_hw(&tbn[i].R));
        }
    }
    if (!ask(INST_Q_EJECT_PICK, "  Enter for [cancel]: ", 0, 0, 0, NULL, line,
             sizeof(line)) || line[strspn(line, " \t")] == '\0') {
        return 0;
    }
    *n = (INST_U32)strtoul(line, NULL, 10);
    return 1;
}

static int eject_confirm(void)
{
    char line[64];
    char text[1024];

    inst_yesno_question(text, sizeof(text), "Eject it now?", "eject it",
                        "cancel (so does anything else)", 0);
    return ask(INST_Q_EJECT_OK, text, 0, 0, 0, NULL, line, sizeof(line)) &&
           inst_tb_answer(line, 0) == 1;
}

static int run_eject(void)
{
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
        /* /eject is for Thunderbolt and USB4 devices (owner, 2026-10-22). */
        out("\nThere is nothing to eject.\nFor a USB drive, use Safely Remove "
            "in the xHCI98 USB Tree.\n");
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
        if (n == 0 && !eject_choose(&n)) {
            out("\nCancelled.\n");
            tb_unask();
            return INST_EXIT_REFUSED;
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
        if (!eject_confirm()) {
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
        } else {
            tb_confirm(nhiK, u, portBdf);
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
            "Thunderbolt driver\nlooking at the port again, can start it "
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
    int asked;
    int def;
    int r;
    int k;
    int installed;
    char line[16];

    tbInclude = tb_decide(&asked, &def, &installed);
    if (!haveCm) {
        tbInclude = 0;
        return;
    }
    out("Thunderbolt 3 / USB4 support (experimental)\n");
    if (tbNhiPresent) {
        if (strlen(tbNhiName) + 49 <= INST_COLUMNS) {
            out("  A Thunderbolt 3 / USB4 controller was found: %s.\n",
                tbNhiName);
        } else {
            out("  A Thunderbolt 3 / USB4 controller was found:\n    %s.\n",
                tbNhiName);
        }
    } else {
        out("  No Thunderbolt 3 / USB4 controller was found (some laptops show "
            "it only with\n  a device plugged in or a BIOS setting): answer Y "
            "only if this computer has\n  a Thunderbolt or USB4 port.\n");
    }
    out("  With it, a device plugged into that port is approved automatically "
        "and can\n  reach the computer's memory (the setting XhciCmApprove; "
        "see\n  tools\\xhciinst\\readme.txt).\n");
    if (installed && !asked) {
        out("  Answering N keeps the Thunderbolt driver already installed as it "
            "is.\n");
    }
    if (asked) {
        char text[1024];
        inst_yesno_question(text, sizeof(text),
                            "Install Thunderbolt 3 / USB4 support?",
                            "install it",
                            installed ? "leave it out (the driver already "
                                        "installed stays as it is)"
                                      : "leave it out", def);
        for (k = 0;; k++) {
            if (!ask(INST_Q_TB, k == 0 ? text : def ? "  Type Y or N, or "
                     "Enter for [Y]: " : "  Type Y or N, or Enter for [N]: ",
                     k > 0, 0, def, NULL, line, sizeof(line))) {
                r = def;
                out("\n");
                break;
            }
            r = inst_tb_answer(line, def);
            if (r >= 0) {
                break;
            }
        }
        tbInclude = r;
    } else if (!usbInclude) {
        out("  The connection manager alone: installed without asking.\n");
    } else if (opts.Thunderbolt) {
        out("  /thunderbolt: installed without asking.\n");
    } else if (opts.List) {
        out("  /list: it would be asked:\n"
            "    Y  install it\n    N  leave it out\n"
            "  Enter would give [%s].\n", def ? "Y" : "N");
    } else {
        out("  /yes: N (/thunderbolt installs it).\n");
    }
    out("\n");
    logonly("thunderbolt: present %d, installed %d, asked %d, default %d, "
            "include %d\n", tbNhiPresent, installed, asked, def, tbInclude);
    tb_apply(tbInclude);
}

/* The decision's inputs (record 23 section 8.5): whether a controller is
 * present (tbNhiPresent), what Enter or /yes gives, whether a Thunderbolt
 * driver is installed now, and whether the question is asked. Returns the
 * decision when it is not asked. */
static int tb_decide(int *asked, int *def, int *installed)
{
    int i;
    int include;

    tb_apply(1);
    tbNhiPresent = envNow.CmNhi;
    /* Without the USB drivers the Thunderbolt support is all the run
     * installs: in it unasked (section 8.5, revision 13). */
    include = inst_tb_decide(haveCm, tbNhiPresent, opts.Yes,
                             opts.Thunderbolt || !usbInclude, opts.List, asked,
                             def);
    *installed = 0;
    for (i = 0; i < devCount; i++) {
        if (inst_eq(devs[i].Service, "xhcicm") ||
            inst_msz_has(devs[i].Upper, devs[i].UpperBytes, "xhcicm")) {
            *installed = 1;
        }
    }
    return haveCm ? include : 0;
}

/* The Thunderbolt INFs in this run or out of it (a No to the question,
 * the window's box unticked), and the plan made again for it. The texts
 * are kept, so the window can tick the box again. */
/* Stor98 in this run or out of it (the Components page's box, the console's
 * question, /stor98), and the plan made again for it. Stor98 needs the
 * Thunderbolt component (owner, 2026-10-19): it is in the run only while
 * that component is - the window's rule for its box, inst_s98_box_open -
 * so tb_apply sets it again too. */
static void s98_set(void)
{
    s98Include = s98Want && inst_s98_box_open(haveS98, haveCm, tbInclude);
    infText[INST_INF_S98] = s98Include ? s98Text : NULL;
    infIds[INST_INF_S98].Count = 0;
    if (s98Include) {
        inst_inf_models(s98Text, &envNow, &infIds[INST_INF_S98]);
    }
}

static void s98_apply(int include)
{
    s98Want = include;
    s98_set();
    if (s98Want && haveS98 && !s98Include) {
        logonly("stor98: left out, %s\n",
                inst_s98_skip_text(INST_S98_NEEDS_TB, 0));
    }
    plan();
}

/* The USB drivers in this run or out of it (record 23 section 4.5): the
 * connection manager alone takes xhci98.inf and xhciuas.inf out, as a No
 * to the Thunderbolt question takes its INFs out. The texts are kept, so
 * the window can tick the box again; the caller plans again. */
static void usb_apply(int include)
{
    int k;

    for (k = INST_INF_XHCI; k <= INST_INF_UAS; k++) {
        if (infKeep[k] == NULL) {
            infKeep[k] = infText[k];
        }
        infText[k] = (include && (k != INST_INF_UAS || uasInclude)) ?
                     infKeep[k] : NULL;
    }
    usbInclude = include ? 1 : 0;
}

static void tb_apply(int include)
{
    int k;

    for (k = INST_INF_CM; k <= INST_INF_CMF; k++) {
        if (infKeep[k] == NULL) {
            infKeep[k] = infText[k];
        }
        infText[k] = (include && haveCm) ? infKeep[k] : NULL;
    }
    tbInclude = include && haveCm;
    s98_set();
    plan();
}

/* After a Yes with no controller present (section 8.5). */
static void tb_later_note(const char *dir)
{
    if (!haveCm || !tbInclude || tbNhiPresent || opts.List) {
        return;
    }
    if (envNow.Nt && !tbStaged) {
        out("\nThe Thunderbolt driver was not staged (see above): run "
            "INSTALL.BAT again once\nthe controller appears.\n");
    } else if (envNow.Nt) {
        out("\nThe Thunderbolt driver is staged: when the controller appears, "
            "Windows installs\nit by itself. Run INSTALL.BAT again then, so "
            "the Thunderbolt ports get their\nfilter too.\n");
    } else {
        out("\nThe Thunderbolt driver is in %s. When the controller "
            "appears:\n  1. The hardware wizard finds xhcicm.inf there by "
            "itself: \"Search for the\n     best driver\", untick every box.\n"
            "  2. Asked for the \"xhci98 xHCI Host Controller Disk\": type "
            "%s\n     under \"Copy files from\", OK.\n", dir, dir);
    }
}

/* ======================================================================== */
/* Stor98, the bundled NVMe driver (record 26 sections 5.6, 6.3 and 6.4)     */
/* ======================================================================== */

/* What runs a device, for the record's rules: 0 no driver, 1 another
 * driver, 2 Stor98. */
static int s98_state_of(const DEVREC *d)
{
    if (inst_eq(d->Service, "stor98")) {
        return 2;
    }
    if (d->Problem == 28UL) {
        return 0;
    }
    if (d->Service[0] != '\0') {
        return 1;
    }
    if (!envNow.Nt && d->Driver[0] != '\0' && s98_loader_9x(d)) {
        return 1;
    }
    return 0;
}

static int s98_base_state(const char *id)
{
    int i;

    for (i = 0; i < s98BaseCount; i++) {
        if (inst_eq(s98Base[i].Id, id)) {
            return s98Base[i].State;
        }
    }
    return 0;   /* not there at the baseline: it appeared with no driver */
}

/* A value under the record's key (sub NULL: the key itself, else one of
 * its subkeys). 1 written. */
static int s98_rec_put(const char *sub, const char *name, DWORD type,
                       const void *data, DWORD bytes)
{
    char path[INST_PATH_CHARS];
    HKEY k;
    LONG w;

    if (opts.List) {
        return 1;
    }
    /* No record before a Stor98 change (record 26 section 6.3, draft 16):
     * the baseline's PreExisting goes first, with the first entry. */
    if (s98PreOwed && !(sub == NULL && inst_eq(name, "PreExisting"))) {
        DWORD one = (DWORD)s98Pre;
        if (!s98_rec_put(NULL, "PreExisting", REG_DWORD, &one, sizeof(one))) {
            return 0;
        }
        s98PreOwed = 0;
    }
    inst_copy(path, INST_S98_KEY, sizeof(path));
    if (sub != NULL) {
        join(path, sizeof(path), path, sub);
    }
    w = RegCreateKeyExA(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &k, NULL);
    if (w == ERROR_SUCCESS) {
        w = RegSetValueExA(k, name, 0, type, (const BYTE *)data, bytes);
        RegCloseKey(k);
    }
    if (w != ERROR_SUCCESS) {
        out("Stor98's install record could not be written (HKLM\\%s, %s: "
            "error %ld).\n", path, name, (long)w);
        flagFailed = 1;
        return 0;
    }
    logonly("stor98 record: %s\\%s\n", sub != NULL ? sub : "", name);
    return 1;
}

static int s98_rec_text(const char *sub, const char *name, const char *text)
{
    return s98_rec_put(sub, name, REG_SZ, text, (DWORD)strlen(text) + 1);
}

/* 1 when the record holds the value, 0 not, -1 unreadable. */
static int s98_rec_has(const char *sub, const char *name)
{
    char path[INST_PATH_CHARS];
    HKEY k;
    LONG r;

    inst_copy(path, INST_S98_KEY, sizeof(path));
    if (sub != NULL) {
        join(path, sizeof(path), path, sub);
    }
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    r = RegQueryValueExA(k, name, NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    return r == ERROR_SUCCESS ? 1 : r == ERROR_FILE_NOT_FOUND ? 0 : -1;
}

static int s98_rec_del(const char *sub, const char *name)
{
    char path[INST_PATH_CHARS];
    HKEY k;
    LONG r;

    if (opts.List) {
        return 1;
    }
    inst_copy(path, INST_S98_KEY, sizeof(path));
    if (sub != NULL) {
        join(path, sizeof(path), path, sub);
    }
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_SET_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 1;
    }
    if (r == ERROR_SUCCESS) {
        r = RegDeleteValueA(k, name);
        RegCloseKey(k);
    }
    if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
        out("Stor98's install record could not be updated (%s: error "
            "%ld).\n", name, (long)r);
        flagFailed = 1;
        return 0;
    }
    return 1;
}

/* The i-th value name of a subkey of the record: 1, 0 at the end, -1 an
 * error. */
static int s98_rec_enum(const char *sub, DWORD i, char *name, DWORD cap)
{
    char path[INST_PATH_CHARS];
    HKEY k;
    LONG r;
    DWORD n = cap;

    join(path, sizeof(path), INST_S98_KEY, sub);
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    r = RegEnumValueA(k, i, name, &n, NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    if (r == ERROR_NO_MORE_ITEMS) {
        return 0;
    }
    return r == ERROR_SUCCESS ? 1 : -1;
}

/* 98 SE and ME: 1 when a driver key of the SCSI adapters names Stor98's
 * port driver, 0 none, -1 unreadable. */
static int s98_driver_keys_9x(void)
{
    HKEY cls;
    char sub[16];
    char path[INST_PATH_CHARS];
    char text[64];
    DWORD i;
    DWORD n;
    LONG r;
    int g;
    int found = 0;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_S98_CLASS_9X, 0, KEY_READ,
                      &cls);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    for (i = 0;; i++) {
        n = sizeof(sub);
        r = RegEnumKeyExA(cls, i, sub, &n, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS) {
            found = -1;
            break;
        }
        join(path, sizeof(path), INST_S98_CLASS_9X, sub);
        g = reg_text3(HKEY_LOCAL_MACHINE, path, "PortDriver", text,
                      sizeof(text));
        if (g < 0) {
            /* Unreadable is not "not Stor98's". */
            found = -1;
            break;
        }
        if (g > 0 && inst_eq(text, "stor98.mpd")) {
            found = 1;
        }
    }
    RegCloseKey(cls);
    return found;
}

/* 1 when any devnode, present or not, is bound to Stor98 (NT: Service;
 * 98 SE and ME: a driver key naming its port driver); -1 unreadable. */
static int s98_bound_any(void)
{
    int i;

    if (!envNow.Nt) {
        return s98_driver_keys_9x();
    }
    for (i = 0; i < devCount; i++) {
        if (inst_eq(devs[i].Service, "stor98")) {
            return 1;
        }
    }
    return 0;
}

/* Record 26 section 6.3's first bullet, "before anything": what Stor98 the
 * machine has, kept for the record's rules; PreExisting written once. 0
 * stops the run (the baseline cannot be read, so no device could be told
 * the installer's). */
/* 98 SE and ME: 1 when the open Services\stor98 key holds a subkey, or a
 * value inst_s98_svc_value_stor98 counts; an unreadable entry counts. */
static int s98_svc_key_stor98_9x(HKEY k)
{
    char name[128];
    DWORD n;
    DWORD i;
    LONG r;

    n = sizeof(name);
    r = RegEnumKeyExA(k, 0, name, &n, NULL, NULL, NULL, NULL);
    if (r != ERROR_NO_MORE_ITEMS) {
        return 1;
    }
    for (i = 0;; i++) {
        n = sizeof(name);
        r = RegEnumValueA(k, i, name, &n, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            return 0;
        }
        if (r != ERROR_SUCCESS || inst_s98_svc_value_stor98(name)) {
            return 1;
        }
    }
}

static int s98_baseline(void)
{
    HKEY k;
    LONG r;
    int any = 0;
    int b;
    int i;
    int h;
    DWORD pre;
    DWORD one;

    s98BaseCount = 0;
    s98Baseline = 0;
    if (!s98Include) {
        return 1;
    }
    if (s98Base == NULL) {
        s98Base = (S98BASE *)calloc(INST_MAX_DEVS, sizeof(S98BASE));
        if (s98Base == NULL) {
            out("Out of memory: nothing was changed.\n");
            return 0;
        }
    }
    /* A service or settings key of Stor98's (record 26 section 6.3: a
     * service or driver key found is Stor98 there before). */
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      envNow.Nt ? INST_S98_SVC_NT : INST_S98_SVC_9X, 0,
                      KEY_READ, &k);
    if (r == ERROR_SUCCESS) {
        /* 98 SE and ME: xhcicm.sys makes this key itself for the link-state
         * locator (its LinkState value), Stor98 installed or not; only a
         * value or subkey of another name is Stor98's (the 98 SE leg of
         * 2026-10-19). */
        any = envNow.Nt ? 1 : s98_svc_key_stor98_9x(k);
        RegCloseKey(k);
    } else if (r != ERROR_FILE_NOT_FOUND) {
        out("Whether Stor98 is installed cannot be read (error %ld): nothing "
            "was changed.\n", (long)r);
        return 0;
    }
    b = s98_bound_any();
    if (b < 0) {
        out("Whether Stor98 is installed cannot be read: nothing was "
            "changed.\n");
        return 0;
    }
    any |= b;
    /* Every devnode stor98.inf's models match, present or not, with what
     * runs it: NT's reading holds the phantoms too; on 98 SE and ME the
     * Enum entries are read (s98_base_9x). */
    if (envNow.Nt) {
        for (i = 0; i < devCount; i++) {
            const DEVREC *d = &devs[i];
            if (inst_ids_match(d->Hw, d->HwBytes, d->Compat, d->CompatBytes,
                               &infIds[INST_INF_S98]) < 0) {
                continue;
            }
            if (s98BaseCount >= INST_MAX_DEVS) {
                out("Too many NVMe devices to read: nothing was changed.\n");
                return 0;
            }
            inst_copy(s98Base[s98BaseCount].Id, d->Id, INST_ID_CHARS);
            s98Base[s98BaseCount].State = s98_state_of(d);
            s98BaseCount++;
        }
    } else if (!s98_base_9x()) {
        out("The NVMe devices' entries cannot be read whole: nothing was "
            "changed.\n");
        return 0;
    }
    for (i = 0; i < s98BaseCount; i++) {
        logonly("stor98 baseline: %s %d\n", s98Base[i].Id, s98Base[i].State);
        if (s98Base[i].State == 2) {
            any = 1;
        }
    }
    s98Baseline = 1;
    if (!s98_pending_resolve(0)) {
        s98Baseline = 0;
        out("Nothing was changed: run the installer again.\n");
        return 0;
    }
    /* PreExisting: written once, never cleared; a value there that cannot
     * be read refuses rather than being written over. */
    h = s98_rec_has(NULL, "PreExisting");
    if (h < 0 || (h > 0 && !reg_dword(HKEY_LOCAL_MACHINE, INST_S98_KEY,
                                      "PreExisting", &pre))) {
        out("Stor98's install record cannot be read: nothing was changed.\n");
        s98Baseline = 0;
        return 0;
    }
    s98PreOwed = 0;
    if (h > 0) {
        s98Pre = pre != 0;
    } else {
        /* Written with the run's first Stor98 change (s98_pre_commit, or
         * the first entry of the record), never in a run that makes none
         * (record 26 section 6.3, draft 16). */
        s98Pre = any;
        s98PreOwed = 1;
    }
    (void)one;
    logonly("stor98: found before %d, PreExisting %d\n", any, s98Pre);
    /* 98 SE and ME: the setup engine's INF copies, before anything. */
    s98_inf_take(0, 1);
    return 1;
}

/* ---- 98 SE and ME: the setup engine's INF copies ------------------------- */

static int s98_inf_dir(int slot, const char *rel, int all)
{
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    DWORD e;
    S98INF *x;
    char *buf;
    DWORD n;

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    join(pattern, sizeof(pattern), win, rel);
    join(pattern, sizeof(pattern), pattern, "*.INF");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
    }
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        if (s98InfCount[slot] >= INST_S98_MAX_INF) {
            FindClose(h);
            return 0;
        }
        x = &s98Inf[slot][s98InfCount[slot]++];
        x->Name[0] = '\0';
        if (strcmp(rel, "INF") != 0) {
            inst_copy(x->Name, "OTHER\\", sizeof(x->Name));
        }
        strncat(x->Name, fd.cFileName, sizeof(x->Name) - strlen(x->Name) - 1);
        x->Size = fd.nFileSizeLow;
        x->Write = fd.ftLastWriteTime;
        x->Hash[0] = '\0';
        /* Every listing is whole: each file's name, size and SHA-256. The
         * log holds the baseline's every line, and afterwards each file
         * that is new or whose hash changed since the listing before. */
        join(path, sizeof(path), win, rel);
        join(path, sizeof(path), path, fd.cFileName);
        n = 0;
        buf = read_file(path, &n);
        if (buf == NULL) {
            FindClose(h);
            logonly("stor98 INF listing: INF\\%s cannot be read\n", x->Name);
            return 0;
        }
        inst_sha256((const unsigned char *)buf, n, x->Hash);
        free(buf);
        if (all || s98_inf_changed(slot == 0 ? 1 : 0, x)) {
            logonly("stor98 INF listing: INF\\%s %lu %s\n", x->Name,
                    (unsigned long)x->Size, x->Hash);
        }
    } while (FindNextFileA(h, &fd));
    e = GetLastError();
    FindClose(h);
    return e == ERROR_NO_MORE_FILES;
}

/* The listing into slot 0 (before a step) or 1 (after it). */
static void s98_inf_take(int slot, int hash)
{
    s98InfOk[slot] = 0;
    s98InfCount[slot] = 0;
    if (envNow.Nt || !s98Include) {
        return;
    }
    if (s98Inf[slot] == NULL) {
        s98Inf[slot] = (S98INF *)calloc(INST_S98_MAX_INF, sizeof(S98INF));
        if (s98Inf[slot] == NULL) {
            return;
        }
    }
    s98InfOk[slot] = s98_inf_dir(slot, "INF", hash) &&
                     s98_inf_dir(slot, "INF\\OTHER", hash);
    logonly("stor98: INF listing %d, %d files, %s\n", slot,
            s98InfCount[slot], s98InfOk[slot] ? "whole" : "NOT whole");
}

static int s98_inf_in(int slot, const char *name)
{
    int i;

    for (i = 0; i < s98InfCount[slot]; i++) {
        if (inst_eq(s98Inf[slot][i].Name, name)) {
            return 1;
        }
    }
    return 0;
}

/* 1 when x is not in the other slot with the same size and hash. */
static int s98_inf_changed(int other, const S98INF *x)
{
    int i;

    if (s98Inf[other] == NULL) {
        return 1;
    }
    for (i = 0; i < s98InfCount[other]; i++) {
        const S98INF *y = &s98Inf[other][i];
        if (inst_eq(y->Name, x->Name)) {
            return y->Size != x->Size || strcmp(y->Hash, x->Hash) != 0;
        }
    }
    return 1;
}

/* The full path of a listed copy, and its SHA-256 for the log. */
static void s98_inf_log(const char *name, const char *why)
{
    char win[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char hex[65];
    char *buf;
    DWORD n = 0;

    hex[0] = '\0';
    if (GetWindowsDirectoryA(win, sizeof(win)) != 0) {
        join(path, sizeof(path), win, "INF");
        join(path, sizeof(path), path, name);
        buf = read_file(path, &n);
        if (buf != NULL) {
            inst_sha256((const unsigned char *)buf, n, hex);
            free(buf);
        }
    }
    logonly("stor98: INF\\%s, %lu bytes, SHA-256 %s: %s\n", name,
            (unsigned long)n, hex[0] ? hex : "(unreadable)", why);
}

/* 1 when a copy's name is the one an InfPath names (InfPath holds the file
 * name; the copy may be in INF\OTHER). On 98 SE and ME the setup engine's
 * INF\OTHER copy has a long name ("Yeo Kheng Mengstor98.inf", the one the
 * listing holds) and the driver key's InfPath its 8.3 alias (YEOKHE~2.INF,
 * the 42-V.1 leg of 2026-10-19), so the alias is compared too. 1 named,
 * 0 not named, -1 unknown: the alias cannot be read, or the bare name (the
 * copy's own or its alias) matches while the other directory (INF against
 * INF\OTHER) holds a file of that name too, so the InfPath could mean
 * either. A caller never records a copy on
 * -1, and the uninstall keeps it. */
static int s98_inf_names(const char *infPath, const char *name)
{
    const char *base = strrchr(name, '\\');
    char win[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char alias[INST_PATH_CHARS];
    const char *abase;
    DWORD n;
    DWORD e;
    int other;
    int qualified;
    int s;

    base = base != NULL ? base + 1 : name;
    if (infPath == NULL || infPath[0] == '\0') {
        return 0;
    }
    if (envNow.Nt) {
        return inst_eq(infPath, base) || inst_eq(infPath, name);
    }
    other = inst_eqn(name, "OTHER\\", 6);
    qualified = strchr(infPath, '\\') != NULL;
    /* A directory-qualified InfPath ("OTHER\X.INF") names one file. */
    if (qualified && inst_eq(infPath, name)) {
        return 1;
    }
    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return -1;
    }
    if (qualified || !inst_eq(infPath, base)) {
        join(path, sizeof(path), win, "INF");
        join(path, sizeof(path), path, name);
        n = GetShortPathNameA(path, alias, sizeof(alias));
        if (n == 0) {
            e = GetLastError();
            /* A copy that is not there has no alias for a driver key to
             * name. */
            return (e == ERROR_FILE_NOT_FOUND ||
                    e == ERROR_PATH_NOT_FOUND) ? 0 : -1;
        }
        if (n >= sizeof(alias)) {
            return -1;
        }
        abase = strrchr(alias, '\\');
        abase = abase != NULL ? abase + 1 : alias;
        if (qualified) {
            /* The alias with the copy's own directory kept. */
            inst_copy(path, other ? "OTHER\\" : "", sizeof(path));
            strncat(path, abase, sizeof(path) - strlen(path) - 1);
            return inst_eq(infPath, path);
        }
        if (!inst_eq(infPath, abase)) {
            return 0;
        }
    }
    /* 98 SE and ME: a bare name, the copy's own or its alias, is told
     * apart only when the other directory holds no file of that name. */
    join(path, sizeof(path), win, other ? "INF" : "INF\\OTHER");
    join(path, sizeof(path), path, infPath);
    s = file_state(path);
    return s == 0 ? 1 : -1;
}

/* 1 when, after a settle, a device of stor98.inf recorded as this
 * installer's names the copy. */
static int s98_inf_named_by_recorded(const char *name)
{
    int i;
    int j;

    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (!d->Present || d->Inf != INST_INF_S98 || d->InfPathState <= 0 ||
            s98_inf_names(d->InfPath, name) <= 0) {
            continue;
        }
        for (j = 0; j < s98NewCount; j++) {
            if (inst_eq(s98NewId[j], d->Id)) {
                return 1;
            }
        }
    }
    return 0;
}

/* NT: the names in %windir%\inf before SetupCopyOEMInf, so a name it
 * returns can be told new from one it found there. */
static void s98_oem_before(void)
{
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    size_t n = 0;
    size_t len;

    free(s98OemNames);
    s98OemNames = NULL;
    s98OemOk = 0;
    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return;
    }
    join(pattern, sizeof(pattern), win, "inf\\oem*.inf");
    s98OemNames = (char *)malloc(65536);
    if (s98OemNames == NULL) {
        return;
    }
    s98OemNames[0] = '\0';
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        s98OemOk = GetLastError() == ERROR_FILE_NOT_FOUND;
        return;
    }
    do {
        len = strlen(fd.cFileName);
        if (n + len + 2 >= 65536) {
            FindClose(h);
            return;
        }
        memcpy(s98OemNames + n, fd.cFileName, len);
        n += len;
        s98OemNames[n++] = '\n';
        s98OemNames[n] = '\0';
    } while (FindNextFileA(h, &fd));
    s98OemOk = GetLastError() == ERROR_NO_MORE_FILES;
    FindClose(h);
}

/* 1 when the name was there before the call, or that cannot be known (it
 * is then never recorded as this installer's). */
static int s98_oem_was(const char *name)
{
    const char *p;
    size_t len = strlen(name);

    if (!s98OemOk || s98OemNames == NULL) {
        return 1;
    }
    for (p = s98OemNames; *p != '\0';) {
        const char *e = strchr(p, '\n');
        if (e == NULL) {
            break;
        }
        if ((size_t)(e - p) == len && inst_eqn(p, name, len)) {
            return 1;
        }
        p = e + 1;
    }
    return 0;
}

/* After a step - a helper install or update (infPath: that device's
 * driver key's InfPath afterwards), a settle (settle 1), or a step with no
 * fresh reading of the devices (settle -1: nothing attributed) - each copy
 * that
 * was not in the listing before is recorded under Staged when the device
 * of the step names it (after a settle: a device newly recorded under
 * Devices); another is left and logged. The after-listing becomes the
 * next step's before. */
static void s98_inf_after(const char *infPath, int settle)
{
    int i;

    if (envNow.Nt || !s98Include || !s98Baseline) {
        return;
    }
    s98_inf_take(1, 0);
    if (!s98InfOk[0] || !s98InfOk[1]) {
        logonly("stor98: an INF listing is not whole: no copy is recorded "
                "for this step\n");
    } else {
        for (i = 0; i < s98InfCount[1]; i++) {
            const char *name = s98Inf[1][i].Name;
            if (s98_inf_in(0, name)) {
                continue;
            }
            if (settle > 0 ? s98_inf_named_by_recorded(name) :
                settle == 0 && s98_inf_names(infPath, name) > 0) {
                s98_inf_log(name, "made in this step and named by the "
                            "device: recorded");
                s98_rec_text("Staged", name, s98VersionText);
            } else if (s98_stray_ours(name)) {
                s98_inf_log(name, "appeared in this step, named by no "
                            "device of this step, Stor98's and no Stor98 "
                            "before this installer: recorded");
                s98_rec_text("Staged", name, s98VersionText);
            } else {
                s98_inf_log(name, "appeared in this step, named by no "
                            "device of this step: left");
            }
        }
    }
    if (s98Inf[0] != NULL && s98Inf[1] != NULL) {
        memcpy(s98Inf[0], s98Inf[1], sizeof(S98INF) * (size_t)s98InfCount[1]);
    }
    s98InfCount[0] = s98InfCount[1];
    s98InfOk[0] = s98InfOk[1];
}

static int s98_file_names_stor98(const char *path);

/* A copy no Stor98 step made (a wizard of Windows' own racing the helper):
 * the package's when its text names Stor98 and no Stor98 was there before
 * this installer (inst_s98_stray_inf_ours). */
static int s98_stray_ours(const char *name)
{
    char win[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    join(path, sizeof(path), win, "INF");
    join(path, sizeof(path), path, name);
    return inst_s98_stray_inf_ours(s98Pre, s98_file_names_stor98(path));
}

/* Before a Stor98 step: the listing taken afresh, a copy that appeared
 * since the last one (another device's install, Windows' own wizard)
 * logged and left - it is not this step's. */
static void s98_inf_sync(void)
{
    int i;

    if (envNow.Nt || !s98Include || !s98Baseline || s98Inf[0] == NULL) {
        return;
    }
    s98_inf_take(1, 0);
    for (i = 0; s98InfOk[0] && s98InfOk[1] && i < s98InfCount[1]; i++) {
        if (s98_inf_in(0, s98Inf[1][i].Name)) {
            continue;
        }
        if (s98_stray_ours(s98Inf[1][i].Name)) {
            s98_inf_log(s98Inf[1][i].Name, "appeared outside a Stor98 step, "
                        "Stor98's and no Stor98 before this installer: "
                        "recorded");
            s98_rec_text("Staged", s98Inf[1][i].Name, s98VersionText);
        } else {
            s98_inf_log(s98Inf[1][i].Name, "appeared outside a Stor98 step: "
                        "left");
        }
    }
    if (s98Inf[1] != NULL) {
        memcpy(s98Inf[0], s98Inf[1], sizeof(S98INF) * (size_t)s98InfCount[1]);
    }
    s98InfCount[0] = s98InfCount[1];
    s98InfOk[0] = s98InfOk[1];
}

/* 98 SE and ME: every Enum\PCI entry stor98.inf's models match, present
 * or not, with what runs it (its driver key's PortDriver, DevLoader or
 * NTMPDriver). 0 when an entry cannot be read, or there are too many. */
static int s98_base_9x(void)
{
    HKEY pci;
    HKEY dev;
    char devName[INST_ID_CHARS];
    char instName[INST_ID_CHARS];
    char path[INST_PATH_CHARS];
    char text[64];
    DEVREC t;
    DWORD i;
    DWORD j;
    DWORD n;
    LONG r;
    int ok = 1;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum\\PCI", 0, KEY_READ, &pci);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 1;
    }
    if (r != ERROR_SUCCESS) {
        return 0;
    }
    for (i = 0; ok; i++) {
        n = sizeof(devName);
        r = RegEnumKeyExA(pci, i, devName, &n, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS ||
            RegOpenKeyExA(pci, devName, 0, KEY_READ, &dev) != ERROR_SUCCESS) {
            ok = 0;
            break;
        }
        for (j = 0; ok; j++) {
            n = sizeof(instName);
            r = RegEnumKeyExA(dev, j, instName, &n, NULL, NULL, NULL, NULL);
            if (r == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (r != ERROR_SUCCESS) {
                ok = 0;
                break;
            }
            memset(&t, 0, sizeof(t));
            _snprintf(t.Id, sizeof(t.Id) - 1, "PCI\\%s\\%s", devName,
                      instName);
            if (reg9x_value(t.Id, "HardwareID", t.Hw, sizeof(t.Hw)) < 0 ||
                reg9x_value(t.Id, "CompatibleIDs", t.Compat,
                            sizeof(t.Compat)) < 0 ||
                reg9x_value(t.Id, "Driver", t.Driver, sizeof(t.Driver)) < 0) {
                ok = 0;
                break;
            }
            t.HwBytes = (DWORD)inst_ids_split(t.Hw, sizeof(t.Hw));
            t.CompatBytes = (DWORD)inst_ids_split(t.Compat, sizeof(t.Compat));
            if (inst_ids_match(t.Hw, t.HwBytes, t.Compat, t.CompatBytes,
                               &infIds[INST_INF_S98]) < 0) {
                continue;
            }
            if (s98BaseCount >= INST_MAX_DEVS) {
                ok = 0;
                break;
            }
            inst_copy(s98Base[s98BaseCount].Id, t.Id, INST_ID_CHARS);
            s98Base[s98BaseCount].State = 0;
            if (t.Driver[0] != '\0') {
                int g;
                driver_key_path(&t, path, sizeof(path));
                g = reg_text3(HKEY_LOCAL_MACHINE, path, "PortDriver", text,
                              sizeof(text));
                if (g < 0) {
                    ok = 0;
                    break;
                }
                if (g > 0) {
                    s98Base[s98BaseCount].State =
                        inst_eq(text, "stor98.mpd") ? 2 : 1;
                } else if (reg_text3(HKEY_LOCAL_MACHINE, path, "DevLoader",
                                     text, sizeof(text)) != 0 ||
                           reg_text3(HKEY_LOCAL_MACHINE, path, "NTMPDriver",
                                     text, sizeof(text)) != 0) {
                    s98Base[s98BaseCount].State = 1;
                }
            }
            s98BaseCount++;
        }
        RegCloseKey(dev);
    }
    RegCloseKey(pci);
    return ok;
}

/* A devnode's driver key read again (98 SE and ME), after a helper job:
 * its InfPath, and whether it names Stor98's port driver. */
static int s98_reread_9x(const char *id, char *infPath, size_t cap)
{
    DEVREC t;
    char text[64];
    char path[INST_PATH_CHARS];

    infPath[0] = '\0';
    memset(&t, 0, sizeof(t));
    inst_copy(t.Id, id, sizeof(t.Id));
    if (reg9x_value(t.Id, "Driver", t.Driver, sizeof(t.Driver)) <= 0) {
        return 0;
    }
    driver_key_path(&t, path, sizeof(path));
    reg_text3(HKEY_LOCAL_MACHINE, path, "InfPath", infPath, (DWORD)cap);
    return reg_text3(HKEY_LOCAL_MACHINE, path, "PortDriver", text,
                     sizeof(text)) > 0 && inst_eq(text, "stor98.mpd");
}

/* ---- the record's devices (record 26 section 6.3, "The record") --------- */

static int s98_record_device(const DEVREC *d, const char *how);

/* Each drive whose stornvme this installer replaced on 8.1 (Pending,
 * record 26 section 5.3.1 case 3b), resolved from the reading: running
 * Stor98, it is recorded as this installer's; running another driver, or
 * with no entry left, it is forgotten; not present, it stays pending, but
 * for the uninstall, which takes one whose entry names Stor98. */
/* A drive's pending value (case 3b): state written ("attempt" only for a
 * drive Windows' stornvme runs; "done" only over an "attempt"), or deleted
 * (NULL). */
static int s98_pending_mark(const char *id, const char *state)
{
    char path[INST_PATH_CHARS];
    char now[16];
    int g;

    if (opts.List || !envNow.Nt) {
        return 1;
    }
    if (state != NULL && inst_eq(state, "attempt")) {
        return s98_rec_text("Pending", id, state);
    }
    join(path, sizeof(path), INST_S98_KEY, "Pending");
    g = reg_text3(HKEY_LOCAL_MACHINE, path, id, now, sizeof(now));
    if (g <= 0) {
        return g == 0;
    }
    /* Only this call's own attempt changes: an unknown outcome kept from
     * an earlier run stays the exclusion it is. */
    if (!inst_eq(now, "attempt")) {
        return 1;
    }
    if (state == NULL) {
        return s98_rec_del("Pending", id);
    }
    return s98_rec_text("Pending", id, state);
}

/* 1 resolved as far as the reading allows; 0 the record could not be read
 * or written (the evidence is kept, said). */
static int s98_pending_resolve(int uninstall)
{
    char id[INST_ID_CHARS];
    char state[16];
    char path[INST_PATH_CHARS];
    DWORD i = 0;
    int j;
    int e;
    int g;
    const DEVREC *d;

    if (!envNow.Nt || opts.List) {
        return 1;
    }
    join(path, sizeof(path), INST_S98_KEY, "Pending");
    for (;;) {
        e = s98_rec_enum("Pending", i, id, sizeof(id));
        if (e < 0) {
            out("Stor98's record of a replaced Windows NVMe driver cannot be "
                "read.\n");
            return 0;
        }
        if (e == 0) {
            return 1;
        }
        d = NULL;
        for (j = 0; j < devCount; j++) {
            if (inst_eq(devs[j].Id, id)) {
                d = &devs[j];
                break;
            }
        }
        g = reg_text3(HKEY_LOCAL_MACHINE, path, id, state, sizeof(state));
        if (g < 0) {
            /* Evidence that cannot be read is kept, and nothing else is
             * resolved in this run. */
            out("Stor98's record of %s cannot be read: it is kept.\n", id);
            return 0;
        }
        if (g > 0 && inst_eq(state, "done")) {
            if (d != NULL && inst_eq(d->Service, "stor98") &&
                (d->Present || uninstall)) {
                if (!s98_record_device(d, "its Windows NVMe driver was "
                                       "replaced by this installer")) {
                    return 0;
                }
                continue;
            }
            if (d != NULL && !d->Present) {
                i++;        /* still to be read back */
                continue;
            }
        } else if (d != NULL && inst_eq(d->Service, "stor98")) {
            /* An attempt whose outcome is unknown, now running Stor98: never
             * this installer's, and kept, so no later adoption takes it. */
            if (!(g > 0 && inst_eq(state, "unknown")) &&
                !s98_rec_text("Pending", id, "unknown")) {
                return 0;
            }
            logonly("stor98: %s runs Stor98 after a replacement of unknown "
                    "outcome: not this installer's\n", id);
            i++;
            continue;
        } else if (d != NULL && !d->Present) {
            i++;
            continue;
        }
        logonly("stor98: pending %s forgotten (%s)\n", id,
                d == NULL ? "no entry" : "another driver");
        if (!s98_rec_del("Pending", id)) {
            return 0;
        }
    }
}

static int s98_rec_del(const char *sub, const char *name);

/* 1 recorded (or already), 0 a value could not be written or deleted. */
static int s98_record_device(const DEVREC *d, const char *how)
{
    if (s98_rec_has("Devices", d->Id) != 1) {
        out("Stor98 now runs %s: recorded as this installer's (%s).\n",
            d->Desc[0] ? d->Desc : d->Id, how);
        if (!s98_rec_text("Devices", d->Id, s98VersionText)) {
            return 0;
        }
        if (s98NewCount < INST_S98_MAX_DEV) {
            /* This step's: a copy made in it may be named by it. */
            inst_copy(s98NewId[s98NewCount++], d->Id, INST_ID_CHARS);
        }
    }
    /* The pending evidence goes only once the device is recorded. */
    if (!opts.List && s98_rec_has("Pending", d->Id) == 1 &&
        !s98_rec_del("Pending", d->Id)) {
        return 0;
    }
    return 1;
}

/* After the installs and every settle: each present device of stor98.inf
 * that Stor98 runs now and did not at the baseline, recorded by one of
 * the two rules; a device Stor98 ran at the baseline, or that an update
 * changed, is never added. */
/* At the end of an install run: a place that is still unknown leaves work
 * (exit 4); one read later in the run does not. */
static void s98_final_unknown(void)
{
    int i;

    for (i = 0; i < devCount; i++) {
        if (devs[i].Present && devs[i].Inf == INST_INF_S98 &&
            devs[i].S98Why == INST_S98_SKIP_UNKNOWN) {
            flagSteps = 1;
        }
    }
}

static void s98_record_devices(void)
{
    int i;
    int c;
    int before;
    int after;
    int staged;

    s98NewCount = 0;
    if (!s98Include || !s98Baseline || opts.List) {
        return;
    }
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (!d->Present || d->Inf != INST_INF_S98) {
            continue;
        }
        before = s98_base_state(d->Id);
        after = s98_state_of(d) == 2;
        if (!after || before == 2) {
            continue;
        }
        c = reached(d->Id);
        if (c >= 0 && changedState[c] == 2) {
            if (inst_s98_owned_by_call(before, after)) {
                s98_record_device(d, "this run installed it");
            }
            continue;
        }
        staged = envNow.Nt && d->InfPathState > 0 &&
                 s98_rec_has("Staged", d->InfPath) == 1;
        if (inst_s98_owned_by_settle(before, after, envNow.Nt, staged,
                                     s98Pre)) {
            s98_record_device(d, "Windows bound it from the staged INF");
        } else {
            logonly("stor98: %s newly bound, not this installer's by the "
                    "record's rules: not recorded\n", d->Id);
        }
    }
}

/* The record's PreExisting, written before the run's first Stor98 change
 * (the copy that carries Stor98, its staging, an install or the file
 * update): 0 when it cannot be written, said. */
static int s98_pre_commit(void)
{
    DWORD one;

    if (!s98PreOwed || opts.List) {
        return 1;
    }
    one = (DWORD)s98Pre;
    if (!s98_rec_put(NULL, "PreExisting", REG_DWORD, &one, sizeof(one))) {
        return 0;
    }
    s98PreOwed = 0;
    return 1;
}

/* The bundled release this installer last installed: written only by a
 * run that installed or updated it (record 26 section 6.3, draft 16) -
 * a device the call or the helper bound or updated, or the file update. */
static void s98_record_version(void)
{
    int c;
    int changed = s98FileDone;

    for (c = 0; c < changedCount && !changed; c++) {
        changed = changedInf[c] == INST_INF_S98 && changedState[c] == 2;
    }
    if (s98Include && s98Baseline && !opts.List && changed) {
        s98_rec_text(NULL, "Version", s98VersionText);
    }
}

/* ---- the file update's confirmation (record 26 section 6.3, draft 16) --- */

/* The console's question before the 98 SE and ME file update (owner,
 * 2026-10-19): one yes/no, Enter for N; N leaves Stor98 for a later
 * run. The window's Summary box answers it there. */
static void s98_ask_unused(void)
{
    char text[1024];
    char q[1300];
    char line[16];
    int k;
    int r = 0;

    if (instWindow || opts.List || !s98FileUpd) {
        return;
    }
    if (opts.Yes) {
        out("/yes: Stor98's driver file is not updated (\"%s\" is not "
            "confirmed unasked;\nrun the installer again without /yes to "
            "update it).\n\n", instS98DisconnectText);
        s98FileOk = 0;
        return;
    }
    _snprintf(text, sizeof(text) - 1,
              "Stor98 %s replaces the installed stor98.mpd when Windows next "
              "starts. %s.", s98VersionText, instS98DisconnectText);
    text[sizeof(text) - 1] = '\0';
    inst_yesno_question(q, sizeof(q), text,
                        "they are disconnected: update Stor98",
                        "leave Stor98 for a later run", 0);
    for (k = 0;; k++) {
        if (!ask(INST_Q_S98_UNUSED, k == 0 ? q :
                 "  Type Y or N, or Enter for [N]: ", k > 0, 0, 0,
                 NULL, line, sizeof(line))) {
            r = 0;
            out("\n");
            break;
        }
        r = inst_tb_answer(line, 0);
        if (r >= 0) {
            break;
        }
    }
    s98FileOk = r == 1;
    logonly("stor98: file update confirmed %d\n", s98FileOk);
}

/* 1 when a devnode the scan read carries the tunnel-port filter. */
static int s98_filtered(const char *id)
{
    int i;

    for (i = 0; i < devCount; i++) {
        if (inst_eq(devs[i].Id, id) &&
            inst_msz_has(devs[i].Upper, devs[i].UpperBytes, "xhcicm")) {
            return 1;
        }
    }
    return 0;
}

static const char *s98_place_text(int place)
{
    return place == INST_S98_AT_TUNNEL ? "behind a tunnel" :
           place == INST_S98_AT_INTERNAL ? "internal" : "unknown";
}

/* The connection manager's confirmed reading (record 24 sections 3.3 and
 * 4, the walk /devices and /eject take), and from it the place of every
 * present device stor98.inf matches or Stor98 runs. Anything not read is
 * unknown. */
static void s98_read_places(void)
{
    int i;
    int j;
    int k;
    int p;
    int n;
    int ok;
    int cmAll;
    DWORD w;

    for (i = 0; i < devCount; i++) {
        devs[i].S98Place = INST_S98_AT_UNKNOWN;
    }
    if (!tbnFull) {
        if (tbn != NULL) {
            return;     /* the self-test's few nodes: no reading */
        }
        tbn = (TBNODE *)calloc(INST_MAX_DEVS, sizeof(TBNODE));
        if (tbn == NULL) {
            logonly("stor98: no memory for the tunnel reading\n");
            return;
        }
        tbnFull = 1;
    }
    if (tbMutex == NULL) {
        tbMutex = shared_mutex(envNow.Nt ? "Global\\XhciinstTbAsk"
                                         : "XhciinstTbAsk");
    }
    if (tbMutex == NULL) {
        logonly("stor98: the tunnel reading's lock could not be opened "
                "(error %lu)\n", (unsigned long)GetLastError());
        return;
    }
    w = WaitForSingleObject(tbMutex, 10000);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        logonly("stor98: the tunnel reading's lock is held elsewhere\n");
        return;
    }
    ok = tb_read_all();
    tb_unask();
    ReleaseMutex(tbMutex);
    if (!ok) {
        logonly("stor98: the tunnel reading failed\n");
        return;
    }
    cmAll = tbhCount > 0;
    for (k = 0; k < tbhCount; k++) {
        if (!tbh[k].Confirmed) {
            cmAll = 0;
        }
    }
    logonly("stor98: tunnel reading: %d devnodes, %d not read whole, "
            "changing %d, %d connection managers, all confirmed %d\n",
            tbnCount, tbnBad, tbChanging, tbhCount, cmAll);
    for (i = 0; i < devCount; i++) {
        DEVREC *d = &devs[i];
        int found = -1;
        int verified = 0;
        int port = 0;
        if (!d->Present ||
            (d->Inf != INST_INF_S98 && !inst_eq(d->Service, "stor98"))) {
            continue;
        }
        for (j = 0; j < tbnCount; j++) {
            if (inst_eq(tbn[j].R.Id, d->Id)) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            for (p = tbn[found].Parent, n = 0; p >= 0 && n < 64;
                 p = tbn[p].Parent, n++) {
                if (tbn[p].Verified) {
                    verified = 1;
                } else if (tbn[p].Candidate || s98_filtered(tbn[p].R.Id)) {
                    port = 1;
                }
            }
        }
        d->S98Place = inst_s98_place(found >= 0, tbChanging, tbnBad == 0,
                                     cmAll, verified, port);
        logonly("stor98: %s is %s (found %d, below a verified port %d, "
                "below an unverified port %d)\n", d->Id,
                s98_place_text(d->S98Place), found >= 0, verified, port);
    }
}

/* 98 SE and ME: the Enum\PCI entry whose Driver is drv. 1 found, 0 none,
 * -1 unreadable. */
static int s98_enum_id_9x(const char *drv, char *id, size_t cap)
{
    HKEY pci;
    HKEY dev;
    char devName[INST_ID_CHARS];
    char instName[INST_ID_CHARS];
    char one[INST_ID_CHARS];
    char text[128];
    DWORD i;
    DWORD j;
    DWORD n;
    int found = 0;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum\\PCI", 0, KEY_READ, &pci) !=
        ERROR_SUCCESS) {
        return -1;
    }
    for (i = 0; !found; i++) {
        n = sizeof(devName);
        if (RegEnumKeyExA(pci, i, devName, &n, NULL, NULL, NULL, NULL) !=
            ERROR_SUCCESS) {
            break;
        }
        if (RegOpenKeyExA(pci, devName, 0, KEY_READ, &dev) != ERROR_SUCCESS) {
            found = -1;
            break;
        }
        for (j = 0; !found; j++) {
            n = sizeof(instName);
            if (RegEnumKeyExA(dev, j, instName, &n, NULL, NULL, NULL, NULL) !=
                ERROR_SUCCESS) {
                break;
            }
            join(one, sizeof(one), "PCI", devName);
            join(one, sizeof(one), one, instName);
            if (reg9x_value(one, "Driver", text, sizeof(text)) > 0 &&
                inst_eq(text, drv)) {
                inst_copy(id, one, cap);
                found = 1;
            }
        }
        RegCloseKey(dev);
    }
    RegCloseKey(pci);
    return found;
}

/* 98 SE and ME: the scan holds present devnodes alone, so every driver key
 * naming Stor98's port driver is read too: 1 when one is not a present
 * tunnel drive's and not an entry this installer recorded, or cannot be
 * read. */
static int s98_other_9x(void)
{
    HKEY cls;
    char sub[16];
    char drv[32];
    char path[INST_PATH_CHARS];
    char text[64];
    DWORD i;
    DWORD n;
    LONG r;
    int g;
    int j;
    int known;
    int present;
    int other = 0;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_S98_CLASS_9X, 0, KEY_READ,
                      &cls);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return 1;
    }
    for (i = 0; !other; i++) {
        n = sizeof(sub);
        r = RegEnumKeyExA(cls, i, sub, &n, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS) {
            other = 1;
            break;
        }
        join(path, sizeof(path), INST_S98_CLASS_9X, sub);
        g = reg_text3(HKEY_LOCAL_MACHINE, path, "PortDriver", text,
                      sizeof(text));
        if (g < 0) {
            other = 1;
            break;
        }
        if (g == 0 || !inst_eq(text, "stor98.mpd")) {
            continue;
        }
        join(drv, sizeof(drv), "SCSIAdapter", sub);
        known = 0;
        present = 0;
        for (j = 0; j < devCount; j++) {
            if (inst_eq(devs[j].Driver, drv) && devs[j].Present &&
                !devs[j].RegOnly) {
                present = 1;
                if (inst_s98_known_tunnel(0, 1, devs[j].S98Place, 0)) {
                    known = 1;
                }
            }
        }
        if (!present) {
            /* An unplugged drive is in no reading: on 98 SE and ME an
             * internal drive is always present, so a key with no present
             * devnode is a drive gone with its tunnel (record 23 section
             * 4.4, revision 23; the uninstall's adoption reads it so). */
            known = inst_s98_known_tunnel(0, 0, INST_S98_AT_UNKNOWN, 0);
        }
        if (!known) {
            logonly("stor98: the driver key SCSIAdapter\\%s is Stor98's and "
                    "no present tunnel drive's\n", sub);
            other = 1;
        }
    }
    RegCloseKey(cls);
    return other;
}

/* The reasons said once each after the plan was printed. */
#define INST_S98_SAID 64
static char s98SaidId[INST_S98_SAID][INST_ID_CHARS];
static int s98SaidWhy[INST_S98_SAID];
static int s98SaidCount;
static int s98ShareSaid = -1;

static int s98_said(const char *id, int why)
{
    int i;

    for (i = 0; i < s98SaidCount; i++) {
        if (inst_eq(s98SaidId[i], id)) {
            if (s98SaidWhy[i] == why) {
                return 1;
            }
            s98SaidWhy[i] = why;
            return 0;
        }
    }
    if (s98SaidCount < INST_S98_SAID) {
        inst_copy(s98SaidId[s98SaidCount], id, INST_ID_CHARS);
        s98SaidWhy[s98SaidCount++] = why;
    }
    return 0;
}

/* The shared file's decision, said on screen and in the log. */
static void s98_share_note(int first)
{
    if (!s98Include || (s98Share == s98ShareSaid && !first)) {
        return;
    }
    s98ShareSaid = s98Share;
    if (s98Share == INST_S98_SHARE_SAME) {
        out("  Stor98's settings are not written in this run: a Stor98 the "
            "installer did not put on a tunnel drive (an internal one, one "
            "whose place could not be read, or one not present) reads them "
            "too. Its file is already the bundled release.\n");
    } else if (s98Share == INST_S98_SHARE_HOLD) {
        out("  Stor98 is LEFT as it is in this run: a Stor98 the installer "
            "did not put on a tunnel drive (an internal one, one whose place "
            "could not be read, or one not present) runs another release, "
            "and installing or updating a tunnel drive would replace the "
            "file and settings it shares. Update Stor98 by hand if you want "
            "the bundled %s (its readme, \"Updating the driver\").\n",
            s98VersionText);
    }
}

/* Stor98's gate (owner, 2026-10-19), after every plan: each device the
 * Stor98 INF matches runs only behind a tunnel, with the Thunderbolt
 * component in the run, and only when no other Stor98 shares its file at
 * another release; whatever the plan decided (install, update, repair,
 * /force, /restage). The places are read once per scan. */
static void s98_places(void)
{
    int i;
    int any = 0;
    int other = 0;
    int r;
    INST_U32 v[4];
    char path[INST_PATH_CHARS];

    s98Share = INST_S98_SHARE_FREE;
    s98FileUpd = 0;
    s98InfRefresh = 0;
    for (i = 0; i < devCount; i++) {
        devs[i].S98Why = INST_S98_ELIGIBLE;
        if (devs[i].Present && (devs[i].Inf == INST_INF_S98 ||
                                inst_eq(devs[i].Service, "stor98"))) {
            any = 1;
        }
    }
    if (!s98Include) {
        return;
    }
    if (any && !s98TopoFresh) {
        s98TopoFresh = 1;
        s98_read_places();
    }
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (!inst_eq(d->Service, "stor98")) {
            continue;
        }
        if (inst_s98_known_tunnel(envNow.Nt, d->Present, d->S98Place,
                                  !d->Present &&
                                  s98_rec_has("Devices", d->Id) == 1)) {
            continue;
        }
        other = 1;
        logonly("stor98: %s runs Stor98 and is not a known tunnel drive "
                "(%s)\n", d->Id, d->Present ?
                s98_place_text(d->S98Place) : "not present, not recorded");
    }
    if (!envNow.Nt && !other && s98_other_9x()) {
        other = 1;
    }
    group_binary(INST_GROUP_S98, path, sizeof(path));
    r = file_version(path, v);
    s98Share = inst_s98_share(other, r > 0 && inst_ver_cmp(v, s98Version) == 0);
    if (s98HoldRun) {
        s98Share = INST_S98_SHARE_HOLD;
    }
    s98FileUpd = inst_s98_file_update(envNow.Nt, s98Include, s98Share,
                                      r > 0 ? 1 : r < 0 ? -1 : 0,
                                      r > 0 ? inst_ver_cmp(v, s98Version) : 0);
    s98InfRefresh = !envNow.Nt && s98Include &&
                    s98Share == INST_S98_SHARE_FREE && r > 0 &&
                    inst_ver_cmp(v, s98Version) <= 0;
    logonly("stor98: other bindings %d, the file %s (%d), share %d, file "
            "update %d\n", other, path, r, s98Share, s98FileUpd);
    for (i = 0; i < devCount; i++) {
        DEVREC *d = &devs[i];
        int why;
        if (!d->Present || d->Inf != INST_INF_S98) {
            continue;
        }
        why = inst_s98_eligible(tbInclude, d->S98Place);
        if (why == INST_S98_ELIGIBLE && s98Share == INST_S98_SHARE_HOLD) {
            why = INST_S98_SKIP_SHARED;
        }
        d->S98Why = why;
        if (why == INST_S98_ELIGIBLE && !envNow.Nt &&
            inst_eq(d->Service, "stor98")) {
            /* 98 SE and ME: a device Stor98 runs is never reinstalled
             * (the fatal exception 0E of 2026-10-19); its file is
             * replaced at the next start when it is older. */
            if (d->Runs) {
                logonly("stor98: %s (%s) not reinstalled: %s\n", d->Id,
                        inst_action_text(d->Action), s98FileUpd ?
                        "the file update covers it" : "Stor98 runs it");
            }
            d->Runs = 0;
            continue;
        }
        if (why == INST_S98_ELIGIBLE) {
            continue;
        }
        if (d->Runs) {
            logonly("stor98: %s (%s) left: %s\n", d->Id,
                    inst_action_text(d->Action), inst_s98_skip_text(why, 0));
        }
        d->Runs = 0;
        if (s98Shown && !s98_said(d->Id, why)) {
            out("Stor98: %s (%s) is left: %s.\n",
                d->Desc[0] ? d->Desc : "an NVMe drive", d->Id,
                inst_s98_skip_text(why, 0));
        } else if (!s98Shown) {
            (void)s98_said(d->Id, why);
        }
    }
    if (s98Shown) {
        s98_share_note(0);
    }
}

/* 1 when the device sits behind a tunnel port this package's connection
 * manager verified, by the latest reading: its driver change tears the
 * tunnel down (record 24 section 5.2). */
static int s98_behind_tunnel(const char *id)
{
    int i;

    for (i = 0; i < devCount; i++) {
        if (inst_eq(devs[i].Id, id) && devs[i].Present) {
            return devs[i].S98Place == INST_S98_AT_TUNNEL;
        }
    }
    return 0;
}

/* NT, before the Stor98 pass: each Stor98 device the run updates whose
 * drive sits behind a port this package's connection manager filters -
 * whether or not its box is ticked this run - read while it is present,
 * so its update awaits the reconnection whatever happens after the call. */
static void s98_tunnel_mark(void)
{
    int i;
    int j;
    int known;

    if (!envNow.Nt || !s98Include) {
        return;
    }
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        if (!d->Present || d->Inf != INST_INF_S98 || !d->Runs || d->Refused ||
            d->Done || !inst_eq(d->Service, "stor98") ||
            !s98_behind_tunnel(d->Id)) {
            continue;
        }
        known = 0;
        for (j = 0; j < s98TunCount; j++) {
            if (inst_eq(s98TunId[j], d->Id)) {
                known = 1;
            }
        }
        if (known) {
            continue;
        }
        if (s98TunCount >= INST_S98_MAX_DEV) {
            continue;   /* as many as a reading holds: not reached */
        }
        inst_copy(s98TunId[s98TunCount++], d->Id, INST_ID_CHARS);
        logonly("stor98: %s is behind a tunnel port\n", d->Id);
    }
}

static int s98_tunnelled(const char *id)
{
    int i;

    for (i = 0; i < s98TunCount; i++) {
        if (inst_eq(s98TunId[i], id)) {
            return 1;
        }
    }
    return 0;
}

/* 1 when the device is present now (located with no phantom flag). */
static int s98_present(const char *id)
{
    DWORD dn;
    char want[INST_ID_CHARS];

    inst_copy(want, id, sizeof(want));
    return api.Locate != NULL && api.Locate(&dn, want, 0) == INST_CR_SUCCESS;
}

static int s98_awaiting(const char *id)
{
    int i;

    for (i = 0; i < s98AwaitCount; i++) {
        if (inst_eq(s98AwaitId[i], id)) {
            return 1;
        }
    }
    return 0;
}

/* NT, after the Stor98 pass: each successful update of a drive behind
 * such a port is "updated, awaiting reconnection" whatever the first
 * read-back shows. The run asks for the unplug and the plug-in and waits,
 * bounded at two minutes and skippable, until the device was seen gone and
 * present again; one not seen so stays awaiting (a step, exit 4), and the
 * next run verifies it (record 26 section 6.3). */
static void s98_reconnect_nt(void)
{
    int c;
    int gone;
    int back;
    DWORD t0;
    char what[256];

    if (!envNow.Nt || !s98Include || opts.List) {
        return;
    }
    for (c = 0; c < changedCount; c++) {
        if (changedInf[c] != INST_INF_S98 || changedState[c] != 2 ||
            (s98_base_state(changedId[c]) != 2 &&
             s98_rec_has("Pending", changedId[c]) != 1) ||
            !inst_s98_await_reconnect(envNow.Nt, 1, 1,
                                      s98_tunnelled(changedId[c]))) {
            continue;
        }
        out("\nStor98 was updated on %s, behind a Thunderbolt or USB4 port.\n"
            "Unplug the drive now and plug it in again: the installer waits "
            "up to two\nminutes for it, then reads it again.\n", changedId[c]);
        _snprintf(what, sizeof(what) - 1, "Unplug the NVMe drive and plug it "
                  "in again (%s); Cancel skips this wait", changedId[c]);
        what[sizeof(what) - 1] = '\0';
        gone = 0;
        back = 0;
        t0 = GetTickCount();
        while (GetTickCount() - t0 < INST_S98_RECONNECT_MS) {
            int now = s98_present(changedId[c]);
            if (!now) {
                gone = 1;
            } else if (gone) {
                back = 1;
                break;
            }
            if (!waiting(INST_WAIT_RECONNECT,
                         (unsigned long)(GetTickCount() - t0),
                         INST_S98_RECONNECT_MS, what)) {
                out("Skipped.\n");
                break;
            }
            Sleep(1000);
        }
        if (back) {
            out("The drive was unplugged and is back: it is read again.\n");
        } else {
            out("Not seen unplugged and back: %s stays \"updated, awaiting "
                "reconnection\".\n", changedId[c]);
            if (s98AwaitCount < INST_S98_MAX_DEV) {
                inst_copy(s98AwaitId[s98AwaitCount++], changedId[c],
                          INST_ID_CHARS);
            }
            flagSteps = 1;
        }
    }
}

/* ---- Stor98's settings (record 26 section 5.6) --------------------------- */

/* Row i's value now: 1 set (in out), 0 not set, -1 unreadable. */
static int s98_read(int i, char *outText, size_t cap)
{
    char param[INST_S98_PARAM_CHARS];
    DWORD dw;
    DWORD t;
    DWORD n;
    HKEY k;
    LONG r;
    char buf[64];
    int g;

    outText[0] = '\0';
    if (!inst_s98_applies(i, envNow.Nt)) {
        return 0;
    }
    if (envNow.Nt) {
        g = s98_param_read(param, sizeof(param));
        if (g <= 0) {
            return g;
        }
        return inst_s98_param_get(param, instS98Values[i].KeyNt, outText,
                                  cap);
    }
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_S98_SVC_9X, 0, KEY_READ, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    n = sizeof(buf) - 1;
    r = RegQueryValueExA(k, instS98Values[i].Name9x, NULL, &t, (LPBYTE)buf,
                         &n);
    RegCloseKey(k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    if (t == REG_DWORD && n == sizeof(DWORD)) {
        memcpy(&dw, buf, sizeof(dw));
        _snprintf(outText, cap - 1, "%lu", (unsigned long)dw);
        outText[cap - 1] = '\0';
        return 1;
    }
    if (t == REG_SZ) {
        buf[n < sizeof(buf) ? n : sizeof(buf) - 1] = '\0';
        inst_copy(outText, buf, cap);
        return 1;
    }
    return -1;
}

/* NT: the DriverParameter string whole: 1 read, 0 absent (no key, or no
 * value), -1 unreadable (another type, longer than the buffer, a key that
 * does not open) - never taken for empty. */
static int s98_param_read(char *param, size_t cap)
{
    HKEY k;
    LONG r;
    DWORD t;
    DWORD n = (DWORD)cap - 1;

    param[0] = '\0';
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_S98_PARAM_NT, 0, KEY_READ, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    r = RegQueryValueExA(k, "DriverParameter", NULL, &t, (LPBYTE)param, &n);
    RegCloseKey(k);
    if (r == ERROR_FILE_NOT_FOUND) {
        param[0] = '\0';
        return 0;
    }
    if (r != ERROR_SUCCESS || t != REG_SZ) {
        param[0] = '\0';
        return -1;
    }
    param[n < cap ? n : cap - 1] = '\0';
    return 1;
}

/* The values chosen, written where Stor98 reads them (its rule, per
 * value), and each recorded by name for the uninstall. A row whose value
 * is the one set already writes nothing. */
static void s98_write_values(void)
{
    char now[64];
    char param[INST_S98_PARAM_CHARS];
    char next[INST_S98_PARAM_CHARS];
    const char *name;
    DWORD dw;
    HKEY k;
    LONG w;
    int i;
    int st;

    if (!s98Include || opts.List) {
        return;
    }
    if (s98Share != INST_S98_SHARE_FREE) {
        for (i = 0; i < INST_S98_COUNT; i++) {
            if (s98Set[i]) {
                out("Stor98's settings were not written: a Stor98 the "
                    "installer did not put on a tunnel drive reads them "
                    "too.\n");
                logonly("stor98: settings held, share %d\n", s98Share);
                break;
            }
        }
        return;
    }
    for (i = 0; i < INST_S98_COUNT; i++) {
        if (!s98Set[i] || !inst_s98_applies(i, envNow.Nt)) {
            continue;
        }
        name = envNow.Nt ? instS98Values[i].KeyNt : instS98Values[i].Name9x;
        st = s98_read(i, now, sizeof(now));
        if (st < 0) {
            out("Stor98's %s cannot be read, so it is not written (nothing of "
                "its settings\nis changed).\n", name);
            flagFailed = 1;
            continue;
        }
        if (st > 0 && inst_eq(now, s98Val[i])) {
            out("Stor98's %s stays %s.\n", name, now);
            continue;
        }
        if (envNow.Nt) {
            if (s98_param_read(param, sizeof(param)) < 0) {
                out("Stor98's DriverParameter cannot be read whole: %s is not "
                    "written.\n", name);
                flagFailed = 1;
                continue;
            }
            if (!inst_s98_param_set(param, instS98Values[i].KeyNt, s98Val[i],
                                    next, sizeof(next))) {
                out("Stor98's %s could not be written: DriverParameter would "
                    "be too long.\n", name);
                flagFailed = 1;
                continue;
            }
            w = RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_S98_PARAM_NT, 0,
                                NULL, 0, KEY_SET_VALUE, NULL, &k, NULL);
            if (w == ERROR_SUCCESS) {
                w = RegSetValueExA(k, "DriverParameter", 0, REG_SZ,
                                   (const BYTE *)next,
                                   (DWORD)strlen(next) + 1);
                RegCloseKey(k);
            }
        } else {
            w = RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_S98_SVC_9X, 0, NULL,
                                0, KEY_SET_VALUE, NULL, &k, NULL);
            if (w == ERROR_SUCCESS) {
                if (instS98Values[i].Kind == INST_S98_WORDS) {
                    w = RegSetValueExA(k, name, 0, REG_SZ,
                                       (const BYTE *)s98Val[i],
                                       (DWORD)strlen(s98Val[i]) + 1);
                } else {
                    dw = (DWORD)strtoul(s98Val[i], NULL, 10);
                    w = RegSetValueExA(k, name, 0, REG_DWORD,
                                       (const BYTE *)&dw, sizeof(dw));
                }
                RegCloseKey(k);
            }
        }
        if (w != ERROR_SUCCESS) {
            out("Stor98's %s could not be written (error %ld).\n", name,
                (long)w);
            flagFailed = 1;
            continue;
        }
        flagChanged = 1;
        flagRestart = 1;
        s98_rec_text("Settings", name, s98Val[i]);
        step(INST_STEP_DONE, "Stor98's %s set to %s", name, s98Val[i]);
        out("Stor98's %s set to %s%s; it applies when the controller next "
            "starts.\n", name, s98Val[i],
            st > 0 ? "" : " (it was not set)");
    }
}

/* The console's rows (record 26 section 6.4): asked only when Stor98 is
 * installed by the run, Enter keeping what is there. */
static void s98_ask_values(void)
{
    char now[64];
    char shown[160];
    char text[1024];
    char line[64];
    char canon[32];
    const char *why;
    int i;
    int k;
    int st;
    int me = !envNow.Nt && envNow.Minor == 90;

    if (instWindow || !s98Include || opts.List || opts.Yes || opts.Defaults ||
        s98Share != INST_S98_SHARE_FREE) {
        return;
    }
    out("Stor98's settings (its readme, SETTINGS); Enter keeps each as it "
        "is.\n");
    for (i = 0; i < INST_S98_COUNT; i++) {
        const INST_S98_VALUE *v = &instS98Values[i];
        if (!inst_s98_applies(i, envNow.Nt) || !v->Asked) {
            continue;
        }
        st = s98_read(i, now, sizeof(now));
        inst_s98_now_text(i, envNow.Nt, me, st, st > 0 ? now : NULL, shown,
                          sizeof(shown));
        if (v->Kind == INST_S98_WORDS) {
            _snprintf(text, sizeof(text) - 1,
                      "%s - %s\n  now %s\n  auto  %s\n  off  %s\n"
                      "  cacheoff  %s\n  fua  %s\n  Type one, or Enter to "
                      "keep it: ", envNow.Nt ? v->KeyNt : v->Name9x, v->What,
                      shown, v->Means[0], v->Means[1], v->Means[2],
                      v->Means[3]);
        } else {
            _snprintf(text, sizeof(text) - 1,
                      "%s - %s\n  now %s\n  0  %s\n  1  %s\n  Type 0 or 1, "
                      "or Enter to keep it: ",
                      envNow.Nt ? v->KeyNt : v->Name9x, v->What, shown,
                      v->Means[0], v->Means[1]);
        }
        text[sizeof(text) - 1] = '\0';
        for (k = 0;; k++) {
            if (!ask(INST_Q_S98_VALUE, k == 0 ? text :
                     "  Not one of those; type one, or Enter to keep it: ",
                     k > 0, i, 0, NULL, line, sizeof(line))) {
                line[0] = '\0';
                out("\n");
                break;
            }
            {
                const char *p = line;
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
                if (*p == '\0') {
                    break;
                }
            }
            if (inst_s98_check(i, envNow.Nt, line, canon, sizeof(canon),
                               &why)) {
                s98Set[i] = 1;
                inst_copy(s98Val[i], canon, sizeof(s98Val[i]));
                break;
            }
        }
    }
    out("\n");
}

/* ---- the question (record 26 section 6.4) -------------------------------- */

static void s98_question(void)
{
    int asked;
    int def;
    int r;
    int k;
    char line[16];
    char text[1024];

    r = inst_s98_decide(haveS98, haveCm && tbInclude, opts.Yes, opts.Stor98,
                        opts.NoStor98, opts.List, osKind == INST_OS_CMONLY,
                        &asked, &def);
    if (!haveS98) {
        s98_apply(0);
        return;
    }
    if (!haveCm || !tbInclude) {
        out("Stor98 %s, the bundled NVMe driver: left out, it comes with the "
            "Thunderbolt 3 /\nUSB4 support.\n\n", s98VersionText);
        logonly("stor98: left out with the Thunderbolt support\n");
        s98_apply(0);
        return;
    }
    out("Stor98 %s, the bundled NVMe driver (a separate project)\n",
        s98VersionText);
    out("  For NVMe drives behind a Thunderbolt or USB4 tunnel; the "
        "installer selects tunnel drives only. On such a drive it installs "
        "where no driver is, and updates an older Stor98. An internal NVMe "
        "drive is left as it is: install Stor98 on one by hand (its "
        "readme).\n");
    if (asked) {
        inst_yesno_question(text, sizeof(text),
                            "Install Stor98, the bundled NVMe driver?",
                            "install it", "leave it out", def);
        for (k = 0;; k++) {
            if (!ask(INST_Q_S98, k == 0 ? text : def ? "  Type Y or N, or "
                     "Enter for [Y]: " : "  Type Y or N, or Enter for [N]: ",
                     k > 0, 0, def, NULL, line, sizeof(line))) {
                r = def;
                out("\n");
                break;
            }
            r = inst_tb_answer(line, def);
            if (r >= 0) {
                break;
            }
        }
    } else if (opts.NoStor98) {
        out("  /nostor98: left out without asking.\n");
    } else if (opts.Stor98) {
        out("  /stor98: installed without asking.\n");
    } else if (opts.List) {
        out("  /list: it would be asked:\n    Y  install it\n"
            "    N  leave it out\n  Enter would give [%s].\n", def ? "Y" : "N");
    } else if (!def) {
        out("  /yes: N on Windows 8 and 8.1 (/stor98 installs it).\n");
    } else {
        out("  /yes: Y, with the Thunderbolt support (/nostor98 leaves it "
            "out).\n");
    }
    out("\n");
    logonly("stor98: asked %d, include %d\n", asked, r);
    s98_apply(r);
}

/* ---- the uninstall (record 26 sections 6.3 and 9.3) ---------------------- */

/* NT: a Stor98 INF the record lists, removed when no devnode names it
 * (SetupUninstallOEMInf without force refuses one in use too). */
static int s98_un_inf_nt(const char *name)
{
    char win[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char pnf[INST_PATH_CHARS];
    int i;
    int s;

    for (i = 0; i < devCount; i++) {
        if (devs[i].InfPathState < 0) {
            out("  A driver key could not be read: %s is kept.\n", name);
            return 0;
        }
        if (devs[i].InfPathState > 0 && inst_eq(devs[i].InfPath, name)) {
            out("  %s is still named by %s: it stays.\n", name, devs[i].Id);
            return 1;
        }
    }
    if (opts.List) {
        out("(/list) %s would be removed from the driver store.\n", name);
        return 1;
    }
    if (api.UninstallOem != NULL) {
        if (!api.UninstallOem(name, 0, NULL)) {
            out("  %s could not be removed (error 0x%08lX).\n", name,
                (unsigned long)GetLastError());
            return 0;
        }
    } else {
        /* Windows 2000 (record 23 section 9.2, KB 813449): nothing names
         * it (read above), then the .pnf, then the .inf; a file already
         * gone is a step done by an earlier run. */
        if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
            return 0;
        }
        join(path, sizeof(path), win, "inf");
        join(path, sizeof(path), path, name);
        inst_copy(pnf, path, sizeof(pnf));
        if (strlen(pnf) > 4) {
            strcpy(pnf + strlen(pnf) - 4, ".pnf");
        }
        s = file_state(pnf);
        if (s < 0 || (s > 0 && !DeleteFileA(pnf))) {
            out("  %s could not be deleted (error %lu).\n", pnf,
                (unsigned long)GetLastError());
            return 0;
        }
        s = file_state(path);
        if (s < 0 || (s > 0 && !DeleteFileA(path))) {
            out("  %s could not be deleted (error %lu).\n", path,
                (unsigned long)GetLastError());
            return 0;
        }
    }
    out("Stor98's %s was removed from the driver store.\n", name);
    flagChanged = 1;
    return s98_rec_del("Staged", name);
}

/* 98 SE and ME: 1 when a driver key of any class names the INF copy. */
static int s98_inf_named_9x(const char *name)
{
    HKEY cls;
    HKEY one;
    char sub[64];
    char sub2[16];
    char path[INST_PATH_CHARS];
    char text[64];
    DWORD i;
    DWORD j;
    DWORD n;
    LONG r;
    int g;
    int found = 0;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "System\\CurrentControlSet\\Services\\Class", 0,
                      KEY_READ, &cls);
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    for (i = 0; found == 0; i++) {
        n = sizeof(sub);
        r = RegEnumKeyExA(cls, i, sub, &n, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS ||
            RegOpenKeyExA(cls, sub, 0, KEY_READ, &one) != ERROR_SUCCESS) {
            found = -1;
            break;
        }
        for (j = 0; found == 0; j++) {
            n = sizeof(sub2);
            r = RegEnumKeyExA(one, j, sub2, &n, NULL, NULL, NULL, NULL);
            if (r == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (r != ERROR_SUCCESS) {
                found = -1;
                break;
            }
            _snprintf(path, sizeof(path) - 1,
                      "System\\CurrentControlSet\\Services\\Class\\%s\\%s",
                      sub, sub2);
            path[sizeof(path) - 1] = '\0';
            g = reg_text3(HKEY_LOCAL_MACHINE, path, "InfPath", text,
                          sizeof(text));
            if (g < 0) {
                found = -1;
            } else if (g > 0) {
                g = s98_inf_names(text, name);
                if (g != 0) {
                    found = g;
                }
            }
        }
        RegCloseKey(one);
    }
    RegCloseKey(cls);
    return found;
}

static int s98_un_inf_9x(const char *name)
{
    char win[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char pnf[INST_PATH_CHARS];
    char alias[INST_PATH_CHARS];
    int named = s98_inf_named_9x(name);
    int s;

    if (named != 0) {
        out("  INF\\%s %s: it stays.\n", name, named > 0 ?
            "is still named by a driver key" : "cannot be checked");
        return named > 0;
    }
    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    join(path, sizeof(path), win, "INF");
    join(path, sizeof(path), path, name);
    inst_copy(pnf, path, sizeof(pnf));
    if (strlen(pnf) > 4) {
        strcpy(pnf + strlen(pnf) - 4, ".PNF");
    }
    if (opts.List) {
        out("(/list) %s would be deleted.\n", path);
        return 1;
    }
    /* The copy's short alias, read while it exists: SETUPX's OEM Name
     * record may name it by either. */
    s = (int)GetShortPathNameA(path, alias, sizeof(alias));
    if (s <= 0 || s >= (int)sizeof(alias)) {
        alias[0] = '\0';
    }
    /* Its .PNF first, then the copy (KB 813449's order). */
    s = file_state(pnf);
    if (s < 0 || (s > 0 && !delete_file(pnf))) {
        out("  %s could not be deleted (error %lu).\n", pnf,
            (unsigned long)GetLastError());
        return 0;
    }
    s = file_state(path);
    if (s < 0 || (s > 0 && !delete_file(path))) {
        out("  %s could not be deleted (error %lu).\n", path,
            (unsigned long)GetLastError());
        return 0;
    }
    out("Stor98's INF copy %s was deleted.\n", path);
    oem_name_forget(path, alias);
    flagChanged = 1;
    return s98_rec_del("Staged", name);
}

/* The service, its files and the recorded settings, when no devnode is
 * bound to Stor98 any more and no Stor98 was there before. */
static int s98_un_service(void)
{
    char win[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char name[96];
    char param[INST_S98_PARAM_CHARS];
    char next[INST_S98_PARAM_CHARS];
    SC_HANDLE scm;
    SC_HANDLE svc;
    HKEY k;
    LONG w;
    int r;
    int g;
    int s;

    if (opts.List) {
        out("(/list) Stor98's service, its driver file and its recorded "
            "settings would be\nremoved when no device is bound to it and "
            "none was there before.\n");
        r = s98_bound_any();
        if (r == 0 && inst_s98_service_removable(0, s98Pre)) {
            svc_gone("stor98");     /* its database entries in the preview */
        }
        return 1;
    }
    r = s98_bound_any();
    if (r < 0) {
        out("Whether a device is still bound to Stor98 cannot be read: it "
            "stays.\n");
        return 0;
    }
    if (!inst_s98_service_removable(r != 0, s98Pre)) {
        out("Stor98 stays: %s.\n", r != 0 ? "a device is still bound to it" :
            "it was installed before this package's installer first ran");
        s98Left = 1;
        return 1;
    }
    /* The settings this installer wrote, each by name; its record entry
     * goes only once the value is gone. */
    while ((g = s98_rec_enum("Settings", 0, name, sizeof(name))) > 0) {
        if (!go_on()) {
            return 0;
        }
        if (envNow.Nt) {
            g = s98_param_read(param, sizeof(param));
            if (g < 0) {
                out("Stor98's DriverParameter cannot be read whole: %s "
                    "stays.\n", name);
                return 0;
            }
            if (g > 0 && inst_s98_param_get(param, name, next, sizeof(next))) {
                if (!inst_s98_param_set(param, name, NULL, next,
                                        sizeof(next))) {
                    return 0;
                }
                w = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_S98_PARAM_NT, 0,
                                  KEY_SET_VALUE, &k);
                if (w == ERROR_SUCCESS) {
                    w = next[0] == '\0' ?
                        RegDeleteValueA(k, "DriverParameter") :
                        RegSetValueExA(k, "DriverParameter", 0, REG_SZ,
                                       (const BYTE *)next,
                                       (DWORD)strlen(next) + 1);
                    RegCloseKey(k);
                }
                if (w != ERROR_SUCCESS) {
                    out("Stor98's %s could not be removed (error %ld).\n",
                        name, (long)w);
                    return 0;
                }
            }
        } else {
            w = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_S98_SVC_9X, 0,
                              KEY_SET_VALUE, &k);
            if (w == ERROR_SUCCESS) {
                w = RegDeleteValueA(k, name);
                RegCloseKey(k);
            }
            if (w != ERROR_SUCCESS && w != ERROR_FILE_NOT_FOUND) {
                out("Stor98's %s could not be removed (error %ld).\n", name,
                    (long)w);
                return 0;
            }
        }
        out("Stor98's %s, which this installer wrote, was removed.\n", name);
        if (!s98_rec_del("Settings", name)) {
            return 0;
        }
    }
    if (g < 0 || GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    if (envNow.Nt) {
        scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (scm == NULL) {
            out("The service manager could not be opened (error %lu): "
                "Stor98's service stays.\n", (unsigned long)GetLastError());
            return 0;
        }
        svc = OpenServiceA(scm, "stor98", DELETE);
        if (svc == NULL) {
            DWORD e = GetLastError();
            CloseServiceHandle(scm);
            if (e != ERROR_SERVICE_DOES_NOT_EXIST) {
                out("The stor98 service could not be opened (error %lu).\n",
                    (unsigned long)e);
                return 0;
            }
            svc_gone("stor98");
        } else {
            if (DeleteService(svc)) {
                out("The stor98 service was removed.\n");
                flagChanged = 1;
                svc_gone("stor98");
            } else if (GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE) {
                svc_gone("stor98");
            } else {
                out("The stor98 service could not be removed (error %lu).\n",
                    (unsigned long)GetLastError());
                CloseServiceHandle(svc);
                CloseServiceHandle(scm);
                return 0;
            }
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
        }
        join(path, sizeof(path), win, "System32\\Drivers\\stor98.sys");
    } else {
        group_binary(INST_GROUP_S98, path, sizeof(path));
        /* A file update queued and not yet done goes with it, or the
         * next start would put stor98.mpd back. */
        s98_wininit_cancel(path);
    }
    s = file_state(path);
    if (s < 0) {
        out("Whether %s is there cannot be read: it stays.\n", path);
        return 0;
    }
    if (s > 0) {
        if (delete_file(path)) {
            out("%s was deleted.\n", path);
            flagChanged = 1;
        } else if (envNow.Nt && delete_at_restart(path)) {
            out("%s is in use: it is deleted at the restart.\n", path);
            flagRestart = 1;
        } else {
            out("%s could not be deleted (error %lu).\n", path,
                (unsigned long)GetLastError());
            flagSteps = 1;
            return 0;
        }
    }
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_S98_KEY "\\Staged");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_S98_KEY "\\Devices");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_S98_KEY "\\Pending");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_S98_KEY "\\Settings");
    w = RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_S98_KEY);
    if (w != ERROR_SUCCESS && w != ERROR_FILE_NOT_FOUND) {
        out("Stor98's install record could not be removed (error %ld).\n",
            (long)w);
        return 0;
    }
    logonly("stor98: the record removed\n");
    return 1;
}

/* Record 26 section 6.3, "The uninstall": the recorded devices, present or
 * not; then each recorded INF copy no devnode names; then the service, its
 * files and settings when nothing depends on them. Each step gated on the
 * one before. 1 go on with the package's own removal; 0 stop. Nothing
 * when the record is absent (a Stor98 this installer did not install). */
/* 1 when the record holds devices to remove (the 98 SE and ME removal
 * takes them in its order even when nothing of the package is left). */
static int s98_rec_devices_any(int present)
{
    char id[INST_ID_CHARS];
    DWORD i;
    int r;

    for (i = 0; (r = s98_rec_enum("Devices", i, id, sizeof(id))) > 0; i++) {
        if (!present || envNow.Nt || enum9x_exists(id) != 0) {
            return 1;
        }
    }
    return r < 0;
}

/* The record's key: 1 there, 0 absent, -1 unreadable. */
static int s98_rec_there(void)
{
    HKEY k;
    LONG r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_S98_KEY, 0, KEY_READ, &k);
    DWORD pre = 0;

    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    RegCloseKey(k);
    /* An unreadable PreExisting keeps everything: Stor98 counts as there
     * before. */
    s98Pre = reg_dword(HKEY_LOCAL_MACHINE, INST_S98_KEY, "PreExisting",
                       &pre) ? pre != 0 : 1;
    return 1;
}

/* 1 when an INF's active text (each line up to its ';') names Stor98's
 * driver file - stor98.mpd on 98 SE and ME, stor98.sys on NT - as a word
 * of its own. An INF that names it only through a string token is not
 * taken: it stays (the conservative side). */
static int s98_file_names_stor98(const char *path)
{
    DWORD n = 0;
    char *buf = read_file(path, &n);
    int found;

    if (buf == NULL) {
        return 0;
    }
    found = inst_inf_names_file(buf, n, envNow.Nt ? "stor98.sys"
                                                  : "stor98.mpd");
    free(buf);
    return found;
}

/* The INF copy an adopted device's driver key names, recorded under
 * Staged when its text is Stor98's. The candidates: INF\<InfPath> (a
 * qualified OTHER\<name> as it is), and on 98 SE and ME each INF\OTHER copy
 * whose long or 8.3 name a bare InfPath is. Exactly one is adopted; none
 * or more than one (the name could mean either) adopts nothing, logged.
 * 0 when the record could not be written. */
/* The candidates for a driver key's InfPath, as s98_adopt_inf takes
 * them: their count (the last one in pick, relative to dir, the engine's
 * INF directory), 0 for none or no InfPath, -1 when a directory cannot
 * be read. */
static int s98_inf_find(const char *infPath, char *dir, size_t dcap,
                        char *pick, size_t pcap)
{
    char win[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char rel[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int count = 0;
    int h1;

    if (infPath[0] == '\0' || GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    join(dir, dcap, win, "INF");
    join(path, sizeof(path), dir, infPath);
    h1 = file_state(path);
    if (h1 < 0) {
        return -1;
    }
    if (h1 > 0) {
        inst_copy(pick, infPath, pcap);
        count++;
    }
    if (!envNow.Nt && strchr(infPath, '\\') == NULL) {
        join(path, sizeof(path), dir, "OTHER\\*.INF");
        h = FindFirstFileA(path, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND &&
                e != ERROR_NO_MORE_FILES) {
                return -1;
            }
        } else {
            do {
                if (inst_eq(fd.cFileName, infPath) ||
                    inst_eq(fd.cAlternateFileName, infPath)) {
                    join(rel, sizeof(rel), "OTHER", fd.cFileName);
                    inst_copy(pick, rel, pcap);
                    count++;
                }
            } while (FindNextFileA(h, &fd));
            h1 = GetLastError() != ERROR_NO_MORE_FILES;
            FindClose(h);
            if (h1) {
                return -1;
            }
        }
    }
    return count;
}

/* 98 SE and ME, the file update (record 26 section 6.3, draft 16): the
 * INF copy each Stor98 driver key names, in INF or INF\OTHER, made the
 * bundled stor98.inf, so a drive first plugged later finds this release
 * there too. A copy whose name is ambiguous, or whose text does not name
 * stor98.mpd, is left and logged. 0 when one could not be written. */
static int s98_inf_refresh_9x(void)
{
    HKEY cls;
    char sub[16];
    char key[INST_PATH_CHARS];
    char text[64];
    char infPath[INST_PATH_CHARS];
    char dir[INST_PATH_CHARS];
    char pick[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char src[INST_PATH_CHARS];
    DWORD i;
    DWORD n;
    LONG r;
    int c;
    int ok = 1;

    join(src, sizeof(src), srcDir, instInfFile[INST_INF_S98]);
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_S98_CLASS_9X, 0, KEY_READ,
                      &cls);
    if (r != ERROR_SUCCESS) {
        return r == ERROR_FILE_NOT_FOUND;
    }
    for (i = 0;; i++) {
        n = sizeof(sub);
        r = RegEnumKeyExA(cls, i, sub, &n, NULL, NULL, NULL, NULL);
        if (r != ERROR_SUCCESS) {
            break;
        }
        join(key, sizeof(key), INST_S98_CLASS_9X, sub);
        if (reg_text3(HKEY_LOCAL_MACHINE, key, "PortDriver", text,
                      sizeof(text)) <= 0 || !inst_eq(text, "stor98.mpd") ||
            reg_text3(HKEY_LOCAL_MACHINE, key, "InfPath", infPath,
                      sizeof(infPath)) <= 0) {
            continue;
        }
        c = s98_inf_find(infPath, dir, sizeof(dir), pick, sizeof(pick));
        if (c != 1) {
            logonly("stor98: InfPath %s of SCSIAdapter\\%s: %d candidate "
                    "copies, left\n", infPath, sub, c);
            if (c < 0) {
                out("The INF copies could not be read: Stor98's INF copy is "
                    "left; run the\ninstaller again.\n");
                ok = 0;
            }
            continue;
        }
        join(path, sizeof(path), dir, pick);
        if (!s98_file_names_stor98(path)) {
            logonly("stor98: INF\\%s does not name stor98.mpd: left\n", pick);
            continue;
        }
        if (same_file(src, path)) {
            continue;
        }
        if (!CopyFileA(src, path, FALSE) || !same_file(src, path)) {
            out("INF\\%s could not be made this release's stor98.inf (error "
                "%lu).\n", pick, (unsigned long)GetLastError());
            ok = 0;
            continue;
        }
        out("INF\\%s is now this release's stor98.inf.\n", pick);
    }
    RegCloseKey(cls);
    return ok;
}

/* 98 SE and ME: the bundled Stor98 put in as a file (record 26 section
 * 6.3, draft 16; the 98 SE update of 2026-10-19 stopped with a fatal
 * exception 0E when Windows' device installer replaced the running
 * stor98.mpd). The new file is written beside the old one as STOR98.TMP
 * and checked, and the swap asked in WININIT.INI's [rename] section, as
 * the setup engine does for a running xhci98.sys (wininit_pending reads
 * it back); the INF copies the driver keys name are refreshed; no device
 * is reinstalled. Windows is then shut down and started again. */
static void s98_file_update_9x(void)
{
    char bin[INST_PATH_CHARS];
    char tmp[INST_PATH_CHARS];
    char src[INST_PATH_CHARS];
    char win[INST_PATH_CHARS];
    char ini[INST_PATH_CHARS];
    char sb[INST_PATH_CHARS];
    char st[INST_PATH_CHARS];
    INST_U32 v[4];
    DWORD n;
    char *slash;

    if (s98Declined && !envNow.Nt && !opts.List) {
        step(INST_STEP_BEGIN, "Updating Stor98's driver file");
    } else if (!s98FileUpd || !s98Include || envNow.Nt || opts.List) {
        if (s98InfRefresh && s98Include && !envNow.Nt && !opts.List &&
            go_on() && !s98_inf_refresh_9x()) {
            flagSteps = 1;
        }
        return;
    } else {
        step(INST_STEP_BEGIN, "Updating Stor98's driver file");
    }
    if (s98FileOk && !s98Declined && !go_on()) {
        /* Cancelled during the jobs before it: nothing of it is done. */
        steps_add_id("stor98.mpd", "not updated: the run was cancelled; run "
                     "the installer again");
        step(INST_STEP_LEFT, "Stor98's driver file: left, the run cancelled");
        return;
    }
    if (!s98FileOk || s98Declined) {
        out("Stor98's driver file is not updated in this run: \"%s\" was "
            "not confirmed.\nRun the installer again to update it.\n",
            instS98DisconnectText);
        steps_add_id("stor98.mpd", "not updated: disconnect every external "
                     "NVMe drive, confirm it, and run the installer again");
        flagSteps = 1;
        step(INST_STEP_LEFT, "Stor98's driver file: left for a later run");
        return;
    }
    group_binary(INST_GROUP_S98, bin, sizeof(bin));
    join(src, sizeof(src), srcDir, "stor98.mpd");
    inst_copy(tmp, bin, sizeof(tmp));
    slash = strrchr(tmp, '\\');
    if (slash == NULL || GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        flagFailed = 1;
        step(INST_STEP_FAILED, "Stor98's driver file: no path");
        return;
    }
    inst_copy(slash + 1, "STOR98.TMP", sizeof(tmp) - (size_t)(slash + 1 - tmp));
    if (!s98_pre_commit()) {
        flagFailed = 1;
        step(INST_STEP_FAILED, "Stor98's driver file: the record");
        return;
    }
    if (!CopyFileA(src, tmp, FALSE) || !same_file(src, tmp) ||
        file_version(tmp, v) <= 0 || inst_ver_cmp(v, s98Version) != 0) {
        out("The new stor98.mpd could not be written beside the old one as "
            "%s (error %lu).\n", tmp, (unsigned long)GetLastError());
        DeleteFileA(tmp);
        flagFailed = 1;
        step(INST_STEP_FAILED, "Stor98's driver file: not written");
        return;
    }
    /* WININIT.INI is read before Windows runs: 8.3 names on both sides. */
    n = GetShortPathNameA(bin, sb, sizeof(sb));
    if (n == 0 || n >= sizeof(sb)) {
        inst_copy(sb, bin, sizeof(sb));
    }
    n = GetShortPathNameA(tmp, st, sizeof(st));
    if (n == 0 || n >= sizeof(st)) {
        inst_copy(st, tmp, sizeof(st));
    }
    join(ini, sizeof(ini), win, "WININIT.INI");
    if (!WritePrivateProfileStringA("rename", sb, st, ini) ||
        !wininit_pending(bin, s98Version)) {
        out("The replacement of %s could not be queued in %s.\n", bin, ini);
        DeleteFileA(tmp);
        flagFailed = 1;
        step(INST_STEP_FAILED, "Stor98's driver file: not queued");
        return;
    }
    logonly("stor98: WININIT.INI [rename] %s=%s\n", sb, st);
    s98FileDone = 1;
    s98Cold = 1;
    flagRestart = 1;
    flagChanged = 1;
    out("Stor98 %s replaces %s when Windows next starts; no drive is "
        "reinstalled.\n", s98VersionText, bin);
    if (!s98_inf_refresh_9x()) {
        /* Retried by the next run (s98InfRefresh), the file then current. */
        flagSteps = 1;
    }
    step(INST_STEP_DONE, "Stor98's driver file: replaced at the next start");
}

/* The uninstall, removing the bundled Stor98 on 98 SE and ME: a file
 * update this installer queued (s98_file_update_9x) is taken back - its
 * [rename] entry for stor98.mpd and STOR98.TMP beside it. */
static void s98_wininit_cancel(const char *bin)
{
    char win[INST_PATH_CHARS];
    char ini[INST_PATH_CHARS];
    char sb[INST_PATH_CHARS];
    char tmp[INST_PATH_CHARS];
    char val[INST_PATH_CHARS];
    char *slash;
    DWORD n;
    int k;

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return;
    }
    join(ini, sizeof(ini), win, "WININIT.INI");
    n = GetShortPathNameA(bin, sb, sizeof(sb));
    if (n == 0 || n >= sizeof(sb)) {
        inst_copy(sb, bin, sizeof(sb));
    }
    for (k = 0; k < 2; k++) {
        const char *key = k == 0 ? sb : bin;
        if (GetPrivateProfileStringA("rename", key, "", val, sizeof(val),
                                     ini) > 0) {
            WritePrivateProfileStringA("rename", key, NULL, ini);
            logonly("stor98: WININIT.INI [rename] %s=%s taken back\n", key,
                    val);
        }
    }
    inst_copy(tmp, bin, sizeof(tmp));
    slash = strrchr(tmp, '\\');
    if (slash != NULL) {
        inst_copy(slash + 1, "STOR98.TMP",
                  sizeof(tmp) - (size_t)(slash + 1 - tmp));
        if (file_state(tmp) > 0 && DeleteFileA(tmp)) {
            logonly("stor98: %s deleted\n", tmp);
        }
    }
}

static int s98_adopt_inf(const char *infPath)
{
    char dir[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char pick[INST_PATH_CHARS];
    int count;
    int h1;

    if (infPath[0] == '\0') {
        return 1;
    }
    count = s98_inf_find(infPath, dir, sizeof(dir), pick, sizeof(pick));
    if (count < 0) {
        return 0;   /* unreadable: no candidate can be ruled out */
    }
    if (count != 1) {
        logonly("stor98: InfPath %s: %d candidate copies, none adopted\n",
                infPath, count);
        return 1;
    }
    join(path, sizeof(path), dir, pick);
    if (!s98_file_names_stor98(path)) {
        logonly("stor98: INF\\%s does not name Stor98's driver: not "
                "adopted\n", pick);
        return 1;
    }
    h1 = s98_rec_has("Staged", pick);
    if (h1 < 0) {
        return 0;
    }
    if (h1 == 0) {
        if (!s98_rec_text("Staged", pick, "adopted")) {
            return 0;
        }
        logonly("stor98: INF\\%s adopted with its device\n", pick);
    }
    return 1;
}

/* 98 SE and ME: every Enum\PCI entry whose driver key names Stor98's port
 * driver, present or not, with the InfPath that key names. -1 when one
 * cannot be read. */
#define INST_S98_ADOPT_MAX 64
static char s98AdId[INST_S98_ADOPT_MAX][INST_ID_CHARS];
static char s98AdInf[INST_S98_ADOPT_MAX][64];
static int s98AdPresent[INST_S98_ADOPT_MAX];
static int s98AdCount;

static int s98_adopt_add(const char *id, const char *inf, int present)
{
    int j;

    for (j = 0; j < s98AdCount; j++) {
        if (inst_eq(s98AdId[j], id)) {
            return 1;
        }
    }
    if (s98AdCount >= INST_S98_ADOPT_MAX) {
        return 0;
    }
    inst_copy(s98AdId[s98AdCount], id, INST_ID_CHARS);
    inst_copy(s98AdInf[s98AdCount], inf, sizeof(s98AdInf[0]));
    s98AdPresent[s98AdCount++] = present;
    return 1;
}

static int s98_enum_bound_9x(void)
{
    HKEY pci;
    HKEY dev;
    char devName[INST_ID_CHARS];
    char instName[INST_ID_CHARS];
    char id[INST_ID_CHARS];
    char drv[128];
    char path[INST_PATH_CHARS];
    char text[64];
    char inf[64];
    DWORD i;
    DWORD j;
    DWORD n;
    int ok = 1;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum\\PCI", 0, KEY_READ, &pci) !=
        ERROR_SUCCESS) {
        return -1;
    }
    for (i = 0; ok; i++) {
        n = sizeof(devName);
        if (RegEnumKeyExA(pci, i, devName, &n, NULL, NULL, NULL, NULL) !=
            ERROR_SUCCESS) {
            break;
        }
        if (RegOpenKeyExA(pci, devName, 0, KEY_READ, &dev) != ERROR_SUCCESS) {
            ok = 0;
            break;
        }
        for (j = 0;; j++) {
            n = sizeof(instName);
            if (RegEnumKeyExA(dev, j, instName, &n, NULL, NULL, NULL, NULL) !=
                ERROR_SUCCESS) {
                break;
            }
            join(id, sizeof(id), "PCI", devName);
            join(id, sizeof(id), id, instName);
            if (reg9x_value(id, "Driver", drv, sizeof(drv)) <= 0) {
                continue;
            }
            join(path, sizeof(path), "System\\CurrentControlSet\\Services\\Class",
                 drv);
            if (reg_text3(HKEY_LOCAL_MACHINE, path, "PortDriver", text,
                          sizeof(text)) <= 0 ||
                !inst_eq(text, "stor98.mpd")) {
                continue;
            }
            if (reg_text3(HKEY_LOCAL_MACHINE, path, "InfPath", inf,
                          sizeof(inf)) <= 0) {
                inf[0] = '\0';
            }
            if (s98_rec_has("Devices", id) == 0 &&
                !s98_adopt_add(id, inf, 0)) {
                ok = 0;
            }
        }
        RegCloseKey(dev);
    }
    RegCloseKey(pci);
    return ok ? 1 : -1;
}

/* The uninstall's side of tunnel-only (owner's coordinator, 2026-10-19):
 * when this package's installer brought Stor98 (PreExisting clear), a
 * device Windows bound to Stor98 after the run - the wizard of an
 * enclosure first plugged later, from the staged INF or the install
 * directory - is the package's too, with the INF copy its driver key
 * names, provided every such device sits behind a tunnel or is gone with
 * its tunnel (not present). One that is internal, or whose place cannot be
 * read, keeps them all, and is named. */
static int s98_un_adopt(void)
{
    int i;
    int j;
    int keep = 0;
    int anyPresent = 0;

    s98AdCount = 0;
    if (!scan()) {
        logonly("stor98: no reading for the adoption: nothing adopted\n");
        return 1;
    }
    /* A drive whose stornvme this installer replaced is its own whatever
     * PreExisting says (record 26 section 5.3.1 case 3b); a record that
     * cannot be read stops the uninstall with it kept. */
    if (!s98_pending_resolve(1)) {
        flagFailed = 1;
        return 0;
    }
    if (s98Pre) {
        return 1;
    }
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        int p = d->Present && !d->RegOnly;
        if (!inst_eq(d->Service, "stor98") ||
            s98_rec_has("Devices", d->Id) != 0) {
            continue;
        }
        if (s98_rec_has("Pending", d->Id) != 0) {
            /* A replacement of unknown outcome, or one not yet read back
             * (record 26 section 5.3.1 case 3b): never adopted. */
            logonly("stor98: %s has a pending replacement: not adopted\n",
                    d->Id);
            continue;
        }
        if (!s98_adopt_add(d->Id, d->InfPathState > 0 ? d->InfPath : "", p)) {
            keep = 1;
        }
        anyPresent |= p;
    }
    if (!envNow.Nt && s98_enum_bound_9x() < 0) {
        out("Stor98 stays: a device bound to it cannot be read.\n");
        keep = 1;
    }
    for (j = 0; j < s98AdCount && !envNow.Nt; j++) {
        for (i = 0; i < devCount; i++) {
            if (inst_eq(devs[i].Id, s98AdId[j]) && devs[i].Present &&
                !devs[i].RegOnly) {
                s98AdPresent[j] = 1;
                anyPresent = 1;
            }
        }
    }
    if (s98AdCount == 0 || keep) {
        return 1;
    }
    if (anyPresent) {
        s98_read_places();
    }
    for (j = 0; j < s98AdCount; j++) {
        int place = INST_S98_AT_UNKNOWN;
        if (!s98AdPresent[j]) {
            continue;
        }
        for (i = 0; i < devCount; i++) {
            if (inst_eq(devs[i].Id, s98AdId[j])) {
                place = devs[i].S98Place;
            }
        }
        if (!inst_s98_adoptable(s98Pre, 1, place)) {
            out("Stor98 stays: %s runs it and is %s, so it is not this "
                "package's to remove.\n", s98AdId[j],
                place == INST_S98_AT_INTERNAL ? "an internal drive" :
                "a drive whose place could not be read");
            keep = 1;
        }
    }
    if (keep) {
        return 1;
    }
    for (j = 0; j < s98AdCount; j++) {
        out("  %s: bound to Stor98 after this installer's run (%s): it "
            "goes with Stor98.\n", s98AdId[j], s98AdPresent[j] ?
            "behind a tunnel" : "not present, its tunnel gone");
        if (opts.List) {
            continue;
        }
        /* The copy first: the device's entry, once removed, no longer
         * says which copy it named. */
        if (!s98_adopt_inf(s98AdInf[j]) ||
            !s98_rec_text("Devices", s98AdId[j], "adopted")) {
            out("Stor98's install record could not be written: the "
                "uninstall stops.\n");
            flagFailed = 1;
            return 0;
        }
    }
    return 1;
}

/* Record 26 section 6.3, "The uninstall", its first step: the recorded
 * devices, present or not, each removed and its entry dropped once it is
 * gone. 1 go on; 0 stop (said). On NT set is the device list; on 98 SE
 * and ME the helper removes them, behind record 27 section 6's gate. */
static int s98_un_devices(HDEVINFO set)
{
    char id[INST_ID_CHARS];
    DWORD i;
    int r;
    int j;
    int gated = 0;
    DEVREC *d;
    DEVREC tmp;
    INST_DI_RESULT res;

    r = s98_rec_there();
    if (r <= 0) {
        if (r < 0) {
            out("Stor98's install record cannot be read: the uninstall "
                "stops.\n");
            flagFailed = 1;
        }
        return r == 0;
    }
    if (!s98_un_adopt()) {
        return 0;
    }
    out("Stor98's devices, as this installer recorded them:\n");
    for (i = 0; (r = s98_rec_enum("Devices", i, id, sizeof(id))) > 0; i++) {
        if (!opts.List && !go_on()) {
            return 0;
        }
        d = NULL;
        for (j = 0; j < devCount; j++) {
            if (inst_eq(devs[j].Id, id)) {
                d = &devs[j];
            }
        }
        if (envNow.Nt) {
            if (d == NULL) {
                out("  %s is gone already.\n", id);
            } else if (!remove_nt(set, d)) {
                return 0;
            }
        } else {
            int e = enum9x_exists(id);
            memset(&tmp, 0, sizeof(tmp));
            inst_copy(tmp.Id, id, sizeof(tmp.Id));
            if (d != NULL) {
                inst_copy(tmp.Desc, d->Desc, sizeof(tmp.Desc));
            }
            if (e < 0) {
                out("  %s cannot be read.\n", id);
                flagFailed = 1;
                return 0;
            } else if (e == 0) {
                out("  %s is gone already.\n", id);
            } else if (opts.List) {
                out("(/list) %s would be removed through Windows' own device "
                    "installer.\n", id);
            } else if (!helper_ready()) {
                out("  %s: remove it in Device Manager, then run the "
                    "uninstall again.\n", id);
                flagSteps = 1;
                return 0;
            } else if (!gated && !gate_9x(1)) {
                return 0;
            } else {
                int v;
                int o;
                gated = 1;
                v = helper_job(INST_DI_REMOVE, &tmp, NULL, 0, &res);
                o = inst_di_outcome(INST_DI_REMOVE, v);
                /* A helper still running after its job leaves the removal
                 * unconfirmed (record 27 section 4.1). */
                if (helperState < 0 ||
                    (o != INST_DI_OUT_OK && o != INST_DI_OUT_RESTART &&
                     o != INST_DI_OUT_ABSENT) || enum9x_exists(id) != 0) {
                    out("  %s could not be removed (%s).\n", id,
                        inst_di_verdict_text(v));
                    if (helperState < 0) {
                        flagSteps = 1;
                    } else {
                        flagFailed = 1;
                    }
                    return 0;
                }
                out("  %s was removed.\n", id);
                flagChanged = 1;
                if (o == INST_DI_OUT_RESTART) {
                    flagRestart = 1;
                }
            }
        }
        /* The record's entry stays until s98_un_verify has read every
         * recorded device gone after the other removals. */
    }
    if (r < 0) {
        out("  The record's devices cannot be read.\n");
        flagFailed = 1;
        return 0;
    }
    return 1;
}

/* After the removals that follow: each Stor98 device removed is still
 * gone (98 SE and ME: no Enum entry; NT: not in a fresh reading), and only
 * then is its entry dropped from the record. 0 when one is back (Windows
 * made it again) or cannot be read: the record keeps it. */
static int s98_un_verify(void)
{
    char id[INST_ID_CHARS];
    DWORD i;
    int j;
    int r;
    int back;

    if (opts.List) {
        return 1;
    }
    r = s98_rec_there();
    if (r <= 0) {
        return r == 0;
    }
    if (envNow.Nt && !scan()) {
        flagFailed = 1;
        return 0;
    }
    /* Every recorded device read gone first; only then is the record
     * emptied, so one Windows made again keeps every entry. */
    for (i = 0; (r = s98_rec_enum("Devices", i, id, sizeof(id))) > 0; i++) {
        back = 0;
        if (envNow.Nt) {
            for (j = 0; j < devCount; j++) {
                if (inst_eq(devs[j].Id, id)) {
                    back = 1;
                }
            }
        } else {
            back = enum9x_exists(id) != 0;
        }
        if (back) {
            out("Stor98's device %s is still or again recorded after its "
                "removal: run the\nuninstall again after a restart.\n", id);
            flagSteps = 1;
            return 0;
        }
    }
    if (r < 0) {
        flagFailed = 1;
        return 0;
    }
    while ((r = s98_rec_enum("Devices", 0, id, sizeof(id))) > 0) {
        if (!s98_rec_del("Devices", id)) {
            return 0;
        }
    }
    if (r < 0) {
        flagFailed = 1;
        return 0;
    }
    return 1;
}

/* Its next steps: each recorded INF copy no devnode names, then the
 * service, its files and settings when nothing depends on them. 1 go on,
 * 0 stop. Nothing when the record is absent. */
static int s98_un_rest(void)
{
    char name[96];
    DWORD i;
    int r;

    r = s98_rec_there();
    if (r <= 0) {
        if (r < 0) {
            flagFailed = 1;
        }
        return r == 0;
    }
    if (envNow.Nt && !opts.List && !scan()) {
        flagFailed = 1;
        return 0;
    }
    for (i = 0; (r = s98_rec_enum("Staged", i, name, sizeof(name))) > 0;) {
        int ok;
        if (!opts.List && !go_on()) {
            return 0;
        }
        ok = envNow.Nt ? s98_un_inf_nt(name) : s98_un_inf_9x(name);
        if (!ok) {
            flagFailed = 1;
            return 0;
        }
        if (opts.List || s98_rec_has("Staged", name) == 1) {
            i++;    /* left (still named), or only listed */
        }
    }
    if (r < 0) {
        flagFailed = 1;
        return 0;
    }
    if (!opts.List && !go_on()) {
        return 0;
    }
    return s98_un_service();
}

/* ---- the window's side (record 26 section 5) ----------------------------- */


/* ======================================================================== */
/* The USB storage drivers on Windows 98 SE (record 26 section 6.5)          */
/* ======================================================================== */

#define INST_STOR_MAX_INF   1024
#define INST_STOR_MAX_JOBS  64

/* A storage device's state at the run's baseline: 0 no driver, Windows'
 * generic disk binding, or not present; 1 bound to the storage drivers;
 * 2 bound to anything else. */
typedef struct _STORBASE {
    char Id[INST_ID_CHARS];
    int State;
} STORBASE;

static int storSt[INST_STOR_COUNT];
static int storUsb20u;
static int storHaveChoice;
static DWORD storChoice;
static int storSource;
static int storDefault;
static int storPlanCase = -1;   /* the case the plan showed */
static int storRunCase = -1;    /* the case read again before the copies */
static int storPre;
static int storPreOwed;
static STORBASE *storBase;
static int storBaseCount;
static int storBaseOk;
static char (*storInfBase)[INST_PATH_CHARS];
static int storInfBaseCount;
static int storInfBaseOk;
static char storJobId[INST_STOR_MAX_JOBS][INST_ID_CHARS];
static int storJobOk[INST_STOR_MAX_JOBS];
static int storJobCount;
static int storUasWait;         /* a UAS devnode at Code 2 before the copies */
static int storWithdrawn;       /* this run took the directory off DevicePath */
static int storInfPlaced;       /* this run placed an INF in %windir%\INF */
static int storInfMissed;       /* an INF's placement failed in this run */
static int storSourceOff;       /* Source is cleared once the copy is made */
static int storStepsLeft;

static int stor_98se(void)
{
    return !envNow.Nt && envNow.Minor == 10;
}

/* Where binary i goes (its INF's DestinationDirs), or where an INF is
 * placed (%windir%\INF, record 26 section 6.5.11): 1, or 0. */
static int stor_dest(int i, char *path, size_t cap)
{
    char d[INST_PATH_CHARS];

    if (instStorFile[i].Dir == INST_STOR_DIR_INF) {
        if (GetWindowsDirectoryA(d, sizeof(d)) == 0) {
            return 0;
        }
        join(d, sizeof(d), d, "INF");
    } else if (instStorFile[i].Dir == INST_STOR_DIR_DRIVERS) {
        if (GetWindowsDirectoryA(d, sizeof(d)) == 0) {
            return 0;
        }
        join(d, sizeof(d), d, "SYSTEM32\\DRIVERS");
    } else if (instStorFile[i].Dir == INST_STOR_DIR_IOS) {
        if (GetSystemDirectoryA(d, sizeof(d)) == 0) {
            return 0;
        }
        join(d, sizeof(d), d, "IOSUBSYS");
    } else {
        return 0;
    }
    join(path, cap, d, instStorFile[i].Name);
    return 1;
}

/* A file's SHA-256: 1 read, 0 absent, -1 present and unreadable. */
static int stor_hash(const char *path, char hex[65])
{
    char *buf;
    DWORD n = 0;
    int s = file_state(path);

    hex[0] = '\0';
    if (s <= 0) {
        return s;
    }
    buf = read_file(path, &n);
    if (buf == NULL) {
        return -1;
    }
    inst_sha256((const unsigned char *)buf, n, hex);
    free(buf);
    return 1;
}

static void stor_key(const char *sub, char *path, size_t cap)
{
    inst_copy(path, INST_STOR_KEY, cap);
    if (sub != NULL) {
        join(path, cap, path, sub);
    }
}

/* A value of the record (sub NULL: the key itself). PreExisting goes
 * first, with the record's first entry. 1 written. */
static int stor_rec_put(const char *sub, const char *name, DWORD type,
                        const void *data, DWORD bytes)
{
    char path[INST_PATH_CHARS];
    HKEY k;
    LONG w;

    if (opts.List) {
        return 1;
    }
    if (storPreOwed && !(sub == NULL && inst_eq(name, "PreExisting"))) {
        DWORD one = (DWORD)storPre;
        storPreOwed = 0;
        if (!stor_rec_put(NULL, "PreExisting", REG_DWORD, &one,
                          sizeof(one))) {
            storPreOwed = 1;
            return 0;
        }
    }
    stor_key(sub, path, sizeof(path));
    w = RegCreateKeyExA(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &k, NULL);
    if (w == ERROR_SUCCESS) {
        w = RegSetValueExA(k, name, 0, type, (const BYTE *)data, bytes);
        RegCloseKey(k);
    }
    if (w != ERROR_SUCCESS) {
        out("The USB storage drivers' install record could not be written "
            "(HKLM\\%s, %s: error %ld).\n", path, name, (long)w);
        flagFailed = 1;
        return 0;
    }
    logonly("storage record: %s\\%s\n", sub != NULL ? sub : "", name);
    return 1;
}

static int stor_rec_text(const char *sub, const char *name, const char *text)
{
    return stor_rec_put(sub, name, REG_SZ, text, (DWORD)strlen(text) + 1);
}

static int stor_rec_dword(const char *name, DWORD v)
{
    return stor_rec_put(NULL, name, REG_DWORD, &v, sizeof(v));
}

/* 1 read, 0 absent (no such value, or no such key), -1 unreadable. */
static int stor_rec_get(const char *sub, const char *name, char *buf,
                        DWORD cap)
{
    char path[INST_PATH_CHARS];
    HKEY k;
    LONG r;

    stor_key(sub, path, sizeof(path));
    buf[0] = '\0';
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    RegCloseKey(k);
    return reg_text3(HKEY_LOCAL_MACHINE, path, name, buf, cap);
}

static int stor_rec_del(const char *sub, const char *name)
{
    char path[INST_PATH_CHARS];
    HKEY k;
    LONG r;

    if (opts.List) {
        return 1;
    }
    stor_key(sub, path, sizeof(path));
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_SET_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 1;
    }
    if (r == ERROR_SUCCESS) {
        r = RegDeleteValueA(k, name);
        RegCloseKey(k);
    }
    if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
        out("The USB storage drivers' install record could not be updated "
            "(%s: error %ld).\n", name, (long)r);
        flagFailed = 1;
        return 0;
    }
    return 1;
}

/* The i-th value of a subkey: its name and text data. 1, 0 at the end,
 * -1 an error. */
static int stor_rec_enum(const char *sub, DWORD i, char *name, DWORD cap,
                         char *data, DWORD dcap)
{
    char path[INST_PATH_CHARS];
    HKEY k;
    LONG r;
    DWORD n = cap;
    DWORD dn = dcap - 1;
    DWORD type;

    stor_key(sub, path, sizeof(path));
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    r = RegEnumValueA(k, i, name, &n, NULL, &type, (LPBYTE)data, &dn);
    RegCloseKey(k);
    if (r == ERROR_NO_MORE_ITEMS) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    data[(type == REG_SZ && dn < dcap) ? dn : 0] = '\0';
    return 1;
}

/* The record's hash of an INF copy, by the copy's long or short path
 * against the Infs values' names (their OEM Name values'), or against the
 * Placed values' (the INFs this installer placed in %windir%\INF, record
 * 26 section 6.5.11). */
static int stor_infs_hash(const char *path, const char *alias, char *hash,
                          DWORD cap)
{
    char name[INST_PATH_CHARS];
    char data[96];
    DWORD i;
    int r;
    int sub;

    hash[0] = '\0';
    for (sub = 0; sub < 2; sub++) {
        for (i = 0;; i++) {
            r = stor_rec_enum(sub == 0 ? "Infs" : "Placed", i, name,
                              sizeof(name), data, sizeof(data));
            if (r < 0) {
                return r;
            }
            if (r == 0) {
                break;
            }
            if (sub == 0 ? inst_oem_name_ours(name, path, alias) :
                (inst_eq(name, path) ||
                 (alias[0] != '\0' && inst_eq(name, alias)))) {
                inst_copy(hash, data, cap);
                return 1;
            }
        }
    }
    return 0;
}

/* 1 when the record's Placed names a copy, 0 none, -1 unreadable. */
static int stor_placed_any(void)
{
    char name[INST_PATH_CHARS];
    char data[96];

    return stor_rec_enum("Placed", 0, name, sizeof(name), data,
                         sizeof(data));
}

/* 1 when a copy Placed names is in place with its recorded bytes, 0 none,
 * -1 the record or a copy cannot be read. */
static int stor_placed_live(void)
{
    char name[INST_PATH_CHARS];
    char data[96];
    char hex[65];
    DWORD i;
    int e;
    int h;

    for (i = 0;; i++) {
        e = stor_rec_enum("Placed", i, name, sizeof(name), data,
                          sizeof(data));
        if (e <= 0) {
            return e;
        }
        h = stor_hash(name, hex);
        if (h < 0) {
            return -1;
        }
        if (h > 0 && inst_eq(hex, data)) {
            return 1;
        }
    }
}

/* The download's folder held to the seven names and hashes (section
 * 6.5.1): 1 verified, 0 not there, -1 not this release's. */
static int stor_pack_check(void)
{
    char path[INST_PATH_CHARS];
    char hex[65];
    int i;
    int bad = 0;

    join(storPackDir, sizeof(storPackDir), root, INST_STOR_PACK_DIR);
    if (file_state(storPackDir) != -1 ||
        GetFileAttributesA(storPackDir) == 0xFFFFFFFFUL) {
        /* Not a directory (file_state answers -1 for one). */
        logonly("storage: %s is not there\n", storPackDir);
        return 0;
    }
    for (i = 0; i < INST_STOR_COUNT; i++) {
        join(path, sizeof(path), storPackDir, instStorFile[i].Name);
        if (stor_hash(path, hex) <= 0 ||
            !inst_eq(hex, instStorFile[i].Sha256)) {
            logonly("storage: %s is %s\n", path, hex[0] ? hex : "missing");
            bad = 1;
        }
    }
    return bad ? -1 : 1;
}

/* Each INF of INF and INF\OTHER that is index i's, by name or text: its
 * state, as one of the candidates of that INF. */
static int stor_inf_states(int i, int *st)
{
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char alias[INST_PATH_CHARS];
    char hex[65];
    char rec[96];
    int cand[16];
    int n = 0;
    int pass;
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char *text;
    DWORD bytes;
    DWORD e;

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        *st = INST_STOR_OTHER;
        return 0;
    }
    for (pass = 0; pass < 2; pass++) {
        const char *sub = pass == 0 ? "INF" : "INF\\OTHER";
        join(pattern, sizeof(pattern), win, sub);
        join(pattern, sizeof(pattern), pattern, "*.INF");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND &&
                e != ERROR_NO_MORE_FILES) {
                *st = INST_STOR_OTHER;
                return 0;
            }
            continue;
        }
        do {
            int mine;
            int s;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            join(path, sizeof(path), win, sub);
            join(path, sizeof(path), path, fd.cFileName);
            bytes = 0;
            text = read_file(path, &bytes);
            if (text == NULL) {
                /* Whose it is cannot be read: it may be this INF. */
                mine = inst_eq(fd.cFileName, instStorFile[i].Name);
                if (mine && n < 16) {
                    cand[n++] = INST_STOR_OTHER;
                }
                continue;
            }
            mine = inst_eq(fd.cFileName, instStorFile[i].Name) ||
                   inst_stor_inf_is(text, bytes, i);
            if (mine) {
                inst_sha256((const unsigned char *)text, bytes, hex);
            }
            free(text);
            if (!mine) {
                continue;
            }
            s = (int)GetShortPathNameA(path, alias, sizeof(alias));
            if (s <= 0 || s >= (int)sizeof(alias)) {
                alias[0] = '\0';
            }
            if (stor_infs_hash(path, alias, rec, sizeof(rec)) < 0) {
                rec[0] = '\0';
            }
            if (n < 16) {
                cand[n++] = inst_stor_state(1, 1, rec, instStorFile[i].Sha256,
                                            hex);
            } else {
                cand[0] = INST_STOR_OTHER;
            }
            logonly("storage: %s is %s's (%s)\n", path, instStorFile[i].Name,
                    hex);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    *st = inst_stor_inf_state(cand, n);
    return 1;
}

/* Every state of section 6.5.3 read, with USB20U and the record. */
static void stor_read_states(int st[INST_STOR_COUNT])
{
    char path[INST_PATH_CHARS];
    char hex[65];
    char rec[96];
    HKEY k;
    LONG r;
    DWORD v;
    int i;
    int h;

    for (i = 0; i < INST_STOR_BINARIES; i++) {
        if (!stor_dest(i, path, sizeof(path))) {
            st[i] = INST_STOR_OTHER;
            continue;
        }
        h = stor_hash(path, hex);
        if (stor_rec_get("Files", path, rec, sizeof(rec)) <= 0) {
            rec[0] = '\0';
        }
        st[i] = inst_stor_state(h != 0, h > 0, rec, instStorFile[i].Sha256,
                                hex);
        logonly("storage: %s %s, recorded %s: state %d\n", path,
                h > 0 ? hex : h == 0 ? "absent" : "unreadable",
                rec[0] ? rec : "no", st[i]);
    }
    for (i = INST_STOR_BINARIES; i < INST_STOR_COUNT; i++) {
        stor_inf_states(i, &st[i]);
        logonly("storage: %s state %d\n", instStorFile[i].Name, st[i]);
    }
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_STOR_USB20U_KEY, 0, KEY_READ,
                      &k);
    if (r == ERROR_SUCCESS) {
        RegCloseKey(k);
    }
    /* Unreadable counts as present: it only ever keeps a file. */
    storUsb20u = r != ERROR_FILE_NOT_FOUND;
    storHaveChoice = reg_dword(HKEY_LOCAL_MACHINE, INST_STOR_KEY, "Choice",
                               &storChoice);
    storSource = reg_dword(HKEY_LOCAL_MACHINE, INST_STOR_KEY, "Source", &v) &&
                 v != 0;
    inst_stor_norm(st, storSource);
    logonly("storage: USB20U %d, choice %d/%lu, source %d, case %d\n",
            storUsb20u, storHaveChoice, (unsigned long)storChoice, storSource,
            inst_stor_case(st, storSource));
}

/* READ: the pack, the states, the default and the switches (98 SE). */
static void stor_read(void)
{
    storHere = 0;
    storInclude = 0;
    storPack = 0;
    storTools = 0;
    storToolsInclude = 0;
    if (opts.Uninstall) {
        return;
    }
    if (!stor_98se()) {
        /* Shown everywhere (task 43.43): elsewhere a tick only copies the
         * seven to the tools folder; unticked by default. */
        storTools = 1;
        storPack = stor_pack_check();
        storToolsInclude = storPack > 0 && opts.Storage;
        logonly("storage: elsewhere, pack %d, include %d\n", storPack,
                storToolsInclude);
        return;
    }
    storHere = 1;
    storPack = stor_pack_check();
    stor_read_states(storSt);
    storPlanCase = inst_stor_case(storSt, storSource);
    storDefault = inst_stor_default(storHaveChoice, (int)storChoice, storSt);
    if (storPack <= 0) {
        storInclude = 0;
    } else if (opts.Storage) {
        storInclude = 1;
    } else if (opts.NoStorage) {
        storInclude = 0;
    } else {
        storInclude = storDefault;
    }
    logonly("storage: pack %d, default %d, include %d\n", storPack,
            storDefault, storInclude);
}

/* The row's lines: the detection's text, and NUSB's entry. */
static void stor_note(char *out, size_t cap)
{
    if (storPack == 0) {
        inst_copy(out, "This download does not carry the USB storage drivers "
                  "(NONFREE\\STORAGE\\NUSB33\\W98SE).", cap);
        return;
    }
    if (storPack < 0) {
        inst_copy(out, "The storage files in this download are not the ones "
                  "this release carries (the log names them).", cap);
        return;
    }
    inst_stor_found_text(out, cap, storSt);
    if (storUsb20u) {
        strncat(out, " NUSB is installed (its Uninstall\\USB20U entry is "
                "there).", cap - strlen(out) - 1);
    }
}

/* What a ticked run does, one sentence (the plan, the Summary). */
static const char *stor_case_text(int c)
{
    switch (c) {
    case INST_STOR_CASE_NONE:
        return "the seven files are installed";
    case INST_STOR_CASE_SAME:
        return "the missing files are installed; the ones found are the same "
               "as this download's and stay as they are";
    case INST_STOR_CASE_OWN:
        return "the files this installer put there stay as they are; any "
               "missing one is put back";
    case INST_STOR_CASE_COMPLETE:
        return "nothing is copied: the files found stay as they are";
    default:
        return "nothing is copied: a file found is not this download's, so "
               "the storage install is not completed";
    }
}

/* A text written as lines of at most INST_COLUMNS, each after lead, broken
 * between words: the console's screens never lean on its own wrap. */
static void stor_out_wrapped(const char *lead, const char *text)
{
    char line[INST_COLUMNS + 1];
    size_t width = INST_COLUMNS - strlen(lead);
    const char *p = text;

    while (*p != '\0') {
        size_t n = strlen(p);
        size_t cut = n;
        if (n > width) {
            cut = width;
            while (cut > 0 && p[cut] != ' ') {
                cut--;
            }
            if (cut == 0) {
                cut = width;
            }
        }
        memcpy(line, p, cut);
        line[cut] = '\0';
        out("%s%s\n", lead, line);
        p += cut;
        while (*p == ' ') {
            p++;
        }
    }
}

static void stor_print_plan(void)
{
    char note[512];

    if (!storHere) {
        return;
    }
    stor_note(note, sizeof(note));
    out("USB storage drivers for Windows 98 SE (NUSB 3.3's):\n");
    stor_out_wrapped("  ", note);
    if (storPack <= 0) {
        out("  Not offered by this download.\n\n");
        return;
    }
    if (!storInclude) {
        out("  Left out of this run%s.\n\n", storSource ?
            "; the copy in the install directory is withdrawn" : "");
        return;
    }
    {
        char t[300];
        _snprintf(t, sizeof(t) - 1, "Ticked: %s.", stor_case_text(storPlanCase));
        t[sizeof(t) - 1] = '\0';
        stor_out_wrapped("  ", t);
        out("\n");
    }
}

/* The console's question (record 26 section 6.5.9), after Stor98's. */
static void stor_question(void)
{
    char text[1200];
    char note[512];
    char line[16];
    int k;
    int r = storInclude;

    if (!storHere) {
        return;
    }
    stor_note(note, sizeof(note));
    out("%s\n", inst_page_text(INST_PT_STOR));
    stor_out_wrapped("  ", inst_page_text(INST_PT_STOR_T));
    stor_out_wrapped("  ", note);
    if (storPack <= 0) {
        out("\n");
        return;
    }
    if (opts.Storage) {
        out("  /storage: installed without asking.\n\n");
        return;
    }
    if (opts.NoStorage) {
        out("  /nostorage: left out without asking.\n\n");
        return;
    }
    if (opts.List) {
        out("  /list: it would be asked; Enter would give [%s].\n\n",
            storDefault ? "Y" : "N");
        return;
    }
    if (opts.Yes || opts.Defaults) {
        out("  %s: %s, the default.\n\n", opts.Yes ? "/yes" : "/defaults",
            storDefault ? "Y" : "N");
        return;
    }
    inst_yesno_question(text, sizeof(text),
                        "Install the USB storage drivers for Windows 98 SE?",
                        "install them", "leave them out", storDefault);
    for (k = 0;; k++) {
        if (!ask(INST_Q_STOR, k == 0 ? text : "  Type Y or N, or Enter: ",
                 k > 0, 0, storDefault, NULL, line, sizeof(line))) {
            r = storDefault;
            out("\n");
            break;
        }
        r = inst_tb_answer(line, storDefault);
        if (r >= 0) {
            break;
        }
    }
    storInclude = r;
    out("\n");
    logonly("storage: asked, include %d\n", storInclude);
}

/* The package's copy takes the seven (package_files): the source path. */
static void pkg_src(const char *rel, char *path, size_t cap)
{
    if (storCopyOk && inst_stor_index(rel) >= 0) {
        join(path, cap, storPackDir, rel);
    } else {
        join(path, cap, pkgDir, rel);
    }
}

/* ---- the run ------------------------------------------------------------ */

/* The INF and INF\OTHER names at the baseline. */
static void stor_inf_base(void)
{
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int pass;
    DWORD e;

    storInfBaseCount = 0;
    storInfBaseOk = 0;
    if (storInfBase == NULL) {
        storInfBase = (char (*)[INST_PATH_CHARS])calloc(INST_STOR_MAX_INF,
                                                        INST_PATH_CHARS);
        if (storInfBase == NULL) {
            return;
        }
    }
    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return;
    }
    for (pass = 0; pass < 2; pass++) {
        join(pattern, sizeof(pattern), win,
             pass == 0 ? "INF\\*.INF" : "INF\\OTHER\\*.INF");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND &&
                e != ERROR_NO_MORE_FILES) {
                return;
            }
            continue;
        }
        do {
            /* Whole names, every one, or the baseline is not whole. */
            if (storInfBaseCount >= INST_STOR_MAX_INF ||
                strlen(fd.cFileName) + 7 >= INST_PATH_CHARS) {
                FindClose(h);
                return;
            }
            inst_copy(storInfBase[storInfBaseCount], pass == 0 ? "" :
                      "OTHER\\", INST_PATH_CHARS);
            strcat(storInfBase[storInfBaseCount], fd.cFileName);
            storInfBaseCount++;
        } while (FindNextFileA(h, &fd));
        e = GetLastError();
        FindClose(h);
        if (e != ERROR_NO_MORE_FILES) {
            return;
        }
    }
    storInfBaseOk = 1;
}

static int stor_inf_was(const char *rel)
{
    int i;

    for (i = 0; i < storInfBaseCount; i++) {
        if (inst_eq(storInfBase[i], rel)) {
            return 1;
        }
    }
    return 0;
}

/* A devnode's storage binding now: 1 the storage drivers, 2 another
 * driver, 0 none, Windows' generic disk binding, or the kind is none. */
static int stor_takes_unit(const DEVREC *d);

static int stor_bound(const DEVREC *d, int kind)
{
    int none = d->Driver[0] == '\0' ||
               (d->Problem == 28UL && d->Service[0] == '\0');

    if (kind == INST_STOR_KIND_BULK) {
        if (inst_eq(d->Service, "USBSTOR")) {
            return 1;
        }
        return none ? 0 : 2;
    }
    if (kind == INST_STOR_KIND_UNIT) {
        if (inst_eq(d->Service, "USBMPHLP")) {
            return 1;
        }
        return stor_takes_unit(d) ? 0 : 2;
    }
    return 2;
}

static int stor_kind(const DEVREC *d)
{
    int k = inst_stor_kind(d->Hw, d->HwBytes, d->Compat, d->CompatBytes);

    /* A device one of this package's INFs matches is the package's (a UAS
     * device is xhciuas.inf's). */
    if (k == INST_STOR_KIND_BULK && d->Inf >= 0) {
        return INST_STOR_KIND_NONE;
    }
    return k;
}

static void stor_dev_base(void)
{
    int i;

    storBaseCount = 0;
    storBaseOk = 0;
    if (storBase == NULL) {
        storBase = (STORBASE *)calloc(INST_MAX_DEVS, sizeof(STORBASE));
        if (storBase == NULL) {
            return;
        }
    }
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        int k = stor_kind(d);
        if (!d->Present || d->RegOnly || k == INST_STOR_KIND_NONE) {
            continue;
        }
        inst_copy(storBase[storBaseCount].Id, d->Id, INST_ID_CHARS);
        storBase[storBaseCount].State = stor_bound(d, k);
        logonly("storage baseline: %s %d\n", d->Id,
                storBase[storBaseCount].State);
        storBaseCount++;
    }
    storBaseOk = 1;
}

static int stor_base_state(const char *id)
{
    int i;

    for (i = 0; i < storBaseCount; i++) {
        if (inst_eq(storBase[i].Id, id)) {
            return storBase[i].State;
        }
    }
    return 0;
}

/* Binary i copied where its INF sends it (section 6.5.4), or INF i placed
 * in %windir%\INF (section 6.5.11): the .TMP beside it, compared, the
 * record (Files, or Placed), then the rename, which never replaces. */
static int stor_place_to(int i, const char *dest, const char *sub);

static int stor_place(int i)
{
    char dest[INST_PATH_CHARS];

    if (!stor_dest(i, dest, sizeof(dest))) {
        return 0;
    }
    return stor_place_to(i, dest, i < INST_STOR_BINARIES ? "Files" : "Placed");
}

/* File i to dest, its record value under sub, never over a file there. */
static int stor_place_to(int i, const char *dest, const char *sub)
{
    char tmp[INST_PATH_CHARS];
    char src[INST_PATH_CHARS];
    char *buf;
    DWORD n = 0;
    DWORD wrote = 0;
    HANDLE h;
    BOOL w;

    own_variant(dest, ".TMP", tmp, sizeof(tmp));
    join(src, sizeof(src), storPackDir, instStorFile[i].Name);
    buf = read_file(src, &n);
    if (buf == NULL) {
        out("%s could not be read.\n", src);
        return 0;
    }
    /* The .TMP made with CREATE_NEW: a name already there is another's,
     * never deleted, and the copy is not made; one this call made is its
     * own to delete. */
    h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        out("%s could not be made (error %lu): %s is not copied.\n", tmp,
            (unsigned long)GetLastError(), instStorFile[i].Name);
        free(buf);
        return 0;
    }
    w = WriteFile(h, buf, n, &wrote, NULL);
    free(buf);
    if (!CloseHandle(h) || !w || wrote != n) {
        out("%s could not be written.\n", tmp);
        delete_file(tmp);
        return 0;
    }
    if (!same_file(src, tmp)) {
        out("%s does not hold the download's bytes.\n", tmp);
        delete_file(tmp);
        return 0;
    }
    SetFileAttributesA(tmp, FILE_ATTRIBUTE_NORMAL);
    if (!stor_rec_text(sub, dest, instStorFile[i].Sha256)) {
        delete_file(tmp);
        return 0;
    }
    if (!MoveFileA(tmp, dest)) {
        out("%s could not be put in place (error %lu): the file there "
            "stays.\n", dest, (unsigned long)GetLastError());
        delete_file(tmp);
        stor_rec_del(sub, dest);
        return 0;
    }
    flagChanged = 1;
    out("Copied %s to %s.\n", instStorFile[i].Name, dest);
    return 1;
}

/* At the first deployment (no Source yet): every copy of the pack's own
 * INF bytes already in INF or INF\\OTHER, listed under Before by its path
 * with BeforeOk 1 once the list is whole (record 26 section 6.5.8 step 1:
 * neither they nor the devices bound through them are ever this
 * installer's). 0 a listing or a record failed. */
static int stor_before_write(void)
{
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char hex[65];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    DWORD v;
    DWORD e;
    int pass;
    int i;

    if (opts.List || reg_dword(HKEY_LOCAL_MACHINE, INST_STOR_KEY, "Source",
                               &v)) {
        return 1;
    }
    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    for (pass = 0; pass < 2; pass++) {
        join(pattern, sizeof(pattern), win,
             pass == 0 ? "INF\\*.INF" : "INF\\OTHER\\*.INF");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND &&
                e != ERROR_NO_MORE_FILES) {
                return 0;
            }
            continue;
        }
        do {
            join(path, sizeof(path), win, pass == 0 ? "INF" : "INF\\OTHER");
            join(path, sizeof(path), path, fd.cFileName);
            if (stor_hash(path, hex) < 0) {
                FindClose(h);
                return 0;
            }
            for (i = INST_STOR_BINARIES; i < INST_STOR_COUNT; i++) {
                if (inst_eq(hex, instStorFile[i].Sha256) &&
                    !stor_rec_text("Before", path, hex)) {
                    FindClose(h);
                    return 0;
                }
            }
        } while (FindNextFileA(h, &fd));
        e = GetLastError();
        FindClose(h);
        if (e != ERROR_NO_MORE_FILES) {
            return 0;
        }
    }
    return stor_rec_dword("BeforeOk", 1);
}

/* Before the package's copy (record 26 section 6.5.4's order): the
 * withdrawal, the baseline, the states read again, PreExisting, the
 * copies, Source. Sets storCopyOk when the seven go into the package's
 * copy. Never refuses the whole run: a storage part that stops is a step
 * left. */
static void stor_run_begin_body(void);
static void stor_place_infs(void);
static void stor_inf_named(int which[2]);
static int stor_inf_found(int i);

/* The INFs this installer placed in %windir%\INF (the record's Placed),
 * withdrawn under section 6.5.8 step 3's protections (sections 6.5.4 and
 * 6.5.11): deleted with its .PNF while its bytes are the recorded ones,
 * NUSB's USB20U is absent and no driver key names it; changed, or with
 * USB20U present, left and forgotten; named by a driver key, left and kept
 * for the uninstall; gone, forgotten. 1 done; 0 a reading or a deletion
 * failed, or the record could not be read or written. */
static int stor_unplace(void)
{
    char name[INST_PATH_CHARS];
    char data[96];
    char hex[65];
    char pnf[INST_PATH_CHARS];
    const char *base;
    DWORD i = 0;
    HKEY u;
    LONG r;
    int usb20u;
    int named;
    int e;
    int h;
    int pass;
    int found = 0;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_STOR_USB20U_KEY, 0, KEY_READ,
                      &u);
    if (r == ERROR_SUCCESS) {
        RegCloseKey(u);
    }
    usb20u = r != ERROR_FILE_NOT_FOUND;
    /* USBSTOR.INF first; USBNTMAP.INF only once no copy of USBSTOR.INF is
     * left where Windows finds it (record 26 section 6.5.5). */
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            found = stor_inf_found(INST_STOR_IDX_USBSTOR_INF);
            if (found < 0) {
                out("Whether USBSTOR.INF is still in the INF directory cannot be "
                    "read.\n");
                return 0;
            }
        }
        for (i = 0;;) {
            e = stor_rec_enum("Placed", i, name, sizeof(name), data, sizeof(data));
            if (e < 0) {
                return 0;
            }
            if (e == 0) {
                break;
            }
            if (inst_stor_rm_late(NULL, data) != pass) {
                i++;
                continue;
            }
            h = stor_hash(name, hex);
            if (h < 0) {
                out("%s cannot be read.\n", name);
                return 0;
            }
            if (h > 0 && found) {
                logonly("storage: %s stays placed: USBSTOR.INF is still there\n",
                        name);
                out("%s stays while USBSTOR.INF is in the INF directory.\n",
                    name);
                i++;
                continue;
            }
            if (h > 0 && inst_eq(hex, data) && !usb20u) {
                base = strrchr(name, '\\');
                base = base != NULL ? base + 1 : name;
                named = s98_inf_named_9x(base);
                if (named < 0) {
                    out("%s: whether a device names it cannot be read.\n", name);
                    return 0;
                }
                if (named > 0) {
                    logonly("storage: %s stays placed: a device names it\n", name);
                    out("%s stays: a device names it (the uninstall removes it "
                        "after the device).\n", name);
                    i++;
                    continue;
                }
                own_variant(name, ".PNF", pnf, sizeof(pnf));
                if (file_state(pnf) < 0 ||
                    (file_state(pnf) > 0 && !delete_file(pnf)) ||
                    !delete_file(name)) {
                    out("%s could not be deleted (error %lu).\n", name,
                        (unsigned long)GetLastError());
                    return 0;
                }
                flagChanged = 1;
                out("Deleted %s.\n", name);
            } else if (h > 0) {
                out("%s stays: %s.\n", name, usb20u ?
                    "NUSB is installed (Uninstall\\USB20U)" :
                    "it has changed since this installer placed it");
            }
            if (!stor_rec_del("Placed", name)) {
                return 0;
            }
        }
    }
    return 1;
}

/* 0 when the run must stop before the package's copy: an earlier
 * deployment could not be withdrawn from DevicePath. */

static int stor_run_begin(void)
{
    int placed;

    storCopyOk = 0;
    storJobCount = 0;
    storUasWait = 0;
    storInfPlaced = 0;
    storInfMissed = 0;
    if (storHere) {
        stor_run_begin_body();
    }
    /* Whatever kept the seven out of this run's copy, an earlier
     * deployment is withdrawn from DevicePath, and the INFs it placed in
     * %windir%\INF deleted, before the copy is tried (sections 6.5.4 and
     * 6.5.11); Source goes only once that copy is made. */
    placed = storHere && !storCopyOk && !opts.List ? stor_placed_any() : 0;
    /* DevicePath first, so a placed copy that cannot be read or deleted
     * never leaves the package directory published. */
    if (storHere && !storCopyOk && (storSource || placed != 0) &&
        !storWithdrawn && !opts.List) {
        if (!devicepath_update(NULL)) {
            out("The USB storage drivers' copy in %s could not be withdrawn "
                "from DevicePath:\nnothing else is changed. Run the "
                "installer again.\n", toolDir);
            flagFailed = 1;
            return 0;
        }
        storWithdrawn = 1;
        out("The USB storage drivers' copy in %s is withdrawn from "
            "DevicePath.\n", toolDir);
    }
    if (placed < 0) {
        out("The USB storage drivers' install record cannot be read: "
            "nothing else is changed.\nRun the installer again.\n");
        flagFailed = 1;
        return 0;
    }
    if (placed > 0 && !stor_unplace()) {
        out("The USB storage drivers' INFs in the INF directory could not "
            "be withdrawn:\nnothing else is changed. Run the installer "
            "again.\n");
        flagFailed = 1;
        return 0;
    }
    storSourceOff = storSource && !storCopyOk;
    return 1;
}

/* The partial-other case's lines (section 6.5.4), pre-wrapped. */
static void stor_say_other(const int *st)
{
    char note[512];

    inst_stor_found_text(note, sizeof(note), st);
    out("USB storage drivers:\n");
    stor_out_wrapped("  ", note);
    stor_out_wrapped("  ", "A file found is not this download's, so nothing "
                     "is copied over or beside it: the storage install is "
                     "not complete.");
}

static void stor_run_begin_body(void)
{
    int st[INST_STOR_COUNT];
    INST_U32 set;
    int i;
    int any = 0;

    if (storPack <= 0) {
        return;
    }
    stor_read_states(st);
    storRunCase = inst_stor_case(st, storSource);
    if (!opts.List && !stor_rec_dword("Choice", (DWORD)storInclude)) {
        return;
    }
    if (!storInclude) {
        return;
    }
    if (storRunCase != storPlanCase) {
        out("The USB storage files changed while the installer waited: "
            "nothing of them is\ncopied in this run. Run the installer "
            "again.\n");
        storStepsLeft = 1;
        return;
    }
    if (!inst_stor_route(storRunCase)) {
        if (storRunCase == INST_STOR_CASE_OTHER) {
            stor_say_other(st);
            storStepsLeft = 1;
        }
        return;
    }
    /* The baselines whole, or nothing is copied: ownership is never read
     * off a partial picture. */
    if (!scan()) {
        out("The devices could not be read whole: the USB storage drivers "
            "are not copied.\n");
        storStepsLeft = 1;
        return;
    }
    plan();
    stor_dev_base();
    stor_inf_base();
    if (!storBaseOk || !storInfBaseOk) {
        out("The devices or the INF directories could not be listed whole: "
            "the USB storage\ndrivers are not copied.\n");
        storStepsLeft = 1;
        return;
    }
    for (i = 0; i < INST_STOR_COUNT; i++) {
        if (st[i] == INST_STOR_SAME || st[i] == INST_STOR_OTHER) {
            any = 1;
        }
    }
    {
        DWORD pre;
        if (!reg_dword(HKEY_LOCAL_MACHINE, INST_STOR_KEY, "PreExisting",
                       &pre)) {
            storPre = any;
            storPreOwed = 1;
        } else {
            storPre = pre != 0;
            storPreOwed = 0;
        }
    }
    set = inst_stor_copy_set(1, st, storSource);
    /* A UAS devnode at Code 2 now, with USBNTMAP.SYS about to be copied:
     * it starts at the next fresh start (section 6.5.5). */
    if ((set & 0x02UL) != 0) {
        for (i = 0; i < devCount; i++) {
            if (devs[i].Present && devs[i].Inf == INST_INF_UAS &&
                devs[i].Problem == 2UL) {
                storUasWait = 1;
            }
        }
    }
    if (opts.List) {
        for (i = 0; i < INST_STOR_BINARIES; i++) {
            if (set & (1UL << i)) {
                out("(/list) %s would be copied.\n", instStorFile[i].Name);
            }
        }
        {
            int named[2];
            INST_U32 inf;
            stor_inf_named(named);
            inf = inst_stor_inf_set(1, named);
            for (i = INST_STOR_BINARIES; i < INST_STOR_COUNT; i++) {
                if (inf & (1UL << i)) {
                    out("(/list) %s would be placed in the INF directory.\n",
                        instStorFile[i].Name);
                }
            }
        }
        return;
    }
    if (!stor_before_write()) {
        storStepsLeft = 1;
        return;
    }
    if (set != 0 && storPreOwed) {
        DWORD one = (DWORD)storPre;
        if (!stor_rec_put(NULL, "PreExisting", REG_DWORD, &one, sizeof(one))) {
            storStepsLeft = 1;
            return;
        }
        storPreOwed = 0;
    }
    for (i = 0; i < INST_STOR_BINARIES; i++) {
        if ((set & (1UL << i)) != 0 && !stor_place(i)) {
            out("The USB storage drivers are not complete: the rest is not "
                "copied, and the\npackage's copy is made without them.\n");
            storStepsLeft = 1;
            return;
        }
    }
    /* Every binary in place, of this download's bytes or this
     * installer's, before the INFs are published. */
    stor_read_states(st);
    for (i = 0; i < INST_STOR_BINARIES; i++) {
        if (st[i] != INST_STOR_OWN && st[i] != INST_STOR_SAME) {
            out("%s is not in place: the USB storage drivers' INFs are not "
                "published.\n", instStorFile[i].Name);
            storStepsLeft = 1;
            return;
        }
    }
    /* The INFs too: nothing of another's bytes may be beside what Setup
     * would copy (the copies just made can turn a partial case into all
     * seven present, which is still this run's route). */
    for (i = INST_STOR_BINARIES; i < INST_STOR_COUNT; i++) {
        if (st[i] == INST_STOR_OTHER) {
            stor_say_other(st);
            storStepsLeft = 1;
            return;
        }
    }
    if (!stor_rec_dword("Source", 1)) {
        storStepsLeft = 1;
        return;
    }
    storSource = 1;
    storCopyOk = 1;
    stor_place_infs();
}

/* Each of the two INFs with no file of its name in %windir%\INF, placed
 * there (section 6.5.11). A failure is a note: the package directory on
 * DevicePath stays the route. */
static void stor_inf_named(int which[2])
{
    char path[INST_PATH_CHARS];
    int k;

    for (k = 0; k < 2; k++) {
        int i = k == 0 ? INST_STOR_IDX_USBSTOR_INF : INST_STOR_IDX_USBNTMAP_INF;
        which[k] = stor_dest(i, path, sizeof(path)) ? file_state(path) : -1;
        if (which[k] != 0) {
            logonly("storage: %s in the INF directory: %d, not placed\n",
                    instStorFile[i].Name, which[k]);
        }
    }
}

/* The INF at index i in %windir%\INF under its own name: 1, 0, -1. */
static int stor_inf_here(int i)
{
    char path[INST_PATH_CHARS];
    int s;

    if (!stor_dest(i, path, sizeof(path))) {
        return -1;
    }
    s = file_state(path);
    return s > 0 ? 1 : s;
}

static void stor_place_infs(void)
{
    int named[2];
    INST_U32 set;
    int i;
    int k;
    int ours = 0;
    int pair;
    int st[INST_STOR_COUNT];

    stor_inf_named(named);
    set = inst_stor_inf_set(storCopyOk, named);
    /* USBNTMAP.INF first, and USBSTOR.INF only once it is there: never
     * USBSTOR.INF alone, which lets 98 SE's own NTMAP.INF take the disk
     * unit (record 26 section 6.5.5). */
    for (k = INST_STOR_BINARIES; k < INST_STOR_COUNT; k++) {
        i = instStorCopyOrder[k];
        if ((set & (1UL << i)) == 0) {
            continue;
        }
        if (i == INST_STOR_IDX_USBSTOR_INF &&
            stor_inf_here(INST_STOR_IDX_USBNTMAP_INF) != 1) {
            storInfMissed = 1;
            continue;
        }
        if (stor_place(i)) {
            /* A placement publishes the pack: the adoption's evidence
             * (record 26 section 6.5.6). */
            if (!storInfPlaced) {
                stor_rec_dword("Published", 1);
            }
            storInfPlaced = 1;
            ours = 1;
        } else {
            storInfMissed = 1;
        }
    }
    /* The pair in %windir%\INF whole or not there: a half this run made is
     * withdrawn, and a step is left. */
    pair = inst_stor_pair(stor_inf_here(INST_STOR_IDX_USBSTOR_INF),
                          stor_inf_here(INST_STOR_IDX_USBNTMAP_INF));
    if (ours && pair != INST_STOR_PAIR_BOTH) {
        out("The USB storage drivers' two INFs could not both be put in the "
            "INF directory:\nneither is left there.\n");
        if (!stor_unplace()) {
            out("The one placed could not be withdrawn. Run the installer "
                "again.\n");
        }
        storInfPlaced = 0;
        storInfMissed = 1;
        storStepsLeft = 1;
    } else if (pair != INST_STOR_PAIR_BOTH && pair != INST_STOR_PAIR_NONE) {
        logonly("storage: the INFs in the INF directory are not a pair "
                "(%d)\n", pair);
        storInfMissed = 1;
        storStepsLeft = 1;
    }
    /* The route read again after the moves, and when nothing was moved:
     * a file of other bytes that appeared meanwhile makes it
     * partial-other, and the withdrawal runs before the package's copy
     * (section 6.5.11). */
    stor_read_states(st);
    for (i = 0; i < INST_STOR_COUNT; i++) {
        if (st[i] == INST_STOR_OTHER ||
            (i < INST_STOR_BINARIES && st[i] != INST_STOR_OWN &&
             st[i] != INST_STOR_SAME)) {
            stor_say_other(st);
            storStepsLeft = 1;
            storCopyOk = 0;
            storInfPlaced = 0;
            storInfMissed = 0;
            return;
        }
    }
    stor_silent_merge();
}

/* After the package's copy is made: Source cleared when it was made
 * without the seven over an earlier deployment. */
static void stor_copy_made(void)
{
    /* The package's copy with the seven made and DevicePath naming it:
     * the publication the uninstall's adoption rests on, kept for good
     * (record 26 section 6.5.6). */
    if (storCopyOk && !opts.List) {
        stor_rec_dword("Published", 1);
    }
    if (storSourceOff && !storCopyOk && !opts.List &&
        stor_rec_dword("Source", 0)) {
        storSource = 0;
        storSourceOff = 0;
    }
}

/* ---- the devices (record 26 section 6.5.5) ------------------------------ */

static int stor_takes_unit(const DEVREC *d)
{
    return inst_stor_unit_takes(!(d->Driver[0] == '\0' ||
                                  (d->Problem == 28UL &&
                                   d->Service[0] == '\0')));
}

/* 1 when a copy of the INF at index i is in INF or INF\OTHER, by its name
 * or its text; 0 none; -1 the directories could not be read. */
static int stor_inf_found(int i)
{
    int s;

    if (!stor_inf_states(i, &s)) {
        return -1;
    }
    return s != INST_STOR_ABSENT;
}

/* The helper's Bulk-Only step admitted (record 26 section 6.5.5): the
 * package's copy holds USBNTMAP.INF of the download's bytes, and the INF
 * directories do not hold USBSTOR.INF without it. */
static int stor_bulk_ok(void)
{
    char from[INST_PATH_CHARS];
    char to[INST_PATH_CHARS];
    const char *n = instStorFile[INST_STOR_IDX_USBNTMAP_INF].Name;

    join(from, sizeof(from), storPackDir, n);
    join(to, sizeof(to), srcDir, n);
    return inst_stor_bulk_admit(storCopyOk, same_file(from, to) ? 1 : 0,
                                inst_stor_pair(
                                    stor_inf_found(INST_STOR_IDX_USBSTOR_INF),
                                    stor_inf_found(INST_STOR_IDX_USBNTMAP_INF)));
}

static int stor_job_index(const char *id)
{
    int i;

    for (i = 0; i < storJobCount; i++) {
        if (inst_eq(storJobId[i], id)) {
            return i;
        }
    }
    return -1;
}

static int stor_takes(const DEVREC *d, int kind)
{
    if (!d->Present || d->RegOnly || stor_kind(d) != kind ||
        stor_job_index(d->Id) >= 0) {
        return 0;
    }
    return stor_bound(d, kind) == 0;
}

static int stor_any(int kind)
{
    int i;

    for (i = 0; i < devCount; i++) {
        if (stor_takes(&devs[i], kind)) {
            return 1;
        }
    }
    return 0;
}

/* One storage device by the helper with the pack's INF from the install
 * directory. */
static void stor_job(DEVREC *d, int kind)
{
    char inf[INST_PATH_CHARS];
    char why[128];
    INST_DI_RESULT r;
    const char *name = d->Desc[0] ? d->Desc : d->Id;
    int v;
    int o;
    int j;

    join(inf, sizeof(inf), srcDir, instStorFile[kind == INST_STOR_KIND_BULK ?
                                                 INST_STOR_IDX_USBSTOR_INF :
                                                 INST_STOR_IDX_USBNTMAP_INF].Name);
    out("Installing %s (%s) ...\n", name, kind == INST_STOR_KIND_BULK ?
        "USB storage" : "USB Disk");
    step(INST_STEP_BEGIN, "Installing %s", name);
    out_flush();
    helperRan = 1;
    if (storJobCount >= INST_STOR_MAX_JOBS) {
        step(INST_STEP_LEFT, "%s: steps left for you", name);
        storStepsLeft = 1;
        return;
    }
    j = storJobCount++;
    inst_copy(storJobId[j], d->Id, INST_ID_CHARS);
    storJobOk[j] = 0;
    v = helper_job(INST_DI_INSTALL, d, inf, 0, &r);
    o = inst_di_outcome(INST_DI_INSTALL, v);
    _snprintf(why, sizeof(why) - 1, "%s%s%s", inst_di_verdict_text(v),
              r.Error[0] ? ": " : "", r.Error);
    why[sizeof(why) - 1] = '\0';
    out("  %s.\n", why);
    switch (o) {
    case INST_DI_OUT_OK:
    case INST_DI_OUT_RESTART:
        storJobOk[j] = 1;
        flagChanged = 1;
        if (o == INST_DI_OUT_RESTART) {
            flagRestart = 1;
        }
        step(INST_STEP_DONE, "%s: %s", name, why);
        break;
    case INST_DI_OUT_REFUSED:
        helperState = -1;
        flagFailed = 1;
        step(INST_STEP_FAILED, "%s: %s", name, why);
        break;
    case INST_DI_OUT_ABSENT:
    case INST_DI_OUT_STEPS:
        storStepsLeft = 1;
        step(INST_STEP_LEFT, "%s: %s", name, why);
        break;
    default:
        if (inst_di_unfinished(v)) {
            storStepsLeft = 1;
            step(INST_STEP_LEFT, "%s: %s", name, why);
        } else {
            flagFailed = 1;
            step(INST_STEP_FAILED, "%s: %s", name, why);
        }
        break;
    }
}

static void stor_record(void);

/* A storage step of record 27 section 6 (3a, Bulk-Only devices; 4a, the
 * units), behind the gate; *first as helper_installs keeps it. 0 stop. */
static int stor_stage(int kind, int *first)
{
    int i;
    int r;

    if (!storCopyOk || helperState < 0 || runCancelled) {
        return 1;
    }
    if (!*first || kind == INST_STOR_KIND_UNIT) {
        out("Waiting for the devices to settle ... ");
        r = settle(INST_INSTALL_WAIT_MS, 20000UL, 20000UL, 0);
        if (r <= 0) {
            settle_failed(r, "10 minutes");
            return 0;
        }
        out("done.\n");
        if (!scan()) {
            flagFailed = 1;
            return 0;
        }
        plan();
        s98_record_devices();
        stor_record();
    }
    if (!stor_any(kind)) {
        return 1;
    }
    if (kind == INST_STOR_KIND_BULK && !stor_bulk_ok()) {
        out("The USB storage devices are not installed now: USBNTMAP.INF is "
            "not where Windows\nfinds USBSTOR.INF. Run the installer "
            "again.\n");
        logonly("storage: the Bulk-Only step not admitted\n");
        storStepsLeft = 1;
        return 1;
    }
    if (!gate_9x(1)) {
        return 0;
    }
    *first = 0;
    for (i = 0; i < devCount; i++) {
        if (!stor_takes(&devs[i], kind)) {
            continue;
        }
        if (!go_on()) {
            return 0;
        }
        stor_job(&devs[i], kind);
        if (helperStop || helperState < 0) {
            return 0;
        }
    }
    return 1;
}

/* The new copies in INF and INF\OTHER of the pack's bytes with their OEM
 * Name values, each recorded by the rule of section 6.5.6 once a recorded
 * device names it or PreExisting is clear. */
static void stor_record_infs(void)
{
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char alias[INST_PATH_CHARS];
    char rel[96];
    char hex[65];
    char vname[INST_PATH_CHARS];
    char vdata[96];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    HKEY k;
    int pass;
    int which;
    int i;
    DWORD n;
    DWORD dn;
    DWORD type;
    DWORD vi;

    if (!storInfBaseOk || GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return;
    }
    for (pass = 0; pass < 2; pass++) {
        join(pattern, sizeof(pattern), win,
             pass == 0 ? "INF\\*.INF" : "INF\\OTHER\\*.INF");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            int named = 0;
            int found = 0;
            inst_copy(rel, pass == 0 ? "" : "OTHER\\", sizeof(rel));
            strncat(rel, fd.cFileName, sizeof(rel) - strlen(rel) - 1);
            if (stor_inf_was(rel)) {
                continue;
            }
            join(path, sizeof(path), win, "INF");
            join(path, sizeof(path), path, rel);
            if (stor_hash(path, hex) <= 0) {
                continue;
            }
            which = -1;
            for (i = INST_STOR_BINARIES; i < INST_STOR_COUNT; i++) {
                if (inst_eq(hex, instStorFile[i].Sha256)) {
                    which = i;
                }
            }
            if (which < 0) {
                continue;
            }
            n = GetShortPathNameA(path, alias, sizeof(alias));
            if (n == 0 || n >= sizeof(alias)) {
                alias[0] = '\0';
            }
            if (stor_infs_hash(path, alias, vdata, sizeof(vdata)) > 0) {
                continue;
            }
            for (i = 0; i < devCount && !named; i++) {
                const DEVREC *d = &devs[i];
                char dv[64];
                if (!d->Present || d->InfPathState <= 0 ||
                    stor_rec_get("Devices", d->Id, dv, sizeof(dv)) <= 0) {
                    continue;
                }
                named = s98_inf_names(d->InfPath, rel) > 0;
            }
            if (!named && storPre) {
                logonly("storage: INF\\%s appeared, named by no device of "
                        "this installer's: left\n", rel);
                continue;
            }
            /* Its OEM Name value, by the copy's path or alias, naming the
             * pack's INF. */
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_OEM_NAME_KEY, 0,
                              KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
                for (vi = 0; !found; vi++) {
                    n = sizeof(vname);
                    dn = sizeof(vdata) - 1;
                    if (RegEnumValueA(k, vi, vname, &n, NULL, &type,
                                      (LPBYTE)vdata, &dn) != ERROR_SUCCESS) {
                        break;
                    }
                    vdata[type == REG_SZ && dn < sizeof(vdata) ? dn : 0] = '\0';
                    if (inst_oem_name_ours(vname, path, alias) &&
                        inst_eq(vdata, instStorFile[which].Name)) {
                        found = 1;
                    }
                }
                RegCloseKey(k);
            }
            if (!found) {
                logonly("storage: INF\\%s has no OEM Name value naming %s: "
                        "not recorded\n", rel, instStorFile[which].Name);
                continue;
            }
            out("The setup engine's copy %s of %s is recorded as this "
                "installer's.\n", path, instStorFile[which].Name);
            stor_rec_text("Infs", vname, hex);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}

/* The INF copy a device's driver key names (its InfPath, in INF or
 * INF\OTHER, the 8.3 alias included): 1 exactly one, of the pack's bytes
 * (*recorded: one the record's Infs holds, by its exact OEM Name value's
 * path, with those bytes); 0 none, or one of other bytes; -1 the name is
 * ambiguous, or a listing or a copy cannot be read. */
static char storLastCopy[INST_PATH_CHARS];  /* the copy it last matched */
static int storLastWhich;                   /* and which INF of the pack */

static int stor_infpath_copy(const DEVREC *d, int *recorded)
{
    char alias[INST_PATH_CHARS];
    char rec[96];
    DWORD an;
    char win[INST_PATH_CHARS];
    char pattern[INST_PATH_CHARS];
    char path[INST_PATH_CHARS];
    char rel[96];
    char hex[65];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    DWORD e;
    int pass;
    int count = 0;
    int pack = 0;
    int r;
    int i;

    *recorded = 0;
    if (d->InfPathState <= 0) {
        return d->InfPathState < 0 ? -1 : 0;
    }
    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return -1;
    }
    for (pass = 0; pass < 2; pass++) {
        join(pattern, sizeof(pattern), win,
             pass == 0 ? "INF\\*.INF" : "INF\\OTHER\\*.INF");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            e = GetLastError();
            if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND &&
                e != ERROR_NO_MORE_FILES) {
                return -1;
            }
            continue;
        }
        do {
            inst_copy(rel, pass == 0 ? "" : "OTHER\\", sizeof(rel));
            strncat(rel, fd.cFileName, sizeof(rel) - strlen(rel) - 1);
            r = s98_inf_names(d->InfPath, rel);
            if (r < 0) {
                FindClose(h);
                return -1;
            }
            if (r == 0) {
                continue;
            }
            count++;
            join(path, sizeof(path), win, "INF");
            join(path, sizeof(path), path, rel);
            if (stor_hash(path, hex) < 0) {
                FindClose(h);
                return -1;
            }
            for (i = INST_STOR_BINARIES; i < INST_STOR_COUNT; i++) {
                if (inst_eq(hex, instStorFile[i].Sha256)) {
                    pack = 1;
                    inst_copy(storLastCopy, path, sizeof(storLastCopy));
                    storLastWhich = i;
                    an = GetShortPathNameA(path, alias, sizeof(alias));
                    if (an == 0 || an >= sizeof(alias)) {
                        alias[0] = '\0';
                    }
                    *recorded = stor_infs_hash(path, alias, rec,
                                               sizeof(rec)) > 0 &&
                                inst_eq(rec, hex);
                }
            }
        } while (FindNextFileA(h, &fd));
        e = GetLastError();
        FindClose(h);
        if (e != ERROR_NO_MORE_FILES) {
            return -1;
        }
    }
    if (count > 1) {
        return -1;
    }
    return count == 1 && pack;
}

/* After every settle and job, at the end: the devices first bound to the
 * storage drivers in this run (section 6.5.6's two rules), then the copies. */
static void stor_record(void)
{
    char dv[64];
    int i;
    int j;

    if (!storCopyOk || !storBaseOk || opts.List) {
        return;
    }
    for (i = 0; i < devCount; i++) {
        const DEVREC *d = &devs[i];
        int k = stor_kind(d);
        int before;
        if (!d->Present || d->RegOnly || k == INST_STOR_KIND_NONE ||
            stor_bound(d, k) != 1) {
            continue;
        }
        before = stor_base_state(d->Id);
        if (before == 1 || stor_rec_get("Devices", d->Id, dv, sizeof(dv)) != 0) {
            continue;
        }
        j = stor_job_index(d->Id);
        if (before == 2) {
            continue;
        }
        if (j >= 0 && storJobOk[j]) {
            /* The helper's own job, with the pack's INF alone. */
            out("%s is recorded as this installer's (this run installed "
                "it).\n", d->Desc[0] ? d->Desc : d->Id);
            stor_rec_text("Devices", d->Id, k == INST_STOR_KIND_BULK ?
                          "bulk" : "unit");
            continue;
        }
        {
            /* Windows bound it during a settle: through a copy of the
             * pack's own INF, and with anything of the seven there before,
             * one the record holds as this installer's (section 6.5.6). */
            int recorded;
            int r = stor_infpath_copy(d, &recorded);
            if (r == 1 && (!storPre || recorded)) {
                out("%s is recorded as this installer's (Windows bound it "
                    "from this installer's copy).\n",
                    d->Desc[0] ? d->Desc : d->Id);
                stor_rec_text("Devices", d->Id, k == INST_STOR_KIND_BULK ?
                              "bulk" : "unit");
            } else {
                logonly("storage: %s newly bound, not this installer's by "
                        "the record's rules (%d)\n", d->Id, r);
            }
        }
    }
    stor_record_infs();
}

/* The end of an install run: the units read back, the notes. */
static void stor_run_end(void)
{
    int i;

    if (!storHere || storPack <= 0) {
        return;
    }
    if (storCopyOk) {
        stor_record();
        for (i = 0; i < devCount; i++) {
            const DEVREC *d = &devs[i];
            if (!d->Present || stor_kind(d) != INST_STOR_KIND_UNIT ||
                stor_bound(d, INST_STOR_KIND_UNIT) != 1) {
                continue;
            }
            if ((d->Status & INST_DN_STARTED) == 0 || d->Problem != 0) {
                out("\n%s\n  Unplug the disk and plug it in again: it starts "
                    "as \"USB Disk\" then.\n", d->Desc[0] ? d->Desc : d->Id);
                storStepsLeft = 1;
            }
        }
        /* Every storage device still without its driver (the helper not
         * usable here, a job that did not finish, a device that came too
         * late): its steps, and the run ends with steps left. */
        for (i = 0; i < devCount && !opts.List; i++) {
            const DEVREC *d = &devs[i];
            int k = stor_kind(d);
            if (!d->Present || d->RegOnly || k == INST_STOR_KIND_NONE ||
                stor_bound(d, k) != 0) {
                continue;
            }
            out("\n%s\n  No driver yet: in Device Manager open it, "
                "Driver, Update Driver, and let\n  the wizard search, or "
                "give it the folder %s.\n",
                d->Desc[0] ? d->Desc : d->Id, toolDir);
            storStepsLeft = 1;
        }
    }
    if (storCopyOk && !opts.List && !silent_gate_on()) {
        out("\nUSB storage: Windows' wizard still opens for each new USB disk "
            "unless the silent\ninstall (XhciSilentInstall) is turned on in "
            "the USB settings.\n");
    }
    if (storInfPlaced && !opts.List) {
        out("\nUSB storage: if Windows asks for a file such as USBSTOR.SYS "
            "when a USB disk is\nfirst plugged in, type %s.\n", toolDir);
    }
    if (storInfMissed && !opts.List) {
        out("\nUSB storage: the drivers' INFs could not all be put in the "
            "INF directory, so\nWindows may open its New Hardware wizard "
            "for a new USB disk: finish it (Next\nuntil Finish); it finds "
            "the driver in %s.\n", toolDir);
    }
    if (storUasWait && storCopyOk) {
        out("\nA UAS disk at Code 2 starts once Windows is shut down and "
            "started again.\n");
    }
    if (storStepsLeft) {
        flagSteps = 1;
    }
}

/* ---- the uninstall (record 26 section 6.5.8) ----------------------------- */

static int stor_rec_devices_any(void)
{
    char name[INST_ID_CHARS];
    char data[16];

    return stor_rec_enum("Devices", 0, name, sizeof(name), data,
                         sizeof(data)) != 0;
}

/* The OEM Name value of an INF copy (by its path or 8.3 alias) whose data
 * names the pack's INF which: 1 found (vname filled), 0 none, -1 the key
 * cannot be read. */
static int stor_oem_value(const char *path, int which, char *vname,
                          DWORD cap)
{
    char alias[INST_PATH_CHARS];
    char vdata[96];
    HKEY k;
    DWORD n;
    DWORD dn;
    DWORD type;
    DWORD vi;
    LONG r;

    n = GetShortPathNameA(path, alias, sizeof(alias));
    if (n == 0 || n >= sizeof(alias)) {
        alias[0] = '\0';
    }
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_OEM_NAME_KEY, 0,
                      KEY_QUERY_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    for (vi = 0;; vi++) {
        n = cap;
        dn = sizeof(vdata) - 1;
        r = RegEnumValueA(k, vi, vname, &n, NULL, &type, (LPBYTE)vdata, &dn);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS) {
            RegCloseKey(k);
            return -1;
        }
        vdata[type == REG_SZ && dn < sizeof(vdata) ? dn : 0] = '\0';
        if (inst_oem_name_ours(vname, path, alias) &&
            inst_eq(vdata, instStorFile[which].Name)) {
            RegCloseKey(k);
            return 1;
        }
    }
    RegCloseKey(k);
    vname[0] = '\0';
    return 0;
}

/* The owner's decision of 2026-10-20 (record 26 section 6.5.8 step 1):
 * every storage devnode, present or not, whose driver key names
 * USBSTOR.SYS or USBMPHLP.PDR and whose InfPath names exactly one copy of
 * the pack's own INF bytes, with its OEM Name value naming that INF, is
 * the installer's - first plugged after the install included - when the
 * copy is one the record holds, or PreExisting is clear; NUSB installed
 * (USB20U) adopts nothing. The copy is recorded first, then the device.
 * 1 done (or nothing to adopt); 0 a reading failed (the uninstall stops). */
static int stor_un_adopt(int record)
{
    HKEY e1;
    HKEY e2;
    HKEY e3;
    char n1[64];
    char n2[INST_ID_CHARS];
    char n3[INST_ID_CHARS];
    char id[INST_ID_CHARS * 2];
    char path[INST_PATH_CHARS];
    char text[64];
    char vname[INST_PATH_CHARS];
    char hex[65];
    char dv[64];
    DEVREC t;
    DWORD i;
    DWORD j;
    DWORD l;
    DWORD n;
    DWORD v = 0;
    LONG r;
    HKEY u;
    int bad = 0;
    int kind;
    int recorded;
    int beforeOk;
    int inBefore;
    int count = 0;
    int g;
    char bh[96];

    if (envNow.Nt) {
        return 0;
    }
    /* Only where this installer deployed the seven: its Source value is
     * there (written once a run put them in the package's copy, cleared to
     * 0 by a withdrawal, never deleted but with the record). A choice
     * alone is no evidence. */
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_STOR_KEY, 0, KEY_READ, &u);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    RegCloseKey(u);
    if (!reg_dword(HKEY_LOCAL_MACHINE, INST_STOR_KEY, "Published", &v) ||
        v != 1) {
        /* A placement whose Published write did not happen: a placed
         * copy still in place with its recorded bytes is the evidence
         * (record 26 section 6.5.8 step 1, draft 19). */
        g = stor_placed_live();
        if (g < 0) {
            return -1;
        }
        if (g == 0) {
            return 0;
        }
    }
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_STOR_USB20U_KEY, 0, KEY_READ,
                      &u);
    if (r == ERROR_SUCCESS) {
        RegCloseKey(u);
    }
    if (r != ERROR_FILE_NOT_FOUND) {
        if (record) {
            out("NUSB is installed (Uninstall\\USB20U): no USB storage device "
                "Windows bound later\nis taken as this installer's.\n");
        }
        return 0;
    }
    /* The copies of the pack's bytes that were there before the first
     * deployment (Before, whole when BeforeOk is 1): never this
     * installer's, nor the devices bound through them. */
    beforeOk = reg_dword(HKEY_LOCAL_MACHINE, INST_STOR_KEY, "BeforeOk", &v) &&
               v == 1;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum", 0, KEY_READ, &e1) !=
        ERROR_SUCCESS) {
        return -1;
    }
    for (i = 0; !bad; i++) {
        n = sizeof(n1);
        r = RegEnumKeyExA(e1, i, n1, &n, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS ||
            RegOpenKeyExA(e1, n1, 0, KEY_READ, &e2) != ERROR_SUCCESS) {
            bad = 1;
            break;
        }
        for (j = 0; !bad; j++) {
            n = sizeof(n2);
            r = RegEnumKeyExA(e2, j, n2, &n, NULL, NULL, NULL, NULL);
            if (r == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (r != ERROR_SUCCESS ||
                RegOpenKeyExA(e2, n2, 0, KEY_READ, &e3) != ERROR_SUCCESS) {
                bad = 1;
                break;
            }
            for (l = 0; !bad; l++) {
                n = sizeof(n3);
                r = RegEnumKeyExA(e3, l, n3, &n, NULL, NULL, NULL, NULL);
                if (r == ERROR_NO_MORE_ITEMS) {
                    break;
                }
                if (r != ERROR_SUCCESS) {
                    bad = 1;
                    break;
                }
                _snprintf(id, sizeof(id) - 1, "%s\\%s\\%s", n1, n2, n3);
                id[sizeof(id) - 1] = '\0';
                if (strlen(id) >= INST_ID_CHARS) {
                    continue;
                }
                memset(&t, 0, sizeof(t));
                inst_copy(t.Id, id, sizeof(t.Id));
                g = reg9x_value(id, "Driver", t.Driver, sizeof(t.Driver));
                if (g <= 0 || t.Driver[0] == '\0') {
                    continue;
                }
                driver_key_path(&t, path, sizeof(path));
                kind = INST_STOR_KIND_NONE;
                if (reg_text3(HKEY_LOCAL_MACHINE, path, "NTMPDriver", text,
                              sizeof(text)) > 0 &&
                    inst_eq(text, "USBSTOR.SYS")) {
                    kind = INST_STOR_KIND_BULK;
                } else if (reg_text3(HKEY_LOCAL_MACHINE, path, "PortDriver",
                                     text, sizeof(text)) > 0 &&
                           inst_eq(text, "USBMPHLP.PDR")) {
                    kind = INST_STOR_KIND_UNIT;
                }
                if (kind == INST_STOR_KIND_NONE ||
                    stor_rec_get("Devices", id, dv, sizeof(dv)) != 0) {
                    continue;
                }
                t.InfPathState = reg_text3(HKEY_LOCAL_MACHINE, path,
                                           "InfPath", t.InfPath,
                                           sizeof(t.InfPath));
                g = stor_infpath_copy(&t, &recorded);
                inBefore = g == 1 &&
                           stor_rec_get("Before", storLastCopy, bh,
                                        sizeof(bh)) != 0;
                if (!inst_stor_adoptable(1, beforeOk, inBefore, 0, g,
                                         recorded)) {
                    logonly("storage: %s not adopted (copy %d, recorded %d, "
                            "before %d/%d)\n", id, g, recorded, beforeOk,
                            inBefore);
                    continue;
                }
                /* The copy read again: still the pack's own bytes, or the
                 * evidence changed and nothing is taken. */
                if (stor_hash(storLastCopy, hex) <= 0 ||
                    !inst_eq(hex, instStorFile[storLastWhich].Sha256)) {
                    logonly("storage: %s's copy %s changed: not adopted\n",
                            id, storLastCopy);
                    continue;
                }
                if (!recorded &&
                    stor_oem_value(storLastCopy, storLastWhich, vname,
                                   sizeof(vname)) <= 0) {
                    logonly("storage: %s's copy %s has no OEM Name value "
                            "naming %s: not adopted\n", id, storLastCopy,
                            instStorFile[storLastWhich].Name);
                    continue;
                }
                count++;
                if (!record || opts.List) {
                    if (opts.List) {
                        out("(/list) %s would be taken as this installer's.\n",
                            id);
                    }
                    continue;
                }
                if (!recorded && !stor_rec_text("Infs", vname, hex)) {
                    bad = 1;
                    break;
                }
                out("%s: bound from this installer's copy of %s, taken as "
                    "this installer's.\n", id, instStorFile[storLastWhich].Name);
                if (!stor_rec_text("Devices", id, kind == INST_STOR_KIND_BULK ?
                                   "bulk" : "unit")) {
                    bad = 1;
                    break;
                }
            }
            RegCloseKey(e3);
        }
        RegCloseKey(e2);
    }
    RegCloseKey(e1);
    if (bad) {
        out("The device entries could not be read whole: nothing is "
            "removed.\n");
        flagFailed = 1;
        return -1;
    }
    return count;
}

/* The recorded devices, units then Bulk-Only devices, through the helper.
 * 1 every one gone; 0 stopped. */
static int stor_un_devices(void)
{
    char name[INST_ID_CHARS];
    char data[16];
    INST_DI_RESULT r;
    DEVREC d;
    DWORD i;
    int pass;
    int e;
    int v;
    int o;

    for (pass = 0; pass < 2; pass++) {
        for (i = 0;;) {
            e = stor_rec_enum("Devices", i, name, sizeof(name), data,
                              sizeof(data));
            if (e < 0) {
                out("The USB storage drivers' install record cannot be "
                    "read.\n");
                flagFailed = 1;
                return 0;
            }
            if (e == 0) {
                break;
            }
            if (inst_eq(data, pass == 0 ? "unit" : "bulk") == 0) {
                i++;
                continue;
            }
            if (enum9x_exists(name) == 0) {
                /* Gone with its parent, or by hand: not an error. */
                if (opts.List) {
                    i++;
                    continue;
                }
                if (!stor_rec_del("Devices", name)) {
                    return 0;
                }
                continue;
            }
            if (opts.List) {
                out("(/list) %s would be removed.\n", name);
                i++;
                continue;
            }
            if (!go_on()) {
                return 0;
            }
            memset(&d, 0, sizeof(d));
            inst_copy(d.Id, name, sizeof(d.Id));
            reg9x_value(name, "DeviceDesc", d.Desc, sizeof(d.Desc));
            out("Removing %s ...\n", d.Desc[0] ? d.Desc : d.Id);
            step(INST_STEP_BEGIN, "Removing %s", d.Desc[0] ? d.Desc : d.Id);
            if (helperState < 0) {
                step(INST_STEP_FAILED, "%s: the helper is not running", d.Id);
                flagSteps = 1;
                return 0;
            }
            v = helper_job(INST_DI_REMOVE, &d, NULL, 0, &r);
            o = inst_di_outcome(INST_DI_REMOVE, v);
            out("  %s%s%s.\n", inst_di_verdict_text(v), r.Error[0] ? ": " :
                "", r.Error);
            if (o == INST_DI_OUT_STEPS) {
                flagSteps = 1;
                return 0;
            }
            if (o == INST_DI_OUT_FAILED || o == INST_DI_OUT_REFUSED) {
                flagFailed = 1;
                return 0;
            }
            if (o == INST_DI_OUT_RESTART) {
                flagRestart = 1;
            }
            flagChanged = 1;
            if (enum9x_exists(name) != 0) {
                out("  %s is still recorded after its removal.\n", name);
                step(INST_STEP_FAILED, "%s: still recorded", d.Id);
                flagSteps = 1;
                return 0;
            }
            step(INST_STEP_DONE, "%s removed", d.Desc[0] ? d.Desc : d.Id);
            if (!stor_rec_del("Devices", name)) {
                return 0;
            }
        }
    }
    return 1;
}

/* The Enum entries, present or not, that use a file: a driver key's
 * NTMPDriver or PortDriver, or an Enum key's UpperFilters or LowerFilters,
 * naming it. The count, or -1 when the walk is not whole. */
/* Whether a filter value of the Enum key id names file: one string, or a
 * list of NUL-separated ones (REG_MULTI_SZ, or the binary form 98 SE may
 * keep it in). 1 names it, 0 not (or no such value), -1 unreadable. */
static int stor_filter_names(const char *id, const char *name,
                             const char *file)
{
    char path[INST_ID_CHARS + 16];
    char data[1024];
    HKEY k;
    DWORD type;
    DWORD n = sizeof(data) - 2;
    DWORD off;
    LONG r;

    inst_copy(path, "Enum\\", sizeof(path));
    if (strlen(path) + strlen(id) >= sizeof(path)) {
        return -1;
    }
    strcat(path, id);
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS) {
        return -1;
    }
    memset(data, 0, sizeof(data));
    r = RegQueryValueExA(k, name, NULL, &type, (LPBYTE)data, &n);
    RegCloseKey(k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    if (r != ERROR_SUCCESS || n >= sizeof(data) - 2 ||
        (type != REG_SZ && type != REG_MULTI_SZ && type != REG_BINARY)) {
        return -1;
    }
    data[n] = '\0';
    data[n + 1] = '\0';
    for (off = 0; off < n; off += (DWORD)strlen(data + off) + 1) {
        if (data[off] != '\0' &&
            inst_inf_names_file(data + off, strlen(data + off), file)) {
            return 1;
        }
    }
    return 0;
}

static int stor_users(const char *file)
{
    HKEY e1;
    HKEY e2;
    HKEY e3;
    char n1[64];
    char n2[INST_ID_CHARS];
    char n3[INST_ID_CHARS];
    char id[INST_ID_CHARS * 2];
    char path[INST_PATH_CHARS];
    char text[512];
    DEVREC t;
    DWORD i;
    DWORD j;
    DWORD l;
    DWORD n;
    LONG r;
    int users = 0;
    int bad = 0;
    int f;
    int g;
    static const char *const names[4] = {
        "NTMPDriver", "PortDriver", "UpperFilters", "LowerFilters"
    };

    /* Every reading whole, or the answer is "unknown" (-1): a file is
     * never deleted on a walk that stopped early. */
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum", 0, KEY_READ, &e1) !=
        ERROR_SUCCESS) {
        return -1;
    }
    for (i = 0; !bad; i++) {
        n = sizeof(n1);
        r = RegEnumKeyExA(e1, i, n1, &n, NULL, NULL, NULL, NULL);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS ||
            RegOpenKeyExA(e1, n1, 0, KEY_READ, &e2) != ERROR_SUCCESS) {
            bad = 1;
            break;
        }
        for (j = 0; !bad; j++) {
            n = sizeof(n2);
            r = RegEnumKeyExA(e2, j, n2, &n, NULL, NULL, NULL, NULL);
            if (r == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (r != ERROR_SUCCESS ||
                RegOpenKeyExA(e2, n2, 0, KEY_READ, &e3) != ERROR_SUCCESS) {
                bad = 1;
                break;
            }
            for (l = 0; !bad; l++) {
                n = sizeof(n3);
                r = RegEnumKeyExA(e3, l, n3, &n, NULL, NULL, NULL, NULL);
                if (r == ERROR_NO_MORE_ITEMS) {
                    break;
                }
                if (r != ERROR_SUCCESS) {
                    bad = 1;
                    break;
                }
                _snprintf(id, sizeof(id) - 1, "%s\\%s\\%s", n1, n2, n3);
                id[sizeof(id) - 1] = '\0';
                if (strlen(id) >= INST_ID_CHARS) {
                    bad = 1;
                    break;
                }
                for (f = 2; f < 4; f++) {
                    g = stor_filter_names(id, names[f], file);
                    if (g < 0) {
                        bad = 1;
                    } else if (g > 0) {
                        users++;
                    }
                }
                memset(&t, 0, sizeof(t));
                inst_copy(t.Id, id, sizeof(t.Id));
                g = reg9x_value(id, "Driver", t.Driver, sizeof(t.Driver));
                if (g < 0) {
                    bad = 1;
                    break;
                }
                if (g == 0 || t.Driver[0] == '\0') {
                    continue;
                }
                driver_key_path(&t, path, sizeof(path));
                {
                    HKEY dk;
                    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ,
                                      &dk);
                    if (r == ERROR_FILE_NOT_FOUND) {
                        /* A Driver value naming no driver key: no driver. */
                        continue;
                    }
                    if (r != ERROR_SUCCESS) {
                        bad = 1;
                        break;
                    }
                    RegCloseKey(dk);
                }
                for (f = 0; f < 2; f++) {
                    g = reg_text3(HKEY_LOCAL_MACHINE, path, names[f], text,
                                  sizeof(text));
                    if (g < 0) {
                        /* A driver key that will not open is a driver
                         * whose files are unknown. */
                        bad = 1;
                    } else if (g > 0 && inst_eq(text, file)) {
                        users++;
                    }
                }
            }
            RegCloseKey(e3);
        }
        RegCloseKey(e2);
    }
    RegCloseKey(e1);
    return bad ? -1 : users;
}

/* The exact OEM Name value the record holds, deleted: 1 gone (or never
 * there), 0 it could not be. */
static int stor_oem_forget(const char *name)
{
    HKEY k;
    LONG r;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_OEM_NAME_KEY, 0, KEY_SET_VALUE,
                      &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 1;
    }
    if (r != ERROR_SUCCESS) {
        return 0;
    }
    r = RegDeleteValueA(k, name);
    RegCloseKey(k);
    if (r == ERROR_SUCCESS) {
        out("Removed the setup engine's record of it (SetupX OEM Name).\n");
        return 1;
    }
    return r == ERROR_FILE_NOT_FOUND;
}

/* The recorded INF copies, then the recorded files (section 6.5.8 steps 3
 * and 4). 1 nothing left for a later run; 0 a removal failed (the record
 * keeps it, the restart asked for, or the failure said). */
static int stor_un_rest(void)
{
    char name[INST_PATH_CHARS];
    char data[96];
    char hex[65];
    char win[INST_PATH_CHARS];
    char rel[INST_PATH_CHARS];
    char pnf[INST_PATH_CHARS];
    DWORD i;
    int e;
    int left = 0;
    int sub;
    int pass;
    int found = 0;
    HKEY u;
    LONG r;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_STOR_USB20U_KEY, 0, KEY_READ,
                      &u);
    if (r == ERROR_SUCCESS) {
        RegCloseKey(u);
    }
    storUsb20u = r != ERROR_FILE_NOT_FOUND;
    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        return 0;
    }
    /* The INF copies, each with its .PNF and its one OEM Name value; then
     * the INFs placed in %windir%\INF (Placed, no OEM Name value). Every
     * copy of USBSTOR.INF first, then USBNTMAP.INF's, each only once no
     * copy of USBSTOR.INF is left where Windows finds it (record 26 section
     * 6.5.5). */
    for (pass = 0; pass < 2; pass++) {
        if (pass == 1 && !opts.List) {
            found = stor_inf_found(INST_STOR_IDX_USBSTOR_INF);
        }
        for (sub = 0; sub < 2; sub++) {
            const char *key = sub == 0 ? "Infs" : "Placed";
            for (i = 0;;) {
                int named;
                int h;
                e = stor_rec_enum(key, i, name, sizeof(name), data, sizeof(data));
                if (e < 0) {
                    flagFailed = 1;
                    return 0;
                }
                if (e == 0) {
                    break;
                }
                if (inst_stor_rm_late(NULL, data) != pass) {
                    i++;
                    continue;
                }
                h = stor_hash(name, hex);
                if (h > 0 && found != 0) {
                    if (found < 0) {
                        out("  %s: whether USBSTOR.INF is still in the INF "
                            "directory cannot be read; it\n  stays for now.\n",
                            name);
                        flagFailed = 1;
                        left = 1;
                        i++;
                        continue;
                    }
                    out("  %s stays: USBSTOR.INF is still in the INF "
                        "directory.\n", name);
                    if (!stor_rec_del(key, name)) {
                        return 0;
                    }
                    continue;
                }
                if (h == 0) {
                    /* The copy is gone (an earlier run deleted it, or by hand):
                     * its exact OEM Name value goes, then the record's. */
                    if (opts.List) {
                        out("(/list) %s is gone; %s.\n", name, sub == 0 ?
                            "its OEM Name value would be removed" :
                            "it would be forgotten");
                        i++;
                        continue;
                    }
                    if (sub == 0 && !stor_oem_forget(name)) {
                        out("  The setup engine's record of %s could not be "
                            "removed.\n", name);
                        flagFailed = 1;
                        left = 1;
                        i++;
                        continue;
                    }
                    if (!stor_rec_del(key, name)) {
                        return 0;
                    }
                    continue;
                }
                inst_copy(rel, name, sizeof(rel));
                {
                    char base[INST_PATH_CHARS];
                    WIN32_FIND_DATAA fd;
                    HANDLE fh;
                    join(base, sizeof(base), win, "INF\\");
                    if (inst_eqn(name, base, strlen(base))) {
                        inst_copy(rel, name + strlen(base), sizeof(rel));
                    }
                    fh = FindFirstFileA(name, &fd);
                    if (fh != INVALID_HANDLE_VALUE) {
                        char *s = strrchr(rel, '\\');
                        FindClose(fh);
                        if (s != NULL) {
                            s[1] = '\0';
                        } else {
                            rel[0] = '\0';
                        }
                        strncat(rel, fd.cFileName, sizeof(rel) - strlen(rel) - 1);
                    }
                }
                named = h > 0 ? s98_inf_named_9x(rel) : -1;
                if (h < 0 || named < 0) {
                    out("  %s: whether a device names it cannot be read; it stays "
                        "for now.\n", name);
                    flagFailed = 1;
                    left = 1;
                    i++;
                    continue;
                }
                if (!inst_eq(hex, data) || storUsb20u || named > 0) {
                    out("  %s stays: %s.\n", name, !inst_eq(hex, data) ?
                        "its bytes changed" : storUsb20u ?
                        "NUSB is installed (Uninstall\\USB20U)" :
                        "a device still names it");
                    if (opts.List) {
                        i++;
                        continue;
                    }
                    if (!stor_rec_del(key, name)) {
                        return 0;
                    }
                    continue;
                }
                if (opts.List) {
                    out("(/list) %s would be deleted.\n", name);
                    i++;
                    continue;
                }
                inst_copy(pnf, name, sizeof(pnf));
                if (strlen(pnf) > 4) {
                    strcpy(pnf + strlen(pnf) - 4, ".PNF");
                }
                if (file_state(pnf) < 0 ||
                    (file_state(pnf) > 0 && !delete_file(pnf)) ||
                    !delete_file(name)) {
                    out("  %s could not be deleted (error %lu): restart Windows, "
                        "then run the uninstall\n  again.\n", name,
                        (unsigned long)GetLastError());
                    flagRestart = 1;
                    flagSteps = 1;
                    left = 1;
                    i++;
                    continue;
                }
                out("Deleted %s.\n", name);
                flagChanged = 1;
                if (sub == 0 && !stor_oem_forget(name)) {
                    /* The record stays: the next run finds the copy gone and
                     * removes the value then. */
                    out("  The setup engine's record of it could not be removed "
                        "now.\n");
                    flagFailed = 1;
                    left = 1;
                    i++;
                    continue;
                }
                if (!stor_rec_del(key, name)) {
                    return 0;
                }
            }
        }
    }
    /* The files, last. */
    for (i = 0;;) {
        int h;
        int users;
        int why;
        const char *base;
        e = stor_rec_enum("Files", i, name, sizeof(name), data, sizeof(data));
        if (e < 0) {
            flagFailed = 1;
            return 0;
        }
        if (e == 0) {
            break;
        }
        h = stor_hash(name, hex);
        if (h < 0) {
            out("  %s cannot be read: it stays for now (restart Windows, then "
                "run the uninstall\n  again).\n", name);
            flagRestart = 1;
            flagSteps = 1;
            left = 1;
            i++;
            continue;
        }
        base = strrchr(name, '\\');
        base = base != NULL ? base + 1 : name;
        users = h > 0 ? stor_users(base) : 0;
        if (users < 0) {
            out("The device entries could not be read whole: %s stays for "
                "now.\n", name);
            flagFailed = 1;
            return 0;
        }
        why = inst_stor_remove(h != 0, h > 0 && inst_eq(hex, data),
                               storUsb20u, users);
        if (why != INST_STOR_RM_DELETE) {
            out("  %s: %s.\n", name, inst_stor_remove_text(why));
            if (opts.List) {
                i++;
                continue;
            }
            if (!stor_rec_del("Files", name)) {
                return 0;
            }
            continue;
        }
        if (opts.List) {
            out("(/list) %s would be deleted.\n", name);
            i++;
            continue;
        }
        if (!delete_file(name)) {
            out("  %s is still in use: restart Windows, then run the "
                "uninstall again.\n", name);
            flagRestart = 1;
            flagSteps = 1;
            left = 1;
            i++;
            continue;
        }
        out("Deleted %s.\n", name);
        flagChanged = 1;
        if (!stor_rec_del("Files", name)) {
            return 0;
        }
    }
    return !left;
}

/* The record deleted whole by the uninstall that finishes. */
static int stor_forget(void)
{
    LONG w;

    if (opts.List) {
        return 1;
    }
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_STOR_KEY "\\Files");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_STOR_KEY "\\Infs");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_STOR_KEY "\\Placed");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_STOR_KEY "\\Tools");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_STOR_KEY "\\Silent");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_STOR_KEY "\\Devices");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_STOR_KEY "\\Before");
    w = RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_STOR_KEY);
    if (w == ERROR_SUCCESS) {
        logonly("deleted HKLM\\%s\n", INST_STOR_KEY);
    } else if (w != ERROR_FILE_NOT_FOUND) {
        out("The USB storage drivers' install record could not be removed "
            "(error %ld):\ndelete HKLM\\%s with regedit.\n", (long)w,
            INST_STOR_KEY);
        flagFailed = 1;
        return 0;
    }
    return 1;
}

/* ---- elsewhere: the copy to the tools folder (record 26 section 5.3.1
 * case 1, task 43.43) ------------------------------------------------- */

static void stor_tools_copy(void)
{
    char dir[INST_PATH_CHARS];
    char dest[INST_PATH_CHARS];
    char left[256];
    int i;
    int there = 0;

    if (!storTools || !storToolsInclude || storPack <= 0 ||
        toolDir[0] == '\0') {
        return;
    }
    join(dir, sizeof(dir), toolDir, "NUSB33");
    if (opts.List) {
        out("(/list) the USB storage drivers' seven files would be copied to "
            "%s.\n", dir);
        return;
    }
    if (!CreateDirectoryA(dir, NULL) && file_state(dir) != -1) {
        out("%s could not be made: the USB storage drivers' files are not "
            "copied.\n", dir);
        return;
    }
    left[0] = '\0';
    for (i = 0; i < INST_STOR_COUNT; i++) {
        join(dest, sizeof(dest), dir, instStorFile[i].Name);
        /* A file of the name already there is never replaced, nor ever
         * recorded as this installer's. */
        if (file_state(dest) != 0 || stor_place_to(i, dest, "Tools")) {
            there++;
            continue;
        }
        if (left[0] != '\0') {
            strncat(left, ", ", sizeof(left) - strlen(left) - 1);
        }
        strncat(left, instStorFile[i].Name, sizeof(left) - strlen(left) - 1);
    }
    out("USB storage drivers for Windows 98 SE: %d of the seven files are in "
        "%s, for use by\nhand on Windows 98 SE; nothing installs them "
        "here.\n", there, dir);
    if (left[0] != '\0') {
        out("Not copied: %s.\n", left);
    }
}

/* The uninstall, on every system: each copy the Tools record names, by its
 * bytes, then the folder if it is empty. 1 nothing left for a later run. */
static int stor_tools_remove(void)
{
    char name[INST_PATH_CHARS];
    char data[96];
    char hex[65];
    char dir[INST_PATH_CHARS];
    DWORD i = 0;
    int e;
    int h;
    int left = 0;
    char *s;

    for (;;) {
        e = stor_rec_enum("Tools", i, name, sizeof(name), data, sizeof(data));
        if (e < 0) {
            flagFailed = 1;
            return 0;
        }
        if (e == 0) {
            break;
        }
        inst_copy(dir, name, sizeof(dir));
        s = strrchr(dir, '\\');
        if (s != NULL) {
            *s = '\0';
        }
        h = stor_hash(name, hex);
        if (h < 0) {
            out("  %s cannot be read: it stays for now.\n", name);
            flagRestart = 1;
            flagSteps = 1;
            left = 1;
            i++;
            continue;
        }
        if (opts.List) {
            out("(/list) %s %s.\n", name, h == 0 ? "is gone" :
                inst_eq(hex, data) ? "would be deleted" :
                "has changed and would stay");
            i++;
            continue;
        }
        if (h > 0 && !inst_eq(hex, data)) {
            out("  %s stays: it has changed since this installer copied it.\n",
                name);
        } else if (h > 0) {
            if (!delete_file(name)) {
                out("  %s could not be deleted: restart Windows, then run the "
                    "uninstall again.\n", name);
                flagRestart = 1;
                flagSteps = 1;
                left = 1;
                i++;
                continue;
            }
            out("Deleted %s.\n", name);
            flagChanged = 1;
        }
        if (!stor_rec_del("Tools", name)) {
            return 0;
        }
        /* The folder goes with its last file, whichever folder it is. */
        if (RemoveDirectoryA(dir)) {
            out("%s removed.\n", dir);
        }
    }
    return !left;
}

/* ---- the silent install with the storage row (record 26 section 6.5.12,
 * task 43.42 option A) ------------------------------------------------- */

/* 1 when XhciSilentInstall reads on after this run's writes, as the
 * driver reads it: the machine-wide value where there is one, else every
 * controller's own value. */
static int silent_gate_on(void)
{
    int v = inst_value_find("XhciSilentInstall");
    DWORD mwv = 0;
    DWORD now;
    int mw;
    int i;
    int any = 0;

    if (v < 0 || v >= INST_MAX_VALUES) {
        return 0;
    }
    mw = mw_value(v, &mwv);
    if (mw != 0) {
        return mw > 0 && mwv == 1;
    }
    /* Every controller xhci98 runs, whether or not this run reached it. */
    for (i = 0; i < devCount; i++) {
        if (!is_controller(&devs[i]) || !inst_eq(devs[i].Service, "xhci98")) {
            continue;
        }
        any = 1;
        /* Its driver key read directly: the device's Inf is -1 when the
         * USB drivers are out of this run. */
        {
            char path[INST_PATH_CHARS];
            if (devs[i].Driver[0] == '\0') {
                return 0;
            }
            driver_key_path(&devs[i], path, sizeof(path));
            if (!reg_dword(HKEY_LOCAL_MACHINE, path, instValues[v].Name,
                           &now) || now != 1) {
                return 0;
            }
        }
    }
    return any;
}

static int silent_taken(const char *name, void *ctx)
{
    HKEY k = (HKEY)ctx;
    DWORD t;
    DWORD n = 0;

    LONG r = RegQueryValueExA(k, name, NULL, &t, NULL, &n);

    if (r == ERROR_FILE_NOT_FOUND) {
        return 0;
    }
    return r == ERROR_SUCCESS || r == ERROR_MORE_DATA ? 1 : -1;
}

static void silent_hex(const unsigned char *e, char *out)
{
    static const char hx[] = "0123456789ABCDEF";
    int i;

    for (i = 0; i < 4; i++) {
        out[2 * i] = hx[(e[i] >> 4) & 0x0F];
        out[2 * i + 1] = hx[e[i] & 0x0F];
    }
    out[8] = '\0';
}

/* The Bulk-Only entries added to SilentClasses where none holds their
 * bytes, each recorded before it is written; never over a value. */
static void stor_silent_merge(void)
{
    HKEY k;
    LONG r;
    DWORD i;
    DWORD n;
    DWORD t;
    DWORD dn;
    char name[256];
    unsigned char data[16];
    unsigned char have[64][4];
    int valid[64];
    int nh = 0;
    int full = 0;
    int kept = 0;
    INST_U32 set;
    int e;
    char hex[9];

    if (!storCopyOk || opts.List || !stor_98se()) {
        return;
    }
    r = RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_STOR_SILENT_KEY, 0, NULL, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &k, NULL);
    if (r != ERROR_SUCCESS) {
        out("USB storage: the SilentClasses list could not be opened (error "
            "%ld): new USB disks\nmay still open Windows' wizard.\n", (long)r);
        return;
    }
    for (i = 0;; i++) {
        n = sizeof(name);
        dn = sizeof(data);
        r = RegEnumValueA(k, i, name, &n, NULL, &t, data, &dn);
        if (r == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (r != ERROR_SUCCESS && r != ERROR_MORE_DATA) {
            out("USB storage: the SilentClasses list could not be read whole: "
                "nothing is added.\n");
            RegCloseKey(k);
            return;
        }
        if (nh >= 64) {
            full = 1;
            continue;
        }
        valid[nh] = r == ERROR_SUCCESS && t == REG_BINARY && dn == 4;
        memcpy(have[nh], data, 4);
        if (valid[nh] && (data[0] & 0xC0) != 0) {
            kept++;
        }
        nh++;
    }
    if (full) {
        out("USB storage: the SilentClasses list is longer than this installer "
            "reads: nothing\nis added.\n");
        RegCloseKey(k);
        return;
    }
    set = inst_stor_silent_add((const unsigned char (*)[4])have, valid, nh);
    for (e = 0; e < INST_STOR_SILENT_COUNT; e++) {
        if ((set & (1UL << e)) == 0) {
            continue;
        }
        if (!inst_stor_silent_name(e, silent_taken, (void *)k, name,
                                   sizeof(name))) {
            out("USB storage: the SilentClasses names could not be read: "
                "nothing more is added.\n");
            break;
        }
        silent_hex(instStorSilent[e], hex);
        if (!stor_rec_text("Silent", name, hex)) {
            continue;
        }
        r = RegSetValueExA(k, name, 0, REG_BINARY, instStorSilent[e], 4);
        if (r != ERROR_SUCCESS) {
            out("USB storage: SilentClasses %s could not be written (error "
                "%ld).\n", name, (long)r);
            stor_rec_del("Silent", name);
            continue;
        }
        flagChanged = 1;
        kept++;
        out("USB storage: SilentClasses %s added (class 08, subclass %02X, "
            "protocol 50).\n", name, (unsigned)instStorSilent[e][2]);
    }
    RegCloseKey(k);
    if (kept > (int)32) {
        out("USB storage: SilentClasses holds more than the 32 entries the "
            "driver reads: one\nadded here may not be read.\n");
    }
}

/* The uninstall: each entry the Silent record names, deleted while it holds
 * the recorded bytes; the key and every other value stay. 1 done; 0 the
 * record or the list could not be read or written (the record kept). */
static int stor_silent_remove(void)
{
    char name[256];
    char data[96];
    char hex[9];
    unsigned char now[16];
    DWORD t;
    DWORD dn;
    DWORD i = 0;
    HKEY k = NULL;
    LONG r;
    int e;

    for (;;) {
        e = stor_rec_enum("Silent", i, name, sizeof(name), data, sizeof(data));
        if (e < 0) {
            flagFailed = 1;
            if (k != NULL) {
                RegCloseKey(k);
            }
            return 0;
        }
        if (e == 0) {
            break;
        }
        if (k == NULL) {
            r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_STOR_SILENT_KEY, 0,
                              KEY_QUERY_VALUE | KEY_SET_VALUE, &k);
            if (r == ERROR_FILE_NOT_FOUND) {
                k = NULL;
            } else if (r != ERROR_SUCCESS) {
                flagFailed = 1;
                return 0;
            }
        }
        dn = sizeof(now);
        r = k != NULL ? RegQueryValueExA(k, name, NULL, &t, now, &dn)
                      : ERROR_FILE_NOT_FOUND;
        if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND &&
            r != ERROR_MORE_DATA) {
            /* Unreadable: the evidence is kept for the next run. */
            out("  SilentClasses %s cannot be read: it stays for now.\n", name);
            flagFailed = 1;
            if (k != NULL) {
                RegCloseKey(k);
            }
            return 0;
        }
        if (r == ERROR_SUCCESS && t == REG_BINARY && dn == 4) {
            silent_hex(now, hex);
        } else {
            hex[0] = '\0';
        }
        if (opts.List) {
            out("(/list) SilentClasses %s %s.\n", name,
                r == ERROR_FILE_NOT_FOUND ? "is gone" :
                inst_eq(hex, data) ? "would be deleted" :
                "has changed and would stay");
            i++;
            continue;
        }
        if (r != ERROR_FILE_NOT_FOUND && inst_eq(hex, data)) {
            if (RegDeleteValueA(k, name) != ERROR_SUCCESS) {
                out("  SilentClasses %s could not be deleted.\n", name);
                flagFailed = 1;
                if (k != NULL) {
                    RegCloseKey(k);
                }
                return 0;
            }
            out("Removed SilentClasses %s.\n", name);
            flagChanged = 1;
        } else if (r != ERROR_FILE_NOT_FOUND) {
            out("  SilentClasses %s stays: it has changed since this installer "
                "added it.\n", name);
        }
        if (!stor_rec_del("Silent", name)) {
            if (k != NULL) {
                RegCloseKey(k);
            }
            return 0;
        }
    }
    if (k != NULL) {
        RegCloseKey(k);
    }
    return !flagFailed;
}

/* ---- the window's (record 26 section 5.3) ------------------------------- */

int inst_eng_stor_state(int *offered, int *def, char *note, size_t cap)
{
    if (storTools) {
        *offered = storPack > 0;
        *def = storToolsInclude;
        if (cap > 0) {
            if (storPack == 0) {
                inst_copy(note, "This download does not carry them.", cap);
            } else if (storPack < 0) {
                inst_copy(note, "The storage files in this download are not "
                          "the ones this release carries.", cap);
            } else {
                inst_copy(note, "For Windows 98 SE only: ticked here, the "
                          "files are copied to the tools folder, nothing "
                          "installs them.", cap);
            }
        }
        return 1;
    }
    *offered = storHere && storPack > 0;
    *def = storInclude;
    if (cap > 0) {
        note[0] = '\0';
        if (storHere) {
            stor_note(note, cap);
        }
    }
    return storHere;
}

void inst_eng_stor_choose(int include)
{
    if (storTools) {
        storToolsInclude = include && storPack > 0;
        logonly("storage: elsewhere, chosen %d\n", storToolsInclude);
        return;
    }
    if (!storHere || storPack <= 0) {
        storInclude = 0;
        return;
    }
    storInclude = include ? 1 : 0;
    logonly("storage: chosen %d\n", storInclude);
}

void inst_eng_stor_summary(char *out, size_t cap)
{
    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (storTools && storToolsInclude) {
        _snprintf(out, cap - 1, "USB storage drivers for Windows 98 SE: their "
                  "files copied to %s\\NUSB33 only; nothing installs them "
                  "here.", toolDir);
        out[cap - 1] = '\0';
        return;
    }
    if (!storHere || storPack <= 0) {
        return;
    }
    if (!storInclude) {
        inst_copy(out, "USB storage drivers for Windows 98 SE: left out.",
                  cap);
        return;
    }
    inst_copy(out, "USB storage drivers for Windows 98 SE: ", cap);
    strncat(out, stor_case_text(storPlanCase), cap - strlen(out) - 1);
    strncat(out, ".", cap - strlen(out) - 1);
}

/* Storage is taken care of on this machine: this run deployed it, or the
 * seven were found installed. */
static int stor_covered(void)
{
    return storCopyOk || (storHere && storPlanCase == INST_STOR_CASE_COMPLETE);
}

/* The final line's shutdown form for a UAS devnode at Code 2 before this
 * run's copies (record 26 section 6.5.5). */
static int stor_cold(void)
{
    return storUasWait && storCopyOk;
}

int inst_eng_s98_state(int *have, char *version, size_t cap)
{
    *have = haveS98;
    inst_copy(version, s98VersionText, cap);
    return s98Include;
}

void inst_eng_s98_choose(int include)
{
    /* Part of the Thunderbolt support (owner, 2026-10-19). */
    s98_apply(include && haveCm && tbInclude);
    logonly("stor98: chosen %d\n", s98Include);
}

int inst_eng_s98_row(int i, INST_S98ROW *row)
{
    char now[64];
    int me = !envNow.Nt && envNow.Minor == 90;

    memset(row, 0, sizeof(*row));
    if (i < 0 || i >= INST_S98_COUNT || !inst_s98_applies(i, envNow.Nt)) {
        return 0;
    }
    row->Value = i;
    row->Asked = instS98Values[i].Asked;
    row->Name = envNow.Nt ? instS98Values[i].KeyNt : instS98Values[i].Name9x;
    row->State = s98_read(i, now, sizeof(now));
    inst_copy(row->Now, row->State > 0 ? now : "", sizeof(row->Now));
    inst_s98_now_text(i, envNow.Nt, me, row->State,
                      row->State > 0 ? now : NULL, row->NowText,
                      sizeof(row->NowText));
    inst_s98_default_text(i, envNow.Nt, me, row->Default,
                          sizeof(row->Default));
    return 1;
}

const char *inst_eng_s98_choose_value(int i, int set, const char *text)
{
    char canon[32];
    const char *why = NULL;

    if (i < 0 || i >= INST_S98_COUNT) {
        return "no such setting";
    }
    if (!set || !inst_s98_applies(i, envNow.Nt)) {
        s98Set[i] = 0;
        return NULL;
    }
    if (!inst_s98_check(i, envNow.Nt, text, canon, sizeof(canon), &why)) {
        return why;
    }
    s98Set[i] = 1;
    inst_copy(s98Val[i], canon, sizeof(s98Val[i]));
    return NULL;
}

int inst_eng_s98_file_update(void)
{
    return !envNow.Nt && s98Include && s98FileUpd;
}

void inst_eng_s98_file_ok(int ok)
{
    s98FileOk = ok != 0;
    logonly("stor98: file update confirmed %d\n", s98FileOk);
}

const char *inst_eng_tb_name(void)
{
    return tbNhiPresent ? tbNhiName : "";
}

int inst_eng_shutdown(void)
{
    return (inst_s98_cold_start(envNow.Nt) && s98Cold) || stor_cold();
}

int inst_eng_s98_left(void)
{
    return s98Left;
}

/* ======================================================================== */
/* The usage                                                                 */
/* ======================================================================== */

void inst_eng_usage(void)
{
    out(
"Usage: INSTALL.BAT /console [switches]\n"
"   (or %s /dir release-... [switches])\n"
"\n"
"  (none)        install or update the package's drivers, after one question\n"
"  /list         show what would be done; change nothing\n"
"  /yes          ask nothing: go on, keep every value /set does not name\n"
"  /defaults     ask no option question; keep every value\n"
"  /set Name=Value  set one of the driver's options (repeatable)\n"
"  /to path      install the xHCI98 tools in that directory without asking\n"
"                (default C:\\XHCI98, or where an earlier install put them)\n"
"  /force        reinstall this release, or install it over a newer one\n"
"  /thunderbolt  install the Thunderbolt 3 / USB4 support without asking\n"
"  /cmonly       install the Thunderbolt 3 / USB4 connection manager alone,\n"
"                without the USB drivers (on 98 SE and ME with a warning)\n"
"  /usb          Windows 8 and 8.1: install the USB drivers too (untested)\n"
"  /stor98       with /thunderbolt: install the bundled Stor98 unasked\n"
"                (the default with /thunderbolt /yes)\n"
"  /nostor98     leave the bundled Stor98 out of a /thunderbolt run\n"
"  /nouas        leave the UAS disk driver (xhciuas.sys) out of this run\n"
"  /storage      Windows 98 SE: install the USB storage drivers (NUSB 3.3's);\n"
"                elsewhere copy their files to the tools folder only\n"
"  /nostorage    leave the USB storage drivers out of this run\n"
"  /notray       do not start the xHCI98 USB Tree at login (and stop doing so)\n"
"  /restage      Vista and 7: replace a stored build with the same DriverVer\n"
"  /uninstall    remove the drivers (asks for the word REMOVE)\n"
"  /debug        (INSTALL.BAT) the debug- directory, for troubleshooting\n"
"  /nopause      do not wait for Enter at the end\n"
"  /log path     write the log there (default XHCIINST.LOG on the system\n"
"                drive)\n"
"  /devices      show the connected Thunderbolt and USB4 devices\n"
"  /eject [N]    remove one of them safely (/eject id=<instance id> for\n"
"                scripts)\n"
"\n"
"INSTALL.BAT with no /console opens the installer's window instead.\n"
"Exit codes: 0 done, nothing needed a change; 1 refused or cancelled, nothing\n"
"changed; 2 failed part-way (see the log); 3 done, restart needed; 4 done,\n"
"steps left for you.\n",
        INST_MY_NAME);
    out_flush();
}

/* ======================================================================== */
/* The self-test's screens (owner, 2026-10-16)                               */
/* ======================================================================== */

/* Every screen an interactive or listing run prints, rendered through the
 * real functions with made-up devices and answers, and measured as printed
 * (out_emit): no line may be wider than INST_COLUMNS. Run twice: with
 * names and paths as long as they come in practice, when the fixed text
 * itself must need no wrap at all; and with names, ids and paths at their
 * longest, when the wrap must hold every line to the width. Nothing here
 * reads a device or writes the registry or a file. XHCIINST_SCREENS=1
 * shows the screens. */

static const char stInfXhci[] =
    "[Version]\r\nSignature=\"$Windows NT$\"\r\nProvider=%P%\r\n"
    "DriverVer=10/16/2026,3.0.0.0\r\n"
    "[Manufacturer]\r\n%P%=M,NTx86,NTamd64\r\n"
    "[M]\r\n%D%=I,PCI\\CC_0C0330\r\n%D%=I,USB\\XHCI98_ROOT_HUB\r\n"
    "[M.NTx86]\r\n%D%=I,PCI\\CC_0C0330\r\n%D%=I,USB\\XHCI98_ROOT_HUB\r\n"
    "[M.NTamd64]\r\n%D%=I,PCI\\CC_0C0330\r\n%D%=I,USB\\XHCI98_ROOT_HUB\r\n"
    "[I.AddReg]\r\n"
    "HKR,,XhciIntelPortSwitch,0x00010003,0\r\n"
    "HKR,,XhciInterruptMode,0x00010003,0\r\n"
    "HKR,,XhciSilentInstall,0x00010003,0\r\n"
    "HKR,,XhciForceBulkOnly,0x00010003,0\r\n"
    "[Strings]\r\nP=\"Yeo Kheng Meng\"\r\nD=\"x\"\r\n";

static const char stInfCm[] =
    "[Version]\r\nSignature=\"$Windows NT$\"\r\nProvider=%P%\r\n"
    "DriverVer=10/16/2026,3.0.0.0\r\n"
    "[Manufacturer]\r\n%P%=M,NTx86,NTamd64\r\n"
    "[M]\r\n%D%=I,PCI\\VEN_8086&DEV_15EB\r\n"
    "[M.NTx86]\r\n%D%=I,PCI\\VEN_8086&DEV_15EB\r\n"
    "[M.NTamd64]\r\n%D%=I,PCI\\VEN_8086&DEV_15EB\r\n"
    "[I.AddReg]\r\n"
    "HKR,,XhciCmMode,0x00010003,1\r\n"
    "HKR,,XhciCmApprove,0x00010003,1\r\n"
    "HKR,,XhciCmSafeEject,0x00010003,1\r\n"
    "[Strings]\r\nP=\"Yeo Kheng Meng\"\r\nD=\"x\"\r\n";

static void st_dev(DEVREC *d, const char *id, const char *desc,
                   const char *service, const char *hw, const char *driver,
                   int major, int minor)
{
    size_t n;

    memset(d, 0, sizeof(*d));
    inst_copy(d->Id, id, sizeof(d->Id));
    inst_copy(d->Desc, desc, sizeof(d->Desc));
    inst_copy(d->Service, service, sizeof(d->Service));
    inst_copy(d->Driver, driver, sizeof(d->Driver));
    /* hw: the ids, '|' between them, made a multi-string. */
    n = strlen(hw);
    if (n + 2 <= sizeof(d->Hw)) {
        size_t i;
        memcpy(d->Hw, hw, n + 1);
        for (i = 0; i < n; i++) {
            if (d->Hw[i] == '|') {
                d->Hw[i] = '\0';
            }
        }
        d->Hw[n + 1] = '\0';
        d->HwBytes = (DWORD)(n + 2);
    }
    d->Present = 1;
    d->Status = INST_DN_STARTED;
    if (major != 0) {
        d->VersionState = INST_VS_HAVE;
        d->Version[0] = (INST_U32)major;
        d->Version[1] = (INST_U32)minor;
        _snprintf(d->VersionText, sizeof(d->VersionText) - 1, "%d.%d.0.0",
                  major, minor);
    }
}

static void st_put32(unsigned char *b, size_t off, INST_U32 v)
{
    b[off] = (unsigned char)(v & 0xFFUL);
    b[off + 1] = (unsigned char)((v >> 8) & 0xFFUL);
    b[off + 2] = (unsigned char)((v >> 16) & 0xFFUL);
    b[off + 3] = (unsigned char)((v >> 24) & 0xFFUL);
}

static const char stLongName[] =
    "xHCI98 USB 3.x eXtensible Host Controller with a very long name ";

/* A run of the screens; longest: names, ids and paths at their longest. */
static void st_screens_once(int longest)
{
    /* The Thunderbolt question answered Y: Enter gives N since record 23
     * revision 23, and the connection manager's questions follow a Y. */
    static const char *const nt1[] = {
        "y", "", "", "9", "", "", "", "", "C:\\WINDOWS\\X", "", "n", NULL
    };
    static const char *const x9[] = {
        "y", "", "", "", "", "", "", "", "", "C:\\Program Files\\xHCI98", "",
        "y", "n", NULL
    };
    static const char *const un[] = { "REMOVE", NULL };
    static const char *const ej[] = { "1", "n", NULL };
    char desc[3][128];
    char id[3][INST_ID_CHARS];
    char dir[INST_PATH_CHARS];
    unsigned char *e;
    int i;
    int k;

    if (longest) {
        for (k = 0; k < 3; k++) {
            memset(desc[k], 'D', sizeof(desc[k]) - 1);
            desc[k][sizeof(desc[k]) - 1] = '\0';
            memcpy(desc[k], stLongName, strlen(stLongName));
            memset(id[k], 'I', sizeof(id[k]) - 1);
            id[k][sizeof(id[k]) - 1] = '\0';
        }
        memcpy(id[0], "PCI\\VEN_8086&DEV_8C31&SUBSYS_", 29);
        memcpy(id[2], "PCI\\VEN_8086&DEV_15EB&SUBSYS_", 29);
        memset(dir, 'P', 99);
        memcpy(dir, "C:\\", 3);
        dir[99] = '\0';
    } else {
        inst_copy(desc[0], "xHCI98 USB 3.x eXtensible Host Controller",
                  sizeof(desc[0]));
        inst_copy(desc[1], "xHCI98 USB 3.x Root Hub", sizeof(desc[1]));
        inst_copy(desc[2], "xHCI98 Thunderbolt 3 Controller", sizeof(desc[2]));
        inst_copy(id[0], "PCI\\VEN_8086&DEV_8C31&SUBSYS_05BE1028&REV_04\\"
                  "3&11583659&0&A0", sizeof(id[0]));
        inst_copy(id[1], "USB\\XHCI98_ROOT_HUB\\4&2C4F2A1B&0", sizeof(id[1]));
        inst_copy(id[2], "PCI\\VEN_8086&DEV_15EB&SUBSYS_22BE17AA&REV_06\\"
                  "4&1E7B9F5C&0&00E0", sizeof(id[2]));
        inst_copy(dir, "C:\\XHCI98", sizeof(dir));
    }
    /* The package and the devices: the controller at 2.4 (an update), its
     * root hub, a Thunderbolt controller with no driver (a new install). */
    inst_copy(root, longest ? dir : "C:\\XHCI98PKG\\", sizeof(root));
    infText[INST_INF_XHCI] = (char *)stInfXhci;
    infText[INST_INF_UAS] = NULL;
    infText[INST_INF_CM] = (char *)stInfCm;
    infText[INST_INF_CMF] = NULL;
    haveCm = 1;
    inst_ver_parse("3.0.0.0", pkgVersion);
    devCount = 3;
    st_dev(&devs[0], id[0], desc[0], "xhci98",
           "PCI\\VEN_8086&DEV_8C31&CC_0C0330|PCI\\CC_0C0330",
           "{36FC9E60}\\0001", 2, 4);
    st_dev(&devs[1], id[1], desc[1], "xhci98", "USB\\XHCI98_ROOT_HUB",
           "{36FC9E60}\\0002", 2, 4);
    st_dev(&devs[2], id[2], desc[2], "", "PCI\\VEN_8086&DEV_15EB", "", 0, 0);
    devs[2].Problem = 28;

    for (k = 0; k < 2; k++) {
        memset(&opts, 0, sizeof(opts));
        envNow.Nt = k == 0;
        envNow.Major = k == 0 ? 5 : 4;
        envNow.Minor = k == 0 ? 1 : 10;
        envNow.Arch = INST_MY_ARCH;
        load_ids();
        inst_copy(oldToolDir, longest ? dir : "", sizeof(oldToolDir));
        /* 98 SE: the USB storage drivers' row with some of the seven found,
         * one of them other, and NUSB's entry (record 26 section 6.5). */
        storHere = k == 1;
        storPack = 1;
        for (i = 0; i < INST_STOR_COUNT; i++) {
            storSt[i] = longest ? (i % 2 ? INST_STOR_OTHER : INST_STOR_SAME) :
                        INST_STOR_ABSENT;
        }
        storSt[0] = INST_STOR_SAME;
        storSt[INST_STOR_COUNT - 1] = longest ? INST_STOR_ABSENT :
                                                INST_STOR_OTHER;
        storUsb20u = 1;
        storSource = 0;
        storPlanCase = inst_stor_case(storSt, 0);
        storDefault = inst_stor_default(0, 0, storSt);
        storInclude = storDefault;

        /* /list */
        opts.List = 1;
        out("==== %s /list\n", k == 0 ? "NT" : "98");
        tb_question();
        stor_question();
        print_plan();
        stor_print_plan();
        ask_values();
        choose_dir();
        warn_uninstall();

        /* An interactive install, answered from a script. */
        memset(&opts, 0, sizeof(opts));
        stInput = k == 0 ? nt1 : x9;
        out("==== %s install\n", k == 0 ? "NT" : "98");
        tb_question();
        stor_question();
        print_plan();
        stor_print_plan();
        if (k == 1) {
            stor_say_other(storSt);
        }
        input_warning();
        ask_values();
        choose_dir();
        confirm(inst_confirm_text(1, 1, envNow.Nt, 0));
        if (k == 1) {
            devs[2].Action = INST_ACT_INSTALL;
            i = steps_9x(dir);
            tb_later_note(dir);
            open_device_manager(i);
            uninstall_steps_9x();
        }
        stInput = un;
        warn_uninstall();
    }

    /* The connection manager alone (record 23 section 4.5): Windows 8.1,
     * the USB drivers out of the run, the controller's installed xhci98
     * left as it is, the Thunderbolt controller a new install; then
     * /cmonly on 7, and the uninstall's warning on such a machine. */
    for (k = 0; k < 2; k++) {
        memset(&opts, 0, sizeof(opts));
        envNow.Nt = 1;
        envNow.Major = 6;
        envNow.Minor = k == 0 ? 3 : 1;
        osKind = k == 0 ? INST_OS_CMONLY : INST_OS_FULL;
        opts.CmOnly = k == 1;
        infText[INST_INF_XHCI] = (char *)stInfXhci;
        load_ids();
        out("==== %s, the connection manager alone\n", k == 0 ? "8.1" : "7");
        usb_decide();
        devs[0].Inf = -1;
        devs[1].Inf = -1;
        devs[2].Inf = INST_INF_CM;
        devs[2].Action = INST_ACT_INSTALL;
        stInput = nt1;
        tb_question();
        print_plan();
        for (i = -1; i <= 1; i++) {
            pkgCmSigned = i;
            testsigning_note_cm();
        }
        pkgCmSigned = -1;
        testSigningConfirmed = 0;
        stCmOnly = 1;
        stInput = un;
        warn_uninstall();
        stCmOnly = 0;
        usb_apply(1);
    }
    osKind = INST_OS_FULL;
    stInput = NULL;
    infText[INST_INF_XHCI] = NULL;
    infText[INST_INF_CM] = NULL;
    envNow.Nt = 1;

    /* /devices and /eject: a Thunderbolt 3 controller, its port, a disk
     * behind it with one function below. */
    out("==== /devices and /eject\n");
    tbnCount = 4;
    memset(tbn, 0, 4 * sizeof(TBNODE));
    st_dev(&tbn[0].R, id[2], desc[2], "xhcicm", "PCI\\VEN_8086&DEV_15EB",
           "", 3, 0);
    tbn[0].IsNhi = 1;
    tbn[0].Parent = -1;
    st_dev(&tbn[1].R, id[0], longest ? desc[0] : "Intel(R) 100 Series PCI "
           "Express Root Port #9 - A118", "pci", "PCI\\VEN_8086&DEV_15EA", "",
           0, 0);
    tbn[1].Verified = 1;
    tbn[1].Candidate = 1;
    tbn[1].Parent = 0;
    st_dev(&tbn[2].R, id[1], longest ? desc[1] : "Samsung SSD 970 EVO Plus "
           "1TB NVMe Controller", "nvme98", "PCI\\VEN_144D&DEV_A808&CC_0108",
           "", 0, 0);
    tbn[2].Parent = 1;
    tbn[2].UnitOf = 2;
    tbn[2].Number = 1;
    tbn[2].HasCaps = 1;
    tbn[2].Caps = INST_CM_DEVCAP_REMOVABLE;
    inst_copy(tbn[2].Shown, "nvme98", sizeof(tbn[2].Shown));
    st_dev(&tbn[3].R, "SCSI\\DISK&VEN_NVME&PROD_SAMSUNG\\5&1", longest ?
           desc[2] : "Samsung SSD 970 EVO Plus 1TB", "disk", "SCSI\\DISK",
           "", 0, 0);
    tbn[3].Parent = 2;
    tbn[3].UnitOf = 2;
    tbhCount = 1;
    memset(&tbh[0], 0, sizeof(tbh[0]));
    tbh[0].Node = 0;
    tbh[0].Valid = 1;
    tbh[0].Confirmed = 1;
    st_put32(tbh[0].Rec, CM_ST_O_CM, CM_ST_CM_ICM);
    st_put32(tbh[0].Rec, CM_ST_O_SECURITY, 1);
    st_put32(tbh[0].Rec, CM_ST_O_GENERATION, 12);
    st_put32(tbh[0].Rec, CM_ST_O_PORT_COUNT, 1);
    st_put32(tbh[0].Rec, CM_ST_O_PORT_ACTIVE, 1);
    st_put32(tbh[0].Rec, CM_ST_O_PORTS, 0x0500UL);
    st_put32(tbh[0].Rec, CM_ST_O_ROUTER_COUNT, 1);
    e = tbh[0].Rec + CM_ST_HDR_BYTES;
    st_put32(e, CM_ST_E_KIND, CM_ST_KIND_ICM);
    st_put32(e, CM_ST_E_PLACE, 0x0101UL);
    st_put32(e, CM_ST_E_APPROVAL, CM_ST_APPR_APPROVED);
    st_put32(e, CM_ST_E_IDS, 0x00038086UL);
    st_put32(e, CM_ST_E_GEN, CM_ST_GEN_TBT3);
    st_put32(e, CM_ST_E_LANES, CM_ST_LANES_BONDED);
    st_put32(e, CM_ST_E_TUNNELS, 0x001UL);
    st_put32(e, CM_ST_E_BRIDGE, 0x0500UL);
    st_put32(e, CM_ST_E_SHORT_ID, 0x1A2BUL);
    memset(e + CM_ST_E_VENDOR, longest ? 'V' : 0, CM_ST_NAME_BYTES - 1);
    memset(e + CM_ST_E_MODEL, longest ? 'M' : 0, CM_ST_NAME_BYTES - 1);
    if (!longest) {
        memcpy(e + CM_ST_E_VENDOR, "Lenovo", 6);
        memcpy(e + CM_ST_E_MODEL, "ThinkPad Thunderbolt 3 Dock", 27);
    }
    tbUnits = 1;
    tbnBad = 0;
    tbChanging = 0;
    tb_print();
    stInput = ej;
    {
        INST_U32 n;
        if (eject_choose(&n)) {
            eject_confirm();
        }
    }
    stInput = NULL;
    for (i = 0; i < 6; i++) {
        out("%s\n", inst_unplug_text(i < 4 ? i : 0));
    }
    out("%s\n", inst_not_removable_text());
    tbnCount = 0;
    tbhCount = 0;

    /* The summary and the usage. */
    out("==== the summary\n");
    for (i = 0; i < 3; i++) {
        out("\n%s\n", inst_final_text(i));
    }
    out("\nExit code 3. The log is %s.\n", longest ? dir : "C:\\XHCIINST.LOG");
    inst_eng_usage();
}

/* 1 when every screen kept to the width, and the ordinary pass needed no
 * wrap. */
/* ---- the screens' own front end ------------------------------------------ */

/* The console's form, measured: the word wrap, the column count of every
 * line as printed, and the scripted answers echoed as a console shows
 * them. The question kinds asked are recorded in order. */
static INST_WRAP stWrap;
static size_t stWidest;     /* the widest line the screens printed */
static size_t stCol;
static int stLongLines;     /* lines wider than INST_COLUMNS */
#define INST_ST_ASKS 64
static int stAsked[INST_ST_ASKS];
static int stAskedCount;

static void st_emit(const char *text)
{
    const char *p;

    for (p = text; *p != '\0'; p++) {
        if (*p == '\n') {
            stCol = 0;
        } else if (*p != '\r') {
            stCol++;
            if (stCol > stWidest) {
                stWidest = stCol;
            }
            if (stCol == INST_COLUMNS + 1) {
                stLongLines++;
            }
        }
    }
    if (stShow) {
        fputs(text, stdout);
        fflush(stdout);
    }
}

static void st_line(void *ctx, const char *text)
{
    char wrapped[16384];

    (void)ctx;
    if (text[0] == '\0') {
        inst_wrap_flush(&stWrap, wrapped, sizeof(wrapped));
    } else {
        inst_wrap_feed(&stWrap, text, wrapped, sizeof(wrapped));
    }
    st_emit(wrapped);
}

static int st_ask(void *ctx, const INST_ASK *q, char *answer, size_t cap)
{
    char wrapped[INST_LINE_CHARS + 128];

    if (q->Text != NULL) {
        st_line(ctx, q->Text);
    }
    inst_wrap_flush(&stWrap, wrapped, sizeof(wrapped));
    st_emit(wrapped);
    inst_wrap_input(&stWrap);
    if (!q->Retry && stAskedCount < INST_ST_ASKS) {
        stAsked[stAskedCount++] = q->Kind;
    }
    if (stInput == NULL || *stInput == NULL) {
        answer[0] = '\0';
        st_emit("\n");
        return 0;
    }
    inst_copy(answer, *stInput++, cap);
    st_emit(answer);
    st_emit("\n");
    return 1;
}

/* 1 when every screen kept to the width, the ordinary pass needed no
 * wrap, and the questions came in record 23's order (record 26 section
 * 11: the two front ends ask through one sequence). */
static int selftest_screens(void)
{
    static const int order[] = {
        /* NT /list: nothing asked; NT install: the Thunderbolt question,
         * the port switchover, the interrupt mode, the forced Bulk-Only,
         * the connection manager's three, the directory, the confirmation,
         * the word. */
        INST_Q_TB, INST_Q_VALUE, INST_Q_VALUE, INST_Q_VALUE, INST_Q_VALUE,
        INST_Q_VALUE, INST_Q_VALUE, INST_Q_DIR, INST_Q_CONFIRM, INST_Q_REMOVE
    };
    INST_ENV saved = envNow;
    INST_FRONT savedFront = front;
    int ok = 1;
    INST_U32 wrapsPlain;
    int pass;
    int i;

    devs = (DEVREC *)calloc(INST_MAX_DEVS, sizeof(DEVREC));
    tbn = (TBNODE *)calloc(8, sizeof(TBNODE));
    if (devs == NULL || tbn == NULL) {
        return 0;
    }
    memset(&front, 0, sizeof(front));
    front.Line = st_line;
    front.Ask = st_ask;
    stScreens = 1;
    stShow = getenv("XHCIINST_SCREENS") != NULL;
    for (pass = 0; pass < 2; pass++) {
        inst_wrap_init(&stWrap);
        stWidest = 0;
        stCol = 0;
        stLongLines = 0;
        stAskedCount = 0;
        st_screens_once(pass);
        wrapsPlain = stWrap.Wraps;
        if (stLongLines != 0 || stWidest > INST_COLUMNS) {
            ok = 0;
        }
        if (stShow || stLongLines != 0) {
            fprintf(stderr, "screens, %s: widest %lu, %d lines over %d, %lu "
                    "wrapped by the console\n", pass ? "longest" : "ordinary",
                    (unsigned long)stWidest, stLongLines, INST_COLUMNS,
                    (unsigned long)wrapsPlain);
        }
        if (pass == 0 && wrapsPlain != 0) {
            fprintf(stderr, "screens, ordinary: %lu lines needed the "
                    "console's wrap\n", (unsigned long)wrapsPlain);
            ok = 0;
        }
        if (pass == 0) {
            int same = stAskedCount >= (int)(sizeof(order) / sizeof(order[0]));
            for (i = 0; same && i < (int)(sizeof(order) / sizeof(order[0]));
                 i++) {
                same = stAsked[i] == order[i];
            }
            if (!same) {
                fprintf(stderr, "screens: the questions came in another "
                        "order (%d asked)\n", stAskedCount);
                ok = 0;
            }
        }
    }
    stScreens = 0;
    storHere = 0;
    storInclude = 0;
    envNow = saved;
    front = savedFront;
    free(devs);
    free(tbn);
    devs = NULL;
    tbn = NULL;
    return ok;
}

/* ======================================================================== */
/* The installer's own files and the uninstall entry (record 26 6.2, 9.1)   */
/* ======================================================================== */

/* The installer's files of this architecture, from the package's
 * tools\xhciinst\: the window, the console tool, and on 98 SE and ME the
 * helper. */
static int own_names(const char **names)
{
    int n = 0;

    names[n++] = inst_self_file(INST_MY_ARCH, INST_OWN_WINDOW);
    names[n++] = inst_self_file(INST_MY_ARCH, INST_OWN_CONSOLE);
    if (!envNow.Nt) {
        names[n++] = INST_DI_NAME;
    }
    return n;
}

/* One file put in place by the USB tree tool's route (record 23 section
 * 9.5): written as .TMP and compared, then a replacing rename on NT, the
 * .BAK dance on 98 SE and ME. 1 done, 0 failed (said), 2 in use (said). */
static int own_put(const char *from, const char *to)
{
    char tmp[INST_PATH_CHARS];
    char bak[INST_PATH_CHARS];
    size_t n;
    PFN_MOVEEX moveEx = move_ex();
    int had = file_exists(to, NULL);

    inst_copy(tmp, to, sizeof(tmp));
    inst_copy(bak, to, sizeof(bak));
    n = strlen(to);
    if (n < 4) {
        return 0;
    }
    strcpy(tmp + n - 4, ".TMP");
    strcpy(bak + n - 4, ".BAK");
    SetFileAttributesA(tmp, FILE_ATTRIBUTE_NORMAL);
    if (!CopyFileA(from, tmp, FALSE) || !same_file(from, tmp)) {
        out("%s could not be copied to %s (error %lu).\n", from, tmp,
            (unsigned long)GetLastError());
        delete_file(tmp);
        return 0;
    }
    SetFileAttributesA(tmp, FILE_ATTRIBUTE_NORMAL);
    if (!had) {
        if (MoveFileA(tmp, to)) {
            return 1;
        }
    } else if (envNow.Nt) {
        if (moveEx != NULL && moveEx(tmp, to, MOVEFILE_REPLACE_EXISTING)) {
            return 1;
        }
    } else if (file_exists(bak, NULL) && !delete_file(bak)) {
        /* An old .BAK in the way: nothing is replaced this time. */
    } else if (MoveFileA(to, bak)) {
        if (MoveFileA(tmp, to)) {
            delete_file(bak);
            return 1;
        }
        MoveFileA(bak, to);
    }
    delete_file(tmp);
    if (!tray_free(to)) {
        out("%s is in use, so it was not updated: close it and run the "
            "installer again.\n", to);
        return 2;
    }
    out("%s could not be put in place (error %lu).\n", to,
        (unsigned long)GetLastError());
    return 0;
}

/* The installer's own files in the install directory (record 26 section
 * 6.2), so the Add/Remove Programs entry has a program to run once the
 * package is gone: outside every XHCIINST.LST, copied when missing or not
 * the package's bytes, never left for a restart. */
static void own_install(void)
{
    const char *names[3];
    char from[INST_PATH_CHARS];
    char to[INST_PATH_CHARS];
    int n;
    int i;
    int r;

    if (opts.List || toolDir[0] == '\0' || stScreens) {
        return;
    }
    n = own_names(names);
    for (i = 0; i < n; i++) {
        join(from, sizeof(from), root, "tools\\xhciinst");
        join(from, sizeof(from), from, names[i]);
        join(to, sizeof(to), toolDir, names[i]);
        if (!file_exists(from, NULL)) {
            logonly("own files: the package has no %s\n", from);
            continue;
        }
        if (same_file3(from, to) == 1) {
            continue;
        }
        if (!make_dirs(toolDir)) {
            out("%s could not be made (error %lu).\n", toolDir,
                (unsigned long)GetLastError());
            flagFailed = 1;
            return;
        }
        r = own_put(from, to);
        if (r == 1) {
            flagChanged = 1;
            out("Copied %s to %s.\n", names[i], toolDir);
        } else if (r == 2) {
            flagSteps = 1;
        } else {
            flagFailed = 1;
        }
    }
}

/* The installer's own files removed from dir, the helper's result file
 * with them, the window last (record 26 section 9.4): a file that cannot
 * go keeps the window, so the Add/Remove Programs entry still has its
 * program for the next run. 1 when none is left; on NT a file in use is
 * deleted at the restart. */
/* A file's presence: 1 there, 0 absent (the one answer that proves it),
 * -1 unknown (access, I/O). */
static int file_state(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    DWORD e;

    if (a != 0xFFFFFFFFUL) {
        return (a & FILE_ATTRIBUTE_DIRECTORY) ? -1 : 1;
    }
    e = GetLastError();
    return (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? 0 : -1;
}

/* path with its extension replaced by ext (".TMP", ".BAK", ".DEL"). */
static void own_variant(const char *path, const char *ext, char *out,
                        size_t cap)
{
    char *dot;

    inst_copy(out, path, cap);
    dot = strrchr(out, '.');
    if (dot != NULL && strchr(dot, '\\') == NULL && strlen(out) + 4 < cap) {
        strcpy(dot, ext);
    }
}

/* One of the installer's files gone with its .TMP and .BAK (own_put's) and
 * .DEL (an earlier run's held copy): deleted; on NT, held by a running
 * program, renamed .DEL and that deleted at the restart, so a later install
 * that puts the file back is never undone. 1 gone or deferred; 0 said. */
static int own_delete(const char *path)
{
    static const char *const ext[3] = { ".TMP", ".BAK", ".DEL" };
    char v[INST_PATH_CHARS];
    char del[INST_PATH_CHARS];
    int i;
    int s;

    for (i = 0; i < 3; i++) {
        own_variant(path, ext[i], v, sizeof(v));
        s = file_state(v);
        if (s == 0) {
            continue;
        }
        if (opts.List) {
            out("(/list) %s would be deleted.\n", v);
            continue;
        }
        if (s > 0 && delete_file(v)) {
            flagChanged = 1;
            continue;
        }
        if (s > 0 && i == 2 && delete_at_restart(v)) {
            flagRestart = 1;
            continue;
        }
        out("%s could not be deleted (error %lu): the installer stays for "
            "the next run.\n", v, (unsigned long)GetLastError());
        return 0;
    }
    s = file_state(path);
    if (s == 0) {
        return 1;
    }
    if (s < 0) {
        out("Whether %s is there cannot be read: the installer stays for the "
            "next run.\n", path);
        return 0;
    }
    if (opts.List) {
        out("(/list) %s would be deleted.\n", path);
        return 1;
    }
    if (delete_file(path)) {
        flagChanged = 1;
        logonly("%s deleted\n", path);
        return 1;
    }
    own_variant(path, ".DEL", del, sizeof(del));
    if (envNow.Nt && MoveFileA(path, del)) {
        if (delete_at_restart(del)) {
            flagChanged = 1;
            flagRestart = 1;
            out("%s is in use: renamed %s, which is deleted at the "
                "restart.\n", path, del);
            return 1;
        }
        /* Not scheduled: the name goes back, so the file is where the
         * next run looks for it. */
        if (!MoveFileA(del, path)) {
            out("%s could not be deleted, nor renamed back from %s (error "
                "%lu).\n", path, del, (unsigned long)GetLastError());
            flagFailed = 1;
            return 0;
        }
    }
    out("%s could not be deleted (error %lu): the installer stays for the "
        "next run.\n", path, (unsigned long)GetLastError());
    return 0;
}

/* The job file a run from dir writes at its drive's root (dir too long or
 * not 8.3 by its short name): 1 with its path, 0 when it would be
 * beside the installer (own_remove_in's own name) or none. */
static int job_file_for(const char *dir, char *out, size_t cap)
{
    char s[INST_PATH_CHARS];
    char beside[INST_PATH_CHARS];
    DWORD n;

    out[0] = '\0';
    n = GetShortPathNameA(dir, s, sizeof(s));
    if (n == 0 || n >= sizeof(s)) {
        inst_copy(s, dir, sizeof(s));
    }
    join(beside, sizeof(beside), s, INST_DI_JOB_NAME);
    if (!inst_di_jobpath(out, cap, s) || inst_eq(out, beside)) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

static int own_remove_in(const char *dir, int window)
{
    const char *names[4];
    char path[INST_PATH_CHARS];
    int n;
    int i;

    if (dir == NULL || dir[0] == '\0') {
        return 1;
    }
    /* own_names gives the window first: it is taken last here, and only
     * when asked. */
    n = own_names(names);
    names[0] = INST_DI_RESULT_NAME;
    if (window) {
        names[n] = inst_self_file(INST_MY_ARCH, INST_OWN_WINDOW);
        n++;
    }
    /* An earlier run's job file (record 27 section 4.1), beside the
     * installer or, for a directory too long or not 8.3, at its drive's
     * root. */
    join(path, sizeof(path), dir, INST_DI_JOB_NAME);
    if (!own_delete(path)) {
        return 0;
    }
    if (job_file_for(dir, path, sizeof(path)) && !own_delete(path)) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        join(path, sizeof(path), dir, names[i]);
        if (!own_delete(path)) {
            return 0;
        }
    }
    return 1;
}

static int own_remove(void)
{
    return own_remove_in(toolDir, 1);
}

/* The run's end: the helper's result and job files go (the job file is
 * deleted after each job already; a helper that did not end keeps it until
 * here). */
static void helper_files_gone(void)
{
    if (helperResult[0] != '\0' && !stScreens) {
        DeleteFileA(helperResult);
        DeleteFileA(helperJobFile);
    }
}

/* 1 when any of the installer's files (or its .TMP, .BAK or .DEL) is in
 * dir, or cannot be known not to be. */
static int own_any_in(const char *dir)
{
    static const char *const ext[3] = { ".TMP", ".BAK", ".DEL" };
    const char *names[3];
    char path[INST_PATH_CHARS];
    char v[INST_PATH_CHARS];
    int n;
    int i;
    int k;

    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }
    join(path, sizeof(path), dir, INST_DI_RESULT_NAME);
    if (file_state(path) != 0) {
        return 1;
    }
    join(path, sizeof(path), dir, INST_DI_JOB_NAME);
    if (file_state(path) != 0) {
        return 1;
    }
    if (job_file_for(dir, path, sizeof(path)) && file_state(path) != 0) {
        return 1;
    }
    n = own_names(names);
    for (i = 0; i < n; i++) {
        join(path, sizeof(path), dir, names[i]);
        if (file_state(path) != 0) {
            return 1;
        }
        for (k = 0; k < 3; k++) {
            own_variant(path, ext[k], v, sizeof(v));
            if (file_state(v) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* One REG_SZ or REG_DWORD of the entry written when it differs. */
static int arp_text(HKEY k, const char *name, const char *v)
{
    char cur[INST_PATH_CHARS * 2];
    DWORD t;
    DWORD n = sizeof(cur) - 1;

    if (RegQueryValueExA(k, name, NULL, &t, (LPBYTE)cur, &n) == ERROR_SUCCESS &&
        t == REG_SZ) {
        cur[n < sizeof(cur) ? n : sizeof(cur) - 1] = '\0';
        if (strcmp(cur, v) == 0) {
            return 0;
        }
    }
    if (RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)v,
                       (DWORD)strlen(v) + 1) != ERROR_SUCCESS) {
        return -1;
    }
    logonly("  wrote HKLM\\%s\\%s = %s\n", INST_ARP_KEY, name, v);
    return 1;
}

static int arp_dword(HKEY k, const char *name, DWORD v)
{
    DWORD cur;
    DWORD t;
    DWORD n = sizeof(cur);

    if (RegQueryValueExA(k, name, NULL, &t, (LPBYTE)&cur, &n) == ERROR_SUCCESS &&
        t == REG_DWORD && n == sizeof(cur) && cur == v) {
        return 0;
    }
    if (RegSetValueExA(k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof(v)) !=
        ERROR_SUCCESS) {
        return -1;
    }
    return 1;
}

/* The Add/Remove Programs entry (record 26 section 9.1): one entry for
 * the package, written by every install and update once the window is in
 * the install directory; incomplete: an uninstall that stopped, whose
 * entry stays for the next run. */
static void arp_write(int incomplete)
{
    char exe[INST_PATH_CHARS];
    char name[160];
    char cmd[INST_PATH_CHARS * 2];
    const char *window = inst_self_file(INST_MY_ARCH, INST_OWN_WINDOW);
    HKEY k;
    int r = 0;
    int w;

    if (opts.List || toolDir[0] == '\0' || stScreens) {
        return;
    }
    join(exe, sizeof(exe), toolDir, window);
    if (!file_exists(exe, NULL)) {
        logonly("Add/Remove Programs: no %s in %s, no entry written\n",
                window, toolDir);
        return;
    }
    /* Named by what is registered (record 26 section 9.1, draft 8). */
    inst_arp_name(name, sizeof(name), XHCIINST_VERSION, incomplete,
                  envNow.Nt && inst_arp_cm_only(svc_state("xhci98"),
                                                svc_state("xhcicm"),
                                                !opts.Uninstall &&
                                                !usbInclude && haveCm &&
                                                tbInclude));
    if (!inst_arp_command(cmd, sizeof(cmd), toolDir, window, envNow.Nt)) {
        out("The Add/Remove Programs entry cannot name %s.\n", exe);
        flagFailed = 1;
        return;
    }
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, INST_ARP_KEY, 0, NULL, 0,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &k, NULL) !=
        ERROR_SUCCESS) {
        out("The Add/Remove Programs entry could not be written.\n");
        flagFailed = 1;
        return;
    }
    w = arp_text(k, "DisplayName", name);
    r |= w;
    if (w >= 0) {
        w = arp_text(k, "DisplayVersion", XHCIINST_VERSION);
        r |= w;
    }
    if (w >= 0) {
        w = arp_text(k, "Publisher", INST_PUBLISHER);
        r |= w;
    }
    if (w >= 0) {
        w = arp_text(k, "InstallLocation", toolDir);
        r |= w;
    }
    if (w >= 0) {
        w = arp_text(k, "UninstallString", cmd);
        r |= w;
    }
    if (w >= 0) {
        w = arp_dword(k, "NoModify", 1);
        r |= w;
    }
    if (w >= 0) {
        w = arp_dword(k, "NoRepair", 1);
        r |= w;
    }
    RegCloseKey(k);
    if (w < 0) {
        out("The Add/Remove Programs entry could not be written whole.\n");
        flagFailed = 1;
        return;
    }
    if (r > 0) {
        flagChanged = 1;
        out("%s: %s.\n", incomplete ? "The Add/Remove Programs entry stays, "
            "for the next run" : "Add/Remove Programs lists it", name);
    }
}

/* The entry deleted, last but for the directory (record 26 section 9.1). */
/* 1 removed, or never there; 0 said, failed. */
static int arp_remove(void)
{
    LONG r = RegDeleteKeyA(HKEY_LOCAL_MACHINE, INST_ARP_KEY);

    if (r == ERROR_SUCCESS) {
        flagChanged = 1;
        out("The Add/Remove Programs entry was removed.\n");
        return 1;
    }
    if (r == ERROR_FILE_NOT_FOUND) {
        return 1;
    }
    out("The Add/Remove Programs entry could not be removed (error %ld).\n",
        (long)r);
    flagFailed = 1;
    return 0;
}

/* The machine-wide values the settings table names (record 26 section 9.3
 * step 6): each deleted by name, the key, SilentClasses, usbport's own
 * DisableSelectiveSuspend and every value the table does not name left. */
/* 1 when none of them is left (or the key is absent); 0 said, failed. */
static int mw_values_remove(void)
{
    HKEY k;
    int v;
    int ok = 1;
    LONG r;

    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, INST_MW_KEY, 0,
                      KEY_SET_VALUE | KEY_QUERY_VALUE, &k);
    if (r == ERROR_FILE_NOT_FOUND) {
        return 1;
    }
    if (r != ERROR_SUCCESS) {
        out("HKLM\\%s could not be opened (error %ld): the machine-wide "
            "values stay.\n", INST_MW_KEY, (long)r);
        flagFailed = 1;
        return 0;
    }
    for (v = 0; v < instValueCount; v++) {
        if (opts.List) {
            DWORD t;
            if (RegQueryValueExA(k, instValues[v].Name, NULL, &t, NULL, NULL) ==
                ERROR_SUCCESS) {
                out("(/list) the machine-wide %s would be deleted.\n",
                    instValues[v].Name);
            }
            continue;
        }
        r = RegDeleteValueA(k, instValues[v].Name);
        if (r == ERROR_SUCCESS) {
            flagChanged = 1;
            out("The machine-wide %s was deleted.\n", instValues[v].Name);
        } else if (r != ERROR_FILE_NOT_FOUND) {
            out("The machine-wide %s could not be deleted (error %ld).\n",
                instValues[v].Name, (long)r);
            flagFailed = 1;
            ok = 0;
        }
    }
    RegCloseKey(k);
    return ok;
}

/* ======================================================================== */
/* The engine's interface (inst_eng.h; record 26 section 3)                 */
/* ======================================================================== */

static int relaunched;      /* the uninstall goes on in a temporary copy */
static char tempCopyDir[INST_PATH_CHARS];   /* this run is that copy     */

void inst_eng_init(const INST_FRONT *f, const char *selfName,
                   const char *otherName, int window)
{
    front = *f;
    instSelf = selfName;
    instOther = otherName;
    instWindow = window;
}

void inst_eng_owner(HWND owner)
{
    front.Owner = owner;
}

int inst_eng_parse(int argc, char **argv, char *err, size_t cap)
{
    return inst_parse_args(argc, argv, &opts, err, cap);
}

INST_OPTS *inst_eng_opts(void)
{
    return &opts;
}

int inst_eng_selftest(void)
{
    return selftest();
}

/* The version as record 23 section 4.3 reads it (revision 13): on NT
 * RtlGetVersion's, looked up at run time, the CSD string GetVersionExA's;
 * where the routine is missing GetVersionExA's alone, which is true below
 * 6.2 and refused from it. 0: not read (dwOSVersionInfoSize nonzero when
 * only the exact reading failed). */
int inst_eng_os_read(OSVERSIONINFOA *v)
{
    PFN_RTLGETVERSION rtl;
    OSVERSIONINFOW w;
    HMODULE nt;

    memset(v, 0, sizeof(*v));
    v->dwOSVersionInfoSize = sizeof(*v);
    if (!GetVersionExA(v)) {
        v->dwOSVersionInfoSize = 0;
        return 0;
    }
    if (v->dwPlatformId != VER_PLATFORM_WIN32_NT) {
        return 1;
    }
    nt = GetModuleHandleA("ntdll.dll");
    rtl = nt != NULL ? (PFN_RTLGETVERSION)GetProcAddress(nt, "RtlGetVersion")
                     : NULL;
    memset(&w, 0, sizeof(w));
    w.dwOSVersionInfoSize = sizeof(w);
    if (rtl != NULL && rtl(&w) == 0) {
        v->dwMajorVersion = w.dwMajorVersion;
        v->dwMinorVersion = w.dwMinorVersion;
        v->dwBuildNumber = w.dwBuildNumber;
        return 1;
    }
    if (v->dwMajorVersion > 6 ||
        (v->dwMajorVersion == 6 && v->dwMinorVersion >= 2)) {
        return 0;
    }
    return 1;
}

int inst_eng_begin(int argc, char **argv)
{
    PFN_ISWOW64 isWow64;
    BOOL wow = FALSE;
    int i;

    if ((!opts.List && !opts.Devices) || opts.Log[0] != '\0') {
        log_open();
    }
    out("%s %s - %s\n", instWindow ? "xhciwiz" : "xhciinst", XHCIINST_VERSION,
        (opts.Devices || opts.Eject)
            ? "Thunderbolt and USB4 devices"
            : "install, update or uninstall the xhci98 package");
    out("Log: %s\n", logPath[0] ? logPath : "none (/list and /devices write "
        "none unless /log names one)");
    for (i = 0; i < argc; i++) {
        logonly("argv[%d] = %s\n", i, argv[i]);
    }

    if (!inst_eng_os_read(&osv)) {
        out("The Windows version could not be read%s.\n",
            osv.dwOSVersionInfoSize != 0 ? " exactly (ntdll.dll's "
            "RtlGetVersion is missing, and Windows answers 6.2 or later)"
            : "");
        return INST_EXIT_REFUSED;
    }
    osKind = inst_os_kind(osv.dwPlatformId == VER_PLATFORM_WIN32_NT,
                          osv.dwMajorVersion, osv.dwMinorVersion,
                          osv.dwBuildNumber & 0xFFFFUL);
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
            return INST_EXIT_REFUSED;
        }
    } else if (osv.dwPlatformId == VER_PLATFORM_WIN32_NT) {
        envNow.Nt = 1;
        if (osKind == INST_OS_REFUSED) {
            out("This Windows is not one this package supports (2000, XP, "
                "XP x64, Vista, 7;\n8 and 8.1 for the Thunderbolt 3 / USB4 "
                "connection manager alone).\n");
            return INST_EXIT_REFUSED;
        }
        isWow64 = (PFN_ISWOW64)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                              "IsWow64Process");
        if (isWow64 != NULL && !isWow64(GetCurrentProcess(), &wow)) {
            wow = FALSE;
        }
        if (wow) {
            out("This is 64-bit Windows: the 32-bit program cannot %s here.\n",
                (opts.Devices || opts.Eject) ? "read the devices"
                                             : "install drivers");
            if (instWindow) {
                out("REFUSED: the package is incomplete: tools\\xhciinst\\%s "
                    "is missing;\nunzip the whole package.\n",
                    INST_OTHER_NAME);
            } else {
                out("REFUSED: run INSTALL.BAT, which runs %s.\n",
                    INST_OTHER_NAME);
            }
            return INST_EXIT_REFUSED;
        }
    } else {
        out("Not a Windows this tool knows.\n");
        return INST_EXIT_REFUSED;
    }
    if (!envNow.Nt && INST_MY_ARCH != INST_ARCH_X86) {
        return INST_EXIT_REFUSED;
    }
    for (i = 0; i < opts.SetCount; i++) {
        int sk = instValues[opts.SetIndex[i]].Inf;
        if ((opts.CmOnly || (osKind == INST_OS_CMONLY && !opts.Usb)) &&
            (sk == INST_INF_XHCI || sk == INST_INF_UAS)) {
            out("REFUSED: /set %s:\nthe USB drivers are not in this run "
                "(/cmonly, or Windows 8 and 8.1).\n",
                instValues[opts.SetIndex[i]].Name);
            return INST_EXIT_REFUSED;
        }
    }
    for (i = 0; i < opts.SetCount; i++) {
        if (!inst_value_allowed(opts.SetIndex[i], &envNow, opts.SetValue[i])) {
            out("REFUSED: /set %s=%lu does not work on this Windows.\n",
                instValues[opts.SetIndex[i]].Name,
                (unsigned long)opts.SetValue[i]);
            return INST_EXIT_REFUSED;
        }
    }
    devs = (DEVREC *)calloc(INST_MAX_DEVS, sizeof(DEVREC));
    if (devs == NULL) {
        out("Out of memory.\n");
        return INST_EXIT_REFUSED;
    }
    return -1;
}

int inst_eng_devices(void)
{
    /* Record 24: no package is read, and no administrator test is made
     * first (each call's answer is reported). */
    free(devs);
    devs = NULL;
    tbn = (TBNODE *)calloc(INST_MAX_DEVS, sizeof(TBNODE));
    if (tbn == NULL) {
        out("Out of memory.\n");
        return INST_EXIT_REFUSED;
    }
    tbnFull = 1;
    load_routines();
    tb_routines();
    if (!tb_lock()) {
        return INST_EXIT_REFUSED;
    }
    return opts.Devices ? run_devices() : run_eject();
}

int inst_eng_read(void)
{
    SC_HANDLE scm;

    /* Record 26 section 9.2 (W12): the uninstall reads the system alone,
     * so it needs no package. */
    if (!opts.Uninstall && !check_package()) {
        return INST_EXIT_REFUSED;
    }
    if (!inst_ver_parse(XHCIINST_VERSION, pkgVersion)) {
        return INST_EXIT_REFUSED;
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
            return INST_EXIT_REFUSED;
        }
        CloseServiceHandle(scm);
    }
    if (envNow.Nt && (api.GetClassDevs == NULL || api.EnumInfo == NULL ||
                      api.GetInstanceId == NULL || api.GetRegProp == NULL ||
                      api.Destroy == NULL || api.Status == NULL)) {
        out("REFUSED: the device routines this needs are missing (the log "
            "names them).\n");
        return INST_EXIT_REFUSED;
    }
    if (opts.Uninstall) {
        return -1;
    }
    if (envNow.Nt && !opts.List && api.UpdateDriver == NULL) {
        out("REFUSED: UpdateDriverForPlugAndPlayDevicesA is missing.\n");
        return INST_EXIT_REFUSED;
    }
    if (!scan()) {
        if (!envNow.Nt) {
            out("The device tree could not be read whole: nothing is "
                "changed.\n");
        }
        return INST_EXIT_REFUSED;
    }
    if (!usb_decide()) {
        return INST_EXIT_REFUSED;
    }
    plan();
    /* Record 26 section 6.5 on Windows 98 SE; elsewhere (section 5.3.1,
     * task 43.43) /storage copies the files to the tools folder only. */
    stor_read();
    if (opts.Storage && storPack <= 0) {
        out("REFUSED: /storage: %s\n", storPack == 0 ?
            "this download does not carry the USB storage drivers." :
            "the storage files in this download are not the ones this "
            "release carries.");
        return INST_EXIT_REFUSED;
    }
    return -1;
}

/* Record 23 section 4.5: the USB drivers in the run or out of it, before
 * the first plan. 0 refused. */
static int usb_decide(void)
{
    const char *why;
    int r = inst_usb_decide(osKind, envNow.Nt, haveCm, opts.CmOnly,
                            opts.Usb, &usbOffered, &why);

    if (r < 0) {
        out("REFUSED: %s\n", why != NULL ? why : "the components.");
        return 0;
    }
    if (osKind == INST_OS_CMONLY && r == 0 && !haveCm) {
        /* Record 26 section 5.3.1 case 5 (owner, 2026-10-22). */
        out("%s: this package carries no Thunderbolt 3 / USB4 driver; it "
            "installs the xHCI98\ntools, and the xHCI98 USB drivers only "
            "with /usb (untested on Windows 8 and\n8.1).\n\n",
            inst_os_name(1, envNow.Major, envNow.Minor));
    } else if (osKind == INST_OS_CMONLY && r == 0) {
        /* Decision D18 as amended by record 23 revision 26 (task 43.43):
         * the USB drivers and Stor98 are offered here, unticked. */
        out("%s: this package installs the Thunderbolt 3 / USB4 connection "
            "manager\nhere; Windows' own USB 3 driver keeps running the USB "
            "controllers unless\n/usb puts the xHCI98 USB drivers in the run "
            "(untested on Windows 8 and 8.1).\n\n",
            inst_os_name(1, envNow.Major, envNow.Minor));
    } else if (osKind == INST_OS_CMONLY) {
        out("/usb: the xHCI98 USB drivers are in this run (untested on %s).\n\n",
            inst_os_name(1, envNow.Major, envNow.Minor));
    } else if (r == 0) {
        out("/cmonly: the Thunderbolt 3 / USB4 connection manager alone; the "
            "USB drivers\nare left out of this run.\n\n");
    }
    uasInclude = inst_uas_decide(1, opts.NoUas);
    if (r != 0 && !uasInclude) {
        out("/nouas: the UAS disk driver (xhciuas.sys) is left out of this run; "
            "one already\ninstalled stays as it is.\n\n");
    }
    usb_apply(r);
    logonly("usb drivers: kind %d, offered %d, include %d, uas %d\n", osKind,
            usbOffered, usbInclude, uasInclude);
    return 1;
}

/* The window's USB drivers' box (record 26 section 5.3, draft 8): *offered
 * 1 when it can be unticked; returns whether they are in the run. */
int inst_eng_usb_state(int *offered, int *notOnThisSystem)
{
    *offered = usbOffered;
    *notOnThisSystem = osKind == INST_OS_CMONLY;
    return usbInclude;
}

int inst_eng_uas_state(void)
{
    return uasInclude;
}

void inst_eng_uas_choose(int include)
{
    uasInclude = include ? 1 : 0;
    usb_apply(usbInclude);
    plan();
    logonly("uas driver: chosen %d\n", uasInclude);
}

void inst_eng_usb_choose(int include)
{
    if (!usbOffered) {
        return;
    }
    usb_apply(include);
    plan();
    logonly("usb drivers: chosen %d\n", usbInclude);
}

/* 1 when this machine has the connection manager alone registered (the
 * uninstall's warning, record 23 section 9.1, revision 13). */
int inst_eng_cm_only_machine(void)
{
    if (stScreens) {
        return stCmOnly;
    }
    if (!envNow.Nt && (GetVersion() & 0x80000000UL) != 0) {
        return 0;
    }
    return inst_arp_cm_only(svc_state("xhci98"), svc_state("xhcicm"), 0);
}

void inst_eng_tb_question(void)
{
    tb_question();
    s98_question();
    stor_question();
}

int inst_eng_tb_state(int *haveCmOut, int *present, int *defaultYes,
                      int *installed)
{
    int asked;
    int include = tb_decide(&asked, defaultYes, installed);

    *haveCmOut = haveCm;
    *present = tbNhiPresent;
    if (!asked) {
        *defaultYes = include;
    }
    tb_apply(tbInclude);
    return include;
}

void inst_eng_tb_choose(int include)
{
    tb_apply(include);
    logonly("thunderbolt: chosen %d (present %d)\n", tbInclude, tbNhiPresent);
}

static int planResult = -1;

int inst_eng_plan(void)
{
    planResult = envNow.Nt ? plan_nt() : plan_9x();
    return planResult;
}

/* Owner, 2026-10-19: a run that writes XhciCmMode 0 says so plainly (a
 * wheel turned the support off unnoticed on real hardware). */
static void cm_off_warning(void)
{
    int v;

    for (v = 0; v < instValueCount && v < INST_MAX_VALUES; v++) {
        if (inst_cm_mode_off(instValues[v].Name, answerValue[v]) &&
            value_needs_write(v)) {
            out("*** %s. ***\n\n", instCmOffText);
            logonly("warned: %s\n", instCmOffText);
        }
    }
}

/* Record 26 section 5.3.1 (task 43.43): a box ticked, or the USB drivers
 * unticked, where it does not apply - each warning in the window's words,
 * before the go-ahead question; printed and logged under /yes and /list,
 * never a question of its own. */
static INST_U32 comp_warnings_for(int usb, int stor, int s98)
{
    return inst_comp_warnings(!envNow.Nt && envNow.Minor == 10, !envNow.Nt,
                              osKind == INST_OS_CMONLY, haveCm, usb, stor,
                              s98);
}

static void comp_warnings_out(void)
{
    char text[2048];
    char *p;
    char *q;
    INST_U32 w = comp_warnings_for(usbInclude, storToolsInclude, s98Include);

    if (w == 0) {
        return;
    }
    inst_comp_warn_text(w, toolDir, text, sizeof(text));
    out("\nWARNING:\n");
    for (p = text; *p != '\0'; p = q) {
        q = strstr(p, "\n\n");
        if (q != NULL) {
            *q = '\0';
            q += 2;
        } else {
            q = p + strlen(p);
        }
        stor_out_wrapped("  ", p);
        if (*q != '\0') {
            out("\n");
        }
    }
    out("\n");
}

INST_U32 inst_eng_comp_warn(int usb, int stor, int s98, const char *dir,
                            char *text, size_t cap)
{
    INST_U32 w = comp_warnings_for(usb, stor && storTools, s98);

    inst_comp_warn_text(w, dir, text, cap);
    if (w != 0) {
        logonly("components: warning %lu shown\n", (unsigned long)w);
    }
    return w;
}

int inst_eng_ask(void)
{
    if (planResult >= 0) {
        return planResult;
    }
    ask_values();
    s98_ask_values();
    if (!choose_dir()) {
        return INST_EXIT_REFUSED;
    }
    s98_ask_unused();
    cm_off_warning();
    comp_warnings_out();
    if (opts.List) {
        tray_install();
        out("/list: nothing was changed.\n");
        return INST_EXIT_DONE;
    }
    if (envNow.Nt) {
        input_warning();
        if (!confirm(inst_confirm_text(planned_runs(), values_to_write(), 1,
                                       0))) {
            return INST_EXIT_REFUSED;
        }
        return -1;
    }
    return ask_9x();
}

int inst_eng_run(void)
{
    int code = envNow.Nt ? run_install_nt() : run_install_9x();
    int f = flagFailed;
    int r = flagRestart;
    int s = flagSteps;

    /* Whatever the run's verdict, the invariant of record 21 section 7.4
     * holds at its end (decision M13). */
    mk_end_of_run();
    /* And no CriticalDeviceDatabase entry names a service of the package
     * that is gone (an update's INF retires xhcimsi on Vista and 7). */
    if (envNow.Nt && !opts.List && !cddb_service_scrub()) {
        flagFailed = 1;
    }
    /* The marks' flags join the run's verdict, which a failure returned
     * without flagFailed (a refused baseline) keeps. */
    if (code != INST_EXIT_REFUSED && code != INST_EXIT_FAILED &&
        (f != flagFailed || r != flagRestart || s != flagSteps)) {
        code = inst_exit_code(0, flagFailed, flagRestart,
                              flagSteps || code == INST_EXIT_STEPS);
    }
    helper_files_gone();
    return code;
}

/* Record 26 section 9.2: a run from the install directory copies itself
 * (and on 98 SE and ME the helper) to a directory of its own under TEMP,
 * starts that copy with /uninstall /from <dir> and ends; the copy does
 * the uninstall. */
int inst_eng_un_route(void)
{
    char self[INST_PATH_CHARS];
    char selfDir[INST_PATH_CHARS];
    char temp[INST_PATH_CHARS];
    char to[INST_PATH_CHARS];
    char from[INST_PATH_CHARS];
    char cmd[INST_PATH_CHARS * 3];
    char *slash;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    int route;
    int selfIn;
    int fromIs;
    DWORD n;

    earlier_dir(toolDir, sizeof(toolDir));
    movefrom_get(oldToolDir, sizeof(oldToolDir));
    logonly("install directory: \"%s\", move from \"%s\"\n", toolDir,
            oldToolDir);
    if (GetModuleFileNameA(NULL, self, sizeof(self)) == 0) {
        return INST_EXIT_REFUSED;
    }
    inst_copy(selfDir, self, sizeof(selfDir));
    slash = strrchr(selfDir, '\\');
    if (slash == NULL) {
        return INST_EXIT_REFUSED;
    }
    *slash = '\0';
    selfIn = toolDir[0] != '\0' && same_dir(selfDir, toolDir);
    fromIs = opts.From[0] != '\0' && toolDir[0] != '\0' &&
             same_dir(opts.From, toolDir);
    route = opts.List ? INST_UN_RUN :
            inst_uninstall_route(selfIn, opts.From[0] != '\0', fromIs);
    if (route == INST_UN_REFUSE) {
        out("REFUSED: /from %s is not the install directory this machine "
            "records%s.\n", opts.From, selfIn ? ", or this copy is in it" : "");
        return INST_EXIT_REFUSED;
    }
    /* The copy goes to TEMP\XHCI98UN, a directory of this program's own. */
    n = GetTempPathA(sizeof(temp), temp);
    if (n == 0 || n >= sizeof(temp)) {
        out("REFUSED: the temporary directory could not be found.\n");
        return INST_EXIT_REFUSED;
    }
    join(temp, sizeof(temp), temp, "XHCI98UN");
    if (route == INST_UN_RUN) {
        if (opts.From[0] != '\0') {
            /* /from is the relaunch's alone: a copy anywhere else (the
             * package's) is refused, and only the relaunch's directory is
             * ever scheduled for deletion (record 26 section 9.2). */
            if (!same_dir(selfDir, temp)) {
                out("REFUSED: /from belongs to the uninstall's own copy in "
                    "%s.\n", temp);
                return INST_EXIT_REFUSED;
            }
            inst_copy(tempCopyDir, selfDir, sizeof(tempCopyDir));
        }
        return -1;
    }
    CreateDirectoryA(temp, NULL);
    join(to, sizeof(to), temp, INST_MY_NAME);
    SetFileAttributesA(to, FILE_ATTRIBUTE_NORMAL);
    if (!CopyFileA(self, to, FALSE)) {
        out("REFUSED: this program could not be copied to %s (error %lu).\n",
            temp, (unsigned long)GetLastError());
        return INST_EXIT_REFUSED;
    }
    if (!envNow.Nt) {
        join(from, sizeof(from), selfDir, INST_DI_NAME);
        join(cmd, sizeof(cmd), temp, INST_DI_NAME);
        if (file_exists(from, NULL) && !CopyFileA(from, cmd, FALSE)) {
            out("REFUSED: the helper could not be copied to %s.\n", temp);
            return INST_EXIT_REFUSED;
        }
    }
    if (!inst_relaunch_cmd(cmd, sizeof(cmd), to, toolDir, opts.Log,
                           opts.NoPause, opts.Yes)) {
        return INST_EXIT_REFUSED;
    }
    logonly("relaunch: %s\n", cmd);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                        instWindow ? 0 : CREATE_NEW_CONSOLE, NULL, temp, &si,
                        &pi)) {
        out("REFUSED: the copy in %s could not be started (error %lu).\n",
            temp, (unsigned long)GetLastError());
        return INST_EXIT_REFUSED;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    relaunched = 1;
    out("The uninstall goes on in a copy of this program in %s, so this one "
        "in\n%s can be removed.\n", temp, toolDir);
    return INST_EXIT_DONE;
}

int inst_eng_uninstall(void)
{
    int code = envNow.Nt ? run_uninstall_nt() : run_uninstall_9x();
    PFN_MOVEEX moveEx = move_ex();
    char path[INST_PATH_CHARS];

    helper_files_gone();
    if (tempCopyDir[0] != '\0' && !opts.List) {
        /* This run is the temporary copy (record 26 section 9.2): on NT
         * it goes at the restart; on 98 SE and ME it stays, said. */
        if (envNow.Nt && moveEx != NULL) {
            join(path, sizeof(path), tempCopyDir, INST_MY_NAME);
            moveEx(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
            moveEx(tempCopyDir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        } else {
            out("\nThis program's copy in %s stays (a few hundred kilobytes): "
                "delete it by\nhand, or with a disk cleanup.\n", tempCopyDir);
        }
    }
    return code;
}

int inst_eng_relaunched(void)
{
    return relaunched;
}

int inst_eng_end(int code, int *line)
{
    int last = INST_FINAL_NO_CHANGES;

    /* Owner, 2026-10-15: a restart after every change, on every system,
     * whatever Windows itself says. The last line and the exit code agree
     * (record 23 section 7.4, orchestrator decision 2026-10-15): a finished
     * run that changed anything asks for the restart and exits 3; a run
     * that stopped short says to finish the steps above first. */
    if (opts.Devices || opts.Eject) {
        /* The device modes' exception (record 23 section 7.4, record 24
         * section 8.2): the eject's own last line, no restart. */
        out("\n%s\n", devLast != NULL ? devLast : "No changes were made.");
    } else if (relaunched) {
        out("\nThe uninstall goes on in the window that opened.\n");
    } else if (!opts.SelfTest) {
        code = inst_final(flagChanged, opts.List, code, &last);
        out("\n%s\n", inst_final_text_cold(last, inst_eng_shutdown()));
    }
    out("\nExit code %d. The log is %s.\n", code,
        logPath[0] ? logPath : "(none)");
    out_flush();
    logonly("exit %d\n", code);
    if (logFile != NULL) {
        fclose(logFile);
        logFile = NULL;
    }
    if (line != NULL) {
        *line = last;
    }
    return code;
}

/* ---- the window's readings ------------------------------------------------ */

int inst_eng_found(int i, INST_FOUND *f)
{
    int j;
    int n = 0;

    for (j = 0; j < devCount; j++) {
        const DEVREC *d = &devs[j];
        if (d->Inf < 0) {
            continue;
        }
        if (n++ != i) {
            continue;
        }
        memset(f, 0, sizeof(*f));
        f->Desc = d->Desc[0] ? d->Desc : "(no description)";
        f->Id = d->Id;
        f->Inf = instInfFile[d->Inf];
        _snprintf(f->Now, sizeof(f->Now) - 1, "%s%s%s%s",
                  d->Service[0] ? d->Service : "no driver",
                  d->VersionText[0] ? " " : "", d->VersionText,
                  d->RegOnly ? " (recorded, not running now)" :
                  !d->Present ? " (not present)" : "");
        f->Now[sizeof(f->Now) - 1] = '\0';
        f->Action = d->Refused ? "left: a newer or unreadable release" :
                    (d->Inf == INST_INF_S98 && d->Present &&
                     d->S98Why != INST_S98_ELIGIBLE) ?
                        inst_s98_skip_text(d->S98Why, 1) :
                    (d->Runs && (d->Action == INST_ACT_CURRENT ||
                                 d->Action == INST_ACT_NEWER)) ?
                        "reinstall (/force)" : inst_action_text(d->Action);
        f->Runs = d->Runs;
        f->Refused = d->Refused;
        f->Present = d->Present;
        return 1;
    }
    return 0;
}

void inst_eng_dir_default(char *def, size_t cap, char *now, size_t nowCap,
                          int *toolHere)
{
    char win[INST_PATH_CHARS];
    char sysDrive[16];
    char from[INST_PATH_CHARS];
    DWORD n;

    earlier_dir(oldToolDir, sizeof(oldToolDir));
    inst_copy(now, oldToolDir, nowCap);
    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        win[0] = '\0';
    }
    sysDrive[0] = '\0';
    if (envNow.Nt) {
        n = GetEnvironmentVariableA("SystemDrive", sysDrive, sizeof(sysDrive));
        if (n == 0 || n >= sizeof(sysDrive)) {
            sysDrive[0] = '\0';
        }
    }
    if (oldToolDir[0] != '\0') {
        inst_copy(def, oldToolDir, cap);
    } else {
        inst_dir_default(sysDrive, win, def, cap);
    }
    join(from, sizeof(from), root, "tools\\xhcitr");
    join(from, sizeof(from), from, inst_tray_file(INST_MY_ARCH));
    *toolHere = file_exists(from, NULL);
}

const char *inst_eng_dir_check(const char *path, char *checked, size_t cap)
{
    char win[INST_PATH_CHARS];

    if (GetWindowsDirectoryA(win, sizeof(win)) == 0) {
        win[0] = '\0';
    }
    return dir_why(path, win, checked, cap);
}

void inst_eng_flags(int *changed, int *failed, int *restart, int *steps)
{
    *changed = flagChanged;
    *failed = flagFailed;
    *restart = flagRestart;
    *steps = flagSteps;
}

const char *inst_eng_steps_text(void)
{
    return stepsText;
}

const char *inst_eng_tool_dir(void)
{
    return toolDir;
}

int inst_eng_controller_changes(void)
{
    int i;

    for (i = 0; i < devCount; i++) {
        if (devs[i].Present && devs[i].Inf == INST_INF_XHCI &&
            is_controller(&devs[i]) && devs[i].Runs) {
            return 1;
        }
    }
    return 0;
}

const char *inst_eng_confirm_text(void)
{
    return inst_confirm_text(planned_runs(), values_to_write(), envNow.Nt, 0);
}

const char *inst_eng_summary_head(int writes, int other)
{
    return inst_summary_head(planned_runs(), writes, other, envNow.Nt);
}

int inst_eng_tray_started(void)
{
    char cur[INST_PATH_CHARS + 16];

    return reg_text3(HKEY_LOCAL_MACHINE, INST_TRAY_RUN_KEY,
                     INST_TRAY_RUN_VALUE, cur, sizeof(cur)) == 1;
}
