/*
 * hcd_cfg.c - the URBs that need commands, served by the controller thread
 * (roadmap-hcd.md task 26-A.5; design record 13 section 6): SELECT_
 * CONFIGURATION, ABORT_PIPE and RESET_PIPE.
 *
 * hcd_urb.c pends each of these on the controller's SlowIrps list with the
 * device record referenced in the IRP's DriverContext[0], and the thread
 * serves the list in its powered pass under the power gate (hcd_enum.c,
 * HcdEnumService), where it alone issues commands and owns the one shared
 * Input Context. The hardware steps are the miniport's (branch 1.2.0.0,
 * xhci_slot.c; .claude-held notes of 2026-10-03), without the usbport
 * plumbing:
 *
 *   select    deconfigure what is open (Configure Endpoint with DC = 1, then
 *             drain and free the pipes), open each endpoint the client's
 *             interfaces name on a pool ring, one Configure Endpoint with
 *             A0 and every new DCI, SET_CONFIGURATION, and the client's pipe
 *             information filled; codes 7, 8 and 35 are NO_BANDWIDTH;
 *   abort     Stop Endpoint (Reset Endpoint instead when it reads halted),
 *             drain the queue as CANCELED, Set TR Dequeue to the enqueue;
 *   reset     Reset Endpoint (TSP 0), Set TR Dequeue to the engine's
 *             position, CLEAR_FEATURE(ENDPOINT_HALT) to the device - the
 *             HCD now owns the request usbport answered (the endpoint's
 *             data toggle restarts at both ends).
 *
 * IRQL: PASSIVE_LEVEL (the controller thread), except HcdCfgQueue and
 * HcdCfgPipe (<= DISPATCH_LEVEL).
 */

#include <ntddk.h>
#include <usbdi.h>
#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_xfer.h"
#include "xhci_pipe.h"
#include "xhci_dbg.h"

/* URB functions, Windows 2000 DDK inc\usbdi.h. */
#define HCD_URB_SELECT_CONFIGURATION    0x0000
#define HCD_URB_SELECT_INTERFACE        0x0001
#define HCD_URB_ABORT_PIPE              0x0002
#define HCD_URB_RESET_PIPE              0x001E

/* The interface handle a client gets back: an opaque nonzero cookie the
 * select-interface path (26-A.5, later) decodes. */
#define HCD_IFACE_COOKIE(n) ((USBD_INTERFACE_HANDLE)(ULONG_PTR)(0x48430000UL | (n)))

static ULONG hcdCfgDeconfigure(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev);
static ULONG hcdCfgQuiesce(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           PHCD_PIPE pipe);
static VOID hcdCfgComplete(PHCD_USB_DEVICE dev, PIRP irp, PURB urb,
                           LONG usbd);

/* ----------------------------------------------------------------------- */
/* Queueing                                                                 */
/* ----------------------------------------------------------------------- */

/* Pend one URB IRP for the thread; the caller's device reference passes to
 * it. IRQL: <= DISPATCH_LEVEL. */
NTSTATUS HcdCfgQueue(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                     PHCD_DEVICE_PDO pdo, PIRP irp)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (dev->Gone) {
        /* The thread has begun freeing the device and has flushed its
         * queued URBs already: one queued now would hold a reference
         * nothing ever returns (round 3, finding 1). Refused at the next
         * tick, not inline (round 8, finding 4). */
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        (VOID)InterlockedDecrement(&dev->Refs);
        return HcdIoRefuseLater(pdo, irp,
                                IoGetCurrentIrpStackLocation(irp)
                                    ->Parameters.Others.Argument1,
                                HCD_USBD_DEVICE_GONE);
    }
    irp->Tail.Overlay.DriverContext[0] = dev;
    irp->Tail.Overlay.DriverContext[2] = pdo;
    (VOID)InterlockedIncrement(&pdo->UrbsPending);
    IoMarkIrpPending(irp);
    InsertTailList(&hc->SlowIrps, &irp->Tail.Overlay.ListEntry);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdThreadWake(hc);
    return STATUS_PENDING;
}

static VOID hcdCfgComplete(PHCD_USB_DEVICE dev, PIRP irp, PURB urb,
                           LONG usbd)
{
    PHCD_DEVICE_PDO pdo;

    pdo = (PHCD_DEVICE_PDO)irp->Tail.Overlay.DriverContext[2];
    urb->UrbHeader.Status = usbd;
    XHCI_DBG_VALUE("hcd: thread URB done, function/status",
                   ((ULONG)urb->UrbHeader.Function << 24) |
                       ((ULONG)usbd & 0x00FFFFFFUL));
    irp->IoStatus.Status = (NTSTATUS)XhciPipeNtStatus((ULONG)usbd);
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    (VOID)InterlockedDecrement(&pdo->UrbsPending);
    (VOID)InterlockedDecrement(&dev->Refs);
}

/* A device leaving (hcd_io.c, HcdIoDeviceGone): its queued slow URBs
 * complete as DEVICE_GONE here, since only this thread serves them and it
 * is the one waiting for their references. Thread only. */
