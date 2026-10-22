/*
 * xhciperf.c - XHCIPERF, the xHCI98 Disk Benchmark (design record 29;
 * roadmap-usb4.md task 43.50): the window, the run (a controlling thread
 * and one worker thread per test thread), the test file, the Stor98
 * counter reads, the command line and the self-test. Every decision and
 * every text is the pure half's (pf_core.c, pf_s98.c); this file makes the
 * system calls. Plain Win32, ANSI, C89 with __int64; it starts on 98 SE,
 * ME, 2000, XP, Vista, 7, 8, 8.1 and 10 (x86), XHCIPF64.EXE on the x64
 * systems.
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#ifdef _WIN64
#define WINVER 0x0502
#define _WIN32_WINNT 0x0502
#define _WIN32_IE 0x0500
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winioctl.h>
#include <process.h>
#include <string.h>
#include <stdlib.h>
/* MSVC 6.0's headers predate the pointer-sized types; WDK 7.1's have them */
#ifndef MAXULONG_PTR
typedef unsigned long ULONG_PTR;
typedef unsigned long DWORD_PTR;
#endif

#include "pf_core.h"
#include "pf_s98.h"
#include "pf_res.h"
#include "../src/xhci_version.h"
#include "../src/stor98/include/stor98pf.h"

#define XHCIPERF_VERSION XHCI_VER_STR

/* The reader's offsets (pf_s98.h) are the header's (stor98pf.h, Stor98's
 * decision MD-8): held here, where the header compiles. */
#define PF_CASSERT(n, c) typedef char pf_cassert_##n[(c) ? 1 : -1]
PF_CASSERT(sz, sizeof(STOR98PF_ANSWER) == S98_ANSWER_BYTES);
PF_CASSERT(ep, STOR98PF_OFS(STOR98PF_ANSWER, Epoch) == S98_OFS_EPOCH);
PF_CASSERT(pci, STOR98PF_OFS(STOR98PF_ANSWER, PciLocation) == S98_OFS_PCI);
PF_CASSERT(ver, STOR98PF_OFS(STOR98PF_ANSWER, DriverVersion) == S98_OFS_DRIVERVERSION);
PF_CASSERT(mx, STOR98PF_OFS(STOR98PF_ANSWER, MaxTransfer) == S98_OFS_MAXTRANSFER);
PF_CASSERT(pol, STOR98PF_OFS(STOR98PF_ANSWER, Polling) == S98_OFS_POLLING);
PF_CASSERT(dcb, STOR98PF_OFS(STOR98PF_ANSWER, DcbDmdFlags) == S98_OFS_DCBDMD);
PF_CASSERT(evt, STOR98PF_OFS(STOR98PF_ANSWER, Events) == S98_OFS_EVENTS);
PF_CASSERT(tot, STOR98PF_OFS(STOR98PF_ANSWER, Totals) == S98_OFS_TOTALS);
PF_CASSERT(t1, STOR98PF_OFS(STOR98PF_TOTALS, CmdSize) == 8 * S98_T_CMDSIZE);
PF_CASSERT(t2, STOR98PF_OFS(STOR98PF_TOTALS, Depth) == 8 * S98_T_DEPTH);
PF_CASSERT(t3, STOR98PF_OFS(STOR98PF_TOTALS, Runs) == 8 * S98_T_RUNS);
PF_CASSERT(t4, STOR98PF_OFS(STOR98PF_TOTALS, Sg9x) == 8 * S98_T_SG9X);
PF_CASSERT(t5, STOR98PF_OFS(STOR98PF_TOTALS, DoubleBuffered) == 8 * S98_T_DOUBLEBUF);
PF_CASSERT(t6, STOR98PF_VPD_PAGE == S98_VPD_PAGE && STOR98PF_BUFFER_ID == S98_BUFFER_ID);

/* ------------------------------------------------------------------------ */
/* Interface definitions this file takes from the SDK and DDK by value, so  */
/* it builds against MSVC 6.0's headers (record 29 section 10.2)            */
/* ------------------------------------------------------------------------ */

#define PF_IOCTL_SCSI_PASS_THROUGH      CTL_CODE(0x00000004, 0x0401, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define PF_IOCTL_SCSI_GET_ADDRESS       CTL_CODE(0x00000004, 0x0406, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PF_IOCTL_STORAGE_QUERY_PROPERTY CTL_CODE(0x0000002D, 0x0500, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define PF_IOCTL_VOLUME_DISK_EXTENTS    CTL_CODE(0x00000056, 0x0000, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct pf_scsi_address {
    ULONG Length;
    UCHAR PortNumber;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
} PF_SCSI_ADDRESS;

typedef struct pf_spt {
    USHORT Length;
    UCHAR ScsiStatus;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR CdbLength;
    UCHAR SenseInfoLength;
    UCHAR DataIn;
    ULONG DataTransferLength;
    ULONG TimeOutValue;
    ULONG_PTR DataBufferOffset;
    ULONG SenseInfoOffset;
    UCHAR Cdb[16];
} PF_SPT;

typedef struct pf_spt_buf {
    PF_SPT spt;
    ULONG filler;
    UCHAR sense[32];
    UCHAR data[2048];
} PF_SPT_BUF;

typedef struct pf_prop_query {
    DWORD PropertyId;
    DWORD QueryType;
    UCHAR Additional[4];
} PF_PROP_QUERY;

/* ASPI for Win32 (the Windows 98 DDK's block\wnaspi32 WNASPI32.H) */
#define PF_SC_HA_INQUIRY    0x00
#define PF_SC_GET_DEV_TYPE  0x01
#define PF_SC_EXEC_SCSI_CMD 0x02
#define PF_SC_ABORT_SRB     0x03
#define PF_SS_PENDING       0x00
#define PF_SS_COMP          0x01
#define PF_SRB_DIR_IN       0x08
#define PF_SRB_EVENT_NOTIFY 0x40

typedef struct pf_srb_hainq {
    BYTE Cmd, Status, HaId, Flags;
    DWORD HdrRsvd;
    BYTE HaCount, HaScsiId;
    BYTE ManagerId[16], Identifier[16], Unique[16];
    WORD Rsvd1;
} PF_SRB_HAINQ;

typedef struct pf_srb_gdev {
    BYTE Cmd, Status, HaId, Flags;
    DWORD HdrRsvd;
    BYTE Target, Lun, DeviceType, Rsvd1;
} PF_SRB_GDEV;

typedef struct pf_srb_exec {
    BYTE Cmd, Status, HaId, Flags;
    DWORD HdrRsvd;
    BYTE Target, Lun;
    WORD Rsvd1;
    DWORD BufLen;
    BYTE *BufPointer;
    BYTE SenseLen, CdbLen, HaStat, TargStat;
    void *PostProc;
    void *Rsvd2;
    BYTE Rsvd3[16];
    BYTE Cdb[16];
    BYTE Sense[16];
} PF_SRB_EXEC;

typedef struct pf_srb_abort {
    BYTE Cmd, Status, HaId, Flags;
    DWORD HdrRsvd;
    void *ToAbort;
} PF_SRB_ABORT;

typedef DWORD (*PF_SENDASPI)(void *);
typedef DWORD (*PF_GETASPIINFO)(void);
typedef BOOL (WINAPI *PF_GETFREEEX)(LPCSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);
typedef DWORD (WINAPI *PF_GETLONGPATH)(LPCSTR, LPSTR, DWORD);
typedef BOOL (WINAPI *PF_CANCELIO)(HANDLE);
typedef DWORD (WINAPI *PF_SETEXEC)(DWORD);
typedef BOOL (WINAPI *PF_ISWOW64)(HANDLE, PBOOL);
typedef DWORD (WINAPI *PF_QUERYDOS)(LPCSTR, LPSTR, DWORD);
typedef BOOL (WINAPI *PF_SETFILEINFO)(HANDLE, int, LPVOID, DWORD);
typedef HRESULT (WINAPI *PF_COINIT)(LPVOID);

#define PF_FILE_ATTRIBUTE_REPARSE   0x00000400
#define PF_FILE_ATTRIBUTE_SPARSE    0x00000200
#define PF_FILE_ATTRIBUTE_COMPRESS  0x00000800
#define PF_FILE_ATTRIBUTE_ENCRYPT   0x00004000
#define PF_ES_SYSTEM_REQUIRED       0x00000001
#define PF_ES_CONTINUOUS            0x80000000

#define WM_APP_LOG      (WM_APP + 1)
#define WM_APP_RESULT   (WM_APP + 2)
#define WM_APP_DONE     (WM_APP + 3)
#define WM_APP_STATUS   (WM_APP + 4)

#define PF_KEY          "Software\\xHCI98\\XHCIPERF"
#define PF_FILENAME     "XHCIPERF.TMP"
#define PF_PREP_CHUNK   1048576u
#define PF_PREP_STEP    (64u * 1048576u)

/* ------------------------------------------------------------------------ */
/* Global state                                                              */
/* ------------------------------------------------------------------------ */

static HINSTANCE g_inst;
static HWND g_wnd;
static HWND g_tab, g_view, g_disk, g_s98, g_log, g_status, g_s98test, g_s98admin;
static HFONT g_font, g_big;
static int g_os;
static int g_preVista;
static DWORD g_ncpu;
static int g_highRes;
static pf_u64 g_freq;
static PF_PARAMS g_p;
static PF_PLAN g_plan;
static PF_RESULT g_res[PF_MAX_STEPS * PF_MAX_PASSES];
static CRITICAL_SECTION g_resCs;        /* g_res and g_s98steps: the controller writes, the window reads */
static volatile LONG g_runBad;          /* a verification failed or could not read: the run's exit is 1 */
static int g_haveResults;
static int g_npasses;
static volatile LONG g_running;
static HANDLE g_ctrl;
static HANDLE g_stopEvent;
static char g_folder[MAX_PATH];
static PF_SAVED g_saved;
static int g_haveSaved;
static char g_savedDiff[200];
static int g_tabSel;
static int g_selftestChecks, g_selftestFails;
static PF_CMDLINE g_cl;
static int g_cliExit;
static char g_s98page[16384];
static char g_s98steps[PF_MAX_STEPS][4096];
static char g_diskText[4096];
static char g_runDate[40];
static char g_system[80];
static char g_driveText[MAX_PATH];
static char g_fsName[32];
static char g_model[96];
static pf_u32 g_sector, g_physSector;
static DWORD g_attributes;
static PF_GETFREEEX g_getFreeEx;
static PF_GETLONGPATH g_getLongPath;
static PF_CANCELIO g_cancelIo;
static PF_SETEXEC g_setExec;
static PF_QUERYDOS g_queryDos;
static PF_SETFILEINFO g_setFileInfo;

/* ------------------------------------------------------------------------ */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------ */

static void sz_cpy(char *d, size_t cap, const char *s)
{
    size_t n = strlen(s);

    if (cap == 0) {
        return;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(d, s, n);
    d[n] = 0;
}

static void sz_cat(char *d, size_t cap, const char *s)
{
    size_t n = strlen(d);

    if (n + 1 < cap) {
        sz_cpy(d + n, cap - n, s);
    }
}

static void sz_u(char *d, size_t cap, pf_u64 v)
{
    char n[24];

    pf_fmt_u64(v, n, sizeof(n));
    sz_cat(d, cap, n);
}

static pf_u64 qpc_now(void)
{
    LARGE_INTEGER c;

    if (!g_highRes) {
        return (pf_u64)GetTickCount();
    }
    QueryPerformanceCounter(&c);
    return (pf_u64)c.QuadPart;
}

static pf_u64 ticks_us(pf_u64 ticks)
{
    if (!g_highRes) {
        return ticks * 1000u;
    }
    return pf_ticks_us(ticks, g_freq);
}

static void post_log(const char *s)
{
    size_t n = strlen(s);
    char *c = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1);

    if (c == NULL) {
        return;
    }
    memcpy(c, s, n + 1);
    if (g_wnd == NULL || !PostMessageA(g_wnd, WM_APP_LOG, 0, (LPARAM)c)) {
        HeapFree(GetProcessHeap(), 0, c);
    }
}

static void post_status(const char *s)
{
    size_t n = strlen(s);
    char *c = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1);

    if (c == NULL) {
        return;
    }
    memcpy(c, s, n + 1);
    if (g_wnd == NULL || !PostMessageA(g_wnd, WM_APP_STATUS, 0, (LPARAM)c)) {
        HeapFree(GetProcessHeap(), 0, c);
    }
}

static void err_text(DWORD e, char *out, size_t cap)
{
    char msg[256];
    DWORD n;

    msg[0] = 0;
    n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, msg,
                       sizeof(msg), NULL);
    while (n > 0 && (msg[n - 1] == '\r' || msg[n - 1] == '\n' || msg[n - 1] == '.' || msg[n - 1] == ' ')) {
        msg[--n] = 0;
    }
    sz_cpy(out, cap, "error ");
    sz_u(out, cap, e);
    if (msg[0] != 0) {
        sz_cat(out, cap, ": ");
        sz_cat(out, cap, msg);
    }
}

static int std_out_ok(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    return h != NULL && h != INVALID_HANDLE_VALUE;
}

static void out_text(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD w;

    if (h != NULL && h != INVALID_HANDLE_VALUE) {
        WriteFile(h, s, (DWORD)strlen(s), &w, NULL);
    }
}

/* ------------------------------------------------------------------------ */
/* The volume and the path (record 29 section 4.1)                           */
/* ------------------------------------------------------------------------ */

static void root_of(const char *path, char *root, size_t cap)
{
    sz_cpy(root, cap, "C:\\");
    if (strlen(path) >= 2 && path[1] == ':') {
        root[0] = path[0];
    }
}

static DWORD serial_of(const char *path)
{
    char root[4];
    DWORD serial = 0, maxc, fl;

    root_of(path, root, sizeof(root));
    if (!GetVolumeInformationA(root, NULL, 0, &serial, &maxc, &fl, NULL, 0)) {
        return 0;
    }
    return serial;
}

static void swap_drive(char *root, size_t cap)
{
    char win[MAX_PATH];
    char pd[8];

    GetWindowsDirectoryA(win, sizeof(win));
    root_of(win, root, cap);
    if (g_os == PF_OS_9X) {
        pd[0] = 0;
        GetPrivateProfileStringA("386Enh", "PagingDrive", "", pd, sizeof(pd), "SYSTEM.INI");
        if (pd[0] != 0) {
            root[0] = pd[0];
        }
    }
}

/* Canonical folder, and a refusal sentence when it may not be tested. */
static int check_folder(const char *in, char *out, size_t cap, char *why, size_t whyCap)
{
    char full[MAX_PATH];
    char lng[MAX_PATH];
    char win[MAX_PATH], sys[MAX_PATH], root[4];
    char *fp;
    UINT dt;
    size_t i;

    if (GetFullPathNameA(in, sizeof(full), full, &fp) == 0) {
        sz_cpy(why, whyCap, "The folder's name is not valid.");
        return 1;
    }
    if (g_getLongPath != NULL && g_getLongPath(full, lng, sizeof(lng)) != 0) {
        sz_cpy(full, sizeof(full), lng);
    }
    if (full[0] == '\\' || strlen(full) < 3 || full[1] != ':') {
        sz_cpy(why, whyCap, "Network folders are not tested; choose a folder on a local drive.");
        return 1;
    }
    root_of(full, root, sizeof(root));
    dt = GetDriveTypeA(root);
    if (dt != DRIVE_FIXED && dt != DRIVE_REMOVABLE && dt != DRIVE_RAMDISK) {
        sz_cpy(why, whyCap, "Only fixed and removable drives are tested; not network, CD or DVD drives.");
        return 1;
    }
    if (g_os == PF_OS_NT && g_queryDos != NULL) {
        char dev[3], target[MAX_PATH];

        dev[0] = full[0];
        dev[1] = ':';
        dev[2] = 0;
        if (g_queryDos(dev, target, sizeof(target)) != 0 && strncmp(target, "\\??\\", 4) == 0) {
            sz_cpy(why, whyCap, "This drive letter is a SUBST alias of a folder; choose the folder's own drive.");
            return 1;
        }
    }
    if (g_os == PF_OS_NT) {
        char part[MAX_PATH];

        for (i = 3; i <= strlen(full); i++) {
            if (full[i] == '\\' || full[i] == 0) {
                DWORD a;

                memcpy(part, full, i);
                part[i] = 0;
                a = GetFileAttributesA(part);
                if (a != 0xFFFFFFFF && (a & PF_FILE_ATTRIBUTE_REPARSE) != 0) {
                    sz_cpy(why, whyCap, "The folder is reached through a junction or a mounted folder; choose the folder on its own drive.");
                    return 1;
                }
            }
        }
    }
    GetWindowsDirectoryA(win, sizeof(win));
    GetSystemDirectoryA(sys, sizeof(sys));
    if (pf_path_under(full, win) || pf_path_under(full, sys)) {
        sz_cpy(why, whyCap, "The Windows folder is not tested; choose another folder, such as the drive's root.");
        return 1;
    }
    sz_cpy(out, cap, full);
    return 0;
}

static void file_path(const char *folder, char *out, size_t cap)
{
    sz_cpy(out, cap, folder);
    if (strlen(out) > 0 && out[strlen(out) - 1] != '\\') {
        sz_cat(out, cap, "\\");
    }
    sz_cat(out, cap, PF_FILENAME);
}

static void read_env(const char *folder, PF_ENV *e)
{
    char root[4], win[MAX_PATH], swap[4];
    DWORD spc = 0, bps = 0, fc = 0, tc = 0, serial = 0, maxc, fl;
    char fs[32];
    MEMORYSTATUS ms;

    memset(e, 0, sizeof(*e));
    e->os = g_os;
    root_of(folder, root, sizeof(root));
    if (GetDiskFreeSpaceA(root, &spc, &bps, &fc, &tc)) {
        e->sector = bps;
        e->freeUser = (pf_u64)fc * spc * bps;
        e->freeTotal = e->freeUser;
        e->volumeBytes = (pf_u64)tc * spc * bps;
    }
    if (g_getFreeEx != NULL) {
        ULARGE_INTEGER a, t, f;

        if (g_getFreeEx(root, &a, &t, &f)) {
            e->freeUser = (pf_u64)a.QuadPart;
            e->volumeBytes = (pf_u64)t.QuadPart;
            e->freeTotal = (pf_u64)f.QuadPart;
        }
    }
    fs[0] = 0;
    if (GetVolumeInformationA(root, NULL, 0, &serial, &maxc, &fl, fs, sizeof(fs))) {
        e->fat = (strncmp(fs, "FAT", 3) == 0);
        sz_cpy(g_fsName, sizeof(g_fsName), fs);
    }
    GetWindowsDirectoryA(win, sizeof(win));
    swap_drive(swap, sizeof(swap));
    e->systemVolume = (serial != 0 && (serial == serial_of(win) || serial == serial_of(swap)));
    e->physSector = g_physSector;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatus(&ms);
    e->memBytes = (pf_u64)ms.dwTotalPhys;
    g_sector = e->sector;
}

/* ------------------------------------------------------------------------ */
/* The disk page (record 29 section 5.4), NT                                 */
/* ------------------------------------------------------------------------ */

