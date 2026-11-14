/*
 * cm_qnetb.h - the builder of the query device's networking answer
 * (cm_qnet.h; design record 35 section 17, task 48.18): pure, DDK-free,
 * host-tested by test\test_cm_qnet.c. cm_net.c gathers the inputs on the
 * connection-manager thread (CmNetQueryFill), CmNetQueryBuild lays them
 * out, cm_qryos.c publishes the bytes and answers the request from them
 * (CmNetQueryAnswer, then CmNetQuerySetAge after its lock).
 *
 * IRQL: any; no lock, no allocation.
 */

#ifndef CM_QNETB_H
#define CM_QNETB_H

#include "../xhci_compat.h"
#include "cm_qnet.h"

typedef struct _CM_QN_LINK_IN {
    ULONG Link;
    ULONG Flags;                /* CM_QN_LF_* */
    ULONG DiscState;
    ULONG NetState;
    ULONG Uuid[4];
    ULONG VendorId;
    ULONG DeviceId;
    ULONG DeviceRev;
    ULONG Prtcstns;
    const char *VendorText;     /* NUL-terminated, or NULL */
    const char *DeviceText;
    ULONG Mbps;
    ULONG Lanes;
    ULONG Sent;
    ULONG Received;
    ULONG Resends;
    ULONG Malformed;
    ULONG LoginsSent;
    ULONG LoginFailures;
    ULONG LoginsReceived;
    ULONG LoginsRefused;
    ULONG ServicesUp;
    ULONG ServiceFailures;
    ULONG Restarts;
} CM_QN_LINK_IN, *PCM_QN_LINK_IN;

typedef struct _CM_QN_IN {
    ULONG Flags;                /* CM_QN_F_* but STOPPING, CLIENT, LINKS_CUT */
    ULONG EpochLo;
    ULONG EpochHi;
    ULONG NetValue;
    ULONG E2EValue;
    ULONG Cm;                   /* CM_ST_CM_* */

    /* The service block. */
    ULONG State;                /* CM_QN_STATE_* */
    ULONG Link;
    UCHAR Mac[6];
    UCHAR PeerMac[6];
    ULONG PeerStns;
    ULONG OurStns;
    ULONG E2E;
    ULONG Mbps;
    ULONG Lanes;
    ULONG MaxFrame;
    ULONG TxDesc;
    ULONG RxDesc;
    ULONG Generation;
    ULONG RxFrames;
    ULONG RxErrors;
    ULONG RxDropped;
    ULONG TxFrames;
    ULONG TxRefused;
    ULONG LinkUps;
    ULONG LinkDowns;
    ULONG RingsFailed;
    ULONG IrqState;         /* CM_QN_IRQ_*                                 */
    ULONG IrqKicks;
    ULONG IrqKind;          /* CM_QN_KIND_* (task 49.27)                   */

    /* The links: Count entries of Links, more than CM_QN_MAX_LINKS cut. */
    const CM_QN_LINK_IN *Links;
    ULONG LinkCount;

    /* The client's counters, XHCI_CM_NET_STATS's words from its Size on
     * (Words[0] is ignored and written as the block's bytes); NULL or 0
     * words: no client block. */
    const ULONG *ClientWords;
    ULONG ClientWordCount;
} CM_QN_IN, *PCM_QN_IN;

/* The answer into out (room bytes, at least CM_QN_MAX_BYTES for a whole
 * one): its bytes, 0 where room is too small. Pass, age and stopping are
 * the request's (CmNetQueryAnswer). */
ULONG CmNetQueryBuild(UCHAR *out, ULONG room, const CM_QN_IN *in);

/* The request's answer from a built one of `bytes`, into out (room
 * bytes), with CM_Q_IOCTL's rules: 2 (CM_Q_ANS_TOO_SMALL) under
 * CM_QN_HDR_BYTES, nothing written; 1 (CM_Q_ANS_HEADER) under the whole,
 * the header alone with CM_QN_O_BYTES the size needed; else 0
 * (CM_Q_ANS_WHOLE). The pass is written, and the stopping flag set where
 * `stopping`. *info the bytes written. */
ULONG CmNetQueryAnswer(UCHAR *out, ULONG room, const UCHAR *built,
                       ULONG bytes, ULONG pass, BOOLEAN stopping,
                       PULONG info);

/* The age into an answer CmNetQueryAnswer wrote (`info` bytes), after the
 * lock, from a clock read after the copy: CM_QN_UNKNOWN where `pass` is
 * 0. */
VOID CmNetQuerySetAge(UCHAR *out, ULONG info, ULONG pass, ULONG ageMs);

#endif /* CM_QNETB_H */
