# Phase 40 Record

The detail behind `docs/contributing/roadmap-usb4.md`, "Phase 40" (the USB4
software connection manager). The roadmap entry carries the goal, the
status, the task list and the checkpoint; this file carries what each task
did and what each reading said. Where the two disagree about a clause, the
roadmap wins. The design is `design/22-usb4-software-connection-manager.md`
(record 22).

**On `out\...` and `vm\...` paths in this file.** They say where a reading
was kept on the host that took it; they are not files a clone has.

## 40-0.3 - The launcher reading, and the harness for leg U.4

**Verdict: done** (2026-10-15). Every property record 22 section 14.2 passes
as `-global` reaches its QEMU property through task 39-0.7's launcher, read
back in `info qtree`; the launcher gained `-BaseImage` and `-RunRoot` so the
98 SE legs can run from an overlay of the image that carries the host
controller driver; and the `usb4-generic` profile, on both host models with
the pre-OS connection manager, was read on Windows 98 SE and Windows 2000.

**The host.** Development host A, TCG, under the admission rule of the
Phase 39 brief (out of `out\vmlocks`, the sum of `-smp` within the logical
processor count, 6 GB of RAM left free). Two guests of another worker were
running throughout and were left alone. Every guest here ran with one vCPU
(the machine lines of the launcher: `pentium3,-apic`), QMP and VNC on 47904
and 47905 (98 SE) and 47924 and 47925 (2000). Evidence (traces, `info pci`,
`info qtree`, screendumps) is in `vm\p40-topo\<run>\`.

**The emulator.** `usb4-emu` read, never written. Its `main` is `3c74189`
(the GPL-2.0-only relicence); committed documents were read with
`git show main:<path>` (`docs/XHCI98-HANDOFF.md`, `docs/HARNESS.md`) and the
property definitions from `main:src/patches/0001-usb4-emu.patch`
(`usb4_host_properties`, `usb4_device_properties`, `hw/usb4/usb4-usb3.c`,
`hw/usb4/usb4-harness.c`). The binary the launcher copied to `vm\usb4\bin\`
is the working tree's build of 2026-10-15 01:42, `QEMU emulator version
11.1.2 (v11.1.2-123-g178237c)`, SHA-256
`92CEE077E409BE917441F240659A421219CF94FDCF451466402B3E3D32686D7E`; that
tree (`m0-bootstrap` at `9d09cae`) is five commits ahead of `main` (a Titan
Ridge firmware-CM variant and documents), none of which touches the
properties or the harness read here.

### Whether `-global` reaches each property

`-device usb4-host,help` and `-device usb4-device,help` on that binary list
all five: `host-model=<str>`, `preos-cm=<bool>`, `usb3-xhci=<str>`,
`usb3-port1=<uint32>` on `usb4-host`, `usb3-dev=<str>` on `usb4-device`.
They are ordinary qdev properties, and `-global <type>.<property>=` sets the
default of a qdev property for every instance of that type created later,
so nothing in the launcher stands in the way: `-Extra` accepts `-global`
with a value that names no back end, file or path, and none of the five
does. The reading (run `p40t-probe`, `-S`, no device router at power-on):

```
powershell -Command "& scripts\usb4\run-usb4-guest.ps1 -Guest win98
    -BaseImage vm\t26\win98-gold.qcow2 -RunRoot p40-topo -Name p40t-probe
    -Usb4Profile generic -Slot 2 -DeviceAtBoot 0 -Tag probe -Extra '-S',
    '-global','usb4-host.host-model=intel-adl','-global','usb4-host.preos-cm=on',
    '-global','usb4-host.usb3-xhci=u4x','-global','usb4-host.usb3-port1=1',
    '-global','usb4-device.usb3-dev=ustor0',
    '-device','qemu-xhci,id=u4x,bus=pci.0,addr=0x7'"
