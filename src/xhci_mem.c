/*
 * xhci_mem.c - controller common-buffer layout, computed and checked.
 *
 * Pure arithmetic on caller-supplied values: no MMIO, no DDK calls, no IRQL
 * dependencies, so it builds and runs on the host under XHCI_HOST_TEST
 * (docs/contributing/design/03-host-unit-tests.md). test/test_membuf.c is the
 * regression suite; the derivation is
 * docs/contributing/design/04-controller-common-buffer.md.
 *
 * The whole point of this file is that the resource-size constant committed
 * in DriverEntry, the offsets carved at StartController, and the spec's
 * alignment / no-cross-boundary rules are checked against each other before
 * any of it reaches a controller.
 *
 * **Pointer contract, because this file has two conventions in it and the
 * difference is deliberate rather than an omission.** `XhciComputeLayout` and
 * every offset accessor take `layout` and `offset` as *trusted* pointers and do
 * not check them: they are internal arithmetic over a structure the caller owns
 * and has just carved, every call site passes `&ext->Layout` and the address of
 * a local, and a NULL there is a driver bug that a status return would hide
 * from the host suite rather than surface. The pooled-ring allocator
 * (`XhciPoolInit` and below) refuses NULL instead, because its `pool` argument
 * is reached from lifecycle paths that legitimately run with no controller
 * state - a teardown after a failed start among them. New code added here
 * should follow whichever of the two it is closer to, and say which.
 *
 * C89 only. IRQL: every function is pure - callable at any IRQL.
 */

#include "xhci.h"

/*
 * Does an object at byte offset `offset` of size `size` span a `boundary`
 * boundary? Boundaries are powers of two; offsets are relative to a base that
 * XhciCheckResourceBase has proven page-aligned in both address spaces.
 *
 * Soundness precondition, and the reason xhci.h caps every ring segment at one
 * page: this answers the question *relative to the base*, but the spec asks it
 * of the physical address, and usbport guarantees the base is only
 * XHCI_PAGE_SIZE-aligned - not 64 KB-aligned. The two questions coincide only
 * while every object fits inside one page, since a page-aligned base plus a
 * within-page object cannot straddle any larger boundary either. Grow an
 * object past a page and this function silently starts answering the wrong
 * question: an 8 KB EP0 ring at a 8 KB-aligned *offset* passes here and can
 * still straddle a 64 KB physical boundary. Anything larger needs a
 * 64 KB-aligned region or a check against StartPA + offset.
 */
static ULONG xhciCrossesBoundary(ULONG offset, ULONG size, ULONG boundary)
{
    if (size == 0) {
        return 0;
    }
    return ((offset / boundary) != ((offset + size - 1) / boundary)) ? 1 : 0;
}

static ULONG xhciIsAligned(ULONG offset, ULONG alignment)
{
    return ((offset & (alignment - 1)) == 0) ? 1 : 0;
}

