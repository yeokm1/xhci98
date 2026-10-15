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

**Status: Phases 39 and 40 opened 2026-10-15 by the owner.** The two
pieces of planned work drawn up on 2026-10-14 with the placeholder ids
`TB3` and `U4CM` are Phase 39 and Phase 40, in the order the `usb4-emu`
handoff gives (its section 2, maintainer 2026-10-14): Phase 39 the
Thunderbolt firmware-connection-manager client with tunnel re-enumeration
and safe eject, because it can be proven on the owner's ThinkPad P14s Gen
1; Phase 40 the USB4 software connection manager; real USB4 devices third,
task 40.4, once the ORICO enclosure capture exists. The release is
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
| Names (D8) | Owner, 2026-10-15, as recommended: `xhcicm.sys`, `xhcicm.inf`, `xhcicmf.inf`, `xHCI98 Thunderbolt 3 Host Interface`, `xHCI98 Thunderbolt 3 Tunnel Port Filter`, class System |
| The NHI ids (D9) | Owner, 2026-10-15, changed: every Alpine Ridge and Titan Ridge NHI claimed from a public id listing, never untested without a plain warning; widened the same day, with the source rule, to every Intel firmware-connection-manager host Linux addresses (Falcon Ridge, Alpine Ridge, Titan Ridge, Maple Ridge, Ice Lake to Meteor Lake), only `15BF` captured, the untested ids acting by default with the warning in the README and release notes (owner); the widening's shape - four-test selection with an inert start, hardware profiles apart from message profiles, per-domain tunnel-port sets, no reset or takeover by the ICM client - is the orchestrator's decision of 2026-10-15 after review, for the owner to confirm (record 21 sections 1.4, 3.3, 6.3) |
| Safe eject (D10) | Owner, 2026-10-15, changed: in Phase 39 as `39-A.8`; NT through the filter's answer to `IRP_MN_QUERY_CAPABILITIES`, 98 SE and ME through CONFIGMG best effort, NT-only with a release-notes line if the 9x half fails; the eject stops the drivers and the user unplugs. Two refinements, the orchestrator's for the owner to confirm: the mark goes on the children of verified tunnel ports, never on a port itself, and `SurpriseRemovalOK` is set only where `39-V.2` shows Windows still lists the device with it (record 21 section 7.4) |
| A master switch (D11) | Owner, 2026-10-15, as recommended: `XhciCmMode`, default 1 (auto), 0 off (the inert start), 2 the ICM client on an Intel id the profile table does not list, at the user's own risk and with that id added to a copy of the INF by the user, 3 the software connection manager's experimental override (design record 22); the install itself is the opt-in, so the default is on (record 21 section 8) |
| The log emission (D12) | Owner, 2026-10-15, as recommended: nothing in `release`, `#if DBG` in `debug` and `qemu` |
| Cut before the bench (D13) | Owner, 2026-10-15, as recommended: the VM legs and gates before the cut, `39-E.1` and `39-E.2` after it on the asset, a failure there a re-cut; the cut is `40.final`'s |
| Time | Owner, 2026-10-15: no task is cut to meet a time; the run goes late instead |

## Phase sequence

Phase 39 is the Thunderbolt firmware-connection-manager client, tunnel
re-enumeration and safe eject; Phase 40 the USB4 software connection
manager; both ship as `3.0.0.0`. Real USB4 devices follow the ORICO capture
and are task 40.4 until they need more. The order is the handoff's: the
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

