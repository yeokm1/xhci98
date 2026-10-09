/*
 * hcd_susp.c - selective suspend's thread executor (roadmap-hcd.md task
 * 36.30; design record 14 sections 3, 3.9 and 3.10).
 *
 * The enum thread's half of selective suspend: the admission gate on a
 * device's pipes and the restart that reopens it (3.2), the suspend and
 * resume sequences at a root port and a USB 2.0 hub port (3.4 to 3.6) as
 * xhci_susp.c's steps, the decision pass over the device's PDO slots with
 * the completions it owes (3.1, 3.9), the wake event's claim (5.4), and
 * the evidence the root port's acknowledgement and the hub port look hand
 * over (3.7, 3.8). Every decision is xhci_susp.c's; this file reads the
 * values under the controller lock, asks, and performs the one step
 * asked. Dispatch's half (D-IRP, WAIT_WAKE and START admission) is
 * hcd_pdo.c's (36.31). Inert while hc->SuspOn is 0.
 *
 * IRQL: PASSIVE_LEVEL, the enum thread only, powered, unless a function
 * says otherwise. Every IRP is completed outside the controller lock.
 */

#include "hcd.h"
#include "xhci_hw.h"
#include "xhci_dbg.h"

/* USB 2.0 9.4: SET_FEATURE/CLEAR_FEATURE(DEVICE_REMOTE_WAKEUP) to the
 * device. */
#define HCD_SUSP_RT_DEVICE_OUT      0x00U
#define HCD_SUSP_REQ_CLEAR_FEATURE  0x01U
#define HCD_SUSP_REQ_SET_FEATURE    0x03U
#define HCD_SUSP_FEAT_REMOTE_WAKEUP 1U

/* The PDOs of a device's chain: one per function of a split device, at
 * most XHCI_FUNC_MAX (xhci_func.c), else one. Read into the controller's
 * scratch (hc->SuspFuncs), not onto the enum thread's stack (Codex review
 * of 36.30, round 1, finding 4). */
#define HCD_SUSP_MAX_FUNCS          XHCI_FUNC_MAX

/* Decisions one device is given in one pass (3.10). */
#define HCD_SUSP_PASSES             4UL

/* The hub port's poll step (hcd_hub.c's HCD_HUB_POLL_STEP_MS). */
#define HCD_SUSP_HUB_POLL_MS        10UL

/* The pure core's D-states are DEVICE_POWER_STATE's values (3.9). */
XHCI_C_ASSERT(susp_d0, XHCI_SUSP_D0 == (ULONG)PowerDeviceD0);
XHCI_C_ASSERT(susp_d1, XHCI_SUSP_D1 == (ULONG)PowerDeviceD1);
XHCI_C_ASSERT(susp_d2, XHCI_SUSP_D2 == (ULONG)PowerDeviceD2);
XHCI_C_ASSERT(susp_d3, XHCI_SUSP_D3 == (ULONG)PowerDeviceD3);

/* ------------------------------------------------------------------ */
/* Small services                                                      */
/* ------------------------------------------------------------------ */

static ULONG hcdSuspHalted(PHCD_CONTROLLER hc)
{
    return hc->Hc.ControllerFailed || hc->ScratchTainted;
}

/*
 * A wait the machine requires - the dwell, TDRSMDN, TRSMRCY, SP=1's
 * minimum - and only these advance its clock (3.10). Before Windows 8 a
 * relative KeDelayExecutionThread may end up to a clock period early, so
 * a minimum is kept by stalling, in 50 us steps, which never ends early;
 * these waits are tens of milliseconds and rare (Codex review of 36.30,
 * round 3, finding 2).
 */
static VOID hcdSuspHold(PHCD_CONTROLLER hc, ULONG milliseconds)
{
    ULONG steps;

    for (steps = milliseconds * 20UL; steps != 0; steps--) {
        KeStallExecutionProcessor(50);
    }
    hc->SuspClockMs += milliseconds;
}

/* A pause between polls, which bounds nothing and is not counted: the
 * clock only ever under-counts, so every wait computed from it is at
 * least as long as asked. */
static VOID hcdSuspPause(ULONG milliseconds)
{
    LARGE_INTEGER due;

    HcdRelativeMs(&due, milliseconds);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &due);
}

static ULONG hcdSuspEpState(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            ULONG dci)
{
    ULONG offset;

    if (XhciEndpointContextOffset(&hc->Hc.Layout, dev->SlotId, dci,
                                  &offset) != XHCI_LAYOUT_OK) {
        return XHCI_EP_STATE_DISABLED;
    }
#if defined(XHCI_FLAVOUR_QEMU)
    return HcdInjEpState(hc, dev->SlotId, dci,
                         XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]));
#else
    return XHCI_EP_GET_STATE(XhciCommonAt(&hc->Hc, offset)[0]);
#endif
}

static PHCD_PIPE hcdSuspPipe(PHCD_USB_DEVICE dev, ULONG dci)
{
    return (dci == 1) ? &dev->Ep0Pipe : dev->Pipes[dci];
}

