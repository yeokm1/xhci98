/*
 * xhci_inj.h - 35-T.9's fault injection, the pure half (qemu flavour only;
 * hcd_inj.c is the driver half). Design record 17 section 5.
 *
 * QEMU raises none of the faults controller tolerance answers, so this layer
 * makes each one real where QEMU allows it and, where it does not, answers
 * the register reads the driver's own handling would meet. It never ships:
 * src/sources compiles it into the qemu flavour alone (XHCI_FLAVOUR_QEMU),
 * the host suite test\test_inj.c into the host build, and the release image
 * carries none of it.
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
 * device cycle; and 14 to 17 hex, 36.22's bulk ones) meet the driver at
 * five hooks, all qemu-only: the event drain (xhci_evt.c), which swallows the
 * Stopped event of the layer's own Stop
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
#define XHCI_INJ_PED             2UL  /* PED write, PEC set, PED clear       */
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
#define XHCI_INJ_CLEAR_HALT      19UL /* 36.12: the next CLEAR_FEATURE(ENDPOINT_
                                       * HALT) answered, not sent: arg 0
                                       * STALL, 1 failed, 2 never sent      */
/* Task 36.22's, on a bulk TD (a stream's too) of a published device at
 * root port bits 15:8 (0 any): `arg` is the endpoint address (0: the first
 * bulk IN), one injection per command. 21 to 23 first withhold the next
 * doorbell of that endpoint, so a TD sits on its ring unstarted - QEMU
 * otherwise finishes a bulk TD inside the doorbell write - then. They
 * follow 36.12's 19 (merged first), so their hex codes are 14 to 17: */
#define XHCI_INJ_BULK_TIMEOUT    20UL /* no doorbell withheld: the next TD's
                                       * completion events are lost until the
                                       * driver's own stop, reset or dequeue
                                       * move, or a slot command; streams
                                       * too                                 */
#define XHCI_INJ_BULK_STOP       21UL /* a real Stop Endpoint whose events
                                       * reach the driver, then the doorbell
                                       * rung; streams too                   */
#define XHCI_INJ_BULK_ERROR      22UL /* TRANSACTION's stop and emulated
                                       * Halted with a Transaction Error for
                                       * the TD; streams too                 */
#define XHCI_INJ_BULK_SHORT      23UL /* a stop, the dequeue moved past the
                                       * TD, a Short Packet on its last TRB
                                       * with half that TRB's length left,
                                       * then the doorbells rung; streams
                                       * too                                 */
/* Task 38.10's (design record 20 section 7), after 36.22's, hex 18 and
 * 19: the steps of an interface switch a QEMU device never refuses,
 * answered by the driver, nothing sent. */
#define XHCI_INJ_SET_INTERFACE   24UL /* the next SELECT_INTERFACE's
                                       * SET_INTERFACE: arg 0 a STALL, 1
                                       * not established                     */
#define XHCI_INJ_SELECT_CONFIG   25UL /* the next interface switch's
                                       * Configure Endpoint: arg 0 refused
                                       * at step 3, 1 refused at the
                                       * rollback (Resource Error), 2 and 3
                                       * no completion there - the
                                       * controller reset requested          */
/* Issue 26 (2026-11-07), hex 1A: the next `arg` Stop Endpoints (0 one) on
 * an endpoint with streams are sent for real and answered Context State
 * Error, the endpoint's next context read answered Running - the race an
 * Intel 7 Series controller showed, which in 3.4.1.0 left a cancel's
 * endpoint paused for good. */
#define XHCI_INJ_STOP_RACE       26UL
/* Task 49.35 (design record 17 section 4.6.1), hex 1B: every HCRST this
 * driver writes from now on takes `arg` x 10 ms (255 for ever) until CLEAR:
 * while the hold runs, the reads the reset waits make answer HCRST set in
 * USBCMD (`port` 0) or CNR set in USBSTS (`port` 1). QEMU completes a reset
 * at once, so this is how a guest meets a controller whose reset outlasts
 * the recovery's 10 ms stall - an ASMedia ASM1042 read at more than 11 ms.
 * Hex 1C: one Host Controller Event, completion code `arg`, handed to the
 * drain, which fails the controller as a real one does. */
