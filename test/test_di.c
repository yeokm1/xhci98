/*
 * test_di.c - host vectors for XHCIDI.EXE's pure part (xhciinst\di\di_core.c;
 * design record 27 section 11; roadmap-usb4.md task 42-A.1): the command
 * line and every refusal of it, the result file's lines and their count,
 * the device id and path rules, the verdicts, and the setup engine's error
 * names and flag values held to the Windows 98 SE DDK's setupx.h.
 *
 * THE HEADER. Where tools\win98ddk-extracted\ holds setupx.h (the member
 * INC_WIN98_INC16_SETUPX.H, SHA-256 aef84421a79596dcf6c7aaa90af03466
 * e73a1a36bf83ae953138a9252421e815 as docs\references\README.md records it)
 * the suite reads its five error enumerations and the flag #defines the
 * helper uses, and requires di_core's table to be them exactly. Where it is
 * not, the suite says so and holds the table to the copy below, read from
 * that header on 2026-10-18: each enumeration's base, length, first and
 * last name.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../xhciinst/di/di_core.h"
#include "test_harness.h"

#define V "3.1.0.0"
#define JOBPATH "C:\\XHCI98\\DIJOB.TXT"

static char jobtext[4096];

/*
 * A job file from words: <job> <result> <hwnd> [<device id> [<inf> [<flag>...]]],
 * each operand on the line record 27 section 4.1 gives it, in order.
 */
static const char *jobfile(const char *words)
{
    char copy[2048];
    char *w[16];
    int n = 0;
    int i;
    char *p;
    strcpy(copy, words);
    for (p = strtok(copy, " \t"); p != NULL && n < 16; p = strtok(NULL, " \t")) {
        w[n++] = p;
    }
    strcpy(jobtext, "XHCIDI-JOB 1\r\n");
    if (n > 0) { strcat(jobtext, "JOB "); strcat(jobtext, w[0]); strcat(jobtext, "\r\n"); }
    if (n > 1) { strcat(jobtext, "RESULT "); strcat(jobtext, w[1]); strcat(jobtext, "\r\n"); }
    if (n > 2) { strcat(jobtext, "HWND "); strcat(jobtext, w[2]); strcat(jobtext, "\r\n"); }
    if (n > 3) { strcat(jobtext, "DEVICE "); strcat(jobtext, w[3]); strcat(jobtext, "\r\n"); }
    if (n > 4) { strcat(jobtext, "INF "); strcat(jobtext, w[4]); strcat(jobtext, "\r\n"); }
    if (n > 5) {
        strcat(jobtext, "FLAGS");
        for (i = 5; i < n; i++) { strcat(jobtext, " "); strcat(jobtext, w[i]); }
        strcat(jobtext, "\r\n");
    }
    strcat(jobtext, "END\r\n");
    return jobtext;
}

static int parse_text(const char *text, DI_CMD *cmd)
{
    return di_parse(V " " JOBPATH, text, V, cmd);
}

static int parse(const char *words, DI_CMD *cmd)
{
    return parse_text(jobfile(words), cmd);
}

static void test_cmdline(void)
{
    char ver[DI_VERSION_MAX + 1];
    char path[DI_JOBPATH_MAX + 1];
    char line[200];
    char longpath[DI_JOBPATH_MAX + 2];
    DI_CMD c;

    CHECK(di_cmdline(V " " JOBPATH, ver, sizeof ver, path, sizeof path), "the command line");
    CHECK(strcmp(ver, V) == 0 && strcmp(path, JOBPATH) == 0, "its two words");
    CHECK(di_cmdline("  " V "\t" JOBPATH " \r\n", ver, sizeof ver, path, sizeof path), "blanks, a tab, a CRLF");
    CHECK(!di_cmdline(V, ver, sizeof ver, path, sizeof path), "one word");
    CHECK(!di_cmdline(V " " JOBPATH " extra", ver, sizeof ver, path, sizeof path), "three words");
    CHECK(!di_cmdline(V " DIJOB.TXT", ver, sizeof ver, path, sizeof path) && path[0] == '\0', "a relative job path");
    CHECK(!di_cmdline(V " C:\\LONGNAME12\\J.TXT", ver, sizeof ver, path, sizeof path), "a job path not 8.3");
    CHECK(!di_cmdline("3.1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0 " JOBPATH, ver, sizeof ver, path, sizeof path),
          "a version longer than 31");

    /* The bound: a job path of 80 characters is taken, 81 is not. */
    strcpy(longpath, "C:\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\J.TXT");
    CHECK_EQ(strlen(longpath), DI_JOBPATH_MAX, "an 80-character job path");
    sprintf(line, V " %s", longpath);
    CHECK(di_cmdline(line, ver, sizeof ver, path, sizeof path), "an 80-character job path is taken");
    CHECK(strlen(line) <= 126, "and the command tail stays under the 126 a 16-bit program is given");
    sprintf(line, "3.1.0.0.0.0.0.0.0.0.0.0.0.0.0.0 %s", longpath);
    CHECK(strlen(line) <= 126 && di_cmdline(line, ver, sizeof ver, path, sizeof path),
          "the longest version and the longest job path: at most 112 characters");
    sprintf(line, V " %s", "C:\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\ABCDEFGH\\JJ.TXT");
    CHECK(!di_cmdline(line, ver, sizeof ver, path, sizeof path), "an 81-character job path is refused");

    CHECK_EQ(di_parse(V, jobfile("probe C:\\X\\R.TXT 0"), V, &c), DI_PARSE_NOFILE, "no job path: no file");
    CHECK(strcmp(c.why, "command-line") == 0, "why: command-line");
    CHECK_EQ(di_parse(V " " JOBPATH, NULL, V, &c), DI_PARSE_NOFILE, "an unreadable job file: no file");
    CHECK(strcmp(c.why, "job-file") == 0, "why: job-file");
}

