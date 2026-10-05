# The Advanced tab's bandwidth on Windows Vista and 7

Design record for roadmap-hcd task 34.3 (Phase 34, release `2.1.1.0`).
Revision 2, 2026-10-05: written before the code, for review; revision 2
adds the WMI buffer and status rules, the registration ordering, the
service key path's copy, zero-bandwidth endpoints and the composite
limitation. Every fact
about the stock binaries below is **static** (`legal-provenance.md` section
4, the rows dated 2026-10-05); nothing in this record has run.

## 1. What is asked, and what is not

On Windows Vista and 7 the controller's Advanced tab lists, for each
connected device, the share of the bus its periodic endpoints hold. Under
`xhci98.sys` every row reads 0 (task 33.5; release notes, known
limitations), because the page asks a question nothing in the HCD answers.
This record makes the HCD answer it.

Not in this design: the page on Windows 98 SE, ME, 2000 and XP, which asks
the door for the open pipes instead and already shows its figure from them
(design record 13 section 8); a bus-wide admission budget of the HCD's own;
and the per-device transfer counters the same structure carries, which stay
0.

## 2. What the page asks

`usbui.dll` on Vista and 7, in both architectures, builds the tab in
`BandwidthPage::Refresh`. It walks the controller's hubs through the door
and, for each connected device that is not a hub, calls
`UsbItem::GetPerformanceInfo` with that device's devnode:

1. `WmiOpenBlock` on `GUID_USB_WMI_DEVICE_PERF_INFO`,
   `{66C1AA3C-499F-49A0-A9A5-61E2359F6407}` (WDK 7.1 `inc\api\usbiodef.h`,
   line 137);
2. `WmiDevInstToInstanceNameW` for that devnode, which yields the instance
   name WMI gives a block registered on a PDO;
3. `WmiQuerySingleInstanceW`, and the answer is used only if it carries at
   least 0xE4 bytes of data, the size of `USB_DEVICE_PERFORMANCE_INFO`
   (`inc\api\usbioctl.h`, lines 1454-1532).

The devnode is the one whose driver key the door's
`IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME` returns for the connection:
a device's own PDO, or the first function's PDO of a composite device the
bus splits (design record 13 section 8.10).

The page computes, per row, in 32-bit unsigned arithmetic:

    (AllocedInterrupt[0] + ... + AllocedInterrupt[5] + AllocedIso) * 100
        / Total32secBandwidth

floored, except that a nonzero sum is never shown as 0. The product
overflows above a sum of about 42.9 million bits. For a USB 2.0 controller
it adds a fixed "System" row of 20%. The units are bits per 32 ms (32
frames); a full-speed bus is 384,000 and a high-speed one 12,800,000, which
are the stock `HCD_BUS_BANDWIDTH_FULL` and `_HIGH` values times 32
(`hcd_urb.c` lines 74-75 carry them).

`usbui.dll` from 98 SE to XP x64 imports no WMI query function at all, so
nothing here reaches those targets' pages.

### The structure

`#pragma pack(1)` is not needed: every field is a ULONG or a WCHAR, and the
offsets are natural on both architectures.

| Offset | Field | This driver writes |
|---|---|---|
| 0x00-0x1C | BulkBytes .. InterruptUrbCount (8 ULONGs) | 0 |
| 0x20 | AllocedInterrupt[6], period 2^n ms | section 6 |
| 0x38 | AllocedIso | section 6 |
| 0x3C | Total32secBandwidth | section 6 |
| 0x40 | TotalTtBandwidth | section 6 |
| 0x44 | DeviceDescription[60] WCHAR | the PDO's `Text`, else "USB Device" |
| 0xBC | DeviceSpeed (`USB_DEVICE_SPEED`, 4 bytes) | 0 low, 1 full, 2 high and SuperSpeed |
| 0xC0 | TotalIsoLatency, DroppedIsoPackets, TransferErrors | 0 |
| 0xCC | the six controller-only fields | 0 |
| 0xE4 | end | |

