/*
 * xhci_psw.h - the Intel EHCI-to-xHCI port switchover, its pure half
 * (roadmap-hcd.md task 34.3).
 *
 * On the 7-, 8- and 9-series PCH (Panther Point, Lynx Point, Wildcat Point,
 * and the -LP parts of the last two), and on Wellsburg (C610/X99), each
 * switchable connector is wired to
 * both the EHCI and the xHCI controller, and four registers in the xHCI's
 * own PCI configuration space choose which one has it: USB3_PSSEN turns on
 * the connector's SuperSpeed terminations under xHCI, XUSB2PR routes its USB
 * 2.0 pair to xHCI, and USB3PRM and XUSB2PRM are the masks of what firmware
 * lets the OS change. Firmware set to "Auto", or offering no setting, leaves
 * every switchable connector on EHCI, so the xHCI starts with nothing on
 * those ports.
 *
 * The sequence is Linux's usb_enable_intel_xhci_ports (pci-quirks.c):
 * USB3_PSSEN = USB3PRM first, so a SuperSpeed device connects at SuperSpeed
 * rather than at High Speed, then XUSB2PR = XUSB2PRM, each read back. Back
 * to EHCI is its usb_disable_xhci_ports: written 0, USB3_PSSEN first -
 * here only the registers a route of this lifetime wrote (task 35.5).
 *
 * The gate is the device-id list, not Linux's rule (any Intel xHCI with an
 * Intel EHCI on the bus), which needs a PCI scan the HCD does not have
 * (owner, 2026-10-05). Nor every Intel xHCI: from the 100-series on there is
 * no EHCI, and what these offsets do there is unread. The one board Linux exempts by subsystem id, a Sony
 * VAIO T-series that "is not capable of switching ports", is exempted here
 * too. The switch is the XhciIntelPortSwitch value: a DWORD of any nonzero
 * number turns it on; 0, absent or of another type, it is off (owner,
 * 2026-10-09, task 36.42; from task 49.47, 2026-11-14, the INFs write 1,
 * and absent stays off for text-mode Setup). An exact
 * 2 also routes on an Intel controller the list does not name, at the
 * user's own risk (task 35.5, design record 16 section 4a).
 *
 * The lifetime (record 16 section 7a) is here too: the start's decision,
 * and which of the two routing registers a route of this started lifetime
 * wrote, so a release writes those and no other.
 *
 * From task 50.34 (record 16 section 21) the start's decision has one more
 * step on a controller whose switchover is on: the setting
 * XhciIntelPortSwitchSetup, Windows' Setup values, the two Windows PE
 * tests, the image state and the first-session guard (Alternative A, the
 * volatile session key), each reading classified here from what the
 * executor's query routine saw and the query's own status, the verdict,
 * and the record XhciIntelPortSwitchResult that says what held a start.
 * A held lifetime has On clear and so makes no routing access at all, at
 * a shutdown's hand-back included (XhciPswShutdownAllowed).
 *
 * The executor is hcd_ctl.c: it supplies the configuration-space accessors
 * and the value reader, and logs what comes back. Nothing here touches a
 * register, takes a lock or calls a kernel service.
 *
 * DDK-free: part of the pure core. C89. IRQL: any for the functions that
 * take no XHCI_PSW_IO; those that do run at their callbacks' IRQL, which
 * with hcd_ctl.c's is PASSIVE_LEVEL under no spin lock.
 */

#ifndef XHCI_PSW_H
#define XHCI_PSW_H

#include "xhci_compat.h"
#include "xhci_regsrc.h"

/* PCI configuration offsets, in the xHCI function's own space. */
#define XHCI_PSW_PCI_ID         0x00UL  /* vendor 15:0, device 31:16        */
#define XHCI_PSW_PCI_SUBSYSTEM  0x2CUL  /* SVID 15:0, SSID 31:16            */
#define XHCI_PSW_XUSB2PR        0xD0UL  /* USB 2.0 pairs routed to xHCI     */
#define XHCI_PSW_XUSB2PRM       0xD4UL  /* which of them the OS may route   */
#define XHCI_PSW_USB3_PSSEN     0xD8UL  /* SuperSpeed terminations enabled  */
#define XHCI_PSW_USB3PRM        0xDCUL  /* which of them the OS may enable  */

