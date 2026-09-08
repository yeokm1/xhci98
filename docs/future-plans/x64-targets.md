# 64-bit targets: what would have to be measured first

Status: proposed, not scheduled. Written 2026-09-08, prompted by pull request
6 (`WDK 7.1`, GeorgeK1ng), which adds a GitHub Actions job that builds the
driver with WDK 7.1 for Windows XP, Server 2003, Vista and Windows 7 in both
x86 and x64, and by the owner's narrowing of it to 64-bit guests only.
Nothing on this page has a phase or task id and none of it has been built.
No 64-bit binary of this driver has ever been produced in this tree, and no
64-bit `usbport.sys` has ever been read.

This page is an investigation plan, not a design. Its subject is the set of
measurements that decide whether a 64-bit target is possible at all, in what
order to take them, what each one costs, and what each outcome would mean.
It deliberately stops short of proposing an implementation: the project's
rule is that miniport ABI facts are derived from the shipping binaries and
never inferred (`AGENTS.md`, "What NOT to Do"), and every 64-bit ABI fact is
currently unmeasured.

---

## 1. The narrowing, and why it matters to the plan

The pull request builds seven configurations. Two observations collapse that
to one binary and one guest before any work starts.

**Windows XP Professional x64 and Windows Server 2003 x64 are the same
operating system.** Both are NT 5.2.3790; the x64 edition of XP is the
Server 2003 x64 kernel with a client licence and shell. They are one target
under two names, not two targets, and one binary built for NT 5.2 amd64
serves both by identity rather than by compatibility. The measurements below
are therefore taken once, on one set of binaries.

**The x86 half of the pull request should not be taken at all.** 32-bit
Windows XP is already a supported VM target (`AGENTS.md`, Quick Reference)
and is served by the same MSVC 6.0 / Windows 2000 DDK binary as the two
primary targets. A second x86 toolchain producing a *different* x86 binary
subtracts from the one-binary property that `docs/using/release-notes.md`
states, and buys nothing: the existing binary already installs and runs
there.

What remains is one question with two halves: can this driver register with a
64-bit `usbport.sys` at all, and if so, does one NT 5.2 amd64 binary also
serve Vista x64 and Windows 7 x64? Section 4 answers the first. Section 5
answers the second at almost no extra cost, and section 6 says what each
answer would mean.

---

## 2. What the driver assumes today that 64 bits touches

None of these is a defect. Each is a decision the tree made deliberately for
two 32-bit primary targets, recorded where it was made, and each is a place a
64-bit build changes meaning.

| Where | What it assumes | What 64 bits does to it |
|---|---|---|
| `src/xhci_usbport.h`, `USBPORT_RESOURCES.StartPA` | The common-buffer physical address is a `ULONG` at offset `0x2C`, "high DWORD is 0" | If the real amd64 structure holds a full 8-byte `PHYSICAL_ADDRESS` there, every field after it is misread. **Measurement M4.** |
| `src/xhci_usbport.h` header, "binary-confirmed facts" | Registration copies exactly `0x13C` (316) bytes for a `Version >= 200` miniport, and usbport writes 16 service pointers at `0xE4`-`0x120` | Both numbers are x86 numbers. A different size on amd64 means usbport reads past `XhciRegPacket`, a static global, or writes service pointers into the wrong fields. **Measurements M3 and M6.** |
| `src/xhci_dispatch.c:4640` | `USBPORT_GetHciMn` returns one of exactly two values, and any third value is refused with `STATUS_UNSUCCESSFUL` | NT 5.2 is a third lineage and its value has never been read. Until it is, an x64 build very likely refuses to register on the one target it was built for. **Measurement M2.** |
| `src/xhci_dispatch.c:4670`, `:4678` | Inline `__asm mov espBefore, esp` brackets the registration call under `XHCI_DBG_TRACE` | MSVC has no inline assembler on x64. The `qemu` flavour - the one a VM leg would most want - does not compile until this is excluded under `_WIN64`. It is also unnecessary there: x64 has one calling convention, so the stack-delta check has nothing to catch. |
| `src/xhci_compat.h:34` | `ULONG_PTR` is `typedef unsigned long` | Wrong under `_WIN64`. Harmless while the host tests build x86; a guard is needed before any 64-bit compile of the DDK-free core. |
| `src/xhci_xfer.c:542`, `src/xhci_probe.c:162` | A scatter-gather element with `SgPhysicalAddressHi != 0` is refused | This one is already right, and is why a >4 GB transfer address would be a clean failure rather than silent corruption. It does not cover the controller common buffer, which arrives through `StartPA`. **Measurement M5.** |
| `docs/contributing/design/04-controller-common-buffer.md` | The fixed 400 KiB block's arithmetic, derived from the sizes of the driver's own structures | Several of those structures hold pointers and grow on amd64. The derivation is arithmetic over `sizeof`, so it does not silently break, but it has to be re-run and the record re-stated rather than assumed to carry. |
| `AGENTS.md`, "Language and arithmetic" | No 64-bit arithmetic, because Windows 98's kernel may lack the `_alldiv`-style compiler helpers | This constraint exists for Windows 98 and does not bind an amd64 binary that never runs there. It must not be relaxed in shared code for that reason: the shared source still has to compile for Windows 98. |

