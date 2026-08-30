/*
 * test_ctx.c - the Slot, Endpoint and Input Control Context encoders
 * (src/xhci_ctx.c).
 *
 * Pure golden vectors: every context is built into a plain array and compared
 * DWORD by DWORD against numbers typed out by hand from
 * docs/usb-xhci-info/xhci-data-structures.md section 8, never recomputed from the same macros
 * the encoder uses. A test that says `dw0 == (speed << 20)` proves the shift is
 * spelled the same way twice and nothing else.
 *
 * **This is where Low Speed lives in Phase 6.** The roadmap struck the LS leg of
 * the checkpoint because no QEMU peripheral model declares Low Speed, and since
 * Phase 5 task 7 every connected port is reported to usbport as High Speed - so
 * LS survives *only* in the slot context and in EP0's Max Packet Size, which is
 * exactly what this file pins.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include "../src/xhci.h"
/* For USBPORT_TRANSFER_TYPE_* - batch 7a-A.1's endpoint builder is fed from
 * usbport's endpoint vocabulary, so the vectors have to speak it too. */
#include "../src/xhci_usbport.h"

static int failures;
static int checks;

#define CHECK(cond, what) check_impl((cond), (what), __LINE__)

static void check_impl(int cond, const char *what, int line)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL %s:%d: %s\n", "test_ctx.c", line, what);
    }
}

#define CHECK_EQ(got, want, what) \
    check_eq_impl((unsigned long)(got), (unsigned long)(want), (what), __LINE__)

static void check_eq_impl(unsigned long got, unsigned long want,
                          const char *what, int line)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s:%d: %s (got 0x%08lX, want 0x%08lX)\n",
               "test_ctx.c", line, what, got, want);
    }
}

/*
 * A context block big enough for a whole Input Context at the large stride,
 * plus a sentinel word past the end. Filled with a poison pattern before every
 * build, so "the encoder wrote exactly eight words" is checkable rather than
 * assumed - which is the property that matters at CSZ = 1, where the upper 32
 * bytes of every context are reserved and must not be written.
 */
#define CTX_WORDS   (33UL * (XHCI_CONTEXT_SIZE_LARGE / 4UL) + 4UL)
#define POISON      0xDEADBEEFUL

static ULONG block[CTX_WORDS];

static void poison(void)
{
    ULONG i;

    for (i = 0; i < CTX_WORDS; i++) {
        block[i] = POISON;
    }
}

static ULONG poisonedFrom(ULONG first, ULONG count)
{
    ULONG i;

    for (i = first; i < first + count; i++) {
        if (block[i] != POISON) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Slot Context                                                        */
/* ------------------------------------------------------------------ */

static void slotParamsClear(XHCI_SLOT_PARAMS *p)
{
    ULONG i;

    for (i = 0; i < sizeof(*p) / sizeof(ULONG); i++) {
        ((ULONG *)p)[i] = 0;
    }
}

/*
 * The three speed vectors the Phase 6 checkpoint's struck LS clause is replaced
 * by. Each is the whole slot context of a device on a root port, hand-encoded:
 *
 *   DW0 = Route String 0 | Speed << 20 | Context Entries 1 << 27
 *   DW1 = Root Hub Port Number << 16
 *   DW2 = 0 (no transaction translator - a direct-attach device)
 *   DW3 = 0 (USB Device Address and Slot State are output-only)
 *
 * The PSIV values are the *default* Protocol Speed IDs, which is what a
 * controller with no PSI table reports and what both target VMs use: 1 = Full,
 * 2 = Low, 3 = High (docs/usb-xhci-info/xhci-data-structures.md section 8).
 */
static void test_slot_context_by_speed(void)
{
    XHCI_SLOT_PARAMS p;

    /* Full Speed on root port 1. */
    poison();
    slotParamsClear(&p);
    p.Psiv = 1;
    p.RootHubPort = 1;
    p.ContextEntries = 1;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_OK, "FS slot builds");
    CHECK_EQ(block[0], 0x08100000UL, "FS slot DW0");
    CHECK_EQ(block[1], 0x00010000UL, "FS slot DW1");
    CHECK_EQ(block[2], 0x00000000UL, "FS slot DW2");
    CHECK_EQ(block[3], 0x00000000UL, "FS slot DW3");
    CHECK(poisonedFrom(XHCI_CONTEXT_DWORDS,
                       XHCI_CONTEXT_SIZE_LARGE / 4UL - XHCI_CONTEXT_DWORDS),
          "FS slot wrote nothing past the eight defined DWORDs");

    /* Low Speed on root port 4 - the leg no VM can produce. */
    poison();
    slotParamsClear(&p);
    p.Psiv = 2;
    p.RootHubPort = 4;
    p.ContextEntries = 1;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_OK, "LS slot builds");
    CHECK_EQ(block[0], 0x08200000UL, "LS slot DW0");
    CHECK_EQ(block[1], 0x00040000UL, "LS slot DW1");

    /* High Speed on root port 2. */
    poison();
    slotParamsClear(&p);
    p.Psiv = 3;
    p.RootHubPort = 2;
    p.ContextEntries = 1;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_OK, "HS slot builds");
    CHECK_EQ(block[0], 0x08300000UL, "HS slot DW0");
    CHECK_EQ(block[1], 0x00020000UL, "HS slot DW1");
}

/*
 * The fields Phase 6 never sets and Phase 7b will. They are encoded here so that
 * the hub tier arriving later is a caller change rather than an encoder change -
 * and so that a shift typo in a field nothing currently uses is caught now.
 */
static void test_slot_context_hub_fields(void)
{
    XHCI_SLOT_PARAMS p;

    poison();
    slotParamsClear(&p);
    p.RouteString = 0x00054321UL;
    p.Psiv = 3;
    p.RootHubPort = 5;
    p.ContextEntries = 31;
    p.Hub = 1;
    p.NumberOfPorts = 4;
    p.MultiTt = 1;
    p.MaxExitLatency = 0x1234;
    p.ParentSlotId = 7;
    p.ParentPortNumber = 3;
    p.TtThinkTime = 2;
    p.InterrupterTarget = 0;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_OK, "hub slot builds");
    /* 0x54321 | 3 << 20 | MTT | Hub | 31 << 27 */
    CHECK_EQ(block[0], 0xFE354321UL, "hub slot DW0");
    /* 0x1234 | port 5 << 16 | 4 ports << 24 */
    CHECK_EQ(block[1], 0x04051234UL, "hub slot DW1");
    /* parent slot 7 | parent port 3 << 8 | TTT 2 << 16 */
    CHECK_EQ(block[2], 0x00020307UL, "hub slot DW2");

    /*
     * **MTT on a non-hub is legal**, so the TTT refusal below must not be
     * generalised to it. Table 6-4 p.408: MTT is '1' "if this is a High-speed
     * hub that supports Multiple TTs and the Multiple TT Interface has been
     * enabled by software, **or if this is a Low-/Full-speed device or
     * Full-speed hub and connected to the xHC through a parent High-speed hub
     * that supports Multiple TTs**". That second clause is task 7b-A.3's child
     * field, and this encoder cannot see which parent a device hangs off - so
     * it takes the caller's word.
     */
    poison();
    slotParamsClear(&p);
    p.Psiv = 1;                         /* Full Speed */
    p.RootHubPort = 2;
    p.ContextEntries = 1;
    p.MultiTt = 1;
    p.ParentSlotId = 5;
    p.ParentPortNumber = 2;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_OK,
             "a Full-Speed non-hub behind a multi-TT hub builds with MTT set");
    /* 3 << 20 is HS; this is 1 << 20 | MTT | 1 << 27 */
    CHECK_EQ(block[0], 0x0A100000UL, "MTT set on a non-hub slot DW0");
    CHECK_EQ(block[2], 0x00000205UL, "with its TT pair and TTT 0");
}

