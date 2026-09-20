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
on both. **"One binary" is a claim about the 32-bit targets**, and it also
covers the VM-supported 32-bit ones below; the 64-bit targets added in
roadmap Phases 21 and 22 are a second build from a second toolchain in a
second package, so no statement about a single binary anywhere in this
repository reaches them (design record 11 section 11).

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

Windows ME is a third target of that same standing, supported in virtual
machines, since the owner's decision of 2026-09-02: one QEMU guest, under
SweetLow's USB 2.0 stack only (its stock USB stack has no `usbport.sys`, so
on a stock machine the driver installs and shows Code 2), the driver
registered and started, and a HID mouse, a mass-storage device and a
composite audio device bound. It carries no checkpoint tax: no phase waits
on a Windows ME observation and "observed on both" does not include it. It
has never run on real hardware. `docs/contributing/build-and-test.md`,
"Windows ME target VM", is the record.

32-bit Windows XP is a fourth target of the same standing, supported in
virtual machines, since the owner's decision of 2026-09-03: one QEMU guest
(XP Professional SP3), on which the `1.0.1.0` package installed on an
xHCI-only install with no prompt, the driver registered and started under
XP's own `usbport.sys`, a HID mouse, a mass-storage device and a composite
audio device bound, and the Device Manager disable, enable, remove and
rescan sequence survived. It reads the INF's `.NTx86` half, and the one
XP-specific defect it showed (issue 4, the hub re-creating a device through
a second device handle mid-enumeration) is fixed in `1.0.1.0`. It carries no
checkpoint tax either, and the standing rule holds: accommodate XP where the
change is small and low-risk, never at a primary target's expense. It has
never run on real hardware. `docs/contributing/build-and-test.md`, "Windows
XP target VM", is the record.

**Windows XP x64 and Windows Server 2003 x64 are a fifth target of the same
standing, supported in virtual machines, and they are the one target that is
not the same binary.** The tier follows roadmap Phase 21 and the owner's
instruction of 2026-09-09 that an ordinary release cut publishes the 64-bit
package alongside the 32-bit one. What was observed is one QEMU guest (XP
Professional x64 SP2, task 21.5, 2026-09-09): the amd64 package installed
through `src/xhci98-amd64.inf`'s `.NTamd64` half on an xHCI-only install with
no prompt, the driver registered with that system's own `usbport.sys`,
started its controller, passed its No Op self-test, answered the root-hub
family, bound a HID mouse, a mass-storage device and a composite audio
device, and survived the Device Manager disable, enable, remove and rescan
sequence - taken on the `qemu` flavour and then read again on the `release`
flavour, on the same guest, the same evening. It carries no checkpoint tax,
and it has never run on real hardware.

Read that tier narrowly, because three things about it are narrower than the
paragraphs above. The binary is a separate amd64 build from a separate
toolchain (WDK 7.1, `WNET`) in a separate package with its own INF, so the
Windows 98 export baseline argument that makes one 32-bit binary safe
everywhere says nothing here; the amd64 import surface has an evidence rule
of its own. Windows XP x64 and Server 2003 x64 are one operating system, NT
5.2.3790, which is why one build serves both - but only XP x64 has been
booted, and Server 2003 x64 rests on that identity rather than on an
observation. And **Vista x64 and Windows 7 x64 are not part of this tier**:
they are the next paragraph's, and what they run is a different registration
path in the same binary. `docs/contributing/build-and-test.md`, "Windows XP
x64 target VM", and `docs/contributing/design/11-x64-targets.md` are the
record; `docs/contributing/legal-provenance.md` sections 3 and 4 carry the
provenance of every amd64 reading behind it, all of them `static`.

