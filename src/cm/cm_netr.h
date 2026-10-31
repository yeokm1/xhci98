/*
 * cm_netr.h - the Thunderbolt networking service's data rings (design record
 * 35 sections 5.3 and 6.3; roadmap-usb4.md task 45.59, step S5), their pure
 * half: the descriptors of one transmit and one receive ring in Frame Mode
 * and the transmit reservation's bookkeeping, kept apart from the driver
 * half (cm_net.c: the network PDO, its interface, the spin lock, the DPC and
 * the poll timer) so the host suite (test\test_cm_net.c) reaches them.
 *
 * THE RINGS. Each descriptor's buffer is one 4096-byte page below 4 GB
 * whose bus address the driver half gives once (CmNetrInit); a ring keeps
 * one slot empty (Spec Tables 12-19 and 12-24).
 *
 *   Transmit: Head is the next descriptor to post (the Producer Index the
 *   host writes), Tail the oldest posted and not yet reaped. A posted
 *   descriptor carries SOF PDF 1, EOF PDF 2, RS 1, IE 0 and its length
 *   (4096 written as 0); it is reaped once the host interface sets DD.
 *
 *   Receive: Head is the next descriptor the host interface completes; the
 *   Consumer Index the host writes is the slot before it, so every other
 *   slot is the host interface's. A completed descriptor is taken by its
 *   DD, its EOF PDF and its length, never by its SOF PDF (the emulator
 *   writes back SOF PDF 0 for a frame of one tunnelled packet, record 35
 *   section 5.3); CRC Error and Buffer Overflow are handed on as flags.
 *
 * THE RESERVATION (record 35 section 6.3). One at a time: Count consecutive
 * transmit descriptors from Head, all or nothing, named by a token of the
 * service's generation and the first index. Its commit posts them only
 * while that generation is current; a token of an earlier generation still
 * releases the reservation it names and posts nothing.
 *
 * C89, DDK-free, no allocation, no lock (the driver half holds its spin
 * lock around every call). IRQL: any.
 */

#ifndef CM_NETR_H
#define CM_NETR_H

#include "../xhci_compat.h"
#include "tbt_regs.h"

#define CM_NETR_BUFFER_BYTES    4096UL
#define CM_NETR_MIN_FRAMES      32UL
#define CM_NETR_MAX_FRAMES      1024UL
#define CM_NETR_MAX_RESERVE     17UL

typedef struct _CM_NETR {
    ULONG Size;                 /* descriptors in the ring                 */
    volatile UCHAR *Desc;       /* Size x TBT_DESC_BYTES                   */
    ULONG Head;
    ULONG Tail;                 /* transmit only                           */
} CM_NETR;

typedef struct _CM_NETR_RES {
    BOOLEAN Held;
    ULONG Generation;
    ULONG First;
    ULONG Count;
} CM_NETR_RES;

/* What CmNetrRxTake found at a slot. */
#define CM_NETR_RX_NONE         0UL     /* not completed (DD clear)          */
#define CM_NETR_RX_FRAME        1UL     /* a frame: its length and flags     */
#define CM_NETR_RX_BAD_PDF      2UL     /* completed with an EOF PDF not 2:
                                           dropped and counted by the caller */

/* Receive flags (xhci_cmnet.h's XHCI_CM_NET_RX_*). */
#define CM_NETR_FLAG_CRC        0x00000001UL
#define CM_NETR_FLAG_OVERFLOW   0x00000002UL

/* What CmNetrRelease decided. */
#define CM_NETR_REL_NONE        0UL     /* the token names no reservation    */
#define CM_NETR_REL_POST        1UL     /* the current one: post it          */
#define CM_NETR_REL_STALE       2UL     /* an earlier generation's: released,
                                           nothing posted                    */

/* The ring over `desc` (Size x 16 bytes), each descriptor's address
 * bufPa[i]; every DW2 cleared, Head and Tail 0. */
VOID CmNetrInit(CM_NETR *r, volatile UCHAR *desc, ULONG size, const ULONG *bufPa);

/* Receive: every descriptor posted (RS 1, IE as `ie`, DD 0); the Consumer
 * Index the host writes for the current Head. */
VOID CmNetrRxPostAll(CM_NETR *r, BOOLEAN ie);
ULONG CmNetrRxConsumer(const CM_NETR *r);
/* Slot `i`: CM_NETR_RX_*, with the frame's length (1 to 4096) and flags. */
ULONG CmNetrRxTake(const CM_NETR *r, ULONG i, PULONG length, PULONG flags);
VOID CmNetrRxRepost(CM_NETR *r, ULONG i, BOOLEAN ie);

/* Transmit: descriptors posted and not reaped, and free to post (one slot
 * always empty). */
ULONG CmNetrTxUsed(const CM_NETR *r);
ULONG CmNetrTxFree(const CM_NETR *r);
/* Slot `i` posted with `length` (1 to 4096). */
VOID CmNetrTxPost(CM_NETR *r, ULONG i, ULONG length);
/* Every descriptor from Tail with DD set, Tail advanced past each: the
 * count reaped. */
ULONG CmNetrTxReap(CM_NETR *r);

/* A reservation of `count` (1 to CM_NETR_MAX_RESERVE) from Head for
 * generation `gen`: `count`, its first index in *first, or 0 with nothing
 * reserved (one already held, too few free, `count` out of range). */
ULONG CmNetrReserve(const CM_NETR *tx, CM_NETR_RES *res, ULONG gen, ULONG count,
                    PULONG first);
/* The token's commit or abort: CM_NETR_REL_*. Any answer but NONE leaves
 * no reservation held. */
ULONG CmNetrRelease(CM_NETR_RES *res, ULONG gen, ULONG tokGen, ULONG tokFirst,
                    ULONG tokCount);

#endif /* CM_NETR_H */
