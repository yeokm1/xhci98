/*
 * test_cmd.c - host tests for the command engine itself (src/xhci_cmd.c),
 * driven through its own entry points, for task 50.30's correction (design
 * record 17 section 4.15.9, "the engine's correction"): a Command Ring
 * Stopped event naming the TRB the same abort's Command Aborted event has
 * just ended rewrites it as a No Op Command before the ring runs again.
 *
 * test_ring's test_stop_abandoned pins the selector (XhciRingStopAbandoned)
 * alone. This suite runs the engine around it: the submit, the watchdog's
 * rung 1 and its CA write, the three completion paths of XhciCommandEvent,
 * the invalidation, and the two command-ring rebuilds of src/xhci_init.c
 * (xhciProgramCommandRing, the start's and a recovery's, and
 * xhciRestoreState). Both files are compiled into this program unchanged,
 * by #include, so their static functions are the driver's own; nothing in
 * src/ is edited for the test, and nothing in src/ is reached by any other
 * route than the driver's.
 *
 * WHAT IS MODELLED, AND WHERE. The engine's outward calls are this file's:
 *
 *   - MMIO (XhciRead32 and the rest of src/xhci_pci.c): a register file in
 *     which CRCR answers CRR while the command ring runs, a CA write is
 *     counted, and a pointer write made while CRR is 0 repositions the
 *     modelled controller's own dequeue (Table 5-24, p.368).
 *   - The command doorbell: the modelled controller fetches from its own
 *     dequeue while the TRB's cycle bit matches its cycle state, follows the
 *     Link TRB (toggling on TC), and queues one Command Completion Event per
 *     command it executes. `hwWedged` makes a doorbell fetch nothing - the
 *     command doorbelled but never fetched, which is the case the rewrite
 *     exists for.
 *   - The abort's events are posted by the test (`postAborted`,
 *     `postStopped`), each naming the TRB the scenario says; the
 *     controller's dequeue is put where the stop says it stopped.
 *   - Events reach the engine through `drain`, which holds the controller
 *     lock across XhciCommandEvent as the HCD's drain does.
 *   - The timer service keeps each arm's callback and context; `fireTimer`
 *     runs the last one, which is how the watchdog's rung 1 is reached.
 *   - The slot layer's three hooks and the reset request only count.
 *
 * The model is this project's reading of 4.6.1 to 4.6.1.2, not a
 * controller: it confirms the engine does what the record says, and it
 * replaces no guest leg. Expected TRB types and completion codes are
 * written as literals (design record 03): a test that says
 * XHCI_TRB_TYPE(23) would agree with the driver even if both were wrong.
 *
 * Build and run:  test\run-host-tests.cmd
 * Exit code = number of failed checks (0 = pass).
 *
 * C89, no framework.
 */

#include <stdio.h>
#include <string.h>
#include "../src/xhci.h"
#include "test_harness.h"

/* ------------------------------------------------------------------ */
/* The four kernel names the engine's files call that xhci_compat.h   */
/* does not declare                                                    */
/* ------------------------------------------------------------------ */

#define UNREFERENCED_PARAMETER(p) ((void)(p))

static ULONG lockDepth;
static ULONG lockNested;
static ULONG lockAcquires;

static VOID KeAcquireSpinLock(PKSPIN_LOCK lock, PKIRQL oldIrql)
{
    (void)lock;
    lockAcquires++;
    if (lockDepth != 0) {
        lockNested++;
    }
    lockDepth++;
    *oldIrql = 0;
}

static VOID KeReleaseSpinLock(PKSPIN_LOCK lock, KIRQL oldIrql)
{
    (void)lock;
    (void)oldIrql;
    if (lockDepth != 0) {
        lockDepth--;
    }
}

static LONG InterlockedIncrement(LONG *value)
{
    *value = *value + 1;
    return *value;
}

static VOID KeStallExecutionProcessor(ULONG microseconds)
{
    (void)microseconds;
}

/* The engine and the ring setup it is rebuilt by, as the driver has them. */
#include "../src/xhci_cmd.c"
#include "../src/xhci_init.c"

/* ------------------------------------------------------------------ */
/* Expectations, by hand                                               */
/* ------------------------------------------------------------------ */

#define RING_PA         0x0F001000UL    /* 64-byte aligned, below 4 GB */
#define RING_TRBS       16UL
#define LINK_INDEX      (RING_TRBS - 1UL)
#define OPS             0x20UL          /* CAPLENGTH of the modelled BAR */

#define T_MASK          0x0000FC00UL
#define T_ENABLE_SLOT   0x00002400UL    /*  9 << 10 */
#define T_DISABLE_SLOT  0x00002800UL    /* 10 << 10 */
#define T_NOOP_CMD      0x00005C00UL    /* 23 << 10 */
#define T_LINK          0x00001800UL    /*  6 << 10 */
#define T_COMPLETION    0x00008400UL    /* 33 << 10 */

#define CC_SUCCESS      1UL
#define CC_SHORT_PACKET 13UL            /* not a command ring code */
#define CC_STOPPED      24UL
#define CC_ABORTED      25UL

#define CRCR_CA         0x00000004UL
#define CRCR_CRR        0x00000008UL

#define PA(i)           (RING_PA + (i) * 16UL)

/* ------------------------------------------------------------------ */
/* The modelled controller                                             */
/* ------------------------------------------------------------------ */

static XHCI_EXTENSION gExt;
static ULONG gBuf[1024];        /* the common buffer; the ring at offset 0 */

static ULONG hwCrr;
static ULONG hwUsbsts;
static ULONG hwAborts;
static ULONG hwCrcrWrites;
static ULONG hwDoorbells;
static ULONG hwWedged;
static ULONG hwDeq;
static ULONG hwCcs;
static ULONG hwCode;            /* the code the next execution answers */

#define RUN_MAX 128
static ULONG hwRunIndex[RUN_MAX];
static ULONG hwRunType[RUN_MAX];
static ULONG hwRunCount;

#define EVQ_MAX 64
static XHCI_TRB evq[EVQ_MAX];
static ULONG evCount;

/* The timer service: every arm kept, the last fired. */
#define TIMER_MAX 16
static XHCI_ASYNC_TIMER_CALLBACK *timerCb[TIMER_MAX];
static XHCI_COMMAND_TIMEOUT timerCtx[TIMER_MAX];
static ULONG timerCount;

/* The slot layer and the reset service. */
static ULONG slotEvents;
static ULONG slotLastCode;
static ULONG slotLastControl;
static ULONG slotLost;
static ULONG slotAbortEnded;
static ULONG slotAbortEndedPA;
static ULONG slotAbortEndedHow;
static ULONG slotFatal;
static ULONG resetRequests;
static ULONG whyStaged;
static ULONG lockWord;

static volatile XHCI_TRB *ringTrb(ULONG index)
{
    return ((volatile XHCI_TRB *)gBuf) + index;
}

