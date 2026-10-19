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

/* ---- the self-test -------------------------------------------------------- */

int mk_selftest(void (*report)(int ok, const char *what));

#endif /* MARK_CORE_H */
