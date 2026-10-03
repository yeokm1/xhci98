/*
 * xhci_strict.c - strict mode's command precondition table (xhci_strict.h).
 *
 * Read against the xHCI 1.2 pseudo-code; the citations are to the command's
 * section, and the quoted phrase is the branch the verdict comes from. The
 * table answers what a conforming controller would do, not what QEMU does.
 *
 * The release flavour compiles none of it (DBG is 0 there): strict mode is
 * an observer for the debug and qemu flavours only.
 *
 * C89, pure: IRQL any.
 */

#include "xhci.h"
#include "xhci_strict.h"

#if DBG || defined(XHCI_HOST_TEST)

ULONG XhciStrictSlotState(ULONG outputSlotState, ULONG enabled)
{
    if (!enabled) {
        return XHCI_STRICT_SLOT_DISABLED;
    }
    switch (outputSlotState) {
    case XHCI_SLOT_STATE_DISABLED:
        return XHCI_STRICT_SLOT_ENABLED;
    case XHCI_SLOT_STATE_DEFAULT:
        return XHCI_STRICT_SLOT_DEFAULT;
    case XHCI_SLOT_STATE_ADDRESSED:
        return XHCI_STRICT_SLOT_ADDRESSED;
    case XHCI_SLOT_STATE_CONFIGURED:
        return XHCI_STRICT_SLOT_CONFIGURED;
    default:
        return XHCI_STRICT_SLOT_RESERVED;
    }
}

ULONG XhciStrictNamesSlot(ULONG trbType)
{
    switch (trbType) {
    case XHCI_TRB_TYPE_DISABLE_SLOT:
    case XHCI_TRB_TYPE_ADDRESS_DEVICE:
    case XHCI_TRB_TYPE_CONFIGURE_EP:
    case XHCI_TRB_TYPE_EVALUATE_CONTEXT:
    case XHCI_TRB_TYPE_RESET_EP:
    case XHCI_TRB_TYPE_STOP_EP:
    case XHCI_TRB_TYPE_SET_TR_DEQUEUE:
    case XHCI_TRB_TYPE_RESET_DEVICE:
    case XHCI_STRICT_TYPE_NEGOTIATE_BANDWIDTH:
        return 1;
    default:
        return 0;
    }
}

ULONG XhciStrictNamesEndpoint(ULONG trbType)
{
    return trbType == XHCI_TRB_TYPE_RESET_EP ||
           trbType == XHCI_TRB_TYPE_STOP_EP ||
           trbType == XHCI_TRB_TYPE_SET_TR_DEQUEUE;
}

/* Default, Addressed or Configured: the slot test that Evaluate Context,
 * Reset Endpoint, Stop Endpoint and Set TR Dequeue Pointer share. */
static ULONG xhciStrictSlotLive(ULONG slotState)
{
    return slotState == XHCI_STRICT_SLOT_DEFAULT ||
           slotState == XHCI_STRICT_SLOT_ADDRESSED ||
           slotState == XHCI_STRICT_SLOT_CONFIGURED;
}

