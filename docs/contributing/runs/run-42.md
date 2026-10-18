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

## 42-A.14 and 42-A.15: PCI below a tunnel port on 9x; a 9x function with no interrupt (2026-10-19)

Worker p42-9x, branch `p42-9x` from `3.1.0.0` at `2a89ba74`; code
`f747fce3`, reviewed and revised through the branch's last commit.
Emulator legs in QEMU (the `usb4-emu` build `bin-2a893ea`), guest 98 SE
from `win98-gold` on i440FX, TCG, the Thunderbolt 3 layout of the
PCI-page legs (the tunnel bridge, the Alpine Ridge host, the ASM2464
enclosure), with the tunnel bridge at 00:1Ch, where SeaBIOS's routing
table has no entry, so a function added below it after POST gets no route
from `PCI.VXD`. xHCI98 and `xhcicm.sys` installed by hand from the
package (the 3.0.0.0 console installer refused a `qemu` build in
`release-x86`, as it should); Stor98 0.4.0.0 on the enclosure's NVMe; the
installer record `HKLM\Software\xhci98\Stor98` `Version` = `0.4.0.0`
imported by hand, since this branch's installer does not write it (task
42-A.3, branch `p42-wiz`); `XhciCmLogVerbosity` 2.

- **The interim (owner's coordinator, 2026-10-19).** Design record 28
  places each call on the member's call worker of task 42-A.12, which no
  branch has. The pure half, `cm_noirq.c`, is a state machine of
  operations that the existing 98 worker drives at its 200 ms tick until
  42-A.12 lands; record 28's dated note says what that costs (a blocked
  CONFIGMG call holds the domain's other 9x work) and what changes then.
  **Owed to 42-A.12**: driving the same machine from the thread's pass
  through each member's R6f handoff word, the 50 ms cadence, and the
  liveness state read by the thread itself.
- **42-A.14, read on 98 SE.** The xHCI98 USB Tree's PCI page of the
  Thunderbolt controller lists the functions below the tunnel port with
  their configuration space, read by the worker through `PCI.VXD`'s
  guarded route: the enclosure's NVMe at 01:01.0 (1B36:0010, class
  010802, pin INTA, line 0, its 64-bit memory BAR, its PCI Express and
  power management capabilities; "35 configuration reads, 0 refused");
  with a bridge chain behind the port, the switch's upstream port at
  01:03.0, its downstream port 02:00.0 and the chain's NVMe 03:00.0, after
  `xhcicm.sys` numbered the chain (buses 02-03, the window
  FE900000-FE9FFFFF) - "102 configuration reads, 0 refused";
  `cm.query.pci.9x` 00010000 (walked, nothing held). ME not read.
- **42-A.15, read on 98 SE: the basic form does not apply.** The NVMe at
  01:01.0 (its pin swizzling off the port's pin, UNROUTABLE by the fresh
  classification) sat at Code 29, "This device is disabled because the
  BIOS for the device did not give it an IRQ", its Resources tab empty.
  The worker's walk, rules and gate ran: one basic configuration at
  `LCPRI_NORMAL` (`cm.reenum.9x.noirq.lc` 01003000), whose descriptors
  read one memory descriptor of 40 bytes and nothing else - no IRQ, no
  I/O, no DMA, and the every-type read met the one memory descriptor
  (`.rd` 01012800, 0100000F, 0400000F, 0200000F, 0300000F, 00012800,
  0000000F). The device was refused, EXCLUDED "configuration"
  (`XhciCmNoIrqDiag` word 4 00060203, word 6 0006000F), nothing added;
  the same after a re-plug. Record 28 section 3's hypothesis - an IRQ
  requirement in `PCI.VXD`'s configuration that cannot be met - is
  refuted for this case; record 28's note says so, and a redesign is owed
  before 42-V.6. Stor98 wrote no `StorInterrupt` (it never started). An
  earlier build of the branch (`f747fce3`) gave no diagnostic at all (a
  refusal wrote none, fixed in review); a later one (`d8c73375`) read the
  descriptors with `ResType_All` and refused at the same place, which
  `2d38fc77`'s commit message took for a failed `ResType_All` read; the
  leg above corrects that.
- **Not read**: the chain NVMe's start (the 98 SE "PCI standard
  PCI-to-PCI bridge" wizard for the hot-added upstream port stopped
  advancing, so the chain's NVMe was never enumerated); the routed case;
  ME; the pin-0 NVMe, which the emulator cannot present (QEMU's NVMe
  hardcodes INTA; a property for it would be a request to `usb4-emu`);
  the file write and compare (nothing started).
- **Owed on real hardware**: 42-A.14 on the owner's P14s Gen 1 under 98
  SE with the ORICO enclosure, the PCI page below the port; nothing at
  hand presents 42-A.15's case (record 28 section 8.2).
