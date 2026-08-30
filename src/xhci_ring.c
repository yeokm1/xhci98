/*
 * xhci_ring.c - TRB encoding and the ring state machines.
 *
 * Pure computation plus stores into caller-supplied common-buffer memory: no
 * MMIO, no DDK calls, no IRQL dependencies, so it builds and runs on the host
 * under XHCI_HOST_TEST (docs/contributing/design/03-host-unit-tests.md).
 * test/test_ring.c is the regression suite. Bit positions come from
 * docs/usb-xhci-info/xhci-data-structures.md section 7.
 *
 * Nothing here rings a doorbell or touches a register. That is the split
 * design doc 03 section 2 asks for: encode first, then one call site in
 * xhci_init.c / xhci_pci.c writes the doorbell. It is also what makes >= 3
 * full laps of a ring - the case no amount of VM testing produces
 * deliberately - a test that runs in milliseconds.
 *
 * Two ownership rules hold throughout:
 *
 *   - The Cycle Bit is this layer's to set, and it is published last - last
 *     within a TRB, and last within a whole TD. A TRB template handed to
 *     XhciRingEnqueueTd carries every other field; whatever it has in bit 0
 *     is discarded. Enqueueing is therefore a TD-level operation: see the
 *     comment on XhciRingEnqueueTd for why a loop over single TRBs would hand
 *     the controller a half-written TD.
 *   - `Dequeue` is *software's* belief about the hardware, advanced from
 *     completion events (XhciRingRetireTd). Hardware never writes it, and the
 *     command/transfer ring gives software no other way to learn it. It moves
 *     in whole TDs, because that is how the hardware moves: an event reports
 *     the TRB that generated it, which is not always the TD's last.
 *
 * C89 only. IRQL: every function is callable at any IRQL.
 */

#include "xhci.h"

/*
 * Producer rings keep one slot permanently empty so "enqueue == dequeue"
 * always means empty and never full (spec 4.9.2.2: the ring is full when
 * advancing Enqueue would reach Dequeue). With the Link TRB occupying the
 * last position, a segment of N TRBs therefore holds N - 2 outstanding TRBs.
 */
#define XHCI_RING_MIN_TRBS 3

static ULONG xhciRingSlots(const XHCI_RING *ring)
{
    return ring->Trbs - 1;
}

/* Next producer index, skipping the Link TRB rather than resting on it. */
static ULONG xhciRingNext(const XHCI_RING *ring, ULONG index)
{
    ULONG next;

    next = index + 1;
    if (next >= ring->Trbs - 1) {
        return 0;
    }
    return next;
}

/* One definition of "how much is outstanding", used by the free count, the
 * ownership walk and the retire bounds check - three places that must agree. */
static ULONG xhciRingUsed(const XHCI_RING *ring)
{
    ULONG slots;

    slots = xhciRingSlots(ring);
    return (ring->Enqueue + slots - ring->Dequeue) % slots;
}

static ULONG xhciRingDistance(const XHCI_RING *ring, ULONG index)
{
    ULONG slots;

    slots = xhciRingSlots(ring);
    return (index + slots - ring->Dequeue) % slots;
}

static ULONG xhciRingIsOutstanding(const XHCI_RING *ring, ULONG index)
{
    if (index >= ring->Trbs - 1) {
        return 0;                       /* the Link TRB is never a TD's TRB */
    }
    return (xhciRingDistance(ring, index) < xhciRingUsed(ring)) ? 1 : 0;
}

/*
 * Is a segment of `trbs` TRBs at `basePA` legal? Table 6-1 gives ring segments
 * three rules and this checks all of them, in the order that makes each one
 * reachable by a test:
 *
 *   1. at most 64 KB;
 *   2. `alignment`-byte aligned (16 for transfer rings, 64 for command and
 *      event rings);
 *   3. it must not *span* a 64 KB boundary - a separate rule from being
 *      64 KB or smaller, and the one that is easy to satisfy accidentally and
 *      then lose. The carve in xhci_mem.c keeps every ring inside one page,
 *      which implies this, but that is the carve's property and not this
 *      function's caller's.
 *
 * Plus the project rule that has nothing to do with the spec: no 64-bit DMA,
 * so a segment may not run off the top of the 32-bit address space. Written as
 * `basePA <= 0xFFFFFFFF - (bytes - 1)` because the last byte is at
 * `basePA + bytes - 1`; the form without the -1 rejects a segment ending
 * exactly at 0xFFFFFFFF, which is legal.
 */
ULONG XhciSegmentUsable(ULONG basePA, ULONG trbs, ULONG alignment)
{
    ULONG bytes;

    if (trbs == 0 || trbs > (65536UL / XHCI_TRB_BYTES)) {
        return 0;
    }
    if ((basePA & (alignment - 1)) != 0) {
        return 0;
    }
    bytes = trbs * XHCI_TRB_BYTES;
    if (basePA > (0xFFFFFFFFUL - (bytes - 1))) {
        return 0;
    }
    if ((basePA & 0xFFFFUL) > (0x10000UL - bytes)) {
        return 0;
    }
    return 1;
}

VOID XhciTrbClear(XHCI_TRB *trb)
{
    trb->Param0 = 0;
    trb->Param1 = 0;
    trb->Status = 0;
    trb->Control = 0;
}

VOID XhciTrbNoOpCommand(XHCI_TRB *trb)
{
    XhciTrbClear(trb);
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NOOP_COMMAND);
}

/*
 * A Link TRB's cycle bit is the gate the hardware stops at, so it is an
 * argument here rather than something the builder decides: at ring init it is
 * the *opposite* of the producer cycle (the hardware must not follow the link
 * before software has filled a lap), and XhciRingEnqueueTd flips it to the
 * producer cycle at the moment it passes.
 */
VOID XhciTrbLink(XHCI_TRB *trb, ULONG segmentPA, ULONG toggleCycle, ULONG cycle)
{
    XhciTrbClear(trb);
    trb->Param0 = segmentPA;
    trb->Param1 = 0;
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_LINK);
    if (toggleCycle) {
        trb->Control |= XHCI_TRB_LINK_TC;
    }
    if (cycle) {
        trb->Control |= XHCI_TRB_CYCLE;
    }
}

/* ------------------------------------------------------------------ */
/* Device-lifecycle command TRBs (Phase 6 batch B)                     */
/* ------------------------------------------------------------------ */

/*
 * Two shared rules, stated once here rather than at each of the four builders.
 *
 * **A field that does not fit is a refusal, not a mask.** Slot ID is eight bits
 * and an Input Context Pointer's low four are RsvdZ; truncating either produces
 * a command that is perfectly well formed and addressed at something else - a
 * different slot, or an address that is not a context. Every builder therefore
 * writes nothing at all when an argument is out of range, which is the same
 * shape XhciRingInit and the transfer builder already use.
 *
 * **The high DWORD of every pointer is written as zero explicitly.** This driver
 * has no 64-bit DMA, so it is always zero - but the caller's template is not
 * guaranteed to be, and a stale high half is a pointer above 4 GB.
 */
