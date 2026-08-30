/*
 * xhci_hw.h - the driver-only side of the split design doc 03 section 2 asks
 * for: MMIO accessors, PCI config access, and bounded waits.
 *
 * Everything declared here needs the DDK or a usbport service, so none of it
 * can live in xhci.h (which the host tests compile). The rule that makes the
 * split worth having: files in `src/sources`' pure list encode and decide,
 * this layer is the only place that dereferences BAR0.
 *
 * Implemented in src/xhci_pci.c. C89 only; every function carries its IRQL.
 */

#ifndef XHCI_HW_H
#define XHCI_HW_H

#include "xhci.h"
#include "xhci_usbport.h"

/*
 * The registration packet, defined in src/xhci_dispatch.c. usbport writes its
 * 16 service pointers into it during registration and the miniport calls them
 * back through it for the life of the driver, so this layer needs it to reach
 * UsbPortWait and UsbPortReadWriteConfigSpace. It is a file-scope object in
 * both flavours - "static" in the C sense was never what kept it alive.
 */
extern USBPORT_REGISTRATION_PACKET XhciRegPacket;

/* ------------------------------------------------------------------ */
/* MMIO                                                                */
/* ------------------------------------------------------------------ */

/*
 * All offsets are byte offsets from BAR0, and every accessor asserts nothing
 * about them: the bounds were established once, by XhciDeriveHcInfo, against
 * the length usbport reported. Callers add the base from ext->HcInfo.
 *
 * IRQL: any. These are plain register accesses - no allocation, no waiting.
 */
ULONG XhciRead32(PXHCI_EXTENSION ext, ULONG barOffset);
VOID XhciWrite32(PXHCI_EXTENSION ext, ULONG barOffset, ULONG value);

/*
 * A 64-bit register (CRCR, DCBAAP, ERSTBA, ERDP) as the two 32-bit halves the
 * only bus this driver runs on can produce, in the order the spec requires of
 * a system that cannot issue Qword accesses: low DWORD first, high DWORD
 * second (spec 5.1, p.337). The high half is always zero - there is no 64-bit
 * DMA here - so the caller supplies only the low one.
 */
VOID XhciWrite64(PXHCI_EXTENSION ext, ULONG barOffset, ULONG low);

/* Operational, runtime and interrupter-0 registers, by their offsets within
 * each block. Valid only once ext->HcInfoStatus is XHCI_HC_OK. */
ULONG XhciReadOp(PXHCI_EXTENSION ext, ULONG opOffset);
VOID XhciWriteOp(PXHCI_EXTENSION ext, ULONG opOffset, ULONG value);
ULONG XhciReadRt(PXHCI_EXTENSION ext, ULONG rtOffset);
VOID XhciWriteRt(PXHCI_EXTENSION ext, ULONG rtOffset, ULONG value);
ULONG XhciReadIr0(PXHCI_EXTENSION ext, ULONG irOffset);
VOID XhciWriteIr0(PXHCI_EXTENSION ext, ULONG irOffset, ULONG value);

/*
 * PORTSC of one 1-based logical port. The bound is ext->HcInfo.MaxPorts, and
 * XhciDeriveHcInfo has already proved the whole PORTSC array fits the mapping -
 * so this is the second line, for a caller that computed a port number rather
 * than read one from the port map. A refused read answers all-ones (what an
 * undecoded access returns) and a refused write does nothing.
 *
 * Never compose the value by hand: every write goes through the builders in
 * src/xhci_port.c, because a read-modify-write of PORTSC that looks right is a
 * disabled port or a discarded connect.
 *
 * IRQL: any.
 */
ULONG XhciReadPortsc(PXHCI_EXTENSION ext, ULONG port);
VOID XhciWritePortsc(PXHCI_EXTENSION ext, ULONG port, ULONG value);

/*
 * Ring one doorbell. `slot` is 0 for the Host Controller Command doorbell and
 * the Slot ID otherwise; `value` is DB Target 7:0 with DB Stream ID 31:16, which
 * for the command ring is 0 - "asserting the Host Controller Command value in
 * the DB Target field and '0' in the DB Stream ID field" (4.6.1, p.92).
 *
 * Write-only, and bounded by MaxPorts' doorbell equivalent - see the definition.
 * IRQL: any.
 */
VOID XhciWriteDoorbell(PXHCI_EXTENSION ext, ULONG slot, ULONG value);

/*
 * The XHCI_READ32 the pure capability walk reads BAR0 through
 * (src/xhci_caps.c). `context` is the PXHCI_EXTENSION.
 */
ULONG XhciBarReader(PVOID context, ULONG byteOffset);

/* ------------------------------------------------------------------ */
/* PCI configuration space                                             */
/* ------------------------------------------------------------------ */

/*
 * Read `length` bytes of this controller's PCI config space through usbport's
 * service. Identification and quirk selection only - never BAR discovery
 * (docs/contributing/implementation-invariants.md, "PnP Resources").
 *
 * Returns an MPSTATUS. IRQL: PASSIVE_LEVEL (usbport's implementation goes out
 * to the PCI bus driver).
 */
MPSTATUS XhciReadPciConfig(PXHCI_EXTENSION ext,
                           ULONG offset,
                           PVOID buffer,
                           ULONG length);

/*
 * Write `length` bytes of this controller's PCI config space.
 *
 * **One caller, one register, one bit.** PCI configuration is the bus driver's
 * and usbport's to manage, and this driver reads it for identification only
 * (docs/contributing/implementation-invariants.md, "PnP Resources"). The exception is Bus
 * Master Enable, and only as the quiesce path's last resort: when the MMIO
 * window has stopped decoding or the controller will not halt, clearing BME is
 * the sole remaining way to prove the xHC cannot reach the common buffer
 * usbport is about to reclaim - which is the second of the two proofs
 * "DMA Teardown" already names.
 *
 * Returns an MPSTATUS. IRQL: PASSIVE_LEVEL.
 */
MPSTATUS XhciWritePciConfig(PXHCI_EXTENSION ext,
                            ULONG offset,
                            PVOID buffer,
                            ULONG length);

/* PCI configuration header offsets this driver reads. */
#define XHCI_PCI_VENDOR_DEVICE  0x00UL      /* VID 15:0, DID 31:16 */
#define XHCI_PCI_COMMAND        0x04UL      /* two bytes                   */
#define XHCI_PCI_INTERRUPT_PIN  0x3DUL      /* one byte; 0 = no INTx */

#define XHCI_PCI_COMMAND_MSE    0x0002U     /* Memory Space Enable         */
#define XHCI_PCI_COMMAND_BME    0x0004U     /* Bus Master Enable           */

/* ------------------------------------------------------------------ */
/* Bounded waits                                                       */
/* ------------------------------------------------------------------ */

/*
 * Poll `barOffset` until `(value & mask) == want`, or until `timeoutMs`
 * elapses. Returns 1 on success and 0 on timeout or on an all-ones read, which
 * ends the wait at once: a window that has stopped decoding satisfies any wait
 * whose wanted bits are all set, and no amount of waiting brings it back. On
 * either refusal *lastValue (when non-NULL) holds the final read, which is the
 * only thing worth logging.
 *
 * Two-phase on purpose. Everything this is used for - the BIOS handoff, the
 * halt, the reset, CNR - normally completes in microseconds, but each has a
 * specification-derived timeout measured in hundreds of milliseconds. A pure
 * KeStallExecutionProcessor loop would busy-wait the whole tail; a pure
 * UsbPortWait loop would pay that service's ~10 ms scheduling granularity even
 * for a controller that was already done. So: check first, then stall in short
 * steps, then sleep.
 *
 * IRQL: PASSIVE_LEVEL - UsbPortWait is KeDelayExecutionThread
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 6) - **except while
 * `ext->InitBelowPassive` is set**, where the sleep phase is skipped entirely
 * and the wait is the 10 ms stall and nothing more. That is the in-place
 * recovery (task 13-R.1) running the same sequence from a DPC; a bit that has
 * not settled inside the stall makes the attempt refuse, and the retry comes
 * from the next health poll.
 */
ULONG XhciWaitForBits(PXHCI_EXTENSION ext,
                      ULONG barOffset,
                      ULONG mask,
                      ULONG want,
                      ULONG timeoutMs,
                      ULONG *lastValue);

/*
 * Sleep for a fixed interval with no register to poll - the port-power settle
 * delay is the only such wait in the driver, because the spec states it as a
 * duration rather than as a condition ("the host is required to have power
 * stable to the port within 20 milliseconds", 5.4.8, p.371).
 *
 * Falls back to stalling when UsbPortWait is unavailable, which is the same
 * degradation XhciWaitForBits takes: a busy-wait of this length is bad, and
 * skipping the delay entirely is worse. It takes that same fallback while
 * `ext->InitBelowPassive` is set, where sleeping is not merely unavailable but
 * illegal (task 13-R.1).
 *
 * IRQL: PASSIVE_LEVEL, or DISPATCH_LEVEL with `ext->InitBelowPassive` set.
 */
VOID XhciDelayMs(PXHCI_EXTENSION ext, ULONG milliseconds);

/* ------------------------------------------------------------------ */
/* Controller initialization (src/xhci_init.c)                         */
/* ------------------------------------------------------------------ */

/*
 * The whole start-up sequence, from the resource packet to a **running**
 * controller with DCBAAP, the command ring and the event ring programmed, its
 * managed USB 2.0 ports powered, and every interrupt source still masked -
 * usbport calls EnableInterrupts once StartController returns success, and that
 * is where the unmasking belongs. Roadmap Phase 4 tasks 2, 3 and 5; the
 * sequence is docs/usb-xhci-info/xhci-programming.md "Initialization Sequence" steps 0-16.
 *
 * Records its verdict in ext->InitStep / ext->InitStatus either way, and sets
 * XHCI_EXT_FLAG_INITIALIZED only on success.
 *
 * IRQL: PASSIVE_LEVEL, or DISPATCH_LEVEL with `ext->InitBelowPassive` set
 * (task 13-R.1). The second form is the in-place recovery: XhciRecoverController
 * sets that bit and calls this from a DPC, and every wait underneath -
 * XhciWaitForBits, XhciDelayMs - takes its stall-only path there.
 *
 * `resources` may be **NULL**, which means "nobody is handing anything over -
 * reinitialize the controller the extension already describes". That is the
 * resume path: same mapping, same common buffer, same interrupt, all of them
 * already copied into the extension by StartController.
 */
MPSTATUS XhciInitController(PXHCI_EXTENSION ext, PUSBPORT_RESOURCES resources);