usb4-qmp.ps1 -Name p40t-probe -RunRoot p40-topo hmp device_add usb4-device,id=enc0,bus=u4.0,port=1,drive=nvme0,serial=USB4EMU01
usb4-qmp.ps1 -Name p40t-probe -RunRoot p40-topo hmp info qtree
```

| Property | `info qtree` | Reaches |
|---|---|---|
| `usb4-host.host-model` | `host-model = "intel-adl"` on `u4` | yes |
| `usb4-host.preos-cm` | `preos-cm = true` | yes |
| `usb4-host.usb3-xhci` | `usb3-xhci = "u4x"` | yes |
| `usb4-host.usb3-port1` | `usb3-port1 = 1 (0x1)` | yes |
| `usb4-device.usb3-dev` | `usb3-dev = "ustor0"` on `enc0`, a router hot-added after start with no `usb3-dev` on its line | yes, hot-added routers included |

That the values act, and not only read back, shows in the boots below: with
the `-global` forms alone the host realises as `usb4_fabric_new host router
... vendor 0x8087 product 0x463e` with `total-paths 12` (`intel-adl`) and the
trace opens with `usb4_preos_start` (`preos-cm`).

Four things to know when using them:

- **An explicit value wins.** A value on the `-device` line beats a
  `-global`, so `-HostModel intel-adl` or `-PreosCm` (which the launcher
  writes on the `-device` line) cannot be undone by a `-global`, and the
  `-Usb3` switch's own `usb3-dev=ustor0` on `enc0` beats a
  `-global usb4-device.usb3-dev`. The launcher's `-HostModel`, `-PreosCm` and
  `-Usb3` remain the plain way; the `-global` forms are for what the
  launcher does not build (a router added by hand, a second router).
- **`host-model` and `preos-cm` belong to `usb4-generic`.** The model refuses
  `host-model=intel-adl` on `tb3-alpine-ridge` at realize and silently
  ignores `preos-cm` there (`cfg.preos_cm = s->preos_cm && profile ==
  usb4-generic`). The launcher now refuses either `-global` with a profile
  other than `generic`, as it already refused `-HostModel` and `-PreosCm`.
- **`usb3-xhci` and `usb3-port1` only shape ACPI.** In `usb4-usb3.c` they are
  read at ACPI table build time, for the `_DSD` that ties a SuperSpeed root
  port of that `qemu-xhci` to the host interface, and only when the host and
  the xHCI are both on the root bus. Attaching and detaching the USB device
  when the USB3 tunnel goes up or down is driven by the router's `usb3-dev`
  list alone (a device id lookup at each transition). So on these guests
  (98 SE reads no `_DSD`; the 2000 xHCI-only image runs `acpi=off`) leg U.8
  depends on `usb3-dev`, and the two host properties are inert for the
  guest; the launcher's `-Usb3` builds all three.
- **`-global` on `-Extra` order.** `-Extra` lines follow the launcher's own
  `-device` lines on the command line; QEMU applies `-global` values before
  any device is created, so the order does not matter.

### The launcher change

`scripts\usb4\run-usb4-guest.ps1` builds every overlay over
`vm\<guest>.img` under `vm\usb4\`. The 98 SE legs need an overlay of
`vm\t26\win98-gold.qcow2` instead (`vm\win98.img` carries the `1.x` miniport
under NUSB's usbport; the gold image carries the host controller driver and
its `AUTOEXEC.BAT` copies `D:\XHCI98.SYS` over the installed one), so the
launcher gained two parameters, and `usb4-qmp.ps1` the second:

- `-BaseImage <qcow2>`: another base under the main checkout's `vm\` (a
  relative path is taken from the main checkout), only ever a backing file;
  the guest's machine line is kept. It cannot sit in the run directory.
- `-RunRoot <dir>`: the directory under `vm\` that holds the run directory
  (default `usb4`, as before), so a worker's overlays sit in its own
  `vm\<branch>\`. A writable `-XferImg` must then be under that directory.
  `run.json` records `runRoot` and `base`; `usb4-qmp.ps1 -RunRoot` finds it.

No property needed a fix: all five reached QEMU unchanged.

### The `usb4-generic` profile with the pre-OS connection manager, both host models

```
run-usb4-guest.ps1 -Guest win98 -BaseImage vm\t26\win98-gold.qcow2 -RunRoot p40-topo
    -Name p40t-98-min -Usb4Profile generic -Slot 2 -BootStrict -Trace -Tag r1
    -Extra '-global','usb4-host.preos-cm=on'