static void test_slot_context_refusals(void)
{
    XHCI_SLOT_PARAMS p;

    slotParamsClear(&p);
    p.Psiv = 3;
    p.RootHubPort = 1;
    p.ContextEntries = 1;

    CHECK_EQ(XhciBuildSlotContext(NULL, &p), XHCI_CTX_BAD_PARAM,
             "NULL context refused");
    CHECK_EQ(XhciBuildSlotContext(block, NULL), XHCI_CTX_BAD_PARAM,
             "NULL params refused");

    /*
     * Each of these is a field that would be *masked* by a builder that trusted
     * its caller, and every one of them produces a plausible context describing
     * a different device. The poison check is what says the refusal wrote
     * nothing at all rather than half a context.
     */
    poison();
    p.RootHubPort = 0;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "root port 0 refused");
    CHECK(poisonedFrom(0, XHCI_CONTEXT_DWORDS), "a refused slot wrote nothing");
    p.RootHubPort = 1;

    p.RootHubPort = 256;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "root port past eight bits refused");
    p.RootHubPort = 1;

    p.ContextEntries = 0;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "context entries 0 refused");
    p.ContextEntries = 32;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "context entries past DCI 31 refused");
    p.ContextEntries = 1;

    p.Psiv = 16;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "speed past four bits refused");
    p.Psiv = 3;

    p.RouteString = 0x00100000UL;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "route string past 20 bits refused");
    p.RouteString = 0;

    p.TtThinkTime = 4;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "TT think time past two bits refused");
    p.TtThinkTime = 0;

    /*
     * A TT think time inside its two bits, on a device that is not a hub.
     * Table 6-6 p.409: "If this device is not a High-speed hub (Hub = '0' or
     * Speed != High-speed), then this field shall be '0'." Only the `Hub = '0'`
     * half is answerable here - `Psiv` is the raw Protocol Speed ID and which
     * value means High Speed belongs to the controller's PSI table - and the
     * other half is XhciTopoHubMark's. Distinct from the range refusal above,
     * which a value of 4 would have caught for the wrong reason.
     */
    p.TtThinkTime = 2;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "a legal TT think time on a non-hub refused");
    p.TtThinkTime = 0;

    /*
     * **Half a TT pair** (task 7b-A.3). Table 6-6 p.409 conditions both DW2
     * fields on one sentence and clears them together, so one set without the
     * other is a caller that resolved half the answer - and either half alone
     * describes a split-transaction path the xHC cannot use. Both directions,
     * because the two halves fail differently: a Slot ID with no port names no
     * downstream port, a port with no Slot ID names no hub.
     */
    poison();
    p.ParentSlotId = 5;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "a TT hub Slot ID with no port refused");
    CHECK(poisonedFrom(0, XHCI_CONTEXT_DWORDS),
          "and the refusal wrote nothing");
    p.ParentSlotId = 0;

    p.ParentPortNumber = 3;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "a TT port with no hub Slot ID refused");
    p.ParentPortNumber = 0;

    /* Number of Ports is defined only when Hub = 1; a port count on a non-hub is
     * a caller confusing the device with its parent. */
    p.NumberOfPorts = 4;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "port count on a non-hub refused");
    p.NumberOfPorts = 0;

    /*
     * And the other direction, which task 7b-A.2 needed: 6.2.2.2 p.412 makes
     * "If Hub = '1', then the Number of Ports field shall be initialized" a
     * requirement of a valid Configure Endpoint Input Slot Context, and 0 is the
     * value a caller marking a hub before reading its descriptor would supply.
     */
    poison();
    p.Hub = 1;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_BAD_PARAM,
             "a hub with no ports refused");
    CHECK(poisonedFrom(0, XHCI_CONTEXT_DWORDS),
          "and that refusal wrote nothing either");
    p.NumberOfPorts = 4;
    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_OK,
             "a hub with ports builds");
    p.Hub = 0;
    p.NumberOfPorts = 0;

    CHECK_EQ(XhciBuildSlotContext(block, &p), XHCI_CTX_OK,
             "the vector every refusal was derived from still builds");
}

/* ------------------------------------------------------------------ */
/* Endpoint Context                                                    */
/* ------------------------------------------------------------------ */

/*
 * EP0 at each speed's initial Max Packet Size, hand-encoded:
 *
 *   DW0 = 0 (EP State output-only, Mult 0, MaxPStreams 0, LSA 0, Interval 0)
 *   DW1 = CErr 3 << 1 | EP Type 4 (Control) << 3 | MPS << 16
 *   DW2 = dequeue pointer | DCS
 *   DW3 = 0 (the pointer's high half)
 *   DW4 = Average TRB Length 8
 */
static void test_ep0_context_by_speed(void)
{
    XHCI_EP_PARAMS ep;

    CHECK_EQ(XhciInitialMps0(XHCI_SPEED_LOW), 8UL, "LS EP0 MPS is 8");
    CHECK_EQ(XhciInitialMps0(XHCI_SPEED_FULL), 64UL,
             "FS EP0 MPS starts at 64 - the largest legal value, because the "
             "field bounds what the controller accepts and usbport's first "
             "descriptor request is 64 bytes; declaring 8 babbles (batch 13-E)");
    CHECK_EQ(XhciInitialMps0(XHCI_SPEED_HIGH), 64UL, "HS EP0 MPS is 64");
    CHECK_EQ(XhciInitialMps0(XHCI_SPEED_SUPER), 0UL,
             "SuperSpeed is refused rather than given a plausible 512");
    CHECK_EQ(XhciInitialMps0(XHCI_SPEED_UNKNOWN), 0UL,
             "an undecoded speed is refused");

    poison();
    CHECK_EQ(XhciBuildEp0Params(8UL, 0x00201000UL, 1UL, &ep), XHCI_CTX_OK,
             "LS/FS EP0 params build");
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK,
             "LS/FS EP0 context builds");
    CHECK_EQ(block[0], 0x00000000UL, "EP0 DW0");
    CHECK_EQ(block[1], 0x00080026UL, "EP0 DW1 at MPS 8");
    CHECK_EQ(block[2], 0x00201001UL, "EP0 DW2 carries DCS in bit 0");
    CHECK_EQ(block[3], 0x00000000UL, "EP0 DW3 is the pointer's zero high half");
    CHECK_EQ(block[4], 0x00000008UL, "EP0 DW4 average TRB length");
    CHECK(poisonedFrom(XHCI_CONTEXT_DWORDS,
                       XHCI_CONTEXT_SIZE_LARGE / 4UL - XHCI_CONTEXT_DWORDS),
          "EP0 wrote nothing past the eight defined DWORDs");

    poison();
    CHECK_EQ(XhciBuildEp0Params(64UL, 0x00201000UL, 0UL, &ep), XHCI_CTX_OK,
             "HS EP0 params build");
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK,
             "HS EP0 context builds");
    CHECK_EQ(block[1], 0x00400026UL, "EP0 DW1 at MPS 64");
    CHECK_EQ(block[2], 0x00201000UL, "EP0 DW2 with DCS clear");

    /* The two Full Speed corrections task 6-B.4 exists for. */
    CHECK_EQ(XhciBuildEp0Params(16UL, 0x00201000UL, 1UL, &ep), XHCI_CTX_OK,
             "FS EP0 corrected to 16");
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK, "builds");
    CHECK_EQ(block[1], 0x00100026UL, "EP0 DW1 at MPS 16");
    CHECK_EQ(XhciBuildEp0Params(32UL, 0x00201000UL, 1UL, &ep), XHCI_CTX_OK,
             "FS EP0 corrected to 32");
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK, "builds");
    CHECK_EQ(block[1], 0x00200026UL, "EP0 DW1 at MPS 32");
}

