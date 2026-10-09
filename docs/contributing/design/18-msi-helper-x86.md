# The NT 6 x86 MSI helper

Design record for the x86 half of message-signalled interrupts on Windows
Vista and Windows 7 (roadmap-hcd Phase 36, tasks 36.15 and 36.16; the task
lines for this record's code and legs are the orchestrator's to number).
Revision 1, 2026-10-09, written before any code, on branch `p36-msi86`;
revision 2 the same day, after review round 1 (section 14).

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
to x86 unchanged once the seam has the pair; amd64, which keeps
`MmGetSystemRoutineAddress` and gets no helper; and more than one
interrupter.

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

**The helper validates before it reads anything else:** `Type3InputBuffer`
non-NULL; `InputBufferLength == OutputBufferLength`; `Size` (read once,
after the length test) equal to that length; `Size >= V1_MIN`. A failure
completes `STATUS_INVALID_PARAMETER` (`STATUS_BUFFER_TOO_SMALL` for a
`Size` below `V1_MIN`), a `CallerMajor` it does not serve
`STATUS_REVISION_MISMATCH`. It then writes `n = min(Size, its own block
size)` bytes' worth of its fields from offset 0x0C on (never the caller's
fields), sets `Returned = n` and the IRP's `Information = n`, and
completes with `STATUS_SUCCESS`.

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
that store choose an address the kernel then calls. **Orchestrator
decision, owner to confirm:** "imports unchanged" is read as "no import
Windows 98 lacks", and the one evidenced pair is taken. Its row cites the
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
a function driver handles it on the way up, after the lower stack. The
current list is at `Irp->IoStatus.Information` when that is non-NULL (a
lower driver, or the PnP manager, put it there), else at
`Parameters.FilterResourceRequirements.IoResourceRequirementList`. A
driver that changes the list's size allocates a replacement from **paged**
pool, frees the list it replaces, and returns the replacement in
`Information` with `STATUS_SUCCESS`.

On the x86 image, NT 6 only, the filter:

1. Forwards the IRP and waits (`HcdForwardAndWait`). If the lower status
   is a failure other than `STATUS_NOT_SUPPORTED`, it completes the IRP
   with that status and touches nothing.
2. Takes the current list as above. If neither pointer is set, it
   completes with the lower status and records "no list".
