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
    (CM_Q_HDR_BYTES + CM_Q_RM_BYTES + CM_ST_MAX_BYTES + \
     CM_ST_MAX_ROUTERS * CM_Q_EXT_BYTES + CM_Q_ID_MAX)

/* Where the record starts in a version 2 answer: after the removal block. */
#define CM_Q_REC_AT             (CM_Q_HDR_BYTES + CM_Q_RM_BYTES)

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
    const UCHAR *Removal;   /* the removal block, CM_Q_RM_BYTES; NULL: not
                               watched, none counted                       */
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

/* The answer into buf (at least CM_Q_BUILD_MAX), version 2: the header,
 * the removal block, the record at CM_Q_REC_AT, the extensions, the id;
 * PASS 0, the age unknown,
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
 * The connection-manager thread's wait as it records it (CM_NHI.ThreadWait,
 * cm_thr.c): between waits, in a wait with a timeout, in one with none, or
 * in the bind a start left to run after the PnP start returned.
 */
#define CM_Q_WAIT_BUSY      0UL
#define CM_Q_WAIT_TIMED     1UL
#define CM_Q_WAIT_FOREVER   2UL
#define CM_Q_WAIT_BINDING   3UL

/*
 * The inert flag of the answer the query device holds until the thread's
 * first publication (task 42-A.7, design record 25 section 14): seeded
 * from the thread's actual wait - set with no thread (`running` FALSE) or
 * a thread already in its wait with no timeout, clear for one in a wait
 * with a timeout or in a start's bind (whose outcome its publication
 * then gives); for a thread between waits, `next` (whether the wait it
 * goes to has no timeout, as its publication judges it). Its pass after
 * the device's creation publishes the flag in any case.
 */
BOOLEAN CmQuerySeedInert(BOOLEAN running, ULONG wait, BOOLEAN next);

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

/* One record of the ring as the DebugView emission prints it (cm_log.c),
 * `n` bytes at `line`: below XhciCmLogVerbosity 4, the value of a record
 * CmQueryLogAddress names is written "--------", the answer's rule; at 4
 * the line is left as recorded, as xhci98.sys's ring holds its addresses
 * at 4. Returns 1 when a value was withheld. */
ULONG CmQueryLogLine(UCHAR *line, ULONG n, ULONG verbosity);

/*
 * The DebugView emission of a drained ring (cm_log.c): `n` bytes at
 * `text`, oldest first, taken from the ring in one drain under its lock.
 * `partial` nonzero when the wrap overwrote bytes since the last
 * emission, so the first line can be a record's tail with its label gone:
 * it is not handed on. Every other line - up to and including its LF, or
 * the bytes left at the end - goes through CmQueryLogLine and then to
 * `emit`, in order, at most CM_QL_EMIT_LINE bytes at a time. Returns the
 * lines handed on.
 */
#define CM_QL_EMIT_LINE         128UL
typedef VOID (*CM_QL_EMIT_FN)(PVOID context, const UCHAR *line, ULONG n);
ULONG CmQueryLogEmitLines(UCHAR *text, ULONG n, ULONG partial,
                          ULONG verbosity, CM_QL_EMIT_FN emit,
                          PVOID context);

/*
 * ---- The PCI answer (cm_query.h CM_QP_*, design record 25 section 4.7) ----
 *
 * Built at the request by CmQpciBuild (cm_qpci.c) over a reader the caller
 * supplies, then answered by CmQpciAnswer with the sizing rules above.
 */

/* The reader: `bytes` (4 or CM_QP_CFG_BYTES) of bdf's configuration space
 * from offset 0 into buf. CM_QP_RD_OK when read (all ones where the
 * function is absent is still OK), CM_QP_RD_ABSENT when the bus driver
 * says no function answers there, CM_QP_RD_FAIL when the read was
 * refused. Never a write. */
#define CM_QP_RD_OK             0UL
#define CM_QP_RD_ABSENT         1UL
#define CM_QP_RD_FAIL           2UL
typedef ULONG (*CM_QP_READ)(PVOID context, ULONG bdf, UCHAR *buf,
                            ULONG bytes, PULONG valid);
