# Design Record 38 - A Controller Stopped After Repeated Failures, Tried Again

Revised 2026-11-19 after the code: the waits in seconds (section 5.7) and
the USB Tree's lines and Settings note (sections 15.1 and 15.2), and
leg (b2)'s trigger (section 19), the owner's decisions given through the
coordinator; reviewed and revised.

Revised 2026-11-19 at the merge: leg (i)'s over-current case worded as
section 12.1 has it and the legs read it - a place held unpowered stays
unpowered across a try until a start; reviewed and revised.

Revised 2026-11-19 after the legs: the scales of legs (b) and (h), the
systems leg (f) may be read on (section 19), and what `ctrl.retry.reset`
carries (section 5.4), corrected to what the legs and the code show;
reviewed and revised.

Status: draft 4, 2026-11-18, reviewed and revised, design before code:
the sampling's sweep made of short controller-lock holds, each
revalidating its admission, a sweep interrupted discarded whole (sections
6.1, 6.3, 14 and 18, leg (i)); legs (b) and (s) and section 10 aligned
with the burst's first-success boundary. Draft 3, 2026-11-18, reviewed
and revised: the leave flag also raised by the refusal of a START to a started
controller and cleared by a query-stop refused locally under mode 4, its
writers one pure table (section 7.5); one burst boundary - the try's
attempts up to its first success - with its counters, its note and the
scope of the leave deferral, true at every `XhciTolerance` value and with
the sliding window (sections 3, 5.5 and 7.4); a try's success taking a
latched resume's phase from FAILED back to RUNNING (section 9); the
injection lock inside the controller lock, as the layer orders them, and
the sampling's locked unit (sections 6.1 and 14); the reset answers
routed to the sampler, the direct transition's evidence deferred and the
release state exported (sections 6.1, 15.3, 17 and 19); suspend and resume
breaking a clean run, and leg (f) reading the computed deadline (sections
5.4 and 19); and the precision corrections (seven capability reads, one
saturation rule, the reconnection against the episode's interval, the
stop's wait). Draft 2, 2026-11-18, reviewed and revised:
valid nonzero values held to the recorded ceiling, 2 to 600 (section 4);
a latch whose last attempt left the capability decode refused is
revalidated against the lifetime's own decode before a try (section 7.3);
a release whose unproven invalidation was then proved is admitted (section
7.2); the try committed inside the recovery's own gated admission, behind
a leave flag every PnP path publishes under the controller lock (sections
7.4 and 7.5); the sampling's admission, its reset suppression and the
invalidation of its evidence (section 6); the hold release kept as it is,
a stable disconnect then a connection (section 6.6); the cost of one try
and the accelerated tries' real bound (sections 5.5 and 6.4); the latched
resume only with tries enabled, and its bookkeeping (section 9); the clock
and the clean run (sections 5.4 and 5.6); the execution contexts and the
stop's wait stated as they are (section 14); both directions of the
snapshot's compatibility (section 15.3); injection codes clear of every
allocation (section 17); legs that can be run as written (section 19); and
three questions for the owner (section 22). Draft 1, 2026-11-18, before
code.

Written for `roadmap-usb4.md` task 50.31 ("a controller stopped after
repeated failures is tried again"), release `3.6.0.0`, from the owner's
decisions of 2026-11-17 recorded on the task's line and in design record
17 section 4.15.0, item 4. Nothing of it is coded. It takes design record
17 section 4.15.13 (revision 23, the first sketch of the task, written
beside task 50.30) as its starting point and settles every item the task's
line leaves "to be settled in its design", and the items a design of this
kind owes besides. **Where this record and section 4.15.13 differ, this
record rules**; section 21 lists each difference.

The USB Tree's half - what the tool shows of a stopped controller, and
task 50.32's restart - is design record 25 section 26; this record gives
it its data (section 15).

Tags: **[code]** read in this tree at `b768be4bc` (branch `3.5.1.0`);
**[s3]** read on branch `w50-devfault-s3` at `814090401` (task 50.30 step
3, the device isolation, with record 17 revisions 24 to 26; not yet
merged); **[spec]** xHCI 1.2c, cited by section as
`docs/usb-xhci-info/xhci-data-structures.md` or a source comment quotes
it; **[inf]** an inference, stated as one; **[unread]** not read on any
vehicle, and what reads it is named.

## 1. What is asked, and what is not

The task's line (owner, 2026-11-17): "in this phase; by a registry value
in seconds, a few seconds by default (`XhciFailedRetrySec`, `5`, ceiling
600 s, `1` invalid); a port change brings a try only on fresh, debounced
evidence within a storm limit; backing off, and at once on a port change.
Today it stays stopped until a stop and start. A registry value in the
form of the others gives the first wait; each later wait doubles to a
ceiling, and a change at a root port (the failing device unplugged) brings
a try at once, no oftener than the value; `0` is today's behaviour. Never
where the controller answers all ones or its memory may still be written;
none while suspended." Record 17 section 4.15.0 item 4 adds: "a normal
ceiling of 600 s, a value of `1` read as invalid (invalid values use the
default), on by default".

To be settled, from the line: the value's name, range and default, the
ceiling, what ends the backing off, 98 SE and ME after a controller
reset, a machine started from a USB drive, and what the USB Tree shows
meanwhile.

**What is not asked, and is not done here.**

- No try of a contained controller (record 17 section 4.6: terminals 4
  and 5), of one whose message interrupt was lost (terminal 6), or of one
  whose common buffer is pinned. They are excluded for good (section 7).
- No new thread, timer, DPC or work item (section 14).
- No change to what task 50.30 decides about a device that stops
  answering, nor to its hold release; the two meet only where section 12
  says.
- No sight of a hub's port while the controller is stopped: a hub needs
  transfers. Only the timed tries reach a fault behind a hub.
- No register write while stopped: the port-change trigger only reads.
  Powering ports while stopped would be a new behaviour, outside this
  task.
- No new balloon or notice; the tray's existing red icon is unchanged
  (record 25 section 26 item 2).

## 2. What was read

All [code] at `b768be4bc` unless tagged.

**The terminals** (`xhci_tol.h`, `XHCI_TOL_TERMINAL_*`, computed by
`XhciTolTerminalEx`, the containments outranking the lost message, which
outranks the window, which outranks the run of failures): 0 running; 1 a
recovery owed; 2 the run of recoveries failed in a row spent
(`RecoveryFailuresConsecutive` at `XHCI_RECOVERY_MAX_ATTEMPTS`, 3); 3 the
recovery window refused a fourth recovery inside ten minutes
(`Tol.Window.Refused`); 4 contained as unreadable; 5 contained with DMA
not proven stopped (`Tol.Unreadable` 2, the buffer pinned); 6
`XhciInterruptMode` 4's message lost (`Msg4Lost`). "Stopped after repeated
failures" is terminals 2 and 3, and only those (record 25 section 26 item
2 draws the same line).

**The recovery** (`hcdRecover`, `hcd_ctl.c`): the request
(`RecoveryRequested` with `ControllerFailed`) is taken under the power
gate and the controller lock, admitted only in D0 outside a transition,
not contained, not barred or lost under mode 4, the window not refused
and the run not spent; it is deferred uncharged while USBSTS reads all
ones (`XhciTolRecoverDefer`), paced after a failure (`XhciTolRecoverPaced`,
task 49.35), charged to the window (`XhciTolWindowAdmit`, which at
`XhciTolerance` 0 always admits), and run: `XhciRecoverController` at
DISPATCH_LEVEL, with up to three continuations at PASSIVE_LEVEL for a
reset still settling (`hcdRecoverSettle`, its bound 1000 ms) or a Bus
Master Enable to put back (`hcdRestoreBusMaster`), the power gate held
throughout. A failed attempt is charged once and re-requested while the
run is not spent. **That admission reads no PnP state, no `ThreadStop`
and no `ShutLatched`** ([s3] nor the leave signal `CmdWaitLeave`): a
request published before a stop begins is taken by the next pass unless
power or containment refuses it.

**The capability guard** (`xhciRecoverOnce`, `xhci_init.c`): a recovery
refuses before anything else, counted as a consecutive failure
(`ctrl.recover.nohw`), whenever `HcInfoStatus` is not `XHCI_HC_OK`.
`XhciInitController` sets `HcInfoStatus` to `XHCI_HC_BAD_PARAM` at its top
and only a whole validated decode sets it back
(`XhciDeriveControllerInfo`; after the reset, `XhciHcInfoEqual` against
the decode already held, `XHCI_HC_CHANGED` when they differ). So an
attempt that refused before the re-decode - a window that stopped
decoding for a while - leaves every later attempt refusing at the guard:
a terminal 2 reached that way can never recover by a recovery alone. The
lifetime's validated decode is still in `ext->HcInfo` (only a whole
validated decode replaces it; `hcdContainAdmitted`'s comment).

**The terminal release** (`hcdTerminalRelease`): once per lifetime
(`hc->TerminalReleased`), on a started controller in D0, terminal 2 or 3
with a device record left, it raises the invalidation no recovery will;
proven when HCH reads set, unproven otherwise. `HcdEnumService` takes the
invalidation (`SlotsInvalidated` cleared at once); an unproven one first
takes the Bus Master Enable proof (`HcdCtlProveDmaStopped`) and drains
(`hcdInvalidate`) on success, or pins and contains (terminal 5) on
failure. **`SlotsUnproven` is not cleared by a successful proof**: only an
HCRST (`XhciSlotInit`) or a start clears it. `TerminalReleased` says the
invalidation was raised, not that it was drained; and
`XhciTolTerminalRelease` raises nothing when no device record is left, so
such a latch leaves `TerminalReleased` at 0.

**The resume of a latched controller** (`hcdResumeContained`,
`hcd_power.c`, asked in `hcdD0Finish` and `hcdDirectTransitionGated`):
refuses the reinitializing resume for terminals 2 to 6, before
`XhciResumeController` clears `XHCI_EXT_FLAG_SUSPENDED`; `hcdRecover`
refuses while that flag is set. Today a latched controller that went
through a sleep stays refused for the lifetime.

**PnP** (`hcd_pnp.c`): the controller's PnP state is a plain store,
under no lock, written after each IRP's own work: `STOP_PENDING` or
`REMOVE_PENDING` at a query (under mode 4 only after `HcdMsg4QueryStop`
has released the message), `STOPPED` after `HcdStopController`,
`SURPRISE_REMOVED` after `HcdMsg4Gone`, `Msg4Removal` and
`HcdStopController`; a cancel restores the state kept before the query.
`HcdStopController` stops the thread (`hcdThreadStop`: `ThreadStop`, a
wake, the join).

**The tolerance clock** (`hcdTolDpc`, `hcd_svc.c`; record 17 section 4.0):
a 32-bit count of the runs of a 100 ms relative timer's DPC, credited at
45 ms a tick (`XHCI_TOL_TICKS(T)` = ceil(T / 45) + 1 ticks for T ms), so
an interval counted on it runs long, never short. The timer is armed at
the start and cancelled only at the stop's drain (`HcdTimersDrain`): it
runs while the controller is in Dx with the system running. It is not
wall time and nothing subtracts a sleep.

**The thread's pass** (`hcdPoll`): the health poll, the counters, mode 4's
thread half, `hcdRecover`, `hcdTerminalRelease`, the poller's pass,
`hcdContain`, then `HcdEnumService` under the power gate; the thread waits
on `WorkEvent` for at most 100 ms (`HCD_POLL_MS`). While the controller is
failed the port pass does not run (`hcdHalted`).

**The location observer** (`XhciTolLocObserveChange`, `xhci_tol.c`, and
[s3] with hold 4): a stable disconnect (CCS clear with PP set and no fault
for `XHCI_TOL_STABLE_DISC_TICKS`) is recorded (`DiscSeen`), and **a
following connection** re-arms the budget and releases hold 1 and, [s3],
hold 4; PP clear or a fault clears the evidence. [s3] Record 17 section
4.15.16 A observes a root port's sample before hold 4 suppresses it, the
release rule unchanged.

**The recovery-status request** (`xhci_qry.h`, `hcdDoorRecovery`): its
64-byte header reserves `XHR_O_RETRY_VALUE`, `XHR_O_RETRY_TRY` and
`XHR_O_RETRY_NEXT_S` and the flag `XHR_F_RETRY`, which today's driver
leaves clear with `NEXT_S` at `XHR_NONE`. The header is full.

**The snapshot.** The reasons' state (`XHCI_TOL_WHY`) is appended to the
extension, its place and size in the HCD header's words 952 and 953;
`tr_snap.c` reads it only when the header is long enough, and the size,
the offset's alignment and its bounds within the image check out ("not a
shape this build reads" otherwise). [s3] Step 3 appends inside it and
after it. `XHCI_TOL_STATS` is not grown, so no offset the harness reads
moves.

**The values' rules.** Every value a release driver reads is written by
both INFs on every controller path under 34.1's don't-overwrite rule
(`0x00010003`), listed in the INF gate's table with its self-test rows and
footprints, in `build-and-test.md`'s table of the values the driver reads,
in `tuning.md`, and in the installer's `instValues[]` and the Tree's
`trsRows[]`. A value read only by the `qemu` flavour and compiled into no
release build is excepted and documented in `build-and-test.md` (owner,
2026-11-18).

**Debts** [s3]. A left command's debt is settled only on a controller that
is not failed; every HCRST, a recovery's included, clears it with
`CmdEnableHeld` and `InputHeldSlot` and drops the resets it deferred
(`HcdEnumLeftHcrstLocked`), and since record 17 section 4.15.16 B also
clears every deferred-enumeration obligation, the HCRST's re-enumeration
looking at every port afresh.

## 3. The states, and the machine

One machine per controller lifetime (a start to a stop):

| State | Means | Leaves by |
|---|---|---|
| OFF | `XhciFailedRetrySec` read as `0` for this lifetime | a stop and start only |
| WATCHING | terminal 0 or 1 | the thread sees terminal 2 or 3: LATCHED; an exclusion: EXCLUDED |
| LATCHED | terminal 2 or 3; a deadline set; the root ports sampled where admitted (section 6) | the deadline passes, or an admitted port episode: DUE |
| DUE | a try wanted; the thread arms it (`RetryArmed`) when its look passes | the commit (section 7.4): TRYING; a skip: LATCHED; an exclusion: EXCLUDED |
| TRYING | the try committed; its burst runs: the try's attempts up to its first success (terminal 1 between them) | the first success: WATCHING; terminal 2 before any success: LATCHED; contained or lost: EXCLUDED; the lifetime ending: nothing |
| EXCLUDED | terminal 4, 5 or 6, or `CommonBufferPinned` | a stop and start only |

1. **Start.** `XhciTolRetryInit` (pure core): the value read (section 4),
   the level and the tries 0, no deadline, no storm history, no evidence.
   OFF if the value is 0, else WATCHING.
2. **WATCHING to LATCHED.** The first pass that computes terminal 2 or 3
   stamps the latch (`LatchStamp`), sets the deadline to `LatchStamp +
   W(level)` (section 5) and clears every port's evidence (section 6.2):
   baselines are taken by the first admitted sample, not at the latch.
   Noted once (`ctrl.retry.latched`: terminal, level, wait in ticks).
3. **LATCHED to DUE.** The deadline has passed, or an admitted port
   episode passes section 6.4's limits. When both hold in the same pass,
   the deadline wins: the cause is 1 (the wait) and the episode is
   consumed without counting a port-change try.
4. **DUE to TRYING.** Section 7.4's commit, inside `hcdRecover`.
5. **TRYING to WATCHING.** An attempt of the burst succeeded: the burst
   ends there, its note is written (section 5.5), the clean run's timing
   begins (section 5.4) and a latched resume's phase is ended (section
   9). Every recovery after that point is an ordinary one.
