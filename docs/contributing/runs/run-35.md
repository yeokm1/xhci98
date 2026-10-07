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

Read in a guest at 35-V (2026-10-07, development host A): codes 08 to 12
at `6808ff9` on Windows 98 SE and the Windows 2000 SMP guest under Driver
Verifier, transient and persistent, and 08 and 0A at `XhciTolerance` 0,
each reaching record 17 section 5's outcome (35-V's table). Two test-aid
gaps the legs found are fixed: code 02 did nothing on QEMU, which ignores
a PED write (`cabedf5`, read at `efc6fe1`), and a long persistent 08 could
strand the mouse's TD once QEMU's dequeue stopped on the Link TRB
(`6c39877`, host vectors only).

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
  the README and the package readme; record 17 section 4.6. 35-V saw the
  pinned branch's disable hang on both guests, at a device PDO's removal
  rather than in `HcdDmaMapDrain` ("Known limitations observed").

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

2026-10-07, development host A, QEMU 11.1.0 TCG, `qemu-xhci,p2=4,p3=4`.
Two guests, each on a throwaway overlay: the Windows 98 SE gold image at
one processor, and the Windows 2000 SP4 guest at two processors (MTTCG)
with Driver Verifier `0x1B` on `xhci98.sys`. A virtual-machine regression
and fault reading, not a Windows 2000 hardware reading. On both, QEMU's
`usb-mouse` at bus port 2 (root port 6, High Speed) was the HID device
every injection aimed at, and a `usb-storage` stick at bus port 1 (root
port 1, SuperSpeed, 5000 Mbit/s) carried the Bulk-Only copy run beside
the faults (four 4 MB copies, each compared with `fc /b`), so the class
drivers were read as record 13 section 6.7 reads them. Counters, notes and
terminal text were read in `XHCISNAP` (on the 2000 guest with `-c 1`) and
the driver's trace on QEMU's debug port.

**The builds.** Each leg built `XHCISNAP` and the x86 `qemu` and `release`
flavours in its own tree, with a private TEMP; every build and
`make-package` exited 0.

| Legs | Commit | `qemu` `xhci98.sys` (SHA-256, size) | `release` `xhci98.sys` (SHA-256, size) |
|---|---|---|---|
| Windows 98 SE, codes 01 to 07 and the values | `ed73d0a` | `65d09fec1389cfa97fa23a2ed39820f504dfae55dbd0c882ca9857060d574539`, 236,282 B | `ca621d30cd2f5c7fa62e0c9b4f4befe8057fe03844be12475182ebdc51ad831f`, 191,941 B |
| Windows 2000 SMP, codes 01 to 07 and the values | `ed73d0a` | `4258ee32f016ea156b52df6d6df46bbc674b17d0d97a651f6d62983eb9fb7353`, 236,282 B | `21c4974515859aed99cbd15eca970a2fc4cbc076dff6681d08b1fd94e4dca41f`, 191,941 B |
| Codes 08 to 12, both guests | `6808ff9` | `f820399b54746652c111c493bbdda3f31dabc9fcb8103ced1265b0085b9a1463`, 243,386 B | `e67d16eecc742df4d8a5610859f707455b07dcb98bee4ff36ede3ce0ac080e8b`, 191,941 B |
| The re-run on the final code, both guests | `efc6fe1` | `af32ae2e1ac3a4ba94cd0bf2a84232a30c75f8b701dfef6987473122e5fa6097`, 246,442 B | `aa305f39bb32837512931d129b904ff55e3c1279e7e6fec26640822871f00178`, 194,501 B |

`ed73d0a` carries 35-T.9's first part (codes 01 to 07) and none of the
final review's fixes; `6808ff9` adds the second part (codes 08 to 12);
`efc6fe1` has every fix below but `6c39877`, which touches only the
`qemu` flavour's injection layer. The legs built the x86 images alone:
host tests and the gates on x86 and amd64 are read at the cut (35.7).

**Windows 98 SE, codes 01 to 07** (`ed73d0a`, the `qemu` flavour unless
named; T transient, P persistent; the mouse and the stick hot-added once
an MS-DOS prompt was open, see the vehicle notes):