static void test_ep0_params_refusals(void)
{
    XHCI_EP_PARAMS ep;

    CHECK_EQ(XhciBuildEp0Params(8UL, 0x1000UL, 0UL, NULL), XHCI_CTX_BAD_PARAM,
             "NULL params refused");
    /*
     * The legal set, not a range. 0 and 9 are both things a malformed device
     * descriptor produces, and 512 is the SuperSpeed value a copied-in table
     * would supply - all three would otherwise reach the TD Size arithmetic of
     * every later control transfer.
     */
    CHECK_EQ(XhciBuildEp0Params(0UL, 0x1000UL, 0UL, &ep), XHCI_CTX_BAD_PARAM,
             "MPS 0 refused");
    CHECK_EQ(XhciBuildEp0Params(9UL, 0x1000UL, 0UL, &ep), XHCI_CTX_BAD_PARAM,
             "MPS 9 refused");
    CHECK_EQ(XhciBuildEp0Params(512UL, 0x1000UL, 0UL, &ep), XHCI_CTX_BAD_PARAM,
             "MPS 512 refused");
}

static void test_endpoint_context_refusals(void)
{
    XHCI_EP_PARAMS ep;

    CHECK_EQ(XhciBuildEp0Params(64UL, 0x00201000UL, 1UL, &ep), XHCI_CTX_OK,
             "baseline builds");

    CHECK_EQ(XhciBuildEndpointContext(NULL, &ep), XHCI_CTX_BAD_PARAM,
             "NULL context refused");
    CHECK_EQ(XhciBuildEndpointContext(block, NULL), XHCI_CTX_BAD_PARAM,
             "NULL params refused");

    poison();
    ep.DequeuePA = 0;
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_BAD_PARAM,
             "dequeue pointer 0 refused");
    CHECK(poisonedFrom(0, XHCI_CONTEXT_DWORDS),
          "a refused endpoint context wrote nothing");
    /* Bits 3:0 of that DWORD are DCS and RsvdZ, so a misaligned pointer would
     * silently become a different address with a different cycle state. */
    ep.DequeuePA = 0x00201008UL;
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_BAD_PARAM,
             "misaligned dequeue pointer refused");
    ep.DequeuePA = 0x00201000UL;

    ep.AverageTrbLength = 0;
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_BAD_PARAM,
             "average TRB length 0 refused - the spec requires > 0");
    ep.AverageTrbLength = XHCI_EP_AVG_TRB_CONTROL;

    ep.EpType = XHCI_EP_TYPE_INVALID;
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_BAD_PARAM,
             "EP type 0 refused");
    ep.EpType = XHCI_EP_TYPE_CONTROL;

    ep.Dcs = 2;
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_BAD_PARAM,
             "a DCS that is not 0 or 1 refused");
    ep.Dcs = 1;

    /* "CErr shall be set to 0" for isoch (Table 6-9), and must not be 0 for
     * anything else - two refusals, opposite directions, one field. */
    ep.EpType = XHCI_EP_TYPE_ISOCH_IN;
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_BAD_PARAM,
             "isoch with a nonzero error count refused");
    ep.ErrorCount = 0;
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK,
             "isoch with CErr 0 builds");
    ep.EpType = XHCI_EP_TYPE_CONTROL;
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_BAD_PARAM,
             "control with CErr 0 refused");
    ep.ErrorCount = XHCI_EP_CERR_DEFAULT;

    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK,
             "the vector every refusal was derived from still builds");
}

/* ------------------------------------------------------------------ */
/* Input Control Context                                               */
/* ------------------------------------------------------------------ */

static void test_input_control_context(void)
{
    /* Address Device: A0 and A1, nothing dropped. */
    poison();
    CHECK_EQ(XhciBuildInputControlContext(block, XHCI_ICC_A0 | XHCI_ICC_A1, 0),
             XHCI_CTX_OK, "address-device flags build");
    CHECK_EQ(block[XHCI_ICC_DW_DROP], 0x00000000UL, "drop word");
    CHECK_EQ(block[XHCI_ICC_DW_ADD], 0x00000003UL, "add word A0|A1");
    CHECK_EQ(block[XHCI_ICC_DW_CONFIG], 0x00000000UL,
             "DW7 stays zero - CFC is 0 and the field is RsvdZ");
    CHECK(poisonedFrom(XHCI_CONTEXT_DWORDS,
                       XHCI_CONTEXT_SIZE_LARGE / 4UL - XHCI_CONTEXT_DWORDS),
          "input control wrote nothing past the eight defined DWORDs");

    /* Evaluate Context for the MPS0 correction: A1 alone. Evaluating the slot
     * context too would re-assert a Route String and speed the xHC holds. */
    poison();
    CHECK_EQ(XhciBuildInputControlContext(block, XHCI_ICC_A1, 0), XHCI_CTX_OK,
             "evaluate-context flags build");
    CHECK_EQ(block[XHCI_ICC_DW_ADD], 0x00000002UL, "add word A1 alone");

    CHECK_EQ(XhciBuildInputControlContext(NULL, XHCI_ICC_A1, 0),
             XHCI_CTX_BAD_PARAM, "NULL context refused");

    /* D0 and D1 do not exist - the Slot Context and EP0 cannot be dropped, and
     * the bits are RsvdZ. */
    poison();
    CHECK_EQ(XhciBuildInputControlContext(block, 0, 0x00000001UL),
             XHCI_CTX_BAD_PARAM, "D0 refused");
    CHECK(poisonedFrom(0, XHCI_CONTEXT_DWORDS),
          "a refused input control context wrote nothing");
    CHECK_EQ(XhciBuildInputControlContext(block, 0, 0x00000002UL),
             XHCI_CTX_BAD_PARAM, "D1 refused");

    /*
     * **Adding and dropping the same context is legal**, and is how an
     * alternate-interface change reprograms an endpoint the xHC already has
     * enabled (task 7a-B.1). Spec 4.6.6 p.106-107 gives it its own case - "If the
     * Drop Context flag is '1' and the Add Context flag is '1', the xHC shall:
     * Release the current Resources and Bandwidth allocated to the endpoint and
     * assign the new Resources and Bandwidth requested" - and p.106 makes it
     * *required* rather than merely allowed, since an Add without a Drop onto a
     * non-Disabled Output Endpoint Context is undefined behaviour. This builder
     * refused it until batch 7a-B on a rule nothing in the spec states.
     */
    poison();
    CHECK_EQ(XhciBuildInputControlContext(block, XHCI_ICC_FLAG(4),
                                          XHCI_ICC_FLAG(4)),
             XHCI_CTX_OK, "add and drop of one context builds");
    CHECK_EQ(block[XHCI_ICC_DW_DROP], 0x00000010UL, "drop word D4");
    CHECK_EQ(block[XHCI_ICC_DW_ADD], 0x00000010UL, "add word A4");
    CHECK_EQ(XhciBuildInputControlContext(block, XHCI_ICC_FLAG(4),
                                          XHCI_ICC_FLAG(5)),
             XHCI_CTX_OK, "disjoint add and drop build");
    CHECK_EQ(block[XHCI_ICC_DW_DROP], 0x00000020UL, "drop word D5");
    CHECK_EQ(block[XHCI_ICC_DW_ADD], 0x00000010UL, "add word A4");
}

/* ------------------------------------------------------------------ */
/* Both context strides                                                */
/* ------------------------------------------------------------------ */

/*
 * The stride decides where the *next* context starts, and nothing else: at
 * either one the encoder writes eight words at the base it is given. This walks
 * a whole Input Context at both strides through the offset accessors, which is
 * the pairing that actually matters - an encoder that wrote ContextSize/4 words
 * would overwrite the following context at CSZ = 0 and clear reserved bytes at
 * CSZ = 1, and only one of those two failures is visible from a single build.
 */
