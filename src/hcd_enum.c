/*
 * hcd_enum.c - root ports and enumeration on the controller thread
 * (roadmap-hcd.md tasks 26-A.3 and 26-A.4; design record 13 sections 5.3,
 * 10.2).
 *
 * The thread takes the ports the event DPC marked changed (hcd_dev.c,
 * XhciRootHubPortEvent), reads each one's PORTSC, acknowledges its change
 * bits, and feeds the port's state machine (xhci_enum.c) a connect or a
 * disconnect. Each action the machine asks for is carried out here, waited
 * for, and its outcome fed back as the next event, until the machine asks for
 * nothing. Commands are waited for on the device layer's completion event;
 * EP0 control transfers on the transfer completion the device layer matches
 * by slot and endpoint (hcd_dev.c).
 *
 * The speed is the port's own, carried into the Slot Context and onward:
 * there is no High-Speed lie and no SET_ADDRESS (section 5.3).
 *
 * Timings, section 10.2: attach debounce 100 ms (TATTDB), the reset timed by
 * the xHC and waited for up to 500 ms, reset recovery 10 ms (TRSTRCY),
 * SetAddress recovery 2 ms (TDSETADDR).
 *
 * Descriptors are read into a 4 KB common buffer of the controller's own
 * (hcd_dma.c), the one DMA-visible scratch the enumeration needs; a
 * configuration descriptor longer than that is refused as a configuration
 * failure.
 *
 * IRQL: PASSIVE_LEVEL (the controller thread), except where a function says
 * otherwise.
 */

#include "hcd.h"
#include "xhci_hw.h"
#include "hcd_svc.h"
#include "xhci_xfer.h"
#include "xhci_enum.h"
#include "xhci_dbg.h"

#define HCD_DEBOUNCE_MS        100UL
#define HCD_RESET_WAIT_MS      500UL
#define HCD_RESET_RECOVERY_MS  10UL
#define HCD_SETADDRESS_MS      2UL
#define HCD_COMMAND_WAIT_MS    5000UL
#define HCD_TRANSFER_WAIT_MS   5000UL
#define HCD_POLL_STEP_MS       10UL

#define HCD_DESC_DEVICE        1
#define HCD_DESC_CONFIGURATION 2

/* No RtlCopyBytes: the DDK spells it as memcpy, an import no row admits. */
static VOID hcdCopy(PUCHAR to, const UCHAR *from, ULONG bytes)
{
    ULONG i;

    for (i = 0; i < bytes; i++) {
        to[i] = from[i];
    }
}

/* ----------------------------------------------------------------------- */
/* Waits                                                                    */
/* ----------------------------------------------------------------------- */

static ULONG hcdWaitEvent(PKEVENT event, ULONG milliseconds)
{
    LARGE_INTEGER due;
    NTSTATUS status;

    HcdRelativeMs(&due, milliseconds);
    status = KeWaitForSingleObject(event, Executive, KernelMode, FALSE, &due);
    return status == STATUS_SUCCESS;
}

/* ----------------------------------------------------------------------- */
/* Commands                                                                 */
/* ----------------------------------------------------------------------- */

/* The enumeration stops for the pass once the controller has failed or a
 * timed-out transfer may still DMA into the scratch: the recovery that
 * follows invalidates every slot, and the thread rescans from there. */
static ULONG hcdHalted(PHCD_CONTROLLER hc)
{
    return hc->Hc.ControllerFailed || hc->ScratchTainted;
}

/*
 * Submit one command and wait for its own completion. Returns the completion
 * code, or 0 when it never completed (lost, refused or timed out); *control
 * receives the Command Completion Event's DW3.
 *
 * A completion counts only when its TRB is the one this submit placed
 * (CmdDonePA, from hcd_dev.c): a completion of an earlier command, the
 * start's No Op among them, can land between the clear and the submit
 * (Codex review of batch (b), round 1, finding 7). The event is cleared
 * before the address is read, so a completion arriving in between still
 * leaves it signalled. A command that never answers is the controller's
 * failure: the reset is requested, and the recovery's invalidation settles
 * whatever the command owned.
 */
static ULONG hcdCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb,
                        PULONG control)
{
    ULONG trbPA;
    ULONG answer;
    ULONG rounds;

    *control = 0;
    if (hcdHalted(hc)) {
        return 0;
    }
    KeClearEvent(&hc->CmdDoneEvent);
    hc->CmdDoneLost = 0;
    hc->CmdDonePA = 0;
    trbPA = 0;
    answer = XhciCommandSubmit(&hc->Hc, trb, &trbPA, XHCI_ARM_UNLOCKED);
    if (answer != XHCI_CMD_OK) {
        hc->EnumCommandsRefused++;
        return 0;
    }
    for (rounds = 0; rounds < 4; rounds++) {
        KeClearEvent(&hc->CmdDoneEvent);
        if (hc->CmdDoneLost) {
            /* The engine's own watchdog aborted it: what the command did,
             * an Enable Slot's slot among it, is unknown, and only the
             * reset settles that (round 2, finding 5). */
            break;
        }
        if (hc->CmdDonePA == trbPA) {
            *control = hc->CmdDoneControl;
            return hc->CmdDoneCode;
        }
        if (!hcdWaitEvent(&hc->CmdDoneEvent, HCD_COMMAND_WAIT_MS)) {
            break;
        }
    }
    hc->EnumCommandsTimedOut++;
    HcdSvcRequestReset(&hc->Hc);
    return 0;
}

/* A command from the thread for the configuration code (hcd_cfg.c). */
ULONG HcdThreadCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb,
                       PULONG control)
{
    return hcdCommand(hc, trb, control);
}

/* ----------------------------------------------------------------------- */
/* Device records                                                           */
/* ----------------------------------------------------------------------- */

