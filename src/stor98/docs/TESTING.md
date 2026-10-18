# Testing with usb4-emu

[usb4-emu](https://github.com/yeokm1/usb4-emu) is a QEMU fork with an emulated USB4 /
Thunderbolt host. Its tunnel bridges put QEMU's `nvme` device behind a PCIe bridge whose
link can be raised, torn down cleanly or surprise-removed, which is the situation Stor98
must survive. Its `docs/XHCI98-HANDOFF.md` describes the test VMs (Windows 98 SE on
i440FX; Windows 2000, XP x64 and Windows 7 x64 setups) and the launch scripts.

Each case below cites the [PLAN.md](PLAN.md) item or feasibility gate (G1-G9, PLAN 0.0) it
validates. Thresholds are initial targets; if a measurement shows one is wrong for a given
OS, change it here with the evidence rather than relaxing it silently.

## Local test VMs (`vms\`)

`vms\` in the repository is a git-ignored directory junction to the maintainer's local VM
folder (create it with `mklink /J vms <your folder>`). The folder is self-contained: nothing in it reads
the usb4-emu repository or its VM images.

| Path | Contents |
|---|---|
| `base\` | Flattened, standalone base images: `win98-prep`, `winme-prep`, `nvme-g9-base` (the FAT32 test volume), `win2k-xonly`, `win2k-nvme8g-base`, `w7-g18` (7 x64, test-signing on), and the overlays on them in `base\w2k\` and `base\w7\`. Never booted directly; every run uses an overlay. |
| `bin\` | The usb4-emu QEMU build (`qemu-system-x86_64.exe`, `qemu-img.exe`) with the MSYS2 UCRT64 DLLs it needs, so it runs with only the Windows folders on PATH. |
| `share\` | QEMU firmware (`-L`), copied from usb4-emu's pinned baseline. |
| `tools\` | `qmp.py` (QMP client) and `reenum-test\`, copied from usb4-emu (GPL-2.0-only test tooling; never copied into this repository). |
| `int\`, `nt\`, `fix\`, `perf\` | Harnesses: 9x requalification (98 SE, Me, 2000), NT smoke (7 x64), the 0.3.0.1 fix runs, and the 9x request-pattern measurements (`perf\perf-hooks-3e9f51b.patch`). Each has its own run scripts and logs. |

The emulator itself is usb4-emu ([github.com/yeokm1/usb4-emu](https://github.com/yeokm1/usb4-emu));
to move to a newer emulator build, replace `bin\` (and `share\` if it changed) and rerun
the smoke tests. 9x guests are never warm-restarted: shut down, quit QEMU, cold start.

## Device lines (i440FX, legacy guests)

```
-global PIIX4_PM.acpi-pci-hotplug-with-bridge-support=off
-device usb4-tunnel-bridge,id=tb0,bus=pci.0,addr=0x5,chassis_nr=1,shpc-autoenable=on,msi=off
-device usb4-host,id=u4,bus=pci.0,addr=0x4,tunnel-port=tb0,msi=off,msix=off
-drive if=none,id=nvme0,file=nvme.qcow2,format=qcow2
-device usb4-device,id=enc0,bus=u4.0,port=1,drive=nvme0,serial=USB4EMU01
-trace 'pci_nvme_*' -trace 'usb4_*' -D trace.log
```

On q35 (Windows 7 and later), `usb4-tunnel-port` gives native PCIe hot-plug.

## Controlling the tunnel (QMP)

```
{"execute":"qom-set","arguments":{"path":"/machine/peripheral/tb0","property":"tunnel","value":"up"}}
```

Values: `up`, `down` (surprise removal by default), `eject`, `surprise`. Unplugging the
device router: `device_del` of `enc0`; plugging back: `device_add` with the same
arguments. Without a connection manager in the guest, the tunnel must be forced up this
way; Windows 98 then needs a Device Manager refresh to find the controller.

QMP and model-hook tunnel gating are test stimuli only; the driver never relies on them as
evidence (PLAN 0.0).

## Windows 7 x64 notes (Q-NT)

- **Test signing on 7 SP1 needs SHA-1.** With test signing on, a test build signed with a
  certificate whose own signature is SHA-256 gives Code 52: Windows 7 SP1 code integrity
  does not take it. Use a SHA-1 certificate signature and a SHA-1 file digest (no
  timestamp), as the project test certificate does (BUILD.md, "Test signing"). Never import
  a certificate into a store on the host.
- **Hibernate under WHPX does not resume** (black screen, kernel running, no disk I/O).
  Run hibernate and resume cases under TCG; WHPX is fine for the other cases.
- **DriverStore reinstall.** On the first WHPX boot of an image PnP reinstalls the driver
  from the DriverStore and asks for a restart; after that the copy in
  `system32\drivers` is replaced from the DriverStore at every boot. To run a test build,
  copy it and load it by disable/enable, or replace the DriverStore copy too
  (takeown/icacls in the guest overlay) for runs across a reboot or hibernate.
- **Resume password.** 7 x64 (and XP x64) lock the session at resume. Turn it off in the
  test overlay (`powercfg ... CONSOLELOCK 0` on 7; `powercfg /globalpowerflag off
  /option:resumepassword` on XP x64) rather than typing a password.
- **Faults at resume** are set by QMP after POST: with Identify failing from power-on,
  SeaBIOS no longer lists the NVMe and Windows 7 discards the hibernation file and
  cold-boots. On XP x64 the fault must be set before the resume itself touches the disk.
- On q35 (`usb4-tunnel-port`, native PCIe hot-plug) a `tunnel surprise` makes the OS remove
  the controller at once; to hold SRBs in `gone` without a hot-plug event, close the gate
  only (`act=gate`).

## Boot from the tunnelled NVMe (dump scenario C, G6)

The NT dump stack only writes to the boot/system disk, so dump-mode tests need the system
on the NVMe (PLAN 4, scenarios B and C). In usb4-emu (G6, 2000 and XP x64):

- Give the guest's system disk image (an overlay) to the NVMe as its backing image, with no
  IDE disk attached, and set `x-force-tunnel=on` on the bridge so the endpoint is present at
  POST. SeaBIOS's NVMe driver hooks it into INT 13h (and leaves CC.EN=1).
- Install Stor98 as a boot-start driver: `Start=0`, group `SCSI miniport`, and
  `CriticalDeviceDatabase` entries for `pci#ven_1b36&dev_0010`. **The service name must
  equal the image base name** (`stor98` / `stor98.sys`): on XP x64 a mismatch makes the
  crash dump driver fail to load (events 45, 49) and hibernation fail with 0xC0000034.
- Guest settings: `CrashDumpEnabled=2` (kernel dump), `AutoReboot=0`,
  `i8042prt\Parameters\CrashOnCtrlScroll=1`, hibernation enabled.
- Crash: QMP key events (right Ctrl held, Scroll Lock twice), STOP 0xE2; `MEMORY.DMP` is
  checked at the next boot. Hibernate: the Shut Down dialog (2000) or `rundll32
  powrprof.dll,SetSuspendState` (XP x64); resume with the same QEMU command line.
- Stimuli: `x-rdy-set-delay-ms=2000` set just before the crash or hibernate (it acts on the
  dump instance's enable); `x-faults` holding or dropping a write completion after the
  dump started.
- The dump instance is recognised by `DriverEntry(NULL, NULL)` and `ArgumentString`
  `"dump=1;"`. Windows 7 x64 (crashdmp, test-signed boot driver) is not set up yet.

## Release package install check

Run whenever the INFs or the release layout change. Stage an unsigned test package with
`scripts\make-release.ps1 -Stage -Test` (releases/README.md) and install from it with the
OS's own tools, no copying by hand:

- **98 SE, ME**: Device Manager, Update Driver (or the Found New Hardware wizard), "specify
  a location", the stage's `x86\` folder. QEMU's plain `nvme` device works; on 9x hot-add
  it once the desktop is up (behind an SHPC `pci-bridge`: `device_add nvme,bus=<bridge>,addr=1`,
  then Device Manager Refresh), because SeaBIOS hooks a boot-present NVMe into INT 13h and
  9x then hangs at the logo.
- **2000, XP, Vista, 7 (32-bit)**: Device Manager, Update / Reinstall Driver, the `x86\`
  folder. **XP x64, Vista x64, 7 x64**: the same with `x64\`; Vista x64 and 7 x64 need
  test-signing on and a signed image (Code 52 otherwise).
- Pass: the controller shows "Stor98 Storage Controller for NVMe SSDs" working, its test volume is
  readable, and each OS has only its own image: `dir C:\stor98.* /s` finds only
  `SYSTEM\IOSUBSYS\STOR98.MPD` on 9x (and the class key's `PortDriver` is `stor98.mpd`),
  only `system32\drivers\stor98.sys` on NT (service `stor98`, demand-start, group `SCSI
  Miniport`). Negative: pointing a 64-bit system at `x86\` finds no driver.

Last run 2026-10-15, on the pre-release layout that 0.3.0.0 kept (unpublished builds 0.1.1.0
and 0.2.0.0, before the rename; the INFs of commit 1444520, driver built from
f88be79): passed on 98 SE (QuickInstall image, Update Driver over its nvme9x, and again
after a restart), ME (stock image, wizard), 2000 SP4 (wizard) and XP x64 (wizard, with the
unpublished 0.1.1.0 amd64 image under the new x64 INF). 7 x64: on an image with test-signing
off it found no driver in `x86\` and gave Code 52 from `x64\`, as expected; on an overlay
of the G1/G8 image with test-signing on, Update Driver, Have Disk `x64\` (same x64 INF and
signed 0.1.1.0 image, SHA-256 `de674964...83ea2`) installed it as a new package (`oem3.inf`,
the x64 INF) over the older Stor98 package already in that image; the service ran
demand-start and F: was readable. XP x86, Vista and 7 x86 had no test image.

Note, not investigated: on the 98 SE image the driver date shown (and the class key's
`DriverDate`) was the INF file's date, 10-15-2026, not `DriverVer` 10/14/2026; ME showed
the `DriverVer` date.

## Harness work needed

Several cases cannot be run reliably with the QMP commands above alone. These are work
items for the test harness (and, where marked, usb4-emu; changes there are made in that
repository, not here). Exact-point fault injection is synchronous in the device model
(R3-12): QMP round trips are too slow and too loosely ordered to place a fault between two
driver steps.

| Need | Approach | For |
|---|---|---|
| Fault at an exact NVMe command | usb4-emu model hooks, synchronous in the device model: close the tunnel gate, or delay or drop a completion, when the Nth NVMe command matching an opcode and LBA range is fetched from the submission queue, or just before its completion is posted | PLAN 0.10, 4 KiB tests, removal acceptance |
| Fault at an exact driver step | test-only usb4-emu debug I/O port; the debug miniport build writes a stage code to it just before `RequestComplete` (and at the RMW stages: after the pre-read, before the write-back). A hook armed on a stage code fires synchronously on that write | PLAN 0.2, 0.10 parent cancellation, 4 KiB tests, removal acceptance |
| Trace-following removal (stress only) | QMP client follows `pci_nvme_*` trace events for a chosen LBA/opcode, then issues `stop`, the tunnel `qom-set` and `cont`. Makes no claim about exact placement; used for randomised stress runs only | removal acceptance (stress) |
| Delayed or never-clearing CSTS.RDY (at enable and at disable), identify failure, failed disable | debug properties on the NVMe device model (usb4-emu) | PLAN 0.10, G1, G4, G5 |
| Hung device behind a live tunnel | debug property that makes the controller's MMIO stop responding (stuck or all-ones CSTS) while its config space still answers with valid IDs | PLAN 0.10 presence loss, unresponsive row |
| Presence lost with the DMA path open (negative test, R4-1) | usb4-emu hook: MMIO and config reads to the function return all-ones and writes are dropped, but the gate's DMA alias stays enabled and the endpoint is not reset, so the controller keeps executing commands it already fetched and can still write guest memory; a command the harness delays completes its data transfer after the reads went all-ones | PLAN 0.10 presence loss, MD-1 |
| Reopen without endpoint reset (R4-3) | usb4-emu hook: close the gate and reopen it without the stage-2 endpoint reset or a fresh endpoint, preserving controller state (CC.EN=1, queues) and one delayed command whose data transfer the harness releases at a chosen point | PLAN 0.10 reopen invariant |
| Abort outcomes (R4-4) | NVMe model hooks: complete an Abort with Dword 0 bit 0 = 1 ("not aborted", which QEMU's model is believed always to report; confirm) or 0; order the target's own completion before or after the Abort's; delay chosen children of a multi-child parent | PLAN 0.10 abort |
| SRB-level test client (R4-5) | two routes, because ordinary `IOCTL_SCSI_PASS_THROUGH` lets the caller set only the CDB, data and sense length, and returns only `ScsiStatus` and the sense bytes: it cannot set `SRB_FLAGS_DISABLE_AUTOSENSE` or observe `SrbStatus` and `SRB_STATUS_AUTOSENSE_VALID`. (1) A **unit harness** built as a host program that links the request-handling and sense code with a fake port (stubs for the `ScsiPort*` calls it uses, recording every call), compiled once against the NT and once against the 9x `SRB.H`: it builds SRBs with arbitrary flags and buffers and checks every completion field exactly. It is the route for the 9x sense cases. (2) An NT **kernel test client**: a test-only driver that builds SRBs itself and sends them as `IRP_MJ_SCSI` to the disk's port PDO, then records `SrbStatus`, `ScsiStatus`, the sense buffer with guard bytes and `SenseInfoBufferLength`; it shows what crosses the real port, including any sense-buffer substitution by SCSIport. Pass-through is kept for what it can express (sense length 0, 14, 18 and the returned sense bytes and `ScsiStatus`). An IOS-level 9x client is a later option if G9 adds a helper VxD | PLAN sense rules |
| CM notification | xhci98 CM prototype, or a guest test tool standing in for it, that publishes the link-state table ([`include/stor98cm.h`](../include/stor98cm.h), MD-6), sets `DOWN_QUIESCED` after tearing the tunnel down and waits for Stor98's acknowledgement before the OS removal path. NT: `tests/cmstub` on `phase0-g3g6` (polls the tunnel bridge's SHPC slot status; "slot empty" stands in for a completed teardown). 9x: `tests/cm9x` on `qual-9x` (`cm9x.sys`, a WDM driver on the usb4-host devnode: per-boot record in an `HKEY_DYN_DATA` dynamic key, the same SHPC slot polling through CONFIGMG's PCI enumerator, the MD-5 `REMOVE_PENDING` handshake and CONFIGMG removal; `cm9xctl.exe` requests ejects; [gates/Q-9x.md](gates/Q-9x.md)). Ordering checked from the host trace (`usb4_tp_cutoff` < CM markers < Stor98 receipt) | G3 |
| Lost interrupts | usb4-emu hook that suppresses selected INTx assertions | PLAN 2.3 |
| INTx model caveat (QEMU NVMe, found with `x-hang`): **an emulator bug**, tracked separately in usb4-emu | QEMU's controller keeps a count of pending completion queues for INTx: a CQ head doorbell write that finds the queue empty decrements it even when that queue was not pending (it can go negative), and a controller reset does not clear the pending interrupt status. A doorbell dropped by `x-hang` while a queue was pending therefore leaves the line asserted after recovery (an interrupt storm with empty queues). The driver does not work around it with doorbell writes: the earlier recovery that rewrote the current CQ head after a run of empty interrupts was removed, because NVMe Base 1.4c Figure 147 allows a controller to report a write of the previous doorbell value as an Invalid Doorbell Write Value error, and on a shared line an empty interrupt does not show which device is asserting. A storm (1000 empty interrupts within 1 s) is answered by masking the vector (INTMS) and polling from the timer; if it goes on masked the engine stays on polling and doubles the polling hold (up to 60 s), and never disables or resets the controller for it, because another device on a shared line keeps interrupting whatever Stor98 masks (lifecycle.c, EngInterrupt; stage code 0xDB, actions 1, 3 and 4). In QEMU's model masking drops the line, so the VMs continue on polling after such a test; the trace shows the 0xDB markers. To be fixed in usb4-emu's QEMU tree (decrement only for a pending queue; clear the status at controller reset) | the recovery tests after a dropped doorbell (`x-hang`), the interrupt-storm tests (`stormtest=1`, `stormints=`), and why no doorbell is rewritten |
| Media error on a chosen block | QEMU `blkdebug` driver under the image, `inject-error` on the sector range | parent error mapping |
| DMA bounds | debug build logs each SRB's caller-buffer and bounce physical ranges to the serial port; a host script checks every PRP-mapped range in the trace lies inside one of them; guest test tool surrounds buffers with sentinel pages and verifies them | PLAN 0.4, 4 KiB tests |
| Timestamps | QEMU `isa-debugcon` port; the debug build writes TSC-stamped log lines to it and rings a marker doorbell write that appears in the host trace (see Measurement definitions) | removal acceptance |
| Namespace layouts | `usb4-device` support for `nvme-ns` children (active NSID 2 only; several namespaces) | PLAN 1.9 |
| Identity change on reconnect | a second image fixture whose controller serial, NGUID and EUI64 all differ from the first | PLAN 2.6 |
| Capacity over 2 TiB | sparse image, e.g. `qemu-img create -f qcow2 big.qcow2 2560G` (and a 4 KiB-block variant) | PLAN 4 KiB reporting |
| Polling-only operation | the polling mode (PLAN "Polling mode (0.4.0.0)"): `StorPollMode` (9x) or `pollmode=` (NT) = 1 forces it on any OS (9x: REG_SZ `StorPollMode` in `HKLM\System\CurrentControlSet\Services\stor98`; NT: `pollmode=1` in DriverParameter), the same path as a controller with no usable interrupt (the port gets 0/0, INTx stays disabled). QEMU's nvme always has Interrupt Pin 1, so the natural cases (Pin 0, no resource, no line) are host-tested only (`tests/unit/poll_ut.c`) | PLAN 2.3, polling mode |

### Debug-port stage codes (phase 0.0 prototypes)

The gate prototypes write stage codes to the usb4-emu debug port (`usb4-debug-port`, I/O
0x5a0-0x5af: argument to +4, stage code to +0) and text lines to `isa-debugcon` at 0xe9
(`n98 <TSC> <code> <arg>`). Without the debug-port signature nothing is written. "Marker"
codes also write the TSC to +8/+C (`usb4_dbg_marker` in the host trace). The tables live
only on the prototype branches ([gates/](gates/) records name the commits).

**Unified NT table** (`src/dbgport.h` on `qual-nt`, which merged G1/G8, G2/G4 and G5/G7;
decoder: usb4-emu `vms/stor98-q1/q1log.py`, local). Codes are never reused:

| Range | Subsystem | Codes |
|---|---|---|
| 0x01-0x7F | shared layer | 0x01-0x05 load, find and options (0x05 the controller registers' BAR); 0x10-0x15 StartIo and bring-up holds (0x13 BUSY while starting, 0x15 hybrid hold to BUSY); 0x21 just before `RequestComplete` (0x22, 0x23 reserved for the RMW stages); 0x30-0x33 `HwInitialize`; 0x40-0x48 state and bring-up (0x44 bring-up failure, 0x45 disable acknowledged, 0x47 first bring-up timer call, 0x48 namespace format refused); 0x50-0x53 resets and abort; 0x60-0x69 shutdown, flush and stop path; 0x7F clock marker |
| 0x80-0x8E | G2/G4: gone state, MD-1 deadline, failed reset | 0x81 gone entered, 0x83 deadline, 0x84 held SRB completed, 0x85 port-retired, 0x86 per-second summary, 0x87 reset result, 0x88 late reset acknowledgement, 0x8A `BusChangeDetected` |
| 0x90-0x99 | G5/G7: memory and address probe | renumbered from 0x80-0x89 on `phase0-g5g7`; 0x91 find (count, flags), 0x92-0x94 address kind and value, 0x95 uncached-extension check, 0x96 stop done, 0x98 contiguous-pool check |
| 0xA0-0xB4 | qualification layer (`q1.c`) | 0xA0 options; 0xA1/0xA2 dirty and clean; 0xA3-0xA8 restartable SHUTDOWN (flush, CC.SHN, SHST, timeout, re-enable, repeat); 0xA9-0xB3 measuring stop path (Flush, SHST and RDY=0, each with a within-budget record); 0xB4 BUSY while starting (count) |
| 0xE0-0xF9 | 9x qualification layer (`src/q9x.h` on `qual-9x`) | 0xE0 options; 0xE1 rendezvous; 0xE2 pool; 0xE3 bring-up step; 0xE4 ready; 0xE5 hold; 0xE6 BUSY; 0xE7 gone; 0xE8 MD-1 deadline; 0xE9 `DOWN_QUIESCED` seen; 0xEA link-down acknowledgement (`AckGeneration`); 0xEB `REMOVE_PENDING` seen; 0xEC handshake step; 0xED handshake acknowledgement; 0xEE, 0xEF withdrawn (v1 cancel, INVALIDATED); 0xF0 `HwResetBus`; 0xF1 late reset acknowledgement; 0xF2 failed; 0xF3 stop; 0xF4 write-through; 0xF5 SHUTDOWN SRB step; 0xF6 pool reuse check; 0xF7 tick summary; 0xF8 entry snapshot rejected or identity lost; 0xF9 request refused after the acknowledgement |
| 0xFA-0xFD | `cm9xctl bench` (ring 3, 9x) | bench start, writes done, committed, read done (host-time bracketing of the write-through measurement) |
| 0x120-0x132 | 9x MD-4 pool provider and interface 3 link-state receiver (`src/pool9x.c`, from `int-0.2`; 0x132 the late lookup, 0.4.0.0) | see `src/pool9x.h`; 0x122 rc 8 no entry, 15 DOWN_QUIESCED, 16 an entry being updated, 17 malformed, 18 IN_USE alone at the find (each but 17 starts the late lookup); 0x132 `P9B_*` (`src/p9bind.h`: 0 bound, 2 DOWN, 4 duplicate, 5 malformed, 6 table changed, 7 window over, 8 IN_USE alone), ms since the find / 10, generation, bus |
| 0x140-0x143 | NT interface 2 link-state receiver (`src/cmlinknt.c`, 0.4.0.0) | see `src/cmlink.h`: 0x140 open and binding result (`CML_RC_*`: 0 bound, 2 no callback object, 3 no answer, 6 another interface version, ...; entry, generation, bus), 0x141 `DOWN_QUIESCED` seen (marker), 0x142 statement applied and `AckGeneration` written (marker), 0x143 binding ended (`CML_LOST_*`) |
| 0x1B5-0x1B6 | 9x CM stand-in, interface 3 revision (0xB5-0xB6 before `int-0.2`) | 0x1B5 teardown request for tunnel port 1 (rule `at=stage code=0x1b5 act=surprise` on the second bridge); 0x1B6 diagnostics (hostile modes 0xA0xx, test switches 0xB0-0xB4, retirement 0xC0+port, UP deferred 0xD0+port, dump 0xD1-0xD4, lifetime 0xE1-0xE3, heartbeat 0xEE) |
| 0x1C7-0x1CF | 9x CM stand-in `cm9x.sys` (`tests/cm9x`; 0xC7-0xCF before `int-0.2`) | 0x1C7 error; 0x1C8 start/parameters; 0x1C9 SHPC slot status; 0x1CA `UP` published (generation); 0x1CB `DOWN`(`_QUIESCED`) published; 0x1CC link-down acknowledgement or timeout (port << 28 \| (ms & 0xFFFFF) << 8 \| acknowledged); 0x1CD CONFIGMG call (call << 24 \| port << 16 \| CONFIGRET); 0x1CE eject/handshake step; 0x1CF teardown request for tunnel port 0 (harness rule `at=stage code=0x1cf act=surprise`) |
| 0x1D0-0x1DF | G9 instrumentation (`src/g9dbg.h`; 0xD0-0xDF on `qual-9x`, not built on the engine) | 0x1D0 `DriverEntry`, 0x1D1 `HwFindAdapter`, 0x1D2 found, 0x1D3/0x1D4 `HwInitialize` enter/exit, 0x1D5 StartIo, 0x1D6 complete, 0x1D7 `HwResetBus`, 0x1D8 `HwAdapterState`, 0x1D9 SHUTDOWN SRB, 0x1DA timer, 0x1DB probe timer, 0x1DC-0x1DE gone/deadline/fast-fail (level 2), 0x1DF summary |

**Separate tables, colliding with the unified one** (decode a trace only with the header
of the branch that produced it):

| Branch | Header | Codes |
|---|---|---|
| `phase0-g9` (G9, 9x) | `src/g9dbg.h` | its own table: 0x01-0x05, 0x10-0x11, 0x20, 0x30-0x31, 0x40-0x41, 0x50-0x52, 0x60, with meanings that differ from `dbgport.h` (on `qual-9x` these moved to 0xD0-0xDF, and on `int-0.2` to 0x1D0-0x1DF, above) |
| `phase0-g3g6` (G3, G6, CM stand-in) | `src/dbgport.h` | shared layer and G2/G4 as above, but 0x90-0x96 are G3 (receipt, link-state attach and seen) and 0xA0-0xAC are G6 (dump `DriverEntry`, find, allocation, init, reset, range), colliding with G5/G7 and the qualification layer; 0xC0-0xC6 are the CM stand-in `cmstub.sys` |

When G3/G6 and G9 merge into product code, their codes move into free ranges of the
unified table, and the 9x and NT builds share the one table. Since `int-0.2` (the merge
of `engine` and `qual-9x`, [INTEGRATION-0.2.md](INTEGRATION-0.2.md)) codes are 32-bit: the
request engine keeps 0xB5-0xBF and 0xC7-0xDC, the test-only 9x stand-ins moved up by
0x100 (cm9x 0x1B5-0x1B6 and 0x1C7-0x1CF, G9 0x1D0-0x1DF), 0xC0-0xC6 stay the NT CM
stand-in's, and 0xDD-0xDF and 0xFE-0xFF are free.

**Product code** (`src/dbgport.h`, built with `STOR98_DBGPORT=1 scripts\build.cmd`, images
in `out\<flavor>-dbgport\`): one table for the 9x and NT builds, keeping the meanings of
the unified table (shared layer, 0x81-0x8D gone state and reset, 0xA1-0xB4 durability,
restartable shutdown and stop path). The request engine uses 0xB5-0xBF (admission,
child submit and completion, parent completion, stale CQE, capacity wait and resume,
bounce, mapping failure, cancellation, idle flush) and 0xC7-0xD9 (queue capabilities,
BUSY limit, engine accounting, clock calibration, DMA negotiation, tunables, isolation
violation, blocked double completion, stop result, admissions, evidence, DMA memory,
clock re-calibration and OS-clock state, disable rewritten in quarantine, settle before a
re-enable, sense kept and read, 9x DMA quarantine gap).
The 9x build writes the port with plain IN/OUT. Test-only tunables, read from
DriverParameter in test builds only: `frag=<KiB>` (largest child, to make multi-child
parents), `bounce=1` (bounce every data child) and `calx=<percent>` (scale the TSC
calibration, to simulate the 0.57x-2.05x miscalibration seen under TCG; `calx=57` makes
the TSC estimate run 1.75x fast), `shnflush=1` (SRB_FUNCTION_FLUSH runs the restartable
shutdown, so `FlushFileBuffers` drives the SHUTDOWN path for the late-Flush tests) and
`capto=<ms>` (a bring-up and disable wait longer than the controller's CAP.TO, 7.5 s on
QEMU, to hold a bring-up past a port's reset threshold), `stormtest=1` (one redundant I/O
CQ head doorbell after bring-up, which in QEMU's model leaves INTx asserted after the next
completion: the interrupt storm of the "INTx model caveat", on demand, to exercise the
mask-and-poll recovery), `stormints=<n>` (empty interrupts per 1 s window taken as a storm, default 1000: a lower value lets another device on a shared INTx line reach it in the emulator, for the shared-line storm test: masking must not disable or reset the controller while another device keeps the line asserted), `idleto=<ms>` (idle Flush deadline, default 30 s and never below CAP.TO, for the held-idle-Flush test: harness `x-faults=at=fetch op=flush act=hold`); `gone=<ms>`, `idle=<ms>` and
`hold9x=<ms>` are product tunables. The 9x port passes no `ArgumentString`, so 9x test
images take test defaults at build time instead: `set STOR98_TESTDEF=-DSTOR98_TEST_CAPTO_MS=40000`
together with `STOR98_DBGPORT=1` (the image lands in the same `out\<flavor>-dbgport\`
folder; copy it away before the next build). build.exe does not track `C_DEFINES`: touch
`src\lifecycle.c` before building with a different `STOR98_TESTDEF` and again before the next
ordinary build, or the objects of the previous build are linked unchanged.

Guest tools on the engine branch (NT, MSVC 6.0, `cl /O2`):

- `tests/eio`: concurrent unbuffered data verify with per-request completion accounting;
  each request must complete within 120 s of its own issue, checked on every loop
  iteration whatever the wait returned (a hung request is reported at its bound even
  while other slots keep completing).
- `tests/trimchk`: the PLAN 1.5 regression (TRIM_MODE_ON rejected, buffer and disk
  unchanged).
- `tests/sensechk`: sense retention and bounded REQUEST SENSE through
  `IOCTL_SCSI_PASS_THROUGH` (the pass-through part of "Sense construction" (b)-(d)).
- `tests/scsichk`: the driver's own SCSI answers through pass-through (INQUIRY standard
  data, short allocation, VPD 00h/80h/83h, refusals with 05/20 and 05/24, MODE SENSE
  WCE, changeable values and block descriptor length, READ CAPACITY) and the miniport
  IOCTL path (`NVME2KDB` query, `SCSIDISK` SMART codes refused within 3 s).
- `tests/devcycle`: disables and re-enables a device through the SetupAPI property-change
  request (what Device Manager does), N times; `list` shows instance IDs, `remove`
  uninstalls a node so that it is found as new hardware, `disable` and `enable` change
  the first match once (used to unload and reload xhci98's CM, whose every device must be
  disabled for that). Link with `setupapi.lib`.
- `tests/cmnt`: test-only NT stand-in for a CM's interface 2 table (`cmnt.sys`, legacy
  driver, `net start cmnt`; build with `tests\cmnt\build.cmd`). Its `Parameters` key sets
  the table's `Version` (default 1, the interface's original version) and one entry (`Bus`, `Slot`,
  `Flags`, `Gen`); the table never changes after `DriverEntry`, as a CM leaves it at its
  unload. It refuses to start when `\Callback\Stor98LinkState` already exists (a live or
  unloaded CM in that boot).
- `tests/logsense`: LOG SENSE page 2Fh through pass-through, an admin command (Get Log
  Page) on demand, for the late admin completion tests.
- `tests/fbar`: a flush is a barrier for the writes before it. Overlapped writes to the
  unpartitioned end of the disk while the harness holds every write
  (`at=fetch op=write act=hold delay=3000 repeat=on`), then `FlushFileBuffers`: no write
  may still be pending when the flush returns.

Host unit tests (`tests\unit\build.cmd`: MSVC 6.0 with the DDK headers, built once against
the NT and once against the 9x `SRB.H`, run on the build machine; route (1) of "SRB-level
test client"):

- `tests/unit/sense_ut.c`: src/sense.c with hand-built SRBs. Autosense for one request
  does not erase another's kept sense (two requests completed in one batch, a
  case no guest tool can produce: pass-through requests are untagged and run one at a
  time), one entry per request in completion order, the new-command and reset rules,
  bounded REQUEST SENSE, the entry cap.
- `tests/unit/cmlink_ut.c`: src/cmlink.h, the NT link-state receiver's decisions, on
  tables the test builds: interface version 4 only, one table per boot, binding (no
  entry, duplicate, malformed, `DOWN_QUIESCED` at binding, an entry mid-update), the
  odd/even entry protocol with torn snapshots, generations (newer `DOWN_QUIESCED` applied
  but not acknowledged; newer `UP` alone not evidence; older generation, another bus,
  `IN_USE` cleared or an unknown flag end the binding), the acknowledgement value, a
  frozen table.
- `tests/unit/nsfmt_ut.c`: src/nsfmt.h, the namespace format decode (PLAN 1.1): the
  format FLBAS selects (with bits 6:5 when there are more than 16 formats), 512 to
  4096 bytes served, other block sizes, NSZE 0, an index past NLBAF, metadata and
  protection refused.

## Measurement definitions

Used by the removal-acceptance cases (PLAN "Removal acceptance", R3-13).

- **T0**: the usb4-emu trace event recording that the tunnel gate closed.
- **T1**: detection; the driver's log line for the transition to `gone` (or `failed`).
- **T2**: the last SRB the driver held at T1 is completed.
- **T3**: the test application receives its first error.
- **Tcm** (9x only, R6-9): the named CM actor's CONFIGMG call (`Query_Remove_SubTree` and
  `Remove_SubTree`, or `Reenumerate_DevNode`) on the controller's devnode. The actor is
  xhci98's CM or a harness stand-in (`cm9x`, or `cmreenum remove` issued 1 s after T3, as
  in G9); the record names the actor and the TSC-stamped time of its call.
- **T4**: the volume is reported gone (NT: the disk disappears from the device stack; 9x:
  the drive letter is removed by IOS, which happens only after Tcm: NO_DEVICE ends IOS's
  retries but leaves the drive letter until CONFIGMG acts, G9).

T1-T4 come from guest log lines stamped with the TSC and written to the debugcon port.
The guest clock is correlated to the host trace by a marker: the debug build writes a
known value to an otherwise unused doorbell offset (or the debug I/O port) together with a
TSC-stamped log line, and the host trace records that write.

Test build settings: watchdog tick 250 ms; per-command deadline 5 s; gone deadline 5 s
(the default of every build, a tunable: `gone=` on NT; PLAN 0.10 and MD-1); admission limit 32 parents / 128 children.

Workload during removal:

- NT: unbuffered, overlapped, write-through I/O (`FILE_FLAG_NO_BUFFERING |
  FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH`), queue depth 8, 64 KiB requests.
- 9x: VWIN32 Int 21h function 7305h (extended absolute disk read/write), queue depth 1.

Thresholds (initial targets): T0 to T1 at most 2 s; T1 to T2 at most 5 s; T0 to T3 at most
60 s; T4 at most T0 + 60 s; driver-pending SRBs never above the admission limit. On 9x
two parts are judged separately (R6-9): the **standalone** part (T0-T3, the miniport's
error completion through IOS to the application, no CM involved) against the thresholds
above, and the **CM-assisted** disappearance, T4 at most Tcm + 5 s; T4 at most T0 + 60 s
then also depends on when the actor calls, which the record states. The
observation window is 10 minutes starting at T0. Recovery-loop criterion (R4-6, the same
as PLAN "Removal acceptance"): recovery events are counted over the whole window, NT events
117 (bus reset) and 9 (timeout), and on 9x, where those events do not exist, the driver's
reset and timeout debug counters. One initial recovery is allowed: at most one bus reset
and at most one timeout in the window, both before T4; none after T4.

Sustained absence (the tunnel stays down for the whole window) and reconnect are separate
tests, not phases of one run.

## Scripted 9x cases (`tests/int9x`)

The 9x harness runs 98 SE and Me guests from the VM folder with the cm9x stand-in
(`tests/cm9x`) and the debug-port image. `lib.sh` holds what the scripts share: the VM
folder (`STOR98_VMS`, by default the `vms\` junction), its `tools\` copies of `qmp.py` and
`typekeys.py` (`STOR98_QMP_PY`, `STOR98_TYPEKEYS` override them), QEMU from its `bin\`, a
Python 3 interpreter (`python3`, else `py -3`, or `STOR98_PYTHON`), and the checks. Build the
images (`scripts\build.cmd`, the same with `STOR98_DBGPORT=1`, `tests\cm9x\build.cmd`), then
`mkxfer.sh` writes the transfer images; `prep.sh` installs cm9x into a base overlay.

Every section ends with explicit checks, each PASS, FAIL or INCONCLUSIVE (a value the run
cannot decide, with the reason); the script prints a table per section and exits with the
number of failures. `q9log.py --ejects` then judges the CM statements and ejects of the
trace. Expected values come from the code and the dated records ([INTEGRATION-0.2.md](INTEGRATION-0.2.md)).

| Script | Cases |
|---|---|
| `smoke9x.sh` | bring-up and data verify; surprise removal with `DOWN_QUIESCED` (evidence 2, no MD-1 deadline); re-arrival reusing the released block; surprise without `QUIESCED` (MD-1 deadline, E1/E2 release at re-arrival); clean eject (MD-5) |
| `rm9x.sh` | removal transaction: clean eject, veto, retry and overlap, FAILED (RDY never clears), CM timeout |
| `rm9x2.sh` (`rm9x4.sh` F-H, `rm9x5.sh` F) | shutdown episode then remove, shared-line ISR during the drain, budget-forced disable, identity loss, BUSY without a limit, overlap during an open transaction |
| `rmacct2.sh` | pool accounting damaged at relinquish (fail closed, cold restart), `BudgetSkewMs` |
| `hostile9x.sh` | the rendezvous refusals for cm9x `Hostile` 1-12 (0x122 rc per mode, mode 10 identity loss) |
| `rmio9x.sh` | surprise removal during I/O with interrupts and in the polling mode (`StorPollMode` 1: 0x152 why 1, no 0x156), back to interrupts |
| `ns4k9x.sh` | 4 KiB-LBA namespace: bring-up, unaligned writes with no hang, removal. Until 512-byte translation lands, its data checks are INCONCLUSIVE |

Rows of the tables below that stay manual: every NT case (Windows 2000, XP x64, 7; the NT
guest tools above are run by hand, and the 2000 `eio` run uses `runw2k.sh`), the release
package install check, the cases that need a usb4-emu hook listed under "Harness work
needed" (exact-point faults, hung device, DMA path open, reopen without endpoint reset,
abort outcomes, lost interrupts, media errors, DMA bounds, namespace layouts, identity
change, capacity over 2 TiB), dump, suspend, resume and shutdown, the bridge cycle under
Verifier, the timing thresholds and the 10-minute window of the removal-acceptance cases,
and on 9x the link-down-in-a-terminal-state and locator-without-proof cases. Their records
are the dated notes in the rows.

## Required test cases

### Data path

| Case | How | Pass |
|---|---|---|
| Basic I/O | tunnel up after boot, format FAT32/NTFS, write and read back, verify on the host offline | hashes match |
| 4 KiB sectors | add `-global nvme.logical_block_size=4096 -global nvme.physical_block_size=4096` | the OS sees 512-byte sectors and the full capacity; data verifies with partitions at sector 63 and 2048; no hang |
| Media traffic (PLAN 4 KiB reporting, R3-11) | trace `pci_nvme_read/write` per SRB on the 4 KiB namespace (B = 4096) | aligned SRB: bytes moved equal the request; unaligned read: the request rounded out to native blocks; unaligned write with E distinct partly covered native blocks (shared head/tail block counted once): rounded-out write bytes + E x B of pre-reads = aligned middle bytes + 2 x E x B, write-backs not counted twice. Checks: 4 KiB write at LBA 63 = 16 KiB; single 512-byte write = 8 KiB |
| DMA bounds (PLAN 0.4) | harness DMA-bounds check, fragmented and non-page-aligned buffers | no PRP range outside the SRB's caller buffer or its bounce buffers; sentinels intact |
| Capacity boundaries (PLAN 4 KiB reporting, R3-10) | sparse images. 512 B namespace: exposed counts 2^32 - 1, 2^32, 2^32 + 1. 4 KiB namespace: exposed counts 2^32 - 8, 2^32, 2^32 + 8 (NSZE 0x1FFFFFFF, 0x20000000, 0x20000001) | 512 B: RC10 last LBA 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF; RC16 last LBA 0xFFFFFFFE, 0xFFFFFFFF, 0x100000000. 4 KiB: RC10 last LBA 0xFFFFFFF7, 0xFFFFFFFF, 0xFFFFFFFF; RC16 last LBA 0xFFFFFFF7, 0xFFFFFFFF, 0x100000007, LBPPBE 3. I/O at the last LBA works and one past it fails with 05/21/00; on OSes that ignore RC16, record the capacity seen and verify no write lands at a wrapped LBA |
| Large disk | I/O beyond 2 GiB and 4 GiB byte offsets | data verifies |
| SCSI answers (PLAN 1.8, 1.1, 0.11 WCE) | `tests/scsichk <n> wce=<0/1> bs=<512/4096>` on 2000, XP x64 and 7: the default namespace, a 4 KiB namespace, and `writethrough=cacheoff`; `sg_inq -e -p 0x80` / `-p 0x83` and `smartctl -H` where available. Bring-up refusals: a namespace formatted with 8-byte metadata (`-device nvme-ns,ms=8`, with and without `mset=1`), with protection (`pi=1`), and NSZE 0 (only `nvme-ns,nsid=2`: namespace 1 inactive) | `scsichk` PASS (wce=1 unless cacheoff was verified, wce=0 then); VPD 80h/83h carry the controller serial; a refused namespace exposes no disk and logs stage 0x48 with its reason; `smartctl -H` completes at once (no port timeout, no bus reset) |
| BAR placement (PLAN 3.3, 4.1) | QEMU with the controller's 64-bit BAR above 4 GiB (q35 or `pci-hole64`, above-4G firmware) on XP x64, 7 x64 and 7 x86 (PAE); 98 SE with the BAR below 4 GiB; a crash dump and a hibernation on 2000 and XP x64 | the disk works where the OS maps the address; stage 0x05 shows the port's range used on NT (flag 01h), the BAR sized with decode off on 9x (flag 02h); on 9x a BAR above 4 GiB fails the find with reason 6; dump and hibernation use the port's range, never size |
| TRIM removed (PLAN 1.5) | PLAN 1.5 regression | disk contents correct; caller buffer unchanged |
| Pre-read media error (parent error mapping) | `blkdebug` error on the edge block of an unaligned write | write fails with 03/11/00; no write-back of that block; other blocks unchanged |
| Sense construction (PLAN sense rules, R3-9, R4-5) | media errors (fake-port media status in the unit harness; `blkdebug` for the kernel test client and pass-through), issued through the SRB-level test client (harness work): all cases in the unit harness on both the NT and 9x builds; all cases through the NT kernel test client on each NT OS; (b)-(e) also through pass-through, checking only the returned sense bytes and `ScsiStatus`. Cases: (a) `SRB_FLAGS_DISABLE_AUTOSENSE` set; (b) `SenseInfoBufferLength` 0; (c) 14; (d) 18; (e) failing native block whose first exposed LBA is above 2^32 | (a)-(c): no byte of the sense buffer written (guard pattern intact), `AUTOSENSE_VALID` clear, a following REQUEST SENSE returns the sense; (d): exactly 18 bytes, `AUTOSENSE_VALID` set; Information and VALID set only when the failing LBA is known (single-native-block child); (e): VALID 0 and Information 0, never a truncated value |
| Namespaces (PLAN 1.9) | only NSID 2 active; several namespaces | lowest active NSID exposed as LUN 0; others logged |
| Interrupt modes (PLAN 2.3) | each data-path case under INTx, INTx with lost interrupts, and polling only | same results; every command completes within the watchdog period plus 1 s |
| Polling mode (PLAN "Polling mode (0.4.0.0)") | polling forced (`StorPollMode` = 1 on 98 SE, `pollmode=1` on 2000, XP x64 and 7 x86; run before the `Stor` prefix, as `PollMode`): install, the interrupt report value (`regedit /e` of `Services\stor98`), I/O with data compare (9x: absw through VWIN32 7305h; NT: raw `PhysicalDrive` write-back and compare at QD1 and QD8, on 7 reads only and a file copy with `fc /b`), surprise removal during I/O, shutdown; the rate check with `StorPollBusyUs` 1000, 10000 and 50000 on 98 SE; polling against interrupts at the defaults; plus the 9x interrupt-mode regression against 0.3.0.1 (98 SE and ME) for the timer re-arm finding | data compares pass; removal reaches gone (and the MD-1 deadline or evidence 2) as with an interrupt; SHUTDOWN or the stop path completes; the time per request follows `StorPollBusyUs`; `HwInterrupt` is not called (stage 0x156 absent). Run 2026-10-16 on the merged tree: pass (numbers in PLAN) |

### Lifecycle and recovery

| Case | How | Pass |
|---|---|---|
| Bring-up, delayed RDY (PLAN 0.10, G1) | harness: RDY set only 2 s after CC.EN | bus scan completes with the disk present; no SRB times out into a port reset; no callback exceeds the stall budget |
| Bring-up failure (PLAN 0.10, 2.5, G1) | harness: identify fails; separately, RDY never set | scan completes with no disk; adapter started in `failed` with its memory owned; controller disabled (RDY=0 acknowledged) or memory quarantined; later requests fail per the parent error mapping |
| Delayed RDY clear (PLAN 0.10) | harness: RDY clears 50 ms after CC.EN=0, then reset via a forced timeout | requests complete with `BUS_RESET` only after RDY reads 0; the wait is carried by the timer; no callback exceeds the budget |
| Failed disable (PLAN 0.10, G4) | harness: RDY never clears after CC.EN=0, reset via a forced timeout | `HwResetBus` returns FALSE; adapter enters `failed`; new I/O fast-fails; held SRBs stay owned and buffers quarantined; at most one `HwResetBus` per expired port timeout; no crash. Record what the port does after `HwResetBus` returns FALSE. Measured on 2000 and XP x64 (G4): one `HwResetBus` per expired timeout (about 10 s apart; a second only 15-23 s later and only when other requests completed in between), LU dispatch stalled (the volume hangs without application errors rather than fast-failing new SRBs), no event 9 or 117 on XP x64, no port-side completion before PnP removal; a late completion of the held SRBs (BUS_RESET plus `ResetDetected`) is accepted and dispatch resumes about 3 s later |
| Stop without acknowledgement (PLAN 0.10 stop, 0.11, G5) | harness: RDY never clears; Device Manager disable (NT) or Configuration Manager stop (9x) | stop returns within the budget; the DMA memory is quarantined, not freed; record whether the port frees or reuses the uncached extension anyway (a G5 failure if it does) |
| Presence lost, no notification (PLAN 0.10 gone state, 2.1, G2, R4-1) | harness: close the tunnel gate at each injection point, with I/O outstanding, and send no G3 notification | driver enters `gone` on all-ones CSTS; no issued SRB is completed and no buffer freed on the all-ones reads alone; new SRBs fast-fail; at the gone deadline the MD-1 outcome applies (see "Gone deadline, no evidence"). Record what the port does meanwhile (G2) |
| Gone deadline, no evidence (PLAN 0.10 gone state, 2.2, MD-1) | harness: close the tunnel gate with I/O outstanding (bounce-buffered RMW children among them), send no G3 notification, keep CSTS all-ones; sentinel-filled bounce and PRP pages | before the deadline nothing is completed or freed; at the gone deadline (5 s after T1, ±1 watchdog tick) every held SRB completes with the terminal status of 2.2; the gone-deadline counter is incremented by exactly 1 and the event is logged; driver-owned DMA memory is quarantined: none of it is freed, reused or handed back afterwards (debug-build pool accounting shows it out of every pool, and later I/O after a reconnect uses fresh allocations or fails). Variant: the G3 notification arrives 1 s before the deadline: the path-down row applies, memory is released, the counter is unchanged. Variant (NT, R6-6): one SRB already outstanding for most of its `TimeOutValue` before T0 (harness delay): it completes at its own per-SRB bound (start + `TimeOutValue` − margin) when that is earlier than the gone deadline, and the port calls no `HwResetBus` during `gone`. Variant (9x): no such guarantee; record whether and when `HwResetBus` arrives during `gone` and what the port does after it returns FALSE |
| Presence lost, hung behind a live tunnel (PLAN 0.10 unresponsive row) | harness: controller MMIO stuck or all-ones while config space answers | no completion of issued SRBs before the gone deadline; then the unresponsive row (stuck CSTS: SRBs held, memory quarantined) or the gone-deadline row (all-ones CSTS: SRBs completed per MD-1, driver memory quarantined, counter incremented) applies |
| Presence lost with the DMA path open (negative, PLAN 0.10, R4-1, MD-1) | harness hook of the same name: reads all-ones, DMA alias open, endpoint not reset; a delayed NVMe Read (the device writes guest memory) completes its data transfer after T1; sentinel-filled caller and bounce buffers | two runs. (a) Transfer released before the gone deadline: nothing has been completed or freed, so the late data lands only in still-owned buffers and no sentinel outside them changes. (b) Transfer released after the gone deadline (MD-1 outcome taken): driver-owned bounce and PRP pages are still quarantined, so a late write there changes no OS page; a late write into a caller buffer already returned to the OS is expected and recorded as the accepted residual risk of MD-1 (which sentinel changed, and when relative to T2) |
| CM notification (PLAN 0.10 evidence 2, G3 interface prototype and per-port qualification, R4-7, M1) | CM prototype or stand-in tears the tunnel down and sends the notification, at each injection point; repeated with the admission limit and the port's queues saturated by held SRBs | on every OS and architecture that relies on G3: notification sent only after completed quiescence, checked in the host trace (the notification's marker timestamp is later than usb4-emu's stage-2 endpoint reset/quiesce completion event, ARCHITECTURE §4.4), and received before that OS's first removal callback, within the gone deadline even under saturation; held SRBs complete per 2.2. The carrier is the link-state table (MD-6): `DOWN_QUIESCED` seen from the watchdog tick, the bring-up timer or `HwStartIo`, `AckGeneration` written, and the CM starting the OS removal path only after that acknowledgement (or its wait, up to the gone deadline, timing out). Injection points as G3 ran them: SRBs held, saturated (port queue full), idle, during bring-up, at the `RequestComplete` stage, and a statement arriving while already `gone`; plus the negatives: no `QUIESCED` flag (ignored), and an early statement (the host-trace check must fail). A stand-in that sends the notification before the stage-2 completion event fails the run. Passed in the prototype on 2000 and XP x64 (G3) |
| CM notification, product NT receiver with xhci98 (0.4.0.0; PLAN "0.4.0.0 plan" item 0) | usb4-emu bin-2a893ea (SHA-256 `6a1dfcc8...bd66`), i440FX, `usb4-host,profile=usb4-generic,host-model=intel-adl` with the tunnel bridge; xhci98 3.0.0.0's `xhcicm.sys` (`objchk_qemu` of `793f8df7`, the link-state code of the 3.0.0.0 tip, `XhciLogVerbosity` 2, notes on port 0xE9) and its bridge filter; Stor98 debug-port build. Plug and unplug by QMP `device_add`/`device_del` of the device router (the CM path); I/O by `tests/eio` (QD 8) on the NVMe volume; counters read at the NHI's stop. Without the CM: the NVMe directly behind the tunnel bridge, tunnel by QMP; the `tests/cmnt` stand-in for a version 1 table and a frozen version 4 table | receiver opens the table (stage 0x140 rc 0) and binds the generation the CM published UP before enumeration; on the CM's `DOWN_QUIESCED` the held requests complete and `AckGeneration` is written (0x141, 0x142), then `BusChangeDetected`; the CM counts the acknowledgement (`acked`), never `no-receiver` or `ack-timeouts`; where the CM withholds the statement, the MD-1 deadline applies; without a CM, after a CM reload, or with a table of another version: no channel and no regression. **Run 2026-10-16 (nt-link):** 2000: bound gen 2; unplug during I/O: `DOWN_QUIESCED` seen 129.1 ms after the emulator's stage-2 completion (`usb4_tp_cutoff`), acknowledged 0.4 ms later, CM wait (`cm.nvlink.await`) 70 ms; idle unplug: acknowledged 211.6 ms after stage 2, CM wait 40 ms; counters `ups` 4, `sent` 2, `acked` 2, `ack-timeouts` 0, `no-receiver` 0, `queries` 2. CM unloaded and reloaded in the same boot (every CM device disabled, then enabled: `cm.nvlink.nt=40000000`): the next find got no answer (rc 3), the unplug was a plain PnP removal. XP x64: unplug during I/O: acknowledged 163.5 ms after stage 2, CM wait 0 ms (already acknowledged at its first read); idle: 129.7 ms, CM wait 94 ms; `ups` 6, `sent` 2, `acked` 2, `ack-timeouts` 0, `no-receiver` 0, `queries` 2. 7 x86 (not qualified by the CM): bound gen 4 and `UP`; unplug during I/O: statement withheld (`cm.nvlink.withheld=09000004`, UNQUALIFIED), the held request completed at the MD-1 deadline (0x84, 0x83), then `BusChangeDetected`; `ups` 4, `sent` 0, `withheld` 1, `acked` 0, `no-receiver` 0, `queries` 1. Without the CM (2000): rc 2, gone deadline at 5 s, disk removed; `cmnt` version 1: rc 6 (refused), adapter ready; `cmnt` version 4, frozen: rc 0, no statement after a surprise removal, MD-1 deadline. Changing the NVMe's driver with xhci98's CM loaded made the CM tear the tunnel down (`cm.reenum.eject`), so each OS needed a re-plug after the update. Note: with `usb4-generic` and no CM, QMP `tunnel up` exposes the NVMe but every READ to it failed (2000, both an older driver and this build), so no-CM runs use the plain NVMe behind the bridge. `tests/devcycle` (32-bit) cannot disable devices on XP x64 (WOW64); there the counters were read at shutdown |
| CM notification, 9x late lookup with xhci98 (0.4.0.0; PLAN "0.4.0.0 plan" item 5) | usb4-emu `vms\bin` (SHA-256 `1bba8fa4...184c`; bin-2a893ea segfaults at start with `-device usb4-debug-port` in this tree), i440FX, `tb3-alpine-ridge` (ICM) with the Alpine Ridge tunnel bridge as in xhci98's NV9X.1 runs; xhci98 3.0.0.0 NV9X.1 (`xhcicm.sys` qemu flavour of `edb1c464`, SHA-256 `7516c71d...6a3e`, `XhciLogVerbosity` 2) on copies of NV9X.1's 98 SE and ME guests; Stor98 debug-port build. Plug and unplug by QMP `device_add`/`device_del` of `enc0` after the desktop is up; Safely Remove with XHCITR | a drive plugged after startup is found without an entry (0x122 rc 8) and bound late (0x132 rc 0) to the generation the CM posts; after an unplug, the re-plug's find refuses the previous generation (rc 18 or 15) and binds the new one late; the adapter stays running and the volume is usable; a boot-present drive starts (with or without a channel); a surprise removal ends at the MD-1 deadline where the CM withholds the statement. **Run 2026-10-16 (latebind):** 98 SE, first plug: rc 8, bound gen 1 160-170 ms after the find (`cm.nvlink.up.late` 0xCE00), E: written and read. XHCITR Safely Remove (stop, record ISOLATED then RELEASED), unplug (CM `cm.nvlink.withheld=09000001`: UNQUALIFIED on 9x, UP cleared), re-plug: rc 18 (IN_USE alone gen 1), bound gen 2 170 ms after the find, running, E: listed. The same sequence on the earlier build of this branch (before the UP-only rule) bound gen 1 at the find, took UP gen 2 as R8b evidence (0x124), went gone and failed: no disk (the defect PLAN item 5 records). Surprise unplug during `xcopy`: gone with no command outstanding, statement withheld, MD-1 deadline (0x83); the system-modal Disk Write Error held the CM's notes until dismissed on 98 SE. Boot-present drive: the find came before the CM published the proof (rc 10), no channel, E: read. `StorWriteThrough` = `fua` read (0xDE 0x122, bring-up FUA). ME: Stor98 installed by the wizard from the transfer drive at the first plug (the find came after UP: bound gen 1 at the find); Safely Remove (no stop callback on Me: the record quarantined at the next find, then released by E2), unplug (withheld), re-plug: rc 18, bound gen 2 190 ms after the find, E: written and read; `auto` write-through resolved to `cacheoff` and verified (0xDE 0x1113, 0x250); surprise unplug: MD-1 deadline. Not exercisable with xhci98 3.0.0.0 on 9x: the link-down statement and its acknowledgement, and the MD-5 clean-eject handshake (xhci98 NV9X.3) |
| Two drives in one ICM domain (9x; PLAN "Decided", a newer generation on 9x, 2026-10-18) | as the row above on 98 SE (`tb3-alpine-ridge`, xhci98 with its CM, Stor98 debug-port build), two NVMe drives on two ports of one domain: drive A plugged and bound, then I/O on A (`xcopy` or `tests/eio`) while drive B is plugged | A keeps running and its I/O completes without error: where the CM increments A's generation at B's tunnel-up, A's receiver logs identity loss (0x126 arg 0x23), never 0x124 or `gone`; B is bound (0x132 rc 0) and usable; after that identity loss a surprise removal of A ends at the MD-1 deadline, and at the next find no record is released by E1 on the later generation alone (no 0x121 with cause 4). Not run yet |
| Quarantined block after a surprise removal (9x; PLAN "Decided", 2026-10-18) | as the row above on 98 SE, one drive: I/O, surprise removal where the CM withholds the statement, then re-plug into the same port | the removal ends at the MD-1 deadline and the old instance's record is QUARANTINED; at the re-plug's find it is not released by E1 (the entry shows a later generation, no DOWN_QUIESCED); after the new bring-up's disable acknowledgement (or a controller found disabled) it is released by E2 (0x121 to 4, from 5, cause 5, same bus); a record of another bus stays quarantined. Not run yet |
| Reopen, fresh endpoint (PLAN 0.10 reopen invariant, 2.6, R4-3) | surprise removal during I/O with the G3 notification, then `tunnel up` (usb4-emu resets the endpoint in stage 2 and realises a fresh one) | the driver's first CC/CSTS read after reopen shows CC.EN=0 and RDY=0; no completion for an old CID arrives; the old queues are gone (the trace shows no fetch from the old submission queue addresses); I/O works after bring-up |
| Reopen without endpoint reset (PLAN 0.10 reopen invariant, R4-3, M2) | harness hook of the same name, with buffers released on the notification filled with sentinels. Delayed transfer released (i) only after the driver's RDY=0 acknowledgement (the hook holds it through the CC.EN=0 write and the RDY=0 wait), (ii) immediately at reopen, before the driver's first access | violation detected: CC.EN=1 or RDY=1 logged as an isolation-contract violation; CC.EN=0 written and RDY=0 acknowledged before any queue, CID or buffer reuse; in case (i) old DMA destinations are untouched up to the acknowledgement, and no driver buffer, queue or CID was reused before it. Case (ii) is expected to change an old destination: recorded as the exposure that only the CM obligation prevents (MD-2). DMA released between the CC.EN=0 write and RDY=0 is not a test case: PLAN 0.10 classifies it as exposure after an already-violated contract (documented behaviour) |
| Reopen without endpoint reset, disable never acknowledged (PLAN 0.10 reopen invariant, unresponsive row; MD-1 not widened) | as above with I/O outstanding and no G3 notification, the gate reopened before the gone deadline, and the harness keeping RDY=1 after the driver's CC.EN=0 write (as "Failed disable"); separately, RDY cleared 2 s after the write | violation marker (0xCD) once per contact, then `resetting` (stage 0x40: 7 to 3, reason 12); no gone-deadline marker (0x83) and no held SRB completed at or after the gone deadline; RDY kept at 1: `quarantined` after CAP.TO, held SRBs stay owned, CC.EN=0 written again at the low rate while CC.EN reads 1 (0xD5), new I/O fast-fails; RDY cleared: the held SRBs complete `BUS_RESET` after the acknowledgement and the adapter is `failed` with nothing re-created (no CC.EN=1 written) |
| Reset with I/O in flight (PLAN 0.2, 0.10) | force a command timeout (harness delays one completion past the 5 s deadline) | parent completes once; no stale completion accepted after the reset |
| Abort, not aborted (PLAN 0.10 abort, R4-4) | `SRB_FUNCTION_ABORT_COMMAND` on a delayed command; the Abort completes with Dword 0 bit 0 = 1 | the abort SRB and target stay pending until the target's own completion is consumed (release the delay) or escalation acknowledges a reset; target never completed early |
| Abort, completion race (PLAN 0.10 abort, R4-4) | Abort reports aborted; the target's own completion posted before, and separately after, the Abort's | in both orders the target completes exactly once, only after its own completion is consumed; abort SRB SUCCESS |
| Abort, several live children (PLAN 0.10 abort, R4-4) | parent with three children, two delayed; abort it | no further child dispatched after the abort; the parent and abort SRB complete only after every child is accounted for (own completion or acknowledged reset); `ABORT_FAILED` if escalation fails |
| Cancellation, fully pending parent (PLAN 0.10 parent cancellation, H1) | admission limit reached so a parent is admitted but has no child dispatched; abort it | no child of it is ever dispatched (trace); the parent completes `ABORTED` with `DataTransferLength` 0, never SUCCESS; the abort SRB completes SUCCESS |
| Cancellation between RMW stages (PLAN 0.10 parent cancellation, H1; 4 KiB) | 4 KiB namespace, unaligned write; exact-point hook on the "after pre-read" stage code issues the abort (and, separately, a forced reset) before the write-back is dispatched | the pre-read completes successfully, the write-back is never issued (trace), the parent completes `ABORTED` (`BUS_RESET` for the reset), never SUCCESS; the bounce buffer is released only after the pre-read child's own completion was consumed; the on-disk block is unchanged |
| Cancellation racing successful children (PLAN 0.10 parent cancellation, H1) | parent split into four children by MDTS, admission limited so two are dispatched and two are undispatched; abort issued while the two dispatched children's successful completions are posted, in both orders relative to the abort (harness completion delay) | in every ordering the undispatched fragments are never issued and the parent completes exactly once, `ABORTED`, never SUCCESS, only after both issued children's completions were consumed; no buffer released before then |
| Orderly stop (PLAN 0.11) | Device Manager disable, then enable | trace: Flush after the last write, then CC.SHN, then CC.EN=0; disk works after enable. On XP x64 and 7 the OS sends no flush before the stop (G8), so the Flush must come from the idle flush (MD-7 (a)) or the stop path itself. Record the Flush and SHST latency against the stop budget, and whether the Flush completed inside the callback (PLAN 0.10, durability at stop, R6-3); the dirty state is cleared only by a completed ordered Flush. See "Unflushed window at disable" |
| Stop flush exceeding the budget (PLAN 0.10 durability at stop, R6-3) | harness: delay the NVMe Flush completion (and, separately, SHST) past the stop budget; writes completed just before a Device Manager disable | the stop returns within the budget; the stop is logged as not clean; the dirty state is not cleared; nothing claims the data durable (MD-7 (a), accept-and-document part). Q-NT measured that even the emulator's Flush misses the budget at an XP x64 or 7 disable (2.1-18.9 ms) |
| Unflushed window at disable (PLAN 0.10 durability at stop, MD-7 (a)) | XP x64 and 7 x64 (where the OS sends no flush before a disable); writes to a known file, then Device Manager disable at a range of delays after the last write completes, from 0 up to beyond the idle-flush time; separately with continuous writes up to the disable; the same on 2000 as the control | the trace shows the timer's idle Flush submitted once writes have been idle for the configured time and the dirty state cleared on its completion; for every run record the **unflushed window** at `ScsiStopAdapter`: the time from the first write submitted after the last completed ordered Flush to the stop (0 when clean). With the disable later than idle time + Flush latency after the last write, the stop finds the adapter clean; earlier, the window is at most idle time + Flush latency and the stop is logged as not clean if its own Flush misses the budget. The measured window is what the user documentation states |
| Write-through on Me (PLAN D3) | Windows Me with the int-0.2 CM stand-in (cm9x); `WriteThrough` (`StorWriteThrough` from 0.4.0.0; REG_SZ, HKLM\System\CurrentControlSet\Services\stor98) auto, off, fua; 16 MiB `cm9xctl write` + commit, then a `COPY` and a surprise removal with DOWN_QUIESCED, then re-arrival and verify | stage 0xDE: auto resolves to cacheoff on Me only (0x1113; 2000 gives 0x0103, off), each bring-up ends with Set and Get Features FID 06h and reports the cache verified off (0x250); `off` and `fua` take effect at the next load (0x1100, 0x1122; FUA bring-up 0x260). **Measured 2026-10-16 (engine 67221d6 merged on int-0.2):** both files verified after re-arrival. Me still showed "Disk Write Error" after the removal: the failing writes are VFAT's lazy writes that reach the driver after the device is gone (fast-failed in `gone`, stage 0x82 WRITE(10)), so device write-through cannot prevent it; only the file system's write-behind (Me: File System Troubleshooting, write-behind caching) or a CM quiescing the volume before removal (MD-5) can. Cost, 16 MiB write spans (TCG, noisy): cacheoff 2.1/7.0/5.5 s, off 6.4/6.8/5.5 s, fua 6.5/5.1 s: no measurable difference in the emulator, which ignores FUA and writes through to a host SSD (as Q-9x item 4); real cost needs the P14s |
| Settings (0.4.0.0; PLAN "0.4.0.0 plan" item 3) | 98 SE (perf/98 base, cm9x stand-in, QEMU TCG, debug-port image of 4e9c0ef installed before the tunnel comes up), one cold boot per registry set in HKLM\System\CurrentControlSet\Services\stor98: (a) none; (b) `StorMaxTransferKB`="256", `StorWriteThrough`="fua", `StorPollMode`="1"; (c) `StorMaxTransferKB`=dword 1000, only the old `WriteThrough`="cacheoff"; (d) `StorMaxTransferKB`="130", `StorWriteThrough`="off" with `WriteThrough`="fua". Each: VWIN32 absolute reads (aligned, 8 MiB at 256 and 512 KiB), the absw write/read/compare, the key read back with `pt rep`. 2000 (int/w2k base, debug-port x86 image): `DriverParameter` absent, then `maxtransferkb=128`, then `maxtransferkb=512`, each followed by a disable/enable and `eio` (16 MiB, QD4, up to 512 KiB requests) | stage 0x160 (limit, result), 0x161 (refused raw value), 0xCB (limit and breaks given to the port), 0xDE (configured write-through, bit 13 = read from `WriteThrough`), 0x152 (polling reason); the 9x interrupt report named `StorInterrupt <b.d.f>`; data compares pass. **Measured 2026-10-16 (maxxfer 4e9c0ef):** 98 SE (a) default 512 KiB / 129 breaks; aligned 512 KiB reads arrived whole (14 NVMe reads of 512 KiB); `StorInterrupt 1.1.0` written (`... interrupt`). (b) 0x160 256 KiB set, 0xCB 256 KiB / 65 breaks; the same aligned 512 KiB reads arrived as IOS's 63 KiB SRBs (144 SRBs; 256 KiB reads unchanged, whole); write-through `fua` configured and resolved; polling forced (why 1), report `polling (forced by StorPollMode)`; compares passed. (c) 1000 refused (0x161 = 1000, 0x160 result 2), 512 KiB / 129 breaks, 512 KiB reads whole again; `WriteThrough`=cacheoff read as the fallback (0xDE configured 1, bit 13 set). (d) 130 refused (0x161 = 130), 512 KiB / 129 breaks; `StorWriteThrough`=off won over `WriteThrough`=fua (0xDE configured 0, bit 13 clear). The fallback read of `WriteThrough` in (c) and (d) was removed before 0.4.0.0 was released: 0.4.0.0 reads only `StorWriteThrough`, and stage 0xDE has no bit 13 (README "Settings"). 2000: absent DriverParameter: 256 KiB / 65 (default); `maxtransferkb=128`: 0x160 128 KiB set, 0xCB 128 KiB / 33 breaks, `eio` PASS; `maxtransferkb=512`: refused (0x161 = 512), 256 KiB / 65, `eio` PASS; report value written (0x154 = 0). **After the poll merge (8608102):** 98 SE with nothing set: stage 0x150 spin 0, busy 1 ms, idle 1 s; `StorPollMode`=1: polling forced (0x152 why 1), report `polling (forced by StorPollMode)`, compares pass. 2000: nothing set: 0x150 spin 500 us; `pollmode=1`: mode 1, spin 500 us, forced (0x152 why 1), report written, `eio` PASS. |
| Shutdown (PLAN 0.11, G8) | OS shutdown and restart with dirty data, `CachesData` TRUE (decided for NT, 9x and Me, 2026-10-15) | trace shows the shutdown flush issued and completed before CC.SHN; the final trigger seen is recorded per OS. Expected (G8, G9): XP x64 and 7 one `SRB_FUNCTION_SHUTDOWN`; 2000 two with I/O between (CC.SHN on each, the controller re-enabled for the I/O after the first: see "Windows 2000 two-SHUTDOWN sequence"); 98 SE `HwAdapterState(FALSE)`; Me `SRB_FUNCTION_SHUTDOWN`. The SHUTDOWN SRB is tagged on XP x64 and 7 x64 (untagged on 2000, Q-NT) |
| I/O after barriers (PLAN 0.11 class 1, R3-5, G8) | issue I/O after a FLUSH and after a SYNCHRONIZE CACHE | each completes normally with no controller re-init; interrupts and timer stay active |
| I/O after a shutdown notification (PLAN 0.11 class 2, R6-5) | issue I/O after an `SRB_FUNCTION_SHUTDOWN` that wrote CC.SHN (test client, any NT OS and Me) | the SHUTDOWN SRB completed only after SHST complete; the next I/O is held or BUSY (0.3) while the controller is re-enabled (CC.EN=0 with RDY=0 acknowledged, then bring-up and queue re-creation) and then completes normally with data verified; resources retained (same pool or uncached VA/PA); interrupts and timer active throughout; no teardown |
| Idle Flush outstanding at SHUTDOWN (PLAN 0.11 class 2, 0.3 hold) | test build, `shnflush=1`, `idle=10`; harness holds the idle Flush at fetch (`x-faults=at=fetch op=flush act=hold`, one shot) so that the SHUTDOWN's own Flush completes first; then a READ | the READ is held during the re-enable and completes with data, not `BUS_RESET`; stage 0xD6 shows the leftover Flush released at the RDY=0 acknowledgement; no parent completed `BUS_RESET` without a reset |
| Read and write with FUA (PLAN 1.6, durability) | `writethrough=off` (or `StorWriteThrough`=`off`), a drive with a volatile write cache; MODE SENSE reports DPOFUA; READ(10), READ(16), WRITE(10) and WRITE(16) with CDB byte 1 bit 3 set through pass-through, and a file written with `FILE_FLAG_WRITE_THROUGH` on NT; trace the NVMe Write commands; then a surprise removal before any Flush, re-arrival and verify | every NVMe Read and Write child of those requests has CDW12 bit 30 set (also when MDTS or `frag=` splits the request), requests without the bit have it clear; the FUA data verifies after re-arrival |
| Flush after a failure from running (PLAN 0.10 durability) | writes, then CSTS.CFS set by the harness before any Flush (the adapter ends in `failed`); then SYNCHRONIZE CACHE and `SRB_FUNCTION_FLUSH`; separately the same after a completed Flush | dirty: SYNCHRONIZE CACHE fails with HARDWARE ERROR 04/44/00 and the FLUSH with an error status; clean: both succeed |
| Windows 2000 two-SHUTDOWN sequence (PLAN 0.11, G8, R6-5) | Windows 2000 shutdown and restart, `CachesData` TRUE, with dirty data, CC.SHN actually written on each `SRB_FUNCTION_SHUTDOWN` (G8 measured the sequence only with a Flush) | trace: SHUTDOWN #1 gives Flush, CC.SHN, SHST complete; the reads, writes and SYNC between the two re-enable the controller first and complete normally; SHUTDOWN #2 gives Flush, CC.SHN, SHST complete; nothing follows; after the next boot every file written before shutdown verifies. Passed in the prototype (Q-NT item 3, Shut Down and Restart); rerun on product code |
| Suspend/resume (PLAN 0.11, G7) | 20 cycles of standby (and hibernate on NT, scenario A) with I/O between cycles. **S3 (standby) is not available on the current VMs** (XP x64: the std VGA driver vetoes it although the firmware reports S3; 2000: run with `acpi=off`), so hibernate is the tested transition until a VM with a standby-capable display driver (or Windows 7) is set up. Resume is lazy on 2000 and XP x64: `HwFindAdapter`/`HwInitialize` run at the first I/O after resume, so issue I/O to observe it | data verifies; no lost flush; no timer or interrupt after stop |
| Resume failure (PLAN 0.10 bring-up, 0.11, G1 + G7, R6-7) | harness: identify fails (or RDY never set) on resume, with the deferred bring-up and `ScsiRestartAdapter` not advertised (the combined G1 + G7 prototype first, then product code) | `HwInitialize` returned TRUE (never FALSE on resume, not even after an acknowledged disable); adapter in `failed` and keeps its memory until the next stop; later SRBs fast-fail; no hang of the kind G7 measured; `ScsiAdapterControlUnsuccessful` never returned. Passed in the combined prototype on 2000, XP x64 and 7 x64 (Q-NT item 2; 2000 keeps the volume mounted and failing, XP x64 and 7 x64 remove the disk after a rescan). `ArgumentString` is NULL at resume, so fault options for this case cannot come from DriverParameter: set them by QMP after POST (see "Windows 7 x64 notes") |
| Dump-mode reset (PLAN 4.2, G6) | boot from the tunnelled NVMe ("Boot from the tunnelled NVMe" above); crash dump and hibernate with RDY delayed 2 s at dump init; a write completion held (10 s, 90 s) during dump I/O | dump completes; the port's reset after the first INQUIRY and any mid-dump reset are disregarded (no controller reset) and the dump does not fail. Passed in the prototype on 2000 and XP x64 (G6) |
| Dump-mode lost completion (PLAN 4.2 pump deadline, G6) | as above, a write completion dropped during dump I/O | the dump fails cleanly through the engine's pump deadline (no hang at the bugcheck screen; with `AutoReboot` the machine reboots). The prototype, without a deadline, hung indefinitely on 2000 |
| Dump-mode allocation (PLAN 4.1, G6) | dump and hibernate on 2000 and XP x64 | uncached allocation under 32 KiB including alignment slack; queues and PRP pages aligned against the physical address; the port-supplied access range used |
| Boot-present | `x-force-tunnel=on` on the bridge | the OS boots and mounts the disk |
| Bridge cycle under Verifier (xhci98 report, 0.1.1.0 hang) | Windows 2000 SMP (2 vCPU, `tcg,thread=multi`), Driver Verifier on stor98.sys with flags 0x1B; inside the guest `tests/devcycle` disables and re-enables the tunnel bridge (`devcycle cycle DEV_0001 6 3000`, then 10 x 2 s) while the host takes the tunnel down (`surprise`) and up again 4 times 5 s apart, then 8 times 3 s apart | no freeze (the desktop answers, the tool finishes every step), no bugcheck or Verifier stop; every stop on absent hardware takes no waits (`STOP_SKIP`), every surprise removal enters `gone`; Verifier's load and unload counts match the cycles. Engine build, 2026-10-15: pass (16 loads, 15 unloads, no stop). 0.1.1.0 under the same stimulus: the guest froze within the first bridge cycle (both vCPUs spinning in the kernel; xhci98 saw CPU1 in `KeStallExecutionProcessor` called from 0.1.1.0's `HwInitialize` poll loop), and the next boot with the endpoint present at POST hung at "Starting up" |

### Removal acceptance

Uses the measurement definitions, build settings and workload above (PLAN "Removal
acceptance"). Scope: a non-critical data disk. Record the QEMU trace for each run; it shows
every NVMe command the driver issued.

| Case | How | Pass |
|---|---|---|
| Surprise removal, idle (PLAN 2.2) | `tunnel surprise`, then one I/O; on 9x, then the CM actor's CONFIGMG remove (Tcm, actor named in the record) | no hang or crash. NT: volume reported gone (T4) within 5 s of that I/O. 9x, standalone: the I/O fails to the application (IOS's retries end) within 5 s, with no CM involved; 9x, CM-assisted: drive letter gone (T4) within 5 s of Tcm (R6-9) |
| Sustained absence during I/O (PLAN 2.2, removal acceptance) | workload running, then `tunnel surprise` with the G3 notification (on an OS without qualified G3, no notification: the run relies on the MD-1 outcome, held SRBs completing at the gone deadline with the gone-deadline counter at 1, and the record says so); tunnel stays down for the 10-minute window; on 9x the CM actor's CONFIGMG remove follows at Tcm (actor and time recorded) | T0-T1 at most 2 s, T1-T2 at most 5 s, T0-T3 at most 60 s, T4 at most T0 + 60 s (9x: the standalone part T0-T3 judged on its own, and T4 at most Tcm + 5 s, R6-9); pending SRBs within the admission limit; recovery-loop criterion above (over the whole window: at most one bus reset and one timeout, both before T4, none after); no stall loop |
| Removal between RMW stages (PLAN 4 KiB tests, 0.10) | exact-point hooks: gate close after the pre-read, before the write-back, before `RequestComplete` | as sustained absence; after reconnect, blocks outside the request are unchanged |
| Removal stress | trace-following QMP removal at random points, many runs | no crash, hang beyond the thresholds or corruption outside the requests in flight; no exact-placement claim |
| Link down in a terminal state (9x CM, K1 of 0.3.0.0) | cm9x `FailRemove 20`, then `cm9xctl eject - 0` with the harness rule `at=stage code=0x1cf act=surprise`: the eject is acknowledged, Remove_SubTree keeps failing, after 30 s (R11 O6) the CM tears the tunnel down and publishes DOWN_QUIESCED to the removal's terminal instance | Stor98 logs 0x124 then 0x125 (written 1); the CM's 0x1CC shows the acknowledgement (1) within a few hundred ms, not its 3 s timeout; the devnode is then retired. Run 2026-10-16 on Me (fix-0.3.0.1, dbgport build): acknowledged after 172 ms |
| Locator without proof (9x, xhci98's 9x CM 3.0.0.0) | cm9x `NoProof 1` (reboot), two ports: tb0 up, surprise, tb1 up, tb0 up | every load starts (0x122 rc 0x0A, no link-state channel); one boot block: 0x128 tokens continue across ports and 0x127 charges accumulate; link-down waits end at the CM's timeout (no AckGeneration), the gone state handles the removal. Run 2026-10-16 on Me (fix-0.3.0.1 plus temporary diagnostic stages, since removed): tokens 1, 2, 3, charged 0, 0x17, 0x2E pages before each load |
| Reconnect, same disk (PLAN 2.6, removal acceptance) | separate run: removal during I/O, then `tunnel up` (plus the OS rescan or xhci98's CM) | disk usable within 60 s of `tunnel up` as the same LUN; data verifies; first CC/CSTS read after reopen shows a fresh controller (see "Reopen, fresh endpoint") |
| Reconnect, new disk (PLAN 2.6, R3-14) | reconnect with the identity-change fixture (different serial, NGUID and EUI64) | old disk reported gone and the new one arrives as a new disk; no request admitted for the old disk reaches the new one. A swap that changes only the contents (same serial, NGUID, EUI64) is undetectable by design and is not a test |
