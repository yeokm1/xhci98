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
#include "xhci_pipe.h"

/* Absent from the Windows 2000 DDK's usbdi.h; values as WDK 7.1's
 * inc\api\usb.h defines them (lines 459 and 439). */
#ifndef USBD_STATUS_DEVICE_GONE
#define USBD_STATUS_DEVICE_GONE    ((USBD_STATUS)0xC0007000L)
#endif

/* The default-pipe transfer flag, absent from the Windows 2000 DDK; WDK 7.1
 * inc\api\usb.h:273. */
#ifndef USBD_DEFAULT_PIPE_TRANSFER
#define USBD_DEFAULT_PIPE_TRANSFER 0x00000008
#endif

/* The first time each URB function reaches any PDO of this image, traced
 * once: a per-function count whose first increment is the trace, so two
 * processors cannot both see "first" (Codex review of batch (c), round 1,
 * finding 5). The per-controller counts are interlocked for the same
 * reason. */
static LONG hcdUrbSeen[HCD_URB_FUNCTIONS];

static VOID hcdCount(PULONG counter)
{
    (VOID)InterlockedIncrement((PLONG)counter);
}

static NTSTATUS hcdUrbComplete(PIRP irp, PURB urb, USBD_STATUS usbd,
                               NTSTATUS status)
{
    urb->UrbHeader.Status = usbd;
    XHCI_DBG_VALUE("hcd: URB refused at dispatch, function/status",
                   ((ULONG)urb->UrbHeader.Function << 24) |
                       ((ULONG)usbd & 0x00FFFFFFUL));
    return HcdCompleteIrp(irp, status, 0);
}

/* A device gone: refused, but completed at the next tick (HcdIoRefuseLater,
 * hcd_io.c) - never inside this dispatch, which a client's completion
 * routine can re-enter by resubmitting. The PDO's own timer, so an orphan
 * whose controller has gone defers too (Codex review of batch (c), round
 * 9, finding 2). */
static NTSTATUS hcdGoneLater(PHCD_DEVICE_PDO pdo, PIRP irp, PURB urb)
{
    return HcdIoRefuseLater(pdo, irp, urb, USBD_STATUS_DEVICE_GONE);
}

/*
 * The device record behind a listed PDO, referenced: taken under PdoListLock
 * while the PDO still names it, so the thread's free (which unlists first and
 * then waits the references out, hcd_io.c) cannot pass it. NULL when the
 * device has left.
 */
static PHCD_USB_DEVICE hcdDeviceRef(PHCD_CONTROLLER hc, PHCD_DEVICE_PDO pdo)
{
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;

    KeAcquireSpinLock(&hc->PdoListLock, &oldIrql);
    dev = pdo->Listed ? pdo->Device : NULL;
    if (dev != NULL) {
        (VOID)InterlockedIncrement(&dev->Refs);
    }
    KeReleaseSpinLock(&hc->PdoListLock, oldIrql);
    return dev;
}

static ULONG hcdIsFeature(ULONG function)
{
    return (function >= XHCI_PIPE_URB_SET_FEATURE_DEVICE &&
            function <= XHCI_PIPE_URB_CLEAR_FEATURE_ENDPOINT) ||
           function == XHCI_PIPE_URB_CLEAR_FEATURE_OTHER ||
           function == XHCI_PIPE_URB_SET_FEATURE_OTHER;
}

/* The URB functions that are one control transfer on the default pipe
 * (design record 13 section 6.3), xhci_pipe.h's list. */
static ULONG hcdIsControl(ULONG function)
{
    return function == XHCI_PIPE_URB_CONTROL_TRANSFER ||
           (function >= XHCI_PIPE_URB_GET_DESC_DEVICE &&
            function <= XHCI_PIPE_URB_GET_STATUS_ENDPOINT) ||
           (function >= XHCI_PIPE_URB_VENDOR_DEVICE &&
            function <= XHCI_PIPE_URB_CLASS_ENDPOINT) ||
           (function >= XHCI_PIPE_URB_CLASS_OTHER &&
            function <= XHCI_PIPE_URB_SET_DESC_INTERFACE);
}

/*
 * One control-transfer URB onto the device's default pipe: its SETUP bytes
 * from xhci_pipe.c (or, for URB_FUNCTION_CONTROL_TRANSFER, the client's own,
 * which may not be SET_ADDRESS, SET_CONFIGURATION or SET_INTERFACE - the bus
 * performs those itself), its buffer and its direction.
 */
