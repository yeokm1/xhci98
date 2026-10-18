/*
 * xhci_quirk.c - Linux's xHCI controller quirks, their pure half (roadmap-hcd
 * task 38.10, design record 20). See xhci_quirk.h.
 *
 * The id lists are record 20 section 3.1's, each read in Linux's
 * drivers/usb/host/xhci-pci.c (xhci_pci_quirks) at the pinned commit, the
 * vendor constants in include/linux/pci_ids.h there; the line beside each
 * list names where. Nothing of Linux's code is copied (section 6.6).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_compat.h"
#include "xhci_quirk.h"

/* PCI offset 0's dword: device 31:16, vendor 15:0. */
#define XHCI_QK_ID(vendor, device) \
    ((((ULONG)(device)) << 16) | ((ULONG)(vendor)))

#define XHCI_QK_VENDOR_VIA          0x1106UL

/* Q1, XHCI_BROKEN_MSI: Fresco Logic FL1000 (PDK) and FL1400, every
 * revision (xhci-pci.c:256-284). */
static const ULONG xhciQkMsiAvoid[] = {
    XHCI_QK_ID(0x1B73, 0x1000), XHCI_QK_ID(0x1B73, 0x1400), 0
};

/* Q2, XHCI_BROKEN_STREAMS (xhci-pci.c:273-274, 286-288, 423, 444-446, 460),
 * and Cadence's USBSSP, whose XHCI_CDNS_SCTX_QUIRK (506-508) the owner put
 * here instead (record 20 section 3.3, Q20). */
static const ULONG xhciQkStreamsOff[] = {
    XHCI_QK_ID(0x1B73, 0x1000), XHCI_QK_ID(0x1B73, 0x1009),
    XHCI_QK_ID(0x1B6F, 0x7023), XHCI_QK_ID(0x1B6F, 0x7052),
    XHCI_QK_ID(0x1106, 0x3432), XHCI_QK_ID(0x1B21, 0x1042),
    XHCI_QK_ID(0x17CD, 0x0200), 0
};

/* Q3, XHCI_RESET_ON_RESUME (xhci-pci.c:326-329, 418-425, 431-435, 436-437,
 * 439-441, 476-478, 497-499); every VIA device besides (xhciQuirkVendorWide). */
static const ULONG xhciQkResumeReinit[] = {
    XHCI_QK_ID(0x1022, 0x15E5), XHCI_QK_ID(0x1B6F, 0x7023),
    XHCI_QK_ID(0x1B6F, 0x7052), XHCI_QK_ID(0x1912, 0x0015),
    XHCI_QK_ID(0x1DB7, 0xDC27), XHCI_QK_ID(0x1B21, 0x3042),
    XHCI_QK_ID(0x1D17, 0x9202), 0
};

/* Q4, XHCI_SPURIOUS_REBOOT: Panther Point, Lynx Point-LP, Wildcat Point-LP
 * (xhci-pci.c:350-370). */
static const ULONG xhciQkShutToEhci[] = {
    XHCI_QK_ID(0x8086, 0x1E31), XHCI_QK_ID(0x8086, 0x9C31),
    XHCI_QK_ID(0x8086, 0x9CB1), 0
};

/* Q5, XHCI_SPURIOUS_WAKEUP: Lynx Point-LP, Wildcat Point-LP
 * (xhci-pci.c:365-370). */
static const ULONG xhciQkShutReset[] = {
    XHCI_QK_ID(0x8086, 0x9C31), XHCI_QK_ID(0x8086, 0x9CB1), 0
};

/* Q7, XHCI_SUSPEND_DELAY (xhci-pci.c:315-320). */
static const ULONG xhciQkHaltDelay[] = {
    XHCI_QK_ID(0x1022, 0x145C), XHCI_QK_ID(0x1022, 0x15E0),
    XHCI_QK_ID(0x1022, 0x15E1), XHCI_QK_ID(0x1022, 0x43BB), 0
};

/* Q8, XHCI_SLOW_SUSPEND: Linux's revision 04 of the FL1000, matched here at
 * every revision (record 20 section 6.3; xhci-pci.c:265-272). */
static const ULONG xhciQkHaltTimeout[] = {
    XHCI_QK_ID(0x1B73, 0x1000), 0
};

