# Phase 39 Record

The detail behind `docs/contributing/roadmap-usb4.md`, "Phase 39" (the
Thunderbolt 3 ICM client and tunnel re-enumeration). The roadmap entry
carries the goal, the status, the task list and the checkpoint; this file
carries what each task did and what each reading said. Where the two
disagree about a clause, the roadmap wins. The design is
`design/21-thunderbolt3-icm-client.md` (record 21).

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was kept on the host that took it; they are not files a clone has.

## 39-0.7 - The vehicle for every Phase 39 and 40 virtual-machine leg

**Verdict: done** (2026-10-15). A launcher and a QMP helper under
`scripts\usb4\`, the readings task 39-0.7 asks for, and the first readings of
the Alpine Ridge and generic models on this project's own guests.

**The host.** Development host A, 32 logical processors. Every leg ran on
TCG under the admission rule of the Phase 39 brief (out of `out\vmlocks`,
the sum of `-smp` over every running `qemu-system` process within the
logical processor count, 6 GB of RAM left free); three `usb4-emu` guests that
this project did not start were running throughout and were left alone.
Evidence, screendumps, transfer-disk read-backs and debug-console logs are in
`out\p39-vm\legs\<run>\`; the throwaway INFs, the reading tool and its source
in `out\p39-vm\x2k\`, `out\p39-vm\x98\` and `out\p39-vm\devq\`.

### The emulator build used

`usb4-emu` was read, never written. Its `main` is `a68bc74` (record 21 cites
`eba0cf1`; `a68bc74` is the merge of `eba0cf1`, "Export QEMU fork: nvme98 test
harness, TB3 switch INTA#", so nothing record 21 relies on moved). Its working
tree is on `m0-bootstrap` at `eba0cf1`, and its built binary,
`src\qemu\build\qemu-system-x86_64.exe`, reports `QEMU emulator version
11.1.2 (v11.1.2-118-gaf0c743)` and was built 2026-10-14 22:47, after that
commit: **the readings below are of that working-tree build**, copied by the
launcher to `vm\usb4\bin\` (SHA-256
`1BAFC71D0A96BBA47F84E89B994CB87CEE741C02829C58E7EFACD63D99A34A49`), with
its DLLs from `usb4-emu\toolchain\msys64\ucrt64\bin` and its firmware from
`usb4-emu\vms\qemu-baseline\share`, both read-only. Committed documents were
read with `git show main:<path>` (`docs/XHCI98-HANDOFF.md`,
`docs/WIN98-TEST.md`, `docs/HARNESS.md`); the local, git-ignored
`vms\README-local.md`, `vms\*\RESULTS-*.md` and launch scripts from the
working tree.

### The launch lines of `usb4-emu`'s own setups

Read from `vms\win2k\RESULTS-win2k.md` and `run-w2k.sh`,
`tools\reenum-test\run2k.sh`, `vms\winxp64\RESULTS-winxp64.md` and
`run-xp64.sh`, and `vms\win7x64\RESULTS-win7x64.md` and `run.sh`:

| Setup | Machine | Device lines | Ports |
|---|---|---|---|
| Windows 2000 SP4 (`run-w2k.sh`, the xHCI-only image copied as `win2k-xonly.qcow2`) | `-machine pc,acpi=off -accel tcg -cpu pentium3,-apic -m 256 -vga cirrus -rtc base=localtime -net none`; driver files on a VVFAT IDE disk | `HOST=generic`: `usb4-tunnel-bridge,id=tb0,bus=pci.0,addr=0x5,chassis_nr=1,shpc-autoenable=on,msi=off`, `usb4-host,id=u4,bus=pci.0,addr=0x4,tunnel-port=tb0,msi=off,msix=off`, `usb4-device,id=enc0,bus=u4.0,port=1,drive=nvme0,serial=USB4EMU01`; `HOST=adl` adds `host-model=intel-adl`; `HOST=ar-preboot` the Alpine Ridge tree below with `preboot=on` and the boot ACL; `FORCE=on` adds `x-force-tunnel=on,nvme-drive=nvme0,nvme-serial=USB4EMU01` | QMP 47750, VNC 47751 |
| Windows 2000 re-enumeration (`run2k.sh <tag>`, `TOPO=ar`) | as above, overlay of `win2k-xonly.qcow2`, a raw transfer disk on IDE 1, `-debugcon file:` | `TOPO=ar`: `usb4-tunnel-bridge,id=tbrp,bus=pci.0,addr=0x1c,chassis_nr=1,msi=off`; `usb4-tb3-pci-bridge,id=tbusp,bus=tbrp,addr=0x1,chassis_nr=2,shpc=off,msi=off`; `usb4-tb3-pci-bridge,id=tbd0,bus=tbusp,addr=0x0,chassis_nr=3,...`; `usb4-tb3-tunnel-bridge,id=tb0,bus=tbusp,addr=0x1,chassis_nr=4,shpc-autoenable=on,msi=off`; `usb4-tb3-pci-bridge,id=tbd2,bus=tbusp,addr=0x2,chassis_nr=5,...`; `usb4-host,...,bus=tbd0,addr=0x0,tunnel-port=tb0,profile=tb3-alpine-ridge`; `usb4-device,...,dialect=tb3`; traces `usb4_tp*`, `usb4_tb*`, `shpc*`, `pci_nvme_*` | QMP 47810, VNC 47811 |
| Windows XP x64 (`run-xp64.sh`) | `-machine pc -accel tcg,thread=multi -cpu qemu64 -smp 2 -m 2048 -vga std -usb -device usb-tablet`, `-boot c`; q35 does not boot this image | the generic lines at 0x5 and 0x4 (`FORCE=on` as above); `MACH=q35` uses `usb4-tunnel-port` | QMP 47850, VNC 47851 |
| Windows 7 x64 (`run.sh`, `MACHINE=pc\|q35`) | `-accel tcg,thread=multi -cpu qemu64 -smp 4 -m 2048 -vga std -rtc base=2026-09-20T12:00:00 -no-shutdown`, IDE system disk, VVFAT transfer disk | `pc`: the generic lines (no `msi=off`); `q35`: `usb4-tunnel-port,id=tp0,bus=pcie.0,addr=5.0,chassis=1,slot=1` | **QMP 47901**, VNC 41950 |

The Windows 7 x64 setup's QMP port, 47901, sits inside the block record 21
section 10.2 gives this project (47900 to 47999) and collides with this
project's Windows 98 SE slot-0 VNC port; the launcher's bind probe refuses
to start on a port in use, so the collision cannot go unnoticed, and the
request to move it is in the usb4-emu request list of 2026-10-15 (not a
tracked file). The other `usb4-emu` guests running during these legs held
QMP 48040 to 48060.

### The launcher: `scripts\usb4\run-usb4-guest.ps1` and `usb4-qmp.ps1`

```
powershell -File scripts\usb4\run-usb4-guest.ps1 -Guest <guest> [-Usb4Profile tb3|generic|none]
    [-Name <run>] [-Bios OFF_OFF|OFF_ON|ON_OFF|ON_ON] [-DeviceAtBoot 0|1] [-RootAddr 0x6]
    [-HostModel minimal|intel-adl] [-PreosCm] [-ForceTunnel] [-Usb3] [-Dock]
    [-BootStrict] [-Xhci] [-Trace] [-TracePattern 'usb4_*,pci_nvme_*']
    [-XferDir <dir> | -XferImg vm\usb4\<run>\xfer*.img] [-NvmeBase <qcow2>] [-NvmeSize 1G]
    [-Slot 0-4] [-QmpPort n -VncPort n] [-Smp n] [-MemMB n] [-Tag <tag>] [-NewOverlay] [-DryRun]
    [-Extra:-S]
powershell -File scripts\usb4\usb4-qmp.ps1 -Name <run> status | unplug [id] | plug |
    tunnel up|down|eject|surprise | harness <x-prop> [value] | shot <file.png> |
    hmp <command> | key <chords> | type <text> | run <command line> | powerdown | quit |
    release | wait
```

- **Guests.** `win98`, `winme`, `win2k-xonly`, `win2k-smp`, `winxp`, `winxp64`,
  `vista`, `vista-x64`, `win7`, `win7-x64`, each from a qcow2 overlay
  `vm\usb4\<run>\disk.qcow2` over its `vm\<guest>.img` (never opened
  writable). The machine lines are those of `build-and-test.md` with two
  changes every guest needs on this fork: `-accel tcg` (`WHPX` aborts once any
  PCI-to-PCI bridge exists) and, on 98 SE, ME and the 2000 xHCI-only image,
  `-cpu pentium3,-apic`. XP and later run 2 vCPUs; the 2000 SMP image 2; 98,
  ME and the 2000 xHCI-only image 1. Ports: 47900 + 10 per guest + 2 per
  slot, VNC the next port.
- **Profiles.** `tb3` is `run-win98.sh`'s `BIOS=` tree (root bridge at
  `-RootAddr`, default `0x6`, since `c9e6d80`, so a function hot-added behind
  it gets an IRQ from SeaBIOS's `$PIR` on Windows 98), with `bios-assist` and
  `preboot` from `-Bios`, the boot ACL holding `enc0`, the xHCI `15C1` when
  Assist is off, and `enc0` with `dialect=tb3` when `-DeviceAtBoot 1`.
  `generic` is `usb4-generic` with the host at 00:04.0 and the SHPC tunnel
  bridge at 00:05.0, `-HostModel`, `-PreosCm`, `-ForceTunnel`. `-Usb3` adds a
  `qemu-xhci` (`u4x`, 00:07.0) with a USB disk that is attached only while a
  connection manager holds a USB3 tunnel to `enc0` (on a 98 SE or 2000 guest
  that controller is one `xhci98.sys` drives); `-Dock` adds `dock=on` and its
  NIC and USB-disk back ends (see the dock reading below).
- **Logs.** Every launch writes `debugcon-<tag>.log` (port 0xE9, what the
  `qemu` flavour prints), `serial-<tag>.log`, `qemu-<tag>.out` (QEMU's own
  messages) and, with `-Trace`, `trace-<tag>.log`. `run.json` records the
  process (pid, creation time, executable), the ports and the device router's
  `device_add` line.
- **Transfer disk.** A host directory as a read-only VVFAT drive, or a raw
  FAT image `xfer*.img` under `vm\usb4\` attached writable and read back with
  mtools after shutdown (the recipe of `usb4-emu`'s
  `tools\reenum-test\mkdisk.sh`; on 98 SE ScanDisk reports "cannot read the
  last cluster" of that image after an unclean shutdown, which is the
  recipe's CHS geometry, not damage; mtools reads it with
  `MTOOLS_SKIP_CHECK=1`).
- **Hot plug.** `unplug` is QMP `device_del enc0` (the device router leaves
  and the ICM or the CM sees the unplug); `plug` is `device_add` of the line
  the launcher built. `tunnel` is `qom-set` on the tunnel bridge, which
  bypasses any connection manager and is for driverless baselines only
  (record 21 section 10.2).
- **The fault-injection harness** (`usb4-emu` `docs/HARNESS.md`, in this
  build). Every `x-*` property sits on the tunnel bridge `tb0` and is set at
  run time with `harness`, for example `harness x-faults "at=fetch op=read
  nth=3 act=surprise"` (the tunnel taken down by surprise between the
  controller's fetch of the third NVMe read and its execution, the
  unplug-during-I/O leg), `harness x-gate reads-fail`, `harness x-hang ones`,
  or read back with `harness x-harness-state`. After a tunnel goes down the
  endpoint's registers read all-ones, which is a presence signal only
  (record 21 section 7.3). The stage-marker port is `-device
  usb4-debug-port` (I/O 0x5A0), which needs the `-Command` form of the
  launcher to pass `-Extra '-device','usb4-debug-port'`.
