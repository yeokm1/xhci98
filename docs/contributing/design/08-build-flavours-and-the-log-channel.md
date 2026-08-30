# 08 - Three build flavours, and what a Windows 98 user can send back

Phase 13, batch `13-L`, task 13-L.0. Written out of the question Phase 14
raised when it asked what a stranger attaches to an issue report.

Sections 1-12 are static evidence only. Everything in them was read on the
development host with `link -dump` over `tools/win98se-extracted/`,
`tools/nusb-extracted/` and `tools/DebugView/`, plus this repository's own
build files and source. No boot was taken for any of it. The batch's original
bench clause, a DebugView boot on the E460 to discriminate the bugcheck's
mechanism, was dropped rather than deferred: it decided which sink the evidence
leaves by, and the repair this document describes is correct whichever way it
would have read. Section 11 records what that leaves unmeasured. The one boot
the batch did take later, task 13-L.3's E460 boot under Windows 98 SE, checks
that the two binaries `0.0.0.6` publishes install and load, which is a
different question. Nothing in sections 1-12 rests on it.

The record exists because the question "why can other Windows 98 drivers log
when this one cannot?" has two answers stacked on top of each other, and only
the first had ever been looked for. The first is structural and permanent. The
second is this project's own defect, visible in the tree since batch 13-E, and
fixable.

Section 13 is a later addendum (task 13-L.2): the log channel that ships in
every flavour, and the registry values that switch it. Sections 1-12 were
written before it; read the two together.

## 1. The question, and why it is not task 12.2 again

Task 12.2 asked whether this project could read counters off a Windows 98
machine at a bench it controls, and closed with "there is none".

The question here is the lower one that publishing `1.0.0.0` creates: when a
stranger's machine misbehaves, is there anything at all they can attach to an
issue? That evidence has weaker requirements than the bench channel had. It
does not have to be live, does not have to survive interrupt load, and does not
have to exist while the failure happens. It only has to survive the failure
and be retrievable afterwards.

## 2. First answer: this driver does not own its driver object

### 2.1 Windows 98 has exactly two logging families

Every driver that successfully logs on Windows 98 does it one of two ways:

