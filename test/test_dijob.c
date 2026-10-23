/*
 * test_dijob.c - the installer and its 98 SE / ME helper read each other
 * (design record 27 sections 4.1 and 4.2, roadmap-usb4.md tasks 42-A.1 and
 * 42-A.2).
 *
 * Links the installer's pure half (xhciinst\inst_core.c) with the helper's
 * (xhciinst\di\di_core.c): every job file and command line the installer
 * writes is read back by the helper's own parser, field by field, and every
 * result file the helper's writer makes is read back by the installer's
 * parser to the verdict it means. Neither side's own vectors can show that
 * the two agree; this file is where they meet.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xhciinst/inst_core.h"
#include "../xhciinst/di/di_core.h"
#include "test_harness.h"

#define V "3.1.0.0"

static const char *rootHub =
    "USB\\XHCI98_ROOT_HUB\\0PCI&VEN_1B36&DEV_000D&SUBSYS_11001AF4&REV_01"
    "\\BUS_00&DEV_03&FUNC_00";

/* One job written by the installer and read by the helper. */
static void round_trip_sec(int job, int dijob, const char *devId,
                           const char *inf, const char *section, int force,
                           int quiet, unsigned long hwnd, const char *what)
{
    char text[INST_DI_JOB_BYTES + 1];
    char path[128];
    char tail[256];
    DI_CMD c;
    int ok;

    ok = inst_di_jobpath(path, sizeof(path), "C:\\XHCI98") &&
         inst_di_jobfile_sec(text, sizeof(text), job,
                             "C:\\XHCI98\\XHCIDI.RES", hwnd, devId, inf,
                             section, force, quiet) &&
         inst_di_cmdline(tail, sizeof(tail), V, path);
    CHECK(ok, what);
    if (!ok) {
        return;
    }
    memset(&c, 0, sizeof(c));
    CHECK(di_parse(tail, text, V, &c) == DI_PARSE_OK, what);
    CHECK(c.job == dijob, what);
    CHECK(strcmp(c.version, V) == 0, what);
    CHECK(strcmp(c.result, "C:\\XHCI98\\XHCIDI.RES") == 0, what);
    CHECK(c.hwnd == hwnd, what);
    if (devId != NULL) {
        CHECK(strcmp(c.devid, devId) == 0, what);
    }
    if (inf != NULL) {
        CHECK(strcmp(c.inf, inf) == 0, what);
    }
    CHECK(((c.flags & DI_FLAG_FORCE) != 0) == (force != 0), what);
    if (job == INST_DI_INSTALL) {
        CHECK(((c.flags & DI_FLAG_QUIET) != 0) == (quiet != 0), what);
    }
    CHECK(strcmp(c.section, section != NULL ? section : "") == 0, what);
}

static void round_trip(int job, int dijob, const char *devId,
                       const char *inf, int force, int quiet,
                       unsigned long hwnd, const char *what)
{
    round_trip_sec(job, dijob, devId, inf, NULL, force, quiet, hwnd, what);
}

static void test_jobs(void)
{
    char longId[201];
    const char *pci = "PCI\\VEN_8086&DEV_15EC&SUBSYS_00000000&REV_00\\"
                      "BUS_00&DEV_0D&FUNC_00";

    round_trip(INST_DI_PROBE, DI_JOB_PROBE, NULL, NULL, 0, 0, 1234UL,
               "the probe, read by the helper");
    round_trip(INST_DI_INSTALL, DI_JOB_INSTALL, pci, "C:\\XHCI98\\XHCICM.INF",
               0, 1, 0UL, "an install, quiet, read by the helper");
    round_trip(INST_DI_INSTALL, DI_JOB_INSTALL, pci, "C:\\XHCI98\\XHCI98.INF",
               1, 1, 65535UL, "an install, force and quiet, read by the helper");
    round_trip(INST_DI_INSTALL, DI_JOB_INSTALL, pci, "C:\\XHCI98\\XHCI98.INF",
               0, 0, 4660UL, "an install with no FLAGS line, read by the helper");
    round_trip(INST_DI_INSTALL, DI_JOB_INSTALL, rootHub,
               "C:\\XHCI98\\XHCI98.INF", 0, 1, 0UL,
               "a root hub's 87-character id, read by the helper");
    round_trip(INST_DI_REMOVE, DI_JOB_REMOVE, rootHub, NULL, 0, 0, 0UL,
               "a removal, read by the helper");
    round_trip_sec(INST_DI_INSTALL, DI_JOB_INSTALL, pci,
                   "C:\\XHCI98\\XHCI98.INF", INST_XHCI_UPD_SECTION, 1, 1, 0UL,
                   "the controller's install with its update section");
    round_trip_sec(INST_DI_INSTALL, DI_JOB_INSTALL, pci,
                   "C:\\XHCI98\\XHCI98.INF", INST_XHCI_UPD_SECTION, 0, 0, 0UL,
                   "the update section with no FLAGS line");
    memset(longId, 'I', 200);
    memcpy(longId, "PCI\\", 4);
    longId[200] = '\0';
    round_trip(INST_DI_INSTALL, DI_JOB_INSTALL, longId,
               "C:\\XHCI98\\XHCI98.INF", 1, 1, 1UL,
               "a 200-character id, read by the helper");
}

