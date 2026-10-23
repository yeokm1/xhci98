/*
 * inst_eng.h - the engine of the install, update and uninstall tool: the
 * system half of design record 23 behind one table of callbacks, so that
 * the console tool (XHCIINST.EXE, XHCIIN64.EXE; xhciinst.c) and the window
 * (XHCIWIZ.EXE, XHCIWZ64.EXE; xhciwiz.c) drive the same code (design
 * record 26 section 3; roadmap-usb4.md task 42-A.2).
 *
 * The engine never writes to a console, never reads one, never calls exit
 * and shows no dialog of its own: everything goes through INST_FRONT. It
 * is single threaded: a front end makes one call into it at a time (the
 * window from its one worker thread). Include <windows.h> first. C89.
 */

#ifndef INST_ENG_H
#define INST_ENG_H

#include "inst_core.h"

/* ---- the front end (record 26 section 3.2) -------------------------------- */

/* A question of record 23 as data. Text is the question as the console
 * prints it, already worded; Retry is set when the same question is asked
 * again after an answer that was not one, Text then being the short
 * reminder. The window answers from its pages and shows nothing but the
 * gate. */
#define INST_Q_TB           1   /* Install Thunderbolt 3 / USB4 support?   */
#define INST_Q_VALUE        2   /* a registry value (Value: the row)       */
#define INST_Q_DIR          3   /* the install directory                   */
#define INST_Q_CONFIRM      4   /* go on: Y or Enter                       */
#define INST_Q_REMOVE       5   /* the uninstall's word REMOVE             */
#define INST_Q_DEVMGR       6   /* open Device Manager now?                */
#define INST_Q_GATE         7   /* 98 SE and ME: no wizard open; go on?    */
#define INST_Q_EJECT_PICK   8   /* /eject: which device                    */
#define INST_Q_EJECT_OK     9   /* /eject: eject it now?                   */
#define INST_Q_S98          10  /* Install Stor98, the bundled NVMe driver? */
#define INST_Q_S98_VALUE    11  /* a Stor98 setting (Value: its row)      */
#define INST_Q_S98_UNUSED   12  /* 98 SE and ME: external NVMe drives
                                   disconnected? (the file update)          */
#define INST_Q_STOR         13  /* 98 SE: the USB storage drivers?         */
#define INST_Q_VSD          14  /* 98 SE and ME: S98VSD.VXD?               */
#define INST_Q_OTHER        15  /* a free or non-free set: copy it, stage it? */

typedef struct _INST_ASK {
    int Kind;               /* INST_Q_*                                   */
    int Retry;
    const char *Text;
    int Value;              /* INST_Q_VALUE: the row of instValues        */
    int DefaultYes;         /* the yes-or-no questions                    */
    const char *Default;    /* INST_Q_DIR: what Enter gives               */
} INST_ASK;

/* A step of record 23's order as it begins, ends or is skipped. */
#define INST_STEP_BEGIN     0
#define INST_STEP_DONE      1
#define INST_STEP_FAILED    2
#define INST_STEP_SKIPPED   3
#define INST_STEP_LEFT      4   /* left for the user (steps)              */

typedef struct _INST_STEP {
    int State;              /* INST_STEP_*                                */
    const char *Text;       /* one line                                   */
} INST_STEP;

/* A bounded wait under way, or a step boundary (INST_WAIT_POINT: no wait,
 * only the chance to stop). */
#define INST_WAIT_POINT     0
#define INST_WAIT_SETTLE    1   /* Windows' own installs, the tree        */
#define INST_WAIT_HELPER    2   /* the 98 SE and ME helper's job          */
#define INST_WAIT_RECONNECT 3   /* NT: a tunnelled Stor98 drive unplugged
                                   and plugged in again; 0 from Waiting
                                   skips this wait, it does not stop the run */

typedef struct _INST_WAIT {
    int Kind;               /* INST_WAIT_*                                */
    unsigned long ElapsedMs;
    unsigned long BoundMs;  /* 0: unbounded (a Windows dialog may wait)   */
    const char *What;
} INST_WAIT;

