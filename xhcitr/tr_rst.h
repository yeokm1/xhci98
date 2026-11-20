/*
 * tr_rst.h - the USB Tree's restart of a controller, the pure half
 * (roadmap-usb4 task 50.32; design record 25 section 27: stage 1, 2000
 * and later, and stage 2, 98 SE and ME). The start's evidence from the
 * recovery-status answer's lifetime part and, on 98 SE and ME, from the
 * removal (item 4), the four-part answer and its exit code (item 8), the
 * confirmation list's digest (item 6), the refusals' order (item 5) and
 * on 98 SE and ME item 7's drive, storage and network rules. No Windows
 * call; the host suite is test\test_trrst.c.
 *
 * Every user-visible string here is a DRAFT for the owner (the ids are
 * section 27 item 12's), kept word for word with the task's draft list.
 */

#ifndef TR_RST_H
#define TR_RST_H

#include "tr_core.h"
#include "tr_log.h"

/* ---- the start's evidence (item 4) ---------------------------------- */

/* The sampling history: the baseline I0 and every sample after the call.
 * Positive evidence only: S1 once seen stays seen. */
typedef struct _TR_RST_HIST {
    int BaseValid;          /* I0 a valid lifetime sample                  */
    TR_XHR Base;
    int Samples;            /* answers read after the call                 */
    int Valid;              /* ... of them valid lifetime samples          */
    int Differed;           /* a valid sample differed from I0             */
    int SeenCompleted;      /* ... one of them with XHR_F_STARTED set      */
    int Failed;             /* reads that failed or did not parse          */
    int NoPart;             /* answers that carried no lifetime part       */
} TR_RST_HIST;

#define TR_RST_S1   1       /* a start seen completed                      */
#define TR_RST_S2   2       /* a start seen to have begun                  */
#define TR_RST_S3   3       /* no new start seen                           */
#define TR_RST_S4   4       /* the tool cannot tell                        */

/* base: the last answer before the call, or NULL where none was read. */
void tr_rst_hist_init(TR_RST_HIST *h, const TR_XHR *base);
/* One sample after the call: x the parsed answer, or NULL for a read that
 * failed or did not parse. */
void tr_rst_hist_add(TR_RST_HIST *h, const TR_XHR *x);
/* TR_RST_S1 .. TR_RST_S4 from the history. */
int tr_rst_start(const TR_RST_HIST *h);

/* ---- the answer (item 8) -------------------------------------------- */

/* Line 1, the request (2000 and later). */
#define TR_RST_REQ_PREP     1   /* the list, the element or the class
                                   parameters failed: no call made (A18)   */
#define TR_RST_REQ_NODEV    2   /* the element's open: no such devinst (R10) */
#define TR_RST_REQ_DENIED   3   /* the call FALSE, access denied (R1)     */
#define TR_RST_REQ_FAILED   4   /* the call FALSE, another error (A9)     */
#define TR_RST_REQ_OK       5   /* the call TRUE (A1)                     */

/* Line 3, the devnode at the end. */
#define TR_RST_DN_GONE      1   /* CR_NO_SUCH_DEVNODE (N1)                 */
#define TR_RST_DN_UNREAD    2   /* the locate or the status read failed (N6) */
#define TR_RST_DN_STOPPED   3   /* present, not started or a problem (N2)  */
#define TR_RST_DN_STARTED   4   /* started, no problem                     */

/* A flag reading: clear, set, or a read that failed; F and P also "not
 * attempted", where the sequence ended before their read (W2 and W4 say
 * only that a read failed). */
#define TR_RST_NO       0
#define TR_RST_YES      1
#define TR_RST_UNKNOWN  2
#define TR_RST_NOTREAD  3

typedef struct _TR_RST_IN {
    const char *Name;       /* the controller's name as the tree shows it */
    int Request;            /* TR_RST_REQ_*                                */
    TR_U32 Error;           /* GetLastError of the failed step             */
    int Start;              /* TR_RST_S*                                   */
    int Dn;                 /* TR_RST_DN_*                                 */
    TR_U32 DnCode;          /* the CONFIGRET of a failed read (N6)         */
    TR_U32 Problem;         /* N2's problem number                         */
    int XhrOk;              /* the request at the end parsed               */
    TR_XHR Xhr;             /* ... its answer                              */
    int F;                  /* DI_NEEDREBOOT | DI_NEEDRESTART after the call
                               (TR_RST_NOTREAD where no call was made)     */
    int P;                  /* ... before it (TR_RST_NOTREAD where the
                               element was never opened)                   */
    int MarkEnd;            /* DN_NEED_RESTART at the end                  */
    int MarkBase;           /* ... at the baseline                         */
    /* 98 SE and ME (stage 2): Error is then the failing CONFIGRET. */
    int Rescans;            /* the rescans made, 0 to 2                    */
    TR_U32 RescanCode[2];   /* ... each one's answer (A20 where not 0)     */
    int RootFail;           /* the root not located for the second (A22)   */
    TR_U32 RootCode;
} TR_RST_IN;

