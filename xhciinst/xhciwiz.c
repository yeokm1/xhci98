/*
 * xhciwiz.c - XHCIWIZ.EXE (32-bit) and XHCIWZ64.EXE (x64): the installer's
 * window (design record 26; roadmap-usb4.md tasks 42-A.2 and 42-A.3).
 *
 * WHY IT EXISTS. The owner's instruction of 2026-10-18: the installer has
 * to be a window that walks the user through the key options and then
 * asks for the restart. This is that window, a wizard over the same
 * engine the console tool runs (inst_eng.c): welcome, what was found,
 * the components, the USB settings, the Thunderbolt settings, the
 * summary, the progress, on 98 SE and ME the steps for a device the
 * helper could not install, and the finish with the restart. Under
 * /uninstall it is the uninstall the Add/Remove Programs entry runs.
 *
 * THE ENGINE. Every rule of record 23 is the engine's: the window shows
 * its plan as a list, keeps the answers on its pages and hands them back
 * as the engine asks (the components as the Thunderbolt choice and /to
 * and /notray, the settings as chosen values), and shows nothing until
 * Install but what the engine read. The engine runs on one worker thread
 * at a time (W3), so the window's own thread keeps the pages drawn and
 * owns Windows' dialogs.
 *
 * TARGETS AND BUILDS. ANSI C89, the in-repo MSVC 6.0, the C runtime static
 * and multithreaded (/MT), /SUBSYSTEM:WINDOWS,4.0, so one 32-bit image
 * starts on 98 SE, ME, 2000, XP and 32-bit Vista and 7; under WOW64 it
 * starts XHCIWZ64.EXE from its own directory and returns that one's exit
 * code (W4). Imports: KERNEL32, USER32, GDI32, ADVAPI32, COMCTL32,
 * COMDLG32 and SHELL32 (check-imports.ps1); every SetupAPI and
 * configuration-manager routine is the engine's, looked up at run time.
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/xhci_version.h"
#include "inst_core.h"
#include "inst_eng.h"
#include "wiz_lay.h"
#include "xhciwiz_res.h"

/* MSVC 6.0's headers predate the pointer-sized window longs and declare a
 * dialog procedure returning BOOL; WDK 7.1's take the _PTR forms. */
#ifdef _WIN64
#define INST_MY_ARCH        INST_ARCH_AMD64
#define SET_RESULT(h, v)    SetWindowLongPtrA((h), DWLP_MSGRESULT, (LONG_PTR)(v))
#define GET_PAGE(h)         ((int)GetWindowLongPtrA((h), GWLP_USERDATA))
#define SET_PAGE(h, v)      SetWindowLongPtrA((h), GWLP_USERDATA, (LONG_PTR)(v))
#define GET_PROC(h)         ((WNDPROC)GetWindowLongPtrA((h), GWLP_WNDPROC))
#define SET_PROC(h, p)      SetWindowLongPtrA((h), GWLP_WNDPROC, (LONG_PTR)(p))
#define WIZ_DLGRET          INT_PTR
#else
#define INST_MY_ARCH        INST_ARCH_X86
#define SET_RESULT(h, v)    SetWindowLongA((h), DWL_MSGRESULT, (LONG)(v))
#define GET_PAGE(h)         ((int)GetWindowLongA((h), GWL_USERDATA))
#define SET_PAGE(h, v)      SetWindowLongA((h), GWL_USERDATA, (LONG)(v))
#define GET_PROC(h)         ((WNDPROC)GetWindowLongA((h), GWL_WNDPROC))
#define SET_PROC(h, p)      SetWindowLongA((h), GWL_WNDPROC, (LONG)(size_t)(p))
#define WIZ_DLGRET          BOOL
#endif

/* The sheet's own buttons (prsht.h's dialog ids). */
#define ID_SHEET_BACK       0x3023
#define ID_SHEET_NEXT       0x3024
#define ID_SHEET_FINISH     0x3025

#define WM_APP_DONE         (WM_APP + 1)    /* a job ended: wParam the job */
#define WM_APP_STEP         (WM_APP + 2)    /* a step: wParam state, lParam text */
#define WM_APP_WAIT         (WM_APP + 3)    /* a wait: lParam text           */
#define WM_APP_LINE         (WM_APP + 4)    /* report text: lParam text      */
#define WM_APP_GATE         (WM_APP + 5)    /* sent: 1 go on, 0 stop         */
#define WM_APP_FOCUS        (WM_APP + 6)    /* the focus to Next or Finish   */
#define WM_APP_FIT          (WM_APP + 7)    /* the sheet moved into the work area */

#define JOB_READ            1
#define JOB_RUN             2
#define JOB_UNINSTALL       3

/* The pages (record 26 section 5; section 9 for the uninstall's). */
#define P_WELCOME           0
#define P_FOUND             1
#define P_COMP              2
#define P_USB               3
#define P_TBS               4
#define P_S98               5
#define P_SUMMARY           6
#define P_PROGRESS          7
#define P_STEPS             8
#define P_FINISH            9
#define P_OTHER             10  /* after P_COMP (record 26 section 6.7.4) */
#define P_COUNT             11

#define WIZ_PATH            520
#define WIZ_ROWS            12

/* ---- state ---------------------------------------------------------------- */

static HINSTANCE hInst;
static HWND sheet;
static HWND pageWnd[P_COUNT];
static int gArgc;
static char **gArgv;
static INST_OPTS *opts;
static int uninstallMode;
static const char *refusal;     /* the window refuses the switches */

static LONG busy;               /* a job runs on the worker thread */
static LONG cancelAsked;
static int readCode = -1;       /* begin and read: -1 went on */
static int planCode = -1;
static int runCode = INST_EXIT_REFUSED;
static int ran;                 /* Install or Remove was pressed */
static int ended;               /* inst_eng_end was called */
static int exitCode = INST_EXIT_REFUSED;
static int finalLine = INST_FINAL_NO_CHANGES;

static int tbHave;
static int tbPresent;
static int tbDefault;
static int tbInstalled;
static int tbChoice;
/* The USB drivers' box (record 26 section 5.3, draft 8): the choice, and
 * whether it can be unticked; the Thunderbolt box's own choice while the
 * USB drivers are out and it is locked on. */
static int usbChoice = 1;
static int usbOffered;
static int usbNotHere;
static int tbOwn;
static int uasChoice = 1;       /* the UAS driver's box (owner, 2026-10-19) */
static int uasOwn = 1;          /* its own state while the USB box is off   */
static int trayChoice = 1;
static int trayHere;
/* Task 43.27 (record 26 section 5.3, draft 33): the "can run faster" box,
 * the computer's choice in force, and whether /faster or /nofaster made it
 * changed. */
static int fastChoice = 1;
static int fastNow = 1;
static int fastForced;
/* The USB storage drivers' box, Windows 98 SE alone (record 26 section
 * 6.5): shown, offered (the download's seven verified), ticked, the
 * detection's line. */
static int storShown;
static int storOffered;
static int storChoice;
static char storNote[600];
static int compLaidOut;

/* Stor98, the bundled NVMe driver (record 26 sections 5.3 and 5.6): the
 * box, and the settings chosen (a word or a number as text, set or kept),
 * handed to the engine at Install; on 98 SE and ME the Summary page's
 * boxes for the drives an update touches. */
static int s98Have;
static int s98Choice;
/* S98VSD.VXD's box (design record 30 section 7.1): Stor98's child. */
static int vsdShown;
static int vsd9x;
static int vsdChoice;
/* Record 26 section 5.0 (draft 35): Stor98's and S98VSD.VXD's own states,
 * the table's default or the user's untick, kept while their parents are
 * unticked; the "Replace the stored build" box. */
static int s98Own = 1;
static int vsdOwn = 1;
static int restageSys;          /* Vista and later: the row is on the page */
static int restageHere;         /* the store trap found: the row open      */
static int restageChoice;
static int readDone;            /* the reading's job has ended */
static char s98Ver[32];
static int s98WinSet[INST_S98_COUNT];
static char s98WinVal[INST_S98_COUNT][32];
static char dirChoice[WIZ_PATH];
static char dirNow[WIZ_PATH];

/* The settings chosen on the pages and in Advanced (record 26 section
 * 5.4): set, and the value; handed to the engine at Install. */
static int choiceSet[INST_MAX_VALUES];

/* A choice whose driver is in the run as the components stand: a value of
 * the USB drivers' INFs only with them ticked, the connection manager's
 * only with it. A choice of a component left out is kept, not shown and
 * not handed to the engine, so ticking the box again brings it back. */
static int choice_live(int v)
{
    int inf = instValues[v].Inf;

    if (inf == INST_INF_XHCI || inf == INST_INF_UAS) {
        return usbChoice;
    }
    if (inf == INST_INF_CM) {
        return tbChoice;
    }
    return 1;
}
static INST_U32 choiceVal[INST_MAX_VALUES];

static INST_ROW rows[INST_MAX_VALUES];
static int rowCount;

/* What the engine reported, per phase. */
typedef struct _TEXTBUF {
    char *Buf;
    size_t Len;
    size_t Cap;
} TEXTBUF;

static CRITICAL_SECTION textLock;
static TEXTBUF readText;
static TEXTBUF planText;
static TEXTBUF runText;
static TEXTBUF *sink = &readText;

/* ---- text ------------------------------------------------------------------ */

static void tb_add(TEXTBUF *t, const char *s)
{
    size_t n = strlen(s);
    char *nb;

    if (t->Len + n + 1 > t->Cap) {
        size_t cap = t->Cap * 2 + n + 4096;
        nb = (char *)realloc(t->Buf, cap);
        if (nb == NULL) {
            return;
        }
        t->Buf = nb;
        t->Cap = cap;
    }
    memcpy(t->Buf + t->Len, s, n + 1);
    t->Len += n;
}

static void tb_clear(TEXTBUF *t)
{
    t->Len = 0;
    if (t->Buf != NULL) {
        t->Buf[0] = '\0';
    }
}

/* LF to CRLF, for an edit control; the caller frees. */
static char *crlf(const char *s)
{
    size_t n = 0;
    const char *p;
    char *out;
    char *q;

    for (p = s; *p != '\0'; p++) {
        n += (*p == '\n') ? 2 : 1;
    }
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    for (p = s, q = out; *p != '\0'; p++) {
        if (*p == '\n' && (p == s || p[-1] != '\r')) {
            *q++ = '\r';
        }
        *q++ = *p;
    }
    *q = '\0';
    return out;
}

static void set_edit(HWND dlg, int id, const char *s)
{
    char *c = crlf(s != NULL ? s : "");

    if (c != NULL) {
        SetDlgItemTextA(dlg, id, c);
        free(c);
    }
}

static char *dup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);

    if (d != NULL) {
        memcpy(d, s, n);
    }
    return d;
}

/* ---- the front end (record 26 section 3.2) -------------------------------- */

static void wiz_line(void *ctx, const char *text)
{
    (void)ctx;
    if (text[0] == '\0') {
        return;
    }
    inst_eng_log_text(text);
    EnterCriticalSection(&textLock);
    tb_add(sink, text);
    LeaveCriticalSection(&textLock);
    if (sink == &runText && pageWnd[P_PROGRESS] != NULL) {
        char *d = dup(text);
        if (d != NULL &&
            !PostMessageA(pageWnd[P_PROGRESS], WM_APP_LINE, 0, (LPARAM)d)) {
            free(d);
        }
    }
}

/* The window answers from its pages: every question but the gate has
 * its answer already; a question asked again is never answered twice. */
static int wiz_ask(void *ctx, const INST_ASK *q, char *answer, size_t cap)
{
    (void)ctx;
    if (q->Retry) {
        return 0;
    }
    switch (q->Kind) {
    case INST_Q_TB:
        inst_copy(answer, tbChoice ? "Y" : "N", cap);
        return 1;
    case INST_Q_VALUE:
        answer[0] = '\0';
        return 1;
    case INST_Q_DIR:
        inst_copy(answer, dirChoice, cap);
        return 1;
    case INST_Q_CONFIRM:
        inst_copy(answer, "Y", cap);
        return 1;
    case INST_Q_REMOVE:
        inst_copy(answer, "REMOVE", cap);
        return 1;
    case INST_Q_DEVMGR:
        inst_copy(answer, "N", cap);
        return 1;
    case INST_Q_GATE:
        if (SendMessageA(sheet, WM_APP_GATE, 0, 0) == 1) {
            inst_copy(answer, "Y", cap);
        } else {
            inst_copy(answer, "N", cap);
        }
        return 1;
    default:
        return 0;
    }
}

static void wiz_progress(void *ctx, const INST_STEP *s)
{
    char *d;

    (void)ctx;
    if (pageWnd[P_PROGRESS] == NULL) {
        return;
    }
    d = dup(s->Text);
    if (d != NULL && !PostMessageA(pageWnd[P_PROGRESS], WM_APP_STEP,
                                   (WPARAM)s->State, (LPARAM)d)) {
        free(d);
    }
}

static int wiz_waiting(void *ctx, const INST_WAIT *w)
{
    char text[512];
    char *d;

    (void)ctx;
    if (w->Kind != INST_WAIT_POINT && pageWnd[P_PROGRESS] != NULL) {
        if (w->BoundMs != 0) {
            _snprintf(text, sizeof(text) - 1, "%s (%lu of %lu minutes).",
                      w->What != NULL ? w->What : "Waiting",
                      (unsigned long)(w->ElapsedMs / 60000UL),
                      (unsigned long)((w->BoundMs + 59999UL) / 60000UL));
        } else {
            _snprintf(text, sizeof(text) - 1, "%s.",
                      w->What != NULL ? w->What : "Waiting");
        }
        text[sizeof(text) - 1] = '\0';
        d = dup(text);
        if (d != NULL &&
            !PostMessageA(pageWnd[P_PROGRESS], WM_APP_WAIT, 0, (LPARAM)d)) {
            free(d);
        }
    }
    if (w->Kind == INST_WAIT_RECONNECT && cancelAsked) {
        /* Cancel skips the reconnection's wait (record 26 section 6.3);
         * the run goes on. */
        InterlockedExchange(&cancelAsked, 0);
        return 0;
    }
    return cancelAsked ? 0 : 1;
}

/* ---- the worker thread (record 26 section 3.3, W3) ------------------------- */

static void end_run(int code)
{
    if (!ended) {
        exitCode = inst_eng_end(code, &finalLine);
        ended = 1;
    }
}

static void os_version(OSVERSIONINFOA *v);

static unsigned __stdcall job_main(void *arg)
{
    int job = (int)(size_t)arg;
    int r;
    int i;

    if (job == JOB_READ) {
        EnterCriticalSection(&textLock);
        sink = &readText;
        LeaveCriticalSection(&textLock);
        readCode = inst_eng_begin(gArgc, gArgv);
        if (readCode < 0 && !uninstallMode) {
            readCode = inst_eng_read();
        }
        if (readCode < 0 && !uninstallMode) {
            usbChoice = inst_eng_usb_state(&usbOffered, &usbNotHere);
            uasChoice = inst_eng_uas_state();
            uasOwn = uasChoice;
            inst_eng_tb_state(&tbHave, &tbPresent, &tbDefault, &tbInstalled);
            /* Owner, 2026-10-19: unticked unless asked for, a
             * controller found or not (record 26 section 5.3, draft
             * 16); tbDefault is 0. */
            tbChoice = tbHave && (opts->Thunderbolt || tbDefault);
            tbOwn = tbChoice;
            if (!usbChoice) {
                tbChoice = tbHave;
            }
            inst_eng_tb_choose(tbChoice);
            /* Ticked with the Thunderbolt box (owner, 2026-10-19,
             * replacing "never unasked"); /nostor98 unticks it. */
            inst_eng_s98_state(&s98Have, s98Ver, sizeof(s98Ver));
            /* The per-system table (record 26 section 5.0, draft 35). */
            s98Own = !opts->NoStor98 &&
                     (opts->Stor98 || inst_eng_s98_default());
            s98Choice = s98Have && tbChoice && s98Own;
            inst_eng_s98_choose(s98Choice);
            {
                int sh;
                int op;
                inst_eng_vsd_state(&vsdShown, &vsd9x);
                vsdOwn = !opts->NoStor98Vxd;
                vsdChoice = inst_vsd_box(vsd9x, vsdShown, s98Choice,
                                         opts->NoStor98Vxd, &sh, &op);
                inst_eng_vsd_choose(vsdChoice);
            }
            storShown = inst_eng_stor_state(&storOffered, &storChoice,
                                            storNote, sizeof(storNote));
            /* The other USB drivers' boxes: unticked, but by /free,
             * /nonfree and the /wdm...: switches (record 26 sections 6.7.4
             * and 6.7.11), which the read applied to the engine's choices. */
            EnterCriticalSection(&textLock);
            sink = &planText;
            LeaveCriticalSection(&textLock);
            planCode = inst_eng_plan();
            {
                OSVERSIONINFOA v;
                os_version(&v);
                restageSys = v.dwPlatformId == VER_PLATFORM_WIN32_NT &&
                             v.dwMajorVersion >= 6;
            }
            restageHere = inst_eng_restage_state();
            restageChoice = opts->Restage;
            inst_eng_dir_default(dirChoice, sizeof(dirChoice), dirNow,
                                 sizeof(dirNow), &trayHere);
            fastNow = inst_eng_fast_state();
            fastForced = opts->Faster || opts->NoFaster;
            fastChoice = opts->Faster ? 1 : opts->NoFaster ? 0 : fastNow;
            if (opts->To[0] != '\0') {
                inst_copy(dirChoice, opts->To, sizeof(dirChoice));
            }
            rowCount = inst_eng_rows(rows, INST_MAX_VALUES);
            /* /set preselects a value on its page (record 26 section 10);
             * from here on the pages say what is written. A greyed row's
             * is not taken: the Summary never lists what is not written. */
            for (i = 0; i < opts->SetCount; i++) {
                int k;
                int grey = 0;
                for (k = 0; k < rowCount; k++) {
                    if (rows[k].Value == opts->SetIndex[i] && rows[k].Greyed) {
                        grey = 1;
                    }
                }
                if (!grey && opts->SetIndex[i] >= 0 &&
                    opts->SetIndex[i] < INST_MAX_VALUES) {
                    choiceSet[opts->SetIndex[i]] = 1;
                    choiceVal[opts->SetIndex[i]] = opts->SetValue[i];
                }
            }
        }
    } else if (job == JOB_RUN) {
        EnterCriticalSection(&textLock);
        sink = &runText;
        LeaveCriticalSection(&textLock);
        inst_copy(opts->To, dirChoice, sizeof(opts->To));
        opts->NoTray = !trayChoice;
        /* Written only when changed from the choice in force (record 23
         * section 9.5 item 7), or when a switch asked for it. */
        opts->Faster = trayHere && (fastForced || fastChoice != fastNow) &&
                       fastChoice;
        opts->NoFaster = trayHere && (fastForced || fastChoice != fastNow) &&
                         !fastChoice;
        opts->SetCount = 0;
        for (i = 0; i < INST_MAX_VALUES; i++) {
            inst_eng_choose_value(i, choiceSet[i] && choice_live(i),
                                  choiceVal[i]);
        }
        for (i = 0; i < INST_S98_COUNT; i++) {
            inst_eng_s98_choose_value(i, s98Choice && s98WinSet[i],
                                      s98WinVal[i]);
        }
        r = inst_eng_ask();
        if (r < 0) {
            ran = 1;
            r = inst_eng_run();
        }
        runCode = r;
        end_run(r);
    } else if (job == JOB_UNINSTALL) {
        EnterCriticalSection(&textLock);
        sink = &runText;
        LeaveCriticalSection(&textLock);
        ran = 1;
        runCode = inst_eng_uninstall();
        end_run(runCode);
    }
    InterlockedExchange(&busy, 0);
    PostMessageA(sheet, WM_APP_DONE, (WPARAM)job, 0);
    return 0;
}

static int start_job(int job)
{
    HANDLE h;
    unsigned id;

    if (InterlockedExchange(&busy, 1) != 0) {
        return 0;
    }
    h = (HANDLE)_beginthreadex(NULL, 0, job_main, (void *)(size_t)job, 0, &id);
    if (h == NULL) {
        InterlockedExchange(&busy, 0);
        return 0;
    }
    CloseHandle(h);
    return 1;
}

/* ---- small helpers ------------------------------------------------------- */

static void buttons(DWORD flags)
{
    PropSheet_SetWizButtons(sheet, flags);
    /* The keyboard may be all there is (a USB mouse on the controller this
     * updates): Enter presses Next or Finish, not a read-only text box's
     * newline. Posted, so it lands after the sheet's own focus. */
    if (sheet != NULL && (flags & (PSWIZB_NEXT | PSWIZB_FINISH)) != 0) {
        PostMessageA(sheet, WM_APP_FOCUS, 0, 0);
    }
}

static void next_text(const char *text)
{
    HWND b = GetDlgItem(sheet, ID_SHEET_NEXT);

    if (b != NULL) {
        SetWindowTextA(b, text);
    }
}

/* The version as record 23 section 4.3 reads it (RtlGetVersion on NT). */
static void os_version(OSVERSIONINFOA *v)
{
    if (!inst_eng_os_read(v) && v->dwOSVersionInfoSize == 0) {
        memset(v, 0, sizeof(*v));
    }
}

/* Windows 8 or 8.1: the connection manager alone (record 23 section 4.5). */
static int is_cm_only_system(void)
{
    OSVERSIONINFOA v;

    os_version(&v);
    return inst_os_kind(v.dwPlatformId == VER_PLATFORM_WIN32_NT,
                        v.dwMajorVersion, v.dwMinorVersion,
                        v.dwBuildNumber & 0xFFFFUL) == INST_OS_CMONLY;
}

static const char *system_name(char *out, size_t cap)
{
    OSVERSIONINFOA v;
    const char *name;

    os_version(&v);
    name = inst_os_name(v.dwPlatformId == VER_PLATFORM_WIN32_NT,
                        v.dwMajorVersion, v.dwMinorVersion,
                        v.dwBuildNumber & 0xFFFFUL);
    _snprintf(out, cap - 1, "This computer: %s (%lu.%lu.%lu %s), %s.", name,
              (unsigned long)v.dwMajorVersion, (unsigned long)v.dwMinorVersion,
              (unsigned long)(v.dwBuildNumber & 0xFFFFUL), v.szCSDVersion,
              INST_MY_ARCH == INST_ARCH_AMD64 ? "x64" : "32-bit");
    out[cap - 1] = '\0';
    return out;
}

static int is_9x(void)
{
    return (GetVersion() & 0x80000000UL) != 0;
}

/* The readme beside this program: tools\xhciinst\readme.txt in the
 * package (record 26 section 5's Help). */
static void open_help(HWND owner)
{
    char path[WIZ_PATH];
    char *slash;

    if (GetModuleFileNameA(NULL, path, sizeof(path)) == 0) {
        return;
    }
    slash = strrchr(path, '\\');
    if (slash == NULL) {
        return;
    }
    strcpy(slash + 1, "readme.txt");
    if (GetFileAttributesA(path) == 0xFFFFFFFFUL) {
        MessageBoxA(owner, "The readme is in the package's tools\\xhciinst\\ "
                    "directory (readme.txt).", "xHCI98 Setup",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if ((size_t)ShellExecuteA(owner, "open", "notepad.exe", path, NULL,
                              SW_SHOWNORMAL) <= 32) {
        MessageBoxA(owner, path, "xHCI98 Setup: the readme",
                    MB_OK | MB_ICONINFORMATION);
    }
}

static void open_devmgr(HWND owner)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[64];

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    inst_copy(cmd, "control.exe sysdm.cpl,,1", sizeof(cmd));
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        MessageBoxA(owner, "Device Manager could not be opened: Control "
                    "Panel, System, Device Manager.", "xHCI98 Setup",
                    MB_OK | MB_ICONWARNING);
    }
}

