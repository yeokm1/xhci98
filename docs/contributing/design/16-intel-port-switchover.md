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
`2.2.0.0`, planned as `2.1.2.0` when this revision was written), written
before its code: `XhciIntelPortSwitch` 2, the
switchover on any Intel controller past the gate, at the user's own risk
(sections 4a and 5); and, at every value, a release that writes only the
registers a route wrote (sections 6 to 8). Its decisions are section 11's
second list; what it changes in the documents is section 13. Built in
`c7b29da`, `d3bf449` and `febf5ab` (task 35.5, 2026-10-06), reviewed and
revised after the code; the documents of section 13 were changed at
task 35.6 (2026-10-07). It ships in `2.2.0.0`, read so far on host tests
alone (section 10).

Revision 4, 2026-10-09, for roadmap-hcd task 36.42 (Phase 36, release
`2.3.0.0`): **the switchover is off by default.** `XhciIntelPortSwitch`
absent, of another type or unreadable now means off, as 0 does, and both
INFs write it at 0 (section 5); 1 and 2 mean what they meant. The owner
took the argument made on GitHub issue 17 (comment 6082858626): a connector the
driver does not route stays on EHCI and works there under the EHCI driver,
at High Speed, so routing it unasked trades a working connector for one
under this driver. A user script, `portsw.vbs`, shows and sets the value
(section 5a). Its decisions are section 11's third list, its documents
section 14. Nothing else in this record changes: the gate, the Sony
exemption, the sequence, the lifetime and value 2 are as revision 3 left
them.

Revision 5, 2026-10-11, for roadmap-hcd task 36.56 (a re-cut of release
`2.3.0.0`, its driver binaries unchanged): two batch files beside the
script, `portsw-on.bat` and `portsw-off.bat`, which set 1 or 0 through it
in one double-click (section 5a, "The batch files"). The owner asked for
them at the E7250 bench, where a USB 3 stick copied at USB 2.0 speed with
the switchover at its default 0. Its decision is section 11's twelfth, its
documents section 15. Nothing in the driver changes.

Revision 6, 2026-10-12, for roadmap-hcd task 37.5 (Phase 37): the script
and its two batch files move out of the four flavour directories, where
`2.3.0.0` published four byte-identical copies, into one `portsw\`
directory at the version root beside `xhciqual\` and `xhcisnap\` (section
5a). The packages no longer carry them. The scripts' logic does not change;
only the batch files' words do. Its decision is section 11's thirteenth,
its documents section 16.

Revision 7, 2026-10-13, for roadmap-hcd task 38.9 (Phase 38, release
`2.4.0.0`): the release's tools move into one `tools\` directory at the
version root, so `portsw\` becomes `tools\portsw\`, beside
`tools\xhciqual\` and `tools\xhcisnap\`, and the version root holds only
the driver directories, `readme.txt` and `LICENSE` (section 5a). The
scripts' logic does not change for it. `2.3.1.0`, the
one release published with `portsw\` at the version root, keeps it there.
Its decision is section 11's fourteenth, its documents section 17.

Revision 8, 2026-11-14, for roadmap-usb4 task 49.47 (Phase 49, release
`3.5.0.0`): **the switchover is on by default again, and the scripts are
gone.** Both INFs write `XhciIntelPortSwitch` 1, under the same
don't-overwrite flag, so an install made from `2.3.0.0` to `3.4.3.0` keeps
its 0 through an update; a value that is absent, of another type or
unreadable stays off, on a listed controller too (`XHCI_PSW_ABSENT_ROUTES`,
`xhci_psw.c`; the owner's second decision of 2026-11-14);
an unlisted Intel controller still needs an exact 2 and another vendor's
is never touched. The download's `tools\portsw\` is no longer published:
the xHCI98 USB Tree's Settings window and `XHCITR /set` set the value
(design record 25 section 22). The owner's decisions are section 11's
fifteenth to eighteenth; section 18 has the history and what the default
brings back.

Revision 9, 2026-11-14 (the same day, the owner's final design for task
49.47): **the INFs write 0 again, and Setup turns it on.** An integrated
install (a Windows image with the driver added by an integration tool or
by Windows' own image servicing) would install the controller from the INF
during Windows Setup and move the blue ports then, which can drop an
install drive on one; so the driver's default is off, exactly as in
`3.4.3.0` - both INFs write 0 and a missing value is off - and an install
from the INF alone (by hand, F6, an integrated image) never moves the
ports by itself. Setup proposes 1 at a first install on a listed board
(`inst_psw_propose`, `xhciinst\inst_core.c`; `psw_proposed`, `psw_seed`,
`xhciinst\inst_eng.c`): its console question and its Settings row offer
1 as the choice, and with no other answer it writes 1 machine-wide, `/yes`
included. An update keeps what is set. Revision 8's "on by default" is
superseded; the removal of `tools\portsw\` stands. Its decisions are
section 11's nineteenth to twenty-first, its story section 19.

Revision 10, 2026-11-17 (Phase 50, release `3.6.0.0`): **the default stays
off**, and the emulator gains a stand-in for a listed controller. The
owner asked whether the INFs could write 1 after all, with Windows Setup
in mind; a trial build that did so was read in emulator Setup legs on
2000, XP, Vista and 7, and the owner decided that the default stays off
(decision 22): revision 9 stands as written. The legs needed the qemu
flavour's `XhciQemuPswModel`, which lets the switchover's own code see a
listed controller on QEMU's; it is kept as a test aid. Section 20 has the
aid, the decision and what the legs read.

Revision 11, 2026-11-17 (Phase 50, roadmap-usb4 task 50.34; a design
written before any code): **the INFs may write the
switchover on, and the driver holds it while Windows Setup runs.** Later
the same day the owner decided, in his words: "when Windows setup marker
is in progress don't switch the ports. Only switch after marker is done.
Allow registry override if read wrongly." (decision 23), so that a user
who integrates the driver into a Windows install image, where Windows
Setup installs it from the INF and `SETUP.EXE` never runs, gets the
switchover too. Section 21 is the design: the values read and the rule
that combines them, a new setting that overrides the reading, the
lifetime, what the INFs write, what is shown, the risks, the tests, and
the questions left to the owner (section 21.11). Nothing in it is built:
until its code is merged the INFs write 0 and decision 22 describes the
driver.

Revision 12, 2026-11-17 (the same task, still before any code): the
design revised with the owner's answers to revision 11's nine
questions, given the same day (decisions 24 to 32). Every INF
section writes the switchover on in this release, 98 SE and ME included,
and the owner's bench reading on a listed board follows the cut; on 2000
and later the hold also covers a Setup value that cannot be read,
Windows PE and, on Vista and later, an image state not yet complete; no
controller switches in the Windows session in which the switchover was
first turned on for it, a boot boundary told by a volatile registry key
and remembered in a value of its own that the INFs write; a held
lifetime makes no routing write at all, the shutdown hand-back included;
the setting's 1 is "bypass". Section 21 is rewritten for it in place,
its revision 11 text superseded; section 21.11 lists the answers.

Revision 13, 2026-11-18 (the same task, still before any code): the
design revised on a probe of what the controller's start can read (section 21.2; a `qemu`-flavour probe on a
branch that is never merged). The probe found the `SOFTWARE` hive, and
with it the image state, not yet loaded at the controller's first start
of a boot on Vista; revision 12's remembered image state is removed, and
such a start holds, as decision 25 says, with the bypass setting as the
way out. The owner decided this in advance of the probe's result (owner,
2026-11-18): an image state not readable at the start holds, and the
bypass setting is the way out. Also: the
guard's algorithm completed, with a boot boundary on 32-bit 2000 to 10
that does not need the helper in an install's own session, and its
restart counts corrected; the guard's value is 0 or 1; the session key's
lifecycle, sleep, hibernation and Fast Startup; the string readers'
validation; the tests the design was missing; and the wording.

Revision 14, 2026-11-18 (the same task, still before any code): the
guard is set out as two complete
alternatives for the owner to choose between (section 21.5): A, the
volatile registry key, now arming the guard's value only on session
evidence proven at that start; B, the guard's value and a flag in the
driver image's own memory. The variation of the image state's
readability from one boot to the next (the 10 base) is treated as a
hazard, with an operating restriction and acceptance cases, and whether
to make automatic mode the same at every boot is put to the owner. The
probe-derived wording is held to its evidence, and the naming is
audited. Section 21's head lists what awaits the owner.

Revision 15, 2026-11-18 (the same task, still before any code): the two
guard alternatives' contracts made exact, so that the code can be
written from either: B stated as a hold for one load of the driver
image, decided per controller start, which any reload of the image ends
(Windows' own during an installation or update included), so that
choosing it relaxes decision 26; its flag's synchronization; A's proof
cited and its limits listed; the update rows of section 21.5.3
corrected; the recommendation A, with both cases; Q12's options (b) and
(c) stated as what they are; the acceptance cases for both alternatives'
boundaries; and the drafts' promises and the off setting's shutdown
exception. The owner answered its three questions the same day
(decisions 34 to 36): Alternative A, the decided rule kept for this
release, and the exception granted; section 21 is carried through to A,
with B kept as the alternative that was weighed.

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
or it may disturb the controller or the machine. A register-layout
check (an EHCI-style mask pattern, say) that would refuse such a
controller was considered; the owner chose to inform rather than guard
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
> list, and at your own risk. Everyone else should use 0 or 1.

## 5. The switch: `XhciIntelPortSwitch`

A REG_DWORD in the controller's driver key (owner, 2026-10-05; the default
reversed 2026-10-09, revision 4):

| Value | Effect |
|---|---|
| absent, not a DWORD, or unreadable | off: the default since revision 4, kept by revision 8 for text-mode Setup (on, on a listed controller, before revision 4) |
| 0 | off: this start reads PCI offset 0 and the value, and makes no switchover write, as every release to `2.1.0.0` made none |
| 1, or any other number but 2 | on, on a listed controller |
| 2 (revision 3) | on, on every Intel controller, listed or not: section 4a, at the user's own risk |

From task 50.34 "on" means the switchover's step runs (section 21.3): on
2000 and later a start routes only where Windows Setup's readings and the
first-session guard allow it, or `XhciIntelPortSwitchSetup` is 1 (section
21.4), and a held start writes nothing to the routing registers; on 98 SE
and ME it routes at every start unless that setting is 2.

Only a DWORD that is not 0 turns it on. Until revision 4 it was the
reverse - only an explicit 0 turned it off, so that a mistyped value could
not disable the feature - and revision 4 keeps the meaning of every DWORD
and changes only what a missing or unusable value means. Until revision 3,
2 was "any other number"; it now means the
bypass, and only an exact 2 does - so the one value that reaches unlisted
silicon has to be typed on purpose, and a stray number keeps the gated
meaning. On a listed controller 2 and 1 do the same thing, and the log
says the gate was not bypassed (section 8). Absent is the case after text-mode Setup's F6 install, since
`txtsetup.oem` writes no values; it is off there too. Both INFs wrote it at 0
from revision 4 to `3.5.0.0`, and from task 50.34 write it at 1 (section 21.8),
on every controller install path with `0x00010003` (FLG_ADDREG_NOCLOBBER),
under task 34.1's rule: an install writes it only when it is missing, so a
user's 1 survives an update - and so does the 1 that `2.1.1.0` and `2.2.0.0`
wrote, so a machine updated from either stays on until the user changes it
(the release notes say so, and how). The INF gate's `VAL-*` table requires
it and its default, and the footprints carry it. The same rule keeps the 0
that `2.3.0.0` to `3.5.0.0` wrote across an update to the release of task
50.34: only a controller key the INF creates gets its 1 (section 21.8).

It is read at each start, on every Intel controller since revision 3 (on a
listed one only before it), and never on another vendor's, so a change takes
effect at the next start (a disable and enable in Device Manager, or a
restart). It exists for a machine whose switchable connectors must stay
EHCI's: a device that works better under that machine's EHCI driver, or
another system on the same disk that expects firmware's routing.

Why off by default (revision 4): on these chipsets the connectors firmware
leaves on EHCI work there under any EHCI driver - NUSB's or another
USB 2.0 stack added to Windows 98 SE and ME, the system's own on 2000 and later - at High Speed.
Routing them by default moves a working connector to this driver at every
start and back at every stop, disconnecting whatever was on it, on machines
whose owners did not ask for SuperSpeed there. The user who wants SuperSpeed
on the blue connectors turns it on.

**On again in the INFs from task 50.34** (owner, decision 32; section 21):
a new install writes 1, while the driver's reading of an absent or
unusable value stays off. What answers the reasons above is the hold of
section 21.3 and the guard of section 21.5 on 2000 and later; on 98 SE and
ME the connectors move at the driver's first start (section 21.8,
accepted).

Setting 0 does not restore firmware's routing; it only stops this driver
writing. After a restart firmware sets the routing again. But a disable and
enable in Device Manager after setting 0 leaves the connectors where the
disable's release put them, on EHCI, whatever firmware had chosen at boot.

**One exception to 0's "no switchover write" (task 38.10, design record 20
sections 4.4 and 13).** `XhciIntelShutdownToEhci`, Linux's
`XHCI_SPURIOUS_REBOOT`, on by default: at a system shutdown's D3 on 8086:1E31,
9C31 and 9CB1, after this record's release, `USB3_PSSEN` and `XUSB2PR` are
both written 0 and read back, **whatever `XhciIntelPortSwitch` says**
(owner, 2026-10-13), so the connectors are EHCI's when the machine powers
off. It has no Sony exemption, as Linux's shutdown write has none. It is a
value of its own, read at each start (0 off, 1 the three ids, a 2 read as
1), and its write is the quirk's, in `hcd_quirk.c` `HcdQuirkShutdown`, not
this record's executor. From task 50.34 a lifetime whose start section 21's
step held gets no hand-back either (section 21.6: `XhciPswShutdownAllowed`,
the log note `quirk.shutdown.held`); an off or routed lifetime keeps it.

## 5a. The user script: `portsw.vbs` (revision 4)

`src\portsw.vbs`, published in the `tools\portsw\` directory of a release
(revision 7; in `portsw\` at the version root from revision 6, and until
then at the root of every package directory beside the INF), shows `XhciIntelPortSwitch` on every xhci98 controller and sets it to 0
or 1, then says a restart is needed. It is Windows Script Host VBScript,
because that is the one way to read and write a REG_DWORD that every target
has with nothing installed: Windows 98 SE ships Script Host 1.0 and ME 2.0,
and neither has `reg.exe`; Windows 2000 has no `reg.exe` until its Support
Tools are installed; XP to 7 have both. A batch file driving `regedit /s`
could write a `.reg` file on every target but cannot read a value back
without `regedit /e` and text parsing that differs between the 9x and NT
export formats, and a console tool built with MSVC 6 would need its own
gates and packaging for what is one value. Script Host's `RegRead` and
`RegWrite` take a REG_DWORD on all of them.

Which key: the controller's driver key is
`HKLM\System\CurrentControlSet\Services\Class\USB\NNNN` on 98 and ME and
`HKLM\SYSTEM\CurrentControlSet\Control\Class\{36FC9E60-C465-11CF-8056-444553540000}\NNNN`
on the NT systems, with NNNN varying. Script Host cannot list subkeys, so
the script asks WMI's registry provider (`StdRegProv.EnumKey`) on the NT
systems, and on 98 and ME, where WMI is not part of a stock install, has
`regedit /e` export the class key to a temporary file and reads its section
headers; only if neither answers does it probe every number from `0000` to
`9999`, which takes minutes (a gap in the numbers says nothing about the keys
after it, so no shorter probe is complete). A key is this driver's controller when its `DriverDesc` is the
INFs' `XhciDesc` ("xHCI98 USB 3 Controller" from 3.2.0.0, "xHCI98 USB 3.x
eXtensible Host Controller" before it, both accepted; neither the root hub
nor a hub carries either), and on 98 and ME also when its
`NTMPDriver` is `xhci98.sys`. Every such key is set, so a key left from an
earlier PCI position is set too, harmlessly. It never writes 2: that value
is the user's own risk (section 4a) and is set by hand.

On Vista and 7 a write without the administrator token fails; the script
then starts itself once more through `ShellExecute` with the `runas` verb,
which raises the elevation prompt, and exits with 3, since it cannot know
the elevated copy's result. Elsewhere a refused write is reported, with the
keys already set named, and the advice to run it as an administrator.

The packaging gates hold it: `make-release.ps1` publishes it once, from
`src\`, into `tools\portsw\` (`Publish-PortSwitchScripts`), and refuses a
staged release whose `tools\portsw\` lacks one of the three or holds
anything else, or that has a copy anywhere outside it
(`Assert-PortSwitchLayout`, before the publish swap); the upload assembly
holds a tree that has `tools\portsw\` to the same rule and accepts a cut
before revision 7 as it was published (none before `2.3.0.0`, one set per
flavour directory in `2.3.0.0`, `portsw\` at the version root in
`2.3.1.0`).
`make-package.ps1` stages none of them since revision 6 (it staged them at
every package root before). The packager's self-tests drive both functions
with fixture trees, check the script is ASCII with CRLF and looks for both
INFs' `XhciDesc`, and the source charset gate reads it as a file the
1998-era tools read.

**Why a directory of its own (revision 6).** The script finds the
controllers through the registry, not through the directory it runs from,
and the batch files look for it beside themselves, so neither depends on
the INF beside it; nothing about them is architecture- or flavour-specific,
and the setup engine never reads them, so one copy serves every flavour
directory where `2.3.0.0` carried four byte-identical ones. No `readme.txt` of their own: the top-level readme's section 9
says what they do and where to run them, and each prints its purpose when
run.

### The batch files (revision 5)

`src\portsw-on.bat` and `src\portsw-off.bat` are published wherever
`portsw.vbs` is, in `portsw\` since revision 6 and `tools\portsw\` since
revision 7. Each shows the
value (`cscript //nologo portsw.vbs show`), stops there if that found no
controller, sets 1 or 0 (`cscript //nologo portsw.vbs 1` or `0`), says
that Windows must be restarted, and ends with `pause`, so a double-clicked
window stays open to be read. Neither sets 2.