1. Ring 3 does the writing. The driver creates a device object, publishes a
   `\DosDevices\` symbolic link, answers `IRP_MJ_CREATE` / `IRP_MJ_CLOSE` /
   `IRP_MJ_DEVICE_CONTROL`, and a user-mode program opens it and drains it.
2. Ring 0 writes a file itself, through IFSMgr.

Family 2 is closed, and that is measured: task 11-V.7 found all three path
roots refuse a read, the shipped root refuses a write, and asking either of the
other two for write access hangs the boot rather than answering. (Those
readings were taken on the guests, not on metal; section 13.0.1 says what that
limits.) The file sink built on that finding has since been retired;
`docs/using/release-notes.md`, "The log, and how to send one", describes the
`XHCISNAP` route that replaced it.

Family 1 is what DebugView is. `Dbgview.exe` carries the wide strings
`\Device\Dbgv` and `\DosDevices\Dbgv`, alongside `DbgSetDebugPrintCallback` and
`DbgSetDebugFilterState` for its NT sink; for the 9x side it carries an embedded
VxD identifying itself as `DBGDD`. DebugView is not an OS facility. It is an
ordinary driver-plus-client pair, and it can log because it owns a driver object.

### 2.2 Option A takes the driver object away

`USBPORT_RegisterUSBPortDriver` takes it over. This has been in
`docs/usb-xhci-info/usbport-miniport-abi.md` since the Phase 3 binary
confirmation; what had not been done is reading the offsets as a list of what
is lost. NUSB `USBPORT.SYS`, VA `0002772D` onward, `eax` holding the
`DRIVER_OBJECT`, `MajorFunction[]` at offset `0x38`:

| Write | Index | Slot |
|---|---|---|
| `[eax+0x38]` | 0 | `IRP_MJ_CREATE` |
| `[eax+0x40]` | 2 | `IRP_MJ_CLOSE` |
| `[eax+0x70]` | 14 | `IRP_MJ_DEVICE_CONTROL` |
| `[eax+0x74]` | 15 | `IRP_MJ_INTERNAL_DEVICE_CONTROL` |
| `[eax+0x90]` | 22 | `IRP_MJ_POWER` |
| `[eax+0x94]` | 23 | `IRP_MJ_SYSTEM_CONTROL` |
| `[eax+0xA4]` | 27 | `IRP_MJ_PNP` |

plus `DriverExtension->AddDevice` and `DriverUnload`. `IRP_MJ_CREATE`,
`IRP_MJ_CLOSE` and `IRP_MJ_DEVICE_CONTROL` are the door in family 1, and usbport
has all three. The takeover also lands before the `Version < 100` rejection in
every shipping build, so there is no version at which a miniport keeps them.

Other Windows 98 drivers can log because they own a driver object. This one
runs inside somebody else's.

Two consequences worth stating:

- This is not a Windows 98 fact. It is equally true on Windows 2000, and
  invisible there only because family 2 works, so the driver never needed a door.
- It is permanent. It is not a bug, it is the price of Option A, and no
  amount of cleverness inside the miniport buys the door back.

### 2.3 What that leaves

Since the driver cannot publish a channel, the only levers left are which
binary the user runs and what that binary records and emits. That is the
flavour axis, and it is where the rest of this document lives. (Section 13
finds one more route, through usbport's own door. It does not change the
finding that the driver has no door of its own.)

## 3. Second answer: the diagnostic binary has never loaded on the machine that needs diagnosing

This is the half that had been sitting in plain sight.

The `0.0.0.4` debug binary does not load on the ThinkPad E460 under
Windows 98 SE. The controller devnode takes a yellow bang and Code 2: nothing
loaded, no crash. The sole import delta against the release flavour is
`HAL.dll!WRITE_PORT_UCHAR`, verified with DUMPBIN on both published binaries,
and a rebuild with `XHCI_DBG_NO_E9` loads clean and drives real devices on that
machine (`docs/contributing/runs/run-13e.md`, session record finding A, and
defect 2b's decision tree at P6).

What that does and does not establish is less than it looks. The control was
taken the only way it could be by then: Device Manager was first read after the
swap, so "Code 2 caused by this binary" and "Code 2 already there" were
indistinguishable, and restoring the previously working binary cleared the
bang. That implicates the debug build and rules out a pre-existing fault.

It does not implicate the import. The `XHCI_DBG_NO_E9` rebuild is often described
as dropping that pair and nothing else, and that is true of the trace channel
and false of the binary: the escape hatch was an `XHCI_EXTRA_DEFINES` value, any
nonempty value makes `src/sources` define `XHCI_DIAGNOSTIC_BUILD`, and so the
binary that loaded also carried the do-not-deploy marker, an uncontrolled
variable in run-13e.md's own words (P6).

The matched control that removes it,
`xhci98-diagcontrol.sys`, is built and has never been booted. Why that build
failed is not established, and section 11 keeps it in the unknown column.

That import exists for one reason: the QEMU port-`0xE9` mirror
(`src/xhci_dbg.h`), a debugging convenience for the emulator that real hardware
was assumed to ignore.

So the position this project had been in was:

| Flavour | Loads on Win98 metal | Log ability |
|---|---|---|
| release | yes | least: one bulk dump from the PASSIVE flush |
| debug | no | most |

The build with the diagnostics was the build that would not start on the target
that needs them. That is not a Windows 98 deficiency and it is not usbport's
doing. It is a flavour-axis error in this repository, and it is the reason the
project spent months concluding that Windows 98 cannot be instrumented when
what was true is that its instrumented binary had never run there.

### 3.1 The build that works was unpackageable

`XHCI_DBG_NO_E9` was reachable only through `XHCI_EXTRA_DEFINES`. Any nonempty
value makes `src/sources` define `XHCI_DIAGNOSTIC_BUILD`, which embeds
`XHCI98_PROBE_BUILD_DO_NOT_DEPLOY`, which `make-package.ps1` refuses to package
(`scripts/build-driver.cmd`, `:checkmarker`).

So the one binary known to load and drive devices on Windows 98 metal was, by
construction, one this project could not ship. The escape hatch that produced
it was also the gate that forbade it. That is the whole defect in one sentence,
and promoting it to a first-class flavour is the whole fix.

## 4. The axis that was conflated

The two flavours were the DDK's `free`/`checked` pair (`objfre`/`objchk`, mapped
in `build-driver.cmd`'s `:buildflavor`), and everything diagnostic hung off the
DDK's own `DBG` macro. That single switch decided three independent things at
once:

| # | Concern | Cost where it is wrong |
|---|---|---|
| 1 | `DBG`: frame pointers (`/Oy-`; both compile `/Oxs`), `VS_FF_DEBUG`, the snapshot's `DEBUG` bit. No asserts: every `XHCI_C_ASSERT` here is compile-time. (The ESP probe gate reports through `XHCI_DBG_VALUE` and follows `XHCI_DBG_TRACE`, so it belongs to concern 3, not here.) | none that matters |
| 2 | What is recorded: the ring, the producers, the counters | memory in the miniport extension |
| 3 | What is emitted live, and through which sink: per-line `DbgPrint` from DPC/ISR; the `0xE9` port write | bugchecks Win98 on metal (per-line); the build carrying `0xE9` is the one that failed the load on the E460, by a mechanism still open (defect 2b) |

Concerns 2 and 3 are not the same thing and must stop sharing a switch.
Concern 3's two halves are not the same thing either: one is a runtime hazard
on a live capture hook, the other is a load-time hazard that has nothing to do
with logging at all.

## 5. The plan: three flavours

| Flavour | DDK | Ships | Port `0xE9` | Live per-line emission | Recording |
|---|---|---|---|---|---|
| release | free | yes, default | no | no | the ring, bulk dump at the PASSIVE flush |
| debug | checked | yes, as the diagnostic download | no | no (see section 6) | highest: richest ring, every producer tier on |
| qemu | checked | never | yes | yes | highest, plus the live trace |

- release is unchanged: what an ordinary user installs, minimal imports,
  minimal behaviour.
- debug is the binary a maintainer asks a user with a problem to install, and
  its defining requirement is that it must load and run on real Windows 98 and
  Windows 2000 hardware. It carries no `HAL.dll!WRITE_PORT_UCHAR`. It has the
  highest log ability of the three in the sense that matters, what it records.
- qemu is the old debug flavour: the port-`0xE9` mirror and the live per-line
  trace, for the emulator and the bench. It is a first-class, gated, buildable
  flavour rather than an `XHCI_EXTRA_DEFINES` probe, but it is never published.

As built, the table is accurate in every column but one.

The "Live per-line emission" column is implemented as written. `src/xhci_dbg.c`
compiles away without `XHCI_DBG_LIVE`, which only `qemu` defines, so `debug`
carries neither the port write nor the per-line `DbgPrint`, and the two hazards
this section separates are separated. There are two qemu defines, `XHCI_DBG_E9`
(load-time) and `XHCI_DBG_LIVE` (runtime), and both matter: gating only the
`0xE9` half would leave the per-line trace in `debug`, the profile measured to
bugcheck Windows 98 metal, in the hands of the very users told to install it.
`src/xhci_dbg.h`'s `XHCI_DBG_TRACE` is the gate, and `src/xhci_probe.c`'s two
direct call sites go through it too.

The "Recording" column is an intention and is not implemented. The producer set
(the `XhciLogNote` sites) is identical in all three flavours, as `src/xhci_log.h`
says, so `debug` records exactly what `release` records. What `debug` differs
by today is the DDK's own `DBG`: `/Oy-` against `/Oy` plus a few flags, not
asserts and not unoptimised code. Both flavours compile `/Oxs` (the build logs
`src/buildchk.log` and `src/buildfre.log` print the compiler line), and every
`XHCI_C_ASSERT` in this driver is compile-time and fires in all three. "Every
producer tier on" describes a producer tiering that does not exist. Section 6's
argument for why it would be safe stands; the tiers themselves are unbuilt work
with no task.

That is a smaller gap than it looks. The claim task 13-L.1 is for is "the
diagnostic binary loads", and that is delivered: `debug`'s import table is
byte-for-byte the set `release` carries, and `release` demonstrably loads on the
E460. What a user gets from installing `debug` rather than `release` is a
frame-pointer-preserving build and a couple of flags a maintainer can read back.
The log a user sends is the same either way; that is section 13's channel.

One column is missing from the table, and section 13 adds it: the PassThru
snapshot read channel, which is in all three flavours. It is how a dump leaves
a machine at all (13.0), so `release` carries it too. The flavour decides how
much there is to read, not whether the door exists.

### 5.1 The polarity inverts

Under the old arrangement the mirror was on by default and `XHCI_DBG_NO_E9`
opted out. That made the dangerous configuration the default, which is how it
reached a published release and a bench trip without anyone noticing it could
not load.

The replacement is a positive `XHCI_DBG_E9`, defined only by the `qemu`
flavour. The safe configuration becomes the default; the emulator convenience
becomes the thing you ask for by name.

## 6. Recording is not emission

This is the rule that makes "debug has the highest log ability" safe to say, and
it is what the old design got wrong.

Filling the ring is cheap and safe at any IRQL. It is a bounded byte ring in the
miniport extension (`src/xhci_log.c`), it takes no lock of its own, it
allocates nothing, it calls no service, and it touches no hardware. A record
appended from an ISR costs a few stores.

Emitting is what is dangerous, and each sink for its own reason:

- Per-line `DbgPrint` from DPC and ISR contexts at real interrupt rates is
  what bugchecks Windows 98 on metal: `0028:C208D79D` (hub), `0028:C207B26D`
  (USB Audio), `0028:C20A3F4D` (Low-Speed mouse), across three device classes,
  E460, three observations.
- The `0xE9` port write is carried by the build that failed the load on that
  chipset (section 3). By which mechanism is open, and section 3 says what the
  reading does and does not establish. The rate is irrelevant to it.

So the debug flavour gets the richest recording and the most conservative
emission: every producer tier on, the ring as full as it can be made, and the
hand-over still exactly one bulk dump from the PASSIVE-level flush that already
measures its own IRQL and refuses above `PASSIVE_LEVEL`
(`xhciLogAtPassive` in `src/xhci_dispatch.c`).

`AGENTS.md`'s rule survives unchanged: no per-line printing outside `#if DBG`,
and no hand-over site at DISPATCH_LEVEL or above. The change is that "record
more" stops being confused with "print more".

