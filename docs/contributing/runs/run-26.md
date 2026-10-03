# Phase 26 Record - The USB 2.0 bus driver on root ports

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 26 - The USB 2.0
Bus Driver on Root Ports". The roadmap entry carries the goal, the status, the
task list and the checkpoint; this file carries what each task did and what
each reading said. Where the two disagree about a clause, the roadmap wins.

**Written while the phase is open**, and kept as written rather than rewritten
in the past tense. A sentence saying something "is owed" describes the day it
was written; the roadmap's task list says how each task closed.

**On `out\...`, `vm\...` and `tools\...` paths in this file.** They say where
a reading was taken and what the file was called, on the host that ran it;
they are not files a clone has.

**How the guests were driven.** Every guest leg in this file ran on
development host A under QEMU (TCG, one virtual CPU, `qemu-xhci` with no
SuperSpeed ports), from a clean read-only base extracted from a recorded
snapshot - Windows 98 SE from `vm\win98.img @ post-nusb` (NUSB 3.3's stack, no
`usbd.sys`), Windows 2000 SP4 from `vm\win2k-xonly.img @
win2k-xonly-clean-install` (no USB controller ever installed) - with a fresh
qcow2 overlay per leg under `vm\t26\`. The coordinating session launched and
quit every QEMU process; one GUI-driving subagent per guest drove the desktop
through the QEMU monitor and wrote its notes and screenshots to
`out\phase26\v0\`. The `qemu` flavour's port-0xE9 trace went to
`vm\t26-<os>-<tag>-debugcon.log`. Windows 98 SE needs a restart to replace an
in-use driver file; the launcher turns the guest's reboot into a shutdown and
the coordinator relaunched the same overlay.

Opened 2026-10-03 on the owner's go.

---

## 26-V.0 - the scaffold on both primaries

The build of task 26-A.1 (commit `bd06240`): an FDO that attaches over the PCI
PDO and refuses its start once the PCI stack below has started. `debug`
flavour, `xhci98.sys` SHA-256 `f5bf675a...0cc1`, staged by
`make-package.ps1`.

| Guest | Install | Prompts | Device Manager | Root hub |
|---|---|---|---|---|
| Windows 2000 SP4 | Found New Hardware Wizard, "Specify a location" `E:\` | **none** (no signature prompt, no file prompt); the finish page and a "System Settings Change" box asked for a restart, answered No | `xhci98 USB 3.x eXtensible Host Controller`, "This device cannot start. (Code 10)"; Driver tab version 1.99.0.0; files `usbd.sys` 5.00.2195.6658, `xhci98.sys` 1.99.0.0, `usbui.dll` | none, by type or by connection |
| Windows 98 SE | Add New Hardware Wizard, "Specify a location" `D:\` | **"Insert Disk"** for the Windows 98 Second Edition CD-ROM, then "The file 'usbd.sys' on Windows 98 Second Edition CD-ROM cannot be found" (given `E:\WIN98`), then the 9x restart prompt | before the restart: Code 2 ("The NTKERN.VXD device loader(s) for this device could not load the device driver"), the new file still `XHCI98.TMP`; **after the restart: `xhci98 USB 3.x eXtensible Host Controller`, Code 10**; `xhci98.sys` 1.99.0.0 | none |

The clause's "installing with no prompt" holds on Windows 2000 and **does not
hold on this Windows 98 SE base**: it has no `usbd.sys`, which the INF fetches
from the OS's own install source through `LayoutFile`, and Windows 98's setup
engine asks for the CD for it - the behaviour `AGENTS.md` ("The INF and install
media") already describes for an xHCI-only Windows 98 machine. The restart is
the 9x engine's for a driver file in use. Neither is the HCD's doing; whether
the clause wants a base that already carries `usbd.sys` is the checkpoint's
question, recorded here rather than decided.

Notes: `out\phase26\v0\v0-2k-notes.md`, `v0-98-notes.md`.

## 26-A.2 - the controller FDO

The `qemu` flavour of the 26-A.2 tree, installed on a fresh overlay each time.
What each run found, in order:

1. **Windows 2000, first build** (`e6cce2a2...0f37`): no prompt, no restart,
   "This device is working properly"; a disable (Code 22) and enable came
   back working. The trace: PCI vendor/device `1B36:000D` and interrupt pin 1
   read through `IRP_MN_READ_CONFIG`, the kept init sequence to "init
   complete", four USB 2.0 ports powered, the No Op self-test completed with
   code 1 through the HCD's own interrupt and DPC; at the disable "ports
   unpowered=4", "quiesce: halted", and the re-enable's No Op passed again.
2. **Windows 98 SE, same build**: Code 10 after the restart. The trace:
   "PCI interrupt pin unreadable", "bus master: Command register unreadable -
   refusing", "init REFUSED at step=3". The configuration IRP the HCD sent to
   the object `IoAttachDeviceToDeviceStack` returned was refused.
3. **The route, read statically**: NUSB's `USBPORT.SYS` and Windows 98 SE's
   `uhcd.sys` send `IRP_MN_READ_CONFIG` / `WRITE_CONFIG` to the PDO saved at
   `AddDevice`, not to the attached object, and query no bus interface
   (`legal-provenance.md` section 4, the row of 2026-10-03; a subagent's
   read).
4. **Windows 98 SE, a build that asked `GUID_BUS_INTERFACE_STANDARD` first**
   (`b1f485ac...1d49`): the query answered `C0000002`
   (`STATUS_NOT_IMPLEMENTED`), the configuration IRP to the attached object
   was refused again, Code 10.
5. **Windows 98 SE, the IRP sent to the PDO** (`4c8a8542...9cb2`): vendor and
   device ids and the pin read, the init completed, the No Op self-test
   passed, "This device is working properly". **The first disable
   blue-screened**: "A fatal exception 0E has occurred at 0028:C00312EE".
   Windows 2000 on the same build: working, two disable/enable cycles clean.
6. **Windows 98 SE, traced** (`48c1799e...92ff`): the disable arrives as
   `QUERY_STOP` then `STOP` (minors 5, 4), not a remove; the last line before
   the same fault was "stopping the controller thread", and "controller
   thread has exited" never printed - the fault was inside the stop's wait,
   which waited on the thread's object from `ObReferenceObjectByHandle`.
7. **Windows 98 SE, the thread waited for through an event it sets before
   `PsTerminateSystemThread`** (`fbd8b6b8...677c`): "This device is working
   properly", and **two disable/enable cycles with no fault**; the trace shows
   the thread leaving its loop, the wait returning, the ports unpowered, the
   controller halted, every resource released, and the next start's No Op
   passing. The thread's own `PsTerminateSystemThread` ran in both cycles
   without a fault, which leaves the wait on the thread object as the step
   that faulted in runs 5 and 6.

Notes: `out\phase26\v0\a2-2k-notes.md`, `a2-98-notes.md`, `a2c-98-notes.md`,
`a2d-98-notes.md`, `a2d-2k-notes.md`, `a2e-98-notes.md`, `a2f-98-notes.md`;
traces `vm\t26-win2k-a2-debugcon.log`, `vm\t26-win98-a2b-`, `a2cb-`, `a2db-`,
`a2eb-`, `a2fb-debugcon.log`.

8. **Windows 2000, the event wait** (`fbd8b6b8...677c`, then `3235369a...ebf5`
   with the wait split by a WDM-version test): working, and then **the first
   disable rebooted the guest, both times** (the launcher resets on a
   guest reboot, so a bugcheck reads as a restart; no dump was read). The
   disable is `QUERY_REMOVE` then `REMOVE` (minors 1, 2), and the trace ran
   through the whole teardown - thread exit, ports unpowered, halted, every
   release step - before the reboot: the fault came after it, at the
   remove's detach, delete and unload. The second build was meant to wait on
   the thread object on NT and did not: it tested
   `IoIsWdmVersionAvailable(1, 0x20)`, and Windows 2000 is WDM 1.10
   (`win98-wdm.md`), so it took the event path too. Read as the thread still
   executing its last instructions in the image when the remove unloaded it
   (inferred).
9. **Both primaries, the final build** (`683f7dfd...dbe7`; the test is
   `IoIsWdmVersionAvailable(1, 0x10)`: TRUE from Windows 2000 on, FALSE on
   Windows 98, 1.00, and ME, 1.05): **Windows 2000** - no prompt, "This
   device is working properly", no root hub by type or by connection, two
   disable/enable cycles each reading Code 22 then "working properly", no
   reboot. **Windows 98 SE** (`3235369a...ebf5`, whose 98 path is the final
   build's: the test is FALSE there under either threshold) - "working
   properly", no root hub, two disable/enable cycles with Code 22 and back,
   no fault.

One observation that is not the driver's: on two of the fresh Windows 2000
guests Explorer reported "E:\ is not accessible. This folder was moved or
removed." after the wizard - the autoplay window for the transfer drive,
opened before QEMU's FAT image was ready, as the agent read it; the drive was
attached throughout (`info block`).

Notes for these: `a2f-98-notes.md`, `a2f-2k-notes.md`, `a2g-98-notes.md`,
`a2g-2k-notes.md`, `a2h-2k-notes.md`; traces `vm\t26-win98-a2fb-`, `a2gb-`,
`vm\t26-win2k-a2f-`, `a2g-`, `a2h-debugcon.log`.

### The Codex review of the batch, round 1, and the fixes it took

Codex reviewed `01b0365..75bb630` (`.claude\codex-p26a-r1*.txt`, not
tracked): seven MAJOR, three MINOR and two NOTE findings, every one from
reading, none reproduced. Fixed: the stop runs the kept `XhciStopController`
whenever a register window is mapped, not only with `INITIALIZED` set; the
interrupt DPC is counted when it is queued, so a dequeued DPC not yet entered
is waited for; the timer service's count and idle event change together under
its lock; the remove closes admission and waits out every IRP inside before
it stops the controller, and pended power IRPs hold the count until they
complete; no power dispatch waits for its own IRP - a D0 resumes from its
completion routine or a work item, and a system IRP completes only from the
callback of the device IRP its completion requested; a failed resume declares
the controller failed and asks for the in-place recovery; a new common buffer
clears the pin an earlier one may have set; the import gate's `SITES=` reads
every undefined external, not only `__imp_` thunks, and over WDK 7.1's LTCG
amd64 objects reads the sources rather than warning; a start whose NT thread
object cannot be referenced fails; the interrupt enable and mask are called
at the DISPATCH_LEVEL of their contract; and the records that overstated
(26-V.0's tick, design record 13 sections 5.4, 7.4 and 7.5) were corrected.

**Both primaries on the fixed build** (`f22413ea...73eb`, `qemu` flavour, fresh
overlays): Windows 2000 - no prompt, working, no root hub, two disable/enable
cycles, and a Start-menu shutdown to "It is now safe to turn off your
computer"; Windows 98 SE - working after its restart, two cycles, and a
Start-menu shutdown to QEMU's "paused (shutdown)", no blue screen seen. On
both, the shutdown's trace shows the policy-owner chain this round wrote:
`IRP_MN_SET_POWER` system `PowerSystemShutdown` (`0x00020006`), then the
device `D3` it requested (`0x00020104`), then the controller suspended
("quiesce: halted"). Notes `r1-98-notes.md`, `r1-2k-notes.md`; traces
`vm\t26-win98-r1b-`, `vm\t26-win2k-r1-debugcon.log`.

### Round 2, and its fixes

Codex's second round over the round-1 commit (`.claude\codex-p26a-r2*.txt`)
judged seven of the twelve round-1 findings fixed and found three MAJOR, two
MINOR and two NOTE items. Fixed: a queued interrupt DPC is no longer dequeued
at teardown - `KeRemoveQueueDpc`'s TRUE does not promise, before Vista SP1,
that the DPC will not run, so retiring its count there could retire it twice
- it is let run, closed, and its own decrement is waited for; the timer drain
takes the slot lock once after the idle event, because the last callback
signals while it still holds that lock; a failed `PoRequestPowerIrp` no
longer abandons the device transition - a work item suspends or resumes the
controller itself before the system IRP completes; the LTCG source scan
refuses a restricted name in any header, where a macro could carry it to any
object; and the remove's comment and the power file's header say what the
count covers and what has run. **Left open, recorded in `hcd_ctl.c`**: on NT,
a thread whose object could not be referenced at start is waited for by its
event only, so a remove that unloaded the image in the instant before the
thread's `PsTerminateSystemThread` call would unload it under the thread - the
residual of a failure no run has seen, counted (`ThreadReferenceFailures`).

**Both primaries on that build** (`24650e1e...2d9a`): the same legs as round
1 - working, two disable/enable cycles, a shutdown - and the same results.
Notes `r2-98-notes.md`, `r2-2k-notes.md`.

### Round 3, and its fixes

Codex's third round (`.claude\codex-p26a-r3*.txt`) judged the two drain fixes
correct and the open thread-reference window accurately recorded, and found
one MAJOR, one MINOR (beside the recorded window) and one NOTE. Fixed: the
fallback for a failed `PoRequestPowerIrp` no longer resumes a controller the
bus may still hold in a low-power state - toward sleep it only suspends the
controller and records it as suspended in D0, toward S0 it resumes only from
that state, and otherwise it touches no hardware and counts the wake; the
LTCG source scan's header rule covers headers at any depth below the sources;
the power file's header says which IRPs hold the outstanding count. These
change only the fallback a failed power-IRP allocation enters, which no
recorded guest run has exercised - it is reachable on any target, not
provokable on purpose here - so no guest leg was run for them; the gates ran
on both architectures. Round 4 then found the fallback's marker surviving a
stop and start, which could skip the next suspend; the start now resets it.

### Rounds 4 and 5, and their fixes

Round 4 confirmed the round-3 fixes and found the fallback's
`SuspendedInD0` marker surviving a stop and start, which could skip the next
suspend; the start now resets it (`34d2fe2`). Round 5, over the whole range,
found two MAJOR and one MINOR beside the recorded thread-reference window.
Fixed: the registry reads go through a query routine that takes a value only
when it is a four-byte `REG_DWORD` - `RTL_QUERY_REGISTRY_DIRECT` would write a
`REG_SZ` as a `UNICODE_STRING` over the four-byte destination, and the flag
that makes it check the type is newer than both targets; the in-place
recovery and every suspend and resume now share a power gate (an event used
as a mutex), and the recovery re-reads under it that the function is in D0
and not suspended - an inherited miniport limitation, reached here because
the HCD's thread and its power dispatch run on different processors; and the
LTCG source scan joins C line splices (and `??/` trigraph splices) before
matching, with token pasting recorded in the gate as the scan's residual.

**Both primaries on that build** (`f6d394c0...5b11`): working, two
disable/enable cycles, a shutdown, as in rounds 1 and 2. The Windows 2000
agent wrote its notes from its round-1 notes because every observation
matched, its screenshots being this leg's own. Notes `r5-98-notes.md`,
`r5-2k-notes.md`.

### Round 6

Round 6 found no MAJOR: it confirmed the round-5 fixes, checked that every
power-gate acquisition has a PASSIVE_LEVEL route and that the gate forms no
deadlock cycle, and confirmed both recorded residuals. Its one MINOR - the
registry query without `RTL_QUERY_REGISTRY_NOEXPAND`, so a value mistakenly
stored as `REG_EXPAND_SZ` would be expanded before the query routine refused
it - is fixed. Whether Windows 98's `RtlQueryRegistryValues` honours that
flag has not been read; every value the HCD reads falls back to its default
when the query fails, so nothing yet depends on it.

### Round 7: clean

Round 7 confirmed the round-6 fix and reported no finding at any level over
`01b0365..d2246cc`, apart from the two recorded residuals (the NT
thread-reference window and token pasting in the LTCG scan). Batch (a) -
26-A.1, 26-A.2 and the 26-V.0 reading - closes its review loop there.

---

## Batch (b): 26-A.3 and 26-A.4

### The root hub

The controller FDO creates the root-hub PDO at its first start, under
`XHCI98\ROOT_HUB` with no compatible id, and the driver attaches its second
role over it (`hcd_rh.c`). The id strings and relations lists PnP frees itself
come from the pool, through `hcd_pool.c`'s handed-off entry, which brought the
pool pair into the allowlists with `SITES=hcd_pool.obj`.

**Both primaries** (`qemu` build `0b27be77...e56a`, fresh overlays): the root
hub installed from the package's INF with no wizard and no prompt; Device
Manager lists `xhci98 USB 3.x Root Hub` under Universal Serial Bus controllers,
"This device is working properly", and by connection it is the controller's one
child. A controller disable takes the root hub with it (on Windows 98 SE it
reads "a device it depends on ... has been dynamically disabled"; on Windows
2000 it leaves the tree) and the enable brings it back working; a shutdown
followed. Its property sheet has General and Driver tabs only - the Power
tab is 26-A.8's. Notes `b1-98-notes.md`, `b1-2k-notes.md`.

### Root ports and enumeration

The controller thread services every port the event DPC marks changed
(`hcd_enum.c`), feeding each port's pure state machine (`xhci_enum.c`, host
suite `test_enum`, 136 checks): debounce 100 ms, the reset, the speed read from
PORTSC, Enable Slot, Address Device with BSR = 0, the 8-byte and 18-byte
device descriptor reads (with Evaluate Context when the EP0 size differs), the
9-byte and full configuration descriptor reads - each EP0 read a control
transfer built by the miniport's pure TD builder, restored whole with
`xhci_xfer.c`, into a 4 KB common-buffer scratch. A device PDO is not created
yet.

**Both primaries** (`6cfc7022...b1de`): a QEMU USB mouse hot-plugged from the
monitor (`device_add usb-mouse,bus=xhci.0`) traced, on Windows 98 SE and on
Windows 2000 alike: port 1 reset, enabled, speed 3 (High Speed - the speed QEMU
gives it on this controller, reported as such); Enable Slot code 1, slot 1;
Address Device; device descriptor `idVendor 0627 / idProduct 0001`; a 34-byte
configuration descriptor; enumerated. Unplugged and plugged again it
enumerated on port 2 with slot 1 again - the disconnect had disabled slot 1 and
freed its record. Nothing appears in Device Manager for it, by design until the
device PDO exists. Traces `vm\t26-win98-b2b-`, `vm\t26-win2k-b2-debugcon.log`.

On the way, WDK 7.1's `ntddk.h` maps `ExFreePool` to `ExFreePoolWithTag(a, 0)`
under `POOL_TAGGING`, which the amd64 build imported; `xhci_compat.h` undoes it
as it already undid the `ExAllocatePool` rewrite, and the LTCG source scan now
reads past comments and the `#undef` / `#ifdef` lines that only name a macro.