SuperSpeed reads as high speed (2): `UsbSuperSpeed` (3) arrived with
Windows 8's headers, and what Vista's and 7's page does with a value it
does not know is unread.

## 3. Who answers in the stock stack

`usbhub.sys` on Vista and 7, on **each device PDO**: a WMILIB context with
three GUIDs, `IoWMIRegistrationControl(pdo, WMIREG_ACTION_REGISTER)` when
the PDO starts and `WMIREG_ACTION_DEREGISTER` at surprise removal and
removal, `WMIREG_FLAG_INSTANCE_PDO` in its registration. The data comes
from `usbport.sys`'s `GetDevicePerformanceInfo` over the device's open
endpoints, with the bandwidth the miniport's scheduler allocated to each.

The HCD replaces both drivers (AGENTS.md, Architecture), so this query has
had no answerer since `2.0.0.0`.

## 4. Where the figure comes from

**From each device's open periodic pipes** (owner, 2026-10-05). The
roadmap's first wording, "the bus's own periodic bandwidth accounting",
assumed an accounting the HCD does not have: periodic admission is the
xHC's (it accepts or refuses each Configure Endpoint), and the HCD keeps no
budget of its own. The figure is therefore an estimate of what each open
isochronous and interrupt endpoint costs the link, from the payload and
interval it was configured with, as `usbport.sys` itself summed per
endpoint.

Rejected (owner, 2026-10-05):

- **The xHCI Get Port Bandwidth command.** It answers per root port and per
  speed, not per device, so every device behind a hub would share one
  figure, and the page wants one row per device.
- **Leaving it as a documented limitation.** It is what `2.1.0.0` does.

## 5. Registration: which PDO, when, and on which systems

**Only on NT 6.x** (`IoIsWdmVersionAvailable(6, 0)`, as `hcd_urb.c` already
decides the USBDI version). Windows 98 SE, ME, 2000 and XP never register,
so on those targets nothing in the WMI path runs and their pages are as
they were.

**Only on the PDO the page names**: a device PDO that is not a hub's
(`!pdo->Hub`; the page skips hubs) and is its group's first
(`pdo->Group == pdo->Serial`, the test `hcdDoorConnection` uses). A split
composite device registers once, on its first function, and that answer
covers **every** pipe of the device, since one row stands for the whole
connection. The other functions' PDOs never register.

**The lifetime.** A flag in the PDO extension, `WmiRegistered`, under the
PnP IRPs' own serialisation:

| Moment | Action |
|---|---|
| `IRP_MN_START_DEVICE` succeeds | register, if not registered |
| `IRP_MN_SURPRISE_REMOVAL` | deregister, if registered |
| `IRP_MN_REMOVE_DEVICE` | deregister, if registered, before the PDO is released |
| `hcdDeletePdo` | deregister, if registered, before `IoDeleteDevice` |
| `IRP_MN_STOP_DEVICE` | nothing: a stop for rebalance restarts the same PDO |

The REMOVE row matters because in this driver a removed PDO whose device is
still present stays listed, and a later enable starts it again (a disable
and enable in Device Manager). That START registers anew. The delete row
is a backstop for every path that deletes a PDO without a REMOVE this
driver saw last: dormant PDOs retired, `ParentLetGo`, Windows 98's retired
orphans (never registered there, but the rule is one rule), and never-reported
PDOs. Every caller of `hcdDeletePdo` runs at PASSIVE_LEVEL, as
`IoWMIRegistrationControl` requires.

A registration that fails is logged and counted and the START still
succeeds: the page loses one row, not the device.

**The ordering.** WMI may send `IRP_MN_REGINFO(_EX)` from inside the
`REGISTER` call, so the PDO is marked a provider (`WmiRegistered` set)
**before** `IoWMIRegistrationControl(REGISTER)` is called, and the flag is
cleared again if the call fails. Deregistration is the reverse:
`IoWMIRegistrationControl(DEREGISTER)` first, at PASSIVE_LEVEL and under no
spin lock, then the flag cleared, then whatever the PDO releases. Microsoft
documents that `DEREGISTER` does not return until the system-control
requests already sent to the device have completed
(`IoWMIRegistrationControl`, WDK reference), so no WMI request is inside
the PDO when its storage goes. Section 7's `Busy` covers the controller
reads inside one request, not the PDO's lifetime.

