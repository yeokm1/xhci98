# 06 - The Device Matrix Verdict

Phase 10, task 10.2. What a device row in the automated matrix must state, and
how the harness decides pass or fail from it. Written before the runner,
because the runner is easy and the verdict is not.

Companion documents: `docs/contributing/design/03-host-unit-tests.md` (what
belongs in a host vector instead of a boot), `docs/contributing/build-and-test.md`
("Reading counters out of a live guest"), and the task 10.1 population table
this design consumes.

## 1. The problem this document exists to solve

A run that ends "no bugcheck" proves very little. This repository has
repeatedly measured counters that read zero by construction and then had to
retract the reading:

- `DrainBoundHits` = 0 was reported as a result until it was shown to need
  1,024 events in a single DPC pass against 4,222 completions for the whole
  leg: structurally unreachable, not a passing negative (task 9-0.2).
- `topology: behind-hub refused - too deep` = 0 is a property of QEMU refusing
  the sixth chained hub itself, not of the driver's ceiling (batch 7b-V).
- `EndpointStoppedEvents` = 0 was read as "this driver never issued Stop
  Endpoint" across three device classes. It counts the xHC's reply, and the
  driver had been issuing them all along (batch 8-V.1 on 2b).
- `MidTdTailsDroppedTotal` was structurally incapable of moving after task
  9-0.2 relocated its recording site, and the zero it produced was the zero the
  change had hoped for. An identity caught it; no counter could have.

So the rule this design is built on: a number is not a verdict. A number is
only a verdict once something has said, in advance, what it was supposed to
do.

Every row in the matrix therefore carries its expectations in the committed
matrix file, written before the run, and the harness's job is to compare, not
to summarise.

## 2. The five outcomes, and why "pass" is only one of them

A device row resolves to exactly one of these. Four of them are results; only
`ERROR` is the harness admitting it does not know.

| Outcome | Meaning |
|---|---|
| `PASS` | Every expectation held. |
| `FAIL` | An expectation did not hold. The report names which, with both readings. |
| `NODRIVER` | The miniport enumerated the device and no function driver bound on this target. This is a first-class result, not a silence. |
| `INERT` | Every expectation this row could have is structurally zero on this vehicle. Reported as such, and it can never be `PASS`. |
| `ERROR` | The harness could not take the reading: the guest was not alive, offsets were stale, the monitor did not answer, the device never attached. |

`NODRIVER` and `INERT` are the two the checkpoint names explicitly, because
they are the two a naive harness reports as `PASS`. A `usb-braille` on Windows
98 moves no transfer counters at all; so does a driver that has silently
stopped working.

### 2.1 How `NODRIVER` is decided without a guest agent

The discriminator is whose job stopped:

- Enumeration is ours. `slots enabled` and `devices addressed` advance because
  this driver answered the port change, addressed the device and opened EP0.
  They move whether or not anything above usbport wants the device.
- Binding is the OS's. A non-EP0 endpoint open cannot happen until a function
  driver has selected a configuration.

The counter that says so is `endpoints opened`, and the choice is derived
rather than picked by name: `EndpointsOpened` is incremented at exactly one
site, inside `xhciSlotOpenNonDefault`, so it counts non-default endpoints and
nothing else. `endpoint opens accepted` is the wrong counter for this. It
counts every accepted `OpenEndpoint`, EP0 included, so it advances on a device
no function driver ever touches and would report `PASS` for every `NODRIVER`
row.

So:

```
devices addressed advanced  AND  endpoints opened did not   ->  NODRIVER
devices addressed did NOT advance                           ->  FAIL (ours)
```

One caveat, stated because it is not visible from the counter name.
`endpoints configured` is not an equivalent signal: task 7b-A.2 marks a hub
with a standalone A0-only Configure Endpoint, so that counter moves for a hub
on the topology path with no function-driver involvement at all. It is
`endpoints opened` that carries the claim, and the hub rows say so.

That distinction is the one that matters for this project. Windows 98 with
NUSB and Windows 2000 SP4 ship different class-driver sets, and a device the
OS ignores says nothing about the miniport. It is also the column task 10.1
could not fill from the emulator, and this is how the run fills it.