ULONG XhciComputeLayout(ULONG contextSize,
                        ULONG hwMaxSlots,
                        ULONG scratchpadCount,
                        ULONG hcPageSizeMask,
                        PXHCI_HC_LAYOUT layout)
{
    ULONG maxSlotsEn;
    ULONG i;

    if (contextSize != XHCI_CONTEXT_SIZE_SMALL &&
        contextSize != XHCI_CONTEXT_SIZE_LARGE) {
        return XHCI_LAYOUT_BAD_CONTEXT_SIZE;
    }

    /*
     * Scratchpad buffers are one selected xHC page each and the spec's
     * no-cross-boundary rules are stated in that size. PAGESIZE is a
     * capability bitmap, not a byte count: select 4096 whenever bit 0 is
     * advertised, even if the controller advertises larger sizes too.
     */
    if ((hcPageSizeMask & 1UL) == 0) {
        return XHCI_LAYOUT_BAD_PAGE_SIZE;
    }

    if (hwMaxSlots == 0) {
        return XHCI_LAYOUT_NO_SLOTS;
    }

    if (scratchpadCount > XHCI_MAX_SCRATCHPAD) {
        return XHCI_LAYOUT_TOO_MANY_SCRATCHPAD;
    }

    /*
     * Clamping rather than refusing is deliberate: MaxSlots is the number of
     * slots the hardware *offers*, and using fewer than offered is legal
     * (CONFIG.MaxSlotsEn). Scratchpad demand is the opposite - the controller
     * dictates it - which is why that one is a refusal above.
     */
    maxSlotsEn = (hwMaxSlots > XHCI_MAX_SLOTS) ? XHCI_MAX_SLOTS : hwMaxSlots;

    layout->ContextSize = contextSize;
    layout->MaxSlotsEn = maxSlotsEn;
    layout->ScratchpadCount = scratchpadCount;

    layout->DcbaaOffset = XHCI_DCBAA_OFFSET;
    layout->DcbaaBytes = (maxSlotsEn + 1) * XHCI_DCBAA_ENTRY_BYTES;

    layout->ScratchpadArrayOffset = XHCI_SPA_OFFSET;
    layout->ScratchpadArrayBytes = scratchpadCount * XHCI_SCRATCHPAD_ENTRY_BYTES;

    layout->CommandRingOffset = XHCI_CMD_RING_OFFSET;
    layout->CommandRingTrbs = XHCI_CMD_RING_TRBS;

    layout->ErstOffset = XHCI_ERST_OFFSET;
    layout->ErstEntries = 1;

    layout->EventRingOffset = XHCI_REGION_EVENT_RING;
    layout->EventRingTrbs = XHCI_EVENT_RING_TRBS;

    layout->InputContextOffset = XHCI_REGION_INPUT_CONTEXT;
    layout->InputContextBytes = XHCI_INPUT_CONTEXT_ENTRIES * contextSize;

    layout->DeviceContextOffset = XHCI_REGION_DEVICE_CONTEXTS;
    layout->DeviceContextStride = XHCI_DEVICE_CONTEXT_STRIDE;
    layout->DeviceContextBytes = XHCI_DEVICE_CONTEXT_ENTRIES * contextSize;

    layout->Ep0RingOffset = XHCI_REGION_EP0_RINGS;
    layout->Ep0RingStride = XHCI_EP0_RING_STRIDE;
    layout->Ep0RingTrbs = XHCI_EP0_RING_TRBS;

    layout->PoolRingOffset = XHCI_REGION_POOL_RINGS;
    layout->PoolRingStride = XHCI_POOL_RING_STRIDE;
    layout->PoolRingTrbs = XHCI_POOL_RING_TRBS;
    /* Not clamped to MaxSlotsEn: the pool is not per-slot, and a controller
     * offering fewer slots than the declared limit does not make a pool ring
     * unusable - it only reduces how many devices can compete for one. */
    layout->PoolRingCount = XHCI_MAX_POOL_RINGS;

    layout->ScratchpadPageOffset = XHCI_REGION_SCRATCHPAD_PAGES;
    layout->ScratchpadPageStride = XHCI_PAGE_SIZE;

    layout->TotalBytes = XHCI_HC_RESOURCES_SIZE;

    /*
     * Everything above is derived from compile-time constants, so these are
     * belt-and-braces against a future edit to one constant that quietly
     * breaks another region. They are cheap and they run once per start.
     */
    if (layout->DcbaaBytes > XHCI_DCBAA_RESERVED ||
        layout->ScratchpadArrayBytes > XHCI_SPA_RESERVED ||
        layout->InputContextBytes > XHCI_INPUT_CONTEXT_RESERVED ||
        layout->DeviceContextBytes > XHCI_DEVICE_CONTEXT_STRIDE) {
        return XHCI_LAYOUT_OVERFLOW;
    }

    if (!xhciIsAligned(layout->DcbaaOffset, 64) ||
        !xhciIsAligned(layout->ScratchpadArrayOffset, 64) ||
        !xhciIsAligned(layout->CommandRingOffset, 64) ||
        !xhciIsAligned(layout->ErstOffset, 64) ||
        !xhciIsAligned(layout->EventRingOffset, 64) ||
        !xhciIsAligned(layout->InputContextOffset, 64)) {
        return XHCI_LAYOUT_OVERFLOW;
    }

    if (xhciCrossesBoundary(layout->DcbaaOffset, layout->DcbaaBytes,
                            XHCI_PAGE_SIZE) ||
        xhciCrossesBoundary(layout->ScratchpadArrayOffset,
                            layout->ScratchpadArrayBytes, XHCI_PAGE_SIZE) ||
        xhciCrossesBoundary(layout->InputContextOffset,
                            layout->InputContextBytes, XHCI_PAGE_SIZE) ||
        xhciCrossesBoundary(layout->CommandRingOffset,
                            XHCI_CMD_RING_BYTES, 65536UL) ||
        xhciCrossesBoundary(layout->EventRingOffset,
                            XHCI_EVENT_RING_BYTES, 65536UL)) {
        return XHCI_LAYOUT_OVERFLOW;
    }

    if (layout->DcbaaOffset + XHCI_DCBAA_RESERVED >
            layout->ScratchpadArrayOffset ||
        layout->ScratchpadArrayOffset + XHCI_SPA_RESERVED >
            layout->CommandRingOffset ||
        layout->CommandRingOffset + XHCI_CMD_RING_BYTES >
            layout->ErstOffset ||
        layout->ErstOffset + XHCI_ERST_RESERVED >
            layout->EventRingOffset ||
        layout->EventRingOffset + XHCI_EVENT_RING_BYTES >
            layout->InputContextOffset ||
        layout->InputContextOffset + XHCI_INPUT_CONTEXT_RESERVED >
            layout->DeviceContextOffset ||
        layout->DeviceContextOffset + XHCI_REGION_DEVICE_CONTEXTS_BYTES >
            layout->Ep0RingOffset ||
        layout->Ep0RingOffset + XHCI_REGION_EP0_RINGS_BYTES >
            layout->PoolRingOffset ||
        layout->PoolRingOffset + XHCI_REGION_POOL_RINGS_BYTES >
            layout->ScratchpadPageOffset ||
        layout->ScratchpadPageOffset +
            XHCI_REGION_SCRATCHPAD_PAGES_BYTES > layout->TotalBytes) {
        return XHCI_LAYOUT_OVERFLOW;
    }

    for (i = 0; i < maxSlotsEn; i++) {
        ULONG dc = layout->DeviceContextOffset + i * layout->DeviceContextStride;
        ULONG ep0 = layout->Ep0RingOffset + i * layout->Ep0RingStride;

        if (!xhciIsAligned(dc, 64) || !xhciIsAligned(ep0, 64)) {
            return XHCI_LAYOUT_OVERFLOW;
        }
        if (xhciCrossesBoundary(dc, layout->DeviceContextBytes,
                                XHCI_PAGE_SIZE) ||
            xhciCrossesBoundary(ep0, layout->Ep0RingStride, 65536UL)) {
            return XHCI_LAYOUT_OVERFLOW;
        }
        if (dc + layout->DeviceContextStride >
            XHCI_REGION_DEVICE_CONTEXTS + XHCI_REGION_DEVICE_CONTEXTS_BYTES) {
            return XHCI_LAYOUT_OVERFLOW;
        }
        if (ep0 + layout->Ep0RingStride >
            XHCI_REGION_EP0_RINGS + XHCI_REGION_EP0_RINGS_BYTES) {
            return XHCI_LAYOUT_OVERFLOW;
        }
    }

    /*
     * Every pool entry, not maxSlotsEn of them: the pool is indexed by pool slot
     * and a controller with fewer hardware slots still exposes the whole region,
     * so checking only the first maxSlotsEn would leave the tail unproven on
     * exactly the controllers where it is most likely to be reached.
     */
    for (i = 0; i < layout->PoolRingCount; i++) {
        ULONG pr = layout->PoolRingOffset + i * layout->PoolRingStride;

        if (!xhciIsAligned(pr, 64) ||
            xhciCrossesBoundary(pr, layout->PoolRingStride, 65536UL)) {
            return XHCI_LAYOUT_OVERFLOW;
        }
        if (pr + layout->PoolRingStride >
            XHCI_REGION_POOL_RINGS + XHCI_REGION_POOL_RINGS_BYTES) {
            return XHCI_LAYOUT_OVERFLOW;
        }
    }

    for (i = 0; i < scratchpadCount; i++) {
        ULONG sp = layout->ScratchpadPageOffset + i * layout->ScratchpadPageStride;

        if (!xhciIsAligned(sp, XHCI_PAGE_SIZE)) {
            return XHCI_LAYOUT_OVERFLOW;
        }
        if (sp + XHCI_PAGE_SIZE > layout->TotalBytes) {
            return XHCI_LAYOUT_OVERFLOW;
        }
    }

    return XHCI_LAYOUT_OK;
}

