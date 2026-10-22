# Design Record 21 - Thunderbolt 3 Firmware Connection Manager Client and Tunnel Re-enumeration

Status: draft 6, 2026-10-15, reviewed and revised; the pure core landed,
the driver integration pending; the
owner's decisions D1 to D13 taken (D4 on 2026-10-14, the rest on
2026-10-15) and written into section 11 and the sections each one names.
Written for Phase 39 of `roadmap-usb4.md` ("Phase 39 - The Thunderbolt 3
ICM Client, Tunnel Re-enumeration and Safe Eject"), the planned work `TB3`
until the owner opened it on 2026-10-15; its task ids are `39-0.n`,
`39-A.n`, `39-V.n`, `39-E.n` and `39.final`, and the USB4 software
connection manager that follows is Phase 40 (planned as `U4CM`). Since it
was written, the pure core has landed in `src\cm\` (`tbt_ring.c`,
`tbt_icm.c`, `tbt_hw.c`, `tbt_topo.c`, tasks `39-A.2`, `39-A.3` and
`39-A.5`'s pure half, with their host suites); the driver around it and
the virtual-machine legs are still to come. Revised on 2026-10-15 for the
`usb4-emu` relicensing (section 9), the decisions shared with design
record 22 (sections 1.4.2 and 6.3) and the pure core's behaviour
(sections 4, 5.3 to 5.5, 6.3 and 10). Section 12 lists what is open. Draft 6 adds, by
the owner's decisions of 2026-10-15, the per-generation profiles and the id
list of every Intel firmware-connection-manager host the Linux driver
addresses, read from Linux under the owner's source rule of the same day
(D9, sections 1.4, 3.3, 5 and 9), and safe eject (D10, task `39-A.8`,
sections 3.2 and 7.4). Section 7.4 revision 2 (2026-10-16): the safe
eject's child filter also goes above bus filters already on a new PDO,
which is where an ACPI machine leaves a PCI-to-PCI bridge. The USB4
software connection manager is design record 22's (Phase 40). Revised 2026-10-18 for Phase 42 (the owner opened the phase that
day): section 7.4 revision 3, the safe-removal mark across a stack
rebuild (task 42-0.4); section 7.2's 9x half, the eject asked of the
connection manager, the 9x link-down statement and the per-port waits
(task 42-0.7); section 3.2's child filter also created by `AddDevice`;
section 8's table gains design record 28's value. Section 3.5, added
2026-10-19 (task 42-A.21, the owner's request of that day): what the
connection manager needs of the USB drivers, for the installer's
connection manager alone; section 7.4's mark service also on Windows 8
and 8.1. Section 7.2 revised 2026-10-19 (task 42-A.25, from the matrix
reading on Windows ME that day): a tunnel named but not published at once,
as on 98 SE and ME right after a resume, is never paid later by an
exposure that names none. Revised 2026-10-19 (task 42-A.23, the owner's decision of that
day): the x64 package keeps the filter INF at the flavour directory's
root beside `xhcicm.inf` and its one `xhcicm.sys`, with no `nt\`; the x86
package keeps `nt\` (section 3.3, the packaging in section 3.3's last
paragraphs, section 6.2's filter install).
Section 5.3 revised 2026-10-21 (task 43.44, the owner's choice of that
day): the client reads each announced router's USB4 Version once per
occupancy, so the status record no longer calls every such router
Thunderbolt 3; first on the integrated rows and Maple Ridge, and from
2026-10-22 (the owner's answer of that day) on every row.

The sources this record rests on are in the `usb4-emu` repository
(`D:\OneDrive\git-projects\usb4-emu`, branch `main`, commit `eba0cf1`; `main`
moved to `a68bc74` by 2026-10-15, and the two commits' trees are identical,
read that day), which
this project reads and never modifies: `docs/XHCI98-HANDOFF.md` (the
handoff), `docs/ICM-PROTOCOL.md` (the Alpine Ridge host contract, cited below
as `ICM-PROTOCOL` with its section and requirement numbers),
`docs/PLATFORM-MATRIX.md`, `docs/REENUM-EXPERIMENT.md`, `docs/WIN98-TEST.md`,
`docs/ARCHITECTURE.md`, `docs/NVME-DRIVER-EVAL.md`, `docs/HARNESS.md` (the
fault-injection harness) and `tools/usb4cm/`. The handoff on `main` is the
single entry point and is re-read when it changes; its maintainer has said
that further controller models are coming (`docs/CONTROLLER-CATALOG.md`, in
progress: likely a Titan Ridge firmware-connection-manager host and an AMD
USB4 host), so nothing here hard-codes Alpine Ridge: the driver branches on
the generation's profiles (section 1.4) and discovers what it can.
Every hardware fact below keeps the tag it carries there: **[obs]** seen in a
P14s Gen 1 capture, **[inf]** inferred from the Linux client's behaviour and
not yet confirmed by a capture. An [inf] fact is a hypothesis this driver is
written to tolerate being wrong, not a fact it may rely on. Draft 6 adds a
fourth tag, **[linux]**: read in this project from the Linux Thunderbolt
driver's structures, constants and per-id choices (section 9), not seen in
any capture, and held to the same tolerance (section 1.4.6).

## 1. Purpose, scope and what it is not

### 1.1 What this is for

The owner's ThinkPad P14s Gen 1 carries an Intel JHL6240 Alpine Ridge LP
Thunderbolt 3 controller [obs] `ICM-PROTOCOL` section 1. Under Windows 98 SE
and Windows 2000 a Thunderbolt 3 dock plugged into it is invisible unless the
BIOS was told to tunnel it before the operating system boots ("Support in Pre
Boot Environment" on, and the device attached at power-on) [obs] section
12.1. In every other state - the device plugged in after boot, or pre-boot
support off - the controller's firmware connection manager (the ICM) waits
for an operating-system driver to say it is ready and to approve the device,
and only then builds the PCIe tunnel [obs] sections 6.2 and 6.4.

This record designs that driver: a WDM function driver for the Thunderbolt 3
host interface (the NHI, `PCI\VEN_8086&DEV_15BF`) that is a client of the
firmware connection manager over ring 0, approves devices, and makes each
target's PCI bus driver see the PCIe devices that appear and disappear behind
the tunnel. The devices themselves - the dock's xHCI controller, an NVMe
enclosure, a NIC - are then driven by the drivers that already exist for
them: `xhci98.sys` for the xHCI, `nvme98` for NVMe. What the handoff calls
"the P14s demo" is the whole of this record's first checkpoint.

### 1.2 Why first, and why not the USB4 software connection manager

The handoff (section 2) names two targets and an order: the Thunderbolt 3 ICM
client first, because it can be proven on the owner's real machine; then a
USB4 software connection manager against the emulator's `usb4-generic`
profile and a Framework 13 capture; then real USB4 devices once a capture of
the owner's ORICO USB4 enclosure exists. This record is the first of the
three. The USB4 software connection manager is a different, larger program
(the handoff's estimate, 4 to 7 thousand lines against 1 to 2 for this) that
drives routers, lanes, paths and tunnels itself; section 13 says what this
record's choices leave it, and `roadmap-usb4.md` carries it as its own
phase, Phase 40 (planned as `U4CM`), released with this one as `3.0.0.0`
(decision D7). Nothing here waits for the ORICO capture.

### 1.3 Scope

In:

- Binding the Alpine Ridge NHI on Windows 98 SE and Windows 2000 SP4 (the
  primary targets) and on the virtual-machine tiers (ME, XP, XP x64, Vista and
  7 in both architectures) under the standing rule for those tiers. The INF
  claims every NHI of section 3.3's list (decision D9): the Falcon Ridge,
  Alpine Ridge, Titan Ridge and Maple Ridge discrete controllers and the
  Ice Lake to Meteor Lake integrated ones, each on a profile of section
  1.4. Only `15BF` has been read [obs]; the rest are claimed untested, on
  profiles read from the Linux driver [linux].
- The ICM client, polling only: firmware status, operating mode, the NHI
  mailbox (`allow all devices`, `driver unloads`), ring 0 in raw mode,
  `driver ready`, `device connected`, `get route` (or Falcon Ridge's
  topology query), `approve device`, `device disconnected`, with the
  timeouts and retries of `ICM-PROTOCOL` section 3 and each profile's
  differences (section 1.4).
- Device approval under security level `user`, by policy, with no user
  interface (section 5.4 and decision D4).
- Re-enumeration of the tunnel bridge's secondary bus after a tunnel comes up
  or goes down: on Windows 98 SE and ME through CONFIGMG, on the NT targets
  through an upper filter on the tunnel bridge (section 6).
- The ordering obligations `nvme98` places on a connection manager (section
  7), and a proposed notification channel, delivered after the first
  checkpoint.
- Suspend, resume, disable and removal of the NHI, as far as `driver
  unloads` and a repeated `driver ready` carry them (section 5.6).
- Safe eject of a device behind the tunnel (task `39-A.8`, decision D10,
  section 7.4): on the NT targets the device is offered for removal and an
  eject stops its drivers, the tunnel staying up until the user unplugs; on
  Windows 98 SE and ME best effort, and NT-only with a release-notes line
  if the 9x half cannot be made to work.

Out, each with where it goes:

- The USB4 software connection manager (Phase 40, section 13).
- Hosts with no firmware connection manager (Lunar Lake and later, Barlow
  Ridge, AMD, ASMedia, any class `0x0C0340` host) and the Maple Ridge and
  Tiger Lake to Meteor Lake hosts when their firmware connection manager is
  not running: the USB4 software connection manager, Phase 40, design
  record 22. Apple machines and Apple's own controllers: not claimed.
- The platform's own power control of an integrated controller (ACPI power
  resources and methods): out of reach on Windows 98 in APM mode and unread
  on NT, as force power is for the discrete parts; only the NHI's own
  configuration-space power step of section 1.4 is done.
- Force power (powering a controller that the BIOS left off because nothing
  was attached at boot, `ICM-PROTOCOL` section 11.2): it is an ACPI WMI
  method, which Windows 98 in APM mode cannot reach at all and which nothing
  in this project has read on NT. A controller that is absent at boot stays
  absent; the release notes say so ("BIOS Assist on with nothing attached").
- The boot ACL, NVM versions and the DMA port, DisplayPort, host-to-host
  links, security levels `secure`, `dponly`, `usbonly` and `nopcie`, the
  firmware-start path of `ICM-PROTOCOL` section 2.7, and interrupts (MSI,
  MSI-X or INTx) on the NHI.
- Tearing a tunnel down on request: under the firmware connection manager
  the only teardown command drops every PCIe tunnel at once (section 7.4),
  so an eject stops drivers and never the tunnel.
- The dock's own devices: nothing here drives an xHCI, an NVMe or a NIC. The
  xHCI behind the tunnel is `xhci98.sys`'s by class, as any other.

### 1.4 How generic a Thunderbolt 3 client can be (owner's question, 2026-10-14)

The owner asked for a best effort at a generic Thunderbolt 3 and USB4
driver, and on 2026-10-15 for as many controllers as possible. Three
layers, with three different answers:

- **The host interface and the re-enumeration are generic.** The NHI ring
  registers follow the USB4 specification's common layout (section 4.1), the
  ring code is the one the USB4 software connection manager will use
  (section 13), and both re-enumeration routes (section 6) do not depend on
  which controller sits above the tunnel. Which PCI ports carry a domain's
  tunnels is not generic: it is a set per domain with a recorded source
  (section 6.3).
- **The ICM client is generic across devices and per-generation across
  the Intel hosts it addresses.** The firmware owns the device side, so
  every Thunderbolt dock, enclosure or cable works through the same
  client with no per-device code (the handoff, section 2, "Other
  Thunderbolt 3 hosts"); a USB4 device behind one of these hosts works as
  far as it is Thunderbolt 3 compatible and the host firmware handles it,
  with no USB4 link or USB3 tunnel through the host. The host side is
  Intel's firmware connection manager (ICM) families, and they differ by
  generation in their power steps, their ownership test, their message
  layouts and their lifecycle. What is shared is the shape: bind, announce,
  approve, disconnect, unload, resume. What is per generation is two
  tables, kept apart: a **hardware profile** (section 1.4.3), which every
  host of a family needs whichever connection manager runs, and a
  **message and lifecycle profile** (section 1.4.4) for the ICM rows.
- **Real USB4 hosts are the software connection manager.** Hosts that
  leave the connection manager to the operating system are Phase 40's work,
  designed in design record 22, in the same binary (section 13). So is a
  Thunderbolt 3 device behind such a host, which the software connection
  manager configures router by router; this driver never programs a fabric
  a firmware connection manager owns, and never runs both on one domain.
  An unknown firmware connection manager is unsupported: its existence
  does not make it Intel's ICM.

#### 1.4.1 The sources of the profiles

Alpine Ridge is captured: its row is cited to `ICM-PROTOCOL`, and each fact
keeps the tag it carries there - [obs] on `15BF` where captured, [inf]
where it is the Linux client's behaviour described there (the byte order,
the timeouts, several lifecycle steps). The other rows come from the Linux Thunderbolt driver,
read under the owner's rule of 2026-10-15 (section 9), at the pinned mirror
commit `c6859eed755df351a3978b33cb92365f9b3e8f06`: `tb_msgs.h` for the message
structures and flags, `icm.c` for which structure, predicate, timeout and
step each generation uses (`icm_probe`'s per-id selection), `nhi.h` and
`nhi_regs.h` for ids and registers, `pci.c` for the integrated controllers'
power operations and the id table, `nhi.c` for the connection-manager
choice and the host reset, and `quirks.c` for router quirks; FreeBSD's
mirror (`9d50245a0f1333e10cc1c4dda16878cd79a7c42b`) for a cross-check of the
ids it names (`nhi_reg.h`). Such facts carry the **[linux]** tag: read from
the Linux driver's structures, constants and per-id choices, not seen in
any capture. A [linux] fact is what Linux's client is written to, which is
better evidence than an analogy and weaker than a capture; this driver
tolerates one being wrong as it does an [inf] (section 1.4.6).

Word positions follow from the structures by one rule both sources share:
the Linux control channel exchanges every frame as big-endian 32-bit words
and reads its structures after swapping them (`ctl.c`, `tb_ctl_tx` and
`tb_ctl_rx_callback`), so a byte field at offset k inside a word occupies bits
`8*(k%4)+7 .. 8*(k%4)` of the word's numeric value, which is `ICM-PROTOCOL`
section 0's convention; the Alpine Ridge row's observed frames agree with
its structures (`icm_fr_pkg_approve_device`'s key and id at word 5 bits 7:0
and 15:8 are `T:34153`'s `0x00000610`).

#### 1.4.2 Selection: four tests, and the inert start

Which code runs on an NHI is decided by four separate tests, never by one,
and nothing is read from the hardware before `IRP_MN_START_DEVICE` has
delivered BAR0:

1. **The hardware candidate**, at `AddDevice`, from the vendor and device id
   the INF matched (section 3.3): a family of section 1.4.3 and, for an ICM
   family, a row of section 1.4.4. The class code (`0x0880` for an Intel
   Thunderbolt host interface, `0x0C0340` for a USB4 host interface)
   classifies and proves nothing. Apple machines are excluded as a
   platform policy, not an id family (Linux's `x86_apple_machine`, used by
   `icm_fr_is_supported` and `icm_probe`, is a test of the machine's vendor,
   not of the NHI): this driver takes the NHI's subsystem vendor `106B`
   (Apple) as the mark, and whether every Apple NHI carries it is a reading
   for the first report from one; a miss there gives at worst the AR or TR
   row on a machine whose firmware runs the ICM, which Linux itself accepts.
2. **Platform readiness**, after start: the family's hardware
   initialisation of section 1.4.3 done (the integrated controllers' force
   power and firmware-ready poll), BAR0 mapped and answering (a read of
   all ones from the host capabilities register is "not accessible").
3. **Connection-manager ownership.** A row's candidate predicate is not
   proof of ownership, so ICM admission is three conditions together, read
   only on an Intel candidate (Intel's firmware-status register means
   nothing on another vendor's NHI): the row's **candidate predicate** -
   FR and ICL (Ice Lake), a non-Apple machine (`icm_fr_is_supported`);
   Tiger Lake to Meteor Lake and Maple Ridge, NVM authentication done,
   polled for 10 ms after the platform initialisation
   (`icm_tgl_is_supported`); AR and TR, none beyond the next - **and**
   firmware status bit 0, ICM enabled, set (the check `icm_firmware_start`
   makes, without its restart: a clear bit never enters a restart path
   here) **and**, on the rows that have one, the **mode query** reporting
   connection-manager mode: AR, TR and Maple Ridge (all three use
   `icm_ar_get_mode`: NVM authentication done within 60 reads 50 ms apart,
   then the operating mode, section 5.1 steps 4 and 5). FR and the ICL row
   have no mode query in Linux (`get_mode` unset in `icm_probe`) and none is
   invented here. Safe mode, authentication mode and endpoint mode give the
   inert start (safe mode with its own log line, `icm_firmware_init`).
4. **A supported host interface**: the host interface version (host
   capabilities bits 23:16) one this driver accepts - 0 on AR [obs], below
   `0x40` on the other ICM rows. That range is this project's policy, not a
   Linux fact: Linux uses `0x40` (`REG_CAPS_VERSION_2`) only as the
   threshold of its host-router reset (`nhi_reset` [linux]), and this
   driver takes a host that wants that reset as not an ICM row's. The
   protocol version the TR and ICL `driver ready` returns is logged and
   gates nothing: Linux uses it only to enable the USB4 proxy operations
   (`ICM_USB4_SWITCH_OP`, from version 3), which this driver never sends,
   and every message this driver does send exists at every version Linux
   handles.

All four positive: the ICM client runs. **The software connection
manager is admitted only positively**, never as a fallback from a failed
ICM; its rule is design record 22's, and in outline (these targets have no
`_OSC` to grant native control) it is: (1) software-connection-manager
candidates with no supported ICM protocol - a class `0x0C0340` host not of
an Intel ICM-capable generation (AMD, `1022:1668` and `1022:1669` among
them, ASMedia, others) and Intel's software-only generations (Lunar Lake,
Panther Lake, Wildcat Lake, Barlow Ridge `5781` and `5784`) - admitted once
their hardware profile's readiness and the interface, BAR, DMA and ring
checks pass, Intel's firmware-status register never read on a non-Intel
host; (2) a Tiger Lake to Meteor Lake or Maple Ridge NHI whose readiness
contract completed and whose ICM enabled bit then reads 0 in a valid read,
a stated best-effort policy not gated on NVM authentication (which is only
the ICM candidate predicate), the case of NVM authentication done with ICM
enabled clear recorded as experimental, not as Linux's normal rule; (3) `XhciCmMode` 3, an experimental user
override on hosts the software connection manager supports, which cannot
bypass a failed platform initialisation, an unreadable required register,
a running firmware connection manager, an unsupported protocol, tunnel
preservation or the cleanup rules, and is refused on the FR, AR and TR
NHIs, which are not USB4 hosts. On every route existing tunnels of every
type are discovered and kept, and no link's power configuration is changed
while discovering them (section 1.4.5). Conflicting or unresolved
firmware-state evidence gives the inert start, which takes precedence.

**Everything else gives the inert start**: a timeout, a register that
cannot be read, NVM authentication never done, ICM enabled set with a
handshake that then fails before activation, an unknown firmware mode,
an interface version no row accepts, a platform step that did not
finish, an unknown vendor's firmware state. Two failure paths are kept
apart:

- **Before activation** - before this driver has started a ring or sent
  any connection-manager mailbox command or protocol message - the start
  is **inert**: the devnode starts (Code 0, so a user sees the driver
  installed), the reason is logged and counted, and nothing else happens:
  no reset of any kind, no ring, no protocol session, no approve, no
  teardown, no mailbox command at start or at stop, no re-enumeration.
  The inert start is also what `XhciCmMode` 0 gives (section 8).
- **After activation** - rings started, `0x23` or `driver ready` sent - a
  failure stops this driver's rings and drains its own queued work before
  the common buffer is freed, preserves every tunnel (no `0x06`, no
  reset), and cleans up only as far as the recorded initialisation stage
  reached: `driver unloads` (`0x07`) at stop only once a `driver ready`
  was sent, as the unbind of section 5.6 does. The instance then stays
  started and does nothing further, logged as failed rather than inert.

**Platform power has its own ownership record.** On the integrated
family the force-power bit (section 1.4.3) is read before it is written,
and the driver records whether it found it clear and set it. At stop
(and at shutdown) only an active instance restores what it changed: a bit
it set is cleared after the unbind, as Linux's `icl_nhi_shutdown` does on
the ICM rows (design record 22 section 4.2 refines this for the USB4
profile: cleared only while no device router is connected). An **inert
instance never clears force power**, not even a bit it set itself and not
even where its own firmware-ready poll timed out: a readiness timeout is
no evidence that nothing on the domain depends on the controller, and
Linux describes firmware readiness apart from the platform's power
management ([linux] `pci.c` `icl_nhi_force_power`). It leaves the bit set
and logs it. A bit the BIOS had already set is left set by every
instance. (Record 22 section 4.2's rule, which this record follows; an
orchestrator decision of 2026-10-15, for the owner to confirm.) Task
`39-A.4` brought the pure core to this rule: `tbt_hw.c`'s
`TbtHwPlatformShutdown` clears only a bit this instance set, and only
when its caller says it may - an active instance, and under the USB4
profile one with no device router connected. A force-power write the
hardware did not take is not taken as ownership: only a bit that reads
back set after this driver's write is recorded as set here (an
orchestrator decision of 2026-10-15, for the owner to confirm).

A Code 10 refusal is kept for the cases where starting would mislead:
BAR0 not delivered or not mappable.

#### 1.4.3 Hardware profiles

| Family | NHI ids (section 3.3) | Initialisation before any connection-manager test | Power and lifecycle | Tunnel ports (section 6.3) |
|---|---|---|---|---|
| Discrete: Falcon Ridge, Alpine Ridge, Titan Ridge, Maple Ridge | FR, AR, TR and Maple Ridge ids | none: the BIOS powers the controller; Bus Master set through the PDO (section 5.1 step 1) | D0 only; no runtime D3 requested (section 5.6); the controller powered down by the BIOS when nothing was attached at boot (Assist on) stays absent, its power-on being an ACPI WMI method out of reach (FreeBSD's `nhi_wmi.c` shows the mechanism, `nhi_wmi_evaluate_method`) | the controller's own switch's downstream ports with hot-plug capable slots, verified as section 6.3 says |
| Integrated: Ice Lake to Meteor Lake (ICM rows), and Lunar Lake, Panther Lake and Wildcat Lake (software connection manager only) | ICL row ids; `A833`, `A834`, `E333`, `E334`, `E433`, `E434`, `4D33` are Phase 40's | the NHI's own **force power**, a PCI configuration access, so reachable on Windows 98 through the PDO as the Bus Master write is: config dword `0xFC` (`VS_CAP_22`) with bit 1 (`VS_CAP_22_FORCE_POWER`) set and `0x22` in bits 31:24 (`VS_CAP_22_DMA_DELAY_MASK`), then config dword `0xC8` (`VS_CAP_9`) bit 31 (`VS_CAP_9_FW_READY`) polled up to 350 times 3 ms apart; then **LTR**: config dword `0xE4` (`VS_CAP_16`) bits 15:0 written to both halves of `0xE0` (`VS_CAP_15`) (`pci.c`, `icl_nhi_force_power`, `icl_nhi_set_ltr`, `icl_nhi_resume` [linux]) | D0 only; the same force power and LTR again at every resume to D0; at unbind and shutdown the force-power bit restored as section 1.4.2's power-ownership record says (Linux clears it unconditionally, `icl_nhi_shutdown`); the LC mailbox handshakes of system sleep (`VS_CAP_19` command with `VS_CAP_19_VALID`, `VS_CAP_18_DONE`, 500 ms, `icl_nhi_lc_mailbox_cmd`, `ICL_LC_PREPARE_FOR_RESET`, `ICL_LC_GO2SX`, `ICL_LC_GO2SX_NO_WAKE`) are not sent in the first cut - suspend with an integrated controller is unsupported and the release notes say so; the fused UUID Linux reads from `VS_CAP_10` and `VS_CAP_11` (`icm_icl_set_uuid`, a read, not a command) is read for the log only. The platform's ACPI power resources, `_OSC` negotiation and retimer `_DSM` (`acpi.c`, `tb_acpi_retimer_set_power`) are out of reach on Windows 98 in APM mode and unread on NT: the BIOS must leave the controller powered and visible, or the start is inert | the processor's PCI Express root ports that carry its tunnels, verified as section 6.3 says, never any root port |
| Any other vendor, class `0x0C0340` (AMD, ASMedia; AMD Pink Sardine `1022:1668`, `1022:1669` named by FreeBSD's `nhi_reg.h`) | none in this record | Phase 40, design record 22 | - | - |

#### 1.4.4 Message and lifecycle profiles (the ICM rows)

| | FR (Falcon Ridge, Thunderbolt 2) | AR (Alpine Ridge) | TR (Titan Ridge; Maple Ridge in ICM mode) | ICL (Ice Lake to Meteor Lake, ICM mode) |
|---|---|---|---|---|
| Evidence | [linux] | **[obs]** on `15BF`; the other ids [linux] by `icm_probe`'s grouping | [linux] | [linux] |
| ICM admission (section 1.4.2 test 3) | not Apple (`icm_fr_is_supported`), and ICM enabled | ICM enabled, and the mode query reporting CM mode [obs] (`icm_ar_is_supported` without its firmware-start branch, `icm_ar_get_mode`) | Titan Ridge as AR; Maple Ridge NVM authentication done within 10 ms (`icm_tgl_is_supported`), ICM enabled, and the AR mode query | Ice Lake as FR; Tiger Lake to Meteor Lake NVM authentication done within 10 ms (`icm_tgl_is_supported`) and ICM enabled; no mode query |
| NVM wait, operating mode, mailbox `0x23` (section 5.1 steps 4, 5, 7) | not done (`get_mode` unset in `icm_probe`) | done [obs] (`icm_ar_get_mode`, `icm_firmware_init`) | done (`icm_ar_get_mode` for Titan Ridge and Maple Ridge both) | not done (`get_mode` unset) |
| `driver ready` response | 2 words, `icm_fr_pkg_driver_ready_response`: ROM 7:0 and RAM 15:8 versions, security level word 1 bits 19:16 (`ICM_FR_SLEVEL_MASK` over bits 31:16) | 2 words [obs], `icm_ar_pkg_driver_ready_response` | 4 words, `icm_tr_pkg_driver_ready_response`: word 1 bits 31:16 the information half-word (level 2:0, protocol version 6:4, boot-ACL count 12:7), word 2 the NVM version, word 3 bits 15:0 the device id; timeout 250 ms with 10 retries, so 11 attempts (`icm_tr_driver_ready`; `icm_request` sends once more than its retry count) | the TR layout, but only the protocol version is taken from it (`icm_icl_driver_ready`: no security level or boot-ACL count is read on this row, so the log prints the word raw); timeout **20 s** with 3 retries, so 4 attempts |
| A new device's route | the topology query, code `0x01`, answered in 14 frames (`ICM_GET_TOPOLOGY_PACKETS`) of `icm_fr_pkg_get_topology_response`, assembled by packet index and walked from the event's link and depth by its port index fields (`ICM_PORT_INDEX_SHIFT`, `ICM_SWITCH_USED`) | `get route` [obs] | in the event | in the event |
| `device connected` | the AR layout (`icm_fr_event_device_connected`) | [obs] | `icm_tr_event_device_connected`: UUID words 1 to 4, route words 5 (high) and 6 (low), word 7 the connection id in bits 7:0 and the link information in 31:16 (approved bit 8, rejected 9, boot 10 of it), the name block from word 8; a frame whose packet index is not 0 is ignored (`__icm_tr_device_connected`) | the TR layout (`icm_icl_device_connected`) |
| Per-device security | the event's flags bits 4:3 for the log, as AR | `ICM-PROTOCOL` section 4.4 | the event's flags bits 4:3 for the log | the same; nothing in this driver depends on the domain's level on this row, `driver ready` not carrying it |
| `approve device` | the AR layout (`icm_fr_pkg_approve_device`) | [obs] | `icm_tr_pkg_approve_device`: UUID words 1 to 4, route words 5 and 6, connection id word 7 bits 7:0; no key | **none**: `icm_icl_ops` has no `approve_switch`, so Linux never approves on this row (its domain refuses an approval it has no callback for). This driver sends none either: an announcement with `approved` = 0 is logged and left as `XhciCmApprove` 0 leaves it, and only an announcement with `approved` = 1 brings a tunnel up |
| `device disconnected` | link and depth (`icm_fr_event_device_disconnected`) | [obs] | route words 1 and 2 (`icm_tr_event_device_disconnected`) | the TR layout |
| Device table key (section 5.3) | link and depth | link and depth | route string | route string |
| Other events | - | - | - | RTD3 veto, code `0x0a` (`icm_icl_event_rtd3_veto`): logged; Linux answers it by holding a runtime-power reference, and this driver satisfies it by never requesting runtime suspend (section 5.6) |
| System suspend (section 5.6) | mailbox `0x05` then `0x07` (`icm_fr_save_devices`, `icm_suspend`) | the same, [inf] in `ICM-PROTOCOL` section 2.3 and [linux] (`icm_probe` gives AR `icm_fr_save_devices`) | `0x07` alone (`icm_suspend` with no `save_devices` for TR in `icm_probe`) | Linux sends nothing (`icm_icl_ops` has no `.suspend`; its runtime callback is not a system-suspend path). This project's policy: system suspend with an integrated controller is unsupported (section 1.4.3), and the driver sends nothing at it |
| Firmware restart, CIO reset | never (`icm_ar_cio_reset` belongs to the firmware-start path) | never | never (`icm_tr_cio_reset` likewise; Maple Ridge has none in `icm_probe`) | never |

Every row shares the register offsets of `ICM-PROTOCOL` section 2.1
(`nhi_regs.h` names the same ones), the header and codes, the 5 s, three
retries (four attempts) and 10 s approve of section 5.5 (`ICM_TIMEOUT`,
`ICM_RETRIES`, `ICM_APPROVE_TIMEOUT`) except where the table names a
`driver ready` timeout, which then takes precedence over
`XhciCmCmdTimeoutMs` for that command, the host-router poll after `driver ready` (`__icm_driver_ready`),
and the error flag (`ICM_FLAGS_ERROR`). The proxy operations from protocol
version 3 (`ICM_USB4_SWITCH_OP`) and every host-to-host, key and challenge
message stay unused.

#### 1.4.5 Reset and takeover

This driver separates four operations Linux performs in different places,
and the ICM client performs **none** of them:

- **Host-router reset** (`nhi_reset`, `REG_RESET_HRR`, only for a host
  interface of version `0x40` and later): never; such a host is not an ICM
  row's (section 1.4.2 test 4).
- **Port reset** (`icm_reset_phy_port`, link disable and enable on both
  lane adapters): never; Linux does it only on its firmware-start path.
- **Firmware restart and CIO reset** (`icm_firmware_reset`,
  `icm_ar_cio_reset`, `icm_tr_cio_reset`, `ICM-PROTOCOL` section 2.7):
  never, and never as a fallback.
- **Tunnel cleanup** (mailbox `0x06`, `NHI_MAILBOX_DISCONNECT_PCIE_PATHS`):
  never (section 7.1).

So a driver loaded late, onto a fabric whose tunnels the BIOS or an
earlier driver built - a pre-boot-tunnelled boot disk among them -
leaves them up: the firmware re-announces them with `approved` set at
`driver ready` [obs] and the driver only records them. What the software
connection manager does with existing tunnels is design record 22's, to
these acceptance criteria: every existing tunnel of every type (PCIe,
USB3, DisplayPort; Linux's `tb_discover_tunnels`) discovered and its
resources reserved before any new path is allocated, no link's power
configuration (CLx and the like) changed while discovering, and no reset
of a fabric with existing tunnels merely because Linux resets during
probe. Existing tunnels prove a prior configuration, not a running
competing connection manager.

#### 1.4.6 What the client does where a row is wrong

Only the AR row has been seen working; every other row is a planned
implementation derived from Linux's layouts, captured nowhere. The contract, which `test_tbt_icm` is to check against frames
synthesised from those structures (proving only that the code follows
them):

- **Tolerate and log, never hang.** Every frame is length-checked against
  its row's structure before a field is read: a short frame, a
  `device connected` whose UUID reads all zeros, a route-carrying event
  whose route reads 0, or a name block that runs past the frame is logged
  with its raw words (all of them, at most the 256-byte receive buffer)
  and dropped; nothing is approved on it. Every command keeps its
  timeouts and retries, so a frame the firmware never answers costs at
  most its bound, and the thread goes back to polling. FR's 14-frame
  topology answer is abandoned whole if a frame is missing at the timeout.
- **An error flag leaves the device unapproved; a timeout leaves the
  outcome unknown.** An `approve` the firmware answers with bit 0 of the
  flags set leaves the entry unapproved with a log line carrying the
  request's and the event's raw words - which is the capture that row is
  waiting for, taken by the first user who reports it - and nothing is
  re-enumerated. An `approve` never answered after its retries is not
  proof that no tunnel exists: the firmware builds the tunnel while it
  processes the request, before it answers [obs] `ICM-PROTOCOL` section
  4.3, so a lost or late response can follow a tunnel that is up. The
  entry is marked "approve outcome unknown", no further approve is sent
  for it, and the tunnel-up poll of section 6.5 runs anyway, after which
  every member of the domain's verified set is re-enumerated whether or
  not a reading changed - a port another device already keeps up shows no
  transition, and a rescan of an unchanged bus is harmless. The entry
  stays "unknown" whatever that finds, since another member turning
  present proves nothing about this device: only an announcement for it
  (`approved` = 1, a tunnel up; 0, the policy again, with no second
  approve while the state is unknown), its disconnect, or the
  reconciliation after the next `driver ready` settles it.
- **A bind step that fails** after activation (no answer to `driver ready`
  after its tries, a ring that will not start, the `0x23` handshake) is
  section 1.4.2's failure after activation: rings stopped and drained,
  tunnels preserved, cleanup only as far as the recorded stage.
- **A disconnect that matches no entry** is logged with its raw words and
  treated as "some device on this domain is gone": the tunnel-down
  sequence of section 6.5 runs for every port of the domain's tunnel set
  (section 6.3), whose re-enumeration removes only functions that no
  longer answer (section 6.1 step 2, section 6.2), and entries whose
  devices still answer are kept. No `nvme98` statement is sent for it
  (section 7.2: the statement needs the entry).
- **Nothing else changes** between the rows: the approval policy, the
  reconciliation, re-enumeration and safe eject are the shared lifecycle.

The release notes carry a line for this (`39.final` drafts it, `40.final`
publishes it with the `3.0.0.0` cut): "Only the Alpine Ridge JHL6240
(`8086:15BF`) has been seen working. Every other Thunderbolt controller this
driver claims - Falcon Ridge, the other Alpine Ridge parts, Titan Ridge,
Maple Ridge, and the Ice Lake to Meteor Lake integrated controllers - is
untested: it is driven to the Linux driver's description of its messages,
and no capture of it exists. On such a machine the driver may start and do
nothing; the `debug` flavour's log says why. `XhciCmMode` 0 turns the
driver off."
`XhciCmMode` 2 in section 8 stays the experimental route for an Intel id
no row lists, and it needs the INF to match that id first.

So the honest claim for the first cut is "any Thunderbolt 3 device, on the
Alpine Ridge JHL6240 host that was captured", with every other listed
Intel firmware-connection-manager host claimed untested.

## 2. The platform, as captured

What a driver on the P14s faces, from `PLATFORM-MATRIX` section 2 and
`ICM-PROTOCOL` section 12 [obs], with the model's reproduction of each state
in `usb4-emu`:

| BIOS Assist | Pre-boot | Device at power-on | Switch and NHI present | The controller's own xHCI (`8086:15c1`) | The device's PCIe tree at boot | What this driver does |
|---|---|---|---|---|---|---|
| off | off | yes | yes | yes | absent | approve, then rescan |
| off | on | yes | yes | yes | present, enumerated by the BIOS | nothing for the boot device; approve and rescan for a later hot-plug |
| on | off | yes | yes | absent | absent | approve, then rescan |
| on | on | yes | yes | absent | present | nothing for the boot device; approve and rescan for a later hot-plug |
| on | either | no | **absent** | absent | absent | nothing: the controller is powered down and this record does not force power |
| off | either | no | yes | yes | absent | approve and rescan on hot-plug |

Facts the design rests on, all from `ICM-PROTOCOL`:

- The NHI is `8086:15bf`, class `0x088000`, BAR0 256 KiB of 32-bit
  non-prefetchable registers, INTx pin A; under the CSM the BIOS leaves it in
  D0 with memory decode on, bus mastering off, and MSI and MSI-X off [obs]
  sections 1 and 12.1. The driver must set Bus Master Enable itself.
- The firmware is running: firmware status `0x800001a1` (ICM enabled, NVM
  authentication done) and operating mode 3 (connection-manager mode) at
  every step, including with no driver bound [obs] section 2.1. Interface
  version 0, so no host-router reset handshake [obs] section 2.1.
- Ring 0 is the only ring used, in raw mode, 10 descriptors each way,
  256-byte receive buffers; the largest frame is 248 bytes plus a 4-byte
  trailer [obs] sections 2.1, 2.4 and 4.4.
- The firmware answers `driver ready` within milliseconds, then holds
  config requests for about 580 ms while it announces attached devices, then
  serves them [obs] section 6.2.
- A device approved earlier keeps its tunnel across `driver unloads`, a
  rebind and a resume, and is re-announced with `approved` = 1; a device
  plugged after boot is announced with `approved` = 0 and gets its tunnel only
  when approved; after an unplug and re-plug it must be approved again [obs]
  section 7.
- Unplug while active produces one `device disconnected` event and nothing
  else; the tunnel port's link is already down when it arrives [obs] section
  6.3. Unplug while the driver is unloaded produces no event [obs] section
  6.6, which is why every `driver ready` re-announces what is attached.
- On the P14s (BIOS N2XET46W 1.36, User Authorization; `usb4-emu`
  `docs/captures/P14S-ORICO-2026-10-15.md` [obs]): with Assist on and
  pre-boot off the NHI stays hidden even with a device attached, so no
  client binds; with Assist on and pre-boot on the firmware connects a
  device at boot - any device attached at power-on, even one not on the
  boot ACL - and the NHI is visible; a device plugged later is announced
  with `boot` = 1 but not approved, so the client still approves it;
  after the last unplug the firmware goes silent, the registers read all
  ones after 10 to 11 s and the controller is removed at about 15 s
  (section 5.5). Under Assist the driver-ready flags read `0x22`, and at
  power-on the outbound mailbox reads `0x800003f0` (`0x800003f5` only
  after the NHI's first D3); both decode to connection-manager mode, bits
  11:8 = 3. A USB4 device router on this ICM is announced with flags
  `0xa8`, or `0x88` single-lane with Assist and pre-boot, and its UUID
  words in the opposite order to a Thunderbolt 3 dock's - the client keeps
  both as given and echoes them in the approve. An unplug resets the
  endpoint: the switch and the NVMe come back reset after a re-plug, while
  a pre-boot tunnel reaches the OS without a reset and a driver rebind
  leaves the tunnel alone.
- The controller's PCIe switch: upstream port `8086:15c0` and three
  downstream ports of the same id - one to the NHI, one the tunnel (a
  hot-plug and surprise-removal capable slot), one to the integrated xHCI
  [obs] section 1. The emulator does not mirror the switch on q35 and stands
  it in with plain bridges on i440FX (`ARCHITECTURE` section 4.4,
  `WIN98-TEST` "P14s BIOS matrix"); section 6.3 says how the driver finds the
  tunnel bridge without assuming either shape.
- Windows 98 routes a hot-added function's interrupt through the BIOS `$PIR`
  table by swizzling up to a bus 0 slot; whether the P14s BIOS lists the
  Thunderbolt root port `00:1c.4` is not captured [obs/open] `WIN98-TEST`,
  "Hot-added NVMe gets no IRQ behind 00:1c". Section 10.4 makes the `$PIR`
  dump a bench reading. The emulator's Windows 98 profile places its
  Thunderbolt root bridge at bus 0 slot 6, inside the slots SeaBIOS's
  `$PIR` lists, so a hot-added function there gets its IRQ, and
  `ROOT_ADDR=0x1c` moves it back to reproduce the Code 29 failure
  (`usb4-emu` `c9e6d80`, `WIN98-TEST`); whether the real P14s's `$PIR`
  covers its root port stays open (`39-0.6`, `39-E.2`).

## 3. The driver: binary, roles, INFs and package

### 3.1 A second binary beside `xhci98.sys` (decision D1)

The connection manager is its own driver, `xhcicm.sys`, built from
`src\cm\` as `xhciuas.sys` is built from `src\uas\`: its own `sources`,
its own INFs (four: the NHI's and the NT-only filter's, one of each per
package, section 3.3), its own import
allowlists (`xhcicm-imports.allow`, `xhcicm-imports-amd64.allow`), its own
INF gate script (`check-cm-inf.ps1`), and the same three flavours derived
from `BUILD_ALT_DIR`. `build-driver.cmd` builds it after `xhciuas.sys` in
every x86 and amd64 run, `make-package.ps1` stages it in every flavour
directory with its INFs, and the amd64 half is test-signed by the packager
like the other two (design record 19).

Why not inside `xhci98.sys`, which already plays four PnP roles: the NHI is
a different PCI class (`0x0880`, a system peripheral) with a different
hardware model, a user may want one driver without the other, the USB4
software connection manager that follows is four times this size and would
ride every USB release, and `xhciuas.sys` set the precedent that a different
device class is a different binary with its own gates. What the two binaries
share is the pure core by path (the log ring `xhci_log.c`, the counters
header) and the build, gate and packaging machinery; `xhcicm.sys` imports
nothing from `xhci98.sys` and `xhci98.sys` does not know it exists. The one
coupling is in section 6: on Windows 98 SE the dock's xHCI appears behind a
bridge the connection manager re-enumerated, and `xhci98.sys` must then start
on it as it starts on any hot-added controller.

### 3.2 Three PnP roles in one binary

`AddDevice` tells the first two apart by the PDO's ids, as `reenum.sys` did
in the experiment and as `hcd_entry.c` tells its roles apart; the third is
never created by `AddDevice`:

1. **The NHI function driver** (every target). The FDO owns BAR0, the rings,
   the ICM client state machine and the connection-manager thread. It is
   installed by the INF's model lines for the NHI ids.
2. **The tunnel bridge's upper filter** (NT targets only). A filter device
   object attached above the bridge's function driver (`pci.sys`) on the
   stack of the switch's tunnel downstream port. It does no device work; it
   exists for two things. First, to hold a pointer to that port's PDO so
   that the NHI FDO can ask it to call
   `IoInvalidateDeviceRelations(Pdo, BusRelations)` (section 6.2). Second,
   on a bridge the NHI has marked **active** (the members of its domain's
   tunnel-port set, section 6.3, the mark set by the NHI thread under the
   list's lock and cleared at the NHI's stop), to watch the bridge's
   `IRP_MN_QUERY_DEVICE_RELATIONS` (BusRelations) on its way back up and
   create a child filter (role 3) for each device `pci.sys` reports there
   for the first time (section 7.4, task `39-A.8`). It is installed by a
   separate NT-only INF as a device-specific `UpperFilters` value on the
   port's id (section 3.3, decision D2), and never by any INF Windows 98's
   engine reads: on Windows 98 a WDM driver on a bridge devnode breaks the
   bridge (section 6.1).
3. **The child filter** (NT targets only, safe eject). A filter device
   object the bridge filter creates and attaches directly above the PDO of
   a device newly reported on an active tunnel bridge's secondary bus,
   before the PnP manager has built that device's stack, so the device's own
   function driver attaches above it. Its one act is section 7.4's answer
   to `IRP_MN_QUERY_CAPABILITIES` on the way up; every other IRP passes
   down untouched (PnP with `IoSkipCurrentIrpStackLocation`, power as the
   bridge filter passes it), and `IRP_MN_REMOVE_DEVICE` passes down, then
   detaches and deletes it. No INF names it, and it acts on no device but
   one reported on an active bridge.

Since section 7.4's revision 3 (2026-10-18) the child filter is also
created by `AddDevice`, for a unit whose devnode carries this driver's
name in a device-specific `LowerFilters` value written by the mark
service of that revision; `AddDevice` then decides the NHI and bridge
roles by id tables and treats every other PDO as such a child, which
acts only once its location (read at its first request) places it on
an active member's secondary bus, and never fails for want of a role. The paragraph below describes the registration list both
children report to.

The two roles find each other through a registration list in the driver's
own image. The lifetime protocol: a filter
instance publishes its entry (its PDO, its device object, and its bus,
device and function from `IoGetDeviceProperty` `DevicePropertyBusNumber`
and `DevicePropertyAddress`, which the experiment read on both targets)
only after its `IRP_MN_START_DEVICE` has completed below it successfully;
it withdraws the entry on `IRP_MN_STOP_DEVICE`, `IRP_MN_SURPRISE_REMOVAL`
and `IRP_MN_REMOVE_DEVICE` (the experiment's `reenum.c` stops its work on
all three), and the withdrawal waits for in-flight use to drain before the
filter detaches and deletes itself. The NHI thread takes an entry under
the list's spin lock, increments its in-flight count, releases the lock,
makes the call, and decrements; a withdrawal sets a gone flag under the
lock, then waits on a rundown event the last decrement signals. The lock
is never held across the call itself. An NHI without a matching filter, or
a filter without an NHI, is not a fault: the filter does nothing until
asked, and the NHI falls back to logging that it cannot re-enumerate
(section 6.4). Concurrent tunnel work and a bridge removal is a leg on the
Windows 2000 SMP guest (section 10.2). One binary, chosen at run time by
`IoIsWdmVersionAvailable(1, 0x10)` exactly as `docs/usb-xhci-info/win98-wdm.md`
prescribes it: FALSE is Windows 98 SE or ME and the CONFIGMG route; TRUE is
an NT target and the filter route.

### 3.3 The INFs

Four INFs, two per package, because Windows 2000 reads the
**undecorated** Models section and decorates only the DDInstall section
(which is why `xhci98.inf` shares `[XhciModels]` between 9x and NT 5.x), so
a Models section that is safe for Windows 98 cannot also carry a line for
the bridge on Windows 2000. The split:

- `xhcicm.inf`, the NHI's, follows `xhci98.inf`'s shape:
  `Signature="$CHICAGO$"`, one `[Manufacturer]` line
  `%Mfg%=CmModels,NTx86.6.0`, one undecorated `[CmModels]` with the NHI
  lines alone (one per id of the list below), undecorated DDInstall sections with `DevLoader=*NTKERN` and
  `NTMPDriver=xhcicm.sys` for Windows 98's 16-bit engine, `.NTx86` sections
  with `AddService` for 2000 and XP, `.NTx86.6.0` sections for Vista and 7
  that copy the driver alone. It never names a bridge. Since task `40-0.2`
  it also carries design record 22 section 3.7's USB4 lines: the Intel
  hosts with no firmware connection manager, AMD's `1022:1668` and
  `1022:1669`, and the class line `PCI\CC_0C0340` the id lines outrank.
- `xhcicmf.inf`, the filter's, NT only: `Signature="$Windows NT$"`, with an
  undecorated `[CmFilterModels]` carrying one line per tunnel-port id of
  the lists below, each in the `&CC_0604` form, for example
  `%TbBridgeDesc%=CmF.Dev,PCI\VEN_8086&DEV_15C0&CC_0604`, for Windows 2000,
  and `.NTx86.6.0` sections for Vista and 7; no undecorated install
  section. Its DDInstall adds the `UpperFilters` value and the service and
  installs no function driver. **The signature does not keep it from
  Windows 98 SE and ME** (`39-0.4`, `runs/run-39.md`): both engines read a
  `$Windows NT$` INF; a plain `PCI\VEN_8086&DEV_15C0` line was installed
  outright on the tunnel bridge of a 98 SE guest, and the `&CC_0604` form,
  though their automatic searches did not choose it, was listed to a user
  browsing for a driver. So the x86 package stages it in an `nt\`
  subdirectory of each flavour directory, never beside `xhcicm.inf` where
  a 98 user points the wizard, beside its own copy of `xhcicm.sys` (the
  same bytes as the flavour directory's), its one source disk having no
  path: Vista's and 7's driver store refuses a source outside the INF's
  directory (`SetupCopyOEMInf` error `0xA1`, read on 7 SP1 x86 on
  2026-10-15; an earlier layout with the source disk at `..` could not be
  staged there). The x64 package stages it as `xhcicmf.inf` at the root of
  each flavour directory, beside `xhcicm.inf` and the one `xhcicm.sys`,
  with no `nt\` (from `3.1.0.0`; `3.0.0.0` used `nt\` there too): 98 and
  ME never read the x64 package, the driver store's same-directory rule is
  met at the root, and no other INF there binds a PCI bridge.
- `xhcicm-amd64.inf`, the 64-bit package's, carries the `NTamd64,NTamd64.6.0`
  pair for the NHI and no undecorated section, held by the gate's
  `PATH-NO9X` and `PATH-MFGDEC` rules as the other amd64 INF is; the filter's
  64-bit half is `xhcicmf-amd64.inf` on the same pattern.

`LayoutFile` is not needed by any of them: the driver imports nothing the OS
has to supply. The gate for the four is `check-cm-inf.ps1`, with the rule
`CM-NO9XBRIDGE`: no bridge id in any `$CHICAGO$` INF of this driver, the
filter INF's signature held to `$Windows NT$`, no undecorated install
section in it, and in a staged x86 package the filter INF in `nt\` with
its own byte-identical `xhcicm.sys` and nothing else (in an x64 one at
the root beside `xhcicm.sys`, with no `nt\`);
`CM-PORTCC` holds every filter line to the `&CC_0604` form, `CM-CLASSLINE`
the NHI INF to its one class line, and `CM-NOROUTER` every INF to no
device router's or unclaimed controller's id.

Model lines, as the engines see them:

| Engine | What it finds |
|---|---|
| Windows 98, ME | `xhcicm.inf` `[CmModels]`: `%NhiDesc%=Cm.Dev,PCI\VEN_8086&DEV_15BF` and a line of the same shape for each other NHI id, the USB4 lines and the class line; `xhcicmf.inf` not on their path (it is in the x86 package's `nt\`; they would read it there too, `39-0.4`; they never read the x64 package, which has it at the root) |
| Windows 2000, XP | the same NHI lines through `Cm.Dev.NTx86`; `xhcicmf.inf` `[CmFilterModels]`: the bridge lines |
| Vista, 7 | the `.NTx86.6.0` halves of both |
| x64 | the two amd64 INFs |

On a discrete controller the filter lines match all three downstream ports and the upstream port,
since the captured switch gives every port the same id [obs] `ICM-PROTOCOL`
section 1. They also match switches inside devices: the CalDigit TS3 Plus
dock's own switch is `8086:15D3` [obs] `ICM-PROTOCOL` section 10.1, an
Alpine Ridge bridge id of the list below, so its ports behind the tunnel
get the filter too. The filter attaches to each and acts only on the ones
the NHI marks active (section 3.2), which are its own controller's tunnel
ports (section 6.3) and never a device's, so matching the others costs
nothing. On an integrated controller the lines match PCI Express root
ports of the processor; the same rule holds. **Decision D2 is answered by
`39-0.4` (`runs/run-39.md`): the device-specific route.** An INF whose
DDInstall includes `machine.inf` and needs `PCI_DRV`, `PCI_DRV.HW` and
`PCI_DRV.Services` (the section names of both the 2000 SP4 and the XP SP3
`machine.inf`) adds a device-specific `UpperFilters` value without
displacing `pci.sys` as the port's service, on 2000, XP and 7: no class
filter, no boot start, no `0x7B` across restarts. Windows 2000 takes the
INF unforced; XP and 7 rank the unsigned INF `0x8005` against
`machine.inf`'s signed `0x2004` and keep `machine.inf` unless the install
is forced, which the installer does (`UpdateDriverForPlugAndPlayDevices`
with `INSTALLFLAG_FORCE`, the install tool of task `40.4`, design record 23) and a user by hand does
through Update Driver, "Don't search", Have Disk. A port that first
appears after the install (a controller absent at boot) gets `machine.inf`
again on XP and later, so the install route is re-run, or the filter
re-applied, once the tree exists. Read on the 32-bit guests only; the
append flag `0x00010008`, the `.NTx86.6.0` models section and the 64-bit
guests are not read yet. The packaging stage carries both INFs per
flavour, the filter's in `nt\` in the x86 package and at the root in the
x64 one.

**The id list (decision D9, owner 2026-10-15, widened the same day to
every Intel firmware-connection-manager host Linux addresses).** Two sources,
cross-checked: the public PCI id listing at <https://pci-ids.ucw.cz/>
(vendor `8086`), read through its downloadable database
<https://pci-ids.ucw.cz/v2.2/pci.ids>, version `2026.10.14`, fetched
2026-10-15, SHA-256
`b0577d41ed389682f2a159cd97b4fd31b13810700e2823254e9689a3898cc837`, ids and
names only, the file not tracked, `legal-provenance.md` section 4 carrying
the row; and the Linux mirror at the pinned commit
(`include/linux/pci_ids.h` and `drivers/thunderbolt/nhi.h`, whose
`PCI_DEVICE_ID_INTEL_*` names are cited below, and `icm.c`'s `icm_probe` for
which profile each NHI takes). The two agree on every id both carry. FreeBSD's
mirror adds nothing: its identifier tables (`tb_pcib.c`,
`tb_pcib_identifiers`) are empty at the pinned commit. The INFs match
exactly this list, the NHI ids with `PCI\VEN_8086&DEV_xxxx` in `xhcicm.inf`
and the port ids with `PCI\VEN_8086&DEV_xxxx&CC_0604` in `xhcicmf.inf`.

NHIs (`xhcicm.inf`), with their profile of section 1.4:

| Id | pci.ids name | Linux name | Profile | Read here |
|---|---|---|---|---|
| `156A` | DSL5320 Thunderbolt 2 NHI [Falcon Ridge 2C 2013] | `FALCON_RIDGE_2C_NHI` | FR | untested |
| `156C` | DSL5520 Thunderbolt 2 NHI [Falcon Ridge 4C 2013] | `FALCON_RIDGE_4C_NHI` | FR | untested |
| `1575` | DSL6340 Thunderbolt 3 NHI [Alpine Ridge 2C 2015] | `ALPINE_RIDGE_2C_NHI` | AR | untested |
| `1577` | DSL6540 Thunderbolt 3 NHI [Alpine Ridge 4C 2015] | `ALPINE_RIDGE_4C_NHI` | AR | untested |
| `15BF` | JHL6240 Thunderbolt 3 NHI (Low Power) [Alpine Ridge LP 2016] | `ALPINE_RIDGE_LP_NHI` | AR | **[obs]**, the P14s Gen 1 |
| `15D2` | JHL6540 Thunderbolt 3 NHI (C step) [Alpine Ridge 4C 2016] | `ALPINE_RIDGE_C_4C_NHI` | AR | untested |
| `15D9` | JHL6340 Thunderbolt 3 NHI (C step) [Alpine Ridge 2C 2016] | `ALPINE_RIDGE_C_2C_NHI` | AR | untested |
| `15E8` | JHL7540 Thunderbolt 3 NHI [Titan Ridge 2C 2018] | `TITAN_RIDGE_2C_NHI` | TR | untested |
| `15EB` | JHL7540 Thunderbolt 3 NHI [Titan Ridge 4C 2018] | `TITAN_RIDGE_4C_NHI` | TR | untested |
| `1134` | Thunderbolt 4 NHI [Maple Ridge 2C 2020] | `MAPLE_RIDGE_2C_NHI` | TR, or Phase 40 | untested |
| `1137` | Thunderbolt 4 NHI [Maple Ridge 4C 2020] | `MAPLE_RIDGE_4C_NHI` | TR, or Phase 40 | untested |
| `8A17` | Ice Lake Thunderbolt 3 NHI #0 | `ICL_NHI0` | ICL | untested |
| `8A0D` | Ice Lake Thunderbolt 3 NHI #1 | `ICL_NHI1` | ICL | untested |
| `9A1B` | Tiger Lake-LP Thunderbolt 4 NHI #0 | `TGL_NHI0` | ICL, or Phase 40 | untested |
| `9A1D` | Tiger Lake-LP Thunderbolt 4 NHI #1 | `TGL_NHI1` | ICL, or Phase 40 | untested |
| `9A1F` | Tiger Lake-H Thunderbolt 4 NHI #0 | `TGL_H_NHI0` | ICL, or Phase 40 | untested |
| `9A21` | Tiger Lake-H Thunderbolt 4 NHI #1 | `TGL_H_NHI1` | ICL, or Phase 40 | untested |
| `463E` | Alder Lake-P Thunderbolt 4 NHI #0 | `ADL_NHI0` | ICL, or Phase 40 | untested |
| `466D` | Alder Lake-P Thunderbolt 4 NHI #1 | `ADL_NHI1` | ICL, or Phase 40 | untested |
| `A73E` | Raptor Lake-P Thunderbolt 4 NHI #0 | `RPL_NHI0` | ICL, or Phase 40 | untested |
| `A76D` | Raptor Lake-P Thunderbolt 4 NHI #1 | `RPL_NHI1` | ICL, or Phase 40 | untested |
| `7EB2` | not listed | `MTL_M_NHI0` | ICL, or Phase 40 | untested |
| `7EC2` | Core Ultra 200 Series Processors Thunderbolt DMA0 | `MTL_P_NHI0` | ICL, or Phase 40 | untested |
| `7EC3` | Core Ultra 200 Series Processors Thunderbolt DMA1 | `MTL_P_NHI1` | ICL, or Phase 40 | untested |

Tunnel ports (`xhcicmf.inf`): the discrete controllers' switch ports, and
the integrated controllers' PCI Express root ports, which carry their
tunnels since they have no switch (section 6.3):

| Id | pci.ids name | Linux name | With | Read here |
|---|---|---|---|---|
| `156B` | DSL5320 Thunderbolt 2 Bridge [Falcon Ridge 2C 2013] | `FALCON_RIDGE_2C_BRIDGE` | FR | untested |
| `156D` | DSL5520 Thunderbolt 2 Bridge [Falcon Ridge 4C 2013] | `FALCON_RIDGE_4C_BRIDGE` | FR | untested |
| `1576` | DSL6340 Thunderbolt 3 Bridge [Alpine Ridge 2C 2015] | `ALPINE_RIDGE_2C_BRIDGE` | AR | untested |
| `1578` | DSL6540 Thunderbolt 3 Bridge [Alpine Ridge 4C 2015] | `ALPINE_RIDGE_4C_BRIDGE` | AR | untested |
| `15C0` | JHL6240 Thunderbolt 3 Bridge (Low Power) [Alpine Ridge LP 2016] | `ALPINE_RIDGE_LP_BRIDGE` | AR | **[obs]**, the P14s Gen 1 |
| `15D3` | JHL6540 Thunderbolt 3 Bridge (C step) [Alpine Ridge 4C 2016] | `ALPINE_RIDGE_C_4C_BRIDGE` | AR | untested as a host's; [obs] as the TS3 Plus dock's own switch |
| `15DA` | JHL6340 Thunderbolt 3 Bridge (C step) [Alpine Ridge 2C 2016] | `ALPINE_RIDGE_C_2C_BRIDGE` | AR | untested |
| `15E7` | JHL7540 Thunderbolt 3 Bridge [Titan Ridge 2C 2018] | `TITAN_RIDGE_2C_BRIDGE` | TR | untested |
| `15EA` | JHL7540 Thunderbolt 3 Bridge [Titan Ridge 4C 2018] | `TITAN_RIDGE_4C_BRIDGE` | TR | untested |
| `1133` | Thunderbolt 4 Bridge [Maple Ridge 2C 2020] | not named | Maple Ridge | untested |
| `1136` | Thunderbolt 4 Bridge [Maple Ridge 4C 2020] | not named | Maple Ridge | untested |
| `8A1D`, `8A1F`, `8A21`, `8A23` | Ice Lake Thunderbolt 3 PCI Express Root Port #0 to #3 | not named | Ice Lake | untested |
| `9A23`, `9A25`, `9A27`, `9A29` | Tiger Lake-LP Thunderbolt 4 PCI Express Root Port #0 to #3 | not named | Tiger Lake-LP | untested |
| `9A2B`, `9A2D`, `9A2F`, `9A31` | Tiger Lake-H Thunderbolt 4 PCI Express Root Port #0 to #3 | not named | Tiger Lake-H | untested |
| `466E`, `463F`, `462F`, `461F` | Alder Lake-P Thunderbolt 4 PCI Express Root Port #0 to #3 | not named | Alder Lake-P | untested |
| `A76E`, `A71F`, `A72F` | Raptor Lake-P Thunderbolt 4 PCI Express Root Port #0 to #2 | not named | Raptor Lake-P | untested |

Linux names no integrated tunnel port: it finds them through the
platform's ACPI (`acpi.c`, the `usb4-host-interface` links), which a
Windows 98 machine in APM mode does not offer and which this driver does
not read. The root-port ids above are therefore the listing's alone; the
listing names no Meteor Lake tunnel root port and a fourth Raptor Lake one,
so on those machines the tunnel set comes from `XhciCmTunnelBridge`, and
the bench or a virtual machine reads the ids for a later list.

Left out on purpose: `15EF`, "JHL7440 Thunderbolt 3 Bridge [Titan Ridge DD
2018]" (`TITAN_RIDGE_DD_BRIDGE`), a device-side part with no NHI, so never
a host's tunnel port, and a device router's id, so a router quirk's
subject (Linux's `quirks.c` keys its early-firmware CLx quirk on it) and
never an INF line, as the Barlow Ridge hub bridges `5786` and `57A4` are;
the USB-only Alpine Ridge NHIs `15DC`, `15DD`, `15DE`
(`ALPINE_RIDGE_*USBONLY_NHI`), which are NHIs in Linux's id table but have
no `icm_probe` row, and which this PCIe-tunnelling release does not claim; Win Ridge `157D` and the
Redwood Ridge parts, which Linux's id table (`pci.c`, `nhi_ids`) does not
bind; the Thunderbolt 1 Light Ridge `1513` and Cactus Ridge `1547` parts,
which it binds only by class and an Apple subsystem id, as software
connection manager targets, not ICM rows; Apple machines, excluded as a
platform policy (section 1.4.2 test 1), not by id; the USB controllers of
every part,
which are `xhci98.sys`'s by class. The USB4 hosts with no firmware
connection manager (Lunar Lake `A833`, `A834`, Panther Lake `E333`,
`E334`, `E433`, `E434`, Wildcat Lake `4D33`, Barlow Ridge `5781`, `5784`,
and any class `0x0C0340` host, AMD's Pink Sardine `1022:1668` and
`1022:1669`, named in FreeBSD's `nhi_reg.h`, among them) are Phase 40's
(design record 22); task `40-0.2` added their lines to the INFs (below).
A
name in either source is not evidence that a part behaves like the one
captured; that is what "untested" means in the last column. A part with
two Thunderbolt ports (the DSL6540, for one, where the DSL6340 has one)
has two tunnel ports, which section 6.3's rule takes as a set; the set is
discovered on the machine, never assumed from the part's name. The warnings are the release-notes line of
section 1.4.6 and the README's hardware list.

**The USB4 lines (task `40-0.2`, design record 22 section 3.7).** In
`xhcicm.inf` and `xhcicm-amd64.inf`, under `%U4Desc%` ("xHCI98 USB4 Host
Router", decision U12 as revised; the Maple Ridge and Tiger Lake to
Meteor Lake lines above take it too, "Class and names" below):
`PCI\VEN_8086&DEV_xxxx` for `A833`, `A834`,
`E333`, `E334`, `E433`, `E434`, `4D33`, `5781`, `5784`; `PCI\VEN_1022&DEV_1668`
and `DEV_1669`; and `PCI\CC_0C0340`, which the id lines outrank. Windows 98
SE matches the class line: on the emulator's generic host `1B36:00F4` (class
`0C0340`, no id line of its own) its wizard offered `xhcicm.inf` from the
transfer disk, and the driver started, "working properly", in its INERT
start (task `40-0.2`, 2026-10-15, development host A). In the
filter INFs, the USB4-only hosts' tunnel ports where the same `pci.ids`
(`2026.10.14`) labels a port as such, and the emulator's:

| Id | pci.ids name | With | Read here |
|---|---|---|---|
| `8086:5780` | JHL9580 Thunderbolt 5 80/120G Bridge [Barlow Ridge Host 80G 2023] | Barlow Ridge `5781` | untested |
| `8086:5783` | JHL9540 Thunderbolt 4 Bridge [Barlow Ridge Host 40G 2023] | Barlow Ridge `5784` | untested |
| `1022:14CD`, `1022:14EF` | Family 19h USB4/Thunderbolt PCIe tunnel | AMD's USB4 hosts | untested |
| `1022:150A` | Strix/Strix Halo PCIe USB4 Bridge | AMD's USB4 hosts | untested |
| `1B21:2423` | ASM4242 PCIe Switch Downstream Port | ASMedia ASM4242 (class `0C0340` host) | untested |
| `1B36:0001` | the emulator's i440FX `usb4-tunnel-bridge` (QEMU's PCI-to-PCI bridge id) | `usb4-emu` | [obs] `runs/run-39.md` 39-0.7 |
| `1B36:000C` | the emulator's q35 `usb4-tunnel-port` (QEMU's PCIe root port id) | `usb4-emu` | not read here |

The listing labels no tunnel port of Lunar Lake, Panther Lake or Wildcat
Lake (Panther Lake's "USB Type-C Subsystem PCIe Root Port #24", `E337`
and `E437`, is not labelled a Thunderbolt or USB4 port, and Meteor Lake's
`7EC4` to `7EC7` of the same name are left out on the same reading); those
machines take their set from `XhciCmTunnelBridge`. `1B36:0001` is QEMU's
generic bridge, so on any QEMU machine the filter attaches to every such
bridge and acts on none the NHI does not assign. On the Windows 2000 guest with the
Alpine Ridge tree, Update Driver on the tunnel port `02:01.0` pointed at the
`nt\` directory found `xhcicmf.inf` as a closer match than `machine.inf`,
installed it unforced with no prompt and no restart, and left the port
"working properly" with `pci.sys` and `xhcicm.sys` as its driver files
(task `40-0.2`, the same day; `xhcicm.sys` was already in place from the NHI's
install, so that reading does not show the `..` source path being used). The excluded ids are the
gate's `CM-NOROUTER` list: the device routers and device-side bridges
(`15EF`, Goshen Ridge `0B26`, `5786`, `57A4`, AMD's `151C` and `151D`
"USB4 Router", ASMedia's ASM2464PD `2463`), the USB-only NHIs and the
unclaimed generations above.

Class and names: `Class=System` with its GUID on the NT paths and `Class=System`
undecorated for 98 (which also knows that class); device descriptions by decision D8, taken as recommended and then
revised by the owner on 2026-10-15 to name the hardware rather than the
driver's role. Each NHI line names its controller's generation, never the
connection manager that runs it, so a controller keeps its name whichever
of the ICM client and the USB4 software connection manager takes it:
`xHCI98 Thunderbolt 2 Controller` (`Tb2Desc`) for Falcon Ridge `156A` and
`156C`; `xHCI98 Thunderbolt 3 Controller` (`NhiDesc`) for Alpine Ridge,
Titan Ridge and Ice Lake `8A17` and `8A0D`; `xHCI98 USB4 Host Router`
(`U4Desc`) for Maple Ridge, Tiger Lake to Meteor Lake and every line of
design record 22 section 3.7, the class line included. A message profile
is not a generation: Ice Lake is Thunderbolt 3 hardware though it shares
the ICL messages with its USB4 successors, and Maple Ridge is USB4
hardware though it speaks the TR messages. "Host Router" is USB4's name
for the host's own router, which the NHI belongs to. The filter INF
becomes the port's device INF - its model line supplies the description,
and `Include`/`Needs` reuse `machine.inf`'s install sections but not its
name - so the filter's descriptions name the bridge, not the filter:
`xHCI98 Thunderbolt PCI Bridge` (`TbBridgeDesc`) for the Thunderbolt 2
and 3 generation's ports (Falcon, Alpine and Titan Ridge, Ice Lake `8A1D`
to `8A23`), `xHCI98 USB4 PCI Bridge` (`U4BridgeDesc`) for the USB4
generation's (Maple Ridge, Tiger Lake to Raptor Lake, Barlow Ridge, AMD,
ASMedia) and `xHCI98 PCI Bridge` (`BridgeDesc`) for the emulator's QEMU
bridges, every line keeping its `&CC_0604` form and the one set of
install sections. One id names every port of a switch, upstream and
downstream alike, and `CC_0604` does not tell them apart, so no name
claims a port's role. The service is `xHCI98 Thunderbolt and USB4
Connection Manager (xhcicm)` in all four INFs; no name carries "(TM)".
The gate's `CM-DESC` holds each line to its generation's description and
each string to its text. `StartType=3` (demand) for the NHI service,
as for `xhci98`; the filter is the same service, demand-started with the
devnode it filters, since it is device-specific and not a class filter.

Registry values, written by every install path at the driver's default under
the don't-overwrite flag, as the 2026-10-11 rule requires for every value
the driver reads: section 8.

### 3.4 Text-mode Setup

None. The NHI is never a boot-critical device for Setup, and nothing behind a
tunnel is one in this project's targets. `txtsetup.oem` is not touched, and
the `OEM-*` gate rules stay as they are.

### 3.5 Without `xhci98.sys` (task 42-A.21, 2026-10-19)

The owner's request of 2026-10-19 is an install of the connection manager
alone, for a Windows whose USB 3 driver is someone else's - Microsoft's
`usbxhci.sys` on Windows 8 and 8.1, a controller vendor's on 2000 to 7
(record 23 section 4.5). What this driver needs of `xhci98.sys`, read
from `src\cm\` and the tools on 2026-10-19:

- **No link at all on NT.** `xhcicm.sys` imports nothing from
  `xhci98.sys`, opens none of its devices, sends it no request (no
  internal USB request, no `USBUSER` door call, no IOCTL of either
  driver's), and `xhci98.sys` does not know it exists (section 3.1). The
  only device requests the driver answers are its own, on
  `\\.\XHCICM<n>` (`cm_query.h`): `IOCTL_XHCICM_QUERY` and
  `IOCTL_XHCICM_MARKS`, read by `XHCITR`, `XHCIINST /devices` and
  `/eject` and the mark service, and the log and PCI configuration
  snapshot requests (`CM_QL_IOCTL`, `CM_QP_IOCTL`) `XHCITR` reads; and
  the Stor98 channel is a callback object and a table of the two drivers'
  own (section 7.2). Its settings come from its own driver key and the
  machine-wide `Services\USB` key, which only the installer and the
  user write (`cm_reg.c`), whichever USB driver runs.
- **The PCIe tunnel** (both profiles) is the connection manager's work
  on the host interface; what appears behind the tunnel port is
  enumerated by Windows' PCI bus driver, under the tunnel-port filter of
  section 3.2 on NT, a filter of `pci.sys`'s bridge stack that never
  touches a USB device. A device there is bound by whatever driver
  Windows chooses: an NVMe drive by Stor98 or another NVMe driver (8.1 has
  its own, `stornvme.sys`; 8 has none), and the xHCI controller of a
  dock by whichever xHCI driver claims its class id - Windows' own on 8
  and 8.1, on 2000 to 7 a vendor's where one matches that controller and
  otherwise nothing.
- **The USB3 tunnel** (the USB4 profile only, `XhciCmUsb3Tunnel`, design
  record 22 section 6.9): its devices appear as an ordinary SuperSpeed
  connect on a root port of the host's own xHCI controller ([model]
  `XHCI98-HANDOFF` section 2), so whichever driver runs that controller
  sees them. The bandwidth handshake the path set-up waits on is the
  host router's and the xHCI hardware's ([linux] `usb4.c`), not a
  driver's; with no driver on the host's xHCI controller at all the
  tunnel may come up and nothing shows its devices.
- **Safe eject and the marks** (section 7.4): the child filter, the mark
  service and the tools' eject act on the PCI unit behind the tunnel
  through Windows' own eject and the connection manager's query device;
  none of it goes through a USB driver.
- **Windows 98 SE and ME are the exception**, and why the installer keeps
  the USB drivers required there: those systems have no xHCI driver of
  their own, so without `xhci98.sys` every xHCI controller is dark, the
  dock's included; and the CONFIGMG re-enumeration of section 6.1 counts
  on `xhci98.sys` starting on a controller the tunnel adds (section 3.1).

So on Windows 8 and 8.1 the connection manager alone gives the
Thunderbolt 3 ICM client and the USB4 software connection manager on the
hosts of section 3.3's lists, the PCIe tunnel and its re-enumeration, an
NVMe drive behind it (Stor98 is not offered there; record 23 section
4.5), a dock's xHCI controller and its devices under Windows' own USB
driver, the USB3 tunnel's devices on the host's controller under the same
driver, and safe eject with the marks. Of it, the install and the driver's start on
the NHI are read on 8 and 8.1 (below); the tunnels, eject and the marks
are task 43.24's. The import gate holds the x86 driver to
Windows 98's evidence and Windows 2000's export tables, and the amd64
driver to NT 5.2's (`build-driver.cmd`'s gate for `xhcicm.sys`,
`check-imports.ps1`); Vista and 7 are read in guests, not by the gate;
and no export table of 8 is in the tree, so whether 8's kernel and HAL
still export each routine of
`xhcicm-imports.allow` (the HAL's `HalGetBusData`, `KfAcquireSpinLock`,
`KfReleaseSpinLock`, `KeGetCurrentIrql` and `KeStallExecutionProcessor`
among them on x86) is unread. The driver's own system tests are WDM
versions (`IoIsWdmVersionAvailable(1, 0x10)` for NT, `(6, 0)` for Vista
and later), which 8 and 8.1 answer as 7 does, so it takes 7's paths there.
On 64-bit 8 and 8.1 it loads only as on Vista x64 and 7 x64: test-signing
mode (Secure Boot off) or a start with signature enforcement disabled
(design record 19; record 23 section 5.10).

Read since (task 43.22, 2026-10-22, in QEMU guests of 8 x86, 8 x64 and
8.1 x64 with the emulator's Thunderbolt 3 controller, the `3.1.0.0`
package; `runs/run-43.md`, "43.22"): the install takes the
`.NTx86.6.0` and `.NTamd64.6.0` models sections and the `Cm.Dev6` and
`CmF.Dev6` install sections, as on Vista and 7; the NHI starts on
`xhcicm` and the tunnel port carries the filter; so the driver loads on
8's and 8.1's kernels (no import refused). Two things differ from 7, both
record 23 section 5.10 revision 30's: 64-bit 8 and 8.1 refuse a package
with no catalog in test-signing mode, and on every 8 and 8.1 the setup
engine makes `xhcicm` a boot-start driver when its filter goes onto the
bridge, so on x64 a start without test-signing mode does not finish. The installer's gates
for this key on the system's kind, so 64-bit 10 below build 22000 takes
them too (record 23 section 5.10, revision 30); unread on 10.

**Windows 10** (roadmap-usb4 task 43.34, the owner's decision of
2026-10-20; record 23 sections 4.3 and 4.5, revision 28): NT 10.0 below
build 22000, the client releases 1507 to 22H2, the LTSC releases and
Windows Server 2016 to 2022, gets what 8.1 gets above, by the same
install: the connection manager alone, or beside `xhci98.sys` where the
user ticks it (task 43.35), the PCIe tunnel and its re-enumeration, an
NVMe drive behind it under Stor98 or 10's own `stornvme`, a dock's xHCI
controller and the USB3 tunnel's devices under whichever USB driver runs
them, and safe eject with the marks. It is the target this driver gives
most to: 10 carries no Thunderbolt or USB4 driver of its own (USB4
arrived with Windows 11, build 22000, which has its own connection
manager and stays refused). What is 10's own, and unread until task
43.33 (c): Intel's Thunderbolt driver, which computer makers ship for
10 on the NHI - its ids and service, the approval it does with the
firmware connection manager, and whether `xhcicm.inf`'s match outranks
it (until the owner decides, an NHI it runs is another vendor's device
and the installer leaves it, record 23 decision D3); and Kernel DMA
Protection (10 1803 and later), whether a device behind a tunnel waits
for the user's sign-in or is refused where the firmware turns the
protection on. No export table of 10 is in the tree either, so the
import surface on 10 is as unread as on 8; 10 is expected to answer the
driver's WDM version tests as 7 does, which is unread. On 64-bit 10 the
driver loads as on 64-bit 8 and 8.1 (record 23 section 5.10, revision
28; from `3.2.0.0` with a test-signed catalog per INF, record 19 section
3a, without which 10 x64 refuses the package); from 10 1607 with Secure Boot on, only portal-signed drivers load
(older cross-signed ones excepted, which the project's test signature is
not), so Secure Boot off is the condition there.

### 3.6 Thunderbolt on Windows 10: Intel's driver, the ranking and Kernel DMA Protection (task 43.33 (c), 2026-10-22; a reading)

A reading for roadmap-usb4 task 43.33 (c), before 43.34 widens the
connection manager to Windows 10; no code changed. Each fact is labelled
as design record 13 section 6.14 labels them: **static** (read out of a
file, nothing from it executed), **measured** (seen in a Windows 10 22H2
guest of task 43.32 through Windows' own tools), **documentation**
(a page read on 2026-10-22, cited by URL and a short phrase) or
**inference**. `legal-provenance.md` section 4 has the rows.

**Windows 10 has no Thunderbolt driver of its own** (static, both 22H2
images, 10.0.19045.2965): none of the 679 (x64) and 636 (x86)
`Windows\INF\*.inf` names an Intel NHI id of section 3.3's list, the
class `PCI\CC_0880`, or `PCI\CC_0C0340`; the word "Thunderbolt" occurs
only in two device descriptions (an LSI SAS adapter, two Lenovo dock
Ethernet chips). Measured on the x64 guest with the emulator's
`tb3-alpine-ridge` profile: the NHI `PCI\VEN_8086&DEV_15BF&SUBSYS_22B017AA&REV_01`
stays "Base System Device", Code 28 (`CM_PROB_FAILED_INSTALL`, problem
status `0xC0000490`), and the emulator's Alpine Ridge xHCI (`8086:15C1`)
is bound by `usbxhci.inf` (record 13 section 6.14.3). So on a 10 machine
the NHI runs whatever the computer maker installed, Intel's driver, or
nothing.

**Intel's driver as makers ship it** (static: three packages taken from
the Microsoft Update Catalog as `.cab` files, unpacked with 7-Zip, only
their INF text read; nothing executed):

| Package | INF and service | NHI ids it names | Signature, other |
|---|---|---|---|
| Older package with the Thunderbolt Software tray application, 17.4.77.13 (Catalog "Windows 10 version 1803 and Later Servicing Drivers") | `tbt100x.inf`, `DriverVer=05/16/2018,17.4.77.13`, `Class=System`, `[Manufacturer] ... NTamd64.10.0.1`; `AddService = nhi`, binary `tbt100x.sys`, demand start, KMDF 1.15; a co-installer `tbtcoinx_17_4_77_13.dll`; copies `setup.msi` (the tray application `Thunderbolt.exe` and the service `tbtsvc.exe`) | `PCI\VEN_8086&DEV_<id>&CC_0880` for 1575, 1577, 15BF, 15D2, 15D9, 15DC, 15DD, 15DE, 15E8, 15EB | Microsoft "Windows Hardware Compatibility Publisher" catalog `tbt100x.cat`; no `DmaRemappingCompatible`; x64 only |
| DCH driver 1.41.1379.0 (Catalog "Windows 10, version 1809 and later") | `TbtHostController.inf`, `DriverVer = 07/27/2023,1.41.1379.0`, decorations `NTamd64.10.0...16299` and `...19041`; `AddService = nhi`, binary `TbtBusDrv.sys`; a UMDF upper filter (`TbtFilterDrv`, `WUDFRd`) | the same ten Alpine Ridge and Titan Ridge ids with `&CC_0880`; `PCI\VEN_8086&DEV_1137` and `&DEV_1134` bare; Ice Lake 8A17, 8A0D, 8AA3, 8AA0, 8AB3, 8AB0 with `&CC_0880`; Tiger Lake and later (9A1B ... A834) with `&CC_0C03` | the same publisher; `HKR, Parameters, DmaRemappingCompatible, 0x00010001, 1` (one Tiger Lake A0 section writes 0); x64 only |
| DCH driver 1.41.1423.0 (Catalog "Windows 10, version 1903 and later") | as 1.41.1379.0, decorations adding `...22000` | **no Alpine Ridge id any more** (1575, 1577, 15BF, 15D2, 15D9, 15DC to 15DE gone); 15E8, 15EB, 1137, 1134, 8A17, 8A0D and the later ids kept | as above; the DCH tie to the Store application is a software component, `TbtHostControllerHsaComponent.inf` (`SoftwareID = pfn://AppUp.ThunderboltControlCenter_8j3eq9eme6ctt`, the Thunderbolt Control Center) |

No package has an x86 INF, so **on 10 x86 the NHI has no Intel driver to
meet** (static). No INF of the three carries a `FeatureScore`. Files,
hashes and the Catalog's update ids are in `legal-provenance.md`
section 4's row.

**Whether `xhcicm.inf`'s match outranks it** (static and documentation,
then inference). `xhcicm-amd64.inf` names the NHIs by `PCI\VEN_8086&DEV_<id>`
alone. Microsoft's page on PCI identifiers
(learn.microsoft.com, "Identifiers for PCI devices") lists as hardware ids
`VEN&DEV&SUBSYS&REV`, `VEN&DEV&SUBSYS`, `VEN&DEV&REV`, `VEN&DEV`,
`VEN&DEV&CC_ccsspp` and `VEN&DEV&CC_ccss`, and says "In Windows 10, some
IDs that previously appeared in the Hardware IDs list now appear in the
list of Compatible IDs". Measured on the x86 guest (the emulator's NHI,
its Enum key): `HardwareID` is `VEN&DEV&SUBSYS&REV`, `VEN&DEV&SUBSYS`,
`VEN&DEV&CC_088000`, `VEN&DEV&CC_0880`, and `CompatibleIDs` begins
`VEN&DEV&REV`, `VEN&DEV` - **so on 10 the bare `PCI\VEN_8086&DEV_15BF`
that `xhcicm.inf` names is a compatible id**, and Intel's `&CC_0880`
form is a hardware id. Intel's match is the better one twice over: a
hardware id against a compatible one, and its package is signed by Microsoft
(signature score 0), where this project's carries no catalog and ranks
`0xFF` in the signer byte (record 13 section 6.14.1 measured
`0x80FF2005` for `xhci98.inf` on the controller) - on 10 x64 it is not
staged at all without a catalog (record 13 section 6.14.5). *(From
3.2.0.0 the x64 package carries a test-signed catalog per INF, record 19
section 3a; what signer score 10 gives a catalog by an untrusted
certificate in test-signing mode is unread, and Intel's Microsoft
signature outranks it either way.)* **So Windows
never picks `xhcicm.inf` over Intel's driver by itself**: the installer's
forced install (`UpdateDriverForPlugAndPlayDevices` with
`INSTALLFLAG_FORCE`, record 23 section 5.5) is the only way the NHI
comes to `xhcicm.sys` on a machine with Intel's driver, and a later
driver search (Windows Update offers the Intel DCH package by these ids)
can take it back. What an install, update and uninstall of ours do to
Intel's driver is 43.37's leg "where an image of it is at hand"
(none has run here): by inference from record 23 section 9, the
uninstall's rescan gives the NHI back to the best-ranked driver in the
store, Intel's where it is still staged.

**The approval it does** (documentation; Intel publishes no Windows
interface of its driver). The firmware connection manager's security
levels are SL0 none, SL1 user, SL2 secure, SL3 DisplayPort and USB only
(docs.kernel.org, "USB4 and Thunderbolt": "User is asked whether the
device is allowed to be connected"); on SL1 Intel's software asks at the
first connection "Do Not Connect", "Connect Only Once", "Always Connect"
(the tray application's "Approve Attached Devices" on the older package,
the Control Center's "Approve Thunderbolt devices" on the DCH one;
Seagate's support article 007783en), "Always Connect" being kept by the
firmware's access list. The messages are the ICM's that section 5 sends
(`ICM_APPROVE_DEVICE`, `ICM_CHALLENGE_DEVICE`, `ICM_PREBOOT_ACL`; Linux
`icm.c`, the same mirror section 1.4.1 reads), so the approval policy of
section 5.4 replaces Intel's dialog when `xhcicm.sys` runs the NHI, and
the firmware's access list is shared between the two.

**Kernel DMA Protection** (documentation: Microsoft's "Kernel DMA
Protection" page, ms.date 2025-08-15, and "Enabling DMA remapping for
device drivers", ms.date 2025-08-25):

- Windows 10 1803 and later, 64-bit only, on firmware that sets
  `DMA_CTRL_PLATFORM_OPT_IN_FLAG` in the DMAR table (Intel) or the IVRS
  "DMA remap support" bit (AMD) and tags its external ports
  (`ExternalFacingPort` `_DSD`): "peripherals with DMA Remapping
  incompatible drivers are blocked from starting and performing DMA
  until an authorized user signs into the system or unlocks the screen";
  after that they run until a restart or an unplug. The
  `DmaGuard/DeviceEnumerationPolicy` policy (10 1809 and later) makes it
  0 block all, 1 (default) after sign-in or unlock, 2 allow all.
- A driver opts in with `HKR,Parameters,DmaRemappingCompatible,0x00010001,<v>`
  in its service key: 0 incompatible, 1 compatible, 2 compatible "only
  when ... the device is an external device (for example, Thunderbolt)"
  or DMA verification is on; no value lets the system decide.
  Windows' own `usbxhci.inf` writes 2 on 10 (record 13 section 6.14.1,
  static), Intel's DCH `TbtHostController.inf` writes 1 (static).
- msinfo32's "Kernel DMA Protection" line reads **Off** on both guests'
  QEMU firmware (measured: no DMAR table), so nothing of this is read on
  the vehicle.

What that means here (inference): the NHI is an internal device, not
behind an external port, so `xhcicm.sys` on the NHI is not itself held.
What the tunnel adds - a dock's xHCI controller (bound by `usbxhci.sys`,
opted in), an NVMe drive (Windows' `stornvme` opts in; **Stor98 writes no
`DmaRemappingCompatible`**) - sits behind an external-facing port, so on a
machine with the protection on, an NVMe drive behind a tunnel under
Stor98 is held until the user signs in, and never started under policy 0;
the same for any device whose driver has no value. Whether Stor98 should
write 1 (it maps its buffers through the DMA adapter) is a question for
43.34 (record 13 section 6.14.8). *(Settled 2026-10-22, the owner: a
static read of Stor98's NT path found every bus address it gives the
controller produced by SCSIport's DMA path - the uncached extension and
the SRB's own buffer, `ScsiPortGetPhysicalAddress` - and none taken around
it, so from `3.2.0.0` its x64 INF writes `DmaRemappingCompatible` 1, the
per-driver value Microsoft documents for releases up to Windows 11 23H2;
`src\stor98\docs\PLAN.md`, "Kernel DMA Protection". Unread on a machine
with the protection on.)* The protection is a feature of the
firmware and of 64-bit 10 only; it is unread on any machine here.
Linux's documentation adds that such machines usually ship with the
Thunderbolt security level set to "none", the IOMMU doing the gating
(docs.kernel.org, "security levels (if set) are redundant"), in which
case section 5.4's approval has nothing to approve.

## 4. The host interface: ring 0, polled

### 4.1 Registers

The driver maps BAR0 (256 KiB, `MmMapIoSpace`, non-cached) and reaches it
through `READ_REGISTER_ULONG` and `WRITE_REGISTER_ULONG`, as the HCD does. The
offsets it touches, every one from `ICM-PROTOCOL` section 2.1 [obs] unless
noted, are kept in one pure header `tbt_regs.h` with shift-and-mask macros
and no bitfields:

| Register | Offset | Use |
|---|---|---|
| host capabilities | `0x39640` | Total Paths (the ring count) in bits 10:0, interface version in 23:16 (0 on Alpine Ridge; the versions each row accepts are section 1.4.2's test 4, anything else the inert start) |
| firmware status | `0x39944` | bit 0 ICM enabled, bit 31 NVM authentication done |
| outbound mailbox | `0x3990c` | operating mode in bits 11:8 |
| inbound mailbox data and command | `0x39900`, `0x39904` | command in bits 7:0, error bit 30, request bit 31 |
| ring interrupt mask (`ICM-PROTOCOL`'s "ring interrupt enable 0") and Interrupt Mask Clear | `0x38200` (mask) and `0x38208` (mask clear), each as many dwords as 3N bits need | every cause masked at start, all 3N bits (section 4.3): this driver polls |
| TX ring 0 base, producer/consumer, size, options | `0x00000` block [inf] generic NHI layout, `0x0000c` size, `0x19800` options [obs] | |
| RX ring 0 base, producer/consumer, size, options, PDF masks | `0x08000` block [inf], `0x0800c` size, `0x29800` options, `0x29804` masks [obs] | the PDF masks written all ones, as the capture shows the Linux client writing them [obs] `ICM-PROTOCOL` section 2.1; in raw mode they have no effect (Spec 12.6.3.3.6, Table 12-27, which asks bits 0, 15, 16 and 31 to be written 0) |
| DMA misc | `0x39864` | read for the log only; the auto-clear bit is an MSI-X matter |

The generic ring register block offsets are the USB4 specification's
(section 12.6) and FreeBSD's `nhi_reg.h` cross-checks them (`ICM-PROTOCOL`
section 2 says so); the `39-0` batch transcribes the exact offsets this
driver uses into `tbt_regs.h` from the specification and `tools/usb4cm/core/u4regs.h`
(MIT), with the usual rule that no bit position comes from memory.

### 4.2 Rings and memory

One TX and one RX ring, 10 descriptors each, raw mode, 256-byte RX buffers
[obs]: 16-byte descriptors, so 160 bytes per ring, plus 10 RX buffers of 256
bytes and 10 TX buffers of 256 bytes, about 5.5 KiB in all. The block is one
common buffer below 4 GB from the adapter `IoGetDmaAdapter` returns, cached,
allocated once at start and freed at stop, through one named site
(`cm_pool.c`, the one file the allowlist admits pool and common-buffer
imports in, as `uas_mem.c` is for `xhciuas.sys`). The upper 32 bits of every
hardware address are written 0; no 64-bit arithmetic.

The ring code is the reference connection manager's `u4hi.c` (MIT), lifted
into the pure core as `tbt_ring.c` under the lift rules of section 9: TX
descriptor consumption by consumer index or the done bit, RX readiness by
the done bit, the RX consumer index written back after each frame, sequence
numbers, CRC-32C over control packets, and the error notifications. Its
callback contract (`mmio_rd`, `mmio_wr`, `dma_alloc`, `udelay`, `log`) is
what makes it host-testable and is kept as the seam between the pure core
and the driver.

Each ring's index register (offset `8h` of its block: the producer index
in one 16-bit half, the consumer index in the other, Spec Tables 12-19 and
12-24) is written as a whole dword: a TX producer write carries 0 in the
consumer half, and an RX consumer write 0 in the producer half. That is
correct only if the host interface ignores writes to the half it owns,
which is an assumption, **unverified on hardware**: it has run on the
emulator alone (39-V.2). Linux's `nhi.c` writes the 16-bit half alone
[linux]. On
a part that latched the written half, the RX producer would read 0 after
every frame. The writes are left as they are until a reading on the P14s
settles it; the alternative is a 16-bit write (`WRITE_REGISTER_USHORT`,
an import the gate would have to admit).

### 4.3 Polling, and no interrupt at all (decision D3)

The driver connects no interrupt. The NHI's INTx pin is wired [obs] but no
captured operating system used it [inf] `ICM-PROTOCOL` section 2.5, MSI-X is
what Linux and Windows 11 use and no primary target can use it, and the
emulator's legacy profile runs with `msi=off,msix=off` (`WIN98-TEST`). The
handoff's figures size the polling: a control-packet round trip is about
156 us median on real hardware, so a request is busy-polled while
outstanding (10 to 20 us stalls, `KeStallExecutionProcessor`, on the
connection-manager thread, never at DISPATCH_LEVEL), and the RX ring is
looked at every 100 to 250 ms when idle for unsolicited events (section
4.4 of the handoff; the period is the registry value `XhciCmPollIdleMs` of
section 8, default 200). Data traffic (NVMe, USB) never crosses the NHI, so
the idle period costs only hot-plug latency. The design leaves an interrupt
mode for a later revision, behind a value of its own, once INTx has been
read on real hardware.

Polling still masks every interrupt cause at ring start, since this
driver never resets the host interface (section 5.1) and so cannot rely
on the reset defaults (Spec 12.6.3.4.4, Table 12-31: default 0) being what
it finds. The mask covers all 3N cause bits the specification packs for N
paths - transmit, receive and receive vacancy (Spec 12.5.1, Figure 12-8) -
not only the 2N Linux masks (`nhi.c` `nhi_disable_interrupts`,
`RING_INTERRUPT_REG_COUNT` [linux]). It is written by each host's own
contract (`nhi.c` `nhi_mask_interrupt` [linux]): on Intel hosts, whose
interrupt status clears itself (`pci.c` `nhi_pci_check_quirks`,
`QUIRK_AUTO_CLEAR_INT` [linux]), by writing 0 to the mask register
directly; on every other host by writing ones to Interrupt Mask Clear
(Spec 12.6.3.4.5, Table 12-32). The interrupt status register is never
read: it clears on read unless auto-clear is disabled (Spec 12.6.3.4.1).

### 4.4 The connection-manager thread

One system thread per NHI, created at start and ended at the first of the
stop, the surprise removal or the removal (the start waits on it only
until the selection is made), at PASSIVE_LEVEL, as `hcd_ctl.c`'s controller thread is, with the same Windows
98 care around `PsCreateSystemThread`'s handle (the `IoIsWdmVersionAvailable`
branches at `hcd_ctl.c:2514`). Every ICM command, every mailbox handshake,
every wait and every re-enumeration call runs on it; nothing else touches
the rings. Work reaches it through a small queue under a spin lock. PnP
requests (start, stop, remove) queue their work and wait on an event, as
PnP dispatch may. **Power requests never wait**, and the NHI FDO is the
device's power policy owner, so its sequence is `hcd_power.c`'s, not a
filter's pass-through: a system power IRP is
marked pending and passed down, and its completion routine requests the
matching device power IRP (`PoRequestPowerIrp`) and completes the system
IRP, with one `PoStartNextPowerIrp` before that completion, when the
device IRP has finished; a D3 device IRP has its quiescence (section 5.6)
run on the thread first and is forwarded when that work is done; a D0
device IRP is marked pending and forwarded with a completion routine that
queues the reinitialisation to the thread and **retains** the IRP until
that work finishes, and only then `PoStartNextPowerIrp` and the
completion. `hcd_power.c` at its D0 branch (`hcdD0Done`, `hcdD0Finish`)
is the model for the completion ordering - with one difference, that the
HCD runs its D0 work in the completion routine when it finds itself at
PASSIVE_LEVEL and queues it otherwise, while this driver's hardware work
is always queued to its thread - and `win98-wdm.md`'s "Power Management" says
why Windows 98 would not show a mistake here: its implementation is a
no-op, so only the NT guests read the ordering. Waiting on a kernel event
in a power dispatch routine is forbidden on NT (Microsoft's "Passing Power
IRPs"). The thread is the only way the CONFIGMG
calls of section 6.1 can be made (they must come from a passive thread of
the system VM, `REENUM-EXPERIMENT` "Caveats") and the only way the
busy-polls can be bounded without blocking a dispatch routine.

Priority inside the thread: the busy-poll of an
outstanding command services every RX frame as it lands, so an event
arriving during a command is decoded at once, not after the command. A
`device disconnected` decoded during a command queues its tunnel-down
action for after the command completes, and the action's sequence (the
port reading, the `nvme98` statement of section 7.2, then the removal and
re-enumeration of section 6) runs then; a command adds no more than its
own bound to that path. The end-to-end
budget for the statement, against `nvme98`'s gone deadline (5 s in its
test build, measured from the endpoint's own presence loss), is section
7.2's; it needs more than this paragraph, since the idle poll, the port
reading and the recipient lookup each cost time of their own.

## 5. The ICM client

### 5.1 Bind, on the thread (`ICM-PROTOCOL` section 6.1)

The steps as the AR row runs them, with each other row's differences from
section 1.4's tables named in the step. "Inert" is section 1.4.2's inert
start: the devnode started, the reason logged and counted, nothing else
done.

0. Integrated family only: the NHI's force power, firmware-ready poll and
   LTR of section 1.4.3, through the PDO's configuration space; never
   ready: inert (section 1.4.2 test 2) [linux].
1. Enable Bus Master in the NHI's command register through
   `IRP_MN_WRITE_CONFIG` to its own PDO, the route `hcd_svc.c` proved on
   Windows 98 SE and Windows 2000 (its header comment, 26-A.2). As built,
   the write is made only once the selection has chosen the ICM client
   (after steps 2 to 5), so an inert start leaves the command register as
   it found it.
2. Read host capabilities. All ones: inert (test 2). Ring count below 1, or
   an interface version the row does not accept (test 4: 0 on AR [obs];
   below `0x40` on the others, this project's policy): inert, the value in the log. A
   host router that wants Linux's version-2 reset is not this record's.
3. Read firmware status (Intel candidates only, test 3). ICM enabled
   clear in a valid read: inert - except on the Maple Ridge and Tiger Lake
   to Meteor Lake ids, where it is the positive software connection
   manager admission (2) of section 1.4.2 and the start is handed to
   design record 22's profile. ICM enabled set: on those ids, poll NVM
   authentication done for 10 ms (`icm_tgl_is_supported` [linux]); never
   done: inert; done, the ICM admission continues. This driver never starts stopped
   firmware (section 2.7 of the contract, and Linux's `start_icm` path,
   are out of scope).
4. AR and TR rows (Maple Ridge included): poll firmware status for NVM authentication done, up to
   60 reads 50 ms apart ([inf]; `icm_ar_get_mode` [linux]); never set:
   inert. FR and ICL rows have no such step (`get_mode` unset [linux]).
5. AR and TR rows (Maple Ridge included): read the operating mode. 3 proceeds; 0 (safe mode)
   starts with no rings and a log line saying the controller is in safe
   mode, which is the inert start under its own name; 1 (authentication)
   and 2 (endpoint), or any other value: inert ([inf]; `icm_firmware_init`
   [linux]).
6. Allocate and start TX and RX ring 0, raw mode, PDF mask all ones, every
   interrupt cause masked (section 4.3). A failure: the failure after activation, the
   rings stopped if started and the common buffer freed after them.
7. AR and TR rows (Maple Ridge included): mailbox `0x23` allow all devices (sent by Linux only in
   connection-manager mode, `icm_firmware_init` [linux]): write the data
   word 0, then the command with the request bit, poll every 10 to 20 us
   for up to 500 ms until the request bit clears; error bit set or
   timeout: the failure after activation of section 1.4.2 (the rings
   stopped, no further mailbox) [inf, observed as the end state].
8. Send `driver ready` (one header word, code 3, flags 0) and wait for its
   response (the AR and FR two words in 5 s, four tries; the TR and ICL
   four words with the timeouts of section 1.4.4). Keep the flags
   byte, the security level, the boot-ACL fields and, on TR and ICL, the
   protocol version, NVM version and device id for the log; act on none of
   them in the first cut. No answer after the tries: the failure after
   activation of section 1.4.2 (the rings stopped and drained, `0x07` at
   stop since `driver ready` was sent, tunnels untouched).
9. Poll the host router: a config read of router-space dword 0, route 0, one
   dword, 100 ms, up to 50 tries 50 ms apart, draining and queueing any
   `device connected` events that arrive meanwhile [inf, consistent with the
   observed 580 ms hold]. Never answered: log it and carry on - the events
   are what matter, and the emulator's R8 says config requests are delayed,
   never dropped. This is the one bounded step whose exhaustion is not a
   failure, at bind or at resume: the reconciliation window of section 5.3
   then starts at the poll's exhaustion instead of its success.
10. Process the queued events (section 5.3).

What is not done at bind, by design: no host-router enumeration, no
capability-chain walk, no EEPROM bit-bang, no DROM, no DMA port, no NVM
version. The Linux client reads all of that [obs] section 5.5; the Windows
11 driver logs NVM versions and so probably reads the DMA port [inf]
section 9 item 3; this driver needs none of it to approve a device and
re-enumerate a bus. `ARCHITECTURE` section 7 says as much for the legacy
client: the minimum set is allow-all, driver ready, get route and approve,
and the names in the event are enough. Linux's own bind also resets the
two physical ports' links when both lanes are up (`icm_reset_phy_port`),
but only when it holds the upstream port's vendor capability, which only
its firmware-start path sets up [linux]; this driver never takes that path.

### 5.2 Frames

A pure `tbt_icm.c` encodes requests and decodes responses and events from
the word layouts of `ICM-PROTOCOL` sections 3.1 and 4, to be host-tested against
the observed frames quoted there: `driver ready` (`T:1`, `T:2`), `get route`
(`T:833`, `T:834`), `approve` (`T:34153`, `T:34154`), `device connected`
(`T:9`, 61 words, the name block decoded entry by entry), `device
disconnected` (`T:21087`). Frame type 11 out, 12 and 10 in; the CRC-32C
trailer is appended to every command and checked on every response and
event (`tbt_ring.c`): a mismatch is counted (`cm.ring.crc-errors`), logged
and flagged on the frame, which is still decoded - an ICM frame is never
dropped for its trailer, while a control packet with a bad one is (Spec
6.4.4 has the request retried). One command outstanding at a time; a response matches by frame type
and code; an event arriving while a command is outstanding is decoded at
once and acted on as section 4.4 says, never dropped. Error is bit 0 of the
response flags; an unknown event code is logged and ignored. The encoders
and decoders sit behind section 1.4's profiles, the AR row filled from the
observed frames and the FR, TR and ICL rows from the Linux structures
[linux]; the profile is chosen by the NHI device id at `AddDevice` and
confirmed after start, and `XhciCmMode` 2 maps an unknown Intel id onto the
AR row. Every decoder length-checks its frame against its row's structure
before reading a field and hands back "malformed" rather than a partial
decode; section 1.4.6 says what the client then does. FR's topology
answer is the one multi-frame response this driver accepts, assembled by
packet index (`ICM-PROTOCOL` section 3.1's rule). No frame of any row but
AR has been observed, so `test_tbt_icm`'s FR, TR and ICL vectors are
synthesised from the structures.

### 5.3 Events and the device table

The driver keeps a table of announced devices, 16 entries (the boot ACL's
size, and more than any one port can carry at depth 1), each with the UUID,
connection key and id, link, depth, the approved, rejected and boot bits,
the vendor and model strings from the name block, and the route string from
`get route` (on the FR row from the topology query; on the TR and ICL rows
from the event, with no key, and the entry keyed by route string where AR
and FR key it by link and depth; section 1.4.4). On `device connected`:

- `rejected` set: log and ignore, as the client does [inf].
- A known UUID at the same place whose entry needs recovery - left
  unrouted by a failed route request, "approve outcome unknown", or
  unapproved after an approve answered with the error flag (section 5.5):
  refresh key and id; retry the route if it is missing; an unknown
  outcome is settled by this announcement (`approved` = 1, a tunnel up;
  0, unapproved); then, where the announcement says `approved` = 0 and the
  entry is not rejected, apply the approval policy as for a new device.
  Each fresh announcement starts at most one approval operation,
  including its bounded timeout retries (section 5.5); after that
  operation finishes, no new approval operation starts until another
  announcement.
- A known UUID at the same link and depth (TR and ICL: route string) and
  approval state, needing no recovery: refresh key, id
  and route, nothing else (the resume case, [obs] section 6.5).
- Otherwise: drop any stale entry at that place, ask for the route where
  the profile says it must be asked for (AR `get route`, FR the topology
  query; TR and ICL carry it in the event, section 1.4.4), store the route,
  and apply the approval policy (section 5.4) - on the ICL row, which has
  no approve operation (section 1.4.4), only its "leave unapproved" half.

On `device disconnected`: drop the entry at that link and depth, or that
route string on the TR and ICL rows, then run the tunnel-down sequence of
section 6.5. No matching entry is section 1.4's unmatched disconnect on
every row. A disconnect, and a replacing announcement at the same place
(the last case of the list above), also removes every entry below the
device it names, deepest first, each with its own tunnel-down sequence:
a device cannot stay behind a router that left (Linux's
`tb_switch_remove` takes the switches below it [linux]). A disconnect
also cancels any queued announcement at or below that place; a
replacement does not, since announcements queued behind it describe the
topology that replaced the old one.

An announcement can be lost when it arrives with this driver's event
queue full. It is not dropped silently: at the next idle poll the driver
sends another `driver ready`, without restarting the rings or sending
`0x23`, as a resume does, and the firmware announces every device again
(tunnels survive it [obs] `ICM-PROTOCOL` sections 2.3 and 7); the
reconciliation then runs as after any `driver ready`. At most three such
`driver ready`s in a row (the count starts again once one recovers
without another loss); if another loss would need a fourth, the next idle
poll logs the exhaustion and treats every tunnel port of the domain as
changed (a RESYNC, section 6),
as it does for a tunnel action that did not fit its queue and for an
unmatched disconnect. The firmware sends
nothing for an unplug while the driver was unloaded [obs] section 6.6, so
after every `driver ready` the table is reconciled: an entry not re-announced
within 500 ms of the host-router poll succeeding, or of its 50 tries running
out (section 5.1 step 9), is treated as gone and the
tunnel-down sequence runs for it [inf, the client's own 500 ms check].

**The router's generation, read from the router (task 43.44, the owner's
choice of 2026-10-21).** The status record (design record 24 section 3)
gives each table entry a generation. Nothing in an announcement carries
one: the event's flags give the link's rate and width, the name block
the DROM's names and ids, and neither says whether the device router is
a Thunderbolt 3 router or a USB4 one. Until this revision every entry
was written "Thunderbolt 3", which the owner's Framework 13 (12th
generation, Alder Lake, this row's Tiger Lake class) showed wrong for
the ORICO ASM246x enclosure, a USB4 device router on a USB4 host. The
client therefore reads the router itself, as Linux's client does for
every router the firmware announces
(`icm.c` `alloc_switch`, `switch.c` `tb_switch_alloc`: one DW of the
router space at the route, then ROUTER_CS_0 to 4, five DWs, and
`tb_switch_get_generation` from ROUTER_CS_4's USB4 Version [linux]).

- **Which hosts: every row** (the owner, 2026-10-22). The generation
  is the device router's, not the link's: the ORICO behind Alpine Ridge
  reads USB4 Version `20h` [obs] and Linux names it generation 4, while
  a Thunderbolt 3 dock behind a USB4 host reads major 0. So the driver
  turns the read on at bind (`TbtIcmSetReadVersion`) on each row:
  - ICL (Ice Lake, and Tiger Lake to Meteor Lake) and TR (Titan Ridge,
    and Maple Ridge in ICM mode): the route from the event;
  - AR: the route from `get route` [obs], the read following it as
    Linux's did in the capture;
  - FR: the route from the topology query (section 1.4.4). Linux's
    `icm_fr_device_connected` reaches `alloc_switch` too [linux], so
    the read is as safe there as on the other uncaptured rows. What it
    can say there is narrower: the record has no Thunderbolt 1 or 2
    value, so a Thunderbolt 1 or 2 router (major 0, as every pre-USB4
    router [linux] `tb_switch_is_usb4`) shows "Thunderbolt 3" as it did
    before, and only a USB4 router - behind a Thunderbolt 3 to 2
    adapter, if one ever works there - is told apart. That is still the
    router's own word rather than an assumption, at the cost of one
    read per occupancy, and one rule for every row is simpler to hold
    than an exception whose reason is a value the record lacks.
  A host test may leave it off (the default of `TbtIcmInit`). The USB3
  field is a separate matter and stays the host's: none on Falcon,
  Alpine and Titan Ridge, whose links carry no USB3 tunnel whatever the
  router, and not known on the integrated rows and on Maple Ridge, a
  USB4 host the TR row also serves (told apart by the hardware row's
  ownership predicate, NVM authentication done; record 24 section 3).
- **The frame.** One Read Request (PDF 1, Spec 6.4.2.3) on TX ring 0,
  the ring the ICM's own commands use, through `TbtRingCtlRead`, the
  path the host-router poll of section 5.1 step 9 already takes at
  route 0 on every row: router configuration space, DW 0, five DWs
  (ROUTER_CS_0 to 4, the read Linux makes), at the route the entry
  holds (the one its row gives, above). It
  is a read alone: no write of any kind, no router operation, nothing a
  firmware connection manager could take for a competing one. The
  response is a Read Response (PDF 1) matched by route and by the echo
  of Address, Size, CS and Sequence Number (`tbt_ring.c`); a
  request-error notification ends it (`TBT_E_NOTIFY`); every other
  frame met during it - an ICM event among them - is handed to the
  client's receiver, where, as in any wait, an announcement is queued
  and a disconnect acts at once (section 5.2).
- **That the firmware answers it.** Observed on the Alpine Ridge
  firmware: Linux's client read the dock's router header at route 1
  after the get route of every announcement, approved or not - 85
  requests in 13 ms for the ORICO, `0x2463174c 0x001101c0 0x00000001
  0x80000000 0x200000fe`, USB4 Version `20h`, and the TS3 Plus's
  `0x020000fe`, USB4 Version `02h`, a Thunderbolt 3 router
  (`ICM-PROTOCOL` sections 5.3, 5.5, 6.4 and 13.3) [obs]. On the
  other rows the same read is [linux] (`icm_fr_device_connected`,
  `icm_tr_device_connected` and `icm_icl_device_connected` all reach
  `alloc_switch`) and uncaptured. A router not yet approved is read the same way: approval
  is the firmware's decision to build the PCIe tunnel, and the router
  answers configuration requests on the control channel before it
  ([obs] section 6.4, the dock enumerated between its announcement with
  `approved` = 0 and the approve). Under level `user`, `secure` (which
  this driver never satisfies, section 5.4) or a level that never
  tunnels PCIe, the router the firmware announced with a route has been
  enumerated by it, and the read neither approves nor refuses anything.
  Under level `none` the firmware tunnels before it announces; the read
  is the same.
- **What the firmware may be doing.** The firmware announces a router
  after it enumerated it; it may still be configuring it (tunnels,
  deeper routers) when the read arrives. A read changes nothing a
  configuration depends on, and the router answers each control request
  on its own; Linux's 85 reads interleave with the firmware's own work
  in the capture without effect [obs]. After the first `driver ready`
  the firmware holds configuration requests while it announces (about
  580 ms, 2.77 s under BIOS Assist with a pre-boot device [obs] section
  5.5's paragraph on held requests); the read is made only after the
  host-router poll, which waited out that hold; a read that met a hold
  still on would time out and leave the generation not known.
- **When.** Never in an event's path and never in a wait: a new entry
  (a new occupancy, section 5.3's last case) is marked "version owed",
  and the next idle poll (`TbtIcmPoll`) makes the read before it takes
  any frame, so the actions of the poll that created the entry have
  already been drained (section 6.5) and never wait behind it. An
  announcement already on the RX ring, or arriving during the read, is
  queued as in any wait (section 5.2) and handled after the read: its
  route request and approve wait at most the read's bound below, which
  is well inside the 9.4 s a USB4 device waits for its approve on the
  P14s before it falls back to USB (section 5.4) - and at most once per
  ring session, since a read that goes unanswered stops the reads (the
  stale answer, below). One read per poll, the oldest owed entry
  first; the client counts itself in a wait for its duration, so a
  re-entrant command is refused. A read is owed only by an entry with a
  route; none is made on a failed or stopped instance, nor inside a
  resume, a recovery or a rediscovery, nor after a `driver ready` whose
  host-router poll was never answered (until a later one is).
- **Timeout and retry.** One attempt: a response wait of its own 1 s
  (`TBT_ICM_VERSION_TIMEOUT_MS`), not the command timeout, after the
  request's consumption by the host interface, which the ring bounds
  separately (`TBT_TX_CONSUME_MS`, 100 ms) - about 1.1 s in all - with
  no retry: a read that answers comes in milliseconds [obs] (85 requests
  in 13 ms), and its value is informational, so a timeout is cheaper to
  report as "not known" than to retry, or to wait longer, at the cost of
  the thread and of an approve queued behind it. That is inside section
  5.5's per-command bound and outside the resume budget of section 5.6
  (no read runs in a resume). A liveness gate already closed, or
  revoked from another context, ends the wait at once; otherwise a host
  interface lost during the read is seen at the thread's next sample
  (section 5.5) and the read's own bound is what it costs.
- **The stale answer.** A Read Response is matched by its route and the
  echo of Address, Size, CS and a Sequence Number of two bits
  (`tbt_ring.c`); nothing in it names the occupancy. A read that was
  sent and not answered (a timeout, or a request published and not seen
  consumed) may still be answered later, and after a replacement at the
  same route and three more requests that late answer would match a new
  read of the same header and be taken for the new router's. So the
  first unanswered read stops the reads until the rings restart (a
  resume, a full recovery, a new bind: a restarted ring delivers
  nothing of an earlier request, section 5.6), and every occupancy
  owed a read meanwhile stays "not known" and is read after the
  restart. A request-error notification carries no sequence number
  either, so it is taken as this read's answer only where no other
  request outstanding could have drawn it: no earlier router read (an
  unanswered one has stopped the reads), and no host-router poll - the
  polls read route 0's DW 0, which the host router answers and has no
  request error to give, and a router read is made only in a session
  whose last host-router poll was answered (section 5.1 step 9; the
  firmware serves the requests it held when it releases the hold [obs]
  section 6.2). A notification so taken stops nothing, so a device that
  left and its re-plug at the same port are not left "not known".
- **What is kept.** The fifth DW's bits 31:24, the USB4 Version, by its
  major alone (design record 22 section 3.5, `TbtHwRouterVersion`): a
  major of 1 or 2 is USB4 with the byte kept, and the entry carries
  `CM_ST_GEN_USB4 | version << 8` as the software connection manager's
  USB4 rows do; major 0 is a Thunderbolt 3 router and the entry carries
  `CM_ST_GEN_TBT3`, as the software connection manager writes such a
  router; any other major is reserved and the generation not known. The
  answer is taken only if ROUTER_CS_3 has TopologyID Valid set and
  ROUTER_CS_2 and ROUTER_CS_3 hold the entry's route (the TopologyID a
  router at that route carries once enumerated, Spec 8.2.1.1 [obs] in
  both readings above): one that does not is logged with its five DWs
  and the generation is not known. The five DWs are logged in every
  case, for the row no capture has seen.
- **Failure.** A timeout, a request-error notification, a short
  response, a header that is not the route's, or a reserved major: the
  generation is "not known" (`CM_ST_GEN_UNKNOWN`) for that occupancy,
  logged and counted, and never "Thunderbolt 3" on these hosts; nothing
  else of the entry changes and nothing is retried. Until the read is
  made the entry is "not known" as well.
- **Resume, reconnect and the occupancy.** The value belongs to the
  occupancy (the entry's serial): a refresh of the same occupancy (the
  resume case, a recovery's or a rediscovery's re-announcement) keeps
  it and reads nothing, as Linux reads nothing on that path [obs]
  section 6.5; a disconnect, a replacement or a move ends the
  occupancy, and the new one is read afresh. An answer that lands after
  its entry left, or after the slot was taken by another occupancy, is
  discarded.
- **What has been verified, and where.** The host vectors (section
  10.1) exercise the client against the read on every row. The
  emulator's firmware-connection-manager models are Alpine Ridge and
  Titan Ridge (`usb4-emu` `src/qemu/hw/usb4/usb4-host.c`, its profile
  selector), each able to carry a USB4 device router (its port's
  `usb4` flag, `usb4-icm.c`) or a Thunderbolt 3 one: the legs read the
  generation there, a USB4 router and a Thunderbolt 3 router on each.
  It has no Ice Lake class or Maple Ridge model yet (one is being
  added at the owner's request); until a leg on it, the integrated
  rows' evidence is the owner's Framework 13 with the ORICO enclosure,
  which must read USB4 Version major 1.
- **The USB3 tunnel stays "not known"** on the integrated rows and
  Maple Ridge. The record's USB3 field there is not filled from this
  read. ROUTER_CS_5's USB3
  Tunneling On is a bit the connection manager sets to let the router's
  USB3 function work tunnelled, not evidence that a path exists, and
  what the firmware sets there is uncaptured; the paths themselves are
  in the adapters' configuration spaces at both ends, a walk of several
  reads (adapter headers, capability chains, path entries) of state
  the firmware changes without an event. That is not a read-once
  value, and this revision does not take it.

### 5.4 Approval policy (decision D4, taken 2026-10-14)

The domain's security level is `user` on the P14s [obs]: every newly plugged
device is announced unapproved and gets its tunnel only on `approve`.
Windows 98 and 2000 have no Thunderbolt control panel and this project will
not write one, so approval is a policy, the registry value `XhciCmApprove`:

| Value | Meaning |
|---|---|
| 1 (default) | approve every announced device that is not rejected, at once, on every announcement with `approved` = 0, on every row with an approve operation (the ICL row has none, section 1.4.4, and behaves as 0) |
| 0 | approve nothing; devices tunnelled by pre-boot or approved by an earlier driver keep their tunnels and are used; a newly plugged device is logged and left unapproved |

**Nothing is remembered** (owner, 2026-10-14). The driver keeps no list of
devices across reboots, and that is harmless because the state that
matters is held elsewhere: the firmware keeps an approval and its tunnel
for the power cycle (across driver unload, disable, resume and rebind, the
device re-announced with `approved` = 1 [obs] section 7), and the
controller's NVM holds the boot ACL the BIOS uses for pre-boot tunnelling,
which this driver reads for the log and never writes. After a re-plug the
device comes back unapproved [obs] section 7; after a reboot it comes back
unapproved unless pre-boot support is on, in which case the firmware
tunnels it before the OS and the OS finds it authorised with `boot` = 1
[obs] section 12.1 (a driverless Windows 98 saw the dock's PCIe tree, and
Linux reported `authorized` 1 and `boot` 1 with no approve command). What
is inferred rather than observed: that boot-ACL membership is the
criterion (section 12.1 says so in as many words [inf]), and the
announcement's fields themselves, `approved` = 1 with link-info bit 10
set, since the Linux tracepoints of that run started after boot [inf];
R25 is the emulator's contract for that announcement, written to the
inference. Either way the policy table's row for a pre-boot-tunnelled
device applies. At 1 an
unapproved return is approved again at once, at 0 it stays unapproved, and
a remembered list would change neither. The one policy a list would enable
- "approve only devices accepted before", which is what the Windows 11
service is inferred to do [inf] with the per-device entries observed under
its `nhi` service key [obs] section 10.3 - has no dialog to accept a device
in on these targets, so it could only be filled by hand from the log's
UUIDs. It is listed as a later option under D4, not a value of this cut.

`approve` carries the fields of the announcement its row names (section
1.4.4: UUID, connection key and id on FR and AR; UUID, route and id on
TR), waits 10 s, and on error flags leaves the entry unapproved with a log
line; no answer is section 1.4.6's unknown outcome. An answer is taken as
this approve's only when, beside its code and packet index 0, it echoes
every word of the request after the header; one that does not - a late
answer to an earlier approve among them - is a stray, and the wait goes
on. A
success means the firmware built the PCIe tunnel while processing the
request [obs] section 4.3, so the tunnel-up sequence of section 6.5 follows
at once.

The approve has a deadline on real hardware. On the P14s an unapproved
USB4 device (not a Thunderbolt 3 one) falls back to USB about 9.4 s after
its announcement: the firmware reports it disconnected and it reappears as
a USB 3.2 disk on the controller's own xHCI [obs] (`usb4-emu`
`docs/captures/P14S-ORICO-2026-10-15.md`); the real approve there took
383 ms. Measured on the emulator under Windows 2000 (`runs/run-39.md`,
39-V.2): 264 ms and 243 ms from announcement to approve on the Titan Ridge
row, which has no get route; 4 ms, 19 ms and 86 ms on the Alpine Ridge
row, get route included. `XhciCmApproveTimeoutMs` bounds only the wait
for the reply to an approve already sent; the ICM commands are serialised
on the connection-manager thread, so a second announcement can wait behind
an earlier command (up to that command's retries) only while the firmware
is not answering. No setting puts a user action between an announcement
and its approve: `XhciCmApprove` 1 approves at once, and 0 never approves,
which on the P14s leaves a USB4 device to its USB fallback. A pending
approval belongs to its connection: an unplug, a re-plug or mailbox `0x06`
cancels it, and the firmware then answers the stale approve with the error
flag. That answer is the error-flag case above - one `approve-failed`
line, no retry, the instance not failed - and the disconnect that follows
removes the entry (host-tested in `test	est_tbt_icm.c`).

What 1 means for the user is written plainly in the release notes: any
Thunderbolt 3 device plugged into the machine gets PCIe access, which on a
machine without an IOMMU (every target here) is access to all of memory.
That is the state every pre-boot-tunnelled device is already in on these
systems, and what the Windows 11 service does for a device it has seen once
[inf] section 10.3, but it is an owner decision and a documented one.

### 5.5 Timeouts, retries and faults

5 s per command, three retries on timeout, 10 s for `approve` [inf], and the
`driver ready` timeouts and tries of section 1.4.4 where a row names them.
A command that never answers after its retries fails the operation, never
the driver: a bind step of section 5.1 gives the inert start or the
failure after activation (section 1.4.2); a later failure is logged and
the thread goes on polling, with the device table changed only as each
case says. `approve`: an error flag leaves the entry unapproved, kept; no
answer leaves it "outcome unknown", kept (section 1.4.6). A failed route
request (`get route`, FR's topology query) leaves the entry unrouted and
unapproved, kept, and the next announcement retries it; no approve is sent
for an unrouted entry. A device the firmware announced already approved
(pre-boot, or approved before this driver bound) still gets its
tunnel-up sequence (section 6.5) with its route missing, since the tunnel
exists whatever the route request did. Entries are removed only by a
disconnect (with the entries below it), by the reconciliation, or by a
replacing announcement at the same place (section 5.3).
A frame longer than its buffer, or a response with an unexpected code, is
counted, not acted on; a CRC mismatch is counted and the frame still
decoded, as above. Counters: section 8.

**A host interface that stops answering** (reviewed and revised on
2026-10-15, after the R32 leg of `39-V.2`). With BIOS Assist on, after the
last unplug the firmware answers no ICM command (`ICM-PROTOCOL` R32) and
the platform then powers the controller off: on the P14s its registers
read all ones after 10 to 11 s and the controller is removed at about
15 s, not to come back on a re-plug [obs]; on the emulator's i440FX tree
the subtree's registers read all ones from the power-off and come back,
on a re-plug, with their configuration reset and no PnP removal. Command
silence alone is never taken as absence. The thread samples one register
this driver programmed and reads back, RX ring 0's size register, at the
idle cadence (`XhciCmPollIdleMs`, never on a command, ring or router
path), against the value it read after the ring start
(`src\cm\cm_live.c`): the first all-ones sample stops the transport
(SUSPECT), the third in a row is ABSENT, and a readable value that differs
is CONFIG_LOST. ABSENT and CONFIG_LOST report every device gone, once per
episode, as `TUNNEL_DOWN` actions of why UNAVAILABLE followed by a RESYNC
(the re-enumeration seam's request; on 98 SE and ME the CONFIGMG worker
removes the children it retained at its last walk of the bridge before
the loss - the tunnel-up re-enumeration's, the start's baseline or a
marking window's - each revalidated by the instance id recorded then and
found through the devnode tree alone, never by reading a bridge header
the loss may have reset; a refusal is respected, and no re-enumeration
of that bridge runs before the removal), and say nothing about the
endpoint: presence lost, not quiescence (section 7.3). `nvme98`'s
link-state channel gets no statement from it: on NT each entry is treated
as for a withheld one (`cm_nvlink.h` CM_NVLINK_LOST: never DOWN_QUIESCED,
UP cleared only where its function reads absent), on 98 SE and ME it is
not touched (that would read the bridges); a presence-lost statement of
its own is owed, and meanwhile `nvme98`'s own deadline path ends what is
in flight. One probe a second follows
outside ACTIVE. No sample is taken out of D0, while the system sleeps or
during a power, PnP or recovery sequence, so a resume - and its own time
budget - never trips it. Nor does a firmware that holds requests: under
Assist with a USB4 device tunnelled pre-boot, the P14s holds config
requests (queued, never dropped) for about 2.77 s after the first driver
ready [obs], within the command timeout (`XhciCmCmdTimeoutMs`, 5 s a try);
the sample is an MMIO read of the host interface, which that hold does not
touch. Owed: the R32 re-plug on an emulator whose i440FX slot removes the
subtree through ACPI hot-plug at the power-off (announced for after the
`3.0.0.0` dry cut); on the current one the re-plug fails by platform
(`runs/run-39.md`, 39-V.2).

### 5.6 Stop, remove, disable, suspend and resume

Each sequence depends on the row (section 1.4.4) and on how far the start
got (section 1.4.2). An **inert** instance does none of them: at stop it
writes nothing, a force-power bit it set included, which it leaves set
and logs, as the power-ownership record says.
An instance that **failed after activation** has already stopped its rings
and runs only the cleanup its recorded stage allows. For an **active**
instance, in the order the capture or the client shows:

- **Unbind** (stop, remove, disable): ring 0 stopped (options cleared,
  interrupts already masked), the thread's queued work drained, then mailbox
  `0x07` driver unloads [obs] section 6.7, on every row (Linux's
  `icm_stop` sends it on all four); the thread ended, the common buffer
  freed after the rings, and on the integrated family the force-power bit
  restored as section 1.4.2 says. Tunnels and approvals survive it [obs]
  section 2.3, so the dock keeps working across a driver disable, as it
  does under Linux's unbind. Nothing behind the tunnel is removed by this
  driver at its own stop.
- **Shutdown**: the unbind sequence, if the stop has not already run it.
  **On Windows 98 SE and ME it runs with no device IRP** (task 43.65,
  2026-10-23; `cm_pwrr.c`, host suite `test_cm_pwrr`): the system
  IRP's completion takes the direct route (`CmPowerDirectWork`, the
  route that already served a failed `PoRequestPowerIrp`) - the thread's
  quiescence, then the system IRP completed - instead of requesting D3
  and passing that D3 down from the thread. Read in the emulator (ME,
  the `usb4-generic` host, 2026-10-23, `runs/run-43.md`, "43.65"): on
  the first shutdown after a wizard or the installer had started the
  driver without a restart, three of five such shutdowns hung with VMM
  idle (EIP `c005255a`, interrupts off, every thread blocked), and the
  driver's log ring read from the guest's memory ended, on the build
  that marked each step, after `PoStartNextPowerIrp` and before
  `PoCallDriver` of the D3 returned: the bus below never returned it.
  The shutdowns of a driver started at boot passed. Why the bus blocks
  is not read (it is NTKERN's and CONFIGMG's; no static reading taken);
  a shutdown needs no device state of this driver's, the quiescence is
  the same on either route, and `xhci98.sys`, whose D3 is passed down
  from its own dispatch routine, is unchanged. Sleeps on 9x, and every
  transition on NT, request their device IRP as before.
- **System suspend** (`IRP_MN_SET_POWER` to D3 for S3 or S4): first the
  held mark of section 6.3 on every active member; then rings stopped,
  then the row's mailbox sequence of section 1.4.4 - FR and AR `0x05` save
  devices then `0x07` ([inf] section 2.3; [linux] `icm_suspend` with
  `icm_fr_save_devices`); TR and Maple Ridge `0x07` alone (`icm_suspend`
  with no `save_devices`); the ICL row nothing, Linux having no system
  suspend operation for it, and this project treating suspend with an
  integrated controller as unsupported (section 1.4.3). Resume to D0: on
  the integrated family the force power, firmware-ready poll and LTR of
  section 1.4.3 first; then the rings reinitialised and restarted in the
  common buffer kept since start (section 4.2 allocates it once; suspend
  only stopped the rings), and the bind sequence of section 5.1 from step
  8, without the mailbox `0x23` [inf] section 6.5 (the one place the
  runtime-resume capture is taken as the model, because no system-resume
  capture exists; Linux's `icm_complete` likewise sends only `driver
  ready` and the host-router poll), and the reconciliation of section
  5.3, so a device unplugged during the sleep is torn down and one that
  stayed is refreshed; then the held members released (section 6.3).
  Each [inf] step is tolerated as a mailbox error or a timeout if the
  firmware refuses it, and a resume that fails takes section 6.3's failure
  path. Windows 98 on the APM machines this project runs rarely reaches
  S3; the Windows 2000 and later guests do, and the VM legs read it there.
- **Runtime suspend** (RTD3): never requested by this driver, whatever the
  driver-ready flags say, so the `0x07`-alone sequence of section 6.5 is
  never sent for it, and the ICL row's RTD3 veto needs nothing.
- **The host interface lost** (section 5.5's SUSPECT, ABSENT and
  CONFIG_LOST, and from the moment `IRP_MN_SURPRISE_REMOVAL` arrives): a
  gate on the BAR accessors refuses every access the state does not admit
  (reads all ones, writes dropped, both counted) and ends every wait of
  the core at once; a surprise removal also refuses the configuration
  route. The stop, the shutdown and the suspend then write nothing to the
  host interface - no ring stop, no `0x07` - and force power is never
  cleared from that state: what depends on it is unknown. The common
  buffer is freed only after a stop whose read-back, after the settle,
  shows both ring 0 controls readable and disabled; without that evidence
  it stays allocated with its adapter until the next boot. A resume from
  a suspend taken in that state takes the failure path (U15's order: the
  held bridges released, then invalidated).
- **Recovery, unlike resume.** Recovery runs only where the platform
  restores access without a PnP removal; it never repairs a bridge's bus
  numbers or windows, which are the bus driver's, so where the platform
  resets the subtree (the emulator's i440FX tree) the instance stays
  absent until a PnP restart or a reboot, and where it removes the
  controller (the P14s) the PnP removal ends the instance. Its
  precondition, on reads alone: the configuration space's first DW and
  BAR0 (both DWs of a 64-bit BAR) as the healthy start read them, memory
  decoding on and Bus Master restored, the host capabilities readable, and
  the selection's tests of section 1.4.2 passing again with the same
  outcome. A sample that comes back unchanged gets the light recovery on
  the retained rings: both rings' programmed values verified, the old
  session's completed frames drained unread, then driver ready and the
  reconciliation. CONFIG_LOST gets the full one: the rings stopped with a
  verified stop (else the buffer is not reused), restarted in the kept
  buffer, mailbox `0x23` on the rows that read the mode (a power-cycled
  ICM needs it, unlike a resume), driver ready, the host-router poll and
  the reconciliation. ACTIVE again only for the episode that began it and
  only if nothing revoked it meanwhile; a failure leaves the instance
  without access until a new outage is seen. The USB4 profile is contained
  instead and never recovers in place (record 22).

## 6. Re-enumeration: making the OS see what the tunnel changed

The experiment (`REENUM-EXPERIMENT`, 2026-10-14) measured that a driver can
do this on both primary targets with no user-mode helper, by a different
mechanism on each. This record takes its recommendation whole.

### 6.1 Windows 98 SE and ME: CONFIGMG from the NHI driver

Windows 98's PCI bus enumerator is `PCI.VXD` under CONFIGMG, and NTKERN
builds a WDM stack only for devnodes it is the loader of. A WDM driver placed
on the tunnel bridge - as an `UpperFilters` value, which is ignored, or by
forcing NTKERN as its loader, which breaks the bridge for PCI.VXD and for
`CM_Reenumerate_DevNode` alike - is the experiment's mechanism A, and it is
harmful. The rule is therefore absolute: **on Windows 98 and ME, never put
this driver, or any WDM driver, on the bridge devnode.** The undecorated INF
sections carry no line for the switch ports (section 3.3), and the INF gate
refuses one (a new rule, `CM-NO9XBRIDGE`).

Mechanism B is what the NHI driver does instead, from its thread: it calls
CONFIGMG's services directly through the VMM dynalink. The call is an
`INT 20h` with a dword `(VxD id << 16) | service`, bit 15 of the service
word set so the VMM patches the six bytes into a jump and a cdecl call
reaches the service with the caller's arguments; the thunk lives in
non-paged pool because the VMM rewrites it on first use. CONFIGMG's VxD id is
`0x0033`; its services are cdecl and return `CONFIGRET` in `EAX`, except
`Get_Version`, which returns a `WORD` in `AX` (`cm_cfgmg.h`). The
services used:

| Service | Number | Use |
|---|---|---|
| `Get_Version` | 0 | the guard: the high byte must read 4 (Windows 98's 4.0B, ME's 4.90 both answer `0x04xx`); otherwise the CONFIGMG route is disabled for the driver's life and re-enumeration falls back to section 6.4 |
| `Locate_DevNode` | 2 | the root devnode (`HTREE\ROOT\0`), a second guard: its id must read back as that string |
| `Get_Child`, `Get_Sibling` | 4, 5 | the tree walk |
| `Get_Device_ID` | 7 | match `PCI\...\BUS_bb&DEV_dd&FUNC_ff` for the tunnel bridge, and `PCI\VEN_vvvv&DEV_dddd` for a vanished child |
| `Query_Remove_SubTree`, `Remove_SubTree` | 12, 13 | a child whose function has left the bus, before the re-enumeration |
| `Reenumerate_DevNode` | 51 | the bridge, after a tunnel up or down |
| `Get_DevNode_Status` | 54 | the log, and safe eject's `DN_REMOVABLE` check before `Change_DevNode_Status` (section 7.4) |
| `Register_Device_Driver` | 14 | not called: its `CM_REGISTER_DEVICE_DRIVER_REMOVABLE` applies only to a devnode the caller drives (section 7.4) |
| `Change_DevNode_Status` | 91 | safe eject's 9x half: `DN_REMOVABLE` added to a tunnelled device's devnode, never to a tunnel port's (section 7.4) |

The experiment took these numbers from memory of the Windows 95/98 DDK's
CONFIGMG service table and confirmed each at run time (`Get_Version` returned
`040Bh`; the root id read back; `Get_DevNode_Status` of the NVMe agreed with
`CM_Get_DevNode_Status` from user mode). This record does not ship numbers
taken from memory: task `39-0.3` fetches the Windows 98 DDK, records its
name, version and SHA-256 in `docs/references/README.md` under the rule that
the artifact is never tracked, reads the service table from its
`CONFIGMG.H` (`Begin_Service_Table` order), and writes the numbers into
`cm_cfgmg.h` with that citation. The run-time guards stay in the shipping
driver regardless, because a wrong number here is a call into the wrong
VMM service.

Numeric values only are taken from the DDK header; no DDK code
or macro body is copied, and `legal-provenance.md` gets a row saying so.

What the driver does on a tunnel change on 98 (section 6.5 says when):

1. `Locate_DevNode` the root, walk to the bridge devnode whose device id's
   instance part matches the tunnel bridge's bus, device and function
   (section 6.3).
2. Tunnel down: for each child of the bridge whose `PCI\VEN_&DEV_` no longer
   answers on the secondary bus, `Query_Remove_SubTree` then `Remove_SubTree`
   with `UI_NOT_OK`; the experiment saw both return `CR_SUCCESS` for an NVMe
   with a mounted volume and the volume go away cleanly ("F:\ is not
   accessible"), with no reboot, hang or prompt.
3. `Reenumerate_DevNode(bridge, 0)`. Tunnel up: the new function is
   enumerated, its BAR and IRQ assigned under the bridge's windows, its
   driver loaded (the experiment: nvme9x loaded, the volume mounted, AutoPlay
   opened it). Tunnel down: a plain re-enumerate also dropped the vanished
   child in the experiment, so step 2 is belt and braces, kept because
   `WIN98-TEST` saw a Refresh keep a stale NVMe in an earlier model build.

The Windows 98 instance id of a hot-added PCI function is not always the
`BUS_&DEV_&FUNC_` form (the experiment saw `082800`), so vanished children
are matched by vendor and device id against what still answers on the bus,
never by instance id. A bus that cannot be read completely - a failed
read, or a function answering Configuration Request Retry Status (vendor
id `0001h`, not ready yet) - takes nothing for vanished on that pass.

Reads of the secondary bus for "what still answers" go through
`HalGetBusData(PCIConfiguration, bus, slot, ...)`, which `hcd_ctl.c` already
uses on bus 0 on every target and whose Windows 98 evidence is recorded
there (`hcdPirqSlot`'s comment); never through port `0xCF8`/`0xCFC` directly,
which the experiment did and its own caveats warn against.

### 6.1.1 A bridge behind the tunnel port: bus numbers on 98 (addendum, 2026-10-16)

**What was seen.** On the owner's ThinkPad P14s Gen 1 (98 SE, Assist off,
pre-boot on, security level 1) the ORICO ASM2464 USB4 enclosure was
approved and tunnelled after a hot plug, and its PCIe switch upstream port
(`PCI\VEN_1B21&DEV_2463`, instance `000800E400`, "PCI standard
PCI-to-PCI bridge") came up with Code 29 (`CM_PROB_HARDWARE_DISABLED`,
status `0x180066A4`, not started, an empty `Allocation` in
`HKEY_DYN_DATA\Config Manager\Enum`); the downstream port and the NVMe
behind it never appeared. The same enclosure present at power-on worked:
both ASMedia bridges started and the NVMe was found. After a Safely Remove
from XHCITR, an unplug and a re-plug, Code 29 again; Device Manager's
Refresh did not change it, and a Remove of the devnode and a Refresh gave
a fresh devnode, a wizard, a restart request and Code 29 again. The tunnel
port 04:01.0 kept the same allocation throughout (memory
`D4100000h`-`E00FFFFFh`, prefetchable `A0000000h`-`BBFFFFFFh` and its
bus-number resource, type `0043h`, `ResType_PCI_Bus_Number` =
`PCI_DEVICE_ID`, VMM.H line 197); its BIOS-programmed range is
secondary `06h`, subordinate `2Ah` (usb4-emu's DOS captures), and the
chain needs `06h`-`08h`.

**Why.** At POST the BIOS numbers the switch (upstream port 06/07/08,
downstream port 07/08/08, the NVMe on 08) and gives it windows, and
PCI.VXD takes what it finds. A bridge that arrives later comes out of
reset with secondary and subordinate bus 0, so nothing behind it is
reachable, and PCI.VXD does not number it usefully: in the emulator (98
SE, ACPI-mode image, a fresh two-level bridge chain hot-added behind the
tunnel bridge, read before any restart) PCI.VXD gave the first bridge
secondary = subordinate (no room for the next level) and asked for a
restart; a second pass widened it and numbered the second bridge, put the
first bridge's window outside the tunnel bridge's window, and asked for a
restart again; the NVMe got no BAR. On the APM-mode P14s the outcome is
Code 29. A restart works because the BIOS numbers the chain at POST. The
emulator's earlier NVMe legs passed because its i440FX model puts the NVMe
directly on the tunnel bridge's secondary bus, with no bridge between.

**What the driver does.** Before each `Reenumerate_DevNode` of a member
(section 6.1, step 3), the 9x worker sets up the bridges below it in the
way the BIOS does at POST, with placements of its own (`tbt_brnum.c`), on these conditions: the request is
still current and healthy (no newer post for the member, no
`CONTROLLER_UNAVAILABLE` since: checked again before every write), and
the member's devnode has no child in CONFIGMG's tree, so a subtree
PCI.VXD still holds (a removal vetoed, a re-plug before the removal ran)
is never renumbered under it.

1. The member's own bus-number register must read a secondary above its
   bus and a subordinate at or above that; otherwise nothing is done.
2. A survey of the member's secondary bus, writing nothing: no bridge
   there, nothing to do (the endpoint directly behind the port, the
   emulator's case); any bridge with a nonzero secondary or subordinate
   (present at power-on, numbered by the BIOS, or numbered by an earlier
   pass), nothing is done either. A function answering Configuration
   Request Retry Status (vendor id `0001h`) is not ready: the whole
   attempt is undone and made again 100 ms later; so is an empty
   secondary bus while the port reads link-up (the switch behind it
   still training), and, on every attempt but the last, an empty bus
   behind a bridge just numbered (a link below it still training);
   twenty attempts at most, two seconds. A bridge with an empty slot
   costs those two seconds and is then numbered as it is.
3. **Bus numbers.** Depth first from the secondary bus, each type 1
   function in reset state is given primary = its bus, secondary = the
   next free bus from the member's secondary + 1, and subordinate = the
   member's subordinate while its subtree is walked (so configuration
   cycles reach it), narrowed to the last bus of its subtree after. The
   secondary latency timer byte is written back as read.
4. **Windows.** Each memory BAR behind the chain is sized (all-ones
   written, read back, the value read first written back; only on a
   function whose Memory Space Enable is clear, else nothing more is
   done; a BAR over 1 GiB, or a 64-bit one of 4 GiB or more, refuses the
   windows). At each level the BARs of the functions on that bus take a
   region at the bottom, rounded up together to the level's largest
   alignment A and starting A-aligned, and each bridge on it a window
   the size of what its subtree needs, rounded up to the subtree's own
   alignment and at least 1 MiB (closed where nothing behind it decodes
   memory, and so is everything below a closed one). The windows are
   placed from the A-aligned top of the member's own memory window
   downward, the largest alignment first so they pack with no gap and
   never reach into the BARs' region, each subtree the same way inside
   its bridge's window; a member whose A-aligned part is smaller than
   the total gets no window at all. Each bridge's I/O and prefetchable
   windows are closed (a 64-bit prefetchable window's upper base and
   limit, `28h` and `2Ch`, written 0 with it and journalled for the undo,
   since `24h` alone does not close it), and its Memory Space and Bus
   Master Enable set. A
   member with no memory window, or one too small for the chain and the
   BARs beside it, gets the bus numbers alone.
5. **Interrupt lines**, once the windows are in place. A function's
   interrupt is provably routed only where it travels a wire whose IRQ is
   already known. The tunnel port's own Interrupt Line is known (the
   BIOS wrote it and PCI.VXD holds it), and a function below the port
   whose Interrupt Pin, swizzled up through the bridges, reaches the port
   on the port's own pin - (pin - 1 + its device + the device numbers of
   the bridges between) mod 4 equal to the port's pin - shares the port's
   wire at the port's primary side. Such a function, where its line reads
   0 or `FFh`, gets the port's line; its pin, and a bridge's Bridge
   Control, are written back as read. Any other function gets nothing,
   and keeps whatever PCI.VXD makes of it (Code 29 for want of an IRQ on
   the P14s). A function this stage cannot read (a failed read, or one
   answering Retry Status) is counted as not set up, so the stage does
   not report every line written. Considered and not used: the IRQ Windows recorded at boot
   for the same position (`HKLM\Enum`'s LogConfig is rewritten at every
   enumeration, as it was in the P14s captures, and `HKEY_DYN_DATA`
   does not survive a restart, so neither is a store a later hot plug
   can trust), and the BIOS's `$PIR` table (it would be a parser of a
   BIOS structure for a case the port's own line already decides: on the
   P14s the table is the standard swizzle, link 61, IRQ 10, the same
   answer). Bridge Control and the Command register need nothing for
   INTx: a PCI-to-PCI bridge forwards it whatever Bridge Control says,
   and Interrupt Disable (Command bit 10) resets to 0.
6. Every write is preceded by a read that the function is still the one
   journalled (its identity, a type 1 header), journalled before it is
   issued, and read back after. Bounds: six levels of bridges, sixteen
   bridges, buses inside the member's range only. A refusal after the
   first write (no room, too deep, too many, a numbered bridge below one
   in reset state, a failed or unheld write, the request gone stale)
   writes everything journalled back as it was read, deepest first, each
   only to a bridge that still reads as the one journalled, and each
   written back value read back; a window refusal puts back the windows
   and keeps the bus numbers, but a stale one puts back the bus numbers
   too, so the newer request finds the chain in reset state. The
   re-enumeration goes ahead in every case but a stale one, which the
   newer request makes.

**What the owner's ThinkPad read** (2026-10-16, a hot plug after a start
without the enclosure, each prototype a replaced `xhcicm.sys`): with the
bus numbers alone, PCI.VXD re-read both ASMedia bridges (their LogConfig
was rewritten) but left the first at Code 29 with its windows closed and
no IRQ; with the windows as well, both bridges started with
`E0000000h`-`E00FFFFFh`, inside the port's `D4100000h`-`E00FFFFFh`, and
the NVMe appeared, at Code 29 for want of an IRQ; with the interrupt
lines as well, the NVMe started on IRQ 10 with its BARs at
`E0000000h`-`E0005FFFh` and its drive letter came up. The first
prototype wrote no diagnostic; the second wrote `XhciCmBridgeDiag` with
18 dwords, the third and this record's layout with 20. The driver puts
the windows at the top of the port's window (`E0000000h`); the BIOS,
with the enclosure present at power-on, had put them at its bottom
(`D4100000h`-`D41FFFFFh`). Either is inside the port's window, which is
what PCI.VXD checks; the placement is this driver's, not a copy of the
BIOS's.

Why the windows too: in the emulator, with the bus numbers alone, PCI.VXD
found the second bridge and the NVMe behind it (it never did before), but
gave the first bridge a window at `10100000h`-`101FFFFFh`, outside the
tunnel bridge's `FCA00000h`-`FE9FFFFFh`, so nothing behind it decoded.
With a window already in place, PCI.VXD takes it as the bridge's boot
configuration, as it does the BIOS's for a chain present at power-on.
I/O BARs behind the chain get no window: an NVMe has none, and a device
that needs one is outside what this does.

**A stopped chain given back** (addendum, 2026-10-17). A Safely Remove
makes Windows stop the bridges, and PCI.VXD closes their memory windows
while their bus numbers stay. Read in the emulator (the overnight
matrix's agent M3, Stor98 `0.4.0.0`): right after the stop, before any
unplug, both bridges kept 1/2/3 and 2/3/3 with their windows at
`FFF00000-000FFFFF`, and the NVMe's BAR unmapped. Where the hardware
gives the chain back without resetting it (the emulator after an unplug
and re-plug; real hardware whose link stayed up), the survey of step 2
read it as numbered and wrote nothing, and the NVMe stayed at Code 29
until a restart. On the owner's ThinkPad an unplug reset the chain, so
the re-plug was numbered afresh and worked (`runs/run-41.md`, series g).
So a survey that reads the chain numbered is followed by a second walk
that journals it as found, writing nothing: each bridge must sit inside
the port's range, its primary its own bus, its range within its
parent's and clear of its siblings'. Only when every journalled bridge's
memory window is closed are the window and interrupt-line stages run
on it, exactly as after a numbering (step 4 onward), with the same
journal, identity re-reads, go callback and undo; the bus numbers are
never written. A chain with any window open (present at power-on, still
started) is left alone, and so is one with a function already decoding
or a bridge BAR holding an address while its Memory Space is on. A
function behind it answering Retry Status, or a bus still empty while
attempts remain, makes the attempt not ready, as for a numbering. The
outcome is logged as 12, re-opened. What this does not reach: a Device
Manager Refresh after a Safely Remove without an unplug, which is
PCI.VXD's own pass and not this driver's; there the stopped chain keeps
its closed windows until it is unplugged (which resets it on real
hardware) or the driver re-enumerates the port.

**The accessors.** Windows 98 SE's `HalGetBusData` does not answer for a
function PCI.VXD does not know yet (read in the emulator: the hot-added
bridge read as absent through it, so the first survey found nothing), and
NTKERN exports `HalSetBusDataByOffset` but no `HalSetBusData`, neither an
import the HCD carries evidence for (section 10.3's rule: `xhcicm.sys`
imports nothing `xhci98.sys` does not). So the reads and writes of this
section go through PCI.VXD's own services, `_PCI_Read_Config` and
`_PCI_Write_Config` (PCI.H lines 58 and 59; `_cdecl (BYTE bus, BYTE
devfunc, BYTE offset)` returning the dword, and `(BYTE bus, BYTE devfunc,
BYTE offset, DWORD value)`, VXDWRAPS.H lines 313-314), reached through the
VMM dynalink exactly as the CONFIGMG services of section 6.1, under
PCI.VXD's VxD id `0043h`, so no import is added. The guard is a sanity
check, not a proof: before the first use, `_PCI_Read_Config` of the
member's identity dword must equal what `HalGetBusData` read there;
otherwise the numbering is off for the worker's life, logged once. A
wrong read ordinal would already have been called by then; the numbers
are the DDK's, and the legs on 98 SE and ME are what confirm them.

**The switch.** `XhciCmBridgeNumbering` (section 8), 1 by default (the
owner, 2026-10-16); 0 turns this off, which leaves a hot-added bridge as
it came up (on the P14s, Code 29 until a restart). Windows 98 SE and ME
only: the NT route never reads it, since `pci.sys` owns a bridge's bus
numbers and windows there. No INF writes it and the installer neither
asks for it nor writes it.

**The diagnostic.** The 9x worker copies its last attempt to the NHI,
and the thread writes it as `XhciCmBridgeDiag` (`REG_BINARY`, twenty
dwords, `cm_statb.h`) in the NHI's hardware key beside `XhciCmStatus`, so
a release build's outcome can be read from a registry export without a
debugger: the outcome, what was seen, the port's bus range and window,
the first two bridges' registers as left, the interrupt lines' outcome
and counts, and why nothing was done where nothing was.

**Not done.** I/O and prefetchable windows, a CardBus bridge, a bridge
the BIOS left half-numbered, BARs above 1 GiB, a concurrent Device
Manager Refresh landing between the first and the last write (the window
is milliseconds and a Refresh only makes PCI.VXD read what is there), and
a member whose bus range or window is too small: that is the BIOS's
choice, and the user's way round it is the enclosure present at power-on
with pre-boot on. Thunderbolt BIOS Assist Mode on is no way round on 98:
read on the P14s with Assist on, the NHI is absent from Device Manager
with or without the enclosure and a plug brings up nothing.

### 6.2 Windows 2000 and later: the bridge filter

`pci.sys` owns the bridge's secondary bus and notices nothing on its own
(no SHPC driver on 2000 or XP; Windows 7 drives native PCIe hot-plug on q35
but not SHPC on i440FX, `REENUM-EXPERIMENT` section 6a of the handoff). The
one thing that makes it look is `IoInvalidateDeviceRelations(bridgePdo,
BusRelations)`, which needs the bridge's PDO, which only a driver in the
bridge's stack has. Hence the filter role of section 3.2: on a tunnel change
the NHI thread finds the filter instance registered for the tunnel bridge's
location and asks it to invalidate; `pci.sys` then enumerates the new
function (nvme2k loaded, F: mounted, no user action in the experiment) or
reports the vanished one missing (surprise removal, with Windows 2000's
"Unsafe Removal of Device" dialog on every tunnel down that was not
preceded by an eject, which the experiment saw on every cycle; section 7.4
says what safe eject changes).

The filter is installed by `xhcicmf.inf` from the package's `nt\`
directory (x86) or the flavour directory itself (x64), the device-specific route of decision D2 (section 3.3): unforced
on Windows 2000, forced on XP and 7 by the installer, which the unsigned
INF's rank requires (`39-0.4`).

The filter does no device work: PnP IRPs other than the six it watches
go down with `IoSkipCurrentIrpStackLocation`; power IRPs go down the way
NT 5.x requires of a filter, `PoStartNextPowerIrp` then
`IoSkipCurrentIrpStackLocation` and `PoCallDriver` (the experiment's
`reenum.c` `Power` routine), except a device `IRP_MN_SET_POWER` to D0 on a
member the NHI has marked **held** (section 6.3), which is passed down
with a completion routine and retained as section 6.3's resume ordering
says,
`PoStartNextPowerIrp` and the completion following its release;
`IRP_MN_DEVICE_USAGE_NOTIFICATION` sets `DO_POWER_PAGABLE` on the way down
unless a device above the filter lacks it, and clears it on the way up
where the device below lacks it (the paging-path rule for a filter); `IRP_MN_START_DEVICE` is passed down with a
completion wait and the entry published only on success;
`IRP_MN_STOP_DEVICE` and `IRP_MN_SURPRISE_REMOVAL` withdraw the entry;
`IRP_MN_REMOVE_DEVICE` withdraws it, waits for the rundown of section 3.2,
passes the IRP down, detaches and deletes the device object;
`IRP_MN_QUERY_DEVICE_RELATIONS` for `BusRelations` is passed down with a
completion wait and read on its way back on every bridge from the
filter's start, whatever its active mark and `XhciCmSafeEject` say, to
keep section 7.4's baseline and snapshot; only on an active bridge with
safe eject on does that reading also attach child filters, before the
request is completed - the list is read and never changed. Its
`AddDevice` attaches to any PDO the INF put it on; the location it
publishes is what the NHI matches.

### 6.3 Finding the tunnel ports: a set per domain, with its source

A **domain** is one NHI and the fabric behind it; a machine can have more
than one (a Tiger Lake has two NHIs, `TGL_NHI0` and `TGL_NHI1`, and a
desktop may add a discrete card). Each domain keeps a **tunnel-port set**:
the PCI bridges whose secondary bus a tunnel of that domain lands on, each
member with the **source** that put it there and its own state (its last
port reading of section 6.5, its active mark of section 3.2, its
re-enumeration count). Two mappings are kept apart, because no source
joins them:

- the **fabric mapping**, from a Thunderbolt or USB4 port to the router's
  PCIe downstream adapter, which a connection manager can read from the
  router (Linux: `usb4_switch_map_pcie_down` for USB4 routers,
  `tb_find_pcie_down` with per-generation tables for older ones) and which
  names an adapter, never a PCI bus, device and function; the ICM client
  does not read it, the firmware owning the routers;
- the **PCI mapping**, from the domain to the PCI bridges, which Linux takes
  from the platform's ACPI (`acpi.c`, `tb_acpi_add_links`, the
  `usb4-host-interface` references), and which even Linux calls hard to
  establish otherwise (`pci.c`, `nhi_pci_check_iommu`).

This driver reads no ACPI (Windows 98 in APM mode has none to read, and the
NT targets' is unread here), so the PCI mapping comes from these sources,
in this order, each recorded per member as its source:

1. **The override**, `XhciCmTunnelBridge` (section 8), read from the NHI's
   own device key, so per domain: a list of `bb:dd.f` entries. When
   present it is the whole set; a listed address where no function
   answers, that is not a PCI-to-PCI bridge, or that repeats an entry is
   logged and dropped, and an address another domain's override also
   names is ambiguous and dropped from both. A listed address that cannot
   be read is never dropped: it makes the result **INCOMPLETE** (below).
   On Windows 98 the
   override is all a port needs, since CONFIGMG reaches any devnode. On NT
   a port is re-enumerated and its children marked only through the
   filter of section 3.2, which attaches only where `xhcicmf.inf` matched
   the port's id: an override naming a port whose id the INF does not list
   gives that port no NT re-enumeration and no safe eject, logged, unless
   the user adds a model line for the id to a copy of `xhcicmf.inf` and
   installs it, the procedure the README spells out beside `XhciCmMode`
   2's.
2. **The discrete switch** (FR, AR, TR and Maple Ridge families). The
   NHI's parent is the header-type-1 function on some bus whose secondary
   bus number equals the NHI's bus; a member is a sibling of that parent
   (same bus) that is a header-type-1 function, is not the NHI's parent,
   carries one of section 3.3's port ids of the same family as the NHI,
   and carries a PCI Express capability whose slot capabilities have
   Hot-Plug Capable set (real Alpine Ridge [obs]: "hot-plug and surprise
   capable slot"). The id test is what makes the set the controller's own
   switch and nothing else on the bus. Source: "switch".
3. **The integrated root ports** (ICL family, and the integrated hosts of
   Phase 40). The NHI is on a root bus with no parent bridge; a member is
   a header-type-1 function on that bus whose id is one of section 3.3's
   integrated tunnel-port ids for the same processor family as the NHI.
   Never any other root port, however hot-plug capable: a machine's other
   root ports (a card reader, a slot) are not tunnel ports. Source:
   "listed root port". Each port belongs to one domain or to none, and
   that is decided from the hardware, not from which driver instances
   happen to be running: the NHI reads every function of its root bus
   (`HalGetBusData`) and counts the NHIs of its own family present,
   started, disabled or failed alike, by the family's NHI id list. That
   list must hold this NHI's own id: a list that does not proves nothing
   about the bus, and the source then takes nothing. With one NHI of the
   family, and it this one, its set is every listed port present. With two (Ice Lake to Meteor Lake carry two NHIs,
   Linux's `*_NHI0` and `*_NHI1`) and no source saying which ports are
   whose - Linux takes that from ACPI and checks that a port references
   the particular NHI (`acpi.c`, `tb_acpi_add_link`) - the listed ports
   are **ambiguous** and stay out of both sets: inactive, never
   re-enumerated or marked by this driver, the candidates logged per
   domain, until the override assigns them.
4. **The emulator**: a sibling of the NHI's parent, or of the NHI on bus 0,
   that carries an SHPC capability (id `0x0C`; `usb4-emu`'s
   `usb4-tb3-tunnel-bridge`, and its `usb4-generic` layout with the host and
   the bridge siblings on bus 0). Source: "SHPC". Finding the capability
   needs reads alone; reading a slot's presence (section 6.5) needs a
   byte write of the capability's DWORD Select into the bridge's
   configuration space, and is made only where the driver half has
   qualified a byte writer for a bridge's configuration space on that
   target (`HalSetBusDataByOffset` writes to a bridge header carry
   restrictions Microsoft documents, and Windows 98's HAL exports are
   fewer). Each such read puts back the DWORD Select value it found, and
   one that cannot is a failed reading; with no qualified
   writer an SHPC member gives no presence reading.

**Exclusive ownership.** Before any member is activated, each domain
publishes its claimed ports, with their sources, in the registration
list under its lock; a port claimed by two domains by any combination of
sources (override against override, override against discovery,
discovery against discovery) is ambiguous for both, inactive and logged,
until only one domain claims it across all sources - for example an
override on the other domain whose list replaces its discovered set and
leaves the port out. Ownership, the active
mark, safe eject and the resume hold therefore always have exactly one
domain behind them.

With no override and an empty set, the driver logs the bridges it saw and
runs with re-enumeration disabled (section 6.4); it never falls back to an
arbitrary bridge.

**INCOMPLETE.** A discovery that could not read what it had to ends
**INCOMPLETE with an empty set**, never with a set that might lack a
port, and is tried again. With the override, that is an entry whose
identity or header could not be read (a failed access, or a present
function whose header reads all ones); such an entry is never treated as
dropped. An entry that is a bridge is taken whatever its capability list
reads, since only its port readings depend on the capabilities, and a
failed reading there is an error (section 6.5), never "absent"; and an
override's outcome stands in place of the automatic discovery's, so an
INCOMPLETE automatic walk beside a complete override is logged only.
Without the override, it is any function on a bus the walk searches whose
configuration access failed, whose header reads all ones, or whose
capability list could not be walked. Until a discovery completes the
domain runs as with an empty set. The other outcomes beside a set are: the override naming no bridge,
no candidate at all, hot-plug bridges seen but none a source names (the
override is the way), listed root ports ambiguous between NHIs, and more
members than the set has room for (empty, logged).

Under the USB4 profile a fifth source, the bridges the host router names,
adds members once these outcomes are known, and a learned correspondence
binds a port to a member; design record 22 section 7.6 says when a named
bridge may join (never after the override's own failure, an INCOMPLETE
discovery or one with too many members, and never the NHI or its parent)
and that a named bridge that cannot be read voids the set as INCOMPLETE.

The layouts the walk and the readings decode are the public
specifications', cited by section in `tbt_topo.h`: the PCI Local Bus
Specification 3.0 sections 6.1 (the common header) and 6.7 (the
capabilities list), the PCI-to-PCI Bridge Architecture Specification 1.2
section 3.2 (the type 1 header's bus numbers), the PCI Express Base
Specification 4.0 section 7.5.3 (the PCI Express Capability: Link
Capabilities, Link Status, Slot Capabilities and Slot Status) and the PCI
Standard Hot-Plug Controller and Subsystem Specification 1.0 section 4.2
(the SHPC capability). None of the four is kept in `docs/references/`, and
`docs/references/README.md` has no row for them: they are cited by
section only.

The walk is a pure routine `tbt_topo.c` over caller-supplied config
accessors (`HalGetBusData` reads of PCI headers in the driver, from the
NHI's own location by `IoGetDeviceProperty` `DevicePropertyBusNumber` and
`DevicePropertyAddress`), to be host-tested against the two captured topologies
- the P14s (`03:00.0` upstream, `04:00.0` to the NHI on bus 5, `04:01.0`
the tunnel, `04:02.0` the xHCI; `ICM-PROTOCOL` section 1 and the Windows 98
`PCIDUMP.TXT` matrix, whose `OFF_OFF` and `OFF_ON` captures are reproduced
unedited in `test\tbt_topo_p14s.h`, `legal-provenance.md` section 4) and the emulator's i440FX stand-in (`WIN98-TEST`, "P14s
BIOS matrix": root bridge at `00:06.0`, switch ports `01:01.0`, `02:00.0`,
`02:01.0`, `02:02.0`) - and against synthetic ones: a two-port discrete
switch, an integrated layout with listed and unlisted hot-plug root ports,
two NHIs of one family with only the first started and with the second
disabled (both ambiguous either way), an override against a discovered
claim, a foreign hot-plug bridge beside the switch, and the override.

**A set of more than one.** Which member a device's tunnel uses is not
read (the fabric mapping above is the firmware's), so every tunnel change
acts on the whole set. The tunnel-up poll of section 6.5 takes each
member's reading when it starts and ends only when a port actually comes
up - a member that read absent, mixed or gone then reads present - or
when every member reads present, or after 2 s; a member already up when
the poll started never ends it, so an existing tunnel on another port
does not cut the settling window short. The tunnel-down poll ends only
when every member reads down (or down and one gone): the departed
device's port is not known, so the reading must be affirmative on all of
them; with another device still tunnelled it runs to its 2 s and the
reading counts as not clear. Every member is re-enumerated either way -
an invalidation or a `Reenumerate_DevNode` of a bus that did not change
is harmless - and the `nvme98` statement, which needs the one port its
condition is read on, is not sent while the set has more than one member
(section 7.2).

Four of the choices above were taken by the orchestrator on 2026-10-15
with task `39-A.5`'s pure half, for the owner to confirm: the INCOMPLETE
outcome (an unread function or override entry gives an empty set, never a
partial one); source 3 taking nothing unless the family's NHI list holds
this NHI's own id; the set polls (up only on a port that actually came
up, down only when every member reads down); and the SHPC presence read
only with a qualified byte writer.

**Resume ordering.** Linux uses the ACPI links above to resume the tunnel
ports only after the NHI, so the tunnels are back before the devices
behind them are touched. Nothing gives that order here, so on NT the
driver makes one:

- At its own system suspend (section 5.6) the NHI marks every active
  member of its set **held**, under the list's lock, before it sends
  anything to the firmware. The mark is set before either device can
  resume, so it holds whichever of the NHI and the port resumes first.
- A device `IRP_MN_SET_POWER` to D0 that reaches a held member's filter
  is passed down with a completion routine and **retained** there, after
  the bus driver has finished with it, so the devices below the port are
  not powered; nothing waits in a dispatch routine.
- The NHI thread clears the mark and releases the IRP when its resume has
  either **succeeded** (section 5.6's resume sequence and the
  reconciliation of section 5.3 done, the tunnels re-announced) or
  **failed** (any step's bound exhausted, the failure after activation
  of section 1.4.2; the host-router poll's exhaustion is not one, section
  5.1 step 9). Failure is not taken for restoration: the thread releases
  the IRP and at once asks the filter for an invalidation, so `pci.sys`
  reports the functions that did not come back as missing. An
  invalidation only requests later PnP processing and is not a barrier,
  so the drivers below may resume against an absent tunnel before their
  removal - the failure path limits that window and does not close it.
- The hold ends at the earlier of the NHI's resume result and a fixed
  5-minute cap, the same on every NT target and under either profile,
  below the power manager's own watchdog; the client bounds its own
  resume at 270 s (`TBT_ICM_RESUME_BUDGET_MS`: bring-up, retries,
  reconciliation and the announcements' drain, each wait clipped to what
  is left), so its result comes inside the cap. The client's computed
  bound (the sum of its steps' timeouts and tries for its row, plus 10 s)
  is logged only. At the cap the filter releases the IRP itself and takes
  the failure path; it never treats the expiry as success.
- An NHI that does not resume at all (removed, or refused at resume)
  takes the failure path for every held member at its removal.

Because of that window, **system suspend with a tunnelled device attached
is unsupported in this release on every target**, and the release notes
say so: the hold orders the successful resume, and nothing yet orders a
failed one. Leg `39-V.2` reads, where the guest offers S3, a resume that
succeeds, one that fails (the firmware not answering `driver ready`), an
unplug during sleep, and the watchdog's expiry. On Windows 98 there is no
such hook at all; Windows 98 on the APM machines this project runs rarely
reaches S3.

The failed resume is a recorded exception to the coverage plan's rule
that tunnels are restored before dependent PCI devices resume: the held D0
is released, then the invalidation asked for, because holding a bridge's
D0 for ever would hang the system's resume. Design record 22 records it
as its decision U15, for both profiles, and this record carries the same
decision (an orchestrator decision of 2026-10-15, for the owner to
confirm).

### 6.4 When re-enumeration cannot be done

The NHI driver still runs the ICM client and approves devices, so a device
tunnelled this way still works after a Device Manager Refresh on 98 or a
"Scan for hardware changes" on NT; the driver logs once, with the reason,
and counts it. The CFGMGR32 user-mode route (`cmreenum.exe` in the
experiment) is a fallback this record does not ship.

### 6.5 Ordering: when the sequences run

Two registers of the tunnel port's PCI Express capability are read, and
they are not the same register: **Link Status**
(capability offset `0x12`, bit 13 Data Link Layer Link Active, valid only
where Link Capabilities bit 20, Data Link Layer Link Active Reporting
Capable, is set) and **Slot Status** (offset `0x1A`, bit 6 Presence Detect
State, bit 8 Data Link Layer State Changed), valid only where the
capability's Slot Implemented bit is set. Both through `HalGetBusData`
reads of the port's header; the pure `tbt_topo.c` decodes them over the
same accessors and host vectors cover a port without either capability bit.
The emulator's i440FX tunnel bridge has no PCI Express capability; there
the SHPC slot's presence bit is the only reading.

- **Tunnel up**: after a successful `approve` (section 5.4), or after a
  `device connected` with `approved` = 1 that was not in the table (a device
  tunnelled before the driver bound: pre-boot, an earlier driver instance, a
  resume). Before re-enumerating, poll the tunnel port until Link Status
  reads Link Active and Slot Status reads Presence Detect (real hardware), or
  the SHPC slot reads present (the emulator), or 2 s pass. The firmware
  brings the link up about 0.3 s before it answers `approve` [obs] section
  4.3, so the poll normally ends at once; it exists for the emulator's
  settle timeout on a re-plug (`ARCHITECTURE` section 4.4) and for real
  hardware's link training.
- **Tunnel down**: after a `device disconnected`, or after the reconciliation
  of section 5.3 finds an entry gone. The link is already down when the event
  arrives [obs] section 6.3. The driver then reads the tunnel port: Link
  Status Link Active clear and Presence Detect clear on real hardware, the
  SHPC presence bit clear on the emulator, polled up to 2 s. Section 7's
  notification, where wired, comes after that reading and before the
  removal and re-enumeration of sections 6.1 or 6.2.
- Both run on the connection-manager thread, serialised with the ICM
  traffic. The tunnel-up poll services RX frames while it waits, and a
  `device disconnected` for the device it is waiting on abandons the poll:
  the tunnel-down sequence then runs in its place and sees the bus empty
  (section 7.2's budget depends on this).

## 7. The contract with Stor98 (formerly `nvme98`)

The NVMe driver this section was written against as `nvme98` was renamed
Stor98 (`stor98`, its file, service and key name) before its first
release, `0.3.0.0` (2026-10-16); its `docs/RENAME-STOR98.md` is the
change list, and section 7.2 follows it. Its maintainer decisions (MD-1 to
MD-6) and gates (G3, Q-9x) keep their names, and sections 7.1 and 7.3 cite
its plan as it was read under the former name.

`nvme98` (`D:\OneDrive\git-projects\nvme98`, `docs/PLAN.md` section 0.10 and
gate G3; maintainer decision MD-2 of 2026-10-14) accepts, under assumption
A1, that a tunnel's PCIe link going down resets the endpoint, and places two
obligations on this connection manager:

1. A tunnel is re-established only to an endpoint whose link went down since
   the last tunnel, so that it was reset.
2. The link-down statement is sent only after completed quiescence: the
   tunnel's PCIe path closed and the endpoint's link-down reset completed.

### 7.1 How the ICM client meets them

Under the firmware connection manager this driver never programs a path and
never tears one down by choice: the firmware builds the tunnel on `approve`
and drops it on unplug, and the only teardown command this driver has is
mailbox `0x06` (every PCIe tunnel at once), which it never sends. Obligation
1 therefore holds by construction: every tunnel up follows either a fresh
`approve` of a freshly announced device (whose previous link, if any, went
down at its unplug) or a re-announcement of a tunnel that never went down.
Obligation 2 is the harder one, and the first draft's answer was narrowed
twice. An empty secondary-bus read is
not evidence of anything: config reads go all-ones at the emulator's stage
1, while endpoint DMA can run until stage 2 ends, and `nvme98`'s PLAN says
the same of all-ones. The **candidate prerequisite** this driver reads
before any statement is **the tunnel downstream port's Data Link Layer
Link Active bit reading 0** (section 6.5): a link in DL_Down carries no
transaction in either direction, and A1 says the component below it is
reset (PCIe Base 2.9.1, as `nvme98`'s PLAN cites it). A prerequisite is not
the boundary. What `nvme98` requires is a **completed** reset or drain at
the endpoint, and on real hardware the upstream link reading down says
nothing about when that reset has finished propagating - through an
enclosure's internal switch in particular, which is the case MD-2 names
for its own verification. So the statement is withheld everywhere until
two qualifications exist, and the release notes say which state the pair
is in:

- **On the emulator**, gate G3's ordering check: the marker of a statement
  sent on the condition is later than the stage-2 endpoint reset/quiesce
  event in the host trace. The i440FX bridge has no Link Active bit, but
  `ARCHITECTURE` section 4.4 does document a candidate on it: a **surprise**
  removal marks the SHPC slot empty in stage 3, after stage 2 has ended, so
  the presence bit reading clear after a `device disconnected` is a
  documented later-than-stage-2 signal for that path (an orderly eject
  raises a request first and is not that signal). That is the condition
  `39-A.7` qualifies on i440FX; a q35 topology with a real Link Active bit
  is the alternative.
- **On hardware, the boundary is unresolved by this record.** Once the
  link is down nothing below it can be read, so no register of the
  endpoint can say when its reset completed during the disconnected
  interval; reading CC.EN and RDY after a reconnection corroborates A1
  after the fact and establishes nothing about the interval. An observation method that is available while the tunnel
  is down, or a bound agreed with `nvme98` on other grounds, is a question
  for `nvme98`'s own hardware verification of MD-2 (the P14s with an NVMe
  enclosure and with one that has an internal switch) and the owner, not
  one this driver can answer alone. Until it is answered, **no hardware
  statement is sent**, on any OS.

The two conditions apply separately: on the emulator, no statement is
sent on an OS until that OS's ordering qualification exists; on hardware,
no statement is sent until that and the hardware boundary are both
settled. Wherever a statement is not sent, `nvme98`'s gone deadline (its
decision MD-1) is the fallback, as its PLAN already provides.

### 7.2 The channel: Stor98's link-state table (decision D6, as adopted)

`nvme98` chose the interface on 2026-10-15 (its decision MD-6, gate G3):
**a link-state table the connection manager owns**. Stor98 `0.3.0.0`
defines it in its `include/stor98cm.h`, which this tree carries unchanged
as `src/cm/stor98cm.h` (BSD-3-Clause; `legal-provenance.md` section 2a),
in three variants: interface 1, a diagnostic `IOCTL_SCSI_MINIPORT`
message (signature `STOR98CM`); interface 2, the table on NT; interface
3, the same table on Windows 98 SE and ME with a registry locator, a
current-boot proof and a removal transaction, its rules numbered R1 to
R12. The rename gave every name and signature a new value and set every
version field to 4 (the table's, the 9x locator's and the IOCTL
message's), with no compatibility with the former names in either
direction: a connection manager built against `nvme98cm.h` and a Stor98
receiver, or the reverse, have no channel (R2), and the gone deadline
(MD-1) applies. The first
draft's `IOCTL_SCSI_MINIPORT` route was measured by G3 and rejected
as the carrier: the port starts it only when the addressed unit has a free
slot, so under a saturated queue it arrived after the driver's gone deadline,
and once the disk is gone it fails. The discovery worker, the recipient
cache and the per-connection discovery context this section proposed go
with it; nothing in this driver looks for Stor98. `src/cm/cm_nvlink.c`
(the pure entry protocol, the locator's bytes and the proof's query
handler, host-tested) and `cm_nvlinkos.c` implement this side.

- **The table.** One `STOR98LS_TABLE` (signature `S98L`, 4C383953h,
  version 4) in non-paged pool, allocated at
  `DriverEntry` and **never freed**, not even at unload, so a pointer
  Stor98 took never dangles. Eight entries, one per tunnelled NVMe
  function, keyed by bus (the tunnel bridge's secondary bus) and
  `PCI_SLOT_NUMBER`; each carries a `LinkGeneration`, `Flags` (`IN_USE`,
  `UP`, `DOWN_QUIESCED`, and on 9x `REMOVE_PENDING`), `AckGeneration` and
  on 9x `RemoveAck` (the only fields Stor98 writes), `RemoveSequence` (9x)
  and a `Sequence` that is odd while this driver writes the entry; the
  header's `Cookie` is the 9x per-boot cookie (0 on NT). One table per
  boot (`stor98cm.h`, "CM lifetime (NT)"): a reload of this driver in the
  same boot finds the callback object the first load created (Stor98 never
  creates it) and publishes nothing - no second table, no callback, no
  `UP` - so a Stor98 instance bound before the reload keeps the old table,
  frozen, and gets no statement, and one that binds after it finds no
  channel; the gone deadline (MD-1) covers both.
- **NT rendezvous** (interface 2). At `DriverEntry` the callback object
  `\Callback\Stor98LinkState` is created (`ExCreateCallback`,
  `OBJ_PERMANENT`) with one callback that answers `STOR98LS_QUERY` with the
  table's address; Stor98 opens it in `HwFindAdapter`, once. The unload
  unregisters the callback and keeps the table.
- **Windows 98 SE and ME rendezvous** (interface 3, R3; `nvme98`'s MD-5:
  never a pointer trusted across boots). At the first tunnel action of the
  load (no registry service runs while the NHI's own start is pending), and
  before any `UP` is published on 9x, the driver fills the table's
  `Cookie` (nonzero, per boot) and then, in this order
  (`CmNvlinkPublish9x`, host-tested):
  1. **residency** (R4): on 98 SE and ME `DriverEntry` leaves the driver
     with no `DriverUnload`, so the image that answers the proof and
     writes the table stays loaded for the rest of the boot whatever its
     PnP devices do (until `3.0.0.0` the start of a CONFIGMG worker,
     section 6.1, did the same later); the publication checks it;
  2. **the current-boot proof** (R3a): it opens
     `HKEY_DYN_DATA\Stor98LinkState`; if the key exists another publisher
     owns it, and if the open fails with anything but "not found"
     (`ERROR_FILE_NOT_FOUND`) its absence is not shown, and either way
     nothing is published; otherwise it creates that
     key with `_RegCreateDynKey`, this image as its provider, with two
     values answered from this driver's own memory by one query handler in
     all four of the provider's slots (`PROVIDER_KEEPS_VALUE_LENGTH`, so
     the handler sets each value's length): `BootCookie` (`REG_DWORD`, the
     cookie) and `TableLinear` (`REG_BINARY`, 12 bytes: the table's linear
     address, its size 272 and the cookie). The key's handle stays open
     for the boot: with it closed at once, 98 SE kept the key's name with
     no values and the provider was never called (run-41.md);
  3. **the locator** (R3b), only after the proof: `REG_BINARY` value
     `LinkState` of
     `HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\stor98`, 20
     bytes of little-endian `ULONG`s - `S98L`, version 4, the table's
     linear address, its size and the cookie.

  The services are the VMM's `_RegOpenKey`, `_RegCloseKey`, `_RegCreateKey`,
  `_RegDeleteKey`, `_RegSetValueEx` and `_RegCreateDynKey` (148h, 149h,
  14Ah, 14Bh, 152h and 176h, their places in the Windows 98 DDK's `VMM.H`
  table, checked against `VMM.INC`; the provider and value structures are
  `VMMREG.H`'s), reached by the INT 20h dynalink of section 6.1. The
  locator persists across boots and is only a locator; the proof exists
  only while this image is loaded in this boot, so Stor98 accepts the
  table only when the proof's cookie and address equal the locator's and
  the table's cookie matches (R5). The key is deleted only at the unload,
  before the image that answers it goes (R4); after step 1 no unload
  comes, so in practice the proof lasts until the restart, and a restart
  starts with no `HKEY_DYN_DATA` key at all, so the next boot publishes a
  new table with a new cookie and the old locator names nothing Stor98
  accepts. Any step that fails leaves that load with no channel: the
  table stays empty, no `UP` is published there, nothing is awaited and
  no `REMOVE_PENDING` request is made - which is the whole of 9x's
  behaviour before this channel existed. The outcome is logged
  (`cm.nvlink.9x.record`: bit 31 published, else the step in bits 18:16
  and its code in 15:0). The VMM refuses a static key under
  `HKEY_DYN_DATA` (Stor98's gate Q-9x), which is why the proof is a
  dynamic key. Each read of the proof is counted, as NT counts its
  callback's queries (`cm.nvlink.9x.proof-read` logs each new count):
  Stor98 reads it once per load, in `HwFindAdapter`, so until it has been
  read no instance holds the table, and neither the acknowledgement wait
  nor the removal request below waits for one.

  R4 also asks the connection manager to veto `IRP_MN_QUERY_REMOVE` of its
  own devices. This driver does not: a disabled or removed Thunderbolt or
  USB4 controller stops its domain, as on every other target, and the
  update and uninstall of design record 23's tool go through that removal. The
  image stays loaded all the same, so the proof is still answered and the
  table stays valid; what is lost is that domain's further statements and
  requests, and Stor98's own gone deadline (MD-1) covers its drives - the
  case R4 itself describes for a connection manager that stops writing.

  What this gives Stor98 on 98 SE and ME is the table and its `UP`
  entries; it reads the proof and the locator once per load, in
  `HwFindAdapter` (R3). Where the tunnel's function answers only after
  `Reenumerate_DevNode` - a drive plugged in after Windows started, on 98
  SE and ME alike (run-41.md) - its `UP` is published about 200 ms after
  Windows has started Stor98 on it, so a Stor98 that bound its entry only
  in `HwFindAdapter` would find none, or a stale one; Stor98 `0.4.0.0`
  looks its entry up after its start as well, taking only an `UP` above a
  generation floor, so a hot-plugged drive gets the channel too (read in
  the joint legs with that release). A drive present when Windows starts
  is enumerated before this driver publishes, and its first load runs
  without the channel; a later one, after a re-plug, finds it. It does
  not give Stor98 a statement or a removal request in `3.0.0.0`: no
  link-down statement is made there ("The statement" below: no reading on
  98 SE and ME), and an eject made with the install or tray tool goes to
  Windows directly (record 24 section 7.1), so the worker's handshake
  below is not reached by it. Stor98 relies on its own gone deadline
  (about 5 s) and on Windows' removal, as before; roadmap-usb4.md 42-0.7
  and 42-A.13 (formerly NV9X.3) are the work that makes both.

- **UP before the enumeration.** For each active member, before the call
  that exposes its secondary bus (the invalidation of section 6.2,
  `Reenumerate_DevNode` of section 6.1), the driver scans that bus and
  publishes `UP` for each function of class 01h subclass 08h that
  answers, with `LinkGeneration` + 1 for a new tunnel only (task 42-A.6;
  `stor98cm.h` as Stor98 revised it on 2026-10-18, entry protocol step 1
  and R8b: UP published again for a tunnel that stayed up keeps its
  generation). An entry not `UP` always gets the new generation. An entry
  already `UP` keeps it - on a tunnel down's re-enumeration, a RESYNC, a
  baseline's or an owed re-enumeration's exposure, and on every member but
  the one a tunnel up names - unless the exposure itself names a tunnel
  other than the one the entry's `UP` was last published for: under the
  USB4 profile (record 22 section 10) the UP hook and the seam name the
  port's tunnel (the USB4 port and its connection generation) to the
  member the port corresponds to, and nothing else names one. A tunnel so
  named but not published at once - the secondary bus unread, as on 98 SE
  and ME right after a resume, before the bridges are numbered again - is
  kept as the bridge's label for what later exposures publish, and no
  later exposure that names none advances an entry already `UP` for it
  (task 42-A.25): by then the endpoint has been enumerated, and on Windows
  ME a Stor98 instance bound since before a Stand by dropped its disk when
  a second enclosure's plug advanced its entry so (record 22 section 10).
  Under the firmware
  connection manager no action names its member, so no tunnel is named
  and no entry already `UP` is advanced: a member whose function is newly
  found gets its generation, and the others stay as they are. (Until
  42-A.6 every exposure after a tunnel up or a RESYNC advanced every
  member's entry: U.11's two NVMe drives under the USB4 profile,
  `runs/run-41.md`, where a second port's tunnel up re-published the
  first drive's entry at a new generation and a bound Stor98 on 98 SE
  dropped that disk.) Nor does the generation change while a 9x removal
  request (`REMOVE_PENDING`, below) is outstanding on an entry (interface
  3, R9a): it is left as it is until the request is withdrawn. Before
  every exposure, fresh or not (it follows the acknowledgement wait), every entry behind
  that bridge that carries a statement is retired (`IN_USE` alone, the
  generation kept), found by the bus and by the bridge it was last published
  or marked for, so a Stor98 instance that binds after the exposure -
  to a function the scan could not see, or one that came back between the
  scan and the enumeration - never records a generation carrying an old
  statement. (Until task 42-A.12 the retirement also advanced the
  generation, so a re-plug after a statement cost two increments; R8b
  allows one per tunnel and none for anything else, and Stor98's 9x late
  lookup takes a generation read not `UP` as its floor and binds only an
  `UP` above it, which the re-plug's one increment is.) On Windows 98 SE and ME no `UP` is published while the
  proof and locator are not published. There the tunnel's bus did not answer before
  `Reenumerate_DevNode` (98 SE, run-39): a fresh exposure that finds it
  silent leaves the bus to be scanned again on each pass for 10 s, and the
  scan that finds the function publishes the new generation the exposure
  owed - after the enumeration, which the retirement before it makes
  harmless. An entry, once
  given to a bus and slot, is never given to another: a Stor98 instance
  may still hold it. Where the SHPC slot reads a card present and nothing
  answers yet, the scan waits up to 1 s for it. Only functions on the
  secondary bus itself are seen: an NVMe function behind an enclosure's
  own PCIe switch, or one that has not answered by the end of the scan,
  gets no entry, so no statement, and the gone deadline (MD-1) stands for
  it. The bridges published are the domain's exclusive members, whether or
  not the NT filter has taken its baseline yet (the baseline's own
  `BusRelations` answer may report the endpoint, so the driver publishes
  before it asks for one). A device tunnelled before this driver started -
  before boot, or before an install - was enumerated by the operating
  system before any `UP` could exist; it gets its entry at the domain's
  first exposure, and a Stor98 instance that found none at
  `HwFindAdapter` has no recorded generation for it (the statement then
  depends on Stor98 looking the entry up later; MD-1 otherwise).
- **The statement** is the entry's move to `DOWN_QUIESCED`, made on section
  7.1's condition alone. The seam's tunnel-down poll reads nothing on the
  emulator's SHPC bridge (no writer for its DWORD Select, run-39 39-A.5),
  so the channel reads the member itself: tbt_topo.c's port reading over
  `HalGetBusData`, with the select byte written by `IRP_MN_WRITE_CONFIG` to
  the bridge's PDO through its filter's registration (a capability offset,
  never the header). Windows 98 SE and ME have no such writer, so no SHPC
  reading and no statement there. The condition: a TUNNEL_DOWN of the ICM
  profile, the domain's set has exactly one member, it is an SHPC bridge,
  and its slot reads empty within 2 s; the member is the emulator's bridge,
  admitted explicitly: it, or the bus-0 bridge whose bus range holds it,
  carries QEMU's vendor id 1B36h (the emulator's root bridge on i440FX,
  its tunnel bridge on the generic profile), and it has an SHPC capability
  and no PCI Express capability; and the operating system and architecture
  are a pair the ordering is qualified on: Windows 2000 (x86) and XP x64
  (amd64), where gate G3 passed and where nothing but this driver removes
  the device - not XP x86 (G3 not run there yet), not Vista or 7 (native
  hot plug races the connection manager; G3 not run); 98 SE and ME were
  added on 2026-10-22, each on its own reading (the 9x half below, part
  2). This qualification list is an orchestrator decision of
  2026-10-15 for the owner to confirm. Otherwise the entries on that member
  whose function no longer answers move to `IN_USE` alone (`UP` cleared,
  no `DOWN_QUIESCED`), which Stor98 ignores, and its gone deadline (MD-1)
  stands; a function still answering keeps its `UP`.
- **The acknowledgement before the removal.** Before the removal call of a
  tunnel down or a RESYNC for a member, every entry this domain marked
  behind that member reads `AckGeneration` = `LinkGeneration`, or the bound
  from the mark has passed. Since task 42-A.12 that is a pending state per
  entry, stepped on the thread's passes and at every servicing point of a
  wait inside one (the sleeps of the core's and the seam's waits), and a
  gate per member that holds that member's removal, exposure and resume
  release alone (part 3 of "The 9x half" below); until then the thread
  itself waited for every entry of the domain. The bound: 5 s on Windows 2000 and later (the gone deadline in `nvme98`'s
  test build; the header gives no number), and 3 s on Windows 98 SE and
  ME, inside interface 3's R12 S4 bounds: at most Stor98's 9x gone
  deadline (`STOR98LS9X_GONE_MS`, 5 s) and not below
  `STOR98LS9X_CM_LINK_WAIT_MS` (2 s, two of its 250 ms polls and 1.5 s of
  timer lateness). The acknowledgement ends the wait at once; an idle
  Stor98 that has not answered by then is a benign timeout (G3 run C:
  nothing was held, and its next request applied the statement), and no
  idle indication shortens or skips the wait - the acknowledgement is the
  only signal. Orchestrator decision of 2026-10-15, owner to confirm. On NT nothing waits while no driver
  has ever asked for the table (no Stor98 is loaded, so none can answer);
  on 9x it does not wait where the proof and locator were not published,
  nor while no Stor98 has read the proof. Each read checks the bound
  first and the acknowledgement after it, so one first seen after the
  bound is late: logged (`cm.nvlink.await.end`, `cm.nvlink.ack-late`)
  and never taken as an acknowledgement (interface 3's R11 rule, which
  R12 S4 extends to this wait; applied on NT too). An acknowledgement is
  taken only when a second read finds it unchanged (R6d). On 98 SE and ME
  this wait follows a statement, so `3.0.0.0` never reaches it there.
- **A removal on 9x** (MD-5): every CONFIGMG call is the domain's
  worker's (section 6.1, `cm_cfgwk.c`), and its removal step wraps the
  handshake around a removal of a function still present:
  `Query_Remove_SubTree` first (the file system's quiescence and the veto -
  a refusal is final, nothing is forced); accepted, a `REMOVE_PENDING`
  request on the entry with a fresh 24-bit sequence (R9a), and up to 14 s
  on the worker for Stor98's answer to that sequence (R11 O4's T_ack:
  its 12 s acknowledgement bound, two of its polls and its lateness),
  ended early when the worker's request goes stale; the deadline is read
  before each read of the answer, so an answer first seen after it is late
  and changes nothing (R11: the log carries `FFh` for it, `FEh` for no
  answer at all, `CmNvlinkRemoveStep`), and an answer is taken only for
  the request's
  sequence on the request's generation (R9c) and when a second read finds
  it unchanged (R6d). Then `Remove_SubTree` always, whatever the
  answer, because Windows 98 has no service that cancels an accepted query
  and a driver left query-removed stays quiesced; after a request it is
  retried every 2 s while it fails, until 30 s after the first attempt
  (R11 O6's bound), and the request is cleared after it (R11 O5). The
  answer is logged, and
  only a clean one is a clean removal; MD-5's "a timeout aborts the orderly
  eject" cannot hold once the query is accepted on 9x. Interface 3 asks
  two things more that this driver does not do in `3.0.0.0`: after an
  answer that is not clean, or none, to isolate the endpoint (path
  teardown, reset, `DOWN_QUIESCED` and its acknowledgement) before
  `Remove_SubTree` (R11 O5); and after O6's 30 s, to keep the devnode,
  tear the tunnel down, keep retrying and publish no `UP` on that port
  until the removal succeeds. Both belong with the per-port, non-blocking
  waits of roadmap-usb4.md 42-0.7 and 42-A.12 (formerly NV9X.2; R6e). R4's rule for a connection
  manager loaded again in the same boot (retire every endpoint devnode
  under its bridges before its first `UP`) does not arise: a load that
  published is resident and never unloads, and one that did not publish
  wrote no `UP` anyone bound to. The
  acknowledgement wait for a statement, which services the ICM, stays on
  the connection-manager thread before anything is posted to the worker.
  The request is the table's `Flags` bit 3, the sequence in
  `RemoveSequence` and the answer in `RemoveAck` (sequence in bits 31:8,
  outcome in 7:0: clean 1, isolated not clean 2, failed 3), interface 3's
  R9 - the layout this driver proposed and `nvme98` adopted. The worker
  runs the whole removal step itself, request, wait and CONFIGMG calls
  together, so no result is handed between threads and R6f's handoff word
  has nothing to carry here. The children the worker removes for a tunnel
  down are
  functions that vanished, which get no request; under the ICM nothing
  removes a present one (section 7.4: no per-device teardown).
  `REMOVE_PENDING` asks Stor98 for an orderly stop (its flushes and
  shutdown), never makes a reset claim, and is made only after an accepted
  query, only while the request is current, and only to the function the
  queried devnode itself names - the entry identified before the query by
  matching its function's path against the devnode's instance id, still
  `UP` at the same generation and still answering with the same ids; where
  that identity cannot be established, no request is made and the removal
  goes on. So it is safe on any removal path of the worker's, a lost
  controller's included. No request is made before a Stor98 has read the
  proof. In `3.0.0.0` the worker removes the functions that vanished,
  which get no request, and after a lost host interface the children it
  retained, which get one only while the function still answers; the
  tools' 9x eject does not come through the worker at all, so no eject
  reaches the handshake - roadmap-usb4.md 42-A.13 (formerly NV9X.3)
  routes it there.

**The budget, an estimate and not an enforced deadline.** Stor98
measures its gone deadline from its own presence loss. The parts: the
event noticed by the idle poll, which is held to at most 250 ms while any
entry reads `UP`, whatever `XhciCmPollIdleMs` says; a command in flight
when the event is decoded, whose own bound (section 5.5) the queued
tunnel-down action waits out; any actions queued before it, each with its
own polls; the seam's tunnel-down poll (it ends at once on the SHPC
bridge, which it does not read); the channel's own reading, 2 s at most
and normally one read once stage 3 has run; the entry write, a few
stores. With nothing queued ahead and no command in flight that is about
2.3 s; a command or earlier actions add their own bounds. A statement that
comes after Stor98's deadline misses the deadline's purpose (MD-1 has
already completed the held requests), but Stor98 may still apply it as
valid evidence, to release memory it quarantined for instance. Nothing on
the path waits for Stor98, which
polls the table itself. The acknowledgement wait comes after the
statement and delays only the removal.

No statement is sent for a disconnect that matches no device-table entry
(no TUNNEL_DOWN is queued for it) or while the set has more than one
member: the condition is read on one port. The release notes say which
state the pair is in: on the emulator the ordering is read per OS against
the stage-2 event (`39-A.7`'s legs); on hardware no statement is made
(section 7.1).

**The 9x half (2026-10-18, roadmap-usb4.md task 42-0.7; planned as
`NV9X.2` and `NV9X.3`).** Three things this section left to later work,
designed here and reviewed and revised: the 9x eject asked of the
connection manager, so that the `REMOVE_PENDING` handshake above is
reached by an eject at all; the 9x link-down statement, so that "no
reading on 98 SE and ME" above ends; and the per-port, non-blocking waits
interface 3's R6e asks for, in place of `CmNvlinkAwait`'s hold of the
connection-manager thread and the removal step's hold of the worker. The
code is 42-A.12 (the waits) and 42-A.13 (the two transactions); the legs
are 42-V.6. Each choice below that is not the owner's is the
orchestrator's, for the owner to confirm (the list at the end). Record 24
section 7.1 (the tool's 9x eject) and record 22 sections 10 and 10.4 (the
USB4 profile's teardown after it) are revised to point here.

*0. The shape: transactions on the thread, calls on per-member workers.*
Interface 3's R6e: "A CM serving several ports keeps every wait (link
acknowledgement, removal acknowledgement, the delay before
Remove_SubTree, removal retries, teardown) as per-port state advanced by
its one polling loop, so its single writer never blocks; no port's wait
may delay another port's statements (S2) by more than one CM poll
period. CONFIGMG calls that can wait on the OS or on other drivers
(Query_Remove_SubTree, Remove_SubTree, Reenumerate_DevNode) are made off
the writer, from a per-port worker, and their result is taken by the
port's state when it arrives." Today one worker serves the domain
(`cm_cfgwk.c`), and a CONFIGMG call blocked on it - a wizard open, a
removal that takes its time - blocks every other member's work; and the
removal step holds that worker for up to 14 s on `RemoveAck` and 30 s on
the removal retries. The design therefore separates two things that
`cm_cfgwk.c` has in one thread:

- **The transactions** - a removal (vanished or ejected), a statement
  decision, an acknowledgement wait, a marking, a slot reading, record
  28's configuration - are **per-member state** owned and advanced by the
  connection-manager thread's pass: their deadlines, their retries, their
  bounds, their results, every one read on the thread's clock, never
  inside a CONFIGMG call. The thread never calls CONFIGMG or `PCI.VXD`.
- **The calls** are made by **one call worker per member** (at most
  `XHCI_TBT_MAX_TUNNELS`, four, per domain), each executing one CONFIGMG or
  `PCI.VXD` call at a time, handed to it and taken back from it through
  R6f's handoff word for that member: `IDLE -> SUBMITTED` by the thread
  after storing the operation (query-remove, remove, re-enumerate,
  mark, slot read, log-conf step, numbering step), its devnode or
  location, its arguments and a new operation id; `SUBMITTED -> RUNNING`
  by the worker with a compare-exchange, so an operation is taken once;
  `RUNNING -> DONE` by the worker after storing the result and the id it
  ran; `DONE -> IDLE` by the thread after taking or discarding the
  result, which it accepts only for the id it submitted for that
  operation and devnode. One operation at a time per member; a member
  whose worker is inside a call is "busy" to the thread, and the thread's
  transaction for it keeps running its deadline meanwhile: a deadline
  that passes while a call runs is a deadline passed (R11 O6: "an
  elapsed deadline, also when a call is still running"), the transaction
  moves on, the call stays the worker's until it returns, and its late
  result is taken as an O6 retry outcome, never as a clean one. Another
  member's worker is unaffected, which is what R6e asks and what one
  worker could not give (decision V5).
- **Who owns a transaction, and what happens at the NHI's stop.** The
  transaction records and the call workers live in the domain's 9x
  re-enumeration block (`cm_cfgwk.c`'s own pool block, never the NHI's
  extension), and the thread advances them while it runs. `CmCfgwkStop`
  keeps its rule: the NHI's stop never waits for a CONFIGMG call (that
  can deadlock the NHI's own removal); it detaches the block, and a
  worker inside a call finishes that call on its own. What a **detached
  block** then does, with no thread to advance it: a transaction that
  **already holds its removal authorisation** - a `CLEAN` or
  `ISOLATED_NOT_CLEAN` answer taken, or an isolation proven by the
  thread before the stop, or a vanished child's query accepted (nothing
  behind it to isolate) - completes that removal alone: the
  `Remove_SubTree`, retried every 2 s on the worker's own clock until
  30 s after its first attempt, its request withdrawn on success (O5).
  A transaction that does not - its answer awaited, its acknowledgement
  awaited, `FAILED` or unanswered with no isolation made, or blocked - is
  **not completed**: an accepted query alone never authorises a removal,
  the controller behind it may still write, and both profiles leave the
  tunnel up across the NHI's stop; its request is left in place as it
  stands (a withdrawal is interface 3's cancel, which would need the
  isolation nobody can make now), and the transaction is kept for a
  later start. The detached block publishes no `UP`, makes no statement,
  posts no teardown and writes no result (the results' handles closed
  with the stop); a call's late result is **kept in the transaction's
  record** where the transaction needs it to resume - a
  `Query_Remove_SubTree`'s `CONFIGRET`, matched by operation id (an
  accepted query is irreversible on 9x, and a later start must know it
  was accepted and owes the removal, or refused and owes nothing, never
  re-issue it), and a `Remove_SubTree`'s success - and dropped otherwise.
  **The retirement debt outlives the workers**: every member
  with a devnode not retired - a transaction kept, or a removal given up
  after its 30 s - leaves a persistent **member blocker** in an
  image-static list, by the member's location, holding the devnode's
  instance id and the transaction's state (the request's sequence and
  generation, the answer seen, the branch reached), which the block's
  exit does not clear. A new start of an NHI in the same boot whose set
  names such a member **adopts** the blocker before any exposure or
  re-enumeration of that member: it finds the devnode by its instance
  id (gone from the tree: retired by its disappearance, the blocker
  cleared; present: the transaction resumed on the thread where it
  stood - an outstanding handoff consumed first, before any operation
  is submitted for that devnode; the query's recorded outcome honoured;
  the answer read as R6d reads it, the branch taken, the removal made or
  the member blocked as part 1 says), and the blocker
  is cleared only by a removal that succeeded, a devnode verified gone,
  or the recorded **refusal** of an orderly eject's query where no
  earlier accepted query of that devnode is outstanding (a refused
  query owes nothing: there is no retirement debt, so the blocker is
  released and the query is not repeated; a vanished child's vetoed
  query is not this case and keeps its 2 s retries and its gate) -
  never by time or by a worker's exit (R8c, S5). The image stays
  resident, so the code and the lists outlive the stop (section 6.1's
  rule).
- **The per-devnode result slot and the member's gates are shared.**
  Record 22 section 7.7's recovery (task 42-0.5) reads the same
  per-devnode result slot for its retirement barrier (its decision U23:
  on 9x the worker's removal result is R8c's retirement) and waits on
  the same exposure gate before any rebuild's publication; the worker
  gains nothing further for it.
- **The persistent per-devnode transaction is not a call.** It outlives
  every call it submits, keeps its `REMOVE_PENDING` request (R9a: one
  request per devnode, left in place through retries) until the devnode
  is retired, and is never cancelled or re-issued by a newer post for the
  member: a tunnel up, a resync or a tunnel down posted meanwhile queues
  behind it (the member's requests stay collapsed as today, latest wins,
  but behind the transaction's barrier), and an accepted query's
  obligation - `Remove_SubTree` always follows an accepted query on 9x -
  survives everything but the devnode's own disappearance.
- **The cadence.** While any member of any domain has a pending
  acknowledgement, removal or statement state, the thread's pass runs at
  most 50 ms apart (R10: "The CM polls acknowledgements at least every
  50 ms while it waits"), whatever `XhciCmPollIdleMs` says and whether or
  not any entry reads `UP` (today's clamp to 250 ms applies only while
  one does; after the last `DOWN` the idle period could reach 5 s, which
  would miss R10): `CmNvlinkIdleMs` gains that rule. The pass between two sleeps is not
  itself short: the tunnel-up and tunnel-down polls (`cmPoll`, up to 2 s),
  the exposure's wait for a silent bus (up to 1 s) and the USB4 profile's
  synchronous teardown (record 22 section 7.3) all wait inside it. So the
  stepping is a **servicing point**, `CmNvlinkStep`, called from every
  bounded wait of the thread at least every 50 ms - `cmPoll`'s loop,
  the exposure's scan wait, `cmNvPause`, the ICM client's command wait
  and the USB4 core's safe points (record 22 section 7.4, which already
  distinguish a safe point from a transport wait) - non-reentrant (a
  flag: a step never nests in a step), bounded (one step per pending
  state, no call, no wait of its own), and the pass proper steps once
  more. A step reads the deadline before the answer and takes an answer
  only when a second read finds it unchanged (R11, R6d), as
  `CmNvlinkAckStep` and `CmNvlinkRemoveStep` do today. What a step may
  do from inside another member's wait is bounded to what it does from
  the pass: advance a state, submit a call to that member's worker, write
  the table's entry under its lock, publish a result; the host vectors
  run a step from inside each wait.

*1. The eject asked of the connection manager.* On Windows 98 SE and ME
`XHCIINST /eject` and XHCITR's Safely Remove of a unit behind a verified
tunnel port no longer call `CM_Query_Remove_SubTree` and
`CM_Remove_SubTree` themselves (record 24 section 7.1 steps 1 to 4); they
ask this driver, and the member's transaction runs the removal step above
- `Query_Remove_SubTree`, the `REMOVE_PENDING` request where the unit is
an NVMe function with an `UP` entry, the wait for `RemoveAck`,
`Remove_SubTree` after an accepted query, the retries - on the function
still present. The ask is the status record's mechanism (record 24 section
3.3), widened:

- **The freshness token, shared by both tools.** The query device of
  record 25 section 4 is open to both tools on 98 SE and ME (XHCITR opens
  it today; XHCIINST gains the same read for this), and its answer carries
  the start epoch and the content generation (record 25 section 4.3), the
  two the tools' transactions already use; the registry record's own
  generation (record 24 section 3.3) is not a token both can supply, and
  is not used. An ask names the epoch and content generation the unit was
  chosen under.
- **The ask.** The tool writes `XhciCmEjectAsk` (`REG_BINARY`, 28 bytes: a
  nonzero nonce that differs from the last, the member's bus, device and
  function, the unit's bus number and device and function, the epoch, the
  content generation, and flags, 0) to the NHI's driver key beside
  `XhciCmStatusAsk`, under the tools' eject mutex (`XhciinstTbAsk`, record
  25 section 6.3), after record 24 section 7.3's revalidation and record
  25 section 6.2's transaction steps 1 to 5, which stay as they are. The
  driver key is writable on 98 SE and ME (record 24 section 3.3). The
  thread reads it with the status ask, at most every 500 ms
  (`cm_statos.c`), and takes a nonce it has not seen.
- **Acceptance, on the thread, before anything irreversible.** The ask is
  **rejected** (the result below written at once with the reason) when
  its epoch is not this start's or its content generation is not the
  answer's current one (a delayed ask that may name a replacement at the
  same location); when the member is not an exclusive, active member of
  this domain's set; when no function answers at the unit's location (one
  `HalGetBusData` read of its ids) or the unit's bus is not that member's
  secondary bus (`tbt_topo.c`'s header reading); when a transaction is
  already running on that member, or a tool-originated transaction
  anywhere in the domain whose result the tool has not yet consumed
  ("busy": one tool eject per domain at a time, so one result slot is
  enough - the result is consumed when the tool deletes its ask after a
  terminal step, or 60 s after the terminal step was written). An ask
  found at the driver's start is never taken (the thread clears the ask
  value at its start), and the thread re-reads the value **immediately
  before acceptance**: a deleted ask is never accepted. **Acceptance is
  the ownership boundary**: before it, the tool's deletion of its ask
  cancels the request, nothing having been called; after it the driver
  owns the transaction and its obligations, and the ask's deletion ends
  the tool's observation only - never a query, an acknowledgement wait,
  an isolation or a removal the transaction owes (a `Query_Remove_SubTree`
  already running when the ask goes is simply running). The ask carries
  no clock, and no age check is made: what the checks guarantee is that
  the unit was chosen under the topology the driver publishes now (the
  epoch and the content generation), that the tool still wanted it at
  acceptance, and that nothing is in flight for it. A rejection is
  answered in a value of its own, `XhciCmEjectReply` (`REG_BINARY`, 8
  bytes: the nonce and the reason), so a busy or stale ask never writes
  into the slot an accepted transaction owns. Otherwise it is
  **accepted**: the result is written with
  the step "accepted" before any call is submitted, and the member's
  transaction begins. So the tool always learns one of accepted,
  rejected, or nothing - and nothing is not "nothing was changed" (item
  "The result" below).
- **The transaction**, on the thread, with the member's call worker making
  each call: the unit's devnode is found under the member's as
  `cmWkRemoveChild` finds one today (the instance id in its
  `BUS_&DEV_&FUNC_` or path form matched against the location and the
  member's path), its identity re-read at the location (the ids, through
  `PCI.VXD`'s route), and the existing identity rules kept
  (`CmNvlinkWkCandidate`'s: the entry `UP`, published for this id, no
  request outstanding); then `Query_Remove_SubTree` (a refusal is final:
  the terminal step "query-refused" with its `CONFIGRET`, mapped at the
  tool by record 24 section 7.1 item 2's table); then, accepted, the request
  (`CmNvlinkWkRequest`'s rules: the same owner, the same id, still `UP` at
  the same generation) with T_ack's deadline recorded from before its
  publication; the answer stepped each pass; then the branches of R11 O5,
  **kept apart** as the header keeps them:
  **The population check**, a prerequisite of **every** teardown an
  eject leads to, in each branch below: every other child of the
  member's devnode read not started (`DN_STARTED` clear), no other
  function on its secondary bus answering with a started devnode, and
  the reading complete (record 24 section 5.4: a dock carries more than
  one unit behind one port; the NT path's `cmEjectReady` makes the same
  check). A live sibling, or an incomplete reading, means **no
  teardown**; what that makes of the branch depends on whether the
  teardown was the branch's isolation: after a `CLEAN` or
  `ISOLATED_NOT_CLEAN` answer the removal is made and succeeds all the
  same, the result is "removed" with the tunnel left up (exit 0 - a
  removal that succeeded is a success; the unplug advice is the tool's
  assessment of record 24 section 5.4, which in this case gives its
  second text); where the teardown would have been the isolation (`FAILED`,
  none, late) or O6's release, the outcome is **blocked**, as those
  branches say. No teardown is ever made past a failed check.
  - `CLEAN`: `Remove_SubTree`, the request withdrawn on success (O5), the
    result "removed clean"; then, the population check passed, the
    port's PCIe tunnel is torn down under the USB4 profile (record 22 section 10.4, the port known
    by correspondence, record 22 section 7.6; a member whose port is not
    known gets no teardown), the path read closed back (record 22 section
    7.3 step 1), and `DOWN_QUIESCED` published for the entry's generation
    (O5: no acknowledgement is waited for, the instance being gone) under
    part 2's qualification; with the check failed the tunnel stays up,
    the removal itself done and the result "removed clean", the tool's
    own assessment then saying what it finds. Under the ICM client there is no per-device teardown (section
    7.4) and the tunnel stays up; the entry stays `UP` until the unplug, as
    item 5 of section 7.4 says for NT.
  - `ISOLATED_NOT_CLEAN`: isolation is proven by the answer itself (the
    controller disabled, no DMA possible); `Remove_SubTree`, the request
    withdrawn, then `Reenumerate_DevNode(bridge)` on the **same tunnel**
    (O5: "D2: the disk returns as a fresh device on the same tunnel"),
    which the member's ordinary re-enumeration post makes; the result
    "removed, not clean, the drive returns as a new device".
  - `FAILED`, no answer within T_ack, or an answer first seen after it:
    the controller may still DMA, so **isolation first** (O5). Under the
    USB4 profile with the port known by correspondence and the population
    check passed: the port's PCIe teardown (record 22 section 10.4), the path read closed back,
    `DOWN_QUIESCED` and the R12 S4 acknowledgement wait (3 s, a pending
    state), each step bounded by record 22 section 7.3's; **only a
    teardown whose read-back proved the path closed is an isolation**,
    and then `Remove_SubTree` follows and the result is "removed after
    isolation". Anything else - the ICM client (no per-device teardown
    exists), a USB4 port not known by correspondence, a sibling still
    running or an incomplete population reading, a teardown whose
    read-back failed, a system not qualified for the statement - is **not
    isolated**, and the design never synthesises isolation or completion:
    the removal is **not** made (`Remove_SubTree` of a devnode whose
    controller may still write would free what it may still reach, the
    9x half of Stor98's G5), the devnode stays query-removed and quiesced,
    the request stays in place, the member is **blocked** - no `UP`
    published on it, no re-enumeration of it (R8c) - and the result reads
    "blocked: the drive did not stop; shut down Windows before you unplug
    it" (exit 2 at the tool). The block ends at the unplug: the tunnel
    down's vanished-child removal retires the devnode (the link down
    having reset the endpoint, assumption A1), after which the member is
    free again; or at a restart. The release notes say that on 98 SE and
    ME under the firmware connection manager a drive that does not
    acknowledge its stop is left stopped until it is unplugged, and that
    the tunnel is closed after an eject under the USB4 connection manager
    alone (decision V6).
  - **O6**, `Remove_SubTree` failing after an accepted query, whichever
    branch led to it: retried every 2 s on the thread's clock (each
    attempt a call the worker makes when it is free), until 30 s after
    the first attempt, measured also while a call is still running; then
    the devnode kept, the running call left the worker's and its late
    success counted as a retry's success and never as "clean", the result
    "blocked", the member blocked as above (no `UP`, no re-enumeration),
    under the USB4 profile with the population check passed the port's
    tunnel torn down (`DOWN_QUIESCED` releasing what the terminal
    instance holds), the retries going on every 2 s with the veto
    honoured until the removal succeeds, which lifts the block; the
    returning tunnel then gets a new generation. **Under the ICM client
    O6 cannot be met as the interface writes it**: it asks for the tunnel
    to be torn down after the deadline, and a firmware connection manager
    offers no per-port teardown (section 7.4), so this driver blocks and
    retries and tears nothing down. That was a departure from interface
    3's O6 that `stor98cm.h` did not provide for, recorded here as an
    open interface exception for Stor98's maintainer; **it is agreed**
    (the owner, as Stor98's maintainer, 2026-10-22; decision V8): O6
    now carries a clause for a connection manager without a per-port
    teardown - no teardown is made, the devnode and the request stay in
    place (the instance stays terminal, R9d), the eject is reported
    failed, the removal is retried every 2 s with vetoes honoured, and no
    `UP` is published on that port until the removal succeeds or the
    devnode is gone; the unplug's link down is then R12's. The release
    notes say that under the firmware connection manager on 98 SE and ME
    a removal that keeps failing leaves the drive stopped until the
    unplug.
- **A unit that is not an NVMe function** (a dock's switch upstream port)
  gets the query and the removal with no request, as record 24 section 7.1
  made them, under the same transaction (the population check before any
  teardown applies to it too).
- **The result.** The thread writes `XhciCmEjectResult` (`REG_BINARY`, 32
  bytes: the nonce; an **acceptance word** written once at acceptance and
  never changed by later steps (so a transaction that has passed several
  steps before the tool's first poll still reads accepted: every state
  with the ask's nonce implies acceptance, rejections living in
  `XhciCmEjectReply`); the step reached - accepted, queried,
  query-refused, requested, answered, isolating, removing, removed,
  blocked - the query's and the
  removal's `CONFIGRET`, the request's sequence and `RemoveAck` outcome or
  `FEh`/`FFh` for none or late, the result code, and the thread's clock at
  the write) to the NHI's hardware key beside `XhciCmStatus`, through the
  handles the start opened (record 24 section 3.2), at every step change
  **and at every retry attempt** at the same step (the clock field is
  what changes, so a retry is visible progress). **The slot belongs to
  one transaction** from its acceptance until its result is consumed
  (above); a transaction whose result was consumed writes the slot no
  more, its continuing cleanup (O6's retries, a block) being visible
  through the member's state instead - a **member-indexed blocked mask**
  in the query answer's header (record 25 section 4.3; a new field, so a
  new answer version as that section's rule requires), beside the
  record's member list and independent of any router row, since under
  the ICM client with several members no router corresponds to a member
  and after an unplug the router row is gone while a vetoed vanished
  child may still block its member; a change of the mask bumps the
  content generation, and XHCITR shows it per port - and only then can another ask be accepted and own the slot;
  rejections go to `XhciCmEjectReply`, never to the slot. A member's
  surprise-removal work is independent of all this. The
  tool polls it every 250 ms with these clocks: **the answer** - no result
  with its nonce within 3 s of the ask is neither accepted nor rejected,
  the driver may have taken the ask and be writing late or take it
  later, so the tool deletes its ask (a deleted ask is never acted on
  afterwards) and waits 3 s more; a result that appears continues the
  transaction; none is the **uncertain** outcome of record 24 section 8.2
  (exit 2: "The Thunderbolt driver did not answer. Look at the device in
  the xHCI98 USB Tree before you unplug it."), never "No changes were
  made" (decision V7); **the budget** - from acceptance, a terminal step
  (query-refused, removed, blocked) is waited for up to
  `STOR98LS9X_CM_ACK_WAIT_MS` plus `CM_NVLINK_RETRY_FOR_MS` plus a 5 s
  margin (about 50 s), the bound the transaction's own deadlines add up
  to, after which the tool ends with the uncertain outcome, the
  transaction going on in the driver and XHCITR showing its state
  afterwards (the tray's poll of the result); **progress** - inside the
  budget, a result whose clock field has not changed for 20 s (a call
  blocked inside the configuration manager: the driver writes at every
  step and retry, and a pending acknowledgement is re-stamped at least
  every 5 s) ends the tool's wait the same way, so the budget governs
  and the progress rule only shortens it. Record 24 section 7.1's
  outcomes map onto the result: a rejection (`XhciCmEjectReply`) is exit
  1 with its reason and "No changes were made" (nothing was called);
  "query-refused" takes item 2's table by its `CONFIGRET`
  (`CR_REMOVE_VETOED` and any other refusal exit 4; `CR_INVALID_DEVNODE`,
  `CR_NO_SUCH_DEVNODE` and `CR_CALL_NOT_IMPLEMENTED` exit 1), the one
  mapping both records and both tools use; "removed" with its branch
  (clean, after isolation, not clean) is item 3: the removal succeeded,
  and what the tool then says about unplugging is record 24 section
  5.4's **whole** assessment, made by the tool after the result as
  record 25 section 6.2 step 7 makes it - the complete walk read again,
  a storage-only unit, one router on the port, no USB3 tunnel, no other
  unit running, which select its first, second or third text, and the
  restarted-unit outcome (exit 4) where the unit runs again, which the
  `ISOLATED_NOT_CLEAN` branch's re-enumeration makes the expected
  reading - with "the drive did not confirm a clean stop" added for the
  last two branches; the population check decides the teardown alone,
  never the advice; "blocked" is
  item 4 (exit 2). The tool deletes its ask at the end of its run as it
  deletes the status ask; the driver clears the slot's nonce when the
  ask is gone after a terminal step and at its next start.

The tools keep the direct calls for a plain USB device (record 25 section
6.5), which is not a unit, and refuse a unit's eject where no record is
confirmed, as today (record 24 section 3.3); the direct route for a unit
goes. XHCITR on 98 SE and ME, which reads the query device, writes the
ask all the same (the driver key is every user's there), so one request
path serves both tools (decision V1).

*2. The 9x link-down statement.* The condition above needs the member's
slot read empty within the statement's deadline after a tunnel down,
through the emulator's SHPC bridge's DWORD Select, "a byte write of the
capability's DWORD Select into the bridge's configuration space" (section
6.3 item 4), for which Windows 98 SE and ME had no qualified writer. The
writer is `PCI.VXD`'s `_PCI_Write_Config`, the dword write section 6.1.1
qualified for the bus numbering (through its guard): the capability's
first dword is read, the DWORD Select byte (offset 2 of the capability)
replaced, the dword written back (the Capability Id and Next Pointer
bytes, and the status byte at offset 3, written as read: the SHPC
capability's register at offset 0 has only DWORD Select writable), the
DWORD Data at offset 4 read, and the first dword restored to what was
read, each read back - the discipline of the numbering's writes. The
select-read-restore sequence is one operation of the member's call
worker, which owns the selector for its duration; whether `PCI.VXD`'s own
accesses (`_PCI_Lock_Unlock`, PCI.H line 60, exists; what it serialises is
not documented in the headers) can interleave with it is not claimed
either way, and the leg qualifies the writer on 98 SE and on ME
separately. The call is made on the member's worker, never on the
thread, as a conservative choice: every call into another VxD that may
wait on the configuration manager is off the thread (section 6.1's rule),
and the design claims nothing about `PCI.VXD`'s locking (decision V3).

So the tunnel-down action's reading on 9x is a **statement decision** of
the member's transaction, entered **before** the read is posted and
**blocking the member's removal** until it is decided: it carries the
connection's identity (the member, the entry, its generation) and the
**absolute deadline** - under the ICM client 2 s from the tunnel down, as
section 6.5's poll has it; under the USB4 profile record 22 section 10.3's
allowance, 3 s from the presence anchor less the 100 ms the delivery
keeps, whichever is sooner - and posts the slot reading to the worker.
The result (present, empty, or unreadable, with the thread's clock at the
read) is consumed on a later pass, validated against the identity (the
same generation, the entry still marked for this decision) and the
deadline (a result after it is late: logged, `CM_NVLINK_NOT_DOWN`, the
statement withheld), and then the statement is published, as
`CmNvlinkIcmDown` and `CmNvlinkPortDown` publish it today, or withheld
with its reason. The USB4 profile's DOWN hook therefore completes
**asynchronously** on 9x: `CmNvlinkPortDown` answers a new verdict,
`PENDING`, the core's teardown goes on to its later steps, and the
decision's completion publishes or withholds within the same allowance;
on NT the hook stays synchronous. A worker busy inside a CONFIGMG call for
longer than the allowance (a wizard open) answers too late, and the
statement is withheld for that unplug with the reason counted, Stor98's
gone deadline (MD-1) standing: honest, and the common case on an idle
machine is a read within a tick. Only after the decision - published, and
then acknowledged or timed out (R12 S4, the 3 s wait, a pending state); or
withheld - does the member's vanished-child removal proceed (part 3).

The qualification: 98 SE and ME join the pairs the ordering is qualified
on (`cmNvOsQualified`) only once gate G3's ordering check has passed on
each - the statement's marker later than the stage-2 reset and quiesce
event in the emulator's host trace, with the disconnect injected at every
point section 7.2's budget names and under a saturated queue (on 9x, by
the owner's decision of 2026-10-22, a held request with more behind it
in Windows: an MS-DOS copy keeps one request at the drive, so no deeper
queue was read) - read by
42-V.6 on each system separately (ME is not 98 SE: its port never calls a
stop, Stor98's MD-5, so its receiver's behaviour is its own). Until a
system's reading is in `runs/run-42.md`, the verdict there stays
`CM_NVLINK_UNQUALIFIED` (9) as today, and the release notes say which
systems make the statement (decision V4). Both readings were taken on
2026-10-22 by task 43.18 on the owner's answer of that day, in
`runs/run-43.md` (Phase 43's sheet, not `run-42.md`): on 98 SE under
the ICM client and on ME under the USB4 profile, the statement came
after the stage-2 reset and quiesce at every point read - an idle unplug,
a read held at its fetch, every read held, an unplug at the `UP` of a
re-plug - and the removal after the acknowledgement; both systems are
admitted. The queue never held more than one request at the drive (an
MS-DOS copy reads one at a time), and the owner decided the same day
that this one-request reading meets the saturated-queue point on 98 SE
and ME; a deeper queue on 9x is not read. The other prerequisites of the
condition (the emulator's SHPC bridge, the single member under the ICM,
the known correspondence under USB4) are unchanged.

*3. The waits, per member, on the thread.* `CmNvlinkAwait` goes. In its
place every member of a domain carries, in the seam, two gates with the
reasons each is up, and nothing of another member's:

- **The removal gate**: a statement decision pending (part 2) or an
  acknowledgement wait pending (an entry behind the member marked
  `DOWN_QUIESCED` and not yet acknowledged, superseded, timed out or found
  without a receiver: `CmNvlinkAckStep`'s ends, stepped at every
  servicing point). While it is up no **removal call** is made for the
  member - the invalidation on NT, a vanished child's query and removal
  on 9x - which is obligation 3 (the acknowledgement before the removal).
  Tunnel-down **evidence** is never held by it or by anything: a DOWN on a
  member with a transaction running enters its statement decision at
  once (the member's existing transaction learns that its devnode's
  function vanished, which retires a blocked transaction through the
  ordinary vanished-child removal once the gate falls), so a
  disappearance's statement is never queued behind an orderly
  acknowledgement wait, and a blocked member's way out (the unplug, part
  1) is never queued behind the block it is meant to end.
- **The exposure gate**: the removal gate up, or a removal transaction
  not yet retired (an ejected unit's, part 1, or a vanished child's
  below), or the member blocked (part 1), or a detached block still
  working on the member (part 0). While it is up no `UP` is published on
  the member and no re-enumeration of it is made (R8c, S5): the up and
  resync posts for the member queue behind it, collapsed as today, and
  run when it falls.

Everything that `CmNvlinkAwait` guards today waits on one of the two:

- **The removal and re-enumeration call** of a tunnel down or a resync for
  that member: its removal half behind the removal gate, its
  re-enumeration half behind the exposure gate, each made by the
  servicing point that finds its gate down.
- **Every exposure** (`CmNvlinkExpose`: the baseline's, a tunnel up's, a
  resync's): behind the exposure gate.
  Today `CmNvlinkExpose` awaits the acknowledgement and then retires the
  entries carrying a statement; the retirement stays **after** the
  acknowledgement decision (acknowledged, timed out, no receiver), never
  in place of it: an exposure never ends a wait as `SUPERSEDED` (obligation
  3 is the acknowledgement before the removal, and R8b's order,
  `DOWN_QUIESCED` acknowledged or timed out before the next `UP`, is kept).
  On 9x the exposure gate's "retired" is a removal transaction's
  `Remove_SubTree` returned success, read as the call's result, never a
  request made or a request bit cleared (R8c, S5).
- **The resume's release** of a held member (`cmReleaseHeld` for that
  member) on NT, behind the removal gate; the failure release at DISPATCH_LEVEL
  (`CmReenumResumeFailed`) is unchanged, as it never waited.
- **42-A.6**: a resync, or an `UP` published again for a tunnel that
  stayed up, keeps the entry's generation (`CmNvlinkPublishUp` never
  increments an entry already `UP` for the same tunnel); only a new tunnel
  increments it, and a rebuilt one publishes exactly one new `UP` after
  the retirement its barrier waited for.

A gate delays the member it belongs to and nothing else: another
member's statement, exposure and removal run in the same pass, so no
port's wait delays another's statement by more than one servicing
interval, 50 ms while any wait is pending (part 0).

**Vanished children** (a tunnel down's removal of functions that no longer
answer) go through the same transaction as an ejected unit, without the
request: the query, and on a veto a retry every 2 s (R12 S4: "a veto is
retried every 2 s and never forced"), the removal, and the member's
exposure gate up until every vanished child of that member is retired
(R8c, S5: no re-enumeration and no `UP` before); today a vetoed query of a vanished
child lets the member's re-enumeration go ahead, which this ends. A
vanished child's removal that keeps failing is O6's without the request
part: retried every 2 s with the member blocked, until it succeeds or the
devnode is gone.

**The worker's other duties** - a member's re-enumeration with the bus
numbering before it (section 6.1.1), the marking window (section 7.4),
`CONTROLLER_UNAVAILABLE`'s removal of retained children (section 5.5), and
record 28's configuration - are each the member's worker's calls submitted
by the thread, their windows and retries kept on the thread's clock, with
the member's gates observed where a removal or statement is pending; the
domain-wide requests (the prune, the loss) are posted per member.

**The table's writers** (R6a, R6b, as built): the connection-manager thread
writes the table's entries, and the worker's request and withdrawal
(`CmNvlinkWkRequest`, `CmNvlinkWkWithdraw`) write them from the worker,
all under the one table lock (`cmNvLock`) and inside the odd/even
`Sequence` bracket - R6b's "one CM lock" form, which the interface permits;
`CmNvlinkWkAnswer` only reads. With the transactions on the thread the
request and the withdrawal become the thread's own writes; the lock form
is kept, and nothing here needs a single-thread conversion.

*Host vectors* (42-A.12, 42-A.13): two members with a statement pending on
one and a tunnel up on the other, the up's exposure made within one pass;
an acknowledgement arriving after the bound (late, not taken) while
another member's removal runs; the member's transaction driven by a fake
configuration manager through every state, with the thread's isolation
proven, failed and never answered; a deadline that passes while a call
runs, the late result taken as a retry's; the handoff word's transitions
with a stale id; an eject ask with each acceptance check failing (the
epoch, the generation, the member, the location, busy, stale) and with a
unit that is not an NVMe function; the result's layout; the statement
decision's identity and deadline checks, early, on time and late; a
member's population check with a second unit running, in each branch; a
vanished child's vetoed query retried and the exposure held; a DOWN on
a member whose transaction is blocked (the statement decided, the
transaction retired by the vanished-child removal); a step run from
inside `cmPoll`'s wait and from a USB4 safe point while another member
awaits an acknowledgement; a result read at its third step by a tool
that polled late (accepted); a second ask while a result is unconsumed
(busy, answered in the reply value while the slot keeps its owner); a
detached block completing an authorised removal after the NHI's stop,
keeping an unauthorised one, and the next start adopting the blocker
(the devnode present: the transaction resumed; gone: cleared; a stop
during the query whose late result is a refusal, the devnode still
present at the next start: no repeated query, no removal, the blocker
released); a
removal that succeeded with a sibling running (removed, exit 0, no
teardown) against a `FAILED` answer with a sibling running (blocked); `cmNvOsQualified` per system
from a table the leg's reading fills; the 50 ms cadence while a wait is
pending with no entry `UP`.

*Legs* (42-V.6, on 98 SE and ME with the bundled Stor98, the emulator's
SHPC bridge): the clean eject from `XHCIINST /eject` and from XHCITR with
its `RemoveAck` read `CLEAN` and the result's steps shown; an eject with
a file open (vetoed, nothing changed); an eject whose answer is late
(Stor98's acknowledgement bound stretched by the harness) reading
"blocked" under the ICM and "removed after isolation" under the USB4
profile; a CONFIGMG call held past 30 s (a wizard left open) and the
member's block and the other member's work going on; a re-plug of the
same member during an acknowledgement wait; the unit disappearing during
an eject; a result write failing (the qemu flavour's switch) and the tool's
uncertain outcome; the two-port legs under the **USB4 profile with known
correspondence** (one port under an eject or a statement's wait while the
other plugs and unplugs, the second port's statement within one pass) and
under the **ICM client with two members** (every statement withheld, as
the condition says, the ejects and removals still per member); the
surprise removal's statement under a saturated queue with G3's ordering
check on 98 SE and on ME, each written into `runs/run-42.md` before its
system is admitted; the acknowledgement wait measured; the USB4 profile's
teardown after a clean eject only when the member's other units are at
rest, and the tunnel staying up under the ICM.

*Decisions* (the orchestrator's, owner to confirm): V1 the eject asked
through a registry ask beside the status ask, not a new IOCTL, with the
query device's epoch and content generation as the freshness token both
tools supply; V2 the result as a second value in the hardware key, no new
version of the status record; V3 the SHPC DWORD Select written through
`PCI.VXD`'s dword write from the member's call worker, the statement
decision owned by the thread with the absolute deadline, the USB4 DOWN
hook asynchronous on 9x, no claim about `PCI.VXD`'s locking; V4 98 SE and
ME admitted to the statement one at a time, each on its own G3 reading; V5
one call worker per member and the transactions on the thread, R6f's one
word per member, the 50 ms pass while a wait is pending; V6 no isolation
is ever synthesised: where none can be made the devnode stays
query-removed and the member blocked until the unplug, said in the release
notes; V7 no answer to an ask is an uncertain outcome, never "nothing was
changed"; V8 O6 under the firmware connection manager is an interface
exception, the driver blocking and retrying and tearing nothing down
(agreed by Stor98's maintainer, the owner, 2026-10-22, and written into
O6); V9 one tool eject per domain at a time, the result
slot owned by one accepted transaction until consumed, rejections in a
value of their own, the acceptance word persistent, the budget governing
the tool's wait; V10 acceptance is the ownership boundary of an ask, and
a detached block completes only a removal already authorised, the
retirement debt kept in a persistent member blocker a later start adopts.

*Open.* Interface 3's O6 under a connection manager with no per-port
teardown (V8) is settled: agreed 2026-10-22 (above). Whether four call workers per domain cost anything measurable on
98 SE and ME (threads are cheap there, but the leg reads it); whether a
`Query_Remove_SubTree` left accepted and never followed by `Remove_SubTree`
(the blocked case) disturbs the configuration manager's own state on ME,
where the port gives Stor98 no stop; how long a `Reenumerate_DevNode`
blocks behind an open wizard on each system, which bounds how late a slot
reading can be; XHCITR's query device on 98 SE and ME reading the result
from the hardware key while the ask goes to the driver key (two keys, both
every user's there).

*As built (roadmap-usb4.md task 43.18, 2026-10-22).* Parts 1 and 2 are
coded on 42-A.12's call workers (`cm_eject.c`, the pure transaction and
decision, `test_cm_eject`; `cm_ejectos.c`, their driver half;
`xhciinst\inst_ej9x.c`, the tools' half both tools link, `test_ej9x`;
`src\cm\cm_ejask.h`, the three values' layout). Where the code had to
settle what the text above leaves open, or found it unworkable, it does
this, reviewed and revised:

- **The unit's bus.** A tool on 98 SE and ME reads no bus number: a
  unit's instance id is the path form (its devfn, then the member's path),
  never the `BUS_` form a bus-0 function has. So the ask's unit word may
  carry bus 0, which names the member's secondary bus: the thread reads
  that bus (one `HalGetBusData` read of the member's bus numbers) before
  the acceptance checks, and the "not below" check then holds by
  construction for such an ask, while a unit word with a bus is still
  checked against it. The tool names the unit the walk found as the
  member's child, under the epoch and content generation it was chosen
  under.
- **The ask found at a start** is not cleared: the driver never writes
  the tool's value (record 24 section 3.3: "the driver only reads it").
  The nonce the start's first read finds is taken as seen, which is the
  same guarantee (never taken), and the result slot is written empty at
  that read instead.
- **The blocked mask is in the status record**, bits 11:8 of its flags
  word (`CM_ST_F_BLOCKED_*`, one a port in the record's port order), not
  a new field of the query answer's header, and beside it, bits 15:12,
  the members with a transaction in progress and not blocked
  (`CM_ST_F_EJECTING_SHIFT`). The record is framed byte for byte in the
  answer, so XHCITR reads it there, and a change of either mask changes
  the content generation as any record byte does; XHCIINST reads the same
  bits in the registry. No answer version and no header growth were
  needed. XHCITR's port details say "Blocked" or "A removal is in
  progress"; a clear bit says neither that a removal ended nor that
  nothing changed.
- **One gate.** The member's removal gate and exposure gate are one: an
  eject transaction not retired, a block, or a statement decision pending
  raises the member's existing gate (`CmNvlinkHeld`, task 42-A.12), which
  holds that member's removal calls, exposures and re-enumerations
  together.
- **Vanished children are not moved (open).** Part 3's transaction for
  the functions a tunnel down took away - a veto retried every 2 s, the
  member's exposure and re-enumeration held until every one is retired,
  the obligation kept across the NHI's stop, an accepted query never
  issued twice, a Stor98 request left in place through the retries - is
  not built: those removals stay on the domain's CONFIGMG worker
  (`cm_cfgwk.c`) as 42-A.12 left them, where a vanished child's vetoed
  query is not retried and the member's re-enumeration goes ahead (the
  behaviour since `3.0.0.0`; R8c and R12 S4 and S5 are not met for that
  case). A barrier added to the worker alone was tried and set aside
  while 43.18 was built: done properly it needs the transaction's whole
  lifetime discipline (a call owner and operation identity per obligation
  across the stop, the takeovers by `CONTROLLER_UNAVAILABLE` and record
  22 section 7.7's removal-only request sharing the same accepted-query
  state, capacity reserved before an irreversible query), which is
  this record's part 0 machinery applied to those removals: a task of its
  own, which the owner placed on 2026-10-22 as roadmap-usb4.md task
  43.47, built on these transaction records.
- **The unit's own disappearance.** The transaction cannot wait on its
  own gate, so it removes its own unit itself, and only on evidence the
  rest of the channel accepts: a tunnel down on its member
  (`CmEjectDown`, from the seam's tunnel-down action, called after that
  action has entered the member's statement decision, so a decision not
  pending then has completed; every member under the firmware connection
  manager), after which the unit's function is read gone, the member's
  statement decided and any acknowledgement wait ended (the deadline read
  before the answer, as always). That is the
  unplug's link-down reset, assumption A1, as for every vanished child; an
  all-ones read without a tunnel down is a presence signal and nothing
  else (section 7.3) and authorises nothing. The removal retires the
  transaction and lowers the gate, and the held tunnel-down
  re-enumeration then runs. A record a later start adopts has seen no
  tunnel down of that start, so it is retired by its devnode verified
  gone, or by a tunnel down after the adoption.
- **The transactions' clock** is the driver's own, from the tick count,
  shared by every thread that steps a record, so a deadline survives the
  NHI's stop and a later start's adoption. The records are image-static
  (eight, for every domain), which makes each one the member blocker of
  part 0 by construction.
- **After the stop.** A record whose removal is authorised and owed, with
  no call out, is handed to its member's worker as a detached operation
  that retries `Remove_SubTree` every 2 s until 30 s after the first
  attempt and withdraws the request on success; a call out at the stop
  ends at the record's late taker, which keeps its result and then does
  the same. A worker busy with another operation at the stop (a statement
  decision's reading) takes nothing: the removal is then made by the
  adopting start. A later start adopts every other record on one of its
  members before any exposure of it, looks the devnode up again by the
  kept instance id over the whole tree, and resumes in the phase the
  record stood in: an answer still awaited is read with its original
  deadline, so it has usually passed and the failed branch follows; a
  teardown, statement or re-enumeration in progress at the stop is not
  carried over (an isolation in progress, its acknowledgement wait
  included, becomes the block). The query's recorded outcome is never
  re-issued.
- **Every call that names the unit's devnode** reads that handle's
  instance id back first: CONFIGMG answering that the handle does not
  exist, or a successful read finding another id (CONFIGMG reuses
  handles), is the devnode verified gone, the call answering
  `CR_NO_SUCH_DEVNODE` and a removal so answered retiring the
  transaction; an inconclusive read makes no call and concludes nothing
  (`CR_FAILURE`, a retry's outcome). The unit's identity at acceptance is
  read through `HalGetBusData` (the route the rest of the thread reads
  with), not `PCI.VXD`.
- **No request, only for a unit that is not an NVMe function.** A dock's
  switch port gets the query and the removal with no request, as record
  24 section 7.1 made them. An NVMe function is asked of Stor98 or not
  removed: the ask is refused before the query (`CM_EJ_WHY_NO_CHANNEL`,
  nothing changed, exit 1) when no Stor98 channel entry names the
  function - no Stor98 runs it, or none has read the channel - since a
  missing channel is no removal support (`stor98cm.h` R2) and an accepted
  query alone is no isolation (the owner confirmed the refusal on
  2026-10-22; both tools say "The drive cannot be asked to stop cleanly
  (Stor98 is not running it), so it is not ejected from here. Shut down
  Windows before you unplug it."); and an NVMe function the channel no longer
  names after its query, or whose request was not published, takes the
  branch of no answer: isolation first, or the block - never a plain
  removal. A unit whose class code cannot be read is taken as nothing
  answering there (`CM_EJ_WHY_NOTHING`), never as "not an NVMe function".
- **O6 read before every removal result.** A removal's result taken after
  the 30 s from its first attempt, whatever it says and on whichever
  thread, is a retry's outcome: the result is "blocked" (it keeps failing)
  before the success is applied, never "removed clean". The clock is read
  after the call returned, on whichever thread takes its result (the
  thread, the late taker, the detached loop), and the deadline found
  passed there is the same transition as on a step: the block, and the
  tunnel's teardown owed once - an obligation the record keeps whoever
  found the deadline (a late taker, the detached loop), made by the start
  that knows the member's USB4 port (an adopting one included), and
  discharged by the retirement; under the firmware connection manager
  there is none to make.
- **The decision's deadline at its taking.** A slot reading counts only
  when both its read (the worker's clock) and its taking by the thread
  fall before the decision's deadline; the USB4 allowance left is read at
  the taking, which is when the statement is published.
- **A terminal step is written once.** What follows it - O6's later
  success, a block lifted by the unplug - is the member's state (the
  mask), never a second terminal step in the slot.
- **Priority.** A pending statement decision has its member's worker
  first: no transaction call is submitted to a member with a decision
  pending.
- **The qualification** (V4) is a table in `cm_eject.c`,
  `CmEjQualified9x`, with neither system admitted; until the driver tells
  98 SE from ME (the VMM's version, read with the first admission), a 9x
  statement needs both. Until then every 9x decision ends withheld
  (`CM_NVLINK_UNQUALIFIED`) after its reading, the failed branch's
  isolation is never available on 9x (so it blocks under both connection
  managers), and a clean removal's teardown under the USB4 profile is made
  without its `DOWN_QUIESCED`. *Since 2026-10-22* both systems are
  admitted, each on its own G3 reading (`runs/run-43.md`), so no
  version read was needed: a 9x decision now follows the condition as on
  2000 and XP x64, the failed branch's isolation is available where the
  condition holds, and the USB4 profile's clean-removal teardown carries
  its `DOWN_QUIESCED`. Those two eject paths are read by 43.19's legs.
  The eject's own statements (the isolation's, the clean removal's and
  O6's teardown) take the whole qualification, not the systems' admission
  alone (`CmEjStatementQualified`, reviewed and revised 2026-10-22): the
  member must also read as the emulator's bridge (QEMU's vendor id on it
  or the bus-0 bridge above it, an SHPC capability, no PCI Express
  capability), read once per transaction. A teardown read back closed
  proves isolation, not that the endpoint's reset completed (record 22),
  so on a real host no eject path makes a statement: the clean removal is
  torn down without `DOWN_QUIESCED`, a failed stop blocks, O6 retries.
- **The entry after a clean eject with no teardown** (owner, 2026-10-22,
  on 43.19's first leg on 98 SE). A removal that ends clean (or plain)
  with an entry and no teardown to follow - the firmware connection
  manager, which has none (V8) - moves the transaction to a last phase,
  `CM_EJ_P_RETIRE`, whose action (`CmNvlinkRetireEjected`) takes the
  entry from `UP` to `IN_USE` alone: no `DOWN_QUIESCED`, no wait, the
  generation unchanged, as the drive's Stor98 went with the devnode.
  Left `UP`, the unplug that followed made its statement and waited the
  whole 3 s for an answer nobody could give. Not from a servicing point
  inside a wait; an adopting start does not carry it over (the entry then
  stays `UP`, as before). A drive started again by Device Manager's
  Refresh before the unplug runs without the channel until its next `UP`
  (a new tunnel). Not changed, because not read: the USB4 profile's clean
  removal with a sibling running (no teardown, so no mark), and the NT
  side, where Windows ejects and nothing tells this driver that Stor98
  left.
- **Not built.** XHCITR's tray does not poll a result after the tool's
  wait has ended; the port's two lines, read from the record's masks, are
  what show a transaction that outlived it.

### 7.3 What this driver never claims

It never reports a link down it has not seen the firmware report; an
all-ones read of the endpoint is a presence signal for the re-enumeration
walk and nothing else (`nvme98` PLAN, "Presence loss is not isolation").

### 7.4 Safe eject (task `39-A.8`, decision D10)

In Phase 39 by the owner's decision of 2026-10-15. The handoff names two
routes (mark the devnodes removable, or an eject of the driver's own);
under the firmware connection manager there is no per-device teardown -
the only one is mailbox `0x06`, which drops every PCIe tunnel at once and
which this driver never sends (section 7.1) - so **an eject stops the
drivers of the device behind the tunnel and leaves the tunnel up**; the
user then unplugs, and the ordinary tunnel-down path of section 6.5
removes what is left.

**What is marked, and why not the tunnel bridge itself.** The decision as
the owner gave it names the tunnel bridge. The bridge is the host
controller's own switch port and never leaves the machine, and Windows
holds a removable device that was ejected but is still reported by its bus
as "prepared for safe removal" (Device Manager's Code 47, which on XP and
later lasts until the device leaves the bus or the machine restarts).
Ejecting the bridge would remove its stack, and this driver's filter with
it, until a restart, and the next plug would find nothing to
re-enumerate. The device that does leave is the one below the bridge - the
NVMe controller of an enclosure, or the upstream port of a dock's own
switch, whose subtree is the dock - so the mark goes on the devices
reported on an active tunnel bridge's secondary bus, set by the bridge's
filter through the child filters of section 3.2. The scope is exactly the
children of **verified** tunnel ports - the active members of a domain's
set, each with its source (section 6.3) - and nothing else: the bridge
filter answers no capability request of its own, so a switch port or a
processor root port the filter INF matched is never itself made
removable, and a port the INF matched that is not in any domain's set is
never active, so its children are never marked. Section 11 records this
reading of D10 for the owner to confirm.

**Windows 2000, XP, Vista and 7, both architectures:**

1. **Attachment.** The bridge filter keeps a snapshot of the PDOs its
   bridge's last `BusRelations` reported, from its own start (up to 32,
   compared by pointer and never dereferenced). A registration entry
   carries a **baseline** flag the filter sets, under the list's lock, when
   its first `BusRelations` after start has completed and its snapshot is
   taken, attaching nothing. The NHI marks an entry active only once its
   baseline is set, under the same lock; for an entry without one it asks
   the filter to invalidate its bus relations first, and marks it active
   when the baseline arrives. So no `BusRelations` response is ever the
   first one an active bridge sees. When a `BusRelations` request completes
   on an **active** bridge, it creates a child filter
   for each PDO not in the previous snapshot (copying the PDO's
   `DeviceType`, `Characteristics` and `DO_POWER_PAGABLE`), attaches it
   directly above that PDO, and replaces the snapshot - at PASSIVE_LEVEL,
   in its dispatch routine after the completion wait, before it completes
   the request and so before the PnP manager builds the new device's
   stack. A device that was already reported when the bridge became active
   (tunnelled before boot, or present when the driver was installed) is in
   the snapshot and is not marked: it is offered for eject only after it
   is re-plugged, and the release notes say so. A full snapshot or a
   failed `IoCreateDevice` is logged and counted, and leaves that device
   unmarked.

   **Revision 2 (2026-10-16): above a bus filter.** Revision 1 attached
   only to a PDO with nothing on it, and returned `STATUS_DEVICE_BUSY`
   otherwise. On Windows XP SP3 and 7 x86, both ACPI machines, a PCI-to-PCI
   bridge reported on the tunnel bus already carries a device object when
   the answer passes the bridge filter. That object is ACPI's bus filter
   (`\Driver\ACPI`, named by the log below in the revision's legs),
   attached in the answer's completion below the bridge filter. So the
   upstream port of an enclosure's switch (the ORICO ASM2464's `1b21:2463`,
   the emulator's stand-in `1b21:2421`) was never marked, and Windows'
   Safely Remove never listed it. The qemu build's counters showed it:
   `child.attached` 1 and `child.skipped` 1 on every plug, the NVMe on the
   same bus attached, the bridge skipped (`runs/run-41.md`, "Bridges behind
   the tunnel port on NT"). The rule is now `CmPortsChildAttach`
   (`cm_ports.c`), and it decides for a PDO the snapshot found new:
   - **Nothing attached:** the child goes directly above the PDO, as
     before.
   - **Something attached:** the child goes on top of what is there,
     provided the walk up the chain (at most `CM_CHILD_CHAIN_MAX`, 4,
     objects) finds no object of the PDO's own driver (`pci.sys`, which is
     also a bridge's function driver), none of this driver's, none without
     a driver object, and the previous snapshot was not truncated.
   - **Anything else** is skipped as before.

   Why this is safe: the PnP manager builds a new device's stack (lower
   filters, the function driver, upper filters) only once the
   `BusRelations` answer that first reports the PDO has reached it, and this
   runs before the bridge filter completes that answer. So for a PDO that
   is new by the snapshot, nothing attached can be its function driver. The
   driver-object checks hold that reasoning to what can be seen.
   - **Truncated snapshot:** a PDO past `CM_SNAP_MAX` reads as new on every
     answer and may carry a built stack. Another driver can detach and
     delete that stack's objects at any time, so `CmPortsChildFirst`
     decides before the walk. After a truncated snapshot it skips any PDO
     with something attached, without reading the objects.
   - **A bridge stopped and restarted:** this case needs no rule of its
     own. A withdrawal clears the baseline, so the first answer after the
     restart attaches nothing and re-takes the snapshot.

   The child then sits below the function driver the PnP manager loads
   next, as it does directly on the PDO. It now copies `DeviceType`,
   `Characteristics` and the buffering and power flags from the device it
   attached to, as the bridge filter does. That device is the PDO or the
   top bus filter, and the two need not match. The log names the driver found on
   the PDO: `cm.reenum.child.under` and `under2`, eight characters after
   `\Driver\`, with `under.count` and `decision`. It also counts
   `cm.reenum.child.over`. Windows 2000 with ACPI off attaches as before;
   this rule changes nothing where nothing is attached. Not changed: on
   2000, and in the revision's legs on XP as well, the child is lost when
   Windows installs a bridge's driver for the first time and rebuilds its
   stack (roadmap-usb4.md 42-0.4 and 42-A.5, formerly EJRB.1), so the
   first plug of a never-seen unit is not
   listed. The re-plugs are listed. Windows 7 x86 listed the first plug
   too. `cmEjectReady` reads a function as without a stack when nothing
   is attached to its PDO, or when the only object of this driver's on
   it is this child with nothing attached above it (`CmPortsStackless`):
   a function Windows found no driver for (Code 28) keeps the child, and
   any bus filter below it, on its PDO, because its stack was never built.
   Before this rule, 7 x86 posted no eject for a unit while a Code 28
   NVMe drive sat behind the same port (`cm.reenum.confirm` followed by
   `cm.reenum.eject.stale` on every confirmation), where 2000 posted it. A
   function driver or an upper filter always attaches above the child, so
   a built stack still refuses the eject; an object of this driver's that
   AddDevice made also counts as a stack, and a PDO with other drivers'
   objects and no child of this one's is not read and counts as a stack.
   An eject taken holds its member (`EjectHold`, with a reference on the
   bridge) until the hook returns, and a start reaching an object of this
   driver's on a PDO that member reported is refused meanwhile
   (`CmRegStartAllowed`), so a function read stackless cannot start under
   the teardown. Read again on 7 x86 with this rule, the same layout (a
   chain's bridge ejected beside a Code 28 NVMe drive) still ended in
   `cm.reenum.eject.stale`: the ejected bridge's PDO is one ACPI put its
   bus filter on (`cm.reenum.child.under` `ACPI`, decision 2), and that
   filter stays on the PDO after the stack above it is removed, so the
   PDO reads as having a stack. Telling such a filter from a rebuilt stack
   needs the top of another driver's chain read safely, which this driver
   does not import; it is open (run-42). Under the firmware connection
   manager nothing is torn down either way; under the USB4 profile an
   eject on an ACPI machine leaves the tunnel up until the unplug.
2. **The capabilities.** The child filter passes
   `IRP_MN_QUERY_CAPABILITIES` down with a completion wait and, when
   `pci.sys` succeeded it, sets `Removable` to 1 on the way up - the way
   up because the bus driver below fills the structure first. On
   `SurpriseRemovalOK`, Microsoft's reference for `DEVICE_CAPABILITIES`
   says a device with `Removable` set is shown in "Unplug or Eject
   Hardware" **unless `SurpriseRemovalOK` is also set**. Setting both,
   which the decision asks for so that a plain unplug shows no Unsafe
   Removal dialog, would therefore take the device back out of the list
   the eject is offered in, wherever Windows follows that rule. The first
   build sets `Removable` alone. Leg `39-V.2` reads Windows 2000, the one
   target where that dialog is known to appear, with both bits and with
   `Removable` alone: if 2000 lists a device with both set, the Windows
   2000 path sets both (an NT target on which `IoIsWdmVersionAvailable(1,
   0x20)` reads FALSE is 2000); if not, the dialog after an un-ejected
   unplug stays a release-notes line for 2000. XP and later are not known
   to show the dialog; `39-V.3` records whether any does. `EjectSupported`
   and `LockSupported` are left as `pci.sys` set them: with
   `EjectSupported` clear the PnP manager sends no `IRP_MN_EJECT`, and
   nothing here could carry one out.
3. **What the user sees.** Windows 2000's "Unplug or Eject Hardware" and
   XP, Vista and 7's "Safely Remove Hardware" list the device under its
   description: the NVMe controller's name, or, for a dock, the name of its
   switch's upstream port (a generic PCI-to-PCI bridge name, which the
   release notes say stands for the dock). Stopping it sends the subtree
   `IRP_MN_QUERY_REMOVE_DEVICE` (a volume with open files vetoes, as for
   any removable disk) and then `IRP_MN_REMOVE_DEVICE`, the subtree's
   drivers stop, and Windows says the hardware may be removed. The tunnel
   is left up and `pci.sys` still reports the PDO, so the device shows as
   held for eject (Code 47 on XP and later; read on 2000 by `39-V.2`).
4. **The unplug.** The firmware's `device disconnected`, then the
   tunnel-down sequence of section 6.5: the bridge's invalidation reports
   the PDO missing and the PnP manager deletes the held devnode. No
   running stack is surprise-removed, so no Unsafe Removal dialog is
   expected; the legs read it. A re-plug is a new PDO and a new child
   filter. A device ejected and never unplugged stays held until it is
   unplugged or the machine restarts; another device's tunnel change
   re-enumerates the same bus and changes nothing for it.
5. **The `nvme98` channel** (section 7.2, task `39-A.7`) holds no reference
   on any device of `nvme98`'s - `nvme98` polls the table this driver owns -
   so nothing of it can hold an eject back. After an eject the function's
   entry stays `UP` until the unplug, whose tunnel-down path makes the
   statement or withholds it as for any unplug; the ejected driver is
   gone by then, and an acknowledgement nobody gives costs the wait alone.
6. **The switch.** `XhciCmSafeEject` 0 (section 8) attaches no child
   filter. The bridge filter keeps its baseline and snapshot all the same,
   since activation (and so re-enumeration) waits on the baseline.

**Windows 98 SE and ME, best effort.** Section 6.1's rule stands: nothing
goes on the bridge, and the devices behind it are `PCI.VXD` devnodes with
no WDM stack this driver could join. What is known, from the Windows 98
DDK's `CONFIGMG.H` (read 2026-10-15 from the git-ignored extraction under
`tools\`, the copy `39-0.3` records; facts and numbers only, as section 6.1
takes them) and from public Microsoft material:

- The configuration manager keeps a devnode status flag for this,
  `DN_REMOVABLE` (`0x00004000`, commented "S: Can be removed"), which
  `Get_DevNode_Status` (service 54, section 6.1) reads, and which is in
  `DN_CHANGEABLE_FLAGS` (`0x79BB62E0`).
- Two ways to set it exist in that header. A devnode's **own driver**
  declares it when it registers, `Register_Device_Driver` with
  `CM_REGISTER_DEVICE_DRIVER_REMOVABLE` (`0x00000002`); this driver is not
  the devnode's driver and must not register in its place. And the
  "4.0 OPK2" service **`Change_DevNode_Status`**
  (`DEVNODE dnDevNode, ULONG ulStatus, ULONG ulFlags`, with
  `CM_CHANGE_DEVNODE_STATUS_ADD_FLAGS` `0x00000001` and
  `CM_CHANGE_DEVNODE_STATUS_REMOVE_FLAGS` `0x00000000`) changes the
  changeable flags of any devnode. Its number is its place in the
  `Begin_Service_Table` order, 91 (`0x5B`), which `39-0.3`
  confirms and writes into `cm_cfgmg.h` beside the others, with the
  `Get_Version` guard covering it as it covers them.
- Whether anything on Windows 98 SE offers an eject for a devnode so
  marked has not been read: nothing this project has seen shows 98 SE with
  a general eject applet. Windows ME carries "Unplug or Eject Hardware",
  and which devnodes it lists, by which flag, has not been read either.
- `Query_Remove_SubTree` and `Remove_SubTree` remove a subtree, and section
  6.1 already calls them at tunnel down; the 98 Device Manager's "Remove"
  also deletes the device's registry entry, so a re-plug would be a new
  installation, and it is not the route.

So the 9x half is: after each tunnel-up re-enumeration the NHI thread
walks the tunnel ports' children (the `Get_Child` walk of section 6.1) and
calls `Change_DevNode_Status(child, DN_REMOVABLE,
CM_CHANGE_DEVNODE_STATUS_ADD_FLAGS)` on each new one, behind the
`Get_Version` guard and `XhciCmSafeEject`, never on a tunnel port itself,
logging the `CONFIGRET`. `39-V.1` reads on 98 SE whether the flag then
reads set and whether anything offers an eject; `39-V.3`'s ME leg reads
whether ME's applet lists the device, whether stopping it runs the
subtree's query-remove and remove with the volume flushed, and that the
unplug then clears it through the tunnel-down path. If the flag cannot be
set, or nothing on either system offers the eject, the 9x half ships off
and safe eject is NT-only, with the release-notes line "Safe eject is
offered on Windows 2000 and later only; on Windows 98 SE and ME, close the
device's files and unplug it" - the unplug itself is the existing path, the
removal of the vanished subtree through CONFIGMG.

As built (2026-10-15), every CONFIGMG call of this half is the 98 worker's
(`cm_cfgwk.c`), never the connection-manager thread's, because a call made
while an Add New Hardware wizard is open blocks until the wizard closes.
The worker remembers each member's children by devnode handle (the
"known" set, so a child is marked once), and CONFIGMG reuses a freed
devnode's handle: the set is therefore pruned to the bridge's present
children at every walk, and a tunnel down asks for a walk, so a child that
left by any removal - the worker's own, the user's, or Windows' - is
forgotten before a re-plug can reuse its handle, and the re-plugged device
is marked as new. A query-remove the worker made and Windows accepted is
always followed by its `Remove_SubTree`, since Windows 98 has no service
that cancels an accepted query.

Under design record 22's software connection manager on Windows 98 SE
and ME, a removal the user makes (the Device Manager's, an eject tool's,
or any other) does not reach the per-port teardown: there is no child
filter on 9x to report it, so the tunnel stays up until the unplug, which
then takes the ordinary tunnel-down path.

What 98 SE's "Unplug or Eject Hardware" lists was read from the applet
NUSB installs (`HOTPLUG.DLL`): a device is listed when a device interface
of NUSB's class `{40EF6CF0-2DB6-11D3-9F78-00C04F6D5135}` is registered on
its **parent** (an enumeration marker: the applet never opens the
interface or sends it a request); `DN_REMOVABLE` and the Capabilities
value are read but decide nothing. Only NUSB's own mappers register that
class, so a device behind a tunnel bridge, marked `DN_REMOVABLE`, is not
listed. Stopping a listed device query-removes it, then its parent, and
removes the device's subtree only, each step needing the last to succeed.

Release-notes lines, drafted by `39.final` and published with the
`3.0.0.0` cut by `40.final`: which half shipped; that a device tunnelled
before the driver bound is offered for eject only until it is re-plugged,
"the same after the device was disabled and enabled, or its driver was
installed or updated; the Thunderbolt device tool in the package can still
eject it" (owner, 2026-10-15); that a dock is listed under its switch
port's name; and, on Windows 2000, that an unplug without an eject still
shows the Unsafe Removal dialog.

**Read on Windows 2000 (`39-V.2`, `runs/run-39.md`).** With `Removable`
and `SurpriseRemovalOK` both set (a variant build), 2000 no longer lists
the NVMe in "Unplug or Eject Hardware", and a plain unplug still shows the
Unsafe Removal dialog, naming the disk instead of the controller and the
disk: the second bit removes the eject and does not remove the dialog, so
2000 keeps `Removable` alone and the dialog stays a release-notes line.
The child filter is lost whenever the device's own stack is rebuilt - a
disable and enable of the device, or its driver installed or updated -
because the PnP manager rebuilds the stack from the registry (service and
registered filters) with no BusRelations query, and the child filter is
created only when the bridge first reports the PDO; the device is offered
again after a re-plug. **Owed:** a fix that survives a rebuild (designed: revision 3, at the
end of this section). A kernel-
only one is not small: the child filter would have to be registered on the
child devnode as a device-specific `LowerFilters` value, which lives in the
Enum key the PnP manager owns and is written through SetupAPI or CfgMgr32
from user mode (the package's Thunderbolt device tool is the natural
place), while keeping the filter attached across a non-surprise
`IRP_MN_REMOVE_DEVICE` is not permitted.

**The eject on 98 SE and ME, and a viewer on every target** (owner,
2026-10-15). Nothing on those systems offers the eject the 9x half above
prepares, and the owner decided not to borrow NUSB's applet. In its place
the install tool is to gain `XHCIINST /devices` and `XHCIINST /eject`
(design record 24, `24-thunderbolt-device-tool.md`, before code): a list of
the devices behind this driver's verified tunnel ports with what it
publishes about each router, and the safe removal of one of them - by
query-remove then remove on 98 SE and ME, and by the eject request Safely
Remove Hardware makes on 2000 and later, which the tool also makes for a
device the applet does not list. This supersedes, for 98 SE and ME, the
fallback above ("the 9x half ships off and safe eject is NT-only"): the
`DN_REMOVABLE` mark stays as it is, a hint the tool shows, and what ships
on those systems is decided by record 24's legs, not by whether an applet
offers the eject. "Offered for eject only after a re-plug" above is about
the applets; the tool attempts a device behind a verified port whether or
not it is marked. Record 24 also carries the status record this driver
publishes for the viewer, and what the 9x worker must do after a removal
it did not make.

**Revision 3 (2026-10-18, roadmap-usb4.md task 42-0.4): the mark across a
stack rebuild.** The owed fix above, designed and reviewed and revised;
the code is task 42-A.5, the legs 42-V.2. The owner's decision of
2026-10-18 (the decisions table, "Phase 42, the open choices"): the writer
of the registry mark is a privileged NT service, not the unprivileged tray
tool. Each item below that is not the owner's is the orchestrator's, for
the owner to confirm (the list at the end).

*What the mark is.* A device-specific `LowerFilters` value naming this
driver's service, `xhcicm`, on the unit's own devnode - the device at the
top of a verified tunnel port's secondary bus: the NVMe controller of an
enclosure, or the upstream port of a dock's switch (record 24 section
4.3's unit) - and on nothing else: never on a tunnel port, a root port or
any other PCI device. The PnP manager reads the value when it builds the
unit's stack from the registry (a disable and enable, a driver install or
update, a restart with the device present) and calls this driver's
`AddDevice` for it below the function driver, which is where the child
filter sits when the bridge filter attached it at `BusRelations`; so the
mark survives exactly the rebuilds that lose the kernel-attached child.
The value is written from user mode, since the Enum key is the PnP
manager's (the paragraph above), by a privileged writer; the tray tool
runs as the user and never writes it.

*The service.* `xhcimark`, display name "xHCI98 Thunderbolt safe-removal
mark service", one binary per architecture, `XHCIMARK.EXE` (x86) and
`XHCIMK64.EXE` (x64), in the package under `tools\xhcimark\`, built and
gated as `XHCIINST` is (MSVC 6.0 and WDK 7.1's amd64 compiler, the C
runtime static, `/SUBSYSTEM:CONSOLE,4.0`, every SetupAPI and
configuration-manager routine looked up at run time, the import allowlist,
`selftest.cmd`, a host suite over its pure half, `test-package.ps1`), a
Win32 service of its own process (`SERVICE_WIN32_OWN_PROCESS`,
`SERVICE_AUTO_START`, the `LocalSystem` account, no desktop interaction,
`SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN`), installed on Windows 2000
to 7 only (and 8 and 8.1 from 2026-10-19, task 42-A.21, where the
installer installs the connection manager alone; unread there), by the
installer, only when the Thunderbolt 3 / USB4 component
is installed (`xhcicm.sys`). **Its executable lives in
`%SystemRoot%\system32`** (the native directory: `system32` on x86, and
on x64 the 64-bit binary in the 64-bit `system32`), copied there by the
installer and registered with its quoted full path, never in the install
directory: a program the service controller starts as `LocalSystem` must
sit where only administrators and the system can replace it, and record
23 section 9.6's install directory is the user's choice with no such
guarantee (decision M8). The installer refuses to register the service
while the copy there is not the one it staged (compared byte for byte
after the copy), and the move of the install directory (record 23 section
9.6) does not concern it. Never on Windows 98 SE or ME, which have no NT
services and where section 7.4's 9x half (the worker's `DN_REMOVABLE`)
stands as it is. The service writes the mark, records what it wrote,
removes the mark when the unit leaves, reconciles its records against
Windows at its start and at every poll, and does nothing else: no general
registry write, no device start or stop but the one restart below, no
answer to anything but the service controller.

*What the driver publishes.* The query device of record 25 section 4
(`\\.\XHCICM<n>`) gains a fourth request, `IOCTL_XHCICM_MARKS`
(`CTL_CODE(FILE_DEVICE_UNKNOWN, 0xA43, METHOD_BUFFERED, FILE_ANY_ACCESS)`
= `0x0022290C`; the answer `XCMM`, version 1, laid out in `cm_query.h` as
the others are: a header with the magic, version, size, the start epoch,
the published-pass counter, a marks generation that the driver bumps on
every change below, the count, the entry size and a flags word whose bit 0 says the answer is
**incomplete** - a current PDO could not be listed; then one 64-byte
entry per child PDO the domain has seen on an active member since its
start). Each entry carries **observable facts, never an inference about
what a user did**, at fixed offsets: 0 the member's bus, device and
function (as the record packs a location); 4 the child's bus number and 8
its address, **once known**; 12 a serial assigned at the PDO's first
appearance; 16 flags - location known, the PDO still in the bridge's last
snapshot, an attachment by `BusRelations` present, an attachment by
`AddDevice` present, `Removable` answered set for this PDO, safe eject
off for the domain; 20, 24 and 28 three words
each holding two saturating 16-bit counts of what the attachments saw
(the low half first): at 20 `IRP_MN_QUERY_REMOVE_DEVICE` and
`IRP_MN_CANCEL_REMOVE_DEVICE`, at 24 `IRP_MN_SURPRISE_REMOVAL` and
`IRP_MN_REMOVE_DEVICE` after a query-remove, at 28 `IRP_MN_REMOVE_DEVICE`
with no query-remove since the last start and a start that failed; 32
the departure pending for this PDO (the thread's clock at the child's
removal after a query-remove, 0 for none, cleared when an attachment
returns, the PDO leaves the snapshot or the eject is posted on its
confirmation); 36 the port's connection generation captured when the
departure was recorded (under the USB4 profile the generation of record
22 section 7.2 for the port the member corresponds to; 0 under the ICM
client, where no connection generation exists and the confirmation's
check of it is skipped), valid while offset 32 is nonzero and cleared
with it; 40 to 43 reserved, 0; 44 the thread's clock at the entry's
last change; 48 to 63 reserved, 0. The driver names no state such as "rebuilt" or "ejected": a
query-remove is sent for a driver update as for an eject, and a remove
with no query-remove follows a failed start as well as a rebuild, so
which happened is for the service to establish from Windows' current
state (below), not for the driver to guess. The table is per domain,
two sets a member kept apart so that neither can crowd the other out:
`CM_SNAP_MAX` (32) **current** entries (PDOs in the bridge's last
snapshot; a snapshot that had to be truncated sets the header's
incomplete bit, as section 7.4 already counts a full snapshot) and 32
**tombstones** (entries whose PDO left the snapshot), kept in the
registration list under its lock and copied into the query device's
extension with the rest of the answer after each pass. A tombstone is
kept for 60 s, and longer while no poll has read a marks generation past
its departure, up to the set's room: when the set is full the oldest
tombstone is evicted and the header's count of tombstones dropped unseen
is bumped (decision M9). So a quick departure is not lost between two
polls on an ordinary day, and when it is - a stopped service, a burst of
more than 32 departures in a minute - the answer says so, and the
service falls back to Windows' own inventory (below) rather than to the
answer's silence. Reading it needs no rights, as
the query needs none (record 25 section 4.1; leg T.1).

**When the child's location is read.** The `BusRelations` attachment is
made before the PnP manager has created the child's devnode (section 7.4
above: before the answer is completed), and a PDO may not be handed to
the routines that need a devnode until then. So the child filter reads
nothing of the PDO at its attachment: it reads `IoGetDeviceProperty`
`DevicePropertyBusNumber` and `DevicePropertyAddress` when the PnP manager
first sends it a request for that device - its `IRP_MN_START_DEVICE`
(passed down with a completion wait, the read on the way back) or an
`IRP_MN_QUERY_CAPABILITIES` that comes first - and publishes the location
to the entry then (decision M10). Until then the entry says "location not
known" and the service does nothing with it. No registry is read in the
kernel for any of this: the design of 2026-10-18's first draft, which had
the bridge filter look the devnode's `LowerFilters` up at the
`BusRelations` completion, is withdrawn, both because no devnode exists
at that point and because `IoOpenDeviceRegistryKey`'s `PLUGPLAY_REGKEY_DEVICE`
opens the device-parameters key (record 24 section 3.2), not the devnode
key the filter value lives in.

*The child by `AddDevice` (the driver's fourth way in).* `cmAddDevice`
(`cm_entry.c`) decides the roles by the PDO's hardware ids, and the
classifier changes for it (`CmClassifyIds`, `cm_vals.c`): today it reads
any `CC_0604` device as the bridge filter and any other non-empty id list
as the NHI, which would send a unit into the NHI path. Since this
revision the **NHI role** is decided by the whole of `xhcicm.inf`'s
matching contract and nothing narrower: an id of section 3.3's NHI
table, or the USB4 host class the INF also binds (`PCI\CC_0C0340`,
which is how the emulator's `1B36:00F4` and any host with no explicit
model line reach the driver; `CmProfileFor` keeps that class fallback
today), the two carried as the driver's NHI table and checked against
`xhcicm.inf` by the host suite both ways; the **bridge role** needs one
of `xhcicmf.inf`'s model ids, carried and checked the same way, as
record 24 section 4.2's candidate table is; and **every other PDO is
the lower-filter child**, with no role waiting on the registration list:
a bridge filter installed by the INF may be added and started before its
NHI has discovered any set (section 6.2 allows it) and still publishes
at once. A PDO whose ids are in the filter INF's table and whose value
the service wrote - a tunnelled switch's port carrying an id the INF also
names - is a bridge filter **and** a child: one object publishes as the
bridge filter does and, when its location (read at its first request)
is a unit on an active member's secondary bus, answers the child's
capability too; bridge function and child eligibility are decided
separately, the first by ids and the second by location (decision M15),
which corrects section 3.2's paragraph of 2026-10-18's first draft that
had location decide every role. `AddDevice` creates the child filter
(role 3) and attaches it to the top of what is on the PDO
(`IoAttachDeviceToDeviceStack`), copying the flags as `CmPortsChildAttach`
does, and never fails for want of a role (a registered filter whose
`AddDevice` fails fails the device's start, the Code 39 class of failure
this revision exists to avoid; decision M7). Such a child is **passive
until associated**: it reads its location at its first request (above)
and reports to the registration list; it answers `Removable` in
`IRP_MN_QUERY_CAPABILITIES` only while the list associates it with an
active member - its bus number equal to the secondary bus of an active,
exclusive member of a listed domain, read from the member's header by the
seam (`tbt_topo.c`) - **and that domain's `XhciCmSafeEject` is 1**; it
passes the request through unchanged otherwise, attached and passive, and
the entry says so (the flags "`Removable` answered" and "safe eject
off"). A value left on a device outside any set stays passive, logged
and counted (`cm.reenum.child.stray`). **A child attached before it could
be associated** - its capabilities answered before its NHI listed the set,
at boot - stays attached and unmarked, since Windows need not ask for
capabilities again: the entry reads "attached, `Removable` not
answered", and the service's gated restart (below) applies to it as to
a first plug the rebuild overtook, so that the PnP manager rebuilds the
stack and asks again with the set listed; until then the tool-side
fallback stands. The `AddDevice` child records the same
pending departure on a removal after a query-remove (never an eject by
itself, below) and withdraws on
`IRP_MN_SURPRISE_REMOVAL` and `IRP_MN_REMOVE_DEVICE` as section 3.2's
protocol says; its entries in the registration list are kept apart from
the bridge filters'. Driver Verifier on the Windows 2000 SMP guest reads
it (42-V.2).

*Two children on one PDO.* A `BusRelations` attachment and an `AddDevice`
attachment meet on one PDO only when the value already exists at a
`BusRelations` answer that reports the PDO as new - a re-plug of a unit
whose value was not removed (the service stopped, a departure missed) -
since on a first plug the stack is built before the service can have
written anything. The registration list keys attachments by PDO and keeps
both: `Removable` set by either is idempotent; the departure is recorded once
per PDO, when the last attachment of this driver on it leaves after a
query-remove, and the eject follows only on the confirmation below
(decision M4); the entry reports both attachments and the
service sees a unit with a child on its stack, which is all it needs. No
attempt is made to skip either attachment.

*The service's loop.* Every second while any NHI answers, every ten
seconds while none does (no `XhciCmQueryName`, or an inert driver), the
cadence the tray tool uses (record 25 section 3.4), on a thread of its
own, never a pended request - the query device has no cancel path, and a
pended IRP with `IoSetCancelRoutine` and a cleanup-on-close path is new
ground on Windows 2000 for this driver, which the design does not open for
a once-a-second read (decision M1). It finds the NHIs as the tools do (the
devnodes bound to `xhcicm`, each one's `XhciCmQueryName`, the answer's
instance id checked against the devnode, record 25 section 4.2) and reads
the query answer (the epoch, the liveness state, the active members) and
the marks answer together. A domain is **verifiable** when its NHI devnode
is present, started and bound to `xhcicm` and its answer is eligible
(record 25 section 6.2's rule: the stage active, the inert and stopping
flags clear, the liveness state ACTIVE) with the same epoch in both
answers; a domain that is not verifiable (no answer, inert, contained,
stopping, an epoch that changed between the two reads) is skipped for
that poll, and nothing of its units is written or removed - a unit is
never unmarked because its driver is momentarily not answering (decision
M11).

*The write.* For each entry of a verifiable domain whose location is
known, whose PDO is reported, and which has at least one attachment of
this driver or none (every reported unit, that is), the service validates
the notice against Windows before it writes:

1. the member the entry names is an active member of that answer's set
   (the bit at offset 52 of the record, record 24 section 3.1), and a
   devnode exists whose `CM_DRP_BUSNUMBER` and `CM_DRP_ADDRESS` equal the
   member's location - the **verified tunnel port** of record 24 section
   4.2, found by the same rule;
2. the child is a child devnode of that port's devnode (`CM_Get_Child`,
   `CM_Get_Sibling`), whose `CM_DRP_BUSNUMBER` and `CM_DRP_ADDRESS` equal
   the entry's, present (`CM_Get_DevNode_Status` succeeds), and not
   protected (record 24 section 4.3's rule: never an NHI or an ancestor of
   one, never the host's own USB controller; the service carries the same
   pure rule, from `inst_core.c`);
3. its `CM_DRP_LOWERFILTERS` (`REG_MULTI_SZ`) is read, and `xhcicm` is
   absent from it.

Then, under the write mutex (below): **the record first** - a value named
by the devnode's instance id under
`HKLM\System\CurrentControlSet\Services\xhcimark\Marks`, `REG_SZ`, its data
the member's location, the epoch and the word `writing` - then the
`LowerFilters` value written back with `xhcicm` appended as the last string
(`CM_Set_DevNode_Registry_PropertyA`, the whole multi-string, the other
names kept in order; an empty or absent value becomes the one name), then
the record's word changed to `written` (decision M12: intent recorded
before the mutation, so a crash between the two leaves a record to
replay). A write that fails is logged, its record left at `writing`, and
tried again at the next poll, at most once a minute per entry after the
first failure. A notice that fails any check is logged with the check
that failed and nothing is written; a location two devnodes answer to
(none is expected) writes nothing.

*The first plug.* A unit Windows has never seen gets the kernel-attached
child at its first `BusRelations` answer, its value within a poll of its
location becoming known; then Windows installs its driver and rebuilds the
stack. If the value landed first, the rebuild loads the child through
`AddDevice` and the unit is listed in Safely Remove Hardware at its first
plug; if the rebuild came first, the entry reads: the PDO reported, the
value present, **no attachment of this driver on the stack now**; and a
child attached before its association (above) reads attached with
`Removable` not answered. For those two cases alone the service requests
the documented restart of that one device, `SetupDiCallClassInstaller(DIF_PROPERTYCHANGE)` with
`DICS_PROPCHANGE` and `DICS_FLAG_GLOBAL` on a device information set
holding that devnode alone - the route Device Manager takes for a
property change (SetupAPI, documented since Windows 2000; `DICS_PROPCHANGE`
is `0x3`, `DIF_PROPERTYCHANGE` `0x12`), which stops the device through
`IRP_MN_QUERY_REMOVE_DEVICE`, so a volume with open files or any driver of
the subtree vetoes it and the veto stands, and starts it again from the
registry, with the value - on these conditions, each read **after** the
tools' eject mutex (`Global\XhciinstTbAsk`, record 25 section 6.3) has been
taken, so no restart runs inside an eject transaction of the tools and no
eject inside a restart, and each read fresh, never from the sample that
made the service look:

- no installation is in progress: `CMP_WaitNoPendingInstallEvents` with a
  zero timeout answers "none" (the settle record 23 section 5.8 uses, the
  same routine), on two reads 500 ms apart;
- the query and marks answers read again have the same epoch and the
  marks generation unchanged from the sample, and the entry still reads
  as above (reported, the value present, no attachment or an attachment
  that did not answer `Removable`);
- the driver needs no notice of the restart, because **a child's
  departure after a query-remove no longer posts an eject by itself**.
  Windows sends the same query-remove and remove for a disable, a driver
  update and the service's restart as for an eject, so nothing the child
  sees, and no time without a rebuild (a wizard or a slow install can
  hold a rebuild for longer than any bound), tells them apart: the
  registration list keeps the departure **pending** for the PDO (entry
  offset 32), and posts the eject - record 22 section 10.4's per-port
  teardown - only on a **positive, identity-checked confirmation** that
  the unit was ejected, dropping it when an attachment of this driver
  returns on the PDO (a rebuild), when the PDO leaves the snapshot (an
  unplug, whose own tunnel-down path takes over), and **on any change of
  the port's connection** under the USB4 profile (the connection
  generation the pending departure was recorded under differs: an
  unplug and re-plug the connection manager saw before `BusRelations`
  reported the PDO missing), as `cmEjectsVoid` forgets eject evidence on
  a connection change today whatever the next snapshot shows. The
  confirmation is a reading only user mode can make, and it has to be
  **exclusive**: on XP, Vista and 7 a unit Windows ejected is **held for
  eject** until it is unplugged (`CM_Get_DevNode_Status`: problem
  `CM_PROB_HELD_FOR_EJECT`, 47, which Microsoft defines as prepared for
  ejection and nothing else), while a disabled unit reads
  `CM_PROB_DISABLED` and a restarting one starts again; on **Windows
  2000** the held devnode read problem 24 (`runs/run-39.md`), which is
  `CM_PROB_DEVICE_NOT_THERE` and which a driver whose `DriverEntry`
  failed also produces on that system, so it is **no confirmation**
  there - on 2000 an applet eject leaves the tunnel up until the unplug
  (the behaviour `3.0.0.0` has under the ICM client on every eject), and
  only a tool's own successful `CM_Request_Device_EjectA` confirms
  itself: XHCIINST, which runs as an administrator, writes the
  confirmation after Windows' answer, and XHCITR only when it runs
  elevated (record 25 section 6.6's run), the unelevated tray reporting
  the teardown unconfirmed. The service, at its poll, reads the status
  of every unit with a pending departure and, for one held for eject on
  XP and later, writes `XhciCmEjectConfirm` (`REG_BINARY`, 28 bytes at
  fixed offsets: 0 and 4 the query's epoch, low and high words
  (`CM_Q_O_EPOCH_LO`, `CM_Q_O_EPOCH_HI`); 8 the member's location; 12
  the unit's bus number; 16 its address; 20 the entry's serial; 24 the
  port's connection generation the entry reports) to the NHI's driver
  key as the tools write the status ask (record 24 section 3.3); the
  tools write the same value for their own confirmed ejects. The thread
  reads it with the status ask, checks the epoch, the serial, the
  connection generation and that the PDO is still reported with its
  departure pending, posts the eject once, and clears the value. No
  confirmation - the service stopped, Windows 2000's applet, a disabled
  unit, a unit mid-rebuild, a unit stopped by the query-and-remove
  fallback of record 24 section 7.2 (which Windows does not hold as
  ejected, that section says) or one with no child of this driver to
  leave a departure - leaves the tunnel up until the unplug, the tools
  saying the driver has not confirmed the teardown (record 24 section
  5.2's second line), and the release notes' fallback; a unit disabled
  and enabled, or updated, or restarted by the service, is never read as
  ejected whatever the rebuild takes. The tools' watch for the tunnel
  line (record 24 section 5.2, 10 s) covers the thread's status-ask read
  of a tool's own confirmation. It did not cover the service's: the
  service writes under the eject mutex, which the tool held through its
  watch, and an unelevated XHCITR on 7 under emulation saw the
  confirmation 12 to 18 s after the stop (the 43.45 legs, 2026-10-22).
  Since then XHCITR, when it has not confirmed the eject itself, lets go
  of the mutex before its watch and waits for the service's pass
  (record 25 section 6.2 step 7). This ends a defect `3.0.0.0` has: a driver update or
  a disable and enable of a unit under the USB4 profile, whose
  query-remove and remove today post an eject and tear its tunnel down.
  Record 22 section 10.4's clause that the tunnel is down before the
  user is told the device may be unplugged is **superseded for Windows
  2000 and later** (its 9x ordering, section 7.2's 9x half, stands):
  Windows reports the software removal first, the teardown follows on
  the confirmation where one exists, and the tools report what they then
  observe of the tunnel, as record 24 section 5.2 already has it for
  their own ejects (decision M16);
- **the unit's own devnode**, read again by a fresh `CM_Get_DevNode_Status`
  immediately before the call, is present and `DN_STARTED` with no
  `DN_HAS_PROBLEM` (a unit Windows holds for eject - problem 47, or 24 on
  2000 - is not started and is left alone; so is a disabled one,
  `CM_PROB_DISABLED`, and one still installing), its identity the one
  the entry names (the same parent port, bus number and address) and its
  `LowerFilters` still carrying `xhcicm`;
- no restart has been made for this entry's serial, and `Restart` (the
  service's `Parameters` key, default 1) is not 0.

The outcome - the driver's next answer reads an attachment on the stack
(restarted); the call fails, or `DI_NEEDREBOOT` or `DI_NEEDRESTART` is set
in the install parameters afterwards (Windows could not stop it now: a
veto, or a stack it would not rebuild in place); or an error - is logged
with the instance id, and a unit that could not be restarted stays as it
is until its next rebuild or re-plug: the tool-side fallback of record 24
section 7.2 and record 25 section 6.6 (the query-and-remove route for a
unit not marked removable) is kept by 42-A.5 exactly for it, and XHCITR
shows "safe-removal mark written; takes effect at the next start of the
device" beside such a unit (decision M3). The mutex coordinates this
project's tools alone, not Windows' own applet; a stop the user makes
there meanwhile is seen by the fresh reads (the device no longer started)
or vetoes the restart, and nothing is forced.

*Departures and reconciliation.* At every poll, for every record under
`Marks` (and once at start and at every epoch change, the same pass with
nothing skipped): the record's devnode is located (`CM_Locate_DevNodeA`
with `CM_LOCATE_DEVNODE_PHANTOM`, which reaches a devnode that is not
present on Windows 2000 and later). A devnode that is **not present**, or
present but **not a child of an active member of a verifiable domain**
(not in any marks answer as a reported unit - taken only from an answer
whose incomplete bit is clear; an incomplete answer never shows a unit
departed, and the devnode's own presence and parent decide - or behind a
member no longer active), has `xhcicm` removed from its `LowerFilters` (the value deleted
with a zero length when nothing else is in it, the rest kept in order
otherwise) and its record deleted - a departure the tombstone announced,
or one missed while the service was not running (a stop, a crash, a
restart of Windows with the unit unplugged meanwhile). A devnode whose
domain is **not verifiable** at this poll (its NHI not answering, inert,
contained, or in a new epoch not yet read twice) is left alone and
re-checked at the next poll: "cannot verify" is never read as "gone"
(decision M11). A record whose devnode cannot be read is kept and logged.
A record at `writing` is replayed: the value read, the name appended if
absent, the record completed. Once at start, and once a day after, the
service also makes a **full inventory**: every PCI devnode, present or
not (`SetupDiGetClassDevs` with `DIGCF_ALLCLASSES` and without
`DIGCF_PRESENT`, the class of each checked), whose `SPDRP_LOWERFILTERS`
carries `xhcicm` and which has no record is a mark some earlier instance
left unrecorded (a crash between the write and the record, a reinstall):
it is recorded and then judged as any record is. So a value exists only
while its unit is present behind an active member (decision M2), with the
delay of one poll, which the text everywhere states as "within a poll",
never as instantaneous; a re-plug is a new `BusRelations` answer, which
the kernel attachment marks as today, and the value is written again
within a poll. Another driver's filter in the value is never touched: the
service edits its own name alone, as record 23 section 9.2 step 1 edits
`UpperFilters`.

*The installer* (record 23 section 9, record 26 sections 6.2 and 9.3, each
revised by 42-A.5 and 42-A.3). The invariant the installer keeps at the
end of **every** run, install, update or uninstall, under the write mutex
and with the mark service stopped: **a `LowerFilters` value naming
`xhcicm` exists only while the `xhcicm` service is registered on the
machine**. So: when the run leaves `xhcicm` registered (an install or
update of the Thunderbolt component, whatever its verdict for the group -
record 23 sections 5.4 and 5.9's refusal, which leaves the group as it
was, and failure, which leaves the old driver installed, both keep the
service registered), the marks stay and the mark service is installed or
updated and started; when the run leaves no `xhcicm` service (the
uninstall; an update that removed the service and did not register it
again; an install that found a stale `xhcicm` name on a machine with no
such service, the hand-removed case), every such name is removed. That
discharges the task's "after a failed upgrade" clause for each verdict
record 23 defines, by the state the run ends in rather than by its
verdict (decision M13). The service itself is installed (`CreateService`,
`StartService`; advapi32, looked up at run time) on 2000 to 7 after
`xhcicm.sys` and the filter INF, from the `system32` copy; an update stops
it, replaces the copy (`MoveFileEx` with replace, the `.TMP` route of
record 23 section 9.5 applied to that directory) and starts it; the
uninstall's **step 0**, before the filter hosts of record 23 section 9.2
step 1, is a gated phase like the others: stop the service
(`ControlService(SERVICE_CONTROL_STOP)`, `SERVICE_STOPPED` within 30 s),
take the write mutex, enumerate every PCI devnode present or not and read
each `SPDRP_LOWERFILTERS` (an enumeration that does not reach its end, or
a value that cannot be read, fails the gate), remove `xhcicm` from each
that carries it (the other names kept), read each back, delete the `Marks`
records, delete the service (`DeleteService`) and its `system32` copy (the
delayed deletion when in use). A stop that times out, an incomplete
enumeration, an unreadable value or a removal that does not read back
**fails step 0**: the run stops before step 1 with "steps left" as the
gated uninstall does, keeps the `Marks` records of what is unresolved,
keeps `xhcicm`'s service and file and the mark service's registration, and
tells the user to restart and run the uninstall again, which resumes from
what is left; and before record 23 section 9.2's step 5 deletes the
`xhcicm` service and step 6 its file, the installer reads the inventory
once more and stops if any name remains (decision M14). A `LowerFilters`
value naming a filter whose service is gone stops the device at its next
start (Code 39), which is what every rule above exists to prevent. On 98
SE and ME nothing of this exists.

*The write mutex.* `Global\Xhci98MarkWrite`, taken by the service around
each record-and-write and each removal, and by the installer for its
inventory and removals (the shared mutex's security of record 25 section
6.3: `SYNCHRONIZE` and `MUTEX_MODIFY_STATE` to everyone, full access to
SYSTEM and the Administrators), so an installer never races a write in
flight; the service's stop is what ends further writes.

*Switches and settings.* `XhciCmSafeEject` 0 (section 8) attaches no
child at `BusRelations`; a child the PnP manager loads through a value
the service wrote earlier still attaches (a registered filter's
`AddDevice` must succeed) but answers no capability, and the marks answer
lists every unit with the flag "safe eject off" and its attachments as
they are: the service writes nothing and removes what it recorded, so
the loaded filter goes at the unit's next rebuild.
The service's own `Parameters` key carries `Restart` (default 1) and
`LogBytes` (the bound of its log, default 262144); nothing else. Neither
is asked by the installer; record 26's "Advanced..." may show `Restart` as
an NT-only row.

*The log.* `XHCIMARK.LOG` in `%SystemRoot%\system32\LogFiles\xhci98\`
(created by the installer with the directory's default rights, which
admit the system and the administrators), bounded by `LogBytes` and
rewritten from the top when full, one line per notice, check, write,
removal, restart and its outcome, with the instance id and the location;
the release notes ask for it with a Thunderbolt report, beside XHCITR's.
No event log source (a message file and a registry source are more than
the service needs).

*The viewer.* XHCITR shows per unit, from the marks answer and the
devnode's `LowerFilters`: "listed by Windows" (`Removable` read set),
"mark written; takes effect at the next start of the device" (the value
present, no attachment), "not marked" (no value, no attachment), and
whether the service is running (`QueryServiceStatus`); 42-A.5's tool-side
fallback is offered as today for a unit Windows does not list.

*What changes elsewhere.* Record 24 section 7.2's and record 25 section
6.6's "not marked removable" routes stay as the fallback. The release
notes' line that a stop under the USB4 connection manager on NT also
tears the PCIe tunnel down is qualified by 42-A.5: the teardown needs
the confirmation above - the mark service running on XP and later, or
an administrator's tool eject on 2000 - and without it the tunnel stays
up until the unplug, the tools saying the teardown was not confirmed. The release
notes' lines on "A device Windows does not list" and "A later release is
to keep the mark across a rebuild" are rewritten by 42-A.5: the first
plug, the disable and enable and the driver update keep the mark on 2000
to 7 where the service runs; a unit whose restart Windows vetoed is listed
after its next start. `docs/using/release-notes.md`'s report fields gain
the service's log. Record 23 sections 3 (the build), 5.1 (the service's
install and start in the order of operations), 9.2 (step 0 and the final
inventory), 9.5 (the `system32` copy) and 13 are revised by 42-A.5, and
record 26 section 12 lists them.

*Legs* (42-V.2, on 2000, XP, Vista and 7, x64 included): the first plug of
a never-seen unit listed (with the restart, and with `Restart` 0 to read
the unlisted case and the tool's fallback), a disable and enable, a
driver update, each keeping the unit listed; the value present while the
unit is present and gone within a poll after an unplug, after the
uninstall and after the service's reconciliation at a start with the unit
unplugged; a quick unplug and re-plug between two polls (the tombstone
read, the value rewritten); the restart vetoed by an open file and the
unit still listed at its next plug, then a real eject made at once (its
teardown made on the service's confirmation within a poll on XP and
later, and on 2000 on `XHCIINST /eject`'s own confirmation, the applet's
eject leaving the tunnel up there); the
service's restart, a driver update whose wizard is left open for a
minute, and a disable held for a minute then an enable, each of a unit
under the USB4 profile, each with no eject posted and the tunnel kept;
an applet eject with the service stopped (the tunnel up until the
unplug, the tools' line saying the driver has not confirmed the
teardown); an applet eject on Windows 2000 (no confirmation, the tunnel
up) against `XHCIINST /eject` there (confirmed, torn down); a driver
update whose new driver fails to start (problem 24 on 2000, no
teardown); a delayed confirmation across an unplug and re-plug with the
PCI snapshot held back (dropped on the connection change); the service ended by hand between the restart's gates and
its call, then a real eject at once (on XP and later confirmed and torn
down once the service runs again; on 2000 by the tool's confirmation or
not at all); a disabled unit and a unit held for eject offered
no restart; the restart refused while an installation is pending; the service stopped by hand and the unit still
ejectable through the tool, and no record of its unit removed while its
driver is stopped (the domain not verifiable); a value left with the
service deleted by hand reading Code 39 on the unit and the installer's
next run repairing it; a crash injected between the write and the record
(the qemu flavour's test switch) recovered by the start inventory; the
`system32` copy and its registration path read; Driver Verifier on the
Windows 2000 SMP guest with the `AddDevice` child; a restart of Windows
with the unit present and its value in place (the child attached before
its NHI lists the set, read unmarked, then restarted by the service with
no eject posted and no tunnel torn down, then listed); `XhciCmSafeEject`
0 with a value in place and the service stopped (the child attached and
passive, the unit not listed); more than 32 departures within a minute
(the incomplete bit, the Windows inventory deciding); the dock's upstream
port as the unit on the emulator's bridge chain, with an id the filter
INF also names.

*Decisions taken in writing this* (the orchestrator's, owner to confirm):
M1 the service polls the query device, no pended request; M2 the value
exists only while the unit is present behind an active member, removed
within a poll of its departure and at the reconciliation; M3 the
documented per-device restart on a first plug the rebuild overtook,
veto-respecting, once per serial, gated on fresh reads under the eject
mutex, on by default; M4 two attachments on one PDO kept and the eject
posted once per PDO; M5 the service installed only with the Thunderbolt
component, on 2000 to 7 only; M6 the names `xhcimark`, `XHCIMARK.EXE`,
`XHCIMK64.EXE`; M7 a child that never fails `AddDevice`, passive until
associated; M8 the executable in `%SystemRoot%\system32`, never in the
install directory; M9 the driver publishes facts and keeps a departed
PDO's entry until a poll has seen it; M10 the child's location read at
its first request, never at the attachment; M11 a domain that cannot be
verified neither writes nor removes; M12 the record before the write, the
start inventory for unrecorded marks; M13 the installer's invariant by
the run's end state, not its verdict; M14 step 0 a gated phase with the
final inventory before `xhcicm`'s service and file go; M15 the roles by
id tables (the NHI's, the filter INF's) and the child by location,
separately, one object serving both where they meet; M16 a child's departure
after a query-remove posts no eject by itself: the eject is posted on an
identity-checked confirmation (`XhciCmEjectConfirm`, bound to the epoch,
the serial and the connection generation) - the service's reading of
problem 47 on XP and later, or an administrator tool's own successful
eject on any NT target - dropped on a rebuild, an unplug or a connection
change, and with no confirmation (Windows 2000's applet among the
cases) the tunnel stays up until the unplug; record 22 section 10.4's NT
ordering clause superseded.

*Open.* How long after the driver install a first plug's rebuild comes on
each target, which decides how often the restart is needed at all;
whether Windows 2000's `DICS_PROPCHANGE` restarts a PCI device with a built
stack as XP's does, read by 42-V.2; whether `CMP_WaitNoPendingInstallEvents`
is exported on every NT target the service runs on (record 23 section 5.8
read it on 2000; the service treats a missing export as "cannot tell" and
makes no restart).

*As built (task 42-A.5, 2026-10-19, branch `p42-ejrb`).* The code follows
the revision with these readings of it, each reviewed and revised with
the code (`runs/run-42.md`, "42-A.5"):

- **Roles.** `CmRoleDecide` (`cm_vals.c`) gives the NHI role to an id of
  the NHI table or the USB4 host class (`CmProfileFor` not unknown) and
  nothing wider, so an unlisted Intel NHI bound by hand for `XhciCmMode` 2
  is a passive child now; the bridge role to a class-0604 id of
  `xhcicmf.inf`'s models (`CmPortsFilterId`, held to the INF both ways by
  `test_cm_ports`); every other PDO, an unreadable id list included, to the
  `AddDevice` child (`CmChildAddDevice`), which answers success even when
  it cannot attach. A bridge the filter INF names whose value the service
  wrote is called twice; the second call finds the first object (a bridge
  filter of this driver on the stack) and attaches nothing, so one object
  serves both (M15).
- **The location** is read at the start's way back (`CmRegAttLocate`) and
  at a capability request's way back from the answer's own `Address` and
  `IoGetDeviceProperty`'s bus number (`CmRegAttLocateCaps`), never while
  the object is itself asking: `DevicePropertyAddress` is answered by an
  `IRP_MN_QUERY_CAPABILITIES` sent down the same stack, and the first
  build's capability handler, which asked for it, overflowed the kernel
  stack at boot on Windows 2000 (bug check 0x7F, read in the 42-V.2 leg).
- **Association.** An `AddDevice` child (or a bridge filter at a unit's
  place) answers `Removable` while its bus number equals the secondary bus
  the thread last read from an active, exclusive member's header (at the
  discovery and after each tunnel action), the PDO has a current entry
  in that domain's table, and that domain's `XhciCmSafeEject` is 1.
- **The table** keeps each member's 32 current entries and 32 tombstones
  apart (`CM_QM_MAX_ENTRIES` 256, the answer at most 16,448 bytes); an
  eviction moves the generation on; the query device answers
  `STATUS_DEVICE_NOT_READY` before the first pass has built one.
- **The confirmation** is read with the status ask on NT only (the driver
  key opened with `KEY_SET_VALUE` there for it) and written empty after the
  check (`ZwSetValueKey` with no data, `cm_statos.c`, the file the
  allowlists admit for it). Writers take the tools' eject mutex around
  their check of an empty value and their write. Under the USB4 profile
  the thread drops a member's pending departures when that member's
  connection generation (`CmNhiMemberGen`) changes.
- **The service's reconciliation** judges a record by its owning domain
  (the one whose answer lists the devnode's parent, or the record's member
  for an absent devnode) and removes nothing while that domain cannot be
  verified; with no owner listed, only when every NHI is verifiable and
  discovery is complete; a failed lookup or an unreadable location is never
  an absence; an inactive member or safe eject off removes the mark even
  from an incomplete answer. A pending departure does not hold the first
  plug's restart back; the unit's fresh status (started, no problem) does.
  An attempt's record (restart, confirmation) is kept until there is
  evidence that its serial is gone. The restart itself is once per serial
  until the unit is seen listed after it (the mark present and an
  attachment that answered `Removable`); that re-arms one later gated
  restart, because Windows' driver installation deletes a device's own
  `LowerFilters` value (read on XP in 42-V.2: a driver update of a marked,
  listed unit left it unlisted, the value written again after the
  install). A restart Windows vetoed leaves the unit unlisted and is never
  repeated.
- **The CriticalDeviceDatabase** (read on Windows 2000 SP4 on 2026-10-19,
  the release matrix, then on XP and 7 x86; roadmap-usb4 42-A.24): when
  Windows installs a driver for a boot-critical class - a PCI-to-PCI bridge (`machine.inf`), a mass-storage
  controller (Stor98 on an NVMe) - it copies the device's own filter values
  into that class's entry under
  `HKLM\SYSTEM\CurrentControlSet\Control\CriticalDeviceDatabase`
  (`pci#cc_0604`, `pci#cc_010802`), and every device of that class Windows
  finds afterwards gets them before any INF runs. So a mark written on a
  unit before its install reached every later bridge and NVMe controller,
  recorded nowhere: against "never on another PCI device", and after an
  uninstall a missing filter on any new one (Code 39; on a boot disk, no
  boot). 2000 makes the copy at a marked unit's first install, through
  its Found New Hardware install of Stor98 on an NVMe drive as well (so
  every NVMe drive after the first came up with an unrecorded mark); XP
  and 7 x86 made it when a marked bridge's driver was installed again (a
  forced `machine.inf` update), and XP made none for a Stor98 update over
  a marked NVMe drive. So the copy is possible on every system the service
  runs on. The service stays on 2000 to 7, where the matrix's tests
  without the mark found it needed: on 2000 for the first plug, on Vista
  and 7 (x86 and x64) for a stack rebuilt by a disable and enable or a
  driver update (the installer puts it on 8 and 8.1 too, for the
  connection manager alone: `inst_mk_system`). The service therefore takes
  `xhcicm` out of every entry's `LowerFilters` at every poll (the other
  names kept, the value deleted when nothing else is left), and runs its
  inventory at once when it took one out and every minute besides; an
  unrecorded device-level mark the inventory finds is recorded only to be
  judged, kept on a unit behind an active member of a verifiable domain and
  removed from any other device. The `UpperFilters` the filter INF writes
  there for a tunnel port's id are its own and stay while it is installed;
  the uninstall's step 0 (record 23 section 9.2) takes both kinds out,
  from every entry and not only those the filter INF names, before it
  takes the devices' values out and verifies both again, so an uninstall
  cleans a machine that ran a 3.1.0.0 test build whatever is installed;
  so does any install or update run that ends with no `xhcicm` service
  (`mk_end_of_run`), and the new service's first poll does the
  `LowerFilters` part. `XHCICLN` only lists such values, with `xhcicm` not
  installed, and points at that uninstall: a second writer could not be
  serialised with the installer. Read on 2000 in the leg of 42-A.24: after
  a plug and install the database held only the filter INF's
  `UpperFilters`; a never-seen chain and a second enclosure on the other
  tunnel port of a two-port USB4 host came up with recorded marks only,
  the copy 2000's Stor98 install made for the second drive taken out at
  the next poll; after the uninstall nothing under `Enum\PCI` or the
  database named `xhcicm`, and a new bridge, an NVMe drive behind it and
  another on bus 0 started (Code 0).
- **Safe eject off** (the owner, 2026-10-19): the service follows
  `XhciCmSafeEject`. The installer reads the value in force at the end of
  each run (the machine-wide value, else each `xhcicm` device's own,
  else 1): with 0 it does not install the service, and removes one that
  is installed with every mark and the database's `LowerFilters` copies,
  as the uninstall's step 0 does, the filter INF's `UpperFilters` copies
  staying with `xhcicm`; a later `/set XhciCmSafeEject=1` installs it
  again (record 23 section 5.1 step 12). The setting's note in the
  installer says so. The service's own reading of a domain's safe eject
  0 (no new mark, a mark there removed) stays as it was, for the time
  before the installer's run.
- **Hand registration.** `XHCIMARK -register` and `-unregister` register
  and delete the service from `system32` for a test guest before the
  installer's hooks exist; the installer does it in a release.

## 8. Registry values, counters, the log and the snapshot

Values, each `REG_DWORD` unless said, each written by every install path at
its default under the don't-overwrite flag, each read once at start. From
`3.1.0.0` (roadmap-usb4 42-A.17; owner, 2026-10-18) every one is read first
from the machine-wide key `HKLM\System\CurrentControlSet\Services\USB`,
which `xhci98.sys` reads too, and from the NHI's driver key only when it is
not there in any type: a machine-wide value wins when present, one of
another type standing as unreadable with the default, and the machine-wide
key is read even when the driver key cannot be opened (`cm_reg.c`
`cmQuery`; the decision is `..\xhci_regsrc.c`'s `XhciRegPick`, design
record 13 section 5.5). The INFs still write only the driver key:

| Value | Default | Meaning |
|---|---|---|
| `XhciCmMode` | 1 | the master switch (decision D11): 0 off - the inert start of section 1.4.2: the driver binds the NHI, logs it, touches no ring, approves nothing, re-enumerates nothing; 1 auto - section 1.4.2's selection: the ICM client where its four tests pass, design record 22's software connection manager where that record's selection takes the start, and the inert start otherwise; 2 the experimental route for an Intel NHI id the profile table does not list: the ICM client with the Alpine Ridge table, at the user's own risk (the `XhciIntelPortSwitch` 2 pattern, said in the same words in the README and release notes). A registry value cannot make PnP bind a driver to an id its INF does not name, so 2 also needs the id matched: the user adds a model line for it to a copy of `xhcicm.inf` and installs through Have Disk, a procedure the README spells out. What 2 bypasses is the id-to-candidate lookup alone; the readiness, ownership and version tests of section 1.4.2 still give the inert start when they fail; 3 the experimental user override that forces design record 22's software connection manager (its section 3.6, decision U1), at the user's own risk: refused on the Falcon, Alpine and Titan Ridge hosts and on Ice Lake, whose host routers are not USB4 routers, and on an NHI whose ids were not read; never a takeover of a domain whose firmware connection manager runs (`ICM_EN` read set vetoes it on the Intel rows); and it bypasses none of the readiness, protocol, tunnel-preservation and DMA-cleanup tests |
| `XhciCmApprove` | 1 | section 5.4; 0 and 1 only |
| `XhciCmReenum` | 1 | 0 disables every re-enumeration call (section 6), for a machine where CONFIGMG or the filter misbehaves; the ICM client still runs |
| `XhciCmTunnelBridge` | absent (`REG_SZ`) | under the NHI's own device key, so per domain: a comma-separated list of `bb:dd.f` bridges that replaces section 6.3's discovery as the domain's whole tunnel-port set; not written by the INFs (absence is meaningful), and named in the README for machines whose tunnel ports no source lists |
| `XhciCmNoIrqStart` | 1 (not written by the INFs) | design record 28 section 7, Windows 98 SE and ME only: 1 starts an NVMe function below a tunnel port whose interrupt cannot be routed with no interrupt, for a bound Stor98 0.4.0.0 or later that polls (record 28 section 4); 0 leaves it at its problem code. Read where `XhciCmBridgeNumbering` is read; absent reads as 1; any value but 0 and 1 is refused and 1 stands. Ignored on NT |
| `XhciCmBridgeNumbering` | 1 (not written by the INFs) | section 6.1.1, Windows 98 SE and ME only: 1 gives the bridges a tunnel brings up below a tunnel port their bus numbers and memory windows before the CONFIGMG re-enumeration; 0 turns that off (a hot-added bridge is then left as it came up: Code 29 on the P14s until a restart). Absent reads as 1; any value but 0 and 1 is refused and 1 stands. Ignored on NT |
| `XhciCmSafeEject` | 1 | section 7.4: 1 marks the devices behind an active tunnel bridge removable (the NT child filters, and the 9x flag through `Change_DevNode_Status`); 0 marks nothing. Absent reads as 1, as written by the INFs. Read by the NHI at start and handed to the bridge filters with the active mark |
| `XhciCmNoIrqStart` | 1 (not written by the INFs) | design record 28, Windows 98 SE and ME only, read from `Services\USB` first and then the NHI's driver key (task 42-A.17): 1 gives an NVMe function bound to Stor98 0.4.0.0 or later, whose interrupt pin has no provable route, a second logical configuration without the interrupt, so that it starts and Stor98 polls; 0 leaves it as `PCI.VXD` made it. Absent reads as 1; any other value is refused and 1 stands. Ignored on NT |
| `XhciCmPollIdleMs` | 200 | the idle RX poll period, held to 50 to 5000, and held to at most 250 while any entry of the `nvme98` link-state table reads `UP` (section 7.2) |
| `XhciCmCmdTimeoutMs` | 5000 | the ordinary ICM command timeout (section 5.5), held to 1000 to 60000, retries 3; the TR and ICL `driver ready` timeouts and tries of section 1.4.4 take precedence for that command |
| `XhciCmApproveTimeoutMs` | 10000 | the `approve` timeout, the same bounds |
| `XhciCmLogVerbosity` | 2 | the shared log rule (the `0`-`4` ladder of `xhci_log.h`); its own name, as every value of this driver, and the only one read: `xhcicm.sys` was never released under another (owner, 2026-10-16) |
| `XhciCmLogDebugView` | 0 | 1 hands the log to `DbgPrint` in every flavour, as `xhci98.sys`'s `XhciLogDebugView` does; 0 to 1, refused otherwise (owner, 2026-10-16: the reversal of decision D12 below) |

Installing the package is already the user's choice of this driver, so the
master switch defaults to on: a driver that installs and does nothing until
a second step is the trap the "every value visible" rule exists to avoid.
What the switch is for is turning the whole thing off on a machine that
misbehaves without uninstalling, and forcing the kind on a machine where
auto-detection guesses wrong. Values Phase 40 (design record 22) will add beside these,
named now so the convention holds: `XhciCmUsb3Tunnel`, `XhciCmLaneBonding`,
`XhciCmPreOsCleanup`. Later, not in this cut: `XhciCmInterruptMode` (0 poll,
1 INTx once read on hardware).

Counters, in the driver's extension and emitted at stop through the log
sink, named `cm.*`: commands sent, responses, timeouts, retries, errors;
events by code; approvals sent, succeeded, failed; tunnel ups and downs;
re-enumerations run, skipped (and why), and their `CONFIGRET` or status;
mailbox commands and their outcomes; idle polls; malformed and unmatched
frames (section 1.4.6); child filters attached, skipped and refused (section
7.4), and since its revision 3 those created by `AddDevice`, the stray and
the doubled ones. On 98 SE and ME the bus numbering's last attempt is also written to
the registry as `XhciCmBridgeDiag` (section 6.1.1), and design record 28's as
`XhciCmNoIrqDiag`, in every flavour.

The log is the pure ring of `xhci_log.c`, compiled in by path: producers
record from the thread, and the ring is emitted at stop from the thread at
PASSIVE_LEVEL, and in the checked flavours (`debug` and `qemu`) after each
pass of the thread as well. **How it is emitted is decision D12** (taken 2026-10-15:
the default below), because `AGENTS.md`
("Coding Style") admits an unguarded `DbgPrint` at exactly two sites,
`hcd_log.c`'s `XhciLogDebugView` sink and the qemu trace, and says not to
widen it to a third. The decision: in the
`release` flavour `xhcicm.sys` prints nothing, as `xhciuas.sys` prints
nothing; in `debug` and `qemu` the emission is under `#if DBG` through the
qemu flavour's port-`0xE9` mirror and `DbgPrint`, so a Windows 98 bare-metal
reading needs the `debug` flavour installed and DebugView, which is the
HCD's state before task 13-L. The alternative that was put beside it - a
separately gated, opt-in, PASSIVE-level sink of this driver's own under a
value of its own (`XhciCmDebugView`), with the rule in `AGENTS.md`
amended to name it - was not taken. `XHCISNAP` does not read this driver in the first cut: the
door is `xhci98.sys`'s, and a `\DosDevices\` link and region of this
driver's own is a later revision if the counters are wanted off a real
machine.

That revision is design record 25's revision 9 (owner, 2026-10-16): the
query device `\\.\XHCICM<n>` (record 25 section 4) answers a second
request, `IOCTL_XHCICM_LOG` (`0x00222904`, answer `XCML` version 1,
record 25 section 4.5), with the ring and the counters, and `XHCITR`
shows them. The request copies the ring under `LogLock` and leaves it as
it was, so a release install's log is read with no checked flavour and no
DebugView; recording needs `XhciCmLogVerbosity` 2 or above on the NHI's
driver key (2 is the default from `3.0.0.0`, owner, 2026-10-16) and is
read at start.

**Decision D12 reversed for the release flavour** (the owner's decision of
2026-10-16, for consistency with `xhci98.sys`): every flavour of
`xhcicm.sys` hands the log to `DbgPrint` when `XhciCmLogDebugView` is 1,
from the connection-manager thread after each pass that recorded
something and from the PnP path at each stop, at `PASSIVE_LEVEL` only:
the whole ring drained in one step under `LogLock` into the NHI's own
buffer, then printed one whole record a `DbgPrint`, at most 128 bytes
each (`cm_log.c`, `CmQueryLogEmitLines`); the checked flavours always
do, as before. `cm_log.obj` stays the binary's one
`DbgPrint` site, now an `all` row of both `xhcicm` allowlists. Below level
4 the emission withholds the value of each address record
(`CmQueryLogLine`, the query answer's rule), and a first line the wrap
may have cut since the last drain is not printed, so no flavour publishes
an address below the level that asks for one.
The counters are no longer only written at stop: the thread formats them
after each pass in the stop dump's names, the child filters' attached,
skipped and refused among them, and the query device serves them at every
verbosity. Three records carry an address as their value
(`cm.live.bar-changed`, a bus address, and on 98 SE and ME
`cm.reenum.9x.gone.superseded` and `.id-changed`, a CONFIGMG devnode
handle); the answer withholds their values. Since record 25's
revision 12 they are written through `CmLogNoteAddress`
(`XhciLogAppendAddress` under `LogLock`), so they reach the ring at
`XhciCmLogVerbosity` 4 alone, as `xhci98.sys`'s address records do; the
answer's label filter stays beside it.

Design record 25's revision 12 (owner, 2026-10-16) adds a third request,
`IOCTL_XHCICM_PCI` (`0x00222908`, answer `XCMP` version 1, record 25
section 4.7): the configuration space of the NHI, of each member of the
domain's tunnel-port set (section 6.3) and, on Windows 2000 and later, of
the functions below each member, read at the request through the same
`HalGetBusData` reads from offset 0 that section 6.1 and `tbt_topo.c`
make, and never written. The members are those the thread last published
with the query answer. The walk below a member follows section 6.3's bus
numbers - a bridge's secondary bus is entered only inside the member's
own secondary-to-subordinate range and above its parent's, each bus once
- and is bounded (32 functions, 16 buses a member, 8 bridges deep). On
Windows 98 SE and ME the functions below a member are not read: section
6.1's `PCI.VXD` does not number a bus a tunnel brings up after boot, and a
read of a bus number nobody assigned is not made; branch `tb-code29`'s
`PCI.VXD` read route, with its guard, is where that half goes once it is
on this line. Nothing of sections 6 and 7 changes: no filter, child filter
or CONFIGMG call is involved, and the request takes no lock the thread
holds across a configuration access.

The clock behind every wait and every time in the log is the tick count,
`KeQueryTickCount` times `KeQueryTimeIncrement` (`cm_thr.c`): monotonic,
unlike the system time, which a user or a time service may set.

## 9. Provenance and licence

This project is GPL-2.0-only and takes material from these places for this
work, under `AGENTS.md`'s rules and `legal-provenance.md`:

| Source | Licence | How it is used | Record |
|---|---|---|---|
| `usb4-emu/docs/ICM-PROTOCOL.md` and the handoff's other documents | MIT | the clean-room contract; every ICM word layout, register and sequence is cited to it by section, as the `usb4-emu` emulator itself is written from it | a `legal-provenance.md` section 4 row per cited fact class, method `static` (a document read), with the capture's run id as the provenance behind each [obs] |
| `usb4-emu/tools/usb4cm/core`: `u4hi.c`, `u4regs.h`, `u4types.h` (`39-A.2`) and `u4cm.c`, `usb4cm.h` (Phase 40) | MIT at `a68bc74`, the commit `39-A.2` lifted from; GPL-2.0-only from `3c74189` | lifted into `src\cm\` (`tbt_ring.c`, `tbt_regs.h` for this record); the file headers keep the original copyright line and the MIT permission notice with an SPDX tag, and `LICENSE`'s scope note names them; a file taken from `3c74189` or later is an adaptation under the rule below the table | notices per `legal-provenance.md` section 2a (incorporated third-party code) |
| USB4 Specification 2.0 and Connection Manager Guide 2.0 (USB-IF, November 2025 bundle) | USB-IF terms, documents only | facts and section numbers for the generic NHI ring registers and CRC-32C; the PDFs are fetched per machine into `docs/references/usb4_202511/` | rows in `docs/references/README.md` with the bundle URL and each PDF's SHA-256, added 2026-10-14 (owner's permission to take the files from `usb4-emu`'s own references folder); the README's "USB4 2.0, November 2025 bundle" section also records that Thunderbolt 3 itself has no public specification to fetch |
| The Windows 98 DDK's `CONFIGMG.H` | Microsoft DDK terms | the CONFIGMG service numbers and flag, status and result values, each cited by line in `src\cm\cm_cfgmg.h` (sections 6.1 and 7.4) | a `docs/references/README.md` row and `legal-provenance.md` rows; never tracked |
| Linux `drivers/thunderbolt` (GPL-2.0) and FreeBSD `sys/dev/thunderbolt` (BSD-2-Clause), at the pins of `external/README.md` | GPL-2.0; BSD-2-Clause | since 2026-10-15 (owner): ids, message layouts, offsets, constants, sequences and quirks, cited by file and symbol, the [linux] facts of sections 1.4, 3.3, 5, 6.3 and 10.1; and, once `usb4-emu`'s relicensing landed (`3c74189`, the same day), code adapted with its notices kept, as the paragraph below says | `legal-provenance.md` section 3, and a section 2a entry per adapted file |
| The PCI ID Repository's listing (<https://pci-ids.ucw.cz/>, `pci.ids` version `2026.10.14`, fetched 2026-10-15) | GPL-2.0-or-later or BSD-3-Clause, by the file's own header | ids only: the Alpine Ridge and Titan Ridge NHI and bridge ids and their listed names, for the INFs and section 3.3 (decision D9) | a `legal-provenance.md` section 4 row, method `static`, with the URL, version and SHA-256; the file never tracked |

Linux `drivers/thunderbolt` (GPL-2.0) and FreeBSD `sys/dev/thunderbolt`
(BSD-2-Clause) have been interface documentation since 2026-10-15
(owner): ids, message layouts, offsets, constants, sequences and quirks,
cited by file and symbol. `ICM-PROTOCOL.md` stays the first-cited
contract; its [inf] may be checked against `icm.c`. Facts read that way
carry the [linux] tag (section 1.4.1). The pinned commits are
`external/README.md`'s (Linux `c6859eed755df351a3978b33cb92365f9b3e8f06`,
FreeBSD `9d50245a0f1333e10cc1c4dda16878cd79a7c42b`); Linux has no
`include/uapi/linux/thunderbolt.h` at its pin.

`usb4-emu`'s relicensing to GPL-2.0-only landed on its `main` at
`3c74189` (2026-10-15), and its handoff's section 3 now says that this
project may adapt code directly from `usb4-emu` (`tools/usb4cm`
included), from Linux `drivers/thunderbolt`, from FreeBSD
`sys/dev/thunderbolt` and from Intel's tbtools data, keeping the notices
of whatever it adapts. By the owner's rule of that day, for the
Thunderbolt and USB4 work only (`legal-provenance.md` section 3, "The
Thunderbolt and USB4 mirrors"; `AGENTS.md`, "Reference
Implementations"):

- code from those sources may be adapted into `src\cm\`, translated to
  C89, safe on Windows 98 and 2000, with no 64-bit arithmetic and in
  `AGENTS.md`'s style, never a paste of Linux kernel APIs;
- every adapted file keeps each source file's copyright lines and SPDX tag
  (`GPL-2.0-only` with Linux's or `usb4-emu`'s lines; FreeBSD's
  BSD-2-Clause notice, or tbtools' MIT notice, verbatim), names its source
  file and is marked as changed, with the date;
- each gets a `legal-provenance.md` section 2a entry naming the source
  file, the pinned commit actually read and what was adapted, in the same
  change;
- behaviour taken from Linux alone stays [linux], unconfirmed, until a
  capture shows it, whether its code was adapted or not;
- nothing moves from this repository or `usb4-emu` into `nvme98`
  (BSD-3-Clause).

The `usb4cm` files `39-A.2` lifted came from `a68bc74`, where they carry
the MIT licence, and keep that notice (`legal-provenance.md` section 2a);
a later file follows the licence of the commit it is taken from. This
settles the conflict design record 22 section 17 raised between this
section's immediate lift and the gate record 22 section 13 set (an
orchestrator decision of 2026-10-15, for the owner to confirm).

As always, no third-party document or binary enters the tree, facts are
written re-derivably (word, bit range, capture line), and binary-derived
facts are tagged `static` or `runtime`.

## 10. Verification

### 10.1 Host tests

`test\test_tbt_ring.c`, `test_tbt_icm.c`, `test_tbt_hw.c` and
`test_tbt_topo.c` under the existing harness, with
`test\run-host-tests.cmd` growing four suites: `test_tbt_ring` (rings),
`test_tbt_icm` (the ICM client), `test_tbt_hw` (the hardware profiles, the
integrated family's force power and the selection of section 1.4.2,
`tbt_hw.c`) and `test_tbt_topo` (topology, with the snapshot rule's
vectors):

- rings: the Spec Appendix A.2 CRC-32C vectors (as `usb4cm`'s host test
  runs them), descriptor wrap, consumption by index and by the done bit, the
  RX give-back, a frame larger than its buffer, sequence numbers and a
  response that echoes the wrong one;
- ICM: every observed frame of section 5.2 decoded to the fields
  `ICM-PROTOCOL` lists, the name block's four entry types and a zero-length
  terminator, the state machine through bind, announce, approve, disconnect,
  resume-refresh and the 500 ms reconciliation, timeouts and retries with a
  fake clock, the error flag, an unknown code; and the FR, TR and ICL rows
  on frames synthesised from section 1.4.4's structures - FR's two-word
  `driver ready` and its 14-frame topology answer (whole, out of order, one
  frame missing), TR's and ICL's four-word `driver ready` with their own
  timeouts, a route-carrying announcement with no `get route`, a packet
  index of 1 ignored, an ICL RTD3 veto logged and an ICL announcement with
  `approved` = 0 left without an approve, an approve answered with the
  error flag (entry unapproved, then an identical announcement earning one new approve) and one executed by the firmware whose
  response is lost (outcome unknown; the verified set re-enumerated after
  the poll whether or not a reading changed, with the port already present
  and with an unrelated member turning present, neither settling it; then
  an announcement settling it), a failed route request followed by an
  identical announcement (the route retried, then the approve sent), a
  short frame, an all-zero UUID, a name block overrunning its frame, and a
  disconnect matching no entry; and section 5.3's generation read - none
  where it is left off, one per occupancy on every row at the next
  idle poll (FR at the topology query's route, AR after get route) and none on a refresh, a USB4 major 1 and 2, a major 0, a
  reserved major, a header that is not the route's, no answer, a
  request-error notification, a disconnect during the read, an
  announcement arriving during an unanswered read, and the reads stopped
  by an unanswered one until the rings restart - with
  the status record's generation for each (`test_cm_status`);
- selection and hardware profiles (`test_tbt_hw`), pure (section 1.4.2),
  as a table of register readings to
  outcomes: each row's ownership predicate passing; ICM enabled clear on
  each row (inert, except on Maple Ridge and Tiger Lake to Meteor Lake,
  which hand over to design record 22 on a valid read whatever NVM
  authentication says); ICM enabled set with NVM authentication never done
  on those ids (inert, never handed over); host
  capabilities all ones (inert); an interface version of `0x40` (inert);
  operating modes 0, 1, 2 and 7 (safe mode, inert, inert, inert); a
  failure after activation (rings stopped, `0x07` only after `driver
  ready`, no `0x06`); an integrated force power whose firmware-ready bit
  never sets (inert, and the bit this driver set left set at stop and
  logged; the vector as first merged expects it cleared, and changes with
  section 1.4.2's code), one the BIOS had set (left set at stop), and an active
  instance's own bit (cleared after the unbind); an
  Apple subsystem id; a non-Intel candidate, whose firmware-status
  register is never read; and the inert state's own vector - after it no
  ring write, no mailbox write and no reset write is issued, at start or
  at stop, which the fake register file records;
- topology: the two captured layouts of section 6.3, the generic layout,
  a two-port switch (both members, source "switch"), an integrated layout
  with listed and unlisted hot-plug root ports (only the listed ones), two
  NHIs of one family with the first started alone and with the second
  disabled (the listed ports ambiguous both times), an override against a
  discovered claim (ambiguous), safe eject off with activation and
  re-enumeration still working, a foreign hot-plug bridge beside the
  switch (not a member), no member at all (re-enumeration disabled), and
  the override list, including an entry that is not a bridge;
- the bridge filter's snapshot rule of section 7.4, pure: a first
  `BusRelations` on an inactive bridge marks nothing, an activation asked
  for before the baseline (deferred until the baseline is taken, and the
  boot-present PDOs then still unmarked), a later one on an
  active bridge marks only the PDOs it had not reported before, a PDO that
  left and a later new one with the same pointer value, and a full
  snapshot.

**The quirk matrix.** What the Linux driver does differently per host or
router, and what this driver does about each, so that a reviewer can see
none was missed:

| Linux quirk or difference | Where | Here |
|---|---|---|
| Intel interrupt auto-clear (`QUIRK_AUTO_CLEAR_INT`, `REG_DMA_MISC_INT_AUTO_CLEAR`) | `pci.c`, `nhi_pci_check_quirks`; `nhi.c`, `nhi_mask_interrupt` | no interrupt is enabled (section 4.3); the quirk chooses only how the masks are written, directly on Intel hosts and through Interrupt Mask Clear elsewhere; DMA misc is read for the log and never written |
| Falcon Ridge end-to-end flow control (`QUIRK_E2E`) | `nhi_pci_check_quirks` | not applicable: it applies to rings opened with E2E, and ring 0 is not (Linux's control channel does not ask for it) |
| CLx disabled on early Titan Ridge firmware and AMD Yellow Carp and Pink Sardine routers (`quirk_clx_disable`) | `quirks.c`, `tb_quirks` | not applicable to the ICM client, whose firmware owns link states; CLx is never enabled by this driver; design record 22's |
| USB3 bandwidth limit on Alder Lake to Meteor Lake and Barlow Ridge (`quirk_usb3_maximum_bandwidth`) | `tb_quirks` | not applicable to the ICM client (Linux skips it under the ICM itself); design record 22's |
| Runtime power kept in DisplayPort redrive (`quirk_block_rpm_in_redrive`), forced link-controller power (`quirk_force_power_link`), DisplayPort credits (`quirk_dp_credit_allocation`) | `tb_quirks` | not applicable: no runtime power management and no DisplayPort tunnel here |
| Runtime D3 and the RTD3 veto | `icm.c`, `icm_icl_rtd3_veto` | never requested (section 5.6) |
| MSI and MSI-X set up by both Linux and FreeBSD (`nhi_pci_allocate_interrupts`) | `pci.c`; FreeBSD `nhi_pci.c` | polling only; neither mirror qualifies polling, so the VM legs and the bench do |
| 64-bit DMA masks requested by Linux | `nhi.c`, `nhi_probe` | the common buffer is allocated below 4 GB (section 4.2); that it works is qualified per family by the legs, not assumed from Linux and not ruled out by it |

**Negative tests**, as host vectors where pure and as legs where not: an
unsupported capability (an interface version of `0x40`, a ring count of
0); an initialisation that fails (force power never ready, the mailbox
error bit, no `driver ready` answer); ownership that is ambiguous (ICM
enabled with an unknown mode, all-ones registers); two domains (two NHIs,
each with its own set, a disconnect on one not touching the other's
table); and tunnels that exist before the driver loads (the V.1.3
boot-tunnelled device, re-announced and left alone, no reset and no
teardown in the trace).

The pure files carry no DDK include and `test\run-host-tests.cmd` compiles
them with the HCD's `XHCI_HOST_TEST` rules; `u4types.h`'s `U4_HAVE_TYPES`
maps its `u32` onto `ULONG` in the driver build.

### 10.2 The virtual-machine legs, and how they are run

The vehicle is `usb4-emu`'s QEMU fork and `scripts/run-win98.sh` on the
`tb3-alpine-ridge` profile with the captured switch topology (`BIOS=` and
`DEVICE_AT_BOOT=`), driven by QMP. Rules for every run from this project:

- Never modify `usb4-emu`: its scripts are run as they are from its tree,
  its disk images are backing files only.
- Own overlays, own directory, own ports. The overlays live under this
  repository's git-ignored `vm\usb4\` (for example `vm\usb4\win98-tb3\`),
  made with `qemu-img create -f qcow2 -b <usb4-emu>\vms\win98\win98.qcow2 -F qcow2`
  (base snapshot `base-install`) and an overlay of `nvme32.qcow2` beside it.
  `run-win98.sh` is pointed at them with `VM_DIR=` (it writes its pid file,
  ports file, log and command line there), `QEMU=` (the fork's built
  binary, `src/qemu/build/qemu-system-x86_64.exe`, or a copy), `NVME=`, and
  `QMP_PORT=`/`VNC_PORT=` from a block this project claims: **47900 to
  47999**, each run sheet naming the pair it used. Every port already in use
  under `usb4-emu/vms/*/ports.env` (47101 to 47800, 48020) is left alone.
- Never stop a QEMU process this project did not start; `stop-win98.sh`
  acts on the pid file in `VM_DIR` only.
- The driver goes in on a FAT transfer disk as the HCD's `deploy loop` does,
  or the `vms/win98/drvnew.img` pattern of a 32 MiB FAT16 IDE disk built on
  the host (`tools/reenum-test/mkdisk.sh` shows the mtools recipe).
- TCG only (`WHPX` aborts with any PCI bridge on i440FX), `-cpu pentium3,-apic`,
  which the script defaults to; a boot to the desktop is about two minutes
  without an NVMe at POST.
- The tunnel's device router is toggled with QMP `device_del enc0` and
  `device_add usb4-device,...` (the hot-plug path through the ICM), never
  with `qom-set ... tunnel up`, which bypasses the connection manager and is
  for the driverless baseline only.
- The fault-injection harness (`docs/HARNESS.md`: properties on the tunnel
  bridge such as `x-faults`, `x-gate`, `x-hang`, set at start or by
  `qom-set`) for the unplug-during-I/O legs and `39-A.7`'s ordering legs;
  its gate actions are test stimuli, never evidence the driver acts on.
- When `usb4-emu` adds the controller models its catalogue announces, the
  legs of this section extend to them. Since its `main` at `a50c19e`
  (2026-10-15) `profile=tb3-titan-ridge` is a real Titan Ridge model,
  written from Linux's description (route-string addressing, approval
  keyed by UUID and route; `ports=1` gives the single-port part), so the
  TR row is testable on an emulator; the other rows still run on none. A
  change the legs need in the emulator is written as a request for its
  maintainer, never made here.
- The machines this is for: the legacy operating systems need a CSM, so
  the realistic targets are Thunderbolt 3 laptops with a CSM (the P14s),
  Intel desktop boards with discrete Thunderbolt and a CSM (the firmware
  connection manager), and AM5 X870 boards with a CSM (ASMedia's ASM4242,
  design record 22's software connection manager). The Framework laptop
  is UEFI-only: its capture is a reference, not a run target.
- `-trace 'usb4_*' -D <VM_DIR>\usb4-trace.log` on every leg; the trace
  decodes every ring 0 frame both ways and is the oracle for "what the
  client sent".
- One emulator observation, modelled only and not yet read in a guest:
  on the q35 profile `usb4-emu`'s `usb4-tb3-dsp` downstream ports keep
  QEMU's default `hotplug=on`, so every one of them is Hot-Plug Capable
  and section 6.3's switch source puts the port carrying the dock's xHCI
  in the set beside the tunnel port. A set of two there is section 6.3's "set of more
  than one", harmless but withholding the `nvme98` statement; the q35
  legs read the set the guest's log names and confirm it.

The Windows 98 SE legs (`39-V.1`), each from a fresh overlay:

| Leg | `BIOS` | `DEVICE_AT_BOOT` | What is read |
|---|---|---|---|
| V.1.1 install | `OFF_OFF` | 1 | the NHI's Found New Hardware, the INF taken, Code 0; the log's bind sequence; the dock announced unapproved, approved, the tunnel up (`usb4_tunnel_transition`), the bridge re-enumerated, the NVMe found by nvme9x (already on the base image) and its volume mounted, with no Device Manager Refresh |
| V.1.2 unplug and re-plug | `OFF_OFF` | 1 | `device_del`: the disconnect event, the volume gone, the devnode removed; `device_add`: announced, approved, the volume back |
| V.1.3 boot-tunnelled device | `OFF_ON` and `ON_ON` | 1 | the NVMe is present at POST, and an NVMe with a namespace at POST hangs Windows 98's GUI boot at the logo (SeaBIOS hooks it into INT 13h; `WIN98-TEST`, "`x-force-tunnel=on` at boot"; the BIOS matrix there used a blank disk and DOS only). The overlay for this leg therefore carries one of the handoff's three workarounds, chosen by `39-0.7`: `BootGUI=0` and `win` from `AUTOEXEC.BAT` (the route `usb4cm`'s validation used after a DOS-side tunnel), `-boot strict=on` with a boot index on the IDE disk, or a SeaBIOS without NVMe. Then: the volume present with no driver action; the driver binds, the device re-announced approved and left alone; then V.1.2's cycle |
| V.1.4 nothing at boot, controller present | `OFF_OFF`, `OFF_ON` | 0 | the driver binds with an empty table; `device_add` goes through V.1.1's path (with Assist on and nothing attached the controller is absent, section 2, so `ON_*` cannot test this) |
| V.1.5 Assist, no device | `ON_OFF`, `ON_ON` | 0 | the controller absent; nothing to bind; the release notes' limitation read as true |
| V.1.6 disable and enable | `OFF_OFF` | 1 | Device Manager disable: the rings stopped then `driver unloads`, the volume stays; enable: the rebind, the device re-announced approved |
| V.1.7 `XhciCmApprove` 0 and `XhciCmMode` 0 | `OFF_OFF` | 1 | approve 0: announced, not approved, no tunnel, logged; mode 0: bound, nothing sent, nothing announced |
| V.1.8 the dock's xHCI | `OFF_OFF` | 1 | with `usb4-device dock=on` if the tb3 dialect supports it on the i440FX profile (a `39-0.7` reading), or on the bench: the xHCI behind the tunnel bound by `xhci98.sys` and a USB device on it |
| V.1.9 safe eject, the 9x half | `OFF_OFF` | 1 | `39-A.8` on 98 SE (section 7.4): the log's `Change_DevNode_Status` result and `Get_DevNode_Status` of the NVMe devnode before and after the tunnel-up re-enumeration (`DN_REMOVABLE` set on the NVMe devnode and never on a tunnel port); whether 98 SE offers any eject for it; then `device_del` and the volume gone as in V.1.2. A failure here is the NT-only release-notes line, not a checkpoint failure |

The Windows 2000 legs (`39-V.2`) run the same table on the Alpine Ridge
tree the experiment used (`TOPO=ar`, `tools/reenum-test/run2k.sh`'s
device lines, taken into this project's own launcher with its own overlay
of `usb4-emu/vms/win2k/win2k-xonly.qcow2` and its own ports; the Windows
2000 image has a Dvorak layout, which the QMP key helper has a switch for),
plus the filter's install through the INF, its boot-start question, S3 if
the guest offers it, and on the SMP guest under Driver Verifier a bridge
removal (`device_del` of the tunnel bridge's parent is not possible, so the
filter's disable through Device Manager) concurrent with tunnel work, for
the rundown of section 3.2, and a child filter's attach and removal under
the same Verifier run. And the NT half of safe eject (`39-A.8`, section
7.4), on Windows 2000: the NVMe listed in "Unplug or Eject Hardware" with
`Removable` alone; stopped there, its volume flushed and its stack removed,
Device Manager's state for the held device read, the tunnel still up in
the trace; `device_del`, the held devnode gone with no Unsafe Removal
dialog; `device_add`, the new device listed again; a plain `device_del`
with no eject, and whether the dialog shows; a device present before the
driver bound (V.1.3's boot-tunnelled case) not listed until re-plugged;
`XhciCmSafeEject` 0, nothing listed; and the build with both `Removable` and
`SurpriseRemovalOK` set, whether 2000 still lists the device and whether
a plain unplug then shows the dialog - the reading that decides section
7.4's choice for 2000. `39-V.3` reads the install and V.1.1's cycle
on ME, XP, XP x64 (no q35: the i440FX tree), Vista x86 and x64, 7 x86 and
x64, and on each the safe-eject cycle (eject, unplug, re-plug, and a plain
unplug: whether the device is listed, whether any dialog shows); on ME,
section 7.4's 9x reading - what "Unplug or Eject Hardware" lists, and
whether a devnode marked through CONFIGMG is listed and stops cleanly.
Those tiers carry no checkpoint tax: the legs are read and recorded,
and a failure on one is a release-notes line, not a checkpoint failure;
what the cut does require on every target is the install leg from the
asset, which is `40.final`'s, the `3.0.0.0` cut's (decision D7), as it is
for every release. On 7 x64 the native hot-plug path may do the filter's
work first, which is a reading, not a fault.

### 10.3 The import gates and the INF gates

`check-imports.ps1 -AllowPath xhcicm-imports.allow` against the Windows 98
evidence list and the 2000 baselines for x86, and the amd64 allowlist for
the second binary; every import this driver adds is one the HCD already has
evidence for (`PsCreateSystemThread`, `KeStallExecutionProcessor`,
`HalGetBusData`, `IoGetDeviceProperty`, `IoGetDeviceObjectPointer`,
`IoInvalidateDeviceRelations`, `MmMapIoSpace`, `IoGetDmaAdapter`,
`ExAllocatePoolWithTag`, and for the child filters of section 7.4
`IoCreateDevice`, `IoAttachDeviceToDeviceStack`, `IoDetachDevice` and
`IoDeleteDevice`) except the dynalink, which is an instruction, not
an import, and `IoRegisterPlugPlayNotification` with its unregister twin
for section 7.4's item 5, which the HCD does not import: design record 13's
export evidence lists the first as a stock 98 SE import and a Windows 2000
export, and `39-A.7` adds both rows to the allowlists with their evidence. `check-cm-inf.ps1` carries the four INFs' rules, the
`CM-NO9XBRIDGE` rule of section 6.1, and the self-test the other gates have.

### 10.4 The bench (`39-E`)

The owner's P14s Gen 1 with the CalDigit TS3 Plus dock, under Windows 98 SE,
read after the cut as the owner reads every bench:

1. BIOS Assist off, pre-boot off, dock attached at power-on: install the
   package, approve, the dock's PCIe tree appears; the dock's xHCI
   controllers (two FL1100, one ASM1142 [obs] `ICM-PROTOCOL` section 12.1)
   bound by `xhci98.sys`, a stick and a keyboard on them. `XHCISNAP` on
   each.
2. Unplug and re-plug the dock at the desktop.
3. Pre-boot on: the dock's tree at boot with no driver, then the driver
   installed and a hot-plug cycle.
4. **The `$PIR` dump**, by the procedure in `xhciqual/hardware-testing.md`
   ("ThinkPad P14s Gen 1 - does the `$PIR` list the Thunderbolt root
   port?"): whether the P14s BIOS lists `00:1c.4`, which routing source
   Windows 98 uses there, and an actual hot-add. A missing entry, with
   Windows on the `$PIR` source, is evidence for the failure the emulator
   reproduces, not proof of it: the hot-add decides. Where it fails, a
   function hot-added behind the tunnel gets Code 29 on Windows 98:
   `WIN98-TEST` ("Hot-added NVMe gets no IRQ behind 00:1c") read that as
   no IRQ **and no BAR assigned and a driver that never starts**, so
   polling is no remedy for it - design record 15's polled delivery acts
   inside a controller `xhci98.sys` was started on, and Code 29 is a
   devnode `PCI.VXD` never started. That outcome is a bench blocker to be
   recorded as such, and the first cut's release notes would then say
   that on such a machine a hot-added Thunderbolt device needs pre-boot
   tunnelling. Only a controller that does start without a usable line
   (Code 0, dead interrupt) is where record 15's fallback applies, and
   that is a separate reading. Task `39-0.6` added the dump to
   `XHCIQUAL`'s routing report (`xhciqual\route.c`); only the P14s
   reading remains, `39-E.2`'s.
5. The log emission at a driver disable, on the flavour decision D12
   chooses (the `debug` flavour and DebugView by default).
6. Safe eject, where `39-A.8`'s 9x half shipped: the dock's devnodes as
   the log reports them after the tunnel up, and the eject the half offers;
   where it did not ship, nothing beyond step 2's unplug.

Windows 2000 has never run on the P14s (Setup bugchecks, `AGENTS.md`), so
its half of "observed on both" is the VM leg, as for every HCD phase.

Whether the bench is read before or after the cut is not a standing
permission: `AGENTS.md` names the phases that were allowed to cut first
(28 to 31, 35), one decision each. Decision D13 (owner, 2026-10-15) grants
the same to Phase 39, with the pre-cut gates being the host suites, the
gates and the VM legs of sections 10.1 to 10.3 and `39.final`'s dry
build, and the post-cut closure being this section's `39-E.1` and
`39-E.2`, a failure there a re-cut. The cut is `3.0.0.0`, which by decision D7 is `40.final`'s
and carries Phase 40 too, so the bench is read after that cut.

## 11. The owner's decisions

Each was put with a recommendation this record was written to; all thirteen
are taken, D4 on 2026-10-14 and the rest on 2026-10-15, before any code.
Where an answer differs from the recommendation, the sections named were
revised to it in draft 6.

| Id | Decision | Taken | Sections |
|---|---|---|---|
| D1 | One binary or two | **Owner, 2026-10-15, as recommended**: a second binary, `xhcicm.sys` in `src\cm\`, with its own INFs, allowlists and gate | 3.1 |
| D2 | How the NT filter is installed | **Owner, 2026-10-15, as recommended; answered by `39-0.4`**: the device-specific route - a separate NT-only INF (`$Windows NT$`, undecorated Models for Windows 2000, `&CC_0604` lines only, no undecorated install section) whose DDInstall includes `machine.inf` and needs `PCI_DRV`, `PCI_DRV.HW` and `PCI_DRV.Services`, adding a device-specific `UpperFilters` with `pci.sys` kept as the service; unforced on 2000, forced on XP and 7 (the unsigned INF ranks `0x8005` against `machine.inf`'s `0x2004`; the installer forces it, a user by hand uses Have Disk); staged in `nt\`, since Windows 98 SE and ME read `$Windows NT$` INFs. No class filter | 3.2, 3.3, 6.2 |
| D3 | Interrupts | **Owner, 2026-10-15, as recommended**: none; polling only, 200 ms idle; an INTx mode later behind its own value once read on hardware | 4.3 |
| D4 | Approval policy | **Owner, 2026-10-14**: `XhciCmApprove` 0 and 1 only, 1 the default, approve everything, stated plainly as a security property in the release notes; nothing remembered, and the record says why that is harmless; "approve known devices only, from a UUID list under the service key, filled by hand" listed as a later option, not a value | 5.4 |
| D5 | What to do about a controller absent at boot (Assist on, nothing attached) | **Owner, 2026-10-15, as recommended**: nothing; a known limitation; force power is an ACPI WMI method out of reach on 98 and unread on NT | 1.3, 2 |
| D6 | The `nvme98` notification channel | **Owner, 2026-10-15, as recommended**: the proposal of 7.2, settled with `nvme98` after the first checkpoint, shipped as `39-A.7`; the statement withheld on any OS until gate G3's ordering is qualified there. Settled by `nvme98` on 2026-10-15 (its MD-6): the link-state table of section 7.2 replaced the proposal | 7.1, 7.2 |
| D7 | The version and the cut | **Owner, 2026-10-15, changed**: `3.0.0.0` ships both this work and the USB4 software connection manager (Phase 40), not the latter as the next minor. `39.final` closes Phase 39 with its legs and a dry build and cuts nothing; `40.final` carries the `3.0.0.0` cut, the release notes for both phases and the install legs from the asset | 1.2, 10.2, 10.4, `roadmap-usb4.md` |
| D8 | Names | **Owner, 2026-10-15, as recommended**: `xhcicm.sys`, `xhcicm.inf` and `xhcicmf.inf`, class System. The device descriptions were revised by the owner the same day (first form: `xHCI98 Thunderbolt 3 Host Interface` and `xHCI98 Thunderbolt 3 Tunnel Port Filter`): by generation, `xHCI98 Thunderbolt 2 Controller`, `xHCI98 Thunderbolt 3 Controller` and `xHCI98 USB4 Host Router` for the NHI, `xHCI98 Thunderbolt PCI Bridge`, `xHCI98 USB4 PCI Bridge` and `xHCI98 PCI Bridge` for the ports the filter attaches to, and the service `xHCI98 Thunderbolt and USB4 Connection Manager (xhcicm)` | 3.3 |
| D9 | Other Alpine Ridge NHI ids | **Owner, 2026-10-15, changed**, then widened the same day: every Alpine Ridge NHI id and the Titan Ridge NHI ids are claimed, ids from the public PCI id listing with the source in `legal-provenance.md`, with the matching port ids for the filter INF; Titan Ridge gets a best-effort row; every id but `15BF` is untested, the README and release notes say so plainly, and the untested ids act by default with that warning (`XhciCmMode` 1). By the owner's source rule of the same day (section 9) the profiles are read from Linux, and the coverage became every Intel firmware-connection-manager host Linux addresses - Falcon Ridge, Alpine Ridge, Titan Ridge, Maple Ridge, Ice Lake to Meteor Lake - with four-test selection, an inert start, hardware profiles apart from message profiles, per-domain tunnel-port sets and a no-reset policy (sections 1.4.2 to 1.4.6, 3.3, 6.3); the widening, the selection and the inert start are orchestrator decisions of 2026-10-15 for the owner to confirm. `XhciCmMode` 2 with a user-edited INF copy stays the route for an id the list does not carry | 1.3, 1.4, 3.3, 5.1, 5.2, 5.3, 6.3, 8, 9, 10.1 |
| D10 | Safe eject | **Owner, 2026-10-15, changed**: in Phase 39, task `39-A.8`; on NT the filter answers `IRP_MN_QUERY_CAPABILITIES` with `Removable` (and `SurpriseRemovalOK`, so a plain unplug shows no Unsafe Removal dialog); on 98 SE and ME through CONFIGMG, best effort; NT-only with a release-notes line if the 9x half cannot be made to work; the eject stops the drivers and the user unplugs, since under the ICM there is no per-device teardown. Section 7.4 applies it with two refinements for the owner to confirm, each from a fact the decision did not have in front of it: the mark goes on the devices reported on the tunnel bridge's bus (through child filters the bridge filter attaches), not on the bridge, which never leaves the machine and would be held for eject until a restart; and `SurpriseRemovalOK` is set only where leg `39-V.2` shows Windows still lists the device with it, since Microsoft's reference says a device with both bits is not shown in "Unplug or Eject Hardware" | 1.3, 3.2, 6.2, 7.4, 8, 10.2, 12 |
| D11 | A master switch for the whole driver | **Owner, 2026-10-15, as recommended**: `XhciCmMode`, default 1 (auto), 0 off, 2 force the ICM client at the user's own risk, 3 reserved for the software connection manager; the install is the real opt-in, so the default is on. Value 3 has since been given its meaning by the orchestrator's coverage decisions of the same day, for the owner to confirm: the software connection manager's experimental override (section 1.4.2, design record 22) | 8 |
| D12 | The log emission | **Owner, 2026-10-15, as recommended**: nothing in `release`, `#if DBG` in `debug` and `qemu`, keeping `AGENTS.md`'s two-site `DbgPrint` rule as it is. **Reversed for `release` by the owner on 2026-10-16**, for consistency with `xhci98.sys`: every flavour prints under `XhciCmLogDebugView` 1, address values withheld below level 4 | 8 |
| D13 | Cut before the bench for Phase 39 | **Owner, 2026-10-15, as recommended**: the VM legs and gates before the cut, `39-E.1` and `39-E.2` after it on the asset, a failure there a re-cut; the cut being `3.0.0.0` at `40.final` (D7) | 10.4 |

## 12. What is open, and what is a risk

- **The P14s `$PIR`** (section 10.4): the fact most likely to decide
  whether a device hot-added behind the tunnel is started at all on
  Windows 98 on the real machine. A missing entry is evidence, and the
  hot-add the bench performs decides; Code 29 there is a devnode never
  started, which no driver below it can mitigate. The pre-boot path,
  where the BIOS assigns the dock's resources, is the fallback the
  release notes would name.
- **The G3 statement** (section 7.1): withheld on every OS until the
  emulator's ordering check exists there (on i440FX through the documented
  stage-3 surprise-removal presence change, or on q35 through a real Link
  Active bit), and withheld on hardware until the completed-reset
  boundary - unobservable through a down link, so unresolved by this
  record - is settled by `nvme98`'s MD-2 verification and the owner.
- **The NT filter's INF route** (D2): untested; the experiment typed a class
  filter by hand.
- **Every row but AR is uncaptured** (section 1.4): FR, TR, Maple Ridge and
  the ICL row are to be code written to Linux's structures [linux],
  host-tested against frames synthesised from those structures, and read
  on no hardware, and on no emulator but the TR row, which `usb4-emu`'s
  Titan Ridge model (section 10.2), itself written from Linux, runs. The
  inert start and the tolerance rules of sections 1.4.2 and 1.4.6 bound
  the damage of a wrong row to "does nothing, says why"; they do not make
  a row work.
- **The integrated controllers' platform** (section 1.4.3): whether a
  Tiger Lake or later machine booted through a CSM leaves the controller
  powered and its firmware connection manager running is unread; ACPI
  power, `_OSC` and retimer methods are out of reach on Windows 98, and the
  root-port tunnel ids rest on the public listing alone, which lacks
  Meteor Lake's.
- **The tunnel-port set** (section 6.3): the PCI mapping from a domain to
  its ports comes from ids and topology, not from the platform's own
  description, and the event-to-port mapping is not read, so a set of more
  than one costs extra re-enumerations and withholds the `nvme98`
  statement.
- **Safe eject** (section 7.4): the NT child filters are a bus-filter
  attachment this project has not made before (Driver Verifier on the SMP
  guest reads it); whether Windows 2000 lists a device with both
  `Removable` and `SurpriseRemovalOK` set decides the 2000 path; the 9x
  half rests on `Change_DevNode_Status` setting `DN_REMOVABLE` and on what
  ME's applet lists, both unread.
- **Resume ordering** (section 6.3): the NT hold orders a successful
  resume and is unread; a failed one is not ordered, so suspend with a
  tunnelled device attached is unsupported on every target in this
  release; Windows 98 has no such hook.
- **The dock's PCIe switch** (`8086:15d3`, `ICM-PROTOCOL` section 10.1) and
  its five downstream ports appear behind the tunnel on the real machine,
  where the emulator shows one NVMe: `pci.sys` and `PCI.VXD` enumerate
  switches below a re-enumerated bridge on their own, but nothing here has
  read a re-enumeration two bridge levels deep on Windows 98 (the experiment
  read it one level deeper on 2000 only).
- **Windows 98's instance ids and resource assignment** for a function
  hot-added behind two bridges under `PCI.VXD`: the experiment's one bridge
  worked; the switch's windows must hold the dock's.
- **The CONFIGMG service numbers**: from the DDK header before they ship,
  with the run-time guards regardless (section 6.1).
- **Windows ME**: the CONFIGMG route is the same VMM and the same service
  table, and `Get_Version` reads `0x04xx` there too; untested until its leg.
- **[inf] facts the driver depends on**: the mailbox handshake's polling
  rule, the host-router poll after `driver ready`, the 500 ms reconciliation,
  the resume without mailbox `0x23`. Each is tolerated as a timeout or a
  no-op if wrong; none can hang the thread.
- **Pre-OS state the BIOS may leave**: a `driver ready` with no prior
  `driver unloads` from a previous boot's driver is the normal case [obs] and
  needs nothing; a controller in safe mode has not been seen.
- **Memory above 4 GB on the x64 guests**: the common buffer is refused
  above 4 GB as the HCD's is; the rings must land below it.

## 13. What this leaves the USB4 software connection manager

Phase 40's design is design record 22, written beside this one. The pieces
that survive into it unchanged: the second binary and its gates; the ring
code (`tbt_ring.c`, which `u4hi.c` already was); the polling thread and its
queue; the hardware profiles of section 1.4.3, the integrated controllers'
force power and LTR among them, which its software-only Intel generations
need too; the four-test selection and the inert start of section 1.4.2,
whose positive software-connection-manager admission design record 22
defines; the
tunnel-port sets and both re-enumeration routes; safe eject; the `nvme98`
channel; the registry and counter conventions. What it adds is the
connection manager proper (`u4cm.c`'s work, done in the driver against the
`usb4-generic` profile with both host models and `preos-cm=on`), the
pre-OS tunnel discovery and preservation section 1.4.5 asks of it, the
router quirks section 10.1's matrix assigns to it, the legacy-router work
a Thunderbolt 3 device behind a USB4 host needs (Linux configures such a
router router by router: `switch.c`, `tb_switch_configure`; `lc.c`;
`tunnel.c`, `tb_pci_init_credits`), PCIe tunnel first, and - since under a
software connection manager this driver does tear paths down itself - the
real work of obligation 2 in section 7. The handoff's warning stands for
it: write it to depend on no exact device router, treat every router
operation as possibly unsupported, discover adapter numbers rather than
assume them, and poll completions with timeouts.

## Sources

- `usb4-emu/docs/XHCI98-HANDOFF.md`, `ICM-PROTOCOL.md`, `PLATFORM-MATRIX.md`,
  `REENUM-EXPERIMENT.md`, `WIN98-TEST.md`, `ARCHITECTURE.md`,
  `NVME-DRIVER-EVAL.md`, `ROADMAP.md`, `AGENTS.md`, `tools/usb4cm/README.md`,
  `tools/reenum-test/README.md` and `reenum.c`, `scripts/run-win98.sh`
  (branch `main`, `eba0cf1`, read 2026-10-14).
- `nvme98/docs/PLAN.md` section 0.10, gate G3 and decisions MD-1 and MD-2
  (branch `main`, read 2026-10-14).
- `nvme98/include/nvme98cm.h` (commit `d08ddadb`), `nvme98/docs/gates/G3.md`
  and `docs/PLAN.md`'s decisions MD-5 and MD-6 (`main` at `52f2ad67`, read
  2026-10-15): the adopted channel of section 7.2 as first carried.
- `stor98/include/stor98cm.h` (carried unchanged as `src/cm/stor98cm.h`),
  `stor98/docs/RENAME-STOR98.md` and the `0.3.0.0` release
  (`releases/0.3.0.0`; `main` at `3e9f51b`, read 2026-10-16): the rename,
  interface version 4 and interface 3 of section 7.2.
- This repository: `AGENTS.md`; design records 13 (sections 5.1, 5.2, 7.5),
  15, 18 and 19; `docs/usb-xhci-info/win98-wdm.md`; `src\hcd_entry.c`,
  `src\hcd_svc.c`, `src\hcd_ctl.c`, `src\uas\sources`, `src\xhci98.inf`.