static LONG hcdControlRequest(PURB urb, ULONG function, PHCD_IO_REQUEST req)
{
    struct _URB_CONTROL_TRANSFER *ct;
    XHCI_PIPE_CONTROL c;
    ULONG answer;
    ULONG trt;
    ULONG i;

    ct = &urb->UrbControlTransfer;
    for (i = 0; i < sizeof(c); i++) {
        ((PUCHAR)&c)[i] = 0;
    }
    c.Function = function;
    req->Control = 1;
    if (hcdIsFeature(function)) {
        c.FeatureSelector = urb->UrbControlFeatureRequest.FeatureSelector;
        c.Index = urb->UrbControlFeatureRequest.Index;
    } else {
        req->Length = ct->TransferBufferLength;
        req->Buffer = ct->TransferBuffer;
        req->Mdl = ct->TransferBufferMDL;
        req->LengthOut = &ct->TransferBufferLength;
        c.Length = req->Length;
        switch (function) {
        case XHCI_PIPE_URB_GET_DESC_DEVICE:
        case XHCI_PIPE_URB_SET_DESC_DEVICE:
        case XHCI_PIPE_URB_GET_DESC_ENDPOINT:
        case XHCI_PIPE_URB_SET_DESC_ENDPOINT:
        case XHCI_PIPE_URB_GET_DESC_INTERFACE:
        case XHCI_PIPE_URB_SET_DESC_INTERFACE:
            c.DescriptorIndex = urb->UrbControlDescriptorRequest.Index;
            c.DescriptorType = urb->UrbControlDescriptorRequest.DescriptorType;
            c.LanguageId = urb->UrbControlDescriptorRequest.LanguageId;
            break;
        case XHCI_PIPE_URB_GET_STATUS_DEVICE:
        case XHCI_PIPE_URB_GET_STATUS_INTERFACE:
        case XHCI_PIPE_URB_GET_STATUS_ENDPOINT:
        case XHCI_PIPE_URB_GET_STATUS_OTHER:
            c.Index = urb->UrbControlGetStatusRequest.Index;
            break;
        case XHCI_PIPE_URB_GET_INTERFACE:
            c.Index = urb->UrbControlGetInterfaceRequest.Interface;
            break;
        case XHCI_PIPE_URB_CONTROL_TRANSFER:
        case XHCI_PIPE_URB_GET_CONFIGURATION:
            break;
        default:
            /* Vendor and class: the client states the direction. */
            c.DirectionIn = (urb->UrbControlVendorClassRequest.TransferFlags &
                             USBD_TRANSFER_DIRECTION_IN) != 0;
            c.ReservedBits =
                urb->UrbControlVendorClassRequest.RequestTypeReservedBits;
            c.Request = urb->UrbControlVendorClassRequest.Request;
            c.Value = urb->UrbControlVendorClassRequest.Value;
            c.Index = urb->UrbControlVendorClassRequest.Index;
            break;
        }
    }

    if (function == XHCI_PIPE_URB_CONTROL_TRANSFER) {
        /* The default pipe is the only control pipe the bus opens: no
         * configuration this project has met carries another. */
        if (ct->PipeHandle != NULL &&
            (ct->TransferFlags & USBD_DEFAULT_PIPE_TRANSFER) == 0) {
            return USBD_STATUS_INVALID_PIPE_HANDLE;
        }
        for (i = 0; i < 8; i++) {
            req->Setup[i] = ct->SetupPacket[i];
        }
        answer = XhciPipeCheckRawSetup(req->Setup, &trt);
    } else {
        answer = XhciPipeBuildSetup(&c, req->Setup, &trt);
    }
    if (answer == XHCI_PIPE_UNSUPPORTED) {
        return USBD_STATUS_INVALID_URB_FUNCTION;
    }
    if (answer != XHCI_PIPE_OK) {
        return USBD_STATUS_INVALID_PARAMETER;
    }
    if (trt == XHCI_PIPE_TRT_NO_DATA) {
        req->Length = 0;
    }
    if (trt == XHCI_PIPE_TRT_IN_DATA) {
        req->Flags = HCD_IO_IN;
    }
    return USBD_STATUS_SUCCESS;
}

XHCI_C_ASSERT(iso_packet_mirror,
              sizeof(USBD_ISO_PACKET_DESCRIPTOR) ==
                  sizeof(XHCI_PIPE_ISO_PACKET));

/*
 * An isochronous URB: its packet table checked against its buffer, and the
 * transfer it asks for. The checks that need the pipe - its type, its Max
 * ESIT Payload, the StartFrame against the frame axis - are hcd_io.c's
 * (hcdIsoAdmit). The direction is the pipe's too, so none is taken here.
 */
