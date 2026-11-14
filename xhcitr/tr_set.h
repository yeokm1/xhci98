/*
 * tr_set.h - the pure half of XHCITR's Settings window and XHCITR /set
 * (design record 25 section 22; roadmap-usb4.md task 47.35).
 *
 * Which registry values the window shows, on which tab and view, which
 * values it offers, what the driver makes of a value, what a change takes
 * to apply, the texts, the exit code, and the elevated run's request. The
 * values' meanings, choices, ranges and system rules are Setup's own table
 * (xhciinst\inst_core.c, instValues[] and instS98Values[]), which since
 * task 49.38 holds every value a driver reads but the listed few, so the
 * two tools show the same rows. No Windows header, no registry: the host
 * suite test\test_trset.c links it. C89.
 */

#ifndef TR_SET_H
#define TR_SET_H

#include <stddef.h>
#include "tr_core.h"
#include "../xhciinst/inst_core.h"

/* The window's tabs. */
#define TRS_TAB_USB         0
#define TRS_TAB_CM          1
#define TRS_TAB_S98         2
#define TRS_TABS            3

/* A row's view: not shown, shown without "Show advanced settings", or
 * only with it. */
#define TRS_VIEW_OUT        0
#define TRS_VIEW_COMMON     1
#define TRS_VIEW_ADV        2

/* What a change takes to apply. On 98 SE and ME every class but REPLUG is
 * BOOT (trs_apply). */
#define TRS_AP_REPLUG       1   /* the drive plugged in again              */
#define TRS_AP_BOOT         2   /* Windows restarted                       */
#define TRS_AP_ADD          3   /* the controller found again (AddDevice)  */
#define TRS_AP_CTRL         4   /* the controller's next start             */
#define TRS_AP_NHI          5   /* the Thunderbolt / USB4 controller's     */

/* What the driver makes of a DWORD (trs_effective). */
#define TRS_EFF_LIST        0   /* a value of Accept, else AbsentDef        */
#define TRS_EFF_NONZERO     1   /* any value but 0 is 1                     */
#define TRS_EFF_ONE         2   /* only 1 is on; anything else is 0         */
#define TRS_EFF_RANGE       3   /* in Lo to Hi (and its step), else AbsentDef */
#define TRS_EFF_CLAMP       4   /* held to the nearer of Lo and Hi          */
#define TRS_EFF_ZERODEF     5   /* 0 is AbsentDef, above Hi is Hi           */
#define TRS_EFF_HIGH        6   /* above Hi is Hi                            */
#define TRS_EFF_LOW2        7   /* only bits 0 and 1 count (XhciResumeKeepBot) */
/* Task 49.38 (numbered past 7, which w49-bkeep2 takes for XhciResumeKeepBot):
 * the value as set, which the Tree does not read further (the mode-4
 * values: one the driver does not take keeps the controller from
 * starting; 0xFFFFFFFF, its reads' "not found", is the default). */
#define TRS_EFF_ASIS        8
#define TRS_EFF_LOW         9   /* below Lo is Lo (LogBytes)                 */
#define TRS_EFF_RING        11  /* XhciEventRingTrbs: 256 is 256, any other
                                   DWORD (or another type) 1024; absent
                                   AbsentDef, 256 (xhci_init.c)           */
#define TRS_EFF_SHARE       10  /* XhciMsiShare: 0 is 1, above Hi is Hi,
                                   0xFFFFFFFF the default (xhci_msg4.c)   */

/* Where a row's value lives. */
#define TRS_LOC_DRIVER      0   /* each device's driver key, and the
                                   machine-wide Services\USB, which wins    */
#define TRS_LOC_PFW         1   /* XHCIPFW.VXD's own key (98 SE, ME)       */
#define TRS_LOC_MARK        2   /* the mark service's Parameters key       */
#define TRS_LOC_S98         3   /* Stor98's service key / DriverParameter  */
#define TRS_LOC_DEVICE      4   /* one UAS drive's own device key (task
                                   49.38): 2000 to 10 its Enum key's Device
                                   Parameters, 98 SE and ME its Enum key   */

