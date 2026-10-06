# The Intel EHCI-to-xHCI port switchover

Design record for roadmap-hcd task 34.3 (Phase 34, release `2.1.1.0`).
Revision 2, 2026-10-05. Revision 1 was written at the owner's request
after the code, to explain it, when nothing in it had run on a machine with
the mux; built in `0def9a3`, `ea707a6` and the commit that added this
record. Revision 2 adds the readings (section 10): the owner's Lenovo B490
(`8086:1E31`) under Windows 98 SE with NUSB, and the gate closed on the
P14s Gen 1. Statements about the hardware beyond what that machine showed
are still taken from Linux and from
`docs/usb-xhci-info/xhci-programming.md`'s `XUSB2PR` section. (The task was
34.4 until Phase 34 was resequenced; commits before `8a8514d` use that
number.)

Revision 3, 2026-10-06, for roadmap-hcd task 35.5 (Phase 35, release
`2.1.2.0`), written before its code: `XhciIntelPortSwitch` 2, the
switchover on any Intel controller past the gate, at the user's own risk
(sections 4a and 5); and, at every value, a release that writes only the
registers a route wrote (sections 6 to 8). Its decisions are section 11's
second list; what it changes in the documents is section 13.

## 1. What is asked, and what is not

A tester reported Ivy Bridge machines on which `xhci98.sys` started cleanly
and then saw nothing on the blue ports, while a SuperSpeed stick ran at High
Speed under the USB 2.0 controller. On the Intel 7-, 8- and 9-series PCH and
on C610/X99, each switchable connector is wired to two controllers, EHCI and
xHCI, and four registers choose which one has it. Firmware that hands the
choice to the operating system - typically a setting of "Auto", and
apparently the tester's machines, which offered none - leaves the switchable
connectors on EHCI and expects the OS's xHCI driver to move them; Windows 7
does it through Intel's own USB 3.0 package, and Linux at probe. Which
firmware does this, and with what initial values, is a per-machine fact
(`xhci-programming.md` gives it as a prediction). The HCD now moves them too.

Not in this design: Panther Point's 64-active-endpoint limit (Linux
`XHCI_EP_LIMIT_QUIRK`), which the same machines carry; any other vendor's
routing; and Linux's other quirks for these parts (`XHCI_SPURIOUS_WAKEUP`'s
controller reset at shutdown, among them).

## 2. The hardware

Four dwords in the **xHCI function's own** PCI configuration space, above
the standard header (offsets and meanings: `xhci-programming.md`, which
cites Linux `pci-quirks.c` and the Intel 7-series PCH datasheet volume 2;
that datasheet has not been read here):

| Offset | Register | Meaning |
|---|---|---|
| 0xD0 | `XUSB2PR` | bit n = 1: switchable USB 2.0 port n routed to xHCI; 0: to EHCI |
| 0xD4 | `XUSB2PRM` | which USB 2.0 ports firmware lets the OS route |
| 0xD8 | `USB3_PSSEN` | bit n = 1: SuperSpeed terminations of port n enabled |
| 0xDC | `USB3PRM` | which SuperSpeed terminations firmware lets the OS enable |

The USB 2.0 pairs and the SuperSpeed terminations are switched separately.
Linux's comment (`pci-quirks.c`, above `usb_enable_intel_xhci_ports`) gives
the order: the terminations first, so that a SuperSpeed device connects at
SuperSpeed when its USB 2.0 pair arrives, rather than at USB 2.0 speed.

Moving a connector disconnects whatever was attached to it on the other
controller. Linux routes from an early PCI quirk, before any USB driver has
started, so that nothing is attached under EHCI yet. A Windows driver cannot
run that early: on Windows 2000 and later the stock EHCI driver may already
have enumerated a device on a switchable port when this driver starts, and
on Windows 98 SE so may NUSB's. Section 6 says what that costs.

## 3. Why the miniport left it alone, and why that no longer holds