ULONG XhciDeviceContextOffset(const XHCI_HC_LAYOUT *layout,
                              ULONG slotId,
                              ULONG *offset)
{
    /* Slot IDs are 1-based (spec 4.5.3); slot i uses array element i - 1. */
    if (slotId == 0 || slotId > layout->MaxSlotsEn) {
        return XHCI_LAYOUT_BAD_INDEX;
    }
    *offset = layout->DeviceContextOffset +
              (slotId - 1) * layout->DeviceContextStride;
    return XHCI_LAYOUT_OK;
}

ULONG XhciEp0RingOffset(const XHCI_HC_LAYOUT *layout,
                        ULONG slotId,
                        ULONG *offset)
{
    if (slotId == 0 || slotId > layout->MaxSlotsEn) {
        return XHCI_LAYOUT_BAD_INDEX;
    }
    *offset = layout->Ep0RingOffset + (slotId - 1) * layout->Ep0RingStride;
    return XHCI_LAYOUT_OK;
}

ULONG XhciPoolRingOffset(const XHCI_HC_LAYOUT *layout,
                         ULONG poolIndex,
                         ULONG *offset)
{
    /* 0-based, unlike the two above: a pool slot is not a Slot ID and the
     * off-by-one that would follow from treating it as one puts the last
     * device's ring past the end of the region. */
    if (poolIndex >= layout->PoolRingCount) {
        return XHCI_LAYOUT_BAD_INDEX;
    }
    *offset = layout->PoolRingOffset + poolIndex * layout->PoolRingStride;
    return XHCI_LAYOUT_OK;
}

