/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026, Yeo Kheng Meng
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * devcycle - disable and re-enable a device through the documented SetupAPI
 * property-change request (DIF_PROPERTYCHANGE, DICS_DISABLE / DICS_ENABLE),
 * the same request Device Manager makes. Used to disable and re-enable the
 * tunnel bridge above stor98's controller while the host unplugs and plugs
 * the enclosure (the xhci98 Windows 2000 SMP hang report; TESTING
 * "Bridge cycle under Verifier").
 *
 *   devcycle list [substring]
 *   devcycle cycle <instance-id substring> <count> <ms between steps>
 *   devcycle remove <instance-id substring>   (uninstall the node, DIF_REMOVE)
 *
 * The first present device whose instance ID contains the substring (case
 * insensitive) is used. Each step prints the tick count and the result. A
 * step is not completed when the call fails or when the installer asks for
 * a reboot or restart (DI_NEEDREBOOT, DI_NEEDRESTART). The exit code is the
 * number of such steps (cycle), or nonzero for a removal not completed.
 * Builds with MSVC 6.0: cl /O2 devcycle.c setupapi.lib
 */

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int match(const char *id, const char *sub)
{
    char a[512], b[256];

    strncpy(a, id, sizeof(a) - 1);
    a[sizeof(a) - 1] = 0;
    strncpy(b, sub, sizeof(b) - 1);
    b[sizeof(b) - 1] = 0;
    _strupr(a);
    _strupr(b);
    return strstr(a, b) != NULL;
}

/* Result of an installer call that succeeded but did not complete the
 * transition live: the installer set DI_NEEDREBOOT or DI_NEEDRESTART. */
#define RC_RESTART  (-1)

/* Error code of the call that just failed, never 0. */
static int lasterr(void)
{
    int rc = (int)GetLastError();

    return rc != 0 ? rc : 1;
}

/* Clears the restart flags of the device's install parameters, so that the
 * flags read after the next call are that call's own. */
static int clearflags(HDEVINFO set, SP_DEVINFO_DATA *dev)
{
    SP_DEVINSTALL_PARAMS ip;

    memset(&ip, 0, sizeof(ip));
    ip.cbSize = sizeof(ip);
    if (!SetupDiGetDeviceInstallParams(set, dev, &ip)) {
        return lasterr();
    }
    ip.Flags &= ~(DI_NEEDREBOOT | DI_NEEDRESTART);
    if (!SetupDiSetDeviceInstallParams(set, dev, &ip)) {
        return lasterr();
    }
    return 0;
}

/* One installer call. Its error code is captured right after a failure. A
 * call that succeeds but leaves DI_NEEDREBOOT or DI_NEEDRESTART set did not
 * make the change live (it takes effect only after a restart): RC_RESTART,
 * counted as not completed (Microsoft's SetupDiCallClassInstaller
 * documentation: check these flags after a successful call). */
static int call(DI_FUNCTION fn, HDEVINFO set, SP_DEVINFO_DATA *dev)
{
    SP_DEVINSTALL_PARAMS ip;
    int rc = clearflags(set, dev);

    if (rc != 0) {
        return rc;
    }
    if (!SetupDiCallClassInstaller(fn, set, dev)) {
        return lasterr();
    }
    memset(&ip, 0, sizeof(ip));
    ip.cbSize = sizeof(ip);
    if (!SetupDiGetDeviceInstallParams(set, dev, &ip)) {
        return lasterr();
    }
    return (ip.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) ? RC_RESTART : 0;
}

static int change(HDEVINFO set, SP_DEVINFO_DATA *dev, DWORD state)
{
    SP_PROPCHANGE_PARAMS p;

    memset(&p, 0, sizeof(p));
    p.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    p.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    p.StateChange = state;
    p.Scope = DICS_FLAG_GLOBAL;
    p.HwProfile = 0;
    if (!SetupDiSetClassInstallParams(set, dev, &p.ClassInstallHeader, sizeof(p))) {
        return lasterr();
    }
    return call(DIF_PROPERTYCHANGE, set, dev);
}

static const char *what(int rc)
{
    return rc == 0 ? "ok" : rc == RC_RESTART ? "NOT COMPLETED (restart required)" : "FAILED";
}

int main(int argc, char **argv)
{
    HDEVINFO set;
    SP_DEVINFO_DATA dev;
    char id[512];
    DWORD i;
    int found = 0, n, count, ms, rc, bad = 0;

    if (argc < 2) {
        printf("usage: devcycle list [substring] | cycle <substring> <count> <ms> | disable|enable|remove <substring>\n");
        return 255;
    }
    set = SetupDiGetClassDevs(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        printf("SetupDiGetClassDevs failed: %lu\n", GetLastError());
        return 254;
    }
    for (i = 0;; i++) {
        dev.cbSize = sizeof(dev);
        if (!SetupDiEnumDeviceInfo(set, i, &dev)) {
            break;
        }
        if (!SetupDiGetDeviceInstanceId(set, &dev, id, sizeof(id), NULL)) {
            continue;
        }
        if (strcmp(argv[1], "list") == 0) {
            if (argc < 3 || match(id, argv[2])) {
                printf("%s\n", id);
            }
            continue;
        }
        if (strcmp(argv[1], "remove") == 0 && argc >= 3 && match(id, argv[2])) {
            // Uninstall the node (Device Manager's Uninstall): it is found
            // again as new hardware at the next rescan. A failure, or a
            // removal deferred to a restart (a veto), is the exit code.
            rc = call(DIF_REMOVE, set, &dev);
            printf("remove %s: %s (%d)\n", id, what(rc), rc);
            SetupDiDestroyDeviceInfoList(set);
            return rc == 0 ? 0 : 1;
        }
        if ((strcmp(argv[1], "disable") == 0 || strcmp(argv[1], "enable") == 0) && argc >= 3 &&
            match(id, argv[2])) {
            // One state change of the first match (both ends of a cycle,
            // for example to unload a driver whose every device is disabled).
            rc = change(set, &dev, strcmp(argv[1], "disable") == 0 ? DICS_DISABLE : DICS_ENABLE);
            printf("%lu %s %s: %s (%d)\n", GetTickCount(), argv[1], id, what(rc), rc);
            SetupDiDestroyDeviceInfoList(set);
            return rc == 0 ? 0 : 1;
        }
        if (strcmp(argv[1], "cycle") == 0 && argc >= 5 && match(id, argv[2])) {
            found = 1;
            break;
        }
    }
    if (strcmp(argv[1], "list") == 0) {
        SetupDiDestroyDeviceInfoList(set);
        return 0;
    }
    if (!found) {
        printf("no present device matches\n");
        SetupDiDestroyDeviceInfoList(set);
        return 253;
    }
    count = atoi(argv[3]);
    ms = atoi(argv[4]);
    printf("cycling %s\n", id);
    for (n = 0; n < count; n++) {
        rc = change(set, &dev, DICS_DISABLE);
        printf("%lu cycle %d disable: %s (%d)\n", GetTickCount(), n, what(rc), rc);
        bad += rc != 0;
        Sleep(ms);
        rc = change(set, &dev, DICS_ENABLE);
        printf("%lu cycle %d enable: %s (%d)\n", GetTickCount(), n, what(rc), rc);
        bad += rc != 0;
        Sleep(ms);
    }
    SetupDiDestroyDeviceInfoList(set);
    printf("%s: %d failed step(s)\n", bad ? "FAIL" : "done", bad);
    return bad;
}