static PHCD_USB_DEVICE hcdDeviceNew(PHCD_CONTROLLER hc, ULONG port,
                                    ULONG slotId, ULONG speed)
{
    PHCD_USB_DEVICE dev;
    KIRQL oldIrql;
    PUCHAR p;
    ULONG i;

    dev = (PHCD_USB_DEVICE)HcdPoolAlloc(sizeof(HCD_USB_DEVICE));
    if (dev == NULL) {
        return NULL;
    }
    p = (PUCHAR)dev;
    for (i = 0; i < sizeof(HCD_USB_DEVICE); i++) {
        p[i] = 0;
    }
    dev->Controller = hc;
    dev->Port = port;
    dev->SlotId = slotId;
    dev->Speed = speed;
    XhciXferQueueInit(&dev->Ep0Queue);
    HcdIoPipeInitEp0(dev);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->SlotDevice[slotId] = dev;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return dev;
}

/* The record leaves the slot table under the controller lock, so the event
 * DPC, which matches EP0 events through SlotDevice[] (hcd_dev.c), never
 * holds a record being freed. IRQL: PASSIVE_LEVEL. */
static VOID hcdDeviceFree(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;
    ULONG kept;

    if (dev == NULL) {
        return;
    }
    /* Its URBs first: the slot is disabled or HCRST has taken it, so the
     * engine's queue can be drained and every IRP completed (hcd_io.c). */
    kept = !HcdIoDeviceGone(hc, dev);
    if (!kept) {
        HcdCfgDeviceGone(hc, dev);
    }
    if (dev->SlotId != 0 && dev->SlotId <= XHCI_MAX_SLOTS) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (hc->SlotDevice[dev->SlotId] == dev) {
            hc->SlotDevice[dev->SlotId] = NULL;
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
    }
    if (kept) {
        /* URBs still pending against a controller that may still DMA:
         * the record and its pipes stay allocated for good. */
        hc->DevicesKept++;
        return;
    }
    HcdPoolFree(dev->Config);
    HcdPoolFree(dev);
}

/* Disable Slot for a slot id. Returns 1 when the controller confirmed it; on
 * any other outcome the reset is requested, since only the HCRST still
 * proves the slot's contexts and rings out of the controller's hands. */
static ULONG hcdDisableSlotId(PHCD_CONTROLLER hc, ULONG slotId)
{
    XHCI_TRB trb;
    ULONG control;

    if (XhciTrbDisableSlot(&trb, slotId) == XHCI_RING_OK &&
        hcdCommand(hc, &trb, &control) == XHCI_CC_SUCCESS) {
        return 1;
    }
    hc->EnumDisableFailures++;
    HcdSvcRequestReset(&hc->Hc);
    return 0;
}

/*
 * Give a slot back: Disable Slot, then its DCBAA entry cleared and its
 * record freed. A Disable Slot the controller did not confirm leaves the
 * record quarantined instead - off the port, still in SlotDevice[], its
 * DCBAA entry and EP0 ring untouched - until the recovery the failure
 * requested invalidates every slot and the thread frees it (Codex review of
 * batch (b), round 1, finding 8).
 */
static VOID hcdDisableRecord(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PXHCI_EXTENSION ext;
    ULONG slotId;
    volatile ULONG *dcbaa;

    ext = &hc->Hc;
    slotId = dev->SlotId;
    if (!hcdDisableSlotId(hc, slotId)) {
        dev->Abandoned = 0;
        return;
    }
    dcbaa = XhciCommonAt(ext, ext->Layout.DcbaaOffset + slotId * 8UL);
    dcbaa[0] = 0;
    dcbaa[1] = 0;
    hcdDeviceFree(hc, dev);
}

static VOID hcdDisableSlot(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PHCD_USB_DEVICE dev;

    dev = p->Device;
    if (dev == NULL) {
        return;
    }
    p->Device = NULL;
    hcdDisableRecord(hc, dev);
}

/*
 * EP0 halted on a URB's control transfer (a STALL; hcd_dev.c set Ep0Halted):
 * Reset Endpoint with TSP 0, then Set TR Dequeue to where the transfer
 * engine already moved the software dequeue - the next TD's head, or the
 * enqueue position - mandatory for a control endpoint after a reset (xHCI
 * 4.6.8), and the doorbell again if transfers are waiting. A device clears
 * a control endpoint's stall itself at the next SETUP, so no
 * CLEAR_FEATURE is owed (.claude\batch-c-endpoint-steps.md, from the
 * miniport). Thread only, powered.
 */
