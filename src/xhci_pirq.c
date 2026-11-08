/*
 * xhci_pirq.c - the Intel PCH interrupt routing quirk, the pure half
 * (xhci_pirq.h; roadmap-hcd.md task 36.3).
 *
 * DDK-free: part of the pure core. C89. IRQL: any for the functions that
 * take no XHCI_PIRQ_IO; XhciPirqRun runs at its callbacks' IRQL, which with
 * hcd_ctl.c's is PASSIVE_LEVEL under no spin lock.
 */

#include "xhci_pirq.h"

#define XHCI_PIRQ_VENDOR_INTEL      0x8086UL
#define XHCI_PIRQ_E7250_XHCI        0x9CB18086UL
#define XHCI_PIRQ_E7250_SUBSYSTEM   0x062D1028UL
#define XHCI_PIRQ_E7250_LPC         0x9CC38086UL
#define XHCI_PIRQ_APIC_FIRST        16UL
#define XHCI_PIRQ_APIC_LAST         23UL
#define XHCI_PIRQ_COUNT             8UL

/*
 * The reviewed pairs (issue 12 section 5.1), each LPC id from its PCH
 * datasheet's device-id table, never a range: engineering samples, ids
 * from coreboot alone and the 7 series' unlisted range members are left
 * out. Each datasheet places RCBA at the LPC's F0h, D20IP at RCBA+3128h and
 * D20IR at RCBA+3160h; 9CC3 (the U-line LPC, absent from 330837-002's
 * Y-line rows) was read on the E7250 itself.
 */
static const USHORT xhciPirqLpc1E31[] = {
    0x1E44, 0x1E46, 0x1E47, 0x1E48, 0x1E49, 0x1E4A, 0x1E53, 0x1E55,
    0x1E56, 0x1E57, 0x1E58, 0x1E59, 0x1E5B
};
static const USHORT xhciPirqLpc8C31[] = {
    0x8C44, 0x8C46, 0x8C49, 0x8C4A, 0x8C4B, 0x8C4C, 0x8C4E, 0x8C4F,
    0x8C50, 0x8C52, 0x8C54, 0x8C56, 0x8C5C
};
static const USHORT xhciPirqLpc9C31[] = { 0x9C43, 0x9C45 };
static const USHORT xhciPirqLpc8CB1[] = { 0x8CC3, 0x8CC4, 0x8CC6 };
static const USHORT xhciPirqLpc9CB1[] = { 0x9CC3 };
static const USHORT xhciPirqLpc8D31[] = { 0x8D44, 0x8D47 };

typedef struct _XHCI_PIRQ_PAIR {
    USHORT Xhci;
    USHORT LpcCount;
    const USHORT *Lpc;
} XHCI_PIRQ_PAIR;

#define XHCI_PIRQ_N(a) ((USHORT)(sizeof(a) / sizeof((a)[0])))

static const XHCI_PIRQ_PAIR xhciPirqPairs[] = {
    { 0x1E31, XHCI_PIRQ_N(xhciPirqLpc1E31), xhciPirqLpc1E31 },
    { 0x8C31, XHCI_PIRQ_N(xhciPirqLpc8C31), xhciPirqLpc8C31 },
    { 0x9C31, XHCI_PIRQ_N(xhciPirqLpc9C31), xhciPirqLpc9C31 },
    { 0x8CB1, XHCI_PIRQ_N(xhciPirqLpc8CB1), xhciPirqLpc8CB1 },
    { 0x9CB1, XHCI_PIRQ_N(xhciPirqLpc9CB1), xhciPirqLpc9CB1 },
    { 0x8D31, XHCI_PIRQ_N(xhciPirqLpc8D31), xhciPirqLpc8D31 }
};

static const XHCI_PIRQ_PAIR *xhciPirqPair(ULONG xhciId)
{
    ULONG device;
    ULONG i;

    if ((xhciId & 0xFFFFUL) != XHCI_PIRQ_VENDOR_INTEL) {
        return NULL;
    }
    device = (xhciId >> 16) & 0xFFFFUL;
    for (i = 0; i < sizeof(xhciPirqPairs) / sizeof(xhciPirqPairs[0]); i++) {
        if (device == (ULONG)xhciPirqPairs[i].Xhci) {
            return &xhciPirqPairs[i];
        }
    }
    return NULL;
}

