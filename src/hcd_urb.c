/*
 * hcd_urb.c - the device PDOs' function-driver contract: the internal device
 * controls a USB function driver sends its PDO (roadmap-hcd.md tasks 26-A.5
 * and 26-A.6; design record 13 section 6).
 *
 * Every IRP_MJ_INTERNAL_DEVICE_CONTROL reaching a device PDO arrives here.
 * For IOCTL_INTERNAL_USB_SUBMIT_URB each URB function is counted and its
 * first occurrence traced; transfers go to hcd_io.c and the URBs that need
 * commands to the thread (hcd_cfg.c). GET_PORT_STATUS, GET_BUS_INFO,
 * GET_HUB_COUNT and CYCLE_PORT are answered here; RESET_PORT is pended for
 * the thread. Any other control code is refused as STATUS_NOT_SUPPORTED and
 * traced once (design record 13 section 6.3 lists what each target's class
 * drivers send).
 *
 * What Windows XP onward adds (task 28-A.1; design record 13 sections 6.4
 * and 6.5) is here too: USB_BUS_INTERFACE_USBDI V0 to V3, the IRP-less
 * interface XP and XP x64 usbstor and usbaudio and Vista and 7 usbstor and
 * usbaudio query; GET_DEVICE_HANDLE; SUBMIT_IDLE_NOTIFICATION, held;
 * GET_TOPOLOGY_ADDRESS (Windows 7 usbstor); RECORD_FAILURE (Windows 7
 * usbaudio); and the SYNC_RESET_PIPE / SYNC_CLEAR_STALL halves of
 * RESET_PIPE (hcd_cfg.c).
 *
 * IRQL: <= DISPATCH_LEVEL (a function driver may send these at DISPATCH),
 * except HcdUrbQueryInterface (PASSIVE_LEVEL, a PnP IRP) and the bus
 * interface's functions (any IRQL, as usbbusif.h requires).
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

/* The amd64 build's WDK carries usbbusif.h, and the structures below are
 * checked against it there; the Windows 2000 DDK has none. */
#ifdef _WIN64
#include <usbbusif.h>
#endif

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

/* The same for each internal IOCTL refused as unserved (hcdIoctlSlot). */
#define HCD_IOCTL_SLOTS 0x40UL
static LONG hcdIoctlSeen[HCD_IOCTL_SLOTS];

/* The bus totals ReactOS's miniports register (usbmport.h:541-542,
 * TOTAL_USB11_BUS_BANDWIDTH and TOTAL_USB20_BUS_BANDWIDTH); the Windows 2000
 * usbport's own value was not read. */
#define HCD_BUS_BANDWIDTH_FULL   12000UL
#define HCD_BUS_BANDWIDTH_HIGH   400000UL

/*
 * The internal IOCTLs Windows XP onward adds, absent from the Windows 2000
 * DDK's usbioctl.h (and the NT 6.x ones from WDK 7.1's WNET headers): WDK
 * 7.1 inc\api\usbiodef.h lines 56-66 for the function indexes, usbioctl.h
 * for METHOD_NEITHER, design record 13 section 6.1 for the values, which the
 * asserts below restate.
 */