static void test_parse_ok(void)
{
    DI_CMD c;

    CHECK_EQ(parse("install C:\\XHCI98\\DI1.TXT 1234 "
                   "PCI\\VEN_8086&DEV_31A8&SUBSYS_00000000&REV_03\\BUS_00&DEV_04&FUNC_00 "
                   "C:\\XHCI98\\XHCI98.INF", &c), DI_PARSE_OK, "install parses");
    CHECK_EQ(c.job, DI_JOB_INSTALL, "install: job");
    CHECK_EQ(c.hwnd, 1234, "install: hwnd");
    CHECK(strcmp(c.result, "C:\\XHCI98\\DI1.TXT") == 0, "install: result file");
    CHECK(strcmp(c.inf, "C:\\XHCI98\\XHCI98.INF") == 0, "install: inf");
    CHECK(strcmp(c.devid, "PCI\\VEN_8086&DEV_31A8&SUBSYS_00000000&REV_03\\BUS_00&DEV_04&FUNC_00") == 0,
          "install: device id");
    CHECK_EQ(c.flags, 0, "install: no flags");

    CHECK_EQ(parse("install C:\\X\\R.TXT 0 USB\\XHCI98_ROOT_HUB\\5&1 C:\\X\\XHCIUAS.INF force quiet", &c),
             DI_PARSE_OK, "install with both flags");
    CHECK_EQ(c.flags, DI_FLAG_FORCE | DI_FLAG_QUIET, "both flags");
    CHECK_EQ(c.hwnd, 0, "hwnd 0, the console tool's");

    CHECK_EQ(parse("install C:\\X\\R.TXT 65535 USB\\A\\B C:\\X.INF quiet", &c), DI_PARSE_OK,
             "an INF at a drive's root, hwnd 65535");
    CHECK_EQ(c.flags, DI_FLAG_QUIET, "quiet alone");

    CHECK_EQ(parse("remove C:\\XHCI98\\DI2.TXT 77 USB\\XHCI98_HUB\\6&2&1", &c), DI_PARSE_OK,
             "remove parses");
    CHECK_EQ(c.job, DI_JOB_REMOVE, "remove: job");
    CHECK(strcmp(c.devid, "USB\\XHCI98_HUB\\6&2&1") == 0, "remove: device id");
    CHECK(c.inf[0] == '\0', "remove: no inf");

    CHECK_EQ(parse("probe C:\\XHCI98\\DIPROBE.TXT 0", &c), DI_PARSE_OK, "probe parses");
    CHECK_EQ(c.job, DI_JOB_PROBE, "probe: job");

    CHECK_EQ(parse_text("XHCIDI-JOB 1\nJOB probe\nRESULT C:\\X\\R.TXT\nHWND 0\nEND", &c), DI_PARSE_OK,
             "LF endings and no newline after END");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nEND\r\n\r\n  \r\n", &c),
             DI_PARSE_OK, "blank lines after END");
    CHECK_EQ(parse_text("XHCIDI-JOB  1\r\nJOB\tprobe\r\n RESULT C:\\X\\R.TXT\r\nHWND 0  \r\nEND\r\n", &c),
             DI_PARSE_OK, "blanks and tabs between and around the words");
}

