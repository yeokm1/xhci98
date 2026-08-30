/*
 * xhci_probe.h - the runtime transfer-contract probe (roadmap task 6-V.1).
 *
 * This is instrumentation, not behaviour. Nothing here decides anything the
 * driver does: the probe sits at the registration-packet surface in
 * src/xhci_dispatch.c, reads what usbport just handed over, folds it into a
 * classification, and leaves counters behind. Deleting every call would change
 * no transfer, no command and no register write.
 *
 * What it exists to answer is the part of the transfer contract that reading
 * usbport's code could not settle (docs/usb-xhci-info/usbport-miniport-interface.md, "What
 * Phase 3 can and cannot prove about transfer mapping"):
 *
 *   - **Element ordering versus SgOffset.** The disassembly shows offsets
 *     increasing within one map round; a transfer needing several
 *     AllocateAdapterChannel rounds, and any bounce mapping (SgList->Flags bit
 *     0), are not settled by reading the code. This driver already orders by
 *     SgOffset defensively, so the probe can only confirm - which is why it
 *     counts the disagreement rather than trusting a "looks fine" line.
 *   - **The high DWORD.** Zero is inherited from the adapter's declared width,
 *     not enforced at the element, so it is checked and counted.
 *   - **The rest of the SubmitTransfer contract**: which parameter fields are
 *     populated, what an empty SG list really carries, the setup bytes, and the
 *     callback's IRQL.
 *   - **Topology, for Phase 7b** (design doc 02 section 4): whether an
 *     endpoint-open parameter block carries hub address, port or parent speed,
 *     and the hub-class setup requests that would let the tree be reconstructed
 *     by snooping. Logged here so 7b's feasibility gate rides this binary
 *     instead of needing a second instrumentation build.
 *   - **Two of batch 6-0's negatives, at runtime.** `CloseEndpoint` and
 *     `GetEndpointState` are never called by either shipping build - a fact read
 *     out of both whole images. `ProbeEpEvents[]` is what would refute that on a
 *     target, and it does so in a *release* build, which no trace line can.
 *
 * ## Why the gates are what they are
 *
 * Every trace macro's budget is a per-site driver-image static that no start,
 * stop or resume resets, so a probe that printed per transfer would be silent
 * long before an operator plugged anything in (docs/contributing/design/03-host-unit-
 * tests.md, "What the host suite cannot check"). So the announcement gates here
 * are **semantic**: a line is printed when the call shows a property that has
 * not been seen since this controller started, and not otherwise. A bus that
 * moves a million transfers prints exactly as much as one that moves thirty.
 *
 * Two shapes of gate, because two shapes of question:
 *
 *   - a fixed set of properties -> an OR-accumulated bitmask (`ProbeSgShape`,
 *     `ProbeEpShape`), bounded by the number of bits;
 *   - an open-ended key, such as a setup packet -> `XHCI_PROBE_KEYSET`, a small
 *     saturating set that counts what it had to drop rather than silently
 *     forgetting it.
 *
 * Both keep a **firing count** beside the state, because a set is idempotent
 * under OR and looks identical whether its gate works or not - the host suite
 * checks the firings, since it cannot see the printing at all.
 *
 * The state lives in the miniport extension, so usbport's zeroing of that
 * extension makes the whole probe **per StartController**: the log re-announces
 * once per start, which is what makes a log spanning several starts readable
 * rather than a flood.
 *
 * C89 only. Every function is DISPATCH_LEVEL-safe: no allocation, no wait, no
 * usbport service, and no MMIO.
 */

#ifndef XHCI_PROBE_H
#define XHCI_PROBE_H

#include "xhci_compat.h"

/* ------------------------------------------------------------------ */
/* The saturating key set                                              */
/* ------------------------------------------------------------------ */

/*
 * Sixteen keys. Enumerating one device costs six or seven distinct setup
 * packets, so this holds two devices' worth before it saturates, and
 * `Overflows` is what says it did - a probe that silently forgot the
 * seventeenth key would look identical to one that never met it.
 */
#define XHCI_PROBE_KEYS 16