static ULONG xhciCommandSlotIdOk(ULONG slotId)
{
    /* Slot ID 0 is "no slot" in every command that carries one (4.5.3), and 255
     * is the widest the field goes. */
    return (slotId != 0 && slotId <= 0xFFUL) ? 1 : 0;
}

static ULONG xhciInputContextPaOk(ULONG pa)
{
    /* "Input Context Pointer ... 16-byte aligned" (Table 6-51). A zero would
     * point the xHC at physical page 0. */
    return (pa != 0 && (pa & 0x0FUL) == 0) ? 1 : 0;
}

ULONG XhciTrbEnableSlot(XHCI_TRB *trb, ULONG slotType)
{
    if (trb == NULL || slotType > 0x1FUL) {
        return XHCI_RING_BAD_PARAM;
    }
    XhciTrbClear(trb);
    /*
     * Slot Type comes from the Supported Protocol capability of the port group
     * the device is on (Table 6-46 note; docs/usb-xhci-info/xhci-data-structures.md section 6),
     * which is why it is an argument rather than the constant 0 the USB 2.0
     * group happens to use on every controller measured so far.
     */
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_ENABLE_SLOT) |
                   XHCI_TRB_SLOT_TYPE(slotType);
    return XHCI_RING_OK;
}

ULONG XhciTrbDisableSlot(XHCI_TRB *trb, ULONG slotId)
{
    if (trb == NULL || !xhciCommandSlotIdOk(slotId)) {
        return XHCI_RING_BAD_PARAM;
    }
    XhciTrbClear(trb);
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_DISABLE_SLOT) |
                   XHCI_TRB_SLOT_ID(slotId);
    return XHCI_RING_OK;
}

ULONG XhciTrbResetDevice(XHCI_TRB *trb, ULONG slotId)
{
    if (trb == NULL || !xhciCommandSlotIdOk(slotId)) {
        return XHCI_RING_BAD_PARAM;
    }
    XhciTrbClear(trb);
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_RESET_DEVICE) |
                   XHCI_TRB_SLOT_ID(slotId);
    return XHCI_RING_OK;
}

/*
 * BSR is the whole difference between the two Address Device commands this
 * driver issues, and they are one builder because they are one command:
 * "if the BSR flag is '1' ... the Slot State is set to Default and no
 * SET_ADDRESS request is generated" (4.6.5, p.101), against BSR = 0, which
 * "generates a USB SET_ADDRESS request" and moves the slot to Addressed.
 *
 * The first is how EP0 becomes usable *before* the device has an address
 * (task 6-B.2); the second is what SET_ADDRESS is intercepted into
 * (task 6-B.3), because software may never place that setup packet on a
 * transfer ring (4.5.4.1).
 */
ULONG XhciTrbAddressDevice(XHCI_TRB *trb,
                           ULONG slotId,
                           ULONG inputContextPA,
                           ULONG blockSetAddress)
{
    if (trb == NULL || !xhciCommandSlotIdOk(slotId) ||
        !xhciInputContextPaOk(inputContextPA)) {
        return XHCI_RING_BAD_PARAM;
    }
    XhciTrbClear(trb);
    trb->Param0 = inputContextPA;
    trb->Param1 = 0;
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_ADDRESS_DEVICE) |
                   XHCI_TRB_SLOT_ID(slotId);
    if (blockSetAddress) {
        trb->Control |= XHCI_TRB_BSR;
    }
    return XHCI_RING_OK;
}

ULONG XhciTrbConfigureEndpoint(XHCI_TRB *trb,
                               ULONG slotId,
                               ULONG inputContextPA,
                               ULONG deconfigure)
{
    if (trb == NULL || !xhciCommandSlotIdOk(slotId)) {
        return XHCI_RING_BAD_PARAM;
    }
    /*
     * A Deconfigure carries no Input Context - spec 6.4.3.5 says the pointer is
     * ignored when DC is set - so it is the one command in this family that may
     * legitimately pass 0, and requiring a context for it would force callers to
     * hand over a meaningless address. Every other case still goes through the
     * same validity check as Address Device and Evaluate Context.
     */
    if (deconfigure) {
        XhciTrbClear(trb);
        trb->Param0 = 0;
        trb->Param1 = 0;
        trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_CONFIGURE_EP) |
                       XHCI_TRB_SLOT_ID(slotId) | XHCI_TRB_DC;
        return XHCI_RING_OK;
    }

    if (!xhciInputContextPaOk(inputContextPA)) {
        return XHCI_RING_BAD_PARAM;
    }
    XhciTrbClear(trb);
    trb->Param0 = inputContextPA;
    trb->Param1 = 0;
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_CONFIGURE_EP) |
                   XHCI_TRB_SLOT_ID(slotId);
    return XHCI_RING_OK;
}

ULONG XhciTrbEvaluateContext(XHCI_TRB *trb,
                             ULONG slotId,
                             ULONG inputContextPA)
{
    if (trb == NULL || !xhciCommandSlotIdOk(slotId) ||
        !xhciInputContextPaOk(inputContextPA)) {
        return XHCI_RING_BAD_PARAM;
    }
    XhciTrbClear(trb);
    trb->Param0 = inputContextPA;
    trb->Param1 = 0;
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_EVALUATE_CONTEXT) |
                   XHCI_TRB_SLOT_ID(slotId);
    return XHCI_RING_OK;
}

/* ------------------------------------------------------------------ */
/* Per-endpoint command TRBs (batch 7a-B)                              */
/* ------------------------------------------------------------------ */

/*
 * The Endpoint ID field is the DCI, and it is checked against the same 1..31
 * range spec 4.5.1 gives the Device Context Index rather than against the 5-bit
 * field's width. The two differ at exactly one value - 0, the Slot Context -
 * and that is the value a zeroed record holds, so masking it in would aim a Stop
 * Endpoint at the slot rather than at an endpoint.
 */
static ULONG xhciCommandDciOk(ULONG dci)
{
    return (dci >= 1 && dci <= XHCI_MAX_DCI) ? 1 : 0;
}

ULONG XhciTrbStopEndpoint(XHCI_TRB *trb,
                          ULONG slotId,
                          ULONG dci,
                          ULONG suspend)
{
    if (trb == NULL || !xhciCommandSlotIdOk(slotId) || !xhciCommandDciOk(dci)) {
        return XHCI_RING_BAD_PARAM;
    }
    XhciTrbClear(trb);
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_STOP_EP) |
                   XHCI_TRB_EP_ID(dci) | XHCI_TRB_SLOT_ID(slotId);
    if (suspend) {
        trb->Control |= XHCI_TRB_SP;
    }
    return XHCI_RING_OK;
}