run-usb4-guest.ps1 -Guest win2k-xonly -RunRoot p40-topo -Name p40t-2k-min
    -Usb4Profile generic -Slot 2 -BootStrict -Trace -Tag r1
    -Extra '-global','usb4-host.preos-cm=on'
```

and `p40t-98-adl`, `p40t-2k-adl` the same with
`'-global','usb4-host.host-model=intel-adl'` added. The device router `enc0`
with its NVMe was attached at power-on; each guest booted from a fresh
overlay, its wizards were driven over QMP keys and read from screendumps.

**What the emulator did** (the `usb4_*` trace, the same on both guests per
host model):

| | `minimal` | `intel-adl` |
|---|---|---|
| Host router | vendor `46F4`, product `4040`, 16 paths | vendor `8087`, product `463E`, 12 paths |
| Pre-OS CM | router `0x1` enumerated, bonded x2, Configuration Valid; PCIe path host adapter 6 to router `0x1` adapter 3, HopID 8 | the same through host adapter 8 (PCIe Down 8) |
| Tunnel | `usb4_tunnel_transition router 0x1 PCIe tunnel up reason 0`; `usb4_tp_insert tb0: insert endpoint gen 1 drive=nvme0 serial=USB4EMU01 cold=1`, before POST | the same |
| At the machine reset | the pre-OS CM runs again over the live tunnel (no second transition), then "host interface reset, 1 hot plug events dropped": nothing pending for the OS | the same |
| After boot | no further `usb4_*` line: no guest touched the host (no driver), the tunnel stayed up to the quit | the same |

**What the guests saw** (`info pci` after boot, both host models alike):

| Function | Location | Ids | 98 SE (gold image) | 2000 SP4 (`win2k-xonly`) |
|---|---|---|---|---|
| USB4 host interface | 00:04.0 | `1B36:00F4`, subsystem `1AF4:1100`, class `0C0340`, BAR0 256 KB | Add New Hardware: "PCI Universal Serial Bus", no driver (cancelled) | Found New Hardware: "Universal Serial Bus (USB) Controller", no driver (cancelled) |
| Tunnel bridge | 00:05.0 | `1B36:0001`, secondary bus 1 | "PCI standard PCI-to-PCI bridge" from `MACHINE2.INF`, installed through the wizard | installed silently |
| NVMe behind the pre-OS tunnel | 01:01.0 | `1B36:0010`, class `010802` | "PCI Card", no driver (cancelled) | "PCI Device", no driver (cancelled) |

So **the pre-OS tunnel appears to both guests from boot on both host
models**: the NVMe is enumerated behind the tunnel bridge by the BIOS and by
each OS with no connection manager in the guest, which is the starting
state record 22 section 9.2 (discover and preserve) is written for. The
NVMe at POST did not stop either boot (`-BootStrict` was carried as in
39-0.7). After the wizards were cancelled, 98 SE's configuration manager
left the host's and the NVMe's BARs unassigned (`info pci`: not mapped);
2000 kept the host's BAR at `0xFEA00000` and left the NVMe's unassigned.
The gold image also shows one "Unknown Device" (Code 28) at the root of the
device tree, under no PCI bus; it was not identified in this reading. The
host router's own ids (`46F4:4040`, `8087:463E`) are fabric-side only: a
guest sees them through the connection manager's configuration reads, not
in PCI.

### The fault-injection harness and leg U.4

`docs/HARNESS.md` (main `3c74189`) puts every control on the tunnel bridge
(`tb0` here), set at run time with `qom-set` (`usb4-qmp.ps1 harness <x-prop>
<value>`): fault rules in `x-faults`, held-command release in `x-release`,
gate overrides `x-gate`, controller timing faults (`x-rdy-set-delay-ms`,
`x-rdy-clear-delay-ms`, `x-shst-delay-ms`, `x-cfs`), a hung controller
`x-hang`, lost interrupts `x-intx-drop-*`, a marker doorbell, the read-back
`x-harness-state`, and a debug port (`-device usb4-debug-port`, I/O 0x5A0,
which `-Extra '-device','usb4-debug-port'` passes) whose stage writes fire
`at=stage` rules before the guest's write returns. Every firing traces
`usb4_h_fire` and raises QMP `USB4_HARNESS`; the gate and the endpoint
cutoff trace as `usb4_tp_gate` and `usb4_tp_cutoff`.

**What it acts on.** The NVMe endpoint behind the bridge and the bridge's
access gate, nothing else. A rule's `surprise`, `eject` or `admin-down`
takes the tunnel slot down (stage 1 in the call, stages 2 to 4 from the
bottom half), but it passes no device router: the router stays attached,
its paths stay enabled and the connection manager receives no hot-plug
event. So a harness tunnel-down is a link lost behind a live path, which
the connection manager cannot see, not an unplug; the unplug of U.4 is
still `device_del enc0` (`usb4-qmp.ps1 unplug`), which detaches the router,
raises the hot-plug event and takes the tunnel down as a surprise.

Which injection gives each U.4 case:

| U.4 case | Injection | What it pins |
|---|---|---|
| Unplug during a large copy, uncontrolled point | the copy running, then `unplug` | the plain leg; where the unplug lands in the I/O stream is whatever it is |
| Unplug with a read in flight | `x-faults "at=fetch op=read act=hold tag=u4"` (or `nth=N`), `x-harness-state` polled until it lists the hold, then `unplug` | a read fetched and not executed is outstanding when the router leaves; the hold makes the state exact, the unplug's arrival is event-following |
| Unplug with a write's completion withheld | `x-faults "at=complete op=write act=hold tag=u4"`, then `unplug` | data transferred, no CQE and no interrupt when the router leaves |
| A held command at the cutoff | either hold above, left held through the `unplug` | the endpoint's reset at the cutoff discards it (HARNESS.md: a reset discards held commands), so the I/O never completes and nvme98's deadline or the notification must end it |
| Link lost at an exact NVMe command, no unplug | `x-faults "at=fetch op=read nth=N act=surprise"` | the tunnel-down without a router change: this driver must not tear down a path the fabric still reports up; nvme98 sees all-ones |
| Presence lost with DMA still open | `x-gate=reads-fail` (or `act=reads-fail`), later `x-gate=normal` | the window before a path is closed (nvme98's R4-1), with this driver idle |
| Late completion past nvme98's deadline | `x-faults "at=complete op=read nth=N act=hold delay=6000"`, then `unplug` | nvme98's MD-1 path when no qualified notification exists |
| G3 qualification of the link-down notification | `-trace usb4_tp_gate -trace usb4_tp_cutoff -trace usb4_dbg_marker`, the notification marked through the debug port | the notification's marker later than `usb4_tp_cutoff` (HARNESS.md "Measurement events") |
| An exact point in this driver's own teardown | `at=stage code=<c> act=...` on a stage code this driver writes to the debug port | not available until this driver's `qemu` flavour writes stage codes (below) |

**What is not there.** Nothing in the harness acts on the fabric: no rule
drops or delays a control packet, fails a router operation, withholds a
hot-plug event or detaches a router at an exact point. The connection
manager's own error paths (a router operation timing out, an unplug between
two Path Enable writes) therefore cannot be pinned by the harness; on this
emulator they are reached only by timing a `device_del` (event-following).
Leg U.4 as record 22 states it (the driver does not hang, the teardown
read-back, the notification qualified or MD-1) is fully drivable; the
exact-point teardown variants need either the stage codes below or a
fabric-side rule in `usb4-emu`.

### Owed

- **Stage codes and markers from this driver.** For the exact-point rows
  and for G3, the `qemu` flavour of the connection manager has to write
  stage codes and its TSC markers to the debug port (0x5A0) at its teardown
  steps (path close, the endpoint reset seen, the notification to nvme98),
  with a code range agreed with nvme98's debug build so the two do not
  collide. Not in any task yet; a 40-V.1 prerequisite for U.4's exact
  variants.
- **A fabric-side harness rule** (a router detach, a dropped or delayed
  control packet, a failed router operation, at `at=stage`), if the exact
  connection-manager error paths are to be read on the emulator: an
  `usb4-emu` request, not raised here.
- The "Unknown Device" (Code 28) on the 98 SE gold image is not identified.
- No leg here installed a connection manager; U.1 to U.13 are 40-V.1 and
  40-V.2's.

## 40-A.5 - The pure core against usb4-emu's fabric model

**Verdict: a reading, recorded** (2026-10-15). Not a gate (record 22
section 14.1, decision U11): the host suite `test_u4cm` over this
project's own fake fabric is the gate; this run checks the same core
against the emulator's own router, adapter and path model.

**The vehicle.** Development host A. `scripts\usb4-crosscheck\` builds the
pure core (`src\cm\tbt_ring.c`, `tbt_hw.c`, `tbt_icm.c`, `tbt_u4cm.c`)
strictly as C89 (`gcc -std=c89 -pedantic -Werror`) and links it, through a
plain-C glue (`u4cm-glue.c`, so the DDK-style types never meet the Windows
headers QEMU's osdep pulls in), with usb4-emu's fabric model -
`src/qemu/hw/usb4/` `usb4-ctl.c`, `usb4-fabric.c`, `usb4-router.c`,
`usb4-router-tb3.c`, `usb4-router-adl.c`, `usb4-preos.c`, `usb4-drom.c`,
`usb4-tunnel.c`, `usb4-fabric-usb3.c` - as usb4-emu's
`tools/usb4cm/host/run-host-test.sh` does for `usb4cm`, with usb4-emu's
MSYS2 UCRT64 gcc 16.2 and glib. usb4-emu read in place, never modified:
`main` checkout at `9d09cae` (its handoff relicence commit `3c74189` is an
ancestor), QEMU fork at `178237c`. Everything built went to
`out\u4-crosscheck\`. The run:

```sh
<usb4-emu>/scripts/msys2-shell.sh "sh <xhci98>/scripts/usb4-crosscheck/run-crosscheck.sh"
```

**The cases** (record 22 section 14.2's U.1, U.2, U.3, U.5, U.6 and U.9,
on the model instead of a guest): a device attached before the start on
port 1, a hot plug on port 2 with USB3 tunnels, its unplug (surprise
removal in the model) and re-plug, on both host models; the model's pre-OS
connection manager (`preos_cm`) on both host models; the driver's stop and
a second instance over the configured fabric; nothing attached.

**What it said** (xhci98 at `20442cd5`; the log is
`out\u4-crosscheck\run.log`): 32449 checks, 0 failures - the model's own
CRC and frame checks on every packet included. The same run at the core's
first complete version (`452151c4` with its first corrections) read 30601
checks, 0 failures. Read again at `167f43f8` against a newer usb4-emu
checkout (`main` `fdcca32`, QEMU fork `53f8fc7`, which adds the
`asm4242` host model in `usb4-router-asm.c`; the script links it when
present): 32449 checks, 0 failures.

**The `asm4242` host model** (usb4-emu `main` `ce13e0e`, a descendant of
the `a50c19e` the handoff names; its `docs/USB4-HOSTS.md` section 5: the
ASMedia ASM4242 of AM5 X870 boards, router `174c:2428`, from public
sources only, nothing captured, with its three quirks on). The CSM
desktop case where this connection manager is the only one. The same
cases on it, plus the run before: 45469 checks, 0 failures.

- `asm4242`: PCIe Down 8 and USB3 Down 10 found on port 1 by discovery
  (lanes 1-4, DP IN 5-6, Host Interface 7, PCIe Down 8-9, USB3 Down
  10-11), both tunnels up and bonded over lanes reporting 174 Total
  Buffers (the path credits taken from Buffer Allocation, never the lane
  figure, and clamped to the 7-bit field in any case); hot plug, unplug
  with the path proven closed, re-plug; the pre-OS tunnel preserved with
  no transition. 897 control requests, 0 retries, 0 timeouts. The Host
  Interface Pending Packets quirk is never met: this connection manager
  never invalidates a Host Interface path. The host suite's fake fabric
  has the same preset (`fabHostAsm4242`, written from that document).
- Linux's quirk table (`quirks.c`, at the pinned revision) has no
  ASMedia row; its only ASMedia code is the NVM version read (`nvm.c`),
  which this connection manager does not use. The three quirks come from
  mailing-list patches, not merged; their behaviours are met by the
  design itself (bounded drains with quarantine, ring 0 only without
  end-to-end flow control, credits clamped).

- `minimal`: port 1 UP at start, PCIe at lane HopIDs 8 and 9 and USB3 at
  10 and 11 (`u4.pcie.up 00010809`, `u4.usb3.up 00010a0b`), bonded, the
  model's PCIe tunnel UP once and its USB3 tunnel UP; port 2 hot plugged
  UP; its unplug torn down with the path proven closed (the link-down
  upcall's `PathClosed`); re-plugged UP. 792 control requests, 0 retries,
  0 timeouts, 10 hot plug events.
- `intel-adl`: the same, with PCIe Down 8 paired with port 1, adapters 10
  and 11 absent (`ERR_ADDR`), the `8087:463E` USB3 bandwidth quirk firing,
  the DP IN plug events acknowledged; 825 requests, 0 retries, 0 timeouts.
- `preos_cm=on`, both models: port 1 preserved at HopIDs 8 and 9
  (`u4.preserved 00010809`), no tunnel transition at the driver's start
  (no flap), no Host Interface Reset and no Host Router Reset; still up
  across idle ticks; its unplug EMPTY.
- Stop and restart: the second instance preserved the first's tunnel with
  no transition.

**One shim difference found.** `usb4cm`'s host test shim
(`tools/usb4cm/host/cm-host-test.c` `shim_rx_flush`) writes the receive
descriptor back with bit 22 set; Spec Table 12-8 makes bit 22 of the
write-back Buffer Overflow, and usb4-emu's own host interface
(`hw/usb4/usb4-host.c`, `DESC_BO`) sets it only on an overflow. This
project's ring code honours Buffer Overflow, so with the copied shim every
answer read as an overflow and the first run timed out at the host router.
The cross-check's shim writes the write-back as `usb4-host.c` does. The
emulator itself is right; `usb4cm`'s shim is not, and `usb4cm` never reads
the bit. No request filed: nothing in the emulator needs to change.

**Also found.** usb4-emu's fabric answers a router-space Write Request with
the arrival adapter in the response's Adapter Num (`usb4-fabric.c`
`send_response`), where Spec Table 6-5 asks for the request's value echoed;
`tbt_ring.c` therefore compares the Adapter Num only outside the router
space (route, address, size, space and sequence number still compared
there).

## 40-A.7 - The `nvme98` statement under the USB4 profile

**Verdict: built and host-tested; no guest leg read yet** (2026-10-15).
The channel of task 39-A.7 (run-39.md, "39-A.7"; record 21 section 7.2)
is wired to the USB4 driver half: `cmU4Activate` registers
`CmNvlinkU4Up` and `CmNvlinkU4Down` through `CmNhiSetU4Hooks`; the UP hook
publishes the new establishment's `UP` before anything can expose the
endpoint (its member by correspondence, or every exclusive member when
the correspondence is not known); the DOWN hook makes the statement only
with `TBT_U4_LINKDOWN.PathClosed`, the port's member known, that member
the emulator's bridge (1B36h above it, SHPC, no PCI Express capability),
an admitted operating system (2000 x86, XP x64), the slot read empty
within what is left of the 3 s, and at least 100 ms left; the seam waits
for the acknowledgement before the removal. `CmNvlinkService` gives the
core one presence sample per port each pass (the anchor of record 22
section 10.3). The conditions are `test_cm_nvlink`'s
(`CmNvlinkU4Verdict`, every branch).

**Owed:** a Windows 2000 guest on the `usb4-generic` profile with the
software connection manager admitted and `nvme98`'s G3 prototype bound
(the leg of run-39.md's 39-A.7 table, on the USB4 profile): the anchor's
age at the statement, the statement after the cutoff, the
acknowledgement before the invalidation, the 5 s bound with an idle
receiver; and the 9x half once `nvme98` has one.