static ULONG trbType(ULONG index)
{
    return ringTrb(index)->Control & T_MASK;
}

static ULONG trbCycle(ULONG index)
{
    return ringTrb(index)->Control & 1UL;
}

static void postEvent(ULONG pa, ULONG code, ULONG slot)
{
    XHCI_TRB *ev;

    if (evCount >= EVQ_MAX) {
        CHECK(0, "event queue overflow");
        return;
    }
    ev = &evq[evCount++];
    ev->Param0 = pa;
    ev->Param1 = 0;
    ev->Status = code << 24;
    ev->Control = (slot << 24) | T_COMPLETION | 1UL;
}

/* Fetch and execute from the controller's own dequeue (4.6.1). */
static void hwExecute(void)
{
    ULONG guard;
    ULONG control;
    ULONG type;

    for (guard = 0; guard < 2UL * RING_TRBS; guard++) {
        control = ringTrb(hwDeq)->Control;
        if ((control & 1UL) != hwCcs) {
            return;
        }
        type = control & T_MASK;
        if (type == T_LINK) {
            if ((control & 2UL) != 0) {
                hwCcs ^= 1UL;
            }
            hwDeq = 0;
            continue;
        }
        if (hwRunCount < RUN_MAX) {
            hwRunIndex[hwRunCount] = hwDeq;
            hwRunType[hwRunCount] = type;
            hwRunCount++;
        }
        postEvent(PA(hwDeq), hwCode,
                  (type == T_ENABLE_SLOT) ? 1UL
                                          : ((control >> 24) & 0xFFUL));
        hwDeq++;
    }
    CHECK(0, "the modelled controller did not come to rest");
}

/* How many times the controller executed index `i` as type `t`. */
static ULONG ran(ULONG i, ULONG t)
{
    ULONG k;
    ULONG n;

    n = 0;
    for (k = 0; k < hwRunCount; k++) {
        if (hwRunIndex[k] == i && hwRunType[k] == t) {
            n++;
        }
    }
    return n;
}

static void postAborted(ULONG index)
{
    postEvent(PA(index), CC_ABORTED, 0);
}

/* The ring stops; the controller's dequeue is where the stop says. */
static void postStopped(ULONG index)
{
    hwCrr = 0;
    hwDeq = index;
    postEvent(PA(index), CC_STOPPED, 0);
}

/* The DPC's drain: the lock held across the engine, one pass. Returns how
 * many events asked for the controller reset. */
static ULONG drain(void)
{
    KIRQL irql;
    ULONG resets;
    ULONG i;

    resets = 0;
    XhciControllerLockAcquire(&gExt, &irql);
    for (i = 0; i < evCount; i++) {
        resets += XhciCommandEvent(&gExt, &evq[i]);
    }
    evCount = 0;
    XhciControllerLockRelease(&gExt, irql);
    return resets;
}

static void fireTimer(void)
{
    XHCI_ASYNC_TIMER_CALLBACK *cb;
    XHCI_COMMAND_TIMEOUT ctx;

    if (timerCount == 0) {
        CHECK(0, "no timer armed to fire");
        return;
    }
    timerCount--;
    cb = timerCb[timerCount];
    ctx = timerCtx[timerCount];
    cb(&gExt, &ctx);
}

/* ------------------------------------------------------------------ */
/* src/xhci_pci.c: the register file                                   */
/* ------------------------------------------------------------------ */

static ULONG capRead(ULONG off)
{
    switch (off) {
    case 0x00: return 0x01000020UL;     /* HCIVERSION 1.0, CAPLENGTH 20h */
    case 0x04: return 0x04000120UL;     /* 4 ports, 1 interrupter, 32 slots */
    case 0x14: return 0x00000800UL;     /* DBOFF */
    case 0x18: return 0x00000600UL;     /* RTSOFF */
    default:   return 0;
    }
}

ULONG XhciRead32(PXHCI_EXTENSION ext, ULONG barOffset)
{
    (void)ext;
    if (barOffset == OPS + XHCI_OP_CRCR) {
        return hwCrr ? CRCR_CRR : 0UL;
    }
    if (barOffset == OPS + XHCI_OP_USBSTS) {
        return hwUsbsts;
    }
    if (barOffset < OPS) {
        return capRead(barOffset);
    }
    return 0;
}

VOID XhciWrite32(PXHCI_EXTENSION ext, ULONG barOffset, ULONG value)
{
    (void)ext;
    if (barOffset != OPS + XHCI_OP_CRCR) {
        return;
    }
    if ((value & CRCR_CA) != 0) {
        hwAborts++;
        return;
    }
    if (hwCrr) {
        return;     /* the pointer is latched only while CRR is 0 */
    }
    hwCrcrWrites++;
    hwDeq = ((value & 0xFFFFFFC0UL) - RING_PA) / 16UL;
    hwCcs = value & 1UL;
}

VOID XhciWrite64(PXHCI_EXTENSION ext, ULONG barOffset, ULONG low)
{
    XhciWrite32(ext, barOffset, low);
}

ULONG XhciReadOp(PXHCI_EXTENSION ext, ULONG opOffset)
{
    return XhciRead32(ext, OPS + opOffset);
}

VOID XhciWriteOp(PXHCI_EXTENSION ext, ULONG opOffset, ULONG value)
{
    XhciWrite32(ext, OPS + opOffset, value);
}

ULONG XhciReadRt(PXHCI_EXTENSION ext, ULONG rtOffset)
{
    (void)ext;
    (void)rtOffset;
    return 0;
}

ULONG XhciReadIr0(PXHCI_EXTENSION ext, ULONG irOffset)
{
    (void)ext;
    (void)irOffset;
    return 0;
}

VOID XhciWriteIr0(PXHCI_EXTENSION ext, ULONG irOffset, ULONG value)
{
    (void)ext;
    (void)irOffset;
    (void)value;
}

ULONG XhciReadPortsc(PXHCI_EXTENSION ext, ULONG port)
{
    (void)ext;
    (void)port;
    return 0;
}

VOID XhciWritePortsc(PXHCI_EXTENSION ext, ULONG port, ULONG value)
{
    (void)ext;
    (void)port;
    (void)value;
}

VOID XhciWriteDoorbell(PXHCI_EXTENSION ext, ULONG slot, ULONG value)
{
    (void)ext;
    (void)value;
    hwDoorbells++;
    if (slot != 0) {
        return;
    }
    hwCrr = 1;
    if (!hwWedged) {
        hwExecute();
    }
}

ULONG XhciBarReader(PVOID context, ULONG byteOffset)
{
    return XhciRead32((PXHCI_EXTENSION)context, byteOffset);
}

MPSTATUS XhciReadPciConfig(PXHCI_EXTENSION ext, ULONG offset, PVOID buffer,
                           ULONG length)
{
    (void)ext;
    (void)offset;
    memset(buffer, 0, length);
    return MP_STATUS_SUCCESS;
}