ULONG XhciTrbResetEndpoint(XHCI_TRB *trb,
                           ULONG slotId,
                           ULONG dci,
                           ULONG preserveState)
{
    if (trb == NULL || !xhciCommandSlotIdOk(slotId) || !xhciCommandDciOk(dci)) {
        return XHCI_RING_BAD_PARAM;
    }
    XhciTrbClear(trb);
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_RESET_EP) |
                   XHCI_TRB_EP_ID(dci) | XHCI_TRB_SLOT_ID(slotId);
    if (preserveState) {
        trb->Control |= XHCI_TRB_TSP;
    }
    return XHCI_RING_OK;
}

ULONG XhciTrbSetTrDequeue(XHCI_TRB *trb,
                          ULONG slotId,
                          ULONG dci,
                          ULONG dequeuePA,
                          ULONG dcs)
{
    if (trb == NULL || !xhciCommandSlotIdOk(slotId) || !xhciCommandDciOk(dci)) {
        return XHCI_RING_BAD_PARAM;
    }
    /*
     * The pointer's low four bits are not part of the address: bit 0 is DCS and
     * bits 3:1 are SCT, which this driver writes as 0 because it enables no
     * Streams. So an address carrying any of them is one this builder would
     * silently turn into a different command, and it is refused for the same
     * reason a misaligned Input Context pointer is.
     */
    if (dequeuePA == 0 || (dequeuePA & 0x0FUL) != 0 || dcs > 1) {
        return XHCI_RING_BAD_PARAM;
    }
    XhciTrbClear(trb);
    trb->Param0 = dequeuePA | dcs;
    trb->Param1 = 0;
    /* DW2 carries the Stream ID in 31:16, and this driver has none. Left as the
     * zero XhciTrbClear wrote, stated here because a nonzero Stream ID with
     * MaxPStreams = 0 is answered with TRB Error (spec 4.6.10 p.128). */
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_SET_TR_DEQUEUE) |
                   XHCI_TRB_EP_ID(dci) | XHCI_TRB_SLOT_ID(slotId);
    return XHCI_RING_OK;
}

ULONG XhciRingInit(PXHCI_RING ring,
                   volatile XHCI_TRB *base,
                   ULONG basePA,
                   ULONG trbs,
                   ULONG kind)
{
    XHCI_TRB link;
    ULONG i;

    if (ring == NULL || base == NULL || basePA == 0) {
        return XHCI_RING_BAD_PARAM;
    }
    if (trbs < XHCI_RING_MIN_TRBS) {
        return XHCI_RING_BAD_PARAM;
    }
    /* Fixed here so no event-time caller can restate it differently. */
    if (kind > XHCI_RING_KIND_ISOCH) {
        return XHCI_RING_BAD_PARAM;
    }
    /*
     * Table 6-1: transfer ring segments are 16-byte aligned, command rings
     * 64-byte - and the rule is derived from `kind` here rather than delegated
     * to the carve. The carve does page-align every ring region, so this is
     * unreachable today; but it was the only statement of the command ring's
     * alignment written as a comment instead of a check, and CRCR's pointer is
     * bits 63:6, so a 16-byte-aligned command ring is one whose base address
     * the register cannot express. `XhciEventRingInit` already enforces its own
     * 64.
     */
    if (!XhciSegmentUsable(basePA, trbs,
                           (kind == XHCI_RING_KIND_COMMAND) ? 64UL
                                                            : XHCI_TRB_BYTES)) {
        return XHCI_RING_BAD_PARAM;
    }

    for (i = 0; i < trbs; i++) {
        base[i].Param0 = 0;
        base[i].Param1 = 0;
        base[i].Status = 0;
        base[i].Control = 0;
    }

    ring->Base = base;
    ring->BasePA = basePA;
    ring->Trbs = trbs;
    ring->Enqueue = 0;
    ring->Dequeue = 0;
    ring->Cycle = 1;
    ring->Kind = kind;

    XhciTrbLink(&link, basePA, 1, ring->Cycle ^ 1);
    base[trbs - 1].Param0 = link.Param0;
    base[trbs - 1].Param1 = link.Param1;
    base[trbs - 1].Status = link.Status;
    base[trbs - 1].Control = link.Control;

    return XHCI_RING_OK;
}

ULONG XhciRingCapacity(const XHCI_RING *ring)
{
    return xhciRingSlots(ring) - 1;
}

ULONG XhciRingFree(const XHCI_RING *ring)
{
    return xhciRingSlots(ring) - 1 - xhciRingUsed(ring);
}

ULONG XhciRingHasRoom(const XHCI_RING *ring, ULONG trbs)
{
    return (XhciRingFree(ring) >= trbs) ? 1 : 0;
}

ULONG XhciRingTrbPA(const XHCI_RING *ring, ULONG index)
{
    if (index >= ring->Trbs) {
        return 0;
    }
    return ring->BasePA + index * XHCI_TRB_BYTES;
}

ULONG XhciRingIndexFromPA(const XHCI_RING *ring, ULONG pa, ULONG *index)
{
    ULONG delta;

    if (index == NULL) {
        return XHCI_RING_BAD_PARAM;
    }
    if (pa < ring->BasePA) {
        return XHCI_RING_NOT_ON_RING;
    }
    delta = pa - ring->BasePA;
    if ((delta & (XHCI_TRB_BYTES - 1)) != 0) {
        return XHCI_RING_NOT_ON_RING;
    }
    delta = delta / XHCI_TRB_BYTES;
    if (delta >= ring->Trbs) {
        return XHCI_RING_NOT_ON_RING;
    }
    *index = delta;
    return XHCI_RING_OK;
}

ULONG XhciRingNextIndex(const XHCI_RING *ring, ULONG index)
{
    return xhciRingNext(ring, index);
}

/*
 * Spec 4.10.1.1.2 p.175: "the total number of received bytes for a Short Packet
 * TD is the sum of the TRB Transfer Length fields in all Transfer TRBs up to
 * and including the one that generated the Short Packet Event, minus the
 * residue value of the TRB Transfer Length field in the Short Packet Event."
 *
 * That sum is this function, and it is on the ring rather than in a side table
 * for the same reason XhciRingTdBounds reads the Chain flags off the ring: the
 * lengths are already there, in the words the hardware itself read, and a
 * parallel copy is one more thing that can fall out of step.
 *
 * The Link TRB is skipped rather than added - its DW2 is a segment pointer's
 * neighbour, not a transfer length - which is why this steps with xhciRingNext
 * instead of incrementing. Both ends must still be outstanding: a caller
 * summing across a retired TRB is reading a previous lap's leftovers.
 */
ULONG XhciRingSumTrbLengths(const XHCI_RING *ring,
                            ULONG fromIndex,
                            ULONG toIndex,
                            ULONG *bytes)
{
    ULONG index;
    ULONG steps;
    ULONG limit;
    ULONG total;

    if (ring == NULL || ring->Base == NULL || bytes == NULL) {
        return XHCI_RING_BAD_PARAM;
    }
    if (!xhciRingIsOutstanding(ring, fromIndex) ||
        !xhciRingIsOutstanding(ring, toIndex)) {
        return XHCI_RING_NOT_ON_RING;
    }
    /* Backwards is not a short walk the long way round: it is a caller that has
     * mixed up two positions, and answering it would return a plausible
     * number. */
    if (xhciRingDistance(ring, toIndex) < xhciRingDistance(ring, fromIndex)) {
        return XHCI_RING_NOT_ON_RING;
    }

    limit = xhciRingUsed(ring);
    total = 0;
    index = fromIndex;
    for (steps = 0; steps <= limit; steps++) {
        total += XHCI_TRB_GET_LENGTH(ring->Base[index].Status);
        if (index == toIndex) {
            *bytes = total;
            return XHCI_RING_OK;
        }
        index = xhciRingNext(ring, index);
    }
    return XHCI_RING_NOT_ON_RING;
}