/* ---- the welcome pages ----------------------------------------------------- */

static void found_fill(HWND dlg);

static void welcome_texts(HWND dlg)
{
    char title[128];
    char sys[256];
    char logLine[WIZ_PATH + 16];
    /* The connection manager alone: on 8 and 8.1 the install's texts; on a
     * machine with it alone registered the uninstall's (record 26 draft
     * 8). */
    int cm = uninstallMode ? inst_eng_cm_only_machine() :
             (is_cm_only_system() && tbHave);

    _snprintf(title, sizeof(title) - 1, "%s %s",
              uninstallMode ? inst_page_text(cm ? INST_PT_UN_TITLE_CM :
                                             INST_PT_UN_TITLE)
                            : cm ? "xHCI98 Thunderbolt 3 / USB4 connection "
                                   "manager, release"
                                 : "xHCI98 USB 3.x drivers, release",
              uninstallMode ? "" : XHCI_VER_STR);
    title[sizeof(title) - 1] = '\0';
    SetDlgItemTextA(dlg, IDC_TITLE, title);
    SetDlgItemTextA(dlg, IDC_TEXT, inst_page_text(uninstallMode ?
                                                  (cm ? INST_PT_UN_WARN_CM :
                                                   INST_PT_UN_WARN) :
                                                  cm ? INST_PT_WELCOME_CM :
                                                  is_cm_only_system() ?
                                                  INST_PT_WELCOME_TOOLS :
                                                  INST_PT_WELCOME));
    if (uninstallMode) {
        SetDlgItemTextA(dlg, IDC_U_AGREE, inst_page_text(cm ?
                                                         INST_PT_UN_AGREE_CM :
                                                         INST_PT_UN_AGREE));
    }
    SetDlgItemTextA(dlg, IDC_SYSTEM, system_name(sys, sizeof(sys)));
    _snprintf(logLine, sizeof(logLine) - 1, "The log: %s",
              inst_eng_log_path()[0] ? inst_eng_log_path() :
              "(opened when the installer starts)");
    logLine[sizeof(logLine) - 1] = '\0';
    SetDlgItemTextA(dlg, IDC_LOG, logLine);
}

static void welcome_state(HWND dlg)
{
    welcome_texts(dlg);
    if (refusal != NULL) {
        set_edit(dlg, IDC_STATUS, refusal);
        buttons(0);
        return;
    }
    if (busy) {
        SetDlgItemTextA(dlg, IDC_STATUS, inst_page_text(INST_PT_WAIT));
        buttons(0);
        return;
    }
    EnterCriticalSection(&textLock);
    set_edit(dlg, IDC_STATUS, readText.Buf != NULL ? readText.Buf : "");
    LeaveCriticalSection(&textLock);
    if (!uninstallMode) {
        /* What was found, on the Welcome page (record 26 section 5.0): the
         * devices and the plan's text once the reading has ended; a
         * refusal shows its text alone. */
        int list = readDone && readCode < 0 && planCode < 0;
        ShowWindow(GetDlgItem(dlg, IDC_LIST), list ? SW_SHOW : SW_HIDE);
        if (list) {
            found_fill(dlg);
            EnterCriticalSection(&textLock);
            if (planText.Buf != NULL && planText.Buf[0] != '\0') {
                set_edit(dlg, IDC_STATUS, planText.Buf);
            }
            LeaveCriticalSection(&textLock);
        }
    }
    if (readCode >= 0 || planCode >= 0) {
        if (planCode >= 0) {
            EnterCriticalSection(&textLock);
            set_edit(dlg, IDC_STATUS, planText.Buf != NULL ? planText.Buf : "");
            LeaveCriticalSection(&textLock);
        }
        buttons(0);
        return;
    }
    if (uninstallMode) {
        buttons(IsDlgButtonChecked(dlg, IDC_U_AGREE) == BST_CHECKED ?
                PSWIZB_NEXT : 0);
    } else {
        buttons(PSWIZB_NEXT);
    }
}

/* ---- what was found, on the Welcome page --------------------------------- */

