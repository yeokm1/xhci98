/*
 * quirks.c - VID/DID -> known-quirk lookup.
 *
 * Entries and flags come from Linux drivers/usb/host/xhci-pci.c and
 * pci-quirks.c directly. This project keeps no quirk catalogue of its own and
 * the driver acts on none of these; the tool reports them. Grow this table as
 * real controllers are tested.
 */

#include <stddef.h>
#include "qual.h"

static const QUIRK quirk_table[] = {
    /* Intel 7/8-series: EHCI<->xHCI mux via XUSB2PR (config 0xD0) */
    { 0x8086, 0x1E31, QF_XUSB2PR | QF_COMPLIANCE | QF_BEI,
      "Intel Panther Point (7-series PCH)" },
    { 0x8086, 0x8C31, QF_XUSB2PR | QF_COMPLIANCE | QF_BEI | QF_PME_STUCK,
      "Intel Lynx Point (8-series PCH)" },
    { 0x8086, 0x8CB1, QF_XUSB2PR | QF_COMPLIANCE | QF_BEI | QF_PME_STUCK,
      "Intel Wildcat Point (9-series PCH)" },
    { 0x8086, 0x8D31, QF_XUSB2PR | QF_COMPLIANCE | QF_BEI | QF_PME_STUCK,
      "Intel Wellsburg (C610 PCH)" },

    /* Intel Skylake+ (no EHCI, no XUSB2PR - ports hardwired to xHCI) */
    { 0x8086, 0xA12F, QF_PME_STUCK, "Intel Sunrise Point-H (100-series)" },
    { 0x8086, 0x9D2F, QF_PME_STUCK, "Intel Sunrise Point-LP (100-series)" },
    { 0x8086, 0xA2AF, QF_PME_STUCK, "Intel Union Point (200-series)" },
    { 0x8086, 0xA36D, QF_PME_STUCK, "Intel Cannon Point (300-series)" },

    /* NEC / Renesas. Linux's XHCI_NEC_HOST (every 0x1033 xHCI) does two
     * things: it queues a vendor NEC Get Firmware command at start and logs
     * the version the card's own firmware answers with, which is what QF_FW_SPI
     * reports (nothing is uploaded), and it sets the chain bit on isoch link
     * TRBs. It sets no spurious-success or command-retry quirk; the row
     * carried QF_SPURIOUS | QF_CMD_RETRY until the 2026-09-17 audit. */
    { 0x1033, 0x0194, QF_FW_SPI,
      "NEC uPD720200/200A (fw on card SPI flash)" },
    { 0x1912, 0x0014, QF_FW_UPLOAD | QF_CMD_RETRY,
      "Renesas uPD720201 (fw upload if ROM-less)" },
    { 0x1912, 0x0015, QF_FW_UPLOAD | QF_CMD_RETRY,
      "Renesas uPD720202 (fw upload if ROM-less)" },

    /* ASMedia. Linux's PCI_DEVICE_ID_ASMEDIA_* names: 0x1142 is the ASM1042A
     * and 0x1242 the ASM1142; the three newer parts carry only
     * XHCI_NO_64BIT_SUPPORT, which cannot matter to a 32-bit-only driver.
     * The ASM1042 gets XHCI_SPURIOUS_SUCCESS and XHCI_BROKEN_STREAMS (no
     * flag here for streams). It carried QF_BULK64K instead until the
     * 2026-09-17 audit; no source in this repository or in Linux supports a
     * 64 KB bulk limit on it. */
    { 0x1B21, 0x1042, QF_SPURIOUS, "ASMedia ASM1042" },
    { 0x1B21, 0x1142, 0,          "ASMedia ASM1042A" },
    { 0x1B21, 0x1242, 0,          "ASMedia ASM1142" },
    { 0x1B21, 0x2142, 0,          "ASMedia ASM2142" },

    /* Fresco Logic, PCI vendor 0x1B73. (The rows carried 0x1D5C, Fresco's
     * USB-IF vendor ID, until the 2026-09-17 audit, so no Fresco controller
     * ever matched.) Linux sets XHCI_BROKEN_MSI for the FL1000/PDK and the
     * FL1400 only; the FL1009 and the FL1000 get XHCI_BROKEN_STREAMS, which
     * this USB 2.0-only project has no flag for. */
    { 0x1B73, 0x1000, QF_BROKEN_MSI, "Fresco Logic FL1000 (PDK)" },
    { 0x1B73, 0x1009, 0,             "Fresco Logic FL1009" },
    { 0x1B73, 0x1100, 0,             "Fresco Logic FL1100" },
    { 0x1B73, 0x1400, QF_BROKEN_MSI, "Fresco Logic FL1400" },

    /* VIA Labs, PCI vendor 0x1106. (The rows carried 0x2109, VIA's USB-IF
     * vendor ID, with the VL812/VL813 hub product IDs, until the 2026-09-17
     * audit.) Linux gives every VIA xHCI XHCI_RESET_ON_RESUME, the VL800
     * XHCI_BROKEN_STREAMS and the VL805 XHCI_TRB_OVERFETCH; none of those
     * has a flag here. */
    { 0x1106, 0x3432, 0, "VIA Labs VL800" },
    { 0x1106, 0x3483, 0, "VIA Labs VL805" },

    /* Etron */
    { 0x1B6F, 0x7023, QF_AVOID, "Etron EJ168" },
    { 0x1B6F, 0x7052, QF_AVOID, "Etron EJ188" },

    { 0, 0, 0, NULL }
};

const QUIRK *quirk_find(u16 vid, u16 did)
{
    const QUIRK *q;

    for (q = quirk_table; q->name != NULL; q++) {
        if (q->vid == vid && q->did == did)
            return q;
    }
    return NULL;
}
