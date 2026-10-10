/*
 * pmctl.c - PMCTL.EXE, the console tool that drives XHCIPM.SYS, selective
 * suspend's test client (roadmap-hcd.md task 36.34; pmioctl.h). A Win32
 * console program built with MSVC 6.0 (build.cmd) that runs unchanged on
 * Windows 98 SE, ME, 2000 and XP. TEST ONLY, NEVER SHIPPED.
 *
 *   PMCTL [-n N] STATUS          print the client's counts
 *   PMCTL [-n N] D0|D1|D2|D3     request that device power state
 *   PMCTL [-n N] WW              request IRP_MN_WAIT_WAKE (S0)
 *   PMCTL [-n N] WWCANCEL        cancel it
 *   PMCTL [-n N] IDLE [flags]    submit the idle notification; flags a
 *                                sum of 1 arm wake, 2 no power, 4 D3,
 *                                8 request D0 on its completion
 *   PMCTL [-n N] IDLECANCEL      cancel it
 *   PMCTL [-n N] READS ON|OFF    the interrupt IN read loop
 *   PMCTL [-n N] RESET           zero the counts
 *   PMCTL [-n N] WATCH secs      STATUS once a second for secs seconds
 *
 * N picks \\.\XHCIPM<N> (default 0). Each command prints one ASCII line
 * per result, so a leg's log reads the same on every target. Exit 0 on
 * success, 1 on a failed request, 2 on a usage error.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmioctl.h"

static const char pmctlMarker[] = PM_TEST_MARKER;

static void usage(void)
{
    printf("usage: PMCTL [-n N] STATUS | D0 | D1 | D2 | D3 | WW | WWCANCEL |"
           " IDLE [flags] | IDLECANCEL | READS ON|OFF | RESET | WATCH secs\n"
           "  (%s)\n", pmctlMarker);
}

static void show(const PM_STATUS *s)
{
    printf("dev D%lu sys S%lu started %lu | power req %lu cb %lu last "
           "0x%08lX D%lu refused %lu\n",
           s->DevicePower - 1, s->SystemPower - 1, s->Started,
           s->PowerRequests, s->PowerCallbacks,
           (unsigned long)s->PowerLastStatus, s->PowerLastState - 1,
           s->PowerRefused);
    printf("wake req %lu cb %lu last 0x%08lX | irp done %lu last 0x%08lX "
           "held %lu\n",
           s->WakeRequests, s->WakeCallbacks,
           (unsigned long)s->WakeLastStatus, s->WakeIrpCompletions,
           (unsigned long)s->WakeIrpLastStatus, s->WakeHeld);
    printf("idle submit %lu cb %lu done %lu last 0x%08lX held %lu | cb "
           "asked D%lu got 0x%08lX\n",
           s->IdleSubmits, s->IdleCallbacks, s->IdleCompletions,
           (unsigned long)s->IdleLastStatus, s->IdleHeld,
           s->IdleCallbackPower != 0 ? s->IdleCallbackPower - 1 : 0,
           (unsigned long)s->IdleCallbackPowerStatus);
    printf("reads on %lu ok %lu failed %lu last 0x%08lX usbd 0x%08lX | "
           "config %lu attr 0x%02lX in 0x%02lX status 0x%08lX\n",
           s->ReadsOn, s->ReadsCompleted, s->ReadsFailed,
           (unsigned long)s->ReadLastStatus, s->ReadLastUsbd, s->Configured,
           s->ConfigAttributes, s->InterruptIn,
           (unsigned long)s->ConfigStatus);
}

static int request(HANDLE h, DWORD code, DWORD arg, int hasArg,
                   PM_STATUS *out)
{
    DWORD got;
    BOOL ok;

    ok = DeviceIoControl(h, code, hasArg ? &arg : NULL,
                         hasArg ? sizeof(arg) : 0, out,
                         out != NULL ? sizeof(*out) : 0, &got, NULL);
    if (!ok) {
        printf("PMCTL: request 0x%08lX failed, error %lu\n", code,
               GetLastError());
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char path[32];
    HANDLE h;
    PM_STATUS st;
    const char *cmd;
    int index;
    int i;
    int rc;
    unsigned long n;

    index = 0;
    i = 1;
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) {
        index = atoi(argv[2]);
        i = 3;
    }
    if (i >= argc || index < 0 || index >= PM_MAX_DEVICES) {
        usage();
        return 2;
    }
    cmd = argv[i];
    sprintf(path, "\\\\.\\%s%d", PM_LINK_PREFIX, index);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("PMCTL: cannot open %s, error %lu\n", path, GetLastError());
        return 1;
    }
    rc = 0;
    if (_stricmp(cmd, "STATUS") == 0) {
        rc = request(h, PM_IOCTL_STATUS, 0, 0, &st);
        if (rc == 0) {
            show(&st);
        }
    } else if ((cmd[0] == 'D' || cmd[0] == 'd') && cmd[1] >= '0' &&
               cmd[1] <= '3' && cmd[2] == 0) {
        rc = request(h, PM_IOCTL_SET_POWER, (DWORD)(cmd[1] - '0' + 1), 1,
                     NULL);
    } else if (_stricmp(cmd, "WW") == 0) {
        rc = request(h, PM_IOCTL_WAIT_WAKE, 0, 0, NULL);
    } else if (_stricmp(cmd, "WWCANCEL") == 0) {
        rc = request(h, PM_IOCTL_WW_CANCEL, 0, 0, NULL);
    } else if (_stricmp(cmd, "IDLE") == 0) {
        n = (i + 1 < argc) ? strtoul(argv[i + 1], NULL, 0) : 0;
        rc = request(h, PM_IOCTL_IDLE, (DWORD)n, 1, NULL);
    } else if (_stricmp(cmd, "IDLECANCEL") == 0) {
        rc = request(h, PM_IOCTL_IDLE_CANCEL, 0, 0, NULL);
    } else if (_stricmp(cmd, "READS") == 0 && i + 1 < argc) {
        rc = request(h, PM_IOCTL_READS,
                     _stricmp(argv[i + 1], "ON") == 0 ? 1UL : 0UL, 1, NULL);
    } else if (_stricmp(cmd, "RESET") == 0) {
        rc = request(h, PM_IOCTL_RESET, 0, 0, NULL);
    } else if (_stricmp(cmd, "WATCH") == 0 && i + 1 < argc) {
        n = strtoul(argv[i + 1], NULL, 0);
        while (rc == 0 && n-- != 0) {
            rc = request(h, PM_IOCTL_STATUS, 0, 0, &st);
            if (rc == 0) {
                printf("[%lu ms]\n", (unsigned long)GetTickCount());
                show(&st);
            }
            Sleep(1000);
        }
    } else {
        usage();
        rc = 2;
    }
    if (rc == 0 && _stricmp(cmd, "STATUS") != 0 &&
        _stricmp(cmd, "WATCH") != 0) {
        printf("PMCTL %s: sent\n", cmd);
    }
    CloseHandle(h);
    return rc;
}