One file serves both command interpreters, so it is written to the
smaller one. Windows 98 SE and ME run it under `COMMAND.COM`, which has no
`%~dp0`, no `cd /d`, no `setlocal`, no `if /i` and no `goto :eof`, and
which reads `<`, `>` and `|` as redirection even on a `rem` or `echo`
line. The split is the first command after `@echo off`:

    if "%OS%"=="Windows_NT" goto nt
    goto run
    :nt
    cd /d "%~dp0"
    :run

On 98 and ME `OS` is not set, the test is false, and `goto run` jumps over
the one line that needs `cmd.exe`, so `COMMAND.COM` never executes it.
`cd /d "%~dp0"` is there because Run as administrator on Vista and 7
starts a batch file in `System32`; 2000 and XP take the same line. On 98
and ME the working directory is the batch file's own when it is
double-clicked in Explorer or typed at a prompt in its directory, which is
how it is meant to be run; started from anywhere else it does not find
`portsw.vbs` and says where to run it from.

From `:run` both interpreters run the same lines, every one of them
`COMMAND.COM` syntax: `if not exist portsw.vbs`, the two `cscript` calls,
`if errorlevel` tests on what `portsw.vbs` returned, `echo` and `pause`.
After the set, exit code 3 means the script handed the write to an
elevated copy of itself (Vista and 7 without the administrator token): the
batch file says that if the prompt was allowed the result is in the window
that opened, that if it was not nothing changed (the script's exit code is
3 either way), and that next time it should be started with right-click,
Run as administrator. Exit
code 1 means a write was refused, possibly after other controllers' keys
were written (the script names them): the batch file says the change did
not complete and to read the lines above, and on the NT systems alone (a
second `if not "%OS%"=="Windows_NT"` test) to run it as an administrator,
by right-click on Vista and 7. Only exit code 0 is followed by the restart
line.

They are ASCII with CRLF endings, held by the source charset gate (`.bat`
is one of its 1998-era kinds), the batch-file line-ending gate, and the
packager's self-tests. Those hold every line to a short list of what both
interpreters run - `rem`, `echo`, a label, `goto`, `if not exist
portsw.vbs`, `if errorlevel`, `if not "%OS%"=="Windows_NT"`, the
`cscript //nologo portsw.vbs` calls and `pause` - rather than search for
constructs one of them lacks, and exempt only the split's OS test and its
`cd /d` line. They also pin the split itself, no `goto nt` but the first,
no redirection, pipe or percent character on an `echo` or `rem` line, no
line longer than `COMMAND.COM`'s 127 characters, exactly one value set,
and the order of the calls and their tests; a set of broken controls
proves the checker refuses each.

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

From task 50.34 a start may decide to hold (`XHCI_PSW_DECIDE_SETUP_HELD`,
section 21.6): `On` stays clear, so no row of section 6's table writes
anything in that lifetime, the shutdown's D3 and its hand-back included.

## 7. The sequence (pure core, `src/xhci_psw.c`)

From task 50.34 the core also classifies Windows Setup's readings, the
guard's and the setting's, and gives the verdict before this sequence runs
(section 21.3, "The pure core"); the sequence itself is unchanged.

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
controller, the value found and not 0 since revision 4, not an explicit 0
before it) or `XHCI_PSW_MODE_BYPASS` (2: an
Intel controller the list does not name, the value found and exactly 2);
`XhciPswIntel(vendorDevice)` says whether the value is read at all.

## 7a. The lifetime (pure core, revision 3)

From task 50.34 `XHCI_PSW_IO` gains the step's callbacks and
`XHCI_PSW_LIFE` its readings, verdict, guard state and `Step` (set only
where the step is reached, which alone gates the record, section 21.7); a
held start ends the decision as `XHCI_PSW_DECIDE_SETUP_HELD` with `On`
clear (sections 21.3 and 21.6).

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

From task 50.34 the executor also reads the step's values, runs the
guard's volatile session key and writes the record (`hcdPswStart`,
`hcdPswRecord`), all in `hcdStartBody`'s decision at PASSIVE_LEVEL under no
spin lock, before `XhciInitController` (section 21.6); and
`HcdQuirkShutdown` asks `XhciPswShutdownAllowed` before its hand-back.

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

From task 50.34 `test\test_psw.c` also holds section 21.10's host vectors
(the Setup tables, the 1296 combinations, the PE and image readers, the
setting, the guard over a modelled registry, the record and its scope),
and `test\test_trpsw.c` the USB Tree's words for each verdict.

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

The readings of section 21's code are `runs/run-50.md`, "50.34": the
`qemu` flavour's stand-in and the host only; the bench on a listed board
follows the cut (decision 32).

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
   register-layout check refuses it (one was considered; the owner chose
   to inform rather than guard).
8. Under 2 the Sony exemption and the refusals on an unreadable identity or
   subsystem are kept.
9. At every value the release writes only the registers a route wrote,
   tracked per register over one started lifetime.

Taken in this revision, not separately decided: only an exact 2 bypasses;
a write not confirmed accepted is not in the set; a release does not empty the
set; the decision and the set move into the pure core (section 7a).