6. **TRYING to LATCHED.** The burst's attempts reached terminal 2 before
   any success: transition 2 again, at the level the try raised.
7. **WATCHING to LATCHED after a try.** Ordinary recoveries after a
   successful try that latch again - terminal 3 when the window, with
   `XhciTolerance` on, refuses a recovery; terminal 2 when three fail in a
   row, at any value - are transition 2, at the level the try raised, or
   at 0 if a clean run has ended the backing off since.
8. **Any state to EXCLUDED.** Terminal 4, 5 or 6, or `CommonBufferPinned`:
   no try for the rest of the lifetime, noted once
   (`ctrl.retry.excluded`).
9. **Stop.** Nothing of the machine survives a stop (section 13); a burst
   the stop ends is noted as ended by the leave (section 5.5).

## 4. The value

- **Name:** `XhciFailedRetrySec` (owner, 2026-11-17).
- **Type and place:** a `REG_DWORD` in the controller's driver key, and,
  winning over it, in `Services\USB` (the 3.1.0.0 rule), read at each
  start with the other per-controller values in `hcd_ctl.c` and latched
  for the lifetime.
- **Reading.** `0`: off, today's behaviour, every path of this record
  inert (the latched resume included, section 9). `2` to `600`: the first
  wait in seconds. Anything else - `1`, any value above `600`, a value of
  another type, absent - is `5`, the default (owner: "ceiling 600 s",
  "`1` invalid", "invalid values use the default"). Nothing reports a
  refusal; the start notes the reading (`ctrl.retry.value`: a number in
  bit 31, a value of another type in bit 30, the number read in 29:16
  held to 16383, the value in effect in 15:0); the snapshot block's
  `Found` keeps which of absent, a number or another type it was.
- **Why `1` is invalid** (owner): a try sooner than the recovery's own
  pacing gap (`XHCI_TOL_RECOVER_GAP_MS`, about 1.2 s by the clock) gains
  nothing.
- **Independent of `XhciTolerance`** (record 17 section 4.15.13 item 1,
  kept as a design rule): at `XhciTolerance` 0 the window never refuses,
  so only terminal 2 is reached and tried.
- **Written by the install:** `5`, on every controller path of both INFs
  (section 16).

## 5. The timing

### 5.1 The waits

Section 5.7 changes the tick credit of this section's intervals and adds
a floor on the system time; the doubling and the cap are as written here.
In ticks of the tolerance clock, every product in ULONG, no 64-bit
arithmetic:

- the base `B = XHCI_TOL_TICKS(V * 1000)` for the value V (2 to 600); V *
  1000 is at most 600 000, B at most 13 335;
- the ceiling `C = XHCI_TOL_RETRY_CEILING_TICKS = XHCI_TOL_TICKS(600000)`,
  13 335 ticks, the recovery window's own length; at V = 600, B = C and
  nothing doubles;
- the wait before the try at level L is `W(L) = min(B * 2^L, C)`, computed
  by doubling with the cap checked first (`w > C - w` takes C); the level
  is held at the first level whose wait is C (9 for V = 2), so neither
  overflows;
- the deadline is the latch's stamp plus `W(level)`: each wait runs from
  the latch the thread saw, so the time a failing try spent recovering is
  not counted toward the next.

### 5.2 What that is

As first written, before section 5.7 (now `B` is 51 ticks and each
wait the written seconds or a little more): at the default (V = 5, B =
113 ticks), at the timer's nominal 100 ms
period - an estimate, not a maximum: a tick is longer whenever the timer
or the system is late:

| Try | Level | Ticks | Nominally |
|---|---|---|---|
| 1st | 0 | 113 | 11 s |
| 2nd | 1 | 226 | 23 s |
| 3rd | 2 | 452 | 45 s |
| 4th | 3 | 904 | 1 min 30 s |
| 5th | 4 | 1 808 | 3 min |
| 6th | 5 | 3 616 | 6 min |
| 7th | 6 | 7 232 | 12 min |
| 8th on | 7 | 13 335 | 22 min |

V = 2 is 46 ticks (nominally 4.6 s); V = 600 is 13 335 (nominally 22
min). The clock's rule, never short, is record 17's for every tolerance
interval; `tuning.md` already words the ten-minute window "about 22
minutes by the clock", and this value is worded the same way.

### 5.3 When the level rises

At every try, timed or brought by a port change, and at every skip
(section 7.4): once, held at its cap. A port change does not lower it.

### 5.4 What ends the backing off: a clean run

