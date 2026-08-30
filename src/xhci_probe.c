/*
 * xhci_probe.c - roadmap task 6-V.1's runtime transfer-contract probe.
 *
 * See src/xhci_probe.h for what the probe is for and why its gates are
 * semantic rather than budgeted. This file is the classification and the
 * printing, in that order and on opposite sides of the controller lock.
 *
 * Two rules hold everywhere below, and both are structural:
 *
 *   **The fold decides nothing the driver acts on.** It reads usbport-owned
 *   structures, writes counters into the miniport extension, and returns a
 *   description. No caller branches on it.
 *
 *   **The reporter runs with no lock held.** A DbgPrint plus a few dozen
 *   port-0xE9 byte writes under a DISPATCH-level spin lock is the cost that
 *   Phase 4 task 4's review round existed to remove, and it does not become
 *   acceptable because the site is instrumentation.
 *
 * C89 only. Every function carries its IRQL requirement.
 */

#include "xhci.h"
#include "xhci_usbport.h"
#include "xhci_hw.h"
#include "xhci_probe.h"
#include "xhci_dbg.h"

/*
 * Both signatures, not just the leading one.
 *
 * The probe's state sits at the very end of the miniport extension, immediately
 * before the trailing signature, so a build whose extension is not the one this
 * image declared is exactly the case where writing it would corrupt something
 * else. Every call site has already asked xhciExtensionValid the same question;
 * asking again costs two loads and means the answer does not depend on where
 * this gets called from next.
 *
 * IRQL: any.
 */
