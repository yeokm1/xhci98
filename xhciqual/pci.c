/*
 * pci.c - PCI configuration access (mechanism 1, 0xCF8/0xCFC) and the
 * Tier A discovery pass. Works from real mode or the 32-bit flat model;
 * only port I/O is used.
 */

#include <conio.h>
#include <string.h>
#include "qual.h"

#define PCI_CFG_ADDR 0xCF8
#define PCI_CFG_DATA 0xCFC

u32 pci_config_write_count;

static u32 cfg_addr(u8 bus, u8 dev, u8 fn, u8 off)
{
    return 0x80000000UL | ((u32)bus << 16) | ((u32)dev << 11) |
           ((u32)fn << 8) | (off & 0xFC);
}

u32 pci_read32(u8 bus, u8 dev, u8 fn, u8 off)
{
    outpd(PCI_CFG_ADDR, cfg_addr(bus, dev, fn, off));
    return inpd(PCI_CFG_DATA);
}

u16 pci_read16(u8 bus, u8 dev, u8 fn, u8 off)
{
    outpd(PCI_CFG_ADDR, cfg_addr(bus, dev, fn, off));
    return inpw(PCI_CFG_DATA + (off & 2));
}

u8 pci_read8(u8 bus, u8 dev, u8 fn, u8 off)
{
    outpd(PCI_CFG_ADDR, cfg_addr(bus, dev, fn, off));
    return inp(PCI_CFG_DATA + (off & 3));
}

void pci_write32(u8 bus, u8 dev, u8 fn, u8 off, u32 val)
{
    pci_config_write_count++;
    outpd(PCI_CFG_ADDR, cfg_addr(bus, dev, fn, off));
    outpd(PCI_CFG_DATA, val);
}

void pci_write16(u8 bus, u8 dev, u8 fn, u8 off, u16 val)
{
    pci_config_write_count++;
    outpd(PCI_CFG_ADDR, cfg_addr(bus, dev, fn, off));
    outpw(PCI_CFG_DATA + (off & 2), val);
}

int pci_disable_bus_master(PCIINFO *p)
{
    u16 want;

    want = p->cmd_orig & (u16)~PCI_CMD_BME;
    pci_write16(p->bus, p->dev, p->fn, 0x04, want);
    return (pci_read16(p->bus, p->dev, p->fn, 0x04) & PCI_CMD_BME) == 0;
}

const char *hc_name(int hctype)
{
    switch (hctype) {
    case HC_OHCI: return "OHCI";
    case HC_EHCI: return "EHCI";
    case HC_XHCI: return "xHCI";
    default:      return "unknown";
    }
}

static int class_to_type(u32 classcode, int family_mask)
{
    if (classcode == PCI_CLASS_XHCI && (family_mask & HC_MASK_XHCI))
        return HC_XHCI;
    if (classcode == PCI_CLASS_EHCI && (family_mask & HC_MASK_EHCI))
        return HC_EHCI;
    if (classcode == PCI_CLASS_OHCI && (family_mask & HC_MASK_OHCI))
        return HC_OHCI;
    return 0;
}

/* A1: enumerate selected USB host-controller programming interfaces. */
int pci_scan_usb(PCIINFO *out, int max, int family_mask)
{
    int found = 0;
    unsigned bus, dev, fn, nfn;
    u16 vid;
    u32 classrev;
    u8 htype;
    int hctype;

    for (bus = 0; bus < 256 && found < max; bus++) {
        for (dev = 0; dev < 32 && found < max; dev++) {
            vid = pci_read16((u8)bus, (u8)dev, 0, 0x00);
            if (vid == 0xFFFF)
                continue;
            htype = pci_read8((u8)bus, (u8)dev, 0, 0x0E);
            nfn = (htype & 0x80) ? 8 : 1;
            for (fn = 0; fn < nfn && found < max; fn++) {
                vid = pci_read16((u8)bus, (u8)dev, (u8)fn, 0x00);
                if (vid == 0xFFFF)
                    continue;
                classrev = pci_read32((u8)bus, (u8)dev, (u8)fn, 0x08);
                hctype = class_to_type(classrev >> 8, family_mask);
                if (hctype == 0)
                    continue;
                memset(&out[found], 0, sizeof(PCIINFO));
                out[found].hctype = (u8)hctype;
                out[found].bus = (u8)bus;
                out[found].dev = (u8)dev;
                out[found].fn  = (u8)fn;
                out[found].vid = vid;
                out[found].did = pci_read16((u8)bus, (u8)dev, (u8)fn, 0x02);
                out[found].rev = (u8)(classrev & 0xFF);
                found++;
            }
        }
    }
    return found;
}

