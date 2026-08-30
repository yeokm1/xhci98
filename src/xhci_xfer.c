/*
 * xhci_xfer.c - the control-transfer engine: TD construction, the pending-
 * transfer queue, and what a Transfer Event means to one usbport transfer.
 *
 * Pure computation plus stores into caller-supplied common-buffer memory: no
 * MMIO, no DDK calls, no usbport services, no IRQL dependencies, so it builds
 * and runs on the host under XHCI_HOST_TEST (docs/contributing/design/03-host-unit-tests.md).
 * test/test_xfer.c is the regression suite. Bit positions come from
 * docs/usb-xhci-info/xhci-data-structures.md section 7.
 *
 * Nothing here rings a doorbell, issues a command, or completes a transfer back
 * to usbport. It decides; the caller acts, after it returns and after the
 * controller lock is dropped (docs/contributing/design/05-locking-model.md section 7).
 *
 * Three rules hold throughout, and each one exists because its obvious
 * alternative is wrong:
 *
 *   - **A control transfer is two or three TDs, not one.** "Control transfers
 *     require two or three TDs to define them: a Setup Stage TD followed by an
 *     Status Stage TD, if a data stage is required for the transfer an optional
 *     Data Stage TD will reside between the Setup Stage and Status Stage TDs"
 *     (spec 6.4.1.2, p.430). They are published as one group so the xHC can
 *     never begin a control transfer whose Status Stage TRB does not exist yet.
 *   - **IOC belongs only to the Status Stage TRB.** "Note: The IOC flag should
 *     only be set in the Status Stage TRB of a Control transfer" (p.430). So a
 *     successful control transfer produces exactly one Transfer Event, naming
 *     the last TRB of the group, and that one event retires all three TDs -
 *     XhciRingRetireTd jumps the dequeue past the matched TD's tail rather than
 *     walking one TD at a time.
 *   - **A transfer's byte count is not "requested - residual".** That holds for
 *     a single-TRB TD and nothing else: "For multi-TRB TDs, if ED = `0`, the
 *     TRB Transfer Length only reflects the number of bytes transferred for the
 *     buffer associated with the Transfer TRB pointed to by the Transfer Event,
 *     not the total bytes transferred for the TD" (Table 6-39 note, p.441). The
 *     arithmetic that is right is spelled out at p.175 and lives in
 *     XhciRingSumTrbLengths.
 *
 * C89 only. IRQL: every function in this file is pure - it touches only memory
 * the caller owns and calls no kernel service - so every one of them is safe at
 * any IRQL. In practice every caller is at DISPATCH_LEVEL (the event DPC and the
 * slot layer under the controller lock), and where a function's own tag names
 * DISPATCH_LEVEL it is recording that call site, not narrowing this blanket.
 */

#include "xhci_xfer.h"

/* ------------------------------------------------------------------ */
/* 6-A.3: completion codes                                             */
/* ------------------------------------------------------------------ */

/* The XHCI_USBD_STATUS_* set moved to src/xhci_xfer.h in batch 6-B: the slot
 * layer completes transfers of its own - cancelled by a teardown, failed by a
 * command chain - and two files deciding independently what "cancelled" is
 * worth is how a status set drifts. */

static VOID xhciXferCodeSet(PXHCI_XFER_CODE info,
                            ULONG class_,
                            LONG usbdStatus,
                            ULONG residualIsBytes,
                            ULONG fatal)
{
    info->Class = class_;
    info->UsbdStatus = usbdStatus;
    info->ResidualIsBytes = residualIsBytes;
    info->Fatal = fatal;
    /* Set by the one arm that carries it, after this call. Cleared here so that
     * every other code answers the question without each arm restating it. */
    info->SlotFatal = 0;
}

ULONG XhciXferCodeInfo(ULONG completionCode, PXHCI_XFER_CODE info)
{
    if (info == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }
    /* Set first, so a caller that ignores the return value still sees INVALID
     * rather than whatever was on its stack. */
    xhciXferCodeSet(info, XHCI_XFER_CC_INVALID,
                    XHCI_USBD_STATUS_INTERNAL_HC_ERROR, 0, 0);

    /* Table 6-90's two vendor ranges carry their own default reading: unknown
     * information is Success, an unknown error is Undefined Error. This driver
     * issues no vendor command and has no vendor knowledge, so "unknown" is the
     * only reading available for either.
     *
     * **Audit round 8 found the error range stopping half way through that
     * reading.** It mapped to Undefined Error's *status* and not to its
     * *treatment*: "If software does not recognize the code, it shall interpret
     * this range of vendor defined values as a Undefined Error condition"
     * (p.470), and an Undefined Error "shall be treated as a fatal error by
     * software" (p.469). Interpreting it as that condition and then handling it
     * more gently than that condition is not an interpretation. So the range
     * carries the same `Fatal` the code it is interpreted as carries, and the
     * two now agree by construction rather than by a comment saying they do. */
    if (completionCode >= XHCI_CC_VENDOR_INFO_MIN &&
        completionCode <= XHCI_CC_VENDOR_INFO_MAX) {
        xhciXferCodeSet(info, XHCI_XFER_CC_SUCCESS,
                        XHCI_USBD_STATUS_SUCCESS, 1, 0);
        return XHCI_XFER_OK;
    }
    if (completionCode >= XHCI_CC_VENDOR_ERROR_MIN &&
        completionCode <= XHCI_CC_VENDOR_ERROR_MAX) {
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_INTERNAL_HC_ERROR, 1, 1);
        return XHCI_XFER_OK;
    }

    switch (completionCode) {
    case XHCI_CC_SUCCESS:
        xhciXferCodeSet(info, XHCI_XFER_CC_SUCCESS,
                        XHCI_USBD_STATUS_SUCCESS, 1, 0);
        return XHCI_XFER_OK;

    /*
     * Short Packet is a *successful* completion carrying a length: the device
     * sent less than was asked for. Whether that is an error is the URB's
     * business, not the miniport's - USBD_SHORT_TRANSFER_OK lives in flags this
     * layer never sees, and usbport sets it on its own enumeration requests
     * (ReactOS usbport urb.c). Report Success with the actual byte count and
     * let the layer that owns the flag decide.
     */
    case XHCI_CC_SHORT_PACKET:
        xhciXferCodeSet(info, XHCI_XFER_CC_SHORT,
                        XHCI_USBD_STATUS_SUCCESS, 1, 0);
        return XHCI_XFER_OK;

    case XHCI_CC_STALL:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_STALL_PID, 1, 0);
        return XHCI_XFER_OK;

    /* No handshake, a CRC failure, a timeout, or a split transaction that went
     * wrong: from the far side of the wire, a device that did not answer. */
    case XHCI_CC_USB_TRANSACTION_ERROR:
    case XHCI_CC_NO_PING_RESPONSE:
    case XHCI_CC_SPLIT_TRANSACTION:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_DEV_NOT_RESPONDING, 1, 0);
        return XHCI_XFER_OK;

    /* The device sent more than the endpoint's Max Packet Size allowed. */
    case XHCI_CC_BABBLE:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_BUFFER_OVERRUN, 1, 0);
        return XHCI_XFER_OK;

    /* The xHC's own data path over- or under-ran. */
    case XHCI_CC_DATA_BUFFER_ERROR:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_DATA_OVERRUN, 1, 0);
        return XHCI_XFER_OK;

    /*
     * TRB Error is this driver's own fault - the controller read a TRB it
     * could not act on - and Invalid Stream ID is a condition with no USB-level
     * equivalent (this driver opens no streams at all, so it means the
     * controller and the driver disagree about the endpoint). Both are "the host
     * controller layer failed", which is what INTERNAL_HC_ERROR says, and
     * neither is fatal: TRB Error puts the endpoint in Error and
     * `xhciEpRecoveryNeeded` repositions it.
     *
     * **Incompatible Device and Undefined Error used to share this arm, and
     * audit round 8 found that both of them carry an instruction it was
     * dropping.** They are still reported to usbport as INTERNAL_HC_ERROR -
     * neither has a USB-level equivalent - but the recovery each one names is
     * not this arm's.
     */
    case XHCI_CC_TRB_ERROR:
    case XHCI_CC_INVALID_STREAM_ID:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_INTERNAL_HC_ERROR, 1, 0);
        return XHCI_XFER_OK;

    /*
     * "This error may be returned by any command or transfer, and is fatal as
     * far as the Slot is concerned. Software shall issue a Disable Slot Command
     * to recover" (Table 6-90, p.468).
     *
     * That is a `shall` naming a specific command, and no amount of endpoint
     * recovery is it: a Reset Endpoint plus Set TR Dequeue Pointer leaves the
     * slot enabled and the device addressed, which is the state the controller
     * has just said it cannot successfully access. `SlotFatal` carries it to
     * `xhciDevSlotFatalEvent`, which owes the Disable Slot through the ordinary
     * device teardown rather than inventing a second route to it. **Audit round
     * 9 found this sentence naming `xhciEpRecoveryNeeded`** - the function called
     * beside it, which does the endpoint's half and never reads this flag.
     *
     * **"Any command or transfer" is the whole clause, and round 9 found the
     * command half missing.** A Command Completion Event carrying this code went
     * to the slot state machine, which reduces every code to success/non-success,
     * so a Configure Endpoint answered with 22 became one failed endpoint record
     * and left the slot in service. `XhciSlotCommandSlotFatal` is the command
     * path's route into the same teardown.
     *
     * It is deliberately **not** `Fatal`: that flag escalates to a controller
     * invalidation, which would answer one unusable device by resetting the
     * controller every other device is on. The spec scopes this one to the Slot
     * and so does this driver.
     */
    case XHCI_CC_INCOMPATIBLE_DEVICE:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_INTERNAL_HC_ERROR, 1, 0);
        info->SlotFatal = 1;
        return XHCI_XFER_OK;

    /*
     * "May be reported by an event when other error codes do not apply. The
     * conditions that assert this condition code are xHC implementation
     * specific ... An Undefined Error shall be treated as a fatal error by
     * software" (Table 6-90, p.469).
     *
     * The last sentence is the whole arm. There is by construction nothing to
     * diagnose - the code exists for conditions the table declines to enumerate -
     * so the only defined response is the one the sentence names, and a driver
     * that retried the endpoint instead would be resuming against a fault whose
     * cause it cannot see.
     */
    case XHCI_CC_UNDEFINED_ERROR:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_INTERNAL_HC_ERROR, 1, 1);
        return XHCI_XFER_OK;

    /*
     * Event Lost is the controller saying it dropped events. Every completion
     * in this engine is matched by identity, so a dropped event is never a late
     * one: the transfer it belonged to has nothing left that can resolve it.
     * That is a controller-level failure, not this transfer's error
     * (docs/contributing/implementation-invariants.md, "Fatal Errors").
     */
    case XHCI_CC_EVENT_LOST:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_INTERNAL_HC_ERROR, 1, 1);
        return XHCI_XFER_OK;

    /*
     * 26-28 arrive after a Stop Endpoint command: software owns the ring and
     * chose to stop it, so the transfers on it are canceled rather than failed.
     * Stopped - Length Invalid says so outright, and Stopped - Short Packet
     * reports the **EDTLA** in the length field rather than a residual
     * (Table 6-38, p.440).
     *
     * `ResidualIsBytes` is 0 for both, and for 28 that is a statement about the
     * *residual arithmetic* rather than about the field being unusable - a
     * correction the third review round of batch 7a-B forced. The EDTLA is a
     * running total of the bytes the TD has moved, maintained per endpoint by
     * the xHC whether or not software ever places an Event Data TRB (4.11.5.2
     * p.209-210), so it is a byte count; what it is not is something
     * `sum - residual` may be applied to. `XhciXferQueueStopped` is the one
     * caller that reads it, and it takes it directly.
     */
    case XHCI_CC_STOPPED:
        xhciXferCodeSet(info, XHCI_XFER_CC_CANCELED,
                        XHCI_USBD_STATUS_CANCELED, 1, 0);
        return XHCI_XFER_OK;
    case XHCI_CC_STOPPED_LENGTH_INVALID:
    case XHCI_CC_STOPPED_SHORT_PACKET:
        xhciXferCodeSet(info, XHCI_XFER_CC_CANCELED,
                        XHCI_USBD_STATUS_CANCELED, 0, 0);
        return XHCI_XFER_OK;

    /*
     * Everything else is refused, and **one of the codes that falls through here
     * is not the family it was labelled as until audit round 9**.
     * `XHCI_CC_EP_NOT_ENABLED` (12) is a *Transfer Event*: Table 6-90 p.467
     * asserts it "if a
     * doorbell is rung for an endpoint that is in the Disabled state", and 4.7
     * p.143 says the xHC "should generate a Transfer Event TRB with the TRB
     * Pointer, TRB Transfer Length, Event Data (ED) fields set to '0'" carrying
     * the Slot and Endpoint IDs. It is refused for the same reason Ring Underrun
     * and Ring Overrun are - there is no TRB pointer for the per-TD matcher to
     * resolve, and offering it a zero would land on the ring's base - and not
     * because it belongs to the command ring.
     *
     * Refusing it means nothing *here* acts on it. Recovery is not absent, it is
     * **delayed**: the TD stays queued until usbport's timeout, and the Stop
     * Endpoint that cancellation issues reads the Endpoint Context back, finds
     * it Disabled - which is the condition code 12 reports - and takes
     * `xhciEpStopped`'s Disabled branch, which raises `XHCI_EPQ_NO_CONTEXT` and
     * owes the Configure Endpoint that puts the context back. That is a
     * deliberate deviation with its reasons and its reopening measurement in
     * docs/contributing/implementation-invariants.md, "Fatal Errors"; audit
     * round 10 corrected an earlier wording of it that said nothing recovered.
     */
    default:
        break;
    }
    return XHCI_XFER_BAD_PARAM;
}

/* ------------------------------------------------------------------ */
/* 6-A.1: building a control transfer                                  */
/* ------------------------------------------------------------------ */

/* bmRequestType bit 7, the Data Transfer Direction (USB2 Table 9-2). */
#define XHCI_SETUP_DTD_IN 0x80

/*
 * EP0's Max Packet Size is one of exactly four values (USB2 9.6.1
 * bMaxPacketSize0: 8, 16, 32 or 64), and the TD Size arithmetic divides by it.
 * Refusing anything else here is cheap and turns a wrong endpoint context into
 * a refused transfer instead of a silently wrong TD Size field, which the
 * hardware treats as a hint and no test would ever catch.
 */
static ULONG xhciXferMps0Valid(ULONG mps)
{
    return (mps == 8 || mps == 16 || mps == 32 || mps == 64) ? 1 : 0;
}

/*
 * TD Size for TRB n of a TD, spec 4.11.2.4:
 *
 *   TD Packet Count      = ROUNDUP(TD Transfer Size / Max Packet Size)
 *   Packets Transferred  = ROUNDDOWN(sum of lengths 1..n / Max Packet Size)
 *   TD Size(n)           = MIN(31, TD Packet Count - Packets Transferred(n))
 *
 * and 0 for the last TRB of the TD, which the caller writes over the value
 * returned here. `lengthSum` is inclusive of TRB n.
 */
static ULONG xhciXferTdSize(ULONG totalLength, ULONG lengthSum, ULONG mps)
{
    ULONG packetCount;
    ULONG transferred;
    ULONG remaining;

    packetCount = (totalLength + mps - 1) / mps;
    transferred = lengthSum / mps;
    if (transferred >= packetCount) {
        return 0;
    }
    remaining = packetCount - transferred;
    if (remaining > XHCI_TRB_TD_SIZE_MAX) {
        return XHCI_TRB_TD_SIZE_MAX;
    }
    return remaining;
}

/*
 * One physical fragment as one or more TRBs, split at every 64 KB physical
 * boundary. "If a physical data buffer spans a 64KB boundary, software shall
 * chain multiple TRBs to describe the buffer" (spec 6.4.1 note) - and keeping
 * the length under 64 KB is *not* sufficient, because an SG element may start
 * anywhere. Violations are silent data corruption on some controllers.
 *
 * usbport's elements are page-granular on both shipping builds (Phase 3 task
 * 10) and a 4 KB page cannot span a 64 KB boundary, so on the measured path
 * this never splits. It is done anyway: page granularity is a fact about two
 * binaries, not a term of the ABI.
 */
typedef struct _XHCI_XFER_BUILD_STATE {
    XHCI_TRB *Out;
    ULONG Capacity;
    ULONG Count;            /* TRBs written so far, whole group */
    ULONG DataFirst;
    ULONG DataCount;
    ULONG LengthSum;        /* bytes described by the data TRBs so far */
    ULONG TotalLength;
    ULONG MaxPacketSize;
    ULONG DirectionIn;
    /* Every TRB is an ordinary Normal TRB rather than the first being a Data
     * Stage TRB - the difference between a control transfer's data stage and a
     * non-control transfer's whole TD (task 7a-A.2 for interrupt, 8-A.1 for
     * bulk; the two are one shape here). */
    ULONG NormalOnly;
} XHCI_XFER_BUILD_STATE;

static ULONG xhciXferEmitData(XHCI_XFER_BUILD_STATE *state,
                              ULONG physicalAddress,
                              ULONG length)
{
    XHCI_TRB *trb;
    ULONG control;

    if (state->Count >= state->Capacity ||
        state->DataCount >= XHCI_XFER_MAX_DATA_TRBS) {
        return XHCI_XFER_TOO_MANY_TRBS;
    }

    trb = &state->Out[state->Count];
    XhciTrbClear(trb);
    trb->Param0 = physicalAddress;
    trb->Param1 = 0;                    /* no 64-bit DMA, ever (AGENTS.md) */

    state->LengthSum += length;
    trb->Status = (length & XHCI_TRB_LENGTH_MASK) |
                  XHCI_TRB_TD_SIZE(xhciXferTdSize(state->TotalLength,
                                                  state->LengthSum,
                                                  state->MaxPacketSize)) |
                  XHCI_TRB_INTERRUPTER(0);

    /*
     * The first data TRB is a Data Stage TRB and carries the direction for the
     * whole TD ("The Direction (DIR) flag in the Data Stage TRB defines the
     * transfer direction for all TRBs in the Data Stage TD", p.193); every
     * later one is an ordinary Normal TRB, which takes its direction from the
     * preceding Data Stage TRB (p.191).
     */
    if (state->DataCount == 0 && !state->NormalOnly) {
        control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_DATA_STAGE);
        if (state->DirectionIn) {
            control |= XHCI_TRB_DIR_IN;
        }
    } else {
        /*
         * A Normal TRB has **no direction field at all** (Table 6-22): the
         * transfer direction of a non-control endpoint is the Endpoint Context's
         * EP Type, fixed when the endpoint was configured. So an interrupt or
         * bulk TD is built from these alone and carries no direction bit
         * anywhere - which is why the builder checks usbport's against the
         * endpoint's rather than encoding either.
         */
        control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL);
    }

    /*
     * ISP on IN TRBs, so a device that answers with less than was asked for
     * generates a Transfer Event carrying the residual instead of ending the
     * transfer silently. It is set on *every* IN data TRB, the last included:
     * a short packet may land on any of them, and only the TRB it lands on
     * reports it. The redundancy the spec notes at p.176 - ISP beside IOC on
     * the same TRB being pointless - is accepted where it occurs: a control
     * transfer's IOC is the Status Stage TRB's alone, but the interrupt and
     * bulk paths share this emitter and XhciXferBuildNormal sets IOC on its last
     * data TRB, so there ISP and IOC do coincide and the event is simply
     * generated once either way.
     */
    if (state->DirectionIn) {
        control |= XHCI_TRB_ISP;
    }
    control |= XHCI_TRB_CH;             /* provisional: cleared on the last */
    trb->Control = control;

    state->Count++;
    state->DataCount++;
    return XHCI_XFER_OK;
}