static void found_fill(HWND dlg)
{
    HWND lv = GetDlgItem(dlg, IDC_LIST);
    LVCOLUMNA c;
    LVITEMA it;
    INST_FOUND f;
    int i;
    int n = 0;

    if (lv == NULL) {
        return;
    }
    if (ListView_GetItemCount(lv) == 0 &&
        Header_GetItemCount(ListView_GetHeader(lv)) == 0) {
        memset(&c, 0, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.cx = 200;
        c.pszText = "Device";
        ListView_InsertColumn(lv, 0, &c);
        c.cx = 120;
        c.pszText = "Runs now";
        ListView_InsertColumn(lv, 1, &c);
        c.cx = 150;
        c.pszText = "Will be done";
        ListView_InsertColumn(lv, 2, &c);
    }
    ListView_DeleteAllItems(lv);
    for (i = 0; inst_eng_found(i, &f); i++) {
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT;
        it.iItem = n;
        it.pszText = (char *)f.Desc;
        ListView_InsertItem(lv, &it);
        ListView_SetItemText(lv, n, 1, f.Now);
        ListView_SetItemText(lv, n, 2, (char *)f.Action);
        n++;
    }
    if (n == 0) {
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT;
        it.pszText = "No device of this package is present now.";
        ListView_InsertItem(lv, &it);
        ListView_SetItemText(lv, 0, 2, "staged for later");
    }
}

/* The plan made again for the choices as they stand (record 26 section
 * 3.3): on the window's own thread, no job running. */
static void replan(void)
{
    if (busy) {
        return;
    }
    inst_eng_uas_choose(uasChoice);
    inst_eng_usb_choose(usbChoice);
    inst_eng_tb_choose(tbChoice);
    inst_eng_s98_choose(s98Choice);
    inst_eng_vsd_choose(vsdChoice);
    inst_eng_stor_choose(storChoice);
    EnterCriticalSection(&textLock);
    tb_clear(&planText);
    sink = &planText;
    LeaveCriticalSection(&textLock);
    planCode = inst_eng_plan();
    rowCount = inst_eng_rows(rows, INST_MAX_VALUES);
    /* The store trap read again with the components as they stand: the
     * Thunderbolt box ticked may find one (record 23 section 5.7). */
    restageHere = inst_eng_restage_state();
    if (pageWnd[P_COMP] != NULL) {
        EnableWindow(GetDlgItem(pageWnd[P_COMP], IDC_C_RESTAGE), restageHere);
        if (!restageHere) {
            CheckDlgButton(pageWnd[P_COMP], IDC_C_RESTAGE, BST_UNCHECKED);
        }
    }
}

/* ---- the Components page (record 26 section 5.3) ------------------------- */

/* Stor98's box, under the Thunderbolt one: open only while that is
 * ticked; unticking it unticks Stor98 (owner, 2026-10-19). */
static void comp_s98_sync(HWND dlg)
{
    int open = inst_s98_box_open(s98Have, tbHave,
                                 IsDlgButtonChecked(dlg, IDC_C_TB) ==
                                 BST_CHECKED);

    int sh;
    int vopen;

    if (!open) {
        CheckDlgButton(dlg, IDC_C_S98, BST_UNCHECKED);
    }
    EnableWindow(GetDlgItem(dlg, IDC_C_S98), open);
    EnableWindow(GetDlgItem(dlg, IDC_C_S98_T), open);
    /* S98VSD.VXD, Stor98's child (record 30 section 7.1): open only while
     * Stor98 is ticked, on 98 SE and ME; unticked and greyed otherwise. */
    inst_vsd_box(vsd9x, vsdShown, IsDlgButtonChecked(dlg, IDC_C_S98) ==
                 BST_CHECKED, 0, &sh, &vopen);
    if (!vopen) {
        CheckDlgButton(dlg, IDC_C_VSD, BST_UNCHECKED);
    }
    EnableWindow(GetDlgItem(dlg, IDC_C_VSD), vopen);
}

/* Ticked whenever Stor98 is ticked (owner, 2026-10-22), unless the user
 * unticked it while Stor98 was (record 26 section 5.0). */
static void comp_vsd_follow(HWND dlg)
{
    CheckDlgButton(dlg, IDC_C_VSD, IsDlgButtonChecked(dlg, IDC_C_S98) ==
                   BST_CHECKED && vsdOwn ? BST_CHECKED : BST_UNCHECKED);
    comp_s98_sync(dlg);
}

/* Stor98 with its parent: its own state while the Thunderbolt box is
 * ticked, unticked without it (record 26 section 5.0, draft 35). */
static void comp_s98_follow(HWND dlg)
{
    CheckDlgButton(dlg, IDC_C_S98, IsDlgButtonChecked(dlg, IDC_C_TB) ==
                   BST_CHECKED && s98Own ? BST_CHECKED : BST_UNCHECKED);
    comp_vsd_follow(dlg);
}

/* The USB drivers' box and what it locks (record 26 section 5.3, draft
 * 8): unticked, the Thunderbolt box is ticked and disabled, it being all
 * the run installs; ticked again, the Thunderbolt box has its own state.
 * A package without the Thunderbolt support keeps that box greyed, and
 * Stor98's, its child, follows it (owner, 2026-10-19). */
static void comp_usb_state(HWND dlg)
{
    int usb = IsDlgButtonChecked(dlg, IDC_C_DRIVERS) == BST_CHECKED;

    /* The UAS driver's box, the USB drivers' child: open only while they
     * are ticked, unticked and greyed with them, its own state back when
     * they are ticked again (owner, 2026-10-19). */
    CheckDlgButton(dlg, IDC_C_UAS, (usb && uasOwn) ? BST_CHECKED :
                                                    BST_UNCHECKED);
    EnableWindow(GetDlgItem(dlg, IDC_C_UAS), usb);
    EnableWindow(GetDlgItem(dlg, IDC_C_UAS_T), usb);

    SetDlgItemTextA(dlg, IDC_C_DRIVERS_T, inst_page_text(
                    usbNotHere ? INST_PT_DRIVERS_T8 :
                    usb ? INST_PT_DRIVERS_T : INST_PT_DRIVERS_OFF));
    if (!usb && tbHave) {
        /* Locked on: Stor98 follows its parent here too (owner,
         * 2026-10-19), as on a click of the Thunderbolt box. */
        CheckDlgButton(dlg, IDC_C_TB, BST_CHECKED);
        CheckDlgButton(dlg, IDC_C_S98, s98Own ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, IDC_C_VSD, s98Own && vsdOwn ? BST_CHECKED :
                                                         BST_UNCHECKED);
    }
    EnableWindow(GetDlgItem(dlg, IDC_C_TB), usb && tbHave);
    comp_s98_sync(dlg);
}

static void comp_usb_click(HWND dlg)
{
    int usb = IsDlgButtonChecked(dlg, IDC_C_DRIVERS) == BST_CHECKED;

    if (!usb) {
        tbOwn = IsDlgButtonChecked(dlg, IDC_C_TB) == BST_CHECKED;
    } else {
        CheckDlgButton(dlg, IDC_C_TB, tbOwn ? BST_CHECKED : BST_UNCHECKED);
    }
    comp_usb_state(dlg);
}

/* A control moved down by dy pixels (up when negative). */
static void comp_move(HWND dlg, int id, int dy)
{
    HWND h = GetDlgItem(dlg, id);
    RECT r;
    POINT pt;

    GetWindowRect(h, &r);
    pt.x = r.left;
    pt.y = r.top;
    ScreenToClient(dlg, &pt);
    SetWindowPos(h, NULL, pt.x, pt.y + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/* The page as 98 SE shows it (record 26 section 5.3, draft 17): the USB
 * storage drivers' row under the UAS box, in the place the locked USB
 * drivers' sentence leaves, and the rows below it moved down; on every
 * other system the row is hidden and the template's layout stands. The
 * "Replace the stored build" box on Vista and later (never with the
 * storage row), open only where the store trap was found: elsewhere the
 * folder's line and the buttons take its place (record 26 section 5.0,
 * draft 35). */
static void comp_layout(HWND dlg)
{
    static const int below[] = {
        IDC_C_TB, IDC_C_TB_T, IDC_C_S98, IDC_C_S98_T, IDC_C_VSD, IDC_C_TRAY,
        IDC_C_FAST, IDC_C_RESTAGE, IDC_C_DIRLINE, IDC_C_SETTINGS,
        IDC_C_FOLDER
    };
    static const int last[] = { IDC_C_DIRLINE, IDC_C_SETTINGS, IDC_C_FOLDER };
    RECT r;
    HWND h;
    POINT pt;
    int up;
    int down;
    size_t i;

    if (compLaidOut) {
        return;
    }
    compLaidOut = 1;
    if (!restageSys) {
        r.left = 0;
        r.top = 0;
        r.right = 0;
        r.bottom = 14;
        MapDialogRect(dlg, &r);
        ShowWindow(GetDlgItem(dlg, IDC_C_RESTAGE), SW_HIDE);
        for (i = 0; i < sizeof(last) / sizeof(last[0]); i++) {
            comp_move(dlg, last[i], -r.bottom);
        }
    }
    if (!storShown) {
        ShowWindow(GetDlgItem(dlg, IDC_C_STOR), SW_HIDE);
        ShowWindow(GetDlgItem(dlg, IDC_C_STOR_T), SW_HIDE);
        return;
    }
    /* Both are vertical distances, in the dialog's vertical units. */
    r.left = 0;
    r.top = 0;
    r.right = 0;
    r.bottom = 17;
    MapDialogRect(dlg, &r);
    up = r.bottom;
    r.left = 0;
    r.top = 0;
    r.right = 0;
    r.bottom = 15;
    MapDialogRect(dlg, &r);
    down = r.bottom;
    ShowWindow(GetDlgItem(dlg, IDC_C_DRIVERS_T), SW_HIDE);
    h = GetDlgItem(dlg, IDC_C_UAS);
    GetWindowRect(h, &r);
    pt.x = r.left;
    pt.y = r.top;
    ScreenToClient(dlg, &pt);
    SetWindowPos(h, NULL, pt.x, pt.y - up, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    h = GetDlgItem(dlg, IDC_C_UAS_T);
    GetWindowRect(h, &r);
    pt.x = r.left;
    pt.y = r.top;
    ScreenToClient(dlg, &pt);
    SetWindowPos(h, NULL, pt.x, pt.y - up, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    for (i = 0; i < sizeof(below) / sizeof(below[0]); i++) {
        comp_move(dlg, below[i], down);
    }
}

/* The folder chosen, as the page's one line. */
static void comp_dirline(HWND dlg)
{
    char text[WIZ_PATH + 32];

    _snprintf(text, sizeof(text) - 1, "Tools folder: %s", dirChoice);
    text[sizeof(text) - 1] = '\0';
    SetDlgItemTextA(dlg, IDC_C_DIRLINE, text);
}

static void comp_fill(HWND dlg)
{
    char text[1200];

    comp_layout(dlg);
    if (storShown) {
        SetDlgItemTextA(dlg, IDC_C_STOR, inst_page_text(INST_PT_STOR));
        _snprintf(text, sizeof(text) - 1, "%s\r\n%s",
                  inst_page_text(INST_PT_STOR_T), storNote);
        text[sizeof(text) - 1] = '\0';
        SetDlgItemTextA(dlg, IDC_C_STOR_T, text);
        CheckDlgButton(dlg, IDC_C_STOR, storOffered && storChoice ?
                       BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(dlg, IDC_C_STOR), storOffered);
    }
    SetDlgItemTextA(dlg, IDC_C_DRIVERS, inst_page_text(INST_PT_DRIVERS));
    CheckDlgButton(dlg, IDC_C_DRIVERS, usbChoice ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(dlg, IDC_C_DRIVERS), usbOffered);
    SetDlgItemTextA(dlg, IDC_C_UAS, inst_page_text(INST_PT_UAS));
    SetDlgItemTextA(dlg, IDC_C_UAS_T, inst_page_text(INST_PT_UAS_T));
    SetDlgItemTextA(dlg, IDC_C_TB, inst_page_text(INST_PT_TB));
    if (tbPresent) {
        char found[300];
        inst_tb_found_text(found, sizeof(found), inst_eng_tb_name());
        SetDlgItemTextA(dlg, IDC_C_TB_T, found);
    } else {
        SetDlgItemTextA(dlg, IDC_C_TB_T, inst_page_text(INST_PT_TB_ABSENT));
    }
    CheckDlgButton(dlg, IDC_C_TB, tbChoice ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(dlg, IDC_C_TB_T), tbHave);
    comp_usb_state(dlg);
    SetDlgItemTextA(dlg, IDC_C_S98, inst_page_text(INST_PT_S98));
    SetDlgItemTextA(dlg, IDC_C_S98_T, inst_page_text(INST_PT_S98_T));
    CheckDlgButton(dlg, IDC_C_S98, s98Choice ? BST_CHECKED : BST_UNCHECKED);
    ShowWindow(GetDlgItem(dlg, IDC_C_S98), s98Have ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg, IDC_C_S98_T), s98Have ? SW_SHOW : SW_HIDE);
    SetDlgItemTextA(dlg, IDC_C_VSD, inst_page_text(vsd9x ? INST_PT_VSD :
                                                   INST_PT_VSD_OTHER));
    CheckDlgButton(dlg, IDC_C_VSD, vsdChoice ? BST_CHECKED : BST_UNCHECKED);
    /* On 98 SE and ME where the package carries the file; greyed on every
     * other system whatever the package carries (record 30 section 7.1). */
    ShowWindow(GetDlgItem(dlg, IDC_C_VSD),
               s98Have && (vsdShown || !vsd9x) ? SW_SHOW : SW_HIDE);
    comp_s98_sync(dlg);
    SetDlgItemTextA(dlg, IDC_C_TRAY, inst_page_text(INST_PT_TRAY));
    SetDlgItemTextA(dlg, IDC_C_FAST, inst_page_text(INST_PT_FAST));
    CheckDlgButton(dlg, IDC_C_TRAY, trayChoice ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dlg, IDC_C_FAST, fastChoice ? BST_CHECKED : BST_UNCHECKED);
    ShowWindow(GetDlgItem(dlg, IDC_C_TRAY), trayHere ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg, IDC_C_FAST), trayHere ? SW_SHOW : SW_HIDE);
    CheckDlgButton(dlg, IDC_C_RESTAGE, restageHere && restageChoice ?
                   BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(dlg, IDC_C_RESTAGE), restageHere);
    comp_dirline(dlg);
}

static int CALLBACK browse_cb(HWND h, UINT msg, LPARAM lp, LPARAM data)
{
    (void)lp;
    if (msg == BFFM_INITIALIZED) {
        SendMessageA(h, BFFM_SETSELECTIONA, TRUE, data);
    }
    return 0;
}

static void comp_browse(HWND dlg)
{
    BROWSEINFOA bi;
    char path[MAX_PATH];
    LPITEMIDLIST pidl;
    LPMALLOC m;

    GetDlgItemTextA(dlg, IDC_C_DIR, path, sizeof(path));
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = dlg;
    bi.lpszTitle = "The folder for the xHCI98 tools:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS;
    bi.lpfn = browse_cb;
    bi.lParam = (LPARAM)path;
    pidl = SHBrowseForFolderA(&bi);
    if (pidl == NULL) {
        return;
    }
    if (SHGetPathFromIDListA(pidl, path)) {
        SetDlgItemTextA(dlg, IDC_C_DIR, path);
    }
    if (SHGetMalloc(&m) == NOERROR) {
        m->lpVtbl->Free(m, pidl);
        m->lpVtbl->Release(m);
    }
}

/* The Change folder dialog (record 26 section 5.0): the folder's edit
 * box, Browse, the 98 SE and ME line; OK stays until the folder passes
 * the checks of section 5.3. */
static WIZ_DLGRET CALLBACK folder_proc(HWND dlg, UINT msg, WPARAM wp,
                                       LPARAM lp)
{
    char typed[WIZ_PATH];
    char checked[WIZ_PATH];
    char text[WIZ_PATH];
    const char *why;

    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextA(dlg, IDC_C_DIR_L, inst_page_text(INST_PT_DIR));
        SetDlgItemTextA(dlg, IDC_C_DIR, dirChoice);
        SetDlgItemTextA(dlg, IDC_C_DIR_T, is_9x() ?
                        inst_page_text(INST_PT_DIR_9X) : "");
        ShowWindow(GetDlgItem(dlg, IDC_C_DIR_WHY), SW_HIDE);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_C_BROWSE:
            comp_browse(dlg);
            return TRUE;
        case IDOK:
            GetDlgItemTextA(dlg, IDC_C_DIR, typed, sizeof(typed));
            why = inst_eng_dir_check(typed, checked, sizeof(checked));
            if (why != NULL) {
                _snprintf(text, sizeof(text) - 1, "Not that folder: %s.", why);
                text[sizeof(text) - 1] = '\0';
                SetDlgItemTextA(dlg, IDC_C_DIR_WHY, text);
                ShowWindow(GetDlgItem(dlg, IDC_C_DIR_T), SW_HIDE);
                ShowWindow(GetDlgItem(dlg, IDC_C_DIR_WHY), SW_SHOW);
                return TRUE;
            }
            inst_copy(dirChoice, checked, sizeof(dirChoice));
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

/* The page's boxes handed to the engine, the plan made again when they
 * changed (Next, and the Settings dialog's opening). */
static void comp_take(HWND dlg)
{
    int tb;
    int s98;
    int usb;
    int uas;
    int stor;
    int vsd;
    int rs;

    trayChoice = IsDlgButtonChecked(dlg, IDC_C_TRAY) == BST_CHECKED;
    fastChoice = IsDlgButtonChecked(dlg, IDC_C_FAST) == BST_CHECKED;
    usb = usbOffered ? IsDlgButtonChecked(dlg, IDC_C_DRIVERS) == BST_CHECKED
                     : usbChoice;
    tb = tbHave && (!usb || IsDlgButtonChecked(dlg, IDC_C_TB) == BST_CHECKED);
    if (usb) {
        tbOwn = tb;
    }
    s98 = inst_s98_box_open(s98Have, tbHave, tb) &&
          IsDlgButtonChecked(dlg, IDC_C_S98) == BST_CHECKED;
    {
        int sh;
        int vopen;
        inst_vsd_box(vsd9x, vsdShown, s98, 0, &sh, &vopen);
        vsd = vopen && IsDlgButtonChecked(dlg, IDC_C_VSD) == BST_CHECKED;
    }
    uas = inst_uas_decide(usb, IsDlgButtonChecked(dlg, IDC_C_UAS) !=
                               BST_CHECKED);
    if (usb) {
        uasOwn = uas;
    }
    stor = storShown && storOffered &&
           IsDlgButtonChecked(dlg, IDC_C_STOR) == BST_CHECKED;
    rs = restageHere && IsDlgButtonChecked(dlg, IDC_C_RESTAGE) == BST_CHECKED;
    if (usb != usbChoice || tb != tbChoice || s98 != s98Choice ||
        uas != uasChoice || stor != storChoice || vsd != vsdChoice ||
        rs != restageChoice) {
        usbChoice = usb;
        uasChoice = uas;
        tbChoice = tb;
        s98Choice = s98;
        storChoice = stor;
        vsdChoice = vsd;
        restageChoice = rs;
        inst_eng_restage_choose(rs);
        replan();
    }
}

/* Next on the Components page: the folder checked again, the warnings of
 * record 26 section 5.3.1 (task 43.43) once, Cancel staying on the page,
 * then the choices taken. 1 go on. */
static int comp_leave(HWND dlg)
{
    char checked[WIZ_PATH];
    char warn[2048];
    char box[2200];
    const char *why;
    int usb;
    int tb;
    int s98;
    int stor;

    why = inst_eng_dir_check(dirChoice, checked, sizeof(checked));
    if (why != NULL) {
        _snprintf(box, sizeof(box) - 1, "Not that folder: %s. Change "
                  "folder... chooses another.", why);
        box[sizeof(box) - 1] = '\0';
        MessageBoxA(GetParent(dlg), box, "xHCI98 Setup", MB_OK |
                    MB_ICONWARNING);
        return 0;
    }
    inst_copy(dirChoice, checked, sizeof(dirChoice));
    usb = usbOffered ? IsDlgButtonChecked(dlg, IDC_C_DRIVERS) == BST_CHECKED
                     : usbChoice;
    tb = tbHave && (!usb || IsDlgButtonChecked(dlg, IDC_C_TB) == BST_CHECKED);
    s98 = inst_s98_box_open(s98Have, tbHave, tb) &&
          IsDlgButtonChecked(dlg, IDC_C_S98) == BST_CHECKED;
    stor = storShown && storOffered &&
           IsDlgButtonChecked(dlg, IDC_C_STOR) == BST_CHECKED;
    if (inst_eng_comp_warn(usb, stor, s98, checked, warn,
                           sizeof(warn)) != 0) {
        _snprintf(box, sizeof(box) - 1, "%s\n\nOK goes on; Cancel stays "
                  "on this page to change the choice.", warn);
        box[sizeof(box) - 1] = '\0';
        if (MessageBoxA(GetParent(dlg), box, "xHCI98 Setup",
                        MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
            return 0;
        }
    }
    comp_take(dlg);
    return 1;
}

/* ---- the other USB drivers' page (record 26 sections 6.7.4, 6.7.11) ------ */

static int dlu_x(HWND dlg, int x);
static HWND make_px(HWND dlg, const char *cls, const char *text, DWORD style,
                    int x, int y, int w, int h, int id, HFONT font);
static int text_h(HDC dc, const char *text, int w, int *wide);

static int oth_id(int set, int what)
{
    return IDC_O_BASE + IDC_O_STRIDE * set + what;
}

/* The packages' list: a child dialog in IDC_O_AREA's place, with a heading
 * per set and class and one plain check box per package for this system,
 * scrolled by its own bar (not a list view: section 6.7.11). */
static HWND othList;
static int othListH;            /* the contents' height, pixels             */
static int othListPos;          /* the scroll position, pixels              */
static int othBoxH;             /* one line's height                        */

static int othlist_id(int set, int i)
{
    return IDC_OL_BASE + 100 * set + i;
}

static void othlist_detail(int set, int i)
{
    INST_OTHPKG_VIEW v;
    char text[1100];
    HWND page = othList != NULL ? GetParent(othList) : NULL;

    if (page == NULL || !inst_eng_other_pkg(set, i, &v)) {
        return;
    }
    {
        char scope[600];
        inst_eng_other_pkg_scope(set, i, scope, sizeof(scope));
        _snprintf(text, sizeof(text) - 1, "%s:%s. %s%s%s%s",
                  set == INST_SET_FREE ? "free" : "nonfree", v.Id,
                  v.Blocked ? "Not available: " : "", v.Blocked ? v.Why : "",
                  v.Blocked ? " " : "", scope);
        if (v.Earlier) {
            strncat(text, " Staged by an earlier run.", sizeof(text) -
                    strlen(text) - 1);
        }
    }
    text[sizeof(text) - 1] = '\0';
    SetDlgItemTextA(page, IDC_O_DETAIL, text);
}

/* Each box's tick, label and enabled state from the engine. */
static void othlist_sync(void)
{
    int set;
    int i;
    int first = 1;

    if (othList == NULL) {
        return;
    }
    {
        HWND tb = GetDlgItem(othList, othlist_id(INST_SET_FREE, 99));
        int offered = 0;
        int on = inst_eng_other_trust(&offered);
        if (tb != NULL) {
            CheckDlgButton(othList, othlist_id(INST_SET_FREE, 99),
                           on ? BST_CHECKED : BST_UNCHECKED);
            EnableWindow(tb, offered);
        }
    }
    for (set = 0; set < INST_SET_COUNT; set++) {
        int open = IsDlgButtonChecked(GetParent(othList), oth_id(set,
                                      IDC_O_BOX)) == BST_CHECKED;
        INST_OTHPKG_VIEW v;
        for (i = 0; inst_eng_other_pkg(set, i, &v); i++) {
            HWND b = GetDlgItem(othList, othlist_id(set, i));
            char label[INST_PKG_DESC_MAX + 40];
            if (b == NULL) {
                continue;
            }
            _snprintf(label, sizeof(label) - 1, "%s%s", v.Desc, v.Blocked ?
                      " (not available: see below)" : "");
            label[sizeof(label) - 1] = '\0';
            SetWindowTextA(b, label);
            CheckDlgButton(othList, othlist_id(set, i), v.Tick ? BST_CHECKED
                                                               : BST_UNCHECKED);
            EnableWindow(b, open);
            {
                /* The tab stop on the first open box: Tab enters the list
                 * there, the arrows move within it. */
                LONG st = GetWindowLongA(b, GWL_STYLE) &
                          ~(LONG)(WS_TABSTOP | WS_GROUP);
                if (open && first) {
                    st |= WS_TABSTOP | WS_GROUP;
                    first = 0;
                }
                SetWindowLongA(b, GWL_STYLE, st);
            }
        }
    }
}

static void othlist_scroll(int to)
{
    SCROLLINFO si;
    RECT r;
    int page;
    int max;

    GetClientRect(othList, &r);
    page = r.bottom;
    max = othListH > page ? othListH - page : 0;
    if (to < 0) {
        to = 0;
    }
    if (to > max) {
        to = max;
    }
    if (to != othListPos) {
        ScrollWindowEx(othList, 0, othListPos - to, NULL, NULL, NULL, NULL,
                       SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
        othListPos = to;
    }
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = othListH > 0 ? othListH - 1 : 0;
    si.nPage = (UINT)page;
    si.nPos = othListPos;
    SetScrollInfo(othList, SB_VERT, &si, TRUE);
}

/* The focused box kept in view. */
static void othlist_show(HWND box)
{
    RECT r;
    RECT c;
    POINT pt;

    GetWindowRect(box, &r);
    pt.x = r.left;
    pt.y = r.top;
    ScreenToClient(othList, &pt);
    GetClientRect(othList, &c);
    if (pt.y < 0) {
        othlist_scroll(othListPos + pt.y);
    } else if (pt.y + (r.bottom - r.top) > c.bottom) {
        othlist_scroll(othListPos + pt.y + (r.bottom - r.top) - c.bottom);
    }
}

static WIZ_DLGRET CALLBACK othlist_proc(HWND dlg, UINT msg, WPARAM wp,
                                        LPARAM lp)
{
    int id;
    int set;
    int i;

    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        return FALSE;
    case WM_VSCROLL:
        switch (LOWORD(wp)) {
        case SB_LINEUP:
            othlist_scroll(othListPos - othBoxH);
            break;
        case SB_LINEDOWN:
            othlist_scroll(othListPos + othBoxH);
            break;
        case SB_PAGEUP:
            othlist_scroll(othListPos - 3 * othBoxH);
            break;
        case SB_PAGEDOWN:
            othlist_scroll(othListPos + 3 * othBoxH);
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            othlist_scroll((int)(short)HIWORD(wp));
            break;
        default:
            break;
        }
        return TRUE;
    case WM_COMMAND:
        id = LOWORD(wp);
        if (id < IDC_OL_BASE || id >= IDC_OL_BASE + 100 * INST_SET_COUNT) {
            break;
        }
        set = (id - IDC_OL_BASE) / 100;
        i = (id - IDC_OL_BASE) % 100;
        if (i == 99) {
            /* The free set's certificate (record 26 section 6.7.11). */
            if (HIWORD(wp) == BN_CLICKED) {
                inst_eng_other_trust_choose(IsDlgButtonChecked(dlg, id) ==
                                            BST_CHECKED);
                othlist_sync();
            } else if (HIWORD(wp) == BN_SETFOCUS) {
                othlist_show((HWND)lp);
                SetDlgItemTextA(GetParent(dlg), IDC_O_DETAIL, "usb-wdm-free "
                                "signs its 64-bit drivers with its own test "
                                "certificate. Ticked, the run adds it to this "
                                "computer's trusted roots and publishers, and "
                                "the uninstall removes what it added. No trust "
                                "is ever added for the non-free drivers.");
            }
            return TRUE;
        }
        if (HIWORD(wp) == BN_SETFOCUS) {
            othlist_show((HWND)lp);
            othlist_detail(set, i);
        } else if (HIWORD(wp) == BN_CLICKED) {
            int tick = IsDlgButtonChecked(dlg, id) == BST_CHECKED;
            /* A barred box takes no tick: its reason said again. */
            inst_eng_other_pkg_tick(set, i, tick);
            othlist_sync();
            othlist_detail(set, i);
        }
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

/* The list made again from the engine's packages, in IDC_O_AREA's place. */
static void othlist_build(HWND page)
{
    HFONT font = (HFONT)SendMessageA(page, WM_GETFONT, 0, 0);
    RECT area;
    RECT c;
    POINT pt;
    HDC dc;
    HFONT old;
    int y = 0;
    int first = 1;
    int w;
    int set;
    int h;
    int i;

    if (othList != NULL) {
        DestroyWindow(othList);
        othList = NULL;
    }
    GetWindowRect(GetDlgItem(page, IDC_O_AREA), &area);
    pt.x = area.left;
    pt.y = area.top;
    ScreenToClient(page, &pt);
    othList = CreateDialogParamA(hInst, MAKEINTRESOURCEA(IDD_OTHERLIST), page,
                                 othlist_proc, 0);
    if (othList == NULL) {
        return;
    }
    SetWindowPos(othList, GetDlgItem(page, IDC_O_HEAD), pt.x, pt.y,
                 area.right - area.left, area.bottom - area.top,
                 SWP_NOACTIVATE);
    GetClientRect(othList, &c);
    w = c.right - dlu_x(page, 4);
    dc = GetDC(othList);
    old = font != NULL ? (HFONT)SelectObject(dc, font) : NULL;
    othBoxH = text_h(dc, "Xg", w, NULL) + 2;
    for (set = 0; set < INST_SET_COUNT; set++) {
        for (h = 0; h < instOtherHeadingCount; h++) {
            INST_OTHPKG_VIEW v;
            int any = 0;
            for (i = 0; inst_eng_other_pkg(set, i, &v); i++) {
                char head[96];
                char label[INST_PKG_DESC_MAX + 40];
                int lh;
                HWND b;
                if (!v.Eligible || v.Heading != h) {
                    continue;
                }
                if (!any) {
                    _snprintf(head, sizeof(head) - 1, "%s: %s", set ==
                              INST_SET_FREE ? "Free drivers" :
                              "Non-free drivers", instOtherHeadings[h]);
                    head[sizeof(head) - 1] = '\0';
                    make_px(othList, "STATIC", head, SS_LEFT | SS_NOPREFIX,
                            dlu_x(page, 2), y, w, othBoxH, -1, font);
                    y += othBoxH;
                    any = 1;
                }
                _snprintf(label, sizeof(label) - 1, "%s (not available: see "
                          "below)", v.Desc);
                label[sizeof(label) - 1] = '\0';
                lh = text_h(dc, label, w - dlu_x(page, 22), NULL);
                if (lh < othBoxH) {
                    lh = othBoxH;
                }
                b = make_px(othList, "BUTTON", v.Desc, BS_AUTOCHECKBOX |
                            BS_MULTILINE | BS_TOP | BS_NOTIFY |
                            (first ? WS_TABSTOP | WS_GROUP : 0),
                            dlu_x(page, 10), y, w - dlu_x(page, 10), lh + 2,
                            othlist_id(set, i), font);
                (void)b;
                first = 0;
                y += lh + 3;
            }
        }
        if (set == INST_SET_FREE && inst_eng_other_trust_possible()) {
            const char *t = "Trust usb-wdm-free's test certificate on this "
                            "computer (its 64-bit drivers are signed with it; "
                            "they load only in test-signing mode)";
            int th = text_h(dc, t, w - dlu_x(page, 22), NULL);
            if (th < othBoxH) {
                th = othBoxH;
            }
            make_px(othList, "BUTTON", t, BS_AUTOCHECKBOX | BS_MULTILINE |
                    BS_TOP | BS_NOTIFY, dlu_x(page, 10), y,
                    w - dlu_x(page, 10), th + 2, othlist_id(set, 99), font);
            y += th + 3;
        }
        {
            const char *e = inst_eng_other_earlier(set);
            if (e[0] != '\0') {
                int eh = text_h(dc, e, w, NULL) + 2;
                make_px(othList, "STATIC", e, SS_LEFT | SS_NOPREFIX,
                        dlu_x(page, 2), y, w, eh, -1, font);
                y += eh + 2;
            }
        }
    }
    if (y == 0) {
        make_px(othList, "STATIC", "None of the drivers carried has one for "
                "this Windows.", SS_LEFT | SS_NOPREFIX, dlu_x(page, 2), 0, w,
                othBoxH, -1, font);
        y = othBoxH;
    }
    if (old != NULL) {
        SelectObject(dc, old);
    }
    ReleaseDC(othList, dc);
    othListH = y;
    othListPos = 0;
    othlist_scroll(0);
    othlist_sync();
}

/* A set's copy box: its packages open only while it is ticked. */
static void oth_sync(HWND dlg, int set)
{
    inst_eng_other_choose(set, IsDlgButtonChecked(dlg, oth_id(set,
                          IDC_O_BOX)) == BST_CHECKED, 0);
    othlist_sync();
}

static void oth_fill(HWND dlg)
{
    int set;

    for (set = 0; set < INST_SET_COUNT; set++) {
        char sentence[800];
        char line[512];
        int state = INST_OTHER_ABSENT;
        int forThis = 0;
        int inc = 0;
        int stage = 0;
        int shown;
        int w;
        RECT top;
        RECT r;

        inst_eng_other_state(set, &state, &forThis, &inc, &stage);
        inst_eng_other_texts(set, dirChoice[0] ? dirChoice : "the tools folder",
                             sentence, sizeof(sentence), line, sizeof(line));
        shown = state == INST_OTHER_CARRIED;
        SetDlgItemTextA(dlg, oth_id(set, IDC_O_BOX), inst_other_label(set));
        SetDlgItemTextA(dlg, oth_id(set, IDC_O_TEXT), sentence);
        SetDlgItemTextA(dlg, oth_id(set, IDC_O_LINE), line);
        for (w = IDC_O_BOX; w <= IDC_O_TEXT; w++) {
            ShowWindow(GetDlgItem(dlg, oth_id(set, w)), shown ? SW_SHOW
                                                              : SW_HIDE);
        }
        /* Absent or not this release's, the line takes the row's place. */
        r.left = 8;
        r.top = set == INST_SET_FREE ? (shown ? 38 : 2) : (shown ? 92 : 56);
        r.right = 8 + 284;
        r.bottom = r.top + 16;
        top = r;
        MapDialogRect(dlg, &top);
        SetWindowPos(GetDlgItem(dlg, oth_id(set, IDC_O_LINE)), NULL, top.left,
                     top.top, top.right - top.left, top.bottom - top.top,
                     SWP_NOZORDER);
        CheckDlgButton(dlg, oth_id(set, IDC_O_BOX), shown && inc ?
                       BST_CHECKED : BST_UNCHECKED);
    }
    SetDlgItemTextA(dlg, IDC_O_DETAIL, "");
    othlist_build(dlg);
}

/* Next from the page: section 6.7.4's warnings, once per Next; Cancel in
 * the box stays on the page. The choices are the engine's already. */
static int oth_leave(HWND dlg)
{
    char warn[2048];
    char box[2200];

    (void)dlg;
    inst_eng_other_warnings(warn, sizeof(warn));
    if (warn[0] != '\0') {
        _snprintf(box, sizeof(box) - 1, "%s\n\nOK goes on; Cancel stays on "
                  "this page to change the choice.", warn);
        box[sizeof(box) - 1] = '\0';
        if (MessageBoxA(GetParent(dlg), box, "xHCI98 Setup",
                        MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
            return 0;
        }
    }
    replan();
    return 1;
}

/* ---- the settings pages (record 26 sections 5.4 and 5.5) ------------------- */

/* The rows a settings page or the Advanced dialog shows. */
static int rows_for(int page, int advanced, int *idx, int cap)
{
    int i;
    int n = 0;
    int inf = page == P_USB ? INST_INF_XHCI : INST_INF_CM;

    for (i = 0; i < rowCount && n < cap; i++) {
        if (instValues[rows[i].Value].Inf == inf &&
            (advanced ? !rows[i].Asked : rows[i].Asked)) {
            idx[n++] = i;
        }
    }
    return n;
}

/* The row's "now", short (the detail goes to the page's note). */
static void row_now(const INST_ROW *r, char *out, size_t cap)
{
    const INST_VALUE *v = &instValues[r->Value];

    if (r->MwState == 1) {
        _snprintf(out, cap - 1, "now: %lu, set machine-wide",
                  (unsigned long)r->MwValue);
    } else if (r->MwState == -1) {
        _snprintf(out, cap - 1, "now: not known");
    } else if (!r->Same) {
        _snprintf(out, cap - 1, "now: each controller its own");
    } else if (r->NowCount == 0) {
        _snprintf(out, cap - 1, "now: not set (default %lu)",
                  (unsigned long)r->Def);
    } else {
        _snprintf(out, cap - 1, "now: %lu%s", (unsigned long)r->InForce,
                  r->InForce == r->Def ? " (the default)" : "");
    }
    out[cap - 1] = '\0';
    if (v->Hi != 0 && strlen(out) + 24 < cap) {
        char range[48];
        _snprintf(range, sizeof(range) - 1, "; %lu to %lu",
                  (unsigned long)v->Lo, (unsigned long)v->Hi);
        range[sizeof(range) - 1] = '\0';
        strcat(out, range);
    }
}

/* What a row is about, whole: its meaning, what each location holds, the
 * choices, the default and the caution (record 26 sections 5.4 and 5.5),
 * for a scrolling note. */
static void row_note(const INST_ROW *r, char *out, size_t cap)
{
    const INST_VALUE *v = &instValues[r->Value];
    char line[512];
    INST_U32 b;
    int i;

    _snprintf(out, cap - 1, "%s - %s.", v->Name, v->Meaning);
    out[cap - 1] = '\0';
    if (r->Greyed) {
        _snprintf(line, sizeof(line) - 1, " %s: not used on this system, "
                  "so it is greyed and nothing is written.",
                  inst_value_only(r->Value));
        line[sizeof(line) - 1] = '\0';
        strncat(out, line, cap - strlen(out) - 1);
    }
    if (r->MwState == 1) {
        _snprintf(line, sizeof(line) - 1, " Set machine-wide: %lu, which "
                  "every controller uses.", (unsigned long)r->MwValue);
        line[sizeof(line) - 1] = '\0';
        strncat(out, line, cap - strlen(out) - 1);
        for (i = 0; i < r->NowCount; i++) {
            if (r->Now[i].State > 0 && r->Now[i].Value != r->MwValue) {
                _snprintf(line, sizeof(line) - 1, " %s's own value: %lu (not "
                          "used).", r->Now[i].Desc,
                          (unsigned long)r->Now[i].Value);
                line[sizeof(line) - 1] = '\0';
                strncat(out, line, cap - strlen(out) - 1);
            }
        }
    } else if (r->MwState == -1) {
        strncat(out, " The machine-wide value could not be read as a "
                "number, and it is what every controller uses: the value in "
                "force is not known. Leave it, or choose a value to write "
                "over it.", cap - strlen(out) - 1);
    } else if (!r->Same) {
        strncat(out, " Each controller keeps its own value until you choose "
                "one:", cap - strlen(out) - 1);
        for (i = 0; i < r->NowCount; i++) {
            _snprintf(line, sizeof(line) - 1, " %s: %lu.", r->Now[i].Desc,
                      (unsigned long)(r->Now[i].State > 0 ? r->Now[i].Value
                                                          : r->Def));
            line[sizeof(line) - 1] = '\0';
            strncat(out, line, cap - strlen(out) - 1);
        }
    }
    if (v->Hi != 0) {
        _snprintf(line, sizeof(line) - 1, " Allowed: %lu to %lu.",
                  (unsigned long)v->Lo, (unsigned long)v->Hi);
        line[sizeof(line) - 1] = '\0';
        strncat(out, line, cap - strlen(out) - 1);
    } else {
        for (b = 0; b < 32UL; b++) {
            if ((r->Mask & (1UL << b)) == 0) {
                continue;
            }
            _snprintf(line, sizeof(line) - 1, " %lu: %s.", (unsigned long)b,
                      (b < INST_CHOICES && v->Choice[b] != NULL) ? v->Choice[b] :
                      "allowed");
            line[sizeof(line) - 1] = '\0';
            strncat(out, line, cap - strlen(out) - 1);
        }
    }
    _snprintf(line, sizeof(line) - 1, " Default: %lu.", (unsigned long)r->Def);
    line[sizeof(line) - 1] = '\0';
    strncat(out, line, cap - strlen(out) - 1);
    if (v->Caution != NULL) {
        char c[1024];
        inst_caution_text(r->Value, v->Hi != 0 ? 0xFFFFFFFFUL : r->Mask, c,
                          sizeof(c));
        strncat(out, " NOTE: ", cap - strlen(out) - 1);
        strncat(out, c, cap - strlen(out) - 1);
    }
    if (r->CmNew && (inst_eq(v->Name, "XhciCmMode") ||
                     inst_eq(v->Name, "XhciCmApprove"))) {
        strncat(out, " A new install starts with the default; a change "
                "applies from the next restart: unplug the dock until then "
                "if you choose 0.", cap - strlen(out) - 1);
    }
}

/* The choice a row starts with: the page's own (a /set preselection
 * included), else the value in force; -1 the mixed state. */
static INST_U32 row_start(const INST_ROW *r, int *mixed)
{
    *mixed = 0;
    if (choiceSet[r->Value]) {
        return choiceVal[r->Value];
    }
    if (!r->Same && r->MwState != 1) {
        *mixed = 1;
        return 0;
    }
    return r->InForce;
}

/* A combo box filled with a row's choices: the mixed state its own item
 * where the controllers differ, every allowed value with its meaning, the
 * default marked; the start selected. */
static void row_fill_combo(HWND combo, const INST_ROW *r)
{
    const INST_VALUE *v = &instValues[r->Value];
    char text[256];
    INST_U32 b;
    INST_U32 cur;
    int mixed;
    int sel = -1;
    int item;

    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    cur = row_start(r, &mixed);
    if (!r->Same && r->MwState != 1) {
        item = (int)SendMessageA(combo, CB_ADDSTRING, 0,
                                 (LPARAM)(r->MwState == -1
                                              ? "leave it as it is"
                                              : "each controller keeps its own"));
        SendMessageA(combo, CB_SETITEMDATA, item, (LPARAM)0xFFFFFFFFUL);
        if (mixed) {
            sel = item;
        }
    }
    for (b = 0; b < 32UL; b++) {
        if ((r->Mask & (1UL << b)) == 0) {
            continue;
        }
        _snprintf(text, sizeof(text) - 1, "%lu  %s%s", (unsigned long)b,
                  (b < INST_CHOICES && v->Choice[b] != NULL) ? v->Choice[b] : "",
                  b == r->Def ? " (default)" : "");
        text[sizeof(text) - 1] = '\0';
        item = (int)SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)text);
        SendMessageA(combo, CB_SETITEMDATA, item, (LPARAM)b);
        if (sel < 0 && !mixed && b == cur) {
            sel = item;
        }
    }
    if (sel < 0 && !mixed) {
        /* A value in force the list does not offer stays what it is. */
        _snprintf(text, sizeof(text) - 1, "%lu  (set now)",
                  (unsigned long)cur);
        text[sizeof(text) - 1] = '\0';
        sel = (int)SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)text);
        SendMessageA(combo, CB_SETITEMDATA, sel, (LPARAM)cur);
    }
    SendMessageA(combo, CB_SETCURSEL, sel < 0 ? 0 : sel, 0);
}

/* What a choice writes (record 26 section 5.4): 1 with *v when it differs
 * from the value in force, or replaces the controllers' mixed values; 0
 * when it leaves things as they are. Stateless: a page made again reads
 * the same. */
static int choice_writes(const INST_ROW *r, int mixedPicked, INST_U32 v)
{
    if (mixedPicked) {
        return 0;
    }
    if (!r->Same && r->MwState != 1) {
        return 1;
    }
    return v != r->InForce;
}

static int combo_read(HWND combo, const INST_ROW *r, INST_U32 *v)
{
    int sel = (int)SendMessageA(combo, CB_GETCURSEL, 0, 0);
    INST_U32 data;

    if (sel < 0) {
        return 0;
    }
    data = (INST_U32)(size_t)SendMessageA(combo, CB_GETITEMDATA, sel, 0);
    *v = data;
    return choice_writes(r, data == 0xFFFFFFFFUL, data);
}

typedef struct _ROWCTL {
    int Row;                /* index into rows[]; Stor98's: instS98Values */
    HWND Label;
    HWND Choice;
    HWND Now;
    HWND Sep;               /* the line above the row; NULL on the first  */
} ROWCTL;

/* A settings page's rows as shown: the part made now, of how many
 * (record 26 section 5.4, "The layout"). */
typedef struct _ROWPAGE {
    int Count;
    int Part;
    int Parts;
    ROWCTL R[WIZ_ROWS];
} ROWPAGE;

static ROWPAGE pageRows[3];     /* P_USB, P_TBS, P_S98 */
static int navBack;             /* the last move between pages was Back */

static ROWPAGE *page_rows(int page)
{
    return &pageRows[page == P_USB ? 0 : page == P_TBS ? 1 : 2];
}

/* DLU to pixels for a dialog. */
static int dlu_x(HWND dlg, int x)
{
    RECT r;

    r.left = 0;
    r.top = 0;
    r.right = x;
    r.bottom = 0;
    MapDialogRect(dlg, &r);
    return r.right;
}

static int dlu_y(HWND dlg, int y)
{
    RECT r;

    r.left = 0;
    r.top = 0;
    r.right = 0;
    r.bottom = y;
    MapDialogRect(dlg, &r);
    return r.bottom;
}

/* A control at a place in pixels. */
static HWND make_px(HWND dlg, const char *cls, const char *text, DWORD style,
                    int x, int y, int w, int h, int id, HFONT font)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, dlg, (HMENU)(size_t)id, hInst, NULL);

    if (c != NULL && font != NULL) {
        SendMessageA(c, WM_SETFONT, (WPARAM)font, FALSE);
    }
    return c;
}

/* ... and at a place in dialog units. */
static HWND make_ctl(HWND dlg, const char *cls, const char *text, DWORD style,
                     int x, int y, int w, int h, int id, HFONT font)
{
    return make_px(dlg, cls, text, style, dlu_x(dlg, x), dlu_y(dlg, y),
                   dlu_x(dlg, w), dlu_y(dlg, h), id, font);
}

/* The label of a row: the name and the one sentence of meaning. */
static void row_label(const INST_ROW *r, char *out, size_t cap)
{
    _snprintf(out, cap - 1, "%s - %s", instValues[r->Value].Name,
              instValues[r->Value].Meaning);
    out[cap - 1] = '\0';
}

/* ---- the settings pages' layout (record 26 section 5.4, "The layout") ---- */

/* A row as the layout sees it: its two texts and its kind of choice. */
typedef struct _LAYROW {
    char Label[256];
    char Now[160];
    int Range;              /* a number box, not a drop-down              */
} LAYROW;

/* A page's geometry in pixels, from its own size and font. */
typedef struct _LAYGEO {
    int X;                  /* the column                                 */
    int W;
    int HeadH;
    int RowsTop;
    int NowW;               /* the "now" column, 0 none                   */
    int LabelW;
    int Gap;                /* a label to its choice                      */
    int Sep;                /* one row to the next, the line in it        */
    int ComboH;             /* a drop-down's closed box                   */
    int EditH;
    int EditW;
    int TopH[WIZ_ROWS];     /* the label's or the "now" text's, the taller */
    int LabelH[WIZ_ROWS];
    int NowH[WIZ_ROWS];
    int RowH[WIZ_ROWS];
    int Start[WIZ_ROWS + 1];
    int Parts;
    int NoteTop;
    int NoteBottom;
} LAYGEO;

/* Text in the font selected into dc: its height wrapped at w, or (w 0) on
 * one line with its width in *wide. */
static int text_h(HDC dc, const char *text, int w, int *wide)
{
    RECT r;

    if (wide != NULL) {
        *wide = 0;
    }
    if (text[0] == '\0') {
        return 0;
    }
    r.left = 0;
    r.top = 0;
    r.right = w > 0 ? w : 32767;
    r.bottom = 0;
    DrawTextA(dc, text, -1, &r, DT_CALCRECT | DT_NOPREFIX |
              (w > 0 ? DT_WORDBREAK : DT_SINGLELINE));
    if (wide != NULL) {
        *wide = r.right - r.left;
    }
    return r.bottom - r.top;
}

/* A drop-down's closed height in a font: one made, read and destroyed. */
static int combo_closed_h(HWND dlg, HFONT font)
{
    RECT r;
    HWND c = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | CBS_DROPDOWNLIST |
                             WS_VSCROLL, 0, 0, 100, 200, dlg, (HMENU)0, hInst,
                             NULL);
    int h = dlu_y(dlg, 12);

    if (c != NULL) {
        if (font != NULL) {
            SendMessageA(c, WM_SETFONT, (WPARAM)font, FALSE);
        }
        GetWindowRect(c, &r);
        h = r.bottom - r.top;
        DestroyWindow(c);
    }
    return h;
}

/* The page's head, with the part words where it has parts. */
static void head_text(const char *head, int part, int parts, char *out,
                      size_t cap)
{
    if (parts > 1) {
        _snprintf(out, cap - 1, "%s (page %d of %d)", head, part + 1, parts);
    } else {
        _snprintf(out, cap - 1, "%s", head);
    }
    out[cap - 1] = '\0';
}

/* The note's height for a number of lines of its font: the lines, its
 * frame and the edit control's own margins. */
static int note_lines_h(HWND dlg, int lines)
{
    HWND note = GetDlgItem(dlg, IDC_S_NOTE);
    HFONT f = (HFONT)SendMessageA(note, WM_GETFONT, 0, 0);
    HDC dc = GetDC(note);
    HFONT old = f != NULL ? (HFONT)SelectObject(dc, f) : NULL;
    TEXTMETRICA tm;
    RECT win;
    RECT cli;

    GetTextMetricsA(dc, &tm);
    if (old != NULL) {
        SelectObject(dc, old);
    }
    ReleaseDC(note, dc);
    GetWindowRect(note, &win);
    GetClientRect(note, &cli);
    return lines * tm.tmHeight + (win.bottom - win.top) - cli.bottom +
           dlu_y(dlg, 2);
}

static void lay_measure(HWND dlg, HFONT font, const LAYROW *lr, int n,
                        const char *head, LAYGEO *g)
{
    RECT page;
    HDC dc;
    HFONT old;
    char text[600];
    int widest = 0;
    int tallest = 0;
    int least;
    int i;
    int w;

    memset(g, 0, sizeof(*g));
    GetClientRect(dlg, &page);
    g->X = dlu_x(dlg, 8);
    g->W = page.right - 2 * g->X;
    g->Gap = dlu_y(dlg, 2);
    g->Sep = dlu_y(dlg, 9);
    g->EditH = dlu_y(dlg, 12);
    g->EditW = dlu_x(dlg, 60);
    g->ComboH = combo_closed_h(dlg, font);
    dc = GetDC(dlg);
    old = font != NULL ? (HFONT)SelectObject(dc, font) : NULL;
    /* The widest part words a page of n rows can have, so the head does
     * not grow once the split is decided. */
    head_text(head, n - 1, n, text, sizeof(text));
    g->HeadH = text_h(dc, text, g->W, NULL);
    g->RowsTop = dlu_y(dlg, 2) + g->HeadH + dlu_y(dlg, 4);
    for (i = 0; i < n; i++) {
        text_h(dc, lr[i].Now, 0, &w);
        if (w > widest) {
            widest = w;
        }
    }
    g->NowW = wiz_lay_now_col(widest, g->W);
    g->LabelW = g->NowW > 0 ? g->W - g->NowW - dlu_x(dlg, 6) : g->W;
    for (i = 0; i < n; i++) {
        g->LabelH[i] = text_h(dc, lr[i].Label, g->LabelW, NULL);
        g->NowH[i] = g->NowW > 0 ? text_h(dc, lr[i].Now, g->NowW, NULL) : 0;
        g->TopH[i] = g->LabelH[i] > g->NowH[i] ? g->LabelH[i] : g->NowH[i];
        g->RowH[i] = g->TopH[i] + g->Gap + (lr[i].Range ? g->EditH
                                                         : g->ComboH);
    }
    if (old != NULL) {
        SelectObject(dc, old);
    }
    ReleaseDC(dlg, dc);
    g->NoteBottom = page.bottom - dlu_y(dlg, 4);
    /* Six lines at least where the rows allow (a part of two or more
     * rows always does); a lone tall row takes from it down to two. */
    least = note_lines_h(dlg, 6);
    g->Parts = wiz_lay_split(g->RowH, n, g->Sep, g->NoteBottom - least -
                             dlu_y(dlg, 6) - g->RowsTop, WIZ_LAY_PER_PART,
                             g->Start, WIZ_ROWS + 1);
    for (i = 0; i < g->Parts; i++) {
        w = wiz_lay_part_h(g->RowH, g->Start[i], g->Start[i + 1], g->Sep);
        if (w > tallest) {
            tallest = w;
        }
    }
    g->NoteTop = wiz_lay_note_top(g->RowsTop, tallest, dlu_y(dlg, 6),
                                  g->NoteBottom, note_lines_h(dlg, 2));
}

/* One row made at y: the line above it (not on a part's first), the
 * label, the choice under the taller of the label and the "now" text, the
 * "now" text at the right of the label's line. */
static void lay_row(HWND dlg, const LAYGEO *g, const LAYROW *lr, int i,
                    int y, int first, HFONT font, int id, ROWCTL *rc)
{
    int cy = y + g->TopH[i] + g->Gap;

    /* The line between rows, with a gap above and below it: a label is
     * not read as the title of the choice above it (owner, 2026-10-19). */
    rc->Sep = first ? NULL : make_px(dlg, "STATIC", "", SS_ETCHEDHORZ, g->X,
                                     y - (g->Sep + 2) / 2, g->W, 2, id + 3,
                                     NULL);
    rc->Label = make_px(dlg, "STATIC", lr->Label, SS_LEFT | SS_NOPREFIX, g->X,
                        y, g->LabelW, g->LabelH[i], id, font);
    if (lr->Range) {
        rc->Choice = make_px(dlg, "EDIT", "", WS_BORDER | WS_TABSTOP |
                             ES_AUTOHSCROLL | ES_NUMBER, g->X, cy, g->EditW,
                             g->EditH, id + 1, font);
    } else {
        /* The height given is the closed box and the open list. */
        rc->Choice = make_px(dlg, "COMBOBOX", "", CBS_DROPDOWNLIST |
                             WS_VSCROLL | WS_TABSTOP, g->X, cy, g->W,
                             g->ComboH + dlu_y(dlg, 80), id + 1, font);
    }
    rc->Now = make_px(dlg, "STATIC", lr->Now, SS_RIGHT | SS_NOPREFIX,
                      g->X + g->W - g->NowW, y, g->NowW, g->NowH[i], id + 2,
                      font);
}

static void rows_clear(ROWPAGE *rp)
{
    int i;

    for (i = 0; i < rp->Count; i++) {
        DestroyWindow(rp->R[i].Label);
        DestroyWindow(rp->R[i].Choice);
        DestroyWindow(rp->R[i].Now);
        if (rp->R[i].Sep != NULL) {
            DestroyWindow(rp->R[i].Sep);
        }
    }
    rp->Count = 0;
}

/* A settings page laid out: the head, the rows of the part shown made
 * (row k's Row is ids[k]), the note and "Advanced..." placed; the caller
 * fills the choices. part: 0 the first, -1 the last. */
static void lay_page(HWND dlg, ROWPAGE *rp, const LAYROW *lr, const int *ids,
                     int n, const char *head, int part)
{
    HFONT font = (HFONT)SendMessageA(dlg, WM_GETFONT, 0, 0);
    LAYGEO g;
    char text[600];
    int btnW = dlu_x(dlg, 60);
    int btnH = dlu_y(dlg, 14);
    int y;
    int i;

    rows_clear(rp);
    lay_measure(dlg, font, lr, n, head, &g);
    rp->Parts = g.Parts > 0 ? g.Parts : 1;
    rp->Part = wiz_lay_part(part, rp->Parts);
    head_text(head, rp->Part, rp->Parts, text, sizeof(text));
    SetDlgItemTextA(dlg, IDC_S_HEAD, text);
    SetWindowPos(GetDlgItem(dlg, IDC_S_HEAD), NULL, g.X, dlu_y(dlg, 2), g.W,
                 g.HeadH, SWP_NOZORDER | SWP_NOACTIVATE);
    y = g.RowsTop;
    for (i = g.Parts > 0 ? g.Start[rp->Part] : 0;
         g.Parts > 0 && i < g.Start[rp->Part + 1]; i++) {
        ROWCTL *rc = &rp->R[rp->Count];
        lay_row(dlg, &g, &lr[i], i, y, rp->Count == 0, font,
                IDC_ROW_BASE + IDC_ROW_STRIDE * rp->Count, rc);
        rc->Row = ids[i];
        rp->Count++;
        y += g.RowH[i] + g.Sep;
    }
    SetWindowPos(GetDlgItem(dlg, IDC_S_NOTE), NULL, g.X, g.NoteTop,
                 g.W - btnW - dlu_x(dlg, 6), g.NoteBottom - g.NoteTop,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(dlg, IDC_S_ADVANCED), NULL, g.X + g.W - btnW,
                 g.NoteBottom - btnH, btnW, btnH, SWP_NOZORDER |
                 SWP_NOACTIVATE);
}

/* What an open list adds to its longest entry: its frame, its scroll bar
 * and 4 dialog units. */
static int combo_extra(HWND combo)
{
    return 2 * GetSystemMetrics(SM_CXEDGE) + GetSystemMetrics(SM_CXVSCROLL) +
           dlu_x(GetParent(combo), 4);
}

/* A drop-down's open list as wide as its longest entry, measured in the
 * box's own font, with the list's frame and scroll bar. */
static void combo_fit(HWND combo)
{
    char text[320];
    HFONT f = (HFONT)SendMessageA(combo, WM_GETFONT, 0, 0);
    HDC dc = GetDC(combo);
    HFONT old = f != NULL ? (HFONT)SelectObject(dc, f) : NULL;
    RECT r;
    RECT work;
    SIZE sz;
    int longest = 0;
    int n = (int)SendMessageA(combo, CB_GETCOUNT, 0, 0);
    int len;
    int i;

    for (i = 0; i < n; i++) {
        len = (int)SendMessageA(combo, CB_GETLBTEXTLEN, (WPARAM)i, 0);
        if (len <= 0 || len >= (int)sizeof(text)) {
            continue;
        }
        SendMessageA(combo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)text);
        if (GetTextExtentPoint32A(dc, text, len, &sz) && sz.cx > longest) {
            longest = sz.cx;
        }
    }
    if (old != NULL) {
        SelectObject(dc, old);
    }
    ReleaseDC(combo, dc);
    GetWindowRect(combo, &r);
    if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
    }
    SendMessageA(combo, CB_SETDROPPEDWIDTH,
                 (WPARAM)wiz_lay_drop_w(longest, combo_extra(combo),
                                        r.right - r.left,
                                        work.right - work.left), 0);
}

