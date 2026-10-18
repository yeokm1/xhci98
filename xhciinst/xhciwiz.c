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

#define JOB_READ            1
#define JOB_RUN             2
#define JOB_UNINSTALL       3

/* The pages (record 26 section 5; section 9 for the uninstall's). */
#define P_WELCOME           0
#define P_FOUND             1
#define P_COMP              2
#define P_USB               3
#define P_TBS               4
#define P_SUMMARY           5
#define P_PROGRESS          6
#define P_STEPS             7
#define P_FINISH            8
#define P_COUNT             9

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
static int trayChoice = 1;
static int trayHere;
static char dirChoice[WIZ_PATH];
static char dirNow[WIZ_PATH];

/* The settings chosen on the pages and in Advanced (record 26 section
 * 5.4): set, and the value; handed to the engine at Install. */
static int choiceSet[INST_MAX_VALUES];
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
            inst_eng_tb_state(&tbHave, &tbPresent, &tbDefault, &tbInstalled);
            tbChoice = tbHave && (opts->Thunderbolt || tbDefault);
            inst_eng_tb_choose(tbChoice);
            EnterCriticalSection(&textLock);
            sink = &planText;
            LeaveCriticalSection(&textLock);
            planCode = inst_eng_plan();
            inst_eng_dir_default(dirChoice, sizeof(dirChoice), dirNow,
                                 sizeof(dirNow), &trayHere);
            if (opts->To[0] != '\0') {
                inst_copy(dirChoice, opts->To, sizeof(dirChoice));
            }
            rowCount = inst_eng_rows(rows, INST_MAX_VALUES);
            /* /set preselects a value on its page (record 26 section 10);
             * from here on the pages say what is written. */
            for (i = 0; i < opts->SetCount; i++) {
                if (opts->SetIndex[i] >= 0 &&
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
        opts->SetCount = 0;
        for (i = 0; i < INST_MAX_VALUES; i++) {
            inst_eng_choose_value(i, choiceSet[i], choiceVal[i]);
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

static const char *system_name(char *out, size_t cap)
{
    OSVERSIONINFOA v;
    const char *name = "Windows";

    memset(&v, 0, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    GetVersionExA(&v);
    if (v.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
        name = v.dwMinorVersion >= 90 ? "Windows ME" : "Windows 98 SE";
    } else if (v.dwMajorVersion == 5 && v.dwMinorVersion == 0) {
        name = "Windows 2000";
    } else if (v.dwMajorVersion == 5 && v.dwMinorVersion == 1) {
        name = "Windows XP";
    } else if (v.dwMajorVersion == 5 && v.dwMinorVersion == 2) {
        name = "Windows XP x64";
    } else if (v.dwMajorVersion == 6 && v.dwMinorVersion == 0) {
        name = "Windows Vista";
    } else if (v.dwMajorVersion == 6 && v.dwMinorVersion == 1) {
        name = "Windows 7";
    }
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

static void welcome_texts(HWND dlg)
{
    char title[128];
    char sys[256];
    char logLine[WIZ_PATH + 16];

    _snprintf(title, sizeof(title) - 1, "%s %s",
              uninstallMode ? inst_page_text(INST_PT_UN_TITLE)
                            : "xHCI98 USB 3.x drivers, release",
              uninstallMode ? "" : XHCI_VER_STR);
    title[sizeof(title) - 1] = '\0';
    SetDlgItemTextA(dlg, IDC_TITLE, title);
    SetDlgItemTextA(dlg, IDC_TEXT, inst_page_text(uninstallMode ?
                                                  INST_PT_UN_WARN :
                                                  INST_PT_WELCOME));
    if (uninstallMode) {
        SetDlgItemTextA(dlg, IDC_U_AGREE, inst_page_text(INST_PT_UN_AGREE));
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

/* ---- the Found page ----------------------------------------------------- */

static void found_fill(HWND dlg)
{
    HWND lv = GetDlgItem(dlg, IDC_LIST);
    LVCOLUMNA c;
    LVITEMA it;
    INST_FOUND f;
    int i;
    int n = 0;

    SetDlgItemTextA(dlg, IDC_TEXT, inst_page_text(INST_PT_FOUND));
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
    EnterCriticalSection(&textLock);
    set_edit(dlg, IDC_DETAIL, planText.Buf != NULL ? planText.Buf : "");
    LeaveCriticalSection(&textLock);
}

/* The plan made again for the choices as they stand (record 26 section
 * 3.3): on the window's own thread, no job running. */
static void replan(void)
{
    if (busy) {
        return;
    }
    inst_eng_tb_choose(tbChoice);
    EnterCriticalSection(&textLock);
    tb_clear(&planText);
    sink = &planText;
    LeaveCriticalSection(&textLock);
    planCode = inst_eng_plan();
    rowCount = inst_eng_rows(rows, INST_MAX_VALUES);
}

/* ---- the Components page (record 26 section 5.3) ------------------------- */

static void comp_fill(HWND dlg)
{
    SetDlgItemTextA(dlg, IDC_C_DRIVERS, inst_page_text(INST_PT_DRIVERS));
    SetDlgItemTextA(dlg, IDC_C_DRIVERS_T, inst_page_text(INST_PT_DRIVERS_T));
    CheckDlgButton(dlg, IDC_C_DRIVERS, BST_CHECKED);
    EnableWindow(GetDlgItem(dlg, IDC_C_DRIVERS), FALSE);
    SetDlgItemTextA(dlg, IDC_C_TB, inst_page_text(INST_PT_TB));
    SetDlgItemTextA(dlg, IDC_C_TB_T, inst_page_text(tbPresent ?
                                                    INST_PT_TB_T :
                                                    INST_PT_TB_ABSENT));
    CheckDlgButton(dlg, IDC_C_TB, tbChoice ? BST_CHECKED : BST_UNCHECKED);
    ShowWindow(GetDlgItem(dlg, IDC_C_TB), tbHave ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg, IDC_C_TB_T), tbHave ? SW_SHOW : SW_HIDE);
    SetDlgItemTextA(dlg, IDC_C_TRAY, inst_page_text(INST_PT_TRAY));
    SetDlgItemTextA(dlg, IDC_C_TRAY_T, inst_page_text(INST_PT_TRAY_T));
    CheckDlgButton(dlg, IDC_C_TRAY, trayChoice ? BST_CHECKED : BST_UNCHECKED);
    ShowWindow(GetDlgItem(dlg, IDC_C_TRAY), trayHere ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(dlg, IDC_C_TRAY_T), trayHere ? SW_SHOW : SW_HIDE);
    SetDlgItemTextA(dlg, IDC_C_DIR_L, inst_page_text(INST_PT_DIR));
    SetDlgItemTextA(dlg, IDC_C_DIR, dirChoice);
    SetDlgItemTextA(dlg, IDC_C_DIR_T, is_9x() ? inst_page_text(INST_PT_DIR_9X)
                                              : "");
    SetDlgItemTextA(dlg, IDC_C_DIR_WHY, "");
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

/* Next on the Components page: the folder checked as a typed answer is,
 * the choices taken, and the plan made again when they changed. 1 go on. */
static int comp_leave(HWND dlg)
{
    char typed[WIZ_PATH];
    char checked[WIZ_PATH];
    const char *why;
    int tb;

    GetDlgItemTextA(dlg, IDC_C_DIR, typed, sizeof(typed));
    why = inst_eng_dir_check(typed, checked, sizeof(checked));
    if (why != NULL) {
        char text[WIZ_PATH];
        _snprintf(text, sizeof(text) - 1, "Not that folder: %s.", why);
        text[sizeof(text) - 1] = '\0';
        SetDlgItemTextA(dlg, IDC_C_DIR_WHY, text);
        return 0;
    }
    SetDlgItemTextA(dlg, IDC_C_DIR_WHY, "");
    inst_copy(dirChoice, checked, sizeof(dirChoice));
    trayChoice = IsDlgButtonChecked(dlg, IDC_C_TRAY) == BST_CHECKED;
    tb = tbHave && IsDlgButtonChecked(dlg, IDC_C_TB) == BST_CHECKED;
    if (tb != tbChoice) {
        tbChoice = tb;
        replan();
    }
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
        strncat(out, " The machine-wide value could not be read.",
                cap - strlen(out) - 1);
    }
    if (!r->Same) {
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
                      (b < 4UL && v->Choice[b] != NULL) ? v->Choice[b] :
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
                                 (LPARAM)"each controller keeps its own");
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
                  (b < 4UL && v->Choice[b] != NULL) ? v->Choice[b] : "",
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
    int Row;                /* index into rows[]                          */
    HWND Label;
    HWND Choice;
    HWND Now;
} ROWCTL;

typedef struct _ROWPAGE {
    int Count;
    ROWCTL R[WIZ_ROWS];
} ROWPAGE;

static ROWPAGE pageRows[2];     /* P_USB, P_TBS */

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

static HWND make_ctl(HWND dlg, const char *cls, const char *text, DWORD style,
                     int x, int y, int w, int h, int id, HFONT font)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             dlu_x(dlg, x), dlu_y(dlg, y), dlu_x(dlg, w),
                             dlu_y(dlg, h), dlg, (HMENU)(size_t)id, hInst,
                             NULL);

    if (c != NULL && font != NULL) {
        SendMessageA(c, WM_SETFONT, (WPARAM)font, FALSE);
    }
    return c;
}

/* The label of a row: the name and the one sentence of meaning. */
static void row_label(const INST_ROW *r, char *out, size_t cap)
{
    _snprintf(out, cap - 1, "%s - %s", instValues[r->Value].Name,
              instValues[r->Value].Meaning);
    out[cap - 1] = '\0';
}

/* One row of a settings page: a label of two lines, the choice under it,
 * the short "now" beside the choice. y in dialog units. */
#define WIZ_ROW_DLU 32
static void row_make(HWND dlg, ROWCTL *rc, int row, int y, HFONT font, int id)
{
    const INST_ROW *r = &rows[row];
    char text[256];

    rc->Row = row;
    row_label(r, text, sizeof(text));
    rc->Label = make_ctl(dlg, "STATIC", text, SS_LEFT | SS_NOPREFIX, 8, y,
                         284, 17, id, font);
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
        rc->Choice = make_ctl(dlg, "EDIT", text, WS_BORDER | WS_TABSTOP |
                              ES_AUTOHSCROLL | ES_NUMBER, 8, y + 18, 60, 12,
                              id + 1, font);
    } else {
        rc->Choice = make_ctl(dlg, "COMBOBOX", "", CBS_DROPDOWNLIST |
                              WS_VSCROLL | WS_TABSTOP, 8, y + 17, 160, 90,
                              id + 1, font);
        row_fill_combo(rc->Choice, r);
    }
    row_now(r, text, sizeof(text));
    rc->Now = make_ctl(dlg, "STATIC", text, SS_LEFT | SS_NOPREFIX, 174, y + 19,
                       118, 10, id + 2, font);
}

/* What a row's choice says now: 1 written (value in *v), 0 left as it is.
 * A range's text that does not parse, or lies outside it, is 0 with *v
 * the text's number (the page says so). */
static int row_read(const ROWCTL *rc, INST_U32 *v)
{
    const INST_ROW *r = &rows[rc->Row];
    char text[32];

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

static void rows_clear(ROWPAGE *rp)
{
    int i;

    for (i = 0; i < rp->Count; i++) {
        DestroyWindow(rp->R[i].Label);
        DestroyWindow(rp->R[i].Choice);
        DestroyWindow(rp->R[i].Now);
    }
    rp->Count = 0;
}

static void settings_note(HWND dlg, int page, int i)
{
    ROWPAGE *rp = &pageRows[page == P_USB ? 0 : 1];
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

static void settings_fill(HWND dlg, int page)
{
    ROWPAGE *rp = &pageRows[page == P_USB ? 0 : 1];
    HFONT font = (HFONT)SendMessageA(dlg, WM_GETFONT, 0, 0);
    int idx[WIZ_ROWS];
    int n;
    int i;
    int adv;

    rows_clear(rp);
    SetDlgItemTextA(dlg, IDC_S_HEAD, inst_page_text(page == P_USB ?
                                                    INST_PT_USB :
                                                    INST_PT_TBSET));
    n = rows_for(page, 0, idx, WIZ_ROWS);
    for (i = 0; i < n && i < 4; i++) {
        row_make(dlg, &rp->R[i], idx[i], 22 + WIZ_ROW_DLU * i, font,
                 IDC_ROW_BASE + IDC_ROW_STRIDE * i);
    }
    rp->Count = i;
    adv = rows_for(page, 1, idx, WIZ_ROWS);
    ShowWindow(GetDlgItem(dlg, IDC_S_ADVANCED), adv > 0 ? SW_SHOW : SW_HIDE);
    settings_note(dlg, page, rp->Count > 0 ? 0 : -1);
}

/* Leaving a page: each row's choice taken. 0 when a range's text is not
 * in its range (said; the page stays). */
static int settings_leave(HWND dlg, int page)
{
    ROWPAGE *rp = &pageRows[page == P_USB ? 0 : 1];
    int i;
    INST_U32 v;

    for (i = 0; i < rp->Count; i++) {
        const INST_ROW *r = &rows[rp->R[i].Row];
        char typed[32];
        typed[0] = '\0';
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

    if (advSet[i]) {
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
        ShowWindow(GetDlgItem(dlg, IDC_A_CHOICE), SW_SHOW);
        ShowWindow(GetDlgItem(dlg, IDC_A_EDIT), SW_HIDE);
    }
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
        SetDlgItemTextA(dlg, IDC_TEXT, inst_page_text(INST_PT_SUMMARY));
        _snprintf(line, sizeof(line) - 1, "%s.\n\n", inst_eng_confirm_text());
        line[sizeof(line) - 1] = '\0';
        tb_add(&t, line);
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
        tb_add(&t, "\nThe components:\n- xhci98.sys and xhciuas.sys\n");
        if (tbHave) {
            tb_add(&t, tbChoice ? "- Thunderbolt 3 / USB4 support\n" :
                   "- Thunderbolt 3 / USB4 support: left out\n");
        }
        if (trayHere) {
            tb_add(&t, trayChoice ? "- the xHCI98 USB Tree, started at login\n"
                                  : "- the xHCI98 USB Tree, not started at "
                                    "login\n");
        }
        tb_add(&t, "\nThe settings written machine-wide:\n");
        any = 0;
        for (i = 0; i < INST_MAX_VALUES && i < instValueCount; i++) {
            if (choiceSet[i]) {
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
    n = (int)SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)line);
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
    if (!ExitWindowsEx(EWX_REBOOT, 0)) {
        _snprintf(text, sizeof(text) - 1, "Windows did not restart (error "
                  "%lu). Restart it from the Start menu.",
                  (unsigned long)GetLastError());
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
    const char *last = inst_final_text(finalLine);

    inst_eng_flags(&changed, &failed, &restart, &steps);
    SetDlgItemTextA(dlg, IDC_F_LAST, ran ? last :
                    "Nothing was changed.");
    EnterCriticalSection(&textLock);
    set_edit(dlg, IDC_DETAIL, runText.Buf != NULL ? runText.Buf : "");
    LeaveCriticalSection(&textLock);
    SetDlgItemTextA(dlg, IDC_F_NOW, inst_page_text(INST_PT_NOW));
    SetDlgItemTextA(dlg, IDC_F_LATER, inst_page_text(INST_PT_LATER));
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
    case P_STEPS:
        return inst_eng_steps_text()[0] == '\0';
    default:
        return 0;
    }
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
        case IDC_C_BROWSE:
            comp_browse(dlg);
            return TRUE;
        case IDC_S_ADVANCED:
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
                ROWPAGE *rp = &pageRows[page == P_USB ? 0 : 1];
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
            case P_USB:
            case P_TBS:
                settings_fill(dlg, page);
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
            if ((page == P_COMP && !comp_leave(dlg)) ||
                ((page == P_USB || page == P_TBS) &&
                 !settings_leave(dlg, page))) {
                SET_RESULT(dlg, -1);
                return TRUE;
            }
            SET_RESULT(dlg, 0);
            return TRUE;
        case PSN_WIZBACK:
            if ((page == P_USB || page == P_TBS) && !settings_leave(dlg, page)) {
                SET_RESULT(dlg, -1);
                return TRUE;
            }
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

/* The sheet's own messages: a job's end, the gate. */
static WNDPROC sheetProc;

static LRESULT CALLBACK sheet_sub(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_APP_DONE) {
        if ((int)wp == JOB_READ) {
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
        HWND b = GetDlgItem(h, ID_SHEET_NEXT);

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
        if (!uninstallMode) {
            start_job(JOB_READ);
        }
    }
    return 0;
}

/* ---- the sheet -------------------------------------------------------------- */

static int run_wizard(void)
{
    static const int order[] = {
        P_WELCOME, P_FOUND, P_COMP, P_USB, P_TBS, P_SUMMARY, P_PROGRESS,
        P_STEPS, P_FINISH
    };
    static const int uorder[] = { P_WELCOME, P_SUMMARY, P_PROGRESS, P_FINISH };
    static const int tmpl[P_COUNT] = {
        IDD_WELCOME, IDD_FOUND, IDD_COMPONENTS, IDD_SETTINGS, IDD_SETTINGS,
        IDD_SUMMARY, IDD_PROGRESS, IDD_STEPS, IDD_FINISH
    };
    static const char *const title[P_COUNT] = {
        "Welcome", "What was found", "Components", "USB settings",
        "Thunderbolt settings", "Summary", "Progress", "Steps left for you",
        "Finish"
    };
    PROPSHEETPAGEA pg[P_COUNT];
    HPROPSHEETPAGE hp[P_COUNT];
    PROPSHEETHEADERA h;
    const int *list = uninstallMode ? uorder : order;
    int n = uninstallMode ? 4 : 9;
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

static int selftest(void)
{
    static const int wIds[] = { IDC_TEXT };
    static const int wTexts[] = { INST_PT_WELCOME };
    static const int uIds[] = { IDC_TITLE, IDC_TEXT, IDC_U_AGREE };
    static const int uTexts[] = { INST_PT_UN_TITLE, INST_PT_UN_WARN,
                                  INST_PT_UN_AGREE };
    static const int fIds[] = { IDC_TEXT };
    static const int fTexts[] = { INST_PT_FOUND };
    static const int cIds[] = {
        IDC_C_DRIVERS, IDC_C_DRIVERS_T, IDC_C_TB, IDC_C_TB_T, IDC_C_TRAY,
        IDC_C_TRAY_T, IDC_C_DIR_L, IDC_C_DIR_T
    };
    static const int cTexts[] = {
        INST_PT_DRIVERS, INST_PT_DRIVERS_T, INST_PT_TB, INST_PT_TB_T,
        INST_PT_TRAY, INST_PT_TRAY_T, INST_PT_DIR, INST_PT_DIR_9X
    };
    static const int c2Ids[] = { IDC_C_TB_T };
    static const int c2Texts[] = { INST_PT_TB_ABSENT };
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
        st_page(parent, IDD_FOUND, fIds, fTexts, 1, "what was found");
        st_page(parent, IDD_COMPONENTS, cIds, cTexts, 8, "the components");
        st_page(parent, IDD_COMPONENTS, c2Ids, c2Texts, 1, "the components, "
                "no controller");
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
        /* Every settings row's label, at its size on the page (record 26
         * section 11): the name and its sentence on two lines. */
        {
            HWND d = CreateDialogParamA(hInst, MAKEINTRESOURCEA(IDD_SETTINGS),
                                        parent, st_proc, 0);
            HFONT font = d != NULL ? (HFONT)SendMessageA(d, WM_GETFONT, 0, 0)
                                   : NULL;
            st(d != NULL, "the settings page for its rows");
            for (i = 0; d != NULL && i < instValueCount; i++) {
                INST_ROW r;
                char text[256];
                char msg[128];
                HWND c;
                memset(&r, 0, sizeof(r));
                r.Value = i;
                row_label(&r, text, sizeof(text));
                c = make_ctl(d, "STATIC", text, SS_LEFT | SS_NOPREFIX, 8, 22,
                             284, 17, IDC_ROW_BASE, font);
                _snprintf(msg, sizeof(msg) - 1, "the row label of %s fits",
                          instValues[i].Name);
                msg[sizeof(msg) - 1] = '\0';
                st(fits(c), msg);
                DestroyWindow(c);
            }
            if (d != NULL) {
                DestroyWindow(d);
            }
        }
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