| Fault | Expected (record 17 section 5) | Observed | Counters | Result |
|---|---|---|---|---|
| `XhciQemuPsiE460` 1, controller disabled and enabled | the stick at SuperSpeed by 35.1's fallback | `qemu.psi.e460` 1; port 1 "ID 4 is SuperSpeed, by default ID, unlisted on a USB 3 table", the slot's PSIV 4, Gen 1x1; `fc` clean; unplug and replug the same; set back to 0 | - | pass |
| 01 T | the backstop drains it | `irq.lost` 1; `fc` clean; the mouse moves | `BackstopDrains` 1 | pass |
| 01 P | each delivered by the backstop, no terminal | 150 lost; the copy `fc` clean; the mouse moves | `BackstopDrains` 149 | pass |
| 02 T and P | re-enumerated; held powered | nothing: QEMU ignores the PED write (below) | `PedFaults` 0 | not makeable at `ed73d0a`; read at `efc6fe1` |
| 03, then 04 | repowered, re-enumerated | `tol.port.oc` 6, disconnect, repower, re-enumeration; the mouse moves; `fc` clean | `OcFaults` 1, `Repowers` 1 | pass |
| 03 P | held unpowered until a start | "NOW HELD UNPOWERED, over-current never cleared"; the mouse gone, the stick unaffected, `fc` clean; Device Manager disable and enable: the mouse back, the counters clean | `OcFaults` 2, `Holds` 1, 0 after the start | pass |
| 05 T | one recovery | `ctrl.hch`, recovery begun and recovered; every device re-enumerated, so the copy in flight failed ("Invalid drive"); `fc` clean after it, the mouse moves | `HchRecoveries` 1, window 1 of 3 | pass |
| 05 P | the window's terminal | three recoveries, the fourth refused: "LATCHED FAILED: recovery window refused a fourth" | `HchRecoveries` 4, `WindowRefused` 1 | the terminal passes; the defect fixed by `882245d` beside it |
| 06 T, 07 T | nothing | the episode stamped, not contained; `fc` clean, the mouse moves | `DeadEpisodes` 1, then 2 | pass |
| 06 P | contained, released | "CONTAINED", "RELEASED: Bus Master Enable read back clear, devices dropped"; F: an invalid drive, no hang; CLEAR, disable and enable: a clean start, `fc` clean, the mouse moves | `tol.contained` 1 | pass |
| 07 P | contained, pinned | "CONTAINED", "PINNED"; the stick "General failure reading drive F", no hang; CLEAR, then a Device Manager disable hung for good (QUERY_STOP reached the controller, no STOP followed, the clock stopped) | `tol.contained` 2 | the containment passes; the hang is the known limitation below |
| 03 and 04 at `XhciTolerance` 0 | counted, nothing acts | no disconnect, repower or hold; the mouse kept working; `XHCISNAP` showed port 6 connected with PP clear (the emulated loss, which only a repower ends, and 0 makes none) until CLEAR | `OcFaults` 1, `Repowers` 0, `Holds` 0 | pass |
| 05 at 0 | no HCH recovery | no `ctrl.hch`, no recovery; the mouse moved and completions advanced | every action counter 0 | pass |
| Back to 1 | a clean start | on, counters clean, `fc` clean, the mouse moves | 0 | pass |
| The three values | written 1, 1, 0 | an update on an overlay of the gold image, which never had them: `XhciTolerance` 1, `XhciIntervalCap` 1, `XhciAvgTrbEsit` 0 written (read with `regedit /e`); a fresh install was not practical on the gold image | - | pass, by the update path |
| `release`: `XhciAvgTrbEsit` 1, `XhciIntervalCap` 2, `XhciIntelPortSwitch` 2, restart | devices work; the cap applies; no switchover write on a controller not Intel | the mouse moves, `fc` clean; the `usb-hub`'s status endpoint (Full Speed, 255 ms) capped, "1 endpoint(s) capped"; `psw.gate` 0, no `psw.mode` or `psw.route.written` note | `CapApplied` 1, `CapIntervals` 1 | pass |

**Windows 2000 SP4 SMP under Driver Verifier, codes 01 to 07** (`ed73d0a`,
the `qemu` flavour). Installed with Have Disk over the base image's
`1.0.0.3`, no restart asked; the new driver key held `XhciTolerance` 1,
`XhciIntervalCap` 1 and `XhciAvgTrbEsit` 0, written by the install into a
key that had never had them.