static void test_both_strides(void)
{
    static const ULONG strides[2] = { XHCI_CONTEXT_SIZE_SMALL,
                                      XHCI_CONTEXT_SIZE_LARGE };
    XHCI_HC_LAYOUT layout;
    XHCI_SLOT_PARAMS slot;
    XHCI_EP_PARAMS ep;
    ULONG which;

    for (which = 0; which < 2; which++) {
        ULONG stride = strides[which];
        ULONG controlOffset;
        ULONG slotOffset;
        ULONG epOffset;
        ULONG base;

        CHECK_EQ(XhciComputeLayout(stride, 8UL, 0UL, 1UL, &layout),
                 XHCI_LAYOUT_OK, "layout for this stride");

        CHECK_EQ(XhciInputControlContextOffset(&layout, &controlOffset),
                 XHCI_LAYOUT_OK, "input control offset");
        CHECK_EQ(XhciInputSlotContextOffset(&layout, &slotOffset),
                 XHCI_LAYOUT_OK, "input slot offset");
        CHECK_EQ(XhciInputEndpointContextOffset(&layout, 1, &epOffset),
                 XHCI_LAYOUT_OK, "input EP0 offset");

        base = layout.InputContextOffset;
        CHECK_EQ(controlOffset - base, 0UL, "control context is index 0");
        CHECK_EQ(slotOffset - base, stride, "slot context is index 1");
        CHECK_EQ(epOffset - base, 2UL * stride,
                 "EP0's context is index 2 - the input context shifts by one");

        poison();
        CHECK_EQ(XhciBuildInputControlContext(
                     &block[(controlOffset - base) / 4UL],
                     XHCI_ICC_A0 | XHCI_ICC_A1, 0),
                 XHCI_CTX_OK, "control context builds at this stride");

        slotParamsClear(&slot);
        slot.Psiv = 3;
        slot.RootHubPort = 1;
        slot.ContextEntries = 1;
        CHECK_EQ(XhciBuildSlotContext(&block[(slotOffset - base) / 4UL], &slot),
                 XHCI_CTX_OK, "slot context builds at this stride");

        CHECK_EQ(XhciBuildEp0Params(64UL, 0x00201000UL, 1UL, &ep), XHCI_CTX_OK,
                 "EP0 params");
        CHECK_EQ(XhciBuildEndpointContext(&block[(epOffset - base) / 4UL], &ep),
                 XHCI_CTX_OK, "EP0 context builds at this stride");

        /* The three contexts are still distinguishable: the slot's DW0 was not
         * overwritten by the endpoint that follows it, and vice versa. */
        CHECK_EQ(block[(controlOffset - base) / 4UL + XHCI_ICC_DW_ADD],
                 0x00000003UL, "control context survived the two after it");
        CHECK_EQ(block[(slotOffset - base) / 4UL], 0x08300000UL,
                 "slot context survived the endpoint after it");
        CHECK_EQ(block[(epOffset - base) / 4UL + 1UL], 0x00400026UL,
                 "endpoint context is where the offset said");
    }
}

/* ------------------------------------------------------------------ */
/* Batch 7a-A.1: the interval conversion and the non-default endpoint  */
/* ------------------------------------------------------------------ */

/*
 * The full reachable space of usbport's `Period`, typed out by hand.
 *
 * These are *not* the `bInterval` conversion in docs/usb-xhci-info/xhci-data-structures.md
 * section 8 - usbport hands the miniport no `bInterval` *(this said the miniport
 * "never sees" one until the post-Phase 13 review rounds; task 9-A.2's descriptor snoop sees it, on
 * EP0 and for isochronous endpoints only, and it never reaches this path)*.
 * usbport has already applied
 * that table and hands over a bucketed `Period` whose unit **differs by speed**:
 * microframes on High Speed, frames on Full/Low Speed
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 5, proven from the 63-entry schedule
 * table in both usbehci builds). So the expected values below are
 * `log2(Period)` for HS and `log2(Period) + 3` for FS/LS, written out rather
 * than computed, because a test that recomputes the formula proves only that it
 * is spelled the same way twice.
 *
 * usbport's own clamps mean the reachable set is small: HS 0-5, FS 3-8, and
 * **LS 6-8** - usbport raises an LS Period below 8 to 8 before it ever arrives,
 * so 3-5 are unreachable at that speed and this function refuses them. The
 * "LS 3-8" this comment used to say was the Full-Speed range copied across, and
 * it survived the eighth review's own correction to the code below it.
 */
static void test_interval_from_period(void)
{
    ULONG iv;

    /* High Speed: Period counts microframes, so Interval == log2(Period).
     * Period 1 is 125 us and Period 32 is 4 ms. */
    CHECK_EQ(XhciIntervalFromPeriod(1UL, XHCI_SPEED_HIGH, &iv), XHCI_CTX_OK,
             "HS period 1 accepted");
    CHECK_EQ(iv, 0UL, "HS period 1 microframe -> Interval 0 (125 us)");
    CHECK_EQ(XhciIntervalFromPeriod(2UL, XHCI_SPEED_HIGH, &iv), XHCI_CTX_OK,
             "HS period 2 accepted");
    CHECK_EQ(iv, 1UL, "HS period 2 -> Interval 1 (250 us)");
    CHECK_EQ(XhciIntervalFromPeriod(4UL, XHCI_SPEED_HIGH, &iv), XHCI_CTX_OK,
             "HS period 4 accepted");
    CHECK_EQ(iv, 2UL, "HS period 4 -> Interval 2 (500 us)");
    CHECK_EQ(XhciIntervalFromPeriod(8UL, XHCI_SPEED_HIGH, &iv), XHCI_CTX_OK,
             "HS period 8 accepted");
    CHECK_EQ(iv, 3UL, "HS period 8 -> Interval 3 (1 ms, once per frame)");
    CHECK_EQ(XhciIntervalFromPeriod(16UL, XHCI_SPEED_HIGH, &iv), XHCI_CTX_OK,
             "HS period 16 accepted");
    CHECK_EQ(iv, 4UL, "HS period 16 -> Interval 4 (2 ms)");
    CHECK_EQ(XhciIntervalFromPeriod(32UL, XHCI_SPEED_HIGH, &iv), XHCI_CTX_OK,
             "HS period 32 accepted");
    CHECK_EQ(iv, 5UL, "HS period 32 -> Interval 5 (4 ms), usbport's ceiling");

    /* Full Speed: Period counts frames, so Interval == log2(Period) + 3. */
    CHECK_EQ(XhciIntervalFromPeriod(1UL, XHCI_SPEED_FULL, &iv), XHCI_CTX_OK,
             "FS period 1 accepted");
    CHECK_EQ(iv, 3UL, "FS period 1 frame -> Interval 3 (1 ms)");
    CHECK_EQ(XhciIntervalFromPeriod(2UL, XHCI_SPEED_FULL, &iv), XHCI_CTX_OK,
             "FS period 2 accepted");
    CHECK_EQ(iv, 4UL, "FS period 2 -> Interval 4 (2 ms)");
    CHECK_EQ(XhciIntervalFromPeriod(4UL, XHCI_SPEED_FULL, &iv), XHCI_CTX_OK,
             "FS period 4 accepted");
    CHECK_EQ(iv, 5UL, "FS period 4 -> Interval 5 (4 ms)");
    CHECK_EQ(XhciIntervalFromPeriod(8UL, XHCI_SPEED_FULL, &iv), XHCI_CTX_OK,
             "FS period 8 accepted");
    CHECK_EQ(iv, 6UL, "FS period 8 -> Interval 6 (8 ms)");
    CHECK_EQ(XhciIntervalFromPeriod(16UL, XHCI_SPEED_FULL, &iv), XHCI_CTX_OK,
             "FS period 16 accepted");
    CHECK_EQ(iv, 7UL, "FS period 16 -> Interval 7 (16 ms)");
    CHECK_EQ(XhciIntervalFromPeriod(32UL, XHCI_SPEED_FULL, &iv), XHCI_CTX_OK,
             "FS period 32 accepted");
    CHECK_EQ(iv, 8UL, "FS period 32 -> Interval 8 (32 ms)");

    /*
     * Low Speed shares Full Speed's frame unit but **not** its range: usbport
     * floors an LS Period at 8, so 1/2/4 are outside the contract at this speed
     * and are refused below rather than translated.
     *
     * An earlier version of this file accepted them, on the argument that the
     * floor was usbport's and this code should translate rather than repair.
     * That confused two things - *repairing* would be silently raising a small
     * Period to 8, which hides a misread field; refusing surfaces one.
     */
    CHECK_EQ(XhciIntervalFromPeriod(8UL, XHCI_SPEED_LOW, &iv), XHCI_CTX_OK,
             "LS period 8 accepted");
    CHECK_EQ(iv, 6UL, "LS period 8 -> Interval 6 (8 ms), usbport's LS floor");
    CHECK_EQ(XhciIntervalFromPeriod(16UL, XHCI_SPEED_LOW, &iv), XHCI_CTX_OK,
             "LS period 16 accepted");
    CHECK_EQ(iv, 7UL, "LS period 16 -> Interval 7 (16 ms)");
    CHECK_EQ(XhciIntervalFromPeriod(32UL, XHCI_SPEED_LOW, &iv), XHCI_CTX_OK,
             "LS period 32 accepted");
    CHECK_EQ(iv, 8UL, "LS period 32 -> Interval 8 (32 ms)");
    /*
     * **All three** of the sub-floor buckets, because the refusal is a range and
     * a range needs its interior tested: the ninth review's mutation replaced
     * `period < 8` with `period == 1 || period == 4` and the suite stayed green
     * while LS Period 2 was accepted and encoded as Interval 4.
     */
    CHECK_EQ(XhciIntervalFromPeriod(1UL, XHCI_SPEED_LOW, &iv),
             XHCI_CTX_BAD_PARAM,
             "LS period 1 refused - below usbport's own floor of 8");
    CHECK_EQ(XhciIntervalFromPeriod(2UL, XHCI_SPEED_LOW, &iv),
             XHCI_CTX_BAD_PARAM, "LS period 2 refused for the same reason");
    CHECK_EQ(XhciIntervalFromPeriod(4UL, XHCI_SPEED_LOW, &iv),
             XHCI_CTX_BAD_PARAM, "LS period 4 refused for the same reason");
    /* The same values are legal at Full Speed, so the refusal is speed-specific
     * rather than a blanket lower bound. */
    CHECK_EQ(XhciIntervalFromPeriod(4UL, XHCI_SPEED_FULL, &iv), XHCI_CTX_OK,
             "FS period 4 still accepted - the floor is Low Speed's alone");

    /*
     * The same Period means different intervals at different speeds, which is
     * the whole reason this function takes a speed. Pinned as a pair so a
     * regression that dropped the speed term would have to fail here.
     */
    CHECK_EQ(XhciIntervalFromPeriod(8UL, XHCI_SPEED_HIGH, &iv), XHCI_CTX_OK, "x");
    CHECK_EQ(iv, 3UL, "period 8 at HS is 1 ms");
    CHECK_EQ(XhciIntervalFromPeriod(8UL, XHCI_SPEED_FULL, &iv), XHCI_CTX_OK, "x");
    CHECK_EQ(iv, 6UL, "the same period 8 at FS is 8 ms - an 8x difference");
}