#define XHCI_INJ_RESET_SLOW      27UL
#define XHCI_INJ_HC_EVENT        28UL
/* Task 49.39 (design record 17 section 4.12.8), hex 1D and 1E. 1D: the
 * next `arg` (0 one) stream Set TR Dequeues a cancel sends after taking its
 * head TD off go out for real with that TD's first TRB and its cycle in
 * place of the driver's position - a deliberately stale hardware dequeue,
 * which the read-back meets. 1E: the next `arg` (0 one) times a stream's
 * Set TR Dequeue is about to be sent, a doorbell is first rung for another
 * stream of the endpoint holding a TD, so the endpoint may run before the
 * command lands. */
#define XHCI_INJ_STALE_DEQUEUE   29UL
#define XHCI_INJ_RESTART_RACE    30UL
/* Task 49.39 (record 17 4.12.8), hex 1F: the next `arg` (0 one) times c1
 * is about to resend a stream's Set TR Dequeue after a difference, a
 * doorbell is first rung for another stream of the endpoint - one holding a
 * TD, else any other - so the endpoint runs between the difference and the
 * resend. */
#define XHCI_INJ_REPAIR_RACE     31UL
/* Task 50.7 (record 17 section 5; record 25 section 24 item 4), hex 20:
 * the next `arg` (0 one, 255 until CLEAR) 8-byte device-descriptor reads
 * of an enumeration at the root port in bits 15:8 (0 any) answered with
 * bMaxPacketSize0 0, the transfer itself real: the DESC8 check fails
 * (XHCI_ENUM_FAIL_DESCRIPTOR) as for a device with a bad descriptor, and
 * with arg 2 its one retry too, so the port rests in Failed, with no cycle,
 * recovery or hold - the "failed enumeration" the USB Tree's fallback note
 * is for. */
#define XHCI_INJ_ENUM_DESC       32UL
/* Task 50.20 (record 17 section 4.12.10), hex 21: arg bit 0, the
 * controller flagged as the Intel 7 Series xHCI is (stream opens of a
 * declaring client acknowledged to the contract); arg bit 1, the QEMU stream
 * resync (4.12.4) off on every endpoint. Unlike every other fault, read at
 * the controller's start (HcdInjStreamStart) and only then, since the flag
 * must stand before the streams open; fired at run time it changes nothing
 * (noted qemu.inj.streamflag.ignored). */
#define XHCI_INJ_STREAM_UNTRUSTED 33UL
#define XHCI_INJ_SU_FLAG         0x01UL
#define XHCI_INJ_SU_RESYNC_OFF   0x02UL
/* Task 50.20, hex 22: RESET_PIPE's recycle Configure Endpoint (the endpoint
 * recycle of record 17 section 4.12.10; fault 19 reaches only a switch's)
 * answered and never issued, once: arg bits 3:0 the attempt (0 the next, N
 * the Nth from now), bit 4 clear refused with Resource Error, set with no
 * completion and the controller reset requested; port byte (15:8) the DCI
 * (0 any). Noted qemu.inj.recycle (slot, DCI, attempt, code). */
#define XHCI_INJ_RECYCLE_CFG     34UL
#define XHCI_INJ_RC_NOCOMPLETE   0x10UL
/* Task 50.20, hex 23: a rendezvous - the bus holds at point `arg` (1 a
 * cancel's take-off, before its completions are delivered; 2 RESET_PIPE
 * between the device's clear and the recycle; 3, task 50.30 step 3, a
 * requested recovery's entry, before it takes the power gate, released at
 * once as well when any route's bit is raised - standing, or raised since
 * the point was reached, revision 34 of record 17 section 4.15.16 C - and
 * noted qemu.inj.rv.route), notes qemu.inj.rv.reached
 * with a count, and waits, at most 60 s, polling the key every 100 ms,
 * until 23 comes again with arg 0 (the release); arg 0 with nothing held
 * disarms. */