**A limitation, for split composite devices.** One registration answers for
the whole connection, and it lives and dies with the first function's
devnode. If that function alone is disabled, fails to start or has no
driver, the connection has no provider and its row is gone from the page,
although the other functions' pipes may still be open. Moving the provider
to another started function would also mean moving the door's driver key
for the connection (`hcdDoorConnection`), which the 98 SE to XP pages read
too; that is not worth its risk for a figure the page shows. The release
notes say so.

## 6. The figure (pure core, `src/xhci_pipe.c`)

`XhciPipePerfInfo` takes the device's speed class, whether it sits behind a
high-speed hub's TT, and its open periodic pipes - for each, the transfer
type, direction, Max ESIT Payload, transactions per interval (Max Burst
Size + 1 at high speed, else 1) and the Endpoint Context `Interval` - and
fills the seven allocation fields and the two totals.

Per pipe, bits per 32 ms:

- **Services per 32 ms** = 256 >> `Interval`, with the shift made last so
  that an interval longer than 32 ms keeps its fraction:
  `(bitsPerService * 256) >> Interval`. The `Interval` is the one the
  endpoint was configured with (`Ep.Interval`), so a fast-polled
  full- or low-speed interrupt pipe (task 33.8) is counted at the rate it
  actually runs. For full and low speed, an `Interval` below 3 (one frame)
  is taken as 3: those buses carry at most one transaction per frame.
- **Bits per service**, with the per-transaction overheads ReactOS's
  `usbport.h` uses (lines 466-472; interface constants, section 3 of
  `legal-provenance.md`), in bytes of that bus:

  | Speed, type | Overhead per transaction (bytes) | Bits per service |
  |---|---|---|
  | full, isochronous | 9 | (payload + 9) * 8 * 7 / 6 |
  | full, interrupt | 13 | (payload + 13) * 8 * 7 / 6 |
  | low, interrupt | 117, in full-speed byte times | (payload * 8 + 117) * 8 * 7 / 6, in full-speed bit times |
  | high, isochronous | IN 18, OUT 38 | (payload + overhead * transactions) * 8 * 7 / 6 |
  | high, interrupt | IN 25, OUT 45 | (payload + overhead * transactions) * 8 * 7 / 6 |
  | SuperSpeed, either | not modelled | payload * 8 / 10 (section 6.1) |

  7/6 is the worst-case bit-stuffing factor of USB 2.0, which ReactOS's own
  budget applies too. SuperSpeed has no bit stuffing, and its protocol
  overhead is not modelled.
- **A zero-bandwidth endpoint counts 0.** An isochronous endpoint with a
  Max ESIT Payload of 0, which the driver accepts
  (`XhciPipeZeroBandwidth`), moves no data and is skipped before any
  overhead is added, at every speed; otherwise an alternate setting with a
  zero-size endpoint would read 1% on the page.
- **Where it goes**: isochronous to `AllocedIso`; interrupt to
  `AllocedInterrupt[n]` with n = `Interval` - 3 when `Interval` is 3 or
  more (compared before the unsigned subtraction) and 0 below, at most 5:
  the period in milliseconds as a power of two.

The totals: `Total32secBandwidth` 384,000 for a full- or low-speed device and
12,800,000 for a high-speed or SuperSpeed one; `TotalTtBandwidth` 384,000
for a full- or low-speed device behind a high-speed hub, else 0.

