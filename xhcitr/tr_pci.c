/*
 * tr_pci.c - the pure half of XHCITR's "PCI configuration" nodes
 * (tr_pci.h; design record 25 sections 3.6 and 4.7, revision 12). No
 * Windows call; test\test_trpci.c links it under /Za. C89.
 *
 * Every offset and bit below is this file's own constant, cited to the
 * public specification that defines it:
 *
 *   [LB]  PCI Local Bus Specification 3.0: 6.1 and 6.2 (the common
 *         header: ids, Command, Status, revision, class, header type,
 *         the type 0 BARs, subsystem ids, capabilities pointer, Interrupt
 *         Line and Pin), 6.2.5.1 (BAR encoding), 6.7 (the capability
 *         list), 6.8.1 (MSI) and 6.8.2 (MSI-X), Appendix H (capability
 *         ids).
 *   [BR]  PCI-to-PCI Bridge Architecture Specification 1.2: 3.2 (the type
 *         1 header: bus numbers, the I/O, memory and prefetchable
 *         windows), 3.2.5.13 (the subsystem capability of a bridge).
 *   [PM]  PCI Bus Power Management Interface Specification 1.2: 3.2 (PMC
 *         and PMCSR).
 *   [PE]  PCI Express Base Specification 4.0: 7.5.3 (the PCI Express
 *         Capability: its Capabilities register, Link Capabilities, Link
 *         Status).
 *
 * Nothing here reads past the 256 bytes it is given, nor decodes past
 * the bytes that were read (tr_pci_text_n).
 */

#include <string.h>
#include "tr_pci.h"
#include "tr_log.h"
#include "../src/cm/cm_query.h"
#include "../src/xhci_qry.h"

/* [LB] 6.1: the common header. */
#define PCI_ID              0x00
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION        0x08
#define PCI_CLASS           0x09    /* prog-if 09h, sub-class 0Ah, base 0Bh */
#define PCI_HEADER          0x0E
#define PCI_BAR0            0x10
#define PCI_SUBSYS          0x2C    /* type 0: vendor 2Ch, id 2Eh */
#define PCI_CAPPTR          0x34
#define PCI_INT_LINE        0x3C
#define PCI_INT_PIN         0x3D
#define PCI_HDR_MF          0x80
#define PCI_HDR_LAYOUT      0x7F

/* [LB] 6.2.2: Command. */
#define CMD_IO              0x0001
#define CMD_MEMORY          0x0002
#define CMD_MASTER          0x0004
#define CMD_PARITY          0x0040
#define CMD_SERR            0x0100
#define CMD_INTX_OFF        0x0400

/* [LB] 6.2.3: Status. */
#define STS_INTX            0x0008
#define STS_CAPLIST         0x0010
#define STS_PARITY_MASTER   0x0100
#define STS_TARGET_ABORT_S  0x0800
#define STS_TARGET_ABORT_R  0x1000
#define STS_MASTER_ABORT_R  0x2000
#define STS_SERR_S          0x4000
#define STS_PARITY_DET      0x8000

/* [BR] 3.2: the type 1 header. */
#define BR_PRIMARY          0x18
#define BR_SECONDARY        0x19
#define BR_SUBORD           0x1A
#define BR_IO_BASE          0x1C
#define BR_IO_LIMIT         0x1D
#define BR_MEM_BASE         0x20
#define BR_MEM_LIMIT        0x22
#define BR_PF_BASE          0x24
#define BR_PF_LIMIT         0x26
#define BR_PF_BASE_HI       0x28
#define BR_PF_LIMIT_HI      0x2C
#define BR_IO_BASE_HI       0x30
#define BR_IO_LIMIT_HI      0x32

/* [LB] Appendix H: capability ids. */
#define CAP_PM              0x01
#define CAP_AGP             0x02
#define CAP_VPD             0x03
#define CAP_SLOTID          0x04
#define CAP_MSI             0x05
#define CAP_HOTSWAP         0x06
#define CAP_PCIX            0x07
#define CAP_HT              0x08
#define CAP_VENDOR          0x09
#define CAP_DEBUG           0x0A
#define CAP_CRC             0x0B
#define CAP_SHPC            0x0C
#define CAP_SSVID           0x0D
#define CAP_AGP8            0x0E
#define CAP_SECURE          0x0F
#define CAP_PCIE            0x10
#define CAP_MSIX            0x11
#define CAP_SATA            0x12
#define CAP_AF              0x13

/* [PM] 3.2: PMC at +2, PMCSR at +4. */
#define PM_PMC              0x02
#define PM_PMCSR            0x04
#define PM_BYTES            0x08

/* [LB] 6.8.1: MSI's Message Control at +2. */
#define MSI_CTRL            0x02
#define MSI_BYTES           0x0C
#define MSI_ENABLE          0x0001
#define MSI_64BIT           0x0080

/* [LB] 6.8.2: MSI-X's Message Control at +2, table at +4, PBA at +8. */
#define MSIX_CTRL           0x02
#define MSIX_TABLE          0x04
#define MSIX_PBA            0x08
#define MSIX_BYTES          0x0C
#define MSIX_SIZE           0x07FF
#define MSIX_MASKALL        0x4000
#define MSIX_ENABLE         0x8000

/* [PE] 7.5.3: the PCI Express Capability. */
#define PCIE_CAPS           0x02    /* version 3:0, port type 7:4, slot 8 */
#define PCIE_LINK_CAP       0x0C    /* max speed 3:0, max width 9:4       */
#define PCIE_LINK_STATUS    0x12    /* speed 3:0, width 9:4, active 13    */
#define PCIE_BYTES          0x14
#define PCIE_SLOT           0x0100
#define PCIE_DLL_ACTIVE     0x2000

