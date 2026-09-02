# XHCI Windows 98/2000 Driver - Agent Guide

This is the maintainer's guide to the repository: what the project is, how it
is put together, the constraints that bind every change, and where to start.
It is written for AI agents and for people alike. It carries the rules and the
map; the detail lives in `docs/`, and each section names the document that
owns its subject.

## Project Purpose

A WDM kernel-mode USB host controller driver for the xHCI (USB 3.0) hardware
spec, so that modern machines, whose USB chipsets are xHCI-only, can run
Windows 98 SE and Windows 2000 SP4 with working USB devices. Both operating
systems are first-class targets: a single `xhci98.sys` binary must install and
work on either, and a phase is not done until its checkpoint has been observed
on both.

The two targets fail in different directions, so one is not a proxy for the
other. Win98 is where the loader gate, the back-ported NUSB `usbport.sys`, and
the 16-bit setup engine bite. Win2000 is where SMP races, Driver Verifier, and
the strictly enforced power/IRQL rules bite. A green run on one says nothing
about the other.

"Observed on both" means something narrower for Windows 2000 than it reads.
Windows 2000 has never run on real hardware in this project: Setup bugchecked
during installation on both machines tried, no cause was investigated and no
bugcheck code was captured, and there is no further candidate. **Do not write
an era wall or any other cause into the record.** Every Windows 2000
observation here is therefore a virtual-machine observation, and for that
target the "observed on both" rule is satisfied by the VM work (the SP4 target
VM, the SMP stress environment, the device matrix), not by metal. Do not write
"validated on both targets" without that qualification, and do not read a
bare-metal Windows 98 result as covering Windows 2000.
`docs/using/release-notes.md` states it under "What this is".

Neither OS has xHCI support. Windows 98 shipped with UHCI/OHCI (USB 1.1) and
got EHCI (USB 2.0) only through later back-ports: the Win2000-derived stack in
NUSB, which is what the project tests against, and SweetLow's XP-derived
rebuild of the same stack that Windows 98 QuickInstall bundles, which the
driver has also been observed running under; Windows 2000 got the Win2000
stack natively in SP4. This driver fills the gap for both.

---

## Quick Reference