The level and the try count return to 0 only at a controller start (a
Windows restart, a Device Manager disable and enable, task 50.32's
restart, a driver update) or after a **clean run**: ten minutes on the
clock (`XHCI_TOL_RETRY_CLEAN_TICKS`, 6 001 ticks since section 5.7) of
healthy operation after a try
recovered. A thread pass is **healthy** when, read under the controller
lock: terminal 0 (`ControllerFailed` clear, so no recovery owed; the
request word is read only with it, as the recovery's admission reads it,
since a resume that reinitializes a failed controller serves an owed
recovery and leaves the word set),
`XHCI_EXT_FLAG_INITIALIZED` set and `XHCI_EXT_FLAG_SUSPENDED` clear, D0
(section 7.2) and not `SuspendedInD0`, the lifetime running (section 7.5's
leave flag clear, `ShutLatched` clear), and the count of reset requests
(`ResetControllerCalls`) unchanged since the previous pass. The clean run
begins at the first healthy pass after the try's recovery; **any pass
that is not healthy restarts it** at the next healthy pass. **A suspend
and a resume break it too, whether or not a thread pass saw the interval
between them**: the suspend's controller-lock hold (`hcdSuspendSettled`,
where `PowerDown` is set) and every resume path's hold clear the clean
run's start (`CleanArmed` 0), so the ten minutes begin again at the first
healthy pass after D0. So time failed, suspended, out of D0, awaiting a
recovery, or after a reset request does not count, and no run spans a
sleep; a controller that keeps needing recoveries has not run clean. The
clean run's words are written under the controller lock (the thread and
the power path both write them). At its end the level and the tries
return to 0
(`ctrl.retry.reset`, carrying the count of clean runs ended since the
start, `BackoffEnds`, the snapshot's "waits reset after a clean run").
Nothing else ends the backing off:
not a port change, a resume or a relatch. The storm history (section 6.4)
is ended only by a start.

### 5.5 The cost of one try, and the tries being unlimited

**The burst, one boundary.** A try's **burst** is its own attempts, from
the commit up to the first of: an attempt that succeeds (WATCHING); the
run of failures spent before any success (terminal 2, LATCHED); a
containment or a lost message (EXCLUDED); the lifetime's end. The try
commits the first attempt with the run of failures at 0, and every later
attempt of the burst is the run's re-request after a failure, so **a
burst is at most three attempts**, each with up to three continuations,
at every `XhciTolerance` value: the run's cap,
`XHCI_RECOVERY_MAX_ATTEMPTS`, applies at both values, and the window,
fresh at the commit, cannot refuse within three attempts. A burst
re-enumerates the controller at most once, at its success (the devices
were already gone when it began).

**After the burst, recovery is ordinary and unchanged.** Once the burst
has succeeded, every later fault is an ordinary recovery, admitted and
bounded as today: with `XhciTolerance` on, by the sliding window (three
recoveries begun inside ten minutes, its oldest stamp ageing out, a
fourth inside refused: terminal 3) and the run of failures; at
`XhciTolerance` 0, by the run of failures alone, the window admitting
every recovery. So what one try leads to after its success is what the
controller's faults lead to in service; this record adds nothing to it
and takes nothing from it, and claims no bound on it beyond the existing
ones. Those recoveries are not the burst's: they are not counted in it,
not deferred by a leave (section 7.4) and not part of its note. Each
re-enumeration, the burst's or an ordinary one, drops every device on the
controller (a keyboard or mouse for a few seconds; a drive's open files
as on an unplug).

**Its counters and its note.** `BurstAttempts` counts the burst's
attempts (reset at the commit), `TriesRecovered` the bursts that ended in
a success; the burst is noted once, at its end (`ctrl.retry.burst`: the
attempts in 15:0, the outcome in 31:16 - 1 recovered, 2 latched again, 3
excluded, 4 ended by the lifetime's end), beside the recovery's existing
notes per attempt. Nothing is noted per pass: a due try waiting for
admission, an exclusion's persistence and a refused port try are counted
in the snapshot block (section 15.3), each noted at most once per latched
episode.

There is no count after which tries stop (record 17 section 4.15.13 item
5, kept as a design rule): a permanently failing controller gets an
endless sequence of tries at a bounded rate - once the waits reach the
ceiling, one timed try per ceiling interval plus at most three
accelerated ones in any ten minutes on the clock (section 6.4), each a
burst of at most three attempts - and the design does not promise that it
ever stops. A cap would leave the
controller stopped for good after a failing device behind a hub is
unplugged, where no port change is seen.

### 5.6 Sleep and the clock

The clock counts the runs of its timer's DPC. It does not measure wall
time and nothing subtracts a sleep: while the system sleeps no DPC runs
[inf], and a relative timer due during the sleep fires once on the wake,
so a sleep adds about one tick. Hours in standby or hibernation therefore
do not by themselves bring a deadline closer than the time awake does.
The clock keeps counting while this controller is in Dx with the system
running (the timer is cancelled only at the stop); a deadline can pass
then, and the try comes at the first admitted pass after D0.

### 5.7 The waits in seconds (owner, 2026-11-19)

The owner's decision, given through the coordinator on 2026-11-19:
`XhciFailedRetrySec` means real seconds. The value, its doubling and its
ten-minute cap are seconds of real time as nearly as the driver's clock
allows. The recovery window of `XhciTolerance` is not part of the
decision and keeps record 17's credit. Where sections 5.1 to 5.4, 6.4,
15.1 and 17 say otherwise, this section rules.

**The two clocks.** The tolerance clock (record 17 section 4.0) counts
its timer's DPC runs. Each tick is late by the DPC's queueing and lock
time, a late tick is lost and never made up, and on Windows 98 a relative
timer can expire up to one clock period early, which is why record 17
credits a tick at 45 ms. Measured in this phase: 9.69 to 10.01 ticks per
host second on 98 SE, 9.04 to 9.15 on XP (its 15.6 ms clock interrupt);
on 98 SE under emulation the guest's own millisecond clock, which the
timer follows, has been seen at about a third of host time from a start.
The second source is the system time (`KeQuerySystemTime`, already
imported on every system, with a Windows 98 precedent in its own HCD:
the import gate's row), Windows' own clock. It counts while the computer
sleeps and moves when the time is set; the tick count does neither.

**The rule.** A retry interval of `T` ms is met when both hold:

- the tolerance clock has counted `XHCI_TOL_RETRY_TICKS(T)` =
  `ceil(T / 100) + 1` ticks since the stamp: each tick at the timer's
  nominal 100 ms, the extra one for a stamp taken just before a tick;
- for the waits only (the latch's deadline, and the "not before `B`" of
  a port-change try), the system time has moved on by at least
  `XHCI_TOL_RETRY_UNITS(T)` = `ceil(T * 625 / 65536) + 1` units since the
  latch's stamp `LatchSys`. A unit is 2^20 x 100 ns (104.8576 ms), so the
  stamp is taken from the time's two halves by shifts alone,
  `(HighPart << 12) | (LowPart >> 20)`, with no 64-bit arithmetic, and
  wraps after about 14 years; `T * 625` is at most 375 000 000. The
  comparison is unsigned: the floor is met when the units since the stamp,
  taken modulo 2^32, reach its threshold, and only then (the cases of a
  clock set during a wait follow below).

The doubling and the cap of section 5.1 are taken in milliseconds, not in
ticks: the wait at level L is `Wms(L) = min(V * 1000 * 2^L, 600 000)`
(doubled with the cap checked first, as in section 5.1), then converted:
`W(L) = XHCI_TOL_RETRY_TICKS(Wms(L))` ticks and a floor of
`XhciTolRetryFloor(W(L)) = XHCI_TOL_RETRY_UNITS((W(L) - 1) * 100)` units,
which is at least `XHCI_TOL_RETRY_UNITS(Wms(L))`. So level 8 at V = 2 is 512 seconds,
not the doubled ticks' 537.5. The default's first wait `B` is 51 ticks
and 49 units, the ceiling `C` 6 001 ticks and 5 724 units. The level is
held at the first level whose `Wms` is the cap.

**What the user can rely on.** While Windows' clock runs on undisturbed,
a wait is never shorter than its seconds by that clock (the floor), to
within one unit (about a tenth of a second, the stamp being taken inside
one). It is longer by the ticks the system loses: on the systems
measured, from nothing to about a tenth, more on a heavily loaded system;
where Windows' own clock runs slow the wait follows that clock. Time
asleep does not count (the tick count stops), as section 5.6 says. Where
the clock is set during a wait (by the user, by a time service, or, if
Windows 98 keeps local time there, by a change of summer time): set
forward, the floor is met that much sooner, at once where the step is at
least the floor's remainder, and the ticks decide what is left; set back
by less than the time since the latch, the floor is met later by the time
set back (a longer wait); set back by more, the unsigned difference reads
as a very long interval, the floor is met, and the ticks alone decide. Where the ticks alone
decide, a wait can be short by as much as the timer's early expiries,
which record 17 section 4.0 bounds at one clock period each; the measured
rates above show none. The user is promised the written seconds or a
little more, never a hard minimum across a change of the clock.

**Each interval, changed or not:**

| Interval | Before | Now | Why |
|---|---|---|---|
| the first wait `B`, each later wait `W(L)`, the cap `C` | 45 ms a tick | 100 ms a tick and the floor | the decision |
| the clean run's ten minutes (section 5.4) | 13 335 ticks | 6 001 ticks, no floor | the decision's ten minutes; a clean run only ends the backing off, so a run a tick short is harmless |
| the storm limit's ten minutes (section 6.4) | 13 335 ticks | 6 001 ticks, no floor | written to the user as ten minutes; a bound on tries, not a wait |
| a port change's stable interval (record 17's 1 000 ms, 24 ticks) | unchanged | unchanged | shared with the hold observer (section 6.6); at least the written second, about 2.4 s by the clock |
| the continuity gap (500 ms) | unchanged | unchanged | a starved thread's mark, internal |
| the recovery window of `XhciTolerance` | unchanged | unchanged | not part of the decision |

**The qemu scale** (section 17) divides the ticks of each interval, at
least one tick, and the floor of a wait follows its scaled ticks
(`XhciTolRetryFloor`, at least one unit).

**The words.** `XHR_O_RETRY_NEXT_S` in LATCHED is the larger of the ticks
left at 100 ms and the units left at 104.8576 ms, rounded up to whole
seconds. The snapshot reader has no system time and gives the ticks'
figure. `LatchSys` takes the structure's last word (the `Reserved` that
kept the count even): the size stays 52 ULONGs and no reader's word
moves.

**The legs.** The legs of section 19 that read a wait's length run again
on the build that carries this: leg (a) reads `NEXT_S` from about 5 and
the try about 5 s after the latch. Each timed reading names the guest's
clock against the host's; a 98 SE guest started with its clock slow is
void for timing.

## 6. The root-port-change trigger

### 6.1 The sampling's admission

In LATCHED and DUE, with the value not 0, the thread samples once per
pass, after `hcdContain`, under the power gate for the whole sweep. **The
controller lock is never held across the sweep.** The sweep is a series
of short controller-lock holds, the lock released (the thread back at
PASSIVE_LEVEL) between each:

1. **The admission hold.** The conditions below are checked, the leave
   flag's generation (`RetryLeaveGen`, advanced by every write of section
   7.5's table) is captured, and USBCMD and USBSTS are read. Bound: the
   checks over memory and two register reads, plus, only where the decode
   stands refused, the seven capability reads of section 7.3 - at most
   nine register reads.
2. **One hold per root port.** The same conditions checked again, the
   leave generation compared with the captured one, and that port's PORTSC
   read into the thread's own array. Bound: the checks over memory and one
   register read.

**A sweep interrupted is discarded whole.** Where a port's hold finds any
condition failing or the leave generation changed, or a PORTSC reads all
ones, the array is thrown away - no baseline, no episode and no observer
feed comes from any port of that sweep - and the pass counts as a
**missed pass** (6.3). Only a sweep whose every hold passed is judged;
the evidence is judged and the observer fed (6.6) after the last hold,
from the array, by the thread alone. A leave raised and cleared between
two holds also discards the sweep, by the generation. The power gate,
held for the sweep, excludes every power transition between the holds.
The conditions, checked in every hold before its read - baselines
included:

- the lifetime running: `ControllerStarted`, `XHCI_EXT_FLAG_STARTED`, the
  PnP state `HCD_PNP_STARTED`, the leave flag clear (section 7.5),
  `ThreadStop` clear, [s3] `CmdWaitLeave` clear, `ShutLatched` clear;
- power: D0 as section 7.2 defines it, `SuspendedInD0` clear,
  `XHCI_EXT_FLAG_SUSPENDED` clear (the power gate held excludes a
  transition);
- not contained, not pinned, terminal not 6 (else EXCLUDED);
- the release settled (section 7.2's memory rows): no invalidation
  pending, none unproven and unproved;
- the window mapped (`hc->BarVa` not NULL, `ResourceBase` not 0) and the
  layout valid for this pass: `HcInfoStatus` is `XHCI_HC_OK`, or section
  7.3's capability reads (seven) equal the lifetime's decode in this
  pass's admission hold (nothing written by the sampling; the port holds
  rely on that hold's result, since nothing writes `HcInfoStatus` while
  the sweep holds the power gate);
- **USBCMD and USBSTS read first**, in the admission hold: USBSTS all ones ends the pass's
  sampling and stops the sampling for the latched episode
  (`ctrl.retry.sample.off`, noted once); HCRST or CNR set
  (`XhciTolResetRunning`) - a reset that outlived the recovery's call and
  its gate - ends the pass's sampling. In the `qemu` flavour both reads go
  through the reset answer (`HcdInjResetAnswer`), as
  `hcdRecoverSettle`'s reads do, so code `1B`'s held HCRST or CNR reaches
  the sampler; the release flavour reads the registers.

A pass where any of these fails in the admission hold is a **missed
pass** (6.3). Otherwise every root port the bus manages (`hc->Ports[]`
with no hub) is read once, each in its own hold: PORTSC, by the ordinary
read, no write. A PORTSC reading all ones discards the sweep and stops
the sampling for the latched episode as USBSTS does.

**PP clear gives no evidence.** A port with PP clear "is nonfunctional and
shall not report attaches, detaches, or Port Link State (PLS) changes",
whatever HCCPARAMS1's PPC says (xHCI 1.2c Table 5-27, p.375, as
`xhci_init.c`'s power step quotes it), so its CCS is not evidence and its
continuity is broken (6.3). That covers a root place held unpowered (hold
reasons 2 and 3) and any port a failed recovery left unpowered.

A connector routed away by the Intel port switchover (record 16) shows no
connection on its xHCI port and brings nothing. Whether a given
controller updates CCS while halted is not established [unread]; QEMU's
is read by leg (c). On an accessible controller in D0, halted or not, the
reads are register semantics: they make the controller write nothing,
clearing Bus Master Enable does not stop the processor's reads, and
reading PORTSC clears no change bit (the change bits are RW1C, cleared
only by a write of 1; Table 5-27 as transcribed). That establishes the
reads, not that broken hardware completes them or reports CCS truthfully:
the sampling is an opportunity, not a guarantee.

### 6.2 Baselines and episodes

- **The baseline.** A port's baseline is its CCS at its first sample with
  evidence continuity: the first admitted sample after the latch, after a
  missed pass, after PP returned, after a resume, or after a reset was
  seen running. No evidence comes from the sample that takes a baseline.
  A CSC set at that moment is consumed logically: never counted, never
  written. CSC, PRC, PEC and the other change bits are never evidence.
- **An episode** is a CCS different from the baseline that reads the same
  on every sample for `XHCI_TOL_STABLE_DISC_TICKS` (at least 1 s, about
  2.4 s nominally), the interval that re-arms a location's budget. A
  sample equal to the baseline restarts the interval. A disconnect and a
  connect are both episodes.
- **Consumed once.** A completed episode's CCS becomes the baseline; every
  episode completed at or before a try is consumed by it, so the two
  logical ports of one connector, or two ports changed together, bring one
  try (`ctrl.retry.port`, noted per consumed episode: port in 15:8, CCS in
  bit 0).

### 6.3 What breaks continuity

Evidence never matures across an interval it did not observe. **For every
port** at a missed pass (6.1) - an admission hold that failed, or a sweep
discarded because a port's hold found admission changed or a PORTSC all
ones - at a gap since the last admitted sample of
more than `XHCI_TOL_RETRY_GAP_TICKS` (`XHCI_TOL_TICKS(500)`, 13 ticks: a
starved thread), at a resume, at a reset seen running, at the sampling's
stop, and at the latch itself; **for one port** when its PP reads clear or
its PORTSC all ones: its baseline is dropped (retaken at the next sample
with continuity), its interval cleared, and its location observer's
evidence cleared as a PP-clear sample clears it (`DiscArmed` and
`DiscSeen` 0, section 6.6). A stable disconnect already matured is
dropped too: a replug after the gap is then evidence only once seen
stable again. Conservative by intent: a hold then stays until the next
unplug and replug.

### 6.4 At once, no oftener than the value, and the storm limit

A completed episode makes the controller DUE at once (cause 2), within
three rules:

1. **No oftener than the value.** Not before `B` ticks after the latch's
   stamp (the base wait, not the current one). Earlier, the episode is
   kept and the controller becomes DUE at `LatchStamp + B`, still cause 2.
2. **It is the step's try.** A port-change try is the current level's try
   taken early: it raises the level and clears the deadline as a timed try
   does. This alone does **not** keep accelerated tries from coming about
   every base interval: each relatch anchors a new latch stamp, the next
   step is a new step, and a fresh episode can accelerate it `B` after that
   relatch, whatever the level, the saturated level included.
3. **The storm limit, which is the effective bound:** no more than three
   port-change tries in any ten minutes on the clock
   (`XHCI_TOL_RETRY_STORM_TICKS`, 6 001 ticks since section 5.7). The
   last three port-change tries'
   stamps are kept; an episode that would make a fourth inside ten minutes
   of the oldest is consumed without a try (`StormRefused` counted, noted
   once per latched episode, `ctrl.retry.storm`), and the timed deadline
   stands.

**The storm history outlives a latch**: the three stamps belong to the
lifetime, not reset at a relatch, a try that recovers, a clean run or the
fresh window a try starts; only a start clears them. So accelerated tries
are at most three per ten minutes for the lifetime, and every other try is
a timed one at the backed-off wait.

### 6.5 The cost

One USBCMD, one USBSTS and one PORTSC read per root port per pass (100 ms)
while latched and admitted, plus, where section 7.3's decode stands
refused, the seven capability reads of `XhciDeriveControllerInfo` (six
fixed, and HCCPARAMS2 where the mapping covers it); the thread keeps
waking.

### 6.6 The hold release, unchanged

The rule stays section 4.5's and record 17 section 4.15.16 A's: a
stable disconnect **then a connection** releases a powered hold (reasons
1 and, [s3], 4); an unplug alone releases nothing. While the controller
is failed the port pass does not run (`hcdHalted`), so the sampling is the
observer's only feeder there: each admitted sample's CCS, with PP set and
no fault, is fed to `HcdTolLocObserve` with the change flag set where the
sampled CCS differs from the last sample's (never CSC on every pass: the
observer restarts its interval while the flag is set), and every break of
6.3 feeds it as a PP-clear sample, which clears its evidence. So a held
place whose device was unplugged, left unplugged for the stable interval
and plugged in again, all while the controller was stopped and sampled
throughout, is released **at the first sample that reads the
reconnection**: the observer's rule, once its stable disconnect has
matured, needs no interval for the connection. The retry's episode for
that same connection is judged separately and does need its own stable
interval (6.2): the try comes only once the connection has read the same
for `XHCI_TOL_STABLE_DISC_TICKS`, within section 6.4's limits, and its
re-enumeration then finds the place free. A device unplugged while
stopped and plugged in only after the try is released by the in-service
observation, as today.