/*
 * Enqueue a group of consecutive Transfer Descriptors that the hardware must
 * either see complete or not see at all - `count` TRBs partitioned into
 * `tdCount` TDs by `tdLengths`.
 *
 * The ordering rule is the reason this is a whole-group operation rather than a
 * loop over single TRBs, or over single TDs. If the first TRB were published as
 * each was written, a controller already consuming the TRBs ahead would walk
 * straight into work whose later TRBs did not exist yet. So the first TRB's
 * Control word is deliberately written with the *wrong* cycle bit first - the
 * hardware's "not produced yet" - every other TRB is written, and only then is
 * the first TRB's real Control word stored. That single store is what makes the
 * work exist. Writing the invalid word explicitly rather than leaving the slot's
 * previous-lap contents alone costs one store and removes the need to reason
 * about what happens to be in a slot from the previous lap.
 *
 * **Why a group and not a TD** is a Phase 6 finding rather than a generalization
 * for its own sake: a USB control transfer is not one TD. "Control transfers
 * require two or three TDs to define them: a Setup Stage TD followed by an
 * Status Stage TD, if a data stage is required for the transfer an optional Data
 * Stage TD will reside between" (spec 6.4.1.2, p.430). Publishing those one TD
 * at a time would let the xHC start the SETUP transaction of a control transfer
 * whose Status Stage TRB software has not written yet.
 *
 * A group may span the Link TRB, and the spec has rules for that (4.11.7, and
 * the Link TRB notes at 4.11.5.1) which the crossing code below implements:
 *
 *   - The Chain flag is set in every TRB of a TD except the last, and a Link
 *     TRB inside a TD is one of them - so CH goes on at a crossing the current
 *     TD continues past, and off at one that falls between two TDs. That test
 *     is the *preceding* TRB's own CH bit ("If the Chain bit (CH) of the
 *     previous TRB is `1`, then the multi-TRB TD that it defines spans segments
 *     and shall continue with the first TRB of the next segment", p.208) - not
 *     "are there more TRBs to write", which is the same thing only while a
 *     group is a single TD. The permanent Link TRB is reused by every lap, so
 *     this has to be *rewritten* at each crossing, not set once.
 *   - A Link TRB may be neither the first nor the last TRB of a multi-TRB TD.
 *     Both are structurally impossible here: the enqueue pointer never rests
 *     on the Link TRB (xhciRingNext skips it), and the last TRB written is
 *     always a caller TRB.
 *   - Consecutive Link TRBs within a TD are forbidden. Also impossible: one
 *     segment has one Link TRB, and a group can never be long enough to reach
 *     it twice because capacity is one less than the number of slots.
 *
 * On a Command Ring the xHC ignores the Link TRB's CH bit entirely, so the
 * same code is correct there without a special case.
 */
ULONG XhciRingEnqueueTdGroup(PXHCI_RING ring,
                             const XHCI_TRB *trbs,
                             ULONG count,
                             const ULONG *tdLengths,
                             ULONG tdCount,
                             PXHCI_TD_GROUP_PLACEMENT placement)
{
    volatile XHCI_TRB *slot;
    volatile XHCI_TRB *link;
    ULONG firstIndex;
    ULONG lastIndex;
    ULONG firstControl;
    ULONG index;
    ULONG cycle;
    ULONG control;
    ULONG td;
    ULONG i;
    ULONG n;

    if (ring == NULL || ring->Base == NULL || trbs == NULL || count == 0) {
        return XHCI_RING_BAD_PARAM;
    }
    if (tdLengths == NULL || tdCount == 0) {
        return XHCI_RING_BAD_PARAM;
    }
    /*
     * Spec 4.11.7: the Chain flag is set in every TRB of a TD except the last.
     * Enforced rather than assumed because the flags the caller supplies are
     * also the only record of where each TD ends - XhciRingTdBounds reads them
     * back off the ring. A TD whose last TRB is chained would make that walk
     * run into the next TD; a TD boundary the caller declared but did not mark
     * would make one TD out of two.
     */
    i = 0;
    for (td = 0; td < tdCount; td++) {
        if (tdLengths[td] == 0 || tdLengths[td] > count - i) {
            return XHCI_RING_BAD_CHAIN;
        }
        for (n = 0; n < tdLengths[td]; n++) {
            ULONG chained;

            chained = (trbs[i].Control & XHCI_TRB_CH) ? 1 : 0;
            if (chained != ((n + 1 < tdLengths[td]) ? 1UL : 0UL)) {
                return XHCI_RING_BAD_CHAIN;
            }
            i++;
        }
    }
    if (i != count) {
        return XHCI_RING_BAD_CHAIN;
    }
    /* The whole group or nothing: a partial TD on the ring is not something the
     * hardware can be told to ignore later
     * (docs/contributing/implementation-invariants.md, "Ring Full and Backpressure"). */
    if (!XhciRingHasRoom(ring, count)) {
        return XHCI_RING_FULL;
    }

    firstIndex = ring->Enqueue;
    lastIndex = firstIndex;
    index = firstIndex;
    cycle = ring->Cycle;

    firstControl = (trbs[0].Control & ~XHCI_TRB_CYCLE);
    if (cycle) {
        firstControl |= XHCI_TRB_CYCLE;
    }

    for (i = 0; i < count; i++) {
        slot = &ring->Base[index];

        slot->Param0 = trbs[i].Param0;
        slot->Param1 = trbs[i].Param1;
        slot->Status = trbs[i].Status;

        control = (trbs[i].Control & ~XHCI_TRB_CYCLE);
        if (cycle) {
            control |= XHCI_TRB_CYCLE;
        }
        if (i == 0) {
            /* Hold the head of the group invalid until the tail exists. */
            control = control ^ XHCI_TRB_CYCLE;
        }
        slot->Control = control;
        lastIndex = index;

        if (index + 1 == ring->Trbs - 1) {
            /*
             * Crossing the Link TRB. Handing it the cycle the hardware is
             * consuming is safe even mid-TD, because the hardware cannot
             * reach it: the head of this group is still invalid and sits in
             * front of it.
             */
            link = &ring->Base[ring->Trbs - 1];
            control = link->Control & ~(XHCI_TRB_CYCLE | XHCI_TRB_CH);
            if (cycle) {
                control |= XHCI_TRB_CYCLE;
            }
            if (trbs[i].Control & XHCI_TRB_CH) {
                control |= XHCI_TRB_CH;
            }
            link->Control = control;
            cycle = cycle ^ 1;
            index = 0;
        } else {
            index = index + 1;
        }
    }

    ring->Enqueue = index;
    ring->Cycle = cycle;

    if (placement != NULL) {
        placement->FirstIndex = firstIndex;
        placement->FirstTrbPA = ring->BasePA + firstIndex * XHCI_TRB_BYTES;
        placement->LastIndex = lastIndex;
        placement->TrbCount = count;
    }

    /* The publishing store. Nothing above this line is visible to the
     * hardware as work; nothing below it may be added to this group. */
    ring->Base[firstIndex].Control = firstControl;
    return XHCI_RING_OK;
}

