/*
 * hcd_strict.c - strict mode, the driver half (xhci_strict.h has the table).
 *
 * QEMU's xHC accepts commands a conforming controller refuses, so a slot or
 * endpoint state the driver got wrong passes every VM leg. Before each
 * command hcd_enum.c's hcdCommand submits - every slot command the driver
 * issues goes through it - the slot's state and the target endpoint's are
 * read from the Output Device Context and checked against the table. A
 * violation is counted and traced once per (command, state) pair, and the
 * command still goes out: strict mode observes, it never changes what the
 * driver does. After the completion, a code a precondition failure answers
 * with (Slot Not Enabled, Parameter, Context State Error) is counted and
 * traced on its own, with the state read before the issue - QEMU never
 * sends one; metal will.
 *
 * Both traces go two ways: the qemu flavour's live line (XHCI_DBG_VALUE) and
 * the log ring (XhciLogNote), which the debug flavour keeps on metal and
 * XHCISNAP reads off. Value layout in both: type << 24 | slot << 16 |
 * DCI << 8 | slot state << 4 | EP state, the slot state being the table's
 * XHCI_STRICT_SLOT_* (0 Disabled, 1 Enabled, 2 Default, 3 Addressed,
 * 4 Configured, 5 reserved; 6 a Drop and 7 an Add on the per-endpoint
 * Configure Endpoint lines) and the EP state the context's raw field.
 *
 * Debug and qemu flavours only: the release image carries none of it.
 *
 * IRQL: PASSIVE_LEVEL (the controller thread), the controller lock not held.
 */

#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_strict.h"
#include "xhci_dbg.h"

#if DBG

static ULONG hcdStrictEnabled(PHCD_CONTROLLER hc, ULONG slotId)
{
    if (slotId == 0 || slotId > XHCI_MAX_SLOTS) {
        return 0;
    }
    return (hc->StrictSlotEnabled[slotId / 32UL] >> (slotId % 32UL)) & 1UL;
}

static VOID hcdStrictMark(PHCD_CONTROLLER hc, ULONG slotId, ULONG enabled)
{
    if (slotId == 0 || slotId > XHCI_MAX_SLOTS) {
        return;
    }
    if (enabled) {
        hc->StrictSlotEnabled[slotId / 32UL] |= 1UL << (slotId % 32UL);
    } else {
        hc->StrictSlotEnabled[slotId / 32UL] &= ~(1UL << (slotId % 32UL));
    }
}

/* HCRST takes every slot without a Disable Slot (hcd_enum.c, the
 * invalidation and the stop). */
VOID HcdStrictForgetSlots(PHCD_CONTROLLER hc)
{
    ULONG i;

    for (i = 0; i < sizeof(hc->StrictSlotEnabled) / sizeof(ULONG); i++) {
        hc->StrictSlotEnabled[i] = 0;
    }
}

static ULONG hcdStrictPack(const HCD_STRICT_SNAP *snap)
{
    return ((snap->Type & 0xFFUL) << 24) | ((snap->SlotId & 0xFFUL) << 16) |
           ((snap->Dci & 0xFFUL) << 8) | ((snap->SlotState & 0xFUL) << 4) |
           (snap->EpState & 0xFUL);
}

/* Whether this (command key, slot state, EP state) is new: one trace each,
 * the counters take every occurrence. */
static ULONG hcdStrictFirst(PHCD_CONTROLLER hc, ULONG key, ULONG slotState,
                            ULONG epState)
{
    ULONG index;
    ULONG bit;

    index = (slotState & 7UL) * 8UL + (epState & 7UL);
    bit = 1UL << (index % 32UL);
    if (hc->StrictSeen[key & 63UL][index / 32UL] & bit) {
        return 0;
    }
    hc->StrictSeen[key & 63UL][index / 32UL] |= bit;
    return 1;
}