#define XHCI_INJ_RENDEZVOUS      35UL
#define XHCI_INJ_RV_CANCEL       1UL
#define XHCI_INJ_RV_RECYCLE      2UL
#define XHCI_INJ_RV_RECOVERY     3UL
/*
 * Task 50.30, step 2 (design record 17 section 4.15.9): a device, or the
 * controller's command engine, that stops answering. Each withholds the
 * doorbell of the next command of its kind (or of the thread's next EP0
 * transfer) for a device at the place - 15:8 the root port, 0 the first
 * that fits, bit 15 a device behind a hub at it - so the engine's watchdog
 * or the thread's deadline meets a real silence; at the watchdog's abort
 * the layer acts out the outcome the argument chooses (bits 6:4); bits 3:0
 * the count, 15 persistent. 0x26 with bit 7 is armed by the device's own
 * behaviour: nothing until a thread control transfer at the place reaches
 * its deadline unwithheld; from then every Address Device and thread EP0
 * transfer at the place is withheld until the place's root port reads
 * disconnected, or CLEAR. 0x26 takes a root port only: its lifetime is
 * that port's disconnect, and a hub's downstream port has no such hook
 * here, so a place behind a hub (bit 15) is refused and nothing is armed.
 * For 0x24 and 0x25, bit 7 (step 3, design record 17 section 4.15.16 C)
 * withholds the recovery command that follows as well: each primary
 * withholding latches its slot, and the first command placed afterwards
 * that is that slot's Disable Slot - or, for 0x25, its Stop Endpoint of
 * DCI 1 - is withheld once, through the same watchdog and outcome, the
 * settlement's included. 0x25 with bit 7 and outcome 7 also arms a mark
 * deadline about a second after the primary: the thread's own EP0 wait
 * marks the cycle once it has passed (HcdInjWaitLook), and the recovery
 * command - then the cycle's Disable Slot alone, a teardown Stop of EP0
 * going out - is acted out as outcome 1.
 */
#define XHCI_INJ_ADDRESS_HANG    36UL /* 0x24: Address Device withheld     */
#define XHCI_INJ_EP0_HANG        37UL /* 0x25: the thread's EP0 doorbell   */
#define XHCI_INJ_DEVICE_HANG     38UL /* 0x26: both, until its disconnect  */
#define XHCI_INJ_CFGEP_HANG      39UL /* 0x27: Configure Endpoint          */
#define XHCI_INJ_RESETEP_HANG    40UL /* 0x28: Reset Endpoint              */
#define XHCI_INJ_SETDEQ_HANG     41UL /* 0x29: Set TR Dequeue Pointer      */
#define XHCI_INJ_EVAL_HANG       42UL /* 0x2A: Evaluate Context            */
#define XHCI_INJ_RESETDEV_HANG   43UL /* 0x2B: Reset Device                */
/* Task 50.34 (design record 16 section 21.10), hex 2C: the Intel port
 * switchover's step, with the qemu flavour's stand-in for a listed
 * controller (XhciQemuPswModel): arg bit 0 the SOFTWARE hive read as not
 * loaded yet, bit 1 the guard's routine not there, bit 2 the session key's
 * create refused, bit 3 ReadyAtStart's reading refused, bit 4 its write
 * refused, bit 5 the guard's XhciIntelPortSwitchReady write refused. Read
 * at a start, and only by a start that reaches the step (HcdInjPswStep), as
 * hex 21 is read at the start alone; fired at run time it changes nothing
 * (noted qemu.inj.psw.ignored). */
#define XHCI_INJ_PSW_STEP        44UL /* 0x2C                              */
#define XHCI_INJ_PSW_HIVE        0x01UL
#define XHCI_INJ_PSW_ROUTINE     0x02UL
#define XHCI_INJ_PSW_OPEN        0x04UL
#define XHCI_INJ_PSW_READ_S      0x08UL
#define XHCI_INJ_PSW_WRITE_S     0x10UL
#define XHCI_INJ_PSW_WRITE_G     0x20UL
#define XHCI_INJ_HG_FOREVER      0x0FUL
#define XHCI_INJ_HG_COUNT(arg)   ((arg) & 0x0FUL)
#define XHCI_INJ_HG_OUTCOME(arg) (((arg) >> 4) & 0x07UL)
#define XHCI_INJ_HG_BIT7         0x80UL
#define XHCI_INJ_HG_BEHIND       0x80UL /* in the port byte              */

/* The outcomes acted out at the abort (record 17 section 4.15.9). */
#define XHCI_INJ_OUT_STOP_HERE   0UL /* Stopped naming the withheld TRB    */
#define XHCI_INJ_OUT_FETCHED     1UL /* No Op, Aborted, Stopped at next    */
#define XHCI_INJ_OUT_FETCHED_NA  2UL /* No Op, Stopped at next             */
#define XHCI_INJ_OUT_ABORT_HERE  3UL /* Aborted and Stopped, same TRB      */
#define XHCI_INJ_OUT_RACE        4UL /* doorbell rung: QEMU completes it   */
#define XHCI_INJ_OUT_NOTHING     5UL /* no stop event                      */
#define XHCI_INJ_OUT_DIVERGE     6UL /* Stopped at an impossible position  */
#define XHCI_INJ_OUT_MARK        7UL /* 0x25 bit 7: a cycle mark in the
                                      * wait; the recovery acted as 1     */