/* Line 1 on 98 SE and ME (item 8). */
#define TR_RST_REQ_9X_NOPARENT  11  /* CM_Get_Parent failed (A21)          */
#define TR_RST_REQ_9X_VETO      12  /* the query CR_REMOVE_VETOED (A10)    */
#define TR_RST_REQ_9X_QFAIL     13  /* the query, another answer (A19)     */
#define TR_RST_REQ_9X_RMFAIL    14  /* the removal not CR_SUCCESS (A12)    */
#define TR_RST_REQ_9X_REMOVED   15  /* both accepted (A13)                 */

/* Line 2 on 98 SE and ME after A13: removed, and not found again (A11;
 * no line 3 follows). */
#define TR_RST_S_A11    5

/* Line 2 on 98 SE and ME (item 4): removed the removal answered
 * CR_SUCCESS; gone the id was not found once it had returned; found a
 * devnode of the id was located again in the settle; seenStarted ...
 * started with no problem; unsure a locate in the settle answered an error
 * other than no such devnode (never found then is S4, not A11). Where the
 * removal is no evidence (not removed, or not gone), the lifetime samples
 * h decide, as on NT. */
int tr_rst_start9x(int removed, int gone, int found, int seenStarted,
                   int unsure, const TR_RST_HIST *h);

/* What a settle saw (item 4), kept across the settles of one restart. */
typedef struct _TR_RST_SEEN {
    int Found;              /* a devnode of the id located                 */
    int Seen;               /* ... started with no problem                 */
    int Unsure;             /* a locate answered another error             */
} TR_RST_SEEN;

/* The configuration manager's calls of item 3's sequence and the settle,
 * given by the caller, so that their order is host-tested. Each call
 * answers its CONFIGRET; Locate with id NULL locates the root. */
typedef struct _TR_RST_OPS9X {
    void *Ctx;
    TR_U32 (*Locate)(void *ctx, TR_U32 *dn, const char *id);
    TR_U32 (*Parent)(void *ctx, TR_U32 *parent, TR_U32 dn);
    TR_U32 (*QueryRemove)(void *ctx, TR_U32 dn);
    TR_U32 (*Remove)(void *ctx, TR_U32 dn);
    TR_U32 (*Reenumerate)(void *ctx, TR_U32 dn);
    void (*Settle)(void *ctx, TR_RST_HIST *h, int gone, TR_RST_SEEN *s);
} TR_RST_OPS9X;

/* Item 3's sequence for the controller of instance id id, after item 5's
 * checks: the controller, its parent, the query-remove, the removal, the
 * parent's rescan and the settle; where the controller is not found again
 * or that rescan failed, one rescan of the root and the settle again; a
 * failed removal is followed by the parent's rescan alone. No call is made
 * through a handle a call did not return, and no removal follows a refused
 * query. Line 1's request and codes into in, line 2 into in->Start. */
void tr_rst_seq9x(const TR_RST_OPS9X *ops, const char *id, TR_RST_IN *in,
                  TR_RST_HIST *h);

/* 1 when an xhci98 controller is offered the restart (M1) by its
 * recovery status: on 2000 and later always; on 98 SE and ME not where
 * it reads stopped after repeated failures (terminal 2 or 3; item 3).
 * recState as TR_RST_CHECK's. */
int tr_rst_offered(int nt, int recState, TR_U32 terminal);

/* 1 when a devnode is a hub node of this driver for item 6's groups: the
 * root hub (an id beginning USB\XHCI98_ROOT_HUB), or a hub's FDO - the
 * service xhci98 and its ids say a hub (Class_09) or its id begins
 * USB\XHCI98_HUB, the project-owned ids this driver gives every hub it
 * drives (USB\XHCI98_HUB and USB\XHCI98_HUB30), which carry no Class_09.
 * Without case. */
int tr_rst_is_hubnode(const char *id, const char *service, int isHubId);

