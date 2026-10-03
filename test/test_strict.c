/*
 * test_strict.c - host vectors for strict mode's command precondition table
 * (src\xhci_strict.c).
 *
 * Every cell: each of the 64 TRB type codes, DW3 bit 9 clear and set, each
 * of the six slot states, each of the eight EP State encodings. The expected
 * table below is written out row by row from the spec's pseudo-code (xHCI
 * 1.2 sections 4.6.2 to 4.6.18) rather than computed, so a rule changed in
 * xhci_strict.c without the spec agreeing fails here.
 */

#include <stdio.h>
#include "../src/xhci.h"
#include "../src/xhci_strict.h"
#include "test_harness.h"

/*
 * One row per (type, bit 9). `slot` has one letter per XHCI_STRICT_SLOT_*
 * (Disabled, Enabled, Default, Addressed, Configured, reserved):
 *   O ok, N Slot Not Enabled, S Context State Error (slot), U unmodelled,
 *   E the endpoint decides - ok when its EP State is in `epOk`, else
 *     Context State Error (endpoint).
 */
typedef struct {
    ULONG Type;
    ULONG Bit9;             /* 2: the row holds for both values */
    const char *Slot;
    ULONG EpOk;             /* bit n: EP State n accepted */
} STRICT_ROW;

static const STRICT_ROW rows[] = {
    /* 4.6.2 */
    { XHCI_TRB_TYPE_NOOP_COMMAND,     2, "OOOOOO", 0 },
    /* 4.6.3 */
    { XHCI_TRB_TYPE_ENABLE_SLOT,      2, "OOOOOO", 0 },
    /* 4.6.4 */
    { XHCI_TRB_TYPE_DISABLE_SLOT,     2, "NOOOOO", 0 },
    /* 4.6.5, BSR = 0 then BSR = 1 */
    { XHCI_TRB_TYPE_ADDRESS_DEVICE,   0, "NOOSSS", 0 },
    { XHCI_TRB_TYPE_ADDRESS_DEVICE,   1, "NOSSSS", 0 },
    /* 4.6.6, DC = 0 then DC = 1 */
    { XHCI_TRB_TYPE_CONFIGURE_EP,     0, "NSSOOS", 0 },
    { XHCI_TRB_TYPE_CONFIGURE_EP,     1, "NSSSOS", 0 },
    /* 4.6.7 */
    { XHCI_TRB_TYPE_EVALUATE_CONTEXT, 2, "NSOOOS", 0 },
    /* 4.6.8: Halted */
    { XHCI_TRB_TYPE_RESET_EP,         2, "NSEEES", 1UL << XHCI_EP_STATE_HALTED },
    /* 4.6.9: Running */
    { XHCI_TRB_TYPE_STOP_EP,          2, "NSEEES", 1UL << XHCI_EP_STATE_RUNNING },
    /* 4.6.10: Stopped or Error */
    { XHCI_TRB_TYPE_SET_TR_DEQUEUE,   2, "NSEEES",
      (1UL << XHCI_EP_STATE_STOPPED) | (1UL << XHCI_EP_STATE_ERROR) },
    /* 4.6.11: no Slot Not Enabled branch */
    { XHCI_TRB_TYPE_RESET_DEVICE,     2, "SSSOOS", 0 },
    /* 4.6.12 to 4.6.18 */
    { XHCI_STRICT_TYPE_FORCE_EVENT,         2, "OOOOOO", 0 },
    { XHCI_STRICT_TYPE_NEGOTIATE_BANDWIDTH, 2, "NSSOOS", 0 },
    { XHCI_STRICT_TYPE_SET_LTV,             2, "OOOOOO", 0 },
    { XHCI_STRICT_TYPE_GET_PORT_BANDWIDTH,  2, "OOOOOO", 0 },
    { XHCI_STRICT_TYPE_FORCE_HEADER,        2, "OOOOOO", 0 },
    { XHCI_STRICT_TYPE_GET_EXT_PROPERTY,    2, "OOOOOO", 0 },
    { XHCI_STRICT_TYPE_SET_EXT_PROPERTY,    2, "OOOOOO", 0 }
};

#define ROW_COUNT (sizeof(rows) / sizeof(rows[0]))

static const STRICT_ROW *find_row(ULONG type, ULONG bit9)
{
    ULONG i;

    for (i = 0; i < ROW_COUNT; i++) {
        if (rows[i].Type == type && (rows[i].Bit9 == 2 || rows[i].Bit9 == bit9)) {
            return &rows[i];
        }
    }
    return NULL;
}