/*
 * The first access a route or release found failed (XHCI_PSW_STATE.Step);
 * 0 when every access was made. A route stops at a failed mask read or a
 * failed write,
 * so the USB 2.0 pairs never move to xHCI without the SuperSpeed
 * terminations; a release attempts each write its set selects, whatever
 * an earlier one did.
 */
#define XHCI_PSW_DONE               0UL
#define XHCI_PSW_STEP_USB3PRM       1UL /* mask unreadable or all-ones      */
#define XHCI_PSW_STEP_PSSEN_WRITE   2UL
#define XHCI_PSW_STEP_PSSEN_READ    3UL /* read-back failed; route went on  */
#define XHCI_PSW_STEP_XUSB2PRM      4UL /* mask unreadable or all-ones      */
#define XHCI_PSW_STEP_XUSB2PR_WRITE 5UL
#define XHCI_PSW_STEP_XUSB2PR_READ  6UL /* read-back failed                 */
#define XHCI_PSW_STEP_BAD_PARAM     7UL

/* Which routing registers a route's write was accepted on (Written). */
#define XHCI_PSW_WROTE_PSSEN        0x1UL   /* USB3_PSSEN, D8h              */
#define XHCI_PSW_WROTE_XUSB2PR      0x2UL   /* XUSB2PR, D0h                 */

/* XhciPswMode. */
#define XHCI_PSW_MODE_OFF           0UL
#define XHCI_PSW_MODE_GATED         1UL     /* a listed controller          */
#define XHCI_PSW_MODE_BYPASS        2UL     /* unlisted Intel, value 2      */

/* What ended the start's decision (XHCI_PSW_LIFE.Decision). */
#define XHCI_PSW_DECIDE_NONE        0UL     /* not run, or a bad parameter  */
#define XHCI_PSW_DECIDE_ID_UNREAD   1UL     /* PCI offset 0 unreadable      */
#define XHCI_PSW_DECIDE_NOT_INTEL   2UL     /* no value read                */
#define XHCI_PSW_DECIDE_OFF         3UL     /* XhciPswMode said off         */
#define XHCI_PSW_DECIDE_BOARD_UNREAD 4UL    /* subsystem id unreadable      */
#define XHCI_PSW_DECIDE_BOARD_REFUSED 5UL   /* the Sony board               */
#define XHCI_PSW_DECIDE_ON          6UL     /* routed                       */
#define XHCI_PSW_DECIDE_SETUP_HELD  7UL     /* held by section 21's step    */

/* The system the start runs on (XHCI_PSW_IO.System). 0 is not 98 SE or
 * ME: an io that does not say reads every check, so it holds where a
 * callback is missing. */
#define XHCI_PSW_SYS_NT6            0UL     /* Vista and later              */
#define XHCI_PSW_SYS_9X             1UL     /* 98 SE, ME                    */
#define XHCI_PSW_SYS_NT5            2UL     /* 2000, XP, XP x64, 2003       */

/* The two DWORD settings (XHCI_PSW_VALUE's which). */
#define XHCI_PSW_WHICH_SWITCH       0UL     /* XhciIntelPortSwitch          */
#define XHCI_PSW_WHICH_SETUP        1UL     /* XhciIntelPortSwitchSetup     */

/* XhciIntelPortSwitchSetup in force (XHCI_PSW_LIFE.Setting). */
#define XHCI_PSW_SETTING_AUTO       0UL
#define XHCI_PSW_SETTING_BYPASS     1UL
#define XHCI_PSW_SETTING_HOLD       2UL

/* The registry types and statuses the classifications name. */
#define XHCI_PSW_REG_NONE           0UL
#define XHCI_PSW_REG_SZ             1UL
#define XHCI_PSW_REG_DWORD          4UL
#define XHCI_PSW_ST_SUCCESS         0x00000000UL
#define XHCI_PSW_ST_NAME_NOT_FOUND  0xC0000034UL
#define XHCI_PSW_ST_PATH_NOT_FOUND  0xC000003AUL
#define XHCI_PSW_ST_CHILD_VOLATILE  0xC0000181UL    /* MUST_BE_VOLATILE   */
#define XHCI_PSW_ST_NOT_RUN         0xFFFFFFFFUL    /* the call not made  */