/* The answer into t, one line a part (A1 then S1 then N3, say), and the
 * one-shot run's exit code (INST_EXIT_*): 1 for R1, R10, A10 and A19; 2
 * for A18, A9, A12, A21, and after A13 for A11, A20 and A22; then, the
 * request accepted (A1, A13), 3 where W1, W3 or W5/W6 is said, 0 where S1
 * and N3 are said and nothing of line 4, 2 otherwise. H1 is the last line
 * where A1, S3 and W1 or W5 are said (revision 50; 2000 and later only),
 * and changes no exit code. The stopped controller's details after N4 are
 * the caller's. */
int tr_rst_answer(const TR_RST_IN *in, TR_TEXT *t);

/* The exit code alone, as tr_rst_answer returns it. */
int tr_rst_exit(const TR_RST_IN *in);

/* Line 3 alone (the state now), as tr_rst_answer writes it: A15 is
 * followed by it. */
void tr_rst_state(const TR_RST_IN *in, TR_TEXT *t);

/* Item 5's refusals: the refusal's text, then W5 where the devnode's
 * DN_NEED_RESTART reads set (mark TR_RST_YES), into t; the exit code (1).
 * name as for tr_rst_answer. */
int tr_rst_refused(const char *text, int mark, const char *name, TR_TEXT *t);

/* ---- the confirmation's digest (item 6) ----------------------------- */

/* The digest over the controller's instance id and every devnode id of
 * its subtree, upper-cased and sorted so the walk's order does not count
 * (tr_res_digest over the sorted list). ids is sorted in place. */
void tr_rst_digest(const char *ctrlId, char (*ids)[TR_ID_CHARS], int n,
                   char *out);

/* ---- the confirmation's list (item 6) ------------------------------- */

/* One devnode of a whole-tree walk, as the list needs it. */
typedef struct _TR_RST_NODE {
    int Parent;             /* its parent's index, -1 for the tree's root  */
    const char *Id;
    const char *Cls;        /* the setup class ("Keyboard", "Mouse", ...)  */
    const char *Name;       /* as the tree shows it                        */
    const char *Place;      /* "port 3", ...; "" where not known           */
    const char *Letters;    /* a disk's drive letters; "" where none       */
    int IsDisk;             /* setup class DiskDrive                       */
    int IsHubNode;          /* the root hub, or a hub's FDO of this driver */
    int IsRootHub;          /* the root hub                                */
    int ReadOk;             /* every property read whole                   */
    int Incomplete;         /* its children were not all read              */
    int Started;            /* started, with no problem                    */
    /* 98 SE and ME (stage 2, item 7); unused on NT. */
    const char *Hw;         /* the hardware ids, NUL-separated             */
    size_t HwBytes;
    const char *Compat;     /* the compatible ids, NUL-separated           */
    size_t CompatBytes;
    const char *Service;    /* NTMPDriver, PortDriver or DevLoader         */
    int LetState;           /* CurrentDriveLetterAssignment: TR_RST_LET_*  */
    const char *DrvLetters; /* ... its letters, where read                 */
} TR_RST_NODE;

#define TR_RST_LET_ABSENT   0   /* the Enum key has no such value          */
#define TR_RST_LET_READ     1
#define TR_RST_LET_FAILED   2   /* the key did not open, or the read failed */

/* The list's facts, and the subtree's ids for the digest (Cap of them). */
typedef struct _TR_RST_LIST {
    char (*Ids)[TR_ID_CHARS];
    int Cap;
    int Count;              /* devnodes below the controller               */
    int Lines;              /* the device lines written                    */
    int StoppedEmpty;       /* nothing below it but the root hub (C3)      */
    int HasDisk;            /* C4                                          */
    int Kbd;                /* a keyboard on it (C7)                       */
    int Mouse;
    int KbdElsewhere;       /* -1 not known (C8a), 0 none (C8), 1 one      */
    int MouseElsewhere;
    char Digest[TR_DIGEST_CHARS];
} TR_RST_LIST;

#define TR_RST_LIST_OK          0
#define TR_RST_LIST_INCOMPLETE  1   /* R11 */

/* The list of the controller n[top] from a whole-tree walk of count
 * devnodes (bound: the walk met its size bound), a line a device into t,
 * and the facts and digest into l. The walk is complete only where no
 * devnode below the controller (nor the controller) is Incomplete or has
 * ReadOk clear, no bound was met, every id fits l->Cap and every line fits
 * t: otherwise TR_RST_LIST_INCOMPLETE and nothing is to be shown. bad, the
 * walk's failure count, and any failed devnode outside the controller
 * make only C8's inventory unsure. */