/* A2-A4 (+A6 regs read later by quirks caller): static config facts */
void pci_read_static(PCIINFO *p)
{
    u16 status;
    u8 capptr;
    int guard;

    p->cmd_orig = pci_read16(p->bus, p->dev, p->fn, 0x04);
    p->cmd_effective = p->cmd_orig;
    p->bme_ok = -1;   /* determined during active tests */

    p->bar_lo = pci_read32(p->bus, p->dev, p->fn, 0x10);
    p->bar_is64 = ((p->bar_lo & 0x6) == 0x4);
    p->bar_pref = ((p->bar_lo & 0x8) != 0);
    p->bar_hi = p->bar_is64 ? pci_read32(p->bus, p->dev, p->fn, 0x14) : 0;
    p->bar_phys = p->bar_lo & 0xFFFFFFF0UL;

    p->ipin  = pci_read8(p->bus, p->dev, p->fn, 0x3D);
    p->iline = pci_read8(p->bus, p->dev, p->fn, 0x3C);

    /* Subsystem IDs: the VID/DID names the silicon, these name the board it
     * is fitted to. Worth recording per machine - two laptops can carry the
     * same xHCI DID behind different firmware, and an add-in card's board
     * vendor is invisible without them. */
    p->subsys_vid = pci_read16(p->bus, p->dev, p->fn, 0x2C);
    p->subsys_did = pci_read16(p->bus, p->dev, p->fn, 0x2E);

    /* capability list walk */
    status = pci_read16(p->bus, p->dev, p->fn, 0x06);
    p->status_orig = status;
    p->status_final = 0;
    p->status_rechecked = 0;
    if (status & 0x0010) {
        capptr = pci_read8(p->bus, p->dev, p->fn, 0x34) & 0xFC;
        for (guard = 0; capptr != 0 && guard < 48; guard++) {
            u8 id   = pci_read8(p->bus, p->dev, p->fn, capptr);
            u8 next = pci_read8(p->bus, p->dev, p->fn, (u8)(capptr + 1));
            switch (id) {
            /* PCI Bus Power Management Interface Specification rev 1.2,
             * section 3.2: PMC at cap+2 (D1_Support bit 9, D2_Support bit 10,
             * PME_Support bits 15:11 = D0,D1,D2,D3hot,D3cold), PMCSR at cap+4
             * (PowerState bits 1:0, PME_En bit 8, PME_Status bit 15). That
             * spec is not mirrored in docs/references - cross-check the first
             * bare-metal run against `lspci -vv` on the same machine
             * (xhciqual/hardware-testing.md, "Cross-checking the PCI block").
             * Reads only: the qualifier never writes PMCSR. */
            case 0x01:
                p->has_pm = 1;
                p->pmc = pci_read16(p->bus, p->dev, p->fn, (u8)(capptr + 2));
                p->pmcsr = pci_read16(p->bus, p->dev, p->fn, (u8)(capptr + 4));
                p->pm_state = (u8)(p->pmcsr & 0x3);
                break;
            case 0x05:
                p->has_msi = 1;
                p->msi_enabled =
                    (pci_read16(p->bus, p->dev, p->fn, (u8)(capptr + 2)) & 1);
                break;
            case 0x10: p->has_pcie = 1; break;
            case 0x11: p->has_msix = 1; break;
            default: break;
            }
            capptr = next & 0xFC;
        }
    }
}

/* Re-read PCI Status after the active tests have run and the controller has
 * been cleaned up. The error bits there (master/target abort, parity, SERR)
 * are sticky, so the only way to attribute one to this run is to compare
 * against the snapshot taken before the tool touched anything.
 *
 * A read, never a write: clearing these bits is RW1C and would destroy
 * evidence a later diagnosis might want - including for whoever runs the tool
 * next. --probe-only never calls this at all, since it runs no traffic that
 * could set them. */
void pci_recheck_status(PCIINFO *p)
{
    p->status_final = pci_read16(p->bus, p->dev, p->fn, 0x06);
    p->status_rechecked = 1;
}