This inference has a stated limit. It says a function driver opened a pipe; it
does not say the device works for its user. A mass-storage device whose disk
never gets a drive letter still opens its bulk pipes (task 8-V.1 measured
that, and the cause was `removable=on`). Rows that care about the higher claim
must express it as traffic, not as a bind.

## 3. What an expectation can be

Five kinds. Every one names a counter by the exact label the driver prints, so
a renamed counter fails loudly rather than silently matching nothing.

| Kind | Written as | Holds when |
|---|---|---|
| `advance` | `advance <label>` | delta > 0 across the row's window |
| `advance-by` | `advance <label> >= N` | delta >= N |
| `zero` | `zero <label>` | delta == 0 |
| `identity` | `identity <expr>` | an arithmetic relation over deltas holds exactly |
| `inert` | `inert <label> because <reason>` | delta == 0, and the reason is printed |

`inert` is the one that carries the design. It is the difference between
"this counter did not move" and "this counter could not have moved, and here
is why". A row whose every expectation is `inert` resolves to `INERT`, never
to `PASS`, because there is nothing in it that a broken driver would have
failed.

`zero` and `inert` are not the same assertion and must not be interchanged.
`zero` says a failure-shaped counter stayed still on a path that could have
moved it, which is evidence. `inert` says the path does not exist here, which
is bookkeeping. Writing `zero` where `inert` is meant is how a vacuous check
gets counted as coverage.

### 3.1 Deltas, not absolutes

Every counter in the driver is cumulative since the extension was last zeroed,
and usbport `RtlZeroMemory`s the miniport extension before every
`StartController` (Phase 4 task 7). So every expectation is over a delta
across the row's own window: read, attach, drive, read, detach.

This also removes the `XHCI_DBG_VALUE_CHANGED` print budget from the critical
path. That macro stops after 32 samples per site for the life of the driver
load and nothing resets it, so a long unattended run goes blind if the trace is
the channel. The harness reads the extension itself through the monitor, by
offset, the mechanism `scripts/local/readcounters.ps1` established and this
harness promotes into `scripts/`. The debug console log stays as corroboration
and as the place a bugcheck or a `cb` line shows up, never as the counter
source.

### 3.2 The identities are worth more than the counters

Task 9-0.2's finding was that a counter which cannot move produces the zero you
hoped for, and only an identity noticed. The matrix therefore carries a small
set of identities that must hold for every row, independent of the device:

```
identity  transfers submitted == transfers completed + transfers cancelled

identity  endpoint opens seen == endpoint opens accepted
                              + EP0 opens refused
                              + endpoint opens refused - unusable buffer
                              + endpoint opens refused - malformed call
                              + endpoint refusals - type
                              + endpoint refusals - no device
                              + endpoint refusals - not ready
                              + endpoint refusals - params
                              + endpoint refusals - ring pool
```

The open identity is nine terms, and it is transcribed from `src/xhci.h`'s
own statement of it rather than assembled from the counter labels. Assembling
it from the label list, by picking names that look like refusals, is the
mistake `OpensTotal` was introduced to catch: task 7b-A.1.0 added it because a
row set assembled path-by-path is only as complete as whoever assembled it,
and assembling it again from the outside would re-open the gap that counter
closes. Note also that `EP0 opens refused - no route` and its sibling
`OpenRefusalsNoClaim` are shares of `EP0 opens refused` and are absent from
the sum on purpose; adding them would double-count.

The transfer identity has a stated evaluation point and it is not slack. A
transfer still in flight at the moment of the read is neither completed nor
cancelled, so it is evaluated after the device is detached and the endpoint
has been torn down, which is the only moment it is exactly true. A
submitted/completed gap read during traffic is not a leak (batch 8-V.2
recorded that in those words) and the harness must not report one.

The two identities do not have the same standing, and conflating them would
be this document repeating the mistake it was written to prevent.

- The open identity is stated by the driver, in `src/xhci.h`, and enforced on
  the host by `note_open_accounting`. The matrix asserts it because it is an
  invariant.