| Fault | Expected | Observed | Counters | Result |
|---|---|---|---|---|
| None: the regression | as 35.4 read `2.1.1.0` | the stick at root port 1, 5000 Mbit/s, `fc` clean; a `usb-hub` with a stick behind it (12 Mbit/s), `fc` clean; the SuperSpeed replug, `fc` clean; PORTSC port 1 `00001203` (speed 4), port 6 `00000E03` (3), port 7 `00000603` (1); the HCD region 844 bytes; the tolerance block on, `XhciIntervalCap` 1 not applying, `XhciAvgTrbEsit` 0, running, window 0 of 3, containment none | 0 nonzero; Success 2513, Short 11, Stopped 2 | pass |
| `XhciQemuPsiE460` 1 | as on 98 SE | "ID 4 is SuperSpeed, by default ID, unlisted on a USB 3 table", PSIV 4; the HCD region's port 1 Bound by the same; `fc` clean; back to 0 | - | pass |
| 01 T | the backstop drains it | `irq.lost` 1; four copies `fc` clean | `BackstopDrains` 1 | pass |
| 01 P | each delivered, no terminal | copies slowed (one 4 MB copy about 3 minutes) and all four `fc` clean; the mouse moves; CLEAR ended it | `BackstopDrains` 592 | pass |
| 02 T, P and at 0 | re-enumerated; held powered; counted | nothing (QEMU ignores the PED write) | `PedFaults` 0 | not makeable at `ed73d0a`; read at `efc6fe1` |
| 03, then 04 | repowered, re-enumerated | the mouse back and moving; copies clean | `OcFaults` 1, `Repowers` 1 | pass |
| 03 P | held unpowered until a start | "NOW HELD UNPOWERED"; the mouse gone; copies clean | `OcFaults` 2, `Holds` 1 | pass |
| Disable and enable over 03's hold (02's leg, 03 standing in) | each start begins clean | controller, root hub and hub started; the mouse re-enumerated and moving; window 0 of 3, no location; Verifier `AllocationsFailed` 0 | 0 | pass |
| 05 T | one recovery | recovered; every device dropped and re-enumerated: Unsafe Removal and Delayed Write Failed boxes, copies 2 to 4 failed ("device no longer available"); after it every device started, `fc` clean | `HchRecoveries` 1, window 1 of 3 | pass; the copy in flight lost |
| 05 P | the window's terminal | recoveries 2 and 3, the fourth refused, "LATCHED FAILED" | `HchRecoveries` 4, `WindowRefused` 1 | the terminal passes; the defect fixed by `882245d` beside it |
| 06 T, 07 T | nothing | the episode only; copies clean, the mouse moves | `DeadEpisodes` 1 | pass |
| 06 P | contained, released | "CONTAINED", "RELEASED"; devices removed (Unsafe Removal), copies 3 and 4 failed; CLEAR, disable and enable: devices back, `fc` clean, a clean tolerance block | `tol.contained` 1 | pass |
| 07 P | contained, pinned | "PINNED: no proof DMA stopped; buffer, transfers kept"; Delayed Write Failed boxes; CLEAR, then a disable: query-removes to every object, then a device PDO's REMOVE_DEVICE, and nothing more for more than 3 minutes | `tol.contained` 2 | the containment passes; the hang is the known limitation below |
| `XhciTolerance` 0, then Have Disk again | the user's 0 kept | 0 kept, same key, no restart asked; `XHCISNAP` "XhciTolerance 0 (off: 2.1.1.0's handling; faults seen still count)" | - | pass |
| 05 at 0 | `2.1.1.0`'s handling | no `ctrl.hch`; `2.1.1.0`'s health-poll recovery (`ctrl.failed.here`, recovery begun, recovered); devices dropped and back | `HchRecoveries` 0, window 0 of 3 | pass |
| 03, then 04, at 0 (standing in for 02) | counted, nothing acts | no repower, no hold, no location | `OcFaults` 1, `Repowers` 0, `Holds` 0 | pass |
| Back to 1 | a clean start | on, 0 nonzero | 0 | pass |

Verifier read `0x1B` and `AllocationsFailed` 0 at every check; no bugcheck
in either session.

**Codes 08 to 12, both guests** (`6808ff9`, the `qemu` flavour; on 98 SE
`XhciTolerance` absent, which is on). The 2000 guest's install wrote the
three values into its new key as above.

