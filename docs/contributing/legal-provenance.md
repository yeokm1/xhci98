# Provenance and third-party material

A factual record of where this project's non-obvious facts came from, what
third-party material it touches and how each piece is handled, so a reader
can see the sources and methods without reconstructing them. `AGENTS.md`,
"Third-Party Material and Provenance" and "Reference Implementations",
states the rules that bind day-to-day work and points here for the record.

It is not a legal opinion and states no conclusion about whether any act
described here is permitted in any jurisdiction. It records what this
repository holds, what it publishes, and what the third-party material says
about itself. Do not quote it as clearance for anything, and do not add a
conclusion to it.

---

## 1. What the project is, and why it needs any of this

`xhci98.sys` is an independently written WDM kernel-mode driver for xHCI
host controllers, Windows 98 SE to Windows 7. Since `2.0.0.0` it is a
monolithic host controller driver that replaces `usbport.sys`, the hub
driver and the composite parent (`AGENTS.md`, "Architecture Overview";
design record 13). Up to `1.2.0.0` it was a miniport of `usbport.sys`,
fitting the undocumented `USBPORT_REGISTRATION_PACKET` and its callbacks;
that miniport is frozen, its sources on branch `1.2.0.0`.

Either way it plugs into interfaces Microsoft documented only in part:
beneath `usbport.sys` then; now beneath Windows' class drivers
(`hidusb.sys`, `usbstor.sys`, `usbaudio.sys` and others), the helper
exports of `usbd.sys` and the Device Manager pages (`usbui.dll`,
`sysclass.dll`). Everything in this file exists because the shape of those
interfaces has to be determined. Two sources supply it:

- ReactOS, which reimplements the NT5-era USB stack in C (`usbport`,
  `usbhub` and three miniports written against `usbport`): most of the
  interface description.
- The Microsoft binaries already installed on the targets (`usbport.sys`
  for the miniport; the class drivers, `usbd.sys` and the Device Manager
  pages for the host controller driver): the target-specific contracts
  where ReactOS differs, omits something, or (as happened repeatedly)
  carries a guard the shipping code does not have.

No protection mechanism on any executable or driver binary is defeated,
and no binary is patched or modified. Each is read as the vendor shipped
it, with the Windows 2000 DDK's COFF dumper (`link -dump -disasm`, the
same dumper as `dumpbin`) and with runtime counters compiled into this
project's own driver.

Two adjacent cases, stated so the distinction is not assumed:

- **Unpacking.** Some binaries ship in distribution packaging and are
  expanded before they can be read: `I386\USBD.SY_` and `I386\USBPORT.SY_`
  off Windows install media, extracted with `7z e` and expanded with
  `expand.exe` (`scripts/package/extract-usbd-sources.ps1`;
  `docs/usb-xhci-info/usbport-miniport-interface.md` section 5). That is
  Microsoft's own compression and decompression tool, applied to get the
  file the installer would place anyway; it is not the defeat of any
  protection measure.
- **The Intel xHCI specification PDF** is encrypted with an empty owner
  password, and the text-extraction recipe supplies that empty password
  (`r.decrypt("")`, `docs/references/README.md`), each user to their own
  downloaded copy of a document Intel publishes for download. No password is
  guessed, broken or supplied from elsewhere, and no extracted text is
  redistributed beyond the short verbatim quotations kept in `docs/` for
  identification.

---

## 2. What is tracked in this repository, and what is not

This project's own code is under `LICENSE`: GPL-2.0-only in every release up
to `3.2.0.0`, GPL-2.0-or-later since 2026-10-26 (roadmap-usb4 task 44.38;
first release `3.3.0.0`). The rows below say "this project's code" for it.
This project's documentation, every tracked `.md` file outside
`src/stor98/`, is under CC BY 4.0 instead (`LICENSE-docs.md`).

**The WDK-object open question** (marked [Q] below; section 6's rule for a
new tracked build output): whether the WDK-supplied objects a row names ask
for a notice beside the binary has not been read, and no `NOTICE.TXT` is
generated for the `-x64` directories. Each such row records the composition
and the question and draws no conclusion.