/*
 * One whole Transfer Descriptor - the case every command and every non-control
 * transfer is. A single TD is a one-entry group, so it goes through the same
 * path rather than keeping a second copy of the wrap and cycle logic in step.
 */
ULONG XhciRingEnqueueTd(PXHCI_RING ring,
                        const XHCI_TRB *trbs,
                        ULONG count,
                        ULONG *firstTrbPA)
{
    XHCI_TD_GROUP_PLACEMENT placement;
    ULONG status;

    status = XhciRingEnqueueTdGroup(ring, trbs, count, &count, 1, &placement);
    if (status == XHCI_RING_OK && firstTrbPA != NULL) {
        *firstTrbPA = placement.FirstTrbPA;
    }
    return status;
}

/*
 * The single-TRB case - every command, and any transfer that fits in one TRB.
 * A one-TRB TD is still a TD, so it goes through the same path rather than
 * having a second copy of the wrap and cycle logic to keep in step.
 */
ULONG XhciRingEnqueue(PXHCI_RING ring, const XHCI_TRB *trb, ULONG *trbPA)
{
    return XhciRingEnqueueTd(ring, trb, 1, trbPA);
}

/*
 * Which TD owns the TRB at `index`? Answered from the Chain flags on the ring
 * rather than from a side table: the flags are already there, the hardware
 * reads the same encoding, and a parallel table is one more thing that can
 * fall out of step with the ring it describes. XhciRingEnqueueTd enforces the
 * spec 4.11.7 shape - chained in every TRB but the last - which is what makes
 * both walks terminate inside the TD.
 *
 * Backwards to the head: keep stepping while the *previous* slot is
 * outstanding and chained, i.e. while it says "the TD continues into me".
 * Forwards to the tail: keep stepping while this slot is chained. Both are
 * bounded by the number of outstanding TRBs, so a corrupt ring cannot spin.
 */
ULONG XhciRingTdBounds(const XHCI_RING *ring,
                       ULONG index,
                       ULONG *headIndex,
                       ULONG *tailIndex)
{
    ULONG head;
    ULONG tail;
    ULONG prev;
    ULONG steps;
    ULONG limit;

    if (ring == NULL || ring->Base == NULL) {
        return XHCI_RING_BAD_PARAM;
    }
    if (!xhciRingIsOutstanding(ring, index)) {
        return XHCI_RING_NOT_ON_RING;
    }

    limit = xhciRingUsed(ring);

    head = index;
    for (steps = 0; steps < limit; steps++) {
        if (head == ring->Dequeue) {
            break;
        }
        prev = (head == 0) ? (ring->Trbs - 2) : (head - 1);
        if ((ring->Base[prev].Control & XHCI_TRB_CH) == 0) {
            break;
        }
        head = prev;
    }

    tail = index;
    for (steps = 0; steps < limit; steps++) {
        if ((ring->Base[tail].Control & XHCI_TRB_CH) == 0) {
            break;
        }
        tail = xhciRingNext(ring, tail);
    }

    if (headIndex != NULL) {
        *headIndex = head;
    }
    if (tailIndex != NULL) {
        *tailIndex = tail;
    }
    return XHCI_RING_OK;
}

/*
 * Table 6-90 gives 24-25 only to Command Completion Events and 26-28 only to
 * Transfer Events. Keeping that split here prevents an event decoder defect
 * from handing ownership of the wrong ring to software.
 */
static ULONG xhciCodeIsStopped(ULONG kind, ULONG completionCode)
{
    if (kind == XHCI_RING_KIND_COMMAND) {
        return (completionCode == XHCI_CC_COMMAND_RING_STOPPED ||
                completionCode == XHCI_CC_COMMAND_ABORTED) ? 1 : 0;
    }
    return (completionCode >= XHCI_CC_STOPPED &&
            completionCode <= XHCI_CC_STOPPED_SHORT_PACKET) ? 1 : 0;
}

/*
 * This layer accepts only events that can identify one of its TDs. Ring
 * Underrun/Overrun explicitly carry no valid Transfer Event TRB pointer, while
 * Event Ring Full is a Host Controller Event. Letting any of them borrow an
 * aligned value would attach a controller-wide condition to an unrelated TD.
 */
static ULONG xhciCompletionCodeValid(ULONG kind, ULONG completionCode);

/* The one list, reachable from the transfer layer. See src/xhci.h. */
ULONG XhciRingCompletionCodeValid(ULONG kind, ULONG completionCode)
{
    return xhciCompletionCodeValid(kind, completionCode);
}

static ULONG xhciCompletionCodeValid(ULONG kind, ULONG completionCode)
{
    if (completionCode >= XHCI_CC_VENDOR_ERROR_MIN &&
        completionCode <= XHCI_CC_VENDOR_INFO_MAX) {
        return 1;
    }

    if (kind == XHCI_RING_KIND_COMMAND) {
        switch (completionCode) {
        case XHCI_CC_SUCCESS:
        case XHCI_CC_TRB_ERROR:
        case XHCI_CC_RESOURCE_ERROR:
        case XHCI_CC_BANDWIDTH_ERROR:
        case XHCI_CC_NO_SLOTS:
        case XHCI_CC_INVALID_STREAM_TYPE:
        case XHCI_CC_SLOT_NOT_ENABLED:
        case XHCI_CC_USB_TRANSACTION_ERROR:
        case XHCI_CC_VF_EVENT_RING_FULL:
        case XHCI_CC_PARAMETER_ERROR:
        case XHCI_CC_CONTEXT_STATE_ERROR:
        case XHCI_CC_INCOMPATIBLE_DEVICE:
        case XHCI_CC_COMMAND_RING_STOPPED:
        case XHCI_CC_COMMAND_ABORTED:
        case XHCI_CC_MAX_EXIT_LATENCY:
        case XHCI_CC_UNDEFINED_ERROR:
        case XHCI_CC_SECONDARY_BANDWIDTH:
            return 1;
        default:
            return 0;
        }
    }

    if (kind == XHCI_RING_KIND_ENDPOINT) {
        switch (completionCode) {
        case XHCI_CC_SUCCESS:
        case XHCI_CC_DATA_BUFFER_ERROR:
        case XHCI_CC_BABBLE:
        case XHCI_CC_USB_TRANSACTION_ERROR:
        case XHCI_CC_TRB_ERROR:
        case XHCI_CC_STALL:
        case XHCI_CC_SHORT_PACKET:
        case XHCI_CC_NO_PING_RESPONSE:
        case XHCI_CC_INCOMPATIBLE_DEVICE:
        case XHCI_CC_STOPPED:
        case XHCI_CC_STOPPED_LENGTH_INVALID:
        case XHCI_CC_STOPPED_SHORT_PACKET:
        case XHCI_CC_EVENT_LOST:
        case XHCI_CC_UNDEFINED_ERROR:
        case XHCI_CC_INVALID_STREAM_ID:
        case XHCI_CC_SPLIT_TRANSACTION:
            return 1;
        default:
            return 0;
        }
    }

    switch (completionCode) {
    case XHCI_CC_SUCCESS:
    case XHCI_CC_DATA_BUFFER_ERROR:
    case XHCI_CC_BABBLE:
    case XHCI_CC_USB_TRANSACTION_ERROR:
    case XHCI_CC_TRB_ERROR:
    case XHCI_CC_SHORT_PACKET:
    case XHCI_CC_BANDWIDTH_OVERRUN:
    case XHCI_CC_NO_PING_RESPONSE:
    case XHCI_CC_INCOMPATIBLE_DEVICE:
    case XHCI_CC_MISSED_SERVICE:
    case XHCI_CC_STOPPED:
    case XHCI_CC_STOPPED_LENGTH_INVALID:
    case XHCI_CC_STOPPED_SHORT_PACKET:
    case XHCI_CC_ISOCH_BUFFER_OVERRUN:
    case XHCI_CC_EVENT_LOST:
    case XHCI_CC_UNDEFINED_ERROR:
    case XHCI_CC_SPLIT_TRANSACTION:
        return 1;
    default:
        return 0;
    }
}