| Group | Fault | Expected | Windows 98 SE | Windows 2000 SMP | Result |
|---|---|---|---|---|---|
| Soft retry | 08, three injections | retried, recovered | `Diverts` 3, `Resets` 3 (Reset Endpoint answered Success), `Recovered` 1 | the same | pass |
| | 08, four | exhausted, then today's path | `Exhausted` 1, `QueueErrors` 2, `QueueHalts` 2; an EP0 Stall after (hidusb's CLEAR_FEATURE(ENDPOINT_HALT), which QEMU stalls); the mouse moves | the same | pass |
| | 08 P | four injections a TD, then today's path; no cycle, no loop | about 45 s: `Exhausted` 15, `Diverts` 48, an EP0 Stall each; no cycle, no hold; the mouse moves after CLEAR | `Exhausted` 12, `Stall` 12; the mouse moves after CLEAR | pass; one long run stranded the mouse, the test aid's (`6c39877`, below) |
| | 08 P with the copy | the copy clean | four `fc` clean, `Exhausted` 31 to 44 meanwhile, no cycle, no hold | four `fc` clean | pass |
| | 09 | the failed Reset Endpoint asks one recovery | answered Context State Error, `RetryResetFailed` 1, window 1 of 3, devices re-enumerated, `fc` clean | the same, the mouse back; the stick came back at problem 31 (Code 31) until it was replugged: the limitation below | pass |
| | 09 P | the window's terminal | three recoveries, the fourth refused, LATCHED FAILED; the later shutdown hung | the same, `RetryResetFailed` 4; a later disable hung (query-removes, nothing after) | the terminal passes; the hang is the defect fixed by `882245d` |
| | 11 | the race on the ring | `held.ring`, Success, `Recovered` +1 | the same | pass |
| | 12 | the second stop | `held.second`, two answers, `Diverts` and `Resets` +2, `Recovered` +1 | the same | pass |
| | 11 P, 12 P | each retry's reset raced; no cycle | 25 s each: `Exhausted` +8, the mouse moves after CLEAR | not run | pass |
| Device cycle | 0A | a cycle, refused code | `CyclesRefusedCode` 1, port 6 charged 1, re-enumerated | the same | pass |
| | 0B | a cycle, halted with no TD | `CyclesHaltNoTd` 1, `HaltReads` 1 | the same | pass |
| | 0C (after a replug re-armed the budget) | a cycle, Error | `CyclesHaltNoTd` 2, `HaltReads` 2 | the same | pass |
| | 0D | the context read only, no cycle | `HaltStale` 1 | the same | pass |
| | 0E | a cycle, Endpoint Not Enabled | `CyclesRefusedCode` 2 | the same | pass |
| | 0A, 0B, 0C, 0E P | three cycles, the fourth held (powered) | each: the fourth `tol.loc.hold`, `CyclesRefused` and `Holds` +1, the mouse removed; released by CLEAR and a replug | the same; 0A's hold released by a controller disable and enable instead, every device started | pass |
| | 0D P | a stale read per injection, no cycle | about 50 s: 118,909 injections, `HaltStale` 118,908; no cycle, no hold; the mouse moves after CLEAR | not run | pass |
| EP0 | 0F | a cycle from the thread's own transfer | armed, then a replug: `tol.cycle.wait`, the cycle, re-enumerated | armed, then the mouse disabled and enabled (the enable answered "reboot needed"; none was): `CyclesRefusedCode` 3, the mouse back | pass |
| | 10 | a cycle before the PDO | `CyclesPrePdo` 1 | the same | pass |
| | 0F P, 10 P | three cycles, then held | held after three; 10: `CyclesPrePdo` 4 | the same; 10: `CyclesPrePdo` 3 | pass |
| At 0 | 08 once | today's path, counted | `QueueErrors` 2, `QueueHalts` 2, Transaction Error 1 in the histogram, `Retry*` 0 | the same | pass |
| | 0A once | counted, no cycle | `QueueBadCodes` 1, Bandwidth Overrun 1 in the histogram, `Cycles` 0, no charge | the same | pass |

Back at 1 after each 0 leg the start was clean; on the 2000 guest Verifier
read `AllocationsFailed` 0, no bugcheck.

**The re-run on the final code** (`efc6fe1`; the 2000 guest updated with
Have Disk to `2.2.0.0`, `XhciTolerance` 1, Verifier `0x1B`; 98 SE with the
value absent): the legs the fixes below touch.