**Arithmetic.** 32-bit unsigned only (AGENTS.md: no 64-bit). The largest
single product the specification allows, a SuperSpeed isochronous ESIT of
49,152 bytes at `Interval` 0, is 49,152 * 8 * 256 = 100,663,296 before the
shift, inside 32 bits; a high-speed one is smaller still. The driver admits
SuperSpeed isochronous payloads of at most 4 KiB today, so that bound is a
margin, not a case that occurs. Each addition into a field
saturates, and the **sum of all seven** is clamped to
`Total32secBandwidth` by adding at most the headroom left, so the page's
`* 100` (at most 1,280,000,000) cannot overflow and no row reads above
100%. A full- or low-speed pipe that the clamp cuts is rare enough to leave
as clamped; the log says when it happens.

### 6.1 SuperSpeed

The page has no SuperSpeed total, and 5 Gbit/s against 12,800,000 would
overflow its product. A SuperSpeed device therefore reports against the
high-speed total, with its bits divided by 10 (5 Gbit/s over 480 Mbit/s is
about 10.4): the row reads roughly the share of its own link. It is the
crudest figure here, and the release notes say so.

### 6.2 What it is not

An estimate, documented as one (owner, 2026-10-05): `usbport.sys` asked
the miniport's scheduler what it had reserved, including split-transaction
budgets; this sums what each pipe would cost on an otherwise idle link. It
does not model the xHC's own scheduling, a TT's microframe packing, or
bulk and control traffic. Pipes that are open but idle count in full, as
they did under `usbport.sys`, since an open periodic endpoint holds its
reservation.

### 6.3 A worked figure

A full-speed audio device playing 48 kHz 16-bit stereo through one
isochronous OUT endpoint of 192 bytes at `bInterval` 1 (`Interval` 3):
(192 + 9) * 8 * 7 / 6 = 1,876 bits per service, times 32 services =
60,032 bits per 32 ms, against 384,000: the page shows 15%. The reading
(section 11) checks the guest device's real descriptor first.

## 7. The WMI requests (executor, `src/hcd_wmi.c`, new)

`IRP_MJ_SYSTEM_CONTROL` on a device PDO, at PASSIVE_LEVEL.