static VOID hcdResetEp0(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    XHCI_TRB trb;
    KIRQL oldIrql;
    ULONG control;
    ULONG pa;
    ULONG dcs;
    ULONG waiting;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->Ep0Halted = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hc->Ep0Resets++;
    /* EP0's pipe paused for the commands: a URB's doorbell meanwhile would
     * run on the endpoint being repositioned (round 3, finding 2). */
    HcdIoPipePause(hc, &dev->Ep0Pipe);
    if (XhciTrbResetEndpoint(&trb, dev->SlotId, 1, 0) == XHCI_RING_OK) {
        (VOID)hcdCommand(hc, &trb, &control);
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    pa = XhciRingDequeuePA(&dev->Ep0);
    dcs = XhciRingDequeueCycle(&dev->Ep0);
    waiting = dev->Ep0Queue.Count != 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (XhciTrbSetTrDequeue(&trb, dev->SlotId, 1, pa, dcs) ==
            XHCI_RING_OK &&
        hcdCommand(hc, &trb, &control) == XHCI_CC_SUCCESS && waiting) {
        XhciWriteDoorbell(&hc->Hc, dev->SlotId, 1);
    }
    HcdIoPipeResume(hc, &dev->Ep0Pipe);
    XHCI_DBG_VALUE("hcd: EP0 reset after a stall, slot", dev->SlotId);
}

/*
 * The records an unpowered root-hub detach left Abandoned - off their
 * ports, their slots still enabled - get their Disable Slot at the first
 * powered pass, so a restore that kept the slots does not strand them
 * (Codex review of batch (b), round 2, finding 3). A failure quarantines the
 * record as hcdDisableSlot's does. Thread only, powered.
 */
static VOID hcdSweepAbandoned(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;
    ULONG i;

    hc->SlotSweep = 0;
    for (i = 1; i <= XHCI_MAX_SLOTS && !hcdHalted(hc); i++) {
        dev = hc->SlotDevice[i];
        if (dev != NULL && dev->Abandoned) {
            hcdDisableRecord(hc, dev);
        }
    }
}

/* ----------------------------------------------------------------------- */
/* Address Device and Evaluate Context                                      */
/* ----------------------------------------------------------------------- */

static VOID hcdZeroCommon(PXHCI_EXTENSION ext, ULONG offset, ULONG bytes)
{
    volatile ULONG *w;
    ULONG i;

    w = XhciCommonAt(ext, offset);
    for (i = 0; i < bytes / 4UL; i++) {
        w[i] = 0;
    }
}

/* The input context for EP0 at `mps`, with the Slot Context too when
 * `withSlot` (Address Device) and EP0 alone otherwise (Evaluate Context). */
static ULONG hcdBuildEp0Input(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              ULONG mps, ULONG withSlot)
{
    PXHCI_EXTENSION ext;
    PXHCI_HC_LAYOUT layout;
    XHCI_SLOT_PARAMS sp;
    XHCI_EP_PARAMS ep;
    PUCHAR b;
    ULONG i;
    ULONG icc;
    ULONG isc;
    ULONG iep;

    ext = &hc->Hc;
    layout = &ext->Layout;
    if (XhciInputControlContextOffset(layout, &icc) != XHCI_LAYOUT_OK ||
        XhciInputSlotContextOffset(layout, &isc) != XHCI_LAYOUT_OK ||
        XhciInputEndpointContextOffset(layout, 1, &iep) != XHCI_LAYOUT_OK) {
        return 0;
    }
    hcdZeroCommon(ext, layout->InputContextOffset, layout->InputContextBytes);

    if (XhciBuildInputControlContext(
            XhciCommonAt(ext, icc),
            withSlot ? 0x3UL : 0x2UL, 0) != 0) {
        return 0;
    }
    if (withSlot) {
        b = (PUCHAR)&sp;
        for (i = 0; i < sizeof(sp); i++) {
            b[i] = 0;
        }
        sp.RouteString = 0;
        sp.Psiv = dev->Speed;
        sp.RootHubPort = dev->Port;
        sp.ContextEntries = 1;
        if (XhciBuildSlotContext(
                XhciCommonAt(ext, isc),
                &sp) != 0) {
            return 0;
        }
    }
    if (XhciBuildEp0Params(mps, dev->Ep0.BasePA, 1, &ep) != 0) {
        return 0;
    }
    if (XhciBuildEndpointContext(
            XhciCommonAt(ext, iep),
            &ep) != 0) {
        return 0;
    }
    return 1;
}

/* Address Device with BSR = 0 for the slot the port just enabled: the EP0
 * ring at the slot's carved place, the device context in the DCBAA. */
static ULONG hcdAddress(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG mps)
{
    PXHCI_EXTENSION ext;
    PXHCI_HC_LAYOUT layout;
    PHCD_USB_DEVICE dev;
    volatile ULONG *dcbaa;
    ULONG ringOffset;
    ULONG ctxOffset;
    XHCI_TRB trb;
    ULONG control;
    LARGE_INTEGER due;

    ext = &hc->Hc;
    layout = &ext->Layout;
    dev = p->Device;

    if (XhciEp0RingOffset(layout, dev->SlotId, &ringOffset) !=
            XHCI_LAYOUT_OK ||
        XhciDeviceContextOffset(layout, dev->SlotId, &ctxOffset) !=
            XHCI_LAYOUT_OK) {
        return 0;
    }
    hcdZeroCommon(ext, ringOffset, layout->Ep0RingTrbs * XHCI_TRB_BYTES);
    if (XhciRingInit(&dev->Ep0, (volatile XHCI_TRB *)XhciCommonAt(ext,
                                                                  ringOffset),
                     XhciCommonPA(ext, ringOffset), layout->Ep0RingTrbs,
                     XHCI_RING_KIND_ENDPOINT) != XHCI_RING_OK) {
        return 0;
    }

    hcdZeroCommon(ext, ctxOffset, layout->DeviceContextBytes);
    dcbaa = XhciCommonAt(ext, layout->DcbaaOffset + dev->SlotId * 8UL);
    dcbaa[0] = XhciCommonPA(ext, ctxOffset);
    dcbaa[1] = 0;

    if (!hcdBuildEp0Input(hc, dev, mps, 1)) {
        return 0;
    }
    if (XhciTrbAddressDevice(&trb, dev->SlotId,
                             XhciCommonPA(ext, layout->InputContextOffset),
                             0) != XHCI_RING_OK) {
        return 0;
    }
    if (hcdCommand(hc, &trb, &control) != XHCI_CC_SUCCESS) {
        return 0;
    }
    dev->Mps0 = mps;
    HcdRelativeMs(&due, HCD_SETADDRESS_MS);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    return 1;
}

static ULONG hcdEvaluate(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG mps)
{
    PXHCI_EXTENSION ext;
    XHCI_TRB trb;
    ULONG control;

    ext = &hc->Hc;
    if (!hcdBuildEp0Input(hc, p->Device, mps, 0)) {
        return 0;
    }
    if (XhciTrbEvaluateContext(&trb, p->Device->SlotId,
                               XhciCommonPA(ext,
                                            ext->Layout.InputContextOffset)) !=
        XHCI_RING_OK) {
        return 0;
    }
    if (hcdCommand(hc, &trb, &control) != XHCI_CC_SUCCESS) {
        return 0;
    }
    p->Device->Mps0 = mps;
    return 1;
}

/* ----------------------------------------------------------------------- */
/* EP0 control transfers                                                    */
/* ----------------------------------------------------------------------- */

/*
 * One IN control transfer of `length` bytes into the scratch buffer,
 * through the transfer engine (xhci_xfer.c) on the device's EP0 queue: the
 * engine builds the TD group, publishes it whole or not at all, matches its
 * events by TRB address, and latches the bytes moved and the USBD status
 * (26-A.5's first step: the enumeration and the URB path share one ring and
 * one queue). `requestType` is the SETUP bmRequestType: a nonzero `length`
 * is an IN data stage into the scratch (the thread sends no OUT data), a
 * zero `length` no data stage at all (SET_CONFIGURATION, CLEAR_FEATURE).
 * Returns 1 with *bytes the count received, 0 on any failure. Thread only.
 */
ULONG HcdThreadControl(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                       UCHAR requestType, UCHAR request, USHORT value,
                       USHORT index, ULONG length, PULONG bytes)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_TRB trbs[XHCI_XFER_MAX_CONTROL_TRBS];
    USBPORT_SCATTER_GATHER_LIST sg;
    KIRQL oldIrql;
    PUCHAR b;
    ULONG answer;
    ULONG done;
    ULONG i;

    *bytes = 0;
    if (dev->Ep0Stuck) {
        /* Its record is still queued from a timeout: reusing it would
         * relink the engine's queue (round 2, finding 10). */
        return 0;
    }
    if (length > HCD_SCRATCH_BYTES ||
        (length != 0 && (requestType & 0x80) == 0)) {
        return 0;
    }

    b = (PUCHAR)&sg;
    for (i = 0; i < sizeof(sg); i++) {
        b[i] = 0;
    }
    sg.SgElementCount = (length != 0) ? 1 : 0;
    sg.SgElement[0].SgPhysicalAddressLo = hc->ScratchPa.LowPart;
    sg.SgElement[0].SgTransferLength = length;
    sg.SgElement[0].SgOffset = 0;

    b = (PUCHAR)&req;
    for (i = 0; i < sizeof(req); i++) {
        b[i] = 0;
    }
    req.Setup.bmRequestType = requestType;
    req.Setup.bRequest = request;
    req.Setup.wValue = value;
    req.Setup.wIndex = index;
    req.Setup.wLength = (USHORT)length;
    req.TransferLength = length;
    req.TransferFlagsIn = (requestType & 0x80) != 0;
    req.MaxPacketSize = dev->Mps0;
    req.SgList = &sg;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    dev->Ep0Done = 0;
    KeClearEvent(&hc->XferDoneEvent);
    answer = XhciXferSubmitControl(&dev->Ep0Queue, &dev->Ep0, &req,
                                   &dev->Ep0Xfer, dev, trbs,
                                   XHCI_XFER_MAX_CONTROL_TRBS);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (answer != XHCI_XFER_OK) {
        return 0;
    }
    XhciWriteDoorbell(&hc->Hc, dev->SlotId, 1);

    done = hcdWaitEvent(&hc->XferDoneEvent, HCD_TRANSFER_WAIT_MS) &&
           dev->Ep0Done;
    if (!done) {
        /* The TD is still on the ring and may yet DMA into the scratch:
         * nothing reuses it until the recovery's HCRST has taken every slot
         * (Codex review of batch (b), round 1, finding 8). The record stays
         * on the queue; the invalidation frees the device with it. */
        hc->EnumTransfersTimedOut++;
        hc->ScratchTainted = 1;
        dev->Ep0Stuck = 1;
        HcdSvcRequestReset(&hc->Hc);
        return 0;
    }
    if (dev->Ep0Xfer.UsbdStatus != XHCI_USBD_STATUS_SUCCESS) {
        return 0;
    }
    *bytes = dev->Ep0Xfer.BytesTransferred;
    return 1;
}