/* The mark deadline of outcome 7, in tolerance clock ticks (100 ms). */
#define XHCI_INJ_HG_MARK_TICKS   10UL

typedef struct _XHCI_INJ_HANG {
    ULONG Fault;        /* 0 none                                         */
    ULONG Port;         /* the root port, 0 the first that fits           */
    ULONG Behind;       /* 1: a device behind a hub at Port               */
    ULONG Outcome;      /* XHCI_INJ_OUT_*                                 */
    ULONG Left;         /* withholdings left, XHCI_INJ_HG_FOREVER for ever */
    ULONG ByBehaviour;  /* 0x26 armed by the device's own silence         */
    ULONG Armed;        /* withholding now                                */
    ULONG HoldNext;     /* the command being submitted fits: its doorbell
                         * is withheld if it is the one rung             */
    ULONG HoldType;     /* ... its TRB type                               */
    ULONG HoldSlot;     /* ... its slot                                   */
    ULONG HeldPA;       /* the withheld command's TRB, 0 none             */
    ULONG NoOpPA;       /* the layer's own No Op (outcomes 1, 2), 0 none  */
    ULONG StopOwedPA;   /* outcome 4: the Stopped owed after its completion */
    ULONG Withheld;     /* withholdings made                              */
    /* Step 3 (bit 7 of 0x24 and 0x25): the recovery command's latch. */
    ULONG Recovery;     /* bit 7 armed                                    */
    ULONG RecSlot;      /* the slot latched at the primary, 0 none        */
    ULONG HoldRec;      /* the marked submit is that recovery command     */
    ULONG MarkSlot;     /* outcome 7: the slot whose wait is marked, 0 none */
    ULONG MarkTick;     /* ... once the tolerance clock reaches this      */
} XHCI_INJ_HANG, *PXHCI_INJ_HANG;

/* Armed from a trigger (fault 0x24 to 0x2B; port byte; argument); 0x26
 * with the port byte's bit 7 (behind a hub) is refused: Fault stays 0. */
VOID XhciInjHangArm(PXHCI_INJ_HANG h, ULONG fault, ULONG port, ULONG arg);

/* Everything ended (CLEAR, or the place's disconnect for 0x26). */
VOID XhciInjHangEnd(PXHCI_INJ_HANG h);

/* The command ring given up (an invalidation, a rebuild at start or
 * recovery, the restore, an HCRST): every address the layer holds on it -
 * the submit's mark, the withheld TRB, its own No Op, the Stopped owed -
 * goes with it, so none matches a later command placed at the same
 * address in a new ring generation. The fault, its place, its count and a
 * 0x26 armed by behaviour stay. */
VOID XhciInjHangRingGone(PXHCI_INJ_HANG h);

/* The command TRB type the fault withholds; 0 for 0x25 (EP0 only). */
ULONG XhciInjHangCmdType(ULONG fault);

/* Step 3: a primary withholding made for `slot` at tolerance tick `now`.
 * With bit 7 the recovery command's latch takes the slot (a later primary
 * replaces it); for 0x25 with outcome 7 the mark deadline is armed too. */
VOID XhciInjHangLatch(PXHCI_INJ_HANG h, ULONG slot, ULONG now);

/* Whether a command of TRB type `type` for `slot` (`epId` its Endpoint ID,
 * for a Stop Endpoint) is the latched recovery command: that slot's
 * Disable Slot, or for 0x25 its Stop Endpoint of DCI 1 - but not under
 * outcome 7, whose recovery command is the cycle's Disable Slot alone.
 * Nothing is spent: the hook before the submit asks this. */
ULONG XhciInjHangRecoveryFits(const XHCI_INJ_HANG *h, ULONG type, ULONG slot,
                              ULONG epId);

/* The same at the doorbell of the command placed: 1 when it is withheld,
 * the latch consumed (one shot). */
ULONG XhciInjHangRecoveryTake(PXHCI_INJ_HANG h, ULONG type, ULONG slot,
                              ULONG epId);

