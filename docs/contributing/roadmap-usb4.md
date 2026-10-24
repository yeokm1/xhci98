# Development Roadmap - USB4 and Thunderbolt 3, the Connection Manager Driver

The fourth roadmap file, opened on 2026-10-14 at the project owner's
request, the day Phase 38 closed on the `2.4.0.0` cut. It carries the
connection-manager work: a driver beside `xhci98.sys` that is a client of
an Intel Thunderbolt controller's firmware connection manager first, and
a USB4 software connection manager after it, so that the PCIe devices
behind a Thunderbolt or USB4 tunnel - a dock's xHCI controller, an NVMe
enclosure - reach the targets this project serves. The three earlier files
are unchanged by it: [`roadmap.md`](roadmap.md) keeps the status, the
conventions and Phases 0-16, [`roadmap-miniport-updates.md`](roadmap-miniport-updates.md)
Phases 17-24, and [`roadmap-hcd.md`](roadmap-hcd.md) the host controller
driver's Phases 25 onward, which stay its own: a USB phase opened after
this file exists still goes there. Every convention of theirs holds here:
the batching convention and the task-id rules, "observed on both" as
`AGENTS.md` qualifies it, phases numbered when the owner opens them, a
phase's closing task named `<phase>.final`, and the rule that no phase
advances past an unobserved checkpoint.

**Status: Phase 43 is open, for `3.2.0.0`; Phase 42 is closed on the `3.1.0.0` cut of 2026-10-20; Phases 39, 40 and 41 are closed on the `3.0.0.0` cut of 2026-10-17.** Phase 43 was opened on 2026-10-20 by the owner from "Planned work", going through every planned item one by one; the decisions table maps the planned ids to its tasks. Phase 42 was opened on 2026-10-18 by the owner from "Planned work" (the windowed installer, the uninstall as NUSB does it, the Windows 98 SE and ME device install, `XHCISNAP` retired, the safe-removal mark across a stack rebuild, and the connection-manager follow-ups), on branch `3.1.0.0`, and widened the same day after the owner went through every planned item; the decisions table maps the planned ids to its tasks and records what was dropped or closed. The history of the first three: Phases 39 and 40 opened 2026-10-15 by the owner. The two
pieces of planned work drawn up on 2026-10-14 with the placeholder ids
`TB3` and `U4CM` are Phase 39 and Phase 40, in the order the `usb4-emu`
handoff gives (its section 2, maintainer 2026-10-14): Phase 39 the
Thunderbolt firmware-connection-manager client with tunnel re-enumeration
and safe eject, because it can be proven on the owner's ThinkPad P14s Gen
1; Phase 40 the USB4 software connection manager; real USB4 devices third,
planned task U4HW.1, once a USB4 host that boots these systems is at hand. The release is
`3.0.0.0` and carries both phases (owner, 2026-10-15, decision D7):
`39.final` closes Phase 39 with its legs and a dry build and cuts nothing;
`40.final` cuts `3.0.0.0`. Design record 21 is Phase 39's plan, its
decisions D1 to D13 taken (D4 on 2026-10-14, the rest on 2026-10-15);
design record 22 is Phase 40's.

## The sources this roadmap rests on

