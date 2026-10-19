# Design Record 22 - USB4 Software Connection Manager

Status: draft 2, 2026-10-15, before code, reviewed and revised the same
day. Written for Phase 40 of
`roadmap-usb4.md` ("USB4: the Software Connection Manager", planned as
`U4CM` until the owner numbered it on 2026-10-15), task 40.1. Nothing in
`src\` implements it yet. The owner's decision D7 of 2026-10-15 puts this
work in `3.0.0.0` beside the Thunderbolt 3 ICM client of design record 21,
in the same binary `xhcicm.sys`, with the profile chosen per host interface
(record 21 sections 1.4 and 13). The owner also decided that day that
nothing is cut if time runs short, Thunderbolt 3 routers behind a USB4 host
included, and that an untested id acts by default, with a warning. Section
15 lists the decisions this record needs; each was taken by the
orchestrator of the 2026-10-15 run so that code can proceed, and each is
the owner's to confirm. Section 16 lists what is open and what is a risk;
section 17 lists what this record needs record 21 to say differently.

Revised 2026-10-16 (task 40-A.8's first guest legs): a router's USB4
Version is decided by its major alone (sections 3.5 and 6.4 step 3). The
first reading of a Thunderbolt 3 device router behind a USB4 host, the
CalDigit TS3 Plus model of `usb4-emu`, reported `0x02` and was refused as
reserved under the whole-byte rule this record first gave. With that
fixed it read Upstream Adapter 0 before enumeration, which section 8 item
2 now covers; and the DROM by bit-bang, the task's second pass, is now
read (section 8 item 4), so the router is named.

Revised 2026-10-18 for Phase 42 (task 42-0.5, planned as `U4REC.1`):
section 7.7 gains the recovery of a contained host interface, and
decision U17 is revised by the owner's promotion of that work into the
phase; sections 10 and 10.4 point to record 21 section 7.2's 9x half
(task 42-0.7), which routes the tools' 9x eject through the connection
manager and reaches this profile's per-port teardown on Windows 98 SE
and ME.

The sources, and the tag each fact carries below:

- **[spec]** the USB4 Specification 2.0 ("Spec") and the USB4 Connection
  Manager Guide 2.0 ("Guide"), November 2025 bundle, fetched per machine
  into `docs/references/usb4_202511/` (`docs/references/README.md`), cited
  by section and table.
- **[model]** the `usb4-emu` repository (`D:\OneDrive\git-projects\usb4-emu`,
  branch `main`, commit `a68bc74`; the documents and `tools/usb4cm` read
  here are unchanged since `eba0cf1`, the commit record 21 cites), read and
  never modified: `docs/XHCI98-HANDOFF.md`, `docs/ARCHITECTURE.md` sections
  2 to 7, `docs/WINDOWS-DEBUG.md`, `docs/USB4-HOSTS.md`,
  `docs/PLATFORM-MATRIX.md`, `docs/NVME-DRIVER-EVAL.md`. A [model] fact is
  what the emulator does: a test bed, not hardware.
- **[obs]** a capture or a run that `usb4-emu` records: the Framework Laptop
  13 (12th gen) capture behind the `intel-adl` host model, the Windows 11
  and Linux runs on `preos-cm=on`, the `USB4CM.EXE` runs in Windows 98.
- **[ref]** what the reference connection manager `usb4cm` does
  (`usb4-emu/tools/usb4cm`, MIT at `a68bc74`, GPL-2.0-only from
  `3c74189`): its `README.md` and `core/u4cm.c`,
  `u4hi.c`, `u4regs.h`, `usb4cm.h`, `u4types.h`.
- **[linux]** and **[bsd]** Linux `drivers/thunderbolt` (mirror pinned at
  `c6859eed755df351a3978b33cb92365f9b3e8f06`, with `include/linux/pci_ids.h`)
  and FreeBSD `sys/dev/thunderbolt` (mirror pinned at
  `9d50245a0f1333e10cc1c4dda16878cd79a7c42b`), both under the git-ignored
  `external\`, read under the owner's rule of 2026-10-15 (section 13):
  ids, layouts, constants, sequences and quirks are taken and cited by
  file and symbol, and since `usb4-emu`'s relicensing landed the same day
  their code may be adapted with its notices kept. In
  the Linux snapshot the PCI glue (`nhi_ids[]`, the host quirks, the Intel
  integrated-host power hooks) lives in `pci.c`.

This project holds no capture of a USB4 host of its own. Every statement
below about a real USB4 host is [obs] from the Framework capture as
`usb4-emu` records it, [linux] or [bsd], or untested, and the release notes
say which.

## 1. Purpose, scope and what it is not

### 1.1 What this is for

On a USB4 host - AMD's integrated hosts, ASMedia's discrete ones, Intel's
integrated hosts from Lunar Lake on, its discrete Barlow Ridge, and its
Tiger Lake to Meteor Lake hosts and Maple Ridge when their firmware runs
no connection manager - nothing builds a PCIe tunnel unless the operating
system's connection manager does. It enumerates the routers over ring 0 of
the host interface, configures lanes and paths and enables the tunnel; only
then does the NVMe enclosure or the dock's PCIe tree appear behind the
host's tunnel port. Windows 98 SE and Windows 2000 have no such connection
manager, so a USB4 enclosure plugged into such a machine is dark under them.

This record designs that connection manager: the USB4 profile of
`xhcicm.sys`, the driver record 21 introduces for the Thunderbolt 3 host
interface. It drives the host router and one device router per host USB4
port with control packets, builds the PCIe tunnel (and a USB3 tunnel where
asked), tears it down on unplug, and makes the operating system enumerate
the tunnel ports' buses with the re-enumeration machinery of record 21
section 6. The devices behind the tunnel are driven by the drivers that
exist: `nvme98` for NVMe, `xhci98.sys` for an xHCI controller in a dock or
for the host's own xHCI where a USB3 tunnel lands.

### 1.2 What the owner decided, and how it fits record 21

Record 21 planned this as the release after the Thunderbolt 3 client. The
owner's D7 of 2026-10-15 ships both in `3.0.0.0`. Record 21 section 13
already fixed the frame: one binary, the profile chosen per host interface,
the register contract confirming it after start, and the ring code, the
thread, the re-enumeration, the registry and counter conventions and the
`nvme98` channel shared. This record is the second profile inside that
frame, and it replaces record 21's one-step profile choice with section
3's four tests, because some Intel ids are either kind of host.

### 1.3 Scope

In:

- The USB4 software connection manager on the hosts section 3.4 admits, on
  Windows 98 SE and Windows 2000 SP4 (the primary targets) and the
  virtual-machine tiers under the standing rule for those tiers.
- Polling only: no interrupt is connected, as record 21 section 4.3
  decided for the ICM client (decision D3).
- The Intel integrated hosts' platform preparation through PCI
  configuration space (section 4), shared with record 21's ICM rows.
- The host router, its adapters discovered from their capability chains,
  one device router per host USB4 port (depth 1), its DROM by router
  operation, the Configuration Valid handshake, lane bonding, the PCIe
  tunnel's four path entries, and the optional USB3 tunnel.
- Hot plug and unplug of the device router, which `usb4cm` lacks, designed
  from the Guide and the Spec, with Linux's sequence as the cross-check.
- Tunnels already present at start (a pre-OS connection manager, this
  driver's earlier instance, `USB4CM.EXE`): discovered and preserved,
  never reset automatically (section 9).
- Thunderbolt 3 device routers at depth 1 behind a USB4 host, router by
  router, PCIe tunnel first (section 8).
- The set of tunnel ports per domain on the PCI side (section 7.6).
- `nvme98`'s two obligations, now real work because this driver tears paths
  down itself (section 10).

Out, each with where it goes:

- Enumerating daisy chains (device routers at depth 2 and deeper) and USB4
  hubs' own downstream ports: a plug event from a device router's
  downstream port is acknowledged and the port left locked (section 7.1).
  A later revision. Discovery of tunnels already present reads paths at
  any depth (section 9.2).
- DisplayPort tunnels, DP resources and DP bandwidth allocation: a DP plug
  event is acknowledged and ignored.
- CLx low-power states: never enabled; runtime power management: none.
  The Time Management Unit is configured only as far as a USB3 tunnel
  needs (section 6.6).
- Inter-domain (host-to-host) links and the DMA port: never set up.
- Asymmetric links and USB4 version 2 features: a version 2 router is run
  as a version 1 one (section 6.2).
- Interrupts, MSI and MSI-X: polling only.
- Firmware upgrade (NVM operations), retimers, and the firmware start or
  CIO reset of a stopped Intel firmware: never touched.
- ACPI: no `_OSC`, no WMI force power of an absent discrete controller, no
  ACPI power resources, no retimer `_DSM` ([linux] `acpi.c`; [bsd]
  `nhi_wmi.c`). The controller must be powered and visible on the PCI bus
  as the BIOS leaves it (section 4.3).
- Sleep and wake per the Guide's chapter 8: not in the first cut (section
  11).

### 1.4 How generic this can be

The owner asked for support of as many USB4 hosts as possible. The USB4
connection manager is generic by construction: the Spec defines the host
interface, the router, adapter and path configuration spaces and the
router operations, and a host that conforms needs no code of its own.
What is not generic, and how this record handles it, from the Linux and
FreeBSD readings:

- **Which hosts are USB4 hosts.** The class code `0x0C0340` is the portable
  match ([bsd] `nhi_pci.c` `nhi_pci_probe` matches nothing else; [linux]
  `pci.c` `nhi_ids[]` ends with that class entry, which is how AMD and
  ASMedia hosts bind in Linux). A class match is a candidate, not an
  owner: section 3.
- **Intel's integrated hosts need platform preparation** before anything
  else can be read: force power and LTR through PCI configuration space
  ([linux] `pci.c` `icl_nhi_force_power`, `icl_nhi_set_ltr`,
  `icl_nhi_resume`). Section 4.
- **Some Intel hosts may run a firmware connection manager instead.** The
  software connection manager runs only on positive evidence that none
  runs; anything ambiguous is an inert start (section 3.5), never a
  takeover.
- **Per-host and per-device quirks** Linux applies: section 6.10.
- **Tunnel ports** are platform root ports or a discrete controller's
  downstream ports, and their association with a host interface is a
  platform fact: section 7.6.

What stays honest: only the emulator's two host models have been run
against any of this, and only `usb4cm` (the DOS reference) has brought a
tunnel up there from Windows 98. Every real host is untested; by the
owner's decision it acts by default anyway, and the README and release
notes warn so in those words.

## 2. The contract, and what the reference connection manager already does

### 2.1 The test bed

`usb4-emu`'s `usb4-generic` profile is the software connection manager's
target [model] (`ARCHITECTURE` section 2): PCI identity `1b36:00f4`, class
`0x0C0340`, so a guest takes the software path; router dialect `usb4`;
paths programmed by the guest through configuration writes. Two host
models, chosen by `host-model=` [model] (`USB4-HOSTS` section 1):

| Host model | Host router | Adapters |
|---|---|---|
| `minimal` (default) | spec-derived, vendor `46f4` | lanes 1-2 and 3-4 (two USB4 ports), a PCIe Down and a USB3 Down per port, the host interface |
| `intel-adl` | the Framework capture's Alder Lake-P host router, vendor `8087`, device `463e` | lanes 1-2 (port 1) and 3-4 (port 2), DP IN 5-6, host interface 7, PCIe Down 8-9, adapters 10-11 not present (`ERR_ADDR`), USB3 Down 12-13; Max Adapter 13; Buffer Allocation Request, DROM Read and the DP resource operations supported, every other operation Not Supported; a plug event for each DP IN adapter every time the router is enumerated |

`preos-cm=on` adds an emulated platform firmware that runs a connection
manager before the operating system and hands over without cleaning up
(section 9) [model]. On i440FX the tunnel's secondary bus sits behind
`usb4-tunnel-bridge`, a QEMU `pci-bridge` subclass (`PCI\VEN_1B36&DEV_0001`)
with SHPC and `shpc-autoenable=on`; on q35 behind `usb4-tunnel-port`, a
`pcie-root-port` subclass (`1B36:000C`) [model] (`ARCHITECTURE` section
4.4, `WIN98-TEST`). The four-stage teardown of `ARCHITECTURE` section 4.4
(gate, quiesce, notify, remove) is the oracle for section 10. The emulator
answers the Intel firmware-status register only on its `tb3-alpine-ridge`
profile; on `usb4-generic` there is no Intel id, so section 3's Intel tests
never run there.

More host models are coming (its maintainer's note of 2026-10-15: an AMD
USB4 host, in `docs/CONTROLLER-CATALOG.md`, in progress). Since its `main`
at `a50c19e` a third host model is there, `host-model=asm4242` (ASMedia's
ASM4242 of AM5 X870 boards, from public sources only), a host with no
firmware connection manager: under a CSM, this record's connection
manager is the only one it can have. The legacy operating systems need a
CSM, so the realistic USB4 targets are such AM5 X870 boards; the
Framework laptop behind `intel-adl` is UEFI-only, a reference capture and
not a run target. The connection
manager is therefore developed against `minimal` and `intel-adl` and
branches only on what it discovers - adapter types, capability chains,
router operation answers, the router's vendor and device for the quirk
table - never on Alder Lake's adapter numbers or any other host's layout.

What the Windows 11 connection manager required of the model [obs]
(`WINDOWS-DEBUG`, "What the Windows CM required of the model"): one USB3
Down adapter per USB4 port on the host router; a tunnel port that repeats a
presence request Windows' PCIe hot-plug driver ignored during boot; and an
admin-down that defaults to surprise removal because Windows never completes
an eject for a mounted volume once the gate has closed. None binds this
driver, which needs no USB3 Down adapter and drives no PCIe hot-plug, but
the third explains why a teardown this driver starts reads as a surprise
removal on the emulator (section 10.2).

### 2.2 What `usb4cm` does, and where this record departs from it

`usb4cm` (C89, polling, 32-bit DMA, about 1.3 thousand lines) brought a
tunnel up from the Windows 98 DOS prompt on both host models [obs]
(`tools/usb4cm/README.md`, "Validation"; `NVME-DRIVER-EVAL`, "Host types":
`intel-adl` paired PCIe Down 8 with port 1, 306 control packets, 0
retries). Its sequence is the starting point, and its code may be adapted
under section 13's rule, changed as the table below says. Its steps: host interface reset, ring 0 raw mode, control packets
with sequence numbers and CRC-32C, hot plug acknowledgement and queue, host
router enumeration and adapter discovery by capability walk, a device router
per port found by lane 0 in CL0, Lock cleared, enumeration at depth 1, the
DROM by the DROM Read operation, lane bonding, the Configuration Valid
handshake with PCIe Tunnelling On, four path entries, Path Enable device
side first, an LTSSM L0 wait, and the optional USB3 tunnel. Its README lists
what it does not do: TMU, daisy chains, DisplayPort, unplug handling after
the run, credit sizing (fixed 32), the Thunderbolt 3 dialect, USB4 version
2.

Read against the Guide, the Spec and Linux, these steps change in this driver:

| `usb4cm` | This record | Why |
|---|---|---|
| Host Interface Reset at every start, then ring 0 programmed | no Host Interface Reset; ring 0 disabled as Guide 5.7.3 says and then programmed (section 4.4) | a ring left enabled by whoever ran before may still DMA; Spec Table 12-13 (a version 2 connection manager should not reset a version 2 host interface); [linux] `nhi.c` never issues it and stops a ring with `tb_ring_stop` |
| Router Ready waited 100 ms | 500 ms | Guide 3.2.1 note; [linux] `usb4.c` `usb4_switch_setup` |
| Notification Timeout left at its default | ROUTER_CS_4 bits 7:0 = 255 at enumeration | Guide 3.2.1 (optional, above 0); [linux] `switch.c` `tb_switch_configure` (`plug_events_delay = 0xff`) |
| Hot plug events left at the router default | ADP_CS_5 Disable Hot Plug Events (bit 31) cleared on every USB4 lane 0 adapter before Lock is cleared | Guide 3.1.1 (set before Lock); [linux] `usb4.c` `usb4_port_hotplug_enable` |
| UTO set after the paths, PTO before CV | PTO and UTO in one write before CV; CNS (bit 23) left 0 | Guide 3.2.3 (tunnelling enabled, then CV); [linux] `usb4.c` `usb4_switch_setup` clears CNS; section 8 makes this connection manager Thunderbolt 3 compatible at depth 1 |
| No LTSSM check before the PCIe paths | both PCIe adapters polled to Detect (0) before any path entry, 500 ms | Guide 5.3.1 step 1; [linux] `tunnel.c` `tb_pci_pre_activate`; section 10.1 makes this the gate for `nvme98`'s obligation 1 |
| Protocol-adapter entry: credits, IFC and ISE cleared, EFC and ESE set | credits, IFC and ISE preserved as read; EFC 1, ESE 0 | Guide Table 5-2 and its note 1; [linux] `path.c` `tb_path_activate` (ingress fields vendor-defined on USB4 protocol adapters), `tunnel.c` `tb_pci_init_path` |
| Lane-ingress credits fixed at 32 | per-lane credit accounting (section 6.8): the lesser of the router's preference (baMaxPCIe, else 32 bonded and 16 single) and what the lane has left; no tunnel below 6 | Guide 5.1.4; [linux] `usb4.c` `usb4_switch_credits_init`, `tunnel.c` `tb_pci_init_credits`, `TB_MIN_PCIE_CREDITS` |
| USB3 path weight 1 | weight 2 | [linux] `tunnel.c` `TB_USB3_WEIGHT` (PCIe stays 1, `TB_PCI_WEIGHT`); Guide Table 6-1 priority 3 for both |
| USB3 tunnel with no bandwidth negotiation | the Host Controller Ack handshake on the host USB3 Down adapter before Path Enable (section 6.9) | Guide 6.2.2; [linux] `usb4.c` `usb4_usb3_port_allocate_bandwidth` |
| Lanes left single and paths built when bonding fails | lane 1 disabled first, then paths over lane 0 | Guide 3.2.2 and 5 ("does not enable any Path in a USB4 Port with two Single-Lane Links that have not yet been bonded"), 7.1, 7.5.1; Linux keeps paths over lane 0 without disabling lane 1 (`switch.c` `tb_switch_lane_bonding_enable`), which this record notes and does not follow |
| USB4 Port is Configured never set | PORT_CS_19 PC (bit 3) set on the host's port after bonding, cleared at unplug | Guide 3.4.1 (restored at removal); [linux] `usb4.c` `usb4_port_configure`, `switch.c` `tb_switch_configure_link`, `tb_switch_unconfigure_link` |
| An error notification of any code below 32 fails the outstanding request | only the request-error codes do; ERR_LINK, ERR_HEC, ERR_FC and ERR_PLUG are events, acknowledged, counted, and never fail a request | Spec Table 6-12 and 6.4.3; Guide 7.7.2; [linux] `ctl.c` `tb_async_error` |
| Request timeout 50 ms, 3 retries | 100 ms, 4 attempts | Guide 4.2 (at least 10 ms); [linux] `tb.c` `TB_TIMEOUT`, `ctl.c` `TB_CTL_RETRIES` |
| Timeouts counted by summing requested delays | measured against a clock callback | section 5.3: a coarse sleep overshoots on Windows 98 |
| Lane 1 assumed to be lane 0 + 1 | the same, now cited | Spec 5.2.1 (a port's two lane adapters are consecutive, lane 0 lower) |
| The Nth PCIe Down adapter taken for port N as a platform property | the same, now cited as a rule, cross-checked by the mapping operation where a host answers it | Spec 5.2.5 (USB4 ports and PCIe and USB3 Gen X adapters paired in increasing adapter number); Guide 3.2.2; [linux] `usb4.c` `usb4_port_index`, `usb4_switch_map_pcie_down` |

## 3. Which connection manager, if any: four tests

Record 21 section 1.4.2 defines the selection both profiles share: four
separate tests, each with its own evidence, and the inert start; its
section 1.4.3 the hardware profiles; and it leaves the software
connection manager's positive admission to this record. This section
restates the four tests as they apply to the USB4 profile and defines that
admission (sections 3.3 to 3.6). Some Intel ids are either kind of host,
and a firmware connection manager and a software one on the same domain
is the outcome no policy may allow, so failing any test is never a reason
to run the other profile: it is the inert start (section 3.5).

### 3.1 Test (a): the hardware candidate, at `AddDevice`

`AddDevice` reads the PDO's hardware and compatible ids
(`IoGetDeviceProperty`, as record 21 section 3.2 already does) and looks
them up in two tables kept apart: a **hardware-profile table** (what the
host interface needs before it can be used: section 4) and a **CM table**
(which connection manager may own it, and how that is proven). Nothing is
read from the hardware before start.

| Matched | Hardware profile | CM candidates | Source |
|---|---|---|---|
| Falcon Ridge `156A` `156C`; Alpine Ridge `1575` `1577` `15BF` `15D2` `15D9`; Titan Ridge `15E8` `15EB` | discrete Thunderbolt | ICM only (record 21's rows) | [linux] `pci.c` `nhi_ids[]`, `icm.c` `icm_probe` |
| Ice Lake `8A17` `8A0D` | Intel integrated | ICM only | [linux] `icm.c` `icm_probe` (`icm_fr_is_supported`) |
| Tiger Lake `9A1B` `9A1D` `9A1F` `9A21`; Alder Lake `463E` `466D`; Raptor Lake `A73E` `A76D`; Meteor Lake `7EB2` `7EC2` `7EC3` | Intel integrated | ICM or USB4 | [linux] `icm.c` `icm_probe` (`icm_tgl_is_supported`) |
| Maple Ridge `1134` `1137` | discrete USB4 | ICM (Titan Ridge messages) or USB4 | the same |
| Lunar Lake `A833` `A834`; Panther Lake `E333` `E334` `E433` `E434`; Wildcat Lake `4D33` | Intel integrated | USB4 only | [linux] `pci.c` `nhi_ids[]` (`icl_nhi_ops`), no `icm_probe` row |
| Barlow Ridge host `5781` `5784` | discrete USB4 | USB4 only | the same, default ops |
| AMD Pink Sardine `1022:1668` `1022:1669`, named; any other host of class `0x0C0340` (ASMedia, other AMD, the emulator's `1B36:00F4`) | generic | USB4 only | [bsd] `nhi_reg.h` `DEVICE_PINK_SARDINE_*`; [linux] `pci.c` class entry |
| Alpine Ridge USB-only `15DC` `15DD` `15DE` | - | none: NHIs with no ICM row, excluded from this release | [linux] `nhi.h` |
| Light Ridge `1513`, Cactus Ridge `1547`, Win Ridge `157D`, Redwood Ridge | - | not claimed | [linux] `pci_ids.h`, `nhi.h` |

The Intel rows are the orchestrator's coverage plan of 2026-10-15 from the
Linux survey, owner to confirm; only `15BF` is captured (record 21). An
Intel id of class `0x0C0340` that is in neither Intel row is taken by the
class match as a generic host, as Linux takes it. Router-side ids
(Titan Ridge DD `15EF`, Barlow Ridge hub bridges `5786` `57A4`) belong to
the quirk table of section 6.10, never to the NHI INF. **Apple** machines
are excluded by platform, not by id ([linux] `icm.c` `icm_fr_is_supported`
uses `x86_apple_machine`): at start, the PCI subsystem vendor `106B` of the
NHI makes the start inert with a log line, since none of this project's
targets runs on a Mac and the firmware there differs.

### 3.2 Test (b): platform readiness, after `IRP_MN_START_DEVICE`

The hardware profile's preparation (section 4) has completed: Bus Master
set, the Intel integrated host's force power and firmware-ready poll
passed, BAR0 mapped and decoding (the host interface capabilities register
at `0x39640` not all-ones, Total Paths 1 to 21, Spec Table 12-11). Any
failure: inert.

### 3.3 Test (c): who owns the domain

Per the CM table's row, never across rows:

- **Discrete Thunderbolt and Ice Lake rows (ICM only)**: record 21's
  admission (the row's predicate, firmware status `ICM_EN` set, and, where
  the row has one, the mode query reporting connection-manager mode). Not
  admitted: inert. These rows never reach the software connection
  manager, `XhciCmMode` 3 included (section 3.6).
- **Tiger Lake to Meteor Lake, Maple Ridge (ICM or USB4)**: after section
  4's readiness contract has completed, read the firmware status register
  (`0x39944`, [linux] `nhi_regs.h` `REG_FW_STS`). A **valid** read is one
  taken after readiness succeeded that does not return all-ones; bit 0 is
  `ICM_EN`, bit 31 `NVM_AUTH_DONE`:
  - `ICM_EN` set: the firmware connection manager owns the domain; record
    21's ICM admission continues, with `NVM_AUTH_DONE` polled as that
    row's candidate predicate ([linux] `icm.c` `icm_tgl_is_supported`, 10
    ms) and Ice Lake messages for the integrated hosts, Titan Ridge
    messages with the Alpine Ridge mode handling for Maple Ridge. Its
    failure is inert, never the software connection manager.
  - `ICM_EN` clear on a valid read: the software connection manager is
    admitted (section 3.4, case 2), as a stated best-effort policy, not
    gated on `NVM_AUTH_DONE`, which is only the ICM candidate predicate.
    `NVM_AUTH_DONE` set with `ICM_EN` clear is recorded in the log and the
    counters as an experimental case, not as Linux's normal rule (Linux
    reaches its software connection manager on these ids through `_OSC`
    or a timeout, `nhi.c` `nhi_select_cm`).
  - No valid read (readiness failed, the register all-ones): inert, which
    is a different outcome from a valid, disabled ICM.
- **USB4-only rows**: no firmware connection manager protocol is
  identified for this hardware ([linux] no `icm_probe` row): the software
  connection manager is admitted once the hardware profile's readiness and
  test (d) pass (section 3.4, case 1). The Intel firmware-status register
  is never read on a non-Intel host, where that offset means nothing, and
  its absence is not an error. Firmware-state evidence that is unresolved
  or conflicting (a vendor-specific readiness bit that never settles, an
  Intel integrated host's FW_READY never set) makes the start inert, and
  that takes precedence over admission.

### 3.4 The software connection manager's admission rule

There is no `_OSC` on these operating systems, so the software connection
manager runs on one of three grounds only (owner's coverage plan of
2026-10-15, reviewed and revised; orchestrator decision, owner to
confirm).
The first two are automatic, the third the only forced route:

1. **Software-only candidates**, under the Linux and FreeBSD matching
   policy, with no supported ICM protocol identified for the hardware: any
   host of class `0x0C0340` not of an Intel ICM-capable generation (AMD,
   `1022:1668` and `1022:1669` among them; ASMedia; others), and Intel
   Lunar Lake, Panther Lake, Wildcat Lake and Barlow Ridge (`5781`,
   `5784`). Admitted automatically once the hardware profile's own
   readiness contract (for the Intel integrated hosts, section 4.2's
   FW_READY), test (d), BAR0 decoding, the below-4-GB common buffer and
   exclusive use of ring 0 (no other driver bound to the function) pass.
2. **Intel Tiger Lake to Meteor Lake and Maple Ridge**: a valid read of
   `ICM_EN` = 0 after the readiness contract (section 3.3), as a stated
   best-effort policy.
3. **`XhciCmMode` 3**, the experimental user override of section 3.6.

In every case tunnels found at start are discovered, reserved and
preserved, never reset automatically (section 9); pre-existing tunnels
prove an earlier configuration, not a running competing connection
manager. Unresolved or conflicting firmware state, an `ICM_EN` set whose
ICM handshake then failed, an unknown vendor's firmware state: inert. The
software connection manager is never a fallback from a failed ICM client.

### 3.5 Test (d), and the inert start

**Test (d), supported protocol**, at the first moment the profile touches
its own registers:

- The Host Interface Version (capabilities bits 23:16) reads `0x00` (USB4
  1.0) or `0x40` (USB4 2.0) (Spec Table 12-11); anything else is inert.
  These are the two versions this profile supports.
- The host router answers the read of ROUTER_CS_0 to 4 at route 0 within
  the request bounds (section 5.2), and ROUTER_CS_4's USB4 Version (bits
  31:24; major version in 31:29, minor in 28:24, Spec 8.2.1.1) has major
  1 (USB4 1.x, `0x20` to `0x3F`) or major 2 (USB4 2.x, `0x40` to `0x5F`).
  The major alone decides, whatever the minor, here and for a device
  router (section 6.4), as [linux] does (`tb_regs.h`
  `USB4_VERSION_MAJOR_MASK`, `tb.h` `usb4_switch_version` and
  `tb_switch_is_usb4`). Major 0 (`0x00` to `0x1F`) is a Thunderbolt 3
  router, which this profile does not drive as a host; majors 3 to 7 are
  reserved and refused. A Thunderbolt 3 router may report a non-zero
  minor: the CalDigit TS3 Plus reads `0x0200000A` in ROUTER_CS_4 in
  `usb4-emu`'s Framework 13 capture [obs], USB4 Version `0x02`.

**The inert start** is record 21 section 1.4.2's, shared by both profiles
(owner's coverage plan; orchestrator decision, owner to confirm), with a
Code 10 refusal kept only for BAR0 not delivered or not mappable. Its two
forms, kept apart by how far the start got:

- **Before activation** (tests (a) to (c) or test (d)'s interface version
  failed, before the rings were built): the devnode starts with Code 0 and a log line naming the
  test and the reading; no ring is built, no control packet or ICM frame is
  sent, no mailbox command is written, no reset of any kind is issued, no
  device is approved, nothing is torn down, and no unload mailbox is sent
  at stop. A platform power change section 4 made is undone only where it
  is this instance's own and safe to undo (section 4.2).
- **After activation** (a failure once this profile's rings run: test
  (d)'s host-router read unanswered or its USB4 Version refused, which
  needs the ring, or a protocol fault later in the start): the rings
  are stopped and every queued work item drained before the common buffer
  is freed; tunnels are preserved; nothing is cleaned up beyond what the
  recorded start stage itself built; the devnode stays started and inert.

An inert host interface is visible in the counters and the log, and the
devices behind any tunnel already up keep working.

### 3.6 `XhciCmMode` 3

`XhciCmMode` 3 (record 21 section 8 reserved it for this profile) is the
**experimental user override** that forces the USB4 software connection
manager past test (c)'s row, at the user's own risk, in the
`XhciIntelPortSwitch` 2 wording the README already uses. It applies only
to hosts the software connection manager supports, and it cannot bypass:

- a failed platform preparation or an unreadable required register (test
  (b));
- a firmware connection manager known to be running: `ICM_EN` read set
  vetoes it, on the rows where that register's contract is established
  (the Intel rows); elsewhere there is no such veto to apply;
- an unsupported host interface or router protocol (test (d));
- the preservation of existing tunnels (section 9) and the DMA cleanup
  rules of section 3.5.

The discrete Thunderbolt NHIs (Falcon, Alpine and Titan Ridge) are not
USB4 hosts: forcing the software connection manager on them is refused,
since their host routers are Thunderbolt routers that only section 8's
legacy-router work could drive, and that work covers device routers, not
host routers, in this phase. Ice Lake's host router is of the same
generation ([linux] `switch.c` `tb_switch_get_generation` lists `8A17`
and `8A0D` as generation 3), so test (d) refuses it there too. On a
software-only candidate the value changes nothing. Decision U1.

### 3.7 The INF lines

`xhcicm.inf` and `xhcicm-amd64.inf` gain, on every path (the undecorated
Windows 98 section, `.NTx86`, `.NTx86.6.0`, the amd64 pair) and with the
same install sections and service: `%U4Desc%=Cm.Dev,PCI\CC_0C0340`; one
line per Intel id of section 3.1's "USB4 only" rows
(`PCI\VEN_8086&DEV_xxxx`), the "ICM or USB4" ids being in record 21
section 3.3's list already; and `PCI\VEN_1022&DEV_1668` and `DEV_1669`,
named beside the class line so the INF says which AMD hosts are meant
(decision U12: `U4Desc` = `xHCI98 USB4 Host Router`). The description
names the hardware's generation, not the connection manager running it,
so the "ICM or USB4" ids (Maple Ridge, Tiger Lake to Meteor Lake) carry
`U4Desc` too and a controller keeps its name whichever manager takes it;
the ports the filter attaches to on these hosts read `xHCI98 USB4 PCI
Bridge` (record 21 section 3.3, "Class and names"). The id lines rank
above the class line, which is what makes section 3's tables apply to
them. Windows 98's PCI enumerator offers the class compatible id, as
`xhci98.inf`'s `PCI\CC_0C0330` line already shows. No device-router id
(section 3.1's last paragraph) and no USB-only NHI id appears.

The NT-only filter INFs (`xhcicmf.inf`, `xhcicmf-amd64.inf`, record 21
section 3.3) already carry, by record 21 section 3.3's id list (`pci.ids`
version `2026.10.14` and the Linux mirror, decision D9), the tunnel ports
of every Intel host this profile shares with the ICM rows: the Maple Ridge
bridges `1133` and `1136`, and the Thunderbolt root ports of Ice Lake,
Tiger Lake-LP and -H, Alder Lake-P and Raptor Lake-P; record 21 notes that
the listing names no Meteor Lake tunnel root port and only three of Raptor
Lake's. On a USB4 host a tunnel port is the platform's PCIe root port
(Intel integrated hosts, AMD) or a discrete controller's PCIe downstream
port (Maple Ridge, Barlow Ridge, ASMedia). This profile adds (decision
U8):

1. **The USB4-only hosts' ports**, read by task 40-0.2 from the same
   listing and recorded the same way (the listing's name and version in
   `legal-provenance.md` section 4): the Thunderbolt or USB4 root and
   downstream port ids of Lunar Lake, Panther Lake, Wildcat Lake, the
   Barlow Ridge host, AMD's USB4 hosts and ASMedia's ASM4242, only where
   the listing labels a port as such. Neither mirror names any of them:
   Linux finds integrated tunnel ports through ACPI ([linux] `acpi.c`
   `tb_acpi_add_links`), and FreeBSD's identifier table is empty at its pin
   ([bsd] `tb_pcib.c`). Every such id is untested; a host whose ports the
   listing does not name gets its set from `XhciCmTunnelBridge`.
2. **The emulator's ports**: `PCI\VEN_1B36&DEV_0001&CC_0604` (the i440FX
   `usb4-tunnel-bridge`) and `PCI\VEN_1B36&DEV_000C&CC_0604` (the q35
   `usb4-tunnel-port`), in every flavour, since the release flavour's VM
   legs on Windows 2000 and later need them (record 21's own emulator legs
   need the first too).

A filter instance on a matched port that record 21 section 6.3 never makes
an active member of a domain's set does nothing: it is a registration
entry no NHI marks (record 21 section 3.2), and it never reports the port
removable. A user whose host's tunnel port is not listed adds a line to a
copy of `xhcicmf.inf`, the procedure the README spells out beside
`XhciCmMode` 2's; without a filter, record 21 section 6.4's fallback
applies. None of this exists on Windows 98 and ME, where the NHI
re-enumerates through CONFIGMG and no driver ever sits on a bridge (record
21 section 6.1).

## 4. Platform preparation: the hardware-profile table

### 4.1 Discrete hosts and generic hosts

Bus Master Enable through `IRP_MN_WRITE_CONFIG` to the PDO (record 21
section 5.1 step 1), and nothing else.

### 4.2 Intel integrated hosts: force power and LTR

Ice Lake through Wildcat Lake (the `icl_nhi_ops` ids, [linux] `pci.c`
`nhi_ids[]`; ICM and USB4 rows alike) keep the host interface's firmware
unpowered until the driver asks, every time it is used ([linux] `pci.c`
comment at `icl_nhi_resume`). The sequence, all through the NHI's PCI
configuration space with `IRP_MN_READ_CONFIG` and `IRP_MN_WRITE_CONFIG` to
the PDO, read-modify-write, at the first start and at every resume to D0
([linux] `icl_nhi_ops` `.init` and `.resume` are both `icl_nhi_resume`),
before test (c):

1. VS_CAP_22 (offset `0xFC`): DMA_DELAY (bits 31:24) = `0x22` and
   FORCE_POWER (bit 1) = 1 ([linux] `pci.c` `icl_nhi_force_power`). The
   value read before the write is recorded, with whether FORCE_POWER was
   already set.
2. Poll VS_CAP_9 (offset `0xC8`) bit 31 FW_READY, up to 350 reads about 3
   ms apart (about 1.05 s). Never set: inert.
3. Copy VS_CAP_16 (`0xE4`) bits 15:0, the maximum LTR, into both halves of
   VS_CAP_15 (`0xE0`) ([linux] `pci.c` `icl_nhi_set_ltr`).

**The power-change record** is record 21 section 1.4.2's, shared: the
bit is read before it is written, the driver records whether it found it
clear and set it, and a bit the BIOS had already set is never cleared.
One correction and one refinement for this profile. The correction: an
inert instance never clears a bit it set, even where its own FW_READY poll
failed - a readiness timeout is not evidence that nothing on the domain
depends on the controller, and Linux describes firmware readiness apart
from the platform's power management ([linux] `pci.c`
`icl_nhi_force_power`) - so it leaves the bit set and logs it (record 21
section 1.4.2 now says the same, for every profile). The refinement, where the driver's own stop leaves tunnels up (section 11): an
active instance clears a bit it set, at stop or shutdown after its rings
are stopped, only when no device router is connected on any of its ports
at that moment - the condition under which Linux itself powers the host
interface down at runtime suspend ([linux] `pci.c` `icl_nhi_suspend`
returns early while a router is connected) - and otherwise leaves it set
and logs it; record 21's ICM rows clear it after the unbind as Linux's
`icl_nhi_shutdown` does. The link-controller mailbox Linux uses at suspend
under an ICM ([linux] `pci.c` `icl_nhi_lc_mailbox_cmd`) is record 21's,
never this profile's.

### 4.3 What cannot be reached

Linux's ACPI `IMR_VALID` property ([linux] `pci.c` `nhi_pci_imr_valid`),
`_OSC` ([linux] `acpi.c` `tb_acpi_is_native`), the WMI power-on of a
discrete controller left unpowered ([bsd] `nhi_wmi.c`), ACPI power
resources and the retimer `_DSM` ([linux] `acpi.c`
`tb_acpi_retimer_set_power`) all need an ACPI implementation these targets
lack. The rule: the controller must be powered and visible on the PCI bus
as the BIOS leaves it; this driver never requests runtime D3; a controller
the BIOS left off is invisible and is a known limitation, as in record 21
section 1.3.

### 4.4 The host interface, once admitted

On the thread, in order:

1. Mask every ring interrupt (Interrupt Mask Clear, `0x38208` and
   `0x3820C`, all ones), leaving throttling and vector registers untouched
   [ref] `u4hi.c` `u4cm_init`; nothing is connected, and the interrupt
   status register is never read, so the per-host read-to-clear rules
   ([linux] `pci.c` `nhi_pci_check_quirks` `QUIRK_AUTO_CLEAR_INT` for
   Intel; [bsd] `nhi.c` `nhi_intr`'s note for AMD) never touch it.
2. **Ring 0 taken over, never reset.** No Host Interface Reset is issued
   (Linux never issues one, [linux] `nhi.c`; the Spec recommends against it
   on a version 2 host interface, Table 12-13), and the Host Router Reset
   (`0x39898`) is issued only by the explicit cleaning of section 9.3
   [spec] Guide 3.5. A ring 0 left enabled by whoever ran before (a pre-OS
   connection manager, `USB4CM.EXE`, this driver's earlier instance) may
   still DMA into memory that is no longer its owner's, so before any
   descriptor address is written, each of TX ring 0 and RX ring 0 is
   disabled as Guide 5.7.3 says: its Ring Control register (`0x19800`,
   `0x29800`) written to its default (Valid and Raw clear, every field 0),
   then its Base Address Low and High and Ring Size written 0 ([linux]
   `nhi.c` `tb_ring_stop` also clears the producer and consumer index
   register, and so does this driver), then at least 1 ms before the new
   ring is programmed (Guide 3.5 note 1's settle after disabling transmit
   rings). Whether ring 0 read enabled is logged. No other ring is touched:
   rings 1 and up belong to nobody here, and one found enabled is logged
   and left alone. Admission (section 3.4) already decided that this
   instance owns the domain; the takeover only makes ring 0 safe to
   replace.
3. Ring 0 TX and RX, raw mode, 16 descriptors each, 256-byte buffers, RX
   PDF masks all ones [ref] `u4hi.c`; [bsd] `nhi.c` `nhi_alloc_ring0` (16
   each way; Linux uses 10, `ctl.c` `TB_CTL_RX_PKG_COUNT`). One common
   buffer below 4 GB from `cm_pool.c`, about 9 KiB. Below-4-GB DMA is
   qualified per host by the legs and the release notes, not assumed:
   Linux sets a 64-bit mask ([linux] `nhi.c` `nhi_probe`), which shows
   only that Linux does not need the limit.
4. Test (d) (section 3.5).
5. The host router: enumerate or verify (section 6.1), discover adapters,
   map ports (section 6.3).
6. Discover what is already there (section 9), then serve hot plug.

Neither mirror has ever run a host interface with no interrupt at all:
Linux fails its probe without MSI or MSI-X ([linux] `pci.c`
`nhi_pci_init_msi`), FreeBSD uses MSI-X only ([bsd] `nhi_pci.c`
`nhi_pci_allocate_interrupts`). `usb4cm` has, on the emulator only [obs].
Completion by the descriptor's done bit is what all three rely on ([linux]
`nhi.c` `RING_DESC_COMPLETED`; [bsd] `nhi.c` `nhi_intr`), so polling needs
no interrupt; that it works on each real host is a qualification, not a
claim.

## 5. The transport

### 5.1 Control packets

`usb4cm`'s `u4hi.c` is the reference transport: one request outstanding
(Guide 4.2), a 2-bit sequence number, a response matched on route, address,
size, configuration space and sequence number but not on the adapter field
(Spec 6.4.2.4), CRC-32C appended and checked (Spec 6.4.1, Appendix A.2)
[ref]. Changes in this driver:

- **Timeout and retries**: 100 ms per attempt, 4 attempts, a new sequence
  number each ([linux] `tb.c` `TB_TIMEOUT`, `ctl.c` `TB_CTL_RETRIES`;
  Guide 4.2 recommends at least 10 ms). A request that never answers fails
  its step, never the driver.
- **Notifications classified** (Spec Table 6-12, 6.4.3.2, 6.4.3.3; Guide
  3.2 notes, 7.7.2):

  | Code | Meaning to this driver |
  |---|---|
  | ERR_CONN 0 | fails the outstanding request; the adapter on the route is disconnected: a port check is queued (section 7.1) |
  | ERR_ADDR 2, ERR_ADP 4, ERR_LEN 11 | fails the outstanding request; ERR_ADDR during adapter discovery marks the adapter unused (Guide 3.2.2) |
  | ERR_ENUM 8, ERR_NUA 9 | fails the request; at enumeration, the router is someone else's or uninitialised behind its own downstream port (Guide 3.2.1 step 1); the port is left alone and logged |
  | ERR_LOCK 15 | fails the request; the adapter was reconnected since it was unlocked: a port check is queued |
  | ERR_LINK 1, ERR_HEC 12, ERR_FC 13, ERR_PLUG 14 | an event: counted, logged, acknowledged with a Notification Acknowledgement (Spec 6.4.2.8), or the Enhanced one (6.4.2.9) where the router has the sequence bit enabled (section 6.4), never fails a request |
  | DP_BW 32, PCIE_WAKE 35, DP_CON_CHANGE 36, DPTX_DISCOVERY 37 | acknowledged and ignored |
  | ROP_CMPLT 33, POP_CMPLT 34, LINK_RECOVERY 38, ASYM_LINK 39 | consumed silently: the Guide forbids an acknowledgement (7.7.2) |

  Linux acknowledges only PCIE_WAKE, DP_CON_CHANGE, DPTX_DISCOVERY and
  DP_BW and ignores the link and flow-control errors ([linux] `tb.c`
  `tb_handle_notification`); this record follows the Guide's list, a
  superset, since a router waiting for an acknowledgement it never gets is
  the worse failure.
- **Hot plug events** (PDF 5, Spec 6.4.2.10: adapter in bits 5:0 of the
  third dword, UPG in bit 31) are acknowledged at once with HP_ACK and PG 2
  or 3, in the order received (Guide 4.3; [linux] `ctl.c` `tb_cfg_ack_plug`
  acknowledges before processing), and queued. A retransmitted copy of a
  queued event is dropped [ref]. The queued event is a hint, not the truth:
  section 7.1 re-reads the port.

### 5.2 Every completion polled with a timeout

| Completion | Bound | Source |
|---|---|---|
| a TX descriptor consumed | 10 ms | [ref] `u4cm_send` |
| a request's response | 100 ms, 4 attempts | section 5.1 |
| ring 0 disabled before reprogramming | 1 ms settle | Guide 5.7.3, 3.5 note 1 |
| Host Router Reset (explicit cleaning only) | rings disabled at least 1 ms before; HRR untouched for 50 ms, then 0 within 500 ms | Guide 3.5; [linux] `nhi.c` `nhi_reset` |
| force power FW_READY | 1.05 s | section 4.2 |
| TMU time posting (Post Time back to 0) | 100 ms, a project bound (Linux polls 100 times 5 to 10 us apart) | section 6.6 |
| TMU convergence before a USB3 path | 50 ms | Guide 7.4.3.1 step 4 |
| a PCIe adapter's LTSSM in a transitional state | 2 s before it counts as a loss | section 7.1 item 4 |
| `NVM_AUTH_DONE` (Tiger Lake to Meteor Lake, Maple Ridge) | 10 ms | section 3.3 |
| Router Ready after enumeration | 500 ms | Guide 3.2.1 note; [linux] `usb4_switch_setup` |
| a router operation's OV clear | 500 ms | [linux] `usb4.c` `usb4_native_switch_op` |
| Configuration Ready after CV | 500 ms | Guide 3.2.3 note; [linux] `usb4_switch_configuration_valid` |
| lane 1 in CL0 before bonding | 1 s | [linux] `switch.c` `tb_wait_for_port` (10 times 100 ms) |
| Negotiated Link Width x2 after Lane Bonding | 500 ms | Guide 7.1 (tBonding); Linux waits 100 ms (`tb_port_wait_for_link_width`); the larger bound is kept for the emulator under TCG |
| both PCIe adapters in Detect before the paths | 500 ms | Guide 5.3.1 step 1; [linux] `tb_pci_pre_activate` |
| LTSSM L0 on the host PCIe Down adapter after Path Enable | 1 s | [ref] `u4cm_pcie_tunnel` |
| Pending Packets 0 after a path entry's Valid is cleared | 500 ms, then at least 2 us before reuse | Guide 5.2.2; Spec Table 5-16 (tTeardown); [linux] `path.c` `__tb_path_deactivate_hop` |
| USB3 Host Controller Ack | 1.5 s | [linux] `usb4.c` `usb4_usb3_port_cm_request` |
| Downstream Port Reset held | 10 ms, then lane 0 in CL0 within 2 s | Spec 6.9 (tClearDPR); Guide 7.3; [linux] `usb4.c` `usb4_port_reset` |
| a device router's lane 0 in CL0 after a plug event | 2 s | [ref] `connect_wait_us` |
| the PCI-side reading of a tunnel port | 2 s | record 21 section 6.5 |

### 5.3 The clock and the waits

`usb4cm`'s core counts each timeout by summing the delays it requested
[ref] `usb4cm.h` ("Every timeout is counted through it"). In this driver a
wait of a millisecond or more is the thread's own timed wait on its queue
event (the wait the idle poll already uses, so no new import), and on
Windows 98 that sleeps to the next timer tick, overshooting a 1 ms request
by up to the tick. Summed requests would stretch every bound by up to the
tick ratio. The core therefore gets one more callback, `clock_us`, a
monotonic 32-bit microsecond counter compared only by unsigned difference
(so its wrap after about 71 minutes is harmless, C89 `unsigned long`
arithmetic, no 64-bit type), and every bound in section 5.2 is measured
against it. The clock is quantised to the tick (10 to 55 ms on these
targets), so the two kinds of bound are measured differently: a
**maximum** (a timeout, a deadline) expires when the measured difference
reaches it, which may be up to a tick late; a **minimum** (the 1 ms ring
settle, the 10 ms Downstream Port Reset hold, the 50 ms TMU convergence,
the 500 ms USB3 teardown gap, the Host Router Reset's 50 ms) is satisfied
only when the measured difference reaches the minimum **plus one tick
period**, since a difference of n ticks may be as little as n - 1 ticks
of real time; minimums under a tick are busy-waited with
`KeStallExecutionProcessor` instead; the host suites supply a fake clock. In the driver it is the
low 32 bits of the tick count (`KeQueryTickCount`) times the tick period
in microseconds (`KeQueryTimeIncrement`, in 100 ns units, divided by 10
once at start). Both are imports: on x86 `KeQueryTickCount` is a macro
reading the data export `ntoskrnl.exe!KeTickCount`, which design record 13
records as evidenced on Windows 98 SE (stock `battc.sys`), and
`KeQueryTimeIncrement` is evidenced there too (stock `uhcd.sys`); on amd64
the form the build produces is read from its import table. Section 14.3
adds them to the allowlists with that evidence. `KeQuerySystemTime`, which
the HCD already imports, is not used: system time can be set backwards. Waits
under a millisecond stay `KeStallExecutionProcessor` busy-polls, as record
21 section 4.3 allows on this thread only.

## 6. The connection manager

### 6.1 The host router

- **Read** ROUTER_CS_0 to 4 at route 0 (Guide 3.2.1 step 1): vendor and
  device, Upstream Adapter, Max Adapter (refused at 64 or more), depth,
  TopologyID Valid (ROUTER_CS_3 bit 31), USB4 Version.
- **TopologyID Valid clear**: enumerate it in one write of ROUTER_CS_1 to 4
  (depth 0, TopologyID 0, Valid, Notification Timeout 255, CMUV 1.0), then
  wait for Router Ready [ref] `enumerate_router`; Router Ready not seen is
  logged and not fatal for the host router, which Linux never waits on
  ([linux] `usb4.c` `usb4_switch_setup` returns at once for route 0).
- **TopologyID Valid set** (a pre-OS connection manager, this driver's
  earlier instance, `USB4CM.EXE`): verify depth 0 and TopologyID 0 and
  write nothing, because a connection manager shall not change DW1 to DW3
  once Valid is set (Guide 3.2.1 note). Section 9 decides the rest.
- **Adapters** 1 to Max Adapter: ADP_CS_0 to 5 each; ERR_ADDR or the
  "Unsupported Adapter" type marks it unused; the capability chain walked
  for the Lane (1), Adapter Configuration (4) and USB4 Port (6)
  capabilities, at most 16 links, undefined capabilities skipped and any
  order accepted (Guide 3.2.2; Spec 8.2.2) [ref] `discover_adapters`.
- **Hot plug enabled**: ADP_CS_5 Disable Hot Plug Events cleared on each
  lane 0 adapter with a USB4 Port Capability (section 2.2) whose port
  carries no preserved tunnel.
- **Capabilities, then buffers**: Get Capabilities and the Buffer
  Allocation Request on the host router, in that order and with section
  6.4's rules.

### 6.2 What version this connection manager claims

It writes CMUV 1.0 to every router it enumerates, version 2 routers
included, and enables none of version 2's features (no Set Capabilities
enabling the sequence bit, no extended encapsulation, no asymmetric link,
no Gen 4 link recovery). A preserved router may carry capabilities a
predecessor enabled; section 6.4 says which of those this driver honours
and how it learns of them. Linux writes CMUV 2.0 to a
version 2 router ([linux] `switch.c` `tb_switch_configure`); the Guide's
3.2.1 step 4 says the CMUV written follows the router's version. Whether a
version 2 router run by a version 1 connection manager tunnels PCIe is
untested anywhere this project can reach, and is open (section 16).

### 6.3 Ports and protocol adapters: the fabric mapping

- A USB4 port is a lane adapter carrying a USB4 Port Capability (lane 0)
  and the adapter after it (lane 1) (Spec 5.2.1; Guide 3.2.2).
- The Nth PCIe Down adapter and the Nth USB3 Down adapter in increasing
  adapter number belong to the Nth USB4 port (Spec 5.2.5; Guide 3.2.2;
  [linux] `usb4.c` `usb4_port_index`, `usb4_switch_map_pcie_down`,
  `usb4_switch_map_usb3_down`). A port with no PCIe Down adapter carries no
  PCIe tunnel; one with no USB3 Down adapter, no USB3 tunnel.
- The Get PCIe Downstream Entry Mapping operation (opcode `30h`, Spec
  Table 8-26, Tables 8-47 and 8-48) is issued once per entry when the host
  router answers it: the entry's PCIe Adapter Number cross-checks the
  pairing (a mismatch is logged and the pairing rule wins), and its
  non-FPB Routing ID names the bus, device and function of the tunnel
  port on the PCI side, which section 7.6 uses. A host may decline it (the
  Spec makes it optional for a host router), Linux never issues it, and
  the `intel-adl` model answers Not Supported [model]; the `minimal` host
  answers it with each tunnel port's routing id [model].

This is the **fabric mapping**, USB4 port to PCIe Down adapter. It names
no PCI device, except through the optional operation; section 7.6 keeps
the PCI mapping apart.

### 6.4 The device router on a port

1. **Connected?** Lane 0's Adapter State (LANE_ADP_CS_1 bits 29:26, Spec
   8.2.2.3) read and classified, here and wherever this record says a
   port is connected or not:

   | Adapter State | Class |
   |---|---|
   | 2 CL0, 3 transmitter in CL0s, 4 receiver in CL0s, 5 CL1, 6 CL2 | connected (a low-power state is a live link; this driver never enables one, but a link it preserves may be in one) |
   | 1 Training or Lane Bonding | transitional: re-read at the next check; connected for a router already UP; for a new connection, waited up to 2 s |
   | 7 CLd | disconnected (Guide 3.1.1, 3.4.1) |
   | 0 Disabled | disabled: never this driver's doing on lane 0; a port found so is HELD (section 9.2) |
   | reserved encodings, or the read failing | unknown: no action, re-read at the next check |

   Among lane states, only CLd is evidence of a disconnect (Guide 3.4.1);
   [ref] `port_connected` tests CL0 alone, which would mistake a preserved
   low-power link for an unplug. Section 7.1's other evidence (Lock set
   again, a changed identity, ERR_CONN or ERR_LOCK) starts a teardown
   whatever the lane state reads.
2. **Unlock**: ADP_CS_4 Lock (bit 31) cleared on the lane 0 adapter of
   the host's port (Guide 3.2.1). Only lane 0: Spec 8.2.2.1 leaves a write
   of Lock on a lane 1 adapter undefined, so lane 1 is never written.
3. **Read** ROUTER_CS_0 to 4 at route = the host's lane 0 adapter number,
   depth 1. ERR_ENUM or ERR_NUA: logged, left alone (Guide 3.2.1 step 1).
   USB4 Version major 0 (`0x00` to `0x1F`, whatever the minor): a
   Thunderbolt 3 router (section 8); majors 1 and 2 proceed; majors 3 to
   7 are reserved: UNSUPPORTED (section 3.5).
4. **Enumerate**: one write of ROUTER_CS_1 to 4 (depth 1, TopologyID = the
   route, Valid, Notification Timeout 255, CMUV 1.0); Router Ready within
   500 ms; retried up to 20 times 50 ms apart while the router is still
   coming up [ref] `u4cm_connect`.
5. **Adapters** as for the host; the Upstream Adapter (ROUTER_CS_1) is the
   device's lane 0, the next its lane 1; the first PCIe Up and USB3 Up
   adapters by type.
6. **DROM** by DROM Read (opcode `24h`, at most 16 dwords per operation,
   metadata dword address in bits 14:2 and size in 19:15), CRC-32 checked,
   the ASCII vendor and model entries parsed (Guide 2.5, 9.1; the USB4
   DROM Specification) [ref] `read_drom`; [linux] `usb4.c`
   `usb4_switch_drom_read`. A failure or Not Supported is logged and the
   device proceeds unnamed.
7. **UUID** from ROUTER_CS_7 and 8 ([linux] `usb4.c` `usb4_switch_read_uid`),
   kept as the port's identity (section 7.4).
8. **Capabilities**: Get Capabilities, below, before any request that
   depends on them.
9. **Buffer Allocation Request** (opcode `33h`, section 6.7) on the device
   router, kept for the credits, shaped by step 8's answer.

**Get Capabilities** (opcode `31h`, Spec 8.3.1.3.2, Tables 8-49 to 8-51) is
issued once on every USB4 router this driver uses, the ones it enumerates
and the ones it preserves, with Capability Index 0 in its metadata, which
returns every capability's supported and enabled bits in one answer (Guide
3.2.1 says a connection manager "shall" issue it; Linux does not, [linux]
`tb_regs.h` `enum usb4_switch_op` has no such opcode). It enables
nothing. It comes before that router's Buffer Allocation Request and
before any acknowledgement of that router's notifications, because a
preserved router keeps what its predecessor enabled until it returns to
the uninitialised state, and two of those change this driver's requests:

- **Sequence Bit in Notification Packet** (8.3.1.3.2.2) enabled: that
  router's notifications are acknowledged with the Enhanced Notification
  Acknowledgement (Spec 6.4.2.9) instead of the plain one (section 5.1).
- **Buffer Allocation Per USB4 Port** (8.3.1.3.2.3) enabled: that router's
  Buffer Allocation Requests carry the lane 0 adapter number in their
  metadata (Spec Table 8-56; Guide 5.1.4 step 3), one per port.
- **Hot Plug Failure Indication** (8.3.1.3.2.1) enabled: ERR_PLUG may
  arrive, already handled as an event.
- Any other enabled capability this driver does not implement: logged; the
  router is used as found where it can be.

What the answer establishes, and what it does not:

- **A router this driver enumerated itself** has every capability at its
  default, disabled, because enumeration starts from the uninitialised
  state; the operation's answer, or its absence, changes nothing.
- **A preserved router answering Not Supported** (ONS set): it has no such
  capabilities, so none is enabled.
- **A preserved router answering successfully** (ONS clear, Status 0): the
  returned supported and enabled bits are adopted as its capability state,
  with the consequences listed above.
- **A preserved router whose answer is anything else** - Status non-zero,
  OV never clearing, the router operation-blocked (section 6.7), a read
  failing - has **unknown** capability state. Nothing is assumed: no
  Buffer Allocation Request is sent to it (section 6.8's defaults apply),
  its notifications are counted and logged but not acknowledged, since
  either acknowledgement may be the wrong one, and the reason is logged.
  Notifications that arrive before the answer wait in the same way and
  are acknowledged once it settles the form.

### 6.5 Lane bonding (`XhciCmLaneBonding` 1, the default)

Following Guide 7.1, with Linux's preconditions ([linux] `switch.c`
`tb_switch_lane_bonding_enable`):

1. Already bonded (Negotiated Link Width in lane 0's LANE_ADP_CS_1, bits
   25:20, reads x2): nothing to do.
2. Possible: PORT_CS_18 Bonding Enabled (bit 8) on the device's upstream
   port, and both lane 0 adapters list dual width as supported. Otherwise
   single.
3. Lane 1 in CL0 on both sides, waited 1 s.
4. Target Link Width = dual (bits 5:4 = 3) on both lanes at both ends, then
   Lane Bonding (bit 15) on the host's lane 0 only.
5. Wait for x2 (500 ms). The lane 1 hot unplug event bonding produces is
   acknowledged and is not an unplug (section 7.1).
6. **Failed, impossible, or `XhciCmLaneBonding` 0.** No path may be built
   over two unbonded single-lane links (Guide 5), so the port proceeds
   single-lane only when that is verified. Lane Disable may be changed
   only with the Lane Adapter Configuration Capability at its defaults
   (apart from Lane Disable, Target Link Speed and Adapter State), no CLx
   state enabled over the link and the TMU mode Off (Guide 7.5, 7.5.1):
   1. Every Target Link Width and Lane Bonding field step 4 wrote, on all
      four adapters, is written back to the value read before step 4
      (Guide 7.5's precondition that the Lane Adapter Configuration
      Capability is at its defaults apart from Lane Disable, Target Link
      Speed and Adapter State).
   2. If both lane 1 adapters are disconnected or disabled (Adapter State
      CLd or Disabled): the link is single; go on to step 7.
   3. **The preconditions read back** on the host's lane 0 and lane 1
      adapters: Target Link Width and Lane Bonding at their Spec defaults
      (not merely at the values read before step 4, which a predecessor
      may have left non-default), the CL0s, CL1 and CL2 enables (LANE_ADP_CS_1
      bits 10 to 12) clear, and the link's TMU Off by section 6.6's
      TMU-Off predicate (TMU item 2). A router this driver enumerated
      itself starts with its TMU Off (Guide 7.4.3.1), and the host's port
      has had its TMU disabled at the previous unplug (section 7.3 step
      5); a reading that cannot be taken, or a link not Off, fails the predicate. A field not
      at its default that this driver did not write is not rewritten here
      (the link may carry another router's configuration): the port is
      FAILED, no path built.
   4. If the host's lane 1 is in CL0 and lane 0's Negotiated Link Width
      reads x1 (two single-lane links, Guide 7.5.1 step 2's state): set the
      host lane 1's Lane Disable (bit 14), then wait up to 500 ms for it to
      read Disabled; the router's unplug event for lane 1 is acknowledged.
      Verified: go on to step 7.
   5. Anything else - lane 1 training, the width not x1, Lane Disable not
      taking, a request failing - the port is FAILED with no path built.
      The Guide's route to two single-lane links from any other state is a
      Downstream Port Reset (7.5.1 step 1), which this driver does not
      issue on its own initiative.
   Linux keeps lane 1 enabled and builds the paths anyway; this record
   follows the Guide (section 2.2).
7. **USB4 Port is Configured**: PORT_CS_19 PC (bit 3) set on the host's
   lane 0 port and on the device's upstream port ([linux] `usb4.c`
   `usb4_port_configure`, `switch.c` `tb_switch_configure_link`).

### 6.6 Configuration Valid, and the TMU before it

The sequence on a device router, after bonding (section 6.5):

1. **ROUTER_CS_5**, one read-modify-write: PTO (bit 24) when the host's
   port has a PCIe Down adapter and the device a PCIe Up one; UTO (bit 25)
   when both USB3 adapters exist and the link is a USB4 link (PORT_CS_18
   TCM, bit 9, clear on the host's port; [linux] `usb4.c` `link_is_usb4`);
   HCO (bit 26) never; CNS (bit 23) left 0, as Linux leaves it. UTO is set
   whatever `XhciCmUsb3Tunnel` says when the router supports it, because
   the Guide says the connection manager "must" (3.2.3); with the value at
   0 the USB3 adapters are then disconnected (section 6.9).
2. **The TMU**, where a USB3 tunnel will be built (below).
3. **CV** (bit 31) in a second write, then Configuration Ready (ROUTER_CS_6
   bit 25) within 500 ms (Guide 3.2.3; [linux] `usb4.c`
   `usb4_switch_setup`, `usb4_switch_configuration_valid`). The TMU comes
   before CV as Linux orders it ([linux] `tb.c` `tb_scan_port`:
   "Configuration valid needs to be set after the TMU has been enabled for
   the upstream port"; Guide 7.4.3 says the same of a version 2 connection
   manager's adapter fields).

**The TMU** (Guide 7.4). PCIe has no accuracy requirement; a router that
tunnels USB3 needs accuracy level 1 or more (Guide 7.4.2), since a USB3
adapter that supports the TMU corrects its timestamps with the domain's
time (Spec 9.1.1.7). So the TMU is configured on a link exactly when this
driver will build a USB3 tunnel over it and finds it Off, and never on a
link it preserves:

1. **At connection** (Guide 7.4.3.1): Disable Time Sync (TMU_ADP_CS_6) set
   on the host's lane 0 adapter and the device's upstream lane 0 adapter,
   so no TMU packet crosses the link until it is configured - on a router
   this driver enumerates itself, whose TMU starts Off. A router whose TMU
   Router Configuration Capability reports Time Synchronization Protocol
   Not Supported gets no TMU on that link (Guide 7.4; the USB4 2.0 ECN
   "Configuration and Behavior When TMU is Disabled"), and its USB3
   tunnel, if any, is built without it, since its adapter has no time to
   correct.
2. **The current mode is read first**, as the **TMU-Off predicate** of
   the link, which section 6.5 uses too. It is a property of the link, not
   of the routers: the upstream (host) router's TSPacketInterval is shared
   by all its links, runs for the host router's own TMU (item 4) and is
   never required to be 0 nor cleared by this driver. Off (Guide Table 7-2
   and 7.4.3.4.2) when the **downstream** router's TMU_RTR_CS_3
   TSPacketInterval reads 0 (the field that drives a bi-directional link),
   TMU_ADP_CS_3 EnableUniDirectionalMode reads 0 on both lane 0 adapters
   (so the upstream router's rate drives no uni-directional mode on this
   link),
   and, on each router whose USB4 Version has major 2 (the fields
   are version 2 ones, read per router; on an older router they are taken
   as absent, as [linux] `tmu.c` `tb_switch_tmu_enhanced_is_supported`
   decides per router), its lane 0 adapter's TMU_ADP_CS_8 Enable Enhanced
   Uni-Directional Mode and TMU_ADP_CS_9 AdapterTimeSyncInterval read 0
   (Spec 8.2.1.2, 8.2.2.2, found by the capability walks; [linux] `tmu.c`
   `tmu_mode_init` reads each router's fields independently). Only a link
   that is Off is configured by this driver. A link in any other state - an
   adopted router whose predecessor configured some mode, complete or
   partial, consistent or not - is left exactly as found and gets no new
   USB3 tunnel: whether an inherited configuration really gives accuracy
   level 1 is not judged. A reading that cannot be taken: the same.
3. **The mode** (Guide 7.4.3), from Off: HiFi-Bi, since this connection
   manager is a version 1 one and never enables CLx; Linux makes the same
   choice on a version 1 link without CL1 ([linux] `tb.c` `tb_enable_tmu`).
   Never on two single-lane links (Guide 7.4.3.1): after bonding, or over
   lane 0 alone once lane 1 is disabled.
4. **Enable, inside a time disruption** (Spec 8.2.1.2 TMU_RTR_CS_0 Time
   Disruption; Guide 7.4.3.4 steps 2 and 7; [linux] `tmu.c`
   `tb_switch_tmu_post_time`, `tb_switch_tmu_enable`): set the device
   router's Time Disruption bit; post its local time from the host
   router's time (Guide 7.4.3.1 step 1: Post Local Time, Post Time low 1
   and high `0xFFFFFFFF`, then high 0, Post Time polled to 0 - Linux polls
   100 times 5 to 10 us apart, this driver for up to 100 ms, a project
   bound); set the downstream router's parameters per Guide Table 7-3
   (Freq Measurement Window 800, the averaging constants 8); the HiFi-Bi
   enable of Guide 7.4.3.4.1 step 3 (EnableUniDirectionalMode 0 on both
   ports, TSPacketInterval 16 in the downstream router, Disable Time Sync 0
   upstream then downstream); clear the Time Disruption bit; then 50 ms
   before any USB3 path (Guide 7.4.3.1 step 4). The host router's own TMU
   is enabled once at start, inside its own time disruption, as Linux's
   `tb_start` does for the root ([linux] `tb.c` `tb_start`, `tmu.c`
   `tb_switch_tmu_enable`), unless the host router is preserved with its
   TMU already running, which is then left as found.
5. **A failure** in any TMU step: the Time Disruption bit cleared again on
   every router this driver set it on, the link's TMU disabled again (item
   6), no USB3 tunnel over the link, the PCIe tunnel proceeding; the
   reason is logged, and a step that cannot be undone is recorded as the
   port's TMU quarantine (no USB3 on that port until it clears, re-tried
   each idle tick).
6. **Disable** on a link this driver configured, when its last USB3 tunnel
   is torn down with both routers present: Guide 7.4.3.4.2's HiFi-Bi branch
   (the downstream router's TSPacketInterval 0) and then Disable Time Sync
   1 on both lane 0 adapters, inside a time disruption. **At unplug**,
   with the downstream router gone, only the host's port remains: its lane
   0 adapter gets EnableUniDirectionalMode 0 and Disable Time Sync 1, and
   the host router's own rate is left alone ([linux] `tmu.c`
   `tb_switch_tmu_disable` disables time sync on the parent's downstream
   port the same way); a failure there is the port's TMU quarantine.

Exact register offsets and field positions of the TMU capabilities are
transcribed by task 40-0.1 from Spec 8.2.1.2 and 8.2.2.2 with [linux]
`tb_regs.h` as the cross-check. CLx is never enabled on any link
(`quirk_clx_disable`'s routers are thus satisfied by default, section
6.10).

### 6.7 Router operations: every one may be unsupported

A router operation is ROUTER_CS_9 to 24 data and ROUTER_CS_25 metadata
written, then ROUTER_CS_26 = opcode with Operation Valid (bit 31) in the
same write, OV polled clear within 500 ms, then Operation Not Supported
(bit 30) and Status (bits 29:24) read before any result (Guide 9.1; Spec
8.3.1) [ref] `router_op`. Three outcomes are kept apart and counted: not
supported, supported but failed (Status non-zero), and OV never clearing.

The Guide forbids a new operation, or any change to ROUTER_CS_9 to 26,
while OV is set (9.1). So before the first operation on a router, and
before every later one, ROUTER_CS_26 is read: OV set (an operation of a
predecessor's still running, or one of this driver's that timed out) is
waited out up to 500 ms; still set, the router is **operation-blocked**:
no write to ROUTER_CS_9 to 26 is made, no operation is issued, and every
use of an operation takes its "unsupported" branch below; the next
operation attempt re-reads OV and unblocks the router if it has cleared.
Since every operation is optional, a blocked router still tunnels.

| Operation | Opcode | Used for | When unsupported or failed |
|---|---|---|---|
| DROM Read | `24h` | names for the log | proceed unnamed ([linux] `usb4.c`: a host router may have no DROM) |
| Buffer Allocation Request | `33h` | baMaxPCIe, baMaxUSB3 (data dwords: index bits 15:0, value bits 31:16; index 1 USB3, 4 PCIe, 5 host interface; [linux] `usb4.c` `usb4_switch_credits_init`) | the preference defaults to 32 credits on a bonded lane, 16 on a single lane ([linux] `tunnel.c` `tb_pci_init_credits`); either way the credits written are bounded by section 6.8's accounting |
| Get PCIe Downstream Entry Mapping | `30h` | the tunnel port's routing id (section 6.3) | the pairing rule and section 7.6's other sources |
| Get Capabilities | `31h` | the inherited capability state of section 6.4 (Capability Index 0) | routers this driver enumerated: all disabled, whatever the answer; a preserved router: adopted on success, none enabled on Not Supported, unknown on any failure, with section 6.4's consequences |

Nothing else is issued, and none of the four is needed for a tunnel.

### 6.8 Paths and the PCIe tunnel

The four path entries ([ref] `program_tunnel_paths`; Guide 5.3.1, Table
5-2; Linux's `tunnel.c` `tb_tunnel_alloc_pci` and `tb_pci_init_path` build
the same shape with HopID 8 at both protocol adapters):

| Entry | Ingress | Output | Attributes |
|---|---|---|---|
| downstream, segment 1 | host PCIe Down, HopID 8 | host lane 0, HopID `hd` | priority 3, weight 1, EFC 1, ESE 0; credits, IFC and ISE as read |
| downstream, segment 2 | device lane 0, HopID `hd` | device PCIe Up, HopID 8 | priority 3, weight 1, IFC 1, EFC 0, ISE 0, ESE 0, credits per the accounting below |
| upstream, segment 1 | device PCIe Up, HopID 8 | device lane 0, HopID `hu` | as downstream segment 1 |
| upstream, segment 2 | host lane 0, HopID `hu` | host PCIe Down, HopID 8 | as downstream segment 2, the host's credits |

`hd` and `hu` are allocated per lane adapter: the lowest free HopIDs from
8 up, which is 8 and 9 on an empty port as `usb4cm` and the pre-OS model
use [ref] [model], skipping every HopID section 9.2 reserved for a
discovered tunnel, and within the Guide's HopID rules (5.2.1: at least 8,
at most the egress adapter's Max Output HopID and the next ingress
adapter's Max Input HopID, read from ADP_CS_5 bits 6:0 (Max Input
HopID) and 17:11 (Max Output HopID), Spec 8.2.2.1; an input HopID once per
ingress adapter) ([linux] `switch.c` `tb_port_alloc_hopid`, `path.c`
`tb_path_alloc`). Each entry is one read of its two dwords and one write (Guide
5.2.1), source adapter first along each path.

**Credit accounting**, per lane adapter that is a path's ingress (Guide
5.1, 5.1.4; Spec 5.3.2.1.1.1 to 5.3.2.1.1.4, 8.2.2.1; [linux] `tunnel.c`
`tb_available_credits`, `tb_pci_init_credits`). The lane's buffers are
ADP_CS_4 Total Buffers (bits 29:20), and each pool is counted once:

- the control path's dedicated credits (the HopID 0 entry's Path Credits
  Allocated, Spec 8.2.3.1);
- the flow-control-disabled pool, ADP_CS_4 Non Flow Controlled Buffers
  (bits 9:0), whatever uses it;
- the shared pool, ADP_CS_5 Link Credits Allocated (bits 28:22), whatever
  uses it (shared and restricted-shared paths draw on it and add nothing
  of their own);
- the dedicated credits of every other entry on the adapter whose ingress
  flow control is on and shared buffering off: Valid entries, discovered
  (section 9.2) or this driver's, and quarantined entries (section 7.3)
  until they are proven drained.

What is left is what new paths may take. A new PCIe path gets the lesser
of the router's preference (baMaxPCIe from that router's Buffer
Allocation Request, section 6.7, else 32 on a bonded link and 16 on a
single lane) and what is left after reserving the USB3 preference
(baMaxUSB3, else the same defaults) when a USB3 tunnel will also be built
on that port; below 6 (`TB_MIN_PCIE_CREDITS`) no PCIe tunnel is built and
the reason is logged. The USB3 path takes the lesser of its preference and
what the PCIe path left, with the same floor. Total Buffers is re-read
after bonding, since it changes with the link width ([linux] `switch.c`
`tb_port_update_credits`). The host router's own lane adapters are
accounted the same way with the host's Buffer Allocation answers; a
Thunderbolt 3 router's, as section 8 says.

The sequence:

1. Both PCIe adapters' LTSSM (ADP_PCIE_CS_0 bits 28:25) in Detect (0)
   within 500 ms (Guide 5.3.1 step 1). Not reached: no path is written,
   the port's PCIe tunnel has failed, logged. This is section 10.1's gate.
2. The downstream path, then the upstream path.
3. Path Enable (ADP_PCIE_CS_0 bit 31): the device's PCIe Up first, then
   the host's PCIe Down (Guide 5.3.1 steps 5 and 6; [linux] `tunnel.c`
   `tb_pci_activate`).
4. LTSSM L0 (6) on the host's PCIe Down adapter within 1 s [ref].
5. On a failure after any write: the orderly teardown of section 9.3 step
   1 for exactly the entries written, then the port is FAILED (section
   7.2).

**Approval** (`XhciCmApprove`, record 21 decision D4 carried over, decision
U5): at 1 the PCIe tunnel is built for every device router; at 0 none is
built for a router this driver enumerated, a tunnel preserved from before
(section 9) stays, and USB3 is unaffected, since a USB3 tunnel gives the
device no access to memory. Linux likewise builds USB3 tunnels at scan and
PCIe tunnels only on approval ([linux] `tb.c` `tb_scan_port`, the domain's
`approve_switch` = `tb_tunnel_pci`).

### 6.9 The USB3 tunnel (`XhciCmUsb3Tunnel` 1, the default)

1. At least 500 ms since any earlier USB3 path teardown on that host
   port (Guide 5.5.1 step 1). The time of the last USB3 teardown is kept
   per host USB4 port, outside the port's connection generation, so a
   quick re-plug cannot skip the wait. And the link's TMU at accuracy
   level 1 or more, configured as section 6.6 says, or the device router
   reporting no TMU support; a link this driver preserves with its TMU
   Off gets no USB3 tunnel, since its configuration is not changed.
2. Bandwidth, on the host's USB3 Down adapter only ([linux] `usb4.c`
   `usb4_usb3_port_allocate_bandwidth`; Guide 6.2.2): set ADP_USB3_CS_2 CMR
   (bit 31); wait for ADP_USB3_CS_1 HCA (bit 31) within 1.5 s; read the
   consumed bandwidth (ADP_USB3_CS_1 bits 11:0 up, 23:12 down) and its
   scale (ADP_USB3_CS_3 bits 5:0); compute the allocation and write it
   (ADP_USB3_CS_2 bits 11:0 up, 23:12 down) at the smallest scale that fits
   12 bits; clear CMR and wait for HCA to clear. The allocation, in order
   ([linux] `usb4.c` `usb4_usb3_port_max_link_rate`, `tunnel.c`
   `tb_tunnel_alloc_usb3`; Guide 6.2.2.1, 6.1.2.3): each adapter's maximum
   link rate (ADP_USB3_CS_4 bits 18:12), the host's first clamped by
   section 6.10's quirk; the lower of the two; 90 per cent of that; then
   no more than the link's available bandwidth, which is 90 per cent of
   its raw rate (Guide Table 6-2, from LANE_ADP_CS_1's current speed and
   width) less 1.5 Gb/s kept for the PCIe path; never below the consumed
   value. A port carrying a preserved DisplayPort tunnel (section 9.2),
   whose consumption this driver does not account, gets no USB3 tunnel,
   logged. HCA never arriving: no USB3 tunnel, logged; the Guide forbids
   touching the allocation fields while HCA is 0. The Ack comes from the
   host's own xHCI hardware, not from its driver.
3. The same four-entry shape, the lane HopIDs from section 6.8's
   allocator (10 and 11 on an otherwise empty port), priority 3,
   weight 2 ([linux] `tunnel.c` `TB_USB3_WEIGHT`); the lane-ingress
   entries take the USB3 allocation section 6.8's accounting computed
   (never the raw preference), the protocol-adapter ingress entries keep
   their credits, IFC and ISE as read, as for PCIe.
4. ADP_USB3_CS_0 Path Enable (bit 31) with Valid (bit 30) on the host's
   USB3 Down adapter, then on the device's USB3 Up (Guide 5.5.1 steps 4 and
   5; [linux] `tunnel.c` `tb_usb3_activate`).
5. **Unwinding a failure**, whatever step failed, each acquired resource
   released in reverse and the PCIe tunnel never touched: a CMR this
   driver set is cleared on every exit, and HCA waited out (1.5 s; CMR that
   will not clear or HCA that will not drop leaves the adapter's
   bandwidth state quarantined and no further USB3 attempt on that port
   until it clears, re-checked each idle tick) ([linux] `usb4.c`
   `usb4_usb3_port_allocate_bandwidth` clears the request on its error
   exit); an allocation written is released as section 7.3 step 4 does;
   path entries written are invalidated and drained, an enable written is
   undone (Path Enable 0 with Valid 1), each with section 7.3's quarantine
   if it fails. The USB3 tunnel is then FAILED on that port, the PCIe
   tunnel unaffected.

With `XhciCmUsb3Tunnel` 0 both USB3 adapters are disconnected instead:
Path Enable 0 with Valid 1 (Guide 5.5.3), which removes the far-end
termination so the device's hub falls back to its USB 2.0 connection. A
USB3 tunnel's devices appear as an ordinary SuperSpeed connect on a root
port of the host's own xHCI controller ([model] `XHCI98-HANDOFF` section
2), which is `xhci98.sys`'s where it drives that controller; nothing in
`xhci98.sys` changes.

### 6.10 The quirk matrix

Quirks match the router configuration space's vendor and device; Intel's
USB4-era routers report vendor `8087`, not `8086`, and an integrated host
router's device id equals its NHI's PCI device id ([linux] `quirks.c`
`tb_check_quirks`). One pure table in `src\cm\`, matched once per router
at enumeration, logged when it fires, host-tested row by row:

| Linux quirk | Applies to | Here |
|---|---|---|
| `quirk_usb3_maximum_bandwidth` ([linux] `quirks.c`) | host routers `8087`: `463E` `466D` (Alder Lake), `A73E` `A76D` (Raptor Lake), `7EB2` `7EC2` `7EC3` (Meteor Lake), `5781` `5784` (Barlow Ridge host), `5786` `57A4` (Barlow Ridge hub) | the host USB3 Down adapter's maximum link rate clamped to 16376 Mb/s before section 6.9 takes 90 per cent of it |
| `quirk_clx_disable` | Titan Ridge DD `8086:15EF` with NVM below `0x65`; AMD routers vendor `0438`, devices `0208` to `020B` | CLx is never enabled on any router, so the quirk is satisfied by the default and is kept in the table so a later CLx enable cannot miss it |
| `quirk_dp_credit_allocation` | Goshen Ridge `8087:0B26` | nothing: no DisplayPort |
| `quirk_block_rpm_in_redrive` | Barlow Ridge host | nothing: no runtime power management |
| `quirk_force_power_link` | Dell WD19TB (DROM `00D4:B070`, `B071`) | nothing: no firmware upgrade |
| `QUIRK_E2E` ([linux] `nhi.h`, `pci.c` `nhi_pci_check_quirks`) | Falcon Ridge NHIs, when end-to-end flow control is enabled on a ring | nothing: ring 0 runs without it, and Falcon Ridge is an ICM row |
| `QUIRK_AUTO_CLEAR_INT` | every Intel NHI | nothing: section 4.4 step 1 |

Linux's per-router helpers ([linux] `tb.h` `tb_switch_is_alpine_ridge`,
`tb_switch_is_titan_ridge`, `tb_switch_is_tiger_lake`) decide CLx and the
Titan Ridge PCIe L1 setting, neither of which this profile does; they are
named in the table for the Thunderbolt 3 routers of section 8.

### 6.11 A real device router: the ASMedia ASM2464PD

The first USB4 device router captured anywhere this project can read is
the ASMedia ASM2464PD of an ORICO AAGM2-U4 NVMe enclosure, on the
Framework Laptop 13's Alder Lake host under Linux's software connection
manager ([obs] `usb4-emu` `docs/captures/ORICO-ASM2464.md`, `main`
`5ba7f60`). `usb4-emu` reproduces it as `usb4-device,device-model=asm2464`
([model] QEMU fork `20655c3`, `hw/usb4/usb4-router-asm2464.c` and the
generated `usb4-asm2464-data.h`; 97.1 per cent of the capture's
device-router dwords replayed equal). Nothing in it is ASMedia-specific
in this driver: no row of section 6.10 matches `174C:2463`, and every
item below is met by a rule that holds for any router.

| What the capture shows | What this driver does |
|---|---|
| A Thunderbolt 3 compatible DROM (revision 1, entries from byte 22), whose Unused Adapter Entry marks adapter 4 unused (a USB3 Up adapter by its number, [inf]: its type register was never read; the model types it USB3 Up); Linux leaves it uninitialised ("disabled by eeprom") and builds no USB3 tunnel | The DROM is read before the adapters (as [linux] `switch.c` `tb_switch_add` reads it before `tb_init_port`), its entries parsed from byte 16 in a revision 3 DROM and from byte 22 otherwise ([linux] `eeprom.c` `tb_drom_parse`, `TB_DROM_HEADER_SIZE`, `USB4_DROM_HEADER_SIZE`); an adapter entry with its disabled bit set takes that adapter out of the router: never read or written, so no UTO for it, no USB3 tunnel and no TMU (section 6.6 configures the TMU only for a USB3 tunnel). A DROM whose CRC fails, or whose entry list runs past its data, is not believed at all and every adapter is used, logged. So is one longer than 512 bytes (`TBT_U4_DROM_MAX`; the 12-bit Length field allows 4108), logged as `u4.drom.length` with the length read: its Unused Adapter entries are then not applied. The router's own upstream lanes are never taken out. A preserved router below depth 1 (section 7.1's links) has its DROM read once when its link is found, and the same mask holds when its parent's side is torn down at its removal. The vendor and model names come from the same entries ("ASMedia", "246x") |
| No CL0s, CL1 or CL2 support (LANE_ADP_CS_0); PORT_CS_18 without Cable Gen 3 Support or CLx Protocol Support | Nothing: no CL state is ever enabled (sections 6.6, 6.10), and neither PORT_CS_18 field is read |
| The router's TMU capability at `C0h`, then vendor-specific capabilities with IDs 1, 3 and 6 (the last a long one) | The chain is walked (section 6.1) and the TMU found where it is. A vendor-specific capability is taken as the Plug Events or link controller one only on a Thunderbolt 3 router: on a USB4 router those IDs are its vendor's |
| NVM Sector Size (`25h`) completes with Status 2; Linux tries it three times | Never issued: section 6.7's four operations are all this driver sends |
| Router Ready not yet set 0.4 ms after Enumerated (the model: 5 ms) | Section 6.4 step 4's 500 ms poll; past it, the next try re-reads the router and waits for Router Ready again without a second enumeration write |
| Buffer Allocation answers PCIe 64 alone (Metadata 1); lanes of 66 Total Buffers that bonding leaves at 66; Max HopID 27 | PCIe lane credits 64, the preference bounded by the 66 less the control path's 2 (section 6.8); no USB3 preference is needed with no USB3 adapter |
| The PCIe Up adapter's HopID 8 entry resets to output HopID 8 with no credits | Its credits, IFC and ISE kept as read, as for every USB4 protocol adapter ([linux] `path.c` `tb_path_activate` keeps them too) |
| After a Downstream Port Reset the router answers again about 594 ms after the release, freshly reset | A port reset is issued only by `XhciCmPreOsCleanup` 1 and 2 (section 9.3); its 2 s wait for lane 0 back in CL0 covers the 594 ms, and the port then connects as new: one enumeration, no retry |
| Router configuration space writes answered with Adapter Num 0 | Already met: the ring compares Adapter Num outside the router space only (`tbt_ring.c`, task 40-A.5) |
| A PCIe switch (`1B21:2463`, an upstream and one downstream port) in front of the NVMe | Nothing in the connection manager: the tunnel port's re-enumeration and eject act on its whole subtree (section 7.6). The model builds the switch only behind a q35 tunnel port; on the i440FX tunnel bridge of the Windows 98 SE and 2000 guests the NVMe sits alone, so the switch has not run here |

The host suite's fake fabric carries the same device as a preset
(`FAB_DEV_ASM2464`, from the capture document and the model's data, not
from this driver), the `usb4-emu` cross-check links the model itself
(section 14.1), and run-40 records the guests.

## 7. Hot plug, unplug, and the PCI side

`usb4cm` acknowledges hot plug events and does nothing more after its run
[ref] README. Everything in this section is designed here, from Guide 3.1,
3.4, 4.3, 5.2.2, 5.3.2 and 7.3 and Spec 4.4.5.2, 6.8 and 6.9, with Linux's
`tb.c` `tb_handle_hotplug` as the cross-check.

### 7.1 Four ways a change is noticed, and one truth

1. A hot plug event (section 5.1), from the host router for a lane adapter
   of one of its ports.
2. The idle poll's port scan: each tick reads lane 0's LANE_ADP_CS_1 and
   ADP_CS_4 for every host port, two reads per port (Guide 3.1.1 and 3.4.1
   allow periodic scanning in place of events); for a port with a PCIe
   tunnel up, the host PCIe Down adapter's ADP_PCIE_CS_0, a third; and,
   for each entry of `nvme98`'s link-state table that reads `UP` behind the
   port's known member (record 21 section 7.2), one PCI configuration read
   of that function's vendor and device id through `HalGetBusData`, the
   presence sample of section 10.3 (`cm_nvlinkos.c`'s `CmNvlinkService`,
   handed to the core as `TbtU4PresenceSample`).
3. ERR_CONN or ERR_LOCK answering a request to a device router's route
   (Spec 6.4.3.2: a disconnected adapter, or a connected one that is locked
   again, meaning it was disconnected since).
4. **The PCIe tunnel lost with the router still there**: on a port whose
   router is still connected and unchanged, the host PCIe Down adapter's
   Path Enable read clear, or its LTSSM (ADP_PCIE_CS_0 bits 28:25, Spec
   8.2.2.7 and its LTSSM encodings) read Detect (0). L0 (6), L1 (7),
   Recovery (4) and Recovery.Idle (5) are a live link - a PCIe power state
   or a link recovery, which disabling USB4 CLx does not prevent - and any
   other encoding is transitional: re-read at each idle tick, and a loss
   only if it persists for 2 s. On a loss the tunnel is down though nothing
   was unplugged (the emulator's admin-down is such a case, `ARCHITECTURE`
   section 4.3). Its PCIe tunnel, and only that, is then torn down: the
   orderly form of section 9.3 step 1 (both ends reachable), followed by
   section 7.3 step 1's read-back (sub-step 3: Path Enable 0 on both
   adapters, every entry's Valid 0 and Pending Packets 0, the host
   adapter's LTSSM in Detect), then section 7.3 steps 2, 3 and 6 for the
   PCI side, the statement and the removal. The port is FAILED for PCIe
   until a re-plug; its USB3 tunnel, if any, stays up and remains an
   obligation of section 7.6.

Each only queues a **port check**, and the check decides from registers,
never from an event's polarity: lane 0's class (section 6.4 step 1); Lock set again on a
port this driver unlocked (the router sets Lock when the port disconnects,
Guide 3.2 note 2), which means a disconnect happened whatever the state
reads now; and, when the port is UP, the device router's TopologyID Valid
and UUID still the same (section 7.4). Events for lane 1 (bonding produces
one), for DP adapters (the `intel-adl` host sends one per DP IN at every
enumeration [model]) are acknowledged and only counted. Events from a
device router's own downstream ports are acknowledged too, and then depend
on what is behind that port: where section 9.2 discovered a preserved
topology (a daisy chain a predecessor configured), the event queues a
check of that link exactly as for a host port - the parent's downstream
lane 0 class, its Lock, and the child router's TopologyID Valid and UUID -
and the port scan of each idle tick includes every such discovered link,
so removal of a preserved router at any depth is noticed; where nothing
was discovered, the port is never unlocked, so a new router there stays
uninitialised, which the Guide allows (3.1.1, "or chose not to initialize
the Router"). A removal found below depth 1 tears down, on the routers
that remain, the side of each discovered path through the removed router
(Guide 3.4.1), as section 7.3 does at a host port, with the same
quarantine; PCIe paths are hop by hop between neighbouring routers (Guide
5.3.1), so that side is the parent router's. Its PCI side is a function
below the depth-1 router's own PCIe switch: on 9x record 21 section 6.1's
removal walk runs recursively below the domain's tunnel ports; on NT only
a rescan of the bus it sat on would notice, which no filter here reaches,
so that removal is left to the endpoint's own driver and is a known
limitation (section 16); no `nvme98` statement is sent for it. Linux likewise ignores lane 1 events, events for
a port that already has a router, and events on unknown routes ([linux]
`tb.c` `tb_handle_hotplug`).

### 7.2 The port state machine

One state per host USB4 port, on the thread:

| State | Meaning | Leaves on |
|---|---|---|
| EMPTY | lane 0 disconnected (CLd) | a check finding it connected: CONNECTING |
| CONNECTING | section 6.4 (or section 8) running | enumerated: BONDING; an unplug: TEARDOWN; ERR_ENUM, ERR_NUA: UNSUPPORTED; enumeration failed 20 times: FAILED |
| BONDING | section 6.5 (section 8 item 3 for a Thunderbolt 3 router) | done or single: CONFIGURING for a USB4 router, TUNNELLING for a Thunderbolt 3 router, which has no Configuration Valid handshake (section 8 item 2); the verified fallback failing: FAILED |
| CONFIGURING | section 6.6 | Configuration Ready: TUNNELLING; timeout: FAILED |
| TUNNELLING | sections 6.8 and 6.9, then the PCI side (section 7.6) | the PCIe tunnel up and its tunnel ports re-enumerated, or approval 0 with USB3 handled: UP; failure: FAILED |
| UP | tunnels up, or preserved from before (section 9) | an unplug: TEARDOWN; the PCIe tunnel lost with the router present (section 7.1 item 4): FAILED for PCIe, the USB3 tunnel kept with its resume obligation |
| HELD | a port whose configuration did not verify and showed no removal evidence: found so at start (section 9.2), or a port holding a resume obligation at resume (section 7.6); left exactly as found; a live preserved tunnel on it remains an obligation | an unplug: TEARDOWN of whatever this driver can read of it, host side only |
| FAILED | enumerated, but some step failed; whatever did succeed (a USB3 tunnel, say) stays | an unplug: TEARDOWN; only a re-plug retries |
| UNSUPPORTED | a router this driver will not drive | an unplug: EMPTY |
| TEARDOWN | section 7.3 | done: EMPTY, then a check (the lane may already be connected again) |

Each port has a **generation** incremented at every entry to CONNECTING;
every multi-step sequence re-checks it, and the port's pending-unplug flag,
after each request.

### 7.3 Unplug: the teardown

When a check finds the router gone (lane 0 in CLd, Lock set again, ERR_CONN
on its route, or a different router) on a port that had one, the steps run
in this one order, everything the `nvme98` statement depends on first
(section 10.3):

1. **The host side of the PCIe tunnel** (Guide 3.4.1 tears down every path
   that ends at or traverses the removed router; Guide 5.3.2, with "the
   relevant steps that target the removed Router" skipped; Guide 5.2.2):
   1. The host's PCIe Down adapter's Path Enable read, and written 0 if
      set. The host router resets it to its default itself when the port
      disconnects (Spec 4.4.5.2), so normally this is a read.
   2. PATH_CS_0 Valid cleared on the host PCIe Down adapter's HopID 8 entry
      and on the host lane 0 adapter's `hu` entry, nothing else changed,
      then PATH_CS_1 Pending Packets (bit 28) polled to 0 on each within
      500 ms, then at least 2 us (Guide 5.2.2; Spec 5.4.2: an ingress
      adapter drops every packet for a path once its Valid is 0 and
      dequeues what it holds; Table 5-16 tTeardown).
   3. Read back: Path Enable 0, both entries' Valid 0, Pending Packets 0,
      and the host PCIe Down adapter's LTSSM in Detect (within 500 ms).
   Linux does the same through `tb_free_invalid_tunnels` and
   `tb_tunnel_deactivate` (PE 0 on the down adapter first, the device's
   write allowed to fail, then the paths; [linux] `tunnel.c`, `path.c`
   `tb_path_deactivate`).
2. **The PCI-side reading** of the port's member of the tunnel-port set
   (record 21 section 6.5), or of every member when the correspondence is
   unknown (section 7.6), in which case it serves the removal and not the
   statement.
3. **The statement** (section 10), sent only when every one of these
   holds: step 1's requests all succeeded and its read-back matched; step
   2's reading of the port's own known member came clear in time; the
   deadline of section 10.3 has not
   passed; an entry of the link-state table reads `UP` behind that member;
   the condition is the qualified one on that OS (section 10.2). Then the
   entry moves to `DOWN_QUIESCED`, and step 6's removal waits for its
   acknowledgement (record 21 section 7.2). Otherwise the statement is
   withheld and the reason counted.
4. **The host side of the USB3 tunnel** the same way (Guide 5.5.2, its
   first step, on the removed router, skipped): Path Enable 0 with Valid 1
   on the host's USB3 Down adapter, its two host-side entries invalidated
   and drained, the bandwidth released to the consumed value but never
   below 900 Mb/s each way ([linux] `usb4.c`
   `usb4_usb3_port_release_bandwidth`) when HCA answers.
5. **The port's lane adapters back to their defaults** where this driver
   changed them (Guide 3.4.1: the TMU disabled on the host's port as
   section 6.6's TMU item 6 says for an unplug; USB4
   Port is Configured, cleared on the host's port; Link Credits, which the
   router reloads itself, Spec 4.4.5.2): Target Link Width and Lane Bonding
   restored, and lane 1's Lane Disable cleared if section 6.5 set it
   ([linux] `switch.c` `tb_switch_unconfigure_link`,
   `tb_switch_lane_bonding_disable`).
6. **The OS side**: the removal and re-enumeration of the port's tunnel
   ports (section 7.6), as record 21 sections 6.1 and 6.2 do them.

A step that cannot be done because a request fails (the host router does
not answer, a Valid bit will not clear, Pending Packets never reaches 0) is
logged and counted, and the teardown continues with the next step (step
3's rule says what that does to the statement). **What did not complete is
quarantined**, never reused: each path entry not proven invalid and
drained (Guide 5.2.2: Valid 0, Pending Packets 0, then the reuse delay) is
recorded as a reserved HopID on its adapter, with its credits counted as in
use, and a protocol adapter whose Path Enable would not clear is recorded
as blocked. The allocator of section 6.8 skips quarantined HopIDs; a port
whose PCIe Down (or USB3 Down) adapter is blocked, or whose HopID 8 entry
there is quarantined, builds no tunnel of that kind until the quarantine is
lifted. Every idle tick retries the outstanding invalidations and drains;
one that succeeds lifts its quarantine. Only a proven invalidation and
drain lifts it - by those retries, or by an explicit cleaning of section
9.3 that succeeded on that entry (a cleaning that was requested and
failed lifts nothing). The end of this driver instance does not lift it
either: the successor's discovery (section 9.2) finds every such entry
again from the registers, an undrained one included, and quarantines it
afresh.

### 7.4 Unplug in the middle of something

- **Inside a request's response wait** the event is decoded and queued
  [ref] `u4_xfer`, and that wait ends within its bound (section 5.2).
- **Two kinds of wait, kept apart.** A *transport wait* is the delay
  inside a request's own response loop ([ref] `u4_xfer` calls the delay
  callback while it waits); it decodes and queues frames and does nothing
  else, because a request is outstanding and only one may be (Guide 4.2).
  A *safe point* is a call the connection manager's own sequences make
  between requests (each poll loop of section 5.2, the waits of sections
  6 and 9); the core gets a second callback for it, distinct from
  the delay. Only a safe point services the queue.
- **At a safe point**, a pending unplug runs its port's teardown (section
  7.3 steps 1 to 3) at once, nested, because it touches only the
  unplugged port; its steps 4 to 6 are queued for after
  the outer step, as record 21 section 4.4 does for a disconnect inside a
  command wait. A teardown in progress sets a flag: safe points inside it
  do not nest a second teardown, and further pending unplugs wait until it
  returns, so recursion is at most one level.
- **This port's own sequence** sees the generation change or the
  pending-unplug flag after its next request, or ERR_CONN from the request
  itself, and stops; the teardown then undoes exactly what the sequence
  had written (each entry and enable bit is recorded as it is written).
- **A quick unplug and re-plug** between two checks: lane 0 reads
  connected both times, but Lock reads set again, or the device router's TopologyID Valid
  reads clear (a fresh router), or its UUID differs; any of the three is a
  teardown followed by a connect. Linux relies on the unplug event arriving
  before the plug and compares the UUID only on resume ([linux] `switch.c`
  `tb_switch_resume`); this driver checks at every port check, because a
  poll can miss the instant between.

### 7.5 Re-plug

A re-plugged router is a new router: CONNECTING from the start, a new
generation, a new `UP` with a new link generation in `nvme98`'s table
before the re-enumeration exposes the endpoint (record 21 section 7.2),
and the Detect gate of section 6.8 before any path. Nothing is remembered
about the previous one except what is kept per host port rather than per
connection: the counters, section 7.3's quarantine, which the new router's
paths respect, and the time of the last USB3 teardown (section 6.9 step
1).

### 7.6 The PCI side: the domain's tunnel-port set

Record 21 section 6.3 defines, for both profiles, a domain's
**tunnel-port set**: the PCI bridges a tunnel of that domain lands on,
each with the source that put it there and its own state, the fabric
mapping (port to PCIe Down adapter) kept apart from the PCI mapping
(domain to bridges), and exclusive ownership (a port two domains claim is
ambiguous for both and inactive). Its sources apply here unchanged: the
per-domain override `XhciCmTunnelBridge` (the whole set when present), the
discrete switch's own ports by id (which covers Maple Ridge in USB4 mode
with its `1133` and `1136` bridges), the integrated root ports listed in
record 21 section 3.3 by processor family - only one NHI of that family on
the root bus, otherwise ambiguous until the override assigns them - and
the emulator's SHPC bridges. Section 3.7 adds the USB4-only hosts' port
ids to that list as task 40-0.2 reads them. This profile adds one source
and one rule:

- **Router-named** (USB4 profile only, after the override): where the host
  router answers the Get PCIe Downstream Entry Mapping operation (section
  6.3), each entry's non-FPB Routing ID names the bridge of one PCIe Down
  adapter. A named location where a PCI-to-PCI bridge answers joins the set
  with source "router-named", and the port-to-bridge correspondence is then
  known for that adapter. It goes through record 21's exclusive-ownership
  check like any other claim.
- **Learned correspondence**: when a tunnel comes up on a port whose
  bridge is not router-named, the member whose secondary bus gained the
  new function is recorded as that port's, for the life of the instance.

As built (task 40-A.4, `tbt_topo.c`'s `XhciTbtTopoAddNamed` and its
learning calls), with the choices marked as the orchestrator's of
2026-10-15, for the owner to confirm:

- A router-named bridge joins the set only when the discovery's outcome
  was a set (`SET`), no candidate (`NO_CANDIDATE`), hot-plug bridges no
  source names (`UNLISTED`) or ambiguous listed root ports (`AMBIGUOUS`),
  and only where a PCI-to-PCI bridge answers that is neither the NHI nor
  its parent. A bridge already a member keeps its own source and is bound
  to the port (orchestrator). An ambiguous listed root port the router
  names joins too, and record 21's exclusive ownership decides it as any
  claim (orchestrator). Under the override, the override stays the whole
  set: a named member is bound, any other location refused. After
  `OVERRIDE_BAD`, `INCOMPLETE` or `MANY`, or with no discovery, every
  naming is refused.
- A named location that cannot be read voids the discovery as
  `INCOMPLETE`, and one member past the set's room voids it as `MANY`:
  the set is emptied and found again, never left without a named port.
- Learning is a new device (a function 0 newly answering) on one member's
  secondary bus while that port is the only PCIe tunnel up and not yet
  attributed. A port stays pending until it is learned or proven down - a
  teardown whose read-back proved the path closed, or the router's removal
  evidence of section 7.1 items 1 to 3; a tunnel-down action alone is not
  proof - and while another port is pending no gain is attributed. A
  member with no secondary bus blocks learning for the domain, and a
  conflicted port's live tunnel blocks it until that tunnel is proven down
  (orchestrator).
- Bindings, named or learned, are kept by bridge for the instance's life.
  A port bound to two bridges, or a bridge to two ports, withdraws both
  bindings for the rest of the instance (orchestrator); the bridges keep
  their membership, so the whole set acts for them.

A tunnel change acts on the port's own member when the correspondence is
known (router-named or learned), and on the whole set otherwise, as record
21 section 6.3 does for every change ("a set of more than one"): every
member is re-enumerated, which is harmless on an unchanged bus. The
`nvme98` statement needs the one member its condition is read on; record
21 sends none while the set has more than one member, and this profile
sends one only when the port's member is known by correspondence, never
from the whole set (section 10.2).

**Safe eject** (task 39-A.8, record 21 section 7.4) applies only to the
children of active members; the filter never reports a matched root port
itself removable.

**Resume ordering** is record 21 section 6.3's hold, unchanged, since the
filter is shared: at its own system suspend the NHI marks every active
member held; a D0 IRP reaching a held member's filter is passed down and
retained in its completion routine; the NHI releases it when its resume
has **succeeded** or **failed**; the filter's watchdog is record 21's
fixed 5-minute cap, never longer (the NHI's resume bound plus 10 s is
logged, and logged as over the cap when it would pass it), and its expiry
is the failure path, never success; an NHI
that does not resume takes the failure path at its removal. Under this
profile:

- **What must be restored** is tracked per tunnel, not per port state:
  at suspend the NHI records every tunnel that is up on the domain - each
  PCIe and USB3 tunnel this driver built or preserved, whatever its
  port's state (an UP port; a FAILED port whose PCIe tunnel succeeded
  before its USB3 failed; a HELD port's preserved live tunnel) - as an
  obligation, with its port, generation and HopIDs.
- **Succeeded** means every obligation is met: its tunnel verified by
  section 9.2's reads; or, where the domain lost its configuration across
  the sleep (the device router uninitialised, its link connected again)
  and the port is one this driver built, rebuilt through sections 6.4 to
  6.9 with the Detect gate of section 6.8, before the hold is released; or
  its port showing removal evidence of the router (section 7.1 items 1 to
  3: CLd, Lock set again, a changed identity, ERR_CONN), torn down
  (section 7.3), the device being gone and the OS removing it. Section 7.1
  item 4's loss of the PCIe tunnel alone discharges only the PCIe
  obligation, through its own teardown and removal; a USB3 obligation on
  the same port still has to be verified.
- **Failed** is any obligation not met: a rebuild that fails; a
  verification that fails without removal evidence (the port is then
  HELD, untouched); a HELD port's preserved tunnel that did not survive,
  which this driver does not rebuild, its configuration being someone
  else's; the NHI's own resume failing. The failure path is record 21's:
  release the IRP and at once ask the filter for an invalidation, so the
  functions that did not come back are reported missing; nothing is torn
  down without removal evidence. Decision U15 records this window.
- **The NHI's resume bound** under this profile is the sum, over its
  steps, of their implemented costs, each rounded as section 5.3 measures
  it - a maximum wait counted at its bound plus one tick, a minimum wait at
  its minimum plus two ticks (the extra tick it waits and the tick it may
  overshoot), a request attempt at 100 ms plus one tick, times its
  attempts - with the tick period read at suspend; the steps: platform preparation (1.05 s), and per
  obligation the verification reads or the rebuild (enumeration with its
  20 retries, Router Ready, Get Capabilities, Buffer Allocation, bonding,
  the TMU's posting and 50 ms convergence where a USB3 obligation needs
  it, Configuration Ready, the Detect gate, the paths, L0, the USB3
  bandwidth handshake), with each step's repetition count (retries,
  polls, the obligations recorded at suspend); record 21's 10 s is added on
  top as the watchdog's margin.

Record 21 section 6.3's conclusion stands for this profile too: the hold
orders a successful resume and cannot close the window of a failed one,
so **system suspend with a tunnelled device attached is unsupported in this
release on every target**, and the release notes say so. On Windows 98 no
filter sits on a bridge (record 21 section 6.1) and there is no hold at
all.

### 7.7 The host interface lost: containment

Everything above assumes the host interface answers. It can stop
answering without any PnP request reaching this driver: the platform
powers the controller off or resets its PCI subtree, and BAR0 reads all
ones, or reads values this driver never wrote (record 21 section 5.5,
from `usb4-emu`'s `ICM-PROTOCOL` R27 and R32 and the P14s readings; the
same platform behaviour is possible behind any host whose power the
firmware manages). Nothing in sections 5 to 7.6 notices it: the idle
tick's requests time out one by one, each a failed step and never a
failed driver (section 5.1), and every sequence after them - a teardown,
a quarantine retry, an eject, a resume - would go on writing to a
function that is no longer the one this driver programmed.

Record 21 section 5.5's liveness monitor serves both profiles: one
sample each idle period of RX ring 0's size register (`0x0800C`), against
the value read after the ring start (`0x01000010` under this profile:
sixteen entries, 256-byte buffers); one all-ones sample is SUSPECT, three
are ABSENT, a readable value other than that one is CONFIG_LOST. Its
access gate sits at the BAR accessors and refuses the ordinary class
outside ACTIVE (and before the monitor is armed, OFF), and its `Cancelled`
answer ends every wait of the transport. Under the ICM client those
states lead to a recovery once the platform restores access. **Under this
profile there was no recovery in `3.0.0.0`** (decision U17 as first
taken; the recovery is designed in this section's revision below): the first
of SUSPECT, ABSENT or CONFIG_LOST, a surprise removal, or a bind or a
resume that reads the size register all ones or other than the value it
programs, puts the instance in a terminal state, CONTAINED, until a PnP
stop and start or a reboot. A
matching sample later, or a system resume, never reactivates it.
Recovery may be deferred; containment may not, since it is what stops
this driver acting on a fabric it can no longer see.

The monitor is armed by the bind's success (section 4.4 step 6) and by
every resume that leaves the rings running, a held one included, each
only once the size register reads back the value programmed; it never
samples during a resume (record 21 section 5.5's transitions). The gate's
rule covers the NHI's PCI configuration writes as well as BAR0: none is
made to a function revoked, lost or contained.

**Entering CONTAINED**, once, on the thread, with no register access of
any kind - no ring stop, no router request, no PCI configuration write
to the NHI:

1. The core is contained: every request it would send is refused before
   it reaches the transport, and so is every request while the gate is
   closed, so a sequence running when a surprise removal arrives stops at
   its next request or wait (the transport's waits end at the gate's
   cancellation) and leaves its port HELD, its reason "contained", with
   what it had written recorded (section 7.4); nothing of it is undone,
   since undoing is writing. The safe point services no ring, the
   capability settle sends no acknowledgement. The queued actions are
   retired, the deferred teardown tails (section 7.4) abandoned, the
   pending resync dropped, and every port's anchor (section 10.3)
   invalidated, so no statement can be made from it.
2. A batch of actions the driver took and has not delivered (the resume's
   ordering, section 7.6) is discarded: no TUNNEL_UP reaches the
   re-enumeration seam or the `nvme98` channel after the loss.
3. The `nvme98` channel is told that presence was lost with nothing
   proven, through its own loss route (record 21 section 5.5): on NT each
   UP entry of the domain's tunnel-port set is marked lost - cleared only
   where its function reads absent, never the path-closed statement; on
   Windows 98 and ME, where the loss reads no bridge, no entry is
   changed. Section 10.2's statement needs section 7.3 step 1's read-back
   on the host router, and a host router that does not answer proves
   nothing about the path or the endpoint's reset (record 21 section
   7.3). `nvme98`'s own gone deadline (MD-1) stands.
4. One reconciliation is requested of the re-enumeration seam, the
   reason "controller unavailable" (record 21 section 5.5): every member
   of the domain's tunnel-port set as changed, so the operating system
   removes what it can no longer reach - on NT through the bridge
   filters' invalidation (asynchronous), on Windows 98 and ME through
   the CONFIGMG worker record 21 gives that reason, never by a
   synchronous CONFIGMG call from the thread a PnP stop may be waiting
   for - with a removal a driver refused never forced, and with `XhciCmReenum` 0 nothing requested. The seam's
   pending ejects are voided by it (section 7.6). This step is not taken
   while a PnP request waits for the thread - on the stop's path (a
   surprise removal, or a loss found at the stop), after a surprise
   removal revoked the gate, or with a stop or removal waiting for the
   thread behind the work running (a resume, a bind), from before it is
   queued until its work has run: the seam's Windows 98 and ME route calls CONFIGMG
   synchronously, and the removal under way and the next start's
   discovery stand for it; steps 1 to 3 and 5 still run, before the seam
   is closed.
5. Logged once with the monitor's state and episode, and counted
   (`u4.drv.contained`; the core's `u4.contained.refused` and
   `u4.contained.retired`).

**What CONTAINED keeps.** The common buffer and its adapter, unless a
verified stop had already cleared record 21 section 5.5's DMA record
(`DmaMayRun`, set before the first ring-enable write, cleared only by a
stop whose read-back shows both ring 0 controls readable and disabled):
a ring never proven stopped may still be written by the controller, so
the block is kept until the next boot. Force power, which a contained
instance never clears (section 4.2: a host this driver cannot read is not
"nothing connected"). The Bus Master bit, which is not written either.
Every port's records - its state, tunnels, quarantine and resume
obligations - as the last reading left them: they describe what may still
be live on a fabric this driver cannot see, and they are never acted on,
since this instance builds nothing again. **USB4 router state is never
read or written once the instance is contained**; the next instance,
after the PnP stop and start, finds the fabric afresh under section 9.2,
which never resets it.

**What CONTAINED refuses**, each where it starts as well as at the gate:

| Path | Under CONTAINED |
|---|---|
| The idle tick: transport service, port scan and checks, connects, lane bonding, the TMU, Configuration Valid, path setup and teardown, lane restore, bandwidth release, quarantine retries | not run; the thread waits for work only |
| Deferred teardown tails (section 7.4) | abandoned at entry |
| Actions: TUNNEL_UP to the seam, the `nvme98` UP, the learned and router-named correspondence | retired at entry; none taken after |
| Safe eject (section 10.4) | refused before any request; the seam's pending ejects voided |
| System suspend (section 11) | nothing written and no hold published: a contained instance has no tunnel it could restore |
| Start again (the core's start) | refused before anything is changed; only a new instance starts |
| Resume, and the restart of the platform step, Bus Master and rings | refused; a hold already published at the suspend is released through section 7.6's failure path (decision U15's order: release, then invalidate) |
| Stop, surprise removal, removal, shutdown | software teardown only: no fresh port reading for force power (a router request), no ring stop, no Bus Master or force power write; the common buffer as `DmaMayRun` says; BAR0 unmapped; the thread ended |

**A loss in the middle of something**, by section 7.4's waits:

- *Inside a tunnel's setup* (sections 6.4 to 6.9): with the gate closed
  (a surprise removal) the next request is refused and the port is HELD,
  its reason "contained"; with the controller silent and the gate still
  open (a power-off between samples) the requests time out, the port
  ends FAILED with what it had written recorded, and the next idle
  sample contains the instance. No tunnel is claimed up either way.
- *Inside an eject*: the teardown's requests fail, the eject reports
  failure, no closure is claimed (the path not proven closed, so no
  statement), and its TUNNEL_DOWN is retired by the containment, the
  reconciliation of step 4 standing for it.
- *Inside a resume*: the resume's requests fail or are refused and it
  returns through section 7.6's failure path, within the resume bound.
  Every resume under this profile, whatever its outcome - the platform
  step or the Bus Master bit failing included - then reads the size
  register once: all ones, the gate revoked meanwhile, or - once the
  resume itself ran, which programs the rings again over a size the
  suspend's stop kept - any value other than the one programmed, and the
  core is contained at once (nothing it rebuilt is published), the hold
  released through the failure path, then the driver's containment. A
  resume that fails with the host interface still answering is the
  failure after activation of section 3.5, as before.
- *Between the ring start and the bind's arming*, or in a bind that
  fails after activation: the size register read then is the check; all
  ones, another value or the gate revoked contains the instance before
  anything is announced, and a bind that fails with the host interface
  still answering is the failure after activation of section 3.5.
- *At stop*: the stop's own fresh reading of the ports (section 4.2) is
  a router request and is not made, so force power stays; the rings'
  stop is a write and is not made, so the buffer is kept unless
  `DmaMayRun` was already clear.

**Recovery was owed** (section 16), and is designed below. Until it is
built (task 42-A.10) a contained host stays contained until PnP restarts
the driver. Recovering it in place would need record 21
section 5.6's validation (identity, BAR0 still assigned, section 3's
tests re-run with the same outcome), the rings restarted in the kept
buffer, and then section 9.2's discovery over a fabric whose host
interface lost its rings but whose routers may still carry tunnels -
never a reset (section 9.1); that is not built.

**Revision (2026-10-18, roadmap-usb4.md task 42-0.5; planned as
`U4REC.1`): recovery after loss.** The owner's opening of Phase 42 on
2026-10-18 promoted the planned recovery into the release (`3.1.0.0`),
which revises decision U17's "no recovery in this release" and leaves its
containment as it is: CONTAINED stays the state a loss puts the instance
in, with everything the section above refuses; what changes is that it is
no longer terminal. The code is task 42-A.10, the legs 42-V.5 (the
emulator's root hot-plug modes, `acpi` and `restore`, on `usb4-generic`).
Each choice below that is not the owner's is the orchestrator's, for the
owner to confirm (the list at the end). Reviewed and revised.

The shape: a recovery **restores a consistent instance and nothing more**.
It proves access back, repairs the two configuration bits the platform may
have cleared, stops and restarts its own rings in the kept buffer, finds
the fabric again by reading, drains what cannot be preserved, revalidates
the PCI side, and commits to ACTIVE. Every tunnel that has to be rebuilt,
every endpoint that has to be removed and every entry of Stor98's table
that has to change is then handled **after the commit by the ordinary
machinery** - the port state machine, the tunnel-up and tunnel-down
actions, the seam, `CmNvlinkExpose` - under one new rule, the retirement
barrier of part 4, so that a generation change has exactly one owner (the
tunnel-up exposure), a publication never precedes the guarded commit, and
nothing speculative is left behind by a stop or a second loss in the
middle (decision U22).

*0. The snapshot and the journal.* Entering CONTAINED (step 1 above)
takes, before anything is retired, one immutable **recovery snapshot** of
the instance: a complete copy of every host port record (`TBT_U4_PORT`,
every field: the state and reason, the generation, the device router
`TBT_U4_ROUTER` with its route - which is the TopologyID expected of it -
its ids, UUID and validity, USB4 version, `Tbt3`, DROM state and
capabilities state, both tunnels `TBT_U4_TUNNEL`, the lane fields
`LaneSaved`, `LaneWritten`, `Bonded`, `SingleLane`, `Lane1Disabled`,
`PcSetHost` and `PcSetDev`, the TMU fields, `Usb3Failed`, `Usb3GapValid`
and `LastUsb3TeardownUs`, `BwQuarantine`, `BwReleaseOwed`, `HostResidual`,
`HostCloseOwed`, `RemovalOwed`, `RelockSeen`, `ClassifyPending`,
`LostKnown`, `AsFoundOwed`, `PcieFailed`, `Ejected`, `DpPreserved`, the
anchor and the link reading), of the host router record, of every
discovered link below depth 1 (`TBT_U4_LINK`), of the quarantined HopIDs
per adapter, of the writes a sequence running at the loss had recorded
(section 7.4's per-write record), of the deferred teardown tails abandoned
at containment, of the tunnel-port set (each member's location, header
identity, secondary and subordinate bus numbers, source, exclusivity and
named or learned correspondence) and of the channel's shadow (each entry
this domain owns with its bus, slot, generation and its flags before and
after step 3's loss marking). Nothing is enumerated by hand: the
structures are copied whole, and the rules below say which fields a
recovery re-initialises (the transitional and pending flags of a sequence,
`CheckPending`, `UnplugPending`, `Transitional`, the anchor, the link
reading) and which it keeps. The resume obligations of section 7.6
(`TBT_U4_OBLIGATION`) are derived inside the snapshot for every tunnel that
was up and are not its replacement (decision U19). Each later loss
(cooldown, then a new episode) takes a new snapshot of the state as it then
is, and every recovery attempt keeps a **journal** of what it changed - the
Command bits, force power, the rings' stop, each drained entry and cleared
enable - so that an attempt interrupted by a stop, a suspend or a second
loss is read by the next attempt's classification from the journal and
the snapshot together, never from the snapshot alone.

*1. Access proven back: the probe.* Under CONTAINED the thread probes once
a second (`CM_LIVE_SLOW_MS`, the `PROBE` class of record 21 section 5.6's
gate, which admits reads alone): first the NHI's own configuration header
through `IRP_MN_READ_CONFIG` to the PDO with the strict length check
(`CfgStrict`: an answer that reports fewer bytes than asked is no answer;
one that reports no length at all - Windows 98 SE's PCI bus driver
completes the read with the data and `Information` 0, read in the
emulator leg of 2026-10-19 - is made twice, through a buffer of all ones
and then of zeros, and is an answer only when both read the same, since a
byte the bus driver did not write differs between them) - the identity
dword, BAR0 (both dwords of a 64-bit BAR) and the Command register - and
only then RX ring 0's size register through the BAR. The header is read
whatever the BAR sample says, because a function whose memory decoding the
platform cleared answers configuration cycles and nothing through its BAR,
and that is the case the Command repair below exists for. Access is proven
back when the header reads the identity dword as the healthy start read it
(`LiveIds`) and BAR0 as read then, attribute bits masked, the high dword
too (`LiveBar0`, `LiveBar0Hi`). A header that reads all ones, or an
identity that differs, keeps the instance contained with the probe going
on; a BAR0 that reads 0, all ones or another address refuses the recovery
for this episode (logged `u4.recover.bar-changed`): a base address is the
bus driver's to assign and only a PnP stop and start maps it (record 21
section 5.6's rule for the ICM profile, kept). The probe does not touch
the fabric and makes no write.

*2. Preparation.* With the header proven, the thread enters PREPARING, a
new state of the monitor whose access class, `PREPARE`, the gate admits
for configuration-space reads and for configuration-space **writes to the
Command register and to the vendor capability section 4.2 names**, and
for nothing else (no BAR write, no ordinary transport; `cmConfig` refuses
every other write as it refuses them under `PROBE`). In order:

1. **The Command register**, where it reads with Memory Space Enable (bit
   1) or Bus Master Enable (bit 2) clear while BAR0 is intact: one 16-bit
   read-modify-write setting the two bits (the word `cm_thr.c` already
   writes for Bus Master, `CmNhiSetBusMaster`'s route widened to bit 1 for
   this stage alone), read back; a read-back without both bits refuses
   the recovery for this episode. The journal notes the bits this stage
   set.
2. **The platform step** of section 4.2, on the integrated family, exactly
   as the start and the resume make it and before any ring is touched,
   since section 3.2's readiness depends on it: VS_CAP_22 read, FORCE_POWER
   set where clear (journalled, as the power-change record requires: a bit
   this stage set is this instance's; one found set is never cleared),
   FW_READY polled (up to 1.05 s), LTR copied into VS_CAP_15. FW_READY
   never set refuses the recovery for this episode (the instance is not
   made inert: it is contained, and stays so).
3. **The selection's tests again**, on reads: (a) the candidate, the
   identity dword already compared; (b) readiness, from step 2's FW_READY
   where it applies; (c) ownership, section 3.3's reads of the domain's
   ownership indication (`ICM_EN` where it decides) through the now
   decoding BAR under the `PROBE` class; (d)'s first half, the Host
   Interface Version from the capabilities register, unchanged. The
   selection's pure half (`TbtHwSelect`) is re-run on those readings with
   the same inputs the start gave it and must give the same outcome,
   `TBT_SEL_SOFTWARE`; anything else refuses the recovery for this episode
   (logged with the test that changed). Platform preparation is what
   makes (b) answerable, which is why this step follows step 2 and not the
   probe (U18 restricts the probe to reads; the preparation's writes are
   the two named here).
4. **The size register**, read once more through the BAR: the value this
   instance programmed (`0x01000010`) means the controller kept its ring
   configuration (a SUSPECT or ABSENT episode the platform ended without
   a reset); any other readable value - the reset default or another -
   means the rings are not this instance's any more. Neither is refused:
   the first is logged as kept, the second as lost, and both go through
   step 3.1's verified stop, which is what proves the controller writes
   nothing into the buffer whatever the register read. `cm_live.h`'s
   CONFIG_LOST is any readable mismatch against the baseline, and all of
   it is admitted here; a readable value that is neither is not taken as
   proof of another owner, since the verified stop, not an inference, is
   the safety argument (decision U18).

*3. The recovery*, in RECOVERING under the `RECOVERY` class (the gate's
rule of record 21 section 5.6), on the thread, with the episode recorded at
its start, bounded as the resume is (the sum of its steps' implemented
costs in `TbtU4ResumeBoundUs`'s shape, read at its start), refused where a
PnP stop or removal is waiting for the thread (the rule of entering
CONTAINED's step 4) and ended through the failure path by anything part 5
lists. The core's own gate is opened for it and for nothing else: a
`TbtU4Recover` entry clears `Contained` for the recovery's work alone
(every request of the core refused by `tbtU4Off` is admitted only on the
recovery's path, through a flag the entry sets and the commit or the
failure path clears), the failure path sets `Contained` again and the
driver's `U4ContainDone` latch is cleared only by the commit, so that a
later loss contains the instance once more and takes a new snapshot
(decision U26). Nothing is built and nothing is published in this part.

1. **The rings, in the kept buffer.** The common buffer kept under
   `DmaMayRun` (record 21 section 5.6) is the one the controller may still
   be writing; nothing is placed in it before a **verified stop**: each of
   TX ring 0 and RX ring 0 has its Ring Control register written to its
   default, its Base Address Low and High, Ring Size and index registers
   written 0, read back readable and disabled, then the 1 ms settle
   (section 4.4 step 2's takeover, of this instance's own rings this
   time), the read-back after the settle as the stop's is - the same
   read-back that clears `DmaMayRun` at a stop. A read-back that fails
   ends the recovery and is **final for this boot** (CONTAINED, final):
   the buffer stays kept with its adapter object, no second buffer is
   allocated for any episode, since a controller that cannot be proven
   stopped cannot be given new rings either (decision U20). With the stop
   verified, section 4.4 steps 1 to 3 run again in the same buffer, with
   the same adapter: interrupts masked, ring 0 programmed, the size
   register read back as programmed; the monitor is not armed here
   (`CmLiveArm` enters ACTIVE, which would open ordinary access before the
   recovery has ended), and the commit of step 7 takes its own fresh
   sample.
2. **Test (d)'s second half**: ROUTER_CS_0 to 4 at route 0 answered within
   the request bounds, the USB4 Version's major 1 or 2 (section 3.5). An
   inert outcome ends the recovery for this episode.
3. **The host router**, by section 6.1's rule as `tbtU4HostRouter` codes
   it: ROUTER_CS_3's **TopologyID Valid** (bit 31) clear means the host
   router lost its enumeration and is enumerated afresh (ROUTER_CS_1 to 4
   written as at start); set, with the enumeration fields consistent with
   a host router (`tbtU4Enumerated`: Valid, depth 0 and the root route),
   means it is adopted as it is, its DW1 to DW3 preserved; set and
   inconsistent refuses the recovery for this episode (logged
   `u4.host.topology`), as the start refuses such a host router.
   Configuration Valid (ROUTER_CS_5) is the separate handshake of section
   6.6 and decides nothing here. Then the adapters are discovered, the
   capabilities read, the PCIe Down mapping asked (section 6.3) and Buffer
   Allocation made, as at start; the host router's path entries are read,
   not written: a host router found enumerated keeps whatever entries it
   holds for step 4 to classify, and one enumerated afresh may hold them
   too, since writing the TopologyID clears no path entry - so the host
   entries at every recorded HopID are read in both cases.
4. **The fabric found again**, reading only, never a reset (no Downstream
   Port Reset, no Host Router Reset, nothing of section 9.3), port by
   port against the snapshot and the journal, **per tunnel**: each PCIe
   and each USB3 tunnel that was up is an obligation of its own, and a
   port's outcome is the pair of them (a USB3 tunnel alone on a port
   `Ejected` or FAILED for PCIe, whose PCIe side stays as the snapshot
   says: no PCIe is built for an eject or a PCIe failure until a re-plug;
   a USB3 tunnel preserved beside a PCIe one rebuilt; both preserved).
   For a Thunderbolt 3 device router (`Tbt3`, section 8) every test and
   step below takes section 8's form: its enumeration state in place of
   TopologyID Valid and Configuration Ready, no router operation (no Get
   Capabilities, no Buffer Allocation), the Detect gate at the USB4 end
   alone and obligation 1 resting on section 10.1's link-down argument
   for it.

   - **A port with no tunnel at the loss** (EMPTY, UNSUPPORTED,
     CONNECTING, or FAILED with no live tunnel and no eject): discovered
     as at start by section 9.2, the quarantine of the snapshot kept.
   - **A tunnel verified** by section 9.2's reads - the device router
     answers at the recorded route with TopologyID Valid set, the same
     UUID (a UUID not read at the loss is compared to nothing and the
     tunnel is HELD), the same lanes (bonded, or single as recorded),
     Configuration Ready set (a USB4 router), and the tunnel's path
     entries Valid at the recorded HopIDs with Path Enable set at both
     ends - is **preserved** in the fabric: the port's record kept as the
     snapshot has it. Whether it stays preserved for the channel and the
     operating system is part 4's.
   - **A partial loss** - the device router answers at the recorded route
     with the same UUID and its Up adapter for that tunnel still carries
     Path Enable and the recorded entries, while the host side of that
     tunnel is gone (the host router re-enumerated in step 3, or the host
     entries at the recorded HopIDs reading Valid 0 or the host adapter's
     Path Enable clear) - is **to be rebuilt**: its drain is step 5's,
     its rebuild part 4's, under these prerequisites, else HELD: (i) the
     router's identity and ownership as above, its enumeration adopted as
     it is (section 6.4 step 4 skipped; Get Capabilities re-read on a
     USB4 router); (ii) the lanes kept as found - a link bonded by this
     driver and still bonded is adopted bonded, a single link single; no
     bonding is attempted and no Lane Disable written, since the Guide
     forbids starting lane bonding with a path enabled in the port and a
     path is enabled on the device side; (iii) the TMU read and left as
     found, a USB3 rebuild admitted only where it still reads configured
     as section 6.6 requires (`TmuOk`), else that USB3 obligation ends
     `Usb3Failed` with its quarantine while the PCIe one goes on.
   - **Removal evidence** (CLd on lane 0, Lock set again, another UUID or
     TopologyID, ERR_CONN at the route, no router): the port's tunnels are
     **departed**: their host side alone is drained in step 5 (a router
     that answers at the route with another identity is a replacement,
     whose adapters and entries are not the departed router's recorded
     resources and are never written here; it is discovered on its own
     after the commit, as section 9.2 discovers a router found at start,
     with its state as found), and the port is EMPTY, its device gone.
   - **A tunnel preserved in the fabric whose member's secondary bus
     changed** (step 6 finds it): the endpoint's devnode is not the one
     the operating system had, and its entry is R8a's case; such a tunnel
     is classified **to be rebuilt**, drained in step 5 and rebuilt after
     the commit behind the barrier, so that the new entry is allocated by
     a true establishment and the old one retired after its devnode (part
     4); nothing is invented for it.
   - **Anything else** (a half-built path, HopIDs matching no record, a
     device router that does not answer while host entries are enabled, a
     lane state this driver did not leave, a prerequisite above failing):
     HELD, as section 9.2 says, untouched, served by its unplug alone.
   - **A sequence interrupted at the loss** (the snapshot's recorded
     writes) or an earlier attempt's (the journal): its entries and
     enable bits are undone from the record where the adapter still
     answers and the entry still reads as written, and quarantined where
     not, before the port is classified; the deferred teardown tails are
     owed as section 7.3's quarantine rule owes them (retried at the idle
     tick once ACTIVE), never resumed as sequences.
   - The discovered links below depth 1 are re-read as section 9.2 reads
     them, and the residual-host-state rule of 9.2 applies to every host
     adapter the walk covers.
5. **The drains**, for every tunnel to be rebuilt or departed and for
   nothing preserved, each protocol by its own contract, and the device
   side only where the router is positively the recorded one (the same
   UUID and TopologyID at the recorded route, its adapters as recorded):
   a departed tunnel, or one whose router is a replacement, is drained on
   the host side alone.
   - **A PCIe tunnel**: the complete recorded teardown - the orderly form
     of section 9.3 step 1 without its reset steps, which is section 7.3
     step 1's discipline applied to both ends: Path Enable cleared on the
     host's PCIe Down adapter and on the device's PCIe Up adapter, each of
     the tunnel's recorded entries (host Down HopID 8, host lane 0 `hu`,
     device lane 0 `hd`, device Up HopID 8) invalidated with Pending
     Packets polled to 0 and tTeardown, every other entry on those
     adapters left as found, and the read-back of section 7.3 step 1
     sub-step 3 (Path Enable 0, Valid 0, Pending Packets 0, the host
     adapter's LTSSM in Detect) - a host entry that reads Valid 0 already
     is still drained (Pending Packets read) before it counts as closed,
     since a cleared Valid alone proves nothing about what the adapter
     holds.
   - **A USB3 tunnel**: section 7.3 step 4's contract, not the PCIe one -
     Path Enable cleared with the adapter's Valid kept set on the host's
     USB3 Down adapter (and the device's USB3 Up where the router is the
     recorded one), its recorded entries invalidated and drained the same
     way, the bandwidth released to the consumed value where HCA answers
     (never below section 7.3's floor, `BwReleaseOwed` where it does
     not), and the time of the teardown recorded for section 6.9's reuse
     gap (`LastUsb3TeardownUs`); no LTSSM reading, which is PCIe's.
   What does not drain is quarantined as section 7.3 says, and a tunnel
   whose drain did not complete is HELD, not rebuilt. The lane adapters
   and the TMU are left as found. No path is built here (decision U21:
   this drain of entries this instance itself recorded is unplug handling
   under the no-reset rule; no register of any router is reset and no
   lane is retrained by this driver). **The removal debt** is recorded
   here too: for every drained PCIe tunnel whose endpoint devnode the
   operating system still has (the containment's reconciliation found
   the function present, or ran on 98 SE and ME where the worker's
   removal is still owed or was abandoned), the member owes a
   **removal-only** request after the commit (part 4), since no
   TUNNEL_DOWN action arises from a classification - the port's
   `RemovalOwed` is set and the member's barrier raised.
6. **The PCI side revalidated** (decision U24): every member of the
   snapshot's tunnel-port set re-read through `HalGetBusData` - the same
   identity dword and header type, the same secondary and subordinate bus
   numbers, the same source still naming it (the router-named source
   re-read through step 3's mapping operation; the switch, listed and
   SHPC sources as record 21 section 6.3 reads them) - and the
   exclusive-ownership check of record 21 section 6.3 run again on the
   registration list. A member whose secondary bus changed sends every
   tunnel preserved behind it back to step 4's rebuild classification
   (R8a: its old entries' `IN_USE` is cleared only after their devnodes
   are retired, part 4, and the rebuild's exposure takes a free entry);
   a member that cannot
   be read, that is no longer a bridge, or whose exclusivity is lost,
   refuses the recovery for this episode (the set is found afresh by the
   next instance, not patched here). Learned correspondences are kept only
   for a member whose secondary bus is unchanged and whose port's tunnel
   was preserved; every other learned binding is withdrawn, the named ones
   re-established by the mapping operation.
7. **The commit.** One fresh sample of the size register, read now; then
   the **locked part**, under the work lock and nothing else there (the
   lock raises IRQL, as `cmLiveRecover` releases it right after
   `CmLiveRecovered`): `CmLiveRecovered`'s rule from CONTAINED's side -
   the episode the recovery recorded is still the current one, nothing
   revoked it, the sample is the programmed value - and, in this order,
   the flag updates alone: the monitor ACTIVE with that sample as its
   baseline; the core's `Contained` cleared, its `Suspended` latch cleared
   (a suspend taken while contained sets it with no resume to clear it,
   and `TbtU4Poll` and `CmNhiRunning` refuse a suspended instance; the
   commit runs in D0 only, the power path's rule), its stage RUNNING; the
   driver's `U4ContainDone` cleared and its stage `CM_STAGE_ACTIVE`; where
   the loss had interrupted an activation or a resume (part 5's
   eligibility), the loss indication and `U4.Failed` cleared - only now,
   after the reconstruction succeeded. Then, **with the lock released**
   and at PASSIVE_LEVEL, the work that reaches outside: the bind's
   remaining steps where the loss interrupted a bind, as the bind would
   have run them (the seam's start, `CmReenumStart`, the hooks, the first
   publication to the query device; the host-router discovery a loss
   before it had completed being steps 2 and 3 of this part), and part
   4's requests - each under the **admission-and-drain protocol** the
   driver already uses for an ICM `UP`'s delivery (`CmNhiDrainActions`:
   admitted under the work lock with the episode and the revocation
   checked there and an in-flight count raised, `Delivering`; run at
   PASSIVE_LEVEL outside the lock; the count lowered after), so that a
   stop or surprise removal that lands between the check and the
   operation finds the admission closed and waits for the admitted work
   to drain (`CmNhiRevoke` on `DeliverIdle`) instead of racing it; the
   USB4 delivery path and `CmReenumStart` are brought under the same
   protocol for this work, since neither has it today. A request not
   admitted is dropped, as `CmLiveRecovered`'s rule drops a late result.
   The port records are as parts 3 and
   4 leave them. A commit refused (the episode moved on, a revocation, a
   sample not the programmed value) is a failed recovery, and none of
   these updates is made.

*4. After the commit: the barrier, the rebuilds and the publications.* From
the commit on, the instance is an ordinary ACTIVE one with some tunnels
in drained states, and the ordinary machinery does the rest, under the
**retirement barrier** (decisions U22 and U23), a per-member flag the
recovery sets for every member behind which an endpoint of a departed or
to-be-rebuilt PCIe tunnel sat whose devnode the operating system still
has, and that is cleared only by the retirement of that endpoint's
devnode: on Windows 98 SE and ME the worker's per-devnode result for its
removal (`cmWkGone`'s outcome for that instance id, or the vanished-child
removal's: removed, or already absent from the tree), carried back
through a per-devnode result slot the worker gains for this, which is
interface 3's R8c (`Remove_SubTree` succeeded before the bridge is
re-enumerated or a new `UP` published); on Windows 2000 and later, where
interface 2 states no retirement rule and every ordinary re-plug already
publishes its new `UP` and invalidates once `pci.sys` stops reporting the
old PDO, the child filter's own observation that the endpoint's
`IRP_MN_REMOVE_DEVICE` completed below it (`CmRegChildRemoved`, which the
child posts after the removal finished and it detached, tagged with the
episode) where a child of this driver was on the PDO, and otherwise a
**fresh and complete** `BusRelations` answer after the removal-only
request (`CmRegRelations`: not truncated, taken after the request,
bounded at 5 s and retried once) that no longer reports the PDO - the
condition every ordinary NT re-plug of this driver rests on, and no
stronger one is claimed for it. A physical unplug clears no barrier: the
devnode's retirement does, which the unplug brings about through the
worker's vanished-child removal or `pci.sys`'s report. While a member's
barrier stands, **no tunnel is built on its port, no `UP` is published
for it and no re-enumeration of it is requested but the removal-only
one** - the block record 21 section 7.2's 9x half imposes on a member
whose removal keeps failing (R8c gates the bridge's re-enumeration as
well as the new `UP`); a removal vetoed or abandoned (`CM_WK_GONE_TRIES`)
keeps the barrier and is retried without forcing, the ports behind it
drained and down, logged `u4.recover.retirement-owed`, until the devnode
is retired.

**The removal-only request** is what retires an endpoint whose devnode
the operating system still has after its tunnel was drained: dispatched
right after the commit for every member with removal debt (step 5), it
is allowed through the barrier and is the one request a barred member
gets - on NT the invalidation of that member with publication mode
*retire* (`pci.sys` reads the drained function absent and reports the
PDO missing, the child filter observes the removal), on 98 SE and ME the
worker's vanished-child removal for the member (its scan reads the
function absent, `Query_Remove_SubTree` and `Remove_SubTree` follow, a
veto respected and retried) - and it is retried until the devnode is
gone, as the worker already retries. **Mode *retire*** is a distinct,
non-exposing operation of the channel (a new pure function beside
`CmNvlinkRetireEntry`, which refuses an entry still `UP` because it
retires acknowledged statements): for each entry behind the member whose
function reads absent on the bus and whose devnode's retirement was
observed, `UP` is cleared to `IN_USE` alone with the generation
unchanged - no `DOWN_QUIESCED` (no quiescence is claimed), no increment
(no establishment happened), nothing scanned or published. Then:

- **A preserved tunnel** (its member's bus unchanged) whose entry still
  reads `UP` and whose endpoint devnode is retained (the containment's
  reconciliation found the function present, since the tunnel stayed
  up): nothing is published and no generation changes; a Stor98 instance
  still bound keeps its identity (R8d). On Windows 98 SE and ME, where
  containment changed no entry, a preserved tunnel keeps its entry `UP`
  at its generation whatever the worker did with the devnode. Its member
  takes publication mode *none*.
- **A preserved tunnel whose entry lost `UP` at containment** (its
  function read absent for the moment PCI access was gone) is **not
  preserved for the channel**: re-publishing `UP` at the same generation is
  nothing the interface defines, and a new generation would say
  "re-established" of a link nobody saw go down (R8b). Such a tunnel is
  reclassified by step 4 as to be rebuilt and drained by step 5 at both
  ends, so that its rebuild's new generation is true; its member takes
  the removal debt and the barrier (decision U22).
- **A rebuilt PCIe tunnel**: once its member's barrier is clear, the
  tunnel is rebuilt by a **per-tunnel rebuild transition** of the core
  (decision U28, new for 42-A.10: the ordinary connect of section 6.4
  cannot serve, since it restores the lanes, requires them idle and
  decides both tunnels, which would touch a preserved sibling): from the
  port's UP, HELD or FAILED state (FAILED is where an eject of the PCIe
  tunnel leaves a port whose USB3 tunnel stays up, `TbtU4EjectPcie`, and
  where a PCIe failure leaves it; such a port is a source for its USB3
  obligation alone, and stays FAILED for PCIe with `Ejected` and
  `PcieFailed` as they were when the USB3 rebuild ends), on the adopted
  router with the lanes and the
  TMU as found, the sibling tunnel untouched, `Ejected` and `PcieFailed`
  respected (a port so flagged gets no PCIe rebuild), the Detect gate of
  section 6.8 step 1 at both ends (the USB4 end alone for a Thunderbolt 3
  router), then section 6.8's paths with Buffer Allocation as section 6.4
  step 9 for an adopted router; a failure is section 6.8's (the PCIe side
  FAILED, `PcieFailed`, the quarantine, the sibling kept). Its TUNNEL_UP
  action reaches the seam as any tunnel up's: `CmNvlinkExpose` with a
  fresh exposure publishes the one new `UP`, generation + 1, before the
  invalidation or `Reenumerate_DevNode`. That exposure is the only owner
  of the generation change (U22). Its member takes mode *fresh*.
- **A rebuilt USB3 tunnel**: the same transition for USB3 alone - section
  6.9 on the adopted router, lanes and TMU as found (admitted only where
  `TmuOk` held in step 4), the reuse gap of section 6.9 step 1 from the
  recorded teardown time, the bandwidth handshake, the sibling untouched
  - with **no TUNNEL_UP action, no PCI-side change and no table
  generation**: a USB3 tunnel carries devices of the host's own xHCI
  tree, enumerated there, and the channel is PCIe's. A failure is section
  6.9's (`Usb3Failed`, the quarantine). A USB3 tunnel on a port whose
  PCIe side is `Ejected` or `PcieFailed` is rebuilt this way with the
  PCIe side left as it was.
- **A departed tunnel**: no action of the fabric remains (step 5 drained
  it; the port is EMPTY and connects afresh when a router appears); its
  removal debt goes through the removal-only request and mode *retire*
  above; no statement is ever made for it (the path-closed proof needs
  the host router's read-back at the time of the loss, which did not
  exist, section 10.2; Stor98's gone deadline, MD-1, stands). On NT an
  entry whose function read absent at containment already reads
  `IN_USE` alone from `CmNvlinkLost`; one whose function still answered
  then, and every 9x entry (untouched at containment), is retired by
  mode *retire* once the devnode is gone, and left `UP` while a veto
  retains the devnode.
- **Unknown correspondence** (section 7.6: a port whose member is not
  known, a binding withdrawn by step 6). Classification first: the
  channel's cases above name a member's entry, and an unattributed port
  cannot be matched to one; so, where any PCIe tunnel of the
  unattributed group would be preserved while some member of the set has
  an entry that lost `UP` at containment, or a member's secondary bus
  changed, **the whole unattributed group is classified to be rebuilt**:
  every PCIe obligation of it drained in step 5 and retired through the
  removal-only request, none preserved, since a tunnel kept with a
  broken or misattributed entry would leave its drive without a channel
  (decision U29: hold nothing on a guess). Then the publications: the
  recovery's own RESYNC takes mode *none* for the whole set; removal-only
  requests go to every member with debt in mode *retire* (which exposes
  nothing); and the rebuilds are **serialised**, one port at a time, under
  a learning handoff this record states because section 7.6's learning
  (`XhciTbtLearn`: exactly one pending port, one member gaining a
  function) was written for ordinary plugs: before each rebuild, every
  member's barrier is clear; the drained obligations' pending-learning
  marks are consumed (step 5's read-back is the proof of closure section
  7.6 asks for a pending port, and the reconciliation after the commit
  does not re-mark a proven-drained obligation pending); a fresh
  occupancy baseline of every member's secondary bus is taken (the
  functions answering now); and no other unattributed port is pending -
  a preserved unattributed port, or one whose closure is unproven, holds
  every rebuild of the group (drained and down, logged
  `u4.recover.learning-held`) until it is attributed or proven down, and
  a rebuild held so until a re-plug is handled by that re-plug as a
  fresh plug. Each rebuild is then an ordinary tunnel up with unknown
  correspondence whose publication follows task 42-A.6's rule for that
  case as section 10 states it - no tunnel named while the member is not
  known, so the function newly found on the member's bus (its entry not
  `UP` after the drain) gets the one new generation and no entry still
  `UP` is advanced, nothing published again once the gain attributes the
  port, and record 21 section 7.2's late route for a function that
  answers only after the 9x enumeration - which that task owns and this
  record does not redefine; a recovery that wants its rebuild's
  publication held until attribution, before the operating system's
  enumeration, is 42-A.10's to design and build. The two-member leg's expected outcome: the two
  rebuilds one after the other, each attributed by its gain, each
  publishing one new `UP` on its own member, no other member's generation
  changed; the ambiguous-group leg's: both tunnels drained and rebuilt,
  neither preserved.

The per-member **publication mode** is what `cmReenumAll`'s `fresh`
argument becomes (decision U25): *fresh* for a member whose port's tunnel
came up, *retire* for one whose device departed, *none* for every other
member of a change, so that an unaffected member's entry is never
advanced; the containment's own RESYNC (reason UNAVAILABLE, step 4 above)
takes mode *none* for every member - until task 42-A.6 it exposed with
`fresh` set on NT, which advanced the generation of an entry still `UP`
behind a tunnel nobody saw go down (since 42-A.6 a RESYNC names no tunnel
and advances no entry `UP` for the tunnel last named on its bridge,
section 10); `CmNvlinkLost` is the whole of what containment says
to the channel, and that RESYNC requests the operating system's
reconciliation alone. The recovery's own RESYNC after the commit (reason
"controller recovered", `TBT_ICM_WHY_RECOVERED`) carries mode *none* for
every member: it re-enumerates the preserved members' buses (which report
nothing new) and skips a member whose barrier stands. On Windows 98 SE and
ME it is posted to the worker per member behind that member's
`CONTROLLER_UNAVAILABLE` removal where one is still owed (`ReqGone`,
`NoScan`: the worker's own ordering), and the barrier holds the member
whose removal the worker abandoned.

*5. The states and what ends a recovery.* The monitor's states gain
CONTAINED's exit and one state; the pure half (`cm_live.c`) carries the
table and the host suite reads every row:

| From | On | To |
|---|---|---|
| CONTAINED (probing) | a probe reading the header proven and BAR0 intact | PREPARING |
| CONTAINED (probing) | a probe reading all ones, or an identity mismatch | CONTAINED (probing), the probe going on |
| CONTAINED (probing) | BAR0 changed | CONTAINED (cooldown) |
| PREPARING | every step of part 2 passed | RECOVERING |
| PREPARING | a step refused (Command read-back, FW_READY, a selection test, a BAR read all ones) | CONTAINED (cooldown); `Rearm` set where the refusal was an all-ones read |
| RECOVERING | step 3.1's verified stop failed | CONTAINED (final) |
| RECOVERING | any other step refused; the bound spent; the commit refused | CONTAINED (cooldown); `Rearm` set where a read came back all ones (the second outage was observed) |
| RECOVERING | step 7's commit for the recorded episode, nothing revoked | ACTIVE |
| PREPARING or RECOVERING | a system suspend (D3) | CONTAINED (cooldown), the attempt journalled; at the resume to D0 one probe at once, which takes the probing row where the header is proven (no second outage is required of a power-cancelled attempt) |
| CONTAINED (cooldown) | an all-ones probe then a readable one, or `Rearm` already set and a readable probe (a new episode) | CONTAINED (probing), the episode bumped, a new snapshot of what was changed meanwhile |
| CONTAINED (final) | anything but a revocation | CONTAINED (final) |
| any of these | `IRP_MN_SURPRISE_REMOVAL`, stop, remove (`CmLiveRevoke`) | REVOKED, terminal |

Cooldown exists so that a platform that keeps answering with a broken
configuration is not re-tried every second: a new episode is the one
trigger, as RECOVERY_FAILED's `Rearm` is for the ICM profile, and an outage
the attempt itself observed counts as that episode's all-ones probe, so a
controller that comes back before the next probe is not stranded. A PnP
stop or removal queued while a recovery runs ends it through the failure
path and takes REVOKED; a result for an episode other than the current
one, or after REVOKED, is dropped (`CmLiveRecovered`'s late-completion
rule). An all-ones reading inside RECOVERING is a refused request (the
gate's cancellation) and ends the recovery as a failure with `Rearm` set.
A resume from a suspend taken in CONTAINED is unchanged (the failure path
of section 7.6, the hold released then invalidated) and the probe follows
it at once. Eligibility (decision U27): the stage ACTIVE, and the stage a
bind or a resume left when **the host interface was lost during it** -
`cmU4Bind` finds the size register all ones or other than programmed, or
the gate revoked, and contains the instance; `CmNhiReinit`'s resume
(`cm_power.c`) fails and its size-register check finds the same, and
contains it; those are 42-A.10's "a bind or resume that reads the ring's
size register wrong", and each is told apart from a protocol failure
(`U4.Failed` with the host interface answering), which stays
`CM_STAGE_FAILED`, never sampled and never recovered. For the
interrupted bind or resume the stage is recorded as loss-interrupted (a
new stage value beside `CM_STAGE_FAILED`, which `CmThrLiveness` samples
as it samples ACTIVE), the thread samples, and the commit clears
`U4.Failed` and the indication and runs the bind's remaining steps where
it was a bind (step 7); a resume interrupted by the loss had its hold
released through section 7.6's failure path already, and the recovery
owes it nothing more.
REVOKED stays terminal, and nothing of this part runs under it.

*6. What CONTAINED keeps*, unchanged but for the exit: the buffer under
`DmaMayRun` until the verified stop of step 3.1; force power; the Bus
Master bit as found; the port records, now inside the snapshot; USB4
router state never read or written until the recovery's steps 3 and 4
read it under the `RECOVERY` class. The table of refusals stands for every
path but the recovery's own, and the stop, surprise removal, removal and
shutdown rows are unchanged (a recovery in progress is ended by them
through the failure path, the buffer as `DmaMayRun` then says).

*7. The seam, the brackets and the query device.* The loss bracket of
`CmReenumLossBegin` and `CmReenumUnavailable` (`LossDrain`) is the short
window in which actions handed over are voided, closed by
`CmReenumUnavailable` itself on both routes; what the barrier waits for on
Windows 98 SE and ME is the worker's longer obligation (`ReqGone` and
`NoScan`) resolved per devnode, not that bracket. On NT the containment's
invalidations are asynchronous and prove nothing by themselves; the
barrier's observations are the proof. The query device's answer carries
the liveness state throughout (CONTAINED, PREPARING, RECOVERING, ACTIVE),
so XHCITR shows the branch "not current" until the commit, and the
counters count each refusal by step (`u4.recover.refused` by step,
`u4.recover.final`, `u4.recover.ok`, `u4.recover.retirement-owed`,
`u4.recover.rearm`).

*Legs* (42-V.5, EMU.2, on `usb4-generic` under the current `usb4-emu`
build's root hot-plug modes once part 2 lands): the R32 power-off read
again with `acpi` on 2000 with ACPI, XP and 7 (a PnP removal: the
containment ends with the instance, no recovery) and with `restore` on 98
SE, 2000 and one later NT target: the bridge restored with its
configuration intact (the probe passes, the rings stopped and restarted
in the kept buffer, a tunnel up at the loss preserved with its generation
unchanged and nothing published - on NT its drive kept; on 98 SE and ME
the devnode as the containment worker's removal left it, removed and
found again by the re-enumeration where that removal ran, the tunnel and
the entry unchanged either way); the bridge restored with the host router reset (the
drain, the barrier, the rebuild through the Detect gate, the drive back
after the re-enumeration with a new generation); a device unplugged
during the loss (departed, its entry retired, no statement); a `restore`
that leaves memory decoding off (the Command repair) and one that brings
BAR0 back unassigned (CONTAINED, cooldown, the reason logged); a loss that
cleared an entry's `UP` while the tunnel stayed up (drained and rebuilt,
not preserved); a USB3-only obligation on an ejected port, a PCIe tunnel rebuilt
beside a preserved USB3 tunnel on the **same port** and the reverse, and
one port preserved beside another rebuilt; a router replaced during the
loss (host-side drain alone, the replacement discovered afresh); a
removal vetoed, the device then unplugged and re-plugged (the barrier
cleared by the devnode's retirement, never by the unplug); a loss before
the host-router discovery had completed; a loss during a resume, and the
recovery after it with polling and tunnel service verified, not the
monitor's state alone; a stop racing the commit's post-lock work (the
qemu flavour pausing after the admission check, before the publication); the ambiguous unattributed group (two tunnels
verified, one member's entry lost); a Thunderbolt 3 device router (the TS3
Plus model) preserved and rebuilt; a suspend during the recovery and the
probe at the resume; the recovery refused with a stop waiting; a second
loss during the recovery and the recovery after it without a further
outage; a vetoed 9x removal holding the barrier until the veto lifts,
with the hardware ready to rebuild meanwhile; two members with no usable
mapping; a loss during the bind; the counters and the query device's
states read in XHCITR throughout. On real USB4 hardware the loss itself is
unread (section 16), so the recovery is emulator-only in `3.1.0.0` and the
release notes say so.

*Decisions* (the orchestrator's, owner to confirm; U17 as revised by the
owner's Phase 42 opening): U18 the probe reads the configuration header
before the BAR and writes nothing; the preparation's writes are the
Command register's decoding and Bus Master bits and section 4.2's
platform registers, under a `PREPARE` class that admits nothing else; a
BAR0 not as before refuses the recovery; every readable size-register
value is admitted and the verified stop is the safety argument, never an
ownership inference. U19 an immutable recovery snapshot at each
containment, a complete copy of the port, router, link, set and channel
records with the writes in flight, the resume obligations derived inside
it, and a journal of each attempt's changes. U20 the kept buffer and its
adapter are reused only after a verified stop of the rings in it, and a
stop that cannot be verified ends recovery for the boot. U21 the fabric
is classified per tunnel by reading, with section 8's forms for a
Thunderbolt 3 router; a tunnel to be rebuilt or departed is drained by
the complete recorded teardown at both ends without any reset, with the
closure read back; HELD for anything short of the prerequisites. U22 the
recovery builds and publishes nothing before its guarded commit; every
rebuild happens afterwards through the per-tunnel rebuild transition, a
PCIe rebuild's tunnel-up exposure the one owner of the generation
change and a USB3 rebuild publishing nothing; a preserved tunnel whose
entry lost `UP`, or whose member's secondary bus changed, is drained and
rebuilt rather than re-published; mode *retire* clears `UP` with no
generation change and no quiescence claim; the containment's own RESYNC
exposes nothing. U23 the per-member retirement barrier, cleared only by
the endpoint devnode's retirement - the worker's per-devnode result on
98 SE and ME (R8c), the child filter's removal observation or a fresh
complete relations answer on NT (the ordinary re-plug's condition,
interface 2 stating no retirement rule) - holding builds, publications
and every re-enumeration but the removal-only request of that member; a
veto blocks, never forces; an unplug clears nothing by itself. U24 the
tunnel-port set revalidated before the commit; a changed member refuses
the recovery for the episode; a changed secondary bus is R8a's case. U25
`cmReenumAll`'s `fresh` becomes a per-member publication mode (fresh,
retire, none), the recovery's RESYNC "controller recovered" taking none
for every member and unknown correspondence taking none for the set with
42-A.6's rule for the gain. U26 a `TbtU4Recover` entry opens the core's
gate for the recovery's work alone, the failure path closes it again, the
commit clears the containment latch so a later loss contains afresh. U27
the eligible stages: ACTIVE, and a bind or a resume the loss interrupted
(told apart from a protocol failure), whose stage and latch updates - the
core's `Suspended` latch included - the commit makes in the order step 7
gives, the bind's remaining steps and part 4's requests after the lock
under the admission-and-drain protocol; CONTAINED's
substates probing, cooldown and final and the PREPARING state, with the
table above. U28 a per-tunnel rebuild transition of the core for
42-A.10, PCIe or USB3 alone on an adopted router with the lanes and TMU
as found and the sibling tunnel untouched, in place of the whole-port
connect; and the device side of a drain written only on the positively
identified recorded router, a replacement router discovered afresh. U29
unknown correspondence holds nothing on a guess: an unattributed group
with any member's entry lost or bus changed is drained and rebuilt whole,
its rebuilds serialised under the learning handoff of part 4.

*Open.* Whether a real USB4 host that loses power without a PnP removal
comes back with its configuration intact or reset, and which the
emulator's `restore` mode models; how long a device router keeps its
configuration while the host side is down (a router that gave up its
enumeration reads as removal evidence and is reconnected as new, its drive
re-enumerated rather than preserved); whether `pci.sys` re-reports an
endpoint whose tunnel stayed up at the containment's invalidation on every
NT target (the retention case of part 4), and how long its removal of a
missing PDO's stack takes where no child of this driver observes it (the
barrier's weaker proof), both read by 42-V.5.

## 8. Thunderbolt 3 device routers behind a USB4 host

Linux keeps the software connection manager for a Thunderbolt 3 device
behind a USB4 host and handles that router by its generation, router by
router, with the same path and PCIe adapter code and a small set of extra
sequences ([linux] `tb.c` `tb_scan_port`, `switch.c`, `lc.c`, `tunnel.c`,
`path.c`). The owner's decision of 2026-10-15 puts it in this phase with
nothing cut (decision U6, changed by the Linux reading from "unsupported"):
an explicit task (40-A.8), PCIe tunnel first, at depth 1. What a device
router reporting a USB4 Version of major 0 needs beyond section 6:

1. **The link**: PORT_CS_18 TCM (bit 9) set on the host's port says it runs
   in Thunderbolt 3 compatible mode ([linux] `usb4.c` `link_is_usb4`); no
   USB3 tunnel over it.
2. **Enumeration** without CMUV, ROUTER_CS_5 or the Configuration Valid
   handshake: the same one write of ROUTER_CS_1 to 4 with Valid. A
   Thunderbolt 3 router may read Upstream Adapter 0 before it is
   enumerated (the TS3 Plus reads ROUTER_CS_1 `0x0602C005` in `usb4-emu`'s
   Framework 13 capture [obs]); its upstream adapter is then the Adapter
   Num of the read's response, the adapter the request arrived at (Spec
   Table 6-3), and the enumeration write sets it ([linux] `ctl.c`
   `tb_cfg_get_upstream_port`, `switch.c` `tb_switch_alloc` and
   `tb_switch_configure`). The
   router's Plug Events vendor-specific capability is required (none: the
   router is refused, [linux] `switch.c` `tb_switch_configure`) and written
   active, bits 2 to 6 of its second dword cleared, with the USB-disable
   bit set except on Alpine Ridge ([linux] `switch.c`
   `tb_plug_events_active`). The Guide makes this the path for a
   Thunderbolt 3 compatible connection manager (3.2.1 step 3), which is why
   CNS stays 0 (section 6.6).
3. **The link controller** capability (vendor-specific; [linux] `lc.c`
   `find_port_lc_cap`): its per-port block's PORT_ATTR bit 12 says bonding
   is possible (`tb_lc_lane_bonding_possible`); after bonding, SX_CTRL L1C
   (bit 16, odd port) or L2C (bit 20, even port) with UPSTREAM (bit 30)
   marks the device's upstream port configured (`tb_lc_configure_port`),
   where a USB4 router takes PORT_CS_19 PC. The host's port still takes PC.
   The bonding register sequence itself is section 6.5's.
4. **UUID** from the link controller's fuse, four dwords ([linux] `lc.c`
   `tb_lc_read_uuid`). The DROM sits behind an EEPROM bit-bang through the
   Plug Events capability (its fourth dword the control, its twelfth the
   DROM's offset in the part; [linux] `eeprom.c` `tb_eeprom_read_n`,
   `tb_drom_bit_bang`). It is read once at connect, after the router's
   capabilities and before its adapters, and parsed as section 6.4 step
   6's DROM (CRC-32 checked, revision 1 entries from byte 22): the vendor
   and model names, and the adapters it marks unused. It is optional for a
   tunnel: an EEPROM that reads not present, a failed read or a bad CRC
   leaves the router unnamed and running. Once the enable is sent the part
   is always deselected and bit-banging disabled again. A router adopted
   from before the start without a connection sequence of this driver's
   (section 9), or rebuilt under a resume budget (section 11, where a
   spent budget would refuse that release), is not read there; one whose
   preserved tunnels this driver completes with a PCIe tunnel goes
   through the connect and is. The first two are owed their names
   (`DromOwed`, task 42-A.9): the core reads the EEPROM on its first poll
   with no budget running and the port `UP` (the start's own first poll
   for an adopted router, the thread's first pass after the resume for a
   rebuilt one), once, as the port's own sequence so that a removal seen
   inside it waits for the port check, and takes only the names - the
   adapters were discovered without the DROM and stay as found, so its
   unused marks are not applied late. A failed read leaves the router
   unnamed, as at a connect; the tunnel and the connection generation are
   untouched, and nothing is re-enumerated (`u4.drom.late`).
5. **The PCIe tunnel**: the same four entries, except that on the
   Thunderbolt 3 router the protocol adapter's ingress fields (initial
   credits, ingress flow control, shared buffering) are written as for a
   lane adapter, 7 credits by default ([linux] `path.c` `tb_path_activate`,
   `tunnel.c` `tb_pci_init_credits`), and the Detect wait covers only the
   USB4 end ([linux] `tunnel.c` `tb_pci_pre_activate`). Credits on the
   Thunderbolt 3 router follow section 6.8's accounting with two
   differences: no Buffer Allocation Request and no Get Capabilities are
   issued to it (Linux initialises USB4 credit allocation on USB4 routers
   only, [linux] `switch.c` `tb_switch_credits_init`), so its preferences
   are the defaults (32 on a bonded lane, 16 single, 7 at the protocol
   adapter); and its control path's credits are taken as 2, Linux's value
   for a router whose HopID 0 entry does not report them ([linux]
   `switch.c` `tb_init_port`). Its router operations are never used.
6. **TMU**: not configured. A Thunderbolt 3 link carries no USB3 tunnel
   (item 1), and PCIe has no accuracy requirement (Guide 7.4.2). Linux
   enables it on these routers anyway ([linux] `tb.c` `tb_enable_tmu`;
   Titan Ridge's objection masking, `tmu.c`
   `tb_switch_tmu_disable_objections`); if a Thunderbolt 3 router does not
   carry a PCIe tunnel without it, Linux's sequence is the second pass,
   listed in section 16.

Left out: the Titan Ridge PCIe L1 setting and the Alpine and Titan Ridge
xHCI connect ([linux] `switch.c` `tb_switch_pcie_l1_enable`,
`tb_switch_xhci_connect`), DisplayPort and CLx. The owner's CalDigit TS3
Plus on a USB4 host is the case this serves. `usb4-emu` models it from
QEMU fork `2a893ea` on (`usb4-device` with `dialect=tb3` behind
`usb4-host profile=usb4-generic`, from the Framework 13 capture [model]),
which the cross-check (section 14.1) and the guest legs of `runs/run-41.md`
read.

The reverse case, a USB4 device behind a Thunderbolt 3 host, is the ICM
client's, unchanged: "Thunderbolt 3 compatible USB4 devices, subject to the
host's firmware", with no USB3 tunnel and no software programming of that
fabric.

## 9. Tunnels already present at start

### 9.1 What may be there

The model's pre-OS firmware connection manager enumerates the host router,
and for each device router clears Lock, enumerates it, sets PTO and CV,
bonds the lanes, programs the PCIe paths with lane HopIDs 8 and 9, sets
Path Enable on both PCIe adapters, consumes its own hot plug events and
hands over without cleaning up; the endpoints are cold-plugged and the
firmware and the operating system find them at boot [model] (`USB4-HOSTS`
section 3). The same state is left by this driver's earlier instance (its
stop leaves tunnels up, section 11), by `USB4CM.EXE` run from the DOS prompt
before `win` [obs] (the `usb4cm` README's Windows 98 validation), and by a
real USB4 BIOS booting a CSM, which is uncaptured (`PLATFORM-MATRIX`
section 4).

Windows 11 cleans such a fabric and rebuilds every tunnel [obs]; Linux does
by default, through Downstream Port Reset on every downstream lane adapter
and the deactivation of every path entry on a version 1 host, and through
Host Router Reset on a version 2 host ([linux] `tb.c` `tb_start`,
`switch.c` `tb_switch_reset_host`, `nhi.c` `nhi_reset`), and keeps the
tunnels when its `host_reset` parameter is off, through
`tb_discover_tunnels` and `tunnel.c` `tb_tunnel_discover_pci`. Both load
before any storage behind the tunnel is mounted. This driver may load after
`nvme98` has mounted a volume behind a cold-plugged tunnel, so the rule
here is the owner's coverage plan (orchestrator decision, owner to
confirm): **this driver never resets a fabric with existing tunnels on its
own initiative**. Host reset, port reset, firmware restart and tunnel
cleanup are four separate operations, and none runs automatically on a
domain that has a tunnel.

### 9.2 Discover and preserve (`XhciCmPreOsCleanup` 0, the default)

The acceptance criteria, on every admission route (owner's coverage plan
of 2026-10-15, reviewed and revised; orchestrator decision, owner to
confirm):

- **Every tunnel type is discovered**, PCIe, USB3 and DisplayPort alike,
  as Linux's discovery walks them ([linux] `tb.c` `tb_discover_tunnels`,
  `tunnel.c` `tb_tunnel_discover_pci`, `tb_tunnel_discover_usb3`,
  `tb_tunnel_discover_dp`, `path.c` `tb_path_discover`), and **every
  resource a discovered path uses is reserved** - each Valid entry's input
  HopID on its ingress adapter, each output HopID on its egress adapter,
  each enabled protocol adapter - before any new path is allocated. A
  DisplayPort tunnel this driver will never build is still reserved, never
  touched.
- **Discovery only reads.** No write during discovery changes an existing
  link's configuration: no lane bonding, Lane Disable, Target Link Width,
  CLx enable, TMU mode, USB4 Port is Configured, credit or path write, and
  no reset of any kind.
- **Discovery depth is not enumeration depth.** Discovery follows each
  discovered path through every router on it, to the Spec's depth of 5
  (Guide 3.2.1 step 2), reading those routers' path and adapter spaces at
  the routes the path itself gives, and never writing; new enumeration
  stays at depth 1. The emulator's pre-OS connection manager configures
  daisy chains [model], so this is not hypothetical.
- **Residual host-side state is discovered whatever the lane reads.**
  On every host adapter (each lane 0 adapter's HopIDs 8 to its Max Input
  HopID, each PCIe Down and USB3 Down adapter's HopID 8, each protocol
  adapter's Path Enable), discovery reads the entries even on a port whose
  lane is disconnected: a Valid entry is a discovered path; an entry with
  Valid 0 but Pending Packets 1 is quarantined (section 7.3); a disconnected
  port's own residual paths (a predecessor's, whose device has since gone)
  are torn down on the host side as section 7.3 tears down an unplugged
  port's, since no tunnel can exist through a disconnected lane - that is
  unplug handling, not a reset - with the same quarantine where a step
  fails. Only a PCIe path found there takes the PCIe steps (the
  path-closed statement, the owed removal): USB3 or DisplayPort residue
  alone is torn down or quarantined without them. A host entry the walk
  could not read is swept from the registers all the same, PCIe path
  found or not, and until a sweep proves the port's host side clear the
  port owes it, retried at the idle tick and before the port connects
  again.
- **Every discovered path has a removal rule.** At the unplug of the port
  it goes through, a discovered PCIe or USB3 tunnel's host-side entries
  are torn down as section 7.3 tears down this driver's own; every other
  discovered path (DisplayPort, or a path this driver cannot classify)
  keeps its host-side entries reserved - quarantined as section 7.3 says,
  never reused - and is logged, since this driver does not drive its
  teardown. A HELD port's resources are reserved the same way.
- **Pre-existing tunnels prove an earlier configuration, not a running
  competing connection manager**: the admission of section 3.4 has already
  decided ownership, and discovery never revisits it.

Per port, at start, after the host router's verification (section 6.1),
reading only:

1. Lane 0 connected (any class section 6.4 step 1 calls connected, low
   power included, its power configuration left as found), ADP_CS_4 Lock
   clear.
2. The device router at route = lane 0 answers, with TopologyID Valid set,
   depth 1, Router Ready, ROUTER_CS_5 PTO and CV set, ROUTER_CS_6
   Configuration Ready set (for a Thunderbolt 3 router, section 8's
   enumeration state instead). Router Ready found clear is waited for
   first, for its 500 ms of section 5.2, by reading ROUTER_CS_6 alone,
   and ROUTER_CS_5 and 6 are read again after it: an enumeration only
   moments old has not set it yet (the ASM2464 sets it after Enumerated,
   section 6.11; [linux] `usb4.c` `usb4_switch_setup` waits the same 500
   ms on a discovered router), and a port judged on that first read would
   be HELD for a tunnel that verifies. Past the 500 ms the step fails as
   before. The model's pre-OS connection manager hands over the moment
   it has written Enumerated, so the `usb4-emu` cross-check reaches this
   wait; a firmware's pre-OS connection manager finishes long before the
   operating system loads.
3. Lanes bonded, or lane 1 disconnected or disabled.
4. The PCIe tunnel as Linux's discovery walks it ([linux] `tunnel.c`
   `tb_tunnel_discover_pci`, `path.c` `tb_path_discover`): the host's PCIe
   Down adapter has Path Enable set and its LTSSM in a live state (L0, L1,
   Recovery or Recovery.Idle, section 7.1 item 4; a transitional state
   re-read for up to 2 s, Detect failing the check); its HopID 8 entry is
   Valid and leads to the host's lane 0 at some HopID `hd` of at least 8;
   the device's lane 0 entry `hd` is Valid and leads to the device's PCIe
   Up at HopID 8; the device's PCIe Up HopID 8 entry is Valid and leads to
   its lane 0 at some `hu`; the host's lane 0 entry `hu` is Valid and leads
   back to the host's PCIe Down at HopID 8; the device's PCIe Up has Path
   Enable set.

The outcomes:

- **All four**: the port is UP and preserved, `hd` and `hu` recorded for
  its eventual teardown; the operating system already has the endpoint, so
  nothing is re-enumerated. A USB3 tunnel is checked the same way and
  preserved; one that does not verify is left as found.
- **No tunnel state at all** (no Path Enable, no Valid entry and no
  undrained entry on any of the port's host adapters, after the residual
  rule above; and, with lane 0 connected, none on the device router's
  adapters either): nothing to preserve; the port is connected normally (section 7), an already
  enumerated router being adopted as it is (its TopologyID kept, section
  6.4's write skipped).
- **Only tunnels this driver does not build** (a DisplayPort tunnel, say)
  beside an otherwise empty PCIe and USB3 side: those are reserved and
  preserved, and a missing PCIe tunnel is built on the link as found - no
  bonding, no lane change, since the Guide forbids starting lane bonding
  while a path is enabled in the port (7.1); no USB3 tunnel is built
  beside a preserved DisplayPort tunnel (section 6.9).
- **Anything else** (a half-built path, Path Enable on one end only, a
  device router that does not answer while the host side carries enabled
  paths): the port is HELD - left exactly as found, logged with every
  register read, served by nothing but its own unplug (section 7.2). A
  user who wants it rebuilt sets `XhciCmPreOsCleanup` 1.

A preserved tunnel never went down, so `nvme98`'s obligation 1 (section
10.1) is not engaged by it, and every register it depends on has just
been read back. The cost: a configuration this driver did not choose
(credits, a TMU mode) stays in force until the device is unplugged.

### 9.3 Explicit cleaning (`XhciCmPreOsCleanup` 1 and 2)

Cleaning is the user's choice, never this driver's, because it
surprise-removes whatever the operating system has behind the tunnel:

- **1, clean what does not verify**: every HELD port is cleaned at start.
- **2, clean everything**: every port with any tunnel state is cleaned at
  start, as Windows 11 and Linux's default do. The README says this may
  remove a mounted volume.

Cleaning one port, the Linux and Windows 11 sequence restricted to it:

1. The orderly PCIe teardown of Guide 5.3.2, both ends reachable: Path
   Enable 0 on the host's PCIe Down adapter, then on the device's PCIe Up;
   every Valid entry on the port's path (host PCIe Down HopID 8, host lane
   0 `hu`, device lane 0 `hd`, device PCIe Up HopID 8, as far as each
   could be read) invalidated and drained (Guide 5.2.2). The same for a
   USB3 tunnel (Guide 5.5.2).
2. On the PCI side the endpoint vanishes: section 10's statement where
   enabled and qualified, then the tunnel ports' removal and
   re-enumeration (section 7.6).
3. Downstream Port Reset on the host's port: PORT_CS_19 DPR (bit 0) set,
   held 10 ms, cleared (Guide 7.3; Spec 6.9; [linux] `usb4.c`
   `usb4_port_reset`). The device router returns to the uninitialised
   state and its link retrains.
4. The port's host-side path entries from HopID 8 to each of its adapters'
   Max Input HopID deactivated ([linux] `switch.c` `tb_switch_reset_host`),
   which clears whatever a previous connection manager allocated that
   section 9.2 did not walk.
5. The port is EMPTY; the retrained link brings it through CONNECTING as
   a fresh plug.

On a version 2 host interface, value 2 uses the Host Router Reset of Guide
3.5 for the whole domain instead of steps 3 and 4 (rings disabled at least
1 ms before, HRR untouched for 50 ms after, the reset complete within 500
ms, the host router enumerated afresh). Step 1 of section 9.3 is also the
teardown of section 6.8 step 5 and of section 10.4's eject, with the
device present.

## 10. The contract with Stor98 (formerly `nvme98`)

`nvme98`, released as Stor98 (record 21 section 7; its `docs/PLAN.md`
section 0.10, gate G3, decisions MD-1 and MD-2) places two obligations on a connection manager (record 21 section 7):
a tunnel is re-established only to an endpoint whose link went down since
the last tunnel; and the link-down statement is sent only after the
tunnel's PCIe path is closed and the endpoint's link-down reset has
completed. Under the firmware connection manager neither could be done
directly. Under this one, this driver opens and closes the paths itself.

The channel is Stor98's link-state table, adopted by `nvme98` on
2026-10-15 (its MD-6), renamed with Stor98 `0.3.0.0` (`stor98cm.h`,
interface version 4: the callback object `\Callback\Stor98LinkState` on
NT, interface 3's proof key `HKEY_DYN_DATA\Stor98LinkState` and locator
`Services\stor98` on 9x) and described with both profiles in record 21
section 7.2; this profile reaches it through the two hooks of the driver
half (`CmNhiSetU4Hooks`, registered where the selection admits this
profile): the UP hook records each port's connection generation for the
presence sample and publishes the port's `UP` itself, before anything can
expose the endpoint (the seam's exposure for the same tunnel up follows,
as for the ICM profile, and finds it published), and the DOWN
hook, at the core's statement upcall, makes or withholds the statement
on section 10.2's condition (`CmNvlinkPortDown`). On Windows 98 SE and
ME, from `3.0.0.0`, the proof key and locator are published (record 21
section 7.2: residency, the proof opened first, the proof, the locator),
so Stor98 `0.3.0.1` binds to the table and sees its function's `UP`; the
DOWN hook still withholds the statement there (section 10.2's condition
needs a reading 98 SE and ME do not have), so no link-down is
acknowledged on them. A removal of a present NVMe function that this
driver starts itself would go through record 21 section 7.2's
`REMOVE_PENDING` handshake (interface 3's R9 and R11, on the CONFIGMG
worker's removal step, `cm_cfgwk.c`); in `3.0.0.0` no eject was such a removal
(every 9x child removal of a tunnel down is the seam's removal of a
vanished function, and the tools' 9x eject went to Windows directly).
Since 2026-10-18 record 21 section 7.2's 9x half (task 42-0.7; the code is
42-A.13) routes the tools' 9x eject through that worker step, hands a clean
removal back to the thread for section 10.4's per-port teardown, and makes
the 9x statement once each system's G3 ordering is read.

**Only a new tunnel moves a generation** (task 42-A.6; `stor98cm.h` as
Stor98 revised it on 2026-10-18, entry protocol step 1 and R8b). The UP
hook publishes its port's `UP` itself, on the port's bridge when the
correspondence (section 7.6) names it, and names the tunnel - the USB4
port and its connection generation (`CM_NVLINK_TUNNEL`). The seam's
exposure that follows for the same tunnel up names that tunnel to the
member the action's port corresponds to (`CmNhiActionMember`) and none to
every other member; a tunnel down's and a RESYNC's name none. An entry
already `UP` keeps its generation unless the exposure names a tunnel other
than the one its `UP` was last published for (`CmNvlinkPublishUp`,
`cmNvTunnel`): so the seam's second exposure of a tunnel the hook already
published, a second port's tunnel up, and a RESYNC leave every other
port's entry as it is, and a bound Stor98 instance keeps its identity
(R8d). An entry not `UP` gets its new generation from any exposure. The
latest tunnel named for each member's bridge is kept (`cmNvTunnelOf`), and
every exposure of that bridge labels what it publishes with it, whatever
it names itself: applied again it changes nothing, and a function a later
exposure - a baseline, an owed re-enumeration, the 9x late scan, a
RESYNC - publishes where an earlier one could not (the bus silent, the
member not yet active) is not advanced again by the named exposure of the
same tunnel. Only an exposure that itself names a new tunnel advances an
entry already `UP` (`CmNvlinkPublishUpEx`'s `advance`); an exposure that
names none never does, since it may come after the operating system has
enumerated the endpoint. Read on Windows ME (the bench zip of 2026-10-19):
after a Stand by the resume rebuilt port 1's tunnel, and its UP hook and
the seam's exposure named the new tunnel but found the secondary bus not
yet numbered, so nothing was published; when a second enclosure was
plugged into port 2, the seam's exposure of port 1's member, naming no
tunnel, advanced port 1's entry for the recorded one, and Stor98, bound
since before the sleep, dropped that disk. Such an entry keeps its
generation now, as an entry does whose new tunnel nothing named. Where the port's
member is not known, nothing names a tunnel: a function newly found gets
its generation, and no entry already `UP` is advanced on a guess (U29's
rule). What lets a new tunnel there still get its new generation is the
DOWN hook: where the port's member is not known it states nothing, but
an entry whose function is read absent (its identifier read, all ones;
a failed read proves nothing) leaves `UP` (`IN_USE` alone), on every
member no other port with a live PCIe tunnel corresponds to, so the
tunnel that follows finds it not `UP`. An entry
not taken out so (no teardown seen, the function answering throughout)
keeps its generation until its member's next named tunnel; it is not
advanced once correspondence is learned, since by then the operating
system has enumerated the function and a bound instance would read the
change as identity loss. The tunnels recorded are forgotten at the
domain's stop, since a later start numbers its connections afresh.

### 10.1 Obligation 1: a link that went down

Every PCIe tunnel this driver builds passes the Detect gate of section 6.8
step 1 first: both the host's PCIe Down adapter and the device's PCIe Up
adapter read LTSSM Detect before any path entry is written, or no tunnel is
built. Detect is the tunnelled PCIe link down at both of its ends at that
moment, which is the condition assumption A1 says resets what is below it.
The cases:

- A fresh plug: a new router, whose adapter starts in Detect.
- A rebuild after a failure or a clean (section 9.3) with the device still
  attached: the teardown cleared Path Enable at both ends first, the link
  drops, and the gate waits for Detect at both ends.
- A preserved tunnel (section 9.2): not a re-establishment; the link never
  went down.
- A Thunderbolt 3 device router (section 8): only the USB4 end is gated
  ([linux] `tb_pci_pre_activate` skips the other), so for such a device
  obligation 1 rests on the router being new, which a re-plug always is,
  and on the rebuild case's teardown having cleared Path Enable at both
  ends before the gate.

### 10.2 Obligation 2: path closed, and the reset

**Path closed is now observable, on the host router.** Section 7.3 step 1
ends with Path Enable 0 on the host's PCIe Down adapter, both host-side
entries' Valid 0 with Pending Packets 0 and tTeardown elapsed, read back,
and the host adapter's LTSSM in Detect. Every packet the endpoint sends
toward host memory enters the host router at the host lane 0 adapter's
`hu` entry, and an ingress adapter drops every packet for a path once its
Valid is 0 and has dequeued what it held once Pending Packets reads 0 (Spec
5.4.2). From that read-back on, no transaction of the endpoint's can reach
host memory, whatever the endpoint does. That is stronger than anything
record 21 could read through the firmware connection manager, and it is
the first half of the statement's condition.

**The endpoint's completed reset is still not observable on hardware.** The
link that went down is the tunnel's; the endpoint sits below the device
router's PCIe Up adapter and, in an enclosure with an internal switch,
below that switch, and nothing below a down link can be read. Record 21
section 7.1's conclusion stands: on hardware the boundary waits for
Stor98's MD-2 verification and the owner, and no hardware statement is
sent until it is settled. On the emulator, gate G3's ordering check (the
statement's marker later than the stage-2 reset and quiesce event in the
host trace) is the qualification per OS; on i440FX the SHPC slot's presence
bit reading clear is the documented later-than-stage-2 signal for a
surprise removal, and a teardown this driver starts reads as one, because
the model's admin-down defaults to surprise removal [model]
(`WINDOWS-DEBUG`; `ARCHITECTURE` section 4.4).

The statement's condition under this profile is therefore: section 7.3
step 1 completed and read back, then record 21 section 6.5's PCI-side
reading of the port's own member of the tunnel-port set, which must be
known by correspondence (section 7.6: no statement is read off the whole
set), then on the
emulator the per-OS G3 qualification and on hardware the unresolved
boundary. A failure in one of the statement's prerequisites (section
7.3 steps 1 and 2, as step 3 lists them) withholds the statement for that
unplug; a failure in a later step does not reach back to it; Stor98's gone deadline (MD-1) is the fallback, as everywhere.

### 10.3 The latency budget

Against Stor98's gone deadline (5 s in its test build, from its own
presence loss), as record 21 section 7.2 requires, under saturation too.
The per-step bounds of section 5.2 are bounds, not a budget: summed, the
worst case of the steps before the statement (several requests at
nominally 400 ms each, before section 5.3's tick allowance, two drains at up to 500 ms, the Detect read, the 2 s PCI-side
reading) passes 5 s. So the statement has a deadline of its own:

- **The order** is section 7.3's: the PCIe host side and its read-back,
  the PCI-side reading, then the statement; USB3 cleanup, lane defaults,
  removal and re-enumeration come after.
- **The anchor** is the time of the last sample that showed the
  endpoint itself present: the latest configuration read of a function
  whose table entry reads `UP` (section 7.1 item 2, one `HalGetBusData` read of
  its vendor and device id per idle tick) that returned that function's
  ids, for the same connection generation, timestamped on section 5.3's
  clock when the read was **issued**, so the time it spent counts against
  the allowance. The endpoint answered its own configuration cycle then,
  so Stor98's presence loss, which is its own registers reading
  all-ones, is no earlier than the anchor; and the emulator closes
  configuration access with MMIO at stage 1 of every down transition
  (`ARCHITECTURE` section 4.4), so its samples go all-ones with the gate.
  Nothing on the fabric side serves: a router reading or the host
  adapter's L0 shows the tunnel's own link, not an endpoint behind the
  device's internal switch, and an event's age at decode is unbounded (a
  router may hold an event, Spec 6.8; Guide 4.3). The sample rides the
  idle tick, so the anchor is normally no older than one idle period
  (250 ms, the ceiling of record 21 section 8 while an entry reads `UP`)
  before the removal is found; a delayed sample only makes it
  older and the allowance below smaller. A connection with no such sample
  (no entry `UP`, or no sample taken since) has no anchor and
  gets no statement.
- **The deadline**: the statement, delivery included, must complete
  within 3 s of the anchor, as measured on section 5.3's clock; a maximum
  measured on that clock may expire up to one tick late (55 ms at most on
  these targets), which the 2 s margin to Stor98's 5 s covers. Queueing behind another port's teardown,
  section 7.3 step 1 and the PCI-side reading all
  spend that one allowance; each step is given only the time left, not
  its own section 5.2 bound; a step that would overrun ends the attempt,
  and a statement with less than 100 ms of it left (the delivery is a
  table write Stor98 polls, so that margin is this driver's, kept from
  the first draft's figure) is withheld and counted. Withheld means MD-1.
  The acknowledgement wait (up to 5 s) follows the statement and delays
  only the removal.

| Part | Bound |
|---|---|
| from the anchor to the removal being found | normally one idle period; counted against the allowance either way |
| queueing behind another teardown, section 7.3 step 1, the PCI-side reading, the table write | together within what is left of the 3 s, or no statement |

So a statement that is delivered at all is delivered within 3 s (plus at
most one tick of clock quantisation) of the last moment the endpoint was
seen present, so within that of its presence loss, inside Stor98's 5 s; task 39-A.7's qualification measures
it per OS, saturated, with the disconnect injected at every point section
7.4 names.

### 10.4 Safe eject

Under the firmware connection manager a per-device teardown does not exist
(record 21 section 7.4; the owner's D10 makes the eject stop the drivers
and leave the unplug to the user). Under this one it does: section 9.3 step
1 is an orderly teardown of one port's tunnel. Decision U9: the USB4
profile exposes it as a per-port entry point on the thread, and task
39-A.8's NT mechanism calls it after the children of that verified tunnel
port have been removed, so the tunnel is down before the user is told the
device may be unplugged - a clause record 21 section 7.4, revision 3,
supersedes for Windows 2000 and later, where the teardown follows an
identity-checked eject confirmation asynchronously and the tools report
what they observe (record 24 section 5.2); on Windows 98 SE and ME the call follows the worker's
removal of the unit, which the tools ask of the connection manager since
2026-10-18 (record 21 section 7.2, the 9x half; the code is 42-A.13): the
worker hands a clean removal back to the thread, which runs the same
teardown, reads the path closed back and publishes the statement's eject
form where the system is qualified. A port ejected this way
stays down until its device is unplugged and plugged again. The eject
tears down the PCIe tunnel alone: a USB3 tunnel on the same port carries
devices of the host's own xHCI tree, which the eject never asked to stop,
so it stays up, as on a port FAILED for PCIe (section 7.2).

## 11. Stop, remove, disable, suspend, resume

- **Stop, remove, disable**: the rings stopped (TX and RX ring control
  cleared [ref] `u4cm_stop`), queued work drained, the thread ended, the
  common buffer freed. Nothing on the fabric is changed: tunnels stay up
  and the devices behind them keep working, as under the ICM client (record
  21 section 5.6) and as Linux leaves non-DMA tunnels at unload ([linux]
  `tb.c` `tb_stop`). Force power follows section 4.2's ownership record.
  Routers keep
  retransmitting unacknowledged hot plug events until the next start
  (Guide 4.3), which discovers and preserves the fabric (section 9.2).
  Decision U7.
- **Shutdown**: the same.
- **System suspend** (D3 for S3 or S4): the NHI marks its tunnel-port
  set's active members held (section 7.6), then the same as stop. The
  Guide's sleep entry (chapter 8.2: Enter Sleep, Sleep Ready, the wake configuration) is
  not in the first cut; a domain that loses power across the sleep comes
  back with its device routers uninitialised.
- **Resume to D0**: section 4.2's force power and LTR where they apply, the
  host interface and rings, then section 7.6's restoration of every
  resume obligation (each tunnel up at suspend, whatever its port's state)
  - verified and preserved, rebuilt where the domain lost its
  configuration, torn down on removal evidence, or HELD on a failed
  verification - and the NT filter's held D0 IRPs released on success or
  through the failure path (section 7.6); only then are other ports'
  connections served. A verification is section 9.2's, its Router Ready
  wait included, inside the resume's budget: the 500 ms is within the
  margin `TbtU4ResumeBoundUs` already counts for the step running when
  the budget runs out (its 2 s longest wait), and a spent budget refuses
  the wait's reads, so the verification fails as it did before the wait
  existed. Hot plug events a wait consumes are not acted on for the port
  being verified until its obligation is judged, so each verification of
  a USB4 router (a Thunderbolt 3 router's has no Router Ready wait and is
  unchanged), and each again after its Router Ready wait, first proves
  the router the one classified: an unplug hint only with no removal evidence behind
  it, lane 0 still unlocked, the same IDs and UUID, still enumerated at
  its route; anything else does not meet the obligation. System suspend with a tunnelled device attached is
  unsupported in this release (section 7.6). Windows 98 on the APM
  machines this project runs rarely reaches S3; the NT guests do, and the
  legs read it.
- **Runtime power management**: none; the host interface is never put into
  D3 while the system runs.

## 12. Registry values, counters, the log

Values, each `REG_DWORD` unless said, each written by every install path
at its default under the don't-overwrite flag, each read once at start
(record 21 section 8's rules):

| Value | Default | Meaning under the USB4 profile |
|---|---|---|
| `XhciCmMode` | 1 | 0 off: the driver binds, logs, touches no ring, writes no configuration register beyond Bus Master; 1 auto: section 3's four tests; 2 the ICM experiment of record 21 (refused on an id with no ICM row); **3 force the USB4 software connection manager** (section 3.6, decision U1) |
| `XhciCmApprove` | 1 | section 6.8 (decision U5) |
| `XhciCmReenum` | 1 | record 21 section 8 |
| `XhciCmTunnelBridge` | absent (`REG_SZ`) | record 21 section 6.3's override, shared: a list of `bb:dd.f` read from the NHI's own device key, so one per domain, the whole tunnel-port set when present; an unordered set, not a port-to-bridge correspondence (section 7.6, decision U13) |
| `XhciCmPollIdleMs` | 200 | record 21 section 8, clamp included |
| `XhciCmUsb3Tunnel` | 1 | 1 build USB3 tunnels (section 6.9); 0 disconnect the USB3 adapters of routers this driver configures (Guide 5.5.3); a preserved USB3 tunnel is left as found either way (decision U3) |
| `XhciCmLaneBonding` | 1 | 1 bond (section 6.5); 0 never bond, disable lane 1 where section 6.5 step 6 can verify it, and run single-lane; a preserved link keeps its configuration either way (decision U4) |
| `XhciCmPreOsCleanup` | 0 | 0 discover and preserve, hold what does not verify (section 9.2); 1 clean what does not verify; 2 clean everything (section 9.3) (decision U2) |
| `XhciLogVerbosity` | as `xhci98.sys` | the shared log rule |

`XhciCmCmdTimeoutMs` and `XhciCmApproveTimeoutMs` are the ICM profile's
alone; the USB4 profile's bounds are fixed by section 5.2, from the Guide
and Linux.

Counters, named `u4.*`, in the extension and emitted at stop through the
log sink: the selection tests and which one made a start inert; control
packets sent, received, retried, timed out, CRC errors, stray responses;
notifications by code, acknowledgements sent; hot plug events by kind (lane
0, lane 1, DP, downstream of a device router, duplicates); port checks and
their outcomes; routers enumerated, refused and why; router operations by
opcode and outcome (supported, not supported, failed, OV timeout); DROM
reads and CRC failures; bonding attempts, x2, single, lane 1 disabled;
Configuration Ready timeouts; PCIe and USB3 tunnels built, failed (and at
which step), torn down; Pending Packets timeouts; Detect-gate timeouts; HCA
timeouts; ports preserved, held and cleaned at start; Downstream Port
Resets; quirks fired; tunnel ports associated by source; statements sent
and withheld by reason. Re-enumeration counters are record 21's `cm.*`. The
log's emission is record 21's decision D12, unchanged.

## 13. Provenance and licence

| Source | Licence | Use | Record |
|---|---|---|---|
| `usb4-emu/tools/usb4cm/core/u4cm.c`, `usb4cm.h` (with `u4hi.c`, `u4regs.h`, `u4types.h`, record 21's) | MIT at `a68bc74`; GPL-2.0-only at `3c74189` | adapted into `src\cm\` under the licence of the commit taken from, notices kept, changed as section 2.2 says | a `legal-provenance.md` section 2a entry per adapted file |
| USB4 Specification 2.0, Connection Manager Guide 2.0, the USB4 2.0 ECN on a disabled TMU, the USB4 DROM Specification (USB-IF, November 2025 bundle) | USB-IF terms, documents only | every register field, sequence and bound cited by section | `docs/references/README.md`, rows of 2026-10-14 |
| Linux `drivers/thunderbolt` and `include/linux/pci_ids.h`, pinned `c6859eed755df351a3978b33cb92365f9b3e8f06` | GPL-2.0 | under the owner's rule of 2026-10-15: ids, register fields, constants, sequences, timeouts and quirk tables taken and cited by file and symbol; code adapted with its copyright lines and SPDX tag kept (the rule below) | `legal-provenance.md` section 3, the subsection record 21 section 9 names, and a section 2a entry per adapted file |
| FreeBSD `sys/dev/thunderbolt`, pinned `9d50245a0f1333e10cc1c4dda16878cd79a7c42b` | BSD-2-Clause | the same: the Ice Lake tunnel port ids, the AMD host ids, the reset timing; code adapted with its BSD-2-Clause notice kept verbatim | the same |
| `usb4-emu`'s documents | MIT at `a68bc74`; GPL-2.0-only from `3c74189` | the test bed's behaviour, cited [model] or [obs] | record 21's rows |
| A public PCI id listing (`pci-ids.ucw.cz`) | its own terms; ids are facts | tunnel port ids the mirrors do not name (section 3.7's filter list) | a `legal-provenance.md` row per batch with the listing's revision, as decision D9's |

The owner's instruction of 2026-10-15 held every code source of this
record to reading and citing until `usb4-emu`'s relicensing to
GPL-2.0-only landed. It landed that day: `usb4-emu`'s `main` at `3c74189`
is GPL-2.0-only, and its handoff's section 3 says that this project may
adapt code directly from `usb4-emu` (`tools/usb4cm` included), from Linux
`drivers/thunderbolt`, from FreeBSD `sys/dev/thunderbolt` and from Intel's
tbtools data, keeping the notices of whatever it adapts. The rule this
record's code follows, for the Thunderbolt and USB4 work only
(`legal-provenance.md` section 3, "The Thunderbolt and USB4 mirrors";
`AGENTS.md`, "Reference Implementations"):

- Code from those sources may be adapted into `src\cm\`. Adapting is
  translating: C89, safe on Windows 98 and 2000, no 64-bit arithmetic,
  `AGENTS.md`'s style, never a paste of a Linux kernel API; `usb4cm`'s
  code changes as section 2.2 says.
- Every adapted file keeps each source file's copyright lines and SPDX tag:
  `SPDX-License-Identifier: GPL-2.0-only` with Linux's or `usb4-emu`'s
  lines; FreeBSD's BSD-2-Clause notice, or tbtools' MIT notice, verbatim.
  It names its source file and is marked as changed, with the date.
- Each adapted file gets a `legal-provenance.md` section 2a entry naming
  the source file, the pinned commit actually read (Linux `c6859eed`,
  FreeBSD `9d50245a`, `usb4-emu` `3c74189` or the commit read) and what was
  adapted, in the same change.
- Nothing moves from this repository or `usb4-emu` into `nvme98`, which is
  BSD-3-Clause.

This record's design does not change with the relicensing - only what the
code may take. The conflict section 17 raised with record 21 section 9's
immediate lift is settled by it for both records: the `usb4cm` files
record 21's task `39-A.2` lifted from `a68bc74` keep their MIT notice, and
a file taken from `3c74189` or later is an adaptation under the rule above
(an orchestrator decision of 2026-10-15, for the owner to confirm).

Behaviour taken from Linux alone stays tagged [linux], unconfirmed, until a
hardware capture shows it, whether or not its code was adapted.

## 14. Verification

### 14.1 Host suites

- **`test_tbt_ring`** (record 21) grows the USB4 transport: control packet
  encode and decode, sequence numbers across retries, section 5.1's
  notification classes (a link error during a request does not fail it;
  ERR_CONN does), hot plug acknowledgement and duplicate suppression, the
  Spec Appendix A.2 CRC vectors, the clock callback's wrap.
- **`test_tbt_select`**, new: section 3's four tests as a pure function of
  the matched id, the subsystem vendor and the register readings, row by
  row of section 3.1's table, including every negative case the owner's
  coverage plan names: an unsupported Host Interface Version, a reserved
  router USB4 Version, a failed FW_READY, the firmware status all-ones,
  `ICM_EN` set with `XhciCmMode` 3, `XhciCmMode` 3 on an Alpine Ridge id,
  an Apple subsystem, a USB-only id; each must come out inert, never as
  the other profile. For the Tiger Lake to Meteor Lake and Maple Ridge
  rows every combination of a valid read: `NVM_AUTH_DONE`/`ICM_EN` 0/0
  admits the software connection manager; 1/0 admits it and records the
  experimental case; x/1 goes to the ICM admission (whose `NVM_AUTH_DONE`
  poll failing is inert); and no valid read is inert. On a non-Intel
  candidate the Intel status register is never read (the fake records
  every register access).
- **`test_u4cm`**, new: the connection manager against a fake fabric written
  in C89 in `test\` (decision U11): a table-driven host router with two
  ports and the `intel-adl` adapter layout as a second preset, two host
  interfaces (two domains) at once, device routers with PCIe Up and USB3 Up,
  a Thunderbolt 3 device router with the Plug Events and link controller
  capabilities, path configuration spaces with Pending Packets, router
  operations each switchable between supported, not supported, failing and
  never completing, and scripted hot plug events. Cases: a cold device at
  start; a hot plug; an unplug in every state of section 7.2, including in
  the Router Ready and Configuration Ready waits (an unplug inside the
  Detect, L0, lane 1 CL0 or HCA polls has no vector); a quick unplug and re-plug seen only through Lock, TopologyID
  Valid or the UUID; bonding succeeding, impossible, timing out with the
  fallback's preconditions met (lane 1 disabled) and unmet (FAILED, no
  path), and with a predecessor's non-default lane fields; every router operation unsupported; the Detect gate timing
  out; HCA never answering; section 9.2's discovery preserving, finding
  nothing, and holding at each of its checks; sections 9.3's values 1 and
  2; approval 0; `XhciCmUsb3Tunnel` 0; the quirk table row by row; the
  teardown read-back that gates the statement, a failure in a
  prerequisite step withholding it and one in a later step not; the
  anchor of section 10.3 (a delayed scan shrinking the allowance); Get
  Capabilities answered, Not Supported, failing and blocked on a preserved
  router (unknown state: no Buffer Allocation, no acknowledgements); an inert start after activation draining before
  freeing. Since 2026-10-15 also the ASM2464 preset of section 6.11 (its
  Thunderbolt 3 compatible DROM with the unused adapter, the TMU at `C0h`
  and the vendor capabilities after it, Router Ready 5, 450 and 700 ms
  after Enumerated, and 5 ms behind a pre-OS tunnel's discovery (waited
  for, preserved, the router written exactly as with Router Ready on
  time) or never set there (HELD after one bounded wait),
  Buffer Allocation with PCIe alone, NVM Sector Size
  failing, the 594 ms reconnect after the cleaning's port reset), a USB4
  DROM marking an adapter unused, and a DROM with a bad CRC or an entry
  list past its end (nothing believed); and, on the generic USB4 device
  router rather than that preset, a resume with Router Ready 5 ms late
  (verified, nothing written to the router), never set, the budget spent
  inside its wait, and another router (another UUID) in its place during
  it (never verified as the first).
- **`test_u4cm`, containment** (section 7.7): the fake fabric's host
  interface can be lost at any request (reads all ones, writes dropped,
  nothing consumed), with or without the gate's cancellation. Cases:
  containment between ticks with an action, a deferred tail and a resync
  queued; a loss inside a tunnel's setup, inside an eject and inside a
  resume, each with and without the cancellation; a loss at stop beside a
  healthy stop. Each must end within its bound, claim no tunnel up and no
  closure, and once contained make not one register access, request,
  action or statement from any entry point, the DMA record unchanged.
- **`test_tbt_topo`** (record 21) gains the tunnel-port set: two ports with
  two tunnel bridges, the mapping operation's routing ids, a discrete
  controller's downstream ports, an integrated host's listed root ports
  beside unlisted ones (never associated), and the learning rule.
- **The `usb4-emu` fabric cross-check**: this project's pure core compiled
  with `usb4-emu`'s own MSYS2 toolchain and linked with its fabric model as
  `tools/usb4cm/host/run-host-test.sh` links `usb4cm`'s, from a script in
  this repository that reads `usb4-emu` in place and writes only under this
  repository's `out\`. A reading recorded per run, not a gate.
  Against a fork that has the `asm2464` model (QEMU fork `20655c3` and
  later) it also links `usb4-router-img.c` and `usb4-router-asm2464.c`
  and runs section 6.11's device on `intel-adl`: cold, hot plug, unplug,
  re-plug, the pre-OS tunnel preserved and cleaned.

### 14.2 The virtual-machine legs

Rules as record 21 section 10.2: this project's own overlays under
`vm\usb4\`, ports 47900 to 47999, `usb4-emu` never modified, no QEMU process
this project did not start ever stopped, `-trace 'usb4_*'` on every leg,
TCG, i440FX with `-cpu pentium3,-apic` for Windows 98. The profile is
`usb4-generic` through this project's own launcher (task 39-0.7), with
`host-model`, `preos-cm` and the USB3 device lines passed as
`-global usb4-host.<property>=` and `-global usb4-device.<property>=`
where the launcher does not build them (whether `-global` reaches each
property is task 40-0.3's reading). On Windows 98 SE (40-V.1), then the
same table on Windows 2000 (40-V.2):

| Leg | Setup | What is read |
|---|---|---|
| U.1 cold device | `minimal`, a device attached at power-on, no pre-OS connection manager | the driver installs by class; the trace shows enumeration, bonding, CV, the four entries, Path Enable, `usb4_tunnel_transition ... up`; the tunnel port re-enumerated; the NVMe found and its volume mounted with no Refresh |
| U.2 hot plug | `minimal`, nothing attached, then `device_add usb4-device` | the plug event acknowledged; U.1's path |
| U.3 unplug and re-plug, idle | after U.2, `device_del`, then `device_add` | the unplug noticed (event or scan), section 7.3's teardown in the trace, the volume gone, the devnode removed; the re-plug through U.2 with the Detect gate in the trace |
| U.4 unplug during I/O | a copy of a large file running, then `device_del`; the injection points and stage markers through `usb4-emu`'s fault-injection harness (`docs/HARNESS.md`), as `nvme98`'s G3 legs use it | no hang of this driver; the teardown read-back; the statement withheld unless qualified on that OS (G3), `nvme98`'s MD-1 otherwise |
| U.5 `intel-adl` | U.1 to U.3 with `host-model=intel-adl` | PCIe Down 8 paired with port 1, `ERR_ADDR` on 10 and 11, the DP IN plug events acknowledged and ignored, the mapping operation Not Supported and the pairing rule used, Buffer Allocation answered, the USB3 bandwidth quirk firing on `8087:463E` |
| U.6 pre-OS, preserve | `preos-cm=on`, both host models, `XhciCmPreOsCleanup` 0, with record 21's POST workaround for an NVMe present at power-on | section 9.2 preserves; no transition and no reset in the trace; the volume present from boot and still mounted after the driver starts |
| U.7 pre-OS, clean | the same with `XhciCmPreOsCleanup` 2 | section 9.3 in the trace as Windows 11's run shows it; the volume removed and back |
| U.8 USB3 | a `qemu-xhci` with `usb3-xhci`, `usb3-port1` and the device's `usb3-dev` (the `USB3=1` lines of `run-cm-test.sh`) | the HCA handshake; the USB3 tunnel up; the USB disk appearing on the `qemu-xhci` under `xhci98.sys` at SuperSpeed; with `XhciCmUsb3Tunnel` 0 the adapters disconnected and no disk |
| U.9 disable and enable | after U.1 | the tunnel preserved across the cycle (section 9.2); the volume stays |
| U.10 policy values | `XhciCmApprove` 0, `XhciCmMode` 0, `XhciCmLaneBonding` 0; `XhciCmMode` 3 on the `tb3-alpine-ridge` profile | no PCIe tunnel, USB3 still built; nothing touched; single lane with lane 1 disabled where section 6.5's preconditions hold, else FAILED; mode 3 refused because `ICM_EN` reads set |
| U.11 two ports | a second tunnel bridge on `tunnel-port2` and a device on port 2 | each port's tunnel re-enumerates its own bridge on `minimal` (mapping operation); on `intel-adl` both verified ports are rescanned |
| U.12 a daisy chain | `CHAIN=1`'s device lines | the depth-2 router left uninitialised, its plug event acknowledged and counted |
| U.13 inert | the `tb3-alpine-ridge` profile's NHI id forced through a copy of the INF onto a USB4 row, and a `usb4-generic` host with `XhciCmMode` 0 | each start inert with its reason logged, no ring built (the trace shows no ring 0 traffic) |
| U.14 host interface lost | U.1, then the NHI taken away with no PnP removal, as far as the emulator allows (its gate closed beneath a gated root bridge, or a surprise removal of the function); with I/O running on the volume | section 7.7: the instance contained at the first sample, no bugcheck and no hang, the guest still running; the trace shows no ring 0 traffic and no router request after the loss; the volume removed by the reconciliation where the operating system allows |

40-V.3 reads the install and U.1 to U.3 on ME, XP, XP x64, Vista and 7 in
both architectures, with no checkpoint tax, as record 21's V.3 tiers; on 7
x64 on q35 the native PCIe hot-plug of `usb4-tunnel-port` may act before
the filter, which is a reading, not a fault.

### 14.3 Gates

The import gate gains the clock's imports of section 5.3: on x86
`ntoskrnl.exe!KeTickCount` (data) and `ntoskrnl.exe!KeQueryTimeIncrement`
in `xhcicm-imports.allow`, each with design record 13's Windows 98 SE
evidence row; on amd64 whatever the build's import table shows for the
same two calls, in `xhcicm-imports-amd64.allow`. Nothing else: section
4.2's configuration accesses are the `IRP_MN_READ_CONFIG` and
`IRP_MN_WRITE_CONFIG` route already used for Bus Master. The INF gate (`check-cm-inf.ps1`) gains the class line and the
named ids on every path of the NHI INFs, a rule that no device-router id
(section 3.1's last paragraph) appears there, and, for the filter INFs, the
rule that every id carries `&CC_0604`; `CM-NO9XBRIDGE` keeps every bridge
id out of the `$CHICAGO$` INF.

### 14.4 Hardware

None is a condition of the cut (decision U10): the owner holds no USB4 host
whose behaviour under a legacy operating system has been read, and task
U4HW.1 (real USB4 devices; task 40.4 until 2026-10-16) waits on a USB4 host that boots these systems.
The release notes say the USB4 profile has run on the emulator only, and
that every real host id acts untested.

## 15. Decisions

Each was taken by the orchestrator of the 2026-10-15 run on this record's
recommendation, so that code can proceed; each is the **owner's to
confirm**, except U17, which the owner took on 2026-10-15. The Linux and
FreeBSD reading and the coverage review changed U1, U2, U6, U8 and U13
from their first form, as each row says.

| Id | Decision | Taken (orchestrator decision, owner to confirm) | Sections |
|---|---|---|---|
| U1 | What `XhciCmMode` 3 means | the experimental user override forcing the USB4 software connection manager past test (c)'s row, on hosts it supports only; it cannot bypass platform preparation, a running firmware connection manager (`ICM_EN` set, where established), an unsupported protocol, tunnel preservation or the DMA cleanup rules; refused on Falcon, Alpine and Titan Ridge NHIs. Changed: the first form allowed it wherever the class matched; now it is the only forced route of section 3.4's three, and no failure is ever a fallback to it | 3.4, 3.6, 12 |
| U2 | Tunnels found at start | `XhciCmPreOsCleanup` 0: discover and preserve, hold a port that does not verify; 1 and 2 clean on the user's request. Changed: the first form cleaned unverified ports by default; no reset now runs on a fabric with tunnels unless the user asks | 9 |
| U3 | USB3 tunnels | `XhciCmUsb3Tunnel` 1, built with the bandwidth handshake, since the Guide says "must"; 0 disconnects the adapters of routers this driver configures; preserved tunnels are left as found | 6.9 |
| U4 | Lane bonding | `XhciCmLaneBonding` 1; on failure or at 0, lane 1 disabled per the Guide, verified, before paths over lane 0, else no paths (Linux does not disable it); preserved links untouched | 6.5 |
| U5 | Approval under the software connection manager | record 21's `XhciCmApprove` carried over: 1 builds PCIe for every device router; 0 builds none for a router this driver enumerated, preserves preserved tunnels, and leaves USB3 alone | 6.8 |
| U6 | What is driven | device routers at depth 1; USB4 version 2 routers run as version 1. Changed: Thunderbolt 3 device routers at depth 1 behind a USB4 host, router by router, PCIe first, are in the phase (task 40-A.8), by the owner's no-cuts decision | 6.2, 8 |
| U7 | The driver's own stop | tunnels left up, force power per section 4.2's ownership record, the fabric discovered at the next start; no Guide chapter 8 sleep flow in the first cut | 11 |
| U8 | The NT filter's ids for USB4 tunnel ports | Changed (first form: the emulator's id only): the ids the mirrors name, ids read from a public PCI id listing by task 40-0.2 with their provenance, and the emulator's two, every flavour; a user line in a copy of the INF for anything else; a matched port acts only once associated (section 7.6) | 3.7, 7.6 |
| U9 | Safe eject under the software connection manager | an orderly per-port teardown that 39-A.8's mechanism calls, for the children of associated tunnel ports only | 10.4 |
| U10 | Cutting with the USB4 profile emulator-only | yes: `3.0.0.0` carries it, the release notes saying "run on the emulator only" and every real host id untested but active by default | 14.4 |
| U11 | The host suites' fabric | a C89 fake fabric of this project's own as the gate, plus the `usb4-emu` fabric cross-check as a recorded reading | 14.1 |
| U12 | The device description | Changed by the owner, 2026-10-15 (first form: `xHCI98 USB4 Host Interface`): `xHCI98 USB4 Host Router` for every USB4-generation host whichever connection manager runs it (the "ICM or USB4" ids included), `xHCI98 USB4 PCI Bridge` for its tunnel ports, the same service and binary | 3.7 |
| U13 | `XhciCmTunnelBridge` with more than one tunnel port | Changed (first form: a list indexed by USB4 port): record 21 section 6.3's per-domain override, an unordered set; the port-to-bridge correspondence is router-named or learned (section 7.6), never taken from list order | 7.6, 12 |
| U14 | The selection | section 3's four tests, the inert start in its two forms, the three positive admissions of section 3.4, and the hardware-profile table apart from the CM table (owner's coverage plan, 2026-10-15) | 3, 4 |
| U15 | A resume that fails to restore every tunnel | an exception to the coverage plan's "tunnels restored before dependent PCI devices resume": the NT hold is released through record 21's failure path (release, then invalidation), since holding a bridge's D0 for ever would hang the system's resume; system suspend with a tunnelled device attached is unsupported in this release on every target and the release notes say so; on 9x there is no hold at all | 7.6, 11 |
| U16 | The TMU | configured (HiFi-Bi) only on links that will carry a USB3 tunnel, as the Guide's accuracy rule requires, never on a preserved link; PCIe-only links and Thunderbolt 3 links left with the TMU off; CLx never enabled | 6.6, 8 |
| U17 | A host interface lost under this profile | the owner's decision of 2026-10-15, not an orchestrator one: containment only, automatic recovery a later item of the roadmap's planned work; a terminal CONTAINED state on the monitor's first sign of loss, a surprise removal or a resume that finds the host interface gone, refusing transport, ejects, deferred teardown, UP actions, suspend and resume until a PnP stop and start or a reboot; force power and an unproven DMA buffer kept; no recovery in `3.0.0.0`. **Revised 2026-10-18** by the owner's opening of Phase 42, which promoted the planned recovery (`U4REC.1`) into `3.1.0.0` as tasks 42-0.5 and 42-A.10: CONTAINED is no longer terminal, the recovery of section 7.7's revision following on its precondition (decisions U18 to U29 there, the orchestrator's) | 7.7 |

## 16. What is open, and what is a risk

- **Every real USB4 host is untested.** The emulator's two host models are
  the only fabrics this has run against; the owner's decision makes every
  listed id act by default with a warning.
- **Polling on real hardware**: no reader has run a host interface with no
  interrupt; the done-bit model is what all three rely on, but a host that
  writes descriptors back only with an interrupt enabled would hang the
  first request (each bound in section 5.2 makes that an inert start, not a
  hang).
- **Below-4-GB DMA** on hosts whose driver Linux runs with a 64-bit mask:
  expected to work, qualified per host.
- **TMU** is configured only for USB3 tunnels (section 6.6). A router that
  withholds Configuration Ready or a PCIe tunnel on a link without it, or a
  Thunderbolt 3 router that will not carry a tunnel without it, would need
  the TMU on every link first, as Linux does (section 8 item 6).
- **Version 2 routers run as version 1** (section 6.2): untested.
- **The tunnel-port set on integrated and USB4-only hosts** rests on
  record 21 section 6.3's sources and this record's router-named one
  (section 7.6): a host whose ports no list names, whose router does not
  answer the mapping operation and whose user sets no override runs
  without re-enumeration, and on a machine with two NHIs of one family the
  integrated root ports need the override unless the router-named source
  assigns them.
- **The best-effort Tiger Lake to Meteor Lake rule**: a valid `ICM_EN` = 0
  admits the software connection manager whatever `NVM_AUTH_DONE` says
  (section 3.3); a firmware that enables its connection manager only later
  would then meet a running software one. No capture shows either order;
  the case is logged, and a host whose `ICM_EN` reads set at a later start
  takes the ICM path then.
- **A Thunderbolt 3 device on a USB4 host** has no emulator preset; its
  task is proven by host suites until one exists, and on the owner's TS3
  Plus only if a USB4 host is at hand.
- **Removal below depth 1 on NT**: a preserved router removed at depth 2
  or deeper is torn down on the fabric side, but no filter reaches the bus
  its endpoint sat on, so the operating system learns of it only from the
  endpoint's driver or a manual rescan (section 7.1).
- **Resume ordering on 9x**: no mechanism holds a dependent device's
  resume until the tunnel is back.
- **The `nvme98` statement on hardware**: unresolved, as record 21 section
  7.1.
- **A host interface lost** (section 7.7): contained, and in `3.0.0.0`
  never recovered (the recovery is designed in section 7.7's revision of
  2026-10-18, task 42-0.5, emulator-only until a real host's loss is
  read); until it is built the domain stays without a connection manager until a PnP
  stop and start or a reboot, and its unproven DMA buffer stays allocated
  until the next boot. Which real USB4 hosts lose power or their PCI
  configuration without a PnP removal under a legacy operating system is
  unread. On Windows 98 and ME the reconciliation is the CONFIGMG
  worker's (`cm_cfgwk.c`, the children retained at tunnel up removed
  without reading a bridge header); a containment before the seam's
  first action has opened that route requests none, and the operating
  system then learns of the devices only from their own drivers.
- **Daisy chains, DisplayPort, CLx, sleep per Guide chapter 8**: out of
  this cut.
- **The ASM2464 (section 6.11) on hardware**: the capture is Linux's, on a
  UEFI host. Whether the real router's PCIe Up adapter needs credits
  written in its HopID 8 entry (it resets to none; this driver keeps them
  as read, as Linux does), what its unused adapter 4 answers if read, and
  the PCIe switch in front of the NVMe under Windows 98 SE and 2000 are
  unread; planned task U4HW.1 (40.4 until 2026-10-16) waits on a USB4 host that boots them.

## 17. What this record needs record 21 to say differently

Record 21 draft 6 (merged into `3.0.0.0` at `59685574`) already carries
the shared selection, hardware profiles, reset and takeover rules,
tunnel-port sets and provenance this record builds on. For the
orchestrator to apply to it (this record does not edit it). On
2026-10-15 the second, third and fourth items were applied to record 21
(sections 1.4.2, 9 and 6.3); the others remain for the tasks that need
them:

- Section 8's `XhciCmMode` row: value 3 now points to this record's
  section 3.6; the Phase 40 values named there are defined in this
  record's section 12, `XhciCmPreOsCleanup` taking 0 to 2.
- Section 1.4.2's power-ownership record: an inert instance never clears
  force power it set, not even after its own FW_READY poll failed (a
  readiness timeout is no evidence that nothing depends on the controller;
  this record's section 4.2), and this record's refinement for the USB4
  profile (an active instance does not clear force power it set while a
  device router is connected).
- Section 9: its immediate lift of `usb4cm`'s MIT files conflicted with
  the owner's instruction that `usb4-emu`'s code, `tools/usb4cm` included,
  is read and cited only until the relicensing is announced; the
  relicensing landed (`3c74189`) and settles it for both records (this
  record's section 13).
- Section 6.3's failed-resume path (release, then invalidate) and its
  "system suspend with a tunnelled device attached is unsupported" are an
  exception to the coverage plan's requirement that tunnels be restored
  before dependent devices resume; this record records it as decision U15
  for the owner, and record 21 should carry the same decision.
- Section 3.3's id lists: the NHI INFs gain the class line
  `PCI\CC_0C0340`, the Lunar Lake, Panther Lake, Wildcat Lake and Barlow
  Ridge NHI ids and AMD's `1022:1668` and `1022:1669` (this record's
  section 3.7); the filter INFs gain the emulator's `1B36:0001` and
  `1B36:000C` and the USB4-only hosts' port ids task 40-0.2 reads.
- Section 6.3: the router-named source and the learned correspondence,
  for the USB4 profile, and the `nvme98` statement's refinement that a
  port's member known by correspondence allows it in a set of more than
  one (this record's section 7.6).

## Sources

- USB4 Specification 2.0 (sections 4.4.5.2, 5.2.1, 5.2.5, 5.4, 6.4, 6.8,
  6.9, 8.2, 8.3.1, 12.6, Tables 5-16, 6-12, 8-26, 8-47, 8-48, 12-11 to
  12-13, 12-38) and USB4 Connection Manager Guide 2.0 (sections 2.4, 2.5,
  3.1 to 3.5, 4.2, 4.3, 5.1.4, 5.2, 5.3, 5.5, 6.1, 6.2.2, 7.1, 7.3, 7.5,
  7.7, 9.1), November 2025 bundle; the USB4 2.0 ECN "Configuration and
  Behavior When TMU is Disabled".
- `usb4-emu` `main` `5ba7f60` (read 2026-10-15 for section 6.11):
  `docs/XHCI98-HANDOFF.md`, `docs/captures/ORICO-ASM2464.md`,
  `docs/captures/P14S-ORICO-2026-10-15.md`; its QEMU fork `20655c3`:
  `hw/usb4/usb4-router-asm2464.c`, `usb4-asm2464-data.h`,
  `usb4-router.c`, `usb4-fabric.c`.
- `usb4-emu` (branch `main`, `a68bc74`, read 2026-10-15):
  `docs/XHCI98-HANDOFF.md`, `ARCHITECTURE.md`, `WINDOWS-DEBUG.md`,
  `USB4-HOSTS.md`, `PLATFORM-MATRIX.md`, `NVME-DRIVER-EVAL.md`,
  `tools/usb4cm/README.md` and `core/*`, `scripts/run-win98.sh`,
  `scripts/run-cm-test.sh`.
- Linux `drivers/thunderbolt` (`pci.c`, `nhi.c`, `nhi.h`, `nhi_regs.h`,
  `icm.c`, `tb.c`, `tb.h`, `tb_regs.h`, `switch.c`, `usb4.c`, `path.c`,
  `tunnel.c`, `ctl.c`, `lc.c`, `eeprom.c`, `tmu.c`, `clx.c`, `quirks.c`,
  `acpi.c`) and `include/linux/pci_ids.h`, pinned `c6859eed`; FreeBSD
  `sys/dev/thunderbolt` (`nhi_pci.c`, `nhi.c`, `nhi_reg.h`, `nhi_wmi.c`,
  `hcm.c`, `router.c`, `tb_pcib.h`), pinned `9d50245a`; read 2026-10-15.
- `nvme98/docs/PLAN.md` section 0.10, gate G3, decisions MD-1 and MD-2, as
  record 21 read them.
- This repository: design record 21; `AGENTS.md`; `src\xhci98.inf`.
