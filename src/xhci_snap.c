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
 *
 * DDK-free: part of the pure core. C89, pure: IRQL any.
 */

#include "xhci.h"
#include "xhci_counters.h"
#include "xhci_enum.h"
#include "xhci_pirq.h"
#include "xhci_xstat.h"
#include "xhci_xpipe.h"

/* The header carries the routing quirk's record whole, after its count. */
XHCI_C_ASSERT(xhci_snap_hcd_pirq,
              sizeof(XHCI_PIRQ_STATE) ==
                  XHCI_SNAPSHOT_HCD_PIRQ_COUNT * sizeof(ULONG) &&
              XHCI_SNAPSHOT_HCD_PIRQ_AT + XHCI_SNAPSHOT_HCD_PIRQ_COUNT ==
                  XHCI_SNAPSHOT_HCD_XSTAT_WORDS);

/* 36.21's readings: the controller's whole, then the class driver records,
 * up to 36.9's pair. */
XHCI_C_ASSERT(xhci_snap_hcd_xstat,
              XHCI_SNAPSHOT_HCD_XSTAT_COUNT == XHCI_XSTAT_WORDS &&
              XHCI_SNAPSHOT_HCD_XSTAT_AT + XHCI_SNAPSHOT_HCD_XSTAT_COUNT ==
                  XHCI_SNAPSHOT_HCD_TELE_AT &&
              XHCI_SNAPSHOT_HCD_TELE_AT +
                      XHCI_SNAPSHOT_HCD_TELE_COUNT *
                          XHCI_SNAPSHOT_HCD_TELE_RECORD_WORDS ==
                  XHCI_SNAPSHOT_HCD_DELIVERY_AT &&
              XHCI_SNAPSHOT_HCD_DELIVERY_AT == XHCI_SNAPSHOT_HCD_TELE_END);

/* 36.9: the delivery's six extension words are contiguous ULONGs, and
 * the pair that places them comes before 36.24's counters. */
XHCI_C_ASSERT(xhci_snap_hcd_delivery,
              XHCI_FIELD_OFFSET(XHCI_EXTENSION, LineRetireState) ==
                  XHCI_FIELD_OFFSET(XHCI_EXTENSION, EventDelivery) +
                      (XHCI_SNAPSHOT_HCD_DELIVERY_COUNT - 1) * 4UL &&
              XHCI_SNAPSHOT_HCD_DELIVERY_WORDS + 1 ==
                  XHCI_SNAPSHOT_HCD_XPIPE_WORDS);

/* 36.24's counters whole, after 36.9's two words, then 36.38's words. */
XHCI_C_ASSERT(xhci_snap_hcd_xpipe,
              sizeof(XHCI_XPIPE_STATS) ==
                  XHCI_XPIPE_STATS_WORDS * sizeof(ULONG) &&
              XHCI_SNAPSHOT_HCD_XPIPE_COUNT == XHCI_XPIPE_STATS_WORDS &&
              XHCI_SNAPSHOT_HCD_XPIPE_AT + XHCI_SNAPSHOT_HCD_XPIPE_COUNT ==
                  XHCI_SNAPSHOT_HCD_DNCTRL_LIVE);

/* 36.38: three DNCTRL words, the record count and size, then the records,
 * which end the header. */
XHCI_C_ASSERT(xhci_snap_hcd_susp,
              XHCI_SNAPSHOT_HCD_DNCTRL_LIVE + 1 ==
                  XHCI_SNAPSHOT_HCD_DNCTRL_ENABLES &&
              XHCI_SNAPSHOT_HCD_DNCTRL_ENABLES + 1 ==
                  XHCI_SNAPSHOT_HCD_DNCTRL_READBACK &&
              XHCI_SNAPSHOT_HCD_DNCTRL_READBACK + 1 ==
                  XHCI_SNAPSHOT_HCD_SUSP_RECORDS &&
              XHCI_SNAPSHOT_HCD_SUSP_RECORDS + 1 ==
                  XHCI_SNAPSHOT_HCD_SUSP_WORDS &&
              XHCI_SNAPSHOT_HCD_SUSP_WORDS + 1 == XHCI_SNAPSHOT_HCD_SUSP_AT &&
              XHCI_SNAPSHOT_HCD_SUSP_LAST + 1 ==
                  XHCI_SNAPSHOT_HCD_SUSP_RECORD_WORDS &&
              XHCI_SNAPSHOT_HCD_SUSP_AT +
                      XHCI_SNAPSHOT_HCD_SUSP_COUNT *
                          XHCI_SNAPSHOT_HCD_SUSP_RECORD_WORDS ==
                  XHCI_SNAPSHOT_HCD_HEAD_WORDS);

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
    case XHCI_SNAPSHOT_HCD_PIRQ_WORDS:
        return XHCI_SNAPSHOT_HCD_PIRQ_COUNT;
    case XHCI_SNAPSHOT_HCD_XSTAT_WORDS:
        return XHCI_SNAPSHOT_HCD_XSTAT_COUNT;
    case XHCI_SNAPSHOT_HCD_TELE_RECORDS:
        return XHCI_SNAPSHOT_HCD_TELE_COUNT;
    case XHCI_SNAPSHOT_HCD_TELE_WORDS:
        return XHCI_SNAPSHOT_HCD_TELE_RECORD_WORDS;
    case XHCI_SNAPSHOT_HCD_DELIVERY_AT:
        return XHCI_FIELD_OFFSET(XHCI_EXTENSION, EventDelivery);
    case XHCI_SNAPSHOT_HCD_DELIVERY_WORDS:
        return XHCI_SNAPSHOT_HCD_DELIVERY_COUNT;
    case XHCI_SNAPSHOT_HCD_XPIPE_WORDS:
        return XHCI_SNAPSHOT_HCD_XPIPE_COUNT;
    case XHCI_SNAPSHOT_HCD_SUSP_RECORDS:
        return XHCI_SNAPSHOT_HCD_SUSP_COUNT;
    case XHCI_SNAPSHOT_HCD_SUSP_WORDS:
        return XHCI_SNAPSHOT_HCD_SUSP_RECORD_WORDS;
    default:
        /* XHCI_SNAPSHOT_HCD_TERMINAL, the routing record, the transfer
         * readings, the class driver records, and 36.38's DNCTRL words and
         * device records are readings, not layout: the door fills them
         * (hcd_door.c). */
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
    return XHCI_SNAPSHOT_HCD_PAST_END;
}

ULONG XhciSnapHcdFlags(ULONG settleDeferred, ULONG linkRecovering,
                       ULONG unreadable)
{
    return (settleDeferred ? XHCI_SNAPSHOT_HCD_F_DEFERRED : 0UL) |
           (linkRecovering ? XHCI_SNAPSHOT_HCD_F_RECOVERING : 0UL) |
           (unreadable ? XHCI_SNAPSHOT_HCD_F_UNREADABLE : 0UL);
}