static void volume_name(const char *folder, char *out, size_t cap)
{
    sz_cpy(out, cap, "\\\\.\\C:");
    out[4] = folder[0];
}

static void disk_facts(const char *folder, PF_ENV *e)
{
    char vol[16], root[4], label[64], fs[32], t[256];
    DWORD serial = 0, maxc, fl;
    HANDLE h;
    PF_TEXT tx;

    root_of(folder, root, sizeof(root));
    volume_name(folder, vol, sizeof(vol));
    pf_tx_init(&tx, g_diskText, sizeof(g_diskText));
    g_model[0] = 0;
    g_physSector = 0;
    label[0] = 0;
    fs[0] = 0;
    GetVolumeInformationA(root, label, sizeof(label), &serial, &maxc, &fl, fs, sizeof(fs));
    pf_tx_s(&tx, "Drive ");
    pf_tx_s(&tx, root);
    pf_tx_s(&tx, "  label \"");
    pf_tx_s(&tx, label);
    pf_tx_s(&tx, "\"  file system ");
    pf_tx_s(&tx, fs);
    pf_tx_nl(&tx);
    pf_tx_s(&tx, "Sector ");
    pf_tx_u(&tx, e->sector);
    pf_tx_s(&tx, " bytes, size ");
    pf_tx_u(&tx, e->volumeBytes / 1000000u);
    pf_tx_s(&tx, " MB, free ");
    pf_tx_u(&tx, e->freeTotal / 1000000u);
    pf_tx_s(&tx, " MB");
    pf_tx_nl(&tx);
    if (g_os != PF_OS_NT) {
        pf_tx_s(&tx, "Windows 98 and ME report nothing more about the disk.");
        pf_tx_nl(&tx);
        return;
    }
    h = CreateFileA(vol, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        pf_tx_s(&tx, "The disk's properties: not reported by this system.");
        pf_tx_nl(&tx);
        return;
    }
    {
        PF_PROP_QUERY q;
        unsigned char buf[1024];
        DWORD n;

        memset(&q, 0, sizeof(q));
        q.PropertyId = 0;
        memset(buf, 0, sizeof(buf));
        if (DeviceIoControl(h, PF_IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &n, NULL) && n >= 36) {
            DWORD vo = *(DWORD *)(buf + 12), po = *(DWORD *)(buf + 16), ro = *(DWORD *)(buf + 20);
            DWORD bus = *(DWORD *)(buf + 28);

            g_model[0] = 0;
            if (vo != 0 && vo < n) {
                sz_cat(g_model, sizeof(g_model), (char *)buf + vo);
                sz_cat(g_model, sizeof(g_model), " ");
            }
            if (po != 0 && po < n) {
                sz_cat(g_model, sizeof(g_model), (char *)buf + po);
            }
            pf_tx_s(&tx, "Disk: ");
            pf_tx_s(&tx, g_model);
            if (ro != 0 && ro < n) {
                pf_tx_s(&tx, " revision ");
                pf_tx_s(&tx, (char *)buf + ro);
            }
            pf_tx_s(&tx, ", bus type ");
            pf_tx_u(&tx, bus);
            pf_tx_s(&tx, buf[10] ? ", removable" : "");
            pf_tx_nl(&tx);
        }
        q.PropertyId = 1;
        if (DeviceIoControl(h, PF_IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &n, NULL) && n >= 20) {
            pf_tx_s(&tx, "Adapter: largest transfer ");
            pf_tx_u(&tx, *(DWORD *)(buf + 8) / 1024u);
            pf_tx_s(&tx, " KiB, physical pages ");
            pf_tx_u(&tx, *(DWORD *)(buf + 12));
            pf_tx_s(&tx, ", alignment mask ");
            pf_tx_u(&tx, *(DWORD *)(buf + 16));
            pf_tx_nl(&tx);
            e->alignMask = *(DWORD *)(buf + 16);
        }
        q.PropertyId = 6;
        if (DeviceIoControl(h, PF_IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &n, NULL) && n >= 28) {
            g_physSector = *(DWORD *)(buf + 20);
            pf_tx_s(&tx, "Sectors: logical ");
            pf_tx_u(&tx, *(DWORD *)(buf + 16));
            pf_tx_s(&tx, ", physical ");
            pf_tx_u(&tx, g_physSector);
            pf_tx_nl(&tx);
            e->physSector = g_physSector;
        } else {
            pf_tx_s(&tx, "Physical sector: not reported by this system");
            pf_tx_nl(&tx);
        }
        if (DeviceIoControl(h, PF_IOCTL_VOLUME_DISK_EXTENTS, NULL, 0, buf, sizeof(buf), &n, NULL) && n >= 4) {
            pf_tx_s(&tx, "Disk extents: ");
            pf_tx_u(&tx, *(DWORD *)buf);
            pf_tx_nl(&tx);
        }
        {
            PF_SCSI_ADDRESS sa;

            memset(&sa, 0, sizeof(sa));
            sa.Length = sizeof(sa);
            if (DeviceIoControl(h, PF_IOCTL_SCSI_GET_ADDRESS, NULL, 0, &sa, sizeof(sa), &n, NULL)) {
                t[0] = 0;
                sz_cpy(t, sizeof(t), "SCSI port ");
                sz_u(t, sizeof(t), sa.PortNumber);
                sz_cat(t, sizeof(t), ", path ");
                sz_u(t, sizeof(t), sa.PathId);
                sz_cat(t, sizeof(t), ", target ");
                sz_u(t, sizeof(t), sa.TargetId);
                sz_cat(t, sizeof(t), ", lun ");
                sz_u(t, sizeof(t), sa.Lun);
                pf_tx_s(&tx, t);
                pf_tx_nl(&tx);
            }
        }
    }
    CloseHandle(h);
}

/* ------------------------------------------------------------------------ */
/* Stor98's counters (record 29 section 6.2)                                 */
/* ------------------------------------------------------------------------ */

#define S98_ST_NONE     0   /* not Stor98's disk */
#define S98_ST_OK       1
#define S98_ST_ADMIN    2   /* NT: needs administrator rights */
#define S98_ST_NOASPI   3   /* 9x: no ASPI layer */
#define S98_ST_AMBIG    4   /* 9x: the disk's adapter could not be told apart */
#define S98_ST_DEAD     5   /* did not answer in time */

static int g_s98state;
static char g_s98drive;
static UCHAR g_s98port, g_s98path, g_s98target, g_s98lun;
static HMODULE g_aspi;
static PF_SENDASPI g_sendAspi;
static BYTE g_aspiHa, g_aspiTarget;

typedef struct s98_req {
    int ok;
    unsigned char data[2048];
    DWORD len;
} S98_REQ;

static int devicemap_is_stor98(UCHAR port)
{
    char key[96], val[64];
    HKEY k;
    DWORD type, n = sizeof(val);
    int ok = 0;

    sz_cpy(key, sizeof(key), "HARDWARE\\DEVICEMAP\\Scsi\\Scsi Port ");
    sz_u(key, sizeof(key), port);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return 0;
    }
    if (RegQueryValueExA(k, "Driver", NULL, &type, (LPBYTE)val, &n) == ERROR_SUCCESS && type == REG_SZ) {
        val[sizeof(val) - 1] = 0;
        ok = (lstrcmpiA(val, "stor98") == 0);
    }
    RegCloseKey(k);
    return ok;
}

static int nt_address(HANDLE h, PF_SCSI_ADDRESS *sa)
{
    DWORD n;

    memset(sa, 0, sizeof(*sa));
    sa->Length = sizeof(*sa);
    return DeviceIoControl(h, PF_IOCTL_SCSI_GET_ADDRESS, NULL, 0, sa, sizeof(*sa), &n, NULL) != 0;
}

static void nt_find(const char *folder)
{
    char vol[16];
    HANDLE h;
    PF_SCSI_ADDRESS sa;

    g_s98state = S98_ST_NONE;
    volume_name(folder, vol, sizeof(vol));
    h = CreateFileA(vol, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    if (nt_address(h, &sa) && devicemap_is_stor98(sa.PortNumber)) {
        unsigned char buf[256];
        DWORD n;

        g_s98port = sa.PortNumber;
        g_s98path = sa.PathId;
        g_s98target = sa.TargetId;
        g_s98lun = sa.Lun;
        g_s98drive = folder[0];
        /* Only a volume shown to lie on one disk is attributed to it */
        if (DeviceIoControl(h, PF_IOCTL_VOLUME_DISK_EXTENTS, NULL, 0, buf, sizeof(buf), &n, NULL) && n >= 8 &&
            *(DWORD *)buf == 1) {
            g_s98state = S98_ST_OK;
        }
    }
    CloseHandle(h);
}

static int nt_read(S98_REQ *rq)
{
    char vol[16];
    HANDLE h;
    PF_SCSI_ADDRESS sa;
    PF_SPT_BUF b;
    DWORD n;
    int ok = 0;

    volume_name("C:", vol, sizeof(vol));
    vol[4] = g_s98drive;
    h = CreateFileA(vol, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_ACCESS_DENIED) {
            g_s98state = S98_ST_ADMIN;
        }
        return 0;
    }
    /* the same volume, the same port and target, still Stor98's */
    if (!nt_address(h, &sa) || sa.PortNumber != g_s98port || sa.PathId != g_s98path || sa.TargetId != g_s98target ||
        sa.Lun != g_s98lun || !devicemap_is_stor98(sa.PortNumber)) {
        CloseHandle(h);
        g_s98state = S98_ST_NONE;
        return 0;
    }
    memset(&b, 0, sizeof(b));
    b.spt.Length = sizeof(PF_SPT);
    b.spt.PathId = sa.PathId;
    b.spt.TargetId = sa.TargetId;
    b.spt.Lun = sa.Lun;
    b.spt.CdbLength = 10;
    b.spt.SenseInfoLength = sizeof(b.sense);
    b.spt.DataIn = 1;
    b.spt.DataTransferLength = S98_ANSWER_BYTES;
    b.spt.TimeOutValue = 2;
    b.spt.DataBufferOffset = (ULONG_PTR)((char *)b.data - (char *)&b);
    b.spt.SenseInfoOffset = (ULONG)((char *)b.sense - (char *)&b);
    b.spt.Cdb[0] = 0x3C;
    b.spt.Cdb[1] = 0x02;
    b.spt.Cdb[2] = S98_BUFFER_ID;
    b.spt.Cdb[6] = (UCHAR)(S98_ANSWER_BYTES >> 16);
    b.spt.Cdb[7] = (UCHAR)(S98_ANSWER_BYTES >> 8);
    b.spt.Cdb[8] = (UCHAR)S98_ANSWER_BYTES;
    if (DeviceIoControl(h, PF_IOCTL_SCSI_PASS_THROUGH, &b, sizeof(b), &b, sizeof(b), &n, NULL) && b.spt.ScsiStatus == 0) {
        rq->len = b.spt.DataTransferLength;
        if (rq->len > sizeof(rq->data)) {
            rq->len = sizeof(rq->data);
        }
        memcpy(rq->data, b.data, rq->len);
        ok = 1;
    }
    CloseHandle(h);
    return ok;
}

static int aspi_exec(BYTE ha, BYTE target, const BYTE *cdb, int cdbLen, BYTE *buf, DWORD len, DWORD *got)
{
    PF_SRB_EXEC s;
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD rc;

    *got = 0;
    if (ev == NULL) {
        return 0;
    }
    memset(&s, 0, sizeof(s));
    s.Cmd = PF_SC_EXEC_SCSI_CMD;
    s.HaId = ha;
    s.Target = target;
    s.Lun = 0;
    s.Flags = PF_SRB_DIR_IN | PF_SRB_EVENT_NOTIFY;
    s.BufLen = len;
    s.BufPointer = buf;
    s.SenseLen = 14;
    s.CdbLen = (BYTE)cdbLen;
    s.PostProc = (void *)ev;
    memcpy(s.Cdb, cdb, cdbLen);
    rc = g_sendAspi(&s);
    if (rc == PF_SS_PENDING) {
        if (WaitForSingleObject(ev, 2000) != WAIT_OBJECT_0) {
            PF_SRB_ABORT ab;

            memset(&ab, 0, sizeof(ab));
            ab.Cmd = PF_SC_ABORT_SRB;
            ab.HaId = ha;
            ab.ToAbort = &s;
            g_sendAspi(&ab);
            /* the request owns s and buf until ASPI says it is done */
            WaitForSingleObject(ev, INFINITE);
            CloseHandle(ev);
            g_s98state = S98_ST_DEAD;
            return 0;
        }
    }
    CloseHandle(ev);
    if (s.Status != PF_SS_COMP || s.TargStat != 0) {
        return 0;
    }
    *got = len;
    return 1;
}

static void norm(const char *in, size_t n, char *out, size_t cap)
{
    size_t i, j = 0;

    for (i = 0; i < n && in[i] != 0 && j + 1 < cap; i++) {
        char c = in[i];

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out[j++] = c;
        }
    }
    out[j] = 0;
}

/* 9x: the Enum\SCSI device id whose instance holds the drive letter */
static int devnode_of_letter(char letter, char *id, size_t cap)
{
    HKEY scsi, dev, inst;
    char dn[256], in[256], v[32];
    DWORD i, j, n, type;
    int found = 0;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum\\SCSI", 0, KEY_READ, &scsi) != ERROR_SUCCESS) {
        return 0;
    }
    for (i = 0; !found && RegEnumKeyA(scsi, i, dn, sizeof(dn)) == ERROR_SUCCESS; i++) {
        if (RegOpenKeyExA(scsi, dn, 0, KEY_READ, &dev) != ERROR_SUCCESS) {
            continue;
        }
        for (j = 0; !found && RegEnumKeyA(dev, j, in, sizeof(in)) == ERROR_SUCCESS; j++) {
            if (RegOpenKeyExA(dev, in, 0, KEY_READ, &inst) != ERROR_SUCCESS) {
                continue;
            }
            n = sizeof(v);
            if (RegQueryValueExA(inst, "CurrentDriveLetterAssignment", NULL, &type, (LPBYTE)v, &n) == ERROR_SUCCESS &&
                type == REG_SZ) {
                v[sizeof(v) - 1] = 0;
                if (strchr(v, letter) != NULL || strchr(v, (char)(letter - 'A' + 'a')) != NULL) {
                    sz_cpy(id, cap, dn);
                    found = 1;
                }
            }
            RegCloseKey(inst);
        }
        RegCloseKey(dev);
    }
    RegCloseKey(scsi);
    return found;
}

static void aspi_find(const char *folder)
{
    PF_GETASPIINFO info;
    DWORD si;
    BYTE nha, ha, t;
    char devid[256], ndev[256];
    int matches = 0;
    char letter = folder[0];

    if (letter >= 'a' && letter <= 'z') {
        letter = (char)(letter - 'a' + 'A');
    }
    g_s98state = S98_ST_NONE;
    if (!devnode_of_letter(letter, devid, sizeof(devid))) {
        return;
    }
    norm(devid, strlen(devid), ndev, sizeof(ndev));
    if (g_aspi == NULL) {
        g_aspi = LoadLibraryA("WNASPI32.DLL");
    }
    if (g_aspi == NULL) {
        g_s98state = S98_ST_NOASPI;
        return;
    }
    info = (PF_GETASPIINFO)GetProcAddress(g_aspi, "GetASPI32SupportInfo");
    g_sendAspi = (PF_SENDASPI)GetProcAddress(g_aspi, "SendASPI32Command");
    if (info == NULL || g_sendAspi == NULL) {
        g_s98state = S98_ST_NOASPI;
        return;
    }
    si = info();
    if (((si >> 8) & 0xFF) != PF_SS_COMP) {
        g_s98state = S98_ST_NOASPI;
        return;
    }
    nha = (BYTE)(si & 0xFF);
    for (ha = 0; ha < nha; ha++) {
        for (t = 0; t < 8; t++) {
            PF_SRB_GDEV g;
            BYTE cdb[6];
            BYTE page[64], std[36];
            DWORD got;
            pf_u32 pci;
            char want[64], v[32];

            memset(&g, 0, sizeof(g));
            g.Cmd = PF_SC_GET_DEV_TYPE;
            g.HaId = ha;
            g.Target = t;
            if (g_sendAspi(&g) != PF_SS_COMP || g.Status != PF_SS_COMP || g.DeviceType != 0) {
                continue;
            }
            /* INQUIRY page 00h, then C9h: the proof before any READ BUFFER */
            memset(cdb, 0, sizeof(cdb));
            cdb[0] = 0x12;
            cdb[1] = 0x01;
            cdb[4] = sizeof(page);
            memset(page, 0, sizeof(page));
            if (!aspi_exec(ha, t, cdb, 6, page, sizeof(page), &got) || !s98_lists_identity(page, sizeof(page))) {
                continue;
            }
            cdb[2] = S98_VPD_PAGE;
            cdb[4] = S98_VPD_LEN;
            memset(page, 0, sizeof(page));
            if (!aspi_exec(ha, t, cdb, 6, page, S98_VPD_LEN, &got) || !s98_identity(page, S98_VPD_LEN, &pci)) {
                continue;
            }
            cdb[1] = 0;
            cdb[2] = 0;
            cdb[4] = sizeof(std);
            memset(std, 0, sizeof(std));
            if (!aspi_exec(ha, t, cdb, 6, std, sizeof(std), &got)) {
                continue;
            }
            norm((char *)std + 8, 24, v, sizeof(v));
            sz_cpy(want, sizeof(want), v);
            if (want[0] != 0 && strstr(ndev, want) != NULL) {
                matches++;
                g_aspiHa = ha;
                g_aspiTarget = t;
            }
        }
    }
    if (matches == 1) {
        g_s98state = S98_ST_OK;
    } else if (matches > 1) {
        g_s98state = S98_ST_AMBIG;
    }
}

static int aspi_read(S98_REQ *rq)
{
    BYTE cdb[10];
    BYTE page[S98_VPD_LEN];
    DWORD got;
    pf_u32 pci;

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x12;
    cdb[1] = 0x01;
    cdb[2] = S98_VPD_PAGE;
    cdb[4] = S98_VPD_LEN;
    if (!aspi_exec(g_aspiHa, g_aspiTarget, cdb, 6, page, S98_VPD_LEN, &got) || !s98_identity(page, S98_VPD_LEN, &pci)) {
        return 0;
    }
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x3C;
    cdb[1] = 0x02;
    cdb[2] = S98_BUFFER_ID;
    cdb[6] = (BYTE)(S98_ANSWER_BYTES >> 16);
    cdb[7] = (BYTE)(S98_ANSWER_BYTES >> 8);
    cdb[8] = (BYTE)S98_ANSWER_BYTES;
    if (!aspi_exec(g_aspiHa, g_aspiTarget, cdb, 10, rq->data, S98_ANSWER_BYTES, &got)) {
        return 0;
    }
    rq->len = got;
    return 1;
}

static unsigned __stdcall s98_thread(void *arg)
{
    S98_REQ *rq = (S98_REQ *)arg;

    rq->ok = (g_os == PF_OS_9X) ? aspi_read(rq) : nt_read(rq);
    return 0;
}

/* One counter read, never longer than 2 s for the caller; a read that does
 * not return keeps its thread and its buffer (record 29 section 6.2). */
