/*
 * xhci_inj.h - 35-T.9's fault injection, the pure half (qemu flavour only;
 * hcd_inj.c is the driver half). Design record 17 section 5.
 *
 * QEMU raises none of the faults controller tolerance answers, so this layer
 * makes each one real where QEMU allows it and, where it does not, answers
 * the register reads the driver's own handling would meet. It never ships:
 * src/sources compiles it into the qemu flavour alone (XHCI_FLAVOUR_QEMU),
 * the host suite test\test_inj.c into the host build, and the debug and
 * release images carry none of it.
 *
 * THE TRIGGER. One REG_DWORD, XhciQemuInject, in the controller's driver key,
 * re-read by the controller thread about once a second, so a guest-side
 * script fires a fault at run time with regedit /s (or reg add):
 *
 *   bits 31:24  sequence - a command fires when this differs from the last
 *               one seen; the value present at a start is latched unfired
 *   bits 23:16  the fault, XHCI_INJ_*
 *   bits 15:8   a root port number, 0 = the first one that fits
 *   bits 7:0    the fault's argument
 *
 * Nothing is written back: the driver's log ring and the qemu trace carry
 * "qemu.inj.*" notes for every command taken, refused or ended.
 *
 * THE ENDPOINT FAULTS (codes 08 to 12 hex: 35-T.2's soft retry, 35-T.3/4's
 * device cycle) meet the driver at five hooks, all qemu-only: the event drain
 * (xhci_evt.c), which swallows the Stopped event of the layer's own Stop
 * Endpoint and hands an injected Transfer Event to the handler a ring event
 * meets; the Endpoint Context state reads (hcd_enum.c, hcd_cfg.c, hcd_hub.c),
 * answered Halted or Error; every command the thread issues (hcd_enum.c,
 * hcdCommand), where a Reset Endpoint or a Set TR Dequeue to the answered
 * endpoint is completed without being sent; the doorbell (xhci_pci.c),
 * dropped while the endpoint is answered for, as a halted endpoint ignores
 * it; and the thread's own EP0 doorbell (hcd_enum.c, hcdThreadControlQuiet),
 * which the EP0 faults withhold.
 *
 * C89, pure: IRQL any. The caller serializes (hcd_inj.c's InjLock).
 */

#ifndef XHCI_INJ_H
#define XHCI_INJ_H

#include "xhci_compat.h"

#if defined(XHCI_FLAVOUR_QEMU) || defined(XHCI_HOST_TEST)

#include "xhci.h"

#define XHCI_INJ_GET_SEQ(v)   ((((ULONG)(v)) >> 24) & 0xFFUL)
#define XHCI_INJ_GET_FAULT(v) ((((ULONG)(v)) >> 16) & 0xFFUL)
#define XHCI_INJ_GET_PORT(v)  ((((ULONG)(v)) >> 8) & 0xFFUL)
#define XHCI_INJ_GET_ARG(v)   (((ULONG)(v)) & 0xFFUL)
#define XHCI_INJ_VALUE(seq, fault, port, arg)                                 \
    (((((ULONG)(seq)) & 0xFFUL) << 24) | ((((ULONG)(fault)) & 0xFFUL) << 16) | \
     ((((ULONG)(port)) & 0xFFUL) << 8) | (((ULONG)(arg)) & 0xFFUL))

/* The faults (record 17 section 5's table). */
#define XHCI_INJ_NONE            0UL  /* no fault (a refused command)     */
#define XHCI_INJ_LOST_IRQ        1UL  /* arg interrupts lost; 0 one, 255 all */
#define XHCI_INJ_PED             2UL  /* real PED write, PEC answered        */
#define XHCI_INJ_OC              3UL  /* PP clear, OCA and OCC answered      */
#define XHCI_INJ_OC_RELEASE      4UL  /* OCA answered clear from now on      */
#define XHCI_INJ_HCH             5UL  /* real Run/Stop clear                 */
#define XHCI_INJ_DEAD            6UL  /* all-ones USBSTS, BME proof real     */
#define XHCI_INJ_DEAD_NOPROOF    7UL  /* the same, BME reads back set        */
/* 35-T.2 to 35-T.4's, on an interrupt-IN TD the controller is NAKing: `arg`
 * is how many injections (0 one, 255 until CLEAR), the port the device's
 * root port. */
