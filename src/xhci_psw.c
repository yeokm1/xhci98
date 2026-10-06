/*
 * xhci_psw.c - the Intel EHCI-to-xHCI port switchover, the pure half
 * (xhci_psw.h; roadmap-hcd.md tasks 34.3 and 35.5).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_psw.h"

#define XHCI_PSW_VENDOR_INTEL   0x8086UL
#define XHCI_PSW_VENDOR_SONY    0x104DUL
#define XHCI_PSW_SONY_VAIO_T    0x90A8UL
#define XHCI_PSW_UNREAD         0xFFFFFFFFUL

/* Panther Point, Lynx Point, Lynx Point-LP, Wildcat Point, Wildcat
 * Point-LP: the 7-, 8- and 9-series PCH xHCI (Linux xhci-pci.c's
 * PCI_DEVICE_ID_INTEL_* for the -LP parts; xhciqual's quirks.c rows); and
 * Wellsburg, the C610/X99 PCH, which ships an EHCI beside its xHCI
 * (xhciqual's QF_XUSB2PR row; owner, 2026-10-05). For Wildcat Point and
 * Wellsburg the support is Linux's one sequence for every Intel xHCI with
 * an EHCI and xhciqual's reading of them as such parts, not a datasheet or
 * reading of their own (design record 16 section 4). Linux's rule would
 * also reach other Intel parts with an EHCI; none is listed here. */
static const USHORT xhciPswDevices[] = {
    0x1E31, 0x8C31, 0x9C31, 0x8CB1, 0x9CB1, 0x8D31
};

ULONG XhciPswGate(ULONG vendorDevice)
{
    ULONG device;
    ULONG i;

    if ((vendorDevice & 0xFFFFUL) != XHCI_PSW_VENDOR_INTEL) {
        return 0;
    }
    device = (vendorDevice >> 16) & 0xFFFFUL;
    for (i = 0; i < sizeof(xhciPswDevices) / sizeof(xhciPswDevices[0]);
         i++) {
        if (device == (ULONG)xhciPswDevices[i]) {
            return 1;
        }
    }
    return 0;
}

ULONG XhciPswBoardRefused(ULONG subsystem)
{
    return (subsystem & 0xFFFFUL) == XHCI_PSW_VENDOR_SONY &&
           ((subsystem >> 16) & 0xFFFFUL) == XHCI_PSW_SONY_VAIO_T;
}

ULONG XhciPswIntel(ULONG vendorDevice)
{
    return (vendorDevice & 0xFFFFUL) == XHCI_PSW_VENDOR_INTEL;
}

/* Only an exact 2 reaches unlisted silicon, so it has to be typed on
 * purpose; any other number keeps the gated meaning (record 16 section 5). */
ULONG XhciPswMode(ULONG vendorDevice, ULONG found, ULONG value)
{
    if (!XhciPswIntel(vendorDevice)) {
        return XHCI_PSW_MODE_OFF;
    }
    if (XhciPswGate(vendorDevice)) {
        return (found && value == 0) ? XHCI_PSW_MODE_OFF
                                     : XHCI_PSW_MODE_GATED;
    }
    return (found && value == 2) ? XHCI_PSW_MODE_BYPASS : XHCI_PSW_MODE_OFF;
}

static VOID xhciPswReset(PXHCI_PSW_STATE st)
{
    st->Usb3Mask = XHCI_PSW_UNREAD;
    st->Usb3Now = XHCI_PSW_UNREAD;
    st->Usb2Mask = XHCI_PSW_UNREAD;
    st->Usb2Now = XHCI_PSW_UNREAD;
    st->Step = XHCI_PSW_DONE;
    st->Written = 0;
}

/* A mask is refused when it could not be read or reads all-ones, which is
 * what an access nothing decodes returns: no PCH has 32 switchable ports,
 * and writing it would claim every bit of the register. */