/*
 * Codes after which the xHC carries on by itself rather than halting: it
 * "shall advance to the first TRB of the next TD or the Enqueue Pointer (i.e.
 * Cycle bit transition), whichever is encountered first" (p.172).
 *
 * This decides recovery, *not* ownership. Landing here mid-TD means only that
 * software has nothing to do - no reset, no Set TR Dequeue Pointer - because
 * the controller is already moving and the TD's tail event is still coming.
 * The TRBs stay outstanding until that event names the tail. p.188 states
 * the distinction directly: the xHC "may automatically advance to the next
 * TD", and even so, if the event "does not point to last TRB of the Isoch TD
 * ... software will have to wait until the next IOC flag is encountered by the
 * endpoint before it can reclaim" the TD.
 *
 * Missed Service is an isoch pipe resynchronizing, not an endpoint in trouble
 * - "the xHC is required to advance through a Transfer Ring until it is
 * resynchronized or the ring is exhausted" (p.201), generating one of these per
 * skipped TD (p.201). Treating it as recovery would have software reset an
 * endpoint and reprogram a dequeue pointer while the controller is still
 * advancing through the ring.
 */
static ULONG xhciCodeAdvancesRing(ULONG completionCode)
{
    return (completionCode == XHCI_CC_SUCCESS ||
            completionCode == XHCI_CC_SHORT_PACKET ||
            completionCode == XHCI_CC_MISSED_SERVICE ||
            (completionCode >= XHCI_CC_VENDOR_INFO_MIN &&
             completionCode <= XHCI_CC_VENDOR_INFO_MAX)) ? 1 : 0;
}

/*
 * Does this event leave something that will not run again until software acts?
 * Position is irrelevant here - a halted endpoint is halted whether the error
 * landed on the TD's last TRB or an earlier one - which is exactly why this is
 * computed separately from CanRetire.
 *
 *   - Codes 24-25 hand a command ring back to software; codes 26-28 do the
 *     same for a transfer ring.
 *   - Otherwise only an error can halt, and only on a ring that *has* an
 *     endpoint that halts: "All Transfer Ring error conditions force the state
 *     of the associated endpoint to Halted and require system software
 *     intervention to recover" (p.176). **Which state it lands in is not this
 *     layer's business and not always Halted** - 4.8.3 p.149 puts a TRB Error
 *     in the Error state instead, needing a Set TR Dequeue Pointer rather than a
 *     Reset Endpoint. What this flag says is only "software must act", which is
 *     true of both; the caller decides with what.
 *   - An isoch endpoint is the stated exception to that sentence, on the same
 *     page: it "never halts because there is no handshake to report a halt
 *     condition ... If an error is detected, the xHC shall continue to process
 *     the data associated with the next ESIT of the transfer", restated at
 *     4.10.2.8 p.184. Recovering one would reset and reposition a pipe the
 *     controller is still running.
 *   - **TRB Error is the exception to the exception.** What p.177 exempts an
 *     isoch endpoint from is the *Halted* state, and 4.8.3 p.149 does not put a
 *     TRB Error there in the first place: it "should cause a Running Endpoint to
 *     transition to the Error state", with no qualification by endpoint type,
 *     and only "a Set TR Dequeue Pointer Command shall be used to transition the
 *     endpoint to the Stopped state". Disabled, Running, Halted, Stopped and
 *     Error are separately encoded (Table 6-8), so an isoch endpoint that cannot
 *     halt can still be sitting in Error - and left there it runs nothing, which
 *     is the one isoch *error* software must act on. (The stopped family is
 *     handled above and reaches every transfer-ring kind; it is not an error.)
 *   - A command ring has no endpoint at all. A failed command is a result, not
 *     a fault: the ring carries on with the next command.
 */
static ULONG xhciEventNeedsRecovery(const XHCI_RING *ring, ULONG completionCode)
{
    if (xhciCodeIsStopped(ring->Kind, completionCode)) {
        return 1;
    }
    if (ring->Kind != XHCI_RING_KIND_ENDPOINT) {
        return (ring->Kind == XHCI_RING_KIND_ISOCH &&
                completionCode == XHCI_CC_TRB_ERROR) ? 1 : 0;
    }
    return xhciCodeAdvancesRing(completionCode) ? 0 : 1;
}

