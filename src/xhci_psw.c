/*
 * xhci_psw.c - the Intel EHCI-to-xHCI port switchover, the pure half
 * (xhci_psw.h; roadmap-hcd.md task 34.4).
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

ULONG XhciPswEnabled(ULONG found, ULONG value)
{
    return !(found && value == 0);
}

static VOID xhciPswReset(PXHCI_PSW_STATE st)
{
    st->Usb3Mask = XHCI_PSW_UNREAD;
    st->Usb3Now = XHCI_PSW_UNREAD;
    st->Usb2Mask = XHCI_PSW_UNREAD;
    st->Usb2Now = XHCI_PSW_UNREAD;
    st->Step = XHCI_PSW_DONE;
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

ULONG XhciPswRelease(const XHCI_PSW_IO *io, PXHCI_PSW_STATE st)
{
    if (st == NULL) {
        return XHCI_PSW_STEP_BAD_PARAM;
    }
    xhciPswReset(st);
    if (io == NULL || io->Read == NULL || io->Write == NULL) {
        st->Step = XHCI_PSW_STEP_BAD_PARAM;
        return st->Step;
    }

    if (!io->Write(io->Context, XHCI_PSW_USB3_PSSEN, 0)) {
        xhciPswFail(st, XHCI_PSW_STEP_PSSEN_WRITE);
    } else if (!io->Read(io->Context, XHCI_PSW_USB3_PSSEN, &st->Usb3Now)) {
        st->Usb3Now = XHCI_PSW_UNREAD;
        xhciPswFail(st, XHCI_PSW_STEP_PSSEN_READ);
    }
    if (!io->Write(io->Context, XHCI_PSW_XUSB2PR, 0)) {
        xhciPswFail(st, XHCI_PSW_STEP_XUSB2PR_WRITE);
    } else if (!io->Read(io->Context, XHCI_PSW_XUSB2PR, &st->Usb2Now)) {
        st->Usb2Now = XHCI_PSW_UNREAD;
        xhciPswFail(st, XHCI_PSW_STEP_XUSB2PR_READ);
    }
    return st->Step;
}