/*
 * **Task 13-R.1: bring a controller this driver declared failed back into
 * service, in place, from a context this driver reaches on its own.**
 *
 * The whole reason it exists is a negative read off both shipping `usbport.sys`
 * builds: after `ResetController` returns, usbport arranges
 * nothing. It never calls `StopController`/`StartController` on a timer, from a
 * watchdog, or from a failed transfer - only a PnP or power transition reaches
 * them - and it reads no verdict from `CheckController`, which is a `VOID`
 * slot. So the stop/start `xhciResetController`'s own trace text asks for never
 * arrives on an ordinary running machine, and the latch was terminal
 * (docs/usb-xhci-info/usbport-miniport-abi.md, the two subsections after the
 * `UsbPortInvalidateController(RESET)` box; docs/contributing/runs/run-13e.md,
 * Finding S).
 *
 * This is `XhciResumeController`'s reinitialization branch, reached from the
 * latch instead of from a power IRP: drop every device record (completing the
 * transfers usbport is holding for them), re-run `XhciInitController` from
 * HCRST, put the interrupt enables back if usbport had them on, and announce
 * whatever the fresh root-hub seed found. HCRST takes every slot and every port
 * back to its default state, so the devices that were on the bus disconnect and
 * usbport enumerates them again - which is the same thing that happens when a
 * hub is unplugged and plugged back in, and is why the recovery does not have
 * to preserve anything.
 *
 * Returns 1 if the controller is running again, 0 if it is not - in which case
 * `ControllerFailed` is still set and `RecoveryLastStep`/`RecoveryLastStatus`
 * say where the sequence refused.
 *
 * **IRQL: DISPATCH_LEVEL, holding no usbport lock** - the contract of
 * `UsbPortRequestAsyncCallback`'s own DPC, which is the only context in which
 * this is legal. It sets `ext->InitBelowPassive` for the duration, which is what
 * keeps every bounded wait a stall and keeps the PASSIVE-only configuration-space
 * service out of the sequence. It must **not** be called from `ResetController`
 * itself, which runs inside one of usbport's spin locks.
 */
ULONG XhciRecoverController(PXHCI_EXTENSION ext);

/*
 * Read the six capability registers and decode them into `info`, returning an
 * XHCI_HC_* status. The init sequence uses it twice - before the handoff and
 * after the reset - and `ResumeController` uses it a third time to ask whether
 * the far end of the mapping is still the controller that went down. One
 * implementation because "is this the same controller" must not have two
 * answers. IRQL: any.
 */
ULONG XhciDeriveControllerInfo(PXHCI_EXTENSION ext, PXHCI_HC_INFO info);

/*
 * CRCR, preserving its RsvdP field - bits 5:4 (Table 5-24, p.367).
 *
 * The other four RsvdP-carrying registers this driver writes (CONFIG, DNCTRL,
 * ERSTSZ, ERSTBA) have all their call sites inside src/xhci_init.c and keep
 * private helpers there. CRCR is the one with a caller elsewhere - the command
 * engine's abort - so its two entry points are exported rather than duplicated,
 * for the reason XhciDeriveControllerInfo is: a rule about what a register write
 * must carry back should not have two implementations that can drift.
 *
 * `XhciWriteCrcr` is the pointer form: it writes `pointerLow` into 31:6 and
 * `definedBits` (RCS, and nothing else this driver uses) into 2:0, both halves
 * of the 64-bit register, low DWORD first. The init sequence and the restore's
 * step 7 are its callers, and both write it on a stopped command ring, which is
 * the only state in which the pointer and RCS are latched at all. `crcrRead`
 * may be NULL; when it is not, it receives the operand that was read.
 *
 * `XhciWriteCrcrAbort` is the Command Abort form, and it is separate because it
 * **declines on a stopped ring**: CA is ignored while CRR = '0', but the pointer
 * bits composed alongside it are not, so writing CA there would repoint the
 * command ring at address 0. It returns 1 only when CA was actually written.
 *
 * Both return 0 having written nothing when no valid operand could be read - the
 * all-ones rule the interrupt paths arrived at. IRQL: any.
 */
ULONG XhciWriteCrcr(PXHCI_EXTENSION ext,
                    ULONG pointerLow,
                    ULONG definedBits,
                    ULONG *crcrRead);
ULONG XhciWriteCrcrAbort(PXHCI_EXTENSION ext, ULONG *crcrRead);

/*
 * The suspend/resume pair: the suspend masks the interrupt enables and halts the
 * controller, the resume reinitializes it. Neither depends on usbport having
 * called DisableInterrupts first, because Win98's idle suspend/resume pairs are
 * not observed to be bracketed that way.
 *
 * **Since task 6-B.6 the pair carries the CSS/CRS protocol.** Phase 4's
 * deferral rested on there being no Slot, Endpoint or Stream state to lose, so a
 * reinitialisation was indistinguishable from a restore; Phase 6 creates all
 * three, and Win98 idle-suspends within about a second of every start, so a
 * resume that reinitialised would drop every device context an enumerated bus
 * depends on. The suspend now attempts a Save State and the resume a Restore;
 * **the error path is the one the target VMs exercise** - QEMU implements CRS as
 * "set SRE" and nothing else - and it ends in XhciSlotInvalidateAll, which tells
 * usbport its addressed devices are gone rather than leaving the address map
 * pointing at slots the xHC no longer has.
 *
 * IRQL: PASSIVE_LEVEL.
 */
VOID XhciSuspendController(PXHCI_EXTENSION ext);
MPSTATUS XhciResumeController(PXHCI_EXTENSION ext);

/*
 * usbport's frame-number callback (task 6-B.1): MFINDEX >> 3, extended to 32
 * bits and **monotone across a halt, a suspend and a controller reset**.
 *
 * That last property is the deliverable rather than a nicety. usbport's
 * post-open wait is an uncapped loop at PASSIVE_LEVEL that ends only when this
 * number passes a frame stamped earlier (batch 6-0), MFINDEX stops counting on a
 * halted xHC and restarts at zero after HCRST, and Win98 idle-suspends within
 * about half a second of every start - so a reader that returned the register
 * would hang the enumerating thread, and one that returned an absolute value
 * would go backwards. Answers are therefore deltas, and a controller that cannot
 * be read is answered with an increment rather than a repeat.
 *
 * IRQL: <= DISPATCH_LEVEL. Takes the controller lock; callers must not hold it.
 */
ULONG XhciFrameNumber(PXHCI_EXTENSION ext);

/*
 * Task 9-A.1. The controller's **current Frame Index** - MFINDEX bits `13:3` -
 * for the isochronous Valid Frame Window test, or a refusal.
 *
 * Returns 1 having written an 11-bit Frame ID, or 0. A 0 is the ordinary answer
 * rather than a fault: it is what a suspended, halted or reinitialising
 * controller gives, and what a driver whose published frame axis has drifted
 * from MFINDEX gives (see `XHCI_EXTENSION.FrameCongruent`). The isochronous
 * submit path answers a 0 by scheduling the whole request with SIA, which is
 * always legal.
 *
 * **The opposite lock discipline to `XhciFrameNumber`**, which is why it is a
 * second entry point rather than an output of the first: this one is called from
 * inside the submit path, which already holds the controller lock, and taking it
 * again would deadlock. It also deliberately does *not* publish or advance
 * anything - it is a read.
 *
 * IRQL: DISPATCH_LEVEL, controller lock **held**.
 */
ULONG XhciFrameIdNow(PXHCI_EXTENSION ext, ULONG *frameId);

/*
 * Resample the published frame axis from MFINDEX. Called from the health poll,
 * and it is what bounds the gap between two readings of the eleven-bit Frame
 * Index below one 2,048-frame lap - the gap across which `XhciFrameIdNow` would
 * otherwise reconstruct the high bits a lap short and call a late request early.
 *
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
VOID XhciFrameSample(PXHCI_EXTENSION ext);

/*
 * Task 13-R.3.5. Advance the health poll's own millisecond clock,
 * XHCI_EXTENSION.PollClockMs, from MFINDEX. Called once per health poll, and it
 * is the only writer.
 *
 * **This is the clock every age and stall threshold in this driver is measured
 * on**, and it exists because the poll counts those thresholds used to be
 * expressed in are a measure of somebody else's timer rather than of time: the
 * E460 polls this miniport at 36-80 ms, so a 64-poll budget sized to be 32 s
 * was 2.3-5.1 s - at or under the 5 s watchdog it was meant to sit far behind
 * (docs/contributing/runs/run-13e.md, Finding V).
 *
 * **A second entry point rather than an output of XhciFrameSample**, which is
 * next to it in the poll and reads the same register. The two axes answer
 * different questions and must not share an admission gate: the published frame
 * axis refuses while it is unsynced or non-congruent, because a Frame ID taken
 * from an axis in that state names the wrong frame - but a watchdog clock that
 * stopped whenever an isochronous Frame ID became unclaimable would be a
 * watchdog that stops for reasons that have nothing to do with it.
 *
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
VOID XhciPollClockAdvance(PXHCI_EXTENSION ext);

/*
 * The controller common buffer, as an address in each of the two spaces. The
 * slot layer carves device contexts and EP0 rings out of it at offsets
 * src/xhci_mem.c computes, so both halves are needed side by side and both are
 * exported rather than re-derived: `StartVA + offset` written out at a second
 * call site is how a carve and its user drift apart.
 *
 * Volatile because this is cached DMA memory
 * (docs/contributing/design/04-controller-common-buffer.md section 6). IRQL: any.
 */
volatile ULONG *XhciCommonAt(PXHCI_EXTENSION ext, ULONG offset);
ULONG XhciCommonPA(PXHCI_EXTENSION ext, ULONG offset);

/*
 * Mask and unmask this controller's interrupt enables, in the orders
 * docs/contributing/implementation-invariants.md, "Interrupt Ordering" requires - USBCMD.INTE
 * before IMAN.IE on the way down, the reverse on the way up.
 *
 * The mask is task 6's DisableInterrupts callback outright, and is also called
 * by the suspend and quiesce paths, which must mask without waiting to be
 * asked. The unmask is only *half* of EnableInterrupts - see
 * XhciEnableInterrupts below for the other half and why it cannot be omitted.
 *
 * IRQL: any.
 */
VOID XhciMaskInterrupts(PXHCI_EXTENSION ext);

/*
 * Reads XhciIsr gives IMAN before it gives up on forming a valid
 * RsvdP-preserving operand and falls back to a counted literal
 * (XHCI_EXTENSION.IsrImanLiteralAcks). Same shape and same reasoning as
 * XHCI_INTERRUPT_WRITE_ATTEMPTS in src/xhci_init.c - a glitch that clears on the
 * next access is covered, and nothing longer is, because this budget is spent at
 * DIRQL where no wait of any kind is permitted.
 *
 * Declared here rather than kept private to src/xhci_evt.c for the reason the
 * bus-master budget below gives: the fallback is part of this ISR's stated
 * contract, and the host vector that pins it counts exactly this many reads.
 */