---

## 3. The material to extract, and how it is recorded

The owner is fetching a Windows XP Professional x64 SP2 ISO. Three files off
that media are the whole input for section 4.

- `USBPORT.SYS` - the port driver whose private contract is the subject.
- `USBEHCI.SYS` - the shipping amd64 miniport. This is the more valuable of
  the two: it *fills* the registration packet and *reads*
  `USBPORT_RESOURCES`, so its stores and loads give the field offsets
  directly. That is the same method the 32-bit record was built with.
- `USBHUB.SYS` - not needed for the ABI, worth hashing while the media is
  mounted so the record is complete.

Extraction, host-side, no VM:

```bat
rem The ISO opens directly; the x64 media's AMD64 directory is the
rem equivalent of I386 on the 32-bit media, and its files are compressed.
7z x xp64.iso -oxp64-media
expand xp64-media\AMD64\USBPORT.SY_ tools\winxp64-extracted\usbport.sys
expand xp64-media\AMD64\USBEHCI.SY_ tools\winxp64-extracted\usbehci.sys

certutil -hashfile tools\winxp64-extracted\usbport.sys SHA256
powershell -c "(Get-Item tools\winxp64-extracted\usbport.sys).VersionInfo.FileVersion"
```

Expect an NT 5.2.3790.x version; record the exact one rather than assuming
it. Routine unpacking of shipped media is the recorded adjacent case in
`docs/contributing/legal-provenance.md` section 1 and needs no separate
decision.

Two rules bind what happens next, and neither is optional:

- **The binaries are not tracked, and neither are any disassembly listings
  taken from them.** They go under `tools/winxp64-extracted/`, which is
  git-ignored, exactly as `tools/nusb-extracted/` and
  `tools/win2ksp4-extracted/` already are. What gets committed is the
  *facts*, in a form a fresh clone can re-derive: the file version, the
  SHA-256, the address, the instruction, and the value read.
- **Every fact this investigation produces is tagged `static`** - read from a
  disassembly, nothing executed - and gets its row in
  `docs/contributing/legal-provenance.md` section 4 in the same change that
  writes it down. A reading is not upgraded to `runtime` because a VM later
  happens to boot.

One tooling note that will otherwise cost an afternoon: **MSVC 6.0's
`dumpbin` cannot disassemble amd64 code.** The tree's existing procedure
(`docs/usb-xhci-info/usbport-miniport-abi.md`,
`tools\MSVC600\VC98\Bin\DUMPBIN.EXE /disasm`) does not carry over. WDK 7.1
ships a `dumpbin` / `link /dump` that does, which is convenient if the WDK is
being fetched for the build anyway, and any modern disassembler serves
equally.

---

## 4. The six measurements

