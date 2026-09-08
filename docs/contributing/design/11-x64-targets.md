# 64-bit targets: Windows XP x64 and Server 2003 x64

Design record for roadmap Phase 21. It was
`docs/future-plans/x64-targets.md` from 2026-09-08 until later the same day,
when the first three measurements it proposed were taken and passed and the
work was scheduled; the future-plans page is gone and this record replaces
it. It was written after the owner asked what a WDK 7.1 build would take -
one that would build the driver for Windows XP, Server 2003, Vista and
Windows 7 in both x86 and x64 - and then narrowed the question to 64-bit
guests.

No 64-bit binary of this driver has been produced yet. What has been produced
is the static ABI evidence that one could work, and that evidence is section
5.

The project's rule is that miniport ABI facts are derived from the shipping
binaries and never inferred (`AGENTS.md`, "What NOT to Do"). Everything in
section 5 marked **read** was read from a disassembly; everything marked
**open** is unmeasured and must not be coded against until it is.

---

## 1. The narrowing, and why it matters to the plan

That scope is a build matrix across four operating systems and two
architectures. Two observations collapse it to one binary and one guest.

**Windows XP Professional x64 and Windows Server 2003 x64 are the same
operating system.** Both are NT 5.2.3790; the x64 edition of XP is the
Server 2003 x64 kernel with a client licence and shell. They are one target
under two names, not two targets, and one binary built for NT 5.2 amd64
serves both by identity rather than by compatibility. The extracted media
confirms it from Microsoft's own side: the WDK ships `lib\wxp\i386` and no
`lib\wxp\amd64`, so there is no XP-target x64 library set to build against -
NT 5.2 (`WNET`) is the only route to a 64-bit XP driver.

**The x86 half should not be taken.** 32-bit Windows XP
is already a supported VM target (`AGENTS.md`, Quick Reference) and is served
by the same MSVC 6.0 / Windows 2000 DDK binary as the two primary targets. A
second x86 toolchain producing a *different* x86 binary subtracts from the
one-binary property `docs/using/release-notes.md` states, and buys nothing.

What remains is one question with two halves: can this driver register with a
64-bit `usbport.sys` at all, and if so, does one NT 5.2 amd64 binary also
serve Vista x64 and Windows 7 x64? Section 5 answers the first: all six
measurements taken, all six pass. Section 6 answers the second the same way,
with three recorded differences that a Version-200 miniport never reaches.

---

## 2. What the driver assumes today that 64 bits touches

None of these is a defect. Each is a decision the tree made deliberately for
two 32-bit primary targets, recorded where it was made, and each is a place a
64-bit build changes meaning.

| Where | What it assumes | What 64 bits does to it |
|---|---|---|
| `src/xhci_usbport.h`, `USBPORT_RESOURCES.StartPA` | The common-buffer physical address is a `ULONG` at offset `0x2C`, "high DWORD is 0" | If the real amd64 structure holds a full 8-byte `PHYSICAL_ADDRESS` there, every field after it is misread. **M4 read it: `StartPA` is a `ULONG` at `0x40`, unwidened, and nothing shifts.** |
| `src/xhci_usbport.h` header, "binary-confirmed facts" | Registration copies `0x13C` (316) bytes for a `Version >= 200` miniport, and usbport writes 16 service pointers at `0xE4`-`0x120` | Both are x86 numbers. The amd64 counterparts are `0x250` and `0x1A0`-`0x218`; **M3 read them**, and both are the exact widening of the x86 pair. |
| `src/xhci_dispatch.c:4640` | `USBPORT_GetHciMn` returns one of exactly two values, and any third is refused with `STATUS_UNSUCCESSFUL` | NT 5.2 amd64 returns `0x10000001`, which is already one of the two. **M2 read it; no change is needed.** |
| `src/xhci_dispatch.c:4670`, `:4678` | Inline `__asm mov espBefore, esp` brackets the registration call under `XHCI_DBG_TRACE` | MSVC has no inline assembler on x64, so the `qemu` flavour - the one a VM leg most wants - does not compile until this is excluded under `_WIN64`. It is also unnecessary there: x64 has one calling convention, so the stack-delta check has nothing to catch. |
| `src/xhci_compat.h:34` | `ULONG_PTR` is `typedef unsigned long` | Wrong under `_WIN64`. Harmless while the host tests build x86; a guard is needed before any 64-bit compile of the DDK-free core. |
| `src/xhci_xfer.c:542`, `src/xhci_probe.c:162` | A scatter-gather element with `SgPhysicalAddressHi != 0` is refused | Already right, and why a >4 GB transfer address is a clean failure rather than silent corruption. It does not cover the controller common buffer, which arrives through `StartPA`. **M5 read the adapter: still created 32-bit, so nothing lands above 4 GB.** |
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

### The Vista and Windows 7 material (tasks 21.7 and 22.1)

Eight more files, extracted 2026-09-09 from the project owner's own Windows
Vista SP2 and Windows 7 Professional SP1 media, in one pass covering both
architectures because the marginal cost of the second one is the disassembly
alone. Vista's `install.wim` carries seven editions and Windows 7's one; the
files below were taken from image 1 (`Windows Vista Business` 6.0.6002.18005;
`Windows 7 Professional` 6.1.7601.17514) and `usbport.sys` was checked to be
byte-identical in another Vista edition's image, which is what a WIM's shared
resources imply and worth one hash to confirm.