**Not ours**: `Parameters.WMI.ProviderId`, compared at pointer width, is
not this PDO, or `WmiRegistered` is clear (it is set before `REGISTER`, so
the registration's own `REGINFO` is ours). Complete with the IRP's current
status and information, untouched; a PDO is the bottom of its stack and has
no one to pass it to.

**Ours**, minor by minor (`Parameters.WMI.DataPath` is the GUID for the
data minors; "another GUID" is any other):

| Minor | This GUID | Another GUID |
|---|---|---|
| `IRP_MN_QUERY_ALL_DATA` (0x00) | answer, below | `STATUS_WMI_GUID_NOT_FOUND` |
| `IRP_MN_QUERY_SINGLE_INSTANCE` (0x01) | answer, below | `STATUS_WMI_GUID_NOT_FOUND` |
| `IRP_MN_CHANGE_SINGLE_INSTANCE` (0x02), `_ITEM` (0x03) | `STATUS_WMI_READ_ONLY` | `STATUS_WMI_GUID_NOT_FOUND` |
| `IRP_MN_ENABLE_EVENTS` .. `IRP_MN_DISABLE_COLLECTION` (0x04-0x07) | `STATUS_SUCCESS`, nothing done: the block has no events and is not expensive to collect | `STATUS_WMI_GUID_NOT_FOUND` |
| `IRP_MN_REGINFO` (0x08), `IRP_MN_REGINFO_EX` (0x0B) | answer, below | (no GUID) |
| `IRP_MN_EXECUTE_METHOD` (0x09) | `STATUS_INVALID_DEVICE_REQUEST`: no methods | `STATUS_WMI_GUID_NOT_FOUND` |
| any other | `STATUS_INVALID_DEVICE_REQUEST` | |

Each `STATUS_WMI_*` value is checked present in the Windows 2000 DDK's
`ntstatus.h` when the code is written, and defined locally with its WDK 7.1
value if not.

**`REGINFO` and `REGINFO_EX`.** A `WMIREGINFO` with one `WMIREGGUID`
(`GUID_USB_WMI_DEVICE_PERF_INFO`, `WMIREG_FLAG_INSTANCE_PDO`,
`InstanceCount` 1, `InstanceInfo` the PDO), no MOF, and `RegistryPath` the
offset of the service key path as a counted string - a USHORT byte count
followed by the UTF-16 characters - after the `WMIREGGUID`. For `_EX` the
PDO is referenced (`ObReferenceObject`) and WMI releases it; for the older
form it is not. The Windows 2000 DDK has no `IRP_MN_REGINFO_EX`; the file
defines it (0x0B). `WMIREGGUID` is 0x1C bytes on x86 and 0x20 on amd64
(`InstanceInfo` is a `ULONG_PTR`); the DDK's types are used and their sizes
asserted (`C_ASSERT`). Buffers: below `sizeof(ULONG)`, `STATUS_BUFFER_TOO_SMALL`
with `Information` 0 and nothing written; below the whole answer, the size
needed as its first ULONG, `STATUS_BUFFER_TOO_SMALL`, `Information`
`sizeof(ULONG)`; otherwise the answer, `STATUS_SUCCESS`, `Information` its
size.

**The service key path.** `DriverEntry`'s `RegistryPath` is valid only
during `DriverEntry`, so `DriverEntry` keeps a copy from `HcdPoolAlloc` for
this alone, freed in the driver's unload, by which time every PDO and so
every provider is gone. If the copy cannot be allocated, the driver loads
as before and no PDO registers (`wmi.nopath` in the log): the Advanced tab
loses its figures, nothing else.

**`QUERY_SINGLE_INSTANCE`.** The buffer holds the request's
`WNODE_SINGLE_INSTANCE`. Below `sizeof(WNODE_SINGLE_INSTANCE)`, or a buffer
whose `WnodeHeader.BufferSize` is larger than the IRP's `BufferSize`:
`STATUS_BUFFER_TOO_SMALL`, `Information` 0, nothing written. An
`InstanceIndex` other than 0 (static names, one instance):
`STATUS_WMI_INSTANCE_NOT_FOUND`. The incoming `DataBlockOffset` is kept,
never moved, and the size needed is `DataBlockOffset` + 0xE4, checked for
wrap. If the buffer is shorter than that: a `WNODE_TOO_SMALL` over the
header (`WnodeHeader.BufferSize` = `sizeof(WNODE_TOO_SMALL)`, `Flags` |=
`WNODE_FLAG_TOO_SMALL`, `SizeNeeded` the size needed), `STATUS_SUCCESS`,
`Information` `sizeof(WNODE_TOO_SMALL)` - provided the buffer holds a
`WNODE_TOO_SMALL`, which it does, being at least a `WNODE_SINGLE_INSTANCE`.
Otherwise the 0xE4 bytes at `DataBlockOffset`, `SizeDataBlock` 0xE4,
`WnodeHeader.BufferSize` the size needed, `STATUS_SUCCESS`, `Information`
the size needed.

**`QUERY_ALL_DATA`.** Below `sizeof(WNODE_TOO_SMALL)`:
`STATUS_BUFFER_TOO_SMALL`, `Information` 0. The answer is one instance:
`DataBlockOffset` = `sizeof(WNODE_ALL_DATA)` rounded up to 8,
`InstanceCount` 1, `WNODE_FLAG_FIXED_INSTANCE_SIZE` set and
`FixedInstanceSize` 0xE4, `OffsetInstanceNameOffsets` 0 (WMI supplies the
names of a PDO-named block), and the size needed `DataBlockOffset` + 0xE4.
A shorter buffer gets the `WNODE_TOO_SMALL` as above; otherwise
`WnodeHeader.BufferSize` and `Information` are the size needed, with
`STATUS_SUCCESS`. The Advanced tab never sends this minor, so the host
tests (section 10) are its only check.

The WNODE layouts are the same on x86 and amd64; their fills are written as
pure functions over a byte buffer beside section 6's, so every rule above
is host-tested.

**A PDO with no device** (gone, or dormant across a controller stop): the
two queries return `STATUS_NO_SUCH_DEVICE`; the page skips the row.

**The data's capture** is `hcdDoorConnection`'s order (design record 13
section 5.4): the PDO's device record taken under `PdoListLock` with its
reference raised, then the pipes read under the controller lock as
`hcdDoorDeviceLocked` reads them - `dev->Pipes[2..31]`, skipping `NULL` and
`Closed` - into a small array on the stack, and the figure computed after
both locks are released. Streams are bulk and never periodic; they are not
in `Pipes[]` and are not read.

**The dispatch's own lifetime.** The PDO's storage is covered by section
5's ordering: `DEREGISTER` returns only once the requests already sent have
completed. Inside a request, the handler raises the PDO's `Busy` before it
reads `Controller`, as `hcd_urb.c`'s dispatches do, so the controller's
release waits the capture out as it waits a URB dispatch.

**Import.** `ntoskrnl.exe!IoWMIRegistrationControl`, new. Called on NT 6.x
only, but imported by the one 32-bit binary, so it must resolve on Windows
98 SE: its own `usbhub.sys` 4.10.2222 imports it (already in
`win98-evidence.list`), Windows 2000 and XP x64 export it. One row in each
of `scripts\import-gate\xhci98-imports.allow` and
`xhci98-imports-amd64.allow`. `WMILIB.SYS` is **not** imported: its
`WmiSystemControl` would be a new module on Windows 98 for a few dozen
lines of IRP handling.

**The log.** `wmi.reg` (the status) at each registration and `wmi.dereg`
at each deregistration, `wmi.query` (the minor) the first time a PDO is
asked, and `wmi.clamp` when the clamp in section 6 cut a figure; counters
`WmiRegistered`, `WmiQueries` and `WmiFailures` in the controller, never
zeroed. The log ring records only with `XhciLogVerbosity` raised.

## 8. Who may open the block

Stock `usbport.inf` grants the block's security from the root hub's install:
`[ROOTHUB.Dev.NT.WMI]` sets `WMIInterface =
{66C1AA3C-...},,WMIGuidSecurity_AllReadAdminFul` (read for everyone, full
for administrators) beside four other GUIDs. The HCD's INFs have no such
section, and on a machine where no stock root hub was ever installed
nothing grants it. Whether Vista's and 7's default GUID security then lets
the page open the block from a non-elevated Device Manager is unread.

**Tried without first** (owner, 2026-10-05). The reading (section 11)
looks at the GUID's value under
`HKLM\SYSTEM\CurrentControlSet\Control\WMI\Security` and reads the page
both elevated and as a standard user. If the standard user's page is empty
where the elevated one is not, an NT 6.x-only `.WMI` section with
Microsoft's SDDL name joins both INFs' `Xhci.Dev6` paths, with an INF gate
rule to hold it; Windows 98's engine never reads those sections.

## 9. The incidental system-control fixes

Today the root-hub and hub FDOs complete `IRP_MJ_SYSTEM_CONTROL` with
`STATUS_NOT_SUPPORTED` instead of passing it down, and every PDO fails it
rather than leaving it as it came (`hcd_entry.c`, `hcdDispatchOther`).
Harmless while nothing registers. WMI sends its IRPs to the top of the
registered PDO's stack, and each driver there passes on what is not its
own: a function driver passes it down to our PDO, which section 7
handles. A hub PDO never registers, but its FDO is ours, and a WMI IRP
meant for a driver below it must not die there. With this task: both FDOs
pass it to their lower device; the root-hub PDO and every device PDO not
registered complete it with its current status. That changes the path on
every target, so the ten install legs of 34.6 cover it.

## 10. Tests

`test\test_pipe.c` drives `XhciPipePerfInfo` and the 0xE4 serialiser:

- section 6.3's audio figure, exactly;
- each row of section 6's overhead table, at `Interval` 0, 3 and 10;
- full-speed interrupt at `bInterval` 255 (`Interval` 10), whose service
  rate is below one per 32 ms and must not read 0;
- a fast-polled full-speed pipe at `Interval` 0, counted at one per frame;
- the period bucket at `Interval` 0 to 3 (bucket 0) and 8 and above (5);
- the largest SuperSpeed isochronous ESIT, and seven such pipes, clamped
  to the total with no wrap;
- low speed, and a full-speed device behind a TT;
- no pipes: every field 0, the totals set;
- a zero-bandwidth isochronous endpoint at full, high and SuperSpeed: 0;
- the serialiser's offsets: each field at section 2's offset, the text
  truncated to 59 characters plus NUL, "USB Device" for an empty text.

A new `test\test_wmi.c` drives the WNODE fills of section 7: for
`QUERY_SINGLE_INSTANCE`, a buffer one byte short of the header, exactly the
header, one byte short of the answer (the `WNODE_TOO_SMALL`), exactly the
answer, a `DataBlockOffset` other than the header's size kept in place, a
`DataBlockOffset` near 0xFFFFFFFF that would wrap, a nonzero
`InstanceIndex`, and a header `BufferSize` above the IRP's; for
`QUERY_ALL_DATA` the same boundaries and the 8-byte data offset; for each,
the status, `Information` and every byte written, and that nothing is
written past the buffer (a guard pattern after it).

The rest of the executor (registration lifetime, the minor table,
`REGINFO`) is not host-tested; it is reviewed and read in the guests.

## 11. Readings

**In virtual machines only** (owner, 2026-10-05): unlike 34.2 and 34.4,
34.3 is ticked on VM readings. The release flavour, on the four guests of
the sixth tier - Vista SP2 and Windows 7 SP1, x86 and x64 - each with a
QEMU USB audio device and a HID mouse on the HCD:

- before anything else, the audio device's isochronous endpoint read
  (`XHCISNAP` or the guest's own descriptor view), and section 6.3's figure
  recomputed for it;
- with audio playing, the Advanced tab: a row for the audio device at the
  recomputed figure, a small nonzero row for the mouse, and what the page
  draws for "System";
- with audio stopped, the audio row at 0 or gone, as its driver closes the
  pipe;
- with `XhciLogVerbosity` raised, `wmi.reg` with a success status for each
  device and no `wmi.clamp`;
- section 8's two checks: the GUID's security value, and the page as a
  standard user;
- a disable and enable of the audio device, then of the controller, and an
  unplug of the audio device, each followed by the page again (the
  registration lifetime of section 5).

Windows 98 SE, 2000 and XP are covered by 34.6's install legs, read from
the asset, since nothing registers there and section 9 is the only change
those systems see.

## 12. Decisions (owner, 2026-10-05)

1. The figure comes from each device's open periodic pipes; the Get Port
   Bandwidth command and leaving the limitation are rejected.
2. GUID security is tried without an INF change first.
3. This record is wanted.
4. VM readings tick 34.3.
5. The figure is an estimate and is documented as one.

Taken in the design, not separately decided: NT 6.x-only registration; one
registration per connection on its first PDO; SuperSpeed against the
high-speed total divided by 10; `Ep.Interval` rather than the
specification's interval for a fast-polled pipe; no `WMILIB.SYS`; a
zero-bandwidth endpoint counting 0; and a split composite device's row
following its first function's devnode, a limitation rather than a moved
provider (section 5).

## 13. What changes in the documents (task 34.5)

- The release notes: the Vista/7 Advanced-tab limitation replaced by what
  the figure is, that it is an estimate, how SuperSpeed reads, and that a
  composite device's row goes with its first function's devnode.
- Design record 13 section 8: a pointer here for the Vista/7 page.
- `implementation-invariants.md`: the WMI registration lifetime beside the
  PDO's.
- `source-files.md`: `hcd_wmi.c`.
- `legal-provenance.md` section 4 carries this record's static rows.