/* A row of the drivers' values as the layout sees it. */
static void row_texts(const INST_ROW *r, LAYROW *lr)
{
    row_label(r, lr->Label, sizeof(lr->Label));
    if (r->Greyed) {
        /* Shown, not hidden, so the page reads the same on every system
         * (owner, 2026-10-19); row_read never takes it. */
        _snprintf(lr->Now, sizeof(lr->Now) - 1, "(%s)",
                  inst_value_only(r->Value));
        lr->Now[sizeof(lr->Now) - 1] = '\0';
    } else {
        row_now(r, lr->Now, sizeof(lr->Now));
    }
    lr->Range = instValues[r->Value].Hi != 0;
}

/* A made row's choice filled: the number, or the list (its open width
 * fitted); greyed with its label where the row is another system's. */
static void row_choice(const ROWCTL *rc)
{
    const INST_ROW *r = &rows[rc->Row];
    char text[32];

    if (instValues[r->Value].Hi != 0) {
        int mixed;
        INST_U32 start = row_start(r, &mixed);
        /* Empty: each controller keeps its own (the mixed state). */
        if (mixed) {
            text[0] = '\0';
        } else {
            _snprintf(text, sizeof(text) - 1, "%lu", (unsigned long)start);
            text[sizeof(text) - 1] = '\0';
        }
        SetWindowTextA(rc->Choice, text);
    } else {
        row_fill_combo(rc->Choice, r);
        combo_fit(rc->Choice);
    }
    if (r->Greyed) {
        EnableWindow(rc->Label, FALSE);
        EnableWindow(rc->Choice, FALSE);
    }
}

/* What a row's choice says now: 1 written (value in *v), 0 left as it is.
 * A range's text that does not parse, or lies outside it, is 0 with *v
 * the text's number (the page says so). */
static int row_read(const ROWCTL *rc, INST_U32 *v)
{
    const INST_ROW *r = &rows[rc->Row];
    char text[32];

    if (r->Greyed) {
        *v = r->InForce;
        return 0;
    }
    if (instValues[r->Value].Hi != 0) {
        GetWindowTextA(rc->Choice, text, sizeof(text));
        if (text[0] == '\0') {
            *v = r->InForce;
            return 0;
        }
        *v = (INST_U32)strtoul(text, NULL, 10);
        if (*v < instValues[r->Value].Lo || *v > instValues[r->Value].Hi) {
            return 0;
        }
        return choice_writes(r, 0, *v);
    }
    return combo_read(rc->Choice, r, v);
}

static void settings_note(HWND dlg, int page, int i)
{
    ROWPAGE *rp = page_rows(page);
    char note[4096];

    if (page == P_TBS && !tbPresent && i < 0) {
        SetDlgItemTextA(dlg, IDC_S_NOTE, inst_page_text(INST_PT_TB_LATER));
    } else if (i >= 0 && i < rp->Count) {
        row_note(&rows[rp->R[i].Row], note, sizeof(note));
        if (page == P_TBS && !tbPresent) {
            strncat(note, " ", sizeof(note) - strlen(note) - 1);
            strncat(note, inst_page_text(INST_PT_TB_LATER),
                    sizeof(note) - strlen(note) - 1);
        }
        SetDlgItemTextA(dlg, IDC_S_NOTE, note);
    } else {
        SetDlgItemTextA(dlg, IDC_S_NOTE, "");
    }
}

/* A settings page shown at a part (0 the first, -1 the last). */
static void settings_fill(HWND dlg, int page, int part)
{
    ROWPAGE *rp = page_rows(page);
    LAYROW lr[WIZ_ROWS];
    int idx[WIZ_ROWS];
    int n;
    int i;
    int adv;

    n = rows_for(page, 0, idx, WIZ_ROWS);
    for (i = 0; i < n; i++) {
        row_texts(&rows[idx[i]], &lr[i]);
    }
    lay_page(dlg, rp, lr, idx, n, inst_page_text(page == P_USB ? INST_PT_USB
                                                               : INST_PT_TBSET),
             part);
    for (i = 0; i < rp->Count; i++) {
        row_choice(&rp->R[i]);
    }
    adv = rows_for(page, 1, idx, WIZ_ROWS);
    ShowWindow(GetDlgItem(dlg, IDC_S_ADVANCED), adv > 0 ? SW_SHOW : SW_HIDE);
    settings_note(dlg, page, rp->Count > 0 ? 0 : -1);
}

/* Leaving a page: each row's choice taken. 0 when a range's text is not
 * in its range (said; the page stays). */
static int settings_leave(HWND dlg, int page)
{
    ROWPAGE *rp = page_rows(page);
    int i;
    INST_U32 v;

    for (i = 0; i < rp->Count; i++) {
        const INST_ROW *r = &rows[rp->R[i].Row];
        char typed[32];
        typed[0] = '\0';
        if (r->Greyed) {
            choiceSet[r->Value] = 0;
            continue;
        }
        if (instValues[r->Value].Hi != 0) {
            GetWindowTextA(rp->R[i].Choice, typed, sizeof(typed));
        }
        if (row_read(&rp->R[i], &v)) {
            choiceSet[r->Value] = 1;
            choiceVal[r->Value] = v;
        } else if (instValues[r->Value].Hi != 0 && typed[0] != '\0' &&
                   (v < instValues[r->Value].Lo ||
                    v > instValues[r->Value].Hi)) {
            char text[256];
            _snprintf(text, sizeof(text) - 1, "%s takes %lu to %lu.",
                      instValues[r->Value].Name,
                      (unsigned long)instValues[r->Value].Lo,
                      (unsigned long)instValues[r->Value].Hi);
            text[sizeof(text) - 1] = '\0';
            MessageBoxA(dlg, text, "xHCI98 Setup", MB_OK | MB_ICONWARNING);
            return 0;
        } else {
            choiceSet[r->Value] = 0;
        }
    }
    return 1;
}

/* ---- the Advanced dialog (record 26 section 5.4) --------------------------- */

/* A list of the page's never-asked values; the one selected shown whole
 * below it with its choice. The choices are kept apart and taken at OK. */
static int advPage;
static int advIdx[WIZ_ROWS];
static int advCount;
static int advCur = -1;
static int advSet[WIZ_ROWS];
static INST_U32 advVal[WIZ_ROWS];

static void adv_cell(HWND lv, int i)
{
    const INST_ROW *r = &rows[advIdx[i]];
    char text[96];

    if (r->Greyed) {
        _snprintf(text, sizeof(text) - 1, "(%s)", inst_value_only(r->Value));
    } else if (advSet[i]) {
        _snprintf(text, sizeof(text) - 1, "%lu (to be written)",
                  (unsigned long)advVal[i]);
    } else {
        row_now(r, text, sizeof(text));
    }
    text[sizeof(text) - 1] = '\0';
    ListView_SetItemText(lv, i, 1, text);
}

