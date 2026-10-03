/*
 * hcd_urb.c - the device PDOs' function-driver contract: the internal device
 * controls a USB function driver sends its PDO (roadmap-hcd.md tasks 26-A.5
 * and 26-A.6; design record 13 section 6).
 *
 * THIS IS THE ENTRY, NOT YET THE TRANSFERS. Every IRP_MJ_INTERNAL_DEVICE_CONTROL
 * reaching a device PDO arrives here and is counted by its control code and,
 * for IOCTL_INTERNAL_USB_SUBMIT_URB, by its URB function, with the first
 * occurrence of each function traced, so a guest run shows what each target's
 * class drivers send before any of it is served. GET_PORT_STATUS is answered
 * from the port's PORTSC. Every URB is still refused - as an unknown function
 * the controller does not serve, USBD_STATUS_INVALID_URB_FUNCTION with
 * STATUS_INVALID_PARAMETER - until the transfer path lands (design record 13
 * section 6.3 lists which functions each target's drivers send).
 *
 * IRQL: <= DISPATCH_LEVEL (a function driver may send these at DISPATCH).
 */

/* The DDK's USB headers come first: WDK 7.1's usb200.h declares
 * UsbLowSpeed..UsbHighSpeed as enum members, which xhci_usbport.h (through
 * hcd.h) defines as macros for the kept files. */
#include <ntddk.h>
#include <usbdi.h>
#include <usbioctl.h>
#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/* Absent from the Windows 2000 DDK's usbdi.h; values as WDK 7.1's
 * inc\api\usb.h defines them (lines 459 and 439). */
#ifndef USBD_STATUS_DEVICE_GONE
#define USBD_STATUS_DEVICE_GONE    ((USBD_STATUS)0xC0007000L)
#endif

/* The first time each URB function reaches any PDO of this image, traced
 * once; the counts are per controller. */
static ULONG hcdUrbSeen[(HCD_URB_FUNCTIONS + 31UL) / 32UL];

static NTSTATUS hcdUrbComplete(PIRP irp, PURB urb, USBD_STATUS usbd,
                               NTSTATUS status)
{
    urb->UrbHeader.Status = usbd;
    return HcdCompleteIrp(irp, status, 0);
}

static NTSTATUS hcdSubmitUrb(PHCD_DEVICE_PDO pdo, PIRP irp, PURB urb)
{
    PHCD_CONTROLLER hc;
    ULONG function;
    ULONG bit;

    if (urb == NULL) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    function = urb->UrbHeader.Function;
    hc = pdo->Controller;
    if (function < HCD_URB_FUNCTIONS) {
        bit = 1UL << (function % 32UL);
        if ((hcdUrbSeen[function / 32UL] & bit) == 0) {
            hcdUrbSeen[function / 32UL] |= bit;
            XHCI_DBG_VALUE("hcd: first URB of function", function);
        }
        if (hc != NULL) {
            hc->UrbCount[function]++;
        }
    } else if (hc != NULL) {
        hc->UrbUnknown++;
    }

    if (hc == NULL || !pdo->Listed) {
        /* Orphaned, or its device has left: nothing on the bus answers. */
        return hcdUrbComplete(irp, urb, USBD_STATUS_DEVICE_GONE,
                              STATUS_DEVICE_NOT_CONNECTED);
    }
    return hcdUrbComplete(irp, urb, USBD_STATUS_INVALID_URB_FUNCTION,
                          STATUS_INVALID_PARAMETER);
}

/*
 * IOCTL_INTERNAL_USB_GET_PORT_STATUS: USBD_PORT_CONNECTED and
 * USBD_PORT_ENABLED from the root port's PORTSC (CCS and PED), read only
 * while the controller runs in D0; a gone or orphaned PDO's port reads 0.
 */
static NTSTATUS hcdPortStatus(PHCD_DEVICE_PDO pdo, PIRP irp, PULONG out)
{
    PHCD_CONTROLLER hc;
    ULONG portsc;
    ULONG bits;

    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    bits = 0;
    hc = pdo->Controller;
    if (hc != NULL && pdo->Listed &&
        (hc->Hc.Flags & XHCI_EXT_FLAG_STARTED) != 0 &&
        hc->Common.DevicePower == PowerDeviceD0 && !hc->SuspendedInD0) {
        portsc = XhciReadPortsc(&hc->Hc, pdo->Port);
        if (portsc != 0xFFFFFFFFUL) {
            if ((portsc & XHCI_PORTSC_CCS) != 0) {
                bits |= USBD_PORT_CONNECTED;
            }
            if ((portsc & XHCI_PORTSC_PED) != 0) {
                bits |= USBD_PORT_ENABLED;
            }
        }
    }
    *out = bits;
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

NTSTATUS HcdDevicePdoInternalIoctl(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PHCD_CONTROLLER hc;
    ULONG code;

    stack = IoGetCurrentIrpStackLocation(irp);
    code = stack->Parameters.DeviceIoControl.IoControlCode;
    switch (code) {
    case IOCTL_INTERNAL_USB_SUBMIT_URB:
        return hcdSubmitUrb(pdo, irp, (PURB)stack->Parameters.Others.Argument1);

    case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
        return hcdPortStatus(pdo, irp,
                             (PULONG)stack->Parameters.Others.Argument1);

    default:
        hc = pdo->Controller;
        if (hc != NULL) {
            hc->IoctlUnknown++;
        }
        XHCI_DBG_VALUE("hcd: internal IOCTL not served", code);
        return HcdCompleteIrp(irp, STATUS_NOT_SUPPORTED, 0);
    }
}
