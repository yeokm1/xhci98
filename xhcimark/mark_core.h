/*
 * mark_core.h - the pure half of XHCIMARK.EXE and XHCIMK64.EXE, the
 * safe-removal mark service (design record 21 section 7.4, revision 3;
 * roadmap-usb4.md task 42-A.5).
 *
 * Everything here is a function of its arguments: no Windows header, no
 * registry, no device, no file. The service links it with
 * ..\xhciinst\inst_core.c (record 24 section 4.3's protected rule and the
 * status record's check) and its -selftest runs it over canned answers.
 * C89.
 */

#ifndef MARK_CORE_H
#define MARK_CORE_H

#include <stddef.h>
#include "../xhciinst/inst_core.h"

#define MK_NAME             "xhcicm"    /* the name the mark is          */
#define MK_FILTERS_BYTES    1024        /* a LowerFilters value read     */

/* ---- the query answer, as much of it as the service reads (cm_query.h) -- */

typedef struct _MK_QUERY {
    INST_U32 Bytes;
    INST_U32 Live;
    INST_U32 Flags;
    INST_U32 EpochLo;
    INST_U32 EpochHi;
    INST_U32 Stage;         /* the record's CM_ST_STAGE_*                */
    INST_U32 Ports;         /* members in the record                     */
    INST_U32 Active;        /* bit n: member n active                    */
    INST_U32 Port[4];       /* their locations (bus 15:8, dev 7:3, fn 2:0) */
    char Id[208];           /* the NHI's instance id                     */
} MK_QUERY;

/* 1 with q filled when b[0..n) is a whole query answer whose record
 * checks (inst_st_check); 0 for anything else. -1 when it is a header
 * alone that asks for more (*need the size). */
int mk_query_parse(const unsigned char *b, size_t n, MK_QUERY *q,
                   INST_U32 *need);

/* ---- the marks answer (cm_query.h CM_QM_*) ------------------------------ */

#define MK_MAX_ENTRIES      256     /* CM_QM_MAX_ENTRIES */

typedef struct _MK_ENTRY {
    INST_U32 Member;
    INST_U32 Bus;
    INST_U32 Address;
    INST_U32 Serial;
    INST_U32 Flags;         /* CM_QM_EF_*                                */
    INST_U32 Pending;
    INST_U32 PendingGen;
} MK_ENTRY;

typedef struct _MK_MARKS {
    INST_U32 Bytes;
    INST_U32 EpochLo;
    INST_U32 EpochHi;
    INST_U32 Gen;
    INST_U32 Flags;         /* CM_QM_F_*                                 */
    INST_U32 Dropped;
    INST_U32 Count;
    MK_ENTRY E[MK_MAX_ENTRIES];
} MK_MARKS;

/* As mk_query_parse, for the marks answer. */
int mk_marks_parse(const unsigned char *b, size_t n, MK_MARKS *m,
                   INST_U32 *need);

/* The entry flags the service decides by (cm_query.h). */
#define MK_EF_LOCATED       0x01UL
#define MK_EF_REPORTED      0x02UL
#define MK_EF_BUS_ATT       0x04UL
#define MK_EF_ADD_ATT       0x08UL
#define MK_EF_REMOVABLE     0x10UL
#define MK_EF_SAFE_OFF      0x20UL
#define MK_F_INCOMPLETE     0x01UL
#define MK_F_STOPPING       0x02UL
#define MK_F_SAFE_OFF       0x04UL

/* ---- decisions ------------------------------------------------------------ */

/* Whether a domain is verifiable at this poll (decision M11): its NHI
 * devnode present, started, bound to xhcicm (`devOk`), both answers read,
 * the record's stage active, neither the inert nor the stopping flag set,
 * the liveness state ACTIVE, the same epoch in both. */
int mk_verifiable(int devOk, const MK_QUERY *q, const MK_MARKS *m);

/* Whether member location `bdf` is an active member of q's set. */
int mk_member_active(const MK_QUERY *q, INST_U32 bdf);

/* The write's checks, in order (section 7.4's three, the first failing
 * one returned; MK_W_OK when all hold). */
#define MK_W_OK             0
#define MK_W_NOT_LOCATED    1   /* the entry's location is not known      */
#define MK_W_NOT_REPORTED   2
#define MK_W_SAFE_OFF       3   /* the domain's XhciCmSafeEject is 0      */
#define MK_W_NOT_ACTIVE     4   /* the member is not an active member     */
#define MK_W_NO_PORT        5   /* no devnode, or two, at the member      */
#define MK_W_NO_UNIT        6   /* no child devnode of the port at the
                                   entry's location, or two              */
#define MK_W_PROTECTED      7   /* an NHI, an NHI's ancestor, the host's
                                   own USB controller                     */