static ULONG xhciProbeExtensionUsable(const XHCI_EXTENSION *ext)
{
    if (ext == NULL) {
        return 0;
    }
    if (ext->Signature != XHCI_EXTENSION_SIGNATURE) {
        return 0;
    }
    return (ext->TrailingSignature == XHCI_EXTENSION_TRAILING) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The saturating key set                                              */
/* ------------------------------------------------------------------ */

/*
 * Is this key new? Records it if so.
 *
 * A full set answers 0 for a key it does not hold and counts an overflow, which
 * is the distinction that matters: "this key was never seen" and "this key was
 * seen after the set filled up" are different readings of the same silence, and
 * only one of them means the probe missed something. A repeat of a held key is
 * not an overflow.
 *
 * Called with the controller lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciProbeKeyIsNew(XHCI_PROBE_KEYSET *set, ULONG key)
{
    ULONG i;

    for (i = 0; i < set->Count; i++) {
        if (set->Key[i] == key) {
            return 0;
        }
    }

    if (set->Count >= XHCI_PROBE_KEYS) {
        set->Overflows++;
        return 0;
    }

    set->Key[set->Count] = key;
    set->Count++;
    set->Firings++;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Transfers                                                           */
/* ------------------------------------------------------------------ */

/*
 * Classify one scatter/gather list.
 *
 * The three properties this is here to measure are all *pairs* - ascending or
 * disordered, tiling or gapped, mapped or not - because an absent bit proves
 * nothing and a probe that only recorded the anomalies would report the same
 * empty result whether the anomaly never happened or the probe never ran.
 *
 * Called with the controller lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciProbeFoldSgList(PXHCI_EXTENSION ext,
                                 const USBPORT_SCATTER_GATHER_LIST *sgList,
                                 ULONG bufferLength)
{
    ULONG shape;
    ULONG count;
    ULONG walk;
    ULONG i;
    ULONG ascending;
    ULONG tiles;
    ULONG highs;
    ULONG total;

    if (sgList == NULL) {
        /*
         * Batch 6-0 read both shipping builds and found the argument is
         * `&Transfer->SgList`, an interior pointer computed with `lea`, so this
         * cannot happen on either target. It is classified rather than ignored
         * precisely because a set bit here would refute a both-binaries
         * negative this driver's transfer path is written against.
         */
        return XHCI_PROBE_SG_NULL;
    }

    shape = 0;
    count = sgList->SgElementCount;

    if ((sgList->Flags & 1UL) != 0) {
        shape |= XHCI_PROBE_SG_MAPPED;
        ext->ProbeSgMapped++;
    } else {
        shape |= XHCI_PROBE_SG_UNMAPPED;
    }

    if (count == 0) {
        /* The routine case, not an anomaly: a transfer with
         * TransferBufferLength == 0 is never mapped (batch 6-0), which is every
         * no-data-stage control transfer on the enumeration path. */
        return shape | XHCI_PROBE_SG_EMPTY;
    }

    shape |= (count == 1) ? XHCI_PROBE_SG_SINGLE : XHCI_PROBE_SG_MULTI;
    if (count > ext->ProbeSgElementsMax) {
        ext->ProbeSgElementsMax = count;
    }

    walk = count;
    if (walk > XHCI_PROBE_SG_WALK) {
        walk = XHCI_PROBE_SG_WALK;
        shape |= XHCI_PROBE_SG_TRUNCATED;
    }

    ascending = 1;
    tiles = (sgList->SgElement[0].SgOffset == 0) ? 1 : 0;
    highs = 0;
    total = 0;

    for (i = 0; i < walk; i++) {
        const USBPORT_SCATTER_GATHER_ELEMENT *element;

        element = &sgList->SgElement[i];
        if (element->SgPhysicalAddressHi != 0) {
            highs++;
        }
        if (i > 0) {
            const USBPORT_SCATTER_GATHER_ELEMENT *previous;

            previous = &sgList->SgElement[i - 1];
            if (element->SgOffset <= previous->SgOffset) {
                ascending = 0;
            }
            if (element->SgOffset !=
                previous->SgOffset + previous->SgTransferLength) {
                tiles = 0;
            }
        }
        total += element->SgTransferLength;
    }

    /*
     * **Only a list with something to order carries an ordering verdict.** A
     * single element trivially "ascends", and setting the bit for one would put
     * XHCI_PROBE_SG_ASCENDING in the accumulated shape word of a run in which
     * no ordering was ever observed - which is precisely the reading this probe
     * exists to avoid, one level up from the pairing rule. On the enumeration
     * path that is not a corner case: usbport caps the default pipe at 0x1000
     * and every descriptor read is far smaller, so a Phase 6 run may legitimately
     * see no multi-element list at all, and the honest outcome is then
     * "unmeasured" rather than "confirmed".
     *
     * Tiling has no such problem: for one element it is the real question of
     * whether the list starts at offset 0, so it is answered for any nonempty
     * list.
     */
    if (count > 1) {
        shape |= ascending ? XHCI_PROBE_SG_ASCENDING : XHCI_PROBE_SG_DISORDERED;
    }
    shape |= tiles ? XHCI_PROBE_SG_TILES : XHCI_PROBE_SG_GAPPED;
    shape |= ((sgList->SgElement[0].SgPhysicalAddressLo &
               (XHCI_PAGE_SIZE - 1UL)) == 0)
                 ? XHCI_PROBE_SG_PAGE_ALIGNED
                 : XHCI_PROBE_SG_MIDPAGE;

    if (highs != 0) {
        shape |= XHCI_PROBE_SG_HIGH_DWORD;
        ext->ProbeSgHighDwords += highs;
    }
    if (!ascending) {
        ext->ProbeSgDisordered++;
    }
    if (!tiles) {
        ext->ProbeSgGapped++;
    }
    /*
     * Only meaningful when the whole list was walked: a truncated walk sums a
     * prefix, and calling that a length gap would manufacture the anomaly.
     */
    if (walk == count && total != bufferLength) {
        shape |= XHCI_PROBE_SG_LENGTH_GAP;
        ext->ProbeSgLengthGaps++;
    }

    return shape;
}

