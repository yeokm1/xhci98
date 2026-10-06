/*
 * xhci_snap.c - the layout of the snapshot's HCD region (roadmap-hcd.md
 * task 35.3; src/xhci.h, XHCI_SNAPSHOT_REGION_HCD).
 *
 * 35.0's E460 reading needed a hand-built offset table and a static
 * inference to say why a trained SuperSpeed link never got a slot: the
 * driver's per-port enumeration state and its counter block live in the
 * HCD's controller object, outside the extension the snapshot copies. The
 * HCD region publishes both as a pointer-free image of ULONGs - an eight-word
 * header, one record per root port, the counters - which the door
 * (hcd_door.c) fills word by word into the caller's window, so no image of
 * the whole is ever built in kernel memory. What lives here is where each
 * word of that image lies and what the header says, so the host suite
 * checks the layout with no controller (test\test_snap.c).
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
    default:
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