**Windows Vista and Windows 7, in both architectures, are a sixth target of
the same standing, supported in virtual machines**, since the owner's
decision of 2026-09-16 (roadmap tasks 21.8 and 22.5). What was observed is
four QEMU guests - Vista Business SP2 and Windows 7 Professional SP1, each
32-bit and x64 - on 2026-09-13 (`docs/issues/07-win7-x86-enable-arrest-usbport-done-dpc.md`
section 7.5): the package installed on a guest with no other USB host
controller, the driver registered and started its controller, passed its No
Op self-test, bound a HID mouse, a mass-storage device and a composite audio
device, and survived five Device Manager disable/enable cycles, a remove and
a rescan, each guest under four virtual processors. Vista x64 then took five
more remove/rescan cycles on 2026-09-16, installed through the committed
`src/xhci98-amd64.inf`. The 32-bit guests run the one binary and the x64
guests the second, both through the `Xhci.Dev6` install path, which copies
`xhci98.sys` alone because every install of those systems already carries the
four OS-supplied files. It carries no checkpoint tax, and the standing rule
holds: accommodate them where the change is small and low-risk, never at a
primary target's expense. Of the four, only 32-bit Windows 7 has run on real
hardware, once (the E460, 2026-09-19, roadmap task 22.9: the install and
devices at a root port and behind USB 2.0 hubs passed, and the first disable
of the controller hung - release notes, "Known limitations"); Vista and 64-bit
Windows 7 never have.