### Device PDOs

Each enumerated device becomes a PDO of its own, a child of the root hub
(`hcd_pdo.c`), with the ids of design record 13 section 10.7 - the device id
`USB\VID_vvvv&PID_pppp`, the hardware ids with and without `&REV_rrrr`, the
compatible ids `USB\Class_cc&SubClass_ss&Prot_pp` and its two shorter forms
taken from the first interface when `bDeviceClass` is 0, the root port as the
instance id until the serial string is read. The PDOs are listed under the
controller's `PdoListLock` and returned as the root hub's BusRelations; on a
disconnect the PDO leaves the list and the relations are invalidated, and its
remove deletes it. `IRP_MJ_INTERNAL_DEVICE_CONTROL` is refused until 26-A.5.

**Both primaries** (`f594d72e...084e`): the hot-plugged QEMU mouse became
`USB Human Interface Device` under Human Interface Devices, bound by its
`USB\Class_03` compatible id to the OS's own `hidusb.sys` (Windows 2000 SP4
5.0.2183.1 driver package, installed silently; Windows 98 SE `HIDDEV.INF`,
whose install asked for the 98 CD for `hidclass.sys`), and shown **Code 10** -
expected, since the HID driver's first URB is refused. By connection it sits
under `xhci98 USB 3.x Root Hub`. Unplugged, it left Device Manager on both
(Windows 2000 without a rescan), the controller and root hub still working,
and a shutdown followed with no fault. The Windows 98 agent pressed Remove on
"Computer" once by mistake; Windows refused it and nothing changed. Notes
`b3-98-notes.md`, `b3-2k-notes.md`.