- The transfer identity is not stated anywhere in the driver. It is an
  inference from readings taken on particular runs (`4,614 submitted = 4,613
  completed + 1 cancelled` on the 2b Ethernet leg, `369 = 369` on 2a's read
  clause), and `TransfersRefused` exists as a separate counter, so a transfer
  offered to the miniport and declined is submitted-to-us and placed nowhere.
  Whether the three-term form is exact in general has never been established.

So the transfer identity is a row-level, opt-in expectation and is not in the
`Always` set. On the rows that carry it, the matrix is testing it rather than
assuming it, and the first full run is what says whether it holds across the
whole device population. If it does, it can be promoted to `Always` and the
promotion recorded; if it does not, the row that broke it names the missing
term. Putting it in `Always` today would have made every future violation look
like a device failure instead of an incomplete partition, which is how
`MidTdTailsDroppedTotal` hid.

## 4. What the harness must refuse to do

Four traps this project has already paid for, each enforced in code rather than
written in a comment. They live in `scripts/vm-matrix/lib/qemu.ps1`.

1. Two `-trace` arguments. QEMU keeps the last one, so
   `-trace events=X -trace file=Y` silently discards the event list and the log
   reads as "the driver did nothing". `Assert-SingleTraceArg` throws.
2. `screendump foo.png` writes a PPM. The extension is not honoured.
   `Get-ScreendumpPath` renames to `.ppm` so a later reader is never handed a
   file that is not what it claims.
3. A stale `offsets.txt`. The counter reader walks the extension by byte
   offset. If the driver has been rebuilt since those offsets were generated,
   every read is off by however much the layout moved, and that surfaces as a
   wrong value, never as an error. `Assert-OffsetsFresh` compares the file's
   `SIZEOF` against the `MiniPortExtensionSize` the running driver prints, and
   voids the run if they differ. `offsets.txt` was found stale across a whole
   batch once already (batch 9-0).
4. A healthy trace is not a living guest. Batch 7b-V0 measured a guest whose
   clock had stopped while its trace kept growing. `Test-GuestAlive` asks the
   monitor two questions instead: `info status` for whether the VM is running
   at all, and `info irq` twice, `SampleMs` apart, for whether the interrupt
   totals are still moving. A guest that is running but taking no interrupts
   is the 7b-V0 shape and is reported as its own verdict, distinct from a
   paused VM and from a monitor that has gone away. The RTC is not consulted:
   `info rtc` does not exist in QEMU 11, which is the version this harness
   runs. The third state, no parseable `info irq` lines at all, is reported as
   unknown rather than as a dead guest, because a machine type with no PIC is
   not a hang.

A fifth, from batch 8-V.1, is a property of the stage rather than of QEMU and
belongs in every row that unplugs: a pull is only a pull if the device left.
`unplug-8v.ps1` printed a confident `FIRED` while `info usb` still listed the
device. Every `device_del` in this harness is followed by a wait on `info usb`,
and a device that is still there is an `ERROR`, not a completed stage.

## 5. The window, and why each row gets its own boot group

A row's window is: read counters, `device_add`, wait for enumeration, drive the
device, read counters, `device_del`, wait for the bus to empty, read again.

Rows are grouped and each group gets a boot. Three reasons, all measured:

- Windows 98 idle-suspends the controller about half a second after the last
  transfer, and an attach onto a halted controller is invisible to the whole
  stack (batch 7a-V). Every Win98 group therefore keeps a USB pointer device
  on a root port and pumps `mouse_move` through it.

  Batch 7b-V measured a second-order effect, that a hub tree stops Win98
  idle-suspending, and it is tempting to conclude that a group containing a
  hub does not need the pump. Measured, that is wrong, and the hub group runs
  with `Pump = $true` (`scripts/vm-matrix/matrix.psd1`): the hub only holds
  the controller awake once it is attached, and the attach is the very step
  that needs a live controller.

  With the pump off, `usb-hub/fs` read `devices addressed +0` with
  nothing enumerating at all, on a target where the hub had just been
  installed by hand. A precondition that the step itself establishes cannot be
  assumed before the step.