VOID HcdCfgFlushDevice(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    LIST_ENTRY mine;
    PLIST_ENTRY entry;
    PLIST_ENTRY next;
    PIRP irp;
    KIRQL oldIrql;

    InitializeListHead(&mine);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (entry = hc->SlowIrps.Flink; entry != &hc->SlowIrps; entry = next) {
        next = entry->Flink;
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        if (irp->Tail.Overlay.DriverContext[0] == dev) {
            RemoveEntryList(entry);
            InsertTailList(&mine, entry);
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    while (!IsListEmpty(&mine)) {
        entry = RemoveHeadList(&mine);
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        hcdCfgComplete(dev, irp,
                       (PURB)IoGetCurrentIrpStackLocation(irp)
                           ->Parameters.Others.Argument1,
                       HCD_USBD_DEVICE_GONE);
    }
}

/* ----------------------------------------------------------------------- */
/* Input Context                                                            */
/* ----------------------------------------------------------------------- */

static VOID hcdCfgZero(PXHCI_EXTENSION ext, ULONG offset, ULONG bytes)
{
    volatile ULONG *w;
    ULONG i;

    w = XhciCommonAt(ext, offset);
    for (i = 0; i < bytes / 4UL; i++) {
        w[i] = 0;
    }
}

/*
 * The Input Context for one Configure Endpoint: the control flags, the Slot
 * Context rewritten whole (a root-port device: route 0, the port's speed,
 * Context Entries the highest DCI enabled after it), and an Endpoint Context
 * for each DCI being added, its dequeue pointer and cycle state read from
 * the ring now. Returns 0 when a builder refuses a value.
 */
static ULONG hcdCfgBuildInput(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              const XHCI_PIPE_PLAN *plan, PHCD_PIPE *add)
{
    PXHCI_EXTENSION ext;
    PXHCI_HC_LAYOUT layout;
    XHCI_SLOT_PARAMS sp;
    XHCI_EP_PARAMS ep;
    PHCD_PIPE pipe;
    ULONG icc;
    ULONG isc;
    ULONG iep;
    ULONG dci;
    ULONG i;

    ext = &hc->Hc;
    layout = &ext->Layout;
    if (XhciInputControlContextOffset(layout, &icc) != XHCI_LAYOUT_OK ||
        XhciInputSlotContextOffset(layout, &isc) != XHCI_LAYOUT_OK) {
        return 0;
    }
    hcdCfgZero(ext, layout->InputContextOffset, layout->InputContextBytes);
    if (XhciBuildInputControlContext(XhciCommonAt(ext, icc), plan->AddFlags,
                                     plan->DropFlags) != 0) {
        return 0;
    }
    for (i = 0; i < sizeof(sp); i++) {
        ((PUCHAR)&sp)[i] = 0;
    }
    sp.RouteString = 0;
    sp.Psiv = dev->Speed;
    sp.RootHubPort = dev->Port;
    sp.ContextEntries = plan->ContextEntries;
    if (XhciBuildSlotContext(XhciCommonAt(ext, isc), &sp) != 0) {
        return 0;
    }
    for (dci = 2; dci < 32; dci++) {
        pipe = add[dci];
        if (pipe == NULL) {
            continue;
        }
        if (XhciInputEndpointContextOffset(layout, dci, &iep) !=
            XHCI_LAYOUT_OK) {
            return 0;
        }
        for (i = 0; i < sizeof(ep); i++) {
            ((PUCHAR)&ep)[i] = 0;
        }
        ep.EpType = pipe->Ep.EpType;
        ep.MaxPacketSize = pipe->Ep.MaxPacketSize;
        ep.MaxBurstSize = pipe->Ep.MaxBurstSize;
        ep.Mult = pipe->Ep.Mult;
        ep.Interval = pipe->Ep.Interval;
        ep.ErrorCount = pipe->Ep.ErrorCount;
        ep.AverageTrbLength = pipe->Ep.AverageTrbLength;
        ep.MaxEsitPayload = pipe->Ep.MaxEsitPayload;
        ep.DequeuePA = XhciRingDequeuePA(pipe->Ring);
        ep.Dcs = XhciRingDequeueCycle(pipe->Ring);
        if (XhciBuildEndpointContext(XhciCommonAt(ext, iep), &ep) != 0) {
            return 0;
        }
    }
    return 1;
}

/* ----------------------------------------------------------------------- */
/* Pipes                                                                    */
/* ----------------------------------------------------------------------- */

/* How many other devices hold no pool ring - the pool's fairness rule
 * (design record 04 section 3.6, XhciPoolAcquire). Controller lock held. */
static ULONG hcdCfgRingless(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    ULONG slot;
    ULONG n;

    n = 0;
    for (slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        if (hc->SlotDevice[slot] != NULL && hc->SlotDevice[slot] != dev &&
            hc->SlotDevice[slot]->PoolRings == 0) {
            n++;
        }
    }
    return n;
}

/* A new pipe for one endpoint, on a pool ring of its own. NULL when the
 * pool or the pool allocator has nothing; *usbd says which. */
static PHCD_PIPE hcdCfgPipeNew(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                               const XHCI_PIPE_EP *ep, PLONG usbd)
{
    PXHCI_EXTENSION ext;
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    ULONG answer;
    ULONG index;
    ULONG offset;
    ULONG i;

    ext = &hc->Hc;
    pipe = (PHCD_PIPE)HcdPoolAlloc(sizeof(HCD_PIPE));
    if (pipe == NULL) {
        *usbd = HCD_USBD_NO_MEMORY;
        return NULL;
    }
    for (i = 0; i < sizeof(HCD_PIPE); i++) {
        ((PUCHAR)pipe)[i] = 0;
    }
    HcdIoPipeInit(pipe, dev);
    pipe->Ep = *ep;
    pipe->Dci = ep->Dci;
    pipe->EndpointAddress = ep->Address;
    pipe->TransferType = ep->TransferType;
    pipe->MaxPacketSize = ep->MaxPacketSize;
    pipe->Interval = ep->BInterval;
    pipe->Ring = &pipe->OwnRing;
    pipe->Queue = &pipe->OwnQueue;
    XhciXferQueueInit(&pipe->OwnQueue);

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    answer = XhciPoolAcquire(&ext->RingPool, dev->SlotId,
                             hcdCfgRingless(hc, dev), &index);
    if (answer == XHCI_POOL_OK) {
        dev->PoolRings++;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (answer != XHCI_POOL_OK ||
        XhciPoolRingOffset(&ext->Layout, index, &offset) != XHCI_LAYOUT_OK) {
        if (answer == XHCI_POOL_OK) {
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            (VOID)XhciPoolRelease(&ext->RingPool, dev->SlotId, index);
            dev->PoolRings--;
            XhciControllerLockRelease(&hc->Hc, oldIrql);
        }
        HcdPoolFree(pipe);
        *usbd = HCD_USBD_NO_MEMORY;
        return NULL;
    }
    pipe->PoolIndex = index;
    hcdCfgZero(ext, offset, ext->Layout.PoolRingTrbs * XHCI_TRB_BYTES);
    if (XhciRingInit(&pipe->OwnRing,
                     (volatile XHCI_TRB *)XhciCommonAt(ext, offset),
                     XhciCommonPA(ext, offset), ext->Layout.PoolRingTrbs,
                     ep->TransferType == XHCI_PIPE_XFER_ISOCH
                         ? XHCI_RING_KIND_ISOCH
                         : XHCI_RING_KIND_ENDPOINT) != XHCI_RING_OK) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        (VOID)XhciPoolRelease(&ext->RingPool, dev->SlotId, index);
        dev->PoolRings--;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        HcdPoolFree(pipe);
        *usbd = HCD_USBD_INTERNAL_HC_ERROR;
        return NULL;
    }
    return pipe;
}

/* Give a pipe's ring back and free it; every record is free and the
 * controller owns none of its TRBs. */
static VOID hcdCfgPipeFree(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           PHCD_PIPE pipe)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    (VOID)XhciPoolRelease(&hc->Hc.RingPool, dev->SlotId, pipe->PoolIndex);
    if (dev->PoolRings != 0) {
        dev->PoolRings--;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdPoolFree(pipe);
}

/* Close the open pipes of `mask` (DCI bits): unpublished under the lock,
 * drained with `usbd`, waited out, freed. Their endpoints are already
 * stopped or disabled, or the slot gone. */
static VOID hcdCfgCloseMask(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            ULONG mask, LONG usbd)
{
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    ULONG dci;

    for (dci = 2; dci < 32; dci++) {
        if ((mask & (1UL << dci)) == 0) {
            continue;
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        pipe = dev->Pipes[dci];
        if (pipe != NULL) {
            pipe->Closed = 1;
            dev->Pipes[dci] = NULL;
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (pipe == NULL) {
            continue;
        }
        HcdIoDrainPipe(hc, pipe, usbd);
        HcdIoWaitPipe(hc, pipe);
        hcdCfgPipeFree(hc, dev, pipe);
    }
}

/* Every open pipe. */
static VOID hcdCfgCloseAll(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, LONG usbd)
{
    hcdCfgCloseMask(hc, dev, 0xFFFFFFFCUL, usbd);
}

/* The device is being freed (hcd_enum.c, after HcdIoDeviceGone): its pipes
 * go without commands. */
VOID HcdCfgDeviceGone(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    hcdCfgCloseAll(hc, dev, HCD_USBD_DEVICE_GONE);
}

/* A handle a client holds, if the device still has it open. IRQL:
 * <= DISPATCH_LEVEL, controller lock held by the caller. */
PHCD_PIPE HcdCfgPipe(PHCD_USB_DEVICE dev, PVOID handle)
{
    ULONG dci;

    for (dci = 2; dci < 32; dci++) {
        if (dev->Pipes[dci] != NULL && (PVOID)dev->Pipes[dci] == handle) {
            return dev->Pipes[dci];
        }
    }
    return NULL;
}

/* ----------------------------------------------------------------------- */
/* SELECT_CONFIGURATION                                                     */
/* ----------------------------------------------------------------------- */

static LONG hcdCfgSelect(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, PURB urb)
{
    struct _URB_SELECT_CONFIGURATION *sc;
    PUSB_CONFIGURATION_DESCRIPTOR cd;
    PUSBD_INTERFACE_INFORMATION ii;
    PHCD_PIPE add[32];
    XHCI_PIPE_IFACE iface;
    XHCI_PIPE_EP ep;
    XHCI_PIPE_PLAN plan;
    PXHCI_EXTENSION ext;
    XHCI_TRB trb;
    KIRQL oldIrql;
    PUCHAR p;
    PUCHAR end;
    ULONG fixed;
    ULONG total;
    ULONG mask;
    ULONG control;
    ULONG code;
    ULONG bytes;
    ULONG dci;
    ULONG e;
    LONG usbd;

    ext = &hc->Hc;
    sc = &urb->UrbSelectConfiguration;
    cd = sc->ConfigurationDescriptor;
    XHCI_DBG_VALUE("hcd: select, URB length", urb->UrbHeader.Length);
    XHCI_DBG_VALUE("hcd: select, ConfigurationDescriptor",
                   (ULONG)(ULONG_PTR)cd);
    XHCI_DBG_VALUE("hcd: select, first interface length/number/alternate",
                   ((ULONG)sc->Interface.Length << 16) |
                       ((ULONG)sc->Interface.InterfaceNumber << 8) |
                       sc->Interface.AlternateSetting);
    if (!hcdCfgDeconfigure(hc, dev)) {
        return HCD_USBD_INTERNAL_HC_ERROR;
    }
    HcdPoolFree(dev->Selected);
    dev->Selected = NULL;
    dev->SelectedLength = 0;
    if (cd == NULL) {
        /* The unconfigure: SET_CONFIGURATION(0), whose failure is the
         * client's to see (round 2, finding 14). */
        if (!HcdThreadControl(hc, dev, 0x00, 9, 0, 0, 0, &bytes)) {
            return HCD_USBD_INTERNAL_HC_ERROR;
        }
        dev->ConfigValue = 0;
        sc->ConfigurationHandle = NULL;
        return XHCI_USBD_STATUS_SUCCESS;
    }
    total = cd->wTotalLength;
    if (cd->bLength < XHCI_PIPE_CONFIG_BYTES ||
        total < XHCI_PIPE_CONFIG_BYTES) {
        XHCI_DBG_VALUE("hcd: select refused, bLength/wTotalLength",
                       ((ULONG)cd->bLength << 16) | total);
        return HCD_USBD_INVALID_PARAMETER;
    }

    /* SELECT_INTERFACE reads the configuration selected, not the one
     * enumerated: a device with several may differ. Taken first, so a
     * configuration that succeeds always has it (round 11, finding 4). */
    dev->Selected = (PUCHAR)HcdPoolAlloc(total);
    if (dev->Selected == NULL) {
        return HCD_USBD_NO_MEMORY;
    }
    for (e = 0; e < total; e++) {
        dev->Selected[e] = ((PUCHAR)cd)[e];
    }
    dev->SelectedLength = total;

    for (dci = 0; dci < 32; dci++) {
        add[dci] = NULL;
    }
    mask = 0;
    usbd = XHCI_USBD_STATUS_SUCCESS;
    fixed = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes);
    p = (PUCHAR)&sc->Interface;
    end = (PUCHAR)urb + urb->UrbHeader.Length;
    while (usbd == XHCI_USBD_STATUS_SUCCESS && p + fixed <= end) {
        ii = (PUSBD_INTERFACE_INFORMATION)p;
        if (ii->Length < fixed || p + ii->Length > end ||
            XhciPipeFindInterface((const UCHAR *)cd, total,
                                  ii->InterfaceNumber, ii->AlternateSetting,
                                  &iface) != XHCI_PIPE_OK ||
            ii->Length < fixed + iface.EndpointCount *
                                     sizeof(USBD_PIPE_INFORMATION)) {
            XHCI_DBG_VALUE("hcd: select refused, interface length/number/alt",
                           ((ULONG)ii->Length << 16) |
                               ((ULONG)ii->InterfaceNumber << 8) |
                               ii->AlternateSetting);
            XHCI_DBG_VALUE("hcd: select refused, URB length/offset",
                           ((ULONG)urb->UrbHeader.Length << 16) |
                               (ULONG)(p - (PUCHAR)urb));
            usbd = HCD_USBD_INVALID_PARAMETER;
            break;
        }
        ii->Class = (UCHAR)iface.InterfaceClass;
        ii->SubClass = (UCHAR)iface.InterfaceSubClass;
        ii->Protocol = (UCHAR)iface.InterfaceProtocol;
        ii->InterfaceHandle = HCD_IFACE_COOKIE(iface.InterfaceNumber);
        ii->NumberOfPipes = iface.EndpointCount;
        for (e = 0; e < iface.EndpointCount; e++) {
            if (XhciPipeEndpointParams((const UCHAR *)cd +
                                           iface.EndpointOffset[e],
                                       dev->Speed, &ep) != XHCI_PIPE_OK ||
                add[ep.Dci] != NULL) {
                XHCI_DBG_VALUE("hcd: select refused, endpoint index/speed",
                               (e << 8) | dev->Speed);
                usbd = HCD_USBD_INVALID_PARAMETER;
                break;
            }
            add[ep.Dci] = hcdCfgPipeNew(hc, dev, &ep, &usbd);
            if (add[ep.Dci] == NULL) {
                break;
            }
            add[ep.Dci]->Interface = iface.InterfaceNumber;
            mask |= 1UL << ep.Dci;
            ii->Pipes[e].MaximumPacketSize = (USHORT)ep.MaxPacketSize;
            ii->Pipes[e].EndpointAddress = (UCHAR)ep.Address;
            ii->Pipes[e].Interval = (UCHAR)ep.BInterval;
            ii->Pipes[e].PipeType = (USBD_PIPE_TYPE)ep.TransferType;
            ii->Pipes[e].PipeHandle = (USBD_PIPE_HANDLE)add[ep.Dci];
        }
        p += ii->Length;
    }

    if (usbd == XHCI_USBD_STATUS_SUCCESS && mask != 0) {
        if (XhciPipeConfigurePlan(0, 0, mask, &plan) != XHCI_PIPE_OK ||
            !hcdCfgBuildInput(hc, dev, &plan, add) ||
            XhciTrbConfigureEndpoint(&trb, dev->SlotId,
                                     XhciCommonPA(ext,
                                                  ext->Layout.InputContextOffset),
                                     0) != XHCI_RING_OK) {
            usbd = HCD_USBD_INTERNAL_HC_ERROR;
        } else {
            code = HcdThreadCommand(hc, &trb, &control);
            if (code != XHCI_CC_SUCCESS) {
                usbd = (LONG)XhciPipeConfigureUsbdStatus(code);
                mask = 0;   /* nothing was enabled */
            }
        }
    }
    if (usbd == XHCI_USBD_STATUS_SUCCESS &&
        !HcdThreadControl(hc, dev, 0x00, 9,
                          (USHORT)cd->bConfigurationValue, 0, 0, &bytes)) {
        usbd = HCD_USBD_INTERNAL_HC_ERROR;
    }

    if (usbd != XHCI_USBD_STATUS_SUCCESS) {
        XHCI_DBG_VALUE("hcd: select configuration failed, USBD status",
                       (ULONG)usbd);
        HcdPoolFree(dev->Selected);
        dev->Selected = NULL;
        dev->SelectedLength = 0;
        if (mask != 0) {
            /* Enabled, then refused by the device: take them back down. */
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            for (dci = 2; dci < 32; dci++) {
                dev->Pipes[dci] = add[dci];
            }
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            (VOID)hcdCfgDeconfigure(hc, dev);
        } else {
            for (dci = 2; dci < 32; dci++) {
                if (add[dci] != NULL) {
                    hcdCfgPipeFree(hc, dev, add[dci]);
                }
            }
        }
        return usbd;
    }

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (dci = 2; dci < 32; dci++) {
        dev->Pipes[dci] = add[dci];
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    dev->ConfigValue = cd->bConfigurationValue;
    sc->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)dev;
    XHCI_DBG_VALUE("hcd: configured, slot/endpoint mask",
                   (dev->SlotId << 24) | (mask >> 8));
    return XHCI_USBD_STATUS_SUCCESS;
}

/* ----------------------------------------------------------------------- */
/* SELECT_INTERFACE                                                         */
/* ----------------------------------------------------------------------- */

/*
 * One interface to another alternate setting, on the configuration the
 * client selected (its copy, dev->Selected): the interface's open pipes are
 * brought to rest and closed, their requests completed as cancelled; one
 * Configure Endpoint drops their endpoints - and any left enabled by an
 * earlier failure (dev->Stale) - and adds the new setting's; then
 * SET_INTERFACE. A failure leaves the interface with no pipes, as a client
 * that sees the error must select again. The other interfaces' pipes are
 * untouched (keepMask). Every SET_INTERFACE failure fails the request, a
 * STALL included: USB 2.0 9.4.10 lets a device with only a default setting
 * STALL it, but then the device has not reset its endpoints' toggles while
 * Drop and Add has reset the controller's (Codex review of batch (c),
 * round 11, findings 1 and 3). Thread only.
 */
static LONG hcdCfgSelectInterface(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                  PURB urb)
{
    struct _URB_SELECT_INTERFACE *si;
    PUSBD_INTERFACE_INFORMATION ii;
    PHCD_PIPE add[32];
    XHCI_PIPE_IFACE iface;
    XHCI_PIPE_EP ep;
    XHCI_PIPE_PLAN plan;
    PXHCI_EXTENSION ext;
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG fixed;
    ULONG keep;
    ULONG old;
    ULONG mask;
    ULONG control;
    ULONG code;
    ULONG bytes;
    ULONG dci;
    ULONG e;
    LONG usbd;

    ext = &hc->Hc;
    si = &urb->UrbSelectInterface;
    ii = &si->Interface;
    fixed = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes);
    if (dev->Selected == NULL || dev->ConfigValue == 0 ||
        si->ConfigurationHandle != (USBD_CONFIGURATION_HANDLE)dev ||
        urb->UrbHeader.Length <
            FIELD_OFFSET(struct _URB_SELECT_INTERFACE, Interface) + fixed ||
        ii->Length < fixed ||
        (PUCHAR)ii + ii->Length > (PUCHAR)urb + urb->UrbHeader.Length ||
        XhciPipeFindInterface(dev->Selected, dev->SelectedLength,
                              ii->InterfaceNumber, ii->AlternateSetting,
                              &iface) != XHCI_PIPE_OK ||
        ii->Length < fixed + iface.EndpointCount *
                                 sizeof(USBD_PIPE_INFORMATION)) {
        XHCI_DBG_VALUE("hcd: select interface refused, length/number/alt",
                       ((ULONG)ii->Length << 16) |
                           ((ULONG)ii->InterfaceNumber << 8) |
                           ii->AlternateSetting);
        return HCD_USBD_INVALID_PARAMETER;
    }

    /* The interface's pipes, to rest and closed; the others kept. */
    keep = 0;
    old = 0;
    for (dci = 2; dci < 32; dci++) {
        if (dev->Pipes[dci] == NULL) {
            continue;
        }
        if (dev->Pipes[dci]->Interface == iface.InterfaceNumber) {
            old |= 1UL << dci;
            HcdIoPipePause(hc, dev->Pipes[dci]);
            (VOID)HcdIoPipeCancelAll(hc, dev->Pipes[dci]);
            if (hcdCfgQuiesce(hc, dev, dev->Pipes[dci]) ==
                XHCI_EP_STATE_RUNNING) {
                return HCD_USBD_INTERNAL_HC_ERROR;
            }
        } else {
            keep |= 1UL << dci;
        }
    }
    hcdCfgCloseMask(hc, dev, old, HCD_USBD_CANCELED);
    old |= dev->Stale;

    for (dci = 0; dci < 32; dci++) {
        add[dci] = NULL;
    }
    mask = 0;
    usbd = XHCI_USBD_STATUS_SUCCESS;
    for (e = 0; e < iface.EndpointCount; e++) {
        if (XhciPipeEndpointParams(dev->Selected + iface.EndpointOffset[e],
                                   dev->Speed, &ep) != XHCI_PIPE_OK ||
            add[ep.Dci] != NULL || (keep & (1UL << ep.Dci)) != 0) {
            XHCI_DBG_VALUE("hcd: select interface refused, endpoint/speed",
                           (e << 8) | dev->Speed);
            usbd = HCD_USBD_INVALID_PARAMETER;
            break;
        }
        add[ep.Dci] = hcdCfgPipeNew(hc, dev, &ep, &usbd);
        if (add[ep.Dci] == NULL) {
            break;
        }
        add[ep.Dci]->Interface = iface.InterfaceNumber;
        mask |= 1UL << ep.Dci;
        ii->Pipes[e].MaximumPacketSize = (USHORT)ep.MaxPacketSize;
        ii->Pipes[e].EndpointAddress = (UCHAR)ep.Address;
        ii->Pipes[e].Interval = (UCHAR)ep.BInterval;
        ii->Pipes[e].PipeType = (USBD_PIPE_TYPE)ep.TransferType;
        ii->Pipes[e].PipeHandle = (USBD_PIPE_HANDLE)add[ep.Dci];
    }

    if (usbd == XHCI_USBD_STATUS_SUCCESS && (old | mask) != 0) {
        if (XhciPipeConfigurePlan(keep, old, mask, &plan) != XHCI_PIPE_OK ||
            !hcdCfgBuildInput(hc, dev, &plan, add) ||
            XhciTrbConfigureEndpoint(&trb, dev->SlotId,
                                     XhciCommonPA(ext,
                                                  ext->Layout.InputContextOffset),
                                     0) != XHCI_RING_OK) {
            usbd = HCD_USBD_INTERNAL_HC_ERROR;
            dev->Stale = old;
        } else {
            code = HcdThreadCommand(hc, &trb, &control);
            if (code != XHCI_CC_SUCCESS) {
                /* Nothing changed: the old endpoints stay enabled with no
                 * pipe, to be dropped by the next command. */
                usbd = (LONG)XhciPipeConfigureUsbdStatus(code);
                dev->Stale = old;
            } else {
                dev->Stale = 0;
            }
        }
    } else if (usbd != XHCI_USBD_STATUS_SUCCESS) {
        dev->Stale = old;
    }
    if (usbd == XHCI_USBD_STATUS_SUCCESS &&
        !HcdThreadControl(hc, dev, 0x01, 11,
                          (USHORT)iface.AlternateSetting,
                          (USHORT)iface.InterfaceNumber, 0, &bytes)) {
        /* Enabled on the controller, not on the device: dropped by the
         * next command. */
        usbd = HCD_USBD_INTERNAL_HC_ERROR;
        dev->Stale = mask;
    }

    if (usbd != XHCI_USBD_STATUS_SUCCESS) {
        XHCI_DBG_VALUE("hcd: select interface failed, USBD status",
                       (ULONG)usbd);
        for (dci = 2; dci < 32; dci++) {
            if (add[dci] != NULL) {
                hcdCfgPipeFree(hc, dev, add[dci]);
            }
        }
        return usbd;
    }

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (dci = 2; dci < 32; dci++) {
        if (add[dci] != NULL) {
            dev->Pipes[dci] = add[dci];
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    ii->Class = (UCHAR)iface.InterfaceClass;
    ii->SubClass = (UCHAR)iface.InterfaceSubClass;
    ii->Protocol = (UCHAR)iface.InterfaceProtocol;
    ii->InterfaceHandle = HCD_IFACE_COOKIE(iface.InterfaceNumber);
    ii->NumberOfPipes = iface.EndpointCount;
    XHCI_DBG_VALUE("hcd: interface selected, number/alt",
                   (iface.InterfaceNumber << 8) | iface.AlternateSetting);
    XHCI_DBG_VALUE("hcd: interface selected, endpoint mask", mask);
    return XHCI_USBD_STATUS_SUCCESS;
}

/* ----------------------------------------------------------------------- */
/* ABORT_PIPE and RESET_PIPE                                                */
/* ----------------------------------------------------------------------- */

/* Set TR Dequeue to `pa`/`dcs` after a stop or reset. */
static ULONG hcdCfgSetDequeue(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              PHCD_PIPE pipe)
{
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG control;
    ULONG pa;
    ULONG dcs;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pa = XhciRingDequeuePA(pipe->Ring);
    dcs = XhciRingDequeueCycle(pipe->Ring);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return XhciTrbSetTrDequeue(&trb, dev->SlotId, pipe->Dci, pa, dcs) ==
               XHCI_RING_OK &&
           HcdThreadCommand(hc, &trb, &control) == XHCI_CC_SUCCESS;
}

static ULONG hcdCfgResetEndpoint(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                 PHCD_PIPE pipe)
{
    XHCI_TRB trb;
    ULONG control;

    return XhciTrbResetEndpoint(&trb, dev->SlotId, pipe->Dci, 0) ==
               XHCI_RING_OK &&
           HcdThreadCommand(hc, &trb, &control) == XHCI_CC_SUCCESS;
}


/* ----------------------------------------------------------------------- */
/* Endpoint state                                                           */
/* ----------------------------------------------------------------------- */

/* The endpoint's state as the controller keeps it in the output Device
 * Context (xHCI 6.2.3, EP State). Read after a command has completed, so
 * the controller has written it. */
static ULONG hcdCfgEpState(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           ULONG dci)
{
    ULONG offset;

    if (XhciEndpointContextOffset(&hc->Hc.Layout, dev->SlotId, dci,
                                  &offset) != XHCI_LAYOUT_OK) {
        return XHCI_EP_STATE_DISABLED;
    }
    return XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]);
}

/*
 * Bring an endpoint to a state in which its ring may be edited, by the state
 * it is in (Codex review of batch (c), round 2, finding 6): Running is
 * stopped (Stop Endpoint), Halted is reset (Reset Endpoint, TSP 0, xHCI
 * 4.6.8 - legal only there), Stopped needs nothing, and Error is left for
 * the Set TR Dequeue that every caller then issues, which is what moves it
 * to Stopped (4.8.3). Returns the state reached, or XHCI_EP_STATE_RUNNING
 * when a command failed - the controller reset is then already requested.
 */
static ULONG hcdCfgQuiesce(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           PHCD_PIPE pipe)
{
    XHCI_TRB trb;
    ULONG control;
    ULONG code;
    ULONG state;
    ULONG bytes;

    state = hcdCfgEpState(hc, dev, pipe->Dci);
    if (state == XHCI_EP_STATE_RUNNING) {
        code = 0;
        if (XhciTrbStopEndpoint(&trb, dev->SlotId, pipe->Dci, 0) ==
            XHCI_RING_OK) {
            code = HcdThreadCommand(hc, &trb, &control);
        }
        if (code != XHCI_CC_SUCCESS && code != XHCI_CC_CONTEXT_STATE_ERROR) {
            HcdSvcRequestReset(&hc->Hc);
            return XHCI_EP_STATE_RUNNING;
        }
        state = hcdCfgEpState(hc, dev, pipe->Dci);
    }
    if (state == XHCI_EP_STATE_HALTED) {
        /* Reset Endpoint keeps the controller's retry position in the
         * failed TD whatever TSP says; only Set TR Dequeue clears it (xHCI
         * 4.6.8 p.116), so it follows at once, to the software dequeue the
         * engine already moved past that TD - a later doorbell would
         * otherwise retry into a request already completed (Codex review
         * of batch (c), round 4, finding 3). */
        if (!hcdCfgResetEndpoint(hc, dev, pipe) ||
            !hcdCfgSetDequeue(hc, dev, pipe)) {
            HcdSvcRequestReset(&hc->Hc);
            return XHCI_EP_STATE_RUNNING;
        }
        /* Reset Endpoint restarted the host's data toggle (TSP 0); the
         * device's restarts with CLEAR_FEATURE(ENDPOINT_HALT), or the two
         * ends disagree and a packet is lost as a duplicate (xHCI 4.6.8
         * pp.116-117; round 5, finding 2). A control endpoint has no
         * toggle to keep: its SETUP restarts it. */
        if (pipe->Dci != 1 && pipe->TransferType != XHCI_PIPE_XFER_ISOCH &&
            !HcdThreadControl(hc, dev, 0x02, 1, 0,
                              (USHORT)pipe->EndpointAddress, 0, &bytes)) {
            /* The device did not take the clear: the two toggles may
             * disagree, which only the reset the failure asks for settles
             * (round 6). */
            HcdSvcRequestReset(&hc->Hc);
            return XHCI_EP_STATE_RUNNING;
        }
        state = XHCI_EP_STATE_STOPPED;
    }
    if (state == XHCI_EP_STATE_ERROR) {
        /* Error is left only by Set TR Dequeue, to the software dequeue -
         * before the ring is edited at all (xHCI 4.8.3; Codex review of
         * batch (c), round 3, findings 5 and 6). */
        if (!hcdCfgSetDequeue(hc, dev, pipe)) {
            HcdSvcRequestReset(&hc->Hc);
            return XHCI_EP_STATE_RUNNING;
        }
        state = XHCI_EP_STATE_STOPPED;
    }
    return state;
}

/* ----------------------------------------------------------------------- */
/* Deconfigure, abort, reset                                                */
/* ----------------------------------------------------------------------- */

/*
 * Disable every endpoint but EP0 and close the pipes. Each open endpoint is
 * stopped first, with its pipe paused and every request on it marked
 * cancelled, because a Drop - which Configure Endpoint with DC = 1 is for
 * DCIs 2 to 31 - needs the endpoint Stopped (xHCI 4.6.6; round 2, finding
 * 5). Returns 0 when the controller refused: the endpoints may still run,
 * so nothing is completed until the reset that refusal requests
 * invalidates the device.
 */
static ULONG hcdCfgDeconfigure(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    XHCI_TRB trb;
    ULONG control;
    ULONG open;
    ULONG dci;

    open = 0;
    for (dci = 2; dci < 32; dci++) {
        if (dev->Pipes[dci] == NULL) {
            continue;
        }
        open = 1;
        HcdIoPipePause(hc, dev->Pipes[dci]);
        (VOID)HcdIoPipeCancelAll(hc, dev->Pipes[dci]);
        if (hcdCfgQuiesce(hc, dev, dev->Pipes[dci]) ==
            XHCI_EP_STATE_RUNNING) {
            return 0;
        }
    }
    if (!open && dev->Stale == 0) {
        return 1;
    }
    if (XhciTrbConfigureEndpoint(&trb, dev->SlotId, 0, 1) != XHCI_RING_OK ||
        HcdThreadCommand(hc, &trb, &control) != XHCI_CC_SUCCESS) {
        HcdSvcRequestReset(&hc->Hc);
        return 0;
    }
    dev->Stale = 0;
    hcdCfgCloseAll(hc, dev, HCD_USBD_CANCELED);
    return 1;
}

/*
 * Give one endpoint a fresh context - Drop and Add of its DCI in one
 * Configure Endpoint, its ring emptied - which restarts its data toggle on
 * the controller's side: the only way to do that for an endpoint that is not
 * Halted (Reset Endpoint is not legal there, xHCI 4.6.8).
 */
static ULONG hcdCfgRecycle(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           PHCD_PIPE pipe)
{
    PXHCI_EXTENSION ext;
    PHCD_PIPE add[32];
    XHCI_PIPE_PLAN plan;
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG control;
    ULONG dci;

    ext = &hc->Hc;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    (VOID)XhciRingSetDequeue(pipe->Ring,
                             XhciRingTrbPA(pipe->Ring, pipe->Ring->Enqueue));
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    for (dci = 0; dci < 32; dci++) {
        add[dci] = NULL;
    }
    add[pipe->Dci] = pipe;
    plan.AddFlags = 1UL | (1UL << pipe->Dci);
    plan.DropFlags = 1UL << pipe->Dci;
    plan.ContextEntries = 1;
    for (dci = 2; dci < 32; dci++) {
        if (dev->Pipes[dci] != NULL || (dev->Stale & (1UL << dci)) != 0) {
            plan.ContextEntries = dci;
        }
    }
    plan.Enabled = 0;
    return hcdCfgBuildInput(hc, dev, &plan, add) &&
           XhciTrbConfigureEndpoint(&trb, dev->SlotId,
                                    XhciCommonPA(ext,
                                                 ext->Layout.InputContextOffset),
                                    0) == XHCI_RING_OK &&
           HcdThreadCommand(hc, &trb, &control) == XHCI_CC_SUCCESS;
}

/*
 * The body of an abort, on a pipe the caller has paused: mark every request
 * on it cancelled wherever it is - the map pump and the held list included
 * (round 2, finding 2) - bring the endpoint to rest (Stopped; hcdCfgQuiesce),
 * complete what is on the ring with `usbd`, leave the ring empty with Set TR
 * Dequeue at the enqueue position, and wait until every marked request has
 * completed.
 */
static LONG hcdCfgAbortPaused(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              PHCD_PIPE pipe, LONG usbd)
{
    KIRQL oldIrql;

    (VOID)HcdIoPipeCancelAll(hc, pipe);
    if (hcdCfgQuiesce(hc, dev, pipe) == XHCI_EP_STATE_RUNNING) {
        return HCD_USBD_INTERNAL_HC_ERROR;
    }
    HcdIoDrainPipe(hc, pipe, usbd);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    (VOID)XhciRingSetDequeue(pipe->Ring,
                             XhciRingTrbPA(pipe->Ring, pipe->Ring->Enqueue));
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (!hcdCfgSetDequeue(hc, dev, pipe)) {
        HcdSvcRequestReset(&hc->Hc);
        return HCD_USBD_INTERNAL_HC_ERROR;
    }
    HcdIoPipeWaitCancelled(hc, pipe);
    pipe->Halted = 0;
    pipe->DrainPending = 0;
    return XHCI_USBD_STATUS_SUCCESS;
}

/* ABORT_PIPE, and the stop-and-drain a refused retire or a removing PDO
 * owes: the body above inside the pipe's pause. */
static LONG hcdCfgAbort(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                        PHCD_PIPE pipe, LONG usbd)
{
    LONG result;

    HcdIoPipePause(hc, pipe);
    result = hcdCfgAbortPaused(hc, dev, pipe, usbd);
    if (result == XHCI_USBD_STATUS_SUCCESS) {
        HcdIoPipeResume(hc, pipe);
    }
    /* On a failure the pipe stays paused: the endpoint may still hold a
     * position no doorbell may restart, and the reset the failure asked
     * for invalidates the device, which drains what is held (Codex review
     * of batch (c), round 5, finding 1). */
    return result;
}

/* Whether any request is on the pipe, wherever it is. Controller lock not
 * held; a snapshot, read inside the pipe's pause. */
static ULONG hcdCfgPipeBusy(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    KIRQL oldIrql;
    ULONG busy;
    ULONG i;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    busy = pipe->Queue->Count != 0 || !IsListEmpty(&pipe->Held);
    for (i = 0; i < HCD_PIPE_XFERS; i++) {
        if (pipe->Xfers[i].State != HCD_XFER_FREE) {
            busy = 1;
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return busy;
}

/*
 * RESET_PIPE: inside the pipe's pause from the start, so nothing is
 * published between the check and the reset (round 3, finding 7). Anything
 * still on the pipe is aborted first. Then the controller's half by the
 * endpoint's state (round 2, finding 6) and the device's -
 * CLEAR_FEATURE(ENDPOINT_HALT) - so both ends restart the data toggle:
 *
 *   Halted    Reset Endpoint (TSP 0, which restarts the toggle), then Set TR
 *             Dequeue past the failed TD;
 *   other     brought to Stopped (hcdCfgQuiesce), then a fresh context
 *             (hcdCfgRecycle), the only toggle restart a non-Halted
 *             endpoint has (xHCI 4.6.8).
 */
static LONG hcdCfgReset(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                        PHCD_PIPE pipe)
{
    ULONG bytes;
    ULONG ok;
    LONG result;

    HcdIoPipePause(hc, pipe);
    result = XHCI_USBD_STATUS_SUCCESS;
    if (hcdCfgPipeBusy(hc, pipe)) {
        result = hcdCfgAbortPaused(hc, dev, pipe, HCD_USBD_CANCELED);
    }
    if (result == XHCI_USBD_STATUS_SUCCESS) {
        if (hcdCfgEpState(hc, dev, pipe->Dci) == XHCI_EP_STATE_HALTED) {
            ok = hcdCfgResetEndpoint(hc, dev, pipe) &&
                 hcdCfgSetDequeue(hc, dev, pipe);
        } else {
            ok = hcdCfgQuiesce(hc, dev, pipe) != XHCI_EP_STATE_RUNNING &&
                 hcdCfgRecycle(hc, dev, pipe);
        }
        if (!ok) {
            HcdSvcRequestReset(&hc->Hc);
            result = HCD_USBD_INTERNAL_HC_ERROR;
            return result;      /* left paused, as hcdCfgAbort */
        }
    } else {
        return result;          /* left paused, as hcdCfgAbort */
    }
    pipe->Halted = 0;
    if (pipe->TransferType != XHCI_PIPE_XFER_ISOCH &&
        !HcdThreadControl(hc, dev, 0x02, 1, 0,
                          (USHORT)pipe->EndpointAddress, 0, &bytes)) {
        /* As in hcdCfgQuiesce: toggles that may disagree are settled
         * only by the reset; the pipe stays paused until it (round 6). */
        HcdSvcRequestReset(&hc->Hc);
        return HCD_USBD_INTERNAL_HC_ERROR;
    }
    HcdIoPipeResume(hc, pipe);
    return result;
}

/*
 * Take a pipe's cancelled requests off it (hcd_io.c marked them). With the
 * pipe paused and the endpoint brought to rest, each cancelled record still
 * held completes, and each on the ring leaves the engine's queue and
 * completes as CANCELED. The TD the controller stopped in is the queue's
 * head and is never rewritten (xHCI 4.6.9): when the head itself is
 * cancelled - or the endpoint sits in Error - Set TR Dequeue moves to the
 * oldest survivor's first TRB, or to the enqueue position when none
 * survives; a cancelled TD behind a survivor becomes No Ops in place. The
 * miniport's placement (xhciEpPlaceDequeue, branch 1.2.0.0).
 */
static VOID hcdCfgCancelPipe(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                             PHCD_PIPE pipe)
{
    PXHCI_TRANSFER cancelled[HCD_PIPE_XFERS];
    PXHCI_TRANSFER t;
    PXHCI_TRANSFER head;
    PXHCI_TRANSFER survivor;
    PLIST_ENTRY entry;
    PLIST_ENTRY next;
    PHCD_XFER x;
    KIRQL oldIrql;
    ULONG state;
    ULONG count;
    ULONG move;
    ULONG index;
    ULONG i;

    HcdIoPipePause(hc, pipe);
    state = hcdCfgQuiesce(hc, dev, pipe);
    if (state == XHCI_EP_STATE_RUNNING) {
        return;                 /* left paused, as hcdCfgAbort */
    }

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe->CancelPending = 0;
    for (entry = pipe->Held.Flink; entry != &pipe->Held; entry = next) {
        next = entry->Flink;
        x = CONTAINING_RECORD(entry, HCD_XFER, Link);
        if (x->CancelRequested) {
            RemoveEntryList(entry);
            x->Engine = 0;
            x->Status = HCD_USBD_CANCELED;
            x->State = HCD_XFER_DONE;
            InsertTailList(&hc->DoneList, &x->Link);
        }
    }
    head = pipe->Queue->Head;
    survivor = NULL;
    count = 0;
    for (t = head; t != NULL; t = t->Next) {
        if (t != &dev->Ep0Xfer && ((PHCD_XFER)t)->CancelRequested) {
            if (count < HCD_PIPE_XFERS) {
                cancelled[count++] = t;
            }
        } else if (survivor == NULL) {
            survivor = t;
        }
    }
    move = (state == XHCI_EP_STATE_ERROR);
    for (i = 0; i < count; i++) {
        t = cancelled[i];
        if (t != head) {
            for (index = t->FirstIndex;; index = XhciRingNextIndex(
                                                 pipe->Ring, index)) {
                (VOID)XhciRingNoOpAt(pipe->Ring, index);
                if (index == t->LastIndex) {
                    break;
                }
            }
        } else {
            move = 1;
        }
        (VOID)XhciXferQueueRemove(pipe->Queue, t);
        t->UsbdStatus = HCD_USBD_CANCELED;
        HcdIoRetired(hc, t);
    }
    if (move) {
        (VOID)XhciRingSetDequeue(
            pipe->Ring,
            XhciRingTrbPA(pipe->Ring, survivor != NULL ? survivor->FirstIndex
                                                       : pipe->Ring->Enqueue));
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);

    if (move && !hcdCfgSetDequeue(hc, dev, pipe)) {
        HcdSvcRequestReset(&hc->Hc);
        HcdIoDeferred(hc);
        return;                 /* left paused, as hcdCfgAbort */
    }
    if (pipe->Queue->Count != 0) {
        XhciWriteDoorbell(&hc->Hc, dev->SlotId, pipe->Dci);
    }
    HcdIoDeferred(hc);
    HcdIoPipeResume(hc, pipe);
}

/* ----------------------------------------------------------------------- */
/* Cancellation                                                             */
/* ----------------------------------------------------------------------- */

/* Every pipe with a cancelled record. Thread only, powered, the controller
 * not halted. */
VOID HcdCfgCancelService(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    ULONG work;
    ULONG slot;
    ULONG dci;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    work = hc->CancelWork;
    hc->CancelWork = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (!work) {
        return;
    }
    for (slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        dev = hc->SlotDevice[slot];
        if (dev == NULL || dev->Gone) {
            continue;
        }
        if (dev->AbortAll) {
            /* Its PDO is being removed (hcd_pdo.c): every open pipe is
             * aborted so the PDO's pending URBs complete before it does. */
            dev->AbortAll = 0;
            /* EP0 too: its control URBs count against the PDO as much as
             * the other pipes' (round 3, finding 3). */
            (VOID)hcdCfgAbort(hc, dev, &dev->Ep0Pipe, HCD_USBD_CANCELED);
            for (dci = 2; dci < 32; dci++) {
                if (dev->Pipes[dci] != NULL) {
                    (VOID)hcdCfgAbort(hc, dev, dev->Pipes[dci],
                                      HCD_USBD_CANCELED);
                }
            }
        }
        for (dci = 1; dci < 32; dci++) {
            pipe = (dci == 1) ? &dev->Ep0Pipe : dev->Pipes[dci];
            if (pipe != NULL && pipe->DrainPending) {
                /* A refused retire (hcd_dev.c): stop and drain. */
                (VOID)hcdCfgAbort(hc, dev, pipe, HCD_USBD_INTERNAL_HC_ERROR);
            } else if (pipe != NULL && pipe->CancelPending) {
                hcdCfgCancelPipe(hc, dev, pipe);
            }
        }
    }
}

/* ----------------------------------------------------------------------- */
/* The thread's service                                                     */
/* ----------------------------------------------------------------------- */

/* Every pended URB, in order. Thread only, powered, the controller not
 * halted (HcdEnumService). */
VOID HcdCfgService(PHCD_CONTROLLER hc)
{
    PLIST_ENTRY entry;
    PHCD_USB_DEVICE dev;
    PHCD_PIPE pipe;
    PIRP irp;
    PURB urb;
    KIRQL oldIrql;
    LONG usbd;

    for (;;) {
        if (hc->Hc.ControllerFailed || hc->ScratchTainted) {
            /* A command or EP0 transfer just failed into the reset: the
             * rest wait for the recovery, whose invalidation completes
             * them with their devices (round 2, finding 10). */
            break;
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (IsListEmpty(&hc->SlowIrps)) {
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            break;
        }
        entry = RemoveHeadList(&hc->SlowIrps);
        XhciControllerLockRelease(&hc->Hc, oldIrql);

        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        dev = (PHCD_USB_DEVICE)irp->Tail.Overlay.DriverContext[0];
        urb = (PURB)IoGetCurrentIrpStackLocation(irp)
                  ->Parameters.Others.Argument1;
        if (dev->Gone) {
            hcdCfgComplete(dev, irp, urb, HCD_USBD_DEVICE_GONE);
            continue;
        }
        switch (urb->UrbHeader.Function) {
        case HCD_URB_SELECT_CONFIGURATION:
            usbd = hcdCfgSelect(hc, dev, urb);
            break;
        case HCD_URB_SELECT_INTERFACE:
            usbd = hcdCfgSelectInterface(hc, dev, urb);
            break;
        case HCD_URB_ABORT_PIPE:
        case HCD_URB_RESET_PIPE:
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            pipe = HcdCfgPipe(dev, urb->UrbPipeRequest.PipeHandle);
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            if (pipe == NULL) {
                usbd = HCD_USBD_INVALID_PIPE;
            } else if (urb->UrbHeader.Function == HCD_URB_ABORT_PIPE) {
                usbd = hcdCfgAbort(hc, dev, pipe, HCD_USBD_CANCELED);
            } else {
                usbd = hcdCfgReset(hc, dev, pipe);
            }
            break;
        default:
            usbd = (LONG)USBD_STATUS_INVALID_URB_FUNCTION;
            break;
        }
        hcdCfgComplete(dev, irp, urb, usbd);
    }
}