/* ZwCreateKey's dispositions. */
#define XHCI_PSW_KEY_CREATED        1UL     /* REG_CREATED_NEW_KEY          */
#define XHCI_PSW_KEY_OPENED         2UL     /* REG_OPENED_EXISTING_KEY      */

/* The queries (XHCI_PSW_QUERY's which). */
#define XHCI_PSW_Q_SSIP             0UL     /* SYSTEM\Setup, the primary    */
#define XHCI_PSW_Q_TYPE             1UL     /* SetupType                    */
#define XHCI_PSW_Q_OOBE             2UL     /* OOBEInProgress               */
#define XHCI_PSW_Q_MINI             3UL     /* MiniSetupInProgress          */
#define XHCI_PSW_Q_MININT           4UL     /* Control\MiniNT, existence    */
#define XHCI_PSW_Q_OPTIONS          5UL     /* SystemStartOptions, string   */
#define XHCI_PSW_Q_HIVE             6UL     /* the SOFTWARE hive, existence */
#define XHCI_PSW_Q_IMAGE            7UL     /* ImageState, string           */
#define XHCI_PSW_Q_READY            8UL     /* G, the driver key's DWORD    */
#define XHCI_PSW_Q_SESSION          9UL     /* ReadyAtStart, session key    */
#define XHCI_PSW_SETUP_VALUES       4UL

/* The string readers' buffers, in characters (record 16 section 21.3). */
#define XHCI_PSW_OPTIONS_CHARS      512UL
#define XHCI_PSW_IMAGE_CHARS        64UL

/* What the query routine saw, and the query's own status. Calls counts
 * every call; Type and Length are the first call's as given; Dword is its
 * first four bytes, little-endian, when it gave four or more. */
typedef struct _XHCI_PSW_RAW {
    ULONG Calls;
    ULONG Type;
    ULONG Length;
    ULONG Status;
    ULONG Dword;
} XHCI_PSW_RAW, *PXHCI_PSW_RAW;

/* A DWORD reading (record 16 section 21.3), and the log's byte for it. */
#define XHCI_PSW_RD_ABSENT          0UL
#define XHCI_PSW_RD_TAKEN           1UL
#define XHCI_PSW_RD_MALFORMED       2UL
#define XHCI_PSW_RD_UNREAD          3UL
#define XHCI_PSW_RD_NOT_READ        0xFFUL  /* not asked on this system     */

/* A string reading. */
#define XHCI_PSW_SZ_VALID           1UL
#define XHCI_PSW_SZ_ABSENT          0UL
#define XHCI_PSW_SZ_MALFORMED       2UL

/* An existence test. */
#define XHCI_PSW_EX_NOT             0UL     /* not there (the hive: not
                                             * loaded yet)                  */
#define XHCI_PSW_EX_THERE           1UL
#define XHCI_PSW_EX_UNREAD          3UL

/* A Windows PE test's answer. */
#define XHCI_PSW_PE_NOT             0UL
#define XHCI_PSW_PE_YES             1UL
#define XHCI_PSW_PE_UNREAD          3UL

/* The image state (the log's psw.image). */
#define XHCI_PSW_IMG_COMPLETE       0UL
#define XHCI_PSW_IMG_NOT_COMPLETE   1UL
#define XHCI_PSW_IMG_UNREAD         2UL
#define XHCI_PSW_IMG_NOT_LOADED     3UL
#define XHCI_PSW_IMG_NOT_READ       0xFFFFFFFFUL    /* before Vista         */

/* The guard's outcome (record 16 section 21.5.1). */
#define XHCI_PSW_GUARD_NOT_RUN      0UL
#define XHCI_PSW_GUARD_ALLOWS       1UL     /* S 1: a later session         */
#define XHCI_PSW_GUARD_FIRST        2UL     /* S 0: the first session       */
#define XHCI_PSW_GUARD_NO_ROUTINE   3UL     /* step 2                       */
#define XHCI_PSW_GUARD_KEY_FAILED   4UL     /* step 3                       */
#define XHCI_PSW_GUARD_NOT_VOLATILE 5UL     /* step 4                       */
#define XHCI_PSW_GUARD_S_UNKNOWN    6UL     /* step 5                       */
#define XHCI_PSW_S_UNKNOWN          0xFFUL