typedef struct _XHCI_PROBE_KEYSET {
    ULONG Count;                    /* keys held */
    ULONG Firings;                  /* announcements made */
    ULONG Overflows;                /* distinct keys refused because it was full */
    ULONG Key[XHCI_PROBE_KEYS];
} XHCI_PROBE_KEYSET;

/* ------------------------------------------------------------------ */
/* Transfer shape bits                                                 */
/* ------------------------------------------------------------------ */

/*
 * Every bit is a property of one SubmitTransfer call. They are deliberately
 * paired where a property has two readings (ascending/disordered, mapped/
 * unmapped): a set "ascending" bit is evidence, an absent one is only silence,
 * and the pair is what tells them apart.
 */
#define XHCI_PROBE_SG_NULL          0x00000001UL /* refutes batch 6-0's "never NULL" */
#define XHCI_PROBE_SG_EMPTY         0x00000002UL /* SgElementCount == 0 */
#define XHCI_PROBE_SG_SINGLE        0x00000004UL
#define XHCI_PROBE_SG_MULTI         0x00000008UL
#define XHCI_PROBE_SG_ASCENDING     0x00000010UL /* SgOffset ascends in list order */
#define XHCI_PROBE_SG_DISORDERED    0x00000020UL /* ...and the case task 6-V.1 exists for */
#define XHCI_PROBE_SG_TILES         0x00000040UL /* offsets+lengths tile from 0 */
#define XHCI_PROBE_SG_GAPPED        0x00000080UL
#define XHCI_PROBE_SG_PAGE_ALIGNED  0x00000100UL /* first element's PA is page-aligned */
#define XHCI_PROBE_SG_MIDPAGE       0x00000200UL
#define XHCI_PROBE_SG_HIGH_DWORD    0x00000400UL /* an element carried a nonzero high DWORD */
#define XHCI_PROBE_SG_MAPPED        0x00000800UL /* SgList->Flags bit 0 - the bounce case */
#define XHCI_PROBE_SG_UNMAPPED      0x00001000UL
#define XHCI_PROBE_SG_TRUNCATED     0x00002000UL /* more elements than the walk reads */
#define XHCI_PROBE_SG_LENGTH_GAP    0x00004000UL /* lengths do not sum to the buffer length */
#define XHCI_PROBE_XFER_IN          0x00008000UL
#define XHCI_PROBE_XFER_OUT         0x00010000UL
#define XHCI_PROBE_XFER_ZERO_LENGTH 0x00020000UL
#define XHCI_PROBE_XFER_SPLIT       0x00040000UL /* IsTransferSplited nonzero */
/*
 * A class-type request, and separately one whose **recipient** makes it a hub
 * candidate. The distinction is not pedantry: the first Win2000 run read eleven
 * "hub-class setups" on a bus with no hub on it, because the audio class driver
 * issues class requests with an *interface* recipient (`0xA1`/`0x21`) and the
 * classifier tested only the type. Design doc 02's hub traffic is
 * GET_DESCRIPTOR(Hub) at `0xA0` - class, **device** recipient - and
 * SET_FEATURE(PORT_RESET) at `0x23` - class, **other** recipient. Any class
 * driver produces the first bit; only a hub produces the second.
 */
#define XHCI_PROBE_XFER_CLASS_SETUP 0x00080000UL /* class type, any recipient */
#define XHCI_PROBE_XFER_HUB_SETUP   0x00100000UL /* ...and a hub's recipient   */

/* How many elements the walk reads. Above this the shape carries TRUNCATED and
 * the classification describes the prefix, which is honest; a transfer that
 * large is already past this driver's own TRB cap. */
#define XHCI_PROBE_SG_WALK 64

/* How many announcements per start may carry a full element dump. The shape
 * line is unbudgeted because its gate is semantic; the dump is many lines per
 * announcement, so it gets a small budget of its own - held in the extension,
 * therefore reset by each StartController rather than spent for the life of the
 * load. */
#define XHCI_PROBE_SG_DUMPS 4