## 7. What the user actually does

When the batch was planned, its deliverable to task `14.0` was one sentence:
download the debug build, install it, reproduce the problem, and send the
capture. It was an instruction this project could not give, because that binary
did not start on the machine being asked about. Making it start was the
deliverable.

What shipped is better than that sentence. Section 13's channel is in every
flavour, so the user does not download a different build at all; the one they
already have answers `XHCISNAP`. The instruction `0.0.0.6` gives is
`XHCISNAP -verbosity 2`, restart, reproduce, `XHCISNAP -o C:\MYDUMP`, and
`releases/history.md` and the generated `readme.txt` both say so. The debug
flavour still had to start on metal (task 13-L.3 measured that on the E460),
but that is about a maintainer being able to ask for it, not about how a report
is produced. Do not reintroduce "download the debug build" as the user-facing
instruction.

The remaining unknown is the sink. Does the bulk-dump-only profile survive
DebugView on Windows 98 metal? The release build has had that profile all along
and has never been run under DebugView on that machine. A boot to discriminate
it was batch 13-L's anchor for one day and was dropped: it decided how the
evidence leaves, not whether the binary starts, and it was holding the repair
behind a boot nobody had taken in three bench sessions. The flavour split is
worth doing whichever way the sink question would have read, because a
diagnostic build that cannot load is useless under any sink at all. A repaired
binary must not be allowed to imply a cleared sink.

The capture now leaves by a different route from the one this section was
worrying about: section 13's PassThru snapshot channel, a read route through
usbport's own already-open door. That is what makes "send the capture" an
instruction this project can give a Windows 98 user. It does not clear DebugView
and does not reopen the dropped boot; section 11 still lists that as unmeasured.

## 8. What has to change

The implementation surface of the flavour split, task 13-L.1. Each item is a
place the two-flavour assumption was wired in.

1. `src/sources`: replace the negative `XHCI_DBG_NO_E9` with a positive
   `XHCI_DBG_E9`, defined only for the `qemu` flavour, and stop routing it
   through `XHCI_EXTRA_DEFINES` so it no longer drags in
   `XHCI_DIAGNOSTIC_BUILD` and the do-not-deploy marker (section 3.1).
