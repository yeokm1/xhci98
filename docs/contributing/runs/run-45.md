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

## 45.10 - Tunnelled NVMe Speed on 98 SE and ME, the Emulator Readings (2026-10-29)

Design record 30 section 13.7's readings on a q35 98 SE guest: two
threads at Stor98, the per-page mapping cost aligned against scattered,
and the request sizes reaching Stor98 at 4 and 8 MiB. Every figure here
is TCG's and compares within this table only (record 30 section 13.4).

### The vehicle

Development host A; q35, `launch-q35.ps1`'s machine (`-Usb4 asm4242
-EncAtBoot`: the ASM4242 model with the ORICO and its 128 MiB FAT test
disk tunnelled at power-on), `pentium3`, one processor, 512 MB; a fresh
overlay of the clean q35 98 SE base (`ab1d4d65`), its bridge wizards
answered. The package: the x86 `release` gated build at `1765b18c`
(`BUILD + GATES PASSED`, this tree's 3.3.0.0 version stamp), cut as a
TEST upload set (`make-release.ps1 -Flavor release -Arch x86
-TestLabel`, zip SHA-256 `463564e8...24e0`), installed with `XHCIINST.EXE
/dir release-x86 /yes /nopause /thunderbolt /stor98 /s98vsd` (exit 3),
then a shut-down and cold start; the drive is E: on Stor98 3.3.0.0 (the
Windows 98 and ME build) with `S98VSD.VXD`. XHCIPERF of the same build
(SHA-256 `cf6b4a3d...25b3`), run from a batch by command line, 48 MiB
test file (the disk's free space allows no more), unbuffered, the random
fixed pattern, write-through off; each report with its Stor98 pages sent
to COM1.

Found on the way (outside this task, reported to the coordinator): the
install's helper asked twice for "the disk labeled 'xhci98 xHCI Host
Controller Disk'" (the file `xhci98.sys`, "Copy files from" offering
`C:\WIN98`) while installing the second xHCI controller; pointing it at
`C:\XHCI98` let the install finish. At the first attempt, on another
fresh overlay, the guest's QEMU process ended by itself while that
prompt stood; that overlay was discarded.

### Readings

R1, the `seqmax` preset (three passes of 5 s, best shown), aligned
buffer:

| Test | Write MB/s | Read MB/s | At Stor98 (read) |
|---|---|---|---|
| 512 KiB, 1 thread | 229.7 | 216.7 | one command a request, at most 1 at the drive |
| 1 MiB, 1 thread | 234.6 | 249.8 | two 512 KiB commands a request (the emulator's MDTS), at most 2 |
| 2 MiB, 1 thread | 241.0 | 234.4 | four 512 KiB commands, at most 3 to 4 |
| 8 MiB, 1 thread | 104.6 | 107.6 | **63 KiB pieces**: 8450 requests "of 63 KiB" for 65 reads, every one a scatter/gather list and double-buffered by IOS; "Largest request granted: 2048 KiB" |
| 1 MiB, 2 threads | 248.5 | 218.6 | at most 2 at the drive |

R2, 1 MiB requests at two and four threads (three passes of 5 s):

| Threads | Write MB/s | Read MB/s | Most commands at the drive at once |
|---|---|---|---|
| 2 | 210.6 | 209.6 | 2 (write and read) |
| 4 | 183.9 | 205.9 | 2 (write and read) |

R3 and R4, the `sweep` preset 64 KiB to 8 MiB at one thread, read
column (write in brackets), aligned against scattered:

| Size | Aligned MB/s | Scattered MB/s | Scattered at Stor98 |
|---|---|---|---|
| 64 KiB | 60.0 (67.9) | 34.0 (62.4) | |
| 128 KiB | 109.3 (93.0) | 64.5 (67.7) | |
| 256 KiB | 167.2 (120.7) | 53.8 (67.3) | |
| 512 KiB | 171.9 (172.2) | 60.7 (73.9) | |
| 1 MiB | 171.8 (170.3) | 67.3 (71.6) | 768 scatter/gather lists for 48 reads, each below 63 KiB; at most 1 at the drive |
| 2 MiB | 215.9 (202.0) | 62.9 (84.5) | 768 lists below 63 KiB for 24 reads; at most 1 |
| 4 MiB | 85.3 (83.2) | 67.7 (78.9) | |
| 8 MiB | 80.0 (82.4) | 67.7 (76.9) | |

At 4 and 8 MiB in R3, aligned, Stor98 received 779 requests "of 63 KiB",
1 "below 63 KiB" and 11 (4 MiB) or 5 (8 MiB) "below 4 KiB" for 12 reads
of 4 MiB and for 6 of 8 MiB, every one a scatter/gather list
double-buffered by IOS. With the scattered buffer (R4) the requests at 1
MiB and above arrived in the "below 63 KiB" class, not double-buffered.

### What it says, for the changes C1 to C5 (record 30 section 13.6)

- **Two threads at Stor98** (section 13.3 item 2): two and four threads
  never put more than two commands at the drive, which is what one
  1 MiB request's two 512 KiB children already do at one thread [read];
  that two threads' requests did not overlap at Stor98 is an inference
  from that depth, the pages carrying no request identities [inf]. The
  rates moved little and both ways (R1's write 234.6 at one thread, 248.5
  at two; R2's 210.6 and 183.9 at two and four) [read]: threads showed no
  clear gain on 98 SE in the emulator.
- **The request sizes at 4 and 8 MiB** (item 4): with the aligned
  buffer, requests above 2 MiB never reached Stor98 whole: they arrived
  as 63 KiB scatter/gather pieces, each double-buffered, at about 80 to
  108 MB/s against 202 to 241 MB/s at 2 MiB (R1 and R3) [read]. Stor98's
  granted maximum is 2048 KiB, so the cut is IOS's above that maximum
  (record 30 section 1.3) [inf]. Whether a larger 9x maximum (C4, with
  this layer's room raised) would let them through whole is not shown,
  every run having the 2048 KiB maximum [inf]. C4 stays planned, decided
  by the bench's B4 rows.
- **The per-page mapping cost, aligned against scattered** (item 3,
  C2): the scattered buffer did not reach Stor98 as a large request at
  all: at 1 and 2 MiB it arrived as scatter/gather lists below 63 KiB
  each (16 a 1 MiB request), one command at a time, at about a third of
  the aligned rate [read]. So the emulator's aligned-against-scattered
  difference mixes IOS's cut of a scattered buffer (other request
  sizes, command counts, depths and handover paths) with whatever
  Stor98's per-page `ScsiPortGetPhysicalAddress` calls cost, and cannot
  isolate or exclude the latter [inf]; C2's own gain needs another
  vehicle (a Stor98 counter of the mapping's time, or the bench's refit
  of B), and stays planned.
- **C1** (a child size setting) and **C3** (more `ENG_PRP_PAGES`): the
  emulator's MDTS of 512 KiB already splits every request above it, and
  depth reached 3 to 4 at 2 MiB with one thread [read]; both stay
  planned, decided by the bench's MDTS and depth histogram (B4).
- **C5** (interrupt coalescing): nothing here reads it; stays planned.

The bench (45-E.9) is unchanged. Evidence:
`E:\xhci98-vm\p45\w45-read\s98c\` (`perf-c.txt` the four reports with
every Stor98 page, `serial-*.log`, `pids.txt`), `xfer98\` (`P.BAT`, the
package).

Read on the way, for task 45.22's 98 SE route (now another worker's):
on the first overlay, before this project's package, QEMU's `info pci` showed
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