- A device that wedges the guest ends its group, not the run. Batch 7b-V0
  killed a guest outright with an unbounded refusal; run 5 of batch 9-V wedged
  the PnP tree. A group boundary is the blast radius.
- State does not reset within a boot. A device that leaves a slot retained,
  or an address recycled, changes what the next row measures. Grouping keeps
  the interference bounded and named.

## 6. The report

One line per expectation, ordered deterministically, with no timestamps,
durations or paths in the body, so a regression is a changed line in a diff
rather than a re-read. Everything variable (host, QEMU build, image hashes,
wall-clock) goes in a header block that a diff can be told to skip.

```
TARGET  ROW                  OUTCOME    EXPECTATION                                    READING
2a      usb-kbd/hs           PASS       advance devices addressed                      +1
2a      usb-kbd/hs           PASS       advance endpoints opened >= 1                  +1
2a      usb-kbd/hs           PASS       zero transfer events for no open endpoint      0
2a      usb-kbd/hs           PASS       identity submitted == completed + cancelled    412 == 412 + 0
2a      usb-braille/fs       NODRIVER   advance endpoints opened >= 1                  +0  (addressed, never claimed)
2a      usb-audio/fs         NODRIVER   inert iso packets answered                     0   (USBAUDIO.VXD faults after one URB - batch 9-V)
```

The row name is `model/variant`, from the task 10.1 table, so the report and
the population table share a key.

The `usb-audio` line is `NODRIVER` and not `INERT`, and both halves of that
are about this document's own distinctions:

- The row is `NODRIVER` by the rule in section 2.1. Measured three times on
  the prepared 2a image: `devices addressed` +1 and `slots enabled` +1, so our
  job finished; what does not happen is the bind, because Windows 98 raises a
  modal Add New Hardware Wizard for `USB Composite Device`. Enumeration is
  ours and the bind is the OS's, so this is the OS declining, not a
  structurally absent path. (Once the class is taught, `USBAUDIO.VXD` faults
  as batch 9-V recorded, at `+00002ED4`, after one URB of ten packets that
  this driver answered in full. The row's `inert` reason is therefore about
  what an unattended run can present, not about the counter being unreachable
  in principle.)
- No row can resolve `INERT` at all, by construction. Every row inherits
  `matrix.psd1`'s `Always` block, which is live `advance`, `zero` and
  `identity` expectations, so "every expectation is inert" is unreachable
  while `Always` exists, and `Always` is what makes `NODRIVER` separable from
  `FAIL`. Neither whole-matrix report contains a single `INERT` row.

So how is the checkpoint's "expected counters are structurally zero ... as
explicit results rather than as silence" met? At expectation level, not at
row level. An `inert` line appears in the report with its counter, its
reading and its reason spelled out, and it can fail (an inert counter that
moved is a `FAIL`, per section 3). That is the substance the clause asks for:
the reading is stated with its justification instead of being dropped. The
row-level `INERT` outcome remains defined, unit-tested in `selftest.ps1`, and
unreached by any real row in this population. Manufacturing one by letting a
row opt out of `Always` was considered and rejected: it would trade a
reporting nicety for the mechanism that stops a broken driver hiding behind
"nothing was expected here".

## 7. What this design does not do

- It does not judge audio quality, throughput or latency. Batch 9-V
  established that the traced build's own port-0xE9 output is what stutters
  an isochronous stream on this vehicle, so a harness that traces cannot also
  be the judge of smoothness. (The traced build is the `qemu` flavour, which
  is the one this harness runs.) Those clauses stay hand-run with a capture as
  the oracle.
- It does not cover Low Speed. Task 10.1 measured that no Low Speed model
  exists in this QEMU build. That is a property of the vehicle; the clause is
  bare metal's.
- It does not cover `usb-host` passthrough. Task 9-V.2 measured that rung
  shut for IAD-grouped multi-interface functions upstream of any guest, and the
  general passthrough case needs host hardware this harness cannot assume.
- It does not replace a hand-run. Task 10.4 requires the harness to reproduce
  results that were measured by hand, and a harness that has never disagreed
  with a hand-run is untested rather than correct.
