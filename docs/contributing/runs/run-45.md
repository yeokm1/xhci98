# Run Sheet - Phase 45 (`3.4.0.0`)

The readings of `roadmap-usb4.md` Phase 45, one section per task, in
the style of `run-44.md`.

## 45.9 - The Prefetchable Windows on the 32-bit 8, 8.1 and 10: a Filter in the PCI Root Bus's Stack, Read (2026-10-29)

The task's reading (roadmap-usb4 Phase 45, PFW8.1's first half): does a
step in the PCI root bus's stack act before the PCI driver reads the
bridges, at every start read? The design that follows from it is design
record 32 section 11.5, **a draft not yet reviewed** (paused 2026-10-29).

### The vehicle

Development host A; q35, the launcher and emulator of "44.72"
(`bin\asm4242-20261026-4goff`, `qemu-system-x86_64.exe` `08D49470...`),
the 4G-off layout with the ORICO at power-on, `qemu64` processors, two
per guest, 2 GB; fresh overlays of the clean q35 bases of 8 x86
(`b8e091bd`), 8.1 x86 (`9714c3d8`) and 10 x86 (`ac361c39`). A probe of
this project's own, not tracked: `pfwflt.sys` (SHA-256 `37ffe267...0a8a`),
built with the Windows 2000 DDK from a scratch copy of `src\pfw\pfw_scan.c`
and a PnP filter of about 200 lines: it marks its `DriverEntry`, each
`AddDevice` and its first `IRP_MN_START_DEVICE` in the configuration
trace (three reads each of 00:00.0 at `F8h`, `F4h` and `F0h`) and on the
debug console, and runs record 32's step inside `KeIpiGenericCall` at
that first start before passing it down; every other request passes
down. Installed on the board without the USB4 model by a batch run
elevated: the image to `system32\drivers\pfwflt.sys`, the service
(Type 1, `Start` 0, `ErrorControl` 0, `Group` `PnP Filter`; the batch's
`ImagePath` line did not take, so the default path served), `powercfg /h
off`, and `pfwflt` written as both `UpperFilters` and `LowerFilters` of
`Enum\ACPI\PNP0A08\0` by a task run as SYSTEM. Then cold starts on the
4G-off layout, "RW" a read and write trace, "W" writes only; the
problem codes by `wmic` to COM1 at the desktop.

### Readings

| System | Start | Trace | `DriverEntry` | `AddDevice` (lower, upper) | start | step's close of the root port | PCI driver's probe of the root port | Result |
|---|---|---|---|---|---|---|---|---|
| 8 | a, b, c, d | RW | 14 457 | 14 460, 14 463 | 14 466 | 31 046 | 31 599 | the Start screen; every bridge 0; NVMe, NHI, `fw_cfg` 28 |
| 8 | e, f | W | - | - | - | 446 | 511 | the same |
| 8.1 | a, b, c | RW | 14 457 | 14 460, 14 463 | 14 466 | 31 046 | 31 600 | the desktop; every bridge 0; NHI, `fw_cfg` 28 |
| 8.1 | d, e | W | - | - | - | 446 | 511 | the same |
| 10 (`g10`) | a | RW | none | none | none | none | 44 715 | "Automatic Repair: Your PC did not start correctly" |
| 10 (`g10`) | b | RW | none | none | none | none | - | stop `INACCESSIBLE_BOOT_DEVICE` (screenshot) |
| 10 (`g10c`, `Group` `Core`) | a | RW | none | - | - | - | - | "Diagnosing your PC" |
| 10 (`g10c`) | the board without the USB4 model | - | none | - | - | - | - | "Preparing Automatic Repair" (after the failed starts) |

