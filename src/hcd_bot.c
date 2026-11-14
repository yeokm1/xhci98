/*
 * hcd_bot.c - the resume keep of a Bulk-Only drive, the bus's half
 * (roadmap-usb4 task 49.34; design record 20 section 4.3.4).
 *
 * The pure machine (xhci_bot.c) is fed here from the three places a Bulk-Only
 * interface's traffic passes: a transfer's admission (HcdIoSubmit), its end
 * (HcdIoDeferred) and the thread's slow requests (selects, pipe resets,
 * aborts, RESET_PORT). From it and the device's state come the keep's inputs
 * (HcdBotFlags), the decision's commit with its latch (HcdBotLatch), and the
 * restore's Bulk-Only end (HcdBotRestore): the replay held paused, the medium
 * check (TEST UNIT READY, REQUEST SENSE, READ CAPACITY on the bus's own
 * record, BotXfer, with nothing of the class driver's admitted), and the
 * reopening in one hold of hcdSerialLock and the controller lock.
 *
 * Nothing here acts unless XhciResumeKeepBot is on: with it off no machine
 * is fed (BotShape stays 0) and no decision changes.
 */

#include "hcd.h"
#include "hcd_svc.h"
#include "xhci_hw.h"
#include "xhci_xfer.h"
#include "xhci_dbg.h"

/* The medium check's bounds (M5): each transfer, the whole check, and the
 * becoming-ready retry. */
#define HCD_BOT_XFER_MS         5000UL
#define HCD_BOT_CHECK_MS        15000UL
#define HCD_BOT_READY_STEP_MS   500UL
#define HCD_BOT_READY_MS        10000UL
#define HCD_BOT_POLL_MS         5UL
#define HCD_BOT_ATTENTIONS      4UL

/* Where the check's three phases sit in the controller scratch's control
 * area, which no control transfer uses while the thread runs the check. */
#define HCD_BOT_AT_CBW          0UL
#define HCD_BOT_AT_DATA         512UL
#define HCD_BOT_AT_CSW          1024UL

/* The bus's own CBW tags: a range of its own, though the device only echoes
 * them back and the class driver never sees one. */
#define HCD_BOT_TAG_BASE        0x42550000UL

static ULONG hcdBotTag;

/* A bound in milliseconds, read two ways so that neither a stalled clock
 * nor the time spent outside the polls lets it run long: the sleeps the
 * check itself counted, and the controller's tolerance clock (100 ms a
 * tick); the larger is the time spent. */
typedef struct _HCD_BOT_BUDGET {
    ULONG Start;                /* HcdTolNow at its start */
    ULONG Slept;                /* milliseconds of sleep counted */
    ULONG Limit;
} HCD_BOT_BUDGET, *PHCD_BOT_BUDGET;

static VOID hcdBotBudgetStart(PHCD_CONTROLLER hc, PHCD_BOT_BUDGET b,
                              ULONG limit)
{
    b->Start = HcdTolNow(hc);
    b->Slept = 0;
    b->Limit = limit;
}

static ULONG hcdBotBudgetSpent(PHCD_CONTROLLER hc, const HCD_BOT_BUDGET *b)
{
    ULONG ticks;
    ULONG clock;

    ticks = HcdTolNow(hc) - b->Start;
    clock = ticks < (b->Limit / 100UL) + 1UL ? ticks * 100UL : b->Limit;
    return (clock > b->Slept ? clock : b->Slept) >= b->Limit ? 1UL : 0UL;
}

/* A sleep of `ms`, counted against every budget given (NULL ones skipped). */
static VOID hcdBotSleep(ULONG ms, PHCD_BOT_BUDGET a, PHCD_BOT_BUDGET b,
                        PHCD_BOT_BUDGET c)
{
    LARGE_INTEGER due;

    HcdRelativeMs(&due, ms);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    if (a != NULL) {
        a->Slept += ms;
    }
    if (b != NULL) {
        b->Slept += ms;
    }
    if (c != NULL) {
        c->Slept += ms;
    }
}

/* The switch on at all (the machines are fed). Any IRQL. */
static ULONG hcdBotSleepSwitch(PHCD_CONTROLLER hc)
{
    return (hc->ResumeKeepOn && hc->ResumeKeepBot != 0) ? 1UL : 0UL;
}