static ULONG xhciXferEmitFragment(XHCI_XFER_BUILD_STATE *state,
                                  ULONG physicalAddress,
                                  ULONG length)
{
    ULONG chunk;
    ULONG toBoundary;
    ULONG status;

    /* A fragment that runs off the top of the 32-bit address space is not
     * something to split - it is an address this driver may not use at all. */
    if (length == 0 || (length - 1) > (0xFFFFFFFFUL - physicalAddress)) {
        return XHCI_XFER_SG_MISMATCH;
    }

    while (length > 0) {
        /* Bytes from here to the next 64 KB boundary. The subtraction is
         * deliberately modular: at 0xFFFF0000 the boundary is 0x100000000,
         * which wraps to 0, and 0 - 0xFFFF0000 is 0x10000 - the right answer.
         * The check above is what guarantees the walk cannot pass it. */
        toBoundary = ((physicalAddress | 0xFFFFUL) + 1UL) - physicalAddress;
        chunk = (length < toBoundary) ? length : toBoundary;

        status = xhciXferEmitData(state, physicalAddress, chunk);
        if (status != XHCI_XFER_OK) {
            return status;
        }
        physicalAddress += chunk;
        length -= chunk;
    }
    return XHCI_XFER_OK;
}

/*
 * Walk the whole SG list in SgOffset order, emitting data TRBs.
 *
 * Ordering versus SgOffset is the one thing about this list that reading
 * usbport's code could not settle, and task 6-V.1 is the probe that measures it
 * (docs/usb-xhci-info/usbport-miniport-interface.md). Selecting each element by its offset
 * rather than trusting array order is the defensive answer the probe can only
 * confirm: it is correct whichever order the elements arrive in, and it turns a
 * list that does not tile the buffer into a refusal instead of a transfer that
 * moves the right number of bytes from the wrong places.
 *
 * The scan is quadratic in the element count, which is bounded by
 * XHCI_XFER_MAX_DATA_TRBS above - at most 32 elements for a control transfer,
 * and the cursor advances strictly, so no element can be selected twice.
 */
static ULONG xhciXferEmitSgList(XHCI_XFER_BUILD_STATE *state,
                                const USBPORT_SCATTER_GATHER_LIST *sgList)
{
    ULONG cursor;
    ULONG consumed;
    ULONG i;
    ULONG status;

    if (sgList == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }
    /*
     * The upper bound is checked **before the first element is read**, and that
     * position is the whole point of it: the selection scan below indexes
     * `SgElement[i]` for every i below the count, in memory usbport owns and
     * sized, so a count this driver has not agreed to is a read past the end of
     * somebody else's allocation rather than a slow loop. `xhciXferEmitData`'s
     * own cap cannot stand in for it - that one fires only once a TRB is being
     * written, by which time the scan has already run.
     *
     * A mutation deleting this line still returns XHCI_XFER_TOO_MANY_TRBS for an
     * over-long *well formed* list, because the per-TRB cap catches it second.
     * What it does not preserve is that the refusal happened before anything was
     * read or written, which is what test/test_xfer.c asserts on instead of the
     * status.
     *
     * There is deliberately no "empty list" case beside it: an empty list for a
     * transfer that has bytes to move already comes out of the walk as
     * XHCI_XFER_SG_MISMATCH, and a separate early return for it was measurably
     * dead. A transfer with *nothing* to move never reaches here at all - the
     * caller skips the Data Stage TD entirely, which is the routine enumeration
     * case (batch 6-0).
     */
    if (sgList->SgElementCount > XHCI_XFER_MAX_DATA_TRBS) {
        return XHCI_XFER_TOO_MANY_TRBS;
    }

    cursor = 0;
    consumed = 0;
    while (cursor < state->TotalLength) {
        const USBPORT_SCATTER_GATHER_ELEMENT *element;

        element = NULL;
        for (i = 0; i < sgList->SgElementCount; i++) {
            if (sgList->SgElement[i].SgOffset == cursor) {
                element = &sgList->SgElement[i];
                break;
            }
        }
        if (element == NULL) {
            return XHCI_XFER_SG_MISMATCH;   /* a gap, or a short list */
        }
        /*
         * usbport stores whatever the HAL returned; it does not mask the high
         * DWORD. The zero is inherited from its adapter's declared 32-bit
         * width, so it is checked here rather than assumed
         * (docs/contributing/implementation-invariants.md, "DMA Addresses").
         */
        if (element->SgPhysicalAddressHi != 0) {
            return XHCI_XFER_SG_HIGH_ADDRESS;
        }
        if (element->SgTransferLength == 0 ||
            element->SgTransferLength > (state->TotalLength - cursor)) {
            return XHCI_XFER_SG_MISMATCH;
        }

        status = xhciXferEmitFragment(state,
                                      element->SgPhysicalAddressLo,
                                      element->SgTransferLength);
        if (status != XHCI_XFER_OK) {
            return status;
        }
        cursor += element->SgTransferLength;
        consumed++;
    }

    /* Elements left over past the end of the buffer describe memory this
     * transfer does not own. Two elements sharing one offset land here too:
     * the second is never selected, so the count does not add up. */
    if (consumed != sgList->SgElementCount) {
        return XHCI_XFER_SG_MISMATCH;
    }
    return XHCI_XFER_OK;
}

ULONG XhciXferBuildControl(const XHCI_CONTROL_REQUEST *request,
                           XHCI_TRB *out,
                           ULONG capacity,
                           PXHCI_CONTROL_LAYOUT layout)
{
    XHCI_XFER_BUILD_STATE state;
    XHCI_TRB *trb;
    ULONG dataStage;
    ULONG dataIn;
    ULONG statusIn;
    ULONG trt;
    ULONG status;

    if (request == NULL || out == NULL || layout == NULL || capacity < 2) {
        return XHCI_XFER_BAD_PARAM;
    }
    if (!xhciXferMps0Valid(request->MaxPacketSize)) {
        return XHCI_XFER_BAD_PARAM;
    }

    layout->TrbCount = 0;
    layout->DataFirst = XHCI_XFER_NO_INDEX;
    layout->DataCount = 0;
    layout->TdCount = 0;

    /*
     * Direction comes from the SETUP bytes, because the spec makes it software's
     * job to keep the TRBs consistent with them: "System software is responsible
     * for ensuring that the Direction (DIR) flag of the Data Stage and Status
     * Stage TRBs are consistent with the USB SETUP Data defined
     * bmRequestType:Data Transfer Direction (DTD) flag and wLength field"
     * (p.192, mapped in Table 4-7 p.193). usbport's own direction flag is
     * checked against it rather than used: a transfer whose two statements of
     * its direction disagree is a malformed request, and guessing which one to
     * believe would program a data stage that moves bytes the wrong way.
     */
    dataStage = (request->TransferLength > 0) ? 1 : 0;
    dataIn = (request->Setup.bmRequestType & XHCI_SETUP_DTD_IN) ? 1 : 0;
    if (dataStage) {
        ULONG flagsIn;

        flagsIn = request->TransferFlagsIn ? 1 : 0;
        if (flagsIn != dataIn) {
            return XHCI_XFER_DIRECTION_CONFLICT;
        }
    }

    /* Table 4-7: the Status Stage direction is the opposite of the data
     * stage's, and IN when there is no data stage. */
    statusIn = (dataStage && dataIn) ? 0 : 1;
    if (!dataStage) {
        trt = XHCI_TRB_TRT_NO_DATA;
    } else if (dataIn) {
        trt = XHCI_TRB_TRT_IN_DATA;
    } else {
        trt = XHCI_TRB_TRT_OUT_DATA;
    }

    /* --- Setup Stage TD: exactly one TRB, immediate data (p.192). --- */
    trb = &out[0];
    XhciTrbClear(trb);
    trb->Param0 = ((ULONG)request->Setup.bmRequestType) |
                  (((ULONG)request->Setup.bRequest) << 8) |
                  (((ULONG)request->Setup.wValue) << 16);
    trb->Param1 = ((ULONG)request->Setup.wIndex) |
                  (((ULONG)request->Setup.wLength) << 16);
    /* "TRB Transfer Length. Always 8" (Table 6-25). */
    trb->Status = 8UL | XHCI_TRB_INTERRUPTER(0);
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_SETUP_STAGE) |
                   XHCI_TRB_IDT | XHCI_TRB_TRT(trt);

    state.Out = out;
    state.Capacity = capacity;
    state.Count = 1;
    state.DataFirst = 1;
    state.DataCount = 0;
    state.LengthSum = 0;
    state.TotalLength = request->TransferLength;
    state.MaxPacketSize = request->MaxPacketSize;
    state.DirectionIn = dataIn;
    state.NormalOnly = 0;

    /* --- Data Stage TD, if there is one. --- */
    if (dataStage) {
        status = xhciXferEmitSgList(&state, request->SgList);
        if (status != XHCI_XFER_OK) {
            return status;
        }
        if (state.DataCount == 0) {
            return XHCI_XFER_SG_MISMATCH;
        }
        /* The Chain flag "is always `0` in the last TRB of a Data Stage TD"
         * (Table 6-29); every TRB was emitted chained, so exactly one bit is
         * cleared here and the TD's extent is unambiguous on the ring. */
        out[state.DataFirst + state.DataCount - 1].Control &= ~XHCI_TRB_CH;
        /* "The value of the TD Size in the last Transfer TRB of a TD shall be
         * cleared to '0' to explicitly indicate that it is the last Transfer
         * TRB of the TD" (p.198). */
        out[state.DataFirst + state.DataCount - 1].Status &=
            ~XHCI_TRB_TD_SIZE(XHCI_TRB_TD_SIZE_MAX);
    }

    /* --- Status Stage TD: one TRB, and the only one carrying IOC. --- */
    if (state.Count >= capacity) {
        return XHCI_XFER_TOO_MANY_TRBS;
    }
    trb = &out[state.Count];
    XhciTrbClear(trb);
    trb->Param0 = 0;
    trb->Param1 = 0;
    trb->Status = XHCI_TRB_INTERRUPTER(0);      /* DW2 bits 21:0 are RsvdZ */
    trb->Control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_STATUS_STAGE) | XHCI_TRB_IOC;
    if (statusIn) {
        trb->Control |= XHCI_TRB_DIR_IN;
    }
    state.Count++;

    layout->TrbCount = state.Count;
    layout->TdLengths[0] = 1;                   /* Setup Stage TD  */
    layout->TdCount = 1;
    if (dataStage) {
        layout->DataFirst = state.DataFirst;
        layout->DataCount = state.DataCount;
        layout->TdLengths[layout->TdCount] = state.DataCount;
        layout->TdCount++;
    }
    layout->TdLengths[layout->TdCount] = 1;     /* Status Stage TD */
    layout->TdCount++;
    return XHCI_XFER_OK;
}

/* ------------------------------------------------------------------ */
/* 7a-A.2 / 8-A.1: building a non-control transfer                     */
/*                                                                     */
/* One builder for interrupt and bulk. Nothing below distinguishes them:  */
/* both are a single TD of Normal TRBs whose direction is the Endpoint    */
/* Context's, and the whole difference between the two types lives in that*/
/* context (EP Type and Interval), which xhci_ctx.c owns.                 */
/* ------------------------------------------------------------------ */

ULONG XhciXferBuildNormal(const XHCI_NORMAL_REQUEST *request,
                          ULONG transferFlagsIn,
                          XHCI_TRB *out,
                          ULONG capacity,
                          ULONG *trbCount)
{
    XHCI_XFER_BUILD_STATE state;
    XHCI_TRB *last;
    ULONG status;

    if (request == NULL || out == NULL || trbCount == NULL || capacity < 1) {
        return XHCI_XFER_BAD_PARAM;
    }
    /*
     * The Max Packet Size is the endpoint's, so unlike EP0's it is not one of
     * four legal values - wMaxPacketSize bits 10:0 is anything up to 1024. What
     * it may not be is 0, which the TD Size arithmetic divides by.
     */
    if (request->MaxPacketSize == 0 || request->MaxPacketSize > 0x7FFUL) {
        return XHCI_XFER_BAD_PARAM;
    }
    /*
     * usbport's direction against the endpoint's, checked and not chosen
     * between. For a control transfer the second opinion is the SETUP bytes; for
     * an interrupt or bulk endpoint it is bEndpointAddress bit 7, which is also
     * where the Endpoint Context's EP Type came from - so a disagreement means
     * usbport is submitting an IN transfer on a pipe this driver configured as
     * OUT, and moving the bytes either way would be wrong.
     */
    if ((transferFlagsIn ? 1UL : 0UL) != (request->DirectionIn ? 1UL : 0UL)) {
        return XHCI_XFER_DIRECTION_CONFLICT;
    }

    *trbCount = 0;

    state.Out = out;
    state.Capacity = capacity;
    state.Count = 0;
    state.DataFirst = 0;
    state.DataCount = 0;
    state.LengthSum = 0;
    state.TotalLength = request->TransferLength;
    state.MaxPacketSize = request->MaxPacketSize;
    state.DirectionIn = request->DirectionIn ? 1UL : 0UL;
    state.NormalOnly = 1;

    if (request->TransferLength == 0) {
        /*
         * **One zero-length Normal TRB, not zero TRBs.** A transfer with nothing
         * to move is still a transfer usbport is waiting on, and a TD with no
         * TRBs is not a TD - it would never be executed and never complete.
         * batch 6-0 established that usbport's SG list is *empty* rather than
         * absent for these, which is why the walk is skipped rather than asked
         * to tile a zero-length buffer.
         */
        status = xhciXferEmitData(&state, 0, 0);
    } else {
        status = xhciXferEmitSgList(&state, request->SgList);
    }
    if (status != XHCI_XFER_OK) {
        return status;
    }
    if (state.DataCount == 0) {
        return XHCI_XFER_SG_MISMATCH;
    }

    last = &out[state.Count - 1];
    /* "The Chain flag ... is always '0' in the last TRB of a TD" and "the TD
     * Size in the last Transfer TRB of a TD shall be cleared to '0'" (p.198).
     * Every TRB was emitted chained, so exactly one of each is cleared here. */
    last->Control &= ~XHCI_TRB_CH;
    last->Status &= ~XHCI_TRB_TD_SIZE(XHCI_TRB_TD_SIZE_MAX);
    /*
     * **IOC lands here rather than on a Status Stage TRB**, because a
     * non-control TD has no third stage to carry it: this is the only TRB whose
     * completion means the transfer is over.
     *
     * **The two halves of ISP are not the same claim**, and the tenth review
     * caught this comment making the stronger one twice. 4.10.1.1.2 requires ISP
     * on every TRB of a TD *except the last* - that is the half that is not
     * optional, because a short packet terminates the TD wherever it lands, so
     * without ISP on the TRB it lands on there would be no event and the IOC
     * here would never be reached: a transfer that finished on the bus and never
     * finished to usbport. On the last TRB it is merely *permitted*, and beside
     * IOC it is redundant (4.10.1.1). It is set anyway, so that "ISP on every IN
     * data TRB" is one rule rather than a rule with an exception - but that is a
     * uniformity choice, not a requirement, and saying otherwise put an invented
     * obligation in the file.
     */
    last->Control |= XHCI_TRB_IOC;

    *trbCount = state.Count;
    return XHCI_XFER_OK;
}

/* Both defined further down, and declared here because the submit paths above
 * them retract tail records, and the retraction asks the range question. */
static VOID xhciXferForgetMidTdTails(PXHCI_TRANSFER_QUEUE queue,
                                     const XHCI_RING *ring,
                                     ULONG firstIndex,
                                     ULONG trbCount);
static ULONG xhciXferRangeContains(const XHCI_RING *ring,
                                   ULONG first,
                                   ULONG count,
                                   ULONG index,
                                   ULONG *offset);

ULONG XhciXferSubmitNormal(PXHCI_TRANSFER_QUEUE queue,
                           PXHCI_RING ring,
                           const XHCI_NORMAL_REQUEST *request,
                           ULONG transferFlagsIn,
                           PXHCI_TRANSFER transfer,
                           PVOID transferParameters,
                           XHCI_TRB *scratch,
                           ULONG scratchCount)
{
    XHCI_TD_GROUP_PLACEMENT placement;
    ULONG tdLengths[1];
    ULONG trbCount;
    ULONG status;

    if (queue == NULL || ring == NULL || request == NULL ||
        transfer == NULL || scratch == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }

    status = XhciXferBuildNormal(request, transferFlagsIn, scratch,
                                 scratchCount, &trbCount);
    if (status != XHCI_XFER_OK) {
        return status;
    }

    /*
     * One TD, published as a group of one - the same call the control path uses,
     * because "write the whole thing or nothing" is the ring-full contract
     * whatever the TD count is (docs/contributing/implementation-invariants.md, "Ring Full
     * and Backpressure").
     */
    tdLengths[0] = trbCount;
    status = XhciRingEnqueueTdGroup(ring, scratch, trbCount, tdLengths, 1,
                                    &placement);
    if (status == XHCI_RING_FULL) {
        return XHCI_XFER_BUSY;
    }
    if (status != XHCI_RING_OK) {
        return XHCI_XFER_BAD_PARAM;
    }

    transfer->Signature = XHCI_TRANSFER_SIGNATURE;
    transfer->Next = NULL;
    transfer->TransferParameters = transferParameters;
    transfer->Token = queue->NextToken;
    queue->NextToken++;
    if (queue->NextToken == 0) {
        queue->NextToken = 1;
    }

    transfer->FirstIndex = placement.FirstIndex;
    transfer->LastIndex = placement.LastIndex;
    transfer->TrbCount = placement.TrbCount;
    /* These TRBs are re-let as of now, so any tail this queue was still
     * waiting for inside them can never legitimately arrive again. */
    xhciXferForgetMidTdTails(queue, ring, placement.FirstIndex,
                             placement.TrbCount);
    /*
     * Every TRB of this TD carries data, so the data range **is** the whole
     * placement - unlike a control transfer, where it is the middle of three
     * TDs and the Setup and Status TRBs are not part of it. The residual
     * arithmetic keys on this range, so getting it wrong would attribute a short
     * packet to the wrong TRBs.
     */
    transfer->DataFirstIndex = placement.FirstIndex;
    transfer->DataTrbCount = trbCount;

    transfer->RequestedLength = request->TransferLength;
    transfer->BytesTransferred = 0;
    transfer->Flags = 0;
    transfer->UsbdStatus = XHCI_USBD_STATUS_SUCCESS;
    /* Not a snooped hub request until the device layer arms it - and this is
     * the site that guarantees the field is never read uninitialized, since
     * the transfer extension is not assumed zeroed (task 7b-A.1). */
    transfer->TopoReply = XHCI_TOPO_REPLY_NONE;
    /* ...and not a snooped configuration descriptor either (task 9-A.2), on
     * exactly the same terms. */
    transfer->DescAction = XHCI_DESC_ACT_NONE;
    /* Same rule: meaningful only under XHCI_XFER_FLAG_SHORT_DEFERRED, but the
     * extension is not assumed zeroed, so it is written here rather than left
     * to whatever the previous transfer through this storage put in it. */
    transfer->ShortTrbPA = 0;

    if (queue->Tail != NULL) {
        queue->Tail->Next = transfer;
    } else {
        queue->Head = transfer;
    }
    queue->Tail = transfer;
    queue->Count++;
    queue->Submitted++;
    return XHCI_XFER_OK;
}

/* ------------------------------------------------------------------ */
/* 6-A.2: the pending queue                                            */
/* ------------------------------------------------------------------ */