Take them in this order. M1 through M3 are cheap and any one of them can end
the investigation; M4 through M6 are the expensive transcription work and are
only worth starting once the first three pass.

### M1 - Does amd64 `usbport.sys` still export the two names?

**Question.** Does the export table contain `USBPORT_RegisterUSBPortDriver`
and `USBPORT_GetHciMn`, exactly those names, undecorated?

**How.** `dumpbin /exports tools\winxp64-extracted\usbport.sys`.

**Why it matters.** These are private exports with no import library. The
existing `scripts\usbport-lib\usbport-imports.expected` manifest records both
names as verified against three 32-bit lineages; amd64 is a fourth. Note that
amd64 has no `__stdcall` name decoration, so the `_Name@N` symbols
`scripts\make-usbport-lib.cmd` checks for do not exist there - the
generator's decoration check is x86-only by nature, not by oversight.

**Record.** A fourth lineage in the manifest's header comment.

**If it fails.** Stop. There is no Option A on that operating system.

### M2 - What does `USBPORT_GetHciMn` return on NT 5.2 amd64?

**Question.** The single 32-bit constant the function returns.

**How.** Disassemble the exported function. On both 32-bit lineages it is a
two-instruction body: load an immediate, return. Expect the same shape.

**Why it matters.** This is the sharpest edge in the whole plan.
`src/xhci_dispatch.c:4640` refuses to register when the value is neither
`0x57324B30` (Windows 2000 / NUSB) nor `0x10000001` (XP 5.1.2600). NT 5.2 is
a third lineage. **If its value is a third constant, an x64 build fails in
`DriverEntry` and never registers** - and the symptom is a yellow bang
indistinguishable from a bad INF, which is exactly the failure mode that
would be misread as an ABI problem and cost days.

**Record.** A `USBPORT_HCI_MN_*` constant beside the existing two in
`src/xhci_usbport.h`, with the file version it was read from, and the
corresponding row in the ABI record.

**If it fails.** It cannot fail; it produces a number. The number is either
already known or new, and either way it is one line of code.

### M3 - What is the version gate, and how many bytes does registration copy?

**Question.** Two numbers and one comparison: the `Version` argument values
the function tests, and the byte count it copies out of the caller's packet
for a `Version >= 200` miniport.

**How.** Disassemble `USBPORT_RegisterUSBPortDriver`. On the 32-bit builds
the shape is a comparison chain against `100` and `200` followed by a copy of
`0x12C` or `0x13C` bytes. Read the amd64 equivalent's constants and its copy
size, whether that is a `rep movs` count or a `memcpy` argument.

**Why it matters.** usbport chooses the copy size itself, from the version
the miniport declares; the miniport does not tell it how large its packet is.
If amd64 usbport copies more than `sizeof(USBPORT_REGISTRATION_PACKET)` as
this driver would declare it, it reads past `XhciRegPacket` - a static global
- and writes service pointers past its end.

**Record.** The ABI record's registration section, alongside the `0x13C` /
`0x12C` pair it already states for x86.

**If it fails.** A copy size that does not correspond to the natural 64-bit
widening of the 316-byte packet means the amd64 packet is a *different*
structure, not the same one with wider pointers. That is a much larger piece
of work than this page describes, and the honest response is to stop and say
so.

### M4 - The `USBPORT_RESOURCES` layout

**Question.** The offset and width of every field, and in particular whether
`StartPA` is 4 bytes or 8.

**How.** Read the amd64 `USBEHCI.SYS`'s `StartController` callback, which is
handed a pointer to this structure and loads from it. Each load's
displacement is a field offset; each load's width is a field width. This is
the method the 32-bit table was built with, and it is more informative than
reading usbport's stores, because the miniport's uses disambiguate what each
field is *for*.

**Why it matters.** `ResourceBase` (the mapped BAR0 virtual address),
`StartVA` and `StartPA` are the three fields the driver's start path depends
on, and `XhciCheckResourceBase(ULONG_PTR startVA, ULONG startPA)` takes the
last as a `ULONG`. A widened `StartPA` shifts everything after it.