/*
 * The key one setup packet is announced under.
 *
 * bmRequestType and bRequest identify the request; wValue's high byte is the
 * descriptor type for GET_DESCRIPTOR and the feature selector's high half
 * elsewhere, which is what separates GET_DESCRIPTOR(Device) from
 * GET_DESCRIPTOR(Hub) - the one design doc 02 needs to see. The low half of
 * wValue is deliberately *not* in the key: it carries the port number on the
 * hub-class port requests, and a key per port would spend the whole set on one
 * hub. The full eight bytes are printed in the line either way.
 *
 * IRQL: any.
 */
static ULONG xhciProbeSetupKey(const XHCI_SETUP_PACKET *setup)
{
    return ((ULONG)setup->bmRequestType << 24) |
           ((ULONG)setup->bRequest << 16) |
           (ULONG)((setup->wValue >> 8) & 0xFFU);
}

/* Called with the controller lock held. IRQL: <= DISPATCH_LEVEL. */
static VOID xhciProbeFoldTransfer(PXHCI_EXTENSION ext,
                                  const USBPORT_TRANSFER_PARAMETERS *parameters,
                                  const USBPORT_SCATTER_GATHER_LIST *sgList,
                                  XHCI_PROBE_REPORT *report)
{
    ULONG shape;

    ext->ProbeTransfers++;

    shape = xhciProbeFoldSgList(ext, sgList, parameters->TransferBufferLength);

    shape |= ((parameters->TransferFlags & 1UL) != 0) ? XHCI_PROBE_XFER_IN
                                                      : XHCI_PROBE_XFER_OUT;
    if (parameters->TransferBufferLength == 0) {
        shape |= XHCI_PROBE_XFER_ZERO_LENGTH;
    }
    if (parameters->IsTransferSplited != 0) {
        shape |= XHCI_PROBE_XFER_SPLIT;
        ext->ProbeSplitTransfers++;
    }
    /*
     * **Type and recipient, not type alone**, and the first Win2000 run is why:
     * it read eleven "hub-class setups" on a bus with no hub, because the audio
     * class driver's requests are class-type with an *interface* recipient. Any
     * class driver produces those. Only design doc 02 section 2's hub traffic -
     * GET_DESCRIPTOR(Hub) at `0xA0`, class with a **device** recipient, and
     * SET_FEATURE(PORT_RESET) at `0x23`, class with an **other** recipient -
     * carries the topology, and usbport answers the *root* hub itself through
     * the RH_ callbacks, so a nonzero hub count means a real hub is on the bus.
     */
    if ((parameters->SetupPacket.bmRequestType & 0x60U) == 0x20U) {
        ULONG recipient;

        shape |= XHCI_PROBE_XFER_CLASS_SETUP;
        ext->ProbeClassSetups++;

        recipient = (ULONG)(parameters->SetupPacket.bmRequestType & 0x1FU);
        if (recipient == 0 || recipient == 3) {
            shape |= XHCI_PROBE_XFER_HUB_SETUP;
            ext->ProbeHubClassSetups++;
        }
    }

    report->Event = 0;
    report->Shape = shape;
    report->NewBits = shape & ~ext->ProbeSgShape;
    report->Detail = xhciProbeSetupKey(&parameters->SetupPacket);
    report->NewKey = xhciProbeKeyIsNew(&ext->ProbeSetups, report->Detail);
    report->Sequence = ext->ProbeTransfers;
    report->DumpElements = 0;

    if (report->NewBits != 0) {
        ext->ProbeSgShape |= shape;
        ext->ProbeSgShapeFirings++;
    }
    report->Announce = (report->NewBits != 0 || report->NewKey != 0) ? 1 : 0;

    /*
     * The element dump is many lines per announcement where the shape line is
     * one, so it keeps a budget of its own. In the extension rather than in a
     * per-site static: usbport zeroes the extension on every StartController,
     * so this is four dumps *per start* instead of four for the life of the
     * driver load - which is the difference between evidence that survives
     * Win98's idle suspend/resume cycling and evidence that does not.
     */
    if (report->Announce != 0 && sgList != NULL &&
        sgList->SgElementCount != 0 && ext->ProbeSgDumps < XHCI_PROBE_SG_DUMPS) {
        ext->ProbeSgDumps++;
        report->DumpElements = 1;
    }
}