ULONG XhciPirqMode(ULONG found, ULONG value)
{
    if (found && value <= XHCI_PIRQ_MODE_REVIEWED) {
        return value;
    }
    return XHCI_PIRQ_MODE_VERIFIED;
}

ULONG XhciPirqReviewed(ULONG xhciId)
{
    return xhciPirqPair(xhciId) != NULL ? 1UL : 0UL;
}

ULONG XhciPirqPairListed(ULONG xhciId, ULONG lpcId)
{
    const XHCI_PIRQ_PAIR *pair;
    ULONG device;
    ULONG i;

    pair = xhciPirqPair(xhciId);
    if (pair == NULL || (lpcId & 0xFFFFUL) != XHCI_PIRQ_VENDOR_INTEL) {
        return 0;
    }
    device = (lpcId >> 16) & 0xFFFFUL;
    for (i = 0; i < (ULONG)pair->LpcCount; i++) {
        if (device == (ULONG)pair->Lpc[i]) {
            return 1;
        }
    }
    return 0;
}

/* Keyed on the subsystem because the fault is this firmware's, not the
 * silicon's. */
ULONG XhciPirqVerified(ULONG xhciId, ULONG subsystem, ULONG lpcId)
{
    return (xhciId == XHCI_PIRQ_E7250_XHCI &&
            subsystem == XHCI_PIRQ_E7250_SUBSYSTEM &&
            lpcId == XHCI_PIRQ_E7250_LPC) ? 1UL : 0UL;
}

/* PIRQx_ROUT's IRQ field reserves 0, 1, 2, 8 and 13 (Intel 330837-002
 * 10.1.17, and each pair's datasheet); they name no routable interrupt. */
ULONG XhciPirqPicIrq(ULONG irq)
{
    return ((irq >= 3 && irq <= 7) || (irq >= 9 && irq <= 12) ||
            irq == 14 || irq == 15) ? 1UL : 0UL;
}

ULONG XhciPirqTarget(ULONG irq, ULONG pirqAd, ULONG pirqEh, ULONG current,
                     PULONG irqMode, PULONG target)
{
    ULONG lowest;
    ULONG route;
    ULONG n;

    *irqMode = XHCI_PIRQ_IRQ_NONE;
    *target = XHCI_PIRQ_UNREAD;
    if (irq >= XHCI_PIRQ_APIC_FIRST && irq <= XHCI_PIRQ_APIC_LAST) {
        *irqMode = XHCI_PIRQ_IRQ_APIC;
        *target = irq - XHCI_PIRQ_APIC_FIRST;
        return XHCI_PIRQ_D_NONE;
    }
    if (!XhciPirqPicIrq(irq)) {
        return XHCI_PIRQ_D_IRQ_UNROUTABLE;
    }

    /* Two stages here: IAR picks the PIRQ, PIRQx_ROUT sends the PIRQ to an
     * 8259 input, as the OS programmed it for the PIRQ it believes the
     * xHCI uses. Several PIRQs routed to one IRQ share it, so any of them
     * delivers it, and the current one is kept rather than moved. */
    *irqMode = XHCI_PIRQ_IRQ_PIC;
    lowest = XHCI_PIRQ_UNREAD;
    for (n = 0; n < XHCI_PIRQ_COUNT; n++) {
        route = (n < 4) ? (pirqAd >> (n * 8)) : (pirqEh >> ((n - 4) * 8));
        route &= 0xFFUL;
        if ((route & XHCI_PIRQ_ROUT_IRQEN) != 0 ||
            (route & XHCI_PIRQ_ROUT_IRQ) != irq) {
            continue;
        }
        if (n == current) {
            *target = n;
            return XHCI_PIRQ_D_NONE;
        }
        if (lowest == XHCI_PIRQ_UNREAD) {
            lowest = n;
        }
    }
    if (lowest == XHCI_PIRQ_UNREAD) {
        return XHCI_PIRQ_D_PIC_NO_PIRQ;
    }
    *target = lowest;
    return XHCI_PIRQ_D_NONE;
}

