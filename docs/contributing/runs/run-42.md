# Run 42 - The Windowed Installer, the Uninstall, the 98 SE and ME Device Install

Phase 42 of `roadmap-usb4.md`, for release `3.1.0.0`, opened by the owner
on 2026-10-18. This sheet records what each task did and every reading;
the roadmap's task list and decisions table are the current state.

## 42-0.1 and 42-0.2: design records 26 and 27 (2026-10-18)

Written on branch `3.1.0.0` from the owner's decisions of 2026-10-18 (the
decisions table, "Phase 42, the open choices"), before any code:

- Design record 26, `docs/contributing/design/26-windowed-installer.md`:
  the wizard over one engine shared with the console tool, its pages, the
  components (the bundled Stor98 only when ticked, with the record the
  uninstall reads), the settings written to the machine-wide key of task
  42-A.17, the 98 SE and ME steps page, the Add/Remove Programs entry and
  the uninstall's relaunch from a temporary copy, the WOW64 self-start.
- Design record 27, `docs/contributing/design/27-win9x-device-install.md`:
  the 16-bit helper over the setup engine's documented device installer,
  what counts as documented (the DDK header's own documentation blocks and
  the DDK's samples; the 98 DDK's help file carries no page for these
  routines, and the Windows 95 DDK is not at hand), the call sequence, the
  devices in order with Windows' own wizards waited for, the removal, the
  toolchain and the cut's prerequisite.