| Material | Tracked? | Where | Note |
|---|---|---|---|
| This project's source, tests, scripts, docs | Yes | `src/`, `test/`, `scripts/`, `xhciqual/`, `docs/` | This project's code |
| Published driver binary, the `1.x.x.x` miniport (miniport history: releases up to `1.2.0.0`) | Yes | `releases/<version>/release-x86/xhci98.sys`, `debug-x86/xhci98.sys`, and the `-x64` pair in a cut that carries one (cuts before roadmap task 21.3 keep the bare `release/` and `debug/` names) | This project's code, linked by the Windows 2000 DDK (`-x86`) or WDK 7.1 (`-x64`; `scripts/build-driver.cmd`, `src/usbport_amd64.lib`) against `ntoskrnl.exe`/`hal.dll`/`usbport.sys` import libraries, which contribute no code; the import table names symbols only (`scripts\import-gate\check-imports.ps1` prints every module/symbol pair). No runtime or extender is linked, unlike the qualifier. The 32-bit build links no third-party object. **The amd64 build links one object that is not this project's code**: WDK 7.1's `/GS` support, `__security_check_cookie` and `__report_gsfailure` (`KeBugCheckEx(0xF7)` on a cookie mismatch), one call site in `.text`, reached from nothing in `src/` (`scripts/import-gate/xhci98-imports-amd64.allow`, the `KeBugCheckEx` row; `runs/run-21.md` task 21.3). Static, from the release image's disassembly; which WDK library supplied it is not recorded (no amd64 linker map read or tracked). [Q] |
| Published host controller driver and UAS driver binaries, `xhci98.sys` from `2.0.0.0` and `xhciuas.sys` | Yes, from `2.0.0.0` | `releases/<version>/release-x86/` and `release-x64/` (and `debug-` up to `3.3.0.0`); from `3.4.0.0` `98-me/`, `nt-x86/` and `nt-x64/` | This project's code, linked by the Windows 2000 DDK (x86) or WDK 7.1 (amd64; `scripts/build-driver.cmd`) against the `ntoskrnl.exe` and `hal.dll` import libraries alone, which contribute no code: the HCD links no usbport import library and imports nothing from `usbport.sys` (`src/sources`; the import gate refuses a `USBPORT.SYS` pair by name), and `src/usbport_amd64.lib` left the tree with the miniport. The 32-bit builds link no third-party object. The amd64 builds carry the same WDK 7.1 `/GS` cookie support and `__report_gsfailure` as the row above (the `KeBugCheckEx` rows of `xhci98-imports-amd64.allow` and `xhciuas-imports-amd64.allow`). Static, from the import tables the gate prints. [Q] |
| The x64 binaries' embedded test signature, and `xhci98-test.cer` | Yes, from `2.3.0.0` | Appended to `xhci98.sys` and `xhciuas.sys` in every `-x64` directory; from `3.0.0.0` to `xhcicm.sys` (in `3.0.0.0` also its `nt\xhcicm.sys` copy; from `3.1.0.0` an `-x64` directory has no `nt\`); from `3.1.0.0` to the bundled `stor98.sys` of `release-x64` (one key for every driver; "The bundled Stor98 build"; AGENTS.md, "The INF and install media"); `releases/<version>/*-x64/xhci98-test.cer`; never in an `-x86` directory | Design record 19 (owner, 2026-10-11). A PKCS #7 `WIN_CERTIFICATE` appended by WDK 7.1's `SignTool.exe`, run in place from git-ignored `tools/WinDDK71/bin/x86` at each cut; it holds this project's self-signed certificate (`CN=xhci98 project test signing (not trusted)`), the image's SHA-1 digest and the signature, and no code. Made by the owner on 2026-10-11 with WDK 7.1's `MakeCert.exe` and `pvk2pfx.exe` run in place (record 19 section 4; `scripts/package/make-testcert.ps1`); SHA-1 thumbprint `A512DF4530C63AFEA9C0F18D64E333BF75E5ABFF`; `xhci98-test.cer` is its public half, DER, exported from the `.pfx` at each cut. None of the three tools is tracked or published; the private key (`.pfx`) is not tracked or published (`.gitignore`: `*.pfx`, `*.pvk`). Not a Microsoft signature; chains to no Microsoft or public root |
| Published MSI helper binary | Yes, from 2.3.0.0 | `releases/<version>/release-x86/xhcimsi.sys` and `debug-x86/xhcimsi.sys`; never in `-x64` | This project's code (`src/msi86/`, `src/xhci_msi86.c`), linked by **WDK 7.1 for x86, Vista target** (`setenv x86 WLH`; design record 18 section 8.1) against `ntoskrnl.exe` import libraries (no code). **Unlike the 32-bit `xhci98.sys` it links WDK library objects**: `wdmsec.lib`'s `IoCreateDeviceSecure` and the members it pulls in (31 `ntoskrnl.exe` pairs, `scripts/import-gate/xhcimsi-imports.allow`, from the first link's import table, 2026-10-10), and with `/GS` left at the kit default, `GsDriverEntry` and `BufferOverflowK.lib`'s cookie support with `__report_gsfailure` (`KeBugCheckEx(0xF7)`). Which members the link retained is for a linker map, which no build has written yet. [Q] |
| Published x86 helper binary, renamed | Yes, from `2.4.0.0` (cut 2026-10-14) | `releases/<version>/release-x86/xhcihlpr.sys` and `debug-x86/xhcihlpr.sys`; never in `-x64`; the `2.3.x` directories keep `xhcimsi.sys` | The same helper renamed and extended to 2000 and XP (roadmap-hcd task 38.3, design record 18 section 15): this project's code (`src/hlpr/`, `src/xhci_hlpr.c`), linked by **WDK 7.1 for x86, Windows XP target** (`setenv x86 WXP`, `SUBSYSTEM_VERSION=5.00`; record 18 section 15.10, decision H5) against `lib\wxp\i386` `ntoskrnl.exe` import libraries (no code). The same kinds of WDK object as the row above, the `wxp` copies (`wdmsec.lib`'s `IoCreateDeviceSecure` and members, `GsDriverEntry`, `BufferOverflowK.lib`'s cookie support), and for a subsystem below 6.01 `lib\crt\i386\safecrtnt.lib` and `libcntpr.lib` (`makefile.new`; the link line, 2026-10-14). Its 39 import pairs (`scripts/import-gate/xhcihlpr-imports.allow`, from the first `WXP` link of each flavour) resolve in every kernel and HAL the gate's manifests authenticate for 2000 SP4, XP SP3, Vista SP2 and 7 SP1 x86, the Vista and 7 PAE kernels not among them (section 4). [Q] |
| Published Thunderbolt 3 and USB4 driver binary | Yes, from `3.0.0.0` | `releases/<version>/<flavour>-<arch>/xhcicm.sys`, all four flavour directories, and the same image under `nt\` beside its tunnel-port filter's INF in the x86 ones (and `3.0.0.0`'s x64 ones; from `3.1.0.0` an x64 directory has the filter INF at its root and one copy) | This project's code (`src/cm/`, and `src/xhci_log.c` by path), linked by the Windows 2000 DDK (x86) or WDK 7.1 (amd64) as `xhci98.sys` is (`src/cm/sources`) against `ntoskrnl.exe` and `hal.dll` import libraries (no code); imports gated by `scripts/import-gate/xhcicm-imports.allow` and `xhcicm-imports-amd64.allow`, every x86 row a pair `xhci98.sys` already imports. The amd64 build carries the same `/GS` cookie support and `__report_gsfailure` as `xhci98.sys`'s (the `KeBugCheckEx` row of `xhcicm-imports-amd64.allow`). [Q] **Some source files carry third-party code with notices kept** (section 2a): files lifted from `usb4-emu`'s `tools/usb4cm` (MIT); files adapted from `usb4-emu` (GPL-2.0-only there; offered to this project under GPL-2.0-or-later by its copyright holder, owner 2026-10-25); up to `3.2.0.0` portions adapted from Linux `drivers/thunderbolt` (GPL-2.0-only), rewritten from the specifications for `3.3.0.0` (task 44.38); and `src/cm/stor98cm.h`, Stor98's header unchanged (BSD-3-Clause) |
| Published DOS qualifier binary | Yes | `releases/<version>/tools/xhciqual/XHCIQUAL.EXE` + `.MAP` | Built from this project's sources but not only this project's code; section 2a |
| Published cleanup tool binaries | Yes, `2.4.0.0` to `3.2.0.0` (dropped in `3.3.0.0`) | `releases/<version>/tools/xhcicln/XHCICLN.EXE`, `XHCICL64.EXE` | Statically linked C runtime (MSVC 6.0's, WDK 7.1's); section 2a |
| Published snapshot reader binary | Yes | `releases/<version>/tools/xhcisnap/XHCISNAP.EXE` | Statically linked MSVC 6.0 C runtime; section 2a |
| Intel xHCI specification PDF | No | `docs/references/` (git-ignored) | URL + version + SHA-256 in `docs/references/README.md` |
| USB-IF backwards-compatibility testing PDF | No | `docs/references/` (git-ignored) | URL + version + SHA-256 in `docs/references/README.md` |
| Published install tool binaries, and `INSTALL.BAT` | Yes, from `3.0.0.0` (the window and `SETUP.EXE` from `3.1.0.0`, cut 2026-10-19) | `releases/<version>/tools/xhciinst/XHCIINST.EXE`, `XHCIIN64.EXE`, from `3.1.0.0` `XHCIWIZ.EXE`, `XHCIWZ64.EXE`; `releases/<version>/INSTALL.BAT`, from `3.1.0.0` `releases/<version>/SETUP.EXE` | Statically linked C runtime in each executable (MSVC 6.0's, WDK 7.1's); section 2a. `INSTALL.BAT` is this project's own text. Neither carries or calls a Microsoft setup program: the routines used are the system's own, looked up at run time |
| Published USB tree tool binaries | Yes, from `3.0.0.0` | `releases/<version>/tools/xhcitr/XHCITR.EXE`, `XHCITR64.EXE` | Statically linked C runtime (MSVC 6.0's, WDK 7.1's); section 2a |
| Published safe-removal mark service binaries | Yes, from `3.1.0.0` | `releases/<version>/tools/xhcimark/XHCIMARK.EXE`, `XHCIMK64.EXE` | Statically linked C runtime (MSVC 6.0's, WDK 7.1's); section 2a |
| Disk benchmark binaries (`XHCIPERF.EXE`, `XHCIPF64.EXE`) | No: a developer diagnostic, never in a package (owner, 2026-10-23: dropped from `3.2.0.0`); built under `xhciperf\`, git-ignored | `xhciperf/` only | Statically linked C runtime (MSVC 6.0's, WDK 7.1's); compiles Stor98's BSD-3-Clause interface header `src/stor98/include/stor98pf.h`; section 2a |
| TCP throughput and thread report binaries (`TBSPEED.EXE`, `TBTHREAD.EXE`) | No: developer diagnostics, never in a package (2026-11-09, the Thunderbolt networking bench); built under `tbspeed\`, git-ignored | `tbspeed/` only | Statically linked MSVC 6.0 C runtime; section 2a |
| Published 98 SE and ME device-install helper binary | Yes, from `3.1.0.0` (cut 2026-10-19) | `releases/<version>/tools/xhciinst/XHCIDI.EXE` | Not only this project's code (design record 27 section 3): a 16-bit Windows program with the Open Watcom 2.0 C runtime statically linked (Sybase Open Watcom Public License, as the qualifier's), linked against the Windows 98 SE DDK's `setupx.lib`, an import library (OMF records naming module `SETUPX` and its exports, no code), so the image holds module-and-ordinal imports of the running system's `SETUPX.DLL` and no Microsoft code; compiled against that DDK's `inc16\setupx.h`, the `prsht.h` it includes and its 98 `windows.h` (the four members in `docs/references/README.md`, "Windows 98 DDK"); section 2a, "XHCIDI.EXE" (task 42-A.1) |
| Published 98 SE and ME IOS layer for Stor98's disks | Yes, from `3.2.0.0` (cut 2026-10-24) | `releases/<version>/release-x86/S98VSD.VXD` and the other x86 flavour directories beside `stor98.mpd` | This project's sources alone (design record 30 section 6): Open Watcom 2.0's compiler with no runtime (`-zl`, no library), linked by the Windows 98 SE DDK's `LINK.EXE` 5.12, which adds its MS-DOS stub and `Rich` record of tool identifiers as Microsoft's linkers do to every PE driver of this package; compiled against ten of that DDK's headers (`docs/references/README.md`, "Windows 98 DDK"), none of whose text the image holds; GPL-2.0 like the rest of this project |
| Published window-closing step, `XHCIPFW.VXD` and `XHCIPFW.SYS` | Yes, from `3.3.0.0` | `releases/<version>/release-x86/` at its root (`3.3.0.0`); from `3.4.0.0` `98-me/cm/XHCIPFW.VXD` and `nt-x86/cm/XHCIPFW.SYS`; never in an x64 directory | This project's code (`src/pfw/`, design record 32). The VxD as `S98VSD.VXD` is (the row above): Open Watcom 2.0's compiler with no runtime, linked by the Windows 98 SE DDK's `LINK.EXE` 5.12 with its MS-DOS stub. The `.SYS` linked by the Windows 2000 DDK as the 32-bit drivers are (`src/pfw/nt/sources`), against the `ntoskrnl.exe` and `hal.dll` import libraries (no code), its imports gated by `scripts/import-gate/xhcipfw-imports.allow` under the `hlprx86` profile; no third-party object |
| Published Thunderbolt networking drivers, `xhcinet.sys` and `xhcinet6.sys` | Yes, from `3.4.0.0` | `releases/<version>/98-me/cm/tbnet/xhcinet.sys`; `nt-x86/cm/tbnet/` and `nt-x64/cm/tbnet/` `xhcinet.sys` and `xhcinet6.sys` | This project's code (`src/net/`, design record 35). `xhcinet.sys`, the NDIS 5 miniport, linked as `xhcicm.sys` is - the Windows 2000 DDK for x86, WDK 7.1 for amd64 (`src/net/ndis5/sources`) - against the `ntoskrnl.exe`, `hal.dll` and `ndis.sys` import libraries (no code); its amd64 build carries the same `/GS` cookie support (the `KeBugCheckEx` row of `xhcinet-imports-amd64.allow`). `xhcinet6.sys`, the NDIS 6.20 miniport of Windows 8 to 10, linked by WDK 7.1's `WIN7` environment for x86 and amd64 (`src/net/ndis6/sources`) against the same kinds of import library; both of its builds carry that kit's `/GS` cookie support (`xhcinet6-imports.allow`'s `KeTickCount` and `KeBugCheckEx` rows, and the amd64 file's `KeBugCheckEx` row). Static, from the import tables the gate prints. [Q] |
| Oney, *Programming the Microsoft Windows Driver Model* (2nd ed., 2003) | No, never; in no commit and no directory of this repository | A purchased book on the reader's own shelf | The one third-party document cited by printed page that is not in `docs/references/`; edition, page count, SHA-256 of the copy read and the page-index offset are in `docs/references/README.md`. No fetch URL: there is no download |
| ReactOS / Linux / FreeBSD / Haiku source mirrors | No | `external/` (git-ignored) | Fetch procedure and pinned commits in `external/README.md` |
| `usbport.sys`, `usbhub20.sys`, NUSB 3.3, MSVC 6.0, the Win2000 DDK | No | `tools/` (git-ignored) | From the user's own install media or download |
| WDK 7.1 (7600.16385.1) | No | `tools/WinDDK71/` (git-ignored) | From the user's own download. The amd64 build's whole toolchain (no compiler here older than its `cl` 15.00 targets x64); since task 36.15 the x86 MSI helper's (`xhcimsi.sys`, Vista target, record 18 section 8.1; its `wlh\i386` import library and headers are the only ones here declaring `IoConnectInterruptEx`), since task 38.3 the helper's as `xhcihlpr.sys` (XP target, `lib\wxp\i386`, subsystem 5.00, record 18 section 15.10, asking `MmGetSystemRoutineAddress` for that pair instead of importing it); the source of `kd.exe -z` for section 4's amd64 static readings; since 2026-10-11 of `MakeCert.exe`, `pvk2pfx.exe` and `SignTool.exe` (`bin/x86`), used in place for the test certificate and the x64 signing (record 19), not redistributed |
| The two `usbd.sys` builds and Windows 98 SE's `usbhub.sys` | No | `tools/` (git-ignored) | Reference copies from the user's own install media, for the import gate's Windows 98 evidence and the 2000 VM setup. The assembled release download carried them from 0.0.0.4 to 1.0.0.0, no longer; section 5 |
| Disassembly extracts taken from those binaries | No | `tools/*-extracted/` (git-ignored) | Convenience copies; section 4 |
| Windows 8 and 8.1 files for task 43.26 (the USB drivers, `usbd.sys`, `Wdf01000.sys`, `usbui.dll`, `ci.dll`, `ntoskrnl.exe`, every `Windows\inf\*.inf`), their Microsoft PDBs, and the mingw-w64 headers for the Windows 8 constants | No, in no directory of this repository | Scratch outside the repository on the development host (owner, 2026-10-21: scratch material off the tree) | Microsoft's, by `7z e` from `sources\install.wim` of the owner's own media (design record 13 section 6.12, whose table gives version, size and SHA-256 of each file it reads; `ci.dll`, `ntoskrnl.exe` and the INFs outside that table, collected for the sweep or for later, are not identified there); the PDBs are Microsoft's public symbols as msdl serves them; nothing from either executed. The mingw-w64 headers (`usb.h`, `usbioctl.h`, `usbiodef.h`, `usbbusif.h`, `usbdlib.h`, `hubbusif.h`, `usbspec.h`) from `https://raw.githubusercontent.com/mingw-w64/mingw-w64/master/mingw-w64-headers/` at commit `62891fa4bf507d674c8c48822b596560b7cb506d`, each carrying the line "This file is part of the mingw-w64 runtime package" and referring to that package's `DISCLAIMER`; only constants taken. Section 4, the 43.26 rows |
| Windows 8 and 8.1 files for task 43.25 (`shell32.dll`, `hotplug.dll`, `usbui.dll`, their `en-US` MUI files, every `System32\en-US\*.mui` of the 8 x86 image for a text search, and the `SOFTWARE` hive) | No, in no directory of this repository | Scratch outside the repository on the development host | Microsoft's, by `7z e` from `sources\install.wim` of the owner's own 8 x86 and 8.1 x64 media (record 13 section 6.12; section 6.12.11's table gives version, size and SHA-256 of each file it reads); nothing executed. Section 4, the 43.25 rows |
| Windows 10 22H2 files for task 43.33 (the USB drivers, `usbd.sys`, `Wdf01000.sys`, `usbui.dll`, `shell32.dll`, `UsbTask.dll`, `UsbSettingsHandlers.dll`, `hotplug.dll`, their `en-US` MUI files, every `Windows\INF\*.inf` of both images, every `System32\en-US\*.mui` of the x64 image) | No, in no directory of this repository | Scratch outside the repository on the development host | Microsoft's, by `7z e` from the two 10 22H2 guest images of task 43.32 (installed from the owner's own media; record 13 section 6.14, whose table gives version, size and SHA-256 of each file it reads); nothing executed. Section 4, the 43.33 rows |
| Windows 2000 SP4 and XP SP3 files for task 44.81 (`usbui.dll` and `usbport.sys` of each) | No, in no directory of this repository | Scratch outside the repository on the development host | Microsoft's, by 7-Zip from `driver.cab`, `sp4.cab` and `sp3.cab` in `Driver Cache\i386` of the project's clean q35 2000 SP4 and XP SP3 guest images (owner's own media). 2000 `usbui.dll` 59,664 bytes, SHA-256 `fc106598c6b2fe8a128e1753099b916662225ccf1aed5addd0e23399b4011313`; XP SP3 `usbui.dll` 74,240 bytes, `82076e04f96b30693f61f0373e17cb8f6d9ca4a7b6ba819679435472c3c7c6a8`; 2000 SP4 `usbport.sys` 138,288 bytes, `22388c0e5ecb8840027559b1b1fbe0d7af9f1c87741a222b3d55772ada91b53f` (the same as `tools\win2ksp4-extracted\USBPORT.SYS`); nothing executed. Section 4, the 44.81 row |
| Intel's Thunderbolt driver packages for task 43.33 (c): 17.4.77.13 (`tbt100x.inf`), DCH 1.41.1379.0 and 1.41.1423.0 (`TbtHostController.inf` and its component INFs) | No, in no directory of this repository | Scratch outside the repository on the development host | Intel's, signed by Microsoft's hardware publisher, downloaded 2026-10-22 from the Microsoft Update Catalog (`catalog.s.download.windowsupdate.com`, update ids `98f33697-c606-43f9-821f-2854d66e5356`, `8c2262db-9cdc-4046-9c9d-6de1311d47cf`, `5854aaf0-891a-4a64-a6ee-ba6466cdd48a`; the three `.cab` files' SHA-256 `aa2beb6dce0e3bd819b055feacfe888261729836bcc44e06518ae9f4b8aa9078`, `d01be65779f2e26ea6c07e4db2f4e0b8bb3a99d5f64aabada8828ac0969eb1c6`, `965d8184f96958b22e305aa29c001aa353f76881f8ed7ece4ef40c020875ddfc`), unpacked with 7-Zip; only INF text read, nothing executed. Section 4, the 43.33 rows |
| Windows 7, 8 and 8.1 storage files for task 43.23 (`scsiport.sys`, `Classpnp.sys`, `disk.sys`, `storport.sys`, `partmgr.sys`, `ntoskrnl.exe`, `hal.dll`, 8.1's `stornvme.sys`, and `stornvme.inf`, `disk.inf`, `mshdc.inf`) | No, in no directory of this repository | Scratch outside the repository on the development host | Microsoft's, by `7z e` from `sources\install.wim` of the owner's own media (the three of the 43.26 row, and `en_windows_7_professional_with_sp1_vl_build_x64_dvd_u_677791.iso` for 7 x64's `scsiport.sys`, `Classpnp.sys`, `disk.sys`); version, size and SHA-256 of each file a fact rests on in `docs/contributing/runs/run-43.md`, "43.23"; nothing executed. Section 4, the 43.23 rows |
| Windows 10 storage files for task 43.23 (`scsiport.sys`, `Classpnp.sys`, `disk.sys`, `stornvme.sys`, `ntoskrnl.exe`, `hal.dll`, `stornvme.inf`, `scsidev.inf` of 10 22H2 x86 and x64) | No, in no directory of this repository | Scratch outside the repository on the development host | Microsoft's, by `7z e` from the `Windows` tree of the powered-off pristine `w10x86` and `w10x64` images (installed from the owner's own 10 22H2 media); version, size and SHA-256 of each file a fact rests on in `docs/contributing/runs/run-43.md`, "43.23", "Windows 10"; nothing executed. Section 4, the 43.23 rows |
| Staged install media | No | `out/` (git-ignored) | Generated; the release upload set is assembled here |
| NUSB 3.3's Windows 98 SE storage files (`USBSTOR.INF`, `USBSTOR.SYS`, `USBNTMAP.INF`, `USBNTMAP.SYS`, `USBMPHLP.PDR`, `USBAUTH.SYS`, `USBU2A.SYS`) and their `nusb33/NOTICE.md` | No, never; from task 43.3 a cut is refused whose `releases/` tree or tracked files hold any of the seven names | The owner's separate repository `usb-wdm-nonfree`, `nusb33/w98se/` (kept apart from its `non-free/` since 2026-10-22); copied at packaging time into `out/` and the upload set only, from `3.2.0.0` | Microsoft's, apart from `USBU2A.SYS` ("USB Compliance"), unmodified from NUSB 3.3; not this project's work; no licence to redistribute them has been established. The decision, the channel and each file's evidence: section 5, "NUSB 3.3's Windows 98 SE storage files" |
| Stor98 builds (BSD-3-Clause) | In `3.0.0.0` no; from `3.1.0.0` yes | `3.0.0.0`: copied at packaging time from the pinned Stor98 release folder (`0.4.0.0`) into `out/` and the upload set alone, never into `releases/`. From `3.1.0.0` built from `src/stor98/` by `scripts/build-driver.cmd` with the drivers and published in `releases/<version>/` ("The bundled Stor98 build"): up to `3.3.0.0` in `release-x86/` and `release-x64/`, from `3.4.0.0` in each family folder's `stor98/` | A separate project's binaries: in `3.0.0.0` its released ones, bundled unchanged; from `3.1.0.0` this tree's build of its sources. Since 2026-10-19 the release folders they come from are tracked in `src/stor98/` (next row); the copies in `out/` are not |
| Stor98, its source, docs, tests and released builds (BSD-3-Clause) | Yes, from 2026-10-19 (roadmap-usb4 task 42-A.20) | `src/stor98/` | The Stor98 project's tracked files, imported as one snapshot (owner, 2026-10-19). Not this project's GPL-2.0 code: Stor98's own `LICENSE` and `NOTICE` govern every file there ("Stor98 in this tree") |
| The Windows 98 Second Edition DDK (`win98se_ddk.exe`) | No | `tools/win98ddk/`, extracted into `tools/win98ddk-extracted/` (git-ignored) | Fetched 2026-10-15 from the Internet Archive for roadmap-usb4 task 39-0.3; identity, SHA-256, source URL and stated licence in `docs/references/README.md`, "Windows 98 DDK". Extracted with 7-Zip, not installed, nothing in it executed. The CONFIGMG and VMM values of `src/cm/cm_cfgmg.h` are taken from it (section 4, the CONFIGMG row) |
| Source files from `usb4-emu`'s `tools/usb4cm` (MIT) | Yes, from the change that lifts the first of them (roadmap-usb4 task 39-A.2) | `src/cm/` | Not only this project's code: which files, their notices and the rule a lifted file follows are in section 2a, "Source files lifted from `usb4cm`" |

From `3.4.0.0` the published directories are the family folders (roadmap-usb4 task 45.44, design record 36): a row's `release-x86/` is `98-me/` or `nt-x86/` there and its `release-x64/` is `nt-x64/`; the connection manager, XHCIPFW, the helper's second copy and the networking drivers sit in a family folder's `cm/`, and Stor98 with `S98VSD.VXD` in its `stor98/`; and the `debug-` directories end with `3.3.0.0` (task 45.45).

`docs/references/`, `external/` and `tools/` are git-ignored because they
hold third-party material, which this project does not track (`AGENTS.md`,
"Third-Party Material and Provenance"); the Oney row records a document
never held in the tree so that a `p.N` citation can be resolved. `out/`,
`vm/` and `scripts/local/` are ignored because they are generated or
host-specific; `out/` also because the release upload set is assembled
there.

"Not tracked" is a statement about this repository, not that a file is
never distributed: the distinction mattered for `usbd98.sys`, `usbd2k.sys`
and `usbhub98.sys` while the assembled asset carried them (section 5).

A fact may be tracked; the document or binary it was read out of may not.
Where a fact cannot be re-derived without the source, the document recording
it also records how to re-derive it (the address, the instruction, the
command), so a clone with none of the inputs can check the claim;
`docs/usb-xhci-info/usbport-miniport-abi.md` does this at length.

### 2a. The tracked binaries that are not only ours

These are the exception to section 2's rule that this project tracks only
its own code. All are tools, not drivers: two executables until `2.4.0.0`
(`XHCIQUAL.EXE`, `XHCISNAP.EXE`), four from it (the cleanup tool's two), eight from `3.0.0.0` (the install and tree tools' four); `3.1.0.0` (cut 2026-10-19) adds the window's two, `SETUP.EXE`, `XHCIDI.EXE` and the mark service's two and drops `XHCISNAP.EXE`, thirteen; and `3.3.0.0`, which drops the cleanup tool, leaves eleven. In a
release before `2.4.0.0` the qualifier and the reader sit at the version
root without `tools/` (roadmap-hcd task 38.9). The tasks that added them:
the cleanup tool roadmap-hcd 38.8; the install tool roadmap-usb4 40.4
(numbered 40.5 before 2026-10-16); the USB tree tool roadmap-usb4 Phase 41;
`XHCIDI.EXE` roadmap-usb4 42-A.1; the mark service roadmap-usb4 42-A.5. Where a subsection says "this project's
GPL-2.0-only code" it describes an executable of a release up to
`3.2.0.0`; the same executable of a later release carries this project's
code under GPL-2.0-or-later (section 2), with the same third-party runtimes.

This section also records source files of this project that carry code
taken from elsewhere, each with its notices. For the Thunderbolt and USB4
work (`src/cm/`) there are two kinds: files lifted from `usb4-emu`'s
`tools/usb4cm` while it was MIT (keeping the MIT notice of the commit they
came from), and, since 2026-10-15, files adapted under section 3's rule for
the Thunderbolt and USB4 mirrors from `usb4-emu` (`3c74189` or later,
GPL-2.0-only), Linux `drivers/thunderbolt`, FreeBSD `sys/dev/thunderbolt`
or Intel's tbtools data, one entry per file added in the change that adapts
it (`AGENTS.md`, "Reference Implementations", for what an entry names).
Since 2026-10-26 (task 44.38) no portion adapted from Linux remains; "Rewritten
from" below records what each was rewritten from.

"Static" below means read from the linker map or the import table; nothing
was executed. The MSVC 6.0 runtime comes from the MSVC 6.0 installation used
in place out of `tools/MSVC600` (untracked, section 2). Nothing of either
runtime's terms is quoted here; the linkage is the fact.

| Tool | Built by, from (this project's own text) | Third-party code in the image | Re-derivable by | Run-time lookups and notice |
|---|---|---|---|---|
| `releases/<version>/tools/xhcisnap/XHCISNAP.EXE` (Windows console reader) | `xhcisnap/build.cmd`, from `xhcisnap/xhcisnap.c` alone | MSVC 6.0 C runtime, static (`cl /nologo /W3 /WX /O2`, no `/MD`), bound into the image rather than reached through `MSVCRT.DLL`; intentional, so the tool is one file that runs on a Windows 98 SE machine with nothing installed, the machine it exists for. No DOS extender and no Open Watcom code: an ordinary Win32 console PE, so the qualifier's two items have no counterpart and its `NOTICE.TXT` is not this one | `link -dump -imports XHCISNAP.EXE`: `KERNEL32.dll`, `ADVAPI32.dll`, no C runtime DLL | `scripts/package/make-release.ps1` generates `NOTICE.TXT` beside it recording the linkage, as for the qualifier; both notices are generated at every cut, so a published directory carries the one for each tool it holds. No text from the runtime's product is reproduced anywhere in this tree |
| `releases/<version>/tools/xhcicln/XHCICLN.EXE` and `XHCICL64.EXE` (optional cleanup tool, `2.4.0.0` to `3.2.0.0`; design record 13 section 10.13.8) | `xhcicln/build.cmd`, from `xhcicln/xhcicln.c` alone, with `xhcicln/xhcicln.rc` embedding `xhcicln/xhcicln.manifest` | `XHCICLN.EXE`: MSVC 6.0 C runtime, static, exactly as `XHCISNAP.EXE` (`cl /nologo /W3 /WX /O2`, no `/MD`). `XHCICL64.EXE`: WDK 7.1 amd64 C runtime (`LIBCMT`, from untracked `tools/WinDDK71/lib/crt/amd64`), static (WDK 7.1's `cl`, `/MT`) | `link -dump -imports`: `KERNEL32.dll`, `ADVAPI32.dll`, no C runtime DLL (`XHCICL64.EXE` read 2026-10-14 with WDK 7.1's `link /dump /imports`) | every SetupAPI and configuration-manager routine either calls is looked up at run time from the system's own `setupapi.dll` and `cfgmgr32.dll`, not linked, shipped or tracked; one `NOTICE.TXT` beside the two records both linkages. Dropped in `3.3.0.0` (task 44.82), its source gone from the tree, so these `xhcicln/` paths are the `3.2.0.0` tree's; the copies published up to `3.2.0.0` stay |
| `releases/<version>/tools/xhciinst/XHCIINST.EXE` and `XHCIIN64.EXE` (install, update and uninstall tool of `3.0.0.0`; design record 23) | `xhciinst/build.cmd`, from `xhciinst/xhciinst.c`, `xhciinst/inst_core.c` and `src/xhci_psw.c` (the driver's own port-switchover gate), with `xhciinst/xhciinst.rc` embedding `xhciinst/xhciinst.manifest`; from `3.1.0.0` (`roadmap-usb4.md` task 42-A.2, design record 23 revision 11) from `xhciinst/inst_eng.c` (the engine) and `xhciinst/xhciinst.c` (its console front end), the same linkages | in exactly `XHCICLN`'s way: `XHCIINST.EXE` MSVC 6.0 C runtime, static (`cl /nologo /W3 /WX /O2`, no `/MD`); `XHCIIN64.EXE` WDK 7.1 amd64 C runtime (`LIBCMT`), static (`/MT`) | `link -dump -imports XHCIINST.EXE`: `KERNEL32.dll`, `ADVAPI32.dll`, no C runtime DLL, checked by `xhciinst/selftest.cmd` at every build; `XHCIIN64.EXE`'s, read the same way with WDK 7.1's `link`, names the same two | every SetupAPI, `newdev.dll`, configuration-manager and `version.dll` routine either calls is looked up at run time from the system's own DLLs, not linked, shipped or tracked; neither contains or runs `devcon` or any other Microsoft setup program. `INSTALL.BAT`, staged at the version root, is this project's own text. One `NOTICE.TXT` beside the two records both linkages, as for `XHCICLN` |
| `releases/<version>/tools/xhciinst/XHCIWIZ.EXE` and `XHCIWZ64.EXE` (the installer's window, from `3.1.0.0`; design record 26) | `xhciinst/build.cmd`, from `xhciinst/xhciwiz.c`, `xhciinst/wiz_lay.c` (from `3.2.0.0`), `xhciinst/inst_eng.c`, `xhciinst/inst_core.c` and `src/xhci_psw.c`, with `xhciinst/xhciwiz.rc` embedding `xhciinst/xhciwiz.manifest` and `xhcitr/xhcitr.ico` (XHCITR's icon, drawn for this project in the 16 VGA colours) | in `XHCITR`'s way: `XHCIWIZ.EXE` MSVC 6.0 C runtime, static multithreaded (`cl /nologo /W3 /WX /O2 /MT`, `LIBCMT`); `XHCIWZ64.EXE` WDK 7.1 amd64 C runtime (`LIBCMT`), static (`/MT`) | `link -dump -imports XHCIWIZ.EXE`: `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ADVAPI32.dll`, `COMCTL32.dll`, `COMDLG32.dll`, `SHELL32.dll`, no C runtime DLL, checked by `xhciinst/selftest.cmd` at every build; `XHCIWZ64.EXE`'s names the same seven | as for the console pair: every SetupAPI, `newdev.dll`, configuration-manager and `version.dll` routine is looked up at run time; neither contains or runs a Microsoft setup program. The one `NOTICE.TXT` beside the four executables records all four linkages |
| `releases/<version>/SETUP.EXE` (from `3.1.0.0`; `roadmap-usb4.md` task 42-A.31, design record 26 section 2: starts the installer's window and exits) | `xhciinst/build.cmd`, from `xhciinst/setup.c`, with `xhciinst/setup.rc` embedding `xhciinst/setup.manifest` and `xhcitr/xhcitr.ico` (the xHCI98 USB Tree's icon, drawn for this project) | MSVC 6.0 C runtime, static (`cl /nologo /W3 /WX /O2`, `LIBC`) | `link -dump -imports SETUP.EXE`: `KERNEL32.dll`, `USER32.dll`, `SHELL32.dll`, no C runtime DLL, checked by `xhciinst/selftest.cmd` at every build | contains and runs no Microsoft setup program; the `NOTICE.TXT` in `tools/xhciinst/` records its linkage with the others' |
| `releases/<version>/tools/xhcitr/XHCITR.EXE` and `XHCITR64.EXE` (the xHCI98 USB Tree of `3.0.0.0`; roadmap-usb4 Phase 41, design record 25) | `xhcitr/build.cmd`, from `xhcitr/xhcitr.c`, `tr_core.c`, `tr_log.c`, `tr_pci.c` and `xhciinst/inst_core.c`, with `xhcitr/xhcitr.rc` embedding `xhcitr/xhcitr.manifest` and `xhcitr/xhcitr.ico` (drawn for this project), and from `3.2.0.0` `xhcitr/xhcitr-f.ico` (the same icon with its controller's blue painted red; record 25 section 7.7) | in exactly `XHCIINST`'s way: `XHCITR.EXE` MSVC 6.0 C runtime, static (`cl /nologo /W3 /WX /O2`, no `/MD`, `/SUBSYSTEM:WINDOWS,4.0`); `XHCITR64.EXE` WDK 7.1 amd64 C runtime (`LIBCMT`, from untracked `tools/WinDDK71/lib/crt/amd64`), static (`/MT`) | without the toolchain: `xhcitr/check-imports.ps1`, run by `xhcitr/selftest.cmd` at every build, reads each image's import directory itself and requires exactly `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `SHELL32`, `COMCTL32` and `COMDLG32` and no delay-load import, so no C runtime DLL | every SetupAPI and configuration-manager routine either calls is looked up at run time from the system's own DLLs, not linked, shipped or tracked; one `NOTICE.TXT` beside the two records both linkages, as for `XHCICLN` and `XHCIINST` |
| `releases/<version>/tools/xhcimark/XHCIMARK.EXE` and `XHCIMK64.EXE` (the Thunderbolt safe-removal mark service, an NT service, from `3.1.0.0`; roadmap-usb4 task 42-A.5, design record 21 section 7.4) | `xhcimark/build.cmd`, from `xhcimark/xhcimark.c`, `xhcimark/mark_core.c`, `xhciinst/inst_core.c` and `xhciinst/inst_bootp.c`, with `xhcimark/xhcimark.rc` embedding `xhcimark/xhcimark.manifest` | The 32-bit image: MSVC 6.0 C runtime, static (`cl /nologo /W3 /WX /O2`, no `/MD`). The 64-bit image: WDK 7.1's amd64 C runtime, static (`/MT`, LIBCMT) | The build line in `xhcimark/build.cmd`; the import tables name `KERNEL32.dll` and `ADVAPI32.dll` and no C runtime DLL | `NOTICE.TXT` beside the two executables records the linkage and quotes no runtime terms |
| `XHCIPERF.EXE` and `XHCIPF64.EXE` (XHCIPERF, the disk benchmark of roadmap-usb4 task 43.50, design record 29; a developer diagnostic no package carries, owner 2026-10-23: dropped from `3.2.0.0`) | `xhciperf/build.cmd`, from `xhciperf/xhciperf.c`, `pf_core.c` and `pf_s98.c`, with `xhciperf/xhciperf.rc` embedding `xhciperf/xhciperf.manifest` and `xhciperf/xhciperf.ico` (drawn for this project); compiles Stor98's interface header `src/stor98/include/stor98pf.h`, BSD-3-Clause, carried with its notice (Stor98's decision MD-8) | in `XHCITR`'s way: `XHCIPERF.EXE` MSVC 6.0 C runtime, static (`/MT`, `/SUBSYSTEM:WINDOWS,4.0`); `XHCIPF64.EXE` WDK 7.1 amd64 C runtime (`LIBCMT`) | `xhciperf/check-imports.ps1`, run by `xhciperf/selftest.cmd` at every gated build, requires exactly `KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `SHELL32`, `COMCTL32` and `COMDLG32` and no delay-load import | `WNASPI32.DLL` and `OLE32.DLL` are the system's own, loaded at run time and never shipped. ATTO Disk Benchmark and CrystalDiskMark were the owner's references for the window's layout only (record 29 section 12): neither was run, read or examined, and nothing of theirs is in these files |
| `TBSPEED.EXE` and `TBTHREAD.EXE` (the TCP throughput reading and the thread creation report for the Thunderbolt networking bench, 2026-11-09; developer diagnostics no package carries) | `tbspeed/build.cmd`, from `tbspeed/tbspeed.c` and `tbs_core.c` (`TBSPEED.EXE`) and `tbspeed/tbthread.c` (`TBTHREAD.EXE`) | MSVC 6.0 C runtime, static: `TBSPEED.EXE` the single-threaded one (`/ML`), `TBTHREAD.EXE` the multithreaded one (`/MT`); `/SUBSYSTEM:CONSOLE,4.0` | The build line in `tbspeed/build.cmd`; the import tables name `KERNEL32.dll` and `WSOCK32.dll` (`TBSPEED.EXE`) and `KERNEL32.dll` alone (`TBTHREAD.EXE`), no C runtime DLL | `WSOCK32.DLL` is the system's own. iperf 2.0.8 and 1.7.0, whose failure on the bench led to these, were read as source for how they create a thread (facts only); nothing of theirs is in these files |

Stated plainly: `XHCISNAP.EXE` is a single executable combining this
project's GPL-2.0-only code with Microsoft's Visual C++ 6.0 static C runtime,
two licences in one file where the qualifier's three meet in the other. In
both cases the binary was published first and its provenance written down
afterwards, by an audit reading the linker map rather than by the cut
itself; section 6's rule for a new tracked build output is written against
that.

#### XHCIQUAL.EXE

`releases/<version>/tools/xhciqual/XHCIQUAL.EXE`, built from `xhciqual/`'s
own C and assembly, also holds:

- **DOS/32 Advanced DOS Extender, embedded as the stub.** `xhciqual/makefile`
  links `system dos32a`; its comment: "`system dos32a` embeds the full
  DOS/32A extender as the EXE stub ... so XHCIQUAL.EXE is one standalone
  file - no DOS4GW.EXE to carry." The linker map's head: "creating a DOS/32
  Advanced DOS Extender (LE-style) executable". Static, from the map and the
  makefile. The tracked map is `releases/<version>/tools/xhciqual/XHCIQUAL.MAP`,
  staged by every release beside the executable; `xhciqual/xhciqual.map` is
  the build's output, git-ignored.
- **Open Watcom C runtime, statically linked.** `XHCIQUAL.MAP` names the
  modules against `C:\WATCOM\lib386\dos\clib3r.lib` (`_strcmp`,
  `strncmp.c`, `fopen.c` and others). Static, from the map.

DOS/32A ships its licence as `binw/license.d32` in an Open Watcom install
(Copyright (C) 1996-2006 by Narech K.). Its clause 2: "Redistributions in
binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or other
materials provided with the distribution"; its clause 3: "The end-user
documentation included with the redistribution, if any, must include the
following acknowledgment: 'This product uses DOS/32 Advanced DOS Extender
technology.'" Open Watcom is distributed under the Sybase Open Watcom Public
License.

The first two cuts carried neither text, naming only this project's
licence. `scripts/package/make-release.ps1` now generates `NOTICE.TXT`
beside the executable reproducing the DOS/32A licence in full, the acknowledgement
included, and recording the Open Watcom linkage with the copyright lines the
linker writes at the head of the map; the qualifier's readme points at it.
Stated plainly: one executable combining this project's GPL-2.0-only code,
the DOS/32A stub and the Open Watcom runtime, three licences in one file.
That composition is what this section records; it draws no conclusion from
it.

#### XHCISNAP.EXE

In the table above.

#### XHCICLN.EXE and XHCICL64.EXE

In the table above.

#### XHCIINST.EXE and XHCIIN64.EXE

In the table above.

#### XHCIWIZ.EXE and XHCIWZ64.EXE

In the table above.

#### SETUP.EXE

In the table above.

#### XHCIPERF.EXE and XHCIPF64.EXE

In the table above.

#### TBSPEED.EXE and TBTHREAD.EXE

In the table above.

#### XHCITR.EXE and XHCITR64.EXE

In the table above.

#### XHCIDI.EXE

`releases/<version>/tools/xhciinst/XHCIDI.EXE`, the Windows 98 SE and ME
device install helper (task 42-A.1, design record 27), from `3.1.0.0` (cut
2026-10-19),
built by `xhciinst/di/build.cmd` from `xhciinst/di/xhcidi.c`, `di_sys.c`
and `di_core.c` (this project's own) with Open Watcom 2.0's 16-bit compiler
for the Windows 3.x target, large model. It also holds:

- **The Open Watcom C runtime for 16-bit Windows, statically linked.**
  `xhciinst/di/xhcidi.map` names the modules against `lib286\win\clibl.lib`
  (`wstart`, `cmodel`, `i4m`, `i4d`, `exit.c`, `initrtns.c`, `winrtns.c`
  and others) and carries the linker's two copyright lines at its head.
  Static, from the map.
- **Linked against Microsoft's `setupx.lib`** (Windows 98 SE DDK), an OMF
  import library whose records (an IMPDEF comment per routine) name module
  `SETUPX`, each routine and its ordinal, and carry no code. The image holds,
  in its NE relocation records, a module-and-ordinal reference per
  `SETUPX.DLL` routine called (`SETUPX.406`, `DiGetClassDevsEx` and ten
  more) and no byte of the library or of the headers it was compiled against
  (`inc16\setupx.h`, `prsht.h`, `windows.h`; hashes in
  `docs/references/README.md`, "Windows 98 DDK"). Re-derivable:
  `xhciinst/di/check-imports.ps1` reads the module reference table and
  relocation records and requires `KERNEL`, `USER` and `SETUPX` alone, every
  import by ordinal, each a row of `xhciinst/di/xhcidi-imports.allow`;
  `check-imports.ps1 -List` prints them, and Open Watcom's `wdump -a` shows
  the same table.

Nothing of either's text is quoted here or shipped beyond the Watcom
copyright lines. `make-release.ps1` adds a section for the helper to the
`NOTICE.TXT` in `tools/xhciinst/`: the copyright lines from that build's
`xhcidi.map`, the name of the Open Watcom licence, and a sentence that the
image references `SETUPX.DLL` of the running system by ordinal and carries
nothing of the DDK. Stated plainly: this project's GPL-2.0-only code with the
Open Watcom 16-bit C runtime, two licences in one file, linked by ordinal to
a module of the Windows it runs on.

#### Source files lifted from `usb4cm` (MIT)

`usb4-emu`'s `tools/usb4cm/` is a polling USB4 connection manager written
for that project; its `README.md` ("What xhci98 can lift") offers `core/`
for lifting into this driver. The Thunderbolt 3 client (`xhcicm.sys`, design
record 21) and the USB4 software connection manager (Phase 40) take the
files below, read at `usb4-emu` `main` commit `a68bc74` (for these paths
identical to `eba0cf1`, the commit record 21 cites).

| File (`tools/usb4cm/`) | Lines | Git blob | Origin, as its header states it | Lifted by |
|---|---|---|---|---|
| `core/u4hi.c` | 522 | `d5f705ed29d12ee42af16c4f0d97e5aab31ea342` | written from scratch from USB4 Specification 2.0 sections 6.4, 12.3, 12.4 and 12.6 | roadmap-usb4 task 39-A.2, as `src/cm/tbt_ring.c` |
| `core/u4regs.h` | 168 | `f4d4a15c775591c73e847f2d91d563b04d629eb8` | written from scratch from Specification 2.0 sections 12.6 and 8, "field positions cross-checked against tbtools registers.json" | 39-0.2 and 39-A.2, as `src/cm/tbt_regs.h` |
| `core/u4types.h` | 29 | `184e46e41f451abc18cfc9ac775c0fa3e03ef4d6` | written from scratch | 39-A.2 (design record 21 section 9) |
| `core/u4cm.c` | 754 | `826722d9c202e1b06439ddffb869c78979cd578c` | written from scratch from the Connection Manager Guide 2.0 and Specification 2.0 | Not lifted: Phase 40 (task 40-A.3) took it from `3c74189`, as an adaptation (the next subsection, `src/cm/tbt_u4cm.c`) |
| `core/usb4cm.h` | 224 | `6be9bab78443e7e8d6238ccf17287f7a23d9556d` | written from scratch from Specification 2.0 and the Connection Manager Guide 2.0 | Not lifted: Phase 40 (task 40-A.3) adapted `u4cm.c` from `3c74189` instead (the next subsection); `src/cm/tbt_u4cm.h` is this project's header, whose sources paragraph names `u4cm.c` (the notices table below) |

Not lifted, and not to be: `dos/usb4cm.c` (a DOS front end whose build links
DOS/32A and the Open Watcom runtime), `host/cm-host-test.c` (links
`usb4-emu`'s QEMU fabric model), and the build scripts.

The notices. Each of the five files opens with `Copyright (c) 2026 usb4-emu
contributors` and `SPDX-License-Identifier: MIT`. `usb4-emu`'s `LICENSE`
is the MIT License with `Copyright (c) 2026 Yeo Kheng Meng`; its
permission notice, carried in full by every lifted file:

```text
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The rule for a lifted file (design record 21 section 9): it keeps its
original copyright line and SPDX tag, carries the permission notice in full
and names the `usb4-emu` path and commit it came from; a change by this
project adds its own copyright line below the original and marks the file
changed, with the date, never replacing the original line; `LICENSE`'s scope
note names the lifted files in the change that lifts the first; this table
gains the `src/cm/` path as each lands.

`u4regs.h`'s header says its field positions were "cross-checked against
tbtools registers.json": Intel's tbtools, which `usb4-emu`'s
`docs/REFERENCES.md` records as MIT (`https://github.com/intel/tbtools`,
commit `aa0b1be590443d7074e799ebd6c72e308c5bdd02`), and whose rule there is
that files generated from `registers.json` carry the tbtools notice.
`u4regs.h` carries no tbtools notice and does not say it was generated. Task
39-A.2 (done 2026-10-15) lifted it as `src/cm/tbt_regs.h` with `usb4-emu`'s MIT notice and that header line as written, and no reading of it against the tbtools rule was recorded; the file carries no tbtools notice and no statement that any part of it was generated from `registers.json`, and since Phase 40 its offsets are cross-checked against the USB4 Specification 2.0 and Linux's `tb_regs.h` (its own header). Whether any of `u4regs.h` was generated from `registers.json` is not recorded here; a reading of that file's history in `usb4-emu` against tbtools at the commit above would settle it.

`usb4-emu`'s `main` was relicensed to GPL-2.0-only at `3c74189`
(2026-10-15). The files that have landed (`u4hi.c` as `src/cm/tbt_ring.c`,
`u4regs.h` as `src/cm/tbt_regs.h`, task 39-A.2) were lifted from `a68bc74`
with the MIT notice and keep it. A file taken from `3c74189` or later is an
adaptation under section 3's rule, entered in the next subsection.

#### Source files adapted for the Thunderbolt and USB4 work

The files adapted under section 3's rule for the Thunderbolt and USB4
mirrors, from 2026-10-15; one row per file, added in the change that adapts
it, keeping the notices that rule names.

| File in this tree | Source file | Commit read | What was adapted | Notices kept | Task |
|---|---|---|---|---|---|
| `src/cm/tbt_u4cm.c` | `usb4-emu` `tools/usb4cm/core/u4cm.c` (blob `921525880d13b772fbac1598ec66c8281ebcfac5`, GPL-2.0-only) | `3c74189` | the connection manager's sequence - router enumeration, adapter discovery by capability walk, port and protocol-adapter pairing, the router operation, the DROM read, lane bonding, the device router's connect, the Configuration Valid handshake, path programming, the PCIe and USB3 tunnels - translated and changed as design record 22 section 2.2 says | `Copyright (c) 2026 usb4-emu contributors`, with the owner's statement of 2026-10-25 below (the source's licence line, quoted until 2026-10-27, dropped at the owner's word) | 40-A.3 |
| `src/cm/tbt_u4cm.c`, until 2026-10-26 (rewritten, see the next table) | Linux `drivers/thunderbolt/tb.c`, `switch.c`, `tunnel.c`, `path.c`, `usb4.c`, `tmu.c`, `lc.c`, `quirks.c`, `cap.c`, `ctl.c`, `eeprom.c` (GPL-2.0) | `c6859eed755df351a3978b33cb92365f9b3e8f06` | sequences, constants and register uses, translated to C89: `tb_switch_configure`, `usb4_switch_setup`, `tb_plug_events_active`, `tb_switch_next_cap`, `usb4_switch_credits_init`, `tb_pci_init_credits`, `tb_available_credits`, `tb_path_activate`, `tb_tunnel_discover_pci` / `_usb3`, `tb_path_discover`, `usb4_usb3_port_allocate_bandwidth` / `_release_bandwidth`, `usb4_port_reset`, `tb_switch_lane_bonding_enable`, `tb_switch_tmu_post_time`, `tb_switch_tmu_enable` (HiFi-Bi), `tb_switch_tmu_disable`, `tb_lc_read_uuid`, `tb_lc_lane_bonding_possible`, `tb_lc_set_port_configured`, `tb_check_quirks` and its table; for a Thunderbolt 3 router behind a USB4 host, the upstream adapter from the read's response (`ctl.c` `tb_cfg_get_upstream_port`, `switch.c` `tb_switch_alloc`) and the EEPROM bit-bang DROM reader (`eeprom.c` `tb_eeprom_get_drom_offset`, `tb_eeprom_active`, `tb_eeprom_transfer`, `tb_eeprom_out`, `tb_eeprom_in`, `tb_eeprom_read_n`) | each file's copyright lines (Andreas Noever 2014; Intel Corporation 2018-2020) until 2026-10-26; none since | 40-A.3, 40-A.8; 44.38 |
| `src/cm/tbt_regs.h` (the Phase 40 block) | Linux `drivers/thunderbolt/tb_regs.h`, `tb_msgs.h` (GPL-2.0) | `c6859eed755df351a3978b33cb92365f9b3e8f06` | the cross-check of every field taken from the USB4 Specification 2.0, and the Thunderbolt 3 Plug Events and link controller fields, which the specification does not define | the file keeps `u4regs.h`'s MIT notice; the block cites Linux by file and symbol | 40-0.1 |
| `src/cm/tbt_ring.c`, `tbt_ring.h` (the USB4 transport) | `usb4-emu` `tools/usb4cm/core/u4hi.c` (`u4_hp_ack`, `u4cm_unsolicited`, `u4_xfer`; blob `869add9a38fb536933f084a81245e095bb598361`); and, until 2026-10-26, Linux `drivers/thunderbolt/ctl.c` (`tb_cfg_ack_plug`, `tb_cfg_ack_notification`) | `3c74189`; `c6859eed` | the hot plug acknowledgement and its queue, the write request on the read's transfer, the plain acknowledgement's form (the Linux part rewritten from Spec 6.4.2.7 to 6.4.2.10 and 6.6 in task 44.38) | the 39-A.2 MIT notice of `u4hi.c` at `a68bc74`, kept for the lifted text; from 2026-10-26 also the `3c74189` copyright line (`Copyright (c) 2026 usb4-emu contributors`) with the owner's statement below (its licence line, quoted until 2026-10-27, dropped at the owner's word); until then the file named only the MIT notice | 40-A.1; 44.38 |
| `test/u4_fakefab.c` (`fabEeByte`, `fabEeCtl` only) | `usb4-emu` `src/qemu/hw/usb4/usb4-router-tb3.c` (`eeprom_byte`, `eeprom_ctl`; GPL-2.0-only) | QEMU fork `2a893ea` | the Thunderbolt 3 router's EEPROM bit-bang model (a 25-series SPI part: chip select, 24 command and address bits, data out on each later rising clock edge) | `Copyright (c) 2026 usb4-emu contributors`, with the owner's statement below (the source's licence line, quoted until 2026-10-27, dropped at the owner's word) | 40-A.8 |
| `scripts/usb4-crosscheck/u4cm-crosscheck.c`, `run-crosscheck.sh` | `usb4-emu` `tools/usb4cm/host/cm-host-test.c`, `run-host-test.sh`, `src/qemu/tests/usb4/fabric-test.c` (GPL-2.0-only) | `3c74189`; QEMU fork `178237c`; QEMU fork `02dda40` (before the 2026-10-29 squash; `2e063be` on `usb4-emu` `main` after its merge) for task 44.42's two stubs and the asm4242 preset configuration, `2e063be` for the cold reconcile stub | the fakes for the fabric's neighbours, the virtual clock and timers, the ring 0 shim, the build | `Copyright (c) 2026 usb4-emu contributors`, with the owner's statement below (the source's licence line, quoted until 2026-10-27, dropped at the owner's word) | 40-A.5 |

The host suite's fake fabric (`test/u4_fakefab.c`) is otherwise written for
this project from the specification and `usb4-emu`'s documents; it adapts no
code but the EEPROM model above. `usb4-emu`'s fabric model
(`src/qemu/hw/usb4/`) is compiled in place, never copied, by task 40-A.5's
cross-check.

**The owner's statement of 2026-10-25 (task 44.38).** The owner, who holds
the copyright of `usb4-emu` (its `LICENSE`: "Copyright (c) 2026 Yeo Kheng
Meng"; its `NOTICE`: "The maintainer, who holds the copyright in all of
it"), states that the code this project takes from `usb4-emu` is offered to
it under GPL-2.0-or-later; `usb4-emu` itself stays GPL-2.0-only. The files
taking `usb4-emu` text under it are this table's rows that name `usb4-emu`
as their source; each keeps the copyright line of the file it came from and
names this statement. The files lifted while `usb4-emu` was MIT
(`tbt_regs.h`, and `tbt_ring.c`'s lifted text, with `tbt_ring.h`) keep that
MIT notice.

**Rewritten from, task 44.38 (2026-10-26).** The portions of
`src/cm/tbt_u4cm.c` and `src/cm/tbt_ring.c` that tasks 40-A.1, 40-A.3 and
40-A.8 had adapted from Linux `drivers/thunderbolt` were rewritten without
reading the Linux text, from the sources below, each step held by a request
trace of the host suites and of the `usb4-emu` cross-check that stayed
identical to the one captured before (`scripts/usb4-trace/u4-trace.ps1`;
`runs/run-44.md`, "44.38"). A value or order whose only source is Linux's
behaviour is cited at its site as a fact and tagged [linux], through design
record 22.

| File in this tree | What was rewritten | Rewritten from | Commit |
|---|---|---|---|
| `src/cm/tbt_ring.c` | the plain and enhanced notification acknowledgements (`tbtCtlSendAck`) and the hot plug acknowledgement (`tbtCtlHpAck`) | USB4 Specification 2.0 6.4.2.7 to 6.4.2.10, 6.6, 6.8, Tables 6-6 to 6-9 and 6-12; `usb4-emu` `u4hi.c` `u4_hp_ack` | `a20ddbf4` |
| `src/cm/tbt_u4cm.c` | the USB3 bandwidth group: the unit conversions, the Request and Ack handshake, the allocation, consumed and maximum-rate reads, the release, the tunnel's budget | Spec 8.2.2.5 Table 8-14; Guide 6.1.2.3, 6.2.2.1, 6.2.2.2 (Figure 6-3), Table 6-2 | `cf0cc8fc`, `c46b1387` |
| `src/cm/tbt_u4cm.c` | path teardown, the credits left on a lane, the cleaning's sweep, the Buffer Allocation Request | Guide 5.1.1 to 5.1.4, 5.2.1, 5.2.2; Spec 8.2.3.2, Table 5-16, Tables 8-56 to 8-58 | `722c74a9` |
| `src/cm/tbt_u4cm.c` | the Thunderbolt 3 vendor capabilities and the EEPROM DROM read | Spec 13.4.5, 13.6.1.2 (Table 13-15), 13.6.1.5 (Tables 13-18, 13-19), 8.2.1.3, 8.2.1.4; `usb4-emu` `docs/ICM-PROTOCOL.md` 5.1 and 5.4 and `usb4-router-tb3.c` | `e51c0dc6`, `5ffeae00` |
| `src/cm/tbt_u4cm.c` | the TMU: the host router's rate, Post Time, HiFi-Bi from Off, the disable | Guide 7.4, 7.4.3.1, 7.4.3.4, 7.4.3.4.1, 7.4.3.4.2, Tables 7-2 and 7-3 | `8f34ee11`, `1fb06dbc` |
| `src/cm/tbt_u4cm.c` | lane bonding and its single-lane fallback, the configured mark, the unconfigure | Guide 3.4.1, 7.1, 7.5; Spec 4.1.2.3, 8.2.2.3, 8.2.2.4, 13.6.1.5; `usb4-emu` `u4cm.c` `bond_lanes` | `2f3c30ae`, `6c7f9487` |
| `src/cm/tbt_u4cm.c` | the PCIe tunnel's order and the discovery of a tunnel already there | Guide 5.3.1, 5.5.1, Table 5-2; Spec 8.2.3.2; `usb4-emu` `u4cm.c` `u4cm_pcie_tunnel`; design record 22 section 9.2 | `bc3e4918` |
| `src/cm/tbt_u4cm.c` | the cited portions of the other functions: the capability walk, the DROM's entry start, the protocol adapter's fields, the timings, the pairing | Spec 8.2.1, 8.2.2.4, 13.6; Guide 3.1.1, 3.2.1, 3.5, 5.1.4, 5.2.1, 7.3; USB4 DROM Specification 1.2 sections 4 to 6.1 and 8; `usb4-emu` `u4cm.c` | `813e958f` |

**Rewritten from, task 44.65 (2026-10-27).** `hcdQuirkAsmWait` in
`src/hcd_quirk.c`, the ASM1042A's bounded wait before each half of its
vendor flow-control write (design record 20 section 4.8, Q9), written from
that record in task 38.10 (its section 13.6), was rewritten at the owner's
word (2026-10-27) without reading any Linux text, from the sources below.
Its contract was written down from the project's own code and record 20
before the new body; the old body was read only afterwards, to confirm the
same reads, stalls, bound and results. The loop moved to the pure half
(`XhciQuirkAsmWait`), pinned by the host suite `test_quirk`
(`runs/run-44.md`, "44.65").

| File in this tree | What was rewritten | Rewritten from | Commit |
|---|---|---|---|
| `src/hcd_quirk.c`, `src/xhci_quirk.c`, `src/xhci_quirk.h` | the wait for the write bit of configuration byte E0h to clear: up to 1000 reads 50 us apart, giving up on a read of FFh or at the bound (`hcdQuirkAsmWait`, `XhciQuirkAsmWait`) | design record 20 section 4.8 (the register, the write-busy bit, the condition and the bound, cited there as facts); the bit's value from the existing `HCD_QK_ASM_WRITE_BIT`; the callers' contract in `hcd_quirk.c` (`HcdQuirkAsmFlow`, `hcdQuirkConfigByte`, the `XHCI_QK_WHY_*` reasons of `xhci_quirk.h`) | `3a01b4d6`, `a7cc978a` |

**The notices each of these files carries from 2026-10-26.**

| File | Notices |
|---|---|
| `src/cm/tbt_u4cm.c` | `SPDX-License-Identifier: GPL-2.0-or-later`; `u4cm.c`'s notice quoted with the owner's statement above; no Linux copyright line |
| `src/cm/tbt_u4cm.h` | none of its own (`LICENSE`); its sources paragraph names `u4cm.c` and the owner's statement |
| `src/cm/tbt_ring.c` | `u4hi.c`'s MIT notice of `a68bc74` in full (the permission notice reproduced in `tbt_regs.h`); `u4hi.c`'s `3c74189` notice quoted with the owner's statement above; `LICENSE` for this project's changes |
| `src/cm/tbt_ring.h` | none of its own; it names `tbt_ring.c`'s |
| `src/cm/tbt_regs.h` | `u4regs.h`'s MIT notice, unchanged |
| `test/u4_fakefab.c` | `usb4-router-tb3.c`'s notice quoted with the owner's statement above; `LICENSE` |
| `scripts/usb4-crosscheck/u4cm-crosscheck.c`, `run-crosscheck.sh` | the `usb4-emu` files' notice quoted with the owner's statement above; `LICENSE` |
| `scripts/usb4-crosscheck/u4cm-glue.c`, `u4cm-glue.h` | `SPDX-License-Identifier: GPL-2.0-or-later` |

#### Third-party headers incorporated unchanged

A header another project publishes as an interface definition, carried byte
for byte with its own licence notice so both sides build against one
definition. It is never edited here: a change is made in its own repository
and copied in again, with this row updated. For `stor98cm.h` that repository
is, since 2026-10-19, `src/stor98/` of this one ("Stor98 in this tree"): the
change is made in `src/stor98/include/stor98cm.h` and copied to `src/cm/`
unchanged.

| File in this tree | Source | Commit read | SHA-256 | Licence | Why | Task |
|---|---|---|---|---|---|---|
| `src/cm/stor98cm.h` | Stor98 `include/stor98cm.h` (the `nvme98` project's `include/nvme98cm.h` until its rename, 2026-10-16) | `56954657` of this repository (`src/stor98/include/stor98cm.h`, 2026-10-22: R11 O6's clause for a connection manager with no per-port teardown, contract text only, interface version 4 and the layout unchanged); before it `f9755aee7a27725e2a0edfb0d8c1e1231669f777` (contract text only, on Stor98's branch `audit-2026-10-18`, read 2026-10-18) and `6609c1c36701e50665daf0305fdc35fe06e258d6`, read 2026-10-16 | `8401af56d65fdf8a4617f3ac0d50ddc83a06ab750d9af1c7afafc9c824e6ab41` | BSD-3-Clause, `Copyright (c) 2026, Yeo Kheng Meng`, the notice kept in the file; the file's own text says it is licensed on its own, independently of the code on either side, and asks that no GPL-licensed code or text be put into it | the link-state table, callback object, 9x locator and current-boot proof Stor98 adopted as its connection-manager interface (decisions MD-5 and MD-6, gate G3, interface version 4); `src/cm/cm_nvlink.c` and `cm_nvlinkos.c` implement this side, on NT and on 98 SE and ME (the 9x half read on 2026-10-16, `runs/run-41.md`, "The Stor98 link-state channel on 98 SE and ME") | 39-A.7, 40-A.7, and the Stor98 rename of 2026-10-16 (`runs/run-41.md`, "Stor98 behind the Thunderbolt 3 controller") |

The file uses `//` comments, which AGENTS.md's C89 rule forbids in this
project's own source: the recorded exception, a third-party header carried
unchanged. MSVC 6.0 (the DDK's flags for every x86 flavour, the host suites'
`/Za /W3 /WX`) and WDK 7.1's amd64 compiler accept it as it is, so no
wrapper is needed. Nothing from this repository moves the other way: Stor98
is BSD-3-Clause, and this project's code stays out of it.

### The bundled Stor98 build

From `3.0.0.0` the release download may carry Stor98, an NVMe storage
driver under its own licence (owner, 2026-10-16). Since `3.1.0.0` (task
42-A.30, owner 2026-10-19) it is built from `src/stor98/` ("Stor98 in this
tree") with the drivers, at the package's version and date.

- **What is shipped.** The Stor98 that `scripts/build-driver.cmd` built and
  gated from `src/stor98/`: `stor98.inf`, `stor98.mpd` and `stor98.sys` at
  the root of `release-x86/`, `stor98.inf` and `stor98.sys` at the root of
  `release-x64/`, no `stor98/` folder; every name starts with `stor98`.
  Binaries only, no Stor98 source; the `debug-` directories carry none of it.
- **The licence files.** Stor98's `LICENSE` (BSD-3-Clause) and `NOTICE`
  (where each part came from) ship beside them in both directories as
  `stor98-LICENSE.txt` and `stor98-NOTICE.txt`, with its
  `docs/package-readme.txt` as `stor98-readme.txt`.
- **Signing.** The x64 `stor98.sys` carries the same embedded test signature
  as this project's other x64 drivers, by this project's certificate,
  applied by `make-package.ps1 -TestSignPfx` in the same step (section 2's
  test-signature row; design record 19). Stor98's own test certificate
  (`CN=stor98 project test signing (not trusted)`, SHA-1 thumbprint
  `461311C79165DC7985522D46274DCAD8832DD919`), which signed the bundled
  `stor98.sys` of `3.0.0.0` (Stor98 `0.4.0.0`, with `stor98-test.cer`
  beside it), signs nothing any more, and `stor98-test.cer` is not shipped.
- **What is tracked.** From `3.1.0.0` the Stor98 files of a `release-`
  directory are published in `releases/<version>/` with this project's own.
  `3.0.0.0`'s were copied into its upload set alone, from the pinned release
  folder (four `-Stor98*` parameters, since removed), tracked now in
  `src/stor98/releases/0.4.0.0/`. The generated `readme.txt` and
  `docs/using/release-notes.md` name the bundled version.
- **The other direction.** No code of this project or of `usb4-emu` is
  copied into Stor98 (AGENTS.md, "Reference Implementations"). Its version
  header, `src/stor98/src/stor98ver.h`, includes this project's
  `src/xhci_version.h` for the version number alone (`#define` lines); no
  code crosses.

### Stor98 in this tree (`src/stor98/`)

Since 2026-10-19 (task 42-A.20; owner) the Stor98 project is in this
repository as `src/stor98/`: every tracked file of its own repository's
branch `audit-2026-10-18` at commit `4253b58` (its `0.4.0.0` release and the
reviewed fixes of its audit of 2026-10-18), copied as one commit with every
blob identical to that commit's and none of its history.

- **Licence.** BSD-3-Clause, the whole of `src/stor98/` (its `LICENSE`, with
  four copyright lines: Dominik Behr for nvme2k, Dominik Behr and Ray
  Hinchliffe for the nvme2k INF its INFs derive from, SweetLow for the
  nvme9x/nvment port, Yeo Kheng Meng for Stor98). This project's `LICENSE`
  (GPL-2.0-or-later since 2026-10-26, GPL-2.0-only before) is not its
  licence; the root `LICENSE`'s scope note says so.
- **Provenance.** Stor98's `NOTICE` is the record and governs; this repeats
  it. Stor98's `src/` was imported from SweetLow's nvme9x/nvment source
  (`nvme2k-src.zip` of `https://github.com/LordOfMice/Tools`, SHA-256
  `272b50836d7f9fe4c1a483f5868d099604916ccd4c89b4daacbc1d17c80bb4f2`),
  without the Microsoft headers `SCSI.H` and `SRB.H` it bundled and with
  BSD-3-Clause headers added, and is Stor98's development of it since.
  SweetLow stated on 2026-10-14 that his port and changes are BSD-3-Clause,
  the licence of the original nvme2k, and his `NVMEVSD.VXD` public domain
  (neither its source nor its binary is in the tree). That code derives from
  Dominik Behr's nvme2k 1.0.0.2, whose licence the `NOTICE` records as read
  in the upstream history (2026-10-19, 42-A.19): at tag `1.0.0.2` (commit
  `cbbadf0`) the `LICENSE` is the BSD-3-Clause text with the template's
  unfilled copyright line, `Copyright (c) <year>, <copyright holder>`, and
  the `README.md` names the 3-Clause BSD License; commit `eb31419`
  (2025-12-02) filled in "Copyright (c) 2025, Dominik Behr" and changed
  nothing else, so 1.0.0.2 is BSD-3-Clause on 1.2.0.0's terms.
  `third_party/nvme2k-1.2.0.0/` is an unmodified copy of
  `https://github.com/techomancer/nvme2k` at commit `0c41727` (release
  1.2.0.0), with its `LICENSE` (BSD-3-Clause). `include/stor98cm.h` is
  Stor98's own, licensed on its own as an interface definition.
- **Its rules.** Stor98's `AGENTS.md` keeps its own provenance rules (every
  nvme2k-derived file keeps Dominik Behr's notice, the reference copy stays
  unmodified, no Microsoft kit file is committed). This project's source
  rules (C89, ASCII, the gates of `build-driver.cmd`) do not bind it.
- **Tracked binaries.** Its released builds up to `0.4.0.0` as Stor98
  committed them (history since `3.1.0.0`, which builds it with this
  package), `src/stor98/releases/0.3.0.1/` and `0.4.0.0/`: `stor98.mpd` and
  `stor98.sys` (x86), `stor98.sys` (x64) with the embedded test signature by
  Stor98's own certificate, and its public half `stor98-test.cer`, each
  listed in the folder's `SHA256SUMS.txt`. The `0.4.0.0` folder's
  `SHA256SUMS.txt` hashes to
  `ef7a230207789ad2a8b6f185988e0328f9f10d6727b4187d95cfbf38889c96b7`, the pin
  `3.0.0.0` bundled it under.
- **Not tracked.** Its build needs `SCSI.H` and `SRB.H` from a Windows 98 (or
  NT4) DDK, kept in the git-ignored `tools/w98ddk/inc/`: `SCSI.H` 36,501
  bytes, SHA-256
  `809d57e6756de21524930eb4d17840ab3047c7e1773da634ceb73a81ff8d39b7`;
  `SRB.H` 21,056 bytes, SHA-256
  `4b3c3305686f2593ceb471ae69f1d690de5d833c8f2047859d735b21affd8396` (the
  copies that came with `nvme2k-src.zip`; `docs/references/README.md`,
  "Windows 98 DDK"). Its build output (`src/stor98/out/`, `obj*/`) is
  ignored by its own `.gitignore`.
- **The interface header.** `src/cm/stor98cm.h` (section 2a, "Third-party
  headers incorporated unchanged") is a copy of the master
  `src/stor98/include/stor98cm.h`; `scripts/check-stor98-header.ps1` holds
  the two to the same bytes until they are made one.

### The third-party PDFs

Both PDFs were once tracked here, at four paths between them (the xHCI
specification moved twice):

- `docs/extensible-host-controler-interface-usb-xhci.pdf` (the original,
  misspelled "controler")
- `docs/extensible-host-controller-interface-usb-xhci.pdf` (the spelling fix)
- `docs/references/extensible-host-controller-interface-usb-xhci.pdf` (the
  move into `docs/references/`)
- `docs/references/xhci-backwards-compatibility-testing-v1-7.pdf` (the USB-IF
  document, one path only)

No third-party PDF is tracked now, and the published repository starts from
a single commit, so no historical blob of either exists. The check walks
every ref and must print nothing:

```sh
git log --all --pretty=format: --name-only | grep -i '\.pdf$' | sort -u
```

Their licence limits are in `docs/references/README.md`: the Intel
specification reserves all rights and grants no licence in the document
itself; the USB-IF test document is licensed "FOR INTERNAL USE ONLY". No
applicable public-redistribution permission is documented for either - a
statement about what this project has established, not a claim that none
could exist. Intel's xHCI Adopters Agreement section 3.2, for instance,
contains a limited, non-transferable licence for an Adopter to reproduce the
final specification as necessary to exercise the agreement's patent rights;
this project has not established that its maintainer is an Adopter or that
public Git redistribution would fall inside that grant.

### The Microsoft public symbol files

Since 2026-09-11 the static readings of the NT 5.x and 6.x `usbport.sys` and
`usbehci.sys` are taken with Microsoft's public symbol files loaded (`x
usbport!MPf_*`, `uf`). They are third-party material like the binaries:
**never tracked**, fetched per machine into git-ignored `tools/symbols/`,
identified here so a clone can fetch the same thirty-nine: twelve for the
two miniport-facing binaries; since 2026-09-17 six for `usbhub.sys` (the
issue 5 selective-suspend readings); since 2026-10-02 twenty-one for Phase
25 (design record 13): fourteen for the class drivers (task 25.2), five for
`usbui.dll` (25.4), two for `usbccgp.sys` (25.6). Each is
`https://msdl.microsoft.com/download/symbols/<name>/<GUID><age>/<name>`,
the GUID and age from the binary's own `RSDS` debug directory
(`link /dump /headers`), which is what makes the pairing verifiable;
`kd -z <binary> -y srv*<repo>\tools\symbols` loads them offline.

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
| `vista-x86/usbhub.sys` | `usbhub.pdb/9C123E3E318C46E591F4DEA40A1076452` | 536,576 |
| `vista-x64/usbhub.sys` | `usbhub.pdb/BF8005633F9646A397498252605BF79A1` | 225,280 |
| `win7-x86/usbhub.sys` | `usbhub.pdb/C8AAA35CAB46490FBA4B37ED793CC0022` | 602,112 |
| `win7-x64/usbhub.sys` | `usbhub.pdb/A47E065E52B543548CC1110A51D008D21` | 249,856 |
| `winxpsp3/usbhub.sys` | `usbhub.pdb/A743565664594F7592F40645D0B71F3A1` | 93,184 |
| `winxp64/usbhub.sys` | `usbhub.pdb/1DE025D0401446F3AF7E1E93BB4BD2F51` | 101,376 |
| `winxp64/class/usbstor.sys` 5.2.3790.3959 | `usbstor.pdb/7A9D13AAAD404CC4BB17DA28D4B761F61` | 60,416 |
| `winxp64/class/usbaudio.sys` 5.2.3790.3959 | `usbaudio.pdb/7B11241A27724E8CB5FDAD1287540BAD1` | 93,184 |
| `vista-x86/class/hidusb.sys` 6.0.6002.18005 | `hidusb.pdb/FCCC8BE329084CDCB5F6ADFF2C0638C21` | 102,400 |
| `vista-x86/class/USBSTOR.SYS` | `usbstor.pdb/74470CDBA465459CB4934412AD5B96891` (a private PDB: names and types) | 159,744 |
| `vista-x86/class/USBAUDIO.sys` | `USBAudio.pdb/FFD1D4D70F764C5B8BD20F631E06E01C1` (private) | 184,320 |
| `vista-x64/class/hidusb.sys` | `hidusb.pdb/1CF06025009B4E74AD429AEFA292BD561` | 86,016 |
| `vista-x64/class/USBSTOR.SYS` | `usbstor.pdb/6F7C26B00C0D4339B4B4D1D2659457781` | 126,976 |
| `vista-x64/class/USBAUDIO.sys` | `USBAudio.pdb/CB89A1C69078406A910D9831B7FCDB0E1` (private) | 151,552 |
| `win7-x86/class/hidusb.sys` 6.1.7601.17514 | `hidusb.pdb/9C9A85942CDA40FBBF5C2E6D0AB14C671` (private) | 135,168 |
| `win7-x86/class/USBSTOR.SYS` | `usbstor.pdb/63312B088857481B9241258C80288AEB1` (private) | 176,128 |
| `win7-x86/class/USBAUDIO.sys` | `USBAudio.pdb/1C621A243F0345D3923D2723A61C3B0A1` (private) | 208,896 |
| `win7-x64/class/hidusb.sys` | `hidusb.pdb/57B441A60E0C47FDBEEF72B8B1C797AD1` (private) | 102,400 |
| `win7-x64/class/USBSTOR.SYS` | `usbstor.pdb/25EB307048DD48ACABCDDB5450AC275E1` | 135,168 |
| `win7-x64/class/USBAUDIO.sys` | `USBAudio.pdb/683FA3370D184C448A05FA9726D9EDE91` (private) | 167,936 |
| `winxpsp3/ui/usbui.dll` 5.1.2600.5512 | `usbui.pdb/3D7E68E1BB304FC99FDBD185EA20A9AB1` | 117,760 |
| `vista-x86/ui/usbui.dll` 6.0.6001.18000 | `usbui.pdb/AF30D5FA26934696BCAA36B3606803581` | 200,704 |
| `vista-x64/ui/usbui.dll` 6.0.6000.16386 | `usbui.pdb/636E7BEE7D334F049EA666BC6E5D45941` | 249,856 |
| `win7-x86/ui/usbui.dll` 6.1.7600.16385 | `usbui.pdb/40E7B5489A444284B06225B9AA04A3191` | 200,704 |
| `win7-x64/ui/usbui.dll` 6.1.7600.16385 | `usbui.pdb/CEF38F196EC24DA2A626627AD0A5DA691` | 249,856 |
| `win7-x86/class/usbccgp.sys` 6.1.7601.17514 | `usbccgp.pdb/98139E8789364020AED0AC05265F1B941` | 184,320 |
| `sweetlow/XP_SP3.QFE/USBCCGP.SYS` 5.1.2600.5585 (identical to `nusb36/usbccgp.sys`) | `usbccgp.pdb/7574CEABA313461FA33512EE9B0C17351` | 44,032 |

Since 2026-10-10 a further set serves design record 13 section 11.6.1's HAL
reading: the public PDBs of XP SP3's seven HALs, of the XP x64, Vista SP2 and
7 SP1 HALs (x86 and x64), of the kernels whose DMA verifier was read and of
two `pci.sys` builds; their identifiers are in that section's binaries table.
The Windows 2000 SP4 HALs have none on the server.

Windows 2000 SP4's and NUSB's `usbport.sys` were read without symbols. A
symbol name is a fact about the binary like an offset, recorded under the
same `static` tag; no function body is copied from any of them.

---

## 3. ReactOS: used as interface documentation, not as source

ReactOS is GPL-2.0, and this project is GPL-2.0-or-later (GPL-2.0-only until
2026-10-26, task 44.38). The two are combined nowhere, and the shared licence
does not change that. What makes `src/` this project's own work is that it
was written independently against recorded interface facts; that is a
statement about authorship, not about whether two licences would have
permitted a copy. The rules below and the overlap audit's result are
unchanged by the licence.

What is taken from ReactOS is the description of an interface: structure
layouts, field offsets, function signatures, constants, and the call ordering
its implementation performs, recorded in
`docs/usb-xhci-info/usbport-miniport-abi.md` with `file:line` citations into
the pinned mirror (`reactos/reactos` commit
`0298e10d5d904a0230868be8f7bdf6436d589c62`). `src/` is written independently
against those facts. Enforced by convention and review:

- No ReactOS function body is copied into `src/`;
  `docs/usb-xhci-info/usbport-miniport-abi.md` contains none either and
  carries the do-not-copy rule at its head.
- `external/` is a read-only local cache, git-ignored, not redistributed.
- The overlap audit checked mechanically and found no non-interface verbatim
  overlap between `src/` and the ReactOS, Linux, Haiku or FreeBSD mirrors. It
  found 37 matching `#define`/typedef lines in `src/xhci_usbport.h`: the
  interface constants and signatures the miniport must declare to be callable
  at all, this section's category. An audit result, not a legal conclusion.
  Demoting a reference does not narrow it: it swept `src/` against all four
  mirrors.

Linux (GPL-2.0), FreeBSD (BSD 2-Clause) and Haiku (MIT) are cross-references
for xHCI hardware behaviour, which the xHCI specification independently
specifies; the same do-not-copy rule applies. Linux is consulted routinely,
above all for the controller quirk population, the stated authority for
`xhciqual/quirks.c`. FreeBSD and Haiku carry nothing today: the 64-bit
register write order is settled by the specification as a `shall` (5.1,
p.337), and the Route String tier order that once rested on the two agreeing
was settled by the controller in batch 7b-A, which resolved this driver's
route strings to the physically correct devices on both usbport builds
(`docs/usb-xhci-info/xhci-data-structures.md`, "Route String tier order").
They stay mirrored as a cheap second opinion.

**The Linux xHCI quirk exception** (owner, 2026-10-12, for roadmap-hcd task
37.17, removed from Phase 37 the same day, so nothing was copied under it;
widened on 2026-10-13 for task 38.10, which took 37.17's place). The facts
then: this project was GPL-2.0-only (`LICENSE`), and Linux's xHCI host
driver files carry a GPL-2.0 SPDX identifier. The decision: the table of
which vendor, device and revision ids carry which quirk, with the comments
that name the hardware, and since 2026-10-13 the code that applies each quirk
taken, may be copied or adapted, under these conditions:

- only from a file whose SPDX identifier is `GPL-2.0`, `GPL-2.0-only` or
  `GPL-2.0+` / `GPL-2.0-or-later`, read on the pinned mirror's commit; a file
  under any other licence, or dual-licensed with one this project cannot
  carry, stays under the do-not-copy rule;
- every copyright, licence and warranty-disclaimer notice of the source
  file, its SPDX identifier among them, is kept beside the copied text
  (GPL-2.0 section 1), and the copy is marked changed, with the date
  (section 2(a)); a copy from a `GPL-2.0-or-later` file keeps that grant as
  received, nothing of it relicensed;
- this section gains a row per copy: the Linux file, the commit (the pinned
  mirror's, `external/README.md`), the lines, where it landed;
- a release whose binary carries such a copy says so in its notices, naming
  the Linux file and its copyright holders, as the tools' `NOTICE.TXT` files
  do, and offers the release's complete corresponding source (GPL-2.0
  section 3), the adapted code and the build and packaging scripts included,
  with the binary: the release's tag in this public repository, named in the
  release notes beside the download, checked at the cut;
- what is taken is the quirk and its handling, adapted to this driver's
  structures, nothing else of Linux's xHCI host driver;
- the overlap audit treats the recorded rows as known overlap; any other
  verbatim overlap with the Linux mirror is still a finding.

Since 2026-10-26 (task 44.38, code GPL-2.0-or-later): nothing was copied
under this exception (the table is empty), and task 44.38's audit of design
record 20's quirks found nothing copied in `src/xhci_quirk.c`,
`src/hcd_quirk.c` or `xhciqual/quirks.c`. Whether the exception stays open
for a `GPL-2.0` or `GPL-2.0-only` file is the owner's to decide; until then
nothing is copied under it from such a file (owner, 2026-10-26).

| Linux file | Commit | Lines | Landed in | Notice kept |
| (none yet) | | | | |

One later reading of source, not binaries: for roadmap task 33.3
(2026-10-04), ReactOS's `drivers/usb/usbhub/pnp.c`
(`USBH_FdoQueryBusRelations`) and Linux's `drivers/usb/core/hub.c`, from
their master branches as fetched that day into a scratch directory (not the
pinned mirrors, not kept), were read for the shape and timing constants of a
hub driver's first enumeration; only those facts are recorded (design record
13 section 5.7), nothing taken into `src/`. The same section cites
Microsoft's USB blog post "How does USB stack enumerate a device?" and USB
2.0 section 9.2.6.4, documentation rather than binaries, so no section 4 row.

This records the decisions and their conditions; it draws no legal
conclusion.

---

### The Thunderbolt and USB4 mirrors (owner, 2026-10-15)

Until 2026-10-15 design record 21 did not open Linux's `drivers/thunderbolt`:
its contract was `usb4-emu`'s `ICM-PROTOCOL.md` alone, a clean-room
description (section 4, the `ICM-PROTOCOL` rows). On 2026-10-15 the owner
opened two trees for roadmap-usb4's work (Phase 39, the firmware connection
manager client; Phase 40, the USB4 software connection manager), so the
driver can support as many controllers as possible, under the same terms as
the mirrors above:

| Tree | Pinned commit | Files | Licence as the files state it |
|---|---|---|---|
| Linux `drivers/thunderbolt` and `include/linux/thunderbolt.h` | `c6859eed755df351a3978b33cb92365f9b3e8f06` | 40, and the header | every file `GPL-2.0` or `GPL-2.0-only` SPDX |
| Linux `drivers/net/thunderbolt` (`main.c`, `trace.c`, `trace.h`, `Kconfig`, `Makefile`), added 2026-10-31 for task 45.59 (design record 35 section 2.4) | the same | 5 | `main.c` `GPL-2.0` SPDX; read for facts about the USB4NET peer the legs run against, nothing adapted, quoted or paraphrased into `src/` |
| FreeBSD `sys/dev/thunderbolt` | `9d50245a0f1333e10cc1c4dda16878cd79a7c42b` (2026-10-14) | 21 | every file `BSD-2-Clause` or `BSD-2-Clause-FreeBSD` SPDX and `Copyright (c) 2022 Scott Long` |

What is taken: device ids, message codes and layouts, register and field
offsets, constants, sequences and controller quirks, each cited by file and
symbol where used; section 4 records the classes. Both mirrors are in
`external/` (`external/README.md` for fetching). On 2026-10-15 every mirrored
file matched the upstream blob at its pinned commit once CR-LF was normalised
to LF; Linux has no `include/uapi/linux/thunderbolt.h` at that commit, so
neither has the mirror. `ICM-PROTOCOL.md`, written clean-room under
`usb4-emu`'s former rule and stated there to remain accurate, stays the
contract record 21 cites first; a statement it tags [inf] (what Linux's
client does) may be checked against `icm.c` and cited there as well.