static VOID xhciPirqClear(PXHCI_PIRQ_STATE st)
{
    st->When = XHCI_PIRQ_WHEN_NONE;
    st->Delivery = XHCI_PIRQ_DELIVERY_NONE;
    st->Decision = XHCI_PIRQ_D_NONE;
    st->XhciId = XHCI_PIRQ_UNREAD;
    st->XhciSubsystem = XHCI_PIRQ_UNREAD;
    st->XhciPin = XHCI_PIRQ_UNREAD;
    st->SlotId = XHCI_PIRQ_UNREAD;
    st->LpcId = XHCI_PIRQ_UNREAD;
    st->Rcba = XHCI_PIRQ_UNREAD;
    st->PirqAd = XHCI_PIRQ_UNREAD;
    st->PirqEh = XHCI_PIRQ_UNREAD;
    st->IrqCount = 0;
    st->IrqLevel = XHCI_PIRQ_UNREAD;
    st->IrqVector = XHCI_PIRQ_UNREAD;
    st->IrqFlags = XHCI_PIRQ_UNREAD;
    st->IrqShare = XHCI_PIRQ_UNREAD;
    st->IrqMode = XHCI_PIRQ_IRQ_NONE;
    st->D20ip = XHCI_PIRQ_UNREAD;
    st->D20ir = XHCI_PIRQ_UNREAD;
    st->Target = XHCI_PIRQ_UNREAD;
    st->D20irAfter = XHCI_PIRQ_UNREAD;
}

VOID XhciPirqReset(PXHCI_PIRQ_STATE st)
{
    if (st == NULL) {
        return;
    }
    xhciPirqClear(st);
    st->ValueFound = 0;
    st->Value = XHCI_PIRQ_UNREAD;
    st->Mode = XHCI_PIRQ_UNREAD;
}

VOID XhciPirqLatch(PXHCI_PIRQ_STATE st, ULONG found, ULONG value)
{
    if (st == NULL) {
        return;
    }
    xhciPirqClear(st);
    st->ValueFound = found ? 1UL : 0UL;
    st->Value = found ? value : XHCI_PIRQ_UNREAD;
    st->Mode = XhciPirqMode(found, value);
}

static ULONG xhciPirqLe32(const UCHAR *b, ULONG offset)
{
    return (ULONG)b[offset] | ((ULONG)b[offset + 1] << 8) |
           ((ULONG)b[offset + 2] << 16) | ((ULONG)b[offset + 3] << 24);
}

/* The xHCI's own header dwords the location check compares. */
static const ULONG xhciPirqSame[] = {
    XHCI_PIRQ_CFG_ID, XHCI_PIRQ_CFG_BAR0, XHCI_PIRQ_CFG_BAR1,
    XHCI_PIRQ_CFG_SUBSYSTEM
};

/* The registers past the LPC bridge, through the mapped page: D20IP, then
 * D20IR, then the target and the write. Ends with the decision; the page
 * is the caller's to unmap. */