/* [BR] 3.2.5.13: a bridge's subsystem ids at +4. */
#define SSVID_IDS           0x04
#define SSVID_BYTES         0x08

static unsigned long g16(const unsigned char *b, unsigned int o)
{
    return (unsigned long)b[o] | ((unsigned long)b[o + 1] << 8);
}

static unsigned long g32(const unsigned char *b, unsigned int o)
{
    return (unsigned long)b[o] | ((unsigned long)b[o + 1] << 8) |
           ((unsigned long)b[o + 2] << 16) | ((unsigned long)b[o + 3] << 24);
}

/* How many bytes of the configuration space being decoded were read
 * (tr_pci_text_n): nothing past them is decoded. The tool is single-
 * threaded where it decodes. */
static unsigned int trLim = TR_PCI_CFG_BYTES;

static void ind(TR_TEXT *t, int indent)
{
    while (indent-- > 0) {
        tr_put(t, " ");
    }
}

/* ======================================================================== */
/* The capability walk                                                       */
/* ======================================================================== */

int tr_pci_caps(const unsigned char *cfg, TR_PCICAP *out, int max, int *how,
                unsigned int *badAt)
{
    unsigned char seen[TR_PCI_CFG_BYTES / 4];
    unsigned int at;
    int n;

    *how = 0;
    *badAt = 0;
    if ((g16(cfg, PCI_STATUS) & STS_CAPLIST) == 0) {
        *how = TR_PCI_WALK_NONE;
        return 0;
    }
    memset(seen, 0, sizeof(seen));
    n = 0;
    /* [LB] 6.7: the pointer's two low bits are reserved. */
    at = cfg[PCI_CAPPTR] & 0xFCU;
    while (at != 0) {
        if (at < 0x40U || at > 0xFCU) {
            *how |= TR_PCI_WALK_BAD_PTR;
            *badAt = at;
            break;
        }
        if (at + 2U > trLim) {
            *how |= TR_PCI_WALK_UNREAD;
            *badAt = at;
            break;
        }
        if (seen[at >> 2]) {
            *how |= TR_PCI_WALK_LOOP;
            *badAt = at;
            break;
        }
        if (n >= max) {
            *how |= TR_PCI_WALK_CUT;
            break;
        }
        seen[at >> 2] = 1;
        out[n].Id = cfg[at];
        out[n].At = at;
        n++;
        at = cfg[at + 1] & 0xFCU;
    }
    return n;
}

/* ======================================================================== */
/* Short forms                                                               */
/* ======================================================================== */

void tr_pci_pin(unsigned int pin, unsigned int line, char *out, size_t cap)
{
    char l[32];

    if (line == 0xFFU) {
        strcpy(l, "line not assigned (FFh)");
    } else {
        tr_sprintf(l, sizeof(l), "line %u", line);
    }
    if (pin == 0) {
        tr_sprintf(out, cap, "no pin (pin 0), %s", l);
    } else if (pin <= 4) {
        tr_sprintf(out, cap, "pin INT%c, %s", 'A' + (int)pin - 1, l);
    } else {
        tr_sprintf(out, cap, "pin invalid (%u), %s", pin, l);
    }
}

void tr_pci_msi(const unsigned char *cfg, unsigned int at, char *out,
                size_t cap)
{
    unsigned long c;
    unsigned int capable;
    unsigned int enabled;

    if (at + MSI_BYTES > trLim) {
        tr_sprintf(out, cap, "MSI: present, past the bytes read");
        return;
    }
    c = g16(cfg, at + MSI_CTRL);
    /* Multiple Message Capable 3:1 and Enable 6:4, each log2 of the count;
     * 6 and 7 reserved. */
    capable = (unsigned int)((c >> 1) & 7UL);
    enabled = (unsigned int)((c >> 4) & 7UL);
    if (capable > 5) {
        capable = 5;
    }
    if (enabled > 5) {
        enabled = 5;
    }
    if (c & MSI_ENABLE) {
        tr_sprintf(out, cap, "MSI: present, enabled (%u of %u vector%s)%s",
                   1U << enabled, 1U << capable, capable ? "s" : "",
                   (c & MSI_64BIT) ? ", 64-bit" : "");
    } else {
        tr_sprintf(out, cap, "MSI: present, disabled (%u vector%s)%s",
                   1U << capable, capable ? "s" : "",
                   (c & MSI_64BIT) ? ", 64-bit" : "");
    }
}

void tr_pci_msix(const unsigned char *cfg, unsigned int at, char *out,
                 size_t cap)
{
    unsigned long c;

    if (at + MSIX_BYTES > trLim) {
        tr_sprintf(out, cap, "MSI-X: present, past the bytes read");
        return;
    }
    c = g16(cfg, at + MSIX_CTRL);
    tr_sprintf(out, cap, "MSI-X: present, %s (table size %lu)%s",
               (c & MSIX_ENABLE) ? "enabled" : "disabled",
               (c & MSIX_SIZE) + 1UL,
               (c & MSIX_MASKALL) ? ", all masked" : "");
}

/* Link speeds by their encoding, [PE] 7.5.3.6 and 7.5.3.8: 1 2.5 GT/s,
 * 2 5 GT/s, 3 8 GT/s, 4 16 GT/s, 5 32 GT/s - Gen 1 to Gen 5. */
