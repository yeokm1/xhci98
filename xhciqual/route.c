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
 *
 * The second half (task 39-0.6, at the end of this file) parses the BIOS
 * PCI IRQ routing table, "$PIR", which is machine-wide and read once per
 * run, and names whether device 00:1C has an entry in it.
 */

#include <stdio.h>
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
        return gsi < ROUTE_APIC_FIRST
                   ? "routing MATCHES the IRQ given on the 8259 path, as "
                     "DOS found it"
                   : "routing MATCHES the GSI given";
    case ROUTE_CMP_MISMATCH:
        return gsi < ROUTE_APIC_FIRST
                   ? "routing MISMATCH against the IRQ given on the 8259 "
                     "path, as DOS found it"
                   : "routing MISMATCH against the GSI given";
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
    } else if (cmp == ROUTE_CMP_MATCH && gsi >= ROUTE_APIC_FIRST) {
        pf("    GSI %ld given: MATCH - PIRQ%c delivers it\n", gsi,
           pirq_letter(pirq));
    } else if (cmp == ROUTE_CMP_MISMATCH && gsi >= ROUTE_APIC_FIRST) {
        pf("    GSI %ld given: MISMATCH - the pin reaches PIRQ%c "
           "(GSI %d), and GSI %ld is PIRQ%c\n", gsi, pirq_letter(pirq),
           16 + pirq, gsi, pirq_letter((int)(gsi - ROUTE_APIC_FIRST)));
    } else if (cmp == ROUTE_CMP_MATCH || cmp == ROUTE_CMP_MISMATCH) {
        /* Below 16 the comparison is the 8259 path's alone, with the
         * PIRQx_ROUT bytes DOS found: Windows 98 and ME program their own,
         * and an NT I/O APIC HAL can hand out an IRQ below 16 whose route
         * is not this one (issue 12 section 5.1, STEERED_NT). */
        pf("    IRQ %ld given: %s on the 8259 path - PIRQ%c_ROUT %s IRQ "
           "%ld, as DOS found it\n", gsi,
           cmp == ROUTE_CMP_MATCH ? "MATCH" : "MISMATCH", pirq_letter(pirq),
           cmp == ROUTE_CMP_MATCH ? "routes it to" : "does not route it to",
           gsi);
        pf("    (Windows 98 and ME program PIRQx_ROUT themselves; under an "
           "NT APIC HAL a\n     low IRQ may take another route, so this "
           "proves nothing for one)\n");
    } else {
        pf("    GSI %ld given: not compared (%s)\n", gsi,
           pirq < 0 ? "no PIRQ selected"
                    : "neither an APIC input 16-23 nor a routable 8259 IRQ");
    }
}

/*
 * Task 39-0.6: the BIOS PCI IRQ routing table, "$PIR".
 *
 * Layout from Microsoft's public "PCI IRQ Routing Table Specification",
 * version 1.0 (formerly at microsoft.com/whdc/archive/pciirq.mspx). The
 * table sits on a 16-byte boundary in F0000h-FFFFFh. Header, 32 bytes:
 * 00h "$PIR"; 04h version (word, 0100h for 1.0); 06h table size in bytes
 * (word; above 32 and a multiple of 16); 08h the interrupt router's bus,
 * 09h its device/function; 0Ah the PCI-exclusive IRQ bitmap (word, bit n =
 * IRQ n); 0Ch the compatible interrupt router (vendor word, device word);
 * 10h miniport data (dword); 14h 11 reserved bytes; 1Fh a checksum that
 * makes every byte of the table sum to 0 mod 256. Then one 16-byte slot
 * entry per device: 00h bus, 01h device (bits 7:3; function bits 2:0 are
 * not a part of it), then for INTA-INTD in turn a link value byte (0 = not
 * connected; the rest are the router's own) and an IRQ bitmap word (bit n
 * = IRQ n may be used), at 02h, 05h, 08h and 0Bh; 0Eh the slot number
 * (0 for a device on the board); 0Fh reserved.
 *
 * Pure, like everything above: pir_scan is handed an image of the BIOS
 * area and pci.c's pir_read fills the 00:1C functions. Every offset is
 * checked against the area before it is read, the table is checked against
 * its own size before it is summed, and at most PIR_MAX_SLOTS entries are
 * copied. Host-tested by xhciqual/test/test_route.c.
 */