| Directory | File | Size | Version | SHA-256 |
|---|---|---|---|---|
| `tools/vista-x86-extracted/` | `usbport.sys` | 226,304 | 6.0.6002.18005 (`lh_sp2rtm.090410-1830`) | `c8b660e4afaf8a070e758f98f77f741b5a63c9772c550becab798fc45e5a7522` |
| | `usbehci.sys` | 39,936 | same | `eb441d3b93965cd927e0c181031ad1082f59f9885bf35cabfdca08c6c76b0daf` |
| | `ntoskrnl.exe` | 3,549,672 | same | `45c9cb0604b9da7ba15e8824ff4b446064ab40ffa8412679a49ec16eaf36f7fc` |
| | `hal.dll` | 177,128 | same (internal name `halmacpi.dll`) | `b6d9de353b13e61eaccdc41eb73043919b7f3cb232756233f0d732071023afe8` |
| `tools/vista-x64-extracted/` | `usbport.sys` | 259,584 | 6.0.6002.18005 | `a8efc84cd937906c87146ef0d3fa2326c16d75c59a12cc6e6fe05394313df5e1` |
| | `usbehci.sys` | 49,664 | same | `0d158916645f782bdeff0be708ca7f4d77f762b9be6263b6608c11abb5f4ff9f` |
| `tools/win7-x86-extracted/` | `usbport.sys` | 284,672 | 6.1.7601.17514 (`win7sp1_rtm.101119-1850`) | `abec8cc91704d13f11bfaa10c33de046653a40981e3687d601c74df0b19bcb88` |
| | `usbehci.sys` | 42,496 | same | `d60698eaa8a085214d5945818b0863976cf116ebe523046c344af4e9392fdf80` |
| | `ntoskrnl.exe` | 3,911,040 | same | `d74e389d1a3ed4c79ffc7c0af1c092102f13e67168ab66a9296cdde62a2c6cf8` |
| | `hal.dll` | 194,432 | same (internal name `halmacpi.dll`) | `f8b007e41157452e8c2d262f37260eb662cb2099e22acffdf9d89836a6290264` |
| `tools/win7-x64-extracted/` | `usbport.sys` | 325,120 | 6.1.7601.17514 | `e7fe3ec3da3cabdbbe2c23baa5fe3cd64da01ff73b4b4c2f077224a607e688dd` |
| | `usbehci.sys` | 52,224 | same | `e8258ea65b0fcad4e077b176e9d9324646b652d6e651241e397346a39770d065` |

The kernel and HAL images are for task 22.2 and are x86 only: there is no
64-bit binary of this driver whose imports could be checked. The `hal.dll` in
`System32` of both install images is the ACPI multiprocessor HAL - the variant
a QEMU guest gets - which is what its version resource says; Setup places the
matching variant under that name.

Extraction, host-side, no VM and no `qemu-img`. Vista and Windows 7 media are
WIM-based rather than the flat `I386`/`AMD64` directory the XP media has, so the
route is one level deeper, and the ISO can be mounted rather than copied:

```powershell
$r = Mount-DiskImage -ImagePath "D:\isos\en_windows_7_professional_with_sp1_vl_build_x86_dvd_u_677896.iso" -PassThru
($r | Get-Volume).DriveLetter        # F, below
```
```bat
7z e "F:\sources\install.wim" -otools\win7-x86-extracted ^
     "Windows\System32\drivers\usbport.sys" "Windows\System32\drivers\usbehci.sys" ^
     "Windows\System32\ntoskrnl.exe" "Windows\System32\hal.dll"
```

On Vista, whose WIM holds seven images, every path takes an image-index prefix
(`1\Windows\System32\...`). 7-Zip 26.00 opens both WIMs directly and no
`dism` (which wants elevation) is needed; the edition names come from the WIM's
own XML, which is stored uncompressed at the offset in the WIM header at
`0x48`. `Dismount-DiskImage` afterwards.

The same two rules bind this material as the XP x64 material above: nothing
extracted is tracked, and every fact taken from it is tagged **static** with its
row in `legal-provenance.md` section 4.

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

All six were read on 2026-09-08 and all six pass. M1 through M3 came first
and any one of them could have ended the investigation; M4 through M6 are
the transcription work that M3's result turned from open questions into
confirmation.

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
`usbport.sys` is. That is what turned M4 and M6 from open questions into
confirmation, and the three sections below are that confirmation.

### M4 - the `USBPORT_RESOURCES` layout - **read, pass**

Read from the amd64 `usbehci.sys`'s `StartController`, which the packet's
`0x48` slot names as `usbehci+0x82D0`. It is handed the structure in `rdx`
(saved into `r12`), and its loads give the field offsets and widths directly:

```
  movzx eax,byte ptr [r12]        ; ResourcesTypes, tested with and al,6 / cmp al,6
  mov   eax,dword ptr [rdx+4]     ; HcFlavor          dword at 0x04
  mov   rdx,qword ptr [r12+28h]   ; ResourceBase      QWORD at 0x28
  mov   rdx,qword ptr [r12+38h]   ; StartVA           QWORD at 0x38
  mov   r8d,dword ptr [r12+40h]   ; StartPA           DWORD at 0x40
  cmp   byte ptr [r12+45h],0      ; IsChirpHandled    byte  at 0x45
```

giving the amd64 layout, `sizeof` `0x48`:

| Offset | Width | Field |
|---|---|---|
| `0x00` | 4 | `ResourcesTypes` |
| `0x04` | 4 | `HcFlavor` |
| `0x08` | 4 | `InterruptVector` |
| `0x0C` | 1 + 3 pad | `InterruptLevel` |
| `0x10` | 8 | `InterruptAffinity` (`KAFFINITY`, widened) |
| `0x18` | 1 + 3 pad | `ShareVector` |
| `0x1C` | 4 | `InterruptMode` |
| `0x20` | 4 + 4 pad | `Reserved` |
| `0x28` | 8 | `ResourceBase` |
| `0x30` | 4 + 4 pad | `IoSpaceLength` |
| `0x38` | 8 | `StartVA` |
| `0x40` | **4** | `StartPA` |
| `0x44`-`0x47` | 1 each | `LegacySupport`, `IsChirpHandled`, `Reserved2`, `Reserved3` |

**`StartPA` did not widen.** It is read as a dword, so the common-buffer
physical address is a `ULONG` on amd64 exactly as on x86, nothing after it
shifts, and `XhciCheckResourceBase(ULONG_PTR startVA, ULONG startPA)` already
has the right signature. This was the most load-bearing open question in
section 2 and it resolves in the driver's favour.

The three obvious `USBPORT_RESOURCES` assertions - `sizeof == 0x48`,
`ResourceBase` at `0x28`, `StartVA` at `0x38` - are all **correct**. Written
against the compiler's own layout they would be tautological, but the binary
agrees with them, so promoting them to measured numbers costs nothing. That is not true of the packet (section 9
item 2), and the difference between the two cases is exactly why this had to
be measured rather than reasoned out.

### M5 - the DMA adapter width - **read, pass**

`IoGetDmaAdapter` is imported at IAT RVA `0x2A0D0` - verified by following
that slot to its import-name-table entry, not by trusting name ordering - and
is called from exactly **one** site in the whole image, `.text` RVA `0x313E`:

```
  mov  rcx,qword ptr [rbx+1A8h]   ; PDO
  lea  r8,[rbx+6C8h]              ; &NumberOfMapRegisters
  lea  rdx,[rsp+88h]              ; DEVICE_DESCRIPTION
  call qword ptr [IoGetDmaAdapter]
```

The containing function starts at RVA `0x2340`: the `.pdata` entry covering
the call site is a chained continuation, `0x2929`-`0x34F1` -> `0x234E`-`0x2929`
-> `0x2340`-`0x234E`, and the descriptor is zeroed and filled in the primary
chunk at RVA `0x269F`-`0x270B`:

```
  mov  qword ptr [rsp+88h],rax        ; zero the whole 0x28-byte descriptor
  mov  qword ptr [rsp+90h],rax        ;   (rax = 0, five qword stores)
  mov  qword ptr [rsp+98h],rax
  mov  qword ptr [rsp+0A0h],rax
  mov  qword ptr [rsp+0A8h],rax
  mov  dword ptr [rsp+88h],r14d       ; +0x00 Version
  mov  byte  ptr [rsp+8Ch],1          ; +0x04 Master            = 1
  mov  byte  ptr [rsp+8Dh],1          ; +0x05 ScatterGather     = 1
  mov  byte  ptr [rsp+90h],1          ; +0x08 Dma32BitAddresses = 1
  mov  dword ptr [rsp+9Ch],5          ; +0x14 InterfaceType     = PCIBus
  mov  dword ptr [rsp+0A0h],2         ; +0x18 DmaWidth          = Width32Bits
  mov  dword ptr [rsp+0A4h],r14d      ; +0x1C DmaSpeed          = Compatible
  mov  dword ptr [rsp+0A8h],0FFFFFFFFh; +0x20 MaximumLength
```

**`Dma32BitAddresses = 1`, `DmaWidth = Width32Bits`, and `Dma64BitAddresses`
(`+0x0B`) is left zero by the zeroing stores and never written.** The adapter
on NT 5.2 amd64 is created 32-bit, identical to what `src/xhci_usbport.h`'s
scatter-gather comment records for the 32-bit builds.

So on a 64-bit guest the common buffer and every mapped transfer buffer stay
below 4 GB whatever the guest's RAM, the high DWORD of an address is zero for
the same measured reason as before, and `StartPA` being a `ULONG` (M4) is
consistent rather than lucky. The RAM cap this section previously called for
is not needed; keeping the first guest small is free and worth doing anyway,
but it is now a convenience rather than a mitigation.

The rule this project already applies does not change: the high DWORD is a
value to **check**, never to assume. `src/xhci_xfer.c:542` stays exactly as it
is, and item 3 of section 9 stands on that principle rather than on a measured
hazard.

### M6 - the offset map, end to end - **read, pass**

Two halves, both taken.

**The service-pointer block.** Before copying, usbport writes 16 pointers into
the *caller's* packet, at RVAs `0x221FE` through `0x222E1`, at offsets

```
0x1A0 0x1A8 0x1B0 0x1B8 0x1C0 0x1C8 0x1D0 0x1D8
0x1E0 0x1E8 0x1F0 0x1F8 0x200 0x208 0x210 0x218
```

Sixteen, ascending, exactly as on x86, and the widening map `f(X) = 0x28 + (X
- 0x28) * 2` carries the x86 block `0xE4`-`0x120` onto `0x1A0`-`0x218`
precisely. usbport's behaviour is unchanged: it writes these into the
miniport's own packet and touches no other field before copying it.

**The miniport callback block.** The amd64 `usbehci.sys` fills its packet -
base RVA `0x9C60`, established from its four size stores at `+0x10`, `+0x14`,
`+0x18` and `+0x24` - in `DriverEntry` at RVA `0x6180`. Extracting every
`mov [rip+d],reg` into that packet gives **50 filled pointer slots, and every
one lands on `f(X)` of a field the x86 record names**:

```
  0x028 OpenEndpoint         0x048 StartController     0x078 SubmitTransfer
  0x030 ReopenEndpoint       0x050 StopController      0x080 SubmitIsoTransfer
  0x038 QueryEndpointReqts   0x058 SuspendController   ...
  0x040 CloseEndpoint        0x060 ResumeController    0x220 RebalanceEndpoint
```

running from `0x028` to `0x238`, no slot off the map and no slot unaccounted
for. Fifty independent confirmations that the amd64 packet is the x86
declaration compiled wide.

**And the tail, which is where the eight bytes are.** The last slot usbehci
fills is `0x238` = `f(0x130)`, the last callback before the x86 record's two
trailing `Reserved` fields at `0x134` and `0x138`. Under the same map those
land at `0x240` and `0x248`, and a field at `0x248` puts the structure's end at
`0x250` - exactly what usbport copies. A declaration that keeps them 4 bytes
wide packs them at `0x240` and `0x244` and ends at `0x248`, which is pull
request 6's number and eight bytes short. The measured requirement is the
size: **a `_WIN64` packet declaration must be `0x250` bytes.** Whether that is
reached by widening the two `Reserved` fields or by explicit tail padding is an
implementation choice - both are reserved and neither is read by this driver.

What is *not* read, and needs not be for this phase: which usbport service
sits in each of the sixteen service slots. Their x86 identities are already in
the record and the map is now validated across 50 slots, so the arithmetic
carries them. If a change ever calls a service this driver does not call
today, read that slot rather than trusting the inference.


---

## 6. Vista and Windows 7 x64 - read 2026-09-09, and the answer is yes

Every measurement in section 5 is a static read of two files, so repeating it on
Vista x64 and Windows 7 x64 cost extraction time and nothing else. It was taken
on 2026-09-09 (task 21.7), in one pass with the `i386` halves of the same media
that task 22.1 needed, and **all six measurements pass on both**. The
transcription is in `usb-xhci-info/usbport-miniport-abi.md`, "The 6.0 and 6.1
lineages"; this section states what it means for the plan.

| | Vista x64 | Windows 7 x64 | vs NT 5.2 amd64 |
|---|---|---|---|
| M1 exports | 4 names, ordinals 1-4 | 4 names, ordinals 1-4 | **differs**: `DllInitialize` added at ordinal 1, shifting the other three |
| M2 `USBPORT_GetHciMn` | `0x10000001` | `0x10000001` | same, and already accepted |
| M3 gate / `Version >= 200` size | `>= 100`, `0x250` | `>= 100`, `0x250` | same; two further tiers added above |
| M4 `USBPORT_RESOURCES` | prefix identical, `StartPA` a DWORD at `0x40` | same | same through `0x48`; the structure grows a tail |
| M5 DMA adapter | 32-bit, one call site | 32-bit for a Version-200 miniport; a second, 64-bit adapter exists behind a version gate | **differs on 6.1**, harmlessly for this driver |
| M6 offset map | 50 slots, all on `f(X)` | 50 slots, all on `f(X)` | same |

So **one `WNET` amd64 binary can serve Vista x64 and Windows 7 x64 as far as
this interface is concerned**, and the claim in task 21.6 does not have to be
narrowed to Windows XP x64 and Server 2003 x64 on ABI grounds. Nothing else in
the phase waited on this, and nothing in it changes 21.2 or 21.4.

Four things are worth carrying forward, in descending order of how much they
could cost:

- **Windows 7's usbport can create a 64-bit DMA adapter, and does not for us.**
  It has two `IoGetDmaAdapter` call sites; the second passes
  `Dma64BitAddresses = 1`. It is reached only when the miniport registered with
  `Version >= 310` *and* filled the packet slot at `0x1F8` (x86) / `0x398`
  (amd64) - Windows 7's own `usbehci.sys` fills it, and this driver does neither.
  When the gate fails, usbport copies the 32-bit adapter into the 64-bit slot
  and there is one adapter as before. This is the one place where a future
  change - raising the declared interface version to get at something in the
  version-300 region - would silently move physical addresses above 4 GB. The
  existing high-DWORD check at `src/xhci_xfer.c:542` is what stands between that
  and corruption, and this reading is the reason not to remove it.
- **`USBPORT_RESOURCES` is longer than `0x48` on both.** Vista x64's
  `usbehci.sys` reads fields at `0x50`, `0x58`, `0x90` and `0x94`; Windows 7
  x64's at `0x50`, `0x90` and `0xC0`. usbport owns the allocation, so reading a
  prefix stays safe, but the `sizeof` this driver declares is not the operating
  system's on 6.0 or 6.1, and no assertion may claim otherwise.
- **A fourth export shifts every ordinal.** The import library binds by name, so
  nothing breaks; it is recorded so that a future reader of an ordinal-based
  disassembly is not misled.
- **The packet has four version tiers, not two.** At `Version = 200` usbport
  copies `0x250` bytes and writes nothing into the caller's packet above
  `0x218`, exactly as NT 5.2 amd64 does. The higher tiers' extra service
  pointers are behind the version tests.

Two things are true of Vista x64 and Windows 7 x64 regardless, unchanged by
these readings, and both belong in the record before anyone builds a guest:

- **Kernel-mode code signing.** Windows XP x64 does not enforce it: an unsigned
  driver installs with a warning and loads, which is why it is the target this
  phase takes. Vista x64 and Windows 7 x64 do enforce it, and the
  cross-certificate route that once made third-party Windows 7 x64 signing
  possible is no longer available in practice. Supporting those two means,
  permanently, a guest booted with driver signature enforcement disabled (F8) or
  with test-signing on. That goes in the release notes beside the tier, not in a
  footnote.