3. Looks for the helper (section 4.1) unless `BOUND`.
4. Records the list (section 9's snapshot), whatever the state.
5. If `BOUND`, or the strip is disabled (below), completes with the lower
   status and `Information` unchanged.
6. Else computes the stripped list (below). If it differs, allocates the
   replacement, writes it, frees the current list with `ExFreePool`, sets
   `Information` to the replacement and completes with `STATUS_SUCCESS`.
   If the allocation fails it leaves the current list in place, completes
   with the lower status, and records "strip failed: no memory"; that
   start then meets a message grant with the helper not bound and polls
   with "message not connected" (section 4.3) - never worse than that.

**The allocation** is one new named site, `HcdPoolAllocRequirements` in
`hcd_pool.c`, the HCD's only `PagedPool` allocation: the IRP's contract
asks for paged pool, and the PnP manager frees it. This is an exception
to design record 13 section 7.5's `NonPagedPool`-only rule, confined to
that site and that IRP; the allowlist's `SITES=hcd_pool.obj` already
covers it (the import is the same). **Orchestrator decision, owner to
confirm.**

**The strip** works at both levels of choice a requirements list has. An
`IO_RESOURCE_REQUIREMENTS_LIST` carries `AlternativeLists` complete
configurations (`IO_RESOURCE_LIST`s), of which the arbiter assigns one;
inside a configuration, a run of descriptors whose followers carry
`IO_RESOURCE_ALTERNATIVE` is one requirement satisfied by any member.
Removing descriptors alone is not enough: from `[BAR, MSI]` and
`[BAR, INTx]` it would leave `[BAR]` and `[BAR, INTx]`, and the arbiter
could take the first and grant no interrupt at all.

So each configuration is classed by its interrupt descriptors:

- **LINE**: at least one interrupt descriptor without
  `CM_RESOURCE_INTERRUPT_MESSAGE` (0x0002, declared by the HCD where the
  Windows 2000 DDK lacks it);
- **MSG-ONLY**: interrupt descriptors, every one a message one (MSI and
  MSI-X both carry the flag);
- **NONE**: no interrupt descriptor.

and the stripped list is:

- **If any configuration is LINE:** only the LINE configurations are kept,
  in their original order (`AlternativeLists` reduced). In each, every
  message descriptor is removed; where a removed descriptor headed a run
  (its `Option` without `IO_RESOURCE_ALTERNATIVE`), the next kept member
  of that run has the bit cleared so that it heads it. Every other
  descriptor is copied unchanged, `CmResourceTypeDevicePrivate` included
  (R2 decides whether that is right before the strip is enabled; nothing
  is inferred from adjacency).
- **If none is LINE** (a controller with `Interrupt Pin` 0, MSI only): the
  list is left as it is. There is no line to give, and stripping would
  only turn a message grant the start cannot connect into no interrupt at
  all. Record 15's rows for a message grant with no routines apply: the
  start polls with "message not connected", under mode 1 too, since record
  15 runs mode 1 under a message grant as mode 0. On XP the same
  controller has no interrupt resource at all, so modes 0 and 2 poll and
  mode 1 fails its start; the one difference from XP here is mode 1
  polling instead of failing, which is recorded, not hidden.
- `ListSize` is recomputed from what is written.

The strip is a pure function over the list's bytes (`XhciMsi86Strip`, in
the DDK-free core, layouts declared with `C_ASSERT`s against the DDK's
`IO_RESOURCE_REQUIREMENTS_LIST`, `IO_RESOURCE_LIST` and
`IO_RESOURCE_DESCRIPTOR`), a sizing pass then a writing pass, with host
vectors in `test_msi`: one configuration with a message-then-line run and
one with a line-then-message run; `[BAR, MSI]` and `[BAR, INTx]` in both
orders; an MSI-X descriptor; a message descriptor with several messages;
two interrupt requirements in one configuration; a MSG-ONLY list
(unchanged); a NONE configuration beside a LINE one (dropped); a list with
no interrupt at all (unchanged); an inconsistent list (`ListSize` short of
what the counts claim: left unchanged, recorded).

**The strip is enabled only after R1 and R2 are read** (section 11). The
first build carries it behind a `qemu`-only switch, `XhciMsi86NoStrip`,
under which the filter records and does not strip; R1 and R2 are read from
section 9's snapshot on both guests; the transformation above is confirmed
or amended in this record (a review round) before the code is taken as
done. If R1 shows that the PCI driver offers no LINE configuration for a
controller with a pin, the strip is withdrawn, the helper-less start takes
record 15's "message not connected" row and polls, and the owner is told
that "exactly as on XP" cannot hold for a helper-less install with
`MSISupported` 1.

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
  defines as "not found": that start polls with "message not connected".
  A message grant **without a bound helper** is reached only through a
  list the filter left in place (an MSI-only controller, a failed
  allocation, the strip disabled) or a filter that never came; the record
  says which (section 9).

Everything after the seam - the connection, its fallback to the line, the
release through `IoDisconnectInterruptEx`, the ISR, the capability read -
is record 15 section 4.3's, unchanged. The x86 caller passes the pair as
`PVOID`s cast to the seam's function-pointer types (`NTAPI`, one pointer
argument each), which are the routines' own signatures in WDK 7.1's
`wdm.h`; record 15's structure declarations already carry x86 size and
offset checks ("for both architectures").

### 4.4 What is not changed on x86

- The amd64 image has no look, no filter and no strip; it reaches the seam
  through `MmGetSystemRoutineAddress` (record 15).
- `XhciInterruptMode` keeps record 15's meaning. Mode 2 polls even with a
  message granted; the filter does not consult the mode, so a mode change
  needs no new resource assignment to take effect.
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