/* Whether the switch keeps Bulk-Only drives after the sleep this suspend
 * was for (M9): bit 0 a hibernation, bit 1 a standby. Any IRQL. */
static ULONG hcdBotSleepOn(PHCD_CONTROLLER hc)
{
    if (!hc->ResumeKeepOn || hc->ResumeKeepBot == 0) {
        return 0;
    }
    if (hc->SuspendSleep == (ULONG)PowerSystemHibernate) {
        return (hc->ResumeKeepBot & 1UL) != 0 ? 1UL : 0UL;
    }
    if (hc->SuspendSleep >= (ULONG)PowerSystemSleeping1 &&
        hc->SuspendSleep <= (ULONG)PowerSystemSleeping3) {
        return (hc->ResumeKeepBot & 2UL) != 0 ? 1UL : 0UL;
    }
    return 0;
}

/* A client buffer's bytes, reached without a mapping call: an MDL mapped or
 * describing nonpaged pool and long enough, or a TransferBuffer
 * (hcd_urb.c's hcdCopyAnswer rule); else NULL. IRQL: <= DISPATCH_LEVEL. */
static const UCHAR *hcdBotMdlBytes(PMDL mdl, ULONG need)
{
    if (mdl == NULL || MmGetMdlByteCount(mdl) < need ||
        (mdl->MdlFlags &
         (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL)) == 0) {
        return NULL;
    }
    return (const UCHAR *)mdl->MappedSystemVa;
}

static const UCHAR *hcdBotReqBytes(const HCD_IO_REQUEST *req, ULONG need)
{
    if (req->Mdl != NULL) {
        return hcdBotMdlBytes(req->Mdl, need);
    }
    return (const UCHAR *)req->Buffer;
}

/* A completed transfer's bytes: its MDL as above, or - with XhciDmaStaging,
 * where a TransferBuffer request keeps no MDL - the TransferBuffer it was
 * staged from (x->SysVa). IRQL: <= DISPATCH_LEVEL. */
static const UCHAR *hcdBotXferBytes(const HCD_XFER *x, ULONG need)
{
    if (x->Mdl != NULL) {
        return hcdBotMdlBytes(x->Mdl, need);
    }
    return (const UCHAR *)x->SysVa;
}

/* Whether `pipe` is the drive's Bulk-In (*in 1) or Bulk-Out pipe. */
/* The same, and a locked MDL neither mapped nor of nonpaged pool - what
 * Windows 2000's usbstor gives a READ CAPACITY's or a REQUEST SENSE's data
 * (read on the first leg: flags 0x8A) - copied into `copy` through the
 * controller's window (hcd_stage.c). IRQL: <= DISPATCH_LEVEL. */
static const UCHAR *hcdBotXferBytesCopy(PHCD_CONTROLLER hc, const HCD_XFER *x,
                                        ULONG need, PUCHAR copy)
{
    const UCHAR *bytes;

    bytes = hcdBotXferBytes(x, need);
    if (bytes != NULL || x->Mdl == NULL || need > HCD_BOT_COPY_BYTES) {
        return bytes;
    }
    return HcdStageCopyHead(hc, x->Mdl, copy, need) ? copy : NULL;
}

static ULONG hcdBotPipe(PHCD_USB_DEVICE dev, PHCD_PIPE pipe, PULONG in)
{
    if (pipe == NULL || pipe == &dev->Ep0Pipe) {
        return 0;
    }
    if (pipe->Dci == dev->BotDciIn) {
        *in = 1;
        return 1;
    }
    if (pipe->Dci == dev->BotDciOut) {
        *in = 0;
        return 1;
    }
    return 0;
}

/*
 * The shape M2 admits: a configuration of one interface whose active
 * setting is class 08h, subclass 06h, protocol 50h with exactly two bulk
 * endpoints, one IN and one OUT, both open as plain bulk pipes. Returns 1
 * with their DCIs. Thread only, after a select.
 */