static u16 pir_rd16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

static u32 pir_rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

/* One candidate at `p`, `room` bytes from it to the end of the area. */
static int pir_check(const u8 *p, u32 room, u16 *version, u16 *size)
{
    u32 i;
    u8 sum;

    *version = 0;
    *size = 0;
    if (room < PIR_HDR_LEN)
        return PIR_TRUNCATED;
    *version = pir_rd16(p + 4);
    *size = pir_rd16(p + 6);
    if ((*version >> 8) != 1)
        return PIR_BAD_VERSION;
    if (*size <= PIR_HDR_LEN || (*size % PIR_SLOT_LEN) != 0)
        return PIR_BAD_SIZE;
    if ((u32)*size > room)
        return PIR_TRUNCATED;
    sum = 0;
    for (i = 0; i < (u32)*size; i++)
        sum = (u8)(sum + p[i]);
    return sum == 0 ? PIR_OK : PIR_BAD_CHECKSUM;
}

int pir_scan(const u8 *area, u32 len, u32 area_phys, PIRINFO *pi)
{
    u32 off, copy;
    int st, first_st;
    u16 ver, size, first_ver, first_size;
    u32 first_off, keep_off;

    pi->state = PIR_NONE;
    pi->phys = 0;
    pi->version = 0;
    pi->size = 0;
    pi->nslots = 0;
    pi->kept = 0;
    pi->candidates = 0;
    first_st = PIR_NONE;
    first_ver = first_size = 0;
    first_off = 0;
    keep_off = 0;
    st = PIR_NONE;
    for (off = 0; len >= 4 && off <= len - 4; off += 16) {
        if (memcmp(area + off, "$PIR", 4) != 0)
            continue;
        pi->candidates++;
        st = pir_check(area + off, len - off, &ver, &size);
        if (st == PIR_OK)
            break;
        if (first_st == PIR_NONE) {
            first_st = st;
            first_ver = ver;
            first_size = size;
            first_off = off;
        }
    }
    if (st == PIR_OK) {
        keep_off = off;
    } else if (first_st != PIR_NONE) {
        st = first_st;
        ver = first_ver;
        size = first_size;
        keep_off = first_off;
    } else {
        return PIR_NONE;
    }

    pi->state = st;
    pi->phys = area_phys + keep_off;
    pi->version = ver;
    pi->size = size;
    if (st != PIR_OK && st != PIR_BAD_CHECKSUM)
        return st;
    /* The size passed every bound: copy the header and what fits. */
    pi->nslots = ((u32)size - PIR_HDR_LEN) / PIR_SLOT_LEN;
    pi->kept = pi->nslots > PIR_MAX_SLOTS ? PIR_MAX_SLOTS : pi->nslots;
    copy = PIR_HDR_LEN + pi->kept * PIR_SLOT_LEN;
    memcpy(pi->table, area + keep_off, copy);
    return st;
}

static int pir_parsed(const PIRINFO *pi)
{
    return pi->state == PIR_OK || pi->state == PIR_BAD_CHECKSUM;
}

static const u8 *pir_slot(const PIRINFO *pi, u32 i)
{
    return pi->table + PIR_HDR_LEN + i * PIR_SLOT_LEN;
}

