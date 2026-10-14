# Development Roadmap - USB4 and Thunderbolt 3, the Connection Manager Driver

The fourth roadmap file, opened on 2026-10-14 at the project owner's
request, the day Phase 38 closed on the `2.4.0.0` cut. It carries the
connection-manager work: a driver beside `xhci98.sys` that is a client of
an Intel Thunderbolt 3 controller's firmware connection manager first, and
a USB4 software connection manager after it, so that the PCIe devices
behind a Thunderbolt 3 or USB4 tunnel - a dock's xHCI controller, an NVMe
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

**Status: nothing is opened.** Two pieces of planned work are drawn up
below with placeholder ids, `TB3` and `U4CM`, in the order the `usb4-emu`
handoff gives (its section 2, maintainer 2026-10-14): the Thunderbolt 3
ICM client with tunnel re-enumeration first, because it can be proven on
the owner's ThinkPad P14s Gen 1; the USB4 software connection manager
second; real USB4 devices third, once the ORICO enclosure capture exists.
The branch `3.0.0.0` exists, so the first release of this file is read as
`3.0.0.0` unless the owner says otherwise. Design record 21 is the `TB3`
plan, a draft before review and before the owner's decisions.

## The sources this roadmap rests on

The test bed and the hardware contract are the `usb4-emu` repository's
(`D:\OneDrive\git-projects\usb4-emu`, branch `main`), which this project
reads and never modifies, and whose QEMU processes it never stops:
`docs/XHCI98-HANDOFF.md` is the entry point and names the rest
(`ICM-PROTOCOL.md`, `PLATFORM-MATRIX.md`, `REENUM-EXPERIMENT.md`,
`WIN98-TEST.md`, `ARCHITECTURE.md`, `NVME-DRIVER-EVAL.md`, `tools/usb4cm`,
`scripts/run-win98.sh`). The NVMe driver behind the tunnel is `nvme98`
(`D:\OneDrive\git-projects\nvme98`, `docs/PLAN.md`), read-only here too;
what it asks of this connection manager is design record 21 section 7.
Every virtual-machine leg in this file runs with this project's own
overlays under the git-ignored `vm\usb4\` and its own QMP and VNC ports,
47900 to 47999 (record 21 section 10.2).

## The decisions this roadmap rests on

Design record 21 section 11 lists the thirteen the `TB3` work needs (one
binary or two, the NT filter's install route, polling with no interrupt,
the approval policy, the absent controller, the `nvme98` channel, the
version and cut, the names, the other Alpine Ridge ids, safe eject, the
master switch, the log emission, cut before the bench), each with a
recommendation; the owner's answers are recorded here when taken, as
`roadmap-hcd.md`'s table records its phases'.

| Decision | Taken |
|---|---|
| Generic by best effort | Owner, 2026-10-14: the Thunderbolt 3 and USB4 work is made as generic as the evidence allows. Record 21 section 1.4 is the answer: the host interface and re-enumeration are generic; the ICM client is generic across devices and per-generation across the Intel firmware-ICM hosts it addresses, a shared lifecycle over a per-generation table of protocol operations and capabilities (Titan Ridge, for one, has no `get route`), with the Alpine Ridge row filled and each further row waiting on a capture; real USB4 hosts are the software connection manager (`U4CM`), and an unknown firmware connection manager is unsupported |
| Approval policy (D4) | Owner, 2026-10-14: `XhciCmApprove` 0 and 1 only, 1 the default; nothing is remembered across reboots, the firmware and the boot ACL holding the state that matters; "approve known devices only, from a hand-filled UUID list" is a later option, not a value (record 21 section 5.4) |
| A master switch (D11) | Recommended, not yet taken: `XhciCmMode`, default 1 (auto), 0 off, 2 the ICM client on an Intel id the profile table does not list, at the user's own risk and with that id added to a copy of the INF by the user since no registry value can bind a driver to an unmatched id, 3 reserved; the install itself is the opt-in, so the default is on (record 21 section 8) |

## Phase sequence

`TB3` is the Thunderbolt 3 ICM client and tunnel re-enumeration, released
on its own. `U4CM` is the USB4 software connection manager, the next
release after it. Real USB4 devices follow the ORICO capture and are a
task of `U4CM` until they need more. The order is the handoff's: the
Thunderbolt 3 client is one to two thousand lines and provable on real
hardware the owner holds; the software connection manager is four to
seven thousand and provable, for now, only against the emulator and a
Framework 13 capture.

---

## Planned - Thunderbolt 3: the ICM Client and Tunnel Re-enumeration

Goal: a Thunderbolt 3 dock or enclosure plugged into an Alpine Ridge
machine after boot, or with pre-boot support off, gets its PCIe tunnel and
its devices appear and disappear under Windows 98 SE and Windows 2000: a
client of the controller's firmware connection manager (the ICM) as a WDM
driver of the host interface, and the re-enumeration of the tunnel bridge's
bus after each change. The first of the three steps the `usb4-emu`
handoff orders (`D:\OneDrive\git-projects\usb4-emu\docs\XHCI98-HANDOFF.md`
section 2, maintainer 2026-10-14): this, then the USB4 software connection
manager (`U4CM`, below), then real USB4 devices once the ORICO enclosure
capture exists.

Status: not opened, unnumbered. Drafted 2026-10-14 from the handoff, the
`ICM-PROTOCOL.md` contract, the re-enumeration experiment and the P14s
captures; design record 21 is the plan, before review and before the
owner's decisions (its section 11, D1 to D13; D4 taken). The branch `3.0.0.0` exists,
so the version is read as `3.0.0.0` unless the owner says otherwise (D7).
Vehicle: the `usb4-emu` QEMU fork's `tb3-alpine-ridge` profile through
`scripts/run-win98.sh` (`BIOS=`, `DEVICE_AT_BOOT=`) and the Alpine Ridge
tree on the Windows 2000 guest, read-only in that repository, with this
project's own overlays under `vm\usb4\` and ports 47900 to 47999 (record
21 section 10.2); the owner's P14s Gen 1 and TS3 Plus dock for the bench.

Why a phase: a second new binary (`xhcicm.sys`) with its own INFs, gates
and package rows; a new hardware contract read from captures rather than a
specification; two operating-system mechanisms (CONFIGMG on 9x, a bridge
filter on NT) no file here has used; and a release.

Batches: `-0` static, `-A` host, `-V` the VM legs, `-E` the bench, and
`.final`. Placeholder ids `TB3-0.n`, `TB3-A.n`, `TB3-V.n`, `TB3-E.n` and
`TB3.final` take the phase's number when the owner opens it.

- [ ] TB3-0.1 design record 21 reviewed to convergence and the owner's decisions D1 to D13 taken and written into it, before any code (as records 14, 15, 18 and 20 were); D4 taken 2026-10-14
- [ ] TB3-0.2 the references: the USB4 Specification 2.0 and Connection Manager Guide 2.0 rows (the USB-IF November 2025 bundle, URL and SHA-256 from `usb4-emu/docs/REFERENCES.md`) in `docs/references/README.md`, fetched per machine, never tracked; the ring register offsets this driver uses transcribed into `tbt_regs.h` from the specification's section 12 and `tools/usb4cm/core/u4regs.h`, each with its citation
- [ ] TB3-0.3 the Windows 98 DDK fetched and recorded (name, version, SHA-256) in `docs/references/README.md`, never tracked, and the CONFIGMG service numbers of record 21 section 6.1 read from its `CONFIGMG.H` service table and written into `cm_cfgmg.h` with that citation; a `legal-provenance.md` row saying the numbers alone were taken. The experiment's run-time guards (`Get_Version` high byte 4, the root id read back) stay in the shipping driver regardless
- [ ] TB3-0.4 the NT filter's install route (D2): read on the Windows 2000 guest, then XP and 7, whether a separate NT-only INF (`$Windows NT$` signature, an undecorated Models section, since Windows 2000 reads only that one) with a line for `PCI\VEN_8086&DEV_15C0&CC_0604` can add a device-specific `UpperFilters` value to a port `pci.inf` serves without displacing its install (an include-and-needs section, or another shape), or whether a class filter is the only route and must be boot-start; and on 98 SE and ME that the engine ignores that INF when it sits beside the other. The answer fixes record 21 sections 3.2, 3.3 and 6.2
- [ ] TB3-0.5 the provenance rows: `ICM-PROTOCOL.md` as the clean-room source (one `legal-provenance.md` section 4 row per fact class, method `static`, the capture run ids behind each [obs]); `usb4cm`'s MIT files lifted with their notices (section 2a); and the rule written into record 21 section 9 and `AGENTS.md`'s reference list that Linux's `drivers/thunderbolt` is not opened for this work
- [ ] TB3-0.6 `XHCIQUAL` dumps the BIOS `$PIR` table in its routing report (`xhciqual\route.c`, host suite `test_route`), so the bench can read whether the P14s BIOS lists the Thunderbolt root port `00:1c.4` (`WIN98-TEST.md`, "Hot-added NVMe gets no IRQ behind 00:1c")
- [ ] TB3-0.7 the launch lines of `usb4-emu`'s local Windows 2000, XP x64 and 7 x64 setups read from their `vms/*/RESULTS-*.md` and `tools/reenum-test/run2k.sh` (`TOPO=ar`), and this project's own launcher written under `scripts\` for its overlays and ports (record 21 section 10.2's rules: never a modification of `usb4-emu`, never a QEMU process this project did not start); whether `usb4-device dock=on` is available in the tb3 dialect on i440FX for V.1.8; and which of the handoff's three workarounds for an NVMe present at POST (`BootGUI=0` then `win`, `-boot strict=on` with a boot index, a SeaBIOS without NVMe) lets the pre-boot legs V.1.3 reach the Windows 98 desktop
- [ ] TB3-A.1 the scaffold: `src\cm\` with its `sources`, the three flavours, the NHI INFs `xhcicm.inf` and `xhcicm-amd64.inf` and the NT-only filter INFs `xhcicmf.inf` and `xhcicmf-amd64.inf`, `xhcicm-imports.allow` and `-amd64.allow`, `check-cm-inf.ps1` with its self-test and the `CM-NO9XBRIDGE` rule (no bridge id in any `$CHICAGO$` INF of this driver; the filter INF held to `$Windows NT$`), `build-driver.cmd` building and gating it after `xhciuas.sys` on x86 and amd64, `make-package.ps1` staging it in every flavour directory with its footprint rows and test-signing the amd64 half, `xhci_version.h` shared; a driver that installs on the NHI and starts with no hardware access
- [ ] TB3-A.2 the host interface, pure: `tbt_ring.c` and `tbt_regs.h` lifted from `u4hi.c` and `u4regs.h` (MIT, notices kept), ring 0 TX and RX in raw mode, CRC-32C, the control-packet read with sequence numbers, timeouts and retries; host suite `test_tbt_ring` with the specification's Appendix A.2 vectors, wrap, exhaustion and a frame over its buffer
- [ ] TB3-A.3 the ICM client, pure: `tbt_icm.c`, the frames and the state machine of record 21 sections 5.1 to 5.6 (bind, driver ready, device connected with the name block, get route, approve, device disconnected, the resume refresh, the 500 ms reconciliation, timeouts, retries and the error flag, an event decoded inside a command wait), behind the per-generation message table of section 1.4 with the Alpine Ridge row; host suite `test_tbt_icm` with every observed frame `ICM-PROTOCOL.md` quotes and a disconnect landing during a 10 s approve
- [ ] TB3-A.4 the NHI function driver: PnP and power (the power policy owner's sequence of record 21 section 4.4 after `hcd_power.c`: the system IRP requesting the device IRP, the D0 IRP retained until the thread's reinitialisation finishes, one `PoStartNextPowerIrp` before each completion, never a wait in dispatch; the unbind, shutdown and system-suspend sequences of section 5.6 in their captured order), the profile chosen from the matched id at `AddDevice` and confirmed by the register checks after start, BAR0 mapped, Bus Master set through the PDO's config write, the firmware status, mode and mailbox handshakes, the common buffer below 4 GB from `cm_pool.c` alone, the connection-manager thread and its queue, polling (`XhciCmPollIdleMs`), the master switch `XhciCmMode` and the other registry values of record 21 section 8 written by every INF path, the counters and the log ring with its emission as D12 decides
- [ ] TB3-A.5 re-enumeration: the tunnel bridge found by the pure `tbt_topo.c` over `HalGetBusData` accessors, with the PCI Express Link Status and Slot Status decoders and the SHPC presence read (host suite `test_tbt_topo` with the P14s and emulator layouts, the generic layout, the no-unique-pick case, a port without the capability bits, and `XhciCmTunnelBridge`); on 98 SE and ME the CONFIGMG route of section 6.1 through the dynalink thunk with both guards; on NT the filter role of section 6.2 with the registration list's publish, withdraw and rundown protocol of section 3.2; the port poll before a tunnel up and the port reading before a tunnel down (section 6.5); `XhciCmReenum` 0 disabling both
- [ ] TB3-A.6 the approval policy `XhciCmApprove` (D4, taken) and the device table of section 5.3, with the release notes' security paragraph and its "nothing is remembered" line drafted beside it
- [ ] TB3-A.7 the `nvme98` link-down notification (record 21 sections 7.1 and 7.2, D6), once `nvme98` has chosen its receiving interface: the NT `IOCTL_SCSI_MINIPORT` route and the 9x VxD-service route, the recipient resolved and cached at tunnel up, each statement sent only after the tunnel port's Data Link Layer Link Active reads 0 and before any removal call, from inside a command wait when the disconnect lands during one, inside the end-to-end budget of record 21 section 7.2 (the idle poll clamped to 250 ms while enabled); the statement withheld on every OS until two qualifications exist: gate G3's ordering against the emulator's stage-2 event (on i440FX through the documented stage-3 surprise-removal presence change, or on q35 through a real Link Active bit), and a hardware reading of the completed-reset boundary on the P14s with an NVMe enclosure and with one that has an internal switch (MD-2's own verification case). Not a condition of the checkpoint; the release notes say which state the pair is in
- [ ] TB3-V.1 Windows 98 SE, the eight legs of record 21 section 10.2's table on `tb3-alpine-ridge` (`OFF_OFF`, `OFF_ON`, `ON_OFF`, `ON_ON`, device at boot and not): install, approve and the NVMe volume with no Refresh; `device_del` and `device_add`; the boot-tunnelled device left alone, on an overlay carrying TB3-0.7's POST workaround; the empty table with the controller present (`OFF_OFF`, `OFF_ON`); the absent controller (`ON_OFF`, `ON_ON`, nothing attached); disable and enable; `XhciCmApprove` 0 and `XhciCmMode` 0; the dock's xHCI under `xhci98.sys` where the dialect offers it; every leg with `-trace 'usb4_*'` and the `qemu` flavour's log
- [ ] TB3-V.2 Windows 2000, the same table on the Alpine Ridge tree (`TOPO=ar`), plus the filter's install through its own INF, the boot question of TB3-0.4, S3 where the guest offers it, and on the SMP guest under Driver Verifier a filter disable concurrent with tunnel work (the rundown of section 3.2)
- [ ] TB3-V.3 ME, XP, XP x64, Vista x86 and x64, 7 x86 and x64: the install and one plug, unplug and re-plug cycle each. These tiers carry no checkpoint tax: the legs are read and recorded, a failure on one is a release-notes line and not a checkpoint failure, and the install leg from the asset that every release requires on every target is TB3.final's; on 7 x64 whether `pci.sys`'s own hot-plug acts before the filter is a reading, not a fault
- [ ] TB3-V.4 the `usb4-generic` profile (class `0x0C0340`, no ICM) left unclaimed: the INF does not match it and nothing is logged, read on 98 SE and 2000
- [ ] TB3-E.1 the owner's P14s Gen 1 with the TS3 Plus under Windows 98 SE, after the cut: Assist off, pre-boot off, the package installed, the dock approved and its tree up, its xHCI controllers under `xhci98.sys` with a stick and a keyboard, `XHCISNAP` on each; unplug and re-plug at the desktop; pre-boot on, the tree at boot and then a hot-plug cycle; the DebugView emission at a disable (record 21 section 10.4)
- [ ] TB3-E.2 the P14s `$PIR` dump from TB3-0.6, and its consequence written into the release notes: whether a function hot-added behind the tunnel is started at all on Windows 98 (Code 29 is a devnode `PCI.VXD` never started, which no polling below it mitigates; `WIN98-TEST.md`, "Hot-added NVMe gets no IRQ behind 00:1c"), and if not, that pre-boot tunnelling is the route on that machine; and separately whether a controller that does start without a usable line runs under `xhci98.sys`'s polled delivery
- [ ] TB3.final the documents (release notes with the security paragraph and the "nothing is remembered" line, the limitations - force power, safe eject, the Unsafe Removal dialog on NT, the `nvme98` channel's state - and "Untested ground"; `README.md`; `source-files.md`; `docs/README.md`'s table), the builds, the gates, the dry cut and the owner's signed cut of `3.0.0.0`, with the install legs read from the asset on every target

Checkpoint: record 21 converged and the owner's decisions taken before the
first `-A` task; the three host suites green with the import and INF gates
on both architectures; TB3-V.1 and TB3-V.2 read as their tables name them
on the `qemu` flavour and then the `release` flavour of the dry cut, with
the `usb4_*` trace showing the client's frames as `ICM-PROTOCOL.md` section
6 describes the real firmware's conversation; TB3-V.3 (no tax: read and
recorded, not a condition) and TB3-V.4 read; the cut, with the install leg
from the asset on every target; then TB3-E.1 and TB3-E.2 read by the owner
after the cut, a failure there a re-cut - an exception to "no phase
advances past an unobserved checkpoint" that is decision D13's to grant,
as Phases 28 to 31 and 35 each had it granted, and that without which the
bench comes before the cut. "Observed on both" is the Windows 98 SE bench
and the Windows 2000 VM leg, as for every HCD phase.

Records: design record 21; `docs/references/README.md` (the USB4 bundle
and the Windows 98 DDK rows); `legal-provenance.md` (the `ICM-PROTOCOL.md`,
`usb4cm` and DDK rows); `runs/run-<phase>.md` when opened; the `usb4-emu`
documents record 21's "Sources" lists.

## Planned - USB4: the Software Connection Manager

Goal: real USB4 hosts, all vendors, where the operating system is the
connection manager: the second step of the handoff's order, after the
Thunderbolt 3 work above. Contract: the USB4 Specification 2.0 and
Connection Manager Guide 2.0, `usb4-emu/docs/ARCHITECTURE.md` sections 4, 5
and 7 and `WINDOWS-DEBUG.md` ("What the Windows CM required of the model").
Start from `usb4-emu/tools/usb4cm/core` (MIT, C89, polling, 32-bit DMA,
about 1.3 thousand lines; it brought a tunnel up from the Windows 98 DOS
prompt on 2026-10-14), in the same binary as the ICM client with a profile
switch at `AddDevice` on the NHI's class code and ids (record 21 section
13). Test on `usb4-generic` with both host models (`minimal`, `intel-adl`)
and with `preos-cm=on` (the pre-OS tunnel cleanup the Windows 11 CM
performs). Write it to depend on no exact device router: every router
operation possibly unsupported, adapter numbers discovered from the
capability chain and the PCIe Downstream Entry Mapping operation, every
completion polled with a timeout. The handoff's size estimate is 4 to 7
thousand lines. Under a software CM this driver tears paths down itself, so
`nvme98`'s obligation 2 (record 21 section 7) becomes real work here: the
link-down statement only after the path is closed and the endpoint's reset
has completed.

Status: not opened, unnumbered and unversioned; a design record of its own
when opened. Nothing in it waits for the ORICO USB4 enclosure capture, which
adds a real device-router preset and a conformance check later.

- [ ] U4CM.1 the design record: the connection manager's state machine from `u4cm.c` (host router, adapters, device router per port, DROM by router operation, the CV/CR handshake, lane bonding, the PCIe tunnel's four path entries, the optional USB3 tunnel), hot-plug events and unplug handling (which `usb4cm` lacks), the pre-OS cleanup, and what the two profiles share
- [ ] U4CM.2 the core lifted into the pure core with host suites, against the `usb4-emu` fabric model as `tools/usb4cm/host` runs it
- [ ] U4CM.3 the driver half and the legs on `usb4-generic`, both host models, `preos-cm=on`, hot-plug, unplug during I/O, and `USB3=1` (a USB device behind the tunnel appearing on the host's own xHCI under `xhci98.sys`)
- [ ] U4CM.4 real USB4 devices, after the maintainer's ORICO capture