### The Codex review of batch (b), rounds 1 and 2, and the fixes they took

Round 1 (`f5e9b5f..9a7ffcd`) raised ten MAJOR, six MINOR and one NOTE, all
taken:
- **PDO lifetime:** a device PDO is listed, gone, deleted or orphaned
  (`hcd_pdo.c`). Only the thread lists one; the relations answer marks
  Reported / MissingReported in the hold that copies the list. A PDO is
  deleted:
  - by its own remove once it is reported missing;
  - by the thread at once if PnP never saw it;
  - by its parent's release once PnP has removed it.
  When its parent goes first, it is orphaned and deletes itself at its own
  remove.
- **Root-hub removal:** it detaches the bus on the controller thread before
  its children are settled, and the root-hub FDO reaches the controller only
  through its PDO. Controller removal orphans or deletes the root-hub PDO.
- **Commands:** a command completion counts only for its own TRB. A command
  that times out or is lost requests the controller reset.
- **Slots:**
  - a Disable Slot the controller does not confirm quarantines the record;
  - an EP0 timeout taints the scratch until that reset;
  - HCRST's slot invalidation drops every record and rescans;
  - an enabled slot with no record is given back.
- **Ports:** connects and disconnects follow `CSC`, and only the change bits
  a read saw are acknowledged. Every root port is serviced, not the first 32.
  The powered check moved inside the power gate.
