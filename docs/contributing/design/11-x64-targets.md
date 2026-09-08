# 64-bit targets: Windows XP x64 and Server 2003 x64

Design record for roadmap Phase 21. It was
`docs/future-plans/x64-targets.md` from 2026-09-08 until later the same day,
when the first three measurements it proposed were taken and passed and the
work was scheduled; the future-plans page is gone and this record replaces
it. Written in response to pull request 6 (`WDK 7.1`, GeorgeK1ng), which adds
a GitHub Actions job building the driver with WDK 7.1 for Windows XP, Server
2003, Vista and Windows 7 in both x86 and x64, and to the owner's narrowing
of it to 64-bit guests.

No 64-bit binary of this driver has been produced yet. What has been produced
is the static ABI evidence that one could work, and that evidence is section
5.

The project's rule is that miniport ABI facts are derived from the shipping
binaries and never inferred (`AGENTS.md`, "What NOT to Do"). Everything in
section 5 marked **read** was read from a disassembly; everything marked
**open** is unmeasured and must not be coded against until it is.

---

## 1. The narrowing, and why it matters to the plan

The pull request builds seven configurations. Two observations collapse that
to one binary and one guest.

**Windows XP Professional x64 and Windows Server 2003 x64 are the same
operating system.** Both are NT 5.2.3790; the x64 edition of XP is the
Server 2003 x64 kernel with a client licence and shell. They are one target
under two names, not two targets, and one binary built for NT 5.2 amd64
serves both by identity rather than by compatibility. The extracted media
confirms it from Microsoft's own side: the WDK ships `lib\wxp\i386` and no
`lib\wxp\amd64`, so there is no XP-target x64 library set to build against -
NT 5.2 (`WNET`) is the only route to a 64-bit XP driver.

**The x86 half of the pull request should not be taken.** 32-bit Windows XP
is already a supported VM target (`AGENTS.md`, Quick Reference) and is served
by the same MSVC 6.0 / Windows 2000 DDK binary as the two primary targets. A
second x86 toolchain producing a *different* x86 binary subtracts from the
one-binary property `docs/using/release-notes.md` states, and buys nothing.

What remains is one question with two halves: can this driver register with a
64-bit `usbport.sys` at all, and if so, does one NT 5.2 amd64 binary also
serve Vista x64 and Windows 7 x64? Section 5 answers the first for the three
cheapest measurements and leaves three open. Section 6 covers the second.

---

## 2. What the driver assumes today that 64 bits touches

None of these is a defect. Each is a decision the tree made deliberately for
two 32-bit primary targets, recorded where it was made, and each is a place a
64-bit build changes meaning.