| Leg | Guest | Observed | Counters | Result |
|---|---|---|---|---|
| 05 P | 2000 SMP | recoveries 1 to 3, the fourth refused, `ctrl.terminal.release` 1; every USB device gone; `dir f:` fails at once; a controller disable completes (problem 22); the enable starts everything, `fc` clean, 0 nonzero, the mouse moves | `HchRecoveries` 4, `WindowRefused` 1 | pass |
| 05 P, the stick replugged and its volume mounted before the fourth | 2000 SMP | the same; reads and copies to F: fail at once; disable and enable complete | the same | pass |
| 05 P, a copy in flight at the fourth | 98 SE | refused, terminal release, LATCHED FAILED; the read failed promptly (Error Reading Disk, General failure, Fail); `dir F:` an invalid drive; the mouse dead; a Device Manager disable completed, no restart asked; the enable brought both devices back, `fc` clean, 0 nonzero; clean shutdowns | the same | pass |
| 02 T | both | `tol.port.ped` 6, port 6 charged 1, the mouse moves | `PedFaults` 1, `CyclesPed` 1 | pass |
| 02 P | both | `tol.loc.hold` `00000601`, "NOW HELD (powered)", the mouse dead; disable and enable: the mouse back, 0 nonzero | `PedFaults` 4, `CyclesPed` 3, `Holds` 1 | pass |
| 02 at 0 | both | counted only, the mouse moves; back at 1, 0 nonzero | `PedFaults` 1 | pass |
| `release` regression | 98 SE | the SuperSpeed stick `fc` clean; a `usb-hub` with a stick behind it (Full Speed) `fc` clean; the replug `fc` clean; tolerance on, running, window 0 of 3, containment none, 0 nonzero | 0 | pass |

Verifier at the end: `0x1B`, `AllocationsFailed` 0, `xhci98.sys` loaded 8
times and unloaded 7; no bugcheck; both guests shut down clean.

**Not made or not read on QEMU.**

- A SuperSpeed device behind a SuperSpeed hub: QEMU has no SuperSpeed hub
  model (35.4); 35.2 read it on the E460.
- A fresh install of the three values on Windows 98 SE: not practical on
  the gold image, so the update path was read on an overlay that never
  had them. On Windows 2000 the Have Disk install created a new driver key
  and wrote all three, which is the fresh-key case.
- A user's 0 kept by an update was read on Windows 2000 only.
- Record 17 section 4.11's restart from 1 to 0 and back with a deferred
  retry, a pending cycle and held locations all outstanding was not run as
  one sequence; its parts were read apart (holds released by a start;
  0 and back to 1 each starting clean).
- Code 02 at `ed73d0a` and `6808ff9`: QEMU 11.1's `xhci_port_write` acts on
  PR, WPR, the change bits, LWS and PLS, and PP, WCE, WDE and WOE, and
  ignores a PED write, so the port stayed enabled and the driver saw PEC
  with PED set, which is no PED fault. Read at `efc6fe1` with `cabedf5`.

**Found by the legs, and fixed before the cut.**

- **A terminal no recovery acts on held its transfers for ever**
  (`882245d`). After the window refused a fourth recovery (persistent 05,
  and persistent 09 through its failed Reset Endpoint), the controller was
  latched failed and nothing completed the transfers on its rings: the
  event drain and the commands refuse a failed controller, and only a
  recovery's invalidation completed them. On 98 SE a read of the stick
  never completed and froze the system VM until its MS-DOS task was ended,
  and the shutdown hung at "Windows is shutting down" for more than 7
  minutes; on 2000 the devices stayed started, a new copy hung, and a
  controller disable hung for more than 7 minutes with the class drivers'
  query-remove waiting. `hcdTerminalRelease` now raises that invalidation
  itself, once per lifetime, at either terminal, on HCH's proof; record 17
  revision 11, section 4.6. Its review: round 1 taken in `4dcc9c2` (no Save
  or Restore State on a latched-failed controller, since CSS writes
  contexts while halted), `d979d51` (a resume refuses the spent run of
  failures too, a change from `2.1.1.0` at 0) and `f1dcd83` (the save gate
  read the frozen miniport's device table, which the HCD never fills, and
  so passed with transfers queued; it now asks the HCD's own queues,
  `XhciSlotSaveBusy`; pre-existing); round 2 taken in `efc6fe1` (a transfer
  still being mapped passed the gate; the gate now counts a transfer from
  submission to completion, and a save a late mapping publishes into is
  spoiled and the resume reinitializes). Re-read at `efc6fe1`: passes, the
  table above.
