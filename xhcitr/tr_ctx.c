/*
 * tr_ctx.c - XHCITR's reading of a UAS drive's stream contexts and the
 * stream trace (task 49.39; design record 17 section 4.12.9; tr_ctx.h).
 * Pure: no Windows call, the regions' bytes in, lines out. C89.
 */

#include <stdio.h>
#include <string.h>
#include "tr_ctx.h"

#define CTX_WORDS           0
#define CTX_SLOT_DCI        1
#define CTX_EP              2
#define CTX_STREAMS         7
#define CTX_FIRST           8
#define CTX_S_WORDS         7UL
#define CTX_NOT_READ        0xFFFFFFFFUL

#define TRACE_HEAD_WORDS    8UL
#define TRACE_H_VERSION     0
#define TRACE_H_ENTRIES     1
#define TRACE_H_WORDS       2
#define TRACE_H_HEAD        3
#define TRACE_H_CAP_SEQ     4
#define TRACE_H_CAP_WHY     5
#define TRACE_H_CAP_AT      6
#define TRACE_H_CAP_HEAD    7

static unsigned long g32(const unsigned char *b, unsigned long off)
{
    return (unsigned long)b[off] | ((unsigned long)b[off + 1] << 8) |
           ((unsigned long)b[off + 2] << 16) |
           ((unsigned long)b[off + 3] << 24);
}

static const char *const ep_state_name[8] = {
    "Disabled", "Running", "Halted", "Stopped", "Error", "5", "6", "7"
};

int tr_ctx_place(unsigned long saved, unsigned long base, unsigned long trbs,
                 unsigned long deq, unsigned long enq)
{
    unsigned long p;
    unsigned long dq;
    unsigned long eq;
    unsigned long sc;
    unsigned long dc;
    unsigned long ec;
    unsigned long a;

    a = saved & ~0x0FUL;
    if (trbs < 2 || a < base || (a - base) / 16 >= trbs ||
        (deq & ~0x0FUL) < base || (enq & ~0x0FUL) < base) {
        return TR_CTX_OFF_RING;
    }
    p = (a - base) / 16;
    dq = ((deq & ~0x0FUL) - base) / 16;
    eq = ((enq & ~0x0FUL) - base) / 16;
    if (dq >= trbs || eq >= trbs) {
        return TR_CTX_OFF_RING;
    }
    sc = saved & 1UL;
    dc = deq & 1UL;
    ec = enq & 1UL;
    if (p == trbs - 1) {
        /* The Link TRB: the position it leads to, the cycle toggled (this
         * driver's rings set Toggle Cycle on their one Link). */
        p = 0;
        sc ^= 1UL;
    }
    if (p == dq && sc == dc) {
        return TR_CTX_AT_DEQ;
    }
    if (dq <= eq) {
        if (p >= dq && p <= eq) {
            return (sc == ec) ? TR_CTX_INSIDE : TR_CTX_WRONG_CYCLE;
        }
        if (p < dq) {
            return (sc == ec) ? TR_CTX_BEHIND : TR_CTX_AHEAD;
        }
        return (sc != ec) ? TR_CTX_BEHIND : TR_CTX_AHEAD;
    }
    if (p >= dq) {
        return (sc == dc) ? TR_CTX_INSIDE : TR_CTX_WRONG_CYCLE;
    }
    if (p <= eq) {
        return (sc == ec) ? TR_CTX_INSIDE : TR_CTX_WRONG_CYCLE;
    }
    return (sc == dc) ? TR_CTX_BEHIND : TR_CTX_AHEAD;
}

static const char *const place_name[6] = {
    "at the driver's dequeue",
    "inside the queued window",
    "BEHIND the driver's dequeue (retired or cancelled TRBs)",
    "AHEAD of the driver's enqueue",
    "in the window with the WRONG cycle",
    "OFF the stream's ring"
};

