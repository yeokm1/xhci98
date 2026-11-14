# Design Record 22 - USB4 Software Connection Manager

Status: draft 2, 2026-10-15, before code, reviewed and revised the same
day. Written for Phase 40 of
`roadmap-usb4.md` ("USB4: the Software Connection Manager", planned as
`U4CM` until the owner numbered it on 2026-10-15), task 40.1. Nothing in
`src\` implemented it when it was written; Phase 40 implemented it and it
shipped in `3.0.0.0` (cut 2026-10-17), and the record is revised since as
the paragraphs below say (the latest 2026-10-25). The owner's decision D7 of 2026-10-15 puts this
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

Revised 2026-10-19 (task 42-A.25, from the matrix reading on Windows ME
that day): section 10 - only an exposure that itself names a new tunnel
advances an entry already `UP`; a tunnel named after a resume while the
secondary bus is not yet numbered labels later publications and is never
paid by a later exposure that names none.

Revised 2026-10-25 (task 44.26, planned as RESUME.1; found by task 43.71):
section 9.4, new - an instance hands its successor on the same NHI a
record of the tunnels it built, so that a tunnel found preserved after a
PnP stop and start of the NHI is still rebuilt across a sleep that lost
the domain's configuration (section 7.6); sections 7.6 and 11 point to it.

Revised 2026-10-29 (task 45.22, the owner's "Phase 45 additions"; a
design, reviewed and revised, no code): section 5.4, new - the ring
interrupt for this profile, designed with the ICM client's in record 21
section 4.5; section 1.3's "polling only" stands until it is coded.
Coded 2026-10-30 behind `XhciCmRingInterrupt` 0 (record 21 sections
4.5.10 and 4.5.11), reviewed and revised: polling stays the default.

Revised 2026-10-29 (task 45.21, the owner's "Phase 45 additions"; a
design, reviewed and revised, no code): section 19, new - Thunderbolt 1, 2 and 3 hosts on
Apple Macs run by this connection manager, from Linux's facts and Apple's
public support pages. Section 3.1's inert start for subsystem vendor
`106B` stands until section 19's code.

Revised 2026-10-25 (task 44.39, planned as U4HW.1): section 18, new - the
ASMedia ASM4242, a USB4 host whose firmware can boot a CSM, as
`usb4-emu`'s asm4242 model and data record it: the firmware's
pre-OS tunnel, the host router, the tunnel-port set's gap on this host,
the legacy targets' open questions and the bench (task 44-E.6); sections
9.1, 14.4 and 16 point to it. No code changes with it. Revised the same
day with the owner's answers to section 18.7: the ASM4242 family (task
44.40, the one code change, in `cm_ports.c`) closes section 18.4 item 3's
gap. Revised
again that day: the credits question answered with an opt-in bench
switch, `XhciCmAsmediaPcie7Credits` (task 44.41, section 18.8; sections
6.8 and 12 point to it).

Revised 2026-10-29 (task 45.20, the design half, reviewed and revised
the same day; no code with it; decisions U30 to U32 confirmed by the
owner on 2026-10-29, each as recommended): section 6.2 rewritten - a USB4
version 2 router, Barlow Ridge among them, is run as a version 2 one:
CMUV 2.0, the Sequence bit capability set by Set Capabilities, path
entries written as two dwords, PCIe Extended Encapsulation, the HiFi
Enhanced Uni-Directional TMU between two such routers, and Gen 4 links,
symmetric or asymmetric, classified and used as found; sections 1.3,
5.1, 6.1, 6.4, 6.5, 6.6, 6.7, 6.8, 7.1, 7.3, 9.2, 9.3, 12, 15 (U6,
U16, the new U30 to U32) and 16 follow it.

Revised 2026-10-30 (task 45.20, the code; reviewed and revised): section
6.2 is coded in `src/cm/tbt_u4cm.c` (with `tbt_regs.h`, `tbt_ring.c`'s
LINK_RECOVERY delivery and `XhciCmUsb4V2` in `cm_vals.c`, `cm_reg.c` and
`cm_nhi.c`), its host vectors in a suite of their own, `test_u4v2`
(sections 6.2.4 and 14.1), over a version 2 host and device router added
to the fake fabric. Section 6.2.4's last paragraph records where the
code settles what the section left open; `usb4-emu` has no version 2
router model yet, so the q35 legs of a version 2 link are owed with it
(section 16).

Revised 2026-10-31 (task 45.56, the design): section 6.2.3 item 4
rewritten - Gen 4 Link Recovery is enabled on a depth-1 Gen 4 link
between two version 2 routers, behind a new value
`XhciCmGen4LinkRecovery` (1 by default), with the recovery check Guide
7.6 asks of a connection manager after each LINK_RECOVERY, and the bit
cleared again at the host's end at a teardown and on both ends before
this instance stops or suspends; a new section 6.2.5 names the code and
the vectors. Sections 1.3, 5.1, 6.2.2, 7.1, 7.3, 11, 12, 15 (U33) and
16 follow it.

Revised 2026-11-06 (task 47.21, was 45.21; a design, reviewed and
revised): section 19.10, new - what the emulator can still read of
section 19 (Titan Ridge, the other systems Apple supports, a device of
another generation, a tunnel the Mac's firmware built), and the router's
generation in the status record and the tools' words.

Revised 2026-11-06 (task 47.32, a design for the owner, reviewed and
revised): section 9.3 gains "Once a boot" and 9.3.1 - value 2 cleans
this driver's own tunnel once a boot, not again after a restart of the
NHI's stack.

Revised 2026-11-08 (Thunderbolt networking on the ASM4242, reviewed and
revised): section 18.9 - on the ASM4242's host router, Pending Packets
on the Host Interface adapter's path entries are read as 0.

Revised 2026-11-09 (task 48.35, a design, reviewed and revised, no code):
section 11.1, new - standby and hibernation with a device attached: the
Guide's sleep entry (chapter 8.2) on this profile and the resume after
it, the firmware connection manager's and the integrated hosts' sleep,
what this driver cannot order on a native PCIe hot-plug tunnel port, the
XP x64 and 7 x64 shutdown stall read as another driver's, and the owner's
questions; decisions U34 to U36 proposed (section 15). Sections 7.6, 11
and 16 point to it; until it is coded they describe what ships.

Revised 2026-11-09 again (task 48.35, the owner's answers and the code,
reviewed and revised): section 11.1.11 records the answers, decisions U34
to U36 are decided, and section 11.1.13 names the code; `XhciCmSleepEntry`
is 1 since the emulator legs passed (section 11.1.12).

The sources, and the tag each fact carries below:

- **[spec]** the USB4 Specification 2.0 ("Spec") and the USB4 Connection
  Manager Guide 2.0 ("Guide"), November 2025 bundle, fetched per machine
  into `docs/references/usb4_202511/` (`docs/references/README.md`), cited
  by section and table.
- **[model]** the `usb4-emu` repository (the `usb4-emu` checkout beside this repository,
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
say which. (Superseded for one host on 2026-10-25: section 18 reads the
ASM4242 from `usb4-emu`'s asm4242 model and data, tagged [hw].)

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
- Polling by default, as record 21 section 4.3 decided for the ICM client
  (decision D3); the ring interrupt beside the idle poll of section 5.4
  and record 21 section 4.5 (task 45.22) is coded behind
  `XhciCmRingInterrupt`, 0 by default, the line on NT only (on every
  system since task 48.26).
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
  Design record 34 designs a minimal two-level, PCIe-only chain for a
  later release.
- DisplayPort tunnels, DP resources and DP bandwidth allocation: a DP plug
  event is acknowledged and ignored.
- CLx low-power states: never enabled; runtime power management: none.
  The Time Management Unit is configured only as far as a USB3 tunnel
  needs (section 6.6).
- Inter-domain (host-to-host) links and the DMA port: never set up.
- Initiating an asymmetric link or a Gen 4 link recovery, USB3 Gen T
  tunnels and the other USB4 version 2 features section 6.2.2 item 11
  lists. A version 2 router is run as a version 2 one since task 45.20:
  CMUV 2.0, the Sequence bit capability set, Gen 4 links symmetric or
  asymmetric used as found (section 6.2); since task 45.56 a depth-1
  Gen 4 link between two version 2 routers also recovers from an
  uncorrectable error (section 6.2.3 item 4).
- MSI and MSI-X: polling (the line interrupt of section 5.4 runs behind
  `XhciCmRingInterrupt`, task 45.22, on NT and since task 48.26 on 98 SE
  and ME too).
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
ASM4242, from public sources only), a host with no
firmware connection manager: under a CSM, this record's connection
manager is the only one it can have. (Since 2026-10-25, section 18: the
ASM4242 has no firmware connection manager an operating
system can talk to, but its firmware does run a pre-OS one that builds a
PCIe tunnel before the hand-off.) The legacy operating systems need a
CSM, so the realistic USB4 targets are such ASM4242 hosts; the
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
| AMD Pink Sardine `1022:1668` `1022:1669`, Rembrandt `162E` `162F`, Phoenix `15C4` `15C5`, Strix `151C` `151D`, Strix Halo `158D` `158E`, Krackan `1120` `1121`, Krackan2 `113B` `113C`, named; any other host of class `0x0C0340` (ASMedia, other AMD, the emulator's `1B36:00F4`) | generic | USB4 only | [bsd] `nhi_reg.h` `DEVICE_PINK_SARDINE_*`; `pci.ids` `2026.10.31` for the others (task 46.23, record 21 section 3.3); [linux] `pci.c` class entry |
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
NHI makes the start inert with a log line, since the firmware there
differs - except on the Falcon, Alpine and Titan Ridge rows, which section
19 (task 45.21) gives its Thunderbolt-host profile.

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
   section 3.1's named AMD hosts among them; ASMedia; others), and Intel
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
(decision U12: `U4Desc` = `xHCI98 USB4 Host Router`; from 3.2.0.0 the
id lines name their products, `xHCI98 USB4 Router (AMD Pink Sardine)` and
so on, and the class line is `xHCI98 USB4 Router`, roadmap-usb4 task 43.73,
record 21 section 3.3). Task 46.23 adds AMD's other integrated hosts'
lines the same way, one string per processor (record 21 section 3.3).
The description
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
  | ROP_CMPLT 33, POP_CMPLT 34, ASYM_LINK 39 | consumed silently: the Guide forbids an acknowledgement (7.7.2) |
  | LINK_RECOVERY 38 | never acknowledged, as above; since task 45.20 it queues a port check, and since task 45.56 that port's recovery check too (section 6.2.3 item 4 part 4) |

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

### 5.4 Ring interrupts (task 45.22)

Added 2026-10-29 for `roadmap-usb4.md` task 45.22. The idle wait of this
profile is the ICM client's (record 21 section 4.4), and the task designs
one interrupt for both in record 21 section 4.5: RX ring 0's Receive Data
Buffer cause ends the thread's idle wait early, the timed wait stays at
its period, requests stay busy-polled (section 5.2), and every failure
returns to polling. Designed as that section's companion; **coded on
2026-10-30**, off by default (record 21 sections 4.5.10 and 4.5.11), the
wake of this profile being `TbtU4RingWake`. What this profile adds:

- **One cause covers its events.** A hot-plug packet and a notification
  arrive on RX ring 0 (section 5.1), so the interrupt wakes the thread for
  each, and the wake runs only the RX ring 0 drain (record 21 section
  4.5.1): a hot-plug packet's port check of section 7.1 is queued and runs
  on the thread as today, and so do the router operations and every wait
  of section 5.2. The idle tick's port scan is not run by an interrupt
  wake: its control reads' responses would raise the interrupt again and
  the scan would never stop. A chain is
  enumerated hop by hop, each router's own hot-plug packet waited for, so
  the gain on this profile is up to one idle period per hop.
- **The maintenance pass keeps its deadline, because this profile hangs
  work on it.** The idle tick's port scan (section 7.1 item 2) and the
  presence sample that anchors Stor98's latency budget (section 10.3,
  "normally no older than one idle period") run on the maintenance pass
  only, when its deadline is due (record 21 section 4.5.3), never on an
  interrupt wake; the wait is computed to that deadline, so no scan or
  sample comes later than today's, and an interrupt wake neither runs nor
  postpones them.
- **A USB4 host need not wire INTx**: a PCIe host router shall support MSI
  or MSI-X, and INTx emulation is optional (Spec 12.5). Whether the
  ASM4242, the AMD hosts and the Intel integrated hosts wire it is not
  read here (`usb4-emu`'s `CONTROLLER-CATALOG.md` records INTx pin A on
  the captured Alpine Ridge LP and Alder Lake functions [obs there], and
  nothing for the others). On 98 SE, ME, 2000 and XP, which have no MSI, a
  host without INTx has no interrupt resource and polls as today; a host
  whose pin is advertised but not delivered fails record 21 section
  4.5.5's self-test and polls.
- **The status contract is the host's**: the cause is opened through
  Interrupt Mask Set and closed through Interrupt Mask Clear, and the
  status is cleared by the service routine's read of every status dword
  or, where Disable ISR Auto-Clear is set, by Interrupt Status Clear
  (record 21 section 4.5.2). The Intel integrated hosts (vendor `8086`)
  take Intel's contract, the mask register written directly, as the
  ring code already masks them (`TBT_HW_INTEL_INTERRUPTS`, `tbt_hw.h`;
  record 21 section 4.3). The arm also writes ITR[0], which section 4.4
  step 1 leaves untouched while polling.
- **The emulator**: `usb4-host`'s generic and ASM4242 profiles implement
  every register record 21 section 4.5.2 names, INTx level-triggered
  while any unmasked status bit is set, one MSI, and an `intx=off`
  property that removes the pin (`usb4-host.c` [emu]), which is what the
  q35 legs of record 21 section 4.5.9 use; a lost-interrupt injection for
  the NHI does not exist there yet.
- **Containment** (section 7.7) takes the interrupt down on entry (record
  21 section 4.5.6) and up only on the recovery's return to ACTIVE; the
  probing passes of CONTAINED keep their own once-a-second period.

## 6. The connection manager

### 6.1 The host router

- **Read** ROUTER_CS_0 to 4 at route 0 (Guide 3.2.1 step 1): vendor and
  device, Upstream Adapter, Max Adapter (refused at 64 or more), depth,
  TopologyID Valid (ROUTER_CS_3 bit 31), USB4 Version.
- **TopologyID Valid clear**: enumerate it in one write of ROUTER_CS_1 to 4
  (depth 0, TopologyID 0, Valid, Notification Timeout 255, CMUV by the
  router's version, section 6.2.1), then
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

Until `3.3.0.0` it wrote CMUV 1.0 to every router, version 2 routers
included, and enabled none of version 2's features. From task 45.20 a
USB4 version 2 router - Barlow Ridge as a host or in a device, or any
router whose USB4 Version has major 2 - is run as a version 2 one, and
this connection manager is a Ver. 2 Connection Manager towards it (Spec
section 1's terms). This section is the whole of what that means; the
sections it names carry the detail. Every router is decided on its own:
a domain may mix a version 2 host with version 1 or Thunderbolt 3 device
routers, and a feature that needs both ends of a link (section 6.2.2
items 6 and 7) is used only where both are version 2 routers.

#### 6.2.1 The router's version, and the CMUV written or found

1. **The router's own version** is ROUTER_CS_4 USB4 Version bits 31:29,
   the major alone (section 3.5; section 6.4 step 3): 0 a Thunderbolt 3
   router, 1 version 1, 2 version 2, 3 to 7 reserved (UNSUPPORTED).
2. **Enumerated here.** The one enumerating write of ROUTER_CS_1 to 4
   (Spec 6.7 and 8.2.1.1, the enumeration rules; Guide 3.2.1
   step 4) carries CMUV (ROUTER_CS_4 bits 15:8) 2.0 (`20h`) on a
   major 2 router and 1.0 (`10h`) on a major 1 one, as the Guide's step 4
   says and as [linux] `switch.c` `tb_switch_configure` does
   (`ROUTER_CS_4_CMUV_V2`). A Thunderbolt 3 router keeps the field as
   read (section 8). Nothing else in that write changes, and CMUV is
   never written again while TopologyID Valid is set (Spec 8.2.1.1: a
   connection manager "shall not change" it; Guide 3.2.1 note).
3. **Preserved** (TopologyID Valid found set, section 9.2): CMUV is read
   and kept, never written. `10h` on a major 2 router (a version 1
   predecessor enumerated it) is logged `u4.cmuv.v1`; any value other
   than `10h` or `20h` on a USB4 router is logged `u4.cmuv.odd`. Neither
   changes section 9.2's adoption.
4. **What keys the version 2 obligations.** This driver is a Ver. 2
   Connection Manager for the whole domain while `XhciCmUsb4V2` (item 5)
   is 1. Section 6.2.2's obligations then key on the router's own
   version (item 1), as the Specification and the Guide state them
   (Spec 6.6, 7.3.1.3 and 11.1.1.1's notes and Guide 5.3.1 step 4 and
   7.7.1 step 1 all name the routers' version or the capability's
   support, never the CMUV): a router is a **version 2 router** here
   when its USB4 Version has major 2, whether this driver enumerated it
   or adopted it and whatever CMUV a predecessor wrote. The CMUV of a
   preserved router decides only how that router itself behaves (Spec
   8.2.1.1); a version 2 router a version 1 predecessor enumerated
   (`10h`) is open on the bench (section 16). Reading a version 2
   register stays keyed on the USB4 Version, as section 6.6's TMU-Off
   predicate is now.
5. **`XhciCmUsb4V2`** (REG_DWORD, section 12), absent meaning 1, written
   at 1 by the INFs since task 45.50 (record 21 section 8.1), a bench switch
   like `XhciCmAsmediaPcie7Credits`. 0 makes
   this driver a version 1 connection manager again, the `3.3.0.0`
   behaviour: CMUV 1.0 to every router it enumerates and none of section
   6.2.2's version 2 obligations (items 3, 5's adoption rule, 6 and
   7), for a bench comparison or a
   router that misbehaves as version 2; item 4's two-dword path writes
   and section 6.2.3 item 4 stay, being right for both. A preserved router keeps its CMUV
   whatever the value. (Section 15, U30, confirmed by the owner on 2026-10-29.)

#### 6.2.2 What a Ver. 2 Connection Manager owes, router by router

Every obligation the Spec and the Guide put on a Ver. 2 Connection
Manager, or on a connection manager of a version 2 router, and what this
one does:

1. **Router Ready** (Guide 3.2.1, after the enumeration): a version 2
   router announces it with a ROP_CMPLT notification, Event Info `01h`;
   the Guide allows polling ROUTER_CS_6 Router Ready instead, which this
   driver does, as for version 1 (section 6.4 step 4). **Configuration
   Ready** (Guide 3.2.3 step 3, Event Info `02h`) and every **router
   operation** (Guide 9.1, Event Info `00h`; Spec 8.3.1 step 6)
   likewise: polled. ROP_CMPLT, POP_CMPLT, LINK_RECOVERY and ASYM_LINK
   need no acknowledgement (Spec 6.6) and are counted by code (section
   5.1's silent class; LINK_RECOVERY also delivered and acted on,
   section 6.2.3 item 4); a version 2 router sends many more of
   them, which the per-code counter shows and nothing else reads.
2. **Notification acknowledgements** (Spec 6.6): a Ver. 2 Connection
   Manager acknowledges PCIE_WAKE, DP_CON_CHANGE and DPTX_DISCOVERY as
   well as the version 1 list. Section 5.1 already acknowledges all
   three and then ignores them (`TbtCtlClassify`, `TBT_NOTE_ACK_IGNORE`),
   on every router; nothing changes.
3. **The Sequence bit in Notification Packet capability** (Spec 6.6's
   note: a Ver. 2 Connection Manager "shall enable" it when the router
   supports it; Guide 7.7.1 step 1; Spec 8.3.1.3.2.2, 8.3.1.3.3). On a
   USB4 router (version 1 or 2: the duty is the connection manager's and
   is conditioned on the capability's support, not on the router's
   version), after section 6.4 step 8's Get Capabilities and
   before its Buffer Allocation Request:
   1. Get Capabilities answered successfully and reporting the
      capability (index 2) supported and not enabled: **Set
      Capabilities** (opcode `32h`, Spec Tables 8-53 to 8-55: Capability
      Index 2 in Metadata bits 7:0, Enable Capability bit 8 set, no data
      dwords), then Get Capabilities again as the read-back. The
      read-back's enabled bit is the router's capability state; until
      it is read, the state is PENDING, so the router's notifications
      wait exactly as section 6.4 says of one whose first answer has not
      come (no acknowledgement in either form; each is acknowledged
      once the state settles, in the form it settles). A Set answering
      Status 1 or Not Supported, with a read-back showing the capability
      still disabled: plain acknowledgements, logged `u4.caps.seq-refused`.
      A Set or read-back that fails, times out or finds the router
      operation-blocked: the state is UNKNOWN, with section 6.4's
      consequences (no acknowledgement in either form, no Buffer
      Allocation Request), logged.
   2. Already enabled (a preserved router): adopted, as now.
   3. Not supported, or Get Capabilities answering Not Supported:
      plain acknowledgements, as now.
   4. A preserved router with the capability supported and
      disabled gets the same Set, read-back and PENDING hold: the
      obligation is the connection manager's, whoever enumerated the
      router.
   A Thunderbolt 3 router is never sent one (section 8 runs it
   without Get Capabilities), nor any router while
   `XhciCmUsb4V2` is 0.
   **The other capabilities** of Table 8-51 are left as found and never
   enabled: Hot Plug Failure Indication (index 1; the Guide's "if
   desired"; an ERR_PLUG from a router where a predecessor enabled it
   is handled as now), Buffer Allocation Per USB4 Port (index 3;
   honoured where found enabled, section 6.4), Notification Retry Limit
   (index 4) and CLx Latency Tolerance (index 5, no CLx here).
4. **Path entries written as both doublewords** (Spec 8.2.3.4: a
   connection manager writing a path "shall issue a single Write
   Request" of both; only a Version 1 Connection Manager "may" tear a
   path down by writing the first alone; Spec 6.6's note points the
   Sequence bit at Set Capabilities, 8.3.1.3.3). Every PATH_CS write this
   driver makes - building (section 6.8), the teardown's invalidation
   (section 7.3 step 1.2) and the explicit cleaning (section 9.3) - is a
   single two-dword write, on every router whatever its version: the
   invalidation writes PATH_CS_0 with Valid cleared and PATH_CS_1 as it
   was just read, bit 29 (RsvdZ) written 0 and bit 28 (Pending Packets,
   RO) as read. The single-dword form, which section 7.3 used, is
   retired; for a version 1 router the two forms are equivalent.
5. **ROUTER_CS_5 after Configuration Valid** (Spec 8.2.1.1, the rules
   after the Router Configuration Space fields: a Ver. 2 Connection
   Manager "shall not change" CNS, PTO, UTO, HSGT and HCO once CV is
   set). On a router this driver enumerates, section 6.6 step 1 writes
   them once before CV and nothing writes them after it; the teardown
   (section 7.3) leaves ROUTER_CS_5 alone. **A preserved USB4 router
   found with CV set** (version 1 or 2: the rule binds the connection
   manager) is not configured again: today a
   preserved router reading CV 1 without PTO goes through section
   6.6's sequence again (`tbtU4ConfigValid` from the CONFIGURING state,
   whose read-modify-write would set PTO and UTO and clear HCO under
   CV), which a Ver. 2 Connection Manager may not do. On such a router
   ROUTER_CS_5 is read and never written: PTO clear means no PCIe
   tunnel can be built, so the port is HELD (section 9.2, logged
   `u4.cv.found`), which `XhciCmPreOsCleanup` 1 then cleans; UTO clear
   means no USB3 tunnel on that port, the PCIe tunnel unaffected. With
   `XhciCmUsb4V2` 0 today's path stands. Enumerated State PCIe Wake
   (Guide 3.2.3, a version 2 router) stays 0: this driver arms no wake
   (section 11).
6. **The TMU** (Spec 7.3.1.3's note: when both routers of a link are
   version 2 and support the TMU, a Ver. 2 Connection Manager "shall
   enable" Enhanced Uni-Directional Time Sync; Guide 7.4.3): on a link
   whose two ends are both version 2 routers with a TMU (neither
   reporting Time Synchronization Protocol Not Supported), the TMU is
   configured in **HiFi Enhanced Uni-Directional** mode whenever this
   driver enumerates the device router, whether or not a USB3 tunnel
   follows: the note sets no tunnel condition, so decision U16's
   USB3-only rule is for the other links. It is configured before CV
   (Guide 7.4.3: a version 2 connection manager sets the upstream
   facing port's TMU fields before CV), after Disable Time Sync at
   connection (section 6.6 TMU item 1), and stays for the life of the
   connection: section 6.6 TMU item 6's disable runs at unplug, not
   when a USB3 tunnel goes. A failure is section 6.6 TMU item 5's (the
   TMU quarantine, logged, no USB3 tunnel, the PCIe tunnel
   proceeding). A preserved link is left as found (section 6.6 TMU item
   2). On every other link HiFi-Bi, only for a USB3 tunnel, as now.
7. **PCIe Extended Encapsulation** (Spec 11.1.1.1's note: if the routers
   on both ends of a link support version 2, a Ver. 2 Connection
   Manager "shall" set ADP_PCIE_CS_1 Extended Encapsulation, bit 0, to
   1, otherwise to 0; Guide 5.3.1 step 4: when both routers' USB4
   Version is 2.0 or higher). On a PCIe tunnel whose host PCIe Down and
   device PCIe Up adapters are both on version 2 routers, EE is set to 1
   on both adapters before either Path Enable (Spec 8.2.2.7: before the
   Path Enable bit is set), as the step after the four path entries in
   section 6.8; read back, and a read-back of 0 or a failure leaves the
   tunnel FAILED with section 6.8's unwinding. Every teardown of a PCIe
   tunnel (sections 7.3 step 1, 9.3 step 1, 10.4), Guide 5.3.2 step 3:
   after the Path Enables are 0 and before the paths are invalidated,
   the EE of each PCIe adapter still reachable on a router whose USB4
   Version has major 2 is read, and written 0 if it reads 1 - a
   predecessor's tunnel included, since a predecessor may have set it.
   On a tunnel this driver builds with either end a version 1 router, EE is not
   written; a router this driver enumerated starts with EE 0 (Spec
   8.2.2.7, its default). A preserved tunnel keeps EE as found while it
   stands, and section 9.2's checks do not read it. [linux] `tunnel.c` `tb_pci_set_ext_encapsulation` sets EE
   only when the link is also Gen 4; this record follows the
   Specification and the Guide, which set no speed condition, and
   section 16 keeps the difference open for the bench.
8. **Bandwidth** (Spec 5.3.3.1's note; Guide 6.2.4: at least 1.5 Gb/s to
   each PCIe and USB3 path): with one PCIe path at weight 1 and one USB3
   path at weight 2 in priority 3 (sections 6.8 and 6.9), and section
   6.9 step 2 keeping 1.5 Gb/s off the USB3 allocation for PCIe, it is
   met on every link speed for the tunnels this driver accounts. Not
   for a port carrying a preserved DisplayPort tunnel: DP paths sit in
   a higher priority group (Spec 5.3.3.1, strict precedence over
   priority 3), their consumption is not accounted here (section 6.9
   step 2 already builds no USB3 tunnel beside one), and on a 10 Gb/s
   single-lane link they could leave a new PCIe path less than 1.5
   Gb/s. A new PCIe tunnel beside a preserved DP tunnel is still built,
   as today, logged `u4.bw.dp-unaccounted`, and the floor there is not
   verified: decision U32, (a), the owner's on 2026-10-29.
9. **No Host Interface Reset** of a version 2 host router (Spec Table
   12-14's recommendation): already so (section 2.2, section 4.4).
10. **USB3 Gen T** is not supported: a USB3 Gen T adapter (Spec Table
    8-10) is never used, so the Guide 3.2.1 duty to read a device's
    Preferred Single Data Path DROM entry, which binds a connection
    manager that supports Gen T, does not apply. A version 2 device that
    prefers Gen T may then expose both its PCIe Up and Gen T adapters
    (Guide 3.2.1's note on path preference); this driver builds the PCIe
    tunnel, as for any device.
11. **Not used**, as before: the asymmetric link transitions (section
    6.2.3 item 3), Initiate Gen 4 Link Recovery (section 6.2.3 item 4
    part 8), CLx, DisplayPort, inter-domain links, the version 2 sleep
    flow (section 11). Gen 4 Link Recovery itself is enabled since task
    45.56 (section 6.2.3 item 4).

#### 6.2.3 Gen 4 and asymmetric links

A version 2 router's USB4 port comes up (Guide 7; Spec 4.2.2) as a Gen 4
link, symmetric (x2) or asymmetric; or as Gen 2 or Gen 3 one- or
two-lane links exactly as a version 1 one. Gen 4 has no single-lane form
and no lane bonding: its two lanes are aggregated by link
initialisation (Spec 4.2.2), Target Link Width and Supported Link Widths
are valid for Gen 2 and Gen 3 only, and Lane Bonding written on a Gen 4
link clears itself (Spec 8.2.2.3, Table 8-12).

1. **The link is classified** from the host's lane 0 LANE_ADP_CS_1:
   Current Link Speed (bits 19:16: `8h` Gen 2, `4h` Gen 3, `2h` Gen 4)
   and Negotiated Link Width (bits 25:20: `01h` x1, `02h` symmetric x2,
   `04h` asymmetric with 3 transmitters, `08h` asymmetric with 3
   receivers), both valid only with the link active (Spec 8.2.2.3), and
   the far end's lane 0 for the asymmetric pairing:

   | Speed | Width | Class |
   |---|---|---|
   | Gen 2, Gen 3 | x1, x2 | section 6.5 as now |
   | Gen 4 | x2 | **Gen 4 symmetric**: bonded; no bonding, no lane write |
   | Gen 4 | 3 Tx at one end, 3 Rx at the other | **Gen 4 asymmetric**: bonded for paths; item 3 |
   | Gen 4 | x1, the two ends not a Tx/Rx pair, or a reserved width | not usable: the port FAILED, no lane write |
   | reserved speed, or a read failing | unknown: no action, re-read at the next check |

2. **Section 6.5 on a Gen 4 link.** Step 1's "already bonded" reads the
   width as above, so a Gen 4 link of either form is bonded without a
   bonding. Lane 0 still Training or Lane Bonding (Adapter State 1):
   waited as step 5 waits (500 ms), with no write. Steps 2 to 6 never
   run on a Gen 4 link: no Target Link Width, Lane Bonding or Lane
   Disable is written, and `XhciCmLaneBonding` 0 has no single-lane
   route to take (logged `u4.bond.gen4`, the link used as bonded); a
   user who wants a single lane has no Gen 4 equivalent here. Step 7
   (USB4 Port is Configured) as now. A version 2 router's Gen 2 or Gen
   3 link is bonded as a version 1 one's (Guide 7.1 is titled Gen 2 or
   Gen 3 only).
3. **Asymmetric links are never initiated.** The Guide leaves the
   stimulus to the connection manager (7.2's note); Linux transitions a
   link only for DisplayPort bandwidth above a threshold ([linux] `tb.c`
   `tb_configure_asym`, `asym_threshold`), and this driver tunnels no
   DisplayPort; a PCIe and a USB3 tunnel fit a symmetric 80 Gb/s link.
   PORT_CS_19 StartAsymmetricFlow and LANE_ADP_CS_1 Target Asymmetric
   Link are never written. A link found asymmetric - a preserved one, or
   one a router brought up that way - is used as found: paths are built
   over lane 0's path configuration space as on any bonded link (Guide
   7.1), and it is not transitioned back to symmetric, which would need
   CLx disabled and Guide 7.2.2's flow on a link that may carry a
   preserved tunnel (section 15, U31). An ASYM_LINK notification is
   counted and changes nothing; a later width reading that differs from
   the one taken when the port came up is section 7.1's re-check, not a
   teardown by itself.
4. **Gen 4 Link Recovery** (Spec 4.4.7; Guide 7.6; task 45.56). With
   PORT_CS_19 Enable Gen 4 Link Recovery (ELR, bit 31) set on both ends
   of a Gen 4 link, an uncorrectable error on it is recovered by the two
   routers with their configuration and states preserved, and the router
   with the Downstream Facing Port sends LINK_RECOVERY once the link is
   back; with the bit clear at either end it is a disconnect (Spec
   4.4.7), handled as section 7 handles any. Guide 7.6 lets a connection
   manager set the bit "at both ends of the Link when the Link is in
   CL0", and after each LINK_RECOVERY obliges it to verify "that all
   tunneled protocols using the recovered Link are in an operational
   state". Since task 45.56 this driver sets it, behind
   `XhciCmGen4LinkRecovery` (section 12, 1 by default). Eight parts:
   1. **Where.** Only on a depth-1 link this driver runs: the port UP or
      FAILED with its device router present, the link classified Gen 4
      (item 1, symmetric or asymmetric), both routers of USB4 major
      version 2 and neither a Thunderbolt 3 router, no preserved
      DisplayPort tunnel on the port (section 9.2's `DpPreserved`: a
      tunnelled protocol this driver cannot verify after a recovery),
      `XhciCmUsb4V2` 1 and `XhciCmGen4LinkRecovery` 1. Never on a HELD,
      UNSUPPORTED or barred port, a port whose departed router's drain
      is owed (section 7.7), or a link below depth 1 (record 34's kids):
      those links keep the bit clear, a disconnect on an error, as
      before.
   2. **When and how.** At the idle tick, after the port's own check
      (section 7.1) has found it unchanged, and as the port's own
      sequence (section 7.4, as the owed Set of section 6.2.2 item 3 is
      made): wanted, not yet set on this connection, and not refused on
      it. At both ends lane 0 LANE_ADP_CS_1 must read Adapter State CL0
      (Guide 7.6), the host's also Current Link Speed Gen 4 (the link
      classified again); PORT_CS_18 AsymmetricTransitionInProgress (bit
      24) 0, since Spec 4.2.2.5 and 4.2.2.6 have Gen 4 Link Recovery
      enabled only once a transition completes; and PORT_CS_19
      StartAsymmetricFlow (bit 24) 0, since Table 8-13 forbids a write
      of PORT_CS_19 while it is 1. A reading short of that writes
      nothing, and the next tick tries again. Then PORT_CS_19 is written
      as read with ELR 1, ILR (bit 30) 0 and StartAsymmetricFlow 0, on
      the device router's upstream port first and then on the host's
      port, and each is read back. Both reading ELR 1: set
      (`u4.elr.set`). A write or read that fails, or a read-back of 0 (a
      router that keeps the bit clear): refused on this connection
      (`u4.elr.refused`) - the bit written 0 again on each end written,
      nothing tried again until a new connection (a re-plug, or the
      reconnect after a disconnect).
   3. **What is owned.** Per end, from its write on, the driver records
      that the end may hold its 1, until a read-back shows the bit 0
      there: the device router's end is forgotten at a teardown, since
      the router's reset returns it to its default (Spec 4.4.5.2); the
      host's is not, since Spec 4.4.5.2 does not reset the Downstream
      Facing Port's bit at a disconnect, and is cleared by section 7.3
      step 5 (part 6). Set (both ends read back 1) and refused are per
      connection, cleared at every entry to CONNECTING, and set counts as
      not set again after a stop, a suspend or a recovery's commit
      (section 7.7). At the tick a port whose ends may hold the bit but
      that does not hold it as set has it set again where part 1 still
      admits the port and the connection has not refused it (part 2:
      a refusal is never followed by another set on that connection),
      and written 0 where it does not: on both ends of an UP or FAILED
      port, whose router this driver has identified; on a HELD one (a
      port a recovery or a resume left HELD, whose router may not be the
      one recorded) on the host's end alone, the device end's ownership
      kept until the router is identified again (the port UP or FAILED)
      or its departure confirmed (the teardown). The bit is thus never
      left set on the host's end where this driver would not verify a
      recovery, and with the host's end clear an error on that link is
      a disconnect (Spec 4.4.7).
   4. **LINK_RECOVERY** is delivered to the connection manager, never
      acknowledged (Spec 6.6), and queues a port check of the port whose
      path holds the router that sent it (section 7.1; task 45.20), and
      now also that port's recovery check, made at the tick after the
      check, while the port is UP or FAILED with its router present, as
      the port's own sequence, whether or not this driver set the bit (a
      predecessor's below depth 1, part 7). The check reads only:
      1. The link: the host's lane 0 LANE_ADP_CS_1 read, Current Link
         Speed and Negotiated Link Width logged (`u4.elr.link`); a width
         or speed other than the one taken at the connection is section
         7.1's re-check, logged, never a teardown by itself (item 3).
      2. Each tunnel this driver has up on the port, by section 9.2's
         reading (`tbtU4Follow`): both protocol adapters' Path Enable and
         the four path entries of the tunnel, at the lane HopIDs
         recorded for it. The PCIe tunnel then has section 7.1 item 4's
         predicate at every tick as before (Path Enable, the LTSSM live
         in L0, L1, Recovery or Recovery.Idle, a transitional state
         watched for 2 s), Guide 7.6's ADP_PCIE_CS_0 reading.
      3. A USB3 tunnel's operational state, Guide 7.6's
         ADP_USB3_GX_CS_4 Port Link State, on the host's USB3 Down
         adapter: operational with USB3 Link Valid 1 and the state U0,
         U2 or U3; anything else (Polling, Recovery, Hot Reset, Resume,
         RxDetect, Inactive, Disabled, a reserved state, or Link Valid 0)
         keeps the check owed and is read again at the ticks for up to 2
         s from the check's first try. Still not operational then, it is
         recorded (`u4.elr.usb3.down`, its state) and the tunnel kept:
         that state is the USB3 link's own, which the USB3 link layer and
         the host's xHCI driver recover (an Inactive link by a warm
         reset, a disabled port by its own port reset), and which a
         teardown of the tunnel would not mend.
      4. The TMU, on a link whose TMU this driver configured: section
         7.7's as-left reading (`tbtU4TmuAsLeft`), logged only
         (`u4.elr.tmu`).
      Every tunnel whole and operational: kept (`u4.elr.ok`). A PCIe
      tunnel whose reading is not whole - an entry invalid or leading
      elsewhere, a Path Enable clear - is section 7.1 item 4's lost
      tunnel, with its full loss sequence (the fabric, the statement, the
      PCI removal; the port FAILED for PCIe until a re-plug, its USB3
      tunnel kept). A USB3 tunnel whose paths are not whole is torn down
      alone in the orderly form (Guide 5.5.2, section 7.3's USB3 steps
      with its bandwidth released) and not built again on this
      connection. A register that does not read leaves the check owed
      for the same 2 s; after that it is given up (`u4.elr.unread`) with
      nothing torn down, section 7.1's checks standing. A LINK_RECOVERY
      drained during the check itself (at a safe point of a loss
      sequence it runs) keeps the check owed afresh, so that a recovery
      after the check's readings is checked too. A kid's PCIe
      tunnel through the recovered link has its own check at every tick
      (record 34 section 6.3), unchanged.
   5. **A lost notification.** Where the transport dropped notifications
      (`NotesLost`), every port with the bit set owes the recovery
      check, since a LINK_RECOVERY may have been among them.
   6. **The clears.** The bit is written 0 (PORT_CS_19 as read with
      bits 31, 30 and 24 0, then read back) on the host's port at
      section 7.3 step 5 where this driver may have set it, retried at
      the tick of an EMPTY port while it fails, never in the way of a new
      connection; where part 3 finds it no longer admitted; and on every
      link where it may be set before this instance stops (section 11:
      stop, remove, disable, and the operating system's suspend, Guide
      8.2.1), since no connection manager then runs to verify a recovery
      - not on a contained instance (section 7.7), which writes nothing;
      the ownership it keeps is reconciled by part 3 after the
      recovery's commit. Each of these clears follows part 3's identity
      rule: both ends, device end first, of an UP or FAILED port; the
      host's end alone of a HELD one, its device end's ownership kept
      across the stop or the suspend until the router is identified
      again or its departure confirmed. Every write of PORT_CS_19 this item makes, the clears
      included, is withheld while StartAsymmetricFlow reads 1 (Table
      8-13): a clear so withheld or failing stays owed where a tick
      retries it, and is logged (`u4.elr.clear.failed`) where the stop
      goes on without it.
   7. **A predecessor's bit.** Section 9.2's adoption clears it on both
      ends of every preserved link it walks, at any depth, whatever
      `XhciCmUsb4V2` and `XhciCmGen4LinkRecovery` say (task 45.20; a
      failing clear, one withheld for StartAsymmetricFlow included, holds
      the port); on a depth-1 link that qualifies, the tick then sets it
      again under this driver's own rule. A link below depth 1 whose
      clear failed stays as the predecessor left it, and its
      LINK_RECOVERY reaches the depth-1 port's check and recovery check,
      its kid's own check standing.
   8. **Never** written: Initiate Gen 4 Link Recovery (bit 30), which
      would start a recovery; StartAsymmetricFlow 1 (item 3). Guide
      8.3.2.4's margining and 8.3.2.5's CLx notes do not apply, since this
      driver runs neither.
5. **The raw bandwidth** of the link (Guide Table 6-2), used by section
   6.9 step 2: Gen 2 x1 10, x2 20; Gen 3 x1 20, x2 40; Gen 4 symmetric 80
   Gb/s each way; Gen 4 asymmetric 120 one way and 40 the other, of
   which section 6.9 takes the lower, 40, since a USB3 tunnel's
   allocation is both ways. Computed per lane and doubled for x2 (Gen 4
   at 40 per lane), the asymmetric widths taken as one lane's 40.
6. **Credits.** A lane adapter's Total Buffers (ADP_CS_4) is read when
   section 6.8 builds a path, after the link's width is final, as now;
   a Gen 4 link's counts are whatever the router reports there ([linux]
   `switch.c` `tb_port_update_credits` re-reads them after a width
   change for the same reason).

#### 6.2.4 What the code changes, and the vectors

For the code task after this section converges (none is written with
it), in `src/cm/tbt_u4cm.c` unless named: `tbtU4Enumerate` (item 6.2.1
2), the adoption path (CMUV read and logged, item 6.2.1 3; the CV-found
rule of 6.2.2 item 5 beside the `TBT_RTR5_CV | TBT_RTR5_PTO` test that
now calls `tbtU4ConfigValid`; the Enable Gen 4 Link Recovery clear of
6.2.3 item 4), `tbtU4ConfigValid` (the enhanced TMU on a v2-v2 link
whatever the USB3 decision),
`tbtU4GetCapabilities` and a new `tbtU4SetSequenceBit` (6.2.2 item 3),
`tbtU4Invalidate` and the preserved-path invalidation beside it (6.2.2
item 4), the TMU enable and disable and `tbtU4TmuAtConnect` (6.2.2 item
6, section 6.6), the PCIe tunnel's build and teardown (6.2.2 item 7),
`tbtU4Bond`, the adoption's lane step and `tbtU4LanesAsLeft` and the
UP check that reads the width (6.2.3 items 1 and 2), the LINK_RECOVERY
class and delivery in `tbt_ring.c` and the port check it queues (6.2.3
item 4), `tbt_regs.h` for CMUV
2.0, Set Capabilities, EE, Current Link Speed and the asymmetric widths,
and the registry value beside `XhciCmLaneBonding` in `cm_reg.c` and
`cm_vals.c` (section 12). Host vectors in
`test/test_u4v2.c`, a suite of its own beside `test_u4cm` over the same
fabric (section 14.1), against a version 2 router in the fake
fabric: CMUV `20h` written to a major 2 router and `10h` to a major 1
one in the same domain, and `10h` everywhere with `XhciCmUsb4V2` 0; a
preserved router's CMUV read and never written, each of item 6.2.1 3's
cases; the Set Capabilities and read-back on a version 2 and on a
version 1 router reporting the capability (a router a predecessor
enumerated with CMUV `10h` included), none with `XhciCmUsb4V2` 0 nor on a
Thunderbolt 3 one, and each refusal and failure of item 6.2.2 3 with its
acknowledgement form (PENDING then enhanced, plain, UNKNOWN); every
PATH_CS write two dwords long; HiFi Enhanced Uni-Directional on a
v2-v2 link with a USB3 tunnel, HiFi-Bi on a v2-v1 one, and the
disable's order, the enhanced TMU on a PCIe-only v2-v2 link and with
`XhciCmUsb3Tunnel` 0; a preserved v2 router with CV 1 and PTO 0 HELD and
ROUTER_CS_5 never written, a version 1 one too; Enable Gen 4 Link
Recovery cleared on every preserved link at any depth whatever
`XhciCmUsb4V2`, and a LINK_RECOVERY delivered as a port check, never
acknowledged; EE set before Path Enable on a v2-v2 PCIe tunnel and
cleared after it at teardown, never written on a v2-v1 one; a Gen 4 x2
link bonded with no lane write, a Gen 4 asymmetric pair used and never
transitioned, a Gen 4 x1 or unpaired asymmetric reading FAILED; the
USB3 allocation's link bound at 72 and 36 Gb/s less 1.5 for Gen 4
symmetric and asymmetric. `usb4-emu`'s version 2 router model for the
q35 legs is that repository's work (roadmap task 45.20); a Barlow Ridge
on the bench is task 45-E.10.

What the code settled that this section left open (task 45.20's code,
2026-10-30):

- **Get Capabilities on a router enumerated here** is read for the Set's
  decision while `XhciCmUsb4V2` is 1: the router stays at its default
  (plain acknowledgements) unless the answer reports the Sequence bit
  capability supported, when the Set and its read-back decide the state.
  A recovery (section 7.7) classifies by reading alone, so it sends no
  Set: a router this driver ever sent the Set (recorded apart from the
  capability state, since a Set may have applied though its read-back
  failed) has the recovery's answer adopted, and a failing answer leaves
  it unknown rather than at its default; one found with the capability
  supported and disabled is owed the Set, made at the first tick after
  the commit as that port's own sequence (no nested teardown of it, and
  no Set once the port shows removal evidence or a new generation).
- **A Gen 4 link is recognised** from the host's lane 0 Current Link
  Speed (`2h`); the far end's lane 0 is read only for an asymmetric
  width. On a link of two version 2 routers lane 0 must read connected
  before it is classified: one in Training or Lane Bonding is waited for
  up to 2 s with no write, and one that does not come back connected
  leaves the port FAILED with no lane written (`Why` 19, the lane state);
  a reserved speed on an active link goes to section 6.5 as before (no
  evidence of Gen 4). A Gen 4 x1 reading, an unpaired asymmetric one
  or a reserved width leaves the port FAILED with no lane write (`Why`
  23, the link). The read-only checks of an existing link (the resume's
  verification and the recovery's lanes as left) count a Gen 4
  asymmetric width as bonded.
- **CV found** (section 6.2.2 item 5) is read in two places: section
  9.2's discovery, where a router with no tunnel state found with CV set
  and PTO clear is HELD at once (`Why` 22, so `XhciCmPreOsCleanup` 1
  cleans it at start), and the connection of an adopted router, where CV
  set skips section 6.6 (PTO clear: HELD; UTO clear: no USB3 tunnel) and
  section 6.6 runs only once CV is read clear - a read that fails holds
  the port. A discovery whose Enable Gen 4 Link Recovery clear fails
  holds the port too, even with no tunnel state.
- **Enable Gen 4 Link Recovery** is cleared by a write of PORT_CS_19 as
  read with bits 31 and 30 written 0; the discovered links below depth 1
  are cleared on both ends as the walk finds them (the child's USB4 Port
  Capability found by its upstream adapter's own capability chain), a
  failure there logged and the link left as a predecessor's.
- **LINK_RECOVERY** is its own class in `tbt_ring.c` (delivered, never
  acknowledged); the connection manager takes the port from the host's
  Event Info when the host router sent it, else from the route's first
  level.
- **Extended Encapsulation** is cleared on an adapter only once its own
  Path Enable reads 0: where the disable failed, the adapter's enable
  quarantine (section 7.3) clears EE when its retry lifts it. A PCIe
  tunnel built with either end a version 1 router reads EE on each
  version 2 end first and clears it where it reads 1 (an earlier
  teardown's clear that failed, or a predecessor's), building no tunnel
  where it will not clear. It is not
  touched by section 7.7's recovery drains, which section 6.2.2 item 7
  does not list; a rebuilt tunnel sets it again before its Path Enable. A
  clear that fails is logged and the teardown goes on.
- **The USB3 link bound** is one function (`tbtU4LinkBound`), exposed to
  the host suite, since no fabric router reports a USB3 rate the Gen 4
  bound would limit.

#### 6.2.5 Gen 4 Link Recovery: the code and the vectors (task 45.56)

For the code task after section 6.2.3 item 4 converges, in
`src/cm/tbt_u4cm.c` unless named: a new `tbtU4ElrWanted`, `tbtU4ElrSet`
and `tbtU4ElrCheck`, made from `TbtU4Poll` after the port's check and its
kids' service (part 2, part 4); the LINK_RECOVERY branch of
`tbtU4TakeQueues` and its `NotesLost` loop (parts 4 and 5); the loss
sequence of `tbtU4PcieLoss` factored out so that the recovery check runs
the same one (part 4); `tbtU4HostRestore` and the EMPTY tick's condition
beside it (part 6, the host's end, never part of the restore's verdict
that gates a connection); `TbtU4Stop` and `TbtU4Suspend` (part 6, before
`TbtRingStop`, on an instance that is not contained); `TbtU4RecoverCommit`
and the resume's end (the bit counted as not set); per port in
`tbt_u4cm.h` the flags set (both ends read back), each end that may
hold this driver's 1 (part 3), refused, the recovery check owed and its
first try's time; one guarded write of PORT_CS_19 for every set and
clear of this item, the adoption's `tbtU4ElrClearAt` included (part 6);
`TBT_PORT19_SAF`, PORT_CS_18's AsymmetricTransitionInProgress and the
USB3 Link Valid and Port Link State fields of ADP_USB3_GX_CS_4 in
`tbt_regs.h`; `TBT_U4_CONFIG.Gen4LinkRecovery`; the value
`XhciCmGen4LinkRecovery` beside `XhciCmUsb4V2` in `cm_vals.c`,
`cm_vals.h`, `cm_reg.c` and `cm_nhi.c` (its start note and the counters),
both `xhcicm` INFs (`HKR,,XhciCmGen4LinkRecovery,0x00010003,1`), the INF
gate's value table and self-test rows, `docs/using/tuning.md` and record
21 section 8.1's table. Counters: enabled, refused, cleared at a stop or
suspend or teardown (and its failures), recovery checks, tunnels kept,
PCIe and USB3 tunnels lost to a check, checks given up.

Host vectors in `test/test_u4v2.c` against the fake fabric's version 2
routers: the bit set on both ends of a Gen 4 link, symmetric and
asymmetric, device end first, each write PORT_CS_19 as read with bit 30
0, only once lane 0 reads CL0 at both ends and StartAsymmetricFlow 0;
none while lane 0 reads short of CL0, AsymmetricTransitionInProgress or
StartAsymmetricFlow reads 1 at either end (then set at a later tick);
never with `XhciCmGen4LinkRecovery` 0, with `XhciCmUsb4V2` 0, on a v2-v1
link, a Gen 3 link of two version 2 routers, a HELD port, a port with a
preserved DisplayPort tunnel, or at depth 2; a port left HELD with the
bit owned: its host end cleared at the tick, its device end left owned;
an owned port no longer admitted: both ends cleared;
a read-back of 0 refused, both ends written 0 again, no retry on that
connection and a retry after a re-plug; a LINK_RECOVERY with every
tunnel whole keeping both (no write, no removal); one with a PCIe entry
gone running the loss sequence (FAILED, the removal queued, the USB3
tunnel kept); one with a USB3 Path Enable clear tearing the USB3 tunnel
down alone, the bandwidth released, not rebuilt; a USB3 Port Link State
of Recovery read again until U0 (kept, `ok`) and one left Inactive for 2
s recorded with the tunnel kept; an unread register
retried and then given up after 2 s with nothing torn down; `NotesLost`
owing the check; the host's end cleared at an unplug, its failure
retried at the EMPTY tick, a new connection not deferred by it, and a
clear withheld while StartAsymmetricFlow reads 1; both
ends cleared at `TbtU4Stop` and `TbtU4Suspend` (a HELD port's host end
alone) and set again after the resume; nothing written by a contained instance; the 45.20 discovery
clear of a predecessor's bit unchanged, the bit then set again on a
qualifying depth-1 link. The value's own vectors in `test/test_cm.c`
beside `XhciCmUsb4V2`'s. `usb4-emu`'s version 2 model already recovers
a link with the bit set at both ends (LINK_RECOVERY, configuration
kept) and disconnects one with it clear at either end (its
`docs/USB4-V2.md`, `x-link-event=error`); the q35 legs use that.

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
   route, Valid, Notification Timeout 255, CMUV by the router's version,
   section 6.2.1); Router Ready within
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
  state; the operation's answer, or its absence, changes nothing - except
  that, while `XhciCmUsb4V2` is 1, the answer decides section 6.2.2
  item 3's Set Capabilities on any USB4 router, whose read-back then
  sets the state.
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
   25:20, reads x2): nothing to do. A Gen 4 link is never bonded or made
   single-lane: section 6.2.3 classifies it, and steps 2 to 6 do not run
   on it.
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
2. **The TMU**, where a USB3 tunnel will be built, and on every link of
   two version 2 routers (below; section 6.2.2 item 6).
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
driver will build a USB3 tunnel over it and finds it Off - or, whatever
the tunnels, when both its routers are version 2 routers and this driver
enumerated the device (section 6.2.2 item 6) - and never on a link it
preserves:

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
3. **The mode** (Guide 7.4.3), from Off: on a link whose two routers are
   both version 2 routers (section 6.2.1 item 4), HiFi Enhanced
   Uni-Directional, configured whatever the tunnels (section 6.2.2 item
   6, the exception to this section's USB3-only rule)
   (Spec 7.3.1.3's note: "shall"; [linux] `tb.c` `tb_enable_tmu` tries
   the enhanced mode first on every link and falls back where a router
   lacks it); on any other link HiFi-Bi, since CLx is never enabled;
   Linux makes the same choice on a version 1 link without CL1 ([linux]
   `tb.c` `tb_enable_tmu`). Never on two single-lane links (Guide
   7.4.3.1): after bonding, or over lane 0 alone once lane 1 is
   disabled.
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
   upstream then downstream: the device router's upstream adapter first,
   an order from Linux ([linux]), where Guide 7.4.3.4.1 steps 3c and 3d
   name the host router first; the USB4 Specification sets no order
   between the routers, and the owner kept this order for `3.3.0.0` on
   2026-10-26, to be read on the hardware bench, roadmap 44-E.6); or, for
   HiFi Enhanced Uni-Directional, Guide 7.4.3.4.1 step 2 with Table 7-3
   and Spec 8.2.2.2's note: the same downstream parameters, and on both
   lane 0 adapters of the link (Table 7-3 names the downstream facing
   port for the replenish values; Spec 8.2.2.2 Table 8-11 requires
   DirSwitchN above 1 on a port whose enhanced mode is enabled, and
   [linux] `tmu.c` `tb_switch_tmu_enable_enhanced` writes the port
   parameters on both) Replenish Timeout 3125, Replenish Threshold 0
   (TMU_ADP_CS_8), ReplenishN 30 and DirSwitchN 255 (TMU_ADP_CS_9) -
   the HiFi values of Spec 8.2.2.2's note, where Linux uses its MedRes
   set (averaging constant 4, threshold 25, ReplenishN 128) - then
   AdapterTimeSyncInterval 16 and
   Enable Enhanced Uni-Directional Mode 1 on the device's upstream lane
   0 adapter and then on the host's, then Disable Time Sync 0 on the
   device's and then on the host's (steps 2b to 2e, the Guide's own
   order); TSPacketInterval is not written for it (Spec 7.3.1.3: ignored
   in that mode); clear the Time Disruption bit; then 50 ms
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
6. **Disable** on a link this driver configured (a HiFi Enhanced
   Uni-Directional link only at unplug, section 6.2.2 item 6), when its last USB3 tunnel
   is torn down with both routers present: Guide 7.4.3.4.2's branch for
   the mode it configured - HiFi-Bi, the downstream router's
   TSPacketInterval 0; HiFi Enhanced Uni-Directional,
   AdapterTimeSyncInterval 0 and Enable Enhanced Uni-Directional Mode 0
   on both lane 0 adapters - and then Disable Time Sync 1 on both lane 0
   adapters, inside a time disruption. **At unplug**,
   with the downstream router gone, only the host's port remains: its lane
   0 adapter gets EnableUniDirectionalMode 0, on a version 2 host router
   AdapterTimeSyncInterval 0 and Enable Enhanced Uni-Directional Mode 0,
   and Disable Time Sync 1, and
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
| Get Capabilities | `31h` | the inherited capability state of section 6.4 (Capability Index 0); also section 6.2.2 item 3's read-back | routers this driver enumerated: all disabled, whatever the first answer, until a Set Capabilities, after which the read-back sets the state (enabled, plain, or unknown when the Set or the read-back is uncertain, section 6.2.2 item 3); a preserved router: adopted on success, none enabled on Not Supported, unknown on any failure, with section 6.4's consequences |
| Set Capabilities | `32h` | nothing: Status only (Spec Table 8-55); since task 45.20 it enables the Sequence bit capability (section 6.2.2 item 3) | plain acknowledgements; a failure leaves the capability state unknown, as section 6.2.2 item 3 says |

Nothing else is issued, and none of the five is needed for a tunnel.

### 6.8 Paths and the PCIe tunnel

The four path entries ([ref] `program_tunnel_paths`; Guide 5.3.1, Table
5-2; Linux's `tunnel.c` `tb_tunnel_alloc_pci` and `tb_pci_init_path` build
the same shape with HopID 8 at both protocol adapters):

| Entry | Ingress | Output | Attributes |
|---|---|---|---|
| downstream, segment 1 | host PCIe Down, HopID 8 | host lane 0, HopID `hd` | priority 3, weight 1, EFC 1, ESE 0; credits, IFC and ISE as read (but for section 18.8's experiment) |
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
2. The downstream path, then the upstream path; then, where both routers
   are version 2 routers, Extended Encapsulation (section 6.2.2 item 7).
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
   of one of its ports; since task 45.20 also a LINK_RECOVERY
   notification from a router on the port's path (section 6.2.3 item 4),
   which since task 45.56 also queues that port's recovery check (part
   4 there) after its port check.
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
| CONNECTING | section 6.4 (or section 8) running | enumerated: BONDING; an unplug: TEARDOWN; ERR_ENUM, ERR_NUA: UNSUPPORTED (XDOMAIN with `XhciCmNet` 1, design record 35 section 3.3); enumeration failed 20 times: FAILED |
| BONDING | section 6.5 (section 8 item 3 for a Thunderbolt 3 router) | done or single: CONFIGURING for a USB4 router, TUNNELLING for a Thunderbolt 3 router, which has no Configuration Valid handshake (section 8 item 2); the verified fallback failing: FAILED |
| CONFIGURING | section 6.6 | Configuration Ready: TUNNELLING; timeout: FAILED |
| TUNNELLING | sections 6.8 and 6.9, then the PCI side (section 7.6) | the PCIe tunnel up and its tunnel ports re-enumerated, or approval 0 with USB3 handled: UP; failure: FAILED |
| UP | tunnels up, or preserved from before (section 9) | an unplug: TEARDOWN; the PCIe tunnel lost with the router present (section 7.1 item 4): FAILED for PCIe, the USB3 tunnel kept with its resume obligation |
| HELD | a port whose configuration did not verify and showed no removal evidence: found so at start (section 9.2), or a port holding a resume obligation at resume (section 7.6); left exactly as found; a live preserved tunnel on it remains an obligation | an unplug: TEARDOWN of whatever this driver can read of it, host side only |
| FAILED | enumerated, but some step failed; whatever did succeed (a USB3 tunnel, say) stays | an unplug: TEARDOWN; only a re-plug retries |
| UNSUPPORTED | a router this driver will not drive | an unplug: EMPTY |
| XDOMAIN | another domain's router, perhaps another host: design record 35's inter-domain discovery and USB4NET service run on the port (task 45.59, `XhciCmNet` 1) | an unplug: TEARDOWN; no answer to the UUID Request, or a loop: UNSUPPORTED |
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
      disconnects (Spec 4.4.5.2), so normally this is a read. On a version
      2 host router its Extended Encapsulation is then read and written 0
      if set (section 6.2.2 item 7).
   2. PATH_CS_0 Valid cleared on the host PCIe Down adapter's HopID 8 entry
      and on the host lane 0 adapter's `hu` entry, nothing else changed,
      each in one two-dword write (section 6.2.2 item 4), then PATH_CS_1 Pending Packets (bit 28) polled to 0 on each within
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
   Port is Configured, cleared on the host's port; Enable Gen 4 Link
   Recovery, cleared on the host's port where this driver set it, since
   Spec 4.4.5.2 does not return it to its default at a disconnect
   (section 6.2.3 item 4 part 6, task 45.56); Link Credits, which the
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
  attributed, and what answers is a tunnel's far end or comes with the
  link's evidence (section 7.6.4). A port stays pending until it is
  learned or proven down - a teardown whose read-back proved the path
  closed, or the router's removal
  evidence of section 7.1 items 1 to 3; a tunnel-down action alone is not
  proof - and while another port is pending no gain is attributed
  (narrowed for ports found up at start by section 7.6.3). A
  member with no secondary bus blocks learning for the domain, and a
  conflicted port's live tunnel blocks it until that tunnel is proven down
  (orchestrator).
- **The learning's window** (task 44.57; the owner, 2026-10-27: a bounded
  retry with back-off or a terminal state). The window bounds how often
  and how long a pending port is *read*; it never changes whether the
  port is pending. A port stays pending, with everything that follows
  from it above and in section 7.7 part 4's handoff, until it is learned
  or proven down, exactly as before; a port whose window has closed is
  simply no longer tried.
  - **Clock and attempts.** Times are the instance's millisecond counter
    (the one its liveness monitor reads), compared as unsigned elapsed
    time, so a wrap is harmless. An *attempt* is one try to learn the
    port (an `XhciTbtLearn` call, or the same try declined because the
    set cannot be read now), from an idle pass or from the delivery of
    its `TUNNEL_UP`; every attempt that leaves the port pending -
    whatever its outcome (`NOT_GAINED`, `UNREAD`, `NOT_ARMED`,
    `SEVERAL`, `SHAPE`, `INACTIVE`, `CONFLICT`) - counts as a miss.
  - **The schedule.** While less than 1000 ms have passed since the
    window opened, the next attempt is at the next idle pass (never a
    second one in the same pass, a delivery's included). From the
    first miss at 1000 ms or later, the next attempt is a gap later: the
    first gap is `XhciCmPollIdleMs` as read for the instance (50 to
    5000 ms), each later gap twice the one before, every gap held to at
    most 5000 ms. An idle pass makes an attempt only when the gap has
    run out.
  - **The end of the window.** Wherever an attempt is due - at an idle
    pass or at a delivery - when 60000 ms or more have passed since the
    window opened (read just before the attempt), the window closes
    instead: no attempt is made for the port again until a window opens
    again, and `u4.learn.expired` is logged once. The port stays
    pending. What a closed window stops is the reading only: at every
    idle pass a pending port that has a binding by now (a naming at a
    discovery or a recovery after the window closed) has its pending
    mark consumed as `XhciTbtLearn`'s `KNOWN` outcome consumes it, with
    no configuration read, whatever its window.
  - **What opens a window.** (a) The first idle pass that finds the port
    pending with no window open and none closed for its connection
    generation (`TBT_U4_PORT.Generation`, recorded when the window
    opens). (b) Each
    `TUNNEL_UP` of the port that marks it pending (the batch's
    `XhciTbtLearnUp`: not one whose generation's path was already proven
    closed), open or closed before, the same connection generation or a
    new one: a tunnel brought up again (a recovery's rebuild keeps the
    generation) can bring a new function. The reconciliation's marking
    of a port pending never opens or renews a window. A tunnel found up
    at start, or preserved through a recovery, queues no `TUNNEL_UP`, so
    its window is the one (a) opened.
  - **What ends it.** The pending mark cleared - learned, bound,
    released by the reconciliation, or marked down on a path proven
    closed (a teardown's read-back, a recovery's drain) - forgets the
    window and any closure where the mark is cleared, and an idle pass
    that finds the port not pending forgets them too. A window, open or
    closed, recorded for another connection generation than the port's
    now is forgotten at the idle pass that sees it, before (a): a
    replacement connection whose tunnel failed queues no `TUNNEL_UP` but
    may be marked pending again by the reconciliation within the same
    tick, and it gets a window of its own. Time passes for a
    window while the instance is suspended or contained (no attempt is
    made then: section 7.7 attributes nothing after the loss), so a
    window may close during either; a rebuild's `TUNNEL_UP` reopens it.
  - **The citations, as policy.** The first second follows the PCI
    Express Base Specification's Conventional Reset rule (section
    6.6.1: at least 1.0 s before software may judge a function that has
    not returned a Successful Completion to be broken, which Request
    Retry Status completions may extend without an upper limit), and the
    60 s is of the order of [linux] `drivers/pci/pci.c`'s
    `PCIE_RESET_READY_POLL_MS` (60000, v6.12), the time Linux allows a
    function after a reset. Both are chosen bounds for how long a
    function is waited for; neither proves that a later gain is not the
    port's, which is why the port stays pending.
  Why: a tunnel found up at start (section 9.2) whose function already
  answered when the baseline was taken never shows a gain, so its port
  stays pending for the instance's life. Without a window every idle
  pass read each member's secondary bus for it (33 configuration reads a
  member: the bridge's bus numbers and one function-0 probe per device
  number), at the 200 ms default, for as long as the instance ran (task
  44.57, on the `asm4242` model with the ASM2464 enclosure attached at
  power-on, where the host router answers Get PCIe Downstream Entry
  Mapping Not Supported, so no router-named correspondence takes the
  port out of learning). Such a port blocked the learning of every
  other port's tunnel while it was pending; since task 48.43 it does not
  where its function showed at the start reading (section 7.6.3), and
  its eject may close its tunnel by an inference (section 7.6.2, off by
  default).
  Only a router-named or a learned correspondence binds a port;
  `XhciCmTunnelBridge` selects the domain's tunnel-port set and does not
  itself resolve a pending port.
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

**Resume ordering** is record 21 section 6.3's, unchanged, since the
filter is shared. At its own system suspend the NHI marks every active
member held. A D0 IRP reaching a held member's filter is passed down at
once and never retained (task 43.24h: a retained bridge D0 deadlocked
the NT resume). When the NHI's resume has **succeeded** or **failed**,
the NHI clears each member's mark and invalidates the member. The NHI's
resume bound plus 10 s, and the five-minute cap, are logged as
diagnostics only. An NHI that does not resume clears and invalidates at
its removal. Under this profile:

- **What must be restored** is tracked per tunnel, not per port state:
  at suspend the NHI records every tunnel that is up on the domain - each
  PCIe and USB3 tunnel this driver built or preserved, whatever its
  port's state (an UP port; a FAILED port whose PCIe tunnel succeeded
  before its USB3 failed; a HELD port's preserved live tunnel) - as an
  obligation, with its port, generation and HopIDs.
- **Succeeded** means every obligation is met: its tunnel verified by
  section 9.2's reads; or, where the domain lost its configuration across
  the sleep (the device router uninitialised, its link connected again)
  and the tunnel is one this driver built - in this instance, or in an
  earlier instance on the same NHI whose record section 9.4 matched -
  rebuilt through sections 6.4 to 6.9 with the Detect gate of section
  6.8, before the hold is released; or
  its port showing removal evidence of the router (section 7.1 items 1 to
  3: CLd, Lock set again, a changed identity, ERR_CONN), torn down
  (section 7.3), the device being gone and the OS removing it. Section 7.1
  item 4's loss of the PCIe tunnel alone discharges only the PCIe
  obligation, through its own teardown and removal; a USB3 obligation on
  the same port still has to be verified.
- **Failed** is any obligation not met: a rebuild that fails; a
  verification that fails without removal evidence (the port is then
  HELD, untouched); a preserved tunnel that did not survive and that
  section 9.4 did not match to this driver's earlier instance (on a HELD
  port it is never matched), which this driver does not rebuild, its
  configuration being someone else's; the NHI's own resume failing. The failure path is record 21's:
  the member's mark cleared and, with re-enumeration on, an invalidation
  asked of its filter, so the functions that did not come back are
  reported missing (since task 43.24h the same release follows a resume
  that succeeded too); nothing is torn down without removal evidence.
  Decision U15 records this window.
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
  top as the logged bound's margin (no watchdog waits on it since task
  43.24h).

Record 21 section 6.3's conclusion stands for this profile too: no NT
resume is ordered (task 43.24h), and the re-enumeration after the NHI's
resume does not close the window,
so **system suspend with a tunnelled device attached is unsupported in this
release on every target**, and the release notes say so. On Windows 98 no
filter sits on a bridge (record 21 section 6.1) and there is no hold at
all. Task 48.35 designs the sleep with a device attached in section 11.1
(the Guide's sleep entry, the resume after it, and the native hot-plug
port this driver cannot order); until it is coded, this paragraph is what
ships.

#### 7.6.1 AMD's integrated hosts: the ACPI-named source (task 46.23; design, reviewed and revised)

**The gap.** Record 21 section 6.3's source 3 takes a listed root port only
beside an NHI on the root bus. AMD's integrated NHIs are functions behind an
internal bridge: on a Strix Halo machine (record 21 section 3.3, "AMD's
pairing") `158D` and `158E` sit behind `1022:150C`, and the two `150A`
tunnel root ports are on the root bus, siblings of that bridge. Source 3
never applies there, and source 2 would not help: both NHIs share that
parent, so each would claim both ports and record 21's exclusive ownership
would leave all of them inactive. `pci.ids` lists an internal bridge of the
same kind per processor (Phoenix's `14EB`, Strix's `150C`), so the same
layout is expected on every AMD processor `cm_ports.c` names, Pink Sardine
included; it is read on Strix Halo alone. Until this section is built the
set there comes from the router-named source, where the host router answers,
or from `XhciCmTunnelBridge`; with neither there is no re-enumeration after
a hot plug and no safe-eject mark.

**What the platform says.** On that machine each `150A` root port's ACPI
device carries a static `_DSD` (a `Name`, not a method) with the
device-properties UUID `daffd814-6eba-4d8c-8a91-bc9bbf4aa301` and the keys
`usb4-host-interface` (a reference to the NHI's ACPI device),
`usb4-port-number` and `ExternalFacingPort`, in an SSDT (the owner's
capture, 2026-11-03; firmware facts only, nothing tracked). Windows 11 lists
each port as depending on one NHI, and Linux links the first port to `158D`
and the second to `158E` (`acpi.c` `tb_acpi_add_link`, by the
`usb4-host-interface` reference, [linux]). Microsoft's USB4 ACPI
requirements [ms-usb4acpi] define these `_DSD` properties for a USB4 host's
PCIe root ports. So the platform gives, per port, its NHI (the set) and a
USB4 port number, which is no binding here (below).

**Decision.** The direction below (the sources' order, the ACPI-named
source on NT, the port number logged only, position rejected, the 9x route
deferred) is the coordinator's, approved on the owner's delegation
(2026-11-04). It first held the ACPI-named source to the amd64 build; the
owner then chose the x86 build too (2026-11-04, the goal being every system
the package serves), through the routine route of record 32 section
12.8.15.15 (below, "The x86 build").

**Scope.** Every AMD machine with built-in USB4 known here (Rembrandt and
later) is UEFI-only with no CSM and no IA32 UEFI (owner, 2026-11-03): 98 SE,
ME, 2000 and XP cannot start there and 7 in practice cannot; the targets
that can are 8.1 and 10 x64. The sources below are built for NT, in both
builds (the x86 one for any 32-bit NT that starts on such a host, none
known); the 9x route is deferred (below), not rejected.

**The sources for an AMD family, in order** (record 21's exclusive
ownership decides every claim, as now):

1. **The override**, `XhciCmTunnelBridge`, unchanged: the whole set when
   present.
2. **Router-named** (section 7.6, Get PCIe Downstream Entry Mapping,
   operation `30h`), unchanged and first of the automatic sources. Whether
   AMD's host router answers it is unread on NT; on 98 SE without ACPI the
   router answers the connection manager (the no-ACPI reading below), the
   operation's answer not yet logged there.
3. **ACPI-named (new, NT only).** The bridge filter (`cm_filt.c`, role 2)
   on a port whose id is in an AMD family's `RootPortIds` sends
   `IOCTL_ACPI_EVAL_METHOD` for `_DSD` down its own stack after its start
   completes (ACPI is a filter in that stack: Windows 11 lists
   `\Driver\pci`, `\Driver\ACPI`, `\Driver\pci` on `150A`). The answer is
   parsed by a pure, bounded reader (`cm_dsd.c`, host vectors): the
   device-properties UUID, then the `usb4-host-interface` element, a name
   string of at most 128 characters; `usb4-port-number`, an integer, is
   read for the log alone. Any other shape, a second instance of a key, an
   oversize answer or a refused request gives no claim and a log line.
   - **What is compared.** Both sides by ACPI name, never by a PCI
     location derived from ACPI (an `_ADR` gives a device and function, not
     a bus, so a path's `_ADR` chain cannot be held against the live
     topology without a bridge walk this design does not need). The NHI
     reads its own ACPI name, `DEVPKEY_Device_BiosDeviceName`, with
     `IoGetDevicePropertyData` (the routine exported from Windows Vista
     on; the property read so far on 10 and 11), found at run time
     with `CmRoutine` (`cm_oscnt.c`, record 32 section 12.8.15.15): in the
     amd64 image its `MmGetSystemRoutineAddress` import (present from XP
     x64 on), in the x86 image the helper's (below). The filter evaluates
     `_DSD` on Vista and later alone (WDM 6.0, the routine's systems), in
     both builds, and not on `CmRoutine`'s answer, which a lookup failed
     for want of memory would leave NULL for that start alone; where the
     NHI then reads no name (the routine not found), the filter's claim is
     matched by none.
     The filter's reference, normalised (one leading backslash, upper
     case, segments of four characters), must equal the NHI's name
     exactly. A name the NHI cannot read, or the routine absent, gives no
     ACPI-named member for that NHI and a log line. The ACPI namespace is
     one per machine, so equal names are the same device whatever the
     buses and segments.
   - **The x86 build** (the owner's choice, 2026-11-04). The x86
     `xhcicm.sys` is one image for 98 SE, ME and NT, and its import gate
     (`scripts\import-gate\xhcicm-imports.allow`) refuses
     `MmGetSystemRoutineAddress`, Windows 98 lacking it. `CmRoutine` asks
     the helper's channel, minor 1, there (`xhcihlpr.sys`), so no import
     row is added. It returns NULL on 98 SE and ME; where the helper is
     not installed (until 3.4.1.0 `xhci98.inf` alone installed it, so a
     32-bit 8 to 10 system with every xHCI controller on Windows' own
     driver had none, record 32 section 12.8.15.15, "the gap"; from
     3.4.1.0 `xhcicm.inf` installs it too, record 18 section 15.15, so a
     Thunderbolt-only install has it); where the helper answers without
     the routine service; and on 2000 and XP, which do not export the
     routine.
     In each the ACPI-named source is simply absent (on 98 SE, ME, 2000
     and XP the filter evaluates nothing; on 8 to 10 without the helper
     its claim names an NHI that reads no name), and the override and the
     router-named source serve as before. The helper is boot-start and `xhcicm.sys` demand-start, so
     the filter's first ask, which can come before any NHI's start, has
     the answer the NHI's would have; the lookup's answer is kept for the
     load, except a query that failed for want of memory (no IRP, or the
     helper's device not opened for it), which the next caller makes
     again, so this earlier caller cannot leave the handover of record 32
     section 12.8.15 (`108h`) or an NHI without the routine for the load.
     `IOCTL_ACPI_EVAL_METHOD` needs no new import either: it is an I/O
     control code sent with `IoAllocateIrp`, `IofCallDriver`,
     `KeInitializeEvent`, `KeSetEvent`, `KeWaitForSingleObject` and
     `IoFreeIrp`, each an x86 row with no `SITES` limit and Windows 98 SE
     evidence of its own. Its code and the input buffer's layout come on
     x86 from the Windows 2000 DDK's `acpiioct.h`, which that DDK keeps
     beside its WDM headers, outside the build's include path (so it is
     named by that path), and which has no package type: `cm_dsd.h`'s own
     constants are held equal to it where it has them, the package type
     on amd64 alone. The amd64 allowlist's `MmGetSystemRoutineAddress`
     row is held to `cm_oscnt.obj` alone.
   - **The port number.** `usb4-port-number` is no binding.
     [ms-usb4acpi] allows it to be zero (unused), and nothing establishes
     that a non-zero value equals `TBT_U4_PORT.Index`, which `tbt_u4cm.c`
     assigns in the order it finds lane adapters. An ACPI-named member
     joins **unbound**; its port comes, as for any member, from the
     router-named source or from learning (section 7.6), so no eject or
     teardown is ever directed by the firmware's number. Zero, a repeated
     value and two values swapped against the router's naming are host
     vectors, each giving membership and no binding.
   - **The claim.** The filter's own location, the normalised reference,
     the port number and the filter's publication serial at the
     evaluation are kept in its registration entry (`cm_reenum.c`), under
     the registration list's lock, written once after the evaluation and
     cleared at withdrawal (`CmRegWithdraw`).
   - **The set changes only at a discovery.** A claim never adds, removes
     or moves a member between discoveries, so the state indexed by member
     position (section 7.7's `RecBarred`, `RecRetireAns`, `RecOldBuses`,
     the eject entries) keeps its meaning. The NHI's thread, at each
     discovery (`cmDiscover`, after the router-named pass and before the
     claims are published for `XhciTbtExclusive`), copies under the lock
     the scalar claims whose reference is its own name, releases it, and
     offers each claimed bridge (`XhciTbtTopoAddAcpi`, source
     `XHCI_TBT_SRC_ACPI`).
   - **Activity follows the claim.** `cmRegRecompute`, which makes a member
     active by its filter's location alone today, makes an ACPI-sourced
     member active, and its filter owned, only while that filter holds a
     current claim naming this NHI. Otherwise the member carries
     `CM_PORT_NO_CLAIM` and not `CM_PORT_EXCLUSIVE`, as a port another
     domain claims does, so every path gated on exclusivity leaves it too
     (the storage link channel's publication and presence sampling,
     `cmNvExclusive`; learning; ejects): a filter restart whose new
     evaluation fails, or names another NHI, leaves the member listed and
     inactive, so no request goes through it and no storage entry is
     published or advanced for it.
     A member another source supports keeps that source and is unaffected.
   - **A renewed claim.** A root port's filter restarted alone (stop and
     start, the NHI staying started) publishes again under a new serial
     and reads the `_DSD` again. When that claim names the same NHI and is
     the only one naming it, the member adopts the new serial at the next
     idle pass, its index and the set unchanged, and its activity follows
     the new publication as a router-named member's does (the baseline is
     asked of each publication). Not while the host interface is lost or
     contained: the change is left unseen until it is back, so the
     recovery's revalidation sees the set as it was (host vector
     `test_cm_dsd`).
   - **A late claim.** A claim published while the domain has no set (no
     listing, rediscovery pending or exhausted) re-arms its rediscovery
     (`CM_REDISCOVER_TRIES` again), so the claim is offered. A claim
     published after the domain was listed, for a bridge not in its set, is
     logged and taken at the NHI's next start; adding members to a listed
     domain is a later step, not built.
   - **Recovery.** Record 22 section 7.7's revalidation
     (`CmReenumSourcesHold`) rebuilds the set with `XhciTbtTopoFind` and
     compares sources exactly, the router-named ones excepted; an ACPI
     member is excepted the same way only when, under the registration
     list's lock, its filter holds a current claim naming this NHI with the
     serial the member was added under. Otherwise the revalidation fails
     as for any vanished member. The set being unchanged since the
     discovery, the snapshot the revalidation checks is the whole set.
   - **The rules** are `XhciTbtTopoAddNamed`'s: the same outcomes allow a
     claim, the same locations are refused (not a bridge, the NHI or its
     parent), a bridge already a member keeps its source, an unreadable
     location voids the discovery, and the override stays the whole set.
   - **One port per NHI** (decided by the coordinator on the owner's
     delegation, 2026-11-04). Each AMD NHI has one tunnel root port, so
     when two or more current claims name one NHI none of them is taken
     for it, not one picked (logged, `cm.acpi.several`): taking none is
     safer than guessing which the firmware meant. The router-named source
     binds as before. The same holds between discoveries and at recovery:
     an ACPI member is active, and revalidates, only while its claim is the
     only one naming its NHI.
   Unknowns, to read on the emulator's AMD layout before the code is
   trusted: how 8.1 and 10 return a reference element inside a `_DSD`
   package (as a name string, which the reader expects, or refused), and
   whether that string is the same full path `DEVPKEY_Device_BiosDeviceName`
   gives the NHI; and, if a non-UEFI AMD USB4 machine is ever found, the
   same on Vista and 7 (2000 and XP lack the property routine).
4. **Learning by elimination (fallback, not built now).** Where a family has
   as many NHIs as listed ports among the siblings of their shared parent
   and no source above names them, the ports are held as candidates of the
   family, members of no domain, and the first tunnel that adds a device
   below one of them gives that port to its domain, and with two NHIs the
   other port to the other. The first hot plug before that still has no set.
5. **Position, rejected.** The first `150A` with the lower NHI function
   matches the one machine read and is documented nowhere.

`DEVPKEY_Device_DependencyProviders`, which shows the same pairing on
Windows 11, is a user-mode property of recent Windows only; it is no driver
source (at most the installer could write the override from it, not
planned).

**The 9x route, deferred.** CSM shims may one day start 98 SE and ME on
these machines (owner, 2026-11-04). Then the route is, in order: operation
`30h`; a static scan for the `150A` port's `_DSD` package through the
on-demand table reader of task 46.21 (option B: the DSDT and every SSDT
whole), resolving the enclosing device's path by static `_ADR` names
alone; or the override. No code now. The owner's no-ACPI start of 98 SE on
the Strix Halo machine (2026-11-04) is the reading this route starts from:
the AMD host routers answered the connection manager; a device attached at
power-on came up as router `1-2` through the firmware's pre-OS PCIe
tunnel on the first `150A` root port; no hot-plug event arrived after the
start on any port; and there was no INTx route.

**Tests.** Host vectors for the `_DSD` reader (the capture's shape made up
again, a method form's answer, a wrong UUID, a missing or repeated key, a
port number of zero, repeated or swapped, an oversize answer), for the
name normalisation and match, for the one-port rule, and for `XhciTbtTopoAddAcpi` (each outcome
as `XhciTbtTopoAddNamed`'s, a member joining unbound, a bridge already
router-named keeping its source). The registration glue (the claim, the
activity rule, the late claim, recovery) runs only in the driver and is
read on the emulator's legs. Legs on the emulator's AMD layout (NHIs behind an
internal bridge, two root ports on the root bus, an SSDT with the static
`_DSD`, a method form and one wrong reference; operation `30h` answered and
refused), on 8.1 and 10 x64, and on 10 x86 with the helper present and
absent.

[ms-usb4acpi]: Microsoft, "USB4 ACPI requirements" (learn.microsoft.com,
windows-hardware/design/component-guidelines), its `_DSD` properties for a
USB4 host's PCIe root ports (`usb4-host-interface`, `usb4-port-number`).

#### 7.6.2 A tunnel found up at start: inferring its port (task 48.43; owner, 2026-11-10: option 3, off by default)

Proposed on 2026-11-10 (Phase 48, the eject sections of `runs/run-48.md`)
and decided by the owner the same day: option 3 below is built, eject
only, behind `XhciCmInferAtStart` with default 0, and the learning rule
is narrowed as section 7.6.3 says. The options are kept as they were
weighed.

**The gap.** A PCIe tunnel the pre-OS connection manager left up, on a
host router that answers Get PCIe Downstream Entry Mapping Not
Supported (the emulator's `asm4242` model), is never router-named and
never shows a gain, so its port stays pending for the instance's life
(the learning's window above only stops the reading). Read on 8.1 x86
under emulation (`runs/run-48.md`, the eject sections of 2026-11-10):

- Its eject reaches the hand-off and stops there (`u4.eject.unmapped`):
  the tunnel stays up until the unplug. Windows holds the unit for the
  eject (problem 47) through a re-enumeration of every member, so
  nothing restarts it; the tools say the tunnel stays up (record 24
  section 5.2). A unit started again after such an eject was the pinned
  emulator build's power controller on the tunnel port (the board's
  ports have none), not this rule.
- While it is pending no other port's gain is attributed (the rule
  above, until section 7.6.3): a second enclosure plugged into USB4
  port 2 while Windows ran was refused as `SEVERAL`
  (`u4.learn=0000020A`), so its eject could not close its tunnel either.
  Only an unplug of the first device (its port proven down) lifted that.

**What is known at start.** The router's own reads: which host USB4
ports have a PCIe path up (the tunnel found up, `TBT_U4_PORT.Pcie.Up`,
from the adapters' path configuration), the routers behind them, their
PCIe Upstream and Down adapters. The PCI side: the set's members (here
the switch's own tunnel ports by id), each one's secondary bus range and
the functions answering below it at the baseline, and its Link Status.
What is not known is which member a given host PCIe Down adapter reaches:
that is exactly the correspondence the router would have named.

**Options, as weighed.**

1. **No inference (as before).** No risk of a wrong teardown. Cost: the
   two readings above, for as long as the device stays connected. This
   is what `XhciCmInferAtStart` 0, the default, keeps.
2. **One tunnel, one occupied member.** Exactly one host USB4 port with a
   PCIe tunnel up, exactly one active member with a function behind it,
   that port bound to that member. The PCI reading is its only witness;
   not built.
3. **Option 2 cross-checked by the links (built).** The same, and in the
   same pass both link readings agree: the PCI side's Data Link Layer
   Link Active, on members that report it, and the router's PCIe Down
   adapter link states. The rules are below.
4. **By position** (tunnel port device 0 for USB4 port 1, device 1 for
   port 2). Rejected, as section 7.6.1 rejects position: the second
   port's place is inferred, not published, for the ASM4242, and no
   specification ties a switch's device numbers to adapter numbers.
5. **By the ACPI `usb4-port-number`** of the tunnel port's `_DSD`, where
   the firmware gives one. Kept rejected as a binding, as section 7.6.1
   keeps it: the number is the platform's port numbering, which no
   specification ties to the router's adapters.
6. **By a probe**: pause the tunnel's path, read which member's link
   drops, restore it. Rejected: it interrupts a working drive, possibly
   the system's own, to learn something only an eject needs.

**The argument.** A function behind a tunnel port of a discrete USB4
controller (a member by the switch's own port ids, record 21 section
6.3 source 2) can only be carried by a PCIe tunnel of that domain; with
exactly one tunnel up, the one occupied member is its. The PCI reading
alone could be wrong in four narrow ways: a member whose function is not
carried by a tunnel (an override's or a listed root port's set, where
another device can sit; or a downstream port of the controller's own
switch with one of the controller's own functions behind it, which the
switch's port ids alone do not tell from a tunnel port where it is hot-plug
capable, as the emulator's model makes its xHCI's port); a reading taken while a second tunnel's
function had not yet answered (a port the router reads down whose path
was in fact up); a function that answers without its tunnel (a stale or
shadowed configuration read); or a second tunnel brought up between the
router's reading and the PCI reading. The rules below exclude the first
by source and by the shape of what answers behind the member, the second and third by requiring both link readings to agree
with the occupancy, and the fourth by the thread: tunnels are built and
torn down on the connection-manager thread alone, which is the thread
that reads, so none changes between the reads of one pass; the bus
numbers read before and after the pass catch a re-enumeration by
Windows in between.

**The inference.** `XhciTbtInfer` (`tbt_topo.c`, pure) over what the
driver half (`cm_nhi.c`) reads; every condition is required, and the
first one that fails is the refusal's reason:

- I1. `XhciCmInferAtStart` is 1 (REG_DWORD, 0 or 1, any other value the
  default 0; the NHI INFs write 0 with NOCLOBBER).
- I2. The host router is a USB4 router, not a Thunderbolt 3 host: the
  LTSSM field is read in the USB4 encoding alone (section 19.4).
- I3. The set as discovered: outcome `SET` from the discovery itself
  (`FindOutcome` `SET`), one to `XHCI_TBT_MAX_TUNNELS` members, every
  member of source 2 (`XHCI_TBT_SRC_SWITCH`, the controller's own
  tunnel ports by id) and active (no other domain claims it). Never
  under the override, never for listed root ports, router-named,
  ACPI-named or SHPC members.
- I4. The shared binding table is empty: no named, learned or
  conflicted entry and no overflow - so never beside a router-named or
  learned binding in the domain - and the port is not marked conflicted
  for the inference (the read-back below).
- I5. Exactly one host USB4 port has its PCIe tunnel up, that port is an
  at-start port (section 7.6.3: its tunnel found up at start and no
  `TUNNEL_UP` taken for it since; a recovery's rebuild queues one and
  ends the mark), and the learning marks agree: that port is pending and
  no other port is (`Binds.Pending` is that port's bit alone - so no
  other port has a path written, incomplete, residual, quarantined or
  held, section 7.6's reconciliation). The seam record (`U4SeamValid`,
  `U4SeamGen`) is no evidence either way: the start sets it for every
  tunnel it finds up, so that the eject's stale check accepts them.
- I6. The router reading, one read of `ADP_PCIE_CS_0` per host PCIe Down
  adapter (`TbtU4PcieLinkClass`): the port's reads Path Enable set and
  its LTSSM in L0, L1, Recovery or Recovery.Idle; every other host USB4
  port's reads Path Enable clear or its LTSSM in Detect. A failed read,
  a port with no PCIe Down adapter, or any other LTSSM value (a
  transitional state included; there is no re-read) refuses.
- I7. The PCI reading, one pass over every member in set order: the bus
  number register; the PCI Express capability, a downstream port, Link
  Capabilities with Data Link Layer Link Active Reporting Capable (bit
  20) and a Link Status read (`XhciTbtDecodeLink`'s valid answer); the
  function-0 probe of every device number on the secondary bus (the
  learning's occupancy) and, where a function answers, device 0's
  Header Type and PCI Express Capabilities register; then every member's
  bus number register again. Every register readable and not all-ones;
  every member's bus numbers the same both times and its secondary bus
  the learning baseline's; exactly one member with any function behind
  it, what answers there **a tunnel's far end** - device 0 alone, a
  PCI-to-PCI bridge whose PCI Express capability says Upstream Switch
  Port - and that member's Link Active set; every other member with
  nothing behind it and its Link Active clear. A member that does not
  report Link Active refuses (a 0 there proves nothing).
  The far end's shape (task 48.43, revised 2026-11-10; reason 17,
  `XHCI_TBT_INFER_SHAPE`): a device's own PCIe switch presents its
  upstream port there, as the emulator's enclosures do, while a host
  controller's own integrated functions behind its switch's ports - its
  xHCI, its NHI - are endpoints. Without it, on a set that counts the
  controller's xHCI's port as a member (a port that reports Link Active,
  its xHCI always answering), a tunnel found up at start whose function
  had not yet answered, its member's link still down, left the xHCI's
  member the one occupied member with both links agreeing, and the
  inference named it as the port's: an eject would then have torn down
  a tunnel the user did not eject. Any other shape refuses, an endpoint
  directly behind a tunnel port included; that costs only the inference.
  A shape that cannot be read (a failed access, or all-ones) is reason
  12, unread, never taken for a shape.

When it is tried: at each learning attempt of that port (section 7.6's
window), after `XhciTbtLearn` left the port pending, so at most once a
pass and never once the window has closed (60 s after it opened at the
start, or at the recovery that marked it again). A port whose tunnel
this driver built never satisfies I5. Its log: `u4.infer` (port 15:8,
member index 7:0) and `u4.infer.bdf` when made; `u4.infer.refused`
(port 15:8, reason 7:0) when the reason differs from the last one
logged, so a refusal repeated at every attempt is logged once.

**The binding kept apart.** An inference is a record of its own,
`XHCI_TBT_INFER` beside the NHI's `XHCI_TBT_BINDS`, never an entry of
that table: `XhciTbtPortMember`, the learning, the tunnel changes'
choice of member, `CmNhiMemberPort` and `CmNhiMemberGen`, the stor98
statements of a closed path (section 10.2) and the isolation statement
to xHCI98 (section 10.5) never read it, and treat the port as unbound,
as before. The port stays pending: the learning goes on as section 7.6
and 7.6.3 say, and an inference never ends the mark (were it cleared, a
function of the inferred member that stopped answering and came back
while another port was pending would be learned as that port's, and a
learned binding reaches every consumer above). The record is dropped,
at the next idle pass that sees it, when a binding of any kind enters
the shared table, when the port is no longer pending, its PCIe tunnel
no longer up or its connection generation another, or its member no
longer in the set; and at every rediscovery that changes the set
(`CmNhiSetChanged`).

**At the eject.** In the hand-off (`cmU4EjectBridge`, every target:
the NT eject hook and the 98 SE and ME eject transaction alike), only
where no shared binding names the bridge and the inference names it:
I1 to I7 are read again, in full, now, and must give the same port, the
same member, the same secondary bus and the same connection generation
as when the inference was made (at the eject Windows has removed the
unit's stacks, but its functions still answer configuration reads while
the tunnel is up, so the occupancy reads as before). Then the stale
check of the shared path: the port's router present, its PCIe tunnel
up, its seam record absent or of the same generation. Any difference
drops the record (`u4.infer.recheck`, the reason) and the hand-off logs
`u4.eject.unmapped` as before; the tunnel stays up until the unplug.
When all agree, `TbtU4EjectPcie` tears the port's PCIe tunnel down as
for a bound port (`u4.eject`, `u4.eject.inferred`).

**The read-back.** After a teardown the router read back closed
(`TbtU4EjectPcie` `TBT_OK`), the member is read at that moment and at
each idle pass after it for 2 s (record 21 section 6.5's poll bound):
Link Status readable with Link Active clear, the bus numbers unchanged
and no function answering on its secondary bus. Seen once, the closure
is confirmed (`u4.infer.closed`): the status record's entry for that
port names the member as its tunnel port for the ejected tunnel (so the
tools' watch reads "closed after an eject", record 24 section 5.2), and
the record is spent. Not seen within the bound - a function still
answering, Link Active set, an unreadable member, or the instance
contained or stopping meanwhile - the port is marked conflicted for the
inference for the instance (`u4.infer.conflict`; no further inference
on it) and no closure is claimed: the status record names no tunnel
port, so the tools give their plain "has not confirmed that the PCIe
tunnel was closed" line. A teardown that did not read back closed is a
failed eject as for a bound port, and the record is dropped. The
read-back detects; it does not undo. A path torn down cannot be put
back as it was: a rebuild goes through the adapters' Detect (sections
6.8 and 10.1, a reset boundary), the teardown has already removed
chained routers and queued its own `TUNNEL_DOWN`, and Windows has
surprise-removed the functions behind it, writes in flight lost. So no
rebuild is made, and the safeguards that count are the ones before the
teardown.

**The tools.** No change. With `XhciCmInferAtStart` 0, or the inference
refused or dropped, the status record names no tunnel port for that
router and the tools' "The PCIe tunnel stays up until the device is
unplugged ..." line stands (record 24 section 5.2). With the closure
confirmed they say "The PCIe tunnel was closed."

**The risk that stays.** A change between the reads of one pass that
the bus numbers do not show, by something other than this driver's
thread, on a set of the controller's own tunnel ports; the re-check at
the eject reads it all again, so it would have to hold at both. The
cost of a wrong inference is a surprise removal of a drive the user did
not eject, which nothing after the teardown repairs; hence the default
0 until the emulator's legs and the owner's board have read it.

**Tests.** Host vectors (`test/test_tbt_topo.c`): the inference made
(one tunnel, one occupied member, both links agreeing) and each guard
refusing in turn (I2 to I7: a Thunderbolt 3 host; the override, a
listed root port, a router-named member and an inactive member; a
binding in the table and a conflicted port; no tunnel, two tunnels, a
tunnel not found at start, another port pending; the router's port not
linked, another port linked, an unknown or transitional LTSSM; an
unreadable member, a member without Link Active Reporting, two occupied
members, none occupied, Link Active set on an empty member or clear on
the occupied one, bus numbers moved, the controller's own xHCI as the
one occupied member with port 1's function not yet answering, an
endpoint or a second device behind the occupied member), a wrong inference refused (the
links disagreeing with the occupancy), the re-check, the read-back's
three outcomes, and the binding kept apart (`XhciTbtPortMember` and
`XhciTbtLearn` unchanged by it). Legs on the emulator's `asm4242` model
with the enclosure at power-on, 8.1 x86: `XhciCmInferAtStart` 0 (the
tunnel stays up, as before) and 1 (the tunnel closed at the eject, or
the refusal logged where a reading does not agree). Then the owner's
board.

**As read** (task 48.43, 2026-11-10, 8.1 x86 on the emulator's
`asm4242` model with the enclosure at power-on; `runs/run-48.md`):
with `XhciCmInferAtStart` 0 nothing is inferred and the eject gives the
tools' "stays up" line, as before. With 1 on the launcher's model the
inference refuses (`u4.infer.refused`, port 1, reason 13): the model
makes the downstream port of the ASM4242's own xHCI hot-plug capable,
so it is a third member of the set, and it does not report Link
Active; the two tunnel ports do (Link Capabilities bit 20), the
occupied one reading Link Active set and the empty one clear. With
that port made not hot-plug capable (`usb4-asm-dsp.hotplug=off`, the
board's two tunnel ports alone as members) the inference is made at
start (`u4.infer=00000100`) and the eject closes the tunnel:
`u4.eject.inferred`, the read-back confirmed at once
(`u4.infer.closed`), and the tool's "The PCIe tunnel was closed." The
owner's board's Link Capabilities are not read.

#### 7.6.3 The learning rule narrowed (task 48.43; owner, 2026-11-10)

The rule of section 7.6 attributes a gain only while one port is
pending, so a port found up at start that stays pending for the
instance's life blocks the learning of every other port for as long as
its device stays (ejr's `u4.learn=0000020A`). Narrowed: another pending
port does not block the attribution when every other pending port is an
**at-start port** whose function already showed at the start reading,
and the gain is on a member that was empty then.

- **At-start ports** (`XHCI_TBT_BINDS.AtStart`, `XhciTbtLearnAtStart`):
  marked by the driver half where the start marks the ports found with
  a live path pending (`cmU4BindRest`, which a recovery also runs after
  its commit), for an unbound port whose PCIe tunnel is up and was found
  up, not built (`Pcie.Preserved`). The mark ends with the pending mark
  (learned, bound, released or proven down), and at every `TUNNEL_UP`
  of the port the batch takes (`XhciTbtLearnNotAtStart`, called beside
  `XhciTbtLearnUp` in `CmNhiU4Take`): a tunnel brought up again may
  bring a function anew. No later pass marks a port again.
- **The start reading.** The first full reading of the set (an arm, or
  a learning attempt's reading) taken after the at-start marks gained a
  port, kept by bridge location: the members as they were
  (`StartBdf`), which had a function behind them (`StartOcc`), and
  whether it can be trusted (`StartOk`): every member then of source 2
  (the switch's own tunnel ports by id) or router-named - the sources
  behind which only a tunnel of this domain can put a function, so
  never under the override and never for listed root ports, ACPI-named
  or SHPC members; every pending port then an at-start port; every
  member occupied and not explained by a binding (named or learned:
  that member's function is its bound port's) holding a tunnel's far
  end (section 7.6.2 I7's shape: device 0 alone, an Upstream Switch
  Port); and exactly as many of those members as there were at-start
  ports. Each tunnel lands on one member, and a far end behind a member
  of those sources is a tunnel's of this domain, so then every at-start
  tunnel's function is on a distinct one of those occupied members.
  The shape is needed (task 48.43, revised 2026-11-10): source 2 does
  not prove that what sits behind a member is a tunnel's. The emulator's
  model makes its xHCI's downstream port hot-plug capable, a member by
  the switch's port ids with the xHCI always behind it; counted, that
  member made up the count while port 1's at-start function had not yet
  answered, and port 1's function answering during port 2's learning
  attempt, on a member empty at the start reading, would have been
  learned as port 2's - a wrong binding in the shared table, which
  every consumer reads (an eject of port 1's device acting on port 2's
  tunnel, the stor98 and isolation statements naming the wrong member).
  A member holding anything but a far end makes the reading untrusted,
  and the old rule stands. A shape that cannot be read (a failed access,
  or all-ones) leaves the reading not taken: the next full reading is
  the start reading. Until it is taken the narrowed attribution holds
  its gain (below); the rule for a port pending alone reads no shape and
  goes on as before. A port joining
  the at-start marks later takes the reading again (more conservative:
  more members count as occupied). When no at-start mark is left the
  reading is forgotten.
- **The narrowed attribution.** In `XhciTbtLearn`, after the reading
  and exactly one gain on a member not bound to another port: where
  another port is pending (formerly `SEVERAL` at once), the gain is
  still attributed to the port tried when all of these hold: the start
  reading is taken and trusted; every other pending port is an at-start
  port; the port tried is not one; every member is active (a member
  another domain claims could hold that domain's tunnel's function); the
  member that gained was a member at the start reading and empty then
  (explained or not); and what it gained is a tunnel's far end (the
  same shape: a function of the controller's own that answers late, on
  a member empty at the start reading, is never taken for a tunnel's),
  or, since section 7.6.4, another shape with the link's evidence.
  Where that shape, the port reading section 7.6.4 needs, or the start
  reading's shape cannot be read the attempt
  is `UNREAD` and the baseline is not moved, so the gain is seen again
  at the next attempt rather than consumed by a refusal. Nor does an
  at-start port's own attempt consume a gain while a port not at start
  is pending (it answers `SEVERAL` and leaves the baseline): it is
  never given the gain while another port is pending, so the gain is
  left for that port's attempt, whichever order the pending ports are
  tried in.
  Otherwise `SEVERAL`, as before. The other outcomes (`INACTIVE`,
  `CONFLICT`, `LEARNED`) follow as before.
- **The risk it keeps out.** A function of an at-start port's member
  that stopped answering and came back reads as a gain; it is on a
  member occupied at the start reading, which never qualifies. A tunnel
  found up at start whose function had not answered by the start
  reading leaves fewer unexplained occupied members than at-start
  ports, even where a bound member or a device that is no tunnel's
  could have made up the count (a bound member's occupancy does not
  count; sources where a device can sit without a tunnel are not
  trusted; and a member of a trusted source holding anything but a far
  end - the controller's own xHCI behind a hot-plug capable port of its
  switch - makes the reading untrusted rather than counting): the
  reading is not trusted and the rule is the old one. A function of the
  controller's own that answers only later, on a member empty at the
  start reading, is no far end and is never attributed under this rule.
  The rule of section 7.6 for a port pending alone was not changed by
  this section; section 7.6.4 narrows it, and this rule, the same way.
- **On the inference.** Independent of it: a port learned under the
  narrowed rule is a binding in the shared table, after which I4
  refuses (and drops) any inference in the domain. With both on, a
  second enclosure learned on port 2 means port 1's tunnel is no longer
  the only one up, so its eject is unmapped again, as before.

**Tests.** Host vectors: an at-start port pending with its member
occupied and a gain on an empty member learned for another port; a gain
on the occupied member refused (`SEVERAL`); the start reading not
trusted (fewer occupied members than at-start ports; a bound member's
occupancy making up the count while the at-start function is late, then
another port's hot plug; a non-at-start port pending at the reading;
the controller's own xHCI's port a member, its xHCI occupying it, port
1 preserved with its function not yet answering, then answering during
port 2's attempt: `SEVERAL` and no binding in the shared table)
refused; with the xHCI silent at a trusted start reading, its function
answering during port 2's attempt refused and port 2's own far end then
learned; an endpoint gained refused; a shape unreadable once, at the
gain (`UNREAD`, then `LEARNED` at the next attempt) and at the start
reading (not taken, then taken and trusted); the xHCI's shape
unreadable at every reading, port 1 pending alone still learned and
port 2's gain beside it held (`UNREAD`); port 1's attempt between
port 2's unreadable attempt and its retry, and port 1 tried first,
leaving port 2's gain to be learned; an inactive member
refused; the
override and listed root ports refused; a member not in
the start reading refused; the at-start mark ended by a `TUNNEL_UP` and
by `XhciTbtLearnDown`; the start's own sequence (`cmU4BindRest` sets
the seam record and the at-start mark both). Leg: a second enclosure on port 2
learned while port 1's at-start tunnel is pending (`u4.learn`
`LEARNED` for port 2), and its eject closing its tunnel.

**As read** (the same leg setting, two tunnel ports as members): a
second enclosure plugged into port 2 while port 1's at-start tunnel
stayed pending was learned (`u4.learn=00000203`; before, `0000020A`).
With the launcher's model as it was the start reading counted the
xHCI's occupied downstream port too and was not trusted, so the old
rule stood there (`SEVERAL`); since the far-end shape that member makes
the reading untrusted whatever the count. Read again with the far-end
rule (2026-11-10, the launcher's xHCI port made not hot-plug capable):
port 2 learned while port 1's at-start tunnel stayed pending
(`u4.learn=00000203`), and with `XhciCmInferAtStart` 1 the inference
made at start and the eject closing port 1's tunnel
(`u4.eject.inferred`, `u4.infer.closed`, "The PCIe tunnel was
closed."). Its eject was not read: the
second enclosure's upstream port sat at problem 31, learned or not. The
cause is the emulator's, not this rule's nor record 31's kept child:
the launcher's build gives each tunnel port one bus, so a switch
plugged after the start gets no bus numbers (record 31 section 4.10,
read 2026-11-10; `runs/run-48.md`).

#### 7.6.4 What one gain may be: a tunnel's far end, or the link's evidence (task 48.43; 2026-11-10)

Sections 7.6.2 and 7.6.3 require a tunnel's far end of the start
reading, of the narrowed attribution and of the inference. The rule of
section 7.6 for a port pending alone did not: it attributed one gain of
any shape. So a function of the controller's own (its xHCI behind a
hot-plug capable port of its switch, as the emulator's model has it),
or a device that is no tunnel's, answering late while one port was
pending alone - a port found up at start whose function had not yet
answered, or a hot plug - was learned as that port's: a wrong learned
binding in the shared table, read by every consumer (an eject of that
port's device acting on another tunnel, the stor98 and isolation
statements naming the wrong member). Record 34 section 3.12's chained
transaction had the same hole: a function of the parent's own answering
late on an empty chain-port candidate was taken for the kid's.

**The rule.** One gain is attributed - under section 7.6's rule for a
port pending alone, under section 7.6.3's narrowed rule, and in record
34 section 3.12 item 4's transaction - only when what answers behind the
bridge (`XhciTbtGainAdmits`) is:

1. **a tunnel's far end**: device 0 alone, a PCI-to-PCI bridge whose PCI
   Express capability says Upstream Switch Port (section 7.6.3's shape).
   A device router with PCIe Down adapters of its own (a hub, a dock)
   and the ASM2464 (as the emulator models it) put a switch behind their
   PCIe Up adapter, and its upstream port is what a tunnel brings; or
2. **any other shape with the link's evidence**: an endpoint directly
   behind the tunnel port (the emulator's plain enclosure puts its NVMe
   there, with no switch), or more than one device, only when the
   bridge's port reading (record 21 section 6.5, `XhciTbtPortRead`) was
   DOWN - it reports at least one of Link Active, Presence Detect State
   and the SHPC slots, and each one it reports says absent - at a
   reading where nothing answered behind it, nothing has been read
   answering there since, and it reads UP at the gain. That is positive
   evidence that a link came up behind that bridge, which is what a
   tunnel does: the PCIe link over a USB4 path trains once the path is
   enabled. A function of the controller's own is wired behind its port,
   which reports it present (Presence Detect State, or Link Active)
   whenever it is there, so no DOWN reading is taken of its port and it
   is never admitted, however late it answers.

The router naming a port (section 7.6's router-named source) binds with
none of this: that is positive evidence of its own, and unchanged.

**Where the evidence is read.** At depth 1, per member
(`XHCI_TBT_BINDS.BaseDown`): at each arm, each member empty in the new
baseline has its port read, and keeps the bit an earlier baseline gave
its location; at each attempt that moves the baseline, each empty
member without the bit is read again and each occupied member's bit is
cleared. An arm that cannot read every member keeps no new baseline (the
old one and its bits stay, not armed), so a transient failure never
loses evidence the next arm would find by location; but a member it read
occupied before failing loses its bit all the same, since a function was
seen answering there. An attempt that ends before the baseline moves
(a member unread, a shape or port not read, an at-start port's attempt
leaving the gain) holds the bit of each member it read occupied
(`BaseHeld`): the bit then stands only while that member stays occupied,
and a reading of that member that finds it empty again spends it at
once, even when another member then fails. A device seen answering in a
member's scan that then fails counts as an occupancy all the same.
In the chained transaction a candidate read gaining at a try that waited
spoils the transaction if it reads empty again before it is decided.
No reading that fails part-way is more lenient than a whole one. In the
chained transaction such a read decides nothing, so anything it did see
differ from the baseline - another candidate set (count or place), a
candidate's buses (as the walk read them), a device gained or lost where it was read - spoils
the transaction at once; so does a failure of the walk itself (which
never descends below a candidate, so a kid answering Retry Status does
not fail it), and reaching a candidate already
waiting on its port reading, whose gain only a whole reading decides. At
depth 1 an
attempt that ends before the baseline moves holds the evidence of what
it saw occupied, spends what it saw empty and held evidence behind a
member scan that failed (what the failure hid may be that loss), sets no
new evidence, and
spends the evidence of a gain it saw that `XhciTbtGainAdmits` rejects
outright (or any gain on a member whose scan failed), so a link coming
up later never admits a function already seen answering without it. The
cost: a kid whose gain is first seen by a read that fails elsewhere is
unattributed, and a direct endpoint first seen on a member whose scan
failed is not learned. At depth 2, at the
arm, once, after the two equal readings:
each empty candidate's port (`XHCI_CHAIN_BASE.Down`). The arm is
immediately before the kid's Path Enable, so no link of the kid's can be
up then.

**Outcomes.** A shape or a port reading that cannot be read (a failed
access, all-ones, the port `ERROR` or `GONE`) is a reading not made: at
depth 1 `UNREAD` with the baseline kept, so the gain is seen again; at
depth 2 `WAIT`, the transaction open. A port reading that fails after
reading whole a signal that says absent (Link Active clear where it is
reported, Presence Detect State clear where the slot is implemented, the
SHPC slots all empty, every slot read even when only the DWORD Select's
restoring write then failed) is not such a reading: the whole reading would
have been `DOWN` or `MIXED`, never `UP`, so the gain is not admitted, as
the whole reading would decide (merge review, 2026-11-10). A gain not admitted: with the
port pending alone, the new outcome `SHAPE` (`u4.learn` low byte `0F`),
the baseline moved - the gain consumed, so that the port's own far end
answering later on another member is one gain, not two - and the port
pending as before, until it is learned or proven down; under the
narrowed rule `SEVERAL`, as before; in the chained transaction
`SPOILED` with the outcome `SHAPE` (`cm.chain.txn` low byte `09`), the
kid unattributed.

**What is no longer learned** (chosen: no binding without evidence):

- A tunnel found up at start whose endpoint answers only after the
  baseline, with no switch: its port was up at every reading, so no
  evidence; it stays pending (its eject unmapped, section 7.6.2 off by
  default; the whole set acts for it), as every port found up at start
  did before section 7.6.3. A far end there is still learned.
- An endpoint behind a member whose port reports none of the three
  signals to the learning's reader. The learning reads through the
  thread's read-only accessor (`cmCfgRead`, `HalGetBusData`), and an
  SHPC slot is read through DWORD Select, a write: so on the emulator's
  i440FX SHPC tunnel bridge a plain enclosure (its NVMe in the slot) is
  no longer learned (on Windows 2000 and later it is again since task
  49.18, section 7.6.5; on 98 SE and ME this stands). Its eject keeps the tunnel up until the unplug, as
  for any port whose member is not known, and no stor98 statement names
  its member (section 10.2); a dock there (the emulator's switch, a far
  end) still is learned, and so is a plain enclosure on a q35 tunnel
  port, which reports Link Active and Presence Detect State.
- The inference (section 7.6.2 I7) and the start reading keep the far
  end alone: no transition can be read of a tunnel already up.

**The residual.** A function wired behind a port whose every reported
signal read absent while it did not answer, and present once it did (no
Presence Detect State reported, its link down and trained again), would
carry the evidence. No member read so far has one: the emulator's model
reports its xHCI's port present (Link Active), and section 7.6.3's leg
setting makes that port no member at all.

**Every other place a binding is made**, checked for the same hole: the
router-named source binds by the router's naming, never by a gain; a
`KNOWN` outcome and `XhciTbtLearnBound` consume a pending mark for a
binding already made; a recovery's rebuild and a resume's
reconciliation learn through `XhciTbtLearn` (this rule); the inference
and the start reading already required a far end (section 7.6.3); the
chained transaction now applies this rule. Nothing else writes a
learned binding.

**Tests.** Host vectors: a port pending alone with the controller's own
xHCI's port a member, the xHCI answering late during its attempt:
`SHAPE`, nothing bound, the gain consumed, the port's own far end then
learned; the same with port 1 found up at start, its function late (the
case reported): `SHAPE` for the xHCI, port 1's far end then learned on
its own member; a far end learned with no link change; a far end's shape
unreadable (`UNREAD`, the baseline kept, then `LEARNED`); a direct
endpoint learned on the evidence (down at the arm, up with the
endpoint), its port unreadable at the gain (`UNREAD`, the baseline and
the evidence kept), with no link change (`SHAPE`, consumed), with its
port up at the arm (no evidence, `SHAPE`), the evidence read at an
attempt and kept by location across a second arm; the emulator's i440FX
SHPC bridge with an NVMe at 01:01.0, refused through a read-only accessor
and learned through one that writes DWORD Select; under the narrowed
rule an endpoint with no link evidence refused and one with it learned;
in the chained transaction an endpoint with its chain port still down
and one whose port read present at the arm refused (`SHAPE`), a far end
with no presence change learned, a shape unreadable waiting then
learned. A mutation that admits every shape without evidence fails the
new vectors and binds the reported case's port 1 to the xHCI's member.

**As read** (2026-11-10, the `release` `xhcicm.sys` of `2023c3ce`
copied into each guest; 8.1 x86 on the `asm4242` model with the
launcher's switch ports padded, `asm4242-20261027-fwpad`; XP on the
`intel-adl` model): a second enclosure (the ASM2464 model, a far end)
plugged into port 2 while port 1's at-start tunnel stayed pending was
learned (`u4.learn=00000203`), came up at problem 0, and its eject closed
its tunnel (`u4.eject.bridge=00000208`, `u4.eject=00000200`, "The PCIe
tunnel was closed."); with `XhciCmInferAtStart` 1 the inference and the
inferred eject read as before (`u4.infer=00000100`,
`u4.eject.inferred=00010200`, `u4.infer.closed=00010200`); on the
`intel-adl` model, whose third member is the platform's own xHCI root
port, a plain enclosure (its NVMe directly behind tunnel port 00:05.0)
plugged after the desktop was learned on the link's evidence
(`u4.learn=00000103`, the tunnel port 00:05.0), Stor98 bound by the
wizard, and its eject closed the tunnel (`u4.eject.bridge=00000028`,
`u4.eject=00000100`, "The PCIe tunnel was closed.").

#### 7.6.5 The SHPC slots read by the learning on Windows 2000 and later (task 49.18; owner, 2026-11-11; a design, reviewed and revised; coded; read)

Put to the owner on 2026-11-11 as five options (`runs/run-49.md`,
"49.18") and decided the same day: the learning on Windows 2000 and
later reads an SHPC member's slots, the eject keeps its own notice
wherever the port stays unknown, and `XhciCmInferAtStart` stays 0 by
default (section 7.6.2 is not changed). The options not taken: the
inference on by default on 2000 to 7; the inference at the eject; a
teardown at the eject read back and kept or undone (the correspondence
it rests on is the one not proven, and a path torn down cannot be put
back as it was, section 7.6.2's read-back).

**The gap.** Section 7.6.4 admits a gain that is no tunnel's far end
only with the link's evidence, a port reading DOWN and then UP. An SHPC
member reports through its slots alone, which are read through the
capability's DWORD Select, a write; the learning read through the
seam's read-only accessor, so on the emulator's i440FX tunnel bridge an
endpoint directly behind the bridge (a plain enclosure) was never
learned: its eject left the tunnel up until the unplug, and no stor98
statement named its member (section 7.6.4, "What is no longer
learned").

**What is read.** Nothing new in the pure half: `XhciTbtPortRead`
(`tbt_topo.c`, record 21 section 6.5) already reads an SHPC member's
slots when its accessor has a writer, and the host vector of section
7.6.4 already learns the i440FX endpoint through one. The registers are
those of the PCI Standard Hot-Plug Controller and Subsystem
Specification 1.0, section 4.2, as `tbt_topo.h` cites them: in the
bridge's SHPC capability (ID 0Ch) the DWORD Select byte at +02h and the
DWORD Data register at +04h, a window onto the SHPC working register
set; through it DWORD 3, Slot Configuration (Number of Slots
Implemented, bits 4:0), and DWORD 9 + n, the Logical Slot Register of
slot n (PRSNT1#/PRSNT2#, bits 11:10, 11b an empty slot). One reading:
the capability's first dword read (the DWORD Select found there kept),
DWORD Select written 3 and DWORD Data read, then for each slot DWORD
Select written 9 + n and DWORD Data read, then DWORD Select written
back to what was found. Only the DWORD Select byte is ever written;
DWORD Data is only read, so no working register is written through the
window. The reading is UP with any slot occupied, DOWN with every slot
empty, and a failed access - the restoring write included - is a
reading not made. No copy of the specification is on the development
host, so no sub-section is cited beyond the one `tbt_topo.h` already
names (`docs/references/README.md` says where the document is
obtained); the offsets and the bit positions were read again on
2026-11-11 against [linux] `drivers/pci/hotplug/shpchp_hpc.c`
(`DWORD_SELECT` 2h, `DWORD_DATA` 4h, `SLOT_NUM`, `FIRST_DEV_NUM`,
`PRSNT_SHIFT` 10; `shpc_indirect_read` writes the select as a byte and
reads the data dword; SPDX GPL-2.0+, read from a public code browser,
not from the pinned mirror, facts only) and against the emulator's
model (QEMU `hw/pci/shpc.c`: the select and the data register writable,
a write that overlaps DWORD Data written through to the working
register, a write of the select byte alone not).

**The writer.** The one record 21 section 7.2 qualified for the stor98
channel's reading: `CmRegConfigWrite` (`cm_reenum.c`), an
`IRP_MN_WRITE_CONFIG` of one byte to the bridge's PDO through its
filter's registration, at 40h or above (a capability, never the
header), for a member of this domain's set whose filter is published
and owned by this domain; it fails otherwise. Windows 98 SE and ME have
no filter and so no writer.

**Where it is given.** The learning's three calls alone - the arm
(`cmU4Arm`), the attempt (`cmU4Learn`) and the recovery's arm before a
rebuild - read through an accessor of their own, the seam's reader with
this writer, and only when all of these hold at that call:

- S1. The target is Windows 2000 or later (the filter route exists).
- S2. Every member of the set is of source SHPC (record 21 section 6.3
  source 4). Read from the discovery's record, no configuration access.
  A set with any member of another source keeps the read-only accessor,
  as today: an SHPC capability on a switch's port, a listed root port,
  an override's, a router-named or an ACPI-named member is not read.
- S3. Each member is the emulator's bridge by the admission the stor98
  channel uses (`cm_nvlinkos.c`): it, or the bus-0 bridge whose bus
  range holds it, carries QEMU's vendor id 1B36h, and it has an SHPC
  capability and no PCI Express capability.

Everything else keeps the seam's read-only accessor: the tunnel-up and
tunnel-down polls of record 21 section 6.5 (which read nothing on an
SHPC member, as before), the chained transaction (its candidates are
not members, so the writer would fail for them and turn "no reading"
into a failed one), the inference and its read-back (section 7.6.2 I3
refuses SHPC members), the status record's path, the recovery's set
record.

**When, and at which IRQL.** `PASSIVE_LEVEL`, on the
connection-manager thread, under no spin lock, where the learning
already runs: at an arm (the bind, a set change, after a tunnel down
this driver handled, before a recovery's rebuild) and at each learning
attempt of a pending port inside its window (section 7.6). The write
is a synchronous IRP to the PDO below the filter, so it does not pass
through the filter's own dispatch. The select, read and restore of one
reading run to the end on that thread, and the stor98 channel's reading
of the same bridge runs on the same thread, so no two of this driver's
readings interleave. Each reading of a 31-slot bridge is 33 writes and
as many reads.

**The reading owed.** The arm takes the evidence (section 7.6.4: each
member empty in the new baseline has its port read). On an SHPC member
that reading can be missing where a q35 port's never is: the member's
filter not yet published or not yet owned when the bind arms (the
filter's INF installed after the connection manager's, a bridge
restarted), or the slot still reading occupied just after a tunnel down
(the emulator empties the slot in its removal's last stage, record 21
section 7.1). With no attempt until a port is pending, the evidence
would then be missing at the next plug. So, under S1 to S3 only: after
an arm that completed, a member empty in the baseline with no evidence
bit is **owed** (`XhciTbtLearnOwed`), and while no port is pending the
idle pass reads the owed members again -

- at each idle pass until 2 s have passed since the arm (record 21
  section 6.5's poll bound; the tunnel-down poll's time for the same
  slot); and after that
- once each time a member's filter record changes: for each member,
  whether its filter is published and owned by this domain and that
  publication's serial, read under the registration's lock with no
  configuration access and compared member by member with the record
  taken before the last reading (taken before it, so a publication
  during a reading is tried at the next pass, never acknowledged
  untried).

The reading again is not an arm. An arm clears the armed mark before it
scans and leaves it clear when a read fails, and the next attempt of an
unarmed baseline arms against what answers then and consumes its gain;
a retry that could fail that way would cost a far end's learning that
needs no SHPC evidence at all. The refresh (`XhciTbtLearnEvidence`,
pure, new) keeps the baseline and the armed mark whatever happens: over
an armed baseline of this set, for each owed member alone, the
occupancy is read again; where that read is whole, the secondary bus is
the baseline's, nothing answers and the port then reads DOWN, the
member's evidence bit is set; anything else - a failed read, another
bus, a device answering, a port not DOWN - leaves that member as it
was, owed. It sets bits only, by section 7.6.4's own condition (the
port DOWN at a reading where nothing answered behind it); it spends
none and moves nothing, so the spending rules of section 7.6.4 stay
with the arm and the attempt. A device it sees answering on a member
the baseline has empty is left for the next arm or attempt to see as
they would have.

It is not made while any port is pending: an attempt that moves the
baseline reads the missing evidence itself (section 7.6.4), and a port
pending means a link may be coming up. A slot that stays occupied with
nothing answering behind it, or a writer that keeps failing, costs the
2 s of passes and then nothing until a member's filter record changes;
no pass reads configuration space for it after that.

**What proves a port, and what does not.** Unchanged in kind; the SHPC
reading only makes section 7.6.4's second kind of evidence available on
such a member.

- Proves: the router naming the port (section 7.6); one gain that is a
  tunnel's far end; one gain of another shape on a member whose port
  read DOWN while nothing answered behind it, nothing read answering
  since, and UP at the gain - for an SHPC member, every slot empty then
  and a slot occupied now.
- Does not: a slot reading occupied by itself, with no gain; a gain on
  a member whose slots were never read empty (a tunnel found up at
  start, section 7.6.4: no transition can be read of a tunnel already
  up; a card that was in the slot at the arm); a reading that failed in
  part (`UNREAD`, or rejected where a slot was already read empty,
  section 7.6.4's outcomes); an SHPC member that S2 or S3 does not
  admit; any reading on 98 SE or ME. Each of these leaves the port
  pending and its tunnel up until the unplug, as before: on 2000 and
  later the eject unmapped (`u4.eject.unmapped`) with the tools' "stays
  up until the device is unplugged" line (record 24 section 5.2); on 98
  SE and ME the eject's own transaction, whose text has no line about
  the tunnel.

**By Windows.**

- 2000, XP, XP x64 and Server 2003 x64, Vista, 7, 8, 8.1 and 10, both
  architectures: the same code and the same route. On an admitted set a
  plain enclosure plugged after the start is learned again, as it was
  before task 48.43 narrowed the rule; its eject closes its tunnel
  (`u4.eject`), tunnel changes act on its member, and the stor98
  statement of a closed path names its member where section 10.2 and
  record 21 section 7.2 qualify the target (2000 on x86, XP x64). The
  task names 2000 to 7 because the gap was read there; 8 to 10 have the
  same gap and the same route, the stor98 channel's reading has used
  this writer on every one of them since task 39-A.7, and a version test
  would keep the gap there for no reason. None of these systems drives
  the emulator's SHPC itself, as read: no SHPC driver on 2000 and XP,
  7 "does not drive SHPC" on i440FX (`usb4-emu`'s handoff, section 6a),
  8 and 8.1 "does not take the bridge's SHPC" (`runs/run-43.md`,
  43.23); so nothing of Windows' shares the select.
- On a set that is not admitted (every real host read so far: tunnel
  ports are PCI Express ports, members by the switch's ids, listed root
  ports, router-named or ACPI-named) nothing changes on any Windows:
  S2 fails on the discovery's record with no access made.
- 98 SE and ME: unchanged. No writer, so the SHPC plain enclosure stays
  unlearned there and its eject leaves the tunnel up until the unplug
  (the 9x eject says nothing of the tunnel; the notice is the NT
  watch's); 49.17's 9x reading is of that state.

**A tunnel found up at start** stays as sections 7.6.2 to 7.6.4 leave
it on every Windows: pending, its eject unmapped, the notice kept (on
2000 and later; the 9x eject has no such line),
unless `XhciCmInferAtStart` is 1 and I1 to I7 hold (never on an SHPC
set). A dock or an enclosure with its own PCIe switch plugged after the
start is a far end and was learned already, with or without this
reading. Under the firmware connection manager (record 21) there is no
learning and nothing here applies.

**Failures.**

- The writer fails (no published filter, not owned, the IRP refused, a
  short write): the reading is not made. At an arm the member gets no
  evidence bit and the arm is owed (above); at a gain with the evidence
  held the attempt is `UNREAD` and the baseline kept; with no evidence
  the gain is `SHAPE` as before. Never a binding that rests on the
  failed reading; a far end binds without any port reading, as before.
- The restoring write fails after the slots were read: the reading is
  not kept (`ERROR`); where every slot had read empty the gain is
  rejected, not left open (section 7.6.4). The select is then left at
  the last index this driver wrote. That harms no reader of the working
  register set through the bridge's memory BAR, and this driver's next
  reading saves and restores what it finds.
- Another agent using the same select between this driver's write and
  its read (firmware, or an operating system's own SHPC driver) would
  make a reading wrong in either direction. S3 keeps the writer to the
  one bridge where that was read not to happen; this is why no real
  SHPC bridge is admitted.
- A refresh that fails part-way changes nothing for the member it
  failed on: the baseline, its armed mark and every other bit stand.

**Whom it changes.** No real machine: S3 admits a bridge by QEMU's
vendor id only. In the emulator: the i440FX layouts whose every tunnel
bridge is an SHPC `usb4-tunnel-bridge`, on Windows 2000 and later, for
an endpoint directly behind the bridge plugged after the start (and
for one whose port this driver's own teardown proved down before a
re-plug). There the consumers of a learned binding act again as they
did up to `3.4.2.0`: the eject's teardown, the tunnel changes' choice
of member, the stor98 statements of section 10.2, the isolation
statement of section 10.5. The earlier incidents on this path, searched
in `runs/` and `issues/`: 39-A.5 (`runs/run-39.md`: the seam's poll has
no writer and reads nothing on the SHPC bridge; kept so), 39-A.7 (the
channel's own reading through this writer, on 2000 and XP x64, in use
since), 48.43 (the rule that stopped the learning there for want of
this reading), 48.35's S.1 (7 x86 on the SHPC bridge: the drive gone
after a standby; a resume path, not a reading, open as 49.11 and 49.12
left it). No incident names the select or the config write as a cause.

**The residual.** As section 7.6.4's: a device that is no tunnel's,
hot-added behind the emulator's bridge by the emulator's operator while
a port is pending alone with its own function not yet answering, would
carry the evidence.

**No registry value, no text.** The reading has no switch: it cannot
act on a bridge that is not the emulator's, so there is nothing for a
user to turn off. One log line is added, `u4.learn.shpc` (bit 0 the
writer given at an arm, bit 1 a member's evidence owed after the arm
or the refresh), logged when its value differs from the last one
logged. No tool or installer text
changes.

**As coded** (task 49.18, 2026-11-11). Pure, `tbt_topo.c`:
`XhciTbtShpcSet` (S2), `XhciTbtLearnOwed`, `XhciTbtLearnEvidence` (the
refresh) and `XhciTbtEvidenceDue` (its schedule); `XhciTbtPortRead`,
`XhciTbtLearnArm` and `XhciTbtLearn` are not changed. Driver half:
`cm_nhi.c`'s `cmU4LearnIo` builds the learning's accessor (a copy of
the seam's reader, with `CmRegConfigWrite` as its writer under S1 to
S3), `cmU4ArmRead` is the arm of `cmU4Arm` and of the recovery,
`cmU4Learn` reads through the same accessor, and `cmU4ShpcRefresh`
runs in the idle pass after an owed arm (`cmU4LearnPending`, after
`ArmOwed`'s arm and before the inference's pass); `cm_nvlinkos.c`'s
`CmNvlinkShpcBridge` is S1 and S3 (the stor98 channel's own test of
the bridge); `cm_reenum.c`'s `CmRegMemberRecords` gives each member's
filter record. Three words of state in the extension (`U4ShpcArmMs`,
`U4ShpcRecord`, `U4ShpcLogged`, the last the log's alone). The
refresh's first tests are S1 and S2, which read nothing, so off the
filter route or on a set that is not all SHPC an idle pass neither
reads nor locks anything for it. S3 is read again at each refresh the
schedule allows, after the member records are acknowledged: a bridge
whose admission could not be read at the arm is admitted by a later
refresh, and a set that is never admitted costs the 2 s of passes after
an arm and one try a publication, no more.

**Tests.** Host vectors (`test/test_tbt_topo.c`), for the pure
decisions the driver half takes from `tbt_topo.c`: the set's
eligibility (`XhciTbtShpcSet`: every member SHPC; a switch's, a listed,
a router-named member or an empty set refused); the owed members
(`XhciTbtLearnOwed`: an empty member without its bit owed, an occupied
one or one with its bit not, nothing owed of an unarmed baseline or of
one over another set); the retry's schedule (`XhciTbtEvidenceDue`:
inside the 2 s at every pass, after it only on a changed member
record - an ownership lost and a serial moved that leave a sum equal
included - never with a port pending, the clock's wrap). And the
sequences on the i440FX model: an arm whose writer fails (no evidence,
owed), the refresh with the writer working (the slot read empty), the
endpoint then learned; an arm with the slot still occupied and nothing
answering (owed), the slot emptied, the refresh, a re-plug learned; a
refresh whose occupancy read fails (nothing changed, still armed, a far
end then learned as before); a refresh that finds a device answering
(no bit, the baseline not moved, the device then `SHAPE` at its
attempt); a refresh of an unarmed baseline (nothing); a card in the
slot at
every arm never learned. Legs: 2000, XP and 7 x86 on the i440FX SHPC
bridge under the software connection manager - a plain enclosure
plugged after the start learned (`u4.learn` low byte `03`), its eject
closing the tunnel, an unplug and a second plug learned again; a dock
and an enclosure with its own switch ejected (far ends, learned as
before); the enclosure at power-on with the pre-boot connection
manager, its eject keeping the notice; the firmware connection
manager's hosts as they were; 98 SE and 10 read for no change.

**As read** (task 49.18, 2026-11-11; the `release` x86 `xhcicm.sys` of
`d53e848be`, installed with the `3.4.3.0` tools on overlays of the q35
bases; one vCPU, TCG; `runs/run-49.md`, "49.18").

- The i440FX SHPC bridge, software connection manager, the host model
  that answers no mapping (`u4.map.none`): on 2000, XP, 7 x86 and 10
  x86 every start logged `u4.learn.shpc=00000001` (the writer given,
  nothing owed). A plain enclosure plugged at the desktop was learned
  (`u4.learn=00000103`, "its PCIe connection reaches the port at PCI
  00:05.0") and its eject closed the tunnel (`u4.eject.bridge=00000028`,
  `u4.eject=00000100`, "The PCIe tunnel was closed."); unplugged and
  plugged again it read `u4.learn=00000102` and closed again (XP). The
  same plug with the released `3.4.3.0` `xhcicm.sys` on XP:
  `u4.learn=0000010F`, `u4.learn.expired`, `u4.eject.unmapped=00000028`
  and the notice. On 7 the first plug after the install restarted the
  host interface under the tunnel (`u4.carry.in` 1, `u4.preserved.own`):
  that instance found the tunnel up and kept the notice, and the next
  plug was learned and closed.
- A tunnel found up at start (the enclosure at power-on with the
  pre-boot connection manager on the i440FX bridge; the ASM4242 model's
  firmware tunnel on q35): pending (`u4.preserved`,
  `u4.learn.expired`), the eject stops the device and keeps the notice,
  on 2000, XP and 7. 10 x86 on the ASM4242 model read the same start
  (`u4.preserved=00010808`, `u4.learn.expired`); its eject was refused
  by Windows before any request reached the driver (a volume of the
  drive "did not allow it", three times), so the notice was not read
  there.
- q35, the ASM4242 model (members PCI Express ports; no `u4.learn.shpc`
  line): a dock (2000, XP, 7) and an enclosure with its own switch (XP)
  plugged after a start with nothing attached were learned as before
  (`u4.learn=00000103`) and their ejects closed the tunnel; not read on
  10. Where a unit had been there at power-on, its re-plug on the ACPI
  machines (XP, 7) is learned but its eject is dropped at the hand-off
  (`cm.reenum.eject.stale.why` bit 3: ACPI's bus filter stays on the
  ejected bridge's PDO, record 21 section 7.4 item 1, open); the
  released driver reads the same on XP, and 2000 on the Standard PC HAL
  closes the tunnel there.
- The firmware connection manager's host (the Alpine Ridge model), a
  dock at power-on, 2000, XP and 7: "Windows has stopped ...",
  `cm.reenum.eject.tunnel-left-up`, as before.
- 98 SE on the i440FX bridge: the plain enclosure is not learned
  (`u4.learn=0000010F`, `u4.learn.expired`), the eject stops the drive
  and the tunnel stays up until the unplug; the released driver reads
  the same. The 9x eject's text carries no line about the tunnel (the
  notice is the NT watch's, record 24 section 5.2).

Not read in a guest: the refresh (`u4.learn.shpc=00000003`, then `1`).
`u4.learn.shpc` never read `3`: at the starts with nothing attached
the filter was published and owned before the bind's arm and the plug
that followed was learned, and after each tunnel down the next plug was
learned too, so no arm was seen to leave a member owed; at the starts
with the enclosure at power-on the slot was occupied, which owes
nothing. The refresh stands on the host vectors alone.
Not read: XP x64 and the other x64 systems (the same code; the stor98
statement's member there), 8 and 8.1, ME.

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
  the removal debt and the barrier (decision U22). On Windows 98 SE and
  ME every member is so marked at the containment, whose removal waits
  for the commit (record 21 section 6.1.2, task 44.54).
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
the entry unchanged either way; since task 44.54 that removal waits for
the commit and a closed path, record 21 section 6.1.2); the bridge restored with the host router reset (the
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
real USB4 BIOS booting a CSM, unread when this was written
(`PLATFORM-MATRIX` section 4), and the ASM4242's firmware (section 18)
builds a PCIe tunnel for an NVMe enclosure attached at power-on and
hands over without tearing it down (section 18.3).

Windows 11 cleans such a fabric and rebuilds every tunnel [obs]; Linux does
by default, through Downstream Port Reset on every downstream lane adapter
and the deactivation of every path entry on a version 1 host, and through
Host Router Reset on a version 2 host ([linux] `tb.c` `tb_start`,
`switch.c` `tb_switch_reset_host`, `nhi.c` `nhi_reset`; the ASM4242's
host router is a version 1 one, section 18.2), and keeps the
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
- **Discovery only reads**, but for one write since task 45.20: the
  Enable Gen 4 Link Recovery bit cleared where it reads set (section
  6.2.3 item 4). No other write during discovery changes an existing
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

**Once a boot (task 47.32, a design put to the owner).** On 7 x86, on a
cold start, Windows stops and starts the NHI's stack once after the first
start (task 47.21's legs; 45.58 read the same restart). The successor
finds the tunnel its predecessor has just rebuilt, matches it to the
predecessor's record (section 9.4, `Own`) and, value 2 cleaning every
preserved port, cleans it again: the drive behind it goes and comes back
twice (`runs/run-47.md`, "47.21"). Value 2 is meant for what was there
before this driver; the design below makes it clean this driver's own
tunnel once, as section 9.3.1 sets out with the options.

#### 9.3.1 The rule, and the options

The rule is all or nothing for the start: a start with value 2 cleans
nothing (it leaves every port as found, as value 0 would) when **all**
of the following hold;
if any fails, the start cleans exactly as before, every port with tunnel
state, through the Host Router Reset on a version 2 host. Leaving a whole
start alone, never one port beside a cleaned one, keeps the domain reset
and the per-port sequence as they are.

1. **The record says this boot's value 2 cleaning has run.** A new field
   of section 9.4's record, `Cleaned`, which an instance hands on only
   when it ran at value 2 and either ran the cleaning or was itself left
   alone under this rule. An instance at value 0 or 1 hands no
   `Cleaned`, so 2, then 0, then 2 cleans at the last start. No record
   (none handed, its age past `CM_U4_CARRY_MS`, its slot emptied by a
   surprise removal, a real reboot) means the cleaning has not run.
2. **Every port's tunnels are its own** (section 9.4): no port HELD,
   with or without tunnel state (a HELD port is always cleaned, and on a
   version 2 host that is the domain's reset); on every port with any
   tunnel state, the PCIe tunnel `Own`, a USB3 tunnel up and preserved
   `Own` too, no DisplayPort tunnel preserved, its discovery complete
   (`Discovered`), no host-side entry quarantined and no sweep owed.
3. **Nothing else on either router.** A read-only inventory of the
   host router's adapters of each port with tunnel state (both lane
   adapters, its PCIe Down and USB3 Down adapters, each DP adapter) and
   of the whole depth 1 device router (every adapter, its upstream and
   downstream lanes and its protocol adapters alike), each adapter's
   path entries from HopID 8 to its Max Input HopID and each protocol
   adapter's Path Enable: the Valid entries are exactly those of the
   port's own verified tunnels, and the enabled protocol adapters
   exactly their ends. An extra Valid entry, an entry left pending, an
   entry that could not be read, or an enable no owned tunnel explains,
   anywhere there, fails the test: it is what this driver's sweep would
   have cleared, a failed child's leftover included (a child that has
   since gone or locked leaves no link, but may leave its parent's
   protocol adapter or lanes holding entries).
4. **No chained link anywhere.** The record names no kid, and discovery
   found no link on any port. A chained child's adoption runs later, at
   the poll (record 34 section 6.9), with checks no start-time read
   repeats, so a domain with a chain is cleaned as before, whatever its
   children's state: a half-built child is cleaned, at the cost of a
   second cleaning of a chain's tunnels when the stack restarts.

What this gives and what it does not. It removes the second cleaning
that 47.21 read: a restart of the NHI's stack moments after the first
start, the predecessor's tunnels matched whole. It is bounded by the
record: a restart more than `CM_U4_CARRY_MS` (120 s) after the
predecessor's stop, or after a surprise removal, cleans again, because
no ownership evidence survives it (the record is also what section 9.4's
own matching needs). The record lives in the driver's memory on every
system. On NT the image stays loaded for the boot once its callbacks are
registered (`cm_nvlinkos.c`: no `DriverUnload` then), so a disable and
enable within the age keeps it; a real reboot clears it. A resume
(section 11) is not a start: its verification and rebuild are unchanged,
and a hibernation's restored memory changes nothing about that; a stack
rebuilt after a failed D0 had its slot emptied by the surprise removal
and cleans as a first start. The same code runs on 98 SE and ME.

The options put to the owner:

- **(a) The record's flag and the four tests, the start left whole or
  cleaned whole** (recommended): no new storage, nothing new written to
  the registry, a chain or any doubt cleaned as today; the second
  cleaning is gone for the case read, a restart within 120 s.
- **(b) The same, with the record's age raised for this purpose** (the
  record kept for the boot while `Cleaned` is set): "once a boot" for a
  later restart too, at the cost of matching tunnels against a record
  that may be many minutes old (section 9.4's matching reads every
  identity again, so a replaced device does not match, but the record's
  age was chosen as a bound on that).
- **(c) No change**: document that value 2 may clean again when Windows
  restarts the stack, and that value 1 cleans only what does not verify.

Taken: option (a) (owner, 2026-11-06). Coded in `tbt_u4cm.c`
(`tbtU4PreOsOnce`, its inventory `tbtU4OnceInventory`, `Cleaned` in
`TbtU4GetCarry`), logged as `u4.preos.once` (1 left whole, else the
first test that failed in bits 15:8 and its port in 7:4), with no
counter of its own (the counter block and its names are XHCITR's too);
host vectors `test_u4cm` `test_preos_once`, and since task 48.8
`test_preos_once_more`, each reading the `u4.preos.once` line: a port
whose USB3 tunnel's record does not match (test 2), port 1 its own and
a firmware's tunnel on port 2 (test 2 at port 2, both cleaned), a path
entry the inventory cannot read and a Path Enable on the device's PCIe
Down adapter (test 3), and a version 2 host interface, left whole with
no Host Router Reset and, with that enable, cleaned through one. A port
with `HostResidual` fails test 2 whatever its tunnel state.

Cleaning one port, the Linux and Windows 11 sequence restricted to it:

1. The orderly PCIe teardown of Guide 5.3.2, both ends reachable: Path
   Enable 0 on the host's PCIe Down adapter, then on the device's PCIe Up;
   Extended Encapsulation cleared where section 6.2.2 item 7 says; every
   Valid entry on the port's path (host PCIe Down HopID 8, host lane
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

### 9.4 A tunnel an earlier instance of this driver built

Task 44.26 (planned as RESUME.1), found by task 43.71
(`runs/run-43.md`, "43.71", runs 1 and 2). Section 9.2 cannot tell a
tunnel that a firmware or another connection manager left from one that
this driver's own earlier instance on the same NHI left: both are found,
verified and preserved. Section 7.6 rebuilds across a sleep only a tunnel
this driver built, so a tunnel it built, then found again as preserved
after a PnP stop and start of the NHI, was not rebuilt when the domain
lost its configuration across a hibernate. On 7 x86 in the emulator,
with the enclosure attached at boot, Windows removed and added the NHI's
function driver once after the boot; at the resume the port was HELD and
the disk reported missing. This section carries what the earlier
instance knew across that restart.

**The record.** At its stop (a PnP stop or removal, section 11) an
instance hands its successor on the same NHI a record of the tunnels it
owns:

- the host router's vendor and device;
- per port whose device router is present with its UUID read: the
  port's lane 0 adapter; the device router's vendor, device, kind (USB4
  or Thunderbolt 3) and UUID; and, for each of its PCIe and USB3 tunnels
  that is up and owned - built by this instance, or itself matched under
  this section - its lane HopIDs `Hd` and `Hu`.

An instance hands a fresh record only while its view of the fabric is
current: it reached its running stage, and since its start it never
failed (at start, or at a resume - a resume that left an obligation
unmet counts) and was never contained (section 7.7) -
a latch, so an instance that recovered from a containment hands none -
and it is not suspended or inside a resume or a recovery when it stops.
The disposition is latched while the instance runs, since its stop
clears the stage. An instance whose start never reached the host router
(its rings refused before activation, so no router was read and no
fabric configuration attempted) hands on the record it was given,
unchanged and with its original stamp (below), so that forwarding never
makes a record younger. Any other instance hands none, and its successor
finds every tunnel as section 9.2 says.

**Where it is kept.** In the image, not in the instance: four slots,
each keyed by the NHI's physical device object and its configuration
space's vendor, device and subsystem vendor as read at the start, and
stamped with the tick count when a running instance wrote it (a
forwarded record keeps the stamp it had). The unbind writes its NHI's
slot (an empty record empties it). Every start of that NHI takes the
slot and empties it, whatever the start then does, so an ICM start, an
inert start or a start that never activates this profile discards it;
only an activation of this profile hands it to the core. A surprise
removal of the NHI empties its slot even when the instance had already
stopped. A record older than 120 s at the take is discarded: no
reference is held on the physical device object, so the stamp, the ids
and the match below are what stand between a record and a device object
reused at the same address (an NHI removed while no instance of this
driver ran). The record lives no longer than the image: for the boot on
98 SE and ME (no `DriverUnload`), and on NT while any device object of
this driver - a bridge filter among them - keeps the image loaded. An
image unloaded between the two instances, a reboot, a restart that takes
longer than 120 s (a Device Manager disable left a while, say), a change
of the NHI's ids or a full table loses it, and the successor preserves
as section 9.2 says. No registry value and no pool.

The identity of "the same NHI" is therefore a heuristic, not a proof,
and its false positives are accepted: a different NHI of the same ids
whose device object took the old one's address within 120 s, or another
connection manager configuring the NHI while no instance of this driver
ran on it (another driver installed and started on it within 120 s), with
the same enclosure, the same device router and the same HopIDs. What a
false positive costs is said under "Why it is safe" below.

**What the successor does with it.** Discovery is unchanged and still
only reads. Where section 9.2's outcome is "all four" (the port UP and
preserved), the port is checked against the record as a whole, because
section 7.6's rebuild of a port that lost its configuration rebuilds the
port, not one tunnel: the host router found already enumerated, with the
recorded vendor and device; an entry for the same lane 0 adapter; the
same device router vendor, device, kind and UUID, read by this
discovery; the PCIe tunnel recorded up at the same `Hd` and `Hu`; a USB3
tunnel up on the port recorded up at its own `Hd` and `Hu` (none up and
none recorded is a match too); and no preserved DisplayPort tunnel on
the port, proven rather than assumed. A USB3 side that is not up must
read empty: no host or device entry found for it, none unreadable,
both USB3 protocol adapters with Path Enable clear and HopID 8 neither
Valid nor pending, and on both routers' lane adapters no Valid entry
leading to them and no entry left pending. No DisplayPort entry on the host leads to the port's
lanes, every such entry read successfully. The match is made only after
the port's discovery completed (section 7.7's `Discovered`: no transport
failure inside it, the budget not spent), and its own reads are judged
the same way: one that fails is no match. Only when all of these hold
are the port's tunnels marked as this driver's own; one mismatch, a missing field or no record leaves
every tunnel on the port preserved as someone else's. An own tunnel
stays preserved for every other rule of this record - hot plug, lanes,
the TMU and credits left as found, its teardown the registers', section
7.7's recovery as for any preserved tunnel - and section 7.6 counts it
as built here: its suspend obligation is rebuilt where the domain lost
its configuration across the sleep. The mark is kept through a resume's
verification and goes with the tunnel: its teardown, a rebuild (which
makes it built here) or a cleaning (section 9.3) clears it. Logged once
per port as `u4.preserved.own` (the port in bits 16 and up, bit 0 PCIe,
bit 1 USB3).

Only the "all four" outcome is matched. A tunnel on a HELD port is left
as found whatever the record says, and so is a USB3 tunnel left alone on
its port after its PCIe tunnel was ejected or failed: section 9.2 finds
such a port HELD, so a sleep that loses its configuration leaves it HELD
as before, even where the earlier instance recorded the USB3 tunnel.

**Why it is safe.** The match can only err towards rebuilding. A false
match needs, within 120 s and between the two instances, either the
heuristic's false positive above or, on the same NHI with no connection
manager running, another configuration through the same device router
(the same UUID) at the same HopIDs - and with no connection manager
running nothing serves a hot plug, so a router re-plugged meanwhile
comes back unenumerated and carries no tunnel. Even a false match costs
only what this driver does for every port it built: at a sleep that
lost the domain's configuration, the port's host side closed and the
port rebuilt through sections 6.4 to 6.9, where without the match it
would have been HELD and its devices reported missing. Nothing is
torn down or rebuilt by the match itself, and a configuration that
survived the sleep is verified, not rebuilt, as for any port. Every
other failure of the record (lost, refused, unmatched) is the behaviour
without this section.

Not changed by this section: section 9.2's discovery, a preserved
tunnel's teardown, the resume's verification (section 9.2's reads),
section 7.7's recovery, and the release notes' standby and hibernation
limitation, which rests on task 43.71's native hot-plug reading and on
decision U15 as before.

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
the 9x statement once each system's G3 ordering is read (both read and
admitted on 2026-10-22, task 43.18, `runs/run-43.md`).

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
enumerated the endpoint (task 42-A.25). Read on Windows ME (the bench zip
of 2026-10-19):
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

### 10.5 The isolation statement to xHCI98 (task 45.35)

This is a second consumer of section 7.3 step 1's read-back, beside
Stor98's statement. Its purpose and the owner's decision are in record 17
section 4.13.6: a USB4 port whose PCIe path this connection manager closed
and read back closed proves that no function behind that port's tunnel
port can reach host memory. xHCI98 takes that as the proof that lets a
contained controller's transfers complete.

**The word it reads.** The domain's driver fields are written by the
connection manager's thread without a lock, and the path is enabled
(`tbtU4PcieTunnel`) before any `TUNNEL_UP` reaches the seam. So the
statement is one word per USB4 port, kept in the core:
`TBT_U4CM.IsoWord[port - 1]`, either 0, or `TBT_U4_ISO_VALID` together
with the low 24 bits of the connection generation. It is kept apart from
the port records, which the snapshots and the resume copy whole, so no
copy can bring an old word back.
- **Set.** The core sets it in `tbtU4LinkDownUpcall` when step 1 read
  back closed (`PathClosed`), before the driver's upcall.
- **Revoked.** It is written 0, by a single aligned store, before
  anything that can enable a PCIe path or reinitialize the core: at the
  top of `tbtU4PcieTunnel`, the only writer of a PCIe Path Enable set
  (`tbtU4UndoEnable` and `tbtU4Reserve` only clear or hold one). For every
  port it is cleared by `TbtU4Init`,
  `TbtU4Start`, `TbtU4Resume`, `TbtU4Recover`, `TbtU4Stop` and
  `TbtU4Contain`.

The reader reads the word once. A yes answered just before a revocation
stays true. The revocations other than `tbtU4PcieTunnel`'s open nothing.
That one opens a new tunnel, which reaches only an endpoint reset since
the closure: the port reconnected through Detect, the gate of section
10.1 (MD-2's obligation 4). Section 10.1's reset is what keeps a granted
proof true after a path reopens.

**The binding.** The binding is taken when the interface is asked, at the
controller's start. It records:
- the domain, by its NHI object and `Reenum.Starts`;
- the member, by its tunnel port's location;
- for every USB4 port whose PCIe tunnel is up at that moment, its
  connection generation (`Pcie.Up`). The generation is read on both sides
  of `Up` (`TbtU4PortPcieGen`), so an `Up` and a generation of two
  connections are never paired.

The port itself is not named at the binding. The member's correspondence
may be learned only after the controller's start: a 2000 leg on the gated
release bound nothing for that reason. So the port is named at each ask,
by the correspondence then (`XhciTbtPortMember`, the same source the
Stor98 statement uses). The proof then holds only for the generation that
port carried at the controller's start.

A domain restart changes `Reenum.Starts`, and a reconnect changes the
generation. Either makes every later answer no. The binding is immutable
while xHCI98 holds a reference, and it is retired when the last reference
is released, so the next start binds afresh.

**The answerer.** It is `xhcicm`'s kernel-attached child filter on the
controller's PDO (record 31 section 4.3), linked below a member of a
domain's tunnel-port set (`CmRegAttLink`). It answers
`IRP_MN_QUERY_INTERFACE` for `GUID_XHCI_CM_ISOLATION` (`src/xhci_cmiso.h`,
shared by both drivers) on NT 5.x only, when the binding can be taken;
otherwise the IRP passes down untouched. `InterfaceReference` and
`InterfaceDereference` count on the filter (`IsoRefs`); no object
reference is taken, because xHCI98 releases the interface before its own
`REMOVE_DEVICE` goes down to this filter, so the filter outlives every
call (record 17 section 4.13.6). The binding marks the filter gone at its
`SURPRISE_REMOVAL` and `REMOVE_DEVICE`, and a call after that answers no.

**The answer.** `Isolated(Context)` is called at `PASSIVE_LEVEL`. Under
`cmRegLock` it finds the bound domain in the domain list, with the same
NHI object and the same `Reenum.Starts`. It returns 1 only when every one
of these holds; the pure decision is `CmIsoDecide` (`cm_iso.c`, host suite
`test_cm_iso.c`):
1. the child is not removed;
2. the domain is listed and unchanged, runs this connection manager, is
   started, and its host interface is not lost (`Live.State` active, no
   `LossDrain`);
3. exactly one USB4 port corresponds to the bound member, read now
   (`XhciTbtPortMember`; none, several, or a conflicting correspondence
   refuses);
4. that port carried a PCIe tunnel at the binding, and its `IsoWord` is
   `TBT_U4_ISO_VALID` with that tunnel's generation.

No spin lock is held across a transport call or a PnP call. Each call is
counted (`cm.iso.asked`, then `cm.iso.yes` or `cm.iso.no`), and the
verdict with the first condition that failed is logged (`cm.iso`). The firmware connection manager's client answers nothing,
because it has no read-back (section 7.3 is this connection manager's).
98 SE and ME have no such interface.

**What it is not.** It is not Stor98's statement:
- it waits for no acknowledgement;
- it is withheld for no deadline (section 10.3's 3 s is Stor98's
  latency budget, not a condition of isolation);
- it is not qualified per OS ordering (section 10.2), because what it
  proves is the closed path, not the endpoint's reset or the order of
  events.

A port whose step 1 did not read back (`PathClosed` clear) never answers
yes. Its controller keeps record 17's limitation.

## 11. Stop, remove, disable, suspend, resume

- **Stop, remove, disable**: the rings stopped (TX and RX ring control
  cleared [ref] `u4cm_stop`), queued work drained, the thread ended, the
  common buffer freed. Nothing on the fabric is changed but one bit:
  before the rings stop, Enable Gen 4 Link Recovery is cleared where
  this driver may have set it - both ends of an UP or FAILED port, the
  host's end alone of a HELD one (section 6.2.3 item 4 parts 3 and 6) -
  since no connection manager then runs to verify a recovery; not on a
  contained instance, which writes nothing. Tunnels stay up
  and the devices behind them keep working, as under the ICM client (record
  21 section 5.6) and as Linux leaves non-DMA tunnels at unload ([linux]
  `tb.c` `tb_stop`). Force power follows section 4.2's ownership record.
  Routers keep
  retransmitting unacknowledged hot plug events until the next start
  (Guide 4.3), which discovers and preserves the fabric (section 9.2).
  A stop also hands the next instance on the same NHI section 9.4's
  record of the tunnels this one built, so that they are still rebuilt
  across a sleep. Decision U7.
- **Shutdown**: the same.
- **System suspend** (D3 for S3 or S4): the NHI marks its tunnel-port
  set's active members held (section 7.6), then the same as stop, the
  Enable Gen 4 Link Recovery clear included (Guide 8.2.1 clears it before
  a sleep); after the resume the tick sets it again on each port that
  qualifies (section 6.2.3 item 4 parts 2 and 6). The
  Guide's sleep entry (chapter 8.2: Enter Sleep, Sleep Ready, the wake configuration) is
  not in the first cut; a domain that loses power across the sleep comes
  back with its device routers uninitialised. Section 11.1 (task 48.35)
  designs that entry and the resume after it; not coded yet.
- **Resume to D0**: section 4.2's force power and LTR where they apply, the
  host interface and rings, then section 7.6's restoration of every
  resume obligation (each tunnel up at suspend, whatever its port's state)
  - verified and preserved, rebuilt where the domain lost its
  configuration, torn down on removal evidence, or HELD on a failed
  verification - and the NT filter's marks released and the bridges
  invalidated, on success or failure (section 7.6; no D0 is held since
  task 43.24h); only then are other ports'
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

### 11.1 Sleep with a device attached (task 48.35; a design, reviewed and revised; the owner's answers of 2026-11-09; coded)

Every release so far leaves standby and hibernation with a Thunderbolt or
USB4 device attached unsupported (the release notes, "Thunderbolt and
USB4: known limitations"; the README's "stop and unplug" line). Task 48.35
(owner, 2026-11-09) asks for the Connection Manager Guide's sleep entry on
this profile, the firmware connection manager's own sleep and resume, the
integrated hosts through a standby, the tunnels back before Windows looks
again at a native PCIe hot-plug port, and the first shutdown that stalls on
XP x64 and 7 x64 after a hibernation with a tunnelled drive. This section
is the design for all five, for both profiles (record 21's ICM client
shares the power path, `cm_power.c`, and the tunnel-port filter), what it
cannot do, and the questions it left to the owner (section 11.1.11, answered
2026-11-09). It is coded (section 11.1.13); `XhciCmSleepEntry` is 1 since the
emulator legs passed (section 11.1.12), 0 the behaviour of sections 7.6 and 11
above.

Since 3.5.0.0 (task 49.46) a drive Stor98 runs whose wake D0 failed against
the absent tunnel is asked to power up again after the resume's release,
under this profile as under the firmware connection manager's (decision
U15 covers both, and the release is the same): design record 21 section
6.3.1.

#### 11.1.1 What ships, and where the limitation is written [code]

- **Nothing refuses a sleep.** `cm_power.c` passes every power IRP but
  `IRP_MN_SET_POWER` down untouched, `IRP_MN_QUERY_POWER` included; no
  installer or tool text asks the user to do anything at a sleep.
- **At a system sleep** (the NHI's D3 for S1 to S4, `CmNhiQuiesce`): the
  tunnel-port set's held marks (`CmReenumSuspend`), then under this
  profile `TbtU4Suspend` - every peer logged out (record 35 section 3.8),
  every tunnel up recorded as a resume obligation, Enable Gen 4 Link
  Recovery cleared (section 6.2.3 item 4 part 6), the rings stopped;
  nothing else is written to the fabric. Under record 21's ICM rows:
  mailbox `0x05` then `0x07` (Falcon Ridge, Alpine Ridge), `0x07` alone
  (Titan Ridge, Maple Ridge), nothing on the ICL row (Ice Lake to Meteor
  Lake under their firmware). The link-controller mailbox Linux sends
  there is coded (`TbtHwPlatformSuspend`, `tbt_hw.c`) and called by
  nothing.
- **At the resume**: section 7.6 - each obligation verified, rebuilt where
  the domain lost its configuration and the tunnel is this driver's own
  (or section 9.4's match), torn down on removal evidence, else HELD; the
  marks released and the bridges invalidated whatever the outcome
  (decision U15).
- **Where it is written**: the README ("Things to know", first bullet;
  "Known limitations", Thunderbolt and USB4, first bullet); the release
  notes' "Thunderbolt and USB4: known limitations" (the standby paragraph
  with its native hot-plug and XP x64 / 7 x64 shutdown sentences) and the
  `3.3.0.0` entry for task 44.26; record 21 sections 1.4.3 (the integrated
  row), 1.4.4 (the ICL row's system suspend), 5.6, 6.3 and 12; this
  record's sections 7.6, 11, 15 (U7, U15), 16 and 17; record 34 section
  6.9; record 33 section 7.6 (its leg R11).

#### 11.1.2 What is read already [run]

- **No D0 is held** (task 43.24h, record 21 section 6.3): a retained bridge
  D0 deadlocked the NT resume; every resume now releases the held marks
  and invalidates the bridges after the NHI's own resume.
- **Hibernation on a tunnel bridge Windows does not drive natively**
  (the i440FX emulator's SHPC bridge): 2000, Vista x86, 7 x86 and x64, 8
  x86, 8.1 x86 and 10 x86 resumed with the drive and its files and shut down in
  under a minute, the tunnel rebuilt by this profile (`runs/run-44.md`,
  "44.27", first and second parts); on q35, with a tunnel the emulated
  firmware built again at the boot (the Titan Ridge profile's
  `preboot=on`, the ASM4242 model's pre-OS tunnel), Vista x64, XP x64 and
  7 x64 the same ("44.27 (continued)").
- **A native PCIe hot-plug tunnel port** (task 43.71, `runs/run-43.md`,
  "43.71"; the q35 `usb4-tunnel-port`, 7 x86): at the resume Windows' PCI
  driver read the slot empty and turned it off (Slot Control `0x17FB`,
  Slot Status `0x0010`) before the tunnel was back; the drive was reported
  missing although this profile's resume succeeded, and a file whose
  directory entry was still in the file system's cache at the hibernation
  was lost. On q35 under the ICM profile without a pre-boot tunnel the
  same loss followed on 7 x64 ("44.27 (continued)", "Two readings on the
  way").
- **The XP x64 and 7 x64 shutdown stall** ("44.27 (continued)"): it
  follows `nvme98`, Stor98's older and unreleased version, on the drive,
  with or without this package installed; with this package's Stor98 on
  the drive no shutdown after a resume stalled, on seven readings.
- **Every reading above is a hibernation.** No standby (S3) leg has run
  under this package: the q35 bases' display adapter vetoes standby
  (43.71's `powercfg /a`). `usb4-emu` records Windows 11 reaching S3 on q35
  with `-vga none -device virtio-gpu-pci` and `ICH9-LPC.disable_s3=0`
  ([model] `usb4-emu` `docs/ROADMAP.md`, the Windows 11 suspend and resume
  item of 2026-10-23); task 48.35's legs take that route where the base
  allows it.

#### 11.1.3 What the specifications ask [spec]

- **Entry** (Guide 8.2.1, page 89): the connection manager sets the wake
  events, disables CLx, clears Enable Gen 4 Link Recovery, verifies that
  each Target Asymmetric Link matches the actual link, then runs the
  sequence of 8.2.1.2.
- **The wake configuration** (Guide 8.2.1.1, Table 8-1, pages 89 and 90):
  Enable Wake on Connect and on Disconnect only on a downstream port
  (DFP), each by its USB4 Port is Configured bit; Enable Wake on USB4 Wake
  "must be set to 1b in all UFP", optional on a configured DFP; Enable
  Wake on Inter-Domain only on a DFP of an inter-domain link; Enable Wake
  on PCIe, USB3 and DP in device routers. A router with any wake event
  enabled needs Enable Wake on USB4 Wake on every port up to the host
  router and USB4 Port is Configured on both ends of its link.
- **The sequence** (Guide 8.2.1.2, pages 90 and 91): (1) wait for the
  connection manager's own sideband transactions; (2) Enter Sleep
  (`ROUTER_CS_5` bit 0) in every router of the domain, in any order; (3)
  a version 1.0 router's Sleep Ready (`ROUTER_CS_6` bit 0) polled, a
  version 2.0 router's taken from its notification (`ROP_CMPLT`, Event
  Info `03h`) and read, or polled; "within 50 milliseconds", a 500 ms
  timeout recommended; (4) and (5) inform the host system, or have the
  host router send `LT_LRoff`, "implementation specific". After Enter
  Sleep the connection manager finishes the sequence, initiates no
  sideband transaction and writes neither USB4 Port is Configured nor USB4
  Port is Inter-Domain. A note asks that a router's internal USB and PCIe
  functions be in U3 and D3 before Enter Sleep.
- **The router's side** (Spec 4.5.1, pages 243 and 244): a host router
  sleeps on an implementation-specific signal; a device router on a PERST
  Active tunnelled packet or an `LT_LRoff` on its upstream port, and never
  unless Enter Sleep was set before it. Sleep Ready within tSetSR (Table
  4-73, page 250: 50 ms; 80 ms with a Thunderbolt 3 compatible sideband
  channel), unless the router waits for a Notification Acknowledgment.
  At the sleep event a port with USB4 Port is Configured clear (and not
  inter-domain) disconnects; an inter-domain port without wake on
  inter-domain disconnects; any other sends PERST Active on its enabled
  PCIe paths and `LT_LRoff`, and goes to CLd.
- **What survives** (Spec 4.5.2, Table 4-71, pages 244 and 245): "On entry
  to sleep state, a Router shall restore all Configuration Spaces to their
  default values"; it keeps, apart from them, the Upstream Adapter, the
  Configured and Inter-Domain bits, the wake enables, the target link
  speed and asymmetry, and a few link indicators.
- **Exit** (Spec 4.5.4, page 246; Guide 8.2.2, pages 91 and 92): on an
  upstream disconnect or a Wake on USB4; every adapter that reaches CL0
  sends a hot plug packet with UPG 0. The connection manager then
  reconfigures each router (Guide chapter 3) and sets up again every path
  that was there before (chapter 5); a USB3 path is disabled or set up as
  a new path.
- **A neighbour asleep** (Guide 8.2.3, Figure 8-1 and Table 8-3, pages 92
  to 94): the sleeping domain's `LT_LRoff` reaches this domain's port as a
  hot plug packet with UPG 1 while Router Detected stays 1; a disconnect
  reads Router Detected 0; the neighbour's wake starts link initialisation
  again with UPG 0.
- **The registers** (Spec 8.2.1.1, pages 359 to 361; 8.2.2.4, pages 395
  to 397): `ROUTER_CS_5` bit 0 Enter Sleep, bits 1 to 3 Enable Wake on
  PCIe, USB3 and DP (hardwired 0 in a host router); `ROUTER_CS_6` bit 0
  Sleep Ready, bits 2 to 4 the wake status; `PORT_CS_19` bit 16 Enable
  Wake on Connect, 17 on Disconnect, 18 on USB4 Wake (default 1), 19 on
  Inter-Domain; `PORT_CS_18` the four port wake status bits.

#### 11.1.4 What Linux and the emulator do [linux] [model]

- **Suspend** ([linux] `switch.c` `tb_switch_suspend`, `usb4.c`
  `usb4_switch_set_wake`, `usb4_switch_set_sleep`): CLx disabled, plug
  events off, the children first, wake enables only where the device may
  wake the system (none otherwise), then Enter Sleep and Sleep Ready
  waited 500 ms, on every router, the host router last. A Thunderbolt 3
  router gets `lc.c` `tb_lc_set_sleep`: bit 31 (`TB_LC_SX_CTRL_SLP`) of
  each link controller's `SX_CTRL` (generation 2 and later), no Sleep
  Ready, and `tb_lc_set_wake`'s six wake bits.
- **Resume** ([linux] `tb.c` `tb_resume_noirq`): every router read again
  and kept only with the same UID, the children configured again, the
  tunnels a firmware or a restore kernel built across a hibernation torn
  down, then Linux's own tunnels built again and 100 ms waited.
- **The integrated hosts** ([linux] `pci.c` `icl_nhi_suspend_noirq`,
  `icl_nhi_resume`): at a sleep through the firmware, under the ICM, the
  link-controller mailbox `GO2SX` (wake armed) or `GO2SX_NO_WAKE`, 500 ms;
  under a software connection manager nothing; at the resume force power,
  firmware ready and LTR again.
- **The ICM** ([linux] `icm.c` `icm_suspend`, `icm_probe`): `0x05` (save
  devices) on Falcon Ridge and Alpine Ridge only, then `0x07`.
- **The order** ([linux] `tb.c` `tb_apple_add_links`, `acpi.c`
  `tb_acpi_add_link`): device links make the tunnelled PCIe ports resume
  after the NHI, so the tunnel is back first; the platform names the pair
  through the ports' `usb4-host-interface` property ([ms-usb4acpi],
  section 7.6.1). No such order exists on 2000 to 10 for this driver
  (record 21 section 6.3).
- **The emulator** ([model] `usb4-emu` at `a0dcdd5`, read only): a router
  sets Sleep Ready at once when Enter Sleep is written
  (`hw/usb4/usb4-router.c`); the machine's S3 wakeup reset returns every
  router's configuration to its defaults but keeps a router that stayed
  attached as the same router, so a tunnel the connection manager
  restores to it is a link retrain, with Data Link Layer Link Active
  cleared and Presence Detect kept (`hw/usb4/usb4-fabric.c`
  `usb4_fabric_wakeup_reset`; `docs/ARCHITECTURE.md` section 4.4); the
  emulated firmware CM (`preos-cm`) runs again after a power-on; and its
  ICM tunnels again, within that wakeup reset, every device saved by
  mailbox `0x05` ([model] `docs/ICM-PROTOCOL.md` R34, from the P14s
  capture: the NVMe answered 0.6 s before the driver ready, so the
  firmware had rebuilt the tunnel itself [obs]).

#### 11.1.5 The sleep entry under this profile (Guide 8.2.1)

At the NHI's D3 for a system sleep (S1 to S4; never at a shutdown, never on
a contained instance, and only with `XhciCmSleepEntry` 1, section 11.1.10),
on the thread, after the peers' logouts and the obligations are recorded
and before the rings stop. **Each router's register is its type's**, as
Linux chooses per router, the host router included ([linux] `switch.c`
`tb_switch_suspend`, `tb_switch_set_wake`): a USB4 router's
`ROUTER_CS_5` and `ROUTER_CS_6` and its USB4 ports' `PORT_CS_19`; a
Thunderbolt router, host or device (section 19.4 item 1: no USB4 router
registers), its link controllers' `SX_CTRL` from generation 2 ([linux]
`lc.c` `tb_lc_set_wake`, `tb_lc_set_sleep`), and nothing at all on
generation 1 (Light Ridge, Cactus Ridge: every `tb_lc_*` returns early
there, section 19.4 item 3). "Enter Sleep" below means `ROUTER_CS_5` bit
0 on a USB4 router and `SX_CTRL` bit 31 in each link controller of a
Thunderbolt router of generation 2 or later.

1. **Sideband.** This driver issues no sideband transaction (no port
   operation is coded), and its router requests are synchronous on the
   thread, so step 1 of Guide 8.2.1.2 has nothing to wait for.
2. **The preflight, per port, before any Enter Sleep is written.** CLx:
   never enabled by this driver (decision U16); on a preserved link a
   predecessor's CL0s, CL1 or CL2 enable (`LANE_ADP_CS_1` bits 10 to 12)
   read set at either end is cleared at both ends, the device end first,
   and read back. The Target Asymmetric Link of each Gen 4 link is read
   and compared with the link's actual state; nothing is written to it
   (decision U31: an asymmetric link is used as found and never
   transitioned, and the target survives the sleep, Table 4-71, so a
   correction now would be a transition at the wake). Enable Gen 4 Link
   Recovery is cleared as today. **A preflight that fails anywhere** (a
   CLx bit that will not clear, a target that does not match, an
   unreadable register, on any router) leaves the whole domain out of the
   entry: no router gets items 4 and 5, since the host router and a
   chain's parent are shared by every branch below them and a per-port
   fallback would still put a router with a failed prerequisite to sleep.
   The fact is logged (`u4.sleep.preflight`, the router, the port and the
   step), every obligation is judged at the resume by section 7.6's rule,
   not by item 1 of section 11.1.6, and the platform's sleep takes the
   links down as it did before this section.
3. **What the entry cannot cover.** A path this driver does not restore
   at the resume - a preserved DisplayPort path, a router watched rather
   than run (section 9.2's links at depth 2 that did not verify, every
   router at depth 3 and deeper), a USB3 path on a link this driver does
   not run - is lost across any sleep that takes the domain's power, as
   in every release so far (Spec 4.5.2: every configuration space returns
   to its defaults); leaving such a router out of the entry does not
   protect it from its parent's sleep. It is not supported, logged at the
   suspend (`u4.sleep.unrestorable`, the port and what is on it) and, at
   the resume, a watched link is read again as section 9.2 reads it at a
   start (its devices reported missing where it does not come back).
   Whether a domain carrying such a path should sleep at all is the
   owner's (question Q7).
4. **The wake configuration: none** (decision U35). On each router this
   instance runs, every downstream USB4 port's Enable Wake on Connect, on
   Disconnect, on USB4 Wake and on Inter-Domain are cleared; each device
   router's upstream port keeps (or is given) Enable Wake on USB4 Wake,
   as Table 8-1 requires of every upstream port; each USB4 device router's
   Enable Wake on PCIe, USB3 and DP are cleared; a Thunderbolt 3 device
   router's six `SX_CTRL` wake bits are cleared ([linux] `tb_lc_set_wake`
   with no flags). The values read before the writes are kept per port for
   item 2 of section 11.1.6. A USB4 host router's downstream ports are
   given the same; a Thunderbolt host router's `SX_CTRL` wake bits are not
   written, as Linux writes none at route 0 ([linux] `lc.c`
   `tb_lc_set_wake` returns before any access there). USB4 Port is
   Configured is not written here:
   section 6.4 set it on both ends of every link this driver configured,
   and a preserved link keeps its predecessor's. An inter-domain port's
   wake on inter-domain is clear, so at the sleep event it disconnects and
   the peer sees the link go (Guide 8.2.3, Table 8-3 row 2), its USB4NET
   service already ended (record 35 section 7.7).
5. **Enter Sleep** in every router this instance runs on a port that
   passed item 2, read-modify-write and read back: the routers at depth 2
   (record 34's kids), then each device router at depth 1, then the host
   router (through its own register type; nothing on a generation 1
   Thunderbolt host). The order does not matter to the Guide; this is
   Linux's. Item 3's routers are left as they are.
6. **Sleep Ready**, `ROUTER_CS_6` bit 0 of each USB4 router given Enter
   Sleep, all polled in one pass per poll, for up to 500 ms on the
   instance's clock from the last Enter Sleep write (the Guide's
   recommendation; a deadline on the elapsed clock with section 5.3's
   tick allowance, not a count of sleeps), each wait a safe point of
   section 5.3, so the transport keeps being served: a notification that
   Guide 7.7.2 lists for acknowledgement (`ERR_PLUG`, for instance) is
   acknowledged there as section 5.1 does, which Spec 4.5.1 needs, since a
   router may hold Sleep Ready back until such an acknowledgement arrives;
   a hot plug event is answered with its own acknowledgement (Guide 4.3,
   section 5.1's hot plug paragraph) and queues its port check for after
   the resume; a version 2.0 router's own `ROP_CMPLT` notification (Event
   Info `03h`) is consumed silently, as section 5.1 and `TbtCtlClassify`
   consume every `ROP_CMPLT` (the Guide lists none for acknowledgement),
   and Sleep Ready is still read. A Thunderbolt router has no Sleep Ready
   (Linux polls none).
7. **Then the rings stop**, as today. A router that does not answer, or
   whose Sleep Ready is not read set in time, is logged
   (`u4.sleep.not-ready`, its route and the value read) and counted; the
   sequence is finished anyway, since the Guide forbids stopping it once
   Enter Sleep is written and this driver cannot stop a system sleep from
   its D3.

**Steps 4 and 5 of Guide 8.2.1.2** (telling the host system, or having
the host router send `LT_LRoff`) are left to the platform: its own entry to
S3 or S4 is taken as the host router's sleep event [inf]. On an Intel
integrated host under this profile Linux sends no link-controller mailbox
(`pci.c` returns before it for a software connection manager), and this
driver sends none either; no host router this driver runs documents a way
to request `LT_LRoff`, so none is requested [inf].

**The order against the tunnelled devices.** The NHI's device object is
`DO_POWER_PAGABLE` and the bridges' are not (record 21 section 6.3), so on
NT the NHI reaches D3 before the tunnel ports, and before the tunnelled
endpoint whenever that endpoint's stack is not pageable [inf: which
storage stacks are]. Enter Sleep is therefore written while the endpoint
may still be in D0. Spec 4.5.1 makes the router sleep only at the sleep
event, so the tunnels keep carrying traffic until the platform sleeps; the
Guide's note on internal functions in D3 concerns a router's own built-in
functions and is a "should". That nothing is lost between Enter Sleep and
the platform's sleep is [inf], read by the legs (section 11.1.12).

**The cost** at the D3, which the system's sleep waits for: per router a
few requests (four attempts of 100 ms at worst, section 5.2) for the reads
and writes of items 2 to 5, then one 500 ms Sleep Ready bound for all of
them; for the largest domain this driver runs, a few seconds at worst,
logged at the entry as `u4.sleep.bound-ms`.

**Off** (`XhciCmSleepEntry` 0): items 2 and 4 to 6 are not run (Enable
Gen 4 Link Recovery is still cleared, as today), which is
the behaviour of every release up to `3.4.x`.

#### 11.1.6 The resume after it (Guide 8.2.2)

Section 7.6's obligations and budget stand; after an entry that completed,
each router is expected to read its configuration lost (Spec 4.5.2) and
section 7.6's rebuild (sections 6.4 to 6.9 and the Detect gate) is Guide
8.2.2's reconfiguration and path setup. Five changes:

1. **A preserved tunnel is rebuilt too** (decision U34, question Q3). An
   obligation recorded at a suspend whose entry prepared every router of
   that port is rebuilt, where its router reads lost, whether this driver built the tunnel or preserved it (a
   pre-OS connection manager's, or a predecessor's that section 9.4 did
   not match): the configuration the domain lost is the one this driver
   put to sleep, and Guide 8.2.2 asks the connection manager to restore
   every path that was present before the sleep. Without the
   entry on that port (`XhciCmSleepEntry` 0, a failed preflight, or a
   router not prepared) section 7.6's rule stands: only this driver's own
   tunnel, or section 9.4's match, is rebuilt. **Prepared** means, by
   type: a USB4 router's Enter Sleep written and read back (Sleep Ready
   late or missing does not undo it, item 7 of section 11.1.5); a
   Thunderbolt router of generation 2 or later, `SX_CTRL` bit 31 written
   and read back in each link controller; a generation 1 Thunderbolt
   router is never prepared (it has no sleep register), so a port through
   one keeps section 7.6's rule.
2. **A sleep that did not happen.** Where the system's transition failed
   after the NHI's D3, or a sleep state kept the domain powered, the
   routers come back configured (section 7.6's "same router, still
   enumerated") with Enter Sleep, and perhaps Sleep Ready, still set. Each
   such router has Enter Sleep cleared in its own register type
   (`ROUTER_CS_5` bit 0, or each link controller's `SX_CTRL` bit 31) and
   its wake enables written back to the values item 4 of section 11.1.5
   kept, host router first, each
   read back, before its verification; a router where that fails leaves
   its port's obligation unmet (HELD), since what a router left with Enter
   Sleep set does at its next `LT_LRoff` or PERST is unknown [inf].
3. **Wake status**, `ROUTER_CS_6` bits 2 to 4 and `PORT_CS_18`'s four bits,
   read once per USB4 router after the resume and logged
   (`u4.wake.status`), a diagnostic: the device-originated sources (PCIe,
   USB3, DP, connect, disconnect, inter-domain) are expected clear, their
   enables having been cleared; an upstream port's Wake on USB4 Wake
   Status may be set, since that port keeps its enable and the host
   router's wake propagates through it (Spec 4.5.4.2).
4. **A tunnel the firmware rebuilt at the boot** (a hibernation on a
   platform whose pre-OS connection manager tunnels at power-on, and the
   emulator's `preos-cm` and `preboot=on`) reads as the same router with
   its tunnel up, verified and kept as preserved (section 9.2), as today.
   Linux tears such a tunnel down and builds its own (`tb_resume_noirq`);
   this driver keeps it, because the device is then already present when
   Windows looks at the port, which is what a native hot-plug port needs
   (section 11.1.8).
5. **On 98 SE and ME, the re-enumeration waits for Windows' restore of
   the tunnel port** (task 48.35, 2026-11-10; record 21 section 6.1.3).
   The wake resets the tunnel port bridge's header, and the rebuilt
   tunnel's member operation reached the bridge before Windows 98 SE
   restored it; its `Reenumerate_DevNode` on the reset bridge then left the
   bridge unconfigured and the drive lost. Each member whose
   bridge read a secondary bus at the suspend now waits, at its next
   operation, until the bridge reads one again (Windows' restore), at most
   20 s, then goes on as before; the outcome is logged
   (`cm.reenum.9x.resume-wait`). Windows 2000 and later are untouched.

**Daisy chains** (record 34 section 6.9): a kid's routers get the entry
(item 5 of section 11.1.5) and its obligation is judged after its parent's,
item 1 applying to it as to a port. **Networking** (record 35): this
domain's peers end before the entry (as today) and its inter-domain port
disconnects at the sleep event; after the resume the link is a new plug
and the discovery runs again (record 35 section 3.8). A neighbour that
sleeps (Guide 8.2.3, Table 8-3 row 1) leaves lane 0 in CLd with Router
Detected still set, which section 7.1 reads as the peer's unplug, ending
it as record 35 section 3.8 ends any peer; its wake (row 3) is a new
plug. That reading is kept as it is; telling the two apart by Router
Detected (`PORT_CS_18` bit 13) in the log is left to a later item, record
35's code not being this task's. This domain never enables wake on
inter-domain, so a neighbour cannot
wake it; the reverse is the neighbour's choice: a neighbour asleep with
its own wake on inter-domain enabled wakes when this domain's port
disconnects the link (Guide 8.2.3), which this domain's own sleep entry
does (item 4 of section 11.1.5). That is accepted as the Guide's intended
behaviour (question Q4). Neither record's code changes; the legs read both
where the emulator offers them.

#### 11.1.7 The firmware connection manager, and the integrated hosts

- **Falcon Ridge and Alpine Ridge**: unchanged, `0x05` then `0x07`. At a
  standby the firmware tunnels each saved device again within the
  platform's resume, before the driver ready ([obs] through `usb4-emu`
  R34; the P14s capture), so the device is present when Windows looks at
  the port. At a hibernation (a power-on) the save is forgotten [model]; a
  device is announced again at the driver ready, approved by
  `XhciCmApprove`'s rule, and its tunnel comes after the NHI's resume:
  section 11.1.8's case where the port is native.
- **Titan Ridge and Maple Ridge (ICM)**: unchanged, `0x07` alone, as Linux
  sends (no `save_devices` for them in `icm_probe`). Whether this
  firmware keeps a device across a standby without `0x05` is unread; the
  emulator restores only after a save, so its Titan Ridge leg is expected
  to show the device back after the driver ready. Sending `0x05` there
  too is not proposed: no source says the firmware accepts it.
- **The ICL row** (Ice Lake to Meteor Lake under their firmware): at a
  system sleep (S1 to S4, never a shutdown), after the rings stop, the
  link-controller mailbox `GO2SX_NO_WAKE` (`TbtHwPlatformSuspend` with no
  wake; `VS_CAP_19` command with Valid, `VS_CAP_18` Done polled up to
  500 ms; [linux] `icl_nhi_suspend_noirq`), its timeout logged
  (`tbt.hw.lc-timeout`, already there) and the sleep going on. The helper
  as coded counts 500 one-millisecond sleeps (`TbtHwLcMailbox`), which a
  coarse tick stretches well past 500 ms (section 5.3); before it is
  called it is changed to a deadline on the elapsed clock (`TBT_WAIT`,
  section 5.3's rule: the bound plus one tick), with a host vector of a
  55 ms tick and Done never set. Force power
  left set, as Linux leaves it at a sleep through the firmware. The resume
  is unchanged (force power, firmware ready, LTR, the bind from step 8).
  This takes away the reason record 21 sections 1.4.3 and 1.4.4 give for
  "suspend with an integrated controller is unsupported" (the handshake
  not sent); it rests on Linux alone, no integrated host having been
  captured.
- **Integrated hosts under this profile** (Lunar Lake and later, and an
  earlier one found running a software connection manager): section
  11.1.5 alone, with no link-controller mailbox ([linux] `pci.c`), force
  power as section 4.2 says.

#### 11.1.8 The native PCIe hot-plug port: what this driver cannot order

**The mechanism** (task 43.71). Where Windows drives a tunnel port's
hot-plug itself - a PCI Express slot that is hot-plug capable, on Vista to
10, where the platform leaves native hot-plug to the operating system
[inf: 2000, XP, 98 SE and ME have no native PCI Express hot-plug in their
PCI drivers, so this case is NT 6.x's] - its PCI driver reads the slot's
presence at the port's own resume and may turn the slot off, before the NHI
has resumed. The tunnelled device is then reported missing, and a file
whose directory entry was still cached at the sleep is lost; the device
comes back only through a new plug, which on q35's slot (with a power
controller and an attention button) means a replug, and on a real
integrated root port is unread.

**Rejected**, each for a recorded reason: holding the bridge's D0 (task
43.24h: it deadlocks the resume); making the NHI non-pageable (the NHI and
its tunnel ports are siblings or cousins, which the power manager does not
order [inf]); pending the bridge's bus-relations query in the filter until
the NHI's resume (in 43.71 the slot was turned off at the port's resume,
not at a query, so it would change nothing there and only make PnP wait);
keeping the domain powered across the sleep (the platform removes its power
in S3 and S4, record 21 section 4.5.6).

**What brings the tunnel back first** is firmware that tunnels before the
operating system resumes: Falcon Ridge and Alpine Ridge's saved devices at
a standby (section 11.1.7) [obs]; a pre-OS connection manager at the boot
of a hibernation resume (platform pre-boot support) [model, task 44.27]; a
pre-OS connection manager in a firmware's standby resume path is unread on
any machine. Elsewhere this case stays, and what the user sees is the
owner's (question Q1).

**Recognising the case**, for the policy and the log: a member of the
tunnel-port set whose PCI Express capability has Slot Implemented set and
Hot-Plug Capable (Slot Capabilities bit 6) set, under Vista or later, with
Hot-Plug Interrupt Enable (Slot Control bit 5) read set at the suspend as
the sign that Windows took the slot [inf: the platform's grant itself is
not readable here]; the registers are record 21 section 6.5's capability,
read through `HalGetBusData` at the suspend while the bridge is in D0.
Not built: the owner chose Q1's (a), which needs no recognition, and a log
line alone was not worth a configuration read at every suspend.

#### 11.1.9 The XP x64 and 7 x64 shutdown stall

Task 44.27's reading puts it on `nvme98`, never released, and not on this
package (section 11.1.2); with Stor98 on the drive it did not recur.
Nothing changes in the code. The release notes' sentence, which ties the
stall to this package, goes when the owner agrees (question Q6), after one
re-read on the reviewed build: 7 x64 and XP x64, i440FX with task 43.24's
Titan Ridge topology, Stor98 on the drive, a hibernation, the resume, then
`shutdown /s`.

#### 11.1.10 Values, counters and the log

- **`XhciCmSleepEntry`**, `REG_DWORD`, written by every install path at its
  default under the don't-overwrite flag with an INF-gate row and a
  `tuning.md` row (the owner's rule of 2026-10-30), a bench switch not
  asked by the installer: 1 runs section 11.1.5 and items 1 and 2 of
  section 11.1.6; 0 the `3.4.x` behaviour; any other value refused, the
  default standing. Read once at start (record 21 section 8's rule). **Its
  default is 1**, set in a commit of its own after the emulator legs passed
  (owner, 2026-11-09, decision U36; section 11.1.12); 0 stays the way back, and a value already on a machine is kept at an update (the
  don't-overwrite flag).
- **No policy value for question Q1**: the owner chose (a).
- **Counters** `u4.sleep.entries`, `.routers` (Enter Sleep read back),
  `.ready`, `.late`, `.refused`, `.clx`, `.preflight`, `.unrestorable`,
  `.cancelled` (item 2 of section 11.1.6) and `u4.resume.preserved-rebuilt`,
  and on the ICM side `cm.lc.go2sx`; the log lines `u4.sleep.entered`,
  `u4.sleep.not-ready`, `u4.sleep.refused`, `u4.sleep.wake.refused`,
  `u4.sleep.asym`, `u4.sleep.cancel`, `u4.sleep.cancel.failed`,
  `u4.wake.status` and `cm.usb4.sleep` (the value as run with); each
  counter emitted at the stop with the others (section 12).

#### 11.1.11 The owner's questions, and the answers

Answered by the owner on 2026-11-09: **Q1 (a)** - sleep as asked; on a
native hot-plug port with no firmware that tunnels first a drive may need
plugging in again after the resume, and the README keeps "Safely Remove
first" for that case; no automatic safe removal. **Q2 yes** - the
best-effort flush in `xhcimark`, NT only, never on 98 SE and ME, told to
the user as reducing the risk, not removing it. **Q3 yes** (decision U34),
keeping the identity and lost-configuration checks and the exclusions
(an unprepared route, a generation 1 router, a path not restorable); a
tunnel the firmware already restored is kept. **Q4 yes** (decision U35):
wake from a Thunderbolt or USB4 device is unsupported, which is not a
guarantee that none happens; the upstream ports' Enable Wake on USB4 Wake
stays as Table 8-1 requires. **Q5 yes** (decision U36), with the default
held at 0 until the emulator legs pass (section 11.1.10). **Q6** is left
to the end of the task: the README and release-notes text is drafted
after the legs, by the owner. **Q7 (b)**: sleep, the path lost and logged,
and the release notes name the case. The questions as they were put:

- **Q1. A native hot-plug tunnel port without a firmware that tunnels
  first** (Vista to 10, section 11.1.8). What this driver can guarantee is
  narrow: it cannot order the resume, and it cannot refuse the sleep there,
  since from Vista on a failed `IRP_MN_QUERY_POWER` for a system sleep does
  not stop the sleep [inf: Windows' behaviour as its documentation states
  it, not read here]. The choices: (a) sleep as asked; the device may be
  reported missing at the resume and come back through a new plug; (c)
  ask Windows to safely remove the tunnelled devices at the suspend notice
  and bring them back after the resume, best effort: refused where a file
  is open, and bounded by the time the notice allows. Recommended: (a),
  with Q2's flush, because (c) makes every drive leave and return and is
  no more certain.
- **Q2. Flush the tunnelled volumes at the suspend notice**, best effort:
  the mark service (`xhcimark`, 2000 to 10) takes the system's suspend
  notice (a service control handler that accepts power events, which it
  does not register today), opens each volume whose disk is behind a
  member of a tunnel-port set and asks the file system to flush it, all
  volumes in parallel within a fixed bound (1.5 s, a design value), and
  writes each outcome (done, failed, out of time) to its log. Windows does
  not wait for the service beyond the notice's own allowance, so a flush
  that has not finished, a service that is not running, or a write made
  after the flush is not protected; nothing else changes when it fails.
  Recommended: yes on NT, where the loss is read, described to the user as
  reducing the risk, not removing it; not on 98 SE and ME, which have no
  native hot-plug and no service.
- **Q3. Rebuild a preserved tunnel after a sleep this driver entered**
  (decision U34, section 11.1.6 item 1), departing from section 7.6's
  "its configuration being someone else's". Recommended: yes.
- **Q4. Wake from a Thunderbolt or USB4 device** (decision U35): none, the
  NHI never arming a wake; and a neighbour asleep over a networking link
  that has its own wake on inter-domain enabled is woken when this domain
  sleeps and its port disconnects (Guide 8.2.3), which is accepted.
  Recommended: none in `3.4.3.0`, the neighbour's wake accepted.
- **Q5. The Guide's sleep entry on by default** (`XhciCmSleepEntry` 1,
  decision U36). Recommended: on, with the value to switch it off.
- **Q6. What is promised.** The README's "stop and unplug" line and the
  release notes' standby paragraph are rewritten after the legs, from what
  they read. Recommended: standby and hibernation stated as working with a
  device attached; the native-port case (Q1) kept as a known limitation
  with "Safely Remove before standby or hibernation" for it; the integrated
  controllers' standby limitation and the XP x64 / 7 x64 shutdown sentence
  removed. Whether that waits for the owner's bench is the owner's.
- **Q7. A domain carrying a path this driver cannot restore** (item 3 of
  section 11.1.5: a preserved DisplayPort path, a watched router at depth
  2, anything at depth 3 and deeper): (a) sleep, the path lost as in every
  release so far, logged; (b) the same, and the release notes name it; (c)
  on 2000 and XP, where a queried sleep can still be refused [inf], fail
  `IRP_MN_QUERY_POWER` while such a path is up (a critical sleep is not
  queried and still happens), and (b) on Vista and later, where a refusal
  does not stop the sleep (Q1). Recommended: (b), one behaviour on every
  system.

#### 11.1.12 Verification

**Host vectors** (the fake fabric of `test_u4cm` gains `ROUTER_CS_5` and
`ROUTER_CS_6`'s sleep bits, `PORT_CS_19`'s wake bits, `SX_CTRL` and a
Sleep Ready delay): the entry's order and every bit written; CLx cleared
on a preserved link; a router late or silent at Sleep Ready; a router
holding Sleep Ready until an earlier `ERR_PLUG` notification is
acknowledged; a preflight failing on the host router of a two-port domain
with the other port passing, which leaves the whole domain out; the
resume of a domain that lost its configuration with a preserved tunnel
rebuilt after the entry and held without it; a sleep that did not happen
(Enter Sleep cleared, the wake enables restored, a refusal leaving the port
HELD); `XhciCmSleepEntry` 0 writing nothing; a port whose preflight fails
left out of the entry and judged by section 7.6's rule; a Thunderbolt host
router (section 19) entered through `SX_CTRL` with no wake bit written,
a generation 1 host given nothing, and a preserved tunnel through it held
at the resume by section 7.6's rule; a `ROP_CMPLT` during the Sleep Ready wait consumed without
an acknowledgement while a hot plug event in the same wait is
acknowledged; and in `test_cm_pwrr` and the ICM suite, `GO2SX_NO_WAKE`
once at an ICL row's sleep, never at its shutdown, a timeout going on
within its elapsed bound at a 55 ms tick. With Q2: the service's pure half
(which volumes are behind a tunnel port, the bound, the outcomes logged
for a flush done, failed and out of time).

**Legs** (the emulator; S3 where the base can stand by with the
`virtio-gpu-pci` route of section 11.1.2, S4 wherever hibernation is
offered; each reading the drive and its files after the resume, the log's
`u4.sleep.*` and resume lines, and the first shutdown after it):

| Leg | Profile and machine | Targets | Expected |
|---|---|---|---|
| S.1 | `usb4-generic`, SHPC tunnel bridge (i440FX) | 2000, Vista x86, 7 x86 and x64, 8.1 x86, 10 x86 and x64; XP x86 S3 only | Enter Sleep and Sleep Ready on both routers; the tunnel rebuilt; the drive and files kept |
| S.2 | `usb4-generic`, q35 native port, no pre-boot CM | 7 x64, 10 x64 | S3: the link retrain (Presence kept), what Windows does read; S4: the device missing (Q1's case); with Q2, the service's log reading each flush done |
| S.3 | the same with `preos-cm=on` | 7 x64, 10 x64 | S4: the firmware's tunnel kept, the device present |
| S.4 | `tb3-alpine-ridge` | 7 x64, 10 x64 | S3: the saved device back before the driver ready, the device present |
| S.5 | `tb3-titan-ridge` | 7 x64 | S3: the device back after the driver ready |
| S.6 | `intel-adl` | 10 x64 | force power and LTR again; the entry and the rebuild |
| S.7 | `asm4242`, its enclosure | 8.1 x64, 10 x64 | as S.1 on that host |
| S.8 | depth 2 (`CHAIN=1`) | 10 x64 | the kid's entry and rebuild after its parent's |
| S.9 | `usb4-generic`, 98 SE and ME on q35 with ACPI | 98 SE, ME | S3 where reached: the entry, the 9x re-enumeration after the resume (record 21 section 6.1) |
| S.10 | the stall (section 11.1.9) | XP x64, 7 x64 | no stall with Stor98 |

Not readable in the emulator, owed to a bench: the ICL row's mailbox (no
integrated ICM is modelled), a sleep that did not happen (no route known
to make one), a real native hot-plug root port's behaviour at the resume,
and a neighbour domain's sleep over a networking link, then this domain's
own sleep (two domains).

**Read so far** (task 48.35, 2026-11-09, `da0573e7`; `runs/run-48.md`,
"48.35"): on 7 x86, q35, the `asm4242` model with its pre-OS CM and the
ORICO enclosure at power-on, `XhciCmSleepEntry` 1, a hibernation and its
resume passed: the entry set Enter Sleep on the host router and the
ASM2464 and read both Sleep Ready, the pre-OS CM's tunnel came back at the
boot and was verified, every obligation was met, and the drive kept its
files and took a new one. S3 did not resume on q35 then: the emulator's
SeaBIOS lost the q35 MMCONFIG window and the ICH9 power-management block
at the wake (with Windows' own drivers too); the `usb4-emu` fork's SeaBIOS
image restores them (`usb4-emu` `docs/Q35-S3-RESUME.md`).

**Read with that image** (task 48.35, 2026-11-09 and 10, the gated build of
`a2d7fc7d`; `runs/run-48.md`, "48.35 (the stand-by legs)"): S.7's host on 7
x86, 8.1 x86 and 10 x86 (q35, the `asm4242` model with its pre-OS CM, the
ORICO enclosure at power-on), each a standby and a hibernation with
`XhciCmSleepEntry` 1 and again with 0, twelve legs: every one resumed with
the drive and its files, took a new file, kept its device count, and shut
down in under a minute after it; at 1 the entry set Enter Sleep on both
routers and read both Sleep Ready (`u4.sleep.entered=00000002`), at 0 no
entry was made, and both met every resume obligation
(`cm.reenum.resumed=00000001`). The `tb3-alpine-ridge` profile (S.4's
case, the firmware connection manager) on 10 x86 with the TB3 enclosure,
behind its native hot-plug tunnel port: the standby the same; after the
hibernation the drive was missing, twice (section 11.1.8's case, the
firmware connection manager's own path; `XhciCmSleepEntry` does not reach
it). Not read: S.1 to S.3, S.5, S.6 and S.8 to S.10, the x64 half, `xhcimark`'s flush (its log held no flush line
on these legs), and the bench. `XhciCmSleepEntry` is 1 from these
readings (decision U36).

**The bench** (the owner's): a machine whose firmware offers S3 to the
operating system (Modern Standby alone does not reach this path), with a
Thunderbolt 3 or USB4 host and the ORICO enclosure; the P14s Gen 1 (Alpine
Ridge, firmware CM) for S.4's case, and a USB4 host under 10 x64 for the
native-port case of Q1.

#### 11.1.13 As coded (task 48.35, 2026-11-09)

- `src/cm/tbt_u4cm.c`: `tbtU4SleepEnter` (section 11.1.5, called from
  `TbtU4Suspend` with `TBT_U4_CONFIG.SleepEntry`), with `tbtU4SleepLink`
  (the preflight), `tbtU4SleepWake` and `tbtU4SleepWakeBack` (item 4 and
  its undoing), `tbtU4SleepBit` (Enter Sleep by router type) and
  `tbtU4SleepLc` (a Thunderbolt router's link controllers);
  `tbtU4SleepCancel`, `tbtU4SleepUndo` and `tbtU4SleepState` (section
  11.1.6 item 2: the host router read first; each device router's lane
  above it read for Lock, set again after a sleep or a new connection, so
  that router is left to the classification; a router read configured is
  undone; a state that cannot be read keeps its record and holds the port,
  never taken as a sleep) and `tbtU4WakeStatus` (item 3), called from
  `TbtU4Resume` once the host router is read again (a host router whose
  Enter Sleep or wake enables cannot be put back leaves every port's
  obligations unmet); the lost-configuration
  rebuild takes `TBT_U4_OBLIGATION.Prepared` beside `BuiltHere`
  (decision U34), for a port and for a kid. `tbt_u4cm.h` carries the
  value, the per-router sleep record, the obligation's flag, the counters
  and `TBT_U4CM.SleepEntered`; `tbt_regs.h` the bits of section 11.1.3.
- `src/cm/cm_power.c`: the ICL row's `GO2SX_NO_WAKE` after the ICM
  client's suspend (section 11.1.7); `src/cm/tbt_hw.c`: `TbtHwLcMailbox`
  bounded on the elapsed clock.
- The value: `cm_vals.c`, `cm_vals.h`, `cm_reg.c`, `cm_nhi.c` (taken and
  logged as `cm.usb4.sleep`), both `xhcicm` INFs at 0,
  `scripts\inf-gate\check-cm-inf.ps1`'s table and mutations,
  `docs/using/tuning.md`.
- `xhcimark/xhcimark.c`: the extended control handler, taken where
  `RegisterServiceCtrlHandlerExA` exists, accepting power events; at
  `PBT_APMQUERYSUSPEND` and `PBT_APMSUSPEND` each fixed or removable volume
  whose disk (by its storage device number, matched to a disk interface's
  devnode) has a member of a tunnel-port set among its ancestors is
  flushed on a thread of its own, each outcome logged; the whole of a
  notice's work (the search for the volumes included) runs on a worker of
  its own, which the control handler waits for at most 1.5 s from the
  notice, and which owns its record until every flush thread has ended; a
  notice that finds the last one's worker still running starts nothing and
  logs it. The members are the poll thread's last reading. `mark_core.c` holds the
  choice (`mk_flush_wants`, `mk_flush_behind`) and its self-test cases.
- Item 5 of section 11.1.6 (task 48.35, 2026-11-10; record 21 section
  6.1.3): `src/cm/cm_cfgwk.c` (`CmCfgwkSuspend`, `CmCfgwkResumed`,
  `cmWkResumeParked` before each member operation, the chain requests and
  no-IRQ work of a marked member held), `src/cm/cm_reenum.c` (the calls at
  the suspend and every end of the resume), `src/cm/cm_nvlinkos.c` (the
  late scan armed at a fresh 9x exposure that finds no secondary bus),
  `src/cm/cm_ports.c` (`CmPortsResumeWaitWanted`, `CmPortsResumeWait`,
  host vectors in `test_cm_ports`, `test_resume_wait`).
- Host vectors: `test_u4cm` (`test_sleep_entry`, over the fake fabric's new
  Enter Sleep and Sleep Ready model, its `NoSleepReady`, `SlpStuck` and
  `ClxStuck`), `test_tbt_hw` (the mailbox at a 55 ms tick), `test_cm` (the
  value) and `test_mark` (the flush's choice).

#### 11.1.14 The function's return before the resume is published (task 49.11; a design, reviewed and revised)

**What was read** (`runs/run-49.md`, "49.11"; 7 x86 on the i440FX
emulator, `usb4-generic` with no pre-boot connection manager, the SHPC
tunnel bridge 00:05.0, the ORICO model, `XhciCmSleepEntry` 1, the qemu
flavour's log and the emulator's `usb4_*` trace): at the wake the
emulator's reset drops the tunnel and removes the endpoint; this
profile's resume rebuilds the PCIe tunnel (`u4.pcie.up` at `cm.ms`
0x377EC); the rebuilt tunnel's stor98 UP is taken (`CmNhiU4Take`, whose
scan of the secondary bus then finds nothing to publish); section 7.6's
release follows at once (`cm.reenum.hold.released`, an invalidation),
then the drain's `TUNNEL_UP` poll (`cm.reenum.up.poll=00000404`,
`NO_READING`: on NT this bridge offers nothing the port reading can vote
on - no PCI Express link or slot, and its SHPC register is not read
without a configuration write); the bridge's BusRelations answer, 220 ms
after the tunnel came up, holds no PDO (`cm.chain.rel=00000028`), and PnP
removes the chain's two devices (`cm.chain.remove`). The endpoint answers
behind the bridge 1.7 s after the tunnel came up (the trace's
`usb4_tp_insert`, gen 2, after the emulated bridge's empty-slot settle);
nothing asks the bridge again (an invalidation accepted is not a
readiness debt; the learning retries attribute, they do not invalidate).
At a hibernation the fresh machine has no departed endpoint, the function
answers as soon as the tunnel is up, and the drive is kept. On hardware a
function behind a newly trained link is not expected to answer at once
either (the PCI Express Base Specification, section 6.6.1: 100 ms after
the link trains before it may be addressed, at least 1.0 s before it may
be judged broken).

**The change.** In the NHI's resume under this profile (`CmNhiReinit`,
the connection-manager thread, PASSIVE_LEVEL), when `TbtU4Resume` met
every obligation (`TBT_OK`: `ok` and not `held`) and the re-enumeration
runs on the filter route (NT), a settle wait (`CmReenumResumeSettle`,
new) runs right after it: **before** the resume's liveness probe of the
host interface (section 7.7's size-register read and the monitor's arm),
the rebuilt tunnels' stor98 UP (`CmNhiU4Take`), section 7.6's release and
invalidation (`CmReenumResumed`) and the drain's `TUNNEL_UP` exposure:
each of them then finds the function there, as at a hibernation, and a
loss of the host interface during the wait is judged by the probe that
follows it, by the existing failure and containment order.

1. **Who is waited for.** Each member whose filter this domain held at the
   suspend (`CmReenumSuspend`: active then) and whose bridge's last
   BusRelations answer then reported at least one PDO: it had a function,
   so one is expected back. The PDO count is latched at the suspend's
   mark (`CM_REENUM_PORT.SettleWant`), since the snapshot moves with every
   answer while the member is held, and consumed by the wait. A member
   that held nothing is not waited for; a stale latch costs at most the
   bound. The set is copied under the filters' lock as scalars (bdfs); no
   read or sleep is made under any lock. The wait also ends when the host
   interface is unreachable (`CmNhiHwAccess`) or a stop, an exit or a
   revocation is asked.
2. **What shows the function.** `XhciTbtFunctionRead` (new, `tbt_topo.c`)
   on the member: its bus number register read, the secondary bus (bits
   15:8) valid only when nonzero, not 0xFF, above the primary and not
   above the subordinate; then the vendor/device id at function 0 of
   each device number of that bus (behind a PCI Express link device 0
   alone answers, but a PCI bridge's slot puts its card at the slot's
   device number: the emulator's SHPC slot 0 is device 1, which a first
   coding that read device 0 alone missed, so its first leg waited the
   whole bound): `PRESENT` at the first vendor id other than 0000h,
   FFFFh and 0001h; else `NOT_READY` where one read 0001h (a
   Configuration Request Retry Status as software may see it, PCI
   Express Base Specification section 2.3.2); else `ABSENT`; `UNREAD`
   when the bus numbers are not valid or a read fails. The port reading (section 6.5) is not used:
   it votes on link, slot and SHPC presence, which can say `UP` before
   the function answers. Only `PRESENT` ends a member's wait; every other
   outcome is read again.
3. **The bound.** One deadline for the whole set, 5000 ms from the first
   reading (`CM_PORTS_SETTLE_MS`): the specification's 1.0 s plus margin
   for an emulated or slow link and the coarse tick of section 5.3;
   readings every 50 ms (`XHCI_TBT_POLL_STEP_MS`). The wait ends as soon
   as every member waited for is `PRESENT`. When the deadline passes
   nothing else changes: the take, the release and the drain follow as
   before (decision U15), so the worst case is today's behaviour 5 s
   later. Between readings `CmNhiWaitService` is called (it serves the
   ICM and stor98 acknowledgements, not USB4 traffic; USB4 events wait
   for the next pass, as during the rest of the resume).
4. **What it costs.** Where the function answers at the first reading (a
   preserved tunnel, a firmware connection manager's, a sleep the domain
   kept power through, every hibernation read so far) the wait is one
   reading per member. A member whose device was taken away during the
   sleep is waited for to the deadline: that resume is up to 5 s longer.
   The NHI's D0 IRP, held by `CmPowerD0Work` across the resume already,
   and so the system power IRP the NHI's power path completes after it
   (`cm_power.c`, `cmSystemDone`), are completed up to 5 s later; no
   bridge's or other device's power IRP waits on it (the bridges' D0s
   pass at once, task 43.24h).
5. **Logged.** `cm.reenum.resume.settle` once per resume that waited for
   any member: bits 31:24 the members waited for, 23:16 those `PRESENT`
   by the end, 15:0 the elapsed ms (capped at 0xFFFF); and for each
   member not `PRESENT` by the deadline `cm.reenum.resume.settle.late`,
   its bdf in bits 23:8 and its last outcome in bits 7:0.
6. **Scope, and what stays.** Only the immediate function is waited for:
   behind a dock's or enclosure's own PCIe switch the switch's upstream
   port answering ends the wait, though its downstream functions may
   answer later (a chain's and a dock's resume keep their limitation,
   record 34 section 6.9). A resume that is not `ok` (any obligation
   unmet, even where other tunnels came back) keeps the failure path's
   immediate release (decision U15's window): that partial resume keeps
   today's limitation. The ICM profile (record 21) is not changed: its
   firmware rebuilds the tunnels, and its standby leg (S.5) is unread. A
   native PCIe hot-plug port's own resume (section 11.1.8) is not
   changed. The CONFIGMG route (98 SE, ME) is record 21
   section 6.1.3's (task 49.12). The hot-plug path's `TUNNEL_UP` poll is
   unchanged. No registry value; no user-visible text but the user
   documents' standby sentence for such a computer, which this change
   lets go once the legs pass.

7. **A deferred release exposes first.** Where a member's stor98 gate
   stood at the release (`CmReenumResumed` holds its release behind the
   gate, and the take's UP was refused meanwhile), the pass that finds
   the gate down and releases a successful resume's member now calls
   `CmNvlinkExpose` (fresh, naming no tunnel: an entry already UP keeps
   its generation) before the invalidation (`CmPortsDeferredExpose`);
   behind section 7.7's retirement barrier the exposure is owed instead
   (the member's missed call with its exposure, made once the barrier
   lifts). So obligation 1 of
   `cm_nvlink.h` holds on that path too.

Host vectors (`test_tbt_topo`): `XhciTbtFunctionRead` over the fake
configuration space - secondary bus 0, 0xFF, equal to the primary, above
the subordinate, a failed bus read (`UNREAD`); vendor 0001h (`NOT_READY`),
0000h and FFFFh (`ABSENT`), a function (`PRESENT`), a failed id read
(`UNREAD`), the card at device 1 with device 0 empty, a retry beside
a function; and the settle loop's pure step (`CmPortsSettleAgain` in
`cm_ports.c`: given each member's outcome, the elapsed time
and the bound, whether to read again), with a late function, a transient
unread, a member never back, several members with one late, and the
deadline across a wrap of the millisecond clock. Acceptance: S.1's
standby leg on 7 x86 at `XhciCmSleepEntry` 1 and 0 (the drive kept at
t+40 s and t+190 s, files compare, `cm.reenum.resume.settle` logged),
S.1's hibernation at 1, and the leg with the emulator's settle at 0 as
the control.

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
| `XhciCmAsmediaPcie7Credits` | 0 (written by the INFs since task 45.50) | a bench experiment, not asked by the installer: 1 writes 7 credits, IFC 1 and ISE 0 into the two protocol-adapter entries of a PCIe tunnel this driver builds between an ASM4242 host router and an ASM2464 device router; 0 keeps them as read; any other value refused, 0 standing (section 18.8, task 44.41) |
| `XhciCmUsb4V2` | 1 (written by the INFs since task 45.50) | a bench switch, not asked by the installer: 1 runs a USB4 version 2 router this driver enumerates as version 2 (section 6.2); 0 makes this driver a version 1 connection manager again, the `3.3.0.0` behaviour: CMUV 1.0 to every router it enumerates and none of section 6.2.2's version 2 obligations, on enumerated and adopted routers alike; at 1 those obligations follow each router's own version and the capabilities it reports, whatever CMUV a predecessor wrote, which is only read and logged; the two-dword path writes and the Gen 4 Link Recovery clear of section 6.2.3 item 4 hold either way; any other value refused, 1 standing (section 6.2.1 item 5, decision U30) |
| `XhciCmGen4LinkRecovery` | 1 (written by the INFs, task 45.56) | a bench switch, not asked by the installer: 1 enables Gen 4 Link Recovery on a depth-1 Gen 4 link between two version 2 routers this driver runs, with the recovery check after each LINK_RECOVERY (section 6.2.3 item 4); 0 never enables it, the task 45.20 behaviour: an uncorrectable error on a Gen 4 link is a disconnect; either way a predecessor's bit is cleared at the adoption (part 7) and a LINK_RECOVERY has its port check and recovery check (part 4); `XhciCmUsb4V2` 0 never enables it; any other value refused, 1 standing |
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
and withheld by reason; Gen 4 Link Recovery enabled, refused, cleared
and failed clears, the recovery checks and their outcomes (section 6.2.3
item 4). Re-enumeration counters are record 21's `cm.*`. The
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

From 2026-10-26 (roadmap-usb4 task 44.38) this project's code is
GPL-2.0-or-later. The portions of `tbt_u4cm.c` and `tbt_ring.c` adapted
from Linux under the rule above were rewritten from the specifications,
the request traces of the host suites and the cross-check unchanged
(`legal-provenance.md` section 2a, "Rewritten from"; `runs/run-44.md`,
"44.38"); from then on code is not adapted from a file whose SPDX
identifier is `GPL-2.0` or `GPL-2.0-only`, Linux's among them, which is
read for facts only, a fact from Linux alone tagged [linux] (owner, 2026-10-26). The owner, `usb4-emu`'s copyright
holder, offers what this project takes from `usb4-emu` under
GPL-2.0-or-later (2026-10-25); `usb4-emu` itself stays GPL-2.0-only.

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
- **`test_u4v2`**, task 45.20: section 6.2.4's vectors over the fake
  fabric's USB4 version 2 host and device routers (USB4 Version `40h`,
  Set Capabilities, Gen 4 links symmetric or asymmetric between two such
  routers, Extended Encapsulation, the enhanced TMU fields): CMUV by each
  router's version in one domain, with `XhciCmUsb4V2` 0 and on a
  Thunderbolt 3 router; a preserved router's CMUV `10h`, `20h` and `30h`;
  the Set and its read-back on version 2 and version 1 routers, the host
  router included, refused (Not Supported, Status 1, nothing enabled),
  failing (never completing, a read-back failing) and with a notification
  held across it, none with `XhciCmUsb4V2` 0 or to a Thunderbolt 3
  router, a preserved router's Set and its adoption; every path entry
  write two DWs (an invalidation, a quarantine's retry) on version 2 and
  version 1 domains; HiFi Enhanced Uni-Directional with and without a
  USB3 tunnel, its order, kept past an eject and cleared on the host's
  port at unplug, HiFi-Bi beside a version 1 router and with
  `XhciCmUsb4V2` 0; CV found with PTO clear (HELD, ROUTER_CS_5 never
  written) and with UTO clear (no USB3 tunnel); Enable Gen 4 Link
  Recovery cleared at depths 1 and 2 with either `XhciCmUsb4V2` value,
  left on a version 1 router; LINK_RECOVERY from the device and the host
  routers; Extended Encapsulation before Path Enable and cleared after it,
  at an eject, an unplug and a preserved tunnel's teardown, never on a
  version 1 end or with `XhciCmUsb4V2` 0, and one that will not set; Gen
  4 symmetric, with `XhciCmLaneBonding` 0, asymmetric both ways and
  preserved asymmetric, never a lane write, and x1, unpaired and reserved
  widths FAILED; a resume on an asymmetric link, verified and rebuilt
  after a power loss; the USB3 allocation's link bound.
- **`test_u4cm`, an earlier instance's tunnels** (section 9.4): an
  instance builds a tunnel, stops and hands its record; a second
  instance on the same fabric finds it preserved and its own, and a sleep
  that loses the domain's configuration rebuilds it; a sleep that keeps
  it, then one that loses it, rebuilds it too. The same with no record,
  another UUID, other HopIDs, another host router, a recorded PCIe
  tunnel beside an unrecorded USB3 one, a HELD port and a USB3 tunnel
  left alone after an eject: preserved as someone else's and HELD at the
  resume, as before. The record handed on through a second instance to a
  third; none from a contained, failed, recovered or suspended instance;
  the given record handed on unchanged by an instance whose rings were
  refused.
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
  re-plug, the pre-OS tunnel preserved and cleaned. Since task 44.42 it
  needs QEMU fork `2e063be` (on `usb4-emu` `main` after its merge of the
  asm4242 model; `02dda40` before the 2026-10-29 squash) or later and builds the `asm4242` host as
  that fork's `usb4-host` does by default, the emulator's asm4242 preset, the
  ASM4242 host router of section 18.2 (lanes 1-4, the host
  interface 5 with 21 paths, PCIe Down 6-7, USB3 Down 8-9, DP IN 10-11,
  an ASM2464 leaving the link 10 s after a plug with no tunnel; section
  18.2), checking that layout; and it runs section 6.11's device on it
  too, cold, hot plug, unplug, re-plug and the pre-OS tunnel, once with
  `XhciCmAsmediaPcie7Credits` 1 (section 18.8: the gate opened for each
  of the three tunnels built, and its lines logged).

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

None is a condition of the cut (decision U10): no USB4 host's behaviour
under a legacy operating system has been read. Task U4HW.1 (real USB4
hardware; task 40.4 until 2026-10-16) is Phase 44's 44.39 (section 18)
and 44-E.6 (the hardware bench, section 18.6).
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
| U6 | What is driven | device routers at depth 1; USB4 version 2 routers run as version 1 (until task 45.20, which runs them as version 2: decisions U30 and U31). Changed: Thunderbolt 3 device routers at depth 1 behind a USB4 host, router by router, PCIe first, are in the phase (task 40-A.8), by the owner's no-cuts decision | 6.2, 8 |
| U7 | The driver's own stop | tunnels left up, force power per section 4.2's ownership record, the fabric discovered at the next start; no Guide chapter 8 sleep flow in the first cut | 11 |
| U8 | The NT filter's ids for USB4 tunnel ports | Changed (first form: the emulator's id only): the ids the mirrors name, ids read from a public PCI id listing by task 40-0.2 with their provenance, and the emulator's two, every flavour; a user line in a copy of the INF for anything else; a matched port acts only once associated (section 7.6) | 3.7, 7.6 |
| U9 | Safe eject under the software connection manager | an orderly per-port teardown that 39-A.8's mechanism calls, for the children of associated tunnel ports only | 10.4 |
| U10 | Cutting with the USB4 profile emulator-only | yes: `3.0.0.0` carries it, the release notes saying "run on the emulator only" and every real host id untested but active by default | 14.4 |
| U11 | The host suites' fabric | a C89 fake fabric of this project's own as the gate, plus the `usb4-emu` fabric cross-check as a recorded reading | 14.1 |
| U12 | The device description | Changed by the owner, 2026-10-15 (first form: `xHCI98 USB4 Host Interface`): `xHCI98 USB4 Host Router` for every USB4-generation host whichever connection manager runs it (the "ICM or USB4" ids included), `xHCI98 USB4 PCI Bridge` for its tunnel ports, the same service and binary. From 3.2.0.0 (owner, 2026-10-23, task 43.73): `xHCI98 USB4 Router` with the maker and product per id, the class line plain (record 21 section 3.3) | 3.7 |
| U13 | `XhciCmTunnelBridge` with more than one tunnel port | Changed (first form: a list indexed by USB4 port): record 21 section 6.3's per-domain override, an unordered set; the port-to-bridge correspondence is router-named or learned (section 7.6), never taken from list order | 7.6, 12 |
| U14 | The selection | section 3's four tests, the inert start in its two forms, the three positive admissions of section 3.4, and the hardware-profile table apart from the CM table (owner's coverage plan, 2026-10-15) | 3, 4 |
| U15 | A resume that fails to restore every tunnel | an exception to the coverage plan's "tunnels restored before dependent PCI devices resume": since task 43.24h the NT bridge's D0 is not held at all, because a held D0 deadlocks the NT resume (a bug check on 8 to 10, the five-minute watchdog's release on 2000 to 7), and after any resume each member still marked is released and, with re-enumeration on, invalidated, which is record 21's old failure path taken every time; system suspend with a tunnelled device attached is unsupported in this release on every target and the release notes say so; on 9x there is no hold at all | 7.6, 11 |
| U16 | The TMU | since task 45.20, on a link of two version 2 routers that this driver enumerates, HiFi Enhanced Uni-Directional whatever the tunnels (section 6.2.2 item 6, the Specification's "shall"); otherwise configured (HiFi-Bi) only on links that will carry a USB3 tunnel, as the Guide's accuracy rule requires, never on a preserved link; other PCIe-only links and Thunderbolt 3 links left with the TMU off; CLx never enabled | 6.6, 8 |
| U17 | A host interface lost under this profile | the owner's decision of 2026-10-15, not an orchestrator one: containment only, automatic recovery a later item of the roadmap's planned work; a terminal CONTAINED state on the monitor's first sign of loss, a surprise removal or a resume that finds the host interface gone, refusing transport, ejects, deferred teardown, UP actions, suspend and resume until a PnP stop and start or a reboot; force power and an unproven DMA buffer kept; no recovery in `3.0.0.0`. **Revised 2026-10-18** by the owner's opening of Phase 42, which promoted the planned recovery (`U4REC.1`) into `3.1.0.0` as tasks 42-0.5 and 42-A.10: CONTAINED is no longer terminal, the recovery of section 7.7's revision following on its precondition (decisions U18 to U29 there, the orchestrator's) | 7.7 |
| U30 | USB4 version 2 routers (task 45.20) | run as version 2 by default: CMUV 2.0 written to a major 2 router this driver enumerates; the version 2 obligations keyed on the routers' own version (the Sequence bit capability enabled with a read-back, Extended Encapsulation on a PCIe tunnel between two version 2 routers, HiFi Enhanced Uni-Directional TMU on their links, a preserved CV never reconfigured, a preserved Gen 4 Link Recovery enable cleared); path entries written as two dwords on every router; `XhciCmUsb4V2` 0, a bench switch written by no INF, restores the version 1 run; confirmed by the owner, 2026-10-29, over making version 1 the default, because the Guide ties the CMUV to the router's version and Linux does the same | 6.2, 6.6, 12 |
| U31 | Asymmetric Gen 4 links (task 45.20) | never initiated (no DisplayPort tunnel needs one) and, found, used as found, never transitioned back to symmetric; confirmed by the owner, 2026-10-29, over a transition to symmetric before the first path, which would touch a link that may carry a preserved tunnel | 6.2.3 |
| U32 | A new PCIe tunnel beside a preserved DisplayPort tunnel (task 45.20) | built as before, its 1.5 Gb/s floor (Spec 5.3.3.1, Guide 6.2.4) not verified, logged; the owner chose (a), 2026-10-29: (a) build it, as now, since a dock a predecessor left with DP alone would otherwise lose its PCIe devices, and a single-lane 10 Gb/s link with a DP stream large enough to starve PCIe is the rare case; (b) build none until the DP tunnel goes, which meets the floor and costs those devices; (c) read the DP adapters' allocated bandwidth (DP bandwidth allocation mode, out of scope here, section 1.3) | 6.2.2, 9.2 |
| U33 | Gen 4 Link Recovery (task 45.56) | enabled by default (`XhciCmGen4LinkRecovery` 1, the owner's task of 2026-10-31, with a value to switch it off) on a depth-1 Gen 4 link between two version 2 routers, set at an idle tick once both lane 0 adapters read CL0, cleared at the host's end at a teardown and on both ends before a stop or a suspend; each LINK_RECOVERY a port check and then a read-only recovery check, whose failing PCIe tunnel goes as section 7.1 item 4 takes a lost one and whose failing USB3 tunnel is torn down alone; never below depth 1, never initiated | 6.2.3, 11, 12 |
| U34 | A tunnel preserved at start, after a sleep this driver entered (task 48.35) | decided (owner, 2026-11-09; question Q3): rebuilt where its router reads lost, as this driver's own tunnels are, when the entry prepared every router of that port (by type, section 11.1.6 item 1; a generation 1 Thunderbolt router never is) (Guide 8.2.2); otherwise section 7.6's rule | 7.6, 11.1.6 |
| U35 | Wake from the fabric (task 48.35) | decided (owner, 2026-11-09; question Q4): unsupported, not a guarantee that none happens; the wake enables the entry programs cleared (USB4 ports and routers, and a Thunderbolt device router's SX_CTRL bits), Enable Wake on USB4 Wake kept on the upstream ports, which Table 8-1 requires; a Thunderbolt host router's wake bits and a generation 1 router left as they are, not programmed; the NHI never arms a wake; a neighbour that enabled its own wake on inter-domain is woken by this domain's disconnect at its sleep, accepted | 11.1.5, 11.1.6 |
| U36 | The Guide's sleep entry (task 48.35) | decided (owner, 2026-11-09; question Q5): on by default, `XhciCmSleepEntry` 1, once the emulator legs pass, in a commit of its own; 0 in the tree until then; 0 the `3.4.x` behaviour and the way back; a value on a machine kept at an update | 11.1.5, 11.1.10 |

## 16. What is open, and what is a risk

- **Every real USB4 host is untested.** The emulator's two host models are
  the only fabrics this has run against; the owner's decision makes every
  listed id act by default with a warning. The ASM4242, a candidate
  host for the targets (no Windows target read on it yet), is recorded
  in section 18; its tunnel-port
  set comes from the ASM4242 family since task 44.40 (section 18.4 item
  3), which only the hardware bench (task 44-E.6) can confirm.
- **Polling on real hardware**: no reader has run a host interface with no
  interrupt; the done-bit model is what all three rely on, but a host that
  writes descriptors back only with an interrupt enabled would hang the
  first request (each bound in section 5.2 makes that an inert start, not a
  hang).
- **Below-4-GB DMA** on hosts whose driver Linux runs with a 64-bit mask:
  expected to work, qualified per host.
- **TMU** is configured only for USB3 tunnels (section 6.6), and on
  every link of two version 2 routers (section 6.2.2 item 6). A router that
  withholds Configuration Ready or a PCIe tunnel on a link without it, or a
  Thunderbolt 3 router that will not carry a tunnel without it, would need
  the TMU on every link first, as Linux does (section 8 item 6).
- **Version 2 routers run as version 2** (section 6.2, task 45.20):
  designed from the Specification and the Guide, run nowhere yet;
  `usb4-emu`'s version 2 router model and the Barlow Ridge bench (task
  45-E.10) are the first readings. Open with it: Extended Encapsulation
  on a Gen 2 or Gen 3 link between two version 2 routers, which the
  Specification asks for and [linux] `tunnel.c` does not set; whether a
  real router's Sequence bit Set Capabilities and read-back behave as
  Spec 8.3.1.3.3 says; the HiFi Enhanced Uni-Directional TMU's
  accuracy for a USB3 tunnel, never measured here; a version 2 router
  that a version 1 predecessor enumerated (CMUV `10h`) and that this
  driver then treats as a version 2 router (section 6.2.1 item 4);
  Gen 4 Link Recovery (task 45.56): the routers' recovery itself, which
  the emulator models only by its outcome, a preserved DisplayPort
  tunnel left unverified after one, a link below depth 1 a predecessor
  left recovering where its clear failed, and what a real tunnelled USB3
  or PCIe link reports right after a recovery, owed to the hardware
  bench, task 45-E.10 (section 6.2.3 item 4 parts 4 and 7). The code (task 45.20, 2026-10-30) has met only the host
  suite's fake fabric: `usb4-emu`'s routers are version 1 routers (USB4
  Version `20h`, no Gen 4 link), so no q35 leg has run a version 2 link.
  What such a leg needs of `usb4-emu`: a host and a device router
  reporting USB4 Version `40h` and a host interface version `40h`; Get
  and Set Capabilities with the Sequence bit capability (the model has
  them); Gen 4 links, symmetric and asymmetric, in LANE_ADP_CS_1 with
  Lane Bonding clearing itself there; ADP_PCIE_CS_1 Extended
  Encapsulation; TMU_ADP_CS_8 and 9; PORT_CS_19 Enable Gen 4 Link
  Recovery; and a way to send ASYM_LINK and LINK_RECOVERY.
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
  this cut. Sleep per Guide chapter 8 is designed in section 11.1 (task
  48.35), not coded; its native hot-plug case (section 11.1.8) stays a
  risk this driver cannot remove, and its open questions are the owner's
  (section 11.1.11).
- **The ASM2464 (section 6.11) on hardware**: the capture is Linux's, on a
  UEFI host. Whether the real router's PCIe Up adapter needs credits
  written in its HopID 8 entry (it resets to none; this driver keeps them
  as read, as Linux does), what its unused adapter 4 answers if read, and
  the PCIe switch in front of the NVMe under Windows 98 SE and 2000 are
  unread; task U4HW.1 (40.4 until 2026-10-16; 44.39 and 44-E.6 from
  2026-10-25; its bench task 45-E.5 from 2026-10-29) waits on a USB4 host
  that boots them (section 18).

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

## 18. The ASMedia ASM4242 (task 44.39)

Added 2026-10-25 for task 44.39, the plan half of U4HW.1 (roadmap-usb4,
decisions table, "U4HW.1"). The ASMedia ASM4242 is a USB4 host with no
firmware connection manager an operating system can talk to, on
platforms whose firmware can boot a compatibility support module (CSM):
a candidate host for this project's legacy targets (no Windows target
has been read on it yet, section 18.6 step 1). This section records its
PCI side, its host router and its firmware's pre-OS tunnel as
`usb4-emu`'s asm4242 model and data record them, what that means for this driver,
and the hardware bench's steps (task 44-E.6). Nothing in `src\` changes
with it.

Tags: **[hw]** the ASM4242's own state as `usb4-emu`'s asm4242 model
and data record it (`usb4-emu` `a0b6749` and `376e2aa` on its `main`
after the merge, the capture archives kept outside that repository;
`7ec3865`, `5d7345a` and `9f333f3` before the 2026-10-29 squash), read
there and never modified from here, `usb4-emu`'s asm4242 research note
being that repository's own reading); where that data came from is
recorded in `usb4-emu`; nothing of it is in this repository. **[inf]** an
inference, and this record's own [linux] and [model]. The enclosure
throughout is the ORICO (ASMedia ASM2464PD device router, Toshiba XG6
NVMe).

`legal-provenance.md` section 4 has the row for every fact below.

### 18.1 The platform [hw]

The ASM4242 sits on CPU root port 00:02.2 (`1022:14DB`, no hot-plug
slot), as the public dumps of section 2.1 and `usb4-emu`'s asm4242
research note said: upstream port 13:00.0 (`1B21:2421`), downstream ports 14:00.0 to
14:03.0 (`1B21:2423`), the xHCI 79:00.0 (`1B21:2426` rev 01) behind
14:02.0 and the host interface 7A:00.0 (`1B21:2425` rev 01, class
`0C0340`) behind 14:03.0:

| Downstream port | Buses | I/O | Memory | Prefetchable | Slot |
|---|---|---|---|---|---|
| 14:00.0, USB4 port 1 | 19-48 | 12 KB | A4000000-BBFFFFFF (384 MB) | 128 GB, above 4 GB | Hot-Plug Capable, Hot-Plug Surprise; no power controller, attention button or MRL; link-active reporting |
| 14:01.0, USB4 port 2 | 49-78 | 12 KB | BC000000-D3FFFFFF (384 MB) | 128 GB, above 4 GB | the same |
| 14:02.0 (xHCI) | 79 | none | D4000000-D43FFFFF | none | no slot |
| 14:03.0 (host interface) | 7A | none | D4400000-D47FFFFF | none | no slot |

- **Every BAR is below 4 GB**: the xHCI's BAR0 D4000000 (32 KB), the
  host interface's BAR0 D4400000 (256 KB) and BAR2 D4440000 (64 KB), the
  enclosure's NVMe BC000000 (16 KB), all 64-bit and non-prefetchable.
- **The prefetchable windows of 00:02.2, 13:00.0, 14:00.0 and 14:01.0
  are 64-bit and above 4 GB** [inf: 128 GB per port does not fit below
  4 GB]. On the emulator's 4G-off layout on the `pentium3` bases the
  firmware programs the root port 1_0000_0000 to 40_FFFF_FFFF, its low
  registers (`24h`, `26h`) reading `0001h` and `FFF1h` [emu]
  (`runs/run-44.md`, "44.49").
- **Interrupts**: the xHCI and the host interface are PCIe Legacy
  Endpoints with INTA#, MSI (8 vectors) and MSI-X (8 and 16).
- **The host interface**: Total Paths 21 (`39640h` reads `15h`). Nothing
  here relies on the firmware's enabling: the operating system's PCI
  driver enables memory decoding at the start, this driver sets Bus
  Master through the PDO and checks that `39640h` decodes (section 3.2).
- **ACPI**: an SSDT with OEM table id `DISCUSB4` carries the `_DSD`
  properties `usb4-host-interface` and `usb4-port-number` (with
  `ExternalFacingPort` and `HotPlugSupportInD3`), another (`CPMMSOSC`)
  the platform `_OSC` with USB4. No target of this project evaluates
  either (section 3.4).
- **The ASM4242's xHCI** (79:00.0) is the controller the host router's
  USB3 Down adapters lead to, and `xhci98.sys` takes it by class like any
  other.

### 18.2 The host router [hw]

`174C:2428`, Max Adapter 11, NVM `200011.241022`, a version 1 router. The
adapters, in the order section 6.1's capability walk finds them: lane
adapters 1 to 4 (USB4 port 1 is 1-2, port 2 is 3-4), the host interface
5 (the Upstream Adapter), PCIe Down 6 and 7, USB3 Down 8 and 9, DP IN 10
and 11; Max HopID 27 on the lanes; lane Total Buffers 174 (two for the
control path); its Buffer Allocation USB3 64, DP AUX 2, DP
main 0, PCIe 64 and DMA 32. This differs from the `usb4-emu` `asm4242`
preset's default layout ([model]: DP IN 5-6, host interface 7, PCIe Down
8-9, USB3 10-11), which this driver never assumed: it walks the adapters
and pairs the Nth PCIe Down adapter with the Nth USB4 port (section 6.3),
and the firmware's tunnels keep that pairing on this router (section
18.3).
174 buffers do not fit the 7-bit Path Credits field, which section 6.8's
accounting already bounds (`TBT_PATH_CREDITS_MAX`).

### 18.3 The firmware's tunnel, and Linux's reset [hw]

**With the enclosure attached at power-on, the firmware builds its PCIe
tunnel before the operating system.** The first PCI scan finds the
tunnel port, the enclosure's switch (`1B21:2463`, its upstream and
downstream ports) and the NVMe behind it. The host router as the
firmware leaves it:

- ROUTER_CS_1 to 4 `0002C520h 00000000h 80000000h 200010FFh`: Topology ID
  Valid set (depth 0, Topology ID 0), Notification Timeout 255.
- On the second USB4 port: PCIe Down adapter 7 ADP_PCIE_CS_0 `8C010400h`
  (Path Enable, LTSSM L0); adapter 6 `00060400h`, USB3 Down 8 and 9
  `40000400h`: no Path Enable. Adapter 7 HopID 8: `80001808h 03000301h`
  (Valid, to lane adapter 3 HopID 8, 0 credits, IFC and EFC set, ISE and
  ESE clear). Lane adapter 3 HopID 8: `80803808h 01802301h` (Valid, to
  adapter 7 HopID 8, 64 credits). So `hd` = `hu` = 8, on the second USB4
  port and its PCIe Down adapter, landing on 14:01.0.
- The host router's TMU: bi-directional HiFi.

**The firmware's tunnel, whole** (the enclosure on the first USB4 port:
lane adapters 1-2, PCIe Down 6, DSP 14:00.0, its switch at 19:00.0 and
1A:00.0 and the NVMe at 1B:00.0 with BAR0 A4000000, so the pairing holds
on this port too) [hw]. The four entries, all at HopID 8 (`hd` = `hu` =
8): the host's PCIe Down 6 `80000808h 03000301h` and the device's PCIe Up
3 `80000808h 03000301h` (each to its lane at HopID 8, **0 credits, IFC
and EFC set**, ISE and ESE clear); the device's lane 1 `80801808h
01802301h` and the host's lane 1 `80803008h 01802301h` (each to its PCIe
adapter at HopID 8, 64 credits, IFC, counter 2 enabled). Both PCIe
adapters read ADP_PCIE_CS_0 `8C010400h` (Path Enable, L0). The device
router: ROUTER_CS_1 to 4 `001101C0h 00000001h 80000000h 200010FEh`
(upstream adapter 1, route 1, Topology ID Valid), ROUTER_CS_5 `81000000h`
(Configuration Valid, PCIe Tunneling On), ROUTER_CS_6 `03000000h` (Router
Ready, Configuration Ready); the lanes bonded (LANE_ADP_CS_1 `4824003Ch`,
dual lanes); its USB4 port marked configured; its TMU uni-directional
HiFi.

**Linux's reset**: by default Linux resets such a fabric when its driver
binds: on a version 1 host, Downstream Port Reset on every downstream lane
adapter and the deactivation of every path entry, so the firmware's
tunnel is gone; with its `host_reset` parameter off it discovers the
tunnel instead ([linux] `tb.c` `tb_start`, `switch.c`
`tb_switch_reset_host`, `tunnel.c` `tb_tunnel_discover_pci`; section
9.1). The reset is exactly what section 9.1 rules out for this driver.

**After a hot plug with no tunnel built, the enclosure leaves the link
10 s later** in the emulator's asm2464 model [model] (section 14.1). On
the P14s's Thunderbolt 3 host the same enclosure falls back to USB about
9.4 s after its announcement (task 44.16; `usb4-emu`
`P14S-ORICO-2026-10-15.md` section 2.2).

**A tunnel Linux builds** leaves a USB4 router's protocol-adapter
credits, IFC and ISE as read ([linux] `path.c` `tb_path_activate`), as
this driver's does (section 6.8). The firmware's protocol-adapter
entries, by contrast, carry **0 credits** with IFC set, and its lane
entries enable counter 2.

### 18.4 What this driver does on this host, and what it must not do

As built and as this record says, read against sections 18.1 to 18.3 (item 3's
Stor98 binding as design record 31 will have it after task 44.3);
nothing below is a change:

1. **Admission**: the host interface is matched by `PCI\CC_0C0340` on
   every path (section 3.7) and admitted as a software-only candidate
   (section 3.4, case 1); its firmware-status register is never read
   (section 3.3). Every real host id acts untested (section 16); this is
   the first that can be read.
2. **No reset, no takeover.** The host router arrives with Topology ID
   Valid set, so section 6.1 verifies depth and Topology ID and writes
   nothing to ROUTER_CS_1 to 4. With `XhciCmPreOsCleanup` 0, the default,
   section 9.2 discovers the firmware's tunnel. Section 18.3's tunnel
   meets everything section 9.2 checks [hw]: the device router at depth 1 with
   Router Ready, PTO, CV and Configuration Ready (step 2), the lanes
   bonded (step 3), and the four entries chained at HopID 8 with Path
   Enable and L0 at both PCIe adapters (step 4). So the port is UP and
   preserved [inf: the registers read against the rules; the host suite's
   `test_u4cm` `test_asm4242_preos` holds the same four entries preserved
   and unwritten]. A HELD port would still leave the boot drive working
   [inf], served only by its own unplug. **This
   driver must not do what Linux does by default** (section 18.3): `XhciCmPreOsCleanup` 2
   removes a drive the system booted with, and 1 does so for a HELD port;
   the bench never sets either with the drive attached at power-on.
3. **The tunnel-port set: the gap.** The downstream ports are
   `1B21:2423`, which the filter INFs name (section 3.7) and
   `cm_ports.c` keeps as an id no family carries (`cmUnpaired`), because
   the host is matched by class and `CmPortsFamily` finds no family for
   `1B21:2425`. Section 7.6's discovery therefore finds no listed port:
   14:00.0 and 14:01.0 are candidates (Hot-Plug Capable, [hw]), 14:02.0
   and 14:03.0 are not (no slot), and the outcome is `UNLISTED`. The set
   then comes only from the router-named source, if this router answers
   Get PCIe Downstream Entry Mapping (unknown), or
   from `XhciCmTunnelBridge` (`14:00.0,14:01.0` in section 18.1's layout;
   the bus numbers move with a platform's PCIe population). With an
   empty set: no re-enumeration after a hot plug (NT's and 9x's routes
   alike), no safe-eject mark, and on 98 SE and ME no
   `PCI\XHCI98_TUNNEL&CC_010802` for the boot-time drive, since the
   CONFIGMG worker gives it only below an active member (design record 31
   section 5.1), so once task 44.3 has made Stor98's INFs match that id
   alone, Stor98 does not bind it there (until then they match
   `PCI\CC_010802`, so it binds by class); on 2000 and later the filter
   on 14:01.0 gives the id at enumeration by record 31 section 4's
   structural rule (task 44.3, not yet built) whether or not the port is
   a member. **Proposed, not taken** (section 18.7): a family for the
   ASM4242, NHI `1B21:2425` with switch ports `1B21:2423`, so section 7.6's
   switch source applies (Hot-Plug Capable ports only), as for Maple
   Ridge. **Taken** by the owner on 2026-10-25 and built as task 44.40:
   `cm_ports.c`'s family `asmedia-asm4242` (record 21 sections 3.3 and
   6.3), the INFs unchanged; the host suite's `test_asm4242_host` holds
   this topology to the set {14:00.0, 14:01.0} by the switch source,
   14:02.0 and 14:03.0 out. The text above stays as the reading that led
   to it.
4. **Hot plug**: polled (200 ms idle), enumerated, bonded and tunnelled
   with `XhciCmApprove` 1, the default, which has to complete within the
   enclosure's 10 s [model]; with `XhciCmApprove` 0 the enclosure leaves
   the link [model]. The tunnel ports' windows (48 buses, 384 MB, 12 KB I/O) stay
   in place with nothing attached [hw], so the ASM2464's two bridges and
   the NVMe fit them: on 98 SE and ME record 21 section 6.1's bridge
   numbering (section 6.1.1) places them inside 14:00.0's or 14:01.0's
   window, and the NVMe's pin, device 0 behind two bridges at device 0
   [hw], swizzles onto the port's own pin, whose Interrupt Line, where
   the CSM writes one, is the known wire that section's interrupt step
   needs [inf].
5. **Credits, TMU, CLx**: section 6.8 bounds the 174 buffers to the field;
   a preserved tunnel keeps the firmware's credits (64 on lane 3's entry)
   and the host's HiFi TMU mode until the unplug (section 9.2's stated
   cost); no CL state is enabled (the link reports none).
6. **The tunnel this driver builds at a plug** uses section 6.3's
   pairing and HopID 8 on the protocol adapters, its lane HopIDs and
   credits by section 6.8's allocation and accounting. On the two
   protocol-adapter entries section 6.8 keeps credits, IFC and ISE as
   read: on a router fresh from its reset or plug that is what the router
   reset them to, and after an earlier connection manager's teardown
   whatever it left. The firmware's tunnels carry PCIe with 0 credits and
   IFC set on both protocol-adapter entries, the host's PCIe Down and the
   device's PCIe Up [hw]; whether an entry with IFC clear carries PCIe on
   this host is not known. This is the open item section 16 already names for the
   ASM2464 ("the PCIe Up adapter credits kept as read"), and the first
   thing 44-E.6's hot plug reads (section 18.6 step 3).

### 18.5 Windows 98 SE, ME, 2000 and XP on an ASM4242 [inf unless marked]

- **A drive attached at power-on is plain PCI at boot**: the firmware's
  tunnel is up when the operating system's PCI enumerator scans, the
  NVMe's BAR is below 4 GB inside 14:01.0's window [hw]. On 98 SE and ME that is the case the
  BIOS numbered at POST, which worked on the P14s with the enclosure
  present at power-on (record 21 section 6.1.1); the bridge numbering of
  that section and record 28's forms concern a drive that arrives
  later.
- **The 64-bit prefetchable windows above 4 GB** are the first unknown.
  A reader of only the low registers sees each as 0 to 4 GB. Whether 98
  SE's and ME's `PCI.VXD` reads the upper halves, ignores the window,
  rewrites it or reports a conflict is unread; so is what Windows 2000's
  and XP's `pci.sys` make of a bridge window above 4 GB on a system that
  addresses no memory there. Nothing behind the tunnel uses a
  prefetchable BAR (the NVMe's is not), so closing those windows would
  lose nothing here. "Above 4G Decoding" is on throughout sections 18.1
  to 18.3.
- **"Above 4G Decoding" off does not move them**: the firmware still
  programs the bridges' prefetchable windows 64-bit and above 4 GB, and
  what changes is the host bridge's `_CRS`, which then offers no window
  above 4 GB. The emulator's 4G-off layout is that case [emu]
  (`runs/run-44.md`, "44.48" and "44.49"). Linux gives a bridge window it
  cannot claim space of its own ([linux] `drivers/pci/setup-bus.c`;
  record 32 section 4.2). So an operating system that keeps the
  firmware's bridge windows, as 98 SE, ME and 2000 do, still meets
  windows above 4 GB with the option off [inf]; in the emulator, on that
  layout, 2000 stops at "Starting up" and 98 SE and ME give the empty
  tunnel port Code 12, the latter probably an artifact of where SeaBIOS
  placed that window (`runs/run-44.md`, "44.48"). Not read on the
  hardware bench.
- **Which systems meet them, and the step for 98 SE and ME** (task
  44.49, `runs/run-44.md` "44.49") [emu]: on the emulator's firmware
  layout 32-bit Vista, 7, 8.1 and 10 give the ASM4242's root port
  problem 12 and enumerate nothing behind it, 32-bit 8 does not start,
  10 x64 needs the option on, XP moves the windows itself; 98 SE and ME
  write only the windows' low registers, and 2000's `pci.sys` writes the
  root port's window as 0 to `FFFFFFFF_FFFFFFFF` and loops in its range
  code (the "Starting up" stop); the tunnel port 98 SE leaves at problem
  12 is, in that kit, the one the enclosure is on, not the empty one. The
  step that closes those windows before 98 SE and ME read them is design
  record 32 (before code).
- **The firmware's own reservation settings** (Advanced, AMD PBS,
  "USB4 configuration (ASM4242 Controller)"):
  per USB4 port, PCIe Bus Number 16 to 56, Non-Prefetchable MMIO 256 to
  4096 MB, Prefetchable MMIO 1 to 512 GB; the defaults are 48
  buses, 384 MB and 128 GB, section 18.1's layout.
  The prefetchable reservation cannot be set to none, and no setting the
  firmware's documentation names places those windows below 4 GB. That documentation also states that "Re-Size BAR" on disables the CSM, so it stays off
  for these systems.
- **Interrupts**: none of this driver's (section 5, polling).
- A platform's memory size and processor are the operating systems' own
  preparation (98 SE's memory limit and the like), outside this record.

### 18.6 The hardware bench, task 44-E.6

After the cut, on the `3.3.0.0` asset (decision D13); step 1 uses no
build of this release, and whether it may be read before the cut is the
owner's question (section 18.7). Each step records the system, the BIOS
settings, Device Manager's resources and problem codes, the connection
manager's log or XHCITR's snapshot, and the time an event took.

1. **The platform, no driver of this release.** The BIOS settings: CSM,
   "Above 4G Decoding", Re-Size BAR, the USB4 items (PCIe tunnelling, the
   controller), the serial port. Then, booted by the CSM with the
   enclosure attached at power-on, 98 SE first, then ME, 2000 and XP:
   Device Manager's view of 00:02.2, 13:00.0, 14:00.0 to 14:03.0, the two
   ASMedia bridges and the NVMe (resources, problem codes), or a crash and
   its text; `usb4-emu`'s DOS `PCIDUMP` for the same functions under the
   CSM. Again with "Above 4G Decoding" off if any target fails.
2. **The drive present at boot, `3.3.0.0` installed** (connection manager
   and Stor98), on 98 SE, then 2000 and XP: the connection manager's
   discovery outcome for each USB4 port (preserved, HELD, empty) and the
   tunnel-port set's outcome and source; Stor98 bound or not; the drive's
   letter, a file copied and read back.
3. **Hot plug**: the enclosure unplugged (ejected first where the system
   offers it) and plugged again, five times per port, both ports: the time
   from the plug to the tunnel and to the drive's letter, and whether any
   plug lost the 10 s; whether the tunnel
   carries PCIe at all (the bridges and the NVMe enumerated; section 18.4
   item 6), with the path entries the log records; then, only if the
   default's tunnel does not carry PCIe, `XhciCmAsmediaPcie7Credits` 1
   (section 18.8) from a verified fresh state - the machine and the
   enclosure powered off, not a warm restart or a replug, since setting
   the value back to 0 keeps whatever the entries hold - and the same
   plugs again, the log's `u4.asm7.*` lines kept; then, with
   `XhciCmApprove` 0 set and the system restarted (the driver reads its
   values once per start), one plug (the enclosure expected to leave the
   link and not to appear as USB), and the value set back.
4. **If step 2 found the set empty**: `XhciCmTunnelBridge` set to the two
   tunnel ports read in step 1, the system restarted, and steps 2 and 3
   again.
5. **Windows 7 x64** (test-signing), the same steps 2 and 3, as the target
   whose PCI stack is newest.
6. **The ASM4242's xHCI under `xhci98.sys`**: a USB 3 stick and a USB 2
   device on each USB4 port's connector with no tunnel involved, on 98 SE
   and 2000.

Evidence goes to the run sheet's 44-E.6 section and, for logs and
screenshots, the git-ignored `xhciqual/results/` or `temp\` as the run
sheets do.

### 18.7 Open, and the owner's

- **The tunnel-port set (section 18.4 item 3)**: answered by the owner on
  2026-10-25, (a), built as task 44.40. The question as it was put: (a)
  the ASM4242 family
  row in `cm_ports.c` and record 21 section 3.3's table (NHI `1B21:2425`,
  switch ports `1B21:2423`; a few lines and a host vector, the INFs
  unchanged), before the bench; (b) the README's `XhciCmTunnelBridge`
  procedure alone, the bus numbers board- and population-specific; (c)
  wait for the bench's reading of Get PCIe Downstream Entry Mapping.
  Recommended: (a), reviewed as a code change of its own, since without a
  set 98 SE and ME bind no driver to the boot-time drive and no target
  re-enumerates after a hot plug.
- **When step 1 is read**: after the cut as D13 says, or now, since it
  needs no build of this release and decides whether the targets boot at
  all on an ASM4242. Recommended: now. The owner, 2026-10-25: whenever
  the hardware bench is set up.
- **What sections 18.1 to 18.3 leave**: Get PCIe Downstream Entry
  Mapping; the 64-bit windows under each target (step 1); whether a
  tunnel built inside the 10 s timer keeps the enclosure, as it does in
  the emulator's model [model].
- **The protocol-adapter entries (section 18.4 item 6)**: (a) build as
  designed and read it on the bench first, a quirk row of section 6.10
  for `174C` routers (7 credits and IFC on the PCIe adapters' HopID 8
  entries) taken only if the tunnel does not carry
  PCIe; (b) take that row now, before the bench. Recommended: (a), since
  the firmware's own tunnel shows the host side working with 0 credits,
  and a row taken without a failing reading would change every USB4
  host's ASMedia device; the bench reading is cheap and the fallback
  small. Answered (the owner, 2026-10-25, through the coordinator): an
  opt-in bench switch, the default unchanged (section 18.8, task 44.41).

### 18.8 `XhciCmAsmediaPcie7Credits`, the credits experiment (task 44.41)

An explicit, opt-in exception to section 6.8's rule for one router pair,
so that the bench can try 7 credits and IFC on those entries without a
rebuild; the default is unchanged, and the experiment is read on the
hardware bench (section 18.6 step 3). The rule
it departs from: on a USB4 protocol adapter the path entry's Path Credits
Allocated, IFC and ISE are vendor-defined, and the connection manager
keeps them as read (USB4 Specification 2.0 Table 8-23; Connection Manager
Guide 2.0 Table 5-2, note 1); a protocol adapter's 0 is therefore not
invalid for PCIe, and 7 is no USB4 requirement.

- **The value**: `XhciCmAsmediaPcie7Credits`, `REG_DWORD`, read once per
  start under the usual precedence (the machine-wide key, then the host
  interface's driver key); absent or 0 is today's behaviour, 1 the
  experiment; any other value is refused, 0 stands, and the log says so
  (`cm.asm.pcie7`, bit 8 found, bit 9 refused, then `cm.asm.pcie7.raw`).
  The INFs write it at 0 since task 45.50 (record 21 section 8.1), never
  over a user's value; the installer neither asks for it nor writes it.
- **Where it acts**: only on a PCIe tunnel this driver builds (a plug, a
  re-plug, a rebuild after a resume of a tunnel it built), only when the
  host router reads `174C:2428` and the connected device router
  `174C:2463` (a USB4 router, not a Thunderbolt 3 one), and only on the
  two protocol-adapter source entries: the host's PCIe Down HopID 8 and
  the device's PCIe Up HopID 8, written with Path Credits Allocated 7,
  IFC 1, ISE 0 and, as always, EFC 1, ESE 0. The lane entries keep
  section 6.8's accounting; USB3 and DisplayPort paths, preserved or HELD
  tunnels (which discovery never writes), and every other router pair are
  untouched, whatever the value.
- **The log**: `u4.asm7.gate` once per tunnel built while the value is 1
  (1 applied, 0 not this pair or not PCIe); then for each of the two
  entries, under `u4.asm7.host` and `u4.asm7.dev`, six lines: the two
  DWs as read, as written and as read back.
- **On the bench** (section 18.6 step 3): the default first; the
  experiment only if the default's tunnel does not carry PCIe, and only
  from a verified fresh state, the machine and the enclosure powered off,
  since setting the value back to 0 keeps the 7 credits the hardware
  then holds (0 means "as read").
- **Host vectors**: `test_cm` (the value's rule) and `test_u4cm`'s
  `test_asm7` (off: the entries as read and nothing logged; on: 7, IFC,
  EFC, ISE 0 on both protocol entries with the lane entries unchanged,
  the thirteen log lines; on with an Intel host or another device: the
  gate closed; on with a pre-OS tunnel: preserved, nothing written).

### 18.9 Pending Packets on the Host Interface adapter

A public Linux patch on the kernel mailing list ("thunderbolt: Stop
waiting on a path pending bit that never clears", v3, tested on two
ASM4242 boards) reports that this host router's Host Interface adapter
sets Pending Packets (PATH_CS_1 bit 28) on a path entry whose Valid is
cleared and never clears it, so a connection manager that waits for it
(Guide 5.2.2, section 6.8 here) waits out its whole timeout at every
teardown; the patch stops waiting there. `usb4-emu` models it as the
`asm4242` host model's quirk a (`asm-quirk-pending`, on by default; the
bit cleared again when the entry is made valid or the router reset, its
inference). Not read on the hardware bench.

What it did to this driver [emu]: the network service's transmit entry
is the Host Interface adapter's entry at the ring's HopID (record 35
section 5.1). At an unplug its Valid clear waited out the 500 ms drain
and the entry was quarantined (section 7.3, `u4.quarantine` 0x00050001:
adapter 5, HopID 1); the quarantine is lifted only by a read of Valid 0
and Pending Packets 0, which never came. The entry, pending with its
output at the port's lane 0 adapter, kept that lane busy in the lane
scan, so every later connection of the port was deferred
(`u4.connect.deferred`, Guide 7.1's no-path-through-the-port rule) and
nothing was sent on the link again until the driver restarted. With the
model's quirk off, the same replug recovered.

So on this router only (ROUTER_CS_0 `174C:2428`, the identity section
18.8 and record 35 section 5.3 key on), and on its Host Interface adapter
only, every read of a path entry returns Pending Packets as 0
(`tbtU4PpStuck` in `tbtU4RdN`): every reader - the drain, the HopID and
credit scan, the quarantine's retry, the sweeps - takes an entry there as
drained once it reads Valid 0, as the patch does. The bit is counted
(`PendingIgnored`) and the first one logged per instance (`u4.asm.pp`,
the adapter in bits 16 and up, the HopID below). Every other router and
adapter keeps the wait and the quarantine. On that adapter the bit
tells nothing either way, so the wait could prove nothing there; the
service's own order down (record 35 section 5.1) stops new sends and
gives the transmit ring up to 500 ms to drain before the entry's Valid
is cleared (what is still outstanding then is counted, `cm.net.tx.left`,
and dropped with the ring), and the receive entry, at the lane adapter,
keeps the drain.
Host vectors: `test_u4xd`'s `test_asm_replug_pp` (the identity, the
fabric's bit stuck: nothing quarantined, the service up again after a
replug) and `test_pp_stuck_other` (the same fabric under another
identity: the entry quarantined, no second service).

## 19. Thunderbolt 1, 2 and 3 hosts on Apple Macs (task 45.21)

Added 2026-10-29 for `roadmap-usb4.md` task 45.21 (the owner, the
decisions table's "Phase 45 additions"): this driver as the connection
manager of the Thunderbolt hosts Apple's Intel Macs carry, which run no
connection manager in firmware. Written first as a reading and a design;
coded on 2026-10-30 (section 19.9), after the `usb4-emu` model the owner
asked for first (Q4). Section 3.1's rule that a host with PCI subsystem
vendor `106B` starts inert now holds for every row but this section's;
the owner decided on 2026-10-30 that the code is active on a Mac from
this release, before the hardware reading (45-E.11; section 19.9, Q5). The release notes' "Not
claimed: Apple machines" stays as it is until that reading.

Tags, beside this record's own: **[linux]** Linux `drivers/thunderbolt`
at the pinned `c6859eed` (`external/README.md`), GPL-2.0-only, read for
facts only and cited by file and symbol, nothing adapted (AGENTS.md,
"Reference Implementations"); **[pub]** Apple's public support pages, as
listed under "Sources"; **[unread]** a question no source here answers,
for the bench.

### 19.1 The hosts, and who runs them on a Mac

| Host NHI | Its host router (switch id) | Generation | Linux on a Mac | Source |
|---|---|---|---|---|
| Light Ridge `8086:1513`, class `0880h`, subsystem `2222:1111` | `1513` | 1 | software connection manager: no `icm_probe` row | [linux] `pci.c` `nhi_ids[]` (the Thunderbolt 1 rows match class `PCI_CLASS_SYSTEM_OTHER` and that subsystem only, "the TB bridges use the same device and vendor (sub)id on gen 1 and gen 2 controllers"), `switch.c` `tb_switch_get_generation`, `icm.c` `icm_probe` |
| Cactus Ridge 4C `8086:1547`, class `0880h`, subsystem `2222:1111` | `1547` | 1 | the same | the same |
| Falcon Ridge 2C `156A`, 4C `156C` | `156B`, `156D` | 2 | software: `icm_fr_is_supported` is false on an Apple machine | [linux] `icm.c` `icm_fr_is_supported`; `pci_ids.h` |
| Alpine Ridge 2C `1575`, 4C `1577`, LP `15BF`, C 4C `15D2`, C 2C `15D9`; Titan Ridge 2C `15E8`, 4C `15EB` | `1576`, `1578`, `15C0`, `15D3`, `15DA`; `15E7`, `15EA` | 3 | software unless the ICM firmware already runs or the `start_icm` module option asks Linux to reset and start it (off by default) | [linux] `icm.c` `icm_ar_is_supported` (`icm_firmware_running`, `start_icm`); `nhi.h`, `pci_ids.h` |

The host router's device id is not the NHI's from Falcon Ridge on; the
second column is the mapping test (d) uses (section 19.3).

Linux recognises an Apple machine by its firmware's vendor string
(`x86_apple_machine`, from `linux/platform_data/x86/apple.h`, not
mirrored here) [linux]; this driver has no such reading and uses the
NHI's PCI subsystem ids instead (section 19.3). The other Thunderbolt 1
and 2 parts in Linux's tables - Eagle Ridge `151A`, Light Peak `151B`,
Port Ridge `1549`, Redwood Ridge (`1566` to `1569`), Cactus Ridge 2C
`1548`, Win Ridge `157D` `157E` - are switches Linux meets as **device
routers** (`tb_switch_get_generation`); none is in `nhi_ids[]` as a host
but Light Ridge and Cactus Ridge 4C [linux]. So the hosts this task
claims are the five rows above, and Thunderbolt 1 and 2 hosts on PCs
stay not claimed (the decisions table, "Phase 45 additions").

### 19.2 Which Windows the Macs run, as far as public facts say

- Apple supplied Windows support software for Windows 7 only on the
  first Thunderbolt Macs of 2011, 32-bit and 64-bit, with no Windows XP
  or Vista drivers ("Windows XP and Vista drivers are not supplied for
  these computers and are not supported"), and Boot Camp 4 ran Windows 7
  only [pub].
- Boot Camp Support Software 5.0 and 5.1 (the Macs of 2011 to 2014,
  Light Ridge to Falcon Ridge) are for the 64-bit Windows 7, 8 and 8.1
  [pub].
- Windows 10 is installed 64-bit only, from Mac models of 2012 on [pub];
  Apple's page does not say whether it starts there through the firmware's
  BIOS emulation or through UEFI, and this record does not say either
  [unread].
- Apple lists **no** 98 SE, ME, 2000, XP or Vista for any Thunderbolt
  Mac [pub]. Whether any of them starts at all through the firmware's
  BIOS emulation on these Macs, and with what of the machine working
  (storage, the keyboard, the xHCI controller), is not in any public
  source read here [unread]; the Macs of 2018 on, with Apple's T2 chip
  (Titan Ridge among them), are commonly described as starting Windows
  through UEFI only, which is not confirmed here [unread].

So the systems this work can reach on a Mac are, by Apple's own support,
the 32-bit and 64-bit Windows 7 and the 64-bit 8, 8.1 and 10, all run
from this project's x64 package in test-signing mode where 64-bit
(design record 19); 98 SE to Vista are owed to the bench, which reads
first whether each starts (45-E.11), and nothing in this design depends
on them starting.

### 19.3 Selection: a third CM-table row, by subsystem and firmware status

Section 3.1's table gains one row, kept apart from the others:

| Matched | Hardware profile | CM candidates |
|---|---|---|
| Light Ridge `1513` and Cactus Ridge 4C `1547` with subsystem `2222:1111`; Falcon Ridge `156A` `156C`, Alpine Ridge and Titan Ridge NHIs with PCI subsystem vendor `106B` | discrete Thunderbolt, Apple | the software connection manager's Thunderbolt-host profile (section 19.4), or record 21's ICM client where its firmware runs |

- Light Ridge and Cactus Ridge take the row by class **and** subsystem,
  as Linux's table does [linux]: their PCI bridges carry the same vendor,
  device and subsystem ids, so an INF line of id and subsystem alone
  would bind this driver as a bridge's function driver and leave nothing
  enumerated behind it. A Windows PCI hardware id carries the subsystem
  or the class, never both, so the INF line is the class form
  (`PCI\VEN_8086&DEV_1513&CC_0880` and the `1547` twin, which no bridge,
  class `0604h`, ever presents), and the start checks the subsystem pair
  and is inert on any other (a PC's Light Ridge or Cactus Ridge then sits
  started and inert, as section 3.5's start does). The ids Windows 98 SE
  and ME build for such a function are read before any 9x INF line is
  written [unread].
- Falcon, Alpine and Titan Ridge are already this driver's (record 21);
  at start, subsystem vendor `106B` moves them from "inert" (section 3.1)
  to this row.
- **Test (c)** for the row: on Falcon, Alpine and Titan Ridge a valid read
  of the firmware status register (`0x39944`) with `ICM_EN` set keeps
  record 21's ICM client, unchanged; `ICM_EN` clear on a valid read admits
  the Thunderbolt-host profile; no valid read is inert. This driver never
  resets or starts the ICM itself (Linux's `start_icm` path is not taken).
  On Light Ridge and Cactus Ridge, which have no ICM [linux], the profile
  is admitted once section 3.2's readiness passes.
- **Test (d)** for the row is not section 3.5's: the host interface's
  version field is read and logged, not checked, since a Thunderbolt 1, 2
  or 3 host interface predates the USB4 versions [unread: the value these
  hosts report]; the host router must answer ROUTER_CS_0 to 4 at route 0
  and report a USB4 Version of major 0 (section 3.5: a Thunderbolt router),
  and its device id (ROUTER_CS_0) must be the host router section 19.1's
  table pairs with the bound NHI (`tb_switch_get_generation` [linux] gives
  the generation); anything else is inert.
- `XhciCmMode` 3 is refused on the row, as it is on the discrete
  Thunderbolt rows today (decision U1).

Whether a Mac's firmware leaves a Thunderbolt host powered and on the PCI
bus at all when nothing was attached at power-on is [unread]: Linux
carries PCI quirks for the Macs' Thunderbolt power (in
`drivers/pci/quirks.c`, not mirrored here and not read); a host the
firmware powered off is simply absent from the PCI bus, and this design
does nothing about it (non-goal, section 19.7).

### 19.4 The Thunderbolt-host profile: what differs from section 6

The profile is the USB4 software connection manager's own code (transport,
ports, paths, the PCIe tunnel, hot plug, the PCI side, Stor98's contract)
with section 8's Thunderbolt 3 device-router handling **applied to the
host router at route 0 as well**, and extended to generations 1 and 2.
What a Thunderbolt host router needs that a USB4 host router does not
[linux unless marked]:

0. **Its ports are not USB4 ports.** Section 6.3's host-port discovery
   requires the USB4 Port Capability and takes a port's state and its
   configured marking from it; a Thunderbolt router's lane adapters have
   none (today's discovery refuses such a lane adapter, `tbt_u4cm.c`).
   The profile's discovery instead: a lane adapter is an adapter of the
   null ("port") type with the PHY capability (`switch.c` `tb_init_port`,
   `TB_PORT_CAP_PHY`), its link state read there; lanes 0 and 1 are paired
   as two consecutive null adapters (`switch.c`
   `tb_switch_default_link_ports`: adapter i and i + 1, unless the DROM
   pairs them); and the host side's "port configured" marking is the link
   controller's (`lc.c` `tb_lc_configure_port`, generation 2 and later),
   not PORT_CS_19 PC, on the host as on the device (section 8 item 3).

1. **No USB4 router registers.** No CMUV, no ROUTER_CS_5, no
   Configuration Valid handshake, no router operations, no Buffer
   Allocation Request: the host router is configured by the one write of
   ROUTER_CS_1 to 4 with Valid, as section 8 item 2 does for a device
   router (`switch.c` `tb_switch_configure`, the non-USB4 branch); its
   Plug Events capability is required and written active (bits 2 to 6 of
   its second dword cleared; the USB-disable bit set except on Light
   Ridge, Eagle Ridge, Port Ridge and Alpine Ridge, `tb_plug_events_active`).
2. **Light Ridge's two port-space quirks** (`cap.c`): TMU access enabled
   (bit 20, `TMU_ACCESS_EN`, of switch dword `26h` on Light Ridge, `2Ah` on
   Eagle Ridge) around a walk of a port's capabilities, and one dummy read
   of port dword 0 after the walk, because the next-capability read
   returns stale data on Light Ridge.
3. **The link controller** (`lc.c`): present from generation 2 (every
   `tb_lc_*` returns early below it), so on Light Ridge and Cactus Ridge
   there is no port-configured marking, no UUID from the fuses and no
   lane bonding through it; Falcon Ridge and later as section 8 item 3.
   The DROM through the Plug Events EEPROM bit-bang (section 8 item 4)
   applies to every generation.
3a. **Identity without the fuses**: the identity's source is chosen once
   per router, at its first connect, and recorded with it: the link
   controller's fused UUID where the router has a link controller
   capability, and only where it has **none** (generation 1) the UID read
   from its DROM alone (`eeprom.c` `tb_drom_read_uid_only`), the UUID
   formed as Linux forms it (the UID's two halves, then two dwords of
   ones). A link-controller read that fails on a router that has the
   capability is a failed identity read, never a reason to fall back to
   the DROM, as Linux falls back only when the capability is missing
   (`switch.c` `tb_switch_set_uuid`, which propagates every other error);
   every later comparison (the replacement check, the resume) reads the
   recorded source. Here the DROM read is **not** optional as section 8
   item 4 makes it for naming: identity is what preservation (section 9),
   the replacement check and the resume (section 11) compare. A router
   whose identity cannot be read is driven for this connection only and
   never matched to a record; after a resume a router whose identity
   cannot be confirmed is treated as section 11 treats a failed
   verification - its port held, never rebuilt on that ground alone, as
   Linux refuses such a router at resume (`switch.c` `tb_switch_resume`)
   - and the log says so (`u4.tbt.noid`).
4. **The host's PCIe down adapter for each Thunderbolt port** is fixed per
   controller (`tb.c` `tb_find_pcie_down`): adapter 6 or 7 on Cactus Ridge
   and Alpine Ridge, 6 or 8 on Falcon Ridge, 8 or 9 on Titan Ridge, by the
   port's physical index; Light Ridge has no fixed map and takes the first
   free PCIe down adapter. Section 7.6's tunnel-port set (the PCI bridge
   behind each adapter) is then read through record 21 section 6.3's
   discrete-switch source, which needs two additions: that source's
   family table gains Light Ridge and Cactus Ridge (their switches'
   downstream ports, with Hot-Plug Capable set, as the tunnel ports;
   their bridges are `1513` and `1547` like their NHIs, so the bridge
   side is told apart by class `0604h`), and the tunnel-port filter's INF
   line for them is the class form too (`PCI\VEN_8086&DEV_1513&CC_0604`,
   with the NHI's `CC_0880` never matching it). Falcon, Alpine and Titan
   Ridge are in that table already.
5. **No USB3 tunnel, ever** (every link is Thunderbolt); and the credits
   are chosen per router, by what the router reports, as Linux chooses
   them (`switch.c` `tb_switch_credits_init`): section 8 item 5's
   defaults, with no USB4 credit negotiation, on every router of
   generation 1 to 3, the host included; a USB4 child (item 8) keeps
   section 6.8's own capability and credit rules.
6. **TMU**: not configured, as section 8 item 6; if a host or device
   router of generation 1 or 2 carries no PCIe tunnel without it, Linux's
   sequence (`tb.c` `tb_enable_tmu`) is the second pass (section 16).
7. **Reset**: a Thunderbolt host is never reset by default (section 9's
   discover-and-preserve holds), and `XhciCmPreOsCleanup` keeps section
   9.3's meaning, port by port: value 1 cleans only the ports that do not
   verify, value 2 every port with tunnel state, each by section 9.3's
   one-port sequence. Its step 3, the Downstream Port Reset, is the link
   controller's port reset on generations 2 and 3 (`lc.c`
   `tb_lc_reset_port`, which refuses below generation 2); on generation 1
   there is no per-port reset, so step 3 is skipped there and the port's
   paths are torn down alone. Linux's whole-router reset
   (`switch.c` `tb_switch_reset_host`: every lane adapter reset and every
   protocol adapter's paths cleared, DisplayPort included; the "reset"
   configuration packet on generation 1) is never used, since it would
   destroy the verified tunnels of the other ports. At a resume, Linux resets
   a non-USB4 host root router before rebuilding its tunnels
   (`tb.c` `tb_resume_noirq`, "remove any PCIe devices the firmware might
   have setup"); this profile follows section 11's resume as it is and
   reads on the bench whether the Mac's firmware rebuilt tunnels at the
   resume (45-E.11) before deciding that.
8. **Device routers behind the host**: generations 1 to 3, section 8 as
   it stands for generation 3, plus items 0 to 3a above for generations 1
   and 2. A router is programmed by what it reports, not by the link:
   one whose USB4 Version has major 1 or 2 is a USB4 router and is driven
   by section 6.4 (CMUV, Configuration Valid, its USB4 ports), with no
   USB3 tunnel because its upstream link runs in Thunderbolt mode
   ([linux] `tb.h` `tb_switch_is_usb4` classifies the router from its
   version; `usb4.c` `usb4_switch_setup` reads the link's mode through
   `link_is_usb4` and withholds USB3 on a Thunderbolt link).
9. **Paths** use the same hop-table entries on every generation
   (`path.c` makes no generation distinction) [linux]; the path entry
   layout this driver writes (section 6.8) is unchanged.

### 19.5 DisplayPort

Thunderbolt 1 and 2 displays (Apple's Thunderbolt Display among them) are
DisplayPort tunnels from the host's DP IN adapters, which carry the
Mac's graphics output. This driver builds no DisplayPort tunnel today
(section 8's "Left out", section 16); the Mac's firmware may have built
one before Windows started (preserved by section 9 like any tunnel).
Building them is not in this design: a DP tunnel needs, from generation 2,
the DP IN to DP OUT capability exchange and its connection-manager
handshake (`tunnel.c` `tb_dp_xchg_caps`, skipped below generation 2), the
sink allocation from generation 3 (`lc.c`), bandwidth accounting, and a
GPU driver on the Windows side that sees the display, none of which is
read here. A Thunderbolt display plugged while Windows runs then shows
nothing; one present at power-on works only as far as the firmware's
tunnel survives (owner question Q3).

### 19.6 Verification (when it is coded)

- **Host vectors** in `test_u4cm`: the row's selection (each id and
  subsystem, `ICM_EN` set and clear, a refused router id), the host
  router's configuration without the USB4 registers, the Light Ridge
  quirks, the PCIe down adapter map, generation 1 and 2 device routers
  behind each host.
- **The emulator**: `usb4-emu`'s Thunderbolt-host profiles (its
  `docs/TB-SWCM-HOSTS.md`, added 2026-10-30 for this task):
  `tb3-apple-alpine-ridge`, `tb3-apple-titan-ridge`,
  `tb2-apple-falcon-ridge`, `tb1-apple-cactus-ridge` and
  `tb1-apple-light-ridge`, with `usb4-device tb3-router-id` for generation 1
  and 2 device routers; the q35 legs (7 x86 and 10 x64) run on them.
- **The bench**, 45-E.11: on each Mac at hand, which of 98 SE to 10
  starts, the NHI's ids and its host interface's version field, whether
  the host is on the PCI bus when nothing is attached at power-on, a
  PCIe device (an NVMe enclosure, a dock) plugged before power-on and
  while Windows runs, a resume, and a display.

### 19.7 Non-goals

DisplayPort tunnels (section 19.5); powering a host the firmware switched
off (section 19.3); starting or resetting the ICM on Alpine and Titan
Ridge; NVM upgrade; XDomain (Mac to Mac); Thunderbolt 1 and 2 hosts on
PCs and the Thunderbolt 2 hosts other than Falcon Ridge (the decisions
table, "Phase 45 additions"); Apple Silicon Macs, which run no x86
Windows.

### 19.8 Open, and the owner's

Decided by the owner on 2026-10-29: Q1, all three generations in this
task - Alpine and Titan Ridge, Falcon Ridge, Light and Cactus Ridge -
each after its emulator model (Q4); Q2 (a), the systems Apple supports
there first; Q3 (a), DisplayPort out; Q4 yes, a Thunderbolt-host
profile in `usb4-emu` before any code. The options as they were put:

- **Q1, the first host.** Which Mac comes to the bench first decides the
  order of the code: (a) an Alpine or Titan Ridge Mac (Thunderbolt 3:
  section 8 already drives such routers as device routers, so the host
  profile is the smallest step), (b) a Falcon Ridge Mac, (c) a Light
  Ridge or Cactus Ridge Mac (both generation 1 quirks and no link
  controller). Recommended: (a), then (b), then (c), each after its
  emulator model.
- **Q2, the systems.** (a) the systems Apple supports there (7, 8, 8.1,
  10; section 19.2) first and 98 SE to Vista only if the bench shows them
  starting; (b) 98 SE and XP read first. Recommended: (a).
- **Q3, DisplayPort.** (a) out, as section 19.5 says, the release notes
  saying a Thunderbolt display is not driven; (b) a later task of its
  own. Recommended: (a) now, (b) if the owner wants displays.
- **Q4, the emulator model.** A Thunderbolt-host profile in `usb4-emu`
  (section 19.6) before any code here. Recommended: yes, Alpine Ridge
  first.

### 19.9 The code (2026-10-30)

What the code does, section by section, and where it settles what the
design left to it (reviewed and revised):

- **Selection** (section 19.3): `TbtHwSelect` takes the NHI's subsystem
  dword whole. Falcon, Alpine and Titan Ridge rows carry `TBT_HW_MAC_HOST`:
  with subsystem vendor `106B` and a valid firmware status, `ICM_EN` clear
  is the software route `TBT_SW_ROUTE_TBT_HOST` with the host router id the
  NHI pairs with (`TbtHwTbtHostRouter`, section 19.1's table), `ICM_EN` set
  is record 21's admission as on any machine. Light Ridge and Cactus Ridge
  4C are rows of `tbt_hw.c`'s table (`TBT_HW_MAC_ONLY`) admitted only with
  subsystem `2222:1111`, inert (`TBT_INERT_NOT_MAC`) on any other, and no
  firmware status read. `XhciCmMode` 3 is refused on every row of the
  profile. Test (d)'s interface version is logged, not checked.
- **Their INF lines**: the class form section 19.3 names
  (`PCI\VEN_8086&DEV_1513&CC_0880` and `DEV_1547`), in the NT 6.x models
  sections alone (`xhcicm.inf`'s `.NTx86.6.0`, `xhcicm-amd64.inf`'s
  `.NTamd64.6.0`): the undecorated section is Windows 98 SE's, ME's,
  2000's and XP's, whose ids for them are unread, and none of those runs
  on these Macs (section 19.2). Their bridges are the filter INFs'
  (`&CC_0604`, every section) and `cm_ports.c`'s `light-ridge` and
  `cactus-ridge` families, the discrete-switch source taking type 1
  siblings only, so the NHI that shares the id is never a member. The CM
  INF gate's CM-TB1 rule holds this; the names are `xHCI98 TB1
  Controller (Intel Light Ridge)` and `(Intel Cactus Ridge)`, in the
  installer's table too. Falcon, Alpine and Titan Ridge are bound by their
  existing lines.
- **Test (d), the router half** (`tbtU4HostRouter`): a Thunderbolt router
  (USB4 Version major 0) whose id is the selection's, else inert
  (`TBT_INERT_ROUTER_VERSION`, `TBT_INERT_HOST_ID`); a Thunderbolt host
  router without the profile stays refused as before.
- **Item 0**: the ports are the odd lane adapters with the lane capability,
  each with the next one; the host's "port configured" mark is its link
  controller's lane bit (no UPSTREAM) from generation 2, none below.
- **Item 1**: configured by `tbtU4Enumerate`'s one write of ROUTER_CS_1 to
  4 (Valid, Notification Timeout 255, no CMUV, no Router Ready); no router
  operation and no Buffer Allocation on the host; its Plug Events made
  active by section 8's own setup, the USB event kept on Alpine Ridge and,
  as Linux keeps it, on Light Ridge, Eagle Ridge and Port Ridge.
- **Item 2**: around each adapter's capability walk on Light Ridge (router
  DW 26h) and Eagle Ridge (2Ah), TMU access set and cleared, then one read
  of the adapter's DW 0.
- **Item 3a**: one helper, `tbtU4TbtUuid`, reads the identity from its
  recorded source (`UidFromDrom` when a generation 1 router has no link
  controller); the start, the adoption, the resume's verification and a
  resume's held classification and the ordinary replacement check all use
  it. A resume that cannot confirm a Thunderbolt router's identity leaves
  the port held. The DROM UID is read under a resume budget too: the
  EEPROM's release writes are refused only by a contained or cancelled
  instance, never by a spent budget.
- **Item 4**: the PCIe Down map by the host router's id, the port's
  physical index from its lane 0 adapter; an adapter the map names that
  is not a PCIe Down adapter falls back to the pairing in increasing
  adapter number. **Light Ridge**, which Linux maps to its first free PCIe
  Down adapter, takes that pairing too (6 for the first port, 7 for the
  second): always free for its own port, and each port's devices stay
  behind one bridge, the consistency `tb_find_pcie_down`'s own comment
  asks for. `usb4-emu`'s Light Ridge accepts either.
- **Item 5**: no USB3 tunnel (`tbtU4LinkUsb4` is false on every link of a
  Thunderbolt host); the host's protocol ingress entry gets 7 credits and
  IFC, as a Thunderbolt router's ([linux] `tunnel.c` `tb_pci_init_credits`
  without credit allocation), the lanes section 8's 32 or 16.
- **Lanes that do not bond** (Linux bonds a Thunderbolt link only through
  the link controller's port attributes, so from generation 2, `lc.c`
  `tb_lc_lane_bonding_possible`, and never on generation 1): the link
  stays two single lanes. Section 6.5 step 6.4's Lane Disable is a USB4
  lane adapter's and is not written on a Thunderbolt host, and neither is
  step 6.3's TMU Off required there, which Guide 7.5 asks for only before
  a lane is disabled (task 45.58: the TMUs of a generation 1 link that
  came up running failed the connection until then); the discovery, the
  resume and the recovery accept that state as they accept a single lane
  on a USB4 link (one predicate).
- **Item 6**: the host router's TMU is left as it is.
- **Item 7**: the cleaning's step 3 is the link controller's PORT_MODE
  Downstream Port Reset on generations 2 and 3 and nothing on 1; the
  whole-router reset (and generation 1's reset packet) is never sent. The
  reset is owned before its write and released and read back clear before
  the port connects again; the host's port-configured mark likewise counts
  neither as made nor as undone while its link controller cannot be read.
- **Vectors** (section 19.6): `test_tbt_select` (`test_mac`: every pair,
  each refusal), `test_tbt_hw`, and `test_u4cm`'s `test_tbt_host` against
  the fake fabric's `fabHostTbt` (Alpine, Titan, Falcon, Cactus and Light
  Ridge; a Port Ridge device router with its DROM UID; the cleaning).

Q5, put to the owner with this code (answered 2026-10-30: (a)): (a) the profile active on a Mac from
this release, the release notes still not claiming Apple machines; (b)
active only with a registry value set (an INF default 0) until 45-E.11 has
read a Mac. Recommended: (a) - inert, a Mac's Thunderbolt does nothing at
all, the profile is reached only on the ids and subsystems above, and (b)
adds a value every INF path must write.

### 19.10 What is left for the emulator, and how it is read (task 47.21, 2026-11-06)

Task 47.21 (roadmap Phase 47; was 45.21) continues where 45.21 and 45.58
stopped. Before it: section 19.9's code and its host vectors for every
row, and q35 legs on 7 x86 and 10 x64 with the Alpine, Falcon, Cactus and
Light Ridge models (`runs/run-45.md`, "45.21 (code)" and "45.58"). The
hardware stays 45-E.11, and the release notes keep "Not claimed: Apple
machines" until it (section 19.8, Q5). What the emulator can still read,
each item a leg, an emulator change or a code change:

1. **Titan Ridge.** No leg has run `tb3-apple-titan-ridge`: its q35
   switch (`usb4-tr-*`), the host router `15EA`, PCIe Down 8 and 9 (item
   4 of section 19.4). Legs on 7 x86 and 10 x64, as 45.58's: the
   install, the enclosure at power-on on port A, an unplug, a plug on
   port B.
2. **The rest of section 19.2's systems.** 7 x64, 8 x64 and 8.1 x64 have
   no leg. One host each, by the generation of the Macs Apple supported
   that system on (section 19.2): Light Ridge on 7 x64 (the Macs of
   2011), Cactus Ridge on 8 x64 and Falcon Ridge on 8.1 x64 (Boot Camp
   5's Macs of 2012 to 2014). Each leg as item 1's; the x64 images signed
   in the guest with a throwaway certificate (design record 19's route
   for a clone without the key).
3. **A device of another generation** (section 19.4 item 8). Behind the
   Alpine Ridge host, a Falcon Ridge (`156D`, generation 2, a link
   controller) and a Port Ridge (`1549`, generation 1, none) device
   router; behind the Cactus Ridge host, a generation 3 device router
   (the model's default `15D3`). The first two are the older device on a
   later Mac (on a real Mac through Apple's Thunderbolt 3 to Thunderbolt
   2 adapter, which the emulator does not model: it links the two
   routers directly [emu]). The link bonds as Linux bonds it, and as
   `tbtU4BondPossible` does: where the device router's link controller
   reports bonding possible (its upstream port's PORT_ATTR BE, so
   generation 2 and later) and both lane 0 adapters list two lanes
   ([linux] `switch.c` `tb_switch_lane_bonding_possible`,
   `tb_port_width_supported`, `lc.c` `tb_lc_lane_bonding_possible`); the
   host's own link controller is not asked. So the Port Ridge device's
   link stays two single lanes, and the Falcon Ridge device's and, on
   the emulator, the generation 3 device's on Cactus Ridge bond: the
   model's Cactus Ridge lane adapters are Alpine Ridge's image and list
   two lanes [emu]; whether a real generation 1 host's do is [unread].
   Legs on 7 x86.
4. **Tunnels the Mac's firmware built** (section 9 on this row; section
   19.4 item 7). Linux's software connection manager discovers the boot
   firmware's tunnels on a Thunderbolt host and keeps them (`tb.c`
   `tb_start`: discovery is skipped only on a USB4 host being reset), and
   resets an Apple host at a resume to "remove any PCIe devices the
   firmware might have setup" (`tb_resume_noirq`) [linux]. Whether a
   Mac's firmware builds a PCIe tunnel to a device attached at power-on,
   and in what state it leaves the routers, is [unread]. `usb4-emu`'s
   firmware connection manager (`preos-cm`, section 9.1) configures USB4
   routers only, so section 9's discovery has not been read on this row
   with a tunnel this driver did not build: 45.58's "found at start" was
   its own tunnel, adopted across a restart of the NHI's stack.
   - **The emulator change**: `preos-cm=on` accepted on the
     `tb*-apple-*` profiles (off by default, as today), the firmware
     running the sequence Linux's software connection manager runs on a
     Thunderbolt router [linux, inferred for a Mac's firmware]: the host
     router and each device router at depth 1 enumerated by one write of
     ROUTER_CS_1 to 4 with Valid, Notification Timeout 255 and no CMUV
     (`switch.c` `tb_switch_configure`), their Plug Events made active
     (`tb_plug_events_active`), the link controller's port-configured
     marks set at both ends from generation 2 (`tb_switch_configure_link`,
     `lc.c` `tb_lc_configure_port`), the lanes bonded by item 3's rule
     (`tb_switch_lane_bonding_enable`), and the
     PCIe tunnel built between the host's PCIe Down adapter of that port
     (section 19.4 item 4's map; Light Ridge the first free one) and the
     device's PCIe Up adapter on HopID 8 both ways, as the model's USB4
     firmware does; then its own hot-plug events consumed. The lane
     HopIDs (8 down, 9 up) and the credits are [emu] (the USB4
     firmware's). Its TMU is left as it comes up (section 19.4 item 6).
     Nothing in this repository changes with it.
   - **The legs** (7 x86; one 10 x64): Alpine Ridge (generation 3) and
     Cactus Ridge (generation 1), the enclosure at power-on, each with
     `XhciCmPreOsCleanup` 0 (expected: the port UP and preserved, the
     disk on Stor98 with no re-enumeration, `u4.host` adopted), 1
     (expected: the same, nothing cleaned, since the tunnel verifies) and
     2 (expected: the port cleaned - the link controller's Downstream
     Port Reset on Alpine Ridge, the paths torn down alone on Cactus
     Ridge, whose device router then stays enumerated and is adopted as
     it is at the fresh connect (section 9.2's "no tunnel state") - and
     the tunnel rebuilt); an unplug and a plug after each. A leg that
     does not read as expected is a finding, fixed here if it is this
     driver's.
5. **The router's generation in the status record and the tools' words**
   (45.58's finding: `XHCIINST /devices` called a Port Ridge and a Falcon
   Ridge device router "Thunderbolt 3"). The record's `CM_ST_E_GEN` keeps
   its layout; on an entry of kind `CM_ST_GEN_TBT3`, bits 15:8, which no
   writer has set until now, carry the router's Thunderbolt generation
   (1, 2 or 3, the router's `TbtGen` from its device id,
   `tb_switch_get_generation` [linux]) where the software connection
   manager has read the router - its host-port entries (kind 2) and the
   chained routers it enumerated (kind 3 with the router read) - and 0
   elsewhere (an ICM entry, a deeper link's entry with no router read,
   and every record of an earlier driver), read as Thunderbolt 3 as
   before. So the record's version does
   not change and an earlier tool reads a new record as it did. `XHCIINST
   /devices` and XHCITR's generation line then say "Thunderbolt 1" or
   "Thunderbolt 2" for a router of generation 1 or 2, and "Thunderbolt 3"
   for 3 or 0, as today. XHCITR's "running as Thunderbolt 2", which it
   says of every router behind a Falcon Ridge host, is not said of a
   generation 1 router there, which is "Thunderbolt 1" alone: a link runs
   at the lower generation of its two ends [inferred]. Host vectors in
   `test_cm_status` and `test_tr`; the owner reads the new words (task
   46.18's rule).

Not in this task, as before: the Mac itself (45-E.11); 98 SE to Vista
(section 19.2, Q2); DisplayPort (Q3); the ids 98 SE and ME give a Light
or Cactus Ridge NHI (section 19.3); whether a Mac's firmware rebuilds a
tunnel at a resume (section 19.4 item 7); and the release notes' "Not
claimed: Apple machines" (Q5), for which the run record carries a draft
for the owner.

## Appendix A. The binary readings behind `legal-provenance.md` section 4

Each item is a reading of a third-party binary, in the words of the
`docs/contributing/legal-provenance.md` section 4 row that indexes it by
method; that row keeps the fact, the method tag, the binary and a pointer
to the item here.
References inside an item ("as above", "the next row", "the same",
"section 2") are to that table and that file as the row stood there; read
them from the row that points here and its neighbours.

| Item | Fact | Method | Where recorded |
|---|---|---|---|
| A.1 | **The ASMedia ASM4242 USB4 host, PCI side and firmware state as `usb4-emu`'s asm4242 model and data record them (task 44.39)**: the topology, ids, bus ranges, bridge windows, BARs and slot capabilities of 00:02.2, 13:00.0, 14:00.0 to 14:03.0, 79:00.0, 7A:00.0 and the enclosure's functions; the host router's id, adapters, HopIDs, buffers, NVM version and ROUTER_CS_1 to 4; the firmware's tunnel (ADP_PCIE_CS_0 of adapters 6 to 9, the HopID 8 entries of adapters 3 and 7); the `_DSD` property names and the `_OSC` UUID in two SSDTs. From `usb4-emu`'s asm4242 data at `7ec3865`, `5d7345a` and `9f333f3` (before the 2026-10-29 squash; the record is `a0b6749` on its `main` after the merge, the archives kept outside that repository), seven archives, by SHA-256: `d37d752481a562f2e1e94013961b0655160d15969971a925f9e642125f2767bd`, `56bb173eafcfaea42e8cf9754eb691d7fef6ae1a113301ea94fae18f890beef1`, `d6bcf5220418a3c4bf8a02938c4a055e4952207004039592fd615b17cdd84cba`, `6476407d996db22244d37e255c7fa048d28ecd1a75bf3b8a68935b0e30d2aef6`, `52ae1ea2c402fe5ef651fef29ce90e58442f1cdbe722c7c0d9af7b9e5823725b`, `846759b3a71ac86612499e0abf3a27f5f979370d8fffb01be0acae84872e5d70` and `a802a36ac83ef79ed138528fe887284d84c1d2b7d6045e4f3e0e79f5aa8607b2`. Where that data came from is recorded in `usb4-emu`; nothing of it is in this repository | static (a text read of the archives' logs, `lspci` output, sysfs files and `thunderbolt` trace, a decode of their raw configuration-space files, and a byte search of two ACPI tables for property names and UUIDs, no disassembly; 2026-10-25; nothing of the archives executed). No file of them is tracked here; `usb4-emu`'s asm4242 research note (section 11 at `a0b6749` on its `main` after the merge; `3090d19` before the 2026-10-29 squash) is that repository's own reading of the same files | design record 22 section 18; roadmap-usb4 44.39 and 44-E.6; design record 23 section 5.13.8 (the controllers' ids and names, task 44.55); the ASM4242 family in `src\cm\cm_ports.c` and the host vector `test\test_tbt_topo.c` `test_asm4242_host`, the ASM4242's topology (task 44.40); the host vectors `test\test_u4cm.c` `test_asm4242_preos` (the firmware's four entries) and `test_asm7` (the fresh entry `00000008h 00000000h`) |

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
- For section 18 (read 2026-10-25): `usb4-emu`'s asm4242 model and data
  (`a0b6749` and `376e2aa` on `usb4-emu` `main` after the merge;
  `7ec3865`, `5d7345a`, `9f333f3` before the 2026-10-29 squash) and its
  asm4242 research note (section 11 at `a0b6749`; `3090d19` before the
  squash), where that data came from is recorded in `usb4-emu`;
  nothing of it is in this repository; `docs/captures/P14S-ORICO-2026-10-15.md`
  section 2.2;
  design records 21, 28 and 31; `src\cm\cm_ports.c`, `cm_reenum.c`,
  `tbt_topo.c`.
- For section 19 (read 2026-10-29): Linux `drivers/thunderbolt` at
  `c6859eed` (`pci.c` `nhi_ids[]`, `icm.c` `icm_probe`,
  `icm_fr_is_supported`, `icm_ar_is_supported`, `tb.c`
  `tb_apple_add_links`, `tb_find_pcie_down`, `tb_resume_noirq`,
  `switch.c` `tb_switch_get_generation`, `tb_switch_configure`,
  `tb_plug_events_active`, `tb_switch_reset_host`,
  `tb_switch_default_link_ports`, `tb_init_port`, `tb_switch_set_uuid`,
  `cap.c`, `lc.c`, `eeprom.c` `tb_drom_read_uid_only`, `tunnel.c`
  `tb_dp_xchg_caps`, `usb4.c` `link_is_usb4`, `usb4_switch_setup`) and
  `include/linux/pci_ids.h`; Apple Support, "Boot Camp Support Software
  5.0.5033" (support.apple.com/en-us/106404), "Download Boot Camp Support
  Software 5.1.5769" (106378), "Install Windows 10 on your Mac with Boot
  Camp Assistant" (102622), "System requirements to install Windows using
  Boot Camp for macOS" (HT204048), and the Windows 7-only statement for
  the Early 2011 MacBook Pro as press reports of February and August 2011
  quote it (Apple's HT1461, not read directly).