MPSTATUS XhciWritePciConfig(PXHCI_EXTENSION ext, ULONG offset, PVOID buffer,
                            ULONG length)
{
    (void)ext;
    (void)offset;
    (void)buffer;
    (void)length;
    return MP_STATUS_SUCCESS;
}

ULONG XhciWaitForBits(PXHCI_EXTENSION ext, ULONG barOffset, ULONG mask,
                      ULONG want, ULONG timeoutMs, ULONG *lastValue)
{
    (void)ext;
    (void)barOffset;
    (void)mask;
    (void)want;
    (void)timeoutMs;
    if (lastValue != NULL) {
        *lastValue = 0;
    }
    return 1;
}

VOID XhciDelayMs(PXHCI_EXTENSION ext, ULONG milliseconds)
{
    (void)ext;
    (void)milliseconds;
}

/* ------------------------------------------------------------------ */
/* The HCD's services and the slot layer: counted only                 */
/* ------------------------------------------------------------------ */

ULONG XhciResourcesRequired = 0;

PKSPIN_LOCK HcdSvcControllerLock(PXHCI_EXTENSION ext)
{
    (void)ext;
    return (PKSPIN_LOCK)&lockWord;
}

ULONG HcdSvcArmTimer(PXHCI_EXTENSION ext, ULONG milliseconds, PVOID context,
                     ULONG contextLength, XHCI_ASYNC_TIMER_CALLBACK *callback)
{
    (void)ext;
    (void)milliseconds;
    CHECK(contextLength == sizeof(XHCI_COMMAND_TIMEOUT),
          "the engine arms with its own context");
    if (timerCount >= TIMER_MAX) {
        CHECK(0, "timer list overflow");
        return 1;
    }
    timerCb[timerCount] = callback;
    memcpy(&timerCtx[timerCount], context, sizeof(XHCI_COMMAND_TIMEOUT));
    timerCount++;
    return 0;
}

VOID HcdSvcRequestResetWhy(PXHCI_EXTENSION ext, ULONG reason, ULONG slot,
                           ULONG port, ULONG extra)
{
    (void)ext;
    (void)reason;
    (void)slot;
    (void)port;
    (void)extra;
    resetRequests++;
}

VOID HcdSvcRequestResetPlaced(PXHCI_EXTENSION ext, ULONG reason, ULONG extra,
                              const XHCI_RESET_REC *place)
{
    (void)ext;
    (void)reason;
    (void)extra;
    (void)place;
    resetRequests++;
}

VOID HcdSvcWhyStageLocked(PXHCI_EXTENSION ext, ULONG reason, ULONG slot,
                          ULONG extra)
{
    (void)ext;
    (void)reason;
    (void)slot;
    (void)extra;
    whyStaged++;
}

VOID HcdSvcDmaNotStopped(PXHCI_EXTENSION ext)
{
    (void)ext;
}

ULONG HcdSvcEventRing(PXHCI_EXTENSION ext, volatile XHCI_TRB **va, PULONG pa)
{
    (void)ext;
    *va = NULL;
    *pa = 0;
    return 0;
}

VOID XhciRootHubRetireOperations(PXHCI_EXTENSION ext)
{
    (void)ext;
}

VOID XhciRootHubDeferredWork(PXHCI_EXTENSION ext, ULONG armMode)
{
    (void)ext;
    (void)armMode;
}

ULONG XhciRootHubInit(PXHCI_EXTENSION ext, ULONG afterRestore)
{
    (void)ext;
    (void)afterRestore;
    return 0;
}

VOID XhciSlotCommandEvent(PXHCI_EXTENSION ext, ULONG completionCode,
                          ULONG control)
{
    (void)ext;
    slotEvents++;
    slotLastCode = completionCode;
    slotLastControl = control;
}

/* Task 50.30 step 3: the abort's clean end, as the device layer is told. */
VOID XhciSlotCommandAbortEnded(PXHCI_EXTENSION ext)
{
    slotAbortEnded++;
    slotAbortEndedPA = ext->CommandAbortEndedPA;
    slotAbortEndedHow = ext->CommandAbortEndedHow;
}

VOID XhciSlotCommandLost(PXHCI_EXTENSION ext)
{
    (void)ext;
    slotLost++;
}

VOID XhciSlotCommandSlotFatal(PXHCI_EXTENSION ext, ULONG completionCode,
                              ULONG control)
{
    (void)ext;
    (void)completionCode;
    (void)control;
    slotFatal++;
}

VOID XhciSlotDeferredWorkForced(PXHCI_EXTENSION ext, ULONG armMode)
{
    (void)ext;
    (void)armMode;
}

VOID XhciSlotInit(PXHCI_EXTENSION ext)
{
    (void)ext;
}

VOID XhciSlotInvalidateAll(PXHCI_EXTENSION ext, ULONG controllerStopped)
{
    (void)ext;
    (void)controllerStopped;
}

VOID XhciSlotResumeSweep(PXHCI_EXTENSION ext)
{
    (void)ext;
}

ULONG XhciSlotSaveBusy(PXHCI_EXTENSION ext)
{
    (void)ext;
    return 0;
}

ULONG XhciSlotSaveCommit(PXHCI_EXTENSION ext)
{
    (void)ext;
    return 1;
}

ULONG XhciSlotSaveSpoiled(PXHCI_EXTENSION ext)
{
    (void)ext;
    return 0;
}

VOID XhciEnableInterrupts(PXHCI_EXTENSION ext)
{
    (void)ext;
}