#define XHCI_ISR_IMAN_READ_ATTEMPTS   3UL

/* Both return through ext->InterruptDeliverySuppressed, which the ISR reads as the
 * condition on its decline gates. XhciUnmaskInterrupts additionally answers
 * whether it applied: 0 means a started controller has delivery still
 * suppressed with
 * nothing scheduled to set them, and the caller owes an escalation once the
 * controller lock is released. IRQL: any. */
ULONG XhciUnmaskInterrupts(PXHCI_EXTENSION ext);

/*
 * Put IMAN.IE back after XhciIsr cleared it, and confirm by read back. The
 * event DPC's last act, and since task 9 made the ISR clear IE this is the only
 * thing that restores interrupt delivery on a running controller - so it carries
 * the same operand, read-back and bounded-retry rules as the two above rather
 * than the bare read-modify-write it started as. Touches IMAN only, so it does
 * **not** update ext->InterruptDeliverySuppressed: that word is a claim about
 * both enables and this function has no evidence about USBCMD.INTE.
 *
 * Returns 1 if IE is confirmed set; 0 means the caller owes an escalation once
 * the controller lock is released, for the same reason a refused unmask does.
 * IRQL: DISPATCH_LEVEL, controller lock held.
 */
ULONG XhciRearmInterrupter(PXHCI_EXTENSION ext);
#ifdef XHCI_FIX_PORT_POLL
/* Bench candidate W10 for Finding 3 - see src/xhci_rh.c. Controller lock
 * RELEASED. IRQL: <= DISPATCH_LEVEL. */
VOID XhciRhPortPollSweep(PXHCI_EXTENSION ext);
#endif
#ifdef XHCI_FIX_PORT_POLL_SLOW
#ifndef XHCI_FIX_PORT_POLL
#error XHCI_FIX_PORT_POLL_SLOW divides the W10/W11 sweep, so it needs XHCI_FIX_PORT_POLL
#endif
/*
 * **Bench candidate W15 for Finding 3** (`run-13e.md` P9): run the sweep on
 * every Nth health poll instead of every poll, so a detection the sweep
 * mediated is separated from one an event delivered by a latency a human can
 * read off a wall clock. W11POLL's "~0.5 s - one health-poll interval" is the
 * single observation the no-event claim rests on, and an operator cannot
 * distinguish 0.5 s from instant against OS enumeration noise; 16 polls at
 * usbport's nominal 500 ms is ~8 s, so an event-driven detection stays at
 * enumeration latency (a second or two) while a sweep-mediated one averages
 * ~4 s from a uniform phase - and three repetitions of a plug cannot all land
 * under 2 s by chance ((1/4)^3).
 */
#define XHCI_RH_SWEEP_SLOW_POLLS    16UL
#endif
#ifdef XHCI_FIX_RH_GATE
/* Bench candidate W7 for Finding 3 - see src/xhci_rh.c. Controller lock
 * RELEASED. IRQL: <= DISPATCH_LEVEL. */
VOID XhciRhGateWatchdog(PXHCI_EXTENSION ext);
#endif

/*
 * Stop the controller: mask its interrupt sources, clear USBCMD.RUN and wait
 * for HCHalted; failing that, clear PCI Bus Master Enable and confirm it.
 * Its halt half is admitted by **either** XHCI_EXT_FLAG_RUNNING (this driver
 * wrote R/S) **or** XHCI_EXT_FLAG_HW_RUNNING (a valid USBSTS read the xHC
 * executing that this driver did not start), so a start that refused during the
 * preflight is still a start that wrote nothing, while a start that *found* the
 * controller running does not walk away from it. The lock-side half - retiring
 * the command generation, masking the enables, closing DPC admission - runs
 * whenever INITIALIZED is set, whatever those two say.
 *
 * **Returns 1 only when the controller is provably unable to do DMA**, and
 * clears both running flags only then - so a caller that has to know whether
 * the common buffer is safe to reclaim can ask, and a failed attempt leaves
 * something for the next one to retry. Returning 0 is a statement that the xHC
 * may still be writing into that buffer, and **a caller that is about to let
 * usbport reclaim it owes XhciFailClosedDma** rather than a trace: see the
 * contract there for why reporting alone was not enough.
 *
 * This is the **shared** half of the lifecycle: everything a stop and a suspend
 * both want. What only a stop wants - clearing port power on unload (4.19.4,
 * p.296), which has to happen while the controller still runs and therefore
 * before this - is in XhciStopController below, which calls this. Suspend and
 * resume call it directly.
 *
 * IRQL: PASSIVE_LEVEL (it waits), or DISPATCH_LEVEL with
 * `ext->InitBelowPassive` set (task 13-R.1) - the failure exits of
 * XhciInitController reach this from the in-place recovery's DPC, and the waits
 * underneath take their stall-only path there.
 */
ULONG XhciQuiesceController(PXHCI_EXTENSION ext);

/*
 * The ordered teardown: take port power off the ports this driver powered,
 * while the controller is still running and PORTSC is still writable, then
 * quiesce it. Roadmap Phase 4 task 8.
 *
 * Returns exactly what XhciQuiesceController returns - 1 only when the xHC is
 * provably unable to do DMA - so **every caller after which usbport reclaims
 * the common buffer owes XhciFailClosedDma on a 0**. Those callers are the
 * StopController callback and the two exits of XhciInitController that follow a
 * written R/S.
 *
 * Not for suspend: an idle suspend/resume pair must not drop VBus on every
 * connector. The long note at the definition also records the two steps this
 * deliberately does *not* take - zeroing the pointer registers, which the
 * primary Event Ring's ERSTSZ cannot express, and an HCRST after the halt,
 * which would re-power every port it just took down.
 *
 * IRQL: PASSIVE_LEVEL (it waits), or DISPATCH_LEVEL with
 * `ext->InitBelowPassive` set (task 13-R.1) - the StopController callback is
 * always PASSIVE, and the two XhciInitController exits named above are not when
 * the in-place recovery is what called it.
 */
ULONG XhciStopController(PXHCI_EXTENSION ext);

/*
 * Attempts that quiesce's bus-master fallback makes before it answers 0. Same
 * shape as XHCI_INTERRUPT_WRITE_ATTEMPTS and for a sharper reason: the answer to
 * a final 0 is now a bugcheck, so a single flaky config cycle must not be what
 * decides to take the machine down. A bit that genuinely will not clear reads
 * back set on every attempt, so the retry cannot manufacture a proof - which is
 * why it is declared here, with the function whose contract it is part of,
 * rather than kept private like the interrupt budget.
 */
#define XHCI_BUS_MASTER_ATTEMPTS 3UL

/*
 * The answer to a quiesce that returned 0 on a path after which usbport
 * reclaims the common buffer.
 *
 * **Returning normally there is not a neutral act.** usbport frees that
 * allocation as soon as the callback returns, the pool hands the pages to
 * somebody else, and a bus master this driver could not stop keeps writing event
 * TRBs and dereferenced context pointers into them. The corruption is silent,
 * arbitrary, and arrives attributed to whichever driver was unlucky enough to be
 * given the memory - which is the one failure mode nothing later can diagnose.
 * docs/contributing/implementation-invariants.md, "DMA Teardown" already asks for a cold boot
 * in this state; this is what makes the machine take one.
 *
 * **The mechanism is usbport's own UsbPortBugCheck** (registration packet
 * +0x11C), not KeBugCheckEx, so the fail-closed path costs no new import on
 * either target. Verified in both shipping builds rather than taken from the
 * ReactOS mirror, the way UsbPortInvalidateController had to be: SP4 VA
 * 0x11C2E and NUSB VA 0x11B72 are the same five instructions -
 * `KeBugCheckEx(0xD2, 0, 0, 0, 0)` then `ret 4` - reached through IAT index 18
 * (thunk 0x2CA80) and 17 (thunk 0x2C29C) of each image's NTOSKRNL.EXE import
 * table. Extract in tools/{nusb,win2ksp4}-extracted/usbport-bugcheck-disasm.txt.
 *
 * Two consequences of that measurement shape the callers. The service passes
 * **no parameters** - all four bugcheck arguments are hard zeros - so the
 * extension's counters and the trace are the only diagnosis that survives, and
 * both are written before the call. And it takes the miniport extension purely
 * to satisfy the slot signature; it never reads it.
 *
 * Not called on the suspend path, and that asymmetry is the point rather than an
 * omission: a suspend leaves the common buffer allocated to this driver, so a
 * controller that would not stop is writing into memory it already owns. That is
 * a fault to count (SuspendFailures), not a reason to take the machine down.
 *
 * IRQL: any. Does not return when the service is present.
 */
VOID XhciFailClosedDma(PXHCI_EXTENSION ext);

/*
 * "The host is required to have power stable to the port within 20
 * milliseconds of the '0' to '1' transition of PP. If PPC = '1' software is
 * responsible for waiting 20 ms. after asserting PP, before attempting to
 * change the state of the port" (5.4.8, p.371).
 *
 * Paid once after the whole port pass rather than once per port: the delays
 * overlap, since every transition was written before the wait begins, and 12
 * managed ports on a fleet controller would otherwise be a quarter of a second
 * of the start-device path. Phase 5 also advertises this figure to the hub
 * stack through RH_GetRootHubData (docs/contributing/implementation-invariants.md, "Wait
 * Primitives"); the two are complementary - that one covers the ports usbport
 * powers later, this one covers the ports powered here.
 */
#define XHCI_PORT_POWER_SETTLE_MS   20UL

/*
 * How often PP is re-read while waiting for it to reflect a write, up to
 * XHCI_PORT_POWER_SETTLE_MS in total. This is the *other* allowance - footnote
 * 91 to Table 5-27 (p.375) lets the flag lag a change in either direction, so
 * it applies to the deassertions too, which owe no flat delay. A pass whose
 * ports already report the wanted state never reaches the first sleep.
 */
#define XHCI_PORT_POWER_POLL_MS     5UL

/*
 * T(DRSMDN): how long resume signalling must be driven on a USB 2.0 port before
 * software may end it. "Software shall ensure that resume is signaled for at
 * least 20 ms (TDRSMDN) ... After TDRSMDN is complete, software shall write a
 * '0' (U0) to the PLS field" (4.15.2.2, p.257; the device-initiated path times
 * the same interval from the transition to the Resume state, 4.15.2.1, p.256).
 *
 * It is here rather than in the pure core because it is not a property of the
 * write - it is the reason the two halves of a resume cannot be issued from one
 * callback.
 */
