/*
 * xhci_ctx.c - Slot, Endpoint and Input Control Context encoding.
 *
 * Pure computation plus stores into caller-supplied common-buffer memory: no
 * MMIO, no DDK calls, no IRQL dependencies, so it builds and runs on the host
 * under XHCI_HOST_TEST (docs/contributing/design/03-host-unit-tests.md).
 * test/test_ctx.c is the regression suite. Bit positions come from
 * docs/usb-xhci-info/xhci-data-structures.md section 8, which was transcribed and verified
 * against the local spec PDF - never from memory.
 *
 * Two properties hold throughout, and both are the reason this is a file of its
 * own rather than a handful of writes inside src/xhci_slot.c:
 *
 *   - **A value that does not fit its field is refused, not masked.** Every
 *     argument here is this driver's own bookkeeping about a device. A masked
 *     Max Packet Size or a truncated Route String is a plausible number
 *     programmed into hardware, and the failure it produces - a device that
 *     enumerates and then misbehaves - is a long way from its cause.
 *   - **Exactly XHCI_CONTEXT_DWORDS words are written**, whatever the context
 *     stride is. With CSZ = 1 the upper 32 bytes of every context are reserved;
 *     writing them because the stride says 64 would be a store into RsvdZ space
 *     that no part of the specification asks for. Where the *whole* block has to
 *     start clean - the Input Context, which has RsvdZ padding throughout
 *     (section 8) - the caller zeroes it with XhciContextZero at the stride, and
 *     that is a deliberate, separate act.
 *
 * C89 only. IRQL: every function is callable at any IRQL.
 */

#include "xhci.h"
/*
 * For USBPORT_TRANSFER_TYPE_* only. This file stays DDK-free (design doc 03's
 * pure-core rule) and xhci_usbport.h is DDK-free too, so the rule holds - but
 * the transfer type genuinely is a *usbport* value, and translating it here
 * rather than at the call site keeps the one mapping from usbport's endpoint
 * vocabulary to the Endpoint Context's in a single place.
 */
#include "xhci_usbport.h"

/*
 * The one place a context is written to memory. Contexts live in cached
 * common-buffer memory, so the pointer is volatile and ordering rests on the
 * compiler not reordering the stores - none of which is published by this file:
 * the command TRB that names the Input Context is the publication, and it is
 * written afterwards (docs/contributing/design/04-controller-common-buffer.md section 6).
 */
static VOID xhciCtxStore(volatile ULONG *context, ULONG index, ULONG value)
{
    context[index] = value;
}

VOID XhciContextZero(volatile ULONG *context, ULONG dwords)
{
    ULONG i;

    if (context == NULL) {
        return;
    }
    for (i = 0; i < dwords; i++) {
        context[i] = 0;
    }
}