VOID XhciXferQueueInit(PXHCI_TRANSFER_QUEUE queue)
{
    ULONG i;

    if (queue == NULL) {
        return;
    }
    queue->Head = NULL;
    queue->Tail = NULL;
    queue->Count = 0;
    /* Token 0 is never issued, so a zeroed transfer record matches no
     * outstanding transfer - the same reason the start epoch never hands out
     * zero (docs/contributing/implementation-invariants.md, "Command Ring"). */
    queue->NextToken = 1;
    queue->Submitted = 0;
    queue->Completed = 0;
    queue->ShortPackets = 0;
    queue->ShortSuccesses = 0;
    queue->IntermediateEvents = 0;
    queue->MidTdShortRetires = 0;
    queue->MidTdShortTails = 0;
    queue->MidTdTailsDropped = 0;
    queue->MidTdTailsCensored = 0;
    queue->MidTdDeferrals = 0;
    queue->MidTdDeferralsTailed = 0;
    queue->MidTdDeferralsTailedSpurious = 0;
    queue->MidTdDeferralsLost = 0;
    queue->MidTdTailCount = 0;
    for (i = 0; i < XHCI_XFER_MID_TD_TAIL; i++) {
        queue->MidTdTailIndex[i] = XHCI_XFER_NO_INDEX;
    }
    queue->Errors = 0;
    queue->Recoveries = 0;
    queue->UnmatchedEvents = 0;
    queue->ForeignEvents = 0;
    queue->EventDataEvents = 0;
    queue->BadCodes = 0;
    queue->ResidualRejects = 0;
    queue->LengthOverruns = 0;
    queue->SumFailures = 0;
    queue->ResidualIgnored = 0;
    queue->SweptTransfers = 0;
    queue->OrphanedGroups = 0;
    queue->PlacementFailures = 0;
    queue->RetryArmed = 0;
    queue->RetryFree = 0;
    queue->RetriesAsked = 0;
    queue->IsoPackets = 0;
    queue->IsoPacketsAnswered = 0;
    queue->IsoPacketErrors = 0;
    queue->IsoMissedService = 0;
    queue->IsoGroupsAwaitingTail = 0;
}

/*
 * Remember the tail TRB this driver did not wait for, so the trailing event
 * p.175 promises can be recognised if it ever arrives. The repo audit
 * finding 1: at the time the early retire happened identically on both kinds of
 * controller, so this record was the only answer to the conformance question.
 * Task 9-0.2 changed that - a conforming controller's tail is consumed in the
 * same drain and ends the transfer positionally, so it never reaches the early
 * retire at all - and `MidTdDeferralsTailed` is now the primary reading. This
 * one remains as the residual measure of the single window the fix leaves open.
 *
 * Eviction is counted rather than silent - a shortfall in `MidTdShortTails`
 * must be attributable to the controller or to this ring, never ambiguous.
 * IRQL: called at DISPATCH_LEVEL; pure, so any IRQL is safe.
 */
static VOID xhciXferRecordMidTdTail(PXHCI_TRANSFER_QUEUE queue, ULONG tailIndex)
{
    ULONG i;

    if (queue == NULL || tailIndex == XHCI_XFER_NO_INDEX) {
        return;
    }
    /*
     * A compacted FIFO rather than a write cursor over fixed slots. The cursor
     * version wrote to the *next physical slot* whether or not it was live, so
     * it could displace a waiting record while other slots stood empty - the
     * count stayed arithmetically consistent and the eviction meant the wrong
     * thing, which is worse than an obviously wrong number (repo audit round 2,
     * finding 2). Here a record is displaced only when all
     * XHCI_XFER_MID_TD_TAIL of them are genuinely outstanding.
     */
    if (queue->MidTdTailCount >= XHCI_XFER_MID_TD_TAIL) {
        for (i = 1; i < XHCI_XFER_MID_TD_TAIL; i++) {
            queue->MidTdTailIndex[i - 1] = queue->MidTdTailIndex[i];
        }
        queue->MidTdTailCount = XHCI_XFER_MID_TD_TAIL - 1;
        queue->MidTdTailsDropped++;
    }
    queue->MidTdTailIndex[queue->MidTdTailCount] = tailIndex;
    queue->MidTdTailCount++;
}

/*
 * Forget any recorded tail that lies inside a TD about to be published there.
 *
 * This is what makes the "a false match cannot occur" claim true rather than
 * merely likely (repo audit round 2, finding 1). The claim rested on an unowned
 * index never being re-let - but that only holds until the ring laps: the TRB
 * *is* re-let eventually, and if that later transfer ends and its own event
 * arrives unowned, a stale record would claim it and report a tail event the
 * controller never sent. Dropping the record at the moment the range is reused
 * closes the window at its only entrance.
 * IRQL: called at DISPATCH_LEVEL; pure, so any IRQL is safe.
 */
static VOID xhciXferForgetMidTdTails(PXHCI_TRANSFER_QUEUE queue,
                                     const XHCI_RING *ring,
                                     ULONG firstIndex,
                                     ULONG trbCount)
{
    ULONG i;
    ULONG j;

    if (queue == NULL || ring == NULL || trbCount == 0) {
        return;
    }
    i = 0;
    while (i < queue->MidTdTailCount) {
        if (xhciXferRangeContains(ring, firstIndex, trbCount,
                                  queue->MidTdTailIndex[i], NULL)) {
            for (j = i + 1; j < queue->MidTdTailCount; j++) {
                queue->MidTdTailIndex[j - 1] = queue->MidTdTailIndex[j];
            }
            queue->MidTdTailCount--;
            /*
             * Counted apart from an eviction, but counted. An earlier revision
             * counted it nowhere, reasoning that a retracted tail can no longer
             * arrive so nothing is outstanding to be short of - which confuses
             * *this driver's* inability to read the answer with the answer
             * being no. The tail may already have been sent and be sitting
             * unprocessed in the event ring right now; the repost loop makes
             * that the ordinary case (repo audit round 3, finding 1). So the
             * observation is censored, not resolved.
             */
            queue->MidTdTailsCensored++;
        } else {
            i++;
        }
    }
}

/*
 * Is this unowned event the trailing half of an early retire? Consumes the
 * record on a match, so one recorded tail answers exactly one event and a
 * controller repeating itself cannot inflate the reading.
 * IRQL: called at DISPATCH_LEVEL; pure, so any IRQL is safe.
 */
static ULONG xhciXferClaimMidTdTail(PXHCI_TRANSFER_QUEUE queue, ULONG index)
{
    ULONG i;
    ULONG j;

    if (queue == NULL || index == XHCI_XFER_NO_INDEX) {
        return 0;
    }
    for (i = 0; i < queue->MidTdTailCount; i++) {
        if (queue->MidTdTailIndex[i] == index) {
            for (j = i + 1; j < queue->MidTdTailCount; j++) {
                queue->MidTdTailIndex[j - 1] = queue->MidTdTailIndex[j];
            }
            queue->MidTdTailCount--;
            return 1;
        }
    }
    return 0;
}

VOID XhciXferQueueArmRetry(PXHCI_TRANSFER_QUEUE queue, const XHCI_RING *ring)
{
    if (queue == NULL || ring == NULL) {
        return;
    }
    queue->RetryArmed = 1;
    queue->RetryFree = XhciRingFree(ring);
}

ULONG XhciXferQueueRetryDue(const XHCI_TRANSFER_QUEUE *queue,
                            const XHCI_RING *ring)
{
    if (queue == NULL || ring == NULL || queue->RetryArmed == 0) {
        return 0;
    }
    return (XhciRingFree(ring) > queue->RetryFree) ? 1UL : 0UL;
}

VOID XhciXferQueueDisarmRetry(PXHCI_TRANSFER_QUEUE queue, ULONG asked)
{
    if (queue == NULL) {
        return;
    }
    queue->RetryArmed = 0;
    queue->RetryFree = 0;
    if (asked) {
        queue->RetriesAsked++;
    }
}

/* `steps` positions forward from `index`, stepping the way the ring does so a
 * walk crossing the wrap never lands on the Link TRB. */
static ULONG xhciXferAdvance(const XHCI_RING *ring, ULONG index, ULONG steps)
{
    ULONG i;

    for (i = 0; i < steps; i++) {
        index = XhciRingNextIndex(ring, index);
    }
    return index;
}

/* Is `index` one of the `count` TRBs starting at `first`? Answered by walking
 * rather than by comparing indices, because the range may wrap and because the
 * Link TRB sits inside it without being part of it. */
static ULONG xhciXferRangeContains(const XHCI_RING *ring,
                                   ULONG first,
                                   ULONG count,
                                   ULONG index,
                                   ULONG *position)
{
    ULONG i;
    ULONG at;

    at = first;
    for (i = 0; i < count; i++) {
        if (at == index) {
            if (position != NULL) {
                *position = i;
            }
            return 1;
        }
        at = XhciRingNextIndex(ring, at);
    }
    return 0;
}

ULONG XhciXferSubmitControl(PXHCI_TRANSFER_QUEUE queue,
                            PXHCI_RING ring,
                            const XHCI_CONTROL_REQUEST *request,
                            PXHCI_TRANSFER transfer,
                            PVOID transferParameters,
                            XHCI_TRB *scratch,
                            ULONG scratchCount)
{
    XHCI_CONTROL_LAYOUT layout;
    XHCI_TD_GROUP_PLACEMENT placement;
    ULONG status;

    if (queue == NULL || ring == NULL || request == NULL ||
        transfer == NULL || scratch == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }

    status = XhciXferBuildControl(request, scratch, scratchCount, &layout);
    if (status != XHCI_XFER_OK) {
        return status;
    }

    /*
     * Preflight and publication are one call, and it writes nothing at all if
     * the whole group does not fit: "If it does not fit, write nothing and
     * return busy from SubmitTransfer - usbport requeues and retries. Never
     * write a partial TD" (docs/contributing/implementation-invariants.md, "Ring Full and
     * Backpressure").
     */
    status = XhciRingEnqueueTdGroup(ring, scratch, layout.TrbCount,
                                    layout.TdLengths, layout.TdCount,
                                    &placement);
    if (status == XHCI_RING_FULL) {
        return XHCI_XFER_BUSY;
    }
    if (status != XHCI_RING_OK) {
        return XHCI_XFER_BAD_PARAM;
    }

    transfer->Signature = XHCI_TRANSFER_SIGNATURE;
    transfer->Next = NULL;
    transfer->TransferParameters = transferParameters;
    transfer->Token = queue->NextToken;
    queue->NextToken++;
    if (queue->NextToken == 0) {
        queue->NextToken = 1;
    }

    transfer->FirstIndex = placement.FirstIndex;
    transfer->LastIndex = placement.LastIndex;
    transfer->TrbCount = placement.TrbCount;
    /* These TRBs are re-let as of now, so any tail this queue was still
     * waiting for inside them can never legitimately arrive again. */
    xhciXferForgetMidTdTails(queue, ring, placement.FirstIndex,
                             placement.TrbCount);
    if (layout.DataCount > 0) {
        transfer->DataFirstIndex = xhciXferAdvance(ring, placement.FirstIndex,
                                                   layout.DataFirst);
    } else {
        transfer->DataFirstIndex = XHCI_XFER_NO_INDEX;
    }
    transfer->DataTrbCount = layout.DataCount;

    transfer->RequestedLength = request->TransferLength;
    transfer->BytesTransferred = 0;
    transfer->Flags = 0;
    transfer->UsbdStatus = XHCI_USBD_STATUS_SUCCESS;
    /* Not a snooped hub request until the device layer arms it - and this is
     * the site that guarantees the field is never read uninitialized, since
     * the transfer extension is not assumed zeroed (task 7b-A.1). */
    transfer->TopoReply = XHCI_TOPO_REPLY_NONE;
    /* ...and not a snooped configuration descriptor either (task 9-A.2), on
     * exactly the same terms. */
    transfer->DescAction = XHCI_DESC_ACT_NONE;
    /* Same rule: meaningful only under XHCI_XFER_FLAG_SHORT_DEFERRED, but the
     * extension is not assumed zeroed, so it is written here rather than left
     * to whatever the previous transfer through this storage put in it. */
    transfer->ShortTrbPA = 0;

    if (queue->Tail != NULL) {
        queue->Tail->Next = transfer;
    } else {
        queue->Head = transfer;
    }
    queue->Tail = transfer;
    queue->Count++;
    queue->Submitted++;
    return XHCI_XFER_OK;
}

/*
 * A transfer is leaving this queue. If it was still holding task 9-0.2's
 * deferral, the observation ends here unanswered - the promised tail can no
 * longer be told from anything else - so it is counted rather than dropped.
 *
 * This lives at the **unlink** rather than at each of the ways a transfer can
 * end, which is the whole reason the partition can be trusted: a row set
 * assembled path-by-path is only as complete as whoever assembled it (task
 * 7b-A.1.0). The two resolutions that are *not* a loss - the in-band tail and
 * the settle's early retire - clear the flag before they get here, so what
 * arrives still carrying it is by construction everything else.
 *
 * IRQL: called at DISPATCH_LEVEL; pure, so any IRQL is safe.
 */
static VOID xhciXferDeferralUnlinked(PXHCI_TRANSFER_QUEUE queue,
                                     PXHCI_TRANSFER transfer)
{
    if (transfer == NULL || !(transfer->Flags & XHCI_XFER_FLAG_SHORT_DEFERRED)) {
        return;
    }
    transfer->Flags &= ~XHCI_XFER_FLAG_SHORT_DEFERRED;
    queue->MidTdDeferralsLost++;
}

/* Detach the queue's first `n` entries, oldest first, and return them as a
 * list. The caller completes them after dropping the controller lock. */
static PXHCI_TRANSFER xhciXferDetach(PXHCI_TRANSFER_QUEUE queue, ULONG n)
{
    PXHCI_TRANSFER head;
    PXHCI_TRANSFER last;
    ULONG i;

    head = queue->Head;
    last = NULL;
    for (i = 0; i < n && queue->Head != NULL; i++) {
        last = queue->Head;
        xhciXferDeferralUnlinked(queue, last);
        queue->Head = queue->Head->Next;
        queue->Count--;
    }
    if (queue->Head == NULL) {
        queue->Tail = NULL;
    }
    if (last != NULL) {
        last->Next = NULL;
    }
    return head;
}

PXHCI_TRANSFER XhciXferQueueDrain(PXHCI_TRANSFER_QUEUE queue,
                                  LONG usbdStatus,
                                  ULONG *count)
{
    PXHCI_TRANSFER head;
    PXHCI_TRANSFER walk;
    ULONG n;

    if (count != NULL) {
        *count = 0;
    }
    if (queue == NULL) {
        return NULL;
    }

    n = queue->Count;
    head = xhciXferDetach(queue, n);
    for (walk = head; walk != NULL; walk = walk->Next) {
        walk->UsbdStatus = usbdStatus;
        walk->Flags |= XHCI_XFER_FLAG_FAILED;
        queue->Completed++;
    }
    if (count != NULL) {
        *count = n;
    }
    return head;
}

ULONG XhciXferQueueRemove(PXHCI_TRANSFER_QUEUE queue, PXHCI_TRANSFER transfer)
{
    PXHCI_TRANSFER walk;
    PXHCI_TRANSFER prev;

    if (queue == NULL || transfer == NULL) {
        return 0;
    }

    prev = NULL;
    for (walk = queue->Head; walk != NULL; walk = walk->Next) {
        if (walk == transfer) {
            break;
        }
        prev = walk;
    }
    if (walk == NULL) {
        return 0;
    }

    if (prev != NULL) {
        prev->Next = walk->Next;
    } else {
        queue->Head = walk->Next;
    }
    if (queue->Tail == walk) {
        queue->Tail = prev;
    }
    if (queue->Count != 0) {
        queue->Count--;
    }
    xhciXferDeferralUnlinked(queue, walk);
    walk->Next = NULL;
    /*
     * Counted as completed, because from the queue's point of view it is: the
     * caller answers usbport for it, and leaving it out would make Submitted and
     * Completed disagree by the number of cancellations - which is exactly the
     * gap somebody would later read as leaked transfers.
     */
    queue->Completed++;
    return 1;
}

ULONG XhciXferQueueOwnsIndex(const XHCI_TRANSFER_QUEUE *queue,
                             const XHCI_RING *ring,
                             ULONG index)
{
    const XHCI_TRANSFER *walk;

    if (queue == NULL || ring == NULL) {
        return 0;
    }
    for (walk = queue->Head; walk != NULL; walk = walk->Next) {
        if (xhciXferRangeContains(ring, walk->FirstIndex, walk->TrbCount, index,
                                  NULL)) {
            return 1;
        }
    }
    return 0;
}

/*
 * The residual, applied to one transfer.
 *
 * p.175, for a TD that is not using Event Data TRBs: "the total number of
 * received bytes for a Short Packet TD is the sum of the TRB Transfer Length
 * fields in all Transfer TRBs up to and including the one that generated the
 * Short Packet Event, minus the residue value of the TRB Transfer Length field
 * in the Short Packet Event." Table 6-38 gives an error event the same shape -
 * "the difference between the expected transfer size and the number of bytes
 * successfully received".
 *
 * Returns 1 if `bytes` was set, 0 if this event carries no byte count for this
 * transfer (an event on the Setup or Status TRB, or a code whose length field
 * is not a residual). A residual larger than the bytes it applies to is an
 * impossible answer, so it is refused rather than clamped: reporting
 * `sum - residual` as an unsigned subtraction would hand usbport a length near
 * 4 GB, and clamping it to zero would report a plausible number nothing
 * measured.
 */
static ULONG xhciXferResidualBytes(PXHCI_TRANSFER_QUEUE queue,
                                   PXHCI_RING ring,
                                   PXHCI_TRANSFER transfer,
                                   ULONG reportedIndex,
                                   ULONG residual,
                                   const XHCI_XFER_CODE *code,
                                   ULONG *bytes,
                                   ULONG *rejected)
{
    ULONG sum;

    *rejected = 0;
    *bytes = 0;

    if (!code->ResidualIsBytes) {
        return 0;
    }
    if (transfer->DataTrbCount == 0 ||
        !xhciXferRangeContains(ring, transfer->DataFirstIndex,
                               transfer->DataTrbCount, reportedIndex, NULL)) {
        /* The Setup Stage TRB's length is 8 and the Status Stage TRB has no
         * length field at all, so neither contributes to what the caller asked
         * to move - ReactOS's EHCI miniport excludes the SETUP PID's length
         * from its own accumulator for the same reason. A residual reported
         * against either is not a byte count this driver can use; it is counted
         * and dropped rather than being allowed to change a length. */
        if (residual != 0) {
            queue->ResidualIgnored++;
        }
        return 0;
    }

    if (XhciRingSumTrbLengths(ring, transfer->DataFirstIndex, reportedIndex,
                              &sum) != XHCI_RING_OK) {
        queue->SumFailures++;
        *rejected = 1;
        return 0;
    }
    /* More bytes not transferred than the named TRBs could hold. Refused rather
     * than clamped: the unsigned subtraction below would otherwise hand usbport
     * a length near 4 GB, and clamping to zero would report a plausible number
     * nothing measured. */
    if (residual > sum) {
        queue->ResidualRejects++;
        *rejected = 1;
        return 0;
    }
    /* And the buffer usbport mapped is RequestedLength bytes. A length beyond
     * it means the TRB lengths on the ring and this transfer's own length no
     * longer agree - a different fault from a bad residual, and one this
     * driver's own bookkeeping is the likelier source of. */
    if ((sum - residual) > transfer->RequestedLength) {
        queue->LengthOverruns++;
        *rejected = 1;
        return 0;
    }
    *bytes = sum - residual;
    return 1;
}

