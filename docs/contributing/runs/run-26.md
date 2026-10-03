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

### Beyond the mouse: keyboard, tablet, several devices at once (c10 to c12)

**c10** (`747698e1...2d3f`, refusals on per-PDO timers):
- **Windows 2000** (c10-2k): every step passed.
  - The USB keyboard and the USB tablet both bound and worked.
  - The tablet moved the pointer as an absolute device.
  - Unplugging both 0.4 s apart left no bugcheck.
- **Windows 98 SE** (c10-98): the keyboard and tablet installed and worked. The tablet pinned the pointer to a corner, as Windows 98's HID mouse driver handles an absolute device.
- **The hang:** unplugging the keyboard and tablet back to back hung the guest.
  - The taskbar clock stopped and the Start menu stopped opening. No blue screen appeared.
  - `info registers` samples placed the CPU in `hidclass.sys`, in the routine that fails a collection's pending reads (`0x1030C`, called from `0x103BF`).
- **c10s-98:** a keyboard unplugged on its own hung the guest as well.

**The control leg (u1).** Windows 98 SE on its own UHCI stack (QEMU `piix3-usb-uhci`, no xhci98) was given the same keyboard and tablet unplugged 0.3 s apart. Twice, it did not hang, so the hang was this driver's.

**Class requests refused.** Every vendor or class URB with a nonzero `RequestTypeReservedBits` was refused with INVALID_PARAMETER, on both primaries.
- Windows 98 SE's `hidusb.sys` stores 0x22 there for SET_REPORT and SET_IDLE (`0x10C2A`, `0x11044`, static).
- Windows 2000 SP4's `usbport.sys` writes type, direction and recipient over that byte, which overlays the SETUP packet's bmRequestType, and then clears bits 4:2 (`0x235E4`, static). Nothing a client puts there reaches the bus.
- `XhciPipeBuildSetup` no longer reads the field, and the host suite carries hidusb's 0x22 case.
- With that change (c11, `8e81d9f8...8842`), a keyboard unplugged alone no longer hung Windows 98. Typing and Caps Lock worked. The keyboard and tablet unplugged together still hung.

**The REMOVE that never ended.** The c11 trace showed both device PDOs receiving REMOVE, and nothing after. `hidclass.sys` (98 SE) answers a read that fails with STATUS_DEVICE_NOT_CONNECTED in two ways while its device is still started:
- it fails every client read and resubmits at once (`0x110A7`; `0x10C20` tests the state at `0x10C60`, static);
- any other failure takes a one-second back-off instead.

It leaves the started state only after the REMOVE below it returns. That REMOVE waited for its refusals, which therefore never ended.

The fix (`9a593a6`): once a STOP or REMOVE has begun, the refusal DPC completes with STATUS_DELETE_PENDING.

**c12** (`e6e9e801...0f61`):
- **Windows 98 SE** (c12-98):
  - The keyboard and tablet unplugged 0.3 s apart: no hang, and the HID entries left Device Manager.
  - Keyboard, tablet and mouse unplugged within 0.5 s: no hang.
  - A mouse after that moved the pointer.
  - The root hub's disable and enable brought it back.
  - Shutdown was clean.
- **Windows 2000** (c12-2k):
  - Typing and Caps Lock worked on the USB keyboard.
  - The keyboard and tablet unplugged 0.3 s apart: no bugcheck.
  - The mouse worked after the controller's disable and enable, and its unplug was clean.
  - Shutdown was clean.

### SELECT_INTERFACE, GET_CURRENT_FRAME_NUMBER, and Codex rounds 11 to 15

**SELECT_INTERFACE** (`a50db22`) is a thread URB:
- The interface's open pipes are brought to rest and closed.
- One Configure Endpoint drops their endpoints, and any left enabled by an earlier failure (`dev->Stale`), and adds the new alternate setting's.
- Then the driver sends SET_INTERFACE. It reads the configuration the client selected, which is copied at SELECT_CONFIGURATION.

**GET_CURRENT_FRAME_NUMBER** answers at dispatch from the miniport's 32-bit MFINDEX-delta counter.

**Round 11** (three MAJOR, one MINOR):
- A STALLed SET_INTERFACE, which USB 2.0 9.4.10 lets a single-setting interface return, had been tolerated. It is now a failure, since Drop and Add reset the controller's toggles while the device keeps its own.
- EP0 is recovered before the thread's next SETUP.
- The configuration copy is taken before anything is committed.

**Rounds 12 and 13** (one MAJOR and one MINOR, then one MAJOR and two MINOR):
- A thread control transfer now waits until no client transfer is on EP0. It reads the halt flag and the queue count under one lock, recovers a STALL as it is found, and is bounded by an elapsed-time deadline.
- MFINDEX is sampled every 500 ms by a dedicated timer, so a long thread wait loses no 2,048-frame lap.

**Round 14:** clean.

**Round 15**, on the DELETE_PENDING change: no MAJOR or MINOR. Two notes:
- The REMOVE wait still relies on a client not resubmitting at once on DELETE_PENDING. The inspected clients back off: `hidclass.sys` 98 SE and 2000, `usbstor.sys` 2000, `usbaudio.sys` 2000.
- Before the PDO closes, a gone device's refusals still return DEVICE_NOT_CONNECTED, so Windows 98's hidclass retries once per timer tick until PnP removes it. c12 showed no harm from it.

### The Waiting list on both primaries, and a USB stick (c13)

**c13** (`72ce2098...2a34`, `08c0d5a`, 2026-10-03, development host A, fresh overlays): a transfer that finds every record of its pipe out waits on the pipe's Waiting list instead of being refused with ERROR_BUSY, and the transfer MDL is bounded to its byte count. Windows 98 SE ran on c13 and, after the install restart, c13b; Windows 2000 on c13. The stick was a QEMU `usb-storage` on a read-only `fat:` directory (`snapshot=on`) holding a 4 MiB random `RAND.BIN` (`04a6b846...fe78`) and `README.TXT`.
- **Windows 98 SE** (c13-98):
  - The controller install asked for the Windows 98 SE CD for `usbd.sys`, then for a restart.
  - The USB mouse installed (the CD again, for `hidclass.sys`) and moved the pointer.
  - The stick enumerated at 480 Mb/s and `USBSTOR.INF` installed with no file prompt. Windows then asked for a restart, answered No. The USB Mass Storage Device showed Code 10, with no disk drive and no drive letter.
  - Unplugging the stick, then the mouse: no hang and no dialog, and the Start menu opened after each.
  - Shutdown was clean.
- **Windows 2000** (c13-2k):
  - The controller install brought no CD or signature prompt and, unlike c12, no restart prompt.
  - The USB mouse moved the pointer.
  - The stick enumerated at 480 Mb/s and showed Code 10 ("This device cannot start"), with no disk and no drive letter. Two restart prompts followed, answered No.
  - Unplugging the stick, then the mouse: no bugcheck, and both left Device Manager.
  - The root hub's disable (Code 22) and enable passed, and shutdown was clean.

**Why the stick failed** (runtime, the debugcon traces). On both, `usbstor.sys` selects its configuration, which succeeds, and then sends IOCTL_INTERNAL_USB_RESET_PORT (0x00220007): once on Windows 98 SE, three times on Windows 2000. The HCD answers STATUS_NOT_SUPPORTED, and `usbstor.sys` fails its start. On Windows 98 SE it first unconfigures (a SELECT_CONFIGURATION with a NULL configuration) and the PDO then receives a STOP; on Windows 2000 the PDO receives a REMOVE.

The Waiting list was not exercised here: no client in this leg queues more than four requests on a pipe, so c13 was a regression check for HID. RESET_PORT (26-A.6) is what gates storage on both primaries, and it is taken next.

### Storage on both primaries, a USB audio device, and Codex rounds 16 and 17 (c14)

