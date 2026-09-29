# Real speeds on root ports: a virtual USB 2.0 hub for each slower device on a root port

Design record for roadmap task 24.3. It was
`docs/future-plans/virtual-hub-per-root-port.md` from 2026-09-07 until
2026-09-25, when the owner took the work up as task 24.3 in the on-demand
shape (section 4), made the permanent shape a second setting of the same
switch rather than a fallback (3.1, 3.8), and answered every decision it
asked for (section 9);
sub-task 24.3.1 moved it here, the next free number, and the future-plans
page is gone. Sections 1 to 8 keep the proposal's wording where it still
holds, and a sentence that says what the driver "would" do now says what
24.3.3 builds. No boot has been taken for it: the host vectors are 24.3.2
(done 2026-09-25, a pure core and its suite, section 8), the driver
24.3.3 (done 2026-09-25, section 8 and section 10), and the readings of
section 5 are 24.3.4 and 24.3.5. 24.3.2 also widened value 1's root-report
exception from the suspend pair to the enable, reset and suspend groups
(3.2), the owner's call of that day.

What 24.3.1 added besides the move: the confirmation that every NT target's
hub INF binds by class, as the 9x ones do (section 3.1, "Which INF binds
the hub"). Everything the record decides is in section 9; what the driver
had to settle that the record left open is in section 10.

The evidence it rests on is the tree as it stands, issue 6
(`docs/issues/06-full-speed-root-port-bugcheck.md`), design record 02
(`docs/contributing/design/02-hub-topology-route-string.md`) and the
root-hub contracts in `docs/usb-xhci-info/usbport-miniport-abi.md`
section 4.

Two rules frame everything below, the same two the SuperSpeed storage
proposal was written under:

1. The virtual hubs are gated behind a registry value and **off by
   default**.
2. With the value absent or 0, the driver behaves **exactly as it does
   today**. Not "equivalently": the same root-hub reports, the same
   override, the same counters, the same trace lines, the same bytes on
   the bus.

Section 3.1 says what the value is, how it is read, and what holds rule 2.

## 1. The problem it solves

The driver reports every connected root port to `usbport.sys` as High Speed
whatever the device's decoded speed, and keeps the true speed for its own
Slot and Endpoint Contexts. Issue 6 says why: usbport applies the EHCI model,
in which a root port can only hold a High-Speed device, and when told a
root-port device is Full or Low Speed it looks up the transaction translator
that model guarantees. The root hub has none, `USBPORT_GetTt` turns the empty
list into a garbage pointer, and both shipping builds bugcheck. The override
in `XhciPortShadowReport` (`src/xhci_port.c`) is the one lever there is, and
it costs three things:

- **The interrupt interval.** usbport turns `bInterval` into the `Period` it
  hands the miniport using the speed it believes, and for a believed
  High-Speed device that is `2^(bInterval-1)` microframes capped at 32. A
  Full or Low Speed device on a root port is bucketed on those rules, the
  driver then floors it at the 1 ms the xHCI specification allows at those
  speeds, and the result is three bands: `bInterval` 1 to 4 gives 1 ms, 5
  gives 2 ms, 6 and above gives 4 ms. A stock mouse at `bInterval` 10 runs
  at 4 ms. The same mouse behind a Full-Speed hub arrives at its true speed
  and gets 8 ms, the value it asked for.
- **The isochronous cadence.** usbport stamps a believed High-Speed
  isochronous endpoint's packets one per microframe whatever the descriptor
  says, and the driver recovers the true cadence from a snooped
  `GET_DESCRIPTOR(Configuration)` (`src/xhci_desc.c`). A Full-Speed audio
  device on a root port works on Windows 98 and 2000, but on a derived
  cadence with a counter recording that usbport and the descriptor
  disagreed; from Windows XP on it plays nothing (issue 6 section 7).
- **Everything usbport shows and budgets.** Device Manager reports every
  root-port device as High Speed, and usbport budgets it against the
  High-Speed bus rather than a frame budget. The bandwidth half has no
  measurement either way.

Issue 6 section 8 lists a true-speed report as an opt-in and says why it
cannot be enabled under any shipping usbport: the fault is in usbport's list
handling, and SweetLow's rebuild has the same branch - read statically, and
on 2026-09-25 run: a truthful build took Windows 98 SE under his stack down
at NUSB's `NTKERN` address (roadmap 24.3, `runs/run-24.md`). The lever has two
positions and both have been taken. This page proposes a third: keep
reporting High Speed on every root port, and give usbport the hub its model
is looking for, only where a device needs one.

## 2. The idea

When a root-port reset on a managed USB 2.0 protocol port decodes a Full or
Low Speed device, the miniport presents, in that device's place, a USB 2.0
hub that does not exist on the bus: High Speed, one downstream port, one
transaction translator, `bDeviceProtocol` 1. The real device becomes that
hub's port 1. When the device is unplugged, the root port reports the
disconnect exactly as it does today, and usbhub removes the hub and the
device behind it the way it removes any hub. A High-Speed device on a root
port gets no hub and takes today's path unchanged, with the switch at 0 or
1. That is the on-demand shape, the switch's value 1. Its value 2 puts the
same hub on every USB 2.0 port from start to stop instead, so every
root-port device, High Speed included, sits behind one (3.8).

In usbport's view a slower device on a root port gains one tier; in the
xHC's view nothing changes, because the controller keeps driving the device
on its root port with a Route String of 0 and no TT fields, as it does today.

What that buys follows from the EHCI model usbport applies. A Full or Low
Speed device behind a High-Speed hub is the case that model was written
for: `USBPORT_GetTt` walks up to the virtual hub, finds a TT record there,
and returns it. usbport then does the truthful thing at every layer:
`DeviceSpeed` is the decoded speed, `Period` is bucketed in frames (which
`XhciIntervalFromPeriod` already handles, since that is what a device
behind a real hub gets), isochronous packets are stamped per frame, the
device is budgeted against the hub's TT, and Device Manager says Full Speed.
The measurement issue 6 already took behind a Full-Speed hub is the
prediction for a device behind the virtual one: `bInterval` 10, 1 and 4
arriving as `Period` 8, 1 and 4. So is the real USB 2.0 hub measured on
32-bit Windows 7 on the E460 (issue 6 section 8): behind it the Low-Speed
mouse ran without the Vista/7 bugcheck and the Full-Speed audio device
played.

Nothing in the xHC has to know. The xHC handles Full and Low Speed on a
root port natively, Table 6-6 conditions the TT fields on "connected
through a High-speed hub", and the driver's own topology graph already
decides the TT fields from each hub's decoded speed rather than from what
usbport names (design record 02, step 3). The virtual hub is a device that
lives entirely in the miniport, answered in software, and the only thing it
translates is what usbport is told.

## 3. What the driver does

### 3.1 The switch, and what off means

| | |
|---|---|
| Name | `XhciVirtualHSHub` (the owner's, 2026-09-25) |
| Type | `REG_DWORD` |
| Where | The controller's driver (software) key: the key a plain `AddReg` under an INF install section writes, and the key `XhciLogVerbosity` and the SuperSpeed storage proposal's `XhciSuperSpeed` live in |
| Values | `0` off, the default. `1` on demand: a virtual hub only on a root port whose reset decodes a Full or Low Speed device, section 4's on-demand shape. `2` always on: a virtual hub on every USB 2.0 port the driver manages, from start to stop, plugged or not and whatever the speed, section 4's permanent shape (3.8). Any other value is refused, and a refused value is not clamped: the driver applies 0 and records that it refused, on the rule the INF states for `XhciLogVerbosity` |
| Absent | 0, and not an error |
| Set by | The user, by hand, in Registry Editor. The INF's `AddReg` writes the 0 so the value is visible where the user looks for it; `XHCISNAP` does not set it |

It is read the way the log values and the SuperSpeed storage proposal's
switch are read: through `UsbPortGetMiniportRegistryKeyValue` from
`StartController`, in a routine of its own beside the log and moderation
reads (`xhciVhubRead` in `src/xhci_dispatch.c`, on the terms
`xhciLogReadValues` set), because every constraint on a registry read has
been paid for there. PASSIVE_LEVEL only, since
`StartController` is the one callback this driver knows is PASSIVE. No new
import, since the `Zw*` names are denied by the import gate and usbport's
service is the only registry channel this project may use. Nothing in the
read may fail a start: the value missing, the read failing and a NULL
service pointer all leave it at 0 and the driver starts as today, with the
`MPSTATUS` kept beside the value so a reader can tell a 0 somebody set from
nothing found. usbport zeroes the miniport extension before every start, so
the value is re-read at every start and cached nowhere else; changing it
takes a disable and enable of the controller or a reboot. What was read,
what was applied and the status go into the snapshot header
(`docs/contributing/passthru-snapshot-instrument.md`), so a dump from a
stranger's machine states which mode the driver was in.

The virtual hub's vendor and product id are **user-settable, and the
binary carries no default** (the owner, 2026-09-25). They come from two more
values beside the switch, read in the same routine and at the same time,
and written by the INF:

| | `XhciVirtualHSHubVid` | `XhciVirtualHSHubPid` |
|---|---|---|
| Type | `REG_SZ`: a hexadecimal string, the form a user reads in Device Manager's `USB\VID_1209&PID_0001` and types into Registry Editor as a String Value | the same |
| Accepted | exactly four hexadecimal digits, either case, optionally prefixed `0x` or `0X`, and nothing else - no spaces, no sign, no fifth digit - with a vendor id of `0000` refused | the same, `0000` accepted |
| Written by the INF | `"1209"` | `"0001"` |
| Set by | the INF's `AddReg` (a plain string line, which both setup engines write as `REG_SZ`), in both INFs and on every install path, beside the switch's 0; the user may change it by hand in Registry Editor | the same |

**With the switch at 1 or 2, both ids must be present and valid, or the
feature is off.** A missing value, a failed read, a value that is not a string of the
accepted form, or a vendor id of `0000` makes the driver apply the switch as
0 for that start and record which value failed and why, beside the switch's
own read status in the snapshot header. A value a user created as a DWORD
instead of a String Value fails the same way, because its bytes are not
hexadecimal characters. The driver never invents an id: there is no id in
the binary to fall back to, so a machine where the INF's values have been
deleted runs today's driver, not a hub with a made-up identity. With the
switch off the two values are not consulted at all, so their state cannot
affect rule 2.

**Reading a string through usbport's service** needs three things the
DWORD values never did, all from what `UsbPortGetMiniportRegistryKeyValue`
is measured to do (ABI document, "`UsbPortGetMiniportRegistryKeyValue` -
read out of both binaries"). It returns neither the value's type nor its
length, and copies exactly the byte count asked for, whatever the value
holds; and anything but a clean `STATUS_SUCCESS` - a value too long for the
buffer included - comes back as the one failure code. So the driver asks
for a fixed buffer sized for the longest accepted form and its terminator
(`0x` plus four digits plus a NUL), treats a failure as "missing or too
long", and parses only up to the first NUL, refusing a buffer with none;
bytes after the terminator are whatever the service's scratch buffer held
and are never looked at. And the character encoding it hands back is
known on NT by construction (the NT registry stores `REG_SZ` as UTF-16)
and **not measured on Windows 98 or ME**, where the value comes
through NTKERN's registry layer. The accepted form makes that decidable
from the bytes alone: every accepted string is at least four characters of
ASCII, so as UTF-16 its second byte is 0 and as a single-byte string it is
not. The parser takes whichever the bytes show, host vectors run both, and
the 9x encoding is the first thing 24.3.4 reads on Windows 98 SE and ME.

The ids reach `GET_DESCRIPTOR(Device)` (3.3) and nothing else. Neither
decides which driver binds the hub: every hub INF on every target binds by
class (the next table), and none names vendor id `0x1209`. What an id does
decide is the hub's hardware id,
`USB\VID_xxxx&PID_yyyy`, which Windows matches **before** the class, so an
id that some vendor INF on the machine names would bind that vendor's
driver to the virtual hub; the values exist so that a user who meets that
can move off it. Changing either one gives the hub a new hardware id, so
Windows treats it as a new device and installs the hub again on each port
it appears on. And since the INF writes all three values on every install,
as it does `XhciLogVerbosity`, a reinstall turns the feature off and puts
the ids back to the INF's.

**Which INF binds the hub** (read 2026-09-25, 24.3.1; method static, a
text search over each file, nothing executed; `legal-provenance.md`
section 4). Every hub INF on every target matches a generic hub by a
compatible id, never by vendor, and none of the eleven names `VID_1209`,
so with the INF's id the hub takes the target's own generic hub driver:

| Target | File, and where it came from | Bytes | SHA-256 | Generic hub match |
|---|---|---|---|---|
| Windows 98 SE, NUSB 3.3 | `USB2.INF`, `tools/nusb-extracted/` | 7,738 | `8A583C40A063E9CF39F920D1DF4D5288AAB5D0B6F4AA276C93073F9C2D4EA10E` | `USB\HubClass` |
| Windows 98 SE, NUSB 3.6 | `USB2.INF`, `tools/nusb36-extracted/` | 14,543 | `A9C75950F2805A1F00EB1222DE525BEE8EAF0689D88143A9C8078208AF7F1F06` | `USB\HubClass` |
| | `USB.INF`, the same | 31,279 | `F67BA17B96E557CA206523303335BB66A6F255E08A7997B16930D455EE197839` | `USB\CLASS_09&SUBCLASS_01`, `USB\CLASS_09` |
| Windows 98 SE and ME, SweetLow | `USB2.INF`, `tools/sweetlow-extracted/` | 4,146 | `A3D2B895A3A776576C0ACC2CE48DD2D61DCA77C7C9FD0DF79EC94105FEAB057A` | `USB\HubClass` |
| Windows 2000 SP4 | `usb.inf`, `I386\USB.IN_` on the SP4 CD | 32,543 | `1B2F63A2F526C7678ECFD0F68C991E9094C5ADAA6D266CA9DF0EF6953049C917`[^cd] | `USB\HubClass`, and `USB\CLASS_09&SUBCLASS_01`, `USB\CLASS_09` under `[GenericHub.Section]` |
| Windows XP SP3 x86 | `usb.inf`, `I386\USB.IN_` | 20,623 | `364F86B157BA808C3AA21B8B8946783FCFB35245155C49DDA0F23C809FBAC418`[^cd] | the class pair, `[GenericHub.Section]` |
| Windows XP x64 SP2 | `usb.inf`, `AMD64\USB.IN_` | 21,580 | `99DE5A2FB199B483D3EFF256E0A45F7A8CA2246E0EF406FB32BC95E23D1D2B7F`[^cd] | the class pair, `[GenericHub.Section.NTamd64]` |
| Vista SP2 x86 | `usb.inf`, `Windows\inf\` in `sources\install.wim` image 1 | 40,740 | `D9F64E8A451838E2A1F78F0F397A987126D9B04D854228379F451BCBD40E25A7` | the class pair, `[GenericHub.Section.NTx86]` |
| Vista SP2 x64 | the same | 41,060 | `6838C76CAF1976D46066510F85D0F241E2723AFFADE7BF133CD67CD0C9610C63` | the class pair, `[GenericHub.Section.NTamd64]` |
| Windows 7 SP1 x86 | `usb.inf`, `Windows\inf\` in `sources\install.wim` | 42,216 | `571CDF50837F269699EF7B54B53550DF18140DA41666196C407F0E584C87E19A` | the class pair, `[GenericHub.Section.NTx86]` |
| Windows 7 SP1 x64 | the same | 42,536 | `6F9EBC72711E3730463B682190E33F61FCAB5ACAD3C4A5D43EA0BDDB0789297D` | the class pair, `[GenericHub.Section.NTamd64]` |

[^cd]: The hash is of the file after `expand.exe`; the Windows 2000 and
both XP media carry it compressed as `USB.IN_`. Every row's hash is
recomputed with `Get-FileHash` over the git-ignored copy under `tools/`.

The virtual hub's compatible ids, which usbhub builds from the device
descriptor, are `USB\Class_09&SubClass_00&Prot_01`,
`USB\Class_09&SubClass_00` and `USB\Class_09` on every target, and
`USB\HubClass` on the ones whose hub driver adds it; one of those is what
each row matches. A vendor INF naming `USB\VID_1209&PID_0001` - another
project's test device, since the id is shared - would outrank them all,
which is the collision section 3.1's user-settable ids exist to step off.

**Why `1209:0001`, and what it is.** `0x1209` is pid.codes' open-source
vendor id, and `0x0001` is the first of its sixteen test product ids
(`0x0001`-`0x0010`), which pid.codes reserves "for use in private testing"
and says "MUST NOT be used on any device that will be redistributed, sold,
or manufactured", adding that software naming one should warn that it is
not unique and not for use outside testing. The owner chose it on
2026-09-25 because the feature is off by default and is documented as
**experimental, for private testing only** - pid.codes' own wording - and
because the id lives in the INF as a setting the user can change rather
than in the binary. Every
document that tells a user how to turn the feature on carries that warning
and says the id is pid.codes' shared test id, not one allocated to this
project. A project-allocated id from pid.codes (a pull request on its
repository, published under the project's name) remains the route if the
feature ever stops being experimental; that is the owner's to apply for.

The ids to avoid in the INF are other vendors'. Two look convenient and are
exactly that: the host controller's PCI vendor id (PCI-SIG and USB-IF
assign vendor ids separately, so the same number can belong to an unrelated
USB vendor, and Intel's `0x8086` and `0x8087` are USB vendor ids of real
hubs too), and `0x1D6B`, the Linux Foundation's id for Linux's own root
hubs.

Rule 2 is held at two divergence points, one per shape, and each tests the
applied value before it does anything new. At 1 it is the decision of 3.2,
reached only after a root-port reset has decoded a Full or Low Speed
device. At 2 it is the start of the controller and the root-hub callbacks
of 3.8, which create the virtual hubs and answer for them. With the switch
applied as 0 neither is taken: no virtual record is ever created, the
address map never holds an address without a Slot ID, and the topology
graph never sees a hub it has to fold out. The only new code that runs in
the off state is the read and the test of the applied value at each
divergence point. The root-hub report itself does not change at 0, nor
at 1 except for the enable, reset and suspend groups of a port in
virtual-hub mode (3.2); at 2 it does (3.8). Host vectors run the affected paths in all three
states, and the device matrix runs with the switch off in both the
value-absent and the INF-installed setups, as the SuperSpeed storage
proposal's plan does, so that "today's driver" is a measured reading rather
than a claim.

The gate is there for two reasons. At 1, every slower device plugged into a
root port brings a "Generic USB Hub" devnode with it, and one more
enumeration before it works; at 2, every USB 2.0 port carries one from
start, and every device on a root port, High Speed included, reaches
usbport through the new path. Either is a visible change a user who is
happy with the 4 ms mouse should not have to take. And the reading section 5
rests on, that each hub driver gives the virtual hub a TT record, has not
been taken; until it has on both primary targets, the default cannot move.

### 3.2 The root port, and the decision

This section is the on-demand shape, value 1. At 2 there is no decision;
3.8 says what replaces it.

The root hub keeps its descriptor, its port count and **every report it
makes today**, with the switch at 0 or 1, with one exception: on a port in
virtual-hub mode, the enable, reset and suspend groups come from the
virtual hub's upstream view rather than from the PORTSC bits
`XhciPortShadowReport` reads, because port 1 is the same physical port and
what usbhub does to port 1 must not reach the root port:

| Root-port bits, at 1, on a port in virtual-hub mode | Source |
|---|---|
| `PORT_CONNECTION`, `PORT_POWER`, `PORT_OVER_CURRENT`, `PORT_HIGH_SPEED`; `C_PORT_CONNECTION`, `C_PORT_OVER_CURRENT` | today's report, from PORTSC; `PORT_LOW_SPEED` stays clear |
| `PORT_ENABLE`, `PORT_RESET`; `C_PORT_ENABLE`, `C_PORT_RESET` | the upstream view: enabled from the end of a root-port reset until usbport disables the port or the device goes, resetting only during a root-port reset |
| `PORT_SUSPEND`, `C_PORT_SUSPEND` | the upstream view's suspend bit (3.5) |

So a port-1 reset's PR and PRC, a port-1 disable's PED write, and a PEC
the hardware raises reach port 1's view only; the upstream stays enabled
across all of them, and loses its enable only to its own reset, disable or
power-off, a disconnect, or the port losing power. Every root-port reset
owns its own completion - the second reset of the hub's enumeration
bracket and every later retry, not only the one that armed the hub - and a
physical reset's end, its PRC or its deadline, is routed by which view
started it and under which reset generation, never by the port's mode when
it ends. Over-current is both views': the root port keeps today's latch and
port 1 latches its own, each cleared independently.
*(Until 24.3.2 this paragraph named the suspend pair alone. Writing the
host vectors showed that port 1's resets and disables would then reach the
root port as a `PORT_RESET`, an unsolicited `C_PORT_RESET` and an upstream
that reads disabled; the owner took the widened exception on 2026-09-25
after a second opinion agreed.)* A direct port, and every
port at 0, keeps today's report bit for bit. A connected root port already
reports connected and High Speed before any reset (the override in
`XhciPortShadowReport`), usbhub already resets it before it creates a
device there, and the reset is a physical one, as now. Nothing about the
root port tells usbport whether a virtual hub is coming.

The decision is taken when a root-port reset completes. The Port Speed
field is valid only once the port is enabled (Table 5-27, which is why the
shadow keeps the decoded speed from the reset), so this is the first moment
the driver knows what it has. With the switch at 1, a decoded Full or Low
Speed puts the port in **virtual-hub mode**; High Speed, or the switch off,
leaves it **direct**, which is today's path. The mode is recorded in the
same shadow update that latches `C_PORT_RESET`, and usbhub learns that the
reset is over only by reading that latch through `RH_GetPortStatus`, so no
request usbport sends to the new device can arrive before the mode is set.
On the timeout path the reset has not decoded a speed, and the port is
direct; on a port that was in virtual-hub mode that is a change of mode, so
the forced connect change below applies to it as to any other.

In virtual-hub mode the reset arms the virtual hub's address-0 open rather
than a physical device's: the next `OpenEndpoint` for address 0 on that
port is served by the virtual record (3.3), and no Enable Slot is issued.
The rule that a root-port reset drops a pending hub-port claim
(`XhciTopoDropPending`) has to learn that difference.

The decision is taken again at **every** root-port reset, not only the
first: after an error usbhub cannot clear, on re-enumeration, and on
resume. A root-port reset while the port holds a virtual hub is a physical
reset, as today - held, like a port-1 reset, while a disable is owed on the
port (3.3's `SET_PORT_FEATURE(1, PORT_RESET)` row), since port 1's disables
are PED writes on this same port - which sends the device behind it back to the Default state,
exactly what resetting a real hub does to its children; usbhub then
re-creates the hub from address 0 and the hub re-creates its port. If a
re-taken decision changes the mode - a High-Speed device found after a
resume where a slower one was, or the reverse, or a timeout where a slower
one was - the driver latches
`C_PORT_CONNECTION` on the root port so usbhub tears down what it believes
is there and enumerates the port again, drops the virtual record if there
was one, and the next reset decides afresh. A disconnect forgets the
decision, so the next device on the port decides with no forced change -
and takes the hub with it (section 11). A
reinitialisation - a recovery in place, or a resume that had to
reinitialise - that finds the port empty behind a hub latches the root
port's connect change itself, since HCRST took the disconnect's CSC and an
empty port raises none: the removal route below then runs as on any unplug
(24.3.3's audit).

`RH_ClearFeaturePortEnable` and `RH_ClearFeaturePortPower` on a port in
virtual-hub mode mean what they mean today: usbport has let go of the port.
The driver drops the virtual record and, through the existing disown path
(`XhciSlotPortDisowned` and the confirmed half), the device's slot.
**That is not the removal route, as this record first said** (section 11):
an NT 6.x usbhub removes a disconnected device without disabling the port,
so the hub is retired by the reading that sees its device gone - no
connection, or a connect change - and the root port reports that reading as
today. The Port Status Change Event path and the start/resume seed keep
every latch rule in `docs/contributing/implementation-invariants.md`, "Root
Hub Reporting", and announce through `UsbPortInvalidateRootHub` as they do
now, except from inside `SubmitTransfer`, and on NT 6.x from the event DPC and
from a root-hub feature or timer callback (section 11).

### 3.3 The virtual hub as a device

usbport creates the virtual hub the way it creates any device: EP0 open at
address 0, `GET_DESCRIPTOR(Device)`, `SET_ADDRESS`, the descriptors again at
the new address, `SET_CONFIGURATION`, then the hub class. The driver
intercepts `SET_ADDRESS` today and issues Address Device; for the virtual
hub it records the address in the usbport-address map against a record
that has no Slot ID and no ring, and every later open and transfer keyed on
that address is served in software. Each request is a table row, and the
table is the whole device:

| Request | Answer |
|---|---|
| `GET_DESCRIPTOR(Device)` | `bcdUSB` 0x0200, class 9, subclass 0, `bDeviceProtocol` 1 (single TT), `bMaxPacketSize0` 64, the vendor and product id parsed from `XhciVirtualHSHubVid` / `XhciVirtualHSHubPid` (3.1; the hub does not exist without both), `bcdDevice` carrying the driver version, `iManufacturer` 0, `iProduct` 1, `iSerialNumber` 0 |
| `GET_DESCRIPTOR(Configuration)` | one configuration, one interface of class 9 with one interrupt IN endpoint (`bInterval` 12, `wMaxPacketSize` 1), `bmAttributes` self-powered, `bMaxPower` 0 |
| `GET_DESCRIPTOR(String)` | index 0: the language table, one LANGID, `0x0409` (English, United States). Index 1: the product string **"xhci98 virtual HS hub"** (the owner's, 2026-09-25), 21 characters, so `bLength` 44, UTF-16LE built from an ASCII literal, answered whatever LANGID `wIndex` names. Both truncated to `wLength`, as a device does when asked for the first two bytes. Any other index: a stall. There is deliberately no manufacturer string and **never a serial number**: with a serial, Windows keys the hub's instance on the serial rather than the port, so one hub instance would follow a device from port to port |
| `GET_DESCRIPTOR(Hub)` | `bNbrPorts` 1, `wHubCharacteristics` individual port power and over-current, `TTT` 0, `bPwrOn2PwrGood` from the root hub's own value (a smaller one would shorten every plug, since the physical port is already powered, but is only worth taking if every hub driver is measured to accept it), `bHubContrCurrent` 0, port 1 removable |
| `SET_CONFIGURATION` | success, no data; value 1 moves the hub to Configured and 0 back to Addressed (port 1 then reads disabled), since `GET_PORT_STATUS(1)` and the status-change pipe read that state; any other value stalls |
| `SET_INTERFACE` (alt 0), `CLEAR_FEATURE(ENDPOINT_HALT)` | success, no data |
| `GET_STATUS(Device)` | two bytes, `0x0001`: self-powered, as the configuration descriptor says, and remote wakeup off. Truncated to `wLength` |
| `GET_STATUS(Interface)`, `GET_STATUS(Endpoint)` | two bytes, `0x0000` (an endpoint is never halted, since it has no hardware behind it). Truncated to `wLength` |
| `GET_HUB_STATUS` | zero |
| `GET_PORT_STATUS(1)` | the virtual port's own state (3.8), which follows the real port's shadow except where the virtual hub itself was reset or disabled, or port 1's power bit is clear: the connect and over-current bits as today, the enable and reset bits from port 1's own view (a root-port reset never shows on port 1), the power bit from port 1's own, the suspend bit from port 1's view (3.5), and the Low-Speed or High-Speed bit from the decoded speed, neither for Full Speed. The enable bit reads set only while the hub is configured and port 1 has been enabled by its own completed reset. At 1, `C_PORT_CONNECTION` is latched when the hub is created, since the device was already there; at 2 it is latched at creation only if a device is there, and afterwards on every physical connect change (3.8). At both, a root-port reset that re-arms the hub latches it again for a device that is there, as a real hub's reset power-cycles its ports; port 1 latches `C_PORT_RESET` at the end of its own reset, `C_PORT_ENABLE` on a PEC the hardware raises while it reads enabled, and `C_PORT_OVER_CURRENT` on every over-current change |
| `SET_PORT_FEATURE(1, PORT_RESET)` | a second physical reset through the existing path: PORTSC.PR, the asynchronous timeout, a reset generation, `C_PORT_RESET` on completion, reported through 3.4. Its PR and its end reach port 1's view only: the root port neither shows the reset nor latches its completion (3.2), the end is routed by the view and generation it was started under, and a deadline that passes latches `C_PORT_RESET` as a root-port reset's does today, with port 1 left disabled. It is physical, not synthesised, so usbhub's enumeration retries reach the device the way they would behind a real hub. **It is held while a disable of the physical port is owed** (`DisownPending` set, from any of this table's disables or 3.8's): the request completes, and the PR write waits until the health poll has collected the PED confirmation and the confirmed half has run, so PR never overlaps an unfinished PED write and the poll never reads the reset's own PED clear as that confirmation. **A debt with no device record under it is not waited for** (round 6, 2026-09-29; section 11): with no record on the physical port or behind it (`XhciSlotPortHasRecords`, which counts a record in any state but free or gone, a disowned one included) there is no slot or ring for the confirmation to guard, so the disable or power-off that raises the debt, and the health poll that collects it, settle it at once, unconfirmed (`xhciRhDisownSettlesEmpty`, counted in `DisownsSettledEmpty`), and a held reset is released. A disable debt is never settled that way while PR or PRC is set, where it still owes the redisable after a reset's end (section 11), nor on an all-ones `PORTSC` read; a power-off debt is settled without reading `PORTSC`. With a record on the port, if the confirmation never comes the reset stays held, because the disown wait it is gated on is unbounded on purpose (`XhciRootHubPoll`): port 1 reports neither a reset nor its end, usbhub's own enumeration timeout is what gives up, and the hold is cleared by a root-port disable or power-off, a reinitialisation or a recovery (24.3.3; the owner decided on 2026-09-27 against a deadline of its own, since usbhub already gives up and new timing in the reset path is where the audit's review loop found its defects - revisited only if a 24.3.4 reading shows a hold that never clears, which round 6's did). The existing holdback covers Port Power only, so this gate is new, at 1 and 2 alike. A port-1 disable or power-off that arrives while a reset is held ends it, reported with port 1's `C_PORT_RESET`, rather than leaving the confirmation to start a reset usbhub has abandoned (section 11). **Asked for while a reset already runs on the physical port** - the root's at 1, or an earlier port-1 reset - it is the request's stall: the running reset keeps its owner and ends as its own, as a root port's second reset is refused as busy today (24.3.3's audit) |
| `SET_PORT_FEATURE(1, PORT_POWER)` | sets port 1's power bit, which is the virtual port's own and not PORTSC.PP (the next rows). If it was clear and a device is physically connected, `C_PORT_CONNECTION` is latched, so usbhub resets and enumerates it again |
| `SET_PORT_FEATURE(1, PORT_SUSPEND)`, `CLEAR_PORT_FEATURE(1, PORT_SUSPEND)` | through 3.5's merge with the root port's own suspend state, then the existing `RH_SetFeature...` and resume bodies for the underlying port. A suspend or resume those bodies refuse (a Port Power change in flight, an operation already armed, no timer service) is the request's stall, with port 1's view put back as it was: usbhub gets the failure the root port's own callback returns for the same refusal, where a request completed with success would have left it waiting on a `C_PORT_SUSPEND` nothing would send. Whether it asks again is the hub driver's own - Vista's `UsbhResumeSuspendedPort` reports the failure and signals its resume event, with no retry of its own (static, 24.3.3's audit) - and a request that follows is served as any other |
| `CLEAR_PORT_FEATURE(1, C_PORT_*)` | clear the virtual port's change bit, as `RH_ClearFeaturePortXChange` does for a root port |
| `CLEAR_PORT_FEATURE(1, PORT_ENABLE)` | the existing disable body on the physical port - the PED write, `XhciSlotPortDisowned` at once and the confirmed half (`XhciSlotPortDisabled`) once the port is observed down, or at once with no record on it (the `PORT_RESET` row) - which leaves the virtual hub in place with port 1 disabled until usbhub resets it again or the root port goes. Landing inside a port-1 reset, the PED write clears nothing: the confirmation waits for PR to clear, and the reset's end has the disable written again (section 11). The root port, the hub's upstream, stays enabled: at 1 its enable group is the upstream view's (3.2), at 2 the whole report is |
| `CLEAR_PORT_FEATURE(1, PORT_POWER)` | clears port 1's power bit and runs the same disable body, **not** the power-off body: PORTSC.PP stays set, because at 1 the root port reports that same PP and clearing it would take the virtual hub down with its port, and at 2 the virtual upstream is powered. While the bit is clear, port 1 reports no power, no connection and not enabled, whatever PORTSC says. The device keeps VBus, so a device that needs a real power cycle to recover does not get one here; it gets one from a root-port power-off (3.8) |
| `CLEAR_TT_BUFFER`, `RESET_TT`, `GET_TT_STATE`, `STOP_TT` | success. The xHC has no translator on a root port to clear |
| anything else | a stall, counted |

Two rules from the tree apply to how the answers are delivered. A transfer
cannot be completed inside `SubmitTransfer`, because usbport writes to the
transfer record after that callback returns (design record 05, section 7:
the submit bracket is a lifetime rule, and `SubmitEpoch` is what defers a
completion out of it); a synthetic completion therefore threads onto the
same deferred-completion list a hardware completion uses and drains from
the same place, with the reply bytes written through the scatter/gather
list's `MappedSystemVa` before `UsbPortCompleteTransfer`, the one window in
which usbport keeps that mapping. And a refusal that can never stop being
true must fail the transfer rather than return nonzero, so an unknown
request is completed with a stall status, never left queued for retry.

The two EP0 snoops share this channel. The hub-topology graph
(`src/xhci_topo.c`) learns hubs from the hub-class traffic it sees at
placement and from the reply bytes it reads at completion; the descriptor
snoop (`src/xhci_desc.c`) reads configuration descriptors the same way. A
virtual hub's traffic must either go through both folds and be recognised
there, or be diverted before them. **It is diverted.** The graph has eight
hub nodes (`XHCI_TOPO_NODES`), and at 2 a virtual hub on every managed
port would spend them before a real hub arrived, leaving `xhciTopoNodeAdd`
nothing for the hub the graph exists for. So the virtual records live in
their own fixed array, one per root port, indexed as `RootHub.Ports` is
and embedded in the extension like everything else (no pool), and a
transfer to a virtual hub's address is answered from that array and never
reaches either snoop. The graph never holds a virtual hub, and its eight
nodes stay the real hubs' at every value; 3.6 says how it places what is
behind one.

### 3.4 The status-change pipe

usbhub opens the hub's interrupt IN pipe and keeps one transfer pending on
it. The driver holds that transfer on the virtual device's queue and
completes it with a one-byte bitmap (bit 1 set) when the virtual port
latches a change; between changes the transfer stays pending, which is what
a real hub's pipe does. At 1 the pipe carries few changes: the connect
latched at creation, the completion of each port-1 reset, a hardware
disable of port 1, a suspend change and an over-current. It sends nothing
while the hub is unconfigured: a change latched then is held until
`SET_CONFIGURATION(1)`. An unplug is **not** one of them; it is the root
port's connect change (3.2), and it removes the hub along with the device.
At 2 the pipe carries those and every plug and unplug as well, because the
root port never reports a disconnect (3.8). A change latched with no transfer pending is held until the next
transfer arrives and completed into it at once. The controller has still
acknowledged the PORTSC bit the moment it was read, so nothing here loosens
the "latch until asked" half of the rule. Because one PORTSC now feeds two
views, the root port's and the virtual port's, each view keeps its own
change bits, and acknowledging one does not clear the other.

The pipe's `Period` is whatever usbport buckets `bInterval` 12 into; it is
never programmed anywhere, since there is no endpoint. `AbortTransfer` on
this pipe must find the held transfer wherever it is, queue or completion
list, as design record 05 already requires, and the hub's teardown on an
unplug is the case that will exercise it most.

### 3.5 Suspend: one port, two views

usbhub can suspend the root port (which, to it, suspends the virtual hub)
and can suspend the virtual hub's port 1, and both are the same physical
port. Each view keeps its own suspend bit, and the physical port is
suspended while either bit is set and resumed when both are clear.

**Neither view's request waits on the other.** A suspend or resume of
either view completes on its own as soon as the physical port has done
whatever the merged state asks of it - which may be nothing. Entering
suspend sets the view's `PORT_SUSPEND` status and latches no change bit.
`C_PORT_SUSPEND` means a completed resume (the invariant in
`implementation-invariants.md`, "Root Hub Reporting"), so a view latches
it only when that view was suspended and its resume has finished, and a
redundant request - a suspend of a suspended view, a resume of a running
one - changes nothing and latches nothing. The order that matters is the ordinary one, both suspended and
usbhub resuming the root port first: that resume clears the upstream bit
and completes at once, the physical port stays suspended because port 1
still is, and the virtual hub is running again as far as usbhub can tell,
so it can send the `CLEAR_PORT_FEATURE(1, PORT_SUSPEND)` that then resumes
the physical port. A rule that held the upstream resume until the physical
port ran would wait on a request that cannot be sent until it completes.

With no device enabled on the physical port - value 2's empty hub (3.8),
and at either value a port 1 that usbhub has disabled or powered off -
the two views differ. The upstream is enabled whatever the physical port
is doing, so a root-port suspend is taken in software alone: the bit is
set, and the existing suspend body is not called because there is nothing
enabled to suspend. Port 1 is disabled, and USB 2.0 section 11.24.2.7.1.3
lets `PORT_SUSPEND` be set only on an enabled port, so a port-1 suspend
there completes and leaves the bit clear. A remote
wake from the device resumes the physical port, clears both bits and
latches `C_PORT_SUSPEND` in each view that had its bit set. The rule is the
same at 1 and 2, so it is not a cost of either shape. At 1 the upstream's
bits are what the root port reports (3.2); the physical port's own link
state reaches neither view's suspend bit directly.

### 3.6 The topology graph

The virtual hub is a hub in usbport's view and must not be one in the
xHC's. These rules in the graph and the device records change meaning:

- A device behind virtual hub port 1 on root port N is a root-port device,
  and its record is **the record a device on root port N has today**, made
  by the same `xhciDevOpenOnRootPort` and carrying the same identity:
  `HubPort` N, so that `xhciDevByHubPort` finds it at every re-open,
  repeated reset and disown exactly as now; Route String 0, Root Hub Port
  Number N and no TT fields in its Slot Context. The virtual hub is a
  second, separate record: it has no Slot ID, is never returned by
  `xhciDevByHubPort` or the behind-hub lookup, and is found only by its
  usbport address and by root port N. The two are told apart at the one
  place they meet, an address-0 open on port N, by which reset armed it: a
  root-port reset (at 1 the one that decoded the slower device, at 2 the
  synthetic one of 3.8) arms the hub's open, and a port-1 reset arms the
  device's, through the root-port claim below. The rule that
  a behind-hub record must not hold a root-hub port holds, because the
  device's record is not a behind-hub record.
- `SET_FEATURE(PORT_RESET)` on a virtual hub arms the root-port
  enumeration claim for the device, not a hub-port claim. The two claims
  stay mutually exclusive.
- `XhciTopoTtFor` returns "no TT" for a device behind a virtual hub
  without being taught anything, because the graph never holds the virtual
  hub (3.3) and the device is a root-port device to it. What has to learn
  is the comparison that feeds `TtPairsAgreed`/`TtPairsDisagreed`:
  usbport's `HubAddr`/`PortNumber` will name the virtual hub for every Full
  and Low Speed root-port device, so the comparison has to recognise a
  virtual address from the virtual array and count the pair as agreed, or
  every such device counts as a disagreement.
- The second reset of the virtual hub's **own** enumeration bracket arms
  nothing. usbhub enumerates the hub as it does any device - reset, the
  address-0 EP0 open, `GET_DESCRIPTOR(Device)`, reset again, then
  `SET_ADDRESS` through the pipe it already opened (design record 02) -
  and `XhciSlotPortReset` suppresses the second reset's claim for exactly
  that reason, with `EnumResetSuppressed` as its one-per-claim bound. The
  same rule, with the same bound, is asked of the virtual record: a
  root-port reset while the virtual hub has EP0 open at address 0 and no
  address yet keeps that binding and arms no new open. That reset still
  owns its completion: at 1 it is physical, and its end is the upstream
  view's `C_PORT_RESET` like any root-port reset's (3.2).
- A device behind a real hub behind a virtual hub is one tier behind a real
  hub, and its TT fields come from that hub as they do today. A Full-Speed
  hub plugged into a root port decodes Full Speed, so it gets a virtual hub
  above it and, in usbport's view, a TT from it; the xHC needs none, which
  is already what the graph derives for a Full-Speed hub on a root port.
  This is the topology that bugchecks Vista and 7 today (issue 6 section
  6.2), and the virtual hub is what takes it off the NULL-TT path. At 1 a
  High-Speed hub on a root port decodes High Speed and stays direct; at 2
  it sits behind the virtual hub like any root-port device, and the graph
  places it as a hub on root port N, as it does today. A Full
  or Low Speed device behind that real hub takes its TT from it - the real
  hub's Slot ID and port, single or multi TT - exactly as today: its
  nearest High-Speed ancestor is the real hub, so the virtual tier above is
  skipped and never named as the TT hub, and usbport's `HubAddr` names the
  real hub too, so the agreement count is unaffected. This configuration
  exists only at 2, and host vectors cover it for both TT kinds (section
  8).

At 1, device removal keeps its route: an unplug is the root-port disconnect
it is today, and the virtual record goes in the same teardown. The hub-path
removal (the `GET_STATUS(port)` reply fold that tears down a behind-hub
device and its subtree) is not involved. At 2 the physical disconnect is
still a connect change in root port N's PORTSC, and the device's record,
a root-port record (the first rule above), is released by the
connect-change path that releases it today. What changes is only where
the change is reported: on the virtual hub's port 1 rather than the root
port, after which usbhub removes the device through its own hub path. The
hub-path reply fold is not involved at either value, because a virtual
hub's traffic never reaches it (3.3).

### 3.7 What stays as it is

Everything below the usbport-facing surface, and, at 0 and 1, the
root-hub report above it, but for 3.2's enable, reset and suspend groups
on a port in virtual-hub mode. Port speed decoding, the Slot Context and EP0 programming from the
decoded speed, the interval floor at Table 6-12's minimum, the reset
generations, the disown and disable split, the failure counters, the log
channel, the PORTSC watchdog and the recovery latch are untouched. The
High-Speed override in `XhciPortShadowReport` stays where it is, and in
virtual-hub mode what it describes is the virtual hub. A High-Speed device
on a root port takes today's path, byte for byte, with the switch at 0 or
1; at 2 its slot, contexts and rings are programmed exactly as today, and
what changes is only how usbport reaches it (3.8).

### 3.8 Value 2: a virtual hub on every USB 2.0 port

At 2 the decision of 3.2 is never taken. Every USB 2.0 protocol port the
driver manages carries a virtual hub from `StartController` to
`StopController`, plugged or not and whatever speed the device decodes.
USB 3.x logical ports stay unpowered and unmanaged, as at every other value,
and carry none. What changes against 3.2:

- **Two sets of state per port.** The virtual hub's record is permanent: it
  is created at `StartController` and freed at `StopController`, and
  nothing between them destroys it. What changes is its state. Its
  **upstream** state is what the root port reports - connected, powered,
  enabled, suspended, and the hub's own device state (Default, Addressed,
  Configured) with its usbport address and pipe bindings. Its **port-1**
  state is what `GET_PORT_STATUS(1)` reports: it follows the physical
  port's shadow, except that it reads disabled while the virtual hub itself
  is unconfigured or has been reset, since a real hub's downstream port is
  disabled then whatever the device behind it is doing, and reads
  unpowered and empty while port 1's own power bit is clear (3.3). Each
  set keeps its own change bits and its own suspend bit (3.4, 3.5).
- **The root port reports the hub, not the device.** Each managed root port
  reports connected and High Speed while its virtual upstream is powered,
  whatever PORTSC says, and enabled once a root-port reset has completed
  and until usbport disables it. `C_PORT_CONNECTION` is latched on the root
  port **once, at start**, so usbhub enumerates one hub per port. The
  physical connect, enable, over-current and speed state goes to the
  virtual hub's port 1 (3.3's `GET_PORT_STATUS(1)` row) and its change bits
  to the status-change pipe (3.4). The override in `XhciPortShadowReport`
  is not what produces this report: at 2 the report is the virtual hub's,
  whatever the device's speed.
- **A root-port reset resets the virtual hub, and takes the physical port
  out of service through the existing path.** `RH_SetFeaturePortReset`
  does not set PORTSC.PR: what usbhub is resetting is the virtual hub. The
  hub goes back to its Default state - address 0, unconfigured - and its
  port 1 reads disabled. **Its EP0 binding is kept**, as a real device's
  default pipe outlives a reset on the host side: usbhub's own enumeration
  of the hub resets it twice and sends `SET_ADDRESS` through the pipe it
  opened before the second reset, and that reset arms no new open (3.6).
  A held status-change transfer stays pending and carries nothing until
  the hub is configured again. If a device is enabled on the physical port,
  the driver then runs **the disable body `RH_ClearFeaturePortEnable` runs
  today** on it: the PED write, `XhciSlotPortDisowned` at once for the
  software half, and the confirmed half (`XhciSlotPortDisabled`) only once
  the port is observed down, with `DisownPending` and the health poll
  collecting a confirmation that is late, exactly as now. Nothing is
  released on the strength of a synthesised completion: the device's slot,
  its rings and every transfer queued on them, and the subtree behind it
  if it is a hub, stay as the hardware may still be reading them until
  that confirmation. The root port's `C_PORT_RESET` is latched when the
  virtual hub is back in its Default state, which does not wait on the
  confirmation, because what usbhub does next is enumerate the hub, not
  the device. The device is reached again only through a port-1 reset,
  which is physical (3.3) and is held while this disable is owed, by the
  gate on 3.3's `SET_PORT_FEATURE(1, PORT_RESET)` row.
- **Plug and unplug go through the hub.** A physical connect or disconnect
  is a change on the virtual port 1, never on the root port; usbhub removes
  an unplugged device through the hub path, and the slot is released as
  3.6's last paragraph says. The forced connect change of 3.2 never arises:
  a device swapped across a suspend is a device change behind a hub that is
  still there.
- **High Speed is behind the hub too.** A High-Speed device, and a
  High-Speed hub, sit on port 1 at their decoded speed. usbport needs no TT
  for them and the xHC is programmed for them as today; what changes is
  that they are enumerated through the virtual hub's port 1 and removed
  through its hub path.
- **`RH_ClearFeaturePortEnable`** on the root port means usbport has let go
  of the virtual hub, as a cancelled hub install does. The virtual record
  stays; its upstream goes to disabled with no address and no bindings
  (unlike a reset, because here usbport has abandoned the hub's pipes),
  its held status-change transfer is completed as cancelled, and the
  physical port is taken out of service exactly as by the reset above. The
  root port then reports connected and not enabled, and the next root-port
  reset re-enables the upstream and the hub is enumerated again from
  address 0, without a controller restart.
- **`RH_ClearFeaturePortPower`** does the same and also clears the
  upstream's power: the power-off is applied to the physical port as today,
  and while it is off the root port reports neither powered nor connected,
  as a real port with its power removed does. `RH_SetFeaturePortPower`
  restores the upstream's power, the physical port is powered through the
  existing body, and the root port reports connected again and latches
  `C_PORT_CONNECTION`, so usbhub enumerates the hub afresh.
- **Resume and recovery keep the hubs.** The virtual records are software
  and live as long as the miniport extension, which usbport zeroes only at
  a start, so every other event keeps them and nothing but the start
  latches a root-port connect change. What each event does to the device
  behind a hub is decided on port 1:
  - **A resume that restored the controller's state** (the slots survived)
    re-reads the physical ports as today, and a device that is still there
    is left alone. A device gone, or swapped, is a connect change on port 1.
  - **A resume that had to reinitialise the controller**, and **a recovery
    in place** (design record 07), lose every slot. The records of the
    devices behind the hubs go the way today's root-port records go on
    those paths; each virtual hub keeps its address, bindings and held
    status-change transfer, and on each port whose device the
    reinitialisation took the driver latches `C_PORT_CONNECTION` on port 1,
    so usbhub removes the device and enumerates the port again behind a hub
    that is still there. The held transfer is software and owns no TRB, so
    whatever a reinitialisation or a recovery does to the hardware's
    transfers, it leaves this one pending: nothing about the hub failed.

The costs of this shape are section 4's; the readings it needs besides the
on-demand ones are in section 5.

## 4. Two shapes, both built

Both shapes are built, as the switch's two "on" values (the owner,
2026-09-25): the user chooses between them, and neither waits on the other's
readings.

**On demand (value 1, the owner's first choice of 2026-09-25).** A virtual hub exists only while a Full or Low Speed device is on that
root port, created at the reset that decodes it and removed with it. What
decides it over the permanent shape is how much the new code touches: it
runs only for the devices that are broken today - the polling bands, the
silent audio from XP on, the Vista/7 bugcheck behind a USB 1.1 hub - and
never for a High-Speed device, which is most of what gets plugged in and
all of what works now. Three things follow:

- The root-hub report is unchanged at 1 but for the enable, reset and
  suspend groups of a port in virtual-hub mode (3.2), so the divergence
  point is one decision after a reset, not a rewrite of the root port.
- An unplug is a root-port disconnect, the removal path that exists today;
  usbhub removes the hub and the device itself (3.6).
- No hub devnode, USB address or boot-time enumeration is spent on an empty
  port or a High-Speed device, and the idle-suspend and boot-time questions
  of section 5 arise only while a slower device is attached.

Its costs, each with a home above: the decision is taken again at every
root-port reset, and a mode change across a resume needs a forced connect
change (3.2); each plug takes two physical resets and one hub enumeration
before the device's own, so a slower device takes longer to become usable
than today; the two views of one port have to merge their suspend states
(3.5); and a "Generic USB Hub" devnode appears and disappears with every
plug and unplug. On Windows 98 its first appearance on each port is a hub
install, which may show the New Hardware wizard once per port; and the hub
removal path in every hub driver runs on every unplug, which is what
section 5's churn reading at 1 measures.

**Permanent (value 2, always on; the owner, 2026-09-25).** Every managed
USB 2.0 port carries a virtual hub from start to stop, plugged or not, and
every root-port device, High Speed included, sits behind one (3.8). It is one state
machine per port with no decision point, a device swap across a suspend is
just a device change behind a hub that already exists, a plug costs no hub
enumeration, and nothing appears or disappears in Device Manager. Its costs
are why it is not value 1: every root-port device reaches usbport
through the new path, so a defect in it reaches the USB stick as well as
the mouse, and the whole device matrix has to be re-read under it; one
"Generic USB Hub" devnode and one USB address per managed port; N hub
enumerations at every controller start (a resume or a recovery keeps the
hubs, 3.8), on a Windows 98 machine that already boots slowly; pending interrupt transfers on N hubs that may hold off
usbport's idle suspend permanently; a root port that must report connected,
enabled and High Speed whether or not anything is plugged in, with
synthetic resets; and removal that goes through the hub path instead of the
root port. Until the owner's word of 2026-09-25 it was a fallback, to be
built only if the churn reading of section 5 failed on a target at 1. It
is built now, beside 1, for a target where that reading fails and for a
user who would rather have a fixed hub per port than a hub that comes and
goes with every slower device.

This page recommended the permanent shape until 2026-09-25, on the grounds
of one state machine and no decision point, and called on demand "the
smaller visible change and the larger state change". That undercounted what
on demand saves: its decision needs no change to what the root hub reports
about connection and speed (its enable group follows the upstream view
only so that port 1's operations stay off the root port, 3.2),
because a connected root port already says High Speed before the reset that
decides, and its removal is the path that already exists. The permanent
shape rewrites both.

## 5. What has to be measured before it is trusted

- **That usbhub creates a TT record for the virtual hub on every stack.**
  The whole idea rests on `USBPORT_GetTt` finding a non-empty list. Batch
  7b-V0 measured that a believed High-Speed hub with `bDeviceProtocol` 0
  got one (design record 02, open question 6), and a hub declaring protocol
  1 is the case the hub driver was written for, but why either holds is
  unconfirmed against the binaries. The probe is the one the tree already
  has: `HubAddr != 0xFFFF` on the first Full-Speed device's endpoint
  properties, read before the device is opened, on NUSB's build, SP4's,
  SweetLow's and XP's.
- **What a one-port hub does to each hub driver.** `bNbrPorts` 1 is legal
  and unusual. usbhub20, Windows 2000's `usbhub.sys`, Windows ME's and XP's
  each need one boot at 1 and one at 2.
- **At 1, the root port quiet under port-1 work.** The device's own
  enumeration behind the hub resets port 1 twice; the root port must show
  no reset, no `C_PORT_RESET` and no loss of enable through it (3.2), read
  from the snapshot's root-hub report on each hub driver.
- **Churn, at both values.** A Full-Speed device plugged and unplugged a
  few dozen times on a root port, on every target. At 1 each cycle creates
  and removes a hub; at 2 each is a plug and unplug behind a hub that stays.
  A target that fails at 1 and passes at 2 is one whose documented setting
  is 2. It is the test issue 6 section 6.2's Vista and 7 bugcheck already
  needed, and the matrix's churn row is the vehicle.
- **Value 2's own readings.** The whole device matrix at 2, High-Speed rows
  included, since every root-port device takes the new path there; a hub
  on every USB 2.0 port at start, with nothing plugged in; the first hub
  install on Windows 98 and ME on every port at once; the root-port reset
  with a device attached and transfers in flight, the device's slot and
  buffers held until the disable confirms; a cancelled hub install, a
  root-port disable and a root-port power-off and power-on, each followed
  by the hub enumerating again without a controller restart; plug and
  unplug through port 1; a real High-Speed hub on a root port with a Full
  or Low Speed device behind it, the device's TT taken from the real hub
  (the E460's USB 2.0 hub, `test-equipment.md`, is the metal vehicle; QEMU
  models no High-Speed hub, so in a guest it rests on host vectors); and
  whether each hub driver counts the virtual hub against USB's five hub
  tiers, which would cost every chain of real hubs one tier (at 1, only a
  Full-Speed hub on a root port pays it).
- **Plug latency.** From plug to device usable, against today, with a
  mouse and an audio device; `bPwrOn2PwrGood` is the lever if it matters.
- **The first hub install on Windows 98 and ME.** Whether the hub's first
  appearance on a port runs the New Hardware wizard or asks for the CD,
  under NUSB's stack and SweetLow's.
- **Idle suspend.** usbport idle-suspends a quiet controller (issue 5).
  Whether a virtual hub's pending interrupt transfer holds that off while a
  slower device is attached at 1, and at all times at 2, where every USB
  2.0 port holds one; either changes what the package's
  `USB_MINIPORT_FLAGS_DISABLE_SS` is for.
- **Resume.** At 1, the decision re-taken at the resume reset: the same
  device back in virtual-hub mode, and a device swapped for a High-Speed
  one while suspended, which must force the connect change of 3.2. At 2,
  the same swap read as a change on port 1 with the hub left in place; a
  resume that reinitialises the controller, and a forced recovery in place
  with the status-change transfers pending, each read as the hubs kept and
  the devices re-enumerated behind them (3.8). And at both, the suspend
  orders of 3.5: root port first, port 1 alone, both suspended and
  resumed root first, and a remote wake.
- **The product string.** Which hub drivers ask for the language table and
  index 1 unprompted, that each accepts the answer, and where the name shows:
  USBView, and the bus-reported description on Vista and 7.
- **Boot time.** One extra enumeration per slower device attached at boot
  at 1, and one per USB 2.0 port at 2.
- **The interval.** The measurement issue 6 took with a `bInterval`
  override tool, repeated on a root port with the switch on: `bInterval`
  10 should arrive as `Period` 8 and program Interval 6.
- **The bandwidth half** stays unmeasured until a root port is loaded with
  enough Full-Speed periodic traffic to reach usbport's TT budget.

## 6. What QEMU can and cannot show

`qemu-xhci` with `usb-kbd,usb_version=1`, `usb-mouse,usb_version=1` and
`usb-audio` (all Full Speed) on root ports reproduces every reading in
section 5 except boot time and Low Speed, on every guest the project holds;
QEMU models no Low-Speed peripheral, so Low Speed takes the `usb-host`
passthrough of a real mouse that roadmap task 24.1 built. Churn is
scriptable from the monitor (`device_add` / `device_del`). The device
matrix's Full-Speed rows run unchanged at 1 and 2 and are the regression
half. Its High-Speed rows must read exactly as with the switch off at 1,
and must pass behind the virtual hub at 2. What QEMU cannot show is a real Windows 98 machine's hub driver
under this, and that is the E460's reading: the Low-Speed mouse and the
Full-Speed audio device from `docs/contributing/test-equipment.md` on root
ports, with the interval read from the snapshot instrument.

## 7. What it does not do

- High-Speed devices see no change with the switch at 0 or 1. At 2 they
  sit behind a virtual hub at the same speed, with the same interval and
  one more devnode above them - **since roadmap 24.5**. Until then the
  interval was not the same: read at 2 on NUSB's, SweetLow's, ME's, SP4's
  and XP's usbport, a High-Speed mouse behind the virtual hub opened at
  `Period` 1, Interval 0 (125 us), where on a root port at 1 it opened at
  `Period` 32, Interval 5 (run-24.md, 24.3.4, finding 1). The cause is not
  the hub: usbport's USB 2.0 budget promotes an interrupt endpoint to
  period 1 whenever its start microframe lands past 2, which on NT 5.x has
  no speed test and happens once three periodic endpoints hold microframes
  0 to 2 - the hub's own status pipe is one of them. So it was older than
  this design, and any High-Speed interrupt device on a loaded bus met it.
  The driver now programs an interrupt endpoint from the pipe's own period,
  which usbport keeps at endpoint-properties byte 0x07 and never moves
  (`PipePeriod`; static, all ten builds). NT 6.x's budget does not promote
  High Speed, but promotes Full and Low Speed behind a TT, which the same
  change covers.
- The 1 ms floor stays, since the xHCI specification allows nothing less at
  Full and Low Speed. What goes away is the bucketing above it.
- usbport still rounds a Full or Low Speed `bInterval` down to a power of
  two in frames, so `bInterval` 10 gives 8 ms, as it does behind a real hub.
- It does not remove the override. Under a usbport that guarded the empty
  list (issue 6 section 8) a truthful root port would be simpler than a
  virtual hub, but no such usbport exists for these targets.
- It does not interact with the SuperSpeed storage proposal except by
  composition: at 1 a SuperSpeed device reported as High-Speed takes the
  direct path, since High Speed is what the decision sees. At 2 the hubs
  are on USB 2.0 protocol ports only, so a SuperSpeed port that proposal
  manages carries none.

## 8. Batches (roadmap sub-tasks 24.3.2 to 24.3.5)

- `-0`: the request table of 3.3 as host vectors (`test/`), fed the
  setup packets both shipping hub drivers send, from the measurements in
  design record 02 (`wValue` 0 on `GET_DESCRIPTOR(Hub)`, `wLength` 71); the
  decision of 3.2 (mode by decoded speed, the timeout path, the re-taken
  decision and its forced connect change); the suspend merge of 3.5 in
  every order it lists, the empty port included, reading back each view's
  status and change bits (no `C_PORT_SUSPEND` on entry, one per completed
  resume, none for a redundant request); the virtual hub's own measured
  enumeration - reset, address-0 open, `GET_DESCRIPTOR`, reset,
  `SET_ADDRESS` through the first pipe - with no claim left armed; port 1's
  power cycle, `CLEAR` then `SET PORT_POWER`, with the hub kept, PORTSC.PP
  untouched, the status read back and the device enumerated again, and the
  same cycle at 1 and at 2 with the PED confirmation delayed, the next PR
  held until it is collected and nothing torn down mid-enumeration; at 1,
  the root port's suspend pair on a port in virtual-hub mode read from the
  upstream view at every intermediate step of 3.5's orders, and a direct
  port's read as today; eight
  virtual hubs, and more managed ports than eight, with a real hub and its
  child still given a graph node; the graph rules of 3.6 as
  vectors over the existing topology tests, including the device's record
  found by `xhciDevByHubPort` across a re-open, a repeated reset and an
  address reused after a disown, the hub's and the device's address-0
  opens told apart by the reset that armed them, and a real High-Speed hub
  behind the virtual one with a Full and a Low Speed device behind it, in
  single and multi TT; value 2's root-port report, the root-port reset with
  transfers in flight (slot and buffers kept until the PED confirmation,
  and a port-1 reset held while it is owed), the root-port disable and
  power-off and power-on with the hub enumerated again, plug and unplug
  through port 1, hub-path removal, and the resume and recovery cases of
  3.8; the `GET_STATUS` byte counts and contents; the switch of 3.1, its refusal of any value
  but 0, 1 and 2, its snapshot header fields, and the off-state vectors that
  hold rule 2.
  **Taken 2026-09-25 as a pure core with its suite**, by the owner's
  scoping: `src/xhci_vhub.c` and `src/xhci_vhub.h` (DDK-free, called by
  nothing yet, not in `src/sources`) and `test/test_vhub.c`, which covers
  the request table, the switch and ids, the decision, the suspend merge,
  port 1's power cycle and held resets, value 2's lifecycle in the record,
  the array beside the graph with the TT cases, and rule 2's off state. The
  vectors that need the driver around the core - the slot and buffers held
  until the PED confirmation with transfers in flight, `xhciDevByHubPort`
  across a re-open, repeated reset and address reuse, the hub's and the
  device's address-0 opens told apart in `OpenEndpoint`, the snoops never
  seeing a virtual address, the deferred completion - moved to `-A`, in
  `test/test_init.c`.
- `-A`: the virtual device records in their own per-root-port array (3.3),
  the decision point, the synthetic
  completion path, the status-change pipe, the suspend merge, and value 2's
  hub on every USB 2.0 port, wired to the core `-0` built, with the
  integration vectors `-0` handed on.
  **Taken 2026-09-25.** The root-hub half is in `src/xhci_rh.c` and the
  device half in `src/xhci_slot.c`, both carrying out the core's verdicts;
  `src/xhci_vhub.c` joined `src/sources`; both INFs write the three values
  on every install path and the INF gate requires them; the snapshot header
  is schema 5 and `XHCISNAP` prints the mode, the ids and any refusal. The
  handed-on vectors are six functions at the end of `test/test_init.c`, and
  eleven driver mutations each failed at least one of them. Section 10
  records what the wiring had to decide.
- `-V`: section 6 on every guest held - Windows 98 SE under NUSB and under
  SweetLow's stack, ME, 2000, XP in both architectures, Vista and 7 in both
  - the switch at 0, then 1, then 2, the device matrix in all three states,
  and the churn reading at 1 and 2.
- `-E`: the E460 reading, at 1 and at 2, and at 2 the USB 2.0 hub on a
  root port with the Low-Speed mouse behind it.

## 9. Decisions

All the owner's, all taken on 2026-09-25 except the fifth, which does not
bind this phase. What 24.3.1 read to settle the record's own open point is
section 3.1's INF table; it needed no decision.

1. Whether it is wanted at all, against the cost in section 4: a hub
   devnode and a longer enumeration for each slower device, for the
   device's own interval (a stock mouse at its declared 8 ms rather than
   today's 4 ms band, and every rate hidusbf offers), Full-Speed audio from
   XP on, and the Vista/7 hub bugcheck. **Answered 2026-09-25: taken, as
   roadmap task 24.3, inside Phase 24, shipping in one cut with task
   24.1.** And on which targets: **all of them** with the switch on -
   Windows 98 SE under NUSB and SweetLow's stack, ME, 2000, XP, Vista and 7
   - rather than XP and later only.
2. Permanent or on demand. **Answered 2026-09-25: on demand, with the
   permanent shape kept as the fallback if the churn reading fails on a
   target. Changed the same day: both, as the switch's values** - 0
   disabled, 1 on demand (a hub only when a Full or Low Speed device is
   plugged into a root port), 2 always on (a hub on every USB 2.0 port).
   The permanent shape no longer waits on the churn reading (3.1, 3.8,
   section 4).
3. The virtual hub's vendor and product id and whether it carries strings.
   **Ids answered 2026-09-25:** user-settable through
   `XhciVirtualHSHubVid` and `XhciVirtualHSHubPid`, hexadecimal strings
   (`REG_SZ`) the INF writes as pid.codes' test id, `"1209"` and `"0001"`,
   no id in the binary, and the feature off
   if either is missing or invalid (3.1); documented as experimental, for
   private testing only. **Strings answered the same day:** a product
   string only, "xhci98 virtual HS hub"; no manufacturer string, never a
   serial number (3.3).
4. The switch's name. **Answered 2026-09-25: `XhciVirtualHSHub`.**
5. Whether the switch defaults to on in a later release once section 5 is
   read on both primary targets, and to which value, or stays an opt-in
   like the SuperSpeed storage proposal's. It is off in the Phase 24 cut; the later release is
   still open.

## 10. What the driver settled (24.3.3)

Sub-task 24.3.3 wired the core into the driver on 2026-09-25. Nothing here
changes a decision of section 9; these are the points the record left to
the build, each with the reason it went the way it did.

- **Where a verdict is carried out.** Every core call returns what to do,
  and the root-hub half carries it out under the same hold of the
  controller lock that took the decision, through the bodies today's
  callbacks run: `RH_ClearFeaturePortEnable`'s PED write, disown and
  confirmed half; the Port Power bodies; the suspend write; the reset and
  resume with their generations and watchdogs. To make that possible the
  two callback bodies were split into lock-held forms
  (`xhciRhPortOperationLocked`, `xhciRhStartOperationLocked`) with the
  unlocked callbacks wrapped around them unchanged, the deferred work
  still run exactly where it ran before, so with the switch off nothing
  about them differs.
- **Rule 2 is one test per divergence point.** `VhubConfig.Applied` is
  asked first at every site - the refresh, the status query, the change
  clear, the six feature callbacks, the root hub's build, the held-reset
  collection, the open, the submit, the abort, the REMOVE - and at 0 each
  returns to today's path before reading anything else. The host suite
  runs its twenty thousand checks with the switch absent, which is the
  standing reading that it holds.
- **A reset's generation is named before it is armed.**
  `XhciPortShadowNextGeneration` (`src/xhci_port.c`) is the arithmetic the
  arm uses, so the core records the generation the reset will carry and
  its end - a PRC, the watchdog's deadline or the health poll's age retire
  - is routed by that, never by the port's mode when it ends (3.2).
- **A reset or a suspend the port refuses.** A physical reset that cannot
  start - an operation armed, a Port Power change in flight, no timer - is,
  for port 1, reported to port 1 as a reset whose deadline passed, since
  the request that asked for it has already been answered; for the root
  port at 1 it is the refusal usbport gets today, with the upstream view
  left not resetting. A suspend the port refuses clears the view's bit it
  had just set, so no view reads suspended while the port does not.
- **The reading is folded in before a reset, suspend or resume decision**,
  as the resume path already did, so the core decides from the port as it
  is; only on a port with nothing armed, because a fold claims a reset
  whose PRC it finds and must not run ahead of the busy test.
- **The third entitlement.** `VhubArmedPort` is the port whose reset last
  armed a virtual hub's address-0 open, and it is kept exclusive with the
  two real claims the way they are kept with each other (3.6): the reset
  that arms a hub's open spends the root-port claim and drops a pending
  hub-port claim, and a root-port device claim (`XhciSlotPortReset`) or a
  snooped hub-port reset spends it. An address-0 open is therefore the
  hub's only when the hub's reset was the last one, which is how the hub's
  and the device's opens on one port are told apart.
- **How an endpoint is bound to a hub.** `XHCI_ENDPOINT` gained `VhubPort`
  and `XHCI_ENDPOINT_FLAG_VHUB`, and a virtual hub's endpoint keeps
  `DeviceIndex` 0 so that no lookup in the device layer can resolve it to
  a record; `MiniPortEndpointSize` is 24 bytes where it was 20. What
  usbport has bound per port - the EP0 and status-change extensions, the
  held transfer and its mapped buffer - is `XHCI_VHUB_BINDING`, beside the
  core's record rather than in it, because the core holds no pointer and
  is 32 bytes by assertion. A submit or a REMOVE through an extension that
  is not the bound one is a displaced handle and touches nothing.
- **Answers are written at submission and delivered later.** A request's
  reply is written through the scatter/gather list's `MappedSystemVa`
  inside `SubmitTransfer`, which is inside usbport's mapping window, and
  the completion goes onto the ordinary completion list; the vectors assert
  that nothing a virtual hub answers is completed from inside
  `SubmitTransfer`.
- **One status-change transfer is held at a time.** A second submitted
  while one is held is refused for retry, which ends when the first is
  completed or aborted. A completed pipe transfer is followed, if port 1's
  change is still uncleared, by the next one completing at once too, which
  is the core's rule (a change is reported until usbhub clears it).
- **The hub's `bcdDevice`** is the driver version's first three fields as
  one BCD digit each, taken from `XHCI_VER_CSV` in `src/xhci_version.h`, the
  version's one editable source.
- **Resume and recovery.** The root hub's build stands the hubs up once per
  start (`VhubStarted`, zero exactly when usbport has just zeroed the
  extension); a resume or a recovery rebuilds the root hub without a start
  and keeps them, and at 2 a port whose device the reinitialisation took is
  a connect change on port 1 even with nothing plugged in any more, since
  no CSC will say so (3.8).
- **The INF lines.** `XhciVirtualHSHub` is a DWORD written as 0; the two
  ids are `REG_SZ` with an empty flags field and quoted data, `"1209"` and
  `"0001"`, the form the property-page strings already use, so a
  numeric-looking id stays a string on both setup engines. They sit after
  the moderation interval on the 9x path and after `Controller` on the NT
  paths, and the INF gate's `VAL-*` rules require all three on every path
  with those defaults.
- **What a dump says.** Snapshot schema 5 appends the switch's status and
  value, the applied mode and refusal, and each id's status, verdict,
  encoding and value; `XHCISNAP` prints them in words. The log ring notes
  `vhub.switch`, `vhub.applied` and `vhub.ids` at every start, and
  `vhub.create`, `vhub.drop`, `vhub.flip`, `vhub.open` and `vhub.address`
  as they happen; the extension counts hubs stood up and dropped, opens,
  requests, stalls, failed transfers, pipe completions and cancels, held
  resets, flipped decisions, and TT pairs that named a virtual hub.

## 11. What 24.3.4's readings changed

The guest readings of 2026-09-27 (`docs/contributing/runs/run-24.md`,
"24.3.4") found two defects that only NT 6.x shows, both of which would hit
metal, and Codex's two rounds over them found two orderings the vectors had
not reached. Fixed together; the host vectors and their mutations are in
run-24.md.

- **No root-hub announcement from inside `SubmitTransfer`.** A request to
  a virtual hub reached `XhciRootHubDeferredWork`, which ends in
  `UsbPortInvalidateRootHub`. usbport holds its EpList lock across
  `SubmitTransfer` (and `AbortTransfer`, `PollEndpoint`,
  `SetEndpointState`), and on NT 6.x that service takes the same lock
  (Vista x64 `USBPORTSVC_InvalidateRootHub` -> `USBPORT_Ev_Rh_IntrEp_Invalidate`
  -> `USBPORT_ReferenceEndpoint` -> `USBPORT_AcquireEpListLock`, FDO+0x1160;
  Windows 7 x64 FDO+0xF88; static), so Windows 7 x64 at 2 and Vista x64 at
  1 hung (runtime: the stacks in run-24.md). The virtual-hub submit now
  runs `XhciRootHubDeferredArms`, and a latched change is left in
  `RootHubInvalidatesOwed` for the event DPC, the health poll, a root-hub
  callback or a port timer. The endpoint callbacks reach no other root-hub
  code. On NT 5.x the timers a reset or resume armed are armed at once,
  since a resume through port 1 is ended by its timer. On NT 6.x that arm
  was **not synchronised**: the legacy timer service skips its own lock and
  assumes the caller holds usbport's timer-list lock, which only its
  root-hub feature callbacks and timer DPC do (static), so it raced the
  timer DPC on another CPU, like every arm the driver made from the command
  pump, the event DPC and the health poll - older than this task. **Roadmap
  24.4 fixed it** (design record 05, "Where a timer may be armed"): on the
  Version 300 tier an arm from `SubmitTransfer` is owed rather than made,
  and the next event DPC, root-hub peek or health poll makes it through
  `UsbPortRequestAsyncCallbackEx` with its own lock. **The cost is here**:
  a resume through a virtual hub's port 1 on Vista or Windows 7 may end up
  to one poll interval (~500 ms) after T(DRSMDN) at 1 or 2 - a longer
  host-driven resume is legal; the wake is slower. The ReactOS-derived note
  that the legacy service is safe under `MiniportSpinLock`
  (`docs/usb-xhci-info/usbport-miniport-abi.md`) holds for NT 5.x only.
- **On NT 6.x a change the virtual hub makes up is announced late** (round
  5, 2026-09-28; `runs/run-24.md`, "Round 5 on the fixed build"). The build
  that carried 24.4 deadlocked Vista x86 at 1 and Windows 7 x86 at 0: the
  event DPC called `UsbPortInvalidateRootHub` under usbport's ISR-DPC lock,
  and usbport's own root-hub code takes its RH-IntrEp lock and then the
  ISR-DPC lock (static, all four builds; design record 05 section 3). On the
  Version 300 tier the event DPC now reports a change through
  `InterruptDpcEx`'s port bit, which usbport turns into the same invalidate
  with its locks released, and the root-hub feature callbacks, the port
  timers and the recovery only latch a change, for the health poll or the
  next event DPC to announce. 24.4's announcement of an owed arm from those
  callbacks went with it. A change a Port Status Change Event follows costs
  nothing. One no hardware event follows waits up to one poll interval
  (~500 ms) on Vista and Windows 7, unless an interrupt comes first: a
  change the virtual hub's root-port operations latch at 1 or 2 from a
  feature callback or a port timer, and the end of a host-driven resume
  when no Port Link State Change follows the U0 write. A resume's 20 ms
  timer is owed too - from `RH_ClearFeaturePortSuspend` on a root port, and
  from `SubmitTransfer` through a virtual hub's port 1 as above - so on an
  idle bus it can start up to one poll interval late and its end be
  announced as late again: legal, and slower. That is what the code does;
  none of it has been read on a guest, and the rebuilt package is what the
  retaken legs read. NT 5.x announces as before.
- **At 1 the hub retires with its device.** Section 3.2 had the hub
  dropped by usbport's disable of the root port, which 98, ME, 2000 and XP
  always sent. Vista's and Windows 7's usbhub do not
  (`usbhub!UsbhPortDisconnect`, Windows 7 x86 0x29967, Vista x64 0x2A5D8,
  static), so the hub stayed Present at its address after usbport freed
  it, and the next device given that address on another port opened as
  the hub - Windows installed "Generic USB Hub" for a High-Speed mouse
  (runtime, Windows 7 x86 and x64 at 1). The refresh now retires the hub
  on any reading that says its device is gone - no connection, or a
  connect change with the port still connected (a swap between two
  readings) - without waiting for the change bit, which an earlier reading
  may have taken. The record is cleared as the disable cleared it, the
  bindings and held transfer go through the existing `DROP` carry, and the
  root port reports the reading as today. A port-1 reset still running is
  kept recognised as nobody's, so its end latches nothing on the root port
  and arms no claim - and so is one whose deadline passed, through
  `LateEnd`, since its PRC may still come after the hub has gone. usbport's
  own root disable and power-off at 1 retire the hub the same way, because a
  disable does not end a reset in flight. A connect change the absent port already spent is not
  fed again to a hub the same reading's reset stood up. A late REMOVE, abort
  or transfer through the old hub's handles touches nothing of a
  replacement hub on the port, since a binding is matched by extension. At
  2 nothing changes: the hub stays and the unplug is port 1's (3.8).
- **A held port-1 reset ends at a port-1 disable.** `CLEAR_PORT_FEATURE(1,
  PORT_ENABLE or PORT_POWER)` left `ResetHeld`, so the confirmation started
  a reset usbhub had abandoned. It now ends there with port 1's
  `C_PORT_RESET`, as a preempted root-port reset is reported.
- **PED = 0 with PR = 1 is not a disable confirmed.** On a port carrying a
  virtual hub, a disable written inside a real reset cleared nothing - PED
  clears when PR is set (Table 5-27, p.372) - and the confirmation took the
  reset's own PED clear as the disable, clearing the debt before the
  reset's end enabled the port again. The confirmation now waits for PR to
  clear there, so the existing redisable runs at the reset's end; that
  redisable also leaves port 1's view disabled and arms no claim for the
  device. The rule also covers a record whose hub has retired while its
  port-1 reset is out, and the redisable collects that reset's end as
  nobody's; a direct port's own root reset is today's, since nothing would
  collect it.
- **A reinitialisation lets go of every reset a record owns**, a hub-less
  record's included: the rebuilt shadow no longer times it, and left owned
  it would take the next reset on the port for its own. After HCRST no PRC
  can follow and the late end goes too; after a successful restore PORTSC
  survived, so a port-1 or nobody's reset becomes a late end instead, its
  PRC - pending for the seed or still to come - nobody's.

Not changed, and recorded in run-24.md for the owner: QEMU's `qemu-xhci`
ignores a PED write, so on Vista and Windows 7 every port-1 reset after
usbhub's first port-1 disable is held for ever by 3.3's gate, which was the
no-deadline rule working as decided (round 6's rule below ends that hold
where no record is on the port); Full- and Low-Speed devices behind a
virtual hub on NT 6.x cannot be read in QEMU without a patched emulator.
The owner accepted that gap (2026-09-27): no emulator is patched, and those
devices are read on the E460 under Windows 7 x86 (roadmap 24.3.5). The
timer-arm race above is roadmap 24.4, fixed in the build 24.3.4's remaining
readings take, with round 5's announcement rule on top of it.

On the fixed build the same gap shows at 1 as a loop on Vista and 7: usbhub
abandons the held resets with a root disable, which retires the hub; QEMU
ignores that disable too, so its debt stands; the next root reset stands up
a replacement hub, the redisable writes PED again under it, and the
replacement's second enumeration reset is held behind the same debt
(run-24.md, "The readings on the fixed build"). The rule is kept as it is.
On hardware whose PED clears after the read-back, with the root reset
asked for before the health poll collects the debt, the redisable is what
collects it and the second reset then proceeds (the vector
`test_vhub_root_disable_confirmed_late_then_reset`); excluding a hub the
same reset stood up would leave that debt with nothing to confirm it, and
discharging the debt at the reset would release the old device's buffers
with no evidence the port stopped reading them.

**Round 6 met 3.3's revisit condition, and a debt with nothing under it now
settles at once** (2026-09-29; `runs/run-24.md`, "Round 6 on the round-5
fixes"). On the build carrying round 5's fixes, Windows 7 x86 (twice) and
x64 bugchecked 0xFE (8, 6, 0xA) at 2 with four devices plugged: usbhub's
own 60 s watchdog on a root port's change-queue gate. The x64 kernel dump
named the gate's holder: a usbhub thread hard-resetting one virtual hub,
waiting on usbport's controller-wide bus lock, which another virtual hub's
port-1 enumeration held while every port-1 reset it asked for was held here
behind a disown QEMU never confirmed; usbhub's 2 s timer abandoned each, the
failed enumeration ended in a hub hard reset, and at 2 that hard reset's
synthetic root reset disabled the port again. All 32 device records were
free and no slot had been enabled (debugger, both dumps; `legal-provenance.md`
section 4). So it was not the hold itself that never ended - usbhub bounds
each one - but the unbounded wait under it, on which every port-1 reset at 2
is gated. The debt exists to keep a record's slot and rings from being
released while the controller may still read them, and here there were
none. Of the five responses the analysis ranked - settle a debt with no
record under it, take the proof from the controller instead (Disable Slot
or Stop Endpoint for the port's records), report the port disabled after
some number of polls, bound the hold, or leave switch 2 on NT 6.x to the
E460 - the owner took the first, which adds no timing to the reset path:
3.3's rule, at the disable and power-off site and in the health poll's
collector. It keeps the redisable: settled while PR or PRC is set, the
reset's end would find no debt and leave port 1 enabled under usbhub's
disable (the PRC half is from Codex's review of the rule). **What it does
not cover** is a port that carries a record - its own device, or one
behind a real hub on it - which still waits for PED as before, for
ever on QEMU; only a proof taken from the controller would end that wait,
and it was not taken. The rule has host vectors (run-24.md) and has not been
read on a guest; whether it also ends the loops at 1 above, or lets a
device behind a virtual hub enumerate on NT 6.x in QEMU, is for the
retaken legs.

## Sources

- `docs/issues/06-full-speed-root-port-bugcheck.md`: the bugcheck, the
  override, the three bands, the behind-hub measurement, the real USB 2.0
  hub on the E460, and the opt-in that cannot be taken.
- `docs/contributing/runs/run-24.md`, 24.3: the truthful report run under
  SweetLow's stack.
- `docs/usb-xhci-info/usbport-miniport-abi.md` section 4 (the root-hub
  callbacks and their contracts), section 5 ("Periodic scheduling: what
  `Period` actually carries") and section 8 ("The transaction-translator
  lookup").
- `docs/usb-xhci-info/xhci-data-structures.md`: Table 5-27 (Port Speed valid
  once enabled) and Table 6-6 (the TT fields).
- `docs/contributing/design/02-hub-topology-route-string.md`: what the
  miniport sees of hub-class traffic, the graph, step 3's TT derivation,
  and open question 6.
- `docs/contributing/design/05-locking-model.md` section 7: the submit
  bracket and the deferred-completion list.
- `docs/contributing/implementation-invariants.md`: "Root Hub Reporting",
  "Device Addressing", "Hub Paths".
- `docs/issues/05-idle-suspend-and-disableselectivesuspend.md`: the idle
  suspend the virtual hubs may or may not hold off.
- `src/xhci_port.c` (`XhciPortShadowReport`), `src/xhci_topo.c`,
  `src/xhci_ctx.c` (`XhciIntervalFromPeriod`).