| Where | What it assumes | What 64 bits does to it |
|---|---|---|
| `src/xhci_usbport.h`, `USBPORT_RESOURCES.StartPA` | The common-buffer physical address is a `ULONG` at offset `0x2C`, "high DWORD is 0" | If the real amd64 structure holds a full 8-byte `PHYSICAL_ADDRESS` there, every field after it is misread. **M4, open.** |
| `src/xhci_usbport.h` header, "binary-confirmed facts" | Registration copies `0x13C` (316) bytes for a `Version >= 200` miniport, and usbport writes 16 service pointers at `0xE4`-`0x120` | Both are x86 numbers. The amd64 counterparts are `0x250` and `0x1A0`-`0x218`; **M3 read them**, and both are the exact widening of the x86 pair. |
| `src/xhci_dispatch.c:4640` | `USBPORT_GetHciMn` returns one of exactly two values, and any third is refused with `STATUS_UNSUCCESSFUL` | NT 5.2 amd64 returns `0x10000001`, which is already one of the two. **M2 read it; no change is needed.** |
| `src/xhci_dispatch.c:4670`, `:4678` | Inline `__asm mov espBefore, esp` brackets the registration call under `XHCI_DBG_TRACE` | MSVC has no inline assembler on x64, so the `qemu` flavour - the one a VM leg most wants - does not compile until this is excluded under `_WIN64`. It is also unnecessary there: x64 has one calling convention, so the stack-delta check has nothing to catch. |
| `src/xhci_compat.h:34` | `ULONG_PTR` is `typedef unsigned long` | Wrong under `_WIN64`. Harmless while the host tests build x86; a guard is needed before any 64-bit compile of the DDK-free core. |
| `src/xhci_xfer.c:542`, `src/xhci_probe.c:162` | A scatter-gather element with `SgPhysicalAddressHi != 0` is refused | Already right, and why a >4 GB transfer address is a clean failure rather than silent corruption. It does not cover the controller common buffer, which arrives through `StartPA`. **M5, open.** |
| `docs/contributing/design/04-controller-common-buffer.md` | The fixed 400 KiB block's arithmetic, derived from the sizes of the driver's own structures | Several of those structures hold pointers and grow on amd64. The derivation is arithmetic over `sizeof`, so it does not silently break, but it has to be re-run and the record re-stated rather than assumed to carry. |
| `AGENTS.md`, "Language and arithmetic" | No 64-bit arithmetic, because Windows 98's kernel may lack the `_alldiv`-style compiler helpers | This constraint exists for Windows 98 and does not bind an amd64 binary that never runs there. It must not be relaxed in shared code for that reason: the shared source still has to compile for Windows 98. |

---

## 3. The material, and how it is recorded

Three files off the Windows XP Professional x64 SP2 media, plus its host
controller INF, are the whole input for section 5. They live git-ignored in
`tools/winxp64-extracted/`, extracted 2026-09-08:

| File | Size | Version | SHA-256 |
|---|---|---|---|
| `usbport.sys` | 212,480 | 5.2.3790.3959 (`srv03_sp2_rtm.070216-1710`) | `6fc83f49afd98c4863159f70a1f6ca53caf3038b0a479efbde214ae30ec05e1d` |
| `usbehci.sys` | 44,160 | 5.2.3790.3959 (`srv03_sp2_rtm.070216-1710`) | `657daf4a3dcdf10e08ca6dcc7b62bf24c437ff4708f18ca0a47f7e8897e83d9a` |
| `usbhub.sys` | 102,400 | 5.2.3790.3959 (`srv03_sp2_rtm.070216-1710`) | `92b1744eb8ffb6bd5c8502508825c8d88f94ef76ed119937a4a791d2ea030198` |
| `usbport.inf` | 24,632 | - | `d9479abd2b09c6443106b2f608bc817b7e6db93e615aa7d2919b87d8922715a5` |

`usbehci.sys` is the more valuable of the two drivers: as the shipping amd64
miniport it *fills* the registration packet and *reads* `USBPORT_RESOURCES`,
so its stores and loads give the field offsets directly. That is the method
the 32-bit record was built with. `usbport.inf` is Microsoft's own host
controller INF for the x64 media and is the model for the `.NTamd64` install
path question in section 8.

Extraction, host-side, no VM. The x64 media's `AMD64` directory is the
equivalent of `I386` on the 32-bit media, and its files are compressed:

```bat
7z x "Win XP SP2 VL x64.iso" -oxp64-media
expand xp64-media\AMD64\USBPORT.SY_ tools\winxp64-extracted\usbport.sys
expand xp64-media\AMD64\USBEHCI.SY_ tools\winxp64-extracted\usbehci.sys
```

Routine unpacking of shipped media is the recorded adjacent case in
`docs/contributing/legal-provenance.md` section 1 and needs no separate
decision. Two rules bind what happens next, and neither is optional:

- **The binaries are not tracked, and neither are any disassembly listings
  taken from them**, exactly as `tools/nusb-extracted/` and
  `tools/win2ksp4-extracted/` are not. What gets committed is the *facts*, in
  a form a fresh clone can re-derive: the file version, the SHA-256, the RVA,
  the instruction bytes, and the value read. Section 5 is written that way.
