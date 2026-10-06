# Phase 35 Record - SuperSpeed on Intel Sunrise Point, the Ungated Port Switchover and Controller Tolerance

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 35 - Release
`2.2.0.0`". The roadmap entry carries the goal, the status, the task list and
the checkpoint; this file carries what each task did and what each reading
said. Where the two disagree about a clause, the roadmap wins.

**Opened at task 35.6 (2026-10-07), while the phase is still open**, from
the roadmap's done-notes for the tasks already done. A section reading "To
be read" is a task or a leg still to be taken, and is filled when it is.

**On `out\...`, `vm\...` and `temp\...` paths in this file.** They say where
a reading was kept on the host or machine that took it; they are not files
a clone has.

**Hardware.** The owner's ThinkPad E460 (`8086:9D2F`, Intel Sunrise
Point-LP), ThinkPad P14s Gen 1 (`8086:02ED`) and Lenovo B490 (`8086:1E31`),
and a tester's HP EliteBook 850 G5 (`8086:9D2F` and `8086:15DB`). Every
hardware reading below is Windows 98 SE or DOS; Windows 2000 has never run
on real hardware in this project, and its legs are virtual-machine legs.

**`2.2.0.0`: cut before the bench** (owner, 2026-10-06; the decisions
table). 35-E is read on real hardware after 35.7's cut, on the `2.2.0.0`
asset, and the release documents are written as if it passes; an outcome
not met is changed and recut before the release is published.

Opened: 2026-10-06 (the phase, on branch `2.2.0.0`, cut from `main` at
`c6f17ba`).

## 35.0 - the first reading on the E460

Read 2026-10-06 by the owner, the E460 under Windows 98 SE with `2.1.1.0`
unmodified, `XHCISNAP -verbosity 3` and a restart first, the applied level
checked in each dump's header; the right rear USB 3 connector, a USB 3 hub
and a UAS-capable USB 3 stick. Eighteen dumps, each taken twice a few
seconds apart - every port empty; each device from power-on, right after a
hot-plug and some 10 s later - kept git-ignored in `temp/e460-cas/` with the
`2.1.1.0` offset table `offsets-211.txt` (SIZEOF 105192).

- Every dump with a device attached reads USB 3 root port 13 at `00001203`:
  CCS and PED set, U0, speed ID 4, no change bits. One taken during a
  hot-plug caught it still in RxDetect.
- No slot is ever made for it: the hub's USB 2.0 half enumerates on port 1,
  the stick is never seen. Decoded, port 13's change events arrive and no
  command follows them.
