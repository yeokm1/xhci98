# The NT 6 x86 MSI helper

Design record for the x86 half of message-signalled interrupts on Windows
Vista and Windows 7 (roadmap-hcd Phase 36, tasks 36.15 and 36.16; the task
lines for this record's code and legs are the orchestrator's to number).
Revision 1, 2026-10-09, written before any code, on branch `p36-msi86`;
revision 2 the same day, after review round 1, revision 3 after round
2, revision 4 after round 3, and revision 5 after round 4, which
converged (section 14). **Revision 11, 2026-10-10**: section 12a, as
built. **Revision 6** records the owner's answers to
section 12 (2026-10-09): the import and the strip accepted, a reboot
requested by the INF (section 5), and mode 1 failing the start where no
line is offered and no helper is bound (section 4.3). **Revision 7**
takes review round 5 and the owner's further decision of the same day:
`XhciInterruptMode` 1 on its own forces the line on Vista and 7, x86
and amd64, through the same filter (section 4.2). Revision 8 takes
review round 6, revision 9 round 7, and revision 10 round 8, which
converged.

Design record 15 section 4.3 designs MSI for the HCD and fixes the seam
this record plugs into: `hcdMsgConnect(hc, connect, disconnect)`, handed
`IoConnectInterruptEx` and `IoDisconnectInterruptEx` by each
architecture's own caller, a NULL in either meaning "not found". On amd64
the caller asks `MmGetSystemRoutineAddress`. On x86 the one `xhci98.sys`
also loads on Windows 98, ME, 2000 and XP, so it can import neither
routine, nor `MmGetSystemRoutineAddress` (absent on 98 and ME), and four
review rounds of 36.5 found no safe way for it to reach them by itself
(record 15 section 13.5, rounds 1 to 4: a walk of kernel memory cannot
prove its pages mapped; a parse of the kernel's file proves where the
driver's own imports are, not where the target is).

**The owner's decision (2026-10-09, option A):** a small x86 helper driver,
installed only by the Vista and 7 x86 install path, imports the two
routines normally and hands them to `xhci98.sys` over a documented channel.
`xhci98.sys` stays the one x86 binary for 98, ME, 2000, XP, Vista and 7,
gains no import that Windows 98 lacks, and, with no helper (absent, failed
to load, refusing, or of another interface version), keeps the line
interrupt, the routing quirk and polling exactly as on XP. Rejected with
it: a second NT 6-only x86 build of `xhci98.sys` (breaks the one-binary
rule) and MSI on amd64 alone.

## 1. What is asked, and what is not

Asked:

- the helper itself: what it is, what it exports, how it is loaded and
  for how long;
- the channel between it and `xhci98.sys`, and how the two agree on a
  version when one is updated without the other;
- what `xhci98.sys` x86 does with what the channel returns, up to the
  seam, and what it does when the channel returns nothing - including
  keeping a message grant from reaching a start that cannot connect it;
- the toolchain that builds an x86 driver importing Vista-only routines;
- every pipeline stage a third binary touches: INF, gates, flavour
  marker, source stamp, packaging, release, provenance;
- the legs.

Not asked, and not changed here: the connection itself, the message ISR,
the one-message rule, the MSI/MSI-X capability read, the recorded fields
and the down-level reasoning, which are record 15 section 4.3's and apply
to x86 unchanged once the seam has the pair; amd64's routines, which
come from `MmGetSystemRoutineAddress` with no helper; and more than one
interrupter. Since revision 7 the requirements filter (section 4.2) is
not x86-only: it also serves mode 1 on amd64.

## 2. The helper

**Name.** `xhcimsi.sys` (8.3, so no Windows 98 media rule is strained by
its presence on the shared x86 media), service `xhcimsi`, sources in
`src\msi86\`. One device object, `\Device\Xhci98MsiHelper`, no symbolic
link.

**What it does.** Its `DriverEntry` creates the device object and sets
three dispatch routines; nothing else runs at load.

**The device object's security.** A named device object needs a security
descriptor of its own: `IoCreateDevice`'s documentation asks a named
object either for one set by the INF (which applies to a PnP stack, not to
this legacy control device) or for `IoCreateDeviceSecure`. The helper uses
`IoCreateDeviceSecure` (WDK 7.1's `wdmsec.lib`, the `wlh\i386` copy) with
the SDDL `D:P(A;;GA;;;SY)` - the system alone - a class GUID of the
project's own (generated once and fixed in `xhci_msi86.h`),
`FILE_DEVICE_UNKNOWN`, `FILE_DEVICE_SECURE_OPEN`, and `Exclusive` FALSE so
every controller's FDO can hold its own open. The HCD's open is a
kernel-mode create (section 3.3), which the access check does not stop;
anything else meets the descriptor first. The library's object and the
imports it brings are the gate's and the provenance record's (section 8.2).

It handles:

- `IRP_MJ_CREATE`: succeeds when `Irp->RequestorMode` is `KernelMode` and
  the file object's `FileName` is empty (no trailing path after the
  device name), else `STATUS_ACCESS_DENIED` or `STATUS_OBJECT_NAME_INVALID`.
  This is the second line behind the descriptor, not a replacement for it.
- `IRP_MJ_CLOSE` and `IRP_MJ_CLEANUP`: succeed.
- `IRP_MJ_INTERNAL_DEVICE_CONTROL` with the query code (section 3):
  fills the caller's block and completes synchronously. Every other code
  completes with `STATUS_INVALID_DEVICE_REQUEST`. Internal device control
  cannot be issued from user mode at all.

It never pends an IRP, allocates no pool, starts no thread, touches no
hardware, and calls neither routine itself: it takes their addresses.
Under WDK 7.1's headers `IoConnectInterruptEx` is declared
`DECLSPEC_IMPORT`, so `(PVOID)IoConnectInterruptEx` is the value the
loader wrote into the helper's import address table - the routine's
address in the running kernel, resolved by the loader from the kernel's
own export table. That is the whole of the "documented" in the channel:
the kernel's loader did the resolution, through the import mechanism
every driver relies on, on the kernel actually running. Because the
helper hands out kernel addresses rather than thunks of its own, nothing
the HCD later calls runs in the helper's image.

**Not unloadable.** It sets no `DriverUnload`, and a driver without one
cannot be unloaded (WDK: the I/O manager unloads a driver only through its
Unload routine). `xhci98.sys` also holds a reference on the helper's file
object for as long as it keeps anything the channel returned (section
3.3), so even a later version that gained an Unload routine could not be
unloaded under it. Neither is needed for the routines themselves, which
are the kernel's, but both are kept: they make "the helper answered
once" mean "the helper's device is there for the next query" on the same
FDO.

**NT 6 only.** Its imports are `ntoskrnl.exe` pairs that Vista SP2 x86 and
7 SP1 x86 export and Windows 2000 and XP do not (section 8.2), so on an
older kernel it cannot load at all; the INF never installs it there
(section 7). It also returns `STATUS_NOT_SUPPORTED` from `DriverEntry`
when `IoIsWdmVersionAvailable(6, 0)` is FALSE. That is a minimum-version
test: it refuses a pre-NT 6 kernel, which the imports already do, and
says nothing about a later one (the INF's `NTx86.6.0` also matches
Windows 8 and later x86, which this project does not target and on which
nothing here has been read).

### 2.1 The `qemu` flavour's injection

The helper has the three flavours like every binary here (section 8.1).
Its `qemu` flavour alone reads one `REG_DWORD`, `XhciMsiInject`, from
its service key's `Parameters` subkey in `DriverEntry`, through
`RtlQueryRegistryValues` (a `qemu`-only import row), and takes:

| Value | Effect | Leg (section 10) |
|---|---|---|
| 0 or absent | none | |
| 1 | `DriverEntry` fails (`STATUS_UNSUCCESSFUL`), no device | "failed load" |
| 2 | the device exists; the query completes `STATUS_INVALID_DEVICE_REQUEST` | "refused" |
| 3 | the query answers interface major 2 | "other major" |
| 4 | the query answers a `Returned` short of the routines | "old, short" |
| 5 | the query answers a NULL `IoDisconnectInterruptEx` | "malformed" |
| 6 | the query answers a wrong `Magic` | "malformed" |

Any other value is taken as 0. The `release` and `debug` flavours read no
registry and import no `RtlQueryRegistryValues`; the import gate's
`FLAVORS` column holds that.

## 3. The channel

### 3.1 The choice

Four channels were weighed.

| Channel | For | Against | Taken |
|---|---|---|---|
| Named device object, `IoGetDeviceObjectPointer`, an internal device control returning a versioned block | Documented end to end; no PnP coupling, so a missing or broken helper fails only the query; the reference it returns is the lifetime handle; internal device control is unreachable from user mode | One new import in `xhci98.sys` (section 3.4) | **Yes** |
| The helper as a lower filter on the controller's stack, answering `IRP_MN_QUERY_INTERFACE` | PnP orders the load (filters are loaded before the function driver's `AddDevice`); the query goes down the FDO's own stack with imports it already has | A `LowerFilters` service that is missing or fails to load fails the whole devnode (the stale-filter Code 39 class), which turns "helper absent" into "controller dead" - the exact opposite of the decision; and the filter must pass every PnP, power and configuration IRP the FDO sends down | No |
| A callback object (`ExCreateCallback`/`ExNotifyCallback`) | No device object | No Windows 98 evidence for the pair; a callback object has no lifetime tie to the helper's image | No |
| The helper pushes the table into `xhci98.sys` (an internal IOCTL to the HCD's FDO, found by interface arrival) | No new import in `xhci98.sys` | The interface arrives after `START_DEVICE`, too late for the requirements filter (section 4) and the connection; the helper would have to track the HCD's FDOs' lifetimes | No |

`IRP_MN_QUERY_INTERFACE` sent to a non-PnP device object was also
considered for the first row's request: it is a PnP minor code whose
contract assumes a PnP stack and the PnP manager's serialisation, neither
of which a legacy device object has, so the request is an internal device
control code of the helper's own.

### 3.2 The query

One code, `IOCTL_XHCIMSI_QUERY_V1 =
CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0901, METHOD_NEITHER, FILE_ANY_ACCESS)`,
whose function number carries the interface major (`0x900 + major`). An
incompatible change takes a new major and a new code, so an older helper
answers a newer HCD's query with `STATUS_INVALID_DEVICE_REQUEST` instead of
filling a block it does not understand.

The request is one block, in and out, in the caller's nonpaged memory,
passed as `Parameters.DeviceIoControl.Type3InputBuffer` with both buffer
lengths set to its size (both sides are kernel mode; no buffering is
involved). Declared once, in `src\xhci_msi86.h`, which both binaries
include, x86 only, C89, with `C_ASSERT`s on every offset in both builds:

| Offset | Field | Written by | Meaning |
|---|---|---|---|
| 0x00 | `ULONG Size` | caller | the size of the block the caller declares (0x28 for V1) |
| 0x04 | `USHORT CallerMajor` | caller | 1 |
| 0x06 | `USHORT CallerMinor` | caller | 0 |
| 0x08 | `ULONG CallerVersion` | caller | the HCD's `XHCI_VER_CSV` packed a.b.c.d, one byte each, for the helper's log only |
| 0x0C | `ULONG Magic` | helper | `'XMSI'` (0x49534D58) |
| 0x10 | `USHORT HelperMajor` | helper | 1 |
| 0x12 | `USHORT HelperMinor` | helper | 0 |
| 0x14 | `ULONG HelperVersion` | helper | the helper's `XHCI_VER_CSV`, packed as above |
| 0x18 | `ULONG Returned` | helper | bytes of the block the helper filled, from offset 0 |
| 0x1C | `PVOID ConnectEx` | helper | `IoConnectInterruptEx` |
| 0x20 | `PVOID DisconnectEx` | helper | `IoDisconnectInterruptEx` |
| 0x24 | `ULONG Flags` | helper | 0; bit 0 set by a `qemu`-flavour helper with an injection active |

`V1_MIN` is 0x28, the whole V1 block. A minor revision may append
fields and raise the caller's `Size`; it never moves or removes a V1
field, so the V1 prefix is the same in every minor.

**The caller (HCD)** zeroes the block, sets `Size` to its own block size
(0x28 today), its major, minor and version, and passes the block with
`InputBufferLength` and `OutputBufferLength` both equal to `Size`.

**The helper validates before it reads anything:** `Type3InputBuffer`
non-NULL; `InputBufferLength == OutputBufferLength`; that length
`>= V1_MIN` - all three before any byte of the block is read, so a
buffer shorter than the header is never dereferenced. Only then is
`Size` read (once) and required to equal the length, and the caller's
major checked. A failure completes with `Information = 0` and
`STATUS_BUFFER_TOO_SMALL` (a length below `V1_MIN`),
`STATUS_INVALID_PARAMETER` (any other length or `Size` failure) or
`STATUS_REVISION_MISMATCH` (a major it does not serve). It then writes
its fields for `n = min(Size, its own block size)` bytes from offset 0x0C
on (never the caller's fields), sets `Returned = n` and the IRP's
`Information = n`, and completes with `STATUS_SUCCESS`. The validation
and the fill are a pure function (`XhciMsi86Answer`) in the shared
DDK-free `src\xhci_msi86.c` that the helper compiles in and the host
suite tests: lengths 0 to 3 and 0x27 (refused before any read), unequal
lengths, `Size` disagreeing with the length, an unknown major, a longer
block from a newer caller (filled to its own size), and the block placed
at the end of a guarded host allocation so a read past it faults the
test.

**The HCD accepts the answer** only if all of these hold, tested in this
order and before any field after `Size` is used, a pure function
(`XhciMsi86Accept`, host vectors in `test_msi`) over the status, the
`Information`, the capacity it supplied, and the block:

1. the status is success;
2. `V1_MIN <= Returned`, `Returned == Information`, and
   `Information <= capacity` (the `Size` it sent);
3. `Magic` is `'XMSI'` and `HelperMajor` is 1;
4. `ConnectEx` and `DisconnectEx` are both non-NULL and differ.

A field past `V1_MIN` (a later minor's) is read only when `Returned`
covers it. Anything else is a refusal, recorded with the number of the
test that failed (section 9). Nothing in an answer is used until it is
accepted, and an accepted answer is copied into the controller (`hc`)
before the IRP is freed. Vectors: each test failing alone; a `Returned`
above the capacity; `Information` and `Returned` disagreeing; a longer
answer from a newer minor to a newer caller (accepted, its tail read only
where covered); a `Returned` of 0x24 (refused: `Flags` uncovered).

**The IRP** is built with the imports the HCD already has, at
PASSIVE_LEVEL: `IoAllocateIrp(device->StackSize, FALSE)`, `device` being
what `IoGetDeviceObjectPointer` returned (a NULL IRP is a failed look: the
reference is dropped and the state records `ABSENT`, "no IRP");
`Irp->RequestorMode = KernelMode`, `IoStatus.Status =
STATUS_NOT_SUPPORTED`, `IoStatus.Information = 0`; the next stack
location's `MajorFunction` `IRP_MJ_INTERNAL_DEVICE_CONTROL`, the code,
`Type3InputBuffer` the block (in the nonpaged controller extension), the
two lengths and the `FileObject`; a completion routine that sets a stack
`KEVENT` and returns `STATUS_MORE_PROCESSING_REQUIRED`, so the IRP is the
HCD's again after it; `IofCallDriver`; an unbounded
`KeWaitForSingleObject` (the helper never pends, as `HcdForwardAndWait`'s
waits rely on the lower driver); the status and `Information` read from
the IRP; `IoFreeIrp`. The HCD owns the IRP from allocation to free on
every path.

### 3.3 The reference

`IoGetDeviceObjectPointer(L"\\Device\\Xhci98MsiHelper", FILE_READ_DATA,
&file, &device)` opens the helper with a kernel-mode create and returns a
referenced file object (which references the device). The HCD keeps
`file`, `device` and the accepted block in `hc` while it is BOUND (section
4.1), and drops the reference with `ObfDereferenceObject(file)` at
`IRP_MN_REMOVE_DEVICE`, after the stop's release has run
`IoDisconnectInterruptEx` (record 15 section 4.3). Not at stop: a
rebalance stops the FDO and then sends it a new requirements filter and a
new start, and keeping the binding across the stop keeps those two in
agreement (section 4); a disable removes the FDO, and the enable's new FDO
looks again. A refused answer drops the reference at once.

### 3.4 What `xhci98.sys` x86 imports

The owner's wording is that `xhci98.sys`'s x86 imports stay unchanged.
The channel needs one new pair, `ntoskrnl.exe!IoGetDeviceObjectPointer`;
everything else it uses (`IoAllocateIrp`, `IoFreeIrp`, `IofCallDriver`,
`KeInitializeEvent`, `KeSetEvent`, `KeWaitForSingleObject`,
`ObfDereferenceObject`, `IoIsWdmVersionAvailable`) is already imported.
The pair is exported by every Windows 2000 SP4 kernel and has stock
Windows 98 SE precedent (`1394bus.sys` and `compbatt.sys` 4.10.2222, hint
48; design record 13's import census, row 43, "evidenced (stock 98 SE
precedent)"), so it keeps the property the decision protects - the one
binary still loads on 98, ME, 2000 and XP - and adds no NT 6 import. It
is called only when `IoIsWdmVersionAvailable(6, 0)` is TRUE.

No channel was found with no new import at all: the documented ways to
reach a named device object from kernel mode are this routine or
`ZwOpenFile` plus `ObReferenceObjectByHandle` (one new import either way,
and `ZwOpenFile` has no stock Windows 98 SE precedent in the census), and
a pointer published through the registry or any other store the HCD can
already read is not a documented channel and would let whoever can write
that store choose an address the kernel then calls. **Owner decision
(2026-10-09):** "imports unchanged" is read as "no import Windows 98
lacks", and the one evidenced pair is taken; the owner checked that
Windows 98 SE's `1394bus.sys` imports it and that `ntkern.vxd` carries the
name. The import gate's row stays, and the Windows 98 SE install leg is a
required check of it (section 10, D2). Its row cites the
Windows 98 SE evidence above; `1394bus.sys` is a tier C file of record 13's
census, so the gate's evidence list gains it (section 8.2).

## 4. The HCD's side: grant, look and connect

The difficulty the decision creates is that Windows decides between a
message and the line **before** `START_DEVICE`, from `MSISupported` in the
controller's hardware key, which the INF writes (section 7). Once a
message is granted there is no line in the start's resource list; record
15's table then has only "message not connected", which polls - not the
XP behaviour the decision asks for when there is no helper.

So the HCD looks for the helper before the grant, in
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS`, and when it is not BOUND, removes
the message-only configurations and the message-interrupt descriptors
from the requirements list there, so the arbiter grants the line the PCI
driver also offers - where it offers one, which section 11's R1 reads
before the strip is enabled (section 4.2). With the helper bound
the list passes untouched and Windows grants the message.

