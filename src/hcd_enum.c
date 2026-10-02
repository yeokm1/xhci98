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

/* Submit one command and wait for its completion. Returns the completion
 * code, or 0 when it never completed (lost, refused or timed out); *control
 * receives the Command Completion Event's DW3. */
static ULONG hcdCommand(PHCD_CONTROLLER hc, const XHCI_TRB *trb,
                        PULONG control)
{
    ULONG trbPA;
    ULONG answer;

    *control = 0;
    KeClearEvent(&hc->CmdDoneEvent);
    hc->CmdDoneLost = 0;
    answer = XhciCommandSubmit(&hc->Hc, trb, &trbPA, XHCI_ARM_UNLOCKED);
    if (answer != XHCI_CMD_OK) {
        hc->EnumCommandsRefused++;
        return 0;
    }
    if (!hcdWaitEvent(&hc->CmdDoneEvent, HCD_COMMAND_WAIT_MS)) {
        hc->EnumCommandsTimedOut++;
        return 0;
    }
    if (hc->CmdDoneLost) {
        return 0;
    }
    *control = hc->CmdDoneControl;
    return hc->CmdDoneCode;
}

/* ----------------------------------------------------------------------- */
/* Device records                                                           */
/* ----------------------------------------------------------------------- */

static PHCD_USB_DEVICE hcdDeviceNew(PHCD_CONTROLLER hc, ULONG port,
                                    ULONG slotId, ULONG speed)
{
    PHCD_USB_DEVICE dev;
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
    dev->Port = port;
    dev->SlotId = slotId;
    dev->Speed = speed;
    hc->SlotDevice[slotId] = dev;
    return dev;
}

static VOID hcdDeviceFree(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    if (dev == NULL) {
        return;
    }
    if (dev->SlotId != 0 && dev->SlotId <= XHCI_MAX_SLOTS) {
        hc->SlotDevice[dev->SlotId] = NULL;
    }
    HcdPoolFree(dev->Config);
    HcdPoolFree(dev);
}

/* Give a slot back: Disable Slot, its DCBAA entry cleared, its record freed.
 * The command's outcome is counted, not acted on - after a disconnect or a
 * failed step there is nothing better to do with the slot. */
