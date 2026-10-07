/*
 * xhci_snap.c - the layout of the snapshot's HCD region (roadmap-hcd.md
 * task 35.3; src/xhci.h, XHCI_SNAPSHOT_REGION_HCD).
 *
 * 35.0's E460 reading needed a hand-built offset table and a static
 * inference to say why a trained SuperSpeed link never got a slot: the
 * driver's per-port enumeration state and its counter block live in the
 * HCD's controller object, outside the extension the snapshot copies. The
 * HCD region publishes both as a pointer-free image of ULONGs - a header,
 * one record per root port, the counters - which the door (hcd_door.c)
 * fills word by word into the caller's window, so no image of the whole is
 * ever built in kernel memory. What lives here is where each word of that
 * image lies and what the header says, so the host suite checks the layout
 * with no controller (test\test_snap.c). The header's words from
 * XHCI_SNAPSHOT_HCD_TOL_AT (35-T.8) place the tolerance state inside the
 * extension image rather than copy it: the state lives in the extension.
 * The diagnostic build 2.2.0.2 (roadmap-hcd.md task 36.6) appends four
 * parts after the counters - a diagnostic block, the pipe records, the
 * recovery timeline and the backstop samples - and the helpers that keep
 * the last two as rings and pick each pipe's oldest transfer.
 *
 * DDK-free: part of the pure core. C89, pure: IRQL any.
 */

#include "xhci.h"
#include "xhci_counters.h"
#include "xhci_enum.h"

/* The image carries the counter block whole, word for word. */
XHCI_C_ASSERT(xhci_snap_hcd_counters,
              sizeof(XHCIHC_COUNTERS) ==
                  XHCI_SNAPSHOT_HCD_COUNTER_WORDS * sizeof(ULONG));

ULONG XhciSnapHcdWords(ULONG ports)
{
    if (ports > XHCI_MAX_ROOT_PORTS) {
        ports = XHCI_MAX_ROOT_PORTS;
    }
    return XHCI_SNAPSHOT_HCD_HEAD_WORDS +
           ports * XHCI_SNAPSHOT_HCD_PORT_WORDS +
           XHCI_SNAPSHOT_HCD_COUNTER_WORDS + XHCI_SNAPSHOT_DIAG_WORDS +
           XHCI_SNAPSHOT_HCD_PIPES_KEPT * XHCI_SNAPSHOT_PIPE_WORDS +
           XHCI_SNAPSHOT_HCD_RECS_KEPT * XHCI_SNAPSHOT_REC_WORDS +
           XHCI_SNAPSHOT_HCD_IRQS_KEPT * XHCI_SNAPSHOT_IRQ_WORDS;
}

/* The first word of the diagnostic block, past the counters. */
static ULONG xhciSnapDiagAt(ULONG ports)
{
    return XHCI_SNAPSHOT_HCD_HEAD_WORDS +
           ports * XHCI_SNAPSHOT_HCD_PORT_WORDS +
           XHCI_SNAPSHOT_HCD_COUNTER_WORDS;
}