/*
 * **`XHCI_DBG_TRACE`, not `DBG`.** This block calls `XhciDbgCallback` directly
 * rather than through a macro - it wants the callback line that carries
 * `irql=` - so it has to test the same thing `src/xhci_dbg.h` gates that
 * function on. Since task 13-L.1 the live trace is the `qemu` flavour's alone,
 * and `debug` compiles none of it.
 */
#ifdef XHCI_DBG_TRACE

/* IRQL: <= DISPATCH_LEVEL, controller lock NOT held. */
static VOID xhciProbeReportTransfer(
    const XHCI_PROBE_REPORT *report,
    const USBPORT_TRANSFER_PARAMETERS *parameters,
    const USBPORT_SCATTER_GATHER_LIST *sgList)
{
    if (report->Announce == 0) {
        return;
    }

    /*
     * XhciDbgCallback rather than a value line because it is the one that
     * carries `irql=`, which is a deliverable of this task in its own right -
     * task 6-V.1 asks for the callback IRQL, and SubmitTransfer's is the
     * DISPATCH_LEVEL-under-MiniportSpinLock the whole device layer is written
     * against. Unbounded on purpose: the gate above is semantic, so this
     * prints at most once per new shape and once per new setup key per start.
     */
    XhciDbgCallback("probe.xfer", report->Sequence, report->Shape,
                    report->NewBits);
    XHCI_DBG_WORDS("probe.xfer params", (const ULONG *)parameters,
                   sizeof(USBPORT_TRANSFER_PARAMETERS) / sizeof(ULONG));

    if (sgList == NULL) {
        XHCI_DBG_TEXT("probe.xfer sg list is NULL - refutes batch 6-0");
        return;
    }

    /* The header is Flags, CurrentVa, MappedSystemVa, SgElementCount - four
     * words, and the whole of what precedes the elements. */
    XHCI_DBG_WORDS("probe.xfer sg head", (const ULONG *)sgList, 4);

    if (report->DumpElements != 0) {
        ULONG count;
        ULONG i;

        count = sgList->SgElementCount;
        if (count > XHCI_PROBE_SG_DUMP_ELEMENTS) {
            count = XHCI_PROBE_SG_DUMP_ELEMENTS;
        }
        for (i = 0; i < count; i++) {
            /*
             * Six words per element - PA low, PA high, reserved, length,
             * offset, reserved - which is the raw evidence
             * docs/usb-xhci-info/usbport-miniport-interface.md's "Phase 6 obligation" asks
             * for, printed rather than summarised so the ABI record can be
             * updated from the log itself.
             */
            XHCI_DBG_WORDS("probe.xfer sg elem",
                           (const ULONG *)&sgList->SgElement[i],
                           sizeof(USBPORT_SCATTER_GATHER_ELEMENT) /
                               sizeof(ULONG));
        }
    }
}

#endif /* XHCI_DBG_TRACE */

/* IRQL: <= DISPATCH_LEVEL. */
VOID XhciProbeTransfer(PXHCI_EXTENSION ext,
                       const USBPORT_TRANSFER_PARAMETERS *parameters,
                       const USBPORT_SCATTER_GATHER_LIST *sgList)
{
    XHCI_PROBE_REPORT report;
    KIRQL oldIrql;

    if (!xhciProbeExtensionUsable(ext) || parameters == NULL) {
        return;
    }

    XhciControllerLockAcquire(&oldIrql);
    xhciProbeFoldTransfer(ext, parameters, sgList, &report);
    XhciControllerLockRelease(oldIrql);

#ifdef XHCI_DBG_TRACE
    xhciProbeReportTransfer(&report, parameters, sgList);
#endif
}

/* ------------------------------------------------------------------ */
/* Endpoints                                                           */
/* ------------------------------------------------------------------ */