int tr_rst_list(const TR_RST_NODE *n, int count, int top, int bad, int bound,
                const char *ctrlId, TR_RST_LIST *l, TR_TEXT *t);

/* ---- the elevated or Run As run's outcome (item 9) ------------------ */

#define TR_RST_CH_ANSWER    0   /* its answer, as written                  */
#define TR_RST_CH_A14       14  /* the prompt or dialog cancelled          */
#define TR_RST_CH_A15       15  /* started, no answer (then line 3)        */
#define TR_RST_CH_A16       16  /* not started                             */
#define TR_RST_CH_A23       23  /* exit 1 with no answer                   */

/* launched: the run was started; err its start's error where not;
 * finished: it ended inside the wait; haveExit: its exit code was read;
 * haveAnswer: an answer was read from the section. */
int tr_rst_child(int launched, TR_U32 err, int finished, int haveExit,
                 TR_U32 exitCode, int haveAnswer);
/* The exit code of each outcome but TR_RST_CH_ANSWER (whose code is the
 * run's own). */
int tr_rst_child_exit(int outcome);

/* ---- the strings (DRAFTS, section 27 item 12) ------------------------ */

#define TR_RST_M1   "&Restart Controller..."
#define TR_RST_M3   "To restart it, right-click the controller and choose " \
                    "Restart Controller."
#define TR_RST_C2   "These devices will disconnect, and should connect " \
                    "again when it has restarted:"
#define TR_RST_C3   "Its devices were removed when it stopped."
#define TR_RST_C4   "A drive's open files may be lost: close them first."
#define TR_RST_C7   "A keyboard or mouse on this controller stops while it " \
                    "restarts, and may not come back if the restart fails. " \
                    "Once you choose Restart, nothing more is asked of you " \
                    "until the answer."
#define TR_RST_C8K  "It is the only keyboard Windows has."
#define TR_RST_C8M  "It is the only mouse Windows has."
#define TR_RST_C8AK "The tool could not tell whether Windows has another " \
                    "keyboard."
#define TR_RST_C8AM "The tool could not tell whether Windows has another " \
                    "mouse."
#define TR_RST_RUNAS "Restart as &administrator..."
#define TR_RST_R1   "Restarting a controller needs an administrator."
#define TR_RST_R3   "This Windows does not offer the call that restarts a " \
                    "device."
#define TR_RST_R4   "Restarting a controller on 64-bit Windows needs " \
                    "XHCITR64.EXE in the same folder as XHCITR.EXE."
#define TR_RST_R6   "Setup is running: try again when it has finished."
#define TR_RST_R7   "Another program is removing a device; try again when " \
                    "it has finished."
#define TR_RST_R8   "The tool cannot tell how this controller stopped, so " \
                    "it does not restart it. Restart it from Device Manager, " \
                    "or restart Windows."
#define TR_RST_A14  "Nothing was restarted: administrator rights were not " \
                    "given."
#define TR_RST_A15  "No answer from the administrator's run."
#define TR_RST_A23  "Nothing was restarted: the run as another account " \
                    "could not report back to this window. The account may " \
                    "not be an administrator, or \"Protect my computer\" may " \
                    "have been ticked; try again with an administrator's " \
                    "account."
#define TR_RST_B1   "Controller restarted"
#define TR_RST_B2   "Controller restart"
/* Line 5 (revision 50; the owner's wording, 2026-11-19). */
#define TR_RST_H1   "A program may be using one of its drives: close it " \
                    "and try again, or restart the computer."

/* The tray shows an answer in its balloon only when it is shorter than
 * this (szInfo's 256 with room to spare); a longer one goes to a message
 * box. 1 when msg goes to the balloon. */
#define TR_RST_BALLOON_CHARS 250
int tr_rst_in_balloon(const char *msg);

/* The texts with a name or a number in them. */
void tr_rst_c1(char *out, size_t cap, const char *name);
void tr_rst_r9(char *out, size_t cap, const char *name);
/* 1 when an answer (an elevated or Run As run's) begins with R9 for this
 * controller: the window then asks again with the new list. */
int tr_rst_is_r9(const char *answer, const char *name);
void tr_rst_r10(char *out, size_t cap, const char *name);
void tr_rst_r11(char *out, size_t cap, const char *name);
void tr_rst_a16(char *out, size_t cap, TR_U32 err);
/* A17, and its exit code (2, item 8). */
int tr_rst_a17(char *out, size_t cap, const char *id);