ULONG XhciEventDiscardStale(PXHCI_EXTENSION ext, PULONG fatalEvent)
{
    (void)ext;
    *fatalEvent = XHCI_RESTORE_FATAL_NONE;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scenario helpers                                                    */
/* ------------------------------------------------------------------ */

/* Admission as the start leaves it: initialized and running. */
static void admit(void)
{
    gExt.Flags |= XHCI_EXT_FLAG_INITIALIZED | XHCI_EXT_FLAG_RUNNING;
}

/* A start: the engine's init, then the start's command-ring setup
 * (xhciProgramCommandRing), with the controller halted, as hcd_ctl.c's
 * start and XhciInitController order them. */
static void startEngine(void)
{
    ULONG status;

    hwCrr = 0;
    XhciCommandInit(&gExt);
    gExt.Signature = XHCI_EXTENSION_SIGNATURE;
    gExt.TrailingSignature = XHCI_EXTENSION_TRAILING;
    status = xhciProgramCommandRing(&gExt);
    CHECK_EQ(status, XHCI_RING_OK, "the start's command ring is built");
    admit();
}

static void reset(void)
{
    memset(&gExt, 0, sizeof(gExt));
    memset(gBuf, 0, sizeof(gBuf));
    hwCrr = 0;
    hwUsbsts = 0;
    hwAborts = 0;
    hwCrcrWrites = 0;
    hwDoorbells = 0;
    hwWedged = 0;
    hwDeq = 0;
    hwCcs = 1;
    hwCode = CC_SUCCESS;
    hwRunCount = 0;
    evCount = 0;
    timerCount = 0;
    slotEvents = 0;
    slotLastCode = 0;
    slotLastControl = 0;
    slotLost = 0;
    slotAbortEnded = 0;
    slotAbortEndedPA = 0;
    slotAbortEndedHow = 0;
    slotFatal = 0;
    resetRequests = 0;
    whyStaged = 0;
    lockDepth = 0;
    lockNested = 0;

    gExt.StartVA = (ULONG_PTR)gBuf;
    gExt.StartPA = RING_PA;
    gExt.IoSpaceLength = 0x10000UL;
    gExt.HcInfoStatus = XHCI_HC_OK;
    gExt.HcInfo.OperationalOffset = OPS;
    gExt.HcInfo.RuntimeOffset = 0x600UL;
    gExt.Layout.CommandRingOffset = 0;
    gExt.Layout.CommandRingTrbs = RING_TRBS;
    startEngine();
}

/* Submit one command of `type`; returns its TRB index. */
static ULONG submit(ULONG type)
{
    XHCI_TRB trb;
    ULONG pa;
    ULONG status;

    memset(&trb, 0, sizeof(trb));
    trb.Control = type | ((type == T_DISABLE_SLOT) ? (1UL << 24) : 0UL);
    pa = 0;
    status = XhciCommandSubmit(&gExt, &trb, &pa, XHCI_ARM_UNLOCKED);
    CHECK_EQ(status, XHCI_CMD_OK, "the submit is accepted");
    return (pa - RING_PA) / 16UL;
}

/* Submit and let it complete normally, through a drain. */
static ULONG runOne(ULONG type)
{
    ULONG index;
    ULONG completed;

    completed = gExt.CommandsCompleted;
    index = submit(type);
    CHECK_EQ(drain(), 0, "an ordinary completion asks for no reset");
    CHECK_EQ(gExt.CommandsCompleted, completed + 1UL,
             "the command's own completion is matched");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE, "the engine is idle");
    timerCount = 0;     /* its watchdog would arrive stale */
    return index;
}

/* The watchdog's rung 1 for the outstanding command: CA written. */
static void timeOut(void)
{
    ULONG aborts;

    aborts = hwAborts;
    fireTimer();
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_ABORTING,
             "rung 1 leaves the engine aborting");
    CHECK_EQ(hwAborts, aborts + 1UL, "rung 1 writes CA on a running ring");
}

/* A submit is refused while the engine is not idle. */
static void checkBusy(const char *what)
{
    XHCI_TRB trb;
    ULONG pa;

    memset(&trb, 0, sizeof(trb));
    trb.Control = T_ENABLE_SLOT;
    pa = 0;
    CHECK_EQ(XhciCommandSubmit(&gExt, &trb, &pa, XHCI_ARM_UNLOCKED),
             XHCI_CMD_BUSY, what);
}

/*
 * The essential result after a rewrite at `x`: the next command is
 * submitted on the same ring, with no rebuild, and its doorbell restarts
 * the controller at `x`. The controller executes the rewritten No Op there
 * - never the command it replaced - and the engine retires it by its
 * marker, not as an unmatched event; then the next command runs and its
 * own completion is matched. Returns the next command's index.
 */
static ULONG restartAfterRewrite(ULONG x, ULONG replacedType)
{
    ULONG unmatched;
    ULONG completed;
    ULONG crcrWrites;
    ULONG slotsBefore;
    ULONG noOps;
    ULONG replaced;
    ULONG next;

    noOps = ran(x, T_NOOP_CMD);
    replaced = ran(x, replacedType);
    slotsBefore = slotEvents;
    unmatched = gExt.CommandsUnmatched;
    completed = gExt.CommandsCompleted;
    crcrWrites = hwCrcrWrites;
    hwWedged = 0;
    next = submit(T_DISABLE_SLOT);
    CHECK_EQ(hwCrcrWrites, crcrWrites, "no ring rebuild: CRCR not rewritten");
    CHECK_EQ(ran(x, T_NOOP_CMD), noOps + 1UL,
             "the rewritten No Op is executed once");
    CHECK_EQ(ran(x, replacedType), replaced,
             "the command it replaced is never executed");
    CHECK_EQ(ran(next, T_DISABLE_SLOT), 1, "the next command is executed");
    CHECK_EQ(drain(), 0, "neither completion asks for a reset");
    CHECK_EQ(gExt.CommandsUnmatched, unmatched,
             "the No Op's completion is not an unmatched event");
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, 0,
             "the No Op is retired by its marker");
    CHECK_EQ(gExt.CommandsCompleted, completed + 1UL,
             "the next command's own completion is matched");
    CHECK_EQ(slotEvents, slotsBefore + 1UL,
             "the slot layer is told of the next command, not the No Op");
    CHECK_EQ(gExt.LastCommandTrbPA, PA(next),
             "the completion matched is the next command's");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE, "the engine is idle");
    CHECK_EQ(gExt.CommandRing.Dequeue, gExt.CommandRing.Enqueue,
             "both retired: nothing left outstanding on the ring");
    timerCount = 0;
    return next;
}

/* ------------------------------------------------------------------ */
/* The orderings                                                       */
/* ------------------------------------------------------------------ */

/* An ordinary completion before the Command Aborted or the stop wins. */
static void test_completion_first(void)
{
    ULONG a;
    ULONG b;

    /* The ordinary race: the command completes between CA and the stop,
     * which then names the next TRB. */
    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    hwExecute();                         /* it completes after all */
    CHECK_EQ(drain(), 0, "completion first: no reset");
    CHECK_EQ(gExt.CommandsCompleted, 1, "completion first: matched");
    CHECK_EQ(slotLost, 0, "completion first: the slot layer is not told lost");
    CHECK_EQ(slotLastCode, CC_SUCCESS, "completion first: its own code");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_ABORTING,
             "completion first: aborting until the stop");
    postStopped(a + 1UL);
    CHECK_EQ(drain(), 0, "completion first, stop next: no reset");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE,
             "completion first, stop next: idle");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 0,
             "completion first, stop next: no rewrite");
    CHECK_EQ(trbType(a), T_ENABLE_SLOT, "completion first: its TRB kept");
    hwWedged = 0;
    b = runOne(T_DISABLE_SLOT);
    CHECK_EQ(b, a + 1UL, "completion first: the next command follows");
    CHECK_EQ(ran(a, T_ENABLE_SLOT), 1, "completion first: run once only");

    /* A late Command Aborted after the completion matches nothing, keeps
     * nothing, and a stop then naming the completed TRB does not select it
     * for the rewrite: the ring cannot hold that position (divergence 4). */
    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    hwExecute();
    CHECK_EQ(drain(), 0, "completion, late abort: the completion matched");
    postAborted(a);
    CHECK_EQ(drain(), 0, "completion, late abort: no reset");
    CHECK_EQ(gExt.CommandsUnmatched, 1,
             "completion, late abort: the abort matches nothing");
    CHECK_EQ(gExt.CommandAbortedPA, 0,
             "completion, late abort: no address kept");
    postStopped(a);
    CHECK_EQ(drain(), 1, "completion, late abort, stop on it: the reset");
    CHECK_EQ(gExt.Why.StagedExtra, 4,
             "completion, late abort, stop on it: divergence 4, not 3");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 0,
             "completion, late abort, stop on it: no rewrite");
    CHECK_EQ(trbType(a), T_ENABLE_SLOT,
             "completion, late abort, stop on it: the TRB untouched");
    CHECK_EQ(lockNested, 0, "completion first: the lock never nested");
}