/* The guard's operations (XHCI_PSW_GUARD_OP's op). */
#define XHCI_PSW_OP_ROUTINE         1UL     /* 1 when ZwCreateKey is there  */
#define XHCI_PSW_OP_OPEN            2UL     /* the session key: status,
                                             * *out the disposition         */
#define XHCI_PSW_OP_PROBE           3UL     /* the stable child's status    */
#define XHCI_PSW_OP_WRITE_S         4UL     /* ReadyAtStart = arg: status   */
#define XHCI_PSW_OP_WRITE_G         5UL     /* G = 1: status                */
#define XHCI_PSW_OP_CLOSE           6UL     /* the session key's handle     */

/* The verdict (record 16 section 21.3), in its order. */
#define XHCI_PSW_V_NONE             0UL     /* the step not reached         */
#define XHCI_PSW_V_ROUTE            1UL
#define XHCI_PSW_V_BYPASS           2UL     /* setting 1                    */
#define XHCI_PSW_V_ROUTE_9X         3UL
#define XHCI_PSW_V_HELD_SETTING     4UL     /* setting 2                    */
#define XHCI_PSW_V_HELD_SETUP       5UL     /* value: the XHCI_PSW_Q_*      */
#define XHCI_PSW_V_HELD_SETUPTYPE   6UL
#define XHCI_PSW_V_HELD_SETUP_UNREAD 7UL    /* value: the XHCI_PSW_Q_*      */
#define XHCI_PSW_V_HELD_PE          8UL     /* value: XHCI_PSW_Q_MININT or
                                             * XHCI_PSW_Q_OPTIONS           */
#define XHCI_PSW_V_HELD_PE_UNREAD   9UL     /* value: as HELD_PE            */
#define XHCI_PSW_V_HELD_IMAGE       10UL
#define XHCI_PSW_V_HELD_IMAGE_UNREAD 11UL
#define XHCI_PSW_V_HELD_IMAGE_NOT_LOADED 12UL
#define XHCI_PSW_V_HELD_FIRST       13UL
#define XHCI_PSW_V_HELD_SESSION     14UL    /* value: the guard's outcome   */

/* One dword of configuration space; return 1 when the access was made. */
typedef ULONG (*XHCI_PSW_READ)(PVOID context, ULONG offset, PULONG value);
typedef ULONG (*XHCI_PSW_WRITE)(PVOID context, ULONG offset, ULONG value);
/* One of the two DWORD settings (XHCI_PSW_WHICH_*), as hcdReadDword reads
 * it: returns the XHCI_REG_* state that stands (xhci_regsrc.h), with
 * *value set when TAKEN and *source the XHCI_REG_FROM_* key it stands in. */
typedef ULONG (*XHCI_PSW_VALUE)(PVOID context, ULONG which, PULONG value,
                                PULONG source);
/* One reading (XHCI_PSW_Q_*): fills raw, and for a string copies up to
 * capChars characters of its data into text. */
typedef VOID (*XHCI_PSW_QUERY)(PVOID context, ULONG which, PXHCI_PSW_RAW raw,
                               PUSHORT text, ULONG capChars);
/* One of the guard's operations (XHCI_PSW_OP_*). */
typedef ULONG (*XHCI_PSW_GUARD_OP)(PVOID context, ULONG op, ULONG arg,
                                   PULONG out);

typedef struct _XHCI_PSW_IO {
    XHCI_PSW_READ Read;
    XHCI_PSW_WRITE Write;
    XHCI_PSW_VALUE Value;   /* the start's decision only                    */
    XHCI_PSW_QUERY Query;   /* the step's readings; NULL reads as unread    */
    XHCI_PSW_GUARD_OP Guard;/* the guard; NULL holds with no arming         */
    PVOID Context;
    ULONG System;           /* XHCI_PSW_SYS_*                               */
    PUSHORT Text;           /* a buffer of XHCI_PSW_OPTIONS_CHARS for the
                             * string readers; NULL reads them as malformed */
} XHCI_PSW_IO, *PXHCI_PSW_IO;