The test bed and the hardware contract are the `usb4-emu` repository's
(`D:\OneDrive\git-projects\usb4-emu`, branch `main`), which this project
reads and never modifies, and whose QEMU processes it never stops:
`docs/XHCI98-HANDOFF.md` is the entry point and names the rest
(`ICM-PROTOCOL.md`, `PLATFORM-MATRIX.md`, `REENUM-EXPERIMENT.md`,
`WIN98-TEST.md`, `ARCHITECTURE.md`, `NVME-DRIVER-EVAL.md`, `tools/usb4cm`,
`scripts/run-win98.sh`). Since 2026-10-15 (owner) Linux's
`drivers/thunderbolt` and FreeBSD's `sys/dev/thunderbolt` are references
for this work (ids, layouts, offsets, constants, sequences and quirks
cited by file and symbol), and since `usb4-emu`'s relicensing to
GPL-2.0-only landed the same day (`3c74189`) their code and `usb4-emu`'s
may be adapted, each adapted file keeping its source's copyright lines
and SPDX tag and gaining a `legal-provenance.md` section 2a entry with the
pinned commit (`AGENTS.md`, "Reference Implementations";
`legal-provenance.md` section 3). The
NVMe driver behind the tunnel is `nvme98`
(`D:\OneDrive\git-projects\nvme98`, `docs/PLAN.md`), read-only here too;
what it asks of this connection manager is design record 21 section 7.
Every virtual-machine leg in this file runs with this project's own
overlays under the git-ignored `vm\usb4\` and its own QMP and VNC ports,
47900 to 47999 (record 21 section 10.2).

## The decisions this roadmap rests on

Design record 21 section 11 lists the thirteen Phase 39 needed (one
binary or two, the NT filter's install route, polling with no interrupt,
the approval policy, the absent controller, the `nvme98` channel, the
version and cut, the names, the other NHI ids, safe eject, the master
switch, the log emission, cut before the bench), each put with a
recommendation; the owner's answers are recorded here as
`roadmap-hcd.md`'s table records its phases'.

| Decision | Taken |
|---|---|
| Generic by best effort | Owner, 2026-10-14: the Thunderbolt 3 and USB4 work is made as generic as the evidence allows; on 2026-10-15, as many controllers as possible. Record 21 section 1.4 is the answer: the host interface and re-enumeration are generic; the ICM client is generic across devices and per-generation across the Intel firmware-ICM hosts it addresses, through a hardware profile and a message and lifecycle profile per generation; real USB4 hosts are the software connection manager (Phase 40, design record 22), and an unknown firmware connection manager is unsupported |
| Phase numbers | Owner, 2026-10-15: `TB3` is Phase 39 (`39-0.n`, `39-A.n`, `39-V.n`, `39-E.n`, `39.final`), `U4CM` is Phase 40 (`40.n`, `40.final`); after 40.1, Phase 40's work is split into `40-0.n`, `40-A.n` and `40-V.n` as Phase 39's is (orchestrator, 2026-10-15, for the owner to confirm) |
| Sources | Owner, 2026-10-15: Linux `drivers/thunderbolt` and FreeBSD `sys/dev/thunderbolt` may be read as interface documentation, under the rule design record 20's reading of Linux's xHCI quirks followed (no function body copied); record 21 section 9. Widened the same day, through the `usb4-emu` maintainer's note, once `usb4-emu`'s relicensing to GPL-2.0-only landed (`3c74189`): code from `usb4-emu` (`tools/usb4cm` included), Linux and FreeBSD may be adapted for this work, notices and SPDX tags kept, a `legal-provenance.md` section 2a entry per adapted file; behaviour from Linux alone stays [linux]; nothing moves into `nvme98` (`legal-provenance.md` section 3) |
| One binary (D1) | Owner, 2026-10-15, as recommended: `xhcicm.sys` in `src\cm\` with its own INFs, allowlists and gate |
| The NT filter's install (D2) | Owner, 2026-10-15, as recommended: a separate NT-only INF adding a device-specific `UpperFilters`; answered by `39-0.4`: the device-specific route (Include/Needs `machine.inf` `PCI_DRV`), unforced on 2000, forced on XP and 7; the filter INF staged in `nt\`, out of the 9x path, `&CC_0604` lines only |
| Interrupts (D3) | Owner, 2026-10-15, as recommended: none, polling only, 200 ms idle |
| Approval policy (D4) | Owner, 2026-10-14: `XhciCmApprove` 0 and 1 only, 1 the default; nothing is remembered across reboots, the firmware and the boot ACL holding the state that matters; "approve known devices only, from a hand-filled UUID list" is a later option, not a value (record 21 section 5.4) |
| The absent controller (D5) | Owner, 2026-10-15, as recommended: nothing; a known limitation, its power-on being an ACPI method out of reach on 98 |
| The `nvme98` channel (D6) | Owner, 2026-10-15, as recommended: record 21 section 7.2's proposal, shipped as `39-A.7` once `nvme98` has chosen, the statement withheld until qualified |
| Version and cut (D7) | Owner, 2026-10-15, changed: `3.0.0.0` ships both phases; `39.final` closes Phase 39 without a cut and `40.final` cuts `3.0.0.0` |
| Names (D8) | Owner, 2026-10-15, as recommended: `xhcicm.sys`, `xhcicm.inf`, `xhcicmf.inf`, class System; the device descriptions revised by the owner the same day to name each controller's generation: `xHCI98 Thunderbolt 2 Controller`, `xHCI98 Thunderbolt 3 Controller`, `xHCI98 USB4 Host Router` (from 3.2.0.0 `xHCI98 TB2 Controller`, `xHCI98 TB3 Controller`, `xHCI98 USB4 Router` with the maker and product per id, task 43.73), and for the ports the filter attaches to `xHCI98 Thunderbolt PCI Bridge`, `xHCI98 USB4 PCI Bridge`, `xHCI98 PCI Bridge` (record 21 section 3.3) |
| The NHI ids (D9) | Owner, 2026-10-15, changed: every Alpine Ridge and Titan Ridge NHI claimed from a public id listing, never untested without a plain warning; widened the same day, with the source rule, to every Intel firmware-connection-manager host Linux addresses (Falcon Ridge, Alpine Ridge, Titan Ridge, Maple Ridge, Ice Lake to Meteor Lake), only `15BF` captured, the untested ids acting by default with the warning in the README and release notes (owner); the widening's shape - four-test selection with an inert start, hardware profiles apart from message profiles, per-domain tunnel-port sets, no reset or takeover by the ICM client - is the orchestrator's decision of 2026-10-15 after review, for the owner to confirm (record 21 sections 1.4, 3.3, 6.3) |
| Safe eject (D10) | Owner, 2026-10-15, changed: in Phase 39 as `39-A.8`; NT through the filter's answer to `IRP_MN_QUERY_CAPABILITIES`, 98 SE and ME through CONFIGMG best effort, NT-only with a release-notes line if the 9x half fails; the eject stops the drivers and the user unplugs. Two refinements, the orchestrator's for the owner to confirm: the mark goes on the children of verified tunnel ports, never on a port itself, and `SurpriseRemovalOK` is set only where `39-V.2` shows Windows still lists the device with it (record 21 section 7.4) |
| A master switch (D11) | Owner, 2026-10-15, as recommended: `XhciCmMode`, default 1 (auto), 0 off (the inert start), 2 the ICM client on an Intel id the profile table does not list, at the user's own risk and with that id added to a copy of the INF by the user, 3 the software connection manager's experimental override (design record 22); the install itself is the opt-in, so the default is on (record 21 section 8) |
| The log emission (D12) | Owner, 2026-10-15, as recommended: nothing in `release`, `#if DBG` in `debug` and `qemu` |
| Cut before the bench (D13) | Owner, 2026-10-15, as recommended: the VM legs and gates before the cut, `39-E.1` and `39-E.2` after it on the asset, a failure there a re-cut; the cut is `40.final`'s |
| Time | Owner, 2026-10-15: no task is cut to meet a time; the run goes late instead |
| Phase 42 | Owner, 2026-10-18: the planned items promoted that day are Phase 42, for `3.1.0.0`, with the ids `42-0.n` (the designs), `42-A.n` (the code), `42-V.n` (the legs) and `42.final`: INSTUI.1 is 42-0.1, DI9X.1 is 42-0.2, SNAP.1 is 42-0.3, EJRB.1 is 42-0.4 (its design) and 42-A.5 (its code), DI9X.2 is 42-A.1, INSTUI.2 is 42-A.2 (its legs 42-V.1), SNAP.2 is 42-A.4, GEN1.1 is 42-A.6, QINR.1 is 42-A.7, UNPL.1 is 42-A.8 and TB3NAME.1 is 42-A.9; SNAP.3 (the removal) and GEN1.2 (the multi-drive legs) stay in "Planned work", as do the bench and hardware readings; UNPL.1 back under its own id on 2026-10-20 ("The items left by `3.1.0.0`") |
| The uninstall | Owner, 2026-10-18: the drivers uninstall as NUSB's do, from Add/Remove Programs in one step, on every target, leaving the machine as it was before the install (42-A.3) |
| Phase 42, widened | Owner, 2026-10-18, going through every planned item one by one: into Phase 42 - SNAP.3 (with SNAP.2 as 42-A.4: the removal in `3.1.0.0`, no deprecation release between), IDLE.1 as 42-V.4, U4REC.1 as 42-0.5 and 42-A.10, NVHW.1 as 42-A.11 and 42-V.8 (Stor98's MD-2 driven from here too), NV9X.2 and NV9X.3 as 42-0.7, 42-A.12, 42-A.13 and 42-V.6, EMU.2 to EMU.5 as 42-V.5, PCI9X.1 as 42-A.14, NOIRQ.1 as 42-0.6 and 42-A.15, GEN1.2 as 42-V.7; dropped - SLIP.1 (the F6 floppy serves 2000 and XP Setup; issue 16 keeps the attempt); closed, every task done - the x64 legs owed at the cut (X64.1, X64.2), the emulator's Thunderbolt 3 dock behind a USB4 host on 2000 (EMU.1, read 2026-10-17 with 40-A.8), the sign-only packaging mode (SIGN.1) and Stor98 the renamed driver (STOR.1, STOR.2), their records in `runs/run-41.md` and `build-and-test.md`; kept in "Planned work" - B24.5 (bench), NOX.1 (rare), PCR.1 (no safe proof reachable, record 17 section 4.13), U4HW.1 and S3W2K.1 (hardware); IDLE.1, NVHW.1, NV9X.3, EMU.2 to EMU.5 and NOIRQ.1 (its code) back under their own ids on 2026-10-20 ("The items left by `3.1.0.0`") |
| The machine-wide registry key | Owner, 2026-10-18, on SweetLow's proposal (42-A.17): every `Xhci*` value of `xhci98.sys` and every `XhciCm*` value of `xhcicm.sys` is also read from `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\USB`; a value there wins over the controller's or host interface's driver-key value, which stays the fallback (the INFs write every per-controller value at install, so the other precedence would leave the machine-wide key dead); the helpers and the installer write the machine-wide value and show both; XHCITR shows both and which won. Not taken: the drivers' own service keys (two keys, and 9x creates none for a WDM driver), a key under `Software` (the wrong hive for a driver that may start before it), per-controller precedence with the INFs no longer writing per-controller values (too large for `3.1.0.0`) |
| `XhciFastPollFsLs` 4, the direct encoding | Owner, 2026-10-18, on SweetLow's proposal: value 4 takes the new scheme (codes 128 to 143 program Interval = code - 128) in `3.1.0.0` in place of the 36.19 codes 128 to 130, no new value and no compatibility, since nobody uses value 4 yet (42-A.18) |
| Stor98 on 98 SE and ME, a newer generation | Owner, 2026-10-18, from Stor98's audit of that day: an entry of the link-state channel at a `LinkGeneration` above the one a Stor98 instance is bound to, without `DOWN_QUIESCED`, is evidence that its own generation is quiesced only once the instance has lost presence (gone, or its controller reads all-ones); while the controller still answers it is identity loss - no evidence, nothing completed, no acknowledgement, the drive keeps running and a later presence loss ends at Stor98's gone deadline. Its pool takes only `DOWN_QUIESCED` from the table, plus a later instance's acknowledged disable of the same function. Not taken: the bump as evidence everywhere (the contract's earlier letter, which with 42-A.6's bump completes a working drive's requests while it may still DMA), and never evidence (a real re-plug would then always wait for the deadline). `stor98cm.h` now says so, and that only a new tunnel increments `LinkGeneration` (42-A.6, 42-A.19) |
| Phase 42, the open choices | Owner, 2026-10-18, the choices its tasks left open: the installer (42-0.1) is a window, a wizard that walks the user through the key options and ends by asking for the restart; its pages - welcome, what was found, the components (`xhci98.sys` and `xhciuas.sys` required, Thunderbolt 3 / USB4 support ticked as record 23 section 8.5 decides its default and with its warning, the bundled Stor98 never ticked unasked, the tools folder, `C:\XHCI98` by default), the USB settings, the Thunderbolt settings (when ticked), Stor98's settings (when ticked), the summary, the progress, and the finish with "Restart now" (the default) and "Restart later"; a settings page shows the values record 23 section 8.1 asks, each only where its "Asked when" holds, with its default and its meaning, an update keeping a value the user set; the values never asked behind an "Advanced..." button, each with its default and a warning; on 98 SE and ME a device 42-A.1's helper could not install, or whose wizard the user cancelled, gets a page with that device's Update Driver steps, which the user can save to a text file, and the install completes for the rest. The 98 SE and ME helper (42-0.2) is built with Open Watcom 2.0 against the 98 SE DDK's `SETUPX.H` and `SETUPX.LIB` (`tools/win98ddk-extracted/`). `XHCISNAP` (42-0.3): everything it does that XHCITR does not moves into XHCITR - the `.DEV` device report, the slot and enumeration regions, the raw `.BIN` and `.PSC` dump, `-probe` - the counter-name check moves to a build script, and `xhcisnap\` is deleted. The safe-removal mark's writer (42-0.4) is a privileged NT service. The uninstall (42-A.3) removes the bundled Stor98 only when this package's installer installed it, and restores nothing of another USB stack present before the install (NUSB's, say): Windows finds the controller again. The unsafe-unplug notice (42-A.8, now UNPL.1) is NT-only in `3.1.0.0`, said so in the release notes. 42-V.7 and 42-V.8 (now NVHW.2) are owed after the cut. 42-A.16 (now SRSTOP.1) was seen with a USB stick |
| The connection manager alone | Owner, 2026-10-19: "The installer should give people the option not to install xhci98 but just the CM. Reason is that in case some Win 8 users which have native USB3 but not USB4 or TB may want it" (task 42-A.21). Taken in writing it, the owner to confirm: offered on 2000 to 7 when the package carries the connection manager, the only install on Windows 8 and 8.1 and never on 98 SE and ME (no USB 3 driver of their own there); 10 and later refused as before (11 has a USB4 connection manager of its own); the bundled Stor98 not offered on 8 and 8.1; a run that leaves the USB drivers out never removes an installed `xhci98.sys`, the way back being the uninstall and a run with the connection manager alone; one Add/Remove Programs entry, named by what is registered; Windows 8 and 8.1 described in the release notes as installable for the connection manager alone and never run here, not as a target (record 23 section 4.5, D17 to D19). The owner, the same day: "let user update existing driver by installing over it. Should not have multiple entries in Add remove programs in Control Panel." - one entry, `Uninstall\xhci98`, for every set of components, an install over any of them an update in place, none under `Wow6432Node` (record 26 section 9.1) |
| The items left by `3.1.0.0` | Owner, 2026-10-20: the Phase 42 items moved to "Planned - Left by `3.1.0.0`" take planned ids, the earlier one where they had one, each keeping its 42 id as "was": 42-A.8 is UNPL.1, 42-A.11 NVHW.1, 42-A.13 NV9X.3, 42-A.15 NOIRQ.1, 42-A.16 SRSTOP.1, 42-A.34 SILENT.2, 42-A.35 S98NAME.1, 42-A.36 PRECUT.1, 42-A.37 NUSBPKG.1, 42-A.38 EJTEXT.1, 42-V.4 IDLE.1, 42-V.5 EMU.2 to EMU.5, 42-V.6 NV9X.4, 42-V.8 NVHW.2, 42-V.10 GEN1.3, 42-V.11 EMUFAST.1 and 42-V.12 OWED31.1 |
| Phase 43 | Owner, 2026-10-20, going through every planned item one by one: Phase 43, for `3.2.0.0`, with plain ids `43.n` and `43.final` (owner, 2026-10-20: the tasks ordered by ease, those that can be done locally in a virtual machine first, and renumbered the same day while no published file cited them; first drawn up as `43-0.n`, `43-A.n` and `43-V.n`; renumbered again the same day, still cited by no published file, when the owner sorted the driver pack's tasks in by ease, and a third time the same day when the owner put them first). NUSBPKG.1's redistribution reading is 43.1; NOX.1 is 43.14, 43.15 and 43.16; PRECUT.1 is 43.9; EMUFAST.1 is 43.12, without WHPX (the NT guests have issues under it, so every guest stays on TCG); SILENT.2 is 43.6, S98NAME.1 43.7, EJTEXT.1 43.8, UNPL.1 43.21, NV9X.3 43.18; NV9X.4 is 43.19 but for its NOIRQ.1 leg; IDLE.1 is 43.13; the virtual-machine parts of GEN1.3 and OWED31.1 are 43.20 and 43.17. Added the same day: Windows 8 and 8.1, x86 and x64, as a target of the connection manager alone (43.22, 43.24), with the bundled Stor98 offered there for tunnelled NVMe drives (43.23; owner, 2026-10-20, in place of 42-A.21's decision not to offer it on 8 and 8.1, and over 8.1's `stornvme`, whose surprise removal behind a tunnel has never been read here); and the "can perform faster" notice, read first (43.25); then, the reporter having forced `xhci98.sys` onto 8, `xhci98.sys` itself an opt-in install on 8 and 8.1, x86 and x64, unticked so Windows' own USB 3 driver stays the default (43.26, 43.29, 43.31; in place of 42-A.21's "the only install on 8 and 8.1"), and what of Windows 8's own stack it matches decided by the owner from 43.25's list (43.30); and the reporter's tray idea, XHCITR's balloon and red icon for a device running slower than it can, on every target, on by default only where Windows shows no notice of its own (43.27, 43.28); and the installer's settings pages laid out to fit their text, three rows a page (43.10, 43.11); and, after the renumbering, the rest of NUSBPKG.1 (43.2 to 43.5; owner, 2026-10-20): NUSB 3.3's 98 SE storage files in the release download, kept in a driver pack, a repository of the owner's separate from this one, and copied in at packaging time, so this repository's history never carries them and only the release download does; the owner ships them with no licence to redistribute established (43.1 records it), in place of roadmap-hcd's "this project does not redistribute them" of 2026-10-02; a Components row of the installer on 98 SE only, unticked where NUSB's storage files are found, a file already there never overwritten even when the row is ticked, the files it placed removed by the uninstall, and never offered on ME or later, which use their own drivers. A storage driver of this project's own was considered the same day and not taken. Added the same day (owner, 2026-10-20): Windows 10, x86 and x64, a target of the connection manager and of `xhci98.sys` as an opt-in install, unticked so Windows' own USB 3 driver stays the default, as planned for 8 and 8.1, the x64 drivers loading in test-signing mode, which the installer documents with Secure Boot off, and no Microsoft attestation signature for now; because Windows 10 has no USB4 support of its own (USB4 arrived with Windows 11), so the connection manager gives it what Windows does not; placed right after the 8 and 8.1 tasks and reusing their work, with Windows 10 virtual machines of its own (43.32 to 43.37). The owner's answers the same day: every Windows 10 build below 22000 admitted, the client releases 1507 to 22H2 and the LTSC releases, only 22H2 read and the release notes saying so; Windows Server 2016 to 2022 (10.0 below 22000) admitted as 10, untested, with no Server check; the bundled Stor98 offered on 10 as on 8.1 (43.23); 43.22 widened to read whether Secure Boot enforces signatures on 32-bit 8 and 8.1, the test-signing and Secure Boot note covering them if it does; and Intel's Thunderbolt driver on 10 left open until 43.33 reads it. Also added the same day (owner, 2026-10-20): the isochronous ring enlarged, now, for the `usb-wdm-free` class drivers (UVC, UAC 2), with interrupt moderation considered in the same work (43.38 to 43.41; drawn up first as the planned ISORING.1 to ISORING.4; design record 13 section 11.8), the larger event ring (43.41) the owner's decision; deferred by the owner on 2026-10-22 and moved back to "Planned work" as ISORING.4. Kept in "Planned work": ISORING.4, SRSTOP.1, DUALMODE.1, NOIRQ.1, NVHW.1, NVHW.2, EMU.2 to EMU.5, B24.5, PCR.1, U4HW.1, S3W2K.1, and the rest of GEN1.3 and OWED31.1 |
| What 43.18 left | Owner, 2026-10-22, the four questions task 43.18 left: the vanished-child transaction of record 21 section 7.2 part 3 (a vetoed removal retried, the member's exposure held until every vanished child is retired) is a task of its own, 43.47, built on 43.18's transaction records; the interface exception V8 (Stor98's O6 under the firmware connection manager, which has no per-port teardown: the driver keeps the devnode and the request, blocks the member and retries, and tears nothing down) is agreed, the owner being Stor98's maintainer, and Stor98's header says so in O6; the 98 SE and ME link-down statement's gate-G3 readings are taken now, one per system, on a trial build in emulator guests, each system that passes admitted in record 21, here and in the release notes, one that fails left out and reported; an NVMe eject asked of `xhcicm.sys` with no Stor98 channel for the drive is refused, as built, with a message that tells the user to shut Windows down before unplugging it |

## Phase sequence

Phase 39 is the Thunderbolt firmware-connection-manager client, tunnel
re-enumeration and safe eject; Phase 40 the USB4 software connection
manager; Phase 41 the graphical tree, eject and tray tool XHCITR; all
three ship as `3.0.0.0`. Real USB4 devices follow the ORICO capture
and are planned task U4HW.1. The order is the handoff's: the
Thunderbolt 3 client is provable on real hardware the owner holds; the
software connection manager is provable, for now, only against the
emulator and a Framework 13 capture.

---

## Phase 39 - The Thunderbolt 3 ICM Client, Tunnel Re-enumeration and Safe Eject

Goal: a Thunderbolt 3 dock or enclosure plugged into an Alpine Ridge
machine after boot, or with pre-boot support off, gets its PCIe tunnel and
its devices appear and disappear under Windows 98 SE and Windows 2000, and
can be safely ejected where the operating system offers it: a client of
the controller's firmware connection manager (the ICM) as a WDM driver of
the host interface, the re-enumeration of the tunnel ports' buses after
each change, and the removable mark on the devices behind them; with the
other Intel firmware-connection-manager hosts claimed on profiles read from
the Linux driver, untested and said so. The first of the three steps the
`usb4-emu` handoff orders (`D:\OneDrive\git-projects\usb4-emu\docs\XHCI98-HANDOFF.md`
section 2, maintainer 2026-10-14): this, then the USB4 software connection
manager (Phase 40), then real USB4 devices once the ORICO enclosure capture
exists.

Status: closed 2026-10-17 on the `3.0.0.0` cut, with 39-E.1 open after
it on the owner's bench (D13). Opened 2026-10-15 by the owner, from the planned work `TB3`
drawn up on 2026-10-14 from the handoff, the `ICM-PROTOCOL.md` contract,
the re-enumeration experiment and the P14s captures; on branch `3.0.0.0`.
Design record 21 is the plan, its decisions D1 to D13 taken (D4 on
2026-10-14, the rest on 2026-10-15). Released with Phase 40 as `3.0.0.0`
(D7): `39.final` closes this phase and `40.final` cuts. Vehicle: the
`usb4-emu` QEMU fork's `tb3-alpine-ridge` profile through
`scripts/run-win98.sh` (`BIOS=`, `DEVICE_AT_BOOT=`) and the Alpine Ridge
tree on the Windows 2000 guest, read-only in that repository, with this
project's own overlays under `vm\usb4\` and ports 47900 to 47999 (record
21 section 10.2); the owner's P14s Gen 1 and TS3 Plus dock for the bench.
No other generation has an emulator profile or a machine here.

Why a phase: a second new binary (`xhcicm.sys`) with its own INFs, gates
and package rows; a new hardware contract read from captures and from the
Linux driver rather than a specification; three operating-system
mechanisms (CONFIGMG on 9x, a bridge filter and child filters on NT) no
file here has used; and a release.

Batches: `-0` static, `-A` host, `-V` the VM legs, `-E` the bench, and
`.final`. The ids were `TB3-0.n`, `TB3-A.n`, `TB3-V.n`, `TB3-E.n` and
`TB3.final` until the owner opened the phase.

- [x] 39-0.1 design record 21 reviewed to convergence and the owner's decisions D1 to D13 taken and written into it, before any code (as records 14, 15, 18 and 20 were); D4 taken 2026-10-14 (done 2026-10-15: draft 6, the decisions written in, the profiles, selection and safe eject added, reviewed and revised)
- [x] 39-0.2 the references: the USB4 Specification 2.0 and Connection Manager Guide 2.0 rows in `docs/references/README.md`, fetched per machine, never tracked (done 2026-10-14: the six PDFs of the USB-IF November 2025 bundle under `docs/references/usb4_202511/` with their hashes, taken from `usb4-emu`'s references folder with the owner's permission; Thunderbolt 3 has no public specification, said there; PDF hashes re-verified 2026-10-15); the ring register offsets this driver uses transcribed into `tbt_regs.h` from the specification's section 12 and `tools/usb4cm/core/u4regs.h`, each with its citation (the register part done 2026-10-15, `p39-core` to 0812fc98: `src\cm\tbt_regs.h`, every field cited to USB4 2.0 sections 6.4 and 12, `ICM-PROTOCOL` section 2 or Linux `nhi_regs.h` and FreeBSD `nhi_reg.h`; nothing taken from tbtools)
- [x] 39-0.3 the Windows 98 DDK fetched and recorded (name, version, SHA-256) in `docs/references/README.md`, never tracked, and the CONFIGMG service numbers of record 21 section 6.1 read from its `CONFIGMG.H` service table and written into `cm_cfgmg.h` with that citation; a `legal-provenance.md` row saying the numbers alone were taken. The experiment's run-time guards (`Get_Version` high byte 4, the root id read back) stay in the shipping driver regardless (done 2026-10-15: `win98se_ddk.exe` from archive.org, recorded in `docs/references/README.md`; values in `src\cm\cm_cfgmg.h` with line citations, `Change_DevNode_Status` 91 and `Register_Device_Driver` 14 among them for `39-A.8`; `legal-provenance.md` rows)
- [x] 39-0.4 the NT filter's install route (D2): read on the Windows 2000 guest, then XP and 7, whether a separate NT-only INF (`$Windows NT$` signature, an undecorated Models section, since Windows 2000 reads only that one) with lines for record 21 section 3.3's port ids (`PCI\VEN_8086&DEV_15C0&CC_0604` and the rest) can add a device-specific `UpperFilters` value to a port `pci.inf` serves without displacing its install (an include-and-needs section, or another shape), or whether a class filter is the only route and must be boot-start; and on 98 SE and ME that the engine ignores that INF when it sits beside the other. The answer fixes record 21 sections 3.2, 3.3 and 6.2 - done 2026-10-15 (`runs/run-39.md` 39-0.4): D2 = device-specific route (Include/Needs machine.inf PCI_DRV); unforced on 2000, forced on XP and 7 (unsigned rank); 98 SE and ME read `$Windows NT$` INFs, so the filter INF stays out of the 9x path with a `&CC_0604` line only.
- [x] 39-0.5 the provenance rows: `ICM-PROTOCOL.md` as the contract (one `legal-provenance.md` section 4 row per fact class, method `static`, the capture run ids behind each [obs]); `usb4cm`'s MIT files lifted with their notices (section 2a); and Linux's `drivers/thunderbolt` and FreeBSD's `sys/dev/thunderbolt` as interface documentation under the owner's rule of 2026-10-15, written into record 21 section 9 and `AGENTS.md`'s reference list (done 2026-10-15: Linux/FreeBSD thunderbolt as interface documentation per the owner's rule; the `pci.ids` row added with record 21 section 3.3)
- [x] 39-0.6 `XHCIQUAL` dumps the BIOS `$PIR` table in its routing report (`xhciqual\route.c`, host suite `test_route`), so the bench can read whether the P14s BIOS lists the Thunderbolt root port `00:1c.4` (`WIN98-TEST.md`, "Hot-added NVMe gets no IRQ behind 00:1c") (done 2026-10-15, `p39-pir` 65188cbe..2632d73b: `XHCIQUAL` prints the BIOS `$PIR` table (Microsoft PCI IRQ Routing Table Specification 1.0; checks on version 1.0, size, bounds and checksum; every entry searched, dump capped at 128) in every mode, names `00:1C` as listed or `NO ENTRY for bus 00 device 1C` with the `00:1C` functions from configuration space, reads only; host suite `test_route` 175 checks; QEMU matrix 53 cases, SeaBIOS's i440FX table read at F55A0h and a bridge at 00:1C.0 named with no entry; the P14s reading is 39-E.2's)
- [x] 39-0.7 the launch lines of `usb4-emu`'s local Windows 2000, XP x64 and 7 x64 setups read from their `vms/*/RESULTS-*.md` and `tools/reenum-test/run2k.sh` (`TOPO=ar`), and this project's own launcher written under `scripts\` for its overlays and ports (record 21 section 10.2's rules: never a modification of `usb4-emu`, never a QEMU process this project did not start); whether `usb4-device dock=on` is available in the tb3 dialect on i440FX for V.1.8; and which of the handoff's three workarounds for an NVMe present at POST (`BootGUI=0` then `win`, `-boot strict=on` with a boot index, a SeaBIOS without NVMe) lets the pre-boot legs V.1.3 reach the Windows 98 desktop - done 2026-10-15 (`runs/run-39.md` 39-0.7; `scripts\usb4` launcher): dock=on not on i440FX (V.1.8 to the bench); no POST workaround needed on this project's win98.img, `-BootStrict` carried; NHI unknown on 98 SE and 2000; generic host 0C0340; tb3 ports are 8086:15C0, tunnel port 02:01.0.
- [x] 39-A.1 the scaffold: `src\cm\` with its `sources`, the three flavours, the NHI INFs `xhcicm.inf` and `xhcicm-amd64.inf` with every NHI id of record 21 section 3.3 and the NT-only filter INFs `xhcicmf.inf` and `xhcicmf-amd64.inf` with every port id of it, `xhcicm-imports.allow` and `-amd64.allow`, `check-cm-inf.ps1` with its self-test and the `CM-NO9XBRIDGE` rule (no bridge or root-port id in any `$CHICAGO$` INF of this driver; the filter INF held to `$Windows NT$`), `build-driver.cmd` building and gating it after `xhciuas.sys` on x86 and amd64, `make-package.ps1` staging it in every flavour directory with its footprint rows and test-signing the amd64 half, `xhci_version.h` shared; a driver that installs on the NHI and starts with no hardware access (done 2026-10-15, `p39-scaffold` to dd6c43a6: builds and gates x86 and amd64 `all`; installed and started INERT on 98 SE and 2000 on the usb4-emu tb3 model; host suite `test_cm`, 124 checks)
- [x] 39-A.2 the host interface, pure: `tbt_ring.c` and `tbt_regs.h` lifted from `u4hi.c` and `u4regs.h` (MIT, notices kept), ring 0 TX and RX in raw mode, CRC-32C, the control-packet read with sequence numbers, timeouts and retries; host suite `test_tbt_ring` with the specification's Appendix A.2 vectors, wrap, exhaustion and a frame over its buffer (done 2026-10-15, `p39-core` to 0812fc98: host suite `test_tbt_ring`, 374 checks; the driver build and import gate follow 39-A.1)
- [x] 39-A.3 the ICM client, pure: `tbt_icm.c`, the selection of record 21 section 1.4.2 (four tests, the inert start, the failure after activation, the hand-over to design record 22's admission) and the message and lifecycle profiles of section 1.4.4 (FR with its 14-frame topology query, AR, TR and Maple Ridge, ICL with its 20 s `driver ready` and RTD3 veto), the frames and the state machine of sections 5.1 to 5.6 (bind, driver ready, device connected with the name block, the route, approve, device disconnected, the resume refresh, the 500 ms reconciliation, timeouts, retries and the error flag, an event decoded inside a command wait), the tolerance of section 1.4.6; host suite `test_tbt_icm` with every observed frame `ICM-PROTOCOL.md` quotes, the FR, TR and ICL frames synthesised from Linux's structures, the selection table of section 10.1, and a disconnect landing during a 10 s approve (done 2026-10-15, `p39-core` to 0812fc98: four message rows, FR, AR, TR and Maple Ridge, and ICL, in `tbt_icm.c`, the hardware-profile and selection layer in `tbt_hw.c`; host suites `test_tbt_icm` and `test_tbt_hw`, 409 and 120 checks; the driver build and import gate follow 39-A.1)
- [x] 39-A.4 the NHI function driver: PnP and power (the power policy owner's sequence of record 21 section 4.4 after `hcd_power.c`: the system IRP requesting the device IRP, the D0 IRP retained until the thread's reinitialisation finishes, one `PoStartNextPowerIrp` before each completion, never a wait in dispatch; the unbind, shutdown and system-suspend sequences of section 5.6 in their row's order), the hardware profile of section 1.4.3 (the integrated controllers' force power, firmware-ready poll and LTR at start and at every resume, through the PDO's configuration space, with the power-ownership record that restores only what this driver changed), the candidate chosen from the matched id at `AddDevice` and confirmed by the tests after start, BAR0 mapped, Bus Master set through the PDO's config write, the firmware status, mode and mailbox handshakes, the common buffer below 4 GB from `cm_pool.c` alone, the connection-manager thread and its queue, polling (`XhciCmPollIdleMs`), the master switch `XhciCmMode` and the other registry values of record 21 section 8 written by every INF path, the counters and the log ring with its emission as D12 decides (done 2026-10-15, `p39-nhi` to 07aa2524: the NHI function driver - PnP and power as the power-policy owner, the selection, the thread and polling, the common buffer, the values, counters and log; x86 and amd64 `all` green; on the usb4-emu tb3 model under 98 SE and 2000, driver ready, the device announced, approved and tunnelled, disable and enable, unplug, and `XhciCmApprove` 0 withholding approval (`runs/run-39.md`); sleep and resume not yet read)
- [x] 39-A.5 re-enumeration: the tunnel-port sets per domain of record 21 section 6.3, found by the pure `tbt_topo.c` over `HalGetBusData` accessors with each member's source (the `XhciCmTunnelBridge` list, the discrete switch, the listed integrated root ports, the emulator's SHPC), the PCI Express Link Status and Slot Status decoders and the SHPC presence read (host suite `test_tbt_topo` with the P14s and emulator layouts, the generic layout, a two-port switch, listed and unlisted root ports, two domains, a foreign hot-plug bridge, no member, a port without the capability bits, and the override list); on 98 SE and ME the CONFIGMG route of section 6.1 through the dynalink thunk with both guards; on NT the filter role of section 6.2 with the registration list's publish, withdraw and rundown protocol of section 3.2 and the resume ordering of section 6.3 (active members held from the NHI's suspend, a held member's D0 retained until the NHI's resume succeeds or fails, a failure releasing it with an invalidation, never an expiry taken as success); exclusive port ownership across every source, decided from the hardware inventory, ambiguous ports left inactive; the port poll before a tunnel up and the port reading before a tunnel down (section 6.5); `XhciCmReenum` 0 disabling both (pure half done 2026-10-15, `p39-topo` to 5e93186a: `tbt_topo.c`, record 21 draft 6 section 6.3 sources, Link/Slot/SHPC readings, set poll; host suite `test_tbt_topo`, 380 checks, P14s capture vectors) Done (p39-reenum 81f75d24, merged d8de89c8): the driver half - re-enumeration through the bridge filter on NT and CONFIGMG on 98 SE and ME, the resume hold, rediscovery of an empty set; legs on Windows 2000 and 98 SE. The Titan Ridge, ME, XP and later and resume-hold legs belong to 39-V.
- [x] 39-A.6 the approval policy `XhciCmApprove` (D4, taken) and the device table of section 5.3, with the release notes' security paragraph and its "nothing is remembered" line drafted beside it (done 2026-10-15, `p39-nhi` to 07aa2524: `XhciCmApprove` wired to the ICM client; the release notes' 3.0.0.0 draft carries the security paragraph and the "nothing is remembered" line)
- [x] 39-A.7 the `nvme98` link-down notification (record 21 sections 7.1 and 7.2, D6), once `nvme98` has chosen its receiving interface: the NT `IOCTL_SCSI_MINIPORT` route and the 9x VxD-service route, the recipient resolved and cached at tunnel up by a discovery worker thread of the driver's own, never on the connection-manager thread, its reference held with a target-device-change notification so an eject is not held back (section 7.4), each statement sent only after the qualified platform condition reads clear (the SHPC stage-3 surprise-removal presence change on i440FX; the tunnel port's Data Link Layer Link Active bit where a PCI Express capability exists) and before any removal call, from inside a command wait, a tunnel-up poll or a discovery when the disconnect lands during one, inside the end-to-end budget of record 21 section 7.2 (the idle poll clamped to 250 ms while enabled; no statement when the recipient is not yet cached, for an unmatched disconnect, or while the domain's tunnel-port set has more than one member); the statement withheld on every OS until gate G3's ordering against the emulator's stage-2 event is qualified there, and withheld on hardware until the completed-reset boundary, which this driver cannot observe through a down link, is resolved by `nvme98`'s own MD-2 verification (the P14s with an NVMe enclosure and with one that has an internal switch) and the owner. Not a condition of the checkpoint; the release notes say which state the pair is in (done 2026-10-15: `p39-nvlink` 03bab294, merged 80bd6ad4. `nvme98cm.h` included unchanged; on NT the callback object and the never-freed table, UP before every enumeration, DOWN_QUIESCED only after the path is closed and the endpoint reset and only on the qualified emulator bridge until nvme98 qualifies hardware, the acknowledgement wait (5 s NT, 3 s provisional on 98 and ME) before every removal; on 98 and ME the per-boot registry locator and the REMOVE_PENDING request on the CONFIGMG worker. Read on Windows 2000 against nvme98's receiver and on 98 SE; nvme98's interface 3 adoption, per-port non-blocking waits and the XP x64 and ME legs owed) (closed 2026-10-17 on the `3.0.0.0` cut with the XP x64 and ME legs unread: the per-port non-blocking waits are 42-A.12; no Phase 42 task carries the two legs)
- [x] 39-A.8 safe eject (D10, record 21 section 7.4): on NT the bridge filter's baseline and snapshot of its port's `BusRelations` (active only after the baseline) and the child filters it attaches to devices newly reported on an active member, answering `IRP_MN_QUERY_CAPABILITIES` on the way up with `Removable` (and `SurpriseRemovalOK` on Windows 2000 only if `39-V.2` shows 2000 still lists the device with it), never marking a port itself; on 98 SE and ME `Change_DevNode_Status` adding `DN_REMOVABLE` to each new child devnode of a tunnel port behind the `Get_Version` guard; `XhciCmSafeEject` (default 1) disabling both; the eject stopping the subtree's drivers with the tunnel left up until the unplug; host vectors for the snapshot rule in `test_tbt_topo`; and the release-notes lines of section 7.4 drafted, the 9x half shipped off with its line if `39-V.1` or `39-V.3` shows it cannot work Done (p39-reenum 81f75d24, merged d8de89c8): Removable from the child filters on NT, DN_REMOVABLE on 98 SE and ME, a refused removal never forced; the Windows 2000 eject legs passed. The SurpriseRemovalOK reading and the 98 SE and ME eject applet belong to 39-V.2.
- [x] 39-V.1 Windows 98 SE, the nine legs of record 21 section 10.2's table on `tb3-alpine-ridge` (`OFF_OFF`, `OFF_ON`, `ON_OFF`, `ON_ON`, device at boot and not): install, approve and the NVMe volume with no Refresh; `device_del` and `device_add`; the boot-tunnelled device left alone, on an overlay carrying 39-0.7's POST workaround, with no reset and no teardown in the trace; the empty table with the controller present (`OFF_OFF`, `OFF_ON`); the absent controller (`ON_OFF`, `ON_ON`, nothing attached); disable and enable; `XhciCmApprove` 0 and `XhciCmMode` 0 (the inert start); the dock's xHCI under `xhci98.sys` where the dialect offers it; V.1.9, the 9x half of safe eject (`Change_DevNode_Status` and `Get_DevNode_Status` of the NVMe devnode, never a port's, and whether 98 SE offers any eject); every leg with `-trace 'usb4_*'` and the `qemu` flavour's log (read 2026-10-15 on 98 SE, `tb3-alpine-ridge`: V.1.1 to V.1.7 pass - install, approve and the NVMe volume with no Refresh, device_del and device_add, the boot-tunnelled device on OFF_ON and ON_ON left alone with no reset, the empty table, the absent controller, disable and enable, approve 0 and mode 0; V.1.9 DN_REMOVABLE set, with the eject through `INSTALL /eject`; V.1.8 to the bench); closed 2026-10-16, V.1.8 moved to 39-E.1
- [x] 39-V.2 Windows 2000, the same table on the Alpine Ridge tree (`TOPO=ar`), plus the filter's install through its own INF, the boot question of 39-0.4, S3 where the guest offers it with the resume ordering of record 21 section 6.3 read (a resume that succeeds, one that fails, an unplug during sleep, the watchdog's expiry), and on the SMP guest under Driver Verifier a filter disable concurrent with tunnel work (the rundown of section 3.2) and a child filter's attach and removal; and the NT half of safe eject: the NVMe listed in "Unplug or Eject Hardware" with `Removable` alone, stopped, held, then `device_del` with no Unsafe Removal dialog, `device_add` listed again, a plain `device_del` with no eject, the boot-tunnelled device not listed until re-plugged, `XhciCmSafeEject` 0, and the build with `Removable` and `SurpriseRemovalOK` both set - whether 2000 still lists the device and whether a plain unplug then shows the dialog, the reading that decides 39-A.8's 2000 path (read 2026-10-15: the Windows 2000 legs pass on the Alpine Ridge tree and on Titan Ridge, the SMP guest under Driver Verifier, the safe eject with Removable alone; R32: the silence passes, the re-plug fails by platform and is owed on the post-cut emulator; the host interface liveness monitor built and read; S3 owed) (done 2026-10-17: the R32 re-plug passes on the current emulator where the platform restores the configuration; S3 not readable on 2000 in the emulator - the guest vetoes Stand by, and a forced S3 never resumes even with no `xhcicm.sys` - closed by the owner, ME's S3 readings standing, bare metal planned as S3W2K.1; run-41.md)
- [x] 39-V.3 ME, XP, XP x64, Vista x86 and x64, 7 x86 and x64: the install, one plug, unplug and re-plug cycle, and the safe-eject cycle (eject, unplug, re-plug, a plain unplug: whether the device is listed, whether any dialog shows) each; on ME, what "Unplug or Eject Hardware" lists and whether a devnode marked through `Change_DevNode_Status` is listed and stops cleanly. These tiers carry no checkpoint tax: the legs are read and recorded, a failure on one is a release-notes line and not a checkpoint failure, and the install leg from the asset that every release requires on every target is `40.final`'s; on 7 x64 whether `pci.sys`'s own hot-plug acts before the filter is a reading, not a fault (NT tiers read 2026-10-15: XP, XP x64, Vista x86 and 7 x86 pass - the install with the nt\ filter, the plug cycle with the NVMe up with no Refresh, the safe-eject cycle listed with Code 47 and no dialog, a plain unplug with no dialog; Vista x64 and 7 x64 owed for want of a test-signed package; ME owed); Vista x64 and 7 x64 read 2026-10-16 on the owner's pre-cut test-signed package (a platform and recipe reading; the release's x64 binaries change before the cut): the install with the nt\ filter in test-signing mode, the plug cycle, the safe-eject cycle through Safely Remove Hardware and through INSTALL /eject, Code 47, no dialog, pass on both (run-39.md, "39-V.3 on Vista x64 and 7 x64"); ME read 2026-10-15: the install through INSTALL.BAT and one plug cycle pass, "Unplug or Eject Hardware" lists nothing (the eject is `INSTALL /eject`), Stand by and Hibernate resume pass; closed 2026-10-16, the reading on the signed asset is X64.1
- [x] 39-V.4 the `usb4-generic` profile (class `0x0C0340`, no ICM) left unclaimed by this phase's INFs: no model line matches it, nothing binds and nothing is logged, read on 98 SE and 2000 (Phase 40's INF lines claim it later) (read 2026-10-15: Phase 40's `PCI\CC_0C0340` line claims the `usb4-generic` host as designed, "xHCI98 USB4 Host Router", on 98 SE and ME; 2000 in run 40); closed 2026-10-16
- [ ] 39-E.1 the owner's P14s Gen 1 with the TS3 Plus under Windows 98 SE, after the `3.0.0.0` cut: Assist off, pre-boot off, the package installed, the dock approved and its tree up, its xHCI controllers under `xhci98.sys` with a stick and a keyboard, `XHCISNAP` on each; unplug and re-plug at the desktop; pre-boot on, the tree at boot and then a hot-plug cycle; the DebugView emission at a disable; the 9x half of safe eject where it shipped (record 21 section 10.4); it also takes 39-V.1's V.1.8 (2026-10-16)
- [x] 39-E.2 the P14s `$PIR` dump from 39-0.6, by the procedure in `xhciqual/hardware-testing.md` (its section "ThinkPad P14s Gen 1 - does the `$PIR` list the Thunderbolt root port?"), and its consequence written into the release notes: whether a function hot-added behind the tunnel is started at all on Windows 98 (Code 29 is a devnode `PCI.VXD` never started, which no polling below it mitigates; `WIN98-TEST.md`, "Hot-added NVMe gets no IRQ behind 00:1c"), and if not, that pre-boot tunnelling is the route on that machine; and separately whether a controller that does start without a usable line runs under `xhci98.sys`'s polled delivery; closed 2026-10-16 (owner) without the dump: the owner's P14s answered the question with the fix of record 21 section 6.1.1 - a function hot-added behind the tunnel starts on 98 SE, on the port's IRQ 10 (docs/issues/18-p14s-gen1-tb3-nvme-hot-plug-98se.md)
- [x] 39.final the phase closed, cutting nothing (D7): the documents drafted for the `3.0.0.0` notes (the security paragraph and the "nothing is remembered" line; the limitations - the absent controller and force power, safe eject and which half shipped, the Unsafe Removal dialog on 2000, a device tunnelled before the driver bound offered for eject only after a re-plug, suspend with an integrated controller, system suspend with a tunnelled device attached unsupported on every target, the `nvme98` channel's state; the untested-hosts line of record 21 section 1.4.6, with every id but `15BF` named untested; and "Untested ground"), `README.md`'s hardware list with the same warning, `source-files.md`, `docs/README.md`'s table; the builds on x86 and amd64, every gate and the host suites; a dry build of the package (`make-package.ps1`, no release directory, no signature); 39-V.1 and 39-V.2 read on the `qemu` flavour and then the `release` flavour of that dry build (done 2026-10-17: closed with the `3.0.0.0` cut, task 40.final)

Checkpoint: record 21 converged and the owner's decisions taken before the
first `-A` task; the three host suites green with the import and INF gates
on both architectures; 39-V.1 and 39-V.2 read as their tables name them on
the `qemu` flavour and then the `release` flavour of `39.final`'s dry
build, with the `usb4_*` trace showing the client's frames as
`ICM-PROTOCOL.md` section 6 describes the real firmware's conversation;
39-V.3 (no tax: read and recorded, not a condition) and 39-V.4 read; then
`40.final`'s cut of `3.0.0.0`, with the install leg from the asset on every
target; then 39-E.1 and 39-E.2 read by the owner after the cut, a failure
there a re-cut - an exception to "no phase advances past an unobserved
checkpoint" that decision D13 granted, as Phases 28 to 31 and 35 each had
it granted. Phase 40 proceeds with 39-E outstanding under the same
decision. "Observed on both" is the Windows 98 SE bench and the Windows
2000 VM leg, as for every HCD phase. No generation but Alpine Ridge is a
condition of the checkpoint: the others are claimed untested (D9).

Records: design record 21; `docs/references/README.md` (the USB4 bundle
and the Windows 98 DDK rows); `legal-provenance.md` (the `ICM-PROTOCOL.md`,
`usb4cm`, DDK, Linux and FreeBSD and `pci.ids` rows); `runs/run-39.md` when
the legs produce runs; the `usb4-emu` documents record 21's "Sources"
lists.

## Phase 40 - The USB4 Software Connection Manager

Goal: real USB4 hosts, all vendors, where the operating system is the
connection manager: the second step of the handoff's order, after Phase
39. Contract: the USB4 Specification 2.0 and Connection Manager Guide 2.0,
`usb4-emu/docs/ARCHITECTURE.md` sections 4, 5 and 7 and `WINDOWS-DEBUG.md`
("What the Windows CM required of the model"), and Linux's and FreeBSD's
drivers, whose code, like `usb4-emu`'s, may be adapted with its notices
kept since `usb4-emu`'s relicensing to GPL-2.0-only landed on 2026-10-15
(`AGENTS.md`, "Reference Implementations"; `legal-provenance.md` section
3). Start from `usb4-emu/tools/usb4cm/core` (GPL-2.0-only from `3c74189`,
C89, polling, 32-bit DMA, about 1.3 thousand lines; it brought a tunnel up
from the Windows 98 DOS prompt on 2026-10-14), in the same binary as the
ICM client, admitted by the
positive software-connection-manager rule record 21 section 1.4.2 outlines
and design record 22 defines. Test on `usb4-generic` with both host models
(`minimal`, `intel-adl`) and with `preos-cm=on` (the pre-OS tunnel cleanup
the Windows 11 CM performs). Write it to depend on no exact device router:
every router operation possibly unsupported, adapter numbers discovered
from the capability chain and the PCIe Downstream Entry Mapping operation,
every completion polled with a timeout. The handoff's size estimate is 4
to 7 thousand lines. Under a software connection manager this driver
tears paths down itself, so `nvme98`'s obligation 2 (record 21 section 7)
becomes real work here: the link-down statement only after the path is
closed and the endpoint's reset has completed.

Status: closed 2026-10-17 on the `3.0.0.0` cut (`40.final`). Opened
2026-10-15 by the owner, from the planned work
`U4CM`; released with Phase 39 as `3.0.0.0` (D7), the cut being
`40.final`'s. Its design record is design record 22. Nothing in it waits
for the ORICO USB4 enclosure capture except planned task U4HW.1, which adds a real
device-router preset and a conformance check.

- [x] 40.1 the design record (design record 22): the connection manager's state machine from `u4cm.c` (host router, adapters, device router per port, DROM by router operation, the CV/CR handshake, lane bonding, the PCIe tunnel's four path entries, the optional USB3 tunnel), hot-plug events and unplug handling (which `usb4cm` lacks), the positive admission and the preservation of every existing tunnel and link power state (record 21 sections 1.4.2 and 1.4.5), the router quirks record 21 section 10.1 assigns to it, a Thunderbolt 3 device behind a USB4 host (legacy routers configured router by router, PCIe tunnel first), the pre-OS cleanup, and what the two profiles share (done 2026-10-15: design record 22 (draft 2), reviewed and revised to convergence; decisions U1-U16 taken by the orchestrator, for the owner to confirm, and U17, the host interface lost, the owner's of the same day; the record's table runs U1 to U17)
- [x] 40-0.1 the transcription: `tbt_regs.h` gains every router, adapter, path, TMU, PCIe, USB3 and notification field record 22 uses, each cited to the USB4 Specification 2.0 section and cross-checked against Linux `tb_regs.h`; the `legal-provenance.md` rows for the Linux and FreeBSD reading and the `usb4cm` and Linux adaptation rows (done 2026-10-15, `p40-core` to b146b58e: the Phase 40 block of `tbt_regs.h`, each field cited to the USB4 Specification 2.0 and cross-checked against Linux `tb_regs.h`)
- [x] 40-0.2 the USB4 ids: the Lunar Lake, Panther Lake, Wildcat Lake, Barlow Ridge, AMD and ASM4242 tunnel-port ids read from `pci.ids` 2026.10.14 with a provenance row; `xhcicm.inf` and `xhcicm-amd64.inf` gain `PCI\CC_0C0340`, the Lunar Lake, Panther Lake, Wildcat Lake and Barlow Ridge NHI ids and `1022:1668` and `1022:1669`; `xhcicmf.inf` and `xhcicmf-amd64.inf` gain those port ids and `1B36:0001` and `1B36:000C`; `check-cm-inf.ps1` gains the class-line, no-device-router-id and `&CC_0604` rules with its self-test (done 2026-10-15, `p40-ids` to 9495719a: the USB4 NHI and port ids, the class line read matching on 98 SE, the filter INF in `nt\` with `CM-NO9XBRIDGE`'s layout rule; x86 and amd64 `all` green)
- [x] 40-0.3 the launcher reading: whether `-global usb4-host.host-model`, `.preos-cm`, `.usb3-xhci`, `.usb3-port1` and `-global usb4-device.usb3-dev` reach their properties through 39-0.7's launcher, and how `docs/HARNESS.md`'s injection points drive leg U.4 (done 2026-10-15, `runs/run-40.md`: all five `-global` properties reach QEMU, read back in `info qtree`; `usb3-dev` drives the USB3 tunnel, `usb3-xhci` and `usb3-port1` shape only the ACPI entry; the launcher gained `-BaseImage` and `-RunRoot`; the pre-OS tunnel seen from boot on 98 SE and 2000 on both host models; the harness acts on the endpoint and the tunnel gate only, the unplug itself is `device_del`)
- [x] 40-A.1 the transport, pure: `tbt_ring.c` gains control packets, record 22 section 5.1's notification classes, plain and enhanced acknowledgements, the hot-plug acknowledgement, the `clock_us` and safe-point callbacks, and maximum-versus-minimum waits; `test_tbt_ring` grows to match (done 2026-10-15, `p40-core` to b146b58e: the control transport with plain, enhanced and deferred acknowledgements, hot-plug acknowledgements, bounded waits and safe points; `test_tbt_ring`, 522 checks)
- [x] 40-A.2 the selection, pure: record 21 section 1.4.2 and record 22 section 3 (in `tbt_hw.c`'s selection or a `tbt_select.c`, as the code decides); host tests with every negative case and every AUTH_DONE and ICM_EN combination (done 2026-10-15, `p40-core` to b146b58e: in `tbt_hw.c`' selection, the positive admission, the experimental and forced routes; `test_tbt_select`, 207 checks)
- [x] 40-A.3 the connection-manager core, pure: `tbt_u4cm.c`, record 22 sections 6, 7.1 to 7.5 and 9 (credits, quarantine, the TMU, the quirk table, discovery and preservation); host suite `test_u4cm` against a C89 fake fabric with the `minimal` and `intel-adl` presets, two domains and a Thunderbolt 3 router (done 2026-10-15, `p40-core` to b146b58e: `tbt_u4cm.c` and `.h`; `test_u4cm`, 672 checks, over a C89 fake fabric with the minimal, intel-adl and asm4242 presets and two domains)
- [x] 40-A.4 `tbt_topo.c` gains the router-named source and the learned correspondence; `test_tbt_topo` grows to match (done 2026-10-15, `p40-topo` to 865dde19: `XHCI_TBT_SRC_ROUTER`, `XhciTbtTopoAddNamed`, the instance bindings `XHCI_TBT_BINDS`, learning with `XhciTbtLearnArm`, `XhciTbtLearnUp`, `XhciTbtLearn` and `XhciTbtLearnDown`, and `XhciTbtPortMember`, failing closed; host suite `test_tbt_topo`, 543 checks)
- [x] 40-A.5 the `usb4-emu` fabric cross-check script, recorded as a reading (done 2026-10-15, `runs/run-40.md`: the pure core against the usb4-emu fabric model at ce13e0e, minimal, intel-adl and asm4242, 45469 checks, no failures)
- [x] 40-A.6 the driver half: the profile chosen at start, Ice Lake to Wildcat Lake force power and LTR with the ownership record, the ring takeover, the resume obligations and the filter hold, `XhciCmMode` 3, `XhciCmUsb3Tunnel`, `XhciCmLaneBonding` and `XhciCmPreOsCleanup` written by every INF path, the `u4.*` counters, `KeTickCount` and `KeQueryTimeIncrement` in the import allowlists with record 13's evidence (done 2026-10-15: `p40-drv` 7acb5efb, merged d0004e1b; legs in run-40.md)
- [x] 40-A.7 the `nvme98` statement under the USB4 profile (the endpoint-presence anchor and the 3 s deadline), with 39-A.7 (done 2026-10-15 with 39-A.7: the USB4 profile's hooks, UP from the hook, DOWN with PathClosed, presence samples; host-tested, the Windows 2000 guest leg on `usb4-generic` owed) (closed 2026-10-17 on the `3.0.0.0` cut with that leg unread; no Phase 42 task carries it)
- [x] 40-A.8 Thunderbolt 3 device routers behind a USB4 host, record 22 section 8 (PCIe first; the DROM by bit-bang as a second pass) (pure part done 2026-10-15 in `tbt_u4cm.c`, `p40-core`; the driver side, the router-kind log, done in `p40-drv`; the DROM bit-bang, the version by its major and the upstream adapter from the read's response done 2026-10-16 on `tb3ver`, read on 98 SE and 7 x86, `runs/run-41.md`) (2000 leg, the one EMU.1 owed, done 2026-10-17 on the signed `3.0.0.0`: the TS3 Plus model behind `usb4-generic`, the DROM by bit-bang, the upstream adapter from the read's response, hot plug, unplug, re-plug and safe eject pass, and the 98 SE regression on the same package; run-41.md)
- [x] 40-A.9 the safe-eject entry point for 39-A.8 (done 2026-10-15: `TbtU4EjectPcie` through `CmReenumSetEject`, the PCIe tunnel alone, USB3 kept; the Windows 2000 leg passed, the eject to be re-read on the final build in 40-V.2)
- [x] 40-V.1 Windows 98 SE, legs U.1 to U.13 of record 22 section 14.2 (partly read 2026-10-15: U.1, U.3, U.5, U.6, U.8 fabric half and U.10 pass) (also read 2026-10-15 on the ASM2464 device model of the ORICO capture on `intel-adl`: the install, the tunnel, the NVMe read and written with no Refresh, unplug, re-plug and a cold boot pass after the DROM unused-adapter fix, record 22 section 6.11); U.2 read 2026-10-15 for the connection manager (the NVMe at Code 29 beside the `-Usb3` xHCI, the guest's interrupt routing), U.8's USB3 tunnel at 5 Gb/s, and U.14 (the host interface lost: contained, no access after the gate reopens); U.4, U.7, U.9 and U.11 to U.13 owed. Read 2026-10-17 on the signed `3.0.0.0` (`runs/run-41.md`, "Windows 98 SE after the cut: legs U.4 to U.13"): U.4, U.7, U.9, U.12 and U.13 pass; U.11's connection manager passes on both host models but two volumes do not mount together (the second tunnel bridge's NVMe given no interrupt, and the first disk dropped at the second port's republication), open, and carried to 42-A.6 and its leg in 42-V.2; closed with the phase on that reading
- [x] 40-V.2 Windows 2000, the same legs plus the filter on `1B36:0001` (partly read 2026-10-15: U.2 hot plug through the filter with nvme98, and the safe eject) (the ASM2464 model as on 98 SE, plus the safe eject, pass) (read 2026-10-15: U.1 to U.13 pass on `minimal`, `intel-adl` and `asm4242`, U.11 on `asm4242` failing by platform with Code 12; the PCIe-only eject re-read passes; S3 and the Thunderbolt 3 router behind a USB4 host owed); U.14, the host interface lost under the USB4 profile, passes 2026-10-15 (contained, no access, a re-enable starts a new instance that finds the tunnel preserved) (done 2026-10-17: the Thunderbolt 3 router behind a USB4 host passes on the signed `3.0.0.0`; S3 not readable on 2000 in the emulator, closed by the owner as for 39-V.2, bare metal planned as S3W2K.1; run-41.md)
- [x] 40-V.3 ME, XP, XP x64, Vista and 7: the install and one plug cycle each, with no checkpoint tax (read 2026-10-15 on `usb4-generic` `intel-adl`: XP, XP x64, Vista x86 and 7 x86 pass, the install and one plug cycle; Vista x64 and 7 x64 owed for want of a test-signed package; ME passes, 2026-10-15: INSTALL.BAT and one plug cycle; Vista x64 and 7 x64 pass, 2026-10-16, on the pre-cut test-signed package, run-40.md's last section); closed 2026-10-16, the reading on the signed asset is X64.1
- [x] 40.4 onward: the install and upgrade tool, design record 23 (`p40-inst`). Done 2026-10-15 (`p40-inst` 2bfcd27f, merged bcc74be4): `INSTALL.BAT` with `XHCIINST.EXE` and `XHCIIN64.EXE`, record 23 rev 8. The tool waits for Windows' own installs before and between its steps, the last line agrees with the exit code, the 98 SE and ME fixes, the Windows 2000 filter restart. Legs pass on 98 SE, ME, 2000, XP, XP x64, Vista x86 and x64, 7 x86 and x64 (run-40.md). Owed: Vista x64 update after the restart, 7 x64 uninstall over 3.0.0.0, and the 2000 filter install through the tool on the Thunderbolt test machine. The first two read 2026-10-16 on the pre-cut test-signed package: pass (run-40.md, last section).
- Renumbered 2026-10-16 (owner): task 40.4, real USB4 devices, left Phase 40 for the planned task U4HW.1 (no USB4 host that boots these systems is at hand), and the tasks after it moved down by one: 40.5 onward is 40.4 onward (40.5 to 40.7 as proposed are 40.4 to 40.6). Run records, commits and memories before this change use the old numbers
- [x] 40.final the `3.0.0.0` cut: the release notes for both phases (`39.final`'s drafts, the safe-eject and untested-hosts lines among them, and Phase 40's), `README.md`, `source-files.md`, `docs/README.md`'s table; the builds, the gates, the dry cut and the owner's signed cut of `3.0.0.0`, with the install legs read from the asset on every target. Task 40.4 is not a condition of the cut: it waits on the ORICO capture and is carried into a later release if the capture is not in hand (an orchestrator decision of 2026-10-15, for the owner to confirm) (done 2026-10-17: the owner's signed cut of `3.0.0.0` at `8ff3b3e0`, the gated x86 and amd64 builds passed, `releases/3.0.0.0` committed; the install legs read on the release candidate's packages on every target overnight and on the signed asset on 7 x64, run-41.md)

Checkpoint: design record 22 converged before 40-A's code; the host suites
green with the gates on both architectures; 40-V.1's and 40-V.2's legs read on the `qemu`
flavour and the `release` flavour of the dry cut; the cut with the install
leg from the asset on every target; then Phase 39's bench (39-E) after it.

---

## Phase 41 - XHCITR, the USB and Thunderbolt Tree with Eject and Tray Icon

Goal: a graphical tool for every target that shows every USB controller,
hub and device and every Thunderbolt 3 / USB4 host, router, tunnel and
the Windows devices behind it, with their details, ejects a tunnelled
device as Windows' own Safely Remove Hardware does, and sits in the
notification area, starting at login. `INSTALL /devices` and
`INSTALL /eject` (design record 24) stay as they are.

Status: closed 2026-10-17 on the `3.0.0.0` cut. Opened 2026-10-16 by the owner, who held `3.0.0.0` for it;
it ships in `3.0.0.0` with Phases 39 and 40, and `40.final`'s cut comes
after `41.final`. Its design record is design record 25.

- [x] 41-0.1 the design record (design record 25), reviewed until it converges; the icon `xhcitr\xhcitr.ico` (done 2026-10-16: revision 5, reviewed and revised four times, converged; the icon drawn in the 16 VGA colours)
- [x] 41-A.1 `xhcicm.sys`: the query device `\\.\XHCICM<n>` and `IOCTL_XHCICM_QUERY` (record 25 section 4), the router extension cache (link rate and width, link state), the pure builder `cm_query.c` with host tests in `test_cm` (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-A.2 `tr_core.c`, pure: query and record parsing, descriptor decoding, speed names, the tray event diff, tree keys, the report text; host suite `test_tr` (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-A.3 the window: tree, details pane, View check boxes, refresh on device change, Save Report, Copy, Properties (record 25 section 3), and the USB branch (section 5) (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-A.4 the eject from the window: record 24's units fed from the query, the confirmation, the call per system, the answers (record 25 section 6) (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-A.5 the tray icon: menus, single instance, events and balloons, the tooltip on 98 SE and ME (record 25 section 7) (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-A.6 install, upgrade and uninstall in XHCIINST (the copy, the Run value, closing a running copy, `/notray`), the packager's `tools\xhcitr\`, the import allowlist, `test-package.ps1` (record 25 sections 8 and 9) (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-V.1 legs T.1 to T.6 of record 25 section 11 on 98 SE, ME, 2000, XP, XP x64, Vista x86 and 7 x86 (Vista x64 and 7 x64 with the test-signed package) (read 2026-10-16, `runs/run-41.md`: 2000, XP, XP x64, Vista x86 and 7 x86 pass; Vista x64 and 7 x64 the tool on the earlier signed drivers (fallback) pass; 98 SE and ME: install, login start, uninstall, the USB tree, the Thunderbolt branch, Safely Remove (window, right click, tray, `/eject`) and the ME balloons pass with the tool of `85f3fb90`, which matches the query by name there (98 gives the driver no instance ID); the 98 SE tooltip text unread; the new x64 drivers wait for the re-signed package; the upgrade of a tool in use unread) Done 2026-10-16: Vista x64 and 7 x64 pass with the owner's test-signed final drivers; unread: the approval balloon (the emulator approves at once), the 98 SE tooltip text, the upgrade of a tool in use.
- [x] 41-A.7 Safely Remove for plain USB devices (the owner's decision of 2026-10-16, which reversed decision T6): record 25 revision 7, section 6.5 - a USB device with a disk (or, on NT, a volume) in what stops with it, on any listed controller; never a hub, an input device, a radio or the controller; the short transaction (the shared mutex, the device found again by instance ID, its set and rule read again, a change count read just before the call); the tray's two groups, `/eject id=`, `/report`; host vectors in `test_tr` (done 2026-10-16, branch `tr-usbeject`; the design and the code reviewed until they converged; `runs/run-41.md`, "Plain USB devices")
- [x] 41-A.8 the drivers' logs in XHCITR (the owner's request of 2026-10-16): record 25 revision 9 and record 21 section 8 - `xhcicm.sys`'s query device answers `IOCTL_XHCICM_LOG` (`XCML`: its log ring copied under `LogLock`, its counters formatted after each pass, the child filters' among them, three address records withheld); `xhci98.sys`'s log and counters read through its door as `XHCISNAP` reads them, with no change to that driver (level 4 withheld); a "Driver log" node per xhci98 controller and per NHI with the recording-off advice; both logs in Save Report and `/report`; host vectors in `test_cm_query` and the new `test_trlog` (done 2026-10-16, branch `tr-logs`; the design and the code reviewed until they converged; `runs/run-41.md`, "The drivers' logs")
- [x] 41-A.9 PCI configuration in XHCITR (the owner's requests of 2026-10-16): record 25 revision 12 and record 13 section 8.12 - `xhcicm.sys`'s query device answers `IOCTL_XHCICM_PCI` (`XCMP`: the NHI, the tunnel ports and, on 2000 and later, the functions below them by a bounded walk, through `HalGetBusData`, never written; 98 SE and ME do not read below a port); `xhci98.sys` answers `IOCTL_XHCI98_QUERY` on `\\.\HCD<n>` (`XHCQ`: its counters, its log ring at levels 2 and 3, its controller's configuration space at every level, framed as `xhcicm.sys`'s log request; the USBUSER pass-through unchanged), and XHCITR reads xhci98's log through it; both drivers' logging level defaults to 2 (`XhciLogVerbosity` and `XhciCmLogVerbosity`); a "PCI configuration" node per NHI and per xhci98 controller with the decoders and the bounded capability walk; host suites `test_trpci` and `test_xhq` (done 2026-10-16, branch `tr-pci`; record 25 leg T.9 read on 98 SE, ME, 2000, XP, 7 x86 and 7 x64, `runs/run-41.md`, "PCI configuration, the query request and the default level"; the owner's ATTO reading of level 2 on the P14s there too)
- [x] 41-V.2 leg T.7 of record 25 section 11 (read 2026-10-16, `runs/run-41.md`, "Plain USB devices": 98 SE, ME, XP and 7 x86 pass - the label with its letter, Safely Remove from the window, Delete, a right click and the tray, the device gone (98 SE, ME) or held at Code 47 (XP, 7), unplug and re-plug with the drive back, the refusal with a file open (98 SE, XP, 7), the UAS disk (98 SE, XP, 7), a tablet, a keyboard and a hub never offered; ME's refusal and the UAS re-plug unread)
- [x] 41-V.3 leg T.8 of record 25 section 11 (read 2026-10-16, `runs/run-41.md`, "The drivers' logs": 98 SE, 2000 and 7 x86 as a standard user pass - both Driver log nodes with recording off (the advice and the key) and on, live after a plug and an unplug, both logs in `/report` with no address; the eject before a log unread, the emulated NVMe having no driver in a cut without Stor98)
- [x] 41-A.11 Start at login, per user (the owner's request of 2026-10-16): record 25 revision 10, section 7.4 - "Start at login" in View and the icon's right-click menu writes `TrayAtLogin` under the user's settings key; a `/tray` start ends at once when it is 0; grey when the machine's Run value is absent; host vectors in `test_tr` (done 2026-10-16, branch `tr-usbeject`, reviewed; read on 98 SE and 7 x86, `runs/run-41.md`)
- [x] 41-A.13 the tree's labels (the owner's request of 2026-10-16): each controller's vendor and PCI id, a Thunderbolt chip's own xHCI named by its NHI, the root hub's ports by protocol, each port's protocol; record 25 revision 10, addendum; host vectors in `test_tr` (done 2026-10-16, branch `tr-usbeject`, reviewed; read on 98 SE and 7 x86, `runs/run-41.md`)
- [x] 41-A.10 resources in the details (the owner's request of 2026-10-16): each devnode's PCI location and what it was allocated - memory, I/O, IRQ (an MSI named as such), DMA, bus numbers (2000 and later) - and, with a problem code, what it asked for; record 25 revision 10, second addendum; host vectors in `test_tr` (done 2026-10-16, branch `tr-usbeject`, reviewed; read on 98 SE behind the emulated Thunderbolt 3 enclosure and on 7 x86, `runs/run-41.md`)
- [x] 41-A.12 Logging level (the owner's decision of 2026-10-16): record 25 revision 13, section 7.5 - `xhci98.sys`'s `XhciLogVerbosity` and `xhcicm.sys`'s `XhciCmLogVerbosity` shown side by side and set 0-4 from the View menu and a controller's or an NHI's right-click, an absent value read as 2 and one out of range shown and never written; a standard user on 2000 to 7 through "Set as administrator..." (`/setlevel`, a second mode of the elevated run of section 6.6); the restart offered; the levels in `/report` and Save Report; it replaces `XHCISNAP -verbosity` and `SNAPON.BAT` (XHCISNAP ships unchanged in `3.0.0.0`); host vectors in `test_tr`; leg T.10 read on 98 SE and on 7 x86 as a standard user, both pass (branch `tr-loglevel`, reviewed; `runs/run-41.md`, "Logging level")
- [x] 41.final `README.md`, the release notes, `source-files.md`, `docs/README.md`; merged into `3.0.0.0` before `40.final` (done 2026-10-17: merged into `3.0.0.0` before the cut)

Checkpoint: design record 25 converged before 41-A's code; the host
suites green with the gates on both architectures; legs T.1 to T.6 read on
the dry cut's `release` flavour; then `40.final`.

---

## Phase 42 - Release `3.1.0.0`: the Windowed Installer, the Uninstall, the Windows 98 SE and ME Device Install, XHCISNAP Retired, and the Connection-Manager Follow-Ups

Goal: a windowed installer in place of the `INSTALL.BAT` console tool,
installing, updating and setting every driver of the package on every
target; an uninstall from Add/Remove Programs in one step, as NUSB's;
the driver put on the controller and its root hub by the tool itself on
98 SE and ME; `XHCISNAP` retired into XHCITR; the safe-removal mark kept
across a stack rebuild on NT; the connection-manager follow-ups; released
as `3.1.0.0`.

Status: closed on the `3.1.0.0` cut of 2026-10-20, on branch `3.1.0.0`. Opened 2026-10-18 by the owner from
"Planned work", with the uninstall (42-A.3) added and the phase widened
the same day when the owner went through every planned item; the
decisions table maps the planned ids to its tasks. SweetLow's first tests
of `3.0.0.0`, Stor98's audit and its move into this repository, the
bench and the matrices added 42-A.16 to 42-A.38 from 2026-10-18 to
2026-10-20. On 2026-10-19 and 2026-10-20 the owner moved what this
release does not finish to "Planned - Left by `3.1.0.0`" below, under
planned ids of their own, and reworded the legs to what was read. It
closes with the `3.1.0.0` cut. Its records are `runs/run-42.md`, design
records 26, 27 and 28, and record 21 sections 7.2 and 7.4 and record 22
section 7.7.

- [x] 42-0.1 design record 26, the windowed installer (was INSTUI.1) (done 2026-10-18, draft 7, reviewed until it converged)
- [x] 42-0.2 design record 27, the Windows 98 SE and ME device install through `SETUPX`, built with Open Watcom 2.0 (was DI9X.1) (done 2026-10-18, draft 6, reviewed until it converged)
- [x] 42-0.3 the `XHCISNAP` inventory (was SNAP.1) (done 2026-10-18, owner: every use moved into XHCITR, the counter-name check to a build script)
- [x] 42-0.4 the design of the safe-removal mark across a stack rebuild (was EJRB.1's design) (done 2026-10-18, record 21 section 7.4 revision 3, the mark service `xhcimark`; reviewed until it converged)
- [x] 42-0.5 the design of a USB4 host's recovery after loss (was U4REC.1's design) (done 2026-10-18, record 22 section 7.7, decisions U17 to U29; reviewed until it converged)
- [x] 42-0.6 design record 28, a 9x function with no interrupt (was NOIRQ.1's design) (done 2026-10-18, draft 11, reviewed until it converged; its mechanism refuted on 98 SE, see NOIRQ.1 below)
- [x] 42-0.7 the 9x half of the Stor98 channel, designed (was NV9X.2's and NV9X.3's design) (done 2026-10-18, record 21 section 7.2, "The 9x half"; reviewed until it converged)
- [x] 42-A.1 the 98 SE and ME helper `XHCIDI.EXE` and the installer's use of it (was DI9X.2) (done 2026-10-19, `p42-di` to `b6bdac85`, fast-forwarded; `runs/run-42.md`)
- [x] 42-A.2 the windowed installer (was INSTUI.2), built and gated as `XHCIINST` is (done 2026-10-19, merged 2d2703b9; matrices N1, N2 and N5)
- [x] 42-A.3 the uninstall as NUSB does it, from Add/Remove Programs in one step (owner, 2026-10-18) (done 2026-10-19, merged 2d2703b9; read in QEMU on 98 SE to 7 x64)
- [x] 42-A.4 `XHCISNAP` retired into XHCITR (was SNAP.2 and SNAP.3) (done 2026-10-19, `p42-tr`; record 25 revision 14, section 15)
- [x] 42-A.5 the safe-removal mark across a stack rebuild (was EJRB.1's code) (done 2026-10-19, merged 3d4e8527, 52e41d9a and 6338dc61; matrices N4 and N5)
- [x] 42-A.6 `fresh` only for the member whose tunnel changed (was GEN1.1) (done 2026-10-19, `p42-cm1` to `9e40589c`, merged; `runs/run-42.md`)
- [x] 42-A.7 the query device's first answer after an inert start (was QINR.1) (done 2026-10-19, `p42-cm1`, merged; read on 7 x86, 98 SE owed)
- [x] 42-A.9 a Thunderbolt 3 dock's name after a resume (was TB3NAME.1) (done 2026-10-19, `p42-cm1`, merged; read on 98 SE and 7 x86)
- [x] 42-A.10 recovery of a contained USB4 host (was U4REC.1) (done 2026-10-19, `p42-u4rec` to `9fb22af3`, merged; `runs/run-42.md`)
- [x] 42-A.12 the per-port, non-blocking Stor98 waits (was NV9X.2) (done 2026-10-19, `p42-nvw` to `9e215216`, merged; `runs/run-42.md`)
- [x] 42-A.14 PCI configuration below a tunnel port on 98 SE and ME (was PCI9X.1), as read for `3.1.0.0` (done 2026-10-19, merged b3077e66; 98 SE in QEMU and on the P14s, `runs/run-42.md`; the rest in OWED31.1)
- [x] 42-A.17 the registry values read machine-wide, under `Services\USB`, as well as per controller (SweetLow, 2026-10-18) (done 2026-10-19, `p42-reg`, `7f25ca3c..2b9a2ae2`; `runs/run-42.md`)
- [x] 42-A.18 `XhciFastPollFsLs` 4 takes the Endpoint Context Interval from the client's code (SweetLow, 2026-10-18) (done 2026-10-19, `p42-reg`; `runs/run-42.md`)
- [x] 42-A.19 Stor98's audit of 2026-10-18 brought in (done 2026-10-19, `p42-a19`, merged 398e06e6; `runs/run-42.md`)
- [x] 42-A.20 Stor98 consolidated into this repository as `src/stor98` (owner, 2026-10-19) (done 2026-10-19, `p42-stor98` to `8022beed`, merged; every blob equal to Stor98's `4253b58`)
- [x] 42-A.21 the connection manager alone, the only install on Windows 8 and 8.1 (owner, 2026-10-19) (done 2026-10-19, `p42-cmo`, merged 8b8fad87; `runs/run-42.md`; 8 and 8.1 owed)
- [x] 42-A.22 the installer's pages tidied (owner, 2026-10-19) (done 2026-10-19, `p42-wizgap`, merged 0478f0ae; `runs/run-42.md`)
- [x] 42-A.23 the x64 package's filter INF beside its driver (owner, 2026-10-19) (done 2026-10-19, `p42-x64flat`, merged 200d841b; `runs/run-42.md`)
- [x] 42-A.24 the safe-removal mark kept out of the CriticalDeviceDatabase (matrix 2026-10-19) (done 2026-10-19, `p42-ejrb`, merged 6338dc61 and 94c22687; `runs/run-42.md`)
- [x] 42-A.25 the second USB4 enclosure after a resume on 98 SE and ME (matrix 2026-10-19) (done 2026-10-19, `p42-nvres`, merged 3dd1117d; `runs/run-42.md`)
- [x] 42-A.26 the bundled Stor98 installed only on NVMe drives behind a tunnel (owner, 2026-10-19) (done 2026-10-19, `p42-s98tun`, merged 7ea6d8f3; `runs/run-42.md`)
- [x] 42-A.27 the installer waits for the tree to settle after a parent device's update (matrix 2026-10-19) (done 2026-10-19, `p42-hubsettle`; `runs/run-42.md`)
- [x] 42-A.28 XHCITR on 2000: a USB disk's drive letter and the stopped mark (matrix 2026-10-19) (done 2026-10-19, `p42-trfix`, merged 6e7b1d17; `runs/run-42.md`)
- [x] 42-A.29 `XhciFastPollFsLs` behind the USB page's Advanced... (owner, 2026-10-19) (done 2026-10-19, `p42-fastpoll`; `runs/run-42.md`)
- [x] 42-A.30 one version, one date and one signature for every binary, Stor98 included (owner, 2026-10-19) (done 2026-10-19, `p42-s98ver`, merged be4a421f; `runs/run-42.md`)
- [x] 42-A.31 `SETUP.EXE` at the package root (owner, 2026-10-19) (done 2026-10-19, `p42-batlaunch`, merged d1ac2b45; `runs/run-42.md`)
- [x] 42-A.32 the 42-A.12 follow-up: the acknowledgement waits during back-to-back busy waits, and held work kept through a transport loss on NT (done 2026-10-19, `p42-callfix`, merged; host vectors only)
- [x] 42-A.33 Stor98's 9x bounce copy across scatter/gather fragments (done 2026-10-19, `stor98-spd`, merged 38df05c1; Stor98's `docs/TESTING.md`)
- [x] 42-V.1 the installer's legs, as read for `3.1.0.0` (done 2026-10-20, `E:\xhci98-vm\m31\N1` to `N5` and the final cut's legs; the rest in OWED31.1)
- [x] 42-V.2 the connection-manager legs, as read for `3.1.0.0` (done 2026-10-20, matrices N3 to N5; the rest in OWED31.1)
- [x] 42-V.3 the VM matrix on the `release` flavour, as read for `3.1.0.0` (done 2026-10-20, matrices N1 to N5 and the final cut's legs; the rest in OWED31.1)
- [x] 42-V.7 more than one NVMe drive on 98 SE (was GEN1.2), as read for `3.1.0.0` (done 2026-10-19, `E:\xhci98-vm\m31\v7\RESULT.md`; the rest in GEN1.3)
- [x] 42-V.9 the legs of Stor98's audit fixes (42-A.19), as read for `3.1.0.0` (done 2026-10-20, Stor98's `docs/TESTING.md`; the rest in OWED31.1)
- [x] 42.final `README.md`, the release notes (what `3.1.0.0` adds, "Installing" rewritten around the installer, "Untested ground" naming what is owed), `releases/history.md`, `source-files.md`, `docs/README.md`; the builds, the dry cut, the owner's signed cut of `3.1.0.0` and the install legs from the asset (done 2026-10-20: README, the release notes, `releases/history.md` and the release readme; cut at 354a4403, release commit 051c38d3; the legs on the final cut and the owner's P14s run in `runs/run-42.md`)

Checkpoint: design records 26, 27 and 28 and the sections of 42-0.4,
42-0.5 and 42-0.7 converged before their code; the host suites green
with the gates on both architectures; the 42-V legs read on the cut's
`release` flavour, what wants hardware or an emulator build not at hand
owed and named in the release notes' "Untested ground"; then 42.final's
install legs from the asset.

---

## Phase 43 - Release `3.2.0.0`: the Cut's Checks, Faster Emulator Legs, the Unsafe-Unplug Notice, the 9x Stor98 Channel, and What `3.1.0.0` Owed

Goal: the cut's text checks run without a build and the emulator legs
run faster; NUSB's 98 SE storage files in the release download from a
driver pack kept in a separate repository, offered by the installer, and
three downloads in all; the installer's and the eject's words corrected,
Stor98 named as part of the package; the unsafe-unplug notice on NT, the
"can run faster" notice and a red tray icon; the Stor98 channel's two 9x
transactions, and large requests reaching Stor98 whole on 98 SE and ME;
a refused interface's device id settled; Windows 8, 8.1 and 10 targets
of the connection manager and Stor98, with `xhci98.sys` an opt-in there;
a larger isochronous ring; the idle poll default decided; the
virtual-machine legs `3.1.0.0` left; released as `3.2.0.0`.

Status: closed on the `3.2.0.0` cut of 2026-10-24, on branch `3.2.0.0`.
Opened 2026-10-20 by the owner from "Planned work", going through every
planned item; the decisions table maps the planned ids to its tasks,
which ran in order of ease (owner, the same day). The legs, the owner
and the coordinator added 43.42 to 43.73 from 2026-10-20 to 2026-10-23;
43.41 was deferred as ISORING.4. At the cut what this release does not
finish moved to "Planned - Left by `3.2.0.0`" below, under planned ids
of its own. Its records are `runs/run-43.md` and `runs/run-43-24.md`,
design records 29 and 30, and the sections of records 13, 19, 20, 21,
22, 23, 24, 25, 26 and 27 its items name.

- [x] 43.1 NUSB's 98 SE storage files, the redistribution decision recorded (was NUSBPKG.1's first step; owner, 2026-10-20) (done 2026-10-20, `1ad1e81d` to `294748cf`; `legal-provenance.md` section 5)
- [x] 43.2 the driver pack, `usb-wdm-nonfree` (owner, 2026-10-20; the rest of NUSBPKG.1) (done 2026-10-20, `usb-wdm-nonfree` `456b8ec` to `a703264`; NUSB at its top-level `nusb33/` from 2026-10-22, 43.51)
- [x] 43.3 the pack in the build, at the download's root and in the download alone (owner, 2026-10-20) (done 2026-10-20, `cd8f9cf2` to `7f911f8c`; `NUSB33\` from 2026-10-22, 43.51)
- [x] 43.4 the storage drivers in the installer, a 98 SE row that overwrites nothing and an uninstall that removes what it placed (owner, 2026-10-20) (done 2026-10-20, `22a73177` to `840ac0c7`; record 26 section 6.5, record 27 draft 10, record 23 revision 24)
- [x] 43.42 a silent first plug on 98 SE, the pack's INFs placed as NUSB's own installer places them (43.5's first leg, 2026-10-20) (done 2026-10-22, merged 22ec1372 and 1557226b; the installer simplified on `p43-42s`, merged 060f29b0, `runs/run-43.md` "43.42s"; on 98 SE and ME the controller's `3.1.0.0` copy row restored, `p43-42w` `a85d5693`, its legs passing, "43.42w"; a controller added later installing with no prompt is W9XPROMPT.1)
- [x] 43.43 every Components box on every system, warned where it does not apply (owner, 2026-10-21), as read for `3.2.0.0` (done 2026-10-22, merged 22ec1372 and 1557226b; record 26 section 5.3.1; case 3 read on 8, 8.1 and 10, `runs/run-43-24.md`; the storage row on 98 SE alone again in the simplified flow, record 26 section 5.0; the rest in OWED32.1)
- [x] 43.48 three release downloads, and the installer offering what the one it runs from carries (owner, 2026-10-22), as read for `3.2.0.0` (done 2026-10-23, merged 00fc7945 and 21bef059; the `usb-wdm-free` pin v0.1.0, `p43-pin2`, `6b992660`; `runs/run-43.md`, "43.48"; the rest in OWED32.1)
- [x] 43.44 an ICM-announced router's generation read from the router, not assumed (owner, 2026-10-21, a Framework 13 with the ORICO enclosure), as read for `3.2.0.0` (done 2026-10-22, `c770790b` to `dd46948c`; record 21 section 5.3, record 24 revision 7; host vectors in `test_tbt_icm`; the Alpine Ridge client showing "USB4 v1.0" in the emulator, 43.55; the rest in ICMGEN.1)
- [x] 43.45 the software connection manager on the emulator's `amd` host model (owner, 2026-10-21) (done 2026-10-22; `runs/run-43.md`, "43.45"; the 2000 and 98 SE resumes in OWED32.1 and the release notes' "Untested ground", its two XHCITR findings 43.52)
- [x] 43.46 large requests reaching Stor98 whole on 98 SE and ME, through this package's own VSD, `S98VSD.VXD` (owner, 2026-10-21) (done 2026-10-23, design record 30, merged 2502146c; `runs/run-43.md`, "43.46 read first", the P14s reading and record 30's legs)
- [x] 43.49 Windows ME stops after rapid back-to-back USB mouse plugs (43.13's ME legs, 2026-10-22) (done 2026-10-22, `b6e7fd4a`: ME's own `hidusb.sys`, no driver change, a known limitation in the release notes, owner; `runs/run-43.md`, "43.49")
- [x] 43.50 XHCIPERF, a disk benchmark with a window (owner, 2026-10-22) (done as scoped 2026-10-23, design record 29; not shipped, kept in the tree as a developer diagnostic, owner 2026-10-23; the rest in PERF.1)
- [x] 43.51 NUSB kept apart from the non-free pack, and the storage row unticked while NUSB is installed (owner, 2026-10-22) (done 2026-10-22, `d64db54b` and `bc0bdcae`, `usb-wdm-nonfree` `f1e0c4e`; record 23 revision 25, record 26 draft 19; the 98 SE leg with NUSB in `runs/run-43.md`, "43.42s")
- [x] 43.52 two XHCITR defects, the unelevated eject's tunnel line and the balloon after a sleep (43.45's legs, 2026-10-22) (done 2026-10-23, `xhcitr-fixes`; record 25 sections 6.2, 6.3 and 7.2; `runs/run-43.md`, "43.52")
- [x] 43.53 a request to a departed device that never completes no longer resets the controller (43.49's 98 SE legs, 2026-10-22), as read for `3.2.0.0` (done 2026-10-23, `27b641c3` to `0a2188f5`, merged 8e331857; record 13 section 10.5.1; 189 cycles on 98 SE with no reset, `runs/run-43.md`, "43.53"; the rest in DEPART.1)
- [x] 43.59 Stor98 or Windows' `stornvme` on a tunnelled NVMe drive on 8.1 and 10 (owner, 2026-10-22; decided 2026-10-23: Stor98 the default on 8.1 alone, `stornvme` on 10 with Stor98 an opt-in), as read for `3.2.0.0` (done 2026-10-23, `p43-59k`, merged 15bdc250; record 21 section 7.5; `runs/run-43.md`, "43.59" and "43.59s"; the rest in NVME81.1)
- [x] 43.5 the storage drivers on 98 SE in QEMU, on a fresh image built from the CD (was NUSBPKG.1's legs), as read for `3.2.0.0` (done 2026-10-23, `p43-5`, merged 97e4ad4a; `runs/run-43.md`, "43.5" and "43.42s"; 43.2's sufficiency claim made from it; the rest in OWED32.1 and HW32.1)
- [x] 43.6 `XhciSilentInstall`'s note without the warning (was SILENT.2) (done 2026-10-22, `9332549d`, merged 77384e93)
- [x] 43.7 Stor98 named as part of this package, not "a separate project" (was S98NAME.1) (done 2026-10-22, `9332549d`, merged 77384e93)
- [x] 43.8 the words after a successful eject (was EJTEXT.1) (done 2026-10-22, `9332549d` and `af8a5025`, merged 77384e93; record 24 revision 7, record 25 section 6.2)
- [x] 43.9 the cut's checks before the build, and a documentation commit that costs no rebuild (was PRECUT.1) (done 2026-10-22, `6cb08b22` to `a168fee3`; `releases/README.md`, `build-and-test.md`)
- [x] 43.10 the installer's settings pages laid out to fit their text (owner, 2026-10-20) (done 2026-10-22, `p43-10`, `484e3157` to `a844d657`; record 26 draft 21 section 5.4)
- [x] 43.11 the settings pages on 98 SE at 640x480, XP and 7 at 96 DPI and 7 at 120 DPI (done 2026-10-22; `runs/run-43.md`, "43.11")
- [x] 43.12 faster emulator legs, every guest on TCG (was EMUFAST.1) (done 2026-10-22; `build-and-test.md`, "Faster emulator legs: starting points and the release matrix"; ticked by the owner with the window legs, Vista, 7, ME and 2000 owed, in OWED32.1)
- [x] 43.13 the idle poll period, 50 against 500 ms (was IDLE.1) (done 2026-10-22: the default stays 50, owner; `runs/run-43.md`, "43.13")
- [x] 43.14 a refused interface's device id on 98 SE and ME, read first (was NOX.1's reading) (done 2026-10-22: a VID/PID line binds it; `runs/run-43.md`, "43.14")
- [x] 43.15 a refused interface's device id in the `XHCI98_NOXPORT` form (was NOX.1's fix) (done 2026-10-22, `66840a9e` and `eb7d0575`; record 13 section 10.7.1)
- [x] 43.16 NOX.1's legs on 98 SE, ME and 2000 (done 2026-10-22; `runs/run-43.md`, "43.16")
- [x] 43.17 the virtual-machine part of OWED31.1 (earlier 42-V.12), as read for `3.2.0.0` (done 2026-10-23, `p43-5`: 42-A.14 and U.11 on ME, U.11 on 98 SE, the installer on XP x64; `runs/run-43.md`, "43.5"; the rest in OWED32.1)
- [x] 43.18 the Stor98 channel's two 9x transactions (was NV9X.3) (done 2026-10-22, `p43-18`, merged 31571b94; record 21 section 7.2; the gate-G3 readings in `runs/run-43.md`, "43.18")
- [x] 43.19 the Stor98 channel on 98 SE and ME (was NV9X.4, but its NOIRQ.1 leg) (done 2026-10-22, `p43-18` and `p43-19`, `e42e2f28`; `runs/run-43.md`, "43.19")
- [x] 43.47 the vanished-child transaction on 98 SE and ME (record 21 section 7.2 part 3; owner, 2026-10-22) (done 2026-10-23, `p43-47`, merged bc6fefe4; 43.47f's backed-off query, merged b47a89f8 2026-10-24, a mitigation, the stall at the holder's release a known limitation in the release notes; `runs/run-43.md`, "43.47" and "43.47r")
- [x] 43.20 the virtual-machine part of GEN1.3, as read for `3.2.0.0` (done 2026-10-23, `p43-20`, merged 04e55571: every leg on ME, and on 98 SE but the resume; `runs/run-43.md`, "43.20"; the rest in GEN1.4)
- [x] 43.21 the unsafe-unplug notice on NT (was UNPL.1) (done 2026-10-22, the gated build of `b03d4422`; record 25 section 7.6; leg T.11, `runs/run-43.md`, "43.21")
- [x] 43.22 Windows 8 and 8.1, x86 and x64, a target of the connection manager (owner, 2026-10-20) (done 2026-10-22, `p43-22`; record 23 section 5.10 revision 30; `runs/run-43.md`, "43.22"; 8.1 x86 read by 43.24)
- [x] 43.23 Stor98 on Windows 8 and 8.1, and 10 below build 22000, x86 and x64 (owner, 2026-10-20) (done 2026-10-23, `p43-23`, merged 58bc4e39; x64 after 43.36's catalogs and 43.59s; `runs/run-43.md`, "43.23", and `runs/run-43-24.md`)
- [x] 43.24 the Windows 8 and 8.1 legs, x86 and x64 (43.22), as read for `3.2.0.0` (done 2026-10-24, `p43-24`, `6a4a478b` and `23b125ec`; `runs/run-43-24.md`; the resume's bug check 0x9F fixed with no D0 hold on any NT, `p43-24n`, `e2c0a877` and `a72649e3`, merged c8ae8063 and f5998096, passing on 8 x64, 8.1 x64 and 10 x64; the rest in HIBER.1, OWED32.1 and INTELTB.1)
- [x] 43.25 "Device can perform faster when connected to USB 3.0", read first (a Windows 8 user's report, owner, 2026-10-20), as read for `3.2.0.0` (done 2026-10-22, `p43-25`, merged af9aeac1: the static half and the Windows 7 legs, record 13 section 6.12.11; the rest in W8USB.1)
- [x] 43.26 `xhci98.sys` on Windows 8 and 8.1, read first (done 2026-10-22, `p43-26`, merged 84c97835; record 13 section 6.12; its running readings by 43.31)
- [x] 43.27 the "can run faster" tray notice in XHCITR, on by default on every target (owner, 2026-10-20 and 2026-10-22) (done 2026-10-23, `p43-27`, merged a8d8a3bb; record 25 section 7.7, record 13 section 8.4.1; the legs 43.28)
- [x] 43.28 the tray notice's legs on 98 SE, ME, 2000, XP and 7 x86 and x64 (done 2026-10-23, `p43-28`; `runs/run-43.md`, "43.28"; a hub and a SuperSpeedPlus device in HW32.1, the two-controller 8 guest in W8USB.1)
- [x] 43.29 `xhci98.sys` an opt-in install on Windows 8 and 8.1 (owner, 2026-10-20) (done 2026-10-22, `p43-29`, merged 19170de8; record 13 section 6.13; the legs 43.31)
- [x] 43.30 the items the owner picked from 43.25's list (owner, 2026-10-22), as read for `3.2.0.0` (done 2026-10-23: the notice with 43.27; Windows 8's client contract and `0x220FB3` as `RESET_PORT` with 43.29, record 13 sections 6.12.10 and 6.13; the rest in W8USB.1)
- [x] 43.31 `xhci98.sys` on Windows 8 and 8.1 (43.29, 43.30), on 43.24's VMs, as read for `3.2.0.0` (done 2026-10-24, `p43-24`; `runs/run-43-24.md`, "43.31"; the rest in W8USB.1)
- [x] 43.32 Windows 10 virtual machines, x86 and x64 (owner, 2026-10-20) (done 2026-10-22, `p43-33`, `3cd11a34`; `build-and-test.md`, "Windows 10 target VMs (roadmap-usb4 43.32)"; the OVMF guest with Secure Boot on not built, owner 2026-10-22, in SECBOOT.1)
- [x] 43.33 Windows 10, read first, as read for `3.2.0.0` (done 2026-10-22, `p43-33`, merged 784094d3; record 13 section 6.14, records 21 and 23; `runs/run-43.md`, "43.33"; Intel's Thunderbolt driver read statically only, in INTELTB.1, and (d)'s open parts in SECBOOT.1)
- [x] 43.34 Windows 10, x86 and x64, a target of the connection manager (owner, 2026-10-20), as read for `3.2.0.0` (done 2026-10-22, `p43-34`, merged 2038250e: the version rule and its vectors, record 23 sections 4.3 and 4.5 revision 28, record 21 section 3.5; Stor98 on 10 by 43.23; the legs 43.37; the rest in INTELTB.1)
- [x] 43.35 `xhci98.sys` an opt-in install on Windows 10, x86 and x64 (owner, 2026-10-20), as read for `3.2.0.0` (done 2026-10-22, `p43-34`, merged 2038250e: the row on 10 as 8.1's; the tray default settled by 43.27; the legs 43.37; the rest in W10USB.1)
- [x] 43.36 the installer's and the documents' Windows 10 text, and the signature (owner, 2026-10-20), as read for `3.2.0.0` (done 2026-10-22, `p43-34` and `p43-36s`, merged 2038250e and 25e7dd6a: the texts, README and the release notes, a test-signed catalog per x64 INF, design record 19 section 3a; Stor98's `DmaRemappingCompatible` withdrawn 2026-10-23, 43.59s; the rest in SECBOOT.1 and W10USB.1)
- [x] 43.37 the Windows 10 legs, x86 and x64 (43.34 to 43.36), as read for `3.2.0.0` (done 2026-10-24, `p43-24`; `runs/run-43-24.md`, "43.37"; the uninstall in one run on 8.1 x64 and 10 x64, `p43-42v`, merged 1705760b, `runs/run-43.md` "43.42v"; the rest in W10USB.1, SECBOOT.1 and INTELTB.1)
- [x] 43.38 the isochronous ring enlarged, the design (was ISORING.1; owner, 2026-10-20) (done 2026-10-20, `a3ddbb2c` to `387f53bc`; record 13 section 11.8)
- [x] 43.39 the isochronous ring, the code and host vectors (was ISORING.2) (done 2026-10-22, `8fe52bf2` and `3d8a3ae6`; record 20's `XHCI_AVOID_BEI` row rewritten)
- [x] 43.40 the isochronous ring, the legs (was ISORING.3) (done 2026-10-22, the `qemu` build on 2000, XP, 7 x64 and 98 SE; a High Speed stream, a camera and the interrupt saving in HW32.1 and the release notes' "Untested ground")
- [x] 43.54 why Windows ME starts `xhci98.sys`'s device PDOs later than its own stack's (owner, 2026-10-22) (done 2026-10-22: it does not, no driver change, owner; `runs/run-43.md`, "43.54")
- [x] 43.55 the tray's own balloon on 98 SE (owner, 2026-10-22) (done 2026-10-22, the gated `qemu` build of `fa458783`'s tree; record 25 revision 17, section 16; leg T.12, ME the control)
- [x] 43.57 the tray icon red while something is wrong that the user can put right (owner, 2026-10-22), as read for `3.2.0.0` (done 2026-10-23, `p43-57`, merged ca8ea257; record 25 revision 19, section 7.2.1; read on 7 x86 and 98 SE, `runs/run-43.md`, "43.57"; the rest of T.14 in OWED32.1); its approval and controller cases, and that a plug, eject, unplug and hibernate leave it blue, read at the cut on 7 x86 and 98 SE (`p43-gui`)
- [x] 43.60 isochronous transfers and the Logitech Brio 100 (coordinator, owner-approved 2026-10-22) (done 2026-10-23: none of the three parts this driver's; 98 SE's freeze at a first USB audio device documented as a known limitation, not fixed, owner 2026-10-22; `runs/run-43.md`, "43.60")
- [x] 43.61 the tool readmes held to the cut's text rule on every gated build (43.11's test cut, 2026-10-22) (done 2026-10-22, `e547cd30` and `scripts/check-readme-text.ps1`; `runs/run-43.md`, "43.61")
- [x] 43.62 98 SE's and ME's "Unplug or Eject Hardware" with `xhci98.sys`, read (owner, 2026-10-22) (done 2026-10-22, merged 05894296: no driver change, owner; the icon comes with NUSB 3.3 itself, in README and the release notes; `runs/run-43.md`, "43.62")
- [x] 43.63 two XHCITR texts 43.19's legs found (owner, 2026-10-22) (done 2026-10-23, `p43-63`, `7b677a7a` to `9216afa2`; record 25 sections 3.6, 4.7 and 6.2)
- [x] 43.64 a cancelled bulk TD behind the head, read against the xHCI specification and QEMU (coordinator, 2026-10-23) (done 2026-10-23, `e439e392` and `a719083c`; record 13 section 14; QEMU's fix the emulator's)
- [x] 43.65 Windows ME's first shutdown after `xhcicm.sys` was started (coordinator, 2026-10-23) (done 2026-10-23, `p43-65`, `2d65409a` and `c91940ec`; record 21 section 5.6; `runs/run-43.md`, "43.65")
- [x] 43.66 a "can run faster" balloon the shell did not show, sent again once (coordinator, 2026-10-23, from 43.28) (done 2026-10-23, `p43-66`, `9063ce5b` to `bcdbe4b4`; record 25 section 7.7 item 9; `runs/run-43.md`, "43.66")
- [x] 43.67 a configuration of more than four endpoints refused (coordinator, 2026-10-23, from `usb-wdm-free`'s xpad98 legs) (done 2026-10-23, `p43-67`: `XHCI_POOL_DEVICE_RINGS` 16; `runs/run-43.md`, "43.67")
- [x] 43.68 the "can run faster" balloon's click on Windows 2000 (coordinator, 2026-10-23, from 43.28) (done 2026-10-23, `p43-68`, `046a8996`; record 25 section 7.7 item 6; `runs/run-43.md`, "43.68")
- [x] 43.69 the stopped TD read from the controller's saved dequeue (owner-approved 2026-10-23, from 43.64) (done 2026-10-23, `p43-69`; record 13 section 14; `runs/run-43.md`, "43.69"; the 98 SE fault-rate comparison in OWED32.1)
- [x] 43.70 the elevated `XHCIINST /eject` left the PCIe tunnel up under native PCIe hot-plug (43.52's legs, 2026-10-23; release-blocking, owner) (done 2026-10-23; record 21 section 7.4; `runs/run-43.md`, "43.70")
- [x] 43.71 a file lost across hibernation on a native hot-plug tunnel port (43.52's legs, 2026-10-23) (done 2026-10-23, read: a known limitation in the release notes; its second cause RESUME.1; `runs/run-43.md`, "43.71")
- [x] 43.72 two tray defects, 98 SE's tooltip and the balloon a click is on (owner, 2026-10-23, release-blocking) (done 2026-10-23, `p43-72`; record 25 revision 20; leg T.15, `runs/run-43.md`, "43.72")
- [x] 43.73 the controllers named with maker and product in Device Manager (owner, 2026-10-23) (done 2026-10-23; record 23 section 5.13, record 21 section 3.3; `runs/run-43.md`, "43.73")
- [x] 43.74 the 98 SE and ME installer asks whether a hardware wizard is open once per run, then looks for a wizard window itself before each later pass (was W9XGATE.1; the owner's bench, 2026-10-24) (done 2026-10-24, `p43-rc2` `1c64d55f`; the 98 SE and ME console updates asked once; the wizard-open case by host vectors only; record 27 section 6)
- [x] 43.75 the Thunderbolt driver's comments and record 22's text that still described the removed bridge hold, rewritten (was CMCOMMENTS.1; found at the cut) (done 2026-10-24, `p43-cmc` `b9142c9e`; comments and record text only, every changed source code-identical with comments stripped)
- [x] 43.76 XHCITR's window opened from the red icon no longer starts with its tree scrolled right (was TRSCROLL.1; found at the cut's red-icon legs) (done 2026-10-24, `p43-trscroll` `6fa7cf6b`; seen in no guest yet, the recut's red-icon legs read it)
- [x] 43.79 an unbuffered read or write whose buffer was not at a 4-byte boundary silently corrupted on 98 SE and ME with `S98VSD.VXD` (the 3.2.0.0 matrix's N3 leg, 2026-10-24; data integrity, release-blocking): IOS's double buffer arrived with `IORF_PHYS_SGDS` and a list holding a linear address, which the layer passed down and Stor98 was given as physical, both directions, foreign memory read and written (done 2026-10-24, `p43-unal`, `7abba43a` to `a7d37b9a`: every request with data gets the layer's own list; record 30 sections 4.1, 4.6 and 9.2 item 1; host vectors in `test_vsd_list` and Stor98's `sg9x_ut`; 98 SE and ME at offsets 1, 2, 3, 511, 4094, 4095 and aligned, 512 B to 2 MiB, both directions, the `qemu` and release images, every compare 0; no real-hardware reading at an unaligned buffer)
- [ ] 43.final `README.md`, the release notes, `releases/history.md`, `source-files.md`, `docs/README.md`; the builds, the dry cut, the owner's signed cut of `3.2.0.0` and the install legs from the asset (done 2026-10-24: README, `1a5336b5` and `2e9ba4f0`; the release notes and `releases/history.md`, `cc44068f`, merged 31802ccb; the version, `4579d757`; the builds, the dry cut and the owner's signed cut of `3.2.0.0`; the install legs from the asset status unverified)

Checkpoint: 43.14 read before 43.15 was written, 43.25 before 43.27,
43.25 and 43.26 before 43.29 and 43.30, and 43.33 before 43.34 to
43.36; 43.1 written before the cut shipped the pack's files, each
design section converged before its code, and 43.5's clean-image
reading before 43.2's sufficiency claim; the host suites green with the
gates on both architectures; the legs read in the emulator, what wants
hardware, an image or an emulator model not at hand owed under
"Planned - Left by `3.2.0.0`" and named in the release notes'
"Untested ground"; then the owner's signed cut of `3.2.0.0`.

---

# Planned work

Not phases yet: each takes the next free phase number when the owner opens
it, and its placeholder ids take that number then (owner, 2026-10-06; the
decisions table).

## Planned - Left by `3.1.0.0`

Goal: the Phase 42 items the owner moved out of `3.1.0.0` on 2026-10-19 and 2026-10-20, each with what it leaves as it stands: the unsafe-unplug notice is not shown (UNPL.1, as in `3.0.0.0`); the 9x eject of a tunnelled NVMe calls Windows' remove itself, with no Stor98 acknowledgement (NV9X.3); a 9x function with no interrupt stays at Code 29 unless attached at power-on (NOIRQ.1, in the release notes); XHCITR's Safely Remove of a USB stick may stop every port on the owner's P14s (SRSTOP.1: not reproduced in QEMU; `3.1.0.0` logs every stop, and the owner's Save Report from the bench decides the fix); the idle poll default stays (IDLE.1); the emulator legs wait on `usb4-emu` part 2 (EMU.2 to EMU.5).

Status: not opened, unnumbered and unversioned. On 2026-10-20 (owner) the items took planned ids of their own, the earlier one where they had one; each keeps its 42 id as "was", which the release notes, the design records and `runs/run-42.md` cite. Later the same day the owner moved SILENT.2, S98NAME.1, EJTEXT.1, UNPL.1, NV9X.3, IDLE.1, PRECUT.1, EMUFAST.1, most of NV9X.4, and the virtual-machine parts of GEN1.3 and OWED31.1 into Phase 43, and NUSBPKG.1 whole (43.1 to 43.5); what is left of each stays below.

### The installer and the tools

- [ ] W9XPROMPT.1 A USB 3 controller added after the install on 98 SE and ME installs with no Insert Disk prompt, and an Update Driver from Device Manager still replaces `xhci98.sys` (owner, 2026-10-24; moved out of `3.2.0.0`, which ships `3.1.0.0`'s copy behaviour: the wizard asks once for the "xhci98 xHCI Host Controller Disk" and the install directory answers it; design record 26 section 5.0, drafts 39 and 40). What the 98 SE legs of 2026-10-23 and 24 read, with `xhci98.inf` found on `DevicePath` (`C:\XHCI98`): the wizard looks for `xhci98.sys` at Windows' own setup source (`Setup\SourcePath`, `C:\WIN98`), not beside the INF; the temporary-name row and a plain row both ask with the file already in `SYSTEM32\DRIVERS`, while `xhciuas.inf`'s plain row does not (`xhciuas.inf` has no `LayoutFile`; `xhci98.inf` has one, which may be the difference, not tested alone); `COPYFLG_NO_OVERWRITE` (16) spares the prompt but makes Update Driver keep an older file (observed), and 64 and 96 ask; a fourth field in `[SourceDisksNames]` (`..\XHCI98`, `\XHCI98` or `C:\XHCI98`, the source path relative to or instead of Windows' source) let the wizard finish with no prompt in a first reading (2026-10-24, the copy's source not confirmed, a single run each, and only for an install directory at `C:\XHCI98`, so a per-machine path or a fixed source folder would be needed). Two helper investigations (the INF's source description; an OEM INF pre-installed into `INF\OTHER` with a recorded source) were stopped early by the owner's decision. To do: confirm the fourth-field reading with the file deleted and its source logged, try an LDID in that field (`%24%`, `%10%`) and a fixed source folder of the package's own, then legs on 98 SE and ME: a fresh install with the controller added (no prompt), an Update Driver from an earlier release and from the same one (the file replaced), `INSTALL.BAT`'s update, the uninstall
- [ ] SRSTOP.1 XHCITR's Safely Remove of a plain USB device stops every USB port (was 42-A.16; owner, 2026-10-18): after the tool's stop of a USB device that is not a Thunderbolt unit (record 25 section 6.5: `CM_Request_Device_EjectA` on the device's devnode on NT, `CM_Query_Remove_SubTree` then `CM_Remove_SubTree` on 98 SE and ME), every port of the controller ends stopped, not the one device; seen by the owner on the ThinkPad P14s Gen 1 under Windows 98 SE, with a USB stick. To investigate, with no cause written into the record until it is read: the device it was seen on, the devnode the call was made on against the one confirmed (the device's, never its hub's, root hub's or controller's), what the bus did with the query-remove and the remove it received (`hcd_pdo.c`'s handling of `IRP_MN_QUERY_REMOVE_DEVICE`, `IRP_MN_REMOVE_DEVICE` and the removal relations it answers, `hcd_hub.c` and `hcd_rh.c` for a hub or root hub drawn into the relations), the driver's log and the counters around the stop (`XHCITR`'s Save Report), and whether `XHCIINST /eject` and Windows' own Safely Remove applet on the same device do the same; then the fix, in the tool or in the bus, with a host vector where the pure half decides and a regression leg on 98 SE and 2000
### The connection manager

- [ ] RESUME.1 a tunnel this driver built, adopted as preserved after a PnP stop and start of the NHI (found by 43.71, 2026-10-23, on 7 x86 in the emulator; `runs/run-43.md`, "43.71"; owner, 2026-10-23: after the release): with the enclosure attached at boot, the first instance builds the PCIe tunnel, Windows stops and starts the NHI once (a rebalance), and the new instance finds the tunnel up and adopts it as preserved (`u4.preserved`). Across a hibernate the domain loses its configuration, and design record 22 section 7.6 rebuilds only a port this driver built, so the resume fails (`u4.resume.held`, `cm.reenum.resumed` 0) and the disk is reported missing, on every target, the SHPC ones included, where the resume hold otherwise orders the resume. The change is record 22's: what counts as built here across a PnP restart of the same NHI (the preserved/BuiltHere rule of section 7.6), then the code, host vectors and a hibernate leg
- [ ] DUALMODE.1 a dual-mode NVMe enclosure that comes up as USB (owner, 2026-10-20, the P14s Gen 1 under 98 SE): an ASM2464 enclosure such as the ORICO falls back to USB 3 (a UAS disk on `xhciuas.sys`) when its Thunderbolt or USB4 link is not up in time, and needs a replug or two to tunnel; N5 saw the same on 7 x64 after a restart with the enclosure attached. Read on the bench how long the Alpine Ridge controller takes to wake and `xhcicm.sys` to approve after a plug, and whether waking it sooner or approving faster wins the race; the known limitation stays in README until then
- [ ] NOIRQ.1 a 9x function with no interrupt, started (was 42-A.15; its design, design record 28, was 42-0.6): what design record 28 decides, coded in `xhcicm.sys`'s 9x bus numbering (`tbt_brnum.c`) or wherever the record puts it; host vectors (code merged 2026-10-19 from `p42-9x`, `a742e405`, reviewed until it converged and built, as an interim on `cm_cfgwk.c`'s single worker; NOT done: on 98 SE the emulator refuted record 28's mechanism - the Code 29 NVMe's only basic configuration from PCI.VXD already holds one memory descriptor and no IRQ, so the code refuses it as "configuration" and the drive stays at Code 29; record 28 opens with the dated note and the open question its redesign starts from, whether `Reprocess_DevNode` or `Setup_DevNode(READY)` clears a Code 29 PCI.VXD set on a memory-only configuration and where PCI.VXD sets it; the code stays, safe, its log and `XhciCmNoIrqDiag` holding the evidence; moving it onto 42-A.12's call workers follows the redesign)

### Stor98 on real hardware

- [ ] NVHW.1 the Stor98 link-down statement on real hardware (was 42-A.11; owner, 2026-10-18: Stor98's hardware qualification, its MD-2 on the P14s with an NVMe enclosure and with one that has an internal switch, is driven from here as well): once MD-2 is qualified, the statement's hardware condition per profile - the tunnel port's Data Link Layer Link Active bit where a PCI Express capability exists, the USB4 profile's path-closed and reset anchor - written into records 21 and 22, the withholding lifted per generation that was read; the release notes' state of the pair updated
- [ ] NVHW.2 the Stor98 statement on real hardware (was 42-V.8; NVHW.1) on the bench, per generation read, after Stor98's MD-2; owed after the cut (owner, 2026-10-18) and named in the release notes' "Untested ground"

### Validation and tooling

- [ ] EMU.2 the emulator's R32 power-off (was part of 42-V.5), on the current `usb4-emu` build and its root hot-plug modes once part 2 lands: the R32 power-off read again with `acpi` on 2000 with ACPI, XP and 7 and `restore` on 98 SE (the ICM client's ABSENT and recovery, and the USB4 profile's containment and 42-A.10's recovery, against a PnP removal and a restored bridge); and 42-A.10's recovery on `usb4-generic` on 98 SE, 2000 and one later NT target
- [ ] EMU.3 the P14s behaviour on the emulator (was part of 42-V.5), on the current `usb4-emu` build and its root hot-plug modes once part 2 lands: the P14s behaviour on the Alpine Ridge profile (Assist decided at power-on, pre-boot tunnelling every device, Assist with pre-boot and a USB4 device at single lane with flags `0x88` and configuration requests held about 2.77 s after the first `driver ready`, `driver ready` flags `0x22`, the power-off delays, pending approvals cancelled by an unplug, a re-plug or mailbox `0x06`)
- [ ] EMU.4 the ORICO enclosure on the emulator (was part of 42-V.5), on the current `usb4-emu` build and its root hot-plug modes once part 2 lands: the ORICO enclosure's ASM2464 on the Alpine Ridge profile (approved and tunnelled, and with `XhciCmApprove` 0 its fall back to USB after about 9.4 s, the disk then on the controller's own xHCI)
- [ ] EMU.5 the Alpine Ridge legs on the emulator (was part of 42-V.5), on the current `usb4-emu` build and its root hot-plug modes once part 2 lands: the `39-V` Alpine Ridge legs read again
- [ ] NV9X.4 NOIRQ.1 on 98 SE and ME with the IRQ withheld (the rest of NV9X.4 is 43.19)
- [ ] GEN1.3 more than one NVMe drive on real two-port hardware where the owner has it (the virtual-machine part is 43.20)
- [ ] OWED31.1 42-A.14 on the P14s with a switch chain (was 42-V.12; owner, 2026-10-20; the virtual-machine part is 43.17)
- [ ] PERF.1 XHCIPERF after `3.2.0.0` (was 43.50's open items; owner, 2026-10-23), before it can be shipped: the clipped Start and Stop buttons the owner saw on 98 SE on the P14s at its high resolution (the emulator at 640x480 showed a full-width estimate line drawn over them, all but their top line: read the cause there first); ME's second study run with S98VSD.VXD finding no Stor98 disk ("not reported by this disk's driver"); the legs not read - ME's P.1, P.2, P.7 and P.8, P.3, P.4 and P.6 on 2000, XP, 7 and 10, a Quick run on 8 and 8.1 (record 29 section 14); and the two 28 s timed steps on the emulator's IDE disk

## Planned - Left by `3.2.0.0`

Goal: what the Phase 43 items leave after the `3.2.0.0` cut, each where
it stands in that release: the hibernate fix read on 8 x64, 8.1 x64 and
10 x64 only; the readings that want real hardware, an image or an
emulator model not at hand; and the decisions still the owner's.

Status: not opened, unnumbered and unversioned. Listed on 2026-10-24,
at the `3.2.0.0` cut, from what each Phase 43 item left; each names the
43 item it comes from, whose run sheet section has the detail.
W9XPROMPT.1, moved out of `3.2.0.0` the same day, is under "Planned -
Left by `3.1.0.0`" above.

### The bus and the connection manager

- [ ] HIBER.1 the hibernate fix re-checked on every NT (the rest of 43.24's resume; `p43-24n`, `e2c0a877` and `a72649e3`: no D0 hold on any NT): read on 8 x64, 8.1 x64 and 10 x64; owed, the re-check legs on the other NT targets, 2000 to 7 and the x86 halves of 8, 8.1 and 10, a resume with the enclosure attached and its drive back; also, on XP x64 and 7 x64 (emulator), the first shutdown after a hibernation resume with a tunnelled NVMe drive attached stalls at "Shutting down" for over ten minutes (XP x64 finished after twelve; 7 x64 quit after fifteen; on 7, package and drive together, neither alone, and Safely Remove first avoids it; 8 x64 and 10 x64 do not stall), the NHI's shutdown path not reached (dump, `runs/run-43-24.md`); documented with the unsupported hibernation case
- [ ] ICMGEN.1 the rest of 43.44: the generation read's legs on the emulator's ICM rows on 2000, 7 and 98 SE with a USB4 and a Thunderbolt 3 device router (the Ice Lake row once `usb4-emu` models it), and the owner's Framework 13 with the ORICO enclosure reading USB4 Version major 1, written into the release notes
- [ ] INTELTB.1 Intel's Thunderbolt software and driver beside this package (the rest of 43.24, 43.33 (c), 43.34 and 43.37): read in a guest once an image is at hand (read statically only, record 21 section 3.6), the owner's decision whether the installer leaves it alone, asks or replaces it, the uninstall giving the NHI back to it, and the install over it on 8.1 and 10
- [ ] DEPART.1 the rest of 43.53: whether real hardware reaches a never-completed request to a departed device (a device pulled mid-enumeration at a root port and behind a hub); and, a low-priority later idea (owner, 2026-10-22), a departed device's PDO start answered on 9x so that ME's `hidusb.sys` takes the counted path (record 13 sections 5.2 and 10.5)
- [ ] STALL47.1 the cause of 43.47's connection-manager thread stall on 98 SE and ME, which `3.2.0.0` ships as a mitigation (the backoff) and a known limitation; the marker run was inconclusive
- [ ] UN9XLEFT.1 the 98 SE uninstall's leftovers (the `3.2.0.0` matrix, 2026-10-24): SetupX's `OEM Name` records for `USBSTOR.INF` and `USBNTMAP.INF` stay after their files are deleted (the records of `xhci98.inf` and `xhciuas.inf` are removed), and phantom disk-drive device nodes under `Enum\SCSI` stay after their USB parents are removed; harmless, cosmetic
- [ ] TRVER.1 XHCITR warns when the running driver's version differs from the installed files' (an update not yet restarted into)

### Windows 8, 8.1 and 10

- [ ] W8USB.1 the rest of 43.25, 43.30 and 43.31 on 8 and 8.1 under `xhci98.sys`: UAS under Windows' own `uaspstor` (QEMU's `usb-uas` fails there, as on 7), Stand by, the "can perform faster" line beside Windows' own driver on the same port, and 43.28's guest with two controllers, one under each driver
- [ ] W10USB.1 the rest of 43.35, 43.36 and 43.37 on 10: the USB drivers' gains-and-losses note, the owner's picks from 43.33's list, Sleep, UAS, the "can perform faster" notice, and 43.36's case 3 warning on the USB drivers' and Stor98's boxes lifted or kept (the owner's)
- [ ] SECBOOT.1 the rest of 43.32, 43.33 (d), 43.36 and 43.37: the installer reading Secure Boot's state (record 26 section 5.3.2, proposed), whether Secure Boot on refuses the x86 drivers on 10 and whether memory integrity refuses the drivers, and the OVMF guest with Secure Boot on for the note's text and the refused load
- [ ] NVME81.1 the rest of 43.59: on 8.1 the installer's own per-drive install where the hardware id is the tunnelled drive's own (case 3b), a real 8.1 machine, and whether a later 8.1 update's `stornvme` mends the hang at an unplug

### Validation

- [ ] GEN1.4 the rest of 43.20: the 98 SE resume with two enclosures on a two-port host, after a one-port diagnostic on the P14s (the run sheet's steps), unless the owner substitutes or defers it
- [ ] OWED32.1 the virtual-machine legs `3.2.0.0` left: 43.5's package copy cut between the two INFs; 43.12's starting points and window legs on Vista, 7, ME and 2000; 43.17's full matrix on every target, the USB4 eject behind ACPI's bus filter on 2000 to Vista and Stor98 `docs/TESTING.md`'s two rows not yet read; 43.24's USB device behind a dock on 8 and 8.1 (the emulator's dock wants `q35`); 43.43's case 2 legs, the connection manager alone on 98 SE and ME (status unverified); 43.45's resume on 2000; 43.48's legs on ME, 2000, 7 and the x64 systems and a plug on XP; 43.57's T.14 (ME, 2000, XP, 7 x64, the waiting and failed-controller conditions, a sleep, an Explorer restart, 16 colours); 43.69's 98 SE mid-copy fault rate, `3.1.0.0` against `3.2.0.0`
- [ ] HW32.1 the real-hardware checks `3.2.0.0` left: 43.5's UAS disk behind a High Speed hub; 43.28's stick behind a High Speed hub and a SuperSpeedPlus device at 5 Gbit/s; 43.40's High Speed isochronous stream, a camera and the interrupt saving; 43.60's Brio 100 under 98 SE
- [ ] TREESPLIT.1 the cut's gated build on two worktrees at one commit, x86 and amd64 at once, with `make-release.ps1` and `-SignOnly` accepting both trees' gated records (about 30 minutes saved per cut; design first: the per-binary `.srcstamp` and `out\gated-<arch>-*.txt` bind a build to its tree today)
- [ ] GUILEG.1 the installer's window legs (`run-leg.ps1 -Window`) driven headless through QMP, screendump to see and send-key or input-send-event to act, so no operator or viewer is needed; documented in `build-and-test.md` and taken into the runner once the `3.2.0.0` cut's window legs have shown it works

## Planned - A Larger Event Ring

Goal: more isochronous budget than one 256-entry event ring allows
(design record 13 section 11.8, point 9).

Status: not opened, unnumbered and unversioned. Taken into Phase 43 as
43.41 by the owner on 2026-10-22 and deferred the same day (owner).

- [ ] ISORING.4 a larger event ring (was 43.41; record 13 section 11.8, point 9), which raises the isochronous budget: deferred by the owner on 2026-10-22 because of the risk - the event ring touches every device and every leg, and a ring larger than one page needs physically contiguous memory on 98 SE - while 43.40's legs showed no Event Ring Full and no Missed Service error. To reopen if UVC or UAC 2 with several streams shows an Event Ring Full or Missed Service errors

## Planned - Bench Follow-Up After `2.4.0.0`

Goal: on real hardware, what Phase 38 could read only in virtual
machines.

Status: not opened, unnumbered and unversioned. Listed by the owner on
2026-10-14 from what Phase 38's tasks owe after the cut (`runs/run-38.md`,
each task's "Owed after the cut"). On 2026-10-18 the owner struck
B24.1 to B24.4 (the Linux quirks on the controllers they name, a real
xHCI 0.96 controller, hidusbf on a composite HID device, and the High
Speed and USB 3 hubs under the `2.4.0.0` ids) from the list; the release
notes' "Untested ground" still says what was not read. A sixth item,
`XHCICLN`'s veto on XP x64, was read before the cut (task 38.8).

- [ ] B24.5 the finer timer while polling, `XhciPollFineTimer` 1, on real hardware (task 38.3, design record 15 section 7.2.4's amendment); its default reconsidered on the reading

## Planned - A Refused Interface's Device Id

Goal: the read-first task deferred from Phase 38 (owner, 2026-10-13).

Status: moved into Phase 43 on 2026-10-20 (owner) as 43.14 (the
reading), 43.15 (the id or the corrected claim) and 43.16 (the legs);
the task below stays as their plan. 43.14 read on 2026-10-22 that a
VID/PID line binds a refused device on 98 SE and ME, so 43.15 moves its
device id (record 13 section 10.7.1). Opened as 38.12 on
2026-10-13 and deferred to after `2.4.0.0` the same day (owner); the plan,
with the owner's answers to its decisions, is design record 13 section
10.7.1, and Phase 38 corrects the record's and `src\xhci_xport.h`'s claim
to what was read.

- [x] NOX.1 (done 2026-10-22 as 43.14 to 43.16; record 13 section 10.7.1) a refused storage interface's device id on Windows 98 SE and ME (from the 38.2 legs, 2026-10-13; owner, the same day: read first, a task of its own). Record 13 and `src\xhci_xport.h` say a REFUSED interface (no transport it can run, task 29-A) "shows no VID/PID-derived hardware id at all", its one hardware id `USB\XHCI98_NOXPORT&VID_v&PID_p&REV_r[&MI_nn]`, so neither `usbstor.inf` nor a vendor INF can bind it. On 98 SE the refused `usb-uas` devnode's registry `HardwareID` read `"USB\\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\\VID_46F4&PID_0003"` (runtime, `Enum\USB\VID_46F4&PID_0003\P382R2`, `out\p38-2\legs\`): the second entry is the bus's device id (`BusQueryDeviceID`), which the PDO still reports in VID/PID form. Read first, on 98 SE and ME (with NUSB 3.3's stack too, whose `usbstor.inf` names 135 VID/PID lines): whether an INF whose only line is that VID/PID binds a refused device. If it does, the refused PDO's device id takes the `XHCI98_NOXPORT` form as well (its instance path moves, so the design says what becomes of a devnode an earlier version made), checked against 38.9's move of the bus's own ids to `USB\XHCI98_...`; if it does not, record 13 and `xhci_xport.h` say why the device id is harmless there. Either way the claim is corrected to what was read, NT's matching (hardware and compatible ids only) stated as read or as not read. Host tests of the id, both builds, legs on 98 SE, ME and one NT target with a UAS-only device at SuperSpeed on a controller that does not stream

## Planned - Releasing the Transfers Kept by a Pinned Containment

Goal: the task deferred from Phase 38 (owner, 2026-10-13), the known
limitation "After the driver closes off a controller it could not prove
stopped, disabling the controller hangs" in the release notes.

Status: not opened, unnumbered and unversioned. Planned as PCR.1, opened
as 38.3 on 2026-10-13 and deferred the same day (owner), after its design
was trimmed to the departure proof and a review showed that configuration
space reading all ones proves no isolation from memory, so no safe proof
is reachable at a disable; the plan, the owner's answers and what a
reopening must answer are design record 17 section 4.13.

- [ ] PCR.1 (in Phase 37 until 2026-10-12) releasing the transfers kept by a pinned containment (owner, 2026-10-11), so disabling, updating or removing a controller the driver closed off without proof that DMA stopped (record 17's containment, `XHCISNAP` "CONTAINED" and "PINNED") completes instead of waiting for ever. Since `2.2.0.0` the driver keeps every transfer the controller might still write into, by design, a hang the release notes list under "Known limitations"; Phase 36's matrix read it again on Windows 2000 (4 of 4 from fresh boots, selective suspend on and off: hidusb's interrupt-IN read kept, its REMOVE waiting) and on Windows 98 SE. At the controller's stop or remove, look for proof that it can no longer write to memory, and only then complete the kept transfers as failed: (a) the device has left the bus (its PCI configuration space, the vendor id included, reads all ones), or (b) it entered D3hot through its PCI power management capability and reads back D3hot, where it may not master (PCI Bus Power Management 1.2). Only at the stop or remove of a controller already pinned by containment, the driver asks for D3 itself, through the PCI stack (a device power IRP), never by writing the power management control register (owner, 2026-10-13, on review): a containment-release step, independent of `XhciSelectiveSuspend`, adding no idle or selective-suspend policy. It reads the state back only after the 10 ms the specification requires before the next access. Conditions from the review: the capability chain validated before it is read; a failed or short configuration read is never taken for all ones, and all-ones data counts as proof (a) only through a validated read whose platform meaning is that the device left the bus or is isolated from memory, an unreachable configuration window alone proving nothing; read completeness established per target, without assuming Windows 98 reports the byte count (`hcd_svc.c` leaves its `IoStatus.Information` unproven), and a stalled configuration or power request addressed, today's wait for it being unbounded; on 98 SE and ME the configuration reads use the route already proven there, to the original PDO; a failed or ineffective transition is no proof and releases nothing; D3hot is not a reset, a function with `No_Soft_Reset` set keeping its state, so before the kept buffers are released the design establishes that DMA stays inhibited through any later return to D0 until HCRST has completed and the old ring and context pointers are invalid, and where it cannot, the buffers stay kept and an unsafe restart is refused; and the release runs before any child's teardown waits on a kept transfer. With neither proof the transfers stay kept, and no registry value overrides that (owner, 2026-10-13). Record 17 gains the section first, a full review loop on the DMA-safety path, host vectors for each proof, and legs: the all-ones injection then a disable on 98 SE, ME, 2000, XP and 7 x64, with and without a client read pending, and the unproven case still holding

## Planned - Real USB4 Hardware

Goal: the USB4 software connection manager (Phase 40) read on a real
USB4 host with real USB4 devices, not only on the emulator's host models.

Status: not opened, unnumbered and unversioned; waiting on hardware.
Moved out of Phase 40 on 2026-10-16 by the owner, where it was task 40.4:
the USB4 hosts are recent UEFI machines, and none at hand offers the
compatibility support module that Windows 98 SE, ME, 2000 and XP need to
boot. A machine that does, or an add-in USB4 card in an older machine
that does, opens it.

- [ ] U4HW.1 real USB4 devices, after the maintainer's ORICO capture (its emulator stand-in, the ORICO capture model, passes on 98 SE and 2000; open on real hardware, record 22 section 16: the PCIe Up adapter credits kept as read, the unused adapter 4's answers, and the enclosure's PCIe switch under 98 SE and 2000); on a USB4 host that boots the target systems

## Planned - Stand By on Windows 2000 With a Tunnelled Device

Goal: the resume ordering of record 21 section 6.3 and the USB4
connection manager's suspend obligations read on Windows 2000 on real
hardware: a resume that succeeds, one that fails, an unplug during sleep,
and the watchdog's expiry.

Status: not opened, unnumbered and unversioned; kept, blocked on a
machine (owner, 2026-10-18). Listed on 2026-10-17 by
the owner, when tasks 39-V.2 and 40-V.2 were closed without it: Windows
2000 in the emulator offers no Stand by (the Cirrus driver vetoes it), and
a forced S3 never resumes, even with no `xhcicm.sys` present. The suspend
half read as designed under both connection managers; ME's S3 readings
(run-39) stand.

- [ ] S3W2K.1 the four readings on a Windows 2000 machine with a Thunderbolt or USB4 controller that resumes from S3, under each connection manager the machine has

## Planned - The Free Driver Pack

Goal: the package draws its class drivers from `usb-wdm-free`, as
Phase 43 draws NUSB's storage files from `usb-wdm-nonfree` (43.2,
43.3), once that repository has a released driver.

Status: not opened, unnumbered and unversioned; waits on
`usb-wdm-free`'s first release (owner, 2026-10-20).
`usb-wdm-free` is the owner's standalone repository beside this one:
GPL-2.0 WDM drivers of the owner's own for Windows 98 SE through Windows 8
(owner, 2026-10-20; later systems possible but not tested), written from the specifications,
Microsoft's published driver kit documentation, this project's code and
GPL-2.0-compatible code (Linux, ReactOS) adapted with its notices kept;
never a Microsoft or vendor binary or its disassembly. It holds both the
source (`src/<class>/<package>/`) and the released builds
(`free/<class>/<package>/<systems>/`, 8.3 names, `SHA256.TXT`), builds
with no file from another repository, and its first planned drivers are a
USB Video Class driver for webcams and a USB Audio Class 2.0 driver.

- [x] FREEPKG.1 (done in 43.48's form, 2026-10-24: download 2 carries usb-wdm-free v0.1.0, pinned by `p43-pin2`, `6b992660`, each package offered on the systems its recorded legs passed on) the package draws from `usb-wdm-free` (owner, 2026-10-20): `make-package.ps1` takes its released `free/` tree from a pinned checkout and tag, checks it against its `SHA256.TXT` and stages each package's folder for the systems it names, as 43.3 stages the non-free pack (its own `-DriverPackRoot`-style parameter and a switch leaving it out); the release download carries it, and whether `releases\<version>\` tracks it too is the owner's (it is GPL-2.0, unlike 43.2's files); a Components row of the installer per driver, offered on the systems its folder names, a file already present never overwritten and the uninstall removing what it placed, as 43.4 does; `legal-provenance.md` a row for it, its `NOTICE.md` shipped beside it; the legs for each driver on each system it names, and the release notes' rows