This is the documented purpose of that IRP: the function driver may change
the resource requirements list before the PnP manager assigns resources.
It arrives on the FDO at PASSIVE_LEVEL after `AddDevice` and before every
resource assignment, including a rebalance and the start after a disable
and enable.

### 4.1 The binding

Per controller FDO, `hc->Msi86State`:

| State | Meaning |
|---|---|
| `UNSEEN` | not looked yet (initial, at `AddDevice`) |
| `ABSENT` | `IoGetDeviceObjectPointer` failed (the status kept) |
| `REFUSED` | the helper answered and the answer failed section 3.2's test (which test, the status, `HelperMajor`, `Returned` kept) |
| `BOUND` | an accepted answer is held, with the reference |

**When it looks:** at each `IRP_MN_FILTER_RESOURCE_REQUIREMENTS` and each
`IRP_MN_START_DEVICE` while the state is not `BOUND`; never once `BOUND`,
until `IRP_MN_REMOVE_DEVICE` drops it. A look is only made on the x86
image (`#if !defined(_WIN64)`) with `IoIsWdmVersionAvailable(6, 0)` TRUE;
on 98, ME, 2000 and XP the state stays `UNSEEN` and nothing below runs.

### 4.2 The requirements filter

**The IRP's contract** (`IRP_MN_FILTER_RESOURCE_REQUIREMENTS` in the WDK):
a function driver handles it on the way up, after the lower stack, and
works on the list at `Irp->IoStatus.Information`. The stack location's
`IoResourceRequirementList` is not a fallback: it can describe an earlier
list, and is neither read nor freed here. A driver that changes the
list's size allocates a replacement from **paged** pool, frees the list it
replaces, and returns the replacement in `Information` with
`STATUS_SUCCESS`.

**Where it runs** (revision 7): on both images, NT 6 only
(`IoIsWdmVersionAvailable(6, 0)`), never on 98, ME, 2000, XP or XP x64.
**The strip is asked for** when either holds:

- **x86, the helper not bound** after this filter's look (the helper's
  purpose, sections 1 and 4); or
- **the effective mode is 1**, on x86 or amd64 (owner, 2026-10-09):
  mode 1 forces the line on Vista and 7 by itself, so a user no longer
  needs `MSISupported` 0 for it. The start has not run yet, so the
  filter uses the latest `XhciInterruptMode` reading cached at
  `AddDevice` or at the last start (below), and reads no registry
  itself. If the value changes
  between the filter and the start, the start's own reading governs and
  meets whatever was granted under record 15's tables and section 4.3:
  1 then 0 starts on the granted line in automatic mode, watcher and
  all; 0 then 1 may receive a message and takes section 4.3a's fallback;
  1 then 2 polls despite the line. Vectors for these three in
  `test_poll`'s decision table and the record's transitions.