/* Outcome 7's deadline, looked at by the thread's wait for `slot` at tick
 * `now`: 1 once, when it is armed for that slot and has passed (disarmed
 * in the same call); 0 for another slot or before the tick. */
ULONG XhciInjHangMarkDue(PXHCI_INJ_HANG h, ULONG slot, ULONG now);

/* The outcome acted out at a withheld command's abort: outcome 7 acts as
 * outcome 1 (the clean abort), every other as itself. */
ULONG XhciInjHangActOutcome(ULONG outcome);

/* Record 17 section 4.15.16 C (revision 34): 1 when rendezvous `point`
 * lets the thread go for a route - point 3 alone, with a bit standing in
 * CmdWaitLeave (`standing`) or raised since the point was reached
 * (`latched`); points 1 and 2 never. */
ULONG XhciInjRvRouteLets(ULONG point, ULONG standing, ULONG latched);

/* Whether a device at root port `port` (route `route`) is the place. */
ULONG XhciInjHangFits(PXHCI_INJ_HANG h, ULONG port, ULONG route);

/* A command of TRB type `type` for a device at (port, route): 1 when it
 * would be withheld. Nothing is spent: the hook before the submit asks
 * this, and the count is spent only at the doorbell of the command
 * actually placed (XhciInjHangCmd), so a refused or busy submission spends
 * nothing and leaves nothing for a later doorbell. */
ULONG XhciInjHangCmdFits(PXHCI_INJ_HANG h, ULONG type, ULONG port,
                         ULONG route);

/* The same, at that doorbell: 1 when it is withheld, the count spent. */
ULONG XhciInjHangCmd(PXHCI_INJ_HANG h, ULONG type, ULONG port, ULONG route);

/* The thread's EP0 doorbell for a device at (port, route): 1 withheld. */
ULONG XhciInjHangEp0(PXHCI_INJ_HANG h, ULONG port, ULONG route);

/* The thread's deadline passed for a device at (port, route), its doorbell
 * not withheld by the layer: 1 when that arms 0x26 by behaviour. */
ULONG XhciInjHangLate(PXHCI_INJ_HANG h, ULONG port, ULONG route,
                      ULONG withheld);

/* Root port `port` read disconnected: 1 when that ends a 0x26. */
ULONG XhciInjHangGone(PXHCI_INJ_HANG h, ULONG port);

/* The outcome's events at the abort, into ev[] (at most 2), in order:
 * Command Completion Events naming heldPA, nextPA or badPA. *rewrite 1
 * when the withheld TRB is to become the layer's No Op first; *ring 1 when
 * the doorbell is to be rung instead (outcome 4). Returns the count. */
ULONG XhciInjHangEvents(ULONG outcome, ULONG heldPA, ULONG nextPA,
                        ULONG badPA, XHCI_TRB *ev, PULONG rewrite,
                        PULONG ring);

/* One Command Completion Event (completion code cc, pointer pa). */
VOID XhciInjCommandEvent(XHCI_TRB *ev, ULONG pa, ULONG cc);

/*
 * Task 50.30 step 3 (design record 17 section 4.15.16 E), hex 2D: the
 * query-stop under a debt, a driver-path exercise. Armed by the trigger,
 * taken by the next command a hang fault (24 to 2B) withholds; then the
 * controller thread itself calls the driver's own query (HcdMsg4QueryStop)
 * and, after the hold, its cancel (HcdMsg4CancelStop) - no IRP, no work
 * item. Argument: bits 3:0 the hold in seconds between the query and its
 * cancel (0 the next turn), bits 6:4 the delay in 100 ms units between the
 * trigger and the query, bit 7 the test window: the withholding raises the
 * qemu-only route bit HCD_LEAVE_QTEST, which holds the debt until the end.
 * Kept apart from XHCI_INJ_LAST_BUILT, which is 2C, task 50.34's.
 */
#define XHCI_INJ_QUERY_STOP      45UL /* 0x2D */
#define XHCI_INJ_QS_HOLD(arg)    ((arg) & 0x0FUL)
#define XHCI_INJ_QS_DELAY(arg)   (((arg) >> 4) & 0x07UL)
#define XHCI_INJ_QS_WINDOW       0x80UL
#define XHCI_INJ_QS_TICKS_PER_S  10UL   /* tolerance ticks of 100 ms */