static ULONG hcdGetDescriptor(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              UCHAR type, ULONG length, PULONG bytes)
{
    return HcdThreadControl(hc, dev, 0x80, 6, (USHORT)((ULONG)type << 8), 0, length,
                        bytes);
}

/* ----------------------------------------------------------------------- */
/* Root ports                                                               */
/* ----------------------------------------------------------------------- */

/* Reset a USB 2.0 root port and wait for the xHC to finish it; the speed is
 * read from PORTSC once it has. Returns 1 when the port came back enabled. */
static ULONG hcdResetPort(PHCD_CONTROLLER hc, ULONG port, PULONG speed)
{
    PXHCI_EXTENSION ext;
    LARGE_INTEGER due;
    ULONG portsc;
    ULONG waited;

    ext = &hc->Hc;
    portsc = XhciReadPortsc(ext, port);
    if (portsc == 0xFFFFFFFFUL || (portsc & XHCI_PORTSC_CCS) == 0) {
        return 0;
    }
    XhciWritePortsc(ext, port, XhciPortscReset(portsc));
    for (waited = 0; waited < HCD_RESET_WAIT_MS; waited += HCD_POLL_STEP_MS) {
        HcdRelativeMs(&due, HCD_POLL_STEP_MS);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        portsc = XhciReadPortsc(ext, port);
        if (portsc == 0xFFFFFFFFUL) {
            return 0;
        }
        if ((portsc & XHCI_PORTSC_PRC) != 0) {
            break;
        }
    }
    XhciWritePortsc(ext, port,
                    XhciPortscClearChanges(portsc, XHCI_PORTSC_PRC));
    if ((portsc & XHCI_PORTSC_PRC) == 0 || (portsc & XHCI_PORTSC_PED) == 0) {
        return 0;
    }
    *speed = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
    HcdRelativeMs(&due, HCD_RESET_RECOVERY_MS);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    return 1;
}

