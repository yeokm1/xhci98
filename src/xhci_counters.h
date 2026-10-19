/*
 * xhci_counters.h - the counter block the device matrix reads out of a live
 * guest (roadmap-hcd.md task 26-A.10; design record 13 sections 9.4 to 9.6;
 * scripts/vm-matrix/README.md, "Two expectation sets").
 *
 * The harness reads these words through the QEMU monitor by byte offset, and
 * scripts\vm-matrix\gen-offsets.ps1 measures the offsets by compiling this
 * header under XHCI_HOST_TEST, with no ntddk.h. That is why it is a header of
 * its own and DDK-free: the controller FDO's extension holds kernel objects a
 * host compile cannot lay out, and an offset measured against stand-ins for
 * them would not be the kernel build's. Everything here is a ULONG, so the
 * x86 and amd64 layouts are the same.
 *
 * Embedded in HCD_CONTROLLER (hcd.h) as Counters, zeroed at every controller
 * start, and published by the qemu flavour's identity lines and its
 * XHCI_DBG_VALUE_CHANGED("<label>", cnt-><Field>) sites (hcd_log.c). The
 * labels, not these names, are what matrix-hcd.psd1 is written in; the table
 * that joins them is generated, so a field renamed here renames there.
 *
 * Writers: the controller thread writes the enumeration and select counters
 * plainly (it alone runs those paths); the event DPC writes under the
 * controller lock; a dispatch routine, which holds neither, interlocks.
 * hcd_log.c's poll copies the two the kept controller sequence keeps in
 * XHCI_EXTENSION, which no HCD path increments.
 */

#ifndef XHCI_COUNTERS_H
#define XHCI_COUNTERS_H

#include "xhci_compat.h"