ULONG XhciSnapHcdHead(ULONG ports, ULONG word)
{
    if (ports > XHCI_MAX_ROOT_PORTS) {
        ports = XHCI_MAX_ROOT_PORTS;
    }
    switch (word) {
    case XHCI_SNAPSHOT_HCD_VERSION_AT:
        return XHCI_SNAPSHOT_HCD_VERSION;
    case XHCI_SNAPSHOT_HCD_HEAD_BYTES:
        return XHCI_SNAPSHOT_HCD_HEAD_WORDS * 4UL;
    case XHCI_SNAPSHOT_HCD_PORTS:
        return ports;
    case XHCI_SNAPSHOT_HCD_PORT_BYTES:
        return XHCI_SNAPSHOT_HCD_PORT_WORDS * 4UL;
    case XHCI_SNAPSHOT_HCD_PORTS_AT:
        return XHCI_SNAPSHOT_HCD_HEAD_WORDS * 4UL;
    case XHCI_SNAPSHOT_HCD_COUNTERS:
        return XHCI_SNAPSHOT_HCD_COUNTER_WORDS;
    case XHCI_SNAPSHOT_HCD_COUNTERS_AT:
        return (XHCI_SNAPSHOT_HCD_HEAD_WORDS +
                ports * XHCI_SNAPSHOT_HCD_PORT_WORDS) * 4UL;
    case XHCI_SNAPSHOT_HCD_NOTE_BUDGET:
        return XHCI_ENUM_NOTE_BUDGET;
    case XHCI_SNAPSHOT_HCD_TOL_AT:
        return XHCI_FIELD_OFFSET(XHCI_EXTENSION, Tol);
    case XHCI_SNAPSHOT_HCD_TOL_BYTES:
        return (ULONG)sizeof(XHCI_TOL_STATE);
    case XHCI_SNAPSHOT_HCD_TOL_STATS:
        return (ULONG)(sizeof(XHCI_TOL_STATS) / sizeof(ULONG));
    case XHCI_SNAPSHOT_HCD_TOL_WINDOW_AT:
        return XHCI_FIELD_OFFSET(XHCI_TOL_STATE, Window);
    case XHCI_SNAPSHOT_HCD_TOL_CLOCK_AT:
        return XHCI_FIELD_OFFSET(XHCI_TOL_STATE, Clock);
    case XHCI_SNAPSHOT_HCD_TOL_LOC_BYTES:
        return (ULONG)sizeof(XHCI_TOL_LOC);
    case XHCI_SNAPSHOT_HCD_TOL_ROOT_AT:
        return XHCI_FIELD_OFFSET(XHCI_TOL_STATE, RootLoc);
    case XHCI_SNAPSHOT_HCD_TOL_ROOT_LOCS:
        return XHCI_TOL_ROOT_PORTS;
    case XHCI_SNAPSHOT_HCD_TOL_HUB_AT:
        return XHCI_FIELD_OFFSET(XHCI_TOL_STATE, HubLoc);
    case XHCI_SNAPSHOT_HCD_TOL_HUB_LOCS:
        return XHCI_TOL_HUB_LOCS;
    case XHCI_SNAPSHOT_HCD_TOL_HUB_PORTS:
        return XHCI_TOL_HUB_LOC_PORTS;
    case XHCI_SNAPSHOT_HCD_DIAG_AT:
        return xhciSnapDiagAt(ports) * 4UL;
    case XHCI_SNAPSHOT_HCD_DIAG_BYTES:
        return XHCI_SNAPSHOT_DIAG_WORDS * 4UL;
    case XHCI_SNAPSHOT_HCD_PIPES:
        return XHCI_SNAPSHOT_HCD_PIPES_KEPT;
    case XHCI_SNAPSHOT_HCD_PIPE_BYTES:
        return XHCI_SNAPSHOT_PIPE_WORDS * 4UL;
    case XHCI_SNAPSHOT_HCD_PIPES_AT:
        return (xhciSnapDiagAt(ports) + XHCI_SNAPSHOT_DIAG_WORDS) * 4UL;
    case XHCI_SNAPSHOT_HCD_RECS:
        return XHCI_SNAPSHOT_HCD_RECS_KEPT;
    case XHCI_SNAPSHOT_HCD_REC_BYTES:
        return XHCI_SNAPSHOT_REC_WORDS * 4UL;
    case XHCI_SNAPSHOT_HCD_RECS_AT:
        return (xhciSnapDiagAt(ports) + XHCI_SNAPSHOT_DIAG_WORDS +
                XHCI_SNAPSHOT_HCD_PIPES_KEPT * XHCI_SNAPSHOT_PIPE_WORDS) *
               4UL;
    case XHCI_SNAPSHOT_HCD_IRQS:
        return XHCI_SNAPSHOT_HCD_IRQS_KEPT;
    case XHCI_SNAPSHOT_HCD_IRQ_BYTES:
        return XHCI_SNAPSHOT_IRQ_WORDS * 4UL;
    case XHCI_SNAPSHOT_HCD_IRQS_AT:
        return (xhciSnapDiagAt(ports) + XHCI_SNAPSHOT_DIAG_WORDS +
                XHCI_SNAPSHOT_HCD_PIPES_KEPT * XHCI_SNAPSHOT_PIPE_WORDS +
                XHCI_SNAPSHOT_HCD_RECS_KEPT * XHCI_SNAPSHOT_REC_WORDS) *
               4UL;
    default:
        /* XHCI_SNAPSHOT_HCD_TERMINAL is a reading, not layout: the door
         * fills it (hcd_door.c). */
        return 0;
    }
}

