# Design Record 21 - Thunderbolt 3 Firmware Connection Manager Client and Tunnel Re-enumeration

Status: draft 4, 2026-10-14, before code; Codex review rounds 1 to 3
(session `01a13afc`, 13, 10 and 5 findings) folded in, and the owner's
decisions of the same day on approval and the master switch (section 11).
Written for the
planned work `TB3` in `roadmap-usb4.md` ("Planned - Thunderbolt 3: the ICM
Client and Tunnel Re-enumeration"), which takes the next free phase number
when the owner opens it. Nothing in `src\` implements it yet. Section 11 lists
the decisions that are the owner's, each with a recommendation; section 12
lists what is open. The record is to be reviewed to convergence and the
decisions taken before the first `-A` task, as design records 14, 15, 18 and
20 were.

The sources this record rests on are in the `usb4-emu` repository
(`D:\OneDrive\git-projects\usb4-emu`, branch `main`, commit `eba0cf1`), which
this project reads and never modifies: `docs/XHCI98-HANDOFF.md` (the
handoff), `docs/ICM-PROTOCOL.md` (the Alpine Ridge host contract, cited below
as `ICM-PROTOCOL` with its section and requirement numbers),
`docs/PLATFORM-MATRIX.md`, `docs/REENUM-EXPERIMENT.md`, `docs/WIN98-TEST.md`,
`docs/ARCHITECTURE.md`, `docs/NVME-DRIVER-EVAL.md` and `tools/usb4cm/`.
Every hardware fact below keeps the tag it carries there: **[obs]** seen in a
P14s Gen 1 capture, **[inf]** inferred from the Linux client's behaviour and
not yet confirmed by a capture. An [inf] fact is a hypothesis this driver is
written to tolerate being wrong, not a fact it may rely on.

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
planned work (`U4CM`). Nothing here waits for the ORICO capture.

### 1.3 Scope

In:

- Binding the Alpine Ridge NHI on Windows 98 SE and Windows 2000 SP4 (the
  primary targets) and on the virtual-machine tiers (ME, XP, XP x64, Vista and
  7 in both architectures) under the standing rule for those tiers.
- The ICM client, polling only: firmware status, operating mode, the NHI
  mailbox (`allow all devices`, `driver unloads`), ring 0 in raw mode,
  `driver ready`, `device connected`, `get route`, `approve device`,
  `device disconnected`, with the timeouts and retries of `ICM-PROTOCOL`
  section 3.
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

Out, each with where it goes:

- The USB4 software connection manager (`U4CM`, section 13).
- Titan Ridge and later Intel generations: not captured, not modelled, a
  different message set (`ICM-PROTOCOL` section 4.9). The INF matches only the
  ids section 3.3 lists, so a Titan Ridge machine is simply not claimed.
- Force power (powering a controller that the BIOS left off because nothing
  was attached at boot, `ICM-PROTOCOL` section 11.2): it is an ACPI WMI
  method, which Windows 98 in APM mode cannot reach at all and which nothing
  in this project has read on NT. A controller that is absent at boot stays
  absent; the release notes say so ("BIOS Assist on with nothing attached").
- The boot ACL, NVM versions and the DMA port, DisplayPort, host-to-host
  links, security levels `secure`, `dponly`, `usbonly` and `nopcie`, the
  firmware-start path of `ICM-PROTOCOL` section 2.7, and interrupts (MSI,
  MSI-X or INTx) on the NHI.
- Safe eject of a tunnelled device: not wired in the first cut (section 7.4).
- The dock's own devices: nothing here drives an xHCI, an NVMe or a NIC. The
  xHCI behind the tunnel is `xhci98.sys`'s by class, as any other.

### 1.4 How generic a Thunderbolt 3 client can be (owner's question, 2026-10-14)

The owner asked for a best effort at a generic Thunderbolt 3 and USB4
driver. Three layers, with three different answers:

- **The host interface and the re-enumeration are generic.** The NHI ring
  registers follow the USB4 specification's common layout (section 4.1), the
  ring code is the one the USB4 software connection manager will use
  (section 13), and the tunnel-bridge discovery and both re-enumeration
  routes (section 6) do not depend on which controller sits above the
  tunnel. Nothing in them is Intel's.
- **The ICM client is generic across devices and per-generation across
  the Intel hosts it addresses.** The firmware owns the device side, so
  every Thunderbolt 3 dock, enclosure or cable works through the same
  client with no per-device code (the handoff, section 2, "Other
  Thunderbolt 3 hosts"). The host side this record addresses is Intel's
  firmware-ICM families - the discrete Thunderbolt 3 controllers in PC
  laptops and boards; Apple's own Thunderbolt controllers in its M-series
  machines are a different design and out of scope - and it differs by
  Intel generation: Alpine Ridge (this record, captured) and Titan Ridge
  use different driver-ready, device-addressing, approve and disconnect
  layouts, and differ in **which operations exist** - Titan Ridge carries
  the route in its events and has no `get route`, where Alpine Ridge must
  ask for it (`ICM-PROTOCOL` section 4.9) - and the Ice Lake and later
  integrated controllers add messages and platform methods of their own
  (the RTD3 veto event, force power through ACPI). What is shared is the
  lifecycle: bind, announce, approve, disconnect, unload, resume. What is
  per generation is a table of protocol operations and capabilities - the
  encoders and decoders of section 5.2, and flags such as "needs get
  route" that section 5.3's sequence consults - selected by the NHI's
  device id at `AddDevice`. The design keeps that table from the start, so
  a Titan Ridge or integrated-controller row is an addition, not a fork;
  but each row needs a capture of that generation before it can be
  claimed, and only Alpine Ridge has one (decision D9 is the id list for
  this generation alone; `XhciCmMode` 2 in section 8 is the experimental
  route for an id not yet listed, and it needs the INF to match that id
  first).
- **Real USB4 hosts are not this at all.** AMD, ASMedia and Intel under the
  operating system's native control run a software connection manager,
  which is the `U4CM` work; whether a USB4 BIOS booted through a CSM runs a
  firmware connection manager for a legacy operating system, and with which
  message set, is uncaptured (`PLATFORM-MATRIX` section 4), and an unknown
  firmware connection manager is **unsupported**: its existence would not
  make it Intel's ICM. The kind is chosen in two steps: at `AddDevice`, a
  candidate profile from the vendor and device id the INF matched (the
  class code, `0x0880` for an Intel Thunderbolt host interface and
  `0x0C0340` for a USB4 host interface, classifies and proves nothing);
  after `IRP_MN_START_DEVICE` has delivered BAR0, the profile's own
  register contract confirms it (section 5.1's firmware status, mode and
  version checks for the ICM profile), and a mismatch refuses the start.
  Nothing is read from the hardware before start.

So the honest claim for the first cut is "any Thunderbolt 3 device, on an
Alpine Ridge host"; "any Intel Thunderbolt 3 host" is one capture per
generation away, on the same driver.

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
  dump a bench reading.

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

### 3.2 Two PnP roles in one binary

`AddDevice` tells them apart by the PDO's ids, as `reenum.sys` did in the
experiment and as `hcd_entry.c` tells its roles apart:

1. **The NHI function driver** (every target). The FDO owns BAR0, the rings,
   the ICM client state machine and the connection-manager thread. It is
   installed by the INF's model line for the NHI id.
2. **The tunnel bridge's upper filter** (NT targets only). A filter device
   object attached above the PDO `pci.sys` created for the switch's tunnel
   downstream port. It does nothing to the IRPs it sees; it exists to hold a
   pointer to that PDO so that the NHI FDO can ask it to call
   `IoInvalidateDeviceRelations(Pdo, BusRelations)` (section 6.2). It is
   installed by a separate NT-only INF as a device-specific `UpperFilters`
   value on the port's id (section 3.3, decision D2), and never by any INF
   Windows 98's engine reads: on Windows 98 a WDM driver on a bridge devnode
   breaks the bridge (section 6.1).

The two roles find each other through a registration list in the driver's
own image. The lifetime protocol, which review round 1 asked for: a filter
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

Four INFs, two per package, because of a fact review round 1 raised: Windows 2000 reads the
**undecorated** Models section and decorates only the DDInstall section
(which is why `xhci98.inf` shares `[XhciModels]` between 9x and NT 5.x), so
a Models section that is safe for Windows 98 cannot also carry a line for
the bridge on Windows 2000. The split:

- `xhcicm.inf`, the NHI's, follows `xhci98.inf`'s shape:
  `Signature="$CHICAGO$"`, one `[Manufacturer]` line
  `%Mfg%=CmModels,NTx86.6.0`, one undecorated `[CmModels]` with the NHI line
  alone, undecorated DDInstall sections with `DevLoader=*NTKERN` and
  `NTMPDriver=xhcicm.sys` for Windows 98's 16-bit engine, `.NTx86` sections
  with `AddService` for 2000 and XP, `.NTx86.6.0` sections for Vista and 7
  that copy the driver alone. It never names a bridge.
- `xhcicmf.inf`, the filter's, NT only: `Signature="$Windows NT$"`, which
  Windows 98's engine ignores outright - the INF gate's own `BOTH-VERSION`
  text records that fact (`check-inf.ps1`), and `TB3-0.4` reads it on 98 SE
  and ME with the file present beside the other - with an undecorated
  `[CmFilterModels]` carrying
  `%TunnelDesc%=CmFilter.Dev,PCI\VEN_8086&DEV_15C0&CC_0604` for Windows
  2000, and `.NTx86.6.0` sections for Vista and 7. Its DDInstall adds the
  `UpperFilters` value and the service and installs no function driver.
- `xhcicm-amd64.inf`, the 64-bit package's, carries the `NTamd64,NTamd64.6.0`
  pair for the NHI and no undecorated section, held by the gate's
  `PATH-NO9X` and `PATH-MFGDEC` rules as the other amd64 INF is; the filter's
  64-bit half is `xhcicmf-amd64.inf` on the same pattern.

`LayoutFile` is not needed by any of them: the driver imports nothing the OS
has to supply. The gate for the four is `check-cm-inf.ps1`, with the rule
`CM-NO9XBRIDGE`: no bridge id in any `$CHICAGO$` INF of this driver, and
the filter INF's signature held to `$Windows NT$`.

Model lines, as the engines see them:

| Engine | What it finds |
|---|---|
| Windows 98, ME | `xhcicm.inf` `[CmModels]`: `%NhiDesc%=Cm.Dev,PCI\VEN_8086&DEV_15BF`; `xhcicmf.inf` ignored by signature |
| Windows 2000, XP | the same NHI line through `Cm.Dev.NTx86`; `xhcicmf.inf` `[CmFilterModels]`: the bridge line |
| Vista, 7 | the `.NTx86.6.0` halves of both |
| x64 | the two amd64 INFs |

The filter line matches all three downstream ports and the upstream port,
since the captured switch gives every port the same id [obs] `ICM-PROTOCOL`
section 1. The filter attaches to each and acts only on the one the NHI
names, so matching the siblings costs nothing. Whether a vendor-id match
from this INF can add an `UpperFilters` value to a port that `pci.inf`
already serves, without displacing `pci.inf`'s own install, is the
`TB3-0.4` reading that decision D2 waits on: the candidate shapes are an INF
section that includes and needs `machine.inf`'s PCI-to-PCI bridge section
and adds the value, or a class filter, which must then be boot-start or the
next boot stops with `0x7B` (`REENUM-EXPERIMENT`, "Windows 2000 SP4",
mechanism A). The device-specific route is the one to prove first, because a
System-class filter touches every bus on the machine. The packaging stage
and `make-package.ps1`'s footprint files carry both INFs per flavour.

Only `15BF` is matched in the first cut, since it is the one NHI id this
project has read [obs]. The other Alpine Ridge NHI ids are taken, if the
owner wants them, from a public PCI id listing and recorded in
`legal-provenance.md` with their source; they are never taken from the Linux
Thunderbolt driver (section 9).

Class and names: `Class=System` with its GUID on the NT paths and `Class=System`
undecorated for 98 (which also knows that class); device descriptions
`xHCI98 Thunderbolt 3 Host Interface` for the NHI and
`xHCI98 Thunderbolt 3 Tunnel Port Filter` for the filter, pending the
owner's spelling (decision D8). `StartType=3` (demand) for the NHI service,
as for `xhci98`; the filter is the same service, demand-started with the
devnode it filters, since it is device-specific and not a class filter.

Registry values, written by every install path at the driver's default under
the don't-overwrite flag, as the 2026-10-11 rule requires for every value
the driver reads: section 8.

### 3.4 Text-mode Setup

None. The NHI is never a boot-critical device for Setup, and nothing behind a
tunnel is one in this project's targets. `txtsetup.oem` is not touched, and
the `OEM-*` gate rules stay as they are.

## 4. The host interface: ring 0, polled

### 4.1 Registers

The driver maps BAR0 (256 KiB, `MmMapIoSpace`, non-cached) and reaches it
through `READ_REGISTER_ULONG` and `WRITE_REGISTER_ULONG`, as the HCD does. The
offsets it touches, every one from `ICM-PROTOCOL` section 2.1 [obs] unless
noted, are kept in one pure header `tbt_regs.h` with shift-and-mask macros
and no bitfields:

| Register | Offset | Use |
|---|---|---|
| host capabilities | `0x39640` | ring count in bits 9:0, interface version in 23:16 (0 on Alpine Ridge; a nonzero version is refused in the first cut, section 5.1) |
| firmware status | `0x39944` | bit 0 ICM enabled, bit 31 NVM authentication done |
| outbound mailbox | `0x3990c` | operating mode in bits 11:8 |
| inbound mailbox data and command | `0x39900`, `0x39904` | command in bits 7:0, error bit 30, request bit 31 |
| ring interrupt enable 0 | `0x38200` | left at 0: this driver polls |
| TX ring 0 base, producer/consumer, size, options | `0x00000` block [inf] generic NHI layout, `0x0000c` size, `0x19800` options [obs] | |
| RX ring 0 base, producer/consumer, size, options, PDF masks | `0x08000` block [inf], `0x0800c` size, `0x29800` options, `0x29804` masks [obs] | |
| DMA misc | `0x39864` | read for the log only; the auto-clear bit is an MSI-X matter |

The generic ring register block offsets are the USB4 specification's
(section 12.6) and FreeBSD's `nhi_reg.h` cross-checks them (`ICM-PROTOCOL`
section 2 says so); the `TB3-0` batch transcribes the exact offsets this
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

### 4.4 The connection-manager thread

One system thread per NHI, created at start and ended at stop, at
PASSIVE_LEVEL, as `hcd_ctl.c`'s controller thread is, with the same Windows
98 care around `PsCreateSystemThread`'s handle (the `IoIsWdmVersionAvailable`
branches at `hcd_ctl.c:2514`). Every ICM command, every mailbox handshake,
every wait and every re-enumeration call runs on it; nothing else touches
the rings. Work reaches it through a small queue under a spin lock. PnP
requests (start, stop, remove) queue their work and wait on an event, as
PnP dispatch may. **Power requests never wait**, and the NHI FDO is the
device's power policy owner, so its sequence is `hcd_power.c`'s, not a
filter's pass-through (review round 2, finding 1): a system power IRP is
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

Priority inside the thread (review round 1, finding 8): the busy-poll of an
outstanding command services every RX frame as it lands, so an event
arriving during a command is decoded at once, not after the command. A
`device disconnected` decoded during a command runs its link-down
notification (section 7) immediately, inside the wait, and queues the
removal and re-enumeration of section 6 for after the command completes.
That removes the one blockage a command wait would add. The end-to-end
budget for the statement, against `nvme98`'s gone deadline (5 s in its
test build, measured from the endpoint's own presence loss), is section
7.2's; it needs more than this paragraph, since the idle poll, the port
reading and the recipient lookup each cost time of their own.

## 5. The ICM client

### 5.1 Bind, on the thread (`ICM-PROTOCOL` section 6.1)

1. Enable Bus Master in the NHI's command register through
   `IRP_MN_WRITE_CONFIG` to its own PDO, the route `hcd_svc.c` proved on
   Windows 98 SE and Windows 2000 (its header comment, 26-A.2).
2. Read host capabilities. Ring count below 1 or interface version above 0:
   refuse the start with a log line and Code 10. The version refusal is
   deliberate: a version that asks for the host-router reset handshake is a
   generation this record has not read.
3. Read firmware status. ICM enabled clear: refuse the start. This driver
   never starts stopped firmware (section 2.7 of the contract is out of
   scope).
4. Poll firmware status for NVM authentication done, up to 60 reads 50 ms
   apart [inf]; never set: refuse.
5. Read the operating mode. 3 proceeds; 0 (safe mode) starts with no rings
   and a log line saying the controller is in safe mode; any other value
   refuses the start [inf].
6. Allocate and start TX and RX ring 0, raw mode, PDF mask all ones, every
   interrupt enable at 0.
7. Mailbox `0x23` allow all devices: write the data word 0, then the command
   with the request bit, poll every 10 to 20 us for up to 500 ms until the
   request bit clears; error bit set or timeout: refuse the start [inf,
   observed as the end state].
8. Send `driver ready` (one header word, code 3, flags 0) and wait for its
   two-word response (5 s, four tries). Keep the flags byte, the security
   level and the boot-ACL fields for the log; act on none of them in the first
   cut.
9. Poll the host router: a config read of router-space dword 0, route 0, one
   dword, 100 ms, up to 50 tries 50 ms apart, draining and queueing any
   `device connected` events that arrive meanwhile [inf, consistent with the
   observed 580 ms hold]. Never answered: log it and carry on - the events
   are what matter, and the emulator's R8 says config requests are delayed,
   never dropped.
10. Process the queued events (section 5.3).

What is not done at bind, by design: no host-router enumeration, no
capability-chain walk, no EEPROM bit-bang, no DROM, no DMA port, no NVM
version. The Linux client reads all of that [obs] section 5.5; the Windows
11 driver logs NVM versions and so probably reads the DMA port [inf]
section 9 item 3; this driver needs none of it to approve a device and
re-enumerate a bus. `ARCHITECTURE` section 7 says as much for the legacy
client: the minimum set is allow-all, driver ready, get route and approve,
and the names in the event are enough.

### 5.2 Frames

A pure `tbt_icm.c` encodes requests and decodes responses and events from
the word layouts of `ICM-PROTOCOL` sections 3.1 and 4, host-tested against
the observed frames quoted there: `driver ready` (`T:1`, `T:2`), `get route`
(`T:833`, `T:834`), `approve` (`T:34153`, `T:34154`), `device connected`
(`T:9`, 61 words, the name block decoded entry by entry), `device
disconnected` (`T:21087`). Frame type 11 out, 12 and 10 in; the CRC-32C
trailer is appended to every command and stripped unchecked from every
response and event, as the Linux client does [inf] and the emulator sends it
anyway. One command outstanding at a time; a response matches by frame type
and code; an event arriving while a command is outstanding is decoded at
once and acted on as section 4.4 says, never dropped. Error is bit 0 of the
response flags; an unknown event code is logged and ignored. The encoders
and decoders sit behind one per-generation table (section 1.4) with the
Alpine Ridge row filled; the table is indexed by the NHI device id at
`AddDevice`, and `XhciCmMode` 2 maps an unknown Intel id onto the Alpine
Ridge row.

### 5.3 Events and the device table

The driver keeps a table of announced devices, 16 entries (the boot ACL's
size, and more than any one port can carry at depth 1), each with the UUID,
connection key and id, link, depth, the approved, rejected and boot bits,
the vendor and model strings from the name block, and the route string from
`get route`. On `device connected`:

- `rejected` set: log and ignore, as the client does [inf].
- A known UUID at the same link, depth and approval state: refresh key, id
  and route, nothing else (the resume case, [obs] section 6.5).
- Otherwise: drop any stale entry at that link and depth, send `get route`
  where the generation's table says the route must be asked for (Alpine
  Ridge; Titan Ridge carries it in the event, section 1.4), store the
  route, and apply the approval policy (section 5.4).

On `device disconnected`: drop the entry at that link and depth (warn if
none), then run the tunnel-down sequence of section 6.5. The firmware sends
nothing for an unplug while the driver was unloaded [obs] section 6.6, so
after every `driver ready` the table is reconciled: an entry not re-announced
within 500 ms of the host-router poll succeeding is treated as gone and the
tunnel-down sequence runs for it [inf, the client's own 500 ms check].

### 5.4 Approval policy (decision D4, taken 2026-10-14)

The domain's security level is `user` on the P14s [obs]: every newly plugged
device is announced unapproved and gets its tunnel only on `approve`.
Windows 98 and 2000 have no Thunderbolt control panel and this project will
not write one, so approval is a policy, the registry value `XhciCmApprove`:

| Value | Meaning |
|---|---|
| 1 (default) | approve every announced device that is not rejected, at once, on every announcement with `approved` = 0 |
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

`approve` carries the UUID, connection key and id of the announcement, waits
10 s, and on error flags leaves the entry unapproved with a log line. A
success means the firmware built the PCIe tunnel while processing the
request [obs] section 4.3, so the tunnel-up sequence of section 6.5 follows
at once.

What 1 means for the user is written plainly in the release notes: any
Thunderbolt 3 device plugged into the machine gets PCIe access, which on a
machine without an IOMMU (every target here) is access to all of memory.
That is the state every pre-boot-tunnelled device is already in on these
systems, and what the Windows 11 service does for a device it has seen once
[inf] section 10.3, but it is an owner decision and a documented one.

### 5.5 Timeouts, retries and faults

5 s per command, three retries on timeout, 10 s for `approve` [inf]. A
command that never answers after its retries fails the operation, never the
driver: the start fails only on the bind steps of section 5.1; a later
failure logs, drops the device entry if any, and leaves the thread polling.
A frame longer than its buffer, a response with an unexpected code, or a
CRC mismatch on a command the firmware reports as an error are counted, not
acted on. Counters: section 8.

### 5.6 Stop, remove, disable, suspend and resume

Three cases the contract keeps apart (review round 1, finding 3), each in
the order the capture or the client shows:

- **Unbind** (stop, remove, disable): ring 0 stopped (options cleared,
  interrupts already 0), then mailbox `0x07` driver unloads [obs] section
  6.7, that order; the thread ended, the common buffer freed. Tunnels and
  approvals survive it [obs] section 2.3, so the dock keeps working across a
  driver disable, as it does under Linux's unbind. Nothing behind the tunnel
  is removed by this driver at its own stop.
- **Shutdown**: the unbind sequence, if the stop has not already run it.
- **System suspend** (`IRP_MN_SET_POWER` to D3 for S3 or S4): rings stopped,
  then mailbox `0x05` save devices, then `0x07` [inf] section 2.3 - the
  client's system-suspend order, not the runtime-suspend order of section
  6.5, which has no `0x05`. Resume to D0: the bind sequence from step 6
  without the mailbox `0x23` [inf] section 6.5 (the one place the
  runtime-resume capture is taken as the model, because no system-resume
  capture exists), and the reconciliation of section 5.3, so a device
  unplugged during the sleep is torn down and one that stayed is refreshed.
  Both [inf] steps are tolerated as a mailbox error or a timeout if the
  firmware refuses them. Windows 98 on the APM machines this project runs
  rarely reaches S3; the Windows 2000 and later guests do, and the VM legs
  read it there.
- **Runtime suspend** (RTD3): never requested by this driver, whatever the
  driver-ready flags say, so the `0x07`-alone sequence of section 6.5 is
  never sent.

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
`0x0033`; its services are cdecl and return `CONFIGRET` in `EAX`. The
services used:

| Service | Number | Use |
|---|---|---|
| `Get_Version` | 0 | the guard: the high byte must read 4 (Windows 98's 4.0B, ME's 4.90 both answer `0x04xx`); otherwise the CONFIGMG route is disabled for the driver's life and re-enumeration falls back to section 6.4 |
| `Locate_DevNode` | 2 | the root devnode (`HTREE\ROOT\0`), a second guard: its id must read back as that string |
| `Get_Child`, `Get_Sibling` | 4, 5 | the tree walk |
| `Get_Device_ID` | 7 | match `PCI\...\BUS_bb&DEV_dd&FUNC_ff` for the tunnel bridge, and `PCI\VEN_vvvv&DEV_dddd` for a vanished child |
| `Query_Remove_SubTree`, `Remove_SubTree` | 12, 13 | a child whose function has left the bus, before the re-enumeration |
| `Reenumerate_DevNode` | 51 | the bridge, after a tunnel up or down |
| `Get_DevNode_Status` | 54 | the log only |

The experiment took these numbers from memory of the Windows 95/98 DDK's
CONFIGMG service table and confirmed each at run time (`Get_Version` returned
`040Bh`; the root id read back; `Get_DevNode_Status` of the NVMe agreed with
`CM_Get_DevNode_Status` from user mode). This record does not ship numbers
taken from memory: task `TB3-0.3` fetches the Windows 98 DDK, records its
name, version and SHA-256 in `docs/references/README.md` under the rule that
the artifact is never tracked, reads the service table from its
`CONFIGMG.H` (`Begin_Service_Table` order), and writes the numbers into
`cm_cfgmg.h` with that citation. The run-time guards stay in the shipping
driver regardless, because a wrong number here is a call into the wrong
VMM service.

Service numbers are the only thing taken from the DDK header; no DDK code
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
never by instance id.

Reads of the secondary bus for "what still answers" go through
`HalGetBusData(PCIConfiguration, bus, slot, ...)`, which `hcd_ctl.c` already
uses on bus 0 on every target and whose Windows 98 evidence is recorded
there (`hcdPirqSlot`'s comment); never through port `0xCF8`/`0xCFC` directly,
which the experiment did and its own caveats warn against.

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
reports the vanished one missing (surprise removal, with the "Unsafe Removal
of Device" dialog on every tunnel down, which the experiment saw on every
cycle and this record accepts for the first cut; section 7.4).

The filter does no device work: PnP IRPs other than the four it watches
go down with `IoSkipCurrentIrpStackLocation`; power IRPs go down the way
NT 5.x requires of a filter, `PoStartNextPowerIrp` then
`IoSkipCurrentIrpStackLocation` and `PoCallDriver` (the experiment's
`reenum.c` `Power` routine); `IRP_MN_START_DEVICE` is passed down with a
completion wait and the entry published only on success;
`IRP_MN_STOP_DEVICE` and `IRP_MN_SURPRISE_REMOVAL` withdraw the entry;
`IRP_MN_REMOVE_DEVICE` withdraws it, waits for the rundown of section 3.2,
passes the IRP down, detaches and deletes the device object. Its
`AddDevice` attaches to any PDO the INF put it on; the location it
publishes is what the NHI matches.

### 6.3 Finding the tunnel bridge

The driver does not assume the P14s's bus numbers, the emulator's, or a
switch at all. From the NHI's own location (`IoGetDeviceProperty`
`DevicePropertyBusNumber`, `DevicePropertyAddress`) it walks up and across
with `HalGetBusData` reads of PCI headers, in a pure routine `tbt_topo.c`
over caller-supplied config accessors, host-tested against two captured
topologies: the P14s (`03:00.0` upstream, `04:00.0` to the NHI on bus 5,
`04:01.0` the tunnel, `04:02.0` the xHCI; `ICM-PROTOCOL` section 1 and the
Windows 98 `PCIDUMP.TXT` matrix) and the emulator's i440FX stand-in
(`WIN98-TEST`, "P14s BIOS matrix": root bridge at `00:06.0`, switch ports
`01:01.0`, `02:00.0`, `02:01.0`, `02:02.0`).

The rule: the NHI's parent is the header-type-1 function on some bus whose
secondary bus number equals the NHI's bus; the tunnel bridge is a sibling of
that parent (same bus) that is a header-type-1 function, not the NHI's
parent, and either carries a PCI Express capability whose slot capabilities
have Hot-Plug Capable set (real Alpine Ridge [obs]: "hot-plug and surprise
capable slot") or carries an SHPC capability (id `0x0C`; the emulator's
`usb4-tb3-tunnel-bridge`). Where neither test picks exactly one, the
registry value `XhciCmTunnelBridge` (a string `bb:dd.f`) names it, and with
no value and no unique pick the driver logs the candidates and runs with
re-enumeration disabled (section 6.4). The generic `usb4-generic` layout,
where the host and the tunnel bridge are siblings on bus 0 with no switch, is
also accepted by the same rule (parent = the root bus, siblings = bus 0
bridges with SHPC), which is what the `U4CM` work will inherit.

### 6.4 When re-enumeration cannot be done

The NHI driver still runs the ICM client and approves devices, so a device
tunnelled this way still works after a Device Manager Refresh on 98 or a
"Scan for hardware changes" on NT; the driver logs once, with the reason,
and counts it. The CFGMGR32 user-mode route (`cmreenum.exe` in the
experiment) is a fallback this record does not ship.

### 6.5 Ordering: when the sequences run

Two registers of the tunnel port's PCI Express capability are read, and
they are not the same register (review round 1, finding 4): **Link Status**
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
  traffic, so a tunnel down arriving during a tunnel up's slot poll is
  handled after it and sees the bus empty.

## 7. The contract with `nvme98`

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
Obligation 2 is the harder one, and review rounds 1 and 2 (findings 7 and
6) narrowed the first draft's answer twice. An empty secondary-bus read is
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
  `TB3-A.7` qualifies on i440FX; a q35 topology with a real Link Active bit
  is the alternative.
- **On hardware, the boundary is unresolved by this record.** Once the
  link is down nothing below it can be read, so no register of the
  endpoint can say when its reset completed during the disconnected
  interval; reading CC.EN and RDY after a reconnection corroborates A1
  after the fact and establishes nothing about the interval (review round
  3, finding 6). An observation method that is available while the tunnel
  is down, or a bound agreed with `nvme98` on other grounds, is a question
  for `nvme98`'s own hardware verification of MD-2 (the P14s with an NVMe
  enclosure and with one that has an internal switch) and the owner, not
  one this driver can answer alone. Until it is answered, **no hardware
  statement is sent**, on any OS.

Until the emulator qualification exists on an OS, and the hardware
boundary is resolved, no statement is sent there and `nvme98`'s gone
deadline (its decision MD-1) is the fallback, as its PLAN already provides.

### 7.2 The notification channel (proposal, decision D6)

The interface is "to be agreed" (handoff section 6b) and `nvme98`'s gate G3
names the candidate shapes: on 9x a VxD service or a registered callback, on
NT `IOCTL_SCSI_MINIPORT` with an `nvme98` signature. This record proposes:

- **NT**: the recipient is resolved **at tunnel up**, never at tunnel down:
  after a re-enumeration, once the bus has settled, the thread walks
  `\Device\ScsiPortN` (N from 0 up to the first miss after 31) with
  `IoGetDeviceObjectPointer` and an `IOCTL_SCSI_MINIPORT` carrying
  `nvme98`'s signature and a "report your PCI location" function, and
  caches the port whose answer matches a function behind the tunnel,
  keeping the reference. At tunnel down that cached port gets one
  `IOCTL_SCSI_MINIPORT`, "link down", sent synchronously from the
  connection-manager thread before the filter's invalidation, and from
  inside a command wait if the disconnect landed during one (section 4.4).
  The IOCTL must not queue behind held SRBs (G3's third clause); that is
  `nvme98`'s side. A disconnect that arrives after `pci.sys`'s own native
  hot-plug (Windows 7 on real PCIe) has already started removal is a case
  the per-OS qualification reads, not one this record resolves.

**The latency budget** (review rounds 2 and 3, findings 7). `nvme98`
measures its gone deadline from its own presence loss, 5 s in its test
build, and requires delivery inside it at every injection point and under
queue saturation. The sender's budget is therefore end to end, every part
of it is bounded while the statement is enabled, and every piece of thread
work that could sit in front of the statement is made interruptible by a
disconnect:

| Part | Bound |
|---|---|
| noticing the event | the idle RX poll: `XhciCmPollIdleMs`, which the driver clamps to 250 ms whenever the statement is enabled on that OS, whatever the registry says (the 5000 ms ceiling of section 8 is for a machine with the statement off) |
| a command in flight | none added: the event is decoded inside the wait (section 4.4) |
| a tunnel-up poll in flight (section 6.5) | none added: the poll services RX frames too, and a `device disconnected` for the device it is waiting on abandons the poll - the event says the device is gone |
| a recipient discovery in flight | none added: discovery is a sequence of IOCTLs between which the thread services RX frames, and a disconnect abandons the sequence |
| the port reading | 2 s at most (section 6.5), and the firmware's event already follows the link drop, so the normal cost is one read |
| the recipient | none when cached; **when the cache is not ready** (the disconnect came before discovery finished) **no statement is sent** and MD-1 applies - a late statement is worth less than none, since `nvme98` would act on it after its own deadline |
| delivery | one synchronous IOCTL or VxD call, whose execution bound is `nvme98`'s to state in the interface (the proposal asks for 100 ms, with the call never queued behind held SRBs) |

So about 2.25 s plus the delivery bound in the worst case, as an
**estimate**, not a measurement: `TB3-A.7`'s qualification measures it on
each OS, saturated, with the disconnect injected during idle, during a
command wait, during a tunnel-up poll and during discovery, as gate G3's
"every injection point" asks.
- **Windows 98 and ME**: `nvme98`'s own VxD (the base ships `NVMEVSD.VXD`)
  exports one service, "link down for bus:dev.fn", under a VxD id `nvme98`
  chooses; this driver reaches it through the same dynalink thunk it uses
  for CONFIGMG, guarded by that VxD's `Get_Version`, and sends it before
  `Query_Remove_SubTree`. The call is a no-op when the VxD is not loaded (the
  dynalink fails to resolve, which the thunk detects by the VMM leaving the
  bytes unpatched - to be read; a safer guard is a registry flag the VxD
  writes at load).

Both halves ship after the first checkpoint (`TB3-A.7`), once `nvme98` has
chosen; until then `nvme98` completes held requests after its gone deadline
(its decision MD-1), and this driver's removal calls are what the operating
system already does on a surprise removal. The release notes say which
state the pair is in.

### 7.3 What this driver never claims

It never reports a link down it has not seen the firmware report; an
all-ones read of the endpoint is a presence signal for the re-enumeration
walk and nothing else (`nvme98` PLAN, "Presence loss is not isolation").

### 7.4 Safe removal

Not wired in the first cut: a tunnelled device is not marked removable, this
driver is not told about an eject, and every tunnel down on NT shows the
Unsafe Removal dialog. The handoff names the two routes (mark the devnodes
removable and tear the tunnel down after an eject, or an eject of the
driver's own); under the firmware connection manager the second needs
mailbox `0x06`, which tears down every tunnel, so the first is the one to
design when the owner wants it. Recorded as a known limitation.

## 8. Registry values, counters, the log and the snapshot

Values, each `REG_DWORD` unless said, each written by every install path at
its default under the don't-overwrite flag, each read once at start:

| Value | Default | Meaning |
|---|---|---|
| `XhciCmMode` | 1 | the master switch (decision D11): 0 off - the driver binds the NHI, logs it, touches no ring, approves nothing, re-enumerates nothing; 1 auto - the ICM client where the matched id's profile is the ICM one and section 5.1's checks pass, the USB4 software connection manager once `U4CM` exists, and until then off with a log line; 2 the experimental route for an Intel NHI id the profile table does not list: the ICM client with the Alpine Ridge table, at the user's own risk (the `XhciIntelPortSwitch` 2 pattern, said in the same words in the README and release notes). A registry value cannot make PnP bind a driver to an id its INF does not name, so 2 also needs the id matched: the user adds a model line for it to a copy of `xhcicm.inf` and installs through Have Disk, a procedure the README spells out. What 2 bypasses is the id-to-profile lookup alone; the firmware status, authentication, mode and interface-version checks of section 5.1 still refuse a start they fail; 3 force the software CM (reserved; the same as 0 until `U4CM`) |
| `XhciCmApprove` | 1 | section 5.4; 0 and 1 only |
| `XhciCmReenum` | 1 | 0 disables every re-enumeration call (section 6), for a machine where CONFIGMG or the filter misbehaves; the ICM client still runs |
| `XhciCmTunnelBridge` | absent (`REG_SZ`) | `bb:dd.f`, overrides section 6.3's discovery |
| `XhciCmPollIdleMs` | 200 | the idle RX poll period, held to 50 to 5000, and clamped to 250 while the `nvme98` statement is enabled (section 7.2) |
| `XhciCmCmdTimeoutMs` | 5000 | the ICM command timeout (section 5.5), held to 1000 to 60000; retries stay 3 |
| `XhciCmApproveTimeoutMs` | 10000 | the `approve` timeout, the same bounds |
| `XhciLogVerbosity` | as `xhci98.sys` | the shared log rule |

Installing the package is already the user's choice of this driver, so the
master switch defaults to on: a driver that installs and does nothing until
a second step is the trap the "every value visible" rule exists to avoid.
What the switch is for is turning the whole thing off on a machine that
misbehaves without uninstalling, and forcing the kind on a machine where
auto-detection guesses wrong. Values the `U4CM` work will add beside these,
named now so the convention holds: `XhciCmUsb3Tunnel`, `XhciCmLaneBonding`,
`XhciCmPreOsCleanup`. Later, not in this cut: `XhciCmInterruptMode` (0 poll,
1 INTx once read on hardware) and a safe-eject switch.

Counters, in the driver's extension and emitted at stop through the log
sink, named `cm.*`: commands sent, responses, timeouts, retries, errors;
events by code; approvals sent, succeeded, failed; tunnel ups and downs;
re-enumerations run, skipped (and why), and their `CONFIGRET` or status;
mailbox commands and their outcomes; idle polls.

The log is the pure ring of `xhci_log.c`, compiled in by path: producers
record from the thread, and the ring is emitted at stop from the thread at
PASSIVE_LEVEL. **How it is emitted is decision D12**, because `AGENTS.md`
("Coding Style") admits an unguarded `DbgPrint` at exactly two sites,
`hcd_log.c`'s `XhciLogDebugView` sink and the qemu trace, and says not to
widen it to a third. The default this record is written to: in the
`release` flavour `xhcicm.sys` prints nothing, as `xhciuas.sys` prints
nothing; in `debug` and `qemu` the emission is under `#if DBG` through the
qemu flavour's port-`0xE9` mirror and `DbgPrint`, so a Windows 98 bare-metal
reading needs the `debug` flavour installed and DebugView, which is the
HCD's state before task 13-L. The alternative the owner may choose instead
is a separately gated, opt-in, PASSIVE-level sink of this driver's own
under a value of its own (`XhciCmDebugView`), with the rule in `AGENTS.md`
amended to name it; that is the richer field route and the one that
widens the rule. `XHCISNAP` does not read this driver in the first cut: the
door is `xhci98.sys`'s, and a `\DosDevices\` link and region of this
driver's own is a later revision if the counters are wanted off a real
machine.

## 9. Provenance and licence

This project is GPL-2.0-only and takes material from four places for this
work, under `AGENTS.md`'s rules and `legal-provenance.md`:

| Source | Licence | How it is used | Record |
|---|---|---|---|
| `usb4-emu/docs/ICM-PROTOCOL.md` and the handoff's other documents | MIT | the clean-room contract; every ICM word layout, register and sequence is cited to it by section, as the `usb4-emu` emulator itself is written from it | a `legal-provenance.md` section 4 row per cited fact class, method `static` (a document read), with the capture's run id as the provenance behind each [obs] |
| `usb4-emu/tools/usb4cm/core/u4hi.c`, `u4regs.h`, `u4types.h` | MIT | lifted into `src\cm\` as `tbt_ring.c`, `tbt_regs.h`; the file headers keep the original copyright line and the MIT permission notice with an SPDX tag, and `LICENSE`'s scope note names them | section 2a of `legal-provenance.md` (incorporated third-party code) |
| USB4 Specification 2.0 and Connection Manager Guide 2.0 (USB-IF, November 2025 bundle) | USB-IF terms, documents only | facts and section numbers for the generic NHI ring registers and CRC-32C; the PDFs are fetched per machine into `docs/references/usb4_202511/` | rows in `docs/references/README.md` with the bundle URL and each PDF's SHA-256, added 2026-10-14 (owner's permission to take the files from `usb4-emu`'s own references folder); the README's "USB4 2.0, November 2025 bundle" section also records that Thunderbolt 3 itself has no public specification to fetch |
| The Windows 98 DDK's `CONFIGMG.H` | Microsoft DDK terms | the CONFIGMG service numbers alone (section 6.1) | a `docs/references/README.md` row and a `legal-provenance.md` row; never tracked |
| FreeBSD `sys/dev/thunderbolt` (`nhi_reg.h`) | BSD-2-Clause | a cross-check of register offsets, if used; nothing adapted in the first cut | a row if anything is adapted, with its notice kept |

**Not a source, and not to be opened for this work: Linux's
`drivers/thunderbolt`.** `ICM-PROTOCOL.md` was written from it as a
behaviour reference under `usb4-emu`'s clean-room process and is what this
driver consumes; reading `icm.c`, `nhi.c`, `tb_msgs.h` or `nhi_regs.h`
while writing `src\cm\` would make the clean-room boundary this record rests
on untrue. `external/` holds a Linux mirror for the xHCI work; its
`drivers/thunderbolt` tree is outside this record's reading list, and design
record 20's Linux exception does not extend to it. Every statement in this
record that cites [inf] is a behaviour of the Linux client described in
`ICM-PROTOCOL.md`'s words, not read from Linux here.

As always, no third-party document or binary enters the tree, facts are
written re-derivably (word, bit range, capture line), and binary-derived
facts are tagged `static` or `runtime`.

## 10. Verification

### 10.1 Host tests

`test\test_tbt_ring.c`, `test_tbt_icm.c`, `test_tbt_topo.c` under the
existing harness, with `test\run-host-tests.cmd` growing three suites:

- rings: the Spec Appendix A.2 CRC-32C vectors (as `usb4cm`'s host test
  runs them), descriptor wrap, consumption by index and by the done bit, the
  RX give-back, a frame larger than its buffer, sequence numbers and a
  response that echoes the wrong one;
- ICM: every observed frame of section 5.2 decoded to the fields
  `ICM-PROTOCOL` lists, the name block's four entry types and a zero-length
  terminator, the state machine through bind, announce, approve, disconnect,
  resume-refresh and the 500 ms reconciliation, timeouts and retries with a
  fake clock, the error flag, an unknown code;
- topology: the two captured layouts of section 6.3, the generic layout,
  a layout with two hot-plug siblings (no unique pick), and the override.

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
- `-trace 'usb4_*' -D <VM_DIR>\usb4-trace.log` on every leg; the trace
  decodes every ring 0 frame both ways and is the oracle for "what the
  client sent".

The Windows 98 SE legs (`TB3-V.1`), each from a fresh overlay:

| Leg | `BIOS` | `DEVICE_AT_BOOT` | What is read |
|---|---|---|---|
| V.1.1 install | `OFF_OFF` | 1 | the NHI's Found New Hardware, the INF taken, Code 0; the log's bind sequence; the dock announced unapproved, approved, the tunnel up (`usb4_tunnel_transition`), the bridge re-enumerated, the NVMe found by nvme9x (already on the base image) and its volume mounted, with no Device Manager Refresh |
| V.1.2 unplug and re-plug | `OFF_OFF` | 1 | `device_del`: the disconnect event, the volume gone, the devnode removed; `device_add`: announced, approved, the volume back |
| V.1.3 boot-tunnelled device | `OFF_ON` and `ON_ON` | 1 | the NVMe is present at POST, and an NVMe with a namespace at POST hangs Windows 98's GUI boot at the logo (SeaBIOS hooks it into INT 13h; `WIN98-TEST`, "`x-force-tunnel=on` at boot"; the BIOS matrix there used a blank disk and DOS only). The overlay for this leg therefore carries one of the handoff's three workarounds, chosen by `TB3-0.7`: `BootGUI=0` and `win` from `AUTOEXEC.BAT` (the route `usb4cm`'s validation used after a DOS-side tunnel), `-boot strict=on` with a boot index on the IDE disk, or a SeaBIOS without NVMe. Then: the volume present with no driver action; the driver binds, the device re-announced approved and left alone; then V.1.2's cycle |
| V.1.4 nothing at boot, controller present | `OFF_OFF`, `OFF_ON` | 0 | the driver binds with an empty table; `device_add` goes through V.1.1's path (with Assist on and nothing attached the controller is absent, section 2, so `ON_*` cannot test this) |
| V.1.5 Assist, no device | `ON_OFF`, `ON_ON` | 0 | the controller absent; nothing to bind; the release notes' limitation read as true |
| V.1.6 disable and enable | `OFF_OFF` | 1 | Device Manager disable: the rings stopped then `driver unloads`, the volume stays; enable: the rebind, the device re-announced approved |
| V.1.7 `XhciCmApprove` 0 and `XhciCmMode` 0 | `OFF_OFF` | 1 | approve 0: announced, not approved, no tunnel, logged; mode 0: bound, nothing sent, nothing announced |
| V.1.8 the dock's xHCI | `OFF_OFF` | 1 | with `usb4-device dock=on` if the tb3 dialect supports it on the i440FX profile (a `TB3-0.7` reading), or on the bench: the xHCI behind the tunnel bound by `xhci98.sys` and a USB device on it |

The Windows 2000 legs (`TB3-V.2`) run the same table on the Alpine Ridge
tree the experiment used (`TOPO=ar`, `tools/reenum-test/run2k.sh`'s
device lines, taken into this project's own launcher with its own overlay
of `usb4-emu/vms/win2k/win2k-xonly.qcow2` and its own ports; the Windows
2000 image has a Dvorak layout, which the QMP key helper has a switch for),
plus the filter's install through the INF, its boot-start question, S3 if
the guest offers it, and on the SMP guest under Driver Verifier a bridge
removal (`device_del` of the tunnel bridge's parent is not possible, so the
filter's disable through Device Manager) concurrent with tunnel work, for
the rundown of section 3.2. `TB3-V.3` reads the install and V.1.1's cycle
on ME, XP, XP x64 (no q35: the i440FX tree), Vista x86 and x64, 7 x86 and
x64. Those tiers carry no checkpoint tax: the legs are read and recorded,
and a failure on one is a release-notes line, not a checkpoint failure;
what the cut does require on every target is the install leg from the
asset, which is `TB3.final`'s, as it is for every release. On 7 x64 the
native hot-plug path may do the filter's work first, which is a reading,
not a fault.

### 10.3 The import gates and the INF gates

`check-imports.ps1 -AllowPath xhcicm-imports.allow` against the Windows 98
evidence list and the 2000 baselines for x86, and the amd64 allowlist for
the second binary; every import this driver adds is one the HCD already has
evidence for (`PsCreateSystemThread`, `KeStallExecutionProcessor`,
`HalGetBusData`, `IoGetDeviceProperty`, `IoGetDeviceObjectPointer`,
`IoInvalidateDeviceRelations`, `MmMapIoSpace`, `IoGetDmaAdapter`,
`ExAllocatePoolWithTag`) except the dynalink, which is an instruction, not
an import. `check-cm-inf.ps1` carries the four INFs' rules, the
`CM-NO9XBRIDGE` rule of section 6.1, and the self-test the other gates have.

### 10.4 The bench (`TB3-E`)

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
4. **The `$PIR` dump**: whether the P14s BIOS lists `00:1c.4`. If it does
   not, a function hot-added behind the tunnel gets Code 29 on Windows 98:
   `WIN98-TEST` ("Hot-added NVMe gets no IRQ behind 00:1c") read that as
   no IRQ **and no BAR assigned and a driver that never starts**, so
   polling is no remedy for it - design record 15's polled delivery acts
   inside a controller `xhci98.sys` was started on, and Code 29 is a
   devnode `PCI.VXD` never started. That outcome is a bench blocker to be
   recorded as such, and the first cut's release notes would then say
   that on such a machine a hot-added Thunderbolt device needs pre-boot
   tunnelling. Only a controller that does start without a usable line
   (Code 0, dead interrupt) is where record 15's fallback applies, and
   that is a separate reading. `XHCIQUAL` does not dump `$PIR` today;
   `TB3-0.6` adds the dump to its routing report (`xhciqual\route.c`) so
   the bench has it.
5. The log emission at a driver disable, on the flavour decision D12
   chooses (the `debug` flavour and DebugView by default).

Windows 2000 has never run on the P14s (Setup bugchecks, `AGENTS.md`), so
its half of "observed on both" is the VM leg, as for every HCD phase.

Whether the bench is read before or after the cut is not a standing
permission: `AGENTS.md` names the phases that were allowed to cut first
(28 to 31, 35), one decision each. This record asks for the same for `TB3`
as decision D13, with the pre-cut gates being everything else in this
section and the post-cut closure being `TB3-E.1` and `TB3-E.2`, a failure
there a re-cut; without that decision the bench comes before the cut.

## 11. Decisions for the owner

Each with the recommendation this record is written to; a different answer
changes the sections named.

| Id | Decision | Recommendation | Sections |
|---|---|---|---|
| D1 | One binary or two | a second binary, `xhcicm.sys` in `src\cm\`, with its own INFs, allowlists and gate | 3.1 |
| D2 | How the NT filter is installed | a separate NT-only INF (`$Windows NT$`, undecorated Models for Windows 2000) adding a device-specific `UpperFilters` on the switch port id, proved by the `TB3-0.4` reading; a class filter only if that route cannot be made to install, and then boot-start | 3.2, 3.3, 6.2 |
| D3 | Interrupts | none: polling only, 200 ms idle; an INTx mode later behind its own value once read on hardware | 4.3 |
| D4 | Approval policy | **Taken, owner 2026-10-14**: `XhciCmApprove` 0 and 1 only, 1 the default, approve everything, stated plainly as a security property in the release notes; nothing remembered, and the record says why that is harmless; "approve known devices only, from a UUID list under the service key, filled by hand" listed as a later option, not a value | 5.4 |
| D5 | What to do about a controller absent at boot (Assist on, nothing attached) | nothing; a known limitation; force power is an ACPI WMI method out of reach on 98 and unread on NT | 1.3, 2 |
| D6 | The `nvme98` notification channel | the proposal of 7.2, settled with `nvme98` after the first checkpoint, shipped as `TB3-A.7`; the statement withheld on any OS until gate G3's ordering is qualified there | 7.1, 7.2 |
| D7 | The version and the cut | the branch is already `3.0.0.0`; cut it after this work's checkpoint, with the USB4 software CM as the next minor, rather than holding the release for both | `roadmap-usb4.md` |
| D8 | Names | `xhcicm.sys`, `xhcicm.inf` and `xhcicmf.inf`, `xHCI98 Thunderbolt 3 Host Interface`, `xHCI98 Thunderbolt 3 Tunnel Port Filter`, class System | 3.3 |
| D9 | Other Alpine Ridge NHI ids | `15BF` alone until another is read, or a public-id-list row with its source if the owner wants the family; `XhciCmMode` 2 with a user-edited INF copy is the experimental route for an unlisted id meanwhile | 1.4, 3.3, 8 |
| D10 | Safe eject | not in the first cut; a known limitation | 7.4 |
| D11 | A master switch for the whole driver | `XhciCmMode`, default 1 (auto), 0 off, 2 force the ICM client at the user's own risk, 3 reserved for the software CM; the install is the real opt-in, so the default is on | 8 |
| D12 | The log emission | nothing in `release`, `#if DBG` in `debug` and `qemu` (the default, keeping `AGENTS.md`'s two-site `DbgPrint` rule as it is); or an opt-in PASSIVE-level sink of this driver's own with the rule amended to name it | 8 |
| D13 | Cut before the bench for `TB3` | asked for, as Phases 28 to 31 and 35 had it: the VM legs and gates before the cut, `TB3-E.1` and `TB3-E.2` after it on the asset, a failure there a re-cut | 10.4 |

## 12. What is open, and what is a risk

- **The P14s `$PIR`** (section 10.4): the one fact that decides whether a
  device hot-added behind the tunnel is started at all on Windows 98 on the
  real machine; Code 29 there is a devnode never started, which no driver
  below it can mitigate. The pre-boot path, where the BIOS assigns the
  dock's resources, is the fallback the release notes would name.
- **The G3 statement** (section 7.1): withheld on every OS until the
  emulator's ordering check exists there (on i440FX through the documented
  stage-3 surprise-removal presence change, or on q35 through a real Link
  Active bit), and withheld on hardware until the completed-reset
  boundary - unobservable through a down link, so unresolved by this
  record - is settled by `nvme98`'s MD-2 verification and the owner.
