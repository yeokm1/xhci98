/*
 * xhciinst.c - XHCIINST.EXE (32-bit) and XHCIIN64.EXE (x64): the console
 * front end of the install, update and uninstall tool (design record 23;
 * record 26 section 3, roadmap-usb4.md tasks 40.4 to 40.6 and 42-A.2).
 *
 * WHY IT EXISTS. The window (xhciwiz.c) is what INSTALL.BAT starts; this
 * console program stays for scripts, the F6 floppy's companion steps, the
 * legs and unattended installs (INSTALL /console /yes /set ...). Both run
 * the one engine, inst_eng.c, which reports through the table this file
 * fills: every line through the console's 79-column word wrap (record 23
 * section 7.6), every question asked at the keyboard with its "Enter for
 * [x]", the log fed the same lines.
 *
 * TARGETS AND BUILDS. ANSI C89, built with the in-repo MSVC 6.0 as
 * XHCIINST.EXE and with WDK 7.1's amd64 compiler as XHCIIN64.EXE
 * (build.cmd). The import table names KERNEL32 and ADVAPI32 alone
 * (selftest.cmd holds it there): every SetupAPI, newdev and configuration
 * manager routine is the engine's, looked up at run time. The 32-bit
 * build under WOW64 refuses and names XHCIIN64.EXE.
 */

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inst_core.h"
#include "inst_eng.h"

#ifdef _WIN64
#define INST_MY_ARCH        INST_ARCH_AMD64
#else
#define INST_MY_ARCH        INST_ARCH_X86
#endif

/* The console's word wrap (owner, 2026-10-16: no line wider than 79
 * columns; inst_core.h, "the console"). Everything printed goes through
 * it, and the log gets what was printed. */
static INST_WRAP outWrap;
static int outMirror;       /* the text to standard error too (out_mirror) */

/* 1 when the standard handle is the console (a console's mode can be
 * read; a file's, a pipe's or NUL's cannot). */
static int std_is_console(DWORD which)
{
    HANDLE h = GetStdHandle(which);
    DWORD mode;

    return h != NULL && h != INVALID_HANDLE_VALUE &&
           GetConsoleMode(h, &mode) != 0;
}

static void emit(const char *text)
{
    if (text[0] == '\0') {
        return;
    }
    fputs(text, stdout);
    fflush(stdout);
    if (outMirror) {
        fputs(text, stderr);
        fflush(stderr);
    }
    inst_eng_log_text(text);
}

/* Line: the text wrapped and printed; an empty text flushes what the wrap
 * holds back (a prompt's last word and its space). */
static void con_line(void *ctx, const char *text)
{
    char wrapped[16384];

    (void)ctx;
    if (text[0] == '\0') {
        inst_wrap_flush(&outWrap, wrapped, sizeof(wrapped));
    } else {
        inst_wrap_feed(&outWrap, text, wrapped, sizeof(wrapped));
    }
    emit(wrapped);
}

/* Ask: the question printed as the engine worded it, one line read. End
 * of input is no answer. A line longer than the answer can be is read to
 * its end and given as an answer no question takes (asked again), so no
 * leftover is ever taken for the next answer. */
static int con_ask(void *ctx, const INST_ASK *q, char *answer, size_t cap)
{
    size_t n;

    if (q->Text != NULL && q->Text[0] != '\0') {
        con_line(ctx, q->Text);
    }
    con_line(ctx, "");
    inst_wrap_input(&outWrap);
    if (fgets(answer, (int)cap, stdin) == NULL) {
        answer[0] = '\0';
        return 0;
    }
    n = strlen(answer);
    if (n == 0 || answer[n - 1] != '\n') {
        int c;
        int more = 0;
        while ((c = getchar()) != EOF && c != '\n') {
            more = 1;
        }
        if (more) {
            inst_copy(answer, "(too long)", cap);
            return 1;
        }
    }
    while (n > 0 && (answer[n - 1] == '\n' || answer[n - 1] == '\r')) {
        answer[--n] = '\0';
    }
    return 1;
}

static int finish(int code)
{
    INST_OPTS *o = inst_eng_opts();
    char line[8];

    code = inst_eng_end(code, NULL);
    if (!o->NoPause && !o->List && !o->Devices && !inst_eng_relaunched()) {
        printf("Press Enter to close this window.");
        fflush(stdout);
        if (outMirror) {
            fputs("Press Enter to close this window.", stderr);
            fflush(stderr);
        }
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
        }
    }
    return code;
}

int main(int argc, char **argv)
{
    INST_FRONT f;
    INST_OPTS *o;
    char err[256];
    int r;

    /* INSTALL.BAT's refusal of a command line longer than COMMAND.COM
     * passes on 98 SE and ME (record 23 section 2), said here so that the
     * batch file ends with this program's exit code: a program is the one
     * thing that sets COMMAND.COM's errorlevel (task 44.4). */
    if (argc == 2 && (argv[1][0] == '/' || argv[1][0] == '-') &&
        inst_eq(argv[1] + 1, "toomany")) {
        fputs(INST_TOOMANY_TEXT, stdout);
        fflush(stdout);
        return INST_EXIT_REFUSED;
    }
    memset(&f, 0, sizeof(f));
    f.Line = con_line;
    f.Ask = con_ask;
    inst_wrap_init(&outWrap);
    inst_eng_init(&f, inst_self_file(INST_MY_ARCH, INST_OWN_CONSOLE),
                  inst_self_file(INST_ARCH_AMD64, INST_OWN_CONSOLE), 0);
    if (inst_eng_parse(argc, argv, err, sizeof(err)) != 0) {
        con_line(NULL, "xhciinst: ");
        con_line(NULL, err);
        con_line(NULL, "\n\n");
        inst_eng_usage();
        return INST_EXIT_REFUSED;
    }
    o = inst_eng_opts();
    if (o->SelfTest) {
        return inst_eng_selftest();
    }
    outMirror = inst_out_mirror(std_is_console(STD_OUTPUT_HANDLE),
                                std_is_console(STD_ERROR_HANDLE),
                                std_is_console(STD_INPUT_HANDLE));
    r = inst_eng_begin(argc, argv);
    if (r >= 0) {
        return finish(r);
    }
    if (o->Devices || o->Eject) {
        return finish(inst_eng_devices());
    }
    r = inst_eng_read();
    if (r >= 0) {
        return finish(r);
    }
    if (o->Uninstall) {
        r = inst_eng_un_route();
        if (r >= 0) {
            return finish(r);
        }
        return finish(inst_eng_uninstall());
    }
    /* Record 26 section 3.3: the console's order of questions is record
     * 23's - the Thunderbolt question, the plan, the values, the install
     * directory, the confirmation - then the run. */
    inst_eng_tb_question();
    r = inst_eng_plan();
    if (r >= 0) {
        return finish(r);
    }
    r = inst_eng_ask();
    if (r >= 0) {
        return finish(r);
    }
    return finish(inst_eng_run());
}