/* Aborted, then Stopped at the same TRB: rewritten once; the ring restarts
 * with no rebuild, the No Op is retired and the next command completes. */
static void test_aborted_stopped_same(void)
{
    ULONG a;
    ULONG cycle;
    ULONG next;

    reset();
    (void)runOne(T_ENABLE_SLOT);         /* not at index 0 */
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    cycle = trbCycle(a);
    timeOut();
    postAborted(a);
    CHECK_EQ(drain(), 0, "same: the Command Aborted asks for no reset");
    CHECK_EQ(gExt.CommandTrbPA, 0, "same: the Aborted ends it outstanding");
    CHECK_EQ(gExt.CommandAbortedPA, PA(a), "same: the aborted TRB is kept");
    CHECK_EQ(gExt.CommandAbortedGen, gExt.CommandGeneration,
             "same: with its generation");
    CHECK_EQ(slotLost, 1, "same: the slot layer is told it is lost");
    checkBusy("same: no submit between the Aborted and the stop");
    postStopped(a);
    CHECK_EQ(drain(), 0, "same: the stop asks for no reset");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 1, "same: rewritten once");
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, PA(a), "same: the marker is set");
    CHECK_EQ(gExt.CommandAbortedPA, 0, "same: the kept address is consumed");
    CHECK_EQ(trbType(a), T_NOOP_CMD, "same: a No Op Command now");
    CHECK_EQ(trbCycle(a), cycle, "same: its cycle bit kept");
    CHECK_EQ(ringTrb(a)->Param0, 0, "same: its parameters cleared");
    CHECK_EQ(gExt.CommandRing.Dequeue, a, "same: the position adopted");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE, "same: idle");
    CHECK_EQ(gExt.CommandRingDiverged, 0, "same: no divergence");
    CHECK_EQ(resetRequests, 0, "same: no reset requested");
    CHECK_EQ(slotAbortEnded, 1, "same: the clean end told once");
    CHECK_EQ(slotAbortEndedPA, PA(a), "same: naming the aborted TRB");
    CHECK_EQ(slotAbortEndedHow, 1, "same: a Command Aborted event came");
    CHECK_EQ(gExt.CommandAbortEndedGen, gExt.CommandGeneration,
             "same: in its generation");

    next = restartAfterRewrite(a, T_ENABLE_SLOT);
    CHECK_EQ(next, a + 1UL, "same: the next command at the next TRB");
    /* And the ring stays in service after it. */
    (void)runOne(T_ENABLE_SLOT);
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 1,
             "same: one rewrite for one abort");
    CHECK_EQ(lockNested, 0, "same: the lock never nested");
}

/* Aborted, then Stopped at the next TRB: the position adopted, no rewrite. */
static void test_aborted_stopped_next(void)
{
    ULONG a;
    ULONG next;

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    CHECK_EQ(drain(), 0, "next: the Aborted, no reset");
    postStopped(a + 1UL);
    CHECK_EQ(drain(), 0, "next: the stop, no reset");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 0, "next: no rewrite");
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, 0, "next: no marker");
    CHECK_EQ(gExt.CommandAbortedPA, 0, "next: the kept address consumed");
    CHECK_EQ(trbType(a), T_ENABLE_SLOT, "next: the aborted TRB untouched");
    CHECK_EQ(gExt.CommandRing.Dequeue, a + 1UL, "next: the position adopted");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE, "next: idle");
    CHECK_EQ(slotAbortEnded, 1, "next: the clean end told");
    CHECK_EQ(slotAbortEndedPA, PA(a), "next: naming the aborted TRB");
    hwWedged = 0;
    next = runOne(T_DISABLE_SLOT);
    CHECK_EQ(next, a + 1UL, "next: the next command at the adopted TRB");
    CHECK_EQ(ran(a, T_ENABLE_SLOT), 0, "next: the aborted command never ran");
    CHECK_EQ(gExt.CommandsUnmatched, 0, "next: nothing unmatched");
}

/* Aborted, then Stopped at the next TRB while an earlier rewrite is still
 * unanswered: the aborted TRB is not the abandoned one unless the stop
 * names it, so the position is adopted as before the correction - no
 * second-abandonment divergence. */
static void test_aborted_next_with_marker(void)
{
    ULONG a;
    ULONG b;

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    postStopped(a);
    CHECK_EQ(drain(), 0, "next with a marker: the first rewrite");
    b = submit(T_DISABLE_SLOT);
    timeOut();
    postAborted(b);
    CHECK_EQ(drain(), 0, "next with a marker: the second Aborted");
    postStopped(b + 1UL);
    CHECK_EQ(drain(), 0, "next with a marker: no reset");
    CHECK_EQ(gExt.CommandRingDiverged, 0, "next with a marker: no divergence");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE,
             "next with a marker: idle");
    CHECK_EQ(trbType(b), T_DISABLE_SLOT, "next with a marker: b untouched");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 1,
             "next with a marker: one rewrite");
}

/* A Command Aborted this engine did not ask for (no abort outstanding: the
 * state is PENDING) keeps nothing: only the current abort's TRB is kept,
 * so a stop that then names it adopts the position without a rewrite. */
static void test_aborted_not_ours(void)
{
    ULONG a;

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    postAborted(a);
    CHECK_EQ(drain(), 0, "not ours: no reset");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_ABORTING,
             "not ours: the doorbell held until the stop");
    CHECK_EQ(gExt.CommandAbortedPA, 0, "not ours: nothing kept");
    postStopped(a);
    CHECK_EQ(drain(), 0, "not ours: the stop, no reset");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 0, "not ours: no rewrite");
    CHECK_EQ(slotAbortEnded, 0, "not ours: no clean end told");
    CHECK_EQ(trbType(a), T_ENABLE_SLOT, "not ours: the TRB untouched");
}

/* Stopped with no Command Aborted (4.6.1.2's note): the outstanding command
 * is the abandoned one, as before the correction. */
