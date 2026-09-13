# Provenance and third-party material

This is a factual record of where this project's non-obvious facts came from,
what third-party material it touches, and how each piece is handled. It is
written so that a reader can see the sources and the methods without having to
reconstruct them.

It is not a legal opinion, and it does not state a conclusion about whether
any act described here is permitted in any jurisdiction. It records what this
repository holds, what it publishes, and what the third-party material it
touches says about itself. Do not quote this file as clearance for anything,
and do not add a conclusion to it.

---

## 1. What the project is, and why it needs any of this

`xhci98.sys` is an independently written WDM kernel-mode driver for xHCI host
controllers, targeting Windows 98 SE and Windows 2000 SP4. It is built as a
miniport of Microsoft's `usbport.sys` (see `AGENTS.md`, "Architecture
Overview"), which means it must fit an interface, `USBPORT_REGISTRATION_PACKET`
and its callbacks, that Microsoft never documented.

Everything in this file exists because of that one problem: to make an
independent driver plug into an undocumented interface, the shape of the
interface has to be determined. Two sources supply it.

- ReactOS reimplements the whole NT5-era USB stack in readable C, including
  `usbport` and three miniports written against it. It supplies most of the
  interface description.
- The `usbport.sys` binaries already installed on the target machines supply
  the target-specific contracts where ReactOS differs from them, omits
  something, or (as happened repeatedly) carries a guard the shipping code
  does not have.

No protection mechanism on any executable or driver binary is defeated, and no
binary is patched or modified. Each is read as the vendor shipped it, with the
Windows 2000 DDK's own COFF dumper (`link -dump -disasm`, the same dumper as
`dumpbin`) and with runtime counters compiled into this project's own driver.

One qualification, because an unqualified "nothing is unpacked" would be
false. Several of these binaries ship inside distribution packaging and are
expanded before they can be read at all: `I386\USBD.SY_` and
`I386\USBPORT.SY_` off Windows install media, extracted with `7z e` and
expanded with `expand.exe` (`scripts/package/extract-usbd-sources.ps1`;
`docs/usb-xhci-info/usbport-miniport-interface.md` section 5). That is
Microsoft's own compression and Microsoft's own decompression tool, applied to
get at the file the installer would place anyway. It is not the defeat of any
protection measure, and it is recorded here so the distinction is stated
rather than assumed.

One further detail belongs up front. The Intel xHCI specification PDF is
encrypted with an empty owner password, and this project's text-extraction
recipe supplies that empty password (`r.decrypt("")`,
`docs/references/README.md`). Each user does that to their own downloaded copy
in order to read and quote a document Intel publishes for download; no
password is guessed, broken, or supplied from elsewhere, and no extracted text
is redistributed beyond the short verbatim quotations kept in `docs/` for
identification.

---

## 2. What is tracked in this repository, and what is not

| Material | Tracked? | Where | Note |
|---|---|---|---|
| This project's source, tests, scripts, docs | Yes | `src/`, `test/`, `scripts/`, `xhciqual/`, `docs/` | GPL-2.0-only (`LICENSE`) |
| Published driver binary | Yes | `releases/<version>/release-x86/xhci98.sys`, `releases/<version>/debug-x86/xhci98.sys`, and the `-x64` pair in a cut that carries one (the directories cut before roadmap task 21.3 keep the bare `release/` and `debug/` names they were written with) | Entirely this project's code. Linked by the Windows 2000 DDK against `ntoskrnl.exe`/`hal.dll`/`usbport.sys` import libraries, which contributes no code to the image; the import table names symbols and nothing more (verify with `scripts\import-gate\check-imports.ps1`, which prints every module/symbol pair in it). No third-party object, runtime or extender is linked in, which is what makes this row different from the qualifier's below. GPL-2.0-only (`LICENSE`) |
| Published DOS qualifier binary | Yes | `releases/<version>/xhciqual/XHCIQUAL.EXE` + `.MAP` | Built from this project's sources but not only this project's code; see section 2a |
| Published snapshot reader binary | Yes | `releases/<version>/xhcisnap/XHCISNAP.EXE` | Built from this project's sources but not only this project's code: the MSVC 6.0 C runtime is statically linked in. See section 2a |
| Intel xHCI specification PDF | No | `docs/references/` (git-ignored) | URL + version + SHA-256 in `docs/references/README.md` |
| USB-IF backwards-compatibility testing PDF | No | `docs/references/` (git-ignored) | URL + version + SHA-256 in `docs/references/README.md` |
| Oney, *Programming the Microsoft Windows Driver Model* (2nd ed., 2003) | No, and it never was; it is in no commit and in no directory of this repository | Nowhere in this tree; a purchased book on the reader's own shelf | The one third-party document cited by printed page that is not kept in `docs/references/`. Edition, page count, SHA-256 of the copy the citations were read from, and the page-index offset are in `docs/references/README.md`. There is no fetch URL to record, because there is no download |
| ReactOS / Linux / FreeBSD / Haiku source mirrors | No | `external/` (git-ignored) | Fetch procedure and pinned commits in `external/README.md` |
| `usbport.sys`, `usbhub20.sys`, NUSB 3.3, MSVC 6.0, the Win2000 DDK | No | `tools/` (git-ignored) | Supplied by the user's own install media or download |
| The two `usbd.sys` builds and Windows 98 SE's `usbhub.sys` | No | `tools/` (git-ignored) | Reference copies, staged from the user's own install media for the import gate's Windows 98 evidence and the Windows 2000 VM setup. The assembled release download carried them from 0.0.0.4 to 1.0.0.0 and carries them no longer; see section 5 |
| Disassembly extracts taken from those binaries | No | `tools/*-extracted/` (git-ignored) | Convenience copies only; see section 4 |
| Staged install media | No | `out/` (git-ignored) | Generated; the upload set for a release is assembled here |

Three of the git-ignored trees exist for this reason rather than for tidiness:
`docs/references/`, `external/` and `tools/` hold third-party material, and
tracking it is what this project does not do. The Oney row is the same rule
reached from the other side: a document this project never held in the tree
still gets its identity written down, because the alternative is a `p.N`
citation a reader cannot resolve. `out/`, `vm/` and `scripts/local/` are
ignored because they are generated or host-specific; `out/` additionally
because the release upload set is assembled there.

"Not tracked" is a statement about this repository. It is not a statement
that a file is never distributed, and the distinction mattered for three
files, `usbd98.sys`, `usbd2k.sys` and `usbhub98.sys`, while the assembled
asset carried them; see section 5.

The rule this implies for future work: a fact may be tracked; the source
document or binary it was read out of may not. Where a fact would be
unre-derivable without the source, the document that records it also records
how to re-derive it (the address, the instruction, the command), so a clone
with none of the inputs can still check the claim.
`docs/usb-xhci-info/usbport-miniport-abi.md` does this at length.

### 2a. The tracked binaries that are not only ours

There are two. Both are tools rather than the driver, and both are the
exception to the row above saying this project tracks only its own code:
`releases/<version>/xhciqual/XHCIQUAL.EXE` and
`releases/<version>/xhcisnap/XHCISNAP.EXE`. They are
not the same case: the qualifier carries an embedded DOS extender and an Open
Watcom runtime, the reader carries a statically linked Microsoft C runtime.

#### XHCIQUAL.EXE

`releases/<version>/xhciqual/XHCIQUAL.EXE` is built from `xhciqual/`'s own C
and assembly, but it is not only that:

- DOS/32 Advanced DOS Extender, embedded as the executable's stub.
  `xhciqual/makefile` links `system dos32a` and says so in its own comment:
  "`system dos32a` embeds the full DOS/32A extender as the EXE stub ... so
  XHCIQUAL.EXE is one standalone file - no DOS4GW.EXE to carry." The linker map
  states it independently at its head: "creating a DOS/32 Advanced DOS Extender
  (LE-style) executable". Static, read from the linker map and the makefile. The tracked copy of the
  map is `releases/<version>/xhciqual/XHCIQUAL.MAP`, which every release
  stages beside the executable; `xhciqual/xhciqual.map` is the build's own
  output and is git-ignored.
- Open Watcom C runtime, statically linked. `XHCIQUAL.MAP` names the modules
  individually against `C:\WATCOM\lib386\dos\clib3r.lib` (`_strcmp`,
  `strncmp.c`, `fopen.c`, and others). Static, read from the map.

What each one's own text says. DOS/32A ships its licence as `binw/license.d32`
in an Open Watcom install (Copyright (C) 1996-2006 by Narech K.). Quoting it
rather than characterising it: its clause 2 reads "Redistributions in binary
form must reproduce the above copyright notice, this list of conditions and the
following disclaimer in the documentation and/or other materials provided with
the distribution", and its clause 3 reads "The end-user documentation included
with the redistribution, if any, must include the following acknowledgment:
'This product uses DOS/32 Advanced DOS Extender technology.'"

The first two cuts carried neither of those texts in the release directory,
naming only this project's own licence. `scripts/package/make-release.ps1` now
generates `NOTICE.TXT` beside the executable reproducing the licence text in
full, including the acknowledgement sentence, and the qualifier's own readme
points at it.

For the Open Watcom runtime, `NOTICE.TXT` records the linkage and reproduces
the copyright lines the linker writes at the head of the map. Open Watcom is
distributed under the Sybase Open Watcom Public License.