#define XHCI_PORT_RESUME_SIGNAL_MS  20UL

/*
 * What the resume's timer is actually asked for, and why it is not the figure
 * above.
 *
 * T(DRSMDN) is a **minimum duration of bus signalling**, so the error direction
 * is not symmetric: signalling for longer than 20 ms is legal, and ending it
 * early is a protocol violation that leaves the device asleep while both sides
 * report success. The timer is usbport's - a standalone `KeSetTimer` per call
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 6) - so its resolution is the system
 * clock tick, 10-15 ms on both targets, and the interval this driver actually
 * needs starts at the `PLS = 15` write rather than at the `KeSetTimer` a few
 * instructions later. Asking for exactly the minimum leaves nothing for either.
 * One tick of margin costs a sleeping device 10 ms of resume it did not need.
 */
#define XHCI_PORT_RESUME_TIMER_MS   (XHCI_PORT_RESUME_SIGNAL_MS + 15UL)

/*
 * How long a root-port reset may take before the watchdog concludes its PRC is
 * not coming.
 *
 * A **deadline**, not a floor, so it errs the other way: too short aborts a
 * reset that was going to finish, too long only delays a diagnosis. USB 2.0
 * root-port reset signalling is tens of milliseconds (TDRSTR, 10 ms minimum,
 * driven by the xHC itself once PR is written - Table 5-27, p.374), and usbhub's
 * own patience for a port reset is measured in seconds, so a bound well above
 * the signalling and well below the layer above it is what this is.
 */
#define XHCI_PORT_RESET_TIMEOUT_MS  500UL

/*
 * How long an armed port operation may survive before the health poll concludes
 * that nothing is going to time it.
 *
 * **This is not a second timeout competing with the two above.** It exists for
 * the one failure `UsbPortRequestAsyncCallback` cannot report: it "returns 0 on
 * success **and** 0 when its pool allocation fails"
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 6), so a port can be armed with no
 * callback ever scheduled for it - and since only a callback disarms a port,
 * that port would refuse every later reset for the life of the driver. The
 * command engine carries the same detector for the same reason
 * (XHCI_COMMAND_AGE_MS).
 *
 * 8 s against a legitimate worst case of one XHCI_PORT_RESET_TIMEOUT_MS
 * interval, and the margin is what makes this a detector of a timer that was
 * never armed rather than a race with one that was.
 *
 * **Task 13-R.3.5 is why this is a time and not a count.** As
 * XHCI_PORT_AGE_POLLS = 16 it was 0.6-1.3 s on the E460 rather than 8 s - so
 * the margin over the 500 ms deadline it has to clear was 1.2-2.6x instead of
 * 16x, and a reset finishing late could be retired while its own timer was
 * still legitimately running. See the sizing rule above XHCI_COMMAND_AGE_MS;
 * the relationship to XHCI_PORT_RESET_TIMEOUT_MS is checked at compile time in
 * src/xhci_rh.c.
 */
#define XHCI_PORT_AGE_MS            8000UL

/*
 * What the copied async-timer context carries for a port operation.
 *
 * The same three-part identity the command engine's context carries and for the
 * same reasons (see XHCI_COMMAND_TIMEOUT below): `Epoch` separates one start
 * from the next, because usbport zeroes the whole miniport extension before
 * every StartController; `Generation` separates one operation on a port from the
 * next; and `Operation` is which of the two rules applies when the callback
 * fires. `HubPort` is which shadow it belongs to - a *hub* port, since that is
 * what indexes the shadow array.
 */
typedef struct _XHCI_PORT_TIMEOUT {
    ULONG Epoch;
    ULONG Generation;
    ULONG HubPort;
    ULONG Operation;    /* XHCI_PORT_OP_RESET or XHCI_PORT_OP_RESUME */
} XHCI_PORT_TIMEOUT, *PXHCI_PORT_TIMEOUT;

/* ------------------------------------------------------------------ */
/* The interrupt path (src/xhci_evt.c)                                 */
/* ------------------------------------------------------------------ */

/*
 * How many events one DPC pass will consume before it stops and lets the next
 * interrupt collect the rest. Four laps of the event ring is far more than any
 * real burst - the ring holds 256 - and the bound exists only so that a
 * controller producing events faster than software consumes them cannot own a
 * CPU at DISPATCH_LEVEL indefinitely. Stopping early is safe: xhciPublishErdp
 * still releases Event Handler Busy, and a non-empty ring re-asserts the
 * interrupter by itself (spec 4.17.5, p.270). A pass that reaches the bound
 * then peeks once more: a ring that reads empty at the bound counts as a
 * settled pass (`DrainBoundEmptyHits`, see the DPC's settle gate in
 * src/xhci_evt.c), and one that does not leaves the settle to the pass that
 * empties it.
 *
 * `XhciEventDiscardStale` reuses the constant and **not** this reason: the
 * restore has no producer to outrun, so what the bound does there is bound a
 * walk of a ring nothing is refilling. Audit round 6 separated the two; a
 * change to this value has to be argued for both.
 */
#define XHCI_DPC_MAX_EVENTS     (XHCI_EVENT_RING_TRBS * 4UL)

/*
 * How often the dequeue pointer is published mid-drain. The xHC decides the
 * ring is full from the pointer software has advertised, so a long burst
 * consumed without publishing produces Event Ring Full against a DPC that is
 * actively draining.
 */
#define XHCI_ERDP_PUBLISH_EVERY 32UL

/*
 * The miniport ISR body. Returns TRUE only when USBSTS.EINT proves the
 * interrupt is this controller's, having acknowledged EINT and then IMAN.IP;
 * usbport queues the miniport DPC off that TRUE. Touches no software state
 * beyond the diagnostic counters. IRQL: DIRQL.
 */
BOOLEAN XhciIsr(PXHCI_EXTENSION ext);

/*
 * The miniport DPC body: drain the event ring until the Cycle Bit says empty,
 * publishing ERDP as it goes and releasing Event Handler Busy at the end.
 * `enableInterrupts` is usbport's own interrupt-enabled state; when it is FALSE
 * this pass must not re-arm IMAN.IE. IRQL: DISPATCH_LEVEL.
 */
VOID XhciEventDpc(PXHCI_EXTENSION ext, BOOLEAN enableInterrupts);

/*
 * Receive and discard whatever the event ring is already holding, returning how
 * many were consumed. The restore's half of save step 2 - see the body for why a
 * stale Command Completion Event left on the ring is a wrong retirement rather
 * than a harmless one, and why some of what it finds may not be discarded.
 *
 * **Which ones is not a list here, and audit round 8 is why it stopped being
 * one.** Rounds 6 and 7 each added a case by hand and each got the set wrong;
 * the type test is still written out (a Host Controller Event has no completion
 * code worth consulting), but every code-bearing event is now referred to
 * `XhciXferCodeInfo`, which is where Table 6-90 is transcribed. A code that
 * becomes fatal there becomes fatal here without a second edit.
 *
 * `fatalEvent` receives `XHCI_RESTORE_FATAL_*` saying which kind was found, and
 * the caller must fail the restore on anything but `NONE`. May be NULL only for
 * a caller that does not care, and there is none. The extension carries the kind
 * and the completion code for a release build to read.
 *
 * The controller must be halted. The controller lock is **not** held and is not
 * needed: the sole caller is the resume's own single-threaded window. IRQL:
 * PASSIVE_LEVEL.
 */

/* What `XhciEventDiscardStale` found. Ordered by which reading to believe when
 * the ring holds more than one: the Host Controller Event is the controller's
 * own statement, and outranks a completion code carried by one endpoint's event.
 *
 * `COMPLETION_CODE` was `EVENT_LOST` until audit round 8, and the rename is the
 * finding: the arm had stopped meaning "code 32" once it began asking
 * `XhciXferCodeInfo` rather than testing one constant, and a name that says
 * which code it was is a name that goes stale the next time the table grows.
 * `RestoreFatalCode` says which code it actually was. */
#define XHCI_RESTORE_FATAL_NONE             0
#define XHCI_RESTORE_FATAL_HOST_CONTROLLER  1
#define XHCI_RESTORE_FATAL_COMPLETION_CODE  2

ULONG XhciEventDiscardStale(PXHCI_EXTENSION ext, PULONG fatalEvent);

/*
 * usbport's interrupt-state callback bodies (roadmap Phase 4 task 6).
 * XhciDisableInterrupts records usbport's state and masks only while the
 * controller is initialized; XhciEnableInterrupts is that mask's inverse
 * **plus** a release of Event Handler Busy - because the controller can have
 * raised IP and filled the event ring during the part of StartController that
 * runs it, and an interrupter left busy never asserts again. The asymmetry is
 * the point; the arguments are at the definitions in src/xhci_evt.c.
 * XhciEnableInterrupts acknowledges neither USBSTS.EINT nor IMAN.IP and
 * dequeues no event. IRQL: DISPATCH_LEVEL under MiniportSpinLock.
 *
 * XhciFlushInterrupts touches **no register at all**, and that is a conclusion
 * rather than a stub: its call site (the shipping builds' D0 power completion,
 * disassembled into tools/*-extracted/usbport-flushinterrupts-disasm.txt) holds
 * neither miniport lock, and an acknowledgement it could make is inseparable
 * from an ERDP write it would be making **outside the controller lock every
 * other writer holds** (design doc 05 section 5). That reason used to read "ERDP
 * has exactly one safe writer until the miniport's own event-ring lock exists
 * (task 9)"; audit round 8 found the phrasing outliving its own condition - the
 * lock arrived in task 9 and section 5 now names three writers - while the
 * conclusion it supports is unchanged. IRQL: <= DISPATCH_LEVEL, no lock held.
 */
VOID XhciEnableInterrupts(PXHCI_EXTENSION ext);
VOID XhciDisableInterrupts(PXHCI_EXTENSION ext);
VOID XhciFlushInterrupts(PXHCI_EXTENSION ext);

/* ------------------------------------------------------------------ */
/* The root-hub callback family (src/xhci_rh.c)                        */
/* ------------------------------------------------------------------ */

/*
 * Root-hub power-on-to-power-good, in the 2 ms units usbport copies straight
 * into the hub descriptor's bPowerOnToPowerGood as a UCHAR (SP4 `0x2E442`,
 * confirmed from the binary). xHCI's rule is that software waits 20 ms after
 * asserting PP before touching the port (5.4.8, p.371), so the value is 10.
 */
#define XHCI_RH_POWER_ON_TO_POWER_GOOD 10