ULONG XhciRingClassifyEvent(const XHCI_RING *ring,
                            ULONG reportedTrbPA,
                            ULONG completionCode,
                            PXHCI_TD_COMPLETION completion)
{
    ULONG index;
    ULONG head;
    ULONG tail;
    ULONG status;

    if (ring == NULL || completion == NULL) {
        return XHCI_RING_BAD_PARAM;
    }
    if (ring->Kind > XHCI_RING_KIND_ISOCH) {
        return XHCI_RING_BAD_PARAM;
    }
    if (!xhciCompletionCodeValid(ring->Kind, completionCode)) {
        return XHCI_RING_BAD_COMPLETION;
    }
    /*
     * Everything that is not an outstanding TRB of this ring is rejected here:
     * a TRB pointer of zero (which the xHC uses for errors it cannot attribute
     * to a TRB, spec 4.11.3.1), an address from another ring, the Link TRB, an
     * Event Data cookie that happens to be 16-byte aligned, and the trailing
     * events a single TD can generate after it has already been retired.
     */
    status = XhciRingIndexFromPA(ring, reportedTrbPA, &index);
    if (status != XHCI_RING_OK) {
        return status;
    }
    status = XhciRingTdBounds(ring, index, &head, &tail);
    if (status != XHCI_RING_OK) {
        return status;
    }

    completion->ReportedIndex = index;
    completion->HeadIndex = head;
    completion->TailIndex = tail;
    completion->IsTail = (index == tail) ? 1 : 0;

    completion->NeedsRecovery = xhciEventNeedsRecovery(ring, completionCode);

    /*
     * Ownership is positional and nothing else: the event named the TD's last
     * TRB (p.214, p.175). Mid-TD the controller may still be walking this TD -
     * an intermediate IOC, a short packet it will skip past, an isoch TD it
     * missed, or an error it stopped on - and reclaiming the TRBs there frees
     * them out from under it. The tail event retires the TD instead, and if
     * this controller drops that event, the next TD's tail event sweeps this
     * one up with it.
     *
     * A stopped code for this ring is the one case where a tail event still
     * does not retire: software owns the ring and chooses where execution
     * resumes rather than letting this event imply a dequeue position.
     */
    completion->CanRetire =
        (completion->IsTail &&
         !xhciCodeIsStopped(ring->Kind, completionCode)) ? 1 : 0;

    return XHCI_RING_OK;
}

ULONG XhciRingRetireTd(PXHCI_RING ring, const XHCI_TD_COMPLETION *completion)
{
    if (ring == NULL || completion == NULL) {
        return XHCI_RING_BAD_PARAM;
    }
    if (!completion->CanRetire) {
        return XHCI_RING_NOT_COMPLETE;
    }
    if (!xhciRingIsOutstanding(ring, completion->TailIndex)) {
        /* The classification is stale - something else moved the dequeue
         * pointer since. Retiring on it would move it backwards. */
        return XHCI_RING_NOT_ON_RING;
    }

    /*
     * A jump to just past the TD's last TRB, not a walk of one TD. Anything
     * still outstanding *behind* this TD is reclaimed in the same store, which
     * is deliberate: those are TDs the controller has already advanced past
     * whose own tail events it never posted (p.187 - no Contiguous Frame ID
     * Capability, or an Event Ring full condition). Reaching an event for a
     * later TD is proof they are done. A caller with its own TD list must
     * complete every entry below the new dequeue index, not just this one.
     */
    ring->Dequeue = xhciRingNext(ring, completion->TailIndex);
    return XHCI_RING_OK;
}

/*
 * The other way a TD's TRBs come back: the caller knows the xHC has finished
 * with the TD even though the event did not name its last TRB.
 *
 * This exists as its own entry point rather than as a widening of `CanRetire`
 * because the knowledge is not the ring layer's. Whether a Short Packet Event
 * mid-TD ends the transfer depends on the TD's *shape* - a Normal TD is over,
 * a control transfer's Data Stage is not, because its Status Stage TD still has
 * to run - and this layer cannot tell them apart. The first attempt at the
 * receive fix put the decision in `XhciRingClassifyEvent` and `test_ring.c`'s
 * control vectors caught it.
 *
 * What licenses the retire is 4.11.5.2 p.210: on a short packet the xHC "shall
 * advance to the first TRB of the next TD or the Enqueue Pointer ... whichever
 * is encountered first", so by the time the event is visible the controller has
 * provably stopped executing this TD. The tail event 4.10.1.1.2 p.175 promises
 * would carry no new information, and QEMU's xHC does not send it at all - see
 * docs/usb-xhci-info/xhci-data-structures.md, "The withheld second Short Packet Event".
 *
 * Short Packet is therefore the only code accepted, and only on an endpoint
 * ring. Every other code that advances the ring by itself has a different
 * reason to wait: Missed Service resynchronizes an isoch pipe across ESITs
 * (p.200-201) and its skipped TDs are swept by the next tail event, and a
 * vendor-defined informational code says nothing about ownership at all.
 * Isochronous rings are excluded because they retire by **group** and not by
 * advanced TD - `XhciRingRetireIsoGroup` is their path, and this one accepts
 * `XHCI_RING_KIND_ENDPOINT` only. A mid-TD isoch short packet reaching here
 * would be a decision nobody has made, and refusing it costs an OrphanedGroups
 * reading rather than a silently reclaimed TRB.
 *
 * *(The exclusion was written as "nothing submits on one yet (Phase 9)" until
 * a later review. Phase 9 landed: isochronous endpoints take
 * `XHCI_RING_KIND_ISOCH`, `XhciXferBuildIso` fills them and
 * `XhciRingRetireIsoGroup` retires them. The exclusion itself did not change -
 * only the reason it is not "yet".)*
 */
ULONG XhciRingRetireAdvancedTd(PXHCI_RING ring,
                               const XHCI_TD_COMPLETION *completion,
                               ULONG completionCode)
{
    if (ring == NULL || completion == NULL) {
        return XHCI_RING_BAD_PARAM;
    }
    if (ring->Kind != XHCI_RING_KIND_ENDPOINT ||
        completionCode != XHCI_CC_SHORT_PACKET) {
        return XHCI_RING_NOT_COMPLETE;
    }
    if (!xhciRingIsOutstanding(ring, completion->TailIndex)) {
        /* Same staleness check as XhciRingRetireTd: something else has moved
         * the dequeue pointer since the classification. */
        return XHCI_RING_NOT_ON_RING;
    }

    ring->Dequeue = xhciRingNext(ring, completion->TailIndex);
    return XHCI_RING_OK;
}

/* See the contract in src/xhci.h. */
ULONG XhciRingRetireIsoGroup(PXHCI_RING ring, ULONG tailIndex)
{
    if (ring == NULL) {
        return XHCI_RING_BAD_PARAM;
    }
    if (ring->Kind != XHCI_RING_KIND_ISOCH) {
        return XHCI_RING_NOT_COMPLETE;
    }
    if (!xhciRingIsOutstanding(ring, tailIndex)) {
        return XHCI_RING_NOT_ON_RING;
    }

    ring->Dequeue = xhciRingNext(ring, tailIndex);
    return XHCI_RING_OK;
}