#define MK_W_PRESENT        8   /* xhcicm already in LowerFilters         */
#define MK_W_UNREADABLE     9   /* its status or LowerFilters could not be
                                   read                                   */
const char *mk_w_text(int w);

/* The restart's wish (the first plug a rebuild overtook): the entry
 * reported and located, safe eject on, the value present, and no
 * attachment of this driver on the stack now or one that did not answer
 * Removable. A pending departure does not stop it (a first plug's install
 * leaves one); the unit's fresh status decides. 1 when a restart is to be
 * considered. */
int mk_restart_wanted(const MK_ENTRY *e, int valuePresent);

/* The reconciliation of one record (section 7.4, "Departures and
 * reconciliation"): MK_R_KEEP, MK_R_REMOVE (the mark taken off and the
 * record deleted) or MK_R_LEAVE (not verifiable now; looked at again).
 *   presence    MK_P_*: present (CM_Locate_DevNode, normal), absent (only
 *               a phantom), or unknown (a lookup failed)
 *   ownerKnown  an answer lists the owning member (the parent's location
 *               for a present devnode, the record's for an absent one)
 *   ownerVerif  that domain is verifiable
 *   anyUnverif  some NHI bound to xhcicm is not verifiable
 *   anyVerif    at least one is verifiable
 *   memberActive  the owning member is an active member of its domain
 *   listed      the owning domain lists a reported unit at the devnode's
 *               location behind that member
 *   incomplete  the owning domain answered incomplete: an omission there
 *               proves no departure, but an inactive member or safe eject
 *               off still removes
 *   safeOff     its safe eject is off
 * With no owner known, nothing is decided unless every NHI is verifiable
 * and one is. */
#define MK_R_KEEP           0
#define MK_R_REMOVE         1
#define MK_R_LEAVE          2
#define MK_P_ABSENT         0
#define MK_P_PRESENT        1
#define MK_P_UNKNOWN        2
int mk_reconcile(int presence, int ownerKnown, int ownerVerif,
                 int anyUnverif, int anyVerif, int memberActive, int listed,
                 int incomplete, int safeOff);

/* Whether the marks answer lists a unit at bus/address behind member
 * `member`: MK_L_LISTED (a reported, located entry there), MK_L_UNSURE (no
 * such entry, but a reported entry behind that member whose location is
 * not known - it may be this unit, so its absence proves nothing),
 * MK_L_NOT (neither). *entry the listed one, or NULL. */
#define MK_L_NOT            0
#define MK_L_LISTED         1
#define MK_L_UNSURE         2
int mk_listed(const MK_MARKS *m, INST_U32 member, INST_U32 bus,
              INST_U32 address, const MK_ENTRY **entry);

/* ---- the multi-string ----------------------------------------------------- */

/* `in` (inBytes, possibly 0 or one empty string) with MK_NAME appended as
 * the last string, the others kept in order, into out (cap bytes);
 * returns the new length with its double terminator, or 0 when it does
 * not fit. */
size_t mk_msz_append(const char *in, size_t inBytes, char *out, size_t cap);

/* ---- the record under Services\xhcimark\Marks ------------------------------ */

#define MK_REC_WRITING      1
#define MK_REC_WRITTEN      2
/* "m=XXXX e=XXXXXXXX:XXXXXXXX s=writing" (the member's location, the
 * epoch high and low words, the state). */
void mk_rec_format(char *out, size_t cap, INST_U32 member, INST_U32 epochHi,
                   INST_U32 epochLo, int state);
/* 1 with the fields when `text` is that form; 0 otherwise. */
int mk_rec_parse(const char *text, INST_U32 *member, INST_U32 *epochHi,
                 INST_U32 *epochLo, int *state);

/* ---- the confirmation (cm_query.h CM_QC_*) ------------------------------- */

#define MK_QC_BYTES         28
void mk_confirm_build(unsigned char *out, const MK_MARKS *m,
                      const MK_ENTRY *e);

/* ---- the poll's cadence --------------------------------------------------- */

/* Milliseconds to the next poll: 1000 while any NHI answered, 10000 when
 * none did (record 25 section 3.4's cadence). */
INST_U32 mk_poll_ms(int anyAnswered);

/* ---- Stor98 on a tunnelled drive (record 31 section 6; record 21 section
 * 7.5 was 3.2.0.0's) ------------------------------------------------------ */

#define MK_S98_VALUE        "Stor98Inf" /* Parameters: the staged INF   */
#define MK_S98_KEY          "Stor98"    /* the records, one per devnode */
#define MK_S98_CC           "PCI\\CC_010802"
#define MK_S98_TUNNEL_ID    "PCI\\XHCI98_TUNNEL&CC_010802"
#define MK_S98_DEPTH_MAX    6   /* the filter chain's bound (record 31 4.3) */
#define MK_S98_PROB_DISABLED 0x00000016UL