/* Refusals: a Period outside usbport's derived contract is a misread field, not
 * a value to repair. */
static void test_interval_refusals(void)
{
    ULONG iv;

    CHECK_EQ(XhciIntervalFromPeriod(0UL, XHCI_SPEED_HIGH, &iv),
             XHCI_CTX_BAD_PARAM, "period 0 refused - usbport never sends it");
    CHECK_EQ(XhciIntervalFromPeriod(3UL, XHCI_SPEED_HIGH, &iv),
             XHCI_CTX_BAD_PARAM, "period 3 refused - not a power of two");
    CHECK_EQ(XhciIntervalFromPeriod(24UL, XHCI_SPEED_FULL, &iv),
             XHCI_CTX_BAD_PARAM, "period 24 refused - not a power of two");
    CHECK_EQ(XhciIntervalFromPeriod(64UL, XHCI_SPEED_HIGH, &iv),
             XHCI_CTX_BAD_PARAM, "period 64 refused - above usbport's clamp");
    CHECK_EQ(XhciIntervalFromPeriod(255UL, XHCI_SPEED_FULL, &iv),
             XHCI_CTX_BAD_PARAM, "period 255 refused - a raw bInterval, not a "
                                 "bucketed Period");
    CHECK_EQ(XhciIntervalFromPeriod(8UL, 0UL, &iv), XHCI_CTX_BAD_PARAM,
             "unknown speed class refused");
    CHECK_EQ(XhciIntervalFromPeriod(8UL, XHCI_SPEED_HIGH, NULL),
             XHCI_CTX_BAD_PARAM, "null out pointer refused");
}

/* DCI math, spec 4.5.1, typed out rather than derived. */
static void test_dci_from_address(void)
{
    CHECK_EQ(XhciDciFromEndpointAddress(0x00UL), 1UL, "EP0 is DCI 1");
    CHECK_EQ(XhciDciFromEndpointAddress(0x01UL), 2UL, "EP1 OUT is DCI 2");
    CHECK_EQ(XhciDciFromEndpointAddress(0x81UL), 3UL, "EP1 IN is DCI 3");
    CHECK_EQ(XhciDciFromEndpointAddress(0x02UL), 4UL, "EP2 OUT is DCI 4");
    CHECK_EQ(XhciDciFromEndpointAddress(0x82UL), 5UL, "EP2 IN is DCI 5");
    CHECK_EQ(XhciDciFromEndpointAddress(0x0FUL), 30UL, "EP15 OUT is DCI 30");
    CHECK_EQ(XhciDciFromEndpointAddress(0x8FUL), 31UL, "EP15 IN is DCI 31");
    /*
     * Spec 4.5.1 **ignores** the direction bit on a control endpoint, so 0x80
     * is DCI 1 exactly like 0x00 - one bidirectional endpoint, one context. An
     * earlier version of this file expected a refusal here, on invented
     * reasoning that a set bit made the address malformed. The bit is not a
     * claim the address makes; it is a field the spec says to disregard.
     */
    CHECK_EQ(XhciDciFromEndpointAddress(0x80UL), 1UL,
             "EP0 with the direction bit set is still DCI 1");
    /* Bits 6:4 are reserved in bEndpointAddress and are ignored, not folded
     * into the endpoint number. */
    CHECK_EQ(XhciDciFromEndpointAddress(0x71UL), 2UL,
             "reserved bits 6:4 ignored, not folded into the number");
}

/*
 * The non-default endpoint parameter builder, one vector per type/direction,
 * checked through to the encoded DWORDs so the EP Type numbering is pinned
 * against Table 6-9 rather than against the enum's own spelling.
 */