- **The install path is different.** The INF's `LayoutFile=layout.inf` route,
  which is how the media carries no Microsoft file, is a Windows 2000 and XP
  mechanism. Vista and later stage a driver package into the driver store before
  installing it and validate its file list more strictly. One thing read off the
  media in the same pass makes this look easier than it did: both install images
  carry `usbport.sys`, `usbehci.sys`, `usbhub.sys` and `usbd.sys` in
  `Windows\System32\drivers` outright, in every architecture, so a Vista or
  Windows 7 machine has them from its first boot whatever controllers it has -
  the opposite of XP and 2000, where an xHCI-only install has none of them and
  Phase 19's `LayoutFile` fix exists for exactly that. Every
  `COPYFLG_NO_OVERWRITE` copy should therefore skip and never need a source.
  "Should" is still doing work in that sentence: this is a reading of the
  install image, not of an installed system, and the driver store's own
  validation of the package's file list is not answered by it at all. It stays a
  guest question (task 22.3 and 22.4).

Windows 7 x64 is also the weakest of the three on merit:
`docs/usb-xhci-info/win98-wdm.md` records that Intel's xHCI driver line *begins*
at Windows 7. The genuine gap there is the newer PCH and SoC silicon Intel never
shipped a Windows 7 driver for, which plausibly includes this project's own
Skylake and Comet Lake machines but has not been checked. Windows XP x64 has the
same total absence of options that 32-bit XP does.

---

## 7. The decision gate

The gate as written before any measurement, with what actually happened:

| Reading | Meaning | Outcome |
|---|---|---|
| M1 fails | Stop. No Option A on 64-bit Windows. | Did not happen; three exports, same ordinals |
| M3 shows a packet that is not the 316-byte one widened | Stop and record it: a different ABI, not a wider one, and a different project | Did not happen; `0x250` and `0x230` are the exact widening |
| M2 gives a third constant; M3-M6 correspond to the x86 record widened | Proceed | Better than this: M2 gave a value the driver already accepts, and M6 confirmed the widening across 50 slots |
| M4 shows `StartPA` widened, or any field where the compiler's natural layout disagrees with the binary | Proceed, but the declaration needs explicit padding and the asserts must carry measured numbers | `StartPA` did **not** widen and `USBPORT_RESOURCES` agrees with the natural layout; the packet does not, and is `0x250` against a natural `0x248` |
| M5 shows a 64-bit adapter | Proceed with a `StartPA` high-DWORD refusal added first, and keep the guest under 4 GB until it has been exercised | Did not happen; the adapter is created 32-bit, `DmaWidth = Width32Bits` |
| Vista / Windows 7 differ from 5.2 | Claim Windows XP x64 and Server 2003 x64 only, and record what differs | Read 2026-09-09; all six pass on Vista x64 and Windows 7 x64, so the claim is not narrowed. Three differences recorded and none of them reaches a Version-200 miniport: a fourth export shifting the ordinals, two further packet version tiers, and - on 6.1 only - a second, 64-bit DMA adapter behind a `Version >= 310` gate. See section 6 |

**All six measurements are taken and all six pass.** The static pass set out
to find a reason this cannot work on Windows XP x64 and Server 2003 x64, and
found none. What it found instead was one trap in the obvious way of writing
the packet, which no compile-time assertion could have caught (section 9
item 2), and one
assumption that turned out to be safe for a measured reason rather than a
lucky one (M5).

What the static pass cannot establish is runtime behaviour, and Phase 19 is
the standing reminder of how much that leaves: there, the ABI was right, the
static work was right, and the guest still produced three problems in an
afternoon. Section 10 says what to expect.

---

## 8. The build path

**One binary, two flavours.** WDK 7.1, `setenv ... x64 WNET`, producing an NT
5.2 amd64 build; `fre` and `chk`, the two shipping flavours. `qemu` needs the
`__asm` exclusion of section 2 before it compiles at all, and is worth having
for the reason it exists on x86: it is the flavour that makes a VM leg
diagnosable.

**The build plumbing is single-architecture the whole way down**, and that
blocks an amd64 binary harder than any of the code changes in section 9 do.
Read 2026-09-08, not assumed:

- `src/sources` hardcodes `TARGETLIBS=.\usbport.lib` - one library, one
  architecture.
- `scripts\build-driver.cmd` hardcodes `i386` in **seven places**, including
  all four `OUTSYS` paths and the `source-stamp.ps1 -Write` call.
- It drives the **Windows 2000 DDK's** `setenv.bat`, not WDK 7.1's. The two
  take different arguments entirely and cannot share a code path without a
  fork.
- The scout adds a fourth: WDK 7.1's `setenv.bat` sets
  `BUILD_ALT_DIR=fre_wnet_AMD64`, which `src/sources`' three-flavour `!ERROR`
  refuses outright. The fork has to override it *after* `setenv.bat`, exactly
  as `build-driver.cmd` already does for `chk_qemu`.

So a first amd64 build needs a second toolchain path **and** the import
library below before a compiler is reached. It is not a half-hour job, and the
small code changes of section 9 are not what stands between here and a binary.

**The gates do not cover a 64-bit binary, and skipping them is not an
option.**

