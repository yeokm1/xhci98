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
the emulator's bridge (1B36h on the bridge itself or on the bus-0 bridge
whose range holds it, SHPC, no PCI Express capability),
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

## 40.5 onward - The install, upgrade and uninstall tool (INSTALL.BAT, XHCIINST, XHCIIN64)

**Verdict: done** (2026-10-15): the legs below pass on the tool as it
stands, with the 98 SE re-reads at the end. Design record 23 is the design; this section
is what the legs read. Development host A, TCG, one vCPU per guest unless
said, the p40-inst kit (`out\p40-inst\kit`, QEMU `-name "p40i ..."`),
overlays under `vm\t40i\` (some early ones under `vm\p40-inst-c\`, moved
there from another drive), evidence and per-leg notes in
`out\p40-inst\legs\<guest>\notes.txt` with the console logs
(`C:\XHCIINST.LOG` copied out) and screenshots beside them.

**The packages.** `pkg24`: the published 2.4.0.0 drivers with the tool at
2.4.0.0. `pkg30`, `pkg30c`, `pkg30d`: this branch restamped 3.0.0.0 in a
throwaway worktree (the stamp is never committed), x64 unsigned (F8 at
every start on Vista x64 and 7 x64). Each leg's notes name the tool's
commit and the SHA-256 of the `XHCIINST.EXE` it staged; the tool changed
during the day, and each defect below was read on the tool named with it.

**The legs.** F: a first install from `pkg24` (then the population: a
mouse, a stick, a UAS disk). R: the registry questions. U24: an update
from 2.4.0.0 to 3.0.0.0. UN: `/uninstall`, the checks after the restart,
then a reinstall. U23: an update over the 2.3.1.0 baselines of Phase 38
(`vm\t38mx\mtx-<g>-u231.qcow2`, read-only backing) with their population
(a hub with a keyboard, sticks and audio behind it). NC: a run on a
current machine. L: the 32-bit tool run alone or on the wrong directory.

| Guest | Leg | Result |
|---|---|---|
| 98 SE | F | pass: copy + `DevicePath`, exit 4 with the steps; after the restart the wizard named the controller and found the INF by itself; the "xhci98 xHCI Host Controller Disk" prompt took the printed directory; the CD for `usbd.sys` |
| 98 SE | R | pass: only `XhciInterruptMode`, `XhciSilentInstall` and `XhciForceBulkOnly` asked; the answer written to the controller's key, none to the root hub's |
| 98 SE | U24 | pass: Update Driver's search found the new copy by itself; driver date and binary the package's |
| 98 SE | U23 | pass: the controller updated by the printed steps, the new `USB\XHCI98_*` devnodes installed, the one line naming `tools\xhcicln\`, no `XHCICLN` run |
| 98 SE | silent install | pass: `XhciSilentInstall` 1 merged `SILENT.REG`'s list and set the switch; a mouse plugged in after the restart got no wizard (one CD prompt for `hidusb.sys`, the documented caveat) |
| 98 SE | UN | uninstall as designed; the reinstall after it failed on the tool of that hour (the local copy held Windows' `xhci98.PNF`, so the tool kept it and then could not remove it, exit 2); fixed (the removal takes the device installer's `.PNF`), re-read below |
| ME | F, U24 | pass: ME installed the controller with no prompt at all; U24 by Update Driver's automatic search |
| 2000 | F, R, U24, UN | pass (F with no Windows prompt; UN left no INF, service, file or local copy of the package, `usbd.sys` and `usbui.dll` kept; the reinstall installed) |
| 2000 | U23 | first run pass (07:38); a later run (U23b, tool 3164e913) failed: the root hub install met Windows' own Found New Hardware install of it (`0x20`), the hub ended at Code 28 and the keyboard behind it stopped; a second run installed it. Fixed by the settle of record 23 section 5.8 |
| 2000 | U23c, U23d (tool e5b6b42d) | pass, twice: after the controller's update the tool waited ("devices moved", settled after 18 and 17 seconds) while Windows installed the root hub silently; no wizard, no busy retry; every device 3.0.0.0 started, the USB keyboard typed; "Changes were made. Please restart Windows now.", exit 3 |
| 2000 | NC (tool e5b6b42d) | pass: `/defaults /yes` on a current machine: each INF "staged already", "No changes were made.", exit 0; a settings-only `/set` asked "Write the changed settings now", exit 3; `/defaults /yes` again exit 0 |
| 2000 | XHCICLN | pass: the banner reads 3.0.0.0 (an earlier test package carried the 2.4.0.0 binary; the release script checks the version) |
| 2000 SMP | F, U24 | pass (over a base carrying the 1.0.0.x miniport, updated in place) |
| XP | F, U24, UN | pass: the root hub's Found New Hardware wizard made the first run stop with exit 4 as designed, the second finish |
| XP x64 | F, U24, L | pass |
| Vista x86 | F, L, U24 | pass (U24 failed once on a package whose filter INF lacked a catalog line for the store; the package was fixed and U24 re-read) |
| Vista x64 | F, U24 | pass (U24's run after the restart not read) |
| 7 x86 | F, U24, UN | pass |
| 7 x64 | F, UN (over 2.4.0.0), U24, U23 | pass; the x64 note cited a section name the package's `readme.txt` does not carry and called an unsigned package test-signed: fixed |

**What the legs changed in the tool** (each in record 23): the settle
beside Windows' own installer and the retry of a call that meets it
(section 5.8); the last line and the exit code agreeing, and no restart
request beside "run INSTALL.BAT again" (section 7.4); an unchanged rerun
saying "No changes were made." (sections 5.6 and 8.4); the confirmation
naming what will happen; the results never printing "started" beside a
problem code; the x64 note reading the drivers' signature (section 5.10);
on 98 SE and ME the root hub's own update step, the 2.3.x warning only
over 2.3.x on 98 SE, the `.PNF` beside a local copy, an old slot's entry
kept as a step but out of the confirmation's wording, the UAS disk's disk
prompt and ME's "Select Other Driver" list in the steps (sections 4.4,
6.3, 6.4); `/yes` asking nothing (section 7.1); and on Windows 2000 a
first filter install's Code 12 waiting for the restart the run asks for
(section 5.9, owner).

**The 98 SE re-reads on the fixed tool** (tool 62d7b22f, `pkg30d`). NC9,
on the U23 overlay: the old slot's `DEV_04` entry listed with its
guidance; the confirmation "Check the local copy and DevicePath now (see
the entry listed apart above)"; "No changes were made.", exit 4 (that
entry is a step, by design); `/set XhciForceBulkOnly=1 /yes` and back to 0
asked nothing and wrote the controller's key ("Changes were made, but not
everything is done ...", exit 4); `/defaults /yes` "No changes were
made.". UN9, on the UN overlay: the install copied and staged normally;
after the wizard and the restart a second run changed nothing;
`/uninstall` restored `DevicePath` and removed every local copy - the one
holding Windows' `xhci98.PNF` included - and `%windir%\XHCI98`; after the
printed steps `USBD.SYS` and `USBUI.DLL` were present; the reinstall after
it copied and staged with no refusal. Pass. A copy of an earlier package
stays beside the new one until the uninstall (record 23 decision D7).

### Owed

- Vista x64: U24's run after the restart; 7 x64: UN over 3.0.0.0.
- The filter install on Windows 2000 through the tool (the upstream
  port's Code 12 now answered with the restart): not re-read on the
  Thunderbolt vehicle with this tool.

## 40-A.6, 40-A.8 and 40-A.9 - The driver half, read on the emulator

**Verdict: the USB4 profile runs end to end on Windows 98 SE and Windows
2000** (2026-10-15), on `usb4-generic` with `host-model=minimal` and
`intel-adl`, a cold device, hot plug, unplug and re-plug, the pre-OS
connection manager's tunnel preserved, a USB3 tunnel, the policy values,
and on 2000 the safe eject tearing the tunnel down before "Safe To Remove
Hardware". One fault was found outside this task's code: on Windows 98 SE
the re-enumeration seam's CONFIGMG call blocks the connection-manager
thread while an Add New Hardware wizard is open on a tunnelled device
(below).

**The vehicle.** Development host A, TCG, the `usb4-emu` fork in
`vm\usb4\bin` (the `a50c19e` build), through task 39-0.7's launcher with
`-RunRoot p40-drv`, `-BootStrict` and `-trace usb4_*`; QMP and VNC on
47984 to 47989. 98 SE from overlays of `vm\t26\win98-gold.qcow2`
(`u98-*`), 2000 from an overlay of `vm\win2k-xonly.img` (`u2k-min`), one
vCPU each. The `qemu` flavour of `xhcicm.sys` from this branch, staged with
`xhcicm.inf` on the transfer drive (D: on 98 SE, E: on 2000) in a copy that
writes `XhciLogVerbosity` 2 (and, for leg U.10, the policy values); its
port-0xE9 lines are the "log" below. The binary was the integration of this
branch with task 39-A.5's `cm_reenum.c` (the trial merge `p40-drv-legs`,
then this branch after it merged `3.0.0.0` at `c2c9ca3d`), except where a
leg names the earlier stub build. Driven over QMP keys and screendumps.

**Install.** 98 SE: the tunnel bridge through 98 SE's wizard
(`MACHINE2.INF`), then the host interface: Add New Hardware, "Specify a
location" D:\, "xHCI98 USB4 Host Interface" from `xhcicm.inf` by its class
line `PCI\CC_0C0340`, no restart. 2000: `devq update E:\xhcicm.inf
"PCI\VEN_1B36&DEV_00F4&CC_0C0340"` (OK, `reboot=0`; Windows 2000's
`UpdateDriverForPlugAndPlayDevices` finds the device only by a hardware id,
and `PCI\CC_0C0340` is a compatible one, so the class line is reached
through the `&CC_0C0340` hardware id), then the filter `devq update
E:\nt\xhcicmf.inf "PCI\VEN_1B36&DEV_0001&CC_0604"` (OK, `reboot=0`), and
for the NVMe `nvme98` 0.1.1.0 (its published x86 build, read-only) by
`devq update ... "PCI\VEN_1B36&DEV_0010&CC_010802"` after the first tunnel.

**The selection** (log, every guest): `cm.select.outcome=00000004`
(SOFTWARE), `u4.select.route=00000001` (no firmware CM by identity),
`cm.select.caps=00000010`; on `minimal` the host router `46F4:4040` with
two ports (lanes 1-2 and 3-4, PCIe Down 6 and 7); on `intel-adl`
`8087:463E`, PCIe Down 8 and 9, `u4.quirk=463E0001` (the USB3 bandwidth
row). On both models the mapping operation answered: port 1 named
`00:05.0` (`u4.named=01002802`, KNOWN: the tunnel bridge was already the
set's member by its SHPC source) and port 2 a location with no bridge
(`02000004`, NO_BRIDGE; these guests carry one tunnel bridge).

| Leg | Guest, setup | What was read |
|---|---|---|
| U.1 cold device | 98 SE, `minimal`, device at power-on, no pre-OS CM (`u98-leg`) | trace: ring 0 disabled then programmed (no Host Interface Reset), host router enumerated, mapping (`30h`), Get Capabilities (`31h`), Buffer Allocation (`33h`) on the host and the device router, DROM Read (`24h`), bonding (lane 1's hot plug event acknowledged), TMU posting, Configuration Valid, the four PCIe entries, Path Enable both ends, `PCIe tunnel up`, and a USB3 tunnel beside it; log `u4.pcie.up=00010809`, `u4.usb3.up=00010A0B`, `u4.action=01010001`, `cm.reenum.9x.reenumerate=00002800` (CR_SUCCESS); Add New Hardware for "PCI Card" (the NVMe) with no refresh |
| U.3 unplug, re-plug | the same guest | unplug: `u4.linkdown=01000109` (path proven closed), `u4.action=02010001`, the bridge re-enumerated, both host-side tunnels invalidated and the USB3 bandwidth released in the trace; re-plug: generation 2, `PCIe tunnel up`, "PCI Card" again |
| U.2 hot plug | 2000, `minimal`, nothing at power-on, filter installed (`u2k-min`) | `device_add enc0`: the tunnels built, `u4.action=01010001`, the filter's invalidation, Found New Hardware for the NVMe at once, `nvme98` bound, the "Unplug or Eject Hardware" icon in the tray |
| 40-A.9 safe eject | the same guest | the NVMe listed ("NVM Express Controller (nvme98) at PCI Slot 4"); Stop, OK: `cm.reenum.eject=00000028`, `u4.eject.bridge=00000028`, `u4.eject=00000100` (port 1, TBT_OK), the trace `PCIe tunnel down reason 1 (admin-down)` before "Safe To Remove Hardware"; `device_del enc0` then: no dialog and no further action (the port stays down until the re-plug); `device_add enc0`: generation 2, the tunnels rebuilt, the NVMe listed again. That build tore the port's USB3 tunnel down with the PCIe one; the driver was then changed to tear down the PCIe tunnel alone (a USB3 tunnel carries the host xHCI's devices, which the eject never asked to stop), host-tested and not read here again |
| U.5 and U.6 | 98 SE, `intel-adl` with `preos-cm=on` and `USB3=1` (`u98-adl`, `u98-adl2`, `u98-adl3`) | `u4.preserved=00010809`: the pre-OS tunnel preserved (port UP, `hd` 8, `hu` 9), no reset, no transition and no reset of any kind in the trace after the driver's ring takeover, nothing re-enumerated; unplug (stub build): `u4.linkdown=01000109` and the host side torn down; re-plug: built here with bonding, the USB3 tunnel and its Host Controller Ack, the USB disk attached to the tunnel's `qemu-xhci` at 5000 Mb/s (`info usb`) - U.8's fabric half |
| U.10 policy values | 98 SE, `minimal`, `USB3=1`, `XhciCmApprove` 0, `XhciCmUsb3Tunnel` 0, `XhciCmLaneBonding` 0 (`u98-pol`) | the router enumerated and Configuration Valid, the port UP with no PCIe tunnel and no USB3 tunnel (`usb3_want ... attached=0`), the host's lane 1 written with Lane Disable (LANE_ADP_CS_1 bit 14) |

**The fault (task 39-A.5's code, not this task's).** On 98 SE with
`cm_reenum.c`, while an Add New Hardware wizard is open on a tunnel port's
child (the NVMe, no driver on this image), a CONFIGMG call the seam makes
on the connection-manager thread blocks until the wizard closes: the
thread stops, ring 0 is not serviced, the routers' hot plug events
overflow the RX ring and an unplug is not seen. Cancelling the wizard
unblocked it at once, and the queued unplug and re-plug then ran in order
(`cm.reenum.9x.query-remove=00000000`, the removal, the re-enumeration,
the tunnel up). The stub build in the same setup kept polling throughout.
Reported to the orchestrator for task 39-A.5.

**Not read here.** U.4 (unplug during I/O, the harness), U.7 (pre-OS
cleaning), U.9 (disable and enable), U.11 (two tunnel bridges), U.12 (a
daisy chain), U.13 (inert starts), the `asm4242` host model, suspend and
resume, ME and the NT 5.1 and 6.x guests (40-V.3), and a Thunderbolt 3
device router behind a USB4 host, for which the emulator has no preset
(record 22 section 8; task 40-A.8 is host-tested only). The NVMe's volume
was not mounted on 98 SE (no NVMe driver on the image). The 2000 legs'
INF copy also carried a test-only `PCI\VEN_1B36&DEV_00F4` model line; which
model line ranked there was not read. Evidence in
`vm\p40-drv\<run>\` (traces, debug-console logs, screendumps).

## 40.6 - Windows ME: a UAS disk as the first storage device after `INSTALL.BAT`

**Verdict: not a regression; the tool now names the step** (2026-10-15).
The installer's first-install leg on Windows ME read a UAS disk that
installed silently from the staged `xhciuas.inf` and stayed at Code 2, with
a yellow mark, through a restart. That is the limitation the release notes
carry since `2.0.0.0` ("Known limitations", "Windows ME: a UAS drive as the
first USB storage device shows Code 2"; the owner's ruling of 2026-10-04,
`roadmap-hcd.md` decisions table, and `runs/run-31.md`, "Windows ME: a UAS
drive first"), and the same leg read it with the published `2.4.0.0`
drivers: the leg's first device reading, before any `3.0.0.0` file was
staged, listed the UAS entry as `xhciuas 2.4.0.0; current (this release)`
at problem code 2. `2.4.0.0` behaves the same.

**The cause, read on the leg's own overlay** (a fresh overlay over
`vm\p40-inst-c\ovl\mtx-me-f3.qcow2`, nothing written to it; the mouse and
the UAS disk plugged in after the desktop, on the leg's ports; readings in
`vm\me-uas\legs\serial-r2.log`). The UAS devnode
(`Enum\USB\VID_46F4&PID_0003\27842-0000:00:03.0-3`) holds
`upperfilters = "USBNTMAP.SYS"`; its driver key (`Class\USB\0002`) holds
`DevLoader = *NTKERN`, `NTMPDriver = xhciuas.sys`, `InfSection = Uas.Dev`,
`MatchingDeviceId = USB\Class_08&SubClass_06&Prot_62` and an `InfPath` that
is the package's own `xhciuas.inf` as ME's `INF\OTHER` copy of it: the
silent install took the expected INF, with the expected entries.
`System32\Drivers` held `XHCIUAS.SYS` and no `USBNTMAP.SYS` or
`USBSTOR.SYS`, and `System\IOSUBSYS` no `USBMPHLP.PDR`. ME ships the three
in its cabinets (`BASE2.CAB` and the cabinets after it, the ME CD read with
7-Zip, nothing executed) and its `usbstor.inf` copies `USBSTOR.SYS` and
`USBNTMAP.SYS` only when a Bulk-Only device installs; with the named upper
filter missing, NTKERN cannot load the devnode's drivers, which Device
Manager shows as Code 2. A Bulk-Only stick plugged into the same guest
installed silently, with no CD and no restart, and placed all three files;
the UAS disk, unplugged and plugged back in, then worked.

**The change** (branch `me-uas`, `1c481f33` and `97d0eff0`; reviewed and
revised). `INSTALL.BAT` said nothing about it, and for a current UAS entry
at Code 2 printed "Nothing: this release runs it". On 98 SE and ME, a
present UAS entry at Code 2 while `System32\Drivers` holds no
`USBNTMAP.SYS` now gets the release notes' recovery in the plan and in the
steps: on ME, plug in any ordinary stick once, then unplug and replug the
disk; on 98 SE, NUSB's mass-storage component, the disk starting at the
next boot. Code 2 with the file present gets no such step. On ME with no
`USBNTMAP.SYS` the steps end with the same advice before any UAS disk is
plugged in. The decision is the pure `inst_uas_fix_9x` (`inst_core.c`,
host suite `test_inst`). No driver or INF changed: the copy of the two
files through `xhciuas.inf` stays parked by the owner's 2026-10-04 ruling,
since the 98 SE CD's `layout.inf` names neither file and 98 SE prompted for
both.

**The legs** (development host A, QEMU 11.1.0, TCG, `pc`, `pentium3`, one
processor, 256 MB, `qemu-xhci,p2=4,p3=4`; the `release-x86` package staged
from the branch, with its `INSTALL.BAT` and tool; devices hot-plugged after
the desktop, the UAS disk as `usb-uas` plus `scsi-hd` on a VVFAT directory,
attached explicitly; readings and shots in `vm\me-uas\legs\` and
`vm\me-uas\shots\`):

| Leg | Result |
| --- | --- |
| ME, first install, a fresh overlay of the stock ME base (`vm\t37me\winme-stock-base.qcow2`) | The New Hardware wizard for "PCI Universal Serial Bus" cancelled; `INSTALL`, Y: the package copied, DevicePath set, the steps ending with the ME advice to plug a stick before the first UAS disk; after the restart ME installed the controller with no wizard and asked for a restart. The UAS disk plugged in first installed silently and showed Code 2 (yellow mark); `/list` and `INSTALL` gave the ME step for it, `INSTALL` with it in place of "Nothing". The step followed: a Bulk-Only stick installed silently and placed `USBNTMAP.SYS`, `USBSTOR.SYS` and `USBMPHLP.PDR`; the UAS disk unplugged and plugged back in on the same port worked with no restart: no yellow mark, `/list` with no problem code, its file read, a 1 MiB file written and compared with `fc /b`, no differences. **PASS** |
| 98 SE (NUSB), first install, a fresh overlay of `vm\t26\win98-clean.qcow2` | `USBNTMAP.SYS` present from NUSB; `INSTALL`, Y: no ME line in the steps; the controller from the wizard by search, the "xhci98 xHCI Host Controller Disk" and the 98 SE CD prompts, restart; the UAS disk through the wizard by search from the staged copy, F: at once; its file read, the 1 MiB write compared clean; `/list` with no problem code and no Code 2 step. **PASS**, unchanged |

Both legs ran the tool unstamped, so it and the drivers read `2.4.0.0`, the
version the branch carried; the change touches the tool alone.

## 40-V.3 - The NT tiers: XP, XP x64, Vista x86, 7 x86

**Verdict: pass on XP, XP x64, Vista x86 and 7 x86; Vista x64 and 7 x64
not run** (2026-10-15). On `usb4-generic` with `host-model=intel-adl`, each
guest from a fresh overlay installed the package through `INSTALL.BAT` -
`xhcicm.sys` on the USB4 host interface `1B36:00F4` by its class line
`PCI\CC_0C0340`, the filter from `nt\` on the tunnel bridge `1B36:0001` -
and one plug cycle (plug, unplug, re-plug) brought the NVMe up with no
Refresh, removed it and brought it back. ME is not in this section.

**Not run: Vista x64 and 7 x64**, for the reason `run-39.md`'s 39-V.3
section gives (no test-signed amd64 package on this run). XP x64 ran the
unsigned package.

**The vehicle and the package** are 39-V.3's (`run-39.md`): the same
builds, staging, NVMe image, tools and ports; `-Usb4Profile generic
-HostModel intel-adl -DeviceAtBoot 0`. XP ran on `vm\usb4\bin` (the
`a50c19e` build); 7 x86, Vista x86 and XP x64 on the `5ba7f60` build in
`vm\usb4\bin-5ba7f60`.

| Leg | XP SP3 | XP x64 SP2 | Vista SP2 x86 | 7 SP1 x86 |
|---|---|---|---|---|
| Install: host `xhcicm` started, bridge `UpperFilters` `xhcicm` | pass | pass | pass | pass |
| Plug: tunnel up, NVMe with no Refresh, `NVME32` mounted | pass | pass | pass | pass |
| Unplug: devnode and volume gone, no dialog | pass | pass | pass | pass |
| Re-plug: back | pass | pass | pass | pass |
| Result | **PASS** | **PASS** | **PASS** | **PASS** |

The trace on each: the hot plug events of the router's lanes, `PCIe tunnel
up reason 0` and `tp_insert` per plug, `tunnel down reason 3` per unplug.
The host interface showed as "xHCI98 USB4 Host Interface" (the build
before the new Device Manager names).

**A reading, not this driver's.** On XP and 7 "Safely Remove Hardware"
also lists the host interface itself, beside the NVMe. Its devnode reads
`DN_REMOVABLE` (status `0x01806400`) before any driver of this project was
installed (XP's dump before the install), so the flag is the platform's
for that function on bus 0 of this emulator, not the child filters'
mark (which only ever goes on devices reported on a tunnel bridge). Why
the platform reports it so was not read. The Alpine Ridge NHI behind the
switch reads `0x0180200a`, with no such flag.

**Evidence.** `vm\v-xp7\xp-gen\`, `xp64-gen\`, `vx86-gen\`, `w7-gen\`,
as for 39-V.3.