**The filter-time mode read.** The start's reader, `hcdReadDword`, calls
`IoOpenDeviceRegistryKey`, which the WDK requires in a system thread.
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS` arrives at PASSIVE_LEVEL in an
arbitrary thread, so the filter reads no registry itself. Instead it uses
a value read where the WDK does promise a system thread: **`AddDevice`**
(documented as called in a system thread at PASSIVE_LEVEL) reads the raw
`XhciInterruptMode` DWORD and its status through the same
`hcdReadDword`, and **`IRP_MN_START_DEVICE`** (documented as arriving at
PASSIVE_LEVEL in a system thread; `HcdForwardAndWait` resumes on that
same dispatch thread) refreshes them when the start reads its values. The
filter applies record 15's strict validation and normalisation (the pure
function the start uses) to the latest raw value. No worker thread or
work item is involved, so nothing of the driver's runs after the filter
completes and no unload window is opened.

Two separate sets of fields hold it. The **cache** (`ModeCacheRaw`,
`ModeCacheStatus`, `ModeCacheFrom`: `AddDevice` or start) is written at
`AddDevice` and at each start. The **filter's snapshot**
(`FilterModeRaw`, `FilterModeStatus`, `FilterModeFrom`, `FilterMode`) is
copied from the cache by each filter, with that filter's sequence number,
and is what section 9 records as the last filter's mode; a start never
writes it. Neither is the start's `PollValues`, and nothing the start
settles (`hcdSettleDelivery` and the rest) is called from the filter. An
unreadable value is mode 0's, as at the start. A vector in
`XhciMsi86FilterRecord`'s sequences: cache 0, a filter records 0, a start
reads 1 (the snapshot still 0), the next filter records 1.

What that means for a change of the value: a disable and enable creates a
new FDO whose `AddDevice` reads the new value before its first filter, so
the documented way to change the mode (a disable and enable, section 4.4)
gives the filter the current value. A rebalance (stop, filter, start on
the same FDO) uses the value the last start read; a value changed while
the controller ran and not followed by a disable and enable can therefore
meet a rebalance's filter with the previous mode, and the start's own
fresh reading then governs as above. That is recorded (`FilterModeRaw`
beside the start's value), not prevented.

Otherwise (amd64 in modes 0, 2 and 3; x86 with the helper bound in those
modes) the filter walks, classifies and records the list (steps 1 to 5)
and changes nothing: outcome "not asked". This is the same code in
`xhci98.sys` on both images, and it does not depend on the helper. The
helper look (step 2) is x86's alone.

The filter:

0. **Resets the class** of the list kept in `hc` to UNKNOWN and advances
   the FDO's filter sequence number, before anything can return early;
   the class is UNKNOWN from `AddDevice` too. A class other than UNKNOWN
   is published only by step 6 of this same filter, with this filter's
   sequence number and outcome beside it, so the start never reads an
   earlier filter's class after a later filter that failed, found no
   list, or found an inconsistent one. Vectors (in the pure record
   function, `XhciMsi86FilterRecord`, shared `xhci_msi86.c`): a NO-LINE
   filter followed by a lower failure, by a NULL list, and by an
   inconsistent list, each leaving the class UNKNOWN for the start.
1. Forwards the IRP and waits (`HcdForwardAndWait`). If the lower status
   is a failure other than `STATUS_NOT_SUPPORTED`, it completes the IRP
   with that status and `Information` untouched, and records "lower
   failed".
2. On x86, looks for the helper (section 4.1) unless `BOUND`.
3. If `Information` is NULL: completes with the lower status, records
   "no list", and dereferences nothing.
4. **Walks the list's structure, checked, before anything else reads it**
   (`XhciMsi86Walk`, shared `xhci_msi86.c`): the header's `ListSize`
   against the fixed header size; then each configuration's header and
   `Count` and each descriptor against the bytes remaining, in
   overflow-safe arithmetic (a remaining-length counter decremented, never
   an end pointer computed from a count). A failure stops the walk where
   it fails: the outcome is "inconsistent", only the validated header
   fields and the failing configuration and descriptor index are
   recorded, nothing past the failure is read, and the IRP completes with
   the lower status and `Information` untouched. The snapshot, the trace
   and the strip only ever run over a list this walk accepted. Vectors: a
   `ListSize` below the header, a configuration header cut short, a
   `Count` larger than the bytes, a `Count` whose product with the
   descriptor size overflows, `AlternativeLists` larger than the bytes,
   and trailing bytes beyond the last configuration (accepted, ignored).
5. Records the list (section 9), whatever the state.
6. Classifies the list (below) - always, whether or not the strip is
   asked for or enabled - and keeps the class in `hc` for the start
   (section 4.3). Then, if the strip is asked for, computes the outcome
   (below); if not, the outcome is "not asked". Every outcome but "replaced" completes
   with the lower status and `Information` untouched; "replaced"
   allocates the replacement, writes it, frees the old list with
   `ExFreePool`, sets `Information` to the replacement and completes with
   `STATUS_SUCCESS`. A failed allocation turns "replaced" into "kept: no
   memory".

Outcomes, recorded by name: **not asked** (the helper is bound and the
mode is not 1, or amd64 in a mode other than 1: nothing changes, Windows
may grant the message); **disabled** (the strip is switched off,
below); **no line offered**, **no interrupt**,
**unsupported shape**, **inconsistent** (each: kept as is, below);
**no message** (nothing to strip: kept); **replaced**; **kept: no
memory**.

**The resource-choice rule, used everywhere in this record:** a
"replaced" outcome leaves only line interrupt choices, so, if resource
assignment succeeds, the controller is granted a line. Every other
outcome leaves the list's choices as Windows built them. Either way the
filter decides only what Windows may grant; the delivery is then settled
at the start by record 15's tables (section 4 and 4.3) from the effective
mode, the resource actually granted, the pin and the connection's
result - forced polling, a line, a message connected through the bound
helper's routines, the connection's own fallback to the line, a message
with no routines ("message not connected", polling - except under mode
1 on x86 with no line offered, which fails the start, section 4.3), a
message under mode 1 where no line was offered or the list was not
replaced (record 15's "line asked for, message granted", logged), or no
interrupt resource at all (mode 1 failing its start as today).

**The allocation** is one new named site, `HcdPoolAllocRequirements` in
`hcd_pool.c`, the HCD's only `PagedPool` allocation: the IRP's contract
asks for paged pool, and the PnP manager frees it. This is an exception
to design record 13 section 7.5's `NonPagedPool`-only rule, confined to
that site and that IRP; the allowlist's `SITES=hcd_pool.obj` already
covers it (the import is the same). **Owner decision (2026-10-09).**

**The strip** works at both levels of choice a requirements list has. An
`IO_RESOURCE_REQUIREMENTS_LIST` carries `AlternativeLists` complete
configurations (`IO_RESOURCE_LIST`s), of which the arbiter assigns one.
Inside a configuration each **requirement** is a run: a head descriptor
(its `Option` without `IO_RESOURCE_ALTERNATIVE`) and the descriptors after
it that carry `IO_RESOURCE_ALTERNATIVE`; every requirement must be
satisfied, by any one member of its run. Removing descriptors alone is
not enough: from `[BAR, MSI]` and `[BAR, INTx]` it would leave `[BAR]` and
`[BAR, INTx]`, and the arbiter could take the first and grant no
interrupt; and from `[BAR, INTx, MSI]` with two separate requirements it
would delete a requirement outright.

So each interrupt requirement is classed by its members (a message member
is one whose `Flags` carry `CM_RESOURCE_INTERRUPT_MESSAGE`, 0x0002,
declared by the HCD where the Windows 2000 DDK lacks it; MSI and MSI-X
both carry it): **line-only**, **mixed** (both kinds) or
**message-only**. Each configuration is then:

- **STRIPPABLE**: at least one interrupt requirement, none of them
  message-only. Stripping it removes the message members of its mixed
  requirements, the next kept member taking over as head where a head was
  removed, and copies every other descriptor unchanged
  (`CmResourceTypeDevicePrivate` included; R2 decides whether that is
  right before the strip is enabled, and nothing is inferred from
  adjacency).
- **MESSAGE-BOUND**: at least one message-only interrupt requirement. It
  cannot be stripped without deleting a requirement.
- **NONE**: no interrupt descriptor.

and the outcome is:

- **replaced**, when at least one configuration is STRIPPABLE and at
  least one configuration has a message member: the new list holds the
  STRIPPABLE configurations alone, stripped, in their original order
  (`AlternativeLists` reduced, `ListSize` recomputed). MESSAGE-BOUND and
  NONE configurations are dropped: the arbiter must not be able to pick a
  configuration that grants a message the start cannot connect, or none
  at all, while a line configuration exists.
- **no line offered**, when there are message members and no
  configuration is STRIPPABLE (an MSI-only controller with `Interrupt
  Pin` 0, or a controller with a pin whose list, against R1's
  expectation, offers no line). There is no line to give, and stripping
  would turn a message grant the start cannot connect into no interrupt
  at all, or delete a requirement; an empty list is never returned.
  Kept as is. The list's class, **NO-LINE** (message members and no
  STRIPPABLE configuration), is what the start reads (section 4.3): a
  message grant with the helper not bound polls with "message not
  connected" under modes 0 and 3, as record 15 has it, and under **mode
  1 on x86 the start fails**, as XP fails a forced-line start on a
  controller with no line (owner, 2026-10-09). On amd64, and on x86 with
  the helper bound, mode 1 with a NO-LINE list runs the message and logs
  that the line was asked for (record 15's row).
- **no interrupt**, when no configuration has an interrupt descriptor:
  kept as is; record 15's no-interrupt rows apply, as on XP.
- **unsupported shape**, when some configuration has a message member and
  the list also has a shape the reading of R1/R2 did not admit (for
  instance a message-only requirement beside a STRIPPABLE configuration
  that R1 never showed): kept as is, the shape recorded. Until R1 and R2
  are read every shape is admitted only through the `qemu` switch below.
- **inconsistent**, when `ListSize` is short of what the counts claim:
  kept as is.

The classification and the strip are one pure function over the list's
bytes (`XhciMsi86Strip`, in the shared DDK-free `xhci_msi86.c`, layouts
declared with `C_ASSERT`s against the DDK's
`IO_RESOURCE_REQUIREMENTS_LIST`, `IO_RESOURCE_LIST` and
`IO_RESOURCE_DESCRIPTOR`), a sizing pass then a writing pass, with host
vectors in `test_msi` and each expected outcome stated: one configuration
with a message-then-line run and one with a line-then-message run
(replaced, the line heading the run); `[BAR, MSI]` and `[BAR, INTx]` in
both orders (replaced, the `[BAR, MSI]` configuration dropped whichever
comes first); an MSI-X member; a message member with several messages;
`[BAR, INTx, MSI]` as two requirements, alone (no line offered) and
beside `[BAR, INTx]` (replaced, the two-requirement configuration
dropped); a message-only list (no line offered); `[BAR]` (NONE) beside
`[BAR, MSI|INTx]` (replaced, the NONE configuration dropped); `[BAR]`
beside `[BAR, INTx]` with no message anywhere (no message, kept as is);
a list with no interrupt (no interrupt); a line-only list (no message);
the walk's failures (inconsistent). These are the transformation's own
vectors. The admitted-shape check (below) has its own: each shape R1 and
R2 admit, transformed; and a STRIPPABLE list of a shape not admitted
(unsupported shape, kept as is).

**The strip is enabled only after R1 and R2 are read** (section 11). The
first build carries a `qemu`-only switch, `XhciMsi86NoStrip`, under which
the filter records and does not strip (outcome "disabled"); R1 and R2 are
read from the complete trace of section 9 on both guests; the shapes they
show are written into this record as the admitted shapes, by a review
round, before the code is taken as done, and any other shape takes
"unsupported shape". If R1 shows that the PCI driver offers no line
configuration for a controller with a pin, the strip is withdrawn: the
switch's "disabled" becomes the shipped default, the class is still
computed and recorded (so such a list is NO-LINE and section 4.3's
mode-1 rule applies to it), the helper-less start takes record 15's
"message not connected" row under modes 0 and 3, mode 1 can no longer
force the line where `MSISupported` is 1, and the owner is told that
"exactly as on XP" cannot hold for a helper-less install with
`MSISupported` 1. The mode-1 strip and the helper-less strip are one
mechanism and are enabled, admitted and withdrawn together.

### 4.3 The start

At `START_DEVICE`, after `hcdParseResources` (record 15 section 4), on the
x86 image:

- **A line resource:** `IoConnectInterrupt` exactly as today, whatever the
  state; the helper's routines are used under a message grant only (record
  15 section 4.3 uses `IoConnectInterruptEx` for a message grant alone).
  The routing quirk and the watcher apply (`hc->IntDelivery` is line).
- **A message resource:** a look if not `BOUND` (a helper loaded between
  the filter and the start, or a filter that never came); then
  `hcdMsgConnect(hc, ConnectEx, DisconnectEx)` with the accepted block's
  pair when `BOUND`, else `hcdMsgConnect(hc, NULL, NULL)`, which record 15
  defines as "not found": that start polls with "message not connected"
  - with one exception, below. A message grant **without a bound
  helper** comes from a list the
  filter kept on purpose (outcomes "no line offered", "unsupported
  shape", "inconsistent", "disabled", "kept: no memory", "no list",
  "lower failed") or from a filter that never came; the record says which
  (section 9).
- **Mode 1, no line, no helper: the start fails** (owner, 2026-10-09).
  When the image is x86, the effective mode is 1, the resource is a
  message, the helper is not bound after the start's look, and this FDO's
  latest filter classified its list NO-LINE (section 4.2: not UNKNOWN, and
  published by that filter), the start fails as a forced-line start with no
  line fails on XP: reason "forced, no line offered", the delivery none,
  the release run, `STATUS_DEVICE_CONFIGURATION_ERROR` as record 15's
  "forced, no interrupt" row returns. The condition is narrow on purpose.
  Every other way a message grant meets an unbound helper under mode 1 -
  a list that offered a line but was not replaced (the strip off before
  R1/R2, an unadmitted shape, no memory, no list, inconsistent) or a
  filter that never ran - keeps record 15's row and polls, because a line
  existed or may have existed and the failure would then be this
  driver's, not the controller's. **A bound helper whose connection
  fails still polls**, as record 15 has it, under every mode: the
  exception is the unbound case alone. A message grant with the helper
  bound and connected under mode 1 runs the message and logs it, as
  record 15 has it. A list with no interrupt at all (outcome "no
  interrupt") already fails mode 1 through record 15's "forced, no
  interrupt" row.

  The failed start is recorded before it returns: the start emits a
  **start-decision event** (section 9, kind `S`) carrying the mode, the
  grant (line, message, none), the binding state, the last filter's
  class and outcome, the delivery, the reason and the status, before the
  failure path runs, and the same fields are kept in `hc` (the counter
  block of a start that failed is still read by the stop dump and
  `HcdCountersPoll`, as the core's refused start is today).

  The decision stays in record 15's one pure function: `XhciPollDecide`
  (on p36-poll's head, `src/xhci_poll.c`) gains one input, "no line was
  offered" (TRUE only on the x86 image under the condition above, FALSE
  on amd64 and on every other target), and returns
  `XHCI_POLL_DELIVERY_NONE` with a new reason,
  `XHCI_POLL_R_FORCED_NO_LINE_OFFERED` (appended to the reason list),
  for mode 1 with a message not connected and that input TRUE, inside
  its unconnected-message branch and after its forced-poll check. The
  x86 caller passes TRUE only for the unbound NO-LINE case above, and
  FALSE for a bound helper whose connection failed. Vectors in
  `test_poll`: mode 1 with the input TRUE fails; modes 0, 2 and 3 with it
  TRUE poll (2 as forced); mode 1 with it FALSE polls as today; mode 1
  with the message connected runs it whatever the input.

### 4.3a Mode 1 forces the line (owner, 2026-10-09)

With the strip enabled, mode 1 on Vista and 7 (x86 and amd64) asks the
filter for the line, so the start under mode 1 normally receives a line
resource and runs it with record 15's forced-line rows (the routing
quirk, the watcher's mode-1 rule: no move). Record 15's "1 under a
message grant runs as 0 and logs that the line was asked for" stays, as
the fallback where a message is granted anyway: a NO-LINE list, the
strip disabled or withdrawn, an unadmitted shape, no memory, no list, an
inconsistent list, or a filter that never ran - except x86 with no helper
bound and a NO-LINE list, which fails (section 4.3). Mode 1 never
produces an empty list. `MSISupported` 0 keeps working as before, and
on XP x64 and the down-level targets mode 1 is unchanged.

Everything after the seam - the connection, its fallback to the line, the
release through `IoDisconnectInterruptEx`, the ISR, the capability read -
is record 15 section 4.3's, unchanged. The x86 caller passes the pair as
`PVOID`s cast to the seam's function-pointer types (`NTAPI`, one pointer
argument each), which are the routines' own signatures in WDK 7.1's
`wdm.h`; record 15's structure declarations already carry x86 size and
offset checks ("for both architectures").

### 4.4 What is not changed on x86

- The amd64 image has no helper and no look; it reaches the seam through
  `MmGetSystemRoutineAddress` (record 15). Since revision 7 it runs the
  filter, and strips only under mode 1 (section 4.2).
- `XhciInterruptMode` keeps record 15's meaning, with section 4.3a's
  mode 1. Mode 2 polls even with a message granted. The filter consults
  the mode for 1 alone. The supported way to change it is a disable and
  enable, whose new FDO reads the value at `AddDevice` before its first
  filter; a same-FDO rebalance's filter uses the previous start's reading
  (section 4.2), and in either case the start's tables meet whatever was
  granted.
- `MSISupported` 0, set by a user, removes the message descriptors before
  the HCD sees the list; the filter then has nothing to strip, and the
  line runs as on XP, helper or not.

## 5. Load order

The helper must be loaded before the controller's first
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS` of a boot.

