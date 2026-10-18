/*
 * xhci_regsrc.c - where a registry value is taken from (xhci_regsrc.h;
 * roadmap-usb4.md task 42-A.17).
 *
 * DDK-free: part of the pure core. C89. IRQL: any.
 */

#include "xhci_regsrc.h"

static ULONG xhciRegKnown(ULONG state)
{
    if (state == XHCI_REG_TAKEN || state == XHCI_REG_OTHER) {
        return state;
    }
    return XHCI_REG_ABSENT;
}

ULONG XhciRegState(ULONG seen, ULONG taken)
{
    if (taken != 0) {
        return XHCI_REG_TAKEN;
    }
    return (seen != 0) ? XHCI_REG_OTHER : XHCI_REG_ABSENT;
}

ULONG XhciRegPick(ULONG machineState, ULONG deviceState)
{
    if (xhciRegKnown(machineState) != XHCI_REG_ABSENT) {
        return XHCI_REG_FROM_MACHINE;
    }
    if (xhciRegKnown(deviceState) != XHCI_REG_ABSENT) {
        return XHCI_REG_FROM_DEVICE;
    }
    return XHCI_REG_FROM_NONE;
}

ULONG XhciRegPickedState(ULONG machineState, ULONG deviceState)
{
    ULONG from;

    from = XhciRegPick(machineState, deviceState);
    if (from == XHCI_REG_FROM_MACHINE) {
        return xhciRegKnown(machineState);
    }
    if (from == XHCI_REG_FROM_DEVICE) {
        return xhciRegKnown(deviceState);
    }
    return XHCI_REG_ABSENT;
}
