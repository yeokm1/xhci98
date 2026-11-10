/*
 * net_upq.h - the deferred upcalls' queues of xhcinet.sys on 98 SE and ME
 * (design record 35 section 7.8, task 48.38): the received Ethernet
 * frames, copied into a byte ring as they arrive, the completed sends, the
 * reset's completion, and when an NDIS timer must be set for the routine
 * that makes the upcalls. Pure and DDK-free, host-tested by
 * test\test_net_upq.c; a completed send is the caller's (an NDIS_PACKET
 * there), linked through a slot the caller names, and the ring's memory
 * is the caller's. No lock: the caller holds one lock over a NET_UPQ for
 * every call, and reads a frame between NetUpqTake and NetUpqRxPop with
 * the lock released (the writer never touches a frame not yet popped).
 *
 * The rules it keeps: a timer is asked for only when none is set, the
 * routine is not running and the halt has not stopped them; the routine
 * keeps that state until a take finds every queue empty, so nothing
 * queued meanwhile waits for another timer and no timer is set again
 * from inside its own routine; sends complete, and frames are read, in
 * the order queued, and a take returns the reset's completion with the
 * completions queued before it, after them.
 *
 * The ring: each frame a ULONG length, a ULONG tag the caller gives (the
 * miniport's power generation) and its bytes, padded to 4; a length of
 * NET_UPQ_WRAP marks the end of the used space, the next frame at the
 * ring's start. A frame that does not fit is refused, counted.
 *
 * IRQL: any; the caller serialises each NET_UPQ.
 */

#ifndef NET_UPQ_H
#define NET_UPQ_H

#include "../xhci_compat.h"

#define NET_UPQ_WRAP        0xFFFFFFFFUL

/* The slot of an item holding the next item's pointer. */
typedef PVOID *(*NET_UPQ_LINK)(PVOID item);

typedef struct _NET_UPQ {
    PUCHAR Ring;
    ULONG Size;         /* bytes, a multiple of 4                        */
    ULONG Head;         /* the oldest frame's offset                     */
    ULONG Tail;         /* where the next frame goes                     */
    ULONG Used;         /* bytes held, the end's padding included        */
    ULONG Frames;       /* frames held                                   */
    ULONG Refused;      /* frames that did not fit                       */
    PVOID DoneHead;
    PVOID DoneTail;
    ULONG ResetDone;
    ULONG Armed;        /* a timer set and its routine not yet done      */
    ULONG Running;      /* the routine runs                              */
    ULONG Stopped;      /* the halt: no timer set again                  */
    ULONG Runs;         /* the routine's runs                            */
} NET_UPQ, *PNET_UPQ;

/* `ring` of `size` bytes (rounded down to a multiple of 4). */
VOID NetUpqInit(PNET_UPQ q, PUCHAR ring, ULONG size);

/* Each returns 1 when the caller must set a timer now, else 0; NetUpqRx
 * returns 2 when the frame did not fit (not queued, counted). */
ULONG NetUpqRx(PNET_UPQ q, const UCHAR *frame, ULONG len, ULONG tag);
ULONG NetUpqDone(PNET_UPQ q, PVOID item, NET_UPQ_LINK link);
ULONG NetUpqReset(PNET_UPQ q);

/* The routine's entry. */
VOID NetUpqEnter(PNET_UPQ q);

/* The routine's take: every queued completion (a list in order, through
 * the link, *done NULL when none), *reset 1 when the reset's completion
 * is due after those completions, and the oldest frame (*frame, *len,
 * *tag; *frame NULL when none), which stays held until NetUpqRxPop. Returns 0
 * when there was nothing: the routine is then done, and the state says so
 * (no timer set, not running). */
ULONG NetUpqTake(PNET_UPQ q, PVOID *done, PULONG reset,
                 const UCHAR **frame, PULONG len, PULONG tag);

/* The frame NetUpqTake returned, read and given back. */
VOID NetUpqRxPop(PNET_UPQ q);

/* The halt: no timer is asked for again (NetUpqStop); NetUpqCancelled
 * when the caller's cancel then removed a set timer before its routine
 * started. */
VOID NetUpqStop(PNET_UPQ q);
VOID NetUpqCancelled(PNET_UPQ q);

/* After a stop (a D3), delivery again (the D0, or a halt that must have
 * the routine deliver what was queued meanwhile): 1 when the caller must
 * set a timer for what is queued. */
ULONG NetUpqResume(PNET_UPQ q);

/* Quiescence, for a D3 or a halt: 1 when nothing is queued, no timer is
 * set and the routine is not running; it then also stops the queues
 * (NetUpqStop) in the same call, so nothing can ask for a timer after
 * the answer. 0 otherwise, the queues untouched. */
ULONG NetUpqQuiesce(PNET_UPQ q);

/* 1 while a timer is set or the routine runs. */
ULONG NetUpqBusy(const NET_UPQ *q);

/* 1 while anything is queued. */
ULONG NetUpqPending(const NET_UPQ *q);

#endif /* NET_UPQ_H */