**The helper is boot-start** (`StartType` 0, `SERVICE_BOOT_START`), with
`ErrorControl` 1 and no `LoadOrderGroup`. `xhci98.sys` is demand-start in
group `Base` (`[Xhci.AddService]`), loaded by the PnP manager when the
controller is enumerated. Boot-start drivers are loaded by the boot loader
and initialised before the system-start phase in which the PnP manager
loads non-boot function drivers for enumerated devices, so on every boot
of a configured machine the helper's device exists before `xhci98.sys`'s
`AddDevice`. A failure to load the helper (file missing, corrupt, or its
`DriverEntry` failing) with `ErrorControl` 1 logs an event and continues
the boot; it never triggers a last-known-good fallback.

`SERVICE_SYSTEM_START` with an early group was considered and not taken:
the relative order of system-start groups and the PnP manager's loading
of demand-start function drivers is not one this project has read, and
boot-start removes the question. The cost of boot-start is that a defect
in the helper's `DriverEntry` affects every boot; the helper's
`DriverEntry` creates one device object and returns, which is what keeps
that cost acceptable, and section 10's Driver Verifier leg reads it.

**The first install, and the reboot it asks for** (owner, 2026-10-09).
SetupAPI does not start a boot-start service when it installs it, so on a
fresh install, and on an update from a package without the helper, the
helper is not running until the next boot. **The INF therefore requests a
reboot**, so that the helper is active from the next boot on.

*The mechanism* is the documented `Reboot` directive of an install
section: `[Xhci.Dev6.NTx86]` carries `Reboot`, which makes setup report
that a restart is required (`DI_NEEDREBOOT`) and the user is asked to
restart. It is that section's only, so Windows 98, ME, 2000 and XP (which
never reach it) and the root hub's and hubs' sections never ask, and the
64-bit INF does not change. The directive is unconditional: a plain INF
cannot ask only when the helper is newly installed (that would need a
co-installer, which this project does not ship), so **every** install and
update through the Vista and 7 x86 controller section asks for a restart.
That also covers the update whose helper changed, since a running helper
is replaced only at the next boot (section 6). The copy and service flags
were considered as a conditional mechanism and found none: no
`AddService` flag asks for a reboot, and a kernel driver's image is not
held open while it runs, so overwriting `xhcimsi.sys` does not make setup
schedule one by itself. `SPSVCINST_STARTSERVICE` on the helper's
`AddService` line (start a non-PnP service at once) would make the helper
active without any restart; it is recorded here as the alternative and
not taken, because the owner decided on the reboot.

*Until the reboot* the controller keeps what section 4.2's rule gives it
(the filter finds the helper `ABSENT`): the line when the list is
replaced, otherwise the grant and record 15's rows decide. Whether
Windows Vista and 7 start the device at all before the restart, or hold
it with "restart required" (problem code 14) once setup has reported
`DI_NEEDREBOOT`, is not documented in a form this project has read; it is
reading R4 (section 11), taken on both guests for a fresh install and for
an update. If R4 shows the device held until the restart, the record
says so and the owner is told that the pre-reboot state is "not started"
rather than line or polling; nothing else in the design changes.

**Late arrival** is covered by the looks of section 4.1: a filter or
start after the helper appears binds it, without a reboot, at the next
disable and enable of the controller.

**Unsigned on x86.** Vista and 7 x86 do not enforce kernel-mode code
signing on load, and `xhci98.sys` x86 already loads unsigned there; that
an unsigned **boot-start** helper loads on both is read on the guests
before the code is taken as done (section 11, R3). If it does not, the
helper becomes system-start and this section is reopened.

## 6. Versions and mixed installs

Both binaries carry the package's version (`XHCI_VER_CSV`) and both are in
one INF's copy list, so an install replaces both. The version check is the
interface's, not the files': the HCD binds any helper whose major is 1 and
whose block covers what it uses, whatever its file version, and logs both
versions. The cases:

| Installed | Running | Result |
|---|---|---|
| this package, freshly | no helper until the next boot | a restart requested; until it, section 4.2's helper-less resource choice with delivery settled at the start (or the device held for the restart, R4); after it the helper is bound (section 5) |
| this package, updated from one with the helper | the old helper until the next boot (the new image is loaded at the next boot) | a restart requested; until it, bound to the old helper if its major is 1, else section 4.2's helper-less outcome |
| a later package with interface major 2 | old HCD, new helper | the old HCD's V1 code is refused by a V2-only helper: section 4.2's helper-less outcome. A later helper that keeps answering V1 (the rule for a minor change) binds |
| this HCD, the helper's file removed or its copy failed | none | section 4.2's helper-less resource choice, delivery settled at the start, recorded `ABSENT` |
| a package older than this one installed over it | old HCD, this helper still loaded | the helper idles; but see below |
| the device uninstalled | the helper service remains, boot-start | it loads, idles, and answers nobody; removing it is a manual step the release notes give (`sc delete xhcimsi`, then reboot) |

**The downgrade hazard.** The INF's `MSISupported` 1 lives in the
controller's hardware key and, under `FLG_ADDREG_NOCLOBBER`, stays there
when an older package is installed over this one. An HCD older than this
release does not filter and does not know a message resource, so it would
start with a message grant it treats as a line. The same holds on amd64
for record 15's INF lines. Neither INF can undo it on the older package's
behalf. The release notes tell a user going back to an earlier version on
Vista or 7 to uninstall the device ("delete the driver software" ticked),
which removes the hardware key, or to set `MSISupported` 0 first; leg U2
reads what 2.2.0.2 actually does with the grant, so the note can say how
it fails, and reads the procedure itself before the note calls it safe. **For the orchestrator:** this is common to both architectures
and belongs beside record 15 section 4.3's INF paragraph as well.

## 7. The INF

Only `src\xhci98.inf` changes, and only on its Vista and 7 x86 path; the
64-bit INF gets no helper.

```
[SourceDisksFiles]
xhci98.sys=1
xhcimsi.sys=1
xhci98.inf=1

[DestinationDirs]
...
XhciMsi.CopyFiles=10,System32\Drivers

[Xhci.Dev6.NTx86]
AddReg=Xhci.AddReg.NT
CopyFiles=Xhci.CopyFiles,XhciMsi.CopyFiles
Reboot

[Xhci.Dev6.NTx86.HW]
AddReg=Xhci.Msi.AddReg

[Xhci.Dev6.NTx86.Services]
AddService=xhci98,0x00000002,Xhci.AddService
AddService=xhcimsi,,XhciMsi.AddService

[XhciMsi.AddService]
DisplayName=%XhciMsiSvcDesc%
ServiceType=1                       ; SERVICE_KERNEL_DRIVER
StartType=0                         ; SERVICE_BOOT_START
ErrorControl=1                      ; SERVICE_ERROR_NORMAL
ServiceBinary=%12%\xhcimsi.sys

[XhciMsi.CopyFiles]
xhcimsi.sys,,xhcimsi.tmp

[Xhci.Msi.AddReg]
; record 15 section 4.3's four lines, unchanged
```

- **Only the controller's section** names the helper: not
  `RootHub.Dev6.NTx86`, not `Hub.Dev6.NTx86`, not the 2000/XP `.NTx86`
  sections, not the undecorated Windows 98 sections, not
  `DefaultInstall` or `DefaultInstall.NTx86`.
- **The `.HW` section on x86 comes with the helper.** The MSI lines are
  written on x86 only by the section that also installs the helper, so a
  package can never ask for a message on x86 without shipping the means
  to connect it. (Without the helper the filter still keeps the grant
  away, so this is the second line of defence, not the first.)
- **Windows 98 and ME** read `[SourceDisksFiles]`, and 2000 and XP read
  it too; none of their sections copies `xhcimsi.sys`, but an entry they
  never use has not been read on their engines, so the four down-level
  install legs are re-run (section 10, D2).
- Section names stay within the Windows 98 engine's length rule
  (`W98-SECTLEN`), and `xhcimsi.tmp` follows the temporary-name rule the
  gate applies to `xhci98.tmp`.