- **Every fact here is tagged `static`** - read from a disassembly, nothing
  executed - with its row in `legal-provenance.md` section 4. A reading is
  not upgraded to `runtime` because a VM later happens to boot.

### The tool, which is not the one the 32-bit record used

**MSVC 6.0's `dumpbin` cannot disassemble amd64 code**, so
`tools\MSVC600\VC98\Bin\DUMPBIN.EXE /disasm`, which
`docs/usb-xhci-info/usbport-miniport-abi.md` names, does not carry over. Nor
does WDK 7.1's `link /dump /disasm`: it needs `msdis160.dll`, a Visual Studio
component that is not anywhere on the WDK media, and without it the command
reports `LINK : warning LNK4195: unable to load msdis160.dll` and prints no
code.

What works, and what section 5 was read with, is `cdb.exe` from the
Debugging Tools for Windows on the same WDK media (6.12.0002.633, extracted
to `tools/WinDDK71/Debuggers/`). It opens a raw PE statically, resolves
export symbols with no PDB, and disassembles:

```bat
tools\WinDDK71\Debuggers\cdb.exe -z tools\winxp64-extracted\usbport.sys ^
    -c "uf usbport!USBPORT_RegisterUSBPortDriver; q"
```

The image loads at its preferred base `0x10000`, so a listed address minus
`0x10000` is the RVA quoted below. `link /dump /exports` and `/headers` do
work on amd64 images and were used for M1.

---

## 4. The toolchain

WDK 7.1 (`GRMWDK_EN_7600_1.ISO`, 649,877,504 bytes, SHA-256
`5edc723b50ea28a070cad361dd0927df402b7a861a036bbcf11d27ebba77657d`) is
extracted to `tools/WinDDK71/`, git-ignored, on the same terms as
`tools/MSVC600` and `tools/ntddk`: sixteen payload MSIs plus the amd64
debuggers unpacked with `msiexec /a`, which lays out files and registers
nothing. Verified 2026-09-08: no WDK or debugger entry appears in installed
programs and no `C:\WinDDK` exists.

```
call tools\WinDDK71\bin\setenv.bat <ddkroot> fre x64 WNET no_oacr
  BASEDIR=...\tools\WinDDK71   BUILD_ALT_DIR=fre_wnet_AMD64
  _BUILDARCH=AMD64             DDK_TARGET_OS=WinNET
  cl.exe -> Microsoft (R) C/C++ Optimizing Compiler Version 15.00.30729.207 for x64
```

`BUILD_ALT_DIR=fre_wnet_AMD64` is what setenv sets, and is why the pull
request has to override it to `fre`: `src/sources` hard-errors on any value
that is not `fre`, `chk` or `chk_qemu`. Whether this toolchain stays, and on
what terms it is fetched, is decision 4 in section 12.

---

## 5. The six measurements

M1 through M3 were read on 2026-09-08 and all three pass. M4 through M6 are
the expensive transcription work and are open.

### M1 - the two private exports · **read, pass**

`link /dump /headers` gives `8664 machine (x64)`. `link /dump /exports` gives
three functions, three names, ordinal base 1, time date stamp `45D6899F`:

| Ordinal | RVA | Name |
|---|---|---|
| 1 | `0x21A00` | `DllUnload` |
| 2 | `0x21A60` | `USBPORT_GetHciMn` |
| 3 | `0x22030` | `USBPORT_RegisterUSBPortDriver` |

The same three names at the same three ordinals as all three 32-bit
lineages, undecorated. amd64 has no `__stdcall` name decoration, so the
`_Name@N` symbols `scripts\make-usbport-lib.cmd` checks for do not exist
here - that check is x86-only by nature, not by oversight, and section 8 says
what replaces it.

### M2 - the `USBPORT_GetHciMn` lineage value · **read, pass**

```
usbport!USBPORT_GetHciMn:                      ; RVA 0x21A60
  b8 01 00 00 10    mov     eax,10000001h
  c3                ret
```