static ULONG hcdPortConnected(PHCD_CONTROLLER hc, ULONG port)
{
    ULONG portsc;

    portsc = XhciReadPortsc(&hc->Hc, port);
    return portsc != 0xFFFFFFFFUL && (portsc & XHCI_PORTSC_CCS) != 0;
}

/* ----------------------------------------------------------------------- */
/* The executor                                                             */
/* ----------------------------------------------------------------------- */

static VOID hcdEventInit(PXHCI_ENUM_EVENT e, ULONG kind, ULONG ok)
{
    e->Kind = kind;
    e->Ok = ok;
    e->Speed = 0;
    e->SlotId = 0;
    e->Bytes = 0;
    e->Value = 0;
}

/*
 * Carry out one action and describe its outcome as the next event.
 * Returns 0 when the action has no outcome to feed back.
 */
static ULONG hcdPerform(PHCD_CONTROLLER hc, PHCD_PORT p,
                        const XHCI_ENUM_ACTION *act, PXHCI_ENUM_EVENT next)
{
    LARGE_INTEGER due;
    XHCI_TRB trb;
    ULONG control;
    ULONG code;
    ULONG speed;
    ULONG bytes;
    ULONG ok;
    PUCHAR s;

    s = (PUCHAR)hc->ScratchVa;
    switch (act->Kind) {
    case XHCI_ENUM_ACT_DEBOUNCE:
        HcdRelativeMs(&due, HCD_DEBOUNCE_MS);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
        hcdEventInit(next, XHCI_ENUM_EV_DEBOUNCED,
                     hcdPortConnected(hc, p->PortId));
        return 1;

    case XHCI_ENUM_ACT_RESET:
        speed = 0;
        ok = hcdResetPort(hc, p->PortId, &speed);
        hcdEventInit(next, XHCI_ENUM_EV_RESET_DONE, ok);
        next->Speed = speed;
        XHCI_DBG_VALUE("hcd: port reset, port/ok/speed",
                       (p->PortId << 16) | (ok << 8) | speed);
        return 1;

    case XHCI_ENUM_ACT_ENABLE_SLOT:
        hcdEventInit(next, XHCI_ENUM_EV_COMMAND_DONE, 0);
        if (XhciTrbEnableSlot(&trb, 0) != XHCI_RING_OK) {
            return 1;
        }
        code = hcdCommand(hc, &trb, &control);
        if (code == XHCI_CC_SUCCESS) {
            next->SlotId = XHCI_TRB_GET_SLOT_ID(control);
            if (next->SlotId == 0 || next->SlotId > XHCI_MAX_SLOTS ||
                hc->SlotDevice[next->SlotId] != NULL) {
                /* A slot id no record can take, or one a quarantined
                 * record still holds: the controller and this driver
                 * disagree, which only its reset settles. */
                next->SlotId = 0;
                HcdSvcRequestReset(&hc->Hc);
                return 1;
            }
            if (hcdDeviceNew(hc, p->PortId, next->SlotId, p->Enum.Speed) ==
                NULL) {
                /* The slot is enabled but unowned: give it back here, the
                 * machine never learns of it (round 1, finding 11). */
                (VOID)hcdDisableSlotId(hc, next->SlotId);
                next->SlotId = 0;
                return 1;
            }
            p->Device = hc->SlotDevice[next->SlotId];
            next->Ok = 1;
        }
        XHCI_DBG_VALUE("hcd: enable slot, code/slot", (code << 8) |
                                                          next->SlotId);
        return 1;

    case XHCI_ENUM_ACT_ADDRESS:
        ok = hcdAddress(hc, p, act->Mps0);
        hcdEventInit(next, XHCI_ENUM_EV_COMMAND_DONE, ok);
        XHCI_DBG_VALUE("hcd: address device, ok", ok);
        return 1;

    case XHCI_ENUM_ACT_EVALUATE:
        ok = hcdEvaluate(hc, p, act->Mps0);
        hcdEventInit(next, XHCI_ENUM_EV_COMMAND_DONE, ok);
        return 1;

    case XHCI_ENUM_ACT_GET_DEVICE:
        bytes = 0;
        ok = hcdGetDescriptor(hc, p->Device, HCD_DESC_DEVICE, act->Length,
                              &bytes);
        hcdEventInit(next, XHCI_ENUM_EV_TRANSFER_DONE, ok);
        next->Bytes = bytes;
        if (ok && bytes >= 8) {
            next->Value = s[7];
        }
        if (ok && bytes == XHCI_ENUM_DEVICE_DESC_BYTES) {
            hcdCopy(p->Device->DeviceDesc, s,
                         XHCI_ENUM_DEVICE_DESC_BYTES);
            XHCI_DBG_VALUE("hcd: device descriptor, idVendor/idProduct",
                           ((ULONG)s[9] << 24) | ((ULONG)s[8] << 16) |
                               ((ULONG)s[11] << 8) | (ULONG)s[10]);
        }
        return 1;

    case XHCI_ENUM_ACT_GET_CONFIG:
        bytes = 0;
        ok = hcdGetDescriptor(hc, p->Device, HCD_DESC_CONFIGURATION,
                              act->Length, &bytes);
        hcdEventInit(next, XHCI_ENUM_EV_TRANSFER_DONE, ok);
        next->Bytes = bytes;
        if (ok && act->Length == XHCI_ENUM_CONFIG_HEAD_BYTES && bytes >= 4) {
            next->Value = (ULONG)s[2] | ((ULONG)s[3] << 8);
        }
        if (ok && act->Length != XHCI_ENUM_CONFIG_HEAD_BYTES &&
            bytes == act->Length) {
            p->Device->Config = (PUCHAR)HcdPoolAlloc(bytes);
            if (p->Device->Config == NULL) {
                next->Ok = 0;
                return 1;
            }
            hcdCopy(p->Device->Config, s, bytes);
            p->Device->ConfigLength = bytes;
            XHCI_DBG_VALUE("hcd: configuration descriptor, bytes", bytes);
        }
        return 1;

    case XHCI_ENUM_ACT_CREATE_PDO:
        hcdEventInit(next, XHCI_ENUM_EV_PDO_CREATED,
                     NT_SUCCESS(HcdDevicePdoCreate(hc, p->Device)));
        XHCI_DBG_VALUE("hcd: device enumerated on port", p->PortId);
        return 1;

    case XHCI_ENUM_ACT_DISABLE_SLOT:
        hcdDisableSlot(hc, p);
        return 0;

    case XHCI_ENUM_ACT_REPORT_GONE:
        /* The machine waits in Gone for that PDO's deletion (AwaitSerial,
         * checked when PortPdoRemoved says to look) unless it went at
         * once. */
        p->AwaitSerial = HcdDevicePdoGone(hc, p->Device);
        hcdDisableSlot(hc, p);
        if (p->AwaitSerial != 0) {
            return 0;
        }
        hcdEventInit(next, XHCI_ENUM_EV_PDO_REMOVED, 1);
        return 1;

    default:
        return 0;
    }
}