static ULONG expected(ULONG type, ULONG bit9, ULONG slot, ULONG ep)
{
    const STRICT_ROW *row;

    row = find_row(type, bit9);
    if (row == NULL) {
        return XHCI_STRICT_UNMODELLED;
    }
    switch (row->Slot[slot]) {
    case 'O':
        return XHCI_STRICT_OK;
    case 'N':
        return XHCI_STRICT_NOT_ENABLED;
    case 'S':
        return XHCI_STRICT_SLOT_STATE;
    case 'E':
        return (row->EpOk >> ep) & 1UL ? XHCI_STRICT_OK : XHCI_STRICT_EP_STATE;
    default:
        return 0xFFFFFFFFUL;
    }
}

static void test_every_cell(void)
{
    char what[96];
    ULONG type;
    ULONG bit9;
    ULONG slot;
    ULONG ep;

    for (type = 0; type < 64; type++) {
        for (bit9 = 0; bit9 < 2; bit9++) {
            for (slot = 0; slot < XHCI_STRICT_SLOT_COUNT; slot++) {
                for (ep = 0; ep < XHCI_STRICT_EP_STATE_COUNT; ep++) {
                    sprintf(what, "type %lu bit9 %lu slot %lu ep %lu",
                            type, bit9, slot, ep);
                    CHECK_EQ(XhciStrictCheck(type, bit9, slot, ep),
                             expected(type, bit9, slot, ep), what);
                }
            }
        }
    }
}

/* The rows themselves, spot-read against the spec's wording, so a typo in
 * the table above cannot pass both sides at once. */
static void test_named_cells(void)
{
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_ADDRESS_DEVICE, 1,
                             XHCI_STRICT_SLOT_DEFAULT, 0),
             XHCI_STRICT_SLOT_STATE,
             "BSR = 1 from Default: 'If the slot is in the Enabled state'");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_ADDRESS_DEVICE, 0,
                             XHCI_STRICT_SLOT_DEFAULT, 0),
             XHCI_STRICT_OK, "BSR = 0 from Default after Reset Device");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_RESET_DEVICE, 0,
                             XHCI_STRICT_SLOT_DEFAULT, 0),
             XHCI_STRICT_SLOT_STATE, "Reset Device from Default");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_CONFIGURE_EP, 1,
                             XHCI_STRICT_SLOT_ADDRESSED, 0),
             XHCI_STRICT_SLOT_STATE, "Deconfigure of an Addressed slot");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_CONFIGURE_EP, 0,
                             XHCI_STRICT_SLOT_ADDRESSED, 0),
             XHCI_STRICT_OK, "Configure Endpoint of an Addressed slot");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_STOP_EP, 0,
                             XHCI_STRICT_SLOT_CONFIGURED,
                             XHCI_EP_STATE_STOPPED),
             XHCI_STRICT_EP_STATE, "Stop Endpoint on a Stopped endpoint");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_STOP_EP, 0,
                             XHCI_STRICT_SLOT_CONFIGURED,
                             XHCI_EP_STATE_HALTED),
             XHCI_STRICT_EP_STATE, "Stop Endpoint on a Halted endpoint");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_RESET_EP, 0,
                             XHCI_STRICT_SLOT_DEFAULT, XHCI_EP_STATE_ERROR),
             XHCI_STRICT_EP_STATE, "Reset Endpoint on an Error endpoint");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_SET_TR_DEQUEUE, 0,
                             XHCI_STRICT_SLOT_ADDRESSED,
                             XHCI_EP_STATE_DISABLED),
             XHCI_STRICT_EP_STATE, "Set TR Dequeue on a Disabled endpoint");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_SET_TR_DEQUEUE, 0,
                             XHCI_STRICT_SLOT_DEFAULT, XHCI_EP_STATE_ERROR),
             XHCI_STRICT_OK, "Set TR Dequeue leaves Error (4.8.3)");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_DISABLE_SLOT, 0,
                             XHCI_STRICT_SLOT_DISABLED, 0),
             XHCI_STRICT_NOT_ENABLED, "Disable Slot of a slot never enabled");
    CHECK_EQ(XhciStrictCheck(XHCI_TRB_TYPE_NORMAL, 0,
                             XHCI_STRICT_SLOT_CONFIGURED, 0),
             XHCI_STRICT_UNMODELLED, "a transfer TRB type is no command");
}