- **Smaller fixes:**
  - the LTCG source scan reads string and character literals;
  - `test_enum` grows from 136 to 238 checks;
  - design record 13 sections 5.2-5.4 are corrected;
  - the `InterlockedExchange` row went, since nothing imports it any more.

Round 2 (the same diff, uncommitted) confirmed twelve of the seventeen fixed.
It raised two MAJOR, three MINOR and two NOTE, all taken except one NOTE:
- a PDO is deleted once only, so a repeated remove is answered and nothing
  more;
- a port waiting in `Gone` waits for its own PDO by serial, across an
  invalidation and a controller stop/start;
- an unpowered detach leaves its slots to the first powered pass;
- an unreported root-hub PDO is deleted at the controller's remove;
- the records' wording is corrected.
The NOTE left is that a restricted name inside a `#include <...>` header-name
still reads as a reference. That only fails the gate closed, so it stays.

### Both primaries after round 1 (`7b8a522d...6b00`, `qemu` flavour)

Fresh overlays, the QEMU mouse hot-plugged from the monitor. The build has
the round-1 fixes, and START clearing `RemoveReceived`. It predates the
round-2 fixes and the owner's renaming of the devices to `xHCI98 USB 3.x ...`
(2026-10-03); a confirmation leg on the later build follows.