ULONG XhciXferQueueStopped(PXHCI_TRANSFER_QUEUE queue,
                           PXHCI_RING ring,
                           ULONG eventTrbPA,
                           ULONG eventDw2)
{
    XHCI_XFER_CODE code;
    PXHCI_TRANSFER walk;
    PXHCI_TRANSFER owner;
    ULONG completionCode;
    ULONG reportedIndex;
    ULONG bytes;
    ULONG rejected;

    if (queue == NULL || ring == NULL) {
        return 0;
    }
    completionCode = XHCI_TRB_GET_COMPLETION(eventDw2);
    /*
     * The three Stopped codes and nothing else. Asking `XhciXferCodeInfo` for
     * `ResidualIsBytes` alone was too broad: an ordinary Success carries one
     * too, and this function's whole premise - that no completion is coming, so
     * a measurement here is the only one there will be - is false for every code
     * the ordinary event path handles.
     */
    if (completionCode < XHCI_CC_STOPPED ||
        completionCode > XHCI_CC_STOPPED_SHORT_PACKET) {
        return 0;
    }
    /*
     * Decoded for the code-26 path below, which hands it to the shared residual
     * arithmetic. It is **not** the gate: all three codes carry something
     * derivable (4.6.9 p.122 gives each its own rule), while `ResidualIsBytes`
     * describes only whether the *residual* form applies - which is 26's alone.
     */
    if (XhciXferCodeInfo(completionCode, &code) != XHCI_XFER_OK) {
        return 0;
    }
    if (XhciRingIndexFromPA(ring, eventTrbPA, &reportedIndex) != XHCI_RING_OK) {
        return 0;
    }

    owner = NULL;
    for (walk = queue->Head; walk != NULL; walk = walk->Next) {
        if (xhciXferRangeContains(ring, walk->FirstIndex, walk->TrbCount,
                                  reportedIndex, NULL)) {
            owner = walk;
            break;
        }
    }
    if (owner == NULL || (owner->Flags & XHCI_XFER_FLAG_LENGTH_FIXED) != 0) {
        return 0;
    }
    /*
     * **Isochronous requests are measured by their own path and not here.** Both
     * arms below are TD-scoped, and an iso submit is a *group* of TDs under one
     * record: `DataFirstIndex`/`DataTrbCount` span the whole group, so code 26's
     * sum from `DataFirstIndex` runs over every earlier packet's TRBs where
     * 4.6.9 p.122 defines the sum over the stopped TD alone (overreporting by
     * each earlier short packet's shortfall), while the EDTLA of
     * Stopped - Short Packet is per-TD (4.11.5.2 p.209 - cleared at each TD's
     * first TRB) and the `=` below would overwrite the running per-packet total
     * `xhciDevIsoTransferEvent` accumulates with `+=` (underreporting). Iso
     * transfers never set `XHCI_XFER_FLAG_LENGTH_FIXED`, so the latch guard
     * above does not cover them.
     *
     * The accumulation the iso path has already done *is* the correct
     * measurement for the packets that completed, and the packets that did not
     * carry their own per-packet status, which is what usbport reads. So the
     * right action here is to leave it alone.
     *
     * **What that costs, stated rather than glossed** (Codex review, same day):
     * this event is the only measurement the *interrupted* TD will ever get, and
     * returning here discards it - so a stream stopped part-way through one
     * packet reports the bytes of the packets before it and nothing of that one.
     * It is an **underreport**, which is this file's preferred direction, and it
     * is bounded by one packet. Three things make it the right trade rather than
     * a smaller version of the bug:
     *
     *   - **usbport reads the per-packet block, not this total.** The iso
     *     completion path says so where it sets `owner->UsbdStatus`, and the
     *     teardown stamps every packet the controller never answered - so the
     *     information a client acts on is not what is being dropped here.
     *   - The alternative is to bound the sum with `XhciRingTdBounds` on the
     *     stopped TD and credit it to that packet's own entry, which means this
     *     pure ring/queue layer reaching into the isochronous packet block the
     *     slot layer owns. That is a second writer of the per-packet totals,
     *     racing the one that already exists.
     *   - `handoff.md`'s finding A1 names both forms and prefers this one, "the
     *     first form is smaller and keeps the 'one reader for the forced event'
     *     design intact".
     *
     * If a Phase 13 audio measurement ever shows the missing packet mattering,
     * the per-TD form is the fix and it belongs in `xhciDevIsoTransferEvent`,
     * where the packet index is already resolved - not here.
     */
    if ((owner->Flags & XHCI_XFER_FLAG_ISOCH) != 0) {
        return 0;
    }

    if (completionCode == XHCI_CC_STOPPED_LENGTH_INVALID) {
        /*
         * **The length field is invalid; the length is not.** 4.6.9 p.122 gives
         * this code its own arithmetic: "software shall ignore the TRB Transfer
         * Length field of the Transfer Event, and simply sum of the TRB Transfer
         * Length fields of all Transfer TRBs in the TD executed **prior to** the
         * TRB referenced by the Transfer Event" - prior to, not up to and
         * including, which is what separates it from code 26.
         *
         * So the sum runs to the TRB before the one named, and an event naming
         * the TD's first data TRB means nothing was transferred. The
         * predecessor is found by walking the TD, because the ring's step skips
         * the Link TRB and `index - 1` would not.
         */
        ULONG at;
        ULONG prev;
        ULONG i;
        ULONG found;

        bytes = 0;
        prev = XHCI_XFER_NO_INDEX;
        at = owner->DataFirstIndex;
        found = 0;
        /*
         * The walk stays **inside** the data range and the match is tested
         * within it, rather than after stepping: a loop bounded by the count but
         * comparing on exit walks one position past the last data TRB and
         * matches whatever follows it - for a control transfer that is the
         * Status Stage TRB, and the sum would then be the whole data stage
         * reported against a TRB that carries no length at all.
         */
        for (i = 0; i < owner->DataTrbCount; i++) {
            if (at == reportedIndex) {
                found = 1;
                break;
            }
            prev = at;
            at = XhciRingNextIndex(ring, at);
        }
        if (!found) {
            return 0;           /* not a TRB of this transfer's data range */
        }
        if (prev != XHCI_XFER_NO_INDEX &&
            XhciRingSumTrbLengths(ring, owner->DataFirstIndex, prev, &bytes) !=
                XHCI_RING_OK) {
            return 0;
        }
    } else if (completionCode == XHCI_CC_STOPPED_SHORT_PACKET) {
        /*
         * **The EDTLA is a total, not a residual, and it is usable.** Batch 6-A
         * excluded it on the reasoning that nothing is accumulating without
         * Event Data TRBs, and 4.11.5.2 p.209 says otherwise: "the xHC maintains
         * an internal 24-bit Event Data Transfer Length Accumulator (EDTLA) for
         * **each endpoint**", cleared "immediately prior to executing the first
         * Transfer TRB of a TD or when a Set TR Dequeue Pointer Command is
         * executed" and added to as each TRB completes - no Event Data TRB
         * required anywhere. p.210 then names this exact case: "If a Stopped
         * Transfer Event is generated and the Condition Code = Stopped - Short
         * Transfer, then the TRB Transfer Length field of the Transfer Event
         * shall contain the value of the EDTLA."
         *
         * So it is taken directly, with no sum and no subtraction - doing either
         * would be treating a total as a residual. `ResidualIsBytes` stays 0 for
         * this code because it is the right answer for the *residual*
         * arithmetic the ordinary event path does; this is the one caller for
         * which the field means something else.
         */
        bytes = XHCI_TRB_GET_RESIDUAL(eventDw2);
        if (bytes > owner->RequestedLength) {
            return 0;           /* more than the buffer holds: not a length */
        }
    } else if (!xhciXferResidualBytes(queue, ring, owner, reportedIndex,
                                      XHCI_TRB_GET_RESIDUAL(eventDw2), &code,
                                      &bytes, &rejected) ||
               rejected) {
        return 0;
    }
    /*
     * Latched, and **deliberately without `XHCI_XFER_FLAG_LENGTH_FIXED`.** This
     * is progress, not an ending: the transfer is still on the ring and a
     * doorbell may resume it, in which case its real completion has to be free
     * to report the final count. Fixing the length here made a survivor's
     * eventual success report the pre-stop partial instead - the third review
     * round found it. What the value is for is the transfer that is *not*
     * resumed, where no later event ever comes and this is the only measurement
     * `AbortTransfer` has to report.
     */
    owner->BytesTransferred = bytes;
    return 1;
}

/*
 * Everything a transfer's ending owes the queue and the ring once the decision
 * that it *has* ended is made: the orphan check, the sweep of everything queued
 * ahead of it, the detach into the caller's completion list, and the explicit
 * dequeue placement a ring that was not retired needs.
 *
 * It is one function because task 9-0.2 gave it a second caller. The event path
 * ends a transfer when an event says so; `XhciXferDrainSettled` ends one when a
 * drain pass proved the promised event is never coming. The *reason* differs and
 * everything after it is identical - and a rule duplicated at two sites is a
 * rule a change can delete from one of them.
 *
 * `retired` is the caller's answer to whether this TD's TRBs are already back,
 * not a question asked here: the two callers reclaim them through different ring
 * entry points, for reasons neither the queue nor the ring layer can decide
 * alone.
 *
 * IRQL: called at DISPATCH_LEVEL; pure, so any IRQL is safe.
 */
static VOID xhciXferFinishGroup(PXHCI_TRANSFER_QUEUE queue,
                                PXHCI_RING ring,
                                PXHCI_TRANSFER owner,
                                ULONG ahead,
                                ULONG retired,
                                PXHCI_XFER_EVENT_RESULT result)
{
    PXHCI_TRANSFER walk;

    /*
     * A transfer that ends without its TRBs being reclaimed leaves the ring
     * holding work nothing owns, and the free count never gets it back.
     *
     * **The non-halting version of this no longer reaches here.** `XhciXferEvent`
     * now intercepts "the group ended, nothing was reclaimed, and the code
     * halted nothing" and leaves the transfer queued as a `RefusedRetire`,
     * because on a *Running* endpoint neither completing it nor repositioning
     * the ring is legal. What still arrives here with `retired` 0 is an error
     * or a cancellation, where the endpoint has stopped running - Halted,
     * Error or Stopped depending on the code - and both become legal; and a
     * state this code did not anticipate, which is what the counter is for. Kept as a backstop: a ring that shrinks by one group
     * per transfer is not recoverable, and a wrongly stopped endpoint is.
     */
    if (!retired && !result->NeedsRecovery) {
        queue->OrphanedGroups++;
        result->NeedsRecovery = 1;
    }

    /*
     * Everything queued *ahead* of this transfer has had its TRBs reclaimed by
     * the same store - XhciRingRetireTd jumps the dequeue pointer past the
     * matched TD rather than walking one TD at a time - so those transfers must
     * be completed here or they are leaked. Reaching this state means an event
     * this driver depends on was dropped, because every control transfer's
     * Status Stage TRB carries IOC and would otherwise have completed its own
     * transfer first. That is a controller-level failure, so they are failed
     * rather than reported as the success their position implies.
     *
     * **Except one, and task 9-0.2 created it.** A transfer still carrying
     * `XHCI_XFER_FLAG_SHORT_DEFERRED` here is not a dropped event: its TD ended
     * short and it was waiting for the tail p.175 promises. Being swept *is* the
     * answer to that wait, and a stronger one than the settle's - the drain is
     * strictly FIFO, so a tail the controller had sent for an earlier TD would
     * be sitting ahead of the event that produced this sweep and would already
     * have been consumed and matched. Reaching here armed therefore proves the
     * tail was never sent.
     *
     * So it is settled here, as a successful short transfer keeping the length
     * the short event measured, and counted as the early retire it is. Failing
     * it instead - which is what this loop did before the deferral existed to
     * reach it - would report a receive that really did arrive as a controller
     * error, on any endpoint keeping more than one transfer posted.
     *
     * No tail index is recorded for it, unlike the settle's: there is no window
     * left for one to arrive in, so a record here could only ever be censored
     * later and would dilute the reading it is part of.
     */
    for (walk = queue->Head; ahead > 0 && walk != NULL; ahead--) {
        if (walk->Flags & XHCI_XFER_FLAG_SHORT_DEFERRED) {
            walk->Flags &= ~XHCI_XFER_FLAG_SHORT_DEFERRED;
            queue->MidTdShortRetires++;
        } else {
            walk->UsbdStatus = XHCI_USBD_STATUS_INTERNAL_HC_ERROR;
            walk->Flags |= XHCI_XFER_FLAG_FAILED;
            queue->SweptTransfers++;
        }
        walk = walk->Next;
        result->CompletedCount++;
    }

    result->CompletedCount++;
    result->Completed = xhciXferDetach(queue, result->CompletedCount);
    result->Action = XHCI_XFER_ACTION_COMPLETE;
    queue->Completed += result->CompletedCount;
    if (owner->Flags & XHCI_XFER_FLAG_FAILED) {
        queue->Errors++;
    }

    /*
     * If the ring was not retired, its dequeue pointer still sits on TRBs
     * belonging to transfers that no longer exist. Place it on the next
     * transfer's Setup Stage TRB - a TD head, which is the only position the
     * xHC may be given (p.172) - or on the enqueue position if nothing is left.
     * The caller programs the same value into Set TR Dequeue Pointer; the two
     * pointers must not diverge.
     */
    if (!retired) {
        ULONG target;

        if (queue->Head != NULL) {
            target = XhciRingTrbPA(ring, queue->Head->FirstIndex);
        } else {
            target = XhciRingTrbPA(ring, ring->Enqueue);
        }
        if (XhciRingSetDequeue(ring, target) != XHCI_RING_OK) {
            /* The ring refused the position rather than corrupting itself. The
             * software and hardware pointers cannot be brought together from
             * here, so this is a diagnosis for CheckController rather than
             * something to retry. */
            queue->PlacementFailures++;
        }
    }
}

