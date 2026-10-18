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

## 42-A.4 and 42-A.16: XHCISNAP retired into XHCITR; the Safely Remove stop investigated (2026-10-19)

Branch `p42-tr` from `2a89ba74`: `99c5fc3a` (the code), `955525c1`,
`3ed6761d` and `ccd9e028` (Codex rounds 1 to 3), `afc2b317` (the gated
build's fix: `hcd_pnp.c` and `hcd_rh.c` include `xhci_hw.h`). Codex
reviewed it over five rounds: 2 P1, 3 P2 and 1 P3, then 4 P2, then 1 P2,
then nothing, then nothing on the build fix.

- 42-A.4, as record 25 section 15 has it. Round 1's P1s were real: the
  configuration manager's `LOG_CONF` and `RES_DES` handles are pointer
  sized, which `XHCISNAP` (x86 alone) never met and `XHCITR64` does; and
  the report's level-4 withholding must read the dump's own header line,
  not a tail of the file. Read on the x64 build host after the fix:
  `XHCITR64 /snapshot` decodes allocated memory ranges and MSI interrupts.
  `test-package.ps1`: 1114 checks pass; host suites pass; `snaptest.cmd`
  33 cases pass.
- 42-A.16, read in QEMU on 98 SE with the released `3.0.0.0` (folder
  `E:\xhci98-vm\p42-tr\a16`): XHCITR's Safely Remove of a Bulk-Only stick
  stopped that stick alone, at SuperSpeed and at High Speed, on a root
  port and behind a hub; a second stick kept reading, the tablet kept
  moving, and the stick came back when plugged again. The confirmation
  listed the stick's own three devnodes. `XHCIINST /eject` of `3.0.0.0`
  offers only Thunderbolt units, so it could not be compared. Not
  reproduced; no cause found. The level-2 log held no record of the stop,
  so `xhci98.sys` now writes `pnp.pdo`, `pnp.rh` and `pnp.hc` for every
  stop, query-remove, remove, cancel and surprise removal its device PDOs,
  root hub PDO and controller receive: the owner's Save Report on the
  ThinkPad P14s Gen 1 under 98 SE says which devices the PnP manager
  stopped. Still to read there: a UAS stick, the internal devices on the
  same root ports, the stick model. The task stays open.
- The gated build at `afc2b317` (`build-driver.cmd all` and `all
  -amd64`, every gate): BUILD + GATES PASSED on both.
- Legs owed: the moved functions on 98 SE and 7 x86 (`/snapshot`,
  `/dump`, `/probe`, Save Snapshot, Save Report's Snapshot part), x64 with
  the test-signed package, the package without `XHCISNAP`, and 42-A.16's
  regression on 98 SE and one NT target with the new records. Guests are
  ready: `a16` (98 SE, QMP 48010) and `w7` (7 x86, QMP 48011).