/* The exercise's state (XHCI_INJ_QSTOP.State). */
#define XHCI_INJ_QS_IDLE         0UL
#define XHCI_INJ_QS_ARMED        1UL  /* waiting for a withholding       */
#define XHCI_INJ_QS_TRIGGERED    2UL  /* waiting for its delay           */
#define XHCI_INJ_QS_CALLING      3UL  /* the thread is in a driver call  */
#define XHCI_INJ_QS_QUERIED      4UL  /* the query made; the hold runs   */

/* XhciInjQsArmRefusal's reasons (qemu.inj.qstop.refused). */
#define XHCI_INJ_QS_R_OK         0UL
#define XHCI_INJ_QS_R_STOPPED    1UL  /* the controller not started      */
#define XHCI_INJ_QS_R_MODE       2UL  /* not interrupt mode 4 on NT 5    */
#define XHCI_INJ_QS_R_ISOLATION  3UL  /* isolation does not apply        */
#define XHCI_INJ_QS_R_ACTIVE     4UL  /* an exercise already active      */
#define XHCI_INJ_QS_R_BUSY       5UL  /* a real request in its bracket   */
#define XHCI_INJ_QS_R_CLOSED     6UL  /* a stop or a removal closed it   */

/* XhciInjQsStep's actions, one a turn of the thread. */
#define XHCI_INJ_QS_NONE         0UL
#define XHCI_INJ_QS_QUERY        1UL  /* CALLING: the query is made      */
#define XHCI_INJ_QS_NODEBT       2UL  /* IDLE: bit 7 and no debt formed  */
#define XHCI_INJ_QS_CANCEL       3UL  /* CALLING: the cancel if released */
#define XHCI_INJ_QS_HANDOFF      4UL  /* IDLE: a terminal request's, no
                                       * reconnection                    */
#define XHCI_INJ_QS_ENDED        5UL  /* IDLE: aborted before any call   */

/* XhciInjQsBracket's answers. */
#define XHCI_INJ_QS_BR_NOTHING   0UL  /* no exercise                     */
#define XHCI_INJ_QS_BR_ENDED     1UL  /* armed or triggered: ended here  */
#define XHCI_INJ_QS_BR_WAIT      2UL  /* a call made: wait for IDLE      */

/*
 * The exercise, under the controller lock and the injection lock inside it.
 * Busy counts the real requests inside their bracket; Closed is set by a
 * terminal one (a stop, a surprise removal, a removal, a START of a started
 * controller) and cleared only by a start; Abort by every bracket, cleared
 * at an arming. HeldSlot and HeldPA: the withheld command the trigger took.
 * Released: the query returned success with the message released.
 */
typedef struct _XHCI_INJ_QSTOP {
    ULONG State;
    ULONG Arg;
    ULONG Abort;
    ULONG Terminal;
    ULONG Released;
    ULONG Due;          /* a tolerance tick: the delay's or the hold's end */
    ULONG Busy;
    ULONG Closed;
    ULONG HeldSlot;
    ULONG HeldPA;
} XHCI_INJ_QSTOP, *PXHCI_INJ_QSTOP;

/* A start: IDLE, not closed, nothing armed; Busy kept (the START's own
 * bracket is inside it). */
VOID XhciInjQsStart(PXHCI_INJ_QSTOP q);

/* Whether a firing of 2D may arm: XHCI_INJ_QS_R_OK, or the first refusal
 * in the order the reasons are numbered. `mode4` is mode 4 on NT 5. */
ULONG XhciInjQsArmRefusal(const XHCI_INJ_QSTOP *q, ULONG started,
                          ULONG mode4, ULONG isolation);

/* Armed with `arg`: ARMED, no abort, nothing released or held. */
VOID XhciInjQsArm(PXHCI_INJ_QSTOP q, ULONG arg);

/* A command withheld (its slot and TRB) at tick `now`: 1 when that takes
 * the arming (one shot): TRIGGERED, the delay's end in Due. */
ULONG XhciInjQsTrigger(PXHCI_INJ_QSTOP q, ULONG slot, ULONG pa, ULONG now);

/* Bit 7's debt check: a debt stands (`owed` not clear) and names the
 * withheld command (its slot and TRB). */
ULONG XhciInjQsDebtFits(const XHCI_INJ_QSTOP *q, ULONG owed, ULONG slot,
                        ULONG pa);