/* (*valid: with CM_QP_RD_OK, the bytes read - `bytes`, or fewer whole
   DWORDs where the bus driver answered fewer, the rest of buf zero.) */

typedef struct _CM_QP_PORT_IN {
    ULONG Bdf;              /* XHCI_TBT_BDF                               */
    ULONG Source;           /* XHCI_TBT_SRC_*                             */
    ULONG Flags;            /* CM_PORT_*                                  */
} CM_QP_PORT_IN;

/* Asked before the walk enters the secondary bus of `bridge` (a member,
 * or a bridge below one), whose 18h dword read `buses`: nonzero to enter.
 * The Windows 98 walk's rule (task 42-A.14): only numbers PCI.VXD reads
 * the same itself, or ones the worker's numbering gave. */
typedef ULONG (*CM_QP_ADMIT)(PVOID context, ULONG bridge, ULONG buses);

typedef struct _CM_QPCI_IN {
    ULONG Flags;            /* CM_QP_F_STOPPING; the builder adds the rest */
    ULONG EpochLo;
    ULONG EpochHi;
    ULONG NhiBdf;           /* CM_Q_UNKNOWN where not read                */
    ULONG PortCount;        /* above CM_QP_MAX_PORTS: cut, and TRUNCATED  */
    CM_QP_PORT_IN Ports[CM_QP_MAX_PORTS];
    ULONG ReadBelow;        /* 0: one CM_QP_S_NOT_READ_OS entry a member  */
    ULONG Reason;           /* ... its CM_QP_NR_* reason                  */
    ULONG Held;             /* bit p: member p not read below either, with
                               CM_QP_NR_HELD (its controller lost)       */
    CM_QP_READ Read;
    PVOID Context;
    CM_QP_READ BelowRead;   /* the functions below a member; NULL: Read   */
    PVOID BelowContext;
    CM_QP_ADMIT Admit;      /* NULL: every bus inside the member's range  */
    PVOID AdmitContext;
} CM_QPCI_IN, *PCM_QPCI_IN;

/* A reader of a prefix: the first `bytes` of bdf's configuration space
 * into buf; returns the bytes it read, 2 where the bus driver says no
 * function answers (HalGetBusData's form), fewer where it read less. */
typedef ULONG (*CM_QP_PREFIX)(PVOID context, ULONG bdf, UCHAR *buf,
                              ULONG bytes);

/* CM_QP_READ's answer for `bytes` of bdf (4 or CM_QP_CFG_BYTES) built one
 * DWORD at a time from prefixes of offset + 4 bytes - the read every
 * other configuration access of this driver makes, which Windows 98's
 * HAL answers where one long read may not. The first DWORD decides:
 * CM_QP_RD_ABSENT where the prefix answers 2, CM_QP_RD_FAIL where it
 * reads less. A later DWORD that cannot be read ends the assembly: the
 * rest is zero, *valid the bytes read, and the answer CM_QP_RD_OK.
 * `scratch` holds CM_QP_CFG_BYTES. */
ULONG CmQpciReadByDword(CM_QP_PREFIX prefix, PVOID context, ULONG bdf,
                        UCHAR *buf, ULONG bytes, UCHAR *scratch,
                        PULONG valid);

/* The PCI answer into buf (room at least CM_QP_MAX_BYTES; 0 returned
 * otherwise): the NHI, the members, the functions below them within the
 * CM_QP_MAX_* bounds. Returns its length. */
ULONG CmQpciBuild(UCHAR *buf, ULONG room, const CM_QPCI_IN *in);

/* The IOCTL's answer from a built one: CM_Q_ANS_TOO_SMALL under
 * CM_QP_HDR_BYTES (nothing written), CM_Q_ANS_HEADER under the whole (the
 * header alone, CM_QP_O_BYTES the size needed), else CM_Q_ANS_WHOLE;
 * *info the bytes written. */
ULONG CmQpciAnswer(UCHAR *out, ULONG room, const UCHAR *built, ULONG bytes,
                   PULONG info);

#endif /* CM_QRYB_H */