/*
 * usbport's root-hub callbacks, one body each, with the packet wrappers in
 * src/xhci_dispatch.c (roadmap Phase 5 task 1).
 *
 * **Two rules run through the whole family**, both from
 * docs/usb-xhci-info/usbport-miniport-abi.md section 4:
 *
 *   A refusal is `MP_STATUS_NOT_SUPPORTED`, never `MP_STATUS_FAILURE`. The
 *   status mapper is seven instructions in both shipping builds and maps 1 -
 *   and only 1 - to `RH_STATUS_NO_CHANGES`, which leaves an endpoint-0 request
 *   queued rather than failing it.
 *
 *   The two status queries answer `MP_STATUS_SUCCESS` even when there is
 *   nothing to report, because the status-change endpoint's scan treats **any**
 *   nonzero return as a hard error, abandons the whole scan and stalls the root
 *   hub's change pipe on every poll from then on. So an unreadable or
 *   unadmitted controller reports zeros and succeeds; the only nonzero these
 *   two ever answer is for a buffer they cannot write to at all.
 *
 * `Port` is a **hub** port number - 1-based within the managed USB 2.0 ports -
 * and every one of these validates it. usbport validates it too, but only on
 * the class-command path: the status-change scan synthesizes 1..N itself, and
 * Win2000's hub-directed feature path passes **0** to
 * `RH_ClearFeaturePortOvercurrentChange`.
 *
 * IRQL: DISPATCH_LEVEL. The status queries run under usbport's
 * `MiniportSpinLock`; the feature callbacks are documented as running under no
 * usbport lock at all and caller-held locking at callback entry was never
 * established either way, so all of them take the driver's own controller lock
 * (docs/contributing/design/05-locking-model.md section 7). None of them waits, and
 * they call usbport services only through XhciRootHubDeferredWork and
 * XhciSlotDeferredWork, after releasing the controller lock.
 */
VOID XhciRhGetRootHubData(PXHCI_EXTENSION ext, PUSBPORT_ROOT_HUB_DATA data);
MPSTATUS XhciRhGetStatus(PXHCI_EXTENSION ext, PUSHORT status);
MPSTATUS XhciRhGetPortStatus(PXHCI_EXTENSION ext,
                             USHORT port,
                             PUSBPORT_PORT_STATUS_AND_CHANGE status);
MPSTATUS XhciRhGetHubStatus(PXHCI_EXTENSION ext,
                            PUSBPORT_HUB_STATUS_AND_CHANGE status);

MPSTATUS XhciRhSetFeaturePortPower(PXHCI_EXTENSION ext, USHORT port);
MPSTATUS XhciRhSetFeaturePortEnable(PXHCI_EXTENSION ext, USHORT port);
MPSTATUS XhciRhSetFeaturePortSuspend(PXHCI_EXTENSION ext, USHORT port);
/*
 * The two asynchronous operations (Phase 5 task 4). Both return as soon as the
 * first register write is issued - a reset takes effect asynchronously and a
 * resume owes 20 ms of bus signalling, and neither of those may be waited for at
 * DISPATCH_LEVEL - so what they leave behind is an armed per-port generation and
 * an uncancellable timer carrying it.
 *
 * They call XhciRootHubDeferredWork themselves, after releasing the controller
 * lock, rather than leaving it to their wrappers: an armed generation that never
 * got a timer onto it is a port permanently ineligible for any other operation,
 * which is easy to forget at a call site and impossible to forget here.
 */
MPSTATUS XhciRhSetFeaturePortReset(PXHCI_EXTENSION ext, USHORT port);
MPSTATUS XhciRhClearFeaturePortPower(PXHCI_EXTENSION ext, USHORT port);
MPSTATUS XhciRhClearFeaturePortEnable(PXHCI_EXTENSION ext, USHORT port);
MPSTATUS XhciRhClearFeaturePortSuspend(PXHCI_EXTENSION ext, USHORT port);

/* The five change-clearing callbacks share one body, because they differ only
 * in which latched bit they take down and a per-callback copy would be five
 * chances to take down the wrong one. `changeBit` is an XHCI_HUB_C_PORT_*. */
MPSTATUS XhciRhClearFeaturePortChange(PXHCI_EXTENSION ext,
                                      USHORT port,
                                      ULONG changeBit);

VOID XhciRhDisableIrq(PXHCI_EXTENSION ext);
VOID XhciRhEnableIrq(PXHCI_EXTENSION ext);
MPSTATUS XhciRhChirpRootPort(PXHCI_EXTENSION ext, USHORT port);

/*
 * Build the root hub from the post-reset port map and seed every shadow from a
 * live PORTSC read. Called by the init sequence at XHCI_INIT_STEP_ROOT_HUB, so
 * once per start and once per resume reinitialization. Returns an XHCI_RH_*
 * status; nonzero refuses the start. IRQL: PASSIVE_LEVEL.
 */
/* `afterRestore` nonzero on the one caller that reaches here with the port
 * registers *surviving* - the restore-success branch of XhciResumeController.
 * It suppresses the U3 -> U0 pass, whose premise is that HCRST has just
 * defaulted every port link state (audit finding A5). */
ULONG XhciRootHubInit(PXHCI_EXTENSION ext, ULONG afterRestore);

/*
 * Fold one Port Status Change Event into the shadow (Phase 5 task 2).
 *
 * **The caller already holds the controller lock** - this is called from inside
 * the event DPC's bounded drain - so it acquires nothing and calls no usbport
 * service. `portId` is the event's raw 1-based *xHCI* port; a port this driver
 * does not manage is counted and ignored. IRQL: DISPATCH_LEVEL.
 *
 * It can *decide* two things its caller must carry out after the lock is
 * released: an announcement, and - on the device-initiated resume path - a timer
 * arm. Both are recorded in the extension and drained by
 * XhciRootHubDeferredWork, which the DPC calls after its release.
 */
VOID XhciRootHubPortEvent(PXHCI_EXTENSION ext, ULONG portId);

/*
 * Everything the root hub decided under the controller lock and could not do
 * while holding it (Phase 5 tasks 4 and 5): arm the timers ports are owed, then
 * announce any latched change with `UsbPortInvalidateRootHub`.
 *
 * **Call only with the controller lock released**, and the rule has teeth here
 * rather than being the usual lock-order caution: `USBPORT_InvalidateRootHub`
 * calls `RH_DisableIrq` straight back into this miniport
 * (docs/usb-xhci-info/usbport-miniport-abi.md section 4), which takes the same non-recursive
 * spin lock - so announcing from inside it is not a lock-order risk but an
 * immediate self-deadlock.
 *
 * Idempotent and cheap when there is nothing owed, which is what lets every
 * caller that *might* have decided something just call it.
 *
 * IRQL: <= DISPATCH_LEVEL.
 */
VOID XhciRootHubDeferredWork(PXHCI_EXTENSION ext);

/*
 * The root-hub half of usbport's `CheckController` poll (Phase 5 task 6): find a
 * port left signalling resume with nothing timing the end of it, arm the
 * terminating write, and then drain the deferred work.
 *
 * It is a **sweep over link states** rather than a reaction to an event because
 * the event may already have been consumed: PLC is acknowledged by whichever
 * refresh sees it first, so a status query racing the Port Status Change Event
 * takes the notification and the event path finds nothing to arm. The link state
 * survives where the change bit does not.
 *
 * Ends by calling XhciRootHubDeferredWork, so the caller owes nothing.
 * IRQL: DISPATCH_LEVEL, under usbport's MiniportSpinLock (CheckController).
 * Takes and releases the controller lock; calls usbport services only
 * through XhciRootHubDeferredWork, after releasing it.
 */
VOID XhciRootHubPoll(PXHCI_EXTENSION ext);

/*
 * Retire every armed port operation and every unannounced change (Phase 5
 * task 6). Called from the quiesce transition, so a stop, a suspend and a
 * failed-start teardown all reach it, and after it every outstanding port timer
 * is stale by generation and returns before touching a register.
 *
 * Called with the controller lock **held**. IRQL: <= DISPATCH_LEVEL.
 */
VOID XhciRootHubRetireOperations(PXHCI_EXTENSION ext);

/* ------------------------------------------------------------------ */
/* Devices, endpoints and transfers (Phase 6 batch B, src/xhci_slot.c) */
/* ------------------------------------------------------------------ */

/*
 * The rule that runs through this whole family: **decide under the controller
 * lock, act after releasing it**, exactly as the root hub does. Everything that
 * has to talk to usbport - `UsbPortCompleteTransfer`, `UsbPortInvalidateEndpoint`
 * - and everything that goes through the command engine (which takes the lock
 * itself) is deferred to `XhciSlotDeferredWork`.
 *
 * Which side of the lock each entry point is on is stated per function, because
 * getting it wrong is a self-deadlock rather than a style problem.
 */

/*
 * Put the device table at its start-of-day state. usbport has already zeroed the
 * extension on a start, so this is what makes a *resume* reinitialisation -
 * which does not get that zeroing - reach the same state.
 *
 * **Called from XhciInitController after HCRST, and the placement is a safety
 * property rather than a convenience.** Two things depend on the reset having
 * happened first: releasing the records is only true once "all of the
 * Operational and Runtime Registers shall be at their default values" (4.23.1,
 * p.312), and the transfers this drains into the completion list are ones
 * `XhciSlotInvalidateAll` deliberately would not answer while their TRBs were
 * still on a live ring. Called from the preflight instead, it would turn every
 * refusal - a path whose design property is that it writes nothing at all - into
 * a silent release, and would hand usbport back buffers the controller could
 * still be writing into.
 *
 * The caller owes XhciSlotDeferredWork afterwards, which is what actually
 * completes them. IRQL: <= DISPATCH_LEVEL (the in-place recovery reaches it
 * from a DPC through XhciInitController); takes the controller lock, so the
 * caller must not hold it.
 */
VOID XhciSlotInit(PXHCI_EXTENSION ext);

/*
 * usbport's endpoint callbacks. All four run at DISPATCH_LEVEL under
 * MiniportSpinLock with the controller lock **not** held; each takes it around
 * the state it touches and calls XhciSlotDeferredWork on the way out.
 *
 * `XhciSlotQueryEndpointRequirements` is pure policy and touches no state.
 */
VOID XhciSlotQueryEndpointRequirements(
    PXHCI_EXTENSION ext,
    const USBPORT_ENDPOINT_PROPERTIES *properties,
    PUSBPORT_ENDPOINT_REQUIREMENTS requirements);

MPSTATUS XhciSlotOpenEndpoint(PXHCI_EXTENSION ext,
                              const USBPORT_ENDPOINT_PROPERTIES *properties,
                              PXHCI_ENDPOINT endpoint);

