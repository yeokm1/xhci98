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
 * IOCTL_INTERNAL_USB_RESET_PORT shares the queue (hcdCfgResetPort): it
 * carries no URB, which hcdCfgUrbOf tells apart.
 *
 * A device the bus splits (26-A.7) is configured once by the bus
 * (HcdCfgParentConfigure); each function PDO's select then opens and closes
 * only its own interfaces' endpoints beside its siblings'
 * (hcdCfgSelectFunction), a removed function's are released the same way
 * (HcdCfgReleaseFunction), and RESET_PORT restores the whole device.
 *
 * IRQL: PASSIVE_LEVEL (the controller thread), except HcdCfgQueue,
 * HcdCfgReleaseFunction and HcdCfgPipe (<= DISPATCH_LEVEL).
 */

#include <ntddk.h>
#include <usbdi.h>
#include <usbioctl.h>
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

/* The URB a pended IRP carries, or NULL for RESET_PORT, which carries none.
 * By the control code, never by Argument1: Parameters.Others.Argument1
 * aliases OutputBufferLength, so a request without a URB need not hold NULL
 * there. IRQL: any. */
static PURB hcdCfgUrbOf(PIRP irp)
{
    PIO_STACK_LOCATION stack;

    stack = IoGetCurrentIrpStackLocation(irp);
    if (stack->Parameters.DeviceIoControl.IoControlCode !=
        IOCTL_INTERNAL_USB_SUBMIT_URB) {
        return NULL;
    }
    return (PURB)stack->Parameters.Others.Argument1;
}

/* Pend one IRP for the thread - a URB, or RESET_PORT; the caller's device
 * reference passes to it. IRQL: <= DISPATCH_LEVEL. */
NTSTATUS HcdCfgQueue(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                     PHCD_DEVICE_PDO pdo, PIRP irp)
{
    KIRQL oldIrql;
    PURB urb;

    urb = hcdCfgUrbOf(irp);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (dev->Gone) {
        /* The thread has begun freeing the device and has flushed its
         * queued URBs already: one queued now would hold a reference
         * nothing ever returns (round 3, finding 1). Refused at the next
         * tick, not inline (round 8, finding 4). */
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        (VOID)InterlockedDecrement(&dev->Refs);
        if (urb == NULL) {
            /* No URB to carry a status, and no client resubmits a port
             * reset from its completion routine: completed now. */
            return HcdCompleteIrp(irp, STATUS_NO_SUCH_DEVICE, 0);
        }
        return HcdIoRefuseLater(pdo, irp, urb, HCD_USBD_DEVICE_GONE);
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
    if (urb != NULL) {
        urb->UrbHeader.Status = usbd;
        XHCI_DBG_VALUE("hcd: thread URB done, function/status",
                       ((ULONG)urb->UrbHeader.Function << 24) |
                           ((ULONG)usbd & 0x00FFFFFFUL));
        irp->IoStatus.Status = (NTSTATUS)XhciPipeNtStatus((ULONG)usbd);
    } else if (usbd == XHCI_USBD_STATUS_SUCCESS) {
        irp->IoStatus.Status = STATUS_SUCCESS;
    } else if (usbd == HCD_USBD_DEVICE_GONE) {
        irp->IoStatus.Status = STATUS_NO_SUCH_DEVICE;
    } else {
        irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
    }
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
        hcdCfgComplete(dev, irp, hcdCfgUrbOf(irp), HCD_USBD_DEVICE_GONE);
    }
}

/* ----------------------------------------------------------------------- */
/* The matrix's select accounting (xhci_counters.h)                         */
/* ----------------------------------------------------------------------- */

/* An endpoint descriptor a select could not open: UNSUPPORTED is a type or
 * speed this bus does not serve; a malformed descriptor, or a DCI named
 * twice or already a sibling's, is its parameters. */
static VOID hcdCfgCountEndpoint(PHCD_CONTROLLER hc, ULONG answer)
{
    if (answer == XHCI_PIPE_UNSUPPORTED) {
        hc->Counters.EndpointRefusalsType++;
    } else {
        hc->Counters.EndpointRefusalsParams++;
    }
}

/* A Configure Endpoint that was to add `mask`'s endpoints and did not; code
 * 0 is one never issued. The split is XhciPipeConfigureUsbdStatus's, the
 * resource class apart from the two bandwidth codes. */
static VOID hcdCfgCountConfigure(PHCD_CONTROLLER hc, ULONG code, ULONG mask)
{
    ULONG n;

    for (n = 0; mask != 0; mask &= mask - 1) {
        n++;
    }
    if (code == XHCI_CC_RESOURCE_ERROR) {
        hc->Counters.EndpointsNoResources += n;
    } else if (code == XHCI_CC_BANDWIDTH_ERROR ||
               code == XHCI_CC_SECONDARY_BANDWIDTH) {
        hc->Counters.EndpointsNoBandwidth += n;
    } else {
        hc->Counters.EndpointConfigureFailures++;
    }
}

/* The endpoints a select's URB named, once the interface naming them is
 * found; at the select's end they are all opened or all refused, so the
 * matrix's identity holds at every return that follows a count. */
static VOID hcdCfgCountAsked(PHCD_CONTROLLER hc, PULONG asked, ULONG n)
{
    *asked += n;
    hc->Counters.SelectEndpointsRequested += n;
}

static LONG hcdCfgCountEnd(PHCD_CONTROLLER hc, ULONG asked, LONG usbd)
{
    if (usbd == XHCI_USBD_STATUS_SUCCESS) {
        hc->Counters.EndpointsOpened += asked;
    } else {
        hc->Counters.SelectEndpointsRefused += asked;
    }
    return usbd;
}

/* Every select's answer, counted where every exit meets: the endpoint
 * counts above see only the exits that follow a parsed interface, and a
 * select whose endpoints all opened can still fail at the device.
 *
 * A failure is not counted once the device is proven to have left: its
 * record is gone or no longer its port's, or its root port reads
 * disconnected or with a connect change the enumeration has yet to take
 * (an unplug and replug). A PORTSC of all ones proves nothing about the
 * device - the register could not be read - so that failure is counted
 * (Codex review round 25). Behind hubs the device is counted only when
 * every hub port on its path still reads connected and enabled with no
 * change pending, or unread in a status-change report, and its root port
 * as above (HcdHubPathPresent: a GET_STATUS per hub, from the thread).
 * A select racing an ordinary unplug fails at
 * SET_CONFIGURATION or SET_INTERFACE before HcdEnumService sees the port
 * change, and counting it would fail a correct matrix run. A device still
 * on its port that refuses - SET_CONFIGURATION(0) among them - is counted
 * (Codex review round 24, finding 1). IRQL: PASSIVE_LEVEL (the thread,
 * which owns the port records). */