/* The selected row's choice read into advSet/advVal. 0 when a range's
 * text is out of its range (said). */
static int adv_take(HWND dlg)
{
    const INST_ROW *r;
    char text[32];
    INST_U32 v;

    if (advCur < 0) {
        return 1;
    }
    r = &rows[advIdx[advCur]];
    if (r->Greyed) {
        advSet[advCur] = 0;
        return 1;
    }
    if (instValues[r->Value].Hi != 0) {
        GetDlgItemTextA(dlg, IDC_A_EDIT, text, sizeof(text));
        if (text[0] == '\0') {
            /* The mixed state, or nothing typed: left as it is. */
            advSet[advCur] = 0;
            advVal[advCur] = r->InForce;
            adv_cell(GetDlgItem(dlg, IDC_LIST), advCur);
            return 1;
        }
        v = (INST_U32)strtoul(text, NULL, 10);
        if (v < instValues[r->Value].Lo || v > instValues[r->Value].Hi) {
            _snprintf(text, sizeof(text) - 1, "%lu to %lu.",
                      (unsigned long)instValues[r->Value].Lo,
                      (unsigned long)instValues[r->Value].Hi);
            text[sizeof(text) - 1] = '\0';
            MessageBoxA(dlg, text, instValues[r->Value].Name, MB_OK |
                        MB_ICONWARNING);
            return 0;
        }
        advSet[advCur] = choice_writes(r, 0, v);
    } else {
        advSet[advCur] = combo_read(GetDlgItem(dlg, IDC_A_CHOICE), r, &v);
    }
    advVal[advCur] = v;
    adv_cell(GetDlgItem(dlg, IDC_LIST), advCur);
    return 1;
}

static void adv_show(HWND dlg, int i)
{
    const INST_ROW *r = &rows[advIdx[i]];
    char note[4096];
    char text[32];
    int hadSet = choiceSet[r->Value];
    INST_U32 hadVal = choiceVal[r->Value];
    int mixed;

    advCur = i;
    row_note(r, note, sizeof(note));
    set_edit(dlg, IDC_A_TEXT, note);
    /* The choice shown starts at what is pending for the row; the pages'
     * own record is put back at once (Cancel keeps nothing). */
    choiceSet[r->Value] = advSet[i];
    choiceVal[r->Value] = advVal[i];
    if (instValues[r->Value].Hi != 0) {
        INST_U32 start = row_start(r, &mixed);
        if (mixed) {
            text[0] = '\0';
        } else {
            _snprintf(text, sizeof(text) - 1, "%lu", (unsigned long)start);
            text[sizeof(text) - 1] = '\0';
        }
        SetDlgItemTextA(dlg, IDC_A_EDIT, text);
        ShowWindow(GetDlgItem(dlg, IDC_A_EDIT), SW_SHOW);
        ShowWindow(GetDlgItem(dlg, IDC_A_CHOICE), SW_HIDE);
    } else {
        row_fill_combo(GetDlgItem(dlg, IDC_A_CHOICE), r);
        combo_fit(GetDlgItem(dlg, IDC_A_CHOICE));
        ShowWindow(GetDlgItem(dlg, IDC_A_CHOICE), SW_SHOW);
        ShowWindow(GetDlgItem(dlg, IDC_A_EDIT), SW_HIDE);
    }
    EnableWindow(GetDlgItem(dlg, IDC_A_LABEL), !r->Greyed);
    EnableWindow(GetDlgItem(dlg, IDC_A_CHOICE), !r->Greyed);
    EnableWindow(GetDlgItem(dlg, IDC_A_EDIT), !r->Greyed);
    choiceSet[r->Value] = hadSet;
    choiceVal[r->Value] = hadVal;
}

static WIZ_DLGRET CALLBACK adv_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND lv;
    LVCOLUMNA c;
    LVITEMA it;
    NMHDR *nm;
    int i;

    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextA(dlg, IDC_A_HEAD, inst_page_text(INST_PT_ADVANCED));
        lv = GetDlgItem(dlg, IDC_LIST);
        memset(&c, 0, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.cx = dlu_x(dlg, 140);
        c.pszText = "Value";
        ListView_InsertColumn(lv, 0, &c);
        c.cx = dlu_x(dlg, 150);
        c.pszText = "Setting";
        ListView_InsertColumn(lv, 1, &c);
        advCount = rows_for(advPage, 1, advIdx, WIZ_ROWS);
        for (i = 0; i < advCount; i++) {
            const INST_ROW *r = &rows[advIdx[i]];
            int mixed;
            advSet[i] = choiceSet[r->Value];
            advVal[i] = advSet[i] ? choiceVal[r->Value]
                                  : row_start(r, &mixed);
            memset(&it, 0, sizeof(it));
            it.mask = LVIF_TEXT;
            it.iItem = i;
            it.pszText = (char *)instValues[r->Value].Name;
            ListView_InsertItem(lv, &it);
            adv_cell(lv, i);
        }
        advCur = -1;
        if (advCount > 0) {
            ListView_SetItemState(lv, 0, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            adv_show(dlg, 0);
        }
        return TRUE;
    case WM_NOTIFY:
        nm = (NMHDR *)lp;
        if (nm->idFrom == IDC_LIST && nm->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *nl = (NMLISTVIEW *)lp;
            if ((nl->uNewState & LVIS_SELECTED) && nl->iItem >= 0 &&
                nl->iItem < advCount && nl->iItem != advCur) {
                if (adv_take(dlg)) {
                    adv_show(dlg, nl->iItem);
                }
            }
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_A_CHOICE && HIWORD(wp) == CBN_SELCHANGE) {
            adv_take(dlg);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            if (!adv_take(dlg)) {
                return TRUE;
            }
            for (i = 0; i < advCount; i++) {
                int value = rows[advIdx[i]].Value;
                choiceSet[value] = advSet[i];
                choiceVal[value] = advVal[i];
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            /* Nothing of the dialog is kept. */
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

/* ---- Stor98's settings page (record 26 section 5.6) ------------------------ */

/* Row i's choice as the page shows it: "<value> - <meaning>". */
static void s98_item(int i, int k, char *out, size_t cap)
{
    const INST_S98_VALUE *v = &instS98Values[i];

    if (v->Kind == INST_S98_WORDS) {
        _snprintf(out, cap - 1, "%s - %s", v->Words[k], v->Means[k]);
    } else {
        _snprintf(out, cap - 1, "%d - %s", k, v->Means[k]);
    }
    out[cap - 1] = '\0';
}

static void s98_item_value(int i, int k, char *out, size_t cap)
{
    if (instS98Values[i].Kind == INST_S98_WORDS) {
        inst_copy(out, instS98Values[i].Words[k], cap);
    } else {
        _snprintf(out, cap - 1, "%d", k);
        out[cap - 1] = '\0';
    }
}

/* Where the values go, said under the rows (Stor98's rule, per system). */
static const char *s98_where(void)
{
    return is_9x() ? "Written as values of their own names under "
                     "HKLM\\System\\CurrentControlSet\\Services\\stor98; read "
                     "when the controller starts."
                   : "Written into the one DriverParameter string under "
                     "HKLM\\SYSTEM\\CurrentControlSet\\Services\\stor98\\"
                     "Parameters\\Device, each key kept apart; read when the "
                     "controller starts.";
}

/* Where the page reads a row: the engine, or the self-test's made-up
 * rows. */
static int (*s98RowFn)(int i, INST_S98ROW *r) = inst_eng_s98_row;

/* A made row's list: not set, "not set (the built-in default is ...)"
 * preselected; set, the value found preselected. Either, left as it is,
 * writes nothing; the default chosen where none was set is written. */
static void s98_combo_fill(HWND combo, int i)
{
    const INST_S98_VALUE *v = &instS98Values[i];
    INST_S98ROW r;
    char text[256];
    char val[32];
    int sel = 0;
    int k;

    s98RowFn(i, &r);
    SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)(r.State > 0 ?
                 "(as it is: nothing written)" : r.NowText));
    for (k = 0; k < INST_S98_WORDS_MAX && v->Means[k] != NULL; k++) {
        s98_item(i, k, text, sizeof(text));
        SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)text);
        s98_item_value(i, k, val, sizeof(val));
        if (s98WinSet[i] ? inst_eq(val, s98WinVal[i])
                         : (r.State > 0 && inst_eq(val, r.Now))) {
            sel = k + 1;
        }
    }
    SendMessageA(combo, CB_SETCURSEL, (WPARAM)sel, 0);
    combo_fit(combo);
}

/* Stor98's page shown at a part (0 the first, -1 the last), laid out as
 * the drivers' pages are (record 26 section 5.6). */
static void s98_page_fill(HWND dlg, int part)
{
    ROWPAGE *rp = page_rows(P_S98);
    LAYROW lr[WIZ_ROWS];
    int ids[WIZ_ROWS];
    INST_S98ROW r;
    int n = 0;
    int i;

    for (i = 0; i < INST_S98_COUNT && n < WIZ_ROWS; i++) {
        if (!s98RowFn(i, &r) || !r.Asked) {
            continue;
        }
        _snprintf(lr[n].Label, sizeof(lr[n].Label) - 1, "%s - %s", r.Name,
                  instS98Values[i].What);
        lr[n].Label[sizeof(lr[n].Label) - 1] = '\0';
        inst_copy(lr[n].Now, r.NowText, sizeof(lr[n].Now));
        lr[n].Range = 0;
        ids[n++] = i;
    }
    lay_page(dlg, rp, lr, ids, n, inst_page_text(INST_PT_S98SET), part);
    for (i = 0; i < rp->Count; i++) {
        s98_combo_fill(rp->R[i].Choice, rp->R[i].Row);
    }
    SetDlgItemTextA(dlg, IDC_S_NOTE, s98_where());
    ShowWindow(GetDlgItem(dlg, IDC_S_ADVANCED), SW_SHOW);
}

static void s98_page_leave(HWND dlg)
{
    ROWPAGE *rp = page_rows(P_S98);
    int n;
    int sel;
    int i;

    (void)dlg;
    for (n = 0; n < rp->Count; n++) {
        INST_S98ROW r;
        i = rp->R[n].Row;
        s98RowFn(i, &r);
        sel = (int)SendMessageA(rp->R[n].Choice, CB_GETCURSEL, 0, 0);
        if (sel <= 0) {
            s98WinSet[i] = 0;
        } else {
            s98_item_value(i, sel - 1, s98WinVal[i], sizeof(s98WinVal[i]));
            /* The value set now, kept: nothing written. */
            s98WinSet[i] = !(r.State > 0 && inst_eq(s98WinVal[i], r.Now));
        }
    }
}

/* The Advanced dialog for Stor98's numbers: the list, the row selected
 * shown whole, an edit box (empty: as it is). */
static int s98AdvRow[INST_S98_COUNT];
static int s98AdvCount;
static int s98AdvCur = -1;
static int s98AdvSet[INST_S98_COUNT];
static char s98AdvVal[INST_S98_COUNT][32];
static int s98AdvGrey[INST_S98_COUNT];   /* inst_s98_greyed: never written */

static void s98_adv_cell(HWND lv, int n)
{
    INST_S98ROW r;
    char text[192];

    inst_eng_s98_row(s98AdvRow[n], &r);
    if (s98AdvGrey[n]) {
        _snprintf(text, sizeof(text) - 1, "(%s)",
                  inst_s98_only(s98AdvRow[n]));
        text[sizeof(text) - 1] = '\0';
    } else if (s98AdvSet[n]) {
        _snprintf(text, sizeof(text) - 1, "%s (to be written)",
                  s98AdvVal[n]);
        text[sizeof(text) - 1] = '\0';
    } else {
        inst_copy(text, r.NowText, sizeof(text));
    }
    ListView_SetItemText(lv, n, 1, text);
}

static int s98_adv_take(HWND dlg)
{
    char text[32];
    char canon[32];
    const char *why;
    INST_S98ROW r;
    int i;

    if (s98AdvCur < 0) {
        return 1;
    }
    if (s98AdvGrey[s98AdvCur]) {
        s98AdvSet[s98AdvCur] = 0;
        return 1;
    }
    i = s98AdvRow[s98AdvCur];
    inst_eng_s98_row(i, &r);
    GetDlgItemTextA(dlg, IDC_A_EDIT, text, sizeof(text));
    {
        /* Spaces alone are nothing typed. */
        size_t a = 0;
        size_t b = strlen(text);
        while (text[a] == ' ' || text[a] == '\t') {
            a++;
        }
        while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) {
            b--;
        }
        memmove(text, text + a, b - a);
        text[b - a] = '\0';
    }
    if (text[0] == '\0' || (r.State > 0 && strcmp(text, r.Now) == 0)) {
        /* Empty, or the value set now: as it is, nothing written. */
        s98AdvSet[s98AdvCur] = 0;
    } else if (!inst_s98_check(i, !is_9x(), text, canon, sizeof(canon),
                               &why)) {
        char msg[160];
        _snprintf(msg, sizeof(msg) - 1, "%s: %s.", text, why);
        msg[sizeof(msg) - 1] = '\0';
        MessageBoxA(dlg, msg, instS98Values[i].KeyNt, MB_OK |
                    MB_ICONWARNING);
        return 0;
    } else {
        s98AdvSet[s98AdvCur] = 1;
        inst_copy(s98AdvVal[s98AdvCur], canon, sizeof(s98AdvVal[0]));
    }
    s98_adv_cell(GetDlgItem(dlg, IDC_LIST), s98AdvCur);
    return 1;
}

static void s98_adv_show(HWND dlg, int n)
{
    const INST_S98_VALUE *v = &instS98Values[s98AdvRow[n]];
    INST_S98ROW r;
    char note[1024];
    char range[160];
    INST_U32 hi = v->Hi;

    s98AdvCur = n;
    EnableWindow(GetDlgItem(dlg, IDC_A_LABEL), !s98AdvGrey[n]);
    EnableWindow(GetDlgItem(dlg, IDC_A_EDIT), !s98AdvGrey[n]);
    SetDlgItemTextA(dlg, IDC_A_LABEL, "The value:");
    ShowWindow(GetDlgItem(dlg, IDC_A_EDIT), SW_SHOW);
    ShowWindow(GetDlgItem(dlg, IDC_A_CHOICE), SW_HIDE);
    if (s98AdvGrey[n]) {
        _snprintf(note, sizeof(note) - 1, "%s: %s.\r\n%s: not used on this "
                  "system, so it is greyed and nothing is written.",
                  v->KeyNt != NULL ? v->KeyNt : v->Name9x, v->What,
                  inst_s98_only(s98AdvRow[n]));
        note[sizeof(note) - 1] = '\0';
        SetDlgItemTextA(dlg, IDC_A_TEXT, note);
        SetDlgItemTextA(dlg, IDC_A_EDIT, "");
        return;
    }
    inst_eng_s98_row(s98AdvRow[n], &r);
    if (s98AdvRow[n] == INST_S98_MAXTRANSFERKB) {
        hi = is_9x() ? 512UL : 256UL;
    }
    if (v->Step > 1UL) {
        _snprintf(range, sizeof(range) - 1, "%lu to %lu %s, in steps of %lu",
                  (unsigned long)v->Lo, (unsigned long)hi, v->Unit,
                  (unsigned long)v->Step);
    } else if (v->ZeroOk) {
        _snprintf(range, sizeof(range) - 1, "%lu to %lu %s, or 0: %s",
                  (unsigned long)v->Lo, (unsigned long)hi, v->Unit, v->Zero);
    } else {
        _snprintf(range, sizeof(range) - 1, "%lu to %lu %s",
                  (unsigned long)v->Lo, (unsigned long)hi, v->Unit);
    }
    range[sizeof(range) - 1] = '\0';
    _snprintf(note, sizeof(note) - 1,
              "%s: %s.\r\nNow: %s.\r\nThe built-in default: %s. Takes %s. "
              "Empty: as it is, nothing written.\r\n%s", r.Name, v->What,
              r.NowText, r.Default, range, s98_where());
    note[sizeof(note) - 1] = '\0';
    SetDlgItemTextA(dlg, IDC_A_TEXT, note);
    SetDlgItemTextA(dlg, IDC_A_LABEL, "The value:");
    /* The value set now is shown, preselected (record 26 section 5.6);
     * left as it is, it writes nothing. */
    SetDlgItemTextA(dlg, IDC_A_EDIT, s98AdvSet[n] ? s98AdvVal[n] :
                                     (r.State > 0 ? r.Now : ""));
    ShowWindow(GetDlgItem(dlg, IDC_A_EDIT), SW_SHOW);
    ShowWindow(GetDlgItem(dlg, IDC_A_CHOICE), SW_HIDE);
}