**c14** (`fbe2eb88...15c8`, `1fa0cc2`, 2026-10-03, development host A, fresh overlays): RESET_PORT, CYCLE_PORT, GET_BUS_INFO and GET_HUB_COUNT are served, on top of the Waiting list (`08c0d5a`) and isochronous transfers (`fb8d8f0`). Windows 98 SE ran on c14 and, after the install restart, c14b; Windows 2000 on c14. The stick was c13's.
- **Windows 98 SE** (c14-98):
  - The controller and the USB mouse installed as in c13, asking for the CD for `usbd.sys` and then `hidclass.sys`, and the controller for a restart. The mouse moved the pointer.
  - The stick enumerated at 480 Mb/s and `USBSTOR.INF` installed with no file prompt and, unlike c13, no restart prompt. An Explorer window opened on F: by itself, and Device Manager showed a USB Disk under Storage device, with no bangs.
  - `dir F:\` listed `RAND.BIN` (4,194,304 bytes) and `README.TXT` (18 bytes), and `type F:\README.TXT` printed `xhci98 c13 stick`.
  - `fc /b` reported "no differences encountered" for `F:\RAND.BIN`, for a copy to `F:\COPY.BIN`, and for a copy from F: back to C:. By the host clock the copy to F: took 3 s or less and the copy back 1 s or less.
  - Unplugging the stick, then the mouse: no hang and no dialog, and the Start menu opened after each.
  - Shutdown was clean.
- **Windows 2000** (c14-2k):
  - The controller installed with no restart prompt, and the USB mouse moved the pointer.
  - The stick enumerated at 480 Mb/s with no prompt at all, and appeared as "QEMU QEMU HARDDISK USB Device" under Disk drives and as F:.
  - `dir`, `type` and the three `fc /b` comparisons gave the same results as on Windows 98 SE. Each copy returned within 5.2 s by the host clock; the guest clock gave 0.05 s and 0.02 s, as the copies returned from cache and the writes to F: are lazy.
  - Unplugging the stick: no bugcheck and no dialog, and the disk left Device Manager.
- Neither trace has a RESET_PORT refusal or a "not served" line.

**The USB audio device.** On Windows 2000, a C-Media USB Audio Device (0D8C:0014, 12 Mb/s) was passed through from the host with `usb-host`, its bus and address read from `info usbhost`.
- Some 35 s later the guest had rebooted. It bugchecked on every boot while the device stayed attached: STOP 0x0000001E (0xC0000005, 0xBF48AA90, 0, 0x00310030) in `usbaudio.sys` (base BF486000, DateStamp 3803cad4).
- Each cycle's trace ends right after a second SELECT_CONFIGURATION (URB length 0x70) completed.
- The rest of the leg was not run.

**Why** (static, `usbaudio.sys` of Windows 2000 SP4; RVA 0x4A90 lies in the function that builds its configuration request):
- It sizes its interface list by the configuration's bNumInterfaces but fills only the audio interfaces and the NULL terminator, whose Interface field `usbd.sys`'s USBD_CreateConfigurationRequestEx leaves unwritten.
- It then reads list[i].Interface for every i < bNumInterfaces. For this device (AudioControl, two AudioStreaming and HID), the pointer at i = 3 is uninitialised.
- On the stock stack `usbaudio.sys` is bound to a function PDO, whose filtered configuration descriptor carries only its own interfaces. This HCD does not yet split composite devices (26-A.7), so `usbaudio.sys` was bound to the whole device through the first interface's class.

Audio therefore waits on 26-A.7, which is taken next. Isochronous transfers were not reached: the bugcheck came first.

**Round 16** (two MAJOR, four MINOR, fixed in `7df3809`): a waiter could be stranded, and an explicit StartFrame fell back to Start Isoch ASAP (SIA).

**Round 17** (two MAJOR, one MINOR, fixed in `f69b723`): the cadence condition, and an access in the refusal of a short URB. Round 18 is pending.

### Composite devices split into function PDOs, and Codex round 19 (c15)

**c15** (`04ab898b...f8ec`, `38635f6`, 2026-10-03, development host A, fresh overlays): composite devices are split into function PDOs (26-A.7). Windows 98 SE ran on c15 and, after the install restart, c15b; Windows 2000 on c15. The stick and the C-Media device were c14's.
- **Both primaries**:
  - The controller and the USB mouse installed as in c14, and the mouse moved the pointer. On Windows 2000, an Explorer box, "E:\ is not accessible.", followed the controller's Finish; E: read normally from `cmd` later.
  - The stick appeared as F:, and `fc /b` reported "no differences encountered" for `RAND.BIN` and for a copy to `F:\COPY.BIN`. Its unplug was clean.
- **The audio device** (runtime, the debugcon traces). On both, the passthrough 0D8C:0014 was split into two function PDOs, MI_00 for audio (interfaces 0 to 2) and MI_03 for HID:

  ```
  split device configured, port/slot/value=00030201
  device PDOs created, port/count=00030002
  function PDO, port/MI=00030000
  function PDO, interface mask=00000007
  function PDO, class/subclass/protocol=00010100
  function PDO, port/MI=00030003
  function PDO, interface mask=00000008
  function PDO, class/subclass/protocol=00030000
  ```

  - No bugcheck: c14's STOP 0x1E is gone. On Windows 98 SE the HID function installed from `HIDDEV.INF` and the audio function from `WDMA_USB.INF`, with no prompt and no restart.
  - Device Manager showed the USB Audio Device and the HID-compliant consumer control device, each "This device is working properly.", and no composite parent entry. The USB Audio Device was the preferred playback device.
  - Playback failed on both: "Your audio hardware cannot play files like the current file." (Sound Recorder, three plays on Windows 98 SE, one on Windows 2000), and on Windows 2000 Media Player's "All wave devices that can play files in the current format are in use." (two plays). No sound played.
  - The HCD refused `usbaudio.sys`'s SELECT_INTERFACE to the streaming alternates, `select interface refused, length/number/alt=00240101` (each `thread URB done, function/status=01000300`), and earlier to `00100100` and `00100200`. On Windows 98 SE one select of interface 1, alternate 1 succeeded between refusals (`interface selected, number/alt=00000101`).
  - It also refused a class-interface request, `function control refused, bmRequestType/wIndex=00A10054` on Windows 2000 and `00A10060` on Windows 98 SE, each with `URB refused at dispatch, function/status=1B000300`.
  - No isochronous URB reached the HCD. Unplugging the audio device was clean on both.
- **Windows 2000** also passed the root hub's disable (Code 22) and enable. Both shutdowns were clean.

**Round 19**, on 26-A.7 (three MAJOR, four MINOR): the filtered configuration copy was not bounded by the MDL, the alternate-0 fallback claimed success, and the split rule differed from Microsoft's composite-parent rule. Both c15 defects were fixed with them in `3191f72`: the SELECT_INTERFACE had been bounded by UrbHeader.Length, which usbport does not read, and that bound is gone; and of class requests, only one naming a sibling function's interface is now refused. Round 20 is pending, and audio playback is the next leg's check.

### The golden images (2026-10-03)

Each leg to c15 started from the clean bases, so every debugging leg first ran the controller install, the class-driver wizards, Windows 98 SE's CD prompts and its restart. From c16 the debugging legs start from a golden image per primary instead: a fresh overlay over the same clean base, on which the controller, the root hub, the HID class drivers (mouse, keyboard, tablet) and `usbstor.sys` were installed once, one device at a time, each unplugged cleanly.
- **Windows 98 SE** (`vm\t26\win98-gold.qcow2`): `C:\AUTOEXEC.BAT` holds one line, `if exist D:\XHCI98.SYS copy /Y D:\XHCI98.SYS C:\WINDOWS\SYSTEM32\DRIVERS\XHCI98.SYS`, so each boot puts the package's driver over the installed one.
- **Windows 2000** (`vm\t26\win2k-gold.qcow2`): saved with the controller disabled (Code 22). A leg copies the `.sys` over the installed one (`copy /Y E:\xhci98.sys C:\WINNT\system32\drivers\xhci98.sys`, "1 file(s) copied.") and enables the controller, which starts with no restart prompt. Proven on the image before it was saved.

The launchers now give `qemu-xhci` eight USB 2.0 ports (`p2=8`). The first attempt at each image ran on four, and the stick, plugged as the fourth device, made QEMU insert its own full-speed hub (0409:55AA) on port 4 with the stick behind it. That exposed the hub defect fixed in `4e79718` (runtime, the debugcon traces): on Windows 2000, `usbhub.sys` bound the hub's device PDO and sent GET_ROOTHUB_PDO (`internal IOCTL not served=0022000F`), and the guest reset some 23 s into every boot from then on; Windows 98 SE showed the hub as "NEC USB Hub" with Code 2. Both images were redone on fresh overlays with eight ports, and no hub appeared.

Formal 26-V legs still install from the clean bases. A build that changes the INF or the device ids refreshes the golden images.

### Audio playback on both primaries (c16)

**c16** (`636df7db...bf46`, `e2470ce`, 2026-10-03, development host A, `qemu` flavour, the golden images): Codex round 20's fixes over c15's two audio fixes (`3191f72`). The stick and the C-Media device were c14's. This is the first audio playback reading of the HCD on both primaries.
- **Both primaries**:
  - The driver swap held: on Windows 98 SE the installed `XHCI98.SYS` matched `D:\XHCI98.SYS` (115,328 bytes); on Windows 2000 the copy gave "1 file(s) copied." and the enable brought no prompt. The controller and the root hub were "working properly".
  - The stick appeared as F:, and `fc /b` reported "no differences encountered" for `RAND.BIN`. Its unplug was clean. On Windows 98 SE the `USBSTOR.INF` wizard ran again, with no prompt.
- **The audio device** (runtime, the debugcon traces). On both, 0D8C:0014 was split into MI_00 (audio) and MI_03 (HID) as in c15, and both functions showed "This device is working properly." On Windows 98 SE the HID function's install asked for the CD for `hidclass.sys`; the audio function installed from `WDMA_USB.INF` with no prompt and no restart.
  - `usbaudio.sys`'s SELECT_INTERFACE to the streaming alternate was accepted on both, at every play: `interface selected, number/alt=00000101`, `endpoint mask=00000004`. Neither trace has a `select interface refused`, `function control refused`, `URB refused at dispatch` or `not served` line.
  - Isochronous URBs were admitted, two per play (`isoch URB, packets << 16 | ASAP << 15 | start - now=00018000`, then `=000A8000`), and published: 32 `isoch published, TRBs << 16 | Frame IDs << 8 | packets=` lines on each, `00010001` then `000A000A` and `000B000A`, all in the first play. The site prints only when its value changes, which is why the later plays show none.
  - Playback ran with no error on both. Windows 2000: Sound Recorder and Media Player, each given `chimes.wav`, closed by themselves; Media Player played "Windows Logon Sound.wav" (5.51 s), its position advancing through 00.39, 00.90, 01.26 and 03.97 over some 17 s, then closed by itself. Windows 98 SE: Sound Recorder played three files, each closing by itself, and showed Position 4.75 of 7.85 sec (`The Microsoft Sound.wav`) and 0.25 of 0.63 sec (`CHIMES.WAV`) while playing. Nothing was heard: the guest has no audio path out, and audible playback is the bench's.
  - Each stream ended with completion code 0x0E (Ring Underrun), `event: transfer, code=0000000E`, on Windows 98 SE; Windows 2000's trace has no transfer-event line after the attach.
  - EP0 STALLs (code 6) were recovered: on Windows 98 SE five `event: transfer, code=00000006` during the audio function's start, each followed by `EP0 reset after a stall, slot=00000001`; on Windows 2000 one `EP0 reset after a stall` at the attach and one at the start of each play.
  - Unplugging the audio device was clean on both.
- Both shutdowns were clean.

### The combined leg on the golden images (c17)

**c17** (`dfa8c929...67a2`, `d5950c4`, 2026-10-03, development host A, `qemu` flavour, the golden images): the A.8 surfaces (the controller's Advanced tab, the root hub's Power tab, the `XHCISNAP` channel) and the Codex fixes to `d5950c4`, among them the timer-slot fix of `370c135`, read together with storage, unplug, audio, disable and enable, and shutdown. The stick and the C-Media device were c14's.
- **The driver swap** held on both. Windows 98 SE: the installed `XHCI98.SYS` matched `D:\XHCI98.SYS` (133,808 bytes). Windows 2000: the copy gave "1 file(s) copied." and the enable brought no prompt. The controller and the root hub were "This device is working properly." on both.
- **Storage.** The stick enumerated at 480 Mb/s and appeared as F:, and `fc /b` reported "FC: no differences encountered" for `RAND.BIN` on both. On Windows 98 SE the `USBSTOR.INF` wizard ran again, with no prompt and no restart, and an Explorer window opened on F:; on Windows 2000 there was no prompt.
- **The controller's Advanced tab** (stick and mouse plugged). On both the tabs are General, Advanced, Driver and Resources.
  - Windows 98 SE: the Advanced tab carries "USB Settings:", a "Disable USB error detection" check box (clear) and a "Bandwidth Usage" button, which opens a dialog titled "Advanced".
  - Windows 2000: the Advanced tab is itself the bandwidth page, with the same check box and a Refresh button; there is no separate dialog.
  - The bandwidth page is the same on both: "Each USB controller has a fixed amount of bandwidth, which all attached devices must share. This page displays the bandwidth distribution among all bandwidth consuming devices attached to the controller." and "By selecting a device from the list, the corresponding section will be selected in the Bandwidth indicator bar, which gives a visual representation of bandwidth consumption." Its list ("Device Description | Bandwidth consumed") holds one row, `System reserved | 11 %`, with the stick and the mouse plugged, and unchanged after a Refresh on Windows 2000.
- **The root hub's Power tab.** On both the tabs are General, Power and Driver. On Windows 2000 the tab is the page; on Windows 98 SE a "Power properties" button opens it as a dialog titled "Power". As read, with the stick and the mouse plugged:

  ```
  Hub Information:  The hub is self powered.
                    Total power available:  500 mA per port.
  Devices on this Hub:  USB Mass Storage Device   Unknown
                        HID Keyboard Device       Unknown      (2000)
                        HID-compliant keyboard    Unknown      (98 SE)
                        6 port(s) available.      0 mA
  ```

  After the root hub's disable and enable, with nothing plugged, it read "8 port(s) available. | 0 mA" on both.
- **`XHCISNAP`** (from the package drive). The usage screen printed in full on both. `XHCISNAP -probe` gave the same four readings on both:

  ```
  route probe - IOCTL 0x00220438 on this controller:
    PassThru, our GUID                status  6 (MINIPORT DECLINED)
      the request reached a miniport and it DECLINED. The ROUTE WORKS.
    unknown request code 15           status  2 (invalid request code)
    RequestBufferLength disagreeing   status  4 (invalid header parameter)
    a 0x20-byte buffer                status  7 (buffer too small)
  ```

  `XHCISNAP -o C:\SNAP` was refused, "usbport refused the request: 6 (MINIPORT DECLINED)", exit code 1 on Windows 2000, and no `C:\SNAP*` file on either. That is the channel shipping off, as the tool says; the route works. `-verbosity 2` needs a restart and was not run, so the dump itself is left to the clause legs (26-V.1, 26-V.2).
- **Unplug.** The stick and the mouse, together: no dialog and no hang on either, and both left Device Manager.
- **"Ex refused the arm": 0 on both**, over the whole session (c16: 21 on Windows 2000, 29 on Windows 98 SE). That is `370c135`'s fix: each command's 5 s watchdog held one of the timer service's four slots for its full 5 s after the command completed, so a burst of more than four commands ran unwatched; a new arm of the same callback now supersedes the pending one and reuses its slot.
- **Audio.** 0D8C:0014 split into MI_00 and MI_03 as in c16, and both functions bound; on Windows 98 SE the HID function's install asked for the CD for `hidclass.sys` again (given `E:\WIN98`) and the audio function installed from `WDMA_USB.INF` with no prompt and no restart. Each OS was given two plays of one file, and every play ran to its end and closed by itself, with no error dialog. On each OS one of the two ran at about a tenth of real time:
  - Windows 2000 (`mplay32 /play /close`, "Windows Logon Sound.wav", 5.51 s): play 1 slow, its position at 00.87, 01.43, 02.28, 03.13 and 05.22, the last some 58 s after the command, then closed (about 0.1x); play 2 at real time (05.01 some 5 s after the start).
  - Windows 98 SE (`sndrec32 /play /close`, "The Microsoft Sound.wav", 7.85 s): play 1 at real time (7.50 sec some 9 s after the start); play 2 slow, 7.50 sec after some 65 s, its position standing at 2.00 sec for at least 2 s (about 0.12x).
  - Each trace has 4 `isoch URB` lines (two per play) and 32 `isoch published` lines, all in play 1, and no `not served` line and no nonzero refusal counter.
  - A static read of the isochronous path by a subagent found nothing in the driver that throttles a stream; the cause of the slow plays is open.
  - Why the later plays print no `isoch published`: that site is an `XHCI_DBG_VALUE_CHANGED`, which prints on a change of value **at most `XHCI_DBG_VALUE_LIMIT` (32) times per driver image**, and play 1 spends all 32. It is the trace's cap, not a missing publication - the slow play is the one with the lines on Windows 2000 and the one without them on Windows 98 SE.
  - Unplugging the audio device was clean on both.
- **Disable and enable.** Windows 2000: the root hub's disable ("Disabling this device will cause it to stop functioning. Do you really want to disable it?") gave Code 22 with the controller still working, and its enable "working properly", both with no prompt; then the controller's disable removed the root hub (`teardown: ports unpowered=00000008`, `ports that would not give up power=00000000`, `quiesce: halted, USBSTS=00000001`) and its enable brought both back, "working properly". Windows 98 SE: the root hub only, through "Disable in this hardware profile": "This device is disabled (Code 22.)", then "This device is working properly." after the box was cleared. The controller was not disabled on Windows 98 SE.
- **Shutdown** was clean on both: Windows 2000 reached "It is now safe to turn off your computer." (`save: declined - the controller does not declare FSC`), Windows 98 SE powered off.

**The Windows 98 SE mouse that came up as a keyboard.** The usb-mouse brought no wizard and Device Manager showed it as "HID-compliant keyboard" under Keyboard, with no HID mouse under Mouse; `mouse_move` through it did not move the pointer, while the PS/2 mouse did. The coordinator's read: the HCD gives a device the port number as its instance id, and QEMU's `usb-kbd`, `usb-mouse` and `usb-tablet` all present `VID_0627&PID_0001`, so the mouse on port 2 matched the devnode the golden image's keyboard had left on port 2, and Windows reused its keyboard driver. It is an artefact of the image, not a reading of the HCD (the stock leg's mouse, 26-V.3 below, installed as a mouse and moved the pointer). Windows 2000 named the same mouse "HID Keyboard Device" in the Power tab, which the same reuse explains; its pointer was not tried. Instance ids from the device's serial number, where it has one, are noted as a later change.

Notes: `out\phase26\v0\c17-2k-notes.md`, `c17-98-notes.md`, and their screenshots.

## 26-A.10 - the first HCD device-matrix run

The first run of `matrix-hcd.psd1`, 26-A.10's expectation set, through `scripts\vm-matrix\run-matrix.ps1` on 2026-10-04 (development host A, QEMU 11.1.0), groups `audio`, `hid`, `storage` and `other`. Targets: overlays of the two golden images with the `qemu` flavour of `40efd31` (`xhci98.sys` SHA-256 `95abc0aa...78b4`) in place, `h98` (Windows 98 SE) and `h2k` (Windows 2000). A per-target `ExtraArgs` key (`f9f57fe`) matches each run to its image's hardware (`-net none`, and Windows 2000's `-vga cirrus`). The run was driven by a matrix subagent, and this is its report, checked against the report files.

| Row | h98 (Windows 98 SE) | h2k (Windows 2000) |
|---|---|---|
| usb-audio/fs | PASS (isochronous counters `inert`, the `2a` line) | PASS |
| usb-kbd/hs | PASS | PASS |
| usb-kbd/fs | PASS | PASS |
| usb-mouse/hs | PASS | PASS |
| usb-mouse/fs | PASS | PASS |
| usb-tablet/hs | EXCLUDED | PASS |
| usb-wacom-tablet/fs | EXCLUDED | PASS |
| usb-storage/hs | PASS | PASS |
| usb-bot/fs | FAIL in run 1 (the expectation), PASS in the rerun | the same |
| usb-uas/fs | FAIL in run 1, NODRIVER in the rerun | the same |
| usb-net/fs | NODRIVER | NODRIVER |
| usb-serial/fs | NODRIVER | NODRIVER |
| usb-braille/fs | NODRIVER | NODRIVER |
| usb-ccid/fs | NODRIVER | NODRIVER |
| u2f-emulated/fs | PASS (the image taught the device in the prep pass) | NODRIVER |

- The harness's row summaries: h98 "PASS 7, NODRIVER 4, FAIL 2, EXCLUDED 2"; h2k "PASS 8, NODRIVER 5, FAIL 2".
- Every NODRIVER above is the row's `ExpectNoDriver` for its target, read as design record 06 defines it: `advance devices addressed` +1 and `-> NODRIVER advance endpoints opened >= 1 +0`.
- The h98 audio row's `inert` line, verbatim: `inert iso packets answered because Windows 98 SE USBAUDIO.VXD faults after one URB - exonerated in batch 9-V through a UHCI control`. The h2k row's: `inert iso packets answered because nothing in an unattended run plays audio ...`, with `zero iso missed service errors` and `zero iso packet errors` both 0.
- The two h98 exclusions are the set's `ExcludedOnTarget`: "installing this driver hangs QEMU on Windows 98 - 3 of 4 main-loop hangs measured; unresolved, and it is the vehicle rather than the miniport", and the Wacom row with it.

**usb-bot and usb-uas at High Speed.** Run 1 failed both rows on both targets on the speed expectation alone:

```
h98    usb-bot/fs             -> FAIL   zero port speed decoded - high speed                           1
h98    usb-bot/fs             -> FAIL   zero slot context speed - high speed                           1
h98    usb-bot/fs             -> FAIL   advance port speed decoded - full speed == 1                   +0
h98    usb-bot/fs             -> FAIL   advance slot context speed - full speed == 1                   +0
```

and the same four lines for `usb-uas/fs` (with `-> FAIL advance endpoints opened >= 1 +0`), and on h2k. QEMU 11.1 presents both, with their `scsi-hd` child, at 480 Mb/s, and the HCD's decoded port speed and the speed it programmed into the Slot Context agree (`zero slot speed disagreeing with port speed` 0). The rows' expectation was wrong, not the HCD: `f9f57fe` sets their `ExpectedSpeed` to `HS`, keeping the row names. The storage group was rerun on both targets after the change:

```
h98    usb-bot/fs             PASS      advance port speed decoded - high speed == 1                   +1
h98    usb-bot/fs             PASS      advance slot context speed - high speed == 1                   +1
h98    usb-uas/fs             -> NODRIVER advance endpoints opened >= 1                                  +0
h2k    usb-bot/fs             PASS      advance port speed decoded - high speed == 1                   +1
h2k    usb-uas/fs             -> NODRIVER advance endpoints opened >= 1                                  +0
```

with the summaries "PASS 2, NODRIVER 1" on each; `usb-storage/hs` passed again, and `usb-uas/fs`'s NODRIVER is its `ExpectNoDriver` (neither OS has a UAS class driver).

Reports: `out\mx\h98\device-matrix-h98.txt`, `out\mx\h2k\device-matrix-h2k.txt`; the reruns `out\mx\h98-storage2\device-matrix-h98-storage.txt`, `out\mx\h2k-storage2\device-matrix-h2k-storage.txt`.

## 26-V.1 - Windows 98 SE under NUSB 3.3 and under SweetLow's stack

On 2026-10-04 (development host A, QEMU under TCG, one virtual CPU, `qemu` flavour), one guest at a time, each install from a clean overlay; driven by a GUI subagent, and this is its notes and screenshots as read. Two builds ran, and every row below names its build:
- **pkg-v**: the `qemu` flavour of `40efd31`, `xhci98.sys` SHA-256 `95abc0aa...78b4`, the build of the matrix run and of 26-V.3.
- **pkg-v2**: the `qemu` flavour of `a7ddbfa`, `xhci98.sys` SHA-256 `685a4155...1ee7` (134,288 bytes), which adds `ac25e4e`'s fix (below) and nothing else. Both hashes were checked on the host before launch.

The guests:
- **NUSB 3.3**: overlays of the NUSB base (`win98-clean.qcow2`). `win98-v1n` took rows 1 to 6 on pkg-v and was then lost to the tooling (below); `win98-v1n2` took row 1 again on pkg-v and the ASIX install that faulted; `win98-v1n3` took rows 1, 5, 7 and 8 on pkg-v2 from clean.
- **SweetLow's stack**: `vm\sweetlow-2a.img @ sweetlow-stack-nodriver`, converted to a read-only base `vm\t26\win98-sweetlow-base.qcow2` (`qemu-img convert -l`, check clean), overlay `win98-v1s`, launched as the matrix's 2a-sweetlow target (`-machine pc,smm=off`). Rows 1 to 4, 6 and 8 on pkg-v; then Update Driver to pkg-v2 and rows 7, 5 and 8 again.

| Row | NUSB 3.3 | SweetLow |
|---|---|---|
| 1 Install, start, the root hub with every device beneath it | pkg-v (twice) and pkg-v2 from clean. Add New Hardware Wizard for "PCI Universal Serial Bus", "Specify a location" `D:\`, found `D:\XHCI98.INF`. **Prompts**: "Please insert the disk labeled 'Windows 98 Second Edition CD-ROM', and then click OK."; "The file 'usbd.sys' on Windows 98 Second Edition CD-ROM cannot be found." (given `E:\WIN98`); "To finish setting up your new hardware, you must restart your computer." (Yes). After the restart: "This device is working properly.", tabs General, Advanced, Driver and Resources, Driver Provider "Yeo Kheng Meng"; by connection, PCI bus > `xHCI98 USB 3.x eXtensible Host Controller` > `xHCI98 USB 3.x Root Hub`, and with the mouse and the stick plugged, the root hub > "USB Human Interface Device" > "HID-compliant mouse" and the root hub > "USB Mass Storage Device" > "USB Disk" | pkg-v, the same wizard, the same three prompts, the same Device Manager and tree. **pkg-v2 by Update Driver** ("Display a list ...", Have Disk `D:\`): "Update Driver Warning" ("Windows detects that your current driver may be a closer match ...", Yes - the two INFs carry the same version and date), the CD prompt for `usbd.sys` again, "Windows has finished installing the driver you selected for your hardware device.", **no restart prompt**; the new file waited as `XHCI98.TMP` until the restart that row 7 takes, after which `XHCI98.SYS` alone was pkg-v2's (134,288 bytes) |
| 2 Mouse | pkg-v. The `HIDDEV.INF` wizard, the CD prompt and "The file 'hidclass.sys' on Windows 98 Second Edition CD-ROM cannot be found." (`E:\WIN98`), no restart. `mouse_move` through the HID mouse moved the pointer and back | pkg-v, the same |
| 3 Storage | pkg-v. The `USBSTOR.INF` wizard, no prompt, no restart; F: "Volume in drive F is QEMU VVFAT"; `fc /b d:\rand.bin f:\rand.bin`: "FC: no differences encountered". The unplug: no dialog | pkg-v, the same |
| 4 Composite audio (C-Media 0D8C:0014, passthrough) | pkg-v. Two wizards, `HIDDEV.INF` and `WDMA_USB.INF`, no prompt, no restart. "USB Audio Device" ((Generic USB Audio)) and "HID-compliant consumer control device" each "This device is working properly.", beneath the root hub with no composite parent. `sndrec32 /play /close "The Microsoft Sound.wav"` (7.85 s): Position 1.25 sec some 4 s in, 4.75 sec some 8 s in, closed by itself within some 15 s, no error - about real time. 31 `isoch published`, two `event: transfer, code=0000000E`. The unplug: no dialog | pkg-v, the same installs and Device Manager. The play: Position 5.00 sec some 7 s in, closed by itself, no error; 32 `isoch published` |
| 5 The ASIX AX88772A (0B95:7720, passthrough, 480 Mb/s) | **pkg-v, no driver**: "Windows was unable to locate a driver for this device." then "USB Device", Code 28, beneath the root hub. **pkg-v with ASIX's Windows 98 driver** (`D:\ASIX98`, `AX88772.SYS` 3.0.3.12): **"A fatal exception 0E has occurred at 0028:C188D256 in VXD ax88772(01) + 00001576."** (below). **pkg-v2 with the same driver**: no fault; "ASIX AX88772 USB2.0 to Fast Ethernet Adapter", Network adapters, "This device is working properly." | **pkg-v2 with the same driver**: no fault; Network adapters, Manufacturer ASIX, "This device is working properly.", Driver Provider ASIX. Not tried on pkg-v: after the NUSB fault the coordinator held the ASIX install for the fixed build |
| 6 The Advanced tab and the Power tab (mouse and stick plugged) | pkg-v. The Advanced tab: "USB Settings:", "Disable USB error detection" (clear), "Bandwidth Usage", whose "Advanced" dialog reads as c17's, one row `System reserved` `11 %`. The root hub's "Power properties" dialog: "The hub is self powered.", "Total power available:  500 mA per port.", `HID-compliant mouse` `Unknown`, `USB Mass Storage Device` `Unknown`, `6 port(s) available.` `0 mA` | pkg-v, the same text |
| 7 `XHCISNAP` | pkg-v2. `-verbosity 2`: `0002 was 0 -> 2 NTMPDriver = xhci98.sys`, "1 key(s) set. RESTART THE MACHINE, ..."; after the restart, `-o c:\snap` with the ASIX adapter plugged: port 1 `00000E03`, ports 2 to 8 `000002A0`, "(1 of 8 ports report a device connected.)", `verbosity read 2, APPLIED 2`, `note ring 885 of 16384 bytes`, **`coherence *** TORN - counters may be a mixture`**; `SNAP.PSC` 32, `SNAP.TXT` 2,367, `SNAP.BIN` 104,740 bytes | pkg-v2. The same `-verbosity 2` text; after the restart `-probe`: "PassThru, our GUID  status 0 (success)", "the miniport ANSWERED - the channel is live, take the dump", and statuses 2, 4 and 7 for the other three probes as c17's; `-o c:\snap` with the adapter plugged and idle: the same port table and summary with **`coherence no tearing`**; `SNAP.PSC` 32, `SNAP.TXT` 2,287, `SNAP.BIN` 104,740 bytes |
| 8 Unplug all, shut down | pkg-v2: the adapter's unplug left `info usb` empty, no dialog; "Shut down" reached QEMU's `paused (shutdown)` | pkg-v and pkg-v2, each clean. pkg-v's trace ends `quiesce: halted, USBSTS=00000001`, `SuspendController: halted, USBCMD=00000000`, `save: declined - the controller does not declare FSC, HCIVERSION=00000100`; its `refused` lines are counters at `00000000` |

The install prompts, on both stacks: the Windows 98 SE CD for `usbd.sys` (the controller), `hidclass.sys` (the first HID device) and `choosusr.dll` (ASIX's driver), ASIX's own "AX88772/AX88772A USB2.0 to Fast Ethernet Driver Diskette" prompt, and a restart prompt after the controller's install and after ASIX's. The `SNAP.TXT` of both stacks carries the start-time records only (`log.verbosity=00000002`, `hc.version=00000100`, `hc.maxslots=00000040`, `hc.maxports=00000008`, `hc.pci=000D1B36`, `map.usb2only=00000008`, `door.hcd=00000000`, `door.interface=00000001`, ...); verbosity 2 writes no per-device lines.

**ASIX's Windows 98 driver and the fault on pkg-v.** The trace at the fault (`vm\t26-win98-v1n2-boot3-debugcon.log`), the adapter's enumeration and its driver's first URBs:

```
xhci98: hcd: device descriptor, idVendor/idProduct=0B957720
xhci98: hcd: configuration descriptor, bytes=00000027
xhci98: hcd: device enumerated on port=00000001
xhci98: hcd: first URB of function=0000000B
xhci98: hcd: first URB of function=00000000
xhci98: transfers submitted=00000001
xhci98: hcd: select, URB length=00000018
xhci98: hcd: select, ConfigurationDescriptor=C14E9A40
xhci98: hcd: select, first interface length/number/alternate=00000000
xhci98: hcd: select refused, bLength/wTotalLength=00100000
xhci98: hcd: thread URB done, function/status=00000300
xhci98: selects failed=00000001
xhci98: event: transfer, code=0000000D
xhci98: event: transfer, code=00000001
```

A GET_DESCRIPTOR (function 0x0B), then a SELECT_CONFIGURATION of 0x18 bytes, with no interface entry, whose configuration descriptor read `bLength` 0x10 and `wTotalLength` 0. The HCD refused it (`src\hcd_cfg.c`'s `bLength`/`wTotalLength` floor), and the driver faulted. The cause, from a static read of `AX88772.SYS` and NUSB's `USBPORT.SYS` by a subagent (2026-10-04, not re-read line by line by the coordinator; `legal-provenance.md` section 4):
- **The driver polls instead of waiting.** `AX88772.SYS` 3.0.3.12 waits for its URBs by polling `Irp->IoStatus.Status` for `STATUS_PENDING` (`cmp dword ptr [esi+18h],103h` at 0x103B5 in the helper at 0x1033E, which then calls `IoFreeIrp`; a second helper at 0x102C8 polls at 0x10327 between `NdisMSleep`s), and imports no `KeWaitForSingleObject`.
- **The HCD left the field at 0.** `IoAllocateIrp` leaves `IoStatus.Status` 0, and pkg-v pended a URB IRP without writing it. The driver read success at once, freed its GET_DESCRIPTOR IRP while it was still in flight, and built its SELECT_CONFIGURATION from a buffer the descriptor had not yet been read into - the 0x10 and 0 above. The fault site, 0x11836, is `.text+0x1576`, after the select at 0x11821 and a read of `NumberOfPipes` at 0x1182D: the `+ 00001576` of the blue screen.
- **usbport writes it.** NUSB's `USBPORT.SYS` stores `STATUS_PENDING` (`mov dword ptr [esi+18h],103h` at 0x15851) before `IoMarkIrpPending` (0x1585B), which is why the driver works under the miniport. Its select handler would have refused this URB too (`wTotalLength` 0 is refused at 0x2E57F with 0xC0000F00), so the refusal was right and the IRP's status was the defect.

`ac25e4e` sets `IoStatus.Status` to `STATUS_PENDING` on entry to `HcdDevicePdoInternalIoctl`, before anything can complete the IRP; every synchronous completion overwrites it. On pkg-v2 the same install selected with a 0x64-byte URB (`select, first interface length/number/alternate=004C0000`, `select endpoints requested=00000003`), with no refusal, and bound on both stacks. No packet was sent: the SweetLow guest ran with `-net none`.

**The overlay the tooling broke.** During `win98-v1n`'s second boot, after row 5, the background shell task that ran its QEMU hit the agent harness's 30-minute default limit and was stopped, which ended QEMU with Windows 98 running; the guest had answered normally a minute before. The next boot gave "Warning: Windows has detected a registry/configuration error.", ScanDisk, then "Invalid VxD dynamic link call from VWIN32(01) + 00000714 to device "0009", service 1. Your Windows configuration is invalid.", with an empty trace (`xhci98.sys` never loaded). That is the tooling's, not the guest's or the HCD's: rows 1 to 6 stand as read on it, and the rest moved to fresh overlays, the launcher task now with a 2-hour limit.

**`XHCISNAP`'s closing text** still says "Windows' USB port driver owns that door and opens it to anyone, so this value IS the lock.", which the HCD no longer sits under. A stale string, left for a later change to the tool.

**The NUSB dump's tearing.** NUSB's `SNAP.TXT` reads "coherence: tear detector 3994 -> 3996 (+2), *** CHANGED between windows", taken with the ASIX adapter bound and active; SweetLow's, with the adapter bound and idle, "no tearing". Recorded as observed: the tool saw the counters move between its windows and said so.

Notes: `out\phase26\v0\v1-98-notes.md` and its screenshots (`shots\v1-98-*`).

## 26-V.2 - Windows 2000 SP4

On 2026-10-04 (development host A, `qemu` flavour), driven by a GUI subagent; this is its notes and screenshots as read. The overlay `vm\t26\win2k-v2.qcow2` of the clean Windows 2000 base. Rows 1 to 3 and 5 to 7 ran on **pkg-v** (`95abc0aa...78b4`); row 4 failed on pkg-v and was rerun on **pkg-v2** (`685a4155...1ee7`), and row 8, the SMP guest, was read on pkg-v2 - the builds of 26-V.1 above.

| Row | Reading |
|---|---|
| 1 Install from clean (pkg-v) | The Found New Hardware Wizard was cancelled to take the Device Manager route, and a "System Settings Change" box ("Windows 2000 has finished installing new devices. You must restart your computer ...") followed, before any `xhci98` install (No). "Universal Serial Bus (USB) Controller" under Other devices, Code 1; Update Driver, "Search for a suitable driver ...", "Specify a location" `E:\`, found `e:\xhci98.inf`, "Completing the Upgrade Device Driver Wizard". **No prompt** - no signature prompt, no file prompt, no restart request. The controller and the root hub "This device is working properly.", the root hub beneath the controller. The controller had no Advanced tab and its Driver tab read Provider Unknown: an empty driver key, below, which a second Update Driver filled |
| 2 Driver Verifier | `verifier /flags 0x1B /driver xhci98.sys` gave `Level: 00000000` after the restart: `verifier /?` says "FLAGS is a decimal combination of bits". `verifier /flags 27` (0x1B: special pool, force IRQL checking, pool tracking, I/O verification) gave `Level: 0000001B`, "Name: xhci98.sys, loads: 1, unloads: 0", from the next boot. Verifier was on for rows 3 to 8 |
| 3 Mouse, storage (pkg-v, Verifier on) | No prompt, no wizard. The HID mouse moved the pointer; root hub > "USB Human Interface Device" > "HID-compliant mouse". The stick: F: "QEMU VVFAT", `fc /b E:\RAND.BIN F:\RAND.BIN`: "FC: no differences encountered". Both unplugs: no dialog |
| 4 Composite audio (Verifier on) | **pkg-v: FAIL**. The split and both functions bound with no wizard and no prompt ("USB Audio Device"; "USB Human Interface Device" > "HID-compliant consumer control device"), but both plays of `mplay32 /play /close "Windows Logon Sound.wav"` stood at 00.00 with "All wave devices that can play files in the current format are in use.  Wait until a wave device is free, and then try again." **pkg-v2: PASS**: two plays ran to their end, 00.89 then 04.92 within some 8 s of the first, each closing by itself, with no error. Below |
| 5 The ASIX AX88772A (pkg-v, Verifier on) | With no driver, "USB Device" under Other devices (wizard cancelled). With ASIX's Windows 2000 driver (`E:\ASIX2K`, 3.4.3.38): `e:\asix2k\ax88772.inf`, "Completing the Found New Hardware Wizard", no signature or file prompt; Network adapters, "This device is working properly." The unplug brought Windows' own "Unsafe Removal of Device" box for the adapter, which an unplug without a stop is |
| 6 The Advanced tab, the Power tab, `XHCISNAP` (pkg-v, after the second Update Driver, the ASIX adapter plugged) | The Advanced tab is the bandwidth page, its text as c17's, one row `System reserved`, `10 %`. The Power tab: "The hub is self powered.", "Total power available:  500 mA per port.", `Unknown USB Device`, `Unknown` (the adapter before its driver), and `7 port(s) available.`, `0 mA`. `XHCISNAP -verbosity 2`: `0002 was 0 -> 2 InfSection = Xhci.Dev`; after the restart `-o C:\SNAP`: `verbosity read 2, APPLIED 2`, `note ring 885 of 16384 bytes`, `coherence no tearing`, `errorlevel` 0, `SNAP.BIN` 104,740, `SNAP.PSC` 32, `SNAP.TXT` 2,287 bytes |
| 7 Disable, enable, remove, rescan (pkg-v, Verifier on) | Disable: "Disabling this device will cause it to stop functioning. Do you really want to disable it?" (Yes); the root hub gone, `teardown: ports unpowered=00000008`, `ports that would not give up power=00000000`. Enable, no prompt: both back. Uninstall: "Confirm Device Removal" (OK), the category gone, no restart prompt. "Scan for hardware changes": the Found New Hardware Wizard, given `E:\`, found `c:\winnt\inf\oem0.inf`, no file or signature prompt, no restart request; both "working properly", the driver key filled (Provider Yeo Kheng Meng, 1.99.0.0). A mouse moved the pointer afterwards |
| 8 The SMP guest of Phase 2d (pkg-v2, Verifier on) | Below. Installed over the miniport, mouse, storage, disable and enable, under TCG with two virtual CPUs; `xhci98.sys, loads: 2, unloads: 1`, clean shutdown |

No bugcheck in any boot, and every shutdown was clean ("It is now safe to turn off your computer."). Over every trace of the leg, `Ex refused the arm` 0 and `not served` 0; every `refused` line is a counter at `00000000`, except the 29 `select endpoints refused` and `selects failed` counter lines of row 4 on pkg-v.

**Row 4 on pkg-v, and its rerun.** From the first play, the trace (`vm\t26-win2k-v2-debugcon.boot3.log`) repeats:

```
hcd: select interface, URB header short, header/length=00000024
hcd: select interface failed, USBD status=80000800
thread URB done, function/status=01000800
```

interleaved with `hcd: EP0 recovered, slot/state=00000102`; over the leg, 56 `select interface failed`, some 79 `EP0 recovered`, and `select endpoints refused=00000038`, `selects failed=00000038` by the second play. A subagent's read, static, of the select path against that trace: an EP0 stall storm - the device refused the SET_INTERFACE to alternate 1 at once, and each time the Configure Endpoint before it had succeeded. The same 0x24-byte select had given `interface selected, number/alt=00000101` in c17 on the same OS without Verifier. The rerun on pkg-v2, on the same overlay with Verifier on (Update Driver with Have Disk `E:\`, no prompt; `fc /b` of the installed file against the package's, "no differences"; then a restart so the new file loaded at boot), read `interface selected, number/alt=00000101`, `endpoint mask=00000004`, two `isoch URB` and 32 `isoch published`, with 0 `select interface failed` and 4 `EP0 recovered` (two at the attach) over the whole log (`vm\t26-win2k-v2a4b-debugcon.log`). pkg-v2 changes nothing on this path. **The coordinator's ruling: the pkg-v failure is recorded as a transient EP0 stall storm, a host or passthrough state, not a reading of the HCD.** No play with Verifier off was taken, so that Verifier was a factor is not excluded.

**The empty controller driver key (an install-notes item).** After row 1's install the controller's software key, `Class\{36FC9E60-C465-11CF-8056-444553540000}\0002`, had **no values at all**, while the devnode's `Driver` value named it and the root hub's `0001` was complete. Hence the Driver tab's Provider Unknown and Version Not available, its file list of `xhci98.sys` alone, no Advanced tab, and `XHCISNAP -verbosity 2`'s "No xhci98 driver key found ... That means this driver's INF has never run on this machine". `setupapi.log` has no `#E` line; it records "Changed class GUID of device to {36FC9E60-C465-11CF-8056-444553540000}." and "Device install finished successfully", and its last entry a further "Selected driver installs from section Xhci.Dev in c:\winnt\inf\oem0.inf. Changed class GUID of device to {36FC9E60-...}. Set selected driver.". A second Update Driver (`E:\`, "Windows found a driver that is a closer match ... c:\winnt\inf\oem0.inf") filled the key with no prompt and no restart: Provider Yeo Kheng Meng, Version 1.99.0.0, files `usbd.sys` 5.00.2195.6658, `xhci98.sys` and `usbui.dll`, and the Advanced tab. Row 7's reinstall after a rescan filled it the first time. The INF has no `DelReg`; a subagent's read is that the first Update Driver, which followed a cancelled Found New Hardware Wizard, ran on a stale device-information set that recreated the software key on the class change (Other devices to USB). Not reproduced; the install notes are to say that a controller with no Advanced tab after its first install takes a second Update Driver.