/* What a route or release read. A field not reached is 0xFFFFFFFF. */
typedef struct _XHCI_PSW_STATE {
    ULONG Usb3Mask;     /* USB3PRM                                          */
    ULONG Usb3Now;      /* USB3_PSSEN read back                             */
    ULONG Usb2Mask;     /* XUSB2PRM                                         */
    ULONG Usb2Now;      /* XUSB2PR read back                                */
    ULONG Step;         /* XHCI_PSW_DONE or the XHCI_PSW_STEP_* it stopped  */
    ULONG Written;      /* XHCI_PSW_WROTE_* this route's writes accepted    */
} XHCI_PSW_STATE, *PXHCI_PSW_STATE;

/* One reading as the step kept it: what the routine saw, the query's
 * status, and its classification (XHCI_PSW_RD_*, _SZ_* or _EX_*); Value
 * is the DWORD taken (0xFFFFFFFF when not taken), or a string reading's
 * answer (XHCI_PSW_PE_* for the boot options, XHCI_PSW_IMG_* for the
 * image state). */
typedef struct _XHCI_PSW_OBS {
    ULONG State;
    ULONG Calls;
    ULONG Type;
    ULONG Length;
    ULONG Status;
    ULONG Value;
} XHCI_PSW_OBS, *PXHCI_PSW_OBS;

/* The image state's text as read, ASCII (another character as '?'), for
 * the record and the Tree: the first XHCI_PSW_IMAGE_TEXT bytes. */
#define XHCI_PSW_IMAGE_TEXT         40UL

/* The guard's readings and writes (record 16 section 21.5.1). */
typedef struct _XHCI_PSW_GUARD {
    ULONG G;            /* 1 only for a REG_DWORD 1                         */
    XHCI_PSW_OBS Ready; /* G's reading                                      */
    ULONG Routine;      /* 1 when the routine was there                     */
    ULONG OpenStatus;   /* XHCI_PSW_ST_NOT_RUN where not reached            */
    ULONG Disposition;
    ULONG ProbeStatus;
    ULONG WriteS;       /* ReadyAtStart's write                             */
    XHCI_PSW_OBS Session; /* ReadyAtStart's reading                         */
    ULONG S;            /* 0, 1 or XHCI_PSW_S_UNKNOWN                       */
    ULONG WriteG;       /* G's write, XHCI_PSW_ST_NOT_RUN when not made     */
    ULONG Outcome;      /* XHCI_PSW_GUARD_*                                 */
} XHCI_PSW_GUARD, *PXHCI_PSW_GUARD;

/*
 * One started lifetime (record 16 section 7a). Written accumulates every
 * route's accepted writes and is emptied only with On, by a start's
 * decision and by XhciPswLifeEnd. The rest is what the decision read, for
 * the log and the record; Id and Subsystem are 0xFFFFFFFF where not read.
 * The step's fields (section 21) are set by a start that reaches it and
 * cleared, as not read, by every other.
 */
typedef struct _XHCI_PSW_LIFE {
    ULONG On;
    ULONG Mode;         /* XHCI_PSW_MODE_*                                  */
    ULONG Written;      /* XHCI_PSW_WROTE_*                                 */
    ULONG Decision;     /* XHCI_PSW_DECIDE_*                                */
    ULONG Id;           /* PCI offset 0                                     */
    ULONG Found;        /* XhciIntelPortSwitch found as a DWORD             */
    ULONG Value;        /* its value, 0 when not found                      */
    ULONG Subsystem;    /* PCI offset 0x2C                                  */
    ULONG System;       /* XHCI_PSW_SYS_*                                   */
    ULONG SwitchState;  /* XhciIntelPortSwitch: XHCI_REG_*                  */
    ULONG SwitchSource; /* XHCI_REG_FROM_*                                  */
    ULONG Step;         /* 1 when the start reached section 21's step       */
    ULONG SettingState; /* XhciIntelPortSwitchSetup: XHCI_REG_*             */
    ULONG SettingValue; /* as found, 0 when not taken                       */
    ULONG SettingSource;/* XHCI_REG_FROM_*                                  */
    ULONG Setting;      /* XHCI_PSW_SETTING_* in force                      */
    XHCI_PSW_OBS Setup[XHCI_PSW_SETUP_VALUES];  /* XHCI_PSW_Q_SSIP..MINI    */
    XHCI_PSW_OBS PeKey;
    XHCI_PSW_OBS PeOptions;
    XHCI_PSW_OBS Hive;
    XHCI_PSW_OBS Image;
    UCHAR ImageText[XHCI_PSW_IMAGE_TEXT];
    XHCI_PSW_GUARD Guard;
    ULONG Verdict;      /* XHCI_PSW_V_*                                     */
    ULONG VerdictValue; /* what it names                                    */
    ULONG RouteStep;    /* the start's route's Step, when routed            */
    ULONG RouteWritten; /* and its accepted writes                          */
} XHCI_PSW_LIFE, *PXHCI_PSW_LIFE;

