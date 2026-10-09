# The NT 6 x86 MSI helper

Design record for the x86 half of message-signalled interrupts on Windows
Vista and Windows 7 (roadmap-hcd Phase 36, tasks 36.15 and 36.16; the task
lines for this record's code and legs are the orchestrator's to number).
Revision 1, 2026-10-09, written before any code, on branch `p36-msi86`.

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
three dispatch routines; nothing else runs at load. It handles:

- `IRP_MJ_CREATE`: succeeds when `Irp->RequestorMode` is `KernelMode`,
  else `STATUS_ACCESS_DENIED`. A user-mode caller can name `\Device\`
  paths through the native API, so the absence of a link is not relied on.
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
7 SP1 x86 export and Windows 2000 and XP do not (section 6.2), so on an
older kernel it cannot load at all; the INF never installs it there
(section 7). It also returns `STATUS_NOT_SUPPORTED` from `DriverEntry`
when `IoIsWdmVersionAvailable(6, 0)` is FALSE, which costs one import and
keeps a hand-installed copy on a later-unsupported kernel inert.

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

The helper fails a `Size` below 0x28 with `STATUS_BUFFER_TOO_SMALL` and a
`CallerMajor` it does not serve with `STATUS_REVISION_MISMATCH`, writes
nothing beyond `Size`, and sets `Returned` (and the IRP's `Information`)
to what it filled. A minor
revision may append fields and raise `Size` and `Returned`; it never moves
one.

**The HCD accepts the answer** only if all of these hold, a pure function
(`XhciMsi86Accept`, host vectors in `test_msi`) over the status, the
`Information`, and the block:

- the status is success and `Information` equals `Returned`;
- `Magic` is `'XMSI'` and `HelperMajor` is 1;
- `Returned` covers `DisconnectEx` (at least 0x24);
- `ConnectEx` and `DisconnectEx` are both non-NULL and differ.

Anything else is a refusal, recorded with which test failed (section 9).
Nothing in an answer is used until it is accepted, and an accepted answer
is copied into the controller (`hc`) before the IRP is freed.

**The IRP** is built with the imports the HCD already has:
`IoAllocateIrp(target->StackSize, FALSE)`, the next stack location's
`MajorFunction` `IRP_MJ_INTERNAL_DEVICE_CONTROL`, the code, the block, the
lengths and the `FileObject`; a completion routine that sets a
`KEVENT` and returns `STATUS_MORE_PROCESSING_REQUIRED`; `IofCallDriver`;
an unbounded `KeWaitForSingleObject` (the helper never pends, as
`HcdForwardAndWait`'s waits rely on the lower driver); `IoFreeIrp`. PASSIVE_LEVEL.

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
the message-interrupt descriptors from the requirements list there, so the
arbiter grants the line the PCI driver also offers. With the helper bound
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

On the x86 image, NT 6 only, after the IRP has been forwarded and has come
back (`HcdForwardAndWait`): the list is the lower driver's
`IoStatus.Information` when it set one, else the IRP's own
`Parameters.FilterResourceRequirements.IoResourceRequirementList`. The
filter then:

1. Looks for the helper (section 4.1) unless `BOUND`.
2. Counts, per alternative list, the interrupt descriptors whose `Flags`
   carry `CM_RESOURCE_INTERRUPT_MESSAGE` (0x0002, declared by the HCD where
   the Windows 2000 DDK lacks it) and those that do not, and records both
   counts (section 9) - this is the runtime reading of what the PCI driver
   offers.
3. If `BOUND`: changes nothing.
4. Else strips: in each alternative list, removes every message-interrupt
   descriptor, compacting the list in place (`Count` and the requirement
   list's `ListSize` reduced; no pool). Where a removed descriptor headed
   a run of alternatives (its `Option` without `IO_RESOURCE_ALTERNATIVE`)
   and the next kept descriptor continues that run (`IO_RESOURCE_ALTERNATIVE`
   set), that bit is cleared on it so it heads the run. Nothing else in a
   descriptor is changed. A `CmResourceTypeDevicePrivate` descriptor that
   immediately follows a removed message descriptor in the same run is
   removed with it only if the step-2 reading shows the PCI driver pairs
   them (section 11, open reading R2); until read, none is removed.
5. Completes the IRP with the forwarded status and the list (the same
   pointer, shrunk in place).

The strip is a pure function over the list's bytes (`XhciMsi86Strip`, in
the DDK-free core, layouts declared with `C_ASSERT`s against the DDK's
`IO_RESOURCE_DESCRIPTOR`), with host vectors in `test_msi`: one message
descriptor alone; message then line alternative; line then message
alternative; two alternative lists; message with `MessageCount` > 1; no
interrupt at all; a list that is all message descriptors (the controller
with pin 0: stripped to no interrupt, so the start polls with "no
interrupt resource" - exactly what XP does with that controller).

**On a list whose alternatives offer no line** - a controller with
`Interrupt Pin` 0 - the strip leaves no interrupt and the start polls with
"no interrupt resource", which is the XP behaviour for that controller.
On a controller with a pin, the decision's "exactly as on XP" rests on the
PCI driver offering the line as an alternative to the message; that is
what `IoConnectInterruptEx`'s `FallBackServiceRoutine` contract implies
(Windows may assign the line where a message was asked for), and it is
read on the guests before the code is taken as done (section 11, R1). If
R1 shows no line alternative, the strip is withdrawn, the no-helper start
takes record 15's "message not connected" row and polls, and the owner is
told that "exactly as on XP" cannot hold for a helper-less install with
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
  With a working helper this row is reached only through a grant the
  filter did not see; it is recorded as such (section 9).

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
which removes the hardware key, or to set `MSISupported` 0 first; leg D1
reads what 2.2.0.2 actually does with the grant, so the note can say how
it fails. **For the orchestrator:** this is common to both architectures
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
| Import gate | A new profile `nt6x86` in `check-imports.ps1`: an x86 image checked against the export tables of Vista SP2 x86 and 7 SP1 x86 `ntoskrnl.exe`/`ntkrnlpa.exe` and `hal.dll` (a new `nt6x86-baselines.expected`, extracted by `extract-target-baselines.ps1`; `ntkrnlpa.exe` of both is to be extracted, today's `tools\vista-x86-extracted` and `win7-x86-extracted` carry `ntoskrnl.exe` only), with no Windows 2000 or Windows 98 evidence asked. `xhcimsi-imports.allow`: `IoConnectInterruptEx`, `IoDisconnectInterruptEx`, `IoCreateDevice`, `IofCompleteRequest`, `IoIsWdmVersionAvailable`, `RtlInitUnicodeString` (`all`), `RtlQueryRegistryValues` (`qemu`), each tagged `vista-x86-export; win7-x86-export`; the profile refuses any `xhci98-imports.allow` row being reused for it. `xhci98-imports.allow` gains `ntoskrnl.exe!IoGetDeviceObjectPointer` (`all`, `w2k-export; win98-precedent 1394bus.sys 4.10.2222`), and `win98-evidence.list` gains `1394bus.sys`. Self-test mutations in `test-flavour-rules.ps1` for the profile (a Windows 2000-only pair passes nothing; a Vista pair in the HCD's x86 list fails) |
| INF gate `check-inf.ps1` | New `MSIHLP-*` rules, x86 only: `MSIHLP-ONLYNT6` (the helper's service, copy section or file named anywhere but `Xhci.Dev6.NTx86`, `[SourceDisksFiles]` and `[DestinationDirs]`); `MSIHLP-SVC` (the second `AddService` on `Xhci.Dev6.NTx86.Services` only, flags 0, `ServiceType` 1, `StartType` 0, `ErrorControl` 1, `ServiceBinary=%12%\xhcimsi.sys`, copied by that section's own `CopyFiles`); `MSIHLP-HW` (on x86, record 15's `MSI-*` lines only in `Xhci.Dev6.NTx86.HW`, and only when `MSIHLP-SVC` holds); `MSIHLP-AMD64` (no helper in the 64-bit INF). `PATH-NT`'s "exactly one `AddService`, flag 0x2, `StartType` 3" is relaxed for that one section by exactly the helper's line. `BOTH-VERSION` reads `src\msi86\xhcimsi.rc` too. Mutations in `test-inf-checks.ps1` for each rule; the `Dev6` mutations rebased on the new section text; `expected-footprint.txt` regenerated (`-EmitFootprint`) |
| Flavour marker | The helper embeds `XHCI98_FLAVOUR_{RELEASE,DEBUG,QEMU}` as `uas_entry.c` does; `check-flavour-marker.ps1` unchanged |
| Source stamp | `source-stamp.ps1 -Driver` gains `xhcimsi` (`src\msi86`, `xhcimsi.srcstamp`, with `xhci_msi86.h` and `xhci_version.h` as parent headers) |
| Charset gate | `check-source-charset.ps1`'s object-directory skips gain `^src\\msi86\\obj` |
| Version resource | `src\msi86\xhcimsi.rc` includes `../xhci_version.h`, `FILEVERSION`/`PRODUCTVERSION` from the macros, `VS_FF_DEBUG` for checked builds, names `xhcimsi.sys` |
| `make-package.ps1` | `-Arch x86` stages `xhcimsi.sys` (the staging table; `$MsiDriverPath`), gated like the UAS pair: exists, flavour marker, import gate with the `nt6x86` profile; `-Arch amd64` refuses one |
| `make-release.ps1` | x86 legs: `xhcimsi.sys` in the declared media (`$publishable` for x86), and `Assert-MsiHelperPublished` modelled on `Assert-UasPublished` (file version, `VS_FF_DEBUG` against the flavour, the marker, bytes equal to `src\msi86\obj...\xhcimsi.sys`, `source-stamp -Driver xhcimsi -Check`); x64 legs refuse it; the readme and contents text name it |
| `test-package.ps1` | stand-ins for the helper in x86 packages, none in x64 |
| `legal-provenance.md` | section 2's toolchain row: WDK 7.1 now also builds an x86 binary (the helper), with `BUFFER_OVERFLOW_CHECKS=0`; section 4: a row for the NT 6 x86 export readings (static: the export tables of the four kernels and two HALs) and for R1-R3 (runtime, through the driver's own log) |
| Host tests | `test_msi` (record 15's suite) gains `XhciMsi86Accept` and `XhciMsi86Strip`; `test\run-host-tests.cmd` unchanged otherwise |

### 8.3 Shared tables touched (for the integrator)

Appended, never reordered: the counter block's new fields (section 9);
`XHCISNAP`'s names for them (36.9); the INF gate's rule list and its
footprint; the import allowlist's one row; `sources` (`hcd_msi86.c`); the
`test_msi` vector list.

## 9. What is recorded

In the counter block (appended) and the start's log note, readable by
`XHCISNAP` from `XhciLogVerbosity` 1:

- `Msi86State` and its detail: the `IoGetDeviceObjectPointer` status for
  `ABSENT`; which acceptance test failed, the IRP status, `HelperMajor`,
  `Returned` for `REFUSED`; `HelperVersion` and `HelperMinor` for
  `BOUND`; the `Flags` word (a `qemu` helper says so);
- at the last filter: message and line interrupt descriptors seen, per
  alternative list (up to four lists, then a count of the rest), and how
  many were stripped;
- at the start: "routines from the helper" or "helper not bound", as
  record 15's "how the routines were found";
- a message grant that reached a start with the helper not bound (the
  row of section 4.3 that should not occur after a filter).

## 10. The legs

On QEMU guests, the `release` package for functional and regression legs
and the `qemu` package for injection legs, as the brief requires; host
"development host A".

**M - MSI with the helper**, on Vista SP2 x86 and 7 SP1 x86, each after a
reboot that follows the install: record 15 section 4.3's x86 set -
`qemu-xhci` with `msi=on,msix=off` (read as MSI), `msi=off,msix=on` (MSI-X),
the launchers' default (whatever Windows granted); modes 0 and 3; HID,
Bulk-Only storage and audio; disable and enable, remove and rescan, a
forced recovery, D3 and back; `XHCISNAP` naming the delivery, the helper
`BOUND` with its version, and the filter's reading. Then `MSISupported` 0,
read as the line with nothing stripped; mode 1 under a grant, read as the
message with its log note; a raised `MessageNumberLimit`, read as several
granted, served as one.

**H - the helper missing or wrong**, on 7 x86 (and the first two on Vista
x86), each read as the line, the requirement list stripped, and the
helper's state with its detail:

- H1 fresh install, before the reboot (section 5): line, `ABSENT`; after
  the reboot: MSI, `BOUND`;
- H2 `xhcimsi.sys` deleted, service left: `ABSENT`, an event logged, boot
  normal;
- H3 the service deleted: `ABSENT`;
- H4 to H9 (`qemu` package) `XhciMsiInject` 1 to 6 (section 2.1): failed
  load (`ABSENT`), refused, other major, short, and the two malformed
  answers (`REFUSED`, each test named);
- H10 the HCD's own `qemu`-flavour failed connection (record 15) with the
  helper bound: polling, "message not connected" - the one place the
  seam's failure row is read on x86.

**D - down-level and mixed**:

- D1 7 x86 with this package, then 2.2.0.2 installed over it without
  uninstalling: what the older HCD does with the left-over grant, recorded
  for the release notes (section 6);
- D2 Windows 98 SE, ME, 2000 SP4 and XP SP3 x86: install from the new
  media and read as the release before it (the line; no helper copied;
  `IoGetDeviceObjectPointer` resolving at load on 98 SE and ME);
- D3 Vista x64 and 7 x64: no helper in the package, MSI as record 15 has
  it (that leg is 36.16's; here only that the x64 package is unchanged by
  this record);
- D4 7 x86 with Driver Verifier on `xhcimsi.sys` and `xhci98.sys`, through
  M's first variant and a disable and enable.

**R - readings that gate the code** (section 11) are taken first, on the
`qemu` package of the first build.

## 11. Open readings

- **R1.** The requirements list Vista's and 7's PCI driver builds for a
  controller with a pin and `MSISupported` 1: does each alternative list
  offer a line interrupt beside the message one, and in which order and
  `Option`? Read through section 9's filter record on both guests with
  the strip disabled by a `qemu`-flavour switch of the HCD. If no line is
  offered, section 4.2's fallback applies.
- **R2.** Whether a message descriptor is paired with a
  `CmResourceTypeDevicePrivate` descriptor in that list.
- **R3.** That an unsigned boot-start x86 driver loads on Vista SP2 and 7
  SP1 x86 (section 5).

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

## 13. Files the code touches

New: `src\msi86\sources`, `src\msi86\makefile`, `src\msi86\msi86_entry.c`,
`src\msi86\xhcimsi.rc`, `src\xhci_msi86.h`, `src\hcd_msi86.c`,
`scripts\import-gate\xhcimsi-imports.allow`,
`scripts\import-gate\nt6x86-baselines.expected`.

Changed: `src\xhci98.inf`, `src\sources`, `src\hcd.h`, `src\hcd_pnp.c`
(the filter and the remove), `src\hcd_ctl.c` (the x86 caller of
`hcdMsgConnect`), `src\hcd_entry.c` (the state at `AddDevice`), the
counter block and log (`src\hcd_log.c`, `src\xhci.h` where record 15's
fields live), `test\test_msi.c`; `scripts\build-driver.cmd`,
`scripts\import-gate\check-imports.ps1`, `xhci98-imports.allow`,
`win98-evidence.list`, `extract-target-baselines.ps1`,
`test-flavour-rules.ps1`, `scripts\inf-gate\check-inf.ps1`,
`test-inf-checks.ps1`, `expected-footprint.txt`,
`scripts\source-stamp.ps1`, `scripts\check-source-charset.ps1`,
`scripts\package\make-package.ps1`, `make-release.ps1`,
`test-package.ps1`; `docs\contributing\legal-provenance.md`,
`docs\contributing\build-and-test.md` (the x86 build's WDK, the log lines,
the legs), `docs\contributing\source-files.md`, the release notes and
`docs\using` (the first-boot line, the downgrade note, removing the
service); `xhcisnap` (36.9's names).

## 14. The reviews

(Filled in by the review loop.)