static ULONG xhciPirqRoute(const XHCI_PIRQ_IO *io, ULONG delivery,
                           const XHCI_PIRQ_IRQ *irq, PXHCI_PIRQ_STATE st)
{
    USHORT ir;
    USHORT after;
    ULONG decision;
    ULONG target;

    if (!io->Read32(io->Context, XHCI_PIRQ_D20IP, &st->D20ip)) {
        st->D20ip = XHCI_PIRQ_UNREAD;
    }
    /* Agreement on another pin is not enough: the repair writes only
     * INTA's field. */
    if ((st->D20ip & XHCI_PIRQ_D20IP_PIN) != XHCI_PIRQ_PIN_INTA ||
        st->XhciPin != XHCI_PIRQ_PIN_INTA) {
        return XHCI_PIRQ_D_PIN;
    }
    ir = 0xFFFFU;
    if (!io->Read16(io->Context, XHCI_PIRQ_D20IR, &ir) || ir == 0xFFFFU) {
        return XHCI_PIRQ_D_D20IR_UNREAD;
    }
    st->D20ir = (ULONG)ir;

    if (delivery == XHCI_PIRQ_DELIVERY_MESSAGE) {
        return XHCI_PIRQ_D_MESSAGE;
    }
    if (delivery == XHCI_PIRQ_DELIVERY_POLL) {
        return XHCI_PIRQ_D_POLL;
    }
    if (delivery != XHCI_PIRQ_DELIVERY_LINE) {
        return XHCI_PIRQ_D_NO_INTERRUPT;
    }
    if (irq->Count != 1 || irq->Level != irq->Vector) {
        return XHCI_PIRQ_D_IRQ_AMBIGUOUS;
    }
    decision = XhciPirqTarget(irq->Level, st->PirqAd, st->PirqEh,
                              st->D20ir & XHCI_PIRQ_IAR, &st->IrqMode,
                              &st->Target);
    if (decision != XHCI_PIRQ_D_NONE) {
        return decision;
    }
    target = st->Target;
    if ((st->D20ir & XHCI_PIRQ_IAR) == target) {
        return XHCI_PIRQ_D_ROUTED;
    }
    /* Below 16 the target is the 8259 path's; only 98 and ME are known
     * to use it (XHCI_PIRQ_IO.Steerable), whatever the value. */
    if (st->IrqMode == XHCI_PIRQ_IRQ_PIC && !io->Steerable) {
        return XHCI_PIRQ_D_STEERED_NT;
    }
    if (st->Mode == XHCI_PIRQ_MODE_OFF) {
        return XHCI_PIRQ_D_OFF;
    }
    if (st->Mode != XHCI_PIRQ_MODE_REVIEWED &&
        !XhciPirqVerified(st->XhciId, st->XhciSubsystem, st->LpcId)) {
        return XHCI_PIRQ_D_NOT_ADMITTED;
    }

    /* IAR alone: INTB-D's fields and the reserved bit 3 are written back
     * as read. */
    ir = (USHORT)((ir & (USHORT)~XHCI_PIRQ_IAR) | (USHORT)target);
    st->Writes++;
    after = 0xFFFFU;
    if (!io->Write16(io->Context, XHCI_PIRQ_D20IR, ir) ||
        !io->Read16(io->Context, XHCI_PIRQ_D20IR, &after)) {
        st->WriteFailures++;
        return XHCI_PIRQ_D_WRITE_FAILED;
    }
    st->D20irAfter = (ULONG)after;
    if (after == 0xFFFFU || (after & XHCI_PIRQ_IAR) != target) {
        st->WriteFailures++;
        return XHCI_PIRQ_D_WRITE_FAILED;
    }
    return XHCI_PIRQ_D_WROTE;
}

