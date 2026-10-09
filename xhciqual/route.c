/*
 * route.c - the Intel PCH interrupt routing report (roadmap-hcd.md task
 * 36.11; docs/issues/12-e7250-xp64-boot-enable-slot-refused.md section 5.1).
 *
 * On the 7-, 8- and 9-series PCH and on Wellsburg, the xHCI's INTx pin
 * reaches an interrupt input through registers in the RCBA space the LPC
 * bridge's F0h names: D20IP (RCBA+3128h) says which pin the xHCI drives,
 * D20IR (RCBA+3160h) routes each pin to PIRQA#-H#, and the LPC's own
 * PIRQx_ROUT bytes (60h-63h, 68h-6Bh) send each PIRQ to an 8259 input in
 * PIC mode. In APIC mode PIRQA#-H# are I/O APIC inputs 16-23. The Dell
 * Latitude E7250's BIOS routed INTA to PIRQA while its ACPI tables told
 * Windows the xHCI was on GSI 21, so the ISR Windows connected never ran.
 *
 * This half is pure: it decides what is in scope and words what the reader
 * (pci.c, route_read) found, and touches no register. The reviewed
 * xHCI/LPC pairs are the driver's own table (src/xhci_pirq.c), so the
 * report and the driver's quirk cannot disagree about which chipsets carry
 * these registers. Host-tested by xhciqual/test/test_route.c.
 */

#include <string.h>
#include "qual.h"
#include "../src/xhci_pirq.h"

#define ROUTE_APIC_FIRST 16L
#define ROUTE_APIC_LAST  23L

int route_scope(u32 xhci_id, u8 bus, u8 dev, u8 fn)
{
    if (!XhciPirqReviewed((ULONG)xhci_id))
        return ROUTE_NA_XHCI;
    /* The registers are device 20's: D20IP and D20IR name no other
     * function, so a reviewed id anywhere else is not what they describe. */
    if (bus != 0 || dev != (u8)XHCI_PIRQ_XHCI_DEVICE || fn != 0)
        return ROUTE_NA_SLOT;
    return ROUTE_READ;
}

int route_lpc_listed(u32 xhci_id, u32 lpc_id)
{
    return XhciPirqPairListed((ULONG)xhci_id, (ULONG)lpc_id) ? 1 : 0;
}

int route_rcba_valid(u32 rcba)
{
    u32 base;

    base = rcba & XHCI_PIRQ_RCBA_BASE;
    return (rcba & XHCI_PIRQ_RCBA_ENABLE) != 0 && base != 0 &&
           base != XHCI_PIRQ_RCBA_BASE;
}

/* D20IP's pin: 1-4 for INTA-INTD, 0 for none or a value outside them. */
static int route_pin(const ROUTEINFO *r)
{
    u32 pin;

    pin = r->d20ip & XHCI_PIRQ_D20IP_PIN;
    return (pin >= 1 && pin <= 4) ? (int)pin : 0;
}

int route_pirq(const ROUTEINFO *r)
{
    int pin;

    if (r->state != ROUTE_READ)
        return -1;
    pin = route_pin(r);
    if (pin == 0)
        return -1;
    /* D20IR: INTA's PIRQ in 2:0, INTB's in 6:4, INTC's 10:8, INTD's 14:12. */
    return (int)((r->d20ir >> ((pin - 1) * 4)) & 7);
}

/* PIRQx_ROUT for PIRQ n (0-7). */
static u8 route_rout(const ROUTEINFO *r, int n)
{
    if (n < 4)
        return (u8)((r->pirq_ad >> (n * 8)) & 0xFF);
    return (u8)((r->pirq_eh >> ((n - 4) * 8)) & 0xFF);
}

int route_compare(const ROUTEINFO *r, long gsi)
{
    int pirq;
    u8 rout;

    if (gsi < 0)
        return ROUTE_CMP_NONE;
    pirq = route_pirq(r);
    if (pirq < 0)
        return ROUTE_CMP_NOT_COMPARED;
    if (gsi >= ROUTE_APIC_FIRST && gsi <= ROUTE_APIC_LAST)
        return ((long)pirq == gsi - ROUTE_APIC_FIRST) ? ROUTE_CMP_MATCH
                                                      : ROUTE_CMP_MISMATCH;
    if (gsi < ROUTE_APIC_FIRST) {
        /* An IRQ below 16 is an 8259 input: the PIRQ reaches it only
         * when its PIRQx_ROUT is enabled and names it. */
        if (!XhciPirqPicIrq((ULONG)gsi))
            return ROUTE_CMP_NOT_COMPARED;
        rout = route_rout(r, pirq);
        return ((rout & XHCI_PIRQ_ROUT_IRQEN) == 0 &&
                (long)(rout & XHCI_PIRQ_ROUT_IRQ) == gsi)
                   ? ROUTE_CMP_MATCH
                   : ROUTE_CMP_MISMATCH;
    }
    return ROUTE_CMP_NOT_COMPARED;
}

const char *route_summary(const ROUTEINFO *r, long gsi)
{
    switch (r->state) {
    case ROUTE_NOT_READ:
        return "routing not read";
    case ROUTE_NA_XHCI:
    case ROUTE_NA_SLOT:
    case ROUTE_NA_LPC:
        return "routing registers not applicable";
    case ROUTE_RCBA_INVALID:
        return "routing: RCBA disabled or invalid";
    case ROUTE_MAP_FAILED:
        return "routing: RCBA page not mapped";
    default:
        break;
    }
    switch (route_compare(r, gsi)) {
    case ROUTE_CMP_MATCH:
        return "routing MATCHES the GSI given";
    case ROUTE_CMP_MISMATCH:
        return "routing MISMATCH against the GSI given";
    case ROUTE_CMP_NOT_COMPARED:
        return "routing read; the GSI given was not compared";
    default:
        return "routing read";
    }
}