static VOID hcdStrictReport(PHCD_CONTROLLER hc, const HCD_STRICT_SNAP *snap,
                            ULONG verdict, ULONG value)
{
    if (verdict == XHCI_STRICT_UNDEFINED) {
        hc->StrictUndefined++;
    } else {
        hc->StrictViolations++;
    }
    if (!hcdStrictFirst(hc, snap->Key, snap->SlotState, snap->EpState)) {
        return;
    }
    switch (verdict) {
    case XHCI_STRICT_NOT_ENABLED:
        XHCI_DBG_VALUE("strict: violation, Slot Not Enabled expected, "
                       "type/slot/dci/slotst/epst", value);
        XhciLogNote(&hc->Hc, "strict.violation.notenabled", value);
        break;
    case XHCI_STRICT_SLOT_STATE:
        XHCI_DBG_VALUE("strict: violation, Context State Error (slot) "
                       "expected, type/slot/dci/slotst/epst", value);
        XhciLogNote(&hc->Hc, "strict.violation.slotstate", value);
        break;
    case XHCI_STRICT_EP_STATE:
        XHCI_DBG_VALUE("strict: violation, Context State Error (endpoint) "
                       "expected, type/slot/dci/slotst/epst", value);
        XhciLogNote(&hc->Hc, "strict.violation.epstate", value);
        break;
    case XHCI_STRICT_UNDEFINED:
        XHCI_DBG_VALUE("strict: violation, undefined behaviour (Configure "
                       "Endpoint flags), type/slot/dci/slotst/epst", value);
        XhciLogNote(&hc->Hc, "strict.violation.undefined", value);
        break;
    default:
        XHCI_DBG_VALUE("strict: command type not modelled, "
                       "type/slot/dci/slotst/epst", value);
        XhciLogNote(&hc->Hc, "strict.unmodelled", value);
        break;
    }
}

static ULONG hcdStrictEpState(PHCD_CONTROLLER hc, ULONG slotId, ULONG dci)
{
    ULONG offset;

    if (dci == 0 || XhciEndpointContextOffset(&hc->Hc.Layout, slotId, dci,
                                              &offset) != XHCI_LAYOUT_OK) {
        return 7UL;     /* reserved: no endpoint command accepts it */
    }
    return XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]);
}

/*
 * A Configure Endpoint with DC = 0 is checked per endpoint too, from the
 * Input Control Context it points at - the controller's one Input Context
 * (hcd_cfg.c and hcd_enum.c build every command's there). A pointer
 * elsewhere is not read.
 */
static VOID hcdStrictConfigureFlags(PHCD_CONTROLLER hc, const XHCI_TRB *trb,
                                    PHCD_STRICT_SNAP snap)
{
    PXHCI_EXTENSION ext;
    HCD_STRICT_SNAP one;
    volatile ULONG *icc;
    ULONG offset;
    ULONG drop;
    ULONG add;
    ULONG dci;

    ext = &hc->Hc;
    if (trb->Param0 != XhciCommonPA(ext, ext->Layout.InputContextOffset) ||
        XhciInputControlContextOffset(&ext->Layout, &offset) !=
            XHCI_LAYOUT_OK) {
        return;
    }
    icc = XhciCommonAt(ext, offset);
    drop = icc[0];
    add = icc[1];
    for (dci = 2; dci < 32; dci++) {
        if (((add | drop) & (1UL << dci)) == 0) {
            continue;
        }
        one = *snap;
        one.Dci = dci;
        one.EpState = hcdStrictEpState(hc, snap->SlotId, dci);
        if (XhciStrictCheckConfigureEndpoint((add >> dci) & 1UL,
                                             (drop >> dci) & 1UL,
                                             one.EpState) !=
            XHCI_STRICT_OK) {
            /* Rows 6 (a Drop) and 7 (an Add) of the Seen map, apart from
             * the slot states; the trace's slot-state field says which. */
            one.SlotState = 6UL + ((add >> dci) & 1UL);
            hcdStrictReport(hc, &one, XHCI_STRICT_UNDEFINED,
                            hcdStrictPack(&one));
        }
    }
}