ULONG XhciPirqRun(const XHCI_PIRQ_IO *io, ULONG when, ULONG delivery,
                  const XHCI_PIRQ_IRQ *irq, PXHCI_PIRQ_STATE st)
{
    UCHAR cfg[XHCI_PIRQ_LPC_BYTES];
    ULONG own[sizeof(xhciPirqSame) / sizeof(xhciPirqSame[0])];
    ULONG interrupt;
    ULONG base;
    ULONG i;

    if (st == NULL) {
        return XHCI_PIRQ_D_BAD_PARAM;
    }
    xhciPirqClear(st);
    st->Runs++;
    st->When = when;
    st->Delivery = delivery;
    if (io == NULL || io->Own == NULL || io->Slot == NULL ||
        io->Map == NULL || io->Read32 == NULL || io->Read16 == NULL ||
        io->Write16 == NULL || io->Unmap == NULL || irq == NULL) {
        st->Decision = XHCI_PIRQ_D_BAD_PARAM;
        return st->Decision;
    }
    st->IrqCount = irq->Count;
    if (irq->Count != 0) {
        st->IrqLevel = irq->Level;
        st->IrqVector = irq->Vector;
        st->IrqFlags = irq->Flags;
        st->IrqShare = irq->Share;
    }

    for (i = 0; i < sizeof(own) / sizeof(own[0]); i++) {
        if (!io->Own(io->Context, xhciPirqSame[i], &own[i])) {
            st->Decision = XHCI_PIRQ_D_OWN_UNREAD;
            return st->Decision;
        }
    }
    interrupt = 0;
    if (!io->Own(io->Context, XHCI_PIRQ_CFG_INTERRUPT, &interrupt)) {
        st->Decision = XHCI_PIRQ_D_OWN_UNREAD;
        return st->Decision;
    }
    st->XhciId = own[0];
    st->XhciSubsystem = own[3];
    st->XhciPin = (interrupt >> 8) & 0xFFUL;
    /* Nothing past the xHCI's own space is read on any other controller:
     * the registers exist only on the reviewed chipsets. */
    if (!XhciPirqReviewed(st->XhciId)) {
        st->Decision = XHCI_PIRQ_D_NOT_REVIEWED;
        return st->Decision;
    }

    /* The function at 00:14.0 is this one only if its identity and BARs
     * are this one's. */
    for (i = 0; i < XHCI_PIRQ_SLOT_BYTES; i++) {
        cfg[i] = 0xFF;
    }
    if (io->Slot(io->Context, XHCI_PIRQ_XHCI_DEVICE, 0, cfg,
                 XHCI_PIRQ_SLOT_BYTES) < XHCI_PIRQ_SLOT_BYTES) {
        st->Decision = XHCI_PIRQ_D_SLOT_UNREAD;
        return st->Decision;
    }
    st->SlotId = xhciPirqLe32(cfg, XHCI_PIRQ_CFG_ID);
    for (i = 0; i < sizeof(own) / sizeof(own[0]); i++) {
        if (xhciPirqLe32(cfg, xhciPirqSame[i]) != own[i]) {
            st->Decision = XHCI_PIRQ_D_NOT_AT_SLOT;
            return st->Decision;
        }
    }

    for (i = 0; i < XHCI_PIRQ_LPC_BYTES; i++) {
        cfg[i] = 0xFF;
    }
    if (io->Slot(io->Context, XHCI_PIRQ_LPC_DEVICE, 0, cfg,
                 XHCI_PIRQ_LPC_BYTES) < XHCI_PIRQ_LPC_BYTES) {
        st->Decision = XHCI_PIRQ_D_LPC_UNREAD;
        return st->Decision;
    }
    st->LpcId = xhciPirqLe32(cfg, XHCI_PIRQ_CFG_ID);
    st->Rcba = xhciPirqLe32(cfg, XHCI_PIRQ_LPC_RCBA);
    st->PirqAd = xhciPirqLe32(cfg, XHCI_PIRQ_LPC_PIRQ_AD);
    st->PirqEh = xhciPirqLe32(cfg, XHCI_PIRQ_LPC_PIRQ_EH);
    if (!XhciPirqPairListed(st->XhciId, st->LpcId)) {
        st->Decision = XHCI_PIRQ_D_LPC_UNLISTED;
        return st->Decision;
    }
    base = st->Rcba & XHCI_PIRQ_RCBA_BASE;
    if ((st->Rcba & XHCI_PIRQ_RCBA_ENABLE) == 0 || base == 0 ||
        base == XHCI_PIRQ_RCBA_BASE) {
        st->Decision = XHCI_PIRQ_D_RCBA_INVALID;
        return st->Decision;
    }

    if (!io->Map(io->Context, base + XHCI_PIRQ_WINDOW)) {
        st->Decision = XHCI_PIRQ_D_MAP_FAILED;
        return st->Decision;
    }
    st->Decision = xhciPirqRoute(io, delivery, irq, st);
    io->Unmap(io->Context);
    return st->Decision;
}

/* ---- the chipset-routed GSI offer (task 47.33) ---------------------- */

/*
 * Value 1's allowlist (owner, 2026-11-06 and 2026-11-07): HP boards
 * (subsystem vendor 103C, 00:14.0's 2Ch SSID 31:16) on the 7-series xHCI
 * 1E31 whose firmware leaves the xHCI's decodes off and names no _PRT
 * route for it (issue 25). 179B (EliteBook 8470p/8470w) is the owner's
 * bench machine, read on 2026-11-07 under XP x64 (the grant of GSI 16,
 * line delivery, busy EHCI sharers, cold and warm boots, disable and
 * enable); 17A7 and 17AB (8570p; 17AB also names the ProBook 6570b) are
 * an inference within the same 68ICE/68ICF firmware family; 17DF (2570p,
 * 68ISB), 176C (8770w) and 176B (8570w, 68IAV like the 8770w) are the
 * owner's choice without a bench on their family. The run's own checks
 * still decide on each machine: a reviewed LPC (issue 12 section 5.1),
 * INTA, and a readable D20IR giving the PIRQ; and the list order lets the
 * PCI driver's own configuration win wherever its line can be granted.
 * A storm, a polarity mismatch or a sharer's regression turns the default
 * off again pending explanation.
 */