/* Q9, XHCI_ASMEDIA_MODIFY_FLOWCONTROL: the ASM1042A (xhci-pci.c:472-474). */
static const ULONG xhciQkAsmFlow[] = {
    XHCI_QK_ID(0x1B21, 0x1142), 0
};

/* Q10, XHCI_LIMIT_ENDPOINT_INTERVAL_7: TI's TUSB73x0 (xhci-pci.c:480-481). */
static const ULONG xhciQkTiCap[] = {
    XHCI_QK_ID(0x104C, 0x8241), 0
};

/* Q12, XHCI_MISSING_CAS: Cherryview, Sunrise Point-LP and -H, Apollo Lake,
 * Denverton (xhci-pci.c:390-396). */
static const ULONG xhciQkStuckLink[] = {
    XHCI_QK_ID(0x8086, 0x22B5), XHCI_QK_ID(0x8086, 0x9D2F),
    XHCI_QK_ID(0x8086, 0xA12F), XHCI_QK_ID(0x8086, 0x5AA8),
    XHCI_QK_ID(0x8086, 0x19D0), 0
};

/* Q13, XHCI_TRB_OVERFETCH: the VIA VL805 and Zhaoxin's two
 * (xhci-pci.c:448-451, 497-504). */
static const ULONG xhciQkRingPad[] = {
    XHCI_QK_ID(0x1106, 0x3483), XHCI_QK_ID(0x1D17, 0x9202),
    XHCI_QK_ID(0x1D17, 0x9203), 0
};

/* Q14a and Q14c, XHCI_ETRON_HOST: the EJ168 and EJ188
 * (xhci-pci.c:418-425). */
static const ULONG xhciQkEtron[] = {
    XHCI_QK_ID(0x1B6F, 0x7023), XHCI_QK_ID(0x1B6F, 0x7052), 0
};

/* Q16, XHCI_DISABLE_SPARSE: AMD Raven2 (xhci-pci.c:326-329). */
static const ULONG xhciQkAmdBit[] = {
    XHCI_QK_ID(0x1022, 0x15E5), 0
};

typedef struct _XHCI_QK_DEF {
    const char *Flag;
    const char *Label;
    const ULONG *Ids;
    ULONG Default;
    ULONG TwoOffered;
} XHCI_QK_DEF;

#define XHCI_QK_NARROW(s) s,

static const char *const xhciQkNames[] = {
    XHCI_QK_VALUE_NAMES(XHCI_QK_NARROW)
};
XHCI_C_ASSERT(quirk_names_count,
              sizeof(xhciQkNames) / sizeof(xhciQkNames[0]) == XHCI_QK_COUNT);

static const XHCI_QK_DEF xhciQkDefs[XHCI_QK_COUNT] = {
    { "XHCI_BROKEN_MSI", "quirk.msiavoid",
      xhciQkMsiAvoid, 1, 1 },
    { "XHCI_BROKEN_STREAMS", "quirk.streamsoff",
      xhciQkStreamsOff, 1, 1 },
    { "XHCI_RESET_ON_RESUME", "quirk.resumereinit",
      xhciQkResumeReinit, 1, 1 },
    { "XHCI_SPURIOUS_REBOOT", "quirk.shuttoehci",
      xhciQkShutToEhci, 1, 0 },
    { "XHCI_SPURIOUS_WAKEUP", "quirk.shutreset",
      xhciQkShutReset, 0, 1 },
    { "XHCI_SUSPEND_DELAY", "quirk.haltdelay",
      xhciQkHaltDelay, 1, 1 },
    { "XHCI_SLOW_SUSPEND", "quirk.halttimeout",
      xhciQkHaltTimeout, 1, 1 },
    { "XHCI_ASMEDIA_MODIFY_FLOWCONTROL",
      "quirk.asmflow", xhciQkAsmFlow, 1, 0 },
    { "XHCI_LIMIT_ENDPOINT_INTERVAL_7", "quirk.ticap",
      xhciQkTiCap, 1, 1 },
    { "XHCI_MISSING_CAS", "quirk.stucklink",
      xhciQkStuckLink, 1, 1 },
    { "XHCI_TRB_OVERFETCH", "quirk.ringpad",
      xhciQkRingPad, 1, 1 },
    { "XHCI_ETRON_HOST", "quirk.linknoop",
      xhciQkEtron, 1, 1 },
    { "XHCI_ETRON_HOST", "quirk.isochsuccess",
      xhciQkEtron, 1, 1 },
    { "XHCI_DISABLE_SPARSE", "quirk.amdbit",
      xhciQkAmdBit, 1, 0 }
};

