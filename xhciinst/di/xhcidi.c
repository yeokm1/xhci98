/*
 * xhcidi.c - XHCIDI.EXE, the Windows 98 SE and ME device install helper
 * (roadmap-usb4.md task 42-A.1; design record 27). The installer, a 32-bit
 * program, starts it once per device with one job on the command line:
 *
 *   XHCIDI.EXE <version> install <result file> <hwnd> <device id> <inf> [<flags>]
 *   XHCIDI.EXE <version> remove  <result file> <hwnd> <device id>
 *   XHCIDI.EXE <version> probe   <result file> <hwnd>
 *
 * and reads what the setup engine answered from the result file (record 27
 * section 4.2), whose END line, not the exit code, is the contract.
 *
 * WHY 16-BIT. The device installer of these systems is SETUPX.DLL, a 16-bit
 * DLL whose routines take far pointers; no 32-bit import of it exists
 * (record 27 section 3.1). This is a Win16 program in the large model,
 * built with Open Watcom against the 98 SE DDK's setupx.h and setupx.lib,
 * which bind each routine by its ordinal in module SETUPX: a system whose
 * SETUPX.DLL lacks one refuses to load the image before WinMain runs, and
 * the probe job is how the installer learns that it loads (section 7).
 *
 * WHAT IT NEVER DOES (section 4.3): show a window of its own, write the
 * registry, touch a devnode other than the one named, start a second job,
 * retry, or run on a system other than 98 SE or ME or for an installer of
 * another version. Exit codes, which nothing rests on: 0 the job ran to a
 * verdict, 1 refused, 2 a line could not be written, 3 no result file.
 */

#include <windows.h>
#include <setupx.h>
#include "di_sys.h"
#include "../../src/xhci_version.h"

/*
 * The string the packager finds in the image to check its version
 * (record 27 section 3.2): a 16-bit image has no PE version resource.
 */
const char xhcidi_stamp[] = "XHCIDI " XHCI_VER_STR;

#define XHCIDI_EXIT_DONE      0
#define XHCIDI_EXIT_REFUSED   1
#define XHCIDI_EXIT_WRITE     2
#define XHCIDI_EXIT_NOFILE    3

/*
 * GetVersion's low word on Windows 98 SE and ME: 3.95, the major version in
 * the low byte. Checked before the registry's build string, which is the
 * test that tells 98 SE from 98 and ME from either.
 */
#define XHCIDI_WIN16_VERSION  0x5F03U

static DI_CMD g_cmd;
static DI_RUN g_run;
static char g_version_number[64];

static int xhcidiFinish(int code)
{
    di_emit(&g_run, di_out_end(&g_run.out));
    _lclose(g_run.file);
    if (g_run.bad) {
        return XHCIDI_EXIT_WRITE;
    }
    return code;
}

static int xhcidiRefuse(const char *reason, const char *detail)
{
    static char text[96];
    lstrcpy(text, reason);
    if (detail != NULL && detail[0] != '\0') {
        lstrcat(text, " ");
        lstrcat(text, detail);
    }
    di_emit(&g_run, di_out_result_text(&g_run.out, DI_V_REFUSED, text));
    return xhcidiFinish(XHCIDI_EXIT_REFUSED);
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpszCmdLine,
                   int nCmdShow)
{
    OFSTRUCT of;
    int parsed;
    HWND hwnd;

    (void)hInstance;
    (void)hPrev;
    (void)nCmdShow;

    parsed = di_parse(lpszCmdLine, XHCI_VER_STR, &g_cmd);
    if (parsed == DI_PARSE_NOFILE) {
        return XHCIDI_EXIT_NOFILE;
    }

    /*
     * Truncated and rewritten (section 4.1), shared with the installer that
     * reads it while the job runs.
     */
    g_run.bad = 0;
    di_out_init(&g_run.out);
    g_run.file = OpenFile(g_cmd.result, &of,
                          OF_CREATE | OF_WRITE | OF_SHARE_DENY_NONE);
    if (g_run.file == HFILE_ERROR) {
        return XHCIDI_EXIT_NOFILE;
    }

    di_emit(&g_run, di_out_header(&g_run.out, XHCI_VER_STR));
    if (parsed == DI_PARSE_VERSION) {
        return xhcidiRefuse("version", NULL);
    }
    if (parsed != DI_PARSE_OK) {
        return xhcidiRefuse("command", g_cmd.why);
    }
    di_emit(&g_run, di_out_job(&g_run.out, &g_cmd));

    if ((LOWORD(GetVersion()) & 0xFFFFU) != XHCIDI_WIN16_VERSION ||
        !di_sys_version_number(g_version_number, sizeof g_version_number) ||
        !di_system_supported(g_version_number)) {
        return xhcidiRefuse("system",
                            g_version_number[0] != '\0' ? g_version_number : "-");
    }

    /*
     * On these systems the 16-bit and 32-bit window handle spaces are one
     * (record 27 section 4.1), so the installer's window is a window here;
     * a number that names none is a malformed command line.
     */
    hwnd = (HWND)(UINT)g_cmd.hwnd;
    if (hwnd != NULL && !IsWindow(hwnd)) {
        return xhcidiRefuse("command", "hwnd");
    }

    switch (g_cmd.job) {
    case DI_JOB_INSTALL:
        di_sys_install(&g_run, &g_cmd, hwnd);
        break;
    case DI_JOB_REMOVE:
        di_sys_remove(&g_run, &g_cmd, hwnd);
        break;
    default:
        di_emit(&g_run, di_out_result_text(&g_run.out, DI_V_PROBED, NULL));
        break;
    }
    return xhcidiFinish(XHCIDI_EXIT_DONE);
}