typedef struct _TRS_ROW {
    const char *Name;       /* the value's name; Stor98's 98 SE / ME name */
    int Tab;                /* TRS_TAB_*                                 */
    int View;               /* TRS_VIEW_*                                */
    int Apply;              /* TRS_AP_* on 2000 and later                */
    int Cycle;              /* 1: a Device Manager disable and enable
                               applies it too (tuning.md says so)        */
    int Effect;             /* TRS_EFF_*                                 */
    INST_U32 Accept;        /* TRS_EFF_LIST: the values the driver takes */
    INST_U32 AbsentDef;     /* the driver's value when none is set       */
    INST_U32 InstallDef;    /* the value the INFs write (tuning.md)      */
    int S98;                /* Stor98's row (INST_S98_*), else -1        */
    const char *Note;       /* the Tree's own caution, or NULL; for an
                               OUT row, why it is out                    */
} TRS_ROW;

/* The system and hardware as the rows see them. */
typedef struct _TRS_ENV {
    INST_ENV Inst;          /* Setup's (Nt, Major, Minor, AnyXhci,
                               PswController, CmNhi, AsmHandover)        */
    int Pfw;                /* XHCIPFW.VXD's key is there (98 SE, ME)     */
    int Mark;               /* the mark service's key is there           */
    int NoRouteW2kListed;   /* an xhci98 controller on XhciIntelNoRouteIrq's
                               list (XhciPirqNoRouteListed): on Windows
                               2000, 0 there brings back STOP 0xA5        */
    int WalkPartial;        /* the device walk was not read whole: a listed
                               controller may be missing from it          */
    int UasDrives;          /* present UAS drives (task 49.38)            */
} TRS_ENV;

/* The settings search (task 49.38): 1 when text is empty or found, without
 * case, in the row's name, meaning or choice texts (Stor98's: both names,
 * what it does, its words and meanings). trs_listed: whether the window
 * lists the row - shown here, and an advanced row only with "Show
 * advanced settings" or while a search is on - and it matches. */
int trs_matches(const TRS_ROW *r, const char *text);
int trs_listed(const TRS_ROW *r, const TRS_ENV *e, int adv, const char *text);

/* The Settings window's devices against a fresh reading (task 49.38): 1
 * when the two lists hold the same devices, each by its instance id and
 * driver key (without case), in any order. ida/drva n of them as the
 * window holds them, idb/drvb m as read now. */
int trs_same_devices(const char *const *ida, const char *const *drva, int n,
                     const char *const *idb, const char *const *drvb, int m);
/* A row whose default is not one number but this Windows's (AbsentDef and
 * InstallDef): XhciResumeKeepBot, which no INF writes (design record 20
 * section 4.3.4, M18). trs_absent_def and trs_install_def give it from the
 * system trs_set_system was last given (inst_keepbot_default); before any,
 * 0. */
#define TRS_DEF_SYSTEM      0xFFFFFFFEUL
void trs_set_system(const INST_ENV *e);
INST_U32 trs_absent_def(const TRS_ROW *r);
INST_U32 trs_install_def(const TRS_ROW *r);

int trs_row_count(void);
const TRS_ROW *trs_row_at(int i);
/* By name, without case; Stor98's rows also by their 2000-to-10 key. */
const TRS_ROW *trs_row_find(const char *name);

/* The row's definition: Setup's instValues[] row (task 49.38: every one);
 * NULL for a Stor98 row. */
const INST_VALUE *trs_value(const TRS_ROW *r);


/* A row's state here: not listed, listed, or listed greyed (the system is
 * another's; its hardware is here). */
#define TRS_HIDE            0
#define TRS_SHOW            1
#define TRS_GREY            2
int trs_state(const TRS_ROW *r, const TRS_ENV *e);
/* A greyed row's "(... only)" text, or NULL. */
const char *trs_only(const TRS_ROW *r, const TRS_ENV *e);
int trs_loc(const TRS_ROW *r);

/* The values a list row offers here, in order, into vals (at most cap);
 * adv: "Show advanced settings" (XhciCmMode's 2 and 3). 0 for a range or
 * a Stor98 number row. XhciIntelPortSwitch offers 0 and 1 only: 2 stays a
 * hand edit. */
int trs_choices(const TRS_ROW *r, const TRS_ENV *e, int adv, INST_U32 *vals,
                int cap);