static VOID hcdDisableSlot(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PXHCI_EXTENSION ext;
    XHCI_TRB trb;
    ULONG control;
    ULONG slotId;
    volatile ULONG *dcbaa;

    ext = &hc->Hc;
    if (p->Device == NULL) {
        return;
    }
    slotId = p->Device->SlotId;
    if (XhciTrbDisableSlot(&trb, slotId) == XHCI_RING_OK) {
        if (hcdCommand(hc, &trb, &control) != XHCI_CC_SUCCESS) {
            hc->EnumDisableFailures++;
        }
    }
    dcbaa = XhciCommonAt(ext, ext->Layout.DcbaaOffset + slotId * 8UL);
    dcbaa[0] = 0;
    dcbaa[1] = 0;
    hcdDeviceFree(hc, p->Device);
    p->Device = NULL;
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

/* One IN control transfer of `length` bytes into the scratch buffer.
 * Returns 1 with *bytes the count received, 0 on any failure. */
static ULONG hcdControlIn(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                          UCHAR request, USHORT value, USHORT index,
                          ULONG length, PULONG bytes)
{
    XHCI_CONTROL_REQUEST req;
    XHCI_CONTROL_LAYOUT layout;
    XHCI_TD_GROUP_PLACEMENT placement;
    XHCI_TRB trbs[XHCI_XFER_MAX_CONTROL_TRBS];
    USBPORT_SCATTER_GATHER_LIST sg;
    PUCHAR b;
    ULONG i;

    *bytes = 0;
    if (length == 0 || length > HCD_SCRATCH_BYTES) {
        return 0;
    }

    b = (PUCHAR)&sg;
    for (i = 0; i < sizeof(sg); i++) {
        b[i] = 0;
    }
    sg.SgElementCount = 1;
    sg.SgElement[0].SgPhysicalAddressLo = hc->ScratchPa.LowPart;
    sg.SgElement[0].SgTransferLength = length;
    sg.SgElement[0].SgOffset = 0;

    b = (PUCHAR)&req;
    for (i = 0; i < sizeof(req); i++) {
        b[i] = 0;
    }
    req.Setup.bmRequestType = 0x80;
    req.Setup.bRequest = request;
    req.Setup.wValue = value;
    req.Setup.wIndex = index;
    req.Setup.wLength = (USHORT)length;
    req.TransferLength = length;
    req.TransferFlagsIn = 1;
    req.MaxPacketSize = dev->Mps0;
    req.SgList = &sg;

    if (XhciXferBuildControl(&req, trbs, XHCI_XFER_MAX_CONTROL_TRBS,
                             &layout) != XHCI_XFER_OK) {
        return 0;
    }

    KeClearEvent(&hc->XferDoneEvent);
    hc->XferWaitSlot = dev->SlotId;
    hc->XferResidual = 0;
    hc->XferCode = 0;
    if (XhciRingEnqueueTdGroup(&dev->Ep0, trbs, layout.TrbCount,
                               layout.TdLengths, layout.TdCount,
                               &placement) != XHCI_RING_OK) {
        hc->XferWaitSlot = 0;
        return 0;
    }
    XhciWriteDoorbell(&hc->Hc, dev->SlotId, 1);

    if (!hcdWaitEvent(&hc->XferDoneEvent, HCD_TRANSFER_WAIT_MS)) {
        hc->XferWaitSlot = 0;
        hc->EnumTransfersTimedOut++;
        return 0;
    }
    hc->XferWaitSlot = 0;
    /* The whole group has been consumed once its last TD completed. */
    dev->Ep0.Dequeue = dev->Ep0.Enqueue;
    if (hc->XferCode != XHCI_CC_SUCCESS) {
        return 0;
    }
    *bytes = (hc->XferResidual > length) ? 0 : length - hc->XferResidual;
    return 1;
}

static ULONG hcdGetDescriptor(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                              UCHAR type, ULONG length, PULONG bytes)
{
    return hcdControlIn(hc, dev, 6, (USHORT)((ULONG)type << 8), 0, length,
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
                hcdDeviceNew(hc, p->PortId, next->SlotId, p->Enum.Speed) ==
                    NULL) {
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
        /* The device PDO is the next step of 26-A.4; until it exists the
         * device is recorded as enumerated and the machine told so. */
        hcdEventInit(next, XHCI_ENUM_EV_PDO_CREATED, 1);
        XHCI_DBG_VALUE("hcd: device enumerated on port", p->PortId);
        return 1;

    case XHCI_ENUM_ACT_DISABLE_SLOT:
        hcdDisableSlot(hc, p);
        return 0;

    case XHCI_ENUM_ACT_REPORT_GONE:
        hcdDisableSlot(hc, p);
        hcdEventInit(next, XHCI_ENUM_EV_PDO_REMOVED, 1);
        return 1;

    default:
        return 0;
    }
}

/* Run the machine from one event until it asks for nothing, retrying once
 * from Reset after a failure while the port still reads connected. */
static VOID hcdRun(PHCD_CONTROLLER hc, PHCD_PORT p, XHCI_ENUM_EVENT event)
{
    XHCI_ENUM_ACTION act;
    ULONG guard;

    for (guard = 0; guard < 64; guard++) {
        (VOID)XhciEnumStep(&p->Enum, &event, &act);
        if (act.Kind == XHCI_ENUM_ACT_NONE ||
            !hcdPerform(hc, p, &act, &event)) {
            if (p->Enum.State != XHCI_ENUM_FAILED ||
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

/* One port marked changed: read it, acknowledge the change bits, and feed
 * its machine. */
static VOID hcdPortChanged(PHCD_CONTROLLER hc, PHCD_PORT p)
{
    PXHCI_EXTENSION ext;
    XHCI_ENUM_EVENT event;
    ULONG portsc;

    ext = &hc->Hc;
    portsc = XhciReadPortsc(ext, p->PortId);
    if (portsc == 0xFFFFFFFFUL) {
        return;
    }
    if ((portsc & XHCI_PORTSC_CHANGE_MASK) != 0) {
        XhciWritePortsc(ext, p->PortId,
                        XhciPortscClearChanges(portsc,
                                               XHCI_PORTSC_CHANGE_MASK));
    }
    hcdEventInit(&event,
                 (portsc & XHCI_PORTSC_CCS) ? XHCI_ENUM_EV_CONNECT
                                            : XHCI_ENUM_EV_DISCONNECT,
                 1);
    hcdRun(hc, p, event);
}

/* The thread's port service: every port the event DPC (or the controller's
 * start) marked. Only the controller thread calls it. */
VOID HcdEnumService(PHCD_CONTROLLER hc)
{
    PXHCI_EXTENSION ext;
    ULONG mask;
    ULONG port;

    ext = &hc->Hc;
    mask = (ULONG)InterlockedExchange((PLONG)&hc->PortChangeMask, 0);
    if (mask == 0 || hc->ScratchVa == NULL) {
        return;
    }
    for (port = 1; port <= ext->PortMap.PortCount && port <= 32; port++) {
        if ((mask & (1UL << (port - 1))) == 0 ||
            !XhciPortIsManaged(&ext->PortMap, port)) {
            continue;
        }
        hcdPortChanged(hc, &hc->Ports[port - 1]);
    }
}

/* At a start: every port empty, every port numbered. IRQL: PASSIVE_LEVEL. */
VOID HcdEnumInit(PHCD_CONTROLLER hc)
{
    ULONG i;

    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        XhciEnumReset(&hc->Ports[i].Enum);
        hc->Ports[i].PortId = i + 1;
        hc->Ports[i].Device = NULL;
    }
    for (i = 0; i <= XHCI_MAX_SLOTS; i++) {
        hc->SlotDevice[i] = NULL;
    }
}

/* At a stop: the HCRST that follows takes every slot, so the records go
 * without commands. IRQL: PASSIVE_LEVEL, the thread stopped. */
VOID HcdEnumDrop(PHCD_CONTROLLER hc)
{
    ULONG i;

    for (i = 0; i < XHCI_MAX_ROOT_PORTS; i++) {
        hcdDeviceFree(hc, hc->Ports[i].Device);
        hc->Ports[i].Device = NULL;
        XhciEnumReset(&hc->Ports[i].Enum);
    }
}
