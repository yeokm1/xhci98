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
 * no stopping flag; returns its length. A record shorter than its header
 * or longer than CM_ST_MAX_BYTES, or whose own size disagrees with its
 * router count, is cut to its header and router count 0's length. */
ULONG CmQueryBuild(UCHAR *buf, const CM_QUERY_IN *in);

/* The IOCTL's answer from a built answer of `bytes`, into out (room
 * bytes). Returns CM_Q_ANS_*; *info the bytes written. */
#define CM_Q_ANS_WHOLE          0UL
#define CM_Q_ANS_HEADER         1UL     /* room < the whole: header alone */
#define CM_Q_ANS_TOO_SMALL      2UL     /* room < CM_Q_HDR_BYTES          */
ULONG CmQueryAnswer(UCHAR *out, ULONG room, const UCHAR *built, ULONG bytes,
                    ULONG pass, ULONG ageMs, BOOLEAN stopping, PULONG info);

/* Milliseconds between two tick counts' low words (modulo 2^32) at
 * `incr` 100 ns units per tick, without 64-bit arithmetic; held below
 * CM_Q_UNKNOWN. */
ULONG CmQueryAgeMs(ULONG ticksNow, ULONG ticksThen, ULONG incr);

#endif /* CM_QRYB_H */