static PHCD_PORT hcdSuspPort(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    if (dev->Location == 0 || dev->Location > HCD_PORT_COUNT) {
        return NULL;
    }
    return &hc->Ports[dev->Location - 1];
}

static PHCD_DEVICE_PDO hcdSuspPdo(PDEVICE_OBJECT object)
{
    return (object != NULL) ? (PHCD_DEVICE_PDO)object->DeviceExtension
                            : NULL;
}

/* The PDOs on the device's chain (the thread is its writer). */
static ULONG hcdSuspChainLength(PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO pdo;
    ULONG n;

    n = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        n++;
    }
    return n;
}

/* Whether the machine serves the device: the switch on, a USB 2.0 device
 * that is not a hub, with a PDO (5.1). */
static ULONG hcdSuspServes(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO pdo;
    ULONG cls;

    if (!hc->SuspOn || dev->Gone || dev->Hub != NULL) {
        return 0;
    }
    pdo = hcdSuspPdo(dev->Pdo);
    if (pdo == NULL || pdo->Hub) {
        return 0;
    }
    if (hcdSuspChainLength(dev) > HCD_SUSP_MAX_FUNCS) {
        /* Not reached: a split makes at most XHCI_FUNC_MAX functions. A
         * decision over part of a chain could suspend the device under a
         * function in D0, so a longer one is not served. */
        return 0;
    }
    /* The class decoded at the PDO's creation (hcd_pdo.c). */
    cls = pdo->SpeedClass;
    return XhciSuspEligible(1, !(cls == XHCI_SPEED_LOW ||
                                 cls == XHCI_SPEED_FULL ||
                                 cls == XHCI_SPEED_HIGH),
                            0);
}

/* The device's PDOs into funcs[] (at most HCD_SUSP_MAX_FUNCS), and their
 * count. Controller lock held. */
static ULONG hcdSuspRead(PHCD_USB_DEVICE dev, PHCD_DEVICE_PDO *pdos,
                         PXHCI_SUSP_FUNC funcs)
{
    PHCD_DEVICE_PDO pdo;
    ULONG n;

    n = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL && n < HCD_SUSP_MAX_FUNCS;
         pdo = pdo->Sibling) {
        pdos[n] = pdo;
        funcs[n].PwrState = pdo->PwrState;
        funcs[n].PwrWanted = pdo->PwrWanted;
        funcs[n].DxPended = (pdo->PwrIrp != NULL &&
                             pdo->PwrWanted != XHCI_SUSP_D0) ? 1UL : 0UL;
        funcs[n].DxEpoch = pdo->PwrDxEpoch;
        funcs[n].D0Pended = (pdo->PwrIrp != NULL &&
                             pdo->PwrWanted == XHCI_SUSP_D0) ? 1UL : 0UL;
        funcs[n].WaitWake = (pdo->WaitWakeIrp != NULL) ? 1UL : 0UL;
        funcs[n].Closed = pdo->PwrClosed;
        funcs[n].Seq = pdo->PwrSeq;
        funcs[n].WokeHold = pdo->WokeHold;
        funcs[n].CompleteDx = 0;
        funcs[n].ToD0 = 0;
        funcs[n].CompleteD0 = 0;
        n++;
    }
    return n;
}

/* A D-IRP the thread claimed, completed SUCCESS (4.2): PoStartNextPowerIrp
 * first, the D0's power state recorded. */
static VOID hcdSuspCompletePower(PHCD_DEVICE_PDO pdo, PIRP irp, ULONG d0)
{
    POWER_STATE state;

    if (d0) {
        pdo->Common.DevicePower = PowerDeviceD0;
        state.DeviceState = PowerDeviceD0;
        (VOID)PoSetPowerState(pdo->Common.Self, DevicePowerState, state);
    }
    PoStartNextPowerIrp(irp);
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    (VOID)InterlockedDecrement(&pdo->PowerPending);
}

/* A WAIT_WAKE the thread claimed, completed with `status`. */
static VOID hcdSuspCompleteWake(PHCD_DEVICE_PDO pdo, PIRP irp,
                                NTSTATUS status)
{
    PoStartNextPowerIrp(irp);
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    (VOID)InterlockedDecrement(&pdo->PowerPending);
}

/* What one claim took, completed after the hold. */
typedef struct _HCD_SUSP_TAKEN {
    ULONG Count;
    PHCD_DEVICE_PDO Pdo[2 * HCD_SUSP_MAX_FUNCS];
    PIRP Irp[2 * HCD_SUSP_MAX_FUNCS];
    ULONG Kind[2 * HCD_SUSP_MAX_FUNCS];
} HCD_SUSP_TAKEN, *PHCD_SUSP_TAKEN;

#define HCD_SUSP_TAKE_DX        1UL
#define HCD_SUSP_TAKE_D0        2UL
#define HCD_SUSP_TAKE_WAKE_OK   3UL
#define HCD_SUSP_TAKE_WAKE_NS   4UL     /* STATUS_NOT_SUPPORTED */

static VOID hcdSuspTake(PHCD_SUSP_TAKEN t, PHCD_DEVICE_PDO pdo, PIRP irp,
                        ULONG kind)
{
    if (t->Count < 2 * HCD_SUSP_MAX_FUNCS) {
        t->Pdo[t->Count] = pdo;
        t->Irp[t->Count] = irp;
        t->Kind[t->Count] = kind;
        t->Count++;
    }
}