- The USB 3 protocol's Supported Protocol capability publishes three PSI
  entries (IDs 1 to 3 at 1248, 2496 and 4992 Mb/s, Intel's SSIC rates) and
  none for 4.

The cause is this driver's: `XhciPortSpeedClass` (`xhci_caps.c`) read a
published table as replacing the default IDs, so speed ID 4 decoded as
unknown; the enumeration failed on the speed before Enable Slot, retried
once and was never retried while the device stayed. The Missing-CAS lead
the phase was planned around (Linux's `XHCI_MISSING_CAS`, a link stuck in
Polling) is not supported: the link trains. The owner re-planned 35.1 to
35.4 around the fix the same day (`6e48f94`, review round 1 `fadd358`), the
`XhciMissingCas` gate dropped. Issue 11 is the write-up.

## 35.1 - the speed-table fix

**First, `XHCIQUAL` reads the tables** (owner, 2026-10-06; `37ab542`).
`f17b43e`, review round 1 `6df116b`: each Supported Protocol's PSI entries
decoded (ID, rate, type, lanes) in the read-only scan, a warning when a USB
3 table lists entries but not ID 4, and the raw extended-capability chain
in the log; `dfbac0c` made the banner OS-neutral; `887c768`, review rounds
`e749374` and `35c5723`, keeps protocol tables past the raw dump's bound.
The probes, `XHCIQUAL --probe-only --no-page --log`, in
`xhciqual/results/*-2026-10-06/` (`824433b`, `e2536ec`, `8185dc6`):

| Machine | Controller | USB 3 protocol | PSI entries | ID 4 |
|---|---|---|---|---|
| E460 | `8086:9D2F` rev 21 | 3.0, PSIC 3 | 1-3: SSIC 1248, 2496, 4992 Mb/s | not listed |
| P14s Gen 1 | `8086:02ED` rev 00 | 3.1, PSIC 8 | 4: 5 Gb/s; 5: 10 Gb/s; 6-11: SSIC rates | listed |
| B490 | `8086:1E31` rev 04 | 3.0, PSIC 1 | 4: 5 Gb/s | listed |
| HP EliteBook 850 G5 | `8086:9D2F` rev 21 | 3.0, PSIC 3 | the E460's words, raw DWORDs included | not listed |
| HP EliteBook 850 G5 | `8086:15DB` rev 02 | 3.1, PSIC 2 | 4: 5 Gb/s; 5: 10 Gb/s | listed |

The two machines that never failed list ID 4; the P14s's 6 and 7, SSIC
rates where the defaults would read Gen 1x2 and Gen 2x2, are the vector
that a listed entry wins. Beside them, the same evening, a probe of an AM5
board (PRO B650M-CT-CSM, `eec1182`) for the tolerance work: four AMD
controllers, every USB 3 table listing 4 and 5, so issue 11's decoding does
not arise there.

**The fix** (`6a22d45`, review round 1 `92659f1`; converged at review
round 2; 2026-10-06): on a USB 3.x protocol with PSIC > 0, a PORTSC speed ID
the table does not list falls back to the default meaning of IDs 4 to 7,
each `XHCI_SPEED_SUPER` for every functional decision with its rate and
lanes kept apart; a listed ID wins, even one whose rate reads unknown; any
other unlisted ID stays unknown; USB 2.0 unchanged. The same rule in
`XhciPortRate` and `XhciPortPsivForSpeed`, which takes 4 for SuperSpeed only
where the table does not list it, so a device behind a SuperSpeed hub is
addressed too; a SuperSpeedPlus child of such a table is never given an
unlisted 5 to 7. The Slot Context keeps the controller's raw ID; a failed
enumeration stays terminal. Host vectors with the E460's exact 18-port map
and PSI words (`test_caps`, `test_link`, `test_sshub`), failing against the
unfixed sources as 35.0 read. Host tests green; x86 debug, release and qemu
and amd64 release built, every gate green. `implementation-invariants.md`,
"Port Speed Decoding", carries the exception.

## 35.2 - the E460 reading with 35.1's package

Read 2026-10-06 by the owner, the E460 under Windows 98 SE, the `release`
flavour built at `7dc9eb5` (35.1 and 35.3 in it), `XhciLogVerbosity` 3; ten
dumps git-ignored in `temp/e460-352/` and an ATTO screenshot. The
UAS-capable stick and the USB 3 hub from 35.0, on another USB 3 connector
than 35.0's (owner).

| # | Clause | Read |
|---|---|---|
| 1 | The stick at boot | **Pass**: seen from power-on, at SuperSpeed |
| 2 | The stick unplugged and replugged | **Pass**: two cycles, seen each time |
| 3 | A file copied at SuperSpeed | **Pass**: a large file there and back, `FC /B` clean; ATTO 242 MB/s writing and 245 MB/s reading at 8 MB transfers |
| 4 | The hub's SuperSpeed half, a SuperSpeed device behind it | **Pass**: both halves present after a hot-plug, a replug and a cold boot, the stick behind it each time |
| 5 | The dumps | **Pass**: on root port 14, `enum.port.speed` "ID 4 is SuperSpeed, by default ID, unlisted on a USB 3 table", 5000 Mbit/s, Enable Slot successful; the Output Slot Contexts speed 4, SuperSpeed 5 Gbit/s Gen 1x1, for the stick direct, the hub and the stick behind it (route 2, tier 1) |
| 6 | A Bulk-Only SuperSpeed device | **Not read**: none to hand |
| 7 | `XHCIQUAL`'s probe on the E460, the P14s Gen 1 and the B490 | **Pass**: taken the same day (35.1) |
| 8 | The connector's USB 2.0 pairing | **Read**: on both connectors USB 3 port n shares the connector with USB 2.0 port n - 12 (13 with 1, 14 with 2), where the driver's convention (`xhciPairCompanions`) pairs 13 with 7; that matters to 29-A.5's send-back and holds, not to this fix |

Not read further: the hub's hot-plug and replug settled only after warm
resets of its SuperSpeed half (three, then two more, `SsWarmResets`) and two
failed resets of its USB 2.0 half (`enum.port.fail`, "failed on the
reset"); the cold boot with the hub attached showed neither. Whether that
is the hub's or this driver's is not read. Issue 11 section 6 has the
reading in full.

## 35.3 - the diagnostics the 35.0 reading lacked

Done 2026-10-06; review round 1 `4311819`, converged at review round 2,
which kept the eight-burst budget; host tests and `XHCISNAP`'s self-test
green; x86 debug, release and qemu and amd64 release built, every gate
green.

- **The enumeration notes** (`ed70d62`): bounded shipping `XhciLogNote`
  records for a root port's enumeration, from `XhciLogVerbosity` 2 -
  `enum.port.look`, `reset`, `speed`, `rate`, `slot`, `fail`, `end` and
  `quiet` - packed by pure functions in `xhci_enum.c` with host vectors in
  `test_enum`, 35.0's port 13 as the worked case; eight bursts a root port
  between enumerations, refused bursts counted. `XHCISNAP` decodes them
  after the ring ("root port enumeration notes, decoded").
- **The HCD region** (`22ffb60`): snapshot region 3,
  `XHCI_SNAPSHOT_REGION_HCD`, with no schema change - an eight-word header,
  a 16-word record a root port (its machine state, failure cause, retries,
  slot, raw speed ID with its class and source, warm resets and give-up,
  notes charged and refused) and the 63 counters of `XHCIHC_COUNTERS`,
  which lie outside the extension; laid out by the pure `xhci_snap.c`
  (`test_snap`) and filled word by word by `hcd_door.c`. An older driver
  answers the region `BAD_REGION` and `XHCISNAP` says so.

`xhcisnap/README.md`, `source-files.md` and
`passthru-snapshot-instrument.md` were updated in the same commits.

## 35.4 - the regression on QEMU

**The override** (2026-10-07; built with 35-T.9's first part in `8d27a13`,
review rounds `cade36c` and `b0dd28c`, converged at round 3):
`XhciQemuPsiE460`, a REG_DWORD in the controller's driver key read at each
start, in the `qemu` flavour alone; exactly 1 makes `XhciPortMapOverridePsi`
(`xhci_caps.c`) replace every USB 3 protocol group's PSI table with the
E460's - PSIC 3, `04E00121` `09C00122` `13800123` - after the preflight
parse and the post-reset parse alike, so 35.1's decoding meets the real
table on QEMU's PORTSC and events; noted `qemu.psi.e460`. Host vectors in
`test_caps`. The debug and release images carry none of it.
`build-and-test.md`, "The qemu flavour's test aids", is the operator's
reference.

**The regression** (2026-10-07, development host A, QEMU 11.1.0 TCG,
`qemu-xhci,p2=4,p3=4`; the tree at `26e8aac` with `469b4d8` taken in, the
`qemu` flavour and, on Windows 98 SE, the `release` flavour too; no device
was attached to a build without `469b4d8`). Passed: QEMU's controller
publishes no PSI table (PSIC 0), and every leg read as `2.1.1.0`'s
published `release-x86` build, run beside it on the same guests.

| Leg | SuperSpeed stick at root port 1, `fc` | `usb-hub` and a stick behind it, `fc` | SuperSpeed unplug and replug, `fc` | Result |
|---|---|---|---|---|
| Windows 98 SE, `2.1.1.0` | pass, 5000 Mb/s | pass, 12 Mb/s | pass | the baseline |
| Windows 98 SE, `qemu` flavour | pass, the same wizards | pass | pass | pass |
| Windows 98 SE, `release` flavour | pass | pass | - | pass |
| Windows 2000 SP4, two processors, Driver Verifier `0x1B`, `qemu` flavour | pass | pass | pass | pass; a restart with every device attached came back with no problem code; Verifier `AllocationsFailed: 0` |
| Windows 2000 SP4, two processors, Driver Verifier, `2.1.1.0` | pass | pass | - | the baseline |

PORTSC and the Output Slot Contexts read the same on both builds (port 1
`00001203`, speed ID 4; the hub's port 7 `00000603`, speed ID 1). 35.3's
notes and the HCD region were read in `XHCISNAP` in both guests and both
flavours: `enum.port.speed` named ID 4 SuperSpeed "by default IDs (no
table)" - the source for PSIC 0, never "listed" nor 35.1's fallback - at
5000 Mbit/s, Enable Slot succeeded at the first attempt, and the region
showed port 1 Bound, no failure cause, USB 3, warm resets 0; after the
replug the notes read disconnect, connect, reset, speed, rate, slot and end
afresh within their budget. The SMP guest's slow Start menu and start-up
were the vehicle's: the same on `2.1.1.0`, both processors idle in the
kernel's idle loop. On that guest `XHCISNAP` needs `-c 1`, since its
launcher also gives it an EHCI controller whose `usbport.sys` takes the
first device name.

Not read on QEMU: a SuperSpeed device behind a SuperSpeed hub. QEMU has no
SuperSpeed hub model - its `usb-hub` is Full Speed, as `run-30.md` found -
so the clause has no vehicle here; 35.2 read it on the E460 with the real
USB 3 hub (the stick behind its SuperSpeed half, ATTO and a compare). The
override set is read with 35-V.

## 35.5 - `XhciIntelPortSwitch` 2

Done 2026-10-06. Design record 16 revision 3 (`b44d753`, converged
`0a70121`) before the code; the code `c7b29da`, review rounds `d3bf449` and
`febf5ab`, converged.

- Value 2 performs the switchover on any Intel controller (vendor `8086`),
  the six-id gate bypassed; 1 and any other nonzero number keep the gated
  meaning; the default stays 1 and 0 off; the INFs' value and the `VAL-*`
  row unchanged. The value is read on every Intel controller.
- At the user's own risk (owner, 2026-10-06): the README and the release
  notes carry record 16 section 4a's warning word for word; no
  register-layout check (the owner chose to inform rather than guard).
- The Sony exemption and the refusal on an unreadable identity or
  subsystem kept under 2.
- At every value, the release writes only the registers a route wrote,
  tracked per register (`USB3_PSSEN` at `D8h`, `XUSB2PR` at `D0h`) over one
  started lifetime: the start's decision and the write set moved into the
  pure core (`XHCI_PSW_LIFE`, record 16 section 7a), `PswLife.On` replacing
  `PswOn`, with the lifetime vectors in `test_psw` running on the shipped
  code.
- `XHCISNAP` shows `psw.mode`, `psw.route.written` and
  `psw.release.written` by label, with no decoder change.

Host tests (`test_psw`) and every gate pass on debug, release and qemu x86
and release amd64. Nothing read in a guest or on hardware: that is 35-V
(value 2 on QEMU, not Intel: no write) and 35-E (value 2 on the B490,
reading as 1).

## 35-T.0 - the design

Design record 17, revision 7, converged 2026-10-06 at review round 8
(`edc7a2a`, `869a69f`); its off-switch `XhciTolerance` (owner, the same
day) converged at review round 11 (`a89b62f`, `0251912`, `d9b39c4`,
`bd1fdac`). The class-driver reading of record 17 section 4.10, taken the
same day, static, is design record 13 section 6.7 with its
`legal-provenance.md` rows (`b8bd0b9`): no class driver sends `CYCLE_PORT`;
`hidusb` resets the port once per failed read without limit, `usbstor` at
most three times per episode (once on NUSB), `usbaudio` never. Record 17
revision 8 (35.6, 2026-10-07) records 35-T.5's departures, revision 9 (the
same day) those of 35-T.2, 35-T.3/4, 35-T.6's review fixes, 35-T.8 and
35-T.9's first part.

## 35-T foundation, 35-T.1, 35-T.5, 35-T.6 and 35-T.7

Done 2026-10-06, each reviewed to convergence, host tests green and x86
debug, release and qemu and amd64 release built with every gate green; no
virtual-machine leg yet (35-V's).

- **The foundation** (`9e8fdbf`, review round 1 `69e62dc`): the pure
  `xhci_tol.c` / `xhci_tol.h` - the decisions, the tolerance clock's tick
  arithmetic, the budgets and intervals as named constants, the counters
  and state in the extension, the three values' rules - with `test_tol`;
  both INFs write `XhciTolerance` 1, `XhciIntervalCap` 1 and
  `XhciAvgTrbEsit` 0 under 34.1's rule, with their `VAL-*` rows and the
  footprints.
- **35-T.7, the interval cap and the Average TRB Length switch**
  (`958db76`): the cap after a caller's rewritten bInterval and before fast
  polling, whose eligibility is the uncapped Interval's and whose fallback
  restores the capped one (`test_cap_fastpoll`); a hub's status endpoint
  capped too (owner, option A), never fast-polled.
- **35-T.1, the lost-interrupt backstop** (`db6d369`; review round 1, no
  material findings): `hcdBackstop` after `hcdRecover`, under the power
  gate, peeking the event ring under the controller lock and queueing
  `IsrDpc` through `hcdIsr`'s admission when the same pending event has
  stood for 100 ms on the tolerance clock; `BackstopDrains`;
  `XHCI_FIX_EVT_REARM` retired; records 05 and 15 updated.
- **35-T.6, the controller halted or unreadable** (`fad9760`): HCH with
  R/S confirmed requests the in-place recovery; the window of three
  recoveries in ten minutes; all-ones containment by a step of its own,
  admission closed and new requests parked, Bus Master Enable cleared and
  read back as the proof, or the buffer pinned first.
- **35-T.5, the location the controller gave up on** (`778b730`, review
  rounds `4a9010d`, `82d87fe`, `64461df`; converged at round 4): the PED
  reconnect and the over-current episode (`XHCI_TOL_OC`) at root ports, one
  budget per location (`Tol.HubLoc[]` for hub ports, in the extension so
  the snapshot carries it, `XHCI_TOL_LOC` at 8 ULONGs and the extension
  held under `XHCISNAP`'s extension image - 128 KB then, 256 KB since
  `7e95afa` - by a compile-time check), a
  recovery's own disconnect latched (`RecoveryDisc`), stable progress from
  validated retirements, an unpowered hold kept off through a
  reinitialization, `HcdTolLocCharge` the hook 35-T.3/4 charge. Three
  departures from record 17, which revision 8 takes in section 4.5: an
  exhausted budget leaves the device disconnected and the location held; a
  hub's disabling of a port is charged; a hub's port budgets start afresh
  when the hub object is reused.

## 35-T.2 - the soft retry

Done 2026-10-07: `4ab1663`, review round 1 taken in `6c0ecc1`, `35f77b6`
and `8fd1f49`, converged at round 2. Design record 17 section 4.2, with
five departures its revision 9 records ("As built").

- **Scope** set per pipe when it is opened (`hcdCfgPipeNew`,
  `XhciTolRetryScope`): bulk or interrupt, no streams, the device not behind
  a TT (`TtSlot` 0), the controller not `1022:43B9`, `1022:43BB`,
  `1B6F:7023` or `1B6F:7052`, and `XhciTolerance` on.
- **The interception** in `XhciXferEvent` (`XhciTolRetryDivert`), before
  the TD's terminal mutations: the event kept on the head TD
  (`XHCI_XFER_FLAG_RETRY_DEFERRED`), the queue's retry generation moved,
  the thread woken; three retries a TD (`XHCI_TOL_SOFT_RETRIES`), the fourth
  error taking today's path whole.
- **The thread** (`HcdCfgRetryService`, `hcdCfgRetryOne`): with an
  operation pending - a cancel, ABORT_PIPE, RESET_PIPE, SYNC_RESET_PIPE,
  RESET_PORT, `DrainPending`, and as built also `AbortAll`, a Paused or
  Closed pipe and a stream pipe - or the head changed, the kept event
  replayed (`XhciXferRetryReplay`, `HcdDevRetryReplay`); otherwise Reset
  Endpoint with TSP 1 (`hcdCfgRetryResetEndpoint`), then the doorbell and
  the request cleared only if the generation is unchanged
  (`XhciTolRetryAfterReset`). A failed Reset Endpoint goes to `hcdCfgFault`
  with no replay.
- **Round 1.** (1) `hcdCfgQuiesce` settled a retry before its own Reset
  Endpoint, which the error's still-queued event could follow, so a Set TR
  Dequeue programmed the failed TD's position and cleared the controller's
  saved progress in it: a later cancel that kept it could send OUT data
  twice. The settle now runs after that command completes, before the
  dequeue is read (`6c0ecc1`). (2) A retried TD completed by its
  successor's sweep was not counted recovered; `RetryRecovered` now counts
  the whole completed group (`35f77b6`). (3) A Transfer Event pointer above
  4 GB, whose low dword could alias a queued TD, is refused as Foreign
  before anything reads it (`XhciXferEventHighRefused`, `8fd1f49`); the
  finding predates the retry.
- Counted as `RetryDiverts`, `RetryResets`, `RetryRecovered`,
  `RetryExhausted`, `RetryReplayed` and `RetryResetFailed`. Host vectors in
  `test_tol` and `test_xfer` (the settle moving the dequeue, the swept
  short packet, the high pointer).

No guest or hardware reading: QEMU raises no Transaction Error, and the
injection codes for it are 35-T.9's second part.

## 35-T.3 and 35-T.4 - the device cycle

Done 2026-10-07: the pure half `0e1cb42` and the driver half `c0f429c`,
review rounds 1 to 3 taken in `18f383f`, `bdc93c0` and `adbc4e3`, converged
at round 4; `d69814f` met 35-T.2. Design record 17 section 4.3, with five
departures its revision 9 records ("As built").

- **The producer**, `hcdTolCycleEvent` in `hcd_dev.c` over the pure
  `XhciTolCycleReason`: a code `XhciXferCodeInfo` refuses on a
  non-isochronous endpoint of a slot naming a device, in any endpoint state
  and whether or not a pipe is open there (T3); a Stall, Transaction, Babble
  or Split Transaction error the queue could not match on an open pipe,
  a streams endpoint's pointer no stream ring holds and a pointer above
  4 GB included (T4). It marks the device through `HcdTolCycleMark`, a no-op
  at `XhciTolerance` 0.
- **The thread**: `hcdCycleResolve` reads a T4's endpoint context (Halted or
  Error confirms; anything else is stale, `HaltReads` and `HaltStale`);
  `hcdCycleService` drops a mark whose device or connect generation
  (`HCD_PORT.ConnectGen`) moved (`CyclesDropped`), and otherwise charges the
  location and cycles a published device by `HcdEnumCycle`; a spent budget
  still removes the device and holds the location (`CyclesRefused`). Before
  the PDO, the enumeration's `XHCI_ENUM_EV_ABANDONED` outcome - no Failed
  state, no retry, no continuation - tears the attempt down, and
  `hcdCycleAfter`, once a run, charges the location and feeds the CONNECT
  if it still reads connected (`CyclesPrePdo`).
- **The thread's own EP0 wait** (`hcdThreadControlQuiet`): a mark for that
  device ends it as abandoned for a cycle; `Ep0Stuck` and `ScratchHeld` keep
  the record and the scratch until the cycle's Disable Slot, a failed one
  setting `ScratchTainted`, and `HcdCfgService` waits meanwhile.
- **Round 1** (`18f383f`): a mark made between the wait's last look and the
  waiter's installation, or between its reading and `KeClearEvent`, was
  lost and the wait timed out into a controller reset; the waiter is now
  registered and each look taken under the controller lock. Stale T4 events
  no longer extend the wait: one 5000 ms deadline, a relative timer polled
  between 100 ms slices of the event wait (`XhciTolWaitStep`).
- **Round 2** (`bdc93c0`): on a multiprocessor kernel a flood of fresh
  stale marks could hold the wait past its deadline; a resolution now makes
  the terminal decision itself (`XhciTolWaitResolved`).
- **Meeting 35-T.2** (`d69814f`): the above-4 GB Foreign event feeds the
  producer; a Halted endpoint holding a soft retry is not confirmed as a
  halt with no TD.
- **Round 3** (`adbc4e3`): the retry and the cycle are decided once a pass
  (`XhciTolJoin`, `XhciTolHaltOwner`): Halted is the retry's only while its
  deferred TD is the head, Error always confirms, a confirmed reason
  commits the pass to the cycle, and otherwise the retry runs, so a mark
  arriving between passes cannot postpone both.
- Notes `tol.cycle.mark`, `tol.cycle`, `tol.cycle.prepdo`,
  `tol.cycle.dropped`, `tol.cycle.wait`; no field added to
  `XHCI_TOL_STATS`. Host vectors in `test_tol` (the mark, the action, the
  reconnect, the wait model with its flood mode, the halt owner, the join
  and a pass model) and `test_enum` (the abandoned outcome).

No guest or hardware reading: the injection codes for T3 and T4 are 35-T.9's
second part.

## 35-T.8 - the counters a user can send

Done 2026-10-07: `dd6bd3c` and `df4045c`, review round 1 taken in
`39066c0`, converged at round 4. Design record 17 section 4.8, as its
revision 9 records.

- **The HCD region's header** grows from 8 to 20 words, its version left
  at 1 and the snapshot schema at 5, so an older `XHCISNAP` walks past the
  new words: words 8 to 18 place the tolerance state in the extension image
  (offset and size, counter words, the window's and the clock's offsets,
  each location kind's offset, count and record size, the hub ports a hub
  object), word 19 the terminal reason from the pure `XhciTolTerminal`
  (running, owed, three failures, the window, unreadable).
- **`XHCISNAP`'s `print_tol`**, at every verbosity the door answers: the
  values in effect, the terminal reason, the recovery window, the
  containment and its branch, every nonzero counter by name, the code
  histogram with xHCI Table 6-90's names, and each location charged, held or
  re-armed; `-selftest-tol` in `selftest.cmd`.
- **Round 1**: the driver prunes the window's stamps only at its next
  admission, so a report an hour after three recoveries said "3 of 3";
  `XHCISNAP` now counts only stamps inside the ten minutes against the
  dump's own clock and reports the older ones as kept, past the window.
- Host vectors in `test_tol` (the terminal reason) and `test_snap` (the
  twenty-word header and the 34 counters the tool names).

## 35-T.9 - the pure core's vectors and the `qemu`-flavour injection

**First part** (2026-10-07): `8d27a13`, review rounds 1 and 2 taken in
`cade36c` and `b0dd28c`, converged at round 3; design record 17 section 5,
as its revision 9 records. The pure `xhci_inj.c` with host suite
`test_inj`, the driver half `hcd_inj.c`, compiled into the `qemu` flavour
alone. `XhciQemuInject`, re-read by the thread about once a second: bits
31:24 a sequence, 23:16 the fault, 15:8 a root port, 7:0 the argument.
Built: 01 lost interrupt, 02 root port PED, 03 over-current, 04 its release,
05 HCH, 06 all-ones with the Bus Master Enable proof, 07 all-ones without
it, FF clear; 08 and above reserved and refused with a note.

- **Round 1** (`cade36c`): the lost-interrupt window had no inactive state
  and compared a running count by signed difference, so once the count
  wrapped past `0x80000000` an unarmed window dropped interrupts; the
  thread now arms and disarms it and the ISR decides through the unsigned
  `XhciInjIrqDrop`. Codes 06 and 07 reached only the containment step's
  read, which `XhciTolerance` 0 never takes; the health poll's USBSTS read
  meets them too.
- **Round 2** (`b0dd28c`): a finite all-ones was spent by the health poll's
  read, which a failed controller no longer takes, so it never ended; it is
  now a per-pass snapshot (`XhciInjDeadPass`) spent one pass at a time
  whatever reads it, and a command that needs a live controller is refused
  on a failed one (`XhciInjNeedsLive`).

**Second part** (2026-10-07): `5044966`, review round 1 taken in `6808ff9`;
design record 17 section 5, as its revision 10 records. Codes `08` to `12`
hex, the soft retry's and the device cycle's faults (record 17 section 5's
remaining rows), are built: `08` Transaction Error, `09` the soft retry's
Reset Endpoint failing, `0A` a refused code, `0B` to `0D` a halt with no TD
under Halted, Error and a real stale context, `0E` Endpoint Not Enabled,
`0F` and `10` EP0 during the thread's own transfer and before the PDO, `11`
and `12` the soft retry's two races. The target is an interrupt-IN TD QEMU
is NAKing; QEMU keeps such a TD fetched, so its Stop Endpoint leaves the
dequeue past it, and the layer puts it back with a real Set TR Dequeue
before injecting. `build-and-test.md`, "The qemu flavour's test aids",
documents each code and its outcomes.

- **Round 1** (`6808ff9`): (1) codes 11 and 12 ran their race on any Reset
  Endpoint to the emulated endpoint, so one from an ABORT_PIPE, a cancel or
  a client could ring the controller onto a TD whose buffer had gone back;
  the race now runs only for the soft retry's own reset
  (`hcdInjRetrySurvives`), and any other, or any at `XhciTolerance` 0, is
  answered Success with nothing rung. (2) Every Configure Endpoint for the
  slot ended the emulation, so another function's turned an injected halt
  stale; only one that deconfigures or whose Drop or Add flags name the
  endpoint ends it, and every ending command ends it only on Success
  (`XhciInjCommandSent`). (3) Code 09's stated outcome corrected:
  `hcdCfgFault` cycles a location only when the device is gone, and the
  injected device is present, so the failed Reset Endpoint requests the
  controller recovery, charged to the recovery window - transient, one
  recovery; persistent, the window's terminal, the controller latched
  failed.
- Host vectors in `test_inj` (the races against cancel, abort and
  tolerance-off resets, the Configure Endpoint flags, failed and successful
  completions).

To be read: the injections in a guest, which are 35-V's.

## Review findings on code already in the branch

Review during the later Phase 35 tasks found these in code that earlier
tasks had left in the branch; each fixed on 2026-10-07 is in design record
17's revision 9.

- **A deadlock on the first transfer error, on a multiprocessor kernel**
  (`469b4d8`). `hcdTolCountEvent`, from the 35-T foundation (`9e8fdbf`),
  wrote its `xfer.error` note through `XhciLogNote`, which takes the
  controller lock, from the event drain that already holds it: on a
  multiprocessor kernel the first error completion - a STALL or a
  Transaction Error - spun for ever at DISPATCH_LEVEL. On a uniprocessor
  kernel, Windows 98 SE's, a spin lock only raises IRQL, which is why the
  E460 never showed it. It now writes through `XhciLogNoteLocked`. It was
  introduced during this release's development and was never in a published
  release: `2.1.1.0` predates it.
- **At `XhciTolerance` 0 the root ports' faults went uncounted** (`469b4d8`,
  `8b54faa`, `511b3a3`). The port path returned before counting, so a dump
  taken for record 17 section 4.11's comparison hid disabled ports and
  over-currents. Off, they are now counted and nothing acts: PEC and OCC
  once each as change bits, and a power loss with OCC or without it once
  until PP reads set again (`XhciTolOffOcCount`).