/* The order of item 5's refusals before the question, as one decision:
 * the inputs, then the refusal's id (0 none). */
#define TR_RST_OK       0
#define TR_RST_NO_ROUTINE 3     /* R3 */
#define TR_RST_WOW64    4       /* R4 */
#define TR_RST_CONTAINED 2      /* R2 */
#define TR_RST_UNKNOWN_FAILED 8 /* R8 */
#define TR_RST_NEED_ADMIN 1     /* R1, with Run As on 2000 and XP          */
#define TR_RST_SETUP    6       /* R6 */
#define TR_RST_WALK     11      /* R11 */
typedef struct _TR_RST_CHECK {
    int Routines;           /* every routine resolved                      */
    int Wow64Limited;       /* XHCITR.EXE on 64-bit Windows, no XHCITR64   */
    int RecState;           /* 1 the recovery status read, 0 not served,
                               -1 failed                                   */
    TR_U32 Terminal;        /* ... its terminal, where read                */
    int QueryFailed;        /* the query's XHQ_F_FAILED, where not served  */
    int Admin;              /* an administrator (elevated on Vista and on) */
    int RunAsOffered;       /* 2000 or XP: R1 comes with Run As            */
    int Elevates;           /* Vista and later: the shield, not R1         */
    int SetupRunning;
    int WalkComplete;
} TR_RST_CHECK;
int tr_rst_check(const TR_RST_CHECK *c);

/* ---- 98 SE and ME: item 7's rules (stage 2) ------------------------- */

/* What the drive rule reads of the computer (item 7 steps 1 and 2). */
typedef struct _TR_RST_DRIVES {
    const char *WinDir;     /* GetWindowsDirectoryA; NULL where it failed  */
    const char *SysDir;     /* GetSystemDirectoryA; NULL where it failed   */
    TR_U32 Mask;            /* GetLogicalDrives                            */
    unsigned int Type[26];  /* GetDriveTypeA of each letter's root         */
} TR_RST_DRIVES;

/* One port of the controller or of a hub on it, from a reading of its
 * ports made with the walk (item 7, the storage rule). */
typedef struct _TR_RST_PORT {
    int Read;               /* its connection answer read                  */
    int Connected;          /* TR_CS_CONNECTED                             */
    int IsHub;
    int HubRead;            /* a connected hub: its own ports read          */
    const unsigned char *Dev;   /* the connection answer's device
                                   descriptor (18 bytes)                   */
    const unsigned char *Cfg;   /* the active configuration's descriptor
                                   (bConfigurationValue the connection
                                   answer's), NULL where it was not fetched
                                   or none is active                       */
    size_t CfgN;
    int Node;               /* its device's devnode in the walk, -1 none   */
    const char *Name;       /* its line for R12 where it has no devnode    */
} TR_RST_PORT;

/* 1 when a devnode's own id, its ids, class or service say USB storage
 * (item 7): an id with Class_08, a class DiskDrive, CDROM, hdc or
 * SCSIAdapter, an id beginning USBSTOR\ or SCSI\, a service USBSTOR,
 * xhciuas or USBNTMAP. */
int tr_rst_node_storage(const TR_RST_NODE *n);

/* A configuration descriptor of n bytes: 1 an interface of class 08h (any
 * alternate setting), 0 none, -1 not whole: its total length, a
 * descriptor's length, an interface descriptor shorter than 9 bytes, or
 * not as many interfaces (alternate setting 0) as it declares. */
int tr_rst_cfg_storage(const unsigned char *cfg, size_t n);

/* Item 7's rules on 98 SE and ME, in their order, for the controller
 * n[top] of a whole-tree walk of count devnodes (bad its failures, bound
 * its size bound met) and its ports p[0..np) (pbound: the port reading's
 * bound met); name the controller's. 0 where every rule passes, else the
 * refusal's id (13, 5, 18, 12, 16, 15) and its text in why, a text cut to
 * cap ending between characters by lead (as tr_copy_lead cuts). */
int tr_rst_rules9x(const TR_RST_DRIVES *d, const TR_RST_NODE *n, int count,
                   int top, int bad, int bound, const TR_RST_PORT *p, int np,
                   int pbound, const char *name, char *why, size_t cap,
                   TR_LEAD_FN lead);

/* The approved line of a controller the restart is not offered for. */
#define TR_RST_DM   "Restart it from Device Manager, or restart Windows."

#endif /* TR_RST_H */
