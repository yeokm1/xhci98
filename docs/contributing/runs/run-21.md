# Phase 21 Record - 64-bit targets: Windows XP x64 and Server 2003 x64, then Vista x64 and Windows 7 x64

The detail behind `docs/contributing/roadmap-phases-17-on.md`, "Phase 21 -
64-bit Targets". The roadmap entry carries the goal, the status, the task list and
the checkpoint; this file carries what each task did and what each reading
said, moved here out of the roadmap on 2026-09-16 when the phase closed. Where
the two disagree about a clause, the roadmap wins.

**Most of this was written while the phase was open**, and it is kept as it
was written rather than rewritten in the past tense. A sentence that says a
box "stays open", or that a task "is still owed", describes the day it was
written; the roadmap entry says how each closed. Task 21.8 closed on Phase
22's task 22.5, whose guests ran the Version 300 path this phase's own
guests found was needed, and task 22.5 in the roadmap is that record.

**On `out\...` and `vm\...` paths in this file.** They say where a reading was
taken and what the file was called, on the host that ran it; they are not
files a clone has.

## The phase narrative, as the roadmap carried it


Goal: whether this driver can be an Option A miniport on a 64-bit Windows
settled from the shipping binaries first and a guest second, and - if it can
- one NT 5.2 amd64 binary observed on a Windows XP Professional x64 guest,
with its standing stated in every document that names the targets. Since
2026-09-09 the goal also asks the same of **Vista x64 and Windows 7 x64**
(task 21.8): 21.7 read their interface and found nothing against them, and
what was left looked like whether a binary the static pass says should work can
be made to install and load on systems that enforce kernel-mode code signing
and stage drivers through a driver store. **21.8's guests answered a different
question on 2026-09-10**: a Version 200 miniport cannot run there at all
(measurements M9 to M11), so the goal's second half is now carried by task
22.5's Version 300 path rather than by this phase, and signing was separately
decided against (design record 11 section 12, decision 9).