static const ULONG xhciPirqNoRouteBoards[] = {
    0x179BUL,   /* EliteBook 8470p, 8470w (68ICF)                 */
    0x17A7UL,   /* EliteBook 8570p (68ICF)                        */
    0x17ABUL,   /* EliteBook 8570p, ProBook 6570b (68ICE)         */
    0x17DFUL,   /* EliteBook 2570p (68ISB)                        */
    0x176CUL,   /* EliteBook 8770w (68IAV)                        */
    0x176BUL    /* EliteBook 8570w (68IAV)                        */
};

#define XHCI_PIRQ_NR_XHCI           0x1E318086UL    /* 7 series         */
#define XHCI_PIRQ_NR_VENDOR         0x103CUL        /* HP               */

ULONG XhciPirqNoRouteMode(ULONG found, ULONG value)
{
    if (found && value <= XHCI_PIRQ_NR_REVIEWED) {
        return value;
    }
    return XHCI_PIRQ_NR_LISTED;
}

ULONG XhciPirqNoRouteListed(ULONG xhciId, ULONG subsystem)
{
    ULONG i;

    if (xhciId != XHCI_PIRQ_NR_XHCI ||
        (subsystem & 0xFFFFUL) != XHCI_PIRQ_NR_VENDOR) {
        return 0;
    }
    for (i = 0; i < sizeof(xhciPirqNoRouteBoards) /
                        sizeof(xhciPirqNoRouteBoards[0]);
         i++) {
        if (((subsystem >> 16) & 0xFFFFUL) == xhciPirqNoRouteBoards[i]) {
            return 1;
        }
    }
    return 0;
}

/* The identity guards passed (the location, 00:14.0's own identity, a
 * reviewed controller, the list at value 1): every decision from the LPC's
 * read to the offer. A later check's failure (an unread or unlisted LPC,
 * RCBA, the window's mapping, the pin, D20IR) reads the route, not the
 * board, and must not bring the line back into a listed board's list. */
ULONG XhciPirqW2kStrip(ULONG isW2k, ULONG decision, ULONG listed)
{
    if (!isW2k || !listed) {
        return 0UL;
    }
    return (decision >= XHCI_PIRQ_O_LPC_UNREAD &&
            decision <= XHCI_PIRQ_O_OFFERED) ? 1UL : 0UL;
}

VOID XhciPirqOfferClear(PXHCI_PIRQ_OFFER st)
{
    if (st == NULL) {
        return;
    }
    st->Decision = XHCI_PIRQ_O_NONE;
    st->Mode = XHCI_PIRQ_UNREAD;
    st->XhciId = XHCI_PIRQ_UNREAD;
    st->Subsystem = XHCI_PIRQ_UNREAD;
    st->Pin = XHCI_PIRQ_UNREAD;
    st->LpcId = XHCI_PIRQ_UNREAD;
    st->Rcba = XHCI_PIRQ_UNREAD;
    st->D20ip = XHCI_PIRQ_UNREAD;
    st->D20ir = XHCI_PIRQ_UNREAD;
    st->Pirq = XHCI_PIRQ_UNREAD;
    st->Gsi = XHCI_PIRQ_NO_GSI;
}

/* The registers past the LPC bridge, read-only; the page is the caller's
 * to unmap. */
static ULONG xhciPirqOfferRoute(const XHCI_PIRQ_IO *io, PXHCI_PIRQ_OFFER st)
{
    USHORT ir;

    if (!io->Read32(io->Context, XHCI_PIRQ_D20IP, &st->D20ip)) {
        st->D20ip = XHCI_PIRQ_UNREAD;
    }
    if ((st->D20ip & XHCI_PIRQ_D20IP_PIN) != XHCI_PIRQ_PIN_INTA ||
        st->Pin != XHCI_PIRQ_PIN_INTA) {
        return XHCI_PIRQ_O_PIN;
    }
    ir = 0xFFFFU;
    if (!io->Read16(io->Context, XHCI_PIRQ_D20IR, &ir) || ir == 0xFFFFU) {
        return XHCI_PIRQ_O_D20IR_UNREAD;
    }
    st->D20ir = (ULONG)ir;
    st->Pirq = st->D20ir & XHCI_PIRQ_IAR;
    st->Gsi = XHCI_PIRQ_GSI_BASE + st->Pirq;
    return XHCI_PIRQ_O_OFFERED;
}

