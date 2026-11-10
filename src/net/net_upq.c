/*
 * net_upq.c - the deferred upcalls' queues (net_upq.h; design record 35
 * section 7.8, task 48.38). Pure and DDK-free.
 */

#include "net_upq.h"

#define NET_UPQ_ALIGN(n)    (((n) + 3UL) & ~3UL)

/* An armed request: 1 when the caller must set a timer. */
static ULONG netUpqArm(PNET_UPQ q)
{
    if (q->Armed || q->Stopped) {
        return 0;
    }
    q->Armed = 1;
    return 1;
}

static ULONG netUpqGet32(const UCHAR *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) |
           ((ULONG)p[3] << 24);
}

static VOID netUpqPut32(PUCHAR p, ULONG v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
    p[2] = (UCHAR)(v >> 16);
    p[3] = (UCHAR)(v >> 24);
}

VOID NetUpqInit(PNET_UPQ q, PUCHAR ring, ULONG size)
{
    q->Ring = ring;
    q->Size = size & ~3UL;
    q->Head = 0;
    q->Tail = 0;
    q->Used = 0;
    q->Frames = 0;
    q->Refused = 0;
    q->DoneHead = NULL;
    q->DoneTail = NULL;
    q->ResetDone = 0;
    q->Armed = 0;
    q->Running = 0;
    q->Stopped = 0;
    q->Runs = 0;
}

ULONG NetUpqRx(PNET_UPQ q, const UCHAR *frame, ULONG len, ULONG tag)
{
    ULONG need;
    ULONG pad;
    ULONG i;
    PUCHAR dst;

    if (len == 0 || len > q->Size) {
        q->Refused++;
        return 2;
    }
    need = 8 + NET_UPQ_ALIGN(len);
    pad = 0;
    if (q->Tail + need > q->Size) {
        /* Not before the end: the rest of the end marked, the frame at
         * the start. */
        pad = q->Size - q->Tail;
    }
    if (need > q->Size || q->Used + pad + need > q->Size) {
        q->Refused++;
        return 2;
    }
    if (pad != 0) {
        netUpqPut32(q->Ring + q->Tail, NET_UPQ_WRAP);
        q->Used += pad;
        q->Tail = 0;
    }
    netUpqPut32(q->Ring + q->Tail, len);
    netUpqPut32(q->Ring + q->Tail + 4, tag);
    dst = q->Ring + q->Tail + 8;
    for (i = 0; i < len; i++) {
        dst[i] = frame[i];
    }
    q->Tail += need;
    if (q->Tail == q->Size) {
        q->Tail = 0;
    }
    q->Used += need;
    q->Frames++;
    return netUpqArm(q);
}

ULONG NetUpqDone(PNET_UPQ q, PVOID item, NET_UPQ_LINK link)
{
    *link(item) = NULL;
    if (q->DoneTail == NULL) {
        q->DoneHead = item;
    } else {
        *link(q->DoneTail) = item;
    }
    q->DoneTail = item;
    return netUpqArm(q);
}

ULONG NetUpqReset(PNET_UPQ q)
{
    q->ResetDone = 1;
    return netUpqArm(q);
}

VOID NetUpqEnter(PNET_UPQ q)
{
    q->Running = 1;
    q->Runs++;
}

/* The oldest frame, past an end marker; NULL when none. */
static const UCHAR *netUpqPeek(PNET_UPQ q, PULONG len, PULONG tag)
{
    ULONG v;

    if (q->Frames == 0) {
        *len = 0;
        *tag = 0;
        return NULL;
    }
    v = netUpqGet32(q->Ring + q->Head);
    if (v == NET_UPQ_WRAP) {
        q->Used -= q->Size - q->Head;
        q->Head = 0;
        v = netUpqGet32(q->Ring);
    }
    *len = v;
    *tag = netUpqGet32(q->Ring + q->Head + 4);
    return q->Ring + q->Head + 8;
}

ULONG NetUpqTake(PNET_UPQ q, PVOID *done, PULONG reset,
                 const UCHAR **frame, PULONG len, PULONG tag)
{
    *done = q->DoneHead;
    q->DoneHead = NULL;
    q->DoneTail = NULL;
    *reset = q->ResetDone;
    q->ResetDone = 0;
    *frame = netUpqPeek(q, len, tag);
    if (*done == NULL && *reset == 0 && *frame == NULL) {
        q->Armed = 0;
        q->Running = 0;
        return 0;
    }
    return 1;
}

VOID NetUpqRxPop(PNET_UPQ q)
{
    ULONG len;
    ULONG tag;
    ULONG need;

    if (netUpqPeek(q, &len, &tag) == NULL) {
        return;
    }
    need = 8 + NET_UPQ_ALIGN(len);
    q->Head += need;
    if (q->Head == q->Size) {
        q->Head = 0;
    }
    q->Used -= need;
    q->Frames--;
    if (q->Frames == 0) {
        /* Empty: the whole ring free again, from its start. */
        q->Head = 0;
        q->Tail = 0;
        q->Used = 0;
    }
}

VOID NetUpqStop(PNET_UPQ q)
{
    q->Stopped = 1;
}

VOID NetUpqCancelled(PNET_UPQ q)
{
    q->Armed = 0;
}

ULONG NetUpqResume(PNET_UPQ q)
{
    q->Stopped = 0;
    if (NetUpqPending(q)) {
        return netUpqArm(q);
    }
    return 0;
}

ULONG NetUpqQuiesce(PNET_UPQ q)
{
    if (NetUpqBusy(q) || NetUpqPending(q)) {
        return 0;
    }
    q->Stopped = 1;
    return 1;
}

ULONG NetUpqBusy(const NET_UPQ *q)
{
    return (q->Armed || q->Running) ? 1UL : 0UL;
}

ULONG NetUpqPending(const NET_UPQ *q)
{
    return (q->DoneHead != NULL || q->ResetDone || q->Frames != 0)
               ? 1UL : 0UL;
}
