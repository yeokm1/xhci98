/*
 * setup.c - SETUP.EXE, at the package root: the program a person
 * double-clicks to install, update or uninstall the drivers (design record
 * 26 section 2; owner, 2026-10-19). It starts the installer's window,
 * tools\xhciinst\XHCIWIZ.EXE or, on 64-bit Windows, XHCIWZ64.EXE, with its
 * own command line, and ends at once. INSTALL.BAT stays for the command
 * line and scripts.
 *
 * WHY IT EXISTS. INSTALL.BAT, double-clicked, keeps a console window open
 * behind the installer's window for the whole run (an MS-DOS box on 98 SE
 * and ME). A windows-subsystem program opens none.
 *
 * ELEVATION. Its manifest asks for asInvoker: Vista's and 7's installer
 * detection would otherwise elevate a program named SETUP.EXE, a second
 * prompt for a program that changes nothing. The window's own manifest
 * asks for an administrator, and ShellExecuteEx raises that one prompt
 * (CreateProcess refuses such an image with ERROR_ELEVATION_REQUIRED).
 *
 * TARGETS AND BUILDS. ANSI C89, the in-repo MSVC 6.0, the C runtime
 * static, /SUBSYSTEM:WINDOWS,4.0, so one 32-bit image starts on 98 SE, ME,
 * 2000, XP and Vista and 7 of both architectures. Imports: KERNEL32,
 * USER32 and SHELL32 (check-imports.ps1, profile setup); IsWow64Process,
 * absent before XP SP2, is looked up at run time.
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#define SETUP_PATH  (MAX_PATH + 64)
#define SETUP_TITLE "xHCI98 Setup"

#ifndef SEE_MASK_FLAG_NO_UI
#define SEE_MASK_FLAG_NO_UI 0x00000400
#endif
/* SEE_MASK_NOASYNC's older name, in every shell from Windows 95 on. */
#ifndef SEE_MASK_FLAG_DDEWAIT
#define SEE_MASK_FLAG_DDEWAIT 0x00000100
#endif
#ifndef ERROR_CANCELLED
#define ERROR_CANCELLED 1223L
#endif

typedef BOOL (WINAPI *PFN_ISWOW64)(HANDLE, PBOOL);

/* The command line after the program's own name, its leading blanks
 * skipped: the arguments as typed, passed to the window unchanged. */
static const char *setup_tail(const char *cmd)
{
    if (*cmd == '"') {
        cmd = strchr(cmd + 1, '"');
        cmd = cmd != NULL ? cmd + 1 : "";
    } else {
        while (*cmd != '\0' && *cmd != ' ' && *cmd != '\t') {
            cmd++;
        }
    }
    while (*cmd == ' ' || *cmd == '\t') {
        cmd++;
    }
    return cmd;
}

/* 1 when the tail is the word /selftest alone. cmd.exe leaves a blank
 * before a redirection on the command line ("SETUP.EXE /selftest > f"
 * arrives as "/selftest "), so trailing blanks are allowed. */
static int setup_is_selftest(const char *tail)
{
    const char *w = "/selftest";

    while (*w != '\0') {
        if (*tail++ != *w++) {
            return 0;
        }
    }
    while (*tail == ' ' || *tail == '\t') {
        tail++;
    }
    return *tail == '\0';
}

/* From the program's own path: the package root (its directory) into root,
 * the window's path into wiz, each buffer size bytes. 0, or -1 when the
 * path has no directory or the result does not fit. */
static int setup_paths(const char *self, int is64, char *root, char *wiz,
                       size_t size)
{
    const char *slash = strrchr(self, '\\');
    const char *name = is64 ? "XHCIWZ64.EXE" : "XHCIWIZ.EXE";
    size_t n;

    if (slash == NULL) {
        return -1;
    }
    n = (size_t)(slash - self);
    if (n + 2 > size ||
        n + strlen("\\tools\\xhciinst\\") + strlen(name) + 1 > size) {
        return -1;
    }
    memcpy(root, self, n);
    root[n] = '\0';
    /* A drive's root keeps its backslash: "C:" alone names that drive's
     * current directory. */
    if (n == 2 && root[1] == ':') {
        root[2] = '\\';
        root[3] = '\0';
    }
    memcpy(wiz, self, n);
    strcpy(wiz + n, "\\tools\\xhciinst\\");
    strcat(wiz, name);
    return 0;
}

static int is_64bit_windows(void)
{
    PFN_ISWOW64 isWow64;
    BOOL wow = FALSE;

    isWow64 = (PFN_ISWOW64)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                          "IsWow64Process");
    if (isWow64 == NULL || !isWow64(GetCurrentProcess(), &wow)) {
        return 0;
    }
    return wow ? 1 : 0;
}