| Item | Value |
|---|---|
| Primary targets | Windows 98 SE (4.10.2222) and Windows 2000 SP4 - one binary, both required |
| Secondary targets | 32-bit Windows XP (best-effort) - one binary, accommodate it where the change is small and low-risk, but do not compromise either primary target or add checkpoint waits for it. Its registration packet is statically compatible, but runtime support remains unvalidated; see `docs/usb-xhci-info/win98-wdm.md`, "What about Windows XP?" |
| USB scope | USB 2.0 (HS/FS/LS) only; HID, mass storage, USB Ethernet, and USB Audio validation targets. USB 3.0 SuperSpeed is out of scope (see `docs/usb-xhci-info/xhci-programming.md`, "What SuperSpeed Support Would Require") |
| Integration model | `usbport.sys` miniport (Option A) - reuse the USB 2.0 stack already on the target (NUSB's Win2000-derived build, SP4's native one, or SweetLow's XP-derived rebuild on Windows 98); do not re-implement the USB stack |
| Compiler | MSVC 6.0, run in place from `tools/MSVC600` (unpacked from `tools/MSVC600.zip`) |
| DDK | Windows 2000 DDK, unpacked into `tools/ntddk` (from `tools/WIN2KDDK.EXE`). Both toolchains live in the repo and install nothing machine-wide; every script finds them relative to itself, and `DDKROOT`/`MSVC6` override |
| Language | C (C89/C90 compatible with MSVC 6.0) |
| Driver type | WDM kernel-mode driver (.sys) |
| Hardware spec | xHCI 1.2c. Transcribed in `docs/usb-xhci-info/xhci-data-structures.md`; the PDF itself is fetched per-machine into the git-ignored `docs/references/` (see its README) |
| Test environment | QEMU (primary dev), real xHCI hardware (validation) |

---

## Repository Layout

```
src/            Driver source code (C)
test/           Host-side unit tests for the DDK-free core (test\run-host-tests.cmd)
scripts/        The build wrapper, host setup helpers, and the import/INF/
                packaging gates. `scripts/local/` is git-ignored per-host
                tooling; do not make a committed procedure depend on it.
xhciqual/       Phase 0 DOS hardware-qualification tool (Open Watcom).
                `xhciqual/README.md` and `xhciqual/hardware-testing.md` are
                its guides; `xhciqual/results/` holds the bare-metal run logs
                that roadmap checkpoints cite as evidence. Tracked.
xhcisnap/       The host-side reader for the driver's log channel
                (`XHCISNAP.EXE`); `xhcisnap/README.md` is its guide. Tracked.
docs/           The documentation tree: using/ (release notes, the release
                acceptance test), contributing/ (roadmap, architecture,
                build/test/runbooks, design records, run sheets and their
                evidence), issues/, usb-xhci-info/, and references/.
                `docs/README.md` is the index, and every document below is
                reachable from it.
tools/          The build toolchain itself, used in place and installed
                nowhere else (`MSVC600/`, `ntddk/`), plus the archives they
                were unpacked from, the NUSB 3.3 package, and the
                `*-extracted/` shipping binaries every ABI derivation is read
                from. Git-ignored except `tools/w98se.url.example`, the
                template that tells a clone where to point the DOS harnesses
                at a boot image this repository cannot carry.
external/       Local read-only mirrors of the reference sources (ReactOS,
                Linux, Haiku, FreeBSD). Git-ignored except
                `external/README.md`, which says how to fetch them.
vm/             Guest disk images and transfer disks. The per-run evidence the
                run sheets and `lessons.md` cite by `vm\` path was discarded on
                2026-08-30 once transcribed; such a path names where a reading
                was taken, not a file a clone can open. Git-ignored.
out/            Staged install media from `scripts\package\make-package.ps1`.
                Generated; git-ignored.
releases/       The releases that have been cut, one directory per version.
                `releases/README.md` has the numbering rule and the standing
                rule that a cut directory is never edited afterwards;
                `releases/history.md` records what each version changed.
                Tracked.
.github/        GitHub issue forms (`ISSUE_TEMPLATE/`): what a reporter is
                asked for (OS, machine, controller, build, XHCIQUAL report,
                screenshots). Tracked.
README.md       The repository's front page.
AGENTS.md       This file: the agent guide, and the entry point for the rest.
CLAUDE.md       Claude Code's entry point; it defers to this file.
LICENSE         The license text and its scope note.
```

Everything above is tracked except `tools/`, `external/`, the PDFs in
`docs/references/`, `vm/`, `out/` and `scripts/local/`: third-party material,
generated output, or host-specific tooling. Three files under `tools/` do go
into the release download, though never into git; "Third-Party Material and
Provenance" below names them. Nothing has been uploaded yet: the repository is
private and no GitHub release exists, so that is a decided channel rather than
a used one. A clone therefore has every procedure but not every input; see
`docs/contributing/build-and-test.md` for what has to be fetched or rebuilt.

### Where to start

Read `docs/contributing/roadmap.md` for the current phase and its checkpoint.
**Do not advance past a phase whose checkpoint has not been observed to pass.**
Then use the "What to read for each phase" table in `docs/README.md` for the
documents that phase needs.

Before debugging hardware, firmware, a build, a DOS extender, or otherwise
"impossible" behaviour, read `docs/contributing/lessons.md` first, so prior
evidence is not rediscovered or contradicted.

---

## Architecture Overview

`xhci98.sys` is a `usbport.sys` miniport (Option A), not a standalone HCD. It
plugs in below Microsoft's `usbport.sys` in the same way `usbehci.sys` and
`usbuhci.sys` do, reusing the USB 2.0 stack already on the target: on Windows
98 the Win2000-derived one NUSB ships (or SweetLow's XP-derived rebuild), on
Windows 2000 SP4's own.

```
  [usbhub.sys]  <- OS hub driver (REUSED, unchanged)
       |  (root hub PDO created by usbport.sys)
  [usbport.sys] <- USB port driver: root hub PDO, IOCTL_INTERNAL_USB,
       |           URB parsing, enumeration, bandwidth (REUSED, from NUSB)
       |  (private USBPORT_REGISTRATION_PACKET miniport interface)
  [xhci98.sys]    <- THIS DRIVER: xHCI miniport registered with usbport.sys
       |
  [XHCI PCI Device]
```

`usbport.sys` owns the root hub PDO, all `IOCTL_INTERNAL_USB_*`, URB parsing,
enumeration, and bandwidth. The miniport never parses URBs or handles those
IOCTLs. `xhci98.sys` deals with the xHCI hardware only:

1. Register with `usbport.sys` (`USBPORT_RegisterUSBPortDriver`); respond to start/stop/suspend controller callbacks
2. Controller init: reset, set up event ring, command ring, scratchpad
3. Root-hub callbacks: report port status, handle port power/reset (usbport builds the PDO and hub descriptor)
4. Transfer processing: translate usbport transfer requests into xHCI TRBs, ring doorbells
5. Event processing: miniport ISR and DPC callbacks drain the event ring and complete transfers back to usbport

The `USBPORT_REGISTRATION_PACKET` miniport ABI is undocumented by Microsoft;
ReactOS is the reference, validated against NUSB's shipping `usbport.sys`. See
`docs/contributing/architecture.md` for the full breakdown, the integration
decision, and the Option B monolithic-HCD fallback, and
`docs/usb-xhci-info/win98-wdm.md` for the `IOCTL_INTERNAL_USB_*` and
URB-function lists that describe what `usbport.sys` does for us.

---

## Port Strategy (USB 2.0 vs USB 3.x)

Every standard USB 3.x physical connector carries both USB 2.0 D+/D- wires
and SuperSpeed pairs, and the xHCI controller exposes one logical port per
protocol for it. The rule: manage only USB 2.0 protocol ports, and leave USB
3.x logical ports unpowered and unmanaged, since USB 3.0 is out of scope.
USB 3.x capable devices then fall back to their USB 2.0 path and connect at
High-Speed, so a laptop with only USB 3.x physical connectors still works.

**You cannot negotiate USB 2.0 speed on a USB 3.x logical port.** The SS and
D+/D- paths are electrically unrelated; no register or command converts one
to the other.

See `docs/usb-xhci-info/xhci-programming.md`: "Port Topology Classification"
for the classification algorithm and the companion-pairing convention, and
"What SuperSpeed Support Would Require" for why an all-SuperSpeed controller
is refused (`XHCI_CAPS_NO_MANAGED_PORTS`) without that making any
USB4/Thunderbolt connector unservable.

---

## Build and Target Constraints (Win98 SE + Win2000 SP4)

### Language and arithmetic

- C89/C90 only. No `//` comments, no mid-block declarations, no `stdint.h`. Use `ULONG`/`USHORT`/`UCHAR` from `ntddk.h`.
- No 64-bit DMA. Win98 is 32-bit; the upper 32 bits of all hardware addresses are always 0.
- No 64-bit arithmetic (the `_alldiv`-style compiler helpers may not exist on Win98's kernel). Keep 64-bit hardware fields as Lo/Hi ULONG pairs, Hi always 0.
- No floating point in kernel mode.
- No C bitfields or enums for hardware layouts; use ULONG words plus shift/mask macros. Bit positions and structure layouts come from `docs/usb-xhci-info/xhci-data-structures.md` (transcribed and verified against the local spec PDF), never from memory.

### Imports and the API ceiling

Imports are a silent load-time gate on both targets: any unresolved
module/symbol pair prevents `xhci98.sys` from loading with no call-site
diagnostic, and the yellow bang can look identical to a bad INF. Build with
`scripts\build-driver.cmd`, which runs the import gate after every link, and
never audit imports as a flat union of symbol names: a matching name from the
wrong module does not resolve.

Win98's export set is the API ceiling; Win2000 exports a superset, so coding
to the Win98 baseline satisfies both. The trap runs the other way: an XP-era
API copied in from a sample blocks the load on Win2000. And Win98 forgives
what Win2000 enforces; power/PnP/locking behaviour that "works" on Win98 is
frequently just unexercised there. Never close a phase on a Win98-only
observation.

Use `ExAllocatePool`, never `ExAllocatePoolWithTag`; the import gate denies
both tagged names. This is policy rather than a missing export: Option A
needs no private pool at all, so prefer embedding fixed software metadata in
the usbport-allocated miniport/common-buffer extensions. (The DDK's
`POOL_TAGGING` rewrite of `ExAllocatePool` is undone in the compatibility
header.)

See `docs/usb-xhci-info/win98-wdm.md` ("Imports are a silent load-time gate"
and "Windows 2000 as a co-primary target") and
`docs/contributing/build-and-test.md` ("Post-link import-compatibility gate").

### Build flavours

There are three build flavours and only two are ever published. `release` and
`debug` are the shipping pair: what `make-package.ps1 -Flavor` stages and what
every document here means by "both flavours". `qemu` is `debug` plus the
port-`0xE9` mirror and its `HAL.dll!WRITE_PORT_UCHAR` import, and **must
never be published**: that import is the sole delta between the two binaries
of the development package whose debug build gave the E460 a `Code 2`, and
since why is not established (defect 2b) the safe configuration is the
default and the emulator-only one is opt-in.
`docs/contributing/design/08-build-flavours-and-the-log-channel.md` is the
source.

Two vocabularies cover "all of them", and they are not interchangeable.
`build-driver.cmd`'s `both` means the two shipping flavours and is its
default; `all` means the three and is what a release cut uses. In the import
allowlist's `FLAVORS` column the word is `all`, and `both` is refused. The
DDK's own words, `free` and `checked`, survive only where the DDK itself
requires them; `build-driver.cmd` makes that mapping in one place
(`docs/contributing/build-and-test.md`).

### DMA memory

Ring memory is DMA common-buffer memory supplied by `usbport.sys` under
Option A: physically contiguous, DMA-accessible below 4 GB, and cached, not
uncached (both shipping builds pass `CacheEnabled = TRUE`). Ordering rests on
`volatile` accesses, publishing each TRB's Cycle Bit last, and the
`WRITE_REGISTER_*` accessors, never on an uncached mapping.

The controller common buffer is one fixed, worst-case block, committed in
`DriverEntry` before any register can be read, so the slot and scratchpad
limits are declared policy with an explicit refusal path above them. Objects
a slot owns (device contexts, EP0 transfer rings) must live in that block:
usbport frees the endpoint common buffer and zeroes the miniport endpoint
extension on every `ReopenPipe`, which happens to EP0 mid-enumeration.
`docs/contributing/design/04-controller-common-buffer.md` has the
derivations.

### The INF and install media

The INF is a silent gate too, in both directions. One `src/xhci98.inf`
carries both install paths: undecorated sections with `DevLoader=*NTKERN` for
Win98's 16-bit engine, and `.NTx86` sections with `AddService` for Win2000. A
single-path file does not half-work; Win2000 falls back to the undecorated
section and leaves a devnode whose driver never loads, which reads as a
registration failure. `build-driver.cmd` runs the INF gate on every build for
that reason, including the Win98-parser traps its engine reports as nothing
at all.

The media also carries a per-target `usbd.sys` (`usbd98.sys`/`usbd2k.sys`),
each reachable only from its own install path and never overwriting an
existing file. Do not "simplify" that to one file: the wrong build loads
rather than failing, so the split is checked by hash and by the INF gate's
`TGT-*` rules. Build install media with `scripts\package\make-package.ps1`,
never by hand-copying the `.sys` and `.inf`.

See `docs/contributing/build-and-test.md` for environment setup, QEMU
configuration, the install procedure, the two model INFs, and "Carrying a
per-target `usbd.sys`"; `docs/usb-xhci-info/win98-wdm.md` for the WDM API
compatibility table and the "MSVC 6.0 / C89 Language Pitfalls" list.

---

## Reference Implementations

Split the references by layer. ReactOS (`drivers/usb/usbport`, plus the
`usbehci`/`usbohci` miniports) documents the upward usbport miniport
interface this driver plugs into, including the otherwise undocumented
`USBPORT_REGISTRATION_PACKET`. Linux (`drivers/usb/host/xhci*.c`) documents
the downward xHCI hardware programming: rings, TRB encoding, events,
slot/endpoint lifecycle. Haiku and FreeBSD are occasional second opinions,
with nothing resting on them. `external/README.md` has the per-tree table and
how to fetch the local mirrors.

For the upward interface, start with
`docs/usb-xhci-info/usbport-miniport-interface.md`: it names the exact
ReactOS files and symbols, maps every miniport callback family onto its xHCI
implementation, and gives the procedure for validating the ABI against the
NUSB-installed `usbport.sys` binary. Its bit-exact companion is
`docs/usb-xhci-info/usbport-miniport-abi.md`.

---

## Third-Party Material and Provenance

This project is GPL-2.0-only licensed and depends on material that is not.
The full record is `docs/contributing/legal-provenance.md`; these are the
rules that bind day-to-day work, and none of them is optional.

- **Never track a third-party document or binary.** Not a specification PDF,
  not a driver, not a disassembly listing, not "just this one page". They go
  in a git-ignored directory (`docs/references/`, `external/`, `tools/`), and
  the repository records where to fetch them: filename, version, SHA-256,
  URL, stated licence limit (`docs/references/README.md` is the pattern).
  Public availability of a document is not permission to rehost it.
- The one distribution exception: the GitHub release download carries three
  Microsoft files (`usbd98.sys`, `usbd2k.sys`, `usbhub98.sys`), the target
  OSes' own binaries that make a download a complete install set on xHCI-only
  machines. None is tracked; `legal-provenance.md` section 5 records the
  decision. **Do not extend the exception to a fourth file** without the same
  decision recorded there, and do not write "this project redistributes
  nothing" anywhere. Decided 2026-09-02: the exception is withdrawn before
  any upload, and release 1.0.0.1 has the OS supply both files instead
  (`handoff.md`; `legal-provenance.md` section 5). Until that change lands,
  this rule still binds anyone touching the packaging.
- A fact may be tracked; the artifact it came from may not. Write facts in a
  form re-derivable without the artifact: the address, the instruction, the
  exact command, the page number and a short verbatim phrase. A claim a fresh
  clone cannot check is a claim that will rot.
- Tag every binary-derived fact **static** (read from a disassembly listing;
  nothing executed), **runtime** (observed live through this driver's own
  counters or traces), or **both**, and add the row to `legal-provenance.md`
  section 4 in the same change. Do not upgrade a static reading to a runtime
  one because the system also happens to run.
- ReactOS and the other mirrors are interface documentation, not source.
  Struct layouts, offsets, signatures, constants, and observed call ordering
  are what this project takes; function bodies are never copied into `src/`
  or the docs. This project being GPL-2.0 itself does not relax that rule,
  because the rule is what makes `src/` independently written.
- Defeat no protection mechanism, and patch no binary; read each one as the
  vendor shipped it. Routine unpacking (`7z e` plus `expand.exe` on install
  media) is not this rule's subject; the recorded adjacent cases are in
  `legal-provenance.md` section 1. If a task appears to need more than this,
  stop and raise it rather than deciding it in passing.
- Keep the licensing claims in `README.md` and `LICENSE` true as you go; both
  have been wrong before. And `legal-provenance.md` states facts, never
  verdicts: do not write a legal conclusion into it, into `README.md`, or
  into a commit message, not even a reassuring one.

---

## Coding Style

- No comments explaining what code does; comment only on why, where that is not obvious.
- Prefix exported symbols with `Xhci` (`XhciAddDevice`, `XhciStartDevice`) and internal static functions with `xhci` (`xhciInitRing`). Structures mirroring xHCI spec structures use spec naming (`XHCI_TRB`, `XHCI_SLOT_CONTEXT`).
- Error paths clean up all allocated resources. Use the `goto cleanup` pattern.

Every function's IRQL requirement must be readable at the function (PASSIVE /
DISPATCH / DIRQL), and where a lock discipline exists, whether the caller
holds it. A file-header blanket discharges this for a file whose functions
genuinely share one contract, and most of the core files use one. Two rules
make the blanket form safe: a function that differs from its file's blanket
carries its own tag, and a tag sits adjacent to the function it describes. A
tag that has drifted onto its neighbour is worse than no tag.

Never use `DbgPrint` outside `#if DBG` guards, with one exception, which is a
switch a user sets rather than a trace site: the `XhciLogDebugView` sink in
`src/xhci_dispatch.c` emits the bounded log ring through `DbgPrint` from the
PASSIVE-level flush in every build flavour, so `ntoskrnl.exe!DbgPrint` is an
`all` row in the import allowlist. **Do not widen it to a second call site.**
Per-line printing from DPC and ISR contexts at real interrupt rates is what
bugchecks Windows 98 on bare metal, and a second emit site would have to be
PASSIVE_LEVEL, which a Windows 98 machine running this package never reaches
between `StartController` and shutdown. See
`docs/contributing/build-and-test.md`, "Getting a trace off a bare-metal
machine".

---

## What NOT to Do

- Do not introduce EHCI, OHCI, or UHCI concepts; xHCI is a completely different hardware model.
- Do not assume physical addresses above 4 GB.
- Do not use C++ syntax, STL, or runtime library functions.
- Do not implement USB 3.0 SuperSpeed paths. USB 3.0 is out of scope (the reused USB 2.0-era `usbport.sys` cannot carry SuperSpeed).
- Do not attempt to make a USB 3.x logical port operate at USB 2.0 speeds; it is electrically impossible.
- Do not re-implement the USB stack (root hub PDO, `IOCTL_INTERNAL_USB`, URB parsing, enumeration) under Option A; that is `usbport.sys`'s job. Only do so if the Phase 3 spike forces the Option B fallback.
- Do not invent `usbport.sys` miniport ABI details from memory. Derive them from ReactOS via the procedure in `docs/usb-xhci-info/usbport-miniport-interface.md`, and validate against the NUSB-installed `usbport.sys` binary.
- Do not place a SET_ADDRESS setup packet on a transfer ring. xHCI forbids software-issued SET_ADDRESS (spec section 4.5.4.1: the xHC blocks it and completes the TRB with TRB Error). Intercept usbport's SET_ADDRESS control transfer and emulate it with the Address Device command (see `docs/contributing/architecture.md`, enumeration data flow).
- Do not use undocumented Win98 kernel internals without documenting why there is no other option.
- Do not commit a third-party document, binary, or disassembly listing. Record where to fetch it and what it hashes to instead. See "Third-Party Material and Provenance".
- Do not describe a fact as runtime-observed when it was read out of a disassembly. Tag the method you actually used.
- Do not write a legal conclusion into any file in this repository. `docs/contributing/legal-provenance.md` records facts, not verdicts.
