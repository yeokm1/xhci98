/*
 * tr_ctx.h - XHCITR's reading of the stream contexts and the stream trace
 * (task 49.39; design record 17 section 4.12.9). Pure: the caller hands the
 * region's bytes and a line sink. C89.
 */

#ifndef TR_CTX_H
#define TR_CTX_H

typedef void (*TR_CTX_OUT)(const char *line);

/* The contexts region (src\xhci.h, XHCI_SNAPSHOT_REGION_CONTEXTS): each
 * stream endpoint's Output Endpoint Context and, per stream, the saved
 * dequeue beside the driver's ring, with its placement. */
void tr_ctx_print(const unsigned char *b, unsigned long bytes, TR_CTX_OUT out);

/* The trace region (XHCI_SNAPSHOT_REGION_TRACE): the last capture, then the
 * live ring, oldest first. */
void tr_trace_print(const unsigned char *b, unsigned long bytes,
                    TR_CTX_OUT out);

/* Where a saved TR Dequeue Pointer (dword 0, with DCS in bit 0) lies on a
 * one-segment ring of `trbs` TRBs at `base` (the Link TRB last), against
 * the driver's dequeue and enqueue (each address | cycle). TR_CTX_AT_DEQ,
 * TR_CTX_INSIDE (in [dequeue, enqueue] with the cycle that position
 * carries), TR_CTX_BEHIND / TR_CTX_AHEAD (on the ring outside that window:
 * nearer the dequeue's back, or past the enqueue), TR_CTX_WRONG_CYCLE,
 * TR_CTX_OFF_RING. */
#define TR_CTX_AT_DEQ       0
#define TR_CTX_INSIDE       1
#define TR_CTX_BEHIND       2
#define TR_CTX_AHEAD        3
#define TR_CTX_WRONG_CYCLE  4
#define TR_CTX_OFF_RING     5
int tr_ctx_place(unsigned long saved, unsigned long base, unsigned long trbs,
                 unsigned long deq, unsigned long enq);

/* A span of the trace's time units (102.4 us) in milliseconds, with no
 * overflow below the 32-bit span itself. */
unsigned long tr_trace_ms(unsigned long units);

#endif /* TR_CTX_H */