static void test_slot_state(void)
{
    char what[64];
    ULONG raw;
    ULONG want;

    for (raw = 0; raw < 32; raw++) {
        sprintf(what, "not enabled, raw %lu", raw);
        CHECK_EQ(XhciStrictSlotState(raw, 0), XHCI_STRICT_SLOT_DISABLED, what);
        want = raw == XHCI_SLOT_STATE_DISABLED ? XHCI_STRICT_SLOT_ENABLED :
               raw == XHCI_SLOT_STATE_DEFAULT ? XHCI_STRICT_SLOT_DEFAULT :
               raw == XHCI_SLOT_STATE_ADDRESSED ? XHCI_STRICT_SLOT_ADDRESSED :
               raw == XHCI_SLOT_STATE_CONFIGURED ?
                   XHCI_STRICT_SLOT_CONFIGURED : XHCI_STRICT_SLOT_RESERVED;
        sprintf(what, "enabled, raw %lu", raw);
        CHECK_EQ(XhciStrictSlotState(raw, 1), want, what);
    }
}

static void test_names(void)
{
    char what[64];
    ULONG type;
    ULONG slot;
    ULONG ep;

    for (type = 0; type < 64; type++) {
        slot = (type >= XHCI_TRB_TYPE_DISABLE_SLOT &&
                type <= XHCI_TRB_TYPE_RESET_DEVICE) ||
               type == XHCI_STRICT_TYPE_NEGOTIATE_BANDWIDTH;
        ep = type == XHCI_TRB_TYPE_RESET_EP || type == XHCI_TRB_TYPE_STOP_EP ||
             type == XHCI_TRB_TYPE_SET_TR_DEQUEUE;
        sprintf(what, "type %lu names a slot", type);
        CHECK_EQ(XhciStrictNamesSlot(type), slot, what);
        sprintf(what, "type %lu names an endpoint", type);
        CHECK_EQ(XhciStrictNamesEndpoint(type), ep, what);
    }
}

static void test_configure_flags(void)
{
    char what[64];
    ULONG add;
    ULONG drop;
    ULONG ep;
    ULONG want;

    for (add = 0; add < 2; add++) {
        for (drop = 0; drop < 2; drop++) {
            for (ep = 0; ep < XHCI_STRICT_EP_STATE_COUNT; ep++) {
                want = XHCI_STRICT_OK;
                if (add && !drop && ep != XHCI_EP_STATE_DISABLED) {
                    want = XHCI_STRICT_UNDEFINED;
                }
                if (drop && (ep == XHCI_EP_STATE_HALTED ||
                             ep == XHCI_EP_STATE_ERROR)) {
                    want = XHCI_STRICT_UNDEFINED;
                }
                sprintf(what, "add %lu drop %lu ep %lu", add, drop, ep);
                CHECK_EQ(XhciStrictCheckConfigureEndpoint(add, drop, ep), want,
                         what);
            }
        }
    }
    CHECK_EQ(XhciStrictCheckConfigureEndpoint(1, 0, XHCI_EP_STATE_RUNNING),
             XHCI_STRICT_UNDEFINED, "an Add over a Running endpoint");
    CHECK_EQ(XhciStrictCheckConfigureEndpoint(1, 1, XHCI_EP_STATE_STOPPED),
             XHCI_STRICT_OK, "Drop and Add of a Stopped endpoint (recycle)");
    CHECK_EQ(XhciStrictCheckConfigureEndpoint(0, 1, XHCI_EP_STATE_DISABLED),
             XHCI_STRICT_OK, "a Drop of a Disabled endpoint is ignored");
}

static void test_precondition_codes(void)
{
    char what[64];
    ULONG code;
    ULONG want;

    for (code = 0; code < 256; code++) {
        want = code == XHCI_CC_SLOT_NOT_ENABLED ||
               code == XHCI_CC_PARAMETER_ERROR ||
               code == XHCI_CC_CONTEXT_STATE_ERROR;
        sprintf(what, "completion code %lu", code);
        CHECK_EQ(XhciStrictIsPreconditionCode(code), want, what);
    }
    CHECK_EQ(XHCI_CC_SLOT_NOT_ENABLED, 11, "Slot Not Enabled Error is 11");
    CHECK_EQ(XHCI_CC_PARAMETER_ERROR, 17, "Parameter Error is 17");
    CHECK_EQ(XHCI_CC_CONTEXT_STATE_ERROR, 19, "Context State Error is 19");
}

int main(void)
{
    test_every_cell();
    test_named_cells();
    test_slot_state();
    test_names();
    test_configure_flags();
    test_precondition_codes();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures;
}