const char *XhciQuirkValueName(ULONG quirk)
{
    return (quirk < XHCI_QK_COUNT) ? xhciQkNames[quirk] : NULL;
}

const char *XhciQuirkLinuxFlag(ULONG quirk)
{
    return (quirk < XHCI_QK_COUNT) ? xhciQkDefs[quirk].Flag : NULL;
}

const char *XhciQuirkLabel(ULONG quirk)
{
    return (quirk < XHCI_QK_COUNT) ? xhciQkDefs[quirk].Label : NULL;
}

ULONG XhciQuirkDefault(ULONG quirk)
{
    return (quirk < XHCI_QK_COUNT) ? xhciQkDefs[quirk].Default : 0UL;
}

ULONG XhciQuirkTwoOffered(ULONG quirk)
{
    return (quirk < XHCI_QK_COUNT) ? xhciQkDefs[quirk].TwoOffered : 0UL;
}

ULONG XhciQuirkMode(ULONG quirk, ULONG found, ULONG value, PULONG norm)
{
    ULONG why;
    ULONG mode;

    if (quirk >= XHCI_QK_COUNT) {
        if (norm != NULL) {
            *norm = XHCI_QK_NORM_ABSENT;
        }
        return XHCI_QK_MODE_OFF;
    }
    mode = xhciQkDefs[quirk].Default;
    if (found == XHCI_QK_FOUND_OTHER) {
        why = XHCI_QK_NORM_TYPE;
    } else if (found != XHCI_QK_FOUND_DWORD) {
        why = XHCI_QK_NORM_ABSENT;
    } else if (value == XHCI_QK_MODE_OFF || value == XHCI_QK_MODE_LISTED) {
        why = XHCI_QK_NORM_READ;
        mode = value;
    } else if (value == XHCI_QK_MODE_EVERY) {
        if (xhciQkDefs[quirk].TwoOffered) {
            why = XHCI_QK_NORM_READ;
            mode = XHCI_QK_MODE_EVERY;
        } else {
            why = XHCI_QK_NORM_TWO_AS_ONE;
            mode = XHCI_QK_MODE_LISTED;
        }
    } else {
        why = XHCI_QK_NORM_NUMBER;
    }
    if (norm != NULL) {
        *norm = why;
    }
    return mode;
}

/* Vendor-wide rows: VIA for XhciResumeReinit alone (xhci-pci.c:436-437). */
static ULONG xhciQuirkVendorWide(ULONG quirk, ULONG vendorDevice)
{
    return (quirk == XHCI_QK_RESUME_REINIT &&
            (vendorDevice & 0xFFFFUL) == XHCI_QK_VENDOR_VIA) ? 1UL : 0UL;
}

ULONG XhciQuirkMatches(ULONG quirk, ULONG vendorDevice)
{
    const ULONG *id;

    if (quirk >= XHCI_QK_COUNT || vendorDevice == 0 ||
        vendorDevice == 0xFFFFFFFFUL) {
        return 0;
    }
    if (xhciQuirkVendorWide(quirk, vendorDevice)) {
        return 1;
    }
    for (id = xhciQkDefs[quirk].Ids; *id != 0; id++) {
        if (*id == vendorDevice) {
            return 1;
        }
    }
    return 0;
}

ULONG XhciQuirkEffective(ULONG mode, ULONG matched)
{
    if (mode == XHCI_QK_MODE_EVERY) {
        return 1;
    }
    return (mode == XHCI_QK_MODE_LISTED && matched) ? 1UL : 0UL;
}

VOID XhciQuirkInit(PXHCI_QUIRK_STATE st)
{
    PUCHAR p;
    ULONG i;

    if (st == NULL) {
        return;
    }
    p = (PUCHAR)st;
    for (i = 0; i < sizeof(XHCI_QUIRK_STATE); i++) {
        p[i] = 0;
    }
    for (i = 0; i < XHCI_QK_COUNT; i++) {
        st->Raw[i] = XHCI_QK_RAW_NONE;
        st->Mode[i] = xhciQkDefs[i].Default;
        st->Norm[i] = XHCI_QK_NORM_ABSENT;
    }
    st->PciRevision = 0xFFFFFFFFUL;
}