**Windows 2000 SP4** (`b5-2k-notes.md`):
- **Install:** no prompts.
- **Mouse:** plugged, it showed Code 10, as expected. Unplugged, it left
  Device Manager with no rescan; plugged again, it came back. Each plug also
  raised the OS's own restart prompts for the HID install, answered No.
- **Root-hub disable:** QUERY_REMOVE and REMOVE. The trace shows "root hub
  detaching, dropping every device", the root hub shows Code 22, and the HID
  device leaves.
- **Root-hub enable:** the mouse was enumerated again with a new PDO and
  showed Code 10.
- **Controller disable:** the root hub detached first, then the thread
  stopped. Only the controller remained, at Code 22.
- **Controller enable:** the controller, the root hub and the mouse all came
  back.
- **Shutdown:** reached "It is now safe to turn off your computer". With
  `acpi=off` the guest does not power off.

**Windows 98 SE** (`b5-98-notes.md`, traces `vm\t26-win98-b5-`, `-b5b-`):
- **Install:** the same CD prompts as before (`usbd.sys`; `hidclass.sys` for
  the HID install) and a restart. The root hub installed by itself after the
  restart.
- **Mouse:** Code 10 on each plug, as expected. Each unplug removed it, and
  each replug opened a new HID wizard with no disk prompt.