static ULONG xhciPswMask(const XHCI_PSW_IO *io, ULONG offset, PULONG mask)
{
    ULONG value;

    value = XHCI_PSW_UNREAD;
    if (!io->Read(io->Context, offset, &value)) {
        *mask = XHCI_PSW_UNREAD;
        return 0;
    }
    *mask = value;
    return value != XHCI_PSW_UNREAD;
}

ULONG XhciPswRoute(const XHCI_PSW_IO *io, PXHCI_PSW_STATE st)
{
    if (st == NULL) {
        return XHCI_PSW_STEP_BAD_PARAM;
    }
    xhciPswReset(st);
    if (io == NULL || io->Read == NULL || io->Write == NULL) {
        st->Step = XHCI_PSW_STEP_BAD_PARAM;
        return st->Step;
    }

    if (!xhciPswMask(io, XHCI_PSW_USB3PRM, &st->Usb3Mask)) {
        st->Step = XHCI_PSW_STEP_USB3PRM;
        return st->Step;
    }
    if (!io->Write(io->Context, XHCI_PSW_USB3_PSSEN, st->Usb3Mask)) {
        st->Step = XHCI_PSW_STEP_PSSEN_WRITE;
        return st->Step;
    }
    st->Written |= XHCI_PSW_WROTE_PSSEN;
    /* The read-back is the log's, not a gate: the write was accepted, so
     * the USB 2.0 pairs follow the terminations. */
    if (!io->Read(io->Context, XHCI_PSW_USB3_PSSEN, &st->Usb3Now)) {
        st->Usb3Now = XHCI_PSW_UNREAD;
        st->Step = XHCI_PSW_STEP_PSSEN_READ;
    }

    if (!xhciPswMask(io, XHCI_PSW_XUSB2PRM, &st->Usb2Mask)) {
        st->Step = XHCI_PSW_STEP_XUSB2PRM;
        return st->Step;
    }
    if (!io->Write(io->Context, XHCI_PSW_XUSB2PR, st->Usb2Mask)) {
        st->Step = XHCI_PSW_STEP_XUSB2PR_WRITE;
        return st->Step;
    }
    st->Written |= XHCI_PSW_WROTE_XUSB2PR;
    if (!io->Read(io->Context, XHCI_PSW_XUSB2PR, &st->Usb2Now)) {
        st->Usb2Now = XHCI_PSW_UNREAD;
        if (st->Step == XHCI_PSW_DONE) {
            st->Step = XHCI_PSW_STEP_XUSB2PR_READ;
        }
    }
    return st->Step;
}

/* Record the first failure only. */
static VOID xhciPswFail(PXHCI_PSW_STATE st, ULONG step)
{
    if (st->Step == XHCI_PSW_DONE) {
        st->Step = step;
    }
}

ULONG XhciPswRelease(const XHCI_PSW_IO *io, ULONG written,
                     PXHCI_PSW_STATE st)
{
    if (st == NULL) {
        return XHCI_PSW_STEP_BAD_PARAM;
    }
    xhciPswReset(st);
    if (io == NULL || io->Read == NULL || io->Write == NULL) {
        st->Step = XHCI_PSW_STEP_BAD_PARAM;
        return st->Step;
    }

    if (written & XHCI_PSW_WROTE_PSSEN) {
        if (!io->Write(io->Context, XHCI_PSW_USB3_PSSEN, 0)) {
            xhciPswFail(st, XHCI_PSW_STEP_PSSEN_WRITE);
        } else if (!io->Read(io->Context, XHCI_PSW_USB3_PSSEN,
                             &st->Usb3Now)) {
            st->Usb3Now = XHCI_PSW_UNREAD;
            xhciPswFail(st, XHCI_PSW_STEP_PSSEN_READ);
        }
    }
    if (written & XHCI_PSW_WROTE_XUSB2PR) {
        if (!io->Write(io->Context, XHCI_PSW_XUSB2PR, 0)) {
            xhciPswFail(st, XHCI_PSW_STEP_XUSB2PR_WRITE);
        } else if (!io->Read(io->Context, XHCI_PSW_XUSB2PR,
                             &st->Usb2Now)) {
            st->Usb2Now = XHCI_PSW_UNREAD;
            xhciPswFail(st, XHCI_PSW_STEP_XUSB2PR_READ);
        }
    }
    return st->Step;
}