Read that tier narrowly too, because four things about it are narrower than
the XP tiers. **It is not the NT 5.x code path.** No Version 200 miniport can
run on Vista or Windows 7 in either architecture (design record 11, M9 to
M11), so on NT 6.x both binaries present `Version = 300`, pass registration a
fourth argument and return their interrupt DPC through a Version 300 slot,
chosen at run time from `IoIsWdmVersionAvailable(6, 0)` (section 6.5,
decisions 10 and 12). **Every clause was taken on the `qemu` build.** The
`release` flavour was then read on the two x64 guests from the published asset
on 2026-09-18, roadmap task 22.10's sixth and seventh install legs, which were
also Windows 7 x64's first install through the committed INF. Vista
x86 has installed the `qemu` build through the committed `src/xhci98.inf`
(2026-09-17, task 22.5's `InfSection` reading), and Windows 7 x86 installed the
published `release` package through it on 2026-09-18 (task 22.10's ninth leg);
Vista x86 ran the `release` flavour the same night (the eighth leg). **The x64 half loads only
on a boot with driver signature enforcement disabled**: the package is not
signed (design record 11 section 12, decision 9), so the user presses F8 and
chooses Disable Driver Signature Enforcement at every start, the machine can
never boot unattended into working USB, and without it the device sits at
Code 39 with nothing loaded. 32-bit Vista and Windows 7 do not enforce
kernel-mode signing. The release notes carry that requirement beside the
tier, not in a footnote. **And it rests on five cycles a guest**, which is
what issue 7 section 6 says five cycles are: not a proof that the enable
arrest this path was fixed for cannot recur. `docs/contributing/build-and-test.md`,
"Windows Vista and Windows 7 target VMs" and "Vista x64 and Windows 7 x64
target VMs", are the record.

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
| Supported in VM | Windows ME (4.90.3000), under SweetLow's USB 2.0 stack only - observed in one QEMU guest on 2026-09-02, never on metal, no checkpoint tax. Same 16-bit setup engine and undecorated INF half as Windows 98 SE; see `docs/contributing/build-and-test.md`, "Windows ME target VM". 32-bit Windows XP (SP3) - observed in one QEMU guest on 2026-09-03 (xHCI-only package install, HID, mass storage, composite audio, the disable/enable/remove/rescan sequence), never on metal, no checkpoint tax; the `.NTx86` INF half under XP's own `usbport.sys`. Accommodate it where the change is small and low-risk, never at a primary target's expense; see `docs/contributing/build-and-test.md`, "Windows XP target VM", and `docs/usb-xhci-info/win98-wdm.md`, "What about Windows XP?" Windows XP x64 / Server 2003 x64 (NT 5.2.3790) - observed in one QEMU guest on 2026-09-09 (the same clauses, on the `qemu` build and then the `release` flavour; `debug-x64` has never been read in a guest), never on metal, no checkpoint tax; the `.NTamd64` half of the *second* INF, and **a second binary, not this one** - see "Windows XP x64 target VM" and `docs/contributing/design/11-x64-targets.md`. Windows Vista (SP2) and Windows 7 (SP1), 32-bit and x64 - observed in four QEMU guests on 2026-09-13 (the same clauses plus five disable/enable cycles each, on the `qemu` build; the published `release` package was then installed on all four on 2026-09-18, roadmap task 22.10), never on metal except one 32-bit Windows 7 session on the E460 (2026-09-19), no checkpoint tax; the `Xhci.Dev6` install path of both INFs and the Version 300 registration path of both binaries. **The x64 half loads only on an F8 boot with signature enforcement disabled, every boot**; see "Windows Vista and Windows 7 target VMs" and "Vista x64 and Windows 7 x64 target VMs" |
| USB scope | USB 2.0 (HS/FS/LS) only; HID, mass storage, USB Ethernet, and USB Audio validation targets. USB 3.0 SuperSpeed is out of scope (see `docs/usb-xhci-info/xhci-programming.md`, "What SuperSpeed Support Would Require") |
| Integration model | `usbport.sys` miniport (Option A) - reuse the USB 2.0 stack already on the target (NUSB's Win2000-derived build, SP4's native one, or SweetLow's XP-derived rebuild on Windows 98); do not re-implement the USB stack |
| Compiler | MSVC 6.0, run in place from `tools/MSVC600` (unpacked from `tools/MSVC600.zip`). The amd64 build is the exception and cannot be otherwise: it is WDK 7.1's `cl` 15.00 from `tools/WinDDK71`, reached by `build-driver.cmd <flavour> -amd64`, because no compiler here older than that can target x64 |
| DDK | Windows 2000 DDK, unpacked into `tools/ntddk` (from `tools/WIN2KDDK.EXE`), for every 32-bit build; WDK 7.1 in `tools/WinDDK71` (`x64 WNET`) for the amd64 one, whether it stays being design record 11's decision 3. All of them live in the repo and install nothing machine-wide; every script finds them relative to itself. `DDKROOT` overrides where the DDK is found and reaches its `setenv.bat`, so it does redirect the compiler the driver is built with. `MSVC6` does NOT: the DDK build takes its compiler from the generated environment script, and `MSVC6` only redirects the host-side tools that need `dumpbin` and `cl` of their own - the import gate and `scripts\vm-matrix\gen-offsets.ps1` |
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
images/         The pictures `README.md` embeds. Tracked.
docs/           The documentation tree: using/ (release notes, the release
                acceptance test), contributing/ (roadmap, architecture,
                build/test/runbooks, design records, run sheets and their
                evidence), issues/, future-plans/ (proposals not
                scheduled), usb-xhci-info/, and references/.
                `docs/README.md` is the index, and every document below is
                reachable from it.
tools/          The build toolchain itself, used in place and installed
                nowhere else (`MSVC600/`, `ntddk/`), plus the archives they
                were unpacked from, the NUSB 3.3 and 3.6 packages, and the
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
generated output, or host-specific tooling. Nothing under `tools/` goes into
the release download since 1.0.0.1; "Third-Party Material and Provenance"
below has the record. Nothing has been uploaded publicly yet: the repository
is private and no GitHub release exists (which build the third-party testers
in `README.md` ran is not recorded). A clone therefore has every procedure
but not every input; see
`docs/contributing/build-and-test.md` for what has to be fetched or rebuilt.

### Where to start

Read `docs/contributing/roadmap.md` for the current phase and its checkpoint.
The roadmap is two files: that one has the status, the conventions and Phases
0-16 (the initial release), and `docs/contributing/roadmap-phases-17-on.md`
has the entry of every later phase, the open one included.
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

Allocate no pool at all. The import allowlist has no row for
`ExAllocatePool` or for either tagged name, so a call to any of them fails the
import gate as "not in the allowlist", and a row would need Windows 98
evidence that none is intended to supply. This is policy rather than a
missing export: Option A needs no private pool, so fixed software metadata is
embedded in the usbport-allocated miniport/common-buffer extensions. (The
DDK's `POOL_TAGGING` rewrite of `ExAllocatePool` is undone in the
compatibility header so the untagged name is what a stray call would resolve
to and be refused on.)

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
carries both 32-bit install paths: undecorated sections with
`DevLoader=*NTKERN` for Win98's 16-bit engine, and `.NTx86` sections with
`AddService` for Win2000. A single-path file does not half-work; Win2000 falls
back to the undecorated section and leaves a devnode whose driver never loads,
which reads as a registration failure. `build-driver.cmd` runs the INF gate on
every build for that reason, including the Win98-parser traps its engine
reports as nothing at all.

**There are two INFs, and they are two packages rather than one file with a
third path.** `src/xhci98-amd64.inf` is the 64-bit package's, carrying no
Windows 98 path and a `[Manufacturer]` line decorated `NTamd64,NTamd64.6.0`:
NT 5.2 gets `[Xhci.Dev.NTamd64]`, which fetches the OS-supplied files, and
Vista and Windows 7 x64 get `[Xhci.Dev6.NTamd64]`, which copies `xhci98.sys`
alone because their file queue aborts on a `LayoutFile` copy (design record 11
section 12, decision 13; the gate refuses an OS file there as `OS-ONNT6`).
The 32-bit file carries the same NT 6.x path as `[Xhci.Dev6.NTx86]`, through
`%Mfg%=XhciModels,NTx86.6.0` - the one line Windows 98's engine parses, so that
field was read on Windows 98 SE, ME, 2000 and 32-bit XP before it was taken
(roadmap task 22.5), and the gate refuses any other field on that file.
Merging it into the first would mean widening `%Mfg%=XhciModels` to
`%Mfg%=XhciModels,NTx86,NTamd64` - the one line Windows 98's 16-bit engine
parses to find its models section, and whether that engine takes only the
first field is unmeasured here, so the merge costs a re-run of all four
existing install legs to prove nothing broke (design record 11 section 12,
decision 2). The gate takes `-Arch x86` or `-Arch amd64` and `build-driver.cmd`
runs it over both files on every build; the self-tests compare the two
directly, because the accepted cost of two files is that they can drift.
Neither file may grow the other's sections: an undecorated section in the
64-bit file is one a 32-bit engine falls back to, which would put an amd64
binary on a 32-bit machine, and the gate refuses it by name (`PATH-NO9X`,
`PATH-MFGDEC`).

The media carries no Microsoft file. `usbd.sys` and `usbhub.sys` (both
targets), `usbport.sys` (the NT targets; on Windows 98 the USB 2.0 stack
places it) and, since 1.0.2.0, `usbui.dll` (every target, the root hub's
property-page provider) are the OS's own, and nothing on an xHCI-only machine
ever placed them, so the INF has the setup engine copy them from the OS's own
install source through `LayoutFile=layout.inf`, never overwriting a file
already there; on an xHCI-only Windows 98 machine that means the Windows 98 CD
may be asked for, and the NT targets take them from `Driver Cache\i386` with no
prompt - `Driver Cache\amd64` on Windows XP x64, where the same route was
measured to work and to ask for nothing, and where `usbd.sys` and `usbui.dll`
come from `driver.cab` while `usbport.sys` and `usbhub.sys` come from
`sp2.cab`. The three drivers go to dirid 10 (`System32\Drivers`) and `usbui.dll`
alone to dirid 11 (`System32`), which the INF gate holds it to. `usbhub20.sys` is on no path: the OS places it itself. Do not put
any of them on the media: the INF gate's `OS-*` rules refuse an INF that names
one, and `PKG-MSFILE` refuses a staged package holding one - by name, which
covers the four, the three retired 1.0.0.0 media names and `usbhub20.sys`. A
name list cannot see the same bytes under a name nobody thought of; what closes
that is the packager refusing to publish anything it did not itself stage.
`legal-provenance.md` section 5 records why. Build install media with `scripts\package\make-package.ps1`, never by
hand-copying the `.sys` and `.inf`. Its `-Arch` moves three things together
and they are not separable: the obj subdirectory, which of the two INFs is
staged, and the architecture both gates run under. A published release
directory is a flavour AND an architecture - `release-x86`, `debug-x86`,
`release-x64`, `debug-x64` - because both architectures' binaries are called
`xhci98.sys`; `make-release.ps1 -Arch` defaults to **both** since 2026-09-09,
so an ordinary cut publishes four directories. It defaulted to `x86` alone
until roadmap task 21.5 passed - an amd64 binary installing and running
through every checkpoint clause on a Windows XP x64 guest, and then the
release flavour installed and read separately on the same guest, because the
clauses had been taken on the `qemu` build. **The x64 half of a cut carries
exactly that standing: virtual machines only, never real hardware** - the XP
x64 guest, and the Vista x64 and Windows 7 x64 guests from `1.1.0.0` - against
the x86 half's four install legs. `-UploadSetOnly` is the one mode
that does not follow the default - it derives the architectures from the
published tree, since every version published before `1.1.0.0` is x86-only.

See `docs/contributing/build-and-test.md` for environment setup, QEMU
configuration, the install procedure, the two model INFs, and "The files the
OS supplies: `usbport.sys`, `usbd.sys`, `usbhub.sys` and `usbui.dll`";
`docs/usb-xhci-info/win98-wdm.md`
for the WDM API
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
- The release download carries no Microsoft file. From 0.0.0.4 to 1.0.0.0
  the assembled asset carried three (`usbd98.sys`, `usbd2k.sys`,
  `usbhub98.sys`), by a decision `legal-provenance.md` section 5 records, and
  that was withdrawn on 2026-09-02 before any upload: release 1.0.0.1 has the
  OS supply both files through the INF's `LayoutFile`, 1.0.1.0 adds
  `usbport.sys` on the NT path by the same route, and 1.0.2.0 adds `usbui.dll`
  on all four paths by it, to dirid 11 rather than 10. **Do not put a
  Microsoft file back onto the media, under any name, without a decision
  recorded there**, and do not write "this project redistributes nothing"
  anywhere: the two tool executables carry statically linked runtimes
  (section 2a).
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

**Source is ASCII with CRLF endings, and a gate now says so.** Every source
edit in this repository passes through a PowerShell string layer, because the
Bash tool mangles quotes - and in a **double-quoted** PowerShell string the
backtick is the escape character, so `` `a `` becomes a literal BEL. That
happened on 2026-09-12: a comment written into `src/xhci_slot.c` carried a BEL
into the tree, and all three x86 flavours compiled and passed every gate with
it there. Use **single-quoted** here-strings for anything containing a
backtick. `scripts\check-source-charset.ps1`, which `build-driver.cmd` runs,
refuses a control byte below 0x20 other than TAB, CR and LF anywhere in
tracked source, and a UTF-8 BOM at the head of any file the 1998-era toolchain
reads, and **any byte >= 0x80** anywhere - a rule taken on 2026-09-12 at
the cost of one character, a UTF-8 section sign in an `src/xhci.h` comment
rewritten as "section" (roadmap task 22.7). Above all, never
put a non-ASCII byte in a **string literal**: the driver's strings go out the
debugcon channel and onto a Windows 98 console, where the encoding is not
UTF-8.

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
