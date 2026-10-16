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
| Names (D8) | Owner, 2026-10-15, as recommended: `xhcicm.sys`, `xhcicm.inf`, `xhcicmf.inf`, class System; the device descriptions revised by the owner the same day to name each controller's generation: `xHCI98 Thunderbolt 2 Controller`, `xHCI98 Thunderbolt 3 Controller`, `xHCI98 USB4 Host Router`, and for the ports the filter attaches to `xHCI98 Thunderbolt PCI Bridge`, `xHCI98 USB4 PCI Bridge`, `xHCI98 PCI Bridge` (record 21 section 3.3) |
| The NHI ids (D9) | Owner, 2026-10-15, changed: every Alpine Ridge and Titan Ridge NHI claimed from a public id listing, never untested without a plain warning; widened the same day, with the source rule, to every Intel firmware-connection-manager host Linux addresses (Falcon Ridge, Alpine Ridge, Titan Ridge, Maple Ridge, Ice Lake to Meteor Lake), only `15BF` captured, the untested ids acting by default with the warning in the README and release notes (owner); the widening's shape - four-test selection with an inert start, hardware profiles apart from message profiles, per-domain tunnel-port sets, no reset or takeover by the ICM client - is the orchestrator's decision of 2026-10-15 after review, for the owner to confirm (record 21 sections 1.4, 3.3, 6.3) |
| Safe eject (D10) | Owner, 2026-10-15, changed: in Phase 39 as `39-A.8`; NT through the filter's answer to `IRP_MN_QUERY_CAPABILITIES`, 98 SE and ME through CONFIGMG best effort, NT-only with a release-notes line if the 9x half fails; the eject stops the drivers and the user unplugs. Two refinements, the orchestrator's for the owner to confirm: the mark goes on the children of verified tunnel ports, never on a port itself, and `SurpriseRemovalOK` is set only where `39-V.2` shows Windows still lists the device with it (record 21 section 7.4) |
| A master switch (D11) | Owner, 2026-10-15, as recommended: `XhciCmMode`, default 1 (auto), 0 off (the inert start), 2 the ICM client on an Intel id the profile table does not list, at the user's own risk and with that id added to a copy of the INF by the user, 3 the software connection manager's experimental override (design record 22); the install itself is the opt-in, so the default is on (record 21 section 8) |
| The log emission (D12) | Owner, 2026-10-15, as recommended: nothing in `release`, `#if DBG` in `debug` and `qemu` |
| Cut before the bench (D13) | Owner, 2026-10-15, as recommended: the VM legs and gates before the cut, `39-E.1` and `39-E.2` after it on the asset, a failure there a re-cut; the cut is `40.final`'s |
| Time | Owner, 2026-10-15: no task is cut to meet a time; the run goes late instead |

## Phase sequence

