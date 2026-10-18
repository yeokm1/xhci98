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

## 42-A.5: the safe-removal mark across a stack rebuild (2026-10-19)

The code of 42-0.4 (design record 21 section 7.4, revision 3), on branch
`p42-ejrb` from `3.1.0.0` at `2a89ba74`: the driver half in `src\cm\`
(the roles by id tables, the `AddDevice` child, the attachment list and the
per-domain marks table `cm_marks.c`, `IOCTL_XHCICM_MARKS`, the departures
held pending and the eject posted only on `XhciCmEjectConfirm`), and the
mark service `xhcimark\` (`XHCIMARK.EXE`, `XHCIMK64.EXE`) with its pure
half, self-test and import check, staged by `make-release.ps1` into
`tools\xhcimark\`. The installer's hooks for the service (its install,
start, update and removal, and the marks removed at the uninstall and
after a failed upgrade) are task 42-A.3's code, not this branch's; record
21 section 7.4's "The installer" paragraph is their specification.

Reviewed and revised until the review converged. Among the changes the
review brought: the reconciliation judges a record by its
owning domain and never reads a failed lookup or an unreadable location as
an absence; the restart's fresh reads re-check the NHI, both answers, the
member and the unit's actual parent; the marks table keeps each member's
sets apart; the confirmation's writers share the tools' eject mutex.

### The legs (42-V.2, the 42-A.5 part)

In QEMU guests on development host A, the emulator and recipe of
"Bridges behind the tunnel port on NT" (`runs/run-41.md`): the
`tb3-alpine-ridge` host with the SHPC tunnel bridge `tb0` and a chained
upstream port (`1B21:2421`) and bridge behind it, an NVMe at 01:01.0 and
another behind the chain. Each guest took the published `3.0.0.0`
package's `INSTALL /yes /thunderbolt /notray /nopause`, then this
branch's qemu-flavour `xhcicm.sys` copied over the installed one and the
service registered by hand (`XHCIMARK -register` from `system32`),
because the installer's hooks come with 42-A.3. Disables, enables and the
driver update were made by a test helper of the guest's own
(`SetupDiCallClassInstaller(DIF_PROPERTYCHANGE)`,
`UpdateDriverForPlugAndPlayDevices` with `machine.inf`, forced); "listed"
is `XHCIINST /devices` printing "removable: yes ... listed in Safely Remove
Hardware" for the unit.

- **Windows 2000 SP4** (one processor, no ACPI). The first build of the
  driver bugchecked at the next boot (0x7F, a double fault, read with the
  emulator's debugger: the kernel stack overflowed): a filter's capability
  handler asked `IoGetDeviceProperty` for its own address, which the PnP
  manager answers with an `IRP_MN_QUERY_CAPABILITIES` down the same stack.
  Fixed in the same session (the capability answer's own address; no
  location read while the object is itself asking). With the fix: the
  first plug of a never-seen unit listed, the upstream port and the NVMe
  marked within a second of the plug (`3.0.0.0` left this unit unlisted,
  the recipe's step 4); a disable (the mark kept while disabled, problem
  22) and an enable, listed; a forced driver update, listed; an unplug,
  both marks removed within a poll (2000's Unsafe Removal dialog as
  before); the service stopped, the unit unplugged, the service started:
  both marks removed at its start. Pass.
- **Windows XP SP3** (ACPI, two processors). The first plug of a
  never-seen unit: the marks written within a second; Windows' driver
  installation of the upstream port then deleted the port's own
  `LowerFilters` value, the service wrote it again, held its restart while
  the NVMe's Found New Hardware wizard was open ("an installation is
  pending"), and once it was cancelled restarted the port
  (`DIF_PROPERTYCHANGE`), which came back listed. A disable and an enable,
  listed. A forced driver update deleted the value again; the first build
  of the service had spent its one restart on the first plug and left the
  unit unlisted, so a unit seen listed after its restart now re-arms it
  (record 21 section 7.4, "As built"); read again from a fresh install: the
  update's install deleted the value, the service wrote it, restarted the
  port, listed. An unplug, both marks removed within a poll; the
  stopped-service departure caught up at the service's start. Pass.
- **Windows 7 SP1, 32-bit** (two processors, from an elevated prompt).
  The first plug of a never-seen unit listed (7 kept the first plug in
  `3.0.0.0` too); the driver installation deleted the upstream port's new
  value within two seconds and the service wrote it again, so the later
  rebuilds kept it. A disable (the mark kept while disabled) and an enable,
  listed; a forced driver update (the value deleted by the install and
  written again), listed; an unplug, both marks removed within a poll; a
  departure while the service was stopped caught up at its start. Pass.
- **Windows XP Professional x64 SP2** (the amd64 build; XP x64 enforces
  no kernel-mode signature, so this branch's unsigned build loads). The
  same readings as 32-bit XP: the first plug's marks written, the port's
  value deleted by its install and written again, the gated restart held
  while a wizard was open and then made, listed; a disable and an enable,
  listed; a forced driver update, the value deleted and written again, the
  re-armed restart made, listed; an unplug, both marks removed within a
  poll; a departure while the service was stopped caught up at its start.
  Pass.

**Owed** (42-V.2's 42-A.5 part, written down here rather than read):

- **Vista (both architectures) and 7 x64**: not read in this run. The x64
  binaries of a cut are test-signed by the packager only, with the owner's
  key and password, which this run does not have, so an unsigned
  `xhcicm.sys` loads on those systems only from an F8 boot with signature
  enforcement disabled; Vista x86 needs no signature and was not reached
  for time.
- **The value gone after the uninstall**, and every leg of the installer's
  part (its install, update, stop and removal of the service, step 0, the
  failed upgrade, the hand-deleted service's Code 39 repaired): they need
  42-A.3's hooks, which are not on this branch.
- The legs of record 21 section 7.4's list that need more than this run
  did: the restart vetoed by an open file (no volume behind the chain: the
  emulator's NVMe has no driver on these guests), an eject and its
  confirmation (an applet stop on XP and later confirmed by the service, on
  2000 by `XHCIINST /eject`'s own write, which is 42-A.3's code), the USB4
  profile's legs (a driver update and a disable held under the USB4
  connection manager with the tunnel kept, a delayed confirmation across a
  connection change), more than 32 departures in a minute, the crash
  between the write and the record, Driver Verifier on the 2000 SMP guest
  with the `AddDevice` child, `XhciCmSafeEject` 0 with a value in place,
  a restart of Windows with the unit present, and the dock's upstream port
  as a unit whose id the filter INF names. Every guest here ran the
  firmware connection manager's emulated Alpine Ridge host.
