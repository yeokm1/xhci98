/*
 * cm_qryb.h - the builder of the query device's answer (cm_query.h;
 * design record 25 section 4): pure, DDK-free, host-tested by
 * test\test_cm_query.c. cm_qryos.c publishes what it builds and answers
 * the IOCTL from it.
 *
 * TWO STEPS. The connection-manager thread builds the whole answer after
 * each pass (CmQueryBuild: the record as CmStatusBuild made it, one
 * extension per router entry from CmQueryExt, the NHI's instance id) into
 * a buffer it owns, and publishes a copy. The IOCTL answers from that
 * copy (CmQueryAnswer), filling in what belongs to the moment of the
 * answer: the pass counter, the pass's age and the stopping flag.
 *
 * IRQL: any; no lock, no allocation.
 */

#ifndef CM_QRYB_H
#define CM_QRYB_H

#include "tbt_icm.h"
#include "tbt_u4cm.h"
#include "cm_status.h"
#include "cm_query.h"

#define CM_Q_ID_MAX             200UL

/* The largest answer this driver builds: a full record, its extensions
 * and the longest instance id. */
#define CM_Q_BUILD_MAX \
    (CM_Q_HDR_BYTES + CM_ST_MAX_BYTES + CM_ST_MAX_ROUTERS * CM_Q_EXT_BYTES + \
     CM_Q_ID_MAX)

typedef struct _CM_QUERY_IN {
    const UCHAR *Record;    /* as CmStatusBuild made it                    */
    ULONG RecordBytes;
    const UCHAR *Ext;       /* CM_Q_EXT_BYTES each, the record's order     */
    ULONG ExtCount;
    ULONG Live;             /* CM_LIVE_*                                   */
    ULONG Inert;            /* the thread waits with no timeout            */
    ULONG EpochLo;
    ULONG EpochHi;
    ULONG NhiBdf;           /* CM_ST_UNKNOWN where not read                */
    const char *Id;         /* the instance id, ASCII                      */
    ULONG IdBytes;
} CM_QUERY_IN, *PCM_QUERY_IN;

/*
 * The extensions of the record's router entries, into ext (room entries
 * of CM_Q_EXT_BYTES), from whichever connection manager the record names
 * (icm under CM_ST_CM_ICM, u4 under CM_ST_CM_USB4; either may be NULL).
 * Built in the record's own order by walking the same tables the record
 * was built from; an entry it cannot match is all unknown. Returns the
 * entry count, the record's router count bounded by room.
 */
ULONG CmQueryExt(UCHAR *ext, ULONG room, const UCHAR *record,
                 ULONG recordBytes, const TBT_ICM *icm, const TBT_U4CM *u4);

/* The answer into buf (at least CM_Q_BUILD_MAX), PASS 0, the age unknown,
 * no stopping flag, content generation 0; returns its length. A record shorter than its header
 * or longer than CM_ST_MAX_BYTES, or whose own size disagrees with its
 * router count, is cut to its header and router count 0's length. */
ULONG CmQueryBuild(UCHAR *buf, const CM_QUERY_IN *in);

/* The content generation (CM_Q_O_CONTENT_GEN) of a built answer `next`,
 * written into it and returned: 1 where there is no previous one (`prev`
 * NULL or prevBytes 0, the start's first); the previous one's where the
 * two are equal but for the pass counter, its age, the generation itself
 * and the record's registry-only fields (its generation, uptime, system
 * time and echo); else the previous one's plus one, past 0 to 1. */
ULONG CmQueryContentGen(UCHAR *next, ULONG nextBytes, const UCHAR *prev,
                        ULONG prevBytes);

/* The IOCTL's answer from a built answer of `bytes`, into out (room
 * bytes). Returns CM_Q_ANS_*; *info the bytes written. */
#define CM_Q_ANS_WHOLE          0UL
#define CM_Q_ANS_HEADER         1UL     /* room < the whole: header alone */
#define CM_Q_ANS_TOO_SMALL      2UL     /* room < CM_Q_HDR_BYTES          */
ULONG CmQueryAnswer(UCHAR *out, ULONG room, const UCHAR *built, ULONG bytes,
                    ULONG pass, ULONG ageMs, BOOLEAN stopping, PULONG info);

/* The device instance id inside a device interface's symbolic link name
 * (IoRegisterDeviceInterface: "\??\PCI#VEN_..&DEV_..#3&..&0&20#{guid}"):
 * the text between the prefix's last backslash and the last '#', each '#'
 * read as a backslash, into id (room bytes, no NUL). Returns its length, 0
 * where the name has no such part or holds a character outside printable
 * ASCII. */