static void speed(char *out, size_t cap, unsigned long s, unsigned long w)
{
    if (s >= 1 && s <= 5) {
        tr_sprintf(out, cap, "Gen %lu x%lu", s, w);
    } else {
        tr_sprintf(out, cap, "code %lu x%lu", s, w);
    }
}

void tr_pci_link(const unsigned char *cfg, unsigned int at, char *out,
                 size_t cap)
{
    unsigned long lc;
    unsigned long ls;
    char now[32];
    char can[32];

    if (at + PCIE_BYTES > trLim) {
        tr_sprintf(out, cap, "past the bytes read");
        return;
    }
    lc = g32(cfg, at + PCIE_LINK_CAP);
    ls = g16(cfg, at + PCIE_LINK_STATUS);
    speed(can, sizeof(can), lc & 0xFUL, (lc >> 4) & 0x3FUL);
    if (((ls >> 4) & 0x3FUL) == 0) {
        tr_sprintf(out, cap, "down (capable %s)", can);
        return;
    }
    speed(now, sizeof(now), ls & 0xFUL, (ls >> 4) & 0x3FUL);
    tr_sprintf(out, cap, "%s (capable %s)", now, can);
}

/* [PE] 7.5.3.2: Device/Port Type. */
const char *tr_pci_port_type(unsigned int type)
{
    switch (type) {
    case 0x0: return "endpoint";
    case 0x1: return "legacy endpoint";
    case 0x4: return "root port";
    case 0x5: return "switch upstream port";
    case 0x6: return "switch downstream port";
    case 0x7: return "PCIe-to-PCI bridge";
    case 0x8: return "PCI-to-PCIe bridge";
    case 0x9: return "root complex integrated endpoint";
    case 0xA: return "root complex event collector";
    default: return "reserved type";
    }
}

/* The few classes this tool meets behind a tunnel, by base, sub-class and
 * programming interface ([LB] Appendix D). */
const char *tr_pci_class_text(unsigned long c)
{
    switch (c) {
    case 0x060400UL: return "PCI-to-PCI bridge";
    case 0x060401UL: return "PCI-to-PCI bridge, subtractive";
    case 0x0C0330UL: return "USB xHCI controller";
    case 0x0C0320UL: return "USB EHCI controller";
    case 0x0C0310UL: return "USB OHCI controller";
    case 0x0C0300UL: return "USB UHCI controller";
    case 0x0C0340UL: return "USB4 host interface";
    case 0x0C0380UL: return "USB controller";
    case 0x088000UL: return "system peripheral";
    case 0x010802UL: return "NVM Express controller";
    case 0x010601UL: return "SATA AHCI controller";
    case 0x020000UL: return "Ethernet controller";
    case 0x030000UL: return "VGA display controller";
    case 0x040300UL: return "HD audio controller";
    default: break;
    }
    switch (c >> 16) {
    case 0x01: return "mass storage controller";
    case 0x02: return "network controller";
    case 0x03: return "display controller";
    case 0x04: return "multimedia controller";
    case 0x06: return "bridge";
    case 0x08: return "system peripheral";
    case 0x0C: return "serial bus controller";
    default: return "other";
    }
}

static const char *cap_name(unsigned int id)
{
    switch (id) {
    case CAP_PM: return "Power management";
    case CAP_AGP: return "AGP";
    case CAP_VPD: return "Vital product data";
    case CAP_SLOTID: return "Slot identification";
    case CAP_MSI: return "MSI";
    case CAP_HOTSWAP: return "CompactPCI hot swap";
    case CAP_PCIX: return "PCI-X";
    case CAP_HT: return "HyperTransport";
    case CAP_VENDOR: return "Vendor specific";
    case CAP_DEBUG: return "Debug port";
    case CAP_CRC: return "CompactPCI central resource control";
    case CAP_SHPC: return "Standard hot-plug controller";
    case CAP_SSVID: return "Bridge subsystem ids";
    case CAP_AGP8: return "AGP 8x";
    case CAP_SECURE: return "Secure device";
    case CAP_PCIE: return "PCI Express";
    case CAP_MSIX: return "MSI-X";
    case CAP_SATA: return "SATA configuration";
    case CAP_AF: return "Advanced features";
    default: return "unknown";
    }
}

/* ======================================================================== */
/* One function                                                              */
/* ======================================================================== */

typedef struct _TR_PCIBIT {
    unsigned long Bit;
    const char *Name;
} TR_PCIBIT;

static const TR_PCIBIT cmdBits[] = {
    { CMD_IO, "I/O" }, { CMD_MEMORY, "memory" },
    { CMD_MASTER, "bus master" }, { CMD_PARITY, "parity" },
    { CMD_SERR, "SERR" }, { CMD_INTX_OFF, "INTx disabled" }
};

static const TR_PCIBIT stsBits[] = {
    { STS_CAPLIST, "capability list" }, { STS_INTX, "INTx pending" },
    { STS_PARITY_MASTER, "master parity error" },
    { STS_TARGET_ABORT_S, "target abort sent" },
    { STS_TARGET_ABORT_R, "target abort received" },
    { STS_MASTER_ABORT_R, "master abort received" },
    { STS_SERR_S, "SERR sent" }, { STS_PARITY_DET, "parity error" }
};

/* "Label               1234 (a, b, c)", the names wrapped so that no line
 * passes 79 columns, each further line under the first name. */