/* A list value's text, "1 - on"; a number as itself ("160"). */
void trs_value_text(const TRS_ROW *r, INST_U32 v, char *out, size_t cap);
/* 1 for a row whose value is a number in a range (a number box). */
int trs_is_range(const TRS_ROW *r);
/* "10 to 4000", "64 to 2048, a multiple of 4". */
void trs_range_text(const TRS_ROW *r, const TRS_ENV *e, char *out, size_t cap);

/* A value typed or given to /set for row r, checked as the window offers
 * it with "Show advanced settings": 1 with its canonical text in out (a
 * decimal number, or Stor98's word in lower case), 0 refused with why.
 * "default" is the row's installed default (Stor98's: its built-in one
 * for the system). */
int trs_parse(const TRS_ROW *r, const TRS_ENV *e, const char *text,
              char *out, size_t cap, const char **why);

/* What the driver makes of a value found (found 0: none set): the value
 * it runs with, and *how it got there (TRS_HOW_*). held is the value a
 * row is held to (XhciPollActiveMs to the idle period,
 * XhciFirstEnumPortMs to the total wait), or TRS_NO_HOLD for none. */
#define TRS_NO_HOLD         0xFFFFFFFFUL   /* held: no other value bounds it */
#define TRS_HOW_AS_SET      0
#define TRS_HOW_DEFAULT     1   /* none set, or one the driver refuses   */
#define TRS_HOW_HELD        2   /* held to a bound or another value       */
#define TRS_HOW_AS_ON       3   /* a number other than 1 read as on       */
INST_U32 trs_effective(const TRS_ROW *r, int found, INST_U32 v,
                       INST_U32 held, int *how);
/* The system the values are read on: 1 for 98 SE and ME, where the drivers
 * run XhciInterruptMode 4 as 0 and poll for XhciCmRingInterrupt 2, so
 * trs_effective reads those as the default; 0 (the start) for NT. */
void trs_set_nine(int nine);

/* A raw reading. */
#define TRS_RAW_UNREAD      0
#define TRS_RAW_ABSENT      1
#define TRS_RAW_DWORD       2
#define TRS_RAW_OTHER       3   /* there, and not a DWORD                 */
typedef struct _TRS_RAW {
    int State;
    INST_U32 Value;
    int IsText;             /* OTHER: a string, Text (task 49.38)        */
    char Text[INST_TEXT_CHARS];
} TRS_RAW;

/* A registry query's outcome into a reading (task 49.38): q is
 * TRS_Q_*, type the value's type (TRS_REG_*), n its length, data its
 * bytes. A value of REG_NONE with no data is absent, as the drivers take
 * it (XhciRegCallSeen): it neither holds nor hides a value. */
#define TRS_Q_OK            0
#define TRS_Q_NOT_FOUND     1
#define TRS_Q_MORE_DATA     2
#define TRS_Q_ERROR         3
#define TRS_REG_NONE        0UL
#define TRS_REG_SZ          1UL
#define TRS_REG_DWORD       4UL
void trs_raw_from_query(int q, unsigned long type, unsigned long n,
                        const unsigned char *data, TRS_RAW *v);

/* The Now column of a driver-key row from the machine-wide reading and
 * each device's own (n of them): the machine-wide value when there, else
 * the devices' common one, else "not the same on every controller". */
void trs_now_text(const TRS_ROW *r, const TRS_RAW *machine,
                  const TRS_RAW *own, int n, char *out, size_t cap);
/* The same with the value the row is held to (trs_held_by's row's value
 * in force, or TRS_NO_HOLD for none). */
void trs_now_text_held(const TRS_ROW *r, const TRS_RAW *machine,
                       const TRS_RAW *own, int n, INST_U32 held, char *out,
                       size_t cap);
/* The same with each device's own held value (held[i] for own[i]; held[0]
 * alone when n is 0; NULL for none): one text when every device's value in
 * force reads alike, else "not the same on every controller", so that
 * controllers held to different values are never shown as one. */
void trs_now_text_each(const TRS_ROW *r, const TRS_RAW *machine,
                       const TRS_RAW *own, int n, const INST_U32 *held,
                       char *out, size_t cap);