## 8. Build, gates, packaging and release

### 8.1 The toolchain

**WinDDK 7.1, x86, Vista target (`setenv.bat <WDK> fre|chk x86 WLH
no_oacr`), for the helper alone.** The x86 HCD and `xhciuas.sys` stay on
MSVC 6 and the Windows 2000 DDK.

Why WDK 7.1 x86 and not MSVC 6 with the 2000 DDK:

- The 2000 DDK has neither the declarations nor an import library entry
  for `IoConnectInterruptEx`/`IoDisconnectInterruptEx`. Building them
  with MSVC 6 would need a hand-made import library (`lib /def:` with
  stdcall decoration mapped to the undecorated export) and hand-copied
  declarations - two artifacts of this project's own making standing
  exactly where the import has to be right. WDK 7.1's `lib\wlh\i386\
  ntoskrnl.lib` and `wdm.h` are Microsoft's own, for this target.
- WDK 7.1 is already in `tools\WinDDK71`, already the amd64 toolchain,
  already in `legal-provenance.md`'s toolchain row; its x86 compiler
  (`bin\x86\x86\cl.exe`) and the `wlh\i386` libraries are in the same
  unpacked kit. No new third-party input.
- The helper is NT 6 only, so none of the reasons the HCD x86 build is
  pinned to MSVC 6 (Windows 98's loader, the compiler helpers, the
  1998-era runtime assumptions) bind it.

`WLH` (Vista) rather than `WIN7`: the helper must load on Vista SP2, and
the `wlh` libraries can only have the Vista exports. **The `/GS` cookie
stays on**, the kit's default (`BUFFER_OVERFLOW_CHECKS` unset, so
`GsDriverEntry` and `BufferOverflowK.lib` initialise the cookie): the
`wdmsec.lib` members that `IoCreateDeviceSecure` pulls in are precompiled
with cookie checks (a static reading of the `wlh\i386` library in review
round 2 found `___security_cookie` and `@__security_check_cookie@4`
references in `ppregstate.obj` and `cmregutil.obj`), and turning the
checks off for the helper's own source would leave those library members
checking a cookie nobody initialised. Which library members and imports
the link actually retains is read from the first link's map and
`dumpbin /imports`, and the gate and the provenance record are written
from that reading, not from this paragraph.
`C_DEFINES` sets the flavour defines as `src\uas\sources` does, from
`BUILD_ALT_DIR`, and refuses any other value.

The cost: an x86 build now needs `tools\WinDDK71` as well as MSVC 6 and
the 2000 DDK. `build-driver.cmd` fails an x86 build whose WDK is missing,
with a message naming it; there is no switch to skip the helper, because
a package whose INF names `xhcimsi.sys` and does not carry it is refused
by `make-package.ps1` anyway.

### 8.2 The stages