/*
 * Record one raw `HubAddr`/`PortNumber` pair. Returns nonzero if the pair had
 * not been seen since this controller started.
 *
 * Every property block is recorded, including the 0xFFFF ones. That is the same
 * pairing rule the shape bits follow: a run in which no pair but 0xFFFF appears
 * has *measured* that no endpoint was ever told about a transaction translator,
 * where an empty table would only mean the fold never ran.
 *
 * Called with the controller lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciProbeFoldTtPair(PXHCI_EXTENSION ext,
                                 const USBPORT_ENDPOINT_PROPERTIES *properties,
                                 ULONG speedBits)
{
    XHCI_PROBE_TT_ENTRY *entry;
    ULONG pair;
    ULONG address;
    ULONG isNew;
    ULONG i;

    ext->ProbeTtObservations++;

    pair = ((ULONG)properties->HubAddr << 16) | (ULONG)properties->PortNumber;

    entry = NULL;
    for (i = 0; i < ext->ProbeTtPairs.Count; i++) {
        if (ext->ProbeTtPairs.Entry[i].Pair == pair) {
            entry = &ext->ProbeTtPairs.Entry[i];
            break;
        }
    }

    isNew = 0;
    if (entry == NULL) {
        if (ext->ProbeTtPairs.Count >= XHCI_PROBE_TT_PAIRS) {
            ext->ProbeTtPairs.Dropped++;
            return 0;
        }
        entry = &ext->ProbeTtPairs.Entry[ext->ProbeTtPairs.Count];
        entry->Pair = pair;
        ext->ProbeTtPairs.Count++;
        isNew = 1;
    }

    entry->Observations++;
    entry->Speeds |= speedBits;

    address = (ULONG)properties->DeviceAddress;
    entry->Addresses |= (address < 31) ? (1UL << address) : 0x80000000UL;

    return isNew;
}

/*
 * Classify one endpoint-properties block.
 *
 * The two bits Phase 7b is waiting for are the TT pair. `HubAddr` is 0xFFFF
 * when the device has no transaction translator, which is every device on a
 * root port, so a set XHCI_PROBE_EP_TT is the first runtime evidence that
 * usbport names the **High-Speed TT ancestor** a device sits behind - not its
 * immediate parent, and not a route (batch 7a-0 corrected). Its absence across
 * a whole run is the measurement design doc 02 section 4 asks for, not a gap in
 * the log.
 *
 * Called with the controller lock held. IRQL: <= DISPATCH_LEVEL.
 */
static ULONG xhciProbeFoldProperties(
    PXHCI_EXTENSION ext,
    const USBPORT_ENDPOINT_PROPERTIES *properties,
    ULONG *newPair)
{
    ULONG shape;

    shape = 0;

    if (properties->HubAddr == USBPORT_NO_TT_HUB) {
        shape |= XHCI_PROBE_EP_NO_TT;
    } else {
        shape |= XHCI_PROBE_EP_TT;
        ext->ProbeEpWithTt++;
    }
    if (properties->PortNumber != 0) {
        shape |= XHCI_PROBE_EP_PORT;
    }

    if (properties->DeviceSpeed == UsbLowSpeed) {
        shape |= XHCI_PROBE_EP_LOW;
    } else if (properties->DeviceSpeed == UsbFullSpeed) {
        shape |= XHCI_PROBE_EP_FULL;
    } else if (properties->DeviceSpeed == UsbHighSpeed) {
        shape |= XHCI_PROBE_EP_HIGH;
    } else {
        shape |= XHCI_PROBE_EP_SPEED_OTHER;
    }

    shape |= (properties->DeviceAddress == 0) ? XHCI_PROBE_EP_ADDRESS_ZERO
                                              : XHCI_PROBE_EP_ADDRESSED;
    shape |= (properties->TransferType == USBPORT_TRANSFER_TYPE_CONTROL)
                 ? XHCI_PROBE_EP_CONTROL
                 : XHCI_PROBE_EP_NON_CONTROL;
    shape |= (properties->BufferLength != 0) ? XHCI_PROBE_EP_BUFFER
                                             : XHCI_PROBE_EP_NO_BUFFER;
    if (properties->Period != 0) {
        shape |= XHCI_PROBE_EP_PERIOD;
    }

    *newPair = xhciProbeFoldTtPair(ext, properties,
                                   shape & XHCI_PROBE_EP_SPEED_MASK);

    return shape;
}