static void test_stopped_without_aborted(void)
{
    ULONG a;

    /* At the command itself: rewritten through the outstanding path. */
    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postStopped(a);
    CHECK_EQ(drain(), 0, "no Aborted, same: no reset");
    CHECK_EQ(slotLost, 1, "no Aborted, same: the stop ends it, lost");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 1,
             "no Aborted, same: rewritten");
    CHECK_EQ(trbType(a), T_NOOP_CMD, "no Aborted, same: a No Op Command");
    CHECK_EQ(slotAbortEndedPA, PA(a), "no Aborted, same: the clean end");
    CHECK_EQ(slotAbortEndedHow, 2, "no Aborted, same: the stop alone");
    (void)restartAfterRewrite(a, T_ENABLE_SLOT);

    /* At the next TRB: the command is lost, the position adopted. */
    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postStopped(a + 1UL);
    CHECK_EQ(drain(), 0, "no Aborted, next: no reset");
    CHECK_EQ(slotLost, 1, "no Aborted, next: lost");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 0, "no Aborted, next: kept");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE, "no Aborted, next: idle");
    CHECK_EQ(slotAbortEnded, 1, "no Aborted, next: the clean end told");
    CHECK_EQ(slotAbortEndedHow, 2, "no Aborted, next: the stop alone");
}

/* Stopped before a late Command Aborted: the stop ends the abort and the
 * late event matches nothing, keeps nothing and changes no state. */
static void test_stopped_before_late_aborted(void)
{
    ULONG a;

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postStopped(a);
    CHECK_EQ(drain(), 0, "late Aborted: the stop first, no reset");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 1,
             "late Aborted: rewritten by the stop (outstanding)");
    postAborted(a);
    CHECK_EQ(drain(), 0, "late Aborted: no reset");
    CHECK_EQ(gExt.CommandsUnmatched, 1, "late Aborted: unmatched");
    CHECK_EQ(gExt.CommandAbortedPA, 0, "late Aborted: no address kept");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE,
             "late Aborted: the engine stays idle");
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, PA(a),
             "late Aborted: the marker stands");
    (void)restartAfterRewrite(a, T_ENABLE_SLOT);

    /* The same with the stop at the next TRB. */
    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postStopped(a + 1UL);
    CHECK_EQ(drain(), 0, "late Aborted, next: no reset");
    postAborted(a);
    CHECK_EQ(drain(), 0, "late Aborted, next: no reset");
    CHECK_EQ(gExt.CommandAbortedPA, 0, "late Aborted, next: nothing kept");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE,
             "late Aborted, next: idle");
    hwWedged = 0;
    (void)runOne(T_DISABLE_SLOT);
    CHECK_EQ(ran(a, T_ENABLE_SLOT), 0, "late Aborted, next: never ran");
}

/* An ordinary completion after the Command Aborted does not reverse the
 * loss: before the stop it is unmatched, and the stop at the same TRB
 * rewrites it. After the rewrite, a completion at that address is taken as
 * the rewritten No Op's: the engine does not tell two completions of one
 * address apart, and the No Op's own one then finds nothing (counted
 * unmatched). Neither is a tolerance of arbitrary duplicate events; the
 * next command is matched by its own address either way. */
static void test_completion_after_aborted(void)
{
    ULONG a;
    ULONG next;

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    postEvent(PA(a), CC_SUCCESS, 1);
    CHECK_EQ(drain(), 0, "after Aborted: no reset");
    CHECK_EQ(gExt.CommandsUnmatched, 1,
             "after Aborted: the completion is unmatched");
    CHECK_EQ(gExt.CommandsCompleted, 0, "after Aborted: not a completion");
    CHECK_EQ(slotEvents, 0, "after Aborted: the slot layer not told success");
    CHECK_EQ(gExt.CommandAbortedPA, PA(a), "after Aborted: still kept");
    postStopped(a);
    CHECK_EQ(drain(), 0, "after Aborted, stop: no reset");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 1,
             "after Aborted, stop: the loss stands, rewritten");
    (void)restartAfterRewrite(a, T_ENABLE_SLOT);

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    CHECK_EQ(drain(), 0, "after rewrite: the Aborted");
    postStopped(a);
    CHECK_EQ(drain(), 0, "after rewrite: the stop");
    postEvent(PA(a), CC_SUCCESS, 1);
    CHECK_EQ(drain(), 0, "after rewrite: the late completion");
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, 0,
             "after rewrite: taken as the No Op's completion");
    CHECK_EQ(gExt.CommandsUnmatched, 0, "after rewrite: not unmatched");
    CHECK_EQ(slotEvents, 0, "after rewrite: the slot layer not told");
    hwWedged = 0;
    next = submit(T_DISABLE_SLOT);
    CHECK_EQ(ran(a, T_NOOP_CMD), 1, "after rewrite: the No Op still runs");
    CHECK_EQ(drain(), 0, "after rewrite: no reset");
    CHECK_EQ(gExt.CommandsUnmatched, 1,
             "after rewrite: the No Op's own completion is unmatched");
    CHECK_EQ(gExt.CommandsCompleted, 1,
             "after rewrite: the next command's completion matched");
    CHECK_EQ(gExt.LastCommandTrbPA, PA(next), "after rewrite: by its address");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE, "after rewrite: idle");
}

/* ------------------------------------------------------------------ */
/* The kept address's lifetime                                         */
/* ------------------------------------------------------------------ */

/* Every invalidation clears it (xhciCommandInvalidateLocked). */
static void test_lifetime_invalidation(void)
{
    ULONG a;
    ULONG gen;

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    CHECK_EQ(drain(), 0, "invalidation: the Aborted");
    CHECK_EQ(gExt.CommandAbortedPA, PA(a), "invalidation: kept");
    gen = gExt.CommandGeneration;
    XhciControllerBeginQuiesce(&gExt);
    CHECK_EQ(gExt.CommandAbortedPA, 0, "invalidation: cleared");
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, 0, "invalidation: no marker");
    CHECK_EQ(gExt.CommandGeneration, gen + 1UL,
             "invalidation: the generation moves");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_IDLE, "invalidation: idle");
    /* A stop arriving after it is one with no abort outstanding. */
    postStopped(a);
    CHECK_EQ(drain(), 1, "invalidation, then the stop: diverged");
    CHECK_EQ(gExt.Why.StagedExtra, 1, "invalidation, then the stop: reason 1");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 0,
             "invalidation, then the stop: no rewrite");
    CHECK_EQ(trbType(a), T_ENABLE_SLOT,
             "invalidation, then the stop: the TRB untouched");

    /* An invalidation that lands with a rewrite outstanding clears both. */
    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    postStopped(a);
    CHECK_EQ(drain(), 0, "invalidation after a rewrite: the events");
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, PA(a),
             "invalidation after a rewrite: marked");
    XhciControllerBeginQuiesce(&gExt);
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, 0,
             "invalidation after a rewrite: the marker cleared");
    CHECK_EQ(gExt.CommandAbortedPA, 0,
             "invalidation after a rewrite: nothing kept");
}