static LONG hcdIsochRequest(PURB urb, PHCD_IO_REQUEST req)
{
    struct _URB_ISOCH_TRANSFER *it;
    ULONG fixed;

    it = &urb->UrbIsochronousTransfer;
    fixed = FIELD_OFFSET(struct _URB_ISOCH_TRANSFER, IsoPacket);
    /* UrbHeader.Length is a USHORT and the count is bounded first, so the
     * product cannot wrap. */
    if (urb->UrbHeader.Length < fixed || it->NumberOfPackets == 0 ||
        it->NumberOfPackets > XHCI_XFER_MAX_ISO_PACKETS ||
        urb->UrbHeader.Length <
            fixed + it->NumberOfPackets * sizeof(USBD_ISO_PACKET_DESCRIPTOR)) {
        return USBD_STATUS_INVALID_PARAMETER;
    }
    if (XhciPipeIsoCheck((const XHCI_PIPE_ISO_PACKET *)it->IsoPacket,
                         it->NumberOfPackets, it->TransferBufferLength,
                         XHCI_XFER_MAX_ISO_PACKETS, 0) != XHCI_PIPE_OK) {
        return USBD_STATUS_INVALID_PARAMETER;
    }
    req->Handle = it->PipeHandle;
    req->Flags = HCD_IO_ISOCH;
    if (it->TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
        req->Flags |= HCD_IO_ASAP;
    }
    req->Buffer = it->TransferBuffer;
    req->Mdl = it->TransferBufferMDL;
    req->Length = it->TransferBufferLength;
    req->LengthOut = &it->TransferBufferLength;
    return USBD_STATUS_SUCCESS;
}

/*
 * The transfer a URB asks for - a control transfer on the default pipe, or
 * a bulk, interrupt or isochronous transfer on the pipe its handle names -
 * or the USBD status that refuses it. Read at dispatch, and read again from
 * the same URB, which its client may not touch while the IRP is pending,
 * when an IRP that waited for one of its pipe's records is given one
 * (hcd_io.c). A bulk or interrupt URB's direction flag counts only on the
 * default pipe: on any other the pipe's endpoint decides, as usbport has it
 * (hcd_io.c, hcdFill).
 */
LONG HcdUrbIoRequest(PVOID urbv, PHCD_IO_REQUEST req)
{
    struct _URB_BULK_OR_INTERRUPT_TRANSFER *bi;
    PURB urb;
    ULONG function;
    ULONG i;

    urb = (PURB)urbv;
    for (i = 0; i < sizeof(*req); i++) {
        ((PUCHAR)req)[i] = 0;
    }
    function = urb->UrbHeader.Function;
    if (hcdIsControl(function)) {
        return hcdControlRequest(urb, function, req);
    }
    if (function == URB_FUNCTION_ISOCH_TRANSFER) {
        return hcdIsochRequest(urb, req);
    }
    if (function != URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER) {
        return USBD_STATUS_INVALID_URB_FUNCTION;
    }
    bi = &urb->UrbBulkOrInterruptTransfer;
    req->Handle = bi->PipeHandle;
    if (bi->TransferFlags & USBD_TRANSFER_DIRECTION_IN) {
        req->Flags |= HCD_IO_IN;
    }
    if (bi->TransferFlags & USBD_SHORT_TRANSFER_OK) {
        req->Flags |= HCD_IO_SHORT_OK;
    }
    req->Buffer = bi->TransferBuffer;
    req->Mdl = bi->TransferBufferMDL;
    req->Length = bi->TransferBufferLength;
    req->LengthOut = &bi->TransferBufferLength;
    return USBD_STATUS_SUCCESS;
}

/* A transfer URB: parsed, then submitted with a device reference that
 * passes to the IRP (HcdIoSubmit). A URB that cannot be parsed is refused
 * inline, as every deterministic dispatch error is. */
static NTSTATUS hcdTransferUrb(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                               PIRP irp, PURB urb)
{
    HCD_IO_REQUEST req;
    PHCD_USB_DEVICE dev;
    LONG usbd;

    usbd = HcdUrbIoRequest(urb, &req);
    if (usbd != USBD_STATUS_SUCCESS) {
        return hcdUrbComplete(irp, urb, usbd, STATUS_INVALID_PARAMETER);
    }
    dev = hcdDeviceRef(hc, pdo);
    if (dev == NULL) {
        return hcdGoneLater(pdo, irp, urb);
    }
    return HcdIoSubmit(hc, dev, pdo, irp, urb, &req);
}