**The race that remains** is the first install. SetupAPI does not start a
boot-start service when it installs it, so on a fresh install (and on an
update from a package without the helper) the helper is not running until
the next boot: the controller's filter finds it `ABSENT`, strips, and the
controller runs the line, as on XP, for the rest of that session. MSI
takes over at the next boot. Asking for a reboot from the INF was
considered and not taken: the line is a working configuration, and a
reboot prompt on every update is a cost for no correctness gain. The
release notes say so.

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
| this package, freshly | no helper until the next boot | line this session; MSI after the boot (section 5) |
| this package, updated from one with the helper | the old helper until the next boot (a boot-start image in use is replaced at reboot) | bound if its major is 1, else line until the boot |
| a later package with interface major 2 | old HCD, new helper | the old HCD's V1 code is refused by a V2-only helper: line. A later helper that keeps answering V1 (the rule for a minor change) binds |
| this HCD, the helper's file removed or its copy failed | none | line (the filter strips), recorded `ABSENT` |
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
the `wlh` libraries can only have the Vista exports. `BUFFER_OVERFLOW_CHECKS=0`
in its `sources`: the helper has no stack buffer, and leaving the `/GS`
cookie object (`BufferOverflowK.lib`) out keeps its image free of the
third-party object whose notice question is open for the amd64 build.
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
| INF gate `check-inf.ps1` | New `MSIHLP-*` rules, x86 only: `MSIHLP-ONLYNT6` (the helper's service, copy section or file named anywhere but `Xhci.Dev6.NTx86`, `[SourceDisksFiles]` and `[DestinationDirs]`); `MSIHLP-SVC` (the second `AddService` on `Xhci.Dev6.NTx86.Services` only, flags 0, `ServiceType` 1, `StartType` 0, `ErrorControl` 1, `ServiceBinary=%12%\xhcimsi.sys`, copied by that section's own `CopyFiles`); `MSIHLP-HW` (on x86, record 15's `MSI-*` lines only in `Xhci.Dev6.NTx86.HW`, and only when `MSIHLP-SVC` holds); `MSIHLP-AMD64` (no helper in the 64-bit INF). `PATH-NT`'s "exactly one `AddService`, flag 0x2, `StartType` 3" is relaxed for that one section by exactly the helper's line. `BOTH-VERSION` reads `src\msi86\xhcimsi.rc` too. Mutations in `test-inf-checks.ps1` for each rule; the `Dev6` mutations rebased on the new section text. **The cross-architecture media rule:** `test-inf-checks.ps1`'s requirement that both INFs declare the same `[SourceDisksFiles]` (~2005) admits exactly one difference, `xhcimsi.sys` in the 32-bit file only, and a mutation proves any other difference still fails. **The footprint models `.HW`:** `-EmitFootprint` today emits a `.HW` section as unmodelled (`check-inf.ps1` ~2575); it is extended to walk a `.HW` section's `AddReg` and emit its values as **hardware-key** writes (distinct from the software-key `HKR` of the install section), with their flags, so `MSISupported`, `MessageNumberLimit` and `FLG_ADDREG_NOCLOBBER` appear in `expected-footprint.txt`; the helper's service appears as a service the package creates (its removal on uninstall is a reading, section 10, not a footprint claim). Mutations: each MSI value dropped, its NOCLOBBER flag dropped, the `.HW` section moved to the root hub. The amd64 footprint takes the same `.HW` modelling for record 15's lines (the orchestrator's to coordinate with record 15's own `MSI-*` rule) |
| Flavour marker | The helper embeds `XHCI98_FLAVOUR_{RELEASE,DEBUG,QEMU}` as `uas_entry.c` does; `check-flavour-marker.ps1` unchanged |
| Source stamp | `source-stamp.ps1 -Driver` gains `xhcimsi` (`src\msi86`, `xhcimsi.srcstamp`, with `xhci_msi86.h` and `xhci_version.h` as parent headers) |
| Charset gate | `check-source-charset.ps1`'s object-directory skips gain `^src\\msi86\\obj` |
| Version resource | `src\msi86\xhcimsi.rc` includes `../xhci_version.h`, `FILEVERSION`/`PRODUCTVERSION` from the macros, `VS_FF_DEBUG` for checked builds, names `xhcimsi.sys` |
| `make-package.ps1` | `-Arch x86` stages `xhcimsi.sys` (the staging table; `$MsiDriverPath`), gated like the UAS pair: exists, flavour marker, import gate with the `nt6x86` profile; `-Arch amd64` refuses one |
| `make-release.ps1` | **Media per leg, not per release.** Today the declared media are derived once, from the first published INF, for every upload leg, on the strength of the two INFs' equal `[SourceDisksFiles]` (~1351). That becomes per leg: each leg's publishable set and media layout come from that leg's own INF (x86 legs: `xhci98.inf`, `xhci98.sys`, `xhcimsi.sys`; x64 legs: as today), through upload assembly and `-UploadSetOnly`, and a historical release whose INFs declare no helper is reconstructed exactly as before. `Assert-MsiHelperPublished`, modelled on `Assert-UasPublished`, per x86 leg: the file exists, its `FileVersion` equals the version, `VS_FF_DEBUG` matches the flavour, the marker, bytes equal to `src\msi86\obj...\i386\xhcimsi.sys`, `source-stamp -Driver xhcimsi -Check`. An x64 leg holding a helper is refused. The readme and contents text name it |
| `test-package.ps1` | Cases: an x86 package with the helper and an x64 one without, assembled in both leg orders; an x86 package missing the helper (refused); a helper in an x64 package (refused); a historical upload set without the helper (reconstructed unchanged); a helper of the wrong flavour (refused); a helper whose source stamp is stale, and one swapped for another build's bytes (refused) |
| `legal-provenance.md` | Section 2's toolchain row: WDK 7.1 now also builds an x86 binary (the helper), with `BUFFER_OVERFLOW_CHECKS=0`, and links one WDK library object into it, `wdmsec.lib`'s `IoCreateDeviceSecure` (stated as a fact, beside the amd64 `/GS` object's open notice question); section 4: a row for the NT 6 x86 export readings (static: the export tables of the four kernels and two HALs) and for R1-R3 (runtime, through the driver's own log) |
| Pool | `hcd_pool.c` gains `HcdPoolAllocRequirements`, the one `PagedPool` site (section 4.2); record 13 section 7.5 and AGENTS.md's pool paragraph name the exception |
| Host tests | `test_msi` (record 15's suite) gains `XhciMsi86Accept` and `XhciMsi86Strip`; `test\run-host-tests.cmd` unchanged otherwise |

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

**The last filter's list snapshot**, bounded: `AlternativeLists`, and for
up to 4 configurations and 12 descriptors in all, each descriptor's
configuration index, descriptor index, `Type`, `Option`, `Flags`,
`ShareDisposition` and its three `u` words (an interrupt's minimum and
maximum vector and its affinity, a device-private descriptor's three data
words), with a truncation flag and the totals when there were more; then
the filter's outcome - not stripped (bound / disabled / MSI-only / no
list / inconsistent), stripped (configurations and descriptors removed),
or strip failed (no memory). This is what R1 and R2 are read from.

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
`qemu` package of the first build with `XhciMsi86NoStrip` set: Vista SP2
x86 and 7 SP1 x86, `qemu-xhci` with `msi=on,msix=off`, `msi=off,msix=on`
and the default, the helper bound and then absent, the snapshot read each
time.

**M - MSI with the helper**, on Vista SP2 x86 and 7 SP1 x86, each after a
reboot that follows the install: record 15 section 4.3's x86 set -
`qemu-xhci` with `msi=on,msix=off` (read as MSI), `msi=off,msix=on`
(MSI-X), the launchers' default (whatever Windows granted); modes 0 and
3; HID, Bulk-Only storage and audio; disable and enable, remove and
rescan, a forced recovery, D3 and back; `XHCISNAP` naming the delivery,
the helper `BOUND` with its version, and the snapshot. Then
`MSISupported` 0, read as the line with nothing stripped; mode 1 under a
grant, read as the message with its log note; a raised
`MessageNumberLimit`, read as several granted, served as one.

**L - lifetime**, on 7 x86 with the helper bound:

- L1 two `qemu-xhci` controllers: both bound; one disabled and enabled,
  and one removed and rescanned, while the other keeps running; each
  FDO's looks and reference read from its own record;
- L2 a stop and restart on the same FDO, with a new filter between: by a
  resource change in Device Manager where the guest offers one, else
  recorded as not reached and left to the strip's and the binding's host
  vectors;
- L3 surprise removal (QEMU `device_del` of the controller) after
  binding: the disconnect, then the reference dropped at remove, read in
  the log;
- L4 a start that fails after binding (the HCD's `qemu`-flavour start
  refusal): the release, then the next start binds without a new look.

**H - the helper missing or wrong**, on 7 x86 (and H1 to H3 on Vista x86
too), each read as the line with the list stripped (or, where R1 found no
LINE configuration, as section 4.2's fallback), and the helper's state
with its detail. A running helper cannot be unloaded, so every change to
the helper's file, service or injection value is followed by a reboot
before it is read:

- H1 fresh install, before the reboot (section 5): line, `ABSENT`; after
  the reboot: MSI, `BOUND`;
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

- U1 a compatible helper update while the old helper is resident: a
  second build of this package with a raised version installed over the
  first; before the reboot the old helper is still loaded and is read
  bound with its old `HelperVersion`; after it, the new one;
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
  media and read as the release before it (the line; no helper copied;
  `IoGetDeviceObjectPointer` resolving at load on 98 SE and ME);
- D3 Vista x64 and 7 x64: no helper in the package, MSI as record 15 has
  it (that leg is 36.16's; here only that the x64 package is unchanged by
  this record);
- D4 7 x86 with Driver Verifier on `xhcimsi.sys` and `xhci98.sys`, through
  M's first variant, a disable and enable, and L3.

## 11. Open readings

- **R1.** The requirements list Vista's and 7's PCI driver builds for a
  controller with a pin and `MSISupported` 1: how many configurations,
  which of them LINE, MSG-ONLY or NONE, and, inside each, the order and
  `Option` of the interrupt descriptors. Read from section 9's snapshot
  with the strip disabled. It confirms or amends section 4.2's
  transformation, and if no LINE configuration exists, section 4.2's
  fallback applies.
- **R2.** Whether a message descriptor is accompanied by a
  `CmResourceTypeDevicePrivate` descriptor, and where; read from the same
  snapshot, and taken as a fact about the readings, not inferred from
  adjacency alone. If one is found inside a LINE configuration that keeps
  it, the transformation is amended before the strip is enabled.
- **R3.** That an unsigned boot-start x86 driver loads on Vista SP2 and 7
  SP1 x86 on an ordinary boot (section 5). WDK guidance has the embedded
  signature optional for x86 boot-start drivers; the reading is what is
  relied on.

## 12. Decisions

1. Option A, the helper (owner, 2026-10-09).
2. **Orchestrator decision, owner to confirm:** the HCD's x86 image takes
   one new import, `IoGetDeviceObjectPointer`, with Windows 2000 export and
   stock Windows 98 SE evidence (section 3.4).
3. **Orchestrator decision, owner to confirm:** without the helper the HCD
   strips the message descriptors in `IRP_MN_FILTER_RESOURCE_REQUIREMENTS`
   so Windows grants the line (section 4.2). This refines record 15 section
   4.3's statement that a helper-less x86 start with a message grant polls:
   with the strip that row is the fallback of a grant the filter missed,
   not the helper-less norm. Record 15 and roadmap 36.15/36.16 are the
   orchestrator's to reconcile.
4. **Orchestrator decision, owner to confirm:** a fresh install runs the
   line until the next boot rather than asking for a reboot (section 5).
5. **Orchestrator decision, owner to confirm:** the strip's replacement
   list is the HCD's one `PagedPool` allocation, at one named site, as the
   IRP's contract asks (section 4.2).
6. **Orchestrator decision, owner to confirm:** an MSI-only controller (no
   LINE configuration) without the helper keeps its message grant and
   polls, so mode 1 polls there where XP would fail the start (section
   4.2).

## 13. Files the code touches

New: `src\msi86\sources`, `src\msi86\makefile`, `src\msi86\msi86_entry.c`,
`src\msi86\xhcimsi.rc`, `src\xhci_msi86.h`, `src\hcd_msi86.c`,
`scripts\import-gate\xhcimsi-imports.allow`,
`scripts\import-gate\nt6x86-baselines.expected`.

Changed: `src\xhci98.inf`, `src\sources`, `src\hcd.h`, `src\hcd_pnp.c`
(the filter and the remove), `src\hcd_ctl.c` (the x86 caller of
`hcdMsgConnect`, the republication after `HcdCountersStart`),
`src\hcd_entry.c` (the state at `AddDevice`), `src\hcd_pool.c` (the paged
site), the counter block and log (`src\hcd_log.c`, `src\xhci.h` where
record 15's fields live), the DDK-free core file that takes
`XhciMsi86Accept` and `XhciMsi86Strip` (record 15's `test_msi` code's),
`test\test_msi.c`; `scripts\build-driver.cmd`,
`scripts\import-gate\check-imports.ps1`, `xhci98-imports.allow`,
`win98-evidence.list`, `extract-target-baselines.ps1`,
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