/* ------------------------------------------------------------------ */
/* The pooled-ring allocator (batch 7a-A, design doc 04 section 3.6)   */
/* ------------------------------------------------------------------ */

VOID XhciPoolInit(PXHCI_RING_POOL pool)
{
    ULONG i;

    if (pool == NULL) {
        return;
    }
    for (i = 0; i < XHCI_MAX_POOL_RINGS; i++) {
        pool->Owner[i] = 0;
    }
    pool->InUse = 0;
    pool->AcquireFailuresEmpty = 0;
    pool->AcquireFailuresFairness = 0;
    pool->AcquireFailuresCap = 0;
    pool->PeakInUse = 0;
#ifdef XHCI_FIX_NO_RING_REUSE
    /* Candidate W2's rotation cursor. Reset here rather than left to the
     * extension's initial zeroing, because this function also runs on the
     * reinitialisation path, where every ring has just been reclaimed and the
     * rotation should start from a known index rather than from wherever the
     * previous generation stopped. */
    pool->NextScan = 0;
#endif
}

ULONG XhciPoolDeviceCount(const XHCI_RING_POOL *pool, ULONG deviceRef)
{
    ULONG i;
    ULONG held;

    if (pool == NULL || deviceRef == 0) {
        return 0;
    }
    held = 0;
    for (i = 0; i < XHCI_MAX_POOL_RINGS; i++) {
        if (pool->Owner[i] == deviceRef) {
            held++;
        }
    }
    return held;
}