#define XHCI_INJ_TRANSACTION     8UL  /* Transaction Error, emulated Halted  */
#define XHCI_INJ_RESET_EP_FAIL   9UL  /* the same, the Reset Endpoint
                                       * answering Context State Error       */
#define XHCI_INJ_REFUSED_CODE    10UL /* Bandwidth Overrun on the TD         */
#define XHCI_INJ_HALT_HALTED     11UL /* Stall, pointer 0, reads Halted      */
#define XHCI_INJ_HALT_ERROR      12UL /* Stall, off the ring, reads Error    */
#define XHCI_INJ_HALT_STALE      13UL /* Stall, pointer 0, reads Stopped     */
#define XHCI_INJ_EP_NOT_ENABLED  14UL /* code 12, pointer and length 0       */
#define XHCI_INJ_EP0_THREAD      15UL /* a refused code on the thread's own
                                       * EP0 transfer to a published device  */
#define XHCI_INJ_EP0_PREPDO      16UL /* the same before the PDO exists      */
#define XHCI_INJ_RACE_RING       17UL /* TRANSACTION, a ring during the held
                                       * Reset Endpoint                      */
#define XHCI_INJ_RACE_SECOND     18UL /* the same, then a second error       */
#define XHCI_INJ_LAST_BUILT      18UL
#define XHCI_INJ_CLEAR           0xFFUL  /* every injection ended            */

/* XhciInjTake's verdicts. */
#define XHCI_INJ_TAKE_NONE       0UL  /* nothing new                         */
#define XHCI_INJ_TAKE_FIRE       1UL  /* a built fault or CLEAR              */
#define XHCI_INJ_TAKE_UNKNOWN    3UL  /* no such code: refused, noted        */

/* XhciInjKind: how a fault is carried out. */
#define XHCI_INJ_KIND_OTHER      0UL  /* 01 to 07, CLEAR: hcd_inj.c's own    */
#define XHCI_INJ_KIND_RETRY      1UL  /* a stop, emulated Halted, a TE       */
#define XHCI_INJ_KIND_HALT       2UL  /* a stop, a Stall naming no TD        */
#define XHCI_INJ_KIND_REFUSED    3UL  /* a refused code, no stop             */
#define XHCI_INJ_KIND_EP0        4UL  /* at the thread's EP0 doorbell        */

/* What the answered endpoint's next Reset Endpoint meets (XHCI_INJ_EP.Hold). */
#define XHCI_INJ_HOLD_NONE       0UL  /* Success, the answers ended          */
#define XHCI_INJ_HOLD_FAIL       1UL  /* Context State Error, still Halted   */
#define XHCI_INJ_HOLD_RING       2UL  /* held: a ring resumes the endpoint   */
#define XHCI_INJ_HOLD_SECOND     3UL  /* held: a ring, then a second error   */

/* XhciInjCommand's verdicts on one command the thread issues. */
#define XHCI_INJ_CMD_PASS        0UL  /* sent as it is                       */
#define XHCI_INJ_CMD_ANSWER      1UL  /* *code answered, nothing sent        */
#define XHCI_INJ_CMD_RING        2UL  /* held (HOLD_RING): the layer's turn  */
#define XHCI_INJ_CMD_SECOND      3UL  /* held (HOLD_SECOND): the layer's     */
#define XHCI_INJ_CMD_SEND_END    4UL  /* sent; the answers end if it succeeds
                                       * (XhciInjCommandSent)               */

/* XhciInjStopVerdict's: where the layer's Stop Endpoint left the TD. */
#define XHCI_INJ_STOP_OK         0UL  /* the dequeue is the TD's first TRB   */
#define XHCI_INJ_STOP_REPOINT    1UL  /* fetched ahead, nothing moved: a Set
                                       * TR Dequeue puts it back             */
#define XHCI_INJ_STOP_BROKEN     2UL  /* anything else: abandoned, counted   */

/* The refused code REFUSED_CODE and the EP0 faults carry: record 17's
 * finding T3 names Bandwidth Overrun among the codes nothing claims. */
#define XHCI_INJ_REFUSED_CC      XHCI_CC_BANDWIDTH_OVERRUN

/* The argument that means "until CLEAR" for LOST_IRQ; for DEAD it is 0. */
#define XHCI_INJ_ARG_FOREVER     0xFFUL
#define XHCI_INJ_DEAD_FOREVER    0xFFFFFFFFUL

/* PCI Command, Bus Master Enable (xhci_hw.h's XHCI_PCI_COMMAND_BME, which
 * this pure half cannot include). */