static void flags(TR_TEXT *t, int indent, const char *label, unsigned long v,
                  const TR_PCIBIT *b, unsigned int count)
{
    char line[96];
    size_t col;
    size_t len;
    unsigned int i;
    int any = 0;

    ind(t, indent);
    tr_sprintf(line, sizeof(line), "%-20s%04lX", label, v);
    tr_put(t, line);
    col = (size_t)indent + strlen(line);
    for (i = 0; i < count; i++) {
        if ((v & b[i].Bit) == 0) {
            continue;
        }
        len = strlen(b[i].Name) + 2;
        if (any && col + len + 1 > 79) {
            tr_put(t, ",\n");
            ind(t, indent + 26);
            col = (size_t)indent + 26;
            tr_put(t, b[i].Name);
            col += strlen(b[i].Name);
        } else {
            tr_printf(t, "%s%s", any ? ", " : " (", b[i].Name);
            col += len;
        }
        any = 1;
    }
    tr_put(t, any ? ")\n" : "\n");
}

/* The BARs raw, as read: never sized, so only the encoding is said
 * ([LB] 6.2.5.1). A 64-bit memory BAR takes the next one as its upper
 * half. */
static void bars(TR_TEXT *t, const unsigned char *cfg, int n, int indent)
{
    int i;
    unsigned long v;
    unsigned long hi;

    for (i = 0; i < n; i++) {
        v = g32(cfg, PCI_BAR0 + 4 * i);
        ind(t, indent);
        if (v & 1UL) {
            tr_printf(t, "BAR%d                %08lX (I/O at %lX)\n", i, v,
                      v & ~3UL);
        } else if (((v >> 1) & 3UL) == 2UL && i + 1 < n) {
            hi = g32(cfg, PCI_BAR0 + 4 * (i + 1));
            tr_printf(t, "BAR%d-%d              %08lX%08lX (memory, 64-bit%s)\n",
                      i, i + 1, hi, v, (v & 8UL) ? ", prefetchable" : "");
            i++;
        } else if (v == 0) {
            tr_printf(t, "BAR%d                00000000\n", i);
        } else {
            tr_printf(t, "BAR%d                %08lX (memory, %s%s)\n", i, v,
                      ((v >> 1) & 3UL) == 0 ? "32-bit"
                      : ((v >> 1) & 3UL) == 1 ? "below 1 MB" : "reserved type",
                      (v & 8UL) ? ", prefetchable" : "");
        }
    }
}

/* A window's line: "E0000000-E00FFFFF", or "none" when its base lies
 * above its limit ([BR] 3.2.5.6 to 3.2.5.10). Hex pairs compared as
 * such; no 64-bit arithmetic. */
static void window(TR_TEXT *t, int indent, const char *name, int wide,
                   unsigned long bHi, unsigned long bLo, unsigned long lHi,
                   unsigned long lLo)
{
    ind(t, indent);
    tr_printf(t, "%-20s", name);
    if (bHi > lHi || (bHi == lHi && bLo > lLo)) {
        tr_put(t, "none\n");
    } else if (wide) {
        tr_printf(t, "Window %08lX%08lX-%08lX%08lX\n", bHi, bLo, lHi, lLo);
    } else {
        tr_printf(t, "Window %08lX-%08lX\n", bLo, lLo);
    }
}

static void bridge(TR_TEXT *t, const unsigned char *cfg, int indent)
{
    unsigned long b;
    unsigned long l;
    unsigned long bHi;
    unsigned long lHi;
    int io32;
    int pf64;

    ind(t, indent);
    tr_printf(t, "Buses               primary %02X, Bus %02X-%02X\n",
              cfg[BR_PRIMARY], cfg[BR_SECONDARY], cfg[BR_SUBORD]);
    /* I/O: 4 KB granular; bits 3:0 say 16- or 32-bit addressing. */
    io32 = (cfg[BR_IO_BASE] & 0x0F) == 1;
    b = ((unsigned long)(cfg[BR_IO_BASE] & 0xF0) << 8);
    l = ((unsigned long)(cfg[BR_IO_LIMIT] & 0xF0) << 8) | 0xFFFUL;
    if (io32) {
        b |= g16(cfg, BR_IO_BASE_HI) << 16;
        l |= g16(cfg, BR_IO_LIMIT_HI) << 16;
    }
    window(t, indent, "I/O window", 0, 0, b, 0, l);
    /* Memory: 1 MB granular, 32-bit. */
    b = (g16(cfg, BR_MEM_BASE) & 0xFFF0UL) << 16;
    l = ((g16(cfg, BR_MEM_LIMIT) & 0xFFF0UL) << 16) | 0xFFFFFUL;
    window(t, indent, "Memory window", 0, 0, b, 0, l);
    /* Prefetchable: 1 MB granular; bits 3:0 say 32- or 64-bit. */
    pf64 = (cfg[BR_PF_BASE] & 0x0F) == 1;
    b = (g16(cfg, BR_PF_BASE) & 0xFFF0UL) << 16;
    l = ((g16(cfg, BR_PF_LIMIT) & 0xFFF0UL) << 16) | 0xFFFFFUL;
    bHi = pf64 ? g32(cfg, BR_PF_BASE_HI) : 0;
    lHi = pf64 ? g32(cfg, BR_PF_LIMIT_HI) : 0;
    window(t, indent, "Prefetchable window", pf64 && (bHi | lHi) != 0, bHi, b,
           lHi, l);
}

