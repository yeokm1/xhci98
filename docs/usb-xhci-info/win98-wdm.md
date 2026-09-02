# Windows 98/2000 WDM Constraints

"Oney p.N" citations in the project refer to Walter Oney, *Programming the
Microsoft Windows Driver Model*, 2nd ed. (2003), using the page number printed
in the book. In the reviewed PDF, the corresponding PDF page index is printed
page + 18.

Both Win98 SE and Win2000 SP4 are first-class targets, served by one binary.
Most of this document is about Win98, because that is where the
compile-and-load constraints come from: it exports the least, so its baseline
is the API ceiling for everything. Win2000 adds almost no constraints of that
kind. It adds behavioural ones, collected in "Windows 2000 as a co-primary
target" below. Read both sections before writing code. A rule satisfied on
Win98 is frequently not satisfied on Win2000, and Win98 will not tell you.

## WDM Version Differences

| Feature | Win98 SE | Win2000 |
|---|---|---|
| WDM version | 1.0 | 1.10 |
| `IoGetDmaAdapter` | Available | Full |
| `KeSetTimerEx` | No | Yes |
| `ExAllocatePoolWithTag` | Exported (measured; see the note under "Functions missing or dangerous on Win98/98 SE"), denied by the import gate as policy | Yes, denied the same way |
| `IoAllocateWorkItem` | No | Yes |
| PnP power management | Partial | Full |
| WMI | No | Yes |

For maximum compatibility across both targets, use only the Win98 SE
baseline APIs. A runtime version check cannot make a missing import safe: the
Win98 loader resolves every import before `DriverEntry` runs. Use a version
check only to select between behaviors whose referenced symbols all exist on
every target (or build separate binaries, or supply a compatibility stub). Do
not use `PsGetVersion` for the check; per Oney (p.437) it is itself not
exported on Win98/Me. The version primitive present on all three targets is
`IoIsWdmVersionAvailable`:

```c
/* IRQL: PASSIVE_LEVEL (call from DriverEntry). Win98 gold/SE report 1.0;
   Me 1.05; Win2000 1.10; XP 1.20. */
BOOLEAN isWin9x = !IoIsWdmVersionAvailable(1, 0x10);   /* 1.10 unavailable => 9x */
```

`IoIsWdmVersionAvailable` cannot separate the original Win98 retail release
from Win98 SE. Both report 1.0, yet they differ in what they export (the
remove-lock family, for one). This project targets SE, so nothing in it needs
that distinction. The only discriminator Oney gives (p.437) is a
`DriverEntry`-time check of the driver object, and he limits it to
`DriverEntry` because the field may change afterward:

```c
/* IRQL: PASSIVE_LEVEL, DriverEntry only. Empty on Win98 gold.
   Gate the field check so WinMe and the NT line do not look like Win98 SE. */
BOOLEAN isWin98SE =
    isWin9x &&
    !IoIsWdmVersionAvailable(1, 0x05) &&
    (DriverObject->DriverExtension->ServiceKeyName.Length != 0);
```

Record the result in a global at `DriverEntry` time if a later path needs it;
do not re-read the field. The version predicates are essential:
`ServiceKeyName` is also nonempty on Win2000, and WinMe is WDM 1.05, so the
field test alone does not identify Win98 SE.

Use the real `ExAllocatePool` entry point (not `ExAllocatePoolWithTag`), but
see the local-DDK macro trap below.

### Imports are a silent load-time gate

Source: Oney Appendix A (p.437-442), a 2003 secondary source. The
load-before-`DriverEntry` rule is confirmed by the project's observed
`usbhub20.sys`/`USBD.SYS` failure (`docs/contributing/lessons.md`). Exact
Win98 symbol availability must still be checked on the target.

On Windows 98, `NTKERN.VXD` and related system components provide the
NT-style module exports used by WDM drivers. **If `xhci98.sys` has even one
unresolved module/symbol import, the driver never reaches `DriverEntry`.**
The failure can look like a bad INF, a wrong hardware ID, or an early crash:
a yellow bang and silence. The Win2000 DDK headers will happily let the
driver reference functions the Win98 target cannot resolve, so this is the
dangerous build direction.

There is no runtime escape from inside the driver. `MmGetSystemRoutineAddress`
(the kernel-mode `GetProcAddress`) is also missing on Win98/Me (p.438), so
`xhci98.sys` cannot resolve a missing symbol dynamically. For this driver the
decision is static, made at link time.

The one documented escape is external, and this project does not use it. On
Win98 the NT-style export tables are not a file; `NTKERN.VXD` builds them at
init and the loader resolves against them through `_PELDR_GetProcAddress`. A
separate driver loaded first can call `_PELDR_AddExportTable` to extend those
tables (name/ordinal/address triples), after which a later-loaded driver links
against the added symbols normally. Oney's `WDMSTUB.SYS` does this
(p.439-440), sequenced ahead of the function driver by the multi-value
`NTMPDriver` INF syntax in `docs/contributing/build-and-test.md`. Two
consequences worth recording:

- A host-side `dumpbin /exports` of any Win98 file is not a valid import
  check, because the authoritative table is built at run time. The target-side
  gate in `docs/contributing/build-and-test.md` is the check that matters.
  Scanning `ntkern.vxd` for the NUL-delimited export name strings it builds
  those tables from is useful positive evidence and worthless negative
  evidence. `DbgPrint`, `WRITE_PORT_UCHAR`, `ExAllocatePool`,
  `ExAllocatePoolWithTag`, `KeStallExecutionProcessor` and
  `READ`/`WRITE_REGISTER_ULONG` are all in there; `KeGetCurrentIrql` and
  `KeQuerySystemTime` are not, yet Windows 98's own `NTMAP.SYS` imports the
  first from `HAL.DLL` (the allow row's recorded precedent) and NUSB's
  `usbehci.sys` imports the second from `NTOSKRNL.EXE` on this platform. So
  the gate treats a hit as evidence and a miss as no information.
- Adding a stub driver to the package is a real option if one unavoidable
  symbol blocks the load, but it doubles the install surface on the platform
  whose install path is already the least reliable, and Oney's own binary is
  licence-encumbered (p.442). Treat it as a last resort. Choosing symbols the
  target already exports remains the rule.

Measured local toolchain trap: both `tools\ntddk\inc\wdm.h` and
`inc\ddk\ntddk.h` unconditionally define `POOL_TAGGING`, then define
`ExAllocatePool(a,b)` as `ExAllocatePoolWithTag(a,b,...)`. So the source
spelling recommended for Win98 still emits the unavailable tagged import with
this exact DDK. After the DDK include, the compatibility header must do:

```c
#ifdef ExAllocatePool
#undef ExAllocatePool
#endif
```

The real two-argument prototype was declared earlier in the same header, so
subsequent calls bind to the actual `ExAllocatePool` export. Verify that fact
in the linked binary; do not trust the source spelling.

Audit imports as module/symbol pairs, not as a flat union of names. The
known-good NUSB `usbehci.sys` imports from `NTOSKRNL.EXE`, `HAL.DLL`
(`KeStallExecutionProcessor`), and `USBPORT.SYS`, so HAL is expected when
this miniport uses the same stall primitive. `usbport.sys` does not
"re-export" arbitrary kernel functions. See
`docs/contributing/build-and-test.md`, "Post-link import-compatibility gate".

### Functions missing or dangerous on Win98/98 SE

Source: Oney per-chapter "Windows 98/Me Compatibility Notes" and Table A-2
(secondary source). Confirm each against the target-side import check and
linked binary before relying on the workaround. Rows marked "Option B only"
only bite if the Phase 3 spike forces the monolithic fallback.

| Function | Win98/98 SE status | What to do instead |
|---|---|---|
| `ExAllocatePoolWithTag` / `ExFreePoolWithTag` | Oney says missing; measured otherwise on 98 SE (see the note below the table) | Undefine the local DDK's `ExAllocatePool` macro as shown above, then call real `ExAllocatePool`; use `ExFreePool`. The import gate denies the tagged names |
| `IoAllocateWorkItem` / `IoQueueWorkItem` / `IoFreeWorkItem` | Not exported (p.375) | Option A: use usbport's uncancellable `UsbPortRequestAsyncCallback` with miniport-owned synchronization plus generation/lifecycle validation; Option B: `ExInitializeWorkItem` / `ExQueueWorkItem` only with explicit lifetime protection |
| `IO_REMOVE_LOCK` family (`IoReleaseRemoveLockAndWait`) | Missing on 98 SE/Me; 98 gold has none of the family (p.191) | Do not use remove locks; roll your own outstanding-I/O count |
| `MmGetSystemAddressForMdlSafe` | Expands to missing `MmMapLockedPagesSpecifyCache` (p.225) | Option B only: set `MDL_MAPPING_CAN_FAIL`, call `MmMapLockedPages`, check NULL, then restore the flag |
| `MmGetSystemRoutineAddress` | Missing (p.438) | No runtime symbol resolution on 98 - decide statically |
| `PsGetVersion` | Missing (p.437) | `IoIsWdmVersionAvailable` (above) |
| `KeReadStateEvent` | Missing (p.95, p.110) | Use a state variable updated with the event, or a zero-time wait when its consume/reset side effects are intended |
| `KeWaitForSingleObject` on a `PKTHREAD` | **Crashes the system** (p.110, p.375) | Never wait on a thread object. Have the thread signal a `KEVENT` immediately before `PsTerminateSystemThread`, and wait on that event |
| `KeWaitForSingleObject` / `KeWaitForMultipleObjects` (general) | Can return undocumented `0xFFFFFFFF` (p.93) | Never treat it as success. Retrying is valid for the ordinary "another thread terminated while this wait was blocked" case; Oney reports no workaround for the VxD event-time nested-block case, so do not turn it into an unbounded retry loop |
| `ZwCreateFile` / `ZwReadFile` / `ZwWriteFile` during early start | File-system services may not be initialized when a boot/PnP device starts (p.82) | Never perform file I/O from `DriverEntry` or the miniport `StartController` path |
| `IoWriteErrorLogEntry` | Win98 has no persistent kernel error log or Event Viewer; output only reaches the debug terminal (p.375) | Do not treat it as field telemetry. Use the driver's own log ring, read back with `XHCISNAP`; that is the field channel on both targets since `0.0.0.6`. In a VM the `qemu` flavour's port-`0xE9` trace is the richer one. The shipping `debug` build has no live trace, and there is no serial sink |
| `PoStartNextPowerIrp` | No-op on 98 (p.254) | Option B only; a no-op on 98 is easy to forget, then it deadlocks Win2000 |
| `Po{Register,Cancel}DeviceNotify`, `Po{Register,Set,Unregister}SystemState` | Not implemented (p.254) | Option B only; avoid entirely |
| `KeNumberProcessors` | Not exported; the WDMSTUB replacement hard-codes 1 (p.441) | Never reference it. Win98 is uniprocessor by construction (see below); if per-CPU code is ever needed it belongs behind a Win2000-only build, not a runtime check |
| `HalTranslateBusAddress` | Only partially available; WDMSTUB re-implements a subset (p.441) | Option B only. Use the *translated* resources PnP already hands to `IRP_MN_START_DEVICE`; do not translate bus addresses yourself |
| `IoReportTargetDeviceChange` / `...Asynchronous` | Fails with `STATUS_NOT_IMPLEMENTED` / symbol not exported at all (p.191) | Do not use. The async form is a load-blocking import, not a runtime failure |

`ExAllocatePoolWithTag` is exported on Win98 SE, whatever Table A-2 says
(measured while building the import gate). Win98 SE's own `usbd.sys` and
`usbhub.sys` (4.10.2222, the USB 1.1 stack it loads on any machine with a
UHCI/OHCI controller) import `NTOSKRNL.EXE!ExAllocatePoolWithTag`, and so do
five drivers in the NUSB set installed on the 2a VM. If that symbol did not
resolve, USB would not work on a stock 98 SE machine at all. The name is also
present in `ntkern.vxd`'s export name table. Oney's row is most plausibly
about Win98 gold, which this project does not target.

The rule above stands anyway, for a reason that does not depend on the row
being right: under Option A the miniport should not be allocating private
pool at all. So the compatibility header still undefines the macro, and
`scripts\import-gate\xhci98-imports.allow` denies both tagged names outright.
The reason is policy rather than a proven missing export, and the gate says so
when it fires.

Under Option A, do not create private helper threads or DPCs for work that
usbport's callback/timer services already cover. The thread/wait rows remain
guardrails for an unavoidable helper and for the Option-B fallback.

One more rule for any wait this driver does take: pass `KernelMode` and
`Alertable = FALSE` to `KeWaitForSingleObject`. A `UserMode` wait authorizes
the Memory Manager to swap out the waiting thread's kernel stack, so a
dispatcher object declared as an automatic variable can be paged out while
another context signals it, which is a bug check at elevated IRQL (Oney
p.92). Never place a `KEVENT` on the stack; put it in the miniport device
extension. This applies directly to the thread-termination event that
replaces the forbidden `PKTHREAD` wait above.

### DriverEntry and DriverUnload differ on Win98

Source: Oney ch.2.6 (p.43), a 2003 secondary source.

`RegistryPath` is not an NT path. On Win2000 `DriverEntry` receives
`\Registry\Machine\System\CurrentControlSet\Services\<name>`; on Win98 it
receives `System\CurrentControlSet\Services\<classname>\<instance#>`.
`ZwOpenKey` accepts either, but string-matching or path-appending on
`RegistryPath` is not portable. Win98's registry layout differs underneath as
well (p.418): hardware keys live under `HKLM\Enum`, class keys under
`HKLM\System\CurrentControlSet\Services\Class` keyed by class name, and the
service key barely matters, since the Configuration Manager loads from the
driver key. If per-controller quirk overrides ever become registry-tunable,
they must be read from a key opened from the supplied `RegistryPath`, never
from a hard-coded path.

`DriverUnload` re-enters from `IoDeleteDevice`. On Win98, `IoDeleteDevice`
called from within `DriverEntry` calls `DriverUnload` before `DriverEntry` has
returned. The Phase 3 spike's `DriverEntry` can fail after
`USBPORT_RegisterUSBPortDriver` has already replaced `DriverUnload` (see
`docs/usb-xhci-info/usbport-miniport-abi.md`), so its error path must be safe
to run against half-initialized state and must not double-free. Prefer failing
before creating anything that needs deleting.

`DriverObject->DriverStart` and `DriverSize` are zero on Win98. NTKERN does
not fill them, so anything that derives the image base from the driver object
(the checked build's "image base" trace line, added to attribute fault
addresses to a module) works on Win2000 only (Phase 3 task 8, runtime). Do
not build a diagnostic on those two fields that has to hold on both targets.

### Win98 is uniprocessor, which hides races rather than preventing them

Win98/Me has no SMP support: spin lock primitives only raise IRQL, and
nonpaged driver code at PASSIVE_LEVEL is not preempted unless it blocks on a
dispatcher object or takes a page fault (Oney p.110). Correct-looking code can
therefore pass indefinitely on Win98 and fail on a multiprocessor Win2000
machine. Oney's advice is to do the synchronization debugging on the NT-line
system so the bugs surface, and that is the standing rationale for the Phase
2d SMP Windows 2000 VM in `docs/contributing/roadmap.md`. Write the ISR/DPC
and ring ownership rules as if two CPUs were real; do not let a green Win98
run stand in for that evidence.

## Windows 2000 as a co-primary target

Source: Oney chs. 4-8 and Appendix A, a 2003 secondary source, read for the
Win2000-vs-XP direction rather than the Win98-vs-XP one the book is organized
around. Oney writes "Windows XP" for the NT line throughout; where a feature
is flagged as new in the XP DDK it is by definition absent from the Win2000
DDK this project builds with.

Win2000 is not a laxer Win98. It is a stricter system that happens to export
more, and the two targets fail in opposite directions:

| | Win98 SE | Win2000 SP4 |
|---|---|---|
| Loader | Rejects the binary for any unresolved import, before `DriverEntry` | Same rule, but exports a superset of Win98 - so Win98's baseline already satisfies it |
| CPUs | Uniprocessor forever; spin locks only raise IRQL | Real SMP; spin locks really spin |
| Preemption | Nonpaged PASSIVE code is not preempted unless it blocks or faults | Preempted at any time |
| Mistake tolerance | Forgiving - many Po/PnP calls are no-ops | Strict; the no-ops become real and are required |
| Tooling | No Driver Verifier; kernel debugging only via the assembly-level `WDEB386.EXE` | Driver Verifier, WinDbg/KD over serial with symbols |
| Install | 16-bit setup, no signatures, `DevLoader`/`NTMPDriver` | NT setup, unsigned-driver warning, `AddService` |
| `usbport.sys` | Back-ported NUSB build `5.00.2195.5652` | Native SP4 build `5.00.2195.6681` |

### The API ceiling does not move, but the failure direction reverses

Because Win2000 exports a superset of Win98, the existing rule (use only the
Win98 SE baseline) already guarantees the binary loads on Win2000. Adding
Win2000 as a target does not shrink the usable API surface.

What it adds is a second way to break the load, in the opposite direction:
referencing an API that is newer than Win2000. The Win2000 DDK is the primary
guard here (it declares no XP-only routine, so the build simply fails), but
that guard is defeated by copying code from XP-era samples or documentation.
The routines most likely to be reached for by someone writing this kind of
driver, all of which are XP-and-later and must not be used:

| XP-only | Use instead on Win2000 |
|---|---|
| `KeAcquireInterruptSpinLock` / `KeReleaseInterruptSpinLock` (p.124, p.204) | `KeSynchronizeExecution` with a SynchCritSection routine. Option B only - under Option A `usbport.sys` owns the interrupt object |
| `IoCsqXxx` cancel-safe queues (p.136) | First described in an XP DDK release. They live in a static library, so they are usable on older *systems* - but that library is not in the Win2K DDK, so they are not usable in this *build*. Option B only |
| `IoForwardIrpSynchronously` (p.190) | Set a completion routine that signals a `KEVENT`, then `IoCallDriver` + wait - the pattern it encapsulates |
| `IoSetCompletionRoutineEx` (p.441) | `IoSetCompletionRoutine`, with the driver keeping its own unload interlock |

One benign exception worth knowing so it is not "fixed" by mistake:
`InterlockedOr` / `InterlockedAnd` / `InterlockedXor` are new with the XP DDK
but are implemented as compiler intrinsics, so they generate no import and run
on earlier systems (p.106). MSVC 6 predates them, so they are unavailable here
for a different reason; use `InterlockedExchange` and
`InterlockedCompareExchange`.

The post-link import check has a Win2000 half, and unlike the Win98 half it
can be settled on the build host. Win2000 has real kernel/HAL files on disk,
so `scripts\import-gate\check-imports.ps1` resolves every `ntoskrnl.exe`
import against both SP4 kernel images (UP and SMP), and every `hal.dll` import
against all eight HAL images on the media (Standard-PC, MPS, the ACPI variants
real hardware can select, and the uncompressed `halborg.dll` that a
`HAL*.DL_` pattern misses). It first authenticates every file by version,
length, and SHA-256 and fails on a partial or substituted set.

That is Oney's `DEPENDS` recipe (p.439) done before the deploy rather than
after. Running `DEPENDS` on the 2b VM remains a valid confirmation, not a
prerequisite. See `docs/contributing/build-and-test.md` "Post-link
import-compatibility gate".

### Win2000 enforces what Win98 silently forgives

This is the substantive reason Win2000 cannot be treated as "Win98 but nicer",
and the `Po*` cluster is the worked example. Oney's framing for the NT line
(p.253) is that Win98/Me will forgive mistakes the NT-line system will not, so
a driver developed and tested only on Win98 has not been tested. Concretely,
from the Power Management section below:

- `PoStartNextPowerIrp` is a no-op on Win98. Omitting it is invisible there
  and deadlocks the power queue on Win2000.
- `PoCallDriver` is literally `IoCallDriver` on Win98, so forwarding power
  IRPs with the wrong one works there and is wrong on Win2000.
- `PoSetPowerState` does nothing on Win98 and returns its own argument.
- `DO_POWER_PAGABLE` has different consequences on each system.

The same asymmetry applies beyond power: Win98 never sends
`IRP_MN_SURPRISE_REMOVAL`, so a missing handler is untested there; and Win98's
absence of preemption and of real spin locks means a missing or wrongly-scoped
lock cannot fail there at all. Treat every "works on Win98" result for
lifecycle, power, and locking as unmeasured until the same build has run on
Win2000 and, for locking, on the Phase 2d SMP VM.

### What Win2000 gives back

Driver Verifier, which has no Win98 equivalent. Use the options Win2000 SP4
provides, including Special Pool, Pool Tracking, and Force IRQL Checking.
Verifier runs are mandatory from Phase 4's checkpoint onward; see
`docs/contributing/build-and-test.md` "Run Driver Verifier here". Deadlock
Detection was added in Windows XP and is not available on Win2000; Phase 2d's
contended SMP stress plus the static lock-order review cover that gap.

A supported kernel debugger: WinDbg/KD over serial, with symbols and kernel
awareness. The advantage is the quality of the tool rather than its
existence. Win98 is not debugger-less; it has the 9x DDK's `WDEB386.EXE` over
the same kind of serial link. But KD's protocol does not support Win9x, and
WDEB386 is assembly-level and awkward enough to be a last resort
(`docs/contributing/build-and-test.md`, "Debug Build Output"). So when a bug
reproduces on both targets, debug it here. See the instrumentation ladder in
`docs/contributing/failure-diagnosis.md` for where each debugger sits by setup
cost.

Full WDM 1.10, including the routines the Win98 rows above forbid. The
temptation is to use them behind an `IoIsWdmVersionAvailable` check; do not.
A runtime check does not make the import resolvable on Win98 (see "Imports
are a silent load-time gate"), so the symbol must be absent from the binary
regardless of who calls it.

### Two `usbport.sys` builds, both required

The miniport binds a different build on each target: NUSB's back-ported
`5.00.2195.5652` on Win98, native SP4 `5.00.2195.6681` on Win2000. Phase 3's
static pass confirmed that they share an export set, ordinals, registration
version range (`>= 100`, with the full packet selected at `>= 200`), 316-byte
USB2 packet layout, and `USBPORT_GetHciMn = 0x57324B30`. That is why one
binary is expected to serve both, but "expected" is not "observed". The
registration packet still must be exercised against both builds, and any
later divergence in callback contract is a real portability problem, not a
NUSB quirk to work around. See
`docs/usb-xhci-info/usbport-miniport-interface.md` "Target ABI record".

### Small portability rules worth fixing once

- Name symbolic links in `\DosDevices`, never `\GLOBAL??`. Win98 does not
  understand the latter, and `\DosDevices` works on both (p.43).
- Pass the PDO to `PoRegisterDeviceForIdleDetection`; Win98 requires it and
  Win2000 accepts it (p.253).
- `$CHICAGO$` is the INF `Signature` that works on every WDM platform (p.383);
  do not switch it to `$Windows NT$` when adding the Win2000 install path.

### What about Windows XP?

Position: best-effort secondary, and the only one. It shares the one binary
and no checkpoint waits on it. Preserve XP compatibility when the
accommodation is small, isolated, and low-risk; do not weaken Win98 SE or
Win2000 behavior, add XP-only imports, create a separate implementation, or
take on XP validation work to do so. This section exists so the boundary is
not re-litigated. (32-bit XP only in any case; XP x64 is a different build and
has never been in scope.)

Much of the compatibility comes for free. The driver codes to the Win98 export
baseline and builds with the Win2K DDK; XP exports a strict superset of both,
so nothing the binary references can be missing there, and the XP-only-API
rule above protects XP by construction. XP's `usbport.sys` (`5.1.2600.x`) is
the direct descendant of the Win2000 one, and the INF's `.NTx86` + `AddService`
half already works there.

One further point cuts mildly in XP's favour, and it is an inference, not a
measurement: the ABI reference this project builds against is itself XP-era.
ReactOS reimplements the NT5.1 stack
(`docs/usb-xhci-info/usbport-miniport-interface.md` section 1,
`docs/usb-xhci-info/usbport-miniport-abi.md` "Trust order"), so where
`docs/usb-xhci-info/usbport-miniport-abi.md` and the `5.00.2195.x` binaries
disagree, the transcription may well be describing XP's layout. That is a
reason to record what XP does, not a reason to believe it in advance.

What the static pass established: XP SP3 uses the same `>= 100` / `>= 200`
registration-version tests, the same 316-byte USB2 packet, the same service
block, and the same tail layout as both 2195.x builds. Packet format therefore
does not rule XP out of Option A. Its `USBPORT_GetHciMn` value is
`0x10000001`, while both primary targets return `0x57324B30`; accepting both
known values in the optional sanity probe is the kind of small, isolated
accommodation this policy permits. None of this proves callback call
contracts, lifecycle behavior, or end-to-end compatibility, so XP remains
best-effort and unvalidated. One related observation exists since 2026-09-02:
SweetLow's Windows 98 rebuild of XP SP2's `usbport.sys` (5.1.2600.2180,
returning `0x10000001`) registers this driver, runs HID and mass storage, and
completes the controller stop on Windows 98
(`docs/usb-xhci-info/usbport-miniport-interface.md` section 5, "The SweetLow
rebuild"). That is XP-lineage code, but on Windows 98's kernel, in a VM; it
says nothing about Windows XP itself.

Why it is not promoted to a checkpointed target: cost, not a technical
obstacle. "Target" in this project means every checkpoint is observed on it,
which means a third VM, a third observation per phase, and real-hardware
validation per OS. That is a recurring tax on every phase even though the
static registration gate looks compatible.

"XP already has xHCI drivers" is not a reason either. It does, and none of
them apply to the hardware this project runs on. Renesas/NEC, ASMedia, Fresco
Logic, VIA, and Etron all shipped XP xHCI drivers, but every one of those is
discrete add-in silicon, a PCIe card or ExpressCard. No integrated PCH/SoC
xHCI ever got an XP driver: Intel's xHCI driver line begins at Windows 7, and
AMD's xHCI postdates AMD's XP support.

On every machine in this project's own test fleet (`docs/contributing/build-and-test.md` "Available Test Hardware":
Skylake and Comet Lake), XP has exactly as many xHCI options as Win98 and
Win2000: none. The escape hatch that argument assumes (buy a card) is equally
available to Win2000, which is a full target anyway.

What the vendor evidence does say is that every one of those drivers was a monolithic stack with its
own hub driver rather than a usbport miniport, cited elsewhere in this
document as evidence that the miniport fit is nontrivial. Nobody shipped an
xHCI-as-usbport-miniport for XP either.

The static registration-format question is settled in Phase 3. When the spike
disassembles the version check on the two current builds, it inspects an XP
`usbport.sys` at the same time. That needs no VM and no install (just the
binary), and can establish the accepted version constant, copied packet size,
and visible field offsets. It cannot establish runtime compatibility.

That pass is `docs/contributing/roadmap.md` Phase 3 task 1, which confirms the
packet against both shipping builds before any code is written; XP rides
along as one more file. No checkpoint depends on it and a missing ISO blocks
nothing, but the result, including a negative one, goes in
`docs/usb-xhci-info/usbport-miniport-interface.md` "Target ABI record" as a
third column marked best-effort and static-only. The answer gets expensive to
act on once Phase 4 onward has committed to the packet.

## MSVC 6.0 / C89 Language Pitfalls

The compiler is MSVC 6.0 (`cl.exe` 12.x) in C mode, building with the Win2K
DDK. It predates C99. Concrete rules that will otherwise surface as confusing
build breaks or silent bugs:

| Not available (C99+) | Use instead |
|---|---|
| Declarations after statements | Declare every variable at the top of its block |
| `//` comments | `/* */` only (project rule; keeps any C89 tool happy) |
| `stdint.h` (`uint32_t` ...) | `ULONG`/`USHORT`/`UCHAR` from `ntddk.h` |
| Variadic macros (`#define P(...)`) | Fixed-arity macros. This driver's trace API is `XHCI_DBG_TEXT`/`XHCI_DBG_VALUE`/`XHCI_DBG_WORDS`/`XHCI_DBG_CB` (`src/xhci_dbg.h`), compiled under `#ifdef XHCI_DBG_TRACE` (never `#if DBG`), and `DbgPrint` is only ever called as `DbgPrint("%s", line)` |
| Designated initializers (`.field =`) | Positional initializers, or explicit assignments (preferred for the registration packet - order mistakes stay visible) |
| `inline` | `__inline` (MSVC extension, works in C) |
| `bool` | `BOOLEAN` with `TRUE`/`FALSE` |
| `for (int i = ...)` | Declare `i` at block top |
| Compound literals | Named temporaries |
| `snprintf`/CRT anything | No CRT in kernel mode, and no `Rtl*` memory/string calls either: `RtlZeroMemory`/`RtlCopyMemory` expand to `memset`/`memcpy`, CRT symbols no allowed module provides, so the import gate fails the build. Write the loop by hand (`src/xhci_dispatch.c` and `src/xhci_desc.c` show the pattern); `DbgPrint("%s", line)` is the only formatting call, and only at its one sanctioned sink |

Additional hazards:

- 64-bit arithmetic: `__int64`/`ULONGLONG` compile, but 64-bit divide,
  multiply and shift emit calls to compiler helpers (`_alldiv`, `_allmul`,
  ...) that NT's kernel exports and Win98's ntkern may not. The driver never
  needs 64-bit math: keep 64-bit hardware fields as `{ULONG Lo; ULONG Hi;}`
  pairs with `Hi = 0`. For the same reason, avoid `LARGE_INTEGER.QuadPart`
  arithmetic; use `.LowPart`/`.HighPart`.
- No C bitfields for hardware layouts; layout is compiler-defined. Use ULONG
  words plus the shift/mask macros in
  `docs/usb-xhci-info/xhci-data-structures.md`.
- No enums for hardware values in structs or register writes. An enum is a
  signed `int` and invites sign-extension surprises; use `#define` constants.
- DbgPrint format strings: no `%I64x`/`%llx` on the 9x kernel. Print 64-bit
  values as two `%08X` halves. `%s` expects ANSI, `%ws` for `PWSTR`.
- Paged sections: do not use `#pragma alloc_text(PAGE, ...)` or
  `PAGED_CODE()`. Keep the whole driver nonpaged (the default). The 9x
  kernel's paging of driver sections is not worth the risk, the driver is
  small, and most of its code runs at DISPATCH_LEVEL anyway.
- Compile-time size checks (no `static_assert`):
  `typedef char ASSERT_NAME[(condition) ? 1 : -1];`. Use for TRB (16), ERST
  entry (16), and registration-packet sizes.
- `RemoveHeadList` / `RemoveTailList` / `PushEntryList` / `PopEntryList` are
  multi-statement macros, not functions (Oney p.66). They expand to an
  assignment plus a compound statement, so an unbraced `if` guards only the
  assignment and the list mutation always runs:
  ```c
  if (cond) pdLink = RemoveHeadList(&Head);   /* WRONG: removal always executes */
  if (cond) { pdLink = RemoveHeadList(&Head); }   /* correct */
  ```
  A ring driver does a lot of list manipulation. Always brace the body.
- Struct packing: xHCI structures are naturally-aligned ULONGs, so no
  `#pragma pack` is needed or wanted. If a byte-exact overlay is ever required
  (e.g. the 8-byte USB setup packet), `#pragma pack(push, 1)` /
  `#pragma pack(pop)` works in MSVC 6.
- Warnings: build clean at `/W3` (the DDK default). MSVC 6 C4761 ("integral
  size mismatch") in hardware-mask code usually means a missing `UL` suffix
  on a constant. Fix the constant, do not cast it away.

## USB Stack Architecture and the Integration Decision

Windows 98 SE can run two distinct, non-interchangeable USB stack
generations, and the host-controller contract differs between them. The
integration model is the single biggest architecture decision in the project.

| Stack | Components | Host-controller contract |
|---|---|---|
| USB 1.1 native (ships with 98 SE) | `usbd.sys` (USBDI 1.01) + `usbhub.sys` + `uhcd.sys`/`openhci.sys` | Old "HCD registers with `usbd.sys`" miniclass model. Does not use the clean `IOCTL_INTERNAL_USB`/root-hub-PDO contract. A USB-1.1-era bus driver. |
| USB 2.0 / usbport (back-ported to 9x) | `usbport.sys` + `usbehci.sys` miniport + `usbhub20.sys` | The NT5 port-driver model: a private `USBPORT_REGISTRATION_PACKET` interface between `usbport.sys` and the host miniport. |

The USB 2.0 stack that runs on Win98/ME is the Windows 2000-derived
`usbport.sys` + `usbehci.sys` + `usbhub20.sys` binaries adapted to 9x (the
community stack that ships inside NUSB). So the NT5 `usbport` architecture is
proven to run on Win98 SE, via ported Win2000 binaries rather than native 98
support.

Oney's Chapter 12 USB examples describe USB function drivers submitting URBs
to a bus driver. They do not document the private
`USBPORT_REGISTRATION_PACKET` host-controller miniport ABI. Use that chapter
only for USB request semantics; use the local ReactOS-derived ABI docs and the
NUSB binaries for the upward interface of `xhci98.sys`.

### The decision: Option A (miniport), not a monolithic HCD

The clean `IOCTL_INTERNAL_USB` + root-hub-PDO contract belongs to
`usbport.sys`, not to the host-controller driver. On NT5 the host controller
is a miniport (`usbehci.sys`, `usbuhci.sys`) that registers with
`usbport.sys`; `usbport.sys` does the root hub PDO, IOCTL handling, URB
parsing, and enumeration.

Chosen direction (Option A): write `xhci98.sys` as a `usbport.sys` miniport,
reusing NUSB's ported Win2000 `usbport.sys` + `usbhub20.sys`, Windows 98 SE's
own native `usbhub.sys` (the composite parent; NUSB does not ship it, and an
xHCI-only machine does not get it from Setup either, so `xhci98.inf` carries
it) and a per-target `usbd.sys` that the package supplies. This removes most
of the generic USB-stack work (root hub, hub class, URB demux, enumeration)
and leaves the project owning only the xHCI hardware layer. It caps at USB
2.0, since a Win2000-era `usbport` has no SuperSpeed concept. USB 3.0
SuperSpeed is out of scope.

There is no USB 3.0-capable `usbport.sys` for this driver model. Microsoft's
SuperSpeed stack uses the Windows 8-era UCX model (`Ucx01000.sys`,
`Usbxhci.sys`, and `Usbhub3.sys`), which cannot run on Win98 or Win2000.
Installing a newer port driver therefore cannot extend Option A to
SuperSpeed.

Fallback (Option B): a monolithic HCD where `xhci98.sys` re-implements
`usbport.sys`'s role itself. Only needed if the spike below fails. Much
larger; it is effectively "be usbport.sys." SuperSpeed would require this
full Option B stack first, followed by the separate port/link, descriptor,
burst/stream, USB 3.x hub, bandwidth, and validation work summarized in
`docs/usb-xhci-info/xhci-programming.md` under "What SuperSpeed Support Would
Require."

### Go/no-go validation gate (before any xHCI hardware code)

The `USBPORT_REGISTRATION_PACKET` interface is undocumented by Microsoft. The
authoritative open reference is ReactOS (`drivers/usb/usbport` + its
`usbehci`/`usbohci`/`usbuhci` miniports), which reimplements the whole NT5
stack in readable C. Before building the xHCI layer, prove the model:

1. Install the ported Win2000 USB 2.0 stack on the 98 SE VM (it ships in
   NUSB 3.3; see `docs/contributing/build-and-test.md`).
2. Get a stub `xhci98.sys` that calls `USBPORT_RegisterUSBPortDriver` with a
   near-empty registration packet to load and receive its first lifecycle
   callback from `usbport.sys` against the QEMU `qemu-xhci` device.
3. If `usbport.sys` accepts the miniport and calls in, Option A is real. If
   the ABI cannot be matched, fall back to Option B.

Two practical notes for the spike:

- `USBPORT_RegisterUSBPortDriver` is a private export with no import library
  in the Win2K DDK. Generate `usbport.lib` from the NUSB-installed binary via
  the stub-DLL method in `docs/contributing/build-and-test.md` "Build Files"
  (plain `lib /def:` emits cdecl-style symbols that cannot link an
  NTAPI-prototyped miniport; verified) and link it via `TARGETLIBS` in
  `src/sources`.
- No third-party xHCI-as-usbport-miniport is known to exist. The vendors that
  shipped xHCI drivers for XP (the same "usbport predates xHCI" situation)
  all shipped monolithic stacks with their own hub drivers (Renesas, ASMedia,
  Fresco Logic, VIA, Etron). That does not prove the miniport route is
  closed, but it is evidence the fit is nontrivial (e.g. the SET_ADDRESS
  interception described in `docs/contributing/architecture.md`), and it is
  why this spike runs before any hardware code.

The `usbport.sys` build version matters: the community flags builds newer
than `5.0.2195.5652` as flaky on 9x, so NUSB pins an older one. The
miniport's registration-packet layout must match the exact `usbport.sys` NUSB
installs. Pin a NUSB version as a documented prerequisite and record that
`usbport.sys` version. (Recorded: the NUSB 3.3 package carries exactly
`5.00.2195.5652`, at the threshold rather than above it; full record in
`docs/usb-xhci-info/usbport-miniport-interface.md` section 5.)

Target-specific `usbport.sys` source. On Win98 SE, the usbport stack is
back-ported (the Win2000 binaries NUSB installs). On Windows 2000 it is
native: install SP4 (or the standalone USB 2.0 update, KB319973); no NUSB.
The same miniport binary must serve both, because both builds share the
Win2000 SP4 lineage (validate the registration-packet version against each,
not just one).

Win2000 is also the cleaner environment (authentic ABI, full
WDM, real kernel debugging, Driver Verifier) and it doubles as the
differential that separates miniport bugs (fail on both) from NUSB-adaptation
quirks (fail on Win98 only). The go/no-go gate runs first on Win98+NUSB
because a back-ported `usbport` accepting a third-party miniport is the
genuinely unproven part; native Win2000 doing so is not. Both must pass
before Phase 3 closes.

### Reused vs owned

Reused, unchanged: NUSB's `usbport.sys` and `usbhub20.sys`; Windows 98 SE's
native `usbhub.sys` and a per-target `usbd.sys`, both carried by this package
because neither NUSB nor an xHCI-only Setup places them. These provide the
root hub PDO, `IOCTL_INTERNAL_USB_*` handling, URB parsing, enumeration state
machine, and bandwidth.

Owned (`xhci98.sys` miniport): all xHCI hardware logic (controller init,
rings, ports, slots/endpoints, TRB encoding, ISR/DPC event handling), exposed
through the miniport callbacks.

The sections below on `IOCTL_INTERNAL_USB_*`, URB function codes, and
host-FDO PnP/power describe what `usbport.sys` handles on our behalf under
Option A. They remain the implementation checklist for the Option B fallback,
and are useful background for understanding what `usbport.sys` is doing above
the miniport.

## PnP Dispatch Requirements

Under Option A (miniport), `usbport.sys` owns the host FDO and handles these
PnP IRPs. The miniport instead receives lifecycle callbacks (start/stop/
suspend the controller) from `usbport.sys`. The table below is the
requirement for the Option B monolithic fallback, and documents the lifecycle
`usbport.sys` is driving above the miniport.

The (monolithic) driver must handle these PnP minor codes for the controller FDO:

| Minor code | Required action |
|---|---|
| `IRP_MN_START_DEVICE` | Map BAR, connect IRQ, init hardware, create root hub PDO |
| `IRP_MN_STOP_DEVICE` | Stop hardware, disable IRQ (do not free resources yet) |
| `IRP_MN_REMOVE_DEVICE` | Free all resources, delete FDO |
| `IRP_MN_QUERY_CAPABILITIES` | Fill `DEVICE_CAPABILITIES`; set `SurpriseRemovalOK = FALSE` |
| `IRP_MN_QUERY_STOP_DEVICE` | Return STATUS_SUCCESS (allow stop) |
| `IRP_MN_CANCEL_STOP_DEVICE` | Resume normal operation |
| `IRP_MN_SURPRISE_REMOVAL` | Stop hardware, cancel all pending IRPs |

For the root hub PDO, additional codes are needed:
- `IRP_MN_QUERY_ID` - return `USB\ROOT_HUB` as hardware ID
- `IRP_MN_QUERY_DEVICE_RELATIONS` (TargetDeviceRelation)
- `IRP_MN_QUERY_CAPABILITIES`

Always pass unhandled PnP IRPs down the stack for the FDO. PDOs complete unhandled IRPs with `STATUS_NOT_SUPPORTED` (default) unless the IRP has a default success status (check `IoStack->MinorFunction` against the table in the DDK docs).

Win98/Me never sends `IRP_MN_SURPRISE_REMOVAL` (Oney p.191). Under Option B,
an out-of-sequence `IRP_MN_REMOVE_DEVICE` must therefore execute the surprise
removal stop/abort path before final cleanup. Do not wait for a notification
that this platform does not generate.

## Power Management

Under Option A (miniport), `usbport.sys` owns the host FDO and handles power
IRPs. The miniport sees usbport lifecycle callbacks instead. The handlers
below are the requirement for the Option B monolithic fallback and describe
the power transitions usbport shields the miniport from under Option A.

Win98 WDM power management is minimal. Required handlers:

- `IRP_MN_SET_POWER` (system power): call `PoStartNextPowerIrp`, pass IRP down.
- `IRP_MN_SET_POWER` (device power): for D0 (on) re-init hardware; for D3 (off) stop hardware. Call `PoSetPowerState` after transition.
- `IRP_MN_QUERY_POWER`: return `STATUS_SUCCESS` for both system and device power queries.

Always call `PoStartNextPowerIrp` before completing or passing down a power IRP.
On Win98/Me, complete power set/query IRPs only at PASSIVE_LEVEL (Oney p.253);
if an Option-B path reaches completion at DISPATCH_LEVEL, defer it with the
Win98-compatible work-item mechanism rather than completing in place.

Further Win98 power differences, all of which fail silently on Win98 and only
bite on Win2000, or vice versa (Oney p.253-254, Option B only):

- `DO_POWER_PAGABLE` must be set on every device object in the stack, PDO
  and filters included, not just on the FDO. If any one lacks it, Win98's
  Configuration Manager is told the device supports D0 only and cannot wake
  the system; idle detection registered via `PoRegisterDeviceForIdleDetection`
  is then silently ignored.
- `PoCallDriver` is just `IoCallDriver` on Win98. It does not switch IRQL the
  way the NT version does. All power IRPs on Win98 must be sent at
  PASSIVE_LEVEL, so a power IRP forwarded from a completion routine at
  DISPATCH_LEVEL must be deferred to a work item first. Using `IoCallDriver`
  directly for power IRPs works on Win98 and is wrong on Win2000; always
  spell it `PoCallDriver`.
- `PoRequestPowerIrp` can lie. Requesting the device power state the device
  is already in returns `STATUS_PENDING` and then never delivers the IRP, so
  anything waiting on that completion hangs forever. Short-circuit the
  request when the requested state equals the current state and run the
  completion path directly.
- `PoSetPowerState` is a no-op on Win98 and returns whatever state argument
  was passed in rather than the previous state. Never use its return value;
  call it anyway, because Win2000 needs it.
- `PoRegisterDeviceForIdleDetection` takes the PDO, not the driver's own
  device object (Win98 needs it to reach the devnode). The PDO is also
  accepted on Win2000, so write it that way from the start.

## IOCTL_INTERNAL_USB_* Codes

Under Option A (miniport), `usbport.sys` handles all of these. The miniport
never sees `IRP_MJ_INTERNAL_DEVICE_CONTROL`; it sees usbport's
transfer/root-hub callbacks instead. This table applies to the Option B
monolithic fallback and explains what `usbport.sys` is doing above us.

These are sent by `usbhub.sys` and `usbd.sys` to the HCD FDO and root hub PDO. All arrive as `IRP_MJ_INTERNAL_DEVICE_CONTROL`.

| IOCTL | Target | Purpose |
|---|---|---|
| `IOCTL_INTERNAL_USB_SUBMIT_URB` | FDO | Primary transfer path |
| `IOCTL_INTERNAL_USB_RESET_PORT` | FDO | Reset a downstream port |
| `IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO` | FDO | Return root hub PDO pointer |
| `IOCTL_INTERNAL_USB_GET_HUB_COUNT` | FDO | Return number of root hubs (always 1) |
| `IOCTL_INTERNAL_USB_CYCLE_PORT` | FDO | Cycle (disconnect+reconnect) a port |
| `IOCTL_INTERNAL_USB_GET_CONTROLLER_NAME` | FDO | Return controller name string |

The `Parameters.Others.Argument1` field of the IO stack location carries the URB pointer for `IOCTL_INTERNAL_USB_SUBMIT_URB`.

## URB Function Codes

Under Option A, `usbport.sys` parses URBs and presents the miniport with
already-decoded transfer requests (control/bulk/interrupt/isoch on a pipe
handle). The miniport does not switch on `URB_FUNCTION_*`. This list
documents what `usbport.sys` decodes on our behalf, and is the dispatch
requirement for the Option B fallback.

Minimum set needed for USB 2.0 device operation:

```c
URB_FUNCTION_SELECT_CONFIGURATION           /* set device configuration */
URB_FUNCTION_SELECT_INTERFACE               /* set alternate interface */
URB_FUNCTION_ABORT_PIPE                     /* cancel pending transfers */
URB_FUNCTION_RESET_PIPE                     /* reset endpoint toggle */
URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE     /* GET_DESCRIPTOR control transfer */
URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE       /* SET_DESCRIPTOR control transfer */
URB_FUNCTION_SET_FEATURE_TO_DEVICE          /* SET_FEATURE control transfer */
URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE        /* CLEAR_FEATURE control transfer */
URB_FUNCTION_GET_STATUS_FROM_DEVICE         /* GET_STATUS control transfer */
URB_FUNCTION_VENDOR_DEVICE                  /* vendor control transfer */
URB_FUNCTION_VENDOR_INTERFACE               /* vendor control transfer */
URB_FUNCTION_VENDOR_ENDPOINT                /* vendor control transfer */
URB_FUNCTION_CLASS_DEVICE                   /* class control transfer */
URB_FUNCTION_CLASS_INTERFACE                /* class control transfer */
URB_FUNCTION_CLASS_ENDPOINT                 /* class control transfer */
URB_FUNCTION_CONTROL_TRANSFER               /* generic control transfer */
URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER     /* bulk and interrupt transfers */
URB_FUNCTION_ISOCH_TRANSFER                 /* isochronous transfers */
```

## Target Class Workloads

HID, mass storage, USB Ethernet, and USB Audio are project validation targets, but `xhci98.sys` remains only the host controller driver. Both target OSes still need a separate class or vendor function driver for each attached device, and they do not draw from the same pool: on Win98 the sources are the base OS, the USB supplement, NUSB, or vendor packages; on Win2000 SP4, the base OS or vendor packages. A device with a working driver on one target may have none on the other.

For HCD validation:
- HID devices stress interrupt endpoints and endpoint polling intervals.
- Mass-storage devices stress bulk transfer chains and short-packet handling.
- USB Ethernet adapters primarily stress control requests, bulk IN/OUT endpoints, and sometimes interrupt/status endpoints.
- USB Audio devices stress isochronous endpoint scheduling, frame IDs, underrun handling, and timely completion.
- If a device enumerates but no class/vendor driver binds, confirm
  class-driver availability on that target before treating it as an xHCI
  failure. If no compatible driver or INF match exists, record a
  function-driver coverage gap and do not feed that result into the
  Win98-vs-Win2000 miniport axis in `docs/contributing/failure-diagnosis.md`.
  If a compatible driver is present but fails to load or start, keep the
  result as a valid differential: its class-specific URBs may be exposing a
  target-specific miniport or usbport-contract bug. Preserve the Device
  Manager status and transfer log before assigning the cause.

## Resource and PCI Access on Win98

Under Option A (miniport), `usbport.sys` receives the PnP resource lists and
passes translated BAR/interrupt/common-buffer resources to the miniport's
start-controller callback. Use those callback resources. Do not process
`IRP_MN_START_DEVICE` in `xhci98.sys`, and do not rediscover BAR addresses
from PCI configuration space. The raw PnP examples below are for the Option B
monolithic fallback.

For `IRP_MN_START_DEVICE` under Option B, use the translated resource list supplied by PnP to find the MMIO BAR and interrupt vector. Do not rediscover BAR addresses from PCI configuration space during normal start; the bus driver has already arbitrated and translated those resources.

Under Option A, access PCI configuration space for quirk selection through usbport's miniport service (`UsbPortReadWriteConfigSpace`, exact name/signature verified in the Phase 3 spike). Under Option B only, access PCI configuration space via HAL for secondary information such as vendor/device IDs, revision IDs, or quirk selection:

```c
/* Read PCI config - Option B only */
ULONG bytesRead = HalGetBusData(
    PCIConfiguration,
    busNumber,
    slotNumber,   /* PCI_SLOT_NUMBER union */
    &configData,
    sizeof(configData)
);

/* Write PCI config - Option B only */
HalSetBusData(PCIConfiguration, busNumber, slotNumber, &configData, sizeof(configData));
```

The MMIO BAR from the translated resource list is mapped with `MmMapIoSpace`:

```c
physAddr.LowPart = barPhysicalAddress & ~0xFUL;
physAddr.HighPart = 0;
mmioBase = MmMapIoSpace(physAddr, barSize, MmNonCached);
```

Use `READ_REGISTER_ULONG` / `WRITE_REGISTER_ULONG` (not direct pointer
dereference) for MMIO access; these emit the necessary memory barriers on
x86.

## Interrupt Handling

Under Option A, `usbport.sys` owns the interrupt object and calls the miniport
ISR/DPC callbacks. The miniport does not call `IoConnectInterrupt`. The
connection example below is for the Option B monolithic fallback.

```c
/* Option B only: connect interrupt in IRP_MN_START_DEVICE after resources translate. */
IoConnectInterrupt(
    &deviceExtension->InterruptObject,
    XhciInterruptService,       /* ISR */
    deviceExtension,
    NULL,                       /* SpinLock (IoConnectInterrupt allocates one) */
    interruptVector,
    interruptIrql,
    interruptIrql,
    LevelSensitive,             /* PCI INTx interrupts are level-sensitive */
    TRUE,                       /* SharedInterrupt */
    processorAffinity,
    FALSE                       /* FloatingSave */
);
```

ISR must return `TRUE` only if the interrupt was from this device (check
USBSTS.EINT). Clear USBSTS.EINT first, then clear IMAN.IP while preserving
IMAN.IE.

Sharing the INTx line with a VxD on Win98. On the real target the xHCI INTx
line is shared, and on Win98 the other occupant is frequently a VxD-driven
device, not a WDM one. Every WDM ISR runs at a higher IRQL than Win98's
non-WDM ("interrupt time") handlers, but when a WDM device shares a line with
a VxD device, the VxD's handler is pulled up to the WDM driver's DIRQL (Oney
p.110). Two consequences for this driver: time spent in the xHCI ISR directly
delays an unrelated VxD's interrupt handling, and a slow VxD sharing IRQ 11
runs at our DIRQL and delays event-ring service.

This reinforces the one-shot ISR rule above (acknowledge and return, drain in
the DPC), and it means "works on QEMU with a private IRQ" is not evidence
about a shared-line laptop. The Phase 0 qualifier records each controller's
Interrupt Line (`xhciqual/results/`; both qualified machines landed on IRQ
11), which tells you which line to look at, not who else is on it.
Establishing that needs a Win98-side view. Treat a busy shared line as a
latency risk to investigate under Phase 13, not a disqualification.

Oney's general INTx lesson is valid: a level-triggered source left asserted
can livelock the machine (his example drains a device-specific status register
in a loop on p.210). Do not transplant that example's loop to xHCI. For xHCI,
IMAN.IP is the INTx line source; EHB stays set while the deferred handler owns
the Event Ring, suppressing another interrupt until the DPC advances ERDP and
clears EHB. This suppression is architectural (spec 4.17.2/4.17.5, verified
against the spec PDF in `docs/usb-xhci-info/xhci-data-structures.md` ISR/DPC
rules), and it is what makes the one-shot ISR safe. The ISR acknowledges EINT
then IP once and returns `TRUE`; `usbport.sys` queues the DPC. The DPC, not
the ISR, drains Event TRBs. See xHCI spec 4.17.3 for INTx pin behavior and
`docs/contributing/implementation-invariants.md` "Event Ring Draining".

## DMA Memory Allocation

Under Option A (miniport), `usbport.sys` supplies the DMA-able common buffer.
The controller-level block is requested via the registration packet's
`MiniPortResourcesSize` and arrives in the `StartController` resources;
per-endpoint transfer-ring memory arrives via
`QueryEndpointRequirements`/`OpenEndpoint` (see
`docs/usb-xhci-info/usbport-miniport-interface.md`). The miniport does not
call HAL allocation itself, but it may still import HAL primitives such as
`KeStallExecutionProcessor`; the known-good NUSB `usbehci.sys` does. The
`HalAllocateCommonBuffer` path below is for the Option B monolithic fallback.

Under Option B, use `HalAllocateCommonBuffer` for controller-owned data structures that must be DMA-accessible for their entire lifetime:

```c
PHYSICAL_ADDRESS maxAddr;
maxAddr.LowPart = 0xFFFFFFFFUL;
maxAddr.HighPart = 0;

PVOID va = HalAllocateCommonBuffer(
    AdapterObject,               /* PADAPTER_OBJECT from HalGetAdapter */
    size,
    &physicalAddress,            /* receives physical address */
    FALSE                        /* CacheEnabled = FALSE */
);
```

The `CacheEnabled = FALSE` in this Option B sample is not what the shipping
driver does, and is not a recommendation. Phase 3 measured both `usbport.sys`
builds calling `AllocateCommonBuffer` with `CacheEnabled = TRUE`, which is
correct on cache-coherent x86 PCI and is not the miniport's choice under
Option A at all. Ordering therefore rests on `volatile` accesses, publishing
each TRB's Cycle Bit last, and the `WRITE_REGISTER_*` accessors, never on an
uncached mapping. See `docs/contributing/design/04-controller-common-buffer.md`
section 6. The sample is left as written because it is the literal legacy-API
shape; if Option B is ever invoked, revisit the flag rather than copying it.

Note the API pairing: legacy `HalAllocateCommonBuffer` takes the
`PADAPTER_OBJECT` returned by `HalGetAdapter` (the WDM 1.0-era path).
Win2000's `IoGetDmaAdapter` returns a different type (`PDMA_ADAPTER`) whose
allocator is `DmaOperations->AllocateCommonBuffer`. Do not mix the two.
Verify which of the pairings the Win98/NUSB target actually exports before
relying on either (Option B only).

The returned `physicalAddress.LowPart` is the address to program into hardware registers. `physicalAddress.HighPart` should always be 0 on 32-bit Windows.

This applies to:
- Command rings, event rings, ERST, DCBAA, scratchpad arrays, and scratchpad pages.
- Device contexts and input contexts.
- Endpoint transfer rings.

URB transfer buffers need their own policy. Never program hardware with a
virtual address from a URB. Under Option A, `usbport.sys` owns the URB buffer
mapping and hands the miniport scatter/gather physical addresses at transfer
submission, confirmed statically in both shipping builds (Phase 3 task 10;
they come from usbport's own NT DMA adapter and are page-granular,
`docs/usb-xhci-info/usbport-miniport-abi.md` section 5). The miniport
programs those into TRBs directly, and no bounce buffering is needed. The
bounce-buffer policy below applies where the driver owns the mapping itself
(the Option B fallback):
- OUT/control-write transfers: copy from the URB buffer into the bounce buffer before ringing the endpoint doorbell.
- IN/control-read transfers: program the bounce buffer into the TRBs, then copy back to the URB buffer after the Transfer Event.
- Split large transfers into multiple bounce-buffer-sized TDs or TRBs.

This is slower than direct DMA mapping, but it avoids relying on Win98 map-register or scatter/gather behavior while the driver is still proving basic correctness. Revisit direct DMA only after the common-buffer path is stable and a Win98-tested mapping path is documented.

## Known Win98-Specific Pitfalls

- DMA adapter APIs: on Win98 the DMA adapter support is limited compared to
  Win2K. If the Option B fallback needs an adapter, use the legacy
  `HalGetAdapter` -> `HalAllocateCommonBuffer`/`HalFreeCommonBuffer` pairing
  and avoid map registers or scatter-gather lists (`IoGetDmaAdapter`'s
  `DmaOperations` path is the Win2K way; verify it exists on the 9x target
  before using it). Under Option A this is moot: usbport supplies the common
  buffer.
- Stack size: Win98 kernel stacks are smaller than Win2K. Avoid large stack
  allocations in driver routines; heap-allocate large structures.
- `KeAcquireSpinLockAtDpcLevel`: available in Win98 WDM, and not used here.
  This driver takes its controller lock through one `KeAcquireSpinLock`
  wrapper from every context (`XhciControllerLockAcquire`, `src/xhci_cmd.c`)
  so that it carries one import pair; a DPC-level variant would add a second
  for no measured gain.
- IRP cancellation: implement a cancel routine for all pended IRPs. Win98 can
  cancel IRPs on device removal.
- `IoCompleteRequest`: must not be called at or above DISPATCH_LEVEL for IRPs
  with completion routines that access paged memory. Completing URB IRPs from
  the DPC at DISPATCH_LEVEL is fine since USBD completion routines are
  non-paged.