static int s98_read(S98_ANSWER *a)
{
    S98_REQ *rq;
    HANDLE th;
    unsigned id;
    int ok = 0;

    if (g_s98state != S98_ST_OK) {
        return 0;
    }
    rq = (S98_REQ *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(S98_REQ));
    if (rq == NULL) {
        return 0;
    }
    th = (HANDLE)_beginthreadex(NULL, 0, s98_thread, rq, 0, &id);
    if (th == NULL) {
        HeapFree(GetProcessHeap(), 0, rq);
        return 0;
    }
    if (WaitForSingleObject(th, 3000) != WAIT_OBJECT_0) {
        g_s98state = S98_ST_DEAD;
        CloseHandle(th);
        return 0;
    }
    CloseHandle(th);
    if (rq->ok && s98_parse(rq->data, rq->len, a)) {
        ok = 1;
    }
    HeapFree(GetProcessHeap(), 0, rq);
    return ok;
}

typedef struct s98_find_req {
    char folder[MAX_PATH];
} S98_FIND_REQ;

static unsigned __stdcall s98_find_thread(void *arg)
{
    aspi_find(((S98_FIND_REQ *)arg)->folder);
    return 0;
}

/* On 9x the search asks each ASPI target and may wait on one that does
 * not answer: it runs on a thread of its own, the caller waits at most
 * 5 s, and a search that has not returned keeps its thread and request. */
static void s98_find(const char *folder)
{
    S98_FIND_REQ *rq;
    HANDLE th;
    unsigned id;

    if (g_os != PF_OS_9X) {
        nt_find(folder);
        return;
    }
    if (g_s98state == S98_ST_DEAD) {
        return;
    }
    rq = (S98_FIND_REQ *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(S98_FIND_REQ));
    if (rq == NULL) {
        return;
    }
    sz_cpy(rq->folder, sizeof(rq->folder), folder);
    th = (HANDLE)_beginthreadex(NULL, 0, s98_find_thread, rq, 0, &id);
    if (th == NULL) {
        HeapFree(GetProcessHeap(), 0, rq);
        return;
    }
    if (WaitForSingleObject(th, 5000) != WAIT_OBJECT_0) {
        g_s98state = S98_ST_DEAD;
        CloseHandle(th);
        return;
    }
    CloseHandle(th);
    HeapFree(GetProcessHeap(), 0, rq);
}

static const char *s98_state_text(void)
{
    switch (g_s98state) {
    case S98_ST_ADMIN:
        return "This disk is driven by Stor98. Reading its counters needs administrator rights.";
    case S98_ST_NOASPI:
        return "Windows' ASPI layer (WNASPI32.DLL) is not here, so Stor98's counters cannot be read.";
    case S98_ST_AMBIG:
        return "This disk's Stor98 adapter could not be told apart from another one.";
    case S98_ST_DEAD:
        return "Stor98 did not answer in time; its counters are not read for the rest of this run.";
    case S98_ST_OK:
        return "";
    default:
        return "This disk is not driven by Stor98; the Stor98 page has nothing to show.";
    }
}

/* ------------------------------------------------------------------------ */
/* The run                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct run RUN;

typedef struct worker {
    RUN *run;
    int index;
    HANDLE h;
    HANDLE thread;
    pf_u64 partStart;
    pf_u64 partBytes;
    pf_u64 next;
    pf_u32 rnd;
    pf_u64 budgetLeft;
    pf_u64 bytes;
    pf_u64 reqs;
    pf_u64 stampUs;
    PF_LAT lat;
    DWORD error;
    int failed;
    int skipped;
    pf_u32 bad;
    pf_u64 inflight[PF_MAX_DEPTH];
    int busy[PF_MAX_DEPTH];
} WORKER;

struct run {
    char path[MAX_PATH];
    HANDLE anchor;
    pf_u32 runId;
    pf_u32 runHi;
    DWORD volSerial;
    DWORD indexLo, indexHi;
    const PF_STEP *step;
    int stepIndex;
    pf_u64 region;
    volatile LONG issueEnd;
    HANDLE go;
    HANDLE stepEnd;
    CRITICAL_SECTION cs;
    PF_GEN gen;
    int genOut;
    pf_u32 *map;
    pf_u32 mapBlock;
    pf_u64 mapBlocks;
    unsigned char *bufNormal;
    unsigned char *bufScatter;
    size_t stride;
    unsigned char *vbuf;
    pf_u32 vbufBytes;
    pf_u32 sector;
    int unbuffered;
    int async;
    int failed;
    DWORD failError;
    WORKER w[PF_MAX_THREADS];
    pf_u32 incomplete;
    int s98ok;
    S98_ANSWER s98before;
};

static RUN g_run;

static int stopped(void)
{
    return WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0;
}

static unsigned char *slot_buf(RUN *r, int thread, int slot, int buffer, int depth)
{
    unsigned char *base = (buffer == PF_BUF_SCATTER && r->bufScatter != NULL) ? r->bufScatter : r->bufNormal;
    unsigned char *p = base + (size_t)(thread * depth + slot) * r->stride;

    if (buffer == PF_BUF_OFFSET) {
        p += r->sector;
    }
    return p;
}

static void map_mark(RUN *r, pf_u64 off, pf_u32 size, pf_u32 gen)
{
    pf_u64 b, e;

    if (r->map == NULL) {
        return;
    }
    b = (off - PF_HEADER_BYTES) / r->mapBlock;
    e = b + size / r->mapBlock;
    for (; b < e && b < r->mapBlocks; b++) {
        r->map[b] = gen;
    }
}

static pf_u32 check_read(RUN *r, const unsigned char *buf, pf_u64 off, pf_u32 size, pf_u32 *firstBadOut)
{
    pf_u32 bad = 0, i, fb, fg;

    for (i = 0; i < size; i += r->mapBlock) {
        pf_u64 blk = (off + i - PF_HEADER_BYTES) / r->mapBlock;
        pf_u32 gen;

        if (blk >= r->mapBlocks) {
            break;
        }
        gen = r->map[blk];
        if (gen == PF_GEN_UNKNOWN) {
            r->incomplete++;
            continue;
        }
        if (pf_check(buf + i, r->mapBlock, r->runId, (off + i) / PF_UNIT, gen, &fb, &fg) != 0) {
            if (bad == 0 && firstBadOut != NULL) {
                *firstBadOut = (pf_u32)((off + i) / PF_UNIT) + fb;
            }
            bad++;
        }
    }
    return bad;
}

/* The next request's offset, or 0 when nothing may be issued. */
static int pick(WORKER *w, pf_u64 *off, int write, int depth)
{
    RUN *r = w->run;
    const PF_STEP *s = r->step;
    pf_u64 o;
    int k, tries;

    if (r->issueEnd || WaitForSingleObject(r->stepEnd, 0) == WAIT_OBJECT_0 || stopped()) {
        return 0;
    }
    if (s->budget != 0 && w->budgetLeft < s->size) {
        return 0;
    }
    for (tries = 0;; tries++) {
        if (s->kind == PF_RND) {
            pf_u64 blocks = w->partBytes / s->size;

            o = w->partStart + ((pf_u64)pf_xs_next(&w->rnd) % blocks) * s->size;
        } else {
            o = w->partStart + w->next;
        }
        if (write) {
            int clash = 0;

            for (k = 0; k < depth; k++) {
                if (w->busy[k] && w->inflight[k] == o) {
                    clash = 1;
                }
            }
            if (clash) {
                if (s->kind == PF_RND && tries < 8) {
                    continue;
                }
                return -1;      /* wait for a completion first */
            }
        }
        break;
    }
    if (s->kind != PF_RND) {
        w->next += s->size;
        if (w->next + s->size > w->partBytes) {
            w->next = 0;
        }
    }
    if (s->budget != 0) {
        w->budgetLeft -= s->size;
    }
    *off = o;
    return 1;
}

static int prep_write_buf(WORKER *w, unsigned char *buf, pf_u64 off, pf_u32 size, pf_u32 *gen)
{
    RUN *r = w->run;
    pf_u64 t0;

    *gen = 0;
    if (g_p.pattern != PF_PAT_VERIFY) {
        return 1;
    }
    EnterCriticalSection(&r->cs);
    *gen = pf_gen_take(&r->gen);
    if (*gen == 0) {
        r->genOut = 1;
    }
    LeaveCriticalSection(&r->cs);
    if (*gen == 0) {
        SetEvent(r->stepEnd);
        SetEvent(g_stopEvent);
        return 0;
    }
    t0 = qpc_now();
    pf_stamp(buf, size, r->runId, off / PF_UNIT, *gen);
    w->stampUs += ticks_us(qpc_now() - t0);
    return 1;
}

static void worker_fail(WORKER *w, DWORD e)
{
    RUN *r = w->run;

    if (r->step->buffer == PF_BUF_OFFSET && (e == ERROR_INVALID_PARAMETER || e == ERROR_NOT_SUPPORTED)) {
        w->skipped = 1;
        SetEvent(r->stepEnd);
        return;
    }
    w->failed = 1;
    w->error = e;
    r->failed = 1;
    r->failError = e;
    SetEvent(r->stepEnd);
    SetEvent(g_stopEvent);
}

static void complete_one(WORKER *w, unsigned char *buf, pf_u64 off, pf_u32 gen, int write, pf_u64 issued, int ok,
                         DWORD got)
{
    RUN *r = w->run;
    const PF_STEP *s = r->step;
    pf_u64 us = ticks_us(qpc_now() - issued);

    if (!ok || got != s->size) {
        if (write) {
            map_mark(r, off, s->size, PF_GEN_UNKNOWN);
        }
        return;
    }
    if (write && g_p.pattern == PF_PAT_VERIFY) {
        map_mark(r, off, s->size, gen);
    }
    if (!write && g_p.verify && g_p.verifyDuring && r->map != NULL) {
        pf_u32 fb = 0;
        pf_u32 bad = check_read(r, buf, off, s->size, &fb);

        if (bad != 0) {
            w->bad += bad;
        }
    }
    w->bytes += s->size;
    w->reqs++;
    pf_lat_add(&w->lat, us);
}

static void worker_sync(WORKER *w)
{
    RUN *r = w->run;
    const PF_STEP *s = r->step;
    int write = (s->op == PF_OP_WRITE);
    unsigned char *buf = slot_buf(r, w->index, 0, s->buffer, s->depth);

    for (;;) {
        pf_u64 off, t0;
        pf_u32 gen = 0;
        LONG hi;
        DWORD got = 0;
        BOOL ok;
        int pk = pick(w, &off, write, 1);

        if (pk <= 0) {
            break;
        }
        if (write && !prep_write_buf(w, buf, off, s->size, &gen)) {
            break;
        }
        hi = (LONG)(off >> 32);
        if (SetFilePointer(w->h, (LONG)(off & 0xFFFFFFFFu), &hi, FILE_BEGIN) == 0xFFFFFFFF && GetLastError() != NO_ERROR) {
            worker_fail(w, GetLastError());
            break;
        }
        t0 = qpc_now();
        ok = write ? WriteFile(w->h, buf, s->size, &got, NULL) : ReadFile(w->h, buf, s->size, &got, NULL);
        if (!ok || got != s->size) {
            DWORD e = ok ? ERROR_HANDLE_EOF : GetLastError();

            complete_one(w, buf, off, gen, write, t0, 0, got);
            worker_fail(w, e);
            break;
        }
        complete_one(w, buf, off, gen, write, t0, 1, got);
    }
}

static void worker_async(WORKER *w)
{
    RUN *r = w->run;
    const PF_STEP *s = r->step;
    int write = (s->op == PF_OP_WRITE);
    int depth = s->depth;
    OVERLAPPED ov[PF_MAX_DEPTH];
    HANDLE ev[PF_MAX_DEPTH + 1];
    pf_u64 issued[PF_MAX_DEPTH];
    pf_u32 gens[PF_MAX_DEPTH];
    int k, stopping = 0;

    for (k = 0; k < depth; k++) {
        memset(&ov[k], 0, sizeof(ov[k]));
        ov[k].hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        w->busy[k] = 0;
    }
    for (;;) {
        int npend = 0;
        HANDLE wl[PF_MAX_DEPTH + 2];
        int wk[PF_MAX_DEPTH];
        DWORD rc;

        /* fill the free slots */
        for (k = 0; k < depth && !stopping; k++) {
            pf_u64 off;
            pf_u32 gen = 0;
            unsigned char *buf;
            BOOL ok;
            DWORD got;
            int pk;

            if (w->busy[k]) {
                continue;
            }
            pk = pick(w, &off, write, depth);
            if (pk == 0) {
                stopping = 1;
                break;
            }
            if (pk < 0) {
                break;
            }
            buf = slot_buf(r, w->index, k, s->buffer, depth);
            if (write && !prep_write_buf(w, buf, off, s->size, &gen)) {
                stopping = 1;
                break;
            }
            ov[k].Offset = (DWORD)(off & 0xFFFFFFFFu);
            ov[k].OffsetHigh = (DWORD)(off >> 32);
            ResetEvent(ov[k].hEvent);
            w->inflight[k] = off;
            gens[k] = gen;
            issued[k] = qpc_now();
            ok = write ? WriteFile(w->h, buf, s->size, &got, &ov[k]) : ReadFile(w->h, buf, s->size, &got, &ov[k]);
            if (!ok && GetLastError() != ERROR_IO_PENDING) {
                DWORD e = GetLastError();

                complete_one(w, buf, off, gen, write, issued[k], 0, 0);
                worker_fail(w, e);
                stopping = 1;
                break;
            }
            w->busy[k] = 1;
        }
        for (k = 0; k < depth; k++) {
            if (w->busy[k]) {
                wl[npend] = ov[k].hEvent;
                wk[npend] = k;
                npend++;
            }
        }
        if (npend == 0) {
            break;
        }
        wl[npend] = r->stepEnd;
        wl[npend + 1] = g_stopEvent;
        rc = WaitForMultipleObjects(npend + (stopping ? 0 : 2), wl, FALSE, INFINITE);
        if (rc == WAIT_OBJECT_0 + (DWORD)npend || rc == WAIT_OBJECT_0 + (DWORD)npend + 1) {
            /* Stop or a failure elsewhere: cancel our own, then collect each */
            if (g_cancelIo != NULL) {
                g_cancelIo(w->h);
            }
            stopping = 1;
            continue;
        }
        if (rc >= WAIT_OBJECT_0 && rc < WAIT_OBJECT_0 + (DWORD)npend) {
            DWORD got = 0;
            BOOL ok;

            k = wk[rc - WAIT_OBJECT_0];
            ok = GetOverlappedResult(w->h, &ov[k], &got, TRUE);
            w->busy[k] = 0;
            complete_one(w, slot_buf(r, w->index, k, s->buffer, depth), w->inflight[k], gens[k], write, issued[k],
                         ok, got);
            if (!ok || got != s->size) {
                DWORD e = ok ? ERROR_HANDLE_EOF : GetLastError();

                if (e != ERROR_OPERATION_ABORTED) {
                    worker_fail(w, e);
                }
                stopping = 1;
            }
        } else {
            /* the wait itself failed: collect everything, synchronously */
            for (k = 0; k < depth; k++) {
                if (w->busy[k]) {
                    DWORD got;

                    GetOverlappedResult(w->h, &ov[k], &got, TRUE);
                    w->busy[k] = 0;
                    if (write) {
                        map_mark(r, w->inflight[k], s->size, PF_GEN_UNKNOWN);
                    }
                }
            }
            break;
        }
    }
    for (k = 0; k < depth; k++) {
        CloseHandle(ov[k].hEvent);
    }
    (void)ev;
}

static unsigned __stdcall worker_main(void *arg)
{
    WORKER *w = (WORKER *)arg;

    WaitForSingleObject(w->run->go, INFINITE);
    if (w->run->async) {
        worker_async(w);
    } else {
        worker_sync(w);
    }
    return 0;
}

static HANDLE open_test(RUN *r, int async, int unbuffered, int wt)
{
    DWORD fl = FILE_ATTRIBUTE_NORMAL;
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;

    if (unbuffered) {
        fl |= FILE_FLAG_NO_BUFFERING;
    }
    if (wt) {
        fl |= FILE_FLAG_WRITE_THROUGH;
    }
    if (async) {
        fl |= FILE_FLAG_OVERLAPPED;
    }
    if (g_os == PF_OS_NT) {
        share |= FILE_SHARE_DELETE;
    }
    return CreateFileA(r->path, GENERIC_READ | GENERIC_WRITE, share, NULL, OPEN_EXISTING, fl, NULL);
}

/* Every reopened handle is checked against the file this run created
 * (record 29 section 3.1). */
static int handle_is_ours(RUN *r, HANDLE h, int async)
{
    BY_HANDLE_FILE_INFORMATION bi;

    if (!GetFileInformationByHandle(h, &bi) || bi.dwVolumeSerialNumber != r->volSerial) {
        return 0;
    }
    if (g_os == PF_OS_NT) {
        return bi.nFileIndexLow == r->indexLo && bi.nFileIndexHigh == r->indexHi;
    }
    if (!async) {
        PF_HEADER hd;
        DWORD got = 0;

        SetFilePointer(h, 0, NULL, FILE_BEGIN);
        if (!ReadFile(h, r->vbuf, r->sector < PF_UNIT ? PF_UNIT : r->sector, &got, NULL) ||
            !pf_header_parse(r->vbuf, got, &hd) || hd.runLo != r->runId || hd.runHi != r->runHi) {
            return 0;
        }
    }
    return 1;
}

static void record_put(RUN *r, pf_u64 planned, pf_u64 prepared)
{
    HKEY k;
    unsigned char rec[48];

    if (RegCreateKeyA(HKEY_CURRENT_USER, PF_KEY, &k) != ERROR_SUCCESS) {
        return;
    }
    memset(rec, 0, sizeof(rec));
    memcpy(rec, "XPL1", 4);
    memcpy(rec + 4, &r->runId, 4);
    memcpy(rec + 8, &r->runHi, 4);
    memcpy(rec + 12, &planned, 8);
    memcpy(rec + 20, &prepared, 8);
    memcpy(rec + 28, &r->volSerial, 4);
    memcpy(rec + 32, &r->indexLo, 4);
    memcpy(rec + 36, &r->indexHi, 4);
    RegSetValueExA(k, "Leftover", 0, REG_BINARY, rec, sizeof(rec));
    RegSetValueExA(k, "LeftoverPath", 0, REG_SZ, (const BYTE *)r->path, (DWORD)strlen(r->path) + 1);
    RegCloseKey(k);
}

static void record_clear(void)
{
    HKEY k;

    if (RegOpenKeyExA(HKEY_CURRENT_USER, PF_KEY, 0, KEY_WRITE, &k) == ERROR_SUCCESS) {
        RegDeleteValueA(k, "Leftover");
        RegDeleteValueA(k, "LeftoverPath");
        RegCloseKey(k);
    }
}

