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
    ULONG DriverVersion;    /* XHCI_VER_PACKED (version 2)                 */
    ULONG XportRecords;     /* the transport part's records, at most
                             * XHQ_XPORT_MAX; left 0 for the caller to
                             * fill (version 2)                            */
    ULONG Ports;            /* the ports part's bytes, at most
                             * XHQ_PORTS_MAX; written XHQ_PT_NONE for the
                             * caller to fill (version 3)                  */
} XHCI_QRY_IN, *PXHCI_QRY_IN;

#define XHQ_ANS_WHOLE           0UL
#define XHQ_ANS_HEADER          1UL     /* room < the whole: header alone  */
#define XHQ_ANS_TOO_SMALL       2UL     /* room < XHQ_HDR_BYTES_V1         */

/*
 * The answer into out (room bytes). What it serves follows the level, as
 * xhcicm's log answer does: the counters from level 1, the ring from level
 * 2 (at level 4 it holds kernel addresses and is served as it is, with
 * XHQ_F_ADDRESSES and XHQ_F_WITHHELD, the owner's decision of
 * 2026-11-13), the PCI configuration at every level. The ring is
 * written as text, oldest byte first, a byte outside 0x20..0x7E, CR and LF
 * as '.'; XHQ_F_FAILED at every level from Terminal; when the oldest
 * bytes may be a record's tail (Dropped != 0, or
 * Emitted) it starts after the first LF (XHQ_F_LOG_CUT). The transport
 * part, XportRecords records at every level, and then the ports part,
 * Ports bytes at every level, are placed last and written zero: the caller
 * fills them under the lock it counted them under. With
 * room for less than the whole, the header alone, cut to its first
 * XHQ_HDR_BYTES_V1 bytes in a room under XHQ_HDR_BYTES. Returns
 * XHQ_ANS_*; *info the bytes written.
 */
ULONG XhciQryAnswer(UCHAR *out, ULONG room, const XHCI_QRY_IN *in,
                    PULONG info);

/*
 * IOCTL_XHCI98_HOLDS's answer (xhci_qry.h XHH_*, task 50.7) into out (room
 * bytes): `root` root locations and `hub` hub locations (each held to its
 * XHH_*_MAX), their holds in `holds`, root ones first; `hubPorts` the hub
 * locations per hub object; `started` the controller is started (with it
 * clear the caller passes 0 locations). Returns XHQ_ANS_WHOLE, XHQ_ANS_HEADER
 * (room < the whole: the header alone, XHH_O_BYTES the size needed) or
 * XHQ_ANS_TOO_SMALL (room < XHH_HDR_BYTES); *info the bytes written.
 */
ULONG XhciHoldsAnswer(UCHAR *out, ULONG room, ULONG started, ULONG root,
                      ULONG hub, ULONG hubPorts, const UCHAR *holds,
                      PULONG info);

/*
 * Task 50.30 step 1: the recovery-status answer (xhci_qry.h, XHR_*).
 * `head` holds the header's words from XHR_O_FLAGS to XHR_O_RECORDS, in
 * order (XHR_HDR_BYTES / 4 - 3 of them); `recs` records of XHR_REC_WORDS
 * words each, newest first, `head`'s last word of them; `counts`
 * XHR_COUNTS words, or NULL; `life` the lifetime part's XHR_LIFE_WORDS
 * words (task 50.32), or NULL for an answer without it. The buffer tiers
 * are xhci_qry.h's: under XHR_HDR_BYTES nothing, under XHR_MAX_BYTES the
 * header, under XHR_LIFE_BYTES (or `life` NULL) the earlier whole answer,
 * else the whole answer with XHR_F_LIFETIME set, which `head`'s flags
 * never carry. Returns XHQ_ANS_* as XhciHoldsAnswer, *info the bytes
 * written. Pure.
 */
ULONG XhciRecoveryAnswer(UCHAR *out, ULONG room, const ULONG *head,
                         const ULONG *recs, const ULONG *counts,
                         const ULONG *life, PULONG info);

#endif /* XHCI_QRYB_H */