/* Run the machine from one event until it asks for nothing, retrying once
 * from Reset after a failure while the port still reads connected. A halted
 * controller (hcdHalted) ends the run where it stands; the invalidation that
 * follows the recovery settles the port from any state. */
static VOID hcdRun(PHCD_CONTROLLER hc, PHCD_PORT p, XHCI_ENUM_EVENT event)
{
    XHCI_ENUM_ACTION act;
    ULONG guard;

    for (guard = 0; guard < 64; guard++) {
        /* An outcome is always fed back before the run can stop: a halt is
         * looked for only between the machine's step and the next action,
         * so a PDO just created is recorded (PdoExists) and the
         * invalidation that follows takes the port to Gone, not Empty
         * (Codex review of batch (b), round 3, finding 1). */
        (VOID)XhciEnumStep(&p->Enum, &event, &act);
        if (act.Kind != XHCI_ENUM_ACT_NONE && hcdHalted(hc)) {
            break;
        }
        if (act.Kind == XHCI_ENUM_ACT_NONE ||
            !hcdPerform(hc, p, &act, &event)) {
            if (p->Enum.State != XHCI_ENUM_FAILED || hcdHalted(hc) ||
                !hcdPortConnected(hc, p->PortId)) {
                break;
            }
            (VOID)XhciEnumRetry(&p->Enum, &act);
            if (act.Kind == XHCI_ENUM_ACT_NONE) {
                XHCI_DBG_VALUE("hcd: enumeration failed, port/cause",
                               (p->PortId << 16) | p->Enum.FailCause);
                break;
            }
            if (!hcdPerform(hc, p, &act, &event)) {
                break;
            }
        }
    }
}

static VOID hcdFeed(PHCD_CONTROLLER hc, PHCD_PORT p, ULONG kind)
{
    XHCI_ENUM_EVENT event;

    hcdEventInit(&event, kind, 1);
    hcdRun(hc, p, event);
}

/*
 * One port marked changed: read it, acknowledge the change bits this read
 * saw - only those, so a change arriving after the read stays for the next
 * pass (Codex review of batch (b), round 1, finding 10) - and feed its
 * machine by the connection change, not by the connection state alone
 * (finding 9):
 *
 *   CSC set     the device left, or left and came back: whatever the port
 *               held goes (DISCONNECT), and a connection now is a new device
 *               (CONNECT);
 *   CSC clear   a change of another kind (the reset's own PRC among them):
 *               a port that reads disconnected goes, and an Empty one that
 *               reads connected starts - a Failed port waits for a new
 *               connection rather than retrying for ever.
 */
static VOID hcdPortChanged(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PXHCI_EXTENSION ext;
    ULONG portsc;
    ULONG changes;
    ULONG state;

    ext = &hc->Hc;
    portsc = XhciReadPortsc(ext, p->PortId);
    if (portsc == 0xFFFFFFFFUL) {
        return;
    }
    changes = portsc & XHCI_PORTSC_CHANGE_MASK;
    if (changes != 0) {
        XhciWritePortsc(ext, p->PortId,
                        XhciPortscClearChanges(portsc, changes));
    }
    state = p->Enum.State;
    if ((changes & XHCI_PORTSC_CSC) != 0) {
        if (state != XHCI_ENUM_EMPTY && state != XHCI_ENUM_FAILED &&
            state != XHCI_ENUM_GONE) {
            hcdFeed(hc, p, XHCI_ENUM_EV_DISCONNECT);
        }
        if ((portsc & XHCI_PORTSC_CCS) != 0 && !hcdHalted(hc)) {
            hcdFeed(hc, p, XHCI_ENUM_EV_CONNECT);
        }
    } else if ((portsc & XHCI_PORTSC_CCS) == 0) {
        hcdFeed(hc, p, XHCI_ENUM_EV_DISCONNECT);
    } else if (state == XHCI_ENUM_EMPTY) {
        hcdFeed(hc, p, XHCI_ENUM_EV_CONNECT);
    }
}