Revision 4 (owner, 2026-10-09, roadmap-hcd task 36.42, after GitHub
issue 17's comment 6082858626):

10. The default is off: both INFs write 0, and the driver reads absent, a
    value of another type or an unreadable one as 0. This reverses decision
    3's default; 1 and 2 keep their meanings.
11. A user script sets 0 or 1 on every xhci98 controller and says a restart
    is needed (section 5a). An existing install keeps its value under the
    don't-overwrite flag, and the release notes tell such a user how to use
    the script.

Taken in this revision: the script is Windows Script Host VBScript and
finds the key by `DriverDesc` (section 5a); it never writes 2.

Revision 5 (owner, 2026-10-11, roadmap-hcd task 36.56, at the E7250
bench):

12. `portsw-on.bat` and `portsw-off.bat` ship beside `portsw.vbs` in every
    package directory and set 1 or 0 through it, in the re-cut of
    `2.3.0.0`, with no change to a driver binary. There is no batch file
    for 2: it stays a hand edit.

Revision 6 (owner, 2026-10-12, roadmap-hcd task 37.5):

13. The script and its two batch files are published once, in `portsw\` at
    the version root, and in no flavour directory; `make-package.ps1`
    stops staging them, so a development package carries none either (a
    guest leg copies them from `src\` or from a cut's `portsw\`). A
    published release is not rewritten: `2.3.0.0` keeps its four copies.

Revision 7 (owner, 2026-10-13, roadmap-hcd task 38.9):

14. The release's tools sit in one `tools\` directory at the version root:
    the script and its two batch files in `tools\portsw\`, beside
    `tools\xhciqual\` and `tools\xhcisnap\`, so the version root holds only
    the driver directories, `readme.txt` and `LICENSE`. First in
    `2.4.0.0`. A published release is not rewritten: `2.3.1.0` keeps
    `portsw\` at its version root, and `2.3.0.0` its four copies.

Revision 8 (owner, 2026-11-14, roadmap-usb4 task 49.47):

15. The default is on for every Windows, on the listed controllers: both
    INFs write 1. The owner was told first that `2.1.1.0` and `2.2.0.0`
    shipped it on and why revision 4 turned it off, and reversed that.
16. An existing install keeps what is set: the don't-overwrite flag stays.
17. A value missing, of another type or unreadable is off, on a listed
    controller too (owner, 2026-11-14, a second decision replacing the
    same day's "on"): text-mode Setup, which writes no value, keeps the
    ports where the firmware put them, as every release so far (one
    constant in `xhci_psw.c`).
18. The download's `tools\portsw\` is removed; Setup gets no new option.

Revision 9 (owner, 2026-11-14, replacing decision 15):

19. The INFs write 0 and a value missing, of another type or unreadable
    is off, as in `3.4.3.0`: an integrated install must not move the blue
    ports during Windows Setup, where an install drive may be on one.
20. Setup proposes 1 at a first install on a computer where it finds a
    listed controller: the console question's Enter and the window's row
    give 1, and an unattended run (`/yes`) writes 1. An update keeps what
    is set; 2 stays a hand edit or `/set`. No new page, option or switch.
21. `tools\portsw\` stays removed (decision 18).

Revision 10 (owner, 2026-11-17):

22. The switchover's default stays off: the INFs keep writing 0, and
    decisions 19 to 21 stand. The trial that wrote 1 is not taken.

Revision 11 (owner, 2026-11-17, later the same day, given by dialog;
roadmap-usb4 task 50.34):

23. In the owner's words: "when Windows setup marker is in progress don't
    switch the ports. Only switch after marker is done. Allow registry
    override if read wrongly." That is: the INFs may write the switchover
    on; the driver makes no switchover write at a start where Windows'
    Setup values say Setup is in progress, and switches at a later start
    once they no longer do; and a registry value lets the user override
    that reading in either direction. It replaces decision 22's default
    once its code is merged; until then decision 22 describes the driver.

Taken in this revision as the design's reading of decision 23 (not
separately decided): the four values of section 21.3
and how each is read, the decision latched for a started lifetime, and
the setting of section 21.4. Everything the design adds beyond the
owner's sentence is a question in section 21.11, not a decision.

Revision 12 (owner, 2026-11-17, the same day, given by dialog: his
answers to revision 11's questions, section 21.11):

24. Q1: a Setup value that cannot be read holds the switchover - the
    main value (`SystemSetupInProgress`) missing, a failed read, any value
    of another type. The three secondary values being absent is normal
    and does not hold.
25. Q2: both further checks. Hold in Windows PE; and on Vista and later
    hold until Windows' image state reads complete (`ImageState`
    `IMAGE_STATE_COMPLETE`); an image state that cannot be read yet holds.
    `SetupType` is described as a conservative veto that can hold for
    good on a stale value, not as "Setup in progress".
26. Q3: yes. On Windows 2000 and later no controller switches in the
    Windows session in which the switchover was first turned on for it;
    it switches from the next Windows boot - a real boot boundary, not a
    PnP start.
27. Q4: on 98 SE and ME the INF writes 1 too, with no Setup hold (there
    are no such values) and no first-session guard: the ports switch at
    the driver's first start. In the owner's words: "Win 98 has no special
    condition. most people install the drivers later."
28. Q5: the setting has three values: 0 automatic, 1 bypass (the Setup
    check and the guard), 2 always hold; the result and the USB Tree say
    which key supplied the value in force.
29. Q6: the guard's memory is its own per-controller value, written by
    the INF at its starting state and listed in `tuning.md` like every
    other value; the result record is output only and never read back.
30. Q7: the shutdown hand-back (`XhciIntelShutdownToEhci`) is suppressed
    while the lifetime is held: a held lifetime writes nothing to the
    routing registers at all, at shutdown included.
31. Q8: the strings stay drafts for the owner, corrected where they
    promised more than the evidence.
32. Q9: the code and the INFs' "on" ship in this release (both INFs,
    every section, write `XhciIntelPortSwitch` 1); the owner's bench
    reading on a real listed Intel board comes after the cut, and a
    failure there is a re-cut.

Revision 13 (2026-11-18, on the probe of section 21.2):

33. The owner decided this in advance of the probe's result (owner,
    2026-11-18): an image state not readable at the start holds, and the
    bypass setting is the way out.

Revision 15's questions (owner, 2026-11-18; section 21.11):

34. Q11: (a), Alternative A - the volatile registry key, proven at every
    start, the guard's value armed only on that proof (section 21.5.1).
35. Q12: (a), the decided rule kept for this release - an image state
    not readable at the start holds, and the bypass setting is the way
    out.
36. Q13: (a), the exception granted for `ReadyAtStart`, the volatile
    value the driver creates and reads, `tuning.md` naming the key and
    the value.

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

Done at task 35.6 (2026-10-07), each item above: the README's Tuning row
and switchover subsection and the release notes' switchover section and
`XhciIntelPortSwitch` table carry section 4a's warning word for word, with
1 named as everyone else's setting, and the release notes and the README
say a stop gives back only what a route wrote; the readme template's
section 9 the same; `hardware-testing.md`'s Intel section; `failure-diagnosis.md`
row 7; `source-files.md`.

## 14. What changes in the documents (revision 4, task 36.42)

- The README's registry table and switchover subsection, and its package
  listing: the default 0, the script, and the update that keeps a 1.
- The release notes: the switchover section (off by default, "Turning it
  on, or off again: `portsw.vbs`"), the update section, the registry
  settings' introduction and the `XhciIntelPortSwitch` table, the known
  limitation on a blue connector's device, and section 4a's last sentence.
- The readme template (`make-release.ps1`): the same in section 9, the
  limitation, and `portsw.vbs` in each directory's contents.
- `xhci-programming.md`'s `XUSB2PR` section; `failure-diagnosis.md` row 7;
  `release-acceptance-test.md` 1.2 and 8.7; `xhciqual/hardware-testing.md`'s
  Intel section; design record 13's value rows; `source-files.md` for
  `portsw.vbs`; `build-and-test.md` for the package's new file and the
  script's legs.

## 15. What changes in the documents (revision 5, task 36.56)

- The README's switchover section and package listing: the two batch files,
  Run as administrator on Vista and 7, and the symptom that sends a user to
  them - a USB 3 stick that copies at USB 2.0 speed on a 7-, 8- or 9-series
  machine.
- The release notes' switchover section and package contents, the same.
- The readme template (`make-release.ps1`): the same in section 9, and the
  two files in section 3 and in each directory's contents.
- `source-files.md` for the two files; `build-and-test.md` for the
  package's new files and their legs.

## 16. What changes in the documents (revision 6, task 37.5)

- The README's download table and switchover section: `portsw\` at the
  top level, out of the driver directories' listing, and where to run the
  scripts from (`2.3.0.0` named as the release that has them per flavour).
- The release notes: the package contents under "Installing", the update
  section's `portsw-off.bat`, the switchover section's script and batch
  files, and the registry table's `XhciIntelPortSwitch` row.
- The readme template (`make-release.ps1`): `PORTSW\` in the contents list
  in place of the two lines in each directory, section 3's file table, and
  section 9's "beside the INF in each RELEASE and DEBUG directory".
- `release-acceptance-test.md` step 2 (2.1, and 2.6 for the layout);
  `build-and-test.md`'s script and batch-file legs; `source-files.md` for
  the three files; `xhciqual/hardware-testing.md`'s and
  `xhci-programming.md`'s "in the package".
- The batch files' own words: their comment says where `portsw.vbs` is,
  and the "not found" message sends the user to the download's `PORTSW\`
  directory rather than "the package directory".

## 17. What changes in the documents (revision 7, task 38.9)

- The README's download table, the qualifier's folder and the switchover
  section: `tools\portsw\`, `tools\xhciqual\` and `tools\xhcisnap\`, with
  the releases before `2.4.0.0` named as having the tools at the top level (`portsw\` only in `2.3.1.0`; `2.3.0.0` per flavour directory, earlier none).
- The release notes: the package contents under "Installing", the
  qualifier's directory, the update section's `portsw-off.bat`, the
  switchover section's script, the registry table's `XhciIntelPortSwitch`
  row, and `XHCISNAP.EXE` in "The log, and how to send one"; the sections
  of published releases ("What `2.3.1.0` changes") stay as they are.
- The readme template (`make-release.ps1`): `TOOLS\XHCIQUAL\`,
  `TOOLS\XHCISNAP\` and `TOOLS\PORTSW\` in the contents list and wherever
  it names the tools' directories (the checker's steps, the log section,
  section 9).
- `release-acceptance-test.md` step 2 (2.1, 2.2 and 2.6), 3.1 and step 8;
  `build-and-test.md`'s package layout and the script and batch-file legs;
  `source-files.md` for the three files and the two tools;
  `xhciqual/README.md`, `xhcisnap/README.md`,
  `xhciqual/hardware-testing.md`'s and `xhci-programming.md`'s "in the
  package".
- Run records keep the paths they read.

## 18. Revision 8: the default on again, the scripts removed (task 49.47)

**History** (each from the record named; no new reading):
- Revision 1, `2.1.1.0`: on by default; read on the B490 under 98 SE with
  NUSB (section 10; runtime, `runs/run-34.md`, 34.3-H2).
- Revision 4, `2.3.0.0`: off by default, after a comment on GitHub issue 17
  that a connector left unrouted stays on EHCI and works there (record).
  No fault of the route or the release was reported then or since
  (`docs/issues/`, `runs/`, `releases/history.md`; record).
- Revision 5: at the E7250 bench (`9CB1`, 7 x64) a USB 3 stick copied at
  USB 2.0 speed at the default 0 and at SuperSpeed at 1 (runtime,
  `runs/run-36.md`).
- Issue 27 (an HP with `1E31`, XP): the blue ports reached the xHCI only at
  1; that firmware routes them for Windows 8 and later only (runtime).
- Issue 29 (the same HP, XP x64, at 1): a device's SuperSpeed link failed on
  the switched connectors; `XhciSsFlapFallback` sends such a device to USB
  2.0 since `3.4.3.0` (runtime).

**What changes:** new installs on the six listed controllers (Setup, Device
Manager, the hardware wizard, an integrated image) route at every start;
an update keeps its value; F6 text-mode Setup, which writes no value, does
not route, and neither does a value deleted by hand (decision 17).

**The risk it brings back** (from sections 6 and 10; unread where said):
- On 98 SE and ME with NUSB's or another added USB 2.0 stack, and on 2000 and
  later under Windows' own EHCI driver, a device on a blue connector moves
  to this driver at every start and back at every disable, removal and
  shutdown; a drive busy at a move loses its letter (section 6). On a stock
  98 SE or ME the EHCI has no driver, so those connectors gain a working
  driver.
- Text-mode Setup from the F6 floppy does not route (decision 17); the
  first start of the installed Windows, whose controller GUI-mode Setup
  installed from the INF, does, so a USB keyboard or disk on a blue
  connector moves then (unread).
- Every return to D0 routes again; a drive the EHCI driver found during the
  wake can be moved (unread: no standby has been read with the switchover
  on, section 10).
- More periodic devices land on the 7-series xHCI (`1E31`), whose periodic
  limit the release notes name.
- Unchanged: the shutdown hand-back (`XhciIntelShutdownToEhci`), the Sony
  exemption, value 2's rule, the release at every stop.

**The scripts.** From `3.5.0.0` no cut publishes `tools\portsw\`
(`make-release.ps1`: `$pswRetiredVersion`, `Assert-PortSwitchLayout`);
published releases keep theirs and the upload assembly reads them so. The
Settings window offers 0 and 1, as the scripts did; it writes the
machine-wide value where one is set and the scope is every controller,
otherwise each present controller's own key (record 25 section 22).

## 19. Revision 9: off in the INFs, on through Setup (task 49.47)

**First install**, in Setup's terms: a present controller that
`XhciPswGate` admits gets xhci98 in this run with no xhci98 driver now
(action "install"; a controller taken from another manufacturer's driver,
task 49.44, counts too), nothing is set machine-wide, and no xhci98
controller's own key, present or not, holds a DWORD of the value
(`inst_keepbot_seed_all`'s test, so a dock unplugged now keeps its 0).
Anything else is an update and keeps what is set: a same-version
reinstall or repair (the controller already runs xhci98, so its key
holds the INF's 0), an install over a newer release with `/force`, and a
computer whose machine-wide value is set. `/set XhciIntelPortSwitch=0`
(or the window's 0, or a typed 0) is an answer and wins over the
proposal; `/set ...=2` too. The written 1 is machine-wide, so the
uninstall removes it with the other values. It takes effect at the next
start: on 2000 and later the controller starts under the INF's 0 during
the run, and the run asks for the restart.

**Written only after the install.** The proposal stays apart from an
answer to the end and is written after the run's results check
(`psw_write_proposed`), only where no takeover of the run stopped at its
failure and that check counted a listed
controller whose install call in this run succeeded (`inst_psw_counts_nt`,
`inst_psw_counts_9x`, `inst_psw_write_now`). On 2000 and later that is
a controller whose service is xhci98 and which is started as the check
wants it, its root hub found below it, or waits for the restart its install asked for (problem 14, a
replaced controller's restart under task 49.44, 32-bit Vista and 7's
helper restart, an update running the old file until then). On 98 SE
and ME it is a controller the helper installed: started with this
release, its file swapped at the next start, or "installed, restart
pending". A refused, failed or cancelled install, a run that installs
no listed controller, a run stopped part-way and a takeover stopped at
its failure write nothing for it; a controller left to the steps page
on 98 SE and ME is not counted either. What the console and the window
showed stays as shown, and no "set to 1" line is printed then.

Which real users it changes: a first install by `SETUP.EXE` on a board
with one of the six listed controllers (the 7-, 8- and 9-series and
C610/X99 PCH), on every Windows; on 98 SE and ME the console's question
was already asked there. Nothing writes 1 without a listed controller:
the proposal needs `XhciPswGate` to admit a present controller this run
installs.

## 20. Revision 10: the emulator's stand-in, and the default kept off

**The aid.** `XhciQemuPswModel` (`hcd_ctl.c`, `hcdPswQemuRead`,
`hcdPswQemuWrite`, `hcdPswQemuStart`), in the `qemu` flavour alone, its
code, its controller-extension fields and its value name under
`XHCI_FLAVOUR_QEMU`, so the release image and its extension's layout are
as before. With a DWORD 1, read machine-wide or from the controller's key
as every value is, on QEMU's own controller (`1B36:000D`), the
switchover's configuration-space callbacks, and no other reader's, see an
Intel 7-series xHCI (`8086:1E31`), with `D0h` to `DCh` held in the
controller's extension; no write reaches QEMU's configuration space, and
`XhciIntelPortSwitch` decides as on hardware. Nothing moves by itself: a
harness outside the driver moves emulated devices between an emulated
EHCI controller and the xHCI controller on the route's and the release's
trace lines. With the model on, each start also traces four values under
`HKLM\SYSTEM\Setup` (`SystemSetupInProgress`, `SetupType`,
`OOBEInProgress`, `MiniSetupInProgress`: registry state, query status,
whole value). `build-and-test.md`, "The qemu flavour's test aids", is
the reference: the keys and their precedence, when it is on, how long
the modelled registers live, the trace's lines and their limits.

**The decision.** Decision 22: the default stays off. Revision 9's reason
holds: an install from the INF during Windows Setup must not move the
blue connectors, where an install drive may be on one.

**What the emulator legs read** (2026-11-17, runtime, the `qemu` flavour
on 32-bit guests; the package a trial whose INFs wrote
`XhciIntelPortSwitch` 1, which is not in the tree, with
`XhciQemuPswModel` 1 in the controller's key). These are emulator
readings under the model's limits: a modelled register file and devices
moved by a harness, not a board's multiplexer. They say nothing about
real boards, whose firmware, timing and devices were not read.

- Text-mode Setup from the F6 floppy never routed: `txtsetup.oem` writes
  no value.
- XP and 2000 (F6): the first start of the first GUI-mode boot read no
  value either; GUI-mode Setup then installed the controller from the INF
  at "Installing Devices" and restarted it, and that start read 1 and
  routed, about 71 to 85 s into that boot. A moved keyboard kept working
  on both, and on XP the moved install CD stayed readable with no disc
  prompt. 2000's text mode could not use a USB CD (STOP `0x7B`), so its
  legs used an IDE CD, which does not move.
- Vista and 7 with the driver staged for the installed system (the answer
  file's `PnpCustomizationsNonWinPE`): the specialize boot's start read
  no value; the OOBE boot's start read 1 and routed. A moved keyboard and
  DVD worked there. 7 installed without the driver and given it at the
  desktop routed at the restart its install asked for.
- Vista and 7 with the driver loaded into Windows PE (the answer file's
  `PnpCustomizationsWinPE`, or Load Driver at the disk page): the first
  start in PE routed at once, and the PE phase carried on copying from
  the moved DVD - with one exception.
- **The one failure:** Vista with the driver given to PE by the answer
  file. After the move, Setup showed "A required CD/DVD drive device
  driver is missing" although the DVD was back under this driver; read
  twice. Choosing the driver again in that dialog let Setup find the
  media and go on, without the answer file's image choice. 7 on the same
  path, and Vista through Load Driver, did not fail.
- `SystemSetupInProgress` read 1 at the routing start in XP's and 2000's
  GUI-mode Setup and in PE, and 0 at the Vista and 7 OOBE boot (with
  `SetupType` 2 and `OOBEInProgress` 1). This is what those boots showed,
  not a timeline of Setup, and no value read here marks a moment at which
  moving a connector is safe.
- Controls with the INFs' 0 never routed. Not read: 98 SE and ME, an XP
  image integrated with the INF, Vista given the driver at the desktop,
  any real hardware.

## 21. Revisions 11 to 15: the switchover held while Windows Setup runs (task 50.34)

A design written before any code: revision 11 (2026-11-17) the design,
revision 12 (2026-11-17) the owner's answers, revision 13 (2026-11-18) a
probe of what a start can read, revision 14 (2026-11-18) the guard as
two alternatives and the Windows 10 hazard, revision 15 (2026-11-18)
the alternatives' contracts made exact so that either can be coded
from. The code is task 50.34's (branch `w50-pswhold`, `runs/run-50.md`,
"50.34"); until it is merged into the release branch both INFs there write
0 and decision 22 describes that branch's driver. The user-visible
strings in it are drafts for the owner to word.

**The owner's answers of 2026-11-18** (section 21.11; section 11,
decisions 34 to 36): **Q11**, the guard is section 21.5's Alternative A,
the volatile registry key, and Alternative B (a flag in the driver
image's memory) stays in this record as the alternative that was
weighed; **Q12**, automatic mode stays as decided for this release where
the image state is read at some boots' start and not at others;
**Q13**, the exception to the standing rule is granted for the one value
Alternative A's driver creates and reads. Nothing in this section awaits
the owner.
The owner decided this in advance of the probe's result (owner,
2026-11-18): an image state not readable at the start holds, and the
bypass setting is the way out.

**The release number, once.** The release this task is for is cut as
`3.6.0.0` (roadmap-usb4 Phase 50); until 50.final the documents, the
INF gate's table and the branch said `3.5.1.0`, and 50.final changed
them together, the branch renamed `3.6.0.0` at the cut. Every `3.6.0.0`
below - the gate's `Since` and `Was` included - is that release.

### 21.1 What is asked, and what was decided

Decision 23 is the owner's sentence: "when Windows setup marker is in
progress don't switch the ports. Only switch after marker is done. Allow
registry override if read wrongly." It is for a user who integrates the
driver into a Windows install image (an image integration tool, Windows'
own image servicing, an answer file's driver path): Windows Setup
installs the controller from the INF, `SETUP.EXE` never runs, and so
nothing writes the 1 that revision 9 leaves to Setup. Revision 9 kept
the INFs at 0 because an install from the INF during Windows Setup moves
the blue connectors, where the install source may be (section 20 read
it once: Vista, with the driver given to Windows PE by an answer file).

The owner's answers to revision 11's questions (section 21.11; section
11, decisions 24 to 32) decide the rest. In short:

- every INF section writes `XhciIntelPortSwitch` 1 in this release, 98 SE
  and ME included, and the owner's bench reading on a listed board comes
  after the cut, a failure there a re-cut (section 21.8);
- on 2000 and later a start makes no switchover write while Windows'
  Setup values say Setup is in progress, while one of them cannot be
  read, in Windows PE, and on Vista and later until Windows' image state
  reads complete; an image state that cannot be read at the start holds,
  with no remembered reading, and the setting's 1 is the way out where it
  is never readable at a boot's start (section 21.3);
- on 2000 and later no controller switches in the Windows session in
  which the switchover was first turned on for it, a real boot boundary
  and not a PnP start (section 21.5, Alternative A, the owner's answer to
  Q11);
- a held lifetime writes nothing to the routing registers at all, the
  shutdown hand-back included (section 21.6);
- on 98 SE and ME nothing is held: no Setup value exists there, and the
  ports switch at the driver's first start (section 21.8);
- a setting with three values overrides the reading: 0 automatic, 1
  bypass, 2 always hold (section 21.4);
- the guard keeps its memory in a value of its own that the INFs write;
  the result record is output only and never read back (sections 21.5
  and 21.7).

Revision 13 removes revision 12's remembered image state (the guard's
value 2 and the once-a-minute read during a lifetime): a reading from an
earlier session is not a current reading. The probe of section 21.2
found the image state unreadable at the first start of every boot read
on the Vista bases and of four of the six read on the 10 base; such a
start holds, as decision 25 says, with the bypass setting as the way
out.
The owner decided this in advance of the probe's result (owner,
2026-11-18): an image state not readable at the start holds, and the
bypass setting is the way out.

**What is relied on until the bench.** Emulator evidence only: every
reading this design rests on is the `qemu` flavour's, with a modelled
multiplexer and devices moved by a harness (section 20). No real board
has run any of it. The INFs' 1 ships on that evidence, by the owner's
decision, with the bench after the cut (section 21.10).

### 21.2 What the values are, and what is known of them

**The evidence boundary.** No document read for this design gives a
universal timeline of when Windows sets and clears its Setup values
relative to a driver's start, and none says that a value read 0 means
moving a connector is safe. Microsoft's Knowledge Base articles 287506
(Windows 2000), 321070 (XP) and 814588 (Server 2003), on Sysprep, say
that preparing an image sets `SystemSetupInProgress`, `MiniSetupInProgress`
and `SetupType` to 1 before the reboot into Mini-Setup, and describe the
recovery of a machine an interrupted preparation left in that state (as
read for the assessment of the trial, 2026-11-17; not re-read here).
Microsoft's page "Windows Setup States" documents the image state:
`ImageState`, a `REG_SZ` under
`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Setup\State`, whose value
`IMAGE_STATE_COMPLETE` means "the specialize and oobeSystem configuration
passes are complete", the other states naming a Setup phase not yet
complete or an image resealed for a later one (the page's current edition
is labelled for Windows 10 and 11; the probe below read the same value,
`IMAGE_STATE_COMPLETE`, on Vista, 7, 8, 8.1 and 10). The values are a
hint that
Windows gives, not a contract; this design rests on them only where the
emulator read them, and holds where it cannot read them.

**What was read.** The trial of section 20 (2026-11-17, runtime, the
`qemu` flavour's trace at each start with `XhciQemuPswModel` 1, 32-bit
guests; that trace's first encoding, which kept the registry state and
the value but not the query's status) read the four Setup values at
each controller start:

| Where (section 20's legs) | `SystemSetupInProgress` | `SetupType` | `OOBEInProgress` | `MiniSetupInProgress` |
|---|---|---|---|---|
| XP and 2000 GUI-mode Setup, the start after the INF's install | 1 | 0 | absent | absent |
| Vista and 7, Windows PE (the answer file, or Load Driver) | 1 | 0 | absent | absent |
| Vista and 7, the OOBE boot of the installed system | 0 | 2 | 1 | absent |
| 7, the first boots after Setup (the store leg's restart, the desktop install's restart) | 0 | 0 | 0 | absent |

**What a start can read (revision 13's probe).** A start-time probe in
the `qemu` flavour, on a branch that is never merged
(`w50-pswhold-probe`, commit `1427bb1b4`), logged at every start of the
controller, before the rest of the switchover's step, the status, call
count, type and length of five queries made as section 21.3 makes them:
the `SOFTWARE` hive's existence test, `ImageState`, the `MiniNT` key's
existence test, `SystemStartOptions` and `SystemSetupInProgress`. Read
2026-11-17 and 18 (runtime, the port `0xE9` trace) on the rig's
installed q35 bases with QEMU's `qemu-xhci` controller; the driver put on
it by `devcon update` from the staged package (32-bit) or by `pnputil`
after an in-guest throwaway test signature (64-bit); one guest at a
time. "First start of a boot" is the controller's start in a boot where
it was the only one before the desktop: after a restart (the restart the
install asks for, or `shutdown /r`) and after an ACPI power-off and a
fresh power-on of the virtual machine. "In session" is a `devcon restart`
of the controller from the desktop (32-bit only: the 64-bit `devcon` is
not in the toolchain).

The `SOFTWARE` hive and `ImageState` at each start read ("loaded" is the
hive test's one call and success; then `ImageState` was one call,
`REG_SZ`, success, the length of its string and terminator; "not
loaded" is no call and `0xC0000034` for the hive test and for
`ImageState` alike):

| System (the q35 base) | First start after the install's restart | First start after a power-on | Further power-ons (first start each) | In session (`devcon restart`) | First start after a restart with `ImageState` set to `IMAGE_STATE_UNDEPLOYABLE` |
|---|---|---|---|---|---|
| Vista SP2, 32-bit | not loaded | not loaded | - | loaded, `IMAGE_STATE_COMPLETE` | not loaded |
| Vista SP2, 64-bit | not loaded (the install's own start, in session: loaded, `IMAGE_STATE_COMPLETE`) | not loaded | - | - | not loaded |
| 7 SP1, 32-bit | loaded, `IMAGE_STATE_COMPLETE` | loaded, `IMAGE_STATE_COMPLETE` | loaded (2 of 2) | loaded, `IMAGE_STATE_COMPLETE` | loaded, `IMAGE_STATE_UNDEPLOYABLE` |
| 7 SP1, 64-bit | loaded, `IMAGE_STATE_COMPLETE` (the install's own start, in session: the same) | loaded, `IMAGE_STATE_COMPLETE` | - | - | loaded, `IMAGE_STATE_UNDEPLOYABLE` |
| 8, 32-bit | loaded, `IMAGE_STATE_COMPLETE` | loaded, `IMAGE_STATE_COMPLETE` | - | loaded, `IMAGE_STATE_COMPLETE` | loaded, `IMAGE_STATE_UNDEPLOYABLE` |
| 8.1, 32-bit | loaded, `IMAGE_STATE_COMPLETE` | loaded, `IMAGE_STATE_COMPLETE` | - | loaded, `IMAGE_STATE_COMPLETE` | loaded, `IMAGE_STATE_UNDEPLOYABLE` |
| 10 22H2 (19045), 32-bit | not loaded | not loaded | not loaded at 2, loaded at 1 (of 3) | loaded, `IMAGE_STATE_COMPLETE` | loaded, `IMAGE_STATE_UNDEPLOYABLE` |

The other queries, at every start read: the `MiniNT` test - no call and
`0xC0000034`, not PE, on every system; `SystemSetupInProgress` - a
four-byte `REG_DWORD` 0 taken with success; `SystemStartOptions` - one
call, `REG_SZ`, success: Vista `/NOEXECUTE=OPTIN` (a leading slash, no
leading space), 7 ` NOEXECUTE=OPTIN` (a leading space), 8 and later
` TESTSIGNING  NOEXECUTE=OPTIN` where test signing is on (two spaces
between the tokens). The existence test of a key that is there (the
`SOFTWARE` hive, where loaded) gave one call, `REG_NONE` of no data, and
success, which is what section 21.3's `NOVALUE` tests take as "there".

What this does not read: a Setup start (PE, specialize, OOBE), a first
start of the controller by Windows' own Setup rather than by an install
from the desktop, 2000, XP and XP x64 (no image state there), 64-bit
8 to 10 (their own USB 3 driver holds the controller, and the rig has no
64-bit tool that forces another driver on it), and real hardware, whose
start order relative to the `SOFTWARE` hive may differ from the
emulator's (the host carried other guests' load while the probe ran,
which moves such timing too). Why the hive comes after the controller's
first start on Vista always and on 10 at some boots, and before it on 7,
8 and 8.1 at every boot read, is not read; nothing here says it is
always so on any system.

### 21.3 The gate: the values, how each is read, and the rule

**Where and on which systems.** All of it on Windows 2000 and later only
(the systems `IoIsWdmVersionAvailable(1, 0x10)` names), both
architectures; the image state on Vista and later only
(`IoIsWdmVersionAvailable(6, 0)`). On 98 SE and ME nothing here is read
(section 21.8). Every read goes through `RtlQueryRegistryValues`, already
imported; the only new routine is section 21.5's.

**The Setup values**, `\Registry\Machine\SYSTEM\Setup`, each a
`REG_DWORD`:

| Value | Role | Why |
|---|---|---|
| `SystemSetupInProgress` | the primary: it must be read, and read 0 | 1 in XP's and 2000's GUI-mode Setup and in Windows PE, the routes where the trial routed during Setup and where the one failure was; named by all three Sysprep articles |
| `OOBEInProgress` | secondary, "in progress" when not 0 | 1 at the Vista and 7 OOBE boot, where `SystemSetupInProgress` read 0 |
| `MiniSetupInProgress` | secondary, "in progress" when not 0 | named by the Sysprep articles for Mini-Setup; absent wherever read |
| `SetupType` | secondary, **a conservative veto** when not 0 | named by the Sysprep articles (1 there); 2 at the Vista and 7 OOBE boot, 0 elsewhere. Its numbers are not a documented completion flag, so a non-zero `SetupType` is not called "Setup in progress": it holds because it is not 0, and a value Windows leaves set after Setup on some system would hold there at every start until the setting's 1 (section 21.4) |

**How each Setup value reads.** The reader is the `qemu` model's
(`hcdPswQemuSetupValue`, section 20), moved into every flavour, with a
query routine of its own that records every call - its type and length
as given - rather than `hcdTakeDword`'s, which folds an empty `REG_NONE`
into "absent" (`XhciRegCallSeen`). The table entry is
`RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_NOEXPAND` with a zeroed
default, and the query's own status is kept beside what the routine saw:

| The routine saw | The query returned success | `STATUS_OBJECT_NAME_NOT_FOUND` (`0xC0000034`) or `STATUS_OBJECT_PATH_NOT_FOUND` (`0xC000003A`) | any other status |
|---|---|---|---|
| one four-byte `REG_DWORD`, value v | **taken, v** | unread | unread |
| an empty `REG_NONE` (type 0, length 0) | malformed | malformed | malformed |
| another type or length, or more than one call | malformed | malformed | malformed |
| nothing | unread | **absent** | unread |

Taken requires both: the routine took the DWORD and the query succeeded;
a DWORD taken by a query that then failed is unread, and both
observations are kept for the record. An empty `REG_NONE` is a value
that is there, malformed, not an absence: with a zeroed default and
`RTL_QUERY_REGISTRY_REQUIRED`, Microsoft's documentation of
`RtlQueryRegistryValues` has a missing value call no routine and return
`STATUS_OBJECT_NAME_NOT_FOUND`, so a call is a stored value; the probe
of section 21.2 read exactly that for a missing key (`MiniNT`: no call,
`0xC0000034`) on every system it ran on. (A system found to call the
routine for a missing value would hold there until the rule is revised,
which is the safe direction.) A missing key reads absent for all four.

What each state means:

| State | The primary | A secondary |
|---|---|---|
| taken, 0 | finished | no objection |
| taken, not 0 (1, 2, `0xFFFFFFFF` alike) | holds: in progress | holds: in progress (`SetupType`: the veto) |
| absent | holds: unread | no objection - absence is normal (`MiniSetupInProgress` everywhere, `OOBEInProgress` in XP's, 2000's and PE's Setup) |
| unread, malformed | holds: unread | holds: unread |

**Windows PE (2000 and later).** No Microsoft reference page read for
this design names a Windows PE marker that a driver can read, so the
design takes two, either of which holds, each with its documentation
stated:

- the key `HKLM\SYSTEM\CurrentControlSet\Control\MiniNT` exists. Windows'
  own servicing tools act on it as Windows PE's mark (answers on
  Microsoft's question site, about the image servicing tool's "does not
  support servicing Windows PE", tell a user to delete it from a full
  system); no reference page states it. Read
  as an existence test: `RtlQueryRegistryValues` relative to
  `RTL_REGISTRY_CONTROL`, path `MiniNT`, one entry with a NULL name and
  `RTL_QUERY_REGISTRY_NOVALUE`. Exactly one call and success: PE, hold.
  No call and `STATUS_OBJECT_NAME_NOT_FOUND` or
  `STATUS_OBJECT_PATH_NOT_FOUND`: not PE. Anything else (another call
  count, another status, a call with a failed status): unread, hold.
  The probe read this test's two outcomes: a key that exists gave one
  call (`REG_NONE`, no data) and success, a missing one no call and
  `0xC0000034` (section 21.2);
- the boot options in `HKLM\SYSTEM\CurrentControlSet\Control`, value
  `SystemStartOptions` (a `REG_SZ`), contain the token `MININT`. The
  boot configuration element `winpe` is documented ("Enables the
  computer to boot to Windows PE", Microsoft's "BCDEdit /set"); that it
  reaches the running system as that token in that value is not stated
  there and is read on the legs. Absent: not PE (its presence is itself
  unread on a PE). Otherwise the string reader below decides.

**The two string readers** (`SystemStartOptions`; `ImageState`). Each is
one table entry, `RTL_QUERY_REGISTRY_REQUIRED |
RTL_QUERY_REGISTRY_NOEXPAND` with a zeroed default, and a query routine
of its own that counts its calls and copies what it is given into a
fixed buffer (512 characters for the boot options, 64 for the image
state). A reading is **valid** only when all of these hold:

- the query returned success and the routine was called exactly once;
- the type is `REG_SZ` (`NOEXPAND` keeps a `REG_EXPAND_SZ` its own type,
  which is therefore not valid, and never expanded);
- the length in bytes is even, at least 2, and within the buffer;
- the string's last character is the terminator and the only one: a
  terminator before the end (an embedded one, or data after the string)
  is not valid, nor is a length that ends without one.

Anything else - another call count, another type, an odd or oversized
length, a missing or embedded terminator, a failed status, a string
taken by a routine whose query then failed - is **malformed**, and a
malformed reading holds: it never reads as "not PE" or as "complete".
Absent (no call, and `STATUS_OBJECT_NAME_NOT_FOUND` or
`STATUS_OBJECT_PATH_NOT_FOUND`) is its own outcome, as each check says.

- **The token `MININT`.** The valid string is split into tokens at spaces
  and tabs; each token, with one leading `/` removed, is compared whole,
  case ignored, with `MININT`. `MININT=1`, `XMININT` and `MININTX` are not
  the token. The probe read both spellings Windows uses: Vista's options
  as `/NOEXECUTE=OPTIN` (a leading slash, no space), 7's and later as
  ` NOEXECUTE=OPTIN` (a leading space) - so neither a leading separator
  nor a slash may be assumed. Any token equal to it: PE, hold. None:
  not PE.
- **The image state.** The valid string, with no space before or after,
  compared whole, case ignored, with `IMAGE_STATE_COMPLETE`.

**The image state (Vista and later).** First the hive: the existence
test above on `\Registry\Machine\SOFTWARE` (`RTL_REGISTRY_ABSOLUTE`).
No call and `STATUS_OBJECT_NAME_NOT_FOUND`: **not loaded yet**. One call
and success: loaded, and `ImageState` under
`Microsoft\Windows\CurrentVersion\Setup\State` is read:

| Reading | Meaning |
|---|---|
| valid, `IMAGE_STATE_COMPLETE` | complete: no objection |
| valid, any other string; or the value or its key absent with the hive loaded | not complete: holds |
| malformed, or the hive test with any other outcome | unread: holds |
| the hive not loaded yet | **holds** ("not readable yet"), with no remembered reading (decision 25) |

**Where the image state is not readable at a boot's start.** The probe
(section 21.2) found the `SOFTWARE` hive not loaded at the controller's
first start of every boot it read on Vista (three on each of 32-bit and
64-bit); on 10 (32-bit) not loaded at four of the six boots read and
loaded at two; and loaded at every boot's first start it read on 7 (five
on 32-bit, three on 64-bit), 8 and 8.1 (three each, 32-bit). It is a
matter of when the hive's load and the controller's start fall in one
boot, which the probe shows varying on 10, so no system is known to be
safe from it. So:

- **On the Vista bases** every boot start read would have held under
  this rule. There the switchover would move only with the setting's 1
  (bypass), or at a later start of the controller in the same session (a
  Device Manager disable and enable, the USB Tree's restart of the
  controller, task 50.32), where the probe read the hive loaded and the
  image state complete - and only when every other check passes and the
  guard allows. That is what the probe's boots showed, not a rule of
  Vista: other machines, timings and real hardware are unread.
- **On the 10 base** the first start came before the hive at four boots
  and after it at two. Those two show the input readable, not two
  routing boots: one of them read `IMAGE_STATE_UNDEPLOYABLE`, set on
  purpose, and would have held under this rule too. On such a machine
  some boots would hold and others could route, so the same device on a
  switchable connector could be under the USB 2.0 controller at one boot
  (with the firmware on Auto) and under this driver at the next. That is
  a hazard, below, not only a fact.
- **On the 7, 8 and 8.1 bases** this check had no objection at any boot
  start read.

A routed lifetime routes again at each return to D0 (section 6) until
its stop, so a machine that sleeps rather than restarts keeps the route;
a held one stays held. The USB Tree's line and the record say exactly
which case held a start and what the user can do (section 21.7). A
system the probe did not read holds wherever its hive is not loaded at
the start, the same way. Whether automatic mode should instead behave
the same at every boot was Q12; the owner kept the decided rule for this
release (section 21.11).

**The hazard where the outcome varies from boot to boot, and the
operating restriction.** A device on a switchable connector can change
its parent controller, its driver path and its enumeration timing from
one boot to the next where this check's outcome varies (the 10 base),
and at every boot the move itself comes at the controller's start, after
Windows' USB 2.0 driver may already be using the device. For a boot or
system drive that can stop the boot or interrupt the storage under
Windows. This design does not make that safe; it is stated as an
operating restriction, in `tuning.md` and the release notes (drafts,
Q8):

- "Do not rely on the automatic port switchover for a drive Windows
  starts from, or keeps its system on, when that drive is on one of the
  shared (usually blue) connectors. Use a connector that is always on
  one controller (a black USB 2.0 one, or one the switchover does not
  move), or choose a fixed setting and check that Windows starts with it
  several times."
- "The fixed settings: XhciIntelPortSwitch 0 (off) - this driver does
  not move the connectors when it starts or when Windows wakes; they stay
  where the computer's firmware put them, except that at shutdown, on
  the Intel 7 Series and two of the 8 and 9 Series chipsets, the driver
  still hands them back to the USB 2.0 controller
  (XhciIntelShutdownToEhci). XhciIntelPortSwitch decides first: at 0
  the next setting is not read. XhciIntelPortSwitchSetup 2 (always hold)
  applies only while the switchover is on (XhciIntelPortSwitch 1 or 2):
  then this driver writes nothing to the connectors at all, at shutdown
  included. XhciIntelPortSwitchSetup 1
  (bypass) - the connectors are moved at every start where nothing
  else stops it, whatever Windows' Setup state says; it also removes the
  protection during Windows Setup, and it does not make a boot drive on
  those connectors safe."
- Recovery, where Windows no longer starts from such a drive: start
  Windows with the drive on another connector (or from other media),
  set `XhciIntelPortSwitch` to 0 or `XhciIntelPortSwitchSetup` to 2
  there (the USB Tree, `XHCITR /set`, or the registry from Windows'
  recovery tools), and restart.

**No check permits against another.** Every check above can
only hold, never permit against another: the start routes only when
every one of them has no objection and the guard allows it, or the
setting is 1.

**Four reads are not one moment.** The Setup values, the PE tests and
the image state are separate queries; nothing makes them an atomic
snapshot, so a Setup that changes its state between two of them is read
half before and half after. The decision is the start's and latched
(section 21.6); the next start reads again.

**The verdict**, in this order (the first that applies):

1. setting 2: **held by the setting**;
2. setting 1: **bypass** - routes, the guard, Setup, PE and image checks
   read and recorded but not obeyed;
3. 98 SE or ME: routes (section 21.8);
4. a Setup value taken non-zero: **held: Setup in progress**
   (`SystemSetupInProgress`, `OOBEInProgress` or `MiniSetupInProgress`,
   the first in that order), else **held: SetupType** (the veto);
5. a Setup value unread or malformed, or the primary absent: **held:
   Setup state unread**;
6. a PE test saying PE: **held: Windows PE**; one unread: **held: PE test
   unread**;
7. Vista and later: image not complete: **held: image state**; unread:
   **held: image state unread**; not loaded yet: **held: image state not
   readable yet**;
8. the guard (section 21.5) not yet allowing this session: **held: first
   session** (or **held: session unknown**);
9. otherwise: routes.

**Where in the decision.** Section 8's order is kept and this step added
last, after the board check and before the route: offset 0; vendor
Intel; `XhciIntelPortSwitch` and the mode (off: done, nothing more is
read); the subsystem and the Sony exemption; then the setting, the Setup
values, the PE tests, the image state, the guard, and the verdict. Every
read of the step is made whatever an earlier one said (they have no side
effects; the setting's 2 included), so the record is complete; the
guard's writes are section 21.5's. A non-Intel controller, an Intel one at
0 and the Sony board read and write nothing new.

**The pure core.** `xhci_psw.c` gains the classification of each reading
(from what the routine saw and the status), the verdict, the guard's
transitions and the record's builder, so the host vectors drive the rule
itself. The `XHCI_PSW_IO` callbacks gain `Override` (the setting, its
value and the key that supplied it), `Setup` (the four readings), `Pe`
(the two), `Image` (the hive test and the value), and `Session` and
`Guard` (section 21.5); flags `Nt` and `Nt6` say which are read. A
callback missing where it is needed reads as unread, so an incomplete
`io` holds. `XHCI_PSW_LIFE` gains the readings, the verdict and the
value it names, and the guard's state; `XhciPswLifeStart` takes the step,
and a hold ends the decision as `XHCI_PSW_DECIDE_SETUP_HELD` with `On`
clear.

### 21.4 The setting: `XhciIntelPortSwitchSetup`

A `REG_DWORD` (the name a draft for the owner, Q8):

| Value | Effect |
|---|---|
| absent, not a DWORD, or any number but 1 and 2 | as 0 |
| 0 (the INFs' value) | automatic: section 21.3's checks and section 21.5's guard on 2000 and later; nothing on 98 SE and ME |
| 1 | **bypass**: the checks and the guard are read and recorded but not obeyed, and the start routes; on 98 SE and ME it has nothing to bypass and acts as 0 |
| 2 | always hold: the start makes no switchover write, whatever Windows says, on every system, 98 SE and ME included |

It changes the driver's policy, not Windows' state: 1 does not say that
Setup has finished. It is read through `hcdReadDword`, as every value of
the controller - the machine-wide
`HKLM\System\CurrentControlSet\Services\USB` first, where it wins in any
type (a value of another type there reads as 0 and masks a valid value
in the controller's key), else the controller's own driver key - and the
record and the USB Tree say which of the two supplied the value in force,
for this setting and for `XhciIntelPortSwitch` alike. Only an exact 1
bypasses, as only an exact 2 bypasses the device-id list (section 5).

**Against `XhciIntelPortSwitch`.** At 0, absent or of another type the
switchover is off as today and this setting is not read. At 1 (a listed
controller) and at 2 (any Intel controller, section 4a) it applies alike;
it never widens the controller list, and the Sony exemption and the
refusals on an unread identity stand under every value of it.

**Getting out of a stale value.** An interrupted Sysprep can leave
`SystemSetupInProgress` or `MiniSetupInProgress` at 1 on a system that
then runs normally (the Sysprep articles' recovery), and a `SetupType`
left non-zero vetoes at every start; the switchover then holds at every
start. The USB Tree says which value holds and where the setting is read
from (section 21.7); the user sets `XhciIntelPortSwitchSetup` to 1 - the
USB Tree's View > Settings... with "Show advanced settings", `XHCITR /set
XhciIntelPortSwitchSetup=1`, or Setup's Advanced list - and restarts
Windows. Its confirmation (a draft): "Bypass: at this controller's next
start the shared connectors move to xHCI98 whatever Windows' Setup state
says. A drive or other device in use on them is disconnected at that
moment. Use it only when Windows Setup has finished and the connectors
still do not move." `tuning.md` says the same.

**For an image integrator** who wants the opposite: the setting's 2,
written where the driver reads it in each environment that will start
the controller - Windows PE's own registry when the driver is given to
PE, and the installed image's when it is staged for the installed system
- since a value written in one does not reach the other (section 21.9);
then set back to 0 (or deleted) once Setup has finished. `tuning.md`
says how, in general terms.

### 21.5 The guard: no switch in the session in which it was turned on

**What it must do** (owner, Q3): on 2000 and later no controller
switches in the Windows session in which the switchover was first turned
on for it; it switches from a later Windows boot. "Turned on for it" is
the first start of that controller that found the mode on and the board
passing, held or not. "Session" is one boot of Windows: from a restart,
or a power-on from off, to the next restart or shutdown. Sleep,
hibernation and Windows 8 to 10's Fast Startup ("Shut down" with it on
hibernates the kernel's session) do not end a session; only a restart,
or a shutdown with Fast Startup off, does. Every text that tells the user
to go past the guard therefore says **Restart** (section 21.7), never
"shut down and switch on".

**Its memory: `XhciIntelPortSwitchReady`** (owner,
Q6), a `REG_DWORD` in the controller's own driver key, written by both
INFs' NT sections at 0 with the don't-overwrite flag and listed in
`tuning.md` like every other value. It is read from the controller's
key only, never machine-wide (a machine-wide copy is ignored, as a UAS
drive's two values are not machine-wide). Only an exact `REG_DWORD` 1
reads as 1 ("armed"); absent, another type or another number reads as 0.
The driver writes it only from 0 to 1, never back. Called **G** below.

**The boot boundary is Alternative A** (owner, 2026-11-18, Q11; section
21.11). Revision 13's mechanism let a session's later start pass the
guard after evidence of the session was lost at an earlier start (a
helper not yet answering, a session key not yet created) while G had
been armed meanwhile; revision 14 set out two alternatives that close
that, in different ways and at different costs. The code implements A;
B (section 21.5.2) stays here as the alternative that was weighed, and
nothing of it is built.

#### 21.5.1 Alternative A: a volatile registry key, G armed only on proven session evidence

**The mechanism.** Microsoft documents `ZwCreateKey`'s
`REG_OPTION_VOLATILE` (the key is kept in memory and not preserved when
the system restarts) and its disposition, `REG_CREATED_NEW_KEY` or
`REG_OPENED_EXISTING_KEY` (Microsoft's `ZwCreateKey` reference); that
`STATUS_CHILD_MUST_BE_VOLATILE` (`0xC0000181`) is the status of an
attempt to create a stable subkey under a volatile parent (Microsoft's
NTSTATUS values, `[MS-ERREF]` section 2.3.1); and that a key that exists
is opened as it is, never converted (Microsoft's `RegCreateKeyEx`
documentation). The driver uses the
subkey `XhciIntelPortSwitchSession` of the controller's own driver key,
and in it one `REG_DWORD`, `ReadyAtStart`: G as the session's first such
start read it.

**At each start that reaches the guard** (2000 and later, mode on, board
passing), at `PASSIVE_LEVEL` in `hcdStartBody`'s decision, under no spin
lock, in this order. "Hold, no arming" ends the step: the start holds
and G is not written.

1. Read G.
2. **The routine.** x64 imports `ZwCreateKey` (an `nt52-amd64-export`
   row in its allowlist). The x86 binary cannot import it - it must load
   on 98 SE and ME, whose kernel's export of it has no evidence row
   (AGENTS.md, "Imports and the API ceiling") - so it takes the address
   at run time through the helper's routine service
   (`MmGetSystemRoutineAddress`, `XHCI_HLPR_SVC_ROUTINE`, design record
   18 section 15.3), the decision making the HCD's look at the helper
   itself (section 15.4) when it is not yet bound. Not available: hold,
   no arming.
3. **The key.** `ZwCreateKey` on `XhciIntelPortSwitchSession` under the
   driver key's handle (opened for `KEY_CREATE_SUB_KEY |
   KEY_QUERY_VALUE | KEY_SET_VALUE`), `OBJ_KERNEL_HANDLE`,
   `REG_OPTION_VOLATILE`, the session key's own handle asked for the same
   three rights. A failed call, or a disposition other than the two:
   hold, no arming.
4. **Its volatility, proven at every start.** `ZwCreateKey` of a subkey
   `V` under it with `REG_OPTION_NON_VOLATILE`: the status must be
   `STATUS_CHILD_MUST_BE_VOLATILE`. Any other outcome means the key is
   not proven volatile (made by hand, or carried by a tool that copies
   keys): hold, no arming; where the create succeeded, the child is
   closed and left (no delete routine is imported), inside a key that
   already holds every start; the Tree says to delete the key
   `XhciIntelPortSwitchSession` by hand and Restart (an administrative
   recovery, section 21.7).
5. **The session's value S.**
   - `REG_CREATED_NEW_KEY`: write `ReadyAtStart` = G, then read it back
     with section 21.3's DWORD reader; S = G only when the write
     succeeded and the read-back is that DWORD. Otherwise S is unknown:
     hold, no arming.
   - `REG_OPENED_EXISTING_KEY`: read `ReadyAtStart`; taken 0 or 1 is S;
     anything else (absent, malformed, unread, another number) is
     unknown: hold, no arming. G is not consulted on this path.
6. **Arming.** S is known here: if G is 0, write G = 1. A failed write is
   logged (`psw.guard.write`); the next session then begins with G 0
   again.
7. **The guard allows** exactly when S is 1.

Every handle opened in steps 3 and 4 is closed on every path.

**Why no session can pass early.** G turns 1 only at a start whose
session evidence is proven: a volatile key that this start created and
whose `ReadyAtStart` it wrote and read back, or an existing volatile key
whose `ReadyAtStart` was so written earlier in the same session. A
session whose first start could not establish that leaves G 0, so a
later start of the same session that does establish it creates the key
with `ReadyAtStart` 0 and holds. A non-volatile key of the name, holding
0, 1 or nothing, fails step 4 at every start. Only the next boot, whose
volatile key is new, can read S 1.

**What the proof is, and its limits.** Step 4 is a documented semantic
argument - the one status Windows gives a stable child under a volatile
parent - not an observation on every system; the code's guest legs
confirm it on each NT system with real registry operations (a volatile
key, a non-volatile key of the name made by hand, each with a real child
create), not with an injected status. Outside the guarantee, and stated
so: a session key deleted by hand after G was armed lets a newly created
key read S 1 in that same session (the administrative recovery is
therefore delete and **Restart**); a copied G 1 carries the reference
machine's history (A cannot tell a deployment from it, hence the
integrators' paragraph); and on x86 a helper that fails for good holds
every automatic start. The implementation must show that an update
or reinstall that keeps the controller's driver key (and its armed G)
does not discard the session key in that session; one that replaces the
driver key re-arms from G 0 (section 21.5.3).

**What it costs.**
- An import row (x64) and the helper's routine service (x86); on x86,
  a helper that never answers - not installed, failed, or older than the
  routine service - means no start ever arms G and every automatic start
  holds, for good; the Tree says so, and the setting's 1 (bypass) is the
  only way the switchover moves there.
- `ReadyAtStart` is a value the driver reads that no INF writes (an INF
  cannot write into a key that does not exist until the driver creates
  it and is gone at the restart). The standing rule says every value a
  driver reads is written by its INF: this alternative needs the owner's
  explicit exception for this one driver-created volatile value (Q13).
  `tuning.md` would name the key and the value as values the driver
  writes.
- A stray child key `V` in the rare non-volatile case.

#### 21.5.2 Alternative B, weighed and not taken: G and a flag in the driver image's own memory

The owner chose A (Q11); this subsection records what B would have been,
so that the choice can be read, and nothing of it is built.

**The mechanism.** One image-wide flag, `HcdPswArmedThisLoad`, a static
of `xhci98.sys`, 0 when the image is loaded (`DriverEntry`), never
written to the registry. No new routine, no key the driver creates, no
helper.

**At each start that reaches the guard** (2000 and later, mode on, board
passing), at `PASSIVE_LEVEL` in `hcdStartBody`'s decision:

1. Read G.
2. If G is 0: set the flag; write G = 1 (a failed write is logged and
   changes nothing in this start); hold.
3. Else, if the flag is set: hold.
4. Else: the guard permits (routing still needs every other check of
   section 21.3).

**Synchronization.** The start gate is per controller, so two
controllers' decisions can run at once. The flag is a `volatile ULONG`
set with `InterlockedExchange` (already imported) before G's write in
step 2, and read once in step 3; nothing else touches it. A decision
that reads it after another controller set it holds; one that read it
before is not affected. The flag never changes a decision already made:
a controller with G 1 that started, and routed, before another
controller's start set the flag stays routed for that lifetime; the flag
holds only decisions made after it is set, in that image load.

**Its contract, exactly.** B is a hold **for the current load of the
driver image**, decided per controller start - not a Windows-boot
boundary. Within one load, once any controller's start has found G 0,
every later decision of every controller holds; a controller that found
G 1 before that may have routed. **Any actual unload and reload of the
image ends the hold**: the next load starts with the flag clear, and a
start that then finds G 1 is permitted at once unless another
controller's start set the flag first in that load (routing still needs
the other checks). That includes reloads Windows causes itself: Windows may
stop a running PnP driver's devices, unload it and start them again to
install an updated driver without a system restart (Microsoft, "Avoiding
System Restarts During Device Installations and Driver Updates"), and
whether a requested unload really happens depends on the driver's
device objects and references (Microsoft, "Unload Routine Environment").
A resource rebalance stops and restarts the devices but keeps the stack
(Microsoft, `IRP_MN_STOP_DEVICE`), so it does not end the hold. While
Windows Setup's values or the PE tests hold, a reload changes nothing;
the exposure is a reload after those checks pass - in the desktop
session of an install made while Windows runs, for example a second
installation or update pass, or a device installation that stops and
restarts the stack. Choosing B therefore relaxes decision 26 (no switch
in the session in which the switchover was first turned on) to "no
switch in the image load in which it was first turned on". A warning in
the USB Tree covers only the Tree's own restart command, not an
installation action that never goes through the Tree.

**What the source and the probe show about the image's lifetime.**
- `hcdUnload` is set in `DriverEntry` (`hcd_entry.c`) and cleared, so the
  image stays for the boot, once a controller kept DMA memory it could
  not free (`hcd_dma.c`, `hcdLedgerKeep`) or once `XhciInterruptMode`
  4's library or refusal table was used (`HcdDmaKeepImage`, from
  `hcd_msgnt.c` and `hcd_msg4.c`). Otherwise the image can unload with
  its last device; a pinned image keeps its flag, and an update then
  waits for the restart to load the new file.
- With one controller, a disable and enable unloaded it: the probe's
  start serial, a static, began again at 1 at every `devcon restart` on
  Vista, 7, 8, 8.1 and 10 (section 21.2; runtime, 32-bit). With two
  xhci98 controllers, disabling one leaves the other's device objects, so
  the image stays (from the PnP rules; not read).
- The USB Tree's restart of a controller (task 50.32) is designed as
  Windows' own restart of the device, as Device Manager's disable and
  enable: on a one-controller machine it can unload the image.
- 2000 and XP unload a WDM driver the same way; not read for this
  driver.
- Crash or power loss: a new boot loads the image afresh, a valid
  boundary; a G write that was lost leaves G 0 and holds that boot too.
  Hibernation and Fast Startup restore the image's memory with the flag.

**What it costs.** The relaxation above: an unasked reload of the image
in the session in which the switchover was turned on, once Setup's
checks pass, can move the connectors in that session. No document read
for this design shows that such a reload cannot happen after those
checks pass, and this project has not read every installation path. In
return: no exception to the INF rule, no new import, identical on x86
and x64, nothing on the helper, one restart fewer on 32-bit 2000 and XP.

#### 21.5.3 What each alternative means for the user

The driver behaves as the column for A says; the column for B is what was
weighed against it. When the guard first permits, on a machine whose Setup checks pass (on
Vista and later with the image state readable at that start; where it
is not, section 21.3); "permits" means the guard no longer holds -
routing still needs every other check to pass:

| System and route | Alternative A | Alternative B |
|---|---|---|
| 2000, 32-bit XP: install while Windows runs (`SETUP.EXE`, Device Manager); the helper is absent in that session (record 18 section 15.5) | that session holds, no arming; the next boot arms and holds; **two restarts** | that session arms and holds; **one restart** |
| XP x64, 64-bit Vista to 10: install while Windows runs | one restart | one restart |
| 32-bit Vista to 10: install while Windows runs (the controller first starts at the restart the install asks for, the helper then present) | that boot arms and holds; two restarts in all | the same; two restarts in all |
| 2000, 32-bit XP: F6, or an image with the INF integrated (GUI-mode Setup finds it on, with no helper running) | GUI mode holds without arming; the first boot after Setup arms and holds; the guard permits from the second | GUI mode arms (Setup's values hold anyway); the guard permits from the first boot after Setup |
| XP x64: F6 or integrated | GUI mode arms; the guard permits from the first boot after Setup | the same |
| Vista to 10, either architecture: an image with the driver staged, or given to PE and to the installed system (the OOBE boot finds it on; held by Setup's values, armed) | the first restart after Setup ends | the same |
| an update over a release without `XhciIntelPortSwitchReady` (the INF adds it at 0): the update's restart of the driver in the session | arms and holds; the guard permits from the next boot | the start that finds G 0 arms and holds; the guard permits from the next boot (or from a later fresh load of the image in that session, B's relaxation) |
| an update over an armed install (G already 1) | the update's start: no session key yet - it creates one with S 1 and the guard permits at once; an existing key with S 0 holds; unknown evidence holds | the guard permits at once whenever the flag is clear, in a freshly loaded image or one kept loaded; a set flag holds (an image kept loaded, `DriverUnload` cleared, keeps a flag set earlier in it); even in a fresh load, another controller's start may set the flag first |
| a disable and enable of the only controller, the Tree's restart, or an installation pass that reloads the image, in the session in which it was turned on (once Setup's checks pass) | held (S 0) | the guard can permit (the image reloaded) |
| x86 where the helper never answers | never automatically; bypass only | as any other |
| 98 SE, ME (no guard) | the driver's first start | the same |

Where a start holds, the connectors stay where the firmware routed them,
which on a board whose firmware routes them to the xHCI (a setting other
than Auto) is already the xHCI controller: the guard holds this
driver's writes, it does not promise the USB 2.0 controller.

**For image integrators**: G is the controller's recorded history, and a
copy carries it. To have a deployed machine's first session held, leave
G at 0 in the image - a driver staged and
never started in the reference image has the INF's 0; where the
reference machine ran the driver, set `XhciIntelPortSwitchReady` to 0 in
each xHCI98 controller's key before capturing (the key `tuning.md`
names, or the Tree's Settings for each controller under "Applies to").
To let the guard permit at a deployed machine's first ordinary start,
leave it at 1. Whether Sysprep's generalize pass removes the controller's key,
and with it this value, depends on its `PersistAllDeviceInstalls`
setting (Microsoft's unattend reference) and has not been read for this
driver's key, so set the value rather than rely on it. The volatile key
never reaches an image; a non-volatile copy of it is refused at every
start (step 4).

**The two cases, as weighed, and the choice** (Q11: the owner chose A,
2026-11-18).
- **For A**: it delivers what decision 26 asks - no switch in the
  Windows session in which the switchover was first turned on - against
  every image reload, Windows' own included, because its evidence lives
  in a volatile key that a reload does not touch. Its costs: on x86 it
  rests on the helper (no automatic move at all where the helper never
  answers), a restart more on 32-bit 2000 and XP, a volatility proof at
  every start, and an exception to the INF rule (Q13).
- **For B**: no helper, no exception, no new import, no driver-created
  registry state, simpler code and tests, a restart fewer on 32-bit 2000
  and XP; but its hold lasts one image load, and an image reload that
  nobody asked for, after Setup's checks pass, ends it.
- **The recommendation was A, and A was chosen.** B would be enough
  only if no unasked reload of the image could happen after Setup's
  checks pass, and no document read for this design shows that;
  Microsoft documents the opposite for driver updates.

### 21.6 The lifetime

The decision is section 7a's and is taken once, at each start
(`hcdStartBody`, `IRP_MN_START_DEVICE`), and latched for that started
lifetime. Nothing re-decides until the next start: no registry watcher,
no timer, no re-reading at a return to D0, and no registry access of
this design's outside the start's decision (revision 12's read during a
lifetime is withdrawn with the memory it fed). Every registry access of
the step runs in `hcdStartBody` at `PASSIVE_LEVEL`, under no spin lock,
before `XhciInitController`: the controller is not yet serving any
device, so no storage this driver runs can be what a registry access
waits on.

**The invariant: a held lifetime writes nothing to the routing registers
at any moment** - no route, no release, and no shutdown hand-back.

- `On` stays clear, so `XhciPswLifeStart` writes nothing, and every later
  step acts on nothing: `XhciPswLifeResume` at each return to D0 and at
  the suspended-in-D0 fallback makes no access, and `XhciPswLifeRelease`
  and `XhciPswLifeEnd` at a stop, surprise removal, removal, refused
  start or a shutdown's D3 find an empty set and make no access (section
  6). A held lifetime's configuration access is the decision's own reads
  (offset 0 and the subsystem).
- **`XhciIntelShutdownToEhci` is suppressed in a held lifetime** (owner,
  Q7). Today, on 1E31, 9C31 and 9CB1, it writes both routing registers 0
  at a shutdown's D3 whatever `XhciIntelPortSwitch` says (section 5,
  design record 20 section 4.4). From this revision `HcdQuirkShutdown`
  first asks a pure predicate of the latched lifetime
  (`XhciPswShutdownAllowed`): a lifetime whose decision is
  `XHCI_PSW_DECIDE_SETUP_HELD` gets no write and a log note
  (`quirk.shutdown.held`). A lifetime that is off (`XhciIntelPortSwitch`
  0) or routed keeps the hand-back as today. The trade-off, accepted by
  the owner: on a board whose firmware itself routed the connectors to
  the xHCI (a firmware setting other than Auto), a held lifetime now
  powers off with them there, which is the state the hand-back exists to
  avoid on those three parts (a board that powers back on after
  shutdown, design record 20 section 4.4). A held lifetime keeps the
  firmware's routing; it does not put the connectors on the EHCI
  controller.
- **Setup ending while the lifetime runs changes nothing**: the Vista
  and 7 OOBE boot, held, goes on to the desktop with the connectors where
  firmware put them; they move at a later start that passes the checks
  and the guard. A setting changed while held likewise waits.
- **Sleep, hibernation and Fast Startup** are returns to D0, not starts:
  a held lifetime stays held across them, a routed one routes again at
  the D0 (section 6). After Fast Startup's "Shut down" and a power-on the
  lifetime and the session both continue; only a restart ends them.
- **A routed lifetime is not undone by a later reading**: a value that
  turns to "in progress" (a Sysprep run from the desktop) is read at the
  next start, and this lifetime's releases happen at their moments.
- **A disable and enable** is a stop and a start: the stop releases what
  that lifetime wrote, and the start decides afresh inside the same
  session; the guard holds it, as it holds every start of that session
  whatever reloads the image (section 21.5.1).
- **When the next decision is taken**: at the next `IRP_MN_START_DEVICE`
  of the controller - the next boot, or within this boot a Device
  Manager disable and enable, the USB Tree's restart of the controller
  (task 50.32), a resource rebalance's stop and start, or an update's
  restart of the driver. A later start in the same boot may route where
  the first could not (the image state now readable, say) only when every
  check passes and the guard allows; a held lifetime itself never
  routes, and it keeps whatever routing the firmware gave the connectors
  (which may already be the xHCI), not the USB 2.0 controller.

### 21.7 The record, the snapshot, the log, and what the user sees

**The record: `XhciIntelPortSwitchResult`** (owner, Q6), a `REG_BINARY`
the driver writes in the controller's own driver key at every start that
reaches section 21.3's step - an Intel controller with the switchover on
(1 on a listed controller, 2 on any) and the board passing - and at no
other: a controller not Intel, an unlisted Intel controller without 2,
the switchover off and the excluded board get nothing new in the
registry, as section 21.3 says (their snapshot words, in memory, say
which of those decided) - output only, never
read back by the driver, so no decision depends on it and it needs no
INF row; `tuning.md` lists it in "A value the driver writes", as
`XhciMode4Refusal`. In the form of `XhciMode4Refusal` (design record 15
section 4.5.20): a layout word, the driver's packed version, the start's
time (`KeQuerySystemTime`), the system family, the decision and verdict
and the value it names, `XhciIntelPortSwitch` and the setting each with
its value and the key that supplied it, each Setup value's routine
observation, status and value, the two PE tests, the hive test and the
image reading, the guard's G and its outcome (the session's value, the
key's disposition and volatility test), the status of each guard write, and for a routed start the
route's step and accepted-write set. It is rewritten at every such start,
the time always that start's. The builder and the reader are pure
(`xhci_psw.c`), shared with the USB Tree, which already links that file.
On 98 SE and ME a write to the driver key by this driver has not been
read; a refused write leaves the old record (handled below).

**Stale results.** A record that could not be overwritten would show an
older start's decision, so the record is never the Tree's source for a
running controller: the controller's snapshot block, read through the
door, carries the latched decision itself (the same words as the
record, from `PswLife`, and the status of this start's record write).
As built (task 50.34's code), those words are appended to the HCD
region's header (words 956 to 1015, `XHCI_SNAPSHOT_HCD_PSW_*` in
`src/xhci.h`, after task 50.31's two words at 954 and 955; on its own
branch the block began at 954) with its version unchanged, as every
block appended there since task 44.35: a header that ends at word 956
is an earlier driver's,
and `XHCI_SNAPSHOT_SCHEMA`, which changes only when the snapshot's own
header changes shape, is not raised. The Tree shows the snapshot's
decision for a controller that runs; where the snapshot says this
start's record write failed, it adds "(not saved in the registry)". The
registry record is shown only for a controller with no door (not
started, or `/dump` with none open), always as "as recorded at
<time>", never as the current state.

**The log.** After `psw.mode`, on every start that reaches the step:
`psw.setup.override` and `psw.setup.override.src` (the setting and the
key: 0 none, 1 machine-wide, 2 the controller's), `psw.setup.state`
(one byte per Setup value: 0 absent, 1 taken, 2 malformed, 3 unread),
`psw.setup.ssip`, `.type`, `.oobe`, `.mini` (each whole, `0xFFFFFFFF` when
not taken), `psw.setup.status` (the first unread value's status),
`psw.pe` (bit 0 the key, bit 1 the boot option; bit 4 and 5 unread),
`psw.image` (0 complete, 1 not complete, 2 unread, 3 not loaded),
`psw.guard` (G and the guard's outcome, the session's value, the
disposition and the volatility test), `psw.guard.write`
(a failed write's status), `psw.setup.verdict`; and `psw.record.write`
when the record's write failed. A held start logs no `psw.route.*`, and
its stop no `psw.release.*`, as an off start's does today (`hcdPswEnd`
notes only a lifetime with `On` set); its shutdown logs
`quirk.shutdown.held` on the three parts. The `qemu` flavour's trace adds
the verdict line to the model's lines.

**The USB Tree** (drafts, the owner's to word, Q8). A line in each Intel
controller's block (design record 25 section 18), from the snapshot,
each followed by where the value in force was read ("set in the
machine-wide key" or "set in this controller's key"):

- "Port switchover: off (XhciIntelPortSwitch 0). This driver does not
  move the shared connectors when it starts or when Windows wakes; at
  shutdown it may still hand them back to the USB 2.0 controller
  (XhciIntelShutdownToEhci)."
- "Port switchover: on. The shared (usually blue) connectors were moved
  to this controller at its start."
- "Port switchover: waiting. Windows says Setup is in progress
  (SystemSetupInProgress = 1), so the shared connectors were not moved
  at this start."
- "Port switchover: waiting. Windows' Setup value SetupType is 2. While
  it is not 0 the shared connectors are not moved; on some systems it
  may stay set after Setup."
- "Port switchover: waiting. A Windows Setup value could not be read
  (SystemSetupInProgress: <absent | malformed | status 0x...>), so the
  shared connectors were not moved."
- "Port switchover: waiting. Windows is running as Windows PE (<the
  MiniNT key | the boot option MININT>), so the shared connectors were
  not moved."
- "Port switchover: waiting. Windows' image state is <state>, not
  complete, so the shared connectors were not moved."
- "Port switchover: waiting. Windows' image state could not be read
  when this controller started: Windows had not loaded it yet. It is
  read again at the controller's next start (a restart of this
  controller, or of Windows); the shared connectors move then only if
  nothing else holds them. On this computer that can differ from one
  start of Windows to the next. XhciIntelPortSwitchSetup 1 (bypass)
  moves them at every start, whatever Windows' Setup state says; a
  device in use on them is disconnected when they move. Do not rely on
  either for a drive Windows starts from."
- "Port switchover: waiting. It was turned on for this controller in
  this Windows session, so the shared connectors are not moved before
  Windows is restarted (Restart, not Shut down)." / "... the driver
  could not tell this Windows session from an earlier one." and "... the
  key XhciIntelPortSwitchSession under this controller's key is not one
  the driver made: delete it, then restart Windows."
- "Port switchover: held by XhciIntelPortSwitchSetup 2. The shared
  connectors are not moved."
- "Port switchover: on, Windows' Setup check bypassed
  (XhciIntelPortSwitchSetup 1)."
- As built (task 50.34's code), "on" is said from the record's own route
  outcome, since the decision is taken before the route runs and kept
  whatever it did: after "Port switchover: on." (or the bypass sentence),
  "The shared (usually blue) connectors were moved to this controller at
  its start." only where every step of the route succeeded; "Moving the
  shared connectors was allowed at this start, but no write to the
  chipset's switch registers was accepted (step N), so they were not
  moved." where none was accepted; "Moving the shared connectors was
  allowed at this start, but not every step of it succeeded (step N), so
  some of them may still be on the USB 2.0 controller." otherwise. The
  window's details put the words under "Port switch:", and for a
  controller with no snapshot served open them with "As recorded at
  <time>:". The full list of the task's user-visible strings, each a
  draft, is kept with the task's notes for the owner.

No line promises a move when the one condition it names clears: the
connectors move at a start where no check holds them and the guard
allows it, and the line says only what held this start. Each "waiting"
line ends: "If Windows Setup has finished and this does not change after
you restart Windows (Restart, not Shut down), XhciIntelPortSwitchSetup 1
(bypass) moves them at the controller's next start; a device in use on
them is disconnected then." The Settings window's confirmation for
`XhciIntelPortSwitch` going on (`trs_psw_text`) gains, on 2000 and later:
"They are not moved while Windows says Setup is in progress or that
cannot be read, nor, after this is first turned on, before Windows is
restarted." The new
setting's row: "Windows Setup check for the port switchover: 0
automatic, 1 bypass (move the connectors whatever Windows says), 2
never move them while the switchover is on, at shutdown included."
The guard's value's row: "Port switchover's first-session check: 0 the
switchover has not been on for this controller yet, 1 it has been on at
an earlier start (that start's Windows session may still be running);
the driver sets it." As built, the row's meaning is "the port
switchover's first-session check for this controller: whether the
switchover has been on for it at an earlier start; the driver sets it",
its choices "not yet on for this controller" and "on at an earlier start
(that start's Windows session may still be running)", and its caution
says to set 0 as the last step of an image's preparation and capture
before the controller starts again. The Tree offers it, never
machine-wide, only on a controller the switchover applies to by its
identity (a listed Intel controller, or any Intel one while
`XhciIntelPortSwitch` 2 is in force for it; never the excluded board):
elsewhere its Now column reads "(the port switchover does not apply to
this controller)" and `/set` refuses "The port switchover does not apply
to <controller>, so its driver never reads XhciIntelPortSwitchReady.
Nothing was changed." The tools' reads of the record are a different
matter from the driver's scope above: the `.DEV` and `/dump` read
`XhciIntelPortSwitchResult` wherever it is - `/dump` scans every subkey
of the USB class keys for the value, whoever's key it is - read-only
history, and the window's details read it only for an Intel controller
with no snapshot served.

**A field report** carries the snapshot's decision and the registry
record in the `.DEV` under the controller and the record in `XHCITR
/dump` (as `XhciMode4Refusal` is), and the log notes in the snapshot's
log, so a report from a machine that did not route says what held it,
how each value read, where the values in force were set, and when.

### 21.8 What the INFs write, and Setup

**Every install section, in this release** (owner, Q9 and Q4):

| Section | `XhciIntelPortSwitch` | `XhciIntelPortSwitchSetup` | `XhciIntelPortSwitchReady` |
|---|---|---|---|
| `xhci98.inf` `[Xhci.AddReg]` (98 SE, ME; added by `[Xhci.Dev]`) | 1 | 0 | not written: not read on 98 SE and ME |
| `xhci98.inf` `[Xhci.AddReg.NT]` (2000, XP; Vista to 10 x86; added by `[Xhci.Dev.NTx86]` and `[Xhci.Dev6.NTx86]`) | 1 | 0 | 0 |
| `xhci98-amd64.inf` `[Xhci.AddReg.NT]` (XP x64, Vista to 10 x64) | 1 | 0 | 0 |

each with `0x00010003`, the don't-overwrite flag.

**98 SE and ME** (owner, Q4, in his words: "Win 98 has no special
condition. most people install the drivers later."): no Setup hold - no
such value exists there - and no guard; the ports switch at the
driver's first start, at every value of the setting but 2. What it
means: an install made during 98 SE's or ME's own Setup (a driver folder
added to the install source, found at Setup's hardware detection) or
from a drive on a switchable connector moves the connectors at the
controller's first start, so a device on them that an EHCI driver ran
(where a USB 2.0 stack was added to 98 SE or ME, a drive or the install
source can be on a blue connector) is disconnected there and found again
under this driver, mid-install if the install is still reading from
it; on a stock 98 SE or ME with no EHCI driver those connectors had no
working driver and gain one. The owner accepted that risk. The setting
there: 2 holds as everywhere; 1 has nothing to bypass and acts as 0.

**2000 and later**: the INFs' 1 is held by section 21.3 and the guard of
section 21.5, which is why `XhciIntelPortSwitchReady` is written there.

**Text-mode Setup** (`txtsetup.oem`, the F6 floppy) writes no value, so
its start is off as today (decision 17), and no change is made to it.

**An update** keeps what is set: an existing controller key keeps its
`XhciIntelPortSwitch` - the 0 that `2.3.0.0` to `3.5.0.0` wrote stays 0,
a user's or `SETUP.EXE`'s 1 stays 1, a machine-wide value stays and wins
- and gains the setting at 0 and, on 2000 and later, the guard's value
at 0, so the guard holds the update's own session (section 21.5.3).
Only a controller key the new INF creates (a first install, a controller
at a new PCI position, an image's install) gets the 1. The release notes
and `tuning.md` say so.

**`SETUP.EXE`.** Its logic does not change. On a first install on a
listed board it still proposes 1 and writes it machine-wide after its
run (section 19); with every INF section now writing 1 that proposal is
redundant for a new controller key, and it is kept so that the console
question and the window still show the choice and an answer of 0 still
writes a machine-wide 0 that wins for every controller. What changes is
when the connectors move: on 98 SE and ME at the controller's first
start under the INF's 1, which may be during the run itself (the owner's
accepted risk above), no longer at the next start; on 2000 and later
under the guard, at a later boot as today. Its texts
that say "at the next start" are checked against that (Q8). It gains
the setting as a data row (its Advanced list, `/set`, the uninstall's
list of machine-wide
values), because Setup's Advanced lists offer every value the Tree does;
the guard's value is a per-controller state and is not offered there.

**The INF gate.** Every section now writes the same default, so the row
of `XhciIntelPortSwitch` needs no per-family default: `Default` 1, `Since`
`2.1.1.0`, `Was` 1 before `2.3.0.0` and 0 before `3.6.0.0`, its `Why` and
`DefaultWhy` taking this revision. New rows: `XhciIntelPortSwitchSetup`,
`Since` `3.6.0.0`, default 0, on every path of both files; and
`XhciIntelPortSwitchReady`, `Since` `3.6.0.0`, default 0, on the NT paths
only, refused on the undecorated path - a marker the gate's table does
not have yet, beside the `X86Only` that marks the x86-only rows. The
footprints and the self-test fixtures that refuse a 1
(`test-inf-checks.ps1`) turn round, and none of the gate's other
assertions is loosened.

### 21.9 Risks

These stand until the bench reading after the cut; until then the only
evidence is the emulator's.

- **The values are a hint, not a contract** (section 21.2). A Setup route
  not read may show every check passing at the moment the controller
  starts, and route; the routes read are section 21.2's table and those
  of section 21.10's legs, and no other.
- **An OOBE boot reads `SystemSetupInProgress` 0.** On Vista and 7
  `OOBEInProgress` and `SetupType` hold it; on 8 to 10 neither has been
  read, and the image state is then the check that holds it.
- **The image state was not readable at the first start of every boot
  read on the Vista bases, and of four of six on the 10 base** (section
  21.2's probe, emulator boots under host load): under the decided rule
  those starts would have held, so on such a machine the switchover may
  hold at every boot's start, or at some boots and not others; the
  bypass is the way out and also bypasses every Setup check from then
  on. A system the probe did not read holds wherever its hive is not
  loaded at the start.
- **A drive Windows starts from on a switchable connector** can change
  controller from one boot to the next where the outcome varies, and is
  moved during the boot wherever it is routed: the operating restriction
  of section 21.3 applies, and nothing in this design makes it safe.
- **Windows PE's live hive is not the installed system's.** In PE the
  driver reads and writes PE's own hive in memory: its checks hold PE's
  start, and nothing it records - the guard's value, the record, a
  setting written there - reaches the installed system, whose own starts
  decide afresh. The PE tests are not on a Microsoft reference page
  (section 21.3); a PE that sets neither, with Setup's values all 0,
  would route.
- **An integrated image** is the route this task exists for, and the
  emulator's readings of it are section 20's F6 and staged-driver legs
  until section 21.10's legs run.
- **Sysprep and Mini-Setup** are unread here: the articles say the three
  values are 1 for Mini-Setup, which holds; an interrupted preparation
  leaves them set, and the switchover then holds at every start until
  the setting's 1.
- **Repair and upgrade installs** are not established: an old system's
  phase, the replacement system and a recovery environment can each have
  their own state and start order; a routed lifetime stays routed until
  its stop.
- **2000's text mode and a USB CD**: section 20's STOP `0x7B` stays a
  limitation of its own; a 2000 leg completed from an IDE CD says
  nothing about a USB source surviving.
- **A moved install source on real hardware** cannot be shown on the
  emulator: its model moves devices from a harness on the trace's lines,
  not by a board's multiplexer with its own timing and firmware, and the
  shutdown quirk's effect on a real board is not modelled at all.
- **98 SE and ME** move the connectors at the first start, during an
  install from a drive on them included (section 21.8, accepted).
- **The shutdown hand-back's suppression** in a held lifetime (section
  21.6, accepted).
- **A value never cleared** on some system (a `SetupType` left non-zero,
  a PE mark on a full system) holds there for good; the Tree names it,
  and the setting's 1 is the way out.
- **No move in the session of an install made while Windows runs, nor in
  the session in which Vista to 10's Setup ends** (its OOBE boot goes on
  to the desktop), with the exceptions and restart counts of section
  21.5.3's column for A. Each move at a later boot is section 6's
  cost, a device on a blue connector found again; a held start keeps
  the firmware's routing, which may already be the xHCI.
- **Registry writes at each start** on Intel machines whose switchover is
  on: the guard's value when it changes from 0, the volatile session
  key at a session's first start, and the record.

### 21.10 Tests and acceptance

No leg records a product key or the contents of an answer file: the
legs name the answer file's role ("the owner's XP answer file"), and its
text and any key stay off the run sheet, the scratch and every log
copied into the tree.

**Before the code merges** (host, gates and emulator; the `qemu` flavour,
`XhciQemuPswModel` 1, the INFs as section 21.8 writes them, the harness
of section 20 moving the modelled connectors' devices):

- **Host vectors** (`test\test_psw.c`, the modelled configuration space
  recording every access and every callback): every cell of section
  21.3's two Setup tables, a DWORD taken with each failing status and an
  empty `REG_NONE` primary and secondary among them; the rule over every
  combination of the four values' representative states (taken 0, taken
  1, taken 2, absent, malformed, unread: 1296), against an independent
  statement of the rule in the test; the PE tests' cells; the image
  state's cells and the hive test; the setting at absent, another type, 0,
  1, 2, 3, `0xFFFFFFFF`, with the key that supplied it, on both families;
  `XhciIntelPortSwitch` absent, 0, 1, 2 on a listed id, an unlisted Intel
  id and QEMU's (the step's callbacks only where the mode is on and the
  board passes); a held start making no access at `D0h` to `DCh` and the
  rest of its lifetime none; `XhciPswShutdownAllowed` false for every
  held verdict and true for off and routed; the latch; the string
  readers over every malformation of section 21.3 (call counts, types,
  odd and oversized lengths, missing and embedded terminators, failed
  statuses) and the `MININT` token's spellings (Vista's `/NOEXECUTE=OPTIN`
  and 7's ` NOEXECUTE=OPTIN`, a token with and without `/`, `MININT=1`,
  `XMININT`); the guard under A (below); the record and the Tree's text
  for each verdict.
- **The guard under A** (host vectors over a modelled registry, then the
  same sequences in a guest by a `qemu`-flavour injection, a test value
  written by no INF): every disposition and a failed call; the
  volatility test's every outcome; `ReadyAtStart` written and read back,
  its write or read-back failing, and on the existing-key path taken 0
  and 1, absent, malformed and another number; G 0, 1, absent and
  another number, its write failing. **Failure then recovery in one
  boot**: the routine unavailable at the first start and available at a
  later one; the key's creation failing and then succeeding; the
  read-back failing and then succeeding - each must hold every start of
  that boot and leave G 0 until a start with proven evidence, which
  itself holds. The volatility proof read with real registry operations
  on each NT system: a volatile key's refused child, a non-volatile key
  of the name made by hand (with `ReadyAtStart` 0, 1 and none) and its
  child's create. **The helper never answering** (x86): every start holds,
  G stays 0, every boot; the bypass routes. **A non-volatile key of the
  name** holding `ReadyAtStart` 0, 1 and nothing, with G 0 and with G 1:
  every start holds and G is not armed; deleting the key and a Restart
  recovers.
- **The guard under B** was planned with its own cases (two controllers
  in both start orders, concurrent starts, an image reload in the
  session); B was not taken (Q11), so none of them is run.
- **Also under A**: successful arming followed by a further
  installation or update pass, with Setup's values active and complete;
  a crash and a power loss after arming, with G persisted and with its
  write lost; a deployed image with G 0 and with G 1; both update rows
  of section 21.5.3 (an update over a release without the value, and
  over an armed install). Every expectation is that the guard holds or
  permits; where it permits, routing is expected only where every other
  check passes.
- **Gates** on both architectures: the build, the import gate (the x64
  row of `ZwCreateKey`; nothing new in the x86 image), the INF gate and its fixtures, the packaging
  checks, `check-text.ps1`.
- **A release-flavour smoke reading on both architectures** (negative):
  the `release` flavour, staged by `make-package.ps1` and installed from
  the family layout's INFs (x86 on a 32-bit system, x64 on a 64-bit
  one): the three values written as section 21.8's table says, and on
  QEMU's non-Intel controller nothing new read or written and no record.
  The stand-in of section 20 (`XhciQemuPswModel`) is in the `qemu`
  flavour alone, so the release flavour cannot show a positive Intel
  record in the emulator: the trace legs below read the decision in the
  instrumented flavour, and the shipped build's positive record is a
  bench reading after the cut (below).
- **XP and 2000 Setup through the first desktop and the next routing
  start** (required): with the owner's answer files, which let unattended
  XP and 2000 installs complete, from F6 with the keyboard and the
  install CD on modelled connectors, the helper's presence traced at
  each start: held through GUI mode, and the guard permitting where
  section 21.5.3's table puts it (the second boot after Setup), with
  routing there as the other checks allow; a disable and enable in that
  session; and a desktop install's session held, the guard permitting
  at the second boot after it.
- **A fresh 32-bit Vista to 10 install** (7 at least): the controller's
  first start at the install's restart held and the guard's value
  armed, the guard permitting at the following restart.
- **An install from an image with the INF integrated and no `SETUP.EXE`**
  (required): XP (an install source with the driver integrated, with the
  owner's answer file), and Vista or 7 with the driver staged in the
  image; held through Setup, the guard permitting where section
  21.5.3 says, routed where the other checks pass too.
- Vista and 7 with the driver given to Windows PE by the answer file -
  **Vista's failure of section 20 run to the desktop**: no "A required
  CD/DVD drive device driver is missing" dialog, the answer file's image
  choice applied - and loaded at the disk page; 7 given the driver at the
  desktop (32-bit: two restarts); each with the PE tests, the image state
  and the guard in the trace.
- **A repeated start in one session** (disable and enable) after a
  desktop install, on one and on two controllers: held, as section
  21.5.3 says.
- **Sleep, hibernation and Fast Startup**: a held lifetime still held
  after a standby, a hibernation and (8 to 10) Fast Startup's "Shut down"
  and power-on (none of them reloads the image); the guard permits only
  after a Restart.
- **Alternating image-state outcomes** (10, and a qemu-flavour injection
  that makes the hive test read "not loaded" at chosen starts, on any
  Vista-or-later base): boots alternating readable and unreadable, with
  a USB drive and a keyboard on modelled connectors - which controller
  owns each device after each boot, that a held boot keeps the
  firmware's routing and a routed boot moves them, repeated starts in
  each boot, and the recovery of section 21.3 (the setting's 2, then 0,
  then 1) with a Restart after each.
- **An ordinary boot of every installed base** (2000 SP4, XP SP3, XP x64,
  Vista and 7 in both architectures, 8, 8.1, 10 in both): each Setup
  value's routine observation and status, the PE tests (both "not PE"),
  the hive test and the image state at the first start, and the verdict;
  the probe of section 21.2 is these readings' first part.
- **A stale value**: `SystemSetupInProgress` set to 1 by hand on a
  throwaway overlay of an installed base: held, the Tree's line naming
  it and the key, then the setting's 1 and a restart: routed; and the
  setting's 2 holding a routed system.
- **A held shutdown**: the model extended to the quirk's accesses, a held
  lifetime's shutdown on the modelled `1E31` making no write, and an off
  lifetime's making the hand-back as today.
- **98 SE and ME** (the rig's bases): the INF's 1 routing at the first
  start, by `SETUP.EXE` and by the hardware wizard, with a device on a
  modelled connector; nothing of section 21.3 read; the setting's 2
  holding; the shutdown hand-back.

Owed, and not to be reported as coverage: Server 2003 Setup, x64 Setup
(Windows PE x64 will not load the test-signed driver without test
signing), 8 to 10 Setup where the rig's media do not reach the desktop,
Sysprep and Mini-Setup, upgrade, repair, failed or resumed Setup.

**After the cut, on the bench** (the owner's, on a listed Intel board of
his - the B490, `8086:1E31`, or the E7250, `8086:9CB1`, this record's
two; a failure there is a re-cut, owner Q9):

- XP from F6 or its answer file with the install CD on a USB drive on a
  blue connector, to the desktop with no prompt for the source, the
  connectors moved at the next routing start and not before;
- 7 from a USB stick on a blue connector with the driver given to Windows
  PE by an answer file, to the desktop, and the Vista answer-file route
  reproduced with the hold applied;
- a restart and a shutdown in a held lifetime: no hand-back, and whether
  the board powers back on;
- the guard over a real restart after a desktop install, and the Tree's
  restart of the controller in that session (held);
- the shipped build's positive record: on a listed board, the Tree's
  line and `XHCITR /dump`'s record for a held and for a routed start;
- a stale value's recovery with the setting's 1;
- an update over an install that holds 0 keeping it;
- on 98 SE (or ME) with a USB 2.0 stack added, an install with a drive
  on a blue connector: the move at the first start, as the owner
  accepted;
- the shutdown trade-off on each board, with the firmware's USB 3
  setting on Auto and with it routing to the xHCI (where the board
  offers it): a held lifetime's shutdown and whether the board powers
  back on, beside a routed lifetime's;
- after each Setup route, the final post-Setup routing start and two
  boots after it routed, the connectors' devices found again each time.

### 21.11 The owner's answers, and the questions open

Revision 11 asked nine questions; the owner answered every one the same
day, by dialog (section 11, decisions 24 to 32):

- **Q1, a value that cannot be read: it holds** - the main value missing,
  a read failure, any value of another type. The three secondary values
  being absent is normal and does not hold.
- **Q2, which checks: both extras** - hold in Windows PE, and on Vista and
  later until the image state reads `IMAGE_STATE_COMPLETE`; an image
  state not readable yet holds. `SetupType` is a conservative veto.
- **Q3, the first session: yes** - on 2000 and later no controller
  switches in the Windows session in which the switchover was first
  turned on for it; it switches from the next Windows boot.
- **Q4, 98 SE and ME: 1, nothing held** - "Win 98 has no special
  condition. most people install the drivers later."
- **Q5, the setting: three values** - 0 automatic, 1 bypass (the Setup
  check and the guard), 2 always hold; the result and the Tree say which
  key supplied the value in force.
- **Q6, the guard's memory: its own per-controller value**, written by
  the INF and listed in `tuning.md`; the result record is output only.
- **Q7, the shutdown hand-back: suppressed while the lifetime is held.**
- **Q8, the words: drafts**, corrected where they promised too much.
- **Q9, the release: the code and the INFs' 1 in this release**, the
  bench on a listed board after the cut, a failure there a re-cut.

Revision 13 (the probe of section 21.2):
The owner decided this in advance of the probe's result (owner,
2026-11-18): an image state not readable at the start holds, and the
bypass setting is the way out.

Asked in revision 14 (the recommendation first in each), and answered by
the owner on 2026-11-18 (section 11, decisions 34 to 36):

- **Q11. The guard's mechanism** (section 21.5). (a) **Alternative A**:
  a volatile registry key proven at every start, the guard's value armed
  only on that proof; it keeps decision 26 as made - no switch in the
  Windows session in which the switchover was first turned on, whatever
  reloads the driver - but on x86 it rests on the helper (no automatic
  move at all where the helper never answers), costs a restart more on
  32-bit 2000 and XP, and needs Q13 (recommended). (b) **Alternative B**:
  the guard's value and a flag in the driver image's memory; no new
  import, no driver-created registry state, no exception, no helper; but
  its hold lasts one load of the driver image, so choosing it relaxes
  decision 26 to "no switch in the driver load in which it was first
  turned on": any reload of the image after Setup's checks pass - one
  Windows makes itself during an installation or update, or a restart of
  the controller - can move the connectors in that session, and no
  warning can cover a reload the user did not start.
  **Decided (owner, 2026-11-18): (a), Alternative A.**
- **Q12. Automatic mode where the image state is readable at some boots'
  start and not at others** (the 10 base; section 21.3). What the user
  meets in each:
  - (a) **As decided** (decision 33): a start that cannot read it holds;
    on such a machine the connectors may move at one boot and not the
    next, and on a machine like the Vista bases at no boot's start; the
    operating restriction and the bypass are the user's tools
    (recommended for this release: the variation was read on one
    emulator base under host load, the bench comes after the cut, and
    the alternatives each give up something the owner chose).
  - (b) **Fail open where the `SOFTWARE` hive is not loaded at the
    start**: there the image-state check has no objection, and the four
    Setup values, the PE tests and the guard decide. It is not
    determinism in general: a readable image state that is not complete
    still holds, the other checks still hold where they apply, and
    nothing promises a move at every boot. What it gives up: the
    image-state protection at exactly the starts where it is unreadable,
    which on 8 to 10 is the only check whose OOBE reading is known
    (`OOBEInProgress` is unread there).
  - (c) **A persistent hold after an unreadable start**: once a
    controller's start has found the image state unreadable, its later
    boots' first starts hold (a value of its own, written by the INF at
    0), and the connectors move only at a later start within a session
    or with the bypass. Its mechanism - how a later start in a session
    is told from a new boot's first start (the guard's alternative
    matters here), what a failed write does, and what resets it - is not
    designed. What it gives up: automatic moves at boot on every such
    machine, 10 among them on the probe's reading.
  Choosing (b) or (c) means a further revision of this design, its
  algorithm and its acceptance cases, before code. The decided rule (a)
  stays in force unless the owner changes it.
  **Decided (owner, 2026-11-18): (a), kept for this release.**
- **Q13. Only under A: an exception to the standing rule** that every
  value a driver reads is written by its INF, for `ReadyAtStart`, a value
  the driver creates in a volatile key at a session's first start and
  reads at later starts of that session; no INF can write it. (a) Grant
  it, `tuning.md` naming the key and the value. (b) Refuse it, which
  rules A out.
  **Decided (owner, 2026-11-18): (a), granted.**

### 21.12 What changes with the code

- **Driver:** `xhci_psw.c` and `.h` (the classifications, the verdict,
  the guard's transitions, `XhciPswShutdownAllowed`, the record's builder
  and reader), `hcd_ctl.c` (the readers, the setting with its source, the
  guard (the session key and its volatility test), the guard's writes,
  the record's write, the log notes, the trace line),
  `hcd_quirk.c` (the shutdown predicate), the snapshot's controller block
  and its HCD region's header (section 21.7: the schema not raised), the
  x86 run-time lookup of
  `ZwCreateKey` through the helper, `hcd.h` (`PswLife` grows;
  `gen-offsets.ps1`'s derivation re-run), `test\test_psw.c`.
- **INFs and gates:** both INFs (section 21.8), the INF gate's rows, the
  footprints, `test-inf-checks.ps1`; the x64 import allowlist's row.
- **Tools:** the USB Tree's line, its Settings rows and cautions
  (`tr_set.c`), the snapshot reader and `.DEV` and `/dump` (`tr_snap.c`),
  `xhcitr\README.TXT`; `SETUP.EXE`'s value table (`inst_core.c`), its
  texts on when the switch takes effect, and its readme.
- **`docs/using/tuning.md`:** `XhciIntelPortSwitch`'s row ("Written by
  the install": `1`), rows for the setting and the guard's value (the
  latter not machine-wide), the switchover's section (drafts below), and
  the record in "A value the driver writes".
- **`README.md`:** the registry table's rows and the switchover's
  paragraph.
- **The release notes (drafts, Q8):** "A new install now turns the Intel
  7-, 8- and 9-series port switchover on, on every Windows: the shared
  (usually blue) connectors move to the USB 3 controller. On Windows 2000
  and later they are not moved while Windows says Setup is in progress,
  while that cannot be read, in Windows PE, or before Windows reports its
  installation complete, nor in the Windows session in which the
  switchover is first turned on; they can move at a later start of
  Windows, once none of these holds them.
  On Windows 98 SE and ME they move at the driver's first start. An
  update keeps the setting you have." - "New setting
  `XhciIntelPortSwitchSetup`: 0 automatic, 1 bypass (move the connectors
  whatever Windows says; a device in use on them is disconnected at that
  start), 2 never move them while the switchover is on (with
  `XhciIntelPortSwitch` 0 this setting is not read, and the hand-back at
  shutdown remains)." - "New value `XhciIntelPortSwitchReady`:
  the driver's own note that the switchover has been on for a
  controller, so its connectors can move from the next restart of
  Windows."
- **`tuning.md`'s section (drafts, Q8):** "On Windows 2000 and later the
  driver does not move the connectors while Windows says Setup is in
  progress or that cannot be read, in Windows PE, or (Vista and later)
  before Windows reports its installation complete, and not in the
  Windows session in which the switchover was first turned on. The xHCI98
  USB Tree says when it is waiting and why. If it keeps waiting after
  Windows Setup has finished, set `XhciIntelPortSwitchSetup` to `1` and
  restart Windows; a device in use on those connectors is disconnected
  when they move. When it is off (`XhciIntelPortSwitch` 0) the driver
  does not move them at its starts or when Windows wakes, but may still
  hand them back to the USB 2.0 controller at shutdown
  (`XhciIntelShutdownToEhci`)."
- **Design records:** this record (sections 5, 6, 7a, 8, 9, 10 brought
  up to the code); design record 20 section 4.4 (the hand-back's
  suppression); design record 25 (the controller block's line, section
  18; the Settings rows, section 22); design records 23 and 26 (Setup's
  value table and texts); design record 18 section 15
  (`ZwCreateKey` among the routines the x86 driver looks up).
- **Contributor documents:** `build-and-test.md` ("The qemu flavour's
  test aids": the INFs' value, the verdict line, the model reaching the
  quirk, and the legs of section 21.10 as a recipe); `failure-diagnosis.md`
  row 7; `release-acceptance-test.md` 1.2; `target-standing.md` (the
  readings, when taken); `runs/run-50.md` (the legs).