- **Root-hub disable:** 98 sends QUERY_STOP and STOP, not a remove. So no
  detach runs, and Windows 98 keeps the never-started HID device listed
  under the disabled root hub. On enable the same PDO stays (no new wizard).
- **Controller disable:** also a STOP. The stop drops the device. On enable a
  new PDO is created, and the old one gets its remove once the next relations
  answer omits it. The HID device shown is the replacement.
- **Shutdown:** clean ("paused (shutdown)").
- Not followed up: an "Unknown Device" under Other devices, which the agent
  saw after the restart and did not look into.

While the root hub is stopped on 98, enumeration still runs: STOP leaves
RootHubStarted set (only the root hub's remove clears it), so a device
plugged in then is enumerated and its PDO created while the hub is stopped;
PnP is asked for the relations at once and reports the PDO when it queries
the hub (inferred from the code; not exercised).

### Rounds 3 and 4

Round 3 confirmed round 2's findings fixed and raised one MAJOR and one
MINOR, both taken:
- A controller reset latched just after a PDO was created could end the run
  before the machine recorded the PDO, and a recovery would then have let the
  port go Empty and re-enumerate before the old PDO was removed. A halt is now
  looked for only between the machine's step and the next action.
- A detach pending with an invalidation sent Disable Slot for slots HCRST had
  already taken. The invalidation now runs first.

Round 4 confirmed both fixed and found no new MAJOR or MINOR. Its one NOTE, a
stale comment on the service order, was corrected. The header-name note stays
as round 2 left it.

### Confirmation on both primaries after round 2 (`9090face...8ab20`, `qemu` flavour)

The round-2 fixes and the owner's renaming, on fresh overlays (notes
`b6-98-notes.md`, `b6-2k-notes.md`). Both installs, Device Manager and the
Windows 2000 disable prompts read `xHCI98 USB 3.x eXtensible Host Controller`
and `xHCI98 USB 3.x Root Hub`, manufacturer `xHCI98 Project`. Every step
matched the run before:
- **Install:** no prompt on Windows 2000; Windows 98 asked for the CD
  (`usbd.sys`) and a restart.
- **Mouse:** Code 10, as expected; it left on each unplug and came back on
  each plug.
- **Root-hub disable and enable:** on Windows 2000 the HID device left and
  came back; on Windows 98 (STOP) it stayed listed.
- **Controller disable and enable:** all three devices came back.
- **Shutdown:** clean on both.