static WIZ_DLGRET CALLBACK s98_adv_proc(HWND dlg, UINT msg, WPARAM wp,
                                        LPARAM lp)
{
    HWND lv;
    LVCOLUMNA c;
    LVITEMA it;
    NMHDR *nm;
    INST_S98ROW r;
    int i;

    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextA(dlg, IDC_A_HEAD, inst_page_text(INST_PT_S98SET));
        SendMessageA(GetDlgItem(dlg, IDC_A_EDIT), EM_LIMITTEXT, 10, 0);
        lv = GetDlgItem(dlg, IDC_LIST);
        memset(&c, 0, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.cx = dlu_x(dlg, 90);
        c.pszText = "Value";
        ListView_InsertColumn(lv, 0, &c);
        c.cx = dlu_x(dlg, 200);
        c.pszText = "Setting";
        ListView_InsertColumn(lv, 1, &c);
        s98AdvCount = 0;
        for (i = 0; i < INST_S98_COUNT; i++) {
            int grey = 0;
            if (!inst_eng_s98_row(i, &r)) {
                /* Another system's row: shown greyed (owner,
                 * 2026-10-19), named as that system names it. */
                if (!inst_s98_greyed(i, !is_9x()) || instS98Values[i].Asked) {
                    continue;
                }
                grey = 1;
                r.Name = instS98Values[i].KeyNt != NULL ?
                         instS98Values[i].KeyNt : instS98Values[i].Name9x;
            } else if (r.Asked) {
                continue;
            }
            s98AdvGrey[s98AdvCount] = grey;
            s98AdvRow[s98AdvCount] = i;
            s98AdvSet[s98AdvCount] = s98WinSet[i];
            inst_copy(s98AdvVal[s98AdvCount], s98WinVal[i],
                      sizeof(s98AdvVal[0]));
            memset(&it, 0, sizeof(it));
            it.mask = LVIF_TEXT;
            it.iItem = s98AdvCount;
            it.pszText = (char *)r.Name;
            ListView_InsertItem(lv, &it);
            s98_adv_cell(lv, s98AdvCount);
            s98AdvCount++;
        }
        s98AdvCur = -1;
        if (s98AdvCount > 0) {
            ListView_SetItemState(lv, 0, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            s98_adv_show(dlg, 0);
        }
        return TRUE;
    case WM_NOTIFY:
        nm = (NMHDR *)lp;
        if (nm->idFrom == IDC_LIST && nm->code == LVN_ITEMCHANGING) {
            /* A value refused keeps its row selected: the change is
             * vetoed before the selection moves. */
            NMLISTVIEW *nl = (NMLISTVIEW *)lp;
            if ((nl->uNewState & LVIS_SELECTED) &&
                !(nl->uOldState & LVIS_SELECTED) && nl->iItem != s98AdvCur &&
                !s98_adv_take(dlg)) {
                SET_RESULT(dlg, TRUE);
                return TRUE;
            }
        }
        if (nm->idFrom == IDC_LIST && nm->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *nl = (NMLISTVIEW *)lp;
            if ((nl->uNewState & LVIS_SELECTED) && nl->iItem >= 0 &&
                nl->iItem < s98AdvCount && nl->iItem != s98AdvCur) {
                s98_adv_show(dlg, nl->iItem);
            }
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            if (!s98_adv_take(dlg)) {
                return TRUE;
            }
            for (i = 0; i < s98AdvCount; i++) {
                s98WinSet[s98AdvRow[i]] = s98AdvSet[i];
                inst_copy(s98WinVal[s98AdvRow[i]], s98AdvVal[i],
                          sizeof(s98WinVal[0]));
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

/* The Summary page's box (record 26 section 6.3, draft 16; owner,
 * 2026-10-19): on 98 SE and ME, one box when the run replaces Stor98's
 * file. Ticked, the update goes ahead; unticked, Stor98 is left for a
 * later run. Install does not wait for it. */
/* ---- the Settings dialog (record 26 section 5.0, draft 35) ----------------- */

/* A property sheet of its own, a tab per component in the run, each tab one
 * part of its main rows at a time and its Advanced button. */
static int setApplied;

/* A tab's "More >" button, left of Advanced, only when its rows take more
 * than one part; the note narrowed to make room. */
static void tab_part_btn(HWND dlg, int page)
{
    ROWPAGE *rp = page_rows(page);
    HWND b = GetDlgItem(dlg, IDC_S_PART);
    HWND a = GetDlgItem(dlg, IDC_S_ADVANCED);
    HWND n = GetDlgItem(dlg, IDC_S_NOTE);
    RECT ra;
    RECT rn;
    POINT pa;
    POINT pn;
    int gap = dlu_x(dlg, 6);
    int w;

    if (rp->Parts <= 1) {
        ShowWindow(b, SW_HIDE);
        return;
    }
    GetWindowRect(a, &ra);
    GetWindowRect(n, &rn);
    pa.x = ra.left;
    pa.y = ra.top;
    pn.x = rn.left;
    pn.y = rn.top;
    ScreenToClient(dlg, &pa);
    ScreenToClient(dlg, &pn);
    w = ra.right - ra.left;
    SetWindowPos(b, NULL, pa.x - w - gap, pa.y, w, ra.bottom - ra.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(n, NULL, pn.x, pn.y, pa.x - w - 2 * gap - pn.x,
                 rn.bottom - rn.top, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(b, SW_SHOW);
}

static void tab_fill(HWND dlg, int page, int part)
{
    if (page == P_S98) {
        s98_page_fill(dlg, part);
    } else {
        settings_fill(dlg, page, part);
    }
    tab_part_btn(dlg, page);
}

/* The tab's choices taken: 0 when a range's text is out of its range. */
static int tab_leave(HWND dlg, int page)
{
    if (page == P_S98) {
        s98_page_leave(dlg);
        return 1;
    }
    return settings_leave(dlg, page);
}

static WIZ_DLGRET CALLBACK tab_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    int page = GET_PAGE(dlg);
    NMHDR *nm;

    switch (msg) {
    case WM_INITDIALOG:
        page = (int)((PROPSHEETPAGEA *)lp)->lParam;
        SET_PAGE(dlg, page);
        tab_fill(dlg, page, 0);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_S_PART:
            if (tab_leave(dlg, page)) {
                ROWPAGE *rp = page_rows(page);
                tab_fill(dlg, page, (rp->Part + 1) % rp->Parts);
            }
            return TRUE;
        case IDC_S_ADVANCED:
            if (page == P_S98) {
                s98_page_leave(dlg);
                DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_ADVANCED), dlg,
                                s98_adv_proc, 0);
                return TRUE;
            }
            if (!settings_leave(dlg, page)) {
                return TRUE;
            }
            advPage = page;
            DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_ADVANCED), dlg,
                            adv_proc, 0);
            return TRUE;
        default:
            if (page != P_S98 && (HIWORD(wp) == CBN_SETFOCUS ||
                                  HIWORD(wp) == CBN_SELCHANGE ||
                                  HIWORD(wp) == EN_SETFOCUS)) {
                ROWPAGE *rp = page_rows(page);
                int i;
                for (i = 0; i < rp->Count; i++) {
                    if ((HWND)lp == rp->R[i].Choice) {
                        settings_note(dlg, page, i);
                    }
                }
            }
            break;
        }
        break;
    case WM_NOTIFY:
        nm = (NMHDR *)lp;
        switch (nm->code) {
        case PSN_KILLACTIVE:
            SET_RESULT(dlg, tab_leave(dlg, page) ? FALSE : TRUE);
            return TRUE;
        case PSN_APPLY:
            if (!tab_leave(dlg, page)) {
                SET_RESULT(dlg, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }
            setApplied = 1;
            SET_RESULT(dlg, PSNRET_NOERROR);
            return TRUE;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

/* Settings... on the Components page: the boxes handed to the engine
 * first, so the tabs are those of the components ticked now; OK keeps the
 * choices, Cancel restores them as they stood when this opening began. */
static void settings_open(HWND comp)
{
    static const char *const title[3] = { "USB", "Thunderbolt", "Stor98" };
    static const int pg[3] = { P_USB, P_TBS, P_S98 };
    static int keepSet[INST_MAX_VALUES];
    static INST_U32 keepVal[INST_MAX_VALUES];
    static int keepS98Set[INST_S98_COUNT];
    static char keepS98Val[INST_S98_COUNT][32];
    PROPSHEETPAGEA p[3];
    HPROPSHEETPAGE hp[3];
    PROPSHEETHEADERA h;
    int idx[WIZ_ROWS];
    int n = 0;
    int i;

    comp_take(comp);
    for (i = 0; i < 3; i++) {
        int in = pg[i] == P_USB ? usbChoice &&
                     (rows_for(P_USB, 0, idx, WIZ_ROWS) > 0 ||
                      rows_for(P_USB, 1, idx, WIZ_ROWS) > 0) :
                 pg[i] == P_TBS ? tbChoice : s98Choice;
        if (!in) {
            continue;
        }
        memset(&p[n], 0, sizeof(p[n]));
        p[n].dwSize = sizeof(p[n]);
        p[n].dwFlags = PSP_USETITLE;
        p[n].hInstance = hInst;
        p[n].pszTemplate = MAKEINTRESOURCEA(IDD_SETTINGS);
        p[n].pszTitle = title[i];
        p[n].pfnDlgProc = tab_proc;
        p[n].lParam = pg[i];
        hp[n] = CreatePropertySheetPageA(&p[n]);
        if (hp[n] == NULL) {
            while (n > 0) {
                DestroyPropertySheetPage(hp[--n]);
            }
            return;
        }
        n++;
    }
    if (n == 0) {
        MessageBoxA(GetParent(comp), "None of the components ticked has "
                    "settings on this computer.", "xHCI98 Setup",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    memcpy(keepSet, choiceSet, sizeof(keepSet));
    memcpy(keepVal, choiceVal, sizeof(keepVal));
    memcpy(keepS98Set, s98WinSet, sizeof(keepS98Set));
    memcpy(keepS98Val, s98WinVal, sizeof(keepS98Val));
    memset(&h, 0, sizeof(h));
    h.dwSize = sizeof(h);
    h.dwFlags = PSH_NOAPPLYNOW;
    h.hwndParent = GetParent(comp);
    h.hInstance = hInst;
    h.pszCaption = "xHCI98 Settings";
    h.nPages = (UINT)n;
    h.phpage = hp;
    setApplied = 0;
    PropertySheetA(&h);
    if (!setApplied) {
        memcpy(choiceSet, keepSet, sizeof(keepSet));
        memcpy(choiceVal, keepVal, sizeof(keepVal));
        memcpy(s98WinSet, keepS98Set, sizeof(keepS98Set));
        memcpy(s98WinVal, keepS98Val, sizeof(keepS98Val));
    }
    /* The tabs' controls went with the sheet. */
    memset(pageRows, 0, sizeof(pageRows));
}

/* Each paragraph's first sentence, one line each, for the Summary. */
static void first_sentences(TEXTBUF *t, const char *text)
{
    const char *p = text;
    char line[600];

    while (*p != '\0') {
        const char *end = strstr(p, ". ");
        const char *para = strstr(p, "\n\n");
        size_t n;
        if (end == NULL || (para != NULL && para < end)) {
            end = para != NULL ? para : p + strlen(p);
        } else {
            end++;
        }
        n = (size_t)(end - p);
        if (n > sizeof(line) - 8) {
            n = sizeof(line) - 8;
        }
        memcpy(line, "! ", 2);
        memcpy(line + 2, p, n);
        line[2 + n] = '\0';
        strcat(line, "\n");
        tb_add(t, line);
        p = para != NULL ? para + 2 : p + strlen(p);
    }
}

/* ---- the summary (record 26 section 5.7) ----------------------------------- */

static void summary_fill(HWND dlg)
{
    TEXTBUF t;
    char line[512];
    INST_FOUND f;
    int i;
    int any = 0;

    memset(&t, 0, sizeof(t));
    if (uninstallMode) {
        SetDlgItemTextA(dlg, IDC_TEXT, inst_page_text(INST_PT_UN_SUMMARY));
        tb_add(&t, "Removed, in this order, each step only once the one "
               "before it finished:\n");
        tb_add(&t, "- every device of this package, present or not, children "
               "first, the\n  controllers last;\n");
        tb_add(&t, "- on NT the tunnel ports' filter name, then the INFs, the "
               "services and the\n  driver files; on 98 SE and ME the INF "
               "copies and the driver files;\n");
        tb_add(&t, "- the xHCI98 tools, this installer and their start at "
               "login;\n- the settings this package wrote machine-wide, its "
               "records, this\n  Add/Remove Programs entry, and the install "
               "directory.\n\n");
        tb_add(&t, "Windows' own usbd.sys and usbui.dll stay, and so do other "
               "drivers' devices.\nNothing of another USB stack is put back: "
               "Windows finds the controller again\nat the next start.\n");
    } else {
        int writes = 0;
        int other;
        SetDlgItemTextA(dlg, IDC_TEXT, inst_page_text(INST_PT_SUMMARY));
        /* The headline from what the page lists below it, not from the
         * engine's answers, which the pages hand over only at Install
         * (matrix N1, 2026-10-19). */
        for (i = 0; i < INST_MAX_VALUES && i < instValueCount; i++) {
            writes |= choiceSet[i] && choice_live(i);
        }
        for (i = 0; s98Choice && i < INST_S98_COUNT; i++) {
            writes |= s98WinSet[i];
        }
        /* The tools installed for the first time or moved, and the
         * USB Tree's start at login turned on or off, count too. */
        other = (tbChoice && !tbInstalled) || s98Choice ||
                (storShown && storOffered && storChoice) ||
                dirNow[0] == '\0' || !inst_eq(dirNow, dirChoice) ||
                (trayHere && trayChoice != inst_eng_tray_started()) ||
                (trayHere && (fastForced || fastChoice != fastNow));
        _snprintf(line, sizeof(line) - 1, "%s.\n\n",
                  inst_eng_summary_head(writes, other));
        line[sizeof(line) - 1] = '\0';
        tb_add(&t, line);
        {
            /* The Components page's warnings repeated (record 26 section
             * 5.0): each one's first sentence. */
            char warn[2048];
            if (inst_eng_comp_warn(usbChoice, storShown && storChoice,
                                   s98Choice, dirChoice, warn,
                                   sizeof(warn)) != 0) {
                first_sentences(&t, warn);
                tb_add(&t, "\n");
            }
        }
        for (i = 0; i < INST_MAX_VALUES && i < instValueCount; i++) {
            if (choiceSet[i] && choice_live(i) &&
                inst_cm_mode_off(instValues[i].Name, choiceVal[i])) {
                _snprintf(line, sizeof(line) - 1, "*** %s. ***\n\n",
                          instCmOffText);
                line[sizeof(line) - 1] = '\0';
                tb_add(&t, line);
            }
        }
        tb_add(&t, "The devices:\n");
        for (i = 0; inst_eng_found(i, &f); i++) {
            if (!f.Present) {
                continue;
            }
            _snprintf(line, sizeof(line) - 1, "- %s: %s\n", f.Desc, f.Action);
            line[sizeof(line) - 1] = '\0';
            tb_add(&t, line);
            any = 1;
        }
        if (!any) {
            tb_add(&t, "- none present: the drivers are staged for later.\n");
        }
        tb_add(&t, usbChoice ? "\nThe components:\n- xhci98.sys\n" :
               tbHave ? "\nThe components:\n- xhci98.sys and xhciuas.sys: "
               "left out (the connection manager alone)\n" :
               "\nThe components:\n- xhci98.sys and xhciuas.sys: left out "
               "(the xHCI98 tools alone)\n");
        if (usbChoice) {
            tb_add(&t, uasChoice ? "- xhciuas.sys, the UAS disk driver\n" :
                   "- xhciuas.sys, the UAS disk driver: left out (one "
                   "installed stays)\n");
        }
        if (tbHave) {
            tb_add(&t, tbChoice ? "- Thunderbolt 3 / USB4 support "
                   "(experimental)\n" : "- Thunderbolt 3 / USB4 support "
                   "(experimental): left out\n");
        }
        if (s98Have) {
            _snprintf(line, sizeof(line) - 1, s98Choice ?
                      "- Stor98 %s, the bundled NVMe driver\n" :
                      "- Stor98 %s, the bundled NVMe driver: left out\n",
                      s98Ver);
            line[sizeof(line) - 1] = '\0';
            tb_add(&t, line);
        }
        if (vsdShown && vsd9x) {
            const char *vl = inst_eng_vsd_plan();
            if (vl != NULL) {
                _snprintf(line, sizeof(line) - 1, "- %s\n", vl);
                line[sizeof(line) - 1] = '\0';
                tb_add(&t, line);
            }
        }
        {
            char ol[900];
            inst_eng_other_summary(dirChoice, ol, sizeof(ol));
            if (ol[0] != '\0') {
                char *p = ol;
                while (*p != '\0') {
                    char *e = strstr(p, "\r\n");
                    if (e != NULL) {
                        *e = '\0';
                    }
                    _snprintf(line, sizeof(line) - 1, "- %s\n", p);
                    line[sizeof(line) - 1] = '\0';
                    tb_add(&t, line);
                    if (e == NULL) {
                        break;
                    }
                    p = e + 2;
                }
            }
        }
        if (storShown) {
            char sl[400];
            inst_eng_stor_summary(sl, sizeof(sl));
            if (sl[0] != '\0') {
                /* Each part added whole: the note may carry the lone
                 * USBSTOR.INF's sentence (record 26 section 6.5.5). */
                tb_add(&t, "- ");
                tb_add(&t, sl);
                tb_add(&t, "\n  ");
                tb_add(&t, storNote);
                tb_add(&t, "\n");
            }
        }
        if (trayHere) {
            tb_add(&t, trayChoice ? "- the xHCI98 USB Tree, started at login\n"
                                  : "- the xHCI98 USB Tree, not started at "
                                    "login\n");
            if (fastForced || fastChoice != fastNow) {
                tb_add(&t, fastChoice ? "- its \"can run faster\" notice turned "
                                        "on for this computer\n"
                                      : "- its \"can run faster\" notice turned "
                                        "off for this computer\n");
            }
        }
        tb_add(&t, "\nThe settings written machine-wide:\n");
        any = 0;
        for (i = 0; i < INST_MAX_VALUES && i < instValueCount; i++) {
            if (choiceSet[i] && choice_live(i)) {
                _snprintf(line, sizeof(line) - 1, "- %s = %lu\n",
                          instValues[i].Name, (unsigned long)choiceVal[i]);
                line[sizeof(line) - 1] = '\0';
                tb_add(&t, line);
                any = 1;
            }
        }
        if (!any) {
            tb_add(&t, "- none: every value stays as it is.\n");
        }
        if (s98Choice) {
            any = 0;
            tb_add(&t, "\nStor98's settings, where Stor98 reads them:\n");
            for (i = 0; i < INST_S98_COUNT; i++) {
                INST_S98ROW r;
                if (s98WinSet[i] && inst_eng_s98_row(i, &r)) {
                    _snprintf(line, sizeof(line) - 1, "- %s = %s\n", r.Name,
                              s98WinVal[i]);
                    line[sizeof(line) - 1] = '\0';
                    tb_add(&t, line);
                    any = 1;
                }
            }
            if (!any) {
                tb_add(&t, "- none: each stays as it is.\n");
            }
            if (is_9x() && inst_eng_s98_file_update()) {
                _snprintf(line, sizeof(line) - 1, "\nStor98's driver file "
                          "(stor98.mpd) is replaced with %s when\nWindows "
                          "next starts, once the box below is ticked; no "
                          "drive is\nreinstalled. Afterwards Windows is shut "
                          "down and started again, not\nrestarted. Unticked, "
                          "Stor98 is left for a later run.\n", s98Ver);
                line[sizeof(line) - 1] = '\0';
                tb_add(&t, line);
            }
        }
        _snprintf(line, sizeof(line) - 1, "\nThe tools folder: %s\n",
                  dirChoice);
        line[sizeof(line) - 1] = '\0';
        tb_add(&t, line);
        if (inst_eng_controller_changes()) {
            tb_add(&t, "\nBEFORE YOU GO ON: the USB keyboard and mouse on the "
                   "controller stop for a\nmoment while it restarts; have a "
                   "keyboard and mouse that do not go\nthrough this driver "
                   "(PS/2, a laptop's built-in ones).\n");
        }
        if (is_9x()) {
            tb_add(&t, "\nWindows' own wizards may open for the devices the "
                   "controller shows after its\nrestart: answer them (Next, "
                   "Next, Finish), and type the tools folder where\nthey ask "
                   "for a folder or for the \"xhci98 xHCI Host Controller "
                   "Disk\". Finish\neach one, even when it finds no driver; "
                   "never cancel one (a cancelled\nwizard hides its device "
                   "until Windows restarts). Asked for the Windows CD,\n"
                   "insert it and type its WIN98 folder (WIN9X on ME) with "
                   "your CD drive's\nletter, or the folder your PC keeps "
                   "Windows' setup files in.\n");
        }
    }
    set_edit(dlg, IDC_DETAIL, t.Buf != NULL ? t.Buf : "");
    free(t.Buf);
}

/* ---- the progress (record 26 section 5.8) --------------------------------- */

static int stepsTotal;
static int stepsDone;
static int stepOpen = -1;       /* the list's "running" row, or -1 */

/* A step's outcome takes the place of its "running" row (matrix N2,
 * 2026-10-19: the copy's row stayed "running" above its own "done"); an
 * outcome with no step open is a row of its own. */
static void progress_add(HWND dlg, int state, const char *text)
{
    char line[600];
    static const char *const mark[] = {
        "running", "done", "FAILED", "skipped", "left for you"
    };
    HWND lb = GetDlgItem(dlg, IDC_STEPLIST);
    int n;

    _snprintf(line, sizeof(line) - 1, "%s - %s", text,
              (state >= 0 && state <= 4) ? mark[state] : "");
    line[sizeof(line) - 1] = '\0';
    if (state != INST_STEP_BEGIN && stepOpen >= 0) {
        SendMessageA(lb, LB_DELETESTRING, (WPARAM)stepOpen, 0);
        n = (int)SendMessageA(lb, LB_INSERTSTRING, (WPARAM)stepOpen,
                              (LPARAM)line);
        stepOpen = -1;
    } else {
        n = (int)SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)line);
        if (state == INST_STEP_BEGIN) {
            stepOpen = n;
        }
    }
    SendMessageA(lb, LB_SETCURSEL, n, 0);
    if (state == INST_STEP_BEGIN) {
        stepsTotal++;
    } else {
        stepsDone++;
        if (stepsDone > stepsTotal) {
            stepsTotal = stepsDone;
        }
    }
    SendDlgItemMessageA(dlg, IDC_PROGRESSBAR, PBM_SETRANGE, 0,
                        MAKELPARAM(0, stepsTotal + 1));
    SendDlgItemMessageA(dlg, IDC_PROGRESSBAR, PBM_SETPOS, stepsDone, 0);
}

static void progress_line(HWND dlg, const char *text)
{
    HWND e = GetDlgItem(dlg, IDC_DETAIL);
    char *c = crlf(text);
    int n;

    if (c == NULL) {
        return;
    }
    n = GetWindowTextLengthA(e);
    if (n > 60000) {
        SetWindowTextA(e, "");
        n = 0;
    }
    SendMessageA(e, EM_SETSEL, n, n);
    SendMessageA(e, EM_REPLACESEL, FALSE, (LPARAM)c);
    free(c);
}

/* ---- the finish (record 26 section 5.9) ----------------------------------- */

typedef BOOL (WINAPI *PFN_OPT)(HANDLE, DWORD, PHANDLE);
typedef BOOL (WINAPI *PFN_LPV)(LPCSTR, LPCSTR, PLUID);
typedef BOOL (WINAPI *PFN_ATP)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD,
                               PTOKEN_PRIVILEGES, PDWORD);

/* "Restart now" (W5): ExitWindowsEx(EWX_REBOOT), on NT once the shutdown
 * privilege is enabled; never forced. A refusal is shown. */
static void restart_now(HWND owner)
{
    HMODULE adv;
    PFN_OPT openToken;
    PFN_LPV lookup;
    PFN_ATP adjust;
    HANDLE tok;
    TOKEN_PRIVILEGES tp;
    char text[256];

    if (!is_9x()) {
        adv = GetModuleHandleA("advapi32.dll");
        openToken = adv != NULL ? (PFN_OPT)GetProcAddress(adv,
                                                         "OpenProcessToken")
                                : NULL;
        lookup = adv != NULL ? (PFN_LPV)GetProcAddress(adv,
                                                      "LookupPrivilegeValueA")
                             : NULL;
        adjust = adv != NULL ? (PFN_ATP)GetProcAddress(adv,
                                                      "AdjustTokenPrivileges")
                             : NULL;
        if (openToken != NULL && lookup != NULL && adjust != NULL &&
            openToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES |
                      TOKEN_QUERY, &tok)) {
            memset(&tp, 0, sizeof(tp));
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (lookup(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid)) {
                adjust(tok, FALSE, &tp, 0, NULL, NULL);
            }
            CloseHandle(tok);
        }
    }
    /* 98 SE and ME after a Stor98 update: a shutdown and a fresh start,
     * not a warm restart (record 26 section 5.9). */
    if (!ExitWindowsEx(inst_eng_shutdown() ? EWX_SHUTDOWN : EWX_REBOOT, 0)) {
        _snprintf(text, sizeof(text) - 1, "Windows did not %s (error %lu). "
                  "Do it from the Start menu.", inst_eng_shutdown() ?
                  "shut down" : "restart", (unsigned long)GetLastError());
        text[sizeof(text) - 1] = '\0';
        MessageBoxA(owner, text, "xHCI98 Setup", MB_OK | MB_ICONWARNING);
    }
}

static void finish_fill(HWND dlg)
{
    int changed;
    int failed;
    int restart;
    int steps;
    const char *last = inst_final_text_cold(finalLine, inst_eng_shutdown());
    int cold = inst_eng_shutdown();

    inst_eng_flags(&changed, &failed, &restart, &steps);
    SetDlgItemTextA(dlg, IDC_F_LAST, ran ? last :
                    "Nothing was changed.");
    EnterCriticalSection(&textLock);
    {
        /* The other USB drivers' paragraph under the run's text, on every
         * install run (record 26 section 6.7.7). */
        static char para[4096];
        char *all;
        const char *run = runText.Buf != NULL ? runText.Buf : "";
        para[0] = '\0';
        if (!uninstallMode) {
            inst_eng_other_finish(para, sizeof(para));
        }
        all = (char *)malloc(strlen(run) + strlen(para) + 8);
        if (all != NULL) {
            strcpy(all, run);
            if (para[0] != '\0') {
                strcat(all, "\n\n");
                strcat(all, para);
            }
            set_edit(dlg, IDC_DETAIL, all);
            free(all);
        } else {
            set_edit(dlg, IDC_DETAIL, run);
        }
    }
    LeaveCriticalSection(&textLock);
    SetDlgItemTextA(dlg, IDC_F_NOW, inst_page_text(cold ? INST_PT_SHUT_NOW
                                                        : INST_PT_NOW));
    SetDlgItemTextA(dlg, IDC_F_LATER, inst_page_text(cold ? INST_PT_SHUT_LATER
                                                          : INST_PT_LATER));
    if (ran && changed) {
        ShowWindow(GetDlgItem(dlg, IDC_F_NOW), SW_SHOW);
        ShowWindow(GetDlgItem(dlg, IDC_F_LATER), SW_SHOW);
        /* Steps left: "later" preselected, the page saying do them first. */
        CheckRadioButton(dlg, IDC_F_NOW, IDC_F_LATER,
                         finalLine == INST_FINAL_UNFINISHED ? IDC_F_LATER
                                                            : IDC_F_NOW);
    } else {
        ShowWindow(GetDlgItem(dlg, IDC_F_NOW), SW_HIDE);
        ShowWindow(GetDlgItem(dlg, IDC_F_LATER), SW_HIDE);
    }
    buttons(PSWIZB_FINISH);
    EnableWindow(GetDlgItem(sheet, IDCANCEL), FALSE);
}

/* ---- the steps page (record 26 section 7) --------------------------------- */

static void steps_save(HWND dlg)
{
    OPENFILENAMEA ofn;
    char path[WIZ_PATH];
    const char *dir = inst_eng_tool_dir();
    char *c;
    HANDLE h;
    DWORD n;

    if (dir[0] != '\0') {
        _snprintf(path, sizeof(path) - 1, "%s\\XHCI98-STEPS.TXT", dir);
    } else {
        inst_copy(path, "XHCI98-STEPS.TXT", sizeof(path));
    }
    path[sizeof(path) - 1] = '\0';
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dlg;
    ofn.lpstrFilter = "Text files (*.TXT)\0*.TXT\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.lpstrDefExt = "TXT";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameA(&ofn)) {
        return;
    }
    c = crlf(inst_eng_steps_text());
    if (c == NULL) {
        return;
    }
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE ||
        !WriteFile(h, c, (DWORD)strlen(c), &n, NULL)) {
        MessageBoxA(dlg, "The steps could not be saved there.", "xHCI98 Setup",
                    MB_OK | MB_ICONWARNING);
    }
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
    free(c);
}

/* ---- the pages' procedure --------------------------------------------------- */

static int page_skipped(int page)
{
    int idx[WIZ_ROWS];

    switch (page) {
    case P_USB:
        return rows_for(P_USB, 0, idx, WIZ_ROWS) == 0 &&
               rows_for(P_USB, 1, idx, WIZ_ROWS) == 0;
    case P_TBS:
        return !tbChoice;
    case P_S98:
        return !s98Choice;
    case P_STEPS:
        return inst_eng_steps_text()[0] == '\0';
    default:
        return 0;
    }
}

/* Next or Back on a settings page with parts (record 26 section 5.4, "The
 * layout"): another part of the same page shown, its choices already
 * taken, and 1 so the sheet stays; 0 at the page's first or last part. */
static int part_step(HWND dlg, int page, int step)
{
    ROWPAGE *rp;
    int to;

    if (page != P_USB && page != P_TBS && page != P_S98) {
        return 0;
    }
    rp = page_rows(page);
    to = rp->Part + step;
    if (to < 0 || to >= rp->Parts) {
        return 0;
    }
    /* The focus to the button pressed first: the control that has it goes
     * with its part. */
    if (sheet != NULL) {
        SendMessageA(sheet, WM_APP_FOCUS, step < 0 ? 1 : 0, 0);
    }
    if (page == P_S98) {
        s98_page_fill(dlg, to);
    } else {
        settings_fill(dlg, page, to);
    }
    return 1;
}

static WIZ_DLGRET CALLBACK page_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    int page = GET_PAGE(dlg);
    NMHDR *nm;
    char *text;

    switch (msg) {
    case WM_INITDIALOG:
        page = (int)((PROPSHEETPAGEA *)lp)->lParam;
        SET_PAGE(dlg, page);
        pageWnd[page] = dlg;
        if (sheet == NULL) {
            sheet = GetParent(dlg);
        }
        if (page == P_WELCOME) {
            welcome_texts(dlg);
        }
        return TRUE;
    case WM_APP_STEP:
        text = (char *)lp;
        progress_add(dlg, (int)wp, text);
        free(text);
        return TRUE;
    case WM_APP_WAIT:
        text = (char *)lp;
        SetDlgItemTextA(dlg, IDC_STATUS, text);
        free(text);
        return TRUE;
    case WM_APP_LINE:
        text = (char *)lp;
        progress_line(dlg, text);
        free(text);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_U_AGREE:
            welcome_state(dlg);
            return TRUE;
        case IDC_C_FOLDER:
            if (DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_FOLDER), dlg,
                                folder_proc, 0) == IDOK) {
                comp_dirline(dlg);
            }
            return TRUE;
        case IDC_C_SETTINGS:
            settings_open(dlg);
            return TRUE;
        case IDC_C_TB:
            if (HIWORD(wp) == BN_CLICKED) {
                /* Stor98 follows its parent (owner, 2026-10-19): its own
                 * state with it, unticked without it (record 26 section
                 * 5.0, draft 35). */
                comp_s98_follow(dlg);
            }
            return TRUE;
        case IDC_C_S98:
            if (HIWORD(wp) == BN_CLICKED) {
                if (IsDlgButtonChecked(dlg, IDC_C_TB) == BST_CHECKED) {
                    s98Own = IsDlgButtonChecked(dlg, IDC_C_S98) ==
                             BST_CHECKED;
                }
                comp_vsd_follow(dlg);
            }
            return TRUE;
        case IDC_C_VSD:
            if (HIWORD(wp) == BN_CLICKED &&
                IsDlgButtonChecked(dlg, IDC_C_S98) == BST_CHECKED) {
                vsdOwn = IsDlgButtonChecked(dlg, IDC_C_VSD) == BST_CHECKED;
            }
            return TRUE;
        case IDC_C_UAS:
            if (HIWORD(wp) == BN_CLICKED) {
                uasOwn = IsDlgButtonChecked(dlg, IDC_C_UAS) == BST_CHECKED;
            }
            return TRUE;
        case IDC_C_DRIVERS:
            if (HIWORD(wp) == BN_CLICKED) {
                comp_usb_click(dlg);
            }
            return TRUE;
        case IDC_O_BASE + IDC_O_BOX:
        case IDC_O_BASE + IDC_O_STRIDE + IDC_O_BOX:
            if (HIWORD(wp) == BN_CLICKED) {
                oth_sync(dlg, LOWORD(wp) == IDC_O_BASE + IDC_O_BOX ?
                         INST_SET_FREE : INST_SET_NONFREE);
            }
            return TRUE;
        case IDC_S_ADVANCED:
            if (page == P_S98) {
                s98_page_leave(dlg);
                DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_ADVANCED), dlg,
                                s98_adv_proc, 0);
                return TRUE;
            }
            if (!settings_leave(dlg, page)) {
                return TRUE;
            }
            advPage = page;
            DialogBoxParamA(hInst, MAKEINTRESOURCEA(IDD_ADVANCED), dlg,
                            adv_proc, 0);
            return TRUE;
        case IDC_SAVE:
            steps_save(dlg);
            return TRUE;
        case IDC_DEVMGR:
            open_devmgr(dlg);
            return TRUE;
        default:
            if (HIWORD(wp) == CBN_SETFOCUS || HIWORD(wp) == CBN_SELCHANGE ||
                HIWORD(wp) == EN_SETFOCUS) {
                ROWPAGE *rp = page_rows(page);
                int i;
                for (i = 0; (page == P_USB || page == P_TBS) && i < rp->Count;
                     i++) {
                    if ((HWND)lp == rp->R[i].Choice) {
                        settings_note(dlg, page, i);
                    }
                }
            }
            break;
        }
        break;
    case WM_NOTIFY:
        nm = (NMHDR *)lp;
        switch (nm->code) {
        case PSN_SETACTIVE:
            if (page_skipped(page)) {
                SET_RESULT(dlg, -1);
                return TRUE;
            }
            next_text("&Next >");
            switch (page) {
            case P_WELCOME:
                welcome_state(dlg);
                break;
            case P_FOUND:
                found_fill(dlg);
                buttons(PSWIZB_BACK | PSWIZB_NEXT);
                break;
            case P_COMP:
                comp_fill(dlg);
                buttons(PSWIZB_BACK | PSWIZB_NEXT);
                break;
            case P_OTHER:
                oth_fill(dlg);
                buttons(PSWIZB_BACK | PSWIZB_NEXT);
                break;
            case P_USB:
            case P_TBS:
                /* Entered with Back: the last part (record 26 section
                 * 5.4, "The layout"). */
                settings_fill(dlg, page, navBack ? -1 : 0);
                buttons(PSWIZB_BACK | PSWIZB_NEXT);
                break;
            case P_S98:
                s98_page_fill(dlg, navBack ? -1 : 0);
                buttons(PSWIZB_BACK | PSWIZB_NEXT);
                break;
            case P_SUMMARY:
                summary_fill(dlg);
                buttons((uninstallMode ? 0 : PSWIZB_BACK) | PSWIZB_NEXT);
                next_text(uninstallMode ? "&Remove" : "&Install");
                break;
            case P_PROGRESS:
                SetDlgItemTextA(dlg, IDC_TEXT, inst_page_text(INST_PT_PROGRESS));
                buttons(0);
                if (!ran && !busy) {
                    start_job(uninstallMode ? JOB_UNINSTALL : JOB_RUN);
                }
                break;
            case P_STEPS:
                SetDlgItemTextA(dlg, IDC_TEXT, inst_page_text(INST_PT_STEPS));
                set_edit(dlg, IDC_DETAIL, inst_eng_steps_text());
                buttons(PSWIZB_NEXT);
                EnableWindow(GetDlgItem(sheet, IDCANCEL), FALSE);
                break;
            case P_FINISH:
                finish_fill(dlg);
                break;
            default:
                break;
            }
            SET_RESULT(dlg, 0);
            return TRUE;
        case PSN_WIZNEXT:
            if (page == P_S98) {
                s98_page_leave(dlg);
            }
            if (page == P_SUMMARY && !uninstallMode) {
                /* The Stor98 file update's message box on 98 SE and ME
                 * (record 26 section 5.0): Cancel stays, nothing written. */
                int ok = 0;
                if (is_9x() && s98Choice && inst_eng_s98_file_update()) {
                    char box[600];
                    _snprintf(box, sizeof(box) - 1, "%s\n\nOK goes on; "
                              "Cancel stays on this page.",
                              inst_page_text(INST_PT_S98_UNUSED));
                    box[sizeof(box) - 1] = '\0';
                    if (MessageBoxA(GetParent(dlg), box, "xHCI98 Setup",
                                    MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
                        SET_RESULT(dlg, -1);
                        return TRUE;
                    }
                    ok = 1;
                }
                inst_eng_s98_file_ok(ok);
            }
            if ((page == P_COMP && !comp_leave(dlg)) ||
                (page == P_OTHER && !oth_leave(dlg)) ||
                ((page == P_USB || page == P_TBS) &&
                 !settings_leave(dlg, page))) {
                SET_RESULT(dlg, -1);
                return TRUE;
            }
            if (part_step(dlg, page, 1)) {
                SET_RESULT(dlg, -1);
                return TRUE;
            }
            /* The direction a page is entered from: a departure's alone
             * (record 26 section 5.4, "The layout"). */
            navBack = 0;
            SET_RESULT(dlg, 0);
            return TRUE;
        case PSN_WIZBACK:
            if (page == P_S98) {
                s98_page_leave(dlg);
            }
            if ((page == P_USB || page == P_TBS) && !settings_leave(dlg, page)) {
                SET_RESULT(dlg, -1);
                return TRUE;
            }
            if (part_step(dlg, page, -1)) {
                SET_RESULT(dlg, -1);
                return TRUE;
            }
            navBack = 1;
            SET_RESULT(dlg, 0);
            return TRUE;
        case PSN_WIZFINISH:
            if (page == P_FINISH && ran &&
                IsWindowVisible(GetDlgItem(dlg, IDC_F_NOW)) &&
                IsDlgButtonChecked(dlg, IDC_F_NOW) == BST_CHECKED) {
                restart_now(dlg);
            }
            SET_RESULT(dlg, 0);
            return TRUE;
        case PSN_QUERYCANCEL:
            if (busy) {
                /* Record 26 section 5.8: the run stops at the next step. */
                InterlockedExchange(&cancelAsked, 1);
                SetDlgItemTextA(dlg, IDC_STATUS, "Cancel heard: the run stops "
                                "at the next step; what was done stays.");
                SET_RESULT(dlg, TRUE);
                return TRUE;
            }
            SET_RESULT(dlg, FALSE);
            return TRUE;
        case PSN_HELP:
            open_help(dlg);
            return TRUE;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

/* The sheet moved, never resized, so its frame lies in the work area (the
 * 43.11 finding: 7 at 120 DPI on an 800 by 600 screen); its top left kept
 * inside where it cannot all fit (record 26 section 5.0, draft 35). */
static void wiz_fit(HWND h)
{
    RECT w;
    RECT a;
    int x;
    int y;

    if (!GetWindowRect(h, &w) ||
        !SystemParametersInfoA(SPI_GETWORKAREA, 0, &a, 0)) {
        return;
    }
    x = w.left;
    y = w.top;
    if (x + (w.right - w.left) > a.right) {
        x = a.right - (w.right - w.left);
    }
    if (y + (w.bottom - w.top) > a.bottom) {
        y = a.bottom - (w.bottom - w.top);
    }
    if (x < a.left) {
        x = a.left;
    }
    if (y < a.top) {
        y = a.top;
    }
    if (x != w.left || y != w.top) {
        SetWindowPos(h, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE);
    }
}

/* The sheet's own messages: a job's end, the gate. */
static WNDPROC sheetProc;

static LRESULT CALLBACK sheet_sub(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_APP_FIT) {
        wiz_fit(h);
        return 0;
    }
    if (msg == WM_APP_DONE) {
        if ((int)wp == JOB_READ) {
            readDone = 1;
            if (pageWnd[P_WELCOME] != NULL) {
                welcome_state(pageWnd[P_WELCOME]);
            }
        } else {
            /* The run ended: on to the steps or the finish by itself. */
            PropSheet_SetWizButtons(sheet, PSWIZB_NEXT);
            PropSheet_PressButton(sheet, PSBTN_NEXT);
        }
        return 0;
    }
    if (msg == WM_APP_GATE) {
        return MessageBoxA(h, inst_page_text(INST_PT_GATE), "xHCI98 Setup",
                           MB_OKCANCEL | MB_ICONINFORMATION) == IDOK ? 1 : 0;
    }
    if (msg == WM_APP_FOCUS) {
        /* wParam 1: Back, a part of a settings page stepped back to. */
        HWND b = GetDlgItem(h, wp == 1 ? ID_SHEET_BACK : ID_SHEET_NEXT);

        if (b == NULL || !IsWindowVisible(b) || !IsWindowEnabled(b)) {
            b = GetDlgItem(h, ID_SHEET_FINISH);
        }
        if (b != NULL && IsWindowVisible(b) && IsWindowEnabled(b)) {
            SendMessageA(h, WM_NEXTDLGCTL, (WPARAM)b, TRUE);
            SendMessageA(h, DM_SETDEFID, (WPARAM)GetDlgCtrlID(b), 0);
        }
        return 0;
    }
    return CallWindowProcA(sheetProc, h, msg, wp, lp);
}

static int CALLBACK sheet_cb(HWND h, UINT msg, LPARAM lp)
{
    (void)lp;
    if (msg == PSCB_INITIALIZED) {
        sheet = h;
        sheetProc = GET_PROC(h);
        SET_PROC(h, sheet_sub);
        /* Windows' own dialogs, and the 98 SE and ME helper's, are owned
         * by the sheet (record 26 section 3.1, record 27 section 4.1). */
        inst_eng_owner(h);
        PostMessageA(h, WM_APP_FIT, 0, 0);
        if (!uninstallMode) {
            start_job(JOB_READ);
        }
    }
    return 0;
}

/* ---- the sheet -------------------------------------------------------------- */

static int run_wizard(void)
{
    /* Record 26 section 5.0: Welcome, Components, Summary, Progress,
     * (Steps), Finish; the settings in the Settings dialog. */
    static const int order[] = {
        P_WELCOME, P_COMP, P_OTHER, P_SUMMARY, P_PROGRESS, P_STEPS,
        P_FINISH
    };
    static const int uorder[] = { P_WELCOME, P_SUMMARY, P_PROGRESS, P_FINISH };
    static const int tmpl[P_COUNT] = {
        IDD_WELCOME, IDD_FOUND, IDD_COMPONENTS, IDD_SETTINGS, IDD_SETTINGS,
        IDD_SETTINGS, IDD_SUMMARY, IDD_PROGRESS, IDD_STEPS, IDD_FINISH,
        IDD_OTHER
    };
    static const char *const title[P_COUNT] = {
        "Welcome", "What was found", "Components", "USB settings",
        "Thunderbolt settings", "Stor98 settings", "Summary", "Progress",
        "Steps left for you", "Finish", "Other USB drivers"
    };
    PROPSHEETPAGEA pg[P_COUNT];
    HPROPSHEETPAGE hp[P_COUNT];
    PROPSHEETHEADERA h;
    const int *list = uninstallMode ? uorder : order;
    int n = uninstallMode ? 4 : (int)(sizeof(order) / sizeof(order[0]));
    int i;

    for (i = 0; i < n; i++) {
        int p = list[i];
        memset(&pg[i], 0, sizeof(pg[i]));
        pg[i].dwSize = sizeof(pg[i]);
        pg[i].dwFlags = PSP_USETITLE | PSP_HASHELP;
        pg[i].hInstance = hInst;
        pg[i].pszTemplate = MAKEINTRESOURCEA(uninstallMode && p == P_WELCOME ?
                                             IDD_UWELCOME : tmpl[p]);
        pg[i].pszTitle = title[p];
        pg[i].pfnDlgProc = page_proc;
        pg[i].lParam = p;
        hp[i] = CreatePropertySheetPageA(&pg[i]);
        if (hp[i] == NULL) {
            return INST_EXIT_REFUSED;
        }
    }
    memset(&h, 0, sizeof(h));
    h.dwSize = sizeof(h);
    h.dwFlags = PSH_WIZARD | PSH_USECALLBACK | PSH_USEICONID | PSH_HASHELP;
    h.hInstance = hInst;
    h.pszIcon = MAKEINTRESOURCEA(IDI_WIZ);
    h.pszCaption = uninstallMode ? "xHCI98 Uninstall" : "xHCI98 Setup";
    h.nPages = (UINT)n;
    h.phpage = hp;
    h.pfnCallback = sheet_cb;
    PropertySheetA(&h);
    if (!ended) {
        /* Closed before Install: exit 1 with nothing changed (record 26
         * section 10). */
        end_run(readCode >= 0 ? readCode : planCode >= 0 ? planCode
                                                         : INST_EXIT_REFUSED);
    }
    return exitCode;
}

/* ---- WOW64 (W4) -------------------------------------------------------------- */

typedef BOOL (WINAPI *PFN_ISWOW64)(HANDLE, PBOOL);

/* The 32-bit window under WOW64 starts the x64 one from its own directory
 * with its own arguments, waits for it and returns its exit code. -1 when
 * this is not WOW64 or the x64 image is not there (the engine's refusal
 * then says so). */
static int wow64_start(void)
{
#ifdef _WIN64
    return -1;
#else
    PFN_ISWOW64 isWow64;
    BOOL wow = FALSE;
    char self[WIZ_PATH];
    char cmd[WIZ_PATH * 4];
    const char *tail;
    char *slash;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = INST_EXIT_REFUSED;

    isWow64 = (PFN_ISWOW64)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                          "IsWow64Process");
    if (isWow64 == NULL || !isWow64(GetCurrentProcess(), &wow) || !wow) {
        return -1;
    }
    if (GetModuleFileNameA(NULL, self, sizeof(self)) == 0) {
        return -1;
    }
    slash = strrchr(self, '\\');
    if (slash == NULL) {
        return -1;
    }
    strcpy(slash + 1, inst_self_file(INST_ARCH_AMD64, INST_OWN_WINDOW));
    if (GetFileAttributesA(self) == 0xFFFFFFFFUL) {
        return -1;
    }
    /* The command line's tail: everything after the program's own name. */
    tail = GetCommandLineA();
    if (*tail == '"') {
        tail = strchr(tail + 1, '"');
        tail = tail != NULL ? tail + 1 : "";
    } else {
        while (*tail != '\0' && *tail != ' ') {
            tail++;
        }
    }
    _snprintf(cmd, sizeof(cmd) - 1, "\"%s\"%s", self, tail);
    cmd[sizeof(cmd) - 1] = '\0';
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(self, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si,
                        &pi)) {
        return -1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
#endif
}