#ifndef IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION
#define IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION                           \
    CTL_CODE(FILE_DEVICE_USB, 9, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_INTERNAL_USB_RECORD_FAILURE
#define IOCTL_INTERNAL_USB_RECORD_FAILURE                                     \
    CTL_CODE(FILE_DEVICE_USB, 10, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE
#define IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE                                  \
    CTL_CODE(FILE_DEVICE_USB, 268, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS
#define IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS                               \
    CTL_CODE(FILE_DEVICE_USB, 271, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif

XHCI_C_ASSERT(ioctl_idle_value,
              IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION == 0x220027UL);
XHCI_C_ASSERT(ioctl_failure_value,
              IOCTL_INTERNAL_USB_RECORD_FAILURE == 0x22002BUL);
XHCI_C_ASSERT(ioctl_handle_value,
              IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE == 0x220433UL);
XHCI_C_ASSERT(ioctl_topology_value,
              IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS == 0x22043FUL);

/* The URB functions XP onward adds that are refused here, by name in the
 * trace (WDK 7.1 inc\api\usb.h). */
#define HCD_URB_GET_MS_FEATURE_DESCRIPTOR   0x002AUL

#ifndef STATUS_NO_CALLBACK_ACTIVE
#define STATUS_NO_CALLBACK_ACTIVE ((NTSTATUS)0xC0000258L)
#endif

/* USB_IDLE_CALLBACK_INFO (WDK 7.1 usbioctl.h): the callback and its
 * context. Only read for NULL here: the bus never calls the callback. */
typedef struct _HCD_IDLE_CALLBACK_INFO {
    PVOID IdleCallback;
    PVOID IdleContext;
} HCD_IDLE_CALLBACK_INFO, *PHCD_IDLE_CALLBACK_INFO;

/* USB_START_FAILDATA (WDK 7.1 usbioctl.h), read for the trace only. */
typedef struct _HCD_START_FAILDATA {
    ULONG LengthInBytes;
    NTSTATUS NtStatus;
    LONG UsbdStatus;
    ULONG ConnectStatus;
    UCHAR DriverData[4];
} HCD_START_FAILDATA, *PHCD_START_FAILDATA;

/*
 * USB_BUS_INTERFACE_USBDI_V3 (WDK 7.1 inc\ddk\usbbusif.h lines 252-414),
 * the union of V0 to V2 as each is a prefix of the next. Declared here
 * because the Windows 2000 DDK has no usbbusif.h; every function pointer is
 * USB_BUSIFFN (__stdcall), and InterfaceReference / InterfaceDereference
 * are the DDK's PINTERFACE_REFERENCE, whose convention the build's default
 * gives. The offsets are asserted against design record 13 section 6.1's
 * sizes, which were read from the class drivers' own stores, and on the
 * amd64 build against the WDK's own structures too.
 */
#define HCD_BUSIFFN __stdcall

typedef VOID (HCD_BUSIFFN *HCD_BUSIF_GETUSBDI_VERSION)(
    PVOID, PUSBD_VERSION_INFORMATION, PULONG);
typedef NTSTATUS (HCD_BUSIFFN *HCD_BUSIF_QUERY_BUS_TIME)(PVOID, PULONG);
typedef NTSTATUS (HCD_BUSIFFN *HCD_BUSIF_SUBMIT_ISO_OUT_URB)(PVOID, PURB);
typedef NTSTATUS (HCD_BUSIFFN *HCD_BUSIF_QUERY_BUS_INFORMATION)(
    PVOID, ULONG, PVOID, PULONG, PULONG);
typedef BOOLEAN (HCD_BUSIFFN *HCD_BUSIF_IS_DEVICE_HIGH_SPEED)(PVOID);
typedef NTSTATUS (HCD_BUSIFFN *HCD_BUSIF_ENUM_LOG_ENTRY)(
    PVOID, ULONG, ULONG, ULONG, ULONG);
typedef NTSTATUS (HCD_BUSIFFN *HCD_BUSIF_QUERY_BUS_TIME_EX)(PVOID, PULONG);
typedef NTSTATUS (HCD_BUSIFFN *HCD_BUSIF_QUERY_CONTROLLER_TYPE)(
    PVOID, PULONG, PUSHORT, PUSHORT, PUCHAR, PUCHAR, PUCHAR, PUCHAR);

typedef struct _HCD_USBDI_INTERFACE {
    USHORT Size;
    USHORT Version;
    PVOID BusContext;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    HCD_BUSIF_GETUSBDI_VERSION GetUSBDIVersion;
    HCD_BUSIF_QUERY_BUS_TIME QueryBusTime;
    HCD_BUSIF_SUBMIT_ISO_OUT_URB SubmitIsoOutUrb;
    HCD_BUSIF_QUERY_BUS_INFORMATION QueryBusInformation;
    HCD_BUSIF_IS_DEVICE_HIGH_SPEED IsDeviceHighSpeed;         /* V1 */
    HCD_BUSIF_ENUM_LOG_ENTRY EnumLogEntry;                    /* V2 */
    HCD_BUSIF_QUERY_BUS_TIME_EX QueryBusTimeEx;               /* V3 */
    HCD_BUSIF_QUERY_CONTROLLER_TYPE QueryControllerType;      /* V3 */
} HCD_USBDI_INTERFACE, *PHCD_USBDI_INTERFACE;

#ifdef _WIN64
#define HCD_USBDI_V0_BYTES 0x40UL
#define HCD_USBDI_V1_BYTES 0x48UL
#define HCD_USBDI_V2_BYTES 0x50UL
#define HCD_USBDI_V3_BYTES 0x60UL
#else
#define HCD_USBDI_V0_BYTES 0x20UL
#define HCD_USBDI_V1_BYTES 0x24UL
#define HCD_USBDI_V2_BYTES 0x28UL
#define HCD_USBDI_V3_BYTES 0x30UL
#endif

XHCI_C_ASSERT(usbdi_v0_size,
              FIELD_OFFSET(HCD_USBDI_INTERFACE, IsDeviceHighSpeed) ==
                  HCD_USBDI_V0_BYTES);
XHCI_C_ASSERT(usbdi_v1_size,
              FIELD_OFFSET(HCD_USBDI_INTERFACE, EnumLogEntry) ==
                  HCD_USBDI_V1_BYTES);
XHCI_C_ASSERT(usbdi_v2_size,
              FIELD_OFFSET(HCD_USBDI_INTERFACE, QueryBusTimeEx) ==
                  HCD_USBDI_V2_BYTES);
XHCI_C_ASSERT(usbdi_v3_size,
              sizeof(HCD_USBDI_INTERFACE) == HCD_USBDI_V3_BYTES);
XHCI_C_ASSERT(usbdi_header,
              FIELD_OFFSET(HCD_USBDI_INTERFACE, BusContext) ==
                  FIELD_OFFSET(INTERFACE, Context) &&
              FIELD_OFFSET(HCD_USBDI_INTERFACE, InterfaceDereference) ==
                  FIELD_OFFSET(INTERFACE, InterfaceDereference));
#ifdef _WIN64
XHCI_C_ASSERT(usbdi_v0_native,
              sizeof(USB_BUS_INTERFACE_USBDI_V0) == HCD_USBDI_V0_BYTES &&
              FIELD_OFFSET(USB_BUS_INTERFACE_USBDI_V0, QueryBusTime) ==
                  FIELD_OFFSET(HCD_USBDI_INTERFACE, QueryBusTime));
XHCI_C_ASSERT(usbdi_v1_native,
              sizeof(USB_BUS_INTERFACE_USBDI_V1) == HCD_USBDI_V1_BYTES &&
              FIELD_OFFSET(USB_BUS_INTERFACE_USBDI_V1, IsDeviceHighSpeed) ==
                  FIELD_OFFSET(HCD_USBDI_INTERFACE, IsDeviceHighSpeed));
XHCI_C_ASSERT(usbdi_v2_native,
              sizeof(USB_BUS_INTERFACE_USBDI_V2) == HCD_USBDI_V2_BYTES &&
              FIELD_OFFSET(USB_BUS_INTERFACE_USBDI_V2, EnumLogEntry) ==
                  FIELD_OFFSET(HCD_USBDI_INTERFACE, EnumLogEntry));
#endif

/* {B1A96A13-3DE0-4574-9B01-C08FEAB318D6}, WDK 7.1 usbbusif.h line 258. */
static const GUID hcdUsbdiGuid = {
    0xb1a96a13, 0x3de0, 0x4574,
    { 0x9b, 0x01, 0xc0, 0x8f, 0xea, 0xb3, 0x18, 0xd6 }
};

/* The USBDI version GetUSBDIVersion reports: usbport's own, 0x500 on NT 5.x
 * (usbbusif.h's list) and 0x600 on NT 6.x (WDK 7.1 inc\api\usb.h:48,
 * USBDI_VERSION). Set at the first QUERY_INTERFACE, at PASSIVE_LEVEL. */
static ULONG hcdUsbdiVersion = 0x500UL;

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
 * inline, as every deterministic dispatch error is - except an isochronous
 * one, which a streaming client resubmits from its completion routine and
 * which therefore completes at the next tick, its packets stamped (Codex
 * review of batch (c), round 16, finding 3). */
static NTSTATUS hcdTransferParsed(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                                  PIRP irp, PURB urb,
                                  const HCD_IO_REQUEST *req)
{
    PHCD_USB_DEVICE dev;

    dev = hcdDeviceRef(hc, pdo);
    if (dev == NULL) {
        return hcdGoneLater(pdo, irp, urb);
    }
    return HcdIoSubmit(hc, dev, pdo, irp, urb, req);
}

static NTSTATUS hcdTransferUrb(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                               PIRP irp, PURB urb)
{
    HCD_IO_REQUEST req;
    LONG usbd;

    usbd = HcdUrbIoRequest(urb, &req);
    if (usbd != USBD_STATUS_SUCCESS) {
        hcdCount(&hc->Counters.UrbsMalformed);
        if (urb->UrbHeader.Function == URB_FUNCTION_ISOCH_TRANSFER) {
            HcdIoIsoRefused(urb, usbd);
            return HcdIoRefuseLater(pdo, irp, urb, usbd);
        }
        return hcdUrbComplete(irp, urb, usbd, STATUS_INVALID_PARAMETER);
    }
    return hcdTransferParsed(pdo, hc, irp, urb, &req);
}

/*
 * GET_DESCRIPTOR(CONFIGURATION) at a function PDO: answered from the PDO's
 * filtered copy (xhci_func.c), never from the device, whose bNumInterfaces
 * counts its siblings' interfaces too - Windows 2000's usbaudio.sys walks
 * that count over a list holding only its own (usbaudio-crash.md). The
 * client's buffer is reached without a mapping call: TransferBuffer, or an
 * MDL's MappedSystemVa when the MDL says it is mapped - the first arm of
 * the DDK's MmGetSystemAddressForMdl, whose second arm (MmMapLockedPages)
 * this driver does not import. Answered inline, as GET_CURRENT_FRAME_NUMBER
 * is: no client resubmits a configuration read from its completion routine.
 * A refusal still waits for the next tick (HcdIoRefuseLater), as every
 * refusal a client can retry from its completion routine does (Codex review
 * of batch (c), round 19, finding 7).
 */
static NTSTATUS hcdFunctionConfig(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                                  PIRP irp, PURB urb,
                                  const HCD_IO_REQUEST *req)
{
    PUCHAR to;
    ULONG wLength;
    ULONG n;
    ULONG i;

    if (req->Setup[2] != 0) {
        /* A split device has one configuration (design record 13 section
         * 10.9). */
        return HcdIoRefuseLater(pdo, irp, urb, USBD_STATUS_INVALID_PARAMETER);
    }
    to = NULL;
    if (req->Mdl != NULL) {
        /* hcdFill's check (hcd_io.c): an MDL shorter than the length the
         * URB states would let the copy run past the client's pages. */
        if (MmGetMdlByteCount(req->Mdl) < req->Length) {
            XHCI_DBG_TEXT("hcd: function configuration read, MDL short");
            hcdCount(&hc->Counters.UrbsMalformed);
            return HcdIoRefuseLater(pdo, irp, urb,
                                    USBD_STATUS_INVALID_PARAMETER);
        }
        if ((req->Mdl->MdlFlags &
             (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL)) != 0) {
            to = (PUCHAR)req->Mdl->MappedSystemVa;
        }
    } else {
        to = (PUCHAR)req->Buffer;
    }
    wLength = (ULONG)req->Setup[6] | ((ULONG)req->Setup[7] << 8);
    n = req->Length;
    if (n > wLength) {
        n = wLength;
    }
    if (n > pdo->ConfigLength) {
        n = pdo->ConfigLength;
    }
    if (to == NULL && n != 0) {
        XHCI_DBG_TEXT("hcd: function configuration read, buffer unmapped");
        hcdCount(&hc->Counters.UrbsMalformed);
        return HcdIoRefuseLater(pdo, irp, urb, USBD_STATUS_INVALID_PARAMETER);
    }
    for (i = 0; i < n; i++) {
        to[i] = pdo->Config[i];
    }
    if (req->LengthOut != NULL) {
        *req->LengthOut = n;
    }
    urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/* A control URB at a function PDO (design record 13 section 10.9): the
 * configuration descriptor is the bus's to give, and a standard or class
 * request to an interface must name one of the function's. */
static NTSTATUS hcdFunctionControl(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                                   PIRP irp, PURB urb)
{
    HCD_IO_REQUEST req;
    LONG usbd;

    usbd = HcdUrbIoRequest(urb, &req);
    if (usbd != USBD_STATUS_SUCCESS) {
        hcdCount(&hc->Counters.UrbsMalformed);
        return HcdIoRefuseLater(pdo, irp, urb, usbd);
    }
    if (req.Setup[0] == 0x80 && req.Setup[1] == 6 && req.Setup[3] == 2) {
        return hcdFunctionConfig(pdo, hc, irp, urb, &req);
    }
    if (!XhciFuncSetupAllowed(req.Setup, pdo->InterfaceMask,
                              pdo->Func.DeviceMask)) {
        XHCI_DBG_VALUE("hcd: function control refused, bmRequestType/wIndex",
                       ((ULONG)req.Setup[0] << 16) |
                           ((ULONG)req.Setup[5] << 8) | req.Setup[4]);
        XHCI_DBG_VALUE("hcd: function control refused, bRequest/wValue",
                       ((ULONG)req.Setup[1] << 16) |
                           ((ULONG)req.Setup[3] << 8) | req.Setup[2]);
        return HcdIoRefuseLater(pdo, irp, urb, USBD_STATUS_INVALID_PARAMETER);
    }
    return hcdTransferParsed(pdo, hc, irp, urb, &req);
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
    if (pdo->Function && hcdIsControl(function)) {
        return hcdFunctionControl(pdo, hc, irp, urb);
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
            hcdCount(&hc->Counters.UrbsMalformed);
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
    case XHCI_PIPE_URB_SYNC_RESET_PIPE:
    case XHCI_PIPE_URB_SYNC_CLEAR_STALL:
        /* Commands: pended for the controller thread (hcd_cfg.c). The two
         * halves of RESET_PIPE are Windows XP's (task 28-A.1); no class
         * driver read sends them (design record 13 section 6.5), and
         * usbport serves both. */
        dev = hcdDeviceRef(hc, pdo);
        if (dev == NULL) {
            return hcdGoneLater(pdo, irp, urb);
        }
        return HcdCfgQueue(hc, dev, pdo, irp);

    case HCD_URB_GET_MS_FEATURE_DESCRIPTOR:
        /* XP onward's hidusb asks for a HID device's Genre descriptor
         * (design record 13 section 6.4). It needs the device's MS OS
         * vendor code from string descriptor 0xEE, which this bus does not
         * read at enumeration; ReactOS's usbport refuses it the same way
         * (urb.c, INVALID_URB_FUNCTION), and a device without MS OS
         * descriptors - every one this project tests - fails it under the
         * stock stack too. */
        XHCI_DBG_TEXT("hcd: GET_MS_FEATURE_DESCRIPTOR refused");
        break;

    default:
        /* Among them CONTROL_TRANSFER_EX (0x32, Vista) and Windows 8's
         * OPEN_STATIC_STREAMS / CLOSE_STATIC_STREAMS and
         * GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS: no class driver on any
         * target sends one (section 6.5), and the stock usbport of XP and
         * of 2000 refuses them all as an unknown function. */
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
 * IOCTL_INTERNAL_USB_GET_BUS_INFO: Windows 2000 usbaudio's bandwidth check
 * before it streams (design record 13 section 6.3). Argument1 is the
 * USB_BUS_NOTIFICATION itself - the sender builds the IRP with no buffers -
 * and only TotalBandwidth and ConsumedBandwidth are read. Nothing is counted
 * as consumed: the controller admits or refuses an endpoint at Configure
 * Endpoint (NO_BANDWIDTH, hcd_cfg.c), which is the check that counts.
 */
static NTSTATUS hcdBusInfo(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                           PIRP irp, PUSB_BUS_NOTIFICATION out)
{
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    if (hc == NULL || !pdo->Listed) {
        return HcdCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    out->NotificationType = AcquireBusInfo;
    out->TotalBandwidth = (pdo->SpeedClass == XHCI_SPEED_HIGH ||
                           pdo->SpeedClass == XHCI_SPEED_SUPER)
                              ? HCD_BUS_BANDWIDTH_HIGH
                              : HCD_BUS_BANDWIDTH_FULL;
    out->ConsumedBandwidth = 0;
    out->ControllerNameLength = 0;
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/* IOCTL_INTERNAL_USB_GET_HUB_COUNT: each hub between the caller and the
 * controller adds one, the root hub included (usbport's root-hub PDO
 * increments the count it is handed; ReactOS usbport ioctl.c:353-363). The
 * bus's hubs are its own (design record 13 section 10.3), so a device PDO's
 * chain is the root hub alone. */
static NTSTATUS hcdHubCount(PIRP irp, PULONG count)
{
    if (count == NULL) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    (*count)++;
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/*
 * IOCTL_INTERNAL_USB_CYCLE_PORT: the device leaves the bus and is enumerated
 * again (design record 13 section 10.9). Only asked for here; the thread
 * feeds the port a disconnect (HcdEnumCycle). Completed now, not when the
 * cycle is done: the cycle frees the device record, which waits for every
 * reference, and deletes this PDO, whose REMOVE waits for every IRP it
 * pended. From a function PDO it cycles the whole device: every sibling
 * leaves with it, named by the group.
 */
static NTSTATUS hcdCyclePort(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                             PIRP irp)
{
    if (hc == NULL || !pdo->Listed) {
        return HcdCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    XHCI_DBG_VALUE("hcd: cycle port asked, port", pdo->Port);
    HcdEnumCycle(hc, pdo->Port, pdo->Group);
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/* IOCTL_INTERNAL_USB_RESET_PORT: pended for the thread with a device
 * reference, as the URBs that need commands are (hcd_cfg.c,
 * hcdCfgResetPort). */
static NTSTATUS hcdResetPortIoctl(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                                  PIRP irp)
{
    PHCD_USB_DEVICE dev;

    dev = (hc != NULL) ? hcdDeviceRef(hc, pdo) : NULL;
    if (dev == NULL) {
        return HcdCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    return HcdCfgQueue(hc, dev, pdo, irp);
}

/* ----------------------------------------------------------------------- */
/* What Windows XP onward sends (task 28-A.1)                               */
/* ----------------------------------------------------------------------- */

/*
 * IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE: an opaque handle for the caller's
 * device, which usbhub gives as its usbport device handle (ReactOS usbport
 * ioctl.c answers its own PDO the same way). The bus has no usbport handle
 * and no interface that takes one back, so the handle is the PDO's
 * extension: stable for the PDO's life and never dereferenced by anyone.
 * No class driver read sends it (design record 13 section 6.5).
 */
static NTSTATUS hcdDeviceHandle(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                                PIRP irp, PVOID *out)
{
    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    if (hc == NULL || !pdo->Listed) {
        return HcdCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    *out = pdo;
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/* The held idle request cancelled by its client: taken off the PDO under
 * the cancel lock it is called with, and completed; IdlePending drops only
 * once the completion has returned, the PDO's last touch here, so a REMOVE
 * that waits for it cannot let the client unload under a completion routine
 * still to run (Codex review of 28-A.1, round 1, finding 2). */
static VOID NTAPI hcdIdleCancel(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PHCD_DEVICE_PDO pdo;

    pdo = (PHCD_DEVICE_PDO)DeviceObject->DeviceExtension;
    if (pdo->IdleIrp == Irp) {
        pdo->IdleIrp = NULL;
    }
    IoReleaseCancelSpinLock(Irp->CancelIrql);
    (VOID)HcdCompleteIrp(Irp, STATUS_CANCELLED, 0);
    (VOID)InterlockedDecrement(&pdo->IdlePending);
}

/*
 * IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION (XP onward's hidusb, design
 * record 13 section 6.5): the selective-suspend handshake. A client sends
 * it when its device has been idle; the parent hub calls the callback when
 * the device may be suspended, and the IRP stays pending until the client
 * cancels it on its way back to D0. The bus has no selective-suspend policy
 * (roadmap 28.3, the idle that never sleeps), so the minimal correct answer
 * is the one where the hub never finds the moment: hold the IRP, never call
 * the callback, complete it STATUS_CANCELLED when the client cancels it or
 * the PDO stops. The device stays in D0, which is what the client gets on a
 * hub that will not suspend. One per PDO: a second while one is held is
 * STATUS_DEVICE_BUSY, and one with no callback STATUS_NO_CALLBACK_ACTIVE,
 * as usbhub answers both (Microsoft's "USB Selective Suspend" pages).
 */
static NTSTATUS hcdIdleSubmit(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                              PIRP irp, PHCD_IDLE_CALLBACK_INFO info)
{
    KIRQL cancelIrql;

    if (hc == NULL || !pdo->Listed) {
        return HcdCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    if (info == NULL || info->IdleCallback == NULL) {
        return HcdCompleteIrp(irp, STATUS_NO_CALLBACK_ACTIVE, 0);
    }
    IoAcquireCancelSpinLock(&cancelIrql);
    if (pdo->IdleIrp != NULL) {
        IoReleaseCancelSpinLock(cancelIrql);
        return HcdCompleteIrp(irp, STATUS_DEVICE_BUSY, 0);
    }
    if (irp->Cancel) {
        IoReleaseCancelSpinLock(cancelIrql);
        return HcdCompleteIrp(irp, STATUS_CANCELLED, 0);
    }
    /* Armed and recorded in one hold of the cancel lock, so a cancel and a
     * flush always agree on who owns the IRP: whoever finds the routine
     * still set under the lock takes it. */
    (VOID)InterlockedIncrement(&pdo->IdlePending);
    IoMarkIrpPending(irp);
    (VOID)IoSetCancelRoutine(irp, hcdIdleCancel);
    pdo->IdleIrp = irp;
    IoReleaseCancelSpinLock(cancelIrql);
    XHCI_DBG_VALUE("hcd: idle notification held, port", pdo->Port);
    return STATUS_PENDING;
}

/*
 * The held idle request, completed STATUS_CANCELLED, and then every
 * completion of one waited out: at a stop, surprise removal or removal of
 * the PDO (hcd_pdo.c, after the dispatches inside it are waited out, so
 * none can hold another), and at its deletion. A cancel routine already
 * called has taken the IRP under the lock; its completion is counted in
 * IdlePending until it has returned, and this waits for that too (Codex
 * review of 28-A.1, round 1, finding 2). IRQL: PASSIVE_LEVEL.
 */
VOID HcdUrbIdleDrain(PHCD_DEVICE_PDO pdo)
{
    LARGE_INTEGER due;
    KIRQL cancelIrql;
    PIRP irp;

    IoAcquireCancelSpinLock(&cancelIrql);
    irp = pdo->IdleIrp;
    if (irp != NULL && IoSetCancelRoutine(irp, NULL) != NULL) {
        pdo->IdleIrp = NULL;
    } else {
        irp = NULL;
    }
    IoReleaseCancelSpinLock(cancelIrql);
    if (irp != NULL) {
        (VOID)HcdCompleteIrp(irp, STATUS_CANCELLED, 0);
        (VOID)InterlockedDecrement(&pdo->IdlePending);
    }
    while (pdo->IdlePending != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
}

/*
 * IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS (Windows 7 usbstor's
 * IsDeviceConnectedToRootHub, design record 13 section 6.4): the
 * controller's PCI location, the root port and the hub ports below it from
 * the device's Route String (all 0 on a root port).
 * The PCI location is read once, at PASSIVE_LEVEL, from the controller's
 * PDO (IoGetDeviceProperty, already imported); a request at DISPATCH_LEVEL
 * before that, or a property the PCI bus will not give, leaves it 0, which
 * usbstor does not read.
 */
static NTSTATUS hcdTopologyAddress(PHCD_DEVICE_PDO pdo, PHCD_CONTROLLER hc,
                                   PIRP irp, PUCHAR out)
{
    ULONG bus;
    ULONG address;
    ULONG bytes;

    if (out == NULL) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    if (hc == NULL || !pdo->Listed) {
        return HcdCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    if (!pdo->PciRead && hc->Pdo != NULL &&
        KeGetCurrentIrql() == PASSIVE_LEVEL) {
        bus = 0;
        address = 0;
        if (NT_SUCCESS(IoGetDeviceProperty(hc->Pdo, DevicePropertyBusNumber,
                                           sizeof(bus), &bus, &bytes)) &&
            NT_SUCCESS(IoGetDeviceProperty(hc->Pdo, DevicePropertyAddress,
                                           sizeof(address), &address,
                                           &bytes))) {
            pdo->PciBus = bus;
            pdo->PciAddress = address;
            pdo->PciRead = 1;
        }
    }
    if (XhciPipeTopologyAddress(pdo->PciBus, pdo->PciAddress, pdo->RootPort,
                                pdo->Route, out) != XHCI_PIPE_OK) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/* IOCTL_INTERNAL_USB_RECORD_FAILURE (Windows 7 usbaudio's start failure,
 * design record 13 section 6.4): usbhub records it for its own diagnostics
 * and completes it; the bus has nowhere to record it but the trace. */
static NTSTATUS hcdRecordFailure(PHCD_DEVICE_PDO pdo, PIRP irp,
                                 PHCD_START_FAILDATA data)
{
    if (data != NULL && data->LengthInBytes >= 12UL) {
        XHCI_DBG_VALUE("hcd: client start failure recorded, port", pdo->Port);
        XHCI_DBG_VALUE("hcd: client start failure, NTSTATUS",
                       (ULONG)data->NtStatus);
        XHCI_DBG_VALUE("hcd: client start failure, USBD status",
                       (ULONG)data->UsbdStatus);
    }
#ifndef XHCI_DBG_TRACE
    UNREFERENCED_PARAMETER(pdo);
#endif
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/* ----------------------------------------------------------------------- */
/* USB_BUS_INTERFACE_USBDI (task 28-A.1)                                    */
/* ----------------------------------------------------------------------- */

/*
 * THE CONTEXT (Codex review of 28-A.1, round 1, finding 3). A client keeps
 * BusContext, and may call through it or dereference it, for as long as it
 * likes - and Windows 7's usbaudio.sys never dereferences it at all: its
 * USBDeviceStart (6.1.7601.17514 x86, 0x1CA0B) puts the interface block in
 * its KS object bag and frees it with ExFreePool, and no image site calls
 * InterfaceDereference (static, cdb -z with the public PDB, 2026-10-04).
 * So the context cannot be the PDO extension, which IoDeleteDevice frees,
 * and an object reference would keep a device object, and with it the
 * driver image, for every audio device ever started. It is a slot of a
 * static table instead, which lives as long as the image:
 *
 *   Pdo        the PDO while it exists; claimed at its first query, under
 *              hcdBusifLock, and given back by its deletion
 *              (HcdUrbBusifRelease), which sets Gone (interlocked), waits
 *              Busy out and only then lets the slot go;
 *   Busy       calls inside the slot: raised before Gone is read, so a call
 *              that finds Gone clear reads a PDO whose deletion is waiting
 *              for it;
 *   Refs       references held: what the client's Reference/Dereference
 *              move, on the slot alone. A free slot with none is taken
 *              first; with every free slot still referenced - clients like
 *              Windows 7's usbaudio - the first free one is reused, and a
 *              stale caller then reads another live device's answers,
 *              never freed memory.
 *
 * Inside a call the PDO's own Busy is raised too before Controller is read,
 * as the IOCTL entry does, so the parent's release (HcdDevicePdoReleaseAll)
 * cannot free the controller under it. QUERY_REMOVE is not failed for an
 * outstanding reference, although Microsoft's query-remove page asks it:
 * the references here are the device's own function driver's, which takes
 * one at every start and, on Windows 7, never drops it, so a refusal would
 * leave every audio device impossible to disable or remove.
 */
#define HCD_BUSIF_SLOTS 64UL

typedef struct _HCD_BUSIF_SLOT {
    PHCD_DEVICE_PDO Pdo;            /* hcdBusifLock; NULL when free       */
    volatile LONG Gone;             /* its PDO is being deleted           */
    volatile LONG Busy;
    volatile LONG Refs;
} HCD_BUSIF_SLOT, *PHCD_BUSIF_SLOT;

static HCD_BUSIF_SLOT hcdBusifSlots[HCD_BUSIF_SLOTS];
static KSPIN_LOCK hcdBusifLock;

/* DriverEntry. IRQL: PASSIVE_LEVEL. */
VOID HcdUrbInit(VOID)
{
    KeInitializeSpinLock(&hcdBusifLock);
}

/* The PDO's slot, claimed at its first query. NULL when the table is full
 * of live PDOs. IRQL: <= DISPATCH_LEVEL. */
static PHCD_BUSIF_SLOT hcdBusifClaim(PHCD_DEVICE_PDO pdo)
{
    PHCD_BUSIF_SLOT slot;
    KIRQL oldIrql;
    ULONG i;

    KeAcquireSpinLock(&hcdBusifLock, &oldIrql);
    slot = (PHCD_BUSIF_SLOT)pdo->BusifSlot;
    if (slot == NULL) {
        for (i = 0; i < HCD_BUSIF_SLOTS && slot == NULL; i++) {
            if (hcdBusifSlots[i].Pdo == NULL && hcdBusifSlots[i].Refs == 0) {
                slot = &hcdBusifSlots[i];
            }
        }
        for (i = 0; i < HCD_BUSIF_SLOTS && slot == NULL; i++) {
            if (hcdBusifSlots[i].Pdo == NULL) {
                slot = &hcdBusifSlots[i];
            }
        }
        if (slot != NULL) {
            slot->Pdo = pdo;
            (VOID)InterlockedExchange(&slot->Gone, 0);
            pdo->BusifSlot = slot;
        }
    }
    KeReleaseSpinLock(&hcdBusifLock, oldIrql);
    return slot;
}

/* The PDO is about to be deleted: its slot forgets it once no call is
 * inside it. IRQL: PASSIVE_LEVEL. */
VOID HcdUrbBusifRelease(PHCD_DEVICE_PDO pdo)
{
    PHCD_BUSIF_SLOT slot;
    LARGE_INTEGER due;
    KIRQL oldIrql;

    slot = (PHCD_BUSIF_SLOT)pdo->BusifSlot;
    if (slot == NULL) {
        return;
    }
    (VOID)InterlockedExchange(&slot->Gone, 1);
    while (slot->Busy != 0) {
        HcdRelativeMs(&due, 1);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
    KeAcquireSpinLock(&hcdBusifLock, &oldIrql);
    slot->Pdo = NULL;
    pdo->BusifSlot = NULL;
    KeReleaseSpinLock(&hcdBusifLock, oldIrql);
}

/* Into a call: the slot's PDO, NULL once it is being deleted, with the
 * PDO's Busy raised as well; *hc its controller, or NULL. Every Enter that
 * returned a PDO is paired with hcdBusifLeave(slot, pdo); one that returned
 * NULL with hcdBusifLeave(slot, NULL). */
static PHCD_DEVICE_PDO hcdBusifEnter(PVOID context, PHCD_CONTROLLER *hc)
{
    PHCD_BUSIF_SLOT slot;
    PHCD_DEVICE_PDO pdo;

    slot = (PHCD_BUSIF_SLOT)context;
    *hc = NULL;
    (VOID)InterlockedIncrement(&slot->Busy);
    if (slot->Gone) {
        return NULL;
    }
    pdo = slot->Pdo;
    if (pdo == NULL) {
        return NULL;
    }
    (VOID)InterlockedIncrement(&pdo->Busy);
    *hc = pdo->Controller;
    return pdo;
}

static VOID hcdBusifLeave(PVOID context, PHCD_DEVICE_PDO pdo)
{
    if (pdo != NULL) {
        (VOID)InterlockedDecrement(&pdo->Busy);
    }
    (VOID)InterlockedDecrement(&((PHCD_BUSIF_SLOT)context)->Busy);
}

static VOID hcdBusifReference(PVOID context)
{
    (VOID)InterlockedIncrement(&((PHCD_BUSIF_SLOT)context)->Refs);
}

static VOID hcdBusifDereference(PVOID context)
{
    (VOID)InterlockedDecrement(&((PHCD_BUSIF_SLOT)context)->Refs);
}

/* What usbport's own answers: its USBDI version, USB 2.0, no real-time
 * thread capability. Answered for the client by usbd.sys's export too,
 * never through here, on every target read (design record 13 section 6.3). */
static VOID HCD_BUSIFFN hcdBusifVersion(PVOID context,
                                        PUSBD_VERSION_INFORMATION info,
                                        PULONG caps)
{
    UNREFERENCED_PARAMETER(context);
    if (info != NULL) {
        info->USBDI_Version = hcdUsbdiVersion;
        info->Supported_USB_Version = 0x200;
    }
    if (caps != NULL) {
        *caps = 0;
    }
}

/* The 32-bit frame number GET_CURRENT_FRAME_NUMBER gives (XhciFrameNumber):
 * XP onward's usbaudio asks it here instead (design record 13 section 6.5).
 * Above DISPATCH_LEVEL, where the controller lock cannot be taken, the last
 * number published is given unlocked - an aligned ULONG, at most one health
 * poll stale. */
static NTSTATUS HCD_BUSIFFN hcdBusifBusTime(PVOID context, PULONG frame)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_CONTROLLER hc;
    ULONG now;

    pdo = hcdBusifEnter(context, &hc);
    now = 0;
    if (hc != NULL) {
        if (KeGetCurrentIrql() <= DISPATCH_LEVEL) {
            now = XhciFrameNumber(&hc->Hc);
        } else {
            now = *(volatile ULONG *)&hc->Hc.FrameNumber;
        }
    }
    hcdBusifLeave(context, pdo);
    if (frame != NULL) {
        *frame = now;
    }
    return STATUS_SUCCESS;
}

/* usbbusif.h: "Returns STATUS_NOT_SUPPORTED", usbport's own answer. */
static NTSTATUS HCD_BUSIFFN hcdBusifIsoOut(PVOID context, PURB urb)
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(urb);
    return STATUS_NOT_SUPPORTED;
}

/* Levels 0 and 1 (xhci_pipe.c), with GET_BUS_INFO's totals; the bus exposes
 * no controller symbolic name, so level 1's is empty. */
static NTSTATUS HCD_BUSIFFN hcdBusifBusInformation(PVOID context, ULONG level,
                                                   PVOID buffer,
                                                   PULONG length,
                                                   PULONG actual)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_CONTROLLER hc;
    ULONG high;

    pdo = hcdBusifEnter(context, &hc);
    high = (pdo != NULL && pdo->SpeedClass == XHCI_SPEED_HIGH);
    hcdBusifLeave(context, pdo);
    if (pdo == NULL) {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    switch (XhciPipeBusInformation(level,
                                   high ? HCD_BUS_BANDWIDTH_HIGH
                                        : HCD_BUS_BANDWIDTH_FULL,
                                   0, (UCHAR *)buffer, length, actual)) {
    case XHCI_PIPE_OK:
        return STATUS_SUCCESS;
    case XHCI_PIPE_TOO_SMALL:
        return STATUS_BUFFER_TOO_SMALL;
    case XHCI_PIPE_UNSUPPORTED:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

/* The device's true speed: High-Speed only. usbport answers whether the
 * controller is USB 2.0 (ReactOS iface.c), which on an EHCI is the same
 * thing because a Full or Low Speed device sits on a companion; on xHCI
 * every speed shares the controller, and XP's usbstor sizes its transfers
 * from this answer (design record 13 section 6.3). */
static BOOLEAN HCD_BUSIFFN hcdBusifHighSpeed(PVOID context)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_CONTROLLER hc;
    BOOLEAN high;

    pdo = hcdBusifEnter(context, &hc);
    high = (BOOLEAN)(pdo != NULL && pdo->SpeedClass == XHCI_SPEED_HIGH);
    hcdBusifLeave(context, pdo);
    return high;
}

static NTSTATUS HCD_BUSIFFN hcdBusifLogEntry(PVOID context, ULONG driverTag,
                                             ULONG enumTag, ULONG p1,
                                             ULONG p2)
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(driverTag);
    UNREFERENCED_PARAMETER(enumTag);
    UNREFERENCED_PARAMETER(p1);
    UNREFERENCED_PARAMETER(p2);
    return STATUS_SUCCESS;
}

/* The High-Speed microframe counter: not supported. The bus keeps a
 * 32-bit frame count (XhciFrameNumber) but no microframe count read in the
 * same hold, and the frame times eight would claim a precision it does not
 * have (Codex review of 28-A.1, round 1, finding 4). No class driver read
 * asks for V3 (design record 13 section 6.5). */
static NTSTATUS HCD_BUSIFFN hcdBusifBusTimeEx(PVOID context, PULONG frame)
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(frame);
    return STATUS_NOT_SUPPORTED;
}

/* The controller's PCI identity: vendor and device from the configuration
 * space read at start, class 0C/03/30 (xHCI); the revision is not kept
 * and reads 0. */
static NTSTATUS HCD_BUSIFFN hcdBusifControllerType(
    PVOID context, PULONG flags, PUSHORT vendor, PUSHORT device,
    PUCHAR pciClass, PUCHAR subClass, PUCHAR revision, PUCHAR progIf)
{
    PHCD_DEVICE_PDO pdo;
    PHCD_CONTROLLER hc;
    ULONG id;

    pdo = hcdBusifEnter(context, &hc);
    id = (hc != NULL) ? hc->Hc.PciVendorDevice : 0;
    hcdBusifLeave(context, pdo);
    if (hc == NULL) {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    if (flags != NULL) {
        *flags = 0;
    }
    if (vendor != NULL) {
        *vendor = (USHORT)(id & 0xFFFFUL);
    }
    if (device != NULL) {
        *device = (USHORT)((id >> 16) & 0xFFFFUL);
    }
    if (pciClass != NULL) {
        *pciClass = 0x0C;
    }
    if (subClass != NULL) {
        *subClass = 0x03;
    }
    if (revision != NULL) {
        *revision = 0;
    }
    if (progIf != NULL) {
        *progIf = 0x30;
    }
    return STATUS_SUCCESS;
}

/* Whether a QUERY_INTERFACE names USB_BUS_INTERFACE_USBDI_GUID - compared
 * field by field, since the DDK's IsEqualGUID is a memcmp. */
ULONG HcdUrbIsUsbdiQuery(const GUID *guid)
{
    ULONG i;

    if (guid == NULL || guid->Data1 != hcdUsbdiGuid.Data1 ||
        guid->Data2 != hcdUsbdiGuid.Data2 ||
        guid->Data3 != hcdUsbdiGuid.Data3) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        if (guid->Data4[i] != hcdUsbdiGuid.Data4[i]) {
            return 0;
        }
    }
    return 1;
}

/*
 * IRP_MN_QUERY_INTERFACE for USB_BUS_INTERFACE_USBDI_GUID (the caller has
 * checked the GUID). Versions 0 to 3, each the one before plus its members;
 * the Size must hold the version asked for (xhci_pipe.c's sizes, design
 * record 13 section 6.1), and only that much of the caller's structure is
 * written. A version above 3 is left as the IRP came, as any interface a
 * bus does not export; the one reference the caller then owns is taken
 * before the IRP completes. IRQL: PASSIVE_LEVEL.
 */
NTSTATUS HcdUrbQueryInterface(PHCD_DEVICE_PDO pdo, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PHCD_USBDI_INTERFACE out;
    PHCD_BUSIF_SLOT slot;
    ULONG version;
    ULONG need;

    stack = IoGetCurrentIrpStackLocation(irp);
    version = stack->Parameters.QueryInterface.Version;
    out = (PHCD_USBDI_INTERFACE)stack->Parameters.QueryInterface.Interface;
    XHCI_DBG_VALUE("hcd: USBDI interface asked, version/size",
                   (version << 16) | stack->Parameters.QueryInterface.Size);
    need = XhciPipeUsbdiSize(version, (ULONG)sizeof(PVOID));
    if (need == 0) {
        return HcdCompleteIrp(irp, irp->IoStatus.Status,
                              irp->IoStatus.Information);
    }
    if (out == NULL || stack->Parameters.QueryInterface.Size < need) {
        return HcdCompleteIrp(irp, STATUS_INVALID_PARAMETER, 0);
    }
    if (pdo->Controller == NULL || !pdo->Listed) {
        return HcdCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
    }
    slot = hcdBusifClaim(pdo);
    if (slot == NULL) {
        XHCI_DBG_TEXT("hcd: USBDI interface refused, no context slot");
        return HcdCompleteIrp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    hcdUsbdiVersion = IoIsWdmVersionAvailable(6, 0) ? 0x600UL : 0x500UL;

    out->Size = (USHORT)need;
    out->Version = (USHORT)version;
    out->BusContext = slot;
    out->InterfaceReference = hcdBusifReference;
    out->InterfaceDereference = hcdBusifDereference;
    out->GetUSBDIVersion = hcdBusifVersion;
    out->QueryBusTime = hcdBusifBusTime;
    out->SubmitIsoOutUrb = hcdBusifIsoOut;
    out->QueryBusInformation = hcdBusifBusInformation;
    if (version >= 1) {
        out->IsDeviceHighSpeed = hcdBusifHighSpeed;
    }
    if (version >= 2) {
        out->EnumLogEntry = hcdBusifLogEntry;
    }
    if (version >= 3) {
        out->QueryBusTimeEx = hcdBusifBusTimeEx;
        out->QueryControllerType = hcdBusifControllerType;
    }
    hcdBusifReference(slot);
    return HcdCompleteIrp(irp, STATUS_SUCCESS, 0);
}

/* The slot an internal IOCTL is traced under once: its function number, the
 * internal range (USB_IOCTL_INTERNAL_INDEX, 0) at 0..0x1F and the
 * USB_IOCTL_INDEX range (0xFF) above it, everything else in the last. */
static ULONG hcdIoctlSlot(ULONG code)
{
    ULONG function;

    function = (code >> 2) & 0xFFFUL;
    if (function < 0x20UL) {
        return function;
    }
    if (function >= 0xFFUL && function < 0xFFUL + 0x1FUL) {
        return 0x20UL + (function - 0xFFUL);
    }
    return HCD_IOCTL_SLOTS - 1;
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

    /* Every IRP this dispatch pends reads STATUS_PENDING in its IoStatus
     * until it completes, as usbport has it (NUSB 3.3 USBPORT.SYS image VA
     * 0x15851, beside its IoMarkIrpPending; external/reactos/usbport/
     * queue.c, USBPORT_QueuePendingTransferIrp). ASIX's Windows 98
     * AX88772.SYS 3.0.3.12 waits for a URB by polling that field (image VA
     * 0x103B5 and 0x10321): it read the 0 IoAllocateIrp left there, freed
     * an IRP still in flight and selected a configuration from a descriptor
     * not yet read (26-V.1, 2026-10-04). Set on entry, before anything can
     * complete the IRP; every synchronous completion overwrites it. */
    irp->IoStatus.Status = STATUS_PENDING;
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

    case IOCTL_INTERNAL_USB_GET_BUS_INFO:
        status = hcdBusInfo(pdo, hc, irp,
                            (PUSB_BUS_NOTIFICATION)
                                stack->Parameters.Others.Argument1);
        break;

    case IOCTL_INTERNAL_USB_RESET_PORT:
        status = hcdResetPortIoctl(pdo, hc, irp);
        break;

    case IOCTL_INTERNAL_USB_CYCLE_PORT:
        status = hcdCyclePort(pdo, hc, irp);
        break;

    case IOCTL_INTERNAL_USB_GET_HUB_COUNT:
        status = hcdHubCount(irp, (PULONG)stack->Parameters.Others.Argument1);
        break;

    case IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE:
        status = hcdDeviceHandle(pdo, hc, irp,
                                 (PVOID *)stack->Parameters.Others.Argument1);
        break;

    case IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION:
        status = hcdIdleSubmit(pdo, hc, irp,
                               (PHCD_IDLE_CALLBACK_INFO)
                                   stack->Parameters.Others.Argument1);
        break;

    case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS:
        status = hcdTopologyAddress(pdo, hc, irp,
                                    (PUCHAR)stack->Parameters.Others.Argument1);
        break;

    case IOCTL_INTERNAL_USB_RECORD_FAILURE:
        status = hcdRecordFailure(pdo, irp,
                                  (PHCD_START_FAILDATA)
                                      stack->Parameters.Others.Argument1);
        break;

    default:
        /* Refused, as no class driver on any target sends one (design
         * record 13 section 6.5) and each needs an object this bus does not
         * give out: GET_ROOTHUB_PDO, ENABLE_PORT and GET_HUB_NAME (hub
         * driver requests), GET_CONTROLLER_NAME and GET_BUSGUID_INFO (no
         * controller symbolic name is exposed), GET_PARENT_HUB_INFO (the
         * parent is the root hub, which no driver above may talk to as a
         * hub), GET_DEVICE_HANDLE_EX and GET_TT_DEVICE_HANDLE (Vista's hub
         * handles), NOTIFY_IDLE_READY, REQ_GLOBAL_SUSPEND / _RESUME and
         * GET_DEVICE_CONFIG_INFO (usbhub's own). */
        if (hc != NULL) {
            hcdCount(&hc->IoctlUnknown);
        }
        if (InterlockedIncrement(&hcdIoctlSeen[hcdIoctlSlot(code)]) == 1) {
            XHCI_DBG_VALUE("hcd: internal IOCTL not served", code);
        }
        status = HcdCompleteIrp(irp, STATUS_NOT_SUPPORTED, 0);
        break;
    }
    (VOID)InterlockedDecrement(&pdo->Busy);
    return status;
}