static ULONG hcdBotShape(PHCD_USB_DEVICE dev, PULONG dciIn, PULONG dciOut)
{
    PUCHAR c;
    PHCD_PIPE pipe;
    ULONG len;
    ULONG at;
    ULONG inside;
    ULONG addr;
    ULONG dci;
    ULONG ins;
    ULONG outs;

    c = dev->Config;
    len = dev->ConfigLength;
    if (c == NULL || len < 9 || c[4] != 1 || dev->ConfigValue == 0 ||
        dev->ConfigValue != (ULONG)c[5]) {
        return 0;
    }
    inside = 0;
    ins = 0;
    outs = 0;
    for (at = 0; at + 2 <= len;) {
        if (c[at] < 2 || at + c[at] > len) {
            return 0;
        }
        if (c[at + 1] == 4 && c[at] >= 9) {
            if (inside) {
                break;
            }
            if (c[at + 2] == 0 && c[at + 3] == dev->Alternate[0]) {
                if (c[at + 5] != 0x08 || c[at + 6] != 0x06 ||
                    c[at + 7] != 0x50 || c[at + 4] != 2) {
                    return 0;
                }
                inside = 1;
            }
        } else if (inside && c[at + 1] == 5 && c[at] >= 7) {
            if ((c[at + 3] & 0x03) != 0x02) {
                return 0;
            }
            addr = (ULONG)c[at + 2];
            dci = ((addr & 0x0FUL) * 2UL) + ((addr & 0x80UL) != 0 ? 1UL : 0UL);
            if (dci < 2 || dci >= 32) {
                return 0;
            }
            pipe = dev->Pipes[dci];
            if (pipe == NULL || pipe->TransferType != XHCI_PIPE_XFER_BULK ||
                pipe->Streams != NULL) {
                return 0;
            }
            if ((addr & 0x80UL) != 0) {
                ins++;
                *dciIn = dci;
            } else {
                outs++;
                *dciOut = dci;
            }
        }
        at += c[at];
    }
    return (inside && ins == 1 && outs == 1) ? 1UL : 0UL;
}

VOID HcdBotAdmitLocked(PHCD_USB_DEVICE dev, PHCD_PIPE pipe,
                       const HCD_IO_REQUEST *req)
{
    ULONG in;

    /* M4: any function request admitted ends a keep in progress. */
    dev->BotLatch = 0;
    if (!dev->BotShape || req == NULL || !hcdBotPipe(dev, pipe, &in)) {
        return;
    }
    /* Only a candidate CBW's bytes are read at its admission. */
    XhciBotSubmit(&dev->Bot, in, req->Length,
                  (!in && req->Length == XHCI_BOT_CBW_BYTES)
                      ? hcdBotReqBytes(req, XHCI_BOT_CBW_BYTES)
                      : NULL);
}