void tr_ctx_print(const unsigned char *b, unsigned long bytes, TR_CTX_OUT out)
{
    char line[200];
    unsigned long at;
    unsigned long words;
    unsigned long n;
    unsigned long i;
    unsigned long dw0;
    const unsigned char *s;
    unsigned long records;

    out("\nstream contexts (task 49.39, design record 17 section 4.12.9):\n");
    out("  (read while the controller runs: one window can catch it "
        "mid-move)\n");
    records = 0;
    for (at = 0; at + CTX_FIRST * 4 <= bytes; at += words * 4) {
        words = g32(b, at + CTX_WORDS * 4);
        n = g32(b, at + CTX_STREAMS * 4);
        if (words < CTX_FIRST || words > 1024 || n > 64 ||
            words != CTX_FIRST + n * CTX_S_WORDS ||
            words * 4 > bytes - at) {
            out("  a record of a shape this build does not read: the rest "
                "is not decoded\n");
            return;
        }
        records++;
        dw0 = g32(b, at + CTX_EP * 4);
        if (dw0 == CTX_NOT_READ) {
            sprintf(line, "  slot %lu DCI %lu: the endpoint context not read "
                          "(the controller not started)\n",
                    g32(b, at + CTX_SLOT_DCI * 4) >> 8,
                    g32(b, at + CTX_SLOT_DCI * 4) & 0xFFUL);
        } else {
            sprintf(line, "  slot %lu DCI %lu: endpoint %s, MaxPStreams %lu, "
                          "array %08lX, %lu streams\n",
                    g32(b, at + CTX_SLOT_DCI * 4) >> 8,
                    g32(b, at + CTX_SLOT_DCI * 4) & 0xFFUL,
                    ep_state_name[dw0 & 7UL], (dw0 >> 10) & 0x1FUL,
                    g32(b, at + (CTX_EP + 2) * 4) & ~0x0FUL, n);
        }
        out(line);
        for (i = 0; i < n; i++) {
            unsigned long c0;
            unsigned long base;
            unsigned long trbs;
            unsigned long deq;
            unsigned long enq;
            unsigned long q;

            s = b + at + (CTX_FIRST + i * CTX_S_WORDS) * 4;
            c0 = g32(s, 0);
            base = g32(s, 8);
            trbs = g32(s, 12);
            deq = g32(s, 16);
            enq = g32(s, 20);
            q = g32(s, 24);
            if (c0 == CTX_NOT_READ) {
                sprintf(line, "    stream %lu: not read\n", i + 1);
            } else {
                sprintf(line, "    stream %lu: saved %08lX DCS %lu SCT %lu - "
                              "%s; driver dequeue %08lX/%lu, enqueue "
                              "%08lX/%lu, %lu TD%s queued\n",
                        i + 1, c0 & ~0x0FUL, c0 & 1UL, (c0 >> 1) & 7UL,
                        place_name[tr_ctx_place(c0, base, trbs, deq, enq)],
                        deq & ~0x0FUL, deq & 1UL, enq & ~0x0FUL, enq & 1UL,
                        q, q == 1 ? "" : "s");
            }
            out(line);
        }
    }
    if (records == 0) {
        out("  no endpoint with streams open\n");
    }
}

static const char *const kind_name[9] = {
    "?", "command", "?", "Set TR Dequeue", "read back", "cancel",
    "doorbell", "event", "placed"
};

static const char *const cap_why[3] = {
    "?", "a stream device's RESET_PORT",
    "a stream's dequeue given up (record 17 4.12.8)"
};

unsigned long tr_trace_ms(unsigned long units)
{
    return (units / 10000UL) * 1024UL + ((units % 10000UL) * 1024UL) / 10000UL;
}

static void trace_ring(const unsigned char *r, unsigned long entries,
                       unsigned long head, TR_CTX_OUT out)
{
    char line[200];
    unsigned long i;
    unsigned long n;
    unsigned long first;
    unsigned long k;
    unsigned long t0;
    const unsigned char *e;

    n = (head < entries) ? head : entries;
    if (n == 0) {
        out("    (empty)\n");
        return;
    }
    first = head - n;
    t0 = g32(r + (first % entries) * 16, 0);
    for (i = 0; i < n; i++) {
        e = r + ((first + i) % entries) * 16;
        k = g32(e, 4) >> 24;
        sprintf(line, "    %+8ld ms %-14s slot %lu DCI %lu stream %lu: "
                      "%08lX %08lX\n",
                (long)tr_trace_ms(g32(e, 0) - t0),
                k < 9 ? kind_name[k] : "?", (g32(e, 4) >> 16) & 0xFFUL,
                (g32(e, 4) >> 8) & 0xFFUL, g32(e, 4) & 0xFFUL, g32(e, 8),
                g32(e, 12));
        out(line);
    }
}

void tr_trace_print(const unsigned char *b, unsigned long bytes,
                    TR_CTX_OUT out)
{
    char line[200];
    unsigned long entries;
    unsigned long words;
    unsigned long ring;
    unsigned long why;

    out("\nstream trace (task 49.39, design record 17 section 4.12.9):\n");
    if (bytes < TRACE_HEAD_WORDS * 4) {
        out("  (too short to read)\n");
        return;
    }
    entries = g32(b, TRACE_H_ENTRIES * 4);
    words = g32(b, TRACE_H_WORDS * 4);
    if (g32(b, TRACE_H_VERSION * 4) != 1 || words != 4 || entries == 0 ||
        entries > 4096) {
        out("  a version or shape this build does not read\n");
        return;
    }
    ring = entries * words * 4;
    if (bytes < TRACE_HEAD_WORDS * 4 + 2 * ring) {
        out("  (shorter than its header says)\n");
        return;
    }
    if (g32(b, TRACE_H_CAP_SEQ * 4) != 0) {
        why = g32(b, TRACE_H_CAP_WHY * 4);
        sprintf(line, "  capture %lu, taken at %s, oldest first "
                      "(times from its first entry):\n",
                g32(b, TRACE_H_CAP_SEQ * 4), why < 3 ? cap_why[why] : "?");
        out(line);
        trace_ring(b + TRACE_HEAD_WORDS * 4 + ring, entries,
                   g32(b, TRACE_H_CAP_HEAD * 4), out);
    } else {
        out("  no capture taken since the controller started\n");
    }
    sprintf(line, "  live, %lu entries since the start, the last %lu:\n",
            g32(b, TRACE_H_HEAD * 4),
            g32(b, TRACE_H_HEAD * 4) < entries ? g32(b, TRACE_H_HEAD * 4)
                                               : entries);
    out(line);
    trace_ring(b + TRACE_HEAD_WORDS * 4, entries, g32(b, TRACE_H_HEAD * 4),
               out);
}