Phase 39 is the Thunderbolt firmware-connection-manager client, tunnel
re-enumeration and safe eject; Phase 40 the USB4 software connection
manager; Phase 41 the graphical tree, eject and tray tool XHCITR; all
three ship as `3.0.0.0`. Real USB4 devices follow the ORICO capture
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
- [x] 39-A.7 the `nvme98` link-down notification (record 21 sections 7.1 and 7.2, D6), once `nvme98` has chosen its receiving interface: the NT `IOCTL_SCSI_MINIPORT` route and the 9x VxD-service route, the recipient resolved and cached at tunnel up by a discovery worker thread of the driver's own, never on the connection-manager thread, its reference held with a target-device-change notification so an eject is not held back (section 7.4), each statement sent only after the qualified platform condition reads clear (the SHPC stage-3 surprise-removal presence change on i440FX; the tunnel port's Data Link Layer Link Active bit where a PCI Express capability exists) and before any removal call, from inside a command wait, a tunnel-up poll or a discovery when the disconnect lands during one, inside the end-to-end budget of record 21 section 7.2 (the idle poll clamped to 250 ms while enabled; no statement when the recipient is not yet cached, for an unmatched disconnect, or while the domain's tunnel-port set has more than one member); the statement withheld on every OS until gate G3's ordering against the emulator's stage-2 event is qualified there, and withheld on hardware until the completed-reset boundary, which this driver cannot observe through a down link, is resolved by `nvme98`'s own MD-2 verification (the P14s with an NVMe enclosure and with one that has an internal switch) and the owner. Not a condition of the checkpoint; the release notes say which state the pair is in (done 2026-10-15: `p39-nvlink` 03bab294, merged 80bd6ad4. `nvme98cm.h` included unchanged; on NT the callback object and the never-freed table, UP before every enumeration, DOWN_QUIESCED only after the path is closed and the endpoint reset and only on the qualified emulator bridge until nvme98 qualifies hardware, the acknowledgement wait (5 s NT, 3 s provisional on 98 and ME) before every removal; on 98 and ME the per-boot registry locator and the REMOVE_PENDING request on the CONFIGMG worker. Read on Windows 2000 against nvme98's receiver and on 98 SE; nvme98's interface 3 adoption, per-port non-blocking waits and the XP x64 and ME legs owed)
- [x] 39-A.8 safe eject (D10, record 21 section 7.4): on NT the bridge filter's baseline and snapshot of its port's `BusRelations` (active only after the baseline) and the child filters it attaches to devices newly reported on an active member, answering `IRP_MN_QUERY_CAPABILITIES` on the way up with `Removable` (and `SurpriseRemovalOK` on Windows 2000 only if `39-V.2` shows 2000 still lists the device with it), never marking a port itself; on 98 SE and ME `Change_DevNode_Status` adding `DN_REMOVABLE` to each new child devnode of a tunnel port behind the `Get_Version` guard; `XhciCmSafeEject` (default 1) disabling both; the eject stopping the subtree's drivers with the tunnel left up until the unplug; host vectors for the snapshot rule in `test_tbt_topo`; and the release-notes lines of section 7.4 drafted, the 9x half shipped off with its line if `39-V.1` or `39-V.3` shows it cannot work Done (p39-reenum 81f75d24, merged d8de89c8): Removable from the child filters on NT, DN_REMOVABLE on 98 SE and ME, a refused removal never forced; the Windows 2000 eject legs passed. The SurpriseRemovalOK reading and the 98 SE and ME eject applet belong to 39-V.2.
- [ ] 39-V.1 Windows 98 SE, the nine legs of record 21 section 10.2's table on `tb3-alpine-ridge` (`OFF_OFF`, `OFF_ON`, `ON_OFF`, `ON_ON`, device at boot and not): install, approve and the NVMe volume with no Refresh; `device_del` and `device_add`; the boot-tunnelled device left alone, on an overlay carrying 39-0.7's POST workaround, with no reset and no teardown in the trace; the empty table with the controller present (`OFF_OFF`, `OFF_ON`); the absent controller (`ON_OFF`, `ON_ON`, nothing attached); disable and enable; `XhciCmApprove` 0 and `XhciCmMode` 0 (the inert start); the dock's xHCI under `xhci98.sys` where the dialect offers it; V.1.9, the 9x half of safe eject (`Change_DevNode_Status` and `Get_DevNode_Status` of the NVMe devnode, never a port's, and whether 98 SE offers any eject); every leg with `-trace 'usb4_*'` and the `qemu` flavour's log (read 2026-10-15 on 98 SE, `tb3-alpine-ridge`: V.1.1 to V.1.7 pass - install, approve and the NVMe volume with no Refresh, device_del and device_add, the boot-tunnelled device on OFF_ON and ON_ON left alone with no reset, the empty table, the absent controller, disable and enable, approve 0 and mode 0; V.1.9 DN_REMOVABLE set, with the eject through `INSTALL /eject`; V.1.8 to the bench)
- [ ] 39-V.2 Windows 2000, the same table on the Alpine Ridge tree (`TOPO=ar`), plus the filter's install through its own INF, the boot question of 39-0.4, S3 where the guest offers it with the resume ordering of record 21 section 6.3 read (a resume that succeeds, one that fails, an unplug during sleep, the watchdog's expiry), and on the SMP guest under Driver Verifier a filter disable concurrent with tunnel work (the rundown of section 3.2) and a child filter's attach and removal; and the NT half of safe eject: the NVMe listed in "Unplug or Eject Hardware" with `Removable` alone, stopped, held, then `device_del` with no Unsafe Removal dialog, `device_add` listed again, a plain `device_del` with no eject, the boot-tunnelled device not listed until re-plugged, `XhciCmSafeEject` 0, and the build with `Removable` and `SurpriseRemovalOK` both set - whether 2000 still lists the device and whether a plain unplug then shows the dialog, the reading that decides 39-A.8's 2000 path (read 2026-10-15: the Windows 2000 legs pass on the Alpine Ridge tree and on Titan Ridge, the SMP guest under Driver Verifier, the safe eject with Removable alone; R32: the silence passes, the re-plug fails by platform and is owed on the post-cut emulator; the host interface liveness monitor built and read; S3 owed)
- [ ] 39-V.3 ME, XP, XP x64, Vista x86 and x64, 7 x86 and x64: the install, one plug, unplug and re-plug cycle, and the safe-eject cycle (eject, unplug, re-plug, a plain unplug: whether the device is listed, whether any dialog shows) each; on ME, what "Unplug or Eject Hardware" lists and whether a devnode marked through `Change_DevNode_Status` is listed and stops cleanly. These tiers carry no checkpoint tax: the legs are read and recorded, a failure on one is a release-notes line and not a checkpoint failure, and the install leg from the asset that every release requires on every target is `40.final`'s; on 7 x64 whether `pci.sys`'s own hot-plug acts before the filter is a reading, not a fault (NT tiers read 2026-10-15: XP, XP x64, Vista x86 and 7 x86 pass - the install with the nt\ filter, the plug cycle with the NVMe up with no Refresh, the safe-eject cycle listed with Code 47 and no dialog, a plain unplug with no dialog; Vista x64 and 7 x64 owed for want of a test-signed package; ME owed); Vista x64 and 7 x64 read 2026-10-16 on the owner's pre-cut test-signed package (a platform and recipe reading; the release's x64 binaries change before the cut): the install with the nt\ filter in test-signing mode, the plug cycle, the safe-eject cycle through Safely Remove Hardware and through INSTALL /eject, Code 47, no dialog, pass on both (run-39.md, "39-V.3 on Vista x64 and 7 x64"); ME read 2026-10-15: the install through INSTALL.BAT and one plug cycle pass, "Unplug or Eject Hardware" lists nothing (the eject is `INSTALL /eject`), Stand by and Hibernate resume pass
- [ ] 39-V.4 the `usb4-generic` profile (class `0x0C0340`, no ICM) left unclaimed by this phase's INFs: no model line matches it, nothing binds and nothing is logged, read on 98 SE and 2000 (Phase 40's INF lines claim it later) (read 2026-10-15: Phase 40's `PCI\CC_0C0340` line claims the `usb4-generic` host as designed, "xHCI98 USB4 Host Router", on 98 SE and ME; 2000 in run 40)
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
- [x] 40-A.6 the driver half: the profile chosen at start, Ice Lake to Wildcat Lake force power and LTR with the ownership record, the ring takeover, the resume obligations and the filter hold, `XhciCmMode` 3, `XhciCmUsb3Tunnel`, `XhciCmLaneBonding` and `XhciCmPreOsCleanup` written by every INF path, the `u4.*` counters, `KeTickCount` and `KeQueryTimeIncrement` in the import allowlists with record 13's evidence (done 2026-10-15: `p40-drv` 7acb5efb, merged d0004e1b; legs in run-40.md)
- [x] 40-A.7 the `nvme98` statement under the USB4 profile (the endpoint-presence anchor and the 3 s deadline), with 39-A.7 (done 2026-10-15 with 39-A.7: the USB4 profile's hooks, UP from the hook, DOWN with PathClosed, presence samples; host-tested, the Windows 2000 guest leg on `usb4-generic` owed)
- [ ] 40-A.8 Thunderbolt 3 device routers behind a USB4 host, record 22 section 8 (PCIe first; the DROM by bit-bang as a second pass) (pure part done 2026-10-15 in `tbt_u4cm.c`, `p40-core`; the driver side, the router-kind log, done in `p40-drv`; the DROM bit-bang, the version by its major and the upstream adapter from the read's response done 2026-10-16 on `tb3ver`, read on 98 SE and 7 x86, `runs/run-41.md`; the 2000 leg is EMU.1's)
- [x] 40-A.9 the safe-eject entry point for 39-A.8 (done 2026-10-15: `TbtU4EjectPcie` through `CmReenumSetEject`, the PCIe tunnel alone, USB3 kept; the Windows 2000 leg passed, the eject to be re-read on the final build in 40-V.2)
- [ ] 40-V.1 Windows 98 SE, legs U.1 to U.13 of record 22 section 14.2 (partly read 2026-10-15: U.1, U.3, U.5, U.6, U.8 fabric half and U.10 pass) (also read 2026-10-15 on the ASM2464 device model of the ORICO capture on `intel-adl`: the install, the tunnel, the NVMe read and written with no Refresh, unplug, re-plug and a cold boot pass after the DROM unused-adapter fix, record 22 section 6.11); U.2 read 2026-10-15 for the connection manager (the NVMe at Code 29 beside the `-Usb3` xHCI, the guest's interrupt routing), U.8's USB3 tunnel at 5 Gb/s, and U.14 (the host interface lost: contained, no access after the gate reopens); U.4, U.7, U.9 and U.11 to U.13 owed
- [ ] 40-V.2 Windows 2000, the same legs plus the filter on `1B36:0001` (partly read 2026-10-15: U.2 hot plug through the filter with nvme98, and the safe eject) (the ASM2464 model as on 98 SE, plus the safe eject, pass) (read 2026-10-15: U.1 to U.13 pass on `minimal`, `intel-adl` and `asm4242`, U.11 on `asm4242` failing by platform with Code 12; the PCIe-only eject re-read passes; S3 and the Thunderbolt 3 router behind a USB4 host owed); U.14, the host interface lost under the USB4 profile, passes 2026-10-15 (contained, no access, a re-enable starts a new instance that finds the tunnel preserved)
- [ ] 40-V.3 ME, XP, XP x64, Vista and 7: the install and one plug cycle each, with no checkpoint tax (read 2026-10-15 on `usb4-generic` `intel-adl`: XP, XP x64, Vista x86 and 7 x86 pass, the install and one plug cycle; Vista x64 and 7 x64 owed for want of a test-signed package; ME passes, 2026-10-15: INSTALL.BAT and one plug cycle; Vista x64 and 7 x64 pass, 2026-10-16, on the pre-cut test-signed package, run-40.md's last section)
- [ ] 40.4 real USB4 devices, after the maintainer's ORICO capture (its emulator stand-in, the ORICO capture model, passes on 98 SE and 2000; open on real hardware, record 22 section 16: the PCIe Up adapter credits kept as read, the unused adapter 4's answers, and the enclosure's PCIe switch under 98 SE and 2000)
- [x] 40.5 onward: the install and upgrade tool, design record 23 (`p40-inst`). Done 2026-10-15 (`p40-inst` 2bfcd27f, merged bcc74be4): `INSTALL.BAT` with `XHCIINST.EXE` and `XHCIIN64.EXE`, record 23 rev 8. The tool waits for Windows' own installs before and between its steps, the last line agrees with the exit code, the 98 SE and ME fixes, the Windows 2000 filter restart. Legs pass on 98 SE, ME, 2000, XP, XP x64, Vista x86 and x64, 7 x86 and x64 (run-40.md). Owed: Vista x64 update after the restart, 7 x64 uninstall over 3.0.0.0, and the 2000 filter install through the tool on the Thunderbolt test machine. The first two read 2026-10-16 on the pre-cut test-signed package: pass (run-40.md, last section).
- [ ] 40.final the `3.0.0.0` cut: the release notes for both phases (`39.final`'s drafts, the safe-eject and untested-hosts lines among them, and Phase 40's), `README.md`, `source-files.md`, `docs/README.md`'s table; the builds, the gates, the dry cut and the owner's signed cut of `3.0.0.0`, with the install legs read from the asset on every target. Task 40.4 is not a condition of the cut: it waits on the ORICO capture and is carried into a later release if the capture is not in hand (an orchestrator decision of 2026-10-15, for the owner to confirm)

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

Status: open. Opened 2026-10-16 by the owner, who held `3.0.0.0` for it;
it ships in `3.0.0.0` with Phases 39 and 40, and `40.final`'s cut comes
after `41.final`. Its design record is design record 25.

- [x] 41-0.1 the design record (design record 25), reviewed until it converges; the icon `xhcitr\xhcitr.ico` (done 2026-10-16: revision 5, reviewed and revised four times, converged; the icon drawn in the 16 VGA colours)
- [x] 41-A.1 `xhcicm.sys`: the query device `\\.\XHCICM<n>` and `IOCTL_XHCICM_QUERY` (record 25 section 4), the router extension cache (link rate and width, link state), the pure builder `cm_query.c` with host tests in `test_cm` (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-A.2 `tr_core.c`, pure: query and record parsing, descriptor decoding, speed names, the tray event diff, tree keys, the report text; host suite `test_tr` (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-A.3 the window: tree, details pane, View check boxes, refresh on device change, Save Report, Copy, Properties (record 25 section 3), and the USB branch (section 5) (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-A.4 the eject from the window: record 24's units fed from the query, the confirmation, the call per system, the answers (record 25 section 6) (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-A.5 the tray icon: menus, single instance, events and balloons, the tooltip on 98 SE and ME (record 25 section 7) (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x] 41-A.6 install, upgrade and uninstall in XHCIINST (the copy, the Run value, closing a running copy, `/notray`), the packager's `tools\xhcitr\`, the import allowlist, `test-package.ps1` (record 25 sections 8 and 9) (done 2026-10-16, merged into `p41-xhcitr`; reviewed until converged; `runs/run-41.md`)
- [x ] 41-V.1 legs T.1 to T.6 of record 25 section 11 on 98 SE, ME, 2000, XP, XP x64, Vista x86 and 7 x86 (Vista x64 and 7 x64 with the test-signed package) (read 2026-10-16, `runs/run-41.md`: 2000, XP, XP x64, Vista x86 and 7 x86 pass; Vista x64 and 7 x64 the tool on the earlier signed drivers (fallback) pass; 98 SE and ME: install, login start, uninstall, the USB tree, the Thunderbolt branch, Safely Remove (window, right click, tray, `/eject`) and the ME balloons pass with the tool of `85f3fb90`, which matches the query by name there (98 gives the driver no instance ID); the 98 SE tooltip text unread; the new x64 drivers wait for the re-signed package; the upgrade of a tool in use unread) Done 2026-10-16: Vista x64 and 7 x64 pass with the owner's test-signed final drivers; unread: the approval balloon (the emulator approves at once), the 98 SE tooltip text, the upgrade of a tool in use.
- [x] 41-A.7 Safely Remove for plain USB devices (the owner's decision of 2026-10-16, which reversed decision T6): record 25 revision 7, section 6.5 - a USB device with a disk (or, on NT, a volume) in what stops with it, on any listed controller; never a hub, an input device, a radio or the controller; the short transaction (the shared mutex, the device found again by instance ID, its set and rule read again, a change count read just before the call); the tray's two groups, `/eject id=`, `/report`; host vectors in `test_tr` (done 2026-10-16, branch `tr-usbeject`; the design and the code reviewed until they converged; `runs/run-41.md`, "Plain USB devices")
- [x] 41-V.2 leg T.7 of record 25 section 11 (read 2026-10-16, `runs/run-41.md`, "Plain USB devices": 98 SE, ME, XP and 7 x86 pass - the label with its letter, Safely Remove from the window, Delete, a right click and the tray, the device gone (98 SE, ME) or held at Code 47 (XP, 7), unplug and re-plug with the drive back, the refusal with a file open (98 SE, XP, 7), the UAS disk (98 SE, XP, 7), a tablet, a keyboard and a hub never offered; ME's refusal and the UAS re-plug unread)
- [x] 41-A.8 Start at login, per user (the owner's request of 2026-10-16): record 25 revision 10, section 7.4 - "Start at login" in View and the icon's right-click menu writes `TrayAtLogin` under the user's settings key; a `/tray` start ends at once when it is 0; grey when the machine's Run value is absent; host vectors in `test_tr` (done 2026-10-16, branch `tr-usbeject`, reviewed; read on 98 SE and 7 x86, `runs/run-41.md`)
- [x] 41-A.9 the tree's labels (the owner's request of 2026-10-16): each controller's vendor and PCI id, a Thunderbolt chip's own xHCI named by its NHI, the root hub's ports by protocol, each port's protocol; record 25 revision 10, addendum; host vectors in `test_tr` (done 2026-10-16, branch `tr-usbeject`, reviewed; read on 98 SE and 7 x86, `runs/run-41.md`)
- [ ] 41-A.12 Logging level (the owner's decision of 2026-10-16): record 25 revision 13, section 7.5 - `xhci98.sys`'s `XhciLogVerbosity` and `xhcicm.sys`'s `XhciCmLogVerbosity` (its old `XhciLogVerbosity` read for one release) shown side by side and set 0-4 from the View menu and a controller's or an NHI's right-click, an absent value read as 2 and one out of range shown and never written; a standard user on 2000 to 7 through "Set as administrator..." (`/setlevel`, a second mode of the elevated run of section 6.6); the restart offered; the levels in `/report` and Save Report; it replaces `XHCISNAP -verbosity` and `SNAPON.BAT` (XHCISNAP ships unchanged in `3.0.0.0`); host vectors in `test_tr`; leg T.10 (branch `tr-loglevel`, reviewed; legs `runs/run-41.md`, "Logging level")
- [ ] 41.final `README.md`, the release notes, `source-files.md`, `docs/README.md`; merged into `3.0.0.0` before `40.final`

Checkpoint: design record 25 converged before 41-A's code; the host
suites green with the gates on both architectures; legs T.1 to T.6 read on
the dry cut's `release` flavour; then `40.final`.

---

# Planned work

Not phases yet: each takes the next free phase number when the owner opens
it, and its placeholder ids take that number then (owner, 2026-10-06; the
decisions table).

## Planned - Slipstreaming xhci98 Into a Windows 2000 CD

Goal: Windows 2000 Setup from a CD whose `TXTSETUP.SIF` carries xhci98,
with no F6 floppy.

Status: not opened, unnumbered and unversioned. Deferred by the owner on
2026-10-14, when Phase 38's Windows 2000 F6 entry was merged; the one
attempt is `docs/issues/16-w2k-f6-first-gui-boot-usbd.md` section 3 and
`lessons.md`.

- [ ] SLIP.1 xhci98 slipstreamed into a copy of a Windows 2000 CD (owner, 2026-10-14). One attempt during 38.final, an `[InputDevicesSupport]` entry for `xhci98` in the copy's `TXTSETUP.SIF` with its own file section listing `usbd.sys`, `hidclass.sys` and `hidparse.sys` and the driver in its `I386`, did not load the driver in text mode (no slot addressed, an empty port-0xE9 log from the `qemu` flavour; issue 16, runs 71 and 72); an `$OEM$\TEXTMODE` preinstall was not tried. Read first why text mode did not load it, then decide whether the documents describe a route

## Planned - The Idle Poll Period, 50 Against 500 ms

Goal: `XhciPollIdleMs`'s default decided on a full reading.

Status: not opened, unnumbered and unversioned. Deferred by the owner on
2026-10-14 from task 38.3's legs (T8); the default stays 50. The partial
table is `runs/run-38.md`, "38.3".

- [ ] IDLE.1 `XhciPollIdleMs` 50 against 500 on every target (owner, 2026-10-14): under mode 2, the idle processor load over a minute, plug-to-working at a root port and behind a hub, USB 2.0 remote wake, the timer's holds and releases per minute. The partial reading on 2026-10-14 (XP, XP x64, 2000 and 2000 SMP, Vista x86 and x64) showed plug-to-working 0.3 to 0.7 s at both values, remote wake working at both on the x86 guests (the test client is x86; not applicable on XP x64 and Vista x64), Vista x86's idle host load halved at 500 (12.7 to 5.4 %) and Vista x64's about 30 % lower, and no difference beyond noise elsewhere; 98 SE, ME and 7 x86 and x64 not taken. The default is decided on the full reading

## Planned - Bench Follow-Up After `2.4.0.0`

Goal: on real hardware, what Phase 38 could read only in virtual
machines.

Status: not opened, unnumbered and unversioned. Listed by the owner on
2026-10-14 from what Phase 38's tasks owe after the cut (`runs/run-38.md`,
each task's "Owed after the cut"); the release notes' "Untested ground"
names B24.1 to B24.5 meanwhile. A sixth item, `XHCICLN`'s veto on XP x64,
was read before the cut (task 38.8) and left the list.

- [ ] B24.1 Linux's controller quirks (task 38.10, design record 20) on the controllers they name, each value read on a controller it lists
- [ ] B24.2 a real xHCI 0.96 controller (task 38.11, design record 17 section 4.14): its start at `XhciAccept096` 1 and its refusal at 0, a device of each speed, and audit items A14 and A21
- [ ] B24.3 hidusbf on a composite HID device (task 38.12, design record 13 section 10.14.8): on the composite devnode and on a function's, on 98 SE, 2000, XP and 7 x86
- [ ] B24.4 a High Speed and a USB 3 hub under the `2.4.0.0` ids, `USB\XHCI98_HUB` and `USB\XHCI98_HUB30` (task 38.8, design record 13 section 10.13, D13 and D14), by passthrough or on the bench
- [ ] B24.5 the finer timer while polling, `XhciPollFineTimer` 1, on real hardware (task 38.3, design record 15 section 7.2.4's amendment); its default reconsidered on the reading

## Planned - A Refused Interface's Device Id

Goal: the read-first task deferred from Phase 38 (owner, 2026-10-13).

Status: not opened, unnumbered and unversioned. Opened as 38.12 on
2026-10-13 and deferred to after `2.4.0.0` the same day (owner); the plan,
with the owner's answers to its decisions, is design record 13 section
10.7.1, and Phase 38 corrects the record's and `src\xhci_xport.h`'s claim
to what was read.

- [ ] NOX.1 a refused storage interface's device id on Windows 98 SE and ME (from the 38.2 legs, 2026-10-13; owner, the same day: read first, a task of its own). Record 13 and `src\xhci_xport.h` say a REFUSED interface (no transport it can run, task 29-A) "shows no VID/PID-derived hardware id at all", its one hardware id `USB\XHCI98_NOXPORT&VID_v&PID_p&REV_r[&MI_nn]`, so neither `usbstor.inf` nor a vendor INF can bind it. On 98 SE the refused `usb-uas` devnode's registry `HardwareID` read `"USB\\XHCI98_NOXPORT&VID_46F4&PID_0003&REV_0000,USB\\VID_46F4&PID_0003"` (runtime, `Enum\USB\VID_46F4&PID_0003\P382R2`, `out\p38-2\legs\`): the second entry is the bus's device id (`BusQueryDeviceID`), which the PDO still reports in VID/PID form. Read first, on 98 SE and ME (with NUSB 3.3's stack too, whose `usbstor.inf` names 135 VID/PID lines): whether an INF whose only line is that VID/PID binds a refused device. If it does, the refused PDO's device id takes the `XHCI98_NOXPORT` form as well (its instance path moves, so the design says what becomes of a devnode an earlier version made), checked against 38.9's move of the bus's own ids to `USB\XHCI98_...`; if it does not, record 13 and `xhci_xport.h` say why the device id is harmless there. Either way the claim is corrected to what was read, NT's matching (hardware and compatible ids only) stated as read or as not read. Host tests of the id, both builds, legs on 98 SE, ME and one NT target with a UAS-only device at SuperSpeed on a controller that does not stream

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

## Planned - USB4 Host Recovery After Loss

Goal: a USB4 host interface that stopped answering taken up again by the
driver, with no disable and enable of the controller and no restart.

Status: not opened, unnumbered and unversioned. Listed by the owner on
2026-10-15 with decision U17 (design record 22 section 7.7): `3.0.0.0`
contains a lost USB4 host and does not recover it; the ICM client's
recovery (record 21 section 5.5) is not this item.

- [ ] U4REC.1 recovery of a contained USB4 host (owner, 2026-10-15, decision U17). Under the USB4 profile the first SUSPECT, ABSENT or CONFIG_LOST reading, a surprise removal, or a bind or resume that reads the ring's size register wrong puts the instance in CONTAINED until a PnP stop and start or a reboot (record 22 section 7.7). Design first, in record 22: when access is proven back (the size register and the host router read as programmed, or a full re-read of the PCI side the platform may have reset), what the instance may trust of its port records, how the fabric is found again (section 9.2's discover-and-preserve, never a reset), what becomes of the common buffer kept under `DmaMayRun`, and how `nvme98` and the re-enumeration seam learn of it; then host vectors and the legs on `usb4-generic` with the emulator's root hot-plug modes (`EMU.2`) on 98 SE, 2000 and one later NT target

## Planned - `nvme98` Link-Down Statements on Real Hardware

Goal: the link-down statement of tasks 39-A.7 and 40-A.7 made on real
tunnel ports, not only on the emulator's.

Status: not opened, unnumbered and unversioned. Listed by the owner on
2026-10-15: `3.0.0.0` makes the statement on the emulator's tunnel
bridges only and withholds it on hardware (record 21 sections 7.1 and
7.2), until `nvme98`'s own hardware qualification, its MD-2, has resolved
the completed-reset boundary this driver cannot observe through a down
link.

- [ ] NVHW.1 enabling `nvme98` link-down statements on real hardware after `nvme98`'s MD-2 (owner, 2026-10-15): once `nvme98` reports MD-2 qualified (the P14s with an NVMe enclosure and with one that has an internal switch), the statement's hardware condition per profile - the tunnel port's Data Link Layer Link Active bit where a PCI Express capability exists, the USB4 profile's path-closed and reset anchor - written into records 21 and 22, the withholding lifted per generation that was read, and the legs on the bench; the release notes' state of the pair updated

## Planned - Safe Eject Across a Stack Rebuild on NT

Goal: a device behind a tunnel stays offered for safe removal after its
driver stack is rebuilt.

Status: not opened, unnumbered and unversioned; to be solved after
`3.0.0.0` (owner, 2026-10-15, confirmed 2026-10-16). Found by the Windows
2000 legs of 39-V.2 on 2026-10-15. Read again on 2026-10-16 with a bridge
chain behind the tunnel port on 2000: the first plug of a device Windows
has not seen before installs its driver, the stack is rebuilt, and the
unit reads not removable, so neither Windows' applet nor `INSTALL /eject`
offers it; a plain unplug then brings up the Unsafe Removal dialog. From
the second plug on it is offered and stops cleanly. `3.0.0.0` ships
instead: `XHCITR` and `INSTALL /eject` fall back to the configuration
manager's query-and-remove for such a unit (administrators only; a
standard user is told to re-plug once), and the release notes state the
behaviour (record 21 section 7.4, records 24 and 25).

- [ ] EJRB.1 the child filter after a stack rebuild (owner, 2026-10-15). A disable and enable of the device, or its driver installed or updated, rebuilds its stack from the registry with no `BusRelations` query, so the child filter that sets `Removable` (record 21 section 7.4) is not attached again and the device drops out of "Unplug or Eject Hardware" and "Safely Remove Hardware" until a re-plug. A kernel-only fix is not small: the filter would have to be registered on the child devnode as a device-specific `LowerFilters` value, in the Enum key the PnP manager owns, written through SetupAPI or CfgMgr32 from user mode (the device tool of design record 24 is the natural place), and keeping a filter attached across a non-surprise `IRP_MN_REMOVE_DEVICE` is not permitted. The writer needs administrator rights on 2000 and XP, so a privileged user-mode piece (a service, told of each new unit by the driver) rather than the unprivileged tray tool; a value left behind that names a filter no longer installed stops the device from starting (Code 39), so its removal on the device's departure, on uninstall and after a failed upgrade is part of the design; only the unit at the top of each tunnel port, never another PCI device. Design first (record 21 section 7.4 and record 24), with the removal of that value when the device leaves; legs on 2000, XP, Vista and 7, x64 included. Read on 2026-10-16 (`runs/run-41.md`, "Bridges behind the tunnel port on NT"): the case of a bridge child that was never attached at all, the ACPI bus filter already on a new PCI-to-PCI bridge's PDO on XP and 7, is fixed in 3.0.0.0 by record 21 section 7.4 revision 2. The rebuild case of this task remains, and is now also read on a first plug: the first plug of a never-seen unit is not listed on 2000 or XP, because Windows installs the bridge's driver for the first time and rebuilds its stack. Windows 7 x86 listed it. The tool-side query-and-remove fallback covers it until then

## Planned - `nvme98` Interface 3 on Windows 98 SE and ME

Goal: the Windows 98 SE and ME half of the `nvme98` link-state channel
read by `nvme98`, under the interface `nvme98` adopts.

Status: not opened, unnumbered and unversioned. Waits on `nvme98`'s
adoption message for its interface 3 (relayed by the owner on
2026-10-15): `nvme98` accepted the `HKEY_LOCAL_MACHINE` locator record and
the `REMOVE_PENDING` layout, and is to name the boot proof's key and the
9x deadline; nothing is built against interface 3 before that message.
The signed `3.0.0.0` cut waits for `nvme98` `0.2.0.0` (owner, 2026-10-15),
after which the NVMe legs are read again with it.

- [ ] NV9X.1 `nvme98` interface 3 adopted on 9x: the boot-proof dynamic key (`_RegCreateDynKey`, its provider in `xhcicm.sys`, the key deleted at the driver's unload, holding the record's cookie; the key path and value name `nvme98` gives), the acknowledgement deadline on 9x (`CM_NVLINK_ACK_9X_MS`, 3000 ms provisional, set against `nvme98`'s gone deadline once it is given), `nvme98cm.h` taken again unchanged from `nvme98`, record 21 section 7.2 and record 22 section 10 updated; host vectors, and the legs on 98 SE and ME with the `nvme98` release that reads it
- [ ] NV9X.2 per-port, non-blocking `nvme98` acknowledgement and removal waits, with NV9X.1 (asked for by `nvme98` with its interface 3, relayed by the owner on 2026-10-15). Today `CmNvlinkAwait` holds the domain's connection-manager thread for up to its bound (5 s on NT, 3 s on 9x), and the 9x `REMOVE_PENDING` wait holds that domain's CONFIGMG worker, so a second port's statement and re-enumeration wait behind the first. It matters only where statements are made - in `3.0.0.0` the emulator's tunnel bridges alone, and under the ICM client never in a set of two or more members. The change: a per-entry pending state with its deadline, polled from the connection-manager thread's pass, and a per-member pending removal on the worker; record 21 section 7.2 and record 22 section 10 first, host vectors, and the two-port legs. Estimated at six to seven hours with its reviews

## Planned - The Emulator Legs After `3.0.0.0`

Goal: the virtual-machine legs that need `usb4-emu` builds landed after
this project's cut froze its emulator binary (`5ba7f60`).

Status: not opened, unnumbered and unversioned. Listed on 2026-10-15 from
the `usb4-emu` maintainer's notes relayed by the owner: the TB3 dock
preset on a USB4 host and the P14s behaviour landed on its `main`
(`c7031a5`) after the freeze, and the root hot-plug modes are its part 2.

- [ ] EMU.1 Thunderbolt 3 device routers behind a USB4 host (task 40-A.8): the emulator's TB3 dock preset on `usb4-generic` (the i440FX command line of `usb4-emu`'s `XHCI98-HANDOFF.md` section 7.1) on 98 SE and 2000, the PCIe tunnel through the legacy router and the DROM by bit-bang (98 SE and 7 x86 read on 2026-10-16 with `bin-2a893ea`, `runs/run-41.md`: the router's USB4 Version by its major, its upstream adapter from the read's response and its DROM by bit-bang were needed and are in; the PCIe tunnel, the NVMe disk, the name, Safely Remove, unplug and re-plug pass; 2000 owed)
- [ ] EMU.2 the R32 power-off read again with the emulator's root hot-plug modes once its part 2 lands: `acpi` on 2000 with ACPI, XP and 7, `restore` on 98 SE (its image is APM-only); the ICM client's ABSENT and recovery and the USB4 profile's containment against a PnP removal and a restored bridge
- [ ] EMU.3 the P14s behaviour on the Alpine Ridge profile: Assist decided at power-on, pre-boot tunnelling every device, Assist with pre-boot and a USB4 device (single lane, flags `0x88`, configuration requests held about 2.77 s after the first `driver ready`), `driver ready` flags `0x22`, the power-off delays, and pending approvals cancelled by an unplug, a re-plug or mailbox `0x06` (a stale `approve` answered with the error flag, taken as benign)
- [ ] EMU.4 the ORICO enclosure's ASM2464 on the Alpine Ridge profile: approved and tunnelled, and with `XhciCmApprove` 0 its fall back to USB after about 9.4 s, the disk then on the controller's own xHCI under `xhci98.sys`
- [ ] EMU.5 the `39-V` Alpine Ridge legs read again on the new emulator build, whose behaviour changed under them

## Planned - The x64 Legs Owed at the Cut

Goal: the Vista x64 and Windows 7 x64 readings Phases 39 and 40 could not
take for want of a test-signed package.

Status: not opened, unnumbered and unversioned. Listed on 2026-10-15:
39-V.3 and 40-V.3 read XP, XP x64, Vista x86 and 7 x86, and the
installer's legs passed on every system but two, because the amd64
package needs the project's test signature, applied only at a signed cut
(design record 19). Struck from this list as the signed cut reads them.

- [ ] X64.1 39-V.3 and 40-V.3 on Vista x64 and 7 x64 from the signed `3.0.0.0` asset: the install with the forced filter, one plug, unplug and re-plug cycle with the NVMe drive, and the safe-eject cycle (read 2026-10-16 on the owner's pre-cut test-signed package, pass on both, run-39.md and run-40.md; the recipe stands, the reading on the cut's signed asset is still owed)
- [ ] X64.2 the installer legs owed by task 40.5 onward: Vista x64's update read after its restart, and 7 x64's uninstall over `3.0.0.0` (both pass 2026-10-16 on the pre-cut test-signed package, run-40.md; again on the cut's signed asset if its drivers differ)

## Planned - An Unsafe-Unplug Notice From the Child Filter

Goal: the owner asked for a notice when a Thunderbolt or USB4 device is
unplugged without being stopped. The tray of design record 25 cannot
prove it by polling (record 25 section 7.2, decision T9). On NT the child
filter sees a unit's surprise removal; publishing a count of those, with
the router's key, in the query of record 25 section 4 would let the tray
say so with authority. 98 SE and ME need their own source.

Status: not opened, unnumbered and unversioned.

- [ ] UNPL.1 the child filter's surprise-removal count and last router key in the query answer (a new `CM_Q_VERSION`), and the tray's notice

## Planned - The Query's First Answer After an Inert Start

Goal: record 25 section 14: after a start with the connection manager
off, the query's first answer can lack the inert flag until the thread's
first pass. Harmless for an eject (the stage refuses it); the answer
should still be right from the first byte.

Status: not opened, unnumbered and unversioned.

- [ ] QINR.1 seed the query device from the thread's actual wait state, or expose it only after the first publication

## Planned - A Sign-Only Packaging Mode

Goal: `make-package.ps1 -TestSignPfx` re-runs the host test suite and the
import gate that `build-driver.cmd` already ran on the same binaries, so
the owner's signing step takes minutes on a loaded host. A sign-only mode
refuses unless each binary's SHA-256 equals the one a gated
`build-driver.cmd` run recorded for it, skips the repeated suites, still
runs the INF gate against the staged package, and signs. The guarantee
that only a tested binary is signed stays; the time goes. Agreed by the
owner on 2026-10-16, after the `3.0.0.0` cut.

Status: SIGN.1 done 2026-10-16; unnumbered and unversioned. The positive
signing run is the owner's, who holds the key.

- [x] SIGN.1 `build-driver.cmd` records the gated binaries' hashes; `make-package.ps1 -SignOnly` checks them, runs the staged INF gate and signs; `test-package.ps1` covers a mismatched hash (done 2026-10-16: `out\gated-<arch>-<flavour>.txt` written by `scripts\package\write-gated-manifest.ps1` under a per-architecture lock held for the whole `build-driver.cmd` run, from each binary's hash taken right before and right after its own import gate (the two must agree) and re-checked once the flavour's last gate has passed, clean only if the tree is still as it was when the run started; `-SignOnly` reads each binary once and signs and stages that copy; it refuses without `-TestSignPfx`, without a record, for another architecture or flavour, for any staged binary not of its recorded SHA-256, and unless the build and the tree are both clean at one commit; it skips the host suite and the import gates and keeps the flavour markers, every INF gate, the staged package's gate, the version checks and the signature checks; `test-package.ps1` covers the parse and each refusal on fixtures, and after a gated x86 `build-driver.cmd release` the record admitted the built binaries and refused one with a byte appended; `build-and-test.md`, "Since 2.3.0.0 the x64 package is test-signed")

## Planned - Stor98, the Renamed NVMe Driver

Goal: the NVMe driver project nvme98 becomes Stor98 (`stor98`, a new
repository) before its first public release, `0.3.0.0`, with interface 3.
`3.0.0.0` bundles no NVMe driver (owner, 2026-10-16). Once Stor98
`0.3.0.0` and its change list (`RENAME-STOR98.md`) arrive: the link-state
channel's names and signatures in `xhcicm.sys` (the NT callback, the 9x
locator and dynamic key, the interface header and its version), the
release scripts' bundling inputs and layout, the installer's skip of the
bundled driver, the README and release notes, then the NVMe legs with
Stor98's receiver, a re-signed x64 package and a release that bundles it.

Status: not opened, unnumbered and unversioned.

- [ ] STOR.1 the rename in `xhcicm.sys`, the scripts, the installer and the docs, from the change list; interface 3 as adopted (the channel and the installer, 2026-10-16: `src/cm/stor98cm.h` in place of `nvme98cm.h`, unchanged from Stor98 `0.3.0.0`; interface version 4, `S98L` and `STOR98CM`, `\Callback\Stor98LinkState` on NT; on 98 SE and ME no channel is offered (interface 3's proof key is NV9X.1, and without it the locator is not written either, so nothing is published or awaited there and Stor98 runs on its gone deadline); records 21 section 7.2 and 22 section 10; `XHCIINST` skips `stor98*` files and never takes an INF that installs the `stor98` service or names `stor98.mpd` as its own, whatever its Provider; host suites pass)
- [ ] STOR.2 bundle Stor98 `0.3.0.0`; NVMe legs (link up and down, the 9x clean-eject handshake, ME write-through); the release (legs read 2026-10-16 on Stor98 `0.3.0.1`; `3.0.0.0` will bundle Stor98 `0.4.0.0` (owner, 2026-10-16), and the legs are owed again on it, as is the release: a dry x86 TEST cut bundling `0.3.0.1`; on `tb3-alpine-ridge`, `bin-2a893ea`, 98 SE, ME and 2000 installed Stor98 from `release-x86\`, wrote and compared a file, stopped it with XHCITR's Safely Remove, and survived an unplug, a re-plug and a surprise unplug with the file intact; ME's write-behind showed its Disk Write Error on an unplug without an eject and the data Stor98 had taken was on the disk. No 9x channel is offered in `3.0.0.0`, so there is no 9x clean-eject handshake (NV9X.1); on 2000 `xhcicm.sys` published the channel and made its statements, and Stor98 `0.3.0.1` never queried it (its NT receiver is a stub), so nothing was acknowledged. `runs/run-41.md`, "Stor98 behind the Thunderbolt 3 controller")

## Planned - Installing the Driver on Windows 98 SE and ME

Goal: `INSTALL.BAT` on 98 SE and ME puts the driver on the controller and
its root hub itself, as it does on 2000 and later, in place of the
Update Driver steps the user follows by hand (design record 23 section
6.1: no routine of these systems installs a driver on a device, and the
16-bit `SETUPX` engine is not driven through anything undocumented).
The Windows 95 and 98 DDKs document `SETUPX`'s device installer functions
for class installers (`DiCreateDeviceInfo`, `DiInstallDevice` and the
rest), so a small 16-bit helper may do what Update Driver does. Asked for
by the owner on 2026-10-16, after an update from `2.4.0.0` on 98 SE left
the driver as it was until the hand steps were done. Not taken (owner,
the same day): overwriting the installed binaries in `system32\drivers`
in place, as NUSB does with Windows' own USB files; the device keeps the
wizard's record of the old INF, and an INF change never reaches it.

Status: not opened, unnumbered and unversioned.

- [ ] DI9X.1 a design record: which `SETUPX` functions, as the DDKs document them; the 16-bit toolchain; the helper's interface to `XHCIINST`; the controller, its root hub and the Thunderbolt controller; a USB keyboard and mouse behind the controller being updated; what the wizards' own bookkeeping then holds; the uninstall
- [ ] DI9X.2 the helper and `XHCIINST`'s use of it, with the legs on 98 SE and ME (a fresh install, an update from `2.4.0.0` and `2.3.x`, the uninstall)

## Planned - A Thunderbolt 3 Dock's Name After a Resume

Goal: a Thunderbolt 3 device router behind a USB4 host keeps its name
when the connection manager adopts it at its start or rebuilds it on a
resume, as it has after a plug.

Status: not opened, unnumbered and unversioned. Listed on 2026-10-16 with
task 40-A.8 (record 22 section 8): the router's DROM is read by EEPROM
bit-bang only on a plug; under a resume budget the read is skipped, and a
router found tunnelled at the start is adopted without it, so the tool
shows the dock unnamed until a re-plug. The dock works either way.

- [ ] TB3NAME.1 the DROM read for an adopted or resumed Thunderbolt 3 router, outside the resume budget (deferred to the connection-manager thread once the resume is done); host cases on the fake fabric; legs on 98 SE and 7 x86 with the dock attached across a suspend and at power-on

## Planned - A Tunnelled Function With No Interrupt on Windows 98 SE and ME

Goal: a function behind a tunnel that Windows 98 SE or ME can give no
IRQ still starts, so that a driver that polls (Stor98's planned polling
mode) can run it.

Status: not opened, unnumbered and unversioned. Listed on 2026-10-16 by
the owner, after the hot-plug fix of 9x bridges (record 21 section 6,
`XhciCmBridgeNumbering`) read on the owner's ThinkPad P14s Gen 1: there
the IRQ the firmware routes is provable and set, but on a machine whose
routing gives no provable IRQ for the tunnel's root port, or for a
function with no interrupt pin (MSI only, which 9x cannot use), Windows
98 disables the function (Code 29) before its driver loads.

- [ ] NOIRQ.1 a design record: how a 9x function with an interrupt pin and no routable IRQ can be started without one (its logical configuration, CONFIGMG, what PCI.VXD requires), with only documented means; the pairing with Stor98's polling mode (Stor98 `docs/PLAN.md`); legs on 98 SE and ME with the IRQ withheld