static void test_parse_refused(void)
{
    DI_CMD c;
    const char *dev = " PCI\\VEN_1033&DEV_0194\\3&1 ";

    CHECK_EQ(parse_text("", &c), DI_PARSE_NOFILE, "an empty job file: no file");
    CHECK(strcmp(c.why, "result-file") == 0, "why: result-file");
    CHECK_EQ(parse("probe", &c), DI_PARSE_NOFILE, "no RESULT line: no file");
    CHECK_EQ(parse("probe RESULT.TXT 0", &c), DI_PARSE_NOFILE, "a relative result path: no file");
    CHECK(c.result[0] == '\0', "no result path kept");
    CHECK_EQ(parse("probe C:\\LONGNAME12\\R.TXT 0", &c), DI_PARSE_NOFILE, "a result path not 8.3: no file");
    CHECK_EQ(parse_text("RESULT C:\\X\\R.TXT C:\\Y\\R.TXT\r\n", &c), DI_PARSE_NOFILE, "a RESULT line of two paths");
    CHECK_EQ(parse_text("JOB probe\r\nRESULT C:\\X\\R.TXT\r\n", &c), DI_PARSE_COMMAND,
             "a RESULT line found in a file whose header is missing: reported, then refused");
    CHECK(strcmp(c.why, "job-file") == 0, "why: job-file");
    CHECK(strcmp(c.result, "C:\\X\\R.TXT") == 0, "the result path is kept");

    CHECK_EQ(di_parse("3.0.0.0 " JOBPATH, jobfile("probe C:\\X\\R.TXT 0"), V, &c), DI_PARSE_VERSION, "another version");
    CHECK(strcmp(c.result, "C:\\X\\R.TXT") == 0, "the result file is known to a version refusal");
    CHECK_EQ(c.job, DI_JOB_NONE, "no job read after a version refusal");
    CHECK_EQ(di_parse("3.1.0.0x " JOBPATH, jobfile("probe C:\\X\\R.TXT 0"), V, &c), DI_PARSE_VERSION, "a longer version");
    CHECK_EQ(di_parse("3.1.0 " JOBPATH, jobfile("probe C:\\X\\R.TXT 0"), V, &c), DI_PARSE_VERSION, "a shorter version");
    CHECK_EQ(di_parse("3.0.0.0 " JOBPATH, jobfile("bogus C:\\X\\R.TXT"), V, &c), DI_PARSE_VERSION,
             "the version is compared before the job is read");

    CHECK_EQ(parse_text("XHCIDI-JOB 2\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nEND\r\n", &c),
             DI_PARSE_COMMAND, "another job file version");
    CHECK(strcmp(c.why, "job-file") == 0, "why: job-file");
    CHECK_EQ(parse_text("JOB probe\r\nXHCIDI-JOB 1\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nEND\r\n", &c),
             DI_PARSE_COMMAND, "lines out of order");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\n\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nEND\r\n", &c),
             DI_PARSE_COMMAND, "a blank line before END");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\n", &c),
             DI_PARSE_COMMAND, "no END");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nEND\r\nEND\r\n", &c),
             DI_PARSE_COMMAND, "a line after END");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r extra\r\nEND\r\n", &c),
             DI_PARSE_COMMAND, "a CR inside a line hides nothing");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nEND\rJUNK\r\n", &c),
             DI_PARSE_COMMAND, "content after END behind a CR");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nEND\r\n\x01JOB remove\r\n", &c),
             DI_PARSE_COMMAND, "a NUL the reader made 0x01, then more records");
    CHECK(strcmp(c.result, "C:\\X\\R.TXT") == 0, "the refusal still has its result path");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\x7F\r\nEND\r\n", &c),
             DI_PARSE_COMMAND, "a DEL byte in a record");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nEND\r", &c),
             DI_PARSE_OK, "a CR at the file's end");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nEND now\r\n", &c),
             DI_PARSE_COMMAND, "END with a word");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\njob probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nEND\r\n", &c),
             DI_PARSE_COMMAND, "a key in lower case");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nFLAGS quiet\r\nEND\r\n", &c),
             DI_PARSE_COMMAND, "FLAGS on a probe");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB remove\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nDEVICE USB\\A\\B\r\n"
                        "FLAGS force\r\nEND\r\n", &c), DI_PARSE_COMMAND, "FLAGS on a removal");

    CHECK_EQ(parse("Install C:\\X\\R.TXT 0 USB\\A\\B C:\\X\\A.INF", &c), DI_PARSE_COMMAND, "a job word in another case");
    CHECK(strcmp(c.why, "job") == 0, "why: job");
    CHECK_EQ(parse("probe C:\\X\\R.TXT", &c), DI_PARSE_COMMAND, "probe with no hwnd");
    CHECK(strcmp(c.why, "job-file") == 0, "why: job-file");
    CHECK_EQ(parse("probe C:\\X\\R.TXT 0 extra", &c), DI_PARSE_COMMAND, "probe with a device");
    CHECK_EQ(parse("remove C:\\X\\R.TXT 0", &c), DI_PARSE_COMMAND, "remove with no device");
    CHECK_EQ(parse("remove C:\\X\\R.TXT 0 USB\\A\\B C:\\X\\A.INF", &c), DI_PARSE_COMMAND, "remove with an INF");
    CHECK_EQ(parse("install C:\\X\\R.TXT 0 USB\\A\\B", &c), DI_PARSE_COMMAND, "install with no INF");
    CHECK_EQ(parse("install C:\\X\\R.TXT 0 USB\\A\\B C:\\X\\A.INF force quiet force", &c), DI_PARSE_COMMAND,
             "install with three flags");
    CHECK(strcmp(c.why, "flags") == 0, "why: flags");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB install\r\nRESULT C:\\X\\R.TXT\r\nHWND 0\r\nDEVICE USB\\A\\B\r\n"
                        "INF C:\\X\\A.INF\r\nFLAGS\r\nEND\r\n", &c), DI_PARSE_COMMAND, "a FLAGS line with none");
    CHECK_EQ(parse_text("XHCIDI-JOB 1\r\nJOB probe\r\nRESULT C:\\X\\R.TXT\r\nHWND 0 1\r\nEND\r\n", &c),
             DI_PARSE_COMMAND, "an HWND line of two words");

    CHECK_EQ(parse("probe C:\\X\\R.TXT -1", &c), DI_PARSE_COMMAND, "a negative hwnd");
    CHECK(strcmp(c.why, "hwnd") == 0, "why: hwnd");
    CHECK_EQ(parse("probe C:\\X\\R.TXT 65536", &c), DI_PARSE_COMMAND, "an hwnd above 16 bits");
    CHECK_EQ(parse("probe C:\\X\\R.TXT 0012", &c), DI_PARSE_COMMAND, "an hwnd with leading zeros");
    CHECK_EQ(parse("probe C:\\X\\R.TXT 0x12", &c), DI_PARSE_COMMAND, "an hwnd in hex");
    CHECK_EQ(parse("probe C:\\X\\R.TXT 123456", &c), DI_PARSE_COMMAND, "an hwnd of six digits");

    {
        char line[600];
        char longid[DI_DEVID_MAX + 2];
        memset(longid, 'A', sizeof longid - 1);
        longid[sizeof longid - 1] = '\0';
        longid[3] = '\\';
        sprintf(line, "remove C:\\X\\R.TXT 0 %s", longid);
        CHECK_EQ(parse(line, &c), DI_PARSE_COMMAND, "a device id of 201 characters");
        CHECK(strcmp(c.why, "device-id") == 0, "why: device-id");
        longid[DI_DEVID_MAX] = '\0';
        sprintf(line, "remove C:\\X\\R.TXT 0 %s", longid);
        CHECK_EQ(parse(line, &c), DI_PARSE_OK, "a device id of 200 characters");
    }
    CHECK_EQ(parse("remove C:\\X\\R.TXT 0 NOBACKSLASH", &c), DI_PARSE_COMMAND, "a device id of one part");
    CHECK_EQ(parse("remove C:\\X\\R.TXT 0 \\USB\\A", &c), DI_PARSE_COMMAND, "a device id with no enumerator");
    CHECK_EQ(parse("remove C:\\X\\R.TXT 0 USB\\\\A", &c), DI_PARSE_COMMAND, "a device id with an empty part");
    CHECK_EQ(parse("remove C:\\X\\R.TXT 0 USB\\A\\", &c), DI_PARSE_COMMAND, "a device id ending in a backslash");
    CHECK_EQ(parse("remove C:\\X\\R.TXT 0 US&B\\A", &c), DI_PARSE_COMMAND, "an enumerator with a symbol");
    CHECK_EQ(parse("remove C:\\X\\R.TXT 0 USB\\A\"B", &c), DI_PARSE_COMMAND, "a device id with a quote");

    {
        char line[300];
        sprintf(line, "install C:\\X\\R.TXT 0%sC:\\X\\A.TXT", dev);
        CHECK_EQ(parse(line, &c), DI_PARSE_COMMAND, "an INF path not ending .INF");
        CHECK(strcmp(c.why, "inf-path") == 0, "why: inf-path");
        sprintf(line, "install C:\\X\\R.TXT 0%sC:\\PROGRAM~1\\XHCI98LONG.INF", dev);
        CHECK_EQ(parse(line, &c), DI_PARSE_COMMAND, "an INF name of nine characters");
        sprintf(line, "install C:\\X\\R.TXT 0%sX98.INF", dev);
        CHECK_EQ(parse(line, &c), DI_PARSE_COMMAND, "an INF path with no drive");
        sprintf(line, "install C:\\X\\R.TXT 0%sC:\\A.B.INF", dev);
        CHECK_EQ(parse(line, &c), DI_PARSE_COMMAND, "an INF name with two dots");
        sprintf(line, "install C:\\X\\R.TXT 0%sC:\\X\\A.INF bogus", dev);
        CHECK_EQ(parse(line, &c), DI_PARSE_COMMAND, "an unknown flag");
        CHECK(strcmp(c.why, "flags") == 0, "why: flags");
        sprintf(line, "install C:\\X\\R.TXT 0%sC:\\X\\A.INF force force", dev);
        CHECK_EQ(parse(line, &c), DI_PARSE_COMMAND, "a flag twice");
        sprintf(line, "install C:\\X\\R.TXT 0%sC:\\X\\A.INF FORCE", dev);
        CHECK_EQ(parse(line, &c), DI_PARSE_COMMAND, "a flag in capitals");
    }
}