- **An owed recovery shut the containment out** (`a76637d`, 35-T.6). A
  command timed out on a window reading all-ones requested the in-place
  recovery, whose reinitialization failed on the dead registers and whose
  new start generation restamped the all-ones episode on every pass, so
  Bus Master Enable was never cleared and neither branch ran. The recovery
  now waits, owed and uncharged, while all-ones stands and the containment
  would be admitted (`XhciTolRecoverDefer`), and the containment's admission
  (`hcdContainAdmitted`) no longer asks `HcInfoStatus`, relying on the
  layout the start validated.
- **A held slow request lost its dispatch stamp** (`02ee24d`, 35-T.6). A
  RESET_PIPE or SYNC request queued for the thread had its stamp
  overwritten, and the containment's hold stamped it again, so an
  ABORT_PIPE submitted after it and already run no longer covered it and it
  stayed held for good. The stamp now moves to `DriverContext[1]` and the
  hold puts it back.
- **A hub port's repower re-armed its own budget** (`cd8f744`, 35-T.5).
  Neither external hub path marked the recovery's disconnect when it
  powered a port again after an over-current, so a device away for the
  stable-disconnect interval and back re-armed a spent budget and released a
  powered hold. `HcdHubPortLook` and `HcdSsHubPortLook` now mark it
  (`HcdTolLocRecovery`) before setting PORT_POWER.