/* A result file made by the helper's writer, read by the installer. */
static const char *res_text(char *buf, size_t cap, const char *const *lines,
                            int n)
{
    int i;

    buf[0] = '\0';
    for (i = 0; i < n; i++) {
        if (lines[i] != NULL && strlen(buf) + strlen(lines[i]) < cap) {
            strcat(buf, lines[i]);
        }
    }
    return buf;
}

static void test_results(void)
{
    static char lines[16][DI_LINE_MAX];
    const char *ptr[16];
    char text[4096];
    DI_OUT o;
    DI_CMD c;
    INST_DI_RESULT r;
    int n;
    int i;

    memset(&c, 0, sizeof(c));
    c.job = DI_JOB_INSTALL;
    strcpy(c.version, V);
    strcpy(c.result, "C:\\XHCI98\\XHCIDI.RES");
    strcpy(c.devid, rootHub);
    strcpy(c.inf, "C:\\XHCI98\\XHCI98.INF");
    c.flags = DI_FLAG_QUIET;

#define PUT(x) do { strcpy(lines[n], (x)); n++; } while (0)
    n = 0;
    di_out_init(&o);
    PUT(di_out_header(&o, V));
    PUT(di_out_job(&o, &c));
    PUT(di_out_found(&o, 1));
    PUT(di_out_driver(&o, "C:\\XHCI98\\XHCI98.INF", "Xhci98.Hub",
                      "xHCI98 USB Root Hub", 0, 0x5B21U));
    PUT(di_out_node(&o, 0x00000040UL, 0));
    PUT(di_out_default(&o, "DIF_SELECTBESTCOMPATDRV"));
    PUT(di_out_ids(&o, "HardwareID", "USB\\XHCI98_ROOT_HUB"));
    PUT(di_out_call(&o, "DiCallClassInstaller", "DIF_INSTALLDEVICE", 0));
    PUT(di_out_flags(&o, 0x00000080UL));
    PUT(di_out_result(&o, di_verdict(DI_JOB_INSTALL, 0, 0x00000080UL), 0));
    PUT(di_out_end(&o));
    for (i = 0; i < n; i++) {
        ptr[i] = lines[i];
    }
    CHECK(inst_di_parse(res_text(text, sizeof(text), ptr, n), V,
                        INST_DI_INSTALL, rootHub, "C:\\XHCI98\\XHCI98.INF",
                        &r) == INST_DI_V_INSTALLED_RESTART && r.Ended,
          "the helper's installed-restart, with NODE, DEFAULT and IDS lines");

    n = 0;
    di_out_init(&o);
    PUT(di_out_header(&o, V));
    PUT(di_out_job(&o, &c));
    PUT(di_out_found(&o, 1));
    PUT(di_out_call(&o, "DiCallClassInstaller", "DIF_INSTALLDEVICE", 0));
    PUT(di_out_flags(&o, 0UL));
    PUT(di_out_result(&o, di_verdict(DI_JOB_INSTALL, 0, 0UL), 0));
    PUT(di_out_end(&o));
    for (i = 0; i < n; i++) {
        ptr[i] = lines[i];
    }
    CHECK(inst_di_parse(res_text(text, sizeof(text), ptr, n), V,
                        INST_DI_INSTALL, rootHub, "C:\\XHCI98\\XHCI98.INF",
                        &r) == INST_DI_V_INSTALLED,
          "the helper's installed");
    CHECK(inst_di_parse(text, V, INST_DI_INSTALL, rootHub,
                        "C:\\XHCI98\\XHCICM.INF", &r) == INST_DI_V_NONE,
          "another job's file is not taken as this one's");

    c.job = DI_JOB_PROBE;
    n = 0;
    di_out_init(&o);
    PUT(di_out_header(&o, V));
    PUT(di_out_job(&o, &c));
    PUT(di_out_result(&o, "probed", 0));
    PUT(di_out_end(&o));
    for (i = 0; i < n; i++) {
        ptr[i] = lines[i];
    }
    CHECK(inst_di_parse(res_text(text, sizeof(text), ptr, n), V,
                        INST_DI_PROBE, NULL, NULL, &r) == INST_DI_V_PROBED,
          "the helper's probe");

    n = 0;
    di_out_init(&o);
    PUT(di_out_header(&o, V));
    PUT(di_out_refused(&o, "command", "job-file"));
    PUT(di_out_end(&o));
    for (i = 0; i < n; i++) {
        ptr[i] = lines[i];
    }
    CHECK(inst_di_parse(res_text(text, sizeof(text), ptr, n), V,
                        INST_DI_INSTALL, rootHub, "C:\\XHCI98\\XHCI98.INF",
                        &r) == INST_DI_V_REFUSED,
          "the helper's refusal of a job file");

    n = 0;
    di_out_init(&o);
    PUT(di_out_header(&o, "2.4.0.0"));
    PUT(di_out_refused(&o, "version", NULL));
    PUT(di_out_end(&o));
    for (i = 0; i < n; i++) {
        ptr[i] = lines[i];
    }
    CHECK(inst_di_parse(res_text(text, sizeof(text), ptr, n), V,
                        INST_DI_INSTALL, rootHub, "C:\\XHCI98\\XHCI98.INF",
                        &r) == INST_DI_V_REFUSED,
          "the helper's version refusal");
#undef PUT
}

int main(void)
{
    test_jobs();
    test_results();
    printf("test_dijob: %d checks, %d failures\n", checks, failures);
    return failures;
}