static void test_paths(void)
{
    char big[300];
    unsigned i;

    CHECK(di_path_ok("C:\\XHCI98\\XHCI98.INF", 1), "the install directory's INF");
    CHECK(di_path_ok("c:\\xhci98\\xhci98.inf", 1), "lower case");
    CHECK(di_path_ok("D:\\A\\B\\C\\D\\E.INF", 1), "deep");
    CHECK(di_path_ok("C:\\XHCI98\\DIRESULT", 0), "a result file with no extension");
    CHECK(di_path_ok("C:\\X~1\\R_1-2.TXT", 0), "a tilde, an underscore and a hyphen");
    CHECK(di_path_ok("C:\\O'NEIL\\XHCI98.INF", 1), "an apostrophe, which the installer's 8.3 rule takes");
    CHECK(di_path_ok("C:\\$!#&@^()\\{}.INF", 1), "every other symbol the installer's rule takes");
    CHECK(!di_path_ok("C:\\XHCI98\\DIRESULT", 1), "no extension is no INF");
    CHECK(!di_path_ok("C:\\", 0), "a drive's root alone");
    CHECK(!di_path_ok("C:\\X\\", 0), "a trailing backslash");
    CHECK(!di_path_ok("C:\\X\\\\A.INF", 1), "an empty part");
    CHECK(!di_path_ok("C:X\\A.INF", 1), "no backslash after the drive");
    CHECK(!di_path_ok("1:\\A.INF", 1), "a digit for a drive");
    CHECK(!di_path_ok("\\\\SERVER\\SHARE\\A.INF", 1), "a UNC path");
    CHECK(!di_path_ok("C:\\X\\A.INFO", 1), "a four-letter extension");
    CHECK(!di_path_ok("C:\\X\\.INF", 1), "an empty name");
    CHECK(!di_path_ok("C:\\X\\A.", 0), "an empty extension");
    CHECK(!di_path_ok("C:\\MY DIR\\A.INF", 1), "a space");
    CHECK(!di_path_ok("C:\\X\\A%B.INF", 1), "a percent sign");
    CHECK(!di_path_ok("C:\\X\\A+B.INF", 1), "a plus");
    CHECK(!di_path_ok("C:\\X\\..\\A.INF", 1), "a parent reference");
    CHECK(!di_path_ok("C:\\X\\A\xE9.INF", 1), "a byte above 0x7F");

    strcpy(big, "C:");
    for (i = 0; i < 31; i++) {
        strcat(big, "\\ABCDEFGH");
    }
    strcat(big, "\\A.INF");
    CHECK(strlen(big) > DI_PATH_MAX, "the long path is long");
    CHECK(!di_path_ok(big, 1), "a path over 255 characters");
    big[DI_PATH_MAX - 6] = '\0';
    strcat(big, "\\A.INF");
    CHECK(strlen(big) == DI_PATH_MAX && di_path_ok(big, 1), "a path of 255 characters");
}

static void test_ids(void)
{
    char buf[40];

    CHECK(di_devid_ok("PCI\\VEN_8086&DEV_15EC&SUBSYS_00000000&REV_06\\BUS_06&DEV_00&FUNC_00"), "a PCI id");
    CHECK(di_devid_ok("USB\\XHCI98_ROOT_HUB\\5&2A3B&0"), "the root hub's id");
    CHECK(!di_devid_ok(""), "an empty id");
    CHECK(!di_devid_ok("USB\\A B"), "a space");
    CHECK(!di_devid_ok("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEF\\A"), "an enumerator of 32 characters");
    CHECK(di_devid_ok("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDE\\A"), "an enumerator of 31 characters");

    CHECK(di_enumerator("PCI\\VEN_1\\X", buf, sizeof buf) && strcmp(buf, "PCI") == 0, "the enumerator");
    CHECK(!di_enumerator("PCI", buf, sizeof buf) && buf[0] == '\0', "no enumerator in one part");
    CHECK(!di_enumerator("PCI\\X", buf, 3), "an enumerator that does not fit");

    CHECK(di_subkey_matches("Enum\\PCI\\VEN_8086&DEV_31A8\\BUS_00&DEV_04&FUNC_00",
                            "pci\\ven_8086&dev_31a8\\bus_00&dev_04&func_00"), "Enum\\<id>, without case");
    CHECK(di_subkey_matches("ENUM\\USB\\A\\B", "USB\\A\\B"), "ENUM in capitals");
    CHECK(!di_subkey_matches("Enum\\USB\\A\\B2", "USB\\A\\B"), "a longer instance");
    CHECK(!di_subkey_matches("Enum\\USB\\A", "USB\\A\\B"), "a shorter subkey");
    CHECK(!di_subkey_matches("USB\\A\\B", "USB\\A\\B"), "no Enum prefix");
    CHECK(!di_subkey_matches("Enum\\", ""), "an empty id");
    CHECK(!di_subkey_matches("Enumx\\USB\\A\\B", "USB\\A\\B"), "a prefix that is not Enum\\");
}