ULONG XhciPoolFree(const XHCI_RING_POOL *pool)
{
    ULONG i;
    ULONG used;

    if (pool == NULL) {
        return 0;
    }
    /*
     * Counted from `Owner[]` rather than returned from `InUse`, because those
     * are two different sources and a disagreement between them must not become
     * an allocation decision. `InUse` is a running total and is the thing that
     * can drift; `Owner[]` is what the acquire loop actually searches, so
     * answering from it makes "free says there is one" and "the scan finds one"
     * incapable of contradicting each other. `InUse` survives as a diagnostic.
     */
    used = 0;
    for (i = 0; i < XHCI_MAX_POOL_RINGS; i++) {
        if (pool->Owner[i] != 0) {
            used++;
        }
    }
    return XHCI_MAX_POOL_RINGS - used;
}

ULONG XhciPoolAcquire(PXHCI_RING_POOL pool,
                      ULONG deviceRef,
                      ULONG ringless,
                      ULONG *poolIndex)
{
    ULONG held;
    ULONG freeRings;
    ULONG i;

    if (pool == NULL || deviceRef == 0 || poolIndex == NULL) {
        return XHCI_POOL_BAD_PARAM;
    }

    held = XhciPoolDeviceCount(pool, deviceRef);
    if (held >= XHCI_MAX_DEVICE_ENDPOINTS) {
        pool->AcquireFailuresCap++;
        return XHCI_POOL_AT_CAP;
    }

    freeRings = XhciPoolFree(pool);
    if (freeRings == 0) {
        pool->AcquireFailuresEmpty++;
        return XHCI_POOL_EMPTY;
    }

    /*
     * The admission rule, and it is the whole reason "no device is starved of
     * its first endpoint" is true - the pool *size* does not establish that and
     * an earlier draft of design doc 04 section 3.6 wrongly said it did. With a
     * per-device cap of 4, eight devices can hold all 32 rings and the ninth
     * gets its first endpoint refused; sizing the guarantee instead would need
     * 31 * 4 + 1 = 125 rings.
     *
     * So a *first* ring is always granted while one is free, and a later ring
     * is granted only if the invariant `free >= ringless` survives it. A
     * first-ring grant moves both sides down by one; a later-ring grant is
     * refused unless there is slack. `ringless` counts the *other* live records
     * holding nothing, which only the caller can know.
     */
    if (held > 0 && freeRings - 1 < ringless) {
        pool->AcquireFailuresFairness++;
        return XHCI_POOL_UNFAIR;
    }

#ifdef XHCI_FIX_NO_RING_REUSE
    /*
     * **EXPERIMENTAL, bench candidate W2 for Finding 3** (HANDOFF.md open item
     * 1). Built only under the define; the scan below it is the shipping one.
     *
     * The shipping scan is **first-fit from zero**, so the ring index a device
     * releases at teardown is the one handed to the very next device that asks.
     * If the wedge is a ring reused while the controller is still reading it -
     * because a Stop Endpoint was never shown to have worked - then that
     * immediate handover is the moment it happens, and this rotation is the
     * cheapest possible test of it: it changes no lifetime, frees nothing late,
     * leaks nothing, and only moves *which* free index is chosen.
     *
     * `NextScan` advances past every grant, so a freed index is not revisited
     * until the rotation wraps. Thirty-two rings against a five-plug recipe
     * taking two or three apiece is roughly ten grants, so the bench test never
     * wraps and the separation is total for the duration of the recipe.
     *
     * This is a DIAGNOSTIC, not a proposed fix. Wrapping still reuses, so it
     * narrows the window rather than closing it; what it buys is a clean answer
     * to "is immediate ring reuse the mechanism at all", which no reasoning
     * about the quiesce path can give.
     */
    for (i = 0; i < XHCI_MAX_POOL_RINGS; i++) {
        ULONG slot;

        slot = pool->NextScan + i;
        if (slot >= XHCI_MAX_POOL_RINGS) {
            slot -= XHCI_MAX_POOL_RINGS;
        }
        if (pool->Owner[slot] == 0) {
            pool->Owner[slot] = deviceRef;
            pool->InUse++;
            if (pool->InUse > pool->PeakInUse) {
                pool->PeakInUse = pool->InUse;
            }
            pool->NextScan = slot + 1;
            if (pool->NextScan >= XHCI_MAX_POOL_RINGS) {
                pool->NextScan = 0;
            }
            *poolIndex = slot;
            return XHCI_POOL_OK;
        }
    }
#else
    for (i = 0; i < XHCI_MAX_POOL_RINGS; i++) {
        if (pool->Owner[i] == 0) {
            pool->Owner[i] = deviceRef;
            pool->InUse++;
            if (pool->InUse > pool->PeakInUse) {
                pool->PeakInUse = pool->InUse;
            }
            *poolIndex = i;
            return XHCI_POOL_OK;
        }
    }
#endif

    /*
     * Unreachable now that XhciPoolFree counts `Owner[]` directly - a nonzero
     * free count and a scan that finds nothing cannot both come from the same
     * array. Kept as a refusal rather than deleted because "unreachable" is a
     * claim about today's code, and the alternative failure (handing out a ring
     * the array says is taken) is the one this file must never commit.
     */
    pool->AcquireFailuresEmpty++;
    return XHCI_POOL_EMPTY;
}