typedef struct _XHCIHC_COUNTERS {
    /* Enumeration: the bus's own Enable Slot and Address Device. The speed
     * pairs count each enumeration's successful Address Device once - the
     * port's decoded speed, and the speed the Slot Context it carried
     * decodes to - and never RESET_PORT's re-address of a kept slot. */
    ULONG SlotsEnabled;
    ULONG DevicesAddressed;
    ULONG PortSpeedHigh;
    ULONG PortSpeedFull;
    ULONG PortSpeedLow;
    ULONG SlotSpeedHigh;
    ULONG SlotSpeedFull;
    ULONG SlotSpeedLow;
    ULONG SpeedDisagreements;

    /* The bind: every non-default endpoint a SELECT_CONFIGURATION or
     * SELECT_INTERFACE names, counted once the URB names its interface, ends
     * opened or refused; the causes are counted beside the refusal. */
    ULONG EndpointsOpened;
    ULONG SelectEndpointsRequested;
    ULONG SelectEndpointsRefused;
    ULONG EndpointRefusalsType;
    ULONG EndpointRefusalsParams;
    ULONG EndpointRefusalsPool;
    ULONG EndpointConfigureFailures;
    ULONG EndpointsNoBandwidth;
    ULONG EndpointsNoResources;
    ULONG UrbsMalformed;
    /* A SELECT_CONFIGURATION or SELECT_INTERFACE answered with a failure,
     * once per request, so a select that fails after its
     * endpoints were opened on the controller - SET_CONFIGURATION or
     * SET_INTERFACE refused by the device - or on a malformed record is
     * still a refusal. BUFFER_TOO_SMALL is not one: it is the length probe
     * a client resizes from. Nor is a failure on a device proven to have
     * left its port (hcd_cfg.c, hcdCfgCountSelect): a select racing an
     * unplug is not a refusal. */
    ULONG SelectsFailed;

    /* The controller. */
    ULONG FatalStatus;
    ULONG TransferEventsUnclaimed;
    ULONG InterruptMaskFailures;
    ULONG CommandsGivenUp;

    /* Transfers on the URB path: a TD published is later completed by an
     * event or taken off the ring cancelled. The isochronous three are the
     * transfer engine's per-queue counts, summed as events move them. */
    ULONG TransfersSubmitted;
    ULONG TransfersCompleted;
    ULONG TransfersCancelled;
    ULONG IsoPacketsAnswered;
    ULONG IsoMissedService;
    ULONG IsoPacketErrors;

    /* Hubs: Phase 27's. Published at zero until the bus serves a hub, so
     * the matrix's hub rows read a field rather than an absent label. */
    ULONG HubsStarted;
    ULONG TopoDescriptors;
    ULONG TopoDescriptorsBad;
    ULONG TopoHubSlotsMarked;
    ULONG TopoNodesDropped;
    ULONG TopoBehindHubAddressed;
    ULONG TopoBehindHubOpens;
    ULONG TopoBehindHubTooDeep;
    ULONG TopoTtProgrammed;

    /* SuperSpeed: Phase 29's. Appended, so every earlier offset stands.
     * The speed pair counts as the High/Full/Low ones do (the port's
     * decoded speed and the Slot Context's) for the SuperSpeed class;
     * SuperSpeedPlus counts the addressed devices whose trained rate is
     * above Gen 1x1 (29-A.1, "counted by rate"). Then the link (29-A.2):
     * warm resets written (driver policy and recovery), hot resets the xHC
     * itself carried out warm (WRC seen after a PR), links given up after
     * the warm-reset budget - the device left to its USB 2.0 companion,
     * 29-A.5's passive fallback - and SuperSpeed-capable devices (a BOS with
     * a SuperSpeed USB Device Capability) enumerated on a USB 2.0 companion
     * port, which is that fallback seen from the other side. A BOS read for
     * it on the USB 2.0 path was withdrawn (Phase 29): it counts instead the
     * held device itself enumerating on its companion, matched by the
     * identity 29-A.5's hold read on the SuperSpeed port. Then the BOS reads
     * that failed, and the SuperSpeedPlus isochronous endpoints refused
     * because their payload does not fit the Endpoint Context (29-A.6). */
    ULONG PortSpeedSuper;
    ULONG SlotSpeedSuper;
    ULONG PortSpeedSuperPlus;
    ULONG SsWarmResets;
    ULONG SsResetsConverted;
    ULONG SsLinksGivenUp;
    ULONG SsDevicesOnUsb2;
    ULONG SsBosMissing;
    ULONG SsEndpointsEsitRefused;

    /* 29-A.5's hold, appended: holds begun by kind - identified and
     * companion-paired (releasable), unidentified (no serial, no descriptor,
     * a failed identity read: until the next start), orphan - then holds
     * released, companion visits by some other device, send-back requests
     * refused, and holds dropped by a controller reset that took the port
     * out of SS.Disabled. */
    ULONG HoldsPaired;
    ULONG HoldsUnidentified;
    ULONG HoldsOrphan;
    ULONG HoldsReleased;
    ULONG HoldCompanionOthers;
    ULONG HoldRequestsRefused;
    ULONG HoldsDropped;

    /* SuperSpeed hubs: Phase 30's (30-A.1), appended after the hold. The
     * SuperSpeed halves configured; hub pairs that look like the two halves
     * of one unit (a counter and nothing more); SET_HUB_DEPTH refused (the
     * hub not served); warm resets of SuperSpeed hub ports, and links given
     * up after the per-port budget - the device left to the hub's USB 2.0
     * half; C_PORT_CONFIG_ERROR seen; and devices behind a SuperSpeedPlus
     * hub given a SuperSpeedPlus Protocol Speed ID from the extended port
     * status, or given SuperSpeed's because none matched the rate. */
    ULONG SsHubsStarted;
    ULONG SsHubPairs;
    ULONG SsHubDepthRefused;
    ULONG SsHubWarmResets;
    ULONG SsHubLinksGivenUp;
    ULONG SsHubConfigErrors;
    ULONG SsHubDevicesPlus;
    ULONG SsHubRateUnmatched;

    /* Event delivery (design record 15 section 8; roadmap-hcd.md 36.6 to
     * 36.8), appended. The three values as read (XHCI_POLL_NOT_FOUND when
     * not found) and in effect; the delivery and its reason
     * (XHCI_POLL_DELIVERY_*, XHCI_POLL_R_*), set at start and again at the
     * dead-line move; the pin's read status and value; the poller's role,
     * the period it last armed and the bus state that chose it
     * (XHCI_POLL_BUS_*); its runs and the runs that drained an event; the
     * opportunistic peeks and their hits; promotions, parks and kicks; arms
     * replaced as displaced; the shortest and longest interval between two
     * runs (XHCI_POLL_NOT_FOUND until one) and the intervals discarded; the
     * watcher's looks, asserted looks and stale pairs; the move (0 or 1);
     * the attempts to retire the line and the retirement's state
     * (XHCI_POLL_RETIRED_*); and a start that cleared a leftover PCI
     * Interrupt Disable. Written at start before the poller runs, then
     * under the controller lock or PollLock (the poller's own words). */
    ULONG IntrModeRead;
    ULONG IntrMode;
    ULONG IntrDelivery;
    ULONG IntrReason;
    ULONG IntrPinStatus;
    ULONG IntrPin;
    ULONG PollIdleMsRead;
    ULONG PollIdleMs;
    ULONG PollActiveMsRead;
    ULONG PollActiveMs;
    ULONG PollRole;
    ULONG PollPeriodLast;
    ULONG PollStateLast;
    ULONG PollRuns;
    ULONG PollRunsWithEvents;
    ULONG PollPeeks;
    ULONG PollPeekHits;
    ULONG PollPromotions;
    ULONG PollParks;
    ULONG PollKicks;
    ULONG PollArmsDisplaced;
    ULONG PollIntervalMinMs;
    ULONG PollIntervalMaxMs;
    ULONG PollIntervalsDiscarded;
    ULONG WatchLooks;
    ULONG WatchAsserted;
    ULONG WatchStalePairs;
    ULONG LineMoves;
    ULONG LineRetireAttempts;
    ULONG LineRetireState;
    ULONG IntxDisableCleared;
    /* The move's mask (record 15 section 8): 1 when both enables read back
     * down at the move's retirement attempt, whatever its IP step did. */
    ULONG LineMoveMasked;
    /* The three values' read status apart from their values (record 15
     * section 3): XHCI_POLL_FOUND_* bits. */
    ULONG IntrValuesFound;

    /* Message-signalled interrupts on Vista and 7 (design records 15
     * section 4.3 and 18; roadmap-hcd.md 36.15), appended: where the
     * IoConnectInterruptEx pair came from (HCD_MSI_ROUTINES_*); the
     * messages connected and the capability read (XHCI_POLL_DELIVERY_*);
     * the connection's NTSTATUS; the raw list's message descriptors; the
     * x86 helper's binding (XHCI_HLPR_STATE_*), its looks, the last
     * look's NTSTATUS and refusal (XHCI_HLPR_REFUSE_*; until task 38.3
     * XHCI_MSI86_REFUSE_*, whose 4, a bad routine pair, is now the MSI
     * service's verdict, HlprMsiVerdict at word 197) and the helper's
     * version; the last requirements filter's sequence, the list's class
     * (XHCI_MSI_CLASS_*), the outcome (XHCI_MSI_OUT_*), the mode it used
     * as read (XHCI_POLL_NOT_FOUND when not found), its configurations,
     * message and line members, and the descriptors a strip removed.
     * Written at each start, after the counters are zeroed, from the
     * controller's own record, and at each later filter. */
    ULONG MsiRoutines;
    ULONG MsiMessages;
    ULONG MsiCapability;
    ULONG MsiConnectStatus;
    ULONG MsiDescriptors;
    ULONG Msi86State;
    ULONG Msi86Looks;
    ULONG Msi86Status;
    ULONG Msi86Refusal;
    ULONG Msi86HelperVersion;
    ULONG MsiFilterSeq;
    ULONG MsiFilterClass;
    ULONG MsiFilterOutcome;
    ULONG MsiFilterMode;
    ULONG MsiFilterLists;
    ULONG MsiFilterMessages;
    ULONG MsiFilterLines;
    ULONG MsiFilterStripped;
    /* Selective suspend, stage A (design record 14; roadmap-hcd.md 36.33),
     * appended contiguous at word 114, after the MSI words, so stage B's (record 14 section 8.10)
     * follow them: the switch as read (0 absent, 1 a DWORD, 2 another
     * type) and in effect; the sequences begun by kind and how they ended,
     * the last refusal's reason (XHCI_SUSP_WHY_*); the remote-wake feature
     * set and cleared; wake events, those that claimed nothing and the
     * WAIT_WAKEs they completed; samples of another device generation;
     * D-IRPs pended, stale-epoch Dx completed and slow IRPs deferred; the
     * idle notification's batches, callbacks called and returned, entries
     * skipped, client cancels, callbacks abandoned and PDOs quarantined;
     * and log lines past the start's budget (hcd_susp.c). Thread plainly,
     * dispatch interlocked or under the controller lock, as above. */
    ULONG SuspSwitchRead;
    ULONG SuspSwitch;
    ULONG SuspSuspendsBegun;
    ULONG SuspResumesBegun;
    ULONG SuspWakesBegun;
    ULONG SuspSuspended;
    ULONG SuspActive;
    ULONG SuspRefused;
    ULONG SuspRefusedReason;
    ULONG SuspUncertain;
    ULONG SuspGone;
    ULONG SuspArms;
    ULONG SuspDisarms;
    ULONG SuspWakeEvents;
    ULONG SuspWakeUnclaimed;
    ULONG SuspWaitWakesCompleted;
    ULONG SuspStaleSamples;
    ULONG SuspDxPended;
    ULONG SuspDxStale;
    ULONG SuspSlowDeferred;
    ULONG IdleBatches;
    ULONG IdleCalls;
    ULONG IdleReturns;
    ULONG IdleSkips;
    ULONG IdleCancels;
    ULONG IdleAbandoned;
    ULONG IdleQuarantined;
    ULONG SuspNotesSuppressed;
    /* Selective suspend, stage B (design record 14 section 8.10; roadmap-hcd
     * 36.38), appended contiguous at word 142: FUNCTION_SUSPEND with options
     * 03h the device took and arm steps whose request was not taken; options
     * 0 taken and disarms not taken (the device cycled); SuperSpeed hub
     * ports observed in U3 and verified back in U0; GetStatus(interface)
     * answered, failed (a STALL, failure or short answer) and the capable
     * bits they found; configurations with a function past interface 31;
     * FUNCTION_WAKE notifications received for a device, dropped at receipt
     * with the window closed, taken as a suspension's wake, taken by a hub
     * port's observation or a refusal's end, emptied by a closing window or
     * taken for a device not suspended, and naming no PDO's first interface
     * or one past 31; Device Notifications dropped with the switch on (another
     * type, or a slot with no device); and WAIT_WAKEs a configuration left
     * without a wake function. The DPC's under the controller lock, the
     * thread's plainly or under it. */
    ULONG SsSuspArms;
    ULONG SsSuspArmRefused;
    ULONG SsSuspDisarms;
    ULONG SsSuspDisarmFailed;
    ULONG SsSuspHubU3;
    ULONG SsSuspHubResumes;
    ULONG SsSuspCapReads;
    ULONG SsSuspCapFailed;
    ULONG SsSuspCapable;
    ULONG SsSuspFarInterfaces;
    ULONG FuncWakeEvents;
    ULONG FuncWakeClosed;
    ULONG FuncWakeAsEvent;
    ULONG FuncWakeInSequence;
    ULONG FuncWakeIgnored;
    ULONG FuncWakeUnattributed;
    ULONG DevNotifyDropped;
    ULONG SsSuspCapSettled;
    /* Composite devices as devnodes (task 36.43; design record 13 section
     * 10.12), appended at word 160: composite-parent PDOs created, and
     * function PDOs re-presented in place at a composite FDO's start. The
     * thread's plain writes. */
    ULONG CompositeParents;
    ULONG CompositeRepresented;
    /* USB 2.0 speed IDs behind a hub (issue 18; implementation-invariants
     * "Port Speed Decoding", the downstream exception), appended at word
     * 162: XhciUsb2HubSpeedFallback as read (0 absent, 1 a DWORD, 2 another
     * type) and the policy in effect (1 on); every behind-hub placement
     * refused for want of a speed ID, USB 2.0 or SuperSpeed hub alike; each
     * placement that took the default ID; and the Slot IDs that hold a
     * device placed so, bit (Slot ID - 1) - not kept here (this word stays
     * 0) but computed by the door from the live records as it copies the
     * block (hcd_door.c). The start's writes and the thread's plain ones. */
    ULONG Usb2HubSpeedSwitchRead;
    ULONG Usb2HubSpeedFallbackOn;
    ULONG TopoBehindHubNoSpeed;
    ULONG Usb2HubSpeedFallbacks;
    ULONG Usb2HubSpeedFallbackSlots;
    /* Stream endpoints (task 36.54; design record 17 section 4.12),
     * appended at word 167: the soft retry's Reset Endpoints for a stream's
     * TD; stream rings a port reset or a recycle restarted at their base;
     * whether QEMU's stream-context quirk is armed on this controller (1 on
     * PCI 1B36:000D alone, set at each start); the Set TR Dequeue commands
     * that quirk completed; its resyncs that could not finish, each failing the
     * Stop or Reset Endpoint it followed; and the extra Stop Endpoints QEMU's
     * own device wakeup cost by restarting a stopped endpoint. The thread's
     * plain writes. */
    ULONG StreamRetryResets;
    ULONG StreamRingRestarts;
    ULONG QemuStreamQuirk;
    ULONG QemuStreamResyncs;
    ULONG QemuStreamResyncFailed;
    ULONG QemuStreamRestops;
    /* Controllers below xHCI 1.0 (task 38.11; design record 17 section
     * 4.14.7), appended at word 173, each 0xFFFFFFFF from HcdCountersStart
     * until written ("not read"): the versions (XhciVersionPairWord - bits
     * 15:0 the register's HCIVERSION, 31:16 the effective one, written from
     * the step 0c read whether or not its check refused); XhciAccept096 as
     * read (the DWORD, or 0xFFFFFFFF absent or of another type); and the
     * policy (XhciVersionPolicyWord - the floor, the TD Size form, the qemu
     * override, and bit 21 once the versions word was written, which is what
     * tells a register that read all ones from one never read). The start's
     * writes. */
    ULONG HciVersions;
    ULONG HciAccept096Value;
    ULONG HciVersionPolicy;
    /* A finer system timer while polling (task 38.3; design record 15
     * section 7.2.9), appended contiguous at word 176: XhciPollFineTimer as
     * read (XHCI_POLL_NOT_FOUND when not found) and in effect; where the
     * routine came from (XHCI_POLL_TMR_SRC_*) and the timer thread's state
     * (XHCI_POLL_TMR_THREAD_*); whether a request is held, the holds, the
     * releases, those a boundary forced, the holds a boundary raced, and
     * the poll runs whose interval crossed a call; the value asked (100 ns)
     * and what the last TRUE and the last FALSE returned; the last
     * release's cause (XHCI_POLL_TMR_CAUSE_*); the time held in this start
     * in whole ms, saturating; the phase observations, RELEASED then HELD,
     * each its count, sum in whole ms (saturating) and late intervals; and
     * the x86 helper's two service verdicts (XHCI_HLPR_SVC_*; record 18
     * section 15.3). The thread's writes under the controller lock, the
     * DPC's under it and PollLock, the start's before the thread. */
    ULONG TmrSwitchRead;
    ULONG TmrSwitch;
    ULONG TmrSource;
    ULONG TmrThread;
    ULONG TmrHeld;
    ULONG TmrHolds;
    ULONG TmrReleases;
    ULONG TmrReleasesForced;
    ULONG TmrHoldsRaced;
    ULONG TmrObsCrossed;
    ULONG TmrDesired;
    ULONG TmrActualHeld;
    ULONG TmrActualReleased;
    ULONG TmrReleaseCause;
    ULONG TmrHeldMs;
    ULONG TmrObsReleasedCount;
    ULONG TmrObsReleasedSumMs;
    ULONG TmrObsReleasedLate;
    ULONG TmrObsHeldCount;
    ULONG TmrObsHeldSumMs;
    ULONG TmrObsHeldLate;
    ULONG HlprMsiVerdict;
    ULONG HlprTimerVerdict;
    /* Linux's xHCI controller quirks (task 38.10; design record 20 section
     * 6.4), appended contiguous at word 199: one packed word per value in
     * xhci_quirk.h's order (XHCI_QK_*: the mode, its normalization, the
     * match, the effective policy, an override, the last outcome and its
     * reason, the value read - xhci_quirk.h, "One packed word"), written at
     * each start and at each action; then the PCI revision (configuration
     * byte 08h, logged, never matched) and the vendor and device dword the
     * lists were matched against; then the counts, copied from the
     * extension's quirk state by the thread's poll (hcd_log.c): messages
     * XhciMsiAvoid refused, suspend halts past 32 ms, the halt delays made,
     * the suspends that skipped their save, the stuck links warm-reset, the
     * padded stream blocks, the No Ops placed, the isochronous Successes
     * dropped, the interrupt endpoints XhciTiIntervalCap capped, the
     * ASMedia writes made and given up, the AMD bit's clears and skips, the
     * shutdown's EHCI writes and HCRSTs, and its log flushes acknowledged
     * drained, acknowledged with the sink off, and not acknowledged. */
    ULONG QuirkMsiAvoid;
    ULONG QuirkStreamsOff;
    ULONG QuirkResumeReinit;
    ULONG QuirkShutdownToEhci;
    ULONG QuirkShutdownReset;
    ULONG QuirkHaltDelay;
    ULONG QuirkHaltTimeout;
    ULONG QuirkAsmediaFlow;
    ULONG QuirkTiIntervalCap;
    ULONG QuirkStuckLinkReset;
    ULONG QuirkStreamRingPad;
    ULONG QuirkControlLinkNoOp;
    ULONG QuirkIsochSuccess;
    ULONG QuirkAmdSuspendBit;
    ULONG QuirkPciRevision;
    ULONG QuirkIdentity;
    ULONG QuirkMsiRefused;
    ULONG QuirkHaltsLong;
    ULONG QuirkHaltDelays;
    ULONG QuirkSavesSkipped;
    ULONG QuirkStuckLinkResets;
    ULONG QuirkPaddedBlocks;
    ULONG QuirkLinkNoOps;
    ULONG QuirkIsochDropped;
    ULONG QuirkTiCapped;
    ULONG QuirkAsmWrites;
    ULONG QuirkAsmGiveUps;
    ULONG QuirkAmdBitClears;
    ULONG QuirkAmdBitSkips;
    ULONG QuirkShutEhciWrites;
    ULONG QuirkShutResets;
    ULONG QuirkShutFlushes;
    ULONG QuirkShutFlushSinkOff;
    ULONG QuirkShutFlushUnacked;
    /* Endpoint rings held until the controller drops them (task 38.10;
     * design record 20 section 5), appended at word 233: the pool rings
     * held now, copied at each poll from the pool's own array - flat across
     * any number of interface switches that end where they began; pipes
     * held closed for an endpoint the controller may still have enabled
     * (stale), and held after a command whose completion was never seen
     * (pinned, until the slot goes); stale ones a later proof released;
     * selects that put the old pipes back after a refused or unissued
     * command; SELECT_INTERFACEs a one-setting STALL ended in success
     * through the clears; the rollbacks issued after a STALL, and those
     * whose clears completed so the old pipes were put back. The thread's
     * plain writes, the first under the controller lock at the poll. */
    ULONG PoolRingsInUse;
    ULONG RingsRetained;
    ULONG RingsPinned;
    ULONG RingsReleased;
    ULONG SelectsReattached;
    ULONG SelectStallSuccesses;
    ULONG SelectRollbacks;
    ULONG SelectRollbacksReopened;
    /* The composite devnode's own SELECT_CONFIGURATION (task 38.12; design
     * record 13 section 10.14.6), appended at word 241: selects the parent
     * PDO recorded; selects it refused, and composite FDO starts whose own
     * sequence failed before the select reached it; endpoints of recorded
     * selects whose matched bInterval differs from the device's, and those
     * that did not match at all; function endpoints built from a record's
     * byte that differs from the device's. The parent's dispatch counts the
     * first four interlocked; the thread the last, plainly. */
    ULONG CompositeSelects;
    ULONG CompositeSelectRefused;
    ULONG CompositeRewrites;
    ULONG CompositeMismatches;
    ULONG CompositeIntervals;
} XHCIHC_COUNTERS, *PXHCIHC_COUNTERS;

XHCI_C_ASSERT(xhcihc_counters_all_ulong,
              sizeof(XHCIHC_COUNTERS) == 246 * sizeof(ULONG));

#endif /* XHCI_COUNTERS_H */