static void test_endpoint_params(void)
{
    XHCI_EP_PARAMS ep;
    ULONG block[XHCI_CONTEXT_DWORDS + 1];

    /* HS interrupt IN, 8-byte reports, Period 8 (1 ms). A boot keyboard. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     8UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_OK, "HS interrupt IN accepted");
    CHECK_EQ(ep.EpType, 7UL, "interrupt IN is EP Type 7");
    CHECK_EQ(ep.Interval, 3UL, "period 8 microframes -> Interval 3");
    CHECK_EQ(ep.MaxPacketSize, 8UL, "max packet size carried");
    CHECK_EQ(ep.ErrorCount, 3UL, "CErr 3 for a non-isoch endpoint");
    CHECK_EQ(ep.MaxBurstSize, 0UL, "no bursting on USB 2.0");
    CHECK_EQ(ep.Mult, 0UL, "no Mult on USB 2.0");
    CHECK_EQ(ep.AverageTrbLength, 8UL, "average TRB length is the packet size");
    CHECK_EQ(ep.MaxEsitPayload, 8UL,
             "Max ESIT Payload is MPS * (burst + 1) on a periodic endpoint");

    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK,
             "HS interrupt IN context built");
    /* DW0: Interval 3 at 23:16, everything else zero. */
    CHECK_EQ(block[0], 0x00030000UL, "interrupt DW0 carries Interval only");
    /* DW1: MPS 8 at 31:16, EP Type 7 at 5:3 = 0x38, CErr 3 at 2:1 = 0x06. */
    CHECK_EQ(block[1], 0x0008003EUL, "interrupt DW1 MPS/type/CErr");
    /* DW4: average TRB length 8 at 15:0, Max ESIT Payload 8 at 31:16. */
    CHECK_EQ(block[4], 0x00080008UL, "interrupt DW4 avg TRB + Max ESIT");

    /* FS interrupt OUT, Period 8 (8 ms this time - the same number, a
     * different meaning). */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 0UL, 8UL,
                                     8UL, XHCI_SPEED_FULL, XHCI_SPEED_FULL,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_OK, "FS interrupt OUT accepted");
    CHECK_EQ(ep.EpType, 3UL, "interrupt OUT is EP Type 3");
    CHECK_EQ(ep.Interval, 6UL, "period 8 frames -> Interval 6, not 3");

    /*
     * Low Speed, which had **no valid vector through this function at all**
     * until the ninth review pointed it out - every LS check was against
     * `XhciIntervalFromPeriod` directly, so the speed had never been carried
     * through the builder that consumes it. This is the shape of a real HID
     * endpoint on a Low-Speed keyboard: 8-byte packets, 8 ms.
     */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     8UL, XHCI_SPEED_LOW, XHCI_SPEED_LOW, 1UL,
                                     0UL, 0x00301800UL, 1UL, &ep, NULL, NULL),
             XHCI_CTX_OK, "LS interrupt IN accepted");
    CHECK_EQ(ep.EpType, 7UL, "interrupt IN is EP Type 7");
    CHECK_EQ(ep.Interval, 6UL, "LS period 8 frames -> Interval 6");
    CHECK_EQ(ep.MaxBurstSize, 0UL, "Low Speed never bursts");
    CHECK_EQ(ep.MaxEsitPayload, 8UL, "LS Max ESIT Payload is one packet");
    /* An LS Period below usbport's floor must not become an endpoint either -
     * the refusal has to survive the builder, not only the converter. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     4UL, XHCI_SPEED_LOW, XHCI_SPEED_LOW, 1UL,
                                     0UL, 0x00301800UL, 1UL, &ep, NULL, NULL),
             XHCI_CTX_BAD_PARAM, "LS period 4 refused through the builder too");

    /* Bulk: no interval, no Max ESIT Payload, and the Period is not read. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_BULK, 1UL, 512UL,
                                     0UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     1UL, 0UL, 0x00301400UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_OK, "HS bulk IN accepted with Period 0");
    CHECK_EQ(ep.EpType, 6UL, "bulk IN is EP Type 6");
    CHECK_EQ(ep.Interval, 0UL, "bulk has no service interval");
    CHECK_EQ(ep.MaxEsitPayload, 0UL, "Max ESIT Payload is periodic-only");
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_BULK, 0UL, 64UL,
                                     0UL, XHCI_SPEED_FULL, XHCI_SPEED_FULL,
                                     1UL, 0UL, 0x00301400UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_OK, "FS bulk OUT accepted");
    CHECK_EQ(ep.EpType, 2UL, "bulk OUT is EP Type 2");

    /*
     * Task 8-A.1's **streams disabled** clause, checked on the encoded context
     * rather than on the parameter block, because that is where it could go
     * wrong: `XHCI_EP_PARAMS` has no stream fields at all, so "streams are
     * disabled" is a property of the *encoder* leaving DW0's Max Primary Streams
     * (bits 14:10) and LSA (bit 15) at the zero its clearing loop wrote.
     *
     * It matters for bulk and only for bulk - streams are a bulk-endpoint
     * feature (Table 6-10) - and it is what makes the TR Dequeue Pointer in DW2
     * a ring address rather than a Stream Context Array address. A nonzero Max
     * Primary Streams here would have the xHC read the ring's first TRB as a
     * Stream Context and follow whatever it found.
     */
    {
        ULONG block[XHCI_CONTEXT_DWORDS + 1];
        ULONG i;

        for (i = 0; i < XHCI_CONTEXT_DWORDS + 1; i++) {
            block[i] = 0xFFFFFFFFUL;
        }
        CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_BULK, 1UL,
                                         512UL, 0UL, XHCI_SPEED_HIGH,
                                         XHCI_SPEED_HIGH, 1UL, 0UL,
                                         0x00301400UL, 1UL, &ep, NULL, NULL),
                 XHCI_CTX_OK, "(a HS bulk IN endpoint)");
        CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK,
                 "encoded into a context filled with ones");
        CHECK_EQ(block[0] & 0x00007C00UL, 0,
                 "Max Primary Streams is 0 - this driver opens no streams, so "
                 "DW2 is a Transfer Ring address and not a Stream Context Array");
        CHECK_EQ(block[0] & 0x00008000UL, 0,
                 "and LSA with it, which only means anything when streams are on");
        CHECK_EQ(block[0] & 0x00FF0000UL, 0, "Interval 0 for bulk");
        CHECK_EQ(block[0] & 0x00000007UL, 0,
                 "EP State is output-only and shall be 0 in an Input Context");
    }

    /*
     * Task 9-A.1: isochronous, whose Interval cannot come from `Period` (which
     * usbport forces to 1). These vectors pass no `bInterval`, so what they pin
     * is the **fallback** - the cadence usbport is itself scheduling to, one
     * packet per microframe on High Speed and one per frame otherwise (task
     * 9-0.1), a transcription of the other side's arithmetic stated as an
     * assumption rather than a derivation.
     *
     * *(This said the Interval comes from neither `Period` nor "a descriptor
     * (which the miniport never sees)", until the post-Phase 13 review rounds. Task 9-A.2 built that
     * channel: where the snooped `bInterval` is known, `src/xhci_ctx.c` derives
     * the Interval from it and these two constants are not used. The vectors
     * below are the case where it is not known - which is why they still read
     * as they did.)*
     */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 1UL,
                                     1024UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 1UL, 0UL, 0x00301C00UL,
                                     1UL, &ep, NULL, NULL),
             XHCI_CTX_OK, "HS isoch IN accepted");
    CHECK_EQ(ep.EpType, 5UL, "isoch IN is EP Type 5");
    CHECK_EQ(ep.Interval, 0UL,
             "HS isoch: usbport stamps one packet per microframe -> Interval 0");
    CHECK_EQ(ep.ErrorCount, 0UL,
             "CErr shall be 0 for isoch (Table 6-9) - there is no handshake to "
             "retry against");
    CHECK_EQ(ep.MaxBurstSize, 0UL, "one transaction per microframe here");
    CHECK_EQ(ep.MaxEsitPayload, 1024UL, "MPS * (burst + 1) * (Mult + 1)");
    CHECK_EQ(ep.AverageTrbLength, 1024UL, "average TRB length is the packet");

    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 0UL,
                                     192UL, 1UL, XHCI_SPEED_FULL,
                                     XHCI_SPEED_FULL, 1UL, 0UL, 0x00301C00UL,
                                     1UL, &ep, NULL, NULL),
             XHCI_CTX_OK, "FS isoch OUT accepted");
    CHECK_EQ(ep.EpType, 1UL, "isoch OUT is EP Type 1");
    CHECK_EQ(ep.Interval, 3UL,
             "FS isoch: usbport stamps one packet per frame -> Interval 3, "
             "which is also Table 6-12's floor for the FS Isoch row");
    CHECK_EQ(ep.ErrorCount, 0UL, "CErr 0 whichever direction");
    CHECK_EQ(ep.MaxEsitPayload, 192UL, "one packet per ESIT at Full Speed");

    /* A high-bandwidth HS isoch endpoint - 3 transactions per microframe, which
     * is where Max Burst and Max ESIT Payload stop coinciding with the packet
     * size. 6.2.3.4 p.418 names isoch in the same sentence as interrupt. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 1UL,
                                     1024UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 3UL, 0UL, 0x00301C00UL,
                                     1UL, &ep, NULL, NULL),
             XHCI_CTX_OK, "high-bandwidth HS isoch accepted");
    CHECK_EQ(ep.MaxBurstSize, 2UL, "Max Burst is the count minus one");
    CHECK_EQ(ep.MaxEsitPayload, 3072UL,
             "and the ESIT payload follows it, not the packet size");

    /*
     * The Endpoint Context an isochronous endpoint encodes to, checked whole -
     * CErr 0 is a *zero* field, which is exactly the kind of value a builder can
     * fail to write and a parameter check cannot tell from a correct one.
     */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 1UL,
                                     1024UL, 1UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 1UL, 0UL, 0x00301C00UL,
                                     1UL, &ep, NULL, NULL),
             XHCI_CTX_OK, "(a HS isoch IN endpoint)");
    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK,
             "HS isoch IN context built");
    CHECK_EQ(block[0], 0x00000000UL,
             "isoch DW0: Interval 0, and Mult/MaxPStreams/LSA all zero");
    /* DW1: MPS 1024 at 31:16, EP Type 5 at 5:3 = 0x28, CErr 0 at 2:1. */
    CHECK_EQ(block[1], 0x04000028UL,
             "isoch DW1 carries EP Type 5 with CErr 0 beside it");
    CHECK_EQ(block[4], 0x04000400UL, "isoch DW4 avg TRB + Max ESIT");
}