/* The systems the fallback knows (record 31 section 6.2). */
#define MK_S98_OS_NONE      0   /* not told, or below 2000                */
#define MK_S98_OS_LOW       1   /* 2000 to 8: a devnode with no driver    */
#define MK_S98_OS_81        2   /* 8.1: none, or stornvme (the 8.1 arm)   */
#define MK_S98_OS_10        3   /* 10 and later: nothing                  */
/* The system from an NT major, minor and build: 5.0 to 6.2 LOW; 6.3 below
 * build 10000 81; 6.3 from build 10000, 6.4 and above 10; else NONE. */
int mk_s98_os(INST_U32 major, INST_U32 minor, INST_U32 build);
/* Two readings of the system (the version the API answers, the one the
 * shared user data page keeps) must agree: else MK_S98_OS_NONE. */
int mk_s98_os_both(int a, int b);
/* 1 for stornvme, case ignored. */
int mk_s98_inbox(const char *service);

/* Why a tunnelled drive arrived without the id (record 31 section 4.4), as
 * far as the service can tell: hasId the id among its compatible ids,
 * portFilter the tunnel port's UpperFilters naming xhcicm (-1 unread),
 * depth the devnodes from the port down to it (1 directly below). */
#define MK_S98_ID_HAS       0   /* it carries the id (outranked on 8.1)   */
#define MK_S98_ID_NOFILTER  1   /* the port carries no filter of ours     */
#define MK_S98_ID_DEPTH     2   /* below the chain's depth                */
#define MK_S98_ID_EARLY     3   /* enumerated before the id reached it    */
#define MK_S98_ID_UNKNOWN   4   /* the port's filters unread              */
#define MK_S98_ID_OS        5   /* 8.1 and later: no id by design         */
int mk_s98_noid_why(int hasId, int portFilter, int depth);
const char *mk_s98_noid_text(int why);
#define MK_S98_RETRY_MS     10000UL     /* between attempts in one arrival */
#define MK_S98_ATTEMPTS     3           /* the first and two retries      */

/* 1 when `inf` is <windir>\INF\oem<digits>.inf (case ignored; windir with
 * or without its trailing backslash): the only form the switch acts on. */
int mk_s98_inf_ok(const char *inf, const char *windir);

/* A devnode's record (the value's text). */
#define MK_S98_NONE         0   /* no record                               */
#define MK_S98_DISABLE      1   /* "disable": about to be stopped           */
#define MK_S98_INSTALL      2   /* "install": stopped, Stor98 going on      */
#define MK_S98_DONE         3   /* "done": read back running Stor98, final  */
#define MK_S98_BUSY         4   /* "busy": Windows refused the stop         */
#define MK_S98_FAILED       5   /* "failed": anything else, final           */
#define MK_S98_ENABLE       6   /* "enable": its enable owed (not verified) */
#define MK_S98_RESTART      7   /* "restart": the install asked for one     */
#define MK_S98_GIVE         8   /* "give": a driverless devnode, installing */
#define MK_S98_GIVE_RESTART 9   /* "give-restart": that install asked for a
                                   restart of Windows                      */
/* The text of a state; NULL for MK_S98_NONE or an unknown state. */
const char *mk_s98_rec_text(int state);
/* The state a record's text holds: MK_S98_NONE for NULL; an unknown text
 * reads as MK_S98_FAILED, so a record nobody can read stops the switch. */
int mk_s98_rec_parse(const char *text);

/* Whether one more attempt is allowed now, in the devnode's current
 * arrival: with no record, the first; after "busy", up to MK_S98_ATTEMPTS
 * in the arrival, MK_S98_RETRY_MS apart; a new arrival starts again at 0.
 * Never for "done", "failed", or a switch's own states. */
int mk_s98_retry(int record, int attempts, INST_U32 sinceMs);

/* 1 when an ancestor of `node` (its parent and up, at most 64 steps) is
 * marked in member[]: parent[] the walk's parent indices (-1 the root). */
int mk_s98_below(const int *parent, int n, int node, const int *member);

/* One devnode against section 7.5.3's rules; MK_S98_GO or the first
 * reason that refuses (mk_s98_why_text). */