static int record_get(PF_RECORD *rec, char *path, size_t cap)
{
    HKEY k;
    unsigned char b[48];
    DWORD n = sizeof(b), type;
    int ok = 0;

    memset(rec, 0, sizeof(*rec));
    if (RegOpenKeyExA(HKEY_CURRENT_USER, PF_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return 0;
    }
    if (RegQueryValueExA(k, "Leftover", NULL, &type, b, &n) == ERROR_SUCCESS && type == REG_BINARY && n == sizeof(b) &&
        memcmp(b, "XPL1", 4) == 0) {
        rec->present = 1;
        memcpy(&rec->runLo, b + 4, 4);
        memcpy(&rec->runHi, b + 8, 4);
        memcpy(&rec->plannedBytes, b + 12, 8);
        memcpy(&rec->preparedBytes, b + 20, 8);
        memcpy(&rec->volSerial, b + 28, 4);
        memcpy(&rec->indexLo, b + 32, 4);
        memcpy(&rec->indexHi, b + 36, 4);
        n = (DWORD)cap;
        if (RegQueryValueExA(k, "LeftoverPath", NULL, &type, (LPBYTE)path, &n) == ERROR_SUCCESS && type == REG_SZ) {
            path[cap - 1] = 0;
            ok = 1;
        }
    }
    RegCloseKey(k);
    return ok;
}

/* An existing XHCIPERF.TMP: 0 gone (deleted or there was none), 1 kept. */
static int leftover_offer(HWND owner, const char *path, int quiet)
{
    HANDLE h;
    unsigned char *b;
    DWORD got = 0;
    PF_HEADER hd;
    PF_RECORD rec;
    char rpath[MAX_PATH];
    BY_HANDLE_FILE_INFORMATION bi;
    int kind, ok;
    char msg[512];
    pf_u64 size;

    /* Opened alone, with delete access where the system has the
     * handle-bound deletion, and kept through the check to the deletion */
    h = CreateFileA(path, GENERIC_READ | (g_setFileInfo != NULL ? DELETE : 0), 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return GetFileAttributesA(path) == 0xFFFFFFFF ? 0 : 1;
    }
    b = (unsigned char *)VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
    ok = b != NULL && ReadFile(h, b, PF_UNIT, &got, NULL) && pf_header_parse(b, got, &hd);
    memset(&bi, 0, sizeof(bi));
    GetFileInformationByHandle(h, &bi);
    size = ((pf_u64)bi.nFileSizeHigh << 32) | bi.nFileSizeLow;
    if (!record_get(&rec, rpath, sizeof(rpath)) || lstrcmpiA(rpath, path) != 0) {
        rec.present = 0;
    }
    kind = pf_leftover(&hd, ok, rec.present ? &rec : NULL, size, bi.nFileIndexLow, bi.nFileIndexHigh,
                       g_os == PF_OS_NT);
    if (b != NULL) {
        VirtualFree(b, 0, MEM_RELEASE);
    }
    if (kind == PF_LEFT_FOREIGN) {
        CloseHandle(h);
        return 1;
    }
    if (quiet) {
        CloseHandle(h);
        return 1;
    }
    sz_cpy(msg, sizeof(msg), kind == PF_LEFT_OURS ? "A test file from an earlier run is still here:\r\n\r\n" :
           "This looks like a test file of this tool's, but this computer has no record of creating it:\r\n\r\n");
    sz_cat(msg, sizeof(msg), path);
    sz_cat(msg, sizeof(msg), " (");
    sz_u(msg, sizeof(msg), size / 1000000u);
    sz_cat(msg, sizeof(msg), " MB)\r\n\r\nDelete it?");
    if (MessageBoxA(owner, msg, "xHCI98 Disk Benchmark", MB_YESNO | (kind == PF_LEFT_OURS ? MB_ICONQUESTION : MB_ICONWARNING)) != IDYES) {
        CloseHandle(h);
        return 1;
    }
    if (g_setFileInfo != NULL) {
        BYTE del = 1;

        g_setFileInfo(h, 4, &del, sizeof(del));
        CloseHandle(h);
    } else {
        CloseHandle(h);
        DeleteFileA(path);
    }
    if (GetFileAttributesA(path) == 0xFFFFFFFF) {
        record_clear();
        return 0;
    }
    return 1;
}

static void cleanup_file(RUN *r)
{
    if (r->anchor != NULL && r->anchor != INVALID_HANDLE_VALUE) {
        CloseHandle(r->anchor);
        r->anchor = NULL;
    }
    if (r->path[0] != 0 && GetFileAttributesA(r->path) != 0xFFFFFFFF) {
        DeleteFileA(r->path);
    }
    if (r->path[0] != 0 && GetFileAttributesA(r->path) == 0xFFFFFFFF) {
        record_clear();
    } else if (r->path[0] != 0) {
        post_log("The test file could not be deleted; the next start offers to delete it.");
    }
}

static int alloc_buffers(RUN *r, const PF_PLAN *pl, int scatter)
{
    size_t page = 4096;
    size_t n;

    r->stride = ((size_t)pl->maxSize + page - 1) / page * page + page;
    n = r->stride * (pl->slots + 1);
    r->bufNormal = (unsigned char *)VirtualAlloc(NULL, n, MEM_COMMIT, PAGE_READWRITE);
    r->vbufBytes = PF_PREP_CHUNK > pl->maxSize ? PF_PREP_CHUNK : pl->maxSize;
    r->vbuf = (unsigned char *)VirtualAlloc(NULL, r->vbufBytes + page, MEM_COMMIT, PAGE_READWRITE);
    if (r->bufNormal == NULL || r->vbuf == NULL) {
        return 0;
    }
    if (g_os == PF_OS_NT) {
        VirtualLock(r->bufNormal, n);
    }
    if (scatter) {
        /* The interleaved first touch (record 29 section 3.4) */
        unsigned char *a = (unsigned char *)VirtualAlloc(NULL, n, MEM_RESERVE, PAGE_READWRITE);
        unsigned char *b = (unsigned char *)VirtualAlloc(NULL, n, MEM_RESERVE, PAGE_READWRITE);
        size_t i;

        if (a == NULL || b == NULL) {
            if (a != NULL) {
                VirtualFree(a, 0, MEM_RELEASE);
            }
            if (b != NULL) {
                VirtualFree(b, 0, MEM_RELEASE);
            }
            return 0;
        }
        for (i = 0; i < n; i += page) {
            if (VirtualAlloc(a + i, page, MEM_COMMIT, PAGE_READWRITE) == NULL ||
                VirtualAlloc(b + i, page, MEM_COMMIT, PAGE_READWRITE) == NULL) {
                break;
            }
            a[i] = 1;
            b[i] = 1;
        }
        VirtualFree(b, 0, MEM_RELEASE);
        if (i < n) {
            VirtualFree(a, 0, MEM_RELEASE);
            return 0;
        }
        r->bufScatter = a;
    }
    return 1;
}

static void free_buffers(RUN *r)
{
    if (r->bufNormal != NULL) {
        VirtualFree(r->bufNormal, 0, MEM_RELEASE);
    }
    if (r->bufScatter != NULL) {
        VirtualFree(r->bufScatter, 0, MEM_RELEASE);
    }
    if (r->vbuf != NULL) {
        VirtualFree(r->vbuf, 0, MEM_RELEASE);
    }
    if (r->map != NULL) {
        VirtualFree(r->map, 0, MEM_RELEASE);
    }
    r->bufNormal = r->bufScatter = r->vbuf = NULL;
    r->map = NULL;
}

static void fill_buffers(RUN *r, const PF_PLAN *pl)
{
    size_t n = r->stride * (pl->slots + 1);

    if (g_p.pattern == PF_PAT_RANDOM) {
        pf_fill_random(r->bufNormal, (pf_u32)n, g_p.seed ^ 0x5A5A5A5Au);
        if (r->bufScatter != NULL) {
            pf_fill_random(r->bufScatter, (pf_u32)n, g_p.seed ^ 0x5A5A5A5Au);
        }
    } else if (g_p.pattern == PF_PAT_ZEROS) {
        memset(r->bufNormal, 0, n);
        if (r->bufScatter != NULL) {
            memset(r->bufScatter, 0, n);
        }
    }
}


/* The preparation (record 29 section 3.1): the region written once. */
static int prepare(RUN *r, const PF_PLAN *pl, PF_ENV *env, char *why, size_t cap)
{
    HANDLE h = open_test(r, 0, g_p.access == PF_ACC_UNBUF, 0);
    pf_u64 off, sinceCheck = 0;
    DWORD got;
    char st[80];

    if (h == INVALID_HANDLE_VALUE || !handle_is_ours(r, h, 0)) {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
        sz_cpy(why, cap, "The test file was changed by another program.");
        return 0;
    }
    for (off = PF_HEADER_BYTES; off < pl->fileBytes; off += PF_PREP_CHUNK) {
        LONG hi = (LONG)(off >> 32);

        if (stopped()) {
            CloseHandle(h);
            sz_cpy(why, cap, "Stopped.");
            return 0;
        }
        if (sinceCheck == 0) {
            PF_ENV e2;

            read_env(g_folder, &e2);
            e2.systemVolume = env->systemVolume;
            if (pf_free_check(&e2, (pl->fileBytes - off) + 0, why, cap)) {
                CloseHandle(h);
                return 0;
            }
            record_put(r, pl->fileBytes, off);
            sz_cpy(st, sizeof(st), "Preparing the test file: ");
            sz_u(st, sizeof(st), (off * 100u) / pl->fileBytes);
            sz_cat(st, sizeof(st), "%");
            post_status(st);
        }
        if (g_p.pattern == PF_PAT_VERIFY) {
            pf_stamp(r->vbuf, PF_PREP_CHUNK, r->runId, off / PF_UNIT, 0);
        } else if (g_p.pattern == PF_PAT_RANDOM) {
            pf_fill_random(r->vbuf, PF_PREP_CHUNK, g_p.seed ^ (pf_u32)(off >> 20));
        } else {
            memset(r->vbuf, 0, PF_PREP_CHUNK);
        }
        SetFilePointer(h, (LONG)(off & 0xFFFFFFFFu), &hi, FILE_BEGIN);
        if (!WriteFile(h, r->vbuf, PF_PREP_CHUNK, &got, NULL) || got != PF_PREP_CHUNK) {
            DWORD e = GetLastError();

            CloseHandle(h);
            err_text(e, why, cap);
            return 0;
        }
        sinceCheck += PF_PREP_CHUNK;
        if (sinceCheck >= PF_PREP_STEP) {
            sinceCheck = 0;
        }
    }
    if (!FlushFileBuffers(h)) {
        DWORD e = GetLastError();

        CloseHandle(h);
        err_text(e, why, cap);
        return 0;
    }
    CloseHandle(h);
    if (r->anchor != NULL && !FlushFileBuffers(r->anchor)) {
        err_text(GetLastError(), why, cap);
        return 0;
    }
    record_put(r, pl->fileBytes, pl->fileBytes);
    return 1;
}

/* The untimed verification of what a write step wrote (record 29 section 3.3) */
/* All: every block but the unknown ones, the preparation's included; else
 * the blocks written since generation genStart. */
static pf_u32 verify_since(RUN *r, pf_u32 genStart, int all, pf_u32 *firstBad)
{
    HANDLE h = open_test(r, 0, g_p.access == PF_ACC_UNBUF, 0);
    pf_u64 b = 0;
    pf_u32 bad = 0;
    pf_u32 perChunk = r->vbufBytes / r->mapBlock;

    if (perChunk == 0) {
        perChunk = 1;
    }

    *firstBad = 0;
    if (h == INVALID_HANDLE_VALUE) {
        return 0xFFFFFFFFu;
    }
    while (b < r->mapBlocks && !stopped()) {
        pf_u64 e;
        pf_u64 off;
        LONG hi;
        DWORD got = 0, len;

        if (r->map[b] == PF_GEN_UNKNOWN) {
            if (all) {
                r->incomplete++;
            }
            b++;
            continue;
        }
        if (!all && r->map[b] <= genStart) {
            b++;
            continue;
        }
        for (e = b + 1; e < r->mapBlocks && e - b < perChunk && (all || r->map[e] > genStart) &&
             r->map[e] != PF_GEN_UNKNOWN; e++) {
        }
        off = PF_HEADER_BYTES + b * r->mapBlock;
        len = (DWORD)((e - b) * r->mapBlock);
        hi = (LONG)(off >> 32);
        SetFilePointer(h, (LONG)(off & 0xFFFFFFFFu), &hi, FILE_BEGIN);
        if (!ReadFile(h, r->vbuf, len, &got, NULL) || got != len) {
            CloseHandle(h);
            return 0xFFFFFFFFu;
        }
        bad += check_read(r, r->vbuf, off, len, bad == 0 ? firstBad : NULL);
        b = e;
    }
    CloseHandle(h);
    return bad;
}

static void step_status(int i, int pass, int npass, int warm)
{
    char st[160];

    sz_cpy(st, sizeof(st), warm ? "Warm-up: " : "Pass ");
    if (!warm) {
        sz_u(st, sizeof(st), (pf_u64)(pass + 1));
        sz_cat(st, sizeof(st), " of ");
        sz_u(st, sizeof(st), (pf_u64)npass);
        sz_cat(st, sizeof(st), ": ");
    }
    sz_cat(st, sizeof(st), g_plan.steps[i].label);
    sz_cat(st, sizeof(st), g_plan.steps[i].op == PF_OP_WRITE ? " write, " : " read, ");
    sz_cat(st, sizeof(st), pf_buf_name(g_plan.steps[i].buffer));
    sz_cat(st, sizeof(st), " buffer");
    post_status(st);
}

static void run_step(RUN *r, int i, PF_RESULT *res)
{
    const PF_STEP *s = &g_plan.steps[i];
    int t, nt = s->threads;
    HANDLE th[PF_MAX_THREADS];
    pf_u64 t0, t1;
    DWORD k0, k1;
    unsigned id;
    pf_u64 part = r->region / (pf_u64)nt;

    memset(res, 0, sizeof(*res));
    part -= part % s->size;
    r->step = s;
    r->stepIndex = i;
    r->issueEnd = 0;
    ResetEvent(r->go);
    ResetEvent(r->stepEnd);
    if (stopped()) {
        SetEvent(r->stepEnd);
    }
    if (s->op == PF_OP_WRITE) {
        fill_buffers(r, &g_plan);
    }
    for (t = 0; t < nt; t++) {
        WORKER *w = &r->w[t];

        memset(w, 0, sizeof(*w));
        w->run = r;
        w->index = t;
        w->partStart = PF_HEADER_BYTES + part * (pf_u64)t;
        w->partBytes = part;
        w->rnd = pf_thread_seed(g_p.seed ^ (pf_u32)i, t);
        w->budgetLeft = s->budget != 0 ? s->budget / (pf_u64)nt - (s->budget / (pf_u64)nt) % s->size : 0;
        if (t == 0 && s->budget != 0) {
            w->budgetLeft += (s->budget - (s->budget / (pf_u64)nt) * (pf_u64)nt) / s->size * s->size;
        }
        pf_lat_init(&w->lat);
        w->h = open_test(r, r->async, r->unbuffered, g_p.writeThrough);
        if (w->h == INVALID_HANDLE_VALUE || !handle_is_ours(r, w->h, r->async)) {
            res->status = PF_ST_FAILED;
            res->error = (w->h == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_FILE_NOT_FOUND;
            r->failed = 1;
            r->failError = res->error;
            SetEvent(g_stopEvent);
            while (t >= 0) {
                if (r->w[t].h != INVALID_HANDLE_VALUE && r->w[t].h != NULL) {
                    CloseHandle(r->w[t].h);
                }
                t--;
            }
            return;
        }
    }
    for (t = 0; t < nt; t++) {
        th[t] = (HANDLE)_beginthreadex(NULL, 0, worker_main, &r->w[t], 0, &id);
        if (th[t] != NULL && g_os == PF_OS_NT && g_preVista && g_ncpu > 1) {
            SetThreadAffinityMask(th[t], (DWORD_PTR)1 << ((t + 1) % g_ncpu));
        }
    }
    k0 = GetTickCount();
    t0 = qpc_now();
    SetEvent(r->go);
    if (s->budget == 0) {
        HANDLE ends[2];

        ends[0] = r->stepEnd;
        ends[1] = g_stopEvent;
        WaitForMultipleObjects(2, ends, FALSE, (DWORD)s->timeSec * 1000u);
        InterlockedExchange((LONG *)&r->issueEnd, 1);
    }
    for (t = 0; t < nt; t++) {
        if (th[t] != NULL) {
            WaitForSingleObject(th[t], INFINITE);
            CloseHandle(th[t]);
        }
    }
    /* A cached write ends with its flush, in the time when the box says so
     * (record 29 section 3.2), else just after it */
    if (s->op == PF_OP_WRITE && !r->unbuffered && g_p.flushInWrite) {
        for (t = 0; t < nt; t++) {
            if (!FlushFileBuffers(r->w[t].h) && !r->w[t].failed) {
                r->w[t].failed = 1;
                r->w[t].error = GetLastError();
            }
        }
    }
    t1 = qpc_now();
    k1 = GetTickCount();
    if (s->op == PF_OP_WRITE && !r->unbuffered && !g_p.flushInWrite) {
        for (t = 0; t < nt; t++) {
            if (!FlushFileBuffers(r->w[t].h) && !r->w[t].failed) {
                r->w[t].failed = 1;
                r->w[t].error = GetLastError();
            }
        }
    }
    res->us = ticks_us(t1 - t0);
    if (g_highRes && !pf_timer_plausible(res->us, (pf_u64)(k1 - k0))) {
        res->timerBad = 1;
        res->us = (pf_u64)(k1 - k0) * 1000u;
    }
    res->status = PF_ST_OK;
    pf_lat_init(&res->lat);
    for (t = 0; t < nt; t++) {
        WORKER *w = &r->w[t];

        res->bytes += w->bytes;
        res->reqs += w->reqs;
        res->stampUs += w->stampUs;
        res->badUnits += w->bad;
        pf_lat_merge(&res->lat, &w->lat);
        if (w->failed) {
            res->status = PF_ST_FAILED;
            res->error = w->error;
        } else if (w->skipped && res->status == PF_ST_OK) {
            res->status = PF_ST_SKIPPED;
        }
        CloseHandle(w->h);
    }
    if (res->stampUs != 0 && nt != 0) {
        res->stampUs /= (pf_u64)nt;
    }
    res->cpuLimited = pf_cpu_limited(res->stampUs, res->us);
    if (res->status == PF_ST_OK && res->badUnits != 0) {
        res->status = PF_ST_DATAERR;
    }
    if (res->status == PF_ST_OK && stopped() && !r->genOut) {
        res->status = PF_ST_STOPPED;
    }
}

static void log_result(int i, int pass, const PF_RESULT *res)
{
    char line[256], v[32];
    const PF_STEP *s = &g_plan.steps[i];

    sz_cpy(line, sizeof(line), s->label);
    sz_cat(line, sizeof(line), s->op == PF_OP_WRITE ? " write " : " read ");
    sz_cat(line, sizeof(line), pf_buf_name(s->buffer));
    sz_cat(line, sizeof(line), " pass ");
    sz_u(line, sizeof(line), (pf_u64)(pass + 1));
    sz_cat(line, sizeof(line), ": ");
    pf_fmt_tenths(pf_rate10(res->bytes, res->us, g_p.unitMiB), v, sizeof(v));
    sz_cat(line, sizeof(line), v);
    sz_cat(line, sizeof(line), g_p.unitMiB ? " MiB/s, " : " MB/s, ");
    sz_u(line, sizeof(line), res->reqs);
    sz_cat(line, sizeof(line), " requests");
    if (res->status == PF_ST_FAILED) {
        sz_cat(line, sizeof(line), ", failed (");
        err_text(res->error, v, sizeof(v));
        sz_cat(line, sizeof(line), v);
        sz_cat(line, sizeof(line), ")");
    } else if (res->status == PF_ST_DATAERR) {
        sz_cat(line, sizeof(line), ", data error in ");
        sz_u(line, sizeof(line), res->badUnits);
        sz_cat(line, sizeof(line), " blocks");
    } else if (res->status == PF_ST_SKIPPED) {
        sz_cat(line, sizeof(line), ", this disk does not accept misaligned buffers");
    }
    if (res->cpuLimited) {
        sz_cat(line, sizeof(line), ", may be limited by the processor");
    }
    if (res->timerBad) {
        sz_cat(line, sizeof(line), ", timer unreliable: milliseconds used");
    }
    post_log(line);
}

static unsigned __stdcall run_main(void *arg)
{
    RUN *r = &g_run;
    PF_ENV env;
    char why[400];
    HANDLE h;
    BY_HANDLE_FILE_INFORMATION bi;
    unsigned char *hb;
    PF_HEADER hd;
    FILETIME ft;
    DWORD got;
    int i, pass, warm;
    int npass = g_p.passes;
    pf_u32 genStart;
    int code = 2;

    (void)arg;
    memset(r, 0, sizeof(*r));
    InitializeCriticalSection(&r->cs);
    pf_gen_init(&r->gen);
    r->go = CreateEventA(NULL, TRUE, FALSE, NULL);
    r->stepEnd = CreateEventA(NULL, TRUE, FALSE, NULL);
    file_path(g_folder, r->path, sizeof(r->path));
    read_env(g_folder, &env);
    r->sector = env.sector != 0 ? env.sector : 512;
    r->region = g_plan.regionBytes;
    r->unbuffered = (g_p.access == PF_ACC_UNBUF);
    r->async = (g_os == PF_OS_NT);
    if (g_setExec != NULL) {
        g_setExec(PF_ES_SYSTEM_REQUIRED | PF_ES_CONTINUOUS);
    }
    why[0] = 0;
    if (!alloc_buffers(r, &g_plan, (g_p.bufMask & (1 << PF_BUF_SCATTER)) != 0)) {
        sz_cpy(why, sizeof(why), "Not enough memory for the buffers; lower the depth, the threads or the largest size.");
        goto done;
    }
    if (g_p.verify && g_plan.mapBlocks != 0) {
        r->mapBlock = g_plan.mapBlock;
        r->mapBlocks = g_plan.mapBlocks;
        r->map = (pf_u32 *)VirtualAlloc(NULL, (size_t)(r->mapBlocks * 4u), MEM_COMMIT, PAGE_READWRITE);
        if (r->map == NULL) {
            sz_cpy(why, sizeof(why), "Not enough memory to verify; lower the file size.");
            goto done;
        }
    }
    GetSystemTimeAsFileTime(&ft);
    r->runId = ft.dwLowDateTime ^ (GetCurrentProcessId() << 16) ^ GetTickCount();
    if (r->runId == 0) {
        r->runId = 1;
    }
    r->runHi = ft.dwHighDateTime;

    /* Create it: the cleanup handle on NT, withholding delete sharing */
    h = CreateFileA(r->path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | (g_os == PF_OS_NT ? FILE_FLAG_DELETE_ON_CLOSE : 0), NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();

        r->path[0] = 0;
        if (e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS) {
            sz_cpy(why, sizeof(why), "A file named XHCIPERF.TMP is already in this folder; choose another folder, or start again to be asked about it.");
        } else {
            sz_cpy(why, sizeof(why), "You cannot write to this folder (");
            err_text(e, why + strlen(why), sizeof(why) - strlen(why));
            sz_cat(why, sizeof(why), "); choose another folder.");
        }
        goto done;
    }
    r->anchor = h;
    memset(&bi, 0, sizeof(bi));
    GetFileInformationByHandle(h, &bi);
    r->volSerial = bi.dwVolumeSerialNumber;
    r->indexLo = bi.nFileIndexLow;
    r->indexHi = bi.nFileIndexHigh;
    record_put(r, g_plan.fileBytes, 0);
    g_attributes = GetFileAttributesA(r->path);
    if (g_attributes != 0xFFFFFFFF &&
        (g_attributes & (PF_FILE_ATTRIBUTE_COMPRESS | PF_FILE_ATTRIBUTE_ENCRYPT | PF_FILE_ATTRIBUTE_SPARSE)) != 0) {
        sz_cpy(why, sizeof(why), "This folder compresses or encrypts new files, which changes what is measured; choose another folder.");
        goto done;
    }
    {
        char win[MAX_PATH], swap[4];

        GetWindowsDirectoryA(win, sizeof(win));
        swap_drive(swap, sizeof(swap));
        env.systemVolume = (r->volSerial != 0 && (r->volSerial == serial_of(win) || r->volSerial == serial_of(swap)));
    }
    if (pf_free_check(&env, g_plan.fileBytes, why, sizeof(why))) {
        goto done;
    }
    hb = r->vbuf;
    memset(hb, 0, PF_PREP_CHUNK);
    memset(&hd, 0, sizeof(hd));
    hd.runLo = r->runId;
    hd.runHi = r->runHi;
    hd.plannedBytes = g_plan.fileBytes;
    hd.volSerial = r->volSerial;
    hd.createdLo = ft.dwLowDateTime;
    hd.createdHi = ft.dwHighDateTime;
    pf_header_build(&hd, hb);
    if (!WriteFile(h, hb, PF_HEADER_BYTES, &got, NULL) || got != PF_HEADER_BYTES || !FlushFileBuffers(h)) {
        err_text(GetLastError(), why, sizeof(why));
        goto done;
    }
    if (!prepare(r, &g_plan, &env, why, sizeof(why))) {
        code = stopped() ? 3 : 2;
        goto done;
    }
    code = 0;
    r->s98ok = 0;
    if (g_s98state == S98_ST_OK) {
        post_log("Stor98 drives this disk; its counters are read before and after each test.");
    }
    for (i = 0; i < g_plan.nsteps && !stopped(); i++) {
        for (warm = g_p.warmup ? 1 : 0; warm >= 0 && !stopped(); warm--) {
            int passes = warm ? 1 : npass;

            for (pass = 0; pass < passes && !stopped(); pass++) {
                PF_RESULT res;
                S98_ANSWER after;
                int haveBefore;

                if (g_p.pauseSec > 0) {
                    WaitForSingleObject(g_stopEvent, (DWORD)g_p.pauseSec * 1000u);
                }
                step_status(i, pass, npass, warm);
                haveBefore = s98_read(&r->s98before);
                EnterCriticalSection(&r->cs);
                genStart = r->gen.last;
                LeaveCriticalSection(&r->cs);
                run_step(r, i, &res);
                if (haveBefore && !warm && s98_read(&after)) {
                    S98_DELTA d;

                    if (s98_diff(&r->s98before, &after, &d)) {
                        PF_TEXT t;
                        static char page[sizeof(g_s98steps[0])];

                        pf_tx_init(&t, page, sizeof(page));
                        s98_page(&t, &after, &d, g_os == PF_OS_9X, res.reqs, g_plan.steps[i].size,
                                 g_plan.steps[i].op == PF_OP_WRITE);
                        EnterCriticalSection(&g_resCs);
                        memcpy(g_s98steps[i], page, sizeof(page));
                        LeaveCriticalSection(&g_resCs);
                    }
                }
                if (g_plan.steps[i].op == PF_OP_WRITE && g_p.verify && r->map != NULL && res.status == PF_ST_OK) {
                    pf_u32 fb;
                    pf_u32 bad;

                    post_status("Verifying what was written");
                    bad = verify_since(r, genStart, 0, &fb);
                    if (bad == 0xFFFFFFFFu) {
                        post_log("The verification could not read the file.");
                        res.status = PF_ST_FAILED;
                        res.error = GetLastError();
                    } else if (bad != 0) {
                        char line[200];

                        res.status = PF_ST_DATAERR;
                        res.badUnits = bad;
                        sz_cpy(line, sizeof(line), "Data error: ");
                        sz_u(line, sizeof(line), bad);
                        sz_cat(line, sizeof(line), " blocks differ; the first at file offset ");
                        sz_u(line, sizeof(line), (pf_u64)fb * PF_UNIT);
                        post_log(line);
                    }
                }
                if (!warm) {
                    EnterCriticalSection(&g_resCs);
                    g_res[i * PF_MAX_PASSES + pass] = res;
                    LeaveCriticalSection(&g_resCs);
                    log_result(i, pass, &res);
                    PostMessageA(g_wnd, WM_APP_RESULT, (WPARAM)i, 0);
                }
                if (res.status == PF_ST_FAILED) {
                    char e[200];

                    err_text(res.error, e, sizeof(e));
                    sz_cpy(why, sizeof(why), "The disk stopped answering (");
                    sz_cat(why, sizeof(why), e);
                    sz_cat(why, sizeof(why), "). The run was stopped.");
                    SetEvent(g_stopEvent);
                }
                if (res.status == PF_ST_DATAERR && g_p.stopOnError) {
                    sz_cpy(why, sizeof(why), "A data error was found; the run was stopped (see the log).");
                    SetEvent(g_stopEvent);
                }
            }
        }
    }
    if (r->genOut) {
        post_log("The run reached the limit of four thousand million writes for a verified run.");
    }
    /* The whole file at the end: when asked, and always for a verified run
     * that wrote nothing (its reads are checked against the preparation) */
    if (g_p.verify && r->map != NULL && !stopped() && (g_p.verifyEnd || g_plan.minWrite == 0)) {
        pf_u32 fb;
        pf_u32 bad;

        post_status("Verifying the whole file");
        bad = verify_since(r, 0, 1, &fb);
        if (bad == 0) {
            post_log("The whole file verified clean.");
        } else {
            InterlockedExchange((LONG *)&g_runBad, 1);
            post_log(bad == 0xFFFFFFFFu ? "The whole file's verification could not read the file." :
                     "The whole file's verification found data errors.");
        }
    }
    if (r->incomplete != 0) {
        char line[160];

        sz_cpy(line, sizeof(line), "Verification incomplete: ");
        sz_u(line, sizeof(line), r->incomplete);
        sz_cat(line, sizeof(line), " blocks written by a request that did not complete were not checked.");
        post_log(line);
    }
    if (stopped() && !r->failed && !r->genOut && why[0] == 0) {
        code = 3;
    }
done:
    if (why[0] != 0) {
        post_log(why);
    }
    cleanup_file(r);
    free_buffers(r);
    if (g_setExec != NULL) {
        g_setExec(PF_ES_CONTINUOUS);
    }
    CloseHandle(r->go);
    CloseHandle(r->stepEnd);
    DeleteCriticalSection(&r->cs);
    {
        char *c = NULL;

        if (why[0] != 0) {
            size_t n = strlen(why);

            c = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1);
            if (c != NULL) {
                memcpy(c, why, n + 1);
            }
        }
        PostMessageA(g_wnd, WM_APP_DONE, (WPARAM)code, (LPARAM)c);
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* The report and the files                                                  */
/* ------------------------------------------------------------------------ */

static void system_text(char *out, size_t cap)
{
    OSVERSIONINFOA v;

    memset(&v, 0, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    GetVersionExA(&v);
    sz_cpy(out, cap, "Windows ");
    sz_u(out, cap, v.dwMajorVersion);
    sz_cat(out, cap, ".");
    sz_u(out, cap, v.dwMinorVersion);
    sz_cat(out, cap, ".");
    sz_u(out, cap, v.dwBuildNumber & 0xFFFF);
    if (v.szCSDVersion[0] != 0) {
        sz_cat(out, cap, " ");
        sz_cat(out, cap, v.szCSDVersion);
    }
#ifdef _WIN64
    sz_cat(out, cap, " x64");
#else
    sz_cat(out, cap, g_os == PF_OS_9X ? "" : " x86");
#endif
}

static char *build_report(size_t *lenOut)
{
    size_t cap = 512u * 1024u;
    char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, cap);
    PF_TEXT t;
    PF_RUNINFO ri;
    int i;

    if (buf == NULL) {
        return NULL;
    }
    EnterCriticalSection(&g_resCs);
    g_s98page[0] = 0;
    for (i = 0; i < g_plan.nsteps; i++) {
        if (g_s98steps[i][0] != 0 && strlen(g_s98page) + strlen(g_s98steps[i]) + 64 < sizeof(g_s98page)) {
            sz_cat(g_s98page, sizeof(g_s98page), "Stor98 during ");
            sz_cat(g_s98page, sizeof(g_s98page), g_plan.steps[i].label);
            sz_cat(g_s98page, sizeof(g_s98page), g_plan.steps[i].op == PF_OP_WRITE ? " write, " : " read, ");
            sz_cat(g_s98page, sizeof(g_s98page), pf_buf_name(g_plan.steps[i].buffer));
            sz_cat(g_s98page, sizeof(g_s98page), ":\r\n");
            sz_cat(g_s98page, sizeof(g_s98page), g_s98steps[i]);
            sz_cat(g_s98page, sizeof(g_s98page), "\r\n");
        }
    }
    memset(&ri, 0, sizeof(ri));
    ri.toolVersion = XHCIPERF_VERSION;
    ri.date = g_runDate;
    ri.system = g_system;
    ri.drive = g_driveText;
    ri.fileSystem = g_fsName;
    ri.model = g_model;
    ri.sector = g_sector;
    ri.physSector = g_physSector;
    ri.timerFreq = g_highRes ? g_freq : 0;
    ri.attributes = g_attributes;
    ri.stor98 = g_s98page;
    pf_tx_init(&t, buf, cap);
    pf_report(&t, &g_p, &g_plan, g_res, g_npasses, &ri);
    LeaveCriticalSection(&g_resCs);
    *lenOut = t.len;
    return buf;
}

static void copy_text(HWND w, const char *s, size_t n)
{
    HGLOBAL g;
    char *p;

    if (!OpenClipboard(w)) {
        return;
    }
    EmptyClipboard();
    g = GlobalAlloc(GMEM_MOVEABLE, n + 1);
    if (g != NULL) {
        p = (char *)GlobalLock(g);
        memcpy(p, s, n);
        p[n] = 0;
        GlobalUnlock(g);
        SetClipboardData(CF_TEXT, g);
    }
    CloseClipboard();
}

static int write_file(const char *path, const char *s, size_t n)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD w = 0;
    BOOL ok;

    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ok = WriteFile(h, s, (DWORD)n, &w, NULL);
    CloseHandle(h);
    return ok && w == n;
}

/* ------------------------------------------------------------------------ */
/* The view: Summary and Sweep (record 29 sections 5.2 and 5.3)              */
/* ------------------------------------------------------------------------ */

static const PF_SAVED_ROW *saved_row(const PF_STEP *s)
{
    int i;

    if (!g_haveSaved) {
        return NULL;
    }
    for (i = 0; i < g_saved.nrows; i++) {
        const PF_SAVED_ROW *r = &g_saved.rows[i];

        if (strcmp(r->label, s->label) == 0 && r->op == s->op && r->buffer == s->buffer) {
            return r;
        }
    }
    return NULL;
}

static void cell_text(int step, char *big, size_t bcap, char *sub, size_t scap)
{
    PF_SHOWN sh;
    char a[32];
    const PF_SAVED_ROW *sv;

    big[0] = 0;
    sub[0] = 0;
    if (step < 0) {
        return;
    }
    pf_shown(&g_res[step * PF_MAX_PASSES], g_npasses, g_p.show, g_p.unitMiB, g_highRes, &sh);
    if (sh.status == PF_ST_NONE) {
        sz_cpy(big, bcap, "-");
        return;
    }
    if (sh.status != PF_ST_OK && sh.rate10 == 0) {
        sz_cpy(big, bcap, sh.status == PF_ST_FAILED ? "failed" : (sh.status == PF_ST_DATAERR ? "data error" :
               (sh.status == PF_ST_SKIPPED ? "not accepted" : "stopped")));
        return;
    }
    pf_fmt_tenths(sh.rate10, big, bcap);
    pf_fmt_u64(sh.iops, sub, scap);
    sz_cat(sub, scap, " IOPS");
    if (sh.haveLat) {
        pf_fmt_us(sh.latMean, a, sizeof(a));
        sz_cat(sub, scap, ", ");
        sz_cat(sub, scap, a);
    }
    if (sh.status == PF_ST_DATAERR) {
        sz_cat(sub, scap, ", data error");
    }
    if (sh.cpuLimited) {
        sz_cat(sub, scap, ", cpu");
    }
    sv = saved_row(&g_plan.steps[step]);
    if (sv != NULL) {
        pf_u64 old = pf_saved_value(sv, g_p.show, g_p.unitMiB);
        long ch = pf_change10(sh.rate10, old);
        char c[32];

        sz_cat(sub, scap, " (was ");
        pf_fmt_tenths(old, a, sizeof(a));
        sz_cat(sub, scap, a);
        sz_cat(sub, scap, ", ");
        sz_cat(sub, scap, ch >= 0 ? "+" : "-");
        pf_fmt_tenths((pf_u64)(ch >= 0 ? ch : -ch), c, sizeof(c));
        sz_cat(sub, scap, c);
        sz_cat(sub, scap, "%)");
    }
}

static void paint_summary(HDC dc, RECT *rc)
{
    int i, j, y = 4, rowh = 44;
    int colw = (rc->right - 200) / 2;
    char big[40], sub[120], hdr[80];
    HFONT old = (HFONT)SelectObject(dc, g_font);
    int shown[PF_MAX_STEPS];

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    TextOutA(dc, 210, y, "Write", 5);
    TextOutA(dc, 210 + colw, y, "Read", 4);
    sz_cpy(hdr, sizeof(hdr), g_p.unitMiB ? "MiB/s" : "MB/s");
    TextOutA(dc, 8, y, hdr, (int)strlen(hdr));
    y += 20;
    memset(shown, 0, sizeof(shown));
    for (i = 0; i < g_plan.nsteps; i++) {
        int wr = -1, rd = -1;
        RECT cell;
        char lbl[64];

        if (shown[i]) {
            continue;
        }
        for (j = i; j < g_plan.nsteps; j++) {
            if (g_plan.steps[j].testIndex == g_plan.steps[i].testIndex &&
                g_plan.steps[j].buffer == g_plan.steps[i].buffer && g_plan.steps[j].sweep == g_plan.steps[i].sweep) {
                shown[j] = 1;
                if (g_plan.steps[j].op == PF_OP_WRITE) {
                    wr = j;
                } else {
                    rd = j;
                }
            }
        }
        pf_test_long(g_plan.steps[i].kind, g_plan.steps[i].size, g_plan.steps[i].depth, g_plan.steps[i].threads, lbl,
                     sizeof(lbl));
        SelectObject(dc, g_font);
        cell.left = 4;
        cell.top = y;
        cell.right = 200;
        cell.bottom = y + rowh - 4;
        FillRect(dc, &cell, GetSysColorBrush(COLOR_BTNFACE));
        cell.left += 4;
        DrawTextA(dc, lbl, -1, &cell, DT_LEFT | DT_WORDBREAK);
        if ((g_p.bufMask & (g_p.bufMask - 1)) != 0) {
            cell.top += 28;
            DrawTextA(dc, pf_buf_name(g_plan.steps[i].buffer), -1, &cell, DT_LEFT);
        }
        for (j = 0; j < 2; j++) {
            int st = j == 0 ? wr : rd;
            RECT c;

            c.left = 206 + j * colw;
            c.top = y;
            c.right = c.left + colw - 6;
            c.bottom = y + rowh - 4;
            FillRect(dc, &c, GetSysColorBrush(COLOR_WINDOW));
            FrameRect(dc, &c, GetSysColorBrush(COLOR_BTNSHADOW));
            if (st < 0) {
                continue;
            }
            cell_text(st, big, sizeof(big), sub, sizeof(sub));
            SelectObject(dc, g_big);
            c.right -= 6;
            DrawTextA(dc, big, -1, &c, DT_RIGHT | DT_TOP | DT_SINGLELINE);
            SelectObject(dc, g_font);
            c.top += 26;
            DrawTextA(dc, sub, -1, &c, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        }
        y += rowh;
        if (y > rc->bottom) {
            break;
        }
    }
    SelectObject(dc, old);
}

static void paint_sweep(HDC dc, RECT *rc)
{
    int i, y = 4, barh = 7;
    pf_u64 maxv = 1, scale;
    int x0 = 140, w = rc->right - x0 - 110;
    HFONT old = (HFONT)SelectObject(dc, g_font);
    char lbl[64], v[32];
    HBRUSH rb = GetSysColorBrush(COLOR_HIGHLIGHT);
    HBRUSH wb = CreateHatchBrush(HS_BDIAGONAL, GetSysColor(COLOR_BTNSHADOW));
    PF_SHOWN sh;

    SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < g_plan.nsteps; i++) {
        pf_shown(&g_res[i * PF_MAX_PASSES], g_npasses, g_p.show, g_p.unitMiB, g_highRes, &sh);
        if (sh.rate10 > maxv) {
            maxv = sh.rate10;
        }
    }
    for (scale = 10; scale < maxv; ) {
        if (scale * 2 >= maxv) {
            scale *= 2;
        } else if (scale * 5 >= maxv) {
            scale *= 5;
        } else {
            scale *= 10;
        }
    }
    sz_cpy(lbl, sizeof(lbl), "write (hatched) and read, ");
    sz_cat(lbl, sizeof(lbl), g_p.unitMiB ? "MiB/s; axis " : "MB/s; axis ");
    pf_fmt_tenths(scale, v, sizeof(v));
    sz_cat(lbl, sizeof(lbl), v);
    TextOutA(dc, 8, y, lbl, (int)strlen(lbl));
    y += 18;
    for (i = 0; i < g_plan.nsteps && y < rc->bottom; i++) {
        const PF_STEP *s = &g_plan.steps[i];
        RECT b;
        int len;

        pf_shown(&g_res[i * PF_MAX_PASSES], g_npasses, g_p.show, g_p.unitMiB, g_highRes, &sh);
        if (s->op == PF_OP_WRITE || i == 0 || g_plan.steps[i - 1].size != s->size) {
            pf_size_text(s->size, lbl, sizeof(lbl));
            if ((g_p.bufMask & (g_p.bufMask - 1)) != 0) {
                sz_cat(lbl, sizeof(lbl), " ");
                sz_cat(lbl, sizeof(lbl), pf_buf_name(s->buffer));
            }
            TextOutA(dc, 8, y, lbl, (int)strlen(lbl));
        }
        len = (int)((sh.rate10 * (pf_u64)w) / scale);
        b.left = x0;
        b.top = y + (s->op == PF_OP_WRITE ? 0 : barh);
        b.right = x0 + len;
        b.bottom = b.top + barh - 1;
        FillRect(dc, &b, s->op == PF_OP_WRITE ? wb : rb);
        FrameRect(dc, &b, GetSysColorBrush(COLOR_WINDOWTEXT));
        if (sh.status != PF_ST_NONE) {
            pf_fmt_tenths(sh.rate10, v, sizeof(v));
            TextOutA(dc, x0 + len + 4, b.top - 3, v, (int)strlen(v));
        }
        if (s->op == PF_OP_READ) {
            y += 2 * barh + 6;
        }
    }
    DeleteObject(wb);
    SelectObject(dc, old);
}

static LRESULT CALLBACK view_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT rc;

        GetClientRect(w, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
        EnterCriticalSection(&g_resCs);
        if (g_plan.nsteps == 0) {
            const char *s = "Choose the drive or folder and the tests, then press Start.";

            SelectObject(dc, g_font);
            SetBkMode(dc, TRANSPARENT);
            TextOutA(dc, 8, 8, s, (int)strlen(s));
        } else if (g_tabSel == 1 || g_p.sweep) {
            paint_sweep(dc, &rc);
        } else {
            paint_summary(dc, &rc);
        }
        LeaveCriticalSection(&g_resCs);
        EndPaint(w, &ps);
        return 0;
    }
    if (m == WM_ERASEBKGND) {
        return 1;
    }
    return DefWindowProcA(w, m, wp, lp);
}

/* ------------------------------------------------------------------------ */
/* The window                                                                */
/* ------------------------------------------------------------------------ */

static HWND ctl(int id)
{
    return GetDlgItem(g_wnd, id);
}

static HWND mk(const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id,
                             g_inst, NULL);

    SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void combo_add(int id, const char *s)
{
    SendMessageA(ctl(id), CB_ADDSTRING, 0, (LPARAM)s);
}

static int combo_get(int id)
{
    return (int)SendMessageA(ctl(id), CB_GETCURSEL, 0, 0);
}

static void combo_set(int id, int i)
{
    SendMessageA(ctl(id), CB_SETCURSEL, (WPARAM)i, 0);
}

static const pf_u32 g_sizes[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384 };
static const int g_times[] = { 1, 2, 5, 10, 20, 30, 60 };
static const int g_depths[] = { 1, 2, 4, 8, 16, 32 };
static const int g_threadsList[] = { 1, 2, 4, 8 };
static int g_ui;

static void ui_from_params(void)
{
    int i;

    g_ui = 1;
    combo_set(IDC_PRESET, g_p.preset >= 0 ? g_p.preset : PF_PRESETS);
    for (i = 0; i < (int)(sizeof(g_sizes) / sizeof(g_sizes[0])); i++) {
        if (g_sizes[i] == g_p.fileMiB) {
            combo_set(IDC_FSIZE, i);
        }
    }
    combo_set(IDC_PASSES, g_p.passes - 1);
    for (i = 0; i < (int)(sizeof(g_times) / sizeof(g_times[0])); i++) {
        if (g_times[i] == g_p.timeSec) {
            combo_set(IDC_TIME, i);
        }
    }
    combo_set(IDC_OPS, g_p.ops == PF_OP_BOTH ? 0 : (g_p.ops == PF_OP_READ ? 1 : 2));
    combo_set(IDC_ACCESS, g_p.access);
    SendMessageA(ctl(IDC_WT), BM_SETCHECK, g_p.writeThrough ? BST_CHECKED : BST_UNCHECKED, 0);
    combo_set(IDC_BUFFER, g_p.bufMask == 7 ? 3 : (g_p.bufMask == 2 ? 1 : (g_p.bufMask == 4 ? 2 : 0)));
    combo_set(IDC_PATTERN, g_p.pattern);
    SendMessageA(ctl(IDC_VERIFY), BM_SETCHECK, g_p.verify ? BST_CHECKED : BST_UNCHECKED, 0);
    combo_set(IDC_DEPTH, 0);
    combo_set(IDC_THREADS, 0);
    EnableWindow(ctl(IDC_DEPTH), g_os == PF_OS_NT);
    SetWindowTextA(ctl(IDC_FOLDER), g_folder);
    g_ui = 0;
}

static void params_from_ui(void)
{
    int i;
    char f[MAX_PATH];

    if (g_ui) {
        return;
    }
    i = combo_get(IDC_FSIZE);
    if (i >= 0) {
        g_p.fileMiB = g_sizes[i];
    }
    i = combo_get(IDC_PASSES);
    if (i >= 0) {
        g_p.passes = i + 1;
    }
    i = combo_get(IDC_TIME);
    if (i >= 0) {
        g_p.timeSec = g_times[i];
    }
    i = combo_get(IDC_OPS);
    g_p.ops = i == 1 ? PF_OP_READ : (i == 2 ? PF_OP_WRITE : PF_OP_BOTH);
    g_p.access = combo_get(IDC_ACCESS) == 1 ? PF_ACC_CACHED : PF_ACC_UNBUF;
    g_p.writeThrough = SendMessageA(ctl(IDC_WT), BM_GETCHECK, 0, 0) == BST_CHECKED;
    i = combo_get(IDC_BUFFER);
    g_p.bufMask = i == 3 ? 7 : (1 << (i < 0 ? 0 : i));
    i = combo_get(IDC_PATTERN);
    g_p.pattern = i < 0 ? PF_PAT_RANDOM : i;
    g_p.verify = SendMessageA(ctl(IDC_VERIFY), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (g_p.pattern != PF_PAT_VERIFY) {
        g_p.verify = 0;
        SendMessageA(ctl(IDC_VERIFY), BM_SETCHECK, BST_UNCHECKED, 0);
    }
    i = combo_get(IDC_DEPTH);
    if (i > 0) {
        int t;

        for (t = 0; t < g_p.ntests; t++) {
            g_p.tests[t].depth = g_depths[i - 1];
        }
        g_p.sweepDepth = g_depths[i - 1] > 4 ? 4 : g_depths[i - 1];
    }
    i = combo_get(IDC_THREADS);
    if (i > 0) {
        int t;

        for (t = 0; t < g_p.ntests; t++) {
            g_p.tests[t].threads = g_threadsList[i - 1];
        }
    }
    GetWindowTextA(ctl(IDC_FOLDER), f, sizeof(f));
    sz_cpy(g_folder, sizeof(g_folder), f);
}

static void update_estimate(void)
{
    PF_ENV e;
    PF_PLAN pl;
    char why[400], t[440];
    char canon[MAX_PATH];

    if (g_running) {
        return;
    }
    params_from_ui();
    if (check_folder(g_folder, canon, sizeof(canon), why, sizeof(why)) == 0) {
        read_env(canon, &e);
        if (pf_plan(&g_p, &e, &pl, why, sizeof(why)) == PF_OK) {
            sz_cpy(t, sizeof(t), "About ");
            sz_u(t, sizeof(t), (pf_u64)(pl.estimateSec + 59) / 60u);
            sz_cat(t, sizeof(t), " min");
            if (pl.warnCache) {
                sz_cat(t, sizeof(t), "; the file fits in memory: cached reads come from the cache");
            }
            if (pl.warnSmallPart) {
                sz_cat(t, sizeof(t), "; a part is smaller than the depth");
            }
        } else {
            sz_cpy(t, sizeof(t), why);
        }
    } else {
        sz_cpy(t, sizeof(t), why);
    }
    SetWindowTextA(ctl(IDC_ESTIMATE), t);
}

static void layout(void)
{
    RECT rc, tr;
    int y0 = 116;

    GetClientRect(g_wnd, &rc);
    SendMessageA(g_status, WM_SIZE, 0, 0);
    GetWindowRect(g_status, &tr);
    MoveWindow(g_tab, 4, y0, rc.right - 8, rc.bottom - y0 - (tr.bottom - tr.top) - 4, TRUE);
    GetClientRect(g_tab, &tr);
    TabCtrl_AdjustRect(g_tab, FALSE, &tr);
    MapWindowPoints(g_tab, g_wnd, (POINT *)&tr, 2);
    MoveWindow(g_view, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
    MoveWindow(g_disk, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
    MoveWindow(g_s98, tr.left, tr.top + 28, tr.right - tr.left, tr.bottom - tr.top - 28, TRUE);
    MoveWindow(g_s98test, tr.left, tr.top + 2, 300, 200, TRUE);
    MoveWindow(g_s98admin, tr.left + 306, tr.top + 2, 180, 23, TRUE);
    MoveWindow(g_log, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, TRUE);
    MoveWindow(ctl(IDC_ESTIMATE), 4, 94, rc.right - 8, 18, TRUE);
}

static void show_tab(void)
{
    g_tabSel = TabCtrl_GetCurSel(g_tab);
    ShowWindow(g_view, (g_tabSel == 0 || g_tabSel == 1) ? SW_SHOW : SW_HIDE);
    ShowWindow(g_disk, g_tabSel == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_s98, g_tabSel == 3 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_s98test, g_tabSel == 3 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_s98admin, (g_tabSel == 3 && g_s98state == S98_ST_ADMIN) ? SW_SHOW : SW_HIDE);
    ShowWindow(g_log, g_tabSel == 4 ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_view, NULL, TRUE);
}

static void log_add(const char *s)
{
    int n = GetWindowTextLengthA(g_log);

    SendMessageA(g_log, EM_SETSEL, (WPARAM)n, (LPARAM)n);
    SendMessageA(g_log, EM_REPLACESEL, FALSE, (LPARAM)s);
    SendMessageA(g_log, EM_REPLACESEL, FALSE, (LPARAM)"\r\n");
}

static void refresh_s98_page(void)
{
    int i = (int)SendMessageA(g_s98test, CB_GETCURSEL, 0, 0);
    char buf[4096];

    if (g_s98state != S98_ST_OK) {
        SetWindowTextA(g_s98, s98_state_text());
        return;
    }
    if (i >= 0 && i < g_plan.nsteps && g_s98steps[i][0] != 0) {
        EnterCriticalSection(&g_resCs);
        memcpy(buf, g_s98steps[i], sizeof(buf) < sizeof(g_s98steps[i]) ? sizeof(buf) : sizeof(g_s98steps[i]));
        LeaveCriticalSection(&g_resCs);
        buf[sizeof(buf) - 1] = 0;
        SetWindowTextA(g_s98, buf);
        return;
    }
    {
        S98_ANSWER a;
        PF_TEXT t;

        pf_tx_init(&t, buf, sizeof(buf));
        if (!g_running && s98_read(&a)) {
            s98_facts(&t, &a, g_os == PF_OS_9X);
        } else {
            pf_tx_s(&t, "Stor98 drives this disk. Run a test to see what reached the drive.");
        }
        SetWindowTextA(g_s98, buf);
    }
}

static void refresh_disk(void)
{
    char canon[MAX_PATH], why[300];
    PF_ENV e;

    if (check_folder(g_folder, canon, sizeof(canon), why, sizeof(why)) != 0) {
        SetWindowTextA(g_disk, why);
        return;
    }
    read_env(canon, &e);
    disk_facts(canon, &e);
    SetWindowTextA(g_disk, g_diskText);
    s98_find(canon);
    refresh_s98_page();
    show_tab();
}

static void fill_s98_tests(void)
{
    int i;

    SendMessageA(g_s98test, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_plan.nsteps; i++) {
        char s[80];

        sz_cpy(s, sizeof(s), g_plan.steps[i].label);
        sz_cat(s, sizeof(s), g_plan.steps[i].op == PF_OP_WRITE ? " write " : " read ");
        sz_cat(s, sizeof(s), pf_buf_name(g_plan.steps[i].buffer));
        SendMessageA(g_s98test, CB_ADDSTRING, 0, (LPARAM)s);
    }
}

static void save_settings(void)
{
    HKEY k;
    unsigned char b[8 + sizeof(PF_PARAMS)];
    DWORD n = (DWORD)sizeof(PF_PARAMS);

    if (RegCreateKeyA(HKEY_CURRENT_USER, PF_KEY, &k) != ERROR_SUCCESS) {
        return;
    }
    memcpy(b, "XPP1", 4);
    memcpy(b + 4, &n, 4);
    memcpy(b + 8, &g_p, sizeof(PF_PARAMS));
    RegSetValueExA(k, "Params", 0, REG_BINARY, b, sizeof(b));
    RegSetValueExA(k, "Folder", 0, REG_SZ, (const BYTE *)g_folder, (DWORD)strlen(g_folder) + 1);
    RegCloseKey(k);
}

static void load_settings(void)
{
    HKEY k;
    unsigned char b[8 + sizeof(PF_PARAMS)];
    DWORD n = sizeof(b), type, want = (DWORD)sizeof(PF_PARAMS);
    char f[MAX_PATH];

    if (RegOpenKeyExA(HKEY_CURRENT_USER, PF_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return;
    }
    if (RegQueryValueExA(k, "Params", NULL, &type, b, &n) == ERROR_SUCCESS && type == REG_BINARY && n == sizeof(b) &&
        memcmp(b, "XPP1", 4) == 0 && memcmp(b + 4, &want, 4) == 0) {
        PF_PARAMS p;
        PF_ENV e;
        PF_PLAN pl;
        char why[300];

        memcpy(&p, b + 8, sizeof(p));
        p.os = g_os;
        memset(&e, 0, sizeof(e));
        e.os = g_os;
        e.sector = 512;
        e.freeUser = e.freeTotal = e.volumeBytes = (pf_u64)1 << 50;
        if (p.ntests >= 0 && p.ntests <= PF_MAX_TESTS && pf_plan(&p, &e, &pl, why, sizeof(why)) == PF_OK) {
            g_p = p;
        }
    }
    n = sizeof(f);
    if (RegQueryValueExA(k, "Folder", NULL, &type, (LPBYTE)f, &n) == ERROR_SUCCESS && type == REG_SZ) {
        f[sizeof(f) - 1] = 0;
        sz_cpy(g_folder, sizeof(g_folder), f);
    }
    RegCloseKey(k);
}

static int writable_root(const char *root)
{
    char p[MAX_PATH];
    HANDLE h;

    sz_cpy(p, sizeof(p), root);
    sz_cat(p, sizeof(p), "XHCIPERF.CHK");
    h = CreateFileA(p, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    CloseHandle(h);
    DeleteFileA(p);
    return 1;
}

static void default_folder(void)
{
    char win[MAX_PATH], root[4];
    char c;

    GetWindowsDirectoryA(win, sizeof(win));
    for (c = 'C'; c <= 'Z'; c++) {
        root[0] = c;
        root[1] = ':';
        root[2] = '\\';
        root[3] = 0;
        if (c != win[0] && (c | 0x20) != (win[0] | 0x20) && GetDriveTypeA(root) == DRIVE_FIXED && writable_root(root)) {
            sz_cpy(g_folder, sizeof(g_folder), root);
            return;
        }
    }
    root_of(win, root, sizeof(root));
    if (writable_root(root)) {
        sz_cpy(g_folder, sizeof(g_folder), root);
        return;
    }
    GetTempPathA(sizeof(g_folder), g_folder);
}

static void browse(void)
{
    BROWSEINFOA bi;
    LPITEMIDLIST pidl;
    char path[MAX_PATH];
    HMODULE ole = LoadLibraryA("OLE32.DLL");
    PF_COINIT coinit = ole ? (PF_COINIT)GetProcAddress(ole, "CoInitialize") : NULL;

    if (coinit != NULL) {
        coinit(NULL);
    }
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = "Choose the drive or folder to test";
    bi.ulFlags = BIF_RETURNONLYFSDIRS;
    pidl = SHBrowseForFolderA(&bi);
    if (pidl != NULL) {
        LPMALLOC ma;

        if (SHGetPathFromIDListA(pidl, path)) {
            sz_cpy(g_folder, sizeof(g_folder), path);
            SetWindowTextA(ctl(IDC_FOLDER), g_folder);
            refresh_disk();
            update_estimate();
        }
        if (SHGetMalloc(&ma) == NOERROR) {
            ma->lpVtbl->Free(ma, pidl);
            ma->lpVtbl->Release(ma);
        }
    }
}

/* The workload is the run's while it lasts (record 29 section 5.1) */
static void controls_enable(int on)
{
    static const int ids[] = { IDC_FOLDER, IDC_BROWSE, IDC_PRESET, IDC_FSIZE, IDC_PASSES, IDC_TIME, IDC_OPS,
                               IDC_ACCESS, IDC_WT, IDC_BUFFER, IDC_PATTERN, IDC_VERIFY, IDC_THREADS, IDC_MORE };
    HMENU m = GetMenu(g_wnd);
    int i;

    for (i = 0; i < (int)(sizeof(ids) / sizeof(ids[0])); i++) {
        EnableWindow(ctl(ids[i]), on);
    }
    EnableWindow(ctl(IDC_DEPTH), on && g_os == PF_OS_NT);
    EnableMenuItem(m, IDM_COMPARE, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m, IDM_SAVEPRESET, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
}

static void refuse(const char *why)
{
    if (g_cl.exitAtEnd) {
        log_add(why);
        g_cliExit = 2;
        PostMessageA(g_wnd, WM_CLOSE, 0, 0);
        return;
    }
    MessageBoxA(g_wnd, why, "xHCI98 Disk Benchmark", MB_OK | MB_ICONINFORMATION);
}

static void start_run(void)
{
    PF_ENV e;
    char why[400], canon[MAX_PATH], path[MAX_PATH];
    unsigned id;
    SYSTEMTIME st;

    if (g_running) {
        return;
    }
    params_from_ui();
    if (check_folder(g_folder, canon, sizeof(canon), why, sizeof(why)) != 0) {
        refuse(why);
        return;
    }
    sz_cpy(g_folder, sizeof(g_folder), canon);
    file_path(g_folder, path, sizeof(path));
    if (GetFileAttributesA(path) != 0xFFFFFFFF && leftover_offer(g_wnd, path, g_cl.exitAtEnd) != 0) {
        refuse("XHCIPERF.TMP is in this folder and is kept; choose another folder.");
        return;
    }
    read_env(g_folder, &e);
    disk_facts(g_folder, &e);
    SetWindowTextA(g_disk, g_diskText);
    if (pf_plan(&g_p, &e, &g_plan, why, sizeof(why)) != PF_OK) {
        refuse(why);
        return;
    }
    s98_find(g_folder);
    memset(g_res, 0, sizeof(g_res));
    memset(g_s98steps, 0, sizeof(g_s98steps));
    g_npasses = g_p.passes;
    g_haveResults = 1;
    GetLocalTime(&st);
    sz_cpy(g_runDate, sizeof(g_runDate), "");
    sz_u(g_runDate, sizeof(g_runDate), st.wYear);
    sz_cat(g_runDate, sizeof(g_runDate), st.wMonth < 10 ? "-0" : "-");
    sz_u(g_runDate, sizeof(g_runDate), st.wMonth);
    sz_cat(g_runDate, sizeof(g_runDate), st.wDay < 10 ? "-0" : "-");
    sz_u(g_runDate, sizeof(g_runDate), st.wDay);
    sz_cat(g_runDate, sizeof(g_runDate), st.wHour < 10 ? " 0" : " ");
    sz_u(g_runDate, sizeof(g_runDate), st.wHour);
    sz_cat(g_runDate, sizeof(g_runDate), st.wMinute < 10 ? ":0" : ":");
    sz_u(g_runDate, sizeof(g_runDate), st.wMinute);
    sz_cpy(g_driveText, sizeof(g_driveText), g_folder);
    fill_s98_tests();
    save_settings();
    ResetEvent(g_stopEvent);
    InterlockedExchange((LONG *)&g_running, 1);
    InterlockedExchange((LONG *)&g_runBad, 0);
    controls_enable(0);
    EnableWindow(ctl(IDC_START), FALSE);
    EnableWindow(ctl(IDC_STOP), TRUE);
    log_add("Run started.");
    g_ctrl = (HANDLE)_beginthreadex(NULL, 0, run_main, NULL, 0, &id);
    if (g_ctrl == NULL) {
        InterlockedExchange((LONG *)&g_running, 0);
        controls_enable(1);
        EnableWindow(ctl(IDC_START), TRUE);
        EnableWindow(ctl(IDC_STOP), FALSE);
    }
    InvalidateRect(g_view, NULL, TRUE);
}

static void save_results(void)
{
    OPENFILENAMEA of;
    char path[MAX_PATH];
    size_t n;
    char *r;

    if (!g_haveResults) {
        return;
    }
    sz_cpy(path, sizeof(path), "XHCIPERF.TXT");
    memset(&of, 0, sizeof(of));
    of.lStructSize = sizeof(of);
    of.hwndOwner = g_wnd;
    of.lpstrFilter = "Text files (*.txt)\0*.txt\0All files\0*.*\0";
    of.lpstrFile = path;
    of.nMaxFile = sizeof(path);
    of.lpstrDefExt = "txt";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameA(&of)) {
        return;
    }
    r = build_report(&n);
    if (r != NULL) {
        if (!write_file(path, r, n)) {
            MessageBoxA(g_wnd, "The results could not be saved there.", "xHCI98 Disk Benchmark", MB_OK | MB_ICONWARNING);
        }
        HeapFree(GetProcessHeap(), 0, r);
    }
}

static void compare_results(void)
{
    OPENFILENAMEA of;
    char path[MAX_PATH];
    HANDLE h;
    DWORD size, got = 0;
    char *b;
    long bad;
    char key[512], msg[400];

    path[0] = 0;
    memset(&of, 0, sizeof(of));
    of.lStructSize = sizeof(of);
    of.hwndOwner = g_wnd;
    of.lpstrFilter = "Text files (*.txt)\0*.txt\0All files\0*.*\0";
    of.lpstrFile = path;
    of.nMaxFile = sizeof(path);
    of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&of)) {
        return;
    }
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    size = GetFileSize(h, NULL);
    if (size == 0xFFFFFFFF || size > 4u * 1048576u) {
        CloseHandle(h);
        return;
    }
    b = (char *)HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (b == NULL) {
        CloseHandle(h);
        return;
    }
    ReadFile(h, b, size, &got, NULL);
    CloseHandle(h);
    bad = pf_saved_parse(b, got, &g_saved);
    HeapFree(GetProcessHeap(), 0, b);
    if (bad != 0) {
        sz_cpy(msg, sizeof(msg), "This is not a results file of this tool's (line ");
        sz_u(msg, sizeof(msg), (pf_u64)bad);
        sz_cat(msg, sizeof(msg), ").");
        MessageBoxA(g_wnd, msg, "xHCI98 Disk Benchmark", MB_OK | MB_ICONINFORMATION);
        g_haveSaved = 0;
        return;
    }
    pf_key(&g_p, g_highRes, key, sizeof(key));
    if (!pf_key_same(g_saved.key, key, g_savedDiff, sizeof(g_savedDiff))) {
        sz_cpy(msg, sizeof(msg), "The saved results were measured differently (");
        sz_cat(msg, sizeof(msg), g_savedDiff);
        sz_cat(msg, sizeof(msg), "), so they are not comparable and are not shown beside these.");
        log_add(msg);
        MessageBoxA(g_wnd, msg, "xHCI98 Disk Benchmark", MB_OK | MB_ICONINFORMATION);
        g_haveSaved = 0;
    } else {
        g_haveSaved = 1;
        log_add("Saved results loaded for comparison.");
    }
    InvalidateRect(g_view, NULL, TRUE);
}

static void run_as_admin(void)
{
    char exe[MAX_PATH], args[MAX_PATH + 32];

    GetModuleFileNameA(NULL, exe, sizeof(exe));
    sz_cpy(args, sizeof(args), "/folder \"");
    sz_cat(args, sizeof(args), g_folder);
    sz_cat(args, sizeof(args), "\"");
    if (g_preVista) {
        MessageBoxA(g_wnd, "Run the xHCI98 Disk Benchmark as an administrator to read Stor98's counters.",
                    "xHCI98 Disk Benchmark", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if ((INT_PTR)ShellExecuteA(g_wnd, "runas", exe, args, NULL, SW_SHOWNORMAL) > 32) {
        PostMessageA(g_wnd, WM_CLOSE, 0, 0);
    }
}

static BOOL CALLBACK more_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    static const pf_u32 sw[] = { 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288,
                                 1048576, 2097152, 4194304, 8388608 };
    static const int pauses[] = { 0, 1, 5, 10, 30 };
    int i;
    char s[32];

    (void)lp;
    if (m == WM_INITDIALOG) {
        for (i = 0; i < PF_SIZES; i++) {
            pf_size_text(sw[i], s, sizeof(s));
            SendDlgItemMessageA(d, IDC_M_SWMIN, CB_ADDSTRING, 0, (LPARAM)s);
            SendDlgItemMessageA(d, IDC_M_SWMAX, CB_ADDSTRING, 0, (LPARAM)s);
            if (sw[i] == g_p.sweepMin) {
                SendDlgItemMessageA(d, IDC_M_SWMIN, CB_SETCURSEL, (WPARAM)i, 0);
            }
            if (sw[i] == g_p.sweepMax) {
                SendDlgItemMessageA(d, IDC_M_SWMAX, CB_SETCURSEL, (WPARAM)i, 0);
            }
        }
        for (i = 0; i < 5; i++) {
            s[0] = 0;
            sz_u(s, sizeof(s), (pf_u64)pauses[i]);
            sz_cat(s, sizeof(s), " s");
            SendDlgItemMessageA(d, IDC_M_PAUSE, CB_ADDSTRING, 0, (LPARAM)s);
            if (pauses[i] == g_p.pauseSec) {
                SendDlgItemMessageA(d, IDC_M_PAUSE, CB_SETCURSEL, (WPARAM)i, 0);
            }
        }
        s[0] = 0;
        sz_u(s, sizeof(s), g_p.sweepTotalMiB);
        SetDlgItemTextA(d, IDC_M_SWTOTAL, s);
        s[0] = 0;
        sz_u(s, sizeof(s), g_p.seed);
        SetDlgItemTextA(d, IDC_M_SEED, s);
        CheckDlgButton(d, IDC_M_SWEEP, g_p.sweep ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(d, IDC_M_SWTIMED, g_p.sweepTimed ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(d, IDC_M_WARMUP, g_p.warmup ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(d, IDC_M_FLUSH, g_p.flushInWrite ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(d, IDC_M_VDURING, g_p.verifyDuring ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(d, IDC_M_VEND, g_p.verifyEnd ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(d, IDC_M_STOPERR, g_p.stopOnError ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    if (m == WM_COMMAND && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)) {
        if (LOWORD(wp) == IDOK) {
            i = (int)SendDlgItemMessageA(d, IDC_M_SWMIN, CB_GETCURSEL, 0, 0);
            if (i >= 0) {
                g_p.sweepMin = sw[i];
            }
            i = (int)SendDlgItemMessageA(d, IDC_M_SWMAX, CB_GETCURSEL, 0, 0);
            if (i >= 0) {
                g_p.sweepMax = sw[i];
            }
            i = (int)SendDlgItemMessageA(d, IDC_M_PAUSE, CB_GETCURSEL, 0, 0);
            if (i >= 0) {
                g_p.pauseSec = pauses[i];
            }
            g_p.sweepTotalMiB = GetDlgItemInt(d, IDC_M_SWTOTAL, NULL, FALSE);
            g_p.seed = (pf_u32)GetDlgItemInt(d, IDC_M_SEED, NULL, FALSE);
            g_p.sweep = IsDlgButtonChecked(d, IDC_M_SWEEP) == BST_CHECKED;
            g_p.sweepTimed = IsDlgButtonChecked(d, IDC_M_SWTIMED) == BST_CHECKED;
            g_p.warmup = IsDlgButtonChecked(d, IDC_M_WARMUP) == BST_CHECKED;
            g_p.flushInWrite = IsDlgButtonChecked(d, IDC_M_FLUSH) == BST_CHECKED;
            g_p.verifyDuring = IsDlgButtonChecked(d, IDC_M_VDURING) == BST_CHECKED;
            g_p.verifyEnd = IsDlgButtonChecked(d, IDC_M_VEND) == BST_CHECKED;
            g_p.stopOnError = IsDlgButtonChecked(d, IDC_M_STOPERR) == BST_CHECKED;
            g_p.preset = PF_PRESET_CUSTOM;
        }
        EndDialog(d, LOWORD(wp));
        return TRUE;
    }
    return FALSE;
}

static BOOL CALLBACK about_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (m == WM_INITDIALOG) {
        SetDlgItemTextA(d, IDC_ABOUT_TEXT, "xHCI98 Disk Benchmark " XHCIPERF_VERSION "\r\n"
                        "Part of xHCI98. Free software under the GNU General Public License, version 2.\r\n"
                        "Laid out after the familiar disk benchmarks; none of their code, names or artwork is used.");
        return TRUE;
    }
    if (m == WM_COMMAND && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)) {
        EndDialog(d, 0);
        return TRUE;
    }
    return FALSE;
}

static void open_readme(void)
{
    char p[MAX_PATH];
    char *s;

    GetModuleFileNameA(NULL, p, sizeof(p));
    s = strrchr(p, '\\');
    if (s != NULL) {
        s[1] = 0;
    }
    sz_cat(p, sizeof(p), "XHCIPERF.TXT");
    if (GetFileAttributesA(p) == 0xFFFFFFFF) {
        s = strrchr(p, '\\');
        if (s != NULL) {
            s[1] = 0;
        }
        sz_cat(p, sizeof(p), "readme.txt");
    }
    ShellExecuteA(g_wnd, "open", "notepad.exe", p, NULL, SW_SHOWNORMAL);
}

static void create_controls(void)
{
    int i;
    TCITEMA ti;
    static const char *const tabs[] = { "Summary", "Sweep", "Disk", "Stor98", "Log" };
    char s[32];

    mk("STATIC", "Folder:", 0, 6, 8, 44, 16, -1);
    mk("EDIT", g_folder, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 52, 6, 250, 20, IDC_FOLDER);
    mk("BUTTON", "Browse...", BS_PUSHBUTTON | WS_TABSTOP, 306, 5, 70, 22, IDC_BROWSE);
    mk("STATIC", "Preset:", 0, 384, 8, 40, 16, -1);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 426, 5, 204, 200, IDC_PRESET);
    for (i = 0; i < PF_PRESETS; i++) {
        combo_add(IDC_PRESET, pf_preset_name(i));
    }
    combo_add(IDC_PRESET, "Custom");
    SendMessageA(ctl(IDC_PRESET), CB_SETDROPPEDWIDTH, 260, 0);

    mk("STATIC", "File:", 0, 6, 36, 30, 16, -1);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 38, 33, 84, 200, IDC_FSIZE);
    for (i = 0; i < (int)(sizeof(g_sizes) / sizeof(g_sizes[0])); i++) {
        s[0] = 0;
        if (g_sizes[i] >= 1024) {
            sz_u(s, sizeof(s), g_sizes[i] / 1024u);
            sz_cat(s, sizeof(s), " GiB");
        } else {
            sz_u(s, sizeof(s), g_sizes[i]);
            sz_cat(s, sizeof(s), " MiB");
        }
        combo_add(IDC_FSIZE, s);
    }
    mk("STATIC", "Passes:", 0, 128, 36, 42, 16, -1);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 172, 33, 40, 200, IDC_PASSES);
    for (i = 1; i <= 9; i++) {
        s[0] = 0;
        sz_u(s, sizeof(s), (pf_u64)i);
        combo_add(IDC_PASSES, s);
    }
    mk("STATIC", "Time:", 0, 218, 36, 30, 16, -1);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 250, 33, 56, 200, IDC_TIME);
    for (i = 0; i < (int)(sizeof(g_times) / sizeof(g_times[0])); i++) {
        s[0] = 0;
        sz_u(s, sizeof(s), (pf_u64)g_times[i]);
        sz_cat(s, sizeof(s), " s");
        combo_add(IDC_TIME, s);
    }
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP, 312, 33, 110, 200, IDC_OPS);
    combo_add(IDC_OPS, "Read and write");
    combo_add(IDC_OPS, "Read");
    combo_add(IDC_OPS, "Write");
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP, 428, 33, 96, 200, IDC_ACCESS);
    combo_add(IDC_ACCESS, "Unbuffered");
    combo_add(IDC_ACCESS, "Cached");
    mk("BUTTON", "Write-through", BS_AUTOCHECKBOX | WS_TABSTOP, 530, 34, 100, 18, IDC_WT);

    mk("STATIC", "Buffer:", 0, 6, 64, 40, 16, -1);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP, 48, 61, 96, 200, IDC_BUFFER);
    combo_add(IDC_BUFFER, "Aligned");
    combo_add(IDC_BUFFER, "Offset");
    combo_add(IDC_BUFFER, "Scattered");
    combo_add(IDC_BUFFER, "All three");
    mk("STATIC", "Pattern:", 0, 150, 64, 46, 16, -1);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP, 198, 61, 90, 200, IDC_PATTERN);
    combo_add(IDC_PATTERN, "Verifiable");
    combo_add(IDC_PATTERN, "Random");
    combo_add(IDC_PATTERN, "Zeros");
    mk("BUTTON", "Verify", BS_AUTOCHECKBOX | WS_TABSTOP, 294, 62, 56, 18, IDC_VERIFY);
    mk("STATIC", "Depth:", 0, 354, 64, 36, 16, -1);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 390, 61, 62, 200, IDC_DEPTH);
    combo_add(IDC_DEPTH, "Preset");
    for (i = 0; i < (int)(sizeof(g_depths) / sizeof(g_depths[0])); i++) {
        s[0] = 0;
        sz_u(s, sizeof(s), (pf_u64)g_depths[i]);
        combo_add(IDC_DEPTH, s);
    }
    mk("STATIC", "Threads:", 0, 456, 64, 46, 16, -1);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 504, 61, 62, 200, IDC_THREADS);
    combo_add(IDC_THREADS, "Preset");
    for (i = 0; i < (int)(sizeof(g_threadsList) / sizeof(g_threadsList[0])); i++) {
        s[0] = 0;
        sz_u(s, sizeof(s), (pf_u64)g_threadsList[i]);
        combo_add(IDC_THREADS, s);
    }
    mk("BUTTON", "More...", BS_PUSHBUTTON | WS_TABSTOP, 572, 60, 58, 22, IDC_MORE);
    mk("STATIC", "", SS_LEFTNOWORDWRAP, 4, 94, 500, 18, IDC_ESTIMATE);
    mk("BUTTON", "Start", BS_DEFPUSHBUTTON | WS_TABSTOP, 506, 90, 60, 23, IDC_START);
    mk("BUTTON", "Stop", BS_PUSHBUTTON | WS_TABSTOP, 570, 90, 60, 23, IDC_STOP);
    EnableWindow(ctl(IDC_STOP), FALSE);

    g_tab = mk(WC_TABCONTROLA, "", WS_CLIPSIBLINGS | WS_TABSTOP, 4, 116, 620, 300, IDC_TAB);
    for (i = 0; i < 5; i++) {
        memset(&ti, 0, sizeof(ti));
        ti.mask = TCIF_TEXT;
        ti.pszText = (char *)tabs[i];
        SendMessageA(g_tab, TCM_INSERTITEMA, (WPARAM)i, (LPARAM)&ti);
    }
    g_view = mk("XhciPerfView", "", 0, 8, 140, 600, 260, IDC_VIEW);
    g_disk = mk("EDIT", "", WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 8, 140, 600, 260, IDC_DISKTEXT);
    g_s98 = mk("EDIT", "", WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 8, 168, 600, 232, IDC_S98TEXT);
    g_s98test = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 8, 140, 300, 200, IDC_S98TEST);
    g_s98admin = mk("BUTTON", "Read as administrator...", BS_PUSHBUTTON, 314, 140, 180, 23, IDC_S98ADMIN);
    g_log = mk("EDIT", "", WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 8, 140, 600, 260, IDC_LOG);
    SendMessageA(g_log, EM_SETLIMITTEXT, 1048576, 0);
    g_status = CreateWindowExA(0, STATUSCLASSNAMEA, "Ready", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, g_wnd,
                               (HMENU)IDC_STATUS, g_inst, NULL);
    SendMessageA(g_status, WM_SETFONT, (WPARAM)g_font, TRUE);
}