- **Test aid: code 02 did nothing on QEMU** (`cabedf5`). The layer now
  answers PED clear in that port's PORTSC reads beside the emulated PEC,
  until the driver's port reset or the device's departure. Re-read at
  `efc6fe1`: passes.
- **Test aid: a long 08 P stranded the mouse** (`6c39877`). Once on each
  guest (98 SE at trace line 121484, during the copy; 2000 at line 9459,
  after it), every later injection abandoned with `qemu.inj.abandon`
  `00020305` and the mouse's interrupt TD never completed again until a
  replug; a second 98 SE run did not reproduce it. QEMU leaves its dequeue
  on the Link TRB after fetching a 64-TRB ring's last usable slot, which
  the layer's stop verdict did not take as a fetch-ahead: one chance in 63
  per TD injected. The `qemu` flavour alone; host vectors in `test_inj`,
  not re-read in a guest.

**Known limitations observed, not fixed.**

- **Disabling a controller the driver closed off without proof hangs.**
  After 07 P ("PINNED"), a controller disable hung on both guests: on 2000
  at a device PDO's REMOVE_DEVICE, on 98 SE after QUERY_STOP. The pinned
  branch keeps the transfers and mappings because DMA was not proven
  stopped; only a restart of the machine ends it. Record 17 section 4.6's
  known limitation, now observed; the release documents say so.
- **Windows 2000: a mounted USB drive can come back at Code 31 after an
  in-place controller recovery** (an HCH, a failed Reset Endpoint): 5 of 5
  on the SMP guest at `efc6fe1` with the volume mounted at the fault; with
  nothing mounted the next recovery brought it back started (2 of 2). The
  mouse always returned; 98 SE never showed it; a replug, the next
  recovery or a controller disable and enable clears it. The driver
  re-presents the device as a new PDO before the old devnode's REMOVE has
  arrived, and the function driver's add fails (`CM_PROB_FAILED_ADD`). The
  same path is in `2.1.1.0` (`hcdInvalidate`, `hcdDropAll`,
  `HcdDevicePdoExists`), so not a regression, though `2.2.0.0` reaches a
  recovery on more faults. Not fixed: delaying the re-presentation until
  the old REMOVE contradicts the owner's ruling of 2026-10-04 cited in
  `hcdPortQuiet`. Left to the owner: an NT-only delay for a device whose
  previous PDO's REMOVE just arrived, or keeping the PDOs across a
  recovery.