/* 1 when PCI offset 0's vendor/device dword names a controller with the
 * mux: Intel 1E31, 8C31, 9C31, 8CB1, 9CB1 or 8D31. */
ULONG XhciPswGate(ULONG vendorDevice);

/* 1 when the vendor is Intel: the only controllers the value is read on. */
ULONG XhciPswIntel(ULONG vendorDevice);

/* 1 when PCI offset 0x2C's subsystem dword names a board that cannot
 * switch (Sony 104D:90A8, Linux's one exemption). */
ULONG XhciPswBoardRefused(ULONG subsystem);

/* The XhciIntelPortSwitch rule: on a listed controller GATED when the
 * value was found and is not 0; on another Intel controller BYPASS only
 * when it was found and is exactly 2; otherwise OFF. */
ULONG XhciPswMode(ULONG vendorDevice, ULONG found, ULONG value);

/* Every switchable connector to xHCI, SuperSpeed terminations first.
 * Returns st->Step, the first access that failed; st->Written says which
 * writes were accepted. */
ULONG XhciPswRoute(const XHCI_PSW_IO *io, PXHCI_PSW_STATE st);

/* The registers in written back to EHCI: USB3_PSSEN then XUSB2PR written
 * 0, each read back; a register not in written is not touched. Returns
 * st->Step, the first access that failed. */
ULONG XhciPswRelease(const XHCI_PSW_IO *io, ULONG written,
                     PXHCI_PSW_STATE st);

/* The start: clear the lifetime, decide, and route when on. Returns 1 when
 * the route ran; the decision's own reads do not count. */
ULONG XhciPswLifeStart(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                       PXHCI_PSW_STATE st);

/* A return to D0: route again when On. Returns 1 when the route ran. */
ULONG XhciPswLifeResume(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                        PXHCI_PSW_STATE st);

/* A shutdown's D3: release Written, keeping On and Written. Returns 1 when
 * Written was not empty. */
ULONG XhciPswLifeRelease(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                         PXHCI_PSW_STATE st);

/* The stop and a refused start: release Written, then clear On and
 * Written. Returns 1 when Written was not empty. */
ULONG XhciPswLifeEnd(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                     PXHCI_PSW_STATE st);

/* ------------------------------------------------------------------ */
/* Section 21: the readings, the verdict, the guard, the record         */
/* ------------------------------------------------------------------ */

/* A DWORD reading (section 21.3's table): XHCI_PSW_RD_*, with *value the
 * DWORD when TAKEN. */
ULONG XhciPswClassifyDword(const XHCI_PSW_RAW *raw, PULONG value);

/* A string reading: XHCI_PSW_SZ_VALID only for success, one call, REG_SZ,
 * an even length of at least 2 within capChars characters, ending in its
 * one terminator; ABSENT for no call and a not-found status; else
 * MALFORMED. text holds what the routine copied. */
ULONG XhciPswClassifyString(const XHCI_PSW_RAW *raw, const USHORT *text,
                            ULONG capChars);

/* An existence test (an entry of NULL name and NOVALUE): THERE for one
 * call and success; NOT for no call and STATUS_OBJECT_NAME_NOT_FOUND, or,
 * when pathCounts, STATUS_OBJECT_PATH_NOT_FOUND too; else UNREAD. */
ULONG XhciPswClassifyExists(const XHCI_PSW_RAW *raw, ULONG pathCounts);

/* 1 when a token of the valid boot options, split at spaces and tabs and
 * with one leading '/' removed, is MININT, case ignored. chars excludes
 * the terminator. */
ULONG XhciPswOptionsMinint(const USHORT *text, ULONG chars);

/* 1 when the valid image state is exactly IMAGE_STATE_COMPLETE, case
 * ignored. */
ULONG XhciPswImageComplete(const USHORT *text, ULONG chars);