typedef struct _INST_FRONT {
    void *Ctx;
    /* Report text: the console wraps it to 79 columns, the window lists
     * it. The front end passes it to inst_eng_log_text for the log. */
    void (*Line)(void *ctx, const char *text);
    /* Log-only text, for a front end that shows a detail view; may be
     * NULL. The engine writes it to the log itself. */
    void (*Log)(void *ctx, const char *text);
    /* 1 with the answer, 0 for no answer (the console's end of input, the
     * window's cancel): the run stops where record 23 says it stops. */
    int (*Ask)(void *ctx, const INST_ASK *q, char *answer, size_t cap);
    void (*Progress)(void *ctx, const INST_STEP *step);
    /* 0 asks the run to stop at this point (a cancel). */
    int (*Waiting)(void *ctx, const INST_WAIT *w);
    HWND Owner;             /* the window's dialogs' owner; NULL: console */
} INST_FRONT;

/* ---- the run (record 26 section 3.3) ---------------------------------------- */

/* The front end and the program's own file name (XHCIINST.EXE,
 * XHCIWIZ.EXE ...); other is the x64 counterpart a 32-bit run under WOW64
 * names. Call first. */
void inst_eng_init(const INST_FRONT *front, const char *selfName,
                   const char *otherName, int window);

/* The window that owns Windows' own dialogs from now on (the wizard's
 * sheet, once it exists); NULL on the console. */
void inst_eng_owner(HWND owner);

/* The switches, parsed into the engine's options. 0, or 1 refused (err). */
int inst_eng_parse(int argc, char **argv, char *err, size_t cap);
INST_OPTS *inst_eng_opts(void);

/* The usage text, through Line. */
void inst_eng_usage(void);

/* A Line's text, as the front end showed it, into the log. */
void inst_eng_log_text(const char *text);
const char *inst_eng_log_path(void);

/* The log, the header, the system and the /set values checked. Every
 * phase returns -1 to go on, or the exit code the run ends with (handed
 * to inst_eng_end). */
int inst_eng_begin(int argc, char **argv);

/* /devices and /eject (record 24). */
int inst_eng_devices(void);

/* INST_PH_READ: the package (an install), the routines, the rights, the
 * first reading of the devices and the store trap. */
int inst_eng_read(void);

/* The Thunderbolt question (record 23 section 8.5): the console asks it;
 * the window reads the default and gives the Components page's choice. */
void inst_eng_tb_question(void);
int inst_eng_tb_state(int *haveCm, int *present, int *defaultYes,
                      int *installed);
void inst_eng_tb_choose(int include);

/* The connection manager alone (record 23 section 4.5; record 26 section
 * 5.3, draft 8): the USB drivers' box, offered (can be unticked) or not,
 * and whether this is Windows 8 or 8.1, where they are never offered;
 * the choice, planned again; whether the machine has the connection
 * manager alone registered (the uninstall's warning); and the version as
 * record 23 section 4.3 reads it (1 read; 0 not). */
int inst_eng_usb_state(int *offered, int *notOnThisSystem);
void inst_eng_usb_choose(int include);

/* The UAS disk driver's box, a child of the USB drivers' (owner,
 * 2026-10-19): whether xhciuas.inf is in the run, and the choice,
 * planned again. */
int inst_eng_uas_state(void);
void inst_eng_uas_choose(int include);
int inst_eng_cm_only_machine(void);
int inst_eng_os_read(OSVERSIONINFOA *v);

/* INST_PH_PLAN: the plan for the choices as they stand, its text through
 * Line; run again when they change. Nothing written. -1, or a refusal's
 * code (an unfinished /restage of another package). */
int inst_eng_plan(void);

/* INST_PH_ASK: the values, the install directory, the confirmation. */
int inst_eng_ask(void);

/* INST_PH_RUN. */
int inst_eng_run(void);

/* The uninstall (record 26 section 9): the route first - a run from the
 * install directory relaunches a temporary copy and ends (0) - then the
 * whole uninstall. */
int inst_eng_un_route(void);

/* 1 when the uninstall went on in a temporary copy: this run ends at
 * once, with no pause, so its image can be removed. */
int inst_eng_relaunched(void);
int inst_eng_uninstall(void);

/* INST_PH_END: the last line and the exit code (record 23 section 7.4),
 * through Line; the log closed. *line is the final line's kind
 * (INST_FINAL_*). Returns the final code. */