/* And how many elements one dump prints. */
#define XHCI_PROBE_SG_DUMP_ELEMENTS 8

/* ------------------------------------------------------------------ */
/* Endpoint shape bits and events                                      */
/* ------------------------------------------------------------------ */

#define XHCI_PROBE_EP_NO_TT         0x00000001UL /* HubAddr == 0xFFFF */
#define XHCI_PROBE_EP_TT            0x00000002UL /* a real TT hub address - Phase 7b */
#define XHCI_PROBE_EP_PORT          0x00000004UL /* PortNumber nonzero */
#define XHCI_PROBE_EP_LOW           0x00000008UL
#define XHCI_PROBE_EP_FULL          0x00000010UL
#define XHCI_PROBE_EP_HIGH          0x00000020UL
#define XHCI_PROBE_EP_SPEED_OTHER   0x00000040UL
#define XHCI_PROBE_EP_ADDRESS_ZERO  0x00000080UL
#define XHCI_PROBE_EP_ADDRESSED     0x00000100UL
#define XHCI_PROBE_EP_CONTROL       0x00000200UL
#define XHCI_PROBE_EP_NON_CONTROL   0x00000400UL
#define XHCI_PROBE_EP_BUFFER        0x00000800UL /* usbport supplied a per-endpoint buffer */
#define XHCI_PROBE_EP_NO_BUFFER     0x00001000UL
#define XHCI_PROBE_EP_PERIOD        0x00002000UL /* Period nonzero - Phase 7a's interval */

/* The four speed bits together. One of them is always set, so this is also what
 * says a per-pair speed record below was ever written to. */
#define XHCI_PROBE_EP_SPEED_MASK                                    \
    (XHCI_PROBE_EP_LOW | XHCI_PROBE_EP_FULL | XHCI_PROBE_EP_HIGH |  \
     XHCI_PROBE_EP_SPEED_OTHER)

/* ------------------------------------------------------------------ */
/* The raw topology pairs (roadmap task 7b-A.1.2)                      */
/* ------------------------------------------------------------------ */

/*
 * `HubAddr`/`PortNumber`, kept as the numbers usbport wrote rather than as bits.
 *
 * XHCI_PROBE_EP_TT and XHCI_PROBE_EP_PORT already say *that* a transaction
 * translator was named, and that was enough for batch 7b-V0's feasibility
 * question. It is not enough for the one measurement left open: batch 7a-0's
 * corrected reading of `PortNumber` (the port on the nearest High-Speed ancestor,
 * at any depth) and the superseded one (the port on the immediate parent) differ
 * **only** for a device two non-High-Speed tiers down, and they differ in the
 * value, not in whether it is nonzero. A shape bit cannot separate them, and the
 * per-observation trace that could belongs to the `qemu` flavour alone (task
 * 13-L.1; this said `#if DBG` until the post-Phase 13 review rounds, and `DBG` is now set in a
 * shipping build that has no trace) - so either published build, which is what
 * a long VM run has to be, says "a TT was named" and never which one.
 *
 * Hence a small table of the distinct pairs seen, with each pair's own witness
 * beside it: the pair alone does not say whose it is, and on the path that
 * matters every candidate arrives at `DeviceAddress` 0, so the address is
 * recorded as a *set* rather than as the first one seen. `Speeds` separates a
 * hub's own opens - High-Speed, whether really or by Phase 5 task 7's
 * override - from the Full/Low-Speed child whose pair is the reading.
 *
 * Distinct pairs, not observations, because a bus repeats the same pair for
 * every endpoint of every device on it; `Observations` is what says how hard.
 */
#define XHCI_PROBE_TT_PAIRS 12

typedef struct _XHCI_PROBE_TT_ENTRY {
    ULONG Pair;         /* (HubAddr << 16) | PortNumber, exactly as read */
    ULONG Observations; /* property blocks that carried it */
    /* Bit N: seen with DeviceAddress N. Bit 31 stands for every address at or
     * above 31, so a large bus saturates the top bit rather than aliasing down
     * onto a real address. Bit 0 is the reading that matters here - a device
     * that never enumerates is only ever seen at address 0. */
    ULONG Addresses;
    ULONG Speeds;       /* OR of the XHCI_PROBE_EP_SPEED_MASK bits */
} XHCI_PROBE_TT_ENTRY;