typedef struct _MK_S98_IN {
    int Os;                 /* MK_S98_OS_*                              */
    int PolicyOk;           /* mk_s98_inf_ok of the value               */
    int WalkWhole;          /* this poll's walk had no failure          */
    int IsPci;              /* its id starts PCI\                       */
    int Nvme;               /* its compatible ids carry MK_S98_CC       */
    const char *Service;    /* its Service value, or NULL               */
    int StatusOk;           /* its status read                          */
    int Started;            /* DN_STARTED                               */
    int Problem;            /* DN_HAS_PROBLEM                           */
    INST_U32 ProblemCode;   /* its problem number                       */
    int Below;              /* below an active member, domain verifiable */
    int Protected;          /* mk_protect's node                        */
    int InstallPending;     /* a Windows installation pending or unknown */
    int Record;             /* MK_S98_*                                 */
    int Attempts;           /* attempts in this arrival                 */
    INST_U32 SinceMs;       /* since the last of them                   */
} MK_S98_IN;
#define MK_S98_GO           0
#define MK_S98_W_CLASS      1
#define MK_S98_W_SERVICE    2
#define MK_S98_W_POLICY     3
#define MK_S98_W_WALK       4
#define MK_S98_W_TUNNEL     5
#define MK_S98_W_PROTECTED  6
#define MK_S98_W_STATE      7
#define MK_S98_W_RECORD     8
#define MK_S98_W_PENDING    9
/* Logged-once markers of the 2000 and XP unit restart (xhcimark.c), never
 * answers of mk_s98_wanted. */
#define MK_S98_W_DECLINED   100
#define MK_S98_W_UNIT       101
#define MK_S98_W_AFTER      102
#define MK_S98_W_OS         10
/* Record 31 section 6.2: on 2000 to 8.1 a devnode with no driver (Service
 * empty), not started and not disabled; on 8.1 also one started on
 * stornvme (the 8.1 arm). Never one on Stor98 or another driver, never on
 * 10. A NULL Service is one that could not be read. */
int mk_s98_wanted(const MK_S98_IN *in);
/* 1 when the devnode takes the 8.1 arm (a stop, the install, a start); 0
 * for the install over nothing. */
int mk_s98_displaces(const MK_S98_IN *in);
const char *mk_s98_why_text(int why);

/* What a record asks for outside a switch. "give" (an install over no
 * driver cut short): once present, "done" when it runs Stor98, "failed"
 * otherwise; nothing to enable, since nothing was stopped. "disable",
 * "install" and
 * "enable" (a switch cut short, or an enable that did not hold): nothing
 * while the devnode is absent (it may come back disabled); its enable when
 * it is present and disabled, the record kept until the enable is seen;
 * once present and not disabled, "done" when it runs Stor98 (s98Started)
 * and "failed" otherwise. "restart": decided at the service's first poll
 * (firstPoll) once present, the same way. Anything else: nothing. */
#define MK_S98_R_NOTHING    0
#define MK_S98_R_ENABLE     1   /* enable it; the record stays           */
#define MK_S98_R_FAIL       2   /* record "failed"                       */
#define MK_S98_R_DONE       3   /* record "done"                         */
int mk_s98_recover(int record, int present, int disabled, int s98Started,
                   int firstPoll);

/* ---- a plain drive's install set to run again (record 31 section 6.2) -- */

/* A driverless NVMe function's state a unit restart admits: not started,
 * and no problem or the problem "not configured" (1), "failed install"
 * (28) or "reinstall" (18) - never disabled or any other problem. */
int mk_s98_driverless(int started, int problem, INST_U32 code);

/* CONFIGFLAG_FAILEDINSTALL cleared and CONFIGFLAG_REINSTALL set, every
 * other flag kept: 0 when `cfg` reads so already (nothing to write), else
 * 1 with *out the value to write. */
#define MK_CONFIGFLAG_FAILEDINSTALL 0x00000040UL
#define MK_CONFIGFLAG_REINSTALL     0x00000020UL
int mk_s98_rerun_flags(INST_U32 cfg, INST_U32 *out);

/* One devnode below a unit the safe-removal mark restarts ("The mark's
 * restart", task 44.52). */
typedef struct _MK_S98_MF {
    int Nt5;                /* Windows 2000 or XP (NT 5.x)                */
    int PolicyOk;           /* the policy value names a staged INF        */
    int Below;              /* strictly below the unit                    */
    int Nvme;               /* its compatible ids carry MK_S98_CC         */
    int HasId;              /* they carry MK_S98_TUNNEL_ID                */
    const char *Service;    /* its Service value, or NULL when unread     */
    int Declined;           /* seen with the id and no driver this arrival */
    int Protected;          /* mk_protect's node                          */
    int StatusOk;           /* its status read                            */
    int Started;            /* DN_STARTED                                 */
    int Problem;            /* DN_HAS_PROBLEM                             */
    INST_U32 ProblemCode;   /* its problem number                         */
} MK_S98_MF;
/* 1 when the mark's restart first sets this devnode's install to run
 * again: on NT 5.x with the policy value good, a plain NVMe function below
 * the unit with no driver, no id, in an admitted state, not declined and
 * not protected. Never one with a driver or an unread Service. */
int mk_s98_mark_sets(const MK_S98_MF *in);

/* ---- the self-test -------------------------------------------------------- */

int mk_selftest(void (*report)(int ok, const char *what));

#endif /* MARK_CORE_H */