ULONG XhciSnapHcdLocate(ULONG ports, ULONG index, PULONG record,
                        PULONG word)
{
    ULONG portWords;

    if (ports > XHCI_MAX_ROOT_PORTS) {
        ports = XHCI_MAX_ROOT_PORTS;
    }
    *record = 0;
    *word = 0;
    portWords = ports * XHCI_SNAPSHOT_HCD_PORT_WORDS;
    if (index < XHCI_SNAPSHOT_HCD_HEAD_WORDS) {
        *word = index;
        return XHCI_SNAPSHOT_HCD_IN_HEAD;
    }
    index -= XHCI_SNAPSHOT_HCD_HEAD_WORDS;
    if (index < portWords) {
        *record = index / XHCI_SNAPSHOT_HCD_PORT_WORDS;
        *word = index % XHCI_SNAPSHOT_HCD_PORT_WORDS;
        return XHCI_SNAPSHOT_HCD_IN_PORT;
    }
    index -= portWords;
    if (index < XHCI_SNAPSHOT_HCD_COUNTER_WORDS) {
        *word = index;
        return XHCI_SNAPSHOT_HCD_IN_COUNTERS;
    }
    index -= XHCI_SNAPSHOT_HCD_COUNTER_WORDS;
    if (index < XHCI_SNAPSHOT_DIAG_WORDS) {
        *word = index;
        return XHCI_SNAPSHOT_HCD_IN_DIAG;
    }
    index -= XHCI_SNAPSHOT_DIAG_WORDS;
    if (index < XHCI_SNAPSHOT_HCD_PIPES_KEPT * XHCI_SNAPSHOT_PIPE_WORDS) {
        *record = index / XHCI_SNAPSHOT_PIPE_WORDS;
        *word = index % XHCI_SNAPSHOT_PIPE_WORDS;
        return XHCI_SNAPSHOT_HCD_IN_PIPE;
    }
    index -= XHCI_SNAPSHOT_HCD_PIPES_KEPT * XHCI_SNAPSHOT_PIPE_WORDS;
    if (index < XHCI_SNAPSHOT_HCD_RECS_KEPT * XHCI_SNAPSHOT_REC_WORDS) {
        *record = index / XHCI_SNAPSHOT_REC_WORDS;
        *word = index % XHCI_SNAPSHOT_REC_WORDS;
        return XHCI_SNAPSHOT_HCD_IN_REC;
    }
    index -= XHCI_SNAPSHOT_HCD_RECS_KEPT * XHCI_SNAPSHOT_REC_WORDS;
    if (index < XHCI_SNAPSHOT_HCD_IRQS_KEPT * XHCI_SNAPSHOT_IRQ_WORDS) {
        *record = index / XHCI_SNAPSHOT_IRQ_WORDS;
        *word = index % XHCI_SNAPSHOT_IRQ_WORDS;
        return XHCI_SNAPSHOT_HCD_IN_IRQ;
    }
    return XHCI_SNAPSHOT_HCD_PAST_END;
}

ULONG XhciSnapHcdFlags(ULONG settleDeferred, ULONG linkRecovering,
                       ULONG unreadable)
{
    return (settleDeferred ? XHCI_SNAPSHOT_HCD_F_DEFERRED : 0UL) |
           (linkRecovering ? XHCI_SNAPSHOT_HCD_F_RECOVERING : 0UL) |
           (unreadable ? XHCI_SNAPSHOT_HCD_F_UNREADABLE : 0UL);
}

/*
 * The quotient of (high:low) by 10000 taken a half-word at a time, each
 * partial dividend under 2^30: only its low 32 bits are kept, so the
 * high word's own quotient, a multiple of 2^32, drops out.
 */
ULONG XhciSnapMs(ULONG high, ULONG low)
{
    ULONG rest;
    ULONG upper;
    ULONG lower;

    rest = high % 10000UL;
    upper = ((rest << 16) | (low >> 16)) / 10000UL;
    rest = ((rest << 16) | (low >> 16)) % 10000UL;
    lower = ((rest << 16) | (low & 0xFFFFUL)) / 10000UL;
    return (upper << 16) + lower;
}

