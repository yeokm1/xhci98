/*
 * ditest.c - DITEST.EXE, the test driver of XHCIDI.EXE for the 42-V.1 legs
 * (roadmap-usb4.md task 42-A.1; design record 27 section 12). A 32-bit
 * console program for Windows 98 SE and ME guests that starts the helper
 * exactly as record 27 section 4.1 says the installer does, without the
 * window: the job file written, CreateProcessA with "<version> <job file>"
 * (record 27 section 4.1), an empty result file created first,
 * one wait loop with a one-second tick that reads the file for its END
 * line, and the four outcomes of that loop. It prints what the legs record:
 * the result file, when its END line was read, when the process handle
 * signalled (or that it did not within 60 s of END), and what
 * GetExitCodeProcess then said.
 *
 *   DITEST probe                              the probe job
 *   DITEST install <device id> <inf> [force] [quiet]
 *   DITEST remove <device id>
 *   DITEST raw <words...>                     the helper's command line after
 *                                             its name, verbatim, the job file
 *                                             as it is (a refusal; a job file
 *                                             written by hand)
 *   DITEST list [<prefix>]                    devnodes: id, status, problem
 *   DITEST reg <device id>                    the Enum key and driver key
 *
 * A device id ending in * is a prefix (install, remove, reg): the first
 * present devnode whose id begins with it, else the first Enum entry.
 *
 * Options before the command: /w gives the helper the hwnd of a window of
 * DITEST's own, pumped by MsgWaitForMultipleObjects as the installer's is
 * (without it the hwnd is 0, the console tool's); /r <file> the result file
 * (default C:\DIRES.TXT); /j <file> the job file (default C:\DIJOB.TXT);
 * /x <path> the helper (default XHCIDI.EXE beside
 * DITEST.EXE). Not part of any package: built by build.cmd beside it with
 * MSVC 6.0 and copied to a guest by hand.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../../src/xhci_version.h"

#define DT_TICK_MS      1000
#define DT_AFTER_END_MS 60000

typedef DWORD (WINAPI *PFN_LOCATE)(DWORD *, char *, ULONG);
typedef DWORD (WINAPI *PFN_REL)(DWORD *, DWORD, ULONG);
typedef DWORD (WINAPI *PFN_ID)(DWORD, char *, ULONG, ULONG);
typedef DWORD (WINAPI *PFN_STATUS)(ULONG *, ULONG *, DWORD, ULONG);

static HWND g_hwnd;
static char g_result[MAX_PATH] = "C:\\DIRES.TXT";
static char g_helper[MAX_PATH];
static char g_job[MAX_PATH] = "C:\\DIJOB.TXT";

/*
 * The job file of record 27 section 4.1, CRLF lines in its order; devid
 * and inf NULL where the job has none, flags NULL or the FLAGS words.
 */
static int dtWriteJob(const char *job, const char *devid, const char *inf,
                      const char *flags)
{
    char text[2048];
    HANDLE f;
    DWORD put = 0;
    /*
     * Each operand bounded so the whole stays inside text: longer ones than
     * the helper takes still reach it (to be refused), absurd ones do not.
     */
    if ((devid != NULL && strlen(devid) > 400) || (inf != NULL && strlen(inf) > 400) ||
        (flags != NULL && strlen(flags) > 64) || strlen(job) > 16) {
        printf("DITEST: an operand too long for the job file\n");
        return 0;
    }
    _snprintf(text, sizeof text - 1, "XHCIDI-JOB 1\r\nJOB %s\r\nRESULT %s\r\nHWND %lu\r\n",
              job, g_result, (unsigned long)(UINT_PTR)g_hwnd);
    text[sizeof text - 1] = 0;
    if (devid != NULL) {
        strcat(text, "DEVICE ");
        strcat(text, devid);
        strcat(text, "\r\n");
    }
    if (inf != NULL) {
        strcat(text, "INF ");
        strcat(text, inf);
        strcat(text, "\r\n");
    }
    if (flags != NULL && flags[0] != 0) {
        strcat(text, "FLAGS");
        strcat(text, flags);
        strcat(text, "\r\n");
    }
    strcat(text, "END\r\n");
    f = CreateFileA(g_job, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        printf("DITEST: cannot write %s (%lu)\n", g_job, GetLastError());
        return 0;
    }
    WriteFile(f, text, (DWORD)strlen(text), &put, NULL);
    CloseHandle(f);
    printf("---- %s ----\n%s", g_job, text);
    return put == strlen(text);
}

