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
 * carries no URB, which hcdCfgUrbOf tells apart. So do the private streams
 * requests of xhci98_streams.h (31-A.1, hcdCfgStreamsRequest), told apart
 * by their control codes: an endpoint's streams are opened and closed
 * here, and every operation above on an endpoint with streams open reaches
 * each stream's pipe as well (hcd_io.c).
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
#include "xhci_stream.h"
#include "xhci98_streams.h"
#include "xhci_dbg.h"

/* URB functions, Windows 2000 DDK inc\usbdi.h. */
#define HCD_URB_SELECT_CONFIGURATION    0x0000
#define HCD_URB_SELECT_INTERFACE        0x0001
#define HCD_URB_ABORT_PIPE              0x0002
#define HCD_URB_RESET_PIPE              0x001E
/* Windows XP's halves of it (WDK 7.1 inc\api\usb.h; task 28-A.1). */
#define HCD_URB_SYNC_RESET_PIPE         0x0030
#define HCD_URB_SYNC_CLEAR_STALL        0x0031

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
    if (answer == XHCI_PIPE_ESIT_REFUSED) {
        hc->Counters.SsEndpointsEsitRefused++;
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
 * (Codex review round 25). Behind hubs the failure is not counted when a
 * hub port on its path confirms the departure - a GET_STATUS reply that
 * reads it disconnected, disabled or changed, or a hub already departing
 * - or its root port does as above; a hub that does not answer confirms
 * nothing (HcdHubPathPresent; Codex review of d54eef0, finding 2).
 * A select racing an ordinary unplug fails at
 * SET_CONFIGURATION or SET_INTERFACE before HcdEnumService sees the port
 * change, and counting it would fail a correct matrix run. A device still
 * on its port that refuses - SET_CONFIGURATION(0) among them - is counted
 * (Codex review round 24, finding 1). IRQL: PASSIVE_LEVEL (the thread,
 * which owns the port records). */
static ULONG hcdCfgDeviceLeft(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    if (dev->Gone || dev->Location == 0 || dev->Location > HCD_PORT_COUNT ||
        hc->Ports[dev->Location - 1].Device != dev) {
        return 1;
    }
    /* Behind hubs, every port on the path is asked as well (Codex review
     * of 23e7715, finding 6). */
    return !HcdHubPathPresent(hc, &hc->Ports[dev->Location - 1]);
}

/* A select's answer: counted (above), and a failure of a device that has
 * left answered DEVICE_GONE, whatever step failed. Thread only. */
static LONG hcdCfgSelectAnswer(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                               LONG usbd)
{
    if (usbd == XHCI_USBD_STATUS_SUCCESS ||
        usbd == HCD_USBD_BUFFER_TOO_SMALL) {
        return usbd;
    }
    if (hcdCfgDeviceLeft(hc, dev)) {
        return HCD_USBD_DEVICE_GONE;
    }
    hc->Counters.SelectsFailed++;
    return usbd;
}

/*
 * A command on the device's slot failed. A controller reset is requested
 * only while the device is still proven present (hcdCfgDeviceLeft, as the
 * select count above reads it): a command that fails because the device
 * has just left - a Configure Endpoint, Stop Endpoint or Set TR Dequeue
 * racing an unplug, the slot's Context State or a Transaction Error - is an
 * ordinary failure of that device, which its departure tears down (the
 * thread's next port pass disables the slot and drains every request).
 * That teardown is made certain, not assumed: a record already gone or
 * replaced is being torn down; otherwise the device's port is cycled
 * (HcdEnumCycle, an unplug as the port's machine sees one), since a hub
 * port's latched enable change can read as a departure that the hub
 * service then finds enabled and leaves alone, which would strand the
 * paused pipe (Codex review of e99f8ea). A device with no PDO yet has no
 * group to cycle by and keeps the reset.
 * A reset for it dropped every device on the controller, and the devices
 * re-enumerated on the other ports got no PnP from Windows ME afterwards
 * (2026-10-04, r3 t2: a mouse pulled during its SELECT_CONFIGURATION).
 * A command that timed out is the engine's to recover, whatever the
 * device (xhci_cmd.c). Returns 1 when a reset was requested. Thread only,
 * powered.
 */
static ULONG hcdCfgFault(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    if (dev->Gone || dev->Location == 0 || dev->Location > HCD_PORT_COUNT ||
        hc->Ports[dev->Location - 1].Device != dev) {
        XHCI_DBG_VALUE("hcd: command failed on a departed device, slot",
                       dev->SlotId);
        return 0;
    }
    /* A hub's own PDO (task 33.4) is presentation only: a hub's failed
     * command keeps the reset it always requested. */
    if (dev->Pdo != NULL && dev->Hub == NULL &&
        !HcdHubPathPresent(hc, &hc->Ports[dev->Location - 1])) {
        XHCI_DBG_VALUE("hcd: command failed on a departing device, "
                       "cycling location", dev->Location);
        HcdEnumCycle(hc, dev->Location, dev->PdoGroup);
        return 0;
    }
    HcdSvcRequestReset(&hc->Hc);
    return 1;
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

/* A Configure Endpoint built but not issued (hcdCfgRecycleCode): distinct
 * from 0, which is a command issued whose outcome is unknown. */
#define HCD_CFG_NOT_ISSUED  0xFFFFFFFFUL

static VOID hcdCfgStreamsFree(PHCD_CONTROLLER hc, PHCD_STREAMS st);

/* The retired stream blocks (hcdCfgPipeFree) whose endpoint is in
 * `dropped` (DCI bits): a command that has completed successfully dropped
 * or disabled that endpoint, so no context of the controller's names the
 * array any more, and the block is freed. Thread only. */
static VOID hcdCfgRetiredReclaim(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                 ULONG dropped)
{
    PHCD_STREAMS *link;
    PHCD_STREAMS st;

    link = &dev->StreamsRetired;
    while ((st = *link) != NULL) {
        if (st->Dci < 32UL && (dropped & (1UL << st->Dci)) != 0) {
            *link = st->Next;
            hcdCfgStreamsFree(hc, st);
        } else {
            link = &st->Next;
        }
    }
}

/* Every command this file issues: the thread's, and a Configure Endpoint
 * that completed successfully reclaims the retired stream blocks of the
 * endpoints it dropped - all of them with DC = 1, else those of the Input
 * Control Context's Drop flags, which the shared Input Context still holds
 * (31-A.1). Thread only, powered. */
static ULONG hcdCfgCommand(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           const XHCI_TRB *trb, PULONG control)
{
    ULONG code;
    ULONG icc;
    ULONG dropped;

    code = HcdThreadCommand(hc, trb, control);
    if (code == XHCI_CC_SUCCESS && dev->StreamsRetired != NULL &&
        XHCI_TRB_GET_TYPE(trb->Control) == XHCI_TRB_TYPE_CONFIGURE_EP) {
        dropped = 0;
        if ((trb->Control & XHCI_TRB_DC) != 0) {
            dropped = XHCI_PIPE_ENDPOINT_MASK;
        } else if (XhciInputControlContextOffset(&hc->Hc.Layout, &icc) ==
                   XHCI_LAYOUT_OK) {
            dropped = XhciCommonAt(&hc->Hc, icc)[0];
        }
        hcdCfgRetiredReclaim(hc, dev, dropped);
    }
    return code;
}

/*
 * An endpoint's streams written for the Configure Endpoint that adds it
 * (31-A.1): every Stream Context from its stream's ring as it stands - the
 * dequeue pointer and cycle state, SCT Primary TR - the reserved Stream ID
 * 0 and every ID past the grant all zero, and the Endpoint Context `epCtx`,
 * already built, given MaxPStreams, LSA and the array's address. The array
 * is the controller's only while the endpoint is enabled, and every caller
 * adds the endpoint from Disabled or with a Drop in the same command, so
 * rewriting it here is safe. Returns 0 when a builder refuses a value.
 */
static ULONG hcdCfgStreamContexts(PHCD_STREAMS st, volatile ULONG *epCtx)
{
    volatile ULONG *array;
    PXHCI_RING ring;
    ULONG id;

    array = (volatile ULONG *)st->Va;
    for (id = 0; id < st->Entries; id++) {
        ring = (id >= 1 && id <= st->Count) ? st->Ring[id] : NULL;
        if (XhciStreamContextBuild(
                array + id * (XHCI_STREAM_CONTEXT_BYTES / 4UL),
                ring != NULL ? XhciRingDequeuePA(ring) : 0UL,
                ring != NULL ? XhciRingDequeueCycle(ring) : 0UL,
                ring != NULL ? XHCI_STREAM_SCT_PRIMARY_TR : 0UL) !=
            XHCI_STREAM_OK) {
            return 0;
        }
    }
    return XhciStreamEndpointContext(epCtx, st->MaxPStreams,
                                     st->Pa.LowPart) == XHCI_STREAM_OK;
}

/*
 * The Input Context for one Configure Endpoint: the control flags, the Slot
 * Context rewritten whole (HcdDeviceSlotParams: the device's route, root
 * port, speed and TT fields, a hub's marking, Context Entries the highest
 * DCI enabled after it), and an Endpoint Context
 * for each DCI being added, its dequeue pointer and cycle state read from
 * the ring now - or, for an endpoint with streams open, its Stream Context
 * Array (hcdCfgStreamContexts). Returns 0 when a builder refuses a value.
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
        if (pipe->Streams != NULL) {
            /* Overwritten by the stream fields below; the builder checks
             * the pointer it is given. */
            ep.DequeuePA = pipe->Streams->Pa.LowPart;
            ep.Dcs = 0;
        } else {
            ep.DequeuePA = XhciRingDequeuePA(pipe->Ring);
            ep.Dcs = XhciRingDequeueCycle(pipe->Ring);
        }
        if (XhciBuildEndpointContext(XhciCommonAt(ext, iep), &ep) != 0) {
            return 0;
        }
        if (pipe->Streams != NULL &&
            !hcdCfgStreamContexts(pipe->Streams, XhciCommonAt(ext, iep))) {
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

/* An endpoint's streams freed (31-A.1): every stream's pipe, and the block
 * holding the array and the rings. Every record is free and the controller
 * holds no pointer into the block - the streams were never installed, or the
 * endpoint has been reconfigured without them, dropped, or its slot is gone.
 * Detached from the endpoint already. */
static VOID hcdCfgStreamsFree(PHCD_CONTROLLER hc, PHCD_STREAMS st)
{
    ULONG id;

    if (st == NULL) {
        return;
    }
    for (id = 1; id < XHCI_STREAM_MAX_ENTRIES; id++) {
        HcdPoolFree(st->Pipe[id]);
    }
    HcdDmaStreamFree(hc, st->Layout.TotalBytes, st->Pa, st->Va);
    HcdPoolFree(st);
}

static ULONG hcdCfgEpState(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           ULONG dci);

/* Give a pipe's ring back and free it, and its streams with it; every
 * record is free and the controller owns none of its TRBs. A select of one
 * interface closes its pipes before the Configure Endpoint that drops their
 * endpoints, which are Stopped meanwhile with their contexts still naming
 * the stream array: a block whose endpoint is not yet Disabled is retired
 * to the device instead, and freed once a successful Configure Endpoint
 * that drops the endpoint, a Reset Device, or the slot's going proves the
 * controller done with it (hcdCfgRetiredReclaim, HcdCfgDeviceGone). A pool
 * ring returned
 * there stays in the controller's own block, which is why rings need no
 * such care. */
static VOID hcdCfgPipeFree(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           PHCD_PIPE pipe)
{
    PHCD_STREAMS st;
    KIRQL oldIrql;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    st = pipe->Streams;
    pipe->Streams = NULL;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (st != NULL && !dev->Gone &&
        hcdCfgEpState(hc, dev, pipe->Dci) != XHCI_EP_STATE_DISABLED) {
        st->Dci = pipe->Dci;
        st->Next = dev->StreamsRetired;
        dev->StreamsRetired = st;
        st = NULL;
    }
    hcdCfgStreamsFree(hc, st);

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
    ULONG id;

    for (dci = 2; dci < 32; dci++) {
        if ((mask & (1UL << dci)) == 0) {
            continue;
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        pipe = dev->Pipes[dci];
        if (pipe != NULL) {
            pipe->Closed = 1;
            dev->Pipes[dci] = NULL;
            if (pipe->Streams != NULL) {
                /* Its streams' handles stop resolving with it, and a
                 * request already past the lookup finds its stream closed
                 * (hcd_io.c, hcdGo and HcdIoMapped). */
                pipe->Streams->Live = 0;
                for (id = 1; id <= pipe->Streams->Count; id++) {
                    pipe->Streams->Pipe[id]->Closed = 1;
                }
            }
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
    PHCD_STREAMS st;

    hcdCfgCloseAll(hc, dev, HCD_USBD_DEVICE_GONE);
    /* The slot is disabled or HCRST has taken it: no context names a
     * retired array any more (hcdCfgPipeFree). */
    while (dev->StreamsRetired != NULL) {
        st = dev->StreamsRetired;
        dev->StreamsRetired = st->Next;
        hcdCfgStreamsFree(hc, st);
    }
}

/* An endpoint's own handle, if the device still has it open: never a
 * stream's. Controller lock held. */
static PHCD_PIPE hcdCfgEndpointPipe(PHCD_USB_DEVICE dev, PVOID handle)
{
    ULONG dci;

    for (dci = 2; dci < 32; dci++) {
        if (dev->Pipes[dci] != NULL && (PVOID)dev->Pipes[dci] == handle) {
            return dev->Pipes[dci];
        }
    }
    return NULL;
}

/* A handle a client holds, if the device still has it open: an endpoint's,
 * or a stream's while its streams are live (31-A.1). IRQL:
 * <= DISPATCH_LEVEL, controller lock held by the caller. */
PHCD_PIPE HcdCfgPipe(PHCD_USB_DEVICE dev, PVOID handle)
{
    PHCD_PIPE pipe;
    ULONG dci;
    ULONG id;

    pipe = hcdCfgEndpointPipe(dev, handle);
    if (pipe != NULL) {
        return pipe;
    }
    for (dci = 2; dci < 32; dci++) {
        pipe = dev->Pipes[dci];
        if (pipe == NULL || pipe->Streams == NULL || !pipe->Streams->Live) {
            continue;
        }
        for (id = 1; id <= pipe->Streams->Count; id++) {
            if ((PVOID)pipe->Streams->Pipe[id] == handle) {
                return pipe->Streams->Pipe[id];
            }
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
        code = hcdCfgCommand(hc, dev, &trb, &control);
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
            answer = XhciPipeEndpointParamsAt((const UCHAR *)cd, total,
                                              iface.EndpointOffset[e],
                                              HcdDevicePipeSpeed(hc, dev),
                                              hc->Hc.HcInfo.Lec, &ep);
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
            code = hcdCfgCommand(hc, dev, &trb, &control);
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
    XHCI_DBG_VALUE("hcd: configured, DCI mask", mask);
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
    ULONG stalled;
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
        answer = XhciPipeEndpointParamsAt(dev->Selected, dev->SelectedLength,
                                          iface.EndpointOffset[e],
                                          HcdDevicePipeSpeed(hc, dev),
                                          hc->Hc.HcInfo.Lec, &ep);
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
            code = hcdCfgCommand(hc, dev, &trb, &control);
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
        !HcdThreadControlEx(hc, dev, 0x01, 11,
                            (USHORT)iface.AlternateSetting,
                            (USHORT)iface.InterfaceNumber, 0, &bytes,
                            &stalled)) {
        /* Enabled on the controller, not on the device: dropped by the
         * next command. A device that STALLed SET_INTERFACE refused it
         * itself and is answered with its own status, not one that
         * blames the controller (26-V.2: a C-Media whose alternate 1 the
         * passthrough refused). */
        XHCI_DBG_VALUE("hcd: SET_INTERFACE failed, stalled", stalled);
        usbd = stalled ? XHCI_USBD_STATUS_STALL_PID :
                         HCD_USBD_INTERNAL_HC_ERROR;
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
        (code = hcdCfgCommand(hc, dev, &trb, &control)) != XHCI_CC_SUCCESS) {
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
            answer = XhciPipeEndpointParamsAt(dev->Selected,
                                              dev->SelectedLength,
                                              iface.EndpointOffset[e],
                                              HcdDevicePipeSpeed(hc, dev),
                                              hc->Hc.HcInfo.Lec, &ep);
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

/* Set TR Dequeue to one ring's software dequeue after a stop or reset: the
 * pipe's own, or for a stream's pipe its stream's - the command then names
 * the Stream ID, with SCT Primary TR (xHCI 6.4.3.9; 31-A.1). */
static ULONG hcdCfgSetDequeueOne(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                 PHCD_PIPE pipe)
{
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG control;
    ULONG pa;
    ULONG dcs;
    ULONG built;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pa = XhciRingDequeuePA(pipe->Ring);
    dcs = XhciRingDequeueCycle(pipe->Ring);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (pipe->StreamId != 0) {
        built = XhciStreamSetDequeueTrb(&trb, dev->SlotId, pipe->Dci,
                                        pipe->StreamId, pa, dcs,
                                        XHCI_STREAM_SCT_PRIMARY_TR) ==
                XHCI_STREAM_OK;
    } else {
        built = XhciTrbSetTrDequeue(&trb, dev->SlotId, pipe->Dci, pa, dcs) ==
                XHCI_RING_OK;
    }
    return built && hcdCfgCommand(hc, dev, &trb, &control) == XHCI_CC_SUCCESS;
}

/* Set TR Dequeue for what `pipe` stands for: a stream its stream; an
 * endpoint with streams open every stream of it, since the controller reads
 * no ring of the endpoint's own (a Stream ID 0 command there is refused);
 * any other pipe its ring. */
static ULONG hcdCfgSetDequeue(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              PHCD_PIPE pipe)
{
    ULONG id;

    if (pipe->Streams == NULL) {
        return hcdCfgSetDequeueOne(hc, dev, pipe);
    }
    for (id = 1; id <= pipe->Streams->Count; id++) {
        if (!hcdCfgSetDequeueOne(hc, dev, pipe->Streams->Pipe[id])) {
            return 0;
        }
    }
    return 1;
}

/* The endpoint a pipe belongs to: a stream's endpoint, or the pipe. */
static PHCD_PIPE hcdCfgEndpointOf(PHCD_PIPE pipe)
{
    return pipe->Parent != NULL ? pipe->Parent : pipe;
}

/* The software ring of `pipe` emptied - dequeue at the enqueue position -
 * and, for an endpoint with streams open, every stream's. Controller lock
 * held. */
static VOID hcdCfgRingsEmpty(PHCD_PIPE pipe)
{
    PHCD_PIPE p;
    ULONG id;

    (VOID)XhciRingSetDequeue(pipe->Ring,
                             XhciRingTrbPA(pipe->Ring, pipe->Ring->Enqueue));
    if (pipe->Streams == NULL) {
        return;
    }
    for (id = 1; id <= pipe->Streams->Count; id++) {
        p = pipe->Streams->Pipe[id];
        (VOID)XhciRingSetDequeue(p->Ring,
                                 XhciRingTrbPA(p->Ring, p->Ring->Enqueue));
    }
}

/*
 * The Set TR Dequeue a Halted endpoint (after its Reset Endpoint) or an
 * Error one owes. Without streams, the pipe's own ring, to its software
 * dequeue past the failed TD. With streams (31-A.1), only each stream whose
 * own event failed (StreamFault, hcd_dev.c) is moved, likewise; every other
 * stream keeps the progress the controller saved in its Stream Context
 * (xHCI 4.12; 4.6.8 keeps it across Reset Endpoint), since a Set TR Dequeue
 * to its oldest unretired TD would replay a TD it had partly moved. A
 * Halted endpoint with no failed stream - a STALL in a Prime Pipe
 * transaction, which names no TD (4.12) - owes none. Error is left only by
 * a Set TR Dequeue (4.8.3): with no failed stream known, every stream's
 * requests are terminated - completed as INTERNAL_HC_ERROR, never replayed
 * - and each stream's ring set empty there. The endpoint is paused and not
 * Running. Returns 0 when a command failed.
 */
static ULONG hcdCfgRecoverDequeue(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                  PHCD_PIPE pipe, ULONG error)
{
    PHCD_PIPE ep;
    PHCD_PIPE p;
    KIRQL oldIrql;
    ULONG any;
    ULONG id;

    ep = hcdCfgEndpointOf(pipe);
    if (ep->Streams == NULL) {
        return hcdCfgSetDequeueOne(hc, dev, pipe);
    }
    any = 0;
    for (id = 1; id <= ep->Streams->Count; id++) {
        p = ep->Streams->Pipe[id];
        if (!p->StreamFault) {
            continue;
        }
        any = 1;
        if (!hcdCfgSetDequeueOne(hc, dev, p)) {
            return 0;
        }
        p->StreamFault = 0;
    }
    if (any || !error) {
        return 1;
    }
    XHCI_DBG_VALUE("hcd: streams endpoint in Error with no failed stream, "
                   "terminated, endpoint", ep->EndpointAddress);
    /* Every request already on the endpoint marked first - one still in
     * the map pump included, which the drain cannot reach and which would
     * otherwise be held and published onto the emptied ring at the resume
     * - and the marked ones settled before the caller resumes (Codex
     * review of 31-A.1, round 2, unit A). */
    (VOID)HcdIoPipeMarkAll(hc, ep, HCD_USBD_INTERNAL_HC_ERROR);
    for (id = 1; id <= ep->Streams->Count; id++) {
        p = ep->Streams->Pipe[id];
        HcdIoDrainPipe(hc, p, HCD_USBD_INTERNAL_HC_ERROR);
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        hcdCfgRingsEmpty(p);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (!hcdCfgSetDequeueOne(hc, dev, p)) {
            return 0;
        }
    }
    HcdIoPipeWaitCancelled(hc, ep);
    return 1;
}

/* Reset Endpoint. `preserve` is TSP: 0 restarts the host's data toggle,
 * 1 keeps it (xHCI 4.6.8 p.115): SYNC_RESET_PIPE and the quiesce of a
 * Halted endpoint ask for that. */
static ULONG hcdCfgResetEndpoint(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                 PHCD_PIPE pipe, ULONG preserve)
{
    XHCI_TRB trb;
    ULONG control;

    return XhciTrbResetEndpoint(&trb, dev->SlotId, pipe->Dci, preserve) ==
               XHCI_RING_OK &&
           hcdCfgCommand(hc, dev, &trb, &control) == XHCI_CC_SUCCESS;
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
 * CLEAR_FEATURE(ENDPOINT_HALT) to the device, and what became of it
 * (HCD_CTL_*, HcdThreadControlOutcome): taken, refused by the device
 * (STALLED or FAILED), or never sent. A device's refusal is the device's -
 * the controller is not at fault, and a controller reset would only
 * re-enumerate every device to settle one device's toggle (QEMU's usb-uas
 * STALLs every endpoint CLEAR_FEATURE; a reset here was a re-init loop,
 * s29k launches 1 and 2) - so no caller resets the controller for it: its
 * caller fails the client's request with the host's half not done
 * (RESET_PIPE, a streams open or close), as usbport does (Codex review of
 * c4ec1c3, finding 1). Thread only.
 */
static ULONG hcdCfgDeviceClearHalt(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                   PHCD_PIPE pipe)
{
    ULONG bytes;
    ULONG outcome;

    outcome = HcdThreadControlOutcome(hc, dev, 0x02, 1, 0,
                                      (USHORT)pipe->EndpointAddress, 0,
                                      &bytes);
    if (outcome != HCD_CTL_DONE) {
        XHCI_DBG_VALUE("hcd: CLEAR_FEATURE(ENDPOINT_HALT) not taken, "
                       "endpoint << 16 | outcome",
                       (pipe->EndpointAddress << 16) | outcome);
    }
    return outcome;
}

/*
 * Bring an endpoint to a state in which its ring may be edited, by the state
 * it is in (Codex review of batch (c), round 2, finding 6): Running is
 * stopped (Stop Endpoint), Halted is reset (Reset Endpoint, xHCI 4.6.8 -
 * legal only there - with TSP 1, so the host keeps its toggle or sequence),
 * Stopped needs nothing, and Error is left for the Set TR Dequeue that every
 * caller then issues, which is what moves it to Stopped (4.8.3). Returns
 * the state reached, or XHCI_EP_STATE_RUNNING when a command failed - the
 * controller reset is then already requested, unless the device has left
 * (hcdCfgFault), whose departure then settles it.
 */
static ULONG hcdCfgQuiesce(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           PHCD_PIPE pipe)
{
    XHCI_TRB trb;
    ULONG control;
    ULONG code;
    ULONG state;

    state = hcdCfgEpState(hc, dev, pipe->Dci);
    if (state == XHCI_EP_STATE_RUNNING) {
        code = 0;
        if (XhciTrbStopEndpoint(&trb, dev->SlotId, pipe->Dci, 0) ==
            XHCI_RING_OK) {
            code = hcdCfgCommand(hc, dev, &trb, &control);
        }
        if (code != XHCI_CC_SUCCESS && code != XHCI_CC_CONTEXT_STATE_ERROR) {
            (VOID)hcdCfgFault(hc, dev);
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
         * of batch (c), round 4, finding 3). For a streams endpoint, the
         * failed streams alone (hcdCfgRecoverDequeue; Codex review of
         * 31-A.1, round 1, finding 2).
         *
         * TSP 1: the host keeps its data toggle or sequence (4.6.8 p.115,
         * the Transfer State Preserve flag; 4.6.10 touches neither), and
         * the device's halt is left where it is - no CLEAR_FEATURE. That
         * halt is the client's to clear with RESET_PIPE, which restarts
         * both ends; an abort or a cancel never clears a device, as
         * usbport's never does (Codex review of the Phase 28-31
         * integration, round 6). Restarting the host alone here, as TSP 0
         * did, owed the device a clear it may refuse and left the two
         * ends' sequences in doubt. With streams the flag applies to the
         * endpoint as it does without: the sequence is the endpoint's, not
         * a stream's, and 4.6.8 does not restrict TSP for a Primary Stream
         * Array endpoint (to verify against the transcription, design
         * record 13, not yet transcribed). */
        if (!hcdCfgResetEndpoint(hc, dev, pipe, 1) ||
            !hcdCfgRecoverDequeue(hc, dev, pipe, 0)) {
            (VOID)hcdCfgFault(hc, dev);
            return XHCI_EP_STATE_RUNNING;
        }
        /* Behind a TT, a control or bulk endpoint's TT buffer may still
         * hold the halted transaction (xHCI 4.6.8 p.116; 27-A.3). */
        HcdHubClearTt(hc, dev, pipe->EndpointAddress, pipe->TransferType, 0);
        state = XHCI_EP_STATE_STOPPED;
    }
    if (state == XHCI_EP_STATE_ERROR) {
        /* Error is left only by Set TR Dequeue, to the software dequeue -
         * before the ring is edited at all (xHCI 4.8.3; Codex review of
         * batch (c), round 3, findings 5 and 6). For a streams endpoint,
         * the failed streams alone (hcdCfgRecoverDequeue). */
        if (!hcdCfgRecoverDequeue(hc, dev, pipe, 1)) {
            (VOID)hcdCfgFault(hc, dev);
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
        hcdCfgCommand(hc, dev, &trb, &control) != XHCI_CC_SUCCESS) {
        (VOID)hcdCfgFault(hc, dev);
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
static ULONG hcdCfgRecycleCode(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
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
    hcdCfgRingsEmpty(pipe);
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
    if (!hcdCfgBuildInput(hc, dev, &plan, add) ||
        XhciTrbConfigureEndpoint(&trb, dev->SlotId,
                                 XhciCommonPA(ext,
                                              ext->Layout.InputContextOffset),
                                 0) != XHCI_RING_OK) {
        return HCD_CFG_NOT_ISSUED;
    }
    return hcdCfgCommand(hc, dev, &trb, &control);
}

/* The same, as success or failure. An endpoint with streams open is added
 * back with them (hcdCfgBuildInput), every stream's ring emptied. */
static ULONG hcdCfgRecycle(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           PHCD_PIPE pipe)
{
    return hcdCfgRecycleCode(hc, dev, pipe) == XHCI_CC_SUCCESS;
}

/*
 * After the thread had a streams endpoint stopped (31-A.1): a doorbell names
 * one stream, so every stream with TDs still on its ring is rung again -
 * the stopped endpoint restarts for the streams rung and no other (xHCI
 * 4.12.2, "to verify" in xhci-data-structures.md). Not a stream paused or
 * closed: its own resume publishes and rings. Nothing for a Disabled
 * endpoint or one without streams. Thread only.
 */
static VOID hcdCfgStreamKick(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                             PHCD_PIPE pipe)
{
    PHCD_PIPE ep;
    PHCD_PIPE p;
    KIRQL oldIrql;
    ULONG id;

    ep = hcdCfgEndpointOf(pipe);
    if (ep->Streams == NULL ||
        hcdCfgEpState(hc, dev, ep->Dci) == XHCI_EP_STATE_DISABLED) {
        return;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (id = 1; ep->Streams != NULL && id <= ep->Streams->Count; id++) {
        p = ep->Streams->Pipe[id];
        if (p->Queue->Count != 0 && !p->Paused && !p->Closed &&
            !dev->Gone) {
            /* Its surviving TDs run again (round 6, finding 4). */
            ep->SeqUsed = 1;
            XhciWriteDoorbell(&hc->Hc, dev->SlotId,
                              XhciStreamDoorbell(p->Dci, p->StreamId));
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
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
    ULONG id;

    (VOID)HcdIoPipeCancelAll(hc, pipe);
    state = hcdCfgQuiesce(hc, dev, pipe);
    if (state == XHCI_EP_STATE_RUNNING) {
        return HCD_USBD_INTERNAL_HC_ERROR;
    }
    HcdIoDrainPipe(hc, pipe, usbd);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hcdCfgRingsEmpty(pipe);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (state != XHCI_EP_STATE_DISABLED && !hcdCfgSetDequeue(hc, dev, pipe)) {
        (VOID)hcdCfgFault(hc, dev);
        return HCD_USBD_INTERNAL_HC_ERROR;
    }
    HcdIoPipeWaitCancelled(hc, pipe);
    pipe->Halted = 0;
    pipe->DrainPending = 0;
    pipe->StreamFault = 0;
    for (id = 1; pipe->Streams != NULL && id <= pipe->Streams->Count; id++) {
        pipe->Streams->Pipe[id]->StreamFault = 0;
    }
    return XHCI_USBD_STATUS_SUCCESS;
}

/* ABORT_PIPE, and the stop-and-drain a refused retire or a removing PDO
 * owes: the body above inside the pipe's pause. For one stream the pause is
 * the whole endpoint's - the stop halts every stream, and a sibling's
 * doorbell would restart it under the Set TR Dequeue - while only that
 * stream's requests are cancelled; the siblings are rung again after
 * (31-A.1). */
static LONG hcdCfgAbort(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                        PHCD_PIPE pipe, LONG usbd)
{
    LONG result;

    HcdIoPipePause(hc, hcdCfgEndpointOf(pipe));
    result = hcdCfgAbortPaused(hc, dev, pipe, usbd);
    if (result == XHCI_USBD_STATUS_SUCCESS) {
        HcdIoPipeResume(hc, hcdCfgEndpointOf(pipe));
        hcdCfgStreamKick(hc, dev, pipe);
    }
    /* On a failure the pipe stays paused: the endpoint may still hold a
     * position no doorbell may restart, and the reset the failure asked
     * for invalidates the device, which drains what is held (Codex review
     * of batch (c), round 5, finding 1). */
    return result;
}

/* Whether any request is on the pipe, wherever it is. Controller lock not
 * held; a snapshot, read inside the pipe's pause. */
static ULONG hcdCfgPipeBusyOne(PHCD_PIPE pipe)
{
    ULONG busy;
    ULONG i;

    busy = pipe->Queue->Count != 0 || !IsListEmpty(&pipe->Held) ||
           !IsListEmpty(&pipe->Waiting);
    for (i = 0; i < pipe->XferCount; i++) {
        if (pipe->Xfers[i].State != HCD_XFER_FREE) {
            busy = 1;
        }
    }
    return busy;
}

static ULONG hcdCfgPipeBusy(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    KIRQL oldIrql;
    ULONG busy;
    ULONG id;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    busy = hcdCfgPipeBusyOne(pipe);
    for (id = 1; pipe->Streams != NULL && id <= pipe->Streams->Count; id++) {
        busy |= hcdCfgPipeBusyOne(pipe->Streams->Pipe[id]);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return busy;
}

/*
 * SYNC_CLEAR_STALL, the device's half alone (task 28-A.1):
 * CLEAR_FEATURE(ENDPOINT_HALT) to the endpoint, the controller's endpoint
 * and the pipe's requests untouched, as usbport's ClearStall leaves them
 * (ReactOS usbport urb.c, USBPORT_ClearStall). An isochronous endpoint has
 * no halt to clear (the same skip as below). A failed request is the
 * client's to see; it says nothing about the controller's toggle, so no
 * recovery is asked for.
 */
static LONG hcdCfgClearStall(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                             PHCD_PIPE pipe)
{
    ULONG bytes;

    if (pipe->TransferType == XHCI_PIPE_XFER_ISOCH) {
        return XHCI_USBD_STATUS_SUCCESS;
    }
    if (!HcdThreadControl(hc, dev, 0x02, 1, 0,
                          (USHORT)pipe->EndpointAddress, 0, &bytes)) {
        return (LONG)XHCI_PIPE_USBD_REQUEST_FAILED;
    }
    return XHCI_USBD_STATUS_SUCCESS;
}

/*
 * SYNC_RESET_PIPE, the controller's half alone (task 28-A.1; Codex review
 * of 28-A.1, round 1, finding 1): the host's halt is cleared and the data
 * toggle KEPT, as Microsoft's URB contract states for this function - the
 * device's toggle is its client's business (SYNC_CLEAR_STALL, or a request
 * of its own), and a host toggle restarted under a device that kept its own
 * loses the next packet as a duplicate. So nothing here may restart it:
 *
 *   busy      any request still on the pipe: USBD_STATUS_ERROR_BUSY, and the
 *             pipe left as it was - usbport's answer (ReactOS usbport
 *             urb.c, USBPORT_ResetPipe) - never the abort-and-quiesce of
 *             RESET_PIPE, whose recovery sends CLEAR_FEATURE(ENDPOINT_HALT);
 *   Halted    Reset Endpoint with TSP 1, which keeps the toggle and moves
 *             the endpoint to Stopped, then Set TR Dequeue to the software
 *             dequeue, which with nothing on the pipe is past the failed TD
 *             (4.6.8 p.116: the retry position is cleared only by it; 4.6.10
 *             touches no toggle). Not a Soft Retry: the doorbell that
 *             follows starts a new TD, not the failed one;
 *   Error     Set TR Dequeue alone (4.8.3), which moves it to Stopped;
 *   other     no halt to clear: nothing is sent, and no context is
 *             recreated - that would restart the toggle (4.6.8 p.118).
 *
 * A command the controller refuses asks for the recovery and leaves the
 * pipe paused, as hcdCfgAbort does.
 */
static LONG hcdCfgResetHost(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            PHCD_PIPE pipe)
{
    ULONG state;
    ULONG ok;

    HcdIoPipePause(hc, pipe);
    if (hcdCfgPipeBusy(hc, pipe)) {
        HcdIoPipeResume(hc, pipe);
        return HCD_USBD_ERROR_BUSY;
    }
    state = hcdCfgEpState(hc, dev, pipe->Dci);
    ok = 1;
    if (state == XHCI_EP_STATE_HALTED) {
        ok = hcdCfgResetEndpoint(hc, dev, pipe, 1) &&
             hcdCfgSetDequeue(hc, dev, pipe);
    } else if (state == XHCI_EP_STATE_ERROR) {
        ok = hcdCfgSetDequeue(hc, dev, pipe);
    }
    if (!ok) {
        (VOID)hcdCfgFault(hc, dev);
        return HCD_USBD_INTERNAL_HC_ERROR;
    }
    pipe->Halted = 0;
    HcdIoPipeResume(hc, pipe);
    return XHCI_USBD_STATUS_SUCCESS;
}

/*
 * RESET_PIPE (`parts` both halves), with SYNC_RESET_PIPE (hcdCfgResetHost)
 * and SYNC_CLEAR_STALL (hcdCfgClearStall) sent to their own paths: inside
 * the pipe's pause from the start, so nothing is published between the
 * check and the reset (round 3, finding 7). Anything still on the pipe is
 * aborted first. Then the device's half - CLEAR_FEATURE(ENDPOINT_HALT),
 * whose failure is returned with the host's half not done (usbport's order;
 * Codex review of c4ec1c3, finding 1) - and the controller's by the
 * endpoint's state (round 2, finding 6), so both ends restart the data
 * toggle (SeqUsed cleared):
 *
 *   Halted    Reset Endpoint (TSP 0, which restarts the toggle), then Set TR
 *             Dequeue past the failed TD;
 *   other     brought to Stopped (hcdCfgQuiesce), then a fresh context
 *             (hcdCfgRecycle), the only toggle restart a non-Halted
 *             endpoint has (xHCI 4.6.8).
 */
static LONG hcdCfgReset(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                        PHCD_PIPE pipe, ULONG parts)
{
    KIRQL oldIrql;
    ULONG ok;
    LONG result;

    /* A stream's handle resets its endpoint, every stream of it: the halt,
     * the reset and the device's sequence are the endpoint's (31-A.1). */
    pipe = hcdCfgEndpointOf(pipe);
    if ((parts & XHCI_PIPE_RESET_HOST) == 0) {
        return hcdCfgClearStall(hc, dev, pipe);
    }
    if ((parts & XHCI_PIPE_RESET_DEVICE) == 0) {
        return hcdCfgResetHost(hc, dev, pipe);
    }
    HcdIoPipePause(hc, pipe);
    result = XHCI_USBD_STATUS_SUCCESS;
    if (hcdCfgPipeBusy(hc, pipe)) {
        result = hcdCfgAbortPaused(hc, dev, pipe, HCD_USBD_CANCELED);
    }
    if (result != XHCI_USBD_STATUS_SUCCESS) {
        return result;          /* left paused, as hcdCfgAbort */
    }
    /* The device's half first, as usbport orders it (SP4 USBPORT.SYS image
     * VA 0x24123 and the failure branch at 0x2412D, NUSB 3.3's 0x23AA3 and
     * 0x23AAD, static; ReactOS usbport urb.c, USBPORT_ResetPipe): a clear
     * the device does not take is the client's answer, and the host's half
     * is not done - the two ends are left as they were, and no controller
     * reset is asked for (Codex review of c4ec1c3, finding 1). A clear that
     * never went out is no answer from the device: it fails busy. */
    if (pipe->TransferType != XHCI_PIPE_XFER_ISOCH) {
        switch (hcdCfgDeviceClearHalt(hc, dev, pipe)) {
        case HCD_CTL_DONE:
            break;
        case HCD_CTL_STALLED:
            HcdIoPipeResume(hc, pipe);
            return XHCI_USBD_STATUS_STALL_PID;
        case HCD_CTL_FAILED:
            HcdIoPipeResume(hc, pipe);
            return HCD_USBD_INTERNAL_HC_ERROR;
        default:
            if (dev->Ep0Stuck) {
                /* Timed out: the reset its control path requested settles
                 * the pipe, paused until it. */
                return HCD_USBD_INTERNAL_HC_ERROR;
            }
            HcdIoPipeResume(hc, pipe);
            return HCD_USBD_ERROR_BUSY;
        }
    }
    if (hcdCfgEpState(hc, dev, pipe->Dci) == XHCI_EP_STATE_HALTED) {
        ok = hcdCfgResetEndpoint(hc, dev, pipe, 0) &&
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
        (VOID)hcdCfgFault(hc, dev);
        result = HCD_USBD_INTERNAL_HC_ERROR;
        return result;          /* left paused, as hcdCfgAbort */
    }
    /* Both ends restarted: the sequence is fresh. */
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe->Halted = 0;
    pipe->SeqUsed = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdIoPipeResume(hc, pipe);
    hcdCfgStreamKick(hc, dev, pipe);
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

    /* A stream's cancel pauses its whole endpoint, as its abort does
     * (hcdCfgAbort). */
    HcdIoPipePause(hc, hcdCfgEndpointOf(pipe));
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
        (VOID)hcdCfgFault(hc, dev);
        HcdIoDeferred(hc);
        return;                 /* left paused, as hcdCfgAbort */
    }
    /* Under the lock the teardown's freeze sets Gone under: no doorbell
     * reaches a device being torn down (finding 1). */
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (pipe->Queue->Count != 0 && state != XHCI_EP_STATE_DISABLED &&
        !dev->Gone) {
        /* The survivors run again: the device's sequence moves (Codex
         * review of the Phase 28-31 integration, round 6, finding 4). */
        hcdCfgEndpointOf(pipe)->SeqUsed = 1;
        XhciWriteDoorbell(&hc->Hc, dev->SlotId,
                          XhciStreamDoorbell(pipe->Dci, pipe->StreamId));
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdIoDeferred(hc);
    HcdIoPipeResume(hc, hcdCfgEndpointOf(pipe));
    hcdCfgStreamKick(hc, dev, pipe);
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
            code = hcdCfgCommand(hc, dev, &trb, &control);
        }
        if (code != XHCI_CC_SUCCESS && code != XHCI_CC_CONTEXT_STATE_ERROR) {
            (VOID)hcdCfgFault(hc, dev);
            return 0;
        }
    }
    HcdIoDrainPipe(hc, pipe, HCD_USBD_CANCELED);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hcdCfgRingsEmpty(pipe);
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
    KIRQL oldIrql;
    PUCHAR s;
    ULONG control;
    ULONG bytes;
    ULONG mask;
    ULONG dci;
    ULONG id;
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
    /* Reset Device disabled every endpoint but EP0, the stale ones too,
     * and so no context names a retired stream array any more. */
    dev->Stale = 0;
    hcdCfgRetiredReclaim(hc, dev, XHCI_PIPE_ENDPOINT_MASK);

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
             hcdCfgCommand(hc, dev, &trb, &control) != XHCI_CC_SUCCESS)) {
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
    /* Both ends of every restored endpoint start afresh - the device by
     * its reset and SET_CONFIGURATION, the controller by the Configure
     * Endpoint - so each endpoint's sequence and halt record go with them
     * before anything is published again (Codex review of the Phase 28-31
     * integration, round 6, finding 3; uas_xport.c's device reset keeps
     * its stream handles across this). */
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (dci = 2; dci < 32; dci++) {
        if (add[dci] == NULL) {
            continue;
        }
        add[dci]->SeqUsed = 0;
        add[dci]->Halted = 0;
        for (id = 1; add[dci]->Streams != NULL &&
                     id <= add[dci]->Streams->Count;
             id++) {
            add[dci]->Streams->Pipe[id]->Halted = 0;
            add[dci]->Streams->Pipe[id]->StreamFault = 0;
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
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
/* Streams (31-A.1; xhci98_streams.h)                                       */
/* ----------------------------------------------------------------------- */

/* The MaxStreams exponent of `pipe`'s endpoint, from its SuperSpeed
 * Endpoint Companion in the configuration the pipes were opened from, at
 * the alternate its interface runs. Thread only. */
static ULONG hcdCfgStreamExponent(PHCD_USB_DEVICE dev, PHCD_PIPE pipe,
                                  PULONG exponent)
{
    XHCI_PIPE_IFACE iface;
    PUCHAR config;
    ULONG length;
    ULONG i;

    *exponent = 0;
    config = dev->Selected != NULL ? dev->Selected : dev->Config;
    length = dev->Selected != NULL ? dev->SelectedLength : dev->ConfigLength;
    if (config == NULL || pipe->Interface >= 32UL ||
        XhciPipeFindInterface(config, length, pipe->Interface,
                              dev->Alternate[pipe->Interface],
                              &iface) != XHCI_PIPE_OK) {
        return XHCI_STREAM_EP_NONE;
    }
    for (i = 0; i < iface.EndpointCount; i++) {
        if ((ULONG)config[iface.EndpointOffset[i] + 2] ==
            pipe->EndpointAddress) {
            return XhciStreamCompanionExponent(config, length,
                                               iface.EndpointOffset[i],
                                               exponent);
        }
    }
    return XHCI_STREAM_EP_NONE;
}

/* A stream block and its pipes, built for `pipe` by `plan`, every stream's
 * ring empty and every stream's pipe paused once - the open resumes them
 * with the endpoint. NULL when memory or the common buffer runs out. */
static PHCD_STREAMS hcdCfgStreamsBuild(PHCD_CONTROLLER hc,
                                       PHCD_USB_DEVICE dev, PHCD_PIPE pipe,
                                       const XHCI_STREAM_PLAN *plan)
{
    PXHCI_EXTENSION ext;
    PHCD_STREAMS st;
    PHCD_PIPE p;
    PUCHAR va;
    ULONG offset;
    ULONG id;
    ULONG i;

    ext = &hc->Hc;
    st = (PHCD_STREAMS)HcdPoolAlloc(sizeof(HCD_STREAMS));
    if (st == NULL) {
        return NULL;
    }
    for (i = 0; i < sizeof(HCD_STREAMS); i++) {
        ((PUCHAR)st)[i] = 0;
    }
    st->Count = plan->Streams;
    st->Dci = pipe->Dci;
    st->Entries = plan->Entries;
    st->MaxPStreams = plan->MaxPStreams;
    if (XhciStreamLayout(plan->Entries, plan->Streams,
                         ext->Layout.PoolRingTrbs, &st->Layout) !=
        XHCI_STREAM_OK) {
        HcdPoolFree(st);
        return NULL;
    }
    st->Va = HcdDmaStreamAlloc(hc, st->Layout.TotalBytes, &st->Pa);
    if (st->Va == NULL) {
        HcdPoolFree(st);
        return NULL;
    }
    va = (PUCHAR)st->Va;
    for (i = 0; i < st->Layout.TotalBytes; i++) {
        va[i] = 0;
    }
    for (id = 1; id <= st->Count; id++) {
        p = (PHCD_PIPE)HcdPoolAlloc(HCD_STREAM_PIPE_BYTES);
        if (p == NULL) {
            goto fail;
        }
        for (i = 0; i < HCD_STREAM_PIPE_BYTES; i++) {
            ((PUCHAR)p)[i] = 0;
        }
        st->Pipe[id] = p;
        HcdIoPipeInitCount(p, dev, HCD_STREAM_XFERS);
        p->Ep = pipe->Ep;
        p->Dci = pipe->Dci;
        p->EndpointAddress = pipe->EndpointAddress;
        p->TransferType = pipe->TransferType;
        p->MaxPacketSize = pipe->MaxPacketSize;
        p->Interval = pipe->Interval;
        p->Interface = pipe->Interface;
        p->PoolIndex = 0xFFFFFFFFUL;
        p->StreamId = id;
        p->Parent = pipe;
        p->Paused = 1;
        p->Ring = &p->OwnRing;
        p->Queue = &p->OwnQueue;
        XhciXferQueueInit(&p->OwnQueue);
        offset = XhciStreamRingOffset(&st->Layout, id);
        if (XhciRingInit(&p->OwnRing, (volatile XHCI_TRB *)(va + offset),
                         st->Pa.LowPart + offset, ext->Layout.PoolRingTrbs,
                         XHCI_RING_KIND_ENDPOINT) != XHCI_RING_OK) {
            goto fail;
        }
        st->Ring[id] = &p->OwnRing;
    }
    return st;

fail:
    for (id = 1; id < XHCI_STREAM_MAX_ENTRIES; id++) {
        HcdPoolFree(st->Pipe[id]);
    }
    HcdDmaStreamFree(hc, st->Layout.TotalBytes, st->Pa, st->Va);
    HcdPoolFree(st);
    return NULL;
}

/* Whether the device's sequence on endpoint `pipe` has moved since both
 * ends last restarted it (SeqUsed, set at each publication by hcd_io.c on
 * the endpoint whichever stream carried it). Read under the lock, by a
 * caller that has the endpoint paused and its requests settled, so no
 * publication can follow the read (Codex review of c4ec1c3, finding 2).
 * Thread only. */
static ULONG hcdCfgStreamsUsed(PHCD_CONTROLLER hc, PHCD_PIPE pipe)
{
    KIRQL oldIrql;
    ULONG used;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    used = pipe->SeqUsed;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return used;
}

/*
 * The device's half of the sequence restart a streams open or close owes,
 * on the paused, settled endpoint, before the Configure Endpoint that
 * gives the controller's half a fresh context. An endpoint that has carried
 * no request since both ends last restarted its sequence (`used` 0,
 * hcdCfgStreamsUsed) needs nothing: the select's SET_CONFIGURATION or
 * SET_INTERFACE, or the last RESET_PIPE or RESET_PORT, already restarted
 * the device's (USB 3.2 9.4.5) - the UAS start opens its streams there.
 * Otherwise CLEAR_FEATURE(ENDPOINT_HALT) to the device; taken, SeqUsed is
 * cleared, the Configure Endpoint then restarting the host's. Not taken -
 * refused, or never sent - the request fails with nothing changed on the
 * host, so the client recovers the endpoint as from any failure (RESET_PIPE,
 * RESET_PORT), and no controller reset is asked for (Codex review of the
 * Phase 28-31 integration, round 6). Returns HCD_CTL_*. Thread only.
 */
static ULONG hcdCfgStreamsSequence(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                   PHCD_PIPE pipe, ULONG used)
{
    KIRQL oldIrql;
    ULONG outcome;

    if (!used) {
        XHCI_DBG_VALUE("hcd: streams, sequence fresh since select, endpoint",
                       pipe->EndpointAddress);
        return HCD_CTL_DONE;
    }
    outcome = hcdCfgDeviceClearHalt(hc, dev, pipe);
    if (outcome == HCD_CTL_DONE) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        pipe->SeqUsed = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
    }
    return outcome;
}

/*
 * OPEN_STREAMS on an endpoint's own handle. Refused, the endpoint as it
 * was, for a controller with MaxPSASize 0, an endpoint that is not a
 * SuperSpeed bulk endpoint whose companion names streams, or one with
 * streams open. Otherwise: the block and the stream pipes built; the
 * endpoint paused and its own requests aborted (hcdCfgAbortPaused, which
 * leaves it Stopped with its ring empty); the streams attached and the
 * endpoint given a fresh context with them - one Configure Endpoint with
 * the DCI dropped and added, as xHCI 4.6.6 requires for an endpoint not
 * Disabled, its TR Dequeue Pointer the array's (hcdCfgRecycleCode); the
 * device's sequence restarted; the handles made live and the endpoint and
 * its streams resumed. A Configure Endpoint the controller refuses leaves
 * the old context in place (4.6.6: the command changes nothing when it
 * fails), so the streams are detached and freed and the endpoint resumed as
 * it was. Thread only, powered.
 */
static ULONG hcdCfgStreamsOpen(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                               PHCD_DEVICE_PDO pdo,
                               PXHCI98_STREAMS_REQUEST rq)
{
    XHCI_STREAM_PLAN plan;
    PHCD_STREAMS st;
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    ULONG hcc1;
    ULONG hcEntries;
    ULONG exponent;
    ULONG answer;
    ULONG cleared;
    ULONG code;
    ULONG id;

    if (rq->StreamsRequested == 0 ||
        rq->StreamsRequested > XHCI98_STREAMS_MAX) {
        return XHCI98_STREAMS_INVALID_REQUEST;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe = hcdCfgEndpointPipe(dev, rq->PipeHandle);
    if (pipe != NULL && (pipe->Closed || !HcdPdoOwnsPipe(pdo, dev, pipe))) {
        pipe = NULL;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (pipe == NULL) {
        return XHCI98_STREAMS_INVALID_PIPE;
    }
    if (pipe->Streams != NULL) {
        return XHCI98_STREAMS_ALREADY_OPEN;
    }
    hcc1 = XhciBarReader(&hc->Hc, XHCI_CAP_HCCPARAMS1);
    hcEntries = XhciStreamHcEntries(hcc1);
    XHCI_DBG_VALUE("hcd: streams asked, endpoint << 16 | requested",
                   (pipe->EndpointAddress << 16) | rq->StreamsRequested);
    XHCI_DBG_VALUE("hcd: streams, HCCPARAMS1 MaxPSASize",
                   XHCI_HCCPARAMS1_MAXPSA(hcc1));
    if (hcEntries == 0) {
        XHCI_DBG_TEXT("hcd: streams refused, the controller has none");
        return XHCI98_STREAMS_NO_CONTROLLER;
    }
    /* Streams are a SuperSpeed bulk endpoint's (USB 3.2 9.6.7): the
     * companion's MaxStreams is the device's word on it. */
    if (pipe->TransferType != XHCI_PIPE_XFER_BULK ||
        pdo->SpeedClass < XHCI_SPEED_SUPER ||
        hcdCfgStreamExponent(dev, pipe, &exponent) != XHCI_STREAM_OK) {
        XHCI_DBG_VALUE("hcd: streams refused, endpoint", pipe->EndpointAddress);
        return XHCI98_STREAMS_NO_ENDPOINT;
    }
    answer = XhciStreamPlan(rq->StreamsRequested, exponent, hcEntries,
                            XHCI_STREAM_MAX_ENTRIES, &plan);
    if (answer == XHCI_STREAM_HC_NONE) {
        return XHCI98_STREAMS_NO_CONTROLLER;
    }
    if (answer != XHCI_STREAM_OK) {
        XHCI_DBG_VALUE("hcd: streams refused by the plan, answer", answer);
        return answer == XHCI_STREAM_BAD_PARAM
                   ? XHCI98_STREAMS_INVALID_REQUEST
                   : XHCI98_STREAMS_NO_ENDPOINT;
    }
    st = hcdCfgStreamsBuild(hc, dev, pipe, &plan);
    if (st == NULL) {
        return XHCI98_STREAMS_NO_RESOURCES;
    }

    HcdIoPipePause(hc, pipe);
    if (hcdCfgAbortPaused(hc, dev, pipe, HCD_USBD_CANCELED) !=
        XHCI_USBD_STATUS_SUCCESS) {
        /* Left paused, as hcdCfgAbort leaves it: the reset the failure
         * asked for settles the endpoint. */
        hcdCfgStreamsFree(hc, st);
        return XHCI98_STREAMS_FAILED;
    }
    /* The device's half first, on the paused and settled endpoint: a
     * refusal fails the open with the host's context untouched. */
    cleared = hcdCfgStreamsUsed(hc, pipe);
    if (hcdCfgStreamsSequence(hc, dev, pipe, cleared) != HCD_CTL_DONE) {
        hcdCfgStreamsFree(hc, st);
        if (!dev->Ep0Stuck) {
            HcdIoPipeResume(hc, pipe);
        }
        return XHCI98_STREAMS_FAILED;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pipe->Streams = st;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    code = hcdCfgRecycleCode(hc, dev, pipe);
    if (code != XHCI_CC_SUCCESS) {
        XHCI_DBG_VALUE("hcd: streams Configure Endpoint failed, code", code);
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        pipe->Streams = NULL;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (code == 0) {
            /* Issued with no completion - timed out or lost: the command
             * may have installed the array, or may still be running, and
             * the reset this asks for masks interrupts but stops no DMA.
             * The block is retired, not freed, until a dropped endpoint, a
             * Reset Device or the slot's going proves the controller done
             * with it (hcdCfgRetiredReclaim, HcdCfgDeviceGone), and the
             * endpoint stays paused for the reset, as CLOSE_STREAMS leaves
             * it (Codex review of 31-A.1, round 1, finding 1). */
            st->Next = dev->StreamsRetired;
            dev->StreamsRetired = st;
            XHCI_DBG_TEXT("hcd: streams Configure Endpoint timed out, "
                          "controller reset");
            HcdSvcRequestReset(&hc->Hc);
            return XHCI98_STREAMS_FAILED;
        }
        /* Refused with a completion code, or never issued: the old
         * context stands (4.6.6) and nothing names the block. */
        hcdCfgStreamsFree(hc, st);
        if (hc->Hc.ControllerFailed) {
            return XHCI98_STREAMS_FAILED;
        }
        /* The device's sequence was restarted by the clear above, and the
         * old context kept the host's: the two must match before anything
         * runs again. Reset Endpoint is legal only on a Halted endpoint
         * (xHCI 4.6.8) and this one is Stopped, so the host's is restarted
         * the one other way, a fresh context for the endpoint as it was,
         * without streams (hcdCfgRecycleCode; 4.6.6). If that is refused
         * too the endpoint stays paused and the controller is reset - it
         * never resumes with the two ends apart (Codex review of the Phase
         * 28-31 integration, round 7, finding 1). SeqUsed stays 0: both
         * ends are fresh. */
        if (cleared && hcdCfgRecycleCode(hc, dev, pipe) != XHCI_CC_SUCCESS) {
            XHCI_DBG_VALUE("hcd: streams open failed, host sequence not "
                           "restarted, endpoint", pipe->EndpointAddress);
            (VOID)hcdCfgFault(hc, dev);
            return XHCI98_STREAMS_FAILED;
        }
        HcdIoPipeResume(hc, pipe);
        return code == XHCI_CC_RESOURCE_ERROR ? XHCI98_STREAMS_NO_RESOURCES
                                              : XHCI98_STREAMS_FAILED;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    st->Live = 1;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    for (id = 0; id <= XHCI98_STREAMS_MAX; id++) {
        rq->StreamPipeHandle[id] = (id >= 1 && id <= st->Count)
                                       ? (PVOID)st->Pipe[id]
                                       : NULL;
    }
    rq->StreamsGranted = st->Count;
    HcdIoPipeResume(hc, pipe);
    XHCI_DBG_VALUE("hcd: streams open, endpoint << 16 | granted",
                   (pipe->EndpointAddress << 16) | st->Count);
    return XHCI98_STREAMS_SUCCESS;
}

/*
 * CLOSE_STREAMS: every stream's requests aborted (hcdCfgAbortPaused over
 * the endpoint, which reaches every stream and leaves the endpoint
 * Stopped), the device's sequence restarted (hcdCfgStreamsSequence; a
 * refusal fails the close with the streams open), the streams detached and
 * closed - their handles stop resolving and anything submitted meanwhile,
 * held by the pause, completes as cancelled - the endpoint given a fresh
 * context on its own ring again (hcdCfgRecycleCode with no streams), the
 * streams freed and the endpoint resumed. A refused Configure Endpoint
 * leaves the array the controller's: the streams stay attached, closed, for
 * the reset the failure asked for to free with the device. ABORT_PIPE on
 * the endpoint's own handle does not come here: it closes nothing. Thread
 * only, powered.
 */
static ULONG hcdCfgStreamsClose(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                PHCD_PIPE pipe)
{
    PHCD_STREAMS st;
    KIRQL oldIrql;
    ULONG used;
    ULONG id;

    st = pipe->Streams;
    HcdIoPipePause(hc, pipe);
    if (hcdCfgAbortPaused(hc, dev, pipe, HCD_USBD_CANCELED) !=
        XHCI_USBD_STATUS_SUCCESS) {
        return XHCI98_STREAMS_FAILED;   /* left paused, as hcdCfgAbort */
    }
    /* Paused and every request settled: nothing can publish after this
     * read, and SeqUsed outlives the stream records freed below (Codex
     * review of c4ec1c3, finding 2). The device's half first: a refusal
     * fails the close with the streams still open and the host's context
     * untouched (round 6). */
    used = hcdCfgStreamsUsed(hc, pipe);
    if (hcdCfgStreamsSequence(hc, dev, pipe, used) != HCD_CTL_DONE) {
        if (!dev->Ep0Stuck) {
            HcdIoPipeResume(hc, pipe);
        }
        return XHCI98_STREAMS_FAILED;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    st->Live = 0;
    for (id = 1; id <= st->Count; id++) {
        st->Pipe[id]->Closed = 1;
    }
    pipe->Streams = NULL;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    for (id = 1; id <= st->Count; id++) {
        HcdIoDrainPipe(hc, st->Pipe[id], HCD_USBD_CANCELED);
        HcdIoWaitPipe(hc, st->Pipe[id]);
    }
    if (hcdCfgRecycleCode(hc, dev, pipe) != XHCI_CC_SUCCESS) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        pipe->Streams = st;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        (VOID)hcdCfgFault(hc, dev);
        return XHCI98_STREAMS_FAILED;   /* left paused */
    }
    hcdCfgStreamsFree(hc, st);
    HcdIoPipeResume(hc, pipe);
    XHCI_DBG_VALUE("hcd: streams closed, endpoint", pipe->EndpointAddress);
    return XHCI98_STREAMS_SUCCESS;
}

/* One OPEN_STREAMS or CLOSE_STREAMS, pended by hcd_urb.c with a device
 * reference, served and completed here; the request's Status says what
 * happened and the IRP's status mirrors it (xhci98_streams.h). Thread
 * only, powered. */
static VOID hcdCfgStreamsRequest(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                                 PHCD_DEVICE_PDO pdo, PIRP irp, ULONG code)
{
    PXHCI98_STREAMS_REQUEST rq;
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    NTSTATUS status;
    ULONG answer;

    rq = (PXHCI98_STREAMS_REQUEST)
             IoGetCurrentIrpStackLocation(irp)->Parameters.Others.Argument1;
    if (code == IOCTL_XHCI98_OPEN_STREAMS) {
        answer = hcdCfgStreamsOpen(hc, dev, pdo, rq);
    } else {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        pipe = hcdCfgEndpointPipe(dev, rq->PipeHandle);
        if (pipe != NULL && !HcdPdoOwnsPipe(pdo, dev, pipe)) {
            pipe = NULL;
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (pipe == NULL) {
            answer = XHCI98_STREAMS_INVALID_PIPE;
        } else if (pipe->Streams == NULL) {
            answer = XHCI98_STREAMS_NOT_OPEN;
        } else {
            answer = hcdCfgStreamsClose(hc, dev, pipe);
        }
    }
    rq->Status = answer;
    switch (answer) {
    case XHCI98_STREAMS_SUCCESS:
        status = STATUS_SUCCESS;
        break;
    case XHCI98_STREAMS_NO_CONTROLLER:
    case XHCI98_STREAMS_NO_ENDPOINT:
        status = STATUS_NOT_SUPPORTED;
        break;
    case XHCI98_STREAMS_ALREADY_OPEN:
        status = STATUS_DEVICE_BUSY;
        break;
    case XHCI98_STREAMS_NO_RESOURCES:
        status = STATUS_INSUFFICIENT_RESOURCES;
        break;
    case XHCI98_STREAMS_FAILED:
        status = dev->Gone ? STATUS_NO_SUCH_DEVICE : STATUS_UNSUCCESSFUL;
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    (VOID)InterlockedDecrement(&pdo->UrbsPending);
    (VOID)InterlockedDecrement(&dev->Refs);
}

/* ----------------------------------------------------------------------- */
/* Cancellation                                                             */
/* ----------------------------------------------------------------------- */

/* One pipe's owed work: a refused retire (hcd_dev.c) is a stop and drain,
 * a cancelled record the cancel. Thread only. */
static VOID hcdCfgCancelOne(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            PHCD_PIPE pipe)
{
    if (pipe->DrainPending) {
        /* On a streams endpoint's own pipe only a stall no stream could be
         * given sets it (hcd_dev.c, a Prime Pipe STALL): every stream's
         * requests complete as stalled, the endpoint reset on both sides,
         * the streams left open. */
        (VOID)hcdCfgAbort(hc, dev, pipe,
                          pipe->Streams != NULL ? HCD_USBD_STALL_PID
                                                : HCD_USBD_INTERNAL_HC_ERROR);
    } else if (pipe->CancelPending) {
        hcdCfgCancelPipe(hc, dev, pipe);
    }
}

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
    ULONG id;

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
            if (pipe == NULL) {
                continue;
            }
            hcdCfgCancelOne(hc, dev, pipe);
            /* Each stream's own marks (31-A.1). */
            for (id = 1; pipe->Streams != NULL && id <= pipe->Streams->Count;
                 id++) {
                hcdCfgCancelOne(hc, dev, pipe->Streams->Pipe[id]);
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
    ULONG code;
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
        code = IoGetCurrentIrpStackLocation(irp)
                   ->Parameters.DeviceIoControl.IoControlCode;
        if (code == IOCTL_XHCI98_OPEN_STREAMS ||
            code == IOCTL_XHCI98_CLOSE_STREAMS) {
            hcdCfgStreamsRequest(hc, dev, pdo, irp, code);
            hcdCfgReleaseAgain(hc, dev, held);
            continue;
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
            usbd = hcdCfgSelectAnswer(hc, dev, usbd);
            break;
        case HCD_URB_SELECT_INTERFACE:
            usbd = hcdCfgSelectInterface(hc, dev, pdo, urb);
            number = urb->UrbSelectInterface.Interface.InterfaceNumber;
            if (usbd == XHCI_USBD_STATUS_SUCCESS && number < 32) {
                held &= ~(1UL << number);
                hcdCfgReleaseSettled(dev, 1UL << number);
            }
            usbd = hcdCfgSelectAnswer(hc, dev, usbd);
            break;
        case HCD_URB_ABORT_PIPE:
        case HCD_URB_RESET_PIPE:
        case HCD_URB_SYNC_RESET_PIPE:
        case HCD_URB_SYNC_CLEAR_STALL:
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
                /* On a streams endpoint's own handle the abort reaches
                 * every stream and closes nothing: the streams, their
                 * handles and the endpoint's context stay, with no
                 * Configure Endpoint and no CLEAR_FEATURE; only
                 * CLOSE_STREAMS, a select and removal close them
                 * (xhci98_streams.h; Codex review of the Phase 28-31
                 * integration, round 7, finding 2). */
                usbd = hcdCfgAbort(hc, dev, pipe, HCD_USBD_CANCELED);
            } else {
                usbd = hcdCfgReset(
                    hc, dev, pipe,
                    XhciPipeResetParts(urb->UrbHeader.Function));
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
