/*
 * xhci_qryb.h - the composer of IOCTL_XHCI98_QUERY's answer (xhci_qry.h;
 * design record 13 section 8.12): pure, DDK-free, host-tested by
 * test\test_xhq.c. hcd_door.c gathers what it needs under the controller
 * lock and calls it into the request's system buffer.
 *
 * IRQL: any; no lock, no allocation. The caller holds whatever guards the
 * ring and the counters while it runs.
 */

#ifndef XHCI_QRYB_H
#define XHCI_QRYB_H

#include "xhci_compat.h"
#include "xhci_qry.h"

typedef struct _XHCI_QRY_IN {
    ULONG Verbosity;        /* applied (0..4)                              */
    ULONG VerbosityRead;
    ULONG Refused;          /* the value read was refused                  */
    ULONG Emitted;          /* XhciLogDebugView drains the ring            */
    ULONG Started;
    ULONG Terminal;         /* XhciTolTerminal's reason (XHCI_TOL_TERMINAL_*) */
    ULONG Flavour;          /* XHCI_SNAPSHOT_FLAVOUR_*                     */
    const UCHAR *Ring;      /* the ring as XHCI_LOG holds it: RingBytes,   */
    ULONG RingBytes;        /* a power of two at most XHQ_LOG_MAX; any     */
    ULONG Head;             /* other shape is an empty log                 */
    ULONG Used;
    ULONG Dropped;
    ULONG Appends;
    const ULONG *Counters;  /* the block, CounterWords words; the one word */
    ULONG CounterWords;     /* the door computes already put in            */
    const UCHAR *Pci;       /* XHQ_PCI_BYTES, or NULL                      */
    ULONG PciState;         /* XHQ_PCI_*                                   */
} XHCI_QRY_IN, *PXHCI_QRY_IN;

#define XHQ_ANS_WHOLE           0UL
#define XHQ_ANS_HEADER          1UL     /* room < the whole: header alone  */
#define XHQ_ANS_TOO_SMALL       2UL     /* room < XHQ_HDR_BYTES            */

/*
 * The answer into out (room bytes). What it serves follows the level, as
 * xhcicm's log answer does: the counters from level 1, the ring from level
 * 2 to 3 (at level 4 it can hold kernel addresses and is withheld,
 * XHQ_F_WITHHELD), the PCI configuration at every level. The ring is
 * written as text, oldest byte first, a byte outside 0x20..0x7E, CR and LF
 * as '.'; XHQ_F_FAILED at every level from Terminal; when the oldest
 * bytes may be a record's tail (Dropped != 0, or
 * Emitted) it starts after the first LF (XHQ_F_LOG_CUT). Returns
 * XHQ_ANS_*; *info the bytes written.
 */
ULONG XhciQryAnswer(UCHAR *out, ULONG room, const XHCI_QRY_IN *in,
                    PULONG info);

#endif /* XHCI_QRYB_H */