static void test_inf_helpers(void)
{
    char d[64];

    CHECK(di_inf_dir("C:\\XHCI98\\XHCI98.INF", d, sizeof d) && strcmp(d, "C:\\XHCI98") == 0, "the INF's directory");
    CHECK(di_inf_dir("C:\\A\\B\\C.INF", d, sizeof d) && strcmp(d, "C:\\A\\B") == 0, "a deeper directory");
    CHECK(di_inf_dir("C:\\X.INF", d, sizeof d) && strcmp(d, "C:\\") == 0, "a drive's root keeps its backslash");
    CHECK(!di_inf_dir("X.INF", d, sizeof d) && d[0] == '\0', "no directory");
    CHECK(!di_inf_dir("C:\\XHCI98\\XHCI98.INF", d, 5), "a directory that does not fit");

    CHECK(strcmp(di_basename("C:\\XHCI98\\XHCICM.INF"), "XHCICM.INF") == 0, "basename");
    CHECK(strcmp(di_basename("XHCICM.INF"), "XHCICM.INF") == 0, "basename of a name");
    CHECK(strcmp(di_basename("C:X.INF"), "X.INF") == 0, "basename after a drive");

    CHECK(di_same_inf("XHCI98.INF", "C:\\XHCI98\\XHCI98.INF"), "a bare name against the path");
    CHECK(di_same_inf("xhci98.inf", "C:\\XHCI98\\XHCI98.INF"), "without case");
    CHECK(di_same_inf("C:\\XHCI98\\XHCI98.INF", "c:\\xhci98\\xhci98.inf"), "a whole path");
    CHECK(!di_same_inf("C:\\WINDOWS\\INF\\OTHER\\XHCI98.INF", "C:\\XHCI98\\XHCI98.INF"),
          "the same name in another directory");
    CHECK(!di_same_inf("USB.INF", "C:\\XHCI98\\XHCI98.INF"), "another INF");
    CHECK(!di_same_inf("", "C:\\XHCI98\\XHCI98.INF"), "an empty atom");
}

static void test_system(void)
{
    CHECK(di_system_supported("4.10.2222"), "98 SE");
    CHECK(di_system_supported("4.10.2222 "), "98 SE with a trailing blank");
    CHECK(di_system_supported(" 4.90.3000"), "ME with a leading blank");
    CHECK(!di_system_supported("4.10.1998"), "98's first edition");
    CHECK(!di_system_supported("4.00.950"), "95");
    CHECK(!di_system_supported("4.10.22220"), "a longer build");
    CHECK(!di_system_supported("4.10.2222.1"), "a further part");
    CHECK(!di_system_supported("4.90.300"), "a shorter build");
    CHECK(!di_system_supported(""), "nothing read");
}

static void test_verdicts(void)
{
    CHECK(strcmp(di_verdict(DI_JOB_INSTALL, 0, 0), "installed") == 0, "installed");
    CHECK(strcmp(di_verdict(DI_JOB_INSTALL, 0, DI_FL_NEEDRESTART), "installed-restart") == 0, "restart");
    CHECK(strcmp(di_verdict(DI_JOB_INSTALL, 0, DI_FL_NEEDREBOOT | 0x10UL), "installed-restart") == 0, "reboot");
    CHECK(strcmp(di_verdict(DI_JOB_INSTALL, 0, DI_FL_ENUMSINGLEINF | DI_FL_QUIETINSTALL), "installed") == 0,
          "other flags are no restart");
    CHECK(strcmp(di_verdict(DI_JOB_INSTALL, DI_ERR_USER_CANCEL, 0), "cancelled") == 0, "cancelled");
    CHECK(strcmp(di_verdict(DI_JOB_INSTALL, 510, DI_FL_NEEDRESTART), "failed") == 0, "failed, whatever the flags");
    CHECK(strcmp(di_verdict(DI_JOB_INSTALL, DI_ERR_DO_DEFAULT, 0), "failed") == 0,
          "ERR_DI_DO_DEFAULT reaching the caller is a failure");
    CHECK(strcmp(di_verdict(DI_JOB_REMOVE, 0, 0), "removed") == 0, "removed");
    CHECK(strcmp(di_verdict(DI_JOB_REMOVE, 0, DI_FL_NEEDREBOOT), "removed-restart") == 0, "removed-restart");
    CHECK(strcmp(di_verdict(DI_JOB_REMOVE, DI_ERR_USER_CANCEL, 0), "cancelled") == 0, "removal cancelled");
    CHECK(strcmp(di_verdict(DI_JOB_REMOVE, 515, 0), "failed") == 0, "removal failed");
    CHECK(strcmp(di_verdict(DI_JOB_PROBE, 0, 0), "failed") == 0, "no engine verdict for a probe");
    CHECK(strcmp(di_verdict(DI_JOB_NONE, 0, 0), "failed") == 0, "no verdict without a job");
}