ULONG XhciPoolRelease(PXHCI_RING_POOL pool, ULONG deviceRef, ULONG poolIndex)
{
    if (pool == NULL || deviceRef == 0 || poolIndex >= XHCI_MAX_POOL_RINGS) {
        return XHCI_POOL_BAD_PARAM;
    }
    /* Refusing a free or wrongly-owned index rather than clearing it: both mean
     * this driver's record of who holds what is wrong, and the repair would be
     * to hand a live ring to a second endpoint. */
    if (pool->Owner[poolIndex] != deviceRef) {
        return XHCI_POOL_BAD_PARAM;
    }
    pool->Owner[poolIndex] = 0;
    /* Guarded because InUse is a running total and these are the only places it
     * goes down: an underflow would wrap to ~4 billion and turn a one-entry
     * bookkeeping slip into a pool reporting itself impossibly
     * over-subscribed. */
    if (pool->InUse > 0) {
        pool->InUse--;
    }
    return XHCI_POOL_OK;
}

ULONG XhciPoolReleaseDevice(PXHCI_RING_POOL pool, ULONG deviceRef)
{
    ULONG i;
    ULONG released;

    if (pool == NULL || deviceRef == 0) {
        return 0;
    }
    released = 0;
    for (i = 0; i < XHCI_MAX_POOL_RINGS; i++) {
        if (pool->Owner[i] == deviceRef) {
            pool->Owner[i] = 0;
            if (pool->InUse > 0) {
                pool->InUse--;
            }
            released++;
        }
    }
    return released;
}

ULONG XhciScratchpadPageOffset(const XHCI_HC_LAYOUT *layout,
                               ULONG index,
                               ULONG *offset)
{
    if (index >= layout->ScratchpadCount) {
        return XHCI_LAYOUT_BAD_INDEX;
    }
    *offset = layout->ScratchpadPageOffset + index * layout->ScratchpadPageStride;
    return XHCI_LAYOUT_OK;
}

/*
 * Context strides. Byte offset of context index i is i * ContextSize, where
 * ContextSize is 32 or 64 from HCCPARAMS1.CSZ - never a hardcoded 32
 * (docs/usb-xhci-info/xhci-data-structures.md section 8). The two context flavours index
 * differently and that off-by-one is the whole reason these are functions:
 * in a Device Context index 0 is the Slot Context and index i is DCI i, while
 * an Input Context inserts the Input Control Context at 0 and shifts
 * everything down by one.
 */
