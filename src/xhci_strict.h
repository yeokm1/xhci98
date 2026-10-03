/*
 * xhci_strict.h - strict mode: the xHCI 1.2 command precondition table
 * (debug and qemu flavours; hcd_strict.c is the driver half).
 *
 * QEMU's xHC model does not refuse a command issued against the wrong Slot
 * State or EP State, and a conforming controller does - with Context State
 * Error, Slot Not Enabled Error or, for a context it judges invalid,
 * Parameter Error. So a command-sequencing defect passes every VM leg and
 * shows only on metal. This table is what stands in for metal until then:
 * before each command goes out, the driver reads the slot's state (and the
 * target endpoint's) from the Output Device Context and asks the table
 * whether the controller would accept it.
 *
 * Every rule is the spec's own pseudo-code for the command (xHCI 1.2
 * sections 4.6.2 to 4.6.18; local dump docs/references/spec-1.2-dump.txt),
 * whose else branches give the completion code; the slot-state rows are
 * also transcribed in docs/usb-xhci-info/xhci-data-structures.md, "Which
 * Slot State each command requires".
 *
 * DDK-free and pure: part of the core, host-tested by test\test_strict.c.
 * The release flavour compiles none of it.
 */

#ifndef XHCI_STRICT_H
#define XHCI_STRICT_H

#include "xhci_compat.h"

/*
 * The slot state as the table takes it. The Output Slot Context encodes
 * Disabled and Enabled alike as 0 (Table 6-7), and the spec's first test
 * for every slot command is "has been enabled by an Enable Slot Command",
 * so that half is software's knowledge: the driver passes whether it saw
 * Enable Slot succeed for the id with no Disable Slot or HCRST since.
 */
#define XHCI_STRICT_SLOT_DISABLED    0UL
#define XHCI_STRICT_SLOT_ENABLED     1UL
#define XHCI_STRICT_SLOT_DEFAULT     2UL
#define XHCI_STRICT_SLOT_ADDRESSED   3UL
#define XHCI_STRICT_SLOT_CONFIGURED  4UL
#define XHCI_STRICT_SLOT_RESERVED    5UL    /* Slot State 4..31 */
#define XHCI_STRICT_SLOT_COUNT       6UL

/* EP State is taken as the context's raw 3-bit field: 0 Disabled, 1
 * Running, 2 Halted, 3 Stopped, 4 Error, 5..7 reserved (Table 6-8). */
#define XHCI_STRICT_EP_STATE_COUNT   8UL

/* Verdicts. */
#define XHCI_STRICT_OK               0UL
#define XHCI_STRICT_NOT_ENABLED      1UL    /* Slot Not Enabled Error      */
#define XHCI_STRICT_SLOT_STATE       2UL    /* Context State Error (slot)  */
#define XHCI_STRICT_EP_STATE         3UL    /* Context State Error (EP)    */
#define XHCI_STRICT_UNDEFINED        4UL    /* "undefined behavior" text   */
#define XHCI_STRICT_UNMODELLED       5UL    /* not a command TRB type      */

/* The optional command types xhci.h does not name (Table 6-91). */
#define XHCI_STRICT_TYPE_FORCE_EVENT         18UL
#define XHCI_STRICT_TYPE_NEGOTIATE_BANDWIDTH 19UL
#define XHCI_STRICT_TYPE_SET_LTV             20UL
#define XHCI_STRICT_TYPE_GET_PORT_BANDWIDTH  21UL
#define XHCI_STRICT_TYPE_FORCE_HEADER        22UL
#define XHCI_STRICT_TYPE_GET_EXT_PROPERTY    24UL
#define XHCI_STRICT_TYPE_SET_EXT_PROPERTY    25UL

/* The table's slot state from the Output Slot Context's Slot State field
 * and whether software holds the slot enabled. IRQL: any. */
ULONG XhciStrictSlotState(ULONG outputSlotState, ULONG enabled);

/* Whether a command type names a slot (Slot ID in DW3) / an endpoint
 * (Endpoint ID too), so the caller knows which contexts to read. */
ULONG XhciStrictNamesSlot(ULONG trbType);
ULONG XhciStrictNamesEndpoint(ULONG trbType);

/*
 * The verdict for one command: `trbType` from DW3 15:10, `bit9` nonzero
 * when DW3 bit 9 is set (BSR for Address Device, DC for Configure
 * Endpoint; TSP for Reset Endpoint, which no rule reads), `slotState` an
 * XHCI_STRICT_SLOT_* value, `epState` the target endpoint's EP State
 * (read only for Reset Endpoint, Stop Endpoint and Set TR Dequeue
 * Pointer). IRQL: any.
 */
ULONG XhciStrictCheck(ULONG trbType, ULONG bit9, ULONG slotState,
                      ULONG epState);

/*
 * One endpoint of a Configure Endpoint with DC = 0, by its Add and Drop
 * Context flags and its Output EP State (4.6.6): XHCI_STRICT_UNDEFINED for
 * an Add without a Drop on an endpoint that is not Disabled, or a Drop on
 * one that is Halted or Error. IRQL: any.
 */
ULONG XhciStrictCheckConfigureEndpoint(ULONG add, ULONG drop, ULONG epState);

/* Whether a Command Completion Event's code is one a precondition failure
 * answers with: Slot Not Enabled, Parameter or Context State Error. */
ULONG XhciStrictIsPreconditionCode(ULONG completionCode);

#endif /* XHCI_STRICT_H */
