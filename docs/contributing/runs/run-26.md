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