static NTSTATUS hcdSubmitUrb(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                             PIRP irp, PURB urb)
{
    ULONG function;
    PHCD_USB_DEVICE dev;

    if (urb == NULL) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    function = urb->UrbHeader.Function;
    if (function < HCD_URB_FUNCTIONS) {
        if (InterlockedIncrement(&hcdUrbSeen[function]) == 1) {
            XHCI_DBG_VALUE("hcd: first URB of function", function);
        }
        if (hc != NULL) {
            hcdCount(&hc->UrbCount[function]);
        }
    } else if (hc != NULL) {
        hcdCount(&hc->UrbUnknown);
    }

    if (hc == NULL || !pdo->Listed) {
        /* Orphaned, or its device has left: nothing on the bus answers. */
        return hcdGoneLater(pdo, irp, urb);
    }
    if (hcdIsControl(function) ||
        function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER ||
        function == URB_FUNCTION_ISOCH_TRANSFER) {
        return hcdTransferUrb(pdo, hc, irp, urb);
    }
    switch (function) {
    case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
        /* The 32-bit frame number the miniport kept (xhci_init.c,
         * XhciFrameNumber): MFINDEX deltas, sampled by the thread's
         * health poll as well, so no 2,048-frame lap is lost. */
        if (urb->UrbHeader.Length <
            sizeof(struct _URB_GET_CURRENT_FRAME_NUMBER)) {
            return hcdUrbComplete(irp, urb, USBD_STATUS_INVALID_PARAMETER,
                                  STATUS_INVALID_PARAMETER);
        }
        urb->UrbGetCurrentFrameNumber.FrameNumber = XhciFrameNumber(&hc->Hc);
        urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);

    case URB_FUNCTION_SELECT_CONFIGURATION:
    case URB_FUNCTION_SELECT_INTERFACE:
    case URB_FUNCTION_ABORT_PIPE:
    case URB_FUNCTION_RESET_PIPE:
        /* Commands: pended for the controller thread (hcd_cfg.c). */
        dev = hcdDeviceRef(hc, pdo);
        if (dev == NULL) {
            return hcdGoneLater(pdo, irp, urb);
        }
        return HcdCfgQueue(hc, dev, pdo, irp);

    default:
        break;
    }
    return hcdUrbComplete(irp, urb, USBD_STATUS_INVALID_URB_FUNCTION,
                          STATUS_INVALID_PARAMETER);
}

/*
 * IOCTL_INTERNAL_USB_GET_PORT_STATUS: connected and enabled while the PDO is
 * listed - its device enumerated on the port and has not left - and 0
 * otherwise. Read from the PDO's own state, not PORTSC: a register read here
 * could race a power-down or the stop's unmapping, which this dispatch,
 * possibly at DISPATCH_LEVEL, cannot exclude (Codex review of batch (c),
 * round 1, finding 2).
 */
static NTSTATUS hcdPortStatus(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                              PIRP irp, PULONG out)
{
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    *out = (hc != NULL && pdo->Listed)
               ? (USBD_PORT_CONNECTED | USBD_PORT_ENABLED)
               : 0;
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/*
 * The entry. The PDO's Busy count is raised before its Controller is read,
 * and the parent's release (HcdDevicePdoReleaseAll) clears Controller and
 * then waits Busy out, so a controller read here is not freed under this
 * dispatch (Codex review of batch (c), round 1, finding 1).
 */
NTSTATUS HcdDevicePdoInternalIoctl(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PHCD_CONTROLLER hc;
    NTSTATUS status;
    ULONG code;

    (VOID)InterlockedIncrement(&pdo->Busy);
    if (pdo->Closing) {
        /* Stopping or removed (hcd_pdo.c): nothing more is pended. The
         * refusal completes inside the Busy count, as every other path
         * here does, so the quiesce that closed admission cannot return
         * before it (Codex review of batch (c), round 4, finding 1). */
        stack = IoGetCurrentIrpStackLocation(irp);
        if (stack->Parameters.DeviceIoControl.IoControlCode ==
                IOCTL_INTERNAL_USB_SUBMIT_URB &&
            stack->Parameters.Others.Argument1 != NULL) {
            /* Completed at the next tick, not here (HcdIoRefuseLater) -
             * an orphaned PDO's too (round 9, finding 2). */
            status = HcdIoRefuseLater(
                pdo, irp, stack->Parameters.Others.Argument1,
                USBD_STATUS_DEVICE_GONE);
        } else {
            status = HcdCompleteIrp(irp, STATUS_DELETE_PENDING, 0);
        }
        (VOID)InterlockedDecrement(&pdo->Busy);
        return status;
    }
    hc = pdo->Controller;
    stack = IoGetCurrentIrpStackLocation(irp);
    code = stack->Parameters.DeviceIoControl.IoControlCode;
    switch (code) {
    case IOCTL_INTERNAL_USB_SUBMIT_URB:
        status = hcdSubmitUrb(pdo, hc, irp,
                              (PURB)stack->Parameters.Others.Argument1);
        break;

    case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
        status = hcdPortStatus(pdo, hc, irp,
                               (PULONG)stack->Parameters.Others.Argument1);
        break;

    default:
        if (hc != NULL) {
            hcdCount(&hc->IoctlUnknown);
        }
        XHCI_DBG_VALUE("hcd: internal IOCTL not served", code);
        status = HcdCompleteIrp(irp, STATUS_NOT_SUPPORTED, 0);
        break;
    }
    (VOID)InterlockedDecrement(&pdo->Busy);
    return status;
}