**Record.** A second `USBPORT_RESOURCES` table in
`docs/usb-xhci-info/usbport-miniport-abi.md`, explicitly labelled amd64 and
kept separate from the x86 one.

**The consequence that makes this measurement worth its cost.** Pull request
6 adds a block of `XHCI_C_ASSERT`s under `#ifdef _WIN64` asserting
`sizeof(USBPORT_RESOURCES) == 0x48`, `ResourceBase` at `0x28`, `StartVA` at
`0x38`, and so on. Those numbers are what MSVC produces for the project's own
declaration after natural pointer widening, so **the assertions assert the
compiler against itself and cannot fail.** Its own comment says they are a
compile-time guard and must not be cited as evidence, which is honest, but it
leaves the declaration itself unanchored. M4 is what makes those numbers mean
something: once measured, either the compiler's natural layout matches the
binary - in which case the asserts become a real guard - or it does not, and
the declaration needs explicit padding to force agreement.

### M5 - Is the DMA adapter still created 32-bit?

**Question.** Does amd64 usbport create its DMA adapter with
`Dma32BitAddresses` set, as the 32-bit builds do?

**How.** Read the `DEVICE_DESCRIPTION` that usbport's start path fills before
`IoGetDmaAdapter`, and the flags it sets in it.

**Why it matters.** `src/xhci_usbport.h`'s scatter-gather comment records the
measured reason the high DWORD of a transfer address is always zero: *the
adapter is created 32-bit, not because any element writer forces it*. That
sentence is a 32-bit-target measurement. On a 64-bit guest with more than
4 GB of RAM, if the adapter is created 64-bit, the common buffer and the
transfer buffers can land above 4 GB. The transfer path already refuses that
cleanly (`src/xhci_xfer.c:542`); the controller common buffer, which arrives
through `StartPA`, has no equivalent guard.

**Record.** The ABI record, and - whichever way it reads - a `StartPA`-side
refusal in the driver, on the same "check it, never assume it" principle the
scatter-gather path already follows.

**Interim mitigation.** Until M5 is answered, cap the guest at 3.5 GB of RAM.
That makes the question moot for a first VM leg without deciding it.

### M6 - The service-pointer block

**Question.** The offsets of the 16 service pointers usbport writes into the
packet after copying it, and the offsets of the miniport callbacks it reads.

**How.** As M4: the amd64 `USBEHCI.SYS` calls through these, and usbport
stores into them.

**Why it matters.** The driver calls `UsbPortRequestAsyncCallback`,
`UsbPortInvalidateController` and `UsbPortGetMiniportRegistryKeyValue`
through this block (`src/xhci_cmd.c:276`, `:329`,
`src/xhci_dispatch.c:503`). A one-slot shift is a call through the wrong
function pointer with the wrong arguments.

**Record.** A second offset table, labelled amd64, beside the x86 one.

---

## 5. Reading the same six on Vista and Windows 7

Every measurement above is a static read of two files. Doing it twice more -
Vista x64 and Windows 7 x64 - costs extraction time and nothing else, needs
no VM, and answers the question the narrowing left open: does one NT 5.2
amd64 binary also serve them?

The prior is mildly favourable and is *inference, not measurement*. Vista and
Windows 7 still ship `usbehci.sys`, `usbohci.sys` and `usbuhci.sys` as
usbport miniports; `usbport.sys` survives to 6.1 and is only displaced by the
Windows 8 UCX model (`Ucx01000.sys`, `Usbxhci.sys`, `Usbhub3.sys`), which
`docs/usb-xhci-info/win98-wdm.md` already surveys. The `>= 100` / `>= 200`
version gate is versioning machinery built precisely so that older miniports
keep working. Against that: the 5.0 to 5.1 step kept the packet
byte-identical and *still* changed the `GetHciMn` value, which is exactly the
kind of small change that produces a total, silent registration failure. The
project's own record holds that precedent; that is why this is a measurement
and not an assumption.