/* ---- the self-test (record 26 section 11) ------------------------------------ */

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

/* 1 when a static control holds its text whole, wrapped at its width in
 * its font. */
static int fits(HWND ctl)
{
    char text[2048];
    RECT r;
    RECT need;
    HDC dc;
    HFONT f;
    HFONT old;

    if (ctl == NULL) {
        return 0;
    }
    GetWindowTextA(ctl, text, sizeof(text));
    if (text[0] == '\0') {
        return 1;
    }
    GetClientRect(ctl, &r);
    need = r;
    dc = GetDC(ctl);
    f = (HFONT)SendMessageA(ctl, WM_GETFONT, 0, 0);
    old = f != NULL ? (HFONT)SelectObject(dc, f) : NULL;
    DrawTextA(dc, text, -1, &need, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    if (old != NULL) {
        SelectObject(dc, old);
    }
    ReleaseDC(ctl, dc);
    return need.bottom - need.top <= r.bottom - r.top &&
           need.right - need.left <= r.right - r.left + 1;
}

static WIZ_DLGRET CALLBACK st_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)dlg;
    (void)msg;
    (void)wp;
    (void)lp;
    return FALSE;
}

static void st_page(HWND parent, int tmpl, const int *ids, const int *texts,
                    int n, const char *what)
{
    HWND d = CreateDialogParamA(hInst, MAKEINTRESOURCEA(tmpl), parent, st_proc,
                                0);

    /* The page templates are children: under the hidden parent here. */
    char msg[128];
    int i;

    st(d != NULL, what);
    if (d == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        SetDlgItemTextA(d, ids[i], inst_page_text(texts[i]));
        _snprintf(msg, sizeof(msg) - 1, "%s: text %d fits its control", what,
                  texts[i]);
        msg[sizeof(msg) - 1] = '\0';
        st(fits(GetDlgItem(d, ids[i])), msg);
    }
    DestroyWindow(d);
}

/* The Components page as 98 SE lays it out (comp_layout, record 26
 * section 5.3, draft 17): the rows one below another, none overlapping,
 * the last inside the page. */
static void st_comp_layout(HWND parent)
{
    static const int order[] = {
        IDC_C_DRIVERS, IDC_C_UAS, IDC_C_UAS_T, IDC_C_STOR, IDC_C_STOR_T,
        IDC_C_TB, IDC_C_TB_T, IDC_C_S98, IDC_C_S98_T, IDC_C_VSD, IDC_C_TRAY,
        IDC_C_FAST, IDC_C_DIRLINE, IDC_C_SETTINGS
    };
    HWND d = CreateDialogParamA(hInst, MAKEINTRESOURCEA(IDD_COMPONENTS),
                                parent, st_proc, 0);
    RECT prev;
    RECT r;
    RECT page;
    POINT pt;
    size_t i;
    int ok = 1;
    int saved = storShown;

    st(d != NULL, "the components, as 98 SE lays them out");
    if (d == NULL) {
        return;
    }
    storShown = 1;
    compLaidOut = 0;
    comp_layout(d);
    GetClientRect(d, &page);
    memset(&prev, 0, sizeof(prev));
    for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        GetWindowRect(GetDlgItem(d, order[i]), &r);
        pt.x = r.left;
        pt.y = r.top;
        ScreenToClient(d, &pt);
        r.bottom = pt.y + (r.bottom - r.top);
        r.top = pt.y;
        if ((i > 0 && r.top < prev.bottom) || r.bottom > page.bottom) {
            printf("components on 98 SE: control %d at %ld..%ld, the one "
                   "before ends at %ld, the page at %ld\n", order[i],
                   (long)r.top, (long)r.bottom, (long)prev.bottom,
                   (long)page.bottom);
            ok = 0;
        }
        prev = r;
    }
    st(ok, "the components on 98 SE: no row over another, all on the page");
    storShown = saved;
    compLaidOut = 0;
    DestroyWindow(d);
}

/* A control's rectangle in its dialog's client coordinates. */
static RECT st_rect(HWND d, HWND c)
{
    RECT r;
    POINT pt;

    GetWindowRect(c, &r);
    pt.x = r.left;
    pt.y = r.top;
    ScreenToClient(d, &pt);
    r.right = pt.x + (r.right - r.left);
    r.bottom = pt.y + (r.bottom - r.top);
    r.left = pt.x;
    r.top = pt.y;
    return r;
}

/* A drop-down's entries (record 26 section 5.4, "The layout"): each
 * within the closed box less the allowance the open list adds to the
 * longest (combo_fit), so the open list is the box's width, and that it
 * is. */
static void st_combo(HWND combo, const char *what)
{
    char text[320];
    char msg[200];
    HFONT f = (HFONT)SendMessageA(combo, WM_GETFONT, 0, 0);
    HDC dc = GetDC(combo);
    HFONT old = f != NULL ? (HFONT)SelectObject(dc, f) : NULL;
    int dropped = (int)SendMessageA(combo, CB_GETDROPPEDWIDTH, 0, 0);
    int n = (int)SendMessageA(combo, CB_GETCOUNT, 0, 0);
    int longest = 0;
    int measured = 0;
    int room;
    RECT r;
    SIZE sz;
    int k;

    /* Every entry measured, or the checks below prove nothing. */
    for (k = 0; k < n; k++) {
        int len = (int)SendMessageA(combo, CB_GETLBTEXTLEN, (WPARAM)k, 0);
        if (len <= 0 || len >= (int)sizeof(text) ||
            (int)SendMessageA(combo, CB_GETLBTEXT, (WPARAM)k,
                              (LPARAM)text) != len ||
            !GetTextExtentPoint32A(dc, text, len, &sz)) {
            continue;
        }
        measured++;
        if (sz.cx > longest) {
            longest = sz.cx;
        }
    }
    if (old != NULL) {
        SelectObject(dc, old);
    }
    _snprintf(msg, sizeof(msg) - 1, "%s: every entry measured (%d of %d)",
              what, measured, n);
    msg[sizeof(msg) - 1] = '\0';
    st(n > 0 && measured == n, msg);
    ReleaseDC(combo, dc);
    GetWindowRect(combo, &r);
    room = (r.right - r.left) - combo_extra(combo);
    _snprintf(msg, sizeof(msg) - 1, "%s: every entry (the longest %d) within "
              "the closed box (%d)", what, longest, room);
    msg[sizeof(msg) - 1] = '\0';
    st(n > 0 && longest <= room, msg);
    _snprintf(msg, sizeof(msg) - 1, "%s: the open list (%d) is the closed "
              "box's width (%ld)", what, dropped, (long)(r.right - r.left));
    msg[sizeof(msg) - 1] = '\0';
    st(dropped == r.right - r.left, msg);
}