/* The Setup values' part of the verdict, over state[i] and value[i] in
 * XHCI_PSW_Q_SSIP..MINI order: XHCI_PSW_V_HELD_SETUP (*which the first of
 * SSIP, OOBE, MINI taken non-zero), _HELD_SETUPTYPE, _HELD_SETUP_UNREAD
 * (*which the first unread, malformed, or the primary absent), or
 * XHCI_PSW_V_NONE when they have no objection. */
ULONG XhciPswSetupHold(const ULONG *state, const ULONG *value, PULONG which);

/* XhciIntelPortSwitchSetup in force from its reading: an exact 1 or 2,
 * else automatic; on 98 SE and ME 1 is automatic too. */
ULONG XhciPswSettingInForce(ULONG state, ULONG value, ULONG system);

/* The guard, steps 1 to 7 of section 21.5.1, through io->Query and
 * io->Guard: returns 1 when it allows (S is 1). Every handle the guard
 * opened is closed through XHCI_PSW_OP_CLOSE before it returns. */
ULONG XhciPswGuardRun(const XHCI_PSW_IO *io, PXHCI_PSW_GUARD guard);

/* 1 when the start reached section 21's step - an Intel controller with
 * the switchover on (XhciIntelPortSwitch 1 on a listed one, 2 on any) and
 * the board passing - so a registry record of it may be written; 0 for
 * every other start, which writes nothing new (section 21.3). */
ULONG XhciPswRecordApplies(const XHCI_PSW_LIFE *life);

/* The shutdown hand-back's predicate (section 21.6): 0 for a lifetime
 * section 21's step held, 1 for every other (off, routed, not Intel). */
ULONG XhciPswShutdownAllowed(const XHCI_PSW_LIFE *life);

/* XhciIntelPortSwitchResult, REG_BINARY in the controller's driver key:
 * XHCI_PSW_REC_WORDS little-endian ULONGs, output only (section 21.7).
 * The snapshot's controller block carries the same words. */
#define XHCI_PSW_REC_MAGIC          0x57535058UL    /* "XPSW"              */
#define XHCI_PSW_REC_VERSION        1UL
#define XHCI_PSW_REC_WORDS          58UL
#define XHCI_PSW_REC_BYTES          (XHCI_PSW_REC_WORDS * 4UL)
#define XHCI_PSW_REC_AT_SETUP       13UL    /* 4 values x 3 words           */
#define XHCI_PSW_REC_AT_PE          25UL    /* key 2 words, options 3       */
#define XHCI_PSW_REC_AT_IMAGE       30UL    /* hive 2 words, value 3        */
#define XHCI_PSW_REC_AT_TEXT        35UL    /* 10 words of ASCII            */
#define XHCI_PSW_REC_AT_GUARD       45UL    /* 12 words                     */
#define XHCI_PSW_REC_AT_ROUTE       57UL

typedef struct _XHCI_PSW_REC_IN {
    ULONG TimeLo;       /* KeQuerySystemTime                                */
    ULONG TimeHi;
    ULONG DriverVersion;/* XHCI_VER_PACKED                                  */
    ULONG Flavour;      /* XHCI_SNAPSHOT_FLAVOUR_*                          */
} XHCI_PSW_REC_IN;

/* The record of a start into words[XHCI_PSW_REC_WORDS]. */
VOID XhciPswRecordBuild(const XHCI_PSW_LIFE *life, const XHCI_PSW_REC_IN *in,
                        PULONG words);

/* What a value holds (XhciPswRecordRead). */
#define XHCI_PSW_REC_ABSENT         0UL
#define XHCI_PSW_REC_OK             1UL
#define XHCI_PSW_REC_FOREIGN        2UL     /* not a record read here       */

/* A record's words, back into a lifetime's fields (the Tree's reader):
 * `present` 0 is ABSENT; not REG_BINARY (`binary` 0), shorter than the
 * record, or of another magic or version is FOREIGN and life is cleared.
 * in, when not NULL, gets the header's time, version and flavour. */
ULONG XhciPswRecordRead(ULONG present, ULONG binary, const UCHAR *data,
                        ULONG length, PXHCI_PSW_LIFE life,
                        XHCI_PSW_REC_IN *in);

#endif /* XHCI_PSW_H */