/* `SetEndpointState(REMOVE)` is the **only** notice either shipping build gives
 * that an endpoint is going away, and it arrives for both the reopen and the
 * delete path (batch 6-0). Any other state is recorded and ignored. */
VOID XhciSlotSetEndpointState(PXHCI_EXTENSION ext,
                              PXHCI_ENDPOINT endpoint,
                              ULONG state);

/*
 * Task 7a-B.3's three, and all three are **live contract**: both shipping builds
 * call every one of them, unlike `CloseEndpoint` and `GetEndpointState` (batch
 * 6-0). `GetEndpointStatus` reports `USBPORT_ENDPOINT_HALT` while a Transfer
 * Event has left the endpoint halted; `SetEndpointStatus(RUN)` is the reset-pipe
 * request and starts the Reset Endpoint -> Set TR Dequeue Pointer -> doorbell
 * sequence of spec 4.6.8 p.116; `SetEndpointDataToggle` is a no-op because that
 * reset clears the toggle itself with TSP = 0.
 *
 * All three run at DISPATCH_LEVEL under `MiniportSpinLock` with the controller
 * lock not held.
 */
ULONG XhciSlotGetEndpointStatus(PXHCI_EXTENSION ext, PXHCI_ENDPOINT endpoint);
VOID XhciSlotSetEndpointStatus(PXHCI_EXTENSION ext,
                               PXHCI_ENDPOINT endpoint,
                               ULONG status);
VOID XhciSlotSetEndpointDataToggle(PXHCI_EXTENSION ext,
                                   PXHCI_ENDPOINT endpoint,
                                   ULONG toggle);

/*
 * SubmitTransfer, including the SET_ADDRESS interception (task 6-B.3). A
 * nonzero return leaves the transfer queued for usbport to retry rather than
 * failing it, which is what a device whose command chain is still running
 * needs - and the retry is *asked for* through UsbPortInvalidateEndpoint when
 * the chain completes, rather than waited for on usbport's 500 ms timer.
 *
 * **`SubmitIsoTransfer` comes through here too**, with `isoParams` non-NULL and
 * `sgList` NULL (task 9-A.1). One entry point rather than two, because
 * everything above the endpoint branch is about the *device* - a record that has
 * gone, a controller that has failed, a suspended controller that will come
 * back - and those gates and their counters are the same question for both
 * callbacks. Duplicating them was the alternative, and a second copy of a gate
 * whose whole content is "which refusals are permanent" is how the two answers
 * drift apart. Exactly one of the last two arguments is non-NULL.
 */
MPSTATUS XhciSlotSubmitTransfer(PXHCI_EXTENSION ext,
                                PXHCI_ENDPOINT endpoint,
                                PUSBPORT_TRANSFER_PARAMETERS parameters,
                                PXHCI_TRANSFER transfer,
                                const USBPORT_SCATTER_GATHER_LIST *sgList,
                                const USBPORT_ISO_TRANSFER *isoParams);

/*
 * Take one transfer back off the queue and report what it moved.
 *
 * **This is the minimum that keeps the completion path honest, not task
 * 7a-B.2's cancellation machine.** It detaches the transfer so nothing can
 * complete it twice, and it deliberately does *not* touch the ring: repositioning
 * an endpoint the xHC may still be executing needs Stop Endpoint and Set TR
 * Dequeue Pointer, which cannot be issued from a callback that may not wait.
 * The TRBs stay outstanding until a later completion retires past them or the
 * slot is torn down.
 */
VOID XhciSlotAbortTransfer(PXHCI_EXTENSION ext,
                           PXHCI_ENDPOINT endpoint,
                           PXHCI_TRANSFER transfer,
                           PULONG completedLength);

/*
 * One Transfer Event, from inside the event DPC's bounded drain.
 *
 * **The caller already holds the controller lock**, like XhciRootHubPortEvent,
 * so this acquires nothing and calls no usbport service: completed transfers are
 * threaded onto the extension's completion list and handed to the caller's
 * XhciSlotDeferredWork. Returns nonzero when the caller must request a
 * controller reset after dropping the lock. IRQL: DISPATCH_LEVEL.
 */
ULONG XhciSlotTransferEvent(PXHCI_EXTENSION ext, const XHCI_TRB *event);

/*
 * Task 9-0.2. The end of a drain pass that found the event ring **empty**:
 * settle every transfer whose TD ended on a mid-TD short packet and whose
 * promised tail never arrived, and recheck the deferral partition.
 *
 * **Callable only when the drain pass observed the event ring empty**, which is
 * not the same as which exit it took: a pass that stopped at
 * `XHCI_DPC_MAX_EVENTS` qualifies if a peek then shows the ring empty, and does
 * not if events remain, because the tail may be one of them. Gating on the exit
 * instead is what stranded a deferral whenever the bound and the controller's
 * last event coincided. Called with the controller lock held.
 */
VOID XhciSlotDrainSettled(PXHCI_EXTENSION ext);

/*
 * The command engine's three notifications, all called with the controller lock
 * held from src/xhci_cmd.c.
 *
 * `XhciSlotCommandEvent` is a completion the engine matched to its own
 * outstanding TRB - so the slot layer needs no address comparison of its own,
 * only the owner it recorded before submitting. `XhciSlotCommandLost` is every
 * other way an outstanding command ends: a timeout that turned into an abort, a
 * stopped ring, or the abandon a quiesce performs. Without it a device would sit
 * in ENABLED for the life of the driver waiting for an event that was already
 * given up on.
 *
 * `XhciSlotCommandSlotFatal` is the third, added by audit round 9: a completion
 * code Table 6-90 calls fatal *to the slot* is a statement about the device
 * rather than about the command, so it is answered after the other two have run
 * and is selected by the event's own Slot ID. IRQL: DISPATCH_LEVEL.
 */
VOID XhciSlotCommandEvent(PXHCI_EXTENSION ext, ULONG completionCode,
                          ULONG control);
VOID XhciSlotCommandLost(PXHCI_EXTENSION ext);
VOID XhciSlotCommandSlotFatal(PXHCI_EXTENSION ext, ULONG completionCode,
                              ULONG control);

/*
 * The port's two announcements, from inside the refresh that observed them, with
 * the controller lock held.
 *
 * A connect *change* is the teardown trigger in both directions: a disconnect
 * obviously, and a re-connect too, because the device that was there is gone
 * whether or not anything else arrived. Batch 6-0 established that this is the
 * only trigger there is - neither shipping build calls `CloseEndpoint`, and an
 * endpoint is deleted with no callback at all - so slot teardown is derived from
 * the port and from nothing else (task 6-B.5).
 *
 * `XhciSlotPortReset` records which port usbhub is about to enumerate, which is
 * the only thing that can associate the address-0 pipe with a root port - unless
 * the port is already mid-enumeration, in which case the reset is that
 * enumeration's own pre-SET_ADDRESS one and entitles nothing (task 7b-A.1.1).
 * **That is why the caller announces this one first when both changes latch
 * together**: the record the reset would read belongs to the device that just
 * left, and reading it would deny the arriving device a claim.
 * IRQL: DISPATCH_LEVEL.
 */
VOID XhciSlotPortConnectChanged(PXHCI_EXTENSION ext, ULONG hubPort);
/*
 * The **second** teardown trigger, and the one the batch 6-V Win98 run added:
 * the caller has taken this port out of service (a disable or a power-off), so
 * whatever is recorded on it is unreachable and must not keep holding the USB
 * address usbport has recycled. Not derived from an observed `PED` = 0 - a port
 * being reset reads that too. IRQL: DISPATCH_LEVEL, controller lock held.
 */
/*
 * The software half of the same event, and the half that must not wait: usbport
 * has destroyed its device object and freed the USB address, which is true the
 * moment the callback arrives whatever the PORTSC write did. Announced
 * unconditionally; `XhciSlotPortDisabled` is the release that follows once the
 * port is observed down. IRQL: DISPATCH_LEVEL, controller lock held.
 */
VOID XhciSlotPortDisowned(PXHCI_EXTENSION ext, ULONG hubPort);
VOID XhciSlotPortDisabled(PXHCI_EXTENSION ext, ULONG hubPort);
VOID XhciSlotPortReset(PXHCI_EXTENSION ext, ULONG hubPort);

/*
 * Drive the command chain and pay usbport what the lock-held paths decided:
 * issue the next owed command, complete retired transfers, and ask for the
 * endpoint retries that are owed.
 *
 * **Call only with the controller lock released** - it submits commands (which
 * take the lock) and calls usbport services. Idempotent and cheap when nothing
 * is owed, which is what lets every caller that might have decided something
 * just call it. IRQL: <= DISPATCH_LEVEL.
 */
VOID XhciSlotDeferredWork(PXHCI_EXTENSION ext);

/*
 * Bracket a `SubmitTransfer` callback. Between them `XhciSlotDeferredWork` does
 * everything it normally does *except* deliver completions to usbport, which
 * would hand back a transfer record usbport writes to again after this callback
 * returns (XHCI_EXTENSION.SubmitDepth carries the call site and the fault).
 * Held completions stay on the list and are delivered by the next event DPC or
 * by XhciSlotPoll.
 *
 * Call with the controller lock released, once each and in order, around the
 * one call usbport makes. IRQL: <= DISPATCH_LEVEL.
 */
VOID XhciSlotEnterSubmit(PXHCI_EXTENSION ext);
VOID XhciSlotLeaveSubmit(PXHCI_EXTENSION ext);

/*
 * The device half of usbport's CheckController poll: age the outstanding command
 * so that one which lost its watchdog is recovered rather than waited on for
 * ever. Ends by calling XhciSlotDeferredWork. IRQL: DISPATCH_LEVEL, controller
 * lock released.
 */
VOID XhciSlotPoll(PXHCI_EXTENSION ext);

/*
 * After a *successful* restore (task 6-B.6's resume path): take back every
 * quiesce chain that was FAILED only because the controller was unavailable
 * (XHCI_EPQ_UNAVAILABLE - Phase 7 review, B3), and re-arm what each record
 * still says it needs. IRQL: <= DISPATCH_LEVEL, controller lock released.
 */
VOID XhciSlotResumeSweep(PXHCI_EXTENSION ext);