Two things are true of Vista and Windows 7 x64 regardless of how the six
read, and both belong in the record before anyone builds a guest:

- **Kernel-mode code signing.** Windows XP x64 does not enforce it: an
  unsigned driver installs with a warning and loads. Vista x64 and Windows 7
  x64 do enforce it, and the cross-certificate route that once made
  third-party Windows 7 x64 driver signing possible is no longer available in
  practice. Support for those two therefore means, permanently, a guest
  booted with driver signature enforcement disabled (F8 at boot) or with
  test-signing on. That is tolerable for a VM-only tier and intolerable as a
  silent surprise, so it goes in the release notes beside the tier, not in a
  footnote.
- **The install path is different.** The INF's `LayoutFile=layout.inf`
  route, which is how the media carries no Microsoft file, is a Windows 2000
  and XP mechanism. Vista and later stage a driver package into the driver
  store before installing it, and validate the package's file list more
  strictly. Vista and 7 both ship `usbport.sys`, `usbd.sys`, `usbhub.sys` and
  `usbui.dll` on disk already, so `COPYFLG_NO_OVERWRITE` should skip every
  one of those copies and never need a source - but "should" is doing work in
  that sentence, and it has never been tested. This is an install-time
  question answerable only in a guest, and it is the reason to treat Vista
  and 7 as a second leg rather than free riders on the first.

Windows 7 x64 is also the weakest of the three on merit, and this page should
say so plainly: `docs/usb-xhci-info/win98-wdm.md` records that Intel's xHCI
driver line *begins* at Windows 7. The genuine gap there is the newer PCH and
SoC silicon Intel never shipped a Windows 7 driver for, which plausibly
includes this project's own Skylake and Comet Lake machines but has not been
checked. Windows XP x64 has the same total absence of options that 32-bit XP
does, for the reasons that section already gives.

---

## 6. The decision gate

After section 4, and before any build work:

| Reading | Meaning |
|---|---|
| M1 fails | Stop. No Option A on 64-bit Windows. |
| M3 shows a packet that is not the 316-byte one widened | Stop, and record it. That is a different ABI, not a wider one, and it is a different project. |
| M2 gives a third constant; M3 to M6 all correspond to the x86 record widened | The expected outcome. Proceed; section 8 lists the code changes. |
| M4 shows `StartPA` widened, or any field where the compiler's natural layout disagrees with the binary | Proceed, but the declaration needs explicit padding, and the `#ifdef _WIN64` asserts must carry the *measured* numbers so that they can actually fail. |
| M5 shows a 64-bit adapter | Proceed with a `StartPA` high-DWORD refusal added first, and keep the guest under 4 GB until that refusal has been exercised. |
| Vista / Windows 7 readings differ from 5.2's | Claim Windows XP x64 and Server 2003 x64 only. Record what differs, so the question is not re-derived. |

---

## 7. If the measurements pass: the build path

**One binary, two flavours.** WDK 7.1, `setenv ... x64 WNET`, producing an NT
5.2 amd64 build; `fre` and `chk`, the two shipping flavours the tree already
publishes. `qemu` needs the `__asm` exclusion of section 2 before it compiles
at all, and is worth having for exactly the reason it exists on x86: it is
the flavour that makes a VM leg diagnosable.

**A second toolchain enters the tree, and its terms are the owner's.** The
standing arrangement is that both toolchains live under `tools/`, are used in
place, and install nothing machine-wide (`AGENTS.md`, Quick Reference). WDK
7.1 has an installer, and the pull request runs it into `C:\WinDDK` on a CI
runner. Whatever is chosen, the same recording rule applies as to every other
third-party input: filename, version, SHA-256, URL and the stated licence
limit, in the pattern `docs/references/README.md` sets. Note that the pull
request's `WDK71_ISO_URL` points at a third-party GitHub rehost rather than a
Microsoft host; that is a decision to record explicitly, not a conclusion to
draw here.

**The gates do not currently cover a 64-bit binary, and skipping them is not
an option.**