ULONG XhciSnapOldest(const ULONG *states, const ULONG *seqs, ULONG count,
                     ULONG freeState)
{
    ULONG best;
    ULONG i;

    best = count;
    for (i = 0; i < count; i++) {
        if (states[i] == freeState) {
            continue;
        }
        if (best == count || (LONG)(seqs[i] - seqs[best]) < 0) {
            best = i;
        }
    }
    return best;
}

PULONG XhciSnapRingNext(PULONG ring, ULONG kept, ULONG words, PULONG total)
{
    PULONG entry;
    ULONG i;

    (*total)++;
    if (*total == 0) {
        (*total)++;
    }
    entry = ring + ((*total - 1UL) % kept) * words;
    for (i = 0; i < words; i++) {
        entry[i] = 0;
    }
    entry[0] = *total;
    return entry;
}

PULONG XhciSnapRingFind(PULONG ring, ULONG kept, ULONG words, ULONG seq)
{
    PULONG entry;

    if (seq == 0) {
        return NULL;
    }
    entry = ring + ((seq - 1UL) % kept) * words;
    return (entry[0] == seq) ? entry : NULL;
}

VOID XhciSnapRecCommand(PULONG rec, ULONG trbType, ULONG code)
{
    ULONG n;

    n = rec[XHCI_SNAPSHOT_REC_COMMANDS];
    if (n < XHCI_SNAPSHOT_REC_CMD_KEPT) {
        rec[XHCI_SNAPSHOT_REC_CMD01 + n / 2UL] |=
            (((trbType & 0x3FUL) << 8) | (code & 0xFFUL)) << ((n % 2UL) * 16);
    }
    if (n != 0xFFFFFFFFUL) {
        rec[XHCI_SNAPSHOT_REC_COMMANDS] = n + 1UL;
    }
}

ULONG XhciSnapIrqClass(ULONG admitted, ULONG pending, ULONG iman)
{
    if (!admitted) {
        return XHCI_SNAPSHOT_BS_SKIPPED;
    }
    if (!pending) {
        return XHCI_SNAPSHOT_BS_IDLE;
    }
    return (iman & XHCI_IMAN_IP) != 0 ? XHCI_SNAPSHOT_BS_IP
                                      : XHCI_SNAPSHOT_BS_NO_IP;
}

ULONG XhciSnapPipeClass(ULONG outstanding, ULONG flagged)
{
    if (outstanding) {
        return XHCI_SNAPSHOT_PC_OUTSTANDING;
    }
    return flagged ? XHCI_SNAPSHOT_PC_FLAGGED : XHCI_SNAPSHOT_PC_IDLE;
}

ULONG XhciSnapPipeBefore(ULONG classA, ULONG tickA, ULONG idA, ULONG classB,
                         ULONG tickB, ULONG idB)
{
    if (classA != classB) {
        return classA < classB;
    }
    if (classA == XHCI_SNAPSHOT_PC_OUTSTANDING && tickA != tickB) {
        return (LONG)(tickA - tickB) < 0;
    }
    return idA < idB;
}

ULONG XhciSnapTick(ULONG now)
{
    return (now != 0xFFFFFFFFUL) ? now + 1UL : now;
}

ULONG XhciSnapHoldOf(ULONG how)
{
    switch (how) {
    case 1:
        return XHCI_SNAPSHOT_RD_HELD;
    case 2:
        return XHCI_SNAPSHOT_RD_COVERED;
    default:
        return XHCI_SNAPSHOT_RD_REFUSED;
    }
}

ULONG XhciSnapClearOf(ULONG outcome)
{
    static const ULONG clear[5] = {
        XHCI_SNAPSHOT_CLEAR_DONE, XHCI_SNAPSHOT_CLEAR_STALLED,
        XHCI_SNAPSHOT_CLEAR_FAILED, XHCI_SNAPSHOT_CLEAR_NOT_SENT,
        XHCI_SNAPSHOT_CLEAR_ABANDONED
    };

    return (outcome < 5) ? clear[outcome] : XHCI_SNAPSHOT_CLEAR_NOT_SENT;
}