ULONG XhciSlotContextOffset(const XHCI_HC_LAYOUT *layout,
                            ULONG slotId,
                            ULONG *offset)
{
    /* Index 0 of the device context - the base itself. */
    return XhciDeviceContextOffset(layout, slotId, offset);
}

ULONG XhciEndpointContextOffset(const XHCI_HC_LAYOUT *layout,
                                ULONG slotId,
                                ULONG dci,
                                ULONG *offset)
{
    ULONG base;
    ULONG status;

    /* DCI 0 is the Slot Context, not an endpoint; DCI 31 is the last one
     * a Device Context can describe (spec 4.5.1). */
    if (dci == 0 || dci > XHCI_MAX_DCI) {
        return XHCI_LAYOUT_BAD_INDEX;
    }
    status = XhciDeviceContextOffset(layout, slotId, &base);
    if (status != XHCI_LAYOUT_OK) {
        return status;
    }
    *offset = base + dci * layout->ContextSize;
    return XHCI_LAYOUT_OK;
}

ULONG XhciInputControlContextOffset(const XHCI_HC_LAYOUT *layout,
                                    ULONG *offset)
{
    *offset = layout->InputContextOffset;
    return XHCI_LAYOUT_OK;
}

ULONG XhciInputSlotContextOffset(const XHCI_HC_LAYOUT *layout,
                                 ULONG *offset)
{
    *offset = layout->InputContextOffset + layout->ContextSize;
    return XHCI_LAYOUT_OK;
}

ULONG XhciInputEndpointContextOffset(const XHCI_HC_LAYOUT *layout,
                                     ULONG dci,
                                     ULONG *offset)
{
    if (dci == 0 || dci > XHCI_MAX_DCI) {
        return XHCI_LAYOUT_BAD_INDEX;
    }
    *offset = layout->InputContextOffset + (dci + 1) * layout->ContextSize;
    return XHCI_LAYOUT_OK;
}

ULONG XhciCheckResourceBase(ULONG_PTR startVA, ULONG startPA)
{
    /*
     * Both shipping usbport builds mask BaseVA and LogicalAddress.LowPart with
     * ~(PAGE_SIZE - 1) before publishing StartVA/StartPA, so both are page
     * aligned. Every offset above inherits its alignment and its
     * no-cross-boundary property from that, in physical space as well as
     * virtual - which is the only reason a single offset table can serve both.
     * Verify it instead of assuming it: a StartPA that is merely 64-byte
     * aligned would silently break the device-context page rule.
     */
    if (startVA == 0 || startPA == 0) {
        return XHCI_LAYOUT_UNALIGNED_BASE;
    }
    if ((startVA & (ULONG_PTR)(XHCI_PAGE_SIZE - 1)) != 0) {
        return XHCI_LAYOUT_UNALIGNED_BASE;
    }
    if ((startPA & (XHCI_PAGE_SIZE - 1)) != 0) {
        return XHCI_LAYOUT_UNALIGNED_BASE;
    }
    /* No 64-bit DMA: a 32-bit StartPA cannot overflow, but the block must
     * still fit below 4 GB end-to-end. */
    if (startPA >
        (0xFFFFFFFFUL - (XHCI_HC_RESOURCES_SIZE - 1UL))) {
        return XHCI_LAYOUT_OVERFLOW;
    }
    return XHCI_LAYOUT_OK;
}

ULONG XhciCommonBufferAllocationBytes(ULONG resourcesSize)
{
    ULONG want;

    if (resourcesSize == 0) {
        return 0;
    }
    /* usbport: ROUND_TO_PAGES(BufferLength + sizeof(header)). */
    if (resourcesSize > (0xFFFFFFFFUL - XHCI_USBPORT_CB_HEADER_BYTES -
                         (XHCI_PAGE_SIZE - 1))) {
        return 0;
    }
    want = resourcesSize + XHCI_USBPORT_CB_HEADER_BYTES;
    return (want + (XHCI_PAGE_SIZE - 1)) & ~(XHCI_PAGE_SIZE - 1);
}