int inst_eng_end(int code, int *line);

/* The console tool's /selftest: the engine's checks and every screen. */
int inst_eng_selftest(void);

/* ---- what the window shows (record 26 section 5) ---------------------------- */

/* One device of the plan, the i-th that a package INF matches. */
typedef struct _INST_FOUND {
    const char *Desc;
    const char *Id;
    const char *Inf;        /* the INF's file name                        */
    char Now[96];           /* what runs it now                           */
    const char *Action;     /* what will be done                          */
    int Runs;
    int Refused;
    int Present;
} INST_FOUND;
int inst_eng_found(int i, INST_FOUND *f);

/* One settings row (record 26 sections 5.4 and 5.5): a value of the table
 * that concerns this machine, asked or advanced. */
#define INST_ROW_NOW_MAX    8
typedef struct _INST_ROW {
    int Value;              /* the row of instValues                      */
    int Asked;              /* on the page (1) or behind Advanced (0)     */
    INST_U32 Mask;          /* the values offered (a list)                */
    INST_U32 Def;
    int MwState;            /* the machine-wide value: 1 set, 0 not, -1 unreadable */
    INST_U32 MwValue;
    int NowCount;
    INST_VNOW Now[INST_ROW_NOW_MAX];
    int Same;               /* every device runs one value: InForce      */
    INST_U32 InForce;
    int CmNew;              /* a new install of the Thunderbolt driver    */
    int Greyed;             /* inst_value_greyed: shown, never written    */
} INST_ROW;
int inst_eng_rows(INST_ROW *rows, int cap);

/* A value the window's page set (as /set does: written when it differs
 * from the value in force), or forgotten (keep). */
void inst_eng_choose_value(int value, int set, INST_U32 v);

/* The install directory: the default (and where the tools are now), and
 * a path checked as a typed answer is - NULL when it can be used, else
 * why not. */
void inst_eng_dir_default(char *def, size_t cap, char *now, size_t nowCap,
                          int *toolHere);
const char *inst_eng_dir_check(const char *path, char *checked, size_t cap);

/* The flags as they stand (record 23 section 7.4). */
void inst_eng_flags(int *changed, int *failed, int *restart, int *steps);

/* 98 SE and ME: the steps for the devices the helper could not install
 * (record 26 section 7's page), as text; "" when none. */
const char *inst_eng_steps_text(void);

/* The install directory this run used, "" when none. */
const char *inst_eng_tool_dir(void);

/* 1 when a controller changes (the input warning applies). */
int inst_eng_controller_changes(void);

/* The confirmation's wording for the plan as it stands. */
const char *inst_eng_confirm_text(void);

/* The window's Summary headline: inst_summary_head with this run's
 * device plan and the page's own writes and other changes. */
const char *inst_eng_summary_head(int writes, int other);

/* S98VSD.VXD (design record 30 section 7): the window's box (its choice;
 * *shown where the package carries the file, *sys9x on 98 SE and ME), the
 * choice made, and the Summary's line (NULL: none). */
int inst_eng_vsd_state(int *shown, int *sys9x);
void inst_eng_vsd_choose(int ticked);
const char *inst_eng_vsd_plan(void);

/* 1 when the xHCI98 USB Tree is started at login now (its Run value is
 * there). */
int inst_eng_tray_started(void);

/* The computer's choice of XHCITR's "can run faster" notice in force (task
 * 43.27): TrayFasterNotice when it is a DWORD 0 or 1, else this system's
 * default (inst_fast_default). 1 on, 0 off. */
int inst_eng_fast_state(void);

/* ---- Stor98, the bundled NVMe driver (record 26 sections 5.3, 5.6, 6.3) -- */

/* Whether the flavour directory carries it (and its version), and whether
 * it is in the run; the Components page's box. */
int inst_eng_s98_state(int *have, char *version, size_t cap);

/* The USB storage drivers' row on Windows 98 SE (record 26 section 6.5):
 * 1 when it is shown; *offered when the download's seven are verified,
 * *def the box's state now (the default and the switches), note the
 * detection's line or why it is greyed. */