VOID HcdBotXferEnd(PHCD_CONTROLLER hc, PHCD_XFER x, LONG status)
{
    PHCD_USB_DEVICE dev;
    const XHCI_SETUP_PACKET *s;
    const UCHAR *bytes;
    UCHAR copy[HCD_BOT_COPY_BYTES];
    KIRQL oldIrql;
    ULONG type;
    ULONG how;
    ULONG in;
    ULONG ok;
    ULONG ep;

    dev = x->Pipe->Device;
    if (!dev->BotShape) {
        return;
    }
    ok = status == XHCI_USBD_STATUS_SUCCESS;
    if (x->Pipe == &dev->Ep0Pipe) {
        if (!x->Control) {
            return;
        }
        s = &x->Setup;
        type = (ULONG)s->bmRequestType;
        bytes = NULL;
        if (type == 0xA1 && s->bRequest == XHCI_BOT_REQ_MAX_LUN && ok &&
            x->Offset >= 1) {
            bytes = hcdBotXferBytesCopy(hc, x, 1, copy);
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if ((type == 0xA1 && s->bRequest == XHCI_BOT_REQ_MAX_LUN) ||
            (type == 0x21 && s->bRequest == XHCI_BOT_REQ_RESET)) {
            XhciBotClassRequest(&dev->Bot, (ULONG)s->bRequest,
                                ok && (s->bRequest == XHCI_BOT_REQ_RESET ||
                                       bytes != NULL),
                                status == XHCI_USBD_STATUS_STALL_PID,
                                bytes != NULL ? (ULONG)bytes[0] : 0UL,
                                bytes != NULL ? 1UL : 0UL);
        } else if ((type & 0x60UL) == 0x20UL && (type & 0x1FUL) == 0x01UL) {
            /* Another class request to the interface: not the transport's. */
            XhciBotClassRequest(&dev->Bot, (ULONG)s->bRequest, ok, 0, 0, 0);
        } else if (type == 0x02 && s->bRequest == 1 && s->wValue == 0) {
            /* CLEAR_FEATURE(ENDPOINT_HALT) sent as a control transfer. */
            ep = (ULONG)s->wIndex & 0x8FUL;
            if (dev->Pipes[dev->BotDciIn] != NULL &&
                ep == dev->Pipes[dev->BotDciIn]->EndpointAddress) {
                XhciBotClearHalt(&dev->Bot, 1, ok);
            } else if (dev->Pipes[dev->BotDciOut] != NULL &&
                       ep == dev->Pipes[dev->BotDciOut]->EndpointAddress) {
                XhciBotClearHalt(&dev->Bot, 0, ok);
            }
        } else if ((type == 0x00 && s->bRequest == 9) ||
                   (type == 0x01 && s->bRequest == 11)) {
            /* A configuration or a setting changed behind the selects. */
            XhciBotOther(&dev->Bot);
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return;
    }
    if (!hcdBotPipe(dev, x->Pipe, &in)) {
        return;
    }
    if (ok) {
        how = XHCI_BOT_XFER_OK;
    } else if (status == XHCI_USBD_STATUS_STALL_PID) {
        how = XHCI_BOT_XFER_STALL;
    } else if (x->CancelRequested || status == HCD_USBD_CANCELED) {
        how = XHCI_BOT_XFER_CANCEL;
    } else {
        how = XHCI_BOT_XFER_ERROR;
    }
    bytes = NULL;
    if (in && ok) {
        bytes = x->Offset <= HCD_BOT_COPY_BYTES
                    ? hcdBotXferBytesCopy(hc, x, x->Offset, copy)
                    : hcdBotXferBytes(x, x->Offset);
    }
    if (in && ok && bytes == NULL && x->Offset <= XHCI_BOT_STASH_BYTES) {
        /* M14's reading: which buffer form the class driver gives a short
         * IN answer (a CSW, a capacity, a sense) the bus cannot read. */
        XHCI_DBG_VALUE_CHANGED("hcd: bot, short IN unreadable, mdl flags",
                       x->Mdl != NULL ? (ULONG)x->Mdl->MdlFlags : 0xFFFFFFFFUL);
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    XhciBotComplete(&dev->Bot, in, how, x->Offset, x->Length, bytes);
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

VOID HcdBotSlowEnd(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG event,
                   PHCD_PIPE pipe, LONG usbd)
{
    KIRQL oldIrql;
    ULONG first;
    ULONG dciIn;
    ULONG dciOut;
    ULONG shape;
    ULONG ok;
    ULONG in;

    ok = usbd == XHCI_USBD_STATUS_SUCCESS;
    if (event == HCD_BOT_EV_SELECT_CONFIG && ok) {
        first = dev->BotConfigured ? 0UL : 1UL;
        dev->BotConfigured = 1;
        dciIn = 0;
        dciOut = 0;
        shape = hcdBotSleepSwitch(hc) && hcdBotShape(dev, &dciIn, &dciOut);
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        if (shape) {
            if (!dev->BotShape) {
                XhciBotInit(&dev->Bot);
            }
            dev->BotShape = 1;
            dev->BotDciIn = dciIn;
            dev->BotDciOut = dciOut;
            XhciBotSelected(&dev->Bot, first, 0);
        } else {
            dev->BotShape = 0;
            XhciBotInit(&dev->Bot);
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        return;
    }
    if (event == HCD_BOT_EV_SELECT_CONFIG) {
        dev->BotConfigured = 1;
    }
    if (!dev->BotShape) {
        return;
    }
    shape = 1;
    if (event == HCD_BOT_EV_SELECT_IFACE && ok) {
        dciIn = 0;
        dciOut = 0;
        shape = hcdBotShape(dev, &dciIn, &dciOut) && dciIn == dev->BotDciIn &&
                dciOut == dev->BotDciOut;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    switch (event) {
    case HCD_BOT_EV_SELECT_IFACE:
        if (ok && shape) {
            XhciBotSelected(&dev->Bot, 0, 1);
        } else if (ok) {
            dev->BotShape = 0;
            XhciBotInit(&dev->Bot);
        } else {
            XhciBotOther(&dev->Bot);
        }
        break;
    case HCD_BOT_EV_RESET_CLEAR:
        if (hcdBotPipe(dev, pipe, &in)) {
            XhciBotClearHalt(&dev->Bot, in, ok);
        } else {
            XhciBotOther(&dev->Bot);
        }
        break;
    case HCD_BOT_EV_RESET_PORT:
        if (ok) {
            XhciBotPortReset(&dev->Bot);
        } else {
            XhciBotOther(&dev->Bot);
        }
        break;
    default:
        /* An abort, a host-only reset, a failed select. */
        XhciBotOther(&dev->Bot);
        break;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
}

VOID HcdBotXferRetired(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    dev->BotXferArmed = 0;
    dev->BotXferDone = 1;
    (VOID)KeSetEvent(&hc->XferDoneEvent, IO_NO_INCREMENT, FALSE);
}

/* Whether the device's root port has a cycle or a re-presentation recorded
 * (CYCLE_PORT's admission, hcd_urb.c, outside the slow queue). Controller
 * lock held. */
static ULONG hcdBotPortCycleLocked(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    ULONG port;
    ULONG bit;

    port = dev->Location;
    if (port == 0 || port > HCD_PORT_COUNT) {
        return 1;
    }
    bit = 1UL << ((port - 1) % 32UL);
    return ((hc->PortCycle[(port - 1) / 32UL] & bit) != 0 ||
            (hc->PortRepresent[(port - 1) / 32UL] & bit) != 0) ? 1UL : 0UL;
}

ULONG HcdBotFlags(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;
    ULONG flags;

    if (!hcdBotSleepOn(hc)) {
        return 0;
    }
    flags = XHCI_RKEEP_F_BOT_ON;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (dev->BotShape) {
        flags |= XHCI_RKEEP_F_BOT;
        if (XhciBotKeepable(&dev->Bot)) {
            flags |= XHCI_RKEEP_F_BOT_SYNC;
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return flags;
}

ULONG HcdBotLatch(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;
    ULONG ok;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    ok = dev->BotShape && XhciBotKeepable(&dev->Bot) &&
         HcdIoDeviceIdleLocked(dev) && !HcdCfgDeviceQueuedLocked(hc, dev) &&
         !hcdBotPortCycleLocked(hc, dev);
    if (ok) {
        dev->BotLatch = 1;
        dev->BotKeeping = 1;
        dev->BotRecorded = dev->Bot.Cap;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    XHCI_DBG_VALUE("hcd: bot keep, latch/state/why",
                   (ok << 24) | (dev->Bot.State << 16) | dev->Bot.LastUnknown);
    XHCI_DBG_VALUE("hcd: bot keep, recorded kind/last LBA",
                   (dev->BotRecorded.Kind << 28) |
                       (dev->BotRecorded.LastLbaLo & 0x0FFFFFFFUL));
    return ok;
}

/*
 * One transfer of the bus's own, on the paused Bulk-In or Bulk-Out pipe:
 * `length` bytes at `at` in the scratch, published under the controller
 * lock as the hub's status read is (hcd_hub.c), its completion routed to
 * BotXfer's flags (HcdBotXferRetired) and awaited, polled, within
 * HCD_BOT_XFER_MS. A timeout leaves the TD possibly still able to write
 * the scratch: as an EP0 timeout does, the scratch is tainted and the
 * controller's reset requested, which takes every slot. Returns 1 on
 * success, *moved the bytes moved. Thread only, powered.
 */
static ULONG hcdBotXfer(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                        PHCD_PIPE pipe, ULONG at, ULONG length, ULONG in,
                        PHCD_BOT_BUDGET cmd, PHCD_BOT_BUDGET check,
                        PHCD_BOT_BUDGET extra, PULONG moved)
{
    USBPORT_SCATTER_GATHER_LIST sg;
    XHCI_NORMAL_REQUEST nreq;
    XHCI_TRB trbs[XHCI_XFER_MAX_CONTROL_TRBS];
    KIRQL oldIrql;
    PUCHAR b;
    ULONG answer;
    ULONG done;
    LONG usbd;
    ULONG i;

    *moved = 0;
    b = (PUCHAR)&sg;
    for (i = 0; i < sizeof(sg); i++) {
        b[i] = 0;
    }
    sg.SgElementCount = 1;
    sg.SgElement[0].SgPhysicalAddressLo = hc->ScratchPa.LowPart + at;
    sg.SgElement[0].SgTransferLength = length;
    sg.SgElement[0].SgOffset = 0;
    b = (PUCHAR)&nreq;
    for (i = 0; i < sizeof(nreq); i++) {
        b[i] = 0;
    }
    nreq.TransferLength = length;
    nreq.DirectionIn = in;
    nreq.MaxPacketSize = pipe->MaxPacketSize;
    nreq.SgList = &sg;
    nreq.TdSizeForm = XhciTdSizeFormOf(hc->Hc.HcInfo.HciVersion);

    /* A bound already spent publishes nothing: only a TD left outstanding
     * is worth the scratch's taint and the controller's reset below. */
    if (hcdBotBudgetSpent(hc, cmd) || hcdBotBudgetSpent(hc, check) ||
        (extra != NULL && hcdBotBudgetSpent(hc, extra))) {
        return 0;
    }
    answer = XHCI_XFER_BUSY;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (!dev->Gone && !pipe->Closed && !pipe->Halted && !dev->BotXferArmed) {
        dev->BotXferDone = 0;
        answer = XhciXferSubmitNormal(pipe->Queue, pipe->Ring, &nreq, in,
                                      &dev->BotXfer, dev, trbs,
                                      XHCI_XFER_MAX_CONTROL_TRBS);
        if (answer == XHCI_XFER_OK) {
            dev->BotXferArmed = 1;
            XhciWriteDoorbell(&hc->Hc, dev->SlotId, pipe->Dci);
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (answer != XHCI_XFER_OK) {
        return 0;
    }
    done = 0;
    for (;;) {
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        done = dev->BotXferDone;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (done || hcdBotBudgetSpent(hc, cmd) ||
            hcdBotBudgetSpent(hc, check) ||
            (extra != NULL && hcdBotBudgetSpent(hc, extra))) {
            break;
        }
        (VOID)HcdDrainPending(hc);
        hcdBotSleep(HCD_BOT_POLL_MS, cmd, check, extra);
    }
    if (!done) {
        hc->ScratchTainted = 1;
        HcdSvcRequestReset(&hc->Hc);
        XhciLogNote(&hc->Hc, "bot.check.timeout", (dev->SlotId << 8) | pipe->Dci);
        return 0;
    }
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    usbd = dev->BotXfer.UsbdStatus;
    *moved = dev->BotXfer.BytesTransferred;
    dev->BotXferDone = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    if (*moved > length) {
        *moved = length;
    }
    return usbd == XHCI_USBD_STATUS_SUCCESS;
}

/*
 * One command of the medium check: its CBW, its data IN (at most
 * XHCI_BOT_STASH_BYTES) and its CSW, each validated by the machine's
 * parsers; any STALL or failure fails the command. Returns 1 with the CSW's
 * status and the data's usable length (section 5.3.3). Thread only.
 */
static ULONG hcdBotCommand(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                           const UCHAR *cdb, ULONG cdbLength,
                           ULONG dataLength, PHCD_BOT_BUDGET check,
                           PHCD_BOT_BUDGET extra, PULONG status,
                           PULONG usable)
{
    HCD_BOT_BUDGET cmd;
    PUCHAR s;
    PHCD_PIPE pin;
    PHCD_PIPE pout;
    ULONG moved;
    ULONG data;
    ULONG residue;
    ULONG tag;

    s = (PUCHAR)hc->ScratchVa;
    pin = dev->Pipes[dev->BotDciIn];
    pout = dev->Pipes[dev->BotDciOut];
    if (s == NULL || pin == NULL || pout == NULL ||
        dataLength > XHCI_BOT_STASH_BYTES) {
        return 0;
    }
    hcdBotBudgetStart(hc, &cmd, HCD_BOT_XFER_MS);
    tag = HCD_BOT_TAG_BASE | (++hcdBotTag & 0xFFFFUL);
    XhciBotBuildCbw(s + HCD_BOT_AT_CBW, tag, dataLength, dataLength != 0,
                    cdb, cdbLength);
    if (!hcdBotXfer(hc, dev, pout, HCD_BOT_AT_CBW, XHCI_BOT_CBW_BYTES, 0,
                    &cmd, check, extra, &moved) ||
        moved != XHCI_BOT_CBW_BYTES) {
        return 0;
    }
    data = 0;
    if (dataLength != 0) {
        if (!hcdBotXfer(hc, dev, pin, HCD_BOT_AT_DATA, dataLength, 1,
                        &cmd, check, extra, &data)) {
            return 0;
        }
    }
    if (!hcdBotXfer(hc, dev, pin, HCD_BOT_AT_CSW, XHCI_BOT_CSW_BYTES, 1,
                    &cmd, check, extra, &moved) ||
        !XhciBotParseCsw(s + HCD_BOT_AT_CSW, moved, tag, dataLength, status,
                         &residue) ||
        *status == 2) {
        return 0;
    }
    *usable = XhciBotUsable(data, dataLength, residue);
    return 1;
}

/*
 * M5's medium check, the class driver fenced by the latch: TEST UNIT READY,
 * REQUEST SENSE on its CHECK CONDITION (only the reset's attention read
 * past, up to four times; becoming ready retried every 500 ms within 10 s;
 * medium not present the no-medium answer; anything else fails), then READ
 * CAPACITY (10), and (16) when (10) reports the largest LBA; the answer
 * compared with the class driver's own last one, recorded at the decision.
 * Returns 1 for the same medium. Thread only, powered.
 */
static ULONG hcdBotMediumCheck(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    UCHAR cdb[16];
    XHCI_BOT_CAP now;
    HCD_BOT_BUDGET check;
    HCD_BOT_BUDGET ready;
    PUCHAR s;
    ULONG attentions;
    ULONG becoming;
    ULONG status;
    ULONG usable;
    ULONG key;
    ULONG asc;
    ULONG ascq;
    ULONG verdict;
    ULONG i;

    s = (PUCHAR)hc->ScratchVa;
    if (s == NULL || hc->ScratchHeld != 0 || hc->ScratchTainted ||
        dev->BotRecorded.Kind == XHCI_BOT_CAP_NONE) {
        XHCI_DBG_VALUE("hcd: bot check, not begun, kind",
                       dev->BotRecorded.Kind);
        return 0;
    }
    now.Kind = XHCI_BOT_CAP_NONE;
    attentions = 0;
    becoming = 0;
    hcdBotBudgetStart(hc, &check, HCD_BOT_CHECK_MS);
    for (;;) {
        if (hcdBotBudgetSpent(hc, &check) ||
            (becoming && hcdBotBudgetSpent(hc, &ready))) {
            return 0;
        }
        for (i = 0; i < sizeof(cdb); i++) {
            cdb[i] = 0;
        }
        cdb[0] = (UCHAR)XHCI_BOT_OP_TEST_UNIT_READY;
        if (!hcdBotCommand(hc, dev, cdb, 6, 0, &check,
                           becoming ? &ready : NULL, &status, &usable)) {
            return 0;
        }
        if (becoming && hcdBotBudgetSpent(hc, &ready)) {
            /* Ready, or not, only past the retries' bound: not kept. */
            return 0;
        }
        if (status == 0) {
            break;
        }
        cdb[0] = (UCHAR)XHCI_BOT_OP_REQUEST_SENSE;
        cdb[4] = 18;
        if (!hcdBotCommand(hc, dev, cdb, 6, 18, &check,
                           becoming ? &ready : NULL, &status, &usable) ||
            status != 0 ||
            !XhciBotParseSense(s + HCD_BOT_AT_DATA, usable, &key, &asc,
                               &ascq)) {
            return 0;
        }
        verdict = XhciBotTurSense(key, asc, ascq);
        XHCI_DBG_VALUE("hcd: bot check, sense key/asc/ascq",
                       (key << 16) | (asc << 8) | ascq);
        if (verdict == XHCI_BOT_TUR_NO_MEDIUM) {
            now.Kind = XHCI_BOT_CAP_NO_MEDIUM;
            return XhciBotSameMedium(&dev->BotRecorded, &now);
        }
        if (verdict == XHCI_BOT_TUR_RETRY && attentions < HCD_BOT_ATTENTIONS) {
            attentions++;
            continue;
        }
        if (verdict == XHCI_BOT_TUR_BECOMING) {
            if (!becoming) {
                becoming = 1;
                hcdBotBudgetStart(hc, &ready, HCD_BOT_READY_MS);
            }
            if (!hcdBotBudgetSpent(hc, &ready)) {
                hcdBotSleep(HCD_BOT_READY_STEP_MS, &check, &ready, NULL);
                continue;
            }
        }
        return 0;
    }
    for (i = 0; i < sizeof(cdb); i++) {
        cdb[i] = 0;
    }
    cdb[0] = (UCHAR)XHCI_BOT_OP_READ_CAPACITY10;
    if (!hcdBotCommand(hc, dev, cdb, 10, 8, &check, NULL, &status,
                       &usable) ||
        status != 0 ||
        !XhciBotParseCapacity(s + HCD_BOT_AT_DATA, usable, 0, &now)) {
        return 0;
    }
    if (now.LastLbaLo == 0xFFFFFFFFUL) {
        for (i = 0; i < sizeof(cdb); i++) {
            cdb[i] = 0;
        }
        cdb[0] = (UCHAR)XHCI_BOT_OP_SERVICE_IN16;
        cdb[1] = (UCHAR)XHCI_BOT_SA_READ_CAPACITY16;
        cdb[13] = 32;
        if (!hcdBotCommand(hc, dev, cdb, 16, 32, &check, NULL, &status,
                           &usable) ||
            status != 0 ||
            !XhciBotParseCapacity(s + HCD_BOT_AT_DATA, usable, 1, &now)) {
            return 0;
        }
    }
    XHCI_DBG_VALUE("hcd: bot check, capacity low/block",
                   now.LastLbaLo);
    return XhciBotSameMedium(&dev->BotRecorded, &now);
}

/*
 * M4's reopening, one step: in a hold of hcdSerialLock that found no twin
 * of the serial (HcdSerialHold) and, inside it, of the controller lock -
 * the latch intact, no request of the class driver taken on any pipe or
 * queued for the thread, no cycle recorded for the port, the bus's own
 * transfer retired - every pipe's pause lifted and the machine set SYNC;
 * the pipes kicked after both holds. Returns 0, the pipes still paused, for
 * the caller's drop. Thread only.
 */
ULONG HcdBotReopen(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL serialIrql;
    KIRQL oldIrql;
    ULONG unique;
    ULONG ok;
    ULONG dci;

#if defined(XHCI_FLAVOUR_QEMU)
    if (hc->QemuBotDelayMs != 0) {
        LARGE_INTEGER due;

        XhciLogNote(&hc->Hc, "qemu.bot.delay", hc->QemuBotDelayMs);
        HcdRelativeMs(&due, hc->QemuBotDelayMs);
        (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
    }
#endif
    unique = HcdSerialHold(dev, &serialIrql);
    ok = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (unique && dev->BotLatch && !dev->BotXferArmed &&
        HcdIoDeviceIdleLocked(dev) && !HcdCfgDeviceQueuedLocked(hc, dev) &&
        !hcdBotPortCycleLocked(hc, dev)) {
        for (dci = 2; dci < 32; dci++) {
            if (dev->Pipes[dci] != NULL) {
                HcdIoPipeUnpauseLocked(dev->Pipes[dci]);
            }
        }
        HcdIoPipeUnpauseLocked(&dev->Ep0Pipe);
        XhciBotPortReset(&dev->Bot);
        ok = 1;
    }
    dev->BotLatch = 0;
    dev->BotKeeping = 0;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    HcdSerialUnhold(serialIrql);
    XhciLogNote(&hc->Hc, "bot.reopen", (dev->Location << 8) | ok);
    if (!ok) {
        return 0;
    }
    for (dci = 2; dci < 32; dci++) {
        if (dev->Pipes[dci] != NULL) {
            HcdIoPipeRelease(hc, dev->Pipes[dci]);
        }
    }
    HcdIoPipeRelease(hc, &dev->Ep0Pipe);
    return 1;
}

ULONG HcdBotRestore(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    ULONG same;

    if (!HcdCfgResumeReplay(hc, dev, HCD_REPLAY_HOLD)) {
        return 0;
    }
    same = hcdBotMediumCheck(hc, dev);
    XhciLogNote(&hc->Hc, "bot.medium", (dev->Location << 8) | same);
    return same;
}