static void caps(TR_TEXT *t, const unsigned char *cfg, int indent)
{
    TR_PCICAP list[TR_PCI_CAP_MAX];
    char s[96];
    int n;
    int i;
    int how;
    int msi = 0;
    int msix = 0;
    int pcie = 0;
    unsigned int badAt;
    unsigned int at;
    unsigned long v;

    n = tr_pci_caps(cfg, list, TR_PCI_CAP_MAX, &how, &badAt);
    ind(t, indent);
    if (how & TR_PCI_WALK_NONE) {
        tr_put(t, "Capabilities        none (Status bit 4 clear)\n");
        return;
    }
    tr_printf(t, "Capabilities        list at %02Xh, %d entr%s\n",
              cfg[PCI_CAPPTR] & 0xFC, n, n == 1 ? "y" : "ies");
    for (i = 0; i < n; i++) {
        at = list[i].At;
        ind(t, indent + 2);
        tr_printf(t, "%02Xh at %02Xh  ", list[i].Id, at);
        switch (list[i].Id) {
        case CAP_MSI:
            msi = 1;
            tr_pci_msi(cfg, at, s, sizeof(s));
            tr_printf(t, "%s\n", s);
            break;
        case CAP_MSIX:
            msix = 1;
            tr_pci_msix(cfg, at, s, sizeof(s));
            tr_printf(t, "%s\n", s);
            if (at + MSIX_BYTES <= trLim) {
                ind(t, indent + 4);
                tr_printf(t, "table BAR%lu+%lXh, pending bits BAR%lu+%lXh\n",
                          g32(cfg, at + MSIX_TABLE) & 7UL,
                          g32(cfg, at + MSIX_TABLE) & ~7UL,
                          g32(cfg, at + MSIX_PBA) & 7UL,
                          g32(cfg, at + MSIX_PBA) & ~7UL);
            }
            break;
        case CAP_PCIE:
            pcie = 1;
            if (at + PCIE_BYTES > trLim) {
                tr_put(t, "PCI Express, past the bytes read\n");
                break;
            }
            v = g16(cfg, at + PCIE_CAPS);
            tr_printf(t, "PCI Express v%lu, %s%s\n", v & 0xFUL,
                      tr_pci_port_type((unsigned int)((v >> 4) & 0xFUL)),
                      (v & PCIE_SLOT) ? ", slot" : "");
            ind(t, indent + 4);
            if (((v >> 4) & 0xFUL) == 0x9 || ((v >> 4) & 0xFUL) == 0xA) {
                tr_put(t, "PCIe link: none (no link of its own)\n");
            } else {
                tr_pci_link(cfg, at, s, sizeof(s));
                tr_printf(t, "PCIe link: %s%s\n", s,
                          (g16(cfg, at + PCIE_LINK_STATUS) & PCIE_DLL_ACTIVE)
                              ? ", link active" : "");
            }
            break;
        case CAP_PM:
            if (at + PM_BYTES > trLim) {
                tr_put(t, "Power management, past the bytes read\n");
                break;
            }
            v = g16(cfg, at + PM_PMC);
            tr_printf(t, "Power management v%lu, state D%lu%s%s\n",
                      v & 7UL, g16(cfg, at + PM_PMCSR) & 3UL,
                      (v & 0xF800UL) ? ", PME capable" : "",
                      (g16(cfg, at + PM_PMCSR) & 0x0100UL) ? ", PME enabled"
                                                           : "");
            break;
        case CAP_SSVID:
            if (at + SSVID_BYTES > trLim) {
                tr_put(t, "Bridge subsystem ids, past the bytes read\n");
                break;
            }
            tr_printf(t, "Subsystem %04lX:%04lX\n", g16(cfg, at + SSVID_IDS),
                      g16(cfg, at + SSVID_IDS + 2));
            break;
        default:
            tr_printf(t, "%s\n", cap_name(list[i].Id));
            break;
        }
    }
    if (how & TR_PCI_WALK_BAD_PTR) {
        ind(t, indent + 2);
        tr_printf(t, "The list ends at an invalid pointer (%02Xh).\n", badAt);
    }
    if (how & TR_PCI_WALK_LOOP) {
        ind(t, indent + 2);
        tr_printf(t, "The list loops back to %02Xh; the walk stopped.\n",
                  badAt);
    }
    if (how & TR_PCI_WALK_UNREAD) {
        ind(t, indent + 2);
        tr_printf(t, "The list goes on at %02Xh, past the bytes read.\n",
                  badAt);
    }
    if (how & TR_PCI_WALK_CUT) {
        ind(t, indent + 2);
        tr_printf(t, "The list holds more than %d entries; the walk "
                     "stopped.\n", TR_PCI_CAP_MAX);
    }
    if (how & TR_PCI_WALK_UNREAD) {
        /* The rest of the list was not read: absence is not known. */
        return;
    }
    if (!msi) {
        ind(t, indent + 2);
        tr_put(t, "MSI: not present\n");
    }
    if (!msix) {
        ind(t, indent + 2);
        tr_put(t, "MSI-X: not present\n");
    }
    if (!pcie) {
        ind(t, indent + 2);
        tr_put(t, "PCI Express: not present\n");
    }
}

void tr_pci_text(TR_TEXT *t, const unsigned char *cfg, int indent)
{
    tr_pci_text_n(t, cfg, TR_PCI_CFG_BYTES, indent);
}

static void pci_text_body(TR_TEXT *t, const unsigned char *cfg,
                          int indent);