**The SMP guest.** A base from `vm\win2k-smp.img @ post-phase20-2d-2026-09-06`, Phase 2d's (`-smp 2`, `-cpu pentium3`, ACPI, with an EHCI beside `qemu-xhci`), whose miniport (built 2026-09-06) Verifier already watched. **Under WHPX the guest hung at "Starting up..."** for 6 minutes with the second CPU halted in the BIOS (`EIP=000fd0b1`) and an empty trace, before any of the driver ran - the accelerator's, not the HCD's; the leg ran under TCG (MTTCG, two threads). Update Driver with "Display a list" and Have Disk `E:\` replaced the miniport with pkg-v, then pkg-v2 the same way (its `fc /b` against the package, no differences, and a restart), each with no prompt and no restart request; the first try, by search, reinstalled the old miniport and asked for its disk, and was cancelled. On pkg-v2: "This device is working properly."; the mouse moved the pointer; the stick's `fc /b` "no differences encountered"; disable ("Do you really want to disable it?", `teardown: ports unpowered=00000008`) and enable, no prompt, both "working properly", and a mouse moved the pointer again; `verifier /query` `Level: 0000001B`, `AllocationsFailed` 0, "xhci98.sys, loads: 2, unloads: 1"; a clean shutdown.

**Another agent's mouse.** For some 9 minutes during rows 1 and 2 another agent had attached a `usb-mouse` (`id=keepalive`) on this guest's monitor. The traces of those two boots have no `port status change`, `device enumerated` or `idVendor` line and the view by connection showed nothing beneath the root hub, and neither row depends on a device; not redone.