ULONG CmQueryIdFromLink(const USHORT *link, ULONG chars, char *id,
                        ULONG room);

/* Milliseconds between two tick counts' low words (modulo 2^32) at
 * `incr` 100 ns units per tick, any period, without 64-bit arithmetic;
 * held below CM_Q_UNKNOWN; 0 where ticksNow is before ticksThen (the
 * difference's top bit set). */
ULONG CmQueryAgeMs(ULONG ticksNow, ULONG ticksThen, ULONG incr);

/* The age written into an answer CmQueryAnswer returned (`info` bytes),
 * after the lock, from a clock read after the copy: CM_Q_UNKNOWN where
 * `pass` is 0. */
VOID CmQuerySetAge(UCHAR *out, ULONG info, ULONG pass, ULONG ageMs);

/* The content generation of a published answer moved one on (past 0 to
 * 1), for a change the thread's builds do not see (the stopping flag);
 * returns it. */
ULONG CmQueryBumpGen(UCHAR *answer, ULONG bytes);

/*
 * ---- The log answer (cm_query.h CM_QL_*, design record 25 section 4.5) ----
 *
 * The counters are text the thread writes after each pass (CmQueryLines,
 * from the stop dump's name tables) and publishes with the status answer;
 * the log is the ring, copied at the request under its own lock
 * (cm_qryos.c) by CmQueryLogAnswer, which leaves the ring as it was.
 */

/* Lines "name=XXXXXXXX" CR LF for names[i] and values[i], i < count,
 * appended to out at *at, out being room bytes in all; *at moves past
 * what was written. A line that does not fit is not written, nor any
 * after it, and *cut is set to 1; once *cut is set, nothing more is
 * written. A name byte outside 0x21..0x7E is written '.'. Returns the
 * lines written. */
ULONG CmQueryLines(UCHAR *out, ULONG room, PULONG at,
                   const char *const *names, const ULONG *values,
                   ULONG count, PULONG cut);

typedef struct _CM_QLOG_IN {
    ULONG Flags;            /* CM_QL_F_RECORDING, _REFUSED, _STOPPING,
                               _EMITTED and _INERT; the builder adds the
                               rest                                      */
    ULONG Verbosity;
    ULONG VerbosityRead;
    ULONG EpochLo;
    ULONG EpochHi;
    const UCHAR *Ctr;       /* the counters' text, CtrBytes bytes        */
    ULONG CtrBytes;         /* above CM_QL_CTR_MAX: none, and cut        */
    ULONG CtrCut;
    const UCHAR *Ring;      /* the ring as XHCI_LOG holds it: RingBytes  */
    ULONG RingBytes;        /* a power of two, at most CM_QL_LOG_MAX;    */
    ULONG Head;             /* any other shape is an empty log           */
    ULONG Used;
    ULONG Dropped;
    ULONG Appends;
    ULONG Pass;             /* the counters' pass                         */
} CM_QLOG_IN, *PCM_QLOG_IN;

/* The log answer into out (room bytes), with CmQueryAnswer's rules:
 * CM_Q_ANS_TOO_SMALL under CM_QL_HDR_BYTES (nothing written);
 * CM_Q_ANS_HEADER under the whole, the header alone with CM_QL_O_BYTES the
 * size needed; else CM_Q_ANS_WHOLE. *info the bytes written. The log is
 * the ring's Used bytes oldest first: a byte outside 0x20..0x7E, CR and LF
 * written '.'; when the oldest bytes may be a record's tail (Dropped != 0,
 * or CM_QL_F_EMITTED: a drain) the text starts after the first LF
 * (CM_QL_F_LOG_CUT); the value of a record CmQueryLogAddress names is
 * written "--------" (CM_QL_F_WITHHELD). The age is CM_Q_UNKNOWN until
 * CmQueryLogSetAge. */
ULONG CmQueryLogAnswer(UCHAR *out, ULONG room, const CM_QLOG_IN *in,
                       PULONG info);

/* The age written into a log answer of `info` bytes: CM_Q_UNKNOWN where
 * `pass` is 0. */
VOID CmQueryLogSetAge(UCHAR *out, ULONG info, ULONG pass, ULONG ageMs);

/* 1 when a record whose label is the `n` bytes at `label` carries an
 * address as its value: cm.live.bar-changed, the BAR as read (cm_nhi.c),
 * and cm.reenum.9x.gone.superseded and .id-changed, a CONFIGMG devnode
 * handle (cm_cfgwk.c). A record added whose value is an address is added
 * here. */
ULONG CmQueryLogAddress(const UCHAR *label, ULONG n);

#endif /* CM_QRYB_H */