- **The NT filter's INF route** (D2): untested; the experiment typed a class
  filter by hand.
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

The pieces that survive into `U4CM` unchanged: the second binary and its
gates; the ring code (`tbt_ring.c`, which `u4hi.c` already was); the polling
thread and its queue; the topology walk and both re-enumeration routes;
the `nvme98` channel; the registry and counter conventions. What `U4CM`
adds is the connection manager proper (`u4cm.c`'s work, done in the driver
against the `usb4-generic` profile with both host models and `preos-cm=on`),
a profile switch at `AddDevice` keyed on the NHI's class code (`0x0C0340`
for USB4, `0x0880` for Alpine Ridge) and ids, the pre-OS tunnel cleanup the
Windows 11 CM performs, and - since under a software CM this driver does
tear paths down itself - the real work of obligation 2 in section 7. The
handoff's warning stands for it: write it to depend on no exact device
router, treat every router operation as possibly unsupported, discover
adapter numbers rather than assume them, and poll completions with
timeouts.

## Sources

- `usb4-emu/docs/XHCI98-HANDOFF.md`, `ICM-PROTOCOL.md`, `PLATFORM-MATRIX.md`,
  `REENUM-EXPERIMENT.md`, `WIN98-TEST.md`, `ARCHITECTURE.md`,
  `NVME-DRIVER-EVAL.md`, `ROADMAP.md`, `AGENTS.md`, `tools/usb4cm/README.md`,
  `tools/reenum-test/README.md` and `reenum.c`, `scripts/run-win98.sh`
  (branch `main`, `eba0cf1`, read 2026-10-14).
- `nvme98/docs/PLAN.md` section 0.10, gate G3 and decisions MD-1 and MD-2
  (branch `main`, read 2026-10-14).
- This repository: `AGENTS.md`; design records 13 (sections 5.1, 5.2, 7.5),
  15, 18 and 19; `docs/usb-xhci-info/win98-wdm.md`; `src\hcd_entry.c`,
  `src\hcd_svc.c`, `src\hcd_ctl.c`, `src\uas\sources`, `src\xhci98.inf`.