What was read for them, on development host A: the Windows 98 SE DDK's
`inc16\setupx.h` and `lib\win98\setupx.lib` (`tools\win98ddk-extracted\`,
their sizes and SHA-256s now in `docs/references/README.md`), the DDK's
`HELP_WIN98DDK.CHM` (extracted with 7-Zip and searched: no device-installer
page), the DDK's `net\ipnew` and `plugplay\custom` samples, and the Open
Watcom 2.0 install at `C:\WATCOM` (its 16-bit compiler, `wlink`, `wrc`,
`h\win\windows.h`, `lib286\win\windows.lib`). Both records were
reviewed and revised until the review converged, the same day (record
26 at draft 7, record 27 at draft 6); their status lines record what
each revision changed.

Owed to the code tasks: record 23's revision (record 26 section 12), the
section 2a provenance entry for `XHCIDI.EXE` with its first build (record
27 section 3.4), and every reading of record 27 section 12 on the 42-V.1
legs.

## 42-0.4 to 42-0.7: the connection-manager designs (2026-10-18)

Written on branch `3.1.0.0` the same day, after records 26 and 27, from the
owner's decision of 2026-10-18 that the safe-removal mark's writer is a
privileged NT service (the decisions table, "Phase 42, the open choices")
and from the task texts; each as a revision of the record that owns the
mechanism, except the one that needed a record of its own:

- 42-0.4, design record 21 section 7.4, revision 3: the mark kept across a
  stack rebuild - a device-specific `LowerFilters` value on the unit,
  written by the service `xhcimark` (`XHCIMARK.EXE`, `XHCIMK64.EXE`) from
  what the driver publishes through a fourth query-device request
  (`IOCTL_XHCICM_MARKS`), validated against the devnode tree; the child
  filter also created by `AddDevice`, never failing it; the first plug's
  documented, veto-respecting restart; the value removed at departure and
  at the service's reconciliation; the installer's install, update, stop
  and removal with the marks removed before the filter hosts and `xhcicm`'s
  service and file kept while a value may remain.
- 42-0.5, design record 22 section 7.7, revised: the recovery of a
  contained USB4 host interface in place - the precondition on reads
  (the header as the healthy start read it, the two Command bits
  restored, a changed BAR0 refused, the host interface's version and the
  selection's tests again, the size register programmed or reset), the
  kept buffer reused only after a verified stop of the rings in it, the
  port records recovered as section 7.6's resume obligations through
  section 9.2's discover-and-preserve with no reset, one RESYNC "controller
  recovered"; decision U17 revised by the owner's promotion of the work.
- 42-0.6, design record 28, `28-win9x-no-interrupt.md`: an NVMe function
  behind a tunnel whose interrupt pin `PCI.VXD` cannot route started
  through a second basic logical configuration without the IRQ descriptor
  (the configuration manager's log-conf services from the 98 worker, then
  `Reprocess_DevNode`), the forced form as the leg's fallback, the pin-0
  function needing nothing, the pairing with Stor98's polling mode,
  `XhciCmNoIrqStart` and `XhciCmNoIrqDiag`.
- 42-0.7, design record 21 section 7.2, "The 9x half": the tools' 9x eject
  asked of the connection manager through `XhciCmEjectAsk` and
  `XhciCmEjectResult` and run by the worker with Stor98's handshake, the
  USB4 per-port teardown after a clean removal and the isolation before a
  removal after an answer that is not clean; the 9x statement through
  `PCI.VXD`'s dword write of the SHPC bridge's DWORD Select on the worker,
  posted by the thread with the 2 s bound, 98 SE and ME admitted one at a
  time on their G3 readings; the per-member owed removal on the thread and
  the per-member state machine on the worker in place of `CmNvlinkAwait`
  and the worker's holds, with interface 3's R6f handoff word.

What was read for them: the Windows 98 SE DDK's `CONFIGMG.H`, `CFGMGR32.H`
and `PCI.H` (`tools\win98ddk-extracted\incs\`) and its help file
(`HELP_WIN98DDK.CHM`, extracted into a scratch directory: no Configuration
Manager page among its 1,548), the Windows 2000 DDK's `SETUPAPI.H` and
`cfgmgr32.h`, `src\cm\stor98cm.h` (interface 3's R6 to R12), Stor98's
`README.md` and `docs/PLAN.md` at its `audit-2026-10-18` head, and the
connection manager's sources (`cm_cfgwk.c`, `cm_nvlinkos.c`, `cm_reenum.c`,
`cm_filt.c`, `cm_nhi.c`, `cm_live.h`, `tbt_u4cm.h`, `tbt_brnum.c`,
`cm_statos.c`, `cm_query.h`). Records 24 (revision 6), 25 and 26 gained
the pointers.

The two record 21 designs were reviewed and revised the same day until
the review converged: 42-0.4 over nine rounds (the first draft's kernel
registry lookup and location-based roles withdrawn; the service's
binary moved to `system32`; a restart latch tried and withdrawn in favour
of the eject posted on a positive confirmation; Windows 2000's problem
24 refused as a confirmation), 42-0.7 over six rounds (the transactions
moved from the worker to the thread with one call worker per member; the
detached block and the persistent member blocker; acceptance as the
ownership boundary of an ask; O6 under the firmware connection manager
left as an open interface exception). 42-0.5 converged over six rounds
(the first draft's recovery transaction restructured: the recovery
restores and commits, every rebuild and publication afterwards going
through the ordinary machinery behind a per-member retirement barrier;
an immutable snapshot per containment; nothing built or published
before a guarded commit; decisions U18 to U29). 42-0.6, design record
28, converged over eleven rounds at draft 11 (the candidate's ownership
by Stor98's binding and version; a journal of the configurations this
driver created, never recognised by priority and never freed; the work
as a per-member transaction on the thread with the member's call worker,
its own admission window independent of safe eject; the descendant walk
below the member; every hypothesis about `PCI.VXD` marked as one;
decisions N1 to N10). With it every design of Phase 42 has converged
before its code, as the checkpoint asks.

## 42-A.17 and 42-A.18: the machine-wide registry key, and value 4's codes (2026-10-18 and 19)

Written on branch `p42-reg` from `3.1.0.0` at `2a89ba74` (`7f25ca3c`, the
implementation; `2a3e7e9c`, `33e98bc3` and `2b9a2ae2`, its revisions),
reviewed and revised until the review converged. The owner's decisions
are the decisions table's two
rows of 2026-10-18.

- 42-A.17: every `Xhci*` value of `xhci98.sys` (`hcd_ctl.c`
  `hcdReadDword`, which every read goes through) and every `XhciCm*`
  value of `xhcicm.sys` (`cm_reg.c` `cmQuery`) is read first from
  `HKLM\System\CurrentControlSet\Services\USB` and, when it is not there
  in any type, from the driver key; the decision is the new pure
  `xhci_regsrc.c` (host suite `test_regsrc`), compiled into both drivers.
  "There" is the query routine having been called with a value (a
  zero-length `REG_NONE` excepted), not a status code. A `reg.machine`
  log note counts the start's values taken machine-wide. The INFs are
  unchanged. The six helpers write the machine-wide value, create the key,
  leave the per-controller values alone and show both; XHCITR's levels
  block and `/report` show both and which won, and its Set writes the
  machine-wide value where one is in force. `INSTALL /set` and
  `INSTALL.BAT`'s questions still write the controllers' own keys; the
  installer's machine-wide write is the installer task's (design record 26
  section 5.4).
- 42-A.18: `XhciFastPollFsLs` 4 programs Interval = `bInterval` - 128 for
  an interrupt endpoint's code 128 to 143, at every speed, on a root port
  or behind a hub, replacing task 36.19's codes with no compatibility; a
  code whose Interval is Table 6-12's own is left unmarked; the fallback
  stays Interval 3; the interval caps still run after it (design record 13
  section 13.7).

Built after the review converged: `scripts\build-driver.cmd all` and
`all -amd64` at `2b9a2ae2`, every gate passing, the host suites (with
`test_regsrc`, `test_fast_codes`, `test_cap_codes` and the XHCITR level
vectors) passing.

### The legs (development host A, QEMU, the `debug` flavour at `2b9a2ae2`)

Each guest had two `qemu-xhci` controllers, a `usb-tablet` on controller
1's root port 2 (High Speed), a `usb-mouse` behind a `usb-hub` on its port
1 (Full Speed behind a hub) and a `usb-mouse` on controller 2's root port
1. The values were set with a leg aid that writes one DWORD machine-wide
or on one controller's driver key and lists both (not shipped), and with
the package's own helpers; each reading is the `XHCITR /report` of the
next start, searched for `fastpoll.*`, `reg.machine`, `silent.gate` and
the levels block, and screenshotted. The `qemu` flavour could not be
used: `XHCIINST` refuses a `qemu` build in the `debug-x86` directory, as
it should. Installed with `INSTALL /debug /yes` from a package made of
`3.0.0.0`'s `INSTALL.BAT` and `XHCIINST`, this branch's helpers and
XHCITR, and the staged `debug` x86 drivers.

| Target | Reading | Result |
|---|---|---|
| 98 SE | Absent: no machine-wide value | PASS: `fastpoll.value 0`, `reg.machine 0`; the levels block "Machine-wide ... not set". One controller recorded nothing: its own `XhciLogVerbosity` was the `0` an older INF had written |
| 98 SE | Machine-wide `XhciFastPollFsLs 3` and `XhciLogVerbosity 3`; controller 2's own `XhciFastPollFsLs 2`, controller 1's own `0` | PASS: both controllers `fastpoll.value 3`, `reg.machine 2`; both now log, the machine-wide level overriding the `0`; XHCITR "3 - adds port status (machine-wide)" for each and "its own value, 0 / 2, is not in force" |
| 98 SE | `silent-on.bat`, `portsw-on.bat` | PASS: each wrote the machine-wide value, read it back and showed both controllers' own `0` as "NOT IN FORCE"; `portsw.vbs show` before it read the missing value as "no machine-wide value is set" (WSH's not-found error matched on 98 SE). At the next start both controllers `silent.gate.value 1`, `silent.gate 1` |
| 98 SE | Machine-wide `XhciFastPollFsLs` deleted | PASS: controller 1 `fastpoll.value 0`, controller 2 `2`, each its own; `reg.machine 2` (the log value and the silent switch) |
| 2000 SP4 | Absent | PASS: `fastpoll.value 0`, `reg.machine 0`, "not set"; `Services\USB` did not exist (`regedit /e` of it wrote no file) |
| 2000 SP4 | `portsw-on.bat` | PASS: the key created, holding `XhciIntelPortSwitch` dword 1 (`regedit /e`); both controllers' own `0` shown "NOT IN FORCE" |
| 2000 SP4 | Machine-wide `3` over controller 2's own `2` | PASS: both `fastpoll.value 3`, `reg.machine 2`, XHCITR's "not in force" lines for `0` and `2` |
| 2000 SP4 | Machine-wide deleted | PASS: `0` and `2`, `reg.machine 1` |
| 7 SP1 x86 | Absent; `portsw-on.bat`; machine-wide `3` over controller 2's own `2`; deleted | PASS: `0`/`0` and `reg.machine 0`; the helper wrote and showed as on 2000 (`reg query` of the key: `XhciIntelPortSwitch 0x1`); `3`/`3` and `reg.machine 1`; `0`/`2` and `reg.machine 0`. Whether the key existed before the helper ran on 7 was not read |

42-A.18 on 98 SE, hidusbf (the 98/ME `hidusbf.sys` of 2026-10-03)
on every QEMU HID device as design record 13 section 13.6 step 1 puts it
by hand, `XhciFastPollFsLs` 4 machine-wide; the programmed Interval is the
`fastpoll.open` record's last byte (slot, DCI, Table 6-12's Interval, the
one programmed):

| Code | Tablet, High Speed, root port | Mouse, Full Speed, behind the hub | Mouse, controller 2 root port | Result |
|---|---|---|---|---|
| 135 | `02030F07` | `03030A07` | `01030F07` | PASS: Interval 7 (16 ms) everywhere; Full Speed behind a hub applied, as task 42-A.18 asks |
| 128 | `02030F00` | `03030A00` | `01030F00` | PASS: Interval 0 (125 us); no `fastpoll.fallback`; the tablet's pointer followed |
| 143 | no record | `03030A0F` | no record | PASS: at High Speed 143 is Table 6-12's own clamp, left unmarked; Full Speed Interval 15 |
| 144 | no record | no record | no record | PASS: not a code |

What these legs do not show: QEMU accepts any Interval (design record 13
section 13.4), so no refusal and no fallback was exercised, and nothing
says how a real controller or a transaction translator schedules
Interval 0 to 2 or 11 to 15 below High Speed; the 7 guest's hidusbf
reading of 36.19 was not repeated; the A.17 legs read `XhciFastPollFsLs`,
`XhciLogVerbosity` and `XhciSilentInstall` as the driver takes them, and
the port switch only as the helpers write and show it (QEMU's controller
is not Intel, so the driver never reads that value); no `XhciCm*` value
was read on a guest with a Thunderbolt or USB4 controller; XHCITR's Set
under a machine-wide value was not run; `silent-off.bat` and
`portsw-off.bat` were not run. Owed to 42-V.2 as its text says, with
these taken.