static LRESULT CALLBACK dtWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static int dtMakeWindow(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = dtWndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "DITESTWND";
    if (!RegisterClassA(&wc)) {
        return 0;
    }
    g_hwnd = CreateWindowA("DITESTWND", "DITEST - the XHCIDI test window",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 420, 160,
                           NULL, NULL, wc.hInstance, NULL);
    return g_hwnd != NULL;
}

static void dtPump(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

/* The result file's text, and whether it holds an END line. */
static int dtReadResult(char *buf, DWORD size)
{
    HANDLE f;
    DWORD got = 0;
    buf[0] = '\0';
    f = CreateFileA(g_result, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!ReadFile(f, buf, size - 1, &got, NULL)) {
        got = 0;
    }
    CloseHandle(f);
    buf[got] = '\0';
    return strncmp(buf, "END ", 4) == 0 || strstr(buf, "\r\nEND ") != NULL;
}

static int dtRun(const char *args)
{
    static char cmd[1024];
    static char text[16384];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE f;
    DWORD t0, tEnd = 0, tSig = 0, code = 0, w;
    int ended = 0;
    int signalled = 0;

    f = CreateFileA(g_result, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        printf("DITEST: cannot create %s (%lu)\n", g_result, GetLastError());
        return 2;
    }
    CloseHandle(f);

    _snprintf(cmd, sizeof cmd - 1, "%s %s", g_helper, args);
    cmd[sizeof cmd - 1] = '\0';
    printf("DITEST: starting %s\n", cmd);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    t0 = GetTickCount();
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("DITEST: CreateProcessA failed (%lu)\n", GetLastError());
        return 2;
    }
    CloseHandle(pi.hThread);

    for (;;) {
        if (g_hwnd != NULL) {
            w = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, DT_TICK_MS, QS_ALLINPUT);
            dtPump();
        } else {
            w = WaitForSingleObject(pi.hProcess, DT_TICK_MS);
        }
        if (w == WAIT_OBJECT_0 && !signalled) {
            signalled = 1;
            tSig = GetTickCount() - t0;
        }
        if (!ended && dtReadResult(text, sizeof text)) {
            ended = 1;
            tEnd = GetTickCount() - t0;
        }
        if (signalled) {
            break;
        }
        if (ended && GetTickCount() - t0 - tEnd > DT_AFTER_END_MS) {
            break;
        }
    }
    dtReadResult(text, sizeof text);
    printf("---- %s ----\n%s---- end of file ----\n", g_result, text);
    if (ended) {
        printf("DITEST: END line read %lu ms after the start\n", tEnd);
    } else {
        printf("DITEST: no END line: the helper did not finish\n");
    }
    if (signalled) {
        GetExitCodeProcess(pi.hProcess, &code);
        printf("DITEST: process handle signalled %lu ms after the start", tSig);
        if (ended) {
            printf(" (%ld ms after END was read)", (long)tSig - (long)tEnd);
        }
        printf("; GetExitCodeProcess %lu\n", code);
    } else {
        printf("DITEST: process handle NOT signalled %u s after END: the end of a helper cannot be seen here\n",
               DT_AFTER_END_MS / 1000);
    }
    CloseHandle(pi.hProcess);
    return ended && signalled ? 0 : 3;
}

static char g_found[256];

/*
 * Every devnode (or, with find, the first whose device id begins with the
 * prefix, without case, kept in g_found and not printed).
 */
static int dtList(const char *prefix, int find)
{
    HMODULE m = LoadLibraryA("cfgmgr32.dll");
    PFN_LOCATE locate;
    PFN_REL child, sibling, parent;
    PFN_ID getid;
    PFN_STATUS status;
    DWORD dn, next;
    char id[256];
    int depth = 0;

    if (m == NULL) {
        printf("DITEST: no cfgmgr32.dll\n");
        return 2;
    }
    locate = (PFN_LOCATE)GetProcAddress(m, "CM_Locate_DevNodeA");
    child = (PFN_REL)GetProcAddress(m, "CM_Get_Child");
    sibling = (PFN_REL)GetProcAddress(m, "CM_Get_Sibling");
    parent = (PFN_REL)GetProcAddress(m, "CM_Get_Parent");
    getid = (PFN_ID)GetProcAddress(m, "CM_Get_Device_IDA");
    status = (PFN_STATUS)GetProcAddress(m, "CM_Get_DevNode_Status");
    if (!locate || !child || !sibling || !parent || !getid || !status) {
        printf("DITEST: cfgmgr32.dll lacks a routine\n");
        return 2;
    }
    if (locate(&dn, NULL, 0) != 0) {
        printf("DITEST: CM_Locate_DevNodeA failed\n");
        return 2;
    }
    for (;;) {
        ULONG st = 0, pr = 0;
        id[0] = '\0';
        getid(dn, id, sizeof id, 0);
        if (prefix == NULL || _strnicmp(id, prefix, strlen(prefix)) == 0) {
            if (find) {
                strcpy(g_found, id);
                return 0;
            }
            status(&st, &pr, dn, 0);
            printf("%*s%s  status %08lX problem %lu\n", depth * 2, "", id, st, pr);
        }
        if (child(&next, dn, 0) == 0) {
            dn = next;
            depth++;
            continue;
        }
        while (sibling(&next, dn, 0) != 0) {
            if (depth == 0 || parent(&next, dn, 0) != 0) {
                return find ? 1 : 0;
            }
            dn = next;
            depth--;
        }
        dn = next;
    }
}