ULONG XhciRingSetDequeue(PXHCI_RING ring, ULONG dequeuePA)
{
    ULONG index;
    ULONG head;
    ULONG status;

    if (ring == NULL) {
        return XHCI_RING_BAD_PARAM;
    }
    status = XhciRingIndexFromPA(ring, dequeuePA, &index);
    if (status != XHCI_RING_OK) {
        return status;
    }
    /* The Link TRB is not a position execution can start at (spec 4.11.7: a
     * Link TRB may not be the first TRB of a TD). */
    if (index >= ring->Trbs - 1) {
        return XHCI_RING_NOT_ON_RING;
    }

    /*
     * Discarding everything still outstanding. This is also the only legal
     * target on an empty ring, where Enqueue == Dequeue already: any other
     * position there would turn stale TRBs from previous laps into
     * "outstanding" work that no event will ever retire.
     */
    if (index == ring->Enqueue) {
        ring->Dequeue = index;
        return XHCI_RING_OK;
    }

    /*
     * Otherwise the target must still be outstanding. Moving backwards into
     * reclaimed space would resurrect those slots as outstanding and shrink
     * the free count for good - the ring would eventually report full and stay
     * that way, with no error anywhere to say why.
     */
    if (!xhciRingIsOutstanding(ring, index)) {
        return XHCI_RING_NOT_ON_RING;
    }

    /* And it must be a TD boundary: "the xHC shall assume that the modified
     * Dequeue Pointer references the first TRB of a TD" (spec p.172). */
    status = XhciRingTdBounds(ring, index, &head, NULL);
    if (status != XHCI_RING_OK) {
        return status;
    }
    if (head != index) {
        return XHCI_RING_NOT_TD_HEAD;
    }

    ring->Dequeue = index;
    return XHCI_RING_OK;
}

ULONG XhciRingNoOpAt(PXHCI_RING ring, ULONG index)
{
    ULONG cycle;

    if (ring == NULL || ring->Base == NULL) {
        return XHCI_RING_BAD_PARAM;
    }
    /* The Link TRB is the ring's own structure. Overwriting it would strand the
     * segment's wrap, which no later command could repair. */
    if (index >= ring->Trbs - 1) {
        return XHCI_RING_NOT_ON_RING;
    }
    if (!xhciRingIsOutstanding(ring, index)) {
        /* Not this driver's to edit: either already reclaimed, or a slot the
         * producer has not written on this lap. */
        return XHCI_RING_NOT_ON_RING;
    }

    /*
     * The Cycle Bit is read back and re-written rather than recomputed. It is
     * the one field whose correct value depends on which lap this slot was
     * produced on, and the producer already put it there; deriving it here would
     * be a second opinion that can differ from the first.
     */
    cycle = ring->Base[index].Control & XHCI_TRB_CYCLE;

    ring->Base[index].Param0 = 0;
    ring->Base[index].Param1 = 0;
    ring->Base[index].Status = 0;
    /*
     * Written last, and it is the store that changes what the slot *is*. Chain
     * and IOC are absent by construction: a chained No Op would join the TD that
     * follows it, and an IOC would ask for an event for a transfer that no longer
     * exists.
     */
    ring->Base[index].Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NOOP) | cycle;
    return XHCI_RING_OK;
}

ULONG XhciRingDequeuePA(const XHCI_RING *ring)
{
    return ring->BasePA + ring->Dequeue * XHCI_TRB_BYTES;
}

/*
 * The CCS value the xHC holds for the TRB at the dequeue pointer - bit 0 of a
 * Set TR Dequeue Pointer command's pointer field (Table 6-67, p.455).
 *
 * Outstanding TRBs are [Dequeue, Enqueue), so the two pointers are separated by
 * exactly one Link crossing when Dequeue > Enqueue and by none otherwise; the
 * producer toggles Cycle on each crossing, so one comparison recovers the
 * consumer's lap. Deriving it beats storing it: there is no field to update on
 * the retire, set-dequeue and enqueue-wrap paths, so none of them can drift.
 *
 * Empty ring (Dequeue == Enqueue) falls out as the producer cycle, which is
 * also the right answer for a flush to the enqueue position - the next TRB
 * written there will carry exactly that.
 */
ULONG XhciRingDequeueCycle(const XHCI_RING *ring)
{
    if (ring->Dequeue > ring->Enqueue) {
        return ring->Cycle ^ 1UL;
    }
    return ring->Cycle;
}

ULONG XhciEventRingInit(PXHCI_EVENT_RING ring,
                        volatile XHCI_TRB *base,
                        ULONG basePA,
                        ULONG trbs)
{
    ULONG i;

    if (ring == NULL || base == NULL || basePA == 0) {
        return XHCI_RING_BAD_PARAM;
    }
    /* Spec Table 6-1 / 6.5: an event ring segment holds 16 to 4096 TRBs. */
    if (trbs < 16 || trbs > 4096) {
        return XHCI_RING_BAD_PARAM;
    }
    if (!XhciSegmentUsable(basePA, trbs, 64UL)) {
        return XHCI_RING_BAD_PARAM;
    }

    /*
     * Zeroing is what makes the first lap work: an all-zero TRB has Cycle = 0,
     * and the consumer starts with Ccs = 1, so every slot reads "not produced
     * yet" until the hardware writes it.
     */
    for (i = 0; i < trbs; i++) {
        base[i].Param0 = 0;
        base[i].Param1 = 0;
        base[i].Status = 0;
        base[i].Control = 0;
    }

    ring->Base = base;
    ring->BasePA = basePA;
    ring->Trbs = trbs;
    ring->Dequeue = 0;
    ring->Ccs = 1;
    return XHCI_RING_OK;
}

ULONG XhciEventRingPending(const XHCI_EVENT_RING *ring)
{
    ULONG cycle;

    cycle = ring->Base[ring->Dequeue].Control & XHCI_TRB_CYCLE;
    return (cycle == (ring->Ccs & 1UL)) ? 1 : 0;
}

ULONG XhciEventRingDequeue(PXHCI_EVENT_RING ring, XHCI_TRB *out)
{
    volatile XHCI_TRB *slot;
    ULONG control;

    if (ring == NULL || ring->Base == NULL || out == NULL) {
        return XHCI_RING_BAD_PARAM;
    }

    slot = &ring->Base[ring->Dequeue];
    /*
     * Read the Cycle Bit before the payload. The xHC writes the whole TRB and
     * PCI ordering puts the payload in memory first, so a matching cycle means
     * the other three DWORDs are already there.
     */
    control = slot->Control;
    if ((control & XHCI_TRB_CYCLE) != (ring->Ccs & 1UL)) {
        return XHCI_RING_EMPTY;
    }

    out->Param0 = slot->Param0;
    out->Param1 = slot->Param1;
    out->Status = slot->Status;
    out->Control = control;

    ring->Dequeue++;
    if (ring->Dequeue >= ring->Trbs) {
        ring->Dequeue = 0;
        ring->Ccs = ring->Ccs ^ 1;
    }
    return XHCI_RING_OK;
}

ULONG XhciEventRingDequeuePA(const XHCI_EVENT_RING *ring)
{
    return ring->BasePA + ring->Dequeue * XHCI_TRB_BYTES;
}

ULONG XhciEventRingErdpValue(const XHCI_EVENT_RING *ring, ULONG ehb)
{
    ULONG value;

    /* One segment, so DESI is always 0; the segment is 64-byte aligned and
     * TRBs are 16 bytes, so bits 3:0 of the address are already clear and the
     * EHB bit can simply be ORed in. */
    value = XhciEventRingDequeuePA(ring) & ~(XHCI_ERDP_DESI_MASK | XHCI_ERDP_EHB);
    if (ehb) {
        value |= XHCI_ERDP_EHB;
    }
    return value;
}