ULONG XhciBuildInputControlContext(volatile ULONG *context,
                                   ULONG addFlags,
                                   ULONG dropFlags)
{
    ULONG i;

    if (context == NULL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /*
     * "Drop Context flags ... bits 1:0 are RsvdZ" (Table 6-15). D0 and D1 do not
     * exist because the Slot Context and the Default Control Endpoint cannot be
     * dropped - only Disable Slot removes them - so a caller asking for one is
     * asking for something that has no encoding, and writing the bits anyway
     * would set RsvdZ.
     */
    if ((dropFlags & XHCI_ICC_DROP_RSVDZ_MASK) != 0) {
        return XHCI_CTX_BAD_PARAM;
    }
    /*
     * **Drop and Add set for the same endpoint is legal, and this builder used
     * to refuse it.** The refusal was invented - its own comment admitted the
     * rule "is not spelled out that way" - and spec 4.6.6 spells out the
     * opposite, twice. p.106 gives the combination its own case in the same list
     * as the other three: "If the Drop Context flag is '1' and the Add Context
     * flag is '1', the xHC shall: Release the current Resources and Bandwidth
     * allocated to the endpoint and assign the new Resources and Bandwidth
     * requested for the endpoint." p.118 then names it as the way to do exactly
     * what task 7a-B.1 needs: "software may issue a Configure Endpoint Command
     * with the Drop and Add bits set for the target endpoint that is in the
     * Stopped state or Running but Idle state."
     *
     * It is not merely permitted, it is **required** for a reprogram: "xHC
     * behavior is undefined if the Drop Context (D) flag is '0', the Add Context
     * (A) flag is '1', and the Output Endpoint Context is not in the Disabled
     * state" (p.106). The order is the xHC's own - "The Drop Context flags are
     * evaluated before the Add Context flags" (p.105) - so one command really is
     * a drop then an add, and nothing here has to sequence it.
     */

    for (i = 0; i < XHCI_CONTEXT_DWORDS; i++) {
        xhciCtxStore(context, i, 0);
    }
    xhciCtxStore(context, XHCI_ICC_DW_DROP, dropFlags);
    xhciCtxStore(context, XHCI_ICC_DW_ADD, addFlags);
    /*
     * DW7's Configuration Value / Interface Number / Alternate Setting are
     * meaningful only when HCCPARAMS1.CFC = 1, and are RsvdZ otherwise
     * (section 8). This driver sets none of them, so the word stays the zero the
     * loop above wrote - stated here because "we left it alone" and "we decided
     * it is zero" are different claims and only the second survives a reviewer.
     *
     * *(This said "issues no Configure Endpoint yet" until the post-Phase 13 review rounds, which has
     * been false since task 7a-A.1: `src/xhci_slot.c` builds and submits
     * `XhciTrbConfigureEndpoint` for every endpoint open and for the hub
     * marking. What is still true is the part that matters here - the command
     * is issued and these three fields are not filled in, whatever CFC says.)*
     */
    return XHCI_CTX_OK;
}

ULONG XhciBuildSlotContext(volatile ULONG *context,
                           const XHCI_SLOT_PARAMS *params)
{
    ULONG dw0;
    ULONG dw1;
    ULONG dw2;
    ULONG i;

    if (context == NULL || params == NULL) {
        return XHCI_CTX_BAD_PARAM;
    }
    if (params->RouteString > XHCI_SLOT_ROUTE_MASK) {
        return XHCI_CTX_BAD_PARAM;
    }
    /* Speed is four bits and holds a raw Protocol Speed ID value, not this
     * driver's decoded class - see the note on XHCI_SLOT_PARAMS. */
    if (params->Psiv > 0x0FUL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /*
     * "Root Hub Port Number ... valid values are 1 to MaxPorts" (Table 6-5), and
     * MaxPorts is eight bits. Zero is refused here rather than treated as
     * "unknown": a slot context with no root port names no path at all.
     */
    if (params->RootHubPort == 0 || params->RootHubPort > 0xFFUL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /*
     * "Context Entries ... identifies the index of the last valid Endpoint
     * Context" (Table 6-4): 1 while only EP0 exists, at most 31.  Zero would
     * describe a device with no default control endpoint, which cannot be
     * addressed.
     */
    if (params->ContextEntries == 0 || params->ContextEntries > XHCI_MAX_DCI) {
        return XHCI_CTX_BAD_PARAM;
    }
    if (params->MaxExitLatency > XHCI_SLOT_MAX_EXIT_MASK) {
        return XHCI_CTX_BAD_PARAM;
    }
    if (params->NumberOfPorts > 0xFFUL || params->ParentSlotId > 0xFFUL ||
        params->ParentPortNumber > 0xFFUL || params->TtThinkTime > 0x03UL ||
        params->InterrupterTarget > 0x3FFUL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /* Number of Ports is "the number of downstream facing ports supported by
     * the hub" and is defined only when Hub = 1 (Table 6-5). A port count on a
     * non-hub is a caller confusing this device with its parent. */
    if (params->Hub == 0 && params->NumberOfPorts != 0) {
        return XHCI_CTX_BAD_PARAM;
    }
    /*
     * And the other direction, which task 7b-A.2 needed: "If this device is a
     * hub (Hub = '1'), then this field is set by software to identify the number
     * of downstream facing ports supported by the hub" (Table 6-5 p.409), and
     * 6.2.2.2 p.412 makes it a requirement of a valid Configure Endpoint Input
     * Slot Context - "If Hub = '1', then the Number of Ports field shall be
     * initialized". Zero is not an initialization, it is the value a caller that
     * marked a hub before reading its descriptor would supply.
     */
    if (params->Hub != 0 && params->NumberOfPorts == 0) {
        return XHCI_CTX_BAD_PARAM;
    }
    /*
     * **Half of Table 6-6's TTT rule, and the half that can be checked here.**
     * p.410: "If this device is not a High-speed hub (Hub = '0' or Speed !=
     * High-speed), then this field shall be '0'." The `Hub = '0'` disjunct is a
     * question about this structure and is answered here; the speed disjunct is
     * not, because `Psiv` is the raw Protocol Speed ID and which value means
     * High-Speed is a property of the controller's PSI table rather than of this
     * encoder (see the note on XHCI_SLOT_PARAMS). That half lives in
     * `XhciTopoHubMark`, where the driver's decoded speed class is in hand.
     *
     * MTT gets no matching refusal on purpose: Table 6-4 p.408 allows it on a
     * *non*-hub - "or if this is a Low-/Full-speed device or Full-speed hub and
     * connected to the xHC through a parent High-speed hub that supports
     * Multiple TTs" - so a caller setting it with Hub = 0 may be right, and this
     * encoder cannot tell which parent the device hangs off.
     */
    if (params->Hub == 0 && params->TtThinkTime != 0) {
        return XHCI_CTX_BAD_PARAM;
    }
    /*
     * **The TT pair is one field in two halves** (task 7b-A.3). Table 6-6 p.409
     * conditions Parent Hub Slot ID and Parent Port Number on the same sentence
     * - an LS/FS device connected through a High-speed hub - and clears both
     * together otherwise, so exactly one of them being set is a caller that
     * resolved half the answer. Refused rather than programmed, because either
     * half alone describes a split-transaction path the xHC cannot use: a Slot
     * ID with no port names no downstream port, and a port with no Slot ID names
     * no hub. Hub ports are 1-based, so 0 is unambiguous for both.
     */
    if ((params->ParentSlotId == 0) != (params->ParentPortNumber == 0)) {
        return XHCI_CTX_BAD_PARAM;
    }

    dw0 = params->RouteString & XHCI_SLOT_ROUTE_MASK;
    dw0 |= (params->Psiv << XHCI_SLOT_SPEED_SHIFT) & XHCI_SLOT_SPEED_MASK;
    if (params->MultiTt) {
        dw0 |= XHCI_SLOT_MTT;
    }
    if (params->Hub) {
        dw0 |= XHCI_SLOT_HUB;
    }
    dw0 |= (params->ContextEntries << XHCI_SLOT_ENTRIES_SHIFT) &
           XHCI_SLOT_ENTRIES_MASK;

    dw1 = params->MaxExitLatency & XHCI_SLOT_MAX_EXIT_MASK;
    dw1 |= params->RootHubPort << XHCI_SLOT_ROOT_PORT_SHIFT;
    dw1 |= params->NumberOfPorts << XHCI_SLOT_PORT_COUNT_SHIFT;

    dw2 = params->ParentSlotId << XHCI_SLOT_TT_SLOT_SHIFT;
    dw2 |= params->ParentPortNumber << XHCI_SLOT_TT_PORT_SHIFT;
    dw2 |= params->TtThinkTime << XHCI_SLOT_TTT_SHIFT;
    dw2 |= params->InterrupterTarget << XHCI_SLOT_INTR_TARGET_SHIFT;

    for (i = 0; i < XHCI_CONTEXT_DWORDS; i++) {
        xhciCtxStore(context, i, 0);
    }
    xhciCtxStore(context, 0, dw0);
    xhciCtxStore(context, 1, dw1);
    xhciCtxStore(context, 2, dw2);
    /*
     * DW3 is USB Device Address and Slot State, and both are **output only**
     * (Table 6-7): the xHC writes them and software reads them back out of the
     * *Device* Context. Writing a device address into an Input Context is the
     * classic way to believe an address was set - it is written as zero here and
     * read nowhere except through XhciSlotContextAddress, which reads the output
     * side.
     */
    return XHCI_CTX_OK;
}

ULONG XhciBuildEndpointContext(volatile ULONG *context,
                               const XHCI_EP_PARAMS *params)
{
    ULONG dw0;
    ULONG dw1;
    ULONG dw2;
    ULONG dw4;
    ULONG i;

    if (context == NULL || params == NULL) {
        return XHCI_CTX_BAD_PARAM;
    }
    if (params->EpType == XHCI_EP_TYPE_INVALID || params->EpType > 0x07UL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /* "Max Packet Size ... 31:16" (Table 6-9), and a zero-length packet size
     * would divide the TD Size computation by zero (4.11.2.4). */
    if (params->MaxPacketSize == 0 || params->MaxPacketSize > 0xFFFFUL) {
        return XHCI_CTX_BAD_PARAM;
    }
    if (params->MaxBurstSize > 0xFFUL || params->Mult > 0x03UL ||
        params->Interval > 0xFFUL || params->ErrorCount > 0x03UL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /* "TR Dequeue Pointer ... 16 byte aligned" (Table 6-10); the low four bits
     * of that DWORD carry DCS and RsvdZ, not address. */
    if (params->DequeuePA == 0 || (params->DequeuePA & 0x0FUL) != 0) {
        return XHCI_CTX_BAD_PARAM;
    }
    if (params->Dcs > 1UL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /* "Average TRB Length ... shall be greater than '0'" (Table 6-11). A zero is
     * the value a caller that forgot the field would supply, which is exactly
     * why it is refused rather than defaulted. */
    if (params->AverageTrbLength == 0 ||
        params->AverageTrbLength > XHCI_EP_AVG_TRB_MASK) {
        return XHCI_CTX_BAD_PARAM;
    }
    if (params->MaxEsitPayload > 0xFFFFUL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /* CErr "shall be set to '0'" for isoch endpoints (Table 6-9), and 1-3 for
     * everything else - a nonzero count on isoch is a retry the specification
     * says the endpoint cannot perform. */
    if (params->EpType == XHCI_EP_TYPE_ISOCH_IN ||
        params->EpType == XHCI_EP_TYPE_ISOCH_OUT) {
        if (params->ErrorCount != 0) {
            return XHCI_CTX_BAD_PARAM;
        }
    } else if (params->ErrorCount == 0) {
        return XHCI_CTX_BAD_PARAM;
    }

    /*
     * EP State is output-only and "shall be cleared to '0'" in an Input Context
     * (Table 6-8), so the field is left at the zero the clearing loop writes -
     * MaxPStreams and LSA with it, since this driver uses no streams.
     */
    dw0 = params->Mult << XHCI_EP_MULT_SHIFT;
    dw0 |= params->Interval << XHCI_EP_INTERVAL_SHIFT;

    dw1 = params->ErrorCount << XHCI_EP_CERR_SHIFT;
    dw1 |= params->EpType << XHCI_EP_TYPE_SHIFT;
    dw1 |= params->MaxBurstSize << XHCI_EP_MAXBURST_SHIFT;
    dw1 |= params->MaxPacketSize << XHCI_EP_MAXPACKET_SHIFT;

    dw2 = (params->DequeuePA & XHCI_EP_DEQUEUE_MASK);
    if (params->Dcs) {
        dw2 |= XHCI_EP_DCS;
    }

    dw4 = params->AverageTrbLength & XHCI_EP_AVG_TRB_MASK;
    dw4 |= params->MaxEsitPayload << XHCI_EP_MAX_ESIT_SHIFT;

    for (i = 0; i < XHCI_CONTEXT_DWORDS; i++) {
        xhciCtxStore(context, i, 0);
    }
    xhciCtxStore(context, 0, dw0);
    xhciCtxStore(context, 1, dw1);
    xhciCtxStore(context, 2, dw2);
    /* DW3 is the TR Dequeue Pointer's high half: always zero here, and written
     * rather than assumed for the reason every other pointer in this driver is
     * (no 64-bit DMA - the value is a claim to check, not one to inherit). */
    xhciCtxStore(context, 3, 0);
    xhciCtxStore(context, 4, dw4);
    return XHCI_CTX_OK;
}

ULONG XhciBuildEp0Params(ULONG maxPacketSize,
                         ULONG dequeuePA,
                         ULONG dcs,
                         PXHCI_EP_PARAMS params)
{
    if (params == NULL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /*
     * The legal set rather than a range. bMaxPacketSize0 is 8, 16, 32 or 64
     * (USB 2.0 section 9.6.1), and a device reporting anything else has given a
     * malformed descriptor - which task 6-B.4 must decline rather than program,
     * because the value ends up in the TD Size arithmetic of every later control
     * transfer.
     */
    if (!XHCI_EP0_MPS_IS_LEGAL(maxPacketSize)) {
        return XHCI_CTX_BAD_PARAM;
    }

    params->EpType = XHCI_EP_TYPE_CONTROL;
    params->MaxPacketSize = maxPacketSize;
    /*
     * Zero because this is a **control** endpoint, not because Max Burst is
     * SuperSpeed's - the sentence that stood here said the latter and it is the
     * claim `XhciBuildEndpointParams` was corrected for. Max Burst comes from a
     * High-Speed *periodic* endpoint's additional-transaction count (Table 6-9,
     * spec 6.2.3.4), and a control endpoint has no such count: EP0 moves one
     * transaction per opportunity at every USB 2.0 speed. Mult really is
     * SuperSpeed isoch only (Table 6-8).
     */
    params->MaxBurstSize = 0;
    params->Mult = 0;
    /* "Interval ... a value of '0' for a control endpoint" - control endpoints
     * are asynchronous and have no service interval (Table 6-12). */
    params->Interval = 0;
    params->ErrorCount = XHCI_EP_CERR_DEFAULT;
    params->DequeuePA = dequeuePA;
    params->Dcs = dcs;
    params->AverageTrbLength = XHCI_EP_AVG_TRB_CONTROL;
    /* Max ESIT Payload is periodic-only; 0 for control and bulk (section 8). */
    params->MaxEsitPayload = 0;
    return XHCI_CTX_OK;
}

ULONG XhciIntervalFromPeriod(ULONG period, ULONG speedClass, ULONG *interval)
{
    ULONG log2;
    ULONG shift;

    if (interval == NULL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /*
     * The contract's own bounds, refused rather than clamped. usbport buckets to
     * a power of two in 1..32 and floors Low Speed at 8; anything else is either
     * a field this driver read from the wrong offset or a usbport build whose
     * bucketing differs from the two that were disassembled. Both are reasons to
     * refuse an endpoint, not to invent an interval for it.
     */
    if (period == 0 || period > 32 || (period & (period - 1)) != 0) {
        return XHCI_CTX_BAD_PARAM;
    }

    if (speedClass == XHCI_SPEED_HIGH) {
        shift = 0;              /* Period already counts microframes */
    } else if (speedClass == XHCI_SPEED_FULL) {
        shift = 3;              /* Period counts frames; 1 frame = 8 microframes */
    } else if (speedClass == XHCI_SPEED_LOW) {
        /*
         * Both shipping usbport builds floor a Low-Speed Period at 8 frames
         * (SP4 0x2520E-0x2521D, NUSB 0x24B90-0x24B9F), so 1/2/4 are **outside
         * the contract** at this speed and are refused like every other value
         * the derivation does not predict.
         *
         * An earlier version accepted them, reasoning that the floor was
         * usbport's and this function should translate rather than repair.
         * That confused two different things: repairing would be silently
         * *raising* a small Period to 8, which would indeed hide a misread
         * field - refusing it does the opposite and surfaces one. The rule
         * everywhere else in this function is refuse-don't-repair, and Low
         * Speed was the one place it was not applied.
         */
        if (period < 8) {
            return XHCI_CTX_BAD_PARAM;
        }
        shift = 3;
    } else {
        return XHCI_CTX_BAD_PARAM;
    }

    log2 = 0;
    while ((period & 1UL) == 0) {
        period >>= 1;
        log2++;
    }

    *interval = log2 + shift;
    return XHCI_CTX_OK;
}

ULONG XhciIntervalForSpeed(ULONG interval,
                           ULONG deviceSpeedClass,
                           ULONG *out,
                           ULONG *floored)
{
    if (out == NULL) {
        return XHCI_CTX_BAD_PARAM;
    }
    if (floored != NULL) {
        *floored = 0;
    }

    /*
     * **The unit conversion is not the whole answer, and this is the half that
     * was missing.** `XhciIntervalFromPeriod` turns usbport's `Period` into
     * log2(microframes) using the speed *usbport* bucketed it with. The Endpoint
     * Context's Interval must additionally be legal for the speed the **Slot
     * Context** carries, which is the port's decoded speed - and Table 6-12
     * p.420 gives those ranges per endpoint type and speed, not per unit:
     *
     *   FS/LS Interrupt ....... Interval 3-10
     *   SS/HS Interrupt/Isoch . Interval 0-15
     *
     * The two speeds are the same on every path but one, and that one is this
     * driver's own doing: Phase 5 task 7 reports every connected root port to
     * usbport as High Speed, so a Full- or Low-Speed device arrives with a
     * `Period` bucketed in microframes. A Full-Speed endpoint with
     * `bInterval = 1` then yields Interval 0 beside a Slot Context that says
     * Full Speed - a combination Table 6-12 does not allow, and which a
     * conforming xHC may answer with Parameter Error rather than by servicing
     * the endpoint more often. That is a Configure Endpoint that never
     * completes, i.e. a HID device that enumerates and stays silent.
     *
     * **What raising it to the floor does and does not achieve, stated exactly.**
     * It makes the Endpoint Context *legal*, which is the difference between an
     * endpoint the xHC schedules and a Configure Endpoint it may answer with
     * Parameter Error. It does **not** recover the descriptor's `bInterval`:
     * usbport's High-Speed bucketing is `1 << min(bInterval-1, 5)`, so a
     * Full-Speed endpoint asking for 2 ms arrives as `Period = 2` and leaves
     * here as 1 ms. The endpoint is therefore polled **at least** as often as it
     * asked and sometimes more - which costs periodic bandwidth and works for
     * HID, and is not the same claim as "correct".
     *
     * Refusing instead is not available: it would decline every Full- and
     * Low-Speed interrupt endpoint whose `bInterval` is under 8 ms, which is
     * most of them, and Phase 7a's checkpoint is a keyboard and a mouse. The
     * residual error is bounded, never in the slow direction, and **owned by
     * whatever removes the cause** - Phase 5 task 7's root-port speed reporting,
     * or Phase 7b, where a device behind an external hub gets its real speed
     * from usbport and none of this arises. `floored` counts it so a target run
     * shows how often it happens rather than leaving it a paragraph.
     */
    if (deviceSpeedClass == XHCI_SPEED_FULL ||
        deviceSpeedClass == XHCI_SPEED_LOW) {
        if (interval > XHCI_EP_INTERVAL_FSLS_MAX) {
            return XHCI_CTX_BAD_PARAM;
        }
        if (interval < XHCI_EP_INTERVAL_FSLS_MIN) {
            interval = XHCI_EP_INTERVAL_FSLS_MIN;
            if (floored != NULL) {
                *floored = 1;
            }
        }
    } else if (deviceSpeedClass == XHCI_SPEED_HIGH) {
        if (interval > XHCI_EP_INTERVAL_HS_MAX) {
            return XHCI_CTX_BAD_PARAM;
        }
    } else {
        return XHCI_CTX_BAD_PARAM;
    }

    *out = interval;
    return XHCI_CTX_OK;
}

ULONG XhciIsochIntervalFromBInterval(ULONG bInterval,
                                     ULONG speedClass,
                                     ULONG *interval)
{
    if (interval == NULL) {
        return XHCI_CTX_BAD_PARAM;
    }
    /*
     * The range is refused rather than clamped, and the two ends fail for
     * different reasons that reach the same answer: `bInterval = 0` is a device
     * declaring no service interval, which no conversion can invent, and a value
     * above 16 exceeds what either row of Table 6-12 can express. Both mean this
     * driver does not know how often the endpoint wants servicing, and the
     * caller's fallback - the cadence usbport is scheduling to - is a better
     * answer than a clamped one, because it is at least the rate the *other*
     * side of this driver believes in.
     */
    if (bInterval < XHCI_DESC_BINTERVAL_MIN ||
        bInterval > XHCI_DESC_BINTERVAL_MAX) {
        return XHCI_CTX_BAD_PARAM;
    }

    if (speedClass == XHCI_SPEED_HIGH) {
        /* period = 2^(bInterval-1) microframes, and Interval is log2 of the
         * period in 125 us units, so the two exponents differ by one. */
        *interval = bInterval - 1;
        return XHCI_CTX_OK;
    }
    if (speedClass == XHCI_SPEED_FULL) {
        /* period = 2^(bInterval-1) *frames*, and a frame is eight 125 us
         * units - hence three more than the High-Speed answer. */
        *interval = bInterval + 2;
        return XHCI_CTX_OK;
    }
    /* Low Speed, or a class this driver does not decode: see the header. */
    return XHCI_CTX_BAD_PARAM;
}

ULONG XhciDciFromEndpointAddress(ULONG endpointAddress)
{
    ULONG number;

    number = endpointAddress & 0x0FUL;
    if (number == 0) {
        /*
         * DCI 1 either way. Spec 4.5.1 gives a control endpoint DCI `2n + 1`
         * and **ignores** the direction bit, because a control endpoint is
         * bidirectional and has one context. An earlier version of this
         * function refused `0x80` as "a malformed address" - that was invented,
         * not read: the bit is not a claim the address makes, it is a field the
         * spec says to disregard.
         */
        return 1;
    }
    /*
     * **The transfer type is not an argument, so this line is right only for
     * non-control endpoints**, which is every endpoint this driver ever sees:
     * 4.5.1 gives *every* control endpoint DCI `2n + 1` regardless of the
     * direction bit, so a hypothetical control endpoint at address 0x02 would
     * be answered here with the OUT DCI 4 instead of 5. Unreachable rather than
     * handled - `XhciBuildEndpointParams` refuses a non-default control
     * endpoint outright - and stated here because the function itself did not
     * say so, and the next caller has no way to know from the signature.
     */
    return (endpointAddress & 0x80UL) ? (2 * number + 1) : (2 * number);
}

ULONG XhciEndpointAddressFromDci(ULONG dci)
{
    /*
     * The inverse of the line above, and only for a *non-default* endpoint: DCI
     * 1 is EP0, whose two directions share one context, so there is no single
     * address to answer with and 0 - which is EP0's own address - is the honest
     * one. DCI 0 is the Slot Context and names no endpoint at all.
     */
    if (dci < 2 || dci > XHCI_MAX_DCI) {
        return 0;
    }
    return (dci & 1UL) ? (((dci - 1) / 2) | 0x80UL) : (dci / 2);
}

ULONG XhciBuildEndpointParams(ULONG transferType,
                              ULONG directionIn,
                              ULONG maxPacketSize,
                              ULONG period,
                              ULONG periodSpeedClass,
                              ULONG deviceSpeedClass,
                              ULONG transactionsPerMicroframe,
                              ULONG descBInterval,
                              ULONG dequeuePA,
                              ULONG dcs,
                              PXHCI_EP_PARAMS params,
                              PULONG intervalFloored,
                              PULONG intervalDerived)
{
    ULONG interval;
    ULONG floored;
    ULONG status;

    if (params == NULL) {
        return XHCI_CTX_BAD_PARAM;
    }
    if (intervalFloored != NULL) {
        *intervalFloored = 0;
    }
    if (intervalDerived != NULL) {
        *intervalDerived = 0;
    }
    /* usbport computes this as wMaxPacketSize bits 12:11 + 1, so 1..3
     * (docs/usb-xhci-info/usbport-miniport-abi.md section 5). A 0 is a caller that read the
     * field from the wrong offset; anything above 3 is not a USB 2.0 value. */
    if (transactionsPerMicroframe == 0 || transactionsPerMicroframe > 3) {
        return XHCI_CTX_BAD_PARAM;
    }
    /* wMaxPacketSize bits 10:0 (Table 6-9 via the properties' MaxPacketSize),
     * and a zero would divide the TD Size arithmetic by zero exactly as it would
     * for EP0. */
    if (maxPacketSize == 0 || maxPacketSize > 0x7FFUL) {
        return XHCI_CTX_BAD_PARAM;
    }

    params->MaxPacketSize = maxPacketSize;
    /*
     * **Max Burst is not SuperSpeed-only**, and an earlier version of this
     * function said it was and hard-coded 0. Table 6-9 / spec 6.2.3.4 take it
     * from the additional-transaction count of a **High-Speed periodic**
     * endpoint - a high-bandwidth HS interrupt or isoch endpoint moves 2 or 3
     * transactions per microframe, and programming 0 for one silently gives it a
     * third of the bandwidth it asked for. usbport already hands the count over
     * as `TransactionPerMicroframe`; the field was simply never consumed.
     *
     * Max Burst is "additional" transactions, so it is the count minus one, and
     * it applies to periodic HS endpoints only - a bulk endpoint has no
     * microframe structure to burst within, and Full/Low Speed has no
     * microframes at all. Those keep 0, which is now a decision rather than an
     * assumption.
     *
     * Mult stays 0: it is SuperSpeed isoch only (Table 6-8).
     */
    params->MaxBurstSize = 0;
    params->Mult = 0;
    params->DequeuePA = dequeuePA;
    params->Dcs = dcs;
    params->ErrorCount = XHCI_EP_CERR_DEFAULT;
    params->Interval = 0;
    params->MaxEsitPayload = 0;
    /* A typical transfer size is the honest estimate for a bulk or interrupt
     * endpoint, and the field must be nonzero (Table 6-11). */
    params->AverageTrbLength = maxPacketSize;

    switch (transferType) {
    case USBPORT_TRANSFER_TYPE_INTERRUPT:
        params->EpType = directionIn ? XHCI_EP_TYPE_INTERRUPT_IN
                                     : XHCI_EP_TYPE_INTERRUPT_OUT;
        /*
         * Two speeds, two jobs. The **period** speed is the one usbport
         * bucketed `Period` with and decides its unit; the **device** speed is
         * what the Slot Context will carry and decides which Intervals are legal
         * (Table 6-12). They differ only because of Phase 5 task 7's root-port
         * reporting - see XhciIntervalForSpeed - and passing one for both is
         * what produced an Endpoint Context the xHC may refuse outright.
         */
        status = XhciIntervalFromPeriod(period, periodSpeedClass, &interval);
        if (status != XHCI_CTX_OK) {
            return status;
        }
        status = XhciIntervalForSpeed(interval, deviceSpeedClass, &interval,
                                      &floored);
        if (status != XHCI_CTX_OK) {
            return status;
        }
        if (intervalFloored != NULL) {
            *intervalFloored = floored;
        }
        params->Interval = interval;
        /*
         * **Max Burst follows the *device* speed, not usbport's belief** - the
         * opposite of the interval's *unit*, and the eleventh review caught a
         * previous round making them symmetric because they looked alike.
         *
         * The unit question is "what did the number that arrived mean", and the
         * answer is whatever usbport thought. This is a validity question, and
         * spec 6.2.3.4 p.418 answers it about the endpoint itself: **"For all
         * Low-/Full-Speed endpoints this field shall be cleared to '0'."** So a
         * Full-Speed endpoint gets 0 whatever usbport believed about its speed,
         * exactly as its Interval has to be in the Full-Speed range.
         *
         * That leaves a Full/Low-Speed endpoint arriving with a count above one,
         * which is refused rather than silently zeroed. It is unreachable
         * through a conforming device - wMaxPacketSize bits 12:11 are reserved
         * and zero at those speeds, so usbport computes 1 - and refusing is what
         * makes a device that is *not* conforming visible instead of programmed.
         */
        if (deviceSpeedClass == XHCI_SPEED_HIGH) {
            params->MaxBurstSize = transactionsPerMicroframe - 1;
        } else if (transactionsPerMicroframe != 1) {
            return XHCI_CTX_BAD_PARAM;
        }
        /*
         * "Periodic only: Max Packet Size * (Max Burst + 1)"
         * (docs/usb-xhci-info/xhci-data-structures.md section 8). Written through the
         * expression rather than as a copy of the packet size, because those
         * two only coincide while the burst is 0 - which, since the line above,
         * is no longer always true.
         */
        params->MaxEsitPayload = maxPacketSize * (params->MaxBurstSize + 1);
        break;

    case USBPORT_TRANSFER_TYPE_BULK:
        params->EpType = directionIn ? XHCI_EP_TYPE_BULK_IN
                                     : XHCI_EP_TYPE_BULK_OUT;
        /* Bulk has no microframe structure to burst within, so a count above
         * one is a malformed descriptor rather than a high-bandwidth pipe. */
        if (transactionsPerMicroframe != 1) {
            return XHCI_CTX_BAD_PARAM;
        }
        /* "a value of '0' ... for bulk" - bulk has no service interval, and
         * Max ESIT Payload is periodic-only (section 8). Both stay 0. */
        break;

    case USBPORT_TRANSFER_TYPE_ISOCHRONOUS:
        /*
         * Task 9-A.1, **corrected by the batch 9-A review**. Until 9-A.1 this
         * fell into the refusal below on the grounds that "usbport forces an
         * isoch `Period` to 1 and carries no interval, so an isoch context built
         * from it would claim a 125 us service interval it was never told
         * about".
         *
         * **That premise still holds, and the first draft of this branch
         * overstated what replaced it.** It said the per-packet frame and
         * microframe stamps task 9-0.1 recovered "are an ESIT and therefore an
         * Interval", i.e. that the endpoint's service interval had been
         * *derived*. It has not been. Those stamps are a fixed indexing of the
         * URB - `StartFrame + (i >> 3)` on High Speed and `StartFrame + i`
         * otherwise - written the same way whatever the endpoint descriptor
         * says, so they carry usbport's own assumption about the cadence and not
         * a measurement of the device's. `docs/usb-xhci-info/usbport-miniport-abi.md` says so
         * in as many words: the block "carries a frame and microframe *per
         * packet* rather than an interval, so the endpoint's ESIT interval is
         * still not something usbport hands over".
         *
         * **So the values below are an assumption, stated as one**, and they are
         * what this branch programs when nothing better is known. The miniport
         * never sees `bInterval` through usbport (task 7a-A.1) and the one number
         * usbport does supply is refuted above, so the honest fallback is the
         * cadence usbport is itself scheduling to - which is what its packets
         * demand and what is correct for a `bInterval` of 1.
         *
         * **Task 9-A.2 supplies the missing channel, and it is applied at the
         * bottom of this branch.** `descBInterval` is the endpoint's own
         * `bInterval`, taken from the configuration descriptor snooped on EP0
         * (src/xhci_desc.h - the reply channel task 7b-A.1 built for hub
         * descriptors), and where it is known the Interval is *derived* rather
         * than assumed. Where it is not - no descriptor read, no such endpoint
         * in it, alternate settings that disagreed - these two values still
         * stand, and `IsoCadenceMismatches` remains the runtime detector for
         * that case, raised per submission by `xhciXferIsoCadenceAgrees`
         * comparing usbport's stamps against the Interval programmed here.
         *
         * **The speed that decides is the device's, not usbport's**, the same
         * split the interrupt branch above makes and for the same reason: the
         * Interval has to be legal for the speed the Slot Context carries
         * (Table 6-12). The two differ on a root port, because Phase 5 task 7
         * reports every connected one as High Speed - so a Full-Speed audio
         * device attached directly arrives with usbport having stamped its
         * packets eight times too fast. Building the Endpoint Context for the
         * true speed is what keeps the *pipe* correct, and the review corrected
         * the other half of this sentence too: a draft claimed "this driver's
         * Frame IDs do not come from them", which was **false** - the builder
         * takes each TD's Frame ID from exactly these stamps. What makes that
         * safe is the cadence gate, which sees the disagreement and drops the
         * whole request to SIA rather than placing eight TDs in a row that all
         * name the same frame.
         *
         * The disagreement is already counted, by `EndpointSpeedMismatches` at
         * the open - which fires on exactly this condition and on nothing else.
         * A draft of this branch reported it through `intervalFloored` instead,
         * which would have put two different diagnoses under one
         * release-build counter (`EndpointIntervalsFloored` means "the
         * bucketed Period landed below the speed's floor", which is not what
         * happens here: an isoch Interval is derived, never bucketed).
         */
        params->EpType = directionIn ? XHCI_EP_TYPE_ISOCH_IN
                                     : XHCI_EP_TYPE_ISOCH_OUT;
        /*
         * "CErr ... shall be set to '0'" for isoch (Table 6-9). An isochronous
         * transaction has no handshake, so a retry count is not merely unused,
         * it describes something the bus cannot do.
         */
        params->ErrorCount = 0;
        if (deviceSpeedClass == XHCI_SPEED_HIGH) {
            params->Interval = XHCI_EP_INTERVAL_ISOCH_HS;
            /* 6.2.3.4 p.418 again, and isoch is named in the same sentence as
             * interrupt: a High-Speed periodic endpoint's Max Burst is its
             * additional-transaction count minus one. */
            params->MaxBurstSize = transactionsPerMicroframe - 1;
        } else if (deviceSpeedClass == XHCI_SPEED_FULL) {
            params->Interval = XHCI_EP_INTERVAL_ISOCH_FS;
            /* "For all Low-/Full-Speed endpoints this field shall be cleared to
             * '0'" - and a Full-Speed endpoint arriving with a count above one
             * is refused rather than silently zeroed, exactly as the interrupt
             * branch refuses it. */
            if (transactionsPerMicroframe != 1) {
                return XHCI_CTX_BAD_PARAM;
            }
        } else {
            /*
             * **Low Speed has no isochronous endpoints at all** (USB 2.0
             * section 5.6: isochronous transfers are full- and high-speed only),
             * so this is not a limitation of this driver - it is a request that
             * describes no legal USB device. Refused rather than given the
             * Full-Speed treatment, which would build a plausible context for a
             * pipe that cannot exist.
             */
            return XHCI_CTX_BAD_PARAM;
        }
        /*
         * **Task 9-A.2 ends the assumption where a descriptor was read.**
         *
         * The two values just programmed are usbport's cadence, and the block
         * comment above says exactly what they rest on: usbport stamps one
         * packet per microframe on High Speed and one per frame otherwise,
         * whatever the endpoint asked for. `descBInterval` is the endpoint's own
         * answer, snooped off its configuration descriptor (src/xhci_desc.h),
         * and it overrides - a High-Speed endpoint declaring `bInterval = 4`
         * wants one ESIT per millisecond and was being serviced eight times too
         * fast, which is a stream consumed eight times too quickly.
         *
         * **The override is applied after the speed branch rather than inside
         * it**, so that everything the branch decides on the *speed* still holds:
         * the Low-Speed refusal above is unreachable-by-descriptor, and Max
         * Burst, CErr and the transaction-count refusal are unchanged. Only the
         * Interval moves, because only the Interval was assumed.
         *
         * A refusal here is a refusal of the endpoint, not a quiet fallback. The
         * only reachable cause is a speed whose row Table 6-12 does not carry -
         * the walk already declines a `bInterval` outside 1..16 - and building
         * the endpoint at the assumed cadence *after* learning it is wrong would
         * be programming a rate this driver has been told is not the device's.
         */
        if (descBInterval != 0) {
            status = XhciIsochIntervalFromBInterval(descBInterval,
                                                    deviceSpeedClass,
                                                    &interval);
            if (status != XHCI_CTX_OK) {
                return status;
            }
            params->Interval = interval;
            if (intervalDerived != NULL) {
                *intervalDerived = 1;
            }
        }
        /*
         * "Max ESIT Payload ... represents the total number of bytes this
         * endpoint will transfer during an ESIT" (6.2.3.8), and the packet count
         * per ESIT is `(Max Burst Size + 1) * (Mult + 1)` (4.14.1 p.234). Mult
         * is 0 for every USB 2.0 endpoint, so the second factor is 1 - written
         * as the product anyway, because the *reason* it is 1 is that Mult is 0
         * and not that the term does not exist.
         */
        params->MaxEsitPayload = maxPacketSize * (params->MaxBurstSize + 1) *
                                 (params->Mult + 1);
        params->AverageTrbLength = maxPacketSize;
        break;

    default:
        /*
         * A non-default **control** endpoint, which neither shipping usbport
         * build opens. Refusing is the whole point: answering for a type this
         * driver cannot schedule would leave usbport submitting into a pipe it
         * mis-programmed.
         */
        return XHCI_CTX_BAD_PARAM;
    }

    return XHCI_CTX_OK;
}

ULONG XhciInitialMps0(ULONG speedClass)
{
    /*
     * "LS = 8, HS = 64. FS = 8/16/32/64, unknowable before the descriptor is
     * read" (docs/usb-xhci-info/xhci-data-structures.md section 8, from spec
     * 4.3). **The FS assumption is 64 rather than spec 4.3's 8**, because the
     * field bounds what the controller will accept and usbport's first
     * descriptor request is 64 bytes - declaring 8 makes any device with a
     * larger bMaxPacketSize0 babble on its very first transfer. The full
     * reasoning and the measurement are on XHCI_EP0_MPS_FULL_INITIAL in
     * src/xhci.h.
     *
     * SuperSpeed is deliberately absent rather than given its 512: USB 3.0 is
     * out of scope (AGENTS.md), a SuperSpeed port is left unpowered by the port
     * strategy, and answering 0 here makes an attempt to address one refuse at
     * the caller instead of producing a plausible context.
     */
    if (speedClass == XHCI_SPEED_LOW) {
        return XHCI_EP0_MPS_LOW;
    }
    if (speedClass == XHCI_SPEED_FULL) {
        return XHCI_EP0_MPS_FULL_INITIAL;
    }
    if (speedClass == XHCI_SPEED_HIGH) {
        return XHCI_EP0_MPS_HIGH;
    }
    return 0;
}