/* Called with the controller lock held. IRQL: <= DISPATCH_LEVEL. */
static VOID xhciProbeFoldEndpoint(
    PXHCI_EXTENSION ext,
    ULONG event,
    const USBPORT_ENDPOINT_PROPERTIES *properties,
    const XHCI_ENDPOINT *endpoint,
    ULONG detail,
    XHCI_PROBE_REPORT *report)
{
    ULONG shape;
    ULONG key;
    ULONG newPair;

    if (event < XHCI_PROBE_EVENT_COUNT) {
        ext->ProbeEpEvents[event]++;
    }

    shape = 0;
    key = (event << 24) | ((detail & 0xFFUL) << 16);
    newPair = 0;

    if (properties != NULL) {
        shape = xhciProbeFoldProperties(ext, properties, &newPair);
        key |= ((ULONG)properties->DeviceAddress & 0xFFUL) << 8;
        key |= (ULONG)properties->EndpointAddress & 0xFFUL;
    } else if (endpoint != NULL &&
               endpoint->Signature == XHCI_ENDPOINT_SIGNATURE) {
        /* No properties block: the callback names an endpoint extension
         * instead, so the key is what identifies it on the hardware side. */
        key |= (endpoint->SlotId & 0xFFUL) << 8;
        key |= endpoint->Dci & 0xFFUL;
    }

    report->Event = event;
    report->Shape = shape;
    report->NewBits = shape & ~ext->ProbeEpShape;
    report->Detail = detail;
    report->NewKey = xhciProbeKeyIsNew(&ext->ProbeEndpoints, key);
    report->Sequence = key;
    report->DumpElements = 0;

    if (report->NewBits != 0) {
        ext->ProbeEpShape |= shape;
        ext->ProbeEpShapeFirings++;
    }
    /*
     * A new pair announces on its own, and it has to: the endpoint key is
     * (event, state, device address, endpoint address), and the two-tier child
     * this exists to read shares every one of those with the one-tier child -
     * both open EP0 at address 0. Without this term a traced build would print
     * the block for whichever arrived first and stay silent for the other, which
     * is the reading the task is after.
     */
    report->Announce =
        (report->NewBits != 0 || report->NewKey != 0 || newPair != 0) ? 1 : 0;
}

/* IRQL: <= DISPATCH_LEVEL. */
VOID XhciProbeEndpoint(PXHCI_EXTENSION ext,
                       ULONG event,
                       const USBPORT_ENDPOINT_PROPERTIES *properties,
                       const XHCI_ENDPOINT *endpoint,
                       ULONG detail)
{
    XHCI_PROBE_REPORT report;
    KIRQL oldIrql;

    if (!xhciProbeExtensionUsable(ext)) {
        return;
    }

    XhciControllerLockAcquire(&oldIrql);
    xhciProbeFoldEndpoint(ext, event, properties, endpoint, detail, &report);
    XhciControllerLockRelease(oldIrql);

#ifdef XHCI_DBG_TRACE
    if (report.Announce != 0) {
        XhciDbgCallback("probe.ep", report.Sequence, report.Shape,
                        report.NewBits);
        if (properties != NULL) {
            /*
             * The whole 64-byte block, which is design doc 02 section 4's
             * "full dumps of every endpoint-open parameter block" - the
             * question being whether any field carries hub address, port or
             * parent speed, and a summary cannot answer it for a field nobody
             * thought to summarise.
             */
            XHCI_DBG_WORDS("probe.ep props", (const ULONG *)properties,
                           sizeof(USBPORT_ENDPOINT_PROPERTIES) /
                               sizeof(ULONG));
        }
    }
#endif
}