- **Not fixed: a pinned teardown can wait for ever.** After the controller
  became unreadable, if the containment found no Bus Master Enable proof and
  pinned the buffer, and an adapter-channel allocation was waiting for map
  registers the kept transfers hold, a later stop, disable or restart waits
  for ever in `HcdDmaMapDrain`. Rare, since a 32-bit scatter-gather bus
  master normally needs no real map registers. The fix proposed is a
  "channel abandoned" teardown (the adapter, the record and the FDO kept,
  `hcdMapExecute` safe after the teardown, a later start refused while the
  request is queued). A known limitation of `2.2.0.0` in the release notes,
  the README and the package readme; record 17 section 4.6.

**The final review of the whole branch** (2026-10-07, at `2.2.0.0`) found
these, fixed the same day and taken into design record 17's revision 10;
it converged at round 4, on `2000fcc`.

- **An unproven invalidation released DMA buffers** (`a113df2`). The
  in-place recovery and the reinitializing resume invalidate every slot
  before their own halt; when that halt timed out or an earlier step
  refused, no HCRST followed, yet the thread drained every device,
  handing back client buffers and map registers the controller could still
  be writing. In the HCD since `2.0.0.0`. The invalidation is now
  remembered as unproven until `XhciSlotInit` (after a completed HCRST),
  and the thread first takes 35-T.6's proof (`HcdCtlProveDmaStopped`); with
  it the devices drop as before, without it the common buffer is pinned and
  the no-proof containment runs. A DMA safety rule, at every
  `XhciTolerance` value.