void tr_pci_text_n(TR_TEXT *t, const unsigned char *cfg,
                   unsigned long valid, int indent)
{
    unsigned long id;
    unsigned long cls;

    if (valid > TR_PCI_CFG_BYTES) {
        valid = TR_PCI_CFG_BYTES;
    }
    valid &= ~3UL;
    if (valid < 0x40) {
        /* Not the whole common header: what was read, and no more. */
        if (valid >= 4) {
            id = g32(cfg, PCI_ID);
            ind(t, indent);
            tr_printf(t, "Vendor:device       %04lX:%04lX\n",
                      id & 0xFFFFUL, id >> 16);
        }
        if (valid >= 0x0C) {
            cls = ((unsigned long)cfg[PCI_CLASS + 2] << 16) |
                  ((unsigned long)cfg[PCI_CLASS + 1] << 8) |
                  cfg[PCI_CLASS];
            ind(t, indent);
            tr_printf(t, "Class               %06lX (%s)\n", cls,
                      tr_pci_class_text(cls));
        }
        ind(t, indent);
        tr_printf(t, "Only the first %lu bytes of its configuration "
                     "space could be read.\n", valid);
        return;
    }
    trLim = (unsigned int)valid;
    pci_text_body(t, cfg, indent);
    trLim = TR_PCI_CFG_BYTES;
}

static void pci_text_body(TR_TEXT *t, const unsigned char *cfg,
                          int indent)
{
    unsigned long id;
    unsigned long cls;
    unsigned int layout;
    char s[64];

    id = g32(cfg, PCI_ID);
    cls = ((unsigned long)cfg[PCI_CLASS + 2] << 16) |
          ((unsigned long)cfg[PCI_CLASS + 1] << 8) | cfg[PCI_CLASS];
    layout = cfg[PCI_HEADER] & PCI_HDR_LAYOUT;
    if (trLim < TR_PCI_CFG_BYTES) {
        ind(t, indent);
        tr_printf(t, "Only the first %u bytes could be read; nothing "
                     "past them is shown.\n", trLim);
    }
    ind(t, indent);
    tr_printf(t, "Vendor:device       %04lX:%04lX, revision %02X\n",
              id & 0xFFFFUL, id >> 16, cfg[PCI_REVISION]);
    if (layout == 0) {
        ind(t, indent);
        tr_printf(t, "Subsystem           %04lX:%04lX\n", g16(cfg, PCI_SUBSYS),
                  g16(cfg, PCI_SUBSYS + 2));
    }
    ind(t, indent);
    tr_printf(t, "Class               %06lX (%s)\n", cls, tr_pci_class_text(cls));
    ind(t, indent);
    tr_printf(t, "Header type         %u%s%s\n", layout,
              layout == 0 ? " (device)" : layout == 1 ? " (PCI-to-PCI bridge)"
              : layout == 2 ? " (CardBus bridge)" : " (unknown)",
              (cfg[PCI_HEADER] & PCI_HDR_MF) ? ", multi-function" : "");
    flags(t, indent, "Command", g16(cfg, PCI_COMMAND), cmdBits,
          sizeof(cmdBits) / sizeof(cmdBits[0]));
    flags(t, indent, "Status", g16(cfg, PCI_STATUS), stsBits,
          sizeof(stsBits) / sizeof(stsBits[0]));
    tr_pci_pin(cfg[PCI_INT_PIN], cfg[PCI_INT_LINE], s, sizeof(s));
    ind(t, indent);
    tr_printf(t, "Interrupt           %s\n", s);
    if (layout == 0) {
        bars(t, cfg, 6, indent);
    } else if (layout == 1) {
        bars(t, cfg, 2, indent);
        bridge(t, cfg, indent);
    }
    if (layout <= 1) {
        caps(t, cfg, indent);
    }
}

/* ======================================================================== */
/* xhcicm.sys's answer                                                       */
/* ======================================================================== */

int tr_qpci_parse(const unsigned char *b, size_t n, TR_QPCI *q)
{
    TR_U32 off;
    TR_U32 shape;

    memset(q, 0, sizeof(*q));
    if (b == NULL || n < CM_QP_HDR_BYTES || g32(b, CM_QP_O_MAGIC) != CM_QP_MAGIC ||
        g32(b, CM_QP_O_VERSION) != CM_QP_VERSION) {
        return 0;
    }
    q->Bytes = g32(b, CM_QP_O_BYTES);
    if (q->Bytes < CM_QP_HDR_BYTES || q->Bytes > CM_QP_MAX_BYTES) {
        return 0;
    }
    if (q->Bytes > n || (n == CM_QP_HDR_BYTES && q->Bytes != n)) {
        return -1;
    }
    off = g32(b, CM_QP_O_ENT_OFFSET);
    shape = g32(b, CM_QP_O_ENT_SHAPE);
    q->Count = shape & 0xFFFFUL;
    if ((shape >> 16) != CM_QP_ENT_BYTES || off < CM_QP_HDR_BYTES ||
        off > q->Bytes || q->Count > CM_QP_MAX_ENTRIES ||
        q->Count * CM_QP_ENT_BYTES > q->Bytes - off) {
        return 0;
    }
    q->Flags = g32(b, CM_QP_O_FLAGS);
    q->EpochLo = g32(b, CM_QP_O_EPOCH_LO);
    q->EpochHi = g32(b, CM_QP_O_EPOCH_HI);
    q->NhiBdf = g32(b, CM_QP_O_NHI_BDF);
    q->Ports = g32(b, CM_QP_O_PORTS);
    q->Reads = g32(b, CM_QP_O_READS);
    q->Fails = g32(b, CM_QP_O_FAILS);
    q->Ent = b + off;
    return 1;
}