static VOID hcdSuspFinish(PHCD_SUSP_TAKEN t)
{
    ULONG i;

    for (i = 0; i < t->Count; i++) {
        switch (t->Kind[i]) {
        case HCD_SUSP_TAKE_DX:
            hcdSuspCompletePower(t->Pdo[i], t->Irp[i], 0);
            break;
        case HCD_SUSP_TAKE_D0:
            hcdSuspCompletePower(t->Pdo[i], t->Irp[i], 1);
            break;
        case HCD_SUSP_TAKE_WAKE_OK:
            hcdSuspCompleteWake(t->Pdo[i], t->Irp[i], STATUS_SUCCESS);
            break;
        default:
            hcdSuspCompleteWake(t->Pdo[i], t->Irp[i], STATUS_NOT_SUPPORTED);
            break;
        }
    }
    t->Count = 0;
}

/*
 * The WAIT_WAKEs of the device's PDOs claimed by 4.2's handshake: with
 * `watermark` (a wake event) those stamped before it whose function is D1
 * or D2, each given WokeHold (3.9), to complete SUCCESS; without (a STALL's
 * refusal), all of them, to complete NOT_SUPPORTED. A cancel routine
 * already running keeps its IRP. Returns how many were claimed.
 * Controller lock held.
 */
static ULONG hcdSuspClaimWakes(PHCD_USB_DEVICE dev, ULONG useMark,
                               ULONG watermark, PHCD_SUSP_TAKEN t)
{
    PHCD_DEVICE_PDO pdo;
    PIRP irp;
    ULONG claimed;

    claimed = 0;
    for (pdo = hcdSuspPdo(dev->Pdo); pdo != NULL; pdo = pdo->Sibling) {
        irp = pdo->WaitWakeIrp;
        if (irp == NULL) {
            continue;
        }
        if (useMark &&
            !XhciSuspWakeClaims(pdo->WakeStamp, watermark, pdo->PwrState)) {
            continue;
        }
        if (!XhciSuspClaim(1, IoSetCancelRoutine(irp, NULL) != NULL)) {
            continue;
        }
        pdo->WaitWakeIrp = NULL;
        if (useMark) {
            pdo->WokeHold = 1;
        }
        hcdSuspTake(t, pdo, irp,
                    useMark ? HCD_SUSP_TAKE_WAKE_OK : HCD_SUSP_TAKE_WAKE_NS);
        claimed++;
    }
    return claimed;
}

/* The wake event's claim, when the machine says one is due (5.4): in the
 * caller's hold. Controller lock held. */
static VOID hcdSuspClaimDue(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            PHCD_SUSP_TAKEN t)
{
    ULONG claimed;

    if (!dev->Susp.ClaimDue) {
        return;
    }
    claimed = hcdSuspClaimWakes(dev, 1, hc->WakeSeq, t);
    XhciSuspWakeCount(&dev->Susp, claimed);
}

/* ------------------------------------------------------------------ */
/* The gate and the restart (3.2)                                      */
/* ------------------------------------------------------------------ */