typedef struct _XHCI_PROBE_TT_TABLE {
    ULONG Count;   /* distinct pairs held */
    ULONG Dropped; /* observations refused because it was full - not distinct
                    * pairs: a pair met twice after saturation counts twice,
                    * which is what makes a full table's silence measurable */
    XHCI_PROBE_TT_ENTRY Entry[XHCI_PROBE_TT_PAIRS];
} XHCI_PROBE_TT_TABLE;

/*
 * The callback that produced the observation. Indices into
 * XHCI_EXTENSION.ProbeEpEvents[], so they start at 0 and NONE is not one of
 * them.
 */
#define XHCI_PROBE_EVENT_QUERY     0
#define XHCI_PROBE_EVENT_OPEN      1
#define XHCI_PROBE_EVENT_REOPEN    2
#define XHCI_PROBE_EVENT_CLOSE     3
#define XHCI_PROBE_EVENT_SET_STATE 4
#define XHCI_PROBE_EVENT_GET_STATE 5
#define XHCI_PROBE_EVENT_ABORT     6
#define XHCI_PROBE_EVENT_COUNT     7

/* ------------------------------------------------------------------ */
/* What one fold decided, for the reporter to print                    */
/* ------------------------------------------------------------------ */

/*
 * The split exists because of the lock, not for tidiness: the fold mutates
 * probe state and therefore runs under the controller lock, while the reporter
 * writes tens of bytes to DbgPrint and to port 0xE9 and therefore must not.
 * The usbport-owned structures the reporter re-reads are valid for the whole
 * callback, so reading them after the release is sound.
 */
typedef struct _XHCI_PROBE_REPORT {
    ULONG Announce;     /* nonzero: the gate fired */
    ULONG Event;        /* XHCI_PROBE_EVENT_* for the endpoint probe */
    ULONG Shape;        /* this call's shape word */
    ULONG NewBits;      /* the part of it never seen since this start */
    ULONG NewKey;       /* nonzero: the key was new too */
    ULONG Detail;       /* setup key / endpoint state, for the line */
    ULONG Sequence;     /* transfer probe: which call this was, within the
                         * start. Endpoint probe: not an ordinal at all - the
                         * identity key (slot/DCI, or address/endpoint) whose
                         * newness NewKey reports. Read it per producer. */
    ULONG DumpElements; /* nonzero: the SG elements are worth the lines */
} XHCI_PROBE_REPORT;

struct _XHCI_EXTENSION;
struct _USBPORT_TRANSFER_PARAMETERS;
struct _USBPORT_SCATTER_GATHER_LIST;
struct _USBPORT_ENDPOINT_PROPERTIES;
struct _XHCI_ENDPOINT;

/*
 * Observe one SubmitTransfer call. Takes and releases the controller lock
 * itself, and prints nothing while holding it.
 * IRQL: <= DISPATCH_LEVEL, no usbport lock assumptions.
 */
VOID XhciProbeTransfer(struct _XHCI_EXTENSION *ext,
                       const struct _USBPORT_TRANSFER_PARAMETERS *parameters,
                       const struct _USBPORT_SCATTER_GATHER_LIST *sgList);

/*
 * Observe one endpoint-family callback. `properties` is NULL for the callbacks
 * that do not carry one (close, the state pair, abort), `endpoint` is NULL
 * where there is no miniport endpoint extension yet, and `detail` carries the
 * state for SetEndpointState and 0 otherwise.
 * IRQL: <= DISPATCH_LEVEL.
 */
VOID XhciProbeEndpoint(struct _XHCI_EXTENSION *ext,
                       ULONG event,
                       const struct _USBPORT_ENDPOINT_PROPERTIES *properties,
                       const struct _XHCI_ENDPOINT *endpoint,
                       ULONG detail);

#endif /* XHCI_PROBE_H */