static void set_menu_checks(void)
{
    HMENU m = GetMenu(g_wnd);

    CheckMenuItem(m, IDM_UNIT_MB, g_p.unitMiB ? MF_UNCHECKED : MF_CHECKED);
    CheckMenuItem(m, IDM_UNIT_MIB, g_p.unitMiB ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_SHOW_BEST, g_p.show == PF_SHOW_BEST ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_SHOW_MEAN, g_p.show == PF_SHOW_MEAN ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_SHOW_MEDIAN, g_p.show == PF_SHOW_MEDIAN ? MF_CHECKED : MF_UNCHECKED);
}

static LRESULT CALLBACK main_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        g_wnd = w;
        create_controls();
        ui_from_params();
        set_menu_checks();
        layout();
        show_tab();
        PostMessageA(w, WM_APP_STATUS, 1, 0);
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = 640;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = 440;
        return 0;
    case WM_NOTIFY:
        if (((NMHDR *)lp)->hwndFrom == g_tab && ((NMHDR *)lp)->code == TCN_SELCHANGE) {
            show_tab();
            if (g_tabSel == 3) {
                refresh_s98_page();
            }
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE:
            browse();
            return 0;
        case IDC_PRESET:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int i = combo_get(IDC_PRESET);

                if (i >= 0 && i < PF_PRESETS) {
                    pf_preset(&g_p, i);
                    ui_from_params();
                }
                update_estimate();
            }
            return 0;
        case IDC_FSIZE:
        case IDC_PASSES:
        case IDC_TIME:
        case IDC_OPS:
        case IDC_ACCESS:
        case IDC_BUFFER:
        case IDC_PATTERN:
        case IDC_DEPTH:
        case IDC_THREADS:
            if (HIWORD(wp) == CBN_SELCHANGE && !g_ui) {
                g_p.preset = PF_PRESET_CUSTOM;
                combo_set(IDC_PRESET, PF_PRESETS);
                update_estimate();
            }
            return 0;
        case IDC_WT:
        case IDC_VERIFY:
            if (!g_ui) {
                g_p.preset = PF_PRESET_CUSTOM;
                combo_set(IDC_PRESET, PF_PRESETS);
                update_estimate();
            }
            return 0;
        case IDC_FOLDER:
            if (HIWORD(wp) == EN_KILLFOCUS && !g_running) {
                params_from_ui();
                refresh_disk();
                update_estimate();
            }
            return 0;
        case IDC_MORE:
            params_from_ui();
            if (DialogBoxParamA(g_inst, MAKEINTRESOURCEA(IDD_MORE), w, (DLGPROC)more_proc, 0) == IDOK) {
                combo_set(IDC_PRESET, PF_PRESETS);
                update_estimate();
            }
            return 0;
        case IDC_START:
            start_run();
            return 0;
        case IDC_STOP:
            SetEvent(g_stopEvent);
            SetWindowTextA(g_status, "Stopping after the requests in flight");
            return 0;
        case IDC_S98TEST:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                refresh_s98_page();
            }
            return 0;
        case IDC_S98ADMIN:
            run_as_admin();
            return 0;
        case IDM_SAVE:
            save_results();
            return 0;
        case IDM_COMPARE:
            compare_results();
            return 0;
        case IDM_SAVEPRESET:
            if (g_running) {
                return 0;
            }
            params_from_ui();
            save_settings();
            log_add("These settings are kept for the next start.");
            return 0;
        case IDM_EXIT:
            PostMessageA(w, WM_CLOSE, 0, 0);
            return 0;
        case IDM_COPY:
            if (g_haveResults) {
                size_t n;
                char *r = build_report(&n);

                if (r != NULL) {
                    copy_text(w, r, n);
                    HeapFree(GetProcessHeap(), 0, r);
                }
            }
            return 0;
        case IDM_COPYPAGE:
            {
                HWND src = g_tabSel == 2 ? g_disk : (g_tabSel == 3 ? g_s98 : (g_tabSel == 4 ? g_log : NULL));

                if (src != NULL) {
                    int n = GetWindowTextLengthA(src);
                    char *b = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)n + 1);

                    if (b != NULL) {
                        GetWindowTextA(src, b, n + 1);
                        copy_text(w, b, (size_t)n);
                        HeapFree(GetProcessHeap(), 0, b);
                    }
                } else if (g_haveResults) {
                    PostMessageA(w, WM_COMMAND, IDM_COPY, 0);
                }
            }
            return 0;
        case IDM_UNIT_MB:
        case IDM_UNIT_MIB:
            g_p.unitMiB = LOWORD(wp) == IDM_UNIT_MIB;
            set_menu_checks();
            InvalidateRect(g_view, NULL, TRUE);
            return 0;
        case IDM_SHOW_BEST:
        case IDM_SHOW_MEAN:
        case IDM_SHOW_MEDIAN:
            g_p.show = LOWORD(wp) == IDM_SHOW_BEST ? PF_SHOW_BEST : (LOWORD(wp) == IDM_SHOW_MEAN ? PF_SHOW_MEAN : PF_SHOW_MEDIAN);
            set_menu_checks();
            InvalidateRect(g_view, NULL, TRUE);
            return 0;
        case IDM_README:
            open_readme();
            return 0;
        case IDM_ABOUT:
            DialogBoxParamA(g_inst, MAKEINTRESOURCEA(IDD_ABOUT), w, (DLGPROC)about_proc, 0);
            return 0;
        }
        break;
    case WM_APP_LOG:
        log_add((char *)lp);
        HeapFree(GetProcessHeap(), 0, (void *)lp);
        return 0;
    case WM_APP_STATUS:
        if (wp == 1) {
            refresh_disk();
            update_estimate();
            if (g_cl.run) {
                PostMessageA(w, WM_COMMAND, IDC_START, 0);
            }
            return 0;
        }
        SetWindowTextA(g_status, (char *)lp);
        HeapFree(GetProcessHeap(), 0, (void *)lp);
        return 0;
    case WM_APP_RESULT:
        InvalidateRect(g_view, NULL, TRUE);
        if ((int)SendMessageA(g_s98test, CB_GETCURSEL, 0, 0) < 0) {
            SendMessageA(g_s98test, CB_SETCURSEL, wp, 0);
        }
        if (g_tabSel == 3) {
            refresh_s98_page();
        }
        return 0;
    case WM_APP_DONE:
        WaitForSingleObject(g_ctrl, INFINITE);
        CloseHandle(g_ctrl);
        g_ctrl = NULL;
        InterlockedExchange((LONG *)&g_running, 0);
        controls_enable(1);
        EnableWindow(ctl(IDC_START), TRUE);
        EnableWindow(ctl(IDC_STOP), FALSE);
        SetWindowTextA(g_status, "Ready");
        log_add("Run ended.");
        InvalidateRect(g_view, NULL, TRUE);
        if (g_cl.report[0] != 0) {
            size_t n;
            char *r = build_report(&n);

            if (r != NULL) {
                write_file(g_cl.report, r, n);
                HeapFree(GetProcessHeap(), 0, r);
            }
        }
        {
            int i, k, failed = 0, data = 0;

            for (i = 0; i < g_plan.nsteps; i++) {
                for (k = 0; k < g_npasses; k++) {
                    int st = g_res[i * PF_MAX_PASSES + k].status;

                    if (st == PF_ST_FAILED) {
                        failed = 1;
                    }
                    if (st == PF_ST_DATAERR) {
                        data = 1;
                    }
                }
            }
            g_cliExit = (failed || data || g_runBad) ? 1 : (int)wp;
        }
        if (lp != 0) {
            if (!g_cl.exitAtEnd) {
                MessageBoxA(w, (char *)lp, "xHCI98 Disk Benchmark", MB_OK | MB_ICONINFORMATION);
            }
            HeapFree(GetProcessHeap(), 0, (void *)lp);
        }
        if (g_cl.exitAtEnd) {
            PostMessageA(w, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_CLOSE:
        if (g_running) {
            if (g_cl.exitAtEnd || MessageBoxA(w, "Stop the test and close?", "xHCI98 Disk Benchmark",
                                              MB_YESNO | MB_ICONQUESTION) == IDYES) {
                SetEvent(g_stopEvent);
                g_cl.exitAtEnd = 1;
                SetWindowTextA(g_status, "Waiting for the disk to finish the last request");
            }
            return 0;
        }
        params_from_ui();
        save_settings();
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}

/* ------------------------------------------------------------------------ */
/* The self-test (record 29 section 10.3)                                    */
/* ------------------------------------------------------------------------ */

static void st_check(int ok, const char *what)
{
    g_selftestChecks++;
    if (!ok) {
        g_selftestFails++;
        out_text("FAIL: ");
        out_text(what);
        out_text("\r\n");
    }
}

static int selftest(void)
{
    PF_PARAMS p;
    PF_ENV e;
    static PF_PLAN pl;
    static PF_RESULT res[PF_MAX_STEPS * PF_MAX_PASSES];
    PF_RUNINFO ri;
    PF_TEXT t;
    static char buf[256 * 1024];
    char why[400], line[64];
    int i, pr;
    unsigned char u[PF_UNIT];
    pf_u32 fb, fg;

    for (pr = 0; pr < PF_PRESETS; pr++) {
        pf_defaults(&p, PF_OS_NT);
        pf_preset(&p, pr);
        memset(&e, 0, sizeof(e));
        e.os = PF_OS_NT;
        e.sector = 512;
        e.freeUser = e.freeTotal = (pf_u64)100000 * 1048576u;
        e.volumeBytes = (pf_u64)200000 * 1048576u;
        st_check(pf_plan(&p, &e, &pl, why, sizeof(why)) == PF_OK, pf_preset_name(pr));
        memset(res, 0, sizeof(res));
        for (i = 0; i < pl.nsteps; i++) {
            res[i * PF_MAX_PASSES].status = PF_ST_OK;
            res[i * PF_MAX_PASSES].bytes = (pf_u64)1000000000u * 9u;
            res[i * PF_MAX_PASSES].us = 1000000u;
            res[i * PF_MAX_PASSES].reqs = 99999999u;
            pf_lat_init(&res[i * PF_MAX_PASSES].lat);
            pf_lat_add(&res[i * PF_MAX_PASSES].lat, 1234567u);
        }
        memset(&ri, 0, sizeof(ri));
        ri.toolVersion = XHCIPERF_VERSION;
        ri.date = "2026-10-22 13:00";
        ri.system = "Windows 6.1.7601 Service Pack 1 x64";
        ri.drive = "F:\\";
        ri.fileSystem = "NTFS";
        ri.model = "";
        ri.sector = 512;
        ri.timerFreq = 3579545u;
        ri.stor98 = "";
        pf_tx_init(&t, buf, sizeof(buf));
        pf_report(&t, &p, &pl, res, 1, &ri);
        st_check(!t.overflow, "the report fits its buffer");
        out_text(buf);
    }
    pf_stamp(u, PF_UNIT, 1, 2, 3);
    st_check(pf_check(u, PF_UNIT, 1, 2, 3, &fb, &fg) == 0, "the pattern checks");
    st_check(sizeof(STOR98PF_ANSWER) == S98_ANSWER_BYTES, "Stor98's answer size");
    st_check(qpc_now() != 0 || !g_highRes, "the timer runs");
    line[0] = 0;
    sz_u(line, sizeof(line), (pf_u64)g_selftestChecks);
    sz_cat(line, sizeof(line), " checks, ");
    sz_u(line, sizeof(line), (pf_u64)g_selftestFails);
    sz_cat(line, sizeof(line), " failures\r\n");
    out_text(line);
    return g_selftestFails != 0;
}

/* ------------------------------------------------------------------------ */
/* Start                                                                     */
/* ------------------------------------------------------------------------ */

static int wow64_relaunch(void)
{
#ifdef _WIN64
    return -1;
#else
    PF_ISWOW64 isw = (PF_ISWOW64)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), "IsWow64Process");
    BOOL wow = FALSE;
    char exe[MAX_PATH], *s;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD rc = 1;
    const char *cmd;
    char *line;
    size_t n;

    if (isw == NULL || !isw(GetCurrentProcess(), &wow) || !wow) {
        return -1;
    }
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    s = strrchr(exe, '\\');
    if (s == NULL) {
        return -1;
    }
    sz_cpy(s + 1, sizeof(exe) - (size_t)(s + 1 - exe), "XHCIPF64.EXE");
    if (GetFileAttributesA(exe) == 0xFFFFFFFF) {
        return -1;
    }
    cmd = GetCommandLineA();
    if (*cmd == '"') {
        cmd = strchr(cmd + 1, '"');
        cmd = cmd ? cmd + 1 : "";
    } else {
        while (*cmd && *cmd != ' ') {
            cmd++;
        }
    }
    n = strlen(exe) + strlen(cmd) + 4;
    line = (char *)HeapAlloc(GetProcessHeap(), 0, n);
    if (line == NULL) {
        return -1;
    }
    sz_cpy(line, n, "\"");
    sz_cat(line, n, exe);
    sz_cat(line, n, "\"");
    sz_cat(line, n, cmd);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(exe, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        HeapFree(GetProcessHeap(), 0, line);
        return -1;
    }
    HeapFree(GetProcessHeap(), 0, line);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)rc;