ULONG XhciXferEvent(PXHCI_TRANSFER_QUEUE queue,
                    PXHCI_RING ring,
                    ULONG slotId,
                    ULONG dci,
                    ULONG eventTrbPA,
                    ULONG eventDw2,
                    ULONG eventDw3,
                    PXHCI_XFER_EVENT_RESULT result)
{
    XHCI_XFER_CODE code;
    XHCI_TD_COMPLETION completion;
    PXHCI_TRANSFER walk;
    PXHCI_TRANSFER owner;
    ULONG completionCode;
    ULONG residual;
    ULONG reportedIndex;
    ULONG ahead;
    ULONG bytes;
    ULONG rejected;
    ULONG haveBytes;
    ULONG lengthFixed;
    ULONG wholeTdIsData;
    ULONG haveCompletion;
    ULONG terminal;
    ULONG retired;

    if (result == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }
    result->Action = XHCI_XFER_ACTION_NONE;
    result->Completed = NULL;
    result->CompletedCount = 0;
    result->NeedsRecovery = 0;
    result->RefusedRetire = 0;
    result->Fatal = 0;

    if (queue == NULL || ring == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }

    /*
     * With ED = 1 the parameter is "64 bits of Event Data" rather than an
     * address (Tables 6-37, 6-39). This driver places no Event Data TRBs, so
     * such an event is unexpected input: count it and discard it, never resolve
     * its parameter as a ring position.
     */
    if (XHCI_EVENT_IS_EVENT_DATA(eventDw3)) {
        queue->EventDataEvents++;
        return XHCI_XFER_OK;
    }

    /*
     * The event names its own Slot ID and Endpoint ID (Table 6-39). Checking
     * them against what this ring is *before* resolving the pointer is what
     * stops an event for another endpoint - whose TRB address could legitimately
     * fall inside another ring's segment only by controller error, but whose
     * DCI is the cheap and exact discriminator - from completing the wrong
     * transfer.
     *
     * Note what this *skips*: a foreign event carrying a fatal completion code
     * does not escalate here. That is deliberate and it is the caller's job -
     * the DPC routes a Transfer Event to an endpoint before this call, so an
     * event naming a slot or DCI nobody has open is the DPC's to escalate, not
     * this queue's to escalate on another endpoint's behalf.
     */
    if (XHCI_TRB_GET_SLOT_ID(eventDw3) != slotId ||
        XHCI_TRB_GET_EP_ID(eventDw3) != dci) {
        queue->ForeignEvents++;
        return XHCI_XFER_OK;
    }

    completionCode = XHCI_TRB_GET_COMPLETION(eventDw2);
    residual = XHCI_TRB_GET_RESIDUAL(eventDw2);

    if (XhciXferCodeInfo(completionCode, &code) != XHCI_XFER_OK) {
        /*
         * An unassigned code, or one Table 6-90 gives to another event family.
         * Nothing here knows what the controller did with the TRBs, so nothing
         * is retired and no transfer is completed on it: the visible failure is
         * the counter plus usbport's own URB timeout, which is armed on every
         * SubmitTransfer that returned success. Treating it as success is the
         * one option that loses data silently.
         */
        queue->BadCodes++;
        return XHCI_XFER_OK;
    }
    if (code.Fatal) {
        result->Fatal = 1;
    }
    /*
     * A Stopped event (codes 26-28) means software stopped the ring, and what
     * that does to the queued transfers is the slot layer's decision: a PAUSED
     * pre-emption cancels nothing and an abort keeps the transfers usbport did
     * not withdraw, so `XhciSlotTransferEvent` routes the three codes to
     * `XhciXferQueueStopped` before this is reached. Refused here so that the
     * contract is enforced where a caller could break it, rather than left to
     * the one caller that keeps it: completing the owner as cancelled and
     * sweeping everything ahead of it is the wrong answer for every stop but a
     * drain.
     */
    if (code.Class == XHCI_XFER_CC_CANCELED) {
        queue->StoppedRefused++;
        return XHCI_XFER_OK;
    }

    if (XhciRingIndexFromPA(ring, eventTrbPA, &reportedIndex) != XHCI_RING_OK) {
        /* Zero (an error the xHC could not attribute to a TRB, 4.11.3.1), an
         * address on another ring, or a misaligned value. */
        queue->ForeignEvents++;
        return XHCI_XFER_OK;
    }

    owner = NULL;
    ahead = 0;
    for (walk = queue->Head; walk != NULL; walk = walk->Next) {
        if (xhciXferRangeContains(ring, walk->FirstIndex, walk->TrbCount,
                                  reportedIndex, NULL)) {
            owner = walk;
            break;
        }
        ahead++;
    }
    if (owner == NULL) {
        /* The trailing events one TD can legitimately produce land here, as do
         * events for a transfer already completed. Expected, not an error
         * (docs/contributing/implementation-invariants.md, "Completion Matching"). */
        queue->UnmatchedEvents++;
        /* One of those trailing events is the second half of the mid-TD
         * departure taken below, and recognising it is the only thing that
         * distinguishes a conforming controller from QEMU's one-event xHC. */
        if (xhciXferClaimMidTdTail(queue, reportedIndex)) {
            queue->MidTdShortTails++;
        }
        return XHCI_XFER_OK;
    }

    /* Snapshotted **before** anything below sets it, because two decisions rest
     * on "had this transfer already been measured when this event arrived": the
     * length must not be moved by a later event, and a repeat of the same short
     * condition must not be counted twice. */
    lengthFixed = (owner->Flags & XHCI_XFER_FLAG_LENGTH_FIXED) ? 1UL : 0UL;

    haveBytes = xhciXferResidualBytes(queue, ring, owner, reportedIndex,
                                      residual, &code, &bytes, &rejected);
    if (rejected) {
        /*
         * The arithmetic produced something impossible. Fail the transfer
         * visibly rather than reporting a length nothing measured - but do
         * **not** let that decide when the transfer ends. Ending it here would
         * detach a transfer whose TRBs the controller is still executing, on a
         * ring that has not stopped, which is a worse outcome than the wrong
         * length: the position rules below are what keep the software and
         * hardware dequeue pointers together.
         */
        owner->UsbdStatus = XHCI_USBD_STATUS_INTERNAL_HC_ERROR;
        owner->BytesTransferred = 0;
        owner->Flags |= (XHCI_XFER_FLAG_FAILED | XHCI_XFER_FLAG_LENGTH_FIXED);
    } else if (haveBytes) {
        /*
         * **Any measurement fixes the length.** `XHCI_XFER_FLAG_LENGTH_FIXED`
         * does not mean "the transfer stopped here" - it means the controller
         * has reported a byte count, so the terminal event's "a successful
         * transfer moved everything it asked for" is no longer the best answer
         * available and must not overwrite it.
         *
         * Keying this on the completion *code* instead is what let two separate
         * overreports through, and both had the same shape: a length the
         * controller actually reported, thrown away in favour of
         * `RequestedLength`. The first was a Success carrying a nonzero residual
         * (the spurious-success quirk below). The second was a Success carrying
         * a residual of **zero** on a non-final data TRB, which measures the
         * bytes moved *so far*: if the controller then went to the Status Stage
         * rather than on to the next data TRB, the transfer really did end
         * there, and reporting the full request claims bytes that never arrived.
         *
         * The asymmetry that decides it: an underreport is a short descriptor,
         * which usbport and usbhub see and retry or fail on; an overreport is a
         * buffer whose tail was never written being read as valid data. Prefer
         * the visible failure. And nothing legitimate is lost - see the
         * `IntermediateEvents` counter, which is the reason.
         *
         * A transfer that has already failed keeps the length it had when it
         * failed; a later event may not put a byte count back on it.
         */
        /*
         * **The *first* measurement fixes it, and later ones may not move it.**
         * The tenth review found this: `XHCI_XFER_FLAG_LENGTH_FIXED` was set
         * here and consulted only at the terminal event, so a second measuring
         * event overwrote the first.
         *
         * It could not happen while every TD was a control transfer's, because
         * there the group's last TRB is the Status Stage TRB and carries no
         * length - so the terminal event never measured anything. A **Normal**
         * TD's last TRB *is* a data TRB - interrupt from task 7a-A.2, bulk from
         * 8-A.1, and a long bulk TD is where it is easiest to see - and the spec
         * requires the controller to
         * emit exactly that second event: "In the second event, the Completion
         * Code shall be set to Short Packet, and the TRB Transfer Length should
         * be set to the same value that was reported by the initial Short Packet
         * Event" (4.10.1.1.2, p.175).
         *
         * Concretely, TRB lengths 64/64/32 with 32 bytes delivered on the first:
         * the Short Packet event on TRB 1 measures 64 - 32 = 32 and is right;
         * the repeat on TRB 3 measures 160 - 32 = 128 and is the same residual
         * against a different sum. Reporting 128 hands usbport 96 bytes of
         * buffer the device never wrote.
         */
        if (!(owner->Flags & XHCI_XFER_FLAG_FAILED) && !lengthFixed) {
            owner->BytesTransferred = bytes;
        }
        owner->Flags |= XHCI_XFER_FLAG_LENGTH_FIXED;
    }

    /*
     * A short packet or an error also fixes the length even when it measured
     * nothing here - the transfer stops there, and the second event the spec
     * promises for the same condition "should be set to the same value that was
     * reported by the initial Short Packet Event" (p.175) rather than adding to
     * it.
     *
     * **And that includes the event that named a TRB outside the data range**,
     * which is the case worth stating because it is the one that looks like an
     * oversight: a Short Packet event reported against a Setup or Status Stage
     * TRB measures nothing (`xhciXferResidualBytes` drops it, counting
     * `ResidualIgnored`), so the length stays 0 and the terminal default at the
     * bottom of this function - "moved everything it asked for" - is suppressed.
     * The transfer then reports 0 bytes.
     *
     * That is deliberate, and it is the same asymmetry the measurement arm above
     * turns on: only nonconforming hardware produces such an event, and of the
     * two available answers, 0 is an underreport that usbport and usbhub see as
     * a short descriptor and retry or fail on, while `RequestedLength` is a
     * buffer whose tail nothing wrote being handed back as valid data. Letting
     * the default run - which is what "only fix the length inside the data
     * range" would do - picks the invisible one. Prefer the visible failure.
     */
    if (code.Class != XHCI_XFER_CC_SUCCESS) {
        owner->Flags |= XHCI_XFER_FLAG_LENGTH_FIXED;
    }
    /*
     * Counted once per short *condition*, not once per event reporting it. The
     * spec's second event repeats the first (p.175), and on a Normal TD both
     * land inside the same transfer - so the already-fixed length is what tells
     * them apart. Without this one short HID report, or one short bulk read,
     * counts as two.
     */
    if (code.Class == XHCI_XFER_CC_SHORT && !lengthFixed) {
        queue->ShortPackets++;
    }

    /*
     * The two shapes a Success event on a **data** TRB can take, both counted,
     * because in this driver's TRB layout neither should happen at all: IOC is
     * on the Status Stage TRB alone (p.430), and ISP produces code 13 rather
     * than code 1, so no data TRB has any reason to raise a Success event.
     *
     *   - With a nonzero residual it is the **spurious-success quirk**
     *     (`docs/usb-xhci-info/xhci-programming.md`): NEC uPD720200, Fresco Logic FL1000 and
     *     early VIA VL800 revisions "return Transfer Event completion code 1
     *     (Success) for transfers that actually delivered fewer bytes than
     *     requested", with the length field still correct. Linux carried this as
     *     `XHCI_TRUST_TX_LENGTH` before making it every controller's default.
     *   - With a zero residual it is an **intermediate event** - the shape the
     *     spec produces for an IOC on a TRB that is not the group's last, which
     *     this driver never sets. It is counted rather than ignored because it
     *     is now load-bearing: it fixes the length, so a controller that emits
     *     one *and then completes the TD normally* would be underreported, and
     *     this counter is the only thing that would say so. Zero on every
     *     conforming controller.
     *
     *     **"Not the group's last TRB" is the whole of that definition**, and
     *     the tenth review found the code had been relying on a coincidence
     *     instead. A control transfer's last TRB is the Status Stage TRB, which
     *     has no length, so `haveBytes` was false there and the test never had
     *     to be written. A Normal TD's last TRB is a data TRB carrying IOC, so
     *     **every ordinary successful interrupt or bulk transfer** landed here -
     *     and a counter documented as "zero on every conforming controller"
     *     became one per HID report, which destroys the only reading that would
     *     have said a transfer was underreported.
     *
     * Kept apart from `ShortPackets` and from each other because the three have
     * different diagnoses - a controller reporting a short transfer as the spec
     * requires, one that does not, and one talking about TRBs nobody asked it
     * to report on.
     */
    if (haveBytes && code.Class == XHCI_XFER_CC_SUCCESS) {
        if (residual != 0) {
            queue->ShortSuccesses++;
        } else if (reportedIndex != owner->LastIndex) {
            queue->IntermediateEvents++;
        }
    }
    if (code.Class == XHCI_XFER_CC_ERROR ||
        code.Class == XHCI_XFER_CC_CANCELED) {
        /* "If any event generated by a TD reports an error, then that
         * Completion Code overrides any Successful Completion Codes that other
         * TRBs associated with the TD may have asserted, whether they come
         * before or after the error Event" (p.214). */
        owner->UsbdStatus = code.UsbdStatus;
        owner->Flags |= XHCI_XFER_FLAG_FAILED;
    }

    /*
     * When does this event end the transfer?
     *
     *   - It named the Status Stage TRB, the last TRB of the group and the only
     *     one carrying IOC. That is the ordinary completion.
     *   - Or the transfer cannot continue: any error halts a control endpoint
     *     ("All Transfer Ring error conditions force the state of the associated
     *     endpoint to Halted", p.176), so the Status Stage will never run and no
     *     further event is coming; and codes 26-28 mean software stopped the
     *     ring under it.
     *
     *   - Or it is a short packet on a TD that is **entirely data**, which is
     *     the receive fix and the one deliberate departure from the spec here.
     *     See below for why the shape test is what decides it.
     *
     * A short packet on a control transfer is none of those. The xHC "shall
     * advance to the Status Stage TD" (p.433), so the transfer's own completion
     * event is still on its way and the length just latched is what it will
     * report. Nothing is retired for it either, deliberately: leaving the whole
     * group outstanding until the transfer ends is what keeps the range lookup
     * above and the length sum below reading TRBs that are still this
     * transfer's.
     *
     * **The short-packet departure.** 4.10.1.1.2 p.175 says software "shall not
     * interpret a Short Packet Event as indicating that the TD ... is complete,
     * unless the TRB Pointer field ... references the last TRB of the TD", and
     * promises the missing tail event outright: "two events shall be generated
     * ... one for the Transfer TRB that the Short Packet occurred on, and a
     * second for the last TRB with the IOC flag set". Waiting for that second
     * event is what this code did, and it is what the spec asks for - but QEMU's
     * xHC emits only the first, so a multi-TRB IN TD that ends short is never
     * retired, the transfer never completes up to usbport, and the vendor driver
     * never posts another receive. That is batch 8-V.2's dead bulk IN endpoint:
     * one 362-byte frame, then nothing (`docs/contributing/lessons.md`).
     *
     * Completing here instead is safe rather than merely pragmatic, for two
     * reasons that hold on a conforming controller too:
     *
     *   - The second event carries no new information. p.175 requires its
     *     length to "be set to the same value that was reported by the initial
     *     Short Packet Event", and the length latched above is already held by
     *     `XHCI_XFER_FLAG_LENGTH_FIXED`.
     *   - The xHC has provably finished the TD before the first event is
     *     visible: on a short packet it "shall advance to the first TRB of the
     *     next TD or the Enqueue Pointer" (4.11.5.2 p.210). So no TRB reclaimed
     *     here is one the controller is still executing.
     *
     * On a conforming controller the second event then names a TRB below the
     * dequeue pointer, belonging to a transfer no longer on this queue, and the
     * owner search above rejects it as `UnmatchedEvents` - which is where the
     * no-double-completion guarantee comes from. Those TRBs cannot be re-let to
     * a later transfer before the enqueue pointer laps the whole ring, because
     * `XhciRingHasRoom` will not let it pass the dequeue pointer.
     *
     * That same arrival is also a *measurement* of which controller this is,
     * and taking it is `MidTdShortTails`: the tail index is recorded here and
     * claimed there. Before task 9-0.2, `MidTdShortRetires` alone could not
     * answer the question, because the retire moved on the first event and both
     * kinds of controller send that. **It can now**: the departure is taken at
     * the end of an empty drain pass, and a conforming controller's tail is
     * consumed in the same pass and ends the transfer positionally, so such a
     * machine never reaches the early retire at all. The primary verdict is
     * `MidTdDeferralsTailed` against `MidTdDeferrals`; this pair measures the
     * residual window that fix leaves open.
     *
     * Linux does the same and has since long before this driver: `case
     * COMP_SHORT_PACKET:` in `process_bulk_intr_td` sets `td->status = 0` and
     * falls through to `finish_td` whether or not the event named `td->end_trb`
     * (`docs/contributing/failure-diagnosis.md` trust order #2 - Linux outranks this repo's
     * own reading of the spec for xHCI hardware behaviour).
     *
     * **The shape test is the whole of the restriction.** A TD that is entirely
     * data has nothing after it, so a short packet ends the transfer. A control
     * transfer's Data Stage does not: the Status Stage TD still has to run, its
     * TRB carries the group's only IOC (p.430), and retiring the data TD would
     * strand that TRB outstanding for the life of the ring. The first attempt at
     * this fix relaxed `XhciRingClassifyEvent` instead, where the two cannot be
     * told apart, and `test_ring.c`'s control vectors caught it.
     *
     * Its two halves are one claim - "the data range *is* the whole placement" -
     * and either half alone decides every shape this driver *builds*: a control
     * transfer differs in both, its data starting one TRB in and never covering
     * the Setup and Status TRBs. So for a long time a mutation dropping either
     * conjunct failed **zero** checks, and this comment recorded the conjunction
     * as consistency rather than as something the suite justified.
     *
     * It is justified now. `test_event_half_data_range_refuses_early_retire`
     * constructs the two half-mismatched records directly - neither is a shape
     * the builder emits, which is the point - and each conjunct is the only
     * thing that refuses one of them (the repo audit, finding 3). Keep
     * both: Phase 9's isochronous TDs are the shape expected to satisfy one and
     * not the other, and the guard is now tested before it is needed rather
     * than after.
     */
    wholeTdIsData = (owner->DataFirstIndex == owner->FirstIndex &&
                     owner->DataTrbCount == owner->TrbCount) ? 1 : 0;

    /*
     * Taken here rather than inside the retire decision below, because the arm
     * that follows needs the same answer: whether the *ring's* own chain walk
     * agrees with this record about where the TD ends. One call, one answer -
     * two would be two opinions that a change can move apart.
     */
    haveCompletion = (XhciRingClassifyEvent(ring, eventTrbPA, completionCode,
                                            &completion) == XHCI_RING_OK)
                     ? 1UL : 0UL;

    /*
     * **Task 9-0.2: the departure is armed here and taken somewhere else.**
     *
     * Retiring the TD on this event frees its TRBs while the tail p.175 promises
     * may still be in the event ring unread - and a Transfer Event names a TRB
     * *address*, not a generation. Once those TRBs are re-let, that tail is
     * indistinguishable from the new TD's own event and the owner search matches
     * it to a live transfer, measuring or completing it wrongly: a truncated
     * bulk IN reported as success. That is the repo audit finding 21, and
     * `MidTdTailsCensored` only ever *reported* it.
     *
     * So the transfer stays on the queue, still owning its TRBs, and the
     * decision moves to the end of a drain pass that found the event ring
     * **empty** (`XhciXferDrainSettled`). Two things follow, and the second is
     * the point:
     *
     *   - A pass qualifies by having **observed the ring empty**, not by which
     *     exit its loop took. A pass that stopped at its bound with events
     *     still in it does not qualify - the tail may be one of them - but one
     *     that stopped at its bound and then peeked an empty ring does.
     *     Conflating those two stranded a deferral whenever the bound and the
     *     controller's last event coincided.
     *   - **On a conforming controller the departure is then never taken at
     *     all.** The tail is already queued behind the short event, so it is
     *     consumed in the same drain, lands on `owner->LastIndex`, and ends the
     *     transfer through the ordinary positional rule below. The one-event
     *     controller the receive fix was written for is the only one that ever
     *     reaches the settle - which is what turns `MidTdShortRetires` from a
     *     number both machines produce into a reading.
     *
     * `completionCode == XHCI_CC_SHORT_PACKET` rather than the class: it is the
     * exact code the settle replays into `XhciRingClassifyEvent`, and today it
     * is the class's only member, so testing the code here is what keeps the two
     * from drifting apart if that ever stops being true.
     *
     * What this does **not** close: a tail the xHC writes after a pass has read
     * the ring empty still arrives against re-let TRBs. That window is now the
     * only one, where before it was joined by every bounded exit and every tail
     * still sitting in the ring; closing it outright needs event identity, which
     * means Event Data TRBs, which this driver deliberately places none of.
     * `MidTdShortTails` measures exactly that residue.
     */
    if (haveCompletion && !completion.CanRetire && wholeTdIsData &&
        completionCode == XHCI_CC_SHORT_PACKET &&
        completion.TailIndex == owner->LastIndex) {
        if (!(owner->Flags & XHCI_XFER_FLAG_SHORT_DEFERRED)) {
            owner->Flags |= XHCI_XFER_FLAG_SHORT_DEFERRED;
            owner->ShortTrbPA = eventTrbPA;
            queue->MidTdDeferrals++;
        }
        return XHCI_XFER_OK;
    }

    /*
     * The deferral's other resolution: the promised tail arrived while the
     * transfer was still queued, so the ordinary rules below complete it and
     * nothing was ever retired early. **This is the conforming controller's
     * whole path**, and counting it is the verdict.
     *
     * Only the true tail counts - p.175 requires the second event to carry Short
     * Packet on the TD's last TRB. A deferred transfer ended by anything else
     * (an error, a cancellation) keeps the flag, and the unlink counts it as an
     * observation lost; letting those in here would inflate the reading in the
     * direction that says "conforming".
     */
    if ((owner->Flags & XHCI_XFER_FLAG_SHORT_DEFERRED) != 0 &&
        reportedIndex == owner->LastIndex &&
        completionCode == XHCI_CC_SHORT_PACKET) {
        owner->Flags &= ~XHCI_XFER_FLAG_SHORT_DEFERRED;
        queue->MidTdDeferralsTailed++;
    } else if ((owner->Flags & XHCI_XFER_FLAG_SHORT_DEFERRED) != 0 &&
               reportedIndex == owner->LastIndex &&
               completionCode == XHCI_CC_SUCCESS) {
        /*
         * **The same resolution, by a controller that used the wrong code.**
         * A second event did arrive on the TD's last TRB, which is the thing
         * the verdict is actually asking about; it just carried Success where
         * p.175 says Short Packet. Linux carries explicit handling for hosts
         * that emit exactly this after a short packet, so it is a known quirk
         * rather than a fault, and nothing about the transfer changes: the
         * positional rule below completes it and `XHCI_XFER_FLAG_LENGTH_FIXED`
         * already holds the short measurement, so the length cannot be
         * overwritten by this event's.
         *
         * It is counted apart rather than folded into the strict counter,
         * because the strict counter is a conformance test and this is not
         * conforming. But it must not be left to fall through to the unlink
         * either: that counted it as `MidTdDeferralsLost`, making a controller
         * that sends two events indistinguishable from a teardown - on the one
         * measurement task 9-0.2 exists to produce. Found by the batch-9-0
         * review round 1.
         */
        owner->Flags &= ~XHCI_XFER_FLAG_SHORT_DEFERRED;
        queue->MidTdDeferralsTailedSpurious++;
    }

    terminal = (reportedIndex == owner->LastIndex ||
                code.Class == XHCI_XFER_CC_ERROR ||
                code.Class == XHCI_XFER_CC_CANCELED ||
                (wholeTdIsData && code.Class == XHCI_XFER_CC_SHORT)) ? 1 : 0;
    if (!terminal) {
        return XHCI_XFER_OK;
    }

    /* A successful transfer that never reported a length moved everything it
     * asked for: the Status Stage TRB has no length field of its own. */
    if (!(owner->Flags & XHCI_XFER_FLAG_LENGTH_FIXED)) {
        owner->BytesTransferred = owner->RequestedLength;
    }

    /*
     * Ring ownership is the ring layer's decision, not this one's - but it is a
     * decision about **one TD**, and a transfer is two or three of them. A
     * retire jumps the dequeue pointer past the *matched* TD's tail, so it is
     * the right and complete answer only when the event named the group's last
     * TRB. An error on the Data Stage TD's last TRB satisfies CanRetire just as
     * well, and retiring on it would reclaim the data TD while leaving the
     * Status Stage TRB of a transfer that no longer exists outstanding for the
     * life of the ring - a slot lost per failed transfer, with no error
     * anywhere to say why.
     *
     * So: retire only at the group's end. Every other way a transfer ends is an
     * error or a stop, and there the position is placed explicitly, which is
     * the spec's own instruction anyway - "software shall use a Set TR Dequeue
     * Pointer Command to advance the Transfer Ring to the next TD" (p.172).
     */
    retired = 0;
    if (haveCompletion) {
        if (completion.CanRetire && reportedIndex == owner->LastIndex &&
            XhciRingRetireTd(ring, &completion) == XHCI_RING_OK) {
            retired = 1;
        }
        /*
         * There is no mid-TD branch here any more. A whole-data TD ending short
         * with the ring agreeing was armed above and returned; every other way
         * a retire can fail to happen is caught by the outcome test below,
         * which is deliberately one test rather than one per shape - the
         * shape-specific version of it missed an event naming the record's last
         * TRB while the ring disagreed, and two overlapping rules is how the
         * next shape gets missed again.
         */
        if (completion.NeedsRecovery) {
            result->NeedsRecovery = 1;
            queue->Recoveries++;
        }
    }

    /*
     * **The group ended, nothing was reclaimed, and nothing halted the
     * endpoint.** That is the general statement of the divergence the two
     * checks above catch only one shape of, and the batch-9-0 review round 6
     * found the gap: the interception before `terminal` tests
     * `reportedIndex != owner->LastIndex`, so an event naming the record's
     * *last* TRB while the **ring** says that index is not the TD's tail - a
     * `CH` bit merging this TD into the next one - sails past it, is terminal
     * by position, fails to retire, and used to be completed and then handed to
     * the halt recovery.
     *
     * So the test is made about the *outcome* rather than the shape: no retire,
     * no recovery already owed, and a completion code that halts nothing. A
     * Short Packet or a Success is such a code, and after either the endpoint
     * is still **Running** - so its TRBs are still the xHC's, its queue may not
     * be answered, and it needs a Stop Endpoint before either becomes legal.
     *
     * An error or a cancellation is deliberately excluded and still falls
     * through to `xhciXferFinishGroup`: those codes stop the endpoint running -
     * Halted for the general case ("all Transfer Ring error conditions force
     * the state of the associated endpoint to Halted", p.176), Error for a TRB
     * Error (4.8.3 p.149), Stopped for codes 26-28 - and *not running* is
     * precisely what makes completing safe and makes the recovery below the
     * right one. Which command each of those needs is `xhciEpRecoveryNeeded`'s
     * decision, not this layer's.
     */
    if (!retired && !result->NeedsRecovery &&
        code.Class != XHCI_XFER_CC_ERROR &&
        code.Class != XHCI_XFER_CC_CANCELED) {
        queue->OrphanedGroups++;
        result->NeedsRecovery = 1;
        result->RefusedRetire = 1;
        return XHCI_XFER_OK;
    }

    xhciXferFinishGroup(queue, ring, owner, ahead, retired, result);
    return XHCI_XFER_OK;
}