static void test_errnames(void)
{
    CHECK(strcmp(di_err_name(0), "OK") == 0, "0 is OK");
    CHECK(strcmp(di_err_name(507), "ERR_DI_USER_CANCEL") == 0, "507");
    CHECK(strcmp(di_err_name(524), "ERR_DI_DONT_INSTALL") == 0, "524");
    CHECK(strcmp(di_err_name(520), "ERR_DI_BAD_UNREMOVEDEVICE_PARAMS") == 0,
          "520 follows ERR_DI_BAD_PATH in the enumeration, not the documentation block's order");
    CHECK(strcmp(di_err_name(321), "ERR_VCP_LDDPATH_INVALID") == 0, "321");
    CHECK(strcmp(di_err_name(105), "ERR_IP_INF_NOT_FOUND") == 0, "105");
    CHECK(strcmp(di_err_name(528), "ERR_UNKNOWN") == 0, "past the last");
    CHECK(strcmp(di_err_name(100), "ERR_UNKNOWN") == 0, "IP_ERROR itself is no value");
    CHECK(strcmp(di_err_name(65535U), "ERR_UNKNOWN") == 0, "0xFFFF");
}

/* ---- the header -------------------------------------------------------- */

static char *load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

typedef struct {
    char name[64];
    unsigned value;
} HDR_ERR;

static HDR_ERR hdr[200];
static int hdr_count;

static long hdr_define(const char *text, const char *name)
{
    const char *p = text;
    size_t n = strlen(name);
    while ((p = strstr(p, "#define")) != NULL) {
        const char *q = p + 7;
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        if (strncmp(q, name, n) == 0 && (q[n] == ' ' || q[n] == '\t')) {
            q += n;
            while (*q == ' ' || *q == '\t' || *q == '(') {
                q++;
            }
            return strtol(q, NULL, 0);
        }
        p += 7;
    }
    return -1;
}

/*
 * The five enumerations: an entry "ERR_x = (BASE + 1)" or "= BASE+1" or
 * "= BASE" opens a run, each later "ERR_y," inside the braces counts on.
 */
static void hdr_read(const char *text)
{
    static const char *const bases[5] = { "IP_ERROR", "TP_ERROR", "VCP_ERROR", "GEN_ERROR", "DI_ERROR" };
    const char *p = text;
    hdr_count = 0;
    while ((p = strstr(p, "enum")) != NULL) {
        const char *open = strchr(p, '{');
        const char *close = open != NULL ? strchr(open, '}') : NULL;
        const char *q;
        long cur = -1;
        if (open == NULL || close == NULL) {
            break;
        }
        q = open + 1;
        while (q < close) {
            const char *line_end = q;
            while (line_end < close && *line_end != '\n') {
                line_end++;
            }
            while (q < line_end && (*q == ' ' || *q == '\t')) {
                q++;
            }
            if (strncmp(q, "ERR_", 4) == 0) {
                char name[64];
                int k = 0;
                const char *eq;
                while (q < line_end && k < 63 && (isalnum((unsigned char)*q) || *q == '_')) {
                    name[k++] = *q++;
                }
                name[k] = '\0';
                eq = q;
                while (eq < line_end && (*eq == ' ' || *eq == '\t')) {
                    eq++;
                }
                if (eq < line_end && *eq == '=') {
                    int b;
                    cur = -1;
                    for (b = 0; b < 5; b++) {
                        const char *at = strstr(eq, bases[b]);
                        if (at != NULL && at < line_end) {
                            long base = hdr_define(text, bases[b]);
                            const char *plus = strchr(at, '+');
                            cur = base;
                            if (plus != NULL && plus < line_end) {
                                cur += strtol(plus + 1, NULL, 0);
                            }
                        }
                    }
                } else if (cur >= 0) {
                    cur++;
                }
                if (cur >= 0 && hdr_count < 200) {
                    strcpy(hdr[hdr_count].name, name);
                    hdr[hdr_count].value = (unsigned)cur;
                    hdr_count++;
                }
            }
            q = line_end + 1;
        }
        p = close;
    }
}