Stated plainly: `XHCIQUAL.EXE` is a single executable combining this project's
GPL-2.0-only code, the DOS/32A extender as its stub, and the Open Watcom C
runtime. Three licences meet in one file (this project's, DOS/32A's and Open
Watcom's), and what ships beside it is `NOTICE.TXT`, reproducing the DOS/32A
text in full and recording the Watcom linkage. That composition is what this
section records; it draws no conclusion from it.

#### XHCISNAP.EXE

`releases/<version>/xhcisnap/XHCISNAP.EXE` is the Windows console reader. It
is built from `xhcisnap/xhcisnap.c` alone, but like the qualifier it is not
only that:

- Microsoft Visual C++ 6.0 C runtime, statically linked. `xhcisnap/build.cmd`
  compiles with `cl /nologo /W3 /WX /O2` and no `/MD`, so MSVC 6.0's default
  static runtime is bound into the image rather than reached through
  `MSVCRT.DLL` at run time. Static, and re-derivable without the toolchain
  that produced it: `link -dump -imports XHCISNAP.EXE` names `KERNEL32.dll`
  and `ADVAPI32.dll` and no C runtime DLL at all. The static link is
  intentional: it is what makes the tool one file that runs on a Windows 98
  SE machine with nothing installed on it, which is the machine it exists for.
- No DOS extender and no Open Watcom code. It is an ordinary Win32 console
  PE, so the qualifier's two items have no counterpart here and its
  `NOTICE.TXT` is not this one.

What its own text says: nothing is quoted here, and no text from that product
is reproduced anywhere in this tree. The runtime modules come from the MSVC
6.0 installation this repository uses in place out of `tools/MSVC600`, itself
untracked third-party material (section 2's row). The linkage is the fact;
`scripts/package/make-release.ps1` generates `NOTICE.TXT` beside the
executable recording it, in the same place and for the same reason as the
qualifier's. Both notices are generated at every cut, so a published directory
carries the one for each tool it holds.

Stated plainly: `XHCISNAP.EXE` is a single executable combining this project's
GPL-2.0-only code with Microsoft's Visual C++ 6.0 static C runtime, two
licences meeting in one file where the qualifier's three meet in the other.
That composition is what this subsection records.

In both cases the binary was published first and its provenance written down
afterwards, by an audit reading the linker map rather than by the cut itself.
Section 6's rule for a new tracked build output is written against that.

### The third-party PDFs

Both PDFs were tracked in this repository for a time. The xHCI specification
was moved twice, so between them they occupied four paths:

- `docs/extensible-host-controler-interface-usb-xhci.pdf` (the original,
  misspelled "controler")
- `docs/extensible-host-controller-interface-usb-xhci.pdf` (the spelling fix)
- `docs/references/extensible-host-controller-interface-usb-xhci.pdf` (the
  move into `docs/references/`)
- `docs/references/xhci-backwards-compatibility-testing-v1-7.pdf` (the USB-IF
  document, one path only)

No third-party PDF is tracked now, and the published repository starts from a
single commit, so no historical blob of either file exists to fetch. The check
walks every ref and must print nothing:

```sh
git log --all --pretty=format: --name-only | grep -i '\.pdf$' | sort -u
```

Their licence limits are recorded in `docs/references/README.md`: the Intel
specification reserves all rights and grants no licence in the document
itself, and the USB-IF test document is licensed "FOR INTERNAL USE ONLY". No
applicable public-redistribution permission is documented for either. That is
a statement about what this project has established, not a claim that no
permission could exist. Intel's xHCI Adopters Agreement §3.2, for instance,
contains a limited, non-transferable licence for an Adopter to reproduce the
final specification as necessary to exercise the agreement's patent rights,
and this project has not established that its maintainer is an Adopter or
that public Git redistribution would fall inside that grant.

### The Microsoft public symbol files

Since 2026-09-11 the static readings of the NT 5.x and 6.x `usbport.sys` and
`usbehci.sys` binaries are taken with Microsoft's own public symbol files
loaded, which name every function and turn a day of address arithmetic into
`x usbport!MPf_*` and `uf`. The files are third-party material of the same
standing as the binaries they describe: **never tracked**, fetched per machine
into `tools/symbols/` (git-ignored with the rest of `tools/`), identified
here so a fresh clone can fetch the same twelve. Each comes from
`https://msdl.microsoft.com/download/symbols/<name>/<GUID><age>/<name>`,
where the GUID and age are the ones in the binary's own `RSDS` debug
directory (`link /dump /headers`), which is also what makes the pairing
verifiable. `kd -z <binary> -y srv*<repo>\tools\symbols` loads them offline.

| Binary (`tools/<dir>-extracted/`) | PDB identifier (`<GUID><age>`) | Bytes |
|---|---|---|
| `vista-x86/usbport.sys` 6.0.6002.18005 | `usbport.pdb/553D0C30409B4BF3A2A62608E2E33B1D2` (a private PDB: function names, no types) | 528,384 |
| `vista-x86/usbehci.sys` | `usbehci.pdb/68D46028449A44D3ADB0A9AA8DB00F2D1` | 118,784 |
| `win7-x86/usbport.sys` 6.1.7601.17514 | `usbport.pdb/5224A6DF50AF4F28A0A7A60F3EA734812` (private, as above) | 462,848 |
| `win7-x86/usbehci.sys` | `usbehci.pdb/059832CE0FDF43FBA7C540F0AD9EF1C21` | 135,168 |
| `vista-x64/usbport.sys` | `usbport.pdb/ABEBA17DE81C48C09F886D815DFF01FF1` | 176,128 |
| `vista-x64/usbehci.sys` | `usbehci.pdb/6543AA7852F24BF699C29F10857682C11` | 94,208 |
| `win7-x64/usbport.sys` | `usbport.pdb/71BCD350024D47208D5EA216690A7BFF1` | 184,320 |
| `win7-x64/usbehci.sys` | `usbehci.pdb/7D5BF41B31524741AC677582C58169B71` | 102,400 |
| `winxpsp3/usbport.sys` | `usbport.pdb/6E97B8D0ED8C4CA8B7BF1E0924887F551` | 125,952 |
| `winxpsp3/usbehci.sys` | `usbehci.pdb/EFB2A9AC22FC4AB18588464EE6D6826B1` | 60,416 |
| `winxp64/usbport.sys` 5.2.3790.3959 | `usbport.pdb/1DAD07D17E804F0697F39D73557F32171` | 134,144 |
| `winxp64/usbehci.sys` | `usbehci.pdb/BE5328AB80DA4B609FE912C491C56EDC1` | 52,224 |

Windows 2000 SP4's and NUSB's `usbport.sys` were read without symbols, as
before. A symbol name is a fact about the binary like an offset is, and is
recorded under the same `static` tag; no function body is copied from any of
them.

---

## 3. ReactOS: used as interface documentation, not as source

ReactOS is GPL-2.0, and so is this project (GPL-2.0-only). The two are
combined nowhere, and the shared licence does not change that by one line.
What makes `src/` this project's own work is that it was written independently
against recorded interface facts; that is a statement about authorship, not
about whether two licences would have permitted a copy. The rules below are
unchanged by the licence, and so is what the overlap audit found.

What is taken from ReactOS is the description of an interface: structure
layouts, field offsets, function signatures, constants, and the call ordering
its implementation performs. Those facts are recorded in
`docs/usb-xhci-info/usbport-miniport-abi.md`, with `file:line` citations into
the pinned mirror (`reactos/reactos` commit
`0298e10d5d904a0230868be8f7bdf6436d589c62`). Code under `src/` is then written
independently against those facts.

Rules that are enforced by convention and by review, and that must stay
enforced:

- No ReactOS function body is copied into `src/`.
  `docs/usb-xhci-info/usbport-miniport-abi.md` contains no ReactOS function
  body either, and carries the do-not-copy rule at its head.
- `external/` is a read-only local cache. It is git-ignored and is not
  redistributed by this project.
- The overlap audit checked this mechanically and found no non-interface
  verbatim overlap between `src/` and the ReactOS, Linux, Haiku or FreeBSD
  mirrors. It did find 37 matching `#define`/typedef lines in
  `src/xhci_usbport.h`; those are the interface constants and signatures the
  miniport must declare in order to be callable at all, which is the category
  of thing this section is about. That is an audit result, not a legal
  conclusion.

The other three mirrors, Linux (GPL-2.0), FreeBSD (BSD 2-Clause) and Haiku
(MIT), are used only as cross-references for xHCI hardware behaviour, which is
independently specified by the xHCI specification itself. The same
do-not-copy rule applies to them.

They are not equal in weight. Linux is consulted routinely, above all for the
controller quirk population, which is the stated authority for
`xhciqual/quirks.c`.

FreeBSD and Haiku were consulted at two points, and neither carries anything
today: the 64-bit register write order is settled by the specification as a
`shall` (5.1, p.337), and the Route String tier order that once rested on the
two of them agreeing was settled by the controller itself in batch 7b-A, which
resolved this driver's own route strings to the physically correct devices on
both usbport builds (`docs/usb-xhci-info/xhci-data-structures.md`, "Route
String tier order"). They remain mirrored because a second opinion on someone
else's silicon is cheap to keep.

Demoting a reference does not narrow the audit above, which swept `src/`
against all four mirrors.

---

## 4. Facts read out of shipping binaries: a source-method inventory

This is the part most easily misread.

"Binary-confirmed" in this project's documents does not mean "observed at
runtime". It means "checked against the shipping binary", and the check was
almost always a static read of a disassembly listing. The distinction matters
legally and technically, so this inventory tags each fact with the method
actually used:

- static: the fact was read from a disassembly listing produced by
  `link -dump -disasm` (the Windows 2000 DDK's COFF dumper) or `dumpbin
  /exports` over an installed binary. Nothing was executed to establish it.
- runtime: the fact was observed while the machine was running, through
  counters and traces compiled into this project's own driver, or through
  property dumps this driver made of data usbport handed it.
- both: established statically and corroborated by running the system.

Which binaries. The miniport ABI work reads NUSB 3.3's `USBPORT.SYS`
5.00.2195.5652 (Windows 98 SE), Windows 2000 SP4's `USBPORT.SYS`
5.00.2195.6681, in a few places Windows XP SP3's `usbport.sys`, and since
2026-09-02 the SweetLow 5.1.2600.2180 rebuild for Windows 98 (below). Other
shipping binaries have been read statically for narrower questions and are not
individually inventoried below: both `usbehci.sys` builds (the periodic
`Period` derivation, abi §5), `usbhub20.sys` and the two `usbd.sys` builds
(import and load-gate work), and a function driver, `ax88772.sys` (a Phase 8
behaviour question).

Since 2026-09-08 the same work reads a fifth lineage, and the first 64-bit
one: the NT 5.2 amd64 `usbport.sys` and `usbehci.sys` of Windows XP
Professional x64 / Windows Server 2003 x64, both 5.2.3790.3959
(`srv03_sp2_rtm.070216-1710`). They were expanded from the `AMD64` directory
of the project owner's own Windows XP x64 SP2 media with 7-Zip and
`expand.exe` - routine unpacking, section 1's recorded case - into
git-ignored `tools/winxp64-extracted/`, together with that media's
`usbhub.sys` and its `usbport.inf`. Method **static** throughout: the export
table was read with `link /dump /exports` and the two functions below were
disassembled with `cdb.exe -z`, which loads a raw PE and executes nothing.
Neither MSVC 6.0's `dumpbin` nor WDK 7.1's `link /dump /disasm` can
disassemble amd64 here, which is why the tool differs from the one this
section names for the 32-bit work; `docs/contributing/design/11-x64-targets.md`
section 3 records the method and the four files' sizes and SHA-256s. What was
established (Phase 21 task 21.1, design record 11 section 5): the three
exports and their ordinals, that `USBPORT_GetHciMn` returns `0x10000001`,
and the registration function's version gate and its two copied packet sizes,
`0x250` and `0x230`. Nothing was executed; no file from that media is tracked.

A further reading was taken from the same two files on 2026-09-09 (task 21.4,
design record 11 section 5, **M7**): the amd64 layout of
`USBPORT_ENDPOINT_PROPERTIES`, read out of `usbehci.sys`'s `OpenEndpoint`,
`QueryEndpointRequirements` and the two endpoint-open paths below them - the
`sizeof` from the nine 8-byte moves that copy the structure, and nine field
offsets each from the instruction that reads it. Method **static**, with
`kd.exe -z` from WDK 7.1's amd64 debuggers, which loads a raw PE and executes
nothing; it is the same technique and the same debugger engine as the `cdb.exe
-z` above, and the reason a debugger is used at all is that neither MSVC 6.0's
`dumpbin` nor WDK 7.1's `link /dump /disasm` can disassemble amd64 on this
host - `msdis160.dll` is not present.

**A 64-bit binary of this driver now exists**, built on 2026-09-09 (task
21.2), and on the same day it installed and ran on a Windows XP x64 guest
(task 21.5) - which changes nothing about the tagging above. Every fact in
this paragraph and the one before it remains `static`, read from Microsoft's
shipped binaries. A running system is not corroboration of a reading: the
standing rule is that a static fact is not upgraded because the machine also
happens to work, and the one amd64 reading a runtime observation is entangled
with, **M8**, says so in its own row - the defect was seen at runtime, the
layout was read off the binary, and no fact rests on the run. The tier that
observation supports is stated in `AGENTS.md`, "Project Purpose", and the
tier does not change what any row here is tagged.

Three more files came off the same media on 2026-09-09 for task 21.3, and they
are a different kind of thing from the ABI readings: they are the *export
tables the import gate resolves against*, the amd64 counterpart of the Windows
2000 SP4 kernels and HALs already recorded here. `NTOSKRNL.EX_`,
`NTKRNLMP.EX_` and `HAL.DL_` were expanded from `AMD64\` with 7-Zip and
`expand.exe` into git-ignored `tools/winxp64-extracted/`, all three
5.2.3790.3959 (`srv03_sp2_rtm.070216-1710`), by
`scripts\import-gate\extract-target-baselines.ps1 -Amd64Iso`, which
authenticates each file against `scripts\import-gate\winxp64-baselines.expected`
by version, length and SHA-256 *before* staging it. Method **static**: only
the export tables are read, with `link /dump /exports`. **Three files and not
ten** - NT 5.2 amd64 ships one HAL where i386 ships eight, checked by listing
every name on the media beginning `HAL` rather than by a pattern, which is the
`HALBORG.DLL` lesson the i386 manifest records. Nothing from that media is
tracked; the manifest records the identities, which is what makes the claim
re-derivable without the files.

Since 2026-09-09 it reads two more lineages, in both architectures: the
`usbport.sys` and `usbehci.sys` of Windows Vista SP2 (6.0.6002.18005,
`lh_sp2rtm.090410-1830`) and Windows 7 Professional SP1 (6.1.7601.17514,
`win7sp1_rtm.101119-1850`), and - for the x86 pair only - those systems'
`ntoskrnl.exe` and `hal.dll`, whose export tables are what an import check
resolves against. All eight came out of the `sources\install.wim` of the project
owner's own retail media, mounted read-only with `Mount-DiskImage` and opened
with 7-Zip: routine unpacking of shipped media, section 1's recorded case, with
nothing copied out of the ISOs but the eight files and nothing written back.
They live git-ignored in `tools/vista-x86-extracted/`, `tools/vista-x64-extracted/`,
`tools/win7-x86-extracted/` and `tools/win7-x64-extracted/`. Method **static**
throughout, with the same tools as the NT 5.2 amd64 work: `link /dump` for
exports, headers and imports, and `cdb.exe -z` for the function bodies. What was
established (Phase 21 task 21.7, Phase 22 tasks 22.1 and 22.2; design record 11
sections 3 and 6, and the ABI document's "The 6.0 and 6.1 lineages"): the export
table gained a fourth name and the ordinals shifted; `USBPORT_GetHciMn` still
returns `0x10000001`; the registration gate and the `Version >= 200` packet size
are unchanged in both architectures; the `USBPORT_RESOURCES` prefix and the 50
miniport callback slots are unchanged; Windows 7 has a second, 64-bit DMA
adapter behind a `Version >= 310` gate; and every import the shipping 32-bit
`xhci98.sys` names resolves, in the right module, in both x86 kernels. Nothing
was executed, no guest was booted, and no file from that media is tracked.

Two further readings were taken from those same files on 2026-09-10, both
method **static** and both with `kd.exe -z`, which loads a raw PE and executes
nothing. The first (task 21.8, design record 11 section 6.1) is the arity of
`USBPORT_RegisterUSBPortDriver` - four arguments on NT 6.x against three on NT
5.x - read from the callee's use of the fourth argument register in
`vista-x64-extracted` and `win7-x64-extracted`, from the `ret` immediate in the
two x86 builds, and from the caller's side in Vista x64's own `usbehci.sys`.
The second (section 6.2) is the `USBPORT_RESOURCES.ResourcesTypes` bit
assignment, read out of `USBPORT_ParseResources` in all four of those
`usbport.sys` builds **and in `tools/winxp64-extracted/usbport.sys`**, which is
the NT 5.2 comparison the finding rests on: NT 6.x carries a second port bit
and moves interrupt and memory up one place each. Because that function is not
exported there was no symbol to disassemble by name, so the whole `.text`
section of each image was swept with `u <start> <end>` - bounds from `link
/dump /headers` - and the listing searched. Still nothing executed; the driver
was later rebuilt and its host tests run, which is this project's own code and
is not a reading of anyone's binary. **Both facts stay `static`.** A Vista x64
guest was running on 2026-09-10 and did report `0x0C` through this driver's own
log channel, which is a `runtime` observation of *this* driver's input and is
recorded as the symptom; the bit assignment itself is read off Microsoft's
binaries and is not upgraded by it.

Two more readings were taken on 2026-09-10 and 2026-09-11, both method
**static**, both with `kd.exe -z` and `link /dump`, nothing executed. The
first (task 21.8, design record 11 section 6.4) is the miniport interrupt DPC
slot, read in **six** `usbport.sys` builds - `vista-x64`, `win7-x64`,
`vista-x86`, `win7-x86`, and the two NT 5.x comparisons the finding rests on,
`winxp64` and `winxpsp3` - together with the version-gated copy length and the
interface allocation size in each. As with `USBPORT_ParseResources`, the
dispatch is not an exported function, so it was reached by following
`IoConnectInterrupt` to usbport's own ISR and on through `KeInsertQueueDpc` to
the DPC's `DeferredRoutine`; on the x86 builds part of that chain was located
by scanning `.text` for raw displacement bytes, because the relevant
`KeInitializeDpc` call goes through a register and no indirect-call scan can
see it. The second (section 6.3) is the WDM version each kernel reports, read
out of `IoIsWdmVersionAvailable` itself in `tools/winxp64-extracted/
ntoskrnl.exe` (1.30), `tools/vista-x86-extracted/ntoskrnl.exe` (6.00) and
`tools/win7-x86-extracted/ntoskrnl.exe` (6.00) - three kernels, five
instructions each, and the fact that corrected this driver's own version
predicate. **Both stay `static`.** The XP x64 guest booted on 2026-09-11 both
before and after that correction, and what it reported is a `runtime`
observation of *this* driver's behaviour, recorded as such; the version
constants themselves are read off Microsoft's binaries and are not upgraded by
it.

The toolchain that reads them is third-party material on the same terms.
WDK 7.1 (`GRMWDK_EN_7600_1.ISO`, 649,877,504 bytes, SHA-256
`5edc723b50ea28a070cad361dd0927df402b7a861a036bbcf11d27ebba77657d`, from the
owner's own media) is unpacked into git-ignored `tools/WinDDK71/` with
`msiexec /a`, an administrative install that lays out files and registers
nothing; verified the same day that no WDK or debugger entry appears in
installed programs and no `C:\WinDDK` exists. It is used in place and
distributed nowhere, exactly as `tools/MSVC600` and `tools/ntddk` are.
Whether it stays is design record 11's decision 3.

One more package has been read at the file level only: `nusb36e.exe` (NUSB
3.6, public download, kept git-ignored in `tools/` beside the 3.3 package).
Method static throughout: its files were extracted, hashed and
version-stamped, and its INFs read, to establish that its USB 2.0 stack is
byte-identical to 3.3's; nothing in it was disassembled.
`docs/usb-xhci-info/usbport-miniport-interface.md` section 5 records the
comparison. The one runtime observation involving it (a 2026-09-01 VM pass)
was taken through this project's own driver counters, not from the package's
binaries.

The Windows ME OEM CD image on the project owner's machine was read at the
file level on 2026-09-02, for the Windows ME target question: `layout.inf`,
`layout1.inf`, `layout2.inf`, `usb.inf` and `hiddev.inf` were extracted from
its `win9x\PRECOPY1.CAB` with 7-Zip and read as text. Nothing was executed
and nothing was disassembled, and no file from it is kept in this tree or
under `tools/`. The facts are in `docs/contributing/build-and-test.md`,
"Windows ME target VM".

The four target CDs were read at the file level again on 2026-09-07, for the
`usbui.dll` question: each one's `layout.inf` (from `PRECOPY1.CAB` on the two
9x CDs, and `I386\LAYOUT.INF`, which the NT CDs carry uncompressed), and the
`usbui.dll` and `sysclass.dll` files themselves, extracted with 7-Zip and
`expand.exe`. Method static: the PE export and import tables were read with a
parser, nothing was disassembled and nothing was executed. What it established
is in `docs/contributing/build-and-test.md`, "The files the OS supplies" -
four distinct per-OS `usbui.dll` builds, each exporting
`USBControllerPropPageProvider` and `USBHubPropPageProvider`, and a 9x
`sysclass.dll` that is a 16-bit NE module carrying the string `usbui.dll`. No
file from any of those CDs is kept in this tree or under `tools/`; the copies
read were staged into a scratch directory outside the repository.

Separately, and not a binary-derived fact at all: the Device Manager behaviour
those files drive was observed the same day in the four target virtual
machines, by opening property sheets with the file present and with it absent.
Those are observations of Windows' own user interface, made with this driver
installed but not through its counters or its traces, so they carry neither
the static nor the runtime tag defined above. `build-and-test.md` records them
as guest readings, naming the guest each came from.

A further package has been read, statically and at run time, on 2026-09-02:
SweetLow's USB 2.0 stack for Windows 98, `usb20_win9x.zip`, from the download
link its author gave the project owner
(`http://sweetlow.orgfree.com/download/usb20_win9x.zip`; the zip is kept
git-ignored in `tools/` and its extraction in `tools/sweetlow-extracted/`
with a README recording URL, sizes, versions and SHA-256s). The same five
binaries had been fetched earlier that day from Windows 98 QuickInstall's
driver library (`oerg866/win98-driver-lib-base`, `[MBD]_sweetlow_usb2.0`,
commit `5ef7f88e`, a public GitHub repository) and hash identical. Its `USBPORT.SYS` is a 5.1.2600.2180
build carrying the resource string "built by: WinDDK", so the file is a
rebuild from XP SP2-level sources rather than a Microsoft-shipped binary; how
those sources were obtained is not recorded here, and this project has not
asked. Method: `dumpbin /exports`, `/imports` and `/disasm` over the port
driver, the miniport and the hub driver as fetched, with the two exported
routines' bodies extracted to `usbport-registration-disasm.txt`; a UTF-16
string scan for registry value names; and one VM session in which this
driver's own trace recorded the registration values and the controller
lifecycle. Nothing was patched. `docs/usb-xhci-info/usbport-miniport-interface.md`
section 5 holds the record. The package is not tracked and not carried in the
release download; the three-file exception in section 5 is unchanged, and the
stack is referred to by its upstream repository only.

Where those copies came from, since "read as installed" is not the whole
story: NUSB's binaries from the publicly-distributed `nusb33e.exe` package;
Windows 98 SE binaries from the installed guest; and the Windows 2000 SP4 and
Windows XP SP3 binaries from installation media (`I386\USBPORT.SY_`,
`USBEHCI.SY_` and `USBD.SY_` off the SP4-integrated `win2ksp4.ISO` and a retail
XP Pro SP3 OEM ISO), extracted with `7z e` and expanded with `expand.exe`
(`docs/usb-xhci-info/usbport-miniport-interface.md` section 5,
`scripts/package/extract-usbd-sources.ps1`). For the SP4 build the media copy
was checked against the installed guest's own copy and matches byte for byte.
Every copy read came from one of those three places: a public download, an
install on this project's own machine, or the maintainer's own install media.

The listings produced from them live under `tools/*-extracted/` and are
git-ignored; they are convenience copies, not the record.

Scope of the table. It indexes the `usbport.sys` miniport ABI facts: the
contracts `src/` is written against, and the ones that would be
unre-derivable without a binary. It is an index into
`docs/usb-xhci-info/usbport-miniport-abi.md` by method, not an exhaustive list
of every observation this project has ever made about a third-party binary;
the roadmap's per-task boxes hold the rest, each with its own method stated.

"abi §N" below means section N of `docs/usb-xhci-info/usbport-miniport-abi.md`.

| Fact | Method | Where recorded |
|---|---|---|
| SweetLow's `USBPORT.SYS` has the same unguarded single-TT lookup: `CreateDevice` calls `0x26628` at `0x26B1D`; a zero count takes `0x2667A` and the empty list becomes `0xFFFFFFEC` at `0x26686` | static: MSVC 6 `dumpbin /disasm` on the 134,912-byte file, SHA-256 `8A3C9F1B568CB25CF5DD9AF3AF9E5C3400DE24BD087CAA3E4E3345588F5CFB56`; no truthful-speed execution | abi section 8; issue 06 section 6 |
| `USBPORT_GetHciMn` present at ordinal 2, `USBPORT_RegisterUSBPortDriver` at 3, plus an undocumented `DllUnload` at 1; `usbehci.sys` imports only the first two | static (`dumpbin /exports`) | abi §1 |
| `USBPORT_REGISTRATION_PACKET` layout identical across all three builds (Phase 3 task 1) | static | abi §3 |
| The SweetLow WinDDK rebuild (5.1.2600.2180, Windows 98) has the same three exports and ordinals, the same `>= 100` / `>= 200` gate, the 300/316-byte copy, the 0x150 wrapper with `Version` at +0x10 and the packet at +0x14, writes the same 16 service pointers, and returns `0x10000001` from `USBPORT_GetHciMn` | both: read from `tools/sweetlow-extracted/usbport-registration-disasm.txt`, then `USBPORT_GetHciMn=10000001` and `packet size=0000013C` in this driver's trace on the `2a-sweetlow` guest | interface doc section 5, "The SweetLow rebuild" |
| Under that build, Windows 98 completes the controller stop (`DisableInterrupts`, `StopController(TRUE)`) on disable, Remove and reinstall, where both 5.00.2195 builds on Windows 98 bugcheck at `0028:C00312EE` after `RH_DisableIrq` | runtime (this driver's trace, QEMU only) | `docs/contributing/lessons.md`, "The Windows 98 teardown bugcheck belongs to the Windows 2000-lineage usbport" |
| The `USBPORT_MINIPORT_INTERFACE` wrapper differs per build: packet at +0x14 (Win2000/XP) vs +0x10 (NUSB), because NUSB's wrapper has no `Version` field, so an interface offset is not a packet offset | static | abi §3, and the `FlushInterrupts` box in §4 |
| Registration version gating: `TakePortControl` additionally gated on interface `Version >= 200` in the Win2000/XP builds | static | abi §4 |
| `UsbPortBugCheck` is `KeBugCheckEx(0xD2, 0, 0, 0, 0)`; all four parameters hard zero; the extension argument is pushed and never read | static | abi §5 |
| `UsbPortRequestAsyncCallback` callbacks run at DISPATCH_LEVEL holding neither miniport lock, and cannot be cancelled | static (NUSB `0002785E`) | abi §5 and the locking table |
| `ResetController` runs at DISPATCH_LEVEL inside a usbport spin lock (so `UsbPortWait` is illegal there); `UsbPortInvalidateController(RESET)` queues a DPC rather than acting inline | static | abi §4 |
| Common buffer: 32-bit DMA adapter, page-aligned `StartVA`/`StartPA`, `ROUND_TO_PAGES(size + 0x30)` with usbport's 48-byte header at the end, block zeroed before `StartController`, and `CacheEnabled = TRUE` | static | abi §4; consequences in design doc 04 |
| Scatter/gather elements are page-granular: `ElementLength = 0x1000 - (PA.LowPart & 0xFFF)` clamped to the remaining length | static | abi §5 |
| SG element ordering versus `SgOffset` | not claimed; a static pass cannot establish it | abi open item 8 |
| `TransferParameters` is an interior pointer of the transfer record; `SubmitTransfer` preconditions (list pointer never NULL, `SgElementCount == 0` legal) | static | abi §4 |
| Post-`SubmitTransfer` lifetime: usbport's post-callback writes happen after it releases the miniport lock, and the completion path takes no lock ordering it behind them | static | abi §4 |
| `AbortTransfer` post-return lifetime: usbport retains nothing, the record is `ExFreePool`d in the same worker pass, and the miniport extension is interior to the freed block | static | abi §4 |
| `ENDPOINT_FLAG_NUKE` is a controller-teardown flag; on that path usbport completes and frees transfers with no miniport callback | static, with the whole-image negative enumerated so it is checkable without the files | abi §4 |
| The `USBPORT_GetTt` defect: `USBPORT_CreateDevice` gates the TT lookup on `USB_MINIPORT_FLAGS_USB2` and not-High-Speed; `USBPORT_GetTt`'s single-TT branch has no empty-list guard and returns `0xFFFFFFEC`, which `OpenPipe`'s null check passes, bugchecking in `ExfInterlockedInsertTailList` | both: the bugcheck was observed on both targets first, then read out of the instructions | abi §8 ("The transaction-translator lookup, and why `USB_MINIPORT_FLAGS_USB2` must be set"); the resulting untruth is in `docs/contributing/implementation-invariants.md`, "Root Hub Reporting" |
| Hub-descriptor request shape, and `PowerOnToPowerGood` copied straight through and truncated to a UCHAR (so 20 ms encodes as 10) | static | abi open item 7, root-hub block in §4 |
| Root-hub `RH_DisableIrq`/`RH_EnableIrq` lifecycle: a close is not guaranteed a matching open; per-build addresses recorded | static | abi §4 |
| `RH_SetFeatureUSB2PortPower`'s helper drops its lock before the callback (correcting an earlier wrong claim); caller-held locking in general remains unverified | static | abi §4 |
| The isochronous block: `SubmitIsoTransfer` at packet slot 0x54, the parameter builder and the completion consumer identical across both builds bar three known private offsets, no version gate | static | abi §4 |
| `FlushInterrupts` call site: the device-power completion routine on the `PowerDeviceD0` path, holding neither miniport lock | static, and explicitly not by waiting for a trace (an earlier "remains unreached" was a statement about Phase 3's traces misread as a statement about the binaries) | abi §4 |
| `InterruptNextSOF`: two call sites per build, both the endpoint state-change machine, DISPATCH under `MiniportSpinLock`, and nothing waits on it | static | abi §4 |
| Callback reachability: which slots are actually called on a live target, e.g. `PollController` called repeatedly from bind onward | runtime (Phase 3 traces) | abi open item, §7 |
| `FlushInterrupts` and `InterruptNextSOF` occurrence corroborated by `XHCI_EXTENSION.InterruptFlushes` and `InterruptNextSofRequests` counters in a release build | runtime, corroborating a static reading | abi open item, §7 |
| `HubAddr`/`PortNumber` property values: `PortNumber` names the root-hub port for a device on a root port, and is not a TT-only field | runtime (batch 6-V VM runs, on both builds): a property dump of what usbport handed this driver | abi §5 |
| `SetEndpointState` is edge-triggered on usbport's own recorded state and skips the miniport call when they match | static | abi §4 |
| `UsbPortGetMiniportRegistryKeyValue` (packet slot 0xF0): populated in both builds; six stack arguments (`ret 18h`); the `BOOL` selects `IoOpenDeviceRegistryKey`'s key type (FALSE = hardware, TRUE = driver/software); the `PCWSTR` is a value name and the first `SIZE_T` its byte length including the NUL; the reader is instruction-for-instruction identical across the two images; the return collapses to `(ntStatus == 0) ? 0 : 8`; PASSIVE_LEVEL only (`IoOpenDeviceRegistryKey`, `ZwQueryValueKey`, PagedPool) | static | abi §6, task 11-V.7 box |
| The shipping `usbehci.sys` calls that slot exactly once per image, with `BOOL = TRUE` and a 4-byte read of `L"EnIdleEndpointSupport"`, from inside `StartController` | static (both builds) | abi §6, task 11-V.7 box |
| Controller-lifecycle census: whole-image enumeration of usbport's slot calls finds exactly three `StartController` and three `StopController` call sites per build and exactly one `ResetController`; every direct caller chain out of the five routines holding them terminates at an `IRP_MJ_PNP` or `IRP_MJ_POWER` handler (plus, in the Win2000/XP build only, an HCD IOCTL that requests a power transition); and the reset DPC's body arms nothing after calling the slot. A transitive whole-image negative was attempted and is explicitly not claimed: indirect transfers are unclassified, and the attempt produced a known false edge. The only producer of `UsbPortInvalidateController(RESET)` is a miniport: usbport's one internal call site passes `SURPRISE_REMOVE` | static (both builds; the commands, the instruction pair enumerated, and every per-build address recorded, so the census is re-runnable without the files) | abi §4, the two notes after the `UsbPortInvalidateController(RESET)` box |
| What drives `PassThru` (packet slot 0xE0): the user-mode escape is `IOCTL_USB_USER_REQUEST` `0x00220438`, METHOD_BUFFERED, `UsbUserRequest == 3`, reached through the `\DosDevices\HCD<n>` symbolic link the HCD FDO's start path creates; the buffer contract, the non-paged copy usbport hands the callback, PASSIVE_LEVEL with no usbport lock held, and the second site being a test-mode-only internal probe whose fallback to `RH_GetPortStatus` fires only on a return of exactly 6 | both. Static for all of it, from the binaries' own comparison chains rather than from a header (the Win2000 DDK here has no `usbuser.h`). Runtime on the Windows 98 target, on the NUSB 5652 build this was read out of, in the 2a guest: the link opens, the round trip completes, the four `-probe` controls return 0 / 2 / 4 / 7, and the driver's own trace carries `cb PassThru` lines. Runtime on Windows 2000 as well: in the 2b guest, against SP4's own `usbport.sys` 6681, the link opens, the round trip completes, and the four `-probe` controls return the same 0 / 2 / 4 / 7. So the structural reading of the SP4 binary is an observation on both targets, and the two builds answer this escape identically at run time as well as in their comparison chains. The same run also measured the route's one limit: usbport builds its link at a fixed index with no retry, so on a machine where Windows 98's own USB stack already owns that name no usbport link appears at all | abi §4, "Debug / single-packet" box |
| SP4's native `usbhub.sys` (5.00.2195.6689, 40,176 B, from the fresh Windows 2000 image) carries no `SelectiveSuspend`, `DisableSelectiveSuspend` or `IdleNotification` string, ASCII or UTF-16; SP4's `usbport.sys` (5.00.2195.6681) carries `DisableSelectiveSuspend` (3), `HcDisableSelectiveSuspend` (2) and `SelectiveSuspend` (3) | static (a string search over the files extracted from the image; nothing disassembled, nothing executed) | build-and-test.md, "A replug onto an idle-suspended controller"; F18's Windows 2000 reading |
| NUSB 3.6's `usbhub20.sys` (5.00.2195.6891, 50,032 B) carries `DisableSelectiveSuspend` (1) and `SelectiveSuspend` (1); NUSB 3.6's `usbhub.sys` (4.90.3002.1) carries none; NUSB 3.6's `usbport.sys` (5.00.2195.5652) carries the same three names as SP4's | static (string search, as above) | the same site: an unconfirmed candidate explanation for the differing bounded idle observations on the two targets (`build-and-test.md`) |
| Windows 98 SE's `usbd.sys` 4.10.2222 (18,912 B) exports `USBD_ParseConfigurationDescriptor` and `USBD_CreateConfigurationRequestEx` but not `USBD_ParseDescriptors`; NUSB 3.6's Windows ME `usbd.sys` 4.90.3000.1 (22,928 B) exports `USBD_ParseDescriptors` and `USBD_ParseConfigurationDescriptorEx` as well | static (`dumpbin /exports`), corroborated at runtime by a third-party filter that imports the symbol failing to load (Code 2) under the first file and loading under the second | `docs/issues/06-full-speed-root-port-bugcheck.md` section 4; the run record is `out\post-release\issue4-hidusbf\README.md` (git-ignored, this host) |
| SweetLow's hidusbf Windows 9x lower filter (`hidusbf.sys` 1.2.0.10, 3,648 B, a public-domain third-party binary, not tracked): reads a `bInterval` DWORD from the device's driver key at `AddDevice` and rewrites the interrupt endpoints' `bInterval` in the `URB_FUNCTION_SELECT_CONFIGURATION` configuration descriptor before passing it down; imports `USBD.SYS!_USBD_ParseDescriptors@16` | static (`dumpbin /disasm`, `/imports`), corroborated at runtime by the `Period` this driver received changing with the value | same |
| Windows XP Professional x64 SP2's `AMD64\LAYOUT.INF`: `usbport.sys = 100,,212480,,,,4_,4,1,3,,1,4`, `usbhub.sys = 100,,102400,...`, `usbd.sys = 1,,222222,...`, `usbui.dll = 1,,222222,,,,,2,1,3`, and no `usbhub20.sys` row at all; `[SourceDisksNames.amd64]` gives disk 1 = `\amd64` on the base CD and disk 100 = `\amd64` on the service-pack source, so the first two resolve to `AMD64\SP2.CAB` and the last two to `AMD64\DRIVER.CAB`. The four files there are 212,480 B, 102,400 B, 7,552 B (5.2.3790.1830) and 123,392 B (5.2.3790.1830); the first three sizes match `tools/winxp64-extracted/` exactly, which is what authenticates the reading | static (7-Zip on the owner's own ISO and on the two cabinets; `expand` not needed, `LAYOUT.INF` is uncompressed on this medium; nothing executed) | `src/xhci98-amd64.inf`'s header block; `build-and-test.md`, "The files the OS supplies"; roadmap task 21.3 |
| `usbui.dll` is absent from a stock Windows XP x64 install, exactly as it is from 32-bit XP: `7z l vm\winxp64.img -r usbui.dll` on the task 21.5 guest's clean-install snapshot returns zero files. Both XP media give the row the `2,1,3` text-mode disposition, "do not copy at Setup", so a USB controller's own install is the only thing that places it | static (a listing through the qcow2's MBR and NTFS; the guest was not booted) | `src/xhci98-amd64.inf`'s header block; roadmap task 21.3 |
| `usbui.dll` IS present on a stock Vista and Windows 7 install, in both architectures, so the `LayoutFile` row those targets have no equivalent of is never consulted there: Vista SP2 x86 `Windows\System32\usbui.dll` 83,456 B 6.0.6001.18000; Vista SP2 x64 `System32` 104,960 B 6.0.6000.16386 and `SysWOW64` 83,456 B 6.0.6001.18000; Windows 7 SP1 x86 80,896 B and x64 `System32` 101,376 B / `SysWOW64` 80,896 B, all 6.1.7600.16385. Read from image 1 of each `install.wim`, which is the applied image, so presence there is presence on an install. Whether the other three OS-supplied files behave the same way on those targets is NOT read | static (7-Zip on the owner's own mounted ISOs; nothing executed) | Phase 22's open question about the `usbui.dll` row on 6.0/6.1 |
| **M8** - the amd64 `USBPORT_SCATTER_GATHER_LIST` and its element, read off the producer in NT 5.2 amd64 `usbport.sys` 5.2.3790.3959 at RVA `0xF468`: `lea rdi,[rsi+118h]` (the list, inside usbport's private transfer record), `mov dword ptr [rdi],r12d` (`Flags` 0x00), `mov qword ptr [rdi+8],rcx` (`CurrentVa` 0x08), `mov qword ptr [rdi+10h],rax` (`MappedSystemVa` 0x10), `mov dword ptr [rdi+18h],r12d` and `inc dword ptr [rdi+18h]` (`SgElementCount` 0x18), `lea rbx,[rdi+20h]` (`SgElement[0]` at **0x20**), `add rbx,18h` (stride 24), `mov qword ptr [rbx],rax` (address, element 0x00), `mov dword ptr [rbx-8],r8d` (`SgTransferLength`, element **0x10**), `mov dword ptr [rbx+14h],r11d` (`SgOffset`, element **0x14**); the 4 KB split survives as `and edx,0FFFh` / `mov r8d,1000h` / `sub r8d,edx` / `cmova r8d,r9d`. Element `sizeof` is 24 on both architectures and the list is `0x50` either way, so neither size moved while three offsets did | static (`tools\WinDDK71\Debuggers\kd.exe -z` on the extracted image, function boundaries taken from `.pdata`; nothing executed. The defect it explains was observed at runtime on the task 21.5 guest, but no fact in this row rests on that) | `usbport-miniport-abi.md`, "The amd64 scatter-gather layout (M8)"; design record 11 section 5 M8; `src/xhci_usbport.h` |
| **The Version 300 tier of `USBPORT_REGISTRATION_PACKET`, slot by slot, on NT 6.x x86**: twelve `ULONG`s at `0x13C`-`0x168` then 29 pointers at `0x16C`-`0x1DC`; the count at `0x13C` read by `USBPORT_AllocateControllerCommonBuffers` behind `MPx_MpRevision >= 300` (vista-x86 `0x2C4C3`/`0x2C4D4`, win7-x86 `0x25C1D`/`0x25C2E`), capped at 8, the eight sizes after it, the three USBX context sizes at `0x160`/`0x164`/`0x168` read by `MPx_HsbControllerContextSize`/`MPx_HsbTtContextSize`/`MPx_HsbEndpointContextSize` behind `MiniPortFlags & 0x400`; every pointer slot NULL-checked by its `usbport!MPf_<Name>` wrapper and the named ones also behind `interface+0x10 >= 0x12C` (vista-x86 wrappers `MPf_InterruptDpcEx 0x12C1E`, `MPf_ReleasePortControl 0x3111D`, `MPf_ReadCfgFlag 0x2631F`, `MPf_SetWakeOnConnect 0x3114F`, `MPf_NotifyTransferQueueState 0x1554A`, `MPf_CheckHwSync 0x30E39`, `MPf_CreateDeviceData 0x2D0FB`, `MPf_DeleteDeviceData 0x311D3`, `MPf_DbgFreeEndpoint 0x1F2BB`, `MPf_HaltController 0x30AC7`, `MPf_Get32BitMicroFrameNumber 0x309DC`, the ten `MPf_Usbx*`; win7-x86 `0x1182D`, `0x2D0DE`, `0x230F2`, `0x1E6DC`, `0x18C4B`, `0x2CDFC`, `0x26F32`, `0x2D154`, `0x1C8DF`, `0x2CADC`, `0x2CA01`); the two OUT services `UsbPortRequestAsyncCallbackEx`/`UsbPortCancelAsyncCallback` stored at `0x1B0`/`0x1B4` by `USBPORT_RegisterUSBPortDriver` at `>= 300` (vista-x86 `0x2DF17`/`0x2DF21`); no reader anywhere in either `.text` for `0x198`, `0x1B8`, `0x1BC`, `0x1C0`, `0x1D0`, `0x1D4`; slot names from the `usbehci.sys` `DriverEntry` stores (vista-x86 `0x16724`, win7-x86 `0x67B0`) | static (`kd -z ... -y srv*tools\symbols`, `x usbport!MPf_*`, `uf` per wrapper, a whole-`.text` `u` sweep grepped for every `[reg+NNNh]` in the tier; Microsoft public PDBs, section 2) | abi §1 "The Version 300 tier, slot by slot"; design 11 §6.5 |
| **The same tier on NT 6.x amd64 follows `0x250 + (X - 0x13C)` for the twelve `ULONG`s and `0x280 + (X - 0x16C) * 2` for the pointers, ending at `0x368`**, confirmed at every site rather than derived: count read at `interface+0x288` (vista-x64 `0x13CED`, win7-x64 `0x137C1`), `InterruptDpcEx` at `interface+0x2D0` in `USBPORT_IsrDpc` (`0x47742` / `0x3D548`), `NotifyTransferQueueState` `+0x2D8` (`0x11223` / `0x111BB`), `CheckHwSync` `+0x2E0` (`0x18D81` / `0x1766A`), `SetWakeOnConnect` `+0x2C8`, `ReadCfgFlag` `+0x2C0`, `ReleasePortControl` `+0x2B8`, `CreateDeviceData` `+0x368` (`0x2EDD9` / `0x27FA3`), `DeleteDeviceData` `+0x370` (`0x3005D` / `0x28CE4`), `DbgFreeEndpoint` `+0x378` (`0x119A2` / `0x1193D`), `HaltController` `+0x390` (`0x2DD57` / `0x27265`), `Get32BitMicroFrameNumber` `+0x398` (`0x21594` / `0x1D494`), the context sizes `+0x2AC`/`+0x2B0`/`+0x2B4`, the OUT services at packet `0x308`/`0x310` (vista-x64 `0x436F3`/`0x43702`) and `0x370` at 310 (`0x43745`); `MPf_CloseEndpoint`, `MPx_OpenEndpoint`, `MPx_PokeEndpoint`, `MPx_InitializeHsbController`, `MPx_QueryEpBandwidthData` and the inlined callers all test `interface+0x20` against `12Ch` | static (as above, on `tools/vista-x64-extracted/` and `tools/win7-x64-extracted/`) | abi §1 same section |
| **The `InterruptDpcEx` contract**: consumer `USBPORT_IsrDpc` acquires the device-extension DPC lock at DPC level (vista-x86 `0x11EDB`, win7-x86 `0x12D85`), passes device-extension flag bit 5 as `BOOLEAN enableInterrupts` (`shr al,5 / and al,1`, `0x11F8F`), calls the slot (`0x11F9B` / `0x12E15`), and reads only bits 0 and 1 of the `ULONG` result: `test al,3` (`0x11FA0` / `0x12E1D`) clears device-extension flag `0x10000`, and after the lock is released the same test (`0x12045`) gates `USBPORT_Ev_Rh_IntrEp_Invalidate` (`0x1204C` / `0x12E96`); producer Vista x86 `usbehci!EHCI_InterruptDpcEx` (`0x16A44`) returns 2 when a port-change pass found `PORTSC & 0x2A` on a port (`0x16BCE`), 1 when the transfer interrupt fired with `extension+0xC` non-zero (`0x16AFF`), else 0, and rewrites the interrupt-enable register at `+0x8` when `enableInterrupts` is set (`0x16BF7`); `usbehci!EHCI_InterruptDpc` (the `0x4C` slot) is a thunk `mov edi,edi / push ebp / mov ebp,esp / pop ebp / jmp EHCI_InterruptDpcEx` at `0x16C0C` (Vista) and `0x16D50` (Windows 7); no read of `interface+0x68` (x86) or `+0xA8` (amd64) follows an interface load in any of the four NT 6.x `.text` sections | static (as above) | abi §1 same section; design 11 §6.5 |
| **`MiniPortFlags` as Microsoft sends it and as NT 6.x tests it**: `usbehci.sys` `DriverEntry` stores `0x295` with `Version = 200` on XP SP3 and XP x64, `0xA95` with `Version = 310` (`push 136h` / `mov edx,136h`) on Vista x86 and x64, `0x80A95` at 310 on Windows 7 x86 and x64; on the interface copy NT 6.x tests `0x10` (many), `0x40` (`USBPORT_MpInterrupts`, `shr eax,6`), `0x400` (the `MPx_*` family and the context sizes), `0x800` (`USBPORT_OpenEndpoint`, vista-x86 `0x1FB05`: endpoint flag `0x2000` in place of `0x200`, which `USBPORT_ProcessNeoStateChangeList` `0x11478` routes to `MPf_CheckHwSync`), `0x4000` (`USBPORT_RootHub_Endpoint0`, vista-x86 `0x20836`, win7-x86 `0x1D29A`, the only path to `USBPORT_RootHub_AsyncCommand`, which calls `interface+0x1FC` = packet `0x1E0` with no NULL check at `0x3E241` / `0x32E6D`; amd64 `0x3BF5C` / `0x31904` through `+0x3A0`), `0x10000` (`MPf_CloseEndpoint`, `shr ecx,10h`), `0x40000` (`USBPORT_Core_UsbHcIntDpc_Worker` `0x12386`), and on Windows 7 `0x80000` on a local copy (`MPf_SuspendController` `0x1E5D5`, `USBPORT_SyncPowerAndChirpUsb2Ports` `0x2B118`) | static (as above) | abi §1 same section |
| **`MPf_CloseEndpoint` is the only call through the `CloseEndpoint` slot (`interface+0x50`) in Vista x86 and Windows 7 x86 `usbport.sys`, and it tests `interface+0x10 >= 12Ch` first** (vista-x86 `0x1F33F`, win7-x86 in `0x1C952`), as do `MPf_RebalanceEndpoint` (`0x31214` / in `0x2D186`) and `MPf_TakePortControl` (`0x262FB` / in `0x21010`) before reading packet `0x124` and `0x130`; the amd64 `MPf_CloseEndpoint` carries the same compare | static (as above) | abi §1 same section; design 11 §6.5 |
| **The NT 5.x control for the 300 tier**: the only compares against `64h`, `0C8h`, `12Ch` or `136h` in the whole `.text` of XP SP3 `usbport.sys` (`.text` `0x10380`-`0x2DA00`) are `USBPORT_RegisterUSBPortDriver` (`64h`, `0C8h`) and `[+10h],0C8h` in `USBPORT_PoRequestCompletion` and `USBPORT_RootHub_PowerAndChirpAllCcPorts`; XP x64 the same three functions (`.text` `0x11000`-`0x39384`); Windows 2000 SP4 `USBPORT.SYS` (`0x10300`-`0x2CA07`) two `[+10h],0C8h` plus `cmp eax,64h` / `0C8h`; NUSB `USBPORT.SYS` (`0x10300`-`0x2C227`) `[esp+14h],64h` / `0C8h` only - **no NT 5.x or 9x-era usbport tests for 300 or 310** | static (`kd -z ... u <text start> <text end>` swept with `grep`) | abi §1 same section; design 11 §6.5 |
| **XP x64 `usbport.sys` (timestamp `45D69800`) list heads at device-extension `+0x988` and `+0x9A8`**: `+0x988` under the lock at `+0x460`, element `LIST_ENTRY` at `+0x78`, reference `lock inc dword ptr [rbx+8]` on a match (walk at `0x1d140`, fault site `0x1d1a7`); nine displacement-`0x988` hits, all 64-bit (self-link init `0x13dc3`, insert-tail `0x1d3ab`, load `0x2010`, walks `0x1d183`/`0x1d2ec`/`0x1f652`/`0x1f6ed`/`0x206ad`/`0x20ff5`), no `dword` store; `+0x9A8` under the lock at `+0x400`, remove-head in `0x100b0` faulting at `0x101c3`, element `LIST_ENTRY` at `+0x48`; no displacement `0x988` in the amd64 `xhci98.sys` and no `-0x2A0` in either binary. In the 2026-09-13 crash-2 kernel dump: device extension = `[FDO+0x40]`, `MiniportExtension` = `devext+0xC28`, `USBPORT_RESOURCES` = `devext+0x378`, and the `+0x988` head's `Flink` = `fffffadf'00000000` with all four elements' links consistent | static (byte search, `kd -z ... uf`) for the code; runtime (`kd` on the guest's own `MEMORY.DMP`) for the dump readings | issue 8 §2-§5 |

Where a fact is inferred rather than read, this project's documents say so,
and several of them record refutations of earlier readings. That habit is what
makes the inventory above trustworthy; keep it.

The complete, citation-level record is
`docs/usb-xhci-info/usbport-miniport-abi.md`. This table is an index into it
by method, not a replacement for it.

---
| **NT 6.x `USBPORTSVC_CompleteTransfer` assumes its caller holds usbport's EpList lock**: the service acquires nothing (win7-x64 RVA `154dc`: `AssertSig`, the `cmpT` ring record, `AssertSig`, `EndpointFromHandle`, `Core_iCompleteTransfer`, `WmiLogEvent`; win7-x86 RVA `8d32` the same shape) and `USBPORT_Core_iCompleteTransfer` ends in `USBPORT_Xdpc_iSignal` (win7-x64 `+d9`, win7-x86 `+ae`), which on state 1 calls `KeInsertQueueDpc` then `Xdpc_iSetState(.., 7, 2)` with no lock; `USBPORT_Xdpc_Worker` calls `USBPORT_TxAcquireLock` = `USBPORT_AcquireEpListLock` (`KeAcquireSpinLockRaiseToDpc` on fdo+0xF88 on x64) before reading the state at obj+0xFC (x64) / +0x9C (x86) and enters the worker function with it held; `USBPORT_Xdpc_Signal` is the locked wrapper used by `IsrDpc` and `DM_IoTimerDpc`; `IsrDpc` calls the miniport's InterruptDpcEx under the fdo+0x1028 lock only; `Xdpc_InitDpc` is `KeInitializeDpc` with no target processor; `USBPORT_RootHub_ClassCommand` calls `RH_GetPortStatus` (x86 `+514`, packet `+0x98`) and `RH_GetHubStatus` (`+540`) directly with no lock while `USBPORT_RootHub_PortRequest` wraps the Set/ClearFeature slots in `USBPORT_AcquireSpinLock`; `USBPORT_Core_UsbHcIntDpc_Worker` reaches `MPf_PollEndpoint` via `iSetGlobalEndpointStateTx+7bc` once per active endpoint per pass; `USBPORTSVC_InvalidateEndpoint` on win7-x64 is `AssertSig` only | static (capstone 5.0 over `tools/win7-x86-extracted/` and `tools/win7-x64-extracted/` `usbport.sys` with the public PDBs `5224A6DF...` age 12 and `71BCD350...` age 1 via `dbh.exe enum`; `scripts/local/usbport-disasm.py`); the IRQL of the root-hub queries is **runtime** from this driver's own callback log on the Windows 7 x86 and x64 guests (`cb RH_GetPortStatus irql=00`) | issue 07 section 7; design 05 section 7 "Where a completion may be handed over"; abi "Completion path" |
| **Windows 7 x64 `usbehci.sys` calls the CompleteTransfer slot (`RegistrationPacket+0x1D0`) only from `EHCI_ProcessDoneAsyncTd+20f` and `EHCI_sMode_PollEndpointSlot+51e/+57c`; `EHCI_InterruptDpcEx` calls `+0x1C8` (InvalidateEndpoint) and `+0x308`; `EHCI_CheckController` calls `+0x208`** - every `call qword ptr [rip+disp]` in `.text` resolving into `RegistrationPacket`, 331 sites, the slot named by design record 11's `0x28 + (X - 0x28) * 2` map | static (capstone over `tools/win7-x64-extracted/usbehci.sys` with PDB `7D5BF41B...` age 1) | issue 07 section 7.1; abi "Completion path" |
| **Vista x64 `usbport.sys` 6.0.6002.18005 holds none of its log-ring tags as literals**: each is assembled with four `mov byte ptr [rsp+N], imm8` stores (e.g. `Tmt2` at `USBPORT_Core_TimeoutAllTransfers+b8`, `biCF` at `USBPORTBUSIF_UsbdBusQueryBusTimeEx+11f` = the USBDI bus-time query, record `biCF | 0 | caller's out pointer | frame`), 677 sites; Windows 7 x64 holds them as imm32, 1,481 sites; `Tmt2` on both is `TimeoutAllTransfers` walking the FDO's all-transfers list under the EpList lock writing `link | 0 | transfer` with `AssertSig(transfer, 'TrxC')` (transfer+0x30), the miniport's transfer parameters at transfer+0x130 (`USBPORTSVC_CompleteTransfer` does `params - 0x130`) | static (capstone with PDBs `ABEBA17D...` age 1 and `71BCD350...` age 1; `scripts/local/usbport-vista-tags.py`, `usbport-disasm.py ... tags`) | `.claude` working notes of 2026-09-13; issue 07 section 7; the tag tables in `scripts/local/` |

## 5. Microsoft binaries: what is published, and through which channel

Two channels have to be kept apart here. This project distributes through the
git repository and, separately, through the GitHub release download, and a
sentence that is true of one is false of the other.

A third distinction sits underneath those two, and everything below is
written in the tense it creates. This section describes a decided channel,
not a channel that has carried anything. No GitHub release has been published
and this repository is still private (see the status note at the end of this
section). So "the release download carries X" throughout means that the asset
`make-release.ps1` assembles carries X and that asset is what will be
uploaded, not that anyone has downloaded anything. The same reading applies to
`releases/README.md`, which describes the same asset in the same tense. A cut
writes files in this working
tree; a publish uploads one of them. This project has cut a release and
uploaded nothing.

This repository also uses "published" in two senses, so that word alone
settles nothing. The INF, `releases/history.md` and `releases/README.md`
say a version is "published under `releases\`", meaning cut and committed to
the tracked tree; that is the sense almost every occurrence outside this
section carries. This section means the other one: uploaded to a GitHub
release, where a stranger can download it. Where the distinction could be
misread, prefer "cut" and "uploaded", which have only one sense each.

### The repository carries none

`usbport.sys` and `usbhub20.sys` come from the target machine's own NUSB
install (Windows 98 SE) or from Service Pack 4 (Windows 2000), and are never
tracked. The toolchain archives (MSVC 6.0, the Windows 2000 DDK) and the NUSB
package are downloaded by the user from the links in `README.md` and live
under the git-ignored `tools/`. The two `usbd.sys` builds and Windows 98 SE's
`usbhub.sys` are staged there too, from the user's own Windows install media,
by `scripts/package/extract-usbd-sources.ps1`, as reference copies;
`scripts/import-gate/win98-evidence.list` records the Windows 98 pair's
identity, never content. `releases/<version>/`, the tracked half of
packaging, carries `xhci98.sys` and `xhci98.inf` and stops there, and since
1.0.0.1 so does the release download.

The overlap audit swept the full added-file history and found no Microsoft or
NUSB binary in it. That is still true. The exception below was made a release
asset rather than a tracked file for the same reason (an asset can be
withdrawn, and a git blob cannot be without rewriting history), and it was
withdrawn before it carried anything.

### The GitHub release download carried three of them, until 1.0.0.1

Decided by the maintainer for the two `usbd.sys` builds, extended to
`usbhub98.sys` on the same grounds and by the same mechanism, and withdrawn
on 2026-09-02 before any release had been uploaded. From release 0.0.0.4 to
1.0.0.0 the asset `make-release.ps1` assembled carried `usbd98.sys`,
`usbd2k.sys` and `usbhub98.sys` alongside the driver, so that what a user
downloaded would be a complete install set. What was being distributed:

- Each `usbd` file was the target OS's own `usbd.sys`, byte-for-byte as
  Microsoft shipped it: Windows 98 SE 4.10.2222 from `BASE5.CAB`, and Windows
  2000 SP4 5.00.2195.6658 expanded from `I386\USBD.SY_`. Neither was
  modified, patched, or recompiled. The exact version, length and SHA-256 of
  the Windows 98 file are in `scripts/import-gate/win98-evidence.list`, and
  of the Windows 2000 file in `scripts/package/extract-usbd-sources.ps1`,
  which still stages both as reference copies.
- They were renamed on the media only, to `usbd98.sys` and `usbd2k.sys`,
  because both install as `usbd.sys` and each install path had to be able to
  reach exactly one of them.
- `src/xhci98.inf` copied whichever one its install path named with
  `COPYFLG_NO_OVERWRITE`, so a machine that already had a `usbd.sys` kept its
  own and the shipped file was never placed.
- `usbhub98.sys` was Windows 98 SE's own `usbhub.sys`, 4.10.2222, 35,680
  bytes, byte-for-byte as Microsoft shipped it on the SE CD and taken from the
  same cab read that yielded `usbd98.sys`, renamed on the media for the same
  reason and copied with the same flag, on the Windows 98 path only: on
  Windows 2000 that filename belongs to the OS's own USB 1.1 hub driver.

Why the `usbd.sys` builds. `usbhub20.sys` imports `USBD.SYS` on both targets,
and on an xHCI-only machine, the entire population this driver exists for,
nothing ever placed one. Without it the root hub fails with `0xc0000034`
naming `usbhub20.sys` on Windows 2000, and sits at Code 2 on Windows 98,
which reads as a fault in this driver and is not one
(`docs/contributing/lessons.md`, "`usbhub20.sys` bugchecks Win2000").

Why the third file. `usbhub.sys` is Windows 98's composite parent driver, and
its absence has the same cause as `usbd.sys`'s: Windows 98 ships `USB.INF` on
every install but copies the USB driver files only when setup detects a USB
controller, and an xHCI-only machine looks empty to Win98 setup. So the INF
matches a multi-interface device, Windows names a `USB Composite Device`
devnode, and the file it needs is not on disk: `Code 2`, matched and
unloadable. Batch 13-E measured this on real hardware: every composite device
on the ThinkPad E460 was dead, including a two-interface HID keyboard that
would not type, and the same machine drove them all once the file was
present. The control was a ThinkPad X61 running the same Windows 98 SE and
the same NUSB 3.3 with no xHCI, where the file is present because setup found
its UHCI controller.

What is documented about permission: nothing. No permission to redistribute
these files is documented anywhere in this repository, and none was claimed
here. That both products are long out of support, and that the files were
unmodified copies placed only where the OS itself would have placed them,
are facts about the situation and not permissions. Do not read them as an
argument that this was allowed.

What replaced it. Release 1.0.0.1 changes `src/xhci98.inf` so that the
Windows setup engine copies `usbd.sys` and `usbhub.sys` from the operating
system's own install source: `LayoutFile=layout.inf` in the INF's
`[Version]` section resolves a `CopyFiles` entry the INF's own
`[SourceDisksFiles]` does not name through the OS's `layout.inf`, and the
engine fetches the file from the Windows source path (the CABs on the hard
disk or the Windows 98 CD, and the NT targets' own driver cache: Windows
2000's `sp4.cab`, Windows XP's `sp3.cab`), with the same
`COPYFLG_NO_OVERWRITE`. The release download carries this project's own
files, the two tools and the readmes, and no Microsoft file; the INF gate
refuses an INF or a package that names one (`OS-MEDIA`, `PKG-MSFILE`);
`scripts/package/usbd-sources.expected` and the three-file wording in
`releases/README.md` and `AGENTS.md` went in the same change. The reference
copies under `tools/` stay, read by the import gate and the Windows 2000 VM
setup and packaged by nothing, and section 4's "Where those copies came
from" paragraph still describes them. The measurements that made the change
possible are in `docs/contributing/build-and-test.md`, "The SweetLow stack"
and "The files the OS supplies"; roadmap Phase 17 has the tasks.

Release 1.0.1.0 extends the same route on the NT install path to
`usbport.sys` and `usbhub.sys`, which that path did not copy until then: an
NT install that never had a USB controller has neither (measured on a
Windows XP guest on 2026-09-03), and both come from the `Driver Cache\i386`
every install carries. No file was added to the media and none of the gate
rules above was relaxed; roadmap Phase 19 has the tasks.

Status: the exception was never used. No asset of any version was uploaded
while it stood; this repository was private throughout, and the first upload
is intended to be the newest cut (1.0.2.0 as of 2026-09-07; `releases/history.md`
names it first), every cut since 1.0.0.1 carrying nothing under it. "The
release download carries three of them" was true of the assembled asset from
0.0.0.4 to 1.0.0.0 and of no download anyone made.

This note can look stale and is not. Version directories exist under
`releases/`, the issue-form configuration and the generated `readme.txt` link a
releases page, and this repository's prose
calls a cut asset "published". None of the three is a distribution: a cut
writes `releases/<version>/` and `out/xhci98-<version>.zip` in this working
tree, a publish uploads that zip to a GitHub release, and this project has
done the first and never the second. "Published" in that usage names which
asset filename was in use at a cut, and `README.md`'s link is written for the
repository as it will be. The roadmap carries no clause for the upload at
all: Phase 14 closed on the cut, and the upload is one act of
the project owner's rather than work this repository can do or close. When
it happens, this note is the sentence that moves.

---

## 6. Keeping this file true

Update it in the same change that creates the need, not later:

- A new third-party document or binary: add it to section 2's table, and to
  `docs/references/README.md` if it is a document. It goes in a git-ignored
  directory with its URL and SHA-256 recorded, never tracked. A document that
  lives in no directory of this tree still gets the row; the Oney book is the
  precedent. It sat outside the repository entirely, was cited by printed page
  in three tracked documents and by name in three more, and for a long time
  had no row anywhere because the rule only imagined documents kept under
  `docs/references/`. Record what makes a citation checkable (edition,
  page-index offset, and a hash of the copy the numbers were read from), and
  say plainly when there is no URL rather than leaving the column empty.
- A new thing published through a channel that is not the repository: say
  which channel, in section 5, and say plainly what goes through it. "Not
  tracked" and "not distributed" were once the same statement in this
  project, and every file that repeated one of them repeated it in the
  other's words. Do not write a sentence that leaves the reader to work out
  which of the two it means.
- A new tracked build output: ask what the linker put in it before treating it
  as this project's code. "Our sources produced it" is not the same claim as
  "it contains only our code", and section 2a exists because those two were
  conflated for a published binary. Read the map, name what is statically
  linked in, and record the notices that material asks for.
- A new fact read out of a shipping binary: add a row to section 4 with its
  actual method. If you established it statically, say static, even if the
  system also happens to run.
- A method correction: correct the row. Section 4 exists because an earlier
  audit described several disassembly-derived facts as live trace results,
  which was wrong in the direction that matters.