- *Import gate.* The pull request's build calls `build -ceZ` directly and so
  never runs `scripts\build-driver.cmd`, which means no import gate and no
  INF gate on the produced binary. The Windows 98 export ceiling is
  meaningless on amd64, but the allowlist's purpose - that nothing enters the
  import table unreviewed - is not. This needs an `amd64` dimension in
  `scripts\import-gate\xhci98-imports.allow`, or a sibling allowlist, plus
  NT 5.2 amd64 baselines to resolve against in the way
  `win2k-baselines.expected` does for SP4.
- *usbport import library.* The pull request's `make-usbport-lib-wdk.cmd`
  drops all five verification steps `scripts\make-usbport-lib.cmd` performs -
  the export-manifest check, the `_USBPORT_GetHciMn@0` decoration check, the
  `DLL name : USBPORT.SYS` module check, and the NTAPI link proof - and
  publishes straight over `src\usbport.lib`. The right shape is
  arch-conditional checks inside the existing generator: on amd64 the
  decoration check becomes an undecorated-name check, and the other four
  stand unchanged.
- *INF gate.* `scripts\inf-gate\check-inf.ps1` knows exactly two install
  paths, the undecorated Windows 98 one and `.NTx86`, across 1854 lines. A
  third path is real work there.

**On the INF, there is a cheap route and a pure one.** The pull request adds
`.NTamd64` sections to `src/xhci98.inf` and changes `[Manufacturer]` from
`%Mfg%=XhciModels` to `%Mfg%=XhciModels,NTx86,NTamd64`. That single line is
the one Windows 98's 16-bit engine parses to find its models section, and
whether that engine takes only the first field is exactly the sort of thing
this tree refuses to assume about a file it documents as a silent gate in
both directions. Merging it costs a full re-run of all four existing install
legs - 98, ME, 2000, XP32 - to prove nothing broke. Shipping the x64 build as
a separate package with its own INF, leaving `src/xhci98.inf` byte-identical,
costs a second INF to keep in sync and no re-validation at all. The single
INF is the better end state; the separate package is the one that fits "a
simple build and not too much additional effort". Either way the x64 INF
*must* carry `NTamd64` decorations, because an undecorated models section is
ignored outright by the 64-bit setup engine.

`scripts\package\make-package.ps1` hardcodes a single `i386` payload path
(line 155) and would need the arch dimension whichever route is chosen.

---

## 8. If the measurements pass: the code changes they imply

Every item here is small, and none of them can be written before the
measurement that determines it.

1. The NT 5.2 `USBPORT_GetHciMn` constant, and the third arm of the
   `src/xhci_dispatch.c:4640` refusal. **From M2.**
2. `#ifdef _WIN64` declarations of `USBPORT_RESOURCES`, the registration
   packet and the scatter-gather structures carrying the *measured* offsets,
   with explicit padding wherever the compiler's natural layout disagrees,
   and `XHCI_C_ASSERT`s on those measured numbers rather than on whatever the
   compiler emits. **From M3, M4, M6.**
3. A `StartPA` high-DWORD refusal, matching the one the scatter-gather path
   already makes. **From M5.**
4. The `__asm` stack-delta check excluded under `_WIN64`.
5. `src/xhci_compat.h`'s `ULONG_PTR` typedef guarded for 64-bit hosts.
6. Design record 04's common-buffer arithmetic re-run against the amd64
   `sizeof`s, and the result stated rather than assumed to carry.
7. `test/test_packet.c` extended to compile the header for amd64, so the
   layout asserts are checked by `test\run-host-tests.cmd` on the build host
   rather than only inside a driver build.

Items 4 and 5 are the only two that could honestly be taken today, and
neither is worth a commit on its own.

---

## 9. If the measurements pass: the VM leg

Modelled on Phase 19, which is the template for adding a VM-only target and
fits without modification.

- One guest, `scripts\setup-qemu-winxp64.ps1`, alongside the existing
  Windows XP, Windows 2000 and Windows ME launchers, with RAM capped below
  4 GB pending M5.