static VOID hcdSuspGateClose(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_PIPE pipe;
    KIRQL oldIrql;
    ULONG dci;
    ULONG mine;

    for (dci = 1; dci < 32; dci++) {
        pipe = hcdSuspPipe(dev, dci);
        if (pipe == NULL) {
            continue;
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        mine = pipe->SuspendPause ? 0UL : 1UL;
        pipe->SuspendPause = 1;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (mine) {
            HcdIoPipePause(hc, pipe);
        }
    }
}

/* Every endpoint the context reads Stopped, not Halted, with TDs on its
 * ring and no pause but the machine's, rung - each stream with work by
 * its Stream ID - then the gate opened. Under the lock the teardown's
 * freeze sets Gone under, as hcd_hub.c's release. */
static VOID hcdSuspGateOpen(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            ULONG restart)
{
    PHCD_PIPE pipe;
    PHCD_PIPE p;
    KIRQL oldIrql;
    ULONG state;
    ULONG dci;
    ULONG id;
    ULONG mine;

    for (dci = 1; dci < 32; dci++) {
        pipe = hcdSuspPipe(dev, dci);
        if (pipe == NULL) {
            continue;
        }
        state = restart ? hcdSuspEpState(hc, dev, dci)
                        : XHCI_EP_STATE_DISABLED;
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        mine = pipe->SuspendPause;
        if (mine && restart && !dev->Gone &&
            state == XHCI_EP_STATE_STOPPED && !pipe->Halted &&
            pipe->Paused == 1) {
            if (pipe->Queue->Count != 0) {
                pipe->SeqUsed = 1;
                XhciWriteDoorbell(&hc->Hc, dev->SlotId, dci);
            }
            for (id = 1;
                 pipe->Streams != NULL && id <= pipe->Streams->Count; id++) {
                p = pipe->Streams->Pipe[id];
                if (p != NULL && p->Queue->Count != 0) {
                    pipe->SeqUsed = 1;
                    XhciWriteDoorbell(&hc->Hc, dev->SlotId,
                                      XhciStreamDoorbell(dci, id));
                }
            }
        }
        pipe->SuspendPause = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        if (mine) {
            HcdIoPipeResume(hc, pipe);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The steps                                                           */
/* ------------------------------------------------------------------ */

/* One device request through the thread's control path, its outcome as a
 * step result (3.10). */
static ULONG hcdSuspControl(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                            UCHAR request)
{
    ULONG bytes;
    ULONG outcome;

    outcome = HcdThreadControlOutcome(hc, dev, HCD_SUSP_RT_DEVICE_OUT,
                                      request, HCD_SUSP_FEAT_REMOTE_WAKEUP,
                                      0, 0, &bytes);
    switch (outcome) {
    case HCD_CTL_DONE:
        return XHCI_SUSP_RES_OK;
    case HCD_CTL_STALLED:
        return XHCI_SUSP_RES_STALL;
    case HCD_CTL_ABANDONED:
        return XHCI_SUSP_RES_TIMEOUT;
    default:
        break;
    }
    if (dev->Ep0Stuck || hcdSuspHalted(hc)) {
        return XHCI_SUSP_RES_TIMEOUT;
    }
    return (outcome == HCD_CTL_NOT_SENT) ? XHCI_SUSP_RES_NOT_SENT
                                         : XHCI_SUSP_RES_FAILED;
}

/* EP0 busy for the arm: a client's transfer queued, or a stall whose
 * recovery (hcdEp0Quiet's Reset Endpoint) would run past the arm's bound -
 * that recovery is the next pass's, the device ACTIVE again after the
 * refusal (Codex review of 36.30, round 2, finding 3). */
static ULONG hcdSuspEp0Queued(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    KIRQL oldIrql;
    ULONG busy;

    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    busy = (dev->Ep0Queue.Count != 0 || dev->Ep0Halted) ? 1UL : 0UL;
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    return busy;
}

/* The arm (3.4 (2)): a first attempt is not sent behind a client's
 * transfer; a repeated one waits for EP0's queue to drain within `bound`
 * milliseconds (3.9). */
static ULONG hcdSuspArm(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG bound)
{
    KTIMER deadline;
    LARGE_INTEGER due;
    LARGE_INTEGER now;
    ULONG drained;

    if (hcdSuspEp0Queued(hc, dev) != 0 && bound == 0) {
        return XHCI_SUSP_RES_NOT_SENT;
    }
    /* An elapsed-time bound, a timer polled with a zero wait, as
     * hcdEp0Quiet's (Codex review of 36.30, round 1, finding 6). */
    KeInitializeTimer(&deadline);
    HcdRelativeMs(&due, bound);
    (VOID)KeSetTimer(&deadline, due, NULL);
    now.QuadPart = 0;
    drained = 0;
    for (;;) {
        if (hcdSuspEp0Queued(hc, dev) == 0) {
            drained = 1;
            break;
        }
        if (KeWaitForSingleObject(&deadline, Executive, KernelMode, FALSE,
                                  &now) == STATUS_SUCCESS) {
            break;
        }
        hcdSuspPause(1);
        HcdIoDeferred(hc);
    }
    (VOID)KeCancelTimer(&deadline);
    if (!drained) {
        return XHCI_SUSP_RES_NOT_SENT;
    }
    /* EP0 drained and its gate closed, so the request's own quiet wait
     * finds nothing to wait for. */
    return hcdSuspControl(hc, dev, HCD_SUSP_REQ_SET_FEATURE);
}

/* Stop Endpoint with SP=1 on every endpoint the context reads Running
 * (3.4 (3)). */
static ULONG hcdSuspStop(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    XHCI_TRB trb;
    ULONG control;
    ULONG code;
    ULONG dci;

    for (dci = 1; dci < 32; dci++) {
        if (hcdSuspPipe(dev, dci) == NULL ||
            hcdSuspEpState(hc, dev, dci) != XHCI_EP_STATE_RUNNING) {
            continue;
        }
        code = 0;
        if (XhciTrbStopEndpoint(&trb, dev->SlotId, dci, 1) == XHCI_RING_OK) {
            code = HcdThreadCommand(hc, &trb, &control);
        }
        if (code == XHCI_CC_SUCCESS || code == XHCI_CC_CONTEXT_STATE_ERROR) {
            continue;
        }
        return hcdSuspHalted(hc) ? XHCI_SUSP_RES_TIMEOUT
                                 : XHCI_SUSP_RES_FAILED;
    }
    HcdIoDeferred(hc);
    return XHCI_SUSP_RES_OK;
}

/* A hub port request's failure as a step result. */
static ULONG hcdSuspHubFailed(PHCD_CONTROLLER hc, PHCD_HUB hub)
{
    if (hcdSuspHalted(hc) || hub->Device == NULL || hub->Device->Ep0Stuck) {
        return XHCI_SUSP_RES_TIMEOUT;
    }
    return XHCI_SUSP_RES_FAILED;
}

/* The U3 request (3.4 (4)), its stale change acknowledged first. */
static ULONG hcdSuspU3(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                       PHCD_PORT q)
{
    ULONG portsc;
    ULONG status;
    ULONG change;

    if (q->Hub == NULL) {
        portsc = XhciReadPortsc(&hc->Hc, q->PortId);
        if (portsc == 0xFFFFFFFFUL) {
            return XHCI_SUSP_RES_TIMEOUT;
        }
        if ((portsc & XHCI_PORTSC_PLC) != 0) {
            XhciWritePortsc(&hc->Hc, q->PortId,
                            XhciPortscClearChanges(portsc, XHCI_PORTSC_PLC));
            portsc = XhciReadPortsc(&hc->Hc, q->PortId);
        }
        XhciWritePortsc(&hc->Hc, q->PortId, XhciPortscSuspend(portsc));
        XhciSuspU3Issued(&dev->Susp);
        return XHCI_SUSP_RES_OK;
    }
    if (!HcdHubPortStatus(hc, q->Hub, q->Number, &status, &change)) {
        return hcdSuspHubFailed(hc, q->Hub);
    }
    if ((change & XHCI_HUB_C_PORT_SUSPEND) != 0 &&
        !HcdHubPortFeature(hc, q->Hub, q->Number, 0,
                           XHCI_HUB_FEAT_C_PORT_SUSPEND)) {
        return hcdSuspHubFailed(hc, q->Hub);
    }
    XhciSuspU3Issued(&dev->Susp);
    if (!HcdHubPortFeature(hc, q->Hub, q->Number, 1,
                           XHCI_HUB_FEAT_PORT_SUSPEND)) {
        /* The request may have taken effect though its transfer failed:
         * nothing proves the link still U0, so it is uncertain, never a
         * refusal that would reopen the gate (Codex review of 36.30,
         * round 2, finding 1). */
        return XHCI_SUSP_RES_TIMEOUT;
    }
    return XHCI_SUSP_RES_OK;
}

/* The observation (3.4 (5)) or the verification (3.6), polled within
 * `bound` milliseconds by an elapsed-time timer. */
static ULONG hcdSuspPoll(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG verify,
                         ULONG bound)
{
    KTIMER deadline;
    LARGE_INTEGER due;
    LARGE_INTEGER now;
    ULONG answer;
    ULONG portsc;
    ULONG status;
    ULONG change;

    KeInitializeTimer(&deadline);
    HcdRelativeMs(&due, bound);
    (VOID)KeSetTimer(&deadline, due, NULL);
    now.QuadPart = 0;
    for (;;) {
        if (q->Hub == NULL) {
            portsc = XhciReadPortsc(&hc->Hc, q->PortId);
            if (portsc == 0xFFFFFFFFUL) {
                answer = XHCI_SUSP_RES_TIMEOUT;
                break;
            }
            answer = verify ? XhciSuspVerifyRoot(portsc)
                            : XhciSuspObserveRoot(portsc);
        } else {
            if (!HcdHubPortStatus(hc, q->Hub, q->Number, &status, &change)) {
                answer = hcdSuspHubFailed(hc, q->Hub);
                if (answer == XHCI_SUSP_RES_FAILED) {
                    answer = XHCI_SUSP_RES_TIMEOUT;
                }
                break;
            }
            if ((change & XHCI_HUB_C_PORT_SUSPEND) != 0 &&
                !HcdHubPortFeature(hc, q->Hub, q->Number, 0,
                                   XHCI_HUB_FEAT_C_PORT_SUSPEND)) {
                /* A transport failure stays uncertain whatever the sample
                 * said (round 1, finding 7). */
                answer = XHCI_SUSP_RES_TIMEOUT;
                break;
            }
            answer = verify ? XhciSuspVerifyHub(status, change)
                            : XhciSuspObserveHub(status, change);
        }
        if (answer != XHCI_SUSP_POLL_AGAIN) {
            break;
        }
        if (KeWaitForSingleObject(&deadline, Executive, KernelMode, FALSE,
                                  &now) == STATUS_SUCCESS) {
            answer = XHCI_SUSP_RES_EXPIRED;
            break;
        }
        hcdSuspPause((q->Hub == NULL) ? 1UL : HCD_SUSP_HUB_POLL_MS);
    }
    (VOID)KeCancelTimer(&deadline);
    return answer;
}

/* The resume's signal (3.6): PLS=Resume at a root port, or
 * ClearPortFeature(PORT_SUSPEND) at a hub port. */
static ULONG hcdSuspSignal(PHCD_CONTROLLER hc, PHCD_PORT q)
{
    ULONG portsc;

    if (q->Hub == NULL) {
        portsc = XhciReadPortsc(&hc->Hc, q->PortId);
        if (portsc == 0xFFFFFFFFUL) {
            return XHCI_SUSP_RES_TIMEOUT;
        }
        XhciWritePortsc(&hc->Hc, q->PortId, XhciPortscResumeSignal(portsc));
        return XHCI_SUSP_RES_OK;
    }
    if (!HcdHubPortFeature(hc, q->Hub, q->Number, 0,
                           XHCI_HUB_FEAT_PORT_SUSPEND)) {
        return hcdSuspHubFailed(hc, q->Hub);
    }
    return XHCI_SUSP_RES_OK;
}

static ULONG hcdSuspU0(PHCD_CONTROLLER hc, PHCD_PORT q)
{
    ULONG portsc;

    portsc = XhciReadPortsc(&hc->Hc, q->PortId);
    if (portsc == 0xFFFFFFFFUL) {
        return XHCI_SUSP_RES_TIMEOUT;
    }
    XhciWritePortsc(&hc->Hc, q->PortId, XhciPortscResumeDone(portsc));
    return XHCI_SUSP_RES_OK;
}

/* ------------------------------------------------------------------ */
/* A sequence                                                          */
/* ------------------------------------------------------------------ */

/* The sequence's end, in the hold that makes its state visible: a STALL's
 * WAIT_WAKEs claimed, then the refusal's latch (XhciSuspLatch's
 * contract), then a due wake claim. Controller lock held. */
static VOID hcdSuspEndLocked(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                             PHCD_SUSP_TAKEN t)
{
    ULONG n;

    if (dev->Susp.State == XHCI_SUSP_ACTIVE &&
        dev->Susp.Outcome == XHCI_SUSP_OUT_REFUSED &&
        dev->Susp.Reason == XHCI_SUSP_WHY_STALL) {
        (VOID)hcdSuspClaimWakes(dev, 0, 0, t);
    }
    if (dev->Susp.LatchDue) {
        n = hcdSuspRead(dev, hc->SuspPdos, hc->SuspFuncs);
        XhciSuspLatch(&dev->Susp, hc->SuspFuncs, n);
    }
    hcdSuspClaimDue(hc, dev, t);
}

/*
 * Run the sequence whose first step `step` (with its wait `wait`) the
 * caller began, to its end. Returns the outcome. The IRPs a claim takes
 * are completed between steps, outside the lock.
 */
static ULONG hcdSuspRun(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev, ULONG step,
                        ULONG wait)
{
    HCD_SUSP_TAKEN taken;
    PHCD_PORT q;
    KIRQL oldIrql;
    ULONG result;
    ULONG group;

    taken.Count = 0;
    q = hcdSuspPort(hc, dev);
    while (step != XHCI_SUSP_STEP_END) {
        result = XHCI_SUSP_RES_TIMEOUT;
        if (q == NULL || hcdSuspHalted(hc)) {
            result = XHCI_SUSP_RES_TIMEOUT;
        } else {
            switch (step) {
            case XHCI_SUSP_STEP_GATE:
                hcdSuspGateClose(hc, dev);
                result = XHCI_SUSP_RES_OK;
                break;
            case XHCI_SUSP_STEP_ARM:
                result = hcdSuspArm(hc, dev, wait);
                break;
            case XHCI_SUSP_STEP_STOP:
                result = hcdSuspStop(hc, dev);
                break;
            case XHCI_SUSP_STEP_U3:
                result = hcdSuspU3(hc, dev, q);
                break;
            case XHCI_SUSP_STEP_OBSERVE:
                result = hcdSuspPoll(hc, q, 0, wait);
                break;
            case XHCI_SUSP_STEP_SIGNAL:
                hcdSuspHold(hc, wait);
                result = hcdSuspSignal(hc, q);
                break;
            case XHCI_SUSP_STEP_U0:
                hcdSuspHold(hc, wait);
                result = hcdSuspU0(hc, q);
                break;
            case XHCI_SUSP_STEP_VERIFY:
                result = hcdSuspPoll(hc, q, 1, wait);
                break;
            case XHCI_SUSP_STEP_RECOVERY:
                hcdSuspHold(hc, wait);
                result = XHCI_SUSP_RES_OK;
                break;
            case XHCI_SUSP_STEP_DISARM:
                result = hcdSuspControl(hc, dev, HCD_SUSP_REQ_CLEAR_FEATURE);
                break;
            case XHCI_SUSP_STEP_RESTART:
                hcdSuspHold(hc, wait);
                hcdSuspGateOpen(hc, dev, 1);
                result = XHCI_SUSP_RES_OK;
                break;
            default:
                break;
            }
        }
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        step = XhciSuspAdvance(&dev->Susp, result, hc->SuspClockMs, &wait);
        if (step == XHCI_SUSP_STEP_END) {
            hcdSuspEndLocked(hc, dev, &taken);
        } else {
            hcdSuspClaimDue(hc, dev, &taken);
        }
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        hcdSuspFinish(&taken);
    }
    XHCI_DBG_VALUE("hcd: suspend sequence, slot/kind/outcome/state",
                   (dev->SlotId << 24) | (dev->Susp.Kind << 16) |
                       (dev->Susp.Outcome << 8) | dev->Susp.State);
    switch (dev->Susp.State) {
    case XHCI_SUSP_ACTIVE:
        HcdCfgUndefer(hc, dev);
        break;
    case XHCI_SUSP_RECOVERING:
    case XHCI_SUSP_GONE:
        if (!hcdSuspHalted(hc)) {
            /* The device cycle re-establishes it, or takes a device the
             * machine saw leave off its port when the port's own
             * processing would not (a PED loss with CCS kept): either way
             * the old device is dropped and its slots settled at the
             * unlink (3.10; round 1, finding 3). Named by its PDO group,
             * so a replacement is not touched. */
            group = hcdSuspPdo(dev->Pdo) != NULL
                        ? hcdSuspPdo(dev->Pdo)->Group
                        : 0;
            HcdEnumCycle(hc, dev->Location, group);
        }
        break;
    default:
        break;
    }
    return dev->Susp.Outcome;
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

/*
 * One decision for one device, and what it begins, in one hold (3.9):
 * the slots copied, decided, written back, the IRPs to complete claimed,
 * and a suspend begun. Returns whether a sequence ran.
 */
static ULONG hcdSuspDecideOne(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev)
{
    PHCD_DEVICE_PDO *pdos;
    PXHCI_SUSP_FUNC funcs;
    XHCI_SUSP_DECISION d;
    HCD_SUSP_TAKEN taken;
    PHCD_PORT q;
    KIRQL oldIrql;
    ULONG step;
    ULONG wait;
    ULONG kind;
    ULONG n;
    ULONG i;

    pdos = hc->SuspPdos;
    funcs = hc->SuspFuncs;
    taken.Count = 0;
    step = XHCI_SUSP_STEP_END;
    wait = 0;
    kind = 0;
    q = hcdSuspPort(hc, dev);
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (dev->SuspWakeDue && dev->Susp.State == XHCI_SUSP_SUSPENDED) {
        /* A wake event taken at the acknowledgement or the hub look. */
        dev->SuspWakeDue = 0;
        kind = XHCI_SUSP_RUN_WAKE;
        step = XhciSuspBegin(&dev->Susp, kind, 0, 0, hc->SuspClockMs, &wait);
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        (VOID)hcdSuspRun(hc, dev, step, wait);
        return 1;
    }
    dev->SuspWakeDue = 0;
    n = hcdSuspRead(dev, pdos, funcs);
    (VOID)XhciSuspDecide(&dev->Susp, funcs, n, hc->PowerEpoch,
                         hc->PowerDown, &d);
    for (i = 0; i < n; i++) {
        pdos[i]->WokeHold = funcs[i].WokeHold;
        if (funcs[i].ToD0) {
            pdos[i]->PwrState = XHCI_SUSP_D0;
        }
        if ((funcs[i].CompleteDx || funcs[i].CompleteD0) &&
            pdos[i]->PwrIrp != NULL) {
            hcdSuspTake(&taken, pdos[i], pdos[i]->PwrIrp,
                        funcs[i].CompleteD0 ? HCD_SUSP_TAKE_D0
                                            : HCD_SUSP_TAKE_DX);
            pdos[i]->PwrIrp = NULL;
        }
    }
    if (q != NULL && d.Do != XHCI_SUSP_DO_NONE) {
        dev->Susp.Generation = q->ConnectGen;
        dev->Susp.Root = (q->Hub == NULL) ? 1UL : 0UL;
        if (d.Do == XHCI_SUSP_DO_SUSPEND) {
            kind = XHCI_SUSP_RUN_SUSPEND;
            step = XhciSuspBegin(&dev->Susp, kind, d.ArmIntent, 0,
                                 hc->SuspClockMs, &wait);
        } else {
            kind = XHCI_SUSP_RUN_RESUME;
            step = XhciSuspBegin(&dev->Susp, kind, 0,
                                 d.Do == XHCI_SUSP_DO_REARM ? 1UL : 0UL,
                                 hc->SuspClockMs, &wait);
        }
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdSuspFinish(&taken);
    if (kind == 0) {
        return 0;
    }
    (VOID)hcdSuspRun(hc, dev, step, wait);
    return 1;
}

/* The pass over every device (3.10). Thread only, powered. */
VOID HcdSuspService(PHCD_CONTROLLER hc)
{
    PHCD_USB_DEVICE dev;
    ULONG slot;
    ULONG i;

    if (!hc->SuspOn || !hc->RootHubStarted) {
        return;
    }
    for (slot = 1; slot <= XHCI_MAX_SLOTS; slot++) {
        for (i = 0; i < HCD_SUSP_PASSES; i++) {
            if (hcdSuspHalted(hc)) {
                return;
            }
            dev = hc->SlotDevice[slot];
            if (dev == NULL || !hcdSuspServes(hc, dev)) {
                break;
            }
            if (!hcdSuspDecideOne(hc, dev)) {
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Evidence (3.7, 3.8)                                                 */
/* ------------------------------------------------------------------ */

/* Whether the machine owns the device on port q now (any state but
 * ACTIVE and GONE). Thread only. */
static PHCD_USB_DEVICE hcdSuspOwner(PHCD_CONTROLLER hc, PHCD_PORT q)
{
    PHCD_USB_DEVICE dev;

    dev = q->Device;
    if (!hc->SuspOn || dev == NULL || dev->Gone ||
        dev->Susp.State == XHCI_SUSP_ACTIVE ||
        dev->Susp.State == XHCI_SUSP_GONE) {
        return NULL;
    }
    return dev;
}

ULONG HcdSuspOwnsPort(PHCD_CONTROLLER hc, PHCD_PORT q)
{
    return hcdSuspOwner(hc, q) != NULL;
}

/* One classified sample to the machine; a wake event's claim in the same
 * hold, its sequence owed to the pass. */
static VOID hcdSuspSample(PHCD_CONTROLLER hc, PHCD_USB_DEVICE dev,
                          ULONG event)
{
    HCD_SUSP_TAKEN taken;
    KIRQL oldIrql;

    taken.Count = 0;
    XhciControllerLockAcquire(&hc->Hc, &oldIrql);
    if (XhciSuspEvidence(&dev->Susp, event)) {
        hcdSuspClaimDue(hc, dev, &taken);
        dev->SuspWakeDue = 1;
    }
    XhciControllerLockRelease(&hc->Hc, oldIrql);
    hcdSuspFinish(&taken);
    if (event == XHCI_SUSP_EV_DISCONNECT &&
        dev->Susp.State == XHCI_SUSP_GONE && hcdSuspPdo(dev->Pdo) != NULL) {
        /* Gone to the machine: dropped through the cycle, which an unplug
         * the port's processing also sees makes no difference to, and
         * which a PED loss with CCS kept needs (round 1, finding 3). */
        HcdEnumCycle(hc, dev->Location, hcdSuspPdo(dev->Pdo)->Group);
    }
    if (dev->SuspWakeDue) {
        HcdThreadWake(hc);
    }
}

/* The root port's acknowledgement hands its sample over (3.7): PORTSC as
 * read, the change bits acknowledged. Connection processing goes on
 * unchanged after it. */
VOID HcdSuspRootSample(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG portsc,
                       ULONG acked)
{
    PHCD_USB_DEVICE dev;

    dev = hcdSuspOwner(hc, q);
    if (dev == NULL) {
        return;
    }
    hcdSuspSample(hc, dev,
                  XhciSuspRootEvidence(&dev->Susp, portsc, acked,
                                       q->ConnectGen));
}

/* A USB 2.0 hub port's finished resume handed over (3.8). `acked` the
 * change bits whose clear the hub took; when C_PORT_SUSPEND was seen and
 * its clear failed (`lost`), the hub may have taken it and report no more:
 * the wake would be stranded, so the device is uncertain and cycled, its
 * gate kept closed (Codex review of 36.30, round 3, finding 1). */
VOID HcdSuspHubSample(PHCD_CONTROLLER hc, PHCD_PORT q, ULONG status,
                      ULONG acked, ULONG lost)
{
    PHCD_USB_DEVICE dev;
    PHCD_DEVICE_PDO pdo;
    KIRQL oldIrql;

    dev = hcdSuspOwner(hc, q);
    if (dev == NULL) {
        return;
    }
    if (lost) {
        /* RECOVERING first, in the hold the dispatch reads it under, so
         * nothing resumes it or opens its gate before the cycle drops it
         * (Codex review of 36.30, round 4, finding 1). */
        XhciControllerLockAcquire(&hc->Hc, &oldIrql);
        XhciSuspUncertain(&dev->Susp);
        dev->SuspWakeDue = 0;
        XhciControllerLockRelease(&hc->Hc, oldIrql);
        pdo = hcdSuspPdo(dev->Pdo);
        if (pdo != NULL && !hcdSuspHalted(hc)) {
            HcdEnumCycle(hc, dev->Location, pdo->Group);
        }
        return;
    }
    hcdSuspSample(hc, dev,
                  XhciSuspHubEvidence(&dev->Susp, status, acked,
                                      q->ConnectGen));
}

/* ------------------------------------------------------------------ */
/* For the rest of the bus                                             */
/* ------------------------------------------------------------------ */

/* A new device's record: ACTIVE, its deferral list empty. */
VOID HcdSuspDeviceInit(PHCD_USB_DEVICE dev, PHCD_PORT port)
{
    XhciSuspLinkInit(&dev->Susp, port->Hub == NULL, port->ConnectGen);
    InitializeListHead(&dev->SlowDeferred);
    dev->SuspWakeDue = 0;
}

/* Whether the device's link is ACTIVE: the survivor restart's doorbell and
 * the slow-IRP service ask. Controller lock held or thread. */
ULONG HcdSuspLinkActive(PHCD_USB_DEVICE dev)
{
    return dev->Susp.State == XHCI_SUSP_ACTIVE;
}

/* Whether a slow IRP for the device waits (3.3): the link not ACTIVE -
 * SUSPENDED, or RECOVERING, whose device the cycle drops - and the request
 * not a host-side one. `abortOnly` the request is ABORT_PIPE. Thread
 * only. */
ULONG HcdSuspDefers(PHCD_USB_DEVICE dev, ULONG abortOnly)
{
    return dev->Susp.State != XHCI_SUSP_ACTIVE && !abortOnly;
}

/* A device leaving: the machine GONE (its slots are settled at the
 * unlink, 36.31). Thread only. */
VOID HcdSuspDeviceGone(PHCD_USB_DEVICE dev)
{
    XhciSuspGone(&dev->Susp);
    dev->SuspWakeDue = 0;
}