VOID HcdStrictBefore(PHCD_CONTROLLER hc, const XHCI_TRB *trb,
                     PHCD_STRICT_SNAP snap)
{
    ULONG offset;
    ULONG output;
    ULONG bit9;

    hc->StrictChecked++;
    snap->Type = XHCI_TRB_GET_TYPE(trb->Control);
    bit9 = (trb->Control & 0x00000200UL) != 0;
    snap->Key = snap->Type | (bit9 ? 32UL : 0UL);
    snap->SlotId = 0;
    snap->Dci = 0;
    snap->SlotState = XHCI_STRICT_SLOT_DISABLED;
    snap->EpState = 0;
    if (XhciStrictNamesSlot(snap->Type)) {
        snap->SlotId = XHCI_TRB_GET_SLOT_ID(trb->Control);
        output = 0;
        if (snap->SlotId != 0 && snap->SlotId <= XHCI_MAX_SLOTS &&
            XhciSlotContextOffset(&hc->Hc.Layout, snap->SlotId, &offset) ==
                XHCI_LAYOUT_OK) {
            output = XHCI_SLOT_GET_STATE(XhciCommonAt(&hc->Hc, offset)[3]);
        }
        snap->SlotState = XhciStrictSlotState(output,
                                              hcdStrictEnabled(hc,
                                                               snap->SlotId));
    }
    if (XhciStrictNamesEndpoint(snap->Type)) {
        snap->Dci = XHCI_TRB_GET_EP_ID(trb->Control);
        snap->EpState = hcdStrictEpState(hc, snap->SlotId, snap->Dci);
    }
    snap->Verdict = XhciStrictCheck(snap->Type, bit9, snap->SlotState,
                                    snap->EpState);
    if (snap->Verdict != XHCI_STRICT_OK) {
        hcdStrictReport(hc, snap, snap->Verdict, hcdStrictPack(snap));
    }
    if (snap->Type == XHCI_TRB_TYPE_CONFIGURE_EP && !bit9 &&
        snap->SlotState != XHCI_STRICT_SLOT_DISABLED) {
        hcdStrictConfigureFlags(hc, trb, snap);
    }
}

VOID HcdStrictAfter(PHCD_CONTROLLER hc, const HCD_STRICT_SNAP *snap,
                    ULONG code, ULONG control)
{
    ULONG bit;
    ULONG value;

    if (code == XHCI_CC_SUCCESS) {
        if (snap->Type == XHCI_TRB_TYPE_ENABLE_SLOT) {
            hcdStrictMark(hc, XHCI_TRB_GET_SLOT_ID(control), 1);
        } else if (snap->Type == XHCI_TRB_TYPE_DISABLE_SLOT) {
            hcdStrictMark(hc, snap->SlotId, 0);
        }
        return;
    }
    if (!XhciStrictIsPreconditionCode(code)) {
        return;
    }
    if (code == XHCI_CC_SLOT_NOT_ENABLED) {
        /* The controller says it is not ours, whatever this driver
         * believed. */
        hcdStrictMark(hc, snap->SlotId, 0);
    }
    hc->StrictRefusals++;
    bit = code == XHCI_CC_SLOT_NOT_ENABLED ? 1UL :
          code == XHCI_CC_PARAMETER_ERROR ? 2UL : 4UL;
    if (snap->Verdict != XHCI_STRICT_OK) {
        bit <<= 3;
    }
    if (hc->StrictRefusalSeen[snap->Key & 63UL] & bit) {
        return;
    }
    hc->StrictRefusalSeen[snap->Key & 63UL] |= bit;
    value = hcdStrictPack(snap);
    XHCI_DBG_VALUE("strict: refused, completion code", code);
    if (snap->Verdict != XHCI_STRICT_OK) {
        XHCI_DBG_VALUE("strict: refused as the table predicted, "
                       "type/slot/dci/slotst/epst before", value);
        XhciLogNote(&hc->Hc, "strict.refused.predicted", value);
    } else {
        XHCI_DBG_VALUE("strict: refused, the table passed it, "
                       "type/slot/dci/slotst/epst before", value);
        XhciLogNote(&hc->Hc, "strict.refused.unpredicted", value);
    }
    XhciLogNote(&hc->Hc, "strict.refused.code", code);
}

#else /* release: strict mode is not in the image */

typedef int hcdStrictReleasePlaceholder;

#endif