/* ---- the self-test ---------------------------------------------------------- */

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
    char root[SETUP_PATH];
    char wiz[SETUP_PATH];
    char longp[SETUP_PATH * 2];

    st(strcmp(setup_tail("\"C:\\a b\\SETUP.EXE\" /uninstall"), "/uninstall") == 0,
       "quoted name, one argument");
    st(strcmp(setup_tail("C:\\PKG\\SETUP.EXE /debug /to C:\\X"),
              "/debug /to C:\\X") == 0,
       "bare name, three arguments");
    st(strcmp(setup_tail("SETUP"), "") == 0, "no argument");
    st(strcmp(setup_tail("\"SETUP.EXE\""), "") == 0, "quoted name, no argument");
    st(strcmp(setup_tail("\"SETUP.EXE"), "") == 0, "unterminated quote");
    st(strcmp(setup_tail("SETUP.EXE  \t /set A=1"), "/set A=1") == 0,
       "blanks before the arguments");
    st(setup_is_selftest("/selftest") && setup_is_selftest("/selftest  "),
       "/selftest, with and without trailing blanks");
    st(!setup_is_selftest("/selftest /uninstall") &&
       !setup_is_selftest("/selftes") && !setup_is_selftest("/selftestx") &&
       !setup_is_selftest(""),
       "/selftest with company, cut short or run on is the window's");

    st(setup_paths("C:\\PKG\\X 1\\SETUP.EXE", 0, root, wiz, sizeof(root)) == 0 &&
       strcmp(root, "C:\\PKG\\X 1") == 0 &&
       strcmp(wiz, "C:\\PKG\\X 1\\tools\\xhciinst\\XHCIWIZ.EXE") == 0,
       "the 32-bit window");
    st(setup_paths("D:\\SETUP.EXE", 1, root, wiz, sizeof(root)) == 0 &&
       strcmp(root, "D:\\") == 0 &&
       strcmp(wiz, "D:\\tools\\xhciinst\\XHCIWZ64.EXE") == 0,
       "the x64 window, a drive's root");
    st(setup_paths("\\\\srv\\share\\pkg\\SETUP.EXE", 0, root, wiz,
                   sizeof(root)) == 0 &&
       strcmp(root, "\\\\srv\\share\\pkg") == 0 &&
       strcmp(wiz, "\\\\srv\\share\\pkg\\tools\\xhciinst\\XHCIWIZ.EXE") == 0,
       "a package on a share");
    st(setup_paths("SETUP.EXE", 0, root, wiz, sizeof(root)) == -1,
       "no directory refused");
    memset(longp, 'a', sizeof(longp) - 1);
    longp[sizeof(longp) - 1] = '\0';
    longp[2] = '\\';
    longp[sizeof(longp) - 12] = '\\';
    st(setup_paths(longp, 0, root, wiz, sizeof(root)) == -1,
       "a path too long refused");

    printf("%d checks, %d failures\n", stChecks, stFailures);
    return stFailures == 0 ? 0 : 1;
}

/* ---- WinMain ------------------------------------------------------------------- */

static int fail(const char *text)
{
    MessageBoxA(NULL, text, SETUP_TITLE, MB_OK | MB_ICONSTOP);
    return 1;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmdLine, int show)
{
    char self[SETUP_PATH];
    char root[SETUP_PATH];
    char wiz[SETUP_PATH];
    char text[SETUP_PATH + 256];
    const char *tail;
    SHELLEXECUTEINFOA sei;
    DWORD err;
    DWORD len;

    (void)hi;
    (void)prev;
    (void)cmdLine;
    (void)show;
    tail = setup_tail(GetCommandLineA());
    if (setup_is_selftest(tail)) {
        return selftest();
    }
    len = GetModuleFileNameA(NULL, self, sizeof(self));
    if (len == 0 || len >= sizeof(self) ||
        setup_paths(self, is_64bit_windows(), root, wiz, sizeof(wiz)) != 0) {
        return fail("SETUP.EXE cannot read its own location. Unzip the whole "
                    "package to a folder with a shorter path and run "
                    "SETUP.EXE from its top directory.");
    }
    if (GetFileAttributesA(wiz) == 0xFFFFFFFFUL) {
        _snprintf(text, sizeof(text) - 1,
                  "The installer's window, %s, is missing.\n\nUnzip the "
                  "whole package and run SETUP.EXE from its top directory.",
                  wiz);
        text[sizeof(text) - 1] = '\0';
        return fail(text);
    }
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    /* The process ends straight after the call, with no message loop, so
     * the shell is asked to finish the start before it returns (it does
     * not wait for the window to end). */
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_FLAG_DDEWAIT;
    sei.lpVerb = "open";
    sei.lpFile = wiz;
    sei.lpParameters = *tail != '\0' ? tail : NULL;
    sei.lpDirectory = root;
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei)) {
        err = GetLastError();
        if (err == ERROR_CANCELLED) {
            return fail("The installer was not started: it needs an "
                        "administrator's approval, which was not given.\n\n"
                        "Run SETUP.EXE again and answer Yes, or sign in as "
                        "an administrator.");
        }
        _snprintf(text, sizeof(text) - 1,
                  "The installer's window, %s, could not be started "
                  "(error %lu).", wiz, (unsigned long)err);
        text[sizeof(text) - 1] = '\0';
        return fail(text);
    }
    return 0;
}
