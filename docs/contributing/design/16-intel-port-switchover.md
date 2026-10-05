# The Intel EHCI-to-xHCI port switchover

Design record for roadmap-hcd task 34.4 (Phase 34, release `2.1.1.0`).
Revision 1, 2026-10-05: written at the owner's request after the code, to
explain it. Built in `0def9a3`, `ea707a6` and the commit that adds this
record. **Nothing in it has run on a
machine with the mux**: no machine this project has ever had carries one, so
every hardware statement below is taken from Linux and from
`docs/usb-xhci-info/xhci-programming.md`'s `XUSB2PR` section, which is itself
a prediction, and the hardware reading is a tester's (section 10).

## 1. What is asked, and what is not

A tester reported Ivy Bridge machines on which `xhci98.sys` started cleanly
and then saw nothing on the blue ports, while a SuperSpeed stick ran at High
Speed under the USB 2.0 controller. On the Intel 7-, 8- and 9-series PCH and
on C610/X99, each switchable connector is wired to two controllers, EHCI and
xHCI, and four registers choose which one has it. Firmware set to "Auto",
or offering no setting, leaves every switchable connector on EHCI and expects
the operating system's xHCI driver to move them; Windows 7 does it through
Intel's own USB 3.0 package, and Linux at probe. The HCD now does it too.

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
   because `xhciqual`'s `quirks.c` already marks it `QF_XUSB2PR`. Every
   entry is a part with Linux or `xhciqual` evidence. It is the driver's
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
subsystem id that cannot be read also skips the switchover**: an unread id is no evidence that the board is not that one.