/* The row a row's value is held to (XhciPollActiveMs to XhciPollIdleMs,
 * XhciFirstEnumPortMs to XhciFirstEnumWaitMs), or NULL. */
const TRS_ROW *trs_held_by(const TRS_ROW *r);
/* One reading as text: "1 - on", "not set (the driver uses 1 - on)",
 * "7 (the driver reads it as 1 - on)", "not a number (the driver uses
 * ...)". */
void trs_raw_text(const TRS_ROW *r, const TRS_RAW *v, char *out, size_t cap);
void trs_raw_text_held(const TRS_ROW *r, const TRS_RAW *v, INST_U32 held,
                       char *out, size_t cap);

/* What a change to row r takes, on this system. */
int trs_apply(const TRS_ROW *r, int nt);

/* The changes of one Apply, summed for its texts and exit code. The
 * REPLUG rows (read as a drive is plugged in): XhciForceBulkOnly and, since
 * UASBOT.1, XhciBulkOnlyOwnId. */
#define TRS_REPLUG_FORCE    1
#define TRS_REPLUG_OWN_ID   2
#define TRS_REPLUG_DRIVE    4   /* a UAS drive's own value (task 49.38)  */
#define TRS_SUM_NAMES       512
typedef struct _TRS_SUM {
    int Restart;            /* a change that needs a restart (any class
                               but REPLUG)                                */
    int Replug;             /* the REPLUG changes: TRS_REPLUG_*           */
    int CycleAll;           /* every restart change may be cycled instead */
    char Names[TRS_SUM_NAMES];      /* the restart changes, ", "-joined   */
    char DriveNames[96];    /* the drive values changed (task 49.38)      */
} TRS_SUM;
void trs_sum_init(TRS_SUM *s);
void trs_sum_add(TRS_SUM *s, const TRS_ROW *r, int nt);
/* The lines after the changes: what they take. nt: the Device Manager
 * alternative is said on 2000 and later only. Empty when nothing changed. */
void trs_sum_text(const TRS_SUM *s, int nt, char *out, size_t cap);
/* Record 23 section 7.4's code: 1 refused, 2 failed, 4 a replug left, 3 a
 * restart needed, 0 nothing. */
int trs_sum_exit(const TRS_SUM *s, int refused, int failed);

/* ---- the elevated run's request (record 25 section 22) ------------------ */

/* The window writes the request into the section it passes the elevated
 * run: "XHCITR-SET 1\n", then one line per change "name=value\ton\n" (on
 * an instance id, or "*" for every device), then "END\n", NUL. */
#define TRS_MAX_CHANGES     48
#define TRS_NAME_CHARS      48
#define TRS_VAL_CHARS       64   /* XhciCmTunnelBridge's text (task 49.38) */
#define TRS_DRV_CHARS       96
typedef struct _TRS_CHANGE {
    char Name[TRS_NAME_CHARS];
    char Value[TRS_VAL_CHARS];
    char On[TR_ID_CHARS];       /* "" for every device                    */
    char Drv[TRS_DRV_CHARS];    /* with On: the driver key the window saw,
                                   "" for no check (task 49.38)          */
} TRS_CHANGE;

/* The whole request into out; 0 when it does not fit cap or a change
 * cannot be carried (a field too long, a tab or line break in it), so no
 * prefix of a request is ever sent. */
int trs_req_make(const TRS_CHANGE *c, int n, char *out, size_t cap);
/* The request read back: 1 with the changes, 0 for anything malformed
 * (no header, no END, a line that does not parse, too many). */
int trs_req_parse(const char *in, size_t cap, TRS_CHANGE *c, int max,
                  int *n);

/* "Name=Value" of /set into a change (the name and value as given, not
 * yet checked against the rows): 1, or 0 with why. */
int trs_arg(const char *arg, TRS_CHANGE *c, const char **why);

/* ---- the port switchover's warning (section 22) -------------------------- */

/* The confirmation for XhciIntelPortSwitch going on (on 1) or off (on 0);
 * noDriver: a USB 2.0 controller here has no working driver. The caller
 * appends the USB 2.0 controllers' names. */
void trs_psw_text(int on, int noDriver, char *out, size_t cap);