#endif
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdLine, int show)
{
    WNDCLASSA wc;
    MSG msg;
    HWND w;
    char why[300];
    LARGE_INTEGER f;
    OSVERSIONINFOA v;
    SYSTEM_INFO si;
    HMODULE k32 = GetModuleHandleA("KERNEL32.DLL");
    INITCOMMONCONTROLSEX icc;
    NONCLIENTMETRICSA ncm;
    LOGFONTA lf;
    int i;

    (void)prev;
    (void)cmdLine;
    g_inst = inst;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    g_os = (GetVersion() & 0x80000000u) ? PF_OS_9X : PF_OS_NT;
    memset(&v, 0, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    GetVersionExA(&v);
    g_preVista = (g_os == PF_OS_NT && v.dwMajorVersion < 6);
    GetSystemInfo(&si);
    g_ncpu = si.dwNumberOfProcessors > 32 ? 32 : si.dwNumberOfProcessors;
    if (QueryPerformanceFrequency(&f) && f.QuadPart != 0) {
        g_highRes = 1;
        g_freq = (pf_u64)f.QuadPart;
    }
    g_getFreeEx = (PF_GETFREEEX)GetProcAddress(k32, "GetDiskFreeSpaceExA");
    g_getLongPath = (PF_GETLONGPATH)GetProcAddress(k32, "GetLongPathNameA");
    g_cancelIo = (PF_CANCELIO)GetProcAddress(k32, "CancelIo");
    g_setExec = (PF_SETEXEC)GetProcAddress(k32, "SetThreadExecutionState");
    g_queryDos = (g_os == PF_OS_NT) ? (PF_QUERYDOS)GetProcAddress(k32, "QueryDosDeviceA") : NULL;
    g_setFileInfo = (PF_SETFILEINFO)GetProcAddress(k32, "SetFileInformationByHandle");
    system_text(g_system, sizeof(g_system));

    pf_defaults(&g_p, g_os);
    load_settings();
    g_p.os = g_os;
    InitializeCriticalSection(&g_resCs);
    if (pf_cmdline(__argc, __argv, &g_p, &g_cl, why, sizeof(why)) != 0) {
        if (std_out_ok()) {
            out_text(why);
            out_text("\r\n");
        } else {
            MessageBoxA(NULL, why, "xHCI98 Disk Benchmark", MB_OK | MB_ICONINFORMATION);
        }
        return 2;
    }
    if (g_cl.selftest) {
        return selftest();
    }
    i = wow64_relaunch();
    if (i >= 0) {
        return i;
    }
    if (g_cl.folder[0] != 0) {
        sz_cpy(g_folder, sizeof(g_folder), g_cl.folder);
    } else if (g_folder[0] == 0) {
        default_folder();
    }
    g_cliExit = 0;
    g_stopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    if (!InitCommonControlsEx(&icc)) {
        InitCommonControls();
    }
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        g_font = CreateFontIndirectA(&ncm.lfMessageFont);
        lf = ncm.lfMessageFont;
    } else {
        g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        GetObjectA(g_font, sizeof(lf), &lf);
    }
    lf.lfHeight = lf.lfHeight * 2;
    lf.lfWeight = FW_BOLD;
    g_big = CreateFontIndirectA(&lf);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = view_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "XhciPerfView";
    RegisterClassA(&wc);
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(IDI_APP));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEA(IDM_MAIN);
    wc.lpszClassName = "XhciPerfMain";
    RegisterClassA(&wc);
    w = CreateWindowExA(0, "XhciPerfMain", "xHCI98 Disk Benchmark", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                        648, 476, NULL, NULL, inst, NULL);
    if (w == NULL) {
        return 2;
    }
    ShowWindow(w, show);
    UpdateWindow(w);
    {
        PF_RECORD rec;
        char rp[MAX_PATH];

        rp[0] = 0;
        if (!g_cl.run && record_get(&rec, rp, sizeof(rp)) && GetFileAttributesA(rp) != 0xFFFFFFFF) {
            leftover_offer(w, rp, 0);
        } else if (!g_cl.run && rec.present && GetFileAttributesA(rp) == 0xFFFFFFFF) {
            record_clear();
        }
    }
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    return g_cliExit;
}