Status: open, 2026-09-09 - **checkpoint passed, the record written, and task
21.8 the only thing left.** It
opened on 2026-09-08 when the owner asked whether a WDK 7.1 build could give
this driver 64-bit guests. Task 21.1 is complete: all six measurements are
read and all six pass, so nothing in the ABI argues against Windows XP x64 and
Server 2003 x64. That was the limit of what a static pass could show, and the
paragraphs below are the record of what the phase then did about it.
Task 21.5's guest was begun on 2026-09-08: `scripts\setup-qemu-winxp64.ps1`,
its two launchers and the 16 GB `vm\winxp64.img` exist and the launcher gate
covers them. It cost one finding already, before any driver: **this guest
needs `-accel tcg`, and WHPX - which every other guest here uses - wedges XP
x64 Setup**, the reverse of the Windows 2000 reading (`lessons.md`, "The
accelerator is the discriminating variable in both directions"). A **compile
scout** was then taken on 2026-09-08 - WDK 7.1's x64 `cl` pointed at `src/`
through `build /L`, compile-to-object only, so none of 21.2's plumbing and no
amd64 import library were needed. It answers the question it was taken for:
**this source produces exactly one diagnostic on amd64**, a `ULONG_PTR` ->
`ULONG` truncation at `xhci_dispatch.c:1001`, so 21.2 is a plumbing job with a
known end. It also found one thing nobody was looking for -
**`USBPORT_ENDPOINT_PROPERTIES` changes size on amd64 and none of M1-M6
measured it** - which adds a reading to 21.4. Design record 11 section 8, "The
compile scout", has it. Task 21.7 is complete as of 2026-09-09: the same six
measurements taken on Vista x64 and Windows 7 x64, and **all six pass on
both**, so one `WNET` amd64 binary can serve them too and nothing here
narrows to Windows XP x64 and Server 2003 x64. It cost only the extraction,
and it found one thing worth carrying: **Windows 7's usbport has a second
`IoGetDmaAdapter` call that asks for a 64-bit adapter**, reachable only by a
miniport declaring `Version >= 310`, which is the one path by which a future
change here could move physical addresses above 4 GB. That reading was taken
when nothing else in the phase had started and no binary existed.

**That order was taken on 2026-09-09 and it held.** The
`USBPORT_ENDPOINT_PROPERTIES` reading went first and became M7; the rest of
21.4's declaration work followed from it; then 21.2's plumbing, which is
indeed where the weight was. **An amd64 `xhci98.sys` now exists in all three
flavours** and the x86 binaries were held byte-identical across the whole
change - `.text`, `.data`, `INIT`, `.rsrc` and `.reloc` unchanged against the
published `1.0.2.0` pair, differing only in the enumerated PE metadata bytes a
re-link always moves. The first build then made 21.3 larger and more
interesting than the plan assumed - the amd64 import surface is seven
module/symbol pairs against x86's eleven, and the differences are structural -
and **21.3 was closed the same day**: the import gate, the library generator,
a second INF, and both packagers.

21.4's last two boxes closed the same day: design record 04's arithmetic was
re-run against the amd64 `sizeof`s and does not move - nothing in the
common-buffer layout is a `sizeof` - and the host suite now compiles and runs
`test_packet` and `test_membuf` for amd64 as well as x86, so the `_WIN64` half
of the ABI declaration is checked on the build host instead of only inside a
driver build.

**And then task 21.5 ran it, the same evening, and every checkpoint clause
passes.** An amd64 `xhci98.sys` has now installed on an xHCI-only Windows XP
Professional x64 SP2 guest, registered with that system's own `usbport.sys`,
started its controller, passed its No Op self-test, answered the root-hub
family, bound a HID mouse, a mass-storage device and a composite audio device,
and survived the Device Manager disable/enable/remove/rescan sequence - with
321 transfers completed and every refusal and error counter zero.

**It cost one defect, and it was the one thing in the phase still running on
an assumption.** `USBPORT_SCATTER_GATHER_LIST`'s amd64 layout had never been
read off a binary; the compiler's guess was four bytes wrong in the element
array's offset, and the first amd64 run refused every control transfer because
of it. It is now measurement **M8**, taken statically off the producer in the
NT 5.2 amd64 `usbport.sys`, and the fix is `_WIN64`-only - the x86 binary does
not move. Task 21.5 has the account.

**The publisher was then changed, on the owner's instruction and not as a
consequence of the checkpoint.** `make-release.ps1 -Arch` defaults to both
architectures since 2026-09-09, so an ordinary cut publishes four directories.
The step that licensed it was not the checkpoint alone but the **release**
flavour leg above: the clauses had been taken on the `qemu` build, and cutting
a package on the strength of a flavour that is never published would have been
the one soft spot in the phase. One consequence came with it and is fixed:
`-UploadSetOnly` re-assembles an already-published version, every such version
is x86-only, and under a both-arch default it began demanding a `release-x64\`
those cuts never had - so that mode now derives the architectures from the
published tree unless `-Arch` is passed.

So Phase 21 is a target observed and a package that can be cut. What may be
written is that the binary has been observed running: in one virtual machine,
on one guest, never on real hardware, and the x64 half of any cut carries
exactly that standing against the x86 half's four install legs. **The cut
itself is not this phase's.** `1.1.0.0` is published by Phase 22, on the
owner's instruction of 2026-09-09, because this phase closes on task 21.8's
guests and a release should not wait on a leg whose answer changes nothing
about what is being published.

**One task is open, and the phase closes on it.** 21.6, the record, was
written on 2026-09-09: the tier is stated in `AGENTS.md`, `build-and-test.md`,
`win98-wdm.md`, the release notes and `README.md`, and what it cost was
saying in each of them the thing this tier does not share with the other four
- that it is not the same binary. 21.8
was added on 2026-09-09 on the owner's instruction and is a second guest leg:
Vista x64 and Windows 7 x64, whose interface 21.7 already read and found
nothing against. It is not a repeat of 21.5, because the two obstacles there
are not ABI obstacles - both systems enforce kernel-mode code signing, so an
unsigned driver loads only on a boot with enforcement disabled, and
both stage a package through a driver store the `LayoutFile` route was not
written for. Either could end the leg, and a well-characterised "no" is a
complete result.

The version was bumped to `1.1.0.0` in the same session, on the owner's
instruction, as a separate step after that identity was proved - its only
effect on the x86 binaries is eight bytes inside `.rsrc`, measured rather than
assumed. **`src/xhci98.inf` is therefore no longer byte-identical to
`1.0.2.0`'s**, since `DriverVer` moved with it; what decision 2 in design
record 11 section 12 actually buys is unchanged and is the thing that
mattered - no new install path, no widened `[Manufacturer]` line, and no
existing install leg to re-validate.

Why a phase: the same reason Phase 19 was one. A target is not a build. The
static pass has to settle the ABI before any code is written, the guest has
to settle the runtime the static pass cannot, and the gates, the INF and the
packager all have to learn a second architecture before anything can ship.
`design/11-x64-targets.md` is the record; it carries the measurements, the
decision gate they feed, and the five decisions that are the owner's.

Windows XP x64 and Server 2003 x64 are one target, not two: both are NT
5.2.3790, and the WDK ships `lib\wxp\i386` with no `amd64` counterpart, so
NT 5.2 (`WNET`) is the only route to a 64-bit XP driver. Vista x64 and
Windows 7 x64 are a separate question and a second leg, because both enforce
kernel-mode code signing and both stage driver packages differently. Task
21.7 reads their binaries by the same static method, because doing so costs
only the extraction; nothing else in the phase waits on it.

A box was ticked only on the evidence its line named; `21.1`'s boxes were
static readings and no other task's box could be ticked on one.

## 21.1 - the static ABI pass

**21.1 - the static ABI pass.** Read 2026-09-08 from
`tools/winxp64-extracted/`, NT 5.2 amd64 `usbport.sys` and
`usbehci.sys` 5.2.3790.3959. Method `static` throughout; design record
11 section 5 has each reading with its RVA and instruction bytes, and
`usb-xhci-info/usbport-miniport-abi.md` carries the transcription.

- M1 the two private exports - three names, same three ordinals as
  every 32-bit lineage
- M2 the `USBPORT_GetHciMn` value - `0x10000001`, already accepted, so
  no code change follows
- M3 the version gate and copied packet sizes - unchanged gate,
  `0x250` / `0x230`, the exact widening of `0x13C` / `0x12C`
- M4 the `USBPORT_RESOURCES` layout - `sizeof` `0x48`, and `StartPA`
  stays a 4-byte `ULONG`, so nothing after it shifts
- M5 the DMA adapter width - still created 32-bit
  (`Dma32BitAddresses = 1`, `DmaWidth = Width32Bits`), so nothing
  lands above 4 GB whatever the guest's RAM
- M6 the offset map end to end - all 50 slots the amd64 `usbehci`
  fills land on `f(X) = 0x28 + (X - 0x28) * 2`

## 21.2 - the x64 build path

**21.2 - the x64 build path.** Done 2026-09-09. All three flavours
compile and link for amd64 from `tools/WinDDK71` at
`src/obj<flavour>/amd64/`, with **no diagnostic on any source file** - no
`build<flavour>.err`, no `.wrn`, and not one `warning Cnnnn` in the log.
Read the build summary's "21 files compiled - 5 Warnings" against that:
those five are `build.exe`'s own "x64 Native compiling isn't supported.
Using cross compilers." notice, counted once per pass, and say only that
an x64 host is using the x86-hosted cross compiler. Design record 11
section 8, "The first build".

- the `qemu` flavour's `__asm` exclusion under `_WIN64`, without which
  that flavour does not compile at all. Both sites and their two locals
  now sit behind one named guard, `XHCI_CHECK_STACK_DELTA`; the check is
  x86-only by nature, since the calling-convention mismatch it watches
  for is a `__stdcall` decoration problem amd64 does not have
- the first build that links. `build-driver.cmd -amd64` selects WDK 7.1
  and `setenv <root> <flavor> x64 WNET no_oacr`, `make-usbport-lib.cmd
  -amd64` produces `src/usbport_amd64.lib` through the same five
  verification steps (the `_Name@N` decoration check becoming its mirror
  - plain names present, decorated forms absent, machine `8664`), and
  `src/sources` picks the library by `_BUILDARCH`. Both traps the scout
  named held: `BUILD_ALT_DIR` is overridden back to the flavour word
  after `setenv.bat`, which puts the output beside `i386` with no second
  obj root, and `release` was built first precisely because `/WX` is on
  there

## 21.3 - the gates

**21.3 - the gates. Complete 2026-09-09.** None of these may be
skipped for an amd64 binary, and none was. An amd64 binary passes the
import gate in full in all three flavours, `src/xhci98-amd64.inf`
exists and passes an INF gate that has learned a second profile, and
the packager and the publisher both take an architecture. The INF
gate's self-tests went from 350 checks to 450 and the packager's from
205 to 243; `build-driver.cmd` now runs the INF gate over **both**
INFs on every build, whichever architecture it is building.

- an `amd64` dimension in the import gate, with NT 5.2 amd64 baselines
  behind it as `win2k-baselines.expected` has for SP4. Done 2026-09-09,
  and task 21.2's first build made it concrete: the amd64 binary imports
  **seven** pairs where x86 imports eleven, and the difference is not
  cosmetic. `READ`/`WRITE_REGISTER_ULONG`, `InterlockedIncrement`,
  `KeInitializeSpinLock` and `KeGetCurrentIrql` become intrinsics or
  inlines and vanish; `HAL!KfAcquireSpinLock` and `KfReleaseSpinLock` -
  x86-only fastcall HAL exports - are replaced by
  `ntoskrnl!KeAcquireSpinLockRaiseToDpc` and `ntoskrnl!KeReleaseSpinLock`,
  a different module as well as a different name; and
  `ntoskrnl!KeBugCheckEx` appears, **which was investigated rather than
  allowlisted on sight** (the owner's call). It comes from
  `__report_gsfailure`, WDK 7.1's `/GS` stack-cookie handler: one call
  site in the whole of `.text`, reached only from
  `__security_check_cookie`, issuing bugcheck `0xF7`
  (`DRIVER_OVERRAN_STACK_BUFFER`). So it is unreachable except on a real
  stack-buffer overrun and is a safety feature the x86 binary cannot
  have, MSVC 6.0 predating `/GS` entirely - which is why it is accepted
  rather than the flag being turned off to match x86. What was built:
  `scripts\import-gate\xhci98-imports-amd64.allow` (**a sibling file,
  not an arch column** - the owner's decision; only 2 of the 13
  kernel/HAL rows are shared, and the x86 file's 27 denials are
  justified end to end by absence on Windows 98 or by blocking the load
  on Windows 2000, reasoning that does not transfer),
  `winxp64-baselines.expected` and its three authenticated files, an
  `-Amd64Iso` arm in `extract-target-baselines.ps1`, and an `-Arch`
  dimension in `check-imports.ps1`. **The evidence rule for amd64 is
  stronger than the Windows 98 one rather than a relaxation of it**, the
  second decision the owner took: the Windows 98 rule compensates for a
  target whose export tables are built at run time by `ntkern.vxd` and
  so cannot be resolved against, while NT 5.2 amd64 has a real export
  table, so direct resolution against both kernels and the one HAL is
  the rule and no proxy is wanted
- arch-conditional checks in `scripts\make-usbport-lib.cmd` - the
  undecorated-name check replacing the `@N` one, the other four steps
  unchanged. Done 2026-09-09 as part of 21.2, and it is the mirror check
  rather than merely a weaker one: the plain names must be present *and*
  the `@N` forms absent, plus a machine check, because a decorated
  symbol in an "amd64" library would mean an x86 library published under
  the 64-bit name
- the second INF. **The decision was taken 2026-09-09: a separate x64
  package**, so no existing install leg is re-validated and Windows 98's
  engine is never asked about a widened `[Manufacturer]` line. Written
  the same day as `src/xhci98-amd64.inf`: `.NTamd64` throughout - the
  `[Manufacturer]` TargetOSVersion field, the models section, the
  install section, its `.Services`, and the right-click section - and
  the same `LayoutFile` route the 32-bit NT path uses, which task 21.5
  had already read as needed here too.

  **The owed reading was taken first**, because design record 11 said
  it had to be: *whether the x64 media's `layout.inf` carries a
  `usbui.dll` row*. It does. `AMD64\LAYOUT.INF` gives `usbui.dll =
  1,,222222,,,,,2,1,3`, disk 1 being `\amd64` on the base CD, so it
  comes from `AMD64\DRIVER.CAB` at 123,392 B / 5.2.3790.1830 - beside
  `usbd.sys` from the same cabinet, and `usbport.sys` and `usbhub.sys`
  from disk 100's `SP2.CAB`. `usbhub20.sys` has no row, as on 32-bit XP.
  The three sizes that can be cross-checked match
  `tools/winxp64-extracted/` exactly, which is what authenticates it,
  and `7z l vm\winxp64.img -r usbui.dll` on the 21.5 guest returns zero
  files, so the file is as absent there as it is on 32-bit XP. Method
  `static` throughout; `legal-provenance.md` section 4 carries the rows.

  The gate learned the path as a **profile switch, `-Arch`**, not a
  second script - the opposite of the allowlist's sibling file next
  door, and for the opposite reason: there the data forked, here the
  rules are the same rules and only the path list differs. It is not a
  relaxation either. It drops the Windows 98 rules because there is no
  Windows 98 path, and adds two refusals the 32-bit file has no need
  of: **`PATH-NO9X`**, an undecorated install section in the 64-bit
  file, and **`OS-DEFAULT`** extended to refuse an undecorated
  `[DefaultInstall]` there. Both are sections a 32-bit engine falls
  back to, and reaching either puts an amd64 binary on a 32-bit
  machine with a service pointing at it. **`PATH-MFGDEC`** holds
  `[Manufacturer]`'s decoration in both directions, which is what pins
  decision 2 in place: widening the 32-bit file's line now fails the
  build.

  The accepted cost of two files - that they drift - is mechanised
  rather than promised. `test-inf-checks.ps1` compares the two INFs
  directly (hardware ID, service and its five values, both log values
  and their defaults, the selective-suspend value, the OS-supplied
  file lists and flags, `[SourceDisksFiles]`, the `[Version]`
  identity, every shared `[Strings]` token) and asserts each file is
  **refused** under the other's profile - the check without which the
  two profiles could accept everything and distinguish nothing.
  `expected-footprint-amd64.txt` is the 64-bit package's own tracked
  footprint, task 11-V.3's claim for the second media
- the packager and the publisher staging a second architecture. Done
  2026-09-09 to the shape decided the same day: `releases\<version>\`
  carries `release-x86`, `debug-x86`, `release-x64` and `debug-x64`,
  four self-contained directories each with its own `xhci98.inf` and
  `xhci98.sys`, the x86 pair renamed so that no untagged directory
  silently means x86 - free now, because nothing has been uploaded, and
  `releases/README.md`'s write-once rule leaves the four cut versions
  alone.

  `make-package.ps1` gained **one** `-Arch` that moves three things
  together and cannot be split: the obj subdirectory, which of the two
  INFs is staged, and the architecture both gates run under. Three
  switches that could disagree would have had a silent wrong answer -
  an amd64 binary staged around the 32-bit INF installs on a 32-bit
  machine and then fails its load with no diagnostic - and the
  packager's self-tests drive exactly that mismatch in both directions
  and assert the refusal. Its default output carries the architecture
  too (`out\pkg-release-x86`), for the same reason the published
  directory does.

  `make-release.ps1` was the larger half. Every place the flavour word
  was a directory name now goes through a **release leg**, one object
  per published directory carrying its flavour, architecture, obj tree
  and INF; the identical-hash refusal compares within an architecture,
  where a collision is the only kind that could happen; and the
  generated `readme.txt` names the directories the cut actually wrote
  rather than a hardcoded `RELEASE\`, with a placeholder-completeness
  check so a template token can never render literally into a
  user-facing file.

  **`-Arch` defaulted to `x86` alone, and that default was asserted by
  the self-tests** - the plumbing staged four directories the moment it
  was asked to (`-Arch x86,x64`), and what had not happened was task
  21.5, so an ordinary cut published exactly what `1.0.2.0` did.
  Changing that line was the deliberate act a passing 21.5 licenses, and
  it was taken on 2026-09-09 once the release flavour had been read on
  the guest too. The assertion moved with it rather than being deleted

## 21.4 - the code changes task 21.1 implies

**21.4 - the code changes task 21.1 implies. Complete 2026-09-09**
(design record 11 section 9). Five of the seven were done with 21.2;
the last two, both of which needed no guest, were done the same day.
**An eighth followed the same evening and did need a guest** - the
`_WIN64` `USBPORT_SCATTER_GATHER_LIST` layout, which no measurement had
covered until the guest forced M8. It is recorded under 21.5 rather than
here, because what makes it worth reading is how it was found.

- the `_WIN64` packet declaration at the measured `0x250`. Reached by
  widening the two trailing `Reserved` canaries to `ULONG_PTR`, which
  M6 records as one of the two valid ways and which leaves the x86
  layout untouched. The asserts carry M3's and M6's measured numbers on
  each architecture rather than the compiler's own, so the eight-byte
  shortfall the scout found can actually fail one
- **`USBPORT_ENDPOINT_PROPERTIES` read off the amd64 `usbehci.sys`** -
  design record 11 section 5, **M7**, read 2026-09-09, method `static`.
  `sizeof` `0x48` measured from `OpenEndpoint`'s nine-qword copy rather
  than inferred, `BufferVA` at `0x20` read as a QWORD, `BufferPA` still
  4 bytes at `0x28`, and the TT pair at `0x38`/`0x3A`. The compiler's
  natural widening turned out to be right, so no `#ifdef` was needed -
  what the reading bought is the right to assert
- `USBPORT_RESOURCES` under `_WIN64` at M4's measured `0x48` -
  `InterruptAffinity` is now a `ULONG_PTR`, being a `KAFFINITY`, which
  is the whole of the gap and is identical on x86
- `src/xhci_dispatch.c:1001`'s implicit `ULONG_PTR` -> `ULONG`
  truncation, the one thing the amd64 compiler objects to in this
  source, and fatal in `release` only
- `src/xhci_compat.h`'s `ULONG_PTR` typedef guarded for 64-bit hosts.
  Windows is LLP64, so `unsigned long` stays 32 bits on amd64; the old
  unconditional typedef was accidentally correct only because the host
  suite has always been built x86
- design record 04's common-buffer arithmetic re-run against the amd64
  `sizeof`s and the result stated. **Nothing in the layout moves, and
  the reason is that nothing in it is a `sizeof`**: every region offset
  and size is a spec constant times a declared policy limit, written as
  a `UL` literal, and the only two `sizeof`s the carve touches are
  `XHCI_TRB` and `XHCI_ERST_ENTRY`, four `ULONG`s each. So
  `MiniPortResourcesSize` is 409,600 bytes on both architectures and
  usbport is asked for the same 101 pages on both. Measured rather than
  argued - the same headers through both compilers - and it is now a
  standing check rather than a reading, since `test_membuf` is one of
  the two suites the box below runs twice. What the pass did find is
  outside the buffer: the extensions usbport allocates for the miniport
  grow (`XHCI_EXTENSION` 91,612 -> 95,496, `XHCI_TRANSFER` 120 -> 152),
  which needs no code change because `DriverEntry` publishes each as a
  `sizeof`, but does mean the snapshot channel's x86 offset table cannot
  decode an amd64 extension. Design record 04 section 8 states the
  result and flags that for whoever first runs an amd64 binary
- `test/test_packet.c` compiling the header for amd64, so
  `test\run-host-tests.cmd` checks the layout on the build host
  rather than only inside a driver build. Done 2026-09-09, and it runs
  the file rather than only compiling it: **191 checks pass on each
  architecture**, with every expectation that differs carrying both
  numbers - M4's `USBPORT_RESOURCES` table, M7's
  `USBPORT_ENDPOINT_PROPERTIES` table, M3's packet size and short-copy
  boundary, and M6's widening map for the 50 callback slots between
  hand-typed anchors. `test_membuf` is built and run for amd64 by the
  same leg, for the opposite reason - it is where the numbers must NOT
  move - and passes 2,027 there. Two limits on the leg: it drops
  `/Za`, because the WDK's own CRT headers are not C89-clean and the
  x86 leg compiles the same files under it anyway; and it is skipped
  by name, with the run's verdict reading "x86 only", on a host with no
  WDK 7.1, since `tools/` is fetched per host rather than cloned

## 21.5 - the Windows XP x64 guest

**21.5 - the Windows XP x64 guest. COMPLETE 2026-09-09, and it cost one
defect.** Every checkpoint clause below was taken on the guest that
evening, on host `minis-w11p-ykm`, with the `qemu`-flavour amd64
`1.1.0.0` package staged to `vm\xferxp64`. The guest was a vehicle and
building it was preparation; this is the leg.

**The first amd64 binary ever to run did everything the static pass
predicted and then refused every control transfer.** It installed on an
xHCI-only machine, registered, started, passed its No Op self-test and
answered the whole root-hub family - and then failed each device
enumeration at the first control transfer with
`slot: control transfer refused by the builder, status=5`
(`XHCI_XFER_SG_HIGH_ADDRESS`). On a 2048 MB guest no physical address
above 4 GB exists, so the driver was reading the wrong bytes. The cause
was the one structure design record 11 had carried as an assumption
rather than a reading - `USBPORT_SCATTER_GATHER_LIST` - and it is now
**M8**: `SgElement[]` is at `0x20`, not the `0x1C` an all-`ULONG`
declaration produces, and the element's `SgTransferLength` and
`SgOffset` are each four bytes higher than on x86 while its `sizeof`
stays 24. Read statically off the producer in the NT 5.2 amd64
`usbport.sys` at RVA `0xF468`, fixed in `src/xhci_usbport.h` under
`_WIN64` alone, and now asserted there and in `test/test_packet.c` on
both architectures. The x86 binary does not move: two builds of
identical source differ in the same six bytes as a build across the
change, three import-table RVAs at the head of `.text`.

**Two things are worth carrying out of that.** The refusal came from the
scatter-gather high-DWORD check, which guards a condition M5 says cannot
arise and which design record 11 section 9 item 3 calls optional on the
evidence; it caught a defect nobody had predicted, on the first transfer,
cleanly and with a counter, instead of a TRB built from two unrelated
halves. And the reading needed a technique: a linear sweep of an amd64
`.text` desynchronises and lies, so the function boundaries come from
`.pdata` and each one is disassembled from its true entry with
`kd -z` - this DDK's `dumpbin` has no amd64 disassembler. Both are in
`lessons.md`.

- the guest installed from `scripts\setup-qemu-winxp64.ps1` and
  snapshotted `winxp64-clean-install` - 2026-09-08, the owner at the
  console for the product key, guest shut down from inside and the
  snapshot taken with the image cold (`qemu-img check`: no errors,
  13.36% of 16 GB allocated; snapshot ID 1, `VM_SIZE` 0 B, which is
  what a powered-off snapshot should read). The static listing taken for
  the readings below then corroborated the install independently: an
  11,767-file NTFS volume with `Documents and Settings\Administrator`,
  a System Restore `RP1`, and `Prefetch` entries for `CTFMON`,
  `RUNDLL32`, `MSIEXEC` and `MMC` timestamped after the last reboot -
  that is a system that reached a desktop and ran programs on it. **The
  installed system has still not been booted from the run launcher
  here**, so nothing about the xHCI is known yet
- the USB stack the guest will install hashed against
  `tools/winxp64-extracted/`. **All three match byte for byte**
  (`sha256`): `usbport.sys` `6fc83f49...05e1d`, `usbhub.sys`
  `92b1744e...30198`, `usbehci.sys` `657daf4a...83d9a`. Read
  2026-09-08 from the `winxp64-clean-install` snapshot, statically,
  with no boot: 7-Zip 26.00 lists straight through the qcow2's MBR and
  NTFS, so no `qemu-img convert` was needed. **Taken from
  `Driver Cache\amd64\sp2.cab` rather than from an installed copy,
  because there is no installed copy** - which is the box below
- whether an xHCI-only XP x64 install has `usbport.sys` on disk at all
  - **it does not, exactly as 32-bit XP.** No `usbport.sys`,
  `usbhub.sys`, `usbehci.sys` or `usbd.sys` anywhere in the 11,767-file
  image, `dllcache` included; the only `usb*.sys` present are
  `usb8023.sys` and `usbcamd2.sys`. They sit in
  `WINDOWS\Driver Cache\amd64\` - `usbport`/`usbhub`/`usbehci` in
  `sp2.cab`, and `usbd.sys` (7,552 B, RTM-dated) in `driver.cab`, not
  `sp2.cab`. So `Driver Cache\amd64` does behave like
  `Driver Cache\i386` and the INF's `LayoutFile` route is needed on the
  `.NTamd64` path too. **One difference from 32-bit XP worth carrying:**
  there, `usbd.sys` was on disk as a 4,736-byte stub; here it is not on
  disk at all
- the checkpoint clauses below, on the amd64 binary. Taken 2026-09-09,
  all seven, on `1.1.0.0` with the M8 fix. The owner drove the GUI (the
  standing 2026-09-03 decision); the hot-plugs and the readings were
  taken over the monitor on port 55562, and the trace is the
  `qemu`-flavour port-`0xE9` log
- **and then the same again on the `release` flavour**, taken the same
  evening on the same guest, because the clauses above were taken on the
  `qemu` build and **that flavour is never published**. Without this the
  64-bit package would have been cut on the strength of a binary no
  released package contains - on amd64 the two are not near-copies
  either, `release` being a free build where `qemu` is checked. Installed
  by Have Disk from the transfer drive's `RELEASE\` directory (94,720 B,
  `sha256 6dac158d...d7b5`, byte-identical to `src\objfre\amd64`), and it
  read: the mouse, the mass-storage device and the composite audio device
  all bound (**USB Human Interface Device**, **HID-compliant mouse**,
  **USB Mass Storage Device**, **USB Composite Device**, **USB Audio
  Device**), and the disable/enable/remove/rescan sequence survived.

  **How a release flavour is read at all, since it writes no port-`0xE9`
  trace, is the reusable part.** Three independent things say it: the
  boot log holds one `DriverEntry` from the outgoing `qemu` build, then
  `StopController`, then **nothing** - the replacement loaded and never
  printed, and the file stayed at 31,548 bytes through three device
  enumerations and the whole door sequence; the staged binary's hash
  matches the built one; and **QEMU's own `usb_xhci_*` trace witnesses
  what the driver was built not to say** - `slot_enable` 3 -> 9,
  `slot_address` 6 -> 18, `slot_configure` 4 -> 12 across the sequence,
  exactly three full re-enumerations of three devices. The silence and
  the trace are complementary: one proves which flavour is loaded, the
  other proves it is working

## 21.6 - the record

**21.6 - the record. Done 2026-09-09.** The tier is stated where
Windows ME's and 32-bit XP's are stated: `AGENTS.md` "Project Purpose"
and its Quick Reference, `build-and-test.md` "Windows XP x64 target
VM", `win98-wdm.md`'s new "And Windows XP x64?" beside "What about
Windows XP?", and `docs/using/release-notes.md`. `README.md` took a row
in its own target table with them, because that table states the same
thing to the same reader. The provenance is beside it in
`legal-provenance.md` section 3, whose amd64 paragraph now records that
a 64-bit binary has *run* and that this upgrades no `static` tag - the
rows themselves (M7, M8, the two `LayoutFile` readings, the NT 5.2
amd64 material, WDK 7.1) were already there from 21.1-21.5.

**The wording could not simply be reused, and the difference is the
point of the entry: this is the only target that is not the same
binary.** Every one of those documents says so, because the sentence
the other four tiers rest on - one binary, coded to the Windows 98
export baseline, therefore safe everywhere above it - has nothing to
say about a kernel Windows 98 never had. Two narrower qualifications
travel with it: only XP x64 was booted, so Server 2003 x64 rests on the
NT 5.2.3790 identity rather than on a run of its own; and Vista x64 and
Windows 7 x64 are **outside** the tier, which is task 21.8's question
and the open half of design record 11's decision 1. Three further
places were corrected in the same pass rather than left to drift:
`AGENTS.md`'s "a single `xhci98.sys` binary" now says which targets it
means (design record 11 section 11 asked for exactly that),
`AGENTS.md`'s Compiler and DDK rows name WDK 7.1 as the amd64
toolchain, and `xhci-data-structures.md`'s "the driver runs 32-bit
only" is now the measurement it rests on (M5) rather than a claim the
amd64 build appears to contradict. The user-facing copy of the release
notes - `make-release.ps1`'s readme template, which the script's own
comment binds to that file - gained the same statements behind three
placeholders that render empty on an x86-only cut, because
`-UploadSetOnly` re-renders the readme of already-published versions
and every one of those is x86-only. Packager self-tests still pass.

## 21.7 - the same six measurements on Vista x64 and Windows 7 x64

**21.7 - the same six measurements on Vista x64 and Windows 7 x64.**
Read 2026-09-09, extraction and disassembly only, no VM and no build.
**All six pass on both**, so one `WNET` amd64 binary can serve them as
far as this interface goes and the claim is not narrowed. Design record
11 section 6 has the outcome and its consequences; the transcription is
in `usb-xhci-info/usbport-miniport-abi.md`, "The 6.0 and 6.1 lineages".
Method `static` throughout.

- Vista x64 - `usbport.sys` and `usbehci.sys` extracted from
  `sources\install.wim` image 1, hashed, version-stamped
  (6.0.6002.18005), and the six read
- Windows 7 x64 - the same, 6.1.7601.17514
- recorded, and three differences from NT 5.2 amd64 with them, none of
  which reaches a miniport that registers with `Version = 200`: a fourth
  export (`DllInitialize`) shifting all three ordinals, two further
  packet version tiers above `0x250`, and - on 6.1 only - **a second
  `IoGetDmaAdapter` call that asks for a 64-bit adapter**, gated on the
  miniport declaring `Version >= 310` and filling packet slot `0x398`.
  The tier claimed is unchanged, and so are the two things true of both
  regardless: kernel-mode code signing, and the driver-store install
  path the `LayoutFile` route was not written for - though the media
  also shows both systems shipping `usbport.sys`, `usbhub.sys`,
  `usbd.sys` and `usbehci.sys` in `System32\drivers` outright, which is
  the opposite of an xHCI-only XP or 2000 install
- **the `i386` halves taken in the same pass** and handed to task 22.1,
  together with each system's `ntoskrnl.exe` and `hal.dll` for task 22.2

## 21.8 - the Vista x64 and Windows 7 x64 guests

**21.8 - the Vista x64 and Windows 7 x64 guests**, on the owner's
instruction of 2026-09-09. Task 21.7 read the interface and found
nothing against them; this is the leg that finds out whether the binary
runs. **It is a second leg, not a repeat of 21.5**, and the reason is
that neither of the two things 21.7 named as true regardless is an ABI
question - both are install-and-load questions that only a guest
settles, and one of them may not have an acceptable answer at all.

**The gate to settle first, because everything else is wasted if it
fails: kernel-mode code signing.** Windows XP x64 does not enforce it,
which is why 21.5 took that target. Vista x64 and Windows 7 x64 both do,
and the cross-certificate route that once made third-party Windows 7 x64
signing possible is no longer available in practice (design record 11
section 6). So an unsigned `xhci98.sys` loads on those systems **only**
on a boot with driver signature enforcement disabled (F8). Test-signing
mode is not a route for it: that mode loads a test-signed driver, and
this package is not signed (design record 11 section 12, decision 9).
Establish whether F8 works on each guest, and what
the user has to do at every boot, before spending a day on anything
else. If the answer is that the driver cannot be made to load at all,
that is the task's result and it is worth having.

**The second question, and 21.7 left it explicitly open: the driver
store.** The INF's `LayoutFile=layout.inf` route is a Windows 2000 and
XP mechanism; Vista and later stage the package into the driver store
first and validate its file list more strictly. 21.7 read one thing that
makes this look easier - both install images carry `usbport.sys`,
`usbhub.sys`, `usbd.sys` and `usbehci.sys` in `Windows\System32\drivers`
outright, in every architecture, so the Code 39 that Phase 19's fix
answers cannot arise and every `COPYFLG_NO_OVERWRITE` copy should skip
without needing a source. **"Should" is doing work there**: that is a
reading of an install image, not of an installed system, and it says
nothing about whether the driver store accepts the package's file list.
If it does not, the fix is a third INF or an `.NTamd64.6.0` decorated
section, and **that is a decision, not a change to make in passing** -
design record 11 section 12's decision 2 kept the 32-bit and 64-bit INFs
apart for a reason.

What is already settled and must not be re-litigated: `USBPORT_GetHciMn`
returns `0x10000001` on both, so no code change follows; and Windows 7's
second `IoGetDmaAdapter`, the one asking for a
64-bit adapter, is gated on `Version >= 310` and is unreachable from
here - **which is exactly why the high-DWORD check at
`src/xhci_xfer.c:542` must stay**, and M8 is the standing argument that
such checks earn their keep.

- the two guests built, from the media already on the development host:
  `en_windows_vista_sp2_x64_dvd_342267.iso` and
  `en_windows_7_professional_with_sp1_vl_build_x64_dvd_u_677791.iso` -
  the same media 21.7 read its measurements out of, so the stack a guest
  installs is the one that was measured, and saying so is cheap. Done
  2026-09-10: `scripts\setup-qemu-vista-x64.ps1` and
  `setup-qemu-win7-x64.ps1`, their launchers, `vm\vista-x64.img` and
  `vm\win7-x64.img` at 32 GB, and their rows in
  `scripts\test-qemu-launchers.ps1` (294 checks, 11 monitor ports, none
  shared). **Not two more copies of `setup-qemu-winxp64.ps1`**: they are
  the 64-bit half of the Vista and Windows 7 recipe and share
  `scripts\qemu-nt6-common.ps1` with the 32-bit pair, with a new `-Arch`
  selecting the four things that differ - the CPU refusal's missing
  feature (long mode, not the NX bit), the INF half and the
  `make-package.ps1 -Arch amd64` staging command, the code-signing
  paragraph, and the memory note. The 32-bit pair's generated launcher
  text was held byte-identical across that change and checked by
  generating both ways and comparing. The planned names
  `setup-qemu-winvista64.ps1` / `setup-qemu-win7x64.ps1` were dropped:
  they disagreed with each other and were coined when these guests were
  expected to be XP x64's siblings rather than Vista's and Windows 7's.
  **Monitor ports 55563 and 55564 are now claimed rather than reserved**,
  so the gate's assertion that nothing takes them is gone, replaced by
  one that these two still do; the ordinary no-two-guests-share-a-port
  scan covers them like any other guest. **Both guests were then
  installed on 2026-09-10** by the owner at the console, each with one
  snapshot of the clean install - `vista-x64-clean-install` and
  `win7-x64-clean-install`, `qemu-img check` clean on both
- **the accelerator probed on each, and the result recorded whichever
  way it goes.** Done 2026-09-10: **both guests want `tcg,thread=multi`**,
  each confirmed by an install that COMPLETED under it and was shut down
  from the Start menu. Both generators now carry that as a measured
  default, the launcher gate's refuses-without-`-Accel` check was deleted
  in the same change (294 checks to 290), and the gate's two 64-bit rows
  now pass no `-Accel` so its assertions read those defaults.

  **The two failed differently under `whpx,kernel-irqchip=off`, and the
  difference is what this box was for.** Vista x64 bugchecks inside WinPE
  before Setup writes a byte - STOP `0x0000000A`, address `0x10` at IRQL
  `0xC` on a read, a near-null dereference at device IRQL, which is the
  surface `kernel-irqchip=off` touches. Windows 7 x64 clears WinPE, runs
  its **entire first phase**, writes 7.27 GB, and only then wedges at the
  first restart: screen unchanged for eight minutes, `ide0-hd0` idle
  climbing monotonically past nine, RIP revisiting the same three
  addresses with `HLT=0`. Screens at `out\task-21-8\`.

  **For most of an afternoon that second reading looked like a
  disagreement with the first, and it was only a slower failure.** Which
  is precisely the rule this project paid for on 2026-09-10 and nearly
  paid for twice: an accelerator may not be written down until an install
  has COMPLETED under it. A prompt, a progress bar and a whole finished
  phase all prove only that the guest has not failed yet.

  **A wedge does not always leave a resumable image, and that qualifies a
  rule this repository had stated flatly.** "Switching to TCG costs no
  reinstall" held for `vm\vista.img`, which wedged on a boot *after* a
  completed phase and so still had a bootable disk. Windows 7 x64 wedged
  *at* the transition, before Setup laid its boot files down; the TCG
  relaunch met `BOOTMGR is missing` and Setup had to run again from the
  DVD. The rule needs its qualifier: no reinstall **provided the guest
  already has a bootable disk**.

  Worth noting and not worth promoting: the 64-bit pair agree where the
  32-bit pair disagreed, and with XP x64 also on `tcg` every 64-bit guest
  here now wants TCG. That is an observation about four guests, not a
  property of bitness
- the code-signing gate above: which route loads an unsigned driver on
  each guest, what it costs the user at every boot, and whether it
  survives a reboot at all.

  **Closed 2026-09-16 on F8, the one route there is.** Every NT 6.x x64
  leg since, on both guests, loaded the driver on an F8 boot (task
  22.5), at the cost measured below: the choice is made at the console
  on every boot and survives nothing. **`bcdedit -set TESTSIGNING ON`
  was removed from this task by the owner the same day, untried**:
  test-signing mode loads a test-signed driver, and the published
  package is not signed (decision 9), so it would measure a route
  that no user of the download can take without signing the driver
  themselves.

  **F8 TAKEN ON THE VISTA X64 GUEST, 2026-09-10, AND THE ANSWER IS YES:
  the amd64 binary loads and runs on 6.0.** `DriverEntry` completes,
  `USBPORT_GetHciMn` returns `0x10000001` and the packet size is `0x250`
  - task 21.7 read both statically and both are now measured live - the
  registration succeeds, usbport writes back its 16 service pointers (read
  as the first eight: the verifier walked the block as sixteen `ULONG`s
  until the 2026-09-17 audit's B1, and the corrected form is unmeasured in
  a guest) and
  calls `StartController`. **Box 3's decisive question is answered and
  boxes 4 to 6 are not foreclosed.** The cost is what the box predicted:
  the F8 menu item applies to exactly one boot, was re-chosen on every
  boot of the session, and survives nothing. Windows 7 x64 was not
  booted that day, so the box stayed open.

  Three things had to be fixed or found on the way, in the order they
  bit, and none of them was a signing question:

  1. **The install aborted before the driver existed on disk.** The
     driver store *accepted and staged* the package - the refusal box 4
     anticipated did not happen - but `_COMMIT_FILE_QUEUE` then aborted
     on `usbport.sys`: setupapi resolved it through
     `LayoutFile=layout.inf` against Vista's own `usbport.inf_518a1f35`
     driver-store package, built an unresolvable source path, and
     `SPFILENOTIFY_NEEDMEDIA` returned `FILEOP_ABORT`.
     `COPYFLG_NO_OVERWRITE` does not save it: the queue resolves the
     source **before** it decides to skip, which is exactly the "should
     is doing work there" caveat this task put on 21.7's reading.
     `usbui.dll` pruned cleanly; the three `[Xhci.CopyNT]` files did not.
  2. **A `.6.0`-decorated install section is silently never read.** The
     OS-version part of a `TargetOSVersion` decoration selects the
     **models** section only; the install section takes the platform
     extension and nothing more, so setupapi ran `[Xhci.Dev.NTAMD64]`
     while logging a driver node of `XhciModels.NTamd64.6.0`. The shape
     that works names a **different install section** from the 6.0 models
     section - `Xhci.Dev6` - and it passes the INF gate as written.
     **Measured on a staged copy only; `src/xhci98-amd64.inf` is
     untouched and the shipping shape is not decided.**
  3. **`USBPORT_RegisterUSBPortDriver` takes a fourth argument on NT
     6.x**, and a three-argument call bugchecks `0x7E` inside usbport's
     `memmove` before any of section 5's measurements is exercised.
     Fixed for amd64 under a runtime arity branch; design record 11
     section 6.1 and decision 10 carry the evidence and the reasoning,
     and **Phase 22 will meet the same boundary on 32-bit, where the fix
     is harder**.

  **WHERE IT STOPPED NEXT, AND THAT IS NOW READ AND FIXED.** The driver
  refused its own initialisation at `XHCI_INIT_STEP_RESOURCES` with
  `ResourcesTypes = 0x0C`, which Device Manager shows as Code 10. Our
  constants were `PORT=1, INTERRUPT=2, MEMORY=4`, so `0x0C` had memory
  but not interrupt, and an undefined bit 3 instead. The rest of the
  resource block mapped onto the amd64 `USBPORT_RESOURCES` layout
  perfectly and the interrupt fields were fully populated - vector
  `0x92`, IRQL 9, affinity `0x0F` for the guest's four vCPUs,
  `ShareVector` set - and `HcFlavor = 1000` is `EHCI_Generic`. The mask
  was also **exactly the NT 5.x value shifted one bit left**
  (`INTERRUPT|MEMORY = 0x06`, and `0x06 << 1 = 0x0C`), which would follow
  from 6.x inserting a member at the bottom of the resource-type enum -
  an inference from a structural coincidence, and the task refused to
  change a constant on it.

  **Read out of `USBPORT_ParseResources` on 2026-09-10, static, in five
  shipping binaries, and the inference was right.** NT 6.x splits the
  port bit in two - an I/O-space port keeps `0x01`, a memory-mapped one
  takes `0x02` - and interrupt and memory move up to `0x04` and `0x08`.
  `MEMORY|INTERRUPT` is therefore `0x06` on NT 5.x and `0x0C` on NT 6.x.
  **XP x64 is what makes it a reading rather than a guess**: the same
  function in `winxp64-extracted` writes `1`, `2` and `4` where
  `vista-x64-extracted` writes `1`/`2`, `4` and `8`, so the value moved
  rather than the build merely differing. The x86 pair were read in the
  same pass and agree, which is Phase 22's half of it. Which branch is
  which is fixed twice - by the descriptor-scan loop's registers and,
  independently, by the `USB_MINIPORT_FLAGS_*` bit each branch tests as
  its guard. Design record 11 **section 6.2** has the RVAs, the
  instructions and the command.

  **Fixed for amd64 and not for x86**, on section 6.1's reasoning:
  `DriverEntry` already asks `IoIsWdmVersionAvailable` for the
  registration arity, and the same answer now settles
  `XhciResourcesRequired`, which `XhciInitController` reads at step 1.
  Unlike the arity branch **this one fails towards NT 5.x**, because the
  cost of being wrong is a legible refusal rather than a bugcheck. No
  import was added on either architecture, every gate is green, and the
  host tests carry both arms - which is the only place any host here can
  exercise the NT 6.x arm at all.

  **The version predicate itself was wrong until 2026-09-11 and both
  branches inherited it.** It was written `(1, 0x30)`; Server 2003
  reports WDM 1.30 exactly and Windows XP x64 *is* Server 2003, so XP x64
  answered TRUE, took the NT 6.x arm and refused at step 1 - **a
  regression in committed code, in the one x64 target the project
  claims.** It is now `(6, 0)`, read out of three kernels
  (`winxp64` 1.30, `vista-x86` 6.00, `win7-x86` **6.00, not 6.01**).
  Design record 11 section 6.3 is the record. This is exactly what box 5
  existed to catch, and it caught it on the first boot.

  **Two things about this are the owner's to weigh, and neither is
  settled here.** First, `XhciResourcesRequired` is one unconditional
  global rather than an `#ifdef`, so the *32-bit* binary changes too -
  not in behaviour, since nothing outside the `_WIN64` guard ever writes
  it and the mask it holds on x86 is the `0x06` the code always compared
  against, but the codegen is a load where it used to be an immediate and
  **the binary is therefore not byte-identical to the one the four x86
  install legs were taken on**. The alternative - a macro on x86 and a
  global elsewhere - buys byte-identity at the price of the host test
  exercising a shape the shipping x86 driver does not have, which is the
  trade this project has refused before. Whether that warrants an x86
  re-validation pass is a call, not a fact. Second, Phase 22 inherits the
  32-bit half of this alongside the arity, and of the two this is much
  the cheaper: the arity needs a second decorated import stub, this needs
  only the version predicate that stub's branch already computes.

  **Both guests have now answered.** Vista x64, 2026-09-10: the mask fix
  clears step 1, all 22 init steps run, Device Manager's Code 10 goes and
  a `USB Root Hub` appears - but `dpc count=00000000`, which is task
  21.8's remaining wall and design record 11 section 6.4. XP x64,
  2026-09-11, with the corrected predicate above: `wdm pre-6.00 ...=1`,
  `resource bits required=00000006`, registration status 0 on the
  **three-argument arm, executed for the first time anywhere**, `init
  step=00000016 / init status=00000000`, No Op self-test `CC_SUCCESS`, a
  hot-plugged HID mouse addressed and carrying transfers, and `isr count
  == isr claimed == dpc count` climbing together. **Box 5 is closed.**
  Evidence: `vm\task218-evidence\winxp64-wdm600-revalidation-boot.log`.
  Vista x64 was then re-checked with the corrected predicate on
  2026-09-11 (`built Sep 11 2026 00:18:56`, QEMU 11.1.0, from the
  clean-install snapshot through the staged `Xhci.Dev6` INF under F8):
  `wdm pre-6.00 ...=0`, `resource bits required=0000000C`, registration
  status 0, all 22 init steps, and the same `dpc count=00000000` wall -
  so both arms of `(6, 0)` are observed, not reasoned. Evidence:
  `vm\task218-evidence\vista-x64-wdm600-revalidation-boot.log`.
  **The host-side half is done, 2026-09-10, before either guest existed
  - which is the point, since this is the gate that comes first.** A
  complete test-signed `Vista_X64,7_X64` package can be produced from
  tools already in this repository with no network and nothing
  installed: `MakeCert`, `SignTool` and `CertMgr` in
  `tools\WinDDK71\bin\x86`, `Inf2Cat` in `tools\WinDDK71\bin\selfsign`.
  Sign the `.sys` first, then `Inf2Cat`, then sign the `.cat` - the
  catalog hashes the signed binary. One host prerequisite, and its
  absence is silent: `Inf2Cat` is a managed .NET 2.0 application, and on
  a stock Windows 11 host it exits `0x80131700` **printing nothing at
  all**. The owner enabled .NET 3.5 on 2026-09-10 and it now runs in
  place; where that is not possible, a `.config` beside a copy of it
  runs it on .NET 4, and both routes give **identical** diagnostics -
  so the two findings below are readings of the package, not of the
  runtime. `build-and-test.md`, "Vista x64 and Windows 7 x64 target
  VMs", has the commands; two of the findings change what this task
  owes:

  **The package is not signed, and as of 2026-09-10 that is decided
  rather than pending** (owner). Signing a release buys the claimed tier
  nothing: XP x64 and Server 2003 x64 do not enforce kernel-mode code
  signing, which is the whole reason 21.5 took that target, and the
  published package installs and loads there unsigned today. Vista x64
  and Windows 7 x64 are outside the tier until this task says otherwise,
  so a decision never to sign costs the tier nothing at all. **What the
  guests measure is what a USER would have to do**, not what this project
  ships, and that measurement stands either way.

  **So the INF line is retired as an open question rather than
  deferred.** `Inf2Cat` refuses `src\xhci98-amd64.inf` outright -
  `22.9.4: Missing AMD64 CatalogFile entry ... from [Version] section` -
  and adding `CatalogFile.NTamd64=xhci98.cat` makes it pass with zero
  errors. **It has not been added and does not need to be**, because a
  catalog is not what the loader checks. `src\xhci98-amd64.inf` stays
  byte-identical, design record 11 section 12's decision 2 is
  undisturbed, and the INF gate is not asked for an opinion it does not
  owe.

  **What makes that true: nothing is signed, so there is nothing for a
  catalog to vouch for.** The catalog governs the install-time publisher
  prompt rather than the loader, and an unsigned package there costs a
  "Windows can't verify the publisher" dialog and an "Install anyway"
  click, which is a cost to record and not a block. The Vista x64 guest
  confirmed it: the driver store staged the package with only a `sto:`
  warning about the missing `CatalogFile` (box 4).

  **And Microsoft's own package validator blesses the `LayoutFile`
  route, by name, for exactly these two systems.** All four OS-supplied
  files come back as `22.9.10: ... missing from [SourceDisksFiles]
  section ...; ok if file source is provided via LayoutFile in
  [Version].` and nothing else. That is a better prior than the "should"
  the second question rests on - it is the vendor's tool asked about
  `Vista_X64` and `7_X64` specifically - and **it is still not the
  reading**: a signability test is not the driver store at install time.

  What is left is entirely guest-side: **F8, Disable Driver Signature
  Enforcement**, which needs no signing at all and is per boot by
  design. Record what it costs the user at every boot. It needs no
  certificate, no catalog and nothing staged, so it goes straight to the
  question that decides this task: does the amd64 binary load and work
  on these systems at all. A negative there closes 21.8 and boxes 4 to 6
  never happen.

  **WHAT F8 COSTS IS WHAT THE TIER WORDING HAS TO SAY.** It applies to
  exactly one boot, by design, so the finding it produces is "the user
  must press F8 and choose that option every time the machine starts".
  For a USB host controller driver that is a heavy cost rather than a
  footnote: the machine can never boot unattended into working USB. That
  is a materially different claim from the one this project makes about
  XP x64 and Server 2003 x64, where the package installs and loads with
  nothing asked of the user, and the tier wording has to say so.

  Two things F8 does NOT do, so neither reads as a failure when it
  happens:

  - **It does not silence the install-time publisher prompt.** "Windows
    can't verify the publisher of this driver software" still appears
    during Update Driver, and still wants "Install this driver software
    anyway". That prompt is the catalog's business, not the loader's -
    the same distinction the paragraphs above rest on - and it is a cost
    to write down rather than a block.
  - **It says nothing about the box below it.** Whether the driver store
    accepts this package's file list is a different mechanism with a
    different failure mode. A clean load under F8 is not evidence about
    it in either direction.

  On the guests the F8 menu is reachable over the PS/2 keyboard, so there
  is no chicken-and-egg between the boot menu and the controller being
  installed
- the `.NTamd64` package installed on each guest, or the driver-store
  refusal characterised precisely enough to decide what would fix it.

  **Closed 2026-09-16, on the owner's decision.** Both halves this box
  was left owing are answered: the `Xhci.Dev6` shape reached the
  shipping file (task 22.5's INF box, design record 11 section 12,
  decision 13), and Windows 7 x64 installed the package through a
  staged copy of that same shape on 2026-09-12 and 2026-09-13
  (`p225win7x64re1`, `fix7win7x64`), every clause passing on the
  second. Vista x64 has also installed through the committed file
  (`vm\vistax64-rr\`, 22.5). **Windows 7 x64's first install through
  the committed `src\xhci98-amd64.inf` is task 22.10's seventh install
  leg**, which reads `setupapi.log` for it

  **ANSWERED FOR VISTA X64, 2026-09-10, AND NOT THE WAY THIS BOX
  EXPECTED.** The driver store does **not** refuse the package: it
  accepted and staged it, `xhci98.inf_0da41353\` in the FileRepository,
  with the only complaint a `sto:` warning that the INF carries no
  `CatalogFile` for the architecture - which did not stop staging, so
  decision 9's no-signing choice costs nothing structural on 6.0. What
  failed is a later and separate step, the file copy queue, and box 3
  above carries it. The `.NTamd64` package therefore installs on 6.0
  once the copy list stops asking for the four OS-supplied files, which
  the `Xhci.Dev6` shape does. **Windows 7 x64 is still owed, and so is
  the decision about whether that shape reaches the shipping INF** - it
  has only ever run from a staged copy.
- then the same clauses 21.5 took, on each guest: registered and
  started, the No Op self-test, the root-hub callbacks, a HID mouse, a
  mass-storage device and a composite audio device bound, and the Device
  Manager disable/enable/remove/rescan sequence. **On the `release`
  flavour as well as `qemu`**, for the reason 21.5 found the hard way -
  the clauses were taken on a build that is never published, and closing
  that gap is what licensed the 64-bit publisher default.

  **The `qemu` half is taken, under task 22.5, and the `release` half
  moved to Phase 22 by the owner on 2026-09-16.** Every clause passes on
  both guests on the `qemu` build with issue 7's fix (`fix7vistax64`,
  `fix7win7x64`, 2026-09-13), and Vista x64's remove and rescan held
  five times more on 2026-09-16 (`vm\vistax64-rr\`). The `release` build
  has run on neither guest. It is read in task 22.10, from the
  published asset, alongside the other install legs; that leg is also
  Windows 7 x64's first install through the committed
  `src\xhci98-amd64.inf` rather than a staged copy
- the tier decided and stated with the rest in task 21.6, including the
  signing requirement, which belongs in the release notes beside the
  tier rather than in a footnote. **Decided and stated 2026-09-16**
  (owner): Vista and Windows 7, both architectures, supported in
  virtual machines with no checkpoint tax, on the `qemu` build's
  evidence, with the F8-at-every-boot cost beside the x64 half in
  `AGENTS.md`, `build-and-test.md`, `win98-wdm.md` and the release
  notes, and the runtime row in `legal-provenance.md` section 4.
  `README.md` says only that signature enforcement has to be disabled
  because the driver is unsigned, on the owner's instruction of the
  same day, and leaves the per-boot detail to the release notes. Task
  22.5's record box is the same change

    Not a checkpoint clause of this phase. Phase 21's checkpoint below is
    written about a Windows XP x64 guest and passed on 2026-09-09; this task
    does not reopen it. **The phase closes when 21.6 and 21.8 are done**, and
    21.8 may close with a negative result - "the driver cannot be loaded on
    these without disabling signature enforcement, and here is exactly what
    that costs" is a complete answer to the question asked.

## The checkpoint, clause by clause

Checkpoint (the Windows XP x64 guest). Every clause, or the phase is not
closed:

- the static pass complete and transcribed into
  `usb-xhci-info/usbport-miniport-abi.md`, every fact tagged `static`.
  **Eight measurements, not the six the phase opened with**: M7 came from
  the compile scout and M8 from this guest
- the gates green on an amd64 binary. All three flavours, every gate,
  2026-09-09: the amd64 import gate resolving all seven module/symbol
  pairs directly against the NT 5.2 amd64 kernels and the one HAL, the
  flavour marker, both INF profiles, and the host suite running
  `test_packet` and `test_membuf` for amd64 as well as x86
- on a Windows XP x64 guest: the package installed on an xHCI-only
  machine. No `usbport.sys`, `usbhub.sys`, `usbehci.sys` or `usbd.sys`
  existed on that install; the `.NTamd64` half's `LayoutFile` route placed
  the stack from `Driver Cache\amd64` with no prompt for the CD, and
  Device Manager showed **USB 2.0 eXtensible Host Controller (xhci98)**
  and **USB Root Hub** with no yellow bang
- the driver registered and started, and its No Op self-test passed.
  `USBPORT_GetHciMn=10000001`, `packet size=00000250`,
  `MiniPortExtensionSize=00017508`, `MiniPortTransferSize=00000098`,
  `MiniPortResourcesSize=00064000` and `common buffer usbport will
  request=00065000` - M2's and M3's measured values and task 21.4's
  computed amd64 sizes, now read off a running driver, with the common
  buffer identical to x86 as design record 04's re-run said it would be.
  `USBPORT_RegisterUSBPortDriver status=00000000`,
  `No Op self-test completion code=00000001`
- the root-hub callbacks answered. `RH_GetRootHubData` reporting 4 managed
  ports, `RH_GetPortStatus`, `RH_SetFeaturePortPower` on all four,
  `RH_ClearFeaturePortConnectChange`, `RH_ChirpRootPort`,
  `RH_GetHubStatus`, `RH_EnableIrq`/`RH_DisableIrq`; port map 4 USB2-only
  ports, 0 USB3, all four powered. No `SuspendController` on an idle
  controller, so the NT half's `DisableSelectiveSuspend` works here as it
  does on 32-bit XP
- a HID mouse, a mass-storage device and a composite audio device bound.
  Hot-plugged from the monitor onto the `p3=0` root ports: **USB Human
  Interface Device**, **USB Mass Storage Device**, and **USB Composite
  Device** plus **USB Audio Device**, all without a yellow bang. The audio
  device declared isochronous endpoints and they were opened; its
  `endpoint speed differs from the port's, usbport << 8 | decoded=00000302`
  line is the byte-for-byte precedented one from the Windows 98 audio run,
  a 12 Mb/s device on a 480 Mb/s port
- the Device Manager disable, enable, remove and rescan sequence survived.
  Read in the trace as `SuspendController: halted, USBCMD=00000000` then
  `StopController` (disable), `DriverEntry` + `StartController` with all
  three devices re-enumerating (enable), `StopController` (remove), and a
  fresh `DriverEntry` + `StartController` reinstalling from the driver
  store with no media prompt and "Your new hardware is installed and ready
  to use" (rescan). 321 transfers completed across the run and **every
  refusal and error counter zero**, `transfer error events` included