/* The Tree's caution for row r where Setup's describes Setup's own work,
 * else the table's (or NULL). */
const char *trs_caution(const TRS_ROW *r);
/* The caution as shown, Setup's "%s" named as Setup names it (its
 * inst_caution_text): the values the caution is about among those
 * offered (values), else all of them. Empty where there is none. */
void trs_caution_text(const TRS_ROW *r, INST_U32 values, char *out,
                      size_t cap);

/* Stor98's row as an index into instS98Values, or -1. */
int trs_s98(const TRS_ROW *r);

/* ---- quick save and its hotkey (record 25 section 22, the owner's
 * addition of 2026-11-07) ------------------------------------------------- */

/* Where a quick save goes: the install folder's Reports when Setup's
 * record names it and a folder can be made there, else the user's
 * documents, else nowhere. */
#define TRS_QS_NONE         (-1)
#define TRS_QS_INSTALL      0
#define TRS_QS_DOCS         1
int trs_qs_where(int installKnown, int installMade, int docsKnown);

/* The dated folder's name, "2026-11-07_121530", and from n 2 on
 * "2026-11-07_121530-2" (a name already there is never reused). */
void trs_qs_name(unsigned y, unsigned mo, unsigned d, unsigned h,
                 unsigned mi, unsigned s, int n, char *out, size_t cap);

/* The hotkey as a DWORD: the RegisterHotKey modifiers (MOD_ALT 1,
 * MOD_CONTROL 2, MOD_SHIFT 4) << 16 | the virtual key; 0 off. */
#define TRS_HK_ALT          0x1UL
#define TRS_HK_CTRL         0x2UL
#define TRS_HK_SHIFT        0x4UL
#define TRS_HK_WIN          0x8UL
#define TRS_HK_DEFAULT      ((0x3UL << 16) | 0x7AUL)    /* Ctrl+Alt+F11 */
#define TRS_HK_CHOICES      6
/* A PCI hardware id's SUBSYS_ssssvvvv as one value (subsystem id << 16 |
 * subsystem vendor); 0 where it has none. */
int trs_subsys(const char *hwid, TR_U32 *subsys);
/* 1 when setting r to value needs the STOP 0xA5 question first:
 * XhciIntelNoRouteIrq 0 on Windows 2000 with a listed controller, or with
 * a walk not read whole, which may have missed one (task 47.40,
 * tuning.md). */
int trs_a5_confirm(const TRS_ROW *r, const TRS_ENV *e, const char *value);
/* The question's text (the window asks; /set refuses without /yes). */
void trs_a5_text(char *out, size_t cap);
/* The note /set prints before a change's outcome (TRSETWARN.1): the STOP
 * 0xA5 question (trs_a5_confirm), the port switch's warning
 * (trs_psw_text), or none; none for a value trs_parse refuses here, so a
 * refused change prints its refusal alone. */
#define TRS_NOTE_NONE       0
#define TRS_NOTE_A5         1
#define TRS_NOTE_PSW        2
int trs_set_note(const TRS_ROW *r, const TRS_ENV *e, const char *value);

/* The icon's right-click menu, item i in order (the owner, 2026-11-07):
 * its text, "-" for a separator, NULL past the end. */
#define TRS_TRAY_OPEN       0
#define TRS_TRAY_SETTINGS   1
#define TRS_TRAY_ABOUT      2
#define TRS_TRAY_QUICKSAVE  4
#define TRS_TRAY_EXIT       6
const char *trs_tray_item(int i);

/* The window's choices, the default first. */
TR_U32 trs_hk_choice(int i);
/* The choice's index, -1 for a key that is not one of them. */
int trs_hk_index(TR_U32 key);
/* The keys pressed together, the function key counted (Ctrl+F11: 2). */
int trs_hk_keys(TR_U32 key);
/* "Ctrl+Alt+F11". */
void trs_hk_text(TR_U32 key, char *out, size_t cap);
/* 1 for a key the tool accepts: F1 to F11 or F13 to F24 (F12 is NT's
 * debugger key) with Ctrl or Alt, and never the Windows key. */
int trs_hk_ok(TR_U32 key);

#endif /* TR_SET_H */