On every other controller (QEMU's `1B36:000D` among them) the HCD reads PCI
offset 0 and nothing else of this design, and writes nothing.

## 5. The switch: `XhciIntelPortSwitch`

A REG_DWORD in the controller's driver key (owner, 2026-10-05):

| Value | Effect |
|---|---|
| absent, or not a DWORD | on: the default |
| 0 | off: the routing is left as firmware set it, as every release to `2.1.0.0` did |
| 1, or any other number | on |

Only an explicit 0 turns it off, so a mistyped value cannot disable the
feature. Absent is the case after text-mode Setup's F6 install, since
`txtsetup.oem` writes no values. Both INFs write it at 1 on every controller
install path with `0x00010003` (FLG_ADDREG_NOCLOBBER), under task 34.1's
rule: an install writes it only when it is missing, so a user's 0 survives
an update. The INF gate's `VAL-*` table requires it and its default, and the
footprints carry it.

It is read at each start and only on a gated controller, so a change takes
effect at the next start (a disable and enable in Device Manager, or a
restart). It exists for a machine whose switchable connectors must stay
EHCI's: a device that works better under that machine's EHCI driver, or
another system on the same disk that expects firmware's routing.

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

**The release is broader than Linux's.** Linux hands the connectors back to
EHCI only at shutdown, and only on Panther Point, Lynx Point-LP and Wildcat
Point-LP, against boards that power back on a few seconds after shutdown
(`XHCI_SPURIOUS_REBOOT`). The HCD releases on every gated controller and at
every stop too, so a disabled, removed or refused driver never leaves the
connectors on an xHCI nothing drives: back on EHCI, they work again if an
EHCI driver is loaded. The release comes after the halt so that the
disconnects it makes are seen by no running controller.

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
The release notes must say so (34.5).

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

The release, as Linux's `usb_disable_xhci_ports`: write `USB3_PSSEN` = 0
then `XUSB2PR` = 0, each read back; both writes are attempted whatever the
first did, and the first failure is the one reported. The masks are written
whole, as Linux writes them, not merged with the register's previous value.

Each returns a step code (`XHCI_PSW_STEP_*`, 0 when every access was made)
and fills the two masks and two read-backs, `0xFFFFFFFF` where an access was
not reached. The gate (`XhciPswGate`), the board exemption
(`XhciPswBoardRefused`) and the value rule (`XhciPswEnabled`) are pure too.
The module takes two configuration-space accessors as callbacks and touches
nothing itself.

## 8. The executor (`src/hcd_ctl.c`, `src/hcd_power.c`)

- **Configuration space** goes through `HcdSvcConfigSpace`, the
  `IRP_MN_READ_CONFIG` / `IRP_MN_WRITE_CONFIG` service the start's own reads
  use, so **no new import**. It is the HCD's second configuration-space writer
  after the quiesce path's Bus Master Enable, and `xhci_hw.h`'s contract says
  so. Whether Windows 98's PCI bus driver honours `IRP_MN_WRITE_CONFIG` above
  the header is unread; the read-backs in the log are how a tester's capture
  answers it.
- **IRQL and locks.** That service sends an IRP and waits, so every call is
  at PASSIVE_LEVEL and under no spin lock: the start and stop hold the door
  gate, and the D0 paths the power gate, both waitable events.
- **State.** `HCD_CONTROLLER.PswOn` (appended at the end of the structure) is
  set by a start that passed the gate, read the switch on and read a
  non-exempt subsystem id; a route or release does nothing without it. Every
  start clears it before deciding, and the stop and a refused start clear it
  after releasing, so it cannot outlive the start that set it.
  `PswRoutes`, `PswReleases` and `PswFailures` count, never zeroed.
- **The log.** On every controller, `psw.gate` (0 or 1), or `psw.pci.unread`
  if offset 0 could not be read. On a gated one, `psw.value.found` and
  `psw.value`, then `psw.board.unread` or `psw.board.refused` if the board
  stops it; at each route `psw.usb3prm`, `psw.usb3pssen`, `psw.xusb2prm`,
  `psw.xusb2pr` and `psw.route.step`; at each release
  `psw.release.usb3pssen`, `psw.release.xusb2pr` and `psw.release.step`.
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

The model shares the register offsets with the code under test, so it proves
the order and the logic, not the offsets; those rest on section 2's sources.
The executor's lifecycle is not host-tested; it was reviewed, and the QEMU
reading (section 10) shows the gate closed.

## 10. Readings

- **QEMU** (owed before 34.4 is ticked): on `1B36:000D`, `psw.gate 0` in the
  log or the trace, and no configuration access past offset 0. A Windows 98 SE
  guest on the `qemu` flavour (owner, 2026-10-05).
- **Hardware** (a tester's; until read, the release notes call this untested
  ground, as 33.8's polling was):
  - a 7-, 8- or 9-series machine with firmware on Auto;
  - an `XHCISNAP` capture with `XhciLogVerbosity` raised, carrying the
    `psw.*` records: the masks and that each read-back equals its mask;
  - a SuperSpeed stick on a switchable connector read at SuperSpeed;
  - a USB 2.0 device on a switchable connector working under the HCD;
  - EHCI keeping the connectors outside the masks;
  - with `XhciIntelPortSwitch` 0, the routing left as firmware set it;
  - a disable in Device Manager returning the connectors to EHCI;
  - after a shutdown, the routing back on EHCI and the machine staying off;
  - a standby and resume with the connectors still working.

## 11. Decisions (owner, 2026-10-05)

1. Supersede the Phase 4 decision: the HCD routes the switchable connectors.
2. The gate is a device-id list, not Linux's rule; 8D31 joins the proposed
   five; every Intel xHCI is refused.
3. The opt-out `XhciIntelPortSwitch` ships at 1 under the don't-overwrite
   flag; absent or any number but 0 is on.
4. The QEMU reading is one Windows 98 SE guest.

Taken in the building, not separately decided: Linux's Sony exemption; the
release at every stop and on all gated parts rather than Linux's three at
shutdown; a sleep keeping the routing.

## 12. What changes in the documents (task 34.5)

- `xhci-programming.md`: the `XUSB2PR` section, which says the driver leaves
  the registers alone, and its row in the deviations table ("Not handled").
- `implementation-invariants.md`, "PnP Resources": configuration space is no
  longer read for identification only.
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