Status: open. Opened 2026-10-15 by the owner, from the planned work `TB3`
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
- [ ] 39-A.7 the `nvme98` link-down notification (record 21 sections 7.1 and 7.2, D6), once `nvme98` has chosen its receiving interface: the NT `IOCTL_SCSI_MINIPORT` route and the 9x VxD-service route, the recipient resolved and cached at tunnel up by a discovery worker thread of the driver's own, never on the connection-manager thread, its reference held with a target-device-change notification so an eject is not held back (section 7.4), each statement sent only after the qualified platform condition reads clear (the SHPC stage-3 surprise-removal presence change on i440FX; the tunnel port's Data Link Layer Link Active bit where a PCI Express capability exists) and before any removal call, from inside a command wait, a tunnel-up poll or a discovery when the disconnect lands during one, inside the end-to-end budget of record 21 section 7.2 (the idle poll clamped to 250 ms while enabled; no statement when the recipient is not yet cached, for an unmatched disconnect, or while the domain's tunnel-port set has more than one member); the statement withheld on every OS until gate G3's ordering against the emulator's stage-2 event is qualified there, and withheld on hardware until the completed-reset boundary, which this driver cannot observe through a down link, is resolved by `nvme98`'s own MD-2 verification (the P14s with an NVMe enclosure and with one that has an internal switch) and the owner. Not a condition of the checkpoint; the release notes say which state the pair is in
- [x] 39-A.8 safe eject (D10, record 21 section 7.4): on NT the bridge filter's baseline and snapshot of its port's `BusRelations` (active only after the baseline) and the child filters it attaches to devices newly reported on an active member, answering `IRP_MN_QUERY_CAPABILITIES` on the way up with `Removable` (and `SurpriseRemovalOK` on Windows 2000 only if `39-V.2` shows 2000 still lists the device with it), never marking a port itself; on 98 SE and ME `Change_DevNode_Status` adding `DN_REMOVABLE` to each new child devnode of a tunnel port behind the `Get_Version` guard; `XhciCmSafeEject` (default 1) disabling both; the eject stopping the subtree's drivers with the tunnel left up until the unplug; host vectors for the snapshot rule in `test_tbt_topo`; and the release-notes lines of section 7.4 drafted, the 9x half shipped off with its line if `39-V.1` or `39-V.3` shows it cannot work Done (p39-reenum 81f75d24, merged d8de89c8): Removable from the child filters on NT, DN_REMOVABLE on 98 SE and ME, a refused removal never forced; the Windows 2000 eject legs passed. The SurpriseRemovalOK reading and the 98 SE and ME eject applet belong to 39-V.2.
- [ ] 39-V.1 Windows 98 SE, the nine legs of record 21 section 10.2's table on `tb3-alpine-ridge` (`OFF_OFF`, `OFF_ON`, `ON_OFF`, `ON_ON`, device at boot and not): install, approve and the NVMe volume with no Refresh; `device_del` and `device_add`; the boot-tunnelled device left alone, on an overlay carrying 39-0.7's POST workaround, with no reset and no teardown in the trace; the empty table with the controller present (`OFF_OFF`, `OFF_ON`); the absent controller (`ON_OFF`, `ON_ON`, nothing attached); disable and enable; `XhciCmApprove` 0 and `XhciCmMode` 0 (the inert start); the dock's xHCI under `xhci98.sys` where the dialect offers it; V.1.9, the 9x half of safe eject (`Change_DevNode_Status` and `Get_DevNode_Status` of the NVMe devnode, never a port's, and whether 98 SE offers any eject); every leg with `-trace 'usb4_*'` and the `qemu` flavour's log
- [ ] 39-V.2 Windows 2000, the same table on the Alpine Ridge tree (`TOPO=ar`), plus the filter's install through its own INF, the boot question of 39-0.4, S3 where the guest offers it with the resume ordering of record 21 section 6.3 read (a resume that succeeds, one that fails, an unplug during sleep, the watchdog's expiry), and on the SMP guest under Driver Verifier a filter disable concurrent with tunnel work (the rundown of section 3.2) and a child filter's attach and removal; and the NT half of safe eject: the NVMe listed in "Unplug or Eject Hardware" with `Removable` alone, stopped, held, then `device_del` with no Unsafe Removal dialog, `device_add` listed again, a plain `device_del` with no eject, the boot-tunnelled device not listed until re-plugged, `XhciCmSafeEject` 0, and the build with `Removable` and `SurpriseRemovalOK` both set - whether 2000 still lists the device and whether a plain unplug then shows the dialog, the reading that decides 39-A.8's 2000 path
- [ ] 39-V.3 ME, XP, XP x64, Vista x86 and x64, 7 x86 and x64: the install, one plug, unplug and re-plug cycle, and the safe-eject cycle (eject, unplug, re-plug, a plain unplug: whether the device is listed, whether any dialog shows) each; on ME, what "Unplug or Eject Hardware" lists and whether a devnode marked through `Change_DevNode_Status` is listed and stops cleanly. These tiers carry no checkpoint tax: the legs are read and recorded, a failure on one is a release-notes line and not a checkpoint failure, and the install leg from the asset that every release requires on every target is `40.final`'s; on 7 x64 whether `pci.sys`'s own hot-plug acts before the filter is a reading, not a fault
- [ ] 39-V.4 the `usb4-generic` profile (class `0x0C0340`, no ICM) left unclaimed by this phase's INFs: no model line matches it, nothing binds and nothing is logged, read on 98 SE and 2000 (Phase 40's INF lines claim it later)
- [ ] 39-E.1 the owner's P14s Gen 1 with the TS3 Plus under Windows 98 SE, after the `3.0.0.0` cut: Assist off, pre-boot off, the package installed, the dock approved and its tree up, its xHCI controllers under `xhci98.sys` with a stick and a keyboard, `XHCISNAP` on each; unplug and re-plug at the desktop; pre-boot on, the tree at boot and then a hot-plug cycle; the DebugView emission at a disable; the 9x half of safe eject where it shipped (record 21 section 10.4)
- [ ] 39-E.2 the P14s `$PIR` dump from 39-0.6, by the procedure in `xhciqual/hardware-testing.md` (its section "ThinkPad P14s Gen 1 - does the `$PIR` list the Thunderbolt root port?"), and its consequence written into the release notes: whether a function hot-added behind the tunnel is started at all on Windows 98 (Code 29 is a devnode `PCI.VXD` never started, which no polling below it mitigates; `WIN98-TEST.md`, "Hot-added NVMe gets no IRQ behind 00:1c"), and if not, that pre-boot tunnelling is the route on that machine; and separately whether a controller that does start without a usable line runs under `xhci98.sys`'s polled delivery
- [ ] 39.final the phase closed, cutting nothing (D7): the documents drafted for the `3.0.0.0` notes (the security paragraph and the "nothing is remembered" line; the limitations - the absent controller and force power, safe eject and which half shipped, the Unsafe Removal dialog on 2000, a device tunnelled before the driver bound offered for eject only after a re-plug, suspend with an integrated controller, system suspend with a tunnelled device attached unsupported on every target, the `nvme98` channel's state; the untested-hosts line of record 21 section 1.4.6, with every id but `15BF` named untested; and "Untested ground"), `README.md`'s hardware list with the same warning, `source-files.md`, `docs/README.md`'s table; the builds on x86 and amd64, every gate and the host suites; a dry build of the package (`make-package.ps1`, no release directory, no signature); 39-V.1 and 39-V.2 read on the `qemu` flavour and then the `release` flavour of that dry build

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

Status: open. Opened 2026-10-15 by the owner, from the planned work
`U4CM`; released with Phase 39 as `3.0.0.0` (D7), the cut being
`40.final`'s. Its design record is design record 22. Nothing in it waits
for the ORICO USB4 enclosure capture except task 40.4, which adds a real
device-router preset and a conformance check.

- [x] 40.1 the design record (design record 22): the connection manager's state machine from `u4cm.c` (host router, adapters, device router per port, DROM by router operation, the CV/CR handshake, lane bonding, the PCIe tunnel's four path entries, the optional USB3 tunnel), hot-plug events and unplug handling (which `usb4cm` lacks), the positive admission and the preservation of every existing tunnel and link power state (record 21 sections 1.4.2 and 1.4.5), the router quirks record 21 section 10.1 assigns to it, a Thunderbolt 3 device behind a USB4 host (legacy routers configured router by router, PCIe tunnel first), the pre-OS cleanup, and what the two profiles share (done 2026-10-15: design record 22 (draft 2), reviewed and revised to convergence; decisions U1-U16 taken by the orchestrator, for the owner to confirm)
- [x] 40-0.1 the transcription: `tbt_regs.h` gains every router, adapter, path, TMU, PCIe, USB3 and notification field record 22 uses, each cited to the USB4 Specification 2.0 section and cross-checked against Linux `tb_regs.h`; the `legal-provenance.md` rows for the Linux and FreeBSD reading and the `usb4cm` and Linux adaptation rows (done 2026-10-15, `p40-core` to b146b58e: the Phase 40 block of `tbt_regs.h`, each field cited to the USB4 Specification 2.0 and cross-checked against Linux `tb_regs.h`)
- [x] 40-0.2 the USB4 ids: the Lunar Lake, Panther Lake, Wildcat Lake, Barlow Ridge, AMD and ASM4242 tunnel-port ids read from `pci.ids` 2026.10.14 with a provenance row; `xhcicm.inf` and `xhcicm-amd64.inf` gain `PCI\CC_0C0340`, the Lunar Lake, Panther Lake, Wildcat Lake and Barlow Ridge NHI ids and `1022:1668` and `1022:1669`; `xhcicmf.inf` and `xhcicmf-amd64.inf` gain those port ids and `1B36:0001` and `1B36:000C`; `check-cm-inf.ps1` gains the class-line, no-device-router-id and `&CC_0604` rules with its self-test (done 2026-10-15, `p40-ids` to 9495719a: the USB4 NHI and port ids, the class line read matching on 98 SE, the filter INF in `nt\` with `CM-NO9XBRIDGE`'s layout rule; x86 and amd64 `all` green)
- [x] 40-0.3 the launcher reading: whether `-global usb4-host.host-model`, `.preos-cm`, `.usb3-xhci`, `.usb3-port1` and `-global usb4-device.usb3-dev` reach their properties through 39-0.7's launcher, and how `docs/HARNESS.md`'s injection points drive leg U.4 (done 2026-10-15, `runs/run-40.md`: all five `-global` properties reach QEMU, read back in `info qtree`; `usb3-dev` drives the USB3 tunnel, `usb3-xhci` and `usb3-port1` shape only the ACPI entry; the launcher gained `-BaseImage` and `-RunRoot`; the pre-OS tunnel seen from boot on 98 SE and 2000 on both host models; the harness acts on the endpoint and the tunnel gate only, the unplug itself is `device_del`)
- [x] 40-A.1 the transport, pure: `tbt_ring.c` gains control packets, record 22 section 5.1's notification classes, plain and enhanced acknowledgements, the hot-plug acknowledgement, the `clock_us` and safe-point callbacks, and maximum-versus-minimum waits; `test_tbt_ring` grows to match (done 2026-10-15, `p40-core` to b146b58e: the control transport with plain, enhanced and deferred acknowledgements, hot-plug acknowledgements, bounded waits and safe points; `test_tbt_ring`, 522 checks)
- [x] 40-A.2 the selection, pure: record 21 section 1.4.2 and record 22 section 3 (in `tbt_hw.c`'s selection or a `tbt_select.c`, as the code decides); host tests with every negative case and every AUTH_DONE and ICM_EN combination (done 2026-10-15, `p40-core` to b146b58e: in `tbt_hw.c`' selection, the positive admission, the experimental and forced routes; `test_tbt_select`, 207 checks)
- [x] 40-A.3 the connection-manager core, pure: `tbt_u4cm.c`, record 22 sections 6, 7.1 to 7.5 and 9 (credits, quarantine, the TMU, the quirk table, discovery and preservation); host suite `test_u4cm` against a C89 fake fabric with the `minimal` and `intel-adl` presets, two domains and a Thunderbolt 3 router (done 2026-10-15, `p40-core` to b146b58e: `tbt_u4cm.c` and `.h`; `test_u4cm`, 672 checks, over a C89 fake fabric with the minimal, intel-adl and asm4242 presets and two domains)
- [x] 40-A.4 `tbt_topo.c` gains the router-named source and the learned correspondence; `test_tbt_topo` grows to match (done 2026-10-15, `p40-topo` to 865dde19: `XHCI_TBT_SRC_ROUTER`, `XhciTbtTopoAddNamed`, the instance bindings `XHCI_TBT_BINDS`, learning with `XhciTbtLearnArm`, `XhciTbtLearnUp`, `XhciTbtLearn` and `XhciTbtLearnDown`, and `XhciTbtPortMember`, failing closed; host suite `test_tbt_topo`, 543 checks)
- [x] 40-A.5 the `usb4-emu` fabric cross-check script, recorded as a reading (done 2026-10-15, `runs/run-40.md`: the pure core against the usb4-emu fabric model at ce13e0e, minimal, intel-adl and asm4242, 45469 checks, no failures)
- [ ] 40-A.6 the driver half: the profile chosen at start, Ice Lake to Wildcat Lake force power and LTR with the ownership record, the ring takeover, the resume obligations and the filter hold, `XhciCmMode` 3, `XhciCmUsb3Tunnel`, `XhciCmLaneBonding` and `XhciCmPreOsCleanup` written by every INF path, the `u4.*` counters, `KeTickCount` and `KeQueryTimeIncrement` in the import allowlists with record 13's evidence
- [ ] 40-A.7 the `nvme98` statement under the USB4 profile (the endpoint-presence anchor and the 3 s deadline), with 39-A.7
- [ ] 40-A.8 Thunderbolt 3 device routers behind a USB4 host, record 22 section 8 (PCIe first; the DROM by bit-bang as a second pass) (pure part done 2026-10-15 in `tbt_u4cm.c`, `p40-core`; the driver side is `p40-drv`'s)
- [ ] 40-A.9 the safe-eject entry point for 39-A.8
- [ ] 40-V.1 Windows 98 SE, legs U.1 to U.13 of record 22 section 14.2
- [ ] 40-V.2 Windows 2000, the same legs plus the filter on `1B36:0001`
- [ ] 40-V.3 ME, XP, XP x64, Vista and 7: the install and one plug cycle each, with no checkpoint tax
- [ ] 40.4 real USB4 devices, after the maintainer's ORICO capture
- [x] 40.5 onward: the install and upgrade tool, design record 23 (`p40-inst`). Done 2026-10-15 (`p40-inst` 2bfcd27f, merged bcc74be4): `INSTALL.BAT` with `XHCIINST.EXE` and `XHCIIN64.EXE`, record 23 rev 8. The tool waits for Windows' own installs before and between its steps, the last line agrees with the exit code, the 98 SE and ME fixes, the Windows 2000 filter restart. Legs pass on 98 SE, ME, 2000, XP, XP x64, Vista x86 and x64, 7 x86 and x64 (run-40.md). Owed: Vista x64 update after the restart, 7 x64 uninstall over 3.0.0.0, and the 2000 filter install through the tool on the Thunderbolt test machine.
- [ ] 40.final the `3.0.0.0` cut: the release notes for both phases (`39.final`'s drafts, the safe-eject and untested-hosts lines among them, and Phase 40's), `README.md`, `source-files.md`, `docs/README.md`'s table; the builds, the gates, the dry cut and the owner's signed cut of `3.0.0.0`, with the install legs read from the asset on every target. Task 40.4 is not a condition of the cut: it waits on the ORICO capture and is carried into a later release if the capture is not in hand (an orchestrator decision of 2026-10-15, for the owner to confirm)

Checkpoint: design record 22 converged before 40-A's code; the host suites
green with the gates on both architectures; 40-V.1's and 40-V.2's legs read on the `qemu`
flavour and the `release` flavour of the dry cut; the cut with the install
leg from the asset on every target; then Phase 39's bench (39-E) after it.