static void test_header(void)
{
    static const char *const paths[2] = {
        "..\\tools\\win98ddk-extracted\\all\\INCS_DDK\\INC_WIN98_INC16_SETUPX.H",
        "..\\tools\\win98ddk-extracted\\incs\\INC_WIN98_INC16_SETUPX.H"
    };
    char *text = NULL;
    unsigned i;
    int k;

    for (k = 0; k < 2 && text == NULL; k++) {
        text = load(paths[k]);
    }
    if (text == NULL) {
        /*
         * The recorded copy: base, length, first and last name of each
         * enumeration, read from the header at the SHA-256 above.
         */
        static const struct { unsigned first; unsigned count; const char *a; const char *z; } rec[5] = {
            { 101, 16, "ERR_IP_INVALID_FILENAME", "ERR_IP_INVALID_INFTYPE" },
            { 201, 6, "ERR_TP_NOT_FOUND", "ERR_TP_INVALID_LINE" },
            { 301, 25, "ERR_VCP_IOFAIL", "ERR_VCP_NO_DIGITAL_SIGNATURE_FILE" },
            { 401, 18, "ERR_GEN_LOW_MEM", "ERR_GEN_BITREG" },
            { 500, 28, "ERR_DI_INVALID_DEVICE_ID", "ERR_DI_NO_DIGITAL_SIGNATURE_FILE" }
        };
        unsigned total = 1;
        printf("test_di: setupx.h is not under tools\\win98ddk-extracted; the table is held to the recorded copy\n");
        for (k = 0; k < 5; k++) {
            total += rec[k].count;
            CHECK(strcmp(di_err_name(rec[k].first), rec[k].a) == 0, "recorded: an enumeration's first name");
            CHECK(strcmp(di_err_name(rec[k].first + rec[k].count - 1), rec[k].z) == 0,
                  "recorded: an enumeration's last name");
            CHECK(strcmp(di_err_name(rec[k].first + rec[k].count), "ERR_UNKNOWN") == 0,
                  "recorded: nothing past an enumeration's end");
        }
        CHECK_EQ(di_errname_count, total, "recorded: the table's length");
        return;
    }

    hdr_read(text);
    CHECK_EQ(hdr_count, di_errname_count - 1, "the header's five enumerations hold as many values as the table");
    for (k = 0; k < hdr_count; k++) {
        char what[160];
        sprintf(what, "the header's %s (%u) is the table's", hdr[k].name, hdr[k].value);
        CHECK(strcmp(di_err_name(hdr[k].value), hdr[k].name) == 0, what);
    }
    for (i = 1; i < di_errname_count; i++) {
        int seen = 0;
        for (k = 0; k < hdr_count; k++) {
            if (hdr[k].value == di_errnames[i].value && strcmp(hdr[k].name, di_errnames[i].name) == 0) {
                seen = 1;
            }
        }
        CHECK(seen, di_errnames[i].name);
    }

    CHECK_EQ(hdr_define(text, "DI_NEEDRESTART"), DI_FL_NEEDRESTART, "DI_NEEDRESTART");
    CHECK_EQ(hdr_define(text, "DI_NEEDREBOOT"), DI_FL_NEEDREBOOT, "DI_NEEDREBOOT");
    CHECK_EQ(hdr_define(text, "DI_ENUMSINGLEINF"), DI_FL_ENUMSINGLEINF, "DI_ENUMSINGLEINF");
    CHECK_EQ(hdr_define(text, "DI_CLASSINSTALLPARAMS"), DI_FL_CLASSINSTALLPARAMS, "DI_CLASSINSTALLPARAMS");
    CHECK_EQ(hdr_define(text, "DI_QUIETINSTALL"), DI_FL_QUIETINSTALL, "DI_QUIETINSTALL");
    CHECK_EQ(hdr_define(text, "DNF_OLDDRIVER"), DI_DNF_OLDDRIVER, "DNF_OLDDRIVER");
    CHECK_EQ(hdr_define(text, "DNF_CURRENT_DRIVER"), DI_DNF_CURRENT_DRIVER, "DNF_CURRENT_DRIVER");
    CHECK_EQ(hdr_define(text, "DI_ERROR"), 500, "DI_ERROR");
    CHECK_EQ((unsigned)DI_ERR_DO_DEFAULT, 506, "ERR_DI_DO_DEFAULT's place");
    CHECK(strcmp(di_err_name(DI_ERR_DO_DEFAULT), "ERR_DI_DO_DEFAULT") == 0, "DI_ERR_DO_DEFAULT");
    CHECK(strcmp(di_err_name(DI_ERR_USER_CANCEL), "ERR_DI_USER_CANCEL") == 0, "DI_ERR_USER_CANCEL");
    CHECK(strcmp(di_err_name(DI_ERR_DONT_INSTALL), "ERR_DI_DONT_INSTALL") == 0, "DI_ERR_DONT_INSTALL");
    free(text);
}

/* ---- the result file --------------------------------------------------- */

static char file[4096];

static void put(const char *line)
{
    strcat(file, line);
}