int inst_eng_stor_state(int *offered, int *def, char *note, size_t cap);
void inst_eng_stor_choose(int include);
/* Record 26 section 5.3.1 (task 43.43): the Components page's warnings for
 * the boxes as ticked (stor: the storage row), their text for dir; 0 none. */
INST_U32 inst_eng_comp_warn(int usb, int stor, int s98, const char *dir,
                            char *text, size_t cap);
/* The Summary's line for it, "" when it is not shown. */
void inst_eng_stor_summary(char *out, size_t cap);

/* The other USB drivers (record 26 section 6.7): a set's state in the
 * download (INST_OTHER_*), its STAGE lines for this system, and the boxes;
 * 0 before the read. Its sentence for dir and the line under its row; the
 * choice; the page's warnings on Next ("" for none); the Summary's lines;
 * the Finish page's paragraph. */
int inst_eng_other_state(int set, int *state, int *forThis, int *include,
                         int *stage);
void inst_eng_other_texts(int set, const char *dir, char *sentence,
                          size_t sCap, char *line, size_t lCap);
void inst_eng_other_choose(int set, int include, int stage);
/* Record 26 section 6.7.11: the i-th package of a set (0 past the last),
 * its box ticked (0 when it cannot be: not eligible, barred, or the set
 * not copied), and the set's earlier staging in words ("" for none). */
typedef struct _INST_OTHPKG_VIEW {
    char Id[INST_PKG_ID_CHARS];
    char Desc[INST_PKG_DESC_MAX + 1];
    char Why[200];
    int Heading;
    int Eligible;
    int Infs;
    int Tick;
    int Blocked;
    int Earlier;
} INST_OTHPKG_VIEW;
int inst_eng_other_pkg(int set, int i, INST_OTHPKG_VIEW *v);
int inst_eng_other_pkg_tick(int set, int i, int tick);
const char *inst_eng_other_earlier(int set);
/* The package's scope on this system in words, for its details. */
void inst_eng_other_pkg_scope(int set, int i, char *out, size_t cap);
/* The free set's certificate (section 6.7.11): ticked, and *offered the
 * box shown; and its choice. */
int inst_eng_other_trust(int *offered);
int inst_eng_other_trust_possible(void);
void inst_eng_other_trust_choose(int on);
void inst_eng_other_warnings(char *text, size_t cap);
void inst_eng_other_summary(const char *dir, char *text, size_t cap);
void inst_eng_other_finish(char *text, size_t cap);
void inst_eng_s98_choose(int include);
/* Stor98's box while the Thunderbolt box is ticked, on this system (record
 * 26 section 5.0, draft 35: inst_s98_box_default). */
int inst_eng_s98_default(void);
/* The store trap found (record 23 section 5.7): the Components page's
 * "Replace the stored build" box shown; its choice is /restage's. */
int inst_eng_restage_state(void);
void inst_eng_restage_choose(int on);

/* One of its settings (section 5.6): its name on this system, on the page
 * (Asked) or behind Advanced, what is set now (State 1 set, 0 not, -1
 * unreadable; Now the value), the page's text for it, the default. 0 when
 * the row is not this system's. */
typedef struct _INST_S98ROW {
    int Value;              /* the row of instS98Values                   */
    int Asked;
    const char *Name;
    int State;
    char Now[32];
    char NowText[160];
    char Default[48];
} INST_S98ROW;
int inst_eng_s98_row(int i, INST_S98ROW *row);

/* A value chosen for row i (set) or forgotten (keep): NULL when taken,
 * else why not. */
const char *inst_eng_s98_choose_value(int i, int set, const char *text);

/* 98 SE and ME (record 26 section 6.3, draft 16): 1 when the run
 * replaces Stor98's file at the next start; the user's word (the Summary
 * page's one box, instS98DisconnectText), without which Stor98 is left
 * for a later run. */
int inst_eng_s98_file_update(void);
void inst_eng_s98_file_ok(int ok);

/* The Thunderbolt 3 / USB4 controller found ("" for none). */
const char *inst_eng_tb_name(void);

/* 1 when the finish offers a shutdown rather than a restart (98 SE and ME
 * after a Stor98 update); 1 when the uninstall left a Stor98 it did not
 * install (the finish page says so). */
int inst_eng_shutdown(void);
int inst_eng_s98_left(void);

#endif /* INST_ENG_H */