/*
 * Give up every device because the controller's state is gone: a stop, or a
 * resume that had to reinitialise instead of restoring (task 6-B.6).
 *
 * **`controllerStopped` is a claim about the hardware and must be evidence, not
 * bookkeeping** - a live `USBSTS.HCH`, or a quiesce that returned success -
 * because it decides two different things:
 *
 *   Records are released, and their Slot IDs and common-buffer blocks made
 *   reusable, **only when the controller can no longer reach them**. A record
 *   released while the xHC still has that slot enabled and its DCBAA entry live
 *   is a claim that hardware state has been released when it has not: the next
 *   start re-carves the same buffer underneath contexts the controller is still
 *   following. When the evidence is missing the records are abandoned in place
 *   instead - counted, DCBAA untouched - and the reset that eventually happens
 *   is what really releases them.
 *
 *   Queued transfers are completed **on the same condition, and not otherwise**.
 *   A completion is not merely an answer: `UsbPortCompleteTransfer` hands the
 *   transfer's mapped buffer back, usbport unmaps its scatter/gather list, and
 *   the pages return to whoever owned them - while the TRBs that name them may
 *   still be on a live ring the controller can execute. Answering them without
 *   the evidence would *create* the DMA-into-reclaimed-memory fault that
 *   `XhciFailClosedDma` exists to report. They are answered instead by whichever
 *   path proves the controller stopped: `XhciSlotInit`, which runs after HCRST,
 *   drains whatever an abandonment left behind. Where nothing ever proves it -
 *   a stop whose quiesce failed - the caller's `XhciFailClosedDma` bugchecks,
 *   and completing anything first would be both unsafe and pointless.
 *
 * No Disable Slot is issued on either path: a controller being stopped or reset
 * has no command ring left to run one on.
 *
 * Deliberately **not** reached from the suspend, and that is the whole point of
 * task 6-B.6: a suspend that tore the devices down would leave a Save State with
 * nothing to save.
 *
 * Nothing here tells usbport the devices are gone, because the reinitialisation
 * that follows does it better: it re-seeds every port shadow from a live PORTSC
 * read, which latches C_PORT_CONNECTION on each connected port and announces it
 * with UsbPortInvalidateRootHub - so usbhub re-enumerates the bus rather than
 * being handed a list of casualties.
 *
 * Called with the controller lock **held**. IRQL: <= DISPATCH_LEVEL.
 */
VOID XhciSlotInvalidateAll(PXHCI_EXTENSION ext, ULONG controllerStopped);

/* ------------------------------------------------------------------ */
/* The asynchronous command engine (src/xhci_cmd.c)                    */
/* ------------------------------------------------------------------ */

/*
 * How long a command may take before this driver assumes it is stuck, and how
 * long the abort itself may take before the ladder escalates. Both are the
 * specification's own figure: "Software should time the completion of all xHCI
 * commands, including the Command Abort operation, i.e. the delay between the
 * negation of CRR ('0') and the assertion of CA ('1'). If software doesn't see
 * CRR negated in a timely manner (e.g. longer than 5 seconds), then it should
 * assume that there are larger problems with the xHC and assert HCRST"
 * (4.6.1.2 implementation note, p.93-95).
 */
#define XHCI_COMMAND_TIMEOUT_MS 5000UL
#define XHCI_COMMAND_ABORT_MS   5000UL

/*
 * How many further abort intervals to wait when CRR has negated but the Command
 * Ring Stopped event has not arrived. Only that event ends an abort - the
 * watchdog deliberately does not recover on a CRR read, because doing so opens a
 * window in which a later stopped event cannot be told from a controller that
 * has stopped a ring this driver believes is running. The wait is bounded
 * because a controller that stops without ever posting the event is as broken as
 * one that will not stop.
 */
#define XHCI_COMMAND_ABORT_WAITS 2UL

/*
 * ------------------------------------------------------------------
 * The poll-clock thresholds, and the rule that sizes all of them
 * ------------------------------------------------------------------
 *
 * **Every threshold below is in milliseconds on XHCI_EXTENSION.PollClockMs,
 * and none of them may be expressed in polls again.** Task 13-R.3.5, and the
 * measurement behind it is docs/contributing/runs/run-13e.md, **Finding V**:
 * this clock read 354,364 ms against 6,461 HealthPolls on the ThinkPad E460, so
 * its CheckController period is **36-80 ms** - 36 ms while idle, ~75 ms while
 * the bus is busy. It is not a constant even on one machine.
 *
 * These counts were all sized against usbport's nominal 500 ms timer and all
 * carried the same safety argument - *"a host that polls more slowly makes this
 * fire later, never sooner"*. That argument names only one of the two
 * directions. **This host polls about an order of magnitude faster**, so
 * XHCI_COMMAND_AGE_POLLS = 64 was **2.3-5.1 s** rather than 32 s - at or under
 * XHCI_COMMAND_TIMEOUT_MS, the 5 s watchdog it was sized to sit 12 s behind.
 * The backstop that "cannot pre-empt a ladder that is working" therefore
 * pre-empted it every time, which is why CommandsTimedOut read 0 across every
 * dump ever taken from that machine, and this driver had been escalating to a
 * controller reset on commands that were merely slow.
 *
 * **Finding U put the period at ~1 ms and that was an inference, not a
 * reading** - 971,359 polls divided by a session nobody timed. Finding V
 * measured it. The defect is unchanged and the repair is unchanged; the
 * magnitude was overstated by one to two orders of magnitude, and the
 * discrepancy against that poll count is recorded there as open.
 *
 * **The rule, and it is a rule rather than a comment that was true once:**
 *
 *   1. A threshold that bounds a wait some *other* mechanism is supposed to end
 *      is sized from that mechanism's own worst case, in the unit that worst
 *      case is stated in - which is always milliseconds here, because every
 *      figure this driver takes from the specification or from usbport is.
 *   2. It is compared against PollClockMs, which is this driver's own clock:
 *      MFINDEX-derived, one tick per frame, one frame per millisecond, sampled
 *      by the health poll (XhciPollClockAdvance, src/xhci_init.c). No import,
 *      and no assumption about anybody else's timer.
 *   3. The poll rate then affects only the *resolution* of the answer, never
 *      its size - which is what makes the error direction genuinely safe in
 *      both directions rather than in the one that was written down.
 *
 * **Raising a poll count instead was considered and rejected.** It
 * re-parameterises a quantity whose *units* are wrong, and the rate is not a
 * constant: the 2a and 2b guests and this same E460 on an earlier boot all show
 * different CheckCallbacks-to-HealthPolls ratios, so a number tuned on one
 * machine is silently wrong on the next one, in whichever direction that
 * machine happens to poll.
 */

/*
 * How long an outstanding command may survive before the health poll concludes
 * that nothing is going to time it.
 *
 * 32 s against a watchdog ladder whose legitimate worst case is
 * XHCI_COMMAND_TIMEOUT_MS + (XHCI_COMMAND_ABORT_WAITS + 1) *
 * XHCI_COMMAND_ABORT_MS = 20 s. The margin is what makes this a detector of a
 * ladder that was never armed rather than a second, competing timeout - rule 1
 * above, and that relationship is checked at compile time in src/xhci_cmd.c
 * rather than left to two numbers agreeing by accident.
 */
#define XHCI_COMMAND_AGE_MS      32000UL

/*
 * The same detector one layer up: how long a *device* command may survive
 * before src/xhci_slot.c concludes nothing is going to finish it.
 *
 * Deliberately **longer** than the engine's own bound, and defined beside it so
 * the relationship is one line rather than two files agreeing by accident. This
 * detector must never fire first: the engine's ladder ends in a controller reset
 * that resolves the command properly, and a device that gave up on a command the
 * engine is still nursing would unwind a slot the hardware still has.
 */
#define XHCI_DEV_AGE_MS          (XHCI_COMMAND_AGE_MS * 2UL)

/*
 * Task 7b-A.0: how long a device record may spend refusing transfers without a
 * break, with nothing placed on a ring and no command in flight, before the
 * record is failed and its transfers answered with an error.
 *
 * **Not** related to the two bounds above, and deliberately far shorter than
 * either, which is only sound because of what this one measures. Those age an
 * *outstanding command* and must never pre-empt the recovery ladder; this one
 * only ever runs while `ActiveOp` is NONE, so there is no ladder running for it
 * to race. What it measures is the absence of any mechanism at all.
 *
 * 5 s is an order of magnitude beyond the gap between two commands of an
 * addressing chain, and short enough that a Windows 98 desktop survives the
 * wait. Batch 7b-V0 is the calibration: an unbounded version of this same wait
 * reached 1,803 refusals and took the guest with it, while Windows 2000 stopped
 * at 17 and stayed usable - **so the budget has to be counted in seconds rather
 * than in refusals**, which is what XHCI_DEV_STALL_POLLS said it was doing and,
 * at 36-80 ms per poll, is not what it did: ten polls was 0.36-0.8 s.
 */
#define XHCI_DEV_STALL_MS        5000UL

/*
 * What the copied async-timer context carries. usbport copies `ctxLen` bytes
 * into its own timer object, so this is a value rather than a pointer into
 * anything the miniport owns - which is the only reason an uncancellable
 * callback is safe to arm at all.
 *
 * Phase says which rung of the recovery ladder armed it: the command's own
 * timeout, or the watchdog on the abort that timeout started.
 */
#define XHCI_CMD_PHASE_COMMAND  1
#define XHCI_CMD_PHASE_ABORT    2
/*
 * Task 13-R.1's third phase, and it is not a rung of the command ladder at all:
 * it is the in-place recovery the `ControllerFailed` latch now arms for itself.
 * It shares this context type rather than adding a second one because it needs
 * exactly the same two things - the epoch that rejects a callback armed by a
 * previous start, and an attempt count that bounds the retries - and because a
 * miniport gets one deferred-work tool and every context it carries has to be a
 * copied value.
 */
#define XHCI_CMD_PHASE_RECOVERY 3

/*
 * How long after the latch the recovery callback is armed for, and how many
 * times it may be attempted.
 *
 * The delay is short because nothing is waiting for it to be long: the request
 * is raised inside `ResetController`, the arming happens at the next 500 ms
 * health poll, and by then the controller has been out of service for however
 * long the ladder above it took. What the delay does buy is that the callback
 * runs from usbport's timer DPC rather than from the poll's own stack, which is
 * the whole point - the poll holds `MiniportSpinLock` and the callback holds no
 * usbport lock at all.
 *
 * Three attempts, because the recovery re-runs the *whole* initialization
 * sequence and each attempt is a complete answer: a controller that will not
 * halt, will not reset, or will not run three times over is one this driver has
 * nothing further to say about. The retries are spaced by the health poll, so
 * they are ~500 ms apart rather than back to back.
 *
 * **It bounds CONSECUTIVE FAILURES, not lifetime attempts**, and that
 * distinction is a correction the E460 made rather than a
 * refinement (run-13e.md, Finding T). Bounding the lifetime total meant three
 * recoveries that all *worked* exhausted the budget, so the fourth incident -
 * an ordinary new fault, retrying nothing - had no recovery available and the
 * port stayed dead. A successful recovery resets the count, which is what the
 * sentence above always claimed and what `RecoveryFailuresConsecutive` now
 * implements.
 */