## 7. The admission, and the try

### 7.1 The exclusions

| Condition | Read as | Why |
|---|---|---|
| value 0 | the latched reading | today's behaviour |
| not stopped after repeated failures | terminal not 2 or 3 | the task is terminals 2 and 3 |
| the controller answers all ones | USBSTS `0xFFFFFFFF` at the commit | a window that stopped decoding |
| contained | `Tol.Unreadable` nonzero (terminals 4, 5) | owner: never |
| pinned | `CommonBufferPinned`, whatever the terminal | the containment's buffers are kept |
| memory possibly still written | section 7.2's release rows | the controller may still reference what the driver would reuse |
| the message interrupt lost | `Msg4Lost` (terminal 6) | the interrupt route is what failed |
| barred under mode 4 | `Msg4Barred` | only section 9's re-arm lifts it |
| not in D0, or in a transition | section 7.2's power rows | owner: none while suspended |
| shutting down, or a D0 the bus refused | `ShutLatched` | the shutdown latch (record 13 section 5.8) |
| leaving | section 7.5's leave flag, `ControllerStarted`, the PnP state, `ThreadStop`, [s3] `CmdWaitLeave` | PnP is taking the device away or stopping it |

Not exclusions: a standing debt of task 50.30 (the try's HCRST is what
settles it, section 12.2); `BusMasterCleared` (the recovery's continuation
puts it back after its HCRST settles); `XhciTolerance` 0.

A condition that ends only at a start (contained, pinned, lost) is
EXCLUDED. The others leave the controller DUE, its try taken at the first
pass that commits it, once, with no catch-up; each pass a due try waits is
counted (`WaitPower` or `WaitLeave`), not noted.

### 7.2 The admission's rows, exactly

- **D0** is `hc->Common.DevicePower == PowerDeviceD0`, which the D0 path
  publishes only after the bus below completed the D0 IRP successfully
  (`hcdD0Finish`; a refused D0 publishes nothing and latches
  `ShutLatched`), with `SuspendedInD0` and `XHCI_EXT_FLAG_SUSPENDED`
  clear. The power gate, held for the commit, excludes every transition.
- **The release settled**, which replaces draft 1's rule:
  - no invalidation pending: `SlotsInvalidated` 0 (the service has taken
    it);
  - no unproven invalidation left unproved: `SlotsUnproven` 0, or the
    pass that took the last invalidation proved it - a new flag,
    `ReleaseProved`, set under the controller lock by `HcdEnumService` when
    `HcdCtlProveDmaStopped` succeeded and `hcdInvalidate` completed, and
    cleared under the lock whenever an invalidation is raised
    (`XhciSlotInvalidateAll`), at every HCRST and at a start.
    `SlotsUnproven` itself is not cleared by the try: the try's HCRST
    clears it as every HCRST does;
  - the drain done: no device record with a slot (`hc->SlotDevice[]`
    empty), whether or not `TerminalReleased` is set (a latch with no
    device record never raises the release);
  - not pinned, not contained.

### 7.3 The capability guard: revalidation, never an override