2. `scripts/build-driver.cmd`: `:buildflavor` maps two names onto
   `free`/`checked`. `debug` and `qemu` are both checked builds, so they
   collide in `src\objchk\i386\`. They need distinct output trees; the DDK's
   `BUILD_ALT_DIR` is the mechanism (`objchk` -> `objchk_<alt>`). It has since
   been verified against this DDK's `build.exe`; `docs/contributing/build-and-test.md`
   records the two facts the wrapper depends on. Also update `both`, the usage
   text, `:badflavor`, `:checkmarker` and `:checkfailstart`.
3. `scripts/import-gate/check-imports.ps1`: `FLAVORS` validated against
   `release|debug|both` and `-Flavor` against `auto|release|debug`. Both need
   `qemu`, and "both" needs a decision about whether it means two flavours or
   three.
4. `scripts/import-gate/xhci98-imports.allow`: `HAL.dll!WRITE_PORT_UCHAR`
   moves from `debug optional` to `qemu required`. That single row change is
   what makes the gate enforce that no published binary can carry the sole
   import delta of the build that broke on the E460, instead of merely
   recording that it did.
5. `make-package.ps1` / `make-release.ps1`: `-Flavor` gains `qemu`, and the
   release cut publishes `release` and `debug` and must refuse `qemu`.
6. A flavour marker in the image. `VS_FF_DEBUG` distinguishes release from
   checked, but `debug` and `qemu` are both checked and would both set it. Add a
   marker string in the style of the existing `XHCI98_*` markers so a binary can
   be identified from the file alone; a user sending a capture must be able to
   say which build produced it.
7. `docs/using/release-notes.md`: the published limitation that the debug
   flavour does not load on Win98 metal becomes a fixed defect, with the same
   honesty the other fixed rows carry about which machines it was observed on.

## 9. Traps

- `objchk_qemu` contains the substring `objchk`. A gate that infers the flavour
  by substring match on the path would gate a qemu binary under a
  `BUILD_ALT_DIR` tree silently as debug, which is the check that is supposed
  to keep `WRITE_PORT_UCHAR` out of a published build. The gate infers from the
  output tree (`src\objchk_qemu\i386\` against `src\objchk\i386\`), and
  `-Flavor` can be given explicitly.
- Defect 2b is still open and this does not close it. The flavour split
  removes the import from every published binary, which makes the symptom go
  away. It does not settle whether the cause was the import failing to
  resolve or the port being decoded. P6's two binaries are built and staged at
  `out\bench-13e-2b\`; taking those boots is still owed, and a repair that
  makes the question unaskable would be a worse outcome than the defect.

  The
  reason the boots matter beyond the defect is the evidence tier: `ntkern-name`
  is "unreliable, not wrong" in the allowlist's own words, and it is the tier
  every future Windows 98 import will be judged on. (The allowlist once gave
  `ZwCreateFile`/`ZwWriteFile` resting on that tier alone as the reason; section
  13.0.1 removes both imports, which removes that consequence and not the
  question.)
- Do not let "highest log ability" become "prints more". Section 6 is the
  whole safety argument, and the bugcheck it routes around is measured on three
  device classes.
- A third flavour is a third thing to keep gated. Every gate, self-test and
  packaging path that enumerates flavours is now a place two can silently
  survive where three are meant.

## 10. Why not the registry

A registry write was investigated during this task as a way to carry evidence
across the reboot a user takes after a failure, and it is not the plan. The
findings are recorded here because they were paid for and because the next
person to have the idea should not re-derive them:

- usbport's 16-service table has exactly one registry entry and it is a read
  (`UsbPortGetMiniportRegistryKeyValue`, packet `0xF0`). There is no `Set`
  counterpart at any offset in any build, so a write would have to be an import
  of this driver's own.
- The right import would be `RtlWriteRegistryValue`, not `ZwSetValueKey`,
  because of the handle rather than evidence strength. `ZwSetValueKey` needs an
  open key, which needs either an NT-registry-path `OBJECT_ATTRIBUTES` (an
  unmeasured mapping on 9x) or `IoOpenDeviceRegistryKey`, which wants a PDO a
  miniport does not have. `RtlWriteRegistryValue` with `RTL_REGISTRY_SERVICES`
  takes a bare relative subkey name and does open/set/close itself. Win98 SE's
  own `usbhub.sys` 4.10.2222 imports it (hint `E5`) and calls it at VA
  `0x102D6` as `RtlWriteRegistryValue(RTL_REGISTRY_SERVICES, L"usb",
  L"EnableSerialNumberGeneration", REG_BINARY, &b, 1)`.
- Writing at the next `StartController` cannot work: usbport `RtlZeroMemory`s
  the miniport extension immediately before every start, so there would be
  nothing left to write, and it would have looked like a working channel
  reporting a clean run.
- It would have cost a new import on both targets, a `SYSTEM.DAT` that never
  shrinks and is backed up into five `RB00x.CAB` generations every boot, and an
  unmeasured per-value size limit.

The flavour split is strictly better on every axis: no new import, no
persistent state on a user's machine, no unmeasured platform limit, and it fixes
a defect that exists independently of the logging question. The registry
remains available as a fallback if the sink question is ever taken up and no
hand-over survives it. It is not refuted and it is not needed, and nothing above
should be re-derived if it is.

This section is about a registry write. Section 13 uses a registry read, which
is a different question. What is rejected above is the driver writing a value
as a way to carry evidence across a reboot; that needs an import this driver
does not have, because usbport's service table has no `Set` at any offset in
any build. What section 13 does is read a value the INF placed, through
`UsbPortGetMiniportRegistryKeyValue` (packet `0xF0`), a service that already
exists and that `xhciLogReadValues` already called. No finding above is
weakened by it: there is still no `Set` service, `RtlWriteRegistryValue` is
still the import a write would need, and the extension is still zeroed before
every `StartController`.

## 11. What is proven, what is inferred, what is unknown

Proven (static, or measured and recorded elsewhere in this repo):

- usbport claims `IRP_MJ_CREATE`, `CLOSE` and `DEVICE_CONTROL` on the miniport's
  driver object, before the version check, in every shipping build.
- usbport's service table has one registry entry and it is a read.
- The `0.0.0.4` debug binary gives Code 2 on the E460; the sole import
  delta is `HAL.dll!WRITE_PORT_UCHAR`; an `XHCI_DBG_NO_E9` rebuild loads and
  drives devices there. The control was the restoration of the previous,
  working binary, which cleared the bang, so the build is implicated and a
  pre-existing fault is excluded. Which import or write is responsible is not
  established and stays in the unknown column below; nor was the rebuild
  otherwise identical, since it carried the do-not-deploy marker too.
- Any nonempty `XHCI_EXTRA_DEFINES` marks the image undeployable, so the
  `XHCI_DBG_NO_E9` binary could not be packaged.
- Per-line `DbgPrint` under DebugView bugchecks Win98 metal on three device
  classes.

Inferred:

- That DebugView's 9x sink is the embedded `DBGDD` VxD doing its own `printf`
  formatting inside the debug path; the `<bad format character>` and
  `<float format not supported>` strings sit inside that VxD's image. If that
  buffer is static, the crash needs concurrent output from two contexts rather
  than a high rate from one. This is a hypothesis and must not become the
  published attribution.

Unknown, and the first two are unowned rather than scheduled:

- Whether the bulk-dump-only profile survives DebugView on Win98 metal. The
  discriminating boot was dropped, not deferred, so this is unmeasured, and
  "dropped" is not evidence in either direction: an ordinary user running the
  release build is neither cleared nor implicated. It survives as a free boot
  with no task in `docs/contributing/runs/run-13e.md`.
- Whether defect 2b's cause is the import or the port. P6's two binaries are
  built and staged at `out\bench-13e-2b\`, boots not taken. The split removes
  the symptom without answering this; section 9 says why it still matters.
- Whether `BUILD_ALT_DIR` works with this DDK's `build.exe` was the one that
  gated the implementation. It has since been verified (section 8 item 2).

## 12. Status

Task 13-L.0 built nothing. Its result is that the question has two answers: one
permanent and structural, which no design can undo, and one that is this
project's own defect and had been visible in the tree since batch 13-E. The
plan that follows from the second is the three-flavour split, and it makes a
published diagnostic binary that starts on Windows 98 metal the deliverable,
which is what "a Windows 98 user has nothing to send back" was really about.

Task 13-L.1 is that implementation. Section 8 is its checklist.

Three more tasks joined the batch at the project owner's direction, and section
13 is the design for the first of them: `13-L.2` (the log channel, the registry
values, and the retirement of the file sink), `13-L.3` (both shipping binaries
installed and read on the E460 under Windows 98 SE; this is not the DebugView
boot section 7 says was dropped, which stays dropped), and `13-L.4` (the
`0.0.0.6` cut). What they add to this record's argument is one sentence: a
repair whose whole content is "the diagnostic build now starts on Windows 98
metal" cannot be published on guest boots, because the `Code 2` in section 3
has never been reproduced on a guest.

## 13. Addendum: the log channel and its values (task 13-L.2)

Added at the project owner's direction after sections 1-12 were written. It is
task 13-L.2's design; the roadmap carries its checklist, and
`docs/contributing/passthru-snapshot-instrument.md` owns what was built. This
record owns the why, that one the what.

### 13.0 The third route, which section 2 did not count

Section 2 says Windows 98 has two logging families, ring 3 writing through a
device object or ring 0 writing a file, and that this driver is locked out of
both: family 1 by Option A taking the driver object, family 2 by task 11-V.7's
measurement. Both findings stand. What section 2 did not say is that there is
a third way in, and that this project had already built it and then deleted it.

usbport's door is already open. `IOCTL_USB_USER_REQUEST` -> `USBUSER_PASS_THRU`
-> the miniport's `PassThru` slot at packet offset `0xE0` reaches this driver
from user mode without this driver owning a device object, a symbolic link or a
single `MajorFunction` slot. usbport owns all of those and answers on our
behalf. That is the whole of the PassThru snapshot instrument. It is specified
in `docs/contributing/passthru-snapshot-instrument.md`, and it is the only
mechanism that has ever carried this driver's own evidence off a Windows 98
machine: it read Finding 3's root cause off the E460.

It did not count as an answer in section 2 because it was built behind
`XHCI_OBS_SNAPSHOT`, reachable only through `XHCI_EXTRA_DEFINES`, the gate
section 3.1 describes: any nonempty value marks the image undeployable. So the
channel that worked was, by construction, one this project could not ship, and
task 13-R.4 removed it ahead of the `0.0.0.5` cut. Section 5's flavour split is
what makes it shippable, so it belongs in this batch and not in one of its own.

It goes into all three flavours, `release` included, at the project owner's
direction. A first draft put it in `debug` only, and that would have kept most
of the defect: the user whose machine misbehaves is running `release`, and
telling them to install a second binary before they can report anything is the
same shape as telling them to install one that does not load. What matters is
that an ordinary user produces evidence from the build they already have. What the
flavour decides is the ladder's ceiling, how much there is to read, not whether
the door exists.

Two consequences of that, both cheap to state and expensive to discover:

- `release`'s import profile must not move. The instrument was written not to
  move it: it copies byte at a time rather than through
  `RtlCopyMemory`/`memcpy`, because those resolve to an intrinsic or an
  `ntoskrnl` import depending on build flags. Verify rather than inherit.
- The instrument's rule 1 (`docs/contributing/passthru-snapshot-instrument.md`
  section 5) stops being a probe-build nicety. Return exactly
  `MP_STATUS_NOT_SUPPORTED` (6) for a GUID that is not ours, never any other
  nonzero value, and do not invent a "disabled" status code for the switched-off
  case either. Why 6 and nothing else (usbport's own root-hub probe and the
  fallback it suppresses) is that rule's to state, with the address it was read
  at. What this record decides is that the rule now binds every published
  binary, and a site that cannot fire in ordinary use needs a regression rather
  than trust. It has one: `test_passthru_snapshot_disabled`.

### 13.0.1 The file sink is retired, and the reason is evidential

`XhciLogFile` is removed, and `ntoskrnl.exe!ZwCreateFile`, `ZwWriteFile` and
`ZwClose` go with it (project owner's decision).

The tempting argument is wrong and should not be reached for. Removing those
imports does not remove a load-time risk the way dropping
`HAL.dll!WRITE_PORT_UCHAR` does. `scripts/import-gate/xhci98-imports.allow`
says so at the `WRITE_PORT_UCHAR` block: `ZwCreateFile` and `ZwWriteFile` rest
on the `ntkern-name` tier alone and do resolve on the E460, since the release
build loads there.

The real reason is that the file sink never wrote a byte on any real machine,
on either target. Task 11-V.7 ran on the 2a and 2b guests
(`docs/contributing/runs/run-11v.md`, stage C), so both halves of its record are
virtual-machine readings:

| Reading | Where | Status |
|---|---|---|
| Windows 2000: a 690-byte `C:\XHCI.LOG` appeared, complete and readable | 2b guest | VM only, and could never be otherwise, since Windows 2000 has never run on real hardware in this project |
| Windows 98: all three path roots refuse a read; asking for write access hangs the boot | 2a guest | VM only. The interlock built on it is correct as caution and is not a metal measurement |

So the driver carried three imports and a path-validation surface for a channel
measured working only in an emulator, force-declined on the one target that has
metal, and whose metal behaviour is unknown in both directions: neither the
hang nor safety is established there. That is not a channel to publish under
`1.0.0.0`, and it is a poor trade against a route that has been read off real
silicon.

Two things must not drift out of the record now that it is gone. The Windows 98
boot-hang stays written as a VM measurement. And the removal does not retire
the `ntkern-name` question; section 9's defect-2b bullet says why.

### 13.0.2 The log file did not go away, it moved to ring 3

`XHCISNAP` writes it, and its name is a command-line argument. `XHCISNAP -o
C:\NAME` produces `NAME.BIN` + `NAME.PSC`; what changed is that this is now the
log file rather than a bench convenience.

Every objection task 11-V.7 measured is answered by moving the writer, not by
cleverness: a user-mode program on Windows 98 writes a file the way any program
does. No `ZwCreateFile` on a boot path, no path-root probe, no interlock, no
import, no unmeasured platform limit. That is section 2's finding read
forwards. Ring 3 doing the writing is family 1, and the reason this driver
could not use it was that it had no door of its own. It does not need one; it
borrows usbport's.

The tool also needs a plain-text companion, and has one. `.BIN` is raw
extension bytes that decode only against an `offsets.txt` regenerated from the
same tree, which the maintainer has and the user does not. So it is the right
artifact to send and the wrong one to be the only output. The companion carries
what can be decoded with no offset table: the header, `ExtensionBytes`,
`BuildFlags`, the flavour marker, the tier, and the PORTSC table the tool
already printed to screen. That retires the instrument document's section 9
trap 2, which existed only because that table scrolls off a DOS box on real
silicon and had to be redirected by hand.

As built, the companion's behaviour is the instrument document's section 8;
this record owns the decision to require it. Two things that document records
which the design here did not anticipate: the companion is gated by the ladder
as a publication line, and task 13-L.6 rebuilt the whole console output for a
25-row screen once a real user's-eye reading of it existed.

Section 2's conclusion is narrowed, not overturned. "This driver cannot publish
a channel of its own" is still true and still permanent. What is new is that it
does not need one, because the port driver it lives inside already published
one and will forward through it.

### 13.1 The switch had the same defect the flavour axis had, one level down

Section 6 says recording is not emission. The switch, as it stood, did not know
that:

```c
/* src/xhci_log.c, before task 13-L.2 */
log->Enabled = (log->FileEnabled || log->DebugViewEnabled) ? 1UL : 0UL;
```

and `Enabled` was the only thing the append sites tested (the flush's own guard
is separate, in `src/xhci_dispatch.c`). So naming a sink was what switched
recording on, and a machine with no reachable sink recorded nothing.

On Windows 98 that was every machine. The file sink was force-declined there
(task 11-V.7, section 2.1) and the DebugView sink is handed to nothing at the
stop, so neither could honestly be selected, and the note ring, which this
project built to hold the evidence a user would send, stayed empty. Once the
file sink is retired (13.0.1) that expression does not even have two terms
left, which settles the question: with one sink and a read channel that is not
a sink at all, gating recording on emission has nothing left to recommend it.

The strongest evidence that this was a defect and not a design is that the
PassThru snapshot instrument had to work around it. Its first operating trap
(instrument document section 9, trap 1) told the operator to set
`XhciLogDebugView` to 1 in the driver's software key, not because it reached
anywhere (DebugView has no viewer on Windows 98) but because setting it made
`Log.Enabled` nonzero, the only thing the append sites tested. That is a
maintainer switching on a sink he knows is dead so that a ring will fill. It is
not an instruction a user can be given, and retiring it is what this section is
for.

### 13.2 The two values, and the ladder

Two registry values, placed by one INF on both install paths, one file for both
shipping flavours:

| Value | Type | Default | What it is |
|---|---|---|---|
| `XhciLogVerbosity` | `DWORD` | `0` | the 0-4 ladder below. 0 is the whole of "off": no channel, no ring |
| `XhciLogDebugView` | `DWORD` | `0` | the DebugView sink at the stop, Windows 2000 in practice |

`XhciLogFile` is gone (13.0.1) and the file it named is written by `XHCISNAP`
instead (13.0.2). `XhciLogVerbosity` is the single switch: 0 means disabled
outright (the PassThru channel answers exactly `MP_STATUS_NOT_SUPPORTED` (6)
and writes nothing, indistinguishable at the slot from a binary built without
one), and 1 and above engages the channel and selects the level.
`XhciLogDebugView` is the emission switch and nothing else.

`XhciLogVerbosity`, `REG_DWORD`. Each level is the one below plus one thing, so
a level is always a superset and never a different report:

| Value | Meaning | Audience |
|---|---|---|
| 0 | disabled: the channel answers 6, the ring is off | the default on every machine |
| 1 | channel engaged, counter block only, ring off | ordinary user |
| 2 | + the note ring. This is the log channel | ordinary user |
| 3 | + the PORTSC table in the companion text; still no addresses | cheap and diagnostic |
| 4 | + kernel-address records in the note ring, which the companion text prints; the bench level | maintainer |

The gating is in what the ring records and what the companion text prints,
not in the transport: `xhciPassThru` serves both regions whole at any level
from 1 up (`.BIN` is the extension, `.PSC` the PORTSC array), the driver
admits an address record into the ring only at 4 (`XhciLogAppendAddress`),
and XHCISNAP's `.TXT` prints the ring from 2 and the PORTSC table from 3.
Levels 3 and 4 correspond to the snapshot channel's two regions
(`XHCI_SNAPSHOT_REGION_PORTSC` and `..._EXTENSION`) in what they add to the
companion, which is one reason the ladder and the channel are one task and
not two. A dump decodes with the
machinery that already exists: an `offsets.txt` regenerated from the same tree,
`counters.py`, `readcounters.ps1`. The instrument's section 7 rule comes with
the format: a dump decoded against the wrong offset table is a wrong reading,
not a failed one, so a dump carries `sizeof(XHCI_EXTENSION)` and a decoder
refuses a mismatch. Out of range is refused, not clamped, and the refusal is
recorded; the range is 0-4.

#### Why one value and not two

The design as first built had a separate consent bit, `XhciLogSnapshot`, that
engaged the channel, with `XhciLogVerbosity` running 0-3 beneath it. The project
owner merged them, and the reasons are recorded so nobody re-litigates it.

`XhciLogSnapshot` was a pure consent bit: the channel served everything or
nothing, so the value carried no width. Consent and depth nest. You cannot want
depth without consent, and consent-without-ring is a rung rather than an axis.
So this is not the axis conflation sections 4 and 13.1 condemn; those were
genuinely independent concerns sharing a switch, and one of them (emission)
still has its own value. The two-value design also had to paper over its own
seam: both defaults being `0` forced the tool to set two values in one command,
and `-probe`'s "declined" message covered two situations. One value dissolves
both, and drops one INF row per install path, one gate-footprint pair, and one
default to explain to a stranger.

The naive merge, 1 = ring on, was considered and rejected, because it deletes
the one configuration that matters twice: channel open, ring off. That is the
level-1 dump whose empty ring is task 13-L.3's proof that the recording default
applied, and it is the minimal-perturbation reading a bench wants, because the
append cost at real interrupt rates on Windows 98 metal is unmeasured (the
stated reason the recording default is off at all). So the ladder shifts rather
than collapsing: every configuration of the two-value design survives under a
new number, and the only one that dies is "recording on, channel off", a
Windows 2000 DebugView-only setup. That loss is accepted because the channel is
local-only, answers only the machine's owner running the tool, and the security
posture below already says the boundary is not a user boundary.

The configuration that dies has an upgrade case as well as a design case. A
machine carrying `XhciLogSnapshot = 0` with `XhciLogVerbosity` nonzero was,
under the old design, saying "record but do not open the door". The new driver
does not read the old value, so on that machine the door opens. That is
accepted, and the reason is population rather than principle: the two-value
design was in the tree for one day and has never been in a release, so the only
machines that can hold that pair are this project's own, and none of them was
installed from an INF that writes it. A version marker and a migration path
would be a mechanism with no population to serve.

The rollback half is real and is fixed rather than argued away. A binary from
before the merge does read `XhciLogSnapshot`, so a stale `1` left in a key
would be a consent bit its owner believes they revoked. `XHCISNAP -disable`
therefore clears the retired value too, and only where it already exists,
because writing it unconditionally would put the retired value back on every
machine that ever runs `-disable`. "Off" has to mean off for whatever binary is
installed next, not only for this one.

The value keeps the name `XhciLogVerbosity`. It had never shipped when the
merge was made (`0.0.0.6` was not cut), so there was no compatibility
constraint, and renaming it would have churned every file a second time for
nothing.

The merge moved `sizeof(XHCI_EXTENSION)` (two fields left `XHCI_LOG`) and the
wire header lost `SwitchStatusSnapshot`, which is schema 3. Rebuild the driver
and `XHCISNAP` together; the tool's schema refusal makes a stale pair a wasted
boot.

#### Why the default is 0

The first reason is this record's own rule. Section 5.1's finding is a polarity
one: the safe configuration becomes the default, the convenience becomes the
thing you ask for by name. A ring filling on every machine that ever installs
this driver is not the safe configuration.

Level 1 is also not "nothing". The counter block is written unconditionally and
is what most of this project's own findings were read out of, so a level-1 dump
is a real report.

The reason to take seriously is that what the append sites cost at real
interrupt rates on Windows 98 metal is unmeasured. Section 6's "cheap and safe
at any IRQL" is an argument from construction (bounded ring, no lock, no
allocation, no service call, no hardware), and a good one, but it is not a
measurement. This record exists because an unmeasured assumption about a debug
facility shipped in a release; making the same move twice in one document would
be remarkable.

The cost of that default is real and it is paid in the tool. A capture of a bug
that has already happened cannot be re-taken at a higher level, so an
intermittent fault needs a second reproduction.

`XHCISNAP` sets the value. It is a ring-3 program, so none of section 10's
objections reach it: no import, no `Set` service, no boot path. Locating the
driver's per-machine software key at run time is part of the tool's work, and
the per-machine key is why the release notes say a ready-made `.reg` cannot
ship.

The sequence is `XHCISNAP -verbosity 2`, restart, reproduce, `XHCISNAP -o
C:\NAME`, and `regedit` appears nowhere in it. (That is not the registry
write section 10 rejected. That was
the driver persisting evidence from kernel mode with an import it does not
have; this is a user-mode program setting a value the driver reads at start,
which it has always done.) One value means one enabling step: a user who has
just hit a bug is not walked through two values and a restart.

#### The tool writes what the command named and nothing else

The tool once had an `-enable` switch, retired at the project owner's prompting
("why can xhcisnap automatically increase the registry value?"). `-enable`
raised a 0 to 2, rewrote an out-of-range value to 2 and never lowered: a level
the user had never named, on the strength of the tool's own hard-coded copy of
the driver's range. That copy already duplicates the driver's wire format for
want of a shared header, and correcting values made the driver's policy a
second thing that can drift. A reported out-of-range value cannot drift; a
silently corrected one can.

So `-verbosity N` is the only knob. The per-key loop reads each key's current
level and prints it beside the write (`XhciLogVerbosity was 7 - OUT OF RANGE
(0-4)`, phrased as what a start reading that value would do, since whether any
start has read it is not knowable from the registry), then writes N. The
parser's own `> XHCISNAP_LEVEL_MAX` refusal is kept because that duplication
drifts in the safe direction: it refuses an input rather than rewriting a key.

Why the per-key report matters on a real machine: the 2a guest carries three
xhci98 driver keys, `Class\USB\0002`, `\0004` and `\0009`, all identified by
`NTMPDriver = xhci98.sys`. `\0009` carries no `XhciLogDebugView` value at all,
so it was installed by an INF older than that row, which is also how it is
known that the key the driver actually reads is `\0002` or `\0004`. A user
editing the wrong one of three by hand gets no error. The tool writing every
key it identifies, and printing what each held, is what makes that visible.

#### Level 0 is the same answer as no channel

At level 0 the driver does not engage the IOCTL at all. The channel is present
in every shipping binary and answers nothing until it is asked to; the code
being there is not the same as the door being open, which is the same polarity
rule as section 5.1's.

The disabled path is not a new return. It is the instrument's section 5 rule 1
(`docs/contributing/passthru-snapshot-instrument.md`): return exactly
`MP_STATUS_NOT_SUPPORTED` (6), the value usbport's root-hub probe requires, so a
switched-off channel answers a caller exactly as a binary built without one
would. Do not invent a "disabled" status code. Rule 1's whole content is that 6
is the only honest nonzero answer at that slot, and usbport's root-hub fallback
retries only on exactly 6.

That is a statement about the answer, not about the image. The two binaries
differ by the whole of `xhciPassThru`; what they share is the reply at that
slot, which is all a caller can see. So the two are separated by reading the
installed file (`fc /b` against a reference, and the flavour marker section 8
item 6 adds) and never by asking the driver.

The sameness of answer has one cost and it lands in the tool. `XHCISNAP -probe`
reports "the ROUTE WORKS, this driver has no snapshot support", which is two
situations wearing one sentence: a driver without the channel, and a shipping
driver with it switched off, the ordinary case on every machine. The tool names
both and says what to do about the second. The bootstrap is not circular: the
tool sets the value from ring 3 without needing the IOCTL, so a machine whose
channel is off can always be switched on and restarted.

#### An absent value is 0, and nothing in this path may fail a start

Three cases must pass through without a changed code path, let alone a fault:
the value missing, the read failing, and `UsbPortGetMiniportRegistryKeyValue`
being NULL in the packet. Each leaves that value at `0` and the driver starts
normally.

`xhciLogReadValues` returns with everything off when the service pointer is
NULL, and `src/xhci_dispatch.c` records that the service collapses "value
absent", "buffer too small" and "key would not open" into one code, so absent
and unreadable are one answer, and that answer is off. The defaulting is
trivially consistent because the default is `0`: absent, unreadable, and an
explicit zero all land in the same place.

They still have to be distinguishable in the counter block, for the reader
rather than the code. A user who believes they set `XhciLogVerbosity` and sends
a level-0 dump needs it to say the driver read nothing, not that it read a
zero. That job is `Log.SwitchStatusVerbosity` and `Log.SwitchStatusDebugView`,
the per-value MPSTATUS: SUCCESS beside a value of 0 is a zero somebody set,
anything else beside 0 is nothing found. (`Log.SwitchRead`, which the design
first proposed for this, is assigned on `xhciLogReadValues`'s first line,
before the registry service is even tested, so it records only that the reader
ran.) This is not a hypothetical case: a machine whose INF never ran, a driver
copied in by hand at a bench, an upgrade over an older INF, and task 13-L.3's
own binary swaps all reach it.

#### What verbosity gates, and what it does not

An earlier draft made verbosity a serving ceiling on the read channel: the tool
asks for a region and the driver serves it only up to the level the registry
allows. That cannot be implemented as written, because levels 1 and 2 name
payloads that are not regions.

The "counter block" is flush-time formatting (`src/xhci_dispatch.c`, "The
counter block, appended at flush time") over counters scattered through the
whole extension, and the note ring is a struct deep inside it. A channel that
refused `REGION_EXTENSION` below level 4 would have nothing at all to serve at
levels 1 and 2, while task 13-L.3's bench clause rightly expects a level-1
dump to come back with the counter block and an empty ring.

Serving those payloads per level would need either a gather
table inside the driver duplicating `offsets.txt` (a second copy of the layout,
in the binary, drifting) or new wire regions and a schema bump the tool's
staleness refusal would propagate to every user. Both are bigger mechanisms
than the thing they would gate. One value carrying both consent and depth does
not change that by a line, and the temptation to read "verbosity" as a serving
ceiling is stronger with one value than it was with two.

So the semantics are these, and each has one owner:

- Recording is gated by verbosity: the ring fills at level 2 and above (the
  whole point of the switch rework), richer producer tiers at 3 and 4 as the
  flavour provides them. `XhciLogAppendAddress` refuses an address record below
  `XHCI_LOG_VERBOSITY_FULL` and admits it at 4.
- The read channel is gated by verbosity being nonzero, and by nothing else.
  Once engaged it serves both regions whole at every level: the `.BIN` stays the
  raw extension image it has always been, the artifact Findings R and S were
  read from. A level-1 dump is therefore the extension with an empty ring,
  which is the reading 13-L.3 expects, and there is no "region refused by
  level" case for the header to report.
- Publication is gated by the ladder, and that is where the tiers bind: the
  push payload (the DebugView hand-over at the stop contains what the level
  says) and the tool's plain-text companion, which prints the counter block
  always, ring text at level 2 and above, and the PORTSC table at level 3 and
  above (`XHCISNAP_LEVEL_LOG` and `XHCISNAP_LEVEL_PORTSC` in
  `xhcisnap/xhcisnap.c`). The companion does not filter the ring, so a level-4
  `.TXT` carries whatever addresses the driver logged.

The tier boundary between 3 and 4 is addresses, and it is a publication line,
not a transport one. It is about what a maintainer may reasonably ask a
stranger to paste into a public issue. Levels 1 to 3 are the paste; level 4 is
a maintainer's artifact, the `.BIN` an attachment a maintainer decodes
themselves. The consent story stays one switch: at `XhciLogVerbosity = 0` the
channel serves nothing at all, and the person who set it nonzero is the
machine's owner running the tool.

#### Security posture

Decided by the project owner: the exposure is accepted, and the reason is that
the enable step is the access control, because the driver owns no other.

- The driver cannot tighten the door. usbport hardcodes `\DosDevices\HCD<n>`
  (17-wchar template, NUSB `00011C6E`, digit patched at `00011CF7`), completes
  `IRP_MJ_CREATE` with no work, and the IOCTL is `FILE_ANY_ACCESS`. The name,
  the open semantics and the device ACL are all usbport's, and Option A leaves
  the miniport no lever on any of them
  (`docs/usb-xhci-info/usbport-miniport-abi.md`, "Reachability from user
  mode"). Changing any of it would mean patching stock usbport, or Option B.
- So the registry value is the whole consent and access story. Setting
  `XhciLogVerbosity` nonzero requires write access to the driver's key under
  `HKLM\...\Services\...`, which on Windows 2000 is Administrators; Windows 98
  has no user boundary at all. At `0` the channel answers exactly
  `MP_STATUS_NOT_SUPPORTED` and writes nothing.
- What is accepted: while enabled, any local user who can open `\\.\HCD<n>`
  can take a dump. The content is this driver's own diagnostic state (counters,
  ring notes, PORTSC, kernel addresses), no user data and no other process's
  memory; and on Windows 2000 address disclosure is worth nothing to an
  attacker (no ASLR of any kind, driver bases enumerable by any user anyway).
  The parser side is bounds-checked: offset-past-end refused, copy capped at
  `min(available, capacity)`, usbport itself capping the block at `0x10000`.
- "Any local user" is measured on this target, not read off a default ACL. On
  the 2b guest (Windows 2000 SP4, SP4's own `usbport.sys` 6681), with the
  channel at level 2, an account in `Users` and nothing else (created with
  `net user tester ... /add` and run through `runas /user:tester`) opened
  `\\.\HCD0`, got `PassThru, our GUID  status 0 (success)`, and took a complete
  dump: 90,272 bytes of extension, the PORTSC array and the plain-text report,
  written to its own `C:\NADUMP.*`. The probe and dump reports
  (`run-13l-2b-nonadmin-probe.txt`, `-dump.txt`) were read and discarded; this
  paragraph is the record. Windows 98 cannot be measured this way
  and does not need to be, having no user boundary.

### 13.3 What changes, and the one thing to measure first

- Verbosity becomes the recording switch. `XhciLogDebugView` is left as an
  emission switch and nothing else. `Enabled` becomes a function of the level,
  so the ring fills on a machine with no sink at all, which is every Windows 98
  machine. That is the property the whole task exists to create, and the one a
  sink-gated tree cannot express.
- The claim this licenses is bigger than the rest of the batch, and it must be
  stated at its strength. Section 2's first answer is untouched: this driver
  does not own its driver object, and no design undoes that. What changes is
  that the route which never needed one is in a binary a user can install, so
  Windows 98 gains a way to get evidence out for the first time.

  That was first measured on the E460 and in the 2a guest with an unshippable
  build; task 13-L.3 then took both shipping flavours to the E460, and a
  shipping binary carried this driver's own log off a Windows 98 machine,
  `release` first, then `debug`, both reading their own flavour, extension and
  schema off the wire.

  The claim's limits are what to keep saying: one machine, one operating system,
  and Windows 2000 is the 2b guest. The readings are the instrument document's
  section 10, which owns "what was executed on which target"; this record owns
  why the claim is worth making carefully.
- Level 4's windowing is already solved for the read channel. usbport refuses
  `ParameterLength > 0x10000` and the extension is larger than that, so the
  tool loops on `Offset` and concatenates; an 87,592-byte extension came back
  in exactly two windows. The windowing was exercised on metal: every window of
  every 13-L.3 dump reported the same `ExtensionBytes`.

  The extension's size moves with every layout change and belongs to the
  generation table in `docs/contributing/runs/run-13e-evidence/README.md` and
  to the header field a dump carries, never to a sentence here.

  What is open is
  only whether level 4 is worth having through DebugView: 90 KB of hex through
  one PASSIVE bulk dump probably is not, and "level 4 is snapshot-only" is an
  acceptable answer to write into the ladder. What is not acceptable is a level
  that truncates silently.

### 13.4 Why the channel and the values are one task

They were nearly split, and the reason not to is that they only mean anything
together. A ladder without the channel is a ring that fills with no way off the
machine, the defect 13.1 exists to end, and a channel without the ladder serves
an image whose ring never fills. Consent to open the channel is meaningless
until the channel exists, and what a dump contains is meaningless until the
ladder says what was recorded into it and what the companion publishes. The
merge at 13.2 took that one step further: two values that are meaningless apart
are a candidate for being one value, and consent and depth nest, so they became
one. The channel and the ladder are one task because they are one switch.

It is also the half a user touches. Section 7's original sentence was "download
the debug build, install it, reproduce, send the capture", and this is the task
that makes the last word of it true on Windows 98, which task 13-L.0 had no
route to. That is the one sentence in this record that section 13 changes
rather than extends.