Notes: `out\phase26\v0\v2-2k-notes.md` and its screenshots (`shots\v2-2k-*`).

## The Windows 98 door sequence

The checkpoint's Windows 98 door sequence - Device Manager disable, enable,
remove and rescan with a USB mouse attached, as task 19.3 defined it - was
read on 2026-10-04 on the checkpoint build `a7ddbfa` (`pkg-v2`, `xhci98.sys`
SHA-256 `685a4155...1ee7`; on disk `134,288 10-04-26 1:22a` on both guests),
on Windows 98 SE under NUSB 3.3 (an overlay of the golden image) and under
SweetLow's stack (a fresh overlay of `sweetlow-2a.img @
sweetlow-stack-nodriver`, the controller installed from clean first: the CD
asked for `usbd.sys`, then a restart). A subagent drove both guests; notes in
`out\phase26\v0\door-98-notes.md`.

| Step | NUSB 3.3 | SweetLow |
|---|---|---|
| Mouse on port 1 | moves the pointer | moves the pointer (its first HID install asked for the CD for `hidclass.sys`) |
| Disable | "This device is disabled  (Code 22.)"; trace `stop controller`, `teardown: ports unpowered=00000008`, `quiesce: halted, USBSTS=00000001`, every resource released | the same |
| Enable | controller and root hub "This device is working properly."; `counters start=00000002`, the No Op self-test passes, the mouse re-addressed and moving | the same |
| Remove | "Confirm Device Removal" > OK; the USB and HID classes leave Device Manager; `root hub detaching, dropping every device`; no hang | the same |
| Rescan (Refresh) | the controller reinstalls from Windows' stored copy of `xhci98.inf` with no prompt, the root hub silently, the mouse through `HIDDEV.INF` (the CD for `hidclass.sys`); no restart prompt; the mouse moves, and an unplug and replug works | the same, with no prompt for the mouse |
| Shut down | clean | clean |

On Disable, Windows 98 keeps the root hub listed with a red X ("This device
is not working properly because a device it depends on, xHCI98 USB 3.x
eXtensible Host Controller, has been dynamically disabled."), as it does the
HID devices beneath it; the trace shows the root hub's and the device's stop
and remove before the controller stops, so the listing is Windows 98's own.
No refused, failed, fatal or given-up counter moved in either trace
(`vm\t26-win98-door-final-debugcon.log`, `vm\t26-win98-doors-boot2-debugcon.log`).

## 26-V.3 - the stock reading

A Windows 98 SE guest with no USB 2.0 stack, on 2026-10-04 (development host A, `qemu` flavour of `40efd31`, `xhci98.sys` SHA-256 `95abc0aa...78b4`, the package drive checked against it).

**The stock base.** By the owner's decision of 2026-10-04 the base was made by uninstalling NUSB 3.3 from the NUSB base rather than installing Windows 98 SE afresh from the ISO. On a new overlay of `vm\win98.img @ post-nusb`, booted with no `qemu-xhci`:
- **NUSB's own uninstaller**, the one Add/Remove Programs entry, "Remove Unofficial Universal USB 2.0 Stack" (`_USB2UN.INF`, UNINSTALL), ran silently and removed `USBEHCI.SYS`, `USBHUB20.SYS`, `USBPORT.SYS`, `INF\USB2.INF` and its own Uninstall key.
- **Deleted by hand**, as the uninstaller leaves them: NUSB's mass-storage half, `USBSTOR.SYS`, `USBNTMAP.SYS`, `USBAUTH.SYS`, `USBU2A.SYS`, `INF\USBSTOR.INF`, `INF\USBNTMAP.INF` and `SYSTEM\IOSUBSYS\USBMPHLP.PDR`; their copies and the stack's under `OPTIONS\CABS`; `INF\usb2.PNF`; and `INF\DRVIDX.BIN` and `DRVDATA.BIN`, the driver index, which Windows rebuilds from `INF\`. In the registry, a phantom ICH4 EHCI devnode from the image's past (`Class\USB\0000` and `0001`, `Enum\PCI\VEN_8086&DEV_24CD...`, `Enum\USB\ROOT_HUB20`), removed with a REGEDIT4 file.
- **Checked**: `SYSTEM32\DRIVERS\usb*.*` is `USBAUDIO.SYS` alone; `INF\usb*.*` is `USB.INF`, `USBCDC.INF` and `USBWAVE.INF`, stock 98 SE; nothing under `OPTIONS\CABS` or `SYSTEM\IOSUBSYS`; a fresh registry export has no `usbehci` or `usbstor` string. After a restart, no New Hardware wizard and no "Universal Serial Bus controllers" category in Device Manager.
- **Left on purpose**: NUSB's copies of Microsoft's 98 SE fixes that are not the USB 2.0 stack - the 1394 and SBP-2 files, `NTMAP.SYS` and `NTMAP.INF`, `NTMAPHLP.PDR`, `DISKTSD.VXD`, `CDVSD.VXD`, `IOS.VXD`, `HOTPLUG.DLL`, `SYSTRAY.EXE`, `USER.EXE`, `USER32.DLL`, `EXPLORER.EXE`, `QFECHECK` and NUSB's `NODRIVER.INF` - and inert registry value data naming the old devnode and `usbport`. `NTMAP.SYS` without `USBNTMAP`, `USBSTOR` and `USBMPHLP` gives no USB storage path.

The base was then made read-only (`vm\t26\win98-stock-base.qcow2`), and the leg ran on an overlay of it with `qemu-xhci` (eight USB 2.0 ports, no SuperSpeed).

| Row | Reading |
|---|---|
| Controller | Add New Hardware Wizard for "PCI Universal Serial Bus", "Specify a location" `D:\`, found `D:\XHCI98.INF`. **Prompts**: "Please insert the disk labeled 'Windows 98 Second Edition CD-ROM', and then click OK."; "The file 'usbd.sys' on Windows 98 Second Edition CD-ROM cannot be found." (given `E:\WIN98`); and "To finish setting up your new hardware, you must restart your computer." / "Do you want to restart your computer now?" (Yes). After the restart the controller and `xHCI98 USB 3.x Root Hub` were "This device is working properly.", with the Advanced and Power tabs present |
| HID mouse | The `HIDDEV.INF` wizard, with the CD prompt and "The file 'hidclass.sys' on Windows 98 Second Edition CD-ROM cannot be found." (given `E:\WIN98`), no restart. "HID-compliant mouse" and "USB Human Interface Device" working properly; `mouse_move` moved the pointer through it |
| Composite audio | 0D8C:0014 split as on the primaries (`split device configured, port/slot/value=00030101`). The HID function from `HIDDEV.INF` and the audio function from `WDMA_USB.INF`, neither with a prompt, no restart. "USB Audio Device" (Generic USB Audio), "HID-compliant consumer control device" and "USB Human Interface Device" each working properly: **both functions bound**. One play of "The Microsoft Sound.wav" (7.85 s) ran to its end and closed by itself, some 55 s for 7.5 s of position (about 0.14x), with no fault and no error dialog: 2 `isoch URB`, 32 `isoch published`, no refused, `not served` or watchdog line |
| Storage | "USB Device": "Windows was unable to locate a driver for this device.", then under Other devices, "The drivers for this device are not installed. (Code 28.)". The trace: `hcd: device descriptor, idVendor/idProduct=46F40001`, `devices addressed=00000002` (was 1), no `endpoints opened` line and no refusal counter moved - **NODRIVER** as design record 06 defines it |
| Unplug | The mouse, the stick and the audio device, each alone: no hang and no dialog, and `info usb` empty after each |

Shutdown was clean. The stick was c13's; storage ran before audio, while the C-Media device was in use by another leg.

Notes: `out\phase26\v0\v3-98-notes.md` and its screenshots.