Where `HcInfoStatus` is not `XHCI_HC_OK` at the commit (a terminal 2 whose
attempts ended at `ctrl.recover.nohw`), the commit re-reads the capability
registers through the lifetime's mapping (`XhciDeriveControllerInfo` into
a local `XHCI_HC_INFO`: its seven reads - six fixed, and HCCPARAMS2 where
the mapping covers it - and no write; the function's full derivation, its
bounds checks included, kept as it is) and compares them with the
lifetime's validated decode (`XhciHcInfoEqual` against `ext->HcInfo`). Only
when the derivation succeeds and the two are equal does the commit set
`HcInfoStatus` back to `XHCI_HC_OK`, in the same hold that commits the try
(the decode the start validated still describes the window). A derivation
that fails, an all-ones read or a difference (`XHCI_HC_CHANGED`'s case)
is a **skip** (`SkipLayout`, `ctrl.retry.skip` 2): the level raised, the
next wait set, the guard left as it was. The guard itself is not changed:
a recovery requested any other way still meets it.

### 7.4 The commit: the synchronized boundary

The thread's retry step does not change the latch. In DUE, when its
unlocked look passes, it arms the try under the controller lock
(`RetryArmed` 1, with the cause) and wakes the thread. **The try is
committed in `hcdRecover`'s own hold**: under the power gate and the
controller lock, before the ordinary admission, an armed try is evaluated
as one decision:

1. every row of 7.1 and 7.2 read in this hold, the leave conditions of
   7.5 among them;
2. USBSTS read once (as `hcdRecover` reads it): all ones is a **skip**
   (`SkipOnes`, `ctrl.retry.skip` 1);
3. section 7.3's revalidation where it applies;
4. the ordinary admission evaluated as it would read with the latch reset
   (the window empty, the run 0): the defer, the pacing and the window's
   admission.

**If every part passes**, in the same hold: `XhciTolWindowInit(&Tol.Window)`,
`RecoveryFailuresConsecutive` 0, `hc->TerminalReleased` 0 (so a relatch
releases again), `HcInfoStatus` restored where 7.3 revalidated,
`RetryArmed` 0, the burst begun (`RetryBurst` 1, `BurstAttempts` 0), the
level raised, the deadline cleared, the tries counted, the request taken
(`go`), the window charged (the try is the fresh window's first
recovery); `ctrl.retry` (the try number, `Tries` after the count, held by
section 15.3's saturation rule, in 31:16; the wait just ended in nominal
seconds, at most 601 since section 5.7, in 15:0) and `ctrl.retry.why` (1 the wait, 2 a
root-port change) are noted. `ControllerFailed` stays 1 until the recovery clears
it, so interrupts stay masked until then. The recovery then runs exactly
as any recovery does.

**If any part fails**, `RetryArmed` is cleared and nothing else changes:
a skip raises the level and sets the next deadline (LATCHED); an
exclusion that ends only at a start is EXCLUDED; any other refusal leaves
DUE, re-armed at a later pass.

**This hold is the boundary.** Before it, nothing of the latch has moved,
and a stop, a removal, a shutdown or a suspend refuses the commit through
the rows it reads. After it, the recovery is running under the power gate
and is "a recovery already running": a suspend waits for the gate, and a
stop's join waits for the thread to leave, which it does only after
finishing its current pass (section 14). **The burst's own later
attempts yield to a leave**: while `RetryBurst` is set - from the commit
to the burst's end as section 5.5 defines it, and no longer -
`hcdRecover`'s admission of a re-requested attempt also reads the leave
flag under its hold and, where it is raised, defers the attempt uncharged
(the request left owed, terminal 1, `BurstAttempts` unchanged) until the
leave is cleared or the lifetime ends. Every attempt it does admit while
`RetryBurst` is set counts in `BurstAttempts`. `RetryBurst` clears, in
the controller-lock hold that records the outcome, at the burst's first
success, at terminal 2 before any success, at an exclusion, and at the
lifetime's end. Every other recovery - one not born of a try, and every
recovery after a burst's success - keeps today's admission exactly: no
leave deferral, no burst counting.

### 7.5 The leave flag

`RetryLeave`, read only by the commit, the burst's admission, the sampling
and the latched resume, is written **under the controller lock** by every
path that ends or may end the lifetime, **first, before that path's own
work and before its PnP state is stored**:

| Path (`hcd_pnp.c`) | At its entry | At its outcome |
|---|---|---|
| `IRP_MN_QUERY_STOP_DEVICE` | set, before `HcdMsg4QueryStop` (which waits for the power gate) | cleared when the query is refused **locally** - `HcdMsg4QueryStop` failing, after it has restored or latched the connection and before the IRP is completed; cleared when a lower driver refuses it (the `HcdMsg4CancelStop` branch, where the PnP state is restored); kept when it succeeds |
| `IRP_MN_QUERY_REMOVE_DEVICE` | set | kept (a refusal below brings the cancel) |
| `IRP_MN_CANCEL_STOP_DEVICE`, `IRP_MN_CANCEL_REMOVE_DEVICE` | - | cleared where the PnP state is restored |
| `IRP_MN_STOP_DEVICE` | set, before `HcdDoorControllerStop` | kept: the lifetime has ended |
| `IRP_MN_SURPRISE_REMOVAL` | set, before `HcdDoorControllerStop` and `HcdMsg4Gone` (the early signal) | kept |
| `IRP_MN_REMOVE_DEVICE` (98 SE and ME's removal without a surprise removal included) | set | kept |
| `IRP_MN_START_DEVICE` to a controller already started (`hcdStart`'s refusal, which stops the live lifetime as a surprise removal does) | set, before `HcdDoorControllerStop` and `HcdStopController`, only on the live-lifetime branch (`ControllerStarted`); the `MapQuarantined` branch has no lifetime and sets nothing | kept: the lifetime has ended |
| A start that makes a new lifetime | - | cleared by `XhciTolRetryInit` in `HcdStartController`'s initialization, before the thread starts; never at `hcdStart`'s entry |

Each row is one call of a pure function
(`XhciTolRetryLeaveNext(minor, phase, outcome, current)`, the next value)
made under the controller lock, so the table is host-tested whole
(section 18). Every such write, setting or clearing, also advances
`RetryLeaveGen` in the same hold, which the sampling's port holds compare
(section 6.1). Each writer releases the controller lock before it calls
anything of mode 4 or of the teardown, so no new lock order arises.

The shutdown needs no new writer: `ShutLatched` is set in the controller
lock hold that sets `PowerDown` at a shutdown's D3, before the halt, and a
refused D0 sets it the same way; both are rows of the commit. Because
every writer publishes under the controller lock before it acts, and the
commit reads under that lock, a commit either precedes the publication
(the recovery is then already running, as above) or sees it and refuses.
The plain store of the PnP state is still read as a row, but no
correctness rests on it.

## 8. The exclusions, by the task's words

- **All ones:** the commit's USBSTS read (a skip); the sampling's USBSTS
  and PORTSC reads (the sampling stops); a containment that follows is
  EXCLUDED.
- **Memory possibly still written by the controller:** the pinned buffer;
  an invalidation pending; an unproven one not proved; a device record
  not yet drained; the containments. The try frees nothing itself; the
  recovery halts and resets before any ring is reused, and the Bus Master
  Enable it restores is restored after its HCRST has settled.
- **Suspended:** no try, no sampling and no comparison outside D0; the
  latched resume is section 9's.
- **Surprise-removed:** the leave flag set at the IRP's entry, the PnP
  state, and the all-ones reads for a function gone before the IRP; a
  dock's or a tunnelled controller's removal reaches the driver as a
  surprise removal or the tunnel's containment, both excluded.
- **Shutting down:** `ShutLatched`, set before the halt in the shutdown's
  own hold; the thread is joined at the stop.

## 9. Power

1. **While suspended** (S1 to S4, and the direct transition's
   `SuspendedInD0`): no try and no sampling. Section 5.6 is the clock.
2. **Value 0 keeps today's resume exactly.** A latched controller is
   refused by `hcdResumeContained` as now; nothing below runs.
3. **The latched resume, with tries enabled.** In `hcdD0Finish` and
   `hcdDirectTransitionGated`, where `hcdResumeContained` refuses today,
   under the power gate those paths hold, and only when every one of
   these reads true under the controller lock: the value not 0; terminal
   2 or 3; not contained, not pinned, terminal not 6; the release settled
   (7.2); the lifetime running (the leave flag, `ControllerStarted`,
   `ShutLatched` clear); under mode 4, neither `Msg4QueryReleased` nor
   `Msg4Removal`. Then the controller is taken to D0 **without
   reinitializing**:
   - `XHCI_EXT_FLAG_SUSPENDED` cleared; `ControllerFailed`, the terminal
     and the window kept; no Restore State, the saved image not eligible
     (`SavedStateValid` 0, as `XhciTolSaveRefused` already keeps it on a
     failed controller); interrupts left masked;
   - `SuspendHaltProven` cleared, as every resume clears it;
     `TolStartGen` advanced as both paths already advance it;
   - the resume's phase settled: a latched controller has no devices to
     keep, so `ResumeInvalidating`, `ResumeSlotsInvalidated` and
     `ResumeKeepReady` 0, `ResumeKeepPending` 0 (so `hcdResumeKeepWait`
     returns at once), and `ResumePhase` set to `XHCI98_RESUME_FAILED`
     with `MarkGen` advanced: "a resume failed, or the controller is
     contained or failed" (`xhci98_xfer.h`), the true state for the UAS
     driver's wake check (`UasWakeDecide` fails the wake on it) while the
     controller stays latched; and the phase marked as the latched
     resume's (`RetryResumeFailed` 1), in the same hold;
   - the sampling's evidence broken (6.3), the clean run's start cleared
     (5.4); `ctrl.retry.resume` noted.
   Where any condition fails, today's refusal stands unchanged.
   **How that FAILED ends.** The recovery's invalidation pass does not end
   it: `HcdEnumService` moves only `PENDING` to `RUNNING`. So the try ends
   it itself: in `hcdRecover`'s controller-lock hold that records the
   burst's first success (section 5.5), when `RetryResumeFailed` is set and
   `ResumePhase` still reads `XHCI98_RESUME_FAILED`, the phase is set to
   `XHCI98_RESUME_RUNNING`, `MarkGen` is advanced (every quiet mark void,
   as at any resume's end) and `RetryResumeFailed` cleared - before the
   recovery's re-enumeration can present a device, so a UAS drive found by
   it meets `RUNNING`. A burst that latches again leaves the phase FAILED
   and the mark set, and the next try's success ends it the same way. A
   suspend clears the mark where it sets `PENDING` (`hcdSuspendSettled`),
   so the next resume's own rules govern from there; the lifetime's end
   and a start clear it. A FAILED phase not set by this transition - an
   ordinary resume that failed (`hcdResume`) - carries no mark and is left
   as today. The decision is a pure function
   (`XhciTolRetryResumeEnd(phase, mark, recovered)`, the next phase and
   whether `MarkGen` advances), host-tested (section 18).
4. **Under `XhciInterruptMode` 4**, in the same transition and only under
   its conditions: the connection re-armed and opened as the ordinary
   resume does (`HcdMsg4Resume`: `HcdMsgNtReArm` with its one repeat, then
   `HcdMsg4Open`), in that function's own context (PASSIVE_LEVEL, the
   power gate held, outside the controller lock). Answer 0: the bar lifted
   under the controller lock (`Msg4Barred` 0), the desired interrupt flag
   (`XHCI_EXT_FLAG_INTERRUPTS`) left as that function left it, the
   interrupter left masked in hardware (`ControllerFailed`; the try's
   recovery unmasks it). Answer 1 (the re-arm failed, or the open did) or
   2 (braked): the loss latched exactly as `hcdResumeMsg4` latches it
   (`HCD_MSG4_LOST_REARM` or `HCD_MSG4_LOST_BRAKED`), terminal 6,
   EXCLUDED. A loss already latched, a released query and a removal are
   never re-armed here.
5. **A sleep after a try recovered** is an ordinary sleep of a running
   controller; the suspend and the resume clear the clean run's start, and
   it begins again at the next healthy pass after D0 (5.4).
6. **The shutdown latch** and a refused D0 exclude (7.1).

## 10. Windows 98 SE and ME after a controller reset

A try on 98 SE and ME is the same try at the same default; turning the
tries off there would leave every device lost where some would come back.

- **Before the try**: every device was removed by the terminal release;
  its PDOs were reported missing. A dropped port is let go once its old
  PDO group has been reported missing (`hcdPortQuiet`, the owner's ruling
  of 2026-10-04, made for ME), as after any recovery.
- **Requests**: a departed device's requests stay held on 98 SE and ME
  until its driver cancels them or Windows removes the device (task
  50.28's rule, unchanged). A burst drops no device (the devices were
  already gone when it began) and re-enumerates at most once, at its
  success; it is a later fault, handled as an ordinary recovery or ending
  in a new latch, that removes the re-enumerated devices again, with the
  same handling of their requests.
- **Windows ME's gap**: after a controller recovery, devices re-enumerated
  on other ports got no Plug and Play from ME (2026-10-04, r3 t2;
  `hcdCfgFault`'s comment). A try is a recovery, so it may recur; a device
  ME does not show again comes back when unplugged and plugged in again.
  **This record assumes no workaround** (section 22, question 2): a second
  report of relations is not known to be safe or sufficient. Legs (g1) and
  (g2) read what ME does after a try and after a latched resume.
- **98 SE's storage**: a drive on NUSB's `usbstor.sys` and `USBNTMAP`
  re-enumerated by a try is a new device to 98 SE; whether its letter
  returns is read by leg (a) [unread]; an open file on it is lost.
- **The thread on 98 SE** can be left unscheduled while a disk request
  waits (task 36.55): a try can come late, never early; a long gap breaks
  the sampling's continuity (6.3).
- **The latched resume on 98 SE and ME** is section 9's, read by leg (f)
  on 98 SE and (g2) on ME.

## 11. A machine started with the controller already failing, or from a USB drive

1. **A start that fails** has no lifetime, no thread and no machine: no
   try; only Windows starts it again.
2. **A latch during the boot**: the machine runs from the start, the first
   try `B` after the latch; on 98 SE a try during the boot's enumeration
   is a recovery during it, as an ordinary recovery already is.
3. **A latch by the capability guard** (an early attempt refused before
   its decode): section 7.3's revalidation; leg (q).
4. **The firmware's USB disk after the takeover** (record 37): the
   takeover is a start's step; a try does not repeat it.
5. **A machine started from a USB drive** on the controller that latched:
   the system volume left with the terminal release. **A try cannot
   promise the lost system volume back**: on 2000 and later a lost paging
   or system volume usually stops Windows before any try, and what
   Windows does when such a disk reappears as a new device is not read
   [unread]. Task 50.32's restart is refused by Windows for a disk holding
   the paging file. **This record assumes no detection of a system disk
   and no suppression of the tries for it** (section 22, question 3). A
   reproducible guest scenario for it is not part of this record's legs:
   the evidence is deferred, and leg (r) records what a guest booted from
   a USB disk does if one is arranged.
6. **A controller whose earlier lifetime latched** starts afresh.

## 12. Device isolation (task 50.30): its holds, its debts, its records

### 12.1 The holds

- A root place's hold survives a try, as it survives any in-place
  recovery; the try's enumeration does not enumerate a held place.
- A powered hold (reasons 1 and 4) is released only by section 6.6's
  rule: a stable disconnect, then a connection, observed by the sampling
  while stopped or by the in-service pass after the try.
- An unpowered hold (reasons 2 and 3) gives no evidence and stays until a
  start, kept off through the reinitialization's power pass.
- A hub's places start afresh with their hub when the try's enumeration
  brings the hub up again.
- Where isolation applies, a device that stops answering fails alone and
  the controller does not latch: no try is needed for it.

### 12.2 The debts

[s3] While the controller is failed a left command's debt is not settled
and the settlement's records stay held. A try frees nothing: its
recovery's completed HCRST clears the debt, `CmdEnableHeld`,
`InputHeldSlot`, the deferred resets and (record 17 section 4.15.16 B)
every deferred-enumeration obligation, through `HcdEnumLeftHcrstLocked`,
and its re-enumeration looks at every port afresh. A debt is therefore
not an exclusion; excluding it would leave the controller latched for
good, since nothing else settles a failed controller's debt. A burst that
fails before an HCRST completes leaves the debt standing, and the next try
meets it the same way. The try changes neither `HcdEnumLeftHcrstLocked`
nor any of section 4.15.16's rules; rebased on step 3, it keeps them.
Where step 3 is not in, there is no debt and `CmdWaitLeave` is absent from
the rows.

### 12.3 The records and the value

The reset reasons' ring and the latch's record are not cleared by a try;
the spread records are kept for the lifetime. `XhciDeviceIsolation` is
untouched: at its default it applies in the release flavour only on the
one controller its table lists (`8086:1E31`, record 17 section 4.15.10,
since 2026-11-20), and the primary case (leg (c)) is a latch that the
tries end.

## 13. The restart, and the other latches

- **A stop and start** (task 50.32's restart, a Device Manager disable and
  enable, a driver update, a Windows restart) ends the lifetime and clears
  everything of this record. A restart requested while a try is due raises
  the leave flag at its query, so no try commits after it.
- **The shutdown latch** and a refused D0 exclude.
- **The resume refusal** changes only under section 9's conditions; at
  value 0 never.
- **`TerminalReleased`**, once per lifetime today, becomes once per
  latched episode.

## 14. Execution contexts, IRQL and locking

**No new thread, timer, DPC or work item**: the retry runs in the
controller thread, which exists on every target and is joined at every
stop (task 50.27), and reads the tolerance clock, whose timer and DPC
exist. It adds no separately joined execution object and no import. It
does add opportunities for a recovery burst, so a stop can wait longer
than it would have for a controller left latched. A stop raises
`ThreadStop` and joins; the thread looks at `ThreadStop` only at the top
of its loop, so the join waits for the rest of the pass in progress: the
attempt in progress if there is one (one `XhciRecoverController` with its
stall-bounded waits, and up to three continuations, each with a settle of
up to 1000 ms), the rest of `hcdPoll` (the terminal release, the poller's
pass, `hcdContain`, the retry step), then `HcdEnumService` with whatever
invalidation drain and services that pass owes, and the log's flush. The
thread then leaves before the burst's next attempt. Each of those parts
exists today; the retry adds the chance that the pass in progress holds
an attempt, and its own bounded step.

| Piece | Context | IRQL | Locks |
|---|---|---|---|
| The value's read | the start | PASSIVE_LEVEL | none (before the thread starts) |
| The pure decisions (`XhciTolRetry*`) | wherever called | any | the caller's |
| The machine's step and the arming | the controller thread, `hcdPoll` | PASSIVE_LEVEL, DISPATCH_LEVEL while the controller lock is held | the power gate, then the controller lock |
| The sampling | the controller thread | PASSIVE_LEVEL, raised to DISPATCH_LEVEL only inside each short hold of 6.1: the admission hold (at most nine register reads) and one hold per root port (one read each), the lock released between them | the power gate for the whole sweep; the controller lock for each hold alone, never across the sweep; the judgment and the observer's feed after the last hold, unlocked, by the thread alone |
| The commit | `hcdRecover`'s hold | DISPATCH_LEVEL inside the controller lock, the power gate taken first at PASSIVE_LEVEL | the power gate, then the controller lock |
| The recovery the try commits | `hcdRecover` | `XhciRecoverController` at DISPATCH_LEVEL (raised by the thread), the continuations at PASSIVE_LEVEL | the power gate throughout, the controller lock where today |
| The tolerance clock | `hcdTolDpc` | DISPATCH_LEVEL | `TimerLock`, never with the controller lock |
| The leave flag's writers | the PnP dispatch | PASSIVE_LEVEL, DISPATCH_LEVEL inside the lock | the controller lock alone (never the power gate, so a PnP IRP never waits for a recovery to set it) |
| The latched resume | `hcdD0Finish`, `hcdDirectTransitionGated` | PASSIVE_LEVEL | the power gate the path holds; the controller lock for the flags; mode 4's re-arm and open outside the controller lock, in their own allowed context |
| The recovery-status request | `hcdDoorRecovery` | PASSIVE_LEVEL inside `HcdIoEnter` | the door gate, then the controller lock |
| The snapshot | its copy | as today | none: each word whole |

- **Lock order**: the power gate (a wait, PASSIVE_LEVEL only), then the
  controller lock; never the reverse. `TimerLock` and mode 4's
  interrupt-object synchronization are never taken while the controller
  lock is held. **The qemu flavour's `InjLock` is a leaf taken inside the
  controller lock where a hook needs it, as the layer orders them today**
  (`hcd_inj.c`: "InjLock is a leaf, taken at <= DISPATCH_LEVEL"; record
  17 section 4.15.16 C [s3]: the controller lock first, `InjLock` inside
  it, no path the other way): the sampler's reset answer
  (`HcdInjResetAnswer`) and the commit's `31` answer take `InjLock`
  inside the controller-lock hold of 6.1 and 7.4. No hook of section 17
  takes the controller lock, the power gate or any other lock while it
  holds `InjLock`.
- **Who writes what.** The thread writes the machine; the commit writes
  the latch's reset; the PnP writers write the leave flag;
  `HcdEnumService` writes `ReleaseProved`. Every word the door, the
  snapshot, the commit or another context reads is written under the
  controller lock. The per-port baselines and intervals, the sample's
  array and the last sample's stamp are the thread's alone; the clean
  run's words are locked, since the suspend and the resume clear its
  start (5.4).
- **The clock** is read as one aligned ULONG and compared by
  `XhciTolElapsed`, safe across the wrap.
- **Per Windows.** One code path on every target; what differs below it
  is unchanged: the thread's join (the object on NT, the exit event on 98
  SE and ME); `KeSetTimer`'s resolution, which only lengthens a tick; on
  2000 and later with several processors the thread, the clock's DPC, the
  door, the PnP dispatch and the power path run concurrently, covered by
  the lock order and the locked words, and legs (j), (k) and (s) run on
  the 2000 guest with two processors; the x64 build is the same ULONG
  arithmetic.

## 15. What the user sees

### 15.1 The recovery-status words

`hcdDoorRecovery` serves the three reserved words and sets `XHR_F_RETRY`
on a started controller; no new request version:

- `XHR_O_RETRY_VALUE`: the value in effect (0, or 2 to 600).
- `XHR_O_RETRY_TRY`: `Tries`, the tries begun since the backing off last
  ended (0 before the first), under section 15.3's one saturation rule,
  so "try K", this word plus 1, the try that comes next, never wraps; the
  header's comment ("tries made in
  this latch"), the Tree, the fixtures and `snapshot-reader.md` all say
  so in the implementation's commit, before any driver serves the flag.
- `XHR_O_RETRY_NEXT_S`: `XHR_NONE` in OFF, WATCHING, TRYING and EXCLUDED;
  in LATCHED the ticks to the deadline at the nominal period, rounded up
  (`(ticks + 9) / 10`, at most 601 since section 5.7), or the floor's seconds where they
  are more (section 5.7); in DUE, 0.
- `XHR_F_RETRY_NEVER` (`0x08`, in `XHR_O_FLAGS`, with `XHR_F_RETRY`):
  set in EXCLUDED, the state the thread enters once it has seen a
  controller it will not try (the common buffer pinned, closed off,
  interrupts lost; section 7.1). It is the Tree's "will not" (section
  15.2); a controller just stopped, before the thread's pass has seen it,
  reads WATCHING and carries no flag, so no false "will not" shows.
- `XHR_O_TERMINAL` unchanged: 2 or 3 while stopped, 1 between the burst's
  attempts (and between any ordinary recovery's), 0 once running.

### 15.2 The USB Tree and the tray

Record 25 section 26 items 2 and 4 carry the controller's line, its
details and the snapshot's terminal line. This record's new lines (DUE,
and a driver without `XHR_F_RETRY`) and the settings and installer texts
are drafts kept outside the tracked product text until the owner words
them (section 22, question 1). The tray's red icon follows the failed
flag it already follows. No balloon.

**The Tree's lines (owner, 2026-11-19, given through the coordinator).**
In a stopped controller's details, after the status and "involved"
lines, from the three words and the flags (`tr_xhr_details`):

Every line below needs `XHR_F_RETRY`; `XHR_F_RETRY_NEVER` is judged
first:

- `XHR_F_RETRY_NEVER` with `VALUE` not 0, at any terminal the details
  show: "The driver will not try it again by itself. It stays stopped
  until it is restarted.";
- terminal 2 or 3, `VALUE` not 0, `NEXT_S` from 1 to 601 (not
  `XHR_NONE`): "The driver will
  try it again by itself in about N seconds." ("second" where N is 1),
  then "A device unplugged from or plugged into one of this
  controller's own ports, not a hub's, can bring the try sooner.";
- the same with `NEXT_S` 0: "The driver will try it again by itself as
  soon as it can.";
- terminal 2 or 3 with `VALUE` 0: "XhciFailedRetrySec is 0, so the
  driver will not try it again by itself. It stays stopped until it is
  restarted.";
- otherwise (no `XHR_F_RETRY`, `NEXT_S` `XHR_NONE` while a burst runs or
  before the thread's pass): no retry line.

The Settings window's row for the value shows, in its Note slot in place
of Setup's note: "The seconds before the driver tries a stopped
controller again: 2 to 600, or 0 for never. Restart the controller after
a change." The snapshot report keeps its own wording.

### 15.3 The snapshot, both directions

A block of its own, `XHCI_TOL_RETRY` (pure core), appended to the
extension after the last field present at the merge, an even number of
ULONGs, its place and size in two HCD header words appended after the
last one present at the merge (954 and 955 if none other comes first),
the header's word count growing with them. Not in `XHCI_TOL_STATS`. Its
fields: the reading (found, read, in effect, the qemu scale); the machine
(state, level, `Tries`, `TriesTotal`, `TriesRecovered`, `LatchStamp`, the
deadline and its validity, the last cause and stamp, `RetryArmed`,
`RetryLeave`, `RetryResumeFailed`); the burst (`RetryBurst`,
`BurstAttempts`, the last burst's attempts and outcome); the skips
(`SkipOnes`, `SkipLayout`); the waits (`WaitPower`, `WaitLeave`); the
sampling (episodes, missed passes, the sample-off flag and port, the last
episode's port); the storm (the three stamps, `StormRefused`); the clean
run (`CleanArmed`, its start, its reference count, the back-off endings).
**The release's state**, which leg (p) reads: `ReleaseProved` itself
lives in this block, and a word `ReleaseState`, written under the
controller lock at each latched pass and at each change of its inputs,
carries in bits 0 to 5 `SlotsInvalidated`, `SlotsUnproven`,
`ReleaseProved`, `TerminalReleased`, a device record with a slot left,
and `CommonBufferPinned` - so the report shows whether a release is
pending, unproven, proved, drained or pinned.

**One saturation rule.** `Tries` is held at 65534, so that the next try's
number, `Tries` + 1, fits the 16 bits of `ctrl.retry` and of the Tree's
"try K"; every other counter of the block is held at `0xFFFFFFFF`; none
wraps. A note's narrower field shows its counter as held, never a
truncation.

- **A new tool, an old snapshot**: a header that ends before the two
  words is an earlier driver's: "not served by this driver".
- **A new tool, a malformed descriptor**: the block is read only when the
  header's length covers both words, the size is at least the fields this
  build knows, the offset is 4-aligned, and offset plus size lies inside
  the extension's image; otherwise "not a shape this build reads" with the
  two figures, as the reasons' block is refused, and nothing else of the
  report changes.
- **An old tool, a new snapshot**: an earlier `XHCITR` reads the header
  words it knows and finds every block it reads where its header words
  say, since nothing earlier moves; the appended words and block are
  ignored.

`tr_snap.c` prints a "Retries" block after the terminal line, and the
terminal line's suffix of record 25 section 26 item 4. `snaptest.cmd`
fixtures: a latched controller with a try due, one in DUE, one excluded,
one off; an earlier driver's header (no words); a truncated header (one
word); an offset past the image; a size below the known fields; an
unaligned offset. The new snapshot read by the earlier tool (the
`3.5.0.0` `XHCITR`, unchanged report) is a guest reading of leg (o), not a
fixture: the tool decodes only a live driver's answer, never a saved
image, so no canned snapshot can be put before an earlier build of it.

### 15.4 Notes

`ctrl.retry.value` (start), `ctrl.retry.latched`, `ctrl.retry.port`,
`ctrl.retry.storm`, `ctrl.retry.sample.off`, `ctrl.retry`,
`ctrl.retry.why`, `ctrl.retry.skip` (1 all ones, 2 the layout),
`ctrl.retry.burst`, `ctrl.retry.excluded`, `ctrl.retry.resume`,
`ctrl.retry.reset`: events only, at the log's usual levels; the
once-per-episode ones as section 5.5 says.

## 16. The registry value's rows and gates

In the implementation's commit:

- **Both INFs**, every controller path (`src\xhci98.inf`'s
  `[Xhci.AddReg]` and `[Xhci.AddReg.NT]`; `src\xhci98-amd64.inf`'s
  `[Xhci.AddReg.NT]`): `HKR,,XhciFailedRetrySec,0x00010003,5`.
- **The INF gate**: the table entry (`Since` `3.6.0.0`, `0x00010003`,
  `5`, its `Why` and `DefaultWhy` saying 0 off, 2 to 600, anything else
  5), its self-test rows (present, the wrong default, the clobbering
  flags), and both footprint files.
- **`build-and-test.md`**: the row (each start, latched; 5; 0 off, 2 to
  600, else 5; 5; yes) and the count under the table; and the qemu value
  of section 17 in "Not in the table".
- **`tuning.md`**: the row (minimum `0`, maximum `600`, default when
  absent `5`, written `5`), a paragraph in "Controller faults", and the
  corrections to the sentences that say a latched controller "stays
  stopped until it is restarted" and "is not brought back by a resume":
  drafts until the owner words them.
- **The installer's `instValues[]`**: the row, 0 to 600, default 5, its
  texts (drafts), and `test_inst`'s defaults.
- **The Tree's `trsRows[]`**: the row on the controller-faults tab beside
  `XhciTolerance`, applied at a controller restart; a new effect kind (0,
  or 2 to 600; anything else the default) held by `test_trset`, which
  also holds the row in step with the INFs and `tuning.md`.
- **`snapshot-reader.md`**: the "Retries" block.

## 17. The qemu flavour's test hooks

Compiled into the `qemu` flavour alone (`XHCI_FLAVOUR_QEMU`): their reads,
their strings and their behaviour absent from the release image
(`check-flavour-marker.ps1`'s rule); documented in `build-and-test.md` -
the value in "Not in the table" (the owner's exception of 2026-11-18) with
a paragraph, the codes in `xhci_inj.h`, record 17 section 5's table and
the `XhciQemuInject` paragraph. The codes are `30` to `35` (hex), clear of
every allocation on this branch and on the branches that merge before it
(`2C` is already `XHCI_INJ_PSW_STEP` there; `2D` to `2F` are left free
for them):

1. **`XhciQemuRetryScale`**, a REG_DWORD read at each start (machine-wide
   or the controller's key): `0` and `1` none; `2` to `600` divide this
   record's own intervals - the base wait, the ceiling, the clean run and
   the storm limit's ten minutes - by that number, each at least one
   tick; never the stable interval, the continuity gap, the recovery
   window, the pacing gap or any other interval. Above 600, 600. Noted
   `qemu.retry.scale`.
2. **`30`, `RETRY_FAIL`**: every recovery attempt begun is refused at its
   first step without touching the controller, charged as a refused
   attempt (`XhciRecoverCharge`), until `arg` runs are spent (a run ends
   at terminal 2; `0` one, `255` until `CLEAR`).
3. **`31`, `RETRY_ONES`**: the commit's USBSTS read answers all ones for
   the next `arg` armed tries (`0` one); no other read touched.
4. **`32`, `RETRY_PORTSC_ONES`**: the sampling's PORTSC reads answer all
   ones from the next pass until `CLEAR` (port byte one port, `0` all).
5. **`33`, `RETRY_HOLD`**: the next armed try's commit is not attempted
   for `arg` times 100 ms on the clock (`0`: 5 s) - the token kept, the
   thread's passes going on as usual, nothing blocked and no lock or gate
   held for it - so that a query-stop, a stop, a query-remove, a surprise
   removal, a cancel, a suspend or a shutdown begun meanwhile meets the
   commit.
6. **`34`, `RECOVER_EARLY`**: the next `arg` recovery attempts (`0` one)
   have their capability decode's reads answer all ones, so the attempt
   refuses at the decode with `HcInfoStatus` left bad, as a window that
   stopped decoding leaves it; the later attempts then meet the guard
   (`ctrl.recover.nohw`). The commit's revalidation (7.3) is not touched.
7. **`35`, `RETRY_SWEEP_BREAK`**: in the next `arg` sweeps (`0` one),
   between the first and the second port hold, the leave generation is
   advanced in a controller-lock hold of its own, as a raise and a clear
   of the flag would advance it (nothing else written), so that the
   sweep's next port hold finds it moved and discards the sweep.

The latch uses existing codes: `05` (HCH) four times inside the window for
terminal 3; `05` with `30` for terminal 2; `09` (`RESET_EP_FAIL`) armed
until `CLEAR` for a controller left running at the latch; `26` for the
field report's case at a root port; `1B` (`RESET_SLOW`) to stretch a
committed recovery's reset; `03` and `04` (over-current, and its
release) for a port's PP answered clear; `06` and `07` for the
containments. Every code here is written in hex, as `xhci_inj.h`'s
comments write them.

## 18. Host vectors

`test_tol`: the value's reading (absent, another type, 0, 1, 2, 5, 600,
601, 3600, `0xFFFFFFFF`); `B`, `C` and `W(L)` for 2, 5 and 600, the cap
and the level's cap; the deadline across the wrap; the clean run's
healthy-pass predicate (each condition false in turn restarting it) and
its end at the tick boundary; each state's transition and every row of
7.1 and 7.2 in turn, each terminal 0 to 6, pinned with every terminal,
`SlotsUnproven` with and without `ReleaseProved`, a pending invalidation,
a record left; the commit's all-or-nothing (a failing part leaves the
latch's fields and the guard untouched); the revalidation's three
outcomes (equal, different, failed derivation), the last two reachable
only here; the skip's level and deadline; the burst's boundary - its end
at the first success, at terminal 2 before one, at an exclusion and at the
lifetime's end, at most three attempts at `XhciTolerance` 0 and 1 - and
that the recoveries after a success are not counted, not deferred and
admitted as today, the sliding window ageing its oldest stamp out at 1 and
admitting all at 0; the burst's leave deferral; the leave flag's table
(`XhciTolRetryLeaveNext`: every row of section 7.5, the local mode-4
refusal and the live-lifetime START refusal among them, and the new
lifetime's clearing); the resume phase's end
(`XhciTolRetryResumeEnd`: FAILED with the mark and a success to RUNNING
with `MarkGen` advanced; FAILED without the mark, or without a success,
unchanged; the mark cleared by a suspend); the clean run broken by a
suspend and a resume with no pass between them; `Tries` held at 65534 and
the other counters at `0xFFFFFFFF`; the timed-over-port precedence; the episode's baseline only after continuity,
a CSC at the baseline not counted, every break of 6.3 dropping baseline,
interval and observer evidence, the gap threshold; the sweep's short
holds - a port hold finding a condition failed, the leave generation
moved (a raise and a clear between two holds included) or a PORTSC all
ones discarding the whole sweep with no baseline, episode or observer
feed from any port of it, and counting a missed pass; the earliest port try
at `B` after each latch, three in ten minutes, the fourth refused, at the
saturated level too, the history across relatches and clean runs and
cleared only by a start; the latched resume's predicate at value 0 and
not 0; the qemu scale and its floor. `test_xhq` and the Tree's vectors:
the three words and the flag, `NEXT_S`'s rounding, `XHR_NONE` per state,
`TRY` held at 65534. `test_trset` and `test_inst`: the row, its effect and
the default.

## 19. Acceptance legs, in emulation

The `qemu` flavour unless a leg names the release flavour. Guests: 98 SE,
ME, 2000 with one processor and with two, XP, 7 x64, 10 x64 where named.
Evidence: the snapshot ("Retries" block, terminal line, notes), the
recovery-status words read by the Tree, Device Manager. A leg passes only
on what it lists.

- **(a) The timed try.** A HID mouse and a stick; `05` four times inside
  the window (terminal 3): `ctrl.retry.latched`, `NEXT_S` from about 5
  (section 5.7),
  the try at the deadline (`ctrl.retry` 1, `why` 1), one burst note, the
  devices back, terminal 0, the Tree's line and the tray cleared; on 98 SE
  whether the stick's letter returns, recorded. All six guests.
- **(b) The doubling, the ceiling and the burst.** Scale 600, `30` with
  `255`, then `05`: the waits double to the ceiling and hold; three
  refused attempts per try (`ctrl.retry.burst` attempts 3, outcome 2);
  one burst note per try and no note per pass. Then, at scale 10,
  `CLEAR` and `05` repeated after each recovery: the try's burst ends at
  its first success (`ctrl.retry.burst` attempts 1, outcome 1), the later
  recoveries are ordinary (no burst counting), and with `XhciTolerance` 1
  the window refuses a recovery begun inside ten minutes of its oldest
  stamp (terminal 3) and the controller relatches at the raised level.
  Scale 10 for this half because at 600, 60 and 20 the scaled clean run
  (section 5.4) ended, and returned the level to 0, before the next `05`
  could be written. 2000 (two processors), 98 SE.
- **(b2) The same at `XhciTolerance` 0.** `30` with `255`, then a Host
  Controller Event (`1C`) as the trigger (owner, 2026-11-19, given
  through the coordinator): `05` cannot start this leg, since at that
  value a halt is by design not recovered (`XhciTolHchRecover` admits a
  halt's recovery only with the value on) and nothing would follow it.
  Three attempts per burst, terminal 2 every time, never terminal 3. Then
  `CLEAR` and `1C` repeated: the burst ends at its success, the ordinary
  recoveries after it are admitted by the run of failures alone, as today
  at that value, and nothing of this record counts or defers them; each
  reset request restarts the clean run. 2000.
- **(c) The port change at a root port.** `26` (outcome 5 or 6) at a root
  port, `XhciDeviceIsolation` 0 so that the controller latches; the
  failing device removed while latched (`device_del`): whether QEMU's
  halted controller shows the CCS change, recorded; where it does, the
  try about 2.4 s after the removal and not before `B` after the latch
  (`why` 2), the other devices back, the device enumerated when added
  again. Again with the device added at once at another root port, and at
  the other controller. 98 SE, 2000, XP, 7 x64.
- **(c2) Behind a hub.** `09` armed until `CLEAR` on a HID device behind
  a hub at a root port (the endpoint faults take a device under the named
  root port): the controller latches; the device removed from the hub
  while latched brings no try; the try comes at the wait; `CLEAR` before
  it, so the try recovers. 2000, 98 SE.
- **(d) Containment.** `06` and `07`: `ctrl.retry.excluded`, never a try,
  `NEXT_S` `XHR_NONE`, the closed-off text. 98 SE, 2000.
- **(e) Off and invalid.** Value 0: no try, `VALUE` 0, the "off" line.
  Values 1, 601, 3600 and a string: "invalid, read as 5", a try at the
  default's time. 2000, 98 SE.
- **(f) Sleep while latched, tries on.** Latched, then standby or
  hibernation for longer than the wait. The snapshot read before the
  sleep gives the deadline and the clock; the one read after the commit
  gives the commit's clock stamp. Must show: `ctrl.retry.resume`, the
  sampling's evidence broken, and the commit's stamp at or after the
  deadline and at the first admitted pass that reaches it - which is the
  first pass after D0 when the deadline was already due before the sleep
  or was reached while the controller was in Dx with the system running,
  and otherwise the remaining ticks counted after the wake (a sleep adding
  about one tick). 2000 and XP standby and
  hibernation where the guest resumes, 98 SE standby where it resumes;
  under `XhciInterruptMode` 4 on 2000 and XP the re-arm, the open and the
  bar lifted, and a re-arm made to fail by mode 4's existing qemu means
  (record 15 section 4.5.4's legs) latching terminal 6. Where none of
  these guests' sleep control resumes, the leg may be read by hibernation
  on another system whose control does (7 x64 was used); the readings of
  the systems named stay owed until their control resumes.
- **(f2) Sleep while latched, tries off.** Value 0: the resume refused as
  today (`resume.terminal`), nothing of section 9 noted. 2000, 98 SE.
- **(f3) The direct transition: deferred evidence.** The path is reached
  only when `PoRequestPowerIrp` fails, and no forcing of that exists in
  the qemu flavour; this record adds none. The latched resume there calls
  the same predicate and the same transition as in `hcdD0Finish`, held by
  the host vectors (section 18); a guest reading is owed and deferred.
- **(f4) A UAS drive and the resume's phase.** A UAS drive (QEMU's
  `usb-uas`) on the controller, tries on: the controller latched, a
  standby, the latched resume (`ResumePhase` FAILED and
  `RetryResumeFailed` in the snapshot); then the try: at its success the
  phase RUNNING and `MarkGen` advanced, before the drive is presented
  again; the drive re-enumerated, its driver's resume-state query reading
  RUNNING (its own log lines), the volume mounted and a file compared.
  2000, XP, 7 x64; 98 SE where its UAS route runs.
- **(g1) Windows ME, a try.** (a) on ME: which devices ME shows again,
  recorded; one not shown, removed and added again.
- **(g2) Windows ME, a latched resume.** (f) on ME where its standby
  resumes in the guest: the transition, the try, what ME shows.
- **(h) The storm limit.** Scale 10, `30` with `255`, a `device_add` and
  `device_del` loop at a root port while latched, with a CSC already set
  at the baseline: at most three port tries in the scaled ten minutes,
  the fourth refused (`StormRefused`, one note), the history kept across
  relatches, port tries still taken at the saturated level within the
  three. Scale 10 because the scaled ten minutes must be wider than the
  spacing of three port tries, or no fourth falls inside them. 2000 (two
  processors).
- **(i) All ones and the sampling's breaks.** `31`: a skip, the level
  raised, no recovery begun. `32`: the sampling stopped, no port try, the
  timed try still coming. A port's PP answered clear by the over-current
  injection (`03`, where the injection takes a port while latched; it
  needs a device enabled there, so it is armed on the port before the
  latch): the port's evidence dropped while PP reads clear, no episode
  from the transition, and the emulation ended by `04`; a place the
  over-current rule has already held unpowered (reason 3, the
  over-current not cleared within its wait) stays unpowered across the
  try until a start (section 12.1), so the try's own power pass powers
  the port only where no such hold stands; where the injection cannot
  be placed, recorded as not reached and the rule held by the host
  vectors. A missed pass by a
  standby between the removal and the addition: no hold released, no
  episode, until a replug seen whole. **A reset reading as running, both
  halves**: `1B` with `arg` 255 (held until `CLEAR`) and port byte 0
  (HCRST), then again with port byte 1 (CNR), each armed before `05`:
  every attempt's reset is held past the settle, so the run ends at
  terminal 2 with the reset still answered running; the sampler's USBCMD
  and USBSTS reads go through the same answer (6.1), so the snapshot's
  missed passes climb and no baseline is taken while it holds; after
  `CLEAR`, baselines are taken afresh at the next admitted sample and a
  replug then brings a try. **An interrupted sweep**: `35` while latched,
  with two or more root ports: the sweep discarded (missed passes counted,
  no baseline or episode from it), the next sweep whole; and `32` with
  the port byte naming the second port, so that its all-ones read
  discards the sweep before any port's evidence is judged. 2000, 98 SE.
- **(j) A leave before the commit.** `33`, then within the hold: a Device
  Manager disable (98 SE: query-stop and stop; 2000: query-remove and
  remove), a query-stop or query-remove that is cancelled (the disable
  refused by an open handle where the guest gives one), and on 2000 and
  later a surprise removal of the controller (QEMU's `device_del` of the
  xHCI where the guest takes it): no try commits while the leave is
  raised (`WaitLeave` counted), the disable or removal completes, and
  after a cancel the try commits. Under mode 4 on 2000 and XP, the
  query-stop's release with the hold running. 98 SE, 2000 (two
  processors), 7 x64.
- **(k) A shutdown or a suspend before the commit.** `33` with a shutdown
  (no try commits, Windows shuts down) and with a standby (no try in D3,
  the commit after the resume). 2000 (two processors), XP, 98 SE.
- **(l) The clean run.** Scale 600: a try that recovers and the scaled
  ten minutes healthy: `ctrl.retry.reset`, level 0; a reset request
  inside them, an owed recovery (`05` once, recovered), and a standby
  and resume inside them each restarting them (`CleanArmed` 0 in the
  snapshot read after the wake, before any healthy pass). 2000.
- **(m) Holds and debts.** `XhciDeviceIsolation` 1 (the emulator listed
  in the qemu flavour): a root place held with reason 4 (`26`), the
  controller then latched by `05`, the held device removed, left out for
  more than 2.4 s and added again while latched: the hold released at the
  addition (`tol.loc.rearm`), a try (`why` 2), the device enumerated by
  it. The same with the addition before the stable interval: the hold
  kept. With step 3 merged, a debt standing at the latch (a left command
  arranged as record 17 section 4.15.9's legs arrange one): the try's
  HCRST (`cmd.left.hcrst`), the debt and the deferred enumerations
  cleared, the ports enumerated. 2000, 98 SE.
- **(n1) No regression, release flavour.** The release flavour with the
  value at its default on 98 SE, 2000, XP, 7 x64 and 10 x64: the device
  matrix; no note of this record on a healthy controller but
  `ctrl.retry.value`.
- **(n2) No regression, qemu flavour.** The 35-V injection rows `08` to
  `12` and record 17 section 4.15.9's legs 1 to 12 with the tries on:
  their outcomes as recorded, plus a try wherever they end latched.
- **(o) The tools.** Record 25 section 26's T.21 and T.26 with this
  driver; the snapshot fixtures of 15.3 (the new tool on an earlier
  driver's header and on the malformed descriptors) through
  `snaptest.cmd`; and the `3.5.0.0` `XHCITR` run against this driver in a
  guest, its report unchanged but for the values it reads.
- **(p) A release proved.** `09` armed until `CLEAR` so the window refuses
  with the controller running: the terminal release unproven, the Bus
  Master Enable proof taken (not pinned), and the snapshot's
  `ReleaseState` reading `SlotsUnproven` set, `SlotsInvalidated` clear,
  `ReleaseProved` set, no device record left and not pinned (section
  15.3); `CLEAR`; the try commits, the Bus
  Master Enable restored after the HCRST (`ctrl.recover.bme.restored`),
  the controller recovered. 2000, 98 SE.
- **(q) The capability guard.** `34` with `0`, then `05`: the first
  attempt refused at the decode, the next two at the guard
  (`ctrl.recover.nohw`), terminal 2; the try revalidates, restores the
  status and recovers. The revalidation's refusing outcomes are host
  vectors only (section 18): the emulator's capability registers do not
  change. 2000, 98 SE.
- **(r) A machine started from a USB disk.** Deferred evidence: where a
  guest booted from a USB disk on this controller is arranged, what a
  latch and a try do, recorded; not a condition of this record.
- **(s) A stop during a committed burst.** `1B` stretching the committed
  recovery's reset, a disable during it: the disable waits for the pass in
  progress (the attempt, its continuations, the rest of the pass) and
  completes. The burst's note shows either end, both correct: outcome 1
  where the attempt in flight succeeded before the stop took the lifetime
  (the burst ended at its success, section 5.5), or outcome 4 where the
  lifetime ended first; in both, no further attempt after the stop. 2000
  (two processors), 98 SE.
- **(t) A START to a started controller: deferred evidence.** No PnP
  manager has been seen to send one (`hcdStart`'s comment), and no vehicle
  here can; the row of section 7.5 is held by the leave table's host
  vectors, and the implementation's change to `hcdStart` sets the flag on
  the live-lifetime branch before `HcdDoorControllerStop`, read at its
  review. A guest reading is owed if one is ever arranged.
- **(u) A query-stop refused locally under mode 4.** On 2000 or XP under
  `XhciInterruptMode` 4, a query-stop: Windows sends one at a resource
  rebalance, attempted by a PCI hot-add in the guest (QEMU's `device_add`
  on a hot-plug slot); whether either sends the xHCI a query-stop then is
  not read [unread]. Where one arrives and `HcdMsg4QueryStop` refuses it
  (its release unproven, by mode 4's existing qemu means where one makes
  it so): the snapshot's `RetryLeave` clear after the refusal and a due
  try committing afterwards. Where no query-stop arrives, deferred
  evidence, held by the leave table's host vectors; this project has no
  query-stop harness (record 17 section 4.15.16's open question).

## 20. Order, commits, risk

One commit or a short series on a branch of its own, after task 50.30 step
3 has merged (both touch `hcd_ctl.c`, `hcd_power.c`, `hcd_enum.c`,
`hcd_pnp.c`, `hcd.h`, the INFs, the gate, `tuning.md`, the installer's and
the Tree's rows; textual conflicts, resolved at the merge, 50.30 first):

1. The pure core (`XHCI_TOL_RETRY`, its functions) and its vectors.
2. The driver: the value, the machine, the arming and the commit in
   `hcdRecover`, the leave flag's writers, `ReleaseProved`, the sampling,
   the latched resume, the door's words, the snapshot block, the notes;
   the qemu hooks.
3. The rows and gates of section 16; `tuning.md` once worded.
4. The reader and the Tree's lines once worded.
5. The legs of section 19.

**Risk, under the default (on).** It acts only on a controller already
latched, which today stays stopped. Its risks are repeated tries (each a
burst of at most three attempts and at most one re-enumeration, bounded
by the backing off and the storm limit, never ended by a count), after
each successful one the ordinary recoveries the controller's faults bring
in service, the new latched resume (a change to a path every sleep takes,
inert at value 0) with its end of the FAILED phase, the commit inside
`hcdRecover` and the leave flag's writers in six PnP paths and their
outcomes, the reset of the window and of `TerminalReleased`, and on 98 SE
and ME the reappearance gap at each try. `0` turns all of it off.

## 21. Where this record differs from record 17 section 4.15.13

1. **The range**: 2 to 600, anything above invalid (section 4.15.13 kept
   values above 600 as fixed waits up to 3600).
2. **The admission after the release**: section 7.2's rows
   (`ReleaseProved` beside `SlotsUnproven`, the drain, no reliance on
   `TerminalReleased`).
3. **The capability guard**: revalidation (7.3); section 4.15.13 did not
   meet it.
4. **The commit inside `hcdRecover`** behind the leave flag (7.4, 7.5);
   section 4.15.13 reset the latch at the try's admission and relied on
   `hcdRecover` refusing a stop, which its admission does not read.
5. **The port-change try's earliest moment** (`B` after each latch) and
   its real bound (the storm limit), 6.4.
6. **The sampling's admission, its reset suppression and its continuity**
   (6.1 to 6.3); PP clear never evidence.
7. **The hold release**: unchanged, a stable disconnect then a connection
   (section 4.15.13 item 6 implied release at the try on an unplug alone).
8. **The deadline's anchor and the skip** (5.1, 7.4).
9. **The clean run** requires healthy passes (5.4).
10. **The latched resume** only with the value not 0, with its
    bookkeeping (9).
11. **The counters** in the appended block, not `Tol.Stats` (15.3); the
    status words are the three reserved ones.
12. **The qemu hooks** (17), new.
13. **The burst's boundary** (5.5): the try's attempts up to its first
    success; what follows is ordinary recovery, unchanged.
14. **The leave flag** (7.5), new, with the live-lifetime START refusal
    and the local mode-4 refusal among its rows.
15. **The latched resume's FAILED phase** ended by the try's success (9).

## 22. Open for the owner

1. **The strings.** The new Tree lines, the settings window's and the
   installer's texts, `tuning.md`'s row and paragraph and its corrected
   sentences, kept outside the tracked product text until the owner words
   them.

Two further choices are written as **assumptions of this record, for the
decision line** that records the owner's answer (that line is entered
separately, not by this record):

2. **Windows ME's gap** (section 10). Assumed: no workaround; a line in
   the release notes says what to do (record 25 section 26 item 13), and
   legs (g1) and (g2) record what ME does. Not adopted: a second report of
   the root hub's relations after a try on 98 SE and ME, not known to be
   safe or sufficient.
3. **A machine started from a USB drive** (section 11 item 5). Assumed:
   no detection of a system disk and no suppression of the tries; a try
   cannot promise the lost system volume back. Not adopted: a detection
   and no try on a controller that held it.

Both assumptions are the owner's decision (owner, 2026-11-18): no
workaround for Windows ME's gap, a line in the release notes only; and no
detection of a machine started from a USB drive and no suppression of
the tries there.