/*
 * HCRST took every slot (recovery, a resume that reinitialised): drop every
 * record without a command - the quarantined ones in SlotDevice[] with the
 * ports' - take each port's PDO off the bus, and put each machine where a
 * disconnect would, with the slot already gone. A port whose PDO PnP still
 * holds waits in Gone for its removal; the rescan that follows finds what is
 * connected now (Codex review of batch (b), round 1, finding 6).
 */
/*
 * A port in Gone leaves it only once the PDO it waits for is deleted - that
 * PDO, by serial, not whichever PDO last reported its port (Codex review of
 * batch (b), round 2, finding 2). Pure transitions, no action run.
 */
static VOID hcdSettleGone(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    XHCI_ENUM_EVENT event;
    XHCI_ENUM_ACTION act;

    if (p->Enum.State != XHCI_ENUM_GONE ||
        HcdDevicePdoExists(hc, p->AwaitSerial)) {
        return;
    }
    p->AwaitSerial = 0;
    hcdEventInit(&event, XHCI_ENUM_EV_PDO_REMOVED, 1);
    (VOID)XhciEnumStep(&p->Enum, &event, &act);
}

/* A port whose slot HCRST took: its record goes without a command, its PDO
 * off the bus, and its machine where a disconnect would put it - waiting in
 * Gone, by serial, for a PDO PnP still holds. */
static VOID hcdDropPort(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    XHCI_ENUM_EVENT event;
    XHCI_ENUM_ACTION act;

    if (p->Device != NULL) {
        p->AwaitSerial = HcdDevicePdoGone(hc, p->Device);
        hcdDeviceFree(hc, p->Device);
        p->Device = NULL;
    }
    hcdEventInit(&event, XHCI_ENUM_EV_DISCONNECT, 1);
    (VOID)XhciEnumStep(&p->Enum, &event, &act);
    hcdSettleGone(hc, p);
}

static VOID hcdInvalidate(PHCD_CONTROLLER hc)
{
    ULONG i;

    XHCI_DBG_TEXT("hcd: slots invalidated, dropping every device");
    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        hcdDropPort(hc, &hc->Ports[i]);
    }
    for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
        hcdDeviceFree(hc, hc->SlotDevice[i]);
    }
    hc->ScratchTainted = 0;
    hc->SlotSweep = 0;
}

/*
 * The root hub is going (HcdEnumDetach): every device leaves the bus and
 * every machine starts again from Empty, so a root hub started again
 * enumerates afresh (Codex review of batch (b), round 1, finding 1). With
 * the controller unpowered no command can run: the records are left
 * Abandoned, and the first powered pass disables their slots
 * (hcdSweepAbandoned; round 2, finding 3) unless an invalidation has
 * dropped them first.
 */
static VOID hcdDetach(PHCD_CONTROLLER hc, ULONG powered)
{
    PHCD_PORT p;
    KIRQL oldIrql;
    ULONG i;

    XHCI_DBG_TEXT("hcd: root hub detaching, dropping every device");
    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        p = &hc->Ports[i];
        if (p->Device != NULL) {
            (VOID)HcdDevicePdoGone(hc, p->Device);
            if (powered && !hcdHalted(hc)) {
                hcdDisableSlot(hc, p);
            } else {
                p->Device->Abandoned = 1;
                p->Device = NULL;
                hc->SlotSweep = 1;
            }
        }
        p->AwaitSerial = 0;
        XhciEnumReset(&p->Enum);
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    for (i = 0; i < HCD_PORT_WORDS; i++) {
        hc->PortPdoStarted[i] = 0;
        hc->PortPdoRemoved[i] = 0;
    }
    hc->EnumDetachRequested = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    (VOID)KeSetEvent(&hc->EnumDetachDone, IO_NO_INCREMENT, FALSE);
}

/*
 * The thread's port service, under the power gate; `powered` is the
 * controller's state read under it (Codex review of batch (b), round 1,
 * finding 5). In order: an invalidation HCRST left, a detach the root hub
 * asked for, the abandoned slots' sweep, the PDO handshake (design record 13 section 5.3: PDO_REMOVED
 * takes a Gone port to Empty and a connected one on to a new enumeration,
 * PDO_STARTED takes Present to Bound), then every port marked changed.
 * Unpowered, only a detach runs; the rest stays marked. Only the controller
 * thread calls it.
 */
