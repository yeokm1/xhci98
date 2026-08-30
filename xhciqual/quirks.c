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

    /* NEC / Renesas */
    { 0x1033, 0x0194, QF_FW_SPI | QF_SPURIOUS | QF_CMD_RETRY,
      "NEC uPD720200/200A (fw on card SPI flash)" },
    { 0x1912, 0x0014, QF_FW_UPLOAD | QF_CMD_RETRY,
      "Renesas uPD720201 (fw upload if ROM-less)" },
    { 0x1912, 0x0015, QF_FW_UPLOAD | QF_CMD_RETRY,
      "Renesas uPD720202 (fw upload if ROM-less)" },

    /* ASMedia */
    { 0x1B21, 0x1042, QF_BULK64K, "ASMedia ASM1042" },
    { 0x1B21, 0x1142, 0,          "ASMedia ASM1142" },
    { 0x1B21, 0x2142, 0,          "ASMedia ASM2142" },

    /* Fresco Logic */
    { 0x1D5C, 0x1000, QF_SPURIOUS | QF_BROKEN_MSI, "Fresco Logic FL1000" },
    { 0x1D5C, 0x1009, QF_BROKEN_MSI,               "Fresco Logic FL1009" },
    { 0x1D5C, 0x1100, QF_BROKEN_MSI,               "Fresco Logic FL1100" },

    /* VIA Labs */
    { 0x2109, 0x0100, QF_SPURIOUS, "VIA Labs VL800" },
    { 0x2109, 0x0812, 0,           "VIA Labs VL805" },
    { 0x2109, 0x0813, 0,           "VIA Labs VL806" },

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