#define XHCI_INJ_PCI_BME         0x0004UL

/*
 * The endpoint the layer answers for (the endpoint faults; one at a time),
 * under InjLock. Slot 0 is none. State is the EP State the context reads
 * answer (Halted or Error; 0 the real one); Block drops the doorbells rung
 * for it (Dropped counts them), from the layer's Stop Endpoint until the
 * answers end, as a halted endpoint ignores a doorbell. Watch swallows the
 * Stopped event of the layer's own Stop, kept in Stop*. Hold is what its
 * next Reset Endpoint meets. Ev is an injected Transfer Event waiting for
 * the drain (EvPending). Ep0* is an armed EP0 fault, fired at the thread's
 * EP0 doorbell. Abandoned and Repointed count the stops that did not find
 * the TD at the dequeue, and those QEMU's fetch-ahead needed put back.
 */
typedef struct _XHCI_INJ_EP {
    ULONG Slot;
    ULONG Dci;
    ULONG State;
    ULONG Block;
    ULONG Dropped;
    ULONG Hold;
    ULONG Watch;
    ULONG StopSeen;
    ULONG StopPA;
    ULONG StopCode;
    ULONG StopResidual;
    ULONG EvPending;
    XHCI_TRB Ev;
    ULONG Ep0Want;
    ULONG Ep0Port;
    ULONG Ep0Left;
    ULONG Abandoned;
    ULONG Repointed;
} XHCI_INJ_EP, *PXHCI_INJ_EP;

/* The trigger's memory: the last sequence seen, and whether one was. */
typedef struct _XHCI_INJ_TRIGGER {
    ULONG Seen;
    ULONG Seq;
} XHCI_INJ_TRIGGER, *PXHCI_INJ_TRIGGER;

/*
 * The register answers the layer gives in place of the hardware's. A port's
 * PEC is answered set from the layer's PED write until the driver's
 * acknowledgement; an over-current port reads PP clear (OCA and OCC set
 * while held) until the driver's PP write after the release, PP itself never
 * reaching the real port while emulated. USBSTS answers all-ones to the
 * health poll and the containment step for DeadLeft passes
 * (XHCI_INJ_DEAD_FOREVER: until CLEAR), and Bus Master Enable reads back
 * set once NoProof is armed.
 */
typedef struct _XHCI_INJ_REGS {
    ULONG PedPort;
    ULONG PecHeld;
    ULONG OcPort;
    ULONG OcActive;
    ULONG OcaHeld;
    ULONG OccHeld;
    ULONG OcPpWrites;
    ULONG DeadLeft;
    ULONG DeadActive;               /* this pass answers all-ones        */
    ULONG NoProof;
} XHCI_INJ_REGS, *PXHCI_INJ_REGS;

/* Latch the value present at a start, so it never fires: a start must not
 * replay the last command a script left in the key. `found` 0 is "no value
 * or not a DWORD". */
VOID XhciInjTriggerStart(PXHCI_INJ_TRIGGER t, ULONG found, ULONG value);

/* One read of the value: XHCI_INJ_TAKE_*. A fresh sequence is consumed
 * whatever its verdict, so a refused command is not retried. */
ULONG XhciInjTake(PXHCI_INJ_TRIGGER t, ULONG found, ULONG value);

/* Every answer off. */
VOID XhciInjRegsClear(PXHCI_INJ_REGS r);

/* A PED or over-current target: a managed USB 2.0 root port, powered, with
 * a device connected and enabled. `wanted` nonzero asks for that port only.
 * 1 fits, 0 not. */
ULONG XhciInjPortFits(const XHCI_PORT_MAP *map, ULONG port, ULONG wanted,
                      ULONG portsc);

/* The arming of each emulated fault on its port. */
VOID XhciInjArmPec(PXHCI_INJ_REGS r, ULONG port);
VOID XhciInjArmOc(PXHCI_INJ_REGS r, ULONG port);
VOID XhciInjReleaseOc(PXHCI_INJ_REGS r);
/* `reads` 0 means until CLEAR. */
VOID XhciInjArmDead(PXHCI_INJ_REGS r, ULONG reads, ULONG noProof);

/* The PORTSC a read answers. An unreadable PORTSC passes unchanged. */
ULONG XhciInjPortscRead(const XHCI_INJ_REGS *r, ULONG port, ULONG raw);