ULONG XhciStrictCheck(ULONG trbType, ULONG bit9, ULONG slotState,
                      ULONG epState)
{
    switch (trbType) {
    /* 4.6.2 and 4.6.3: neither names a slot; Enable Slot's only failure is
     * "No Slots Available", a resource answer. */
    case XHCI_TRB_TYPE_NOOP_COMMAND:
    case XHCI_TRB_TYPE_ENABLE_SLOT:
        return XHCI_STRICT_OK;

    /* 4.6.4: "If the Device Slot ... has been previously enabled by an
     * Enable Slot Command ... else ... Slot Not Enabled Error". */
    case XHCI_TRB_TYPE_DISABLE_SLOT:
        return slotState == XHCI_STRICT_SLOT_DISABLED ?
                   XHCI_STRICT_NOT_ENABLED : XHCI_STRICT_OK;

    /* 4.6.5: BSR = 1 "If the slot is in the Enabled state"; BSR = 0 "If
     * the slot is in the Enabled or Default state"; else Context State
     * Error; never enabled, Slot Not Enabled Error. */
    case XHCI_TRB_TYPE_ADDRESS_DEVICE:
        if (slotState == XHCI_STRICT_SLOT_DISABLED) {
            return XHCI_STRICT_NOT_ENABLED;
        }
        if (slotState == XHCI_STRICT_SLOT_ENABLED ||
            (!bit9 && slotState == XHCI_STRICT_SLOT_DEFAULT)) {
            return XHCI_STRICT_OK;
        }
        return XHCI_STRICT_SLOT_STATE;

    /*
     * 4.6.6: DC = 0 "If the Output Device Context Slot State is equal to
     * Addressed or Configured"; DC = 1 acts only under "If the Output
     * Device Context Slot State is equal to Configured", the command that
     * "transition[s] the Device Slot from the Configured to the Addressed
     * state". Else Context State Error; never enabled, Slot Not Enabled
     * Error ("If the Slot State is Disabled ... Slot Not Enabled Error").
     */
    case XHCI_TRB_TYPE_CONFIGURE_EP:
        if (slotState == XHCI_STRICT_SLOT_DISABLED) {
            return XHCI_STRICT_NOT_ENABLED;
        }
        if (slotState == XHCI_STRICT_SLOT_CONFIGURED ||
            (!bit9 && slotState == XHCI_STRICT_SLOT_ADDRESSED)) {
            return XHCI_STRICT_OK;
        }
        return XHCI_STRICT_SLOT_STATE;

    /* 4.6.7: "If the Output Slot State is equal to Default, Addressed or
     * Configured ... else ... Context State Error". */
    case XHCI_TRB_TYPE_EVALUATE_CONTEXT:
        if (slotState == XHCI_STRICT_SLOT_DISABLED) {
            return XHCI_STRICT_NOT_ENABLED;
        }
        return xhciStrictSlotLive(slotState) ? XHCI_STRICT_OK :
                                               XHCI_STRICT_SLOT_STATE;

    /* 4.6.8: slot Default, Addressed or Configured, then "If the Endpoint
     * State (EP State) field is set to Halted ... else ... Context State
     * Error". */
    case XHCI_TRB_TYPE_RESET_EP:
        if (slotState == XHCI_STRICT_SLOT_DISABLED) {
            return XHCI_STRICT_NOT_ENABLED;
        }
        if (!xhciStrictSlotLive(slotState)) {
            return XHCI_STRICT_SLOT_STATE;
        }
        return epState == XHCI_EP_STATE_HALTED ? XHCI_STRICT_OK :
                                                 XHCI_STRICT_EP_STATE;

    /* 4.6.9: "If the Endpoint State (EP State) field equals Running ...
     * else ... Context State Error" - Stopped, Halted and Error included.
     * The note on the race (an endpoint halting under the command) is why
     * the caller checks the state it read, not the state it assumed. */
    case XHCI_TRB_TYPE_STOP_EP:
        if (slotState == XHCI_STRICT_SLOT_DISABLED) {
            return XHCI_STRICT_NOT_ENABLED;
        }
        if (!xhciStrictSlotLive(slotState)) {
            return XHCI_STRICT_SLOT_STATE;
        }
        return epState == XHCI_EP_STATE_RUNNING ? XHCI_STRICT_OK :
                                                  XHCI_STRICT_EP_STATE;

    /* 4.6.10: "If the Endpoint State (EP State) field equals Stopped or
     * Error ... else ... Context State Error". */
    case XHCI_TRB_TYPE_SET_TR_DEQUEUE:
        if (slotState == XHCI_STRICT_SLOT_DISABLED) {
            return XHCI_STRICT_NOT_ENABLED;
        }
        if (!xhciStrictSlotLive(slotState)) {
            return XHCI_STRICT_SLOT_STATE;
        }
        return epState == XHCI_EP_STATE_STOPPED ||
                       epState == XHCI_EP_STATE_ERROR ?
                   XHCI_STRICT_OK : XHCI_STRICT_EP_STATE;

    /* 4.6.11: "If the Device Slot was in the Addressed or Configured state
     * ... else ... Context State Error". The pseudo-code has no Slot Not
     * Enabled branch, so a never-enabled slot is answered the same way. */
    case XHCI_TRB_TYPE_RESET_DEVICE:
        return slotState == XHCI_STRICT_SLOT_ADDRESSED ||
                       slotState == XHCI_STRICT_SLOT_CONFIGURED ?
                   XHCI_STRICT_OK : XHCI_STRICT_SLOT_STATE;

    /* 4.6.13: "If the Slot ID identifies a slot in the Addressed or
     * Configured state ... else ... Context State Error". */
    case XHCI_STRICT_TYPE_NEGOTIATE_BANDWIDTH:
        if (slotState == XHCI_STRICT_SLOT_DISABLED) {
            return XHCI_STRICT_NOT_ENABLED;
        }
        return slotState == XHCI_STRICT_SLOT_ADDRESSED ||
                       slotState == XHCI_STRICT_SLOT_CONFIGURED ?
                   XHCI_STRICT_OK : XHCI_STRICT_SLOT_STATE;

    /* 4.6.12, 4.6.14 to 4.6.18: no Slot State or EP State branch (VF, LTV,
     * port and protocol arguments only). */
    case XHCI_STRICT_TYPE_FORCE_EVENT:
    case XHCI_STRICT_TYPE_SET_LTV:
    case XHCI_STRICT_TYPE_GET_PORT_BANDWIDTH:
    case XHCI_STRICT_TYPE_FORCE_HEADER:
    case XHCI_STRICT_TYPE_GET_EXT_PROPERTY:
    case XHCI_STRICT_TYPE_SET_EXT_PROPERTY:
        return XHCI_STRICT_OK;

    default:
        return XHCI_STRICT_UNMODELLED;
    }
}

/*
 * 4.6.6: "xHC behavior is undefined if the Drop Context (D) flag is '0',
 * the Add Context (A) flag is '1', and the Output Endpoint Context is not
 * in the Disabled state", and "An endpoint shall be in the Stopped state or
 * if in the Running state shall be 'idle' ... if its Drop Context flag is
 * set". Idleness is not in the context, so only Halted and Error are
 * decidable; a Drop of a Disabled endpoint "shall be ignored".
 */
ULONG XhciStrictCheckConfigureEndpoint(ULONG add, ULONG drop, ULONG epState)
{
    if (add && !drop && epState != XHCI_EP_STATE_DISABLED) {
        return XHCI_STRICT_UNDEFINED;
    }
    if (drop && (epState == XHCI_EP_STATE_HALTED ||
                 epState == XHCI_EP_STATE_ERROR)) {
        return XHCI_STRICT_UNDEFINED;
    }
    return XHCI_STRICT_OK;
}

ULONG XhciStrictIsPreconditionCode(ULONG completionCode)
{
    return completionCode == XHCI_CC_SLOT_NOT_ENABLED ||
           completionCode == XHCI_CC_PARAMETER_ERROR ||
           completionCode == XHCI_CC_CONTEXT_STATE_ERROR;
}

#else /* release: strict mode is not in the image */

typedef int xhciStrictReleasePlaceholder;

#endif