/*
 * Task 9-0.2. A drain pass has emptied the event ring; settle the head-most
 * transfer still waiting for a tail that will now never come.
 *
 * **Why the end of an empty pass is the right instant.** A Transfer Event
 * names a TRB address, not a generation, so the moment a short TD's TRBs are
 * re-let, the tail 4.10.1.1.2 p.175 promises becomes indistinguishable from
 * the new TD's own event and the owner search matches it to a live transfer -
 * a truncated bulk IN reported as success (the repo audit, finding 21).
 * Holding the transfer until the ring reads empty means every tail the
 * controller had already written has been consumed and matched normally, so a
 * TD still short at this point is one whose tail was not sent. On a conforming
 * controller that never happens and this function retires nothing, which is
 * what makes `MidTdShortRetires` a reading rather than a number both kinds of
 * controller produce.
 *
 * **What it does not close, stated rather than implied**: the xHC may write
 * the tail just after the pass read the ring empty. That window survives, and
 * closing it needs event identity - Event Data TRBs, which this driver
 * deliberately places none of. It is now the only one, where before it was
 * joined by every bounded exit and by every tail still unread in the ring.
 * `MidTdShortTails` measures exactly that residue, and on a conforming
 * controller it should now read zero along with everything it qualified.
 *
 * The caller's gate is the **observation** that the ring is empty, not which
 * exit its drain loop took. A pass that stopped at `XHCI_DPC_MAX_EVENTS` may
 * call this if a peek then shows the ring empty, and must not if events remain
 * - the tail may be one of them. Conflating the two stranded a deferral
 * whenever the bound and the controller's last event coincided.
 *
 * One transfer per call, `Action` saying whether anything was settled, so the
 * caller loops - which keeps every completion going through the same fold and
 * the same `UsbPortCompleteTransfer` path a Transfer Event's does, rather than
 * through a second accounting written beside it.
 *
 * IRQL: called at DISPATCH_LEVEL under the controller lock; pure, so any IRQL
 * is safe.
 */
ULONG XhciXferDrainSettled(PXHCI_TRANSFER_QUEUE queue,
                           PXHCI_RING ring,
                           PXHCI_XFER_EVENT_RESULT result)
{
    XHCI_TD_COMPLETION completion;
    PXHCI_TRANSFER walk;
    PXHCI_TRANSFER owner;
    ULONG ahead;
    ULONG retired;

    if (result == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }
    result->Action = XHCI_XFER_ACTION_NONE;
    result->Completed = NULL;
    result->CompletedCount = 0;
    result->NeedsRecovery = 0;
    result->RefusedRetire = 0;
    result->Fatal = 0;

    if (queue == NULL || ring == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }

    owner = NULL;
    ahead = 0;
    for (walk = queue->Head; walk != NULL; walk = walk->Next) {
        if ((walk->Flags & XHCI_XFER_FLAG_SHORT_DEFERRED) != 0) {
            owner = walk;
            break;
        }
        ahead++;
    }
    if (owner == NULL) {
        return XHCI_XFER_OK;
    }

    /*
     * Cleared before anything can fail, and unconditionally: this **is** the
     * settle, and there is no later pass that would give the deferral a second
     * chance. Leaving it set on a refused retire would strand the transfer on
     * the queue for the life of the endpoint with nothing scheduled to look at
     * it again.
     */
    owner->Flags &= ~XHCI_XFER_FLAG_SHORT_DEFERRED;

    /*
     * The classification is re-taken from **the TRB the short event named**,
     * not from `LastIndex`: what has to be established is that the ring's own
     * chain walk still puts that TRB and this record's last TRB in one TD.
     * Classifying from `LastIndex` would ask the ring whether `LastIndex` is
     * `LastIndex`. The ring may also have moved since the event - a Set TR
     * Dequeue between the two would make the retire stale - and
     * `XhciRingRetireAdvancedTd` is what refuses that.
     *
     * `XHCI_CC_SHORT_PACKET` is not a stored value: the arm site tests for
     * exactly that code, so replaying it here is replaying what happened.
     */
    retired = 0;
    if (XhciRingClassifyEvent(ring, owner->ShortTrbPA, XHCI_CC_SHORT_PACKET,
                              &completion) == XHCI_RING_OK &&
        !completion.CanRetire &&
        completion.TailIndex == owner->LastIndex &&
        XhciRingRetireAdvancedTd(ring, &completion,
                                 XHCI_CC_SHORT_PACKET) == XHCI_RING_OK) {
        retired = 1;
        queue->MidTdShortRetires++;
        /* Recorded after the retire succeeded, not before: a refused retire
         * waited for nothing, so there is no promised tail to account for and
         * a record here would understate conformance. */
        xhciXferRecordMidTdTail(queue, owner->LastIndex);
    }

    if (!retired) {
        /*
         * **The ring refused, so this transfer is not answered here at all.**
         *
         * The first fix for this branch completed the transfer anyway and asked
         * the slot layer to reprogram the position, on the reasoning that the
         * event path answers the same divergence the same way. The batch-9-0
         * review round 2 refuted the analogy: on the event path the endpoint is
         * *halted* or in *Error* by the time the orphan case is reached, and it
         * is that state which makes both the completion and a Set TR Dequeue
         * legal. Here the endpoint is **Running** - this branch's whole premise
         * is that nothing halted it - and two things follow that the analogy
         * hid. A Set TR Dequeue Pointer "may be executed only if the target
         * endpoint is in the Error or Stopped state" (4.6.10 p.126), so the
         * command would be answered Context State Error; and completing the
         * transfer hands its mapped buffer back to usbport while a running
         * endpoint may still own or have prefetched the very TRBs that name it.
         *
         * So the transfer stays queued and the caller is told to quiesce the
         * endpoint. A Stop Endpoint is what transfers ownership of the ring's
         * TDs to software (4.11.4.8), and only after it completes may the
         * position be programmed and the queue answered - which is the drain
         * the caller arms, not anything this layer can do.
         *
         * **The accounting still happens here**, because this is where the
         * observation ends: the flag was cleared above, so the unlink hook will
         * not see it when the drain finally detaches the transfer, and no
         * retire happened, so `MidTdShortRetires` did not move either. Without
         * this the deferral is counted by nothing, the identity
         * `XhciSlotDrainSettled` checks comes up one short, and the sticky
         * `MidTdDeferAccountingBroken` fires - which by its own rule makes
         * every mid-TD counter unsafe to read. `Lost` is the right term: a
         * record/ring divergence answers nothing about whether the tail was
         * ever sent.
         */
        queue->MidTdDeferralsLost++;
        result->NeedsRecovery = 1;
        result->RefusedRetire = 1;
        return XHCI_XFER_OK;
    }

    xhciXferFinishGroup(queue, ring, owner, ahead, retired, result);
    return XHCI_XFER_OK;
}

ULONG XhciXferDeferralsArmed(const XHCI_TRANSFER_QUEUE *queue)
{
    const XHCI_TRANSFER *walk;
    ULONG armed;

    if (queue == NULL) {
        return 0;
    }
    armed = 0;
    for (walk = queue->Head; walk != NULL; walk = walk->Next) {
        if ((walk->Flags & XHCI_XFER_FLAG_SHORT_DEFERRED) != 0) {
            armed++;
        }
    }
    return armed;
}

/* ------------------------------------------------------------------ */
/* 9-A.1: isochronous transfers                                        */
/*                                                                     */
/* One usbport request of N packets is N Isoch TDs published as one     */
/* group. Everything below keeps packet *i* and TD *i* the same thing:  */
/* the builder emits them in order, the layout's TdLengths[i] is TD i's */
/* extent, and the event path recovers *i* by counting TD boundaries    */
/* from the group's first TRB. That identity is what makes a per-packet */
/* status writable at all - usbport's block is indexed by packet and    */
/* the controller reports by TRB.                                       */
/* ------------------------------------------------------------------ */

ULONG XhciXferIsoCodeInfo(ULONG completionCode, PXHCI_XFER_CODE info)
{
    if (info == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }
    xhciXferCodeSet(info, XHCI_XFER_CC_INVALID,
                    XHCI_USBD_STATUS_INTERNAL_HC_ERROR, 0, 0);

    switch (completionCode) {
    /*
     * Reinterpreted rather than inherited - see the contract in the header.
     * `ResidualIsBytes` stays 1: the length field is a residual whichever value
     * the status carries, and the byte count is what the client driver's audio
     * buffer is filled from.
     */
    case XHCI_CC_SHORT_PACKET:
        xhciXferCodeSet(info, XHCI_XFER_CC_SHORT,
                        XHCI_USBD_STATUS_DATA_UNDERRUN, 1, 0);
        return XHCI_XFER_OK;

    /*
     * "The data associated with the TD in error shall be lost, however for the
     * next ESIT the xHC shall advance to the next Isoch TD and attempt to
     * execute it" (4.10.3.2 p.187).
     *
     * `ResidualIsBytes` is **0**, and that is the whole content of this case.
     * The event does carry a number - "its TRB Transfer Length the residue data
     * bytes in the buffer" - but the sentence above says the data is lost, so
     * `length - residual` would report bytes that went nowhere. A missed packet
     * moved nothing; reporting a plausible partial length for it is how a
     * dropped interval becomes silent corruption in the client's buffer.
     */
    case XHCI_CC_MISSED_SERVICE:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_NOT_ACCESSED, 0, 0);
        return XHCI_XFER_OK;

    /*
     * The device sent more than the TD had room for. Same reading as Babble in
     * the shared decoder, and the same status: from the client's side it is a
     * buffer that overran.
     */
    case XHCI_CC_ISOCH_BUFFER_OVERRUN:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_BUFFER_OVERRUN, 1, 0);
        return XHCI_XFER_OK;

    /*
     * **Bandwidth Overrun is this driver's own arithmetic coming back at it.**
     * The xHC raises it when a TD's Transfer Size exceeds the Max ESIT Payload
     * (4.14.2.1 p.238) - a condition `XhciXferBuildIso` refuses outright, from
     * the same Endpoint Context field. So one arriving means the context the
     * controller holds and the one this driver believes it programmed disagree,
     * which is a host-controller-layer failure rather than anything the device
     * did. Reporting it as a data error would send a reader looking at the wire.
     */
    case XHCI_CC_BANDWIDTH_OVERRUN:
        xhciXferCodeSet(info, XHCI_XFER_CC_ERROR,
                        XHCI_USBD_STATUS_INTERNAL_HC_ERROR, 1, 0);
        return XHCI_XFER_OK;

    /*
     * Neither names a TD (4.10.3.1 p.185), so there is no packet for a decode to
     * be about. Refused here rather than given a status, because the device
     * layer intercepts both by completion code before this is reached and a
     * mapping would be a second answer nobody consults - until somebody does.
     */
    case XHCI_CC_RING_UNDERRUN:
    case XHCI_CC_RING_OVERRUN:
        return XHCI_XFER_BAD_PARAM;

    default:
        break;
    }

    /*
     * **Everything else is asked of the ring layer's list first**, and that is
     * the batch 9-A review's fifth MAJOR: this function used to fall straight
     * through to the shared decoder, which serves the *endpoint* ring kind and
     * therefore accepts Stall Error and Invalid Stream ID. Neither is legal on an
     * isochronous ring - an isoch pipe has no handshake to report a stall with
     * (p.177) and this driver implements no streams - so a Stall arriving here,
     * from a controller fault or a misattributed event, was decoded into
     * `STALL_PID`, written into usbport's block, and allowed to retire the group
     * and hand its mapped buffer back. The ring *kind* exists to reject exactly
     * that, and the per-packet path was the one route around it.
     *
     * Asked of `XhciRingCompletionCodeValid` rather than restated here, because a
     * second copy of the list is how the two drift.
     */
    if (!XhciRingCompletionCodeValid(XHCI_RING_KIND_ISOCH, completionCode)) {
        return XHCI_XFER_BAD_PARAM;
    }

    /* What is left means on an isoch ring exactly what it means anywhere else -
     * Success, the transaction errors, the stopped family, Event Lost - and is
     * decoded once, in the shared decoder. */
    return XhciXferCodeInfo(completionCode, info);
}

ULONG XhciXferFrameIdUsable(const XHCI_ISO_FRAME_POLICY *policy,
                            ULONG frameNumber,
                            ULONG *frameId)
{
    ULONG id;
    ULONG distance;
    ULONG windowStart;

    if (policy == NULL || frameId == NULL) {
        return 0;
    }
    *frameId = 0;
    if (!policy->Allowed) {
        return 0;
    }
    /*
     * A threshold that swallows the whole window. IST is four encoded bits, so
     * at most 7 frames against a window of 895 - unreachable in practice, and
     * checked anyway because the alternative is a start bound above the end
     * bound and a test that silently accepts everything.
     */
    windowStart = policy->IstFrames + 1;
    if (windowStart > XHCI_FRAME_ID_WINDOW_END) {
        return 0;
    }

    /*
     * **A distance, and taken in the 32-bit domain rather than in the Frame ID's
     * own 11 bits.**
     *
     * A distance is required because the Frame ID space has no ordering: at
     * current = 2040 the frames 2041..2047 and 0..7 are all in the near future
     * and half of them carry the smaller number, so a magnitude comparison is
     * right for half of every second and wrong for the other half.
     *
     * Taking it in 11 bits was the *second* half of the same mistake, and the
     * batch 9-A review's fourth MAJOR. Masking first makes a stale stamp
     * indistinguishable from a future one - 2,000 frames in the past is 48 frames
     * ahead once both are reduced mod 2,048 - so a usbport stamp left over from a
     * lap ago was accepted and given an explicit Frame ID naming a frame that had
     * already passed, which is a Missed Service Error per TD until the pipe
     * resynchronizes. The subtraction is therefore done on the full 32-bit
     * numbers, where a stale stamp lands near 2^32 and no window accepts it, and
     * only the *result* is compared against the spec's two bounds. The Frame ID
     * written into the TRB is still the low 11 bits, because that is the field's
     * width.
     */
    distance = frameNumber - policy->CurrentFrame;
    if (distance < windowStart || distance > XHCI_FRAME_ID_WINDOW_END) {
        return 0;
    }
    id = frameNumber & XHCI_FRAME_ID_MASK;
    *frameId = id;
    return 1;
}

typedef struct _XHCI_ISO_BUILD_STATE {
    XHCI_TRB *Out;
    ULONG Capacity;
    ULONG Count;            /* TRBs written across the whole group   */
    ULONG TdTrbs;           /* TRBs written for the TD in progress   */
    ULONG LengthSum;        /* bytes described so far, this TD only  */
    ULONG PacketLength;     /* this TD's total                       */
    ULONG MaxPacketSize;
    ULONG DirectionIn;
    ULONG IsochControl;     /* TBC/TLBPC/Frame ID or SIA for TRB 0   */
} XHCI_ISO_BUILD_STATE;

/*
 * One TRB of the TD in progress. The first is an Isoch TRB carrying the
 * scheduling fields; every later one is a plain Normal TRB, which "depends on
 * the direction defined by the Endpoint Context that it is associated with"
 * (p.191) and has no scheduling fields of its own - an Isoch TD is "an Isoch TRB
 * chained to zero or more Normal TRBs" (4.11.2.3 p.195), never two Isoch TRBs.
 */
static ULONG xhciXferEmitIsoData(XHCI_ISO_BUILD_STATE *state,
                                 ULONG physicalAddress,
                                 ULONG length)
{
    XHCI_TRB *trb;
    ULONG control;

    if (state->Count >= state->Capacity) {
        return XHCI_XFER_TOO_MANY_TRBS;
    }

    trb = &state->Out[state->Count];
    XhciTrbClear(trb);
    trb->Param0 = physicalAddress;
    trb->Param1 = 0;                    /* no 64-bit DMA, ever (AGENTS.md) */

    state->LengthSum += length;
    /*
     * TD Size, not TBC: this driver leaves Extended TBC disabled, and with
     * ETE = 0 the DW2 `21:17` field is the TD Size of 4.11.2.4 exactly as it is
     * on every other transfer TRB (Table 6-33). The burst count lives in DW3
     * `8:7` and is written into `IsochControl` by the caller.
     *
     * The arithmetic is per **TD**, so `PacketLength` is this packet's total and
     * not the request's: TD Size counts the packets that remain to be
     * transferred for *a TD*.
     */
    trb->Status = (length & XHCI_TRB_LENGTH_MASK) |
                  XHCI_TRB_TD_SIZE(xhciXferTdSize(state->PacketLength,
                                                  state->LengthSum,
                                                  state->MaxPacketSize)) |
                  XHCI_TRB_INTERRUPTER(0);

    if (state->TdTrbs == 0) {
        control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_ISOCH) | state->IsochControl;
    } else {
        control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NORMAL);
    }
    /*
     * ISP on IN TRBs, for the reason the other two builders set it: a device
     * that answers with less than was asked for must generate an event carrying
     * the residual rather than ending the packet silently. On an isoch IN it is
     * load-bearing in a way it is not elsewhere - a short packet is the *normal*
     * result on an audio input, and without ISP the per-packet length written
     * back into usbport's block would be the full request every time.
     */
    if (state->DirectionIn) {
        control |= XHCI_TRB_ISP;
    }
    control |= XHCI_TRB_CH;             /* provisional: cleared on the last */
    trb->Control = control;

    state->Count++;
    state->TdTrbs++;
    return XHCI_XFER_OK;
}