The decisions, in order:

- **2026-10-15, first:** reading and citing only, no function body of either
  tree copied.
- **2026-10-15, widened** by the owner through the `usb4-emu` maintainer's
  note, effective when `usb4-emu`'s relicensing from MIT to GPL-2.0-only
  landed on its `main`. It landed at `3c74189` (2026-10-15), whose handoff
  (`docs/XHCI98-HANDOFF.md` section 3) reads: "xhci98 may adapt code
  directly from this repo (`tools/usb4cm` included) and from Linux
  `drivers/thunderbolt` (GPL-2.0-only), as well as FreeBSD
  `sys/dev/thunderbolt` (BSD-2) and Intel tbtools data (MIT). Keep the
  notices of whatever you adapt: the copyright lines of the source file, its
  SPDX tag, and an entry in xhci98's provenance record naming the source file
  and pinned commit". The rule kept for it, for the Thunderbolt and USB4 work
  only (Phases 39 and 40, design records 21 and 22, `src/cm/`), is
  `AGENTS.md`, "Reference Implementations"; what this record adds to it:
  a file adapted from Linux carries `SPDX-License-Identifier: GPL-2.0-only`
  with the source's copyright lines; a file adapted from `usb4-emu` at or
  after `3c74189` keeps `usb4-emu`'s copyright line and the owner's statement
  of 2026-10-25 (section 2a) and is under this project's GPL-2.0-or-later,
  having quoted the source's GPL-2.0-only tag until 2026-10-27, when the
  owner had that quotation dropped; a FreeBSD file keeps its BSD-2-Clause
  copyright line, conditions and disclaimer verbatim, tbtools data its MIT
  copyright and permission notice verbatim, and a `usb4cm` file lifted from a
  commit before `3c74189` the MIT notice of that commit (section 2a, "Source
  files lifted from `usb4cm`"); nothing moves from this repository or from
  `usb4-emu` into Stor98, which is BSD-3-Clause; an adapted file is marked
  changed, with the date, and its header names the source file; its section 2a entry
  ("Source files adapted for the Thunderbolt and USB4 work") names the
  source file, the commit actually read (the Linux and FreeBSD pins above;
  the `usb4-emu` commit read, `3c74189` or later; the tbtools commit read),
  what was adapted and where it landed, in the same change; a release whose
  binary carries adapted code says so in its notices, naming the source
  files and their copyright holders, and offers its corresponding source as
  the Linux xHCI exception does (taken over from that exception as an
  orchestrator decision of 2026-10-15, for the owner to confirm); the
  overlap audit treats the recorded entries as known overlap, any other
  verbatim overlap with these mirrors still a finding. Every other mirror,
  and Linux and FreeBSD outside this work, stays under the do-not-copy rule,
  with the xHCI quirk exception as the only other one.
- **2026-10-26 (task 44.38)**, with this project's code GPL-2.0-or-later: the
  portions adapted from Linux were rewritten from the specifications (section
  2a, "Rewritten from"); no adapted Linux code remains. Linux and FreeBSD
  stay mirrored and are read for the facts above, cited by file and symbol, a
  fact from Linux alone tagged [linux]; code is no longer adapted from a
  `GPL-2.0` or `GPL-2.0-only` file (owner, 2026-10-26). Code taken from
  `usb4-emu` is offered under GPL-2.0-or-later by its copyright holder
  (owner, 2026-10-25; section 2a) and keeps the notice of the file it came
  from.

This records the decisions, the text they rest on and what was fetched; it
draws no legal conclusion.

## 4. Facts read out of shipping binaries: a source-method inventory

"Binary-confirmed" in this project's documents means "checked against the
shipping binary", almost always by a static read of a disassembly listing,
not "observed at runtime". Each fact is tagged with the method actually used
(`AGENTS.md`, "Third-Party Material and Provenance", for the rule):

- static: read out of a binary **without executing any of it**; that is the
  whole claim. The tool varies with the target and each row names it:
  `link -dump -disasm` (the Windows 2000 DDK's COFF dumper) and
  `dumpbin /exports` for the 32-bit work; `cdb.exe -z` and `kd.exe -z`, which
  load a raw PE and execute nothing, for amd64, where neither dumper can
  disassemble; capstone over an extracted image with its public PDB for the
  NT 6.x readings.
- runtime: observed while the machine ran, through counters and traces
  compiled into this project's own driver, or through property dumps this
  driver made of data usbport handed it.
- debugger (since 2026-09-17): observed on a running or crashed system
  through a debugger rather than this driver - `kd` on a guest's
  `MEMORY.DMP`, or a QEMU gdbstub watchpoint. Live, but not this driver's own
  evidence, so not `runtime`.
- both: established statically and corroborated by running the system.

History: the 2026-09-16 audit (its E8) found the static entry listing the
two 32-bit tools as though they were the definition, while four rows were
taken with the others, and a horizontal rule above the table's second run
that read as closing this section; until 2026-09-17 two rows carried
`runtime` for readings taken by debugger.

A running system is not corroboration of a reading. The 64-bit build of this
driver (2026-09-09, task 21.2; installed and ran on an XP x64 guest the same
day, task 21.5) and the Vista and Windows 7 tier (since 2026-09-16, both
architectures; the four NT 6.x guests of task 22.5 ran the Version 300 path
through every clause, issue 7 section 7.5, through this driver's log channel
and, on the two x86 guests, its extension read out of guest memory) change
no tag below. The amd64 reading a runtime observation is entangled with,
**M8**, says so in its row (defect seen at runtime, layout read off the
binary, no fact rests on the run); the one row recording the NT 6.x run is
the runtime row beside the Version 300 tier rows, and it says what it
corroborates rather than re-tagging it. The tiers are stated in `AGENTS.md`,
"Project Purpose".

**Which binaries.** The miniport ABI work reads NUSB 3.3's `USBPORT.SYS`
5.00.2195.5652 (Windows 98 SE), Windows 2000 SP4's `USBPORT.SYS`
5.00.2195.6681, in a few places Windows XP SP3's `usbport.sys`, and since
2026-09-02 the SweetLow 5.1.2600.2180 rebuild for Windows 98 (below). Read
statically for narrower questions and not individually inventoried: both
`usbehci.sys` builds (the periodic `Period` derivation, abi section 5),
`usbhub20.sys` and the two `usbd.sys` builds (import and load-gate work), and
a function driver, `ax88772.sys` (a Phase 8 behaviour question).

**Where those copies came from.** Every copy read came from a public
download, an install on this project's
own machine, or the maintainer's own install media: NUSB's binaries from the
publicly distributed `nusb33e.exe`; Windows 98 SE's from the installed
guest; Windows 2000 SP4's and XP SP3's from installation media
(`I386\USBPORT.SY_`, `USBEHCI.SY_` and `USBD.SY_` off the SP4-integrated
`win2ksp4.ISO` and a retail XP Pro SP3 OEM ISO), by `7z e` and `expand.exe`
(`docs/usb-xhci-info/usbport-miniport-interface.md` section 5,
`scripts/package/extract-usbd-sources.ps1`); the SP4 media copy matches the
installed guest's byte for byte. Unpacking with 7-Zip and `expand.exe`
below is section 1's recorded case. The listings produced live under
git-ignored `tools/*-extracted/`, convenience copies, not the record. No
file from the media in the next table is tracked, and nothing in them was
executed.

The lineages and files added since, each method **static**:

| Date, task | Files | Source and route | Read with | What was established |
|---|---|---|---|---|
| 2026-09-08, Phase 21 task 21.1 (design record 11 section 5) | NT 5.2 amd64 `usbport.sys` and `usbehci.sys` of XP Professional x64 / Server 2003 x64, both 5.2.3790.3959 (`srv03_sp2_rtm.070216-1710`), with that media's `usbhub.sys` and `usbport.inf`; the first 64-bit lineage, the fifth | `AMD64` directory of the owner's own XP x64 SP2 media, 7-Zip and `expand.exe`, into git-ignored `tools/winxp64-extracted/`; `docs/contributing/design/11-x64-targets.md` section 3 records the method and the four files' sizes and SHA-256s | export table with `link /dump /exports`, the two functions with `cdb.exe -z` (neither MSVC 6.0's `dumpbin` nor WDK 7.1's `link /dump /disasm` can disassemble amd64 here) | the three exports and their ordinals; `USBPORT_GetHciMn` returns `0x10000001`; the registration function's version gate and its two copied packet sizes, `0x250` and `0x230` |
| 2026-09-09, task 21.4 (record 11 section 5, **M7**) | the same two files | as above | `kd.exe -z` from WDK 7.1's amd64 debuggers (the same engine as `cdb.exe -z`; a debugger because `msdis160.dll` is not present on this host) | the amd64 layout of `USBPORT_ENDPOINT_PROPERTIES` from `usbehci.sys`'s `OpenEndpoint`, `QueryEndpointRequirements` and the two endpoint-open paths below them: the `sizeof` from the nine 8-byte moves that copy it, nine field offsets each from the instruction that reads it |
| 2026-09-09, task 21.3 | `NTOSKRNL.EX_`, `NTKRNLMP.EX_`, `HAL.DL_`, all 5.2.3790.3959 (`srv03_sp2_rtm.070216-1710`): the export tables the import gate resolves against, the amd64 counterpart of the 2000 SP4 kernels and HALs | `AMD64\` of the same media, 7-Zip and `expand.exe`, into `tools/winxp64-extracted/`, by `scripts\import-gate\extract-target-baselines.ps1 -Amd64Iso`, which authenticates each against `scripts\import-gate\winxp64-baselines.expected` by version, length and SHA-256 *before* staging it | export tables only, `link /dump /exports` | **three files, not ten**: NT 5.2 amd64 ships one HAL where i386 ships eight, checked by listing every name on the media beginning `HAL` rather than by a pattern (the `HALBORG.DLL` lesson of the i386 manifest); the manifest records the identities, so the claim is re-derivable without the files |
| 2026-10-14, task 38.3 (design record 18 section 15.11) | eleven files for the import gate's `hlprx86` profile (`xhcihlpr.sys`): `ntoskrnl.exe`, `ntkrnlpa.exe`, `ntkrnlmp.exe`, `ntkrpamp.exe` and the seven HALs `hal.dll`, `halaacpi.dll`, `halacpi.dll`, `halapic.dll`, `halmacpi.dll`, `halmps.dll`, `halsp.dll`, all 5.1.2600.5512 (`xpsp.080413-2111`) | the owner's own `Win XP Pro SP3 OEM original.iso` (volume `GRTMPOEM_EN`, 621,314,048 bytes, SHA-256 `47E08EC0D987783411EFE47DC3582824E4ABD5B0931059D34C2F644BB4144F98`; the medium named above for XP SP3's `usbport.sys`), read only: `7z e` took `I386\SP3.CAB` into a temporary directory and its members (stored uncompressed, nothing expanded) into git-ignored `tools/winxpsp3-extracted/`, by `extract-target-baselines.ps1 -WinXpIso`, authenticating each against `scripts\import-gate\winxp-x86-baselines.expected` by version, length and SHA-256 before staging; the medium was not modified. The cab is the only place on the medium with the two PAE kernels; the loose compressed `I386\NTOSKRNL.EX_` and `HAL.DL_` were expanded once with `expand.exe` to compare and are byte-identical to the cab's members | export tables only, `link /dump /exports` | the export tables `xhcihlpr.sys` resolves against |
| 2026-09-09, Phase 21 task 21.7, Phase 22 tasks 22.1 and 22.2 (record 11 sections 3 and 6; the ABI document's "The 6.0 and 6.1 lineages") | `usbport.sys` and `usbehci.sys` of Vista SP2 (6.0.6002.18005, `lh_sp2rtm.090410-1830`) and Windows 7 Professional SP1 (6.1.7601.17514, `win7sp1_rtm.101119-1850`), both architectures, and for x86 only those systems' `ntoskrnl.exe` and `hal.dll` (export tables for the import check): eight files | `sources\install.wim` of the owner's own retail media, mounted read-only with `Mount-DiskImage` and opened with 7-Zip, nothing copied out but the eight files, nothing written back; git-ignored `tools/vista-x86-extracted/`, `vista-x64-extracted/`, `win7-x86-extracted/`, `win7-x64-extracted/` | `link /dump` for exports, headers and imports; `cdb.exe -z` for function bodies; no guest booted | the export table gained a fourth name and the ordinals shifted; `USBPORT_GetHciMn` still returns `0x10000001`; the registration gate and the `Version >= 200` packet size unchanged in both architectures; the `USBPORT_RESOURCES` prefix and the 50 miniport callback slots unchanged; Windows 7 has a second, 64-bit DMA adapter behind a `Version >= 310` gate; every import the shipping 32-bit `xhci98.sys` names resolves, in the right module, in both x86 kernels |
| 2026-09-16/17, issue 5's mechanism work | a ninth file in each of those four directories, that lineage's `usbhub.sys` (196,096 B Vista x86, byte-identical to the Vista guest's copy; 273,920 B Vista x64; 258,560 B Windows 7 x86; 343,040 B Windows 7 x64), needed because the question was **who asks** usbport to apply a selective-suspend setting, and the asking is in the hub driver; and a tenth lineage, XP SP3's `usbhub.sys`, 59,520 B, 5.1.2600.5512, in `tools/winxpsp3-extracted/` | image 1 of the same `install.wim`; XP SP3's expanded with `expand.exe` from the loose `I386\USBHUB.SY_` on the owner's own XP SP3 medium, identical to the copy in its `SP3.CAB` | `kd.exe -z` with the public PDBs (section 2, "The Microsoft public symbol files"); no guest booted | the rows below |
| 2026-09-10 (task 21.8, record 11 sections 6.1 and 6.2) | the same files, and `tools/winxp64-extracted/usbport.sys` | as above | `kd.exe -z`; `ParseResources` is not exported, so each image's whole `.text` was swept with `u <start> <end>` (bounds from `link /dump /headers`) and the listing searched | the arity of `USBPORT_RegisterUSBPortDriver` (four arguments on NT 6.x, three on NT 5.x), from the callee's use of the fourth argument register in `vista-x64-extracted` and `win7-x64-extracted`, the `ret` immediate in the two x86 builds and the caller's side in Vista x64's `usbehci.sys`; the `USBPORT_RESOURCES.ResourcesTypes` bit assignment from `USBPORT_ParseResources` in all four NT 6.x builds **and the NT 5.2 one** the finding rests on: NT 6.x carries a second port bit and moves interrupt and memory up one place. **Both stay `static`**: the driver was later rebuilt and its host tests run (this project's own code), and a Vista x64 guest reported `0x0C` through this driver's log channel on 2026-09-10, a `runtime` observation of this driver's input, recorded as the symptom |
| 2026-09-10 and 2026-09-11 (task 21.8, record 11 sections 6.4 and 6.3) | six `usbport.sys` builds (`vista-x64`, `win7-x64`, `vista-x86`, `win7-x86`, and the NT 5.x comparisons `winxp64`, `winxpsp3`); `ntoskrnl.exe` of `winxp64` (1.30), `vista-x86` (6.00) and `win7-x86` (6.00) | as above | `kd.exe -z` and `link /dump`; the DPC dispatch is not exported, so it was reached by following `IoConnectInterrupt` to usbport's ISR and through `KeInsertQueueDpc` to the DPC's `DeferredRoutine`, on x86 partly by scanning `.text` for raw displacement bytes (the `KeInitializeDpc` call goes through a register, invisible to an indirect-call scan) | the miniport interrupt DPC slot with the version-gated copy length and interface allocation size in each build; the WDM version each kernel reports, read out of `IoIsWdmVersionAvailable` (five instructions each), the fact that corrected this driver's version predicate. **Both stay `static`**: the XP x64 guest booted on 2026-09-11 before and after that correction, a `runtime` observation of this driver, recorded as such |

The toolchain that reads them is third-party material on the same terms.
WDK 7.1 (`GRMWDK_EN_7600_1.ISO`, 649,877,504 bytes, SHA-256
`5edc723b50ea28a070cad361dd0927df402b7a861a036bbcf11d27ebba77657d`, the
owner's own media) is unpacked into git-ignored `tools/WinDDK71/` with
`msiexec /a`, an administrative install that lays out files and registers
nothing (verified the same day: no WDK or debugger entry in installed
programs, no `C:\WinDDK`). It is used in place and distributed nowhere, as
`tools/MSVC600` and `tools/ntddk` are. Whether it stays is design record 11's
decision 3.

Read at the file level only (method static; nothing disassembled or
executed, nothing kept in this tree or under `tools/` unless stated):

- `nusb36e.exe` (NUSB 3.6, public download, git-ignored in `tools/` beside
  the 3.3 package): files extracted, hashed and version-stamped and the INFs
  read, establishing that its USB 2.0 stack is byte-identical to 3.3's
  (`docs/usb-xhci-info/usbport-miniport-interface.md` section 5). The one
  runtime observation involving it (a 2026-09-01 VM pass) was through this
  project's driver counters, not the package's binaries.
- The Windows ME OEM CD image on the owner's machine, 2026-09-02, for the ME
  target question: `layout.inf`, `layout1.inf`, `layout2.inf`, `usb.inf` and
  `hiddev.inf` from `win9x\PRECOPY1.CAB` with 7-Zip, read as text
  (`docs/contributing/build-and-test.md`, "Windows ME target VM").
- The four target CDs, 2026-09-07, for the `usbui.dll` question: each one's
  `layout.inf` (`PRECOPY1.CAB` on the 9x CDs, the uncompressed
  `I386\LAYOUT.INF` on the NT CDs) and the `usbui.dll` and `sysclass.dll`
  files, with 7-Zip and `expand.exe`, PE export and import tables read with a
  parser, staged in a scratch directory outside the repository. Established
  (`build-and-test.md`, "The files the OS supplies"): four distinct per-OS
  `usbui.dll` builds, each exporting `USBControllerPropPageProvider` and
  `USBHubPropPageProvider`, and a 9x `sysclass.dll` that is a 16-bit NE
  module carrying the string `usbui.dll`. The Device Manager behaviour those
  files drive was observed the same day in the four target VMs (property
  sheets with the file present and absent): observations of Windows' own user
  interface, with this driver installed but not through its counters or
  traces, so neither `static` nor `runtime`; `build-and-test.md` records them
  as guest readings, naming each guest.

SweetLow's USB 2.0 stack for Windows 98, `usb20_win9x.zip`, was read
statically and at run time on 2026-09-02, from the download link its author
gave the owner (`http://sweetlow.orgfree.com/download/usb20_win9x.zip`; the
zip git-ignored in `tools/`, its extraction in `tools/sweetlow-extracted/`
with a README recording URL, sizes, versions and SHA-256s). The same five
binaries had been fetched earlier that day from Windows 98 QuickInstall's
driver library (`oerg866/win98-driver-lib-base`, `[MBD]_sweetlow_usb2.0`,
commit `5ef7f88e`, a public GitHub repository), hash-identical. Its
`USBPORT.SYS` is a 5.1.2600.2180 build carrying the resource string "built
by: WinDDK", a rebuild from XP SP2-level sources rather than a
Microsoft-shipped binary; how those sources were obtained is not recorded
here, and this project has not asked. Method: `dumpbin /exports`, `/imports`
and `/disasm` over the port driver, the miniport and the hub driver as
fetched, the two exported routines' bodies extracted to
`usbport-registration-disasm.txt`; a UTF-16 string scan for registry value
names; one VM session in which this driver's trace recorded the registration
values and the controller lifecycle. Nothing was patched
(`docs/usb-xhci-info/usbport-miniport-interface.md` section 5). The package
is not tracked and not in the release download; section 5's three-file
exception is unchanged, and the stack is referred to by its upstream
repository only.

**Scope of the table.** It indexes the `usbport.sys` miniport ABI facts: the
contracts `src/` is written against, and those unre-derivable without a
binary. It is an index into `docs/usb-xhci-info/usbport-miniport-abi.md`
(the complete, citation-level record) by method, not a replacement for it
nor an exhaustive list of every observation this project has made about a
third-party binary; the roadmap's per-task boxes hold the rest, each with its
own method stated. Where a fact is inferred rather than read, the documents
say so, and several record refutations of earlier readings; keep that habit.
"abi section N" means section N of `docs/usb-xhci-info/usbport-miniport-abi.md`.
Each row gives the fact in one sentence, its method tag, the binary and
where the fact is used; "detail: <record> appendix X, X.n" points at the
appendix item of that record that keeps the full reading (addresses,
offsets, commands, the evidence) in the words the row had here. "The ABI
builds" are NUSB 3.3's and Windows 2000 SP4's `USBPORT.SYS` and, in a few
places (a row naming XP, or "all three"), XP SP3's, as "Which binaries"
names them.
The rows from the issue 7 and issue 8 readings on (then the three task 22.12
(b) readings of 2026-09-18) were added after this paragraph was first
written; they are part of this section.

| Fact | Method | Binary | Where used; detail |
|---|---|---|---|
| SweetLow's `USBPORT.SYS` has the same unguarded single-TT lookup as the 5.00.2195 builds; the branch was never observed executing | static | SweetLow's `USBPORT.SYS` 5.1.2600.2180, 134,912 bytes, SHA-256 `8A3C9F1B568CB25CF5DD9AF3AF9E5C3400DE24BD087CAA3E4E3345588F5CFB56` | abi section 8; issue 06 section 6; detail: abi appendix A, A.1 |
| Under that build a truthfully reported Full-Speed device on a root port takes Windows 98 SE down with a fatal exception 0E in `NTKERN`, after usbhub's port reset and before usbport opens EP0 | runtime (QEMU only) | the same file | abi section 8; issue 06 sections 1 and 8; `runs/run-24.md`, 24.3; detail: abi appendix A, A.2 |
| `USBPORT_GetHciMn` present at ordinal 2, `USBPORT_RegisterUSBPortDriver` at 3, plus an undocumented `DllUnload` at 1; `usbehci.sys` imports only the first two | static (`dumpbin /exports`) | the ABI builds ("Which binaries") | abi section 1 |
| `USBPORT_REGISTRATION_PACKET` layout identical across all three builds (Phase 3 task 1) | static | the three ABI builds | abi section 3 |
| The SweetLow WinDDK rebuild has the same exports, ordinals and registration gate as the Windows 2000 SP4 / XP builds, their form of the wrapper (`Version` at +0x10, packet at +0x14), and returns `0x10000001` from `USBPORT_GetHciMn` | both | SweetLow's `USBPORT.SYS` 5.1.2600.2180 (Windows 98) | interface doc section 5, "The SweetLow rebuild"; detail: interface doc appendix A, A.1 |
| Under that build Windows 98 completes the controller stop on disable, Remove and reinstall, where both 5.00.2195 builds on Windows 98 bugcheck after `RH_DisableIrq` | runtime (QEMU only) | SweetLow's 5.1.2600.2180; NUSB 3.3's and 2000 SP4's 5.00.2195 builds | `docs/contributing/lessons.md`, "The Windows 98 teardown bugcheck belongs to the Windows 2000-lineage usbport"; detail: interface doc appendix A, A.2 |
| The `USBPORT_MINIPORT_INTERFACE` wrapper differs per build: packet at +0x14 (Win2000/XP) vs +0x10 (NUSB), because NUSB's wrapper has no `Version` field, so an interface offset is not a packet offset | static | the ABI builds | abi section 3, and the `FlushInterrupts` box in section 4 |
| Registration version gating: `TakePortControl` additionally gated on interface `Version >= 200` in the Win2000/XP builds | static | the ABI builds | abi section 4 |
| `UsbPortBugCheck` is `KeBugCheckEx(0xD2, 0, 0, 0, 0)`; all four parameters hard zero; the extension argument is pushed and never read | static | the ABI builds | abi section 5 |
| `UsbPortRequestAsyncCallback` callbacks run at DISPATCH_LEVEL holding neither miniport lock, and cannot be cancelled | static (NUSB `0002785E`) | the ABI builds | abi section 5 and the locking table |
| `ResetController` runs at DISPATCH_LEVEL inside a usbport spin lock (so `UsbPortWait` is illegal there); `UsbPortInvalidateController(RESET)` queues a DPC rather than acting inline | static | the ABI builds | abi section 4 |
| Common buffer: 32-bit DMA adapter, page-aligned `StartVA`/`StartPA`, `ROUND_TO_PAGES(size + 0x30)` with usbport's 48-byte header at the end, block zeroed before `StartController`, and `CacheEnabled = TRUE` | static | the ABI builds | abi section 4; consequences in design doc 04 |
| Scatter/gather elements are page-granular: `ElementLength = 0x1000 - (PA.LowPart & 0xFFF)` clamped to the remaining length | static | the ABI builds | abi section 5 |
| SG element ordering versus `SgOffset` | not claimed; a static pass cannot establish it | the ABI builds | abi open item 8 |
| `TransferParameters` is an interior pointer of the transfer record; `SubmitTransfer` preconditions (list pointer never NULL, `SgElementCount == 0` legal) | static | the ABI builds | abi section 4 |
| Post-`SubmitTransfer` lifetime: usbport's post-callback writes happen after it releases the miniport lock, and the completion path takes no lock ordering it behind them | static | the ABI builds | abi section 4 |
| `AbortTransfer` post-return lifetime: usbport retains nothing, the record is `ExFreePool`d in the same worker pass, and the miniport extension is interior to the freed block | static | the ABI builds | abi section 4 |
| `ENDPOINT_FLAG_NUKE` is a controller-teardown flag; on that path usbport completes and frees transfers with no miniport callback | static, with the whole-image negative enumerated so it is checkable without the files | the ABI builds | abi section 4 |
| The `USBPORT_GetTt` defect: the single-TT branch has no empty-list guard and returns `0xFFFFFFEC`, which `OpenPipe`'s null check passes, bugchecking in `ExfInterlockedInsertTailList` | both | the ABI builds | abi section 8; `docs/contributing/implementation-invariants.md`, "Root Hub Reporting"; detail: abi appendix A, A.3 |
| Hub-descriptor request shape, and `PowerOnToPowerGood` copied straight through and truncated to a UCHAR (so 20 ms encodes as 10) | static | the ABI builds | abi open item 7, root-hub block in section 4 |
| Root-hub `RH_DisableIrq`/`RH_EnableIrq` lifecycle: a close is not guaranteed a matching open; per-build addresses recorded | static | the ABI builds | abi section 4 |
| `RH_SetFeatureUSB2PortPower`'s helper drops its lock before the callback (correcting an earlier wrong claim); caller-held locking in general remains unverified | static | the ABI builds | abi section 4 |
| The isochronous block: `SubmitIsoTransfer` at packet slot 0x54, the parameter builder and the completion consumer identical across both builds bar three known private offsets, no version gate | static | the ABI builds | abi section 4 |
| `FlushInterrupts` call site: the device-power completion routine on the `PowerDeviceD0` path, holding neither miniport lock | static, and explicitly not by waiting for a trace (an earlier "remains unreached" was a statement about Phase 3's traces misread as a statement about the binaries) | the ABI builds | abi section 4 |
| `InterruptNextSOF`: two call sites per build, both the endpoint state-change machine, DISPATCH under `MiniportSpinLock`, and nothing waits on it | static | the ABI builds | abi section 4 |
| Callback reachability: which slots are actually called on a live target, e.g. `PollController` called repeatedly from bind onward | runtime (Phase 3 traces) | the ABI builds | abi open item, section 7 |
| `FlushInterrupts` and `InterruptNextSOF` occurrence corroborated by `XHCI_EXTENSION.InterruptFlushes` and `InterruptNextSofRequests` counters in a release build | runtime, corroborating a static reading | the ABI builds | abi open item, section 7 |
| `HubAddr`/`PortNumber` property values: `PortNumber` names the root-hub port for a device on a root port, and is not a TT-only field | runtime (batch 6-V VM runs, on both builds): a property dump of what usbport handed this driver | the ABI builds | abi section 5 |
| `SetEndpointState` is edge-triggered on usbport's own recorded state and skips the miniport call when they match | static | the ABI builds | abi section 4 |
| `UsbPortGetMiniportRegistryKeyValue` (packet slot 0xF0) is populated in both builds and reads one named value from the hardware or software key, PASSIVE_LEVEL only | static | the ABI builds | abi section 6, task 11-V.7 box; detail: abi appendix A, A.4 |
| The shipping `usbehci.sys` calls that slot exactly once per image, with `BOOL = TRUE` and a 4-byte read of `L"EnIdleEndpointSupport"`, from inside `StartController` | static (both builds) | the two `usbehci.sys` builds | abi section 6, task 11-V.7 box |
| Controller-lifecycle census: usbport calls `StartController` and `StopController` from three sites and `ResetController` from one per build, every direct caller chain ending at a PnP or power handler (in the Win2000/XP build also an HCD IOCTL that requests a power transition); no transitive negative is claimed | static | the ABI builds | abi section 4, the two notes after the `UsbPortInvalidateController(RESET)` box; detail: abi appendix A, A.5 |
| `PassThru` (packet slot 0xE0) is driven by the user-mode `IOCTL_USB_USER_REQUEST` escape through the `\DosDevices\HCD<n>` link, PASSIVE_LEVEL with no usbport lock held | both (runtime on both targets: NUSB 5652 in the 2a guest, SP4's 6681 in the 2b guest) | the ABI builds | abi section 4, "Debug / single-packet" box; detail: abi appendix A, A.6 |
| SP4's native `usbhub.sys` carries no selective-suspend or idle-notification string; SP4's `usbport.sys` carries `DisableSelectiveSuspend`, `HcDisableSelectiveSuspend` and `SelectiveSuspend` | static | 2000 SP4 `usbhub.sys` 5.00.2195.6689, 40,176 B; `usbport.sys` 5.00.2195.6681 | build-and-test.md, "A replug onto an idle-suspended controller"; F18's Windows 2000 reading; detail: `build-and-test.md` appendix A, A.1 |
| In the three symbol-less 32-bit `usbport.sys` builds the idle suspend is gated by `HcDisableSelectiveSuspend` read from the driver (software) key | both (static; the Hc-alone conclusion corroborated at runtime 2026-09-16) | NUSB 3.3/3.6 5.00.2195.5652, SweetLow's 5.1.2600.2180, 2000 SP4 5.00.2195.6681 | `docs/contributing/lessons.md`, "And then the whole framing turned out to be wrong", the correction; issue 5 section 4; detail: `lessons.md` appendix A, A.1 |
| NUSB 3.6's `usbhub20.sys` carries `DisableSelectiveSuspend` and `SelectiveSuspend`, its `usbhub.sys` neither, its `usbport.sys` the same three names as SP4's | static | NUSB 3.6 `usbhub20.sys` 5.00.2195.6891, 50,032 B; `usbhub.sys` 4.90.3002.1; `usbport.sys` 5.00.2195.5652 | an unconfirmed candidate explanation for the differing bounded idle observations on the two targets (`build-and-test.md`); detail: `lessons.md` appendix A, A.2 |
| The idle suspend gate in the six symbol-bearing builds follows the NT 5.x shape on XP and the NT 6.x variants on Vista and 7, completing all nine builds | static | XP SP3 x86, XP/2003 x64 5.2.3790.3959, Vista SP2 and 7 SP1 x86 and x64 `usbport.sys` (public PDBs, section 2) | issue 5 section 5.4; `docs/contributing/lessons.md`, the same correction; detail: issue 05 appendix A, A.1 |
| `USBPORTBUSIF_ControllerSelectiveSuspend` writes `HcDisableSelectiveSuspend` back, so who calls it decides whether a per-controller value survives | static | the nine `usbport.sys` builds | issue 5 section 5.4; detail: issue 05 appendix A, A.2 |
| Miniport flag `USB_MINIPORT_FLAGS_DISABLE_SS` (0x20) is tested once per build, in the start routine, and only blocks idle suspend | static | the nine `usbport.sys` builds | issue 5 sections 5.4 and 5.5; `src/xhci_dispatch.c` above `XHCI_MINIPORT_FLAGS`; abi, the `DISABLE_SS` row; detail: issue 05 appendix A, A.3 |
| A driver declaring `USB_MINIPORT_FLAGS_DISABLE_SS` keeps the controller out of idle suspend on all ten targets, measured against a control that idles | runtime (QEMU guests only) | this driver `1.1.0.0` under each target's own `usbport.sys` | issue 5 section 5.5; detail: issue 05 appendix A, A.4 |
| No `usbhub.sys` on any NT 6.x build calls `UsbdQueryControllerType`, so the raw `MiniPortFlags` word never leaves usbport on a machine with only the OS's own drivers | static | the four NT 6.x `usbhub.sys` and `usbport.sys` (Vista SP2, 7 SP1, x86 and x64) | issue 5 sections 5.4 and 6; abi, "What `DISABLE_SS` does"; detail: issue 05 appendix A, A.5 |
| On 32-bit Vista the power plan rewrites `HcDisableSelectiveSuspend` to 0 and the controller suspends at once | runtime (QEMU only) | Vista SP2 x86 `usbport.sys` 6.0.6002.18005, under this driver | issue 5 section 5.4; detail: issue 05 appendix A, A.6 |
| Vista SP2 x86 and Windows 7 SP1 x86 `usbport.sys` carry the selective-suspend value names, counted as UTF-16 substring matches | static (the value's effect a separate runtime reading) | Vista SP2 x86 `usbport.sys` 6.0.6002.18005, 226,304 B; 7 SP1 x86 6.1.7601.17514, 284,672 B | `build-and-test.md`, "Windows Vista and Windows 7 target VMs"; issue 5 section 5; detail: `build-and-test.md` appendix A, A.2 |
| Windows 98 SE's and NUSB 3.6's ME `usbd.sys` both export `_USBD_ParseDescriptors@16` (corrected 2026-10-05) | both | 98 SE `usbd.sys` 4.10.2222, 18,912 B, SHA-256 `0118DB14...00AF6F56A`; NUSB 3.6 ME `usbd.sys` 4.90.3000.1, 22,928 B | issue 06 section 5; detail: issue 06 appendix A, A.1 |
| SweetLow's hidusbf lower filter rewrites the interrupt endpoints' `bInterval` in the select-configuration descriptor from a registry DWORD and imports `_USBD_ParseDescriptors@16` | both (static, corroborated at runtime) | `hidusbf.sys` 1.2.0.10, 3,648 B, not tracked | issue 06 section 5; detail: issue 06 appendix A, A.2 |
| XP x64 SP2's `LAYOUT.INF` names `usbport.sys`, `usbhub.sys`, `usbd.sys` and `usbui.dll` and no `usbhub20.sys`, resolving to `AMD64\SP2.CAB` and `AMD64\DRIVER.CAB` | static | XP Professional x64 SP2 `AMD64\LAYOUT.INF`, the owner's own ISO | `src/xhci98-amd64.inf`'s header block; `build-and-test.md`, "The files the OS supplies"; roadmap task 21.3; detail: `build-and-test.md` appendix A, A.3 |
| `usbui.dll` is absent from a stock Windows XP x64 install, as from 32-bit XP: only a USB controller's own install places it | static | the task 21.5 XP x64 guest's clean-install snapshot | `src/xhci98-amd64.inf`'s header block; roadmap task 21.3; detail: `roadmap-miniport-updates.md` appendix A, A.1 |
| `usbui.dll` is present on a stock Vista and Windows 7 install in both architectures, so the `LayoutFile` row is never consulted there | static | Vista SP2 and 7 SP1 `usbui.dll` (6.0.6001.18000, 6.0.6000.16386, 6.1.7600.16385), image 1 of the owner's own media | Phase 22's open question about the `usbui.dll` row on 6.0/6.1 (`roadmap-miniport-updates.md`); detail: `roadmap-miniport-updates.md` appendix A, A.2 |
| **M4**: the amd64 `USBPORT_RESOURCES` is `0x48` bytes and `StartPA` stays a 4-byte `ULONG` at `0x40`, so the common buffer is below 4 GB | static | NT 5.2 amd64 `usbport.sys` 5.2.3790.3959 | design record 11 section 5 M4; `src/xhci_usbport.h`; detail: record 11 appendix A, A.1 |
| **M5**: on NT 5.2 amd64 usbport creates its DMA adapter 32-bit, so no address it hands the miniport is above 4 GB | static | NT 5.2 amd64 `usbport.sys` 5.2.3790.3959 | design record 11 section 5 M5; detail: record 11 appendix A, A.2 |
| **M6**: all 50 packet callback slots the amd64 `usbehci.sys` fills land on the `0x28 + (X - 0x28) * 2` widening map | static | NT 5.2 amd64 `usbehci.sys` 5.2.3790.3959 | design record 11 section 5 M6; `src/xhci_usbport.h`; detail: record 11 appendix A, A.3 |
| **M8**: the amd64 `USBPORT_SCATTER_GATHER_LIST` and element layout, read off the producer | static (the defect it explains seen at runtime; no fact rests on that) | NT 5.2 amd64 `usbport.sys` 5.2.3790.3959 | abi, "The amd64 scatter-gather layout (M8)"; design record 11 section 5 M8; `src/xhci_usbport.h`; detail: abi appendix A, A.7 |
| The Version 300 tier of `USBPORT_REGISTRATION_PACKET` on NT 6.x x86 is twelve `ULONG`s at `0x13C`-`0x168` then 29 pointers at `0x16C`-`0x1DC`, slot by slot | static | Vista SP2 and 7 SP1 x86 `usbport.sys` (public PDBs) | abi section 1 "The Version 300 tier, slot by slot"; design 11 section 6.5; detail: abi appendix A, A.8 |
| The same tier on NT 6.x amd64 follows `0x250 + (X - 0x13C)` and `0x280 + (X - 0x16C) * 2`, ending at `0x368`, confirmed at every site | static | Vista SP2 and 7 SP1 x64 `usbport.sys` | abi section 1 same section; detail: abi appendix A, A.9 |
| **The `InterruptDpcEx` contract**: `USBPORT_IsrDpc` calls the slot under the DPC lock with an `enableInterrupts` BOOLEAN and reads only bits 0 and 1 of the result | static | Vista SP2 and 7 SP1 `usbport.sys` | abi section 1 same section; design 11 section 6.5; detail: abi appendix A, A.10 |
| `MiniPortFlags` as Microsoft's `usbehci.sys` sends it per OS and Version, and the bits NT 6.x usbport tests | static | XP SP3, XP x64, Vista and 7 `usbehci.sys` and NT 6.x `usbport.sys` | abi section 1 same section; detail: abi appendix A, A.11 |
| `MPf_CloseEndpoint` is the only call through the `CloseEndpoint` slot on Vista and 7 x86 and tests `interface+0x10 >= 12Ch` first, as do `MPf_RebalanceEndpoint` and `MPf_TakePortControl` | static | Vista SP2 and 7 SP1 `usbport.sys` | abi section 1 same section; design 11 section 6.5; detail: abi appendix A, A.12 |
| The NT 5.x control for the 300 tier: no NT 5.x `usbport.sys` compares against 300 or 310 anywhere in its `.text` | static | XP SP3 and XP x64 `usbport.sys`, 2000 SP4 `USBPORT.SYS`, and the other NT 5.x-family builds | abi section 1 same section; design 11 section 6.5; detail: abi appendix A, A.13 |
| On all four NT 6.x guests usbport accepts a `Version = 300` registration and drives it | runtime (QEMU only) | Vista SP2 and 7 SP1 `usbport.sys`, x86 and x64, under this driver | roadmap task 22.5; issue 7 section 7.5; detail: abi appendix A, A.14 |
| Every 32-bit NT 5.x-era `usbport` moves a completed transfer off the endpoint's list with no lock, and reads that list in an endpoint worker holding a per-endpoint lock | static | the 32-bit NT 5.x-era `usbport.sys` builds in `tools/` | issue 8 section 4d; detail: issue 08 appendix A, A.1 |
| XP SP3 x86 `usbport.sys` calls the `CloseEndpoint` slot with three arguments, from exactly one site | static (the bugcheck that led to it observed at runtime) | XP SP3 x86 `usbport.sys` 5.1.2600.5512, link stamp `480254CE` | abi section 1 `CloseEndpoint` row; `src/xhci_usbport.h` above `PHCI_CLOSE_ENDPOINT`; issue 7 section 7.9; detail: abi appendix A, A.15 |
| XP x64 `usbport.sys` keeps list heads at device-extension `+0x988` and `+0x9A8`, `+0x988` under the lock at `+0x460` | static; debugger for the dump readings | XP x64 `usbport.sys`, timestamp `45D69800` | issue 8 section 2-section 5; detail: issue 08 appendix A, A.2 |
| In XP x64 `usbport.sys` a transfer removal stores a status `dword` that lands on `devext+0x988`, and the race behind it (correcting the previous row's "no `dword` store") | static; debugger for the store | XP x64 `usbport.sys`, timestamp `45D69800` | issue 8 sections 4b-4c; `src/xhci.h` `DeliverPerEndpointOnly`; detail: issue 08 appendix A, A.3 |
| NT 6.x `USBPORTSVC_CompleteTransfer` acquires nothing and assumes its caller holds usbport's EpList lock | static; runtime for the root-hub queries' IRQL | Windows 7 SP1 x86 and x64 `usbport.sys` (PDBs `5224A6DF...` age 12, `71BCD350...` age 1) | issue 07 section 7; design 05 section 7; abi "Completion path"; detail: issue 07 appendix A, A.1 |
| Windows 7 x64 `usbehci.sys` calls the CompleteTransfer slot only from its done-TD and poll paths, never from `EHCI_InterruptDpcEx` | static | Windows 7 SP1 x64 `usbehci.sys` (PDB `7D5BF41B...` age 1) | issue 07 section 7.1; abi "Completion path"; detail: issue 07 appendix A, A.2 |
| Vista x64 `usbport.sys` assembles its log-ring tags byte by byte, Windows 7 x64 holds them as imm32 | static | Vista SP2 x64 `usbport.sys` 6.0.6002.18005 (PDB `ABEBA17D...` age 1); 7 SP1 x64 (PDB `71BCD350...` age 1) | issue 07 section 7; the tag tables in `scripts/local/`; detail: issue 07 appendix A, A.3 |
| XP SP3 `usbhub.sys` suspends a child's port only from a D-state change and hands a child its idle callback only from its own | static | XP SP3 `usbhub.sys` 5.1.2600.5512, 59,520 B | `runs/run-22.md` 22.12 (b); detail: `runs/run-22.md` appendix A, A.1 |
| XP SP3 `hidclass.sys` idles a device only when two extension fields are nonzero and a registry value is set | static | XP SP3 `hidclass.sys` 5.1.2600.5512, 36,864 B, SHA-256 `84a55432...48bff2` | `runs/run-22.md` 22.12 (b); detail: `runs/run-22.md` appendix A, A.2 |
| Vista SP2 x86 `usbhub.sys` runs a child's idle callback regardless of the root hub's selective-suspend state; Windows 7 SP1 x86's does not | both (static; the Vista half also runtime) | Vista SP2 x86 `usbhub.sys`, 196,096 B; 7 SP1 x86, 258,560 B | `runs/run-22.md` 22.12 (b); `src/xhci_rh.c` `xhciRhResumeOwesU0`; detail: `runs/run-22.md` appendix A, A.3 |
| Windows 7 SP1 x86 `usbport.sys` faults in its USB 2.0 bus-time budgeter when a Full-Speed device behind a Full-Speed hub on a root port is configured | both | Windows 7 SP1 x86 `usbport.sys` 6.1.7601.17514, DateStamp `4CE79C15` | `runs/run-22.md` 22.9; issue 6 section 6.2; detail: `runs/run-22.md` appendix A, A.4 |
| Windows 2000 SP4 and NUSB `usbport.sys` bucket an interrupt endpoint's `Period` in `USBPORT_OpenPipe` and raise a Low-Speed value below 8 to 8 | static | 2000 SP4 `USBPORT.SYS` 5.00.2195.6681; NUSB 3.3 5.00.2195.5652 | abi, "Periodic scheduling: what `Period` actually carries"; `src/xhci_ctx.c` `XhciIntervalFromPeriod`; `src/xhci.h`; detail: abi appendix A, A.16 |
| SweetLow's Windows 9x `USBPORT.SYS` buckets an interrupt endpoint's `Period` with no Low-Speed floor | static | SweetLow's `USBPORT.SYS` 5.1.2600.2180 | abi, "Periodic scheduling: what `Period` actually carries", the Low-Speed floor table; `src/xhci_ctx.c` `XhciIntervalFromPeriod`; `runs/run-24.md` 24.1; detail: abi appendix A, A.17 |
| Windows XP SP3 x86 `usbport.sys` carries the Low-Speed floor SP4 and NUSB carry | static | XP SP3 x86 `usbport.sys` 5.1.2600.5512 | abi, "Periodic scheduling: what `Period` actually carries"; detail: abi appendix A, A.18 |
| Every `usbport.sys` this driver runs under copies an interrupt pipe's bucketed `Period` into `USBPORT_ENDPOINT_PROPERTIES` byte `0x07` before its USB 2.0 budget runs, and nothing else touches that byte | static | all ten `usbport.sys` builds (NUSB 3.3 = 3.6, SweetLow, 2000 SP4, XP SP3, XP x64, Vista and 7 x86 and x64) | `src/xhci_usbport.h` `USBPORT_ENDPOINT_PROPERTIES.PipePeriod`; `src/xhci_slot.c` `xhciEpProgramPeriod`; design 12 section 7; run-24 "24.5"; detail: record 12 appendix A, A.1 |
| The USB 2.0 budget runs when `MiniPortFlags` has `0x10`, and it overwrites props `+0x06` with the period it budgeted before `OpenEndpoint` is called | static | the same builds | the same; detail: record 12 appendix A, A.2 |
| `Promote_endpoint_periods` re-budgets an interrupt endpoint at period 1 when its start microframe is above 2, with no speed test on NT 5.x, while NT 6.x skips High Speed | static | the same builds | the same; detail: record 12 appendix A, A.3 |
| `InterruptScheduleMask` and `SplitCompletionMask` are written only by the USB 2.0 budget and, for High Speed, say which microframe, not how often | static | the same builds | the same; detail: record 12 appendix A, A.4 |
| Neither `bInterval` nor the endpoint descriptor is passed to the miniport | static | the same builds | the same; detail: record 12 appendix A, A.5 |
| When a later allocation promotes an open endpoint, the NT 5.x-family usbport rewrites its `Period` and masks, and XP SP3's then calls the miniport's `RebalanceEndpoint`; the callback was not read on the other NT 5.x-family builds, and whether NT 6.x changes `Period` there was not traced | static | the NT 5.x-family builds of the rows above (XP SP3's addresses re-checked; the rest from the reports); NT 6.x not traced | the same; detail: record 12 appendix A, A.6 |
| XP SP3 x86 `usbport.sys` schedules an isochronous transfer on a separate branch when the endpoint's device is High Speed | both | XP SP3 x86 `usbport.sys` 5.1.2600.5512 (public PDB) | `runs/run-22.md` 22.9; `docs/using/release-notes.md`, Known limitations; detail: `runs/run-22.md` appendix A, A.5 |
| Every hub INF on this driver's targets binds a generic hub by class, and none names vendor id `0x1209` | static | the `USB2.INF` / `USB.INF` / `usb.inf` of NUSB 3.3, NUSB 3.6, SweetLow's stack, 2000 SP4 and the other targets' media | design record 12 section 3.1; roadmap 24.3.1; detail: record 12 appendix A, A.7 |
| NT 6.x `USBPORTSVC_InvalidateRootHub` takes usbport's EpList lock | static | Vista SP2 x64 `usbport.sys` 6.0.6002.18005; Windows 7 SP1 x64 | design 12 section 11; abi section 6 `UsbPortInvalidateRootHub`; detail: record 12 appendix A, A.8 |
| NT 6.x's legacy `UsbPortRequestAsyncCallback` takes neither the timer-list lock nor EpList; its caller is assumed to hold the timer-list lock | static | Vista SP2 and 7 SP1 `usbport.sys`, x64 and x86 | design 12 section 11; `src/xhci_hw.h` `XhciRootHubDeferredArms`; abi section 6; run-24 "The fix batch"; detail: record 12 appendix A, A.9 |
| NT 6.x `usbport.sys` keeps its spin locks at fixed offsets of the FDO extension, and the miniport extension at a fixed delta from it, per build | static | Vista SP2 and 7 SP1 `usbport.sys`, x86 and x64 (public PDBs) | design 05 "Where a timer may be armed"; `src/xhci_hw.h` `XHCI_ARM_*`; abi section 6; run-24 "24.4"; detail: record 5 appendix A, A.1 |
| NT 6.x `usbport.sys` calls the miniport's `CheckController` holding no usbport spin lock | static | the same four builds | the same; detail: record 5 appendix A, A.2 |
| NT 6.x `usbport.sys` calls the miniport's `InterruptDpcEx` under its ISR-DPC lock only | static | the same four builds | the same; detail: record 5 appendix A, A.3 |
| NT 6.x `usbport.sys` calls a miniport timer callback from `USBPORT_AsyncTimerDpc` holding the timer-list lock, and on Windows 7 also from `USBPORT_CancelAllAsyncTimerCallbacks` holding no lock | static | the same four builds | the same; detail: record 5 appendix A, A.4 |
| NT 6.x `usbport.sys` calls the root-hub feature callbacks with the timer-list lock from some paths and without it from others | static | the same four builds | the same; detail: record 5 appendix A, A.5 |
| NT 6.x `usbport.sys` calls `RH_GetPortStatus` and `RH_GetHubStatus` under its MP lock from `USBPORT_RootHub_Endpoint1_Peek`, and the root-hub status callbacks under none of its MP, ISR-DPC or timer-list locks from every other caller | static | the same four builds | the same; detail: record 5 appendix A, A.6 |
| NT 6.x `usbport.sys` calls the endpoint entry points under its MP-call lock, `StartController`, `StopController` and `PollController` under no spin lock, and `SuspendController` / `ResumeController` under only a wait object | static | the same four builds | the same; detail: record 5 appendix A, A.7 |
| No NT 6.x `usbport.sys` function takes the MP, MP-call, ISR-DPC or EpList lock while holding the timer-list lock, or the timer-list lock while holding the MP or ISR-DPC lock | static | the same four builds | the same; detail: record 5 appendix A, A.8 |
| The NT 6.x services a timer callback of this driver may call take none of usbport's MP, ISR-DPC or timer-list locks | static | the same four builds | the same; detail: record 5 appendix A, A.9 |
| NT 6.x `USBPORTSVC_RequestAsyncCallbackEx` takes seven arguments `(MiniportExt, ms, ctx, ctxLen, callback, PVOID *outHandle /* optional */, BOOLEAN skipLock)` and returns an NTSTATUS | static | the same four builds | the same; detail: record 5 appendix A, A.10 |
| NT 6.x `USBPORTSVC_CancelAsyncCallback (MiniportExt, entryHandle, BOOLEAN lockAlreadyHeld)` cancels one armed timer under the timer-list lock and returns whether it did | static | the same four builds | the same; detail: record 5 appendix A, A.11 |
| NT 6.x `usbport.sys` calls `RH_EnableIrq` / `RH_DisableIrq` only from `MPRH_EnableIrq` / `MPRH_DisableIrq`, under the RH-IntrEp lock and then always the ISR-DPC lock | static | the same four builds | design 05 section 3 and "Where the root hub may be announced"; abi section 1 "The `InterruptDpcEx` contract" and section 4 "The root hub on NT 6.x"; `src/xhci.h` `RootHubReportThroughDpc`; detail: record 5 appendix A, A.12 |
| NT 6.x `USBPORT_IsrDpc` calls `USBPORT_Ev_Rh_IntrEp_Invalidate` after releasing its ISR-DPC lock, if and only if bit 0 or 1 of `InterruptDpcEx`'s return is set, and that path never calls `RH_DisableIrq` | static | the same four builds | the same; detail: record 5 appendix A, A.13 |
| NT 6.x `usbport.sys` takes its MP lock while holding EpList, through `USBPORT_iSetGlobalEndpointStateTx` -> `MPf_PollEndpoint` | static | the same four builds | design 05 section 3 (cycle C3); detail: record 5 appendix A, A.14 |
| The build of 2026-09-28 deadlocked Windows 7 SP1 x86 and Vista SP2 x86 with this driver's event DPC calling `USBPORTSVC_InvalidateRootHub` from inside `InterruptDpcEx` | debugger | Windows 7 SP1 x86 and Vista SP2 x86 `usbport.sys`, four vCPUs, under that build | design 05 section 3 (cycle C1); abi section 4 "The root hub on NT 6.x"; run-24 "Round 5 on the fixed build"; detail: record 5 appendix A, A.15 |
| No NT 5.x or 9x-era usbport was found to take the lock `IsrDpc` holds across `InterruptDpc` under a lock `USBPORT_InvalidateRootHub` takes, and there the service calls `RH_DisableIrq` holding no usbport lock | static | XP SP3 x86, XP x64, 2000 SP4, SweetLow's and NUSB's `usbport.sys` | design 05 section 3 (why the Version 200 tier keeps its in-DPC announcement); detail: record 5 appendix A, A.16 |
| On every NT 5.x and 9x-era usbport, a claimed interrupt queues `IsrDpc` unconditionally, and `IsrDpc` reads its interrupt-enabled flag under the lock its enable/disable wrapper holds, then calls `InterruptDpc` with no other gate | static | NUSB 3.3 = 3.6 5.00.2195.5652 (SHA-256 `EEC79B5A...9B55`), SweetLow's 5.1.2600.2180, 2000 SP4 5.00.2195.6681, XP SP3 5.1.2600.5512, XP x64 SP2 | design 05 section 4; abi sections 1 and 7; `src/xhci_init.c` `XhciRearmInterrupter` and `XhciUnmaskInterrupts`; run-24 "Round 5 on the fixed build"; detail: record 5 appendix A, A.17 |
| The same holds on NT 6.x, except that `IsrDpc` skips the miniport while the FDO extension's "not operational" bit 1 is set, and two power paths call `USBPORT_MpInterrupts(TRUE)` while it is still set | static | Vista SP2 and 7 SP1 `usbport.sys`, x86 and x64 | the same; detail: record 5 appendix A, A.18 |
| Windows 2000 SP4's `KeInsertQueueDpc` returns FALSE only when the DPC is already queued; Windows 98 SE's `NTKERN.VXD` exports no `KeInsertQueueDpc` by name | static | 2000 SP4 `ntkrnlmp.exe`; 98 SE `NTKERN.VXD` | design 05 section 4; `src/xhci_init.c` `XhciRearmInterrupter`; run-24 "Round 5 on the fixed build"; detail: record 5 appendix A, A.19 |
| Windows 7 SP1 `usbhub.sys` bugchecks 0xFE (8, 6, code, PortData) when a port's change-queue gate is not released within 60 s | static | Windows 7 SP1 `usbhub.sys` 6.1.7601.17514, x86 DateStamp `4CE79C42`, x64 `4CE7A68E` | run-24 "Round 6 on the round-5 fixes"; design 12 section 11; detail: `runs/run-24.md` appendix A, A.1 |
| The three 0xFE bugchecks of round 6 were that watchdog, on a root port of the root hub's usbhub instance | debugger | the same `usbhub.sys`, on the round-6 guests' dumps | run-24 "Round 6 on the round-5 fixes"; detail: `runs/run-24.md` appendix A, A.2 |
| Windows 7 SP1 `usbhub.sys` gives a port reset 2 s | static (from the dump analyses; not re-checked) | the same `usbhub.sys` | run-24 "Round 6 on the round-5 fixes"; design 12 section 11; detail: `runs/run-24.md` appendix A, A.3 |
| Windows 7 SP1 x64 `usbhub.sys` takes usbport's controller-wide bus lock for a port enumeration, and usbport records no owner of it | static (not re-checked) | Windows 7 SP1 x64 `usbhub.sys`; `usbport.sys` DateStamp `4CE7A670` | run-24 "Round 6 on the round-5 fixes"; detail: `runs/run-24.md` appendix A, A.4 |
| In the Windows 7 x64 dump the gate's holder had waited 60.03 s on that bus lock, a virtual hub's port-1 enumeration held it, and this driver held no device record | debugger | the round-6 Windows 7 x64 kernel summary dump | run-24 "Round 6 on the round-5 fixes"; design 12 sections 3.3 and 11; `src/xhci_slot.c` `XhciSlotPortHasRecords`; detail: `runs/run-24.md` appendix A, A.5 |
| In the round-7 Windows 7 x64 dump the same chain held, and each looping root port's disown debt had its own device's disowned, idle record under it | debugger | the round-7 Windows 7 x64 dump (this build's PDB timestamp `6ABB4C14`) | run-24 "Round 7 on the disown fix"; design 12 section 11; `src/xhci_slot.c` `XhciSlotPortHasRecords`; detail: `runs/run-24.md` appendix A, A.6 |
| In the round-7 Windows 7 x86 dump the crash was the same 0xFE, and every looping root port's debt had one disowned, idle record under it | debugger | the round-7 Windows 7 x86 dump (PDB timestamp `6ABB4A71`) | run-24 "Round 7 on the disown fix"; design 12 section 11; `src/xhci_slot.c` `XhciSlotPortRecordsDisownedIdle`; detail: `runs/run-24.md` appendix A, A.7 |
| Windows 7 SP1 x86 `usbhub.sys` records the bus lock's owner in the hub, and usbport keeps the address it allocates in the device handle | static (not re-checked) | Windows 7 SP1 x86 `usbhub.sys` `4CE79C42` and `usbport.sys` | run-24 "Round 8 on the idle-disown fix"; detail: `runs/run-24.md` appendix A, A.8 |
| In the round-8 Windows 7 x86 dump the same 0xFE was a circular wait on a USB address this driver still held, with no disown debt anywhere | debugger | the round-8 Windows 7 x86 dump (PDB timestamp `6ABBAAAD`) | run-24 "Round 8 on the idle-disown fix"; design 12 section 11; `src/xhci_slot.c` `xhciDevByAddress`; detail: `runs/run-24.md` appendix A, A.9 |
| Vista's and Windows 7's usbhub remove a disconnected device without disabling its port | both | static: Windows 7 SP1 x86 and Vista SP2 x64 `usbhub.sys`; runtime: Windows 7 x86 and x64 | design 12 section 11; detail: record 12 appendix A, A.10 |
| XP SP3 `usbhub.sys` clears a port feature through one helper and never with `PORT_POWER` | static; runtime for the neighbouring claim | XP SP3 `usbhub.sys` 5.1.2600.5512, 59,520 B | `runs/run-24.md`, "The clauses never read"; detail: `runs/run-24.md` appendix A, A.10 |
| Vista's and Windows 7's `usbhub.sys` hard-reset a hub only through one exception table, and a first-reset change reading a port enabled and not connected is one of its entries | static | Vista SP2 and 7 SP1 `usbhub.sys`, x86 and x64 (Windows 7 SP1 x86 DateStamp `4CE79C42`) | run-24 "Round 9 on the address reclaim"; design 12 section 11; `src/xhci_vhub.c` `XhciVhubPort1Report`; detail: `runs/run-24.md` appendix A, A.11 |
| In the round-8 Windows 7 x86 dump a virtual hub's usbhub had repeated that first-reset path on its port 1 until the crash | debugger | the round-8 Windows 7 x86 dump | run-24 "Round 9 on the address reclaim"; `src/xhci_vhub.c` `XhciVhubPort1Report`; detail: `runs/run-24.md` appendix A, A.12 |
| In the round-10 Windows 7 x86 NMI dump every hub-reset turn began with usbhub's first or second reset of the hub's port 1 timing out with the port enabled | debugger plus static (the raisers and state tables disassembled) | the round-10 Windows 7 x86 dump | run-24 round 10; `src/xhci.h` `SoftInterruptOwed`; detail: `runs/run-24.md` appendix A, A.13 |
| Windows 7 x86 `usbhub.sys` times a port reset with a 2000 ms timer, and ends it on a change that takes three requests to the hub | static | Windows 7 SP1 x86 `usbhub.sys` (PDB) | `src/xhci.h` `SoftInterruptOwed`; detail: `runs/run-24.md` appendix A, A.14 |
| Windows 7 x86 usbport's controller-invalidate service, type 3, simulates an interrupt through its own interrupt DPC | static | Windows 7 SP1 x86 `usbport.sys` (PDB) | `src/xhci.h` `SoftInterruptOwed`; `src/xhci_slot.c` `xhciSlotDeferredWorkEx`; detail: `runs/run-24.md` appendix A, A.15 |
| NUSB 3.6 gives a `usbstor.sys` disk a drive letter on Windows 98 SE through `usbntmap.inf`'s `[USBMPHLP.Dev]` binding, as its INFs state it | static | NUSB 3.6's `usbntmap.inf`, `ntmap.inf`, `_nusb.inf` (from `nusb36e.exe`) | `roadmap-hcd.md`, the decisions table ("Mass storage on Windows 98 SE"); design record 13 section 4; roadmap task 31-0; detail: record 13 appendix B, B.1 |
| Windows 98 SE's `hidusb.sys` sends its PDO `SUBMIT_URB`, `GET_PORT_STATUS` and `RESET_PORT`, and no `IRP_MN_QUERY_INTERFACE` | static | 98 SE `hidusb.sys` 4.10.2222 (identities in record 13 section 6.2) | record 13 section 6; detail: record 13 appendix B, B.2 |
| Windows 98 SE's `usbaudio.sys` sends only `SUBMIT_URB` and no `QUERY_INTERFACE` | static | 98 SE `usbaudio.sys` 4.10.2222 | record 13 section 6; detail: record 13 appendix B, B.3 |
| NUSB 3.3's and 3.6's `usbstor.sys` are the same file; it sends `SUBMIT_URB`, `GET_PORT_STATUS` and `RESET_PORT`, no `QUERY_INTERFACE` | static | NUSB `usbstor.sys` 4.90.3000.1, 21,040 bytes, SHA-256 `B92A6A15...2D8C` | record 13 section 6; detail: record 13 appendix B, B.4 |
| Windows 2000 SP4's `hidusb.sys` and `usbstor.sys` each send `SUBMIT_URB`, `GET_PORT_STATUS` and `RESET_PORT`, and neither queries an interface | static | 2000 SP4 `hidusb.sys` 5.00.2142.1, `usbstor.sys` 5.00.2195.6655 | record 13 section 6; detail: record 13 appendix B, B.5 |
| Windows 2000 SP4's `usbaudio.sys` sends `IOCTL_INTERNAL_USB_GET_BUS_INFO` and fails when the bandwidth does not fit | static | 2000 SP4 `usbaudio.sys` 5.00.2150.1 | record 13 section 6; roadmap 26-A.6; detail: record 13 appendix B, B.6 |
| Windows XP SP3's `hidusb.sys` adds `SUBMIT_IDLE_NOTIFICATION` and URB 0x2A `GET_MS_FEATURE_DESCRIPTOR` to the 2000 set; no `QUERY_INTERFACE` | static | XP SP3 `hidusb.sys` 5.1.2600.5512, 10,368 bytes | record 13 section 6; detail: record 13 appendix B, B.7 |
| Windows XP SP3's `usbstor.sys` queries `USB_BUS_INTERFACE_USBDI_GUID` at version 1, size 0x24, and tolerates a failure | static | XP SP3 `usbstor.sys` 5.1.2600.5512 | record 13 section 6; detail: record 13 appendix B, B.8 |
| Windows XP SP3's `usbaudio.sys` queries `USB_BUS_INTERFACE_USBDI_GUID` at version 0, size 0x20, and calls only `QueryBusTime` | static | XP SP3 `usbaudio.sys` 5.1.2600.5512 | record 13 section 6; detail: record 13 appendix B, B.9 |
| **`USB_BUS_INTERFACE_USBDI_GUID` is `{b1a96a13-3de0-4574-9b01-c08feab318d6}`**, with versions 0 to 3 at x86 sizes 0x20 / 0x24 / 0x28 / 0x30 and x64 sizes 0x40 / 0x48 / 0x50 / 0x60 | static | WDK 7.1 `inc\ddk\usbbusif.h`; the bytes in XP SP3 and XP x64 `usbstor.sys` and XP SP3 `usbaudio.sys` | record 13 section 6; detail: record 13 appendix B, B.10 |
| ASIX `AX88772.SYS` (98 SE / ME) sends only `SUBMIT_URB` and refuses to continue below USBDI version 0x101 | static | ASIX `AX88772.SYS` 3.0.3.12 | record 13 section 6; detail: record 13 appendix B, B.11 |
| ASIX `ax88772.sys` (2000 / XP) is a KMDF 1.7 client whose PDO traffic goes out through `Wdf01000.sys`, which was not read | static | ASIX `ax88772.sys` 3.4.3.38 | record 13 section 6; roadmap 26-A.6, 28-A.1; detail: record 13 appendix B, B.12 |
| `hidclass.sys` of 98 SE, 2000 SP4 and XP SP3 builds `IRP_MN_QUERY_CAPABILITIES` and requests `IRP_MN_WAIT_WAKE` and `IRP_MN_SET_POWER`; none sends an `IOCTL_INTERNAL_USB_*` | static | `hidclass.sys` 4.10.2222, 5.00.2195.6655, 5.1.2600.5512 (record 13 section 6.2) | record 13 section 6; detail: record 13 appendix B, B.13 |
| The identities of the XP x64 SP2, Vista SP2 and Windows 7 SP1 class drivers read for task 25.2 | static | `hidusb.sys`, `usbstor.sys`, `usbaudio.sys` of those systems (versions, lengths and SHA-256s in record 13 section 6.2) | record 13 section 6; detail: record 13 appendix B, B.14 |
| The `usbd.sys` exports the class drivers import are the same on every target read | static | the class drivers in `tools\*-extracted\class\` | record 13 section 6; detail: record 13 appendix B, B.15 |
| `hidusb.sys` on XP x64, Vista and 7 sends `SUBMIT_URB`, `RESET_PORT`, `GET_PORT_STATUS` and `SUBMIT_IDLE_NOTIFICATION` | static | XP x64, Vista SP2 and 7 SP1 `hidusb.sys` (record 13 section 6.2) | record 13 section 6; detail: record 13 appendix B, B.16 |
| `usbstor.sys` on XP x64, Vista and 7 queries `USB_BUS_INTERFACE_USBDI_GUID` at version 1 in `USBSTOR_GetBusInterface` | static | XP x64, Vista SP2 and 7 SP1 `usbstor.sys` | record 13 section 6; detail: record 13 appendix B, B.17 |
| `usbaudio.sys` queries `USB_BUS_INTERFACE_USBDI_GUID` at version 0, size 0x40, on XP x64, and at version 1 on Vista and 7 | static | XP x64, Vista SP2 and 7 SP1 `usbaudio.sys` | record 13 section 6; detail: record 13 appendix B, B.18 |
| Windows 2000 SP4 `usbaudio.sys` builds its configuration request for the audio interfaces only and then walks `bNumInterfaces` entries | both | 2000 SP4 `usbaudio.sys`, DateStamp `3803CAD4` | `docs/contributing/runs/run-26.md`, c14; detail: `runs/run-26.md` appendix A, A.1 |
| None of the fifteen XP x64, Vista and 7 class drivers sends `CYCLE_PORT`, `GET_DEVICE_HANDLE(_EX)`, `GET_HUB_COUNT`, `GET_ROOTHUB_PDO`, `GET_BUS_INFO`, `GET_CONTROLLER_NAME` or `GET_DEVICE_CONFIG_INFO`, queries an interface but USBDI, or asks for a USBDI version above 1 | static | the fifteen class drivers of record 13 section 6.2 | record 13 section 6; roadmap 26-A.6, 28-A.1; detail: record 13 appendix B, B.19 |
| Windows 98 SE's own USB 1.1 host controller drivers import `IoGetDmaAdapter`, `IoConnectInterrupt`, `KeSynchronizeExecution` and the other kernel names listed | static | 98 SE `uhcd.sys` 4.10.2222, 30,448 bytes; `openhci.sys` 4.10.2222, 23,632 bytes | record 13 section 7; `scripts/import-gate/xhci98-imports.allow`; roadmap 25.3, 25.7; detail: record 13 appendix B, B.20 |
| Both reach DMA only through the adapter's `DMA_OPERATIONS` | static | the same two files | record 13 section 7; roadmap 25.7; detail: record 13 appendix B, B.21 |
| Over 86 binaries, `ExAllocatePoolWithTag` is imported by 62 stock 98 SE drivers and `ExFreePool` by 61, while none imports the untagged `ExAllocatePool` | static | 67 98 SE WDM drivers, the two SE files, NUSB 3.3's eight and NUSB 3.6's nine (record 13 Appendix A) | record 13 section 7 and Appendix A; `scripts/import-gate/xhci98-imports.allow`; detail: record 13 appendix B, B.22 |
| Nineteen kernel and HAL names a stock 98 SE driver imports are absent from `ntkern.vxd`'s NUL-delimited strings, `KeInsertQueueDpc` among them | static | 98 SE `ntkern.vxd` and the drivers of the row above | record 13 section 7; the `win98-evidence.list` header; detail: record 13 appendix B, B.23 |
| NUSB 3.6's `wdmstub.sys` carries as strings but does not import `ExFreePoolWithTag` and the other names listed | static | NUSB 3.6 `wdmstub.sys` (Walter Oney Software) 5.00.006, 12,767 bytes | `scripts/import-gate/win98-evidence.list` NUSB 3.6 caveat; record 13 section 7; the deny rows of `xhci98-imports.allow`; detail: record 13 appendix B, B.24 |
| The Windows 2000 DDK links drivers against `ntoskrnl.lib hal.lib wmilib.lib`, its `ntoskrnl.lib` defines `READ_REGISTER_*` / `WRITE_REGISTER_*`, and `memcmp` is in none and not exported by 2000 SP4's kernels | static | the Windows 2000 DDK's `libfre\i386` libraries and headers; 2000 SP4's kernels | record 13 section 7; the allowlist's module column; detail: record 13 appendix B, B.25 |
| The Windows 98 SE files staged in `tools\win98se-extracted\wdm\` (67) and the NUSB 3.6 files (9) are identified by version, length and SHA-256 | static | those 76 files (record 13 Appendix A) | `scripts/import-gate/win98-evidence.list`; record 13 Appendix A; detail: record 13 appendix B, B.26 |
| The `USBUSER` request set `usbui.dll` sends a controller is one request (`USBUSER_GET_CONTROLLER_INFO_0`) on two targets and none on seven | static | the nine `usbui.dll` builds (section 2's symbol table) | record 13 section 8; roadmap 26-A.8; detail: record 13 appendix B, B.27 |
| The legacy controller IOCTLs `usbui.dll` sends on all nine builds are `IOCTL_GET_HCD_DRIVERKEY_NAME` (`0x220424`) and `IOCTL_USB_GET_ROOT_HUB_NAME` (`0x220408`) | static | the nine `usbui.dll` builds | record 13 section 8; roadmap 26-A.8; detail: record 13 appendix B, B.28 |
| The hub IOCTLs `usbui.dll` sends a hub, per build | static | the nine `usbui.dll` builds | record 13 section 8; roadmap 26-A.8; detail: record 13 appendix B, B.29 |
| `usbui.dll`'s configuration-descriptor request carries `bRequest` 0, on all nine builds | static | the nine `usbui.dll` builds | record 13 section 8; `src/hcd_door.c` `hcdDoorDescriptor`; detail: record 13 appendix B, B.30 |
| **How the pages find their device**: 98 SE, ME, 2000 SP4, XP SP3 and XP x64 read `SymbolicName` from the devnode's hardware key; Vista and 7 find it otherwise | static | the nine `usbui.dll` builds | record 13 section 8; roadmap 26-A.8; detail: record 13 appendix B, B.31 |
| What the Power page computes from the hub's and devices' descriptors | static | Windows 7 x86 `usbui.dll` (PDB names) | record 13 section 8; detail: record 13 appendix B, B.32 |
| **Who reads the `_EX` `Speed` byte** (task 36.18): Vista's and 7's `usbui.dll` treat only 2 as High Speed, so 3 and 4 list a device among the Full Speed devices (a misclassification, not a crash); XP's pages never read the byte | static | Windows 7 x86 and Vista x64 `usbui.dll` (PDB names) | record 13 section 8.4; detail: record 13 appendix B, B.33 |
| The speed ceiling of the node-connection interface: `USB_DEVICE_SPEED` stops at `UsbHighSpeed` and the Windows 2000 DDK has no `usb200.h` | static | WDK 7.1 `inc\api` and the Windows 2000 DDK `inc` headers | record 13 section 8; the Phase 25 checkpoint wording; detail: record 13 appendix B, B.34 |
| The root-hub page each OS's INF registers | static | the targets' `usb.inf` / `usbport.inf` and NUSB's and SweetLow's INFs | record 13 section 8; `src/xhci98.inf`, `src/xhci98-amd64.inf`; the INF gate's `HCD-HUBPAGE`; detail: record 13 appendix B, B.35 |
| The non-VID `USB\` ids the OS INFs match | static | the INFs and DLLs of 98 SE, ME, NUSB 3.3 / 3.6, SweetLow, 2000 SP4, XP SP3, XP x64, Vista and 7 | record 13 section 8 (the root-hub id `XHCI98\ROOT_HUB`); detail: record 13 appendix B, B.36 |
| Windows 7 x86 `usbport.sys` writes `SymbolicName` from `USBPORT_EnableDeviceInterface`, and every `usbport.sys` and `usbhub(20).sys` read carries the string | static | Windows 7 x86 `usbport.sys` 6.1.7601.17514 (PDB `usbport.pdb/5224A6DF50AF4F28A0A7A60F3EA734812`) and the others named | record 13 sections 7 and 8; detail: record 13 appendix B, B.37 |
| Windows 98 SE's `usb.inf` binds `USB\COMPOSITE` (and 28 VID/PIDs) to `[Composite.Dev]`, whose `NTMPDriver` is `usbhub.sys` | static | the INFs of the owner's `Win98SE.iso` (SHA-256s in record 13 section 10.6) | record 13 section 10; detail: record 13 appendix B, B.38 |
| Windows 2000 SP4's `usb.inf` binds `USB\COMPOSITE` to service `usbhub`; XP SP3's, XP x64's, Vista SP2's and 7 SP1's to service `usbccgp` | static | the INFs of those media (SHA-256s in record 13 section 10) | record 13 section 10; detail: record 13 appendix B, B.39 |
| **NUSB 3.6's `USB.INF` and SweetLow's `USB2.INF` bind `USB\COMPOSITE` to `NTMPDriver` `usbccgp.sys` on Windows 98** | static (a text read of `tools\nusb36-extracted\USB.INF` and `tools\sweetlow-extracted\USB2.INF`; a subagent's read, 2026-10-02, not re-read line by line by the coordinator) | NUSB 3.6 `USB.INF`; SweetLow's `USB2.INF` | `docs/contributing/design/13-superspeed-hcd.md` section 10 (never `USB\COMPOSITE`) |
| No hub INF on any target names an `XHCI98\` id, and the hub ids they match are all under `USB\` (task 33.4) | static | the targets' `usb.inf` / `usbport.inf` (extracted INFs) | record 13 section 10.11; `src/xhci98.inf`, `src/xhci98-amd64.inf`; the INF gate's `HCD-HUB`; detail: record 13 appendix B, B.40 |
| No INF read names a `USB\XHCI98_` id or contains `XHCI98` at all, and no `USB\` token in them is a wildcard (task 38.8) | static | 214 INFs (the 187 under `tools\*-extracted\` and the others named) and the binaries searched | record 13 section 10.13.2; detail: record 13 appendix B, B.41 |
| The hub drivers' id templates, as UTF-16 strings | static | 98 SE `usbhub.sys` 35,680 bytes, SHA-256 `e898b75f...afc31`; XP SP3, Vista x86 and 7 x86 `usbhub.sys` | record 13 section 10 (case-insensitive matching; the open `DevClass` question); detail: record 13 appendix B, B.42 |
| `usbccgp.sys` 5.1.2600.5585 and 5.1.2600.2180 carry the `USB\Class_nn...` and `&MI_` id templates | static | `usbccgp.sys` 5.1.2600.5585, SHA-256 `4c1b3e8f...c66c5` (NUSB 3.6's and SweetLow's XP SP3 QFE copy); 5.1.2600.2180, `683061af...86457` (SweetLow's) | record 13 section 10; detail: record 13 appendix B, B.43 |
| Windows 7 SP1 x86 `usbccgp.sys` groups interfaces by IAD, else by a positional legacy rule | static | Windows 7 SP1 x86 `usbccgp.sys` 6.1.7601.17514, SHA-256 `288cac9f...e9dc` | record 13 sections 10.8 rule 2, 10.9; detail: record 13 appendix B, B.44 |
| In that build `GroupInterfacesByFunction` calls `FindInterfaceByClass` with class 2, subclass 8, then `GroupSubordinateInterfaces`; the CDC path's condition was not read | static | the same file | record 13 sections 10.7, 10.8 rule 3, 10.9; detail: record 13 appendix B, B.45 |
| `usbccgp.sys` 5.1.2600.5585 has `GetFunctionInterfaceListBase` with the same class `0x0D` skip and class `0x01` comparisons, and `ParseUSBInterfaceAssociationDescriptors`, and no `Group*` routine | static | `usbccgp.sys` 5.1.2600.5585 (the XP SP3 QFE build NUSB 3.6 ships) | record 13 section 10.8 rules 2 and 3; detail: record 13 appendix B, B.46 |
| NUSB's `USBPORT.SYS` and Windows 98 SE's `uhcd.sys` reach PCI configuration space with `IRP_MN_READ_CONFIG` / `IRP_MN_WRITE_CONFIG` sent to the PDO saved at `AddDevice`, not to the attached lower object | static | NUSB `USBPORT.SYS` 5.00.2195.5652; 98 SE `uhcd.sys` 4.10.2222 and `openhci.sys` | `src/hcd_svc.c` `HcdSvcConfigSpace`; detail: record 13 appendix B, B.47 |
| The id vocabulary NUSB's `usbstor.sys` builds its disk PDOs' ids from, as strings in the image | static | NUSB 3.3 / 3.6 `usbstor.sys` | roadmap task 31-A.2, `src/uas/uas_iu.c` `UasBuildId` and `src/uas/uas_pdo.c`; task 31-0; detail: record 13 appendix B, B.48 |
| ASIX's Windows 98 `AX88772.SYS` waits for its URBs by polling `Irp->IoStatus.Status` for `STATUS_PENDING`, not on an event | static | ASIX `AX88772.SYS` 3.0.3.12 | `src/hcd_urb.c` `HcdDevicePdoInternalIoctl`; detail: record 13 appendix B, B.49 |
| NUSB 3.3's `USBPORT.SYS` makes `STATUS_PENDING` visible before it pends a URB IRP, and its select handler refuses a configuration descriptor with `wTotalLength` 0 | static | NUSB 3.3 `USBPORT.SYS` 5.00.2195.5652 | `src/hcd_urb.c` `HcdDevicePdoInternalIoctl`; detail: record 13 appendix B, B.50 |
| Windows 98 SE's configuration manager sends a device PDO BusRelations and `IRP_MN_QUERY_ID` after its `IRP_MN_REMOVE_DEVICE` has completed; Windows 2000 SP4 sends nothing after the REMOVE | runtime (QEMU) | 98 SE under NUSB 3.3 and 2000 SP4, with this driver | `src/hcd_pdo.c` (`RemovedPdos`, `hcdReapRemoved`); `runs/run-27.md`, "The closing legs"; `lessons.md`; detail: `runs/run-27.md` appendix A, A.1 |
| ReactOS's `usbhub` names a device by its serial number only when the serial string passes a character check and is unique among the hub's ports | ReactOS source read, not a binary (section 3) | ReactOS `drivers/usb/usbhub` at `0298e10d5d904a0230868be8f7bdf6436d589c62` | `src/xhci_func.c` (`XhciFuncSerialId`), `src/hcd_pdo.c` (`hcdSerialTakenLocked`, `hcdSerialTakenElsewhere`); record 13; detail: record 13 appendix B, B.51 |
| Text-mode Setup's own USB support, per medium | static | `TXTSETUP.SIF` of `win2ksp4.ISO` (335,081 B, SHA-256 `ECA9CDFB...CB6BF2`) and the other media (record 13 section 5.6) | record 13 section 5.6; `src/txtsetup.oem`, `src/txtsetup-amd64.oem`; roadmap 33.3; detail: record 13 appendix B, B.52 |
| The `txtsetup.oem` format, from the Windows 2000 DDK's sample | static | 2000 DDK `src\setup\inf\scsi\txtsetup.oem`, 1,472 B, SHA-256 `62736CB3...6A165C01` | record 13 section 5.6; `scripts/inf-gate/check-txtsetup-oem.ps1`; detail: record 13 appendix B, B.53 |
| **What text-mode Setup does with each `txtsetup.oem` file type** (design record 18 section 15.9's amendment) | both (static; corroborated by the F6 legs) | 2000 SP4 and XP SP3 `SETUPLDR.BIN` and `SETUPDD.SYS` | record 18 section 15.9; `src/txtsetup.oem`; `scripts/inf-gate/check-txtsetup-oem.ps1` `OEM-COPY`; detail: record 18 appendix A, A.1 |
| Where text mode reads an F6 option's files, and what a `driver` line costs | both (static; corroborated at runtime, QEMU) | 2000 SP4 `setupdd.sys` (`16dfe965...6ada69`) and XP SP3's | `lessons.md`, "A `txtsetup.oem` disk is always read from drive A: ..."; record 13 section 5.6, limit 7; detail: record 18 appendix A, A.2 |
| Windows 7 SP1 x86 `usbhub.sys` keeps a device PDO it is sent `IRP_MN_STOP_DEVICE` for, and restarts the device under the same PDO at the next START | static | Windows 7 SP1 x86 `usbhub.sys` 6.1.7601.17514 (`usbhub.pdb` `C8AAA35CAB46490FBA4B37ED793CC0022`) | issue 09 section 4.3; `src/hcd_pdo.c` (the dormant state, roadmap-hcd task 33.1); detail: issue 09 appendix A, A.1 |
| Windows 98 SE and ME disable a controller in Device Manager with `QUERY_STOP` and `STOP` to each device PDO, the root hub and the controller, and no `REMOVE`, and enable it by starting them again | runtime (QEMU) | 98 SE and ME configuration managers, with this driver | issue 09 sections 3, 4 and 7; `src/hcd_pnp.c` (`StopPreserve`); detail: issue 09 appendix A, A.2 |
| What a hub driver answers in a device PDO's `DEVICE_CAPABILITIES` `Address` and `UINumber`, and XP's `DeviceTextLocationInformation` | static | XP SP3 `usbhub.sys` 5.1.2600.5512, 59,520 B, SHA-256 `a99c4528...b300fe`; Windows 7 x86's (PDBs `A743565664594F7592F40645D0B71F3A1`, `C8AAA35CAB46490FBA4B37ED793CC0022`) | record 13 section 10.11, "Address"; `src/xhci_hub.c` `XhciHubPdoAddress`; `src/hcd_pdo.c`; detail: issue 09 appendix A, A.3 |
| hidusbf's `Setup.exe` takes a USB device's `SPDRP_ADDRESS` as the connection index on its parent hub | static | hidusbf `Setup.exe`, 2026-10-03 build, 576,016 B, SHA-256 `b8be2152...2aff185e` | record 13 section 10.11, "Address"; `src/xhci_hub.c` `XhciHubPdoAddress`; detail: record 13 appendix B, B.54 |
| No class driver of any target sends `IOCTL_INTERNAL_USB_CYCLE_PORT`, and `usbaudio` sends no port request at all (task 35-T.0) | static | the `hidusb.sys`, `usbstor.sys`, `usbaudio.sys` and `hidclass.sys` under `tools\*-extracted\` | record 13 section 6.7; record 17 section 4.10; roadmap 35-T.0 / 35-V; detail: record 13 appendix B, B.55 |
| `hidusb.sys` resets on a failed interrupt read by NTSTATUS alone, once per failed read, with no counter | static | `hidusb.sys` of 98 SE, 2000 SP4, XP SP3, XP x64, Vista and 7 | record 13 section 6.7; record 17 section 4.10; roadmap 35-V; detail: record 13 appendix B, B.56 |
| `hidclass.sys` re-reads a failed interrupt read after a backoff of 1 s growing by 1 s to 5 s, with no limit | static | `hidclass.sys` 98 SE 4.10.2222, 2000 SP4 5.00.2195.6655, XP SP3 (XP x64 and NT 6.x not read) | record 13 section 6.7; record 17 section 4.10; detail: record 13 appendix B, B.57 |
| `usbstor.sys` clears a stall and resets the port for everything else, at most three times per episode (once on NUSB) | static | `usbstor.sys` of NUSB 3.6, 2000 SP4, XP SP3, XP x64, Vista and 7 | record 13 section 6.7; record 17 section 4.10; roadmap 35-V; detail: record 13 appendix B, B.58 |
| `usbaudio.sys` answers a failed isochronous or MIDI completion with `ABORT_PIPE` and `SYNC_RESET_PIPE_AND_CLEAR_STALL` before its next submission, and keeps streaming | static | `usbaudio.sys` of XP SP3, 98 SE and 2000 SP4 (XP x64, Vista and 7 only by the byte census) | record 13 section 6.7; record 17 section 4.10; roadmap 35-V; detail: record 13 appendix B, B.59 |
| On Windows 7 SP1 x86 no class driver's remove path sends a port request | static | Windows 7 SP1 x86 `hidusb.sys` and `USBSTOR.SYS` | record 13 section 6.7; roadmap 35-V; detail: record 13 appendix B, B.60 |
| `HAL.dll!HalGetBusData` and `ntoskrnl.exe`'s `READ_REGISTER_USHORT` and `WRITE_REGISTER_USHORT` are exported on every target, and named in Windows 98 SE's `ntkern.vxd` | static | the 2000 SP4 HALs, Vista and 7 x86 `hal.dll`, XP x64's `hal.dll`, the amd64 `hal.lib`, 98 SE `ntkern.vxd` | `src/hcd_ctl.c` `hcdPirqSlot`, `hcdPirqRead16`, `hcdPirqWrite16`; `xhci98-imports.allow` and `xhci98-imports-amd64.allow`; detail: record 13 appendix B, B.61 |
| On every NT target HAL, `IoMapTransfer` with a NULL `MapRegisterBase` returns the MDL page's physical address and extends the run over contiguous frames within one 4 GB region | both (static; the XP x64 grant of 524,289 registers also runtime) | 2000 SP4's eight, XP SP3's seven, XP x64's, Vista SP2's and 7 SP1's HALs (files, versions and SHA-256 in record 13 section 11.6.1) | record 13 section 11.6.1; detail: record 13 appendix B, B.62 |
| `IoConnectInterruptEx` and `IoDisconnectInterruptEx` are exported by the Vista SP2 x86 and 7 SP1 x86 kernels, with every other `ntoskrnl.exe` pair the x86 MSI helper imports | static | Vista SP2 x86 and 7 SP1 x86 `ntoskrnl.exe` | `scripts/import-gate/xhcimsi-imports.allow` (`xhcihlpr-imports.allow` since task 38.3); record 18 section 8.2; detail: record 18 appendix A, A.3 |
| Every pair `xhcihlpr.sys` imports is exported by every authenticated kernel and HAL image of 2000 SP4 and XP SP3 x86, and by the uniprocessor non-PAE kernel and `hal.dll` of Vista SP2 and 7 SP1 x86 | static | the images of the gate's `hlprx86` profile | `scripts/import-gate/xhcihlpr-imports.allow`; record 18 sections 15.10 and 15.11; detail: record 18 appendix A, A.4 |
| What Windows Vista and 7's PCI driver offers in `IRP_MN_FILTER_RESOURCE_REQUIREMENTS` for an MSI-capable xHCI controller with `MSISupported` 1 | runtime (QEMU) | Vista and 7 `pci.sys`, with this driver's `M86` trace | record 18 sections 4.2 and 11; detail: record 18 appendix A, A.5 |
| How usbhub answers `0x220410` (`IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION`) (task 36.45) | static | XP SP3 `usbhub.sys` 5.1.2600.5512 (59,520 B, SHA-256 `a99c4528...b300fe`), 7 x86 and 98 SE `usbhub.sys`; 2000 SP4 `USBHUB20.SYS` 5.00.2195.6655 (49,776 bytes) | record 13 section 8.11; `src/hcd_door.c` `hcdDoorDescriptor`; detail: record 13 appendix B, B.63 |
| Windows 7 SP1 `usbaudio.sys` gives every isochronous URB of an asynchronous Type I stream an explicit `StartFrame`, and resets the feedback pipe before each feedback read (task 36.57) | static | Windows 7 SP1 x86 `USBAUDIO.sys`, 80,768 B, SHA-256 `72603e0a...b933`, and x64 | record 13 section 6.10; detail: record 13 appendix B, B.64 |
| The six `USB\SilentClasses` entries of SweetLow's `SilentClasses.reg` (task 38.2) | static | SweetLow's `SilentClasses.reg` (not tracked) | record 13 section 6.11.8; detail: record 13 appendix B, B.65 |
| Windows 98 SE's `RtlQueryRegistryValues` with a NULL-`Name` query-table entry calls the routine once for every value of the key | runtime (QEMU) | 98 SE NTKERN under NUSB 3.3, and ME, with this driver | record 13 section 6.11, O1; `src/hcd_ctl.c` (`hcdReadSilent`); detail: record 13 appendix B, B.66 |
| Windows XP SP3's `usbccgp.sys` selects the whole configuration down the composite devnode's own stack before it creates any function PDO | static | XP SP3 `usbccgp.sys` 5.1.2600.5585, 32,384 B, SHA-256 `4C1B3E8F3F658E356A955108FF84FB5C95244CB2A9D323AA0DFAEF92927C66C5` | record 13 section 10.14.1; detail: record 13 appendix B, B.67 |
| On Windows 98 SE and ME, `cfgmgr32.dll`'s `CM_Uninstall_DevNode` is a stub, and `CM_Locate_DevNodeA` honours `CM_LOCATE_DEVNODE_PHANTOM` (task 38.8) | static | 98 SE `CFGMGR32.DLL`, 45,056 B, SHA-256 `269bbe3e...7aea`, and ME's | record 13 section 10.13.8; detail: record 13 appendix B, B.68 |
| **The CONFIGMG row**: the Windows 98 / ME configuration manager's ring-0 interface values in `src/cm/cm_cfgmg.h` (VxD id, dynalink, service numbers, flags), from the Windows 98 SE DDK headers | static | the Windows 98 SE DDK `win98se_ddk.exe` headers (SHA-256s in `docs/references/README.md`) | `src/cm/cm_cfgmg.h`; record 21 section 6.1; detail: record 21 appendix A, A.1 |
| The configuration manager's logical-configuration services in `src/cm/cm_cfgmg.h`, counted as the CONFIGMG row counts them (task 42-A.15) | static | the same DDK headers; Stor98's INF | `src/cm/cm_cfgmg.h`, `cm_cfgmg.c`, `cm_nvlinkos.c`, `cm_cfgwk.c`; design records 28 and 31; detail: record 21 appendix A, A.2 |
| The Windows 98 SE setup engine's device-installer values and ordinals in `xhciinst/di/`, from the DDK's `inc16\setupx.h` and `setupx.lib` (task 42-A.1) | static | the Windows 98 SE DDK (as in the CONFIGMG row; member hashes in `docs/references/README.md`) | `xhciinst/di/di_core.c`, `di_core.h`, `xhcidi-imports.allow`; design record 27; detail: record 21 appendix A, A.3 |
| **PCI.VXD's configuration-space services in `src/cm/cm_cfgmg.h`** (record 21 section 6.1.1): its VxD id `0x0043` and service numbers | static | the Windows 98 SE DDK's `PCI.H` (8,081 bytes, SHA-256 `0acdf32afe0cc5baca1d74d08638f4f02f744d4f7ed54edb10764c136feb34f9`) and `VXDWRAPS.H` (60,450 bytes, `d43708f618ad2cd5ed4b66e1e3ebc31235710df83cc9673c54c22d4b07fbe884`) | `src/cm/cm_cfgmg.h`, `src/cm/cm_cfgmg.c`; record 21 section 6.1.1; detail: record 21 appendix A, A.4 |
| **`usb4-emu/docs/ICM-PROTOCOL.md`**, the Thunderbolt 3 client's clean-room contract (record 21) | static (a document read) | `usb4-emu` `main` `a68bc74` (= `eba0cf1`), git blob `976cbbe2d4088eb86711463b3f0df3fa2d7fc414` | record 21 section 9 and every section citing `ICM-PROTOCOL`; detail: record 21 appendix A, A.5 |
| `ICM-PROTOCOL` sections 1 and 2: NHI and switch identity, PCI topology, and the NHI's BAR0 | static (a document read) | the document above; its [obs] from run `p14s-linux-20261014-122716-assistOFF_prebootOFF` | record 21 sections 2, 4.1-4.3, 6.3; detail: record 21 appendix A, A.6 |
| `ICM-PROTOCOL` sections 0, 3 and 4.1-4.8: ICM framing on ring 0 and the Alpine Ridge message set | static (a document read) | the same; [obs] from the same run's `T:` lines | record 21 sections 5.1-5.5; detail: record 21 appendix A, A.7 |
| `ICM-PROTOCOL` section 4.9: how Titan Ridge differs from Alpine Ridge (every row [inf]) | static (a document read) | the same; no Titan Ridge capture exists | record 21 sections 1.4 and 11 (D9); detail: record 21 appendix A, A.8 |
| `ICM-PROTOCOL` sections 5-7: the config-space traffic the client generates, the sequences, and security level `user` | static (a document read) | the same; [obs] from that run and run `p14s-linux-20261014-133751-assistON_prebootON` | record 21 sections 5.1, 5.3-5.6; detail: record 21 appendix A, A.9 |
| `ICM-PROTOCOL` sections 10 and 11: Windows 11 with Intel's driver, and the platform ACPI | static (a document read) | the same; [obs] from run `p14s-win11-20261014-135249-assistOff_prebootOff` | record 21 sections 1.3, 3.3, 10.4; detail: record 21 appendix A, A.10 |
| `ICM-PROTOCOL` section 12: pre-boot support and BIOS Assist | static (a document read) | the same; [obs] from the Windows 98 MS-DOS-mode matrix and run `p14s-linux-20261014-133751-assistON_prebootON` | record 21 sections 2, 6.5, 10.2, 10.4; detail: record 21 appendix A, A.11 |
| Linux `drivers/thunderbolt`: NHI and bridge device ids, and which connection manager each gets | static (a source read) | the pinned Linux mirror (section 3) | record 21 sections 1.4 and 3.3, and the INFs; detail: record 21 appendix A, A.12 |
| Linux `drivers/thunderbolt`: the ICM message codes and layouts per generation | static (a source read) | the pinned Linux mirror | record 21 section 5 and `src\cm\`; detail: record 21 appendix A, A.13 |
| Linux and FreeBSD: NHI register offsets and fields, and control-packet framing | static (a source read) | the pinned Linux and FreeBSD mirrors | `src\cm\tbt_regs.h`; record 21 section 4; detail: record 21 appendix A, A.14 |
| Linux `drivers/thunderbolt`: controller quirks, taken as descriptions and written fresh | static (a source read) | the pinned Linux mirror | record 21 and `src\cm\`; detail: record 21 appendix A, A.15 |
| The PCI ID Repository: the Thunderbolt NHI, switch-port and tunnel root-port ids and their listed names (decision D9) | static (a text listing read) | `pci.ids` from <https://pci-ids.ucw.cz/v2.2/pci.ids> | record 21 section 3.3; detail: record 21 appendix A, A.16 |
| The ThinkPad P14s Gen 1's PCI configuration space, as the owner captured it, used as host-test vectors (task 39-A.5) | a hardware capture (neither static nor runtime here) | `usb4-emu` `main` `a68bc74` `tests/captures` (names and SHA-256s in `test\tbt_topo_p14s.h`) | `test\tbt_topo_p14s.h`, `test\test_tbt_topo.c`; record 21 section 6.3; detail: record 21 appendix A, A.17 |
| Linux `drivers/thunderbolt`: how a domain's PCI side is associated; nothing adapted into `tbt_topo.c` | static (a source read) | the pinned Linux mirror | record 21 section 6.3; `src\cm\tbt_topo.c` and `tbt_topo.h`; detail: record 21 appendix A, A.18 |
| The PCI ID Repository, second reading: the USB4 hosts' tunnel-port ids (task 40-0.2) | static (a text listing read) | `pci.ids` version `2026.10.14`, SHA-256 `b0577d41ed389682f2a159cd97b4fd31b13810700e2823254e9689a3898cc837` | `src\cm\xhcicmf.inf`, `xhcicmf-amd64.inf`, `scripts\inf-gate\check-cm-inf.ps1`; record 21 section 3.3; detail: record 21 appendix A, A.19 |
| The PCI ID Repository, third reading: xHCI controllers' names (task 44.55; owner, 2026-10-27: a source for a controller's name where no other names it) | static (a text listing read) | `pci.ids` (record 23 section 5.13.2) | `xhciinst\inst_core.c` `instXhciNames`, `src\xhci98.inf`'s 9x models section, README.md and the release notes' tables; record 23 section 5.13.2; detail: record 23 appendix A, A.1 |
| The PCI ID Repository, fourth reading: AMD's integrated USB4 hosts' NHI ids and PCIe USB4 bridges (task 46.23), with a Strix Halo machine's device properties | static (a text listing read), and a reading of a machine's device properties (nothing installed) | `pci.ids` version `2026.10.31`, SHA-256 `3af2de60a0cb598912d4ede5314fcb36a59593d08fba4f7f26ca4d4e53a7b9ea`; Windows 11's `Get-PnpDeviceProperty`, 2026-11-03 | `src\cm\cm_vals.c`, `cm_ports.c`, `tbt_hw.c`, the four connection-manager INFs, `src\pfw\pfw_ids.c`, `xhciinst\inst_core.c`, `xhcitr\tr_core.c`, `scripts\inf-gate\check-cm-inf.ps1`; record 21 section 3.3; detail: record 21 appendix A, A.21 |
| Windows 98 SE's and ME's `cfgmgr32.dll` export the same 179 names, and no `CM_Request_Device_Eject` | static | 98 SE `CFGMGR32.DLL` 4.10.2222, 45,056 bytes; ME's (the guest images) | record 24 section 7.1; `xhciinst\xhciinst.c`; detail: record 24 appendix A, A.1 |
| Windows 2000's `setupapi.dll`, not `cfgmgr32.dll`, exports `CM_Request_Device_EjectA` | static | 2000 SP4 `cfgmgr32.dll` and `setupapi.dll` (the guest image) | record 24 section 7.2; `xhciinst\xhciinst.c`; detail: record 24 appendix A, A.2 |
| Windows 2000's `CM_Query_And_Remove_SubTreeA` is a stub, and its `W` form accepts `CM_REMOVE_UI_NOT_OK` and `CM_REMOVE_NO_RESTART` | static | 2000 SP4 `setupapi.dll` (the guest image) | record 24 section 7.2; `xhciinst\xhciinst.c`; `xhcitr\xhcitr.c`; detail: record 24 appendix A, A.3 |
| **NUSB 3.3's seven Windows 98 SE storage files: whose each is, its version and its identity** (roadmap-usb4 task 43.1) - the five binaries' `CompanyName`, `ProductName`, `FileVersion` and `LegalCopyright`, the two INFs' `Provider` and comment headers, and each file's size and SHA-256 | static: PowerShell's `(Get-Item <file>).VersionInfo` on the driver pack's copies, and for `USBMPHLP.PDR` (a VxD that reader does not parse) the `VS_VERSION_INFO` strings in the file; the INFs read as text; `Get-FileHash` against the pack's `SHA256.TXT`, 2026-10-20; nothing executed | the driver pack's copies (section 5's table) | section 5, "NUSB 3.3's Windows 98 SE storage files" |
| **Task 43.46 read-first**: how IOS builds the physical list a `SCSIPORT.PDR` request carries, and the calldown chain on Stor98's DCB | both | the QuickInstall 98 SE image's `IOS.VXD` (69,594 bytes) and the files named in the run record | `runs/run-43.md`, "43.46 read first"; Stor98 `docs/INVESTIGATION-9X-REQUEST-SIZE.md` sections 1 and 2; detail: `runs/run-43.md` appendix A, A.1 |
| Task 43.46 design: what IOS loads from `IOSUBSYS`, and the request path's facts in three IOS builds | static | `IOS.VXD` of gold 98 SE (69,570 bytes, SHA-256 `bb133fb8edc9464f38bda9ee94349014eebdfd3164583432304a2e88e1a70652`) and the other two builds | record 30 section 2; detail: record 30 appendix A, A.1 |
| Task 43.46 design: the layers above Stor98's disk and `SCSIPORT.PDR`'s own requests | static | the QuickInstall image's `DISKTSD.VXD` (18,685 bytes) and the others named | record 30 section 2.3; detail: record 30 appendix A, A.2 |
| Task 43.79: IOS's double buffer on a DCB that does not demand physical lists | both | the three `IOS.VXD` builds of the two rows above; this project's `S98VSD.VXD` on the 98 SE and ME emulators | record 30 section 4.6; `src/vsd/vsd_list.h` (`VsdPlan`); detail: record 30 appendix A, A.3 |
| Windows 8 and 8.1's xHCI match: only `usbxhci.inf` names `PCI\CC_0C0330`, with `FeatureScore=80` and three `ExcludeID` lines for Fresco Logic | static | the 8 and 8.1 INFs (record 13 section 6.12's table) | record 13 section 6.12.1; detail: record 13 appendix B, B.69 |
| What Windows 8's and 8.1's class drivers send a USB device PDO | static | `hidusb`, `hidclass`, `USBSTOR`, `uaspstor`, `USBAUDIO`, `usbccgp`, `winusb`, `Wdf01000` of 8 x86 and x64 and 8.1 x64 (record 13 section 6.12) | record 13 sections 6.12.2 to 6.12.4; detail: record 13 appendix B, B.70 |
| Windows 8's client library `usbdex`: `USBD_CreateHandle` asks `GUID_USBD_INTERFACE_V1` then USBDI V1 and succeeds even when both are refused | static | the same clients | record 13 section 6.12.3; detail: record 13 appendix B, B.71 |
| Windows 8's `usbd.sys`: the Windows 7 exports plus five hub-only ones, and the `usbshim` Kernel Shim Engine shim | static | 8 and 8.1 `usbd.sys`, `usbhub3.sys`, `ucx01000.sys` (record 13 section 6.12) | record 13 sections 6.12.1, 6.12.3 and 6.12.5; detail: record 13 appendix B, B.72 |
| The layout and calls of `GUID_USBD_INTERFACE_V1` at version `0x602`, from both ends | static | Windows 8 x86 and x64 and 8.1 x64 `uaspstor.sys`, `USBSTOR.SYS` and `UCX01000.SYS` | record 13 sections 6.13.1 and 6.13.2; detail: record 13 appendix B, B.73 |
| Windows 8's and 8.1's `IoIsWdmVersionAvailable` answers as Windows 7's, so it cannot tell 8 from 7 | static | `ntoskrnl.exe` of 8 x86, 8 x64 (6.2.9200.16384) and 8.1 x64 (6.3.9600.17415) | record 13 section 6.13.4; detail: record 13 appendix B, B.74 |
| No Windows 98 SE to 7 driver, nor NUSB 3.3, NUSB 3.6 or SweetLow's pack, carries `GUID_USBD_INTERFACE_V1`'s bytes, `0x49104B` or `0x220FB3` (task 43.29) | static | every `.sys`, `.dll`, `.vxd`, `.pdr`, `.mpd` and `.exe` under `tools\*-extracted\` | record 13 section 6.13.4; detail: record 13 appendix B, B.75 |
| Windows 8's and 8.1's `usbui.dll` carry `0x22045C` (`IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2`) beside Windows 7's request codes | static | 8 and 8.1 `usbui.dll` 6.2.9200.16384 (72,192 and 87,552 bytes) and 6.3.9600.17415 (89,088 bytes) | record 13 section 6.12.7; detail: record 13 appendix B, B.76 |
| Windows 8's and 8.1's SCSIport export every NT import of Stor98 | static | 7 x64, 8 x86 and x64 and 8.1 x64 `scsiport.sys` (identities in `runs/run-43.md`, "43.23") | `runs/run-43.md`, "43.23"; `src/stor98/docs/TESTING.md`, "Windows 8 and 8.1"; detail: `runs/run-43.md` appendix A, A.2 |
| Windows 10's SCSIport (22H2, x86 and x64) exports every NT import of Stor98 and exactly 8's names | static | 10 22H2 x86 and x64 `scsiport.sys` (identities in `runs/run-43.md`, "43.23", "Windows 10") | `runs/run-43.md`, "43.23", "Windows 10"; `src/stor98/docs/TESTING.md`, "Windows 10"; detail: `runs/run-43.md` appendix A, A.3 |
| Windows 8 x64's, 8.1 x64's and 10 x64's driver store refuses a package with no catalog file in test-signing mode (`0xE000022F`) | runtime (Windows' own, in QEMU) | Windows 8 x64, 8.1 x64, 10 x64, 8 x86 and 10 x86 from the owner's media | `runs/run-43.md`, "43.23"; `src/stor98/docs/TESTING.md`, "Windows 8 and 8.1" and "Windows 10"; detail: `runs/run-43.md` appendix A, A.4 |
| Windows 8 x86's `classpnp` retries a WRITE that SCSIport completes with `SRB_STATUS_NO_DEVICE` and `SRB_STATUS_SELECTION_TIMEOUT` alike, until the disk is removed | runtime (QEMU) | Windows 8 x86 and 10 x86 `classpnp.sys`, with Stor98 builds of this project | `runs/run-43.md`, "43.23"; `src/stor98/docs/PLAN.md` item 2.2; detail: `runs/run-43.md` appendix A, A.5 |
| Windows 8.1 x64's `stornvme` under a link loss with no notice holds the write, logging events 129 and 11, and completes it about 23 s after the link returns | runtime (QEMU, Windows' own driver) | Windows 8.1 x64 and 10 x86 and x64 `stornvme.sys` | `runs/run-43.md`, "43.23"; `src/stor98/docs/TESTING.md`, "Windows 8 and 8.1"; detail: `runs/run-43.md` appendix A, A.6 |
| Windows 8.1's `stornvme.inf` binds `PCI\CC_010802` as a boot-start Storport miniport, `DriverVer=06/21/2006,6.3.9600.16421`, `ExcludeFromSelect = *` (task 43.23) | static (its outranking Stor98's INF seen at runtime) | `Windows\inf\stornvme.inf` of the 8.1 medium named in the run record | `runs/run-43.md`, "43.23"; `xhciinst\inst_core.c` `inst_s98_over_inbox`; detail: `runs/run-43.md` appendix A, A.7 |
| Windows 98 SE's and ME's ASPI layer is installed by Setup, and its adapter identity is a fixed string (task 43.50, record 29's reading P.0) | static | the owner's `Win98SE.iso` (SHA-256 begins `c048c31e414e3fe4`) and `Windows Me OEM Full.iso` | record 29 sections 6.2 and 15; `xhciperf\xhciperf.c`; Stor98 `docs/PLAN.md` MD-8, `src\stor98\src\sg9x.h`; detail: record 29 appendix A, A.1 |
| **NUSB 3.3's installer: how it places its INFs** (task 43.42): an IExpress package | static | `tools\nusb33e.exe`, 774,144 bytes, SHA-256 `f1f30a800cf6013eb6f18db775d88e777ddc3924622e6560edd2dcd7b53698d3` (section 5's) | record 26 section 6.5.11; detail: record 26 appendix A, A.1 |
| Where Windows 8's and 8.1's "Device can perform faster when connected to USB 3.0" comes from: `shell32.dll`'s strings and `usbui.dll`'s Devices and Printers status provider; `hotplug.dll` carries neither | static | 8 x86 `shell32.dll`; `usbui.dll` 6.2 and 6.3 (record 13 section 6.12.11's table) | record 13 section 6.12.11; detail: record 13 appendix B, B.77 |
| What the line tests: the volume's `BusType` 7, the disk, the container id, the hub and the port | static | the same files | record 13 section 6.12.11; detail: record 13 appendix B, B.78 |
| Windows 10 22H2's xHCI match: only `usbxhci.inf` names `PCI\CC_0C0330`, with 8.1's models, `FeatureScore=80` and the Fresco Logic `ExcludeID` lines | static | 10 22H2 `usbxhci.inf`, `DriverVer = 01/13/2023,10.0.19041.2546` | record 13 section 6.14.1; record 21 section 3.6; detail: record 13 appendix B, B.79 |
| What Windows 10's class drivers send a USB device PDO, against 8.1 | static | 10 22H2 `USBSTOR`, `uaspstor`, `hidusb`, `hidclass`, `usbccgp`, `winusb`, `Wdf01000`, `USBAUDIO` (record 13 section 6.14's table) | record 13 section 6.14.2; detail: record 13 appendix B, B.80 |
| Where Windows 10 22H2 shows "Device can perform faster when connected to USB 3.0" and what it tests | static | 10 22H2 `shell32.dll` 10.0.19041.2788, `usbui.dll` 10.0.19041.1 | record 13 section 6.14.4; detail: record 13 appendix B, B.81 |
| **Intel's Thunderbolt driver on Windows 10**: its INFs install the service `nhi`, x64 only, and match the NHI ids in the forms the item lists | static | `tbt100x.inf` 17.4.77.13; `TbtHostController.inf` 1.41.1379.0 and 1.41.1423.0 (section 2's row) | record 21 section 3.6; detail: record 21 appendix A, A.20 |
| Windows 8 and 8.1 x64 refuse a driver package with no catalog in test-signing mode, and 8 and 8.1 make `xhcicm` boot start when its filter goes onto a PCI bridge (task 43.22) | runtime (Windows' own logs and state, QEMU) | Windows 8 Pro x86 and x64 (6.2.9200) and 8.1 Pro x64 (6.3.9600) | record 23 section 5.10 (revision 30), `runs/run-43.md` "43.22"; detail: record 23 appendix A, A.2 |
| Microsoft's "Driver Signing Policy" table, by Windows version and Secure Boot | documentation, not a binary reading | Microsoft's page, read 2026-10-22 | record 23 section 5.10 (revision 30); detail: record 23 appendix A, A.3 |
| **`PCI.VXD`'s Code 29 on 98 SE and ME (task 44.30)**: for a function with neither decode bit set (not a CardBus bridge or display) and no IRQ, its enumerator calls `Disable_DevNode(dn, 2)` | static (consistent with the runtime reading of `runs/run-44.md` 44.30) | 98 SE `pci.vxd` 4.10.0.2222, 65,895 bytes, SHA-256 `a9268f51f5a486817f5484686398215b3600c8eb3608773195ea05ecfacb303d`; ME `pci.vxd` 4.90.0.3000, 70,539 bytes, SHA-256 `5fcb833b90693617fc8003e3d84e48e7b24058cd579096a43e28effeae61cb58` | record 28 section 12.1; `src/cm/cm_noirq.c` (`niMemOnly`); detail: record 28 appendix A, A.1 |
| CONFIGMG's `Disable_DevNode` with `CM_DISABLE_HARDWARE` sets problem 29, and `Reprocess_DevNode` and `Setup_DevNode(READY)` clear a problem whatever its code (task 44.30) | both | 98 SE `configmg.vxd`, 125,057 bytes, SHA-256 `568f800c9633f1ebaf48204fe2c5e6b5cffaee25f00191b9c29e762a0e154162` | record 28 section 12.1; detail: record 28 appendix A, A.2 |
| On 98 SE a timed kernel wait is a VMM global time-out, and the VMM processes that list through one restricted event it requests only when its accumulator leaves zero (task 44.29) | static; runtime for the list's state, read from a memory image | 98 SE NTKERN and VMM (the objects placed in a guest's memory image) | `runs/run-44.md` 44.29; detail: `runs/run-44.md` appendix A, A.1 |
| Windows 98 SE's `VMOUSE.VXD` cancels, when the mouse moves to another VM, an event handle one of its event callbacks leaves stale (task 44.29) | static; debugger (a QEMU gdbstub breakpoint) for the cancel | 98 SE `VMOUSE.VXD` | `runs/run-44.md` 44.29; detail: `runs/run-44.md` appendix A, A.2 |
| The ASMedia ASM4242 USB4 host, PCI side and firmware state as `usb4-emu`'s asm4242 model and data record them (task 44.39) | static | `usb4-emu`'s asm4242 archives (logs, `lspci`, sysfs, trace, raw configuration space, two ACPI tables) | record 22 section 18; roadmap-usb4 44.39 and 44-E.6; record 23 section 5.13.8; `src\cm\cm_ports.c`; detail: record 22 appendix A, A.1 |
| Windows 2000 SP4's `pci.sys` and 98 SE's and ME's PCI enumerator with a 64-bit prefetchable window above 4 GB (task 44.49) | debugger and static | 2000 SP4 `pci.sys`; 98 SE and ME `PCI.VXD` (read from the running guests' memory) | `runs/run-44.md` "44.49"; design record 32 sections 1 to 3; detail: `runs/run-44.md` appendix A, A.3 |
| Windows 2000 SP4's `pci.sys` at a function PDO's stop, remove and power-down | static | 2000 SP4 `pci.sys` 5.00.2195.6655, 59,312 bytes, SHA-256 `66CCD7B1...EA26` | record 17 section 4.13.5; detail: record 17 appendix A, A.1 |
| **Windows 98 SE's `PCI.VXD` 4.10.2222 at a devnode's stop, remove and set-D3** | static | 98 SE `PCI.VXD` 4.10.2222, 65,895 bytes, SHA-256 `A9268F51...303D` | record 17 section 4.13.5; detail: record 17 appendix A, A.2 |
| The signature `DMAR` is in none of the files read (a narrow static result) | static | the files listed in the item (from the owner's media) | record 17 section 4.13.5; detail: record 17 appendix A, A.3 |
| Windows 2000's and XP's whole-machine USB walk opens `\\.\HCD<n>` for n below 10, and 2000 usbport's `HCD<n>` name (task 44.81) | both | 2000 SP4 and XP SP3 `usbui.dll` and `usbport.sys` (section 2's 44.81 row) | `runs/run-44.md` "44.81"; record 13 sections 8.1 and 8.8; detail: `runs/run-44.md` appendix A, A.4 |
| Windows 7's Intel 82574L miniport walks the PCI capability list with no bound, so configuration space reading all ones holds the processor in it (task 45.49) | debugger and static | Windows 7's 82574L miniport, read from the running guest's memory | `runs/run-45.md` "45.49"; detail: `runs/run-45.md` appendix A, A.1 |
| What the disk class drivers send a disk's port for the SMART and pass-through IOCTLs (task 45.52, reading R1) | static | `disk.sys` and the class drivers of 2000 SP4, XP SP3, XP x64 SP2, Vista x86 SP2 and 7 x86 | record 13 section 16.11, R1; detail: record 13 appendix B, B.82 |
| The NDIS.SYS imports of `xhcinet.sys` x86 on 98 SE and ME (task 45.59 step S6): each of its 27 pairs is a name in `NDIS.VXD` of 98 SE and ME | static (export names alone; owner's rule of 2026-10-31); runtime for the 9x legs | 98 SE `NDIS.VXD`, 166,029 bytes, and ME's (SHA-256s in the item and in `ndisvxd-baselines.expected`) | `scripts/import-gate/xhcinet-imports.allow`, `ndisvxd-baselines.expected` and `net98-runtime.list`; design record 35 section 7.8; detail: record 35 appendix A, A.1 |
| The imports of `xhcinet6.sys` on 8, 8.1 and 10, x86 and x64 (task 45.59 step S7), are all in the export tables of the 8.1 and 10 `ntoskrnl.exe`, `hal.dll` and `ndis.sys` of its architecture, and on x86 also of Windows 7 SP1 x86's kernel and HAL | static | 8.1 and 10 `ntoskrnl.exe`, `hal.dll` and `ndis.sys` (in the four `net6-*-baselines.expected`) | `scripts/import-gate/net6-win81-x86-baselines.expected` and its three siblings, `xhcinet6-imports.allow`, `xhcinet6-imports-amd64.allow`; design record 35 section 7.9; detail: record 35 appendix A, A.2 |
| A common buffer of less than a page on Windows 2000 and XP can come back 8-byte aligned (task 45.59) | runtime | Windows 2000 SP4 x86's HAL `AllocateCommonBuffer`, under a scratch build of this driver | `src/cm/cm_pool.c` (`CmDmaCommonAlloc`, whole pages); `runs/run-45.md` "45.59 (legs, continued)"; detail: `runs/run-45.md` appendix A, A.2 |
| Windows' own USB stacks answer `IRP_MN_QUERY_BUS_INFORMATION` with `GUID_BUS_TYPE_USB` `{9D7DEBBC-C85D-11D1-9EB4-006008C3A19A}`, `PNPBus` (15) and bus number 0 on every root hub, hub and device PDO and on a composite device's function PDO from XP on; on 2000 that function PDO answers nothing (task 48.13) | runtime (the PnP manager's stored `BusTypeGuid`, `LegacyBusType` and `BusNumber` of each devnode, read through SetupAPI by a guest tool of the leg; nothing disassembled, not this driver's counters) | Windows 2000 SP4 (`usbport`, `usbhub`), XP SP3 (`usbhub`, `usbccgp`), 7 SP1 x86, and 10 x64 19045 (`usbhub`, `USBHUB3`, `usbccgp`), in QEMU | `src/hcd_pdo.c` (`HcdPdoBusInformation`), `src/hcd_rh.c`; design record 13 section 20; `runs/run-48.md`, "48.13" |

## 5. Microsoft binaries: what is published, and through which channel

This project distributes through the git repository and, separately, through
the GitHub release download; a sentence true of one is false of the other.
"The release download carries X" means the asset `make-release.ps1`
assembles carries X, and that asset is what is uploaded (`releases/README.md`
describes the same asset). Every version under `releases/` has since been
uploaded to a GitHub release, and this repository is public. "Published" has
two senses here: the INF, `releases/history.md` and `releases/README.md` say
a version is "published under `releases\`", meaning cut and committed to the
tracked tree (a cut writes `releases/<version>/` and
`out/xhci98-<version>.zip` in this working tree; in that usage "published"
names which asset filename was in use at a cut); this section means uploaded
to a GitHub release, where a stranger can download it. Where it could be
misread, prefer "cut" and "uploaded". The roadmap carries no clause for an
upload: it is one act of the owner's, not work this repository can do or
close.

### The repository carries none

`usbport.sys` and `usbhub20.sys` come from the target machine's own NUSB
install (Windows 98 SE) or from Service Pack 4 (Windows 2000) and are never
tracked. The toolchain archives (MSVC 6.0, the Windows 2000 DDK) and the NUSB
package are downloaded by the user from the links in `README.md` into the
git-ignored `tools/`. The two `usbd.sys` builds and Windows 98 SE's
`usbhub.sys` are staged there too, from the user's own install media, by
`scripts/package/extract-usbd-sources.ps1`, as reference copies;
`scripts/import-gate/win98-evidence.list` records the Windows 98 pair's
identity, never content. `releases/<version>/`, the tracked half of
packaging, carries `xhci98.sys` and `xhci98.inf` and stops there, and since
1.0.0.1 so does the release download, with the exceptions below that the
repository never carries (NUSB 3.3's seven, decided for `3.2.0.0`; downloads
2 and 3).

The overlap audit swept the full added-file history and found no Microsoft or
NUSB binary in it; that is still true. The withdrawn exception below was a
release asset rather than a tracked file for the reason the later ones are
too: an asset can be withdrawn, and a git blob cannot be without rewriting
history.

### The Windows 2000 F6 option: files the user copies, not the media

Decided by the owner on 2026-10-14. Windows 2000's text-mode Setup lays down
`hidusb.sys` and `usbstor.sys` for a USB keyboard, mouse or stick on the xHCI
controller but not `usbd.sys`, `hidclass.sys` or `hidparse.sys`, which they
import, and the first GUI-mode boot stops (design record 13 section 5.6,
limit 7). From `2.4.0.0` the 32-bit `txtsetup.oem` has a second F6 entry, for
Windows 2000 only, that copies those three from the F6 floppy; the release
notes tell the user to copy them there, `USBD.SY_`, `HIDCLASS.SY_` and
`HIDPARSE.SY_` unchanged, from the `I386` folder of their own Windows 2000
CD (from `3.4.3.0` the same entry serves 32-bit XP, with the three files
from the user's own XP CD: task 48.25, owner 2026-11-09). What is
published is `txtsetup.oem` naming the three, and the
instructions. The release download and this repository carry none of them,
under either name: `PKG-MSFILE` (in `check-inf.ps1` and
`check-txtsetup-oem.ps1`) refuses a staged package holding any of them or the
compressed name of any file it refuses, and `OEM-W2K` admits the three names
in `txtsetup.oem` only on that entry's lines. The legs that read the route
used copies from the development machine's own Windows 2000 SP4 CD image, in
a git-ignored directory (`out\p38-matrix\legs\f6w2k\`).

### NUSB 3.3's Windows 98 SE storage files: in the release download, from the driver pack

Decided by the owner on 2026-10-20 (roadmap-usb4 tasks 43.1 to 43.5). Windows
98 SE has no USB mass-storage driver, and storage there, Bulk-Only and UAS
alike, needs the files NUSB's mass-storage part installs (`xhciuas.inf` names
`USBNTMAP.SYS` as a filter on the UAS device; without it a UAS disk sits at
Code 2). From `3.2.0.0` the release download may carry seven of them, the one
exception to "the release download carries no Microsoft file" since 1.0.0.1.
Decided:

- **The channel is the release download only.** Never in a commit of this
  repository, under `releases/<version>/` or anywhere else; taken at
  packaging time from the owner's separate repository `usb-wdm-nonfree` (its
  top-level `nusb33/w98se/`) and staged into the upload set in `out\` alone,
  at the download's root as `NUSB33\`, a folder of its own beside the
  installer (`NUSB33\NOTICE.md`, `NUSB33\SHA256.TXT`, `NUSB33\W98SE\`; owner,
  2026-10-20, and 2026-10-22 for the folder), where the installer takes them
  from. No package of `make-package.ps1`'s carries them (task 43.3:
  `make-release.ps1` reads them from the checkout's git objects at one
  revision and holds them to the pack's `SHA256.TXT` and to an allowlist of
  the seven hashes below).
- **NUSB is kept apart from the non-free driver pack** (owner, 2026-10-22).
  In `usb-wdm-nonfree` the seven moved, unchanged, from
  `non-free/storage/nusb33/w98se/` to a top-level `nusb33/w98se/`, with their
  provenance in `nusb33/NOTICE.md` (`usb-wdm-nonfree` `f1e0c4e`); in the
  download they are in `NUSB33\`, never inside a `NONFREE\`. The owner's
  stated reasons: the seven are Microsoft's files and NUSB's `USBU2A.SYS`, no
  licence to redistribute them has been found, the owner accepted that risk
  for them, and the base download carries them for its installer while the
  rest of the pack is offered apart.
- **No licence to redistribute them has been established.** None was found
  for any of the seven, none is claimed here or in the pack, and the pack's
  `nusb33/NOTICE.md`, shipped beside the files, says whose each is and that
  its owner grants no licence to them. The owner took the decision with that
  stated; it is a fact about the decision, not a permission.
- **The files are unmodified**, byte for byte as NUSB 3.3 for English Windows
  98 SE ships them: `nusb33e.exe`, 774,144 bytes, SHA-256
  `f1f30a800cf6013eb6f18db775d88e777ddc3924622e6560edd2dcd7b53698d3`. NUSB
  3.3's INFs are taken rather than 3.6's, which copy no file on their own;
  the binaries are the same in both.
- **The decision requires the installer to offer them on Windows 98 SE
  only**, as a row the user can untick, installing over no file already
  present (task 43.4, done 2026-10-20).

Per file, with its evidence (static: the version resource of the pack's copy
by PowerShell's `(Get-Item <file>).VersionInfo`, and for `USBMPHLP.PDR`, a
VxD that reader does not parse, the `VS_VERSION_INFO` strings in the file;
the INFs' own comment headers; nothing executed). The hashes are the pack's
`SHA256.TXT`, checked against the files on 2026-10-20.

| File | Bytes | SHA-256 | Whose, by the evidence |
| `USBSTOR.SYS` | 21,040 | `b92a6a1582287867d4950495c0a746f0f5d8831bc311061423f8645073e42d8c` | Microsoft Corporation, "Microsoft(R) Windows(R) Millennium Operating System", 4.90.3000.1 |
| `USBNTMAP.SYS` | 7,136 | `d1595cdbfdc22925c4e47d1720f117fe6c9beda36417ba41a1fd7398878cc6e4` | Microsoft Corporation, the same product, 4.90.3000 |
| `USBMPHLP.PDR` | 4,861 | `e8e0604fddcd1772f1c5cba99de9d72d79288d0b303ed3e7bf02d36160698c1c` | Microsoft Corporation, the same product, 4.90.3000 ("USBMPHLP Virtual Device (Version 4.0)") |
| `USBAUTH.SYS` | 6,608 | `12b09e67a5a9a8a883ab40b91c59be294f2ddc5940b9780c056176df04a463d1` | Microsoft Corporation, the same product, 4.90.3000.1 |
| `USBU2A.SYS` | 5,183 | `1c5656dcf3dbbca97236fc0239537a34ae5a6ca1597f29cadc9c38537b2a7b78` | "USB Compliance", 1.42, "Copyright c 2000"; no product name |
| `USBSTOR.INF` | 22,017 | `e78b5f2d2d30b039d74d670f09ba41c39bdfca058b72fd7c22eb7d55f81f0f6b` | Microsoft's INF (`provider=%MSFT%`), with the edits its own comment header lists (eight, seven of them Maximus Decim's and one unattributed); its `CopyFiles` copy `USBAUTH.SYS` and `USBU2A.SYS` with the driver |
| `USBNTMAP.INF` | 1,339 | `372d0a4829eab33cf5c6a1fca347a3be45da82d69a692e89b33d28dc02848657` | Microsoft's INF (`Provider=%Msft%`), with the two edits its own comment header lists, both Maximus Decim's |

What is published when the pack is included: the seven, the pack's
`nusb33/NOTICE.md` and a `SHA256.TXT` of the seven in `NUSB33\` at the upload
set's root; the generated readme and the installer's `NOTICE.TXT` saying so
in place of "No Microsoft file is in this download" (`make-release.ps1`
chooses the text by whether the pack is in the download); and the record of
the pack revision used, `releases/<version>/drvpack.txt` (tracked, plain
text: whether the pack is included, its revision and every file's hash, from
which `make-release.ps1 -UploadSetOnly` rebuilds the download; it holds none
of the files; task 43.3). A download built without the pack carries none of
them and says so as before. Whether the seven are all a stock Windows 98 SE
needs is not yet read (task 43.5); nothing here or in the pack claims it.

Status: in every cut from `3.2.0.0` (2026-10-24; its `drvpack.txt`:
"Included: yes"). `3.1.0.0` and every earlier download carry none of the
seven.

### The other USB drivers: downloads 2 and 3 (task 43.48)

Decided by the owner on 2026-10-22 (task 43.48; design record 26 section
6.7). From `3.2.0.0` a release may have three downloads, each holding the one
before: `xhci98-<version>.zip` (the package and `NUSB33\`),
`xhci98-<version>-free.zip` (adds `WDMFREE\`) and
`xhci98-<version>-nonfree.zip` (adds `NONFREE\`). This widens 43.1's
decision, which admitted NUSB 3.3's seven alone, and relaxes the owner's rule
of 2026-10-20 that the repositories are independent, for the release
downloads only: the copies are made at packaging time, and no repository's
sources or build depend on another's.

- **The channel is the release download only**, as for the seven: never a
  commit of this repository, never `releases/<version>/`, never a package of
  `make-package.ps1`'s; `make-release.ps1` reads each set from its
  repository's git objects at a pinned tag and refuses a published tree
  holding either folder.
- **`NONFREE\` is the `non-free/` folder of the owner's `usb-wdm-nonfree`
  at tag `v1.3.0`** (commit `d2f7a26987fb9e074f6c5c4e974b6cad80c96c46`), every
  file held to that commit's `SHA256.TXT`, whose own SHA-256
  (`adf5ab70cb6bbf52ced63038f5f327cd1e2e466d71cdec61354537f0e75888f7`) is
  pinned in `package-common.ps1`; with the pack's `NOTICE.md` and `README.md`
  unchanged, its `STAGE.TXT` unchanged but for the lines of a folder left
  out, and a `SHA256.TXT` the cut writes. The pack's `nusb33/` is not in it:
  NUSB 3.3's files are in `NUSB33\` alone, in every download.

  | Releases | Tag | Commit | Folder holds |
  |---|---|---|---|
  | `3.2.0.0` | `v1.0.0` | `3b558c04a462a643abec84b31e5611c866e3a72d` | the whole `non-free/` |
  | `3.3.0.0`, `3.4.0.0` | `v1.1.0` | `565d11b40fbeb53f5a576c01b5ceba331892c610` | `non-free/wifi/` left out (owner, 2026-10-28: the Wi-Fi drivers stay in `usb-wdm-nonfree` and its own release): its Ethernet, HID and serial drivers |
  | `3.4.1.0` (owner, 2026-11-02: the next release uses the updated pack) | `v1.2.0` | `c40ad7e4568fe2be7a889a79f6ada01f33123e81` | as `v1.1.0`, and its audio driver: `audio/us122/w98se-me/`, TASCAM's US-122 driver 3.24 for 98 SE and ME, which TASCAM no longer offers |
  | from `3.4.2.0` (owner, 2026-11-08) | `v1.3.0` | `d2f7a26987fb9e074f6c5c4e974b6cad80c96c46` | as `v1.2.0`, and Microsoft's USB smart card reader driver: `smartcard/usbccid/w2k-xp/` (5.2.3790.2444) and `smartcard/usbccid/wxp/` (6.0.6001.18176), copied only |

  The folder's files are other makers' drivers (ASIX, Realtek, FTDI, WCH and
  Masahiko Morii's Xbox 360 controller drivers, among others; the pack's
  `NOTICE.md` says whose each is), unmodified, as their makers released them.
  **Among them are Microsoft files**, the KMDF co-installers ASIX and Davicom
  ship with their drivers: at `v1.0.0`
  `ethernet/ax88772/w2k-xp/WdfCoInstaller01007.dll` (KMDF 1.7, 1.7.6001.0)
  and `ethernet/ax88179/wxpvis/WdfCoInstaller01009.dll` with its `wxpvis64/`
  twin (KMDF 1.9, 1.9.7600.16385) (static: the pack's `NOTICE.md` lines 29
  and 31, read 2026-10-22); at `v1.1.0`, `wifi/` left out, 26 copies of
  `WdfCoInstaller01007.dll` and `WdfCoInstaller01009.dll` in folders of
  `ax88172a/`, `ax88178/`, `ax88179/`, `ax88772/`, `ax88772b/`,
  `dm96xx/wxp-7/` and `mcs7830/` under `ethernet/` (static: the exported
  folder's file names and the pack's `NOTICE.md`, which names Microsoft
  Corporation for each, read 2026-10-28); and at `v1.3.0`, Microsoft's USB
  smart card reader driver itself, `smartcard/usbccid/`: two Microsoft
  Update Catalog cabinets, unpacked and unmodified, the second with the
  co-installers Microsoft ships in it. The pack's `NOTICE.md` records the
  catalog updates and cabinet hashes, that neither cabinet carries a
  licence, and the catalog FAQ's statement on the terms that govern such
  updates (static: the pack's `NOTICE.md`, read 2026-11-08).

  The pack's `NOTICE.md` quotes the statements some makers made about passing
  their files on - FTDI's ("The driver may be distributed in any form as long
  as our license information is not modified", in its INFs and on its driver
  page as archived on 2011-10-20) and Masahiko Morii's (on his home page as
  archived on 2011-11-23, that those who need to repost his files may do so
  on their own responsibility) - and records that, apart from those, no
  licence permitting the files' redistribution has been found for any of them
  (its "The driver files are not this repository's to license"). The US-122
  package differs in one respect, recorded in the pack's `NOTICE.md`: it
  carries no licence of its own, and TASCAM's download page links TEAC's site
  terms, which forbid "downloading, reproduction, change, uploading, posting,
  transmission, distribution" of the site's content, programs among it,
  without TEAC's prior written permission (<https://www.teac.co.jp/int/terms>,
  "Last updated: April 1, 2016", read 2026-11-02). No permission has been
  sought or given; the owner's view is that those terms no longer cover a
  file the site no longer has (TASCAM's link to it returns HTTP 404,
  2026-11-02), which is not established. Nothing here assesses those
  statements, none is claimed for this project, and the owner took the
  decision with that stated; it is a fact about the decision, not a
  permission. The pack says its drivers are untested by its maintainer
  (owner, 2026-10-22: no VM checks of them); the installer and the readme say
  they are other makers' drivers, not this project's work (until 2026-10-24
  they also said the drivers were not tested by this project; the owner
  removed that warning that day).
- **`WDMFREE\` is the qualifying drivers of a tagged `usb-wdm-free`
  release** (the owner's own project, GPL-2.0-or-later from its `v0.1.2`,
  GPL-2.0-only before), every file held to a SHA-256 in
  `scripts/package/wdmfree-pin.txt`, with that release's `LICENSE` and
  `NOTICE.md`. A driver qualifies only once its legs have passed on its
  systems (roadmap 43.48). The pin, in order:

  | Tag | Commit | Task, owner | What changed |
  |---|---|---|---|
  | `v0.1.0` | `57ba8091b382fbc3d0793cf85d5e912a8bd08a7d` | - | `3.2.0.0`'s download 2 |
  | `v0.1.1` | `646b51aa6dc0205714b02be5635028cc102629a1` | 44.34, 2026-10-25 | from `3.3.0.0`: `v0.1.0`'s files unchanged, plus `ax88179`, the owner's driver for ASIX AX88179 adapters, whose framing, control and bring-up code is adapted from Linux's `ax88179_178a.c` (GPL-2.0-or-later, ASIX's copyright notice kept in each adapted file; that release's `NOTICE.md` lists the files and their sources); no file of ASIX's own driver |
  | `v0.1.2` | `c9b5269c953ab59b2cd0abf6920a10858fdf6243` | 44.34, 2026-10-26 | the same packages under GPL-2.0-or-later, `rtl815x`'s chip core rewritten from the chips' register facts so that it no longer follows Linux's `r8152.c` (GPL-2.0-only), the earlier adaptation (usb-wdm-free's `docs/design/ethernet.md` section 6.4) |
  | `v0.1.3` | `062989b3a88df71d566ed31c53bd18f911f97129` | 44.44, 2026-10-26 | seven INFs of its own changed (class matches), no file from another project |
  | `v0.1.4` | `222730cbbc8992880409c1d32c5038a043af6482` | 44.70, 2026-10-27 | `rtl815x`'s newer Realtek chips and `uac2`'s volume and mute, its own code; its `NOTICE.md` unchanged |
- **The installer copies a set only when the user ticks it, and never
  installs it** (owner, 2026-10-22); the uninstall removes what it placed.
- **The generated readme of each download says which download it is** and,
  for download 3, that it carries other makers' files, Microsoft's two among
  them, with no licence to redistribute them established.

This tree holds the pins and, per release, `releases/<version>/downloads.txt`
(which downloads were cut, each set's tag, commit and the hash of the
`SHA256.TXT` the cut wrote) and, with the free set, a copy of the pin it was
cut with, from which `-UploadSetOnly` rebuilds the downloads.

Status: in every cut from `3.2.0.0`.

### The GitHub release download carried three of them, until 1.0.0.1

Decided by the maintainer for the two `usbd.sys` builds, extended to
`usbhub98.sys` on the same grounds and by the same mechanism, and withdrawn on
2026-09-02 before any release had been uploaded. From release 0.0.0.4 to
1.0.0.0 the asset `make-release.ps1` assembled carried `usbd98.sys`,
`usbd2k.sys` and `usbhub98.sys` beside the driver, so a download would be a
complete install set:

- Each `usbd` file was the target OS's own `usbd.sys`, byte for byte as
  Microsoft shipped it: Windows 98 SE 4.10.2222 from `BASE5.CAB`, Windows 2000
  SP4 5.00.2195.6658 expanded from `I386\USBD.SY_`; neither modified, patched
  or recompiled. Version, length and SHA-256: of the 98 file in
  `scripts/import-gate/win98-evidence.list`, of the 2000 file in
  `scripts/package/extract-usbd-sources.ps1`, which still stages both as
  reference copies. Renamed on the media only, to `usbd98.sys` and
  `usbd2k.sys`, because both install as `usbd.sys` and each install path had
  to reach exactly one; `src/xhci98.inf` copied the one its path named with
  `COPYFLG_NO_OVERWRITE`, so a machine with a `usbd.sys` kept its own.
- `usbhub98.sys` was Windows 98 SE's own `usbhub.sys`, 4.10.2222, 35,680
  bytes, byte for byte from the SE CD, taken from the same cab read that
  yielded `usbd98.sys`, renamed and copied the same way, on the Windows 98
  path only (on Windows 2000 that filename is the OS's own USB 1.1 hub
  driver).
- Why: `usbhub20.sys` imports `USBD.SYS` on both targets, and on an
  xHCI-only machine nothing placed one, so the root hub failed with
  `0xc0000034` naming `usbhub20.sys` on Windows 2000 and sat at Code 2 on
  Windows 98 (`docs/contributing/lessons.md`, "`usbhub20.sys` bugchecks
  Win2000"). `usbhub.sys` is Windows 98's composite parent driver, missing
  for the same cause: Windows 98 ships `USB.INF` on every install but copies
  the USB driver files only when setup detects a USB controller, so a
  multi-interface device sat at `Code 2`. Batch 13-E measured it on real
  hardware: every composite device on the ThinkPad E460 was dead, a
  two-interface HID keyboard among them, and all worked once the file was
  present; the control was a ThinkPad X61 with the same Windows 98 SE and
  NUSB 3.3 and no xHCI, where setup had found its UHCI controller.
- Permission: no permission to redistribute these files is documented
  anywhere in this repository, and none was claimed. That both products are
  long out of support, and that the files were unmodified copies placed only
  where the OS itself would have placed them, are facts about the situation,
  not permissions.

What replaced it. Release 1.0.0.1 has the Windows setup engine copy
`usbd.sys` and `usbhub.sys` from the operating system's own install source:
`LayoutFile=layout.inf` in the INF's `[Version]` resolves a `CopyFiles` entry
its own `[SourceDisksFiles]` does not name through the OS's `layout.inf`, and
the engine fetches the file from the Windows source path (the CABs on the
hard disk or the Windows 98 CD; the NT targets' driver cache, Windows 2000's
`sp4.cab`, Windows XP's `sp3.cab`), with the same `COPYFLG_NO_OVERWRITE`. The
release download carries this project's own files, the two tools and the
readmes, and no Microsoft file; the INF gate refuses an INF or a package that
names one (`OS-MEDIA`, `PKG-MSFILE`); `scripts/package/usbd-sources.expected`
and the three-file wording in `releases/README.md` and `AGENTS.md` went in the
same change. The reference copies under `tools/` stay, read by the import gate
and the Windows 2000 VM setup and packaged by nothing (section 4, "Where those
copies came from"). The measurements are in
`docs/contributing/build-and-test.md`, "The SweetLow stack" and "The files the
OS supplies"; roadmap Phase 17 has the tasks. Release 1.0.1.0 extends the
route on the NT path to `usbport.sys` and `usbhub.sys`, which that path did
not copy until then: an NT install that never had a USB controller has
neither (measured on a Windows XP guest on 2026-09-03), and both come from the
`Driver Cache\i386` every install carries; no file was added to the media and
no gate rule relaxed (roadmap Phase 19).

Status: the exception was never used; no asset of any version was uploaded
while it stood. Every version under `releases/` (`1.0.0.0` to `1.1.0.0`) has
since been uploaded to a GitHub release; the uploaded `1.0.0.0` asset holds no
Microsoft file (the owner, 2026-09-22), and every cut since 1.0.0.1 carries
nothing under the exception. "The release download carries three of them" was
true of the assembled asset from 0.0.0.4 to 1.0.0.0 and of no download anyone
made.

---

## 6. Keeping this file true

Update it in the same change that creates the need:

- A new third-party document or binary: a row in section 2's table, and in
  `docs/references/README.md` if a document; a git-ignored directory with its
  URL and SHA-256 recorded, never tracked (`AGENTS.md`, "Third-Party Material
  and Provenance"). A document kept in no directory of this tree still gets
  the row (the Oney book, cited by printed page in three tracked documents and
  by name in three more, long had none): record what makes a citation
  checkable (edition, page-index offset, a hash of the copy read), and say
  plainly when there is no URL.
- Something published through a channel that is not the repository: say
  which channel, in section 5, and plainly what goes through it. "Not
  tracked" and "not distributed" were once the same statement here; write
  no sentence that leaves the reader to work out which it means.
- A new tracked build output: ask what the linker put in it before treating
  it as this project's code ("our sources produced it" is not "it contains
  only our code"; section 2a exists because the two were conflated for a
  published binary). Read the map, name what is statically linked in, and
  record the notices that material asks for.
- A new fact read out of a shipping binary: a section 4 row with its actual
  method; static stays static even if the system also runs.
- A method correction: correct the row. Section 4 exists because an earlier
  audit described several disassembly-derived facts as live trace results,
  wrong in the direction that matters.
