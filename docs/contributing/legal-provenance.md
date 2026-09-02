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
| Published driver binary | Yes | `releases/<version>/release/xhci98.sys`, `releases/<version>/debug/xhci98.sys` | Entirely this project's code. Linked by the Windows 2000 DDK against `ntoskrnl.exe`/`hal.dll`/`usbport.sys` import libraries, which contributes no code to the image; the import table names symbols and nothing more (verify with `scripts\import-gate\check-imports.ps1`, which prints every module/symbol pair in it). No third-party object, runtime or extender is linked in, which is what makes this row different from the qualifier's below. GPL-2.0-only (`LICENSE`) |
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
  (LE-style) executable". Static, read from `xhciqual/xhciqual.map` and the
  makefile.
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
| The `USBPORT_GetTt` defect: `USBPORT_CreateDevice` gates the TT lookup on `USB_MINIPORT_FLAGS_USB2` and not-High-Speed; `USBPORT_GetTt`'s single-TT branch has no empty-list guard and returns `0xFFFFFFEC`, which `OpenPipe`'s null check passes, bugchecking in `ExfInterlockedInsertTailList` | both: the bugcheck was observed on both targets first, then read out of the instructions | abi §6; the resulting untruth is in `docs/contributing/implementation-invariants.md`, "Root Hub Reporting" |
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

Where a fact is inferred rather than read, this project's documents say so,
and several of them record refutations of earlier readings. That habit is what
makes the inventory above trustworthy; keep it.

The complete, citation-level record is
`docs/usb-xhci-info/usbport-miniport-abi.md`. This table is an index into it
by method, not a replacement for it.

---

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
disk or the Windows 98 CD, and Windows 2000's own `driver.cab`), with the
same `COPYFLG_NO_OVERWRITE`. The release download carries this project's own
files, the two tools and the readmes, and no Microsoft file; the INF gate
refuses an INF or a package that names one (`OS-MEDIA`, `PKG-MSFILE`);
`scripts/package/usbd-sources.expected` and the three-file wording in
`releases/README.md` and `AGENTS.md` went in the same change. The reference
copies under `tools/` stay, read by the import gate and the Windows 2000 VM
setup and packaged by nothing, and section 4's "Where those copies came
from" paragraph still describes them. The measurements that made the change
possible are in `docs/contributing/build-and-test.md`, "The SweetLow stack"
and "The files the OS supplies"; roadmap Phase 17 has the tasks.

Status: the exception was never used. No asset of any version was uploaded
while it stood; this repository was private throughout, and the first upload
is intended to be 1.0.0.1, which carries nothing under it. "The release
download carries three of them" was true of the assembled asset from 0.0.0.4
to 1.0.0.0 and of no download anyone made.

This note can look stale and is not. A version directory exists under
`releases/`, `README.md` links a releases page, and this repository's prose
calls a cut asset "published". None of the three is a distribution: a cut
writes `releases/<version>/` and `out/xhci98-<version>.zip` in this working
tree, a publish uploads that zip to a GitHub release, and this project has
done the first and never the second. "Published" in that usage names which
asset filename was in use at a cut, and `README.md`'s link is written for the
repository as it will be. The roadmap carries no clause for the upload at
all, deliberately: Phase 14 closed on the cut, and the upload is one act of
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