- *Import gate.* A build that calls `build -ceZ` directly never runs
  `scripts\build-driver.cmd`, which means no import gate and no INF gate on
  the produced binary - so the amd64 build has to go through the wrapper like
  every other. The Windows 98 export ceiling is
  meaningless on amd64, but the allowlist's purpose - that nothing enters the
  import table unreviewed - is not. This needs an `amd64` dimension in
  `scripts\import-gate\xhci98-imports.allow`, or a sibling allowlist, plus
  NT 5.2 amd64 baselines to resolve against in the way
  `win2k-baselines.expected` does for SP4.
- *usbport import library.* There is no amd64 import library, and nothing can
  link without one, so this is a prerequisite for a first build rather than a
  gate refinement. The temptation is a second generator publishing straight
  over `src\usbport.lib`; that would drop all five verification steps
  `scripts\make-usbport-lib.cmd` performs - the export-manifest check, the
  `_USBPORT_GetHciMn@0` decoration check, the `DLL name : USBPORT.SYS` module
  check, and the NTAPI link proof. The right shape is
  arch-conditional checks inside the existing generator: on amd64 the
  decoration check becomes an undecorated-name check, M1 supplies the fourth
  lineage for the export manifest, and the other four steps stand unchanged.
- *INF gate.* `scripts\inf-gate\check-inf.ps1` knows exactly two install
  paths, the undecorated Windows 98 one and `.NTx86`, across 1854 lines. A
  third path is real work there.

**On the INF, there is a cheap route and a pure one.** The obvious route adds
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

### The compile scout - taken 2026-09-08, and it comes back nearly clean

Before any of that plumbing is written, WDK 7.1's x64 compiler was pointed
straight at `src/` to find out what amd64 actually says about this code. The
scout needs none of the plumbing: `build /L` compiles every file and runs no
link phase, so no amd64 `usbport.lib` and no arch fork through
`build-driver.cmd` were required, and the repo's own `sources` drove it
unmodified in a scratch copy of `src/`.
`setenv.bat <root> {fre|chk} x64 WNET no_oacr` gives `cl` 15.00.30729.207 for
x64; `BUILD_ALT_DIR` was overridden after it exactly as `build-driver.cmd`
does for `chk_qemu`, so all three flavours were read. Nothing was built in the
real tree and no binary was produced.

**The code itself is nearly clean.** With the layout asserts of section 9
item 2 neutralised - they are the expected failures, and neutralising them is
what proves they were masking nothing - all 18 `.c` files compile for amd64 at
`/W3` with **exactly one diagnostic in the whole driver**:

```
xhci_dispatch.c(1001) : warning C4242: 'function' : conversion from 'ULONG_PTR' to 'ULONG', possible loss of data
```

That is `XhciLogNoteAddress(ext, "start.ok", ext->ResourceBase)`, whose third
parameter is a `ULONG`. It is one site rather than a class of problem: the
only other call site, `:953`, already writes the truncation out as
`(ULONG)(ULONG_PTR)resources`. So the first time this source has been through
a non-MSVC-6 compiler it produced a single opinion, and **21.2 is a plumbing
job with a known end rather than a long tail** - which is what the scout was
taken to find out.

**`/WX` is on in `fre` and off in `chk`**, which is the reverse of the
intuition that the checked build is the strict one. WDK 7.1's `setenv.bat`
sets `BUILD_ALLOW_COMPILER_WARNINGS=1` for a checked build and leaves it unset
for a free one, so that single C4242 fails `release` outright (`C2220`, no
object produced) and merely warns in `debug` and `qemu`. A first amd64 build
that only ever ran `chk` would not see it.

**The `__asm` exclusion is confirmed, with its sites.** The `qemu` flavour
takes 10 errors and no others, at four lines - `4670`, `4673`, `4678`, `4695`
- headed by `error C4235: nonstandard extension used : '__asm' keyword not
supported on this architecture`. `release` and `debug` are unaffected.

**The layout asserts split 12 failing against 11 passing, and the split is
itself a reading.** The 11 that pass say the substituted types are 4-byte-exact
on amd64 too: `USBPORT_ENDPOINT_REQUIREMENTS`, `XHCI_SETUP_PACKET`,
`USBPORT_TRANSFER_PARAMETERS`, `USBPORT_SCATTER_GATHER_ELEMENT`,
`USBPORT_ISO_PACKET`, `USBPORT_ISO_TRANSFER`, `USBPORT_ROOT_HUB_DATA` and both
status words are unchanged, and so are the two packet offsets at or below the
hinge - `MiniPortResourcesSize` at `0x24` and `OpenEndpoint` at `0x28`.

The 12 that fail were read against the compiler's own layout with
`/d1reportSingleClassLayout`. **For the registration packet the compiler
reproduces M6's map and M3's boundary exactly, from an entirely different
instrument**, and disagrees only on the total size:

| Slot | x86 | `f(X) = 0x28 + (X - 0x28) * 2` | compiler |
|---|---|---|---|
| `MiniPortResourcesSize` | `0x24` | `0x24` (below the hinge) | `0x24` |
| `OpenEndpoint` | `0x28` | `0x28` (the hinge) | `0x28` |
| `StartController` | `0x38` | `0x48` | `0x48` |
| `RH_GetRootHubData` | `0x90` | `0xF8` | `0xF8` |
| `StartSendOnePacket` | `0xD8` | `0x188` | `0x188` |
| `UsbPortDbgPrint` | `0xE4` | `0x1A0` | `0x1A0` |
| `UsbPortNotifyDoubleBuffer` | `0x120` | `0x218` | `0x218` |
| `RebalanceEndpoint` | `0x124` | `0x220` | `0x220` |
| `RH_ChirpRootPort` | `0x12C` | `0x230` | `0x230` (= M3's measured short copy) |
| `sizeof` | `0x13C` | - | **`0x248`, against M3's measured `0x250`** |

So the eight-byte shortfall of section 9 item 2 is now a compiler reading as
well as a disassembly one, and it sits entirely in the trailing reserved
region: the compiler ends the structure at `Reserved5` + 4 = `0x248`, and
usbport copies to `0x250`.

**The other three failures are the finding the scout did not go looking for.**
Each is a structure whose amd64 `sizeof` the current declaration gets wrong,
and only one of the three has a measured number to be checked against:

| Structure | x86 | current declaration on amd64 | measured | why it moves |
|---|---|---|---|---|
| `USBPORT_RESOURCES` | `0x34` | `0x40` | **`0x48`** (M4) | `ULONG InterruptAffinity` at `0x10`. M4 read it as **8 bytes** (`KAFFINITY`, widened); the declaration pins it to 4. That one field is the whole `0x8` gap, and everything after it in M4's table shifts by 8 |
| `USBPORT_SCATTER_GATHER_LIST` | `0x40` | `0x50` | **not measured** | `ULONG_PTR CurrentVa` at `0x04` and `PVOID MappedSystemVa` at `0x08` both widen and realign; `SgElement[]` moves from `0x10` to `0x1C` |
| `USBPORT_ENDPOINT_PROPERTIES` | `0x40` | `0x48` | **not measured** | `ULONG_PTR BufferVA` at `0x1C` widens and forces 8-byte alignment, so 4 bytes of padding appear at `0x1C`, `BufferVA` lands at `0x20`, and every field after it shifts by 8 |

`USBPORT_RESOURCES` and the scatter-gather structures are already named in
section 9 item 2. **`USBPORT_ENDPOINT_PROPERTIES` is not, and it is the
structure every endpoint callback is handed** - `OpenEndpoint`,
`ReopenEndpoint`, `QueryEndpointRequirements` and `RebalanceEndpoint` all
receive a pointer to it. It changes shape on amd64 and its real layout has not
been measured, so it is now item 2a below. The rule M4 and M3 established
holds here with nothing to fall back on: the compiler's natural widening is
a guess until the binary confirms it, and for this structure the binary has
not been asked.

---

## 9. The code changes

1. ~~The NT 5.2 `USBPORT_GetHciMn` constant and a third arm of the
   `src/xhci_dispatch.c:4640` refusal~~ - **not needed. M2 read
   `0x10000001`, which the driver already accepts.**
2. `#ifdef _WIN64` declarations of `USBPORT_RESOURCES`, the registration
   packet and the scatter-gather structures carrying the *measured* offsets,
   with explicit padding wherever the compiler's natural layout disagrees.
   **M3 already shows one disagreement**: declared the obvious way under
   `_WIN64`, `sizeof(USBPORT_REGISTRATION_PACKET)` comes out `0x248` (584),
   and usbport copies `0x250` (592). That is eight bytes past the end of
   `XhciRegPacket`, a static global, read into fields usbport believes are
   part of the packet. A `C_ASSERT` on the declared size cannot catch it,
   because it asserts the compiler against itself; only the binary shows it.
   The asserts must carry measured numbers so that they can fail.
   **The compile scout of section 8 has since read all three from the
   compiler's side**: the packet is `0x248` against the measured `0x250`,
   `USBPORT_RESOURCES` is `0x40` against M4's measured `0x48` - the whole gap
   being `InterruptAffinity`, which M4 read as 8 bytes and the declaration
   pins to 4 - and `USBPORT_SCATTER_GATHER_LIST` is `0x50` against `0x40`,
   with `SgElement[]` moving from `0x10` to `0x1C`.

   **2a - `USBPORT_ENDPOINT_PROPERTIES` has to be measured, and it has not
   been.**
   The scout found it changes size on amd64 - `0x40` to `0x48`, because
   `ULONG_PTR BufferVA` at `0x1C` widens and realigns, moving `BufferVA` to
   `0x20` and shifting every field after it by 8. It is the structure
   `OpenEndpoint`, `ReopenEndpoint`, `QueryEndpointRequirements` and
   `RebalanceEndpoint` are each handed, so a wrong layout is misread on every
   endpoint operation, silently. None of M1-M6 covers it. It needs the same
   treatment M4 gave `USBPORT_RESOURCES`: read the offsets out of the amd64
   `usbehci.sys`'s `OpenEndpoint`, then declare it under `_WIN64` with the
   measured numbers. Until that reading exists, this structure is the one
   remaining place where an amd64 binary would be running on an assumption.

   What to watch for is *width*, not position. `BufferVA` is the declared
   `ULONG_PTR` and the reason the size moves at all; the three fields this
   header spells `ULONG` in place of NT enums - `DeviceSpeed`, `TransferType`
   and `Direction` - are the ones to check are still 4 bytes.
   `InterruptAffinity` in item 2 is the cautionary case: a field pinned here
   to `ULONG` that the real amd64 structure widens to 8, and the whole of why
   `USBPORT_RESOURCES` comes out `0x40` instead of `0x48`.
3. A `StartPA` high-DWORD refusal matching the one the scatter-gather path
   already makes. **M5 shows the adapter is created 32-bit, so this guards
   nothing measured** - it is the "check it, never assume it" rule applied one
   place further, and is optional on the evidence rather than required by it.
4. The `__asm` stack-delta check excluded under `_WIN64`. The scout names the
   sites the compiler stops at - `4670`, `4673`, `4678`, `4695` - and the
   `espBefore`/`espAfter` locals at `4591`-`4592` have to go inside the same
   guard, or the free build takes an unreferenced-local warning, which `/WX`
   makes an error there.
5. `src/xhci_compat.h`'s `ULONG_PTR` typedef guarded for 64-bit hosts. **This
   does not block the amd64 driver build at all**, and reading it as a blocker
   is the easy mistake: the typedef sits inside the `#ifdef XHCI_HOST_TEST`
   block, and in a driver build `ntddk.h` supplies the real one. What it
   blocks is item 7 - `test/test_packet.c` compiling for amd64. Windows is
   LLP64, so `unsigned long` stays 32 bits while `ULONG_PTR` must be
   pointer-sized; the typedef is accidentally correct today only because the
   host tests build x86.
6. Design record 04's common-buffer arithmetic re-run against the amd64
   `sizeof`s, and the result stated rather than assumed to carry.
7. `test/test_packet.c` extended to compile the header for amd64, so the
   layout asserts are checked by `test\run-host-tests.cmd` on the build host
   rather than only inside a driver build.
8. `src/xhci_dispatch.c:1001`'s implicit `ULONG_PTR` -> `ULONG` truncation,
   which is the whole of what the amd64 compiler objects to in this source
   (section 8's scout) and which fails the `release` build alone, because
   `/WX` is on in `fre` and off in `chk`. The narrow fix is the explicit cast
   `:953` already writes; the question it raises - whether the log channel
   should carry a truncated kernel VA at all on a 64-bit target - is worth
   answering once rather than casting twice.

Items 4 and 5 are the only two that could be taken before M4 and M6 land;
item 8 became known once the scout ran, and is a one-line change.

---

## 10. The VM leg

Modelled on Phase 19, which is the template for adding a VM-only target.

- One guest, `scripts\setup-qemu-winxp64.ps1`, alongside the existing
  Windows XP, Windows 2000 and Windows ME launchers, with RAM capped below
  4 GB pending M5. **Written and run 2026-09-08**, and M5 having read the DMA
  adapter as 32-bit, the cap is a convenience rather than a mitigation: the
  guest has 2048 MB because XP x64 wants it, not because 4 GB would be
  unsafe.
- **The guest cost one prediction this record did not make.** It was written
  expecting the 32-bit XP recipe to carry over with a CPU model, a RAM figure,
  a disk size and a monitor port changed. It does not: the accelerator changes
  too, and in the direction nothing here would have guessed. Under
  `-accel whpx,kernel-irqchip=off` - the value every other guest in this
  project uses, and the one `lessons.md` argues for - XP x64 Setup wedges on
  "Setup is starting Windows" with `RIP` pinned; under `-accel tcg`, the same
  command line one flag apart, Setup runs. That is the reverse of the Windows
  2000 reading, and it is now a second `lessons.md` entry with a cross-link on
  the first. The generator defaults to TCG and the launcher gate holds it
  there. **The methodological point belongs in this record**, because section
  5's whole argument is that a static pass cannot establish runtime behaviour:
  here the very act of building the vehicle for the runtime work produced a
  fact no amount of reading `usbport.sys` could have.
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
  the guest settles it. **Half of it is now settled, statically, 2026-09-08.**
  An XP x64 install with no USB controller has no `usbport.sys`,
  `usbhub.sys`, `usbehci.sys` or `usbd.sys` on disk at all - exactly the
  32-bit XP finding, so the Code 39 would recur and the first fix is needed
  here too. The files are in `WINDOWS\Driver Cache\amd64\`, which is the
  `LayoutFile` route's premise, and the three in `sp2.cab` are **byte-identical
  by `sha256` to `tools/winxp64-extracted/`** - so the stack this guest will
  install is the one M1-M6 were read from, which was worth confirming and is
  not something the measurements themselves could say. Two details for whoever
  writes the `.NTamd64` copy section: `usbd.sys` comes from `driver.cab` and
  not `sp2.cab`, and unlike 32-bit XP it is not on disk in any form. The
  second fix, `DisableSelectiveSuspend`, still needs the running guest.
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
   driver signature enforcement disabled, permanently and by design. The ABI
   half of this is now settled and does not narrow the claim (section 6); what
   is left is the signing decision and two more guests.
2. One INF with a third install path and a four-leg re-validation, or a
   separate x64 package leaving `src/xhci98.inf` untouched.
3. Whether WDK 7.1 stays in the tree as a third toolchain, and from which
   source. Third-party rehosts of the ISO exist and are not a Microsoft host;
   the copy now in `tools/WinDDK71/` came from the owner's own media
   (section 4), which is the provenance to keep.
4. Whether this project adopts GitHub Actions at all. There are no workflows
   today, and the build's defining property is that its toolchain lives in
   the repository and installs nothing; a CI job that downloads and installs
   an SDK is a different arrangement, not an extension of that one.
5. Whether the x86 half of a WDK 7.1 build is declined, as section 1
   recommends.

---

## Sources

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
  VM-supported target), Phase 21 (this record's tasks) and Phase 22, whose
  tasks 22.1 and 22.2 were taken in the same pass as 21.7 off the same media
  and whose static half section 6 therefore also settles.
- `docs/contributing/legal-provenance.md` sections 1 and 4: routine
  unpacking, and the static tagging every fact here carries.
- `docs/contributing/design/04-controller-common-buffer.md`: the arithmetic
  section 9 item 6 re-runs.
