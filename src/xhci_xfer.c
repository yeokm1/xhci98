/*
 * xhci_xfer.c - the transfer completion-code table of xhci98.sys (xHCI 1.2
 * section 6.4.5, Table 6-90), lifted unchanged from the miniport's file of
 * the same name at 1.2.0.0 (roadmap-hcd.md task 26-A.2; branch 1.2.0.0,
 * src/xhci_xfer.c).
 *
 * The miniport's TD builder and transfer queue were in this file too and left
 * the tree with it (design record 13 section 5.1, the "Rewritten" row). The
 * event drain and the command engine this phase keeps classify completion
 * codes through XhciXferCodeInfo, so the table comes back first; the TD
 * builder over the HCD's own mapping is task 26-A.5's.
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