On Windows 98 the controller's enable dropped the old HID entry at once, and
a Refresh showed its replacement, as the b5 trace explains. The round-3
fixes (`hcdRun`'s halt check, the service order) postdate this build. They
change only paths that a controller failure reaches, which no leg exercised.

## Batch (c): 26-A.5 and 26-A.6

### EP0 through the transfer engine

The controller thread's EP0 control transfers now go through the kept
transfer engine (`xhci_xfer.c`), on a queue and a transfer record per device
record:
- submitted under the controller lock, published whole or not at all;
- matched in the event DPC by TRB address, slot and endpoint
  (`XhciXferEvent`);
- settled at the end of a drain that saw the event ring empty;
- the bytes moved and the USBD status latched by the engine.

The old direct ring writes and the faked retire are gone, so enumeration and
the URB path to come share one ring and one queue. Device records enter and
leave the slot table under the controller lock, because the DPC now reads it.

**Both primaries** (`855c2d69...8549`, notes `c1-98-notes.md`,
`c1-2k-notes.md`): installed and plugged as before. The traces read
idVendor/idProduct 0627/0001, a 34-byte configuration descriptor and a PDO
created on each plug, the same as before the change. HID bound with Code 10
(expected), and the device left and came back on unplug and replug. Both
shutdowns were clean.

On Windows 2000 a shell box, "E:\ is not accessible", appeared after the
install's Finish. That was the transfer drive's autoplay window, not the
driver; the trace shows the controller and root hub started normally.

### URBs served: control, configuration, interrupt (working tree, not yet committed)

**Control URBs** (`e7881a4d...fa17`, c3 legs):
- On Windows 2000, `hidusb.sys` sent GET_DESCRIPTOR (`0x0B`) first; served through the map pump, it then reached SELECT_CONFIGURATION (`0x00`), which that build refused.
- On Windows 98 SE, `hidusb.sys` sent SELECT_CONFIGURATION first, with no descriptor URB before it.

**The first working USB mouse** (`a006cd65...04e9`, Windows 2000, c4-2k):
- With SELECT_CONFIGURATION (Configure Endpoint on pool rings, then SET_CONFIGURATION) and interrupt transfers, `USB Human Interface Device` reported "This device is working properly" and a `HID-compliant mouse` appeared under Mice.
- QEMU made the USB mouse its current mouse. `mouse_move` from the monitor moved the guest pointer, so the pointer was driven by interrupt-IN transfers through this driver.
- URBs seen, in order: `0x0B`, `0x00`, `0x1B` (SET_IDLE), `0x28` (the report descriptor), `0x09` (the interrupt reads).

**Unplugging it bugchecked the guest: 0xCE** (DRIVER_UNLOADED_WITHOUT_CANCELLING_PENDING_OPERATIONS).
- The minidump, extracted from the guest disk (qemu-img to VHD, then 7-Zip) and read with WDK 7.1's kd, gives the faulting address `hidusb+0xb0a`, in the unloaded hidusb.
- The trace gives the order:
  1. The thread took the PDO off the bus and began Disable Slot.
  2. PnP sent SURPRISE_REMOVAL. hidusb's RESET_PIPE and ABORT_PIPE were refused at once, the PDO no longer naming a device, and REMOVE completed.
  3. The device-gone drain then completed the pending interrupt read, whose completion routine was hidusb's.
- Fixed in the tree:
  - a device PDO counts the URBs pended through it, and its STOP, SURPRISE_REMOVAL and REMOVE complete only when that count is 0, with the thread aborting the pipes of a device still present;
  - the device-gone drain also completes the device's queued slow URBs, which only the thread could otherwise complete while it waits for their references.

**Codex rounds 2 to 4 on the URB path, and the legs between them.**

Round 2 raised ten MAJOR and four MINOR, round 3 eight new MAJOR, round 4 four MAJOR and one MINOR; all were taken. The model that came out of them:
- **Per-pipe ordering:** a pause gate holds the pipe while the thread edits it. Records wait on a held list - unmapped when they wait for an owner or a pause, so they hold no map registers. A request split into chunks owns its pipe from its submission, and a sequence number keeps earlier submissions ahead of it. The doorbell is rung under the controller lock.
- **Endpoint state:** each stop, cancel, abort, reset and deconfigure reads the endpoint's state from the output context first. Running is stopped; Halted gets Reset Endpoint followed at once by Set TR Dequeue; Error gets Set TR Dequeue. RESET_PIPE recycles a non-Halted endpoint (Drop and Add) to restart its toggle.
- **Cancellation:** the cancel routine only marks the record (a CancelsRunning count guards the record's lifetime); the thread stops the endpoint, takes cancelled TDs off and places the dequeue as the miniport did.
- **Map pump:** its channel is released by a dedicated DPC queued from the execution routine.
- **PDO admission:** a device PDO closes admission, waits its in-flight dispatches and then its pending URBs before a STOP, SURPRISE_REMOVAL or REMOVE completes.
- **Unproven halt:** if the controller could not be proven halted, URBs stay pending and the record is kept.

**Windows 2000 SP4** (`ea898a7e...17cd6`, c5-2k; `f0163503...24ea`, c6-2k): the USB mouse works end to end. The pointer moves and clicks register. Unplug and replug work with no bugcheck. Disable and enable work for the HID-compliant mouse, the root hub (c5) and the controller (c6). Shutdown is clean.

**Windows 98 SE** (c4, c5, c6): still Code 10. hidusb's only URB is a SELECT_CONFIGURATION, which the c6 trace shows completed by the thread with success on the unconfigure path - its ConfigurationDescriptor was NULL - after which Windows stopped the device. Under investigation; the next build dumps the URB's fields.

**Codex rounds 5 to 7.**
- **Round 5** (four MAJOR, one MINOR):
  - a failed dequeue recovery could still ring the doorbell into the old DMA position;
  - an abort or cancel reset the host's data toggle without the device's;
  - tearing down devices one after another could deadlock on another device's map registers;
  - a late cancel routine could cancel a reused IRP's next transfer.
- **The fixes:**
  - a Halted endpoint is reset, its dequeue set, and CLEAR_FEATURE(ENDPOINT_HALT) sent;
  - invalidation and stop drain every device before any per-device wait;
  - the cancel routine compares a sequence number captured under the cancel lock.
- **Round 6** (one MAJOR): a CLEAR_FEATURE that failed was treated as success. Now a failure in the quiesce or in RESET_PIPE requests controller recovery and keeps the pipe paused.
- **Round 7:** clean.

**The Windows 98 SE Code 10** (c7-98, `dfc725b5...f523d`): the trace showed `hidclass.sys` sending IRP_MN_QUERY_CAPABILITIES with a DEVICE_CAPABILITIES of Version 0 and Size 0. The PDO refused it as too short, and Windows reported Code 10. Both PDO kinds now accept Version 0 / Size 0 as well as any Size that reaches `D1Latency`, and write no field past it.

**The first working mouse on Windows 98 SE, and an unplug that reset the guest** (c8, `f498e911...3490`):
- **On Windows 98 SE:**
  - The mouse installed as `USB Human Interface Device` plus `HID-compliant mouse`, both working, and the pointer moved.
  - Unplugging it left the guest "paused (shutdown)" within about 30 s, with no blue screen. Under `-no-reboot` that is a reset.
  - The cause: `hidclass.sys` resubmits its interrupt read from that read's completion routine. Each device-gone refusal was completed inside the dispatch that received the resubmission, so the stack grew with every retry until it overflowed. The trace shows one "refused" line after another up to the stop.
- **On Windows 2000** (c8-2k), the same build passed every step.

**Refusals completed later: Codex rounds 8 to 10.**
- The first fix had the controller thread complete refusals at its next poll.
- **Round 8** (five MAJOR) showed that was not enough:
  - the thread stops before the stop path drains transfers, and a drained read's completion resubmits;
  - a refusal could be queued after the final flush;
  - a client that kept resubmitting kept a STOP from finishing;
  - other refusals were still completed inline (a closed pipe, no free record, no MDL, the slow-URB queue);
  - the fallback read the IRP after completing it.
- **A static sweep after round 8** found three more inline failures: a record that failed before reaching the ring (a zero-length control transfer the ring refused, a failed adapter-channel request, a cancel while the request was being filled in).
- **Round 9** (two MAJOR, one MINOR):
  - a refusal timer in the controller could outlive the controller once a PDO was orphaned;
  - an orphaned PDO still refused inline;
  - a cancelled refusal left the URB's status unchanged.
- **Round 10:** clean.
- **The model that came out of them** (`HcdIoRefuseLater`, `src\hcd_io.c`):
  - Every refusal the URB path can reach is pended and completed by a one-shot timer DPC at the next clock tick, which also paces a resubmitting client. This includes a record that fails before the ring has it.
  - The list, timer and DPC belong to the device PDO, not the controller, so an orphaned PDO defers too.
  - The list sits under the cancel spin lock, so a refusal can be cancelled; a cancelled one completes with `STATUS_CANCELLED` and `USBD_STATUS_CANCELED`.
  - Refusals are counted apart from URBs (`RefusedPending`, plus one while the timer is armed or its DPC runs):
    - STOP and SURPRISE_REMOVAL wait for the URBs only, since a client may resubmit until its own stack hears of the stop;
    - REMOVE, which reaches the PDO after every driver above it has stopped submitting, also waits for the refusals;
    - the PDO is never deleted before that count is 0.

**Both primaries after the thread-completed refusals** (c9, `7f4bb2f6...0e01`):
- **Windows 98 SE** (c9-98):
  - Unplugging the mouse no longer stops the guest: it stayed running through 60 s and the HID devices left Device Manager.
  - Replug, disable and enable of the HID-compliant mouse, disable and enable of the root hub, and shutdown all passed.
  - After the mouse was re-enabled, its first move appeared some 20 to 30 s late; later moves were prompt.
- **Windows 2000** (c9-2k): install, move and click, unplug, replug, disable and enable of the mouse and of the controller, and shutdown all passed with no bugcheck.