int pir_find(const PIRINFO *pi, u8 bus, u8 dev, int *count)
{
    u32 i;
    int first, n;
    const u8 *s;

    first = -1;
    n = 0;
    if (pir_parsed(pi)) {
        for (i = 0; i < pi->kept; i++) {
            s = pir_slot(pi, i);
            if (s[0] == bus && (u8)(s[1] >> 3) == dev) {
                if (first < 0)
                    first = (int)i;
                n++;
            }
        }
    }
    if (count != 0)
        *count = n;
    return first;
}

/* "IRQ 9 10 11" for a bitmap, or "none". out holds 64 bytes. */
static void pir_irq_list(u16 map, char *out)
{
    int i;
    size_t n;

    out[0] = '\0';
    for (i = 0; i < 16; i++) {
        if ((map & (1U << i)) == 0)
            continue;
        n = strlen(out);
        sprintf(out + n, "%s%d", n == 0 ? "IRQ " : " ", i);
    }
    if (out[0] == '\0')
        strcpy(out, "none");
}

static void pir_pin_text(const u8 *s, int pin, char *out)
{
    u8 link;

    link = s[2 + pin * 3];
    if (link == 0)
        strcpy(out, "--/----");
    else
        sprintf(out, "%02X/%04X", link, pir_rd16(s + 3 + pin * 3));
}

static const char *pir_state_text(int state)
{
    switch (state) {
    case PIR_BAD_VERSION:
        return "version is not 1.x";
    case PIR_BAD_SIZE:
        return "table size is not above 32 and a multiple of 16";
    case PIR_TRUNCATED:
        return "the table runs past FFFFFh";
    case PIR_BAD_CHECKSUM:
        return "checksum FAILS";
    default:
        return "checksum OK";
    }
}

/* 00:1C named explicitly: the entry or its absence, then each function
 * configuration space showed (when pci.c read it). */
static void report_pir_rp(const PIRINFO *pi, QPRINTF_FN pf)
{
    int idx, count, fn, any;
    const u8 *s;
    char pin[16];

    idx = pir_find(pi, 0, PIR_RP_DEV, &count);
    if (idx < 0) {
        pf("  00:%02X: NO ENTRY%s - a function behind a root port here that "
           "POST\n", PIR_RP_DEV,
           pi->kept < pi->nslots ? " in the entries kept" : "");
        pf("    gave no Interrupt Line gets no IRQ from this table\n");
    } else if (count == 1) {
        pf("  00:%02X: listed, entry %d\n", PIR_RP_DEV, idx + 1);
    } else {
        pf("  00:%02X: listed %d times, first at entry %d\n", PIR_RP_DEV,
           count, idx + 1);
    }
    if (!pi->rp_read)
        return;
    any = 0;
    for (fn = 0; fn < 8; fn++) {
        /* No function 0, no device (pci.c reads no further). */
        if ((pi->rp_id[fn] & 0xFFFFUL) == 0xFFFFUL) {
            if (fn == 0)
                break;
            continue;
        }
        any = 1;
        if (pi->rp_pin[fn] >= 1 && pi->rp_pin[fn] <= 4 && idx >= 0) {
            s = pir_slot(pi, (u32)idx);
            pir_pin_text(s, pi->rp_pin[fn] - 1, pin);
        } else {
            strcpy(pin, idx >= 0 ? "no pin" : "no entry");
        }
        pf("    00:%02X.%d %04lX:%04lX%s pin %s%c line %u: %s\n",
           PIR_RP_DEV, fn, pi->rp_id[fn] & 0xFFFFUL,
           (pi->rp_id[fn] >> 16) & 0xFFFFUL,
           (pi->rp_hdr[fn] & 0x7F) == 1 ? " bridge," : ",",
           (pi->rp_pin[fn] >= 1 && pi->rp_pin[fn] <= 4) ? "INT" : "none",
           (pi->rp_pin[fn] >= 1 && pi->rp_pin[fn] <= 4)
               ? (char)('A' + pi->rp_pin[fn] - 1) : ' ',
           (unsigned)pi->rp_line[fn], pin);
        /* A function 0 that is not multi-function has no others. */
        if (fn == 0 && (pi->rp_hdr[0] & 0x80) == 0)
            break;
    }
    if (!any)
        pf("    no function at 00:%02X.0 in configuration space\n",
           PIR_RP_DEV);
}