/* A PORTSC write: the acknowledgements and PP writes it carries are taken,
 * and the value the hardware is given returned. */
ULONG XhciInjPortscWrite(PXHCI_INJ_REGS r, ULONG port, ULONG value);

/*
 * The all-ones fault is a snapshot taken once per thread pass, before any
 * reader: XhciInjDeadPass sets DeadActive for the pass and spends one of
 * DeadLeft, whether or not any read follows - a failed controller's health
 * poll reads nothing, and a finite fault still runs out. Every USBSTS read
 * in the pass (the health poll's at every XhciTolerance value, the
 * containment step's at 1) answers from it, spending nothing. So an
 * argument of N is N passes of all-ones to whichever readers run.
 */
ULONG XhciInjDeadPass(PXHCI_INJ_REGS r);
ULONG XhciInjUsbsts(const XHCI_INJ_REGS *r, ULONG raw);

/*
 * The lost-interrupt window, decided without a compare-exchange (no import
 * has one on Windows 98's evidence). Disarmed it drops nothing whatever the
 * counts; armed, `taken` is the ISR's count of interrupts claimed since the
 * arming (post-increment, from 1), and the first `budget` are dropped, or
 * every one when `forever`. The thread disarms a spent window
 * (XhciInjIrqSpent), so the count never runs on unarmed.
 */
ULONG XhciInjIrqDrop(ULONG armed, ULONG forever, ULONG taken, ULONG budget);
ULONG XhciInjIrqSpent(ULONG armed, ULONG forever, ULONG taken, ULONG budget);

/* The PCI Command register's read-back in the containment's proof. */
ULONG XhciInjPciCommand(const XHCI_INJ_REGS *r, ULONG command);

/* Whether a fault needs a controller that has not failed: every built
 * fault but CLEAR and OC_RELEASE, which end one. A failed controller's
 * reads are never taken, so a fault armed on it would stand until CLEAR. */
ULONG XhciInjNeedsLive(ULONG fault);

/* LOST_IRQ's argument as a count: 0 is one, 255 is "until CLEAR"
 * (returned as 0xFFFFFFFF). The endpoint faults' count reads the same. */
ULONG XhciInjLostCount(ULONG arg);

/* ----------------------------------------------------------------------- */
/* The endpoint faults (08 to 12 hex)                                       */
/* ----------------------------------------------------------------------- */

/* XHCI_INJ_KIND_* of a fault code. */
ULONG XhciInjKind(ULONG fault);

/* The Hold a fault's injection sets: FAIL, RING, SECOND or NONE. */
ULONG XhciInjHoldOf(ULONG fault);

/* The EP State a fault's context reads answer: Halted for TRANSACTION, the
 * RESET_EP_FAIL and RACE ones and HALT_HALTED, Error for HALT_ERROR, 0 (the
 * real state) for the rest. */
ULONG XhciInjHaltStateOf(ULONG fault);

/* One of a count spent: 0 when none was left. 0xFFFFFFFF never runs out. */
ULONG XhciInjSpend(PULONG left);

/*
 * A pipe the endpoint faults may be aimed at (record 17 section 5's soft
 * retry target): an interrupt-IN pipe with no streams, of a published
 * device that is not a hub and is not leaving, at `wanted`'s root port when
 * that is nonzero; idle of every operation (paused, closed, halted, a drain
 * or a cancel owed: `busy`), with a TD queued and no soft retry owed.
 * 1 fits, 0 not.
 */
ULONG XhciInjPipeFits(ULONG wanted, ULONG devPort, ULONG published,
                      ULONG isHub, ULONG gone, ULONG transferType,
                      ULONG endpointAddress, ULONG streams, ULONG busy,
                      ULONG queued, ULONG retryWanted);

/* A Transfer Event as a controller writes one: `pa` the TRB pointer (the
 * high dword 0), `residual` the untransferred length, `cc` the completion
 * code; ED 0 and the cycle bit 0, which no handler reads. */
VOID XhciInjTransferEvent(XHCI_TRB *ev, ULONG pa, ULONG residual, ULONG cc,
                          ULONG slot, ULONG dci);

/* Every field of the endpoint off: a start. */
VOID XhciInjEpClear(PXHCI_INJ_EP e);

/* The answers end - endpoint, state, block, hold and watch - leaving the
 * counts, a pending event and the EP0 arming. */
VOID XhciInjEpEnd(PXHCI_INJ_EP e);