/* One physical fragment, split at every 64 KB boundary - the same rule and the
 * same reason as the control and normal builders' (spec 6.4.1 note). usbport's
 * isoch fragments are page-bounded on both shipping builds, so this never splits
 * on the measured path; page granularity is a fact about two binaries rather
 * than a term of the ABI. */
static ULONG xhciXferEmitIsoFragment(XHCI_ISO_BUILD_STATE *state,
                                     ULONG physicalAddress,
                                     ULONG length)
{
    ULONG chunk;
    ULONG toBoundary;
    ULONG status;

    if (length == 0 || (length - 1) > (0xFFFFFFFFUL - physicalAddress)) {
        return XHCI_XFER_ISO_MALFORMED;
    }

    while (length > 0) {
        toBoundary = ((physicalAddress | 0xFFFFUL) + 1UL) - physicalAddress;
        chunk = (length < toBoundary) ? length : toBoundary;

        status = xhciXferEmitIsoData(state, physicalAddress, chunk);
        if (status != XHCI_XFER_OK) {
            return status;
        }
        physicalAddress += chunk;
        length -= chunk;
    }
    return XHCI_XFER_OK;
}

/*
 * TBC and TLBPC, spec 4.11.2.3 p.197, from the Transfer Descriptor Packet Count
 * of 4.14.1 p.234:
 *
 *   TDPC  = ROUNDUP ( TD Transfer Size / Max Packet Size )
 *   TBC   = ROUNDUP ( TDPC / ( Max Burst Size + 1 ) ) - 1
 *   TLBPC = residue == 0 ? Max Burst Size : residue - 1
 *
 * **A zero-length packet has a TDPC of 1, not 0**, which is what "note that a
 * partial or a zero-length packet increments this count by 1" means and is not
 * an edge case worth skipping: the xHC "shall transmit a zero-length DP to the
 * USB bus regardless bus speed, consuming the Isoch TD for the Service Interval"
 * (4.14.2.1 p.239), so a silent packet in an audio stream is a real TD. At TDPC
 * 0 the TBC expression is `ROUNDUP(0) - 1`, which underflows to 0xFFFFFFFF and
 * masks into the field as 3.
 *
 * Returns 0 if either result does not fit its field, which cannot happen while a
 * packet is bounded by Max ESIT Payload but is checked rather than assumed - the
 * two bounds come from different places and only one of them is enforced here.
 */
static ULONG xhciXferIsoBurstFields(ULONG packetLength,
                                    ULONG maxPacketSize,
                                    ULONG maxBurstSize,
                                    ULONG *tbc,
                                    ULONG *tlbpc)
{
    ULONG tdpc;
    ULONG burst;
    ULONG residue;

    burst = maxBurstSize + 1;
    tdpc = (packetLength + maxPacketSize - 1) / maxPacketSize;
    if (tdpc == 0) {
        tdpc = 1;
    }

    *tbc = ((tdpc + burst - 1) / burst) - 1;
    residue = tdpc % burst;
    *tlbpc = (residue == 0) ? maxBurstSize : (residue - 1);

    return (*tbc <= XHCI_TRB_TBC_MAX && *tlbpc <= XHCI_TRB_TLBPC_MAX) ? 1 : 0;
}

/* One packet as one Isoch TD. `tdLength` receives the TRB count. */
static ULONG xhciXferBuildIsoPacket(XHCI_ISO_BUILD_STATE *state,
                                    const USBPORT_ISO_PACKET *packet,
                                    const XHCI_ISO_REQUEST *request,
                                    ULONG useFrameId,
                                    ULONG *tdLength)
{
    XHCI_TRB *last;
    ULONG tbc;
    ULONG tlbpc;
    ULONG frameId;
    ULONG status;

    /*
     * "1 or 2; the builder has no path that writes anything else"
     * (docs/usb-xhci-info/usbport-miniport-abi.md). Checked rather than trusted, and the
     * refusal is what turns a block this driver has misunderstood into a failed
     * transfer instead of a walk off the end of usbport's allocation.
     */
    if (packet->FragmentCount == 0 || packet->FragmentCount > 2) {
        return XHCI_XFER_ISO_MALFORMED;
    }
    /* usbport stores whatever the HAL returned and does not mask the high
     * DWORD - the same rule the scatter/gather walk carries. */
    if (packet->Fragment0AddressHi != 0 ||
        (packet->FragmentCount == 2 && packet->Fragment1AddressHi != 0)) {
        return XHCI_XFER_ISO_MALFORMED;
    }
    /*
     * The fragments must describe exactly this packet. A shortfall would move
     * fewer bytes than the client asked for while reporting the packet's own
     * length back; an excess would read memory past what usbport mapped.
     */
    if (packet->FragmentCount == 1) {
        if (packet->Fragment0Length != packet->Length) {
            return XHCI_XFER_ISO_MALFORMED;
        }
    } else {
        if (packet->Fragment0Length == 0 || packet->Fragment1Length == 0) {
            return XHCI_XFER_ISO_MALFORMED;
        }
        if (packet->Fragment0Length > packet->Length ||
            packet->Fragment1Length !=
                packet->Length - packet->Fragment0Length) {
            return XHCI_XFER_ISO_MALFORMED;
        }
    }
    /*
     * "Software shall not define a TD Transfer Size for a TD of an Isoch
     * endpoint that exceeds the Max ESIT Payload" (4.14.2.1 p.238). Refused
     * here, which is the only place it can be: the xHC's own answer is to
     * truncate the transfer and raise a Bandwidth Overrun - to lose part of an
     * audio frame and say so afterwards.
     */
    if (packet->Length > request->MaxEsitPayload) {
        return XHCI_XFER_ISO_MALFORMED;
    }

    if (!xhciXferIsoBurstFields(packet->Length, request->MaxPacketSize,
                                request->MaxBurstSize, &tbc, &tlbpc)) {
        return XHCI_XFER_ISO_MALFORMED;
    }

    state->TdTrbs = 0;
    state->LengthSum = 0;
    state->PacketLength = packet->Length;
    state->IsochControl = XHCI_TRB_TBC(tbc) | XHCI_TRB_TLBPC(tlbpc);
    if (useFrameId) {
        /* The caller has already established that every packet of this request
         * converts, so this cannot fail - and it is called rather than having
         * its result passed in, so there is one conversion in the file and no
         * second copy to disagree with it. */
        (VOID)XhciXferFrameIdUsable(&request->Frames, packet->FrameNumber,
                                    &frameId);
        state->IsochControl |= XHCI_TRB_FRAME_ID(frameId);
    } else {
        state->IsochControl |= XHCI_TRB_SIA;
    }

    /*
     * A zero-length packet is one zero-length Isoch TRB, not zero TRBs - the
     * same rule the normal builder follows and for a stronger reason: the xHC
     * consumes one TD per interval, so a TD that does not exist is not a silent
     * packet, it is the ring running dry and a Ring Underrun.
     */
    if (packet->Length == 0) {
        status = xhciXferEmitIsoData(state, packet->Fragment0AddressLo, 0);
    } else {
        status = xhciXferEmitIsoFragment(state, packet->Fragment0AddressLo,
                                         packet->Fragment0Length);
        if (status == XHCI_XFER_OK && packet->FragmentCount == 2) {
            status = xhciXferEmitIsoFragment(state, packet->Fragment1AddressLo,
                                             packet->Fragment1Length);
        }
    }
    if (status != XHCI_XFER_OK) {
        return status;
    }

    last = &state->Out[state->Count - 1];
    /* "The Chain bit is always '0' in the last TRB of an Isoch TD" (Table 6-34)
     * and "the TD Size in the last Transfer TRB of a TD shall be cleared to '0'"
     * (p.198). Every TRB was emitted chained, so exactly one of each is
     * cleared. */
    last->Control &= ~XHCI_TRB_CH;
    last->Status &= ~XHCI_TRB_TD_SIZE(XHCI_TRB_TD_SIZE_MAX);
    /*
     * **IOC on every packet's last TRB**, which is the one place this builder
     * deliberately costs more than the others.
     *
     * A control transfer sets IOC once because the spec says so (p.430) and a
     * Normal TD sets it once because one TD is one transfer. Here one *request*
     * is many TDs and usbport wants a length and a status **per packet** - so
     * every TD has to produce an event, and the cost is one interrupt per
     * interval: 1,000 a second on a Full-Speed audio stream, 8,000 on a
     * High-Speed one. The interrupter's own moderation (IMOD, left at its 1 ms
     * default) is what absorbs that.
     *
     * BEI suppresses the interrupt while keeping the event, which is exactly
     * what this wants and is **not available**: `docs/usb-xhci-info/xhci-programming.md`
     * records Linux setting `XHCI_AVOID_BEI` on every Intel controller, and
     * every machine in this project's fleet is Intel. It is the one optimisation
     * here that could silently stop a stream.
     */
    last->Control |= XHCI_TRB_IOC;

    *tdLength = state->TdTrbs;
    return XHCI_XFER_OK;
}

/*
 * **Do usbport's per-packet stamps describe the schedule this driver
 * programmed?**
 *
 * usbport stamps packet `i` with `StartFrame + (i >> 3)` on High Speed and
 * `StartFrame + i` otherwise (docs/usb-xhci-info/usbport-miniport-abi.md section 4) - a fixed
 * indexing of the URB, one packet per microframe or one per frame. The Endpoint
 * Context says the same thing from the controller's side: `PacketsPerFrame` TDs
 * consumed per frame. When the two agree, packet `i`'s stamp is exactly
 * `i / PacketsPerFrame` frames past the first, and a Frame ID taken from it names
 * the frame the xHC will actually reach that TD in.
 *
 * **When they disagree, a Frame ID is a wrong answer rather than a missing one**,
 * which is why this is a gate and not a warning. The reachable disagreement is
 * Phase 5 task 7's: this driver reports every connected root port as High Speed,
 * so a Full-Speed audio device attached directly to one arrives with usbport
 * having stamped eight packets per frame while the Endpoint Context - built from
 * the device's *true* speed - consumes one per frame. Eight consecutive TDs would
 * carry the same Frame ID, and only the first would name the frame it is executed
 * in; the other seven name a frame that has already passed by the time the
 * endpoint reaches them, which is a Missed Service Error each and a stream that
 * never resynchronizes. SIA has no such failure - it is "schedule this at the
 * next opportunity", which is true whatever the cadence.
 *
 * The comparison is over the whole request, because one packet cannot show a
 * cadence, and it is done in the driver's published 32-bit frame domain where
 * subtraction wraps correctly.
 */
static ULONG xhciXferIsoCadenceAgrees(const USBPORT_ISO_TRANSFER *iso,
                                      ULONG packets,
                                      ULONG packetsPerFrame)
{
    ULONG first;
    ULONG i;

    if (packetsPerFrame == 0) {
        return 0;
    }
    first = iso->Packet[0].FrameNumber;
    for (i = 1; i < packets; i++) {
        if (iso->Packet[i].FrameNumber - first != i / packetsPerFrame) {
            return 0;
        }
    }
    return 1;
}

ULONG XhciXferBuildIso(const XHCI_ISO_REQUEST *request,
                       ULONG transferFlagsIn,
                       XHCI_TRB *out,
                       ULONG capacity,
                       PXHCI_ISO_LAYOUT layout)
{
    XHCI_ISO_BUILD_STATE state;
    const USBPORT_ISO_TRANSFER *iso;
    ULONG packets;
    ULONG useFrameId;
    ULONG frameId;
    ULONG status;
    ULONG i;

    if (request == NULL || out == NULL || layout == NULL || capacity == 0) {
        return XHCI_XFER_BAD_PARAM;
    }
    layout->TrbCount = 0;
    layout->TdCount = 0;
    layout->FrameIdsUsed = 0;
    layout->CadenceMismatch = 0;

    iso = request->Iso;
    if (iso == NULL) {
        return XHCI_XFER_ISO_MALFORMED;
    }
    /*
     * The signature is the one field that says the pointer usbport handed over
     * is the structure this driver believes it is, and every offset read below
     * rests on it. usbport writes it unconditionally on both shipping builds.
     */
    if (iso->Signature != USBPORT_ISO_SIGNATURE) {
        return XHCI_XFER_ISO_MALFORMED;
    }
    packets = iso->NumberOfPackets;
    if (packets == 0) {
        return XHCI_XFER_ISO_MALFORMED;
    }
    if (request->MaxPacketSize == 0 || request->MaxPacketSize > 0x7FFUL) {
        return XHCI_XFER_BAD_PARAM;
    }
    if (request->MaxEsitPayload == 0) {
        return XHCI_XFER_BAD_PARAM;
    }
    /*
     * **Bounded before the first packet is read**, and that position is the
     * point of it: the loops below index `iso->Packet[i]` in memory usbport owns
     * and sized, so a count this driver has not agreed to is a read past the end
     * of somebody else's allocation rather than a slow loop. The per-TRB
     * capacity check inside the emitter cannot stand in for it - by the time
     * that fires, packets have already been read.
     */
    if (packets > XHCI_XFER_MAX_ISO_PACKETS) {
        return XHCI_XFER_ISO_TOO_LARGE;
    }
    /*
     * usbport's direction against the endpoint's, checked and not chosen
     * between - the third statement of the rule the control and normal builders
     * already carry. A disagreement means usbport is submitting an IN request on
     * a pipe this driver configured as an isoch OUT, and moving the bytes either
     * way would be wrong.
     */
    if ((transferFlagsIn ? 1UL : 0UL) != (request->DirectionIn ? 1UL : 0UL)) {
        return XHCI_XFER_DIRECTION_CONFLICT;
    }

    /*
     * **The Frame ID decision is taken once, for the whole request.**
     *
     * Mixing SIA and explicit Frame IDs inside one group is not a compromise
     * between them: "To induce a gap in the data stream of a Running Isoch
     * endpoint, software simply specifies a gap in the Frame IDs assigned to the
     * TDs of the data stream, and the xHC will pause the data stream until the
     * Frame ID matches" (4.11.2.5 p.199). A group where some TDs carry an ID and
     * some do not is a request for a pause nobody asked for.
     *
     * So every packet is asked and one refusal drops the whole request to SIA.
     * A refusal is an ordinary outcome rather than a fault: a late submission
     * whose first packet is already in the past is one interval late when
     * scheduled ASAP, where the same request given expired Frame IDs is a Missed
     * Service Error per TD until the pipe resynchronizes (p.186).
     */
    /*
     * Measured whether or not Frame IDs were going to be used, because it is
     * what says the Endpoint Context's Interval and usbport's own packet
     * cadence describe the same schedule - which is the precondition for a
     * Frame ID taken from those packets to name the frame the TD executes in.
     *
     * **What a mismatch means depends on the Interval**, and the caller is what
     * knows that: usbport hands over no `bInterval` and forces an isoch `Period`
     * to 1, so an endpoint still carrying usbport's own cadence disagreeing says
     * that cadence is wrong, while one carrying the device's - task 9-A.2, from
     * the configuration descriptor snooped on EP0 - disagrees by design. The two
     * readings are counted apart in src/xhci_slot.c; this function reports the
     * observation and judges neither.
     */
    layout->CadenceMismatch =
        xhciXferIsoCadenceAgrees(iso, packets, request->PacketsPerFrame)
            ? 0UL : 1UL;

    useFrameId = (request->Frames.Allowed && !layout->CadenceMismatch)
                     ? 1UL : 0UL;
    for (i = 0; useFrameId && i < packets; i++) {
        if (!XhciXferFrameIdUsable(&request->Frames,
                                   iso->Packet[i].FrameNumber, &frameId)) {
            useFrameId = 0;
        }
    }

    state.Out = out;
    state.Capacity = capacity;
    state.Count = 0;
    state.TdTrbs = 0;
    state.LengthSum = 0;
    state.PacketLength = 0;
    state.MaxPacketSize = request->MaxPacketSize;
    state.DirectionIn = request->DirectionIn ? 1UL : 0UL;
    state.IsochControl = 0;

    for (i = 0; i < packets; i++) {
        status = xhciXferBuildIsoPacket(&state, &iso->Packet[i], request,
                                        useFrameId, &layout->TdLengths[i]);
        if (status != XHCI_XFER_OK) {
            /*
             * **Running out of scratch is a size refusal, not a malformed
             * request**, and the two have different counters because they mean
             * opposite things on a target: `IsoRefusalsMalformed` says this
             * driver and usbport disagree about the block layout, and
             * `IsoRefusalsTooLarge` says the pooled ring is too small for what a
             * real audio device asks for. The packet-count bound above catches
             * the common shape, but a request of *fewer* packets each split
             * across two fragments needs two TRBs apiece and fills the scratch
             * first - and the scratch is exactly the ring's capacity, so it is
             * the same "can never be placed" refusal arriving by the other door.
             */
            return (status == XHCI_XFER_TOO_MANY_TRBS) ? XHCI_XFER_ISO_TOO_LARGE
                                                       : status;
        }
    }

    layout->TrbCount = state.Count;
    layout->TdCount = packets;
    layout->FrameIdsUsed = useFrameId;
    return XHCI_XFER_OK;
}

ULONG XhciXferSubmitIso(PXHCI_TRANSFER_QUEUE queue,
                        PXHCI_RING ring,
                        const XHCI_ISO_REQUEST *request,
                        ULONG transferFlagsIn,
                        PXHCI_TRANSFER transfer,
                        PVOID transferParameters,
                        XHCI_TRB *scratch,
                        ULONG scratchCount,
                        PXHCI_ISO_LAYOUT layout)
{
    XHCI_TD_GROUP_PLACEMENT placement;
    ULONG requested;
    ULONG status;
    ULONG i;

    if (queue == NULL || ring == NULL || request == NULL ||
        transfer == NULL || scratch == NULL || layout == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }

    status = XhciXferBuildIso(request, transferFlagsIn, scratch, scratchCount,
                              layout);
    if (status != XHCI_XFER_OK) {
        return status;
    }
    /*
     * A request that cannot fit an **empty** ring can never be placed, so it is
     * separated from ring-full here rather than answered with a retry usbport
     * would repeat for ever. `XhciRingCapacity` is the ring's own figure: the
     * constant in the header bounds the packet loop, this bounds the placement,
     * and a pool ring resized without the constant following it would otherwise
     * turn into a silent refusal of every large request.
     */
    if (layout->TrbCount > XhciRingCapacity(ring)) {
        return XHCI_XFER_ISO_TOO_LARGE;
    }

    status = XhciRingEnqueueTdGroup(ring, scratch, layout->TrbCount,
                                    layout->TdLengths, layout->TdCount,
                                    &placement);
    if (status == XHCI_RING_FULL) {
        return XHCI_XFER_BUSY;
    }
    if (status != XHCI_RING_OK) {
        return XHCI_XFER_BAD_PARAM;
    }

    requested = 0;
    for (i = 0; i < layout->TdCount; i++) {
        requested += request->Iso->Packet[i].Length;
    }

    transfer->Signature = XHCI_TRANSFER_SIGNATURE;
    transfer->Next = NULL;
    transfer->TransferParameters = transferParameters;
    transfer->Token = queue->NextToken;
    queue->NextToken++;
    if (queue->NextToken == 0) {
        queue->NextToken = 1;
    }

    transfer->FirstIndex = placement.FirstIndex;
    transfer->LastIndex = placement.LastIndex;
    transfer->TrbCount = placement.TrbCount;
    xhciXferForgetMidTdTails(queue, ring, placement.FirstIndex,
                             placement.TrbCount);
    /*
     * Every TRB of this group carries data, exactly as a Normal TD's does - but
     * the residual arithmetic is per *packet* here and never uses this range,
     * because a sum taken across the whole group would fold eight audio frames
     * into one length. It is filled because a transfer record with an unset data
     * range is one every generic path has to special-case.
     */
    transfer->DataFirstIndex = placement.FirstIndex;
    transfer->DataTrbCount = placement.TrbCount;

    transfer->RequestedLength = requested;
    transfer->BytesTransferred = 0;
    transfer->Flags = XHCI_XFER_FLAG_ISOCH;
    transfer->UsbdStatus = XHCI_USBD_STATUS_SUCCESS;
    transfer->TopoReply = XHCI_TOPO_REPLY_NONE;
    transfer->DescAction = XHCI_DESC_ACT_NONE;
    transfer->ShortTrbPA = 0;
    /*
     * Cast away const: the block is usbport's, the two output fields in each
     * entry are the miniport's to write, and this is the pointer they are
     * written through. The request carries it const because *building* may not
     * touch it.
     */
    transfer->IsoParams = (PVOID)request->Iso;
    transfer->IsoPacketCount = layout->TdCount;
    transfer->IsoPacketsAnswered = 0;

    if (queue->Tail != NULL) {
        queue->Tail->Next = transfer;
    } else {
        queue->Head = transfer;
    }
    queue->Tail = transfer;
    queue->Count++;
    queue->Submitted++;
    queue->IsoPackets += layout->TdCount;
    return XHCI_XFER_OK;
}