**`0x10000001`** - the same value as Windows XP 5.1.2600, which
`src/xhci_usbport.h` already defines as `USBPORT_HCI_MN_XP` and
`src/xhci_dispatch.c:4640` already accepts. NT 5.2 is not a fourth constant.

This was the sharpest edge in the plan when it was written: the driver
refuses to register on an unknown lineage, and the failure presents as a
yellow bang indistinguishable from a bad INF. It is simply gone, and no code
change follows from it.

### M3 - the version gate and the copied packet size · **read, pass**

`USBPORT_RegisterUSBPortDriver` at RVA `0x22030`. The arguments settle into
`rbx` = DriverObject, `esi` = Version, `rbp` = RegistrationPacket.

The gate, at RVA `0x22176`, and its refusal at `0x221D8`:

```
  83 fe 64          cmp     esi,64h              ; Version < 100
  ...
  73 14             jae     +0x1bc
  b8 01 00 00 c0    mov     eax,0C0000001h       ; STATUS_UNSUCCESSFUL
```

The size selection, at RVA `0x221F3`:

```
  ba 50 02 00 00    mov     edx,250h             ; 592
  41 b8 30 02 00 00 mov     r8d,230h             ; 560
  81 fe c8 00 00 00 cmp     esi,0C8h             ; Version >= 200 ?
  44 0f 43 c2       cmovae  r8d,edx
  48 8d 4f 28       lea     rcx,[rdi+28h]        ; destination
  48 8b d5          mov     rdx,rbp              ; source = caller's packet
```

followed by the copy call at RVA `0x222E8`. So **`Version >= 200` copies
`0x250` (592) bytes and `100 <= Version < 200` copies `0x230` (560)**, into a
`0x278`-byte `'usbp'`-tagged allocation made at RVA `0x2214B` and zeroed
before the copy. Allocation failure returns `0xC000009A`
(`STATUS_INSUFFICIENT_RESOURCES`) at RVA `0x221E2`; success returns
`STATUS_SUCCESS`.

**Both sizes are the exact 64-bit widening of the x86 pair**, and that is the
most important thing this measurement establishes. The packet is ten leading
`ULONG` data fields (`0x00`-`0x27`, 40 bytes) followed by function pointers:

| | x86 | amd64 | identity |
|---|---|---|---|
| USB2 (`Version >= 200`) | `0x13C` = 316 | `0x250` = 592 | 40 + 69 x 4 → 40 + 69 x 8 |
| USB1 (`100 <= V < 200`) | `0x12C` = 300 | `0x230` = 560 | 40 + 65 x 4 → 40 + 65 x 8 |

This is not an interface that happens to resemble the x86 one. It is the same
C declaration compiled for a wider pointer, which is what NT 5.2 amd64
`usbport.sys` is. That is what makes M4 and M6 transcription work rather than
open questions.

### M6 (partial) - the service-pointer block · **read**

Taken for free while reading M3. Before copying, usbport writes 16 pointers
into the *caller's* packet at these offsets, at RVAs `0x221FE` through
`0x222E1`:

```
0x1A0 0x1A8 0x1B0 0x1B8 0x1C0 0x1C8 0x1D0 0x1D8
0x1E0 0x1E8 0x1F0 0x1F8 0x200 0x208 0x210 0x218
```

Sixteen, exactly as on x86, and the widening map `f(X) = 0x28 + (X - 0x28) *
2` carries the x86 block `0xE4`-`0x120` onto `0x1A0`-`0x218` precisely. The
behaviour is unchanged too: usbport writes these into the miniport's own
packet and touches no other field before copying it.

Which service is at which of the sixteen slots is not yet read; that is the
rest of M6.

### M4 - the `USBPORT_RESOURCES` layout · **open**

The offset and width of every field, and in particular whether `StartPA` is
4 bytes or 8. Read the amd64 `usbehci.sys`'s `StartController` callback,
which is handed a pointer to this structure and loads from it: each load's
displacement is a field offset and each load's width is a field width.