/*
 * The start's rebuild clears it (xhciProgramCommandRing). A start resets
 * the command generation to 0 (XhciCommandInit) and the ring to index 0,
 * so a command placed by the new start at the same address carries the
 * same generation as one aborted early in the previous start: here only
 * the rebuild's clear keeps the old address from matching it. The stop
 * then names that new command's TRB after its own completion; it must be
 * read as a position the ring cannot hold (divergence 4), never as the
 * old abort's TRB to rewrite (divergence 3).
 */
static void test_lifetime_start_rebuild(void)
{
    ULONG a;
    ULONG b;

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    CHECK_EQ(a, 0, "start rebuild: the first command at index 0");
    CHECK_EQ(gExt.CommandGeneration, 1, "start rebuild: generation 1");
    timeOut();
    postAborted(a);
    CHECK_EQ(drain(), 0, "start rebuild: the Aborted");
    CHECK_EQ(gExt.CommandAbortedPA, PA(0), "start rebuild: kept");

    /* A new start, with no stop event between. */
    startEngine();
    CHECK_EQ(gExt.CommandAbortedPA, 0, "start rebuild: cleared by the rebuild");
    CHECK_EQ(hwDeq, 0, "start rebuild: the controller at the base");

    timerCount = 0;
    b = submit(T_DISABLE_SLOT);
    CHECK_EQ(b, 0, "start rebuild: the new command at the same address");
    CHECK_EQ(gExt.CommandGeneration, 1, "start rebuild: the same generation");
    timeOut();
    hwWedged = 0;
    hwExecute();
    CHECK_EQ(drain(), 0, "start rebuild: its own completion first");
    postStopped(b);
    CHECK_EQ(drain(), 1, "start rebuild: the stop on it diverges");
    CHECK_EQ(gExt.Why.StagedExtra, 4,
             "start rebuild: divergence 4, not the old abort's rewrite");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 0,
             "start rebuild: no rewrite");
}

/* The restore's rebuild clears it (xhciRestoreState), the marker with it. */
static void test_lifetime_restore(void)
{
    ULONG a;

    reset();
    (void)runOne(T_ENABLE_SLOT);
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    CHECK_EQ(drain(), 0, "restore: the Aborted");
    CHECK_EQ(gExt.CommandAbortedPA, PA(a), "restore: kept");
    gExt.CommandNoOpRewrittenPA = PA(0);

    CHECK_EQ(XhciDeriveControllerInfo(&gExt, &gExt.HcInfo), XHCI_HC_OK,
             "restore: the modelled controller derives");
    hwCrr = 0;
    hwUsbsts = XHCI_USBSTS_HCH;
    gExt.SavedStateValid = 1;
    CHECK_EQ(xhciRestoreState(&gExt), 1, "restore: the restore succeeds");
    CHECK_EQ(gExt.CommandAbortedPA, 0, "restore: cleared by the rebuild");
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, 0, "restore: the marker cleared");
    CHECK_EQ(gExt.CommandRing.Enqueue, 0, "restore: the ring rebuilt");
    CHECK_EQ(hwDeq, 0, "restore: the controller at the base");
}

/* The generation: a kept address from another generation is not taken.
 * No live path reaches this - the generation moves only at a submit, which
 * is refused while aborting, at an invalidation and at a start, and both
 * of those clear the address - so the test moves the generation itself. */
static void test_generation(void)
{
    ULONG a;

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    CHECK_EQ(drain(), 0, "generation: the Aborted");
    gExt.CommandGeneration++;
    postStopped(a);
    CHECK_EQ(drain(), 0, "generation: no reset");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 0,
             "generation: another generation's address is not taken");
    CHECK_EQ(trbType(a), T_ENABLE_SLOT, "generation: the TRB untouched");
    CHECK_EQ(gExt.CommandAbortedPA, 0, "generation: consumed all the same");
}

/* Taken once: the stop that ends the abort consumes it, whether it
 * rewrites, adopts or diverges. */
static void test_once_only(void)
{
    ULONG a;
    ULONG b;

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    postStopped(a);
    CHECK_EQ(drain(), 0, "once: rewritten");
    CHECK_EQ(gExt.CommandAbortedPA, 0, "once: consumed by the rewrite");
    /* A second stop finds no abort outstanding. */
    postStopped(a);
    CHECK_EQ(drain(), 1, "once: a second stop diverges");
    CHECK_EQ(gExt.Why.StagedExtra, 1, "once: reason 1");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 1, "once: one rewrite");

    /* Consumed on the divergence path too (the second abandonment). */
    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    postStopped(a);
    CHECK_EQ(drain(), 0, "once, divergence: the first rewrite");
    b = submit(T_DISABLE_SLOT);
    timeOut();
    postAborted(b);
    CHECK_EQ(drain(), 0, "once, divergence: the second Aborted");
    postStopped(b);
    CHECK_EQ(drain(), 1, "once, divergence: the second abandonment diverges");
    CHECK_EQ(gExt.CommandAbortedPA, 0, "once, divergence: consumed");
}

/* ------------------------------------------------------------------ */
/* The divergences the newly abandoned TRB meets                       */
/* ------------------------------------------------------------------ */

/* A second abandonment while the first rewrite is unanswered: the reset,
 * the engine left aborting, nothing rewritten. */
static void test_double_abandonment(void)
{
    ULONG a;
    ULONG b;
    ULONG diverged;

    reset();
    hwWedged = 1;                        /* the No Op is never fetched */
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    postStopped(a);
    CHECK_EQ(drain(), 0, "double: the first rewrite");
    b = submit(T_DISABLE_SLOT);
    CHECK_EQ(b, a + 1UL, "double: the second command behind the No Op");
    timeOut();
    postAborted(b);
    CHECK_EQ(drain(), 0, "double: the second Aborted");
    postStopped(b);
    diverged = gExt.Tol.Stats.CmdDiverged;
    CHECK_EQ(drain(), 1, "double: the reset is asked for");
    CHECK_EQ(gExt.Why.Staged, XHCI_WHY_ENGINE_DIVERGED,
             "double: staged as the engine's divergence");
    CHECK_EQ(gExt.Why.StagedExtra, 2, "double: reason 2");
    CHECK_EQ(gExt.Tol.Stats.CmdDiverged, diverged + 1UL,
             "double: counted for the release build");
    CHECK_EQ(gExt.CommandRingDiverged, 1, "double: one divergence");
    CHECK_EQ(gExt.CommandResetRequests, 1, "double: one reset request");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_ABORTING,
             "double: left aborting until the reset");
    checkBusy("double: nothing submitted before the reset");
    CHECK_EQ(trbType(b), T_DISABLE_SLOT, "double: the second TRB untouched");
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, PA(a), "double: the first marker");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 1, "double: one rewrite");
}