/*
 * Which packet of `transfer` owns ring index `index`, and where that packet's TD
 * ends.
 *
 * The walk is over TD boundaries rather than over a stored table, for the reason
 * every other position question in this engine is answered off the ring: the
 * Chain flags are already there, in the words the hardware itself read, and a
 * parallel copy of the packet-to-TRB mapping is one more thing that can fall out
 * of step with a rebuilt ring.
 *
 * Bounded by the transfer's own packet count, which is also its TD count.
 * Returns 1 and fills both outputs, or 0 - and a 0 means the ring's chain walk
 * and this record disagree, since the caller has already placed the index inside
 * this transfer's range.
 */
static ULONG xhciXferIsoPacketAt(const XHCI_RING *ring,
                                 const XHCI_TRANSFER *transfer,
                                 ULONG index,
                                 ULONG *packetIndex,
                                 ULONG *packetHead)
{
    ULONG eventHead;
    ULONG eventTail;
    ULONG cursor;
    ULONG head;
    ULONG tail;
    ULONG i;

    /*
     * The TD the event landed in, asked of the ring **once** and from the
     * event's own index. Matching the TDs by their heads afterwards is what
     * makes the walk a lookup rather than a second containment test: two
     * different chain walks that had to agree would be two statements of one
     * fact.
     */
    if (XhciRingTdBounds(ring, index, &eventHead, &eventTail) !=
            XHCI_RING_OK) {
        return 0;
    }

    cursor = transfer->FirstIndex;
    for (i = 0; i < transfer->IsoPacketCount; i++) {
        if (XhciRingTdBounds(ring, cursor, &head, &tail) != XHCI_RING_OK) {
            return 0;
        }
        if (head == eventHead) {
            *packetIndex = i;
            *packetHead = eventHead;
            return 1;
        }
        cursor = XhciRingNextIndex(ring, tail);
    }
    return 0;
}

VOID XhciXferIsoFinalise(PXHCI_TRANSFER transfer)
{
    PUSBPORT_ISO_TRANSFER iso;
    ULONG i;

    if (transfer == NULL || (transfer->Flags & XHCI_XFER_FLAG_ISOCH) == 0) {
        return;
    }
    iso = (PUSBPORT_ISO_TRANSFER)transfer->IsoParams;
    if (iso == NULL) {
        return;
    }
    for (i = transfer->IsoPacketsAnswered; i < transfer->IsoPacketCount; i++) {
        iso->Packet[i].LengthTransferred = 0;
        iso->Packet[i].Status = transfer->UsbdStatus;
    }
    transfer->IsoPacketsAnswered = transfer->IsoPacketCount;
}

ULONG XhciXferIsoEvent(PXHCI_TRANSFER_QUEUE queue,
                       PXHCI_RING ring,
                       ULONG slotId,
                       ULONG dci,
                       ULONG eventTrbPA,
                       ULONG eventDw2,
                       ULONG eventDw3,
                       PXHCI_XFER_EVENT_RESULT result)
{
    XHCI_XFER_CODE code;
    PUSBPORT_ISO_TRANSFER iso;
    PXHCI_TRANSFER walk;
    PXHCI_TRANSFER owner;
    ULONG completionCode;
    ULONG residual;
    ULONG reportedIndex;
    ULONG packetIndex;
    ULONG packetHead;
    ULONG ahead;
    ULONG packetLength;
    ULONG bytes;
    ULONG i;

    if (result == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }
    result->Action = XHCI_XFER_ACTION_NONE;
    result->Completed = NULL;
    result->CompletedCount = 0;
    result->NeedsRecovery = 0;
    result->RefusedRetire = 0;
    result->Fatal = 0;

    if (queue == NULL || ring == NULL) {
        return XHCI_XFER_BAD_PARAM;
    }
    if (XHCI_EVENT_IS_EVENT_DATA(eventDw3)) {
        queue->EventDataEvents++;
        return XHCI_XFER_OK;
    }
    if (XHCI_TRB_GET_SLOT_ID(eventDw3) != slotId ||
        XHCI_TRB_GET_EP_ID(eventDw3) != dci) {
        queue->ForeignEvents++;
        return XHCI_XFER_OK;
    }

    completionCode = XHCI_TRB_GET_COMPLETION(eventDw2);
    residual = XHCI_TRB_GET_RESIDUAL(eventDw2);

    if (XhciXferIsoCodeInfo(completionCode, &code) != XHCI_XFER_OK) {
        queue->BadCodes++;
        return XHCI_XFER_OK;
    }
    if (code.Fatal) {
        result->Fatal = 1;
    }
    if (completionCode == XHCI_CC_MISSED_SERVICE) {
        queue->IsoMissedService++;
    }
    /*
     * **TRB Error is the one code that leaves an isoch endpoint needing
     * software, and it is asked here rather than by the ring classifier** -
     * which this path does not call, because the isochronous retire is
     * positional across a whole group rather than per TD.
     *
     * The rule itself is already stated once, in `xhciEventNeedsRecovery`, and
     * this is the second site that has to obey it: what p.177 exempts an isoch
     * endpoint from is *halting* ("it never halts because there is no handshake
     * to report a halt condition"), and 4.8.3 p.149 puts a TRB Error somewhere
     * else entirely - it "should cause a Running Endpoint to transition to the
     * Error state. A Set TR Dequeue Pointer Command shall be used to transition
     * the endpoint to the Stopped state", with no qualification by endpoint
     * type. Error and Halted are separately encoded states, so an endpoint that
     * cannot halt can still sit in Error, and an endpoint sitting in Error runs
     * nothing: the stream stops while this driver keeps ringing a doorbell that
     * the state field makes meaningless. It is the one isochronous *error*
     * software must act on, and the flag routes it to the same policy function
     * the bulk path uses, which already reads the completion code to pick Set TR
     * Dequeue over Reset Endpoint.
     */
    if (completionCode == XHCI_CC_TRB_ERROR) {
        result->NeedsRecovery = 1;
    }

    if (XhciRingIndexFromPA(ring, eventTrbPA, &reportedIndex) != XHCI_RING_OK) {
        queue->ForeignEvents++;
        return XHCI_XFER_OK;
    }

    owner = NULL;
    ahead = 0;
    for (walk = queue->Head; walk != NULL; walk = walk->Next) {
        if (xhciXferRangeContains(ring, walk->FirstIndex, walk->TrbCount,
                                  reportedIndex, NULL)) {
            owner = walk;
            break;
        }
        ahead++;
    }
    if (owner == NULL) {
        queue->UnmatchedEvents++;
        return XHCI_XFER_OK;
    }
    if ((owner->Flags & XHCI_XFER_FLAG_ISOCH) == 0 ||
        owner->IsoParams == NULL || owner->IsoPacketCount == 0) {
        /* A non-isochronous transfer on a ring this driver only ever creates as
         * XHCI_RING_KIND_ISOCH. Not reachable by construction, counted rather
         * than asserted away, because the alternative is writing packet results
         * through a pointer that means something else. */
        queue->ForeignEvents++;
        return XHCI_XFER_OK;
    }
    if (!xhciXferIsoPacketAt(ring, owner, reportedIndex, &packetIndex,
                             &packetHead)) {
        /*
         * **A record/ring divergence, not a stray event.** The caller has already
         * placed this index inside this transfer's range, so the walk failing
         * means the ring's Chain flags and this record's packet count disagree -
         * and the event that found it may be the group's only tail. Counting it
         * as unmatched and returning strands the request until something else
         * cancels it, which is what the ordinary transfer path refuses to do for
         * the same condition: it asks for a Stop Endpoint with a drain
         * continuation, the one sequence that makes both a completion and a
         * repositioning legal on a Running endpoint. Answered the same way here.
         */
        queue->UnmatchedEvents++;
        result->RefusedRetire = 1;
        result->NeedsRecovery = 1;
        return XHCI_XFER_OK;
    }

    iso = (PUSBPORT_ISO_TRANSFER)owner->IsoParams;

    /*
     * **The bytes are this packet's, and the measurement stops at the TRB the
     * event named.**
     *
     * `sum - residual` across the transfer's whole data range - which is what
     * the bulk path computes - would fold every earlier packet's length into
     * this one's answer, so the sum is taken over this TD alone: from the
     * packet's head TRB through the one the event reported.
     *
     * **Not `packet->Length - residual`.** That form is a single-TRB-TD rule,
     * which batch 6-A already established for the other two builders and which
     * this function repeated for a shape that is not always single-TRB: a packet
     * usbport splits across two fragments is two TRBs, and Table 6-39 p.441 says
     * for that case the residual "only reflects the number of bytes transferred
     * for the buffer associated with the Transfer TRB pointed to by the Transfer
     * Event, **not the total bytes transferred for the TD**". A short read of
     * 100 bytes on the first of a 700+324 split was therefore reported as
     * 1024-100 = 924 rather than 700-100 = 600 - over-reporting by an entire
     * fragment, into the length usbport hands the audio client.
     *
     * An impossible residual is refused exactly as it is elsewhere rather than
     * reported as a plausible number, and a ring that cannot sum the range is
     * the same refusal: neither may fall through to a number.
     *
     * **The bound, stated rather than closed** (batch 9-A review round 2). If the
     * first of the two events p.175 promises is *dropped* - an Event Ring Full
     * condition does that - and only the tail arrives, this sum runs to the tail
     * and the residual it carries is the original one, so a short packet that
     * ended on an early TRB is reported as though it ended on the last: the
     * over-reporting direction. Nothing available here can tell that case from
     * the ordinary one, because the two events are identical in every field the
     * xHC provides and a short read on a TD's final TRB is the common, correct
     * shape. Closing it would need Event Data TRBs, which this driver places
     * none of - the same boundary `docs/contributing/implementation-invariants.md` records for
     * the mid-TD tail. It is bounded by one TD's length and requires a dropped
     * event, and it is written down here rather than mitigated by a rule that
     * would be wrong more often than right.
     */
    bytes = 0;
    packetLength = 0;
    if (code.ResidualIsBytes) {
        if (XhciRingSumTrbLengths(ring, packetHead, reportedIndex,
                                  &packetLength) != XHCI_RING_OK ||
            residual > packetLength) {
            queue->ResidualRejects++;
            code.Class = XHCI_XFER_CC_ERROR;
            code.UsbdStatus = XHCI_USBD_STATUS_INTERNAL_HC_ERROR;
        } else {
            bytes = packetLength - residual;
        }
    }

    /*
     * **Packets are answered in ring order and never twice.**
     *
     * The xHC consumes one TD per interval in ring order, so events arrive in
     * packet order, and `IsoPacketsAnswered` is both the count and the position.
     * Advancing it *past* `packetIndex` is what accounts for packets the
     * controller skipped without reporting - which it may do, since it "may not
     * generate a Missed Service Error for each Isochronous deadline missed, e.g.
     * if the Event Ring is full" (4.11.2.3 p.196). Those intervening packets are
     * stamped here rather than left to `XhciXferIsoFinalise`, which can no
     * longer see them once the count has moved past: a skipped packet reported
     * as Success with no data is precisely the silent lie that function exists
     * to prevent.
     *
     * An event for a packet *behind* the count is a duplicate and changes
     * nothing - not even the byte total, which a second measurement of the same
     * packet would otherwise double.
     */
    if (packetIndex < owner->IsoPacketsAnswered) {
        /*
         * **A duplicate measurement, but not necessarily a duplicate event.**
         * The tail of a multi-TRB TD reports again with the original condition
         * code (p.175 for a short packet, p.201 for a skipped Isoch TD), so an
         * event naming the group's last TRB can arrive for a packet an earlier,
         * intermediate event has already answered. It changes nothing about the
         * packet - a second measurement of the same bytes would double the
         * total - but it is still the positional proof of ownership the retire
         * below waits for, so it falls through instead of returning.
         */
        queue->UnmatchedEvents++;
        if (reportedIndex != owner->LastIndex) {
            return XHCI_XFER_OK;
        }
    } else {
        for (i = owner->IsoPacketsAnswered; i < packetIndex; i++) {
            iso->Packet[i].LengthTransferred = 0;
            iso->Packet[i].Status = XHCI_USBD_STATUS_NOT_ACCESSED;
            queue->IsoPacketsAnswered++;
            queue->IsoPacketErrors++;
            /*
             * A packet the controller skipped is a packet that failed, and the
             * request-level status has to say so for the same reason the error
             * arm below sets it: `AbortTransfer` and every trace line read the
             * request's own status, not the per-packet block, and a request that
             * lost packets reporting Success is the report contradicting itself.
             */
            owner->UsbdStatus = XHCI_USBD_STATUS_NOT_ACCESSED;
            owner->Flags |= XHCI_XFER_FLAG_FAILED;
        }

        iso->Packet[packetIndex].LengthTransferred = bytes;
        iso->Packet[packetIndex].Status = code.UsbdStatus;
        owner->IsoPacketsAnswered = packetIndex + 1;
        owner->BytesTransferred += bytes;
        queue->IsoPacketsAnswered++;
        if (code.Class == XHCI_XFER_CC_SHORT) {
            queue->ShortPackets++;
        } else if (code.Class != XHCI_XFER_CC_SUCCESS) {
            queue->IsoPacketErrors++;
            /* The request's own status is the **most recent** non-success packet
             * status, not the worst: this and the skipped-packet stamp above
             * both assign, so a later packet's code overwrites an earlier one -
             * including a bare `NOT_ACCESSED` landing on top of something more
             * specific. It is left last-wins rather than ranked because there is
             * no ordering over USBD statuses to rank them by, and the reading is
             * only ever a summary - a trace line, `AbortTransfer`'s report.
             * usbport takes the per-packet values out of the block and does not
             * consult this, so nothing decides on it. `XHCI_XFER_FLAG_FAILED`
             * beside it is the part that is not lossy: it is set, never cleared,
             * so "this request lost packets" survives whatever the last code
             * was. */
            owner->UsbdStatus = code.UsbdStatus;
            owner->Flags |= XHCI_XFER_FLAG_FAILED;
        }
    }

    /*
     * **Is the request over? One way only: the event named the group's last
     * TRB.** The positional rule of 4.11.7 p.214 holds here exactly as it holds
     * on every other ring kind, and the completion code does not override it.
     *
     * An earlier version of this function had a second way out - every packet
     * answered, by an event naming some *earlier* TRB - on the reasoning that an
     * error or a Missed Service on a final packet split across two fragments
     * leaves the controller advanced to the next ESIT with nothing further
     * coming. **The second half of that is false**, and the repository had
     * already written down why: the xHC "shall not drop Events associated with
     * TRBs as it attempts to resynchronize an Isoch pipe, e.g. ... if IOC = `1`
     * in an Event Data or Normal TRB then it returns Missed Service Error"
     * (p.201), and this builder puts IOC on every TD's last TRB. The tail event
     * is coming; p.188 says so in as many words for this exact case - if the
     * event "does not point to last TRB of the Isoch TD ... software will have to
     * wait until the next IOC flag is encountered by the endpoint before it can
     * reclaim" the TD. Retiring there handed a mapped buffer back to usbport,
     * and re-let ring slots to the next submission, while the xHC still owned
     * the tail.
     *
     * **What happens if that tail really is lost** - an Event Ring Full
     * condition drops events outright (p.197) - is the same thing that happens
     * to any deferred TD on any ring in this driver: the next group's retire
     * jumps the dequeue pointer past it and the sweep below completes it. Behind
     * that stands usbport's own cancellation - its transfer timeout, or a
     * teardown - which reaches `AbortTransfer` and the quiescence chain.
     *
     * **Not** `StartFrame + NumberOfPackets + 1`: a draft of this comment named
     * that as the thing which gives up on the request, and it is not. That
     * threshold is an early *return* inside an abort pass that is already under
     * way - it makes usbport wait one frame longer before touching an isochronous
     * transfer it has already decided to cancel (docs/usb-xhci-info/usbport-miniport-abi.md) -
     * so it delays a cancellation rather than starting one. What it does mean for
     * this driver is unchanged and still load-bearing: a frame number that stops
     * advancing strands that abort, which is the dependency batch 6-0 put on task
     * 6-B.1.
     *
     * Counted, because a nonzero reading is how a controller that drops tails
     * announces itself.
     */
    if (reportedIndex != owner->LastIndex) {
        if (owner->IsoPacketsAnswered >= owner->IsoPacketCount) {
            queue->IsoGroupsAwaitingTail++;
        }
        return XHCI_XFER_OK;
    }

    if (XhciRingRetireIsoGroup(ring, owner->LastIndex) != XHCI_RING_OK) {
        /*
         * The ring refused: something moved the dequeue pointer since this group
         * was placed. Nothing is completed on it - the same rule the bulk path's
         * refused retire follows, and for the same reason. A transfer whose TRBs
         * may still be the controller's must not have its mapped buffer handed
         * back, and on an isoch endpoint there is no halt to make a
         * repositioning legal either.
         */
        queue->PlacementFailures++;
        result->RefusedRetire = 1;
        result->NeedsRecovery = 1;
        return XHCI_XFER_OK;
    }

    /*
     * Everything queued *ahead* of this group had its TRBs reclaimed by the same
     * store, so it is completed too - the rule
     * docs/contributing/implementation-invariants.md, "Completion Matching" states for every
     * ring. On an isoch endpoint those are requests whose TDs the controller
     * skipped without reporting; each one's unanswered packets are stamped at
     * delivery by `XhciXferIsoFinalise` from the status set here.
     */
    for (walk = queue->Head, i = 0; i < ahead && walk != NULL; i++) {
        walk->UsbdStatus = XHCI_USBD_STATUS_NOT_ACCESSED;
        walk->Flags |= XHCI_XFER_FLAG_FAILED;
        queue->SweptTransfers++;
        walk = walk->Next;
    }

    result->CompletedCount = ahead + 1;
    result->Completed = xhciXferDetach(queue, result->CompletedCount);
    result->Action = XHCI_XFER_ACTION_COMPLETE;
    queue->Completed += result->CompletedCount;
    if (owner->Flags & XHCI_XFER_FLAG_FAILED) {
        queue->Errors++;
    }
    return XHCI_XFER_OK;
}