`ResourceBase` (the mapped BAR0 virtual address), `StartVA` and `StartPA` are
the three fields the driver's start path depends on, and
`XhciCheckResourceBase(ULONG_PTR startVA, ULONG startPA)` takes the last as a
`ULONG`. A widened `StartPA` shifts everything after it.

Record it as a second `USBPORT_RESOURCES` table in
`docs/usb-xhci-info/usbport-miniport-abi.md`, explicitly labelled amd64 and
kept separate from the x86 one.

### M5 - the DMA adapter width · **open**

Does amd64 usbport create its DMA adapter with `Dma32BitAddresses` set, as
the 32-bit builds do? Read the `DEVICE_DESCRIPTION` its start path fills
before `IoGetDmaAdapter`.

`src/xhci_usbport.h`'s scatter-gather comment records the measured reason the
high DWORD of a transfer address is always zero on the 32-bit targets: *the
adapter is created 32-bit, not because any element writer forces it*. On a
64-bit guest with more than 4 GB of RAM, if the adapter is created 64-bit,
the common buffer and the transfer buffers can land above 4 GB. The transfer
path already refuses that cleanly; the controller common buffer, which
arrives through `StartPA`, has no equivalent guard.

This is the largest remaining *runtime* unknown, and the one that could
produce a driver that works with 2 GB of guest RAM and fails intermittently
with 8. **Interim mitigation: cap the guest below 4 GB**, which makes the
question moot for a first VM leg without deciding it.

---

## 6. Vista and Windows 7 x64

Every measurement above is a static read of two files, so repeating it on
Vista x64 and Windows 7 x64 costs extraction time and nothing else, and
answers whether one NT 5.2 amd64 binary also serves them.

M3's outcome improves the prior but does not settle it. Vista and Windows 7
still ship `usbehci.sys`, `usbohci.sys` and `usbuhci.sys` as usbport
miniports; `usbport.sys` survives to 6.1 and is only displaced by the
Windows 8 UCX model (`Ucx01000.sys`, `Usbxhci.sys`, `Usbhub3.sys`), which
`docs/usb-xhci-info/win98-wdm.md` surveys. Against that, the 5.0 to 5.1 step
kept the packet byte-identical and *still* changed the `GetHciMn` value.
That precedent is the reason this stays a measurement.

Two things are true of Vista and Windows 7 x64 regardless, and both belong in
the record before anyone builds a guest:

- **Kernel-mode code signing.** Windows XP x64 does not enforce it: an
  unsigned driver installs with a warning and loads, which is why it is the
  target this phase takes. Vista x64 and Windows 7 x64 do enforce it, and the
  cross-certificate route that once made third-party Windows 7 x64 signing
  possible is no longer available in practice. Supporting those two means,
  permanently, a guest booted with driver signature enforcement disabled (F8)
  or with test-signing on. That goes in the release notes beside the tier,
  not in a footnote.
- **The install path is different.** The INF's `LayoutFile=layout.inf` route,
  which is how the media carries no Microsoft file, is a Windows 2000 and XP
  mechanism. Vista and later stage a driver package into the driver store
  before installing it and validate its file list more strictly. Both ship
  `usbport.sys`, `usbd.sys`, `usbhub.sys` and `usbui.dll` on disk already, so
  `COPYFLG_NO_OVERWRITE` should skip every one of those copies and never need
  a source - but "should" is doing work in that sentence and it has never been
  tested. It is answerable only in a guest, and it is why Vista and 7 are a
  second leg rather than free riders on the first.

Windows 7 x64 is also the weakest of the three on merit:
`docs/usb-xhci-info/win98-wdm.md` records that Intel's xHCI driver line
*begins* at Windows 7. The genuine gap there is the newer PCH and SoC silicon
Intel never shipped a Windows 7 driver for, which plausibly includes this
project's own Skylake and Comet Lake machines but has not been checked.
Windows XP x64 has the same total absence of options that 32-bit XP does.

---