At the end of Phase 4 the miniport decided not to read or write any of the
four (`xhci-programming.md`, "`XUSB2PR` - the Intel 7/8-series port mux this
driver leaves alone"; `roadmap.md`, Phase 4). Two reasons:

- **The miniport drove no SuperSpeed port.** Its USB 3.x logical ports stayed
  unpowered, so disabled SuperSpeed terminations were what made a
  SuperSpeed-capable device fall back to its USB 2.0 pair.
- **EHCI was the Windows 98 file-transfer safety net.** The miniport sat on
  `usbport.sys` and had no mass storage of its own; on a machine with both
  controllers, moving the connectors could take away the only working path.

Neither holds for the HCD. It drives SuperSpeed ports since `2.0.0.0`, so
with the terminations off a SuperSpeed stick runs at High Speed somewhere
else rather than at SuperSpeed here. And mass storage runs on it directly
(Bulk-Only and UAS), so EHCI is no longer the only path. The owner superseded
the Phase 4 decision with this task (roadmap-hcd decisions table, "The Intel
port switchover", 2026-10-05).

## 4. Which controllers: the gate

The registers exist only where an EHCI shares the connectors. Three rules
were weighed (owner, 2026-10-05):

1. **A device-id list (taken).** Intel `8086` and one of six device ids:

   | Id | PCH |
   |---|---|
   | 1E31 | Panther Point, 7-series |
   | 8C31 | Lynx Point, 8-series |
   | 9C31 | Lynx Point-LP, 8-series mobile |
   | 8CB1 | Wildcat Point, 9-series |
   | 9CB1 | Wildcat Point-LP, 9-series mobile (Broadwell-U) |
   | 8D31 | Wellsburg, C610/X99 |

   The first five were proposed with the task. 8D31 was added the same day,
   because `xhciqual`'s `quirks.c` already marks it `QF_XUSB2PR`. The
   evidence is not even across the list. The local Linux mirror defines
   8C31, 9C31 and 9CB1 by number (`xhci-pci.c`) and uses Panther Point's id
   by name; the register meanings are the 7-series datasheet's as
   `xhci-programming.md` gives them. For 8CB1 and 8D31 the support is only
   that Linux applies the same sequence to every Intel xHCI with an EHCI
   beside it and that `xhciqual` reads both as such parts. No datasheet or
   reading specific to Wildcat Point or Wellsburg is held. It is the driver's
   first device-id row, against `xhci-programming.md`'s "no per-controller
   quirk table"; that is accepted for this one mechanism.
2. **Linux's rule (not taken).** Linux tries every Intel xHCI, but first scans
   the PCI bus for an Intel EHCI and returns if there is none. That reaches
   parts no list names (some Atom generations may be among them), but it
   needs a bus scan the HCD does not have, a new import with Windows 98
   evidence, and readings on both primary targets.
3. **Every Intel xHCI (refused).** From the 100-series (Skylake) on there is
   no EHCI, and what offsets 0xD0 to 0xDC do on those parts is unread here.
   Without Linux's EHCI check, this would write them on every modern Intel
   machine, the project's own E460 (9D2F) and P14s Gen 1 (02ED) among them.

One board is exempt: Linux skips a Sony VAIO T-series, subsystem
`104D:90A8`, which "is not capable of switching ports from EHCI to xHCI".
The HCD reads the subsystem dword (offset 0x2C) and skips that board. **A
subsystem id that cannot be read also skips the switchover**: an unread id
is no evidence that the board is not that one.

On every other controller (QEMU's `1B36:000D` among them) the switchover
reads PCI offset 0 and nothing else, and writes nothing; the start's own
configuration reads (the command register, the interrupt pin) are not this
design's and are unchanged.

## 4a. Past the gate: value 2 (revision 3)

The list will miss switchable parts: Linux's rule reaches Intel xHCIs it
does not name, and a tester with one has no way in short of a new release.
Value 2 of section 5 is that way in (owner, 2026-10-06): on any controller
whose vendor is Intel `8086`, the list is bypassed and the start routes as
on a listed one. It bypasses the device id only. Under 2 the switchover still
does nothing on a non-Intel controller, on a PCI offset 0 that could not be
read, on the Sony board, or on a subsystem id that could not be read; the
sequence, the masks' all-ones refusal, the moments of section 6 and the
release are the same.

**Value 2 is at the user's own risk, and the driver does not guard it.** On
a controller whose configuration offsets `D0h` to `DCh` are not the switch
registers - every Intel xHCI without an EHCI beside it, from the 100-series
on (the E460's `9D2F` and the P14s Gen 1's `02ED` among them), or any part
whose layout nobody has read - the driver writes registers of unknown
meaning: at each start, at each return to D0, and zero at each stop, refused
start and shutdown. What those writes do there is unknown; it may be nothing,
or it may disturb the controller or the machine. A review of the plan
proposed a register-layout check (an EHCI-style mask pattern, say) that
would refuse such a controller; the owner chose to inform rather than guard
(2026-10-06), so none is added, and the all-ones refusal (section 7) is the
only check, as at 1. The README and the release notes say so in these words:

> Setting `XhciIntelPortSwitch` to 2 makes the driver read four Intel
> chipset registers and write two of them on any Intel USB 3 controller,
> not only on the 7-, 8- and 9-series and C610/X99 chipsets it was written
> for. On a controller that does not have those registers - every Intel
> chipset from the 100-series (Skylake) on, and any other whose layout has
> not been read - it writes registers of unknown meaning, at every start,
> resume, stop and shutdown, and the result is unknown. Use 2 only for an Intel chipset that
> has both an EHCI and an xHCI controller and that this driver does not
> list, and at your own risk. Everyone else should leave the value at 1.

## 5. The switch: `XhciIntelPortSwitch`

A REG_DWORD in the controller's driver key (owner, 2026-10-05):

| Value | Effect |
|---|---|
| absent, or not a DWORD | on, on a listed controller: the default |
| 0 | off: this start reads PCI offset 0 and the value, and makes no switchover write, as every release to `2.1.0.0` made none |
| 1, or any other number but 2 | on, on a listed controller |
| 2 (revision 3) | on, on every Intel controller, listed or not: section 4a, at the user's own risk |

Only an explicit 0 turns it off, so a mistyped value cannot disable the
feature. Until revision 3, 2 was "any other number"; it now means the
bypass, and only an exact 2 does - so the one value that reaches unlisted
silicon has to be typed on purpose, and a stray number keeps the gated
meaning. On a listed controller 2 and 1 do the same thing, and the log
says the gate was not bypassed (section 8). Absent is the case after text-mode Setup's F6 install, since
`txtsetup.oem` writes no values. Both INFs write it at 1 on every controller
install path with `0x00010003` (FLG_ADDREG_NOCLOBBER), under task 34.1's
rule: an install writes it only when it is missing, so a user's 0 survives
an update. The INF gate's `VAL-*` table requires it and its default, and the
footprints carry it.

It is read at each start, on every Intel controller since revision 3 (on a
listed one only before it), and never on another vendor's, so a change takes
effect at the next start (a disable and enable in Device Manager, or a
restart). It exists for a machine whose switchable connectors must stay
EHCI's: a device that works better under that machine's EHCI driver, or
another system on the same disk that expects firmware's routing.

Setting 0 does not restore firmware's routing; it only stops this driver
writing. After a restart firmware sets the routing again. But a disable and
enable in Device Manager after setting 0 leaves the connectors where the
disable's release put them, on EHCI, whatever firmware had chosen at boot.

## 6. When

| Moment | Action | Where |
|---|---|---|
| Each start | route | `hcdStartBody`, before `XhciInitController` |
| Each return to D0 | route | `hcdD0Finish`, before `XhciResumeController` |
| The suspended-in-D0 fallback's resume | route | `hcdDirectTransitionGated`, before the resume |
| Each stop, surprise removal, remove | release | `hcdStopBody`, after `XhciStopController` |
| A refused start | release | `hcdStartBody`'s refusal path, after the stop |
| The D3 of a system shutdown | release | `HcdControllerPower` and the direct-transition fallback, when `SystemPower` is `PowerSystemShutdown` |
| A sleep or hibernate | nothing | the D0 that follows routes again |

**The start routes before `XhciInitController`**, as Linux routes before the
controller's reset: the connectors are the xHCI's when it powers its ports
and the root hub first enumerates them, so no connect has to be chased after
the fact.

**Every resume routes again.** Linux does it unconditionally after a system
resume (`xhci-pci.c`, the comment above its second call), because firmware
may put the connectors back on EHCI across the sleep. Routing twice writes
the same values, so a resume on which firmware kept the routing costs four
config reads and two writes.

**The release is broader than Linux's.** Linux's xHCI driver hands the
connectors back to EHCI only in `xhci_shutdown`, and only on Panther Point,
Lynx Point-LP and Wildcat Point-LP, against boards that power back on a few
seconds after shutdown (`XHCI_SPURIOUS_REBOOT`); its early PCI quirk also
releases them when the kernel is built without an xHCI driver. The HCD
releases on every gated controller and at every stop too, so a disabled,
removed or refused driver does not leave the connectors on an xHCI nothing
drives: back on EHCI, they work again if an EHCI driver is loaded. The
release comes after the halt so that the disconnects it makes are seen by no
running controller.

**Every access is best effort.** A route or release that fails part-way is
logged (its step) and counted (`PswFailures`), and nothing else follows: a
failed route does not refuse the start or undo the writes already made, and
`PswLife.On` stays set, so the stop still releases; a failed release does not
hold up the stop, which clears `PswLife.On` all the same, so a refused
`XUSB2PR = 0` leaves those connectors on xHCI.

**The release writes only what a route wrote (revision 3).** Until revision
3 the release wrote both registers whenever `PswOn` was set, and `PswOn` was
set before the start's route ran: a route that stopped at an unreadable or
all-ones `USB3PRM` had written nothing, and the stop still wrote zero to
both; one that stopped at `XUSB2PRM` had written `USB3_PSSEN` only, and the
stop still wrote zero to `XUSB2PR`. On a listed part that is a zero written
over firmware's routing for no reason; under value 2 it is a write to an
unknown register that the route itself had declined to make. So the
lifetime (section 7a) keeps, per register, whether a route's write to it was accepted -
`USB3_PSSEN` (`D8h`) and `XUSB2PR` (`D0h`) separately - accumulated over one
started lifetime: the start's route and every resume's. A later route that
fails does not clear what an earlier one wrote, since the register may still
hold the earlier value. The release writes zero to each register in that set,
`USB3_PSSEN` first as before, and to no other; with the set empty it makes no
access at all and is not counted. A release does not empty the set (writing
zero twice is harmless, and a refused zero must be retried by the stop that
may follow a shutdown's D3); it is emptied only where `PswLife.On` is cleared, at
each start's decision, the stop and a refused start. Membership is the
configuration-space callback reporting success, nothing more: a write whose
request failed was not confirmed accepted, though `HcdSvcConfigSpace`'s
failure does not prove the hardware saw nothing. Such an unconfirmed write
gets no release unless an earlier route of the same lifetime already
recorded that register; releasing a register on no evidence it was written
is what this revision exists to stop.

This applies at every value, 1 included. The masks, the routes, the moments
and the INFs' value are unchanged; at 1 the release now writes only the
accumulated accepted-write set, partial and empty sets included: a route
that wrote only `D8h` is followed by a release of `D8h` alone, and one that
wrote nothing by a release that writes nothing. The read-backs are logged
but not compared with what was written, so step 0 says every access was
accepted, not that the routing changed; the tester's capture is what
compares them.

**What is not touched.** The in-place recovery (design record 07) re-runs the
controller sequence at DISPATCH_LEVEL, where configuration space cannot be
reached, and does not route. A controller reset is not expected to change
PCH configuration registers, but that is unread; if a tester sees the
connectors go dead after a recovery, this is the first place to look.

**The cost on a machine with an EHCI driver running.** A device attached to a
switchable connector under EHCI when the HCD starts is disconnected there and
reappears under the HCD. A USB drive mounted under EHCI loses its volume
mid-session, and writes in flight are at risk; Linux avoids this by running
first, which a Windows driver cannot. The reverse happens at the HCD's stop.
The release notes must say so (34.4).

## 7. The sequence (pure core, `src/xhci_psw.c`)

The route, as Linux's `usb_enable_intel_xhci_ports`:

1. Read `USB3PRM`. Unreadable, or all-ones (what an access nothing decodes
   returns; no PCH has 32 switchable ports): stop, nothing written.
2. Write `USB3_PSSEN` = `USB3PRM`. Refused: stop, so the USB 2.0 pairs never
   move without the terminations.
3. Read `USB3_PSSEN` back, for the log. A failed read-back is reported and
   the route goes on, since the write was accepted.
4. Read `XUSB2PRM`; refused or all-ones: stop.
5. Write `XUSB2PR` = `XUSB2PRM`; refused: stop.
6. Read `XUSB2PR` back.

The route also returns which of its two writes were accepted
(`XHCI_PSW_STATE.Written`: `XHCI_PSW_WROTE_PSSEN`, `XHCI_PSW_WROTE_XUSB2PR`),
and nothing else sets them: a refused write, a mask read and a read-back
leave them clear (revision 3).

The release, as Linux's `usb_disable_xhci_ports`: write `USB3_PSSEN` = 0
then `XUSB2PR` = 0, each only when the set the caller passes names it
(revision 3; section 6). Linux makes the two writes only; the HCD reads each
written register back for the log. Both writes are attempted whatever the
first did, and the first failure is the one reported. A register not in the
set is neither written nor read, and its read-back stays `0xFFFFFFFF`. The masks are written
whole, as Linux writes them, not merged with the register's previous value.

Each returns a step code (`XHCI_PSW_STEP_*`, 0 when every access was made)
and fills the two masks and two read-backs, `0xFFFFFFFF` where an access was
not reached. The gate (`XhciPswGate`), the board exemption
(`XhciPswBoardRefused`) and the value rule are pure too. Revision 3 replaces
`XhciPswEnabled(found, value)` with `XhciPswMode(vendorDevice, found, value)`,
which returns `XHCI_PSW_MODE_OFF` (0), `XHCI_PSW_MODE_GATED` (1: a listed
controller, the value not an explicit 0) or `XHCI_PSW_MODE_BYPASS` (2: an
Intel controller the list does not name, the value found and exactly 2);
`XhciPswIntel(vendorDevice)` says whether the value is read at all.

## 7a. The lifetime (pure core, revision 3)

Until revision 3 the start's decision and `PswOn` lived in `hcd_ctl.c`. The
per-register write set has to survive every route of a started lifetime and
be emptied at exactly the places `PswOn` was cleared, and the roadmap asks for host
vectors over that lifetime, so the decision and the set move into
`xhci_psw.c` beside the sequence:

- `XHCI_PSW_IO` gains a third callback, `Value(context, &value)`, returning 1
  when `XhciIntelPortSwitch` was found as a DWORD; the executor's reads the
  driver key as `hcdReadDword` does today.
- `XHCI_PSW_LIFE` holds `On`, `Mode`, `Written` and what the decision read
  (`Id`, `Found`, `Value`, `Subsystem`, and `Stop`, which of section 8's
  outcomes ended it: unread offset 0, not Intel, mode off, unread subsystem,
  the Sony board, or routed).
- `XhciPswLifeStart(io, life, st)` clears the lifetime, decides in section
  8's order through `io`, and when on, routes and adds the route's
  `Written`. `XhciPswLifeResume(io, life, st)` routes when `On` and adds.
  `XhciPswLifeRelease(io, life, st)` releases `Written`, leaving `On` and the
  set alone (the shutdown's D3). `XhciPswLifeEnd(io, life, st)` releases and
  then clears `On` and the set (the stop and a refused start). Their return
  values are defined narrowly, so the executor counts and logs only what
  happened: `LifeStart` and `LifeResume` return 1 when the route sequence
  was invoked, and the decision's own reads (offset 0, the value, the
  subsystem) never count, so a start on QEMU, at 0 or on the Sony board
  returns 0; `LifeRelease` and `LifeEnd` return 1 when the set they acted on
  was not empty. Every function initialises `st` on every path, the
  unreached fields `0xFFFFFFFF` and `Step` `XHCI_PSW_DONE`, so a caller that
  logs `st` after a 0 return logs no stale values.

The executor keeps the I/O callbacks, the counters and the log notes, and
its four sites call these four functions where they call `HcdPswRoute`,
`HcdPswRelease` and `hcdPswStart` today. `XHCI_PSW_LIFE` is appended to the
end of `HCD_CONTROLLER` as `PswLife`, and `PswOn` is removed, `PswLife.On`
replacing it: nothing outside `hcd_ctl.c` and `hcd_power.c` reads it (no
tracked harness reads `HCD_CONTROLLER`; `gen-offsets.ps1` derives
`XHCIHC_COUNTERS`), and the three counters behind it move up one dword,
which nothing depends on.
The module takes two configuration-space accessors as callbacks and touches
nothing itself.

## 8. The executor (`src/hcd_ctl.c`, `src/hcd_power.c`)

- **Configuration space** goes through `HcdSvcConfigSpace`, the
  `IRP_MN_READ_CONFIG` / `IRP_MN_WRITE_CONFIG` service the start's own reads
  use, so **no new import**. It is the HCD's second configuration-space writer
  after the quiesce path's Bus Master Enable, and `xhci_hw.h`'s contract says
  so. Windows 98's PCI bus driver honours `IRP_MN_WRITE_CONFIG` above the
  header on one machine: on the B490 under Windows 98 SE with NUSB every write
  was accepted and each read-back equalled its mask (section 10). Other
  systems and chipsets are unread, and the read-backs in the log are how a
  capture answers it there.
- **IRQL and locks.** That service sends an IRP and waits, so every call is
  at PASSIVE_LEVEL and under no spin lock: the start and stop hold the door
  gate, and the D0 paths the power gate, both waitable events.
- **State.** `HCD_CONTROLLER.PswLife.On` (section 7a; before revision 3,
  the `PswOn` field it replaces) is set by a start that decided on and read
  a non-exempt subsystem id; a route or release does nothing without it.
  Every start clears it before deciding, and the stop and a refused start
  clear it after releasing, so it cannot outlive the start that set it.
  `PswRoutes`, `PswReleases` and `PswFailures` count, never zeroed.
  `PswLife` also carries the accepted-write set of section 6 and the mode.
  `On` is set exactly when the mode is not `OFF` and the board check passed,
  and the start decides in this order: offset 0 read (else `psw.pci.unread`, nothing more);
  vendor Intel (else `psw.gate` 0, no value read); the value read; the mode
  (`OFF`: done); the subsystem read and the Sony check; then the route.
- **The log.** On every controller, `psw.gate` (0 or 1), or `psw.pci.unread`
  if offset 0 could not be read. On a gated one, `psw.value.found` and
  `psw.value`, then `psw.board.unread` or `psw.board.refused` if the board
  stops it; at each route `psw.usb3prm`, `psw.usb3pssen`, `psw.xusb2prm`,
  `psw.xusb2pr` and `psw.route.step`; at each release
  `psw.release.usb3pssen`, `psw.release.xusb2pr` and `psw.release.step`.
  Revision 3: `psw.gate` keeps its meaning, the list's verdict (so 0 on an
  unlisted Intel part under 2), and on every Intel controller
  `psw.value.found` and `psw.value` follow it, then `psw.mode` (0, 1 or 2),
  which is how `XHCISNAP` names the value in effect and whether the gate was
  bypassed: it prints log notes by label, so no decoder change is needed.
  `psw.route.written` follows each route (the set it added) and
  `psw.release.written` precedes each release (the set it acts on); a
  release with an empty set logs that and nothing more.
  The `qemu` flavour's trace prints the gate's verdict and the two
  read-backs. The log ring records only with `XhciLogVerbosity` raised
  (design record 08, section 13), so a tester sets it before the reading.

## 9. Tests

`test\test_psw.c` (in `test\run-host-tests.cmd`) drives the pure half over
a modelled configuration space that records every access in order and fails
any one on request:

- the six ids, their neighbours (Sunrise Point, Comet Lake-LP, an id under
  another vendor, vendor and device swapped, all-ones, 0) and QEMU's id;
- the Sony board against nearby ids;
- the value rule at absent, 0, 1, 2 and all-ones;
- the route's six accesses in order with the values written, a second route
  as at D0, masks of 0, and a refusal at each step with what it leaves;
- the release's four accesses, a refusal at each, and that a release reads
  no mask.

Revision 3 adds, in the same model:

- `XhciPswMode` over a listed id, an unlisted Intel id (`9D2F`, `02ED`) and
  a non-Intel id (QEMU's) at absent, 0, 1, 2, 3 and all-ones: 2 bypasses
  only on the unlisted Intel id, a listed id reads `GATED` at 2, and nothing
  but `OFF` on the non-Intel one; `XhciPswIntel` on the same ids;
- `Written` after a route stopped at each step: `USB3PRM` unreadable or
  all-ones, nothing; `USB3_PSSEN`'s write refused, nothing; its read-back
  failed and every later access good, both (the route goes on past that
  failure, section 7), with `Step` still reporting the read-back;
  `XUSB2PRM` unreadable (the `D4h` read), `PSSEN` only; `XUSB2PR`'s write
  refused, `PSSEN` only; its read-back failed, both;
- the release over each set: empty, no access at all; `PSSEN` only, `D8h`
  written and read and `D0h` untouched; both, the four accesses as before;
- the lifetime, as the executor accumulates it: a good start route then a
  resume route that fails at `USB3PRM`, the release still writes both; a
  start route that wrote `PSSEN` only then a good resume route, both;
- the start's decision under 2 with the Sony subsystem and with the
  subsystem read failing: no write, and `LifeStart` returns 0;
- an unreadable offset 0: no value read and no subsystem read;
- the lifetime's edges: start, shutdown release, end - the end releases the
  same set again, and a shutdown zero that was refused is retried there; a
  release without an end keeps `On` and the set; a refused start's end
  clears both; a second end makes no access and returns 0; a fresh start
  after an end inherits no bit.

The last two items drive the lifetime functions of section 7a, so the
decision order and the accumulation are host-tested in the code that ships;
what stays reviewed rather than host-tested is only where `hcd_ctl.c` and
`hcd_power.c` call them, as before.

The model shares the register offsets with the code under test, so it proves
the order and the logic, not the offsets; those rest on section 2's sources.
The executor's lifecycle is not host-tested; it was reviewed, and the QEMU
reading (section 10) is to show the gate closed.

## 10. Readings

All three are recorded clause by clause in `docs/contributing/runs/run-34.md`.

- **QEMU** (34.3-Q, passed 2026-10-05): a Windows 98 SE + NUSB 3.3 guest on
  the `qemu` flavour, `1B36:000D`: the trace's "not an Intel 7/8/9-series
  xHCI, no config write" once at the one start, and no route, release or
  read-back line.
- **The gate closed on non-mux Intel silicon** (34.3-H1, passed 2026-10-05,
  the owner's P14s Gen 1, `02ED`): `psw.gate=00000000` and no other `psw.`
  record; every external connector working.
- **The switchover** (34.3-H2, passed 2026-10-05, the owner's Lenovo B490,
  `8086:1E31`, under Windows 98 SE with NUSB's EHCI driver loaded, the
  release build of `f393bb6`). The hardware reading had been planned as a
  tester's; the owner took it instead. On Auto:
  - before Windows, `XHCIQUAL` found `XUSB2PR` and `USB3_PSSEN` at 0 and
    both masks nonzero, as `xhci-programming.md` predicted;
  - under the driver, `psw.route.step` 0 and each read-back equal to its
    mask;
  - a SuperSpeed stick on a blue connector linked at 5 Gbit/s and a file
    made the round trip, and a USB 2.0 device worked under the HCD;
  - the one connector outside the masks stayed with EHCI;
  - a disable gave the blue connectors back to EHCI, and an enable took
    them again;
  - with `XhciIntelPortSwitch` 0 and a restart, no route record, and the
    connectors stayed on EHCI;
  - after a shutdown the machine stayed off, and the next boot found both
    registers at 0;
  - a mouse attached under EHCI at boot ended up under the HCD.

  Smart Auto repeated the firmware reading, the route, the SuperSpeed link
  and the shutdown with the same results. The register values themselves
  are the owner's report; the logs stayed on the B490.
- **Not read:** a standby and resume (Windows 98 SE offers none on the
  B490); any system but Windows 98 SE; the five other device ids; and
  whether an in-place recovery (section 6) leaves the routing alone.

The readings above are revision 2's. Under revision 3 the P14s Gen 1's log
at the default 1 is no longer `psw.gate=00000000` alone: an Intel part, it
also records `psw.value.found`, `psw.value` and `psw.mode=00000000`, and
still no route or release. Revision 3's own readings are roadmap-hcd 35-V
and 35-E.

## 11. Decisions (owner, 2026-10-05)

1. Supersede the Phase 4 decision: the HCD routes the switchable connectors.
2. The gate is a device-id list, not Linux's rule; 8D31 joins the proposed
   five; every Intel xHCI is refused.
3. The opt-out `XhciIntelPortSwitch` ships at 1 under the don't-overwrite
   flag; absent or any number but 0 is on.
4. The QEMU reading is one Windows 98 SE guest.
5. The task is ticked on real hardware: the switchover on the owner's B490,
   the gate closed on the P14s Gen 1, replacing the tester's reading.

Taken in the building, not separately decided: Linux's Sony exemption; the
release at every stop and on all gated parts rather than Linux's three at
shutdown; a sleep keeping the routing.

Revision 3 (owner, 2026-10-06, roadmap-hcd task 35.5):

6. `XhciIntelPortSwitch` 2 routes on any Intel controller, the device-id
   list bypassed; 1 and any other nonzero number keep the gated meaning; the
   default stays 1 and 0 stays off; the INFs' value and the `VAL-*` row are
   unchanged.
7. Value 2 is at the user's own risk, and the README and release notes say
   so in section 4a's words, with 1 as the setting for everyone else. No
   register-layout check refuses it (a review proposed one; the owner chose to
   inform rather than guard).
8. Under 2 the Sony exemption and the refusals on an unreadable identity or
   subsystem are kept.
9. At every value the release writes only the registers a route wrote,
   tracked per register over one started lifetime.

Taken in this revision, for the review to test: only an exact 2 bypasses;
a write not confirmed accepted is not in the set; a release does not empty the
set; the decision and the set move into the pure core (section 7a).

## 12. What changes in the documents (task 34.4)

- `xhci-programming.md`: the `XUSB2PR` section, which says the driver leaves
  the registers alone, and its row in the deviations table ("Not handled").
- `implementation-invariants.md`, "PnP Resources": it admits configuration
  reads for identification and quirk selection and refuses BAR rediscovery;
  the switchover's writes above the header are a new kind of access to name
  there, and the BAR rule stands as it is.
- `failure-diagnosis.md`, row 7: the dead-looking controller now has the
  switch and its log as the first check.
- The release notes: the switchover, its untested standing, the opt-out, the
  disconnect of EHCI-attached devices at start and stop, and the seven values
  that become eight.
- The README's registry table; the acceptance test's note under 1.2;
  `xhciqual/hardware-testing.md`'s Intel 7/8-series section;
  `source-files.md` for `xhci_psw.c` and `.h`.
- Design record 13: a pointer here from the controller start (section 5)
  rather than a copy.

## 13. What changes in the documents (revision 3, task 35.6)

- The README's registry table and the release notes: value 2, with section
  4a's warning in full and 1 named as everyone else's setting; the release
  notes also say a release now writes only what a route wrote.
- The readme template, beside the same table.
- `xhciqual/hardware-testing.md`'s Intel section: value 2 as the way in for
  an unlisted part with an EHCI, with the same warning.
- `failure-diagnosis.md`, row 7: `psw.mode` and the two `written` notes.
- `source-files.md`: `xhci_psw.c` now holds the lifetime too.
- Design record 13: nothing beyond the existing pointer.