- **The recovery after a proof could never succeed** (`372dcd4`). The proof
  cleared Bus Master Enable, and the recovery's retry, at DISPATCH_LEVEL
  with `InitBelowPassive`, refused to set it again, so every attempt failed
  at `XHCI_INIT_STEP_BUS_MASTER_RESTORE`. `hcdRecover` now sets it at
  PASSIVE_LEVEL after the proven halt and reset (`hcdRestoreBusMaster`) and
  runs the sequence once more as the same recovery, the window not charged
  again.
- **The no-proof branch latched nothing** (`b21e59d`, `2000fcc`). A later
  recovery or resume could reinitialize into the pinned allocation, and
  every later drain kept the new transfers too. The branch now latches
  35-T.6's containment terminal (`ControllerFailed`, `Tol.Unreadable`,
  `Contained` pinned), whose value now says which containment set it (1
  unreadable, 2 DMA unproven); `XhciTolTerminal` reports the second as
  terminal reason 5, which `XHCISNAP` prints as "CONTAINED: halt and reset
  did not complete and Bus Master Enable would not clear; DMA not proven
  stopped, common buffer pinned" rather than as an all-ones controller.
- **An Incompatible Device Error on a transfer was ignored** (`f5c458c`,
  `3cc80ec`). Code 22 is fatal to the slot and asks for a Disable Slot
  (xHCI 1.2 Table 6-90); the HCD acted on a command's and not on a
  transfer's, so the slot stayed enabled; the gap predates this release.
  Both
  now take the device down with a Disable Slot and re-enumerate it
  (`hcdSlotFatalService`, `HcdEnumCycle`), counted in
  `IncompatibleDeviceTeardowns`, noted `slot.fatal.cycle`, at every
  `XhciTolerance` value; the re-enumeration is charged to the location's
  budget at every value, 0 included (`XhciTolLocActive`, `XhciTolLocHeld`),
  so a device answering 22 to every Configure Endpoint is removed and its
  location held after three rather than re-enumerated for ever. Host
  vectors in `test_xfer` (every code: only 22 is slot-fatal) and `test_tol`
  (the predicates at 0 and 1, the budget's boundary, the re-arm and the
  start's release).
- **The configuration's fallback cycles bypassed the budget** (`3459f81`).
  A failed RESET_PORT recovery and a departing device's failed command
  cycled the location uncharged; both go through `hcdCfgCycle`, charged
  with tolerance on, a refusal still cycling the device and holding the
  location (`CyclesRefused`).
- **One cycle could be charged twice** (`f364849`). A RESET_PORT failure
  answered with code 22 was charged by the fallback and again by the
  slot-fatal teardown, and with two earlier cycles the second charge
  refused the third re-enumeration the budget permits. Each port now keeps
  the charge of the cycle pending at its connect generation
  (`XHCI_TOL_CYCLE_CHARGE`, `XhciTolCycleCharge`), made once and read by
  every later producer. Host vectors in `test_tol`
  (`test_cycle_charge`).

**The extension image grows to 256 KB** (`7e95afa`). Once 35-T.2 added its
fields, the amd64 extension outgrew the 128 KB image `XHCISNAP` keeps it in,
and `hcd_door.c`'s compile-time check stopped the amd64 build.
`EXT_IMAGE_MAX` and the check both move to 256 KB, still one static
allocation.

Host tests read green at `adbc4e3` (`test\run-host-tests.cmd`, 2026-10-07,
at the reconciliation of the documents).

## 35-V - the legs

To be read: host tests and every gate, x86 and amd64; the QEMU legs on
Windows 98 SE and the Windows 2000 SP4 guest (SMP, Driver Verifier), a
virtual-machine regression and not a Windows 2000 hardware reading; 35.4's
regression; 35.3's notes and region read in `XHCISNAP`; `XhciIntelPortSwitch`
2 on QEMU; 35-T.9's injections against record 17 section 5's outcomes with
the class drivers as record 13 section 6.7 reads them; the three values
written by a fresh install and kept by an update; with no fault injected,
every leg reading as `2.1.1.0`.

## 35.6 - the docs

2026-10-07: the release notes and README (the Sunrise Point fix, value 2's
warning, the controller-fault handling and the three values, the AMD
report), the readme template, records 13 (section 6.8), 15 (section 2), 16
(header, section 13) and 17 (revision 8), `implementation-invariants.md`
("Fatal Errors" rewritten; "Port Speed Decoding" checked),
`failure-diagnosis.md`, `xhciqual/hardware-testing.md`, issue 11 closed,
`source-files.md` and this file. The readings 35-V owes stand in the
release notes as `TODO(35-V: ...)` markers, which the cut refuses to run
over. `xhcisnap/README.md` already carried the enumeration notes and the
HCD region (35.3); its tolerance counters are 35-T.8's.

The same day, once 35-T.2, 35-T.3/4, 35-T.8 and 35-T.9's first part had
converged, the documents written before that code were reconciled with it
at `adbc4e3`: design record 17 revision 9 records each departure; the
release notes, the README and the readme template say what
`XhciTolerance` 0 still counts, which error the soft retry takes, that a
spent budget removes the device at the next fault, and the pinned teardown
as a known limitation; record 13 section 6.8, `implementation-invariants.md`,
`failure-diagnosis.md` and `source-files.md` follow the code; and this file
records the four tasks.

Again at `2000fcc`, once 35-T.9's second part was built and the final
review of the branch had converged: design record 17 revision 10; the
release notes, the README and the readme template (a device the
controller reports incompatible removed and re-enumerated a bounded number
of times, and a recovery that cannot stop the controller closing it off,
both at every `XhciTolerance` value, with `XHCISNAP`'s new terminal text);
`xhcisnap/README.md` (terminal reason 5); `implementation-invariants.md`'s
"Fatal Errors"; and this file.

## 35.7 - the cut

To be read.

## 35-E - the bench

To be read after 35.7's cut, on the `2.2.0.0` asset (decisions table,
"`2.2.0.0`: cut before the bench").

## Review

- 35.1: round 1 taken in `92659f1`; round 2 converged. `XHCIQUAL`'s PSI
  tables: round 1 `6df116b`; the protocol-table reserve rounds 1 and 2
  `e749374` and `35c5723`.
- 35.3: round 1 taken in `4311819`; round 2 converged, keeping the
  eight-burst budget.
- 35.5: record 16 revision 3 converged before code (`0a70121`); the code's
  rounds 1 and 2 taken in `d3bf449` and `febf5ab`, then converged.
- 35-T.0: record 17 converged at round 8 on revision 7, its off-switch at
  round 11; the rounds and where each finding is answered are record 17
  section 10.
- 35-T foundation: round 1 taken in `69e62dc`. 35-T.7: round 1 taken in
  `958db76` (the cap counted only once the Configure Endpoint succeeds),
  round 2 clean. 35-T.1: round 1, no material findings. 35-T.6: five rounds
  folded into `fad9760` (round 1: HCH during a resume, the window bypassed
  by a resume, `Unreadable` tested after Gone, the drop's flush completing
  queued commands `DEVICE_GONE`; round 2: held commands lost their pipe;
  round 3: a held stream request lost its endpoint; round 4: a helper
  re-read the PDO's controller; round 5 clean). 35-T.5: rounds 1 to 3 taken
  in `4a9010d`, `82d87fe` and `64461df`, converged at round 4.
- 35-T.2: round 1 taken in `6c0ecc1`, `35f77b6` and `8fd1f49`; round 2
  converged. 35-T.3/4: rounds 1 to 3 taken in `18f383f`, `bdc93c0` and
  `adbc4e3`, converged at round 4. 35-T.8: round 1 taken in `39066c0`,
  converged at round 4. 35.4's override and 35-T.9's first part: rounds 1
  and 2 taken in `cade36c` and `b0dd28c`, converged at round 3. 35-T.9's
  second part: round 1 taken in `6808ff9`. The final review of the whole
  branch: its fixes `a113df2` to `2000fcc`, converged at round 4. What these
  reviews found in code already in the branch, and its fixes, is "Review
  findings on code already in the branch".
- The re-plan and issue 11: round 1 taken in `fadd358`.
- 35.6: to be read.