void report_pir(const PIRINFO *pi, QPRINTF_FN pf)
{
    const u8 *t;
    const u8 *s;
    u32 i;
    char irqs[64];
    char a[16], b[16], c[16], d[16];

    switch (pi->state) {
    case PIR_NOT_READ:
        return;
    case PIR_NONE:
        pf("PCI IRQ routing table ($PIR, read only): none in "
           "F0000h-FFFFFh\n");
        return;
    case PIR_OK:
    case PIR_BAD_CHECKSUM:
        break;
    default:
        pf("PCI IRQ routing table ($PIR, read only): at %05lXh, UNUSABLE\n",
           pi->phys);
        pf("  version %u.%u, size %u: %s; nothing parsed\n",
           (unsigned)(pi->version >> 8), (unsigned)(pi->version & 0xFF),
           (unsigned)pi->size, pir_state_text(pi->state));
        return;
    }

    t = pi->table;
    pf("PCI IRQ routing table ($PIR, read only): at %05lXh, version %u.%u, "
       "%u bytes\n", pi->phys, (unsigned)(pi->version >> 8),
       (unsigned)(pi->version & 0xFF), (unsigned)pi->size);
    pf("  %lu slot entries, %s; router %02X:%02X.%X, compatible "
       "%04X:%04X\n", pi->nslots, pir_state_text(pi->state), t[8],
       t[9] >> 3, t[9] & 7, pir_rd16(t + 12), pir_rd16(t + 14));
    if (pi->state == PIR_BAD_CHECKSUM)
        pf("  (the specification requires the sum; the entries are shown "
           "as found)\n");
    pir_irq_list(pir_rd16(t + 10), irqs);
    pf("  exclusive PCI IRQs %04X: %s; miniport data %08lX\n",
       pir_rd16(t + 10), irqs, pir_rd32(t + 16));
    pf("  #   bus:dev slot INTA    INTB    INTC    INTD   (link/IRQ "
       "bitmap)\n");
    for (i = 0; i < pi->kept; i++) {
        s = pir_slot(pi, i);
        pir_pin_text(s, 0, a);
        pir_pin_text(s, 1, b);
        pir_pin_text(s, 2, c);
        pir_pin_text(s, 3, d);
        pf("  %-3lu %02X:%02X   %-4u %s %s %s %s%s\n", i + 1, s[0],
           s[1] >> 3, (unsigned)s[14], a, b, c, d,
           (s[1] & 7) != 0 ? " fn bits set" : "");
    }
    if (pi->kept < pi->nslots)
        pf("  %lu more entries not shown\n", pi->nslots - pi->kept);
    report_pir_rp(pi, pf);
}

void report_pir_short(const PIRINFO *pi, QPRINTF_FN pf)
{
    int idx;

    switch (pi->state) {
    case PIR_NOT_READ:
        return;
    case PIR_NONE:
        pf("IRQ table: no $PIR in F0000h-FFFFFh\n");
        return;
    case PIR_OK:
    case PIR_BAD_CHECKSUM:
        break;
    default:
        pf("IRQ table: $PIR at %05lXh unusable (%s)\n", pi->phys,
           pi->state == PIR_BAD_VERSION ? "version"
           : pi->state == PIR_BAD_SIZE  ? "size" : "truncated");
        return;
    }
    idx = pir_find(pi, 0, PIR_RP_DEV, 0);
    pf("IRQ table: $PIR at %05lXh, %lu entries%s; 00:%02X %s\n", pi->phys,
       pi->nslots, pi->state == PIR_BAD_CHECKSUM ? ", checksum FAILS" : "",
       PIR_RP_DEV, idx >= 0 ? "listed" : "NOT LISTED");
}