void tr_qpci_entry(const TR_QPCI *q, TR_U32 i, TR_QPENT *e)
{
    const unsigned char *p = q->Ent + i * CM_QP_ENT_BYTES;

    e->Bdf = g32(p, CM_QP_E_BDF);
    e->Role = g32(p, CM_QP_E_ROLE);
    e->State = g32(p, CM_QP_E_STATE);
    e->Port = g32(p, CM_QP_E_PORT);
    e->Parent = g32(p, CM_QP_E_PARENT);
    e->Depth = g32(p, CM_QP_E_DEPTH);
    e->Info = g32(p, CM_QP_E_INFO);
    e->Valid = g32(p, CM_QP_E_VALID);
    e->Cfg = p + CM_QP_EHDR_BYTES;
}

static void bdf_text(char *out, size_t cap, TR_U32 bdf)
{
    if (bdf > 0xFFFFUL) {
        tr_sprintf(out, cap, "location not read");
    } else {
        tr_sprintf(out, cap, "PCI %02lX:%02lX.%lu", (bdf >> 8) & 0xFFUL,
                   (bdf >> 3) & 0x1FUL, bdf & 7UL);
    }
}

static const char *source_text(TR_U32 s)
{
    /* XHCI_TBT_SRC_* (src\cm\tbt_topo.h). */
    switch (s) {
    case 1: return "switch";
    case 2: return "listed root port";
    case 3: return "XhciCmTunnelBridge";
    case 4: return "named by the host router";
    case 5: return "SHPC bridge";
    default: return "unknown source";
    }
}

/* Why the functions below a port were not read (CM_QP_E_INFO, task
 * 42-A.14); a driver before it gave 0 on 98 SE and ME too. */
static void not_read_text(TR_TEXT *t, TR_U32 reason)
{
    switch (reason) {
    case CM_QP_NR_NO_WORKER:
        tr_put(t, "    Not read: xhcicm.sys has no configuration-manager "
                  "worker running\n    for this controller (no tunnel "
                  "route, or not started yet).\n");
        break;
    case CM_QP_NR_BUSY:
        tr_put(t, "    Not read this time: xhcicm.sys's worker is inside "
                  "a configuration-\n    manager call (a hardware wizard "
                  "may be open). Refresh later.\n");
        break;
    case CM_QP_NR_GUARD:
        tr_put(t, "    Not read: PCI.VXD's configuration read did not "
                  "pass xhcicm.sys's check.\n");
        break;
    case CM_QP_NR_HELD:
        tr_put(t, "    Not read: the controller was lost, and nothing "
                  "below the port is read\n    until it is back.\n");
        break;
    case CM_QP_NR_UNADMITTED:
        tr_put(t, "    Not walked: PCI.VXD does not read the port's bus "
                  "numbers as the driver\n    does, and xhcicm.sys did not "
                  "give them.\n");
        break;
    default:
        tr_put(t, "    Not read on this system: this driver does not "
                  "read below the port\n    on Windows 98 SE and ME.\n");
        break;
    }
}

/* An entry's state when its bytes are not shown. */
static void state_text(TR_TEXT *t, int indent, TR_U32 state)
{
    ind(t, indent);
    if (state == CM_QP_S_ABSENT) {
        tr_put(t, "No function answers there (all ones).\n");
    } else if (state == CM_QP_S_NOT_READ_OS) {
        tr_put(t, "Not read on this system.\n");
    } else if (state == CM_QP_S_FAILED) {
        tr_put(t, "The configuration read was refused.\n");
    } else {
        tr_printf(t, "State %lu, not one this tool reads.\n",
                  (unsigned long)state);
    }
}

/* One function below a port, then the functions behind it when it is a
 * bridge, depth first by each entry's Parent, so a bridge's own functions
 * follow it whatever order the walk found them in. `level` is bounded, and
 * each entry is drawn once (done[]), so a malformed answer whose parents
 * loop ends. Deeper than two bridges is drawn at the second's indent, so
 * no line passes 79 columns. */
static void below_text(TR_TEXT *t, const TR_QPCI *q, TR_U32 port,
                       TR_U32 parent, int level, unsigned char *done)
{
    TR_QPENT b;
    TR_U32 j;
    char loc[48];
    int in;

    if (level > (int)CM_QP_MAX_DEPTH) {
        return;
    }
    for (j = 0; j < q->Count; j++) {
        if (done[j]) {
            continue;
        }
        tr_qpci_entry(q, j, &b);
        if (b.Role != CM_QP_R_BELOW || b.Port != port || b.Parent != parent ||
            b.State == CM_QP_S_NOT_READ_OS) {
            continue;
        }
        done[j] = 1;
        in = 4 + 2 * (level > 2 ? 2 : level);
        bdf_text(loc, sizeof(loc), b.Bdf);
        ind(t, in);
        tr_printf(t, "%s\n", loc);
        if (b.State == CM_QP_S_READ) {
            tr_pci_text_n(t, b.Cfg, b.Valid, in + 2);
        } else {
            state_text(t, in + 2, b.State);
        }
        if (b.Bdf <= 0xFFFFUL) {
            below_text(t, q, port, b.Bdf, level + 1, done);
        }
    }
}