#define XHCI_RECOVERY_DELAY_MS      50UL
#define XHCI_RECOVERY_MAX_ATTEMPTS  3UL

/*
 * Epoch **and** generation, because they answer different questions and neither
 * covers the other. The generation separates one command from the next within a
 * start; the epoch separates one start from the next, which the generation
 * cannot do because usbport zeroes the whole miniport extension before every
 * StartController (see the note on xhciStartEpoch in src/xhci_cmd.c).
 */
typedef struct _XHCI_COMMAND_TIMEOUT {
    ULONG Epoch;
    ULONG Generation;
    ULONG Phase;
    ULONG Attempt;      /* abort intervals already waited                   */
} XHCI_COMMAND_TIMEOUT, *PXHCI_COMMAND_TIMEOUT;

/*
 * Put one command on the ring and ring DB[0], then return - never wait. The
 * callbacks that need commands run at DISPATCH_LEVEL under usbport's locks and
 * cannot block (docs/usb-xhci-info/usbport-miniport-abi.md section 7), so every command in
 * this driver is asynchronous and the Command Completion Event is serviced from
 * the DPC.
 *
 * **One command outstanding at a time**, which is what makes completion matching
 * a single comparison and makes command-ring-full unreachable on a 64-TRB ring.
 * A second submit while one is in flight answers XHCI_CMD_BUSY rather than
 * queueing, and a submit while an abort is in progress answers it too - ringing
 * the doorbell before the ring has fully stopped is undefined behaviour
 * (Table 5-24 note, p.368).
 *
 * `trbPA` receives the physical address the command went out at, which is the
 * value its Command Completion Event will report: "The Command TRB Pointer field
 * of the Command Completion Event shall point to the Command TRB that initiated
 * the event" (4.6.1, p.93). It may be NULL.
 *
 * Refuses a TRB type outside the architected command range 9-23 (Table 6-91),
 * and refuses with XHCI_CMD_NO_TIMER - **before** enqueuing anything - if there
 * is no async timer service to time the command with. Otherwise arms a bounded
 * timeout through UsbPortRequestAsyncCallback *after* dropping the interior
 * lock. IRQL: <= DISPATCH_LEVEL.
 */
ULONG XhciCommandSubmit(PXHCI_EXTENSION ext,
                        const XHCI_TRB *command,
                        ULONG *trbPA);

/*
 * The Command Completion Event arm of the DPC's event handler. Matches by TRB
 * pointer, retires the command's TD, and runs the two stopped codes' half of the
 * recovery ladder. Returns nonzero when the caller must request a controller
 * reset after dropping the controller lock. IRQL: DISPATCH_LEVEL, under
 * MiniportInterruptsSpinLock and the controller lock.
 */
ULONG XhciCommandEvent(PXHCI_EXTENSION ext, const XHCI_TRB *event);

/* Atomically retire command callbacks, mask both interrupt enables, and clear
 * INITIALIZED before lifecycle code begins a bounded halt. The controller lock
 * is released before the caller waits. IRQL: <= DISPATCH_LEVEL. */
VOID XhciControllerBeginQuiesce(PXHCI_EXTENSION ext);

/*
 * Create the controller-state lock. **DriverEntry only, once per driver load**,
 * and never again: the whole point of putting it in the driver image rather than
 * in the miniport extension is that no restart can re-create it under a callback
 * that cannot be cancelled. IRQL: PASSIVE_LEVEL.
 */
VOID XhciControllerGlobalInit(VOID);

/* The driver-image lock shared by command state, the event-ring DPC,
 * EnableInterrupts and the terminal failure transition. No usbport service may
 * be called while held. IRQL: <= DISPATCH_LEVEL on acquire. */
VOID XhciControllerLockAcquire(PKIRQL oldIrql);
VOID XhciControllerLockRelease(KIRQL oldIrql);

/*
 * Update the shared lifecycle word under the stable controller lock. Returns
 * the value from before the update so a test-and-clear transition (resume's
 * SUSPENDED gate) is one atomic decision rather than an unlocked read followed
 * by a separately locked write.
 *
 * Refuses a NULL or unsignatured extension and answers 0, the same guard
 * XhciControllerBeginQuiesce carries. Every caller today validates first, so
 * the guard is unreachable from src/ - it is here because this is a public
 * helper whose next caller may not, and 0 is the answer that makes a
 * test-and-clear read "the bit was not set" rather than act on a wild word.
 *
 * **Not for a caller already holding the lock** - it acquires, and a KSPIN_LOCK
 * is not recursive. Paths inside the lock update ext->Flags directly.
 * IRQL: <= DISPATCH_LEVEL.
 */
ULONG XhciControllerUpdateFlags(PXHCI_EXTENSION ext,
                                ULONG clearMask,
                                ULONG setMask);

/*
 * Task 11-V.7's log ring, reached the same way `ext->Flags` is: one lock-backed
 * helper for a standalone append, and a `...Locked` spelling for the paths that
 * are already inside the controller lock and would otherwise acquire a
 * non-recursive KSPIN_LOCK twice on one CPU.
 *
 * **Compiled into every flavour, on purpose.** A diagnostic build whose
 * diagnostics were a per-line trace would reproduce the exact fault task 11-V.7
 * exists to route around - per-line `DbgPrint` from DPC and ISR contexts is
 * what bugchecks Win98 on real hardware (task 12.2) - so the always-on producer
 * set must be reachable from a `release` build and must never print. These are
 * it. *(This said "both builds" and named debug-build `DbgPrint` until
 * a later review; there are three flavours since task 13-L.1 and the per-line trace
 * is `qemu`'s alone, so shipping `debug` has none of it to reproduce.)*
 *
 * Both refuse a NULL or unsignatured extension, and both are silent unless the
 * switch in the driver's own software key turned the ring on, so a call site
 * needs no guard of its own.
 *
 * **Never call either from the ISR.** It runs at DIRQL and takes no lock
 * (docs/contributing/design/05-locking-model.md section 4); the ring is ordinary shared
 * extension state and has no DIRQL story. That is a review property - nothing
 * here can enforce it.
 *
 * **`XhciLogNoteAddress` is the third, and which one a call site uses is a
 * decision about PUBLICATION rather than about locking.** A record whose value
 * is a kernel address is a **level-4** record: the ladder's boundary between 3
 * and 4 is addresses, and `XHCISNAP`'s plain-text companion - the file a
 * stranger pastes into a public issue - is built out of the ring. Logging an
 * address through the ordinary form puts it in a level-2 capture and defeats
 * that boundary wherever it is documented, which is what two of these call
 * sites did until the snapshot-value merge. The refusal is counted in `Suppressed` like any
 * other. *(This block still named the pre-merge rungs - addresses at 3,
 * ordinary records at 1 - until the post-Phase 13 review rounds; the snapshot-value merge of
 * `XhciLogSnapshot` into the ladder shifted every one of them by one, and
 * `XhciLogAppendAddress` tests `XHCI_LOG_VERBOSITY_FULL`.)*
 *
 * There is deliberately no `Locked` twin of it. Both of the sites that need it
 * are on the start path and neither holds the lock; adding one before it is
 * needed would be a fourth spelling of the same thing to keep in step.
 *
 * IRQL: <= DISPATCH_LEVEL for XhciLogNote and XhciLogNoteAddress;
 * DISPATCH_LEVEL for the Locked form.
 */
VOID XhciLogNote(PXHCI_EXTENSION ext, const char *label, ULONG value);
VOID XhciLogNoteLocked(PXHCI_EXTENSION ext, const char *label, ULONG value);
VOID XhciLogNoteAddress(PXHCI_EXTENSION ext, const char *label, ULONG value);

/*
 * The health half of usbport's CheckController callback: the two fatal USBSTS
 * bits, and the outstanding command nothing is going to time.
 *
 * **Returns whether an escalation is owed, and never performs one**, because
 * everything it decides is decided under the controller lock and
 * UsbPortInvalidateController is a usbport service - the same
 * decide-under-the-lock, act-outside-it rule the command timeout and the event
 * DPC follow. The caller escalates after this returns.
 *
 * Both halves are gated on the same admission the ISR uses (HcInfoStatus, then
 * INITIALIZED and ControllerFailed), so a quiesced, re-initializing or already
 * failed controller is not polled and cannot be escalated a second time.
 *
 * IRQL: DISPATCH_LEVEL - usbport calls CheckController under MiniportSpinLock.
 * Reads one register and takes no wait.
 */
ULONG XhciControllerHealthPoll(PXHCI_EXTENSION ext);

/* The recovery ladder's deferred usbport service action. Call only with the
 * controller-state lock released. IRQL: <= DISPATCH_LEVEL. */
VOID XhciRequestControllerReset(PXHCI_EXTENSION ext);

/*
 * Is there an async timer service at all?
 *
 * The only answerable half of "can this operation be timed": the service returns
 * 0 on success *and* on its own pool-allocation failure, so a miniport can check
 * that usbport gave it the pointer and nothing else. One spelling for both
 * callers - the command engine and the root hub's two asynchronous port
 * operations - because both must ask the same question *before* the write that
 * needs timing, not after it. IRQL: any.
 */
ULONG XhciAsyncTimerAvailable(VOID);

/*
 * Put the engine at idle for a new start and publish this start's epoch, under
 * the lock XhciControllerGlobalInit created - so the callbacks of the previous
 * start are *excluded* rather than merely able to detect that they are stale.
 *
 * **StartController and nothing else.** It runs before the signatures are
 * published; the stable lock excludes any uncancellable callback while the new
 * epoch and idle command state are installed. Reinitialization is reached only
 * after the lifecycle quiesce transition retired the previous generation.
 *
 * IRQL: PASSIVE_LEVEL.
 */
VOID XhciCommandInit(PXHCI_EXTENSION ext);

/*
 * Issue the No Op Command - "can be issued by software to exercise the TRB Ring
 * mechanism of the xHC without affecting any xHC or USB Device state" (4.6.2,
 * p.94) - as the last step of a start. Returns the submit's XHCI_CMD_* status;
 * the *completion* arrives later, through the interrupt path, which is exactly
 * what makes this the Phase 4 checkpoint's end-to-end proof rather than a
 * register poke. IRQL: <= DISPATCH_LEVEL (the in-place recovery reaches it
 * from a DPC through XhciInitController); nothing here waits.
 */
ULONG XhciCommandNoOpSelfTest(PXHCI_EXTENSION ext);

#endif /* XHCI_HW_H */