static VOID hcdCfgCountSelect(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              LONG usbd)
{
    if (usbd == XHCI_USBD_STATUS_SUCCESS ||
        usbd == HCD_USBD_BUFFER_TOO_SMALL) {
        return;
    }
    if (dev->Gone || dev->Location == 0 || dev->Location > HCD_PORT_COUNT ||
        hc->Ports[dev->Location - 1].Device != dev) {
        return;
    }
    /* Behind hubs, every port on the path is asked as well (Codex review
     * of 23e7715, finding 6). */
    if (!HcdHubPathPresent(hc, &hc->Ports[dev->Location - 1])) {
        return;
    }
    hc->Counters.SelectsFailed++;
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
 * Context rewritten whole (HcdDeviceSlotParams: the device's route, root
 * port, speed and TT fields, a hub's marking, Context Entries the highest
 * DCI enabled after it), and an Endpoint Context
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
    HcdDeviceSlotParams(dev, 1, &sp);
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
    if (ep->TransferType == XHCI_PIPE_XFER_ISOCH) {
        pipe->Iso = (PHCD_ISO_BLOCK)HcdPoolAlloc(
            (ULONG)(HCD_PIPE_XFERS * sizeof(HCD_ISO_BLOCK)));
        if (pipe->Iso == NULL) {
            HcdPoolFree(pipe);
            *usbd = HCD_USBD_NO_MEMORY;
            return NULL;
        }
    }

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
        HcdPoolFree(pipe->Iso);
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
        HcdPoolFree(pipe->Iso);
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
    HcdPoolFree(pipe->Iso);
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

/*
 * A device the bus splits is configured by the bus, once, before any of its
 * function PDOs exists (design record 13 section 10.8): SET_CONFIGURATION
 * with the configuration it read (index 0, its first), every interface at
 * alternate 0, no endpoint open. Each function's SELECT_CONFIGURATION then
 * opens only its own interfaces' endpoints against it
 * (hcdCfgSelectFunction), and RESET_PORT replays it as it replays a
 * client's (ConfigValue, Alternate). Returns 0 with the device left as it
 * was when the request fails. Thread only, powered.
 */
ULONG HcdCfgParentConfigure(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    ULONG bytes;
    ULONG i;

    if (dev->Config == NULL || dev->ConfigLength < XHCI_PIPE_CONFIG_BYTES ||
        dev->Config[5] == 0) {
        return 0;
    }
    HcdPoolFree(dev->Selected);
    dev->Selected = (PUCHAR)HcdPoolAlloc(dev->ConfigLength);
    dev->SelectedLength = 0;
    if (dev->Selected == NULL) {
        return 0;
    }
    for (i = 0; i < dev->ConfigLength; i++) {
        dev->Selected[i] = dev->Config[i];
    }
    if (!HcdThreadControl(hc, dev, 0x00, 9, (USHORT)dev->Config[5], 0, 0,
                          &bytes)) {
        HcdPoolFree(dev->Selected);
        dev->Selected = NULL;
        return 0;
    }
    dev->SelectedLength = dev->ConfigLength;
    dev->ConfigValue = dev->Config[5];
    for (i = 0; i < 32; i++) {
        dev->Alternate[i] = 0;
    }
    dev->IfaceUsed = 0;
    dev->Split = 1;
    XHCI_DBG_VALUE("hcd: split device configured, port/slot/value",
                   (dev->Port << 16) | (dev->SlotId << 8) | dev->ConfigValue);
    return 1;
}

/*
 * A hub's status-change endpoint (hcd_hub.c; design record 13 section 10.3
 * step 3): one pipe on a pool ring, opened by the one Configure Endpoint
 * that also carries the hub marking HcdDeviceSlotParams adds once
 * dev->HubMarked is set - an Evaluate Context cannot set those fields. The
 * hub has no client, so no URB ever names this pipe; its one record is the
 * device's HubXfer. Returns the pipe, listed in dev->Pipes, or NULL with
 * nothing enabled. Thread only, powered.
 */
PHCD_PIPE HcdCfgHubOpen(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                        const XHCI_PIPE_EP *ep, ULONG iface)
{
    PHCD_PIPE add[32];
    XHCI_PIPE_PLAN plan;
    PXHCI_EXTENSION ext;
    XHCI_TRB trb;
    KIRQL oldIrql;
    PHCD_PIPE pipe;
    ULONG control;
    ULONG code;
    ULONG dci;
    LONG usbd;

    ext = &hc->Hc;
    if (ep->Dci < 2 || ep->Dci > 31 || dev->Pipes[ep->Dci] != NULL) {
        return NULL;
    }
    usbd = XHCI_USBD_STATUS_SUCCESS;
    pipe = hcdCfgPipeNew(hc, dev, ep, &usbd);
    if (pipe == NULL) {
        return NULL;
    }
    pipe->Interface = iface;
    for (dci = 0; dci < 32; dci++) {
        add[dci] = NULL;
    }
    add[ep->Dci] = pipe;
    code = 0;
    if (XhciPipeConfigurePlan(0, 0, XHCI_PIPE_DCI_BIT(ep->Dci), &plan) ==
            XHCI_PIPE_OK &&
        hcdCfgBuildInput(hc, dev, &plan, add) &&
        XhciTrbConfigureEndpoint(&trb, dev->SlotId,
                                 XhciCommonPA(ext,
                                              ext->Layout.InputContextOffset),
                                 0) == XHCI_RING_OK) {
        code = HcdThreadCommand(hc, &trb, &control);
    }
    if (code != XHCI_CC_SUCCESS) {
        XHCI_DBG_VALUE("hcd: hub Configure Endpoint failed, slot/code",
                       (dev->SlotId << 8) | code);
        hcdCfgPipeFree(hc, dev, pipe);
        return NULL;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->Pipes[ep->Dci] = pipe;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return pipe;
}

/* ----------------------------------------------------------------------- */
/* SELECT_CONFIGURATION                                                     */
/* ----------------------------------------------------------------------- */

/* An interface record too short for its alternate's pipes is answered with
 * the length it needs, as both shipping usbport binaries answer it (Windows
 * 2000 at image VA 0x2A963, NUSB at 0x2A181; external/reactos/usbport/
 * device.c, USBPORT_InitInterfaceInfo), so a caller that resizes from the
 * returned Length can retry (Codex review of batch (c), round 20, finding
 * 3). */
static VOID hcdCfgNeedLength(PUSBD_INTERFACE_INFORMATION ii,
                             const XHCI_PIPE_IFACE *iface)
{
    ii->Length = (USHORT)(FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes) +
                          iface->EndpointCount *
                              sizeof(USBD_PIPE_INFORMATION));
}

static LONG hcdCfgSelect(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, PURB urb)
{
    struct _URB_SELECT_CONFIGURATION *sc;
    PUSB_CONFIGURATION_DESCRIPTOR cd;
    PUSBD_INTERFACE_INFORMATION ii;
    PHCD_PIPE add[32];
    UCHAR alt[32];
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
    ULONG asked;
    ULONG answer;
    ULONG dci;
    ULONG e;
    LONG usbd;

    ext = &hc->Hc;
    asked = 0;
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
    /* Nothing of the old configuration is open now, so a RESET_PORT before
     * this select succeeds restores none of it. */
    dev->ConfigValue = 0;
    for (dci = 0; dci < 32; dci++) {
        dev->Alternate[dci] = 0;
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
        alt[dci] = 0;
    }
    mask = 0;
    usbd = XHCI_USBD_STATUS_SUCCESS;
    fixed = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes);
    p = (PUCHAR)&sc->Interface;
    end = (PUCHAR)urb + urb->UrbHeader.Length;
    while (usbd == XHCI_USBD_STATUS_SUCCESS && p + fixed <= end) {
        ii = (PUSBD_INTERFACE_INFORMATION)p;
        /* An interface number past 31 is refused, not served: the
         * alternate each one is at is kept for RESET_PORT's replay in a
         * 32-entry table (Codex review of batch (c), round 16, finding 5). */
        if (ii->Length < fixed || p + ii->Length > end ||
            ii->InterfaceNumber >= 32 ||
            XhciPipeFindInterface((const UCHAR *)cd, total,
                                  ii->InterfaceNumber, ii->AlternateSetting,
                                  &iface) != XHCI_PIPE_OK) {
            usbd = HCD_USBD_INVALID_PARAMETER;
        } else {
            hcdCfgCountAsked(hc, &asked, iface.EndpointCount);
            if (ii->Length < fixed + iface.EndpointCount *
                                         sizeof(USBD_PIPE_INFORMATION)) {
                usbd = HCD_USBD_BUFFER_TOO_SMALL;
            }
        }
        if (usbd != XHCI_USBD_STATUS_SUCCESS) {
            XHCI_DBG_VALUE("hcd: select refused, interface length/number/alt",
                           ((ULONG)ii->Length << 16) |
                               ((ULONG)ii->InterfaceNumber << 8) |
                               ii->AlternateSetting);
            XHCI_DBG_VALUE("hcd: select refused, URB length/offset",
                           ((ULONG)urb->UrbHeader.Length << 16) |
                               (ULONG)(p - (PUCHAR)urb));
            if (usbd == HCD_USBD_BUFFER_TOO_SMALL) {
                hcdCfgNeedLength(ii, &iface);
            }
            break;
        }
        ii->Class = (UCHAR)iface.InterfaceClass;
        ii->SubClass = (UCHAR)iface.InterfaceSubClass;
        ii->Protocol = (UCHAR)iface.InterfaceProtocol;
        ii->InterfaceHandle = HCD_IFACE_COOKIE(iface.InterfaceNumber);
        ii->NumberOfPipes = iface.EndpointCount;
        if (iface.InterfaceNumber < 32) {
            alt[iface.InterfaceNumber] = (UCHAR)iface.AlternateSetting;
        }
        for (e = 0; e < iface.EndpointCount; e++) {
            answer = XhciPipeEndpointParams((const UCHAR *)cd +
                                                iface.EndpointOffset[e],
                                            HcdDevicePipeSpeed(hc, dev), &ep);
            if (answer != XHCI_PIPE_OK || add[ep.Dci] != NULL) {
                XHCI_DBG_VALUE("hcd: select refused, endpoint index/speed",
                               (e << 8) | dev->Speed);
                hcdCfgCountEndpoint(hc, answer);
                usbd = HCD_USBD_INVALID_PARAMETER;
                break;
            }
            add[ep.Dci] = hcdCfgPipeNew(hc, dev, &ep, &usbd);
            if (add[ep.Dci] == NULL) {
                hc->Counters.EndpointRefusalsPool++;
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
            hcdCfgCountConfigure(hc, 0, mask);
            usbd = HCD_USBD_INTERNAL_HC_ERROR;
        } else {
            code = HcdThreadCommand(hc, &trb, &control);
            if (code != XHCI_CC_SUCCESS) {
                hcdCfgCountConfigure(hc, code, mask);
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
    /* SET_CONFIGURATION leaves every interface at its alternate 0 (USB 2.0
     * 9.4.7), so one the client selected at another needs its
     * SET_INTERFACE here, or the device's endpoints would not be the ones
     * just opened. */
    for (dci = 0; dci < 32 && usbd == XHCI_USBD_STATUS_SUCCESS; dci++) {
        if (alt[dci] != 0 &&
            !HcdThreadControl(hc, dev, 0x01, 11, (USHORT)alt[dci],
                              (USHORT)dci, 0, &bytes)) {
            XHCI_DBG_VALUE("hcd: select, SET_INTERFACE failed, number/alt",
                           (dci << 8) | alt[dci]);
            usbd = HCD_USBD_INTERNAL_HC_ERROR;
        }
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
        return hcdCfgCountEnd(hc, asked, usbd);
    }

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (dci = 2; dci < 32; dci++) {
        dev->Pipes[dci] = add[dci];
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    dev->ConfigValue = cd->bConfigurationValue;
    for (dci = 0; dci < 32; dci++) {
        dev->Alternate[dci] = alt[dci];
    }
    sc->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)dev;
    XHCI_DBG_VALUE("hcd: configured, slot/endpoint mask",
                   (dev->SlotId << 24) | (mask >> 8));
    return hcdCfgCountEnd(hc, asked, XHCI_USBD_STATUS_SUCCESS);
}

/* ----------------------------------------------------------------------- */
/* SELECT_INTERFACE                                                         */
/* ----------------------------------------------------------------------- */

/* The configuration handle a select returns and SELECT_INTERFACE quotes:
 * the device for a device PDO, the PDO itself for a function, so a
 * sibling's handle is refused. */
static USBD_CONFIGURATION_HANDLE hcdCfgHandle(PHCD_USB_DEVICE dev,
                                              PHCD_DEVICE_PDO pdo)
{
    return pdo->Function ? (USBD_CONFIGURATION_HANDLE)pdo
                         : (USBD_CONFIGURATION_HANDLE)dev;
}

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
 * round 11, findings 1 and 3). From a function PDO the handle is the PDO's
 * own and the interface must be one of its (design record 13 section
 * 10.9). The interface information's own Length bounds it, as usbport has
 * it (external/reactos/usbport/device.c, USBPORT_HandleSelectInterface and
 * USBPORT_InitInterfaceInfo): UrbHeader.Length is not read - usbport
 * rewrites it - and a Length too short for the alternate's pipes is
 * USBD_STATUS_BUFFER_TOO_SMALL. Windows 2000's usbaudio.sys sends its
 * alternate-1 select with an interface Length of 0x24, room for its one
 * pipe, and every one was refused here while the header was read (c15,
 * 2026-10-03). Thread only.
 */
static LONG hcdCfgSelectInterface(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                  PHCD_DEVICE_PDO pdo, PURB urb)
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
    ULONG asked;
    ULONG answer;
    ULONG dci;
    ULONG e;
    LONG usbd;

    ext = &hc->Hc;
    asked = 0;
    si = &urb->UrbSelectInterface;
    ii = &si->Interface;
    fixed = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes);
    if (dev->Selected == NULL || dev->ConfigValue == 0 ||
        si->ConfigurationHandle != hcdCfgHandle(dev, pdo)) {
        XHCI_DBG_VALUE("hcd: select interface refused, handle/selected",
                       ((si->ConfigurationHandle != hcdCfgHandle(dev, pdo))
                            ? 0x100UL : 0UL) |
                           ((dev->Selected == NULL) ? 0x10UL : 0UL) |
                           ((dev->ConfigValue == 0) ? 1UL : 0UL));
        return HCD_USBD_INVALID_PARAMETER;
    }
    if (ii->Length < fixed || ii->InterfaceNumber >= 32 ||
        (pdo->Function &&
         (pdo->InterfaceMask & (1UL << ii->InterfaceNumber)) == 0) ||
        XhciPipeFindInterface(dev->Selected, dev->SelectedLength,
                              ii->InterfaceNumber, ii->AlternateSetting,
                              &iface) != XHCI_PIPE_OK) {
        XHCI_DBG_VALUE("hcd: select interface refused, length/number/alt",
                       ((ULONG)ii->Length << 16) |
                           ((ULONG)ii->InterfaceNumber << 8) |
                           ii->AlternateSetting);
        return HCD_USBD_INVALID_PARAMETER;
    }
    hcdCfgCountAsked(hc, &asked, iface.EndpointCount);
    if (ii->Length < fixed + iface.EndpointCount *
                                 sizeof(USBD_PIPE_INFORMATION)) {
        XHCI_DBG_VALUE("hcd: select interface too small, length/number/alt",
                       ((ULONG)ii->Length << 16) |
                           ((ULONG)ii->InterfaceNumber << 8) |
                           ii->AlternateSetting);
        hcdCfgNeedLength(ii, &iface);
        return hcdCfgCountEnd(hc, asked, HCD_USBD_BUFFER_TOO_SMALL);
    }
    if ((ULONG)urb->UrbHeader.Length <
        FIELD_OFFSET(struct _URB_SELECT_INTERFACE, Interface) +
            (ULONG)ii->Length) {
        /* Served all the same, as usbport serves it; traced so a guest run
         * shows whether this was c15's refusal. */
        XHCI_DBG_VALUE("hcd: select interface, URB header short, header/length",
                       ((ULONG)urb->UrbHeader.Length << 16) | ii->Length);
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
                return hcdCfgCountEnd(hc, asked, HCD_USBD_INTERNAL_HC_ERROR);
            }
        } else {
            keep |= 1UL << dci;
        }
    }
    hcdCfgCloseMask(hc, dev, old, HCD_USBD_CANCELED);
    old |= dev->Stale;
    if (iface.InterfaceNumber < 32) {
        /* No pipe of the interface is open until this succeeds: a reset
         * meanwhile leaves it at the alternate 0 the reset gives it. */
        dev->Alternate[iface.InterfaceNumber] = 0;
    }

    for (dci = 0; dci < 32; dci++) {
        add[dci] = NULL;
    }
    mask = 0;
    usbd = XHCI_USBD_STATUS_SUCCESS;
    for (e = 0; e < iface.EndpointCount; e++) {
        answer = XhciPipeEndpointParams(dev->Selected +
                                            iface.EndpointOffset[e],
                                        HcdDevicePipeSpeed(hc, dev), &ep);
        if (answer != XHCI_PIPE_OK || add[ep.Dci] != NULL ||
            (keep & (1UL << ep.Dci)) != 0) {
            XHCI_DBG_VALUE("hcd: select interface refused, endpoint/speed",
                           (e << 8) | dev->Speed);
            hcdCfgCountEndpoint(hc, answer);
            usbd = HCD_USBD_INVALID_PARAMETER;
            break;
        }
        add[ep.Dci] = hcdCfgPipeNew(hc, dev, &ep, &usbd);
        if (add[ep.Dci] == NULL) {
            hc->Counters.EndpointRefusalsPool++;
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
            hcdCfgCountConfigure(hc, 0, mask);
            usbd = HCD_USBD_INTERNAL_HC_ERROR;
            dev->Stale = old;
        } else {
            code = HcdThreadCommand(hc, &trb, &control);
            if (code != XHCI_CC_SUCCESS) {
                /* Nothing changed: the old endpoints stay enabled with no
                 * pipe, to be dropped by the next command. */
                hcdCfgCountConfigure(hc, code, mask);
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
        return hcdCfgCountEnd(hc, asked, usbd);
    }

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (dci = 2; dci < 32; dci++) {
        if (add[dci] != NULL) {
            dev->Pipes[dci] = add[dci];
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (iface.InterfaceNumber < 32) {
        dev->Alternate[iface.InterfaceNumber] = (UCHAR)iface.AlternateSetting;
        if (mask != 0) {
            dev->IfaceUsed |= 1UL << iface.InterfaceNumber;
        }
    }
    ii->Class = (UCHAR)iface.InterfaceClass;
    ii->SubClass = (UCHAR)iface.InterfaceSubClass;
    ii->Protocol = (UCHAR)iface.InterfaceProtocol;
    ii->InterfaceHandle = HCD_IFACE_COOKIE(iface.InterfaceNumber);
    ii->NumberOfPipes = iface.EndpointCount;
    XHCI_DBG_VALUE("hcd: interface selected, number/alt",
                   (iface.InterfaceNumber << 8) | iface.AlternateSetting);
    XHCI_DBG_VALUE("hcd: interface selected, endpoint mask", mask);
    return hcdCfgCountEnd(hc, asked, XHCI_USBD_STATUS_SUCCESS);
}

/* ----------------------------------------------------------------------- */
/* SELECT_CONFIGURATION from a function PDO                                 */
/* ----------------------------------------------------------------------- */

/* The open pipes of the function whose interfaces are `interfaceMask`,
 * brought to rest (paused, cancelled, quiesced) and closed; *keep gets the
 * siblings' DCIs, the return the function's. 0xFFFFFFFF when a quiesce
 * failed: the reset it requested settles the device, and the pipes stay
 * paused until it, as hcdCfgAbort leaves them. */
static ULONG hcdCfgCloseFunction(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                 ULONG interfaceMask, PULONG keep)
{
    PHCD_PIPE pipe;
    ULONG old;
    ULONG dci;

    *keep = 0;
    old = 0;
    for (dci = 2; dci < 32; dci++) {
        pipe = dev->Pipes[dci];
        if (pipe == NULL) {
            continue;
        }
        if (pipe->Interface >= 32UL ||
            (interfaceMask & (1UL << pipe->Interface)) == 0) {
            *keep |= 1UL << dci;
            continue;
        }
        old |= 1UL << dci;
        HcdIoPipePause(hc, pipe);
        (VOID)HcdIoPipeCancelAll(hc, pipe);
        if (hcdCfgQuiesce(hc, dev, pipe) == XHCI_EP_STATE_RUNNING) {
            return 0xFFFFFFFFUL;
        }
    }
    hcdCfgCloseMask(hc, dev, old, HCD_USBD_CANCELED);
    return old;
}

/* One Configure Endpoint: `old` (and the stale) dropped, `mask`'s added
 * from `add`, `keep` beside. 1 on success; on a failure Stale records what
 * the controller may still have enabled with no pipe, for the next
 * Configure Endpoint to drop. */
static ULONG hcdCfgReplace(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           ULONG keep, ULONG old, ULONG mask, PHCD_PIPE *add)
{
    PXHCI_EXTENSION ext;
    XHCI_PIPE_PLAN plan;
    XHCI_TRB trb;
    ULONG control;
    ULONG code;

    ext = &hc->Hc;
    old |= dev->Stale;
    if ((old | mask) == 0) {
        return 1;
    }
    code = 0;
    if (XhciPipeConfigurePlan(keep, old, mask, &plan) != XHCI_PIPE_OK ||
        !hcdCfgBuildInput(hc, dev, &plan, add) ||
        XhciTrbConfigureEndpoint(&trb, dev->SlotId,
                                 XhciCommonPA(ext,
                                              ext->Layout.InputContextOffset),
                                 0) != XHCI_RING_OK ||
        (code = HcdThreadCommand(hc, &trb, &control)) != XHCI_CC_SUCCESS) {
        /* A release adds nothing, and refuses no endpoint a client asked
         * for. */
        if (mask != 0) {
            hcdCfgCountConfigure(hc, code, mask);
        }
        dev->Stale = old;
        return 0;
    }
    dev->Stale = 0;
    return 1;
}

/* Whether interface `number` has an alternate setting other than 0 in the
 * device's configuration (walked to wTotalLength; a malformed tail counts
 * as one, so the caller does not assume a single setting). */
static ULONG hcdCfgHasAlternates(PHCD_USB_DEVICE dev, ULONG number)
{
    ULONG total;
    ULONG at;
    ULONG len;

    if (dev->Config == NULL || dev->ConfigLength < 9) {
        return 1;
    }
    total = (ULONG)dev->Config[2] | ((ULONG)dev->Config[3] << 8);
    if (total > dev->ConfigLength) {
        total = dev->ConfigLength;
    }
    at = dev->Config[0];
    while (at < total) {
        if (total - at < 2) {
            return 1;
        }
        len = dev->Config[at];
        if (len < 2 || len > total - at) {
            return 1;
        }
        if (dev->Config[at + 1] == 4 && len >= 9 &&
            (ULONG)dev->Config[at + 2] == number &&
            dev->Config[at + 3] != 0) {
            return 1;
        }
        at += len;
    }
    return 0;
}

/*
 * An interface's device-side toggles restarted after its endpoints were
 * re-added (fresh contexts start at DATA0): SET_INTERFACE, or - for an
 * alternate 0 a single-setting device STALLs it on (USB 2.0 9.4.10) -
 * CLEAR_FEATURE(ENDPOINT_HALT) on each of its new non-isochronous
 * endpoints, which restarts the device's toggle too (9.4.5). Only that
 * case falls back: a SET_INTERFACE that never reached the device, failed
 * otherwise, or was STALLed by an interface with other alternates has not
 * moved the device to alternate 0, and recording 0 then would be false
 * (Codex review of batch (c), round 19, finding 2). The STALL halts EP0 on
 * the controller; the engine reports it for the thread's own record as for
 * a URB's (hcd_dev.c, Ep0Halted), and the next thread control transfer
 * resets EP0 before its SETUP (hcd_enum.c, hcdEp0Quiet).
 */
static ULONG hcdCfgSetInterface(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                PHCD_PIPE *add, ULONG number, ULONG alt)
{
    ULONG bytes;
    ULONG stalled;
    ULONG dci;

    if (HcdThreadControlEx(hc, dev, 0x01, 11, (USHORT)alt, (USHORT)number, 0,
                           &bytes, &stalled)) {
        dev->Alternate[number] = (UCHAR)alt;
        return 1;
    }
    if (alt != 0 || !stalled || hcdCfgHasAlternates(dev, number)) {
        return 0;
    }
    for (dci = 2; dci < 32; dci++) {
        if (add[dci] != NULL && add[dci]->Interface == number &&
            add[dci]->TransferType != XHCI_PIPE_XFER_ISOCH &&
            !HcdThreadControl(hc, dev, 0x02, 1, 0,
                              (USHORT)add[dci]->EndpointAddress, 0, &bytes)) {
            return 0;
        }
    }
    dev->Alternate[number] = 0;
    return 1;
}

/* A function's endpoints, closed already (hcdCfgCloseFunction gave `old`
 * and `keep`), dropped in one Configure Endpoint beside its siblings', and
 * each of its interfaces left at a nonzero alternate sent back to 0, so an
 * audio function stops reserving isochronous bandwidth; a SET_INTERFACE
 * that fails leaves that interface as it was. 1 when the Configure Endpoint
 * succeeded; *owed gets the interfaces not yet released (all of them when
 * the Configure Endpoint failed). */
static ULONG hcdCfgUnconfigureFunction(PHCD_CONTROLLER hc,
                                       PHCD_USB_DEVICE dev,
                                       ULONG interfaceMask, ULONG keep,
                                       ULONG old, PULONG owed)
{
    PHCD_PIPE none[32];
    ULONG bytes;
    ULONG n;

    for (n = 0; n < 32; n++) {
        none[n] = NULL;
    }
    *owed = interfaceMask;
    if (!hcdCfgReplace(hc, dev, keep, old, 0, none)) {
        return 0;
    }
    *owed = 0;
    for (n = 0; n < 32; n++) {
        if ((interfaceMask & (1UL << n)) == 0 || dev->Alternate[n] == 0) {
            continue;
        }
        if (HcdThreadControl(hc, dev, 0x01, 11, 0, (USHORT)n, 0, &bytes)) {
            dev->Alternate[n] = 0;
            dev->IfaceUsed &= ~(1UL << n);
        } else {
            *owed |= 1UL << n;
        }
    }
    return 1;
}

/*
 * A function PDO removed (hcd_pdo.c): its interfaces are owed a release,
 * done by the thread at its next pass (hcdCfgReleases), or claimed with the
 * first slow IRP of the device dequeued after it (HcdCfgService), so a
 * select from the same PDO started again cannot be overtaken by it. The
 * REMOVE does not wait for it: the PDO's URBs are complete already, the
 * release reads only the device record and the mask, never the PDO, and a
 * device that leaves first is freed with its pipes anyway. IRQL:
 * <= DISPATCH_LEVEL; the caller holds a reference on `dev`.
 */
VOID HcdCfgReleaseFunction(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           ULONG interfaceMask)
{
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->FuncRelease |= interfaceMask;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdThreadWake(hc);
}

/* Passes a refused release is tried in before it is given up: a Configure
 * Endpoint or SET_INTERFACE the device keeps refusing with no reset would
 * otherwise be retried at every poll for as long as the device stays. */
#define HCD_CFG_RELEASE_TRIES 3

/* The interfaces of `mask` start a fresh retry history: what they owed is
 * settled, given up with the device, or taken over by a select of their
 * own function, so a later release's first failure is counted as a first
 * (Codex review of batch (c), round 21, finding 2). Thread only. */
static VOID hcdCfgReleaseSettled(PHCD_USB_DEVICE dev, ULONG mask)
{
    ULONG n;

    for (n = 0; n < 32; n++) {
        if ((mask & (1UL << n)) != 0) {
            dev->FuncReleaseTries[n] = 0;
        }
    }
}

/* One claimed release (`mask`, interfaces). Returns the interfaces still
 * owed, for the caller to record again so the next pass retries them: none
 * when it succeeded, when a failure has brought the controller to the
 * recovery that settles the whole device (a failed quiesce requests it;
 * a command that never completes enters it), and not an interface whose
 * tries are spent. Thread only, powered, the controller not halted. */
static ULONG hcdCfgReleaseOne(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              ULONG mask)
{
    ULONG keep;
    ULONG old;
    ULONG owed;
    ULONG spent;
    ULONG n;

    if (mask == 0) {
        return 0;
    }
    if (dev->Gone || !dev->Split || dev->Selected == NULL ||
        dev->ConfigValue == 0) {
        hcdCfgReleaseSettled(dev, mask);
        return 0;
    }
    XHCI_DBG_VALUE("hcd: removed function released, port/mask",
                   (dev->Port << 24) | (mask & 0x00FFFFFFUL));
    old = hcdCfgCloseFunction(hc, dev, mask, &keep);
    if (old == 0xFFFFFFFFUL) {
        hcdCfgReleaseSettled(dev, mask);
        return 0;
    }
    (VOID)hcdCfgUnconfigureFunction(hc, dev, mask, keep, old, &owed);
    if (hc->Hc.ControllerFailed || hc->ScratchTainted) {
        owed = 0;
    }
    hcdCfgReleaseSettled(dev, mask & ~owed);
    spent = 0;
    for (n = 0; n < 32; n++) {
        if ((owed & (1UL << n)) == 0) {
            continue;
        }
        dev->FuncReleaseTries[n]++;
        if (dev->FuncReleaseTries[n] >= HCD_CFG_RELEASE_TRIES) {
            dev->FuncReleaseTries[n] = 0;
            spent |= 1UL << n;
        }
    }
    if (spent != 0) {
        XHCI_DBG_VALUE("hcd: function release abandoned, port/mask",
                       (dev->Port << 24) | (spent & 0x00FFFFFFUL));
        hc->FuncReleasesAbandoned++;
    }
    return owed & ~spent;
}

/* `owed` recorded again for the next pass (hcdCfgReleaseOne). IRQL:
 * PASSIVE_LEVEL; takes the controller lock. */
static VOID hcdCfgReleaseAgain(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                               ULONG owed)
{
    KIRQL oldIrql;

    if (owed == 0) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->FuncRelease |= owed;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

/* Every release HcdCfgReleaseFunction has recorded, once a pass, so a
 * refused one is retried at the next. Thread only, powered, the controller
 * not halted. */
static VOID hcdCfgReleases(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;
    ULONG mask;
    ULONG slot;

    for (slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        if (hc->Hc.ControllerFailed || hc->ScratchTainted) {
            break;
        }
        dev = hc->SlotDevice[slot];
        if (dev == NULL) {
            continue;
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        mask = dev->FuncRelease;
        dev->FuncRelease = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        hcdCfgReleaseAgain(hc, dev, hcdCfgReleaseOne(hc, dev, mask));
    }
}

/*
 * SELECT_CONFIGURATION from a function PDO (design record 13 section 10.9).
 * The device is configured already (HcdCfgParentConfigure), so no
 * SET_CONFIGURATION reaches it. Every USBD_INTERFACE_INFORMATION is checked
 * before anything is touched: its length, an interface of this function
 * named once, an alternate the configuration carries, room for its pipes.
 * Then the function's pipes close, one Configure Endpoint drops them and
 * adds the new ones beside the siblings', and each named interface whose
 * alternate changes - or whose endpoints have carried traffic since the
 * device last restarted their toggles (IfaceUsed) - gets its SET_INTERFACE;
 * an interface still at the alternate 0 the bus's SET_CONFIGURATION left,
 * never opened since, has fresh toggles at both ends and gets none (HID
 * devices often STALL a SET_INTERFACE). A NULL descriptor unconfigures
 * this function only. A failure leaves the function with no pipes, as
 * hcdCfgSelectInterface's does. *held is the function's interfaces still
 * owed a release (HcdCfgService): a select that succeeds takes over the
 * ones it names, and the unconfigure's own leftovers replace it (Codex
 * review of batch (c), round 21, finding 1 and its note). Thread only.
 */
static LONG hcdCfgSelectFunction(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                 PHCD_DEVICE_PDO pdo, PURB urb,
                                 PULONG held)
{
    struct _URB_SELECT_CONFIGURATION *sc;
    PUSBD_INTERFACE_INFORMATION ii;
    PHCD_PIPE add[32];
    UCHAR alt[32];
    XHCI_PIPE_IFACE iface;
    XHCI_PIPE_EP ep;
    KIRQL oldIrql;
    PUCHAR p;
    PUCHAR end;
    ULONG fixed;
    ULONG named;
    ULONG opened;
    ULONG owed;
    ULONG keep;
    ULONG old;
    ULONG mask;
    ULONG done;
    ULONG asked;
    ULONG answer;
    ULONG dci;
    ULONG e;
    ULONG n;
    LONG usbd;

    asked = 0;
    sc = &urb->UrbSelectConfiguration;
    if (!dev->Split || dev->Selected == NULL || dev->ConfigValue == 0) {
        return HCD_USBD_INTERNAL_HC_ERROR;
    }
    for (dci = 0; dci < 32; dci++) {
        add[dci] = NULL;
        alt[dci] = 0;
    }

    named = 0;
    fixed = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes);
    end = (PUCHAR)urb + urb->UrbHeader.Length;
    for (p = (PUCHAR)&sc->Interface;
         sc->ConfigurationDescriptor != NULL && p + fixed <= end;
         p += ii->Length) {
        ii = (PUSBD_INTERFACE_INFORMATION)p;
        if (ii->Length < fixed || p + ii->Length > end ||
            ii->InterfaceNumber >= 32 ||
            (pdo->InterfaceMask & (1UL << ii->InterfaceNumber)) == 0 ||
            (named & (1UL << ii->InterfaceNumber)) != 0 ||
            XhciPipeFindInterface(dev->Selected, dev->SelectedLength,
                                  ii->InterfaceNumber, ii->AlternateSetting,
                                  &iface) != XHCI_PIPE_OK) {
            XHCI_DBG_VALUE("hcd: function select refused, length/number/alt",
                           ((ULONG)ii->Length << 16) |
                               ((ULONG)ii->InterfaceNumber << 8) |
                               ii->AlternateSetting);
            return hcdCfgCountEnd(hc, asked, HCD_USBD_INVALID_PARAMETER);
        }
        hcdCfgCountAsked(hc, &asked, iface.EndpointCount);
        if (ii->Length < fixed + iface.EndpointCount *
                                     sizeof(USBD_PIPE_INFORMATION)) {
            XHCI_DBG_VALUE("hcd: function select too small, length/number/alt",
                           ((ULONG)ii->Length << 16) |
                               ((ULONG)ii->InterfaceNumber << 8) |
                               ii->AlternateSetting);
            hcdCfgNeedLength(ii, &iface);
            return hcdCfgCountEnd(hc, asked, HCD_USBD_BUFFER_TOO_SMALL);
        }
        named |= 1UL << ii->InterfaceNumber;
        alt[ii->InterfaceNumber] = ii->AlternateSetting;
    }

    old = hcdCfgCloseFunction(hc, dev, pdo->InterfaceMask, &keep);
    if (old == 0xFFFFFFFFUL) {
        return hcdCfgCountEnd(hc, asked, HCD_USBD_INTERNAL_HC_ERROR);
    }

    if (sc->ConfigurationDescriptor == NULL) {
        /* This function's unconfigure; the siblings keep theirs. What it
         * could not finish is retried as a release would be. */
        done = hcdCfgUnconfigureFunction(hc, dev, pdo->InterfaceMask, keep,
                                         old, &owed);
        hcdCfgReleaseSettled(dev, pdo->InterfaceMask & ~owed);
        *held = owed;
        if (!done) {
            return HCD_USBD_INTERNAL_HC_ERROR;
        }
        sc->ConfigurationHandle = NULL;
        return XHCI_USBD_STATUS_SUCCESS;
    }

    mask = 0;
    opened = 0;
    usbd = XHCI_USBD_STATUS_SUCCESS;
    for (p = (PUCHAR)&sc->Interface;
         usbd == XHCI_USBD_STATUS_SUCCESS && p + fixed <= end;
         p += ii->Length) {
        ii = (PUSBD_INTERFACE_INFORMATION)p;
        (VOID)XhciPipeFindInterface(dev->Selected, dev->SelectedLength,
                                    ii->InterfaceNumber, ii->AlternateSetting,
                                    &iface);
        if (iface.EndpointCount != 0) {
            opened |= 1UL << ii->InterfaceNumber;
        }
        for (e = 0; e < iface.EndpointCount; e++) {
            answer = XhciPipeEndpointParams(dev->Selected +
                                                iface.EndpointOffset[e],
                                            HcdDevicePipeSpeed(hc, dev), &ep);
            if (answer != XHCI_PIPE_OK || add[ep.Dci] != NULL ||
                (keep & (1UL << ep.Dci)) != 0) {
                XHCI_DBG_VALUE("hcd: function select refused, endpoint/speed",
                               (e << 8) | dev->Speed);
                hcdCfgCountEndpoint(hc, answer);
                usbd = HCD_USBD_INVALID_PARAMETER;
                break;
            }
            add[ep.Dci] = hcdCfgPipeNew(hc, dev, &ep, &usbd);
            if (add[ep.Dci] == NULL) {
                hc->Counters.EndpointRefusalsPool++;
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
        ii->Class = (UCHAR)iface.InterfaceClass;
        ii->SubClass = (UCHAR)iface.InterfaceSubClass;
        ii->Protocol = (UCHAR)iface.InterfaceProtocol;
        ii->InterfaceHandle = HCD_IFACE_COOKIE(iface.InterfaceNumber);
        ii->NumberOfPipes = iface.EndpointCount;
    }
    if (usbd != XHCI_USBD_STATUS_SUCCESS) {
        /* The closed ones are still enabled on the controller, with no
         * pipe: the next Configure Endpoint drops them. */
        dev->Stale |= old;
    } else if (!hcdCfgReplace(hc, dev, keep, old, mask, add)) {
        usbd = HCD_USBD_INTERNAL_HC_ERROR;
    }
    for (n = 0; n < 32 && usbd == XHCI_USBD_STATUS_SUCCESS; n++) {
        if ((named & (1UL << n)) == 0 ||
            (alt[n] == dev->Alternate[n] &&
             (dev->IfaceUsed & (1UL << n)) == 0)) {
            continue;
        }
        if (!hcdCfgSetInterface(hc, dev, add, n, alt[n])) {
            XHCI_DBG_VALUE("hcd: function select, SET_INTERFACE failed",
                           (n << 8) | alt[n]);
            usbd = HCD_USBD_INTERNAL_HC_ERROR;
            /* Enabled on the controller, not on the device. */
            dev->Stale |= mask;
        } else {
            dev->IfaceUsed &= ~(1UL << n);
        }
    }
    if (usbd != XHCI_USBD_STATUS_SUCCESS) {
        XHCI_DBG_VALUE("hcd: function select failed, USBD status",
                       (ULONG)usbd);
        for (dci = 2; dci < 32; dci++) {
            if (add[dci] != NULL) {
                hcdCfgPipeFree(hc, dev, add[dci]);
            }
        }
        return hcdCfgCountEnd(hc, asked, usbd);
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (dci = 2; dci < 32; dci++) {
        if (add[dci] != NULL) {
            dev->Pipes[dci] = add[dci];
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    dev->IfaceUsed |= opened;
    *held &= ~named;
    hcdCfgReleaseSettled(dev, named);
    sc->ConfigurationHandle = hcdCfgHandle(dev, pdo);
    XHCI_DBG_VALUE("hcd: function configured, first interface/DCI mask",
                   (pdo->Func.FirstInterface << 24) | (mask >> 8));
    return hcdCfgCountEnd(hc, asked, XHCI_USBD_STATUS_SUCCESS);
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
        /* Behind a TT, a control or bulk endpoint's TT buffer may still
         * hold the halted transaction (xHCI 4.6.8 p.116; 27-A.3). */
        HcdHubClearTt(hc, dev, pipe->EndpointAddress, pipe->TransferType, 0);
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
 * 5). The command is sent only when the slot is Configured, which DC = 1
 * requires (4.6.6), and one of the endpoints it is for is enabled: after a
 * Reset Device the slot is Default and every endpoint but EP0 Disabled
 * already (4.6.11), and a Stale bit can name an endpoint a refused command
 * never enabled. Returns 0 when the controller refused: the endpoints may
 * still run, so nothing is completed until the reset that refusal requests
 * invalidates the device.
 */
static ULONG hcdCfgDeconfigure(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    XHCI_TRB trb;
    ULONG control;
    ULONG enabled;
    ULONG offset;
    ULONG state;
    ULONG dci;

    enabled = 0;
    for (dci = 2; dci < 32; dci++) {
        if (dev->Pipes[dci] == NULL) {
            if ((dev->Stale & (1UL << dci)) != 0 &&
                hcdCfgEpState(hc, dev, dci) != XHCI_EP_STATE_DISABLED) {
                enabled = 1;
            }
            continue;
        }
        HcdIoPipePause(hc, dev->Pipes[dci]);
        (VOID)HcdIoPipeCancelAll(hc, dev->Pipes[dci]);
        state = hcdCfgQuiesce(hc, dev, dev->Pipes[dci]);
        if (state == XHCI_EP_STATE_RUNNING) {
            return 0;
        }
        if (state != XHCI_EP_STATE_DISABLED) {
            enabled = 1;
        }
    }
    if (enabled &&
        (XhciSlotContextOffset(&hc->Hc.Layout, dev->SlotId, &offset) !=
             XHCI_LAYOUT_OK ||
         XHCI_SLOT_GET_STATE(XhciCommonAt(&hc->Hc, offset)[3]) !=
             XHCI_SLOT_STATE_CONFIGURED)) {
        enabled = 0;
    }
    if (!enabled) {
        dev->Stale = 0;
        hcdCfgCloseAll(hc, dev, HCD_USBD_CANCELED);
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
 * completed. A Disabled endpoint - a Reset Device or a Configure Endpoint
 * dropped it, the pipe not yet closed - gets no Set TR Dequeue, which is a
 * Context State Error there (xHCI 4.6.10), and no reset for its refusal:
 * only the software ring is emptied, for the context that adds it again.
 */
static LONG hcdCfgAbortPaused(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              PHCD_PIPE pipe, LONG usbd)
{
    KIRQL oldIrql;
    ULONG state;

    (VOID)HcdIoPipeCancelAll(hc, pipe);
    state = hcdCfgQuiesce(hc, dev, pipe);
    if (state == XHCI_EP_STATE_RUNNING) {
        return HCD_USBD_INTERNAL_HC_ERROR;
    }
    HcdIoDrainPipe(hc, pipe, usbd);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    (VOID)XhciRingSetDequeue(pipe->Ring,
                             XhciRingTrbPA(pipe->Ring, pipe->Ring->Enqueue));
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (state != XHCI_EP_STATE_DISABLED && !hcdCfgSetDequeue(hc, dev, pipe)) {
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
    busy = pipe->Queue->Count != 0 || !IsListEmpty(&pipe->Held) ||
           !IsListEmpty(&pipe->Waiting);
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
            if (ok) {
                HcdHubClearTt(hc, dev, pipe->EndpointAddress,
                              pipe->TransferType, 0);
            }
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
 * miniport's placement (xhciEpPlaceDequeue, branch 1.2.0.0). A Disabled
 * endpoint gets neither the Set TR Dequeue (a Context State Error there,
 * xHCI 4.6.10) nor a doorbell: its software ring alone moves, for the
 * context that adds it again.
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
        if (t != &dev->Ep0Xfer && t != &dev->HubXfer &&
            ((PHCD_XFER)t)->CancelRequested) {
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
        hc->Counters.TransfersCancelled++;
        HcdIoRetired(hc, t);
    }
    if (move) {
        (VOID)XhciRingSetDequeue(
            pipe->Ring,
            XhciRingTrbPA(pipe->Ring, survivor != NULL ? survivor->FirstIndex
                                                       : pipe->Ring->Enqueue));
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);

    if (move && state != XHCI_EP_STATE_DISABLED &&
        !hcdCfgSetDequeue(hc, dev, pipe)) {
        HcdSvcRequestReset(&hc->Hc);
        HcdIoDeferred(hc);
        return;                 /* left paused, as hcdCfgAbort */
    }
    if (pipe->Queue->Count != 0 && state != XHCI_EP_STATE_DISABLED) {
        XhciWriteDoorbell(&hc->Hc, dev->SlotId, pipe->Dci);
    }
    HcdIoDeferred(hc);
    HcdIoPipeResume(hc, pipe);
}

/* ----------------------------------------------------------------------- */
/* RESET_PORT                                                               */
/* ----------------------------------------------------------------------- */

/*
 * RESET_PORT's first step on one pipe, EP0's included, which the caller has
 * paused: every request on it cancelled wherever it is, the endpoint stopped
 * if it runs, what was on its ring completed as CANCELED, and the ring left
 * empty at its enqueue position for the context that re-adds it. Unlike
 * hcdCfgAbortPaused, a Halted or Error endpoint is left as it is - no Reset
 * Endpoint, Set TR Dequeue or CLEAR_FEATURE(ENDPOINT_HALT) - because the
 * Reset Device that follows disables it (xHCI 4.6.11) and the device's own
 * reset clears its halt. Returns 0 when Stop Endpoint failed; the controller
 * reset is then requested.
 */
static ULONG hcdCfgResetQuiet(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              PHCD_PIPE pipe)
{
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG control;
    ULONG code;

    (VOID)HcdIoPipeCancelAll(hc, pipe);
    if (hcdCfgEpState(hc, dev, pipe->Dci) == XHCI_EP_STATE_RUNNING) {
        code = 0;
        if (XhciTrbStopEndpoint(&trb, dev->SlotId, pipe->Dci, 0) ==
            XHCI_RING_OK) {
            code = HcdThreadCommand(hc, &trb, &control);
        }
        if (code != XHCI_CC_SUCCESS && code != XHCI_CC_CONTEXT_STATE_ERROR) {
            HcdSvcRequestReset(&hc->Hc);
            return 0;
        }
    }
    HcdIoDrainPipe(hc, pipe, HCD_USBD_CANCELED);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    (VOID)XhciRingSetDequeue(pipe->Ring,
                             XhciRingTrbPA(pipe->Ring, pipe->Ring->Enqueue));
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdIoPipeWaitCancelled(hc, pipe);
    pipe->Halted = 0;
    pipe->DrainPending = 0;
    return 1;
}

/*
 * IOCTL_INTERNAL_USB_RESET_PORT (design record 13 section 10.9): the device
 * reset on the wire and restored as it was - same slot, the same
 * configuration and alternates, every pipe handle still valid; what was in
 * flight completes as cancelled. usbstor keeps its pipe handles across it.
 *
 *   every pipe paused and brought to rest (hcdCfgResetQuiet), EP0's too;
 *   the port reset, Reset Device, Address Device (HcdThreadReaddress);
 *   the device descriptor read back and compared byte for byte, so a
 *     different device behind the reset is not configured as the old one;
 *   every open endpoint added back in one Configure Endpoint, each on its
 *     emptied ring - a fresh context, so the controller's data toggle starts
 *     at DATA0, as the device's does after its reset;
 *   SET_CONFIGURATION, then SET_INTERFACE for each interface the last select
 *     left at a nonzero alternate;
 *   the pipes resumed: what was submitted meanwhile is published now.
 *
 * Any failure becomes a cycle (HcdEnumCycle): the device is dropped and
 * enumerated afresh, and its pipes stay paused, so nothing is published to
 * an endpoint the reset disabled - the device's free completes whatever
 * waits. Thread only, powered.
 */
static LONG hcdCfgResetPort(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PXHCI_EXTENSION ext;
    PHCD_PIPE add[32];
    XHCI_PIPE_PLAN plan;
    XHCI_TRB trb;
    PUCHAR s;
    ULONG control;
    ULONG bytes;
    ULONG mask;
    ULONG dci;
    ULONG ok;

    ext = &hc->Hc;
    if (dev->Location == 0 || dev->Location > HCD_PORT_COUNT ||
        hc->Ports[dev->Location - 1].Device != dev) {
        return HCD_USBD_DEVICE_GONE;
    }
    XHCI_DBG_VALUE("hcd: reset port, port/slot",
                   (dev->Port << 16) | dev->SlotId);

    ok = 0;
    mask = 0;
    HcdIoPipePause(hc, &dev->Ep0Pipe);
    for (dci = 0; dci < 32; dci++) {
        add[dci] = NULL;
        if (dci >= 2 && dev->Pipes[dci] != NULL) {
            add[dci] = dev->Pipes[dci];
            mask |= 1UL << dci;
            HcdIoPipePause(hc, add[dci]);
        }
    }
    for (dci = 2; dci < 32; dci++) {
        if (add[dci] != NULL && !hcdCfgResetQuiet(hc, dev, add[dci])) {
            goto cleanup;
        }
    }
    if (!hcdCfgResetQuiet(hc, dev, &dev->Ep0Pipe) ||
        !HcdThreadReaddress(hc, dev)) {
        goto cleanup;
    }
    /* Reset Device disabled every endpoint but EP0, the stale ones too. */
    dev->Stale = 0;

    s = (PUCHAR)hc->ScratchVa;
    if (!HcdThreadControl(hc, dev, 0x80, 6, 0x0100, 0, 18, &bytes) ||
        bytes != 18) {
        goto cleanup;
    }
    for (dci = 0; dci < 18; dci++) {
        if (s[dci] != dev->DeviceDesc[dci]) {
            XHCI_DBG_TEXT("hcd: reset port, another device answered");
            goto cleanup;
        }
    }

    if (dev->ConfigValue == 0) {
        if (mask != 0) {
            goto cleanup;
        }
    } else {
        if (mask != 0 &&
            (XhciPipeConfigurePlan(0, 0, mask, &plan) != XHCI_PIPE_OK ||
             !hcdCfgBuildInput(hc, dev, &plan, add) ||
             XhciTrbConfigureEndpoint(&trb, dev->SlotId,
                                      XhciCommonPA(ext,
                                          ext->Layout.InputContextOffset),
                                      0) != XHCI_RING_OK ||
             HcdThreadCommand(hc, &trb, &control) != XHCI_CC_SUCCESS)) {
            goto cleanup;
        }
        if (!HcdThreadControl(hc, dev, 0x00, 9, (USHORT)dev->ConfigValue, 0,
                              0, &bytes)) {
            goto cleanup;
        }
        for (dci = 0; dci < 32; dci++) {
            if (dev->Alternate[dci] != 0 &&
                !HcdThreadControl(hc, dev, 0x01, 11,
                                  (USHORT)dev->Alternate[dci], (USHORT)dci,
                                  0, &bytes)) {
                goto cleanup;
            }
        }
    }
    ok = 1;

cleanup:
    if (!ok) {
        XHCI_DBG_VALUE("hcd: reset port failed, cycling port", dev->Port);
        if (dev->Pdo != NULL) {
            /* The device's group: every function PDO of it goes. */
            HcdEnumCycle(hc, dev->Location, dev->PdoGroup);
        }
        return HCD_USBD_INTERNAL_HC_ERROR;
    }
    for (dci = 2; dci < 32; dci++) {
        if (add[dci] != NULL) {
            HcdIoPipeResume(hc, add[dci]);
        }
    }
    HcdIoPipeResume(hc, &dev->Ep0Pipe);
    XHCI_DBG_VALUE("hcd: reset port done, endpoint mask", mask);
    return XHCI_USBD_STATUS_SUCCESS;
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

/* Every pended IRP, in order. Thread only, powered, the controller not
 * halted (HcdEnumService). */
VOID HcdCfgService(PHCD_CONTROLLER hc)
{
    PLIST_ENTRY entry;
    PHCD_USB_DEVICE dev;
    PHCD_DEVICE_PDO pdo;
    PHCD_PIPE pipe;
    PIRP irp;
    PURB urb;
    KIRQL oldIrql;
    ULONG release;
    ULONG owed;
    ULONG held;
    ULONG number;
    LONG usbd;

    hcdCfgReleases(hc);
    for (;;) {
        if (hc->Hc.ControllerFailed || hc->ScratchTainted) {
            /* A command or EP0 transfer just failed into the reset: the
             * rest wait for the recovery, whose invalidation completes
             * them with their devices (round 2, finding 10). */
            break;
        }
        /* The IRP's device's pending releases are claimed in the lock hold
         * that dequeues it and run before it is served. A REMOVE records
         * its release under this lock before it completes, so before its
         * PDO, started again, can queue anything; and every release the
         * thread claimed earlier ran to its end before this dequeue. So no
         * slow IRP is served while a release recorded before it waits, and
         * none can close the pipes a later select opened (Codex review of
         * batch (c), round 20, finding 1). */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (IsListEmpty(&hc->SlowIrps)) {
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            break;
        }
        entry = RemoveHeadList(&hc->SlowIrps);
        irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
        dev = (PHCD_USB_DEVICE)irp->Tail.Overlay.DriverContext[0];
        release = dev->FuncRelease;
        dev->FuncRelease = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);

        pdo = (PHCD_DEVICE_PDO)irp->Tail.Overlay.DriverContext[2];
        urb = hcdCfgUrbOf(irp);
        if (dev->Gone) {
            hcdCfgComplete(dev, irp, urb, HCD_USBD_DEVICE_GONE);
            continue;
        }
        owed = hcdCfgReleaseOne(hc, dev, release);
        held = 0;
        if (pdo->Function) {
            /* A retry of this PDO's own interfaces is recorded only after
             * its request: a select of the PDO, started again, takes over
             * the interfaces it reconfigures, and a retry run after it would
             * undo that. The rest stay owed - an ABORT_PIPE, a refused or
             * short select, an interface left unnamed (Codex review of batch
             * (c), round 21, finding 1). */
            held = owed & pdo->InterfaceMask;
            owed &= ~held;
        }
        hcdCfgReleaseAgain(hc, dev, owed);
        if (hc->Hc.ControllerFailed || hc->ScratchTainted) {
            /* Put back for the recovery's invalidation to complete. */
            hcdCfgReleaseAgain(hc, dev, held);
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            InsertHeadList(&hc->SlowIrps, entry);
            XhciControllerLockRelease(&hc->Hc, oldIrql);
            break;
        }
        if (urb == NULL) {
            usbd = hcdCfgResetPort(hc, dev);
            hcdCfgReleaseAgain(hc, dev, held);
            hcdCfgComplete(dev, irp, NULL, usbd);
            continue;
        }
        switch (urb->UrbHeader.Function) {
        case HCD_URB_SELECT_CONFIGURATION:
            usbd = pdo->Function
                       ? hcdCfgSelectFunction(hc, dev, pdo, urb, &held)
                       : hcdCfgSelect(hc, dev, urb);
            hcdCfgCountSelect(hc, dev, usbd);
            break;
        case HCD_URB_SELECT_INTERFACE:
            usbd = hcdCfgSelectInterface(hc, dev, pdo, urb);
            number = urb->UrbSelectInterface.Interface.InterfaceNumber;
            if (usbd == XHCI_USBD_STATUS_SUCCESS && number < 32) {
                held &= ~(1UL << number);
                hcdCfgReleaseSettled(dev, 1UL << number);
            }
            hcdCfgCountSelect(hc, dev, usbd);
            break;
        case HCD_URB_ABORT_PIPE:
        case HCD_URB_RESET_PIPE:
            XhciControllerLockAcquire(&hc->Hc, &oldIrql);
            pipe = HcdCfgPipe(dev, urb->UrbPipeRequest.PipeHandle);
            if (pipe != NULL && !HcdPdoOwnsPipe(pdo, dev, pipe)) {
                /* A sibling function's pipe (design record 13 section
                 * 10.9). */
                pipe = NULL;
            }
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
        hcdCfgReleaseAgain(hc, dev, held);
        hcdCfgComplete(dev, irp, urb, usbd);
    }
}