void tr_qpci_text(TR_TEXT *t, const TR_QPCI *q, int nt)
{
    unsigned char done[CM_QP_MAX_ENTRIES];
    TR_QPENT e;
    TR_QPENT b;
    TR_U32 i;
    TR_U32 j;
    TR_U32 below;
    TR_U32 shown;
    int walked;
    char loc[48];

    tr_put(t, "PCI configuration, read through xhcicm.sys\n");
    if (q->Flags & CM_QP_F_STOPPING) {
        tr_put(t, "  The controller is stopping.\n");
    }
    for (i = 0; i < q->Count; i++) {
        tr_qpci_entry(q, i, &e);
        if (e.Role == CM_QP_R_NHI) {
            bdf_text(loc, sizeof(loc), e.Bdf);
            tr_printf(t, "\nHost interface (NHI), %s\n", loc);
        } else if (e.Role == CM_QP_R_PORT) {
            bdf_text(loc, sizeof(loc), e.Bdf);
            tr_printf(t, "\nTunnel port, %s (%s%s)\n", loc,
                      source_text(e.Info & 0xFFUL),
                      ((e.Info >> 8) & 0x02UL) ? ", active" : "");
        } else {
            continue;
        }
        if (e.State == CM_QP_S_READ) {
            tr_pci_text_n(t, e.Cfg, e.Valid, 2);
        } else {
            state_text(t, 2, e.State);
        }
        if (e.Role != CM_QP_R_PORT || e.State != CM_QP_S_READ) {
            continue;
        }
        tr_put(t, "  Functions below this port:\n");
        below = 0;
        for (j = 0; j < q->Count; j++) {
            tr_qpci_entry(q, j, &b);
            if (b.Role != CM_QP_R_BELOW || b.Port != e.Bdf) {
                continue;
            }
            below++;
            if (b.State == CM_QP_S_NOT_READ_OS) {
                not_read_text(t, b.Info);
            }
        }
        memset(done, 0, sizeof(done));
        below_text(t, q, e.Bdf, e.Bdf, 0, done);
        /* Any the tree did not reach (a parent not in the answer): listed
         * flat, so nothing the driver read goes unshown. */
        shown = 0;
        for (j = 0; j < q->Count; j++) {
            tr_qpci_entry(q, j, &b);
            if (done[j] || b.Role != CM_QP_R_BELOW || b.Port != e.Bdf ||
                b.State == CM_QP_S_NOT_READ_OS) {
                continue;
            }
            if (shown++ == 0) {
                tr_put(t, "    Behind a bridge not listed:\n");
            }
            done[j] = 1;
            bdf_text(loc, sizeof(loc), b.Bdf);
            tr_printf(t, "      %s\n", loc);
            if (b.State == CM_QP_S_READ) {
                tr_pci_text_n(t, b.Cfg, b.Valid, 8);
            } else {
                state_text(t, 8, b.State);
            }
        }
        /* Task 43.63: on 98 SE and ME a port with nothing listed below it
         * is "None found." only where the driver says it walked it: the
         * walk made (CM_QP_F_BELOW_OS clear) and every member it did not
         * enter given its own entry, with the reason said above
         * (CM_QP_F_UNWALKED_SAID). A cut-short answer says no port is
         * empty, on any system. */
        walked = nt || ((q->Flags & CM_QP_F_BELOW_OS) == 0 &&
                        (q->Flags & CM_QP_F_UNWALKED_SAID) != 0);
        if (below != 0) {
            /* Listed above. */
        } else if (walked && e.Valid < 0x1C) {
            tr_put(t, "    Not walked: its bus numbers could not be read.\n");
        } else if (walked && (q->Flags & CM_QP_F_TRUNCATED) != 0) {
            tr_put(t, "    None listed: the walk stopped at its bounds "
                      "(below).\n");
        } else if (walked) {
            tr_put(t, "    None found.\n");
        } else if ((q->Flags & CM_QP_F_BELOW_OS) == 0) {
            tr_put(t, "    None listed: this xhcicm.sys does not say whether "
                      "it walked the port.\n");
        } else {
            tr_put(t, "    Not read on this system.\n");
        }
    }
    if (q->Flags & CM_QP_F_NO_SET) {
        tr_put(t, "\nThe driver has no tunnel ports for this host interface."
                  "\n");
    }
    if (q->Flags & CM_QP_F_TRUNCATED) {
        tr_printf(t, "\nThe walk stopped at its bounds (%lu functions, %lu "
                     "buses a port, %lu bridges\ndeep): more may be there.\n",
                  (unsigned long)CM_QP_MAX_BELOW,
                  (unsigned long)CM_QP_MAX_BUSES,
                  (unsigned long)CM_QP_MAX_DEPTH);
    }
    tr_printf(t, "\n%lu configuration reads, %lu refused.\n",
              (unsigned long)q->Reads, (unsigned long)q->Fails);
}

/* ======================================================================== */
/* xhci98.sys's PCI region                                                   */
/* ======================================================================== */

int tr_xhq_pci_text(TR_TEXT *t, const TR_XHQ *x)
{
    tr_put(t, "PCI configuration, read through xhci98.sys\n");
    if (x == NULL) {
        tr_put(t, "  Not served by this xhci98.sys: it is older than this "
                  "tool.\n");
        return 0;
    }
    if (x->PciState == XHQ_PCI_READ && x->Pci != NULL &&
        x->PciBytes == XHQ_PCI_BYTES) {
        tr_put(t, "\n");
        tr_pci_text(t, x->Pci, 2);
        return 1;
    }
    if (x->PciState == XHQ_PCI_ABSENT) {
        tr_put(t, "  Unread: the controller answered all ones (it may be "
                  "gone).\n");
    } else {
        tr_put(t, "  Unread: the driver's configuration read was refused.\n");
    }
    return 0;
}