/*
 * The first Enum\<enumerator>\<device>\<instance> key whose id begins with
 * the prefix: an entry that is not present, for a removal.
 */
static int dtFindReg(const char *prefix)
{
    HKEY e, d, i;
    DWORD a, b, c;
    char en[64], dv[200], in[200], id[600];
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum", 0, KEY_READ, &e) != ERROR_SUCCESS) {
        return 1;
    }
    for (a = 0; RegEnumKeyA(e, a, en, sizeof en) == ERROR_SUCCESS; a++) {
        if (RegOpenKeyExA(e, en, 0, KEY_READ, &d) != ERROR_SUCCESS) {
            continue;
        }
        for (b = 0; RegEnumKeyA(d, b, dv, sizeof dv) == ERROR_SUCCESS; b++) {
            if (RegOpenKeyExA(d, dv, 0, KEY_READ, &i) != ERROR_SUCCESS) {
                continue;
            }
            for (c = 0; RegEnumKeyA(i, c, in, sizeof in) == ERROR_SUCCESS; c++) {
                _snprintf(id, sizeof id - 1, "%s\\%s\\%s", en, dv, in);
                id[sizeof id - 1] = 0;
                if (_strnicmp(id, prefix, strlen(prefix)) == 0 && strlen(id) < sizeof g_found) {
                    strcpy(g_found, id);
                    RegCloseKey(i);
                    RegCloseKey(d);
                    RegCloseKey(e);
                    return 0;
                }
            }
            RegCloseKey(i);
        }
        RegCloseKey(d);
    }
    RegCloseKey(e);
    return 1;
}

/*
 * A device id ending in * is a prefix: the first present devnode that
 * matches, else the first Enum entry. Returns the id to use, or NULL.
 */
static const char *dtResolve(const char *arg)
{
    static char prefix[256];
    size_t n = strlen(arg);
    if (n == 0 || arg[n - 1] != '*' || n >= sizeof prefix) {
        return arg;
    }
    memcpy(prefix, arg, n - 1);
    prefix[n - 1] = 0;
    if (dtList(prefix, 1) == 0 || dtFindReg(prefix) == 0) {
        printf("DITEST: %s is %s\n", arg, g_found);
        return g_found;
    }
    printf("DITEST: no device id begins %s\n", prefix);
    return NULL;
}

static void dtValues(HKEY root, const char *path)
{
    HKEY k;
    DWORD i;
    if (RegOpenKeyExA(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        printf("  %s: not there\n", path);
        return;
    }
    printf("  %s:\n", path);
    for (i = 0;; i++) {
        char name[256];
        BYTE data[1024];
        DWORD nn = sizeof name, dl = sizeof data - 1, type;
        if (RegEnumValueA(k, i, name, &nn, NULL, &type, data, &dl) != ERROR_SUCCESS) {
            break;
        }
        if (type == REG_SZ) {
            data[dl] = 0;
            printf("    %s = \"%s\"\n", name[0] ? name : "(default)", (char *)data);
        } else if (type == REG_DWORD && dl == 4) {
            printf("    %s = dword %08lX\n", name, *(DWORD *)data);
        } else {
            printf("    %s = type %lu, %lu bytes\n", name, type, dl);
        }
    }
    RegCloseKey(k);
}

static int dtReg(const char *devid)
{
    char path[400];
    char drv[200];
    HKEY k;
    DWORD dl = sizeof drv, type;
    _snprintf(path, sizeof path - 1, "Enum\\%s", devid);
    path[sizeof path - 1] = '\0';
    dtValues(HKEY_LOCAL_MACHINE, path);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return 1;
    }
    drv[0] = '\0';
    if (RegQueryValueExA(k, "Driver", NULL, &type, (BYTE *)drv, &dl) == ERROR_SUCCESS && type == REG_SZ) {
        drv[sizeof drv - 1] = '\0';
        _snprintf(path, sizeof path - 1, "System\\CurrentControlSet\\Services\\Class\\%s", drv);
        path[sizeof path - 1] = '\0';
        dtValues(HKEY_LOCAL_MACHINE, path);
    }
    RegCloseKey(k);
    return 0;
}