- **Guards** (reviewed and revised on 2026-10-15). The guest lock, the binary
  copy, the overlays and the launch run under `out\vmlocks\_admit`; the lock
  file names the branch and a per-launch token and is removed only by its
  owner; `-Extra` takes `-S` and `-device`, `-global`, `-trace` or `-d` with a
  value that names no back end, file or path; the helper sends nothing to a
  process it cannot confirm by pid, creation time, executable and QMP port.
- **Keyboards.** The 98 SE and ME images type as US-QWERTY; the 2000, XP and 7
  images as US-Dvorak (XP and 7 read here: QWERTY text came out transposed and
  Win+R opened on the Dvorak R). `type` and `run` map through Dvorak on every
  NT guest; `-Layout` overrides.

### `dock=on` in the tb3 dialect on i440FX (for 39-V.1, leg V.1.8)

**Not available.** `usb4-device` accepts `dock=on` with `dialect=tb3`, but the
i440FX tunnel bridge (`usb4-tunnel-bridge` and its `usb4-tb3-tunnel-bridge`
form) is not a dock-capable slot: on the first tunnel up QEMU printed
`warning: tb0: dock=on and downstream-ports need a PCIe tunnel port
(usb4-tunnel-port); inserting the NVMe only`, and the trace shows a plain NVMe
insert (`usb4_tp_insert tb0: insert endpoint gen 1 drive=nvme0`)
(`out\p39-vm\legs\dock-probe\`). The model's own comment says the same
("Dock mode (q35 usb4-tunnel-port only)"). A dock's xHCI under `xhci98.sys`
therefore needs a q35 guest, which this project's 9x and 2000 images are not,
or the bench: V.1.8 reads on the bench (39-E.1) unless `usb4-emu` grows an
i440FX dock.

### An NVMe present at power-on: which workaround the pre-boot legs need

The handoff's warning (an NVMe at POST makes SeaBIOS hook it into INT 13h and
Windows 98's GUI boot hangs at the logo) was read on this project's
`vm\win98.img` (98 SE, 256 MB, `pentium3,-apic`), with an overlay of
`usb4-emu`'s `vms\win98\nvme32.qcow2` (an MBR with an active FAT32 partition)
behind the tunnel:

| Run | Configuration | Result |
|---|---|---|
| `win98-pbs` | `tb3`, `BIOS=OFF_ON`, device at boot, `-BootStrict` (`-boot strict=on`, `bootindex=0` on the IDE disk) | first boot and a warm restart to the desktop; the trace shows the NVMe inserted cold (`cold=1`) before POST |
| `win98-pbc` | the same without `-BootStrict` | first boot, two warm restarts and a power-on boot to the desktop |
| `probe-post` | `tb3`, `OFF_ON`, `pci_nvme_*` traced | SeaBIOS does probe the NVMe behind the Alpine Ridge tree at POST: IDENTIFY controller and namespace, then a READ of LBA 0 |
| `win98-gft` | `generic`, `-ForceTunnel` (the handoff's own failing configuration) | to the desktop |
| `win98-pbc` + nvme9x | the `usb4-emu` `vms\win98\drvnew` build of nvme9x installed through the Add New Hardware wizard, then a restart through MS-DOS mode and a power-on boot, NVMe at POST each time | to the desktop both times, the NVMe's FAT32 volume mounted as F: (`dir f:\` listed `fromw98.txt`, 1.6 GB free) |

So **none of the three workarounds is needed on this project's image**: the
hang is particular to `usb4-emu`'s own Windows 98 image (512 MB, its own
install), not to SeaBIOS plus Windows 98 in general. `-boot strict=on` with a
boot index is harmless and is the cheapest of the three to carry, so V.1.3
runs with `-BootStrict` as insurance; `BootGUI=0` then `win` was not needed and
not exercised; a SeaBIOS without NVMe support is not built on this host and is
not needed.

### Device Manager with the Alpine Ridge model (`tb3`, `OFF_OFF`)

| Guest | Reading | Evidence |
|---|---|---|
| 98 SE | First boot installs the five bridges as "PCI standard PCI-to-PCI bridge" through the wizard; **the NHI `8086:15BF` is "PCI System Peripheral" under Other devices, no driver** (`HardwareID` `PCI\VEN_8086&DEV_15BF&SUBSYS_22B017AA&REV_01,...`, class `Unknown`, `ConfigFlags` 0x40); by connection it sits under the root bridge, the upstream port and the first downstream port. The xHCI `15C1` is bound by `xhci98.sys` ("USB 2.0 eXtensible Host Controller (xhci98)", already on this image) with its root hub | `legs\win98-tb3\s6.png`, `s7.png`, `s10.png`, `out1\ENUM98A.REG` |
| 2000 SP4 | The bridges install silently and ask for a restart; **the NHI is "Base System Device" under Other devices, no driver** (Problem 1, the Found New Hardware wizard cancelled); the xHCI `15C1` "Universal Serial Bus (USB) Controller" also under the wizard | `legs\w2k-tb3\s16.png`, `out0\ENUM0.REG`, `out1\OUT\IDS1.TXT` |

### The generic profile (`usb4-generic`, `minimal`)

| Guest | Reading |
|---|---|
| 2000 SP4 | The host interface `1B36:00F4`, subsystem `1AF4:1100`, rev 01, at 00:04.0, **class `0C0340`** (`PCI\VEN_1B36&DEV_00F4&CC_0C0340` among its hardware ids), "Universal Serial Bus (USB) Controller" under Other devices with no driver; the tunnel bridge `1B36:0001` at 00:05.0 takes `machine.inf`'s `PCI_DRV`. `xhci98.inf` does not claim it (`legs\w2k-gen\out1\IDSG.TXT`) |
| 98 SE | The host is "PCI Universal Serial Bus" under Other devices, no driver, not claimed by `xhci98.sys` (`legs\win98-gen\s4.png`) |

### The bridge ids the emulator presents on i440FX

| Function | Location (98 SE and 2000) | Ids | Notes |
|---|---|---|---|
| Root bridge (stand-in for the PCH root port) | 00:06.0 | `1B36:0001` rev 00, subsystem 0000:0000, class 060400 | QEMU's `pci-bridge` with the tunnel gate; the P14s has `8086:02BC` here |
| Switch upstream port | 01:01.0 | `8086:15C0` rev 01, subsystem 0000:0000, class 060400 | |
| Downstream port to the NHI | 02:00.0 | `8086:15C0` rev 01 | NHI `8086:15BF` (subsystem `17AA:22B0`, class 088000) at 03:00.0 |
| **Downstream port to the tunnel** | **02:01.0** | **`8086:15C0` rev 01** | SHPC capability, secondary bus 4; the only port with a hot-plug controller, which is what record 21 section 6.3's rule picks |
| Downstream port to the xHCI | 02:02.0 | `8086:15C0` rev 01 | xHCI `8086:15C1` (subsystem `17AA:22B0`, class 0C0330) at 05:00.0, present with Assist off |
| Generic profile's tunnel bridge | 00:05.0 | `1B36:0001` rev 00 | host `1B36:00F4` at 00:04.0 |

So on the `tb3` profile the stand-in ports **are** `8086:15C0`, every one of
them, as on the P14s, and record 21's model line needs no change for the V
legs. The hardware ids each OS builds for them:

- 98 SE: `PCI\VEN_8086&DEV_15C0&SUBSYS_00000000&REV_01`, `...&SUBSYS_00000000`,
  `...&REV_01`, `PCI\VEN_8086&DEV_15C0`, `...&REV_01&CC_0604`,
  `...&CC_060400`, **`PCI\VEN_8086&DEV_15C0&CC_0604`**; compatible
  `PCI\VEN_8086&CC_060400`, `PCI\VEN_8086&CC_0604`, `PCI\VEN_8086`,
  `PCI\CC_060400`, `PCI\CC_0604`.
- 2000, XP and 7: the same without the `REV_01&CC_0604` one.

The id `PCI\VEN_8086&DEV_15C0&CC_0604` is therefore a hardware id on all four,
**98 SE included**.

## 39-0.4 - The NT filter's install route (decision D2)

**Verdict: the device-specific route works on Windows 2000, XP and 7; a class
filter is not needed. Windows 98 SE and ME do NOT ignore a `$Windows NT$`
INF, so the package must keep the filter INF out of every place their engine
looks** (2026-10-15).

**The test filter.** `usb4-emu`'s `tools\reenum-test\out\reenum.sys` (MIT,
built 2026-10-14 18:14, SHA-256
`3A4FFDFB424B88C8C172DEBEAC3F5C12CC6FFCBE32148B055CAC1ADEEF174198`),
used read-only: a WDM upper filter that logs every `AddDevice` and its
attachment to port 0xE9, polls the bridge's secondary bus and calls
`IoInvalidateDeviceRelations(BusRelations)` on a presence change. Installed
by `devq.exe`, a throwaway reading tool built here with the in-repo MSVC 6.0
(`UpdateDriverForPlugAndPlayDevices` with and without `INSTALLFLAG_FORCE`,
and a dump of each PCI devnode's ids, service, filters and driver key),
since the WDK 7.1 `devcon.exe` does not run on Windows 2000 (it needs
`msvcrt.dll!__iob_func`).

**The INF shapes** (`out\p39-vm\x2k\`, `out\p39-vm\x98\`):

- **A**, the proposed shape: `Signature="$Windows NT$"`, an undecorated
  `[FltModels]` with `%TunnelDesc%=Flt.Dev,PCI\VEN_8086&DEV_15C0&CC_0604`,
  and `.NTx86` sections only: `Flt.Dev.NTx86` with `Include=machine.inf`,
  `Needs=PCI_DRV` and the filter's `CopyFiles`; `Flt.Dev.NTx86.HW` with
  `Include=machine.inf`, `Needs=PCI_DRV.HW` and `HKR,,UpperFilters,0x00010000,reenum`;
  `Flt.Dev.NTx86.Services` with `Include=machine.inf`, `Needs=PCI_DRV.Services`
  and `AddService=reenum,,Flt.Svc` (kernel driver, demand start). `PCI_DRV`,
  `PCI_DRV.HW` and `PCI_DRV.Services` are the section names of both the
  2000 SP4 and the XP SP3 `machine.inf` (read from the install media).
- **B**: A without `Include` and `Needs`, naming `pci` as the service
  directly; carried for the 98 readings only, not installed on NT.
- **C**, the probe for 98: A with the model line `PCI\VEN_8086&DEV_15C0` and an
  added undecorated `[Flt.Dev]` (AddReg only).
- **D**, the second probe for 98: A plus an undecorated `[Flt.Dev]`.
- `NHI98.INF`: a `$CHICAGO$` INF for `PCI\VEN_8086&DEV_15BF`, the "other INF"
  beside them.

| Guest | Reading |
|---|---|
| 2000 SP4 (`w2k-tb3`) | `devq update FLTA.INF PCI\VEN_8086&DEV_15C0&CC_0604` without force: **installed on all four `15C0` ports, `reboot=0`**. Each port afterwards: `Service` `pci` (machine.inf's service through `Needs`), `UpperFilters` `reenum`, driver key `InfPath` `oem1.inf`, `InfSection` `Flt.Dev`, `MatchingDeviceId` `pci\ven_8086&dev_15c0&cc_0604`, status `0x0180200A` (started), problem 0. The filter loaded at once and attached to 01:01.0, 02:00.0, 02:01.0 and 02:02.0. With `tunnel up` the filter on 02:01.0 saw bus 4 populate and invalidated, and Windows found the NVMe; with `tunnel surprise` it invalidated again. Two restarts: no `0x7B`, the filter loads at boot on all four (a demand-start device filter on bridges that are not on the boot path). The root bridge `1B36:0001` kept `machine.inf`. setupapi logged the INF as unsigned; no prompt appeared on this image |
| XP SP3 (`winxp-tb3`) | The same `update` without force: **refused, `ERROR_NO_MORE_ITEMS` (0x103)**. setupapi gives the reason: the unsigned A ranks `0x00008005`, `machine.inf`'s signed `PCI_DRV` `0x00002004`, so the current driver is better. With `INSTALLFLAG_FORCE`: installed on all four, `reboot=1` asked, yet the filter loaded and attached at once and re-enumerated on `tunnel up`; after a restart the four ports read `Service` `pci`, `UpperFilters` `reenum`, `InfPath` `oem0.inf`, started |
| 7 SP1 x86 (`win7-tb3`) | Without force: refused, 0x103, as on XP. With force: "Windows can't verify the publisher of this driver software", Install anyway; `reboot=1`, and the filter loaded only after the restart, then attached to every port and re-enumerated on `tunnel up`; afterwards `Service` `pci`, `UpperFilters` `reenum`, `InfPath` `oem2.inf`. During that boot the filter on one port briefly read a secondary bus of 0xFF while `pci.sys` renumbered the buses, and invalidated twice |
| 98 SE (`win98-tb3`) | With A, B, C and `NHI98.INF` copied to `C:\WINDOWS\INF\OTHER`, the tunnel bridge removed and Device Manager refreshed: Windows rebuilt its driver database and **installed C on the tunnel bridge** ("Test Tunnel Port Filter C", `Mfg` from C, driver key `InfPath` `FLTC.INF`, `InfSection` `Flt.Dev`, `MatchingDeviceId` `PCI\VEN_8086&DEV_15C0`), so the signature did not stop it; the NHI took `NHI98.INF` ("Test NHI 98"). With C deleted and the bridge removed again, it came back as "PCI PCI-to-PCI Bridge" and Windows "was unable to locate a driver", and the same after A and B were deleted too: no rebuild of the driver database was seen after the deletions, which is the likely cause, and the bridge was left without a driver |
| 98 SE (`win98-fa`, a fresh overlay) | With A alone in `INF\OTHER`, and again with A and D: the automatic search for the re-detected tunnel bridge chose `C:\WINDOWS\INF\MACHINE2.INF` ("PCI standard PCI-to-PCI bridge"). But "Display a list of all the drivers in a specific location", Have Disk, `C:\WINDOWS\INF\OTHER` **listed "Test Tunnel Port Filter (39-0.4)" as compatible hardware for the bridge** |
| ME (`winme-tb3`) | For a newly found `15C0` port, "Specify the location of the driver" with `D:\` holding A and `NHI98.INF`: "Windows has found multiple software packages", recommended "PCI standard PCI-to-PCI bridge", and **A listed under "One of the other packages"** (`D:\FLTA.INF`, provider "xhci98 39-0.4 reading") |

What this settles for record 21 sections 3.2, 3.3 and 6.2:

1. **Decision D2 is answered: the device-specific route.** A separate NT-only
   INF whose DDInstall includes `machine.inf` and needs `PCI_DRV`,
   `PCI_DRV.HW` and `PCI_DRV.Services` adds a device-specific
   `UpperFilters` value without displacing `pci.sys` as the port's service,
   on 2000, XP and 7. No class filter, no boot start, no `0x7B`.
2. **On XP and later the unsigned filter INF loses the ranking** to the
   signed `machine.inf` (`0x8005` against `0x2004`), so the install has to
   force it: `UpdateDriverForPlugAndPlayDevices` with `INSTALLFLAG_FORCE`, or
   Update Driver, "Don't search", Have Disk. 2000 takes it unforced. A port
   that appears for the first time after the install (with BIOS Assist on,
   the controller's tree is absent until a device arrives) gets
   `machine.inf` again on XP and later; the install route must be run, or the
   filter re-applied, once the tree exists. A signed catalog would remove the
   penalty and is not in this project's x86 package.
3. **Windows 98 SE's and ME's engines read `$Windows NT$` INFs.** The
   `BOTH-VERSION` belief that the signature alone keeps the filter INF away
   from 98 is wrong. The model line in the `&CC_0604` form was not chosen by
   98 SE's automatic search (with or without an undecorated install section)
   nor recommended by ME's, but both list it to a user who browses for a
   driver, and the plain `PCI\VEN_8086&DEV_15C0` form was installed outright.
   So: the filter INF carries only the `&CC_0604` model line and no
   undecorated install section, and it lives where neither 9x engine looks:
   not beside `xhcicm.inf` in the package directory a 98 user points the
   wizard at, never copied into a 9x `INF` directory (a separate `nt\`
   directory of the package, installed by the NT install route only).
   `CM-NO9XBRIDGE` in `check-cm-inf.ps1` should check that layout and the
   model line's form, not only the signature.

**The proposed INF shape, as a draft** (the names are record 21's; the
service is the NHI driver's own image, as section 3.3 says):

```
; xhcicmf.inf - NT only. Never placed where the 9x engine looks (see 39-0.4).
[Version]
Signature="$Windows NT$"
Class=System
ClassGuid={4D36E97D-E325-11CE-BFC1-08002BE10318}
Provider=%Mfg%
DriverVer=<date>,<version>

[Manufacturer]
%Mfg%=CmFilterModels,NTx86.6.0

; Windows 2000 reads only the undecorated section; XP reads it too.
[CmFilterModels]
%TunnelDesc%=CmFilter.Dev,PCI\VEN_8086&DEV_15C0&CC_0604

[CmFilterModels.NTx86.6.0]
%TunnelDesc%=CmFilter.Dev,PCI\VEN_8086&DEV_15C0&CC_0604

[DestinationDirs]
DefaultDestDir=12

; No undecorated [CmFilter.Dev]: nothing here is installable on 9x.
[CmFilter.Dev.NTx86]
Include=machine.inf
Needs=PCI_DRV
CopyFiles=CmFilter.Copy

[CmFilter.Dev.NTx86.HW]
Include=machine.inf
Needs=PCI_DRV.HW
AddReg=CmFilter.HW.AddReg

[CmFilter.Dev.NTx86.Services]
Include=machine.inf
Needs=PCI_DRV.Services
AddService=xhcicm,,CmFilter.Svc

[CmFilter.HW.AddReg]
; MULTI_SZ, appended if absent, so another filter on the port survives
HKR,,UpperFilters,0x00010008,xhcicm

[CmFilter.Svc]
DisplayName=%SvcDesc%
ServiceType=1
StartType=3
ErrorControl=1
ServiceBinary=%12%\xhcicm.sys

[CmFilter.Copy]
xhcicm.sys
```

with `.NTx86.6.0` copies of the three DDInstall sections if Vista and 7 need
anything different (the readings used the `.NTx86` sections on 7 and they
applied), the `[SourceDisksNames]`, `[SourceDisksFiles]` and `[Strings]`
sections, and the amd64 twin on the same pattern. Read, not yet tested: the
append flag `0x00010008` (the readings used `0x00010000`, on ports that had
no `UpperFilters`), the `.NTx86.6.0` Models section, and the 64-bit guests.

**The source disk, read again 2026-10-15 (task 40.6's legs).** The package
first staged the filter INF in `nt\` with its one source disk at `..`, the
flavour directory where `xhcicm.sys` is. On Windows 7 SP1 x86 the install
tool's `SetupCopyOEMInf` of that `nt\xhcicmf.inf` failed with `0x000000A1`
(`ERROR_BAD_PATHNAME`): Vista's and 7's driver store takes no source file
outside the INF's own directory tree, so no route - the tool, Have Disk or
`UpdateDriverForPlugAndPlayDevices` - could install the filter there. So
`nt\` now carries its own copy of `xhcicm.sys`, the same bytes as the
flavour directory's (copied after test-signing), and both filter INFs name
a source disk with no path; `CM-NO9XBRIDGE` admits exactly those two files
in `nt\` and refuses two copies that differ, and the release assembly
refuses them too. Windows 98 SE and ME never receive `nt\`, so nothing
changes for them (an orchestrator decision, for the owner to confirm).

### A side reading for the 98 legs: the 0028:C0031B0A fault is NUSB's usbport, not the driver

On 98 SE, removing the root bridge in Device Manager while `xhci98.sys` was
bound to the xHCI `15C1` below it stopped Windows with "A fatal exception 0E
has occurred at 0028:C0031B0A", then Rundll32 failed and the desktop did not
recover until a reset (`legs\win98-tb3\s18.png` to `s20.png`). Removing the
empty tunnel bridge or the NHI alone did not. **Attribution, read on
2026-10-15 (branch `p39-xrm`; evidence in `out\p39-xrm\legs\`): the fault
belongs to NUSB 3.3's `USBPORT.SYS` teardown under the frozen `1.x`
miniport, the known `0028:C00312EE` fault at another load address; the
`2.x`/`3.0.0.0` host controller driver does not fault.**

**What was bound in that reading.** `vm\win98.img` carries the miniport:
the debug-console log of that run (`debugcon-ar2.log`) opens with
`DriverEntry (built Sep  3 2026 20:24:44)`, `USBPORT_GetHciMn=57324B30` and
`registered with usbport.sys`, so the `15C1` was driven by the `1.x`
miniport under NUSB's 5.00.2195-lineage `usbport.sys`. The log of the boot
that faulted stops at `cb RH_DisableIrq irql=00` (with `RH irq gate
closes`), with no `DisableInterrupts` or `StopController` after it: the
signature AGENTS.md's frozen miniport and `lessons.md` ("Option A works on
Win98", "The Windows 98 teardown bugcheck belongs to the Windows
2000-lineage usbport") record for every controller stop on that stack.

| Run | Image, driver | Removal | Result |
|---|---|---|---|
| `98m` | a fresh overlay of `vm\win98.img`, `tb3` OFF_OFF with `-Usb3` as p39-vm ran it: the `1.x` miniport (built Sep 3 2026, debug) on `15C1` and on QEMU's xHCI, NUSB 3.3 | Device Manager, Remove on the root bridge `1B36:0001` (00:06.0) | **fatal exception 0E at 0028:C0031B0A**, reproduced; the miniport's trace ends at `cb RH_DisableIrq irql=00` for the `15C1` |
| `98r` r1 | an overlay of `vm\t26\win98-gold.qcow2` (98 SE, NUSB 3.3, the host controller driver installed; its `AUTOEXEC.BAT` copies `D:\XHCI98.SYS` over the installed driver), the published `2.4.0.0` `release-x86` `xhci98.sys` (SHA-256 `bd5ab1ac...57be`) on `15C1`, its root hub from the `2.4.0.0` INF | the same | removed cleanly, no fault; Refresh brought the bridges, the controller and its root hub back |
| `98r` r2 | the same overlay, `3.0.0.0` at `55936d1` `qemu` flavour (SHA-256 `b89ba192...a48f`) | the same | removed cleanly: the trace reads root hub query-remove and remove ("dropping every device"), controller `IRP_MN_QUERY_REMOVE_DEVICE` then `IRP_MN_REMOVE_DEVICE`, the controller thread stopped, eight ports unpowered, `quiesce: halted, USBSTS=00000001`, every release; Refresh restarted the controller with its root hub |
| `2k` k2 | a fresh overlay of `vm\win2k-xonly.img`, `tb3` OFF_OFF, the same `qemu` build installed on `15C1` with `devq update` | `devq remove` (`DIF_REMOVE`, Device Manager's Remove) on the root bridge | removed cleanly, the same sequence; after the restart Windows 2000 asks for, the re-detected controller was installed again (`devq update`) and started |

**Where the fault is.** With QEMU's `log int` on across the removal in `98m`,
the one ring-0 page fault at the address is `v=0e e=0000 cpl=0
IP=0028:c0031b0a CR2=0465057d`, `ECX=0465056d`. The bytes at the address
are `8B 49 08 8B 49 10` (`mov ecx,[ecx+8]; mov ecx,[ecx+10h]`), the same
two-step walk over a pointer that is already garbage that `lessons.md`
records at `0028:C00312EE`. The return addresses on the stack, matched
byte for byte against the files: `C0031DA7`, `C0032404` and `C003016C`
(VMM-range code, compressed inside `VMM32.VXD`, not matched), then
`C002CEF8`, `C002CEB5`, `C002FEA4` and `C002B818` in Windows 98 SE's
`NTKERN.VXD` (file offsets `0x110E4`, `0x110A4`, `0x14094`, `0xFA04`
blocks), then `FF046C6F`, `FF041EB5`, `FF04E2C7`, `FF042FF4` and
`FF042EEF` in NUSB 3.3's `USBPORT.SYS` (SHA-256 `eec79b5a...9b55`;
file offsets `0x51B0`, `0x400`, `0xC814`, `0x1554`, `0x1440` blocks). No
frame is in the miniport, whose code that boot sat at `FF043xxx` to
`FF05Exxx` by its own service pointers. So: NUSB's usbport, in the stop
it runs before calling the miniport's `DisableInterrupts`, calls an NTKERN
service that walks a VMM list through a stale element. The emulator, PCI's
`CONFIG_REMOVE` ordering and the NVMe are not involved: the same removal
on the same tree completes when the host controller driver is bound, and
the controller is stopped while its registers are still live (Windows 98
removes the children before the bridge).

**The tunnelled endpoint alone.** With the `3.0.0.0` `qemu` build on `15C1`,
`tunnel up` (the NVMe on bus 4) and then `usb4-qmp unplug` (`device_del
enc0`) on 98 SE, and `plug`, `unplug` and `tunnel surprise` on 2000: no
fault, the controller untouched (it is not behind the tunnel on this tree).
Neither 98 SE nor 2000 has a driver for the tunnel bridge's hot-plug
controller here, so neither noticed the NVMe come or go.

**What this does not cover.** A Device Manager removal is orderly: query
remove, remove, children first, the controller's registers readable
throughout. A dock's xHCI vanishing on unplug is not: the registers read
all-ones before Windows hears of it. The emulator cannot do that on
i440FX today (`device_del tbrp`: "Device 'usb4-tunnel-bridge' does not
support hotplugging"; the xHCI's port has no hot-plug controller; `dock=on`
is q35-only, above), so that case stays with V.1.8 on the bench (39-E.1)
or a future emulator change, which is requested in the `usb4-emu` request
list of 2026-10-15 (not a tracked file). The driver's own handling of
all-ones reads is design record 17's and was read with the `qemu`
flavour's fault injection for `2.2.0.0`.

## 39-A.4 and 39-A.6 - The NHI driver and the approval policy

**Verdict: the driver legs pass on both primary guests** (2026-10-15). The
NHI function driver of `xhcicm.sys` bound the emulator's Alpine Ridge NHI
on Windows 98 SE and Windows 2000, ran the ICM client's whole bind on its
own thread, approved the announced device at `XhciCmApprove` 1 and withheld
it at 0, handed the tunnel actions to the re-enumeration seam (a stub in
this branch; task 39-A.5 implements it), and survived a disable, an enable
and an unplug on both.

**The vehicle.** Development host A, TCG, the `usb4-emu` fork through
task 39-0.7's launcher (`run-usb4-guest.ps1 -Usb4Profile tb3`, `Bios
OFF_OFF`, the device router `enc0` with its NVMe attached at power-on, the
root bridge at slot 6), used read-only from a copy outside the tree. Guests
`win98` and `win2k-xonly` from their own overlays (`vm\usb4\nhi-98`,
`vm\usb4\nhi-2k`; `vm\win98.img` carries the frozen `1.x` miniport under
NUSB, which these legs neither need nor touch, the NHI driver being a
binary of its own), QMP and VNC on 47960 to 47963, `-trace usb4_*`, the
package's `xhcicm.inf` and `xhcicm.sys` on the VVFAT transfer drive (D: on
98 SE, E: on 2000). The binary was the `qemu` flavour of commit
`4e88bf3d` (the driver after its first review round); the later rounds
changed the power and race paths, which these legs do not reach (no guest
here sleeps), and the readings below are what the final build owes again
before the driver's checkpoint. Driven entirely over QMP keys and
screendumps; on 2000 the guest's keyboard is Dvorak.

**Install.** Both guests found the switch's bridges first (machine.inf on
98 SE, a restart asked for on 2000), then the NHI: 98 SE's Add New
Hardware wizard and 2000's Found New Hardware wizard took `xhcicm.inf`
from the transfer drive, "xHCI98 Thunderbolt 3 Host Interface", Device
Manager "This device is working properly" (98 SE) and no problem mark
(2000). The INF wrote every value at its default (98 SE:
`HKLM\System\CurrentControlSet\Services\Class\System\0034`; 2000:
`...\Control\Class\{4D36E97D-...}\0014`).

**What the emulator saw** (its `usb4_*` trace), the same on both guests:

| Step | Trace |
|---|---|
| start, `XhciCmApprove` 1 | ring 0 TX and RX enabled raw; mailbox `0x23` ok; ICM command `0x03` driver ready (security 1, boot ACL 16 slots); device connected link 1 `approved 0`; the host-router read; get route `0x0a` link 1 depth 1 to route 1; approve `0x04` with the UUID, key `0x10`; the PCIe path up; "ICM approve link 1: tunnel up" |
| disable (98 SE: Device Manager's "Disable in this hardware profile"; 2000: Action, Disable) | rings disabled, then mailbox `0x07` ok - in that order (record 21 section 5.6) |
| enable | rings, `0x23`, driver ready; device connected `approved 1` (the tunnel kept across the disable); get route; no approve |
| `device_del enc0` | device disconnected link 1 |
| `XhciCmApprove` 0 (set in regedit, then disable and enable), `device_add enc0` | device connected `approved 0`; get route; no approve command |

**What the driver logged** (`XhciLogVerbosity` 2, the `qemu` flavour's
port-0xE9 mirror, emitted from the connection-manager thread after each
pass): the values as read; `cm.bar.length=00040000`; `cm.cfg.ids=15BF8086`,
`cm.cfg.class=00088000`, `cm.cfg.subsystem=22B017AA`;
`cm.hw.profile=00000002` (Alpine Ridge); `tbt.hw.fw-status=800001A1`,
`tbt.hw.mode=00000003`; `cm.select.outcome=00000001` (ICM); `cm.bus-master`;
`cm.reenum.start`; `tbt.icm.security-level=00000001`; `cm.bind=00000000`;
on the enable with the device present `cm.action.tunnel-up=00000002` (why:
announced already approved), `cm.action.uuid0=4DE23D20` and
`cm.reenum.not-done` (the stub); on the unplug `tbt.icm.device-gone` and
`cm.action.tunnel-down=00000003` (why: disconnected); at `XhciCmApprove` 0
`tbt.icm.connected` then `tbt.icm.approve-withheld`. Windows 2000's
DbgPrint path gave the same lines.

**The final build, read again.** The `qemu` flavour of the reviewed
driver (commit `08eb9304`, built x86 `all` and `-amd64 all` with every
gate) replaced the earlier one on the 98 SE guest through Device
Manager's Update Driver and Have Disk: the old instance's stop stopped the
rings and then sent `0x07`, logging the device table and its counters
(`cm.icm.approve.withheld=00000001`); the new one bound (`0x23`, driver
ready, the device announced `approved 0`, get route) and, at
`XhciCmApprove` 0, withheld it; `device_del enc0` then gave
`cm.action.tunnel-down=00000003`. The 2000 legs were not repeated on that
build.

**What is not read here.** Re-enumeration (the stub logs; 39-A.5), safe
eject (39-A.8), the other rows (only Alpine Ridge is modelled), suspend and
resume, shutdown, surprise removal of the NHI itself, and the `release` and
`debug` flavours. On both guests the Thunderbolt controller's own xHCI
`15C1`, behind the switch, raised a wizard offering the base image's
older `xhci98` INF, which was cancelled: it is `xhci98.sys`'s device, not
this driver's.

## 39-A.5 and 39-A.8 - Re-enumeration and safe eject

**Verdict: re-enumeration passes on both primary guests, and safe eject on
Windows 2000; on Windows 98 SE the removable flag is set and nothing on 98
SE offers an eject** (2026-10-15). With `xhcicm.sys` bound to the emulator's
Alpine Ridge NHI, a device approved by the ICM client appeared on Windows
2000 and Windows 98 SE with no Refresh and no "Scan for hardware changes",
and left when its device router was removed; on Windows 2000 the device was
listed in "Unplug or Eject Hardware", stopped from there, and then unplugged
with no Unsafe Removal dialog.

**The vehicle.** Development host A, TCG, the `usb4-emu` fork through task
39-0.7's launcher (`-Usb4Profile tb3`, `Bios OFF_OFF`, `-DeviceAtBoot 0`, the
root bridge at slot 6, slot 4: QMP and VNC 47908/47909 and 47928/47929), the
launcher copied read-only outside the tree. Guests: `win2k-xonly` from a copy
of the 39-A.1 smoke's overlay (`vm\usb4\reenum-2k`, `xhcicm.inf` already
bound to the NHI), and Windows 98 SE from an overlay of
`vm\t26\win98-gold.qcow2` (`vm\usb4\reenum-98`, ADDENDUM 8 of the brief:
the base `win98.img` carries the frozen miniport). The QEMU binary was the
copy in `vm\usb4\bin`, refreshed to the fork's `a50c19e` build during the
session; which readings ran on which build was not recorded per leg. The `qemu` flavour of
`xhcicm.sys`, `XhciLogVerbosity` 2, its port-0xE9 lines in the debug-console
log. Driven over QMP keys and screendumps; the 2000 guest types Dvorak.
`nvme98` 0.1.0.0 (its published `releases\` x86 build, used read-only) was
the NVMe driver on 2000; 98 SE ran with no NVMe driver (the device's own
wizard was cancelled).

**Install.** 2000: `xhcicm.sys` copied over the bound one and a restart; the
filter through the 39-0.4 route (`devq force xhcicmf.inf
PCI\VEN_8086&DEV_15C0&CC_0604`, OK, `reboot=0`, then a restart so the new
image loaded). 98 SE: the switch's bridges through 98 SE's own wizard
(machine.inf), the NHI through Device Manager (Remove, Refresh, then "Specify
a location" D:\ to `xhcicm.inf`), "xHCI98 Thunderbolt 3 Host Interface",
working, with no restart. `xhcicmf.inf` is not installed on 98 SE.

**What the driver found** (both guests, `cm.reenum.*`): the NHI at 03:00.0
(`nhi=00000300`), its parent 02:00.0, two candidates on bus 2, 02:01.0 and
02:02.0, and one member, 02:01.0 by source 5 (SHPC: the emulator's i440FX
tunnel bridge, record 21 section 6.3 source 4), exclusive and active
(`member.flags=00020803`). Its port reading gives nothing to read (the SHPC
bridge has no PCI Express capability and no qualified by-offset writer
exists for its DWORD Select, so the SHPC slot is not read): every poll ended
at once with `up.poll=00000404` / `down.poll=00000404` (NO_READING, NONE),
and the re-enumeration followed.

| Leg | Windows 2000 | Windows 98 SE |
|---|---|---|
| ICM bind, driver ready, approve | `cm.bind=0`, `tbt.icm.connected=00000811`, `tbt.icm.approved`, `cm.action.tunnel-up=00000001` | the same |
| tunnel up (`device_add enc0`) | `IoInvalidateDeviceRelations` on 02:01.0 through its filter; the NVMe `1B36:0010` reported on bus 4 at once (Found New Hardware, then `nvme98` bound), the "Unplug or Eject Hardware" icon appeared | the bridge's devnode found by its path id; `cm.reenum.9x.reenumerate=00020800` (CR_SUCCESS); Add New Hardware for "PCI Card" at once; `cm.reenum.9x.removable=00000000` (DN_REMOVABLE added, CR_SUCCESS) |
| tunnel down (`device_del enc0`) | `tbt.icm.device-gone`, `cm.action.tunnel-down=00000003`, the invalidation; `devq ids 1B36` no longer lists the NVMe | `cm.reenum.9x.reenumerate=00020800`; a re-plug raised Add New Hardware again (a new devnode) and added DN_REMOVABLE again |
| safe eject | the NVMe listed in "Unplug or Eject Hardware" ("NVM Express Controller (nvme98) at PCI Slot 5"); Stop, OK: "Safe To Remove Hardware"; the driver logged `cm.reenum.eject=00000208` and `cm.reenum.eject.tunnel-left-up` (the ICM has no per-device teardown, record 21 section 7.4); then `device_del enc0`: no dialog | not offered: 98 SE has no eject applet; DN_REMOVABLE reads set (the CONFIGRET above); whether ME's applet lists it is 39-V.3's |
| unplug without an eject (`Removable` alone) | Windows 2000's "Unsafe Removal of Device" dialog, naming the controller and its disk - the release-notes line of record 21 section 7.4 stands for 2000 | - |
| disable and enable the NHI | disable: the stop's `cm.reenum.*` counters (`calls=1`, `child.attached=1`) and the client's (`approve.ok=1`, `mailbox.ok=2`); enable: rebind, the device announced approved (`connected=00000911`), `tunnel-up=00000002`, re-enumeration | (39-A.4's leg) |
| `XhciCmReenum` 0 (regedit, then disable and enable) | `cm.reenum.not-done=000001FF` at the tunnel up and `000002FF` at the tunnel down: no call made | not run |

**The last build, read again on 2000** (commit `4d33e5b0`'s driver, whose
eject hand-off waits for the bridge to settle after a tunnel change):
tunnel up, the NVMe listed in "Unplug or Eject Hardware", Stop, "Safe To
Remove Hardware", `cm.reenum.eject=00000208` and `eject.tunnel-left-up`,
then `device_del enc0` with no dialog. The combined legs of the table (bind,
driver ready, approve, tunnel up and down, disable and enable,
`XhciCmReenum` 0) ran on the merged build carrying p39-nhi's final driver
(`ee23bc17`), and are the re-read 39-A.4's 2000 legs owed.

**98 SE's instance ids** (read through the walk's own diagnostic, then
used): a function behind a PCI-to-PCI bridge is not named
`BUS_bb&DEV_dd&FUNC_ff` on 98 SE but by its path, two hexadecimal digits
per element, leaf first - its devfn, each parent bridge's devfn, then the
root bus: 01:01.0 is `083000`, 02:01.0 `08083000`, 02:02.0 `10083000`,
02:00.0 `00083000`, the NHI at 03:00.0 `0000083000`, the xHCI at 05:00.0
`0010083000`. The experiment's hot-added `082800` is the same form. The
first build matched the `BUS_` form alone and found no devnode; the second
matches both.

**98 SE at boot.** With the NHI started during 98 SE's boot (after a
restart), the first discovery found no candidate on bus 2 (siblings 0)
where the same guest, the driver installed from the desktop, had found two;
the rediscovery from the thread's pass, 2 s later, found the set. The cause
(configuration space behind the bridges not yet as it reads later, or the
HAL's view of bus 2 at that point of the boot) is not read.

**Two readings from the installer's filter runs** (p40-inst, its evidence
under `out\p40-inst\legs\w2kc\`), recorded here because they concern the
filter: (1) after `xhcicmf.inf` was installed, `Services\xhcicm` read
`Start=0` on 2000, XP and 7 although both INFs say `StartType=3`; the cause
is not confirmed (the likeliest is Windows promoting the service of an upper
filter on a PCI bridge to boot start). `DriverEntry` and the filter role use
nothing a boot start lacks (no registry read in `DriverEntry`, non-paged
extensions, spin locks and events); the NHI role is still started by PnP.
(2) On the first unforced install on 2000 the switch's upstream port (also
`8086:15C0`) was left at Code 12 with its subtree gone until a restart. The
INF matches all four `15C0` ports by id and cannot tell the upstream port
from the others; the restart of the upstream port's stack by the install,
with its subtree's resources to place again, is the likelier cause than the
filter's attach, and declining the attach in `AddDevice` would not avoid
that restart. The install route should apply the filter to the downstream
ports only, or the release notes say a restart follows the filter's install
on 2000.

**Not read here.** The resume hold (no guest here sleeps); a removal refused
by a driver with open files on 98 SE (no NVMe driver on that guest; the
refusal path is in the code, logged and counted, never forced); ME; XP and
later; `SurpriseRemovalOK` set together with `Removable` on 2000 (a variant
build, owed to 39-V.2); the Titan Ridge model; the `release` and `debug`
flavours.

## 39-A.7 - The `nvme98` link-state channel (ICM profile)

**Verdict: on Windows 2000 the channel meets its three obligations on the
emulator, against `nvme98`'s own receiver; on Windows 98 SE the image
loads and the record is written, and no statement is possible there**
(2026-10-15). With `xhcicm.sys` bound to the emulator's Alpine Ridge NHI
and `nvme98`'s gate-G3 prototype bound to the tunnelled NVMe controller,
the table entry was published `UP` before `nvme98`'s `HwFindAdapter` read
it, the slot-empty reading after a surprise unplug produced
`DOWN_QUIESCED`, `nvme98` applied it and acknowledged, and the removal
started only after the acknowledgement - or after 5 s when none came.

**The vehicle.** Development host A, TCG, `usb4-emu`'s fork through task
39-0.7's launcher (`-Usb4Profile tb3`, `Bios OFF_OFF`, `-DeviceAtBoot 0`,
slot 1: QMP 47922, VNC 47923), `-Trace` with `usb4_tp_*`, `usb4_dbg_*` and
`usb4_h_*`, and `usb4-debug-port` for `nvme98`'s stage markers.
`win2k-xonly` from a copy of the 39-A.5 overlay (`vm\usb4\nvlink-2k`,
`xhcicm.inf` and the filter already installed); the `qemu` flavour of
`xhcicm.sys` copied over the bound one and a restart. The receiver:
`nvme98`'s prototype of its decision MD-6, branch `phase0-g3g6` at
`f348a2d`, the `free-dbgport` x86 build from that worktree's `out\`
(read-only use, copied over `nvme98` 0.1.0.0's image; SHA-256
`9805cb87...85bf8`). Its stage codes: `0x95` attach (bit 16 table found,
bit 17 entry found, bits 31:24 the generation recorded), `0x96` the
statement seen, `0x92` the result. Load: `nvme98`'s G3 workload `gio`
(its `tests\gio`, built here with MSVC 6.0), unbuffered reads, queue depth
8, 64 KiB, on `\.\PhysicalDrive2` (drive 1 is the transfer disk). Times
below are the host trace's, from T0 = the tunnel gate closing.

| Leg | Driver build | What the trace and the log show |
|---|---|---|
| tunnel up | `f1c11201` (and `0c107673`) | `cm.nvlink.nt=80000000` (callback registered, no query yet); `cm.action.tunnel-up`, then `cm.nvlink.up=01000401` (new entry, bus 4, slot 1, generation 1) before the invalidation; `nvme98` `0x95 arg=01030000`: table found, entry found, generation 1 recorded at `HwFindAdapter` |
| unplug under load (`device_del enc0`) | `f1c11201` | `cm.nvlink.icm-down=00000402` (verdict SEND; the seam's poll read nothing, the channel's own SHPC reading DOWN), `cm.nvlink.down=00000001`, `cm.nvlink.await=00000064` (acknowledged after 100 ms), then `cm.nvlink.retired` and the invalidation. Trace: stage-2 cutoff T0+0.5 ms, `nvme98` gone T0+1.9 ms, `0x96` (generation 1 for recorded 1) T0+129 ms, `0x92` accepted; the statement's receipt follows the cutoff |
| the same, earlier build | `0c107673` | ack after 60 ms; `0x96` at T0+72.5 ms with 1 held request completed (`0x84`), memory released (`0x93`), `BusChangeDetected` (`0x8A`) |
| re-plug | `f1c11201` | the old entry retired (`cm.nvlink.retired`), `cm.nvlink.up=02000401` (same entry, generation 3); `nvme98` `0x95 arg=03030000` |
| idle unplug (no I/O) | `f1c11201` | statement made, no `0x96` (an idle `nvme98` reads the table only at its next request, G3 run C); `cm.nvlink.await=0000138F`: the wait ran to its 5 s bound, then the removal |
| `nvme98` absent (image renamed, restart, plug, unplug) | `0c107673` | `cm.nvlink.await=00000000`: no driver ever asked for the table, so no wait |

**Windows 98 SE** (`vm\usb4\nvlink-98`, a copy of the 39-A.4 overlay,
`XhciCmApprove` set to 1 in its driver key): the image with the three
callback-object imports loaded and bound (`cm.bind=0`,
`cm.nvlink.9x=00000000`), which is the import gate's authoritative check
for `ExCreateCallback`, `ExRegisterCallback` and `ExUnregisterCallback`
on 98 SE (their previous Windows 98 evidence was static: the name in `ntkern.vxd`).
The first record build put the record under `HKEY_DYN_DATA`; the VMM's
`_RegCreateKey` answered error 5 there (`cm.nvlink.9x.record=00000005`),
so the record moved to `HKLM\System\CurrentControlSet\Services\nvme98`,
value `LinkState`: `cm.nvlink.9x.record=80000000`, and `regedit /e` read
it back as `4e,39,38,4c, 01,00,00,00, a0,48,46,c1, 10,01,00,00, ee,3e,80,ce`
(signature, version 1, the table at C14648A0h, 110h bytes, the cookie).
At the tunnel up the scan of the tunnel's secondary bus found no function
answering through `HalGetBusData` within its 1 s (`cm.nvlink.up.waited=
00041400`), so no `UP` was published on 98 SE; why the endpoint does not
answer there before `Reenumerate_DevNode` is not read. No 9x statement is
made in any case (nothing reads the SHPC slot there, record 21 section
7.2), and `nvme98`'s 9x receiver does not exist (its MD-5: designed, not
prototyped), so the 9x half's runtime behaviour beyond the record is not
readable yet.

**Re-read after the CONFIGMG worker** (2026-10-15 evening; the `qemu`
build of `5a7b3387`, gated, SHA-256 prefix `CEE627E4`; QEMU pinned to
`vm\usb4\bin-5ba7f60\qemu-system-x86_64.exe`; both guests from the same
overlays). Windows 2000: `cm.nvlink.up=01000401` before `nvme98`'s attach
(`0x95 arg=01030000`); under `gio`, the stage-2 cutoff at T0+0.7 ms,
`nvme98`'s `0x96` at T0+270 ms, `cm.nvlink.await=0000005A` (90 ms), then
the retirement and the invalidation. Windows 98 SE: the record written
(`80000000`); the fresh exposure found the bus silent for its 1 s
(`up.waited=0003F800`), `Reenumerate_DevNode` (CR_SUCCESS), then the
rescan published the entry (`cm.nvlink.up=01000401`) 206 ms after the
exposure (`cm.nvlink.up.late=0000CE00`; 1016 ms in an earlier run of
`da0d1f0f`); at the unplug the statement withheld as designed
(`icm-down` verdict 9, an OS not admitted; the entry left `IN_USE`). The
worker's handshake (`REMOVE_PENDING`) was not exercised: the children the
worker removes at an unplug have vanished, and `nvme98` has no 9x
receiver yet.

**Not read here.** XP x64 (the amd64 build; the other admitted pair), XP
x86, Vista and 7 (not admitted: the statement is withheld there by
design); the `REMOVE_PENDING` handshake (no receiver, and no caller in
the ICM profile); a hardware tunnel port (no statement by design); the
`release` and `debug` flavours in a guest.

## 39-V.3 - The NT tiers: XP, XP x64, Vista x86, 7 x86

**Verdict: pass on XP, XP x64, Vista x86 and 7 x86; Vista x64 and 7 x64
not run** (2026-10-15). On each of the four, the package installed through
`INSTALL.BAT`, the tunnel-port filter from `nt\` included; a device router
plugged on `tb3-alpine-ridge` was approved and its NVMe appeared with no
Refresh and no "Scan for hardware changes", left on unplug and came back on
a re-plug; and the NVMe was listed in "Safely Remove Hardware", stopped
there, unplugged with no dialog, listed again after a re-plug, and a plain
unplug showed no dialog either. ME is not in this section (another run).

**Not run: Vista x64 and 7 x64.** Both load only a signed driver (or one
started from an F8 boot with enforcement off), and the x64 package is
test-signed only by the packager with the project key, whose password was
not available to this run; nothing was signed by hand. They stay owed,
with 7 x64's reading of whether `pci.sys`'s own hot-plug acts before the
filter. XP x64 does not check kernel-mode signatures, so its legs ran on
the unsigned `release-x64` package.

**The vehicle.** Development host A, TCG, task 39-0.7's launcher with
`-RunRoot v-xp7` (`-Usb4Profile tb3 -Bios OFF_OFF -DeviceAtBoot 0`, the
root bridge at slot 6, `-Trace`), each guest from a fresh overlay of its
`vm\<guest>.img`, two vCPUs, QMP and VNC on 47946/47947 (XP and XP x64:
47956/47957), 47966/47967 (Vista) and 47986/47987 (7). The NVMe behind the
tunnel was an overlay of `usb4-emu`'s `vms\win98\nvme32.qcow2` (one FAT32
volume, `NVME32`), read-only. The QEMU binary: XP and 7 x86 ran on the copy
in `vm\usb4\bin` (the `a50c19e` build); Vista x86 and XP x64 on the
`usb4-emu` `main` `5ba7f60` build (QEMU fork `20655c3`) staged in
`vm\usb4\bin-5ba7f60`. Driven over QMP keys and screendumps; device state
read with a throwaway SetupAPI tool (`devq`, task 39-0.4's, rebuilt 64-bit
for XP x64) whose output went to the guest's COM1, that is the serial log.

**The package.** Built on branch `v-xp7` at `6fa331d4` (the `3.0.0.0` tip
of the run): `build-driver.cmd all` and `all -amd64`, every gate passing;
`make-package.ps1 -Flavor release` for x86 and amd64 (amd64 unsigned), and
`XHCIINST.EXE` and `XHCIIN64.EXE` from `xhciinst\build.cmd` of the same
tree, staged with `INSTALL.BAT` in the release layout
(`out\v-xp7\xfer-x86\PKG`, `out\v-xp7\xfer-x64\PKG`; hashes in
`out\v-xp7\pkg-*-sha256.txt`). The binaries are not restamped, so Windows
shows them as 2.4.0.0. That build predates the new Device Manager names:
the NHI showed as "xHCI98 Thunderbolt 3 Host Interface" and the ports as
"xHCI98 Thunderbolt 3 Tunnel Port Filter"; the names of the later build
were not read here. The NVMe driver was `nvme98` 0.1.1.0 (its published x86
and amd64 builds, read-only), installed after the first plug with
`UpdateDriverForPlugAndPlayDevices` and `INSTALLFLAG_FORCE`.

**Install.** `INSTALL /defaults /yes /nopause` from the transfer drive (an
elevated prompt on Vista and 7). On every guest the tool staged
`xhci98.inf`, `xhciuas.inf`, `xhcicm.inf` and `nt\xhcicmf.inf`, installed
the Alpine Ridge xHCI `15C1` and its root hub, `xhcicm.sys` on the NHI
`15BF`, and the filter on the four `15C0` ports and the emulator's root
bridge `1B36:0001`, after Windows' unsigned-driver questions; the
`nt\` copy of `xhcicm.sys` installed on Vista and 7 with no `0xA1`. On XP
x64 the root hub's Found New Hardware wizard stopped the first run with
exit 4 as designed; answered, a second run finished. Every run ended
"Changes were made. Please restart Windows now.", exit 3. After the
restart every port read `Service` `pci`, `UpperFilters` `xhcicm`, started,
and the NHI `xhcicm`, started, problem 0.

| Leg | XP SP3 | XP x64 SP2 | Vista SP2 x86 | 7 SP1 x86 |
|---|---|---|---|---|
| Install, filter from `nt\` | pass | pass (two runs) | pass | pass |
| Plug: approved, NVMe with no Refresh | pass | pass | pass | pass |
| Volume `NVME32` mounted (`nvme98`) | pass | pass | pass | pass |
| Unplug: devnode and volume gone, no dialog | pass | pass | pass | pass |
| Re-plug: back, no wizard | pass | pass | pass | pass |
| Listed in "Safely Remove Hardware" | yes | yes | yes | yes |
| Stop: "Safe To Remove Hardware", device at Code 47 | pass | pass | pass (see below) | pass |
| Unplug after the stop: dialog | none | none | none | none |
| Re-plug after the eject: listed again | yes | yes | yes | yes |
| Plain unplug, no eject: dialog | none | none | none | none |
| Result | **PASS** | **PASS** | **PASS** | **PASS** |

What the emulator saw (`trace-*.log`), the same on each: per plug the ICM
event `device connected ... approved 0`, the approve command, `PCIe tunnel
up reason 0`, `tp_insert`; per unplug `tunnel down reason 3`,
`device disconnected`, `tp_removed` - three cycles on each guest.

**Readings.**

- **The dialog.** No Unsafe Removal dialog on XP, XP x64, Vista or 7 after
  a plain unplug of a device marked `Removable` alone: the dialog is a
  Windows 2000 matter (39-V.2), and the release-notes line of record 21
  section 7.4 stays 2000's.
- **Vista's veto.** The first Stop on Vista was refused - "Windows can't
  stop your 'Generic volume' device because it is in use" - while an
  Explorer window that AutoPlay had opened on F: was open; with it closed
  the Stop went through. The ordinary veto of section 7.4 step 3, nothing
  forced.
- **Code 47.** After the Stop the NVMe stayed reported (status
  `0x01806400`, problem 47) with the tunnel up until the unplug, as
  section 7.4 says for XP and later.

**Evidence.** `vm\v-xp7\xp-tb3\`, `xp64-tb3\`, `vx86-tb3\`, `w7-tb3\`:
`notes.txt` (the leg's narrative and the screenshot it cites), the
screendumps `s*.png`, the serial log (`G <tag>` device dumps and the
installer log), `trace-*.log` and `debugcon-*.log`.

### The 98 SE worker, the resume deadline and the hold cap (2026-10-15)

**Verdict: passes.** On Windows 98 SE a CONFIGMG call made while an Add New
Hardware wizard is open blocks until the wizard closes; made on the
connection-manager thread it left ring 0 unserviced (an unplug unseen until
the wizard was cancelled, found under the USB4 profile in run 40). Every
CONFIGMG call is now the 98 worker's (`cm_cfgwk.c`); the connection-manager
thread only posts requests (latest per member wins). The ICM client bounds
its own resume at 270 s, and the bridges' hold is a fixed 5-minute cap under
either profile (record 21 section 6.3). The 98 SE listing rule of NUSB's
"Unplug or Eject Hardware" was read (record 21 section 7.4).

| Leg | Guest | Reading |
|---|---|---|
| Wizard open (`reenum-adl`, the `u98-adl3` setup) | 98 SE, `usb4-generic`, `intel-adl`, `preos-cm=on`; the NVMe "PCI Card" wizard left open | unplug: `u4.linkdown=01000109`, the tunnel-down action, `cm.reenum.down.poll=00000404`; re-plug: `u4.router.up`, `u4.pcie.up=00010809`, `u4.usb3.up=00010A0B`, the tunnel-up action, `cm.reenum.up.poll=00000404` - all with the wizard still open; after Cancel the worker drained: `query-remove=0` (accepted), `remove=5` (the devnode already gone), the stale removal's own `reenumerate=2800`, the up request's `reenumerate=2800`, `removable=0`, and the wizard came back for the re-plugged device |
| tb3 cycle (`reenum-98`) | 98 SE, Alpine Ridge model, `nvme98` installed | plug: approved, `cm.reenum.up.poll=00000404`, `reenumerate=00020800`; unplug: `query-remove=0`, `remove=0`, `reenumerate=00020800`; re-plug: `reenumerate=00020800`, no wizard, no error |
| 2000 cycle (`reenum-2k`) | 2000 SP4, Alpine Ridge model, the filter and `nvme98` installed | plug: tunnel up, the NVMe found, the "Unplug or Eject Hardware" icon in the tray; unplug without a stop: the device removed, the Unsafe Removal dialog listing "NVM Express Controller (nvme98)" and its disk (as recorded above for 2000); re-plug: tunnel up, the icon back |

The first wizard-open run (before the fix to the removal step) showed the
worker's Query_Remove_SubTree blocking behind the wizard while a re-plug
made the request stale, after which the accepted query went without its
Remove_SubTree; 98 has no service to cancel an accepted query, so the step
now always completes it, and follows a stale one with its own rescan. Host
checks: `test_tbt_icm` (the 270 s deadline: clipping, expiry mid-drain, the
last attempt, a stopped and a partway-stopped clock, a frame flood during the
host poll) and `test_cm_ports` (the fixed watchdog, the known-children
pruning). The x86 `qemu` build passes every gate.

98 SE's applet (NUSB's `HOTPLUG.DLL`) lists a device only when a device
interface of NUSB's class is registered on its parent; our devices, marked
`DN_REMOVABLE`, are not listed (record 21 section 7.4). ME was not read.

## 39-A.8 follow-up - The device viewer and eject (`XHCIINST /devices`, `/eject`)

**Verdict: passes on Windows 98 SE and Windows 2000** (2026-10-15), design
record 24. On 98 SE the tool listed the tunnelled NVMe enclosure from the
driver's confirmed status record, ejected it while idle, refused while a
file was held open on its volume and left the volume working, ejected it
once the file was closed, and the device came back at the re-plug with its
data intact. On 2000 the same, through `CM_Request_Device_EjectA`, with no
Unsafe Removal dialog at the unplug; a device Windows does not see as
removable cannot be stopped there at all, and the tool says so.

**The vehicle.** Development host A, TCG, task 39-0.7's launcher with
`-Usb4Profile tb3 -Bios OFF_OFF`, run root `vm\eject9x\`, QMP and VNC
47970/47971 (98 SE) and 47972/47973 (2000). The emulator binary was
`vm\usb4\bin-5ba7f60\qemu-system-x86_64.exe` (`v11.1.2-161-g20655c3`)
for every leg, passed with `-Qemu`. The NVMe behind the tunnel was an
overlay of `usb4-emu`'s `vms\win98\nvme32.qcow2` (FAT32), opened read-only
as a backing file. Guests: 98 SE from a copy of the `vm\usb4\reenum-98`
overlay (`vm\t26\win98-gold.qcow2` below it; `xhcicm.sys` bound to the
NHI, `nvme98` on the NVMe), `xhcicm.sys` replaced by this branch's `qemu`
flavour; 2000 from a copy of `vm\usb4\reenum-2k` (`xhcicm.sys` and the
tunnel-port filter, `nvme98` 0.1.0.0), the same replacement. The tool was
this branch's `XHCIINST.EXE`, run from the transfer disk with its output
redirected to a file; on 98 SE the confirmation answered through
`echo Y|`.

| Leg | Windows 98 SE | Windows 2000 |
|---|---|---|
| D, viewer, nothing plugged | the controller, "firmware connection manager, security level 1", "confirmed now", "Nothing is connected", the verified port 02:01.0 found by its 98 path `08083000`, "(nothing behind it now)" | - |
| D, viewer, plugged | the router "QEMU USB4 NVMe Enclosure", Thunderbolt 3, link 1, depth 1, approved, Thunderbolt ids `46F4:0004`, PCIe up; unit [1] "NVM Express Controller (nvme98)" with its disk entry, "marked removable by the Thunderbolt driver", its instance id `...\0808083000` | the same router; the port by bus and address; unit [1] with "removable: yes; surprise removal OK: no (listed in Safely Remove Hardware)" |
| E1, eject idle | `CM_Query_Remove_SubTree` then `CM_Remove_SubTree` accepted, "Windows has stopped NVM Express Controller (nvme98).", exit 0; the unplug: `tbt.icm.device-gone`, `cm.action.tunnel-down`, the worker's re-enumeration, no fault | `CM_Request_Device_EjectA` (from `setupapi.dll`: `cfgmgr32.dll` does not export it, the log's routine lines) accepted, "You can now unplug the device.", exit 0; `xhcicm.sys` logged `cm.reenum.eject` and `cm.reenum.eject.tunnel-left-up` (the child filter's hand-off, as after the applet); the unplug: no Unsafe Removal dialog |
| E2, a file held open (`type con`/`copy con` to the NVMe volume) | "Windows refused: the device is in use (...)", exit 4; the held file closed, read back, a test file written, then the eject accepted | `CR_REMOVE_VETOED` with the volume named ("Windows refused: STORAGE\Volume\... did not allow it."), exit 4; the file closed, then the eject accepted |
| E3, re-plug after the eject and unplug | the device back at the plug, `nvme98` started, marked removable; `fc` of the test file against the original: no differences | (the same plug brought the device back, listed again) |
| E4, disabled and enabled in Device Manager | - | the unit's capabilities read `Removable` no; `CM_Request_Device_EjectA` answered `CR_REMOVE_VETOED`, veto 8 (`PNP_VetoIllegalDeviceRequest`); `CM_Query_And_Remove_SubTreeA` answered `CR_CALL_NOT_IMPLEMENTED` from `cfgmgr32.dll` and from `setupapi.dll`; the tool's step text, exit 4, the device still started. After a restart with the device connected its capabilities read `Removable` again |

**Readings taken by the legs, and changed in the code the same day:** the
viewer showed "no driver" for an IOS miniport on 98 SE (it now shows the
driver key's `PortDriver`, or `DevLoader` for a disk); the record said
"USB3 not known" for Alpine Ridge on 98 SE, because the AddDevice profile
it was taken from is not read on 98 (it now takes the running ICM row's
generation: the 2000 leg read "USB3 none"); the 2000 tree's root devnode
answers no property of its own and was counted as an unread entry (it no
longer is); on 2000 the query-and-remove routine is not implemented, so
the not-removable case gets a step rather than an error.

**Not read here.** ME (no ME image with `xhcicm.sys` installed on this
host); XP, Vista and 7, and so the not-removable case there; the USB4
profile (`usb4-generic`); a dock with several units; a removal that fails
after an accepted query; leg E5 (a unit unplugged between the confirmation
and the call), which the host suite covers with injected answers; the
`release` and `debug` flavours.

## 39-V.2 - Windows 2000 on the Alpine Ridge tree, and the Titan Ridge leg

**Verdict: the table's legs pass on Windows 2000 except the R32 re-plug,
which fails by platform; the R32 silence, the eject and the boot question
pass; the SMP leg under Driver Verifier found no violation in `xhcicm.sys`
(its first run froze in `nvme98`, the re-run with the NVMe disabled
passed)** (2026-10-15). Suspend and resume (S3, the resume
ordering of record 21 section 6.3, the watchdog) are not read here: they
wait for the client's resume budget and another worker reads them.

**The vehicle.** Development host A, TCG, task 39-0.7's launcher with
`-RunRoot v-2k`, `-BootStrict` and `-trace usb4_*` (with `-Timestamps`
from the Titan Ridge leg on), QMP and VNC on 47926/47927 (the xHCI-only
2000 image, one vCPU), 47928/47929 (a second one) and 47932/47933 (the SMP
image, two vCPUs). The emulator: the copy in `vm\usb4\bin` (QEMU 11.1.2
`v11.1.2-149-gc645f13`, SHA-256 `9A55E985...DCB3`, also pinned as
`vm\v-2k-bin\`) for the Alpine Ridge and R32 legs, and
`vm\usb4\bin-5ba7f60` (`v11.1.2-161-g20655c3`, `usb4-emu` main
`5ba7f60`) for the Titan Ridge and SMP legs. The `qemu` flavour of
`xhcicm.sys` from `3.0.0.0` at `6fa331d4` (SHA-256 `757E2A1D...0E60E`),
packaged by `make-package.ps1 -Flavor qemu`, with `XhciLogVerbosity` 2 in a
staged copy of `xhcicm.inf`; `nvme98` 0.1.1.0 (its published x86 build,
read-only); an NVMe overlay of `usb4-emu`'s `vms\win98\nvme32.qcow2` (a
FAT32 volume, `NVME32`). Installs by `devq` (`out\v-2k\devq\`, the 39-0.4
tool with `disable`, `enable`, `cycle`, `setval` and `getval` added): the
NHI by its hardware id `PCI\VEN_8086&DEV_15BF&CC_0880` (on 2000
`PCI\VEN_8086&DEV_15BF` is a compatible id, which
`UpdateDriverForPlugAndPlayDevices` does not match), the filter by
`nt\xhcicmf.inf` and `PCI\VEN_8086&DEV_15C0&CC_0604`, unforced. Evidence:
`vm\v-2k\<run>\` (debug-console log, trace, screenshots `s*.png`).

| Leg | Run | Result |
|---|---|---|
| Install (OFF_OFF, device at boot) | `ar-a` | The switch's bridges silently, restart; the NHI by `devq` (`reboot=0`): bind, driver ready, the dock announced unapproved (`0x0811`), get route, approve, tunnel up. The filter through its INF: `reboot=0`; the NHI's stack restarted twice while the ports' stacks were rebuilt, the NVMe then found ("PCI Device") and `nvme98` installed; volume `D:` (`NVME32`) mounted. PASS |
| The boot question (39-0.4) | `ar-a` | After the filter's install `Services\xhcicm` reads `Start` 0 and `Group` "Base", although both INFs say demand start; two restarts later: no `0x7B`, the filter attached at boot on every `15C0` port, the NHI bound, the device approved and re-enumerated, the eject icon in the tray. The promotion is Windows 2000's; nothing in the filter's role needs more than a boot start offers. PASS |
| V.1.2 unplug and re-plug | `ar-a` | `device_del`: disconnect, tunnel down, devnode gone; `device_add`: announced, approved, `D:` back. PASS |
| V.1.3 boot-tunnelled (OFF_ON) | `ar-pb` | NVMe inserted before POST (`cold=1`); the driver bound with the device announced approved with boot (`0x0D11`), `tunnel-up` why 2, no approve and no mailbox `0x06` in the trace; `D:` from boot; not offered for eject until re-plugged, then offered. PASS |
| V.1.4 nothing at boot, controller present (OFF_OFF) | `ar-nb` | Bind with an empty table (`cm.dev.count` 0); `device_add`: approved, `D:` mounted (Explorer opened it) with no Refresh. PASS. OFF_ON with nothing attached was not run separately (pre-boot changes nothing without a device) |
| V.1.5 Assist, no device (ON_OFF) | `ar-on0` | The controller's tree absent: the root bridge's secondary bus empty, the switch's upstream port at bus 0 with no windows, no NHI, nothing bound, nothing logged - the release notes' limitation. PASS. ON_ON with nothing attached is the same power model and was not run |
| V.1.6 disable and enable | `ar-a` | Disable: rings disabled, then mailbox `0x07`, `D:` stays; enable: `0x23`, driver ready, the device announced approved (`0x0911`), `tunnel-up` why 2, no approve. PASS |
| V.1.7 `XhciCmApprove` 0 | `ar-a` | Announced `0x0811`, `tbt.icm.approve-withheld`, no `0x04` in the trace, no tunnel. PASS |
| V.1.7 `XhciCmMode` 0 | `ar-a` | `cm.inert` 1: no ring, no mailbox, nothing in the trace across an unplug and a plug. PASS |
| V.1.8 the dock's xHCI | - | Not available on i440FX (39-0.7); bench |
| R32: Assist on, silence after the last unplug (ON_ON, device at boot) | `ar-r32` | Bound with the boot device approved. `device_del`: disconnect, tunnel down, "ICM silent 1"; the client sent no command in the window, no retry, no hang. 35 s later "controller power 0": the root bridge's gate closed, the NHI's registers all ones; the emulator does not remove the NHI devnode. `device_add`: power 1, the ICM reset ("driver not ready"), and the controller's subtree came back with its configuration reset (the switch's upstream port at secondary bus 0, no windows), so the NHI stays unreachable. The client never noticed: re-plug not approved. A disable and enable of the NHI left it absent; a root rescan removed the switch's bridges (Unsafe Removal dialog naming the tunnel port filter) and a second did not re-add them. **Silence survived: PASS. Re-plug: FAIL by platform** - the emulator resets the subtree without a PnP removal and 2000's `pci.sys` (`acpi=off`) cannot renumber it; the P14s removes the controller instead. The driver's part - detecting the loss, retiring the devices, refusing access, recovering where the platform restores access - is the liveness monitor of record 21 sections 5.5 and 5.6, built on branch `v-2k` after this reading; its re-reading is the next row |
| R32 re-read with the liveness monitor | `ar-r32b` | The emulator `vm\usb4\bin-5ba7f60`; `xhcicm.sys` (`qemu` flavour) from `v-2k` at `03e19f3e`, SHA-256 `CA543911...AE8E6`, copied over the installed one. Bind: `cm.live.baseline=0100000A` (RX ring 0's size register). `device_del`: "ICM silent 1", tunnel down. 35 s later the emulator gated the subtree to all ones: `cm.live.state` SUSPECT then ABSENT (`00000102`, `00000103`), `tbt.icm.lost` (no entry left), a RESYNC of why 9 (UNAVAILABLE), `cm.reenum.unavailable` 1; from then the trace shows no access reaching the host interface. `device_add`: power 1, the subtree's configuration reset: CONFIG_LOST (`00000104`), the validation's identity read through the reset bridges `FFFFFFFF` (`cm.live.ids-changed`), RECOVERY_FAILED (`00000106`) - no recovery attempted on a reset subtree, as record 21 section 5.6 says. A disable of the NHI then: `cm.unbind.no-access`, no ring stop, no mailbox `0x07`, `cm.buffer-kept` 1 (no verified stop, so the common buffer stays), `cm.live.final=00000106`; the enable left the NHI absent (as in the first reading). No hang, no bugcheck. **Detection and containment: PASS. Re-plug: FAIL by platform** (owed post-cut on an emulator whose slot removes the subtree) |
| Plug cycle and safe eject on the monitored build | `ar-c` | OFF_OFF, device at boot, the same binary: unplug (the Unsafe Removal dialog, no eject asked), re-plug approved, tunnel up; then "Unplug or Eject Hardware", Stop: `cm.reenum.eject=00000208`, `eject.tunnel-left-up`, "Safe To Remove Hardware"; `device_del`: no dialog; `device_add`: listed again. No `cm.live.state` transition across any of it. PASS |
| Safe eject (`Removable` alone) | `ar-a` | Listed ("NVM Express Controller (nvme98) at PCI Slot 5"); Stop: the volume and the disk listed, "Safe To Remove Hardware", `cm.reenum.eject=00000208`, `eject.tunnel-left-up`; the held devnode at problem 24; `device_del`: no dialog, devnode gone; `device_add`: listed again. A plain `device_del`: the Unsafe Removal dialog naming the controller and its disk. PASS |
| `XhciCmSafeEject` 0 | `ar-a` | Device present (`D:`), nothing listed. PASS |
| `Removable` + `SurpriseRemovalOK` (variant build, `xfer\sro\`, SHA-256 `AA49ABFE...5A45`, not committed) | `ar-sro` | 2000 no longer lists the NVMe in "Unplug or Eject Hardware", and a plain unplug still shows the Unsafe Removal dialog, naming only the disk. Decides 39-A.8's 2000 path: `Removable` alone; the dialog stays a release-notes line |
| Eject listing after a stack rebuild | `tr-a` | A disable and enable of the NVMe devnode, or its driver installed by Found New Hardware, drops it from the list until a re-plug (record 21 section 7.4: the child filter is created only at the bridge's first report). Release-notes line widened, fix owed |
| SMP under Driver Verifier: filter disable concurrent with tunnel work | `smp-a` | The SMP image, `verifier /all` (level `0x1B`, every driver, `xhcicm.sys` included), filter and NHI installed as above. `devq cycle` disabling and enabling the tunnel port 02:01.0 (its filter instance's rundown) while the host ran four unplug and plug cycles: every tunnel up and down logged in order, no Verifier stop, no bugcheck; the guest then froze with CPU 1 for minutes in `KeStallExecutionProcessor` called from `nvme98.sys` under `scsiport`, CPU 0 idle - `nvme98` polling a controller that reads all ones (reported to its owners; not this driver's). Re-run with the NVMe devnode disabled: the next row |
| SMP under Driver Verifier, re-run | `smp-b` | A fresh overlay of the SMP image, `verifier /flags 27 /driver xhcicm.sys` (level `0x1B` read back), the emulator `vm\usb4\bin-5ba7f60`, OFF_OFF with the device at boot, the NVMe devnode disabled (`nvme98` 0.1.1.0 is not used on SMP with bridge cycling). `devq cycle` disabling and enabling the tunnel port 02:01.0 eight times, 3 s apart, while the host ran five `device_del`/`device_add` cycles: every cycle "OK", five tunnel-ups and five tunnel-downs logged in order (`cm.reenum.ups` 6 with the boot one, `downs` 5, `ejects` 3, `child.attached` 3); then a disable and enable of the NHI itself: no Verifier stop, no bugcheck, the guest responsive throughout. PASS |

**The Titan Ridge leg** (`tr-a`, `-Tb3Model titan-ridge`, OFF_OFF, nothing at
power-on, from the prepared image whose driver store already held both
INFs). The NHI `8086:15EB` was offered "xHCI98 Thunderbolt 3 Host Interface"
by Found New Hardware and installed by `devq` (`&CC_0880`); all five
`8086:15EA` ports took the filter from the stored INF at their first
discovery, with no install step (`upper: [xhcicm]`, started). Bind:
`cm.hw.profile` 3, `cm.icm.row` 3, driver-ready flags `0xA2`, two members
(02:01.0 and 02:04.0, the second tunnel port). Plug: announced unapproved,
**no get route**, approve (8 words, route-addressed) 264 ms after the
announcement (re-plug 243 ms), tunnel up, the NVMe found with no Refresh,
`nvme98` installed, `D:` mounted. Unplug and re-plug: PASS. Safe eject:
listed, Stop, `cm.reenum.eject=00000208`, `device_del` with no dialog.
`XhciCmMode` 3 on the Titan Ridge NHI: refused, inert (`cm.inert` 4).
PASS. For comparison the Alpine Ridge row on the SMP guest: 4 ms (at
driver ready), 19 ms and 86 ms from the announcement to the approve, get
route included.

**Not read here.** S3 and the resume ordering; ON_ON and OFF_ON with
nothing attached (same power model as their read twins); the dock's xHCI;
the monitor's CONTROLLER_UNAVAILABLE route on 98 SE and ME (record 21
section 5.5; built and reviewed on `v-2k`, its reading owed with the R32
re-plug); `nvme98`'s presence-lost statement (its link-state channel has
none in this build; owed);
the 98 SE, ME and NT 5.1/6.x tiers (39-V.1, 39-V.3); the `release` and
`debug` flavours.