- **A copy in flight across an in-place controller recovery fails**, since
  every device is re-enumerated (`2.1.1.0`'s recovery). By design.

**Vehicle notes.**

- Windows 98 SE gold image: with QEMU's `usb-mouse` attached at boot every
  MS-DOS prompt failed ("must be run in MS-DOS mode") or hung, on this
  build and on 35.4's alike; so the devices were hot-added after a prompt
  was open.
- QEMU's mouse stalls hidusb's CLEAR_FEATURE(ENDPOINT_HALT) on EP0: one
  EP0 Stall in the histogram per exhausted soft-retry episode.
- The 2000 SMP guest's interface is slow, as on `2.1.1.0`; `XHCISNAP`
  needs `-c 1` there, since the guest's EHCI controller's `usbport.sys`
  takes the first device name, and refuses `-c 1` with `-verbosity 2`
  together, so the verbosity was set in the registry.

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

Cut 2026-10-07 (owner's date): `xhci_version.h` and the four INFs'
`DriverVer` at `10/07/2026,2.2.0.0` (`355199f`), the `history.md` entry
(`92fa6ca`), and `releases\2.2.0.0` by `make-release.ps1` from `fcaa47a`
(`e192335`); the cut first refused two release numbers spelled out in the
readme template's new limitation lines, reworded in `fcaa47a`. Built
before it: x86 debug, release and qemu and amd64 release and debug, every
gate green. Asset `xhci98-2.2.0.0.zip`, 761,822 bytes, SHA-256
`3fa85b75ba234fc6a544c4e7a1e79cbba88499282f373ea348c94e7904d7fe43`, every
file dated 2026-10-07 12:00; `release-x86\xhci98.sys` SHA-256
`98f1fb1d...4922b88a`, `release-x64\xhci98.sys` `07bec20c...6643aeee`.

**The ten install legs from the asset** (development host A, QEMU 11.1.0,
fresh overlays, run by four agents in parallel, 07:48 to 09:50; reports and
shots in the git-ignored `out\phase35\asset\`), on 34.5's clauses: 32.3's
1 to 9, the hub disable and enable on the seven NT legs, `hidusbf` behind
the hub on XP, and the controller's driver key read after the install.
All ten passed, with no driver defect: Windows 98 SE with NUSB and with
SweetLow's stack, Windows ME, Windows 2000 SP4, XP SP3, XP x64, Vista x86
and x64, and Windows 7 x86 and x64. On every leg the install wrote
`XhciTolerance` 1, `XhciIntervalCap` 1 and `XhciAvgTrbEsit` 0 beside the
other eight values; every installed driver file compared equal to the
asset's; every debugcon log was empty. The Driver tab read 2.2.0.0 dated
10-7-2026 on Windows 98 SE (34.5's file dating holding), 10/07/2026 on ME,
07-Oct-26 on XP and XP x64, 7/10/2026 (day first) on Vista and 7, and "Not
available" on 2000 as always. Vehicle notes, as on `2.1.1.0`: the Program
Compatibility Assistant's box after each unsigned install on Vista x64 and
7 x64; XP x64 restarted once when a step script's Enter answered Windows'
restart prompt for a newly installed volume (setupapi "required reboot:
Device not started"), the driver's devices staying at no problem; Vista's
hub disable passed on its first cycle with Explorer's window on the hub's
drive closed first; on Windows 98 SE with SweetLow's stack the mouse moved
again some 100 s after a controller re-enable, slow under TCG.

**Re-cut** (2026-10-07, before publication, readme only, as 34.5's): the
owner asked that the update steps name the root hub too - Update Driver on
"xHCI98 USB 3.x Root Hub" after the controller - in the README, the release
notes and the readme template; `make-release.ps1 -Force` regenerated
`readme.txt` alone (`ab0f870`), every binary and INF unchanged, so the ten
legs stand. A second readme-only re-cut the same day (`dff56a8`) took out the
limitation saying USB storage on Windows 98 is slower than the drive
(owner: not something the driver can change). Asset now 761,688 bytes,
SHA-256 `c4c9cd65ccc834e2d703d3a11469c33046d552f3afaf51d373307151185c1258`.

## 35-E - the bench

Read by the owner on the `2.2.0.0` asset's `release-x86` package after the
cut (decisions table, "`2.2.0.0`: cut before the bench"). So far
(2026-10-07, the ThinkPad E460 under Windows 98 SE; `XHCISNAP` dumps
git-ignored in `temp\e460-2200\`): the USB 3 hub (`05E3:0612`) at root port
13 with a UAS-capable stick (`090C:2320`) behind its SuperSpeed half, the
hub's USB 2.0 half at port 1, and a second stick (`0781:55AB`) at port 15 -
every SuperSpeed device read speed ID 4 as SuperSpeed by 35.1's fallback,
enumerated at the first attempt and configured at PSIV 4; file copies, a
mouse and audio working (owner); the tolerance block running, the window 0
of 3, no location charged, the only fault counters one Stall on a stick's
bulk-IN endpoint, an ordinary mass-storage stall a pipe reset clears; then
5 cold boots with the hub and both sticks attached, each coming up with the
hub's SuperSpeed half present, the stick behind it and the stick at the
root port both at SuperSpeed (owner). Port
2, the USB 2.0 half of port 14's connector, read Failed on its reset after
a stick was pulled from that connector - a connect no longer than the pins'
contact, left until that connector's next connect. Then 5 hot-plugs, the
SuperSpeed devices found again each time (owner). The owner set the E460's
campaign at those 5 cold boots and 5 hot-plugs (2026-10-07, in place of
the roadmap's 10 and 10): the E460's clauses passed. The P14s
Gen 1, the B490 and value 2 on the B490 are the owner's still.

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
  branch: its fixes `a113df2` to `2000fcc`, converged at round 4. 35-V's
  terminal release (`882245d`): round 1 taken in `4dcc9c2`, `d979d51` and
  `f1dcd83`, round 2 in `efc6fe1`; the test aids `cabedf5` and `6c39877`.
  What these
  reviews found in code already in the branch, and its fixes, is "Review
  findings on code already in the branch".
- The re-plan and issue 11: round 1 taken in `fadd358`.
- 35.6: to be read.