/* The layer's Stop Endpoint begins: the endpoint taken, its doorbells
 * dropped and its Stopped event watched for, nothing seen yet. */
VOID XhciInjEpWatch(PXHCI_INJ_EP e, ULONG slot, ULONG dci);

/* An event off the ring, in the drain: 1 when it is the watched Stopped
 * event (a Transfer Event, code 26 to 28, for the watched endpoint), kept
 * and swallowed; 0 when the driver is to see it. */
ULONG XhciInjEpSwallow(PXHCI_INJ_EP e, const XHCI_TRB *ev);

/* A doorbell write (`value` the register's, target in 7:0): 1 when it is
 * dropped. */
ULONG XhciInjEpDoorbell(PXHCI_INJ_EP e, ULONG slot, ULONG value);

/* The EP State a context read answers. */
ULONG XhciInjEpState(const XHCI_INJ_EP *e, ULONG slot, ULONG dci, ULONG raw);

/*
 * One command the thread issues (not the layer's own), against the answered
 * endpoint: XHCI_INJ_CMD_*. A Disable Slot, Address Device or Reset Device
 * for its slot is sent and ends the answers once it succeeds (SEND_END);
 * so is a Configure Endpoint that deconfigures (DC) or whose Input Control
 * Context Drop (`icDrop`) or Add (`icAdd`) flags name the endpoint - one
 * for other endpoints of the slot is sent and changes nothing. For the
 * endpoint itself: a Reset Endpoint on Halted is answered Success, ending
 * them (HOLD_NONE), answered Context State Error, leaving Halted
 * (HOLD_FAIL), or held (HOLD_RING, HOLD_SECOND) - only when
 * `retrySurvives` says it is the soft retry's own reset, its pipe not
 * paused and the queue's head still the deferred TD its RetryWanted names;
 * any other reset spends the hold and is answered Success. On Error a
 * Reset Endpoint is answered Context State Error (xHCI 4.6.8: Halted
 * only). A Set TR Dequeue on Halted is answered Context State Error; on
 * Error it is sent - the real endpoint is Stopped, which takes it - and
 * ends the answers once it succeeds (4.6.10: Stopped or Error). Everything
 * else is sent. `*code` is written only for ANSWER.
 */
ULONG XhciInjCommand(PXHCI_INJ_EP e, const XHCI_TRB *cmd, ULONG icDrop,
                     ULONG icAdd, ULONG retrySurvives, PULONG code);

/* A SEND_END command's completion `code` for `slot`: the answers end on
 * Success, and stand on any failure. */
VOID XhciInjCommandSent(PXHCI_INJ_EP e, ULONG slot, ULONG code);

/*
 * Where the layer's Stop Endpoint left the TD (`firstPA` its first TRB,
 * `afterPA` the TRB after its last, `firstLen` the first TRB's length,
 * `single` 1 for a one-TRB TD), from the context's TR Dequeue Pointer and
 * the Stopped event it swallowed, if one came. OK: the dequeue is the first
 * TRB and no event says anything moved. REPOINT: a one-TRB TD the controller
 * had fetched and stopped with nothing moved (Stopped, code 26, at its TRB,
 * the residual its whole length) and a dequeue past it - QEMU's NAK
 * handling, which fetches a TD before it transfers - which a Set TR Dequeue
 * to its first TRB puts back where real hardware leaves it. BROKEN:
 * anything else.
 */
ULONG XhciInjStopVerdict(ULONG ctxDeqPA, ULONG firstPA, ULONG afterPA,
                         ULONG single, ULONG firstLen, ULONG seen,
                         ULONG seenPA, ULONG seenCode, ULONG seenResidual);

/* Arm an EP0 fault: `left` injections (XhciInjLostCount's), at `port`. */
VOID XhciInjEp0Arm(PXHCI_INJ_EP e, ULONG fault, ULONG port, ULONG left);

/* The thread's EP0 doorbell for a device at root port `devPort`,
 * `published` 1 once its PDO exists: 1 when an armed EP0 fault fires on it
 * (THREAD on a published device, PREPDO on one not yet published, at the
 * armed port or any), one of its count spent and the fault disarmed when
 * none is left. Never while an injected event still waits. */
ULONG XhciInjEp0Fires(PXHCI_INJ_EP e, ULONG devPort, ULONG published);

#endif /* XHCI_FLAVOUR_QEMU || XHCI_HOST_TEST */

#endif /* XHCI_INJ_H */