ULONG XhciPirqOffer(const XHCI_PIRQ_IO *io, ULONG bus, ULONG address,
                    ULONG ownId, ULONG ownSubsystem, ULONG mode,
                    PXHCI_PIRQ_OFFER st)
{
    UCHAR cfg[XHCI_PIRQ_LPC_BYTES];
    ULONG base;
    ULONG i;

    if (st == NULL) {
        return XHCI_PIRQ_O_BAD_PARAM;
    }
    XhciPirqOfferClear(st);
    st->Mode = mode;
    if (io == NULL || io->Slot == NULL || io->Map == NULL ||
        io->Read32 == NULL || io->Read16 == NULL || io->Unmap == NULL) {
        st->Decision = XHCI_PIRQ_O_BAD_PARAM;
        return st->Decision;
    }
    if (mode == XHCI_PIRQ_NR_OFF) {
        st->Decision = XHCI_PIRQ_O_OFF;
        return st->Decision;
    }
    /* The PDO's location: bus 0, device 20, function 0 is 00:14.0, the
     * only place the PCH's xHCI sits. */
    if (bus != 0 || address != (XHCI_PIRQ_XHCI_DEVICE << 16)) {
        st->Decision = XHCI_PIRQ_O_NOT_SLOT;
        return st->Decision;
    }
    for (i = 0; i < XHCI_PIRQ_LPC_BYTES; i++) {
        cfg[i] = 0xFF;
    }
    if (io->Slot(io->Context, XHCI_PIRQ_XHCI_DEVICE, 0, cfg,
                 XHCI_PIRQ_SLOT_BYTES) < XHCI_PIRQ_SLOT_BYTES) {
        st->Decision = XHCI_PIRQ_O_OWN_UNREAD;
        return st->Decision;
    }
    st->XhciId = xhciPirqLe32(cfg, XHCI_PIRQ_CFG_ID);
    st->Subsystem = xhciPirqLe32(cfg, XHCI_PIRQ_CFG_SUBSYSTEM);
    st->Pin = (xhciPirqLe32(cfg, XHCI_PIRQ_CFG_INTERRUPT) >> 8) & 0xFFUL;
    /* And the function there is this one, by its own space's identity. */
    if (st->XhciId != ownId || st->Subsystem != ownSubsystem) {
        st->Decision = XHCI_PIRQ_O_NOT_AT_SLOT;
        return st->Decision;
    }
    if (!XhciPirqReviewed(st->XhciId)) {
        st->Decision = XHCI_PIRQ_O_NOT_REVIEWED;
        return st->Decision;
    }
    if (mode != XHCI_PIRQ_NR_REVIEWED &&
        !XhciPirqNoRouteListed(st->XhciId, st->Subsystem)) {
        st->Decision = XHCI_PIRQ_O_NOT_ADMITTED;
        return st->Decision;
    }

    for (i = 0; i < XHCI_PIRQ_LPC_BYTES; i++) {
        cfg[i] = 0xFF;
    }
    if (io->Slot(io->Context, XHCI_PIRQ_LPC_DEVICE, 0, cfg,
                 XHCI_PIRQ_LPC_BYTES) < XHCI_PIRQ_LPC_BYTES) {
        st->Decision = XHCI_PIRQ_O_LPC_UNREAD;
        return st->Decision;
    }
    st->LpcId = xhciPirqLe32(cfg, XHCI_PIRQ_CFG_ID);
    st->Rcba = xhciPirqLe32(cfg, XHCI_PIRQ_LPC_RCBA);
    if (!XhciPirqPairListed(st->XhciId, st->LpcId)) {
        st->Decision = XHCI_PIRQ_O_LPC_UNLISTED;
        return st->Decision;
    }
    base = st->Rcba & XHCI_PIRQ_RCBA_BASE;
    if ((st->Rcba & XHCI_PIRQ_RCBA_ENABLE) == 0 || base == 0 ||
        base == XHCI_PIRQ_RCBA_BASE) {
        st->Decision = XHCI_PIRQ_O_RCBA_INVALID;
        return st->Decision;
    }
    if (!io->Map(io->Context, base + XHCI_PIRQ_WINDOW)) {
        st->Decision = XHCI_PIRQ_O_MAP_FAILED;
        return st->Decision;
    }
    st->Decision = xhciPirqOfferRoute(io, st);
    io->Unmap(io->Context);
    if (st->Decision != XHCI_PIRQ_O_OFFERED) {
        st->Gsi = XHCI_PIRQ_NO_GSI;
    }
    return st->Decision;
}