/* One part of a settings page as laid out (record 26 section 5.4, "The
 * layout"): every text whole, nothing over anything else or off the page,
 * the note six lines at least, each closed box across the column and its
 * open list as wide as its longest entry. */
static void st_lay_part(HWND d, const ROWPAGE *rp, const char *what)
{
    HWND note = GetDlgItem(d, IDC_S_NOTE);
    RECT page;
    RECT head = st_rect(d, GetDlgItem(d, IDC_S_HEAD));
    RECT n = st_rect(d, note);
    RECT adv = st_rect(d, GetDlgItem(d, IDC_S_ADVANCED));
    RECT nc;
    RECT prev = head;
    TEXTMETRICA tm;
    HDC dc;
    HFONT f;
    HFONT old;
    char msg[160];
    int ok = 1;
    int i;

    GetClientRect(d, &page);
    st(fits(GetDlgItem(d, IDC_S_HEAD)), "the head fits");
    st(rp->Count >= 1 && rp->Count <= WIZ_LAY_PER_PART, "one to three rows a "
       "part");
    for (i = 0; i < rp->Count; i++) {
        const ROWCTL *rc = &rp->R[i];
        RECT l = st_rect(d, rc->Label);
        RECT w = st_rect(d, rc->Now);
        RECT c = st_rect(d, rc->Choice);
        char cls[16];
        _snprintf(msg, sizeof(msg) - 1, "%s, row %d: the label fits", what, i);
        msg[sizeof(msg) - 1] = '\0';
        st(fits(rc->Label), msg);
        _snprintf(msg, sizeof(msg) - 1, "%s, row %d: the now text fits", what,
                  i);
        msg[sizeof(msg) - 1] = '\0';
        st(fits(rc->Now), msg);
        if (l.top < prev.bottom || l.right > w.left || l.bottom > c.top ||
            w.bottom > c.top || w.right > head.right || c.right > head.right ||
            l.left != head.left || c.left != head.left) {
            printf("%s, row %d: label %ld..%ld, now %ld..%ld (x %ld..%ld), "
                   "choice %ld..%ld (x %ld..%ld), above it %ld\n", what, i,
                   (long)l.top, (long)l.bottom, (long)w.top, (long)w.bottom,
                   (long)w.left, (long)w.right, (long)c.top, (long)c.bottom,
                   (long)c.left, (long)c.right, (long)prev.bottom);
            ok = 0;
        }
        GetClassNameA(rc->Choice, cls, sizeof(cls));
        if (lstrcmpiA(cls, "COMBOBOX") == 0) {
            if (c.right != head.right) {
                printf("%s, row %d: the closed box ends at %ld, the column at "
                       "%ld\n", what, i, (long)c.right, (long)head.right);
                ok = 0;
            }
            _snprintf(msg, sizeof(msg) - 1, "%s, row %d", what, i);
            msg[sizeof(msg) - 1] = '\0';
            st_combo(rc->Choice, msg);
        }
        prev = c;
    }
    if (n.top < prev.bottom || n.bottom > page.bottom || n.left != head.left ||
        adv.left < n.right || adv.right > page.right ||
        adv.bottom > page.bottom || adv.top < prev.bottom) {
        printf("%s: the note %ld..%ld (x %ld..%ld), the last row ends at %ld, "
               "Advanced x %ld..%ld y %ld..%ld, the page %ldx%ld\n", what,
               (long)n.top, (long)n.bottom, (long)n.left, (long)n.right,
               (long)prev.bottom, (long)adv.left, (long)adv.right,
               (long)adv.top, (long)adv.bottom, (long)page.right,
               (long)page.bottom);
        ok = 0;
    }
    _snprintf(msg, sizeof(msg) - 1, "%s: no control over another, all on the "
              "page", what);
    msg[sizeof(msg) - 1] = '\0';
    st(ok, msg);
    dc = GetDC(note);
    f = (HFONT)SendMessageA(note, WM_GETFONT, 0, 0);
    old = f != NULL ? (HFONT)SelectObject(dc, f) : NULL;
    GetTextMetricsA(dc, &tm);
    if (old != NULL) {
        SelectObject(dc, old);
    }
    ReleaseDC(note, dc);
    /* The edit control's formatting rectangle: where its text is drawn. */
    SendMessageA(note, EM_GETRECT, 0, (LPARAM)&nc);
    _snprintf(msg, sizeof(msg) - 1, "%s: the note shows six lines (%ld px, "
              "a line %ld)", what, (long)(nc.bottom - nc.top),
              (long)tm.tmHeight);
    msg[sizeof(msg) - 1] = '\0';
    st(nc.bottom - nc.top >= 6 * tm.tmHeight, msg);
}

/* The made-up Stor98 rows: both systems, set and not set. */
static int stS98Nt;
static int stS98State;

static int st_s98_row(int i, INST_S98ROW *r)
{
    const INST_S98_VALUE *v = &instS98Values[i];
    char now[32];

    memset(r, 0, sizeof(*r));
    if (!inst_s98_applies(i, stS98Nt)) {
        return 0;
    }
    r->Value = i;
    r->Asked = v->Asked;
    r->Name = stS98Nt ? v->KeyNt : v->Name9x;
    r->State = stS98State;
    if (v->Kind == INST_S98_WORDS) {
        inst_copy(now, v->Words[0], sizeof(now));
    } else {
        _snprintf(now, sizeof(now) - 1, "%lu", (unsigned long)v->Hi);
        now[sizeof(now) - 1] = '\0';
    }
    inst_copy(r->Now, stS98State > 0 ? now : "", sizeof(r->Now));
    inst_s98_now_text(i, stS98Nt, 0, stS98State, stS98State > 0 ? now : NULL,
                      r->NowText, sizeof(r->NowText));
    return 1;
}

/* The made-up rows of the drivers' values: every value of the table for
 * the pages and Advanced, in one form (0 mixed, 1 set machine-wide, 2
 * greyed: the values another system has alone). */
static void st_rows(int form)
{
    int i;
    int k;

    rowCount = 0;
    for (i = 0; i < instValueCount && rowCount < INST_MAX_VALUES; i++) {
        INST_ROW *r = &rows[rowCount];
        if ((instValues[i].Inf != INST_INF_XHCI &&
             instValues[i].Inf != INST_INF_CM) ||
            (form == 2 && inst_value_only(i) == NULL)) {
            continue;
        }
        memset(r, 0, sizeof(*r));
        r->Value = i;
        r->Asked = instValues[i].Asked != 0;
        r->Mask = instValues[i].Settable | instValues[i].Asked;
        r->Def = instValues[i].Def;
        r->Same = form != 0;
        r->InForce = r->Def;
        r->MwState = form == 1;
        r->MwValue = 0xFFFFFFFFUL;
        r->NowCount = 2;
        for (k = 0; k < 2; k++) {
            r->Now[k].State = 1;
            r->Now[k].Value = (INST_U32)k;
            r->Now[k].Desc = "Intel(R) USB 3.0 eXtensible Host "
                             "Controller";
        }
        r->Greyed = form == 2;
        rowCount++;
    }
}

/* A choice made on a part kept across Next and Back (record 26 section
 * 5.4, "The layout"): the first row's first entry that writes chosen, the
 * page left to its last part and back to its first. */
static void st_keep(HWND d, int page)
{
    ROWPAGE *rp = page_rows(page);
    const INST_ROW *r;
    int n;
    int k;

    settings_fill(d, page, 0);
    if (rp->Count == 0) {
        return;
    }
    r = &rows[rp->R[0].Row];
    if (r->Greyed || instValues[r->Value].Hi != 0) {
        rows_clear(rp);
        return;
    }
    n = (int)SendMessageA(rp->R[0].Choice, CB_GETCOUNT, 0, 0);
    for (k = 0; k < n; k++) {
        INST_U32 data = (INST_U32)(size_t)SendMessageA(rp->R[0].Choice,
                                                        CB_GETITEMDATA,
                                                        (WPARAM)k, 0);
        if (data != 0xFFFFFFFFUL && choice_writes(r, 0, data)) {
            break;
        }
    }
    if (k < n) {
        SendMessageA(rp->R[0].Choice, CB_SETCURSEL, (WPARAM)k, 0);
        st(settings_leave(d, page), "the part left");
        settings_fill(d, page, -1);
        st(settings_leave(d, page), "the last part left");
        settings_fill(d, page, 0);
        st(choiceSet[r->Value] &&
           (int)SendMessageA(rp->R[0].Choice, CB_GETCURSEL, 0, 0) == k,
           "a choice made on the first part kept after Next and Back");
        choiceSet[r->Value] = 0;
    }
    rows_clear(rp);
}

/* The Advanced dialog with each of a page's advanced values shown in
 * turn: its list's entries within the closed box, the open list as wide
 * as the longest. */
static void st_advanced(HWND parent, int page, const char *form)
{
    HWND a;
    char what[96];
    int i;

    advPage = page;
    a = CreateDialogParamA(hInst, MAKEINTRESOURCEA(IDD_ADVANCED), parent,
                           adv_proc, 0);
    st(a != NULL, "the Advanced dialog");
    if (a == NULL) {
        return;
    }
    for (i = 0; i < advCount; i++) {
        adv_show(a, i);
        if (instValues[rows[advIdx[i]].Value].Hi != 0) {
            continue;
        }
        _snprintf(what, sizeof(what) - 1, "Advanced, %s, %s", form,
                  instValues[rows[advIdx[i]].Value].Name);
        what[sizeof(what) - 1] = '\0';
        st_combo(GetDlgItem(a, IDC_A_CHOICE), what);
    }
    DestroyWindow(a);
}

/* Every settings page laid out (record 26 sections 5.4 and 11), every
 * part, at the IDD_SETTINGS template's size and at the sheet's (the
 * Components template's): the drivers' pages with every asked value of
 * the table, mixed, set machine-wide and greyed, and Stor98's with its
 * asked rows on both systems, set and not set; a choice kept across
 * parts; the Advanced dialog's lists. */
static void st_settings_layout(HWND parent)
{
    static const char *const form[] = { "mixed", "machine-wide", "greyed" };
    static const char *const size[] = { "the template", "the sheet" };
    static const int pages[] = { P_USB, P_TBS };
    HWND d = CreateDialogParamA(hInst, MAKEINTRESOURCEA(IDD_SETTINGS), parent,
                                st_proc, 0);
    HWND c = CreateDialogParamA(hInst, MAKEINTRESOURCEA(IDD_COMPONENTS),
                                parent, st_proc, 0);
    RECT win;
    RECT cli;
    RECT big;
    char what[128];
    int idx[WIZ_ROWS];
    int s;
    int v;
    int p;
    int i;
    int k;
    int seen;

    st(d != NULL && c != NULL, "the settings page for its layout");
    if (d == NULL || c == NULL) {
        return;
    }
    GetClientRect(c, &big);
    DestroyWindow(c);
    for (s = 0; s < 2; s++) {
        if (s == 1) {
            /* The sheet gives every page its largest template's size. */
            GetWindowRect(d, &win);
            GetClientRect(d, &cli);
            SetWindowPos(d, NULL, 0, 0, big.right + (win.right - win.left) -
                         cli.right, big.bottom + (win.bottom - win.top) -
                         cli.bottom, SWP_NOMOVE | SWP_NOZORDER |
                         SWP_NOACTIVATE);
        }
        for (v = 0; v < 3; v++) {
            st_rows(v);
            for (p = 0; p < 2; p++) {
                ROWPAGE *rp = page_rows(pages[p]);
                int n = rows_for(pages[p], 0, idx, WIZ_ROWS);
                settings_fill(d, pages[p], 0);
                seen = 0;
                for (k = 0; k < rp->Parts; k++) {
                    settings_fill(d, pages[p], k);
                    _snprintf(what, sizeof(what) - 1, "%s settings at %s, "
                              "%s, part %d of %d", pages[p] == P_USB ? "USB" :
                              "Thunderbolt", size[s], form[v], k + 1,
                              rp->Parts);
                    what[sizeof(what) - 1] = '\0';
                    if (n > 0) {
                        st_lay_part(d, rp, what);
                    }
                    for (i = 0; i < rp->Count; i++) {
                        st(seen + i < n && rp->R[i].Row == idx[seen + i],
                           "the rows in their order across the parts");
                    }
                    seen += rp->Count;
                }
                st(seen == n, "every row in one part");
                settings_fill(d, pages[p], -1);
                st(rp->Part == rp->Parts - 1, "entered by Back: the last "
                   "part");
                rows_clear(rp);
                st_keep(d, pages[p]);
                if (s == 0) {
                    st_advanced(parent, pages[p], form[v]);
                }
            }
        }
        rowCount = 0;
        s98RowFn = st_s98_row;
        for (v = 0; v < 4; v++) {
            ROWPAGE *rp = page_rows(P_S98);
            stS98Nt = v & 1;
            stS98State = v >> 1;
            s98_page_fill(d, 0);
            for (k = 0; k < rp->Parts; k++) {
                s98_page_fill(d, k);
                _snprintf(what, sizeof(what) - 1, "Stor98's settings at %s, "
                          "%s, %s, part %d of %d", size[s], stS98Nt ?
                          "2000 to 7" : "98 SE and ME", stS98State ? "set" :
                          "not set", k + 1, rp->Parts);
                what[sizeof(what) - 1] = '\0';
                st_lay_part(d, rp, what);
            }
            rows_clear(rp);
        }
        s98RowFn = inst_eng_s98_row;
    }
    DestroyWindow(d);
}

static int selftest(void)
{
    static const int wIds[] = { IDC_TEXT };
    static const int wTexts[] = { INST_PT_WELCOME };
    static const int uIds[] = { IDC_TITLE, IDC_TEXT, IDC_U_AGREE };
    static const int uTexts[] = { INST_PT_UN_TITLE, INST_PT_UN_WARN,
                                  INST_PT_UN_AGREE };
    static const int fIds[] = { IDC_C_DIR_L, IDC_C_DIR_T };
    static const int fTexts[] = { INST_PT_DIR, INST_PT_DIR_9X };
    static const int cIds[] = {
        IDC_C_DRIVERS, IDC_C_DRIVERS_T, IDC_C_TB, IDC_C_TB_T, IDC_C_S98,
        IDC_C_S98_T, IDC_C_TRAY, IDC_C_FAST, IDC_C_UAS, IDC_C_UAS_T,
        IDC_C_STOR, IDC_C_VSD
    };
    static const int cTexts[] = {
        INST_PT_DRIVERS, INST_PT_DRIVERS_T, INST_PT_TB, INST_PT_TB_T,
        INST_PT_S98, INST_PT_S98_T, INST_PT_TRAY, INST_PT_FAST, INST_PT_UAS,
        INST_PT_UAS_T, INST_PT_STOR, INST_PT_VSD_OTHER
    };
    static const int s3Ids[] = { IDC_S_HEAD };
    static const int s3Texts[] = { INST_PT_S98SET };
    static const int n2Ids[] = { IDC_F_NOW, IDC_F_LATER };
    static const int n2Texts[] = { INST_PT_SHUT_NOW, INST_PT_SHUT_LATER };
    static const int c2Ids[] = { IDC_C_TB_T };
    static const int c2Texts[] = { INST_PT_TB_ABSENT };
    static const int c3Ids[] = { IDC_C_DRIVERS_T, IDC_C_DRIVERS_T };
    static const int c3Texts[] = { INST_PT_DRIVERS_T8, INST_PT_DRIVERS_OFF };
    static const int w2Ids[] = { IDC_TEXT };
    static const int w2Texts[] = { INST_PT_WELCOME_CM };
    static const int u2Ids[] = { IDC_TITLE, IDC_TEXT, IDC_U_AGREE };
    static const int u2Texts[] = { INST_PT_UN_TITLE_CM, INST_PT_UN_WARN_CM,
                                   INST_PT_UN_AGREE_CM };
    static const int sIds[] = { IDC_S_HEAD, IDC_S_NOTE };
    static const int sTexts[] = { INST_PT_USB, INST_PT_TB_LATER };
    static const int s2Ids[] = { IDC_S_HEAD };
    static const int s2Texts[] = { INST_PT_TBSET };
    static const int yIds[] = { IDC_TEXT };
    static const int yTexts[] = { INST_PT_SUMMARY };
    static const int y2Ids[] = { IDC_TEXT };
    static const int y2Texts[] = { INST_PT_UN_SUMMARY };
    static const int pIds[] = { IDC_TEXT };
    static const int pTexts[] = { INST_PT_PROGRESS };
    static const int tIds[] = { IDC_TEXT };
    static const int tTexts[] = { INST_PT_STEPS };
    static const int nIds[] = { IDC_F_NOW, IDC_F_LATER };
    static const int nTexts[] = { INST_PT_NOW, INST_PT_LATER };
    static const int aIds[] = { IDC_A_HEAD };
    static const int aTexts[] = { INST_PT_ADVANCED };
    char *argv1[3];
    INST_OPTS o;
    char err[128];
    HWND parent;
    int i;

    for (i = 0; i < instPageTextCount; i++) {
        const INST_PAGE_TEXT *t = &instPageTexts[i];
        st(t->Text != NULL && t->Text[0] != '\0', "a page text is there");
        if (t->Width != 0 &&
            inst_text_lines(t->Text, inst_page_cols(t->Width)) >
            inst_page_rows(t->Height + 2)) {
            printf("page text %d: %lu lines at %lu columns, room for %lu\n",
                   t->Id, inst_text_lines(t->Text, inst_page_cols(t->Width)),
                   inst_page_cols(t->Width), inst_page_rows(t->Height + 2));
            st(0, "a page text fits by count");
        }
    }
    argv1[0] = "x";
    argv1[1] = "/yes";
    st(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 &&
       inst_window_refusal(&o, 2, argv1) != NULL, "/yes is the console's");
    argv1[1] = "/console";
    st(inst_window_refusal(&o, 2, argv1) != NULL, "/console is INSTALL.BAT's");
    argv1[1] = "/uninstall";
    st(inst_parse_args(2, argv1, &o, err, sizeof(err)) == 0 &&
       inst_window_refusal(&o, 2, argv1) == NULL, "/uninstall is the window's");
    InitCommonControls();
    parent = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 640, 480, NULL,
                             NULL, hInst, NULL);
    if (parent == NULL) {
        printf("No window could be made (no desktop here): the pure texts "
               "were checked alone.\n");
    } else {
        st_page(parent, IDD_WELCOME, wIds, wTexts, 1, "the welcome");
        st_page(parent, IDD_UWELCOME, uIds, uTexts, 3, "the uninstall's "
                "welcome");
        st_page(parent, IDD_FOLDER, fIds, fTexts, 2, "the tools folder");
        st_page(parent, IDD_COMPONENTS, cIds, cTexts, 12, "the components");
        st_comp_layout(parent);
        st_page(parent, IDD_SETTINGS, s3Ids, s3Texts, 1, "Stor98's settings");
        st_page(parent, IDD_FINISH, n2Ids, n2Texts, 2, "the finish, a "
                "shutdown");
        st_page(parent, IDD_COMPONENTS, c2Ids, c2Texts, 1, "the components, "
                "no controller");
        st_page(parent, IDD_COMPONENTS, c3Ids, c3Texts, 2, "the components, "
                "the connection manager alone");
        st_page(parent, IDD_WELCOME, w2Ids, w2Texts, 1, "the welcome, the "
                "connection manager alone");
        st_page(parent, IDD_UWELCOME, u2Ids, u2Texts, 3, "the uninstall's "
                "welcome, the connection manager alone");
        st_page(parent, IDD_SETTINGS, sIds, sTexts, 2, "the USB settings");
        st_page(parent, IDD_SETTINGS, s2Ids, s2Texts, 1, "the Thunderbolt "
                "settings");
        st_page(parent, IDD_SUMMARY, yIds, yTexts, 1, "the summary");
        st_page(parent, IDD_SUMMARY, y2Ids, y2Texts, 1, "the uninstall's "
                "summary");
        st_page(parent, IDD_PROGRESS, pIds, pTexts, 1, "the progress");
        st_page(parent, IDD_STEPS, tIds, tTexts, 1, "the steps");
        st_page(parent, IDD_FINISH, nIds, nTexts, 2, "the finish");
        st_page(parent, IDD_ADVANCED, aIds, aTexts, 1, "the Advanced dialog");
        st_settings_layout(parent);
        DestroyWindow(parent);
    }
    printf("%d checks, %d failures\n", stChecks, stFailures);
    return stFailures == 0 ? 0 : 1;
}

/* A refusal before any page: printed when standard output goes to a file
 * or a pipe (selftest.cmd's switch refusals, a script), else shown. */
static void say(const char *text)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD type = (h != NULL && h != INVALID_HANDLE_VALUE) ? GetFileType(h)
                                                          : FILE_TYPE_UNKNOWN;

    if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE ||
        type == FILE_TYPE_CHAR) {
        printf("xhciwiz: %s\n", text);
        fflush(stdout);
        return;
    }
    MessageBoxA(NULL, text, "xHCI98 Setup", MB_OK | MB_ICONSTOP);
}

/* ---- WinMain ------------------------------------------------------------------- */

int WINAPI WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmdLine, int show)
{
    INST_FRONT f;
    char err[256];
    int r;

    (void)prev;
    (void)cmdLine;
    (void)show;
    hInst = hi;
    gArgc = __argc;
    gArgv = __argv;
    InitializeCriticalSection(&textLock);
    memset(&f, 0, sizeof(f));
    f.Line = wiz_line;
    f.Ask = wiz_ask;
    f.Progress = wiz_progress;
    f.Waiting = wiz_waiting;
    inst_eng_init(&f, inst_self_file(INST_MY_ARCH, INST_OWN_WINDOW),
                  inst_self_file(INST_ARCH_AMD64, INST_OWN_WINDOW), 1);
    if (inst_eng_parse(gArgc, gArgv, err, sizeof(err)) != 0) {
        say(err);
        return INST_EXIT_REFUSED;
    }
    opts = inst_eng_opts();
    if (opts->SelfTest) {
        return selftest();
    }
    /* The switches first, whatever the architecture: a refusal is said
     * here, never by the x64 window started below. */
    refusal = inst_window_refusal(opts, gArgc, gArgv);
    if (refusal != NULL) {
        say(refusal);
        return INST_EXIT_REFUSED;
    }
    r = wow64_start();
    if (r >= 0) {
        return r;
    }
    uninstallMode = opts->Uninstall;
    InitCommonControls();
    if (uninstallMode) {
        /* Record 26 section 9.2: a run from the install directory hands
         * the uninstall to a temporary copy before any page is shown. */
        sink = &readText;
        r = inst_eng_begin(gArgc, gArgv);
        if (r < 0) {
            r = inst_eng_read();
        }
        if (r < 0) {
            r = inst_eng_un_route();
            if (r >= 0) {
                exitCode = inst_eng_end(r, &finalLine);
                ended = 1;
                return exitCode;
            }
        }
        readCode = r;
    }
    trayChoice = !opts->NoTray;
    return run_wizard();
}