static void test_writer(void)
{
    DI_OUT o;
    DI_CMD c;

    file[0] = '\0';
    di_out_init(&o);
    CHECK_EQ(parse("install C:\\XHCI98\\DI1.TXT 0 PCI\\VEN_8086&DEV_15EC\\BUS_00&DEV_0D&FUNC_00 "
                   "C:\\XHCI98\\XHCICM.INF quiet", &c), DI_PARSE_OK, "the record's example parses");
    put(di_out_header(&o, V));
    put(di_out_job(&o, &c));
    put(di_out_found(&o, 1));
    put(di_out_driver(&o, "C:\\XHCI98\\XHCICM.INF", "XhciCm.Install", "xHCI98 Thunderbolt 3 Controller",
                      0, (unsigned)(((2026U - 1980U) << 9) | (10U << 5) | 18U)));
    put(di_out_node(&o, 0x00000040UL, 0));
    put(di_out_call(&o, "DiBuildCompatDrvList", NULL, 0));
    put(di_out_call(&o, "DiCallClassInstaller", "DIF_INSTALLDEVICE", 0));
    put(di_out_flags(&o, 0x00800080UL));
    put(di_out_result(&o, di_verdict(DI_JOB_INSTALL, 0, 0x00800080UL), 0));
    put(di_out_end(&o));
    CHECK(strcmp(file,
                 "XHCIDI 3.1.0.0\r\n"
                 "JOB install PCI\\VEN_8086&DEV_15EC\\BUS_00&DEV_0D&FUNC_00 C:\\XHCI98\\XHCICM.INF\r\n"
                 "FOUND 1\r\n"
                 "DRIVER XHCICM.INF XhciCm.Install \"xHCI98 Thunderbolt 3 Controller\" rank 0 date 2026-10-18\r\n"
                 "NODE 00000040\r\n"
                 "CALL DiBuildCompatDrvList 0\r\n"
                 "CALL DiCallClassInstaller DIF_INSTALLDEVICE 0\r\n"
                 "FLAGS 00800080\r\n"
                 "RESULT installed-restart\r\n"
                 "END 9\r\n") == 0, "the record's example file, line for line");
    CHECK(!o.overflow, "nothing cut");

    file[0] = '\0';
    di_out_init(&o);
    put(di_out_header(&o, V));
    put(di_out_refused(&o, "version", NULL));
    put(di_out_end(&o));
    CHECK(strcmp(file, "XHCIDI 3.1.0.0\r\nRESULT refused version\r\nEND 2\r\n") == 0, "a version refusal");

    /* Each refusal's reason and detail are separate fields (section 4.4). */
    file[0] = '\0';
    di_out_init(&o);
    CHECK_EQ(parse("probe C:\\X\\R.TXT 99999", &c), DI_PARSE_COMMAND, "a refused hwnd");
    put(di_out_header(&o, V));
    put(di_out_refused(&o, "command", c.why));
    put(di_out_end(&o));
    CHECK(strcmp(file, "XHCIDI 3.1.0.0\r\nRESULT refused command hwnd\r\nEND 2\r\n") == 0,
          "a command refusal: no JOB line, the reason and what was refused");
    di_out_init(&o);
    CHECK(strcmp(di_out_refused(&o, "system", "4.10.1998"), "RESULT refused system 4.10.1998\r\n") == 0,
          "a system refusal with the build string");
    CHECK(strcmp(di_out_refused(&o, "system", "4.10 A"), "RESULT refused system 4.10_A\r\n") == 0,
          "a build string with a space stays one field");
    CHECK(strcmp(di_out_refused(&o, "system", ""), "RESULT refused system\r\n") == 0, "no detail");

    file[0] = '\0';
    di_out_init(&o);
    CHECK_EQ(parse("remove C:\\X\\R.TXT 9 USB\\XHCI98_HUB\\1", &c), DI_PARSE_OK, "remove parses");
    put(di_out_header(&o, V));
    put(di_out_job(&o, &c));
    put(di_out_found(&o, 1));
    put(di_out_call(&o, "DiCallClassInstaller", "DIF_REMOVE", 515));
    put(di_out_flags(&o, 0x00100000UL));
    put(di_out_error(&o, di_err_name(515), 515));
    put(di_out_result(&o, di_verdict(DI_JOB_REMOVE, 515, 0), 515));
    put(di_out_end(&o));
    CHECK(strcmp(file,
                 "XHCIDI 3.1.0.0\r\n"
                 "JOB remove USB\\XHCI98_HUB\\1\r\n"
                 "FOUND 1\r\n"
                 "CALL DiCallClassInstaller DIF_REMOVE 515\r\n"
                 "FLAGS 00100000\r\n"
                 "ERROR ERR_DI_BAD_REMOVEDEVICE_PARAMS 515\r\n"
                 "RESULT failed ERR_DI_BAD_REMOVEDEVICE_PARAMS 515\r\n"
                 "END 7\r\n") == 0, "a failed removal");

    file[0] = '\0';
    di_out_init(&o);
    CHECK_EQ(parse("probe C:\\X\\R.TXT 0", &c), DI_PARSE_OK, "probe parses");
    put(di_out_header(&o, V));
    put(di_out_job(&o, &c));
    put(di_out_result_text(&o, DI_V_PROBED, NULL));
    put(di_out_end(&o));
    CHECK(strcmp(file, "XHCIDI 3.1.0.0\r\nJOB probe\r\nRESULT probed\r\nEND 3\r\n") == 0, "a probe");

    di_out_init(&o);
    CHECK(strcmp(di_out_default(&o, "DIF_SELECTBESTCOMPATDRV"), "DEFAULT DIF_SELECTBESTCOMPATDRV lowest-rank\r\n") == 0,
          "the helper's own selection after ERR_DI_DO_DEFAULT (record 27 section 5.2)");

    di_out_init(&o);
    CHECK(strcmp(di_out_result_text(&o, DI_V_FAILED, DI_SELF_OTHERINF), "RESULT failed XHCIDI_OTHER_INF 0\r\n") == 0,
          "a failure of the helper's own");
    CHECK(strcmp(di_out_result(&o, DI_V_NOT_PRESENT, 0), "RESULT not-present\r\n") == 0, "not-present");
    CHECK(strcmp(di_out_result(&o, DI_V_FAILED, 9999), "RESULT failed ERR_UNKNOWN 9999\r\n") == 0, "an unknown error");
    CHECK(strcmp(di_out_ids(&o, "HardwareID", "USB\\VID_0BDA&PID_8153,USB\\X Y"),
                 "IDS HardwareID USB\\VID_0BDA&PID_8153,USB\\X_Y\r\n") == 0, "an id list with a space");
    CHECK(strcmp(di_out_ids(&o, "CompatibleIDs", ""), "IDS CompatibleIDs -\r\n") == 0, "no compatible ids");
    CHECK(strcmp(di_out_driver(&o, "", "", "A \"q\"\tz", 65535U, 0), "DRIVER - - \"A _q__z\" rank 65535 date -\r\n") == 0,
          "an empty node: dashes, a quote and a tab made safe, no date");
    CHECK(strcmp(di_out_call(&o, "CtlSetLddPath", "D:\\WIN 98", 0), "CALL CtlSetLddPath D:\\WIN_98 0\r\n") == 0,
          "a path with a space in a bare field");
    CHECK(strcmp(di_out_flags(&o, 0xFFFFFFFFUL), "FLAGS FFFFFFFF\r\n") == 0, "every flag");
    CHECK(strcmp(di_out_node(&o, 0x102UL, 1), "NODE 00000102 force\r\n") == 0, "a node with force");
    CHECK_EQ(o.count, 9, "each line counted");
    CHECK(strcmp(di_out_end(&o), "END 9\r\n") == 0, "END counts the lines before it");

    {
        static char huge[DI_LINE_MAX + 10];
        memset(huge, 'x', sizeof huge - 1);
        huge[sizeof huge - 1] = '\0';
        di_out_init(&o);
        CHECK(strcmp(di_out_call(&o, "R", huge, 7), "CALL R 7\r\n") == 0, "a field too long is left out whole");
        CHECK(o.overflow, "and the overflow said");
        CHECK_EQ(o.count, 1, "the line is still counted");
    }
}

int main(void)
{
    test_cmdline();
    test_parse_ok();
    test_parse_refused();
    test_paths();
    test_ids();
    test_inf_helpers();
    test_system();
    test_verdicts();
    test_errnames();
    test_header();
    test_writer();
    printf("test_di: %d checks, %d failures\n", checks, failures);
    return failures;
}