On 8 and 8.1 the debug console showed the same sequence at every start:
`DriverEntry`; `AddDevice` 1 (attached over the ACPI driver's object:
the lower filter); three `IRP_MN_QUERY_INTERFACE` through it (the PCI
driver's own `AddDevice`); `AddDevice` 2 (over the PCI driver's object:
the upper filter); `IRP_MN_QUERY_LEGACY_BUS_INFORMATION`,
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS` and `IRP_MN_START_DEVICE`, each to
the upper filter first; the step's line `result/functions/closed/refused`
in hex, `1 12 9 0` (18 functions, nine bridges closed). On 10 the image had entered
`DriverEntry` at the start before the filters were written (`g10`'s
install start, a plain boot driver of group `PnP Filter`, "pfwflt
DriverEntry" and no `AddDevice`); with the root bus naming it, it was
never entered and the start failed.

### What it says

- **8 and 8.1**: a filter in the root bus's stack runs before the PCI
  driver reads the bridges at every start read (six on 8, five on 8.1),
  at the same trace lines every time, where "44.72"'s boot driver of
  `System Reserved` came after the PCI driver at three of four starts on
  8 and one of six on 8.1 [read]. The step there closes the windows and
  8, which does not start without it ("44.49"), starts [read].
- **10**: a boot-start filter named by the root bus devnode is not
  initialised and the root bus does not start: Windows stops with
  `INACCESSIBLE_BOOT_DEVICE` or goes to its recovery, in group `PnP
  Filter` and in group `Core` alike [read]; why is not read.
- **The hazard is real**: a root bus whose stack names a filter that is
  not loaded stops Windows at start (10's readings) [read]; the design
  makes the removal take the filter out of `UpperFilters` first.

So the step can act in time on 8 and 8.1 in the root bus's stack; on 10
it cannot be placed there as read. Record 32 section 11.5 (draft) designs
the filter role for 8 and 8.1 and keeps 10 not offered. **The code is not
this worker's** (roadmap: "the code only if the reading shows such a
step acts in time"): it is reported to the coordinator, with the owner's
questions Q10 to Q12 of that section. Left: the design's review loop to
convergence, the owner's answers, the code and its legs; for 10, why the
filter is not initialised. The task stays unticked.

Evidence: `E:\xhci98-vm\p45\w45-read\` (`g8`, `g81`, `g10`, `g10c`:
`trace-*.log`, `debugcon-*.log`, `serial-*.log`, `leg-*.txt`,
screenshots; `xflt`, `xfltc` the batches and the probe image;
`pids.txt`, `STATE.md`); the probe's source in
`E:\xhci98-scratch\p45-w45-read\flt`.

## 45.10 - Tunnelled NVMe Speed on 98 SE and ME, the Emulator Readings (2026-10-29, paused)

Not read yet: paused by the owner's usage limit on 2026-10-29 with the
leg in hand. Prepared: the x86 `release` gated build at `1765b18c`
(`out\build-x86-release.log`, BUILD + GATES PASSED), a TEST cut of it
(`make-release.ps1 -Flavor release -Arch x86 -TestLabel`, zip SHA-256
`463564e8...24e0`), XHCIPERF of the same build (SHA-256 `cf6b4a3d...25b3`)
and a batch running four XHCIPERF command lines on the tunnelled drive
(the `seqmax` preset at 64 MiB: 512 KiB, 1, 2 and 8 MiB at one thread and
1 MiB at two; 1 MiB at two and four threads; the `sweep` preset 64 KiB to
8 MiB, aligned and then scattered), each report with its Stor98 pages to
COM1. A fresh q35 98 SE overlay with the ASM4242 model and the ORICO at
power-on reached the desktop after its bridge wizards; the installer
(`/thunderbolt /stor98 /s98vsd`) was running when the guest was stopped,
so that overlay is not used again: the readings start from a fresh one.

Read on the way, for task 45.22's 98 SE route (now another worker's):
on that guest, before this project's package, QEMU's `info pci` showed
the ASM4242's NHI (`1B21:2425`, bus 8) with Interrupt Line 11, pin A,
beside the tunnelled NVMe (11, pin A) and the q35 xHCI and AHCI (10)
[emu]; whether 98 SE's configuration manager allocates that IRQ to the
NHI's devnode is not read.

## 45.21 - Thunderbolt 1, 2 and 3 on Apple Macs: the Reading and the Design (2026-10-29)

The task's first half (roadmap-usb4 Phase 45, the owner's "Phase 45
additions"): what a Mac's Thunderbolt host needs from this driver as its
connection manager, read from Linux's facts and the public record, then a
design section converged before any code. No code, no leg.

### What was read

- Linux `drivers/thunderbolt` at the pinned `c6859eed` (GPL-2.0-only,
  facts only): `pci.c` `nhi_ids[]` (Light Ridge `1513` and Cactus Ridge 4C
  `1547` matched only with subsystem `2222:1111`, Falcon Ridge by id),
  `icm.c` (`icm_probe` has no Light Ridge or Cactus Ridge row;
  `icm_fr_is_supported` is false on an Apple machine;
  `icm_ar_is_supported` takes the ICM on Alpine and Titan Ridge only when
  its firmware already runs or `start_icm` asks), `tb.c`
  (`tb_apple_add_links`, `tb_find_pcie_down`'s per-controller PCIe down
  adapters, `tb_resume_noirq`'s reset of a non-USB4 host), `switch.c`
  (`tb_switch_get_generation`: Light Ridge, Eagle Ridge, Light Peak,
  Cactus Ridge, Port Ridge and Redwood Ridge generation 1, Win Ridge and
  Falcon Ridge 2, Alpine and Titan Ridge 3; `tb_switch_configure`'s
  non-USB4 branch; `tb_plug_events_active`; `tb_switch_reset_host`, the
  "reset" packet on generation 1), `cap.c` (Light Ridge's TMU access bit
  and dummy read), `lc.c` (nothing below generation 2, the DP sink from
  3), `tunnel.c` `tb_dp_xchg_caps` (skipped below generation 2).
- Apple's support pages (Boot Camp Support Software 5.0.5033 and 5.1:
  64-bit Windows 7, 8 and 8.1 on the Macs of 2011 to 2014; Windows 10
  64-bit only, Macs of 2012 on) and the press reports of 2011 quoting
  Apple: the Early 2011 MacBook Pro, the first Thunderbolt Mac, supported
  Windows 7 32-bit and 64-bit only, XP and Vista "not supported", Boot
  Camp 4 Windows 7 only. No public page read names 98 SE, ME, 2000, XP or
  Vista for any Thunderbolt Mac; whether any starts there through the
  firmware's BIOS emulation is unread.
- This project: design records 21 and 22 (sections 3, 6, 8, 9, 11),
  `usb4-emu`'s `docs/CONTROLLER-CATALOG.md` (no Thunderbolt 1 or 2 model,
  no Apple host profile).

### What it says

- The five Mac hosts (Light Ridge, Cactus Ridge 4C, Falcon Ridge, Alpine
  Ridge, Titan Ridge) are, for Linux on a Mac, software-connection-manager
  hosts; the rest of Linux's Thunderbolt 1 and 2 parts are device routers
  there.
- What the USB4 software connection manager lacks for them is small in
  kind: section 8's Thunderbolt 3 device-router handling applied to the
  host router at route 0 and extended to generations 1 and 2 (no USB4
  router registers, the Plug Events capability, Light Ridge's two quirks,
  no link controller below generation 2, a fixed PCIe down adapter per
  controller); paths and the PCIe tunnel are the same code. DisplayPort is
  the large part and is left out.
- The systems within reach by Apple's own support are 7 (x86 and x64) and
  the 64-bit 8, 8.1 and 10; 98 SE to Vista are owed to the bench.

### The design

Design record 22 section 19 (commits `1765b18c` and `f46fbf5c`,
reviewed and revised), with the owner's questions Q1 to Q4 there. Section
3.1's inert start for subsystem vendor `106B` and the release notes' "Not
claimed: Apple machines" stay until the code and 45-E.11.

Left: the code (after the owner's answers and an emulator model in
`usb4-emu`, section 19.6), its host vectors and legs, and 45-E.11. The
task stays unticked.