/*
 * High-bandwidth High-Speed periodic endpoints - 2 or 3 transactions per
 * microframe, which usbport reports in `TransactionPerMicroframe`
 * (wMaxPacketSize bits 12:11 + 1).
 *
 * The first version of the builder hard-coded Max Burst to 0 and asserted that
 * the field was SuperSpeed-only. It is not: Table 6-11 / spec 6.2.3.4 take it
 * from exactly this count for HS periodic endpoints, so a 3-transaction
 * interrupt endpoint was being given a third of the bandwidth it asked for -
 * silently, because every existing vector used a single-transaction endpoint
 * and so could not tell 0 from "correct".
 */
static void test_endpoint_params_high_bandwidth(void)
{
    XHCI_EP_PARAMS ep;
    ULONG block[XHCI_CONTEXT_DWORDS + 1];

    /* HS interrupt IN, 1024-byte packets, 3 transactions per microframe. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL,
                                     1024UL, 8UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 3UL, 0UL, 0x00301000UL,
                                     1UL, &ep, NULL, NULL),
             XHCI_CTX_OK, "HS high-bandwidth interrupt accepted");
    CHECK_EQ(ep.MaxBurstSize, 2UL, "Max Burst is additional transactions, 3-1");
    CHECK_EQ(ep.MaxEsitPayload, 3072UL, "Max ESIT Payload is MPS * 3");
    CHECK_EQ(ep.Mult, 0UL, "Mult stays 0 - SuperSpeed isoch only");

    CHECK_EQ(XhciBuildEndpointContext(block, &ep), XHCI_CTX_OK,
             "high-bandwidth context built");
    /* DW1: MPS 1024 at 31:16 = 0x04000000, Max Burst 2 at 15:8 = 0x0200,
     * EP Type 7 at 5:3 = 0x38, CErr 3 at 2:1 = 0x06. */
    CHECK_EQ(block[1], 0x0400023EUL, "DW1 carries Max Burst 2");
    /* DW4: average TRB length 1024 at 15:0, Max ESIT 3072 at 31:16. */
    CHECK_EQ(block[4], 0x0C000400UL, "DW4 Max ESIT Payload 3072");

    /* Two transactions. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL,
                                     512UL, 8UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 2UL, 0UL, 0x00301000UL,
                                     1UL, &ep, NULL, NULL),
             XHCI_CTX_OK, "HS 2-transaction interrupt accepted");
    CHECK_EQ(ep.MaxBurstSize, 1UL, "Max Burst 1");
    CHECK_EQ(ep.MaxEsitPayload, 1024UL, "Max ESIT Payload is MPS * 2");

    /* A single-transaction HS endpoint is still burst 0 - the ordinary case
     * must not have moved. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     8UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_OK, "single-transaction HS interrupt accepted");
    CHECK_EQ(ep.MaxBurstSize, 0UL, "one transaction means no additional burst");
    CHECK_EQ(ep.MaxEsitPayload, 8UL, "Max ESIT Payload is one packet");

    /*
     * Full and Low Speed have no microframes, so a count above one is not
     * something usbport can have meant - refused rather than quietly dropped,
     * which is the direction that would under-program the endpoint.
     *
     * **Both speeds, because the sentence above names both.** Until the ninth
     * review only the Full-Speed half was here, and narrowing the source guard
     * to `speedClass == XHCI_SPEED_FULL` left the suite green while a Low-Speed
     * 2-transaction endpoint was accepted with Max Burst 0.
     */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL,
                                     64UL, 8UL, XHCI_SPEED_FULL,
                                     XHCI_SPEED_FULL, 2UL, 0UL, 0x00301000UL,
                                     1UL, &ep, NULL, NULL),
             XHCI_CTX_BAD_PARAM, "FS cannot have 2 transactions per microframe");
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     8UL, XHCI_SPEED_LOW, XHCI_SPEED_LOW, 2UL,
                                     0UL, 0x00301000UL, 1UL, &ep, NULL, NULL),
             XHCI_CTX_BAD_PARAM, "LS cannot have 2 transactions per microframe");
    /* Bulk has no microframe structure to burst within either. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_BULK, 1UL, 512UL,
                                     0UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     2UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_BAD_PARAM, "bulk cannot be high-bandwidth");

    /* Out-of-range counts. 0 is a caller reading the wrong offset. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     8UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     0UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_BAD_PARAM, "zero transactions refused");
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     8UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     4UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_BAD_PARAM, "four transactions refused - not a USB 2.0 value");
}

static void test_endpoint_params_refusals(void)
{
    XHCI_EP_PARAMS ep;

    /* Low Speed has no isochronous endpoints at all (USB 2.0 section 5.6), so
     * this is a request describing no legal device rather than a limitation of
     * this driver - and it is refused rather than given the Full-Speed
     * treatment, which would build a plausible context for a pipe that cannot
     * exist. Task 9-A.1 admitted the other two speeds; this one stayed out. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 1UL,
                                     8UL, 1UL, XHCI_SPEED_LOW, XHCI_SPEED_LOW,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_BAD_PARAM, "Low Speed has no isochronous endpoints");
    /* A Full-Speed isoch endpoint claiming additional transactions per
     * microframe: 6.2.3.4 p.418 clears Max Burst for every Low/Full-Speed
     * endpoint, so the count is refused rather than silently zeroed. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_ISOCHRONOUS, 1UL,
                                     1023UL, 1UL, XHCI_SPEED_FULL,
                                     XHCI_SPEED_FULL, 2UL, 0UL, 0x00301000UL,
                                     1UL, &ep, NULL, NULL),
             XHCI_CTX_BAD_PARAM, "FS isoch cannot be high-bandwidth");
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_CONTROL, 1UL, 64UL,
                                     0UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_BAD_PARAM, "non-default control endpoint refused");

    /* An interrupt endpoint whose Period is outside the contract must not
     * become a plausible endpoint. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     0UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_BAD_PARAM, "interrupt with Period 0 refused");
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     12UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_BAD_PARAM, "interrupt with a non-power-of-two refused");

    /* wMaxPacketSize is 11 bits; 0 would divide the TD Size arithmetic by
     * zero exactly as it would on EP0. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 0UL,
                                     8UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_BAD_PARAM, "zero max packet size refused");
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_BULK, 1UL, 0x800UL,
                                     0UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep, NULL,
                                     NULL),
             XHCI_CTX_BAD_PARAM, "max packet size past 11 bits refused");
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     8UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     1UL, 0UL, 0x00301000UL, 1UL, NULL, NULL,
                                     NULL),
             XHCI_CTX_BAD_PARAM, "null params refused");
}

/*
 * **The two speeds, and what happens when they disagree** (tenth review,
 * finding 3).
 *
 * usbport buckets `Period` against the speed *it* believes, which decides the
 * unit; the Endpoint Context's `Interval` must be legal for the speed the
 * **Slot Context** carries, which Table 6-12 p.420 fixes at 3-10 for an FS/LS
 * interrupt endpoint and 0-15 for HS. The two differ systematically today
 * because Phase 5 task 7 reports every connected root port to usbport as High
 * Speed - and passing one speed for both produced an Endpoint Context a
 * conforming xHC may answer with Parameter Error.
 */