## 7. The decision gate

The gate as written before any measurement, with what actually happened:

| Reading | Meaning | Outcome |
|---|---|---|
| M1 fails | Stop. No Option A on 64-bit Windows. | Did not happen; three exports, same ordinals |
| M3 shows a packet that is not the 316-byte one widened | Stop and record it: a different ABI, not a wider one, and a different project | Did not happen; `0x250` and `0x230` are the exact widening |
| M2 gives a third constant; M3-M6 correspond to the x86 record widened | Proceed | Better than this: M2 gave a value the driver already accepts |
| M4 shows `StartPA` widened, or any field where the compiler's natural layout disagrees with the binary | Proceed, but the declaration needs explicit padding and the asserts must carry measured numbers | **Already true - see section 9, item 2** |
| M5 shows a 64-bit adapter | Proceed with a `StartPA` high-DWORD refusal added first, and keep the guest under 4 GB until it has been exercised | Open |
| Vista / Windows 7 differ from 5.2 | Claim Windows XP x64 and Server 2003 x64 only, and record what differs | Open |

Nothing measured so far argues against Windows XP x64 and Server 2003 x64.

---

## 8. The build path

**One binary, two flavours.** WDK 7.1, `setenv ... x64 WNET`, producing an NT
5.2 amd64 build; `fre` and `chk`, the two shipping flavours. `qemu` needs the
`__asm` exclusion of section 2 before it compiles at all, and is worth having
for the reason it exists on x86: it is the flavour that makes a VM leg
diagnosable.

**The gates do not cover a 64-bit binary, and skipping them is not an
option.**

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
  decoration check becomes an undecorated-name check, M1 supplies the fourth
  lineage for the export manifest, and the other four steps stand unchanged.
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
ignored outright by the 64-bit setup engine, and
`tools/winxp64-extracted/usbport.inf` is Microsoft's own model for it.

`scripts\package\make-package.ps1` hardcodes a single `i386` payload path
(line 155) and needs the arch dimension whichever route is chosen.

---

## 9. The code changes

1. ~~The NT 5.2 `USBPORT_GetHciMn` constant and a third arm of the
   `src/xhci_dispatch.c:4640` refusal~~ - **not needed. M2 read
   `0x10000001`, which the driver already accepts.**
2. `#ifdef _WIN64` declarations of `USBPORT_RESOURCES`, the registration
   packet and the scatter-gather structures carrying the *measured* offsets,
   with explicit padding wherever the compiler's natural layout disagrees.
   **M3 already shows one disagreement**: pull request 6 asserts
   `sizeof(USBPORT_REGISTRATION_PACKET) == 0x248` (584) under `_WIN64`, and
   usbport copies `0x250` (592). That is eight bytes past the end of
   `XhciRegPacket`, a static global, read into fields usbport believes are
   part of the packet. The assertions in that pull request could not have
   caught it, because they assert the compiler against itself; only the
   binary shows it. The asserts must carry measured numbers so that they can
   fail.
3. A `StartPA` high-DWORD refusal matching the one the scatter-gather path
   already makes. **From M5.**
4. The `__asm` stack-delta check excluded under `_WIN64`.
5. `src/xhci_compat.h`'s `ULONG_PTR` typedef guarded for 64-bit hosts.
6. Design record 04's common-buffer arithmetic re-run against the amd64
   `sizeof`s, and the result stated rather than assumed to carry.
7. `test/test_packet.c` extended to compile the header for amd64, so the
   layout asserts are checked by `test\run-host-tests.cmd` on the build host
   rather than only inside a driver build.

Items 4 and 5 are the only two that could be taken before M4 and M6 land.

---

## 10. The VM leg

Modelled on Phase 19, which is the template for adding a VM-only target.

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
  `1.0.1.0` - the OS supplying `usbport.sys`, and the
  `DisableSelectiveSuspend` value - and NT 5.2's usbport is XP-lineage by
  M2's reading, so both should apply unchanged. "Should" is doing work again;
  the guest settles it.