VOID XhciQuirkSetValue(PXHCI_QUIRK_STATE st, ULONG quirk, ULONG found,
                       ULONG value)
{
    ULONG norm;

    if (st == NULL || quirk >= XHCI_QK_COUNT) {
        return;
    }
    st->Raw[quirk] = (found == XHCI_QK_FOUND_DWORD) ? value
                                                     : XHCI_QK_RAW_NONE;
    st->Mode[quirk] = XhciQuirkMode(quirk, found, value, &norm);
    st->Norm[quirk] = norm;
}

VOID XhciQuirkSetEffective(PXHCI_QUIRK_STATE st, ULONG quirk, ULONG matched,
                           ULONG on, ULONG overridden)
{
    if (st == NULL || quirk >= XHCI_QK_COUNT) {
        return;
    }
    st->Matched[quirk] = matched ? 1UL : 0UL;
    st->On[quirk] = on ? 1UL : 0UL;
    st->Overridden[quirk] = overridden ? 1UL : 0UL;
}

VOID XhciQuirkMatchAll(PXHCI_QUIRK_STATE st, ULONG vendorDevice)
{
    ULONG matched;
    ULONG i;

    if (st == NULL) {
        return;
    }
    st->Identity = vendorDevice;
    for (i = 0; i < XHCI_QK_COUNT; i++) {
        if (i == XHCI_QK_MSI_AVOID) {
            continue;
        }
        matched = XhciQuirkMatches(i, vendorDevice);
        XhciQuirkSetEffective(st, i, matched,
                              XhciQuirkEffective(st->Mode[i], matched), 0);
    }
}

ULONG XhciQuirkOn(const XHCI_QUIRK_STATE *st, ULONG quirk)
{
    if (st == NULL || quirk >= XHCI_QK_COUNT) {
        return 0;
    }
    return st->On[quirk];
}

VOID XhciQuirkOutcome(PXHCI_QUIRK_STATE st, ULONG quirk, ULONG outcome,
                      ULONG why)
{
    if (st == NULL || quirk >= XHCI_QK_COUNT) {
        return;
    }
    st->Outcome[quirk] = outcome & 0xFUL;
    st->Why[quirk] = why & 0xFUL;
}

ULONG XhciQuirkPacked(const XHCI_QUIRK_STATE *st, ULONG quirk)
{
    ULONG w;
    ULONG raw;

    if (st == NULL || quirk >= XHCI_QK_COUNT) {
        return 0;
    }
    raw = (st->Raw[quirk] == XHCI_QK_RAW_NONE) ? 0xFFFFUL
                                                : (st->Raw[quirk] & 0xFFFFUL);
    w = (st->Mode[quirk] & 0x3UL) | ((st->Norm[quirk] & 0x7UL) << 2);
    if (st->Matched[quirk]) {
        w |= XHCI_QK_PACK_MATCHED;
    }
    if (st->On[quirk]) {
        w |= XHCI_QK_PACK_EFFECTIVE;
    }
    if (st->Overridden[quirk]) {
        w |= XHCI_QK_PACK_OVERRIDDEN;
    }
    w |= (st->Outcome[quirk] & 0xFUL) << 8;
    w |= (st->Why[quirk] & 0xFUL) << 12;
    w |= raw << 16;
    return w;
}

ULONG XhciQuirkMsiAvoidDecide(ULONG mode, ULONG identityKnown,
                              ULONG vendorDevice)
{
    if (mode == XHCI_QK_MODE_EVERY) {
        return 1;
    }
    if (mode != XHCI_QK_MODE_LISTED) {
        return 0;
    }
    if (!identityKnown) {
        return 1;
    }
    return XhciQuirkMatches(XHCI_QK_MSI_AVOID, vendorDevice);
}

ULONG XhciQuirkStreamsHcc1(ULONG streamsOff, ULONG hccparams1)
{
    return streamsOff ? (hccparams1 & ~0x0000F000UL) : hccparams1;
}