static void test_interval_for_speed(void)
{
    XHCI_EP_PARAMS ep;
    ULONG out;
    ULONG floored;

    /* The range itself, at both ends, for each speed. */
    CHECK_EQ(XhciIntervalForSpeed(3UL, XHCI_SPEED_FULL, &out, &floored),
             XHCI_CTX_OK, "FS interval 3 accepted");
    CHECK_EQ(out, 3UL, "unchanged");
    CHECK_EQ(floored, 0UL, "and not floored");
    CHECK_EQ(XhciIntervalForSpeed(10UL, XHCI_SPEED_LOW, &out, &floored),
             XHCI_CTX_OK, "LS interval 10 accepted - the top of the range");
    CHECK_EQ(XhciIntervalForSpeed(11UL, XHCI_SPEED_LOW, &out, &floored),
             XHCI_CTX_BAD_PARAM, "LS interval 11 refused - above Table 6-12");
    CHECK_EQ(XhciIntervalForSpeed(0UL, XHCI_SPEED_HIGH, &out, &floored),
             XHCI_CTX_OK, "HS interval 0 is legal - 125 us");
    CHECK_EQ(floored, 0UL, "and never floored at High Speed");
    CHECK_EQ(XhciIntervalForSpeed(15UL, XHCI_SPEED_HIGH, &out, &floored),
             XHCI_CTX_OK, "HS interval 15 accepted");
    CHECK_EQ(XhciIntervalForSpeed(16UL, XHCI_SPEED_HIGH, &out, &floored),
             XHCI_CTX_BAD_PARAM, "HS interval 16 refused");
    CHECK_EQ(XhciIntervalForSpeed(3UL, XHCI_SPEED_UNKNOWN, &out, &floored),
             XHCI_CTX_BAD_PARAM, "a speed this driver does not address refused");

    /* Below the floor at FS/LS: raised, and said so. Every value below 3, not
     * just one - the interior of a range needs testing. */
    CHECK_EQ(XhciIntervalForSpeed(0UL, XHCI_SPEED_FULL, &out, &floored),
             XHCI_CTX_OK, "FS interval 0 accepted");
    CHECK_EQ(out, 3UL, "raised to the 1 ms floor Table 6-12 gives FS");
    CHECK_EQ(floored, 1UL, "and reported as floored");
    CHECK_EQ(XhciIntervalForSpeed(1UL, XHCI_SPEED_FULL, &out, &floored),
             XHCI_CTX_OK, "FS interval 1");
    CHECK_EQ(out, 3UL, "also raised");
    CHECK_EQ(floored, 1UL, "also reported");
    CHECK_EQ(XhciIntervalForSpeed(2UL, XHCI_SPEED_LOW, &out, &floored),
             XHCI_CTX_OK, "LS interval 2");
    CHECK_EQ(out, 3UL, "also raised");
    CHECK_EQ(floored, 1UL, "also reported");
    /* `floored` is optional, because most callers only need the value. */
    CHECK_EQ(XhciIntervalForSpeed(0UL, XHCI_SPEED_FULL, &out, NULL),
             XHCI_CTX_OK, "a NULL floored pointer is allowed");
    CHECK_EQ(XhciIntervalForSpeed(3UL, XHCI_SPEED_FULL, NULL, &floored),
             XHCI_CTX_BAD_PARAM, "a NULL output is not");

    /*
     * And end to end through the builder, which is where the two speeds actually
     * arrive separately. **This is the exact shape of the bug**: a Full-Speed
     * HID endpoint with `bInterval = 1`, which usbport - believing High Speed -
     * buckets to `Period = 1` microframe.
     */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     1UL, XHCI_SPEED_HIGH, XHCI_SPEED_FULL,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep,
                                     &floored, NULL),
             XHCI_CTX_OK, "an FS endpoint bucketed as HS is accepted");
    CHECK_EQ(ep.Interval, 3UL,
             "with an Interval legal for the speed the Slot Context carries");
    CHECK_EQ(floored, 1UL, "and the translation reported");

    /* The same numbers with the speeds agreeing are untouched, so the floor is
     * about the disagreement and not a blanket clamp. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     1UL, XHCI_SPEED_HIGH, XHCI_SPEED_HIGH,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep,
                                     &floored, NULL),
             XHCI_CTX_OK, "a genuine HS endpoint at Period 1");
    CHECK_EQ(ep.Interval, 0UL, "keeps its 125 us interval");
    CHECK_EQ(floored, 0UL, "and is not floored");

    /* A Period that already clears the floor is not touched either. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL, 8UL,
                                     32UL, XHCI_SPEED_HIGH, XHCI_SPEED_FULL,
                                     1UL, 0UL, 0x00301000UL, 1UL, &ep,
                                     &floored, NULL),
             XHCI_CTX_OK, "an FS endpoint at Period 32 microframes");
    CHECK_EQ(ep.Interval, 5UL, "keeps its 4 ms interval");
    CHECK_EQ(floored, 0UL, "and is not floored");

    /*
     * **Max Burst follows the *device* speed**, which is the opposite of the
     * interval's unit and is the asymmetry the eleventh review caught an earlier
     * vector pinning backwards. Spec 6.2.3.4 p.418: "For all Low-/Full-Speed
     * endpoints this field shall be cleared to '0'." So a Full-Speed endpoint
     * arriving with a transaction count above one is refused, not given a burst
     * - whatever usbport believed its speed to be.
     */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL,
                                     64UL, 8UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_FULL, 2UL, 0UL, 0x00301000UL,
                                     1UL, &ep, &floored, NULL),
             XHCI_CTX_BAD_PARAM,
             "a Full-Speed endpoint may not burst, whatever usbport read");
    /* Three as well as two: the refusal is "not one", and a guard written as
     * `== 2` would let a malformed three-transaction descriptor through. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL,
                                     64UL, 8UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_LOW, 3UL, 0UL, 0x00301000UL,
                                     1UL, &ep, &floored, NULL),
             XHCI_CTX_BAD_PARAM, "and neither may a Low-Speed one at three");
    /* The same count on a genuinely High-Speed endpoint is the high-bandwidth
     * case and still works, so the refusal is about the speed. */
    CHECK_EQ(XhciBuildEndpointParams(USBPORT_TRANSFER_TYPE_INTERRUPT, 1UL,
                                     64UL, 8UL, XHCI_SPEED_HIGH,
                                     XHCI_SPEED_HIGH, 2UL, 0UL, 0x00301000UL,
                                     1UL, &ep, &floored, NULL),
             XHCI_CTX_OK, "a 2-transaction High-Speed endpoint");
    CHECK_EQ(ep.MaxBurstSize, 1UL, "keeps its additional transaction");
    CHECK_EQ(ep.MaxEsitPayload, 128UL, "and its Max ESIT Payload");

    /* Full Speed at the top of its range, which only Low Speed had covered. */
    CHECK_EQ(XhciIntervalForSpeed(11UL, XHCI_SPEED_FULL, &out, &floored),
             XHCI_CTX_BAD_PARAM, "FS interval 11 refused as well as LS");
    CHECK_EQ(XhciIntervalForSpeed(10UL, XHCI_SPEED_FULL, &out, &floored),
             XHCI_CTX_OK, "and 10 accepted at both");
}

int main(void)
{
    test_slot_context_by_speed();
    test_slot_context_hub_fields();
    test_slot_context_refusals();
    test_ep0_context_by_speed();
    test_ep0_params_refusals();
    test_endpoint_context_refusals();
    test_input_control_context();
    test_both_strides();
    test_interval_from_period();
    test_interval_refusals();
    test_dci_from_address();
    test_endpoint_params();
    test_endpoint_params_high_bandwidth();
    test_endpoint_params_refusals();
    test_interval_for_speed();

    printf("test_ctx: %d checks, %d failures\n", checks, failures);
    return failures;
}