/*
 * The thread's step at tick `now`, committed in the caller's one hold of
 * both locks: XHCI_INJ_QS_*, the state changed with it. `ready`: the
 * controller started, in D0, not suspended in D0, not suspended, and no
 * stop, removal, power, direct or shutdown route's bit standing; `debt`:
 * XhciInjQsDebtFits. TRIGGERED: the query once the delay has passed, not
 * aborted, Busy 0, not closed and ready - with bit 7 only while `debt`,
 * else NODEBT, no call. QUERIED: HANDOFF for an abort by a terminal
 * request, CANCEL for one by any other or once the hold has passed.
 */
ULONG XhciInjQsStep(PXHCI_INJ_QSTOP q, ULONG now, ULONG ready, ULONG debt);

/* The query made: QUERIED, `released` kept, the hold's end in Due. */
VOID XhciInjQsQueried(PXHCI_INJ_QSTOP q, ULONG released, ULONG now);

/* The cancel's end (made or, for a refused query, not needed): IDLE. */
VOID XhciInjQsEnded(PXHCI_INJ_QSTOP q);

/* A real request's bracket entered: Busy raised, Abort set, and for a
 * terminal request Closed and Terminal; ARMED or TRIGGERED ended here
 * (BR_ENDED, IDLE), CALLING or QUERIED waited for (BR_WAIT), IDLE
 * nothing. Never waits for Busy, so a nested request cannot wait for the
 * one enclosing it. */
ULONG XhciInjQsBracket(PXHCI_INJ_QSTOP q, ULONG terminal);

/* A real request's handling ended: Busy lowered. */
VOID XhciInjQsLeave(PXHCI_INJ_QSTOP q);

#define XHCI_INJ_LAST_BUILT      44UL

/*
 * Task 50.31 (design record 38 section 17), hex 30 to 35: the retry of a
 * controller stopped after repeated failures. A range of their own, apart
 * from XHCI_INJ_LAST_BUILT's (2C is task 50.34's, 2D the query-stop's
 * above, 2E and 2F left free), and taken on a failed controller, which is where
 * they act (XhciInjNeedsLive answers 0 for them).
 *   30 RETRY_FAIL         every recovery attempt refused at its first step,
 *                         charged as a refusal, until `arg` runs of three
 *                         are spent (0 one, 255 until CLEAR)
 *   31 RETRY_ONES         the commit's USBSTS read answers all ones for the
 *                         next `arg` armed tries (0 one)
 *   32 RETRY_PORTSC_ONES  the sweep's PORTSC reads answer all ones until
 *                         CLEAR (port byte one port, 0 every one)
 *   33 RETRY_HOLD         the next armed try's commit not attempted for
 *                         `arg` ticks (0: 50), nothing blocked
 *   34 RECOVER_EARLY      the next `arg` attempts' capability decode answers
 *                         all ones (0 one)
 *   35 RETRY_SWEEP_BREAK  the next `arg` sweeps (0 one) find the leave
 *                         generation moved between their first two port
 *                         holds
 */
#define XHCI_INJ_RETRY_FAIL         0x30UL
#define XHCI_INJ_RETRY_ONES         0x31UL
#define XHCI_INJ_RETRY_PORTSC_ONES  0x32UL
#define XHCI_INJ_RETRY_HOLD         0x33UL
#define XHCI_INJ_RECOVER_EARLY      0x34UL
#define XHCI_INJ_RETRY_SWEEP_BREAK  0x35UL
#define XHCI_INJ_RETRY_FIRST        XHCI_INJ_RETRY_FAIL
#define XHCI_INJ_RETRY_LAST         XHCI_INJ_RETRY_SWEEP_BREAK
#define XHCI_INJ_RETRY_HOLD_TICKS   50UL /* 33's default: 5 s nominal    */

#define XHCI_INJ_CLEAR           0xFFUL  /* every injection ended            */

/* XhciInjTake's verdicts. */
#define XHCI_INJ_TAKE_NONE       0UL  /* nothing new                         */
#define XHCI_INJ_TAKE_FIRE       1UL  /* a built fault or CLEAR              */
#define XHCI_INJ_TAKE_UNKNOWN    3UL  /* no such code: refused, noted        */

/* XhciInjKind: how a fault is carried out. */
#define XHCI_INJ_KIND_OTHER      0UL  /* 01-07, 13, 18, 19 hex and CLEAR:
                                       * hcd_inj.c's own                     */