static void dtUsage(void)
{
    printf("DITEST %s - the XHCIDI.EXE test driver (design record 27 section 12)\n"
           "  DITEST [/w] [/r <result>] [/j <job file>] [/x <helper>] probe\n"
           "  DITEST [...] install <device id> <inf> [force] [quiet]\n"
           "  DITEST [...] remove <device id>\n"
           "  DITEST [...] raw <the helper's words>\n"
           "  DITEST list [<prefix>]\n"
           "  DITEST reg <device id>\n", XHCI_VER_STR);
}

int main(int argc, char **argv)
{
    static char args[1024];
    char *slash;
    int i = 1;
    int k;
    int want_window = 0;

    GetModuleFileNameA(NULL, g_helper, sizeof g_helper);
    slash = strrchr(g_helper, '\\');
    if (slash != NULL) {
        strcpy(slash + 1, "XHCIDI.EXE");
    }
    while (i < argc && argv[i][0] == '/') {
        if (_stricmp(argv[i], "/w") == 0) {
            want_window = 1;
            i++;
        } else if (_stricmp(argv[i], "/r") == 0 && i + 1 < argc) {
            strncpy(g_result, argv[i + 1], sizeof g_result - 1);
            i += 2;
        } else if (_stricmp(argv[i], "/j") == 0 && i + 1 < argc) {
            strncpy(g_job, argv[i + 1], sizeof g_job - 1);
            i += 2;
        } else if (_stricmp(argv[i], "/x") == 0 && i + 1 < argc) {
            strncpy(g_helper, argv[i + 1], sizeof g_helper - 1);
            i += 2;
        } else {
            dtUsage();
            return 1;
        }
    }
    if (i >= argc) {
        dtUsage();
        return 1;
    }
    if (_stricmp(argv[i], "list") == 0) {
        return dtList(i + 1 < argc ? argv[i + 1] : NULL, 0);
    }
    if (_stricmp(argv[i], "reg") == 0 && i + 1 < argc) {
        const char *id = dtResolve(argv[i + 1]);
        return id != NULL ? dtReg(id) : 1;
    }
    if (want_window && !dtMakeWindow()) {
        printf("DITEST: the window could not be made\n");
        return 2;
    }
    if (g_hwnd != NULL) {
        printf("DITEST: window hwnd %lu\n", (unsigned long)(UINT_PTR)g_hwnd);
    }
    if (_stricmp(argv[i], "raw") == 0) {
        args[0] = '\0';
        for (k = i + 1; k < argc; k++) {
            if (strlen(args) + strlen(argv[k]) + 2 >= sizeof args) {
                return 1;
            }
            if (args[0] != '\0') {
                strcat(args, " ");
            }
            strcat(args, argv[k]);
        }
        return dtRun(args);
    }
    _snprintf(args, sizeof args - 1, "%s %s", XHCI_VER_STR, g_job);
    if (_stricmp(argv[i], "probe") == 0 && i + 1 == argc) {
        return dtWriteJob("probe", NULL, NULL, NULL) ? dtRun(args) : 2;
    }
    if (_stricmp(argv[i], "remove") == 0 && i + 2 == argc) {
        const char *id = dtResolve(argv[i + 1]);
        if (id == NULL) {
            return 1;
        }
        return dtWriteJob("remove", id, NULL, NULL) ? dtRun(args) : 2;
    }
    if (_stricmp(argv[i], "install") == 0 && i + 3 <= argc && i + 5 >= argc) {
        char flags[64];
        const char *id = dtResolve(argv[i + 1]);
        if (id == NULL) {
            return 1;
        }
        flags[0] = 0;
        for (k = i + 3; k < argc; k++) {
            if (strlen(flags) + strlen(argv[k]) + 2 >= sizeof flags) {
                return 1;
            }
            strcat(flags, " ");
            strcat(flags, argv[k]);
        }
        return dtWriteJob("install", id, argv[i + 2], flags) ? dtRun(args) : 2;
    }
    dtUsage();
    return 1;
}
