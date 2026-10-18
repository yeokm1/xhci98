/*
 * cm_marks.h - the pure half of the marks answer (design record 21
 * section 7.4, revision 3; roadmap-usb4.md task 42-A.5): one domain's
 * table of the child PDOs it has seen on its active members since its
 * start, and the IOCTL_XHCICM_MARKS answer built from it (cm_query.h
 * CM_QM_*). cm_reenum.c keeps the table under the registration list's lock
 * and feeds it what the bridge filters' snapshots and the child filters'
 * PnP requests show; the host suite (test\test_cm_marks.c) drives it here.
 *
 * TWO SETS a member keeps apart, so that neither can crowd the other out:
 * CM_QM_MAX_CURRENT current entries (PDOs in a member bridge's last
 * snapshot) and CM_QM_MAX_TOMBS tombstones (entries whose PDO left it). A
 * tombstone is kept CM_QM_TOMB_MS, and longer while no answer read by a
 * poll has carried a marks generation past its departure; when the set is
 * full the oldest goes and, if no poll had seen it, Dropped is bumped.
 * A PDO is a key compared by pointer and never dereferenced.
 *
 * C89, DDK-free, no allocation, no lock (the caller's). IRQL: any.
 */

#ifndef CM_MARKS_H
#define CM_MARKS_H

#include "../xhci_compat.h"
#include "cm_query.h"

/* CM_MARK.Count[] indices, in the answer's order (CM_QM_E_COUNTS*). */
#define CM_MK_QUERY_REMOVE      0
#define CM_MK_CANCEL_REMOVE     1
#define CM_MK_SURPRISE          2
#define CM_MK_REMOVE_AFTER_QR   3
#define CM_MK_REMOVE_NO_QR      4
#define CM_MK_START_FAILED      5
#define CM_MK_COUNTS            6

typedef struct _CM_MARK {
    PVOID Pdo;              /* the key; NULL when the slot is free        */
    ULONG Tomb;             /* left the snapshot                          */
    ULONG Member;           /* the member's index in the domain's set     */
    ULONG MemberBdf;
    ULONG Bus;
    ULONG Address;
    ULONG Serial;
    ULONG Flags;            /* CM_QM_EF_*, but REPORTED and SAFE_EJECT_OFF,
                               which the table and the build decide       */
    ULONG Count[CM_MK_COUNTS];
    ULONG PendingAt;
    ULONG PendingGen;
    ULONG ChangedAt;
    ULONG TombAt;
    ULONG TombGen;          /* the generation that made it a tombstone    */
} CM_MARK, *PCM_MARK;

typedef struct _CM_MARKS {
    CM_MARK E[CM_QM_MAX_ENTRIES];
    ULONG Gen;              /* bumped on every change, past 0 to 1        */
    ULONG NextSerial;
    ULONG Incomplete;       /* the last sync could not list a current PDO */
    ULONG Dropped;          /* tombstones evicted unseen                  */
    ULONG ReadGen;          /* the newest generation a poll has read      */
    ULONG ReadAny;          /* ReadGen holds a reading                    */
} CM_MARKS, *PCM_MARKS;

/* An empty table (the domain's start and stop). */
VOID CmMarksReset(PCM_MARKS m);

/* The current entry of `pdo`, or NULL. */
PCM_MARK CmMarksFind(PCM_MARKS m, PVOID pdo);

/* The entry changed: its clock and the table's generation moved on. */
VOID CmMarksTouch(PCM_MARKS m, PCM_MARK e, ULONG now);

/*
 * Member `member` (at `memberBdf`) as its bridge last reported it: with
 * `active`, each of snap[0..count-1] has a current entry (one is made
 * where none is, with a new serial; where the current set is full the
 * table reads incomplete), and `truncated` (the answer held more than the
 * snapshot keeps) reads incomplete too; every current entry of the member
 * whose PDO is not in snap[] - all of them when not `active` - becomes a
 * tombstone, its departure cleared. Returns TRUE when anything changed.
 * The table's Incomplete is the OR over the members the caller syncs
 * between two CmMarksBegin calls.
 */
VOID CmMarksBegin(PCM_MARKS m);
BOOLEAN CmMarksSync(PCM_MARKS m, ULONG member, ULONG memberBdf,
                    PVOID const *snap, ULONG count, BOOLEAN truncated,
                    BOOLEAN active, ULONG now);

/* Every current entry of a member index at or past `members` made a
 * tombstone (the set shrank). */
BOOLEAN CmMarksDropMembers(PCM_MARKS m, ULONG members, ULONG now);

/* A saturating 16-bit count of what an attachment saw. */
VOID CmMarksCount(PCM_MARKS m, PCM_MARK e, ULONG which, ULONG now);

/* Tombstones past CM_QM_TOMB_MS that a poll has seen are freed. */
VOID CmMarksEvict(PCM_MARKS m, ULONG now);

/* A poll read an answer of generation `gen`. */
VOID CmMarksSeen(PCM_MARKS m, ULONG gen);

/* Whether generation `gen` has been read by a poll: ReadGen at or past
 * it (wrap-safe). */
BOOLEAN CmMarksWasSeen(const CM_MARKS *m, ULONG gen);

/* What the answer's header carries besides the table. */
typedef struct _CM_QM_IN {
    ULONG EpochLo;
    ULONG EpochHi;
    ULONG NhiBdf;
    ULONG SafeEjectOff;
} CM_QM_IN, *PCM_QM_IN;

/* The answer into out (at least CM_QM_MAX_BYTES), PASS 0 and no stopping
 * flag, which the answer to the request fills in (CmMarksAnswer); the
 * current entries first. Returns its length. */
ULONG CmMarksBuild(UCHAR *out, const CM_MARKS *m, const CM_QM_IN *in);

/* The request's answer from a built one of `bytes` (cm_qryb.h's
 * CM_Q_ANS_* and its sizing rules). */
ULONG CmMarksAnswer(UCHAR *out, ULONG room, const UCHAR *built, ULONG bytes,
                    ULONG pass, BOOLEAN stopping, PULONG info);

/*
 * The confirmation of record 21 section 7.4 (cm_query.h CM_QC_*): the
 * entry it names, or NULL with *why (CM_MK_CONF_*) when it is not one
 * the thread may act on: the epoch not this start's, no current entry of
 * that serial, its member, bus or address not the confirmation's, its
 * location unknown, not reported, no departure pending, or (with
 * `checkGen`, the USB4 profile) the generation not the departure's or not
 * portGen[member], the port's now (members past `ports` have none).
 */
#define CM_MK_CONF_OK           0UL
#define CM_MK_CONF_SHORT        1UL
#define CM_MK_CONF_EPOCH        2UL
#define CM_MK_CONF_SERIAL       3UL
#define CM_MK_CONF_IDENTITY     4UL
#define CM_MK_CONF_NOT_PENDING  5UL
#define CM_MK_CONF_GEN          6UL
PCM_MARK CmMarksConfirm(PCM_MARKS m, const UCHAR *v, ULONG bytes,
                        ULONG epochLo, ULONG epochHi, BOOLEAN checkGen,
                        const ULONG *portGen, ULONG ports, PULONG why);

#endif /* CM_MARKS_H */