VOID HcdEnumService(PHCD_CONTROLLER hc, ULONG powered)
{
    PXHCI_EXTENSION ext;
    ULONG changed[HCD_PORT_WORDS];
    ULONG started[HCD_PORT_WORDS];
    ULONG removed[HCD_PORT_WORDS];
    KIRQL oldIrql;
    ULONG invalidated;
    ULONG detach;
    ULONG bit;
    ULONG port;
    ULONG i;
    PHCD_PORT p;

    ext = &hc->Hc;
    XhciControllerLockAcquire(ext, &oldIrql);
    detach = hc->EnumDetachRequested;
    invalidated = 0;
    if (powered) {
        invalidated = hc->SlotsInvalidated;
        hc->SlotsInvalidated = 0;
    }
    XhciControllerLockRelease(ext, oldIrql);

    /* An invalidation first: what HCRST took needs no Disable Slot, which a
     * detach after it would otherwise send and, refused, turn into another
     * reset (round 3, finding 2). */
    if (invalidated) {
        hcdInvalidate(hc);
    }
    if (detach) {
        hcdDetach(hc, powered);
    }
    if (!powered) {
        return;
    }
    if (hc->SlotSweep && !hcdHalted(hc)) {
        hcdSweepAbandoned(hc);
    }
    for (i = 1; i <= XHCI_MAX_SLOTS && !hcdHalted(hc); i++) {
        if (hc->SlotDevice[i] != NULL && hc->SlotDevice[i]->Ep0Halted) {
            hcdResetEp0(hc, hc->SlotDevice[i]);
        }
    }
    if (!hcdHalted(hc)) {
        HcdCfgCancelService(hc);
        /* The URBs that need commands (hcd_cfg.c). */
        HcdCfgService(hc);
    }

    XhciControllerLockAcquire(ext, &oldIrql);
    for (i = 0; i < HCD_PORT_WORDS; i++) {
        changed[i] = hc->PortChange[i];
        started[i] = hc->PortPdoStarted[i];
        removed[i] = hc->PortPdoRemoved[i];
        hc->PortChange[i] = 0;
        hc->PortPdoStarted[i] = 0;
        hc->PortPdoRemoved[i] = 0;
        if (invalidated) {
            changed[i] = 0xFFFFFFFFUL;
        }
    }
    XhciControllerLockRelease(ext, oldIrql);

    for (port = 1; port <= XHCI_MAX_ROOT_PORTS; port++) {
        bit = 1UL << ((port - 1) % 32UL);
        p = &hc->Ports[port - 1];
        if ((removed[(port - 1) / 32UL] & bit) != 0 &&
            p->Enum.State == XHCI_ENUM_GONE) {
            /* The bit says when to look; the serial says whether the PDO
             * this port waits for is the one that went. */
            hcdSettleGone(hc, p);
            changed[(port - 1) / 32UL] |= bit;
        }
        if ((started[(port - 1) / 32UL] & bit) != 0) {
            hcdFeed(hc, p, XHCI_ENUM_EV_PDO_STARTED);
        }
    }

    if (!hc->RootHubStarted || hc->ScratchVa == NULL || hcdHalted(hc)) {
        /* Nothing is enumerated without a started root hub to report to;
         * its start marks every port again. A halted controller's recovery
         * does the same through the invalidation. */
        return;
    }
    for (port = 1; port <= ext->PortMap.PortCount &&
                   port <= XHCI_MAX_ROOT_PORTS; port++) {
        if ((changed[(port - 1) / 32UL] & (1UL << ((port - 1) % 32UL))) ==
                0 ||
            !XhciPortIsManaged(&ext->PortMap, port)) {
            continue;
        }
        hcdPortChanged(hc, &hc->Ports[port - 1]);
        if (hcdHalted(hc)) {
            break;
        }
    }
}

/*
 * The root hub's removal (hcd_rh.c): no PDO is created from here on, and the
 * thread drops every device and resets every machine before this returns, so
 * the caller can release the PDOs with nothing listing more. Without a
 * running thread the stop has dropped them already (HcdEnumDrop).
 * IRQL: PASSIVE_LEVEL.
 */
VOID HcdEnumDetach(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG wait;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->RootHubStarted = 0;
    wait = hc->ThreadRunning;
    if (wait) {
        hc->EnumDetachRequested = 1;
        KeClearEvent(&hc->EnumDetachDone);
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (!wait) {
        return;
    }
    HcdThreadWake(hc);
    (VOID)KeWaitForSingleObject(&hc->EnumDetachDone, Executive, KernelMode,
                                FALSE, NULL);
}

/*
 * The root hub's start (hcd_rh.c): PDOs may be created, and every port is
 * looked at. IRQL: PASSIVE_LEVEL.
 */
VOID HcdEnumAttach(PHCD_CONTROLLER hc)
{
    KIRQL oldIrql;
    ULONG i;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    hc->RootHubStarted = 1;
    for (i = 0; i < HCD_PORT_WORDS; i++) {
        hc->PortChange[i] = 0xFFFFFFFFUL;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdThreadWake(hc);
}

/* At a start: every port empty, every port numbered, no request pending -
 * except a port still waiting in Gone for a PDO PnP holds across the
 * controller's stop, which keeps waiting (round 2, finding 2). RootHubStarted
 * is kept: a root hub started before a controller restart is still started.
 * IRQL: PASSIVE_LEVEL, the thread not yet running. */
VOID HcdEnumInit(PHCD_CONTROLLER hc)
{
    PHCD_PORT p;
    ULONG i;

    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        p = &hc->Ports[i];
        p->PortId = i + 1;
        p->Device = NULL;
        if (p->Enum.State == XHCI_ENUM_GONE &&
            HcdDevicePdoExists(hc, p->AwaitSerial)) {
            continue;
        }
        XhciEnumReset(&p->Enum);
        p->AwaitSerial = 0;
    }
    for (i = 0; i <= XHCI_MAX_SLOTS; i++) {
        hc->SlotDevice[i] = NULL;
    }
    for (i = 0; i < HCD_PORT_WORDS; i++) {
        hc->PortChange[i] = 0xFFFFFFFFUL;
        hc->PortPdoStarted[i] = 0;
        hc->PortPdoRemoved[i] = 0;
    }
    hc->SlotsInvalidated = 0;
    hc->EnumDetachRequested = 0;
    hc->ScratchTainted = 0;
    (VOID)KeSetEvent(&hc->EnumDetachDone, IO_NO_INCREMENT, FALSE);
}

/* At a stop: the HCRST that follows takes every slot, so the records go
 * without commands, the quarantined and abandoned ones with the rest, and
 * each port is dropped as an invalidation drops it - a port whose PDO PnP
 * still holds waits for it in Gone (round 2, finding 2). IRQL:
 * PASSIVE_LEVEL, the thread stopped. */
VOID HcdEnumDrop(PHCD_CONTROLLER hc)
{
    ULONG i;

    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        hcdDropPort(hc, &hc->Ports[i]);
    }
    for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
        hcdDeviceFree(hc, hc->SlotDevice[i]);
    }
    hc->SlotSweep = 0;
}