/* ------------------------------------------------------------------------ */
/* The lifetime (record 16 section 7a)                                       */
/* ------------------------------------------------------------------------ */

static VOID xhciPswLifeClear(PXHCI_PSW_LIFE life)
{
    life->On = 0;
    life->Mode = XHCI_PSW_MODE_OFF;
    life->Written = 0;
    life->Decision = XHCI_PSW_DECIDE_NONE;
    life->Id = XHCI_PSW_UNREAD;
    life->Found = 0;
    life->Value = 0;
    life->Subsystem = XHCI_PSW_UNREAD;
}

ULONG XhciPswLifeStart(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                       PXHCI_PSW_STATE st)
{
    ULONG value;

    if (st != NULL) {
        xhciPswReset(st);
    }
    if (life == NULL) {
        return 0;
    }
    xhciPswLifeClear(life);
    if (st == NULL || io == NULL || io->Read == NULL || io->Write == NULL ||
        io->Value == NULL) {
        return 0;
    }

    if (!io->Read(io->Context, XHCI_PSW_PCI_ID, &life->Id)) {
        life->Id = XHCI_PSW_UNREAD;
        life->Decision = XHCI_PSW_DECIDE_ID_UNREAD;
        return 0;
    }
    if (!XhciPswIntel(life->Id)) {
        life->Decision = XHCI_PSW_DECIDE_NOT_INTEL;
        return 0;
    }
    value = 0;
    life->Found = io->Value(io->Context, &value) ? 1 : 0;
    life->Value = life->Found ? value : 0;
    life->Mode = XhciPswMode(life->Id, life->Found, life->Value);
    if (life->Mode == XHCI_PSW_MODE_OFF) {
        life->Decision = XHCI_PSW_DECIDE_OFF;
        return 0;
    }

    /* The exemption holds only on a reading: a subsystem id that could not
     * be read is no evidence the board is not the one that cannot switch
     * (record 16 section 4); value 2 keeps it (section 4a). */
    if (!io->Read(io->Context, XHCI_PSW_PCI_SUBSYSTEM, &life->Subsystem)) {
        life->Subsystem = XHCI_PSW_UNREAD;
        life->Decision = XHCI_PSW_DECIDE_BOARD_UNREAD;
        return 0;
    }
    if (XhciPswBoardRefused(life->Subsystem)) {
        life->Decision = XHCI_PSW_DECIDE_BOARD_REFUSED;
        return 0;
    }

    life->On = 1;
    life->Decision = XHCI_PSW_DECIDE_ON;
    (VOID)XhciPswRoute(io, st);
    life->Written |= st->Written;
    return 1;
}

/* A later route that fails does not clear what an earlier one wrote: the
 * register may still hold the earlier value (record 16 section 6). */
ULONG XhciPswLifeResume(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                        PXHCI_PSW_STATE st)
{
    if (st == NULL) {
        return 0;
    }
    xhciPswReset(st);
    if (life == NULL || !life->On) {
        return 0;
    }
    (VOID)XhciPswRoute(io, st);
    life->Written |= st->Written;
    return 1;
}

/* Written is kept: a refused zero at a shutdown's D3 is retried by the
 * stop that may follow it. */
ULONG XhciPswLifeRelease(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                         PXHCI_PSW_STATE st)
{
    if (st == NULL) {
        return 0;
    }
    xhciPswReset(st);
    if (life == NULL || life->Written == 0) {
        return 0;
    }
    (VOID)XhciPswRelease(io, life->Written, st);
    return 1;
}

ULONG XhciPswLifeEnd(const XHCI_PSW_IO *io, PXHCI_PSW_LIFE life,
                     PXHCI_PSW_STATE st)
{
    ULONG released;

    released = XhciPswLifeRelease(io, life, st);
    if (life != NULL) {
        life->On = 0;
        life->Written = 0;
    }
    return released;
}