#define XHCI_INJ_KIND_RETRY      1UL  /* a stop, emulated Halted, a TE       */
#define XHCI_INJ_KIND_HALT       2UL  /* a stop, a Stall naming no TD        */
#define XHCI_INJ_KIND_REFUSED    3UL  /* a refused code, no stop             */
#define XHCI_INJ_KIND_EP0        4UL  /* at the thread's EP0 doorbell        */
#define XHCI_INJ_KIND_BULK       5UL  /* a withheld bulk doorbell (36.22)    */

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

/* The refused code REFUSED_CODE and the EP0 faults carry: design record 17's
 * row on the codes nothing claims (section 3, T3) names Bandwidth Overrun
 * among them. */
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
    /* 36.22: the endpoint's doorbells withheld for a bulk fault; the
     * driver's Stop Endpoint for it ends the withholding once sent. */
    ULONG Bulk;
} XHCI_INJ_EP, *PXHCI_INJ_EP;

/* The trigger's memory: the last sequence seen, and whether one was. */
typedef struct _XHCI_INJ_TRIGGER {
    ULONG Seen;
    ULONG Seq;
} XHCI_INJ_TRIGGER, *PXHCI_INJ_TRIGGER;

/*
 * The register answers the layer gives in place of the hardware's. A port's
 * PEC is answered set from the layer's PED write until the driver's
 * acknowledgement, and its PED answered clear - QEMU ignores the write -
 * until the driver's port reset (PR written) or the device's departure
 * (CCS read clear); an over-current port reads PP clear (OCA and OCC set
 * while held) until the driver's PP write after the release, PP itself never
 * reaching the real port while emulated. USBSTS answers all-ones to the
 * health poll and the containment step for DeadLeft passes
 * (XHCI_INJ_DEAD_FOREVER: until CLEAR), and Bus Master Enable reads back
 * set once NoProof is armed.
 */
typedef struct _XHCI_INJ_REGS {
    ULONG PedPort;
    ULONG PecHeld;
    ULONG PedClear;
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

/* Task 50.30 step 2: declared with XHCI_INJ_HANG above. */

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

/* The PORTSC a read answers. An unreadable PORTSC passes unchanged. A read
 * with CCS clear on the PED port ends the PED-clear answer: the device has
 * gone. */
ULONG XhciInjPortscRead(PXHCI_INJ_REGS r, ULONG port, ULONG raw);

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
/* The endpoint faults (08 to 12 hex, 14 to 17 hex)                        */
/* ----------------------------------------------------------------------- */

/* XHCI_INJ_KIND_* of a fault code. */
ULONG XhciInjKind(ULONG fault);

/* The Hold a fault's injection sets: FAIL, RING, SECOND or NONE. */
ULONG XhciInjHoldOf(ULONG fault);

/* The EP State a fault's context reads answer: Halted for TRANSACTION, the
 * RESET_EP_FAIL and RACE ones, HALT_HALTED and BULK_ERROR, Error for
 * HALT_ERROR, 0 (the real state) for the rest. */
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

/* Task 49.35: XHCI_INJ_RESET_SLOW's hold in ms for `arg` (255 for ever:
 * 0xFFFFFFFF), and whether a reset written `elapsedMs` ago is still held;
 * XhciInjResetAnswer is the register value a read at that moment answers
 * (`opOffset` USBCMD or USBSTS, `cnr` the port byte's choice), any other
 * register, an all-ones read or a hold run out passing unchanged. A Host
 * Controller Event as a controller writes one: completion code `cc`, the
 * cycle bit 0, which no handler reads. */
ULONG XhciInjResetHoldMs(ULONG arg);
ULONG XhciInjResetHeld(ULONG holdMs, ULONG elapsedMs);
ULONG XhciInjResetAnswer(ULONG opOffset, ULONG value, ULONG held, ULONG cnr);
VOID XhciInjHostControllerEvent(XHCI_TRB *ev, ULONG cc);

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

/*
 * XhciInjStopVerdict's `afterPA` for a TD whose last TRB is at `lastIndex`
 * of the ring at `basePA`: where QEMU's fetch leaves its dequeue, the next
 * slot of the segment. After the ring's last usable slot that is the Link
 * TRB, not the ring's next TRB at index 0: QEMU follows a Link only when it
 * fetches the TRB beyond it (hcd-xhci.c, xhci_ring_fetch).
 */
ULONG XhciInjAfterPA(ULONG basePA, ULONG lastIndex);

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