- The checkpoint the other VM tiers use: the package installs on an
  xHCI-only guest, the driver registers (`USBPORT_GetHciMn` and
  `USBPORT_RegisterUSBPortDriver status=0` both logged), the controller
  starts and passes its No Op self-test, the root-hub callbacks answer, and a
  HID mouse, a mass-storage device and a composite audio device bind. Then
  the Device Manager disable, enable, remove and rescan sequence.
- The two NT install-path fixes 32-bit XP needed are already in the INF since
  `1.0.1.0` - the operating system supplying `usbport.sys`, and the
  `DisableSelectiveSuspend` value - and NT 5.2's usbport is XP-lineage, so
  both should apply unchanged. "Should" is again doing work; the guest is
  what settles it.
- **The tier this would earn is the Windows ME and 32-bit XP tier**:
  supported in virtual machines, never observed on real hardware, no
  checkpoint tax on any phase, accommodated where the change is small and
  low-risk and never at a primary target's expense. Not a primary target.
  The wording those two tiers already carry in `AGENTS.md` and
  `docs/contributing/build-and-test.md` is the wording to reuse.

---

## 10. What this costs, and what it ends

The static pass of section 4 is the cheap part: extraction is minutes, and
the six measurements are an afternoon each at worst, with M4 and M6 the only
ones that are real transcription work. It needs no VM, no toolchain and no
code change, and it is the only part that can say *no*.

The build path of section 7 is where the cost sits, and most of it is gates
and packaging rather than driver code: a second toolchain in the tree with
its provenance recorded, an amd64 dimension in the import gate with NT 5.2
baselines behind it, an arch-conditional import-library generator, an INF
decision with either a gate extension or a second INF behind it, and a
packager that can stage two architectures.

And one property ends, which should be stated rather than discovered: **the
driver stops being one binary.** "A single `xhci98.sys` must install and work
on either" is the first paragraph of `AGENTS.md`, and it is a claim about the
two primary targets that a 64-bit build does not weaken - but every document
that says "one binary" would need to say which targets it means, and the
release download would carry two.

---

## 11. The owner's decisions

1. Whether a 64-bit tier is wanted at all, if section 4 reads favourably.
2. Whether it is Windows XP x64 and Server 2003 x64 only, or also Vista x64
   and Windows 7 x64, given that those two require a guest with driver
   signature enforcement disabled, permanently and by design.
3. One INF with a third install path and a four-leg re-validation, or a
   separate x64 package leaving `src/xhci98.inf` untouched.
4. Whether WDK 7.1 enters the tree as a third toolchain, on what terms, and
   from which source - the pull request's URL is a third-party rehost.
5. Whether this project adopts GitHub Actions at all. There are no workflows
   today, and the build's defining property is that its toolchain lives in
   the repository and installs nothing; a CI job that downloads and installs
   an SDK is a different arrangement, not an extension of that one.
6. Whether the x86 half of pull request 6 is declined, as section 1
   recommends.

---

## Sources

- Pull request 6, `WDK 7.1` (GeorgeK1ng): the CI job, the WDK build wrapper,
  the `.NTamd64` INF sections, and the `#ifdef _WIN64` assertion block this
  page responds to.
- `docs/usb-xhci-info/usbport-miniport-abi.md`: the x86 ABI record every
  measurement here is the amd64 counterpart of, and the disassembly method.
- `docs/usb-xhci-info/usbport-miniport-interface.md` section 5, "Extracting
  the record from the NUSB package": the extraction pattern section 3
  follows.
- `docs/usb-xhci-info/win98-wdm.md`, "What about Windows XP?": the tier
  wording, the `GetHciMn` lineage difference, and the vendor xHCI driver
  survey.
- `docs/contributing/roadmap.md` Phase 19: the template for adding a
  VM-supported target.
- `docs/contributing/legal-provenance.md` sections 1 and 4: routine
  unpacking, and the static/runtime tagging every fact here needs.
- `docs/contributing/design/04-controller-common-buffer.md`: the arithmetic
  item 6 of section 8 would re-run.