/*
 * A rewrite the ring refuses: the aborted TRB was reclaimed before the stop
 * named it. Reached here by the earlier rewritten No Op's completion
 * carrying a code no command may carry, whose retirement flushes the ring
 * to its enqueue (xhciRetireCommand), reclaiming the aborted TRB behind it.
 * The reset, the engine left aborting, the TRB untouched. The Link TRB
 * itself is never a rewrite target: a stop naming it is read as index 0
 * first (test_wrap).
 */
static void test_rewrite_refused(void)
{
    ULONG a;
    ULONG b;

    reset();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    timeOut();
    postAborted(a);
    postStopped(a);
    CHECK_EQ(drain(), 0, "refused: the first rewrite");
    b = submit(T_DISABLE_SLOT);
    timeOut();
    postAborted(b);
    CHECK_EQ(drain(), 0, "refused: the second Aborted");
    postEvent(PA(a), CC_SHORT_PACKET, 0);
    CHECK_EQ(drain(), 0, "refused: the No Op's odd completion");
    CHECK_EQ(gExt.CommandsBadCompletion, 1,
             "refused: the code is refused and the ring flushed");
    CHECK_EQ(gExt.CommandNoOpRewrittenPA, 0, "refused: the marker gone");
    CHECK(!XhciRingIsOutstanding(&gExt.CommandRing, b),
          "refused: the aborted TRB reclaimed");
    postStopped(b);
    CHECK_EQ(drain(), 1, "refused: the reset is asked for");
    CHECK_EQ(gExt.Why.StagedExtra, 3, "refused: reason 3");
    CHECK_EQ(gExt.CommandState, XHCI_CMD_STATE_ABORTING,
             "refused: left aborting until the reset");
    CHECK_EQ(trbType(b), T_DISABLE_SLOT, "refused: the TRB untouched");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 1, "refused: one rewrite");
}

/* ------------------------------------------------------------------ */
/* The wrap                                                            */
/* ------------------------------------------------------------------ */

/* Fill the ring until the next command lands on the last usable TRB. */
static void toLastUsable(void)
{
    ULONG i;

    for (i = 0; i < LINK_INDEX - 1UL; i++) {
        (void)runOne(T_ENABLE_SLOT);
    }
    CHECK_EQ(gExt.CommandRing.Enqueue, LINK_INDEX - 1UL,
             "wrap: the next command at the last usable TRB");
}

static void test_wrap(void)
{
    ULONG a;
    ULONG next;
    ULONG link;

    /* Rewritten at the last usable TRB: the No Op runs there, the controller
     * crosses the Link with its toggle, and the next command at index 0 on
     * the new lap runs and completes. */
    reset();
    toLastUsable();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    link = ringTrb(LINK_INDEX)->Control;
    CHECK_EQ(a, LINK_INDEX - 1UL, "wrap: the command before the Link");
    CHECK_EQ(gExt.CommandRing.Enqueue, 0, "wrap: the enqueue crossed");
    timeOut();
    postAborted(a);
    postStopped(a);
    CHECK_EQ(drain(), 0, "wrap: no reset");
    CHECK_EQ(trbType(a), T_NOOP_CMD, "wrap: rewritten at the last TRB");
    CHECK_EQ(ringTrb(LINK_INDEX)->Control & (T_MASK | 3UL),
             link & (T_MASK | 3UL), "wrap: the Link's type, cycle and TC kept");
    next = restartAfterRewrite(a, T_ENABLE_SLOT);
    CHECK_EQ(next, 0, "wrap: the next command at index 0");
    CHECK_EQ(hwCcs, 0, "wrap: the controller toggled at the Link");
    (void)runOne(T_ENABLE_SLOT);
    CHECK_EQ(ran(1, T_ENABLE_SLOT), 2, "wrap: the new lap continues");

    /* Aborted, then the stop naming the Link: index 0, the position
     * adopted, nothing rewritten and the Link not touched. */
    reset();
    toLastUsable();
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    link = ringTrb(LINK_INDEX)->Control;
    timeOut();
    postAborted(a);
    postStopped(LINK_INDEX);
    CHECK_EQ(drain(), 0, "wrap, Link: no reset");
    CHECK_EQ(gExt.CommandRingStoppedOnAbandoned, 0, "wrap, Link: no rewrite");
    CHECK_EQ(gExt.CommandRing.Dequeue, 0, "wrap, Link: read as index 0");
    CHECK_EQ(ringTrb(LINK_INDEX)->Control, link, "wrap, Link: untouched");
    CHECK_EQ(trbType(a), T_ENABLE_SLOT, "wrap, Link: the aborted TRB kept");
    hwWedged = 0;
    next = runOne(T_DISABLE_SLOT);
    CHECK_EQ(next, 0, "wrap, Link: the next command at index 0");
    CHECK_EQ(ran(a, T_ENABLE_SLOT), 0, "wrap, Link: the aborted one never ran");
    CHECK_EQ(ran(0, T_DISABLE_SLOT), 1, "wrap, Link: the next one ran");

    /* A rewrite on the second lap, whose producer cycle is 0: the cycle bit
     * kept is 0, and the controller, now on cycle 0, fetches the No Op. */
    reset();
    toLastUsable();
    (void)runOne(T_ENABLE_SLOT);         /* the last usable TRB */
    (void)runOne(T_ENABLE_SLOT);         /* index 0, lap 2 */
    hwWedged = 1;
    a = submit(T_ENABLE_SLOT);
    CHECK_EQ(a, 1, "lap 2: the command at index 1");
    CHECK_EQ(trbCycle(a), 0, "lap 2: produced with cycle 0");
    timeOut();
    postAborted(a);
    postStopped(a);
    CHECK_EQ(drain(), 0, "lap 2: no reset");
    CHECK_EQ(trbType(a), T_NOOP_CMD, "lap 2: rewritten");
    CHECK_EQ(trbCycle(a), 0, "lap 2: its cycle bit 0 kept");
    next = restartAfterRewrite(a, T_ENABLE_SLOT);
    CHECK_EQ(next, 2, "lap 2: the next command at index 2");
}

int main(void)
{
    test_completion_first();
    test_aborted_stopped_same();
    test_aborted_stopped_next();
    test_aborted_next_with_marker();
    test_aborted_not_ours();
    test_stopped_without_aborted();
    test_stopped_before_late_aborted();
    test_completion_after_aborted();
    test_lifetime_invalidation();
    test_lifetime_start_rebuild();
    test_lifetime_restore();
    test_generation();
    test_once_only();
    test_double_abandonment();
    test_rewrite_refused();
    test_wrap();

    printf("test_cmd: %d checks, %d failures\n", checks, failures);
    return failures;
}