| Stage | Change |
|---|---|
| `scripts\build-driver.cmd` | `:buildmsi %FLAVOR%` after `:builduas`, x86 runs only: `DDKROOT` for that call is `tools\WinDDK71` (or `WDKROOT`), `setenv` args `x86 WLH no_oacr`, the same `ALTDIR`, `cd src\msi86 && build -cZ`; then the import gate with `-AllowPath xhcimsi-imports.allow -Profile nt6x86`, `check-flavour-marker.ps1`, `source-stamp.ps1 -Driver xhcimsi -Write`. An `-amd64` run builds no helper |
| Import gate | A new profile `nt6x86` in `check-imports.ps1`: an x86 image checked against the export tables of Vista SP2 x86 and 7 SP1 x86 `ntoskrnl.exe`/`ntkrnlpa.exe` and `hal.dll` (a new `nt6x86-baselines.expected`, extracted by `extract-target-baselines.ps1`; `ntkrnlpa.exe` of both is to be extracted, today's `tools\vista-x86-extracted` and `win7-x86-extracted` carry `ntoskrnl.exe` only), with no Windows 2000 or Windows 98 evidence asked. `xhcimsi-imports.allow`: `IoConnectInterruptEx`, `IoDisconnectInterruptEx`, `IofCompleteRequest`, `IoIsWdmVersionAvailable`, `RtlInitUnicodeString` (`all`), the pairs `wdmsec.lib`'s `IoCreateDeviceSecure` object brings (listed from the first link's `dumpbin /imports`, each with its own evidence, none admitted by name in advance), `RtlQueryRegistryValues` (`qemu`), each tagged `vista-x86-export; win7-x86-export`; the profile refuses any `xhci98-imports.allow` row being reused for it. `xhci98-imports.allow` gains `ntoskrnl.exe!IoGetDeviceObjectPointer` (`all`, `w2k-export; win98-precedent 1394bus.sys 4.10.2222`), and `win98-evidence.list` gains `1394bus.sys`. Self-test mutations in `test-flavour-rules.ps1` for the profile (a Windows 2000-only pair passes nothing; a Vista pair in the HCD's x86 list fails) |
| INF gate `check-inf.ps1` | New `MSIHLP-*` rules, x86 only: `MSIHLP-ONLYNT6` (the helper's service, copy section or file named anywhere but `Xhci.Dev6.NTx86`, `[SourceDisksFiles]` and `[DestinationDirs]`); `MSIHLP-SVC` (the second `AddService` on `Xhci.Dev6.NTx86.Services` only, flags 0, `ServiceType` 1, `StartType` 0, `ErrorControl` 1, `ServiceBinary=%12%\xhcimsi.sys`, copied by that section's own `CopyFiles`); `MSIHLP-HW` (on x86, record 15's `MSI-*` lines only in `Xhci.Dev6.NTx86.HW`, and only when `MSIHLP-SVC` holds); `MSIHLP-AMD64` (no helper in the 64-bit INF); `MSIHLP-REBOOT` (`Reboot` present in `Xhci.Dev6.NTx86` and in no other section of either INF, so no Windows 98, ME, 2000 or XP path and no hub section asks for a restart; the footprint records it). `PATH-NT`'s "exactly one `AddService`, flag 0x2, `StartType` 3" is relaxed for that one section by exactly the helper's line. `BOTH-VERSION` reads `src\msi86\xhcimsi.rc` too. Mutations in `test-inf-checks.ps1` for each rule; the `Dev6` mutations rebased on the new section text. **The cross-architecture media rule:** `test-inf-checks.ps1`'s requirement that both INFs declare the same `[SourceDisksFiles]` (~2005) admits exactly one difference, `xhcimsi.sys` in the 32-bit file only, and a mutation proves any other difference still fails. **The footprint models `.HW`:** `-EmitFootprint` today emits a `.HW` section as unmodelled (`check-inf.ps1` ~2575); it is extended to walk a `.HW` section's `AddReg` and emit its values as **hardware-key** writes (distinct from the software-key `HKR` of the install section), with their flags, so `MSISupported`, `MessageNumberLimit` and `FLG_ADDREG_NOCLOBBER` appear in `expected-footprint.txt`; the helper's service appears as a service the package creates (its removal on uninstall is a reading, section 10, not a footprint claim). Mutations: each MSI value dropped, its NOCLOBBER flag dropped, the `.HW` section moved to the root hub. The amd64 footprint takes the same `.HW` modelling for record 15's lines (the orchestrator's to coordinate with record 15's own `MSI-*` rule) |
| Flavour marker | The helper embeds `XHCI98_FLAVOUR_{RELEASE,DEBUG,QEMU}` as `uas_entry.c` does; `check-flavour-marker.ps1` unchanged |
| Shared source | `src\xhci_msi86.c` is compiled into the HCD by `src\sources` and into the helper by `src\msi86\sources` as `..\xhci_msi86.c` (build.exe accepts a relative path in `SOURCES`), and into `test_msi` by the host build. It is C89 and DDK-free, so the 2000 DDK's MSVC 6 and WDK 7.1's compiler both take it |
| Source stamp | `source-stamp.ps1 -Driver` gains `xhcimsi` (`src\msi86`, `xhcimsi.srcstamp`, with `xhci_msi86.h` and `xhci_version.h` as parent headers). Its `SOURCES` parser today splits on every backslash (`source-stamp.ps1` ~108), which would break `..\xhci_msi86.c` into two tokens; it is changed to remove only line-continuation backslashes and keep path separators, so the stamp hashes the parent `src\xhci_msi86.c` itself. Mutations: a change to `src\xhci_msi86.c` alone invalidates both the helper's stamp and the HCD's; the existing stamps of `xhci98` and `xhciuas` are unchanged by the parser change |
| Charset gate | `check-source-charset.ps1`'s object-directory skips gain `^src\\msi86\\obj` |
| Version resource | `src\msi86\xhcimsi.rc` includes `../xhci_version.h`, `FILEVERSION`/`PRODUCTVERSION` from the macros, `VS_FF_DEBUG` for checked builds, names `xhcimsi.sys` |
| INF gate, the bare directive | `MSIHLP-REBOOT` and the footprint must recognise `Reboot` as a bare directive: the existing `Get-Directive` helpers and the footprint's generic loop read only `key=value` entries, so both gain a bare-entry reader, with a mutation for a `Reboot` moved to another section and one for it removed |
| `make-package.ps1` | `-Arch x86` stages `xhcimsi.sys` (the staging table; `$MsiDriverPath`), gated like the UAS pair: exists, flavour marker, import gate with the `nt6x86` profile; `-Arch amd64` refuses one |
| `make-release.ps1` | **Media per leg, not per release.** Today the declared media are derived once, from the first published INF, for every upload leg, on the strength of the two INFs' equal `[SourceDisksFiles]` (~1351). That becomes per leg: each leg's publishable set and media layout come from that leg's own INF (x86 legs: `xhci98.inf`, `xhci98.sys`, `xhcimsi.sys`; x64 legs: as today), through upload assembly and `-UploadSetOnly`, and a historical release whose INFs declare no helper is reconstructed exactly as before. `Assert-MsiHelperPublished`, modelled on `Assert-UasPublished`, per x86 leg: the file exists, its `FileVersion` equals the version, `VS_FF_DEBUG` matches the flavour, the marker, bytes equal to `src\msi86\obj...\i386\xhcimsi.sys`, `source-stamp -Driver xhcimsi -Check`. An x64 leg holding a helper is refused. The readme and contents text name it |
| `test-package.ps1` | Cases: an x86 package with the helper and an x64 one without, assembled in both leg orders; an x86 package missing the helper (refused); a helper in an x64 package (refused); a historical upload set without the helper (reconstructed unchanged); a helper of the wrong flavour (refused); a helper whose source stamp is stale, and one swapped for another build's bytes (refused) |
| `legal-provenance.md` | Section 2's toolchain row: WDK 7.1 now also builds an x86 binary (the helper), with the `/GS` cookie support the amd64 build also links (`BufferOverflowK.lib`, under the amd64 row's open notice question) and the `wdmsec.lib` members its link retains, listed from the link map (stated as facts); section 4: a row for the NT 6 x86 export readings (static: the export tables of the four kernels and two HALs) and for R1-R3 (runtime, through the driver's own log) |
| Pool | `hcd_pool.c` gains `HcdPoolAllocRequirements`, the one `PagedPool` site (section 4.2); record 13 section 7.5 and AGENTS.md's pool paragraph name the exception |
| Host tests | `test_msi` (record 15's suite) gains `XhciMsi86Answer`, `XhciMsi86Accept`, `XhciMsi86Strip` and `XhciMsi86BindStep`, all in the shared DDK-free `src\xhci_msi86.c`; `test\run-host-tests.cmd` unchanged otherwise |

### 8.3 Shared tables touched (for the integrator)

Appended, never reordered: the counter block's new fields (section 9);
`XHCISNAP`'s names for them (36.9); the INF gate's rule list and its
footprint; the import allowlist's one row; `sources` (`hcd_msi86.c`); the
`test_msi` vector list.

## 9. What is recorded

**Where it lives.** The filter runs before the start, and the start's
`HcdCountersStart` zeroes the whole counter block (`hcd_ctl.c` ~1714,
`hcd_log.c` ~425), as it zeroes the core's extension. So what the filter
and the looks find is kept in persistent controller state (`hc`), outside
both areas a start resets, and republished into the counter block and the
start's log note after `HcdCountersStart` on every start; each later
filter or look updates `hc` and, when the controller is started, the
block too. `XHCISNAP` reads it from `XhciLogVerbosity` 1 (field names are
36.9's).

**The binding:** `Msi86State` and its detail - the
`IoGetDeviceObjectPointer` status (or "no IRP") for `ABSENT`; the failed
acceptance test's number, the IRP status, `Information`, `Returned` and
`HelperMajor` for `REFUSED`; `HelperMajor`, `HelperMinor`,
`HelperVersion` and `Flags` (a `qemu` helper says so) for `BOUND`; the
number of looks made on this FDO.

**The last filter's list snapshot**, bounded, for every flavour:
`AlternativeLists`, and for up to 4 configurations and 12 descriptors in
all, each descriptor's configuration index, descriptor index, `Type`,
`Option`, `Flags`, `ShareDisposition` and the 24 bytes of its `u` area
raw, labelled by each architecture's WDK 7.1 `IO_RESOURCE_DESCRIPTOR`
layout (`wdm.h`): for an interrupt `MinimumVector`, `MaximumVector`,
`AffinityPolicy`, `PriorityPolicy` and `TargetedProcessors`, the last a
32-bit `KAFFINITY` at +16 on x86 and a 64-bit one at +16 on amd64; for a
device-private descriptor its data words),
with a truncation flag and the totals when there were more; then the
filter's sequence number, class and outcome by name (section 4.2), for
"replaced" the configurations and descriptors removed, and the
filter-time mode: `FilterModeRaw`, `FilterModeStatus`, `FilterMode`, kept
apart from the start's values. `XHCISNAP` labels the words by
that layout. A truncated snapshot is evidence that something is missing,
not evidence of what.

**The complete trace, for R1 and R2:** the `qemu` flavour also writes
every descriptor of every configuration of the list it received, and of
the list it returned, on the debug channel (the port-`0xE9` mirror,
captured by the QEMU launcher to a host file, which survives the guest).
That channel writes a byte at a time with no serialisation
(`xhci_dbg.c` ~120) and formats into a 128-byte buffer, so the trace is
built to be checked rather than trusted:

- one record per line, fixed layout, `M86` then fields in hex:
  record kind (`B` list begin, `C` configuration, `D` descriptor, `E` list
  end, `L` lifecycle), the FDO serial, a per-FDO filter sequence number,
  `R` or `T` (received or returned list), indices with their expected
  counts (`3/5`), the descriptor's fields and six `u` words, and last a
  two-digit checksum of the record's preceding bytes;
- the longest record is under 128 bytes by construction, and a
  `C_ASSERT` on the format's maximum keeps it so;
- the `B` record carries the configuration count and `ListSize`, each
  `C` its descriptor count, and `E` the total of `D` records written, so
  a missing record is visible;
- emitted from the filter at PASSIVE_LEVEL through the existing
  `xhci_dbg.obj` writer only (the one `qemu` site the import gate admits
  for `WRITE_PORT_UCHAR`), never from an ISR or DPC.

A host validator (`scripts\vm-matrix\check-msi86-trace.ps1`) rejects a
capture with any record whose checksum or layout fails, whose counts
disagree, or that is cut, and reports interleaving with other writers'
bytes as a failed record; a rejected capture is retaken, and R1, R2 and
the L legs are closed only from a capture it accepts. The validator has
self-tests (a clean capture, an interleaved byte, a dropped `D`, a
truncated line).

**Lifecycle events**, `qemu` flavour, the same channel and record format
(kind `L`), each with the FDO's serial: look (result), bind, refuse, the
start's routine source, the disconnect, the reference dropped; and the
**start-decision event** (kind `S`, both images): mode, grant, binding,
the last filter's class and outcome, delivery, reason and status,
emitted before a failing start returns. They are the durable evidence of
section 10's L legs, since a removed FDO's counter block goes with it and
the HCD's last log flush at stop (`hcd_ctl.c` ~1912) precedes the
reference drop at remove.

**At the start:** "routines from the helper" or "helper not bound", as
record 15's "how the routines were found"; and when a message grant meets
a helper not bound, which of section 4.3's causes it was.

## 10. The legs

On QEMU guests, the `release` package for functional and regression legs
and the `qemu` package for injection legs, as the brief requires; host
"development host A". Every NT 6 x86 leg records which kernel image ran
(`ntoskrnl.exe` or `ntkrnlpa.exe`, from the guest's loaded-module list)
and that the boot was an ordinary one: no test-signing, no
signature-enforcement override, no kernel debugger.

**R - the readings that gate the strip** (section 11), first, on the
`qemu` packages of the first build with `XhciMsi86NoStrip` set: Vista SP2
and 7 SP1, **x86 and x64**, `qemu-xhci` with `msi=on,msix=off`,
`msi=off,msix=on` and the default (on x86 with the helper bound and then
absent; on x64 under mode 1), the complete trace read each time. The
admitted shapes are recorded per OS and architecture; the strip is
enabled on an OS and architecture only for the shapes its own capture
admitted, unless the captures show the lists identical, which is then
stated with them.

**M - MSI with the helper**, on Vista SP2 x86 and 7 SP1 x86, each after a
reboot that follows the install: record 15 section 4.3's x86 set -
`qemu-xhci` with `msi=on,msix=off` (read as MSI), `msi=off,msix=on`
(MSI-X), the launchers' default (whatever Windows granted); modes 0 and
3; HID, Bulk-Only storage and audio; disable and enable, remove and
rescan, a forced recovery, D3 and back; `XHCISNAP` naming the delivery,
the helper `BOUND` with its version, and the snapshot. Then
`MSISupported` 0, read as the line with nothing stripped; a raised
`MessageNumberLimit`, read as several granted, served as one.

**F - mode 1 forces the line** (section 4.3a), on Vista SP2 and 7 SP1,
**x86 and x64**, `MSISupported` 1, after the strip is enabled: mode 1
read as the line ("replaced", the requirement list without message
choices, the line connected, no log note of a message); then the same
with `XhciMsi86NoStrip` set (`qemu` package): the message with record
15's "line asked for, message granted" note; then mode 1 on a NO-LINE
list (`XhciMsi86InjNoLine`) on x64: the message with that note (x86's
case is H1a); and back to mode 0 after a disable and enable: the
message (x64; x86 with the helper bound). Each mode-1 line start is also
read for record 15's forced-line properties: the routing quirk applied
as on a line (where the chipset has it; QEMU's has not, so its record
says "not applicable"), and the watcher not opened (mode 1 never moves).

**L - lifetime**, on 7 x86 with the helper bound:

- L1 two `qemu-xhci` controllers: both bound; one disabled and enabled,
  and one removed and rescanned, while the other keeps running; each
  FDO's looks and reference read from its own record;
- L2 a stop and restart on the same FDO, with a new filter between: by a
  resource change in Device Manager where the guest offers one, read as
  no new look and the same binding; where none is offered, recorded as
  not reached on a guest. The binding's rules are host-tested either way:
  the per-FDO state machine is a pure function (`XhciMsi86BindStep`,
  shared `xhci_msi86.c`, `test_msi`) over the events filter, start, stop,
  surprise removal, remove and look result. Vectors: filter and start
  while not bound ask for a look; filter and start while bound do not;
  stop, surprise removal, remove and a look result never ask for one, in
  any state; stop then filter then start keeps the binding with no new
  look; remove drops an existing reference exactly once, after the
  disconnect, and acquires none; a refused or failed look result drops
  its reference at once and asks for no further look;
- L3 surprise removal (QEMU `device_del` of the controller) after
  binding: the disconnect, then the reference dropped at remove, read
  from section 9's lifecycle trace;
- L4 a start that fails after binding (the HCD's `qemu`-flavour start
  refusal): the release (disconnect), `IRP_MN_REMOVE_DEVICE` with the
  reference dropped, then - on the next enumeration - a new FDO that looks
  again and binds, read from the trace with both FDO serials.

**H - the helper missing or wrong**, on 7 x86 (and H1 to H3 on Vista x86
too), each read as the line with the list stripped (or, where R1 found no
line configuration, as section 4.2's "no line offered"), and the helper's state
with its detail. A running helper cannot be unloaded, so every change to
the helper's file, service or injection value is followed by a reboot
before it is read:

- H1 fresh install: the restart prompt shown (section 5); before the
  restart, R4's reading (the device started on section 4.2's choice with
  the helper `ABSENT`, or held for the restart with problem code 14);
  after the restart: MSI, `BOUND`;
- H1a mode 1 on a NO-LINE list without the helper, after the install's
  restart: the stimulus is a `qemu`-flavour switch, `XhciMsi86InjNoLine`,
  applied in the filter after the walk and before the classification: in
  every configuration it removes each line member of an interrupt run
  that also has a message member (so `[BAR, MSI|INTx]` becomes
  `[BAR, MSI]`), and drops every configuration whose interrupt
  requirements are all line-only, never leaving a run or the list empty;
  the rewritten list is returned as a replacement exactly as the strip's
  is, and it overrides `XhciMsi86NoStrip` and the admitted shapes (the
  rewritten list is NO-LINE, so the strip has nothing to do). The
  validated trace must show the received list, the rewritten one, class
  NO-LINE, an actual message grant (the start's `S` event) and the
  verdict: under mode 1 the start fails with "forced, no line offered"
  and its status; under mode 0, polling with "message not connected";
  with the helper bound under mode 1, the message with record 15's
  "line asked for" note (section 4.3);
- H2 `xhcimsi.sys` deleted, service left, reboot: `ABSENT`, the load
  failure in the event log, the boot normal;
- H3 the service deleted, reboot: `ABSENT`;
- H4 to H9 (`qemu` package) `XhciMsiInject` 1 to 6 (section 2.1), each
  followed by a reboot: failed load (`ABSENT`), refused, other major,
  short, and the two malformed answers (`REFUSED`, each test named);
- H10 the HCD's own `qemu`-flavour failed connection (record 15) with the
  helper bound: polling, "message not connected" - the one place the
  seam's failure row is read on x86.

**U - updates and mixed versions**, on 7 x86:

- U1 (Vista SP2 x86 and 7 SP1 x86) a compatible helper update while the
  old helper is resident: a second build of this package with a raised
  version installed over the first; the restart prompt shown; before the restart the old helper is
  still loaded and is read bound with its old `HelperVersion` (or R4's
  hold); after it, the new one;
- U2 the downgrade: 2.2.0.2 installed over this package without
  uninstalling, read for what the older HCD does with the left-over grant
  (for the release notes, section 6); then the prescribed procedure -
  uninstall with "delete the driver software", or `MSISupported` 0 - read
  as the hardware key without the message request and the older HCD on
  the line, before it is written into the release notes as safe;
- U3 the device uninstalled: the helper service left in place, loading
  and idle at the next boot; `sc delete xhcimsi` and a reboot remove it.

**D - down-level and other packages**:

- D2 Windows 98 SE, ME, 2000 SP4 and XP SP3 x86: install from the new
  media and read as the release before it (the line; no helper copied; no
  restart asked beyond what the release before it asked). **The Windows
  98 SE install is a required check** (owner, 2026-10-09): the HCD loads
  there with `IoGetDeviceObjectPointer` in its imports, read as the
  driver started and the device working, not inferred from the gate; ME
  likewise;
- D3 Vista x64 and 7 x64: no helper and no media change in the x64
  package; MSI as record 15 has it under modes 0 and 3 (that leg is
  36.16's). The amd64 HCD's behaviour does change under mode 1 (leg F);
- D4 7 x86 with Driver Verifier on `xhcimsi.sys` and `xhci98.sys`, through
  M's first variant, a disable and enable, and L3.

## 11. Open readings

- **R1.** The requirements list Vista's and 7's PCI driver builds for a
  controller with a pin and `MSISupported` 1: how many configurations,
  which of them STRIPPABLE, MESSAGE-BOUND or NONE, and, inside each, the
  requirements and the order and `Option` of their members. Read from
  section 9's complete trace with the strip disabled. It fixes the
  admitted shapes of section 4.2, and if no STRIPPABLE configuration is
  offered for a controller with a pin, section 4.2's withdrawal applies.
- **R2.** Whether a message descriptor is accompanied by a
  `CmResourceTypeDevicePrivate` descriptor, and where; read from the same
  complete trace, and taken as a fact about the readings, not inferred
  from adjacency alone. If one is found inside a STRIPPABLE configuration
  that keeps it, the transformation is amended before the strip is
  enabled.
- **R4.** Whether Vista SP2 and 7 SP1 x86 start the controller before the
  restart the INF asks for (section 5), on a fresh install and on an
  update, or hold it with problem code 14 until the restart; read in H1
  and U1. It changes only what the release notes say about the time
  before the restart.
- **R3.** That an unsigned boot-start x86 driver loads on Vista SP2 and 7
  SP1 x86 on an ordinary boot (section 5). WDK guidance has the embedded
  signature optional for x86 boot-start drivers; the reading is what is
  relied on.

## 12. Decisions

All taken by the owner; items 2 to 6 were answered on 2026-10-09 after
revision 5 put them as "orchestrator decision, owner to confirm".

1. Option A, the helper (owner, 2026-10-09).
2. **Owner decision (2026-10-09), accepted as designed:** the named device
   object, and the HCD's x86 image taking one new import,
   `IoGetDeviceObjectPointer`, with Windows 2000 export and stock Windows
   98 SE evidence (the owner checked `1394bus.sys` and `ntkern.vxd`); the
   gate row stays and the Windows 98 SE install leg is a required check
   (sections 3.4 and 10).
3. **Owner decision (2026-10-09), accepted as designed:** without the
   helper the HCD keeps only the line configurations in
   `IRP_MN_FILTER_RESOURCE_REQUIREMENTS`, so Windows grants the line where
   the list offers one (section 4.2); it ships off, behind the `qemu`-only
   switch, until R1 and R2 are read. This refines record 15 section 4.3's
   statement that a helper-less x86 start with a message grant polls: with
   the strip, that row is reached when the list is not replaced and
   Windows then grants a message, or when the filter never ran - except
   under mode 1 with no line offered (decision 6).
4. **Owner decision (2026-10-09), changed from revision 5:** the Vista
   and 7 x86 controller install section requests a restart (`Reboot`), on
   a fresh install and on every update through it, so the boot-start
   helper is active from the next boot; until the restart the controller
   keeps section 4.2's helper-less choice, or is held for the restart if
   R4 shows that (section 5).
5. **Owner decision (2026-10-09), accepted:** the strip's replacement list
   is the HCD's one `PagedPool` allocation, at one named site in
   `hcd_pool.c`, as the IRP's contract asks (section 4.2).
6. **Owner decision (2026-10-09), changed from revision 5:** mode 1 on a
   Vista or 7 x86 controller whose list offered no line, with no helper
   bound, **fails the start**, strictly, as XP does - it does not poll
   (section 4.3). A bound helper's failed connection still polls.
7. **Owner decision (2026-10-09):** `XhciInterruptMode` 1 on its own forces
   the line on Vista and 7, x86 and amd64, through the same filter and
   strip (sections 4.2 and 4.3a); it ships behind the same `qemu`-only
   switch until R1 and R2 show the PCI driver offers a line beside the
   message; it never leaves an empty list; where no line is offered mode
   1 falls back to record 15's rule (the message runs, with the log
   note), except x86 with no helper bound, which fails (decision 6).

**For the orchestrator, to reconcile at merge** (this record does not
edit them):

- Record 15 section 4's message table, the row "0 or 1 |
  `IoConnectInterruptEx` not found, or the connection refused | poll |
  message not connected": mode 1 with routines not found **and no line
  offered** (x86, no helper) fails the start with "forced, no line
  offered". Section 4.3's decision paragraph ("a message connection that
  fails polls for that start") and its "Without the helper" bullet ("keep
  the line, the routing quirk and polling") take the same exception, and
  the latter the strip (decision 3) and the restart (decision 4).
- Record 15 section 4.3, the decision paragraph's "1 under a message
  grant runs as 0 and logs that the line was asked for" and "the line is
  forced by `MSISupported` 0": mode 1 now forces the line itself through
  the filter on x86 and amd64 (section 4.3a), and the message-under-mode-1
  row becomes the fallback where no line was offered or the list was not
  replaced; `MSISupported` 0 remains a second way.
- Record 15 section 4.3's "nothing is added to the x86 image's imports"
  (the "Why it is safe" paragraph) and the like: one import is added
  (`IoGetDeviceObjectPointer`, section 3.4).
- Record 15 section 4.3's unconditional helper-less statements - "a
  message descriptor that reaches the x86 image with no helper answering
  ... polls", the seam paragraph's "a message resource polls with
  'message not connected'" when not found, and the legs' "Vista and 7 x86
  without the helper ... read as polling with 'message not connected'":
  each takes the strip (decision 3) and the mode-1 exception (decision 6).
- Record 15 section 4.3's INF paragraph: the x86 `.HW` section and the
  helper with it, the `Reboot` directive, and the downgrade note
  (section 6).
- Record 15 section 8's catalogue of reasons: the new
  `XHCI_POLL_R_FORCED_NO_LINE_OFFERED`; section 9's `XHCISNAP` names for it
  and for section 9's new fields here.
- Record 15 section 3's "read at each start": on Vista and 7
  `XhciInterruptMode` is also read at `AddDevice` (and kept from each
  start's reading) for the requirements filter, into separate fields
  (section 4.2).
- Roadmap's decisions table, the "MSI on Vista and 7" row (~79), which
  still says x86's imports are unchanged and x86 keeps the line: one
  import added, the helper, the strip, mode 1 forcing the line.
- Record 15 section 11's regression line ("mode 1 the same, ... on Vista
  and 7 with `MSISupported` 0"): mode 1 now forces the line on Vista and 7
  with `MSISupported` 1 too.
- p36-poll's code: `src/xhci_poll.c` `XhciPollDecide` (the new input and
  branch), `src/xhci_poll.h` (the appended reason constant and the changed
  declaration), the HCD's call site in `hcd_ctl.c` (the input, and the
  `S` event before the failure return), `test_poll` (the vectors of
  section 4.3), and the `XHCISNAP` decoding of the new reason.
- Roadmap 36.15: "A message connection that fails, or a message resource
  with no `IoConnectInterruptEx` found, polls for that start" (the
  mode-1 exception; a bound helper's failed connection still polls);
  "The modes on Vista and 7" bullet ("1 under a message grant runs as 0
  and logs") and "Forcing the line ... is `MSISupported` 0" (mode 1 forces
  it, section 4.3a); the "single x86 binary ... must not import these
  routines" and "Amended in 36.5's review" lines where they still speak
  of x86 without the helper.
- Roadmap 36.16: "mode 1 under a grant, read as the message with its log
  line" becomes leg F (the line, x86 and x64; the message only with the
  strip off or no line offered); the x86 legs (the restart, R4, H1a, U1
  on both guests, the required 98 SE check); the down-level line "on
  Vista or 7 x86 a hand-set `MSISupported` 1 read as polling".

## 12a. As built (code, 2026-10-10)

Built on branch `p36-msi86` after 2.3.0.0 took p36-poll's 36.5 to 36.8,
together with record 15 section 4.3 itself (the amd64 connection, the
message ISR and the capability read had no code before). Where the code
differs from, or settles, what the sections above say:

- **Where the pure functions live.** The channel's two halves and the
  version packing (`XhciMsi86Answer`, `XhciMsi86Accept`,
  `XhciMsi86PackVersion`) are `src\xhci_msi86.c`, the one file the helper
  and the HCD both compile (`XHCI_MSI86_HELPER` makes its header take its
  types from `ntddk.h`). The rest is the HCD's alone, in `src\xhci_msi.c`:
  the capability read (`XhciMsiCapRead`, record 15), the checked walk, the
  class, the strip and the qemu no-line rewrite (`XhciMsiWalk`,
  `XhciMsiClassify`, `XhciMsiStrip`, `XhciMsiInjNoLine`, `XhciMsiOutcome`),
  the filter record (`XhciMsiFilterBegin`/`Publish`), the binding's state
  machine (`XhciMsi86BindStep`) and the trace record
  (`XhciMsiTraceFormat`). Both are host-tested by `test\test_msi.c`; the
  sections above that name `xhci_msi86.c` for all of them mean the pair.
- **The kernel half** is `src\hcd_msi86.c`: the mode cache
  (`HcdMsiAddDevice`, `HcdMsiStartValues`), the filter (`HcdMsiFilter`),
  the helper's look and remove (x86), `HcdMsiRoutines` (amd64's
  `MmGetSystemRoutineAddress`, x86's binding), `HcdMsiNoLineOffered`, the
  counters and the trace. The seam `hcdMsgConnect`, the message ISR and
  the capability read are `src\hcd_ctl.c`'s, beside the line's
  connection; the release disconnects through `IoDisconnectInterruptEx`
  whatever it connected, a line Windows fell back to included.
- **The connection passes no spin lock** (record 15 section 4.3's revision
  6): `SpinLock` NULL, so the system's own lock at the unified IRQL serves
  the message ISR and `KeSynchronizeExecution`.
- **The strip ships off** (`HCD_MSI_STRIP_DEFAULT` 0 in `hcd_msi86.c`)
  until R1 and R2 are read; the `qemu` flavour's `XhciMsi86NoStrip` keeps
  it off after the default is turned on, and `XhciMsi86InjNoLine` is H1a's
  stimulus. The admitted shapes are the classification's own: a run whose
  members are not all of the head's type, or a list opening on an
  alternative, is "unsupported shape" and kept.
- **The counters**: eighteen words appended to `XHCIHC_COUNTERS` (114 in
  all; `xhci_counters.h` names them): the routines' source, the messages
  connected, the capability, the connection's status, the raw message
  descriptors; the helper's state, looks, last status, refusal and
  version; the last filter's sequence, class, outcome, mode as read, its
  configurations, message and line members, and the descriptors a strip
  removed. The bounded descriptor snapshot is `hc->FilterSnap` (12
  descriptors of 9 words); the start's log notes carry the summary
  (`msi.*`, `msi86.*`).
- **The trace** is the `M86` record of section 9, one per line on the
  `qemu` flavour's debug channel; a list's `B` record names it `R`
  (received), `I` (after the no-line rewrite) or `T` (returned), and
  `scripts\vm-matrix\check-msi86-trace.ps1` validates a capture and prints
  it (its `-SelfTest` runs in `build-driver.cmd`).
- **The helper's imports**: `wdmsec.lib`'s `IoCreateDeviceSecure` brings
  31 `ntoskrnl.exe` pairs, all listed in `xhcimsi-imports.allow` from the
  first link and gated by the `nt6x86` profile against the Vista SP2 and
  7 SP1 x86 kernels; the checked builds add `RtlAssert`, the `qemu` build
  `RtlQueryRegistryValues`.
- **The pipeline**: `build-driver.cmd :buildmsi` (x86 runs); the import
  gate's `-GateProfile nt6x86`; `source-stamp.ps1 -Driver xhcimsi`, whose
  `SOURCES` parse now keeps path separators; the charset gate's skip;
  `make-package.ps1 -MsiDriverPath`; `make-release.ps1`'s per-leg media
  and publishable sets and `Assert-MsiHelperPublished`; `check-inf.ps1`'s
  `MSI-*` and `MSIHLP-*` rules and `.HW`/directive footprint rows;
  `check-txtsetup-oem.ps1` not counting the helper's service.

## 13. Files the code touches

New: `src\msi86\sources`, `src\msi86\makefile`, `src\msi86\msi86_entry.c`,
`src\msi86\xhcimsi.rc`, `src\xhci_msi86.h`, `src\xhci_msi86.c` (the shared
pure functions, compiled into both drivers and the host suite),
`src\hcd_msi86.c`,
`scripts\import-gate\xhcimsi-imports.allow`,
`scripts\import-gate\nt6x86-baselines.expected`.

Changed: `src\xhci98.inf`, `src\sources`, `src\hcd.h`, `src\hcd_pnp.c`
(the filter and the remove), `src\hcd_ctl.c` (the x86 caller of
`hcdMsgConnect`, the republication after `HcdCountersStart`),
`src\hcd_entry.c` (the state and the filter-time mode read at `AddDevice`), `src\hcd_pool.c` (the paged
site, now in both images), `src\xhci98-amd64.inf` unchanged but the amd64
image's filter (`src\hcd_msi86.c`'s filter part compiled for both; the
helper look x86-only), `src\xhci_poll.c` and `test\test_poll.c` (the mode-1 input and
reason, at merge with p36-poll), the counter block and log (`src\hcd_log.c`, `src\xhci.h` where
record 15's fields live),
`test\test_msi.c`; `scripts\build-driver.cmd`,
`scripts\import-gate\check-imports.ps1`, `xhci98-imports.allow`,
`win98-evidence.list`, `extract-target-baselines.ps1`,
`scripts\vm-matrix\check-msi86-trace.ps1` (new) and its self-test,
`test-flavour-rules.ps1`, `scripts\inf-gate\check-inf.ps1`,
`test-inf-checks.ps1`, `expected-footprint.txt`,
`expected-footprint-amd64.txt` (the `.HW` modelling),
`scripts\source-stamp.ps1`, `scripts\check-source-charset.ps1`,
`scripts\package\make-package.ps1`, `make-release.ps1`,
`package-common.ps1` if the per-leg media helpers live there,
`test-package.ps1`; `AGENTS.md` (the pool paragraph, the x86 build's
WDK), design record 13 section 7.5 (the pool exception),
`docs\contributing\legal-provenance.md`,
`docs\contributing\build-and-test.md` (the x86 build's WDK, the log lines,
the legs), `docs\contributing\source-files.md`, the release notes and
`docs\using` (the first-boot line, the downgrade procedure, removing the
service); `xhcisnap` (36.9's names).

## 14. The reviews

### 14.1 Revision 1

Round 1 (`69b69f1`, review thread `01a120a8-e037-7a31-b88b-4e325ef35abb`):
2 P1, 6 P2, 2 P3, all taken in revision 2.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: the filter broke the IRP's buffer and completion contract (in-place shrink, forwarded status) | Section 4.2: the list's source, a paged replacement, the old list freed, `STATUS_SUCCESS`, allocation failure kept safe; decision 5 |
| 2 | P1: removing descriptors does not select the line configuration (`[BAR, MSI]` + `[BAR, INTx]`) | Section 4.2: configurations classed LINE / MSG-ONLY / NONE, only LINE kept; MSI-only left alone; vectors; decision 6 |
| 3 | P2: the named device had no documented security | Section 2: `IoCreateDeviceSecure`, system-only SDDL, secure open, non-exclusive, no trailing name; section 8.2 |
| 4 | P2: the block's length rules were incomplete | Section 3.2: caller and helper validation, `V1_MIN`, `Returned == Information <= capacity`, IRP initialisation and ownership; vectors |
| 5 | P2: the x86-only file breaks the equal-media assumption in the INF self-test and `make-release.ps1` | Section 8.2: one admitted media difference; media and publishable sets per leg; `test-package.ps1` cases |
| 6 | P2: the footprint does not model `.HW` | Section 8.2: `.HW` walked as hardware-key writes with flags; mutations; amd64 coordinated |
| 7 | P2: counts cannot answer R1/R2, and the counter block is zeroed at start | Section 9: a bounded descriptor snapshot in `hc`, republished after `HcdCountersStart`; section 11 |
| 8 | P2: legs missed lifetime and mixed-install cases | Section 10: L1-L4, U1-U3, reboots after every helper change, the kernel image and an ordinary boot recorded |
| 9 | P3: "no interrupt" polls only by mode; the normal MSI path called exceptional | Sections 4.2 and 4.3; the index line |
| 10 | P3: the WDM test is a minimum, and a dangling section reference | Section 2 |

### 14.2 Revision 2

Round 2 (`a4c8307`, same thread): 1 P1, 5 P2, 2 P3, all taken in
revision 3. Round-1 findings 3, 5, 6 and 10 were confirmed closed.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: the stack's list pointer was still a fallback, and could be freed | Section 4.2: `IoStatus.Information` only; NULL is "no list", nothing dereferenced; every outcome's completion stated |
| 2 | P2: a buffer shorter than the header passed the length tests before `Size` was read | Section 3.2: lengths `>= V1_MIN` before any read, `Information = 0` on refusal; `XhciMsi86Answer` host-tested, guarded end-of-allocation vector |
| 3 | P2: a configuration with a separate message-only requirement lost that requirement | Section 4.2: requirements (runs) classed line-only / mixed / message-only; only STRIPPABLE configurations kept; outcomes named; the two-requirement vectors' results stated |
| 4 | P2: `wdmsec.lib` members carry `/GS` checks, so turning the cookie off is wrong | Section 8.1: the cookie stays on (kit default); retained members and imports read from the first link; section 8.2's provenance row |
| 5 | P2: a truncated snapshot cannot close R1/R2 | Section 9: a complete `qemu`-flavour descriptor trace on the debug channel; section 11 reads from it |
| 6 | P2: L4 expected no new look after a failed start; L2's host vectors unnamed; L3's evidence lost at remove | Section 10: L4 expects remove and a new FDO's look; `XhciMsi86BindStep` vectors; lifecycle events on the durable trace (section 9) |
| 7 | P3: the fallback promises were broader than the algorithm | Sections 4.2, 4.3, 5, 6 and decision 3: outcomes "no line offered", "no interrupt" and the rest distinguished; results conditional on the list |
| 8 | P3: the third interrupt word is `AffinityPolicy`, not affinity | Section 9: six raw words labelled by WDK 7.1's layout |

### 14.3 Revision 3

Round 3 (`f2fede5`, same thread): 4 P2, 2 P3, all taken in revision 4.
Round-2 findings 1, 2, 3, 4 and 8 were confirmed closed; no new P1.

| # | Finding | Where answered |
|---|---|---|
| 1 | P2: the list was recorded before it was validated | Section 4.2, step 4: `XhciMsi86Walk`, overflow-safe, before any snapshot, trace or strip; vectors |
| 2 | P2: the debug channel interleaves and truncates, so the trace was not provably complete | Section 9: fixed-layout checksummed records with counts, under 128 bytes, the existing writer only; a host validator that rejects bad captures |
| 3 | P2: the binding vectors asked for looks during teardown | Section 10, L2: looks at filter and start while unbound only; negative vectors for stop, surprise removal, remove and look results |
| 4 | P2: the shared C file was outside the helper's source stamp | Section 8.2: the shared source's build arrangement; `source-stamp.ps1`'s parser keeps path separators; mutations |
| 5 | P3: fallback promises still exceeded the algorithm | Section 4.2's rule (replaced gives the line; otherwise the grant and the binding at start decide), used in sections 5, 6 and decision 3 |
| 6 | P3: stale vector results and classification names | Section 4.2's vectors; section 11 renamed to STRIPPABLE / MESSAGE-BOUND / NONE and the complete trace |

### 14.4 Revision 4

Round 4 (`db66b6a`, same thread): **converged** - "the design is sound
enough to implement", no P1 or P2; round-3 findings 1 to 4 and 6 closed.
Two P3s taken in revision 5: the delivery rule restated as a
resource-choice rule that defers delivery to record 15's mode, pin and
connection tables (section 4.2), and the last unconditional line promises
removed (section 6, decisions 4 and 6). R1 to R3 stay runtime gates.

### 14.5 Revision 6

The owner's answers to section 12 (2026-10-09), written in: decisions 2,
3 and 5 accepted as designed; decision 4 changed to a restart requested
by the INF (section 5, R4, the `MSIHLP-REBOOT` rule, legs H1 and U1);
decision 6 changed to mode 1 failing the start where no line was offered
and no helper is bound (section 4.3, leg H1a), with the record 15 and
roadmap wording to reconcile listed in section 12.

### 14.6 Revision 6, and the owner's mode-1 decision

Round 5 (`b502184`, same thread): 1 P2, 3 P3, all taken in revision 7,
with the owner's decision 7. The `Reboot` mechanism, `SPSVCINST_STARTSERVICE`
as the recorded alternative, and the narrow mode-1 rule were found sound.

| # | Finding | Where answered |
|---|---|---|
| 1 | P2: H1a had no stimulus that yields a message grant with no line offered, and the failure's reason had no durable record | Section 10, H1a: `XhciMsi86InjNoLine` defined (mixed runs, its interaction with `NoStrip` and the admitted shapes), run after the restart; section 4.3 and 9: the `S` event before the failure return, the fields kept in `hc` |
| 2 | P3: the reconciliation list was incomplete | Section 12: `xhci_poll.h`, record 15's import statements, helper-less polling statements, reason catalogue, the call site, `XHCISNAP`; a bound helper's failed connection still polls |
| 3 | P3: the general rule and the withdrawal paragraph contradicted the exception | Section 4.2: the class NO-LINE computed always and kept for the start; the rule names the exception; withdrawal makes "disabled" the default and keeps the class |
| 4 | P3: R4's Vista update reading was outside U1 | Section 10, U1 on both guests |

Also from round 5: `MSIHLP-REBOOT` must read a bare directive (section
8.2).

### 14.7 Revision 7

Round 6 (`17f83ba`, same thread): 1 P1, 2 P2, 1 P3, all taken in
revision 8. Round-5's fixes were confirmed; the mode-change policy and
mode 1's forced-line properties (routing quirk, no watcher) were found
coherent.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: the start's registry reader needs a system thread; the filter arrives in an arbitrary one | Section 4.2: revision 8 used an `ExQueueWorkItem` worker; revision 9 (round 7) replaced it with readings taken at `AddDevice` and at each start, both documented system-thread contexts |
| 2 | P2: a NO-LINE class could survive a later filter that never classified | Section 4.2, step 0: UNKNOWN at `AddDevice` and at every filter entry, published only with that filter's sequence; vectors |
| 3 | P2: amd64 stripping enabled without amd64 readings | Section 10, R on x64 too, admission per OS and architecture; section 9's labels per architecture |
| 4 | P3: the filter-time read missing from the reconciliation and the record | Sections 9 and 12; D3 reworded; transition vectors 1 to 0, 0 to 1, 1 to 2 |

### 14.8 Revision 8

Round 7 (`bb65eae`, same thread): 1 P1, 1 P3, both taken in revision 9.
Round-6 findings 2 to 4 were confirmed closed.

| # | Finding | Where answered |
|---|---|---|
| 1 | P1: the worker's completion event does not keep the image loaded until the callback returns (`ExQueueWorkItem` holds no device reference; `xhci98.sys` is unloadable) | Section 4.2: no worker at all; the filter uses the raw mode read at `AddDevice` and refreshed at each start, both documented system-thread contexts; a rebalance after an unannounced value change is recorded |
| 2 | P3: `START_DEVICE` is documented as a system thread | Section 4.2: the audit note removed; the start's reading is one of the two sources |

### 14.9 Revision 9

Round 8 (`dede1e1`, same thread): **converged** - "sound enough to
implement", no P1 or P2. Two P3s taken in revision 10: the filter's use
of the cached reading stated where the strip is asked for and in section
4.4 (a disable and enable as the supported way to change the mode, the
rebalance exception kept), and the cache's fields kept apart from the
filter's snapshot, with a transition vector. Review thread
`01a120a8-e037-7a31-b88b-4e325ef35abb`, rounds 1 to 8.