- **The tier this earns is the Windows ME and 32-bit XP tier**: supported in
  virtual machines, never observed on real hardware, no checkpoint tax on any
  phase, accommodated where the change is small and low-risk and never at a
  primary target's expense. Not a primary target. The wording those two tiers
  carry in `AGENTS.md` and `docs/contributing/build-and-test.md` is the
  wording to reuse.

What Phase 19 predicts about where the trouble comes from is worth stating in
advance, because it is the calibration this record rests on. For 32-bit XP the
ABI was fine and the static pass was right, and the guest then produced three
problems in an afternoon: Code 39 from a missing `usbport.sys`, a
thirty-second idle suspend that made hot-plug invisible, and one real
enumeration defect (issue 4). None was an ABI problem and two were
install-path. Expect the same shape here.

---

## 11. What this costs, and what it ends

The static pass is the cheap part and half of it is done. M4 and M6 are the
only real transcription work left, and they need no VM, no toolchain beyond
what is now in `tools/`, and no code change.

The build path is where the cost sits, and most of it is gates and packaging
rather than driver code: an amd64 dimension in the import gate with NT 5.2
baselines behind it, an arch-conditional import-library generator, an INF
decision with either a gate extension or a second INF behind it, and a
packager that can stage two architectures. On top of that sits one thing no
measurement can shorten: **this source has never been compiled for x64 by any
compiler**, and WDK 7.1's `cl` 15.00 is also the first non-MSVC-6 compiler to
touch it. DDK builds run warnings as errors.

And one property ends, which should be stated rather than discovered: **the
driver stops being one binary.** "A single `xhci98.sys` must install and work
on either" is the first paragraph of `AGENTS.md`, and it is a claim about the
two primary targets that a 64-bit build does not weaken - but every document
that says "one binary" needs to say which targets it means, and the release
download would carry two.

---

## 12. The owner's decisions

1. Whether the tier is Windows XP x64 and Server 2003 x64 only, or also
   Vista x64 and Windows 7 x64, given that those two require a guest with
   driver signature enforcement disabled, permanently and by design.
2. One INF with a third install path and a four-leg re-validation, or a
   separate x64 package leaving `src/xhci98.inf` untouched.
3. Whether WDK 7.1 stays in the tree as a third toolchain, and from which
   source - the pull request's URL is a third-party GitHub rehost rather
   than a Microsoft host. The copy now in `tools/WinDDK71/` came from the
   owner's own media (section 4).
4. Whether this project adopts GitHub Actions at all. There are no workflows
   today, and the build's defining property is that its toolchain lives in
   the repository and installs nothing; a CI job that downloads and installs
   an SDK is a different arrangement, not an extension of that one.
5. Whether the x86 half of pull request 6 is declined, as section 1
   recommends.

---

## Sources

- Pull request 6, `WDK 7.1` (GeorgeK1ng): the CI job, the WDK build wrapper,
  the `.NTamd64` INF sections, and the `#ifdef _WIN64` assertion block
  section 9 item 2 answers.
- `docs/usb-xhci-info/usbport-miniport-abi.md`: the x86 ABI record every
  measurement here is the amd64 counterpart of, and where sections 5's
  results are transcribed.
- `docs/usb-xhci-info/usbport-miniport-interface.md` section 5, "Extracting
  the record from the NUSB package": the extraction pattern section 3
  follows.
- `docs/usb-xhci-info/win98-wdm.md`, "What about Windows XP?": the tier
  wording, the `GetHciMn` lineage difference, and the vendor xHCI driver
  survey.
- `docs/contributing/roadmap.md` Phase 19 (the template for adding a
  VM-supported target) and Phase 21 (this record's tasks).
- `docs/contributing/legal-provenance.md` sections 1 and 4: routine
  unpacking, and the static tagging every fact here carries.
- `docs/contributing/design/04-controller-common-buffer.md`: the arithmetic
  section 9 item 6 re-runs.