static char pirq_letter(int n)
{
    return (char)('A' + n);
}

static void report_rout(const ROUTEINFO *r, int n, QPRINTF_FN pf)
{
    u8 rout;
    u32 irq;

    rout = route_rout(r, n);
    irq = rout & XHCI_PIRQ_ROUT_IRQ;
    if ((rout & XHCI_PIRQ_ROUT_IRQEN) != 0) {
        pf("    PIRQ%c_ROUT=%02X: IRQEN set, not routed to the PIC "
           "(IRQ field %lu)\n", pirq_letter(n), rout, irq);
    } else if (!XhciPirqPicIrq(irq)) {
        pf("    PIRQ%c_ROUT=%02X: routed, IRQ field %lu is a reserved "
           "encoding\n", pirq_letter(n), rout, irq);
    } else {
        pf("    PIRQ%c_ROUT=%02X: routed to the PIC, IRQ %lu\n",
           pirq_letter(n), rout, irq);
    }
}

void report_route(const ROUTEINFO *r, long gsi, QPRINTF_FN pf)
{
    int pin;
    int pirq;
    int cmp;

    switch (r->state) {
    case ROUTE_NOT_READ:
        return;
    case ROUTE_NA_XHCI:
        pf("  Routing: registers not applicable - not a reviewed Intel "
           "7/8/9-series or C610 xHCI; nothing read\n");
        return;
    case ROUTE_NA_SLOT:
        pf("  Routing: registers not applicable - a reviewed xHCI id, but "
           "not at 00:14.0; nothing read\n");
        return;
    default:
        break;
    }
    pf("  Routing (Intel PCH, read only): LPC 00:1F.0 %04lX:%04lX\n",
       r->lpc_id & 0xFFFFUL, (r->lpc_id >> 16) & 0xFFFFUL);
    if (r->state == ROUTE_NA_LPC) {
        pf("    not this xHCI's reviewed LPC: registers not applicable, "
           "nothing more read\n");
        return;
    }
    pf("    RCBA (F0h)=%08lX: base %08lX, %s\n", r->rcba,
       r->rcba & XHCI_PIRQ_RCBA_BASE,
       (r->rcba & XHCI_PIRQ_RCBA_ENABLE) ? "enabled" : "disabled");
    pf("    PIRQA-D_ROUT (60h)=%08lX  PIRQE-H_ROUT (68h)=%08lX\n",
       r->pirq_ad, r->pirq_eh);
    if (r->state == ROUTE_RCBA_INVALID) {
        pf("    RCBA disabled or invalid: D20IP and D20IR not read\n");
        return;
    }
    if (r->state == ROUTE_MAP_FAILED) {
        pf("    the RCBA page could not be mapped: D20IP and D20IR not "
           "read\n");
        return;
    }

    pin = route_pin(r);
    pf("    D20IP (RCBA+3128h)=%08lX: xHCI pin %s%c  (its own 3Dh: %s%c%s)\n",
       r->d20ip, pin ? "INT" : "none", pin ? (char)('A' + pin - 1) : ' ',
       (r->xhci_pin >= 1 && r->xhci_pin <= 4) ? "INT" : "none",
       (r->xhci_pin >= 1 && r->xhci_pin <= 4)
           ? (char)('A' + r->xhci_pin - 1) : ' ',
       ((u32)pin == (u32)r->xhci_pin) ? ", agrees" : ", DIFFERS");
    pf("    D20IR (RCBA+3160h)=%04X\n", r->d20ir);
    pirq = route_pirq(r);
    if (pirq < 0) {
        pf("    no pin, so no PIRQ selected\n");
    } else {
        pf("    INT%c -> PIRQ%c (GSI %d in APIC mode)\n",
           (char)('A' + pin - 1), pirq_letter(pirq), 16 + pirq);
        report_rout(r, pirq, pf);
    }

    cmp = route_compare(r, gsi);
    if (cmp == ROUTE_CMP_NONE) {
        pf("    no GSI given (--gsi N): nothing compared\n");
    } else if (cmp == ROUTE_CMP_MATCH) {
        pf("    GSI %ld given: MATCH - PIRQ%c delivers it\n", gsi,
           pirq_letter(pirq));
    } else if (cmp == ROUTE_CMP_MISMATCH) {
        if (gsi >= ROUTE_APIC_FIRST) {
            pf("    GSI %ld given: MISMATCH - the pin reaches PIRQ%c "
               "(GSI %d), and GSI %ld is PIRQ%c\n", gsi, pirq_letter(pirq),
               16 + pirq, gsi, pirq_letter((int)(gsi - ROUTE_APIC_FIRST)));
        } else {
            pf("    GSI %ld given: MISMATCH - PIRQ%c's PIRQx_ROUT does "
               "not route it to IRQ %ld\n", gsi, pirq_letter(pirq), gsi);
        }
    } else {
        pf("    GSI %ld given: not compared (%s)\n", gsi,
           pirq < 0 ? "no PIRQ selected"
                    : "neither an APIC input 16-23 nor a routable 8259 IRQ");
    }
}
