# Run 45 - Release `3.4.0.0`

Phase 45 of `roadmap-usb4.md`, for release `3.4.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

## 45.20 USB4 version 2 links: the design half (2026-10-29)

Design record 22 section 6.2 rewritten so a USB4 version 2 router is run
as a version 2 one, with the sections it touches (the record's revision
note of 2026-10-29 lists them). Reading only: no code, no build, no
guest; development host A, 16:05 to 17:00.

### What was read

- The USB4 Specification 2.0 and the Connection Manager Guide 2.0 of the
  November 2025 bundle (`docs/references/README.md`, the hashes there),
  as text extracted per machine into the worker's scratch folder, never
  tracked. Spec: 1 (the Ver. 1 and Ver. 2 terms), 4.2.2 (link
  configurations; Gen 4 has no single-lane form), 4.2.2.5, 4.4.7 (Gen 4
  Link Recovery), 5.3.3.1 (the 1.5 Gb/s note), 6.6 (the Ver. 2
  acknowledgement list and the Sequence bit note), 6.7, 7.3.1.3 (the
  Enhanced Uni-Directional note), 8.2.1.1 (ROUTER_CS_4 CMUV and USB4
  Version; the enumeration rules; ROUTER_CS_5 after CV), 8.2.2.2 (the
  enhanced-mode values), 8.2.2.3 (Table 8-12: Supported Link Speeds and
  Widths, Gen 4 Asymmetric Support, Target Link Width and Target
  Asymmetric Link, Current Link Speed, Negotiated Link Width), 8.2.2.4
  (PORT_CS_18 and 19), 8.2.2.7 (Extended Encapsulation), 8.2.3.4 (path
  writes of both doublewords), 8.3.1 (ROP_CMPLT), 8.3.1.3.2 and
  8.3.1.3.3 (Tables 8-49 to 8-55), 11.1.1.1 (the Extended Encapsulation
  note), Table 12-14. Guide: 3.2.1 (CMUV by router version, Router
  Ready, Get and Set Capabilities, the Gen T DROM entry), 3.2.3, 5.3.1
  step 4, 5.3.2 step 3, 6.1.2.1 (Table 6-2), 6.2.4, 7 (the port
  configurations), 7.1, 7.2, 7.4.3 with Tables 7-2 and 7-3, 7.6, 7.7.1,
  9.1.
- Linux `drivers/thunderbolt` at the mirror's pinned commit, for facts
  only (its files are GPL-2.0): `switch.c` `tb_switch_configure` (CMUV
  by version), `tb_switch_lane_bonding_enable`, `tb_switch_set_link_width`,
  `tb_port_update_credits`; `tb.c` `tb_configure_asym` and
  `asym_threshold` (asymmetric links for DisplayPort bandwidth only),
  `tb_enable_tmu`, `tb_start` (the Host Router Reset on a version 2
  host); `tunnel.c` `tb_pci_set_ext_encapsulation` (EE only when both
  routers are version 2 and the link is Gen 4), `TB_PCI_WEIGHT`,
  `TB_USB3_WEIGHT`, `USB4_V2_PCI_MIN_BANDWIDTH`; `tmu.c`
  `tb_switch_tmu_enhanced_is_supported`; `tb_regs.h` (the TMU_ADP_CS_8
  and 9, LANE_ADP_CS_1 and PORT_CS_18 and 19 layouts as a cross-check).
  Linux issues no Get or Set Capabilities (`enum usb4_switch_op`).
- This tree's `src/cm/tbt_u4cm.c`, `tbt_ring.c` and `tbt_regs.h` as they
  stand at `3e314cde`: CMUV 1.0 written to every USB4 router
  (`tbtU4Enumerate`); Get Capabilities only (`tbtU4GetCapabilities`);
  PCIE_WAKE, DP_CON_CHANGE and DPTX_DISCOVERY already acknowledged
  (`TbtCtlClassify`); the teardown's invalidation a single-dword write
  of PATH_CS_0 (`tbtU4Invalidate` and the preserved-path one beside it);
  a link bonded only when Negotiated Link Width reads x2 (`tbtU4Bond`,
  the adoption's lane step, `tbtU4LanesAsLeft`), so an asymmetric Gen 4
  link would go to a bonding attempt and then FAIL, and a preserved one
  be held; the USB3 allocation's link bound already counts Gen 4 at 40
  per lane; the TMU-Off predicate already reads the version 2 TMU fields
  per router.

### What the design fixes

Section 6.2's four parts: the run-as version per router (CMUV 2.0 to a
major 2 router, a preserved router's CMUV read and kept, the bench
switch `XhciCmUsb4V2`); the Ver. 2 obligations item by item (polled
readiness, the acknowledgement list, the Sequence bit Set Capabilities
with a read-back, two-dword path writes, ROUTER_CS_5 untouched after CV,
the HiFi Enhanced Uni-Directional TMU, Extended Encapsulation, the
bandwidth floor, Gen T out); Gen 4 and asymmetric links (classified from
Current Link Speed and Negotiated Link Width, never bonded, an
asymmetric link never initiated and used as found, Gen 4 Link Recovery
never enabled, the raw bandwidth of Table 6-2); and the code and the
host vectors that follow. Decisions U30 to U32 are new, the owner's to
confirm: U30 version 2 by default with the bench switch `XhciCmUsb4V2`,
U31 asymmetric links never initiated and used as found, U32 a new PCIe
tunnel beside a preserved DisplayPort tunnel built without its 1.5 Gb/s
floor verified.

Reviewed and revised over five rounds the same day. What the rounds
changed: the version 2 obligations keyed on the routers' own version and
the capabilities they report, not on a predecessor's CMUV; the Sequence
bit Set Capabilities and the rule against reconfiguring a preserved
router's CV on every USB4 router; the enhanced TMU on every link of two
version 2 routers whatever the tunnels, with DirSwitchN and the
replenish values on both ports; a predecessor's Gen 4 Link Recovery
enable cleared on every preserved link, a LINK_RECOVERY queueing the
existing port check, and what a recovery leaves unverified recorded as a
limitation; the bandwidth floor beside a preserved DisplayPort tunnel
made decision U32. The last round left only U32, which is the owner's.

The host vectors were not written: each one tests behaviour the code
task adds, and the fake fabric (`test/u4_fakefab.c`) needs a version 2
router first (CMUV, Set Capabilities, Current Link Speed Gen 4 and the
asymmetric widths, ADP_PCIE_CS_1, the enhanced TMU fields), so they land
with the code.

### What is left

The code, the host vectors in `test/test_u4cm.c`, `usb4-emu`'s version 2
router model and the q35 legs, and the bench reading (45-E.10). 45.20
stays unticked.

## 45.22 ring interrupts for the connection manager: the design (2026-10-29)

A design only, no code and no guest run (the owner, 2026-10-29: 45.22 is
design only this phase, and whether it is coded is the owner's). Written
into design record 21 section 4.5 (both profiles) and record 22 section
5.4 (what the USB4 profile adds), reviewed and revised until the review
found nothing material beyond the residual risks the record names; the
task is not ticked.

**What was read** (files only, nothing run):

- The tree: `src\cm\cm_thr.c` (`cmThread`: the one timed
  `KeWaitForSingleObject` on `WorkEvent` per pass, `CmThrQueue` under
  `WorkLock`), `cm.h` (the `CM_WORK_*` bits), `cm_live.h` (ABSENT at three
  all-ones samples, one per pass), `tbt_regs.h` and `tbt_hw.h` (the mask
  registers `0x38200` and `0x38208`, `TBT_DESC_IE`,
  `TBT_HW_INTEL_INTERRUPTS`), records 05 section 4, 15 sections 4.2 and
  4.3, 18 section 15, 21 sections 4.3 and 4.4, 22 sections 4.4, 5.3, 7.1
  and 10.3, 28; `scripts\import-gate\xhcicm-imports.allow`, its amd64
  twin and `xhci98-imports.allow` (the five interrupt pairs and their
  `uhcd.sys` evidence; `MmGetSystemRoutineAddress` and
  `KeAcquireInterruptSpinLock` on the x86 deny lists).
- The USB4 Specification 2.0 (November 2025), sections 12.5 (INTx
  optional, MSI or MSI-X required; one MSI vector for all causes; ITR[0]
  for INTx and MSI), 12.6.3's register map (ISR `37800h`, ISC `37808h`,
  ISS `37810h`, IMR `38200h`, IMC `38208h`, IMS `38210h`, ITR `38C00h`)
  and Table 12-14 (Disable ISR Auto-Clear, bit 17 of the Host Interface
  Control register).
- Linux `drivers/thunderbolt` (GPL-2.0-only, facts only): `nhi_regs.h`
  (the status block fires new interrupts only after every status dword is
  read), `nhi.c` (`ring_interrupt_index`, `nhi_mask_interrupt`,
  `ring_interrupt_active`, `nhi_interrupt_work`), `pci.c`
  (`nhi_pci_check_quirks`, `nhi_pci_init_msi`: MSI-X, else one MSI, never
  INTx).
- `usb4-emu` (read, not run): `ARCHITECTURE.md` section 5, `HARNESS.md`
  (its `x-intx-drop-*` is the NVMe endpoint's, not the NHI's),
  `ICM-PROTOCOL.md` section 2.5 and R6/R7, `src/qemu/hw/usb4/usb4-host.c`
  (ISS, IMS, IMC and ISC implemented; INTx level while any unmasked status
  bit is set; `intx`, `msi` and `msix` properties).
- `runs/run-41.md`, "Resources": on i440FX, 98 SE gave the
  `tb3-alpine-ridge` NHI "IRQ 11 (level, shared)". Nothing in the tree
  reads the NHI's IRQ on q35, nor the ASM4242 model's on any machine.
- Branch `w45-read` (commit `1765b18c`): its first draft of record 21
  section 4.5 and record 22 section 5.4, built on here. Its points
  revised: every status dword read, not one; the service routine never
  declines a cause of its own, the "armed" flag deciding only the DPC;
  the timed wait kept at today's period (the liveness sample, the port
  scan and the Stor98 presence anchor ride it), not lengthened to a
  watchdog of 1000 ms; MSI's `MmGetSystemRoutineAddress` admitted on
  amd64 only (x86 deny list), x86 through `xhcihlpr.sys`; the DPC drain at
  the stop; the NHI lost-interrupt leg owed to a `usb4-emu` addition.
  Revised again after review: an interrupt wake drains RX ring 0 only
  (the scan's responses would otherwise wake the thread forever); a storm
  masked at DIRQL; disconnection of a source proven quiet (mask read
  back, or for a line PCI Interrupt Disable), an unproven one retained in
  D0 and disconnected unproven only at a stop, a removal, a surprise
  removal or a D3 whose emergency mask also failed, the residual windows
  named in record 21 section 4.5.7; the descriptors' Interrupt Enable
  decided before the ring start; the wait computed from deadlines; an
  activation-long failure latch; the image kept resident once an
  interrupt was connected; MSI keeps the whole status read; the x86 MSI
  step's `IoGetDeviceObjectPointer`.

**Owed before any 98 SE or ME connection** (record 21 section 4.5.4): on
the q35 bases with `-Usb4 tb3ar` and `-Usb4 asm4242`, each of 98 SE and
ME, the NHI devnode's allocated configuration after the start (IRQ or
none), its Interrupt Line and Pin and the IRQ's sharers, by `XHCISNAP`
level 2 or Device Manager's Resources page.

**The q35 readings that would set the default** (record 21 section
4.5.9): hot-plug latency on the host's clock, from the QMP plug to the
emulator's trace of the RX consumer index passing that plug's own event
frame (identified by type, route, adapter and ring slot), and to the
connection manager's `connected` line, ten plugs each, interrupt off and
on; idle cost over ten minutes with nothing attached and with an
idle drive (NT: `% Processor Time`, `Interrupts/sec`; 98 SE and ME: the
QEMU process's CPU time), and polling at 50 ms beside them.

## 45.3 a faster cut build (2026-10-29)

The owner, 2026-10-28, after the `3.3.0.0` cut (was BUILDFAST.1): that
cut built amd64 and then x86, about 67 and 55 minutes, one after the
other. The design is `build-and-test.md`, "A faster gated build"
(reviewed and revised); the code is `5a0aa420` (reviewed and revised).
Development host A, no virtual machine; eight other workers' builds and
guests were running on the host throughout, so every time here is a
loaded host's, and the readings that are compared ran at the same time.

**Verdict: the cut's two builds at once took 1:00:12 against 1:27:38
for the same two builds of the base commit beside them, 27:26 saved
(31 %); the INF gate's own self-tests, 56 to 57 minutes alone, are now
the whole of what is left; the second tree was made by the launcher;
and at the base commit no gated build with `TEMP` under a worktree
could pass, which the same commit fixes.**

### What changed

- `scripts\build-steps.ps1` (new) runs `build-driver.cmd`'s checks that
  need no DDK as separate processes, at most `XHCI98_BUILD_STEPS` at once
  (default 4), each with its own `TEMP` and log under `out\steps-<arch>\`,
  the five longest first; each step's output is printed whole when it
  ends, then a table of every step's result and time.
- `build-driver.cmd` hands it that block, after its text checks, and
  prints `--- <step>: m:ss` after every other step and the whole run.
- `cut-builds.ps1`'s `-X64Tree` is optional: tree B is then `<A>-x64`,
  made by the script and brought to A's HEAD before each cut.
- **The measured problem (coordinator, 2026-10-29), and the way out
  taken.** The packager's self-tests read `src\pfw\obj\XHCIPFW.VXD` and
  `src\pfw\nt\objfre\i386\xhcipfw.sys` before the build rebuilt them: a
  stale image failed the case that runs the packager with its binary
  gates on ("no stamp 'XHCIPFW.VXD 3.3.0.0'", and the same for
  `XHCIPFW.SYS`); a missing one was passed over (a fresh tree's run,
  below, failed neither). Of building them first or taking the
  self-tests off them, the second was taken: a stand-in caller of
  `make-package.ps1` (one that names `-DriverPath`) stages
  `XHCIPFW.VXD`, `XHCIPFW.SYS` and `S98VSD.VXD` only when it names them
  (`-PfwVxdPath`, `-PfwSysPath`, `-VsdPath`), so no self-test reads an
  image an earlier build left, on either architecture's run; a cut, which
  names none, stages the tree's images as before. Building them first
  would still have left an amd64 run, which builds neither, reading
  whatever x86 images its tree held.
- **A second problem found on the way.** The packager's self-test "a
  directory that is no git checkout is not read as a missing tag" (new on
  the base commit's day, `2f57cad2`) made its directory under `TEMP`; with
  `TEMP` under a worktree's `out\`, as `cut-builds.ps1` sets it and the
  phase's workers were told to, `git -C` found the enclosing worktree and
  the check failed, and with it the build. `Invoke-DriverPackGit` now
  gives git `GIT_CEILING_DIRECTORIES`, the parent of the checkout it was
  given.

### The readings

| Step | Commit | Result | Time |
|---|---|---|---|
| `test-package.ps1` alone, fresh worktree, no XHCIPFW image, `TEMP` under its `out\` | `70bd2e0e` | 1 of 1679 checks failed: the not-a-repo case, `'...\out\tmp-pkgtest\...\not-a-repo-...' has no tag v9.0.0`; no PFW case failed | 20:23 |
| `cut-builds.ps1 -X64Tree <B>` (x86 in tree A', amd64 in B, `TEMP` under each tree's `out\`; `XHCIPFW` built first in A') | `70bd2e0e` | both exit 1 at the packager's self-tests, the same not-a-repo case (1 of 1683 and 1 of 1679 checks) | 1:13:41 to the failure; the INF gate's self-tests from about 15:05 to 15:52, the packager's 15:52 to 16:17 |
| Before, at once by hand: `build-driver.cmd all` in A' and `all -amd64` in a second tree, `TEMP` outside both | `70bd2e0e` | both exit 0; packager 1683 checks, INF gate self-tests 1555 | x86 1:27:38, amd64 1:25:42; the INF gate's self-tests 57:10, the packager's about 24 minutes, one after the other |
| After, beside the line above: `cut-builds.ps1` with no `-X64Tree` in this tree | `5a0aa420` | tree B `E:\xhci98-wt\w45-tools-x64` made, its 28 `tools\` directories linked; both exit 0; release, debug and qemu amd64 records imported | x86 1:00:12, amd64 0:59:01, both 1:00:12; import 0:01 |

The after run's own log, x86 (`out\cut-build-x86.log`; amd64 within a
few seconds of it):

| Step | Time |
|---|---|
| text checks | 0:12 |
| the checks that need no DDK, at most 4 at once | 56:47 |
| - of which the INF gate's self-tests | 56:24 |
| - the packager's self-tests (1700 checks) | 27:14 |
| - the host suite | 0:52 |
| - every other step | 0:25 or less each |
| Stor98, `S98VSD.VXD`, `XHCIPFW.VXD`, `XHCIPFW.SYS` | 0:06, 0:03, 0:04, 0:08 |
| debug, release, qemu, each built and gated | 0:56, 0:56, 0:54 |
| the whole run | 60:11 |

The amd64 run's flavours took 0:37 to 0:42 each. So the DDK builds are
a few minutes of a cut's hour: `scripts\inf-gate\test-inf-checks.ps1`
(one `powershell.exe` per case) is the next place a cut's time can be
taken back from.

The runner's own self-test (`build-steps.ps1 -SelfTest`): 44 checks
passed. The imported amd64 release record, SHA-256: `xhci98.sys`
`B5C70434...`, `xhciuas.sys` `61E8B6D5...`, `xhcicm.sys` `B16B150E...`,
`stor98.sys` `245A6265...`.

### Not read

- `-Serial` and a single build alone on a quiet host: every reading here
  ran beside other work, and the before and after pairs beside each
  other.
- The signed half of a cut from the imported build, which only the key
  can run (as 44.31).

## 45.4 the USB Tree's window title carries its version (2026-10-29)

The owner, 2026-10-28, after the `3.3.0.0` cut (was TRTITLE.1): the main
window's title says which build it came from, so a screenshot or a report
does.

### The change

`bab16e60` (reviewed and revised). `xhcitr\tr_core.h` defines
`TR_MAIN_TITLE` as `"xHCI98 USB Tree "` followed by `src\xhci_version.h`'s
`XHCI_VER_STR`, and `xhcitr.c` creates the main window with it: `xHCI98
USB Tree 3.3.0.0` on this branch, `xHCI98 USB Tree 3.4.0.0` once the
version is bumped for the cut. The main window only: the message boxes,
the tray tooltip and menu and the Run value keep the plain name, and
nothing finds the main window by its title (the installer and the
single-instance check find the tray window by its class). Design record
25 section 3.1 says so.

### The readings

| Step | Result |
|---|---|
| `test_tr`, compiled alone with MSVC 6.0 under `/Za` | 1577 checks, 0 failures; the new `test_main_title` holds the title against `XHCI_VER_CSV` with its commas read as dots, and against `XHCI_VER_STR` |
| The gated x86 build of 45.3's after run (`5a0aa420`): `xhcitr\build.cmd`, the host suite, `xhcitr\selftest.cmd` and `snaptest.cmd` | XHCITR.EXE and XHCITR64.EXE built; `test_tr: 1577 checks, 0 failures`; "XHCITR.EXE: imports the seven allowed modules alone", "OK: xhcitr self-tests passed"; XHCITR.EXE SHA-256 `2F762CED...` |

### Not read

- The window leg on 98 SE and 7: this worker had no virtual processors,
  so no guest was started. Owed: XHCITR from this build started on a
  98 SE and a Windows 7 guest, and a screendump of the main window with
  the title `xHCI98 USB Tree <version>`.

## 45.11 the kept kernel child: the prototype read under Driver Verifier (2026-10-29 and 30)

The readings behind record 31 section 4.9, the design put to the owner;
nothing here is decided. Record 31 section 9.3 items 11 and 12. Runtime,
development host A, QEMU under TCG, 2026-10-29 15:11 to 19:45 and
2026-10-30. The prototype `w45-kept-proto` up to `434665c8`, built with
task 45.31's changes for 7 (below).

### The vehicle

- The prototype: branch `w45-kept-proto` (never shipped, never merged),
  commits `f372c4ac`, `0b559009`, `30ead9fa`, `3cb28ab8` and `434665c8`
  on the base `70bd2e0e`; reviewed and revised up to `3cb28ab8`. On NT
  6.x a child the bridge's `BusRelations` attached does a logical removal
  at `IRP_MN_REMOVE_DEVICE` while its PDO stays (it stays attached and on
  the attachment list, its departure recorded, its snapshot cleared), is
  re-armed at its next `IRP_MN_START_DEVICE`, and is destroyed when its
  PDO is deleted, through the driver's `FastIoDetachDevice` callback
  (`cmFastIoDetach`, `CmFilterDetachCb`); one detach owner for the
  callback and every removal (`CmDetachOnce`); a reference across every
  other PnP request of a kept child; the links of a kept child to a
  departing filter dropped. The tunnel id is given on every NT system,
  8.1 included. `XhciCmKeptProto` 0 (DWORD, machine-wide `Services\USB`)
  turns the kept child off for the control leg. The `qemu` flavour,
  `xhcicm.sys` alone built (no gates: a test build), hashes `9a09615e...`
  (`3cb28ab8`) and `489cfc0d...` (`434665c8`); `cm.kept.*` records on the
  debug port.
- The package: the `3.3.0.0` release tree (`releases\3.3.0.0`), its
  `release-x86\stor98.inf` changed for the test alone: the
  `[Stor98_Models.NTx86.6.3]` line matches `PCI\XHCI98_TUNNEL&CC_010802`
  as the undecorated section does (`0285665c...`). Installed with
  `INSTALL.BAT /console /yes /nopause /thunderbolt /stor98` (exit 3), the
  prototype copied over `System32\drivers\xhcicm.sys` before the
  restart, Driver Verifier `verifier /standard /driver xhcicm.sys`.
- The guests: q35 bases `w81x86` and `w8x86`, fresh overlays, 2 vCPUs,
  2 GB; `launch-q35.ps1 -Usb4 asm4242 -EncAtBoot` (the ASM4242 model, the
  ORICO `asm2464` enclosure on USB4 port 1 at power-on: tunnel DSP,
  enclosure USP, DSP, NVMe, the drive at depth 5). Kits and logs:
  `E:\xhci98-vm\p45\w45-kept\k81a`, `k8a`.

### A defect found first: `cmQNt` under a spin lock (fixed, `3967a033`)

The first boot of 8.1 with the enclosure stopped with bug check 0xC4
(0x2000E, 0x8536C164, 0, 0): Driver Verifier's DDI compliance check
caught `IoIsWdmVersionAvailable` called by `CmQueryPublish` with the
query device's spin lock held (the thread's stack: `cmThread` ->
`CmQueryPublish` -> the folded `cmQNt`; read from the minidump's stack
against a map of the same objects). The same call stands in the PCI
answer under the lock. Not the prototype's; in every release with the
query device. Fixed on `w45-kept` (`3967a033`, reviewed): the answer is
read once in `CmQueryInit`, at `DriverEntry`.

### 8.1 x86, the kept child on (Verifier on `xhcicm.sys`)

| Step | Result |
|---|---|
| First boot, the drive tagged | `stornvme` first (rank `0xFF2007`); the mark service's 8.1 arm installed the id model (event 400 `oem3.inf`, rank `0x80FF2000`, Matching Device Id `PCI\XHCI98_TUNNEL&CC_010802`, `stornvme` outranked), two logical removals, the id given again at each restart; Stor98 running, `E:` ORICO; no second event 400 |
| Disable and enable (SetupAPI) | the id kept, Stor98 running, no event 400 |
| Stor98 installed again over itself (`DIF_INSTALLDEVICE`, its restart) | the same |
| The DSP above the drive disabled and enabled | the drive's PDO deleted with its parent's stack: `FastIoDetachDevice` called for the kept child at `PASSIVE_LEVEL`, outside any dispatch of its own, and it was destroyed; the DSP's kept chain child stayed, a new child was attached to the new PDO, the id given, Stor98 bound |
| Surprise unplug (QMP `device_del`) with a write loop and a file held open on `E:` | each kept child logically removed, then its callback, bottom-up (NVMe, DSP, USP); where the PDO went inside its own removal, the callback came inside the forwarded removal and the removal finished the destruction; a registered child (`LowerFilters`) above a kept child was called back when the kept child was deleted; no bug check |
| Hot re-plug | tagged again, Stor98 bound (the q35 Code 31 of 44.45 not met here) |
| Eject (`INSTALL /console /eject id=...`) | after the logical removal the kept child saw `IRP_MN_QUERY_RESOURCES`, `IRP_MN_EJECT`, `IRP_MN_QUERY_DEVICE_RELATIONS` and a second `IRP_MN_REMOVE_DEVICE`, the PDO deleted inside it; the departure recorded, `cm.reenum.eject`; the installer said "has not confirmed that the PCIe tunnel was closed", as it did with the kept child off |
| Cold boot | the id given, Stor98 running |
| Uninstall (`INSTALL /console /uninstall /yes`) | every kept child destroyed through its callback, the bridge filters' callbacks inside their removals, the driver unloaded; then bug check 0xC4 (0x62, ..., 1): one pool allocation of `xhcicm.sys` not freed at its unload, the link-state table (read on 7, below; task 45.31) |

### 8.1 x86, the kept child off (the control, the same overlay)

After a cold boot the drive ran Stor98 by the id; a disable and enable
lost the id at the enable, and 8.1 configured `stornvme` again: event
400, rank `0xFF2006`, Matching Device Id `PCI\CC_010802`, "Device
Updated: true". So a drive already on Stor98 leaves it at a later
restart of its stack on 8.1 (record 31 section 4.8's unread case), and
the kept child is what holds it there. Item 12's reading (why 8.1
chooses again): at a cold boot with the kept child back on, the drive
then on `stornvme` gained the id in its recorded ids and no event 400
followed (`stornvme` kept); 8.1 chose again only when the configured
driver's matching id had left the device's ids. Read as an observation
of 8.1's behaviour, not as its rule.

### Seen on the way (not the kept child's)

After `stornvme` had run the drive, Stor98 failed to start on it on 8.1
(problem 10, Stor98's error log entry 4 "internal error in its data
structures"), at an install and at a cold boot, with the device's
`Interrupt Management\MessageSignaledInterruptProperties` value
`MSISupported` 1 that `stornvme.inf` writes; with it set to 0 Stor98
started. Earlier boots of the same overlay with the value present
started Stor98, so the condition is not read whole.

### 8 x86 (2026-10-29, the same vehicle, overlay `k8a`)

The prototype `489cfc0d` with Verifier, the enclosure at boot: the drive
tagged and bound to Stor98 by Windows (event 400 `oem3.inf`). Disable and
enable: the id kept, Stor98. The DSP above the drive disabled and
enabled: the drive's child called back at `PASSIVE_LEVEL` and destroyed,
the DSP's child kept, a new child with the id, Stor98. Surprise unplug
with the write loop and a file held open: the callbacks bottom-up, no bug
check; a hot re-plug tagged and bound again. Eject: the same request
sequence as on 8.1, and the installer said "The PCIe tunnel was closed."
Uninstall: every kept child destroyed, then the same bug check 0xC4
(0x62, 0x8A43D0F4, 0x8B66F3C8, 1) at the unload.

### 7 x86 (2026-10-29 and 30, overlay `k7a`)

- The prototype `489cfc0d`, its first boot with the enclosure: bug check
  0xC4 (0x62) during the boot. The control, `3.3.0.0`'s release
  `xhcicm.sys` copied over it with Verifier still on: bug check 0xA
  (0x827E9FB7, 2, 8, 0x827E9FB7), execution of `ntkrnlpa.exe+1DAFB7` at
  `DISPATCH_LEVEL`, called from `xhcicm.sys` `+36B1E`, which the release
  image's disassembly shows as `cmQNt` calling `IoIsWdmVersionAvailable`
  from `CmQueryPublish` under the query device's lock: the defect fixed
  in `3967a033`, a page fault on 7 where the routine is pageable. Read
  from the two minidumps (exported over COM1, parsed on the host).
- The prototype with `3967a033` and `1ecdd85a` (`7a1d85dd...`): bug check
  0xC4 (0x62) again. The debug port: `cm.nvlink.queries` 1, then the NHI's
  removal (`cm.stop` 2): the boot's device installs (the NHI's driver
  installed at that boot, "You must restart") removed every device of the
  driver after Stor98 had taken the link-state table, the driver unloaded,
  and the table, which `stor98cm.h` interface 2 never frees once handed
  out, was what Verifier found. So the 0x62 of 8.1, 8 and 7 is that
  table, not the prototype's.
- The prototype with task 45.31's second change as well (`95420bf2...`:
  the driver stays loaded once a stor98 holds the table): the same boot
  went through the removal and a new start with no stop. The matrix:
  disable and enable, the id kept, Stor98; the DSP restart, the drive's
  child above ACPI's bus filter (`cm.reenum.child.under` "ACPI", decision
  2), called back when ACPI's filter went with the PDO, destroyed, a new
  child with the id, Stor98; surprise unplug with the write loop and a
  file held open, callbacks bottom-up, no stop; re-plug, Stor98; eject,
  "The PCIe tunnel was closed."; uninstall exit 3 with every kept child
  destroyed and no stop, the restart after it clean.

### Owed

8.1 x64; another topology; a parent's removal with its children already
disabled; the update from `3.3.0.0` on 8.1 (record 31 section 4.9 item
10); 2000, XP, Vista and 10 for option (A2); the owner's decision on
record 31 section 4.9.

## 45.29 the readme and the release history in the shape of the GitHub release notes (2026-10-29)

The owner, 2026-10-29: the download's `readme.txt` and
`releases/history.md` were both too long-winded. The code is `34172250`,
`d207bcfb` and `636e997e` (reviewed and revised). Development host A, no
virtual machine.

### What changed

- **`releases/history.md`.** Every entry, `1.0.0.0` to `3.3.0.0`, is the
  text the owner published with that release on GitHub (New, Changed,
  Fixed, Installing/Updating, Known limitations, the downloads), under
  its old heading and date. The text was made plain ASCII: two published
  notes held three and nine characters outside it, a dash and arrows. A
  markdown link artefact is plain text: `[wdmaudio.cat](http://wdmaudio.cat/)`
  is `wdmaudio.cat`, and a link is its text with the address after it.
  Three edits (the owner, 2026-10-29): `1.0.0.1`'s "(tested in QEMU
  only)" and `1.0.1.0`'s "Tested in virtual machines only." are left out,
  since no system is labelled so; `1.0.0.1`'s second address for
  SweetLow's stack, a mirror, is left out; and `3.3.0.0`'s Known
  limitations keep the Windows 2000 `CriticalDeviceDatabase\pci#cc_010802`
  one the published text lacks. The detailed record of each change stays
  in the roadmap and the run sheets.
- **The readme**, from `3.4.0.0` on (`make-release.ps1`'s template): the
  version's own entry (`Get-HistoryEntryLines`), a short install,
  the files Windows supplies and 98 SE's storage, how to report a problem
  (logging level 2, Save Report, Save Snapshot, `XHCIINST.LOG`, and the
  DOS report only if a maintainer asks), the directory's files and the
  earlier releases on one line (`Get-HistoryEarlierText`), with the full
  release notes linked for the rest (each system's standing, the
  registry settings, every file). XHCIQUAL is no longer a first step: it
  is in the folder list and in the report section. The three texts each
  download's readme replaces (`{DOWNLOAD}`, the other drivers' lines,
  `{MSFILES}`) are each there once, as before, so downloads 2 and 3 are
  still rendered from download 1's.
- `ConvertFrom-MarkdownBlocks` renders bullets two levels deeper (the
  published notes nest them). `check-readme-text.ps1` renders every entry
  as the cut renders the version's own, and its self-tests cover the
  entry, the earlier-release line and the nesting. `releases\3.3.0.0` and
  every earlier cut folder are unchanged.

### The readings

| Step | Result |
|---|---|
| `check-text.ps1` and `-SelfTest` (`636e997e`) | OK; "readme text self-test: OK", "text check self-test: OK" |
| The readme rendered for `3.3.0.0` from the template, as the cut renders it (a stand-in for the folder list) | 393 lines, none past 78 columns (the published `3.3.0.0` readme: 3,331) |
| `test-package.ps1` at `34172250` | 3 of 1697 checks failed: three source checks naming the old install and missing-tool lines, corrected in `d207bcfb` |
| `test-package.ps1` at `0581fbe0` | passed, 1698 checks (the not-a-repo and git cases among them) |

### Not read

- `make-release.ps1 -CheckOnly` and a dry cut: this worktree has no
  XHCIQUAL build and no checkout of the driver packs, so the check stops
  before it renders the readme. The `3.4.0.0` cut renders it.

## 45.30 the INF gate's self-tests, in process and skipped when unchanged (2026-10-29)

The owner, 2026-10-29: `scripts\inf-gate\test-inf-checks.ps1`, 56 of a
gated build's 60 minutes in 45.3's readings, made fast both ways the
owner chose. The design is `build-and-test.md`, "The INF gate's
self-tests, in process and skipped when unchanged" (reviewed and
revised); the code is `4da95db8` and `0581fbe0` (reviewed and revised).
Development host A, loaded by other work throughout.

**Verdict: in process the full run took 40:16 against 47:33 for the old
suite beside it (15 % less); unchanged, it skips in under a second; a cut
always runs it in full. Each case's gate run itself, about 7 seconds,
is now nearly all of what is left.**

### What changed

- Each case runs `check-inf.ps1` in the self-test's process
  (`Invoke-GateHere`): a script scope of its own per call, the `-Name
  value` words made a named splat, the global `$LASTEXITCODE` reset and
  read, an error leaving the gate read as exit 1 with what it printed
  before. `check-inf.ps1` is unchanged for its other callers. The case
  about a relative `-ManifestPath` after `Set-Location` stays a process.
- A digest of every file it and the gate read (all of `scripts\inf-gate\`,
  `common.ps1`, `test-harness.ps1`, every file directly in `src\`,
  XHCITR's manifest, and the PowerShell version), held against
  `out\inf-selftest-pass.txt`, which a passing run writes whole (digest,
  tree, time, count, an end line) only if the digest at its end is the one
  it began with. `-Force` and `XHCI98_FULL_SELFTESTS=1` run it all;
  `build-driver.cmd all` and `cut-builds.ps1` set the second, so a cut and
  any `all` build run it in full, and `both` or one flavour takes the skip.
- A self-test that needs git fails at once without it: the packager's
  (its fixtures are git repositories), and the cut launcher's two reads
  of git's exit code no longer take an earlier program's.

### The readings

| Step | Commit | Result | Time |
|---|---|---|---|
| The old suite, base tree | `70bd2e0e` | passed, 1555 checks | 47:33 (18:02:12 to 18:49:45) |
| In process, beside it | before `4da95db8` (the named binding in; the strict record, the per-run temporary name and the kept output not yet) | passed, 1568 checks | 40:16 (18:05:51 to 18:46:07) |
| In process, `-Force` | `0581fbe0` | passed, 1569 checks; record written | 46:26 (19:15:27 to 20:01:53, beside a full packager run) |
| Unchanged, the next run | `0581fbe0` | skipped: "nothing they or the gate read has changed since the run that passed at 2026-10-29T12:01:53Z (1569 checks ...)" | 0.6 s |
| One gate run in process, alone | - | about 7 s; the costliest statements: the required-values loop over each route's AddReg sections (`check-inf.ps1` near line 1870, 2.4 s) and the OS-supplied files loop (near line 2494, 1.9 s) | - |
| `test-package.ps1` with no git on `PATH` | `636e997e` | stopped at "the driver pack" with "The term 'git' is not recognized", exit 1; given that exit, `build-steps.ps1` marks the step failed and returns 1, and `build-driver.cmd` stops | - |

A build reported (2026-10-29) to have ended with exit 0 after this
self-test failed without git could not be reproduced: no log of it was
kept, and the readings above stop the build. The check now names git
before anything else runs.

### Not read

- The other gates' self-tests (`check-uas-inf.ps1`, `check-cm-inf.ps1`,
  `check-stor98-inf.ps1`, `test-flavour-rules.ps1`) take seconds and start
  no process per case; they were left as they were.
- A faster gate itself: the two loops above are where its own time goes.

## 45.24 the hardware wizard hanging on 32-bit XP while it installs `uac2` (2026-10-29)

A user's report: with `3.2.0.0`, whose free set is `usb-wdm-free` v0.1.0,
the wizard stopped responding while it installed the free set's `uac2`
for a USB Audio 2.0 device on 32-bit XP, and the device worked after a
restart. The device, its port and the reporter's `setupapi.log` were not
at hand; the owner's reading of the report is that `uac2` went in through
Device Manager's Update Driver (Have Disk at `USBDRV\FREE\audio\uac2\w2k-xp`).
Runtime readings, development host A, 15:00 to 17:10.

**Verdict: reproduced with `3.2.0.0` and `3.3.0.0` alike, and with no
xHCI98 on the machine at all.** On XP Windows' own `usbaudio.sys` takes a
UAC 2.0 function first, by `wdma_usb.inf`'s bare `USB\CLASS_01`, even when
`uac2wdm.inf` was copied in before the plug; from then on no change of
that devnode completes - Update Driver to `uac2`, Disable, the update
after an unplug, the shutdown after an unplug - until a reset, after
which `uac2` binds and plays. `uac2wdm.sys` never loads before that. The
fix in this driver is design record 13 section 10.7.2: a function whose
triple is `01/ss/20` or `01/ss/30` no longer offers the bare
`USB\Class_01`, so under xHCI98 `usbaudio` no longer takes it through that
generic line (`wdma_usb.inf`'s few VID/PID lines still match the devices
they name) and `uac2` is installed with nothing to tear down. Under Windows' own USB stack the
generic id stays; that is `usb-wdm-free`'s known issue.

### The vehicle

- Guests: q35 XP SP3 x86, overlays of `E:\xhci98-vm\q35-bases\xp\disk.qcow2`
  (`64ada103...`), stock QEMU 11.1.0 (`qemu-system-x86_64.exe`
  `47d57a60...`, it carries `usb-redir`, which the q35 bases' own emulator
  build does not), TCG, 2 vCPUs, 1 GB, `qemu-xhci` behind a root port, or
  `ich9-usb-ehci1` at `00:05.0` for the controls. (An EHCI function at
  `00:1d.7` with no function 0 beside it is not seen by XP's PCI scan.)
- The device: `usb-wdm-free`'s simulator `uacemu.py` (`3b85819a...`,
  branch `w45-uac2`), profile `hs24` (High Speed, one programmable
  clock, 2-channel playback and capture, an IAD `01/00/20`), id
  `FFFF:0201`, over `usb-redir` on `xh.0` port 1 or `ehci.0` port 1.
- Packages: `xhci98-3.2.0.0-free.zip` (`486df50b...`; `xhci98.sys` x86
  `dd230d50...`, `uac2wdm.sys` `ec4fcce5...`, `DriverVer`
  10/24/2026,0.1.0.0) and `xhci98-3.3.0.0-free.zip` (`81e6f760...`;
  `xhci98.sys` `8ff8d0d2...`, `uac2wdm.sys` `0aafc635...`, 0.1.4.0),
  installed by `INSTALL.BAT /console /yes /nopause /wdmfree:copy`
  (`/wdmfree:stage=audio/uac2` on `x1`'s second run), the
  unsigned-driver prompts answered Continue Anyway.
- Evidence: `E:\xhci98-vm\p45\w45-uac2\` - `shots\*.png` (QMP
  screendumps), `ev\*.jsonl` (the simulator's request log), `pids.txt`,
  `STATE.md`.

### The legs

| Leg | Host controller driver, package | Steps | Result |
|---|---|---|---|
| `a1` | xHCI98 `3.2.0.0`, `uac2` copied, not staged on that run (`s1` staged it) | plug; Found New Hardware for "xHCI98 USB Composite Device" (Continue Anyway, Finish); then Device Manager, "USB Audio Device", Update Driver, Have Disk `C:\XHCI98\USBDRV\FREE\audio\uac2\w2k-xp`, Continue Anyway | the function came up silently as "USB Audio Device", no bang (`a1-dm2`); the update stopped at "uac2wdm.sys To C:\WINDOWS\system32\DRIVERS" for 20 minutes (`u10`-`u12`); `sc query`: `usbaudio` RUNNING, `uac2wdm` STOPPED, exit 1077 (`u13`); no request reached the device after the enumeration |
| `c1` | Windows' own `usbehci.sys` and `usbccgp.sys`, no xHCI98, `uac2` from the transfer disk | the same update | the same wait (`c1-u3`), `usbaudio` RUNNING and `uac2wdm` never started (`c1-u4`) |
| `d1` | xHCI98 `3.2.0.0` | plug, then Disable the "USB Audio Device" | Device Manager waited, 12 minutes (`d1-dm3`, `d1-dm4`, `d1-final`) |
| `s1` | xHCI98 `3.2.0.0`, the installer run again with `uac2` staged ("free:audio/uac2 staged") | restart; plug | still "USB Audio Device" on `usbaudio`: the signed generic line outranks the unsigned specific ones (`s1-w2`) |
| `s1` | | unplug; Update Driver on the non-present devnode, `uac2` from the list | the same wait (`s1-h10`, `s1-h11`); `shutdown -r` then stopped at "End Program - mmc.exe" and, after End Now, at "Windows is shutting down" for 5 minutes (`s1-r1` to `s1-r3`): reset |
| `s1` | | after the reset, plug | `uac2wdm` RUNNING, the clock's RANGE requests at the device, `sndrec32 /play` of `tada.wav`: 3 s of 2-channel 16-bit 48 kHz OUT data, non-zero, with feedback (`s1-r8`, `ev\s1c-hs24.jsonl`) - the reporter's "it worked after a restart" |
| `v1` | xHCI98 `3.3.0.0` (`uac2` 0.1.4.0) | as `a1` | the same wait (`v1-u2`, `v1-u3`): not gone in `3.3.0.0` |
| `w1` | xHCI98 `3.3.0.0` | plug; unplug; `shutdown -r` | stopped at "Windows is shutting down", 6 minutes (`w1-r1`, `w1-r2`): reset |
| `w1` | | after the reset, the device unplugged: Device Manager with `devmgr_show_nonpresent_devices=1`, Show hidden devices, the hidden "USB Audio Device", Update Driver, Have Disk `uac2`, Continue Anyway; then plug | the update completed in about 3 minutes (`w1-h5`, `w1-h6`); on the plug `uac2` bound and `tada.wav` played (`ev\w1b-hs24.jsonl`) - the way out for a devnode already on `usbaudio` |
| `c2` | Windows' own EHCI, no xHCI98 | plug; unplug; `shutdown -r` | the same wait at "Windows is shutting down" (`c2-r1`) |

### Where the wizard waits

In the old driver's teardown: `uac2wdm` is never started, `usbaudio`
stays running, and the same wait comes with Disable, with an update after
an unplug, and with Windows' own shutdown after an unplug, under Windows'
own USB stack as under xHCI98. Which component waits - `usbaudio.sys`,
KS, PortCls or the PnP manager - was not read (no thread's stack was
taken); this bus is not needed for it, and `uac2` never runs before it.
On XP the signed `wdma_usb.inf` outranks the unsigned `uac2wdm.inf`, so
every plug of a new UAC 2.0 device there meets it; the user's "Found New
Hardware" wizard is the Hardware Update Wizard of the same install path,
or the first device after a staging that lost to `usbaudio`.

### The fix

Design record 13 section 10.7.2 (reviewed and revised): `XhciFuncCompatCount`
in `xhci_func.c`, taken by `XhciFuncId` and `hcdDeviceQueryId`; host
vectors in `test_func`. Commit `590b67e2`. No registry switch for it
(owner, 2026-10-29).

Builds at `590b67e2`: the x86 gated build `all` passed (host tests passed,
the gates and the packager self-tests clean; `out\gated-x86-release.txt`,
`xhci98.sys` release `a3de04a5...`); the amd64 gated build `all` passed too (`out\gated-amd64-release.txt`, its tree
dirty only by this sheet; the x64 images unsigned, as a clone without the
key stages them).

### The fix leg (`f1`, 19:02 to 19:30)

A fresh overlay of the XP base (nothing installed), stock QEMU as above,
this tree's release x86 package (`make-package.ps1 -Flavor release -Arch
x86`, `xhci98.sys` `a3de04a5...`) installed from the controller's Found New
Hardware wizard (Have Disk), then the root hub's; `uac2` 0.1.4.0 from
`3.3.0.0`'s free download on the transfer disk.

| Step | Result |
|---|---|
| plug `hs24` on `xh.0` port 1 | "xHCI98 USB Composite Device" (Continue Anyway, Finish), then a second Found New Hardware wizard for "UAC2 hs24 - SIMULATED" (`f1-p2`, `f1-p3`): no "USB Audio Device", `usbaudio` not even installed as a service (`sc query usbaudio`: 1060) |
| that wizard, Install from a list, Have Disk `E:\UAC2` | "Hardware Type" page first (no compatible driver on the machine), then "UAC2 USB audio (experiment)" offered alone (`f1-p6`); Continue Anyway; "Completing the Found New Hardware Wizard" within a minute (`f1-p7`): no hang |
| `sc query uac2wdm`; `sndrec32 /play /close tada.wav` | `uac2wdm` RUNNING; the first `sndrec32`, started in the same breath as the wizard closed, said no sound device; the second played: IF1 alt 1, 2 channels 16-bit 48 kHz, about 3 s of OUT data with signal and feedback (`ev\f1-hs24.jsonl`) |
| the control: QEMU's `usb-audio` (USB Audio 1.0, protocol 00) on port 2 | its composite wizard, then Windows' own driver installed by itself: `usbaudio` RUNNING (`f1-c4`) - the bare `USB\Class_01` still offered to a 1.0 function |

**PASS.** Under this build a new UAC 2.0 device on 32-bit XP that no
`wdma_usb.inf` VID/PID line names (the simulated `FFFF:0201` here) gets a
Found New Hardware wizard instead of Windows' USB Audio 1.0 driver, and `uac2`
installs from it with nothing to tear down. A machine that already holds
a "USB Audio Device" devnode for such a device keeps it; the way out is
the `w1` row above (record 13 section 10.7.2, the release notes).

## 45.12 the requirement lists of hot-added bridges on 2000 and XP: the code, and the readings R1 and R5 before it (2026-10-29)

Design record 33 coded: the pure rewriter `src/cm/cm_breq.c` and its
host suite `test\test_cm_breq.c` (the templates on the lists captured in
44.63 and 44.64, one change of every compared byte refused, the worked
examples of section 6.5 byte for byte on both systems, the rules' edges,
section 6's arithmetic bounds, the BAR walk, the diagnostic and a seeded
sweep of the invariants); the bridge case of the child filter's
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS` in `cm_filt.c` (NT 5.x alone, the
lowest of this driver's filters in the stack, below an active tunnel
port) with its snapshot read from configuration space in `cm_reenum.c`;
the registered child's role by position (`CmRolePlace`, after the NHI's
answer) and ancestry (`CmRegAttLink`'s second search over the child
filters' snapshots); the bridge inventory in the marks answer
(`cm_query.h` `CM_QB_*`); the mark service's bridge marks, the restart
and its trigger and the other restarts' wait (`xhcimark\`, its pure half
in `mark_core.c` with the new host suite `test\test_mark.c`);
`XhciCmBridgeReq` (read on NT 5.x alone) and `XhciCmBridgeReqDiag`. Not
made: the installer's "Advanced..." row for `XhciCmBridgeReq` (record 33
section 9.1).

**The readings before the commit (record 33 section 10.2, R1 and R5).**
Runtime, development host A, QEMU under TCG, 15:38 to 16:40; at most
three virtual processors. Emulator `asm4242-20261027-boardint`
(`qemu-system-x86_64.exe` SHA-256 `deca3fcb...`) with 44.63's ASM4242
layout (the root port `bus-reserve=103,mem-reserve=776M,io-reserve=24K,
pref32-reserve=4M,hotplug=off`; `usb4-asm-usp` `fw-bus-pad=48,
fw-io-pad=12K,fw-mem-pad=384M`; both tunnel ports
`power-controller=off`). The guests: fresh overlays of the q35 XP and
2000 bases (2000 on its Standard PC HAL), `3.3.0.0`'s release package
installed (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`; on
2000 twice, the first run's documented Code 12 restart between), then
this tree's test build put in its place by hand: the `qemu` flavour
`xhcicm.sys` (SHA-256 `67d46177...`, port `0xE9` log) and `XHCIMARK.EXE`
(`73628344...`, then `f0913e22...` for the R5b legs), the golden disk
shut down, and each leg a child overlay of it. The dock: `usb4-device
...,dialect=usb4,dock=on,drive=nvme0` on USB4 port 1 by QMP `device_add`
(the TI XIO3130 switch `104C:8232`/`8233` with the NVMe, an e1000e and a
qemu-xhci; for R5b also `usb-drive=` a USB disk on the dock's xHCI).
Evidence `E:\xhci98-vm\p45\w45-dock\` (`xp\`, `w2k\`: the port `0xE9`
logs `debugcon-r5a.log` and `-r5b.log`, the serial dumps and their
decodes `D*-r5a.txt`, `D*-r5b.txt`, `infopci-r5a.txt`).

| Leg | System | Result |
|---|---|---|
| R1 and R5a: a never-seen dock hot-plugged, the service running | XP | The upstream port's list rewritten at its first filter IRP with its secondary bus already numbered (its `18h` `00040403`), three downstream ports counted: I/O 12, 8, 4 KB; memory 384 MB halving to 2 MB, then 1 MB; prefetchable 2 MB, 1 MB (example 1). Each downstream port's list rewritten with every sibling's bus numbered at its IRP (`18h` `00050504`, `00101004`, `001B1B04`): the NVMe's and the xHCI's ports 128 MB of memory alone (I/O and prefetchable dropped), the NIC's 4 KB of I/O and 128 MB. Every start after a rewrite succeeded (`XhciCmBridgeReqDiag`: 8 rewrites, 8 starts, 0 failed). `info pci`: the upstream port the tunnel port's whole windows (I/O `1000h-3FFFh`, memory `CE000000h-E5FFFFFFh`, prefetchable `CD200000h-CD3FFFFFh`); each downstream port 128 MB; the NVMe's and the xHCI's ports started with their I/O and prefetchable windows closed, the NIC's with I/O `3000h-3FFFh`. All three downstream ports Code 0 (44.63: two at problem 12). The bridges installed silently; the service wrote the three bridge marks after their installs (16:00:42 to 16:00:53), waited while the dock's functions' installs were pending (Found New Hardware), then made one restart (16:10:17), with the NVMe's install set to run again first; after it the NVMe came back with the tunnel id at depth 3 below the registered children, Windows' wizard installed Stor98, and the drive mounted (`D:` ORICO); the dock's xHCI on xHCI98 with its USB tablet; the NIC at problem 28 (no XP driver, as 44.45). `LowerFilters` `xhcicm` on the upstream port and all three downstream ports; `CriticalDeviceDatabase\pci#cc_0604` clean (the service took out a copy the unit mark's install had made, see below) |
| R1 and R5a | 2000 | The same rewrites with 2000's template (shape 2) and the same windows in `info pci`; every start succeeded; the bridge marks written at 16:01:16; one restart at 16:14:43 after the wizards; the NVMe on Stor98 mounted (`E:` ORICO) on the Standard PC HAL |
| R5b: the service stopped at the plug, started after the bridges' installs | XP | With no bridge marks, the bridges' installs took the children away: the second and third downstream ports at problem 12, the first placed with no filter (the NVMe untagged, no driver). At its start the service wrote the three bridge marks and logged the trigger: "04:00.0 runs with no filter of xhcicm below it", "04:01.0 has no resources (problem 12)", "04:02.0 has no resources (problem 12)"; one restart (16:31:07); after it every port Code 0 and the NVMe on Stor98 |
| R5b | 2000 | Every port placed by the rewrite at the first start, then each run with no filter after its install (the trigger: all three "run with no filter"); the USB disk on the dock's xHCI started: the restart refused, "a disk or volume below the unit is in use. Unplug the dock safely and plug it in again", and the unit mark's own restart refused the same way. A re-plug (QMP `device_del`, `device_add`): the marks of the departed dock removed at the service's next poll ("the unit left"), the kernel chain on every bridge at the new plug, the lists rewritten again, the marks written again with no restart, the NVMe arriving with the tunnel id (Windows' wizard offering Stor98) |

**What the readings settle.** R1: at each bridge's filter IRP its own
bus numbers and, for a downstream port, its siblings' are already
written (record 33 section 2.4's [inf], read on XP and 2000), and a
bridge found closed starts from a list with its I/O group removed, its
I/O window left closed. R5: the bridge marks are written after the
bridges' silent installs; the restart's trigger reads both the problem 12
and the placed-without-attachment cases; the restart waits for pending
installs and is refused with a started disk below; a re-plug places the
dock with no restart.

**Changed by the readings.** The unit's own mark was written while the
unit's install was pending, and Windows copied it into
`CriticalDeviceDatabase\pci#cc_0604` (the service's scrub took it out at
its next poll, 16:00:40): where the bridge marks follow a unit that is a
bridge, its own mark now waits for its install as the bridge marks do
(record 33 section 4.3, "the unit itself when it is a bridge"). The
service's "not made yet" line was written at every poll while installs
were pending; it is now written once a reason, and what asked for the
restart once an arrival.

**The legs on the gated build (record 33 section 10.2).** Runtime,
development host A, 19:08 to 22:21, at most five virtual processors. The
gated x86 build of `98c22600` (debug, release and `qemu` flavours, gates
and host suites passed: `test_cm_breq` 17130 checks, `test_mark` 149);
the amd64 release compiler refused the zero plan's declaration (C4132),
initialised in `4d4aacd0`, and the gated amd64 build then passed; both
release packages made (x86 `xhcicm.sys` SHA-256 `587b7e96...`,
`XHCIMARK.EXE` `3ca901f3...`). Guests: fresh overlays of the q35 XP,
2000 (Standard PC HAL) and 7 x86 bases with this release installed from
the package (`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`),
the same emulator and layout as above; the XP legs R7d to R7g below are
child overlays of the R5a leg's disk with the test build. Evidence
`E:\xhci98-vm\p45\w45-dock\` (`xp2\`, `w2k2\`, `w7\`, `xp\`: `D*.txt` and
`W*.txt` dumps, `report-*.txt` XHCITR reports, screenshots).

| Leg | System | Result |
|---|---|---|
| R3: the dock hot-plugged on a fresh install | XP | All three downstream ports Code 0; the NVMe on Stor98 (`D:` ORICO), the dock's xHCI on xHCI98 with its USB tablet, the NIC at problem 28 (no XP driver). `XhciCmBridgeReqDiag`: 8 rewrites (the first starts and the restart's), 8 starts, 0 failed. `LowerFilters` `xhcicm` on the upstream port and the three downstream ports. Seen: with the unit's mark now waiting for the unit's install, the downstream ports went to problem 12 after the upstream port's install rebuilt their stacks, and stayed there until the restart for the bridges (19:53:34), made once the Found New Hardware wizards had been answered: per the record (the restart waits for pending installs), but an interval the user sees |
| R3 | 2000 | All ports placed; one restart (19:43:08) after the wizards; the NVMe on Stor98 (`E:` ORICO). Diagnostic: 7 rewrites, the upstream port's second list (after the restart) in the configured shape and left (shape 0, as section 7.4 says of 2000); `LowerFilters` on the four bridges |
| R8: 7 x86 | 7 | 7 placed the dock itself: every function Code 0, the NIC on its inbox driver, the NVMe on Stor98. No `XhciCmBridgeReqDiag`, no bridge marks, the unit's mark alone. `FilteredConfigVector` differs from `BasicConfigVector` on all four bridges: the basic list asks 1 MB of memory and of prefetchable memory and 4 KB of I/O, the filtered one 4 MB preferred with 1 MB alternatives and an I/O alternative of length 0. That is 7's own filtering (the rewrite does not act on NT 6, and no diagnostic entry was posted), so record 33's R8 expectation "filtered equal to basic" is restated as "the lists as 7 makes them" |
| The switch, `XhciCmBridgeReq` 0 | XP | Outcome "off" on all four bridges (no location recorded), no rewrite, the second and third downstream ports at problem 12 as `3.3.0.0`, the unit's mark alone, no bridge mark |
| The switch, 2 | XP | Outcome "record" on all four with the computed sizes (the upstream port 384 MB; each downstream port 21 MB), nothing replaced: the second and third downstream ports at problem 12, the first with the NVMe placed (`D:` ORICO); no bridge mark |
| R6: a cold start with the dock attached | XP | Every port Code 0, the NVMe on Stor98 (`D:` ORICO); each bridge's `FilteredConfigVector` equal to its `BasicConfigVector`, in the configured shape (`148h` and `E8h` bytes, not the closed list's `268h`); no rewrite attempt recorded in that start; the bridge marks and the unit's mark written at the service's first poll, no restart |
| R7: unplug and plug in again, the dock with a USB disk on its xHCI | XP | Three times (a plug after the switch's legs, a re-plug 3 s after the unplug, one 90 s after): at each unplug the service removed all four marks within a second ("the unit left"), so the case of section 7.5 with the bridge marks still in place is not reached by an unplug the service sees; at each plug the kernel chain alone rewrote the four lists in the closed shape (4 more rewrites each time, 12 in all, every start succeeded), the marks were written again with no restart, every port Code 0, `D:` ORICO back |
| R7, the same | 2000 | Twice (3 s and 90 s): the same, 2000's template each time (shape 2), the marks written again with no restart, `E:` ORICO back |
| R7 with the dock's USB tablet | XP, 2000 | **Fails: Windows' removal of the dock stalls.** On XP the unplug's departure is read (`cm.reenum.departure`), but the surprise removal never reaches the switch's own filters (no `cm.reenum.unexpected`, no `cm.chain.remove`): the dock's devices stay listed Code 0, a re-plug is not enumerated (no `BusRelations` reaches the tunnel port after the tunnel comes up again), and the shutdown does not finish. 2000 shows "Unsafe Removal of Device" and then the same: the re-plug not enumerated. Read on the gated release (XP and 2000) and on the test build (XP, legs R7d, R7e with no drive, R7g). Seen: with `XhciCmBridgeReq` 2 (R7f: the dock's xHCI and NIC never start) the unplug is removed normally; with switch 1 and a USB disk in the tablet's place (R7g) it is removed normally (`cm.reenum.unexpected`, seven `cm.chain.remove`); the tablet again in the same guest stalls. The cause is not established. No earlier release started the dock's xHCI on XP (its downstream port stayed at problem 12); owed to a task of its own |
| R9: the single enclosure (ORICO model) hot-plugged | XP | Its two bridges' lists not matched by a template (outcome "shape", five attempts), left; the drive on Stor98 (`D:` ORICO). The service wrote the downstream port's bridge mark and asked for the restart ("runs with no filter of xhcicm below it"), refused: "a disk or volume below the unit is in use. Unplug the dock safely and plug it in again" (the XP refusal with a started volume below) |
| R9, the same | 2000 | Lists left (outcomes "left" and "shape"); the drive on Stor98 (`E:` ORICO); the bridge mark written, no restart asked |

**Not read in this run (owed).** R4 (XP x64: the amd64 package is made;
its closed list is not read, so its template is not added), 2000 on the
ACPI HAL, R6 on 2000, R7's replug with the bridge marks still in place
on XP and 2000 (not reached: the service removed them within a second of
each unplug), R8's single enclosure on 7 x86 and R8 on 7 x64, R9 at a
cold start, R10 (the chain, after 45.16), R11 (standby and hibernation),
R12 (the uninstall and the update over `3.3.0.0`), R13 (98 SE and ME).

## 45.34 a device with several configurations configured and split (2026-10-29)

Owner, 2026-10-29, from `usb-wdm-free`'s USB Audio 3.0 reading
(`docs/design/uac2.md` section 16.6): a USB Audio 3.0 device carries an
ADC 1.0 or 2.0 function in its configuration index 0 (ADC 3.0 section
3.3) and its 3.0 function later; under `3.4.0.0` before this task the bus
read configuration 0 only, selected none and left the device one unsplit
PDO with no driver. Design record 13 section 10.8 (reviewed and revised):
a device with `bNumConfigurations` above 1 is split by configuration
index 0 under the unchanged rule, and a function's SELECT_CONFIGURATION
naming another `bConfigurationValue` is refused. Commit `3df23249`
(`XhciFuncSplit`, `hcdCfgSelectFunction`, `test_func` vectors). Runtime
readings, development host A.

**Builds at `3df23249`:** the x86 and amd64 gated builds `all` passed,
host tests passed (`out\gated-x86-release.txt` and
`out\gated-amd64-release.txt`, tree clean); release x86 `xhci98.sys`
`0098b4a3...`.

**The device:** `usb-wdm-free`'s `uacemu.py` profile `uac3dual`
(`FFFF:0230`, `bNumConfigurations` 2: configuration 1 at index 0 is a UAC
2.0 function, IAD `01/00/20`; configuration 2 a UAC 3.0 function by its
standard descriptors, protocol 30h), over `usb-redir`; `uac2` from
`usb-wdm-free` `815957c` (`uac2wdm.sys` x86 `85b490ad...`).

| Leg | System | Result |
|---|---|---|
| `m1` | XP SP3 x86, fresh q35 overlay, this build installed from the controller's Found New Hardware wizard | plug: the device got SET_CONFIGURATION 1 (the simulator's log), "xHCI98 USB Composite Device", then a Found New Hardware wizard for the function "UAC2 uac3dual - SIMULATED"; Have Disk `uac2`; `uac2wdm` RUNNING; `sndrec32` played through it (950 packets, signal). Configuration 2 never selected, protocol 30h never shown. Before this task (`usb-wdm-free` 16.6) the same device had no driver |
| `m1` | the same guest, regression | a single-configuration UAC 2.0 device (`hs24`) on another port: composite parent and function as before, `uac2` taken from the installed INF without a prompt |
| `p98` | 98 SE, the `pc`-machine pristine base (`E:\xhci98-vm\p43-5\base\w98se-pristine.qcow2`) with `qemu-xhci`, stock QEMU 11.1.0 (`usb-redir`); this build installed from the controller's Add New Hardware wizard | plug: SET_CONFIGURATION 1, the function's wizard "UAC2 uac3dual - SIMULATED", `uac2` from `w98se-me` (the KS file prompts skipped, as `usb-wdm-free`'s 98 SE legs do), `sndrec32` played (15348 packets, signal) |

The q35 98 SE base did not boot under stock QEMU (a hang in SMM, then,
with `smm=off`, "While initializing device IOS: Error: An I/O subsystem
driver failed to load"), and the q35 bases' own emulator has no
`usb-redir`, so the 98 SE leg ran on the `pc` base. ME, 2000 and the
x64 systems were not read; the change is the same pure split on every
target (record 13 section 10.12).

Evidence: `E:\xhci98-vm\p45\w45-uac2\` (`shots\m1-*`, `p98-*`;
`ev\m1-uac3.jsonl`, `m1-hs24.jsonl`, `p98-uac3.jsonl`).

## 45.33 98 SE crawling once xHCI98 is installed, the system drive in MS-DOS compatibility mode (2026-10-29)

A report on a desktop board with six xHCI controllers, under CSM:
Windows 98 SE, whose system drive is an internal NVMe drive it reads
through the BIOS ("Drive C is using MS-DOS compatibility mode file
system" on the Performance tab, before and after), was usable before
xHCI98 and very slow after it, "as if polling", also without the
Thunderbolt 3 / USB4 support. 98 SE's own IRQ list put five of the six
xHCI controllers on lines shared with NVMe drives: IRQ 15 with three xHCI
controllers, two NVMe drives (the system drive one of them), an audio
function and chipset bridges; IRQ 11 with two xHCI controllers, a third
NVMe drive and a parallel port; IRQ 14 with the sixth xHCI controller;
IRQ 4 with the USB4 NHI.
Readings in QEMU on development host A, 22:09 to 23:41; the owner's on the
board, relayed the same evening.

**Verdict: not an xHCI98 defect, and narrowed to documentation (owner,
2026-10-29, 23:30).** A device on the shared line holds its
level-triggered interrupt asserted with no Windows driver to acknowledge
it. Windows 98 keeps such a line masked while no driver uses it, so the
holder costs nothing; once any driver connects to the line, Windows
unmasks it and the holder's interrupt storms. xHCI98's ISR rightly
declines each entry. On the board the holder is the BIOS-driven system
NVMe drive [inf: the owner's readings below point to it; not read at the
register]. The way round it, `XhciInterruptMode` `2`, was read to leave
the line masked with USB working; it is now in `README.md`'s known
issues and `docs/using/tuning.md`. No driver change in `3.4.0.0`.

### The owner's readings on the board

In the order they came, each relayed by the coordinator:

1. With xHCI98 not installed and a third-party Windows 98 NVMe driver
   on two of the NVMe drives instead: just as slow; the system drive
   still in compatibility mode.
2. That driver then on all three NVMe drives: still slow; the system
   drive's own instance at Code 10, so the BIOS still runs that drive.
3. The audio card taken out: still slow - not the holder.
4. The one other driver on IRQ 15 (the third-party NVMe driver's
   instance on the second NVMe drive there) disabled in Device Manager,
   with xHCI98 not installed: **responsive again**. So IRQ 15 storms as
   soon as any driver uses it.
5. The BIOS's legacy USB support had been off throughout, so no xHCI
   controller was the BIOS's: not the holder.
6. The system drive moved to the CPU-attached M.2 slot (now IRQ 11):
   its driver instance still at Code 10, still compatibility mode - the
   failure follows the drive, not the slot or its routing.
7. The owner's interim set-up: xHCI98 installed with the three IRQ 15
   xHCI controllers disabled in Device Manager.

`IOS.LOG` read "Unit number 02 going through real mode drivers"; the
real-mode drivers in `CONFIG.SYS` hook nothing unsafe for IOS.

### The vehicle

- Guests: q35 98 SE, overlays of `E:\xhci98-vm\q35-bases\w98se\disk.qcow2`
  (`ab1d4d65...`), the bases' emulator (QEMU 11.1.2, `da60139f...`),
  TCG, one vCPU, 512 MB. The base's C: is on the ICH9 AHCI controller,
  which 98 SE has no driver for, so it already reads C: (and the `-Xfer`
  disk, D:) through SeaBIOS's INT 13h: compatibility mode, as on the
  board. SeaBIOS's disk code polls and asserts nothing.
- The holder: `pci-serial` (`1B36:0002`) at 01:00.1 beside the
  `qemu-xhci` at 01:00.0 (`multifunction=on`, behind the root port at
  00:1C.0), so both are INTA of one slot: `info pci` gave both IRQ 10, and
  I/O C000h to the serial function. 98 SE found no driver for it ("PCI
  Serial Controller", Other devices). Writing `02h` to its Interrupt
  Enable register (`o c001 2` in `DEBUG`) makes it hold INTx asserted
  until its IIR is read, which nothing does; `o c001 0` releases it. From
  `AUTOEXEC.BAT` (`if exist D:\ARMBOOT.FLG debug < D:\ARM.SCR`) it is held
  from before Windows starts.
- Timing: batch files on D: copy a 4 MB file and echo marks to COM1; the
  host time-stamps the serial log's lines.
- Packages: `out\3.2.0.0`'s release package (`xfer320\PKG`), installed
  with `XHCIINST /dir release-x86 /yes /nopause`.

### What was read

| # | Set-up | Result |
|---|---|---|
| 1 | No xHCI98, holder armed (`DEBUG` in a DOS box) | Slave 8259 `irr=04 imr=8c elcr=0e`: IRQ 10 level, asserted, **masked**; copies unchanged (4 MB from D: 7.8 s cold; cached 0.6 s before and after arming) |
| 2 | xHCI98 `3.2.0.0`, `XhciInterruptMode` 0, no holder | IRQ 10 unmasked (`imr=88`) from the start; 4 MB cold copies 5.0 and 5.8 s: xHCI98's timers and poller cost nothing visible in compatibility mode |
| 3 | As 2, holder armed | About 78,000 interrupts a second on the cascade; keystrokes no longer processed; QEMU stopped about 70 s later on a **triple fault** (`-d cpu_reset`); twice of two |
| 4 | As 2, the controller disabled in Device Manager, then the holder armed | The line stayed unmasked after the disconnect until the holder's first assertion, then Windows **masked IRQ 10 again** (`imr=8c`); the system ran normally |
| 5 | `XhciInterruptMode` 2 (machine-wide key), the controller enabled with the holder already armed | IRQ 10 stayed masked, no storm; 4 MB cold copy 8.6 s |
| 6 | Fresh overlay, `3.2.0.0`, `XhciInterruptMode` 2, holder armed from `AUTOEXEC.BAT` | IRQ 10 masked all session (`irr=04 imr=8c`, 118 cascade interrupts in all); a USB stick (`usb-storage` on `xh` port 3, plugged after the start) found by polling, "USB Mass Storage Device" installed, E: listed, a 4 MB file copied to C: and compared clean (`fc /b`); `XHCITR /dump`: `IntrMode 2`, delivery POLLING, reason "forced polling (XhciInterruptMode 2)", 5924 poll runs |

So on 98 SE a held, unacknowledged level line is harmless while no
driver uses it and ruinous once one does, and polling (mode 2), which
connects no interrupt, leaves it as Windows had it. QEMU's holder never
lets go and the guest dies; the board's lets go now and then
[inf: around the BIOS's own disk commands] and the system crawls.

### A prototype that was not kept

Before the owner narrowed the task, a fix was designed and prototyped in
this worktree (uncommitted; kept untracked as
`E:\xhci98-scratch\p45-w45-compat\45-33-draft.patch`, the design as a
section 4.4 of record 15, reviewed and revised): on 98 SE and ME
under mode 0, (1) the start setting its own controller's PCI Interrupt
Disable before the connection when its Interrupt Status bit was set,
(2) a walk of PCI configuration space through `HalGetBusData` for a
function on the same Interrupt Line with Interrupt Status set and
Interrupt Disable clear, confirmed four times 10 ms apart, which then
polled the controller with a new reason, and (3) a storm detector in the
ISR wrapper (2048 declined entries within 100 ms, a window over the
system time's two words) latching a move to polling by the dead-line
watcher and a disconnect by the controller thread. Read with a `qemu`
build of it swapped into the guest of reading 2 (diagnostic only, never
packaged):

- (2) worked: with the holder armed from `AUTOEXEC.BAT`, the start chose
  polling, IRQ 10 stayed masked, and the snapshot named the holder
  (01:00.1, `1B36:0002`, IRQ 10, eight functions read).
- (3) did not: with the holder armed after the start, the ISR latched the
  storm 30 to 35 ms in, and the watcher's DPC never ran again; the guest
  stopped on a triple fault within seconds. The same with the holder
  pulsed - from `DEBUG`-assembled code in a DOS box (which the storm
  starved before it could release the line) and from a Win32 program in
  the System VM (2000 loop iterations held, then `Sleep(20)`): latched,
  no move, triple fault. Meanwhile the slave IMR alternated between IRQ
  10 masked and unmasked (`imr=8c`, `88`, IRQ 10 sometimes in service),
  so VPICD masks and unmasks the line around each pass: a mask written
  at the 8259 by the driver would not hold either.

That left only (2) as something that works, and only for a holder
already asserting at the start; the owner chose documentation instead
(owner, 23:30).

### What changed

- `README.md`, Known limitations/issues, Windows 98 SE (ME expected the
  same, not tested): the symptom, how to spot it in Device Manager, and
  the ways round it (a working driver or removing the device; and, where
  no other working driver uses that IRQ, disabling the xHCI98 controllers
  on it or `XhciInterruptMode` `2` with a restart).
- `docs/using/tuning.md`, `XhciInterruptMode` `2`: the same case, with
  reading 6 behind it.
- The release notes' text is the coordinator's for `45.final`.

### Evidence

`E:\xhci98-vm\p45\w45-compat\g1\` (readings 1 to 5 and the prototype's:
`ts-*.txt` time-stamped serial marks, `qemu-c.log`, `qemu-f.log`,
`qemu-h.log`, `qemu-k.log`, `qemu-l.log` with the triple faults,
`debugcon-h.log` with the latch, `serial-i.log` with the prototype's
snapshot, screenshots `s01.png` to `s38.png`, `pids.txt`) and
`E:\xhci98-vm\p45\w45-compat\g2\` (reading 6: `serial-b.log` with the
snapshot and the `fc` result, `ts-b.txt`, `s01.png` to `s09.png`,
`pids.txt`); the transfer folder `E:\xhci98-scratch\p45-w45-compat\xfer\`
(`ARM.SCR`, `CT.BAT`, `CR.BAT`, `STK.BAT`, `MODE2.REG`, `BURST.COM`,
`BURSTW.EXE`). Not tracked.

### Owed

- The board: whether `XhciInterruptMode` `2` leaves IRQ 15 masked there
  too and the system usable with the three IRQ 15 controllers enabled
  (the owner's reading, when convenient). Windows ME: not read; the
  documentation says it is expected to behave as 98 SE, not tested.

## 45.23 a UAS drive behind a VIA VL805 that fails writes after standby (2026-10-29)

**Verdict: the report's symptom was reproduced on q35 XP through the
driver's resume re-initialization, in the report's own words; the owner
decided the same day to keep devices across it, and that is built (design
record 20 sections 4.3.1 and 4.3.2). A UAS drive with a serial number on a
root port and idle at the sleep was restored in place and kept its open
file; on 2026-10-30 the owner extended that to a drive under load (section
4.3.3), built and read the same way: under a copy loop it is kept, every
copy after the resume compared equal and the held file still growing, and
a disk swapped behind it while the machine slept, with another unit
serial, is found again as a new one, never written as the old.** The
reporter's system, Windows version, xHCI98 version and log were not had,
and hibernation stands in for standby, so the reporter's cause is not
confirmed; the reading is the code's and the emulator's. The real VL805
is 45-E.12.

### The reading of the code

On a VIA controller `XhciResumeReinit` (design record 20 section 4.3)
skips the suspend's save, so every resume re-initializes the controller
(`XhciResumeController`), as it does on any controller without a usable
save, QEMU's included. The re-initialization raises
`XhciSlotInvalidateAll`; the controller thread's next pass ran
`hcdInvalidate`, whose `hcdDropAll` drained every device, reported each PDO
missing and freed each record, and the bus then enumerated every device
again as a new PDO. `xhciuas.sys` passes power IRPs down and keeps nothing
across them. So a storage stack saw a surprise removal at every such
resume: requests in flight failed, cached writes were lost, and a handle
opened before the sleep - a virtual machine's disk image - failed for good.

### The vehicle

Development host A, QEMU under TCG (the q35 bases' emulator), the q35 XP
base (`launch-q35.ps1 -Os xp`, 2 processors), a fresh overlay per run,
`-vga cirrus -global ICH9-LPC.disable_s3=0 -global
ICH9-LPC.disable_s4=0`, a `usb-uas` stick (`serial=W45UAS01`, one
`scsi-hd` LUN on an overlay of the FAT test disk) on `qemu-xhci` port 3,
`XhciResumeReinit` 2 in the machine-wide key. Standby cannot be made on XP
here: with the standard VGA XP offers no sleep state ("System does not
support hibernation", standby "Not Supported"), and with Cirrus it offers
standby but the display driver vetoes it ("System Standby Failed ... the
device driver for the 'Cirrus Logic 5446 Compatible Graphics Adapter'
device is preventing the machine from entering standby"), as `lessons.md`
records for 2000. Hibernation works with Cirrus, and its resume takes the
same re-initialization, so the legs hibernate (`powercfg /hibernate on`,
`rundll32 powrprof.dll,SetSuspendState 1,1,0`; the resume by starting the
overlay again). Load: `W.BAT D:` (a 16 MiB file copied, compared and
deleted in a loop, a COM1 line a pass) and a file held open across the
sleep and written every second (`cmd /k (for /l %i in (1,1,100000) do
@echo %i & ping -n 2 127.0.0.1 >nul) > D:\LONG.TXT`).

### Before the fix

| Build | Transport | Result |
|---|---|---|
| `3.3.0.0` release | UAS, copy loop | 8 copies `COPYFAIL` over 4 s after the resume, then `OK` on the drive enumerated again |
| this tree at `4fe3d9b1`, release (`xhci98.sys` `134c7f02...`) | UAS, copy loop and held file | 9 copies `COPYFAIL`, then `OK`; "Windows - Delayed Write Failed: Windows was unable to save all the data for the file D:\LONG.TXT. The data has been lost."; the held file: "The volume for a file has been externally altered so that the opened file is no longer valid" and "Windows - Wrong Volume ... insert volume ORICO"; XP's balloon "Delayed Write Failed" - the report's symptom |
| the same | Bulk-Only (`usb-storage`), copy loop and held file | 1 `CMPFAIL`, 22 `COPYFAIL`, then `OK`; "Delayed Write Failed ... D:\LONG2.TXT" |

QEMU's `usb-uas` offers UAS alone (one alternate setting), so
`XhciForceBulkOnly` cannot put it on Bulk-Only (re-plugged with the value
at 1 it bound "xHCI98 USB Attached SCSI Storage" again); the Bulk-Only
reading is QEMU's `usb-storage`. A harness note: a `usb-uas` hot-plugged
before its `scsi-hd` attaches at 12 Mbit/s and is never addressed (`info
usb`: "Device 0.0 ... 12 Mb/s"); plugged with `attached=false`, its LUN
added, then `qom-set attached=true`, it attaches at 5 Gbit/s and
enumerates. Evidence: `E:\xhci98-vm\p45\w45-legs\xp23\`
(`delayed-write-failed-uas.png`, `held-handle-invalid-uas.png`,
`balloon-delayed-write.png`, `delayed-write-failed-bot.png`) and
`t330xp\`.

### The fix and its legs

The owner chose option (a) of record 20 section 4.3.1 on 2026-10-29. Built
as section 4.3.2 says (`59ef97c9` to `87d3d489`, reviewed and revised): a
device on a root port that `XhciEnumResumeKeep` keeps is restored in place
on a new slot with its PDOs, record and pipe handles, the D0 waiting for
that before the function drivers above are powered; anything else is
dropped as before; `XhciResumeKeep` 0 turns it off. Host vectors:
`test_enum` (the decision), `test_membuf` (the pool's claim and rehome).

Read on fresh XP overlays (`xpk\`) with the `qemu` flavour of the tree
copied over the installed `xhci98.sys` for its port-0xE9 trace (the
package `xhci98-3.3.0.0.zip` of the gated x86 build of `06642a74`, release
`xhci98.sys` `d7f8ab2c...`, then the `qemu` builds named below):

| Driver | Load at the sleep | Result |
|---|---|---|
| `06642a74`, `qemu` (`8b294fec...`) | copy loop and held file | the tablet kept ("resume keep, restored, port/slot=00000601"); the UAS drive not kept, its note `resume.keep.port=0000030A` (busy: a URB outstanding on its PDO); dropped and found again, "Delayed Write Failed" as before |
| `7e1b1934` (the D0 waits for the keep), `qemu` (`8fc7337b...`) | held file alone | the UAS drive kept: "resume keep, port/flags/why=0300E500" (root, started, storage, UAS, serial; kept), "endpoints replayed, mask=000001A4" (its four bulk endpoints), "restored, port/slot=00000301"; no popup, the held window shows no error, and `D:\LONG4.TXT` grew from 858 to 990 bytes after the resume |
| the same | copy loop and held file | "a PDO's URBs pending=00000003" (three URBs pending on the UAS PDO when the pass looked, the count alone: which they were was not read), `why` 0x0A: dropped, "Delayed Write Failed ... D:\LONG4.TXT" |

So the fix keeps the files of a drive the decision keeps and the restore
succeeds on - here one idle when the machine slept; a drive with URBs
pending across the sleep is dropped, as record 20's K4 rules for a
storage interface busy at the reset. Whether
`xhciuas.sys` should wait out its commands before it passes a power-down
on, so that a busy drive is idle by the sleep, is left open (the record's
section 4.3.2). The wait was revised after these legs (`87d3d489` and
`0e9a6151`: armed only with the switch on, released only once the
invalidation is taken, on the direct-transition route too, and settled by
a stop or a fresh start). Read again with the `qemu` flavour of the
final gated x86 build of `0e9a6151` (`xhci98.sys` `8953e2cb...`; the
build's release `a021a33b...`) on the same overlay: the held file alone -
"port/flags/why=0300E500", "endpoints replayed, mask=000001A4",
"restored, port/slot=00000301", no popup, `D:\LONG5.TXT` grew from 726
to 846 bytes after the resume; then `XhciResumeKeep` 0 in the
machine-wide key and a restart: "port/flags/why=0300E501" (switch off),
the drive dropped, "Delayed Write Failed ... D:\LONG6.TXT" as before the
fix. With the switch at 0 the drop runs through the keep's own function,
each device refused, and the D0 does not wait.

### A drive under load kept too (2026-10-30)

The owner decided on 2026-10-30 that a UAS drive busy at the
re-initialization is kept as well, in this release. Designed in design
record 20 section 4.3.3 (`30040196`, reviewed and revised before any
code): no command is issued again; `xhciuas.sys` holds the class
driver's requests from the system sleep, waits out the commands already
in flight, records each LUN's identity and sends it SYNCHRONIZE CACHE (a
LUN that refuses it is counted and declares all the same, with no
durability claim), then declares itself quiet to the bus, which keeps a
UAS drive only with that declaration; after the wake it waits for the
bus's pass and, when the drive was kept across a re-initialization,
checks each LUN's medium before it starts the held requests, or cycles
the port. Built in `e8db6905` (reviewed and
revised); host vectors in `test_enum` (the decision, the declaration's
life) and `test_uas` (sense, the page list, the identity comparison, the
wake decision). One gated build of `e8db6905`: x86 `all` (debug, release,
qemu) BUILD + GATES PASSED, amd64 `both` BUILD + GATES PASSED, host tests
PASSED.

Read on the same q35 XP overlay (`xpk\`), the `qemu` flavour of the
gated x86 build's `xhci98.sys` (`8a20a537...`) and its release
`xhciuas.sys` (`3092ffde...`) copied over the installed ones,
`XhciResumeKeep` absent (on), the `scsi-hd` LUN given a unit serial
(`serial=W45LUN01`); load as above (`W.BAT D:`, a held file written every
second); hibernation, the resume by starting the overlay again. Each sleep
logs the withdrawal and the declaration (`uas resume state, op/value=
02...` and `01000100`: one LUN flushed, none refused); each wake check
its outcome (`03`, then 3 verified, 4 failed).

| Leg | Load at the sleep | Result |
|---|---|---|
| SuperSpeed (streams), tags `k`, `l` | copy loop and held file `LONG7` | "port/flags/why=0304E500" (the quiet mark among the flags; kept), "endpoints replayed, mask=000001A4", "restored, port/slot=00000301", wake check verified (`03030000`); the copy loop's next pass after "H back" `OK` and every pass after it `OK` (each compared byte for byte by `fc /b`), no popup, `LONG7.TXT` grew from 666 to 768 bytes |
| idle (no copy loop), tags `l`, `m` | held file alone | kept and verified as above; `LONG7.TXT` grew from 1,584 to 1,716 bytes, no popup |
| two hibernations in a row, the second 86 s after the first resume (its wake check long done), tags `m`, `n`, `o` | copy loop and held file | both resumes kept and verified (`03030000`, `03030001`); no failed pass, `LONG7.TXT` grew from 2,934 to 3,048 bytes |
| High-Speed (streamless): `qemu-xhci` with `p3=0`, the drive at 480 Mbit/s, tags `t`, `u` | copy loop and held file `LONG10` | "port/flags/why=0304E500", "endpoints replayed, mask=000001A4", verified; passes `OK` after "H back", `LONG10.TXT` grew from 690 to 804 bytes |
| the disk image swapped while hibernated (`uas2.qcow2`, a fresh overlay of the same base, `serial=W45LUN02`, the same USB serial), tags `o`, `p` | copy loop and held file | the bus kept it (its USB identity matched), the wake check failed (`03040001`) and asked for the cycle ("cycle port asked, port=00000003"); "Delayed Write Failed ... D:\LONG7.TXT", the copy loop's passes in flight failed, then `OK` on the drive enumerated again; the second image holds only its `TESTDATA.BIN` - nothing of the held writes - and the first, attached again, still has `LONG7.TXT` (3,252 bytes) |
| the previous `xhciuas.sys` (`24e7a9d4...`, no declaration), tags `q`, `r` | copy loop and held file `LONG8` | "a PDO's URBs pending=00000003", "port/flags/why=0301E50B" (busy, no mark: not quiet), dropped, "Delayed Write Failed ... D:\LONG8.TXT" as before the fix |
| `XhciResumeKeep` 0, the new `xhciuas.sys`, tags `r`, `s` | copy loop and held file `LONG9` | the sleep declared quiet; "port/flags/why=0304E501" (switch off), dropped, "Delayed Write Failed ... D:\LONG9.TXT" |

The sleep phase also runs at a shutdown (S5) and a restart: it declared
there too (seen before the restarts of tags `q` and `r`), harmless, the
declaration void at the next start.

What the legs show is bounded: the held file's growth and the copy
loop's comparisons, not the held file's every byte; the swap rejected
for a different unit serial, not a swap the identification pages cannot
tell (section 4.3.3 B7's bound, a bridge that describes itself there);
one LUN, flushed each time (none refused).

Owed: Windows 2000 SP4 and 98 SE (their hibernation was not tried), the
real VL805 (45-E.12); a sleep that overtakes a wake check not yet run
(the stacked hold); the paths QEMU was not made to force - a drain or a
flush that runs out of time, an abandoned internal command, a check
command that times out, a keep pass slower than fifteen seconds, a drive
with neither identification page - whose decisions the host vectors
cover but whose timeouts and abandonment were not run; RESET_PORT's new
continuity test was not exercised by these legs (no transport error
occurred).

## 45.27 the legs the fixes of 2026-10-29 owe, and what that reading left (2026-10-29)

**Verdict: the small items are done (`4fe3d9b1`; the developer paths
`4f9c775d` and `984fcc36`, owner 2026-10-29); the installer's DevicePath
limit read on 98 SE; the update over `3.3.0.0` read on 98 SE and ME, with
a first-boot hang on each in the first run that the repeats did not show;
98 SE's and ME's event-wait paths read under load and replug; the rest
owed, each named below.**

### The small items

- The temporary-file field of `xhciuas-amd64.inf` and `xhcicm-amd64.inf`
  dropped, as their x86 INFs and `xhcicmf-amd64.inf` have it; UAS-TMPNAME
  and CM-TMPNAME hold every INF of their gate, the amd64 UAS-MSFILE
  mutation's anchor follows and the new mutations are in the self-tests
  (51 and 144 mutations pass).
- `s98_wininit_cancel` reads WININIT.INI from the file (`vsd_entry_r`), as
  `wininit_pending` does; only this installer's line goes, read again
  until gone; STOR98.TMP is deleted; and when a queued update may still
  put `stor98.mpd` back (another program's line, an unread file, a .TMP
  left) the uninstall keeps the driver and Stor98's record for a later
  run.
- `hcdHubPowerWork`: one work item per hub is enough beside the
  controller's D0 pair, since a hub's D0 completes in its completion
  routine (`hcd.h`, a comment; no code change).
- Design record 21 section 8: `XhciCmMode` 2 named for an unlisted Intel id
  of a class other than `0x0C0340`, as `TbtHwMatch` orders it.
- The `ObfDereferenceObject` rationale: every target for `xhci98.sys`, with
  its call sites; NT only for `xhcicm.sys`, named.
- `tuning.md`: `StorInterrupt`'s reasons, "INTx would not enable" among
  them; Stor98's README: "forced by storpollmode", as the code writes it.
- The developer paths in tracked documents, scripts and Stor98's records
  neutralised (owner, 2026-10-29); `run-usb4-guest.ps1` takes `-EmuRoot`,
  then `USB4EMU_ROOT`, then the `usb4-emu` checkout beside the main one;
  the dated run sheets stay as written.

### The installer's DevicePath on 98 SE

Fresh q35 98 SE overlay (`w98dp\`), `DevicePath` set to 270 characters
(`C:\WINDOWS\INF` and eleven `C:\DRV\...` entries) by `REGEDIT /S`, the
test cut of `4fe3d9b1` installed with `XHCIINST /dir release-x86 /yes
/nopause`: `C:\XHCI98` added, 280 characters exactly, accepted (the limit
is inclusive). The same run again with `/force /to C:\XHCI98B`: "DevicePath
would be 281 characters with C:\XHCI98B on it, past the 280 with which
Windows 98 SE's hardware wizard finds a driver: C:\XHCI98B is left off it;
point the wizard at it instead.", exit 4. PASS.

### The update over `3.3.0.0` on 98 SE and ME, and its WININIT reads

q35 98 SE and ME overlays with the ASM4242 model and the ORICO enclosure
at power-on: `3.3.0.0` installed (`/thunderbolt /stor98`, exit 3), a cold
start, then the test cut installed over it (`/force /thunderbolt
/stor98`, exit 3), a shutdown and a cold start. WININIT.BAK after the
start held `[rename]
C:\WINDOWS\System32\Drivers\xhci98.sys=C:\WINDOWS\System32\Drivers\xhci98.tmp`
on both: the setup engine's replacement of the loaded `xhci98.sys`. Stor98
was not loaded in these guests (the installer's log: "0 connection
managers" answered, the NVMe drive "install (no driver)"), so the
`stor98.mpd` half of the leg was not reached; it stays owed.

| Run | 98 SE | ME |
|---|---|---|
| `w98up`, `meup`: `3.3.0.0`, then the test cut | first start after the update hung (black text screen, the processor halted in the VMM's idle, 30 min; `first-boot-hang.png`); the second cold start reached the desktop | the same |
| `w98ctl`, `mectl`: `3.3.0.0`, then `3.3.0.0` `/force` (control) | desktop | desktop |
| `w98up2`: the same as `w98up`, the `qemu` `xhci98.sys` put in place of the queued `xhci98.tmp` | desktop; the 0xE9 trace runs to the root hub's start | - |
| `w98up3`, `meup3`: the same as the first row, release | desktop | desktop |

The update leg itself ran three times on 98 SE (the first run, which
hung; `w98up3`, release; `w98up2`, the `qemu` driver in place of the
queued one) and twice on ME (the first run, which hung; `meup3`); only the
first run hung on each. The control installs `3.3.0.0` over itself and
says nothing of the test cut's update. The hang is not explained and
stays open (evidence `w98up\`, `meup\`).

### The event waits on 98 SE and ME

The waits that a Windows 98 unsatisfied `KeWaitForSingleObject` return no
longer ends (`HcdWaitEvent`, `UasWaitEvent`): the code cannot force the
0xFFFFFFFF return, so the leg runs the paths that wait - the remove
events, the forward-and-wait of PnP IRPs, the UAS driver's rundowns, the
door's gate - many times under load and reads that none of them hangs
and that no gate wait was abandoned. Fresh q35 98 SE and ME overlays
(`w98ev\`, `meev\`), the test cut of `06642a74` installed (98 SE with
`/storage`, NUSB 3.3's files), a cold start, a Bulk-Only stick
(`usb-storage`, port 1) and a UAS stick (`usb-uas`, port 2, plugged
detached and then attached) hot-plugged and installed, `W9.BAT E:` (an
8 MiB random file copied and compared in a loop); then four Bulk-Only
and two UAS unplug-and-replug cycles, 15 s out and 25 to 30 s back, during
the loop; then `XHCITR /dump`. On both: each removal during a write gave
9x's own "Disk Write Error ... Unable to write to disk in drive E:";
once dismissed, both sticks were addressed again (`info usb`), the loop ran
on to its end, the snapshot was written through the door, and the
driver's notes hold no `gate.power.missed` or `gate.door.missed` (the
waits' abandonment record). What this proves is the paths' completion
under load and replug, not the unsatisfied return's handling, which only
a machine that produces it can show. PASS.

### A D0 overlapping a system power IRP

XP: every hibernation resume above (seven) completed with the devices
back and no hang, but whether a D0 and the system IRP's completion were
ever deferred together - the case the fix is for - was not observed, so
the leg is owed on XP as on 2000 and 7.

### Owed

- The power gate's wait under a standby on 98 SE or ME (ME's standby
  works in QEMU); owed.
- A D0 overlapping a system power IRP on 2000, XP and 7; owed.
- A UAS STOP and START with a LUN gone, a short REPORT LUNS answer, and
  ATA pass-through and SMART from a tool: owed (no such tool on the
  guests).
- Stor98's INTx fallback and a BUSY that keeps its length: QEMU's NVMe
  takes the INTx enable, and no BUSY was provoked; owed to hardware.
- The connection manager's eject or PCIe loss with an unplug during it and
  a resume with a lane adapter gone (`usb4-emu`'s `x-cable` and
  `x-pcie-down` are in the bases' emulator), its no-IRQ clock and retry
  wait on 98 SE, a failed D0 during a re-enumeration on 98 SE: owed.
- The WININIT read of the update over a loaded Stor98 `.MPD` (above).
- The SuperSpeed hub's reset with a stale change bit, the ring's RX index
  and cancelled waits: hardware readings, owed to the bench.

## 45.16 usb4-emu's depth-2 support for the Thunderbolt 3 firmware connection manager (2026-10-29)

Design record 34 section 5's requirements, in the owner's order (record
34 section 12, Q5): E10 first, then E1 to E5 and E7, then E6 and E8, with
E9 the documents. The work is `usb4-emu`'s (its own repository, branch
`w45-emu` from its `main` at `e0346a4`, commits `ae28571` and `4688042`), where the emulator
calls them requirements R35 to R41 (`docs/ICM-PROTOCOL.md` section 8 and
the new section 15, "Daisy chains"); the QEMU fork's commits, in that
commit's `src/patches/0001-usb4-emu.patch`, are on `51d078a`:

| QEMU commit | What |
|---|---|
| `c0b0edf` | `usb4-tunnel-dsp` `power-controller` (default on), the same lines as `7780a6e` on `usb4-emu`'s `asm4242-model` branch, so the two branches merge |
| `0546296` | E10: `usb4-device chain-power-controller` (on, off, auto) passed to every chain port the tunnel slot builds; `auto` keeps the port's default, which `-global usb4-tunnel-dsp.power-controller` sets |
| `fcc8a1b` | E1 to E7: the emulated firmware enumerates, announces, approves and removes one router per host port at depth 2 (R35 to R41) |
| `4e1d4ea` | a depth-2 announcement never before its parent's (a child plugged during a driver ready's hold) |
| `7467a1a` | the standalone fabric harness builds again (no qapi type in `usb4-internal.h`; a stub) |
| `91610eb` | no approve for a depth-2 router not yet announced in its connection |

Each requirement and what shows it, all on QEMU's qtest (q35, polling,
no interrupt enabled), development host A:

| Record 34 | Emulator | Shown by |
|---|---|---|
| E10 | `usb4-device chain-power-controller` | `/usb4-tunnel-dsp/chain-power-controller`: Slot Capabilities with and without a Power Controller, Hot-Plug Capable either way; off: Power Controller Control on and read-only, the chained NVMe answering config and BAR0 with no slot write; on and default: unpowered (all ones) |
| E1, E7 | R35 | `/usb4-icm/chain-announce` (the dock `dialect=tb3,downstream-ports=1` with a plain USB4 router and with `device-model=asm2464` behind it: two events, the second link 1 depth 2, `0x0821`; the chained router read back at route `0x301` with depth 2), `/usb4-icm/chain-approve` (a hot plug: the trace line "router 0x1 adapter 3 unplug 0: plug", and "unplug 1: unplug" at its removal), `/usb4-icm/chain-none` (security none: approved at once), `/usb4-icm-tr/chain` (route `0x301` in the event, `0x303` behind Titan Ridge port B) |
| E2 | R36 | `/usb4-icm/chain-announce`: get route (1, 2) answers `0x301`, (1, 3) and (1, 2) with nothing chained the error flag |
| E3 | R37 | `/usb4-icm/chain-approve`: the child's approve before the dock's: the error flag, no chain port, no tunnel; after the dock's: flags 0 and the NVMe behind the chain port answers its BAR0 read on q35; `/usb4-icm/chain-stale-approve`; `/usb4-icm-tr/chain` (a wrong route refused) |
| E4 | R38 | `/usb4-icm/chain-unplug`: the child's `device_del` one disconnect at depth 2 (`0x00210000`), the dock's tunnel and drive kept; the dock's two, depth 2 first; `usb4-host chain-parent-only=on` the dock's alone |
| E5 | R39 | `/usb4-icm/chain-rebind`: after `0x07` and a new driver ready, depth 1 then depth 2, both approved; `/usb4-icm/chain-hold`: a child plugged during a 500 ms hold announced after the dock |
| E6 | R40 | `/usb4-icm/chain-preboot`: pre-boot on, both NVMe functions present before any guest action, the chain port's endpoint `gen=1` (a cold insertion), the announcements `0x0d11` and `0x0d21` |
| E8 | `scripts/run-cm-test.sh` | `CHAIN=1` on `tb3-alpine-ridge` and `tb3-titan-ridge` (`CHAIN_PC=` for the power controller); the same device set brought up headless with security none on both profiles: both routers enumerated and auto-approved, two NVMe functions in `info pci` (`E:\xhci98-scratch\p45-w45-emu\e8check.py`, `e8-*.log`) |
| E9 | | `ICM-PROTOCOL.md` R35 to R41 and section 15, `ICM-TITAN-RIDGE.md` T13, `USB4-HOSTS.md` section 8, `XHCI98-HANDOFF.md`, `ROADMAP.md`, `README.md`; `usb4-icm.c`'s scope line |

Every depth-2 message is [inf]: no capture shows a real firmware's, so
the model follows the Linux client's forms (link and depth on Alpine
Ridge, the route in the event on Titan Ridge and Ice Lake), as record 34
section 4.1 does. Not modelled (R41): the USB fallback (R33) and the S3
restore (R34) at depth 2, more than one chained router per host port,
depth 3, Falcon Ridge.

Regression: every `usb4` qtest suite passes (host 40, tunnel 30, icm 38,
icm-tr 16, icm-icl 5, harness 25, fault 9, asm4242 8, asm2464 4, tb3dev
12, root-hotplug 17, usb3 9, retrain 10); the standalone fabric harness
45816 checks, 0 failures; the Alpine Ridge conformance replay
(`run-ar-conformance.sh`, `--exclude-comm cat`) gives the figures it gave
at `51d078a` (ICM responses 15 of 15, connected 3 of 3, disconnected 1 of
1; host router 199 of 294 dwords, the dock 2265 of 2514); QEMU's
`checkpatch.pl` clean. Logs under `E:\xhci98-scratch\p45-w45-emu\`
(`qt-*.log`, `fabric.log`, `arconf.log`).

Reviewed and revised: the defects fixed in `4e1d4ea` and `91610eb`, and
two statements of the documents corrected in `4688042` (the model's
chained tunnel is 1:7 <-> 301, the dock being at route 1; Linux refuses
a child's authorisation before its parent's, `domain.c`
`tb_domain_approve_switch`, so it never sends that approve first).

Guest readings are 45.17's (the ICM client on `tb3-alpine-ridge` and
`tb3-titan-ridge`) and 45.13's to 45.15's legs.

## 45.17 record 34 Part 2: the ICM client's approval order (2026-10-29 and 30, partial)

Design record 34 section 4.6 (revision 5) makes section 4.1 step 3 and
section 4.2 exact for `src\cm\tbt_icm.c`, reviewed and revised before
and with the code: the parent's entry (by route on Titan Ridge and Ice
Lake, by depth - 1 on the same physical port on Falcon and Alpine
Ridge); "established" (approved, outcome known, announced in this
session: the new `Unconfirmed`, which a failed driver ready does not
clear); the order check after the policy (`ApproveOwed`, `OwedParent`,
`cm.icm.approve.owed`, `tbt.icm.approve-owed*` with the event's words);
the owed children asked again whenever a parent's approval is
established by any path (its approve, an announcement settling it
approved, the refresh confirming a kept one), skipping one approved
meanwhile, unconfirmed, unrouted, behind another occupancy, or with a
newer announcement queued; a withheld or owed entry announced approved
settled in place with its subtree, where it was a replacement before. No
9x decline (record 34 Q1 (b)).

Not in this task: the depth-2 branch of the tunnel-up action to section
3's chain action. Every action already carries its `Depth` to
`CmReenumAction`; the chain action is Part 1's (45.13, `cm_reenum.c`),
and until it is in the tree a depth-2 tunnel-up takes today's member
rescan, which does not reach the chain port's bus (record 34 sections
3.2 and 4.6).

Host vectors (`test	est_tbt_icm.c`): `test_chain_order_ar`,
`test_chain_parent_outcomes`, `test_chain_route_rows`,
`test_chain_rebind_and_reconcile`, `test_chain_settled_in_place`,
`test_chain_resume_child_first`, `test_chain_queued_and_unconfirmed`
(record 34 section 9.1's list); the suite 777 checks, 0 failures with
MSVC 6.0 `/Za /W3 /WX`; every earlier vector unchanged.

Builds: the gated x86 build `scripts\build-driver.cmd all` of
`6a1d46b3` passed ("BUILD + GATES PASSED (debug release qemu)", log
`E:\xhci98-scratch\p45-w45-emu\build-x86.log`); the full host suite
`test\run-host-tests.cmd` passed (`hosttests.log`: test_tbt_icm 777,
test_tbt_hw 174, test_tbt_select 219, test_u4cm 1362, each 0
failures). After 3.4.0.0 was merged at `bcc4bc35` (45.12 in it), the
merge `1a2a72ca` passed both gated builds, `all` x86 and `all -amd64`
("BUILD + GATES PASSED (debug release qemu)" each, `build-x86-2.log`,
`build-amd64-2.log`).

### The legs (2026-10-30, q35, 7 SP1 x86)

**Vehicle.** The release x86 package of `1a2a72ca`
(`make-package.ps1 -Flavor release -Arch x86`; `xhcicm.sys`
`d012fccf...`, `xhci98.sys` `30b79dc6...`) laid into the
`3.3.0.0` download's layout with 45.12's gated `XHCIMARK.EXE`, installed
on a fresh overlay of the q35 7 x86 base with nothing attached
(`INSTALL /console /yes /thunderbolt /stor98`: exit 3, then after the
restart exit 0), kept as the legs' golden (`f0d7caf2...`); each leg a
fresh child overlay of it. Emulator: `usb4-emu` QEMU at `0b4a259`
(45.16's code; `qemu-system-x86_64.exe` `c1227fda...`), the q35 Thunderbolt
3 topology of the q35 bases' launcher (`-Usb4 tb3ar` / `tb3tr`: the
controller's switch, the NHI, tunnel port `tp0`, the TB xHCI), the dock
`usb4-device dialect=tb3,downstream-ports=1,chain-power-controller=off`
and a USB4 device router chained behind it, each with an NVMe; the
emulator's `usb4_icm_*` trace and `XHCIINST /devices` read on COM1.
Evidence: `E:\xhci98-vm\p45\w45-emu\w7g\` (`serial-<leg>.log`,
`trace-<leg>.log`, `cmdline-<leg>.txt`, screenshots).

| Leg | Setup | Read | Verdict |
|---|---|---|---|
| a | Alpine Ridge, security user, pre-boot off; the dock hot-plugged at the desktop, then the chained router | the dock announced (link 1, depth 1), get route, approved; the child announced at link 1 depth 2, get route (1, 2) answered `0x301`, approved after its parent (`ICM approve link 1 depth 2: tunnel up`); `XHCIINST /devices`: "USB4 (version 1.0), link 1, depth 2, approved". The child unplugged: one disconnect at depth 2, its entry gone, the dock kept; plugged again: approved again. The dock unplugged with the child attached: the child's disconnect, then the dock's; the table empty; no hang; shutdown in 13 s | client: pass. PCI side: not reached, see below |
| b | Alpine Ridge, pre-boot on, the chain attached at power-on | both levels "approved before Windows started"; announced approved at link 1 depth 1 and depth 2 after each driver ready, nothing approved; the dock's switch, both downstream ports and both NVMe functions started, both on Stor98, two disks; one volume (D:), the other disk offline because both test disks are overlays of one image (Windows' duplicate-signature rule) | pass |
| c | Titan Ridge 4C, pre-boot off, the chain attached at power-on | both announced with their routes (no get route), unapproved; the dock approved, then the child; both "approved" in `XHCIINST /devices` (the Titan Ridge controller's tunnel ports were never given this package's filter on this Alpine Ridge golden, so no volume was expected) | client: pass |
| d | control: Alpine Ridge, a plain dock (`dock=on`, no chain port) hot-plugged | approved; the dock's switch upstream port at Code 31 | see below |

**A finding outside 45.17.** On 7 x86 with the Thunderbolt 3 topology
(the tunnel port a downstream port of the controller's own switch), a
switch hot-plugged behind the tunnel port does not start: its upstream
port (`104C:8232`) carries `LowerFilters` `xhcicm` and fails with
`CM_PROB_FAILED_ADD` (Code 31; setupapi.dev.log: "Device not started:
Device has problem: 0x1f"), with a chain port (leg a) and without
(leg d). A switch present at power-on starts (leg b). The chained
device's PCI side under a hot-plugged dock was therefore not read here.
Reported to the coordinator as 45.12's area (the bridge marks).

Owed: the PCI side of a hot-plugged chain under the firmware connection
manager (record 34 section 9.3 clauses 1 to 3 and 5 to 8: the chain
action is 45.13's, and on 7 the Code 31 above first); the other systems
(98 SE, ME, 2000, XP, 10 x64); Titan Ridge with its tunnel ports
installed.

## 45.5 98 SE's storage record written out before each storage file (2026-10-29)

Commit `e4097713` (reviewed and revised): `stor_place_to` flushes the
temporary copy (`FlushFileBuffers`) and then, after the `Files` or
`Placed` value is written, the registry (`RegFlushKey(HKEY_LOCAL_MACHINE)`)
before `MoveFileA`; `Published` is written out when first set; a flush
that fails stops the placement, and for an INF the publication too.
Design record 26 section 6.5.6 (draft 46). Gated build 3 (x86 and amd64,
every gate, the window self-test 673 checks with no failure) and a test
cut labelled "Release 3.3.0.0 TEST" from it.

**The leg** (development host A, QEMU q35, a fresh 98 SE overlay `w98c`,
the cut's `-nonfree` download with `USBDRV\NUSB33`, 2026-10-29): the
install from the window with the storage row ticked, a gdb stub on
`KERNEL32!MoveFileA` (`0xBFFA1A44`) and the power cut (QMP `quit`)
before the move of `XHCIINST.NEW\USBSTOR.INF`, 44.20's cut b. After a
cold start `HKLM\Software\xhci98\Storage` was whole: `Choice` 1,
`PreExisting` 0, `BeforeOk` 1, `Source` 1, `Published` 1, the five
`Files` values and the two `Placed` ones. `DevicePath` read
`C:\WINDOWS\INF` (the run had not reached its own entry). The next run
read all seven files as this installer's (state 1, case 4, "Installed by
this installer earlier."), and the uninstall deleted the seven and the
`Storage` key. **Pass.**

## 45.7 and 45.8 98 SE and ME: every package staged again, and the page (2026-10-29)

Commits `26973326` and `86fa7cee` (the page's words for the rest in two
lines), then `29d6177b` (ME's limit, below); all reviewed and revised.
Design record 26 section 6.7.13 (drafts 47 and 50). Gated build 3 and its
test cut for 98 SE; gated build 4 for `29d6177b`.

**98 SE, the window** (`w98s`, a fresh overlay, the `-nonfree` download):
the free page with its copy box unticked listed every stageable package
greyed and, below them, "Copied only (no automatic install on this
Windows):" with `rtl815x` (`USBDRV\FREE\net\rtl815x\w98se-me`) and
`noaud`, and "Nothing of this set is copied in this run: tick the box
above to copy it and to choose packages to stage."; End and Home on the
copy box scrolled the list. Ticked, the two lines read "Ticked: staged,
so Windows' hardware wizard finds the driver by itself." and "Not
ticked: copied only; when the wizard asks for a driver, choose Specify a
location and the package's folder under C:\XHCI98\USBDRV\FREE.". Ticking
down the list (Down and Space inside it): `uac2`, `usbmidi`, `uvc98`,
`xpad98`, `cdcnet` and `ax88179` took their ticks, then `usbflop`,
`usbcom` and `libusb0` read "(not available: see below)" with "DevicePath
would pass 280 characters, past which Windows 98 SE's hardware wizard
finds no driver: point the wizard at ... instead."; unticking `cdcnet`
and `ax88179` gave the three back. On the non-free page `ftdi` ticked
and the rest of that set barred the same way. Installed: `DevicePath`
239 characters, `C:\WINDOWS\INF`, `C:\XHCI98` and the folders of `uac2`,
`usbmidi`, `uvc98`, `xpad98` and the non-free `ftdi`, each under
`Other\Free\Staged` or `Other\NonFree\Staged` with the data
`DevicePath`. After a cold start QEMU's `usb-serial` (FTDI 0403:6001)
plugged in: "New Hardware Found", "USB Serial Converter", no wizard
(Windows took `FTDIBUS.INF` from the staged folder by itself), then the
vendor's file copy asking for "FTDI USB Drivers Disk" as in run 43's
leg (cancelled; the non-free drivers themselves are not checked, the
owner's rule of 2026-10-22). QEMU's `usb-serial` enumerates only with a
chardev that opens: with a `null` one it stays unattached and nothing
happens, as record 9 says. The console uninstall took the five folders
out of `DevicePath` one by one and left `C:\WINDOWS\INF`. **Pass.**

**98 SE, an update over an earlier staging** (`w98v`, an overlay of a
98 SE disk with the `3.3.0.0` release installed and its free `uac2`
staged): the free page, its copy box unticked, listed "Staged by an
earlier run: free:audio/uac2 (still offered to Windows); leaving a box
unticked does not undo it; the uninstall removes what this installer
placed and no device uses." and "Nothing of this set is copied in this
run: ... Earlier staging kept is listed above."; ticked, "Ticked: staged,
... ; earlier staging kept is listed above." With every box ticked,
`usbflop`, `usbcom` and `libusb0` were barred; installed, `DevicePath`
278 characters with `uac2` kept and five more folders. **Pass.**

**ME** (`wme`, a fresh overlay, the `-free` download): the page as on 98
SE, its copied-only list `usbmidi` (`USBDRV\FREE\audio\usbmidi\wme`) and
`rtl815x`. Every box ticked fitted 280: installed, `DevicePath` was
exactly 280 characters. At the next cold start ME's "New Hardware Found"
window for "xHCI98 USB 3 Root Hub" stayed at "Windows has found new
hardware and is locating the software for it" for more than twelve
minutes, the guest busy; quit and started cold again, the same. ME0E.1's
stop was not seen. A control install on another fresh overlay (`wme2`,
the free set unticked, `DevicePath` 24) started clean, its root hub
installed. On that control, with `DevicePath` set by hand through
`regedit /s` (the folders copied from the download, or an absent folder
as padding), a QEMU USB keyboard, mouse or tablet plugged in was
installed within seconds and with no wizard at 24, 238 and 259
characters, and stayed at "locating the software" at 260 (padding), 270
(real folders) and 280 (the install's own value). So ME's limit is 259,
and past it ME's wizard stops responding rather than finding no driver,
which holds the root hub too. Fixed by `29d6177b`: the limit is the
system's (`inst_9x_devicepath_max`: 280 on 98 SE, 259 on ME), and a
barred box's reason says "past which Windows ME's hardware wizard stops
responding". The ME leg on the fixed build: below.

**ME on the fixed build** (`wm4`, a fresh overlay, gated build 4's test
cut, `-free`, 2026-10-30): with every free box ticked down the list,
`usbcom` read "(not available: see below)" with "DevicePath would pass
259 characters, past which Windows ME's hardware wizard stops
responding: point the wizard at
C:\XHCI98\USBDRV\FREE\serial\usbcom\w98se-me instead."; installed,
`DevicePath` 235 characters with `uac2`, `cdcnet`, `ax88179`, `xpad98`
and `usbflop`. At the cold start the root hub was installed with no
window left open and the tray tool came up; a QEMU mouse plugged in was
installed silently. The console uninstall took the five folders out of
`DevicePath` (`C:\WINDOWS\INF` left) and removed `C:\XHCI98`. **Pass.**

## 45.6 an exclusion lifted where the devices are apart (2026-10-29)

Commit `26973326` (reviewed and revised): the cut reads the free and
non-free sets' device ids and lifts an `EXCLUDES` between two packages
whose devices are apart on a system (`Get-OtherLifted`, `LIFTED` lines
in the cut's record, `Test-OtherTableLifts` both ways), and the
installer's table carries such a pair as kind 2 (`INST_REL_LIFTED`), which
holds it against an earlier staging of another release. Design record 26
section 6.7.14 (draft 47). The packager's self-tests passed (1700
checks) with the table as it was. The owner's decision of 2026-10-29:
`usb-wdm-free` 0.1.5 splits its `rtl815x` line, `COEXISTS` on XP to 7 x64
and `EXCLUDES` on the rest. The installer's row for the pair, split to
match (`0x1E07` kind 1 and `0x1F8` kind 2, with its host vector), is not
in this branch's commits: with the pin still at 0.1.4 the cut refuses it
("the installer's table marks free:net/rtl815x and nonfree:ethernet/rtl815x
lifted on WXP WXP64 WVISTA WVISTA64 W7 W7X64, and this cut lifts no
EXCLUDES of the pair there"), so it goes in with the pin's move (the
worker's patch `D-45.6.diff`), and the XP and 7 leg after it. **Partial.**

## 45.28 the Components page evenly spaced (2026-10-29)

Commit `f7876c31` (reviewed and revised): `comp_layout` stacks the shown
rows with one gap each, every row's sentence at the height its words
take, and puts the buttons and the folder line at the page's foot.
Design record 26 section 5.3.4 (draft 48). The window's self-test checks
nine variants and every storage note (gated build 3: 673 checks, no
failure). Screenshots of the page from the test cut, nothing installed
(development host A, QEMU q35): 98 SE (with the storage row), ME, 2000,
XP, 7 x86 (with the greyed "Replace the stored build" row) and 10 x86
(the controller row unticked, the Thunderbolt row ticked and greyed, the
Stor98 rows open): on each the rows evenly spaced, no sentence clipped
or overlapping, the buttons and the folder line at the foot. **Pass.**

## 45.12's installer half: `XhciCmBridgeReq` under "Advanced..." (2026-10-29)

Commit `9d6a8290` (reviewed and revised): design record 33 section 9.1's
switch is a row of the Thunderbolt page's "Advanced..." list on 2000, XP
and XP x64 (`INST_APPLY_NT5_CM`), greyed elsewhere with "Windows 2000 and
XP only"; never asked, not written by the INFs, the driver's default 1;
`/set XhciCmBridgeReq=0..2` on those systems only. Host vectors; design
record 26 section 5.5 (draft 49). Gated build 4.

## 45.32 the 98 SE and ME hardware-wizard question only when needed (2026-10-29)

Commit `1084624b` (reviewed and revised): on a system read as English the
first gate looks for a hardware wizard too and asks only when it finds
one or cannot look; elsewhere it asks once as before; `/yes` unchanged.
English: `GetSystemDefaultLangID` and the language of `KERNEL32.DLL`'s
version resource both `LANG_ENGLISH` (both through KERNEL32, so the
console's imports stay KERNEL32 and ADVAPI32; the resource's language is
the build's, which Regional Settings do not move). Before the change,
the ME install above asked the question with no wizard open. Design
record 27 section 6 and H6 (draft 16). Gated build 4.

**Legs** (gated build 4's test cut, fresh 98 SE overlays, 2026-10-30):
with no wizard open (`w9g`) the install asked no question; its log read
"gate: system language 0409, KERNEL32's version resource 0409: English,
the first gate looks" and "gate: no hardware wizard open". With the Add
New Hardware Wizard open (`w9w`, opened by `control sysdm.cpl,Add New
Hardware`) the question came, the log adding "A hardware wizard is open
("Add New Hardware Wizard")." On ME (`wm4`, above) no question with no
wizard open. **Pass.**

## A second xHCI controller asks for the driver disk on 98 SE (2026-10-29, reproduced)

Reported from task 45.10's leg (a 98 SE install with `/thunderbolt`: the
ASM4242's own xHCI controller asked twice for "xhci98 xHCI Host
Controller Disk"). Reproduced here on a fresh 98 SE overlay (`w98x`)
with two QEMU xHCI controllers, the test cut and the default rows: the
helper's job for the first controller installed it ("installed, restart
pending"); its job for the second, the same INF `C:\XHCI98\xhci98.inf`
with `LDID_SRCPATH` set to `C:\XHCI98` exactly as in the first job,
brought "Insert Disk: Please insert the disk labeled 'xhci98 xHCI Host
Controller Disk'" and then "The file 'xhci98.sys' ... cannot be found",
copy from `C:\WIN98`; typing `C:\XHCI98` let it finish. The helper's
result lines of the two jobs are the same to the flags. setupx is one
instance for every process on 98 SE, so its state from the first job's
copy outlives the helper process; the second copy is looked for at the
Windows source. Seen again on `w9w` above (two controllers). Not fixed in
this branch: a fix, a `nocopy` job flag with which the helper sets
`DI_NOFILECOPY` for a later controller of an INF a controller's job of
the same run installed, is drafted, not committed, for a task of its
own. **Reproduced; open.**

## 45.35 a dock's xHCI lost with its tunnel stalled Windows' removal on 2000 and XP: the reading, the owner's decision, the code (2026-10-29 to 2026-10-30)

**The reading.** On an XP leg taken from 45.12's R5a disk, the qemu
flavour of `xhci98.sys` (port `0xE9` log) was put in place and the dock
was unplugged twice: once with its USB tablet on the dock's xHCI, once
with a USB disk there. Evidence is in `E:\xhci98-vm\p45\w45-dock\xp\`:
`debugcon-r7h-tablet.log`, `r7h-unplug.txt` and `r7h-disk-unplug.txt`,
and `xp2\report-r7b.txt` (the gated release's XHCITR report in the stalled
state).

| Case | What the logs show |
|---|---|
| The tablet | After `cm.reenum.departure` the dock's xHCI read all ones (CRCR, USBSTS). A command timed out, the reset was requested, the recovery was refused (`ctrl.recover.refused=4`) and the unproven invalidation pinned (`slots.unproven.pinned`). XHCITR's report: "containment PINNED: no proof DMA stopped; buffer, transfers kept". No `SURPRISE_REMOVAL` reached any of `xhci98.sys`'s objects: the tablet's interrupt-IN read stayed on a ring whose stop could not be commanded, and the removal waited on it |
| The USB disk | `SURPRISE_REMOVAL` reached the device PDO, the root hub and the controller in turn. The controller's stop found the function off the bus ("quiesce: device is off the bus - it masters nothing") and released everything |

So the stall was record 17 section 4.13's case, "a tunnelled controller
lost with its tunnel": the fail-closed rule keeps the transfers of a
controller whose DMA cannot be proven stopped. It was not seen before
because, until 45.12 placed the dock whole, the dock's xHCI port stayed
at problem 12 on XP.

**The owner's decision (2026-10-29): both.**
- A release-notes limitation for 3.4.0.0. On 2000 and XP, stop a dock
  first (Safely Remove Hardware, or XHCITR /eject) before unplugging it
  with a keyboard, mouse or other interrupt device on its own xHCI. This
  stays for Thunderbolt 3 hosts under the firmware connection manager,
  and for 98 SE and ME.
- For this release's own connection manager: a tunnel path that it has
  closed and read back closed is proof that no function behind that
  tunnel port can reach host memory. Record 17 section 4.13.6 and record
  22 section 10.5 set this out. Both were reviewed and revised before any
  code.

**The code (`8b246287`).**
- The core keeps one word per USB4 port, `TBT_U4CM.IsoWord[]`.
  - It is set when step 1 of the teardown read back closed.
  - It is withdrawn at the top of `tbtU4PcieTunnel`, the only writer that
    sets a PCIe Path Enable, and for every port by `TbtU4Start`, `Resume`,
    `Recover`, `Stop` and `Contain`.
  - `TbtU4PortIsolated` reads it; `TbtU4PortPcieGen` gives the generation
    of a tunnel that is up.
- `xhcicm`'s kernel-attached child filter on the controller's PDO answers
  `IRP_MN_QUERY_INTERFACE` for `GUID_XHCI_CM_ISOLATION`, on NT 5.x.
  - The binding (`CmRegIsoBind`) records the domain by its NHI object and
    `Reenum.Starts`, the member, the USB4 port by correspondence, and the
    tunnel's generation.
  - `Isolated()` (`CmRegIsoAsk`, pure decision `CmIsoDecide`) answers yes
    only while all of these hold: the domain is unchanged, it runs this
    connection manager and is live; the port still corresponds to the
    member; the port's word carries the bound generation.
  - It is counted as `cm.iso.asked`, then `cm.iso.yes` or `cm.iso.no`.
- `xhci98.sys` takes the interface at its start, before its thread runs,
  and releases it after the thread is joined (and at its removal).
  - `hcdContainProve` asks the interface only where Bus Master Enable
    gives no proof (`XhciTolProof`). It logs `tol.proof.tunnel`.
  - A containment proven this way is recorded `XHCI_TOL_CONTAIN_TUNNEL`,
    which XHCITR shows as "RELEASED: the USB4 tunnel's path read back
    closed".
- No new import in either driver.

Host suites:
- `test_u4cm`, 1384 checks: the word set on a closure read back, never
  without the read-back, withdrawn at a replug and at a stop.
- `test_cm_iso`, 1037 checks: every condition refused alone, and all 1024
  patterns.
- `test_tol`, 3420 checks: the proof's verdict.

**A correction found by the legs (`96d01e1f`).** On 2000 the gated
release of `8b246287` bound nothing. The member's USB4-port
correspondence was learned (`u4.learn`) only after the dock's xHCI had
started and asked for the interface, so no port could be named, and the
tablet's unplug stalled as before. The qemu flavour, slower to the xHCI's
start, had bound.

Now the binding records every USB4 port whose PCIe tunnel is up at the
controller's start, with its generation. The port that feeds the member
is named at each ask, by the correspondence then, and the proof holds
only for the generation that port carried at the start. Record 22
section 10.5 was revised to match, and reviewed.

**The legs.** Runtime, development host A, 2026-10-29 23:53 to
2026-10-30 02:40; at most three virtual processors. The emulator and the
layout are 45.12's.
- **Test build:** the qemu flavours of both drivers on child overlays of
  the R5a disks (XP leg `r7i`, 2000 leg `r7j`).
- **Release:** the gated release binaries of `8b246287` swapped into the
  45.12 release installs (`xp2`, `w2k2`). On 2000, the release
  `xhcicm.sys` of `96d01e1f` was swapped in on top.
- **2000:** the swap is followed by a shutdown and a cold start. A warm
  restart hung on a black screen there.

| Leg | System | Result |
|---|---|---|
| The tablet, unplugged, replugged, unplugged again (test build) | XP | `SURPRISE_REMOVAL` reached the device PDO, root hub and controller; `cm.iso` yes for generation 1. The replug was enumerated and placed, with the tablet and the HID device back and the binding taken again for generation 2. The second unplug: yes for generation 2. The shutdown completed |
| The tablet unplugged, then a replug with the USB disk and its unplug (test build) | 2000 | The tablet's unplug: removed, `cm.iso` yes. A replug with the USB disk was enumerated, its unplug removed, and the shutdown reached "It is now safe to turn off your computer" |
| The tablet twice, then the USB disk (gated release of `8b246287`) | XP | `cm.iso.asked` 2, `cm.iso.yes` 2 (generations 1 and 2). Every replug placed (the dock's functions, the tablet), the disk's cycle removed, the shutdown completed |
| The same | 2000 | **Bound nothing** (the correspondence came after the start, see above). The first unplug stalled as before 45.35: no re-enumeration after the next plug |
| The tablet twice, then the USB disk (`96d01e1f`'s release `xhcicm.sys`) | 2000 | `cm.iso.bind` with the port mask, `cm.iso.asked` 2, `cm.iso.yes` 2 (generations 1 and 2), the replug enumerated (16 chain attachments, two plugs), the disk's cycle removed, the shutdown reached "safe to turn off" |
| The tablet, unplugged, replugged, unplugged again (`96d01e1f`'s release `xhcicm.sys`) | XP | `cm.iso.asked` 2, `cm.iso.yes` 2 (generations 1 and 2), the replug placed with the tablet back, the shutdown completed |

**Release-notes limitation (owner's decision, for 45.final).** On
Windows 2000 and XP, stop a dock first (Safely Remove Hardware, or
`XHCITR /eject`) before unplugging it with a keyboard, mouse or other
interrupt device on the dock's own USB controller.
- **When the limitation applies:** Thunderbolt 3 hosts under the firmware
  connection manager, 98 SE and ME, and the residual order of record 17
  section 4.13.6.
- **What happens otherwise:** Windows may never finish removing the dock.
  It is not found again when plugged back, and Windows does not shut
  down.

On a USB4 host that this release's connection manager runs, the legs
above removed the dock cleanly, the closure having been recorded before
the proof was asked; the reverse order keeps the pin (record 17 section
4.13.6).

## 45.38 98 SE's driver-disk prompt at a second xHCI controller (2026-10-30)

The finding above is task 45.38. Commit `a01f9c88` (reviewed and revised):
a controller's job for an INF that an earlier controller's job of the
same run installed (outcome installed, or installed with a restart) now
passes `nocopy` on its `FLAGS` line, and the helper sets `DI_NOFILECOPY`
for it. The first copy has put the same files in place, or left them
for `WININIT` at the restart it already asks for. The root hub, the
hubs, the composite entries and Stor98's devices neither set nor take
the mark. Design record 27 sections 4.1, 5.3, 6 and H4 (draft 17).
Why setupx looks at the Windows source for the second copy was not
read: the prompt offered `C:\WIN98`, the value the first job had
restored `LDID_SRCPATH` to, and setupx is one instance for every
process. Keeping `LDID_SRCPATH` set for the whole run was the other
remedy weighed. It was not taken: it would leave shared setupx state
changed between helper jobs, while other installs may run, and it needs
a restore on every way out.

**The legs** (gated build 5, its test cut, fresh overlays with two QEMU
xHCI controllers, development host A, 2026-10-30): the window install
with its default rows on 98 SE (`g98`, the `-nonfree` download) and ME
(`gme`, the `-free` download). On both the first controller's job read
`FLAGS quiet`, the second's `FLAGS quiet nocopy` with the log line
"helper: nocopy: a controller's job of this run copied xhci98.inf's
files", both "installed-restart", and no prompt. After the cold start
both systems had two xHCI98 root hubs, one under each controller. On
98 SE, an update with `/force /thunderbolt /stor98` over `3.3.0.0`
behind the ASM4242 model (task 45.36's `u1`) reinstalled both
controllers (the ASM4242's own and QEMU's) with no prompt, where the
same machine's `3.3.0.0` install had prompted. The `3.3.0.0` installs of
the same layout on ME (`hm1`) did not prompt. The QEMU exit while the
prompt was up (45.10's leg) did not recur in the four 98 SE installs
that prompted (`w98x`, `w9w`, and the `3.3.0.0` installs on `h1` and
`h2`). **Pass.**

## 45.36 98 SE and ME stopping at the first start after an update over `3.3.0.0`: the rate and the stop (2026-10-30, partial)

No code. Development host A, QEMU q35 with the ASM4242 model and the
ORICO enclosure at power-on (as 45.27's legs), gated build 5's test cut
as "this build".

**The machines.** Fresh 98 SE (`h1`, `h2`) and ME (`hm1`) overlays:
`3.3.0.0` installed with `/thunderbolt /stor98` (exit 3; on 98 SE the
ASM4242's controller asked for the xhci98 disk, task 45.38, answered
`C:\XHCI98`), one cold start to the desktop, then shut down and kept as
read-only bases. Stor98 was bound to the tunnel's NVMe drive ("Stor98
Storage Controller for NVMe SSDs" on `PCI\VEN_1B36&DEV_0010`), unlike
45.27's guests, where it never was. Each trial is an overlay of a base:
a start, `INSTALL`'s console with `/force /thunderbolt /stor98` from the
cut (the release build, or the debug build), the `WININIT.INI` read
(every time `[rename]` with `xhci98.sys=xhci98.tmp`), a shutdown, and
the first start watched for nine minutes.

**The rate.** 98 SE: six trials (`u1` to `u6`), five reached the
desktop and one (`u5`, the release build) stopped about 30 seconds into
its first start with "A fatal exception 0E has occurred at
019F:BFF72170. The current application will be terminated."; after a
key the screen stayed an empty desktop with no taskbar for minutes,
which is how 45.27's black screen would look if its stop screen went
unseen. ME: two trials (`um3`, `um4`) reached the desktop; a third
overlay of the ME base (`um1`) stopped at its first start, before any
update, with "Error: 0E : 01A7 : BFF62170" (ME0E.1's text and address,
the 44.20 one) and then a grey screen.

**The stop.** The code at `0xBFF72170` (read from the stopped guest's
memory, `memsave`, and disassembled) is `pop ds; pop es; pop fs; pop gs;
popa; popf; push -1; push 0x2A0009; call ...`: a register-frame restore
followed by a VxD call into VWIN32 (device `0x2A`, service 9), the end of
a ring-3 return path in the shared arena. The page fault is on the first
`pop`: the thread's stack page was not present when the saved frame was
popped. ME's stop is at the same offset, `0x2170`, of the same region
(`BFF62170`), so 45.36's stop and ME0E.1 look to be one fault. Neither
the release nor the debug build wrote anything to the debug port (the
debug console file stayed empty), and none of this package's 9x drivers
queues a user APC or calls into ring 3 (no `_VWIN32_QueueUserApc`,
`_SHELL_CallAtAppyTime` or `_VWIN32_SetWin32Event` in the tree). Who
queued the frame is not read.

**Owed.** Which thread and frame: a gdb stub with a breakpoint on the
fault (the VMM's page-fault path for `0x2170` with a non-present stack
page) on more trials, and the same layout without the enclosure and
without Stor98 to see whether either is needed; then the cause, and a
fix where it is this package's. **Partial.**

## 45.9 - The Prefetchable Windows on the 32-bit 8, 8.1 and 10: a Filter in the PCI Root Bus's Stack, Read (2026-10-29)

The task's reading (roadmap-usb4 Phase 45, PFW8.1's first half): does a
step in the PCI root bus's stack act before the PCI driver reads the
bridges, at every start read? The design that follows from it is design
record 32 section 11.5, **a draft not yet reviewed** (paused 2026-10-29).

### The vehicle

Development host A; q35, the launcher and emulator of "44.72"
(`bin\asm4242-20261026-4goff`, `qemu-system-x86_64.exe` `08D49470...`),
the 4G-off layout with the ORICO at power-on, `qemu64` processors, two
per guest, 2 GB; fresh overlays of the clean q35 bases of 8 x86
(`b8e091bd`), 8.1 x86 (`9714c3d8`) and 10 x86 (`ac361c39`). A probe of
this project's own, not tracked: `pfwflt.sys` (SHA-256 `37ffe267...0a8a`),
built with the Windows 2000 DDK from a scratch copy of `src\pfw\pfw_scan.c`
and a PnP filter of about 200 lines: it marks its `DriverEntry`, each
`AddDevice` and its first `IRP_MN_START_DEVICE` in the configuration
trace (three reads each of 00:00.0 at `F8h`, `F4h` and `F0h`) and on the
debug console, and runs record 32's step inside `KeIpiGenericCall` at
that first start before passing it down; every other request passes
down. Installed on the board without the USB4 model by a batch run
elevated: the image to `system32\drivers\pfwflt.sys`, the service
(Type 1, `Start` 0, `ErrorControl` 0, `Group` `PnP Filter`; the batch's
`ImagePath` line did not take, so the default path served), `powercfg /h
off`, and `pfwflt` written as both `UpperFilters` and `LowerFilters` of
`Enum\ACPI\PNP0A08\0` by a task run as SYSTEM. Then cold starts on the
4G-off layout, "RW" a read and write trace, "W" writes only; the
problem codes by `wmic` to COM1 at the desktop.

### Readings

| System | Start | Trace | `DriverEntry` | `AddDevice` (lower, upper) | start | step's close of the root port | PCI driver's probe of the root port | Result |
|---|---|---|---|---|---|---|---|---|
| 8 | a, b, c, d | RW | 14 457 | 14 460, 14 463 | 14 466 | 31 046 | 31 599 | the Start screen; every bridge 0; NVMe, NHI, `fw_cfg` 28 |
| 8 | e, f | W | - | - | - | 446 | 511 | the same |
| 8.1 | a, b, c | RW | 14 457 | 14 460, 14 463 | 14 466 | 31 046 | 31 600 | the desktop; every bridge 0; NHI, `fw_cfg` 28 |
| 8.1 | d, e | W | - | - | - | 446 | 511 | the same |
| 10 (`g10`) | a | RW | none | none | none | none | 44 715 | "Automatic Repair: Your PC did not start correctly" |
| 10 (`g10`) | b | RW | none | none | none | none | - | stop `INACCESSIBLE_BOOT_DEVICE` (screenshot) |
| 10 (`g10c`, `Group` `Core`) | a | RW | none | - | - | - | - | "Diagnosing your PC" |
| 10 (`g10c`) | the board without the USB4 model | - | none | - | - | - | - | "Preparing Automatic Repair" (after the failed starts) |

On 8 and 8.1 the debug console showed the same sequence at every start:
`DriverEntry`; `AddDevice` 1 (attached over the ACPI driver's object:
the lower filter); three `IRP_MN_QUERY_INTERFACE` through it (the PCI
driver's own `AddDevice`); `AddDevice` 2 (over the PCI driver's object:
the upper filter); `IRP_MN_QUERY_LEGACY_BUS_INFORMATION`,
`IRP_MN_FILTER_RESOURCE_REQUIREMENTS` and `IRP_MN_START_DEVICE`, each to
the upper filter first; the step's line `result/functions/closed/refused`
in hex, `1 12 9 0` (18 functions, nine bridges closed). On 10 the image had entered
`DriverEntry` at the start before the filters were written (`g10`'s
install start, a plain boot driver of group `PnP Filter`, "pfwflt
DriverEntry" and no `AddDevice`); with the root bus naming it, it was
never entered and the start failed.

### What it says

- **8 and 8.1**: a filter in the root bus's stack runs before the PCI
  driver reads the bridges at every start read (six on 8, five on 8.1),
  at the same trace lines every time, where "44.72"'s boot driver of
  `System Reserved` came after the PCI driver at three of four starts on
  8 and one of six on 8.1 [read]. The step there closes the windows and
  8, which does not start without it ("44.49"), starts [read].
- **10**: a boot-start filter named by the root bus devnode is not
  initialised and the root bus does not start: Windows stops with
  `INACCESSIBLE_BOOT_DEVICE` or goes to its recovery, in group `PnP
  Filter` and in group `Core` alike [read]; why is not read.
- **The hazard is real**: a root bus whose stack names a filter that is
  not loaded stops Windows at start (10's readings) [read]; the design
  makes the removal take the filter out of `UpperFilters` first.

So the step can act in time on 8 and 8.1 in the root bus's stack; on 10
it cannot be placed there as read. The owner then chose the filter role
on 8 and 8.1, coded in this release; the design, the code and its legs
are "45.9 (continued)" below.

Evidence: `E:\xhci98-vm\p45\w45-read\` (`g8`, `g81`, `g10`, `g10c`:
`trace-*.log`, `debugcon-*.log`, `serial-*.log`, `leg-*.txt`,
screenshots; `xflt`, `xfltc` the batches and the probe image;
`pids.txt`, `STATE.md`); the probe's source in
`E:\xhci98-scratch\p45-w45-read\flt`.

## 45.9 (continued) - `XHCIPFW.SYS` as a Root-Bus Filter on the 32-bit 8 and 8.1: the Code and Its Legs (2026-10-30)

The owner's answers of 2026-10-29 (record 32 section 11.5.4): the filter
role on 8 and 8.1 only, the image boot-critical there accepted with the
order of changes and the readme's recovery, coded in `3.4.0.0`. Record 32
section 11.5.2 is the design (reviewed and revised over six rounds, the
last with no findings); the code is commits `e409d8c7`, `8cbed23c`,
`6fb1a773`, `106286a7` and `1c0a48d9` (reviewed and revised, the last
round with no material findings): the driver's two roles by version, the
filter role's step over its own root bus's bus range at that bus's first
start, the installer's filter role under names of its own (service
`xhcipff`, images `xhcipff.sys` and `xhcipff2.sys`, record
`HKLM\Software\xhci98\XhciPfwFilter`) so that `3.3.0.0`'s installer
cannot strand it, the entries taken out of every root bus before any
removal and an update that keeps them, a machine-wide lock, the readme's
recovery; host vectors (`test_pfw`, `test_inst`).

### The vehicle

Development host A; the gated `release` builds x86 and amd64 at
`1c0a48d9` (`BUILD + GATES PASSED`, both), a TEST cut of the x86 half
(`make-release.ps1 -Flavor release -Arch x86 -TestLabel`, zip SHA-256
`51d5ba1d...0091`, `XHCIPFW.SYS` `c38e2e02...a8a`, `XHCIINST.EXE`
`11887345...bf65`). q35, the launcher and emulator of "45.9" above; fresh
overlays of the clean 8 x86, 8.1 x86 and 10 x86 bases, two processors,
2 GB. The install `XHCIINST.EXE /dir release-x86 /yes /nopause
/thunderbolt` from an elevated prompt on the board without the USB4 model
(each "Windows can't verify the publisher" box answered "Install this
driver software anyway"); then cold starts on the 4G-off layout with the
ORICO at power-on, "RW" a read and write trace, "W" writes only; the
registry, the filters and the problem codes read by a batch to COM1.

### Readings

| System | Step | Result |
|---|---|---|
| 8 | install | exit 3; service `xhcipff` (Type 1, `Start` 0, `ErrorControl` 0, `Group` `PnP Filter`, `ImagePath` `System32\DRIVERS\xhcipff.sys`); `UpperFilters` `xhcipff` on `ACPI\PNP0A08\0` |
| 8 | cold starts a, b (RW), c (W) | the step's close of the root port before the PCI driver's probe at every start (trace lines 31 034 against 31 584; c 446 against 511); the Start screen; `XhciPfwRole` 2, `XhciPfwMode` 2 (inside `KeIpiGenericCall`), `XhciPfwBuses` 0 to FFh, `XhciPfwClosed` 9, `XhciPfwSkipped` 0 (no "late" bit), the hold 70, 78 and 30 ms of TCG time; every ASM4242 and enclosure bridge at problem 0 (only the NVMe, with no driver on 8, and `fw_cfg` at 28) |
| 8 | the installer again (update over itself, 4G-off) | exit 3; the entry and the service kept as they were; the step closed 9 at that start |
| 8 | the image deleted by hand, the entry left | the start stops: "Automatic Repair: Windows couldn't load correctly" offering System Restore; System Restore returned a working start, the package's registry rolled back to before its install (a restore point from before it [inf]; Startup Repair's log: root cause "unspecified changes to system configuration", repair action System Restore) |
| 8 | install again, then `/uninstall` | exit 3 each; after the uninstall no `xhcipff` service, no entry, no image; a cold start reached the Start screen |
| 8 | install again, the image deleted, the repair's Cancel | the start stopped again and stayed at the logo; the readme's manual route (the entry removed from the recovery environment's registry editor) was not driven |
| 8.1 | install | exit 3; the same service, group and entry |
| 8.1 | cold starts a, b (RW), c (W) | close before probe at every start (31 034 against 31 585; 446 against 511); the desktop; `XhciPfwRole` 2, `XhciPfwClosed` 9, `XhciPfwSkipped` 0; only `fw_cfg` at 28 |
| 8.1 | update over itself (4G-off), `/uninstall`, a cold start | exit 3, the entry kept; exit 3, "XHCIPFW.SYS is no longer registered", no entry; the desktop |
| 10 | `/xhcipfw` | exit 1, "REFUSED: /xhcipfw: XHCIPFW.SYS cannot start in time on Windows 10.", nothing changed |
| 10 | the install without it | exit 3; no `xhcipff` or `xhcipfw` service, no entry on any root bus, no image |

### What it says

- On 8 and 8.1 the filter role closes the windows before the PCI driver
  reads them at every start read (three each), with no "late" bit, and
  both systems start with every bridge behind the ASM4242 at problem 0
  [read].
- The update over itself keeps the entries and the service; the
  uninstall takes the entry out and removes the service and the image,
  and the system starts after it [read].
- A root bus naming the filter with its image gone stops the start, as
  the design says; Windows' own System Restore recovered it [read]; the
  readme's manual route is owed (a recovery-environment leg).
- On 10 nothing is placed and `/xhcipfw` is refused [read].

Left: the readme's manual recovery route in the recovery environment;
an update from one image to another (only the same release over itself
was read); a two-processor hold's length on hardware and the hardware
bench (45-E). The task stays unticked for the coordinator to decide.

Evidence: `E:\xhci98-vm\p45\w45-read\` (`h8`, `h81`, `h10`:
`serial-*.log`, `trace-*.log`, `leg-*.txt`, screenshots; `xfl` the
batches and the cut; `pids.txt`, `STATE.md`); the builds' logs in
`E:\xhci98-scratch\p45-w45-read\build2-x86.log` and `build2-amd64.log`.

## 45.10 - Tunnelled NVMe Speed on 98 SE and ME, the Emulator Readings (2026-10-29)

Design record 30 section 13.7's readings on a q35 98 SE guest: two
threads at Stor98, the per-page mapping cost aligned against scattered,
and the request sizes reaching Stor98 at 4 and 8 MiB. Every figure here
is TCG's and compares within this table only (record 30 section 13.4).

### The vehicle

Development host A; q35, `launch-q35.ps1`'s machine (`-Usb4 asm4242
-EncAtBoot`: the ASM4242 model with the ORICO and its 128 MiB FAT test
disk tunnelled at power-on), `pentium3`, one processor, 512 MB; a fresh
overlay of the clean q35 98 SE base (`ab1d4d65`), its bridge wizards
answered. The package: the x86 `release` gated build at `1765b18c`
(`BUILD + GATES PASSED`, this tree's 3.3.0.0 version stamp), cut as a
TEST upload set (`make-release.ps1 -Flavor release -Arch x86
-TestLabel`, zip SHA-256 `463564e8...24e0`), installed with `XHCIINST.EXE
/dir release-x86 /yes /nopause /thunderbolt /stor98 /s98vsd` (exit 3),
then a shut-down and cold start; the drive is E: on Stor98 3.3.0.0 (the
Windows 98 and ME build) with `S98VSD.VXD`. XHCIPERF of the same build
(SHA-256 `cf6b4a3d...25b3`), run from a batch by command line, 48 MiB
test file (the disk's free space allows no more), unbuffered, the random
fixed pattern, write-through off; each report with its Stor98 pages sent
to COM1.

Found on the way (outside this task, reported to the coordinator): the
install's helper asked twice for "the disk labeled 'xhci98 xHCI Host
Controller Disk'" (the file `xhci98.sys`, "Copy files from" offering
`C:\WIN98`) while installing the second xHCI controller; pointing it at
`C:\XHCI98` let the install finish. At the first attempt, on another
fresh overlay, the guest's QEMU process ended by itself while that
prompt stood; that overlay was discarded.

### Readings

R1, the `seqmax` preset (three passes of 5 s, best shown), aligned
buffer:

| Test | Write MB/s | Read MB/s | At Stor98 (read) |
|---|---|---|---|
| 512 KiB, 1 thread | 229.7 | 216.7 | one command a request, at most 1 at the drive |
| 1 MiB, 1 thread | 234.6 | 249.8 | two 512 KiB commands a request (the emulator's MDTS), at most 2 |
| 2 MiB, 1 thread | 241.0 | 234.4 | four 512 KiB commands, at most 3 to 4 |
| 8 MiB, 1 thread | 104.6 | 107.6 | **63 KiB pieces**: 8450 requests "of 63 KiB" for 65 reads, every one a scatter/gather list and double-buffered by IOS; "Largest request granted: 2048 KiB" |
| 1 MiB, 2 threads | 248.5 | 218.6 | at most 2 at the drive |

R2, 1 MiB requests at two and four threads (three passes of 5 s):

| Threads | Write MB/s | Read MB/s | Most commands at the drive at once |
|---|---|---|---|
| 2 | 210.6 | 209.6 | 2 (write and read) |
| 4 | 183.9 | 205.9 | 2 (write and read) |

R3 and R4, the `sweep` preset 64 KiB to 8 MiB at one thread, read
column (write in brackets), aligned against scattered:

| Size | Aligned MB/s | Scattered MB/s | Scattered at Stor98 |
|---|---|---|---|
| 64 KiB | 60.0 (67.9) | 34.0 (62.4) | |
| 128 KiB | 109.3 (93.0) | 64.5 (67.7) | |
| 256 KiB | 167.2 (120.7) | 53.8 (67.3) | |
| 512 KiB | 171.9 (172.2) | 60.7 (73.9) | |
| 1 MiB | 171.8 (170.3) | 67.3 (71.6) | 768 scatter/gather lists for 48 reads, each below 63 KiB; at most 1 at the drive |
| 2 MiB | 215.9 (202.0) | 62.9 (84.5) | 768 lists below 63 KiB for 24 reads; at most 1 |
| 4 MiB | 85.3 (83.2) | 67.7 (78.9) | |
| 8 MiB | 80.0 (82.4) | 67.7 (76.9) | |

At 4 and 8 MiB in R3, aligned, Stor98 received 779 requests "of 63 KiB",
1 "below 63 KiB" and 11 (4 MiB) or 5 (8 MiB) "below 4 KiB" for 12 reads
of 4 MiB and for 6 of 8 MiB, every one a scatter/gather list
double-buffered by IOS. With the scattered buffer (R4) the requests at 1
MiB and above arrived in the "below 63 KiB" class, not double-buffered.

### What it says, for the changes C1 to C5 (record 30 section 13.6)

- **Two threads at Stor98** (section 13.3 item 2): two and four threads
  never put more than two commands at the drive, which is what one
  1 MiB request's two 512 KiB children already do at one thread [read];
  that two threads' requests did not overlap at Stor98 is an inference
  from that depth, the pages carrying no request identities [inf]. The
  rates moved little and both ways (R1's write 234.6 at one thread, 248.5
  at two; R2's 210.6 and 183.9 at two and four) [read]: threads showed no
  clear gain on 98 SE in the emulator.
- **The request sizes at 4 and 8 MiB** (item 4): with the aligned
  buffer, requests above 2 MiB never reached Stor98 whole: they arrived
  as 63 KiB scatter/gather pieces, each double-buffered, at about 80 to
  108 MB/s against 202 to 241 MB/s at 2 MiB (R1 and R3) [read]. Stor98's
  granted maximum is 2048 KiB, so the cut is IOS's above that maximum
  (record 30 section 1.3) [inf]. Whether a larger 9x maximum (C4, with
  this layer's room raised) would let them through whole is not shown,
  every run having the 2048 KiB maximum [inf]. C4 stays planned, decided
  by the bench's B4 rows.
- **The per-page mapping cost, aligned against scattered** (item 3,
  C2): the scattered buffer did not reach Stor98 as a large request at
  all: at 1 and 2 MiB it arrived as scatter/gather lists below 63 KiB
  each (16 a 1 MiB request), one command at a time, at about a third of
  the aligned rate [read]. So the emulator's aligned-against-scattered
  difference mixes IOS's cut of a scattered buffer (other request
  sizes, command counts, depths and handover paths) with whatever
  Stor98's per-page `ScsiPortGetPhysicalAddress` calls cost, and cannot
  isolate or exclude the latter [inf]; C2's own gain needs another
  vehicle (a Stor98 counter of the mapping's time, or the bench's refit
  of B), and stays planned.
- **C1** (a child size setting) and **C3** (more `ENG_PRP_PAGES`): the
  emulator's MDTS of 512 KiB already splits every request above it, and
  depth reached 3 to 4 at 2 MiB with one thread [read]; both stay
  planned, decided by the bench's MDTS and depth histogram (B4).
- **C5** (interrupt coalescing): nothing here reads it; stays planned.

The bench (45-E.9) is unchanged. Evidence:
`E:\xhci98-vm\p45\w45-read\s98c\` (`perf-c.txt` the four reports with
every Stor98 page, `serial-*.log`, `pids.txt`), `xfer98\` (`P.BAT`, the
package).

Read on the way, for task 45.22's 98 SE route (now another worker's):
on the first overlay, before this project's package, QEMU's `info pci` showed
the ASM4242's NHI (`1B21:2425`, bus 8) with Interrupt Line 11, pin A,
beside the tunnelled NVMe (11, pin A) and the q35 xHCI and AHCI (10)
[emu]; whether 98 SE's configuration manager allocates that IRQ to the
NHI's devnode is not read.

## 45.21 - Thunderbolt 1, 2 and 3 on Apple Macs: the Reading and the Design (2026-10-29)

The task's first half (roadmap-usb4 Phase 45, the owner's "Phase 45
additions"): what a Mac's Thunderbolt host needs from this driver as its
connection manager, read from Linux's facts and the public record, then a
design section converged before any code. No code, no leg.

### What was read

- Linux `drivers/thunderbolt` at the pinned `c6859eed` (GPL-2.0-only,
  facts only): `pci.c` `nhi_ids[]` (Light Ridge `1513` and Cactus Ridge 4C
  `1547` matched only with subsystem `2222:1111`, Falcon Ridge by id),
  `icm.c` (`icm_probe` has no Light Ridge or Cactus Ridge row;
  `icm_fr_is_supported` is false on an Apple machine;
  `icm_ar_is_supported` takes the ICM on Alpine and Titan Ridge only when
  its firmware already runs or `start_icm` asks), `tb.c`
  (`tb_apple_add_links`, `tb_find_pcie_down`'s per-controller PCIe down
  adapters, `tb_resume_noirq`'s reset of a non-USB4 host), `switch.c`
  (`tb_switch_get_generation`: Light Ridge, Eagle Ridge, Light Peak,
  Cactus Ridge, Port Ridge and Redwood Ridge generation 1, Win Ridge and
  Falcon Ridge 2, Alpine and Titan Ridge 3; `tb_switch_configure`'s
  non-USB4 branch; `tb_plug_events_active`; `tb_switch_reset_host`, the
  "reset" packet on generation 1), `cap.c` (Light Ridge's TMU access bit
  and dummy read), `lc.c` (nothing below generation 2, the DP sink from
  3), `tunnel.c` `tb_dp_xchg_caps` (skipped below generation 2).
- Apple's support pages (Boot Camp Support Software 5.0.5033 and 5.1:
  64-bit Windows 7, 8 and 8.1 on the Macs of 2011 to 2014; Windows 10
  64-bit only, Macs of 2012 on) and the press reports of 2011 quoting
  Apple: the Early 2011 MacBook Pro, the first Thunderbolt Mac, supported
  Windows 7 32-bit and 64-bit only, XP and Vista "not supported", Boot
  Camp 4 Windows 7 only. No public page read names 98 SE, ME, 2000, XP or
  Vista for any Thunderbolt Mac; whether any starts there through the
  firmware's BIOS emulation is unread.
- This project: design records 21 and 22 (sections 3, 6, 8, 9, 11),
  `usb4-emu`'s `docs/CONTROLLER-CATALOG.md` (no Thunderbolt 1 or 2 model,
  no Apple host profile).

### What it says

- The five Mac hosts (Light Ridge, Cactus Ridge 4C, Falcon Ridge, Alpine
  Ridge, Titan Ridge) are, for Linux on a Mac, software-connection-manager
  hosts; the rest of Linux's Thunderbolt 1 and 2 parts are device routers
  there.
- What the USB4 software connection manager lacks for them is small in
  kind: section 8's Thunderbolt 3 device-router handling applied to the
  host router at route 0 and extended to generations 1 and 2 (no USB4
  router registers, the Plug Events capability, Light Ridge's two quirks,
  no link controller below generation 2, a fixed PCIe down adapter per
  controller); paths and the PCIe tunnel are the same code. DisplayPort is
  the large part and is left out.
- The systems within reach by Apple's own support are 7 (x86 and x64) and
  the 64-bit 8, 8.1 and 10; 98 SE to Vista are owed to the bench.

### The design

Design record 22 section 19 (commits `1765b18c` and `f46fbf5c`,
reviewed and revised), with the owner's questions Q1 to Q4 there. Section
3.1's inert start for subsystem vendor `106B` and the release notes' "Not
claimed: Apple machines" stay until the code and 45-E.11.

Left: the code (after the owner's answers and an emulator model in
`usb4-emu`, section 19.6), its host vectors and legs, and 45-E.11. The
task stays unticked.

## 45.37 a switch hot-added behind a Thunderbolt 3 tunnel port on 7 x86 at Code 31: the emulated platform's bus numbers (2026-10-30)

**The question.** 45.17's legs a and d found the hot-plugged dock's
switch upstream port (`104C:8232`) at Code 31 (`CM_PROB_FAILED_ADD`) on
7 x86 with the Thunderbolt 3 topology, its `LowerFilters` naming
`xhcicm` (the unit's mark), while the same dock present at power-on
started (leg b), and the same dock behind the ASM4242 layout started on
7 x86 with the same mark (45.12's R8: `PCI\VEN_104C&DEV_8232...`,
`LowerFilters` `xhcicm`, Code 0).

**The reading (leg r1).** A child overlay of 45.17's 7 x86 golden (the
release x86 package of `1a2a72ca`), the same emulator
(`qemu-system-x86_64.exe` `c1227fda...`, `usb4-emu` `0b4a259`), the
launcher's `-Usb4 tb3ar`; the dock (`usb4-device ... dialect=tb3,dock=on`)
plugged by QMP at the desktop. Evidence:
`E:\xhci98-vm\p45\w45-chain\w7\` (`serial-r1.log`, `trace-r1.log`,
`cmdline-r1.txt`).

| Step | Read |
|---|---|
| `info pci` before the plug | the root port `00:08.0` buses 1 to 9; the controller's switch upstream port 1 to 5; its downstream ports `02:00.0` (the NHI) 3 to 3, `02:01.0` (the tunnel port) **4 to 4**, `02:02.0` (the controller's xHCI) 5 to 5 |
| the plug | approved (`ICM approve link 1 depth 1: tunnel up`); `info pci`: `104C:8232` at bus 4, its **secondary and subordinate bus 0**, windows closed; `XHCIINST /devices`: "PCI Express standard Upstream Switch Port - pci, problem code 31"; `setupapi.dev.log`: "Device not started: Device has problem: 0x1f: CM_PROB_FAILED_ADD"; `LowerFilters` `xhcicm` on its instance key |
| A/B: the mark taken away | `net stop xhcimark`; the `LowerFilters` value deleted (as SYSTEM); the dock unplugged and plugged again: `LowerFilters` absent, so no `AddDevice` of `xhcicm` on the device, `104C:8232` again at bus 4 with secondary and subordinate 0, **again Code 31**. The tunnel port's bridge filter, left in place, still attaches its child filter above the new device's PDO at `BusRelations` (`cmAttachNew`), as it does in R8 and leg b, where the same switch started |

**Cause.** The emulated platform's. The emulated Thunderbolt 3 tunnel port
(`usb4-tb3-tunnel-dsp`, an `8086:15C0` downstream port of the
controller's own switch) carries no bus reservation, and SeaBIOS
reserves buses only for a bridge with QEMU's own resource-reserve
capability on a QEMU bridge id (`1B36:000C` or `1B36:0001`, 44.45), so the tunnel port spans one bus
(its own secondary). A switch hot-added there has no bus for its
secondary side; 7 neither renumbers the parent nor starts the bridge.
This is the limit "44.20 (continued)" read on 8 x86 and "44.64 Part A"
read on all five systems (`info pci`: `104C:8232` at bus 4 with
secondary and subordinate 0). Present at power-on, SeaBIOS numbers the
dock's switch itself, which is why leg b started. The ASM4242 layout
has its firmware padding (`fw-bus-pad`, 44.45), which is why R8 started
with the same mark. `xhcicm`'s registered child (`CmChildAddDevice`)
always answers success and is not the failing `AddDevice`: the A/B
fails the same without it. The child filter the tunnel port's bridge
filter attaches stayed in the A/B; it is on the stack in R8 and in leg
b too, where the switch, numbered, started.

**What follows.** No change in this tree. A hot-plugged switch behind
the emulated Thunderbolt 3 tunnel port needs firmware padding in the
emulator, as the ASM4242 preset has it (`usb4-emu`'s work, outside this
repository; reported to the coordinator). Until then, on the
Thunderbolt 3 topology a chained device is read on q35 behind a dock
present at power-on (SeaBIOS numbered the dock; a device without its
own switch then lands on the chain port's own secondary bus), and a
hot-plugged dock only behind a tunnel port with a reservation.

## 45.13 record 34 Part 1, the NT half: the chain action's first cut, and what 7 and XP do without it (2026-10-30)

**The design.** Record 34 section 3.9 (revision 6), reviewed and revised
before and with the code: chain ports walked below every active member
(`XhciTbtChainPorts`, `tbt_topo.c`: a switch downstream port, Slot
Implemented, Hot-Plug Capable, inside the member's bus range, at most
four, one level, no descent below one); a chained tunnel's up, down and
reconciliation invalidating each chain port's bus through the child
filter on it, under that filter's removal reference (`CmFilterTryHold`,
the list's lock then the filter's); an up owed per chained device (the
action's `Slot`) until every chain port is done, cancelled when no
longer live, at a loss and at the stop, dropped at 10 s
(`cm.chain.nofilter` or `cm.chain.owed-expired`); the counters in the
stop's log lines.

**The code** (`38ac4e18`): `src\cm\tbt_topo.c`/`.h` (the walk, the owed
table's pure part), `cm_reenum.c`/`.h` (`cmChainTake`, `cmChainTry`,
`cmChainAction`, `cmChainService`, the hooks in `CmReenumAction`,
`CmReenumService`, `CmReenumLossBegin`, `CmReenumStop`), `cm_filt.c`
(`CmFilterTryHold`, `CmFilterUnhold`). No new import, no INF change.
Host vectors: `test_tbt_topo.c` `test_chain_ports` (the emulator's dock
as read on q35, a five-port switch with one hot-plug port, none, the
non-candidates, the count and level bounds, a bridge outside the range,
the member unnumbered, absent or unread, a failed read, a failed
capability walk, a retry status, power off) and `test_chain_owed` (the
first owe, a retry keeping the clock and the done list, a new tunnel
restarting it, two children at one link and depth kept apart, the table
full, the done list's bound, the millisecond wrap); the suite 679
checks, 0 failures.

**Builds.** The gated x86 build of `38ac4e18`
(`scripts\build-driver.cmd all`): "BUILD + GATES PASSED (debug release
qemu)" (`E:\xhci98-scratch\p45-w45-chain\gb-x86.log`); the gated amd64
build (`all -amd64`): the same (`gb-amd64.log`); the release x86 package
(`make-package.ps1 -Flavor release -Arch x86`): `xhcicm.sys`
`23a2e9ab...`, `xhci98.sys` `5806fc7b...`.

### The legs (q35; emulator `usb4-emu` `0b4a259`, `qemu-system-x86_64.exe` `c1227fda...`; Thunderbolt 3 Alpine Ridge, `-Usb4 tb3ar`)

The chain: the dock `usb4-device ... dialect=tb3,downstream-ports=1,
chain-power-controller=off` attached at power-on with the host's
`preboot=on` (SeaBIOS numbers the dock's switch: the tunnel port
`02:01.0` buses 4 to 7, the dock's upstream port `04:00.0`, its fixed
port `05:00.0` to the dock's NVMe on bus 6, its chain port `05:01.0`,
bus 7), and the chained device `usb4-device ... parent=enc0,port=1`
(a plain enclosure, its NVMe directly on bus 7) plugged by QMP at the
desktop. A dock hot-plugged on this layout cannot be read (45.37).
Evidence: `E:\xhci98-vm\p45\w45-chain\w7\` (r2) and `xp\` (x0, x1):
`serial-<leg>.log`, `debugcon-<leg>.log`, `trace-<leg>.log`,
`cmdline-<leg>.txt`, screenshots.

| Leg | System, build | Read | Verdict |
|---|---|---|---|
| r2 | 7 SP1 x86, the 45.17 golden (release of `1a2a72ca`, no chain action) | the child approved at depth 2 (`ICM approve link 1 depth 2: tunnel up`); 7 found and numbered the chained NVMe itself (native hot plug on the chain port), Stor98 started, its disk started (no volume: both disks are overlays of one image, Windows' duplicate-signature rule); the child unplugged and plugged again: gone, then Code 0 again; the dock unplugged with the child attached: every devnode gone, no hang; shutdown in 9 s | the child's hot plug behind a dock present at power-on (part of clause 1, without its file check: no volume), 2, 3, 8: pass; the chain action was not what found it |
| x1 | XP SP3, a fresh golden (release of `1a2a72ca` installed with `/thunderbolt /stor98`, exit 3, restart; `1cec6889...`), the qemu flavour of `xhcicm.sys` at the reviewed source (`77f34053...`) swapped in | after the dock's first boot (Stor98 for the dock's NVMe by its wizard, a restart) the child plugged: `cm.chain.ports=00000001`, `cm.chain.invalidate=01020508` (an up at depth 2 on `05:01.0`), then the chain port's `BusRelations` with one new PDO, the child filter attached and the tunnel id given; Stor98 by its wizard; the drive as F: ("ORICO"), a file written and compared (`fc`: no differences). The child unplugged: `cm.chain.invalidate=02020508`, the NVMe gone. Plugged again: back at Code 0, F: again; a file written before the unplug was lost ("Delayed Write Failed" on F:: the chained unit is not offered for safe removal in this cut and was not stopped first). The dock unplugged with the child attached: both removed, XP's notice, no hang; shutdown in 10 s. Counters at the stop: ports 3, invalidate 3, owed 0, nofilter 0 | the child's hot plug with its file check behind a dock present at power-on (part of clauses 1 and 4), 2, 3: pass |
| x2 | XP, the same golden, the gated release `xhcicm.sys` and `xhci98.sys` of `38ac4e18` swapped in | the same steps: the chained NVMe on Stor98 (its wizard), F: "ORICO", a file copied and compared (`fc`: no differences); the child unplugged: gone; plugged again: Code 0, F: again (the file copied before the unplug lost, as in x1); the dock unplugged with the child attached: no PCI devnode of the dock left, no hang; shutdown in 10 s | pass on the gated build |
| x0 | XP, the same golden, its release `xhcicm.sys` (no chain action) | the same steps: the chained NVMe found (Stor98 by the "Found New Hardware" balloon, Code 0) and removed at its unplug | the A/B: XP finds and removes the chained device without the chain action |

**What x0 shows.** On XP with this layout the chained device was found
and removed without the chain action (x0). What found it there is an
inference: the member's invalidation, made at every tunnel change
(`CmReenumAction`), is the only request this tree made, so XP's
re-enumeration from it reaches the bus behind the dock's started chain
port. x1's log does not separate the two requests (`BusRelations` on
`04:00.0` and `05:00.0` before the action's call, the chain port's
answer with the new PDO after it). Record 34 section 3.2's inference
that the member's invalidation would not reach it is therefore not
borne out on XP; 2000 is not read. With 7's native hot plug (r2), the
action was not needed on either system read. It is a second request
that costs one invalidation per chain port per change.

**The lost write.** In x1 and x2 a file written to the chained drive
just before its surprise unplug was lost ("Delayed Write Failed"): XP
caches writes to a disk it is not told is removable, and in this cut the
chained unit is neither answered `Removable` nor offered for safe
removal (record 34 section 3.7, left below). The dock's own drive is
offered; a chained drive is to be stopped by stopping the dock until
section 3.7's code.

**The owner's question.** Keep the chain action (a second request beside
what XP's member invalidation and 7's native hot plug already do, kept
for 2000, unread, and for a layout where the member's re-enumeration
does not reach the chain port), or take its invalidation out and keep
the walk for section 3.7's chained unit and 45.14's 9x numbering, with
the reading recorded. Recommended: keep it until 2000 is read; it is
bounded (one call per chain port per change) and fails safe.

**Left in 45.13** (not ticked): the owner's question above; 2000's
reading (with and without the action); the correspondence learning, the chained unit's `Removable`,
marks and eject on NT (the lost write above is the case it is for), the
per-tunnel ownership, names and generations (record 34 sections 3.1,
3.5, 3.7 and Q4), the status record's kind 3 entries. 45.17's PCI-side
clauses read here on 7 and XP: 2, 3 and 8, and the parts of 1 and 4 the
legs above name (clause 1's hot-plugged dock cannot be read on this
layout, 45.37; clause 4's whole chain at power-on was not read); 5 (safe removal of
the chained unit) waits for section 3.7's code; 6 is 9x (45.14); 7 on
XP: the dock at power-on then the child hot-plugged works, a hot-plugged
dock cannot be read on this layout (45.37).

## 45.39 isochronous OUT slower than real time on the emulator's xHCI: the emulator's pacing, not the driver's (2026-10-30)

**The question.** `usb-wdm-free`'s `uac2` legs (its `docs/design/uac2.md`
section 15.3a) read a 3-second `UACWAV play` taking about 11.5 s on the
simulated Sound Blaster X4, and about 13 s with 6 of 8 buffers on the
real X4 passed through, under xHCI98 on QEMU's `qemu-xhci`. The same real
device through the same passthrough under XP's own EHCI driver plays 8
of 8 in about 3.3 s.

**The driver's side, read.** `uac2` sends every isochronous URB with
`USBD_START_ISO_TRANSFER_ASAP` (`uacusb.c`). xHCI98 publishes an ASAP
request with SIA on every TD (`hcd_io.c`, the isochronous publication;
`XhciXferIsoStartPlan` returns `XHCI_ISO_START_ASAP`) and IOC on every
TD. The ring is the pipe's pool ring (62 TRBs) or, for a High Speed
endpoint of `bInterval` 1 to 3 with a budget, a page ring of 120 (design
record 13 section 11.8). An explicit `StartFrame` gets a Frame ID on
every TD only where HCCPARAMS1's CFC bit is set, and otherwise on the
first TD of a data flow alone (task 36.57, xHCI 4.11.2.5). The
emulator's HCCPARAMS1 reads `0x00080001` plus its stream bits: CFC is 0.

**The emulator's side, read** (QEMU 11.1, `hw/usb/hcd-xhci.c`,
`xhci_calc_iso_kick`, `xhci_check_intr_iso_kick` and `xhci_kick_epctx`;
facts only). The emulator keeps no frame schedule of its own. It gives
each TD a target microframe and runs the TD once MFINDEX (the virtual
clock in 125-us steps) has reached that target. A TD that is not yet due
arms the endpoint's timer and ends the ring walk until the timer fires.
For a TD with SIA the target is the previous TD's target plus the
Interval, provided "as soon as possible" (now, rounded up to the
Interval) lies within four Intervals of that previous target; otherwise
the target is "as soon as possible". So when the emulator runs more than
four Intervals after the last TD, it does not catch up. It runs the TD
that was waiting, restarts the stream at "now", and arms the timer for
the next TD one Interval later. Each time it gets to run more than 500
us late it therefore serves about two TDs of a High Speed `bInterval` 1
endpoint (Interval 125 us, so a 500-us window), and every TD that is
due on a 1-ms endpoint up to 4 ms late. A TD with an explicit Frame ID
is due once its frame has come, and late ones run at once, within
limits: the target is rebuilt from the Frame ID in the current MFINDEX
epoch, and one more than 32 ms (256 microframes) behind is read as
2.048 s later. With CFC 0, though, only the first TD of a flow may
carry a Frame ID. A doorbell also makes the emulator look at a waiting
TD, under the same rule, outside its timer. The emulator's EHCI model, by
contrast, walks every microframe that has elapsed since its last run
(`hcd-ehci.c`, `ehci_work_bh`), which is why the EHCI path keeps time.

**The leg** (development host A, 2026-10-30, worker kit
`E:\xhci98-vm\p45\w45-iso`). Stock QEMU 11.1.0 (`qemu-system-x86_64.exe`
SHA-256 `47d57a60...`), run with `-msg timestamp=on` and the trace events
`usb_xhci_xfer_start`, `usb_xhci_queue_event` and
`usb_xhci_doorbell_write`. XP SP3 x86 on q35 with `-smp 2` and
`qemu-xhci` behind a root port. The disk is an overlay over copies of
task 45.24's leg `f1`, which has this release's tree (`xhci98.sys`
`a3de04a5...`; no isochronous source has changed since), and 45.25's
`x6`, which has `uac2` build 18 with the jack fix. The simulator,
`uacemu.py` (`usb-wdm-free` `0cf767a`, SHA-256 `b745ee98...`), ran over
`usb-redir`, and `UACWAV.EXE` (`95a036b1...`) ran `play 2 16 48000 3`.
The script `tools\isorate.py` counts, for each endpoint, the TDs that
the emulator started, the groups of TDs that share one host timestamp
(the trace stamps each line to the microsecond and does not mark the
emulator's passes over the ring, so a group stands for a pass only by
inference) and the completion codes of the Transfer Events.

| Stream | Expected a second | TDs a second | Timestamp groups a second (median gap) | TDs a group | Ring Underrun | `UACWAV` |
|---|---|---|---|---|---|---|
| `x4full`, High Speed, EP 1 OUT, 24 bytes a microframe | 8,000 | 2,057 (24,123 TDs in 11.73 s) | 711 (1.065 ms) | 2 in 4,887 of 8,335 groups, 3 in 1,500, at most 13 | 4, in one 0.2-s burst mid-stream | 8 of 8, `elapsed_ms=11766` |
| the same device, EP 3 IN feedback, Interval 1 ms | 1,000 | 983 | 697 (1.007 ms) | 1 or 2 | (31 Overrun) | |
| `fs`, Full Speed, EP 1 OUT, 1 ms | 1,000 | 995 (3,045 TDs in 3.06 s) | 683 (1.143 ms) | 1 or 2 | 12, in the first 3 s | 8 of 8, `elapsed_ms=3062` |

The simulator counted the same 24,122 OUT packets for the High Speed
play. In all three streams the TDs started in about 700 groups a
second, most about a millisecond apart (gaps of 1 or 2 ms). The 1-ms
endpoints, on the same device, driver and guest, ran at real time,
because a 4-ms window covers that gap. The 125-us endpoint ran at about
two TDs a group, a quarter of real time, which is what the reading
above predicts if the emulator ran about once a millisecond, as the
groups suggest. Its ring ran empty only in one 0.2-s
burst (four Underrun events in 11.7 s); for the rest of the play the
driver had the next TD queued whenever the emulator looked, so the
ring's depth was not the limit. The same reading fits the result that
`uac2.md` records for the three ALC4080 outputs played at once, each at
the slow rate, because each endpoint has its own timer and window. It
is also consistent with the real X4's 6 of 8, a device that needs 8,000
packets a second fed at this rate until `UACWAV`'s wait ends; that was
not traced here, and `uac2.md` also records one re-enumeration by the
passthrough during those runs. Evidence: `ev\r1-play1-rate.txt`,
`ev\r1-fsplay-rate.txt`, `ev\r1-qtrace-full.log`, `ev\r1-x4a.jsonl`,
`ev\r1-fs.jsonl`, `shots\r1-play1.png` and `shots\r1-fsplay.png`.

**Verdict.** The cause is the emulator's. Its SIA pacing serves a High
Speed `bInterval` 1 isochronous endpoint at about two TDs each time it
gets to run late and never catches up, and on development host A the
traced TDs started in groups about a millisecond apart. **Neither a deeper ring nor Frame IDs
helps within the specification.** A deeper ring does not, because the ring
was not the limit. The one path on which the emulator does catch up, a
Frame ID on every TD, is not allowed on a controller without CFC, and
the emulator is one; ASAP requests are published with SIA by design
(`hcdIsoPublish`: a Frame ID from the driver's estimate of the
stream's position would open a gap or miss a service whenever the
estimate is off by one; SIA cannot be). Extra doorbells are within
the specification and would give the emulator more passes; a pass that
comes within four Intervals of the last TD runs every TD then due, so
a doorbell every half millisecond or less while a stream runs could let
it keep time. That is beyond what a timer on these systems gives, and
it would cost real hardware a doorbell write every half millisecond per
stream for an emulator's sake: not tried, and declined. A `qemu`-flavour hook that stamped Frame IDs everywhere would
change the driver's behaviour only where no published flavour runs, and
it is not proposed either.

**What a real controller does.** It services the ring at every ESIT of
the endpoint from its own frame schedule, SIA TDs back to back, with no
dependence on the host's timers. A 125-us endpoint gets 8,000 TDs a
second while the driver keeps the ring ahead, which the pool ring (7.75
ms at one TRB a TD) and the page (15 ms) do with `uac2`'s requests. That
is not read here. The bench reading owed is a High Speed UAC 2.0 device
(the Sound Blaster X4) on a real xHCI controller under XP and 98 SE with
`uac2`: `UACWAV play 2 16 48000 3` timed (about 3 s and 8 of 8
expected) and, on XP, two outputs at once. Until then, the emulator's
High Speed isochronous timings say nothing about the driver's.

**Left.** The bench reading above. The change that would make the
emulator keep time is in `qemu-xhci`'s SIA target: take the previous
target plus the Interval, however late, while the ring has not run
empty, up to a bound. That change lies outside this repository.

### 45.36, the second reading: under the gdb stub, cold starts, and a stock comparison (2026-10-30, partial)

`3.4.0.0` merged first (`28900e16`). The trials of the first reading
went on with a breakpoint on the faulting instruction (`0xBFF72170` on
98 SE, `0xBFF62170` on ME) through QEMU's gdb stub, every hit logged
with its ESP and whether the page at ESP read; a stop screen is told by
its 720 x 400 text mode in the screenshots taken every 30 seconds. The
scripts are the worker's (`gdb36.py`, `cold.sh`, `trial2.sh`), not
tracked.

**What the instruction is.** Every hit has EAX to EBP zero and ESP at a
stack's top less 0x48 (`xxxxFFB8`): it is the first ring-3 instruction
of a new thread, popping the frame its creator built. A boot of the
`3.3.0.0` base hits it 27 times on 98 SE and 44 to 45 times on ME; the
stock base of the same layout (no xHCI98 installed, `s98`) 21 times.
The six more on 98 SE are threads whose stacks are in the shared arena
(`82Dxxxxx` to `83xxxxxx`), as `PsCreateSystemThread`'s are: by the
source, with selective suspend off (its default), one service thread
per xHCI controller (two here, `hcd_ctl.c`), the connection manager's
main thread (`cm_thr.c`), its configuration-manager worker
(`cm_cfgwk.c`) and one call worker per USB4 port (two, `cm_callwk.c`).
Stor98 creates no thread (corrected in the third reading below; this
paragraph first said Stor98's).
The page fault of the stop is therefore a new thread whose first stack
page is not present when it first runs.

**The rate, with the first reading's.**

| Run | Starts | Stops |
|---|---|---|
| 98 SE, first start after the update | 7 (`u1` to `u6`, `u8`) | 1 (`u5`, 0E at 019F:BFF72170) |
| ME, first start after the update | 3 (`um3` to `um5`) | 0 |
| 98 SE, cold start of the `3.3.0.0` base, no update | 30 (`c1`, `c2`, `r1a`/`b` to `r14a`/`b`, under the stub from `r8`) | 0 |
| ME, the same | 15 (`cm1`, `r1m` to `r14m` but `r7m`, `um1`) | 1 (`um1`, 0E : 01A7 : BFF62170, before any update) |
| 98 SE stock, cold start, same layout | 8 (`sk1` to `sk8`) | 0 |

`r7m` was stopped by the first version of the script at a page the
stub could not read, a page the VMM faults in as it should (the next
version steps over such a hit and goes on); it is left out.

**What this says.** The stop is not specific to the update: ME stopped
the same way at a plain cold start, which is ME0E.1's text and address.
It is rare (2 of 55 starts with xHCI98 installed, 0 of 8 stock), too
rare to tell the enclosure's or Stor98's part in the hours this reading
had, and the run under the stub (21 starts from `r8`) never stopped. The
stub changes the timing of every new thread's first instruction (each
hit stops the machine), so a race at thread start would hide under it.
The six system threads this package adds are the plausible exposure; a
thread's first stack page not being present is the Windows 98 kernel's
thread creation, not code of this package, unless a creation passes a
stack size or starts a thread at a moment the kernel does not support.
Not shown either way.

**Owed.** The comparison with the enclosure and Stor98 left out, and
the stock layout, each over enough starts for a rate (tens, unattended);
the creation calls' arguments and timing on 9x (`hcd_ctl.c`,
`hcd_tmr.c`, `hcd_susp.c`, `cm_thr.c`, `cm_cfgwk.c`); then
the cause, or the known issue. A text for the release notes if it stays
unexplained, for the owner's choice: "Windows 98 SE and ME, rarely: the
first start after the install or an update (and, on ME, any start) can
stop with 'A fatal exception 0E has occurred at 019F:BFF72170' (98 SE)
or 'Error: 0E : 01A7 : BFF62170' (ME), then a black or empty screen.
Turn the computer off and on again; the next start works. The cause is
not known." **Partial.**

## 45.31 the connection manager's unload under Driver Verifier (2026-10-30, the `xhcicm.sys` half)

## 45.31 the connection manager's unload under Driver Verifier, and Stor98 on a drive `stornvme` ran (2026-10-30)

The `xhcicm.sys` unload stop that 45.11's reading found (bug check 0xC4,
0x62, at the uninstall on 8.1 and 8 and during the boot on 7 x86) and
what it hid; and Stor98's start on 8.1 on a drive `stornvme` ran, the
task's other half (below).

### The reading

- 7 x86, `3.3.0.0`'s release `xhcicm.sys` under Verifier, the ORICO at
  boot: bug check 0xA (0x827E9FB7, 2, 8, 0x827E9FB7), `ntkrnlpa.exe`
  code executed at `DISPATCH_LEVEL` from `xhcicm.sys` `+36B1E`, which the
  release image's disassembly places in `cmQNt` (`IoIsWdmVersionAvailable`)
  called from `CmQueryPublish` under the query device's lock: fixed by
  `3967a033` (45.11, merged), here a page fault where the routine is
  pageable. Read from the minidump, exported over COM1 and parsed on the
  host.
- The 0x62: `cm.nvlink.queries` 1, then every device of the driver
  removed (the boot's device installs on 7; the uninstall on 8 and 8.1)
  and the driver unloaded. The one allocation of the driver that outlives
  its devices is the link-state table, which `stor98cm.h` interface 2
  never frees once a stor98 holds it; its R4 asks the CM to stay loaded
  for the boot, and on NT the driver had an unload routine.

### The fix

- `1ecdd85a`: on NT the unload frees a table no stor98 received.
- `ec1b21f0`: on NT the load that registers the callback takes its own
  `DriverUnload` away before the registration (restored if it fails), so
  it stays loaded for the boot and its table is never left behind;
  `DriverEntry` sets the unload routine before `CmNvlinkInit` and no
  more. Reviewed and revised.

### The legs (2026-10-30)

The gated `qemu` build of `ec1b21f0` (x86: "BUILD + GATES PASSED (debug
release qemu)", host tests passed; `xhcicm.sys` `50457b54...`), copied
over the `3.3.0.0` release package's `xhcicm.sys` after its install
(`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`), Verifier's
standard checks on `xhcicm.sys`, q35 with the ASM4242 model and the ORICO
at boot, fresh overlays `k7b` and `k81b`.

| System | The first boot after the install | The uninstall |
|---|---|---|
| 7 x86 | the NHI's driver installed at that boot: `cm.nvlink.queries` 1, `cm.stop` 2, `cm.start` again, the driver resident, no stop; ORICO (D:) on Stor98 | exit 3; the NHI stopped and started three more times, Stor98 asking again (`cm.nvlink.queries` 2 after the first restart), no stop; the restart after it clean |
| 8.1 x86 | `stornvme` first, the mark service's 8.1 arm by the class model, Stor98 running, ORICO (E:); no stop | exit 3; `cm.stop` 2 three times with the table held, no stop; the restart after it clean |

With the prototype of 45.11 and these two commits (7 x86, `k7a`): the
same boot and the uninstall with no stop (`runs/run-45.md`, "45.11").

### Stor98 on a drive `stornvme` ran (8.1 x86, 2026-10-30)

The failure 45.11's reading met (problem 10, the port's event 4 "Driver
detected an internal error in its data structures") reproduced on a fresh
q35 8.1 x86 overlay (`k81b`, the ORICO at boot): the drive on `stornvme`,
Stor98 installed over it by `DIF_INSTALLDEVICE`, a cold boot, problem 10.

- **The mechanism.** `stornvme.inf` writes `MSISupported` 1 and
  `MessageNumberLimit` 0x41 under the drive's `Device Parameters\Interrupt
  Management\MessageSignaledInterruptProperties`. With it, the drive's
  filtered requirements (`Control\FilteredConfigVector`) list interrupt
  descriptors with flags 7 (latched, message, policy) and vector
  0xFFFFFFFE, the message-signaled form; Stor98 runs under the SCSI port
  driver, which takes no message-signaled interrupt, and the start fails.
  On the same devnode, the value set to 0 and the device restarted:
  Stor98 started; set to 1 and restarted: problem 10 again. Why earlier
  starts with the value present had worked (45.11, the first boots of
  `k81a`) is not read; the restarts that failed were all after a new
  resource assignment.
- **The fix** (`a0020db6`): both NT install sections of `stor98.inf` write
  `MSISupported` 0 through a `.HW` section; `check-stor98-inf.ps1` holds
  it. The release package of that commit (x86 gated build passed),
  staged as the download's `release-x86`:
  - the failed drive (`k81b`, `MSISupported` 1): Stor98 installed again
    from the new INF, the value 0, Stor98 running; after a cold boot,
    running (problem 0, ORICO (E:));
  - a fresh overlay (`k81c`): the install (exit 3) put Stor98 on the drive
    `stornvme` ran at the first boot ("Stor98 now runs ...: recorded as
    this installer's", the installer's case 3b); after a cold boot,
    running;
  - back to `stornvme`: the uninstall (exit 3) gave the drive back to
    `stornvme`, running, with `MSISupported` 1 and `MessageNumberLimit`
    0x41 written again by its own INF.
  - the mark service's switch (`k81d`, a fresh overlay, the package
    installed with the ASM4242 and no enclosure, the ORICO first at the
    next cold boot): `stornvme` first, then "switching ... from stornvme
    (member 0200; no id on Windows 8.1 and later, by design)", the class
    model selected from the staged INF, `DiInstallDevice` success, "now
    runs Stor98", `MSISupported` 0; after a cold boot, running.

### 8.1 x64 (2026-10-30, overlay `k81x`)

The amd64 release package of `a0020db6` (gated build passed), staged
unsigned with `make-package.ps1`, its five catalogs made unsigned with
the packager's own `New-PackageCatalogFile`; in the guest its images and
catalogs signed with a throwaway certificate made there (valid 2026 to
2030, in Root and TrustedPublisher; never the project's key, nothing
signed on the host); test signing on; Driver Verifier's standard checks
on `xhcicm.sys`; the ASM4242 model with the ORICO at boot.

- The install (exit 3), then a cold boot: `stornvme` first, the mark
  service's switch, Stor98 running (problem 0, ORICO (E:)) with
  `MSISupported` 0; another cold boot: running. Verifier: `xhcicm.sys`
  loaded once, not unloaded.
- The uninstall under Verifier: bug check 0xC9 (0x226,
  0xFFFFF80037B97170, 0xFFFFCF81F9EE2DC0, 0), "DRIVER_VERIFIER_IOMANAGER_
  VIOLATION (storport.sys)": the dispatch routine the minidump names is
  `storport.sys+6170`, and no `xhcicm.sys` frame is on the stack (read
  from the minidump on the host). The drive was moving back to
  `stornvme`, whose port driver is `storport.sys`; Verifier checks that
  stack's requests because `xhcicm.sys`'s child filter is in it. Not read
  further: which request, and whether 8.1 x86's `storport.sys` behaves
  the same (the x86 uninstalls under Verifier, 45.31's legs above, did
  not stop).
- With Verifier reset, the uninstall run again: exit 3, the drive on
  `stornvme`, the restart after it clean.

The stop in `storport.sys`, read from the complete memory dump (exported
from the guest, parsed on the host from the dump's own page tables): the
IRP (argument 3) has four stack locations and only the top one used,
`IRP_MJ_PNP` / `IRP_MN_QUERY_CAPABILITIES`, sent to a device object of
`\Driver\stornvme` (a controller-type device, so `storport.sys`'s
adapter object, the dispatch routine argument 2 names) whose
`DEVOBJ_EXTENSION` reads removal processed and deletion pending, attached
to nothing below, reference count 0: a request sent to the drive's
adapter after its own removal. Its unused lower locations mean it never
went down the stack, so it never reached the child filter of
`xhcicm.sys`, and the thread's whole kernel stack holds `ntoskrnl.exe`
frames and that one `storport.sys` routine, no `xhcicm.sys` frame. So the
failing request was dispatched to `storport.sys`'s removed adapter and
not through the filter; why the adapter was removed then, and whether
anything earlier contributed, is not read. The stop under the standard
checks stays an unresolved observation (below).

The unload half on 8.1 x64, read on a second fresh overlay (`k81y`, the
same vehicle) with Verifier's pool checks alone on `xhcicm.sys` (special
pool and pool tracking, flags 0x9): the install, a cold boot with the
ORICO (the mark service's switch, Stor98 running), the uninstall (exit
3) with no stop; `xhcicm.sys` loaded once and not unloaded (it stays for
the boot once a stor98 holds the table, `ec1b21f0`); the restart after
it clean. A second install on `k81x` over the first was refused by the
installer (exit 1, the x64 connection manager's test-signing gate), so
`k81y` was a fresh overlay.

So on 8.1 x64 both halves of the task hold: Stor98 starts, and the
unload leaves no allocation behind.

### Owed

Nothing of 45.31's scope. Left open: the stop in `storport.sys` at the
8.1 x64 uninstall under Verifier's standard checks on `xhcicm.sys`
(location read, cause not).

### 45.13 (continued): 2000 read, and the chained unit's own `Removable` (2026-10-30)

**2000** (the reading the owner's question waited for). A 2000 SP4
golden made like XP's: an overlay of the q35 2000 base (Standard PC
HAL), the launcher's `-Usb4 tb3ar`, nothing attached; the release x86
package of `1a2a72ca` installed with `/thunderbolt /stor98` (exit 0,
the Found New Hardware wizards answered), a cold second boot
(`XHCIINST /devices`: the Alpine Ridge controller, tunnel port
`02:01.0`); kept read-only (`cd1b7ecf...`). Each leg a child overlay
with the dock `enc0` at power-on (`preboot=on`, `downstream-ports=1`,
`chain-power-controller=off`); the dock's first boot answered its
wizards (Stor98 for the dock's NVMe, E: "ORICO") and was kept as the
legs' starting state (`dockinst.qcow2`, `947a92b7...`); the chained
device `usb4-device ... parent=enc0,port=1` plugged by QMP at the
desktop. Evidence: `E:\xhci98-vm\p45\w45-chain\w2k\`
(`serial-<leg>.log`, `debugcon-w2b.log`, `trace-<leg>.log`,
`infopci-w0c*.txt`, `w0c-serial.txt`, screenshots).

| Leg | Build | Read |
|---|---|---|
| w0b, w0c | the golden's release `xhcicm.sys` (`d012fccf...`, no chain action) | the child approved at depth 2; 2000 found the chained NVMe itself (a Found New Hardware wizard for it, Stor98 bound); the wizard's restart hung in real mode (EIP `0d4f`, the base's known warm-restart hang), so a cold start (w0c); the child plugged again: Stor98 at **Code 10**; unplugged and plugged again: gone, then Code 10 again; the dock unplugged with the child attached: every devnode of the dock gone, no hang; shutdown in 6 s |
| w2b | the qemu flavour at `38ac4e18`'s source (`77f34053...`, with the chain action), swapped in, a cold start | the chain port's `BusRelations` reported the new PDO (`cm.chain.rel=01040508`, one new, the child filter attached) before the action's `cm.chain.invalidate=01020508`; Code 10 as in w0c; the child's unplug: `cm.chain.invalidate=02020508`, gone; the dock's unplug with the child: all gone, no hang; shutdown in 5 s |

**Why Code 10 on 2000.** The chained NVMe's requirement list
(`BasicConfigVector`, read from the registry) holds its memory BAR and no
interrupt, where the dock's NVMe, numbered by SeaBIOS, has IRQ 10;
`info pci`: the chained function's IRQ 0, its BAR unmapped. 2000 on the
Standard PC HAL gives a hot-added function no interrupt, and the child
filter's interrupt step (record 21 section 6.2.1, task 44.53) declined
it: `cm.chain.irq.no=00020700`, why 2, the tunnel port's Interrupt Pin
not 1 to 4 (the emulated Thunderbolt 3 tunnel port `02:01.0` carries no
pin; `info pci` prints none). The chained function would also arrive on
another pin of the tunnel port (the chain port is device 1 on the dock's
internal bus: pin A becomes B), record 21 section 6.2.1's other
documented limit. So on 2000's Standard PC HAL a chained drive behind the
emulated dock is found and does not start; that is the platform's
interrupt routing, not the chain's discovery. Reported to the
coordinator (the emulator's tunnel port has no pin; on real hardware the
other-pin limit applies to a chain port at an odd device number).

**What 2000 shows.** As on XP (x0, x1): the member's invalidation alone
reached the chain port's bus; the action's call came after the PDO was
already reported. So on all three systems read (7, XP, 2000) the chain
action is a second request. The owner kept it (below).

**The chained unit's `Removable`** (record 34 section 3.10, reviewed and
revised before and with the code): the chain port is classified by its
own child filter at each of its `BusRelations` answers, before the answer
goes up (`cmChainClassify`, `XhciTbtChainPortTest`); its children get
marks entries with that chain port as their landing (`CmMarksBury`,
`CmMarksBuryLandings`, `CmMarksMake`, two phases); the `Removable` gate
compares a chained unit's bus with its chain port's secondary bus; a
chained entry is never confirmed as a member eject
(`CM_MK_CONF_CHAINED`); the answer carries `CM_QM_EF_CHAINED` and the
landing at offset 40.

The code is `0751ad83` (with the host vectors: `test_tbt_topo` 715
checks, `test_cm_marks` 221, 0 failures each); `b83a5d34` adds three
fixes from the integration review of 45.35's isolation interface (the
first reference taken with the binding under the registration list's
lock, every later one and the release under it; the bindings to a
domain voided when it is delisted; a comment).

**Builds.** The gated x86 build of `0751ad83` and of `b83a5d34`
(`scripts\build-driver.cmd all`): "BUILD + GATES PASSED (debug release
qemu)" each (`E:\xhci98-scratch\p45-w45-chain\gb-x86-0751ad83.log`,
`gb-x86.log`); the gated amd64 build of `b83a5d34` (`all -amd64`):
the same ("BUILD + GATES PASSED (debug release qemu)",
`gb-amd64.log`); the host suite: "Host tests PASSED" (`hosttests.log`). The release `xhcicm.sys` of `b83a5d34` `a101d05f...`,
`xhci98.sys` `cc189335...`.

### The legs of section 3.10 (2026-10-30)

| Leg | System, build | Read | Verdict |
|---|---|---|---|
| x3, x3b | XP, the XP golden, a qemu-flavour diagnostic build of the uncommitted section 3.10 code (`95857dde...`), swapped in | `cm.chain.class=00070508` (the chain port `05:01.0`, secondary bus 7); the chained Stor98 answered `Removable` (`cm.chain.removable=21070508`) and listed on its own in Safely Remove Hardware ("at PCI bus 7"); a file written to it, the unit stopped from the applet ("Safe To Remove Hardware"), the child unplugged and plugged again: the file there (`fc`: no differences), the dock's drive untouched. In x3, the session in which the chained device was installed for the first time, the applet did not list it | diagnostic |
| x4 | XP, the qemu flavour of `b83a5d34` | the chained device plugged for the first time (never seen): `cm.chain.removable=21070508` at that plug | diagnostic |
| x5 | XP, the gated release `xhcicm.sys` and `xhci98.sys` of `b83a5d34` swapped into the golden | the dock's first boot (Stor98 by its wizard), a restart, the chained device plugged for the first time: Stor98 by its wizard; Safely Remove Hardware lists "Stor98 Storage Controller for NVMe SSDs" on its own ("at PCI bus 7, device 0, function 0") beside the dock's bridge; a file copied to F: and compared; the unit stopped from the applet (its volume F: and its disk in the confirmation, "Safe To Remove Hardware"); the child unplugged and plugged again: the file there (`fc`: no differences), D: (the dock's drive) there; the dock unplugged with the child attached: no hang; shutdown in 17 s | pass (section 9.3 clause 5 on NT; the lost write of x1 and x2 avoided by the eject) |
| w3, w3b | 2000, the dock-installed state, the gated release of `b83a5d34` swapped in, a cold start; emulator `usb4-emu` `b84be72` (`qemu-system-x86_64.exe` `7ff0d230...`: the Thunderbolt 3 switch ports report Interrupt Pin A, the tunnel port padded) | `info pci`: the tunnel port `02:01.0` IRQ 10, pin A, buses 4 to 40h; the chained device plugged: Stor98 by its wizard ("will not work until you restart"), the chained NVMe at Code 10, IRQ 0, BAR unmapped; the "Unplug or Eject Hardware" applet lists the chained Stor98 on its own; the child unplugged, then the dock: nothing left behind the tunnel port, no hang; shutdown in 7 s | the listing: pass. The interrupt: with the tunnel port's pin now present, the chained function still gets none, as its interrupt arrives on pin B of the tunnel port (the chain port is device 1 of the dock's internal bus; record 21 section 6.2.1's other-pin limit) |

Evidence: `E:\xhci98-vm\p45\w45-chain\xp\` (`serial-x3*.log`,
`debugcon-x3*.log`, `debugcon-x4.log`, `serial-x5.log`, `x3b-*.png`,
`x5-*.png`) and `w2k\` (`serial-w3b.log`, `infopci-w3b.txt`,
`w3b-*.png`).

**The owner's decisions** (2026-10-30): the chain action is kept (Q1
(a)); the correspondence learning, the per-tunnel ownership, names and
generations and the status record's kind 3 entries move to 45.15, Part
3, where a chained tunnel of the software connection manager gives them
a reader (Q2 (a)); a chained drive on 2000's Standard PC HAL that gets no
interrupt is a release-notes limitation, and the emulator's Thunderbolt 3
tunnel port gets an Interrupt Pin (`usb4-emu` `b84be72`, read in w3b
above) (Q3 (a)).

**45.13 is done** with that: the chain action (section 3.9) and the
chained unit's own `Removable` (section 3.10). What was read: on 7 x86
the discovery and removal of a chained device, on the release of
`1a2a72ca` before either action (r2); on XP the discovery without and
with the chain action (x0 to x2) and, on the gated release of
`b83a5d34`, the chained unit listed, stopped from the applet and a file
kept through the unplug (x5); on 2000 the discovery without and with the
action (w0b to w2b) and, on the gated release, the chained unit listed
on its own (w3b; it does not start there, so no stop was read). The
applet's stop on 2000 and 7 is not read. 45.17's PCI-side clause 5 (safe
removal of the chained unit from the system's applet) is read on XP;
from XHCITR it waits for record 25's chained unit (leg T.11).

The unload half on 8.1 x64 under Verifier, behind the stop in
`storport.sys` (which request, and whether it is the filter's doing);
the same uninstall on 8.1 x86 was read with no stop.

## 45.22 ring interrupts for the connection manager: the code (2026-10-30)

The owner's answers to record 21 section 4.5.10 (2026-10-29: Q1 (a) coded
in this release behind `XhciCmRingInterrupt` 0, Q2 (b) 0 everywhere until a
hardware reading, Q3 (a) the line first, Q4 (a) the idle period unchanged;
98 SE and ME only after their route is read) coded on branch `w45-irq2`
from `2a86c41d`. Development host A.

### What was coded

Record 21 section 4.5 for both profiles, as section 4.5.11 (new) records
where the code settles what the design left open; record 22 section 5.4
and section 1.3's two lines, record 21's section 8 table and decision D3's
row brought up to date. Files: `src\cm\cm_rirq.c` and `.h` (the pure
half), `cm_rirqos.c` and `.h` (the service routine, its DPC, the
connection, the arm and mask under `KeSynchronizeExecution`, the self-test
through Interrupt Status Set, the watchdog, the orders down and up),
`cm_thr.c` (the wait to the maintenance deadline, the `CM_WORK_RING` wake,
`CmCfgIntxRetire`), `tbt_ring.c` (IE in RX descriptors, `TbtRingRxPending`),
`tbt_icm.c` `TbtIcmRingWake`, `tbt_u4cm.c` `TbtU4RingWake`, `tbt_regs.h`
(ISC, ISS, IMS, ITR[0], bit 17), `cm_nhi.c` and `cm_power.c` (the intent
before every ring start, the order down first in every stop and
quiescence), `cm.h` (`CM_WORK_RING`, the field), `cm_vals.c`, `cm_reg.c`
(`XhciCmRingInterrupt`, no INF writes it; the qemu flavour's
`XhciCmQemuIrqDead`), the five interrupt pairs in both `xhcicm` allowlists
(`xhci98.sys`'s rows, evidence verbatim), `docs\using\tuning.md`'s row.

Host vectors: `test_cm_rirq` (new: the cause for every Total Paths, the
routine's outcomes, the storm count, the mask by contract and its proof,
the watchdog's suspicion and slower storm, the order down's end and latch,
the waits across the clock's wrap), `test_tbt_ring` (IE from the initial
post, kept by a repost, dropped after the retirement; the peek takes
nothing), `test_u4cm` and `test_tbt_icm` `test_ring_wake` (a plug served
by one wake with no tick between; the wakes end; a check already owed is
the tick's), `test_cm` (the value's rule).

Commits: `d311d97b` (the code), `1098f0f6` (reviewed and revised: a
retained interrupt's geometry published only before the connection or
under the interrupt's lock; an armed interrupt taken down before every
ring start, since a full recovery can restart the rings within one pass;
the maintenance deadline the last pass plus the period in force),
`addee4bb` (the wakes' vectors). Reviewed and revised in three areas
(IRQL and lifetime, the lifecycle, registers and supporting files) until
nothing material was left.

Builds at `addee4bb`: the x86 gated build `all` passed
(`out\gated-x86-release.txt`; `xhcicm.sys` release `550c3e18...`, qemu
`bbb1a272...`), the amd64 gated build `all` passed
(`out\gated-amd64-release.txt`; `xhcicm.sys` release `88991790...`,
unsigned as a clone without the key stages it), `src\pfw\build.cmd` once
before; the host suites passed (`test\run-host-tests.cmd`).

### 98 SE and ME: the NHI's route (record 21 section 4.5.4's reading)

On fresh overlays of the q35 bases, no xHCI98 installed, `bin\asm4242-20261025-2230`
as the launcher pins it, one vCPU: Device Manager, Computer's View
Resources (IRQ) and the NHI's own Resources page, read by QMP screendump
(`E:\xhci98-vm\p45\w45-irq2\r98t`, `r98a`, `rmet`, `rmea`).

| System | Profile | The NHI as Device Manager names it | Allocated | Sharing its IRQ |
|---|---|---|---|---|
| 98 SE | `tb3ar` (Alpine Ridge behind its switch, root port `00:08.0`) | "PCI System Peripheral", Code 28 (no driver), basic configuration 0000 | IRQ 10, memory `FE400000-FE43FFFF` and `FE440000-FE440FFF` | the SMBus, the ICH9 AHCI ("PCI Card"), two PCI-to-PCI bridges, one of the two xHCI controllers, an IRQ holder |
| 98 SE | `asm4242` (the NHI behind the chip's own switch) | "PCI Universal Serial Bus" (class `0C0340`), Code 28 | IRQ 11, memory `FA800000-FA83FFFF` and `FA840000-FA84FFFF` | one PCI-to-PCI bridge and an IRQ holder |
| ME | `tb3ar` | as on 98 SE | IRQ 10, the same ranges | as on 98 SE |
| ME | `asm4242` | as on 98 SE | IRQ 11, the same ranges | as on 98 SE |

So on q35 both 98 SE and ME give the NHI an IRQ under both profiles,
shared and level (PCI). Not read: the configuration space's Interrupt Line
and Pin, and whether the IRQ is the one the pin raises, which only the
self-test can say (section 4.5.4). The code connects nothing on 98 SE and
ME (`cm.irq.9x`); their connection and its first leg stay owed.

### The legs on q35 (2026-10-30)

Vehicle: fresh overlays of `E:\xhci98-vm\q35-bases\w7x86` and `xp`, the
launcher's pinned emulator (`bin\asm4242-20261025-2230`), TCG, 2 vCPUs,
with `-msg timestamp=on` and the trace events `usb4_host_rx_done`,
`usb4_host_frame_decode`, `usb4_host_mmio_write` and `usb4_host_irq_intx`
(host clock). Install set: `releases\3.3.0.0`'s `INSTALL.BAT` and tools with
its `release-x86` replaced by this tree's `make-package.ps1 -Flavor release
-Arch x86` output unchanged (`xhcicm.sys` `550c3e18...`, `xhci98.sys`
`4022c19a...`; the owner, 2026-10-30: these legs count), `INSTALL.BAT
/console /yes /nopause /thunderbolt /stor98`, exit 3, restart. The qemu
phases replace `xhcicm.sys` alone by `make-package.ps1 -Flavor qemu`'s
(`bbb1a272...`), whose log reaches the host through port `0xE9`, each
line stamped on the host clock as it lands. Values set in `Services\USB`
(`reg add`), each followed by a restart. Evidence:
`E:\xhci98-vm\p45\w45-irq2\w7t` and `wxa` (`serial-*.log`: the XHCITR
reports and `typeperf`; `cpu.txt`; `plugs-*.txt`; `dbg-*.txt`;
`trace-*.log`; `seq.log`), the scripts in `kit\`.

**The driver loaded is this build** on both: `cm.irq.value` and
`cm.irq.resource` in each report (7 x86, Alpine Ridge under the ICM
client: a line at IRQL 10, shared, level; XP, the ASM4242 under the USB4
profile: IRQL 5, shared, level).

**Value 1, 7 x86 `tb3ar`**: `cm.irq.connected`, `cm.irq.armed=00001000`
(bit 12 of 36 causes: RX ring 0 for Total Paths 12), `cm.irq.selftest`
passed (10 ms at the first start, 203 ms on a slower boot); the drive came
up behind the tunnel as with value 0.

**Hot-plug latency, 7 x86 `tb3ar`, ten plugs of the TB3 enclosure each**
(QMP `device_add` at 70 s intervals, `device_del` 40 s after each; record
21 section 4.5.9's two figures):

| Value | The plug's own event frame consumed after the plug (`RX0.PROD_CONS` naming its descriptor) | Connection done (`cm.action.tunnel-up`) after the plug |
|---|---|---|
| 0 (poll, 200 ms) | median 170 ms, max 201 ms | median 206 ms, max 225 ms |
| 1 (the interrupt) | median 2.0 ms, max 3.3 ms | median 25 ms, max 38 ms |

Each plug's own frame is the first frame of its type (`ICM_EVENT` here,
`HOTPLUG` below) the emulator wrote from 5 ms before the plug's stamp on
(the QMP script's clock and QEMU's trace clock differ by up to a
millisecond) to 10 s after it, and its descriptor; every one of the
twenty plugs matched its own (`kit\lat.py`). The emulator wrote it 0 to
2 ms after the plug.

**The same, XP `asm4242` under the USB4 profile, ten plugs of the ORICO
enclosure each** (value 1: `cm.irq.connected`, `cm.irq.armed=00200000`,
bit 21 of 63 causes for Total Paths 21, `cm.irq.selftest` passed):

| Value | The plug's own hot-plug packet consumed after the plug | Connection done (`u4.action`) after the plug |
|---|---|---|
| 0 (poll, 200 ms) | median 158 ms, max 213 ms | median 2370 ms, max 2554 ms |
| 1 (the interrupt) | median 4.5 ms, max 6.9 ms | median 2245 ms, max 2359 ms |

Under the USB4 profile the connection is the enumeration of section 6.4,
about 2.2 s here, so the interrupt's gain (one poll's wait) is a small
part of it; the wake's port check ran the whole connection (the tick did
not have to), and no wake fed another (`TbtU4RingWake`).

**Idle cost, 7 x86 `tb3ar`, release flavour, ten minutes each**: the QEMU
process's host CPU, and the guest's `% Processor Time` and
`Interrupts/sec` (`typeperf`, 10 s samples).

| Configuration | Nothing attached: host / guest / interrupts per s | Drive attached, idle |
|---|---|---|
| value 0, period 200 ms | 65.2 % / 26.2 % / 152 | 65.0 % / 27.5 % / 140 |
| value 1, period 200 ms | 63.1 % / 25.7 % / 135 | 62.9 % / 25.1 % / 134 |
| value 0, period 50 ms | 67.2 % / 26.9 % / 148 | 60.2 % / 24.3 % / 134 |

Within the noise of a TCG guest sharing the host (other workers' guests
ran beside it): neither the interrupt nor the 50 ms period shows an idle
cost there, on this guest and profile only. XP `asm4242`, value 0: 102.6 % / 31.1 % / 130 with nothing
attached, 102.9 % / 31.5 % / 129 with the drive.

**A lost interrupt, 7 x86 `tb3ar`, qemu flavour, value 1,
`XhciCmQemuIrqDead` 12**: the line died after its twelfth claim during
the second plug. The plugs after that were still served, by the
maintenance pass (connection done 160 ms after the fourth plug, against
25 to 49 ms before), and the drive came and went each time: a lost
interrupt costs one idle period, today's latency. The latch to polling
for the activation (two suspected misses in a row, the self-test,
`cm.irq.lost`) was not reached: the ICM client's events are single frames
70 s apart here, so no two maintenance passes in a row found an unannounced
frame. That path rests on `test_cm_rirq`'s watchdog vectors.

**Not read on a guest**: the storm stop (no emulator storm), the
retained interrupt (no mask that does not latch), a D3 and resume with
value 1 (no sleep leg), Windows 2000, Vista, 8 to 10 and the x64 build.

### What is left

The task stays unticked: its line connects the interrupt on 98 SE and ME
too once their route is read, and that connection (and its first leg,
the self-test on those systems) is owed - the route is read above. Also
owed: the latch to polling read on a guest (it needs an `usb4-emu` INTx
drop on `usb4-host`, or a host with denser events); section 4.5.9's
no-interrupt leg (`usb4-host` `intx=off`, `cm.irq.none`) and shared-line
leg (the NHI on another device's line, traffic on that device); the USB4
profile's idle cost at value 1 and at a 50 ms period (XP was read at
value 0 alone); the readings of the "Not read" list; and the hardware
reading at 45-E.5 (the P14s's Alpine Ridge INTx, the self-test first). The
default stays 0 (owner, Q2).

## 45.15 record 34 Part 3, the no-VM part: the design read against the code, the host vectors (2026-10-29)

Design record 34 Part 3 (section 6, the USB4 software connection
manager's minimal chain at depth 2) read against `src/cm/tbt_u4cm.c` and
`tbt_u4cm.h` at `3e314cde`, on development host A; no virtual machine,
no QEMU, no `usb4-emu` run.

**Verdict: the design holds with seven additions (record 34 revision 10 after the merges into `3.4.0.0`, numbered 5 when written);
none of its code exists yet.** `tbt_u4cm.c` has no `TBT_U4_KID` and no
`TBT_U4_VIEW`: below depth 1 it has only the watched `TBT_U4_LINK`
path (`tbtU4DiscoverLinks`, `tbtU4CheckLinks`, `tbtU4RemoveLink`), a
depth-2 router plugged later counted and left locked. The reading
against the code found what Part 3 did not yet say:

1. **Whose sequence a kid's is.** `ActivePort` makes a depth-1
   sequence its port's own (`TbtU4Eject`, `tbtU4Clean`,
   `tbtU4PcieLoss`, since `2f57cad2`); a kid's sequences now run as
   their host port's, so the parent's teardown never nests under them,
   and a kid's own unplug is not nested before 45.18 admits statements
   at depth 2 (section 6.2).
2. **A kid's own unplug hint confirmed by evidence**, as `tbtU4Live`
   confirms the active port's, so a late event never cancels a healthy
   connect (section 6.2).
3. **Which check a notification serves.** `tbtU4TakeQueues` hands every
   device router's ERR_CONN or ERR_LOCK to the host port as its unplug;
   one naming a kid's parent lane is now the kid's, a plain check only
   for the host port (section 6.5).
4. **The parent's Disable Hot Plug** cleared after the parent is UP, so
   depth 1's writes stay byte for byte (section 6.5).
5. **Links below a kid** dropped with it, their routes forgotten
   (section 6.7).
6. **Adoption from a link only when the link carries nothing else**, and
   the kid's teardown followed by `tbtU4RemoveLink`'s sweep as a residual
   pass: a kid closes only its PCIe tunnel, a watched link's removal
   every path (sections 6.7 and 6.9).
7. **The arrays the kids grow**: the obligations, the resume bound's
   count and the carry (section 6.9); and the one vector of today that
   expects a depth-2 plug left uninitialised changes with the code
   (section 9.1).

The revision was reviewed and revised before its commit (`bb3434c7`).

### The fake fabric and the vectors

- `test/u4_fakefab.c`, `.h`: protocol adapters paired below depth 1 (a
  device router's Nth down adapter with the child on its Nth downstream
  port, Spec 5.2.5); a device router's power loss setting Lock on its
  downstream lanes and resetting the routers behind them, still attached
  (Spec 3.3), the host-only reset kept local, no Lock on a Thunderbolt 3
  router's lanes; `FAB_DEV_TBT3_DOCK`, the Thunderbolt 3 dock with a
  downstream port as `usb4-emu`'s `usb4-router-tb3.c` device router has
  it (lanes 1 to 4, PCIe Up 6, PCIe Down 7, the latter's LTSSM field
  fixed at 3 as the emulator reads it); `fabHotPlugChild`,
  `fabHotUnplugChild`, `fabPreosChild`, `fabDownLane0`,
  `fabChainPcieUp` (the four entries chained and both Path Enables) and
  `fabTriedTo` (writes sent to a route, delivered or not).
- `test/test_u4cm.c`: `test_chain_fabric` runs today (the dock at depth
  1, a hot-plugged child locked and uninitialised, the chained pair
  trained only with both Path Enables, Lock again after the parent's
  power loss, an enumerated child reset by its parent's unplug): 1,375
  checks, 0 failures (1,362 before). Eleven vectors under
  `#ifdef TBT_U4_MAX_KIDS`, written against section 6's names and
  compiled only once `tbt_u4cm.h` defines it: the connect behind a USB4
  parent and behind the dock (sections 6.4 and 6.6, all four entries,
  bonding before Configuration Valid, the child's Path Enable first),
  the refusals (a Thunderbolt 3 child, no PCIe Down, Detect not reached,
  no PCIe tunnel on the parent), a write never answered while the paths
  are written or enabled and a drain that never completes (undone or
  quarantined under the kid, the parent kept), the end device's unplug
  and a quick re-plug, a child's ERR_CONN never the parent's unplug, a
  stale unplug event during a healthy connect, the middle unplug with no
  write even attempted on the departed routers with the kid UP and
  CONNECTING, a kid's unplug at another port's safe point served at the
  next tick and a depth-3 link dropped with its kid, the parent's PCIe
  loss and both ejects, adoption (a preserved chain, a half-built one,
  one with a second path, one with no tunnel, and a preserved chain
  behind the dock), and the resume (verified, also behind the dock,
  rebuilt after the parent, the parent HELD with its kid, and behind the
  dock a power cycle and the dock re-plugged with its child attached).
  Checked to compile under `/Za /W3 /WX` against a copy of the header
  with section 6's fields added; not run.

`test\run-host-tests.cmd` on development host A at `39453f0d`: every
suite passed, x86 and the amd64 legs. The vectors were reviewed and
revised until the review found nothing material.

### Left

- The code (record 34 section 6.10's `tbt_u4cm.c` and `.h` rows, then
  `cm_nhi.c`, `cm_eject.c`, `cm_ejectos.c` and `cm_status.c`, after
  45.12), until the guarded vectors pass with every depth-1 vector
  unchanged; then the gated builds.
- The `usb4-emu` cross-check on `CHAIN=1` (record 34 section 9.2,
  `scripts\usb4-crosscheck\u4cm-crosscheck.c`'s `test_chain`).
- The q35 legs on 98 SE to 10 x64 (record 34 section 9.3).

## 45.15 (continued) the minimal chain coded, reviewed, built, and read on 7 x86 and XP (2026-10-30)

Worker w45-u4chain2, development host A. Record 34 Part 3 coded against
today's tree, the guarded vectors enabled, and record 34 section 6.11
written as the record of what the code does where it differs from
sections 6.1 to 6.10 (revision 11 after the merges into `3.4.0.0`).

### The code

- `src/cm/tbt_u4cm.h`, `tbt_u4cm.c`: `TBT_U4_KID` in `TBT_U4CM.Kids`
  (`TBT_U4_MAX_KIDS` 4 a domain); the kid connect (a USB4 parent's lane
  unlocked, the child read, enumerated or taken as found, its DROM,
  adapters, capabilities, Get Capabilities and Buffer Allocation; bonding
  through the parent's downstream lanes with the verified single-lane
  fallback; the parent's port marked configured, `PORT_CS_19` or a
  Thunderbolt 3 parent's link controller; PTO, the USB3 adapters
  disconnected, CV and Configuration Ready; the Detect gate, the four
  chained entries, the child's Path Enable first, L0 behind a USB4
  parent); the idle tick's kid service (checks from the registers, the
  parent's Disable Hot Plug, adoption, connect); the end and middle
  unplugs; the parent's PCIe loss and eject taking the kid's tunnel down
  first; `TbtU4EjectKid`; the kid's own PCIe loss; the resume obligation;
  the carry; kids back to links at a recovery; the kid index in the
  action, the link-down record, the obligation and the quarantine entry;
  error notifications attributed to a kid in the queue and at the
  request.
- `src/cm/cm_nhi.c`: the seam (a kid's action reaches the chain action at
  depth 2, keyed per kid, live by the kid; no learning, seam generation,
  member exposure or stor98 statement for a chained tunnel; seven counter
  names).
- `test/test_u4cm.c`: the eleven guarded vectors enabled, eleven more from
  the reviews; three link vectors given a half-built parent path and
  `test_links`' depth-2 re-plug changed, as section 6.11 says. 1638
  checks, 0 failures at `3ac7f071`.
- `scripts/usb4-crosscheck`: `test_chain` (record 34 section 9.2) on
  `intel-adl` behind the Thunderbolt 3 dock (cold and hot) and behind a
  USB4 router, and on `asm4242` behind a USB4 router (cold and hot): the
  kid UP at route `0x303` with the model's chained tunnel up, HopIDs 8
  and 9, bonded, the end unplug and re-plug, the parent's unplug with the
  child attached. Against a QEMU tree of v11.1.2 with `usb4-emu` main
  `fc16468`'s patch: 376552 checks, 0 failures (a reading, not a gate).

### The reviews

Every change reviewed and revised until the review found nothing
material: nine rounds, the last on `c3152fde..94568ef8` with no finding.
What the rounds found and the fixes are section 6.11's points: the
TMU-Off predicate before a lane is disabled; a Thunderbolt 3 parent's
PCIe Down chosen only when free; a request-ended notification attributed
by the reporting router's place on the request's path, a foreign one
asked again (never a router operation's launch); kid removal checks under
a FAILED or HELD parent, no kid write under one not yet classified; the
link proven idle before the parent's lanes are put back, each lane change
kept until read back undone; a connect gated on the parent's lanes being
idle; kids of a departed parent dropped at the recovery and when the port
holds another connection; a kid's quarantine and its child-side teardown
written only on the same child (its UUID).

### The builds

`3ac7f071` (one rename on top of the reviewed tip: `except` is a keyword
to the DDK's compiler): the gated x86 build `all`, "BUILD + GATES PASSED
(debug release qemu)" (`E:\xhci98-scratch\p45-w45-u4chain2\gb-x86.log`);
`test\run-host-tests.cmd` "Host tests PASSED" (`hosttests2.log`). The
amd64 build is the coordinator's after the merges. Packages
(`make-package.ps1 -Arch x86`): release `xhcicm.sys` `ca983242...`,
`xhci98.sys` `ab426a3f...`; qemu `xhcicm.sys` `afe53c1a...`, `xhci98.sys`
`d71bef37...`.

### The legs (q35; `usb4-emu` main `fc16468`)

The emulator: QEMU v11.1.2 with `fc16468`'s `src/patches` applied, built
on development host A (`qemu-system-x86_64.exe` `7961f7f8...`). The host:
`-Usb4 asm4242` of `launch-q35.ps1` (`asm-preset=asm4242-desktop`, its
pre-OS connection manager on). The chain: the parent `usb4-device
... dialect=usb4,downstream-ports=1,chain-power-controller=off` with its
own NVMe on USB4 port 1 at power-on (the pre-OS connection manager builds
its tunnel; SeaBIOS numbers its switch, the chain port `04:01.0` with one
bus, 6, while empty); the child `usb4-device ... parent=enc0,port=1` (a
plain enclosure, its NVMe directly behind the chain port: an ASM2464
child's own switch needs three buses, which an empty chain port numbered
at power-on does not have) plugged by QMP at the desktop, or with
`-Chain` at power-on. Goldens: fresh overlays of the q35 bases with the
release package installed (`INSTALL.BAT /console /yes /nopause
/thunderbolt /stor98`, exit 3), the qemu flavour's `xhcicm.sys` and
`xhci98.sys` of the same commit swapped in for the log, restarted
(7 x86 `7cb86e5a...`, XP `8bbf0a75...`); each leg a child overlay.
Evidence: `E:\xhci98-vm\p45\w45-u4chain\w7\` and `xp\`
(`debugcon-<leg>.log`, `serial-<leg>.log`, `cmdline-<leg>.txt`,
screenshots).

| Leg | System | Read | Verdict |
|---|---|---|---|
| r1 | 7 SP1 x86 | the parent preserved at the start (`u4.preserved`), the chain port classified (`cm.chain.class=00060408`); the child plugged: `u4.kid.connect`, `u4.kid.pcie.up=00010809` (hd 8, hu 9), `u4.kid.state` UP, the action with the kid (`u4.action.kid`), the chain port invalidated (`cm.chain.invalidate=01020408`) and its new child attached; the chained NVMe on Stor98, Code 0; its disk offline at first (both NVMe disks are overlays of one image: Windows' duplicate signature), put online, F:, a file copied and compared (`fc`: no differences). The child unplugged: `u4.kid.teardown`, `u4.linkdown.kid` with the path read back closed, the TUNNEL_DOWN with the kid, the chain port's child removed, the parent's D: untouched. Plugged again: UP, F: again, a file compared. The parent unplugged with the child attached: `u4.kid.dropped` first, then the port's own link-down and removal; no NVMe left, no hang; shutdown in 20 s | the child's hot plug behind a parent present at power-on (part of clause 1), 2, 3: pass |
| r2 | 7 SP1 x86 | the chain at power-on: the depth-2 link discovered and adopted (`u4.kid.adopted=00010808`, the pre-OS paths' HopID 8 both ways), nothing re-enumerated; both NVMe on Stor98, Code 0 (Windows asked a restart for the chained one's first install, not taken). The child unplugged and plugged again: torn down, connected again, F: compared. The parent unplugged with the child attached and plugged again (the emulator keeps the child attached to it): the parent's tunnel up and the parent UP first, then the kid connected; every switch port and both NVMe Code 0; shutdown in 20 s | clause 4 and the approve order: pass |
| x1 | XP SP3 | as r1: the kid connected at the hot plug, the chain port invalidated, Stor98 by its Found New Hardware wizard for the chained NVMe, F:, a file compared; the child unplugged without being stopped first: torn down, the chain port invalidated again and the NVMe removed, D: kept - and XP's "Delayed Write Failed" for F: (the file just written; a surprise unplug, as 45.13's x1 and x2 read; the Safely Remove Hardware applet's listing of the chained unit, section 3.10, was not read in this leg); plugged again: F:, a file compared; the parent unplugged with the child attached: the kid dropped first, everything removed, no hang; shutdown in 22 s | the child's hot plug behind a parent present at power-on (part of clause 1), 2, 3: pass |
| r3 | 7 SP1 x86 | the parent hot-plugged at the desktop (clause 1 as written), then the child: the parent's tunnel up and the parent UP, then the kid connected (`u4.kid.pcie.up=00010809`, UP); but the parent's PCIe switch upstream port at Code 31 (Windows could not give it bus numbers: the `asm4242` layout's tunnel port numbered at power-on with no room for a switch added later, the emulated platform's limit as 45.37 read for the Thunderbolt 3 tunnel port), so neither NVMe was reachable on the PCI side and the chain action's walk found its chain port only intermittently (`cm.chain.owed-expired`) | clause 1 on this layout: not readable (the platform); the connection manager's half pass |
| x2 | XP SP3 | as r2: adopted at the start, both drives D: and F:; the child unplugged and plugged again, a file compared; the parent unplugged with the child attached and plugged again: the parent first, then the kid, both drives back; shutdown in 17 s | clause 4 and the approve order: pass |

### Left

- Clause 1 with the parent hot-plugged at the desktop on a layout whose
  tunnel port leaves room for a switch added later (r3: Code 31 on this
  one).
- The chained unit's tunnel teardown at its eject (`TbtU4EjectKid` from
  the eject paths: the mark service's confirmation of a chained unit,
  which record 34 section 3.10 refuses today, turned into the kid's
  teardown; Windows' own eject of the chained unit, section 3.10's
  `Removable`, already stops the drive and leaves the tunnel up) and, moved into 45.15 from
  45.13 by the owner, the chain-port correspondence learning and the
  per-tunnel names and generations (section 3.5, Q4 (a)).
- The legs on 98 SE, ME, 2000 and 10 x64. On 98 SE and ME the chained
  device's PCI side is task 45.14's (not done): the legs read what
  happens without it.

### With 45.20's version 2 code (2026-10-30)

`3.4.0.0` at `66187290` (45.20's USB4 version 2 code, 45.18's design)
merged into the branch (`ae346700`, both sides kept). Its rules reached
the kid paths in three commits, each reviewed until the review found
nothing material, recorded in record 34 section 6.11 ("Version 2
routers"): a kid's Gen 4 link classified as a port's, waited for while
training, and Extended Encapsulation on a chained PCIe tunnel between two
version 2 routers, cleared at its teardown; a preserved child with CV
set and no PTO held (`32af0777`, `239f4ec4`); HiFi Enhanced
Uni-Directional TMU on a v2-v2 kid link before CV, the parent's side
reset at the kid's teardown and kept owed until proven (`239f4ec4`,
`bc0c7be5`). `test_u4v2`: 373 checks, 0 failures; `test_u4cm` 1638, 0.
At `bc0c7be5`: `test\run-host-tests.cmd` "Host tests PASSED"; the gated x86
build `all`, "BUILD + GATES PASSED (debug release qemu)"
(`E:\xhci98-scratch\p45-w45-u4chain2\gb-x86-m.log`). No leg was run on
the version 2 paths: the emulator's chain in this run is version 1.

The status record's kind 3 entries for kids (record 34 section 6.11, "The
status record"; `11f58253`, reviewed): `test_cm_status` 131 checks, 0
failures.

## 45.41 a UAS drive behind a SuperSpeed hub dropped by its link's warm reset: read, reproduced, and kept in place (2026-10-30)

**Verdict: the report's drop is the driver's design, reproduced on q35 XP
with `3.3.0.0`.** A SuperSpeed link that falls to SS.Inactive under a
device - behind a SuperSpeed hub, or on a root port - made the bus drop
the device and enumerate it afresh; to the storage stack that is a
surprise removal, and its cached writes are lost ("Delayed Write
Failed"). Design record 13 section 15 now keeps a one-interface UAS
drive with a serial number in place across that recovery, through the
restore RESET_PORT already does, and every other device is dropped as
before. The freeze the reporter also saw is not this driver's (task
45.42, below).

### The report and its snapshot

A desktop with a VIA VL805 card (`1106:3483`), Windows XP x64 SP2
(`xhci98.sys` 361432 bytes, `3.3.0.0`'s release-x64 build; `XHCITR`'s
"bitness x86 (32-bit Windows)" line beside it is a fault of `XHCITR64`'s,
which reads `IsWow64Process` as false on a native x64 system), `3.3.0.0`. A SanDisk UAS drive (`0781:55A9`) on port 1 of a Realtek
SuperSpeed hub (`0BDA:0411`) at root port 3. The snapshot, taken 1347 s
after the controller's start:

- the UAS driver's telemetry: record 1 "its device departed, slot 4", its
  last send 229.9 s before the dump; record 2 "its device present, slot
  4", 229 sends; neither counted a sense, protocol error, timeout, abort,
  LUN reset or port reset - the drive left with no error of its own;
- `SsHubWarmResets` 1, `SsWarmResets` 1, `SlotsEnabled` 8 for 6 devices
  present: the hub port's link recovery, and a second one;
- root port 2's last note: "machine Bound, fed disconnect, link warm
  reset; PORTSC PLS 6, changes CSC PLC" - a 5 Gbit/s RTL8157 network
  adapter (`0BDA:8157`) whose link fell to SS.Inactive (PLS 6, CCS 0) and
  which was dropped the same way; root port 2 reads Empty at the dump.

One hub warm reset in 1347 s of uptime, the drive's departure at about
1117 s; a single snapshot gives no rate beyond that.

### The reading of the code

`HcdSsHubPortLook` (`hcd_sshub.c`) reads a hub port's link in SS.Inactive
or Compliance Mode, marks the port `HubSsRecover` and asks for the
disconnect of what it holds; `hcdHubPortChanged` (`hcd_enum.c`) feeds it
- the device frozen, its transfers ended, its slot disabled, its PDOs
reported missing - and only then `HcdSsHubPortRecover` sends
BH_PORT_RESET, and the trained link is a new device. A root port does the
same through `XhciLinkPortFeed` and PORTSC.WPR. The transfers the
teardown ended are held on the old PDO until its surprise removal
(`HcdIoPark`). SS.Inactive is a link failure with the device attached,
not a detach (USB 3.2 7.5.2); nothing in the path restored the device.

### The vehicle

Development host A, QEMU 11.1.0 under TCG, built in the worker's scratch
from `usb-wdm-free`'s `qemu-cancel` tree (11.1.0 with its xHCI cancel and
No Op fixes) plus a test-only patch of this task's, kept with the build:

- `usb-ss-hub`, a SuperSpeed hub model (the SuperSpeed half of a USB 3
  hub: bcdUSB 3.00, hub descriptor 0x2A, SET_HUB_DEPTH, the USB 3.2
  section 10.16 port status, change bits and features, BH_PORT_RESET),
  downstream ports SuperSpeed only;
- its `link-drop` property: `qom-set <hub> link-drop N` fails port N's
  link - SS.Inactive, PORT_ENABLE 0, C_PORT_LINK_STATE (with 0x100 the
  connection bit drops too, with 0x200 no change bit is set) - and the
  host controller forgets every transfer in flight to that device, with no
  event, and starts none until the port's warm reset;
- the same on `qemu-xhci`'s root ports: `qom-set <xhci>/xhci-core
  link-drop N` sets PLS 6 with CCS 0, PED 0, CSC and PLC, as the VL805
  reported; any reset of the port brings the link back.

The q35 XP base (`launch-q35.ps1 -Os xp`, 2 processors, 1 GiB, `-vga
cirrus`; the "before" readings, taken before the reporter's system was
known to be XP x64) and the q35 XP x64 base (2 GiB unless stated), `3.3.0.0`
installed on an overlay (`INSTALL.BAT /console /yes
/nopause`, exit 3, then shut down); each run an overlay of that. The hub
on `qemu-xhci` root port 3, a `usb-uas` (serial `W45UAS41`, one `scsi-hd`
LUN on an overlay of the 128 MiB FAT test disk) on its port 1, the drive
D:. Load: `W.BAT D:`, a 16 MiB file copied, compared and deleted in a
loop, one COM1 line a pass with the guest's time.

### Before the change: `3.3.0.0`

| Run | Result |
|---|---|
| one hub-port link failure under the copy loop (host 09:28:59.6) | the drive dropped: the copy in flight failed at 09:29:01.5, 34 copies failed over 8 s while the drive was absent, the drive came back as a new device and passed from 09:29:17.3 - about 18 s without the disk |
| twenty failures 40 s apart | each dropped and re-enumerated the drive; for the first seven, the disk was absent 11.3 to 31.2 s each (18.2, 29.9, 19.8, 31.2, 27.3, 14.5, 11.3 s from the last good pass before to the first after). From the eighth on every pass failed: the failed passes had left their 16 MiB files on the 128 MiB disk, which was full (15 MB free) - the harness's fault, corrected for the later runs (`W.BAT` deletes leftovers at each pass) |
| `XHCITR /snapshot` after the twenty-one failures | `SsHubWarmResets` 21, `SlotsEnabled` 24 and `DevicesAddressed` 24 for three devices present, three slots held, six pool rings in use, 18 transfers cancelled; nothing of the bus grew beyond what each re-enumeration adds and frees |

### The change

Design record 13 section 15 (reviewed and revised, commits `273a95dc`,
`b42919bc` - the merge of `3.4.0.0` with the resume keep's review fixes,
which added the refusal of a device on another configuration - and
`bd9be1d8`). A SuperSpeed link read in SS.Inactive or Compliance Mode
under a device of one interface, storage on UAS under `xhciuas.sys`, with
a serial number taken as its instance id, started, not under selective
suspend, on its configuration index 0, is restored in place by
RESET_PORT's own sequence instead of dropped: what its pipes held fails as
a transport error, the port is warm-reset, Reset Device and Address
Device run on the same slot, the device descriptor, the whole
configuration and the serial number are compared, the configuration is
replayed; `xhciuas.sys`'s own recovery then resets it once more and the
disk class driver retries. Every other device - a hub, a composite device,
Bulk-Only storage, a HID device, a network adapter - is dropped as before;
so the RTL8157 the same snapshot shows dropped on root port 2 is not kept,
and task 45.40 is not answered here (record 13 section 15, L2, says what a
network adapter would need). Three keeps a device inside a window of 600
tolerance-clock ticks (nominally 60 s); the fourth is dropped as before.
`XhciLinkKeep` 0 (machine-wide `Services\USB` or the controller's driver
key) restores the drop. Two changes reach beyond the link: RESET_PORT
quiets every pipe before it settles any, within a bound; and a device
reset of `xhciuas.sys` that gives up without the bus having had its
RESET_PORT now asks the bus to cycle the device (charged to the location
as a failed RESET_PORT is) instead of leaving the drive present and dead
until it is unplugged.

Builds: `bd9be1d8`, the gated x86 build (all three flavours, "BUILD +
GATES PASSED (debug release qemu)", the host suites passed; release
`xhci98.sys` `58cb5613...`, `xhciuas.sys` `6be02f4c...`), and a plain
amd64 release build for the XP x64 legs ("BUILD + GATES PASSED
(release)"; `xhci98.sys` `7d14bbd1...`, `xhciuas.sys` `e57f44dd...`).
Each leg's guest is an overlay of a `3.3.0.0` install with this tree's two
files copied over the installed ones and the guest restarted, and
`XHCITR /snapshot` showing their hashes before the leg; on XP x64 the
`3.3.0.0` x64 package was installed after its test certificate was
imported in the overlay (`rundll32 cryptext.dll,CryptExtAddCER`, the
wizard's automatic store), as the base's README says is owed.

### After the change

| Leg | System | Result |
|---|---|---|
| a hub-port link failure under the copy loop and a held file | XP x64, amd64 | kept: `link.keep=01000000` (location 0100h, the first hub's port 1, in the high half; reason 0, kept), `link.keep.done=01000001` (restored); `SsHubWarmResets` 1; no copy failed, the pass spacing (about 1.35 s) unchanged across the failure; the held file grew on; no "Delayed Write Failed"; the UAS telemetry's record unchanged (no second device) |
| nine hub-port failures in eight minutes | XP x64, amd64 | all nine kept (`SsHubWarmResets` 9), `SlotsEnabled` unchanged at 4, the same telemetry record for 1038 sends, no copy failed, the held file at 1043 lines |
| four failures inside 18 s | XP x64, amd64 | three kept, the fourth refused by the budget (`link.keep=01000009`, reason 9) and dropped as before: a new telemetry record, `SlotsEnabled` 5, and the held file's handle invalid ("The volume for a file has been externally altered so that the opened file is no longer valid") - the drop's own symptom, as designed |
| a root-port link failure (PLS 6, CCS 0, CSC and PLC) | XP x64, amd64, disk on root port 3 | kept: `link.keep=00030000` (location 3, reason 0), `link.keep.done=00030001`; `SsWarmResets` 1 (the inspection's precharge refunded: one charge for one warm reset); `SlotsEnabled` unchanged; no copy failed; the held file grew on |
| the same with `XhciLinkKeep` 0 | XP x64, amd64 | `link.keep.on=00000000`, `link.keep=00030001` (reason 1, the switch off): dropped as before - a new telemetry record, the held handle invalid |
| a hub-port link failure under the copy loop and a held file | XP x86, x86 gated release | kept, restored, `SsHubWarmResets` 1; no copy failed, the pass spacing (about 5.5 s) unchanged across the failure (the copy after it at 14:17:59.9 for a failure at 14:17:55.8); one telemetry record; the held file grew on. `3.3.0.0` on the same harness: the disk absent about 18 s, 34 copies failed |
| a plain unplug of the disk under load, then a replug | XP x86, x86 gated release | as before: the copy in flight failed 10 s after the unplug and every copy failed while the disk was absent; 0.7 s after the replug the disk was back and passing; no hang |
| 45.23's keep: hibernation under the copy loop and a held file, `XhciResumeReinit` 2 | XP x86, x86 gated release, disk on root port 3 | kept as before: `resume.keep=00020000` (two kept - the disk and the tablet - none failed or refused), `resume.keep.port=00000300` (root port 3, decision 0), `resume.keep.d0wait=00000104`; no copy failed after the resume, one telemetry record, the held file grew on |

Owed, of design record 13 section 15's L9: the soak after the change ran
nine failures in eight minutes on XP x64, not twenty forty seconds apart;
the hub model's variants (the connection bit dropped too, 0x100, and a
failure the hub never reports, 0x200) and an unplug of the hub itself
under load were not run; the root-port failure was read on XP x64 only (the
XP x86 root-port leg is 45.23's resume regression, not a link failure).
Not read on a guest: the Bulk-Only, composite, hub and no-serial refusals
(host vectors only: QEMU's `usb-uas` always carries a serial, and every
refusal ends in today's drop, which the budget and `XhciLinkKeep` 0 legs
show); Windows 98 SE, ME and 2000 (the same bus binary; their storage
stacks above `xhciuas.sys` were not exercised); a real VL805 and Realtek
hub - the reporter's.

### The freeze

Not reproduced, and on 2026-10-30 the reporter wrote that the same random
freezes come with the VL805 card removed (task 45.42): not this driver's.
The keep removes the drop's own outage - every request failing for 11 to
31 s, the old volume's handles failing for good - which section 45.41's
first readings measured.

### Side finding: a UAS disk present at boot on XP x64 is not enumerated

On the XP x64 guests with `3.3.0.0`, a `usb-uas` attached at power-on was
not enumerated: no PDO and no problem device; a replug from the emulator
(`attached` false, then true) enumerated it at once. Seen on every XP x64
boot of this task, `3.3.0.0` and this tree alike. Task 45.43.

## 45.42 an X58 desktop with a VL805 freezing at random with no USB device attached (2026-10-30)

**Verdict: not xHCI98's.** The reporter wrote on 2026-10-30 that the same
random freezes come with the VL805 card removed. Before that, the reading
of the snapshot found nothing of the controller's (no recovery, no
tolerance event, the interrupt line healthy, the longest completion pass
3.2 ms), and one candidate - transfer buffers above 4 GB bounced through
map registers below it, `xhci98.sys` describing a 32-bit adapter on
amd64 as on x86 - was read on q35:

| Run | Result |
|---|---|
| XP x64, `3.3.0.0` release amd64 (the reporter's `xhci98.sys` by hash), `-m 8192` (8191 MB seen by Windows, 7573 MB free); a USB 2.0 hub with a mouse, the tablet and a UAS disk on xHCI; copy loops on the UAS disk and the IDE system disk at once, a COM1 ticker every second | no stall: the ticker over 593 s, the longest gap 1.42 s; the UAS loop 471 passes over 684 s (median 1.45 s, longest 3.35 s), the IDE loop 646 over 672 s (median 1.03 s, longest 2.16 s), none failed (guest clock throughout); `XHCITR`: 401615 bulk URBs, 7.99 GB, publication mean 32 us (longest 1.65 ms), completion pass mean 16 us (longest 0.67 ms), map grant 256, no URB waiting for a mapping |
| the same at `-m 3072` | no stall: the ticker over the last 263 s, the longest gap 1.56 s; UAS 774 passes over 1003 s (median 1.30 s, longest 1.66 s), IDE 1086 over 991 s (median 0.91 s, longest 1.42 s), none failed |

The 8 GB passes were about a tenth slower with a longer tail, consistent
with bouncing, and nothing froze. The 64-bit DMA question is dropped with
the task.

### 45.36, the third reading: the rate by variant, the threads' creation read, and the known issue (2026-10-30)

`3.4.0.0` at `79d46dc2` merged first. No code; the owner's limit for
this task was about half a day, then a known-issue note.

**The threads' creation, read in the source** (with a second read).
Every `PsCreateSystemThread` of this package passes no stack size, runs
at `PASSIVE_LEVEL` from a system thread's context (the PnP start, or the
connection manager's own thread) and holds no spin lock across the call,
which is what the call's documentation asks on Windows 98 and ME. With
selective suspend off (its default) a 98 SE start with the ASM4242 model
makes six: one service thread per xHCI controller (`hcd_ctl.c`), the
connection manager's thread (`cm_thr.c`), its configuration-manager
worker (`cm_cfgwk.c`, made by that thread right after the start) and one
call worker per USB4 port (`cm_callwk.c`, the same). The fine-timer
thread (`hcd_tmr.c`) is never made on 98 SE or ME, the suspend threads
(`hcd_susp.c`) only with selective suspend on, and Stor98 makes none (the
two creations under `src/stor98/` are its test connection manager's). No
creation was found that breaks a documented rule; the read cannot tell
whether NTKERN's creation in this boot phase can leave a new thread's
first stack page unresolved. The experiment proposed with the read,
creating the per-port call workers on first need, was not made: they are
also the route of task 43.47's handover at start (`CmCfgwkTakeover`),
so the change reaches the connection manager's start, and at the rates
below no run in the time could show an effect.

**The rate by variant.** Unattended cold starts of overlays of a kept
base, each watched three minutes for the stop screen (its blue, read
from a screendump), on the ASM4242 layout with the enclosure at power-on:

| Variant | System | Starts | Stops |
|---|---|---|---|
| `3.3.0.0` with Thunderbolt and Stor98 (`hm1`), cold start | ME | 60 (20 + 40) | 0 |
| this build, default rows: no Thunderbolt, no Stor98 (`bmenocm`), cold start | ME | 20 | 0 |
| no xHCI98 (`bmestock`), cold start | ME | 20 | 0 |
| `3.3.0.0` with Thunderbolt and Stor98 (`h1`), the update and its first start | 98 SE | 4 (`w2`, `w3`, `w5`, `w6`; two more lost to the script) | 0 |

With the first two readings: 2 stops in 139 starts with xHCI98 installed
(98 SE 1 in 11 first starts after an update, 0 in 30 cold starts; ME 1
in 78 starts, none after an update), 0 in 28 without it (98 SE 8, ME
20). The variants without the enclosure or without Stor98 on 98 SE were
not run: the 98 SE cold start never stopped in 30, so they could not
have shown a difference. A rate this low does not separate the package
from the system: two stops against none in 28 is not evidence either
way.

**Result.** No cause found in the time given. What is known: the stop is
a page fault on a new thread's first ring-3 instruction (its first stack
page not present), at the same offset on 98 SE and ME (ME0E.1's), rare
(about 1 in 70 starts with this package, more often at the first start
after an update on 98 SE), and gone at the next start. **Partial; the
known issue below is for the owner.**

**The known-issue note, three wordings for the owner's choice:**

1. Short: "Windows 98 SE and ME: rarely, a start stops with 'A fatal
   exception 0E has occurred at 019F:BFF72170' (98 SE) or 'Error: 0E :
   01A7 : BFF62170' (ME). Turn the computer off and on again; the next
   start works."
2. With what is known: "Windows 98 SE and ME: about one start in
   seventy, more often the first start after installing or updating
   xHCI98 on Windows 98 SE, can stop with 'A fatal exception 0E has
   occurred at 019F:BFF72170' (98 SE) or 'Error: 0E : 01A7 : BFF62170'
   (ME), followed by a black or empty screen. It is a fault in Windows'
   start of a new program thread; it has also been seen without the
   update. Turn the computer off and on again: the next start works and
   nothing is lost. The cause is not known yet."
3. As an install note: "After installing or updating on Windows 98 SE
   or ME, if the first start stops at a blue screen naming exception 0E
   (or at a black screen), switch the computer off and on again; the
   install is complete and the next start works. This happens rarely and
   its cause is still being looked into."

## 45.14 record 34 section 3.3, the 9x half: the padding and PCI.VXD's first reading (2026-10-30, partial)

**The design.** Record 34 section 3.3.6 (revision 9 after the merge into `3.4.0.0`, written as revision 8 at `9b3e0936`),
reviewed and revised before code: section 3.3.2's Option A made exact for
its first part, the padding of a chain port at the first numbering, which
the rest of section 3.3 (the chain port's own numbering, the devnode
locate, the removal, `DN_REMOVABLE`) rests on. A chain-port candidate
(none below another) keeps half of what is left of the port's range, at
most 8 buses and at least 2; a numbering that ends `NO_ROOM` with the
padding is made again at once without it, once its undo has put every
bridge back; the memory slack beside what the chain needs is shared
equally among the padded candidates, a power of two, at most 64 MiB and
at least 2 MiB, added beside each one's own need and checked before any
window-placement write; a `REOPENED` chain gives its memory again to a
candidate whose retained range leaves room for a chained switch.

**The code** (`02676bbb`): `src\cm\tbt_brnum.c`/`.h`
(`XHCI_TBT_BRNUM_F_PAD`, `brCandidate`, the bus padding in `brBus` with
`InCand`, the retry in `XhciTbtNumberBridges`, `brSpan`, `brPadMem`, the
padded size in `brNeed`, the `REOPENED` qualification), `cm_cfgwk.c`
(`cmWkNumber` passes the flag; `cm.brnum.9x.pad`, `cm.brnum.9x.pad-mem`);
host vectors in `test\test_tbt_brnum.c` (the model given a PCI Express
capability; `test_pad_buses`, `test_pad_memory`, `test_pad_nested`), 506
checks, 0 failures; `test\run-host-tests.cmd` links `tbt_topo.c` into
that suite. The gated x86 build of `92fd318c` (`scripts\build-driver.cmd
all`): "BUILD + GATES PASSED (debug release qemu)"; the host suite:
"Host tests PASSED" (`E:\xhci98-scratch\p45-w45-chain\gb-x86.log`,
`hosttests.log`).

### The reading (2026-10-30)

**Vehicle.** 98 SE and ME goldens made for this task: overlays of the
q35 bases, the launcher's `-Usb4 tb3ar` on the `usb4-emu` `230bed5`
emulator (`qemu-system-x86_64.exe` `bb2b1ec9...`, its Thunderbolt 3
tunnel port padded by the firmware model and its switch ports with
Interrupt Pin A), the release x86 package of `1a2a72ca` installed
(`XHCIINST /thunderbolt /stor98`, exit 3; on 98 SE the installer's
second xHCI controller asked for the driver disk, 45.38, answered with
`C:\XHCI98`), a cold second boot (98 SE `4c12f9b2...`, ME
`7b84aa34...`). Each leg a child overlay with the qemu flavour of
`xhcicm.sys` at `02676bbb` (`a083d92f...`) copied over the installed
one and a cold start; the dock `usb4-device ... dialect=tb3,
downstream-ports=1, chain-power-controller=off` plugged by QMP at the
desktop. Evidence: `E:\xhci98-vm\p45\w45-chain\w98\` and `me\`
(`debugcon-<leg>.log`, `infopci-<leg>-<n>.txt`, `serial-<leg>.log`,
screenshots).

**The tunnel port's slot power.** With the emulator's default the
Thunderbolt 3 tunnel port has a Power Controller, and 98 SE never turns
the slot on: the hot-plugged dock's switch read nothing for the
numbering's twenty tries (`cm.brnum.9x.outcome` NONE, functions 0; leg
p2b) and stayed unnumbered at bus 4. Every leg below runs with
`-global usb4-tb3-tunnel-dsp.power-controller=off`, as the ASM4242
legs run their tunnel ports.

| Leg | System | Read |
|---|---|---|
| p2c | 98 SE | the dock numbered (`cm.brnum.9x.outcome=00020801`): its upstream port `04:00.0` buses 04/05/0F, the fixed port `05:00.0` 05/06/06, the chain port `05:01.0` **05/07/0F** (pad 8; `cm.brnum.9x.pad=00000001`) with the memory window **FA100000-FE0FFFFF** (`cm.brnum.9x.pad-mem=04000000`, 64 MiB), the port's window F2200000-FE1FFFFF; `info pci` after the re-enumeration: the chain port 7 to 15, its window as written. Windows' bridge wizards then asked for a restart, which ends the emulator (98 SE's warm restart is a cold one here) |
| p2d | 98 SE, the same overlay, cold | the dock plugged again: numbered alike; `info pci` after the plug, after the next bridge's install, after Stor98's install for the dock's NVMe and after the chain port's own install: **unchanged** (7 to 15, FA100000-FE0FFFFF) |
| p2e | 98 SE, the same overlay, cold, every driver installed | the dock plugged: no wizard; numbered alike; `info pci`: unchanged; `XHCIINST /devices` lists the dock (link 1, depth 1, approved) and its upstream port started |
| m1b | ME | numbered alike (pad 8, 64 MiB); one bridge wizard; `info pci` after it: unchanged |
| m1c | ME, the same overlay, cold | the dock plugged: numbered alike; `info pci`: unchanged; shutdown with the dock attached ended normally |

**What it shows.** On 98 SE and ME, PCI.VXD leaves a padded empty chain
port's bus range and memory window as the numbering wrote them, through
the re-enumeration that follows, the first installs of the dock's
bridges and of Stor98, and in a later session: section 3.3.6's first
premise holds on both. That PCI.VXD's allocation holds them (that a
device chained later is placed inside them) is read only with the chain
port's own numbering, next.

**Left in 45.14** (not ticked): the chain port's own numbering and
re-enumeration, the devnode locate (section 3.3.4), the removal (section
3.3.5) and `DN_REMOVABLE` on the chained unit (section 3.7), each
designed in detail and reviewed before its code, then their legs on 98
SE and ME with a device chained behind the padded port.

## 45.6 (continued) the installer's row split, the cut's checks with one set, and the legs on XP and 7 (2026-10-30)

The rest of 45.6 after `26973326`, on branch `w45-rtlpair` from
`d265c092`, reviewed and revised. Not yet committed: by the
coordinator's decision of 2026-10-30 it goes in as one commit with the
free set's pin moved to `usb-wdm-free` 0.1.5 (`wdmfree-pin.txt` and
`INST_WDMFREE_SUMS`), since the cut refuses the split row at 0.1.4. It
is four parts:

- `instOtherRels`' row for `free:net/rtl815x` and
  `nonfree:ethernet/rtl815x` split, kind 1 (`EXCLUDES`) on `0x1E07` (98
  SE, ME, 2000, 8 to 10 x64) and kind 2 (lifted) on `0x1F8` (XP to 7
  x64), with a `test_inst` vector over the thirteen systems (7138
  checks, 0 failures).
- The packager self-test over the real pinned sets
  (`test-package.ps1`) exports the pinned free set too and runs the
  cut's checks against the repository's `releases\` at the INF's
  version, as the cut does, and requires a `LIFTED` line when the table
  has a lifted row; with the non-free checkout alone it runs as before.
- A lifted row held to a lift only in a cut that carries both sets of
  its pair (`Test-OtherTableLifts -Carried`); a cut that leaves either
  set out records the pair's `COEXISTS` lines on the row's systems as
  `EXCLUDES` (`Get-OtherHeldLifts`), which is how the installer holds
  the row when the partner's set is not in the download
  (`other_same_release`), while a cut carrying both keeps the
  `COEXISTS` and must prove the lift.
  Found by the first gated build: the self-test "a release recorded
  without the pack is assembled without it", a `-NoWdmFree` cut, was
  refused by the lifted row (1 of 1715 checks).
- Design record 26 section 6.7.14, draft 51.

**Gated build** (x86, `build-driver.cmd both`, the working tree above
over `d265c092`): BUILD + GATES PASSED in 25:51, the packager's
self-tests passing in 23:58; `xhci98.sys` release `270D1454...`.

**The cut's checks with the real non-free set** (`usb-wdm-nonfree`
v1.1.0, the cut's own functions, the repository's `releases\`, so
3.2.0.0's record is the previous one while the tree's `DriverVer` is
3.3.0.0):

| Free set | Result |
|---|---|
| v0.1.4 (the pin) | refused: its single `EXCLUDES` and the table's lifted row are two kinds on XP to 7 x64, as expected until the pin moves |
| stand-in A: v0.1.4's files with 0.1.5's split `STAGE.TXT` lines | admitted; recorded `LIFTED`, `free:net/rtl815x` and `nonfree:ethernet/rtl815x` on `W7 W7X64 WVISTA WVISTA64 WXP WXP64` |
| stand-in B3: B1 (below) with `USB\VID_0BDA&PID_8153&REV_3099` added to the free XP INF | refused: "were excluded on WXP by 3.2.0.0, and this cut's COEXISTS there is admitted only where the cut reads them apart ...: both match VID_0BDA&PID_8153" |
| stand-in A without the non-free set (`-NoNonFree`) | admitted; the pair recorded `EXCLUDES` on XP to 7 x64, no `LIFTED` |

The stand-ins are tags in a scratch clone of `usb-wdm-free` at v0.1.4,
not in that repository: A splits the line as 0.1.5 does (in 0.1.5 the
free `rtl815x` is still copied only, so the lift shows nothing on the
page); B1 is A with the free `rtl815x`'s four XP to 7 x64 `COPY` lines
made `STAGE`, so both boxes can be ticked; B2 is B1 with one comment
changed, another release of the set. Each was cut as a TEST
(`make-release.ps1 -Arch x86 -TestLabel ... -WdmFreePin`, the
repository's 3.2.0.0 record copied into the cut's `-ReleasesDir`) with
an installer built for that stand-in's `SHA256.TXT`
(`INST_WDMFREE_SUMS` set for the cut and restored afterwards):
`XHCIINST.EXE` A `9AB8A6B1...`, B1 `36DF649A...`, B2 `9533E505...`.
Each cut passed.

**XP** (q35, `xp` base, a fresh overlay, `launch-q35.ps1 -Os xp -Smp
2`, the `-nonfree` download as the transfer disk, `INSTALL.BAT
/console`):

| Run | Download | Switches | Result |
|---|---|---|---|
| R1 | B1 | `/list` | both `free:net/rtl815x` and `nonfree:ethernet/rtl815x` among the packages offered for this Windows |
| R2 | B1 | `/wdmfree:stage=hid/xpad98 /wdmnonfree:stage=hid/x360wc` | "REFUSED: free:hid/xpad98: Not with nonfree:hid/x360wc, ticked: the two are not staged together (both bind the Play and Charge Kit 045E:028F).. Nothing was changed.", exit 1 |
| R3 | B1 | `/wdmfree:stage=net/rtl815x /wdmnonfree:stage=ethernet/rtl815x` | xHCI98 3.3.0.0 installed and started; "free:net/rtl815x staged." and "nonfree:ethernet/rtl815x staged.", exit 3 |
| R4 | B2, after a restart | `/wdmfree:copy /wdmnonfree:stage=ethernet/rtl815x` | "REFUSED: nonfree:ethernet/rtl815x: Not with free:net/rtl815x, staged by an earlier run (they would claim one device): uninstall the other drivers first.. Nothing was changed.", exit 1 |

**7 x86** (q35, `w7x86` base, a fresh overlay, an elevated prompt, the
publisher prompts answered "Install this driver software anyway"):

| Run | Download | Switches | Result |
|---|---|---|---|
| R1 | A | `/list` | `nonfree:ethernet/rtl815x` offered; the free `rtl815x` copied only |
| R2 | A | `/wdmfree:copy /wdmnonfree:stage=ethernet/rtl815x` | both sets copied, "nonfree:ethernet/rtl815x staged.", the free one's folder `C:\XHCI98\USBDRV\FREE\net\rtl815x\wv-7-x86` listed for a by-hand install, exit 3 |
| R3 | B1, after a restart | `/wdmfree:stage=net/rtl815x /wdmnonfree:stage=ethernet/rtl815x` | the non-free one "Staged by an earlier run" of the same release and not barred; the free set's earlier copy of another release replaced; "free:net/rtl815x staged." and "nonfree:ethernet/rtl815x staged.", exit 3 |
| R4 | B2, after a restart | `/wdmfree:copy /wdmnonfree:stage=ethernet/rtl815x` | refused as on XP, exit 1 |

**Pass**: with the two packages' chips disjoint both install side by
side on XP and 7, and the overlapping cases stay excluded: an id in
common at the cut, the excluded pair `xpad98` and `x360wc`, and a
lifted pair against an earlier staging of another release. No adapter
is at hand, so no device was bound by either driver. A double full
stop in the refusal's text ("first.. Nothing"), where the reason ends
with its own, is older than this change.

Evidence: `E:\xhci98-vm\p45\w45-rtlpair\shots\` (`xp1-*.png`,
`w7a-*.png`), the cuts and the stand-in pins under
`E:\xhci98-scratch\p45-w45-rtlpair\`.

**The pin** (2026-10-30): `usb-wdm-free` v0.1.5 was released at
`048aecd` (`uac2` 0.1.5.0 and `usbcom` 0.1.5.0; `rtl815x` and every
other package 0.1.4's bytes; its `STAGE.TXT` splits the `rtl815x`
line) and tagged by the owner. Commit `db824326` carries the four
parts above with the pin moved to it (105 `SHA256` lines) and
`INST_WDMFREE_SUMS` `8AF8F638...`, the hash of the exported
`USBDRV\FREE\SHA256.TXT`. On that commit:

- The gated build (x86, `both`): BUILD + GATES PASSED in 36:14, the
  packager's self-tests in 22:15 with both sets' checkouts present (the
  free one a scratch clone carrying v0.1.5 on the same commit, before the
  owner's tag), so the self-test over the real pinned sets proved the
  lift against the repository's history; `xhci98.sys` release
  `D10F83CA...`. The installer's tools were rebuilt after it, since
  `build-driver.cmd` does not rebuild them and they include
  `inst_core.h`.
- `make-release.ps1 -CheckOnly -Arch x86` against the real
  `usb-wdm-free` (tag v0.1.5) and `usb-wdm-nonfree`: "download 2:
  usb-wdm-free v0.1.5 (048aecd369f1), 107 files in USBDRV\FREE\" and
  "the other USB drivers' sets: folders, self-containment, hardware ids
  and the package table hold, against 3.2.0.0's otherpkgs.txt"; the
  one refusal is `releases\3.3.0.0` already existing, since the tree's
  version is still 3.3.0.0 until the release's bump.

So `3.4.0.0`'s free download offers the free and non-free `rtl815x`
together on XP to 7 x64; with v0.1.5 the free one is copied only, so
the page shows no change until the free set stages it.

## 45.45 one build for everyone: the debug flavour retired (2026-10-30)

The owner, 2026-10-30: the `debug` flavour is retired, its frame
pointers and its command-state checker kept in `release`. The design is
record 08 section 14 (`a6f5578f`, reviewed and revised; the owner's two
answers in `126c1515`); the code is `d4201bc4` (reviewed and revised),
merged with `3.4.0.0` at `56682b82`. Development host A, loaded by other
work throughout.

**Verdict: the gated x86 build (`all`, now release and qemu) and the
amd64 release build passed; the release package installed and ran on
98 SE, XP, 7 x86 and XP x64, each read in an XHCITR snapshot with the
checker on (the INFs' 1) and with `XhciCommandCheck` 0, and the value
was obeyed on every one. Retiring the flavour saves about a minute of a
gated build per architecture, against whole runs of 30 to 81 minutes
that the INF gate's and the packager's self-tests fill.**

### The builds

| Build | Result | Time |
|---|---|---|
| x86 gated, `build-driver.cmd all`, at `126c1515` | BUILD + GATES PASSED (release qemu) | 74:53 in all: the INF gate's self-tests 72:22 (in full: the INFs changed), the packager's 16:54 beside them, release 0:47, qemu 0:51 |
| amd64, `build-driver.cmd release -amd64`, at `126c1515` | BUILD + GATES PASSED (release) | 33:24 in all: the INF gate's self-tests 32:28, the packager's 16:02, release 0:31 |

`scripts\check-frame-pointers.ps1` found `/Oy-` on every compiler line of
the x86 release logs of `xhci98.sys`, `xhciuas.sys` and `xhcicm.sys`
("frame pointers kept (/Oy-)" three times); every release image carried
`XHCI98_FLAVOUR_RELEASE` and every qemu one `XHCI98_FLAVOUR_QEMU`. The
host suite passed on the merged tree beforehand, and `test-package.ps1`
(1688 checks) on the code commit.

The time before, from other workers' gated builds on the same host the
same day (`build-driver.cmd both` or `all`, base `b5258e6e` or close to
it): the debug flavour's step took 0:39 to 1:05 on x86 (release 0:38 to
1:02) and 0:29 to 1:01 on amd64 (release 0:31 to 1:15), in whole runs of
30:07 to 80:51 (x86) and 30:53 to 75:31 (amd64). Retiring it takes that
one step out of each run, about a minute an architecture; the rest of a
run is the INF gate's self-tests (28 to 78 minutes when they run in
full) and the packager's (17 to 30), which this task does not touch.

### The legs

The release packages of these builds (`make-package.ps1 -Flavor
release`, x86 and amd64, unsigned on this host), each installed with
this tree's `INSTALL.BAT /console /yes` (98 SE: `XHCIINST /dir
release-x86 /yes`) on a fresh overlay of the q35 base, a USB tablet on
the controller (98 SE: plugged after the start). Each reading is an
`XHCITR /snapshot`, its `strict` lines quoted (XHCITR prints a counter
only when it is not 0). Then `XhciCommandCheck` 0 in the machine-wide
key (`HKLM\System\CurrentControlSet\Services\USB`; `reg add`, on 98 SE
`regedit /s`), a restart (98 SE: shut down and a cold start), and the
second reading. The images: x86 `xhci98.sys` SHA-256 `9b315166...6798`,
amd64 `366dab24...f3b2`, each the build's own.

| System | The install | The checker on | `XhciCommandCheck` 0 |
|---|---|---|---|
| XP SP3 (2 vCPU) | exit 3; controller and root hub started | build flavour `release`; `strict.value=1`, `strict.on=1`; `StrictSwitchRead 1`, `StrictOn 1`, `StrictChecked 3` | `strict.value=0`, `strict.on=0`; `StrictSwitchRead 1`, nothing counted |
| 7 SP1 x86 (2 vCPU) | exit 3, restart (the helper); started after it | the same, `StrictChecked 3` | the same as XP |
| 98 SE (1 vCPU) | exit 3, restart pending; started after a cold start | `strict.value=1`, `strict.on=1`, `StrictOn 1`; no command checked before the tablet was plugged (`StrictChecked` 0), `StrictChecked 2` once the tablet was plugged | `strict.value=0`, `strict.on=0`, `StrictSwitchRead 1`; the tablet enumerated (one slot held), nothing counted |
| XP x64 SP2 (2 vCPU) | exit 3; controller and root hub started (the package without `xhcicm`, below) | build flavour `release`; the same as XP, `StrictChecked 3` | the same as XP |

QEMU's xHC refuses nothing, so the counters show that the checker ran
and was switched off, and no violation, undefined case or refusal; a
real controller's snapshot is where one would show, the owner's reason
for keeping it on. What the driver did was the same with it on and off:
each device enumerated and each install finished alike.

Two things the legs met that are not this task's:

- The installer refuses an unsigned 64-bit package that carries
  `xhcicm.sys` without its catalog ("this 64-bit package carries the
  Thunderbolt driver without its catalog, xhcicm.cat"), as designed; a
  clone without the key stages no catalog, so the XP x64 leg ran from a
  copy of the staged package with the three connection manager files
  left out ("This package carries no Thunderbolt or USB4 driver ...;
  going on without it").
- On 98 SE `XHCITR /snapshot` returns before its window closes, so a
  batch file reading the snapshot straight after it finds no file; the
  lines were read in a second prompt.

### Not read

- The `qemu` flavour's leg kits were not run: nothing in them names the
  retired flavour (they stage `-Flavor qemu`, which is unchanged).
- A real controller with the checker on: owed to the bench with the
  rest of `3.4.0.0`'s hardware readings.

## 45.47 a hot-added dock's xHCI at Code 1 on Vista and 7 x86 until a restart: `xhcimark` starts it once (2026-10-30)

**The question.** On 7 x86 the first plug of a Thunderbolt 3 dock after
the install leaves the dock's own xHCI controller at "This device is not
configured correctly (Code 1)", while every other device of the dock
starts. A restart starts it.

**The reading (legs d1 to d3).** `setupapi.dev.log` of the dock xHCI's
install: the `DelFiles` of `xhcimsi.sys` fails with error 2 and is
handled (the file is absent), and the install goes on; then "Device
required reboot: 'Reboot' in section: [Xhci.Dev6.NTx86]", "Device restart
was skipped because DI_NEEDREBOOT", exit success. The devnode is left at
problem 1 (`CM_PROB_NOT_CONFIGURED`) with `DN_NEED_RESTART`: status
`0x01802500`. `CM_Setup_DevNode(..., CM_SETUP_DEVNODE_READY)` from an
administrator's prompt started it: status `0x0180200A`, problem 0, its
root hub and the tablet behind it started. The cause is the Vista and 7
section's `Reboot`, which the HLPR-REBOOT gate rule keeps.

**The owner's decision (option B).** The INF's `Reboot` and the gate rule
stay. `xhcimark` starts such a controller once, and only when it sits
behind a verified tunnel port, is not configured with a restart pending,
and `xhcihlpr` is already running. Design record 21 section 7.6 is the
design; `mark_core.c`'s `mk_ctl_ready_why` is its pure half, with the
host vectors (every condition refused alone in the record's order), and
`xhcimark.c`'s `mk_ctl_poll` and `mk_ctl_start` the service's half (the
fresh reads under the eject mutex before `CM_Setup_DevNode`).

**The legs.** 7 x86 on q35 with the launcher's `-Usb4 tb3ar`, from the
plain 7 x86 base disk; the emulator `qemu-system-x86_64.exe`
(`3e7f6bf5b98fde98...`, `usb4-emu` fork `674ff08`, the dock's switch
ports on interrupt pin A); a TEST x86 release cut of this branch
(`7ce1ed4d`, `xhci98.sys` md5 `1f49751fb50f...`), installed by
`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`, every
unsigned-driver prompt answered "Install this driver software anyway".
Evidence: `E:\xhci98-vm\p45\w45-emufix\w7\` (`serial-h1.log`,
`serial-h2.log`, `steps-h1.txt`, `steps-h2.txt`, `rep-h1c.txt`,
`rep-h2r.txt`).

| Leg | Steps | Read |
|---|---|---|
| h1 | install with no dock, exit 0; restart; `xhcimark` running with `Restart 1`, `xhcihlpr` running; the dock plugged by QMP at the desktop | 31 s after the plug: "controller not started yet: ...DEV_000D...: an installation is pending (or cannot be told)"; 32 s later "controller started: ...DEV_000D... (it waited for a restart of Windows; the helper is running)"; the dock xHCI's `ConfigManagerErrorCode` 0, its root hub, "USB Input Device" and "HID-compliant mouse" started; every other device of the dock started. No restart |
| h2 | the dock present at power-on, install, exit 0 | the dock xHCI at Code 0 already before the restart, and every device of the dock started after it; `XHCIMARK.LOG` holds no "controller started" line: the service started nothing |

Not read: Vista x86 (the same section and the same service path), and a
leg with the service's `Restart` value 0 (the host vectors refuse it).

**45.47, Vista x86 (legs vh1 and vh2, 2026-10-30).** The Vista Business
SP2 x86 q35 base, `-Usb4 tb3ar`, the same emulator; the same TEST x86
cut as h1 and h2 (its `XHCIMARK.EXE` built from the same `xhcimark\`
sources as 3.4.0.0's `42093a03`). Evidence:
`E:\xhci98-vm\p45\w45-emufix\vx\` (`serial-vh1.log`, `serial-vh2.log`,
`steps-vh1.txt`, `steps-vh2.txt`, `rep-vh1r.txt`). On this base Windows
opens "Found New Hardware" for devices with no driver (an unknown base
device, and the dock's 82574L, for which Vista has none); each was
answered "Ask me again later" or cancelled, and while one is open the
install and the service both wait for it.

| Leg | Steps | Read |
|---|---|---|
| vh1 | install with no dock: the first run ended at exit code 4 behind an open "Found New Hardware", the second at exit code 3 (changes made, restart); restart; the dock plugged by QMP | Windows asked "You must restart your computer to apply these changes" (answered Restart Later); "controller not started yet: ...DEV_000D...: an installation is pending (or cannot be told)" while a "Found New Hardware" was open (the dock's NIC has no Vista driver); once it was cancelled, "controller started: ...DEV_000D... (it waited for a restart of Windows; the helper is running)"; the dock xHCI at Code 0, its root hub, "USB Human Interface Device" and "HID-compliant mouse" started; the NIC "Ethernet Controller - no driver, problem code 1". No restart |
| vh2 | the dock present at power-on, install (exit code 4: the NIC's "Found New Hardware" open), restart | the dock xHCI, its root hub and the tablet's USB and HID devices at Code 0; problem codes only on two base devices (`8086:2930`, `ACPI\QEMU0002`) |

So on Vista x86 too a dock hot-added after the install starts its xHCI
without a restart, once Windows has no installation pending, and a dock
present at the install works after the install's restart. Windows' own
restart prompt still appears at the hot-add, from the section's `Reboot`.
Not read on Vista: the 45.49 stop-first sequence; `XHCIMARK.LOG` in vh2.

## 45.49 a 7 x86 guest hung by a Thunderbolt 3 dock unplugged and plugged in again: the reading (2026-10-30)

**The question.** After the restart that started the dock's xHCI (45.47's
leg d2), an unplug and a replug of the dock hung the guest. Is it 45.35's
dock-removal stall on 7, or something new?

**The legs.** 7 x86 on q35, `-Usb4 tb3ar`, the same emulator as 45.47's;
a disk with the release x86 package of `1a2a72ca` installed, then the
dock installed and a restart taken, so the dock's xHCI starts at the
plug. Each leg: the dock plugged by QMP at the desktop, `XHCIINST
/devices`, the dock unplugged (`device_del`), `XHCIINST /devices` 60 s
later, the dock plugged again, both processors' registers and the
guest's interrupt time sampled every 4 s. Evidence:
`E:\xhci98-vm\p45\w45-emufix\w7\` (`samp-u2*.txt`, `samp-u3.txt`,
`samp-u4.txt`, `rep-u3u.txt`, `rep-u4u.txt`, `rep-u4r.txt`,
`steps-u2.txt` to `steps-u4.txt`).

| Leg | USB device on the dock's xHCI | After the unplug | After the replug |
|---|---|---|---|
| u2, u3 | the tablet (the emulator's default) | every device of the dock still listed, "not started", but "USB Input Device - HidUsb, **started**" | processor 0 never leaves one loop (sampled for 90 s and more); processor 1 keeps running; the console stops answering |
| u4 | a USB disk (`usb-drive=`) | "(nothing behind it now)": the dock removed | the dock enumerated again, every device started; no hang |

**Where processor 0 loops (u2).** The stack from the timer DPC:
`nt` (the timer's DPC) -> `NDIS.SYS` -> the dock NIC's miniport
(`e1qexpress`, the 82574L) -> `NDIS.SYS` -> `pci.sys` -> the HAL's byte
read of configuration space (`ECAM 0xb0e00000`, bus 14, offset `0xFF`).
The miniport (`e1q6032.sys` 11.0.5.21, Windows 7's own; the image
offsets, the hash and the method in `legal-provenance.md` section 4:
the registers and the bytes read from the running guest, the bytes
disassembled, nothing of it executed) looks for a capability (id `0x10`,
PCI Express) by walking the list: it reads the word at the pointer, compares the low
byte with the id, takes the high byte as the next pointer, and stops only
at a next pointer of 0. Configuration space reading all ones gives id
`0xFF` and next `0xFF` for ever. Nothing decodes bus 14 then: after the
replug the new dock's switch upstream port (`104C:8232` at bus 4) still
has secondary bus 0.

**Reading.** It is 45.35's removal stall, on 7. The tablet's interrupt-IN
read stays on the dock xHCI's ring, the removal of the whole dock waits
for it, and the dock NIC's devnode stays on, with its watchdog timer. A
third-party driver's unbounded capability walk over the absent device
then holds processor 0 at `DISPATCH_LEVEL` for good. A
USB disk in the tablet's place leaves nothing pending: the removal runs
and the replug works (u4), as 45.35's control did on XP. 45.35's proof
(record 17 section 4.13.6: a tunnel path the USB4 connection manager
closed and read back closed) does not apply here, by the owner's decision
of 2026-10-29: under the firmware connection manager (the ICM profile,
record 21) nothing reads the path back. The release notes' limitation
("Stop a dock before unplugging it when a keyboard, mouse, tablet or
similar device is on the dock's own USB controller") names 98 SE and ME,
and 2000 and XP under a firmware connection manager; from 45.47 on, 7 and
Vista x86 start the dock's xHCI at the first plug too, so the limitation
reaches them.

**What follows.** The owner's decision: a proof for the firmware
connection manager's tunnels, or the limitation extended to Vista and 7
(the stall there ends in a hang, through the dock NIC's driver).

**45.49, the stop first (leg s1, 2026-10-30).** The owner closed 45.49 as
a known limitation (`080c33dc`): stop such a dock before unplugging it.
The advice read on 7 x86 with u2's setup (the same kit and emulator,
`-Usb4 tb3ar`, the dock installed and a restart taken, the tablet on the
dock's xHCI). Evidence: `E:\xhci98-vm\p45\w45-emufix\w7\` (`steps-s1.txt`,
`rep-s1p.txt`, `rep-s1u.txt`, `rep-s1r.txt`, `samp-s1.txt`, `s1-*.png`).

| Step | Read |
|---|---|
| the dock plugged | every device of the dock started, "USB Input Device - HidUsb" with them |
| the xHCI98 USB Tree (`XHCITR.EXE` of the installed `1a2a72ca` package, from an administrator's prompt): the unit under "Port 1 (bridge 02:01.0)" selected (`104C:8232` at `04:00.0`, "[can be stopped]"), Del | the dialog "Stop a Thunderbolt device" listed the switch upstream port and everything on it: the three downstream ports, Stor98 and the disk, the 82574L, the dock's xHCI, its root hub, "USB Input Device" and "HID-compliant mouse"; OK; "Windows has stopped QEMU NVMe Ctrl SCSI Disk Device (D:). You can now unplug the device." |
| the dock unplugged, `XHCIINST /devices` 60 s later | "(nothing behind it now)"; no device of the dock listed anywhere, HidUsb included |
| the dock plugged again | processors sampled every 4 s for 90 s: idle or moving, no loop; every device of the dock started again, HidUsb included |
| `shutdown /s /t 0` | Windows powered off and the emulator exited within 10 s, nothing on its error output |

So the release notes' advice holds on 7 x86. Windows' own Safely Remove
Hardware is not a way to give it: the USB Tree and `XHCIINST /devices`
both say Windows does not list this unit there ("not listed in Safely
Remove Hardware; /eject asks Windows to stop it"), so the stop is the USB
Tree's or `INSTALL /console /eject`'s. Not read: Vista x86.

## 45.48 the packager self-tests' skip: four gated x86 builds of one tree (2026-10-30)

Branch `w45-pkgskip` (from `e7c70a50`), tree `E:\xhci98-wt\w45-pkgskip`
on development host A, each build `scripts\build-driver.cmd all` with
`XHCI98_SELFTEST_SKIP_OK=1` (a pre-merge build), `src\pfw\build.cmd`
first, under the host's gated-build lock. Times are the steps' own
(`--- ...: passed (exit 0, m:ss)`) and the build's start to end.

| Run | Commit | Build | Packager self-tests | INF gate self-tests |
|---|---|---|---|---|
| 1, fresh tree | `7a1292c5` | 73 min | 27:20, whole, no record | 69:33, whole, record |
| 2 | `7a1292c5` | 34 min | 20:58, whole, no record | 31:17, whole, record |
| 3 | `4203fef8` | 37 min | 20:25, whole, record | 34:20, whole, record |
| 4 | `4203fef8` | 42 min | 0:06, skipped | 39:03, whole, record |

- Run 1 wrote no packager record: a gated build's parallel steps make and
  delete `xhcitr\xhcitr.res` and `xhciperf\xhciperf64.res` (untracked, not
  ignored) while the suite runs, so the digest at its end was not the one
  at its start; `4203fef8` leaves compiler intermediates out of the
  digest. Run 2's record was also voided by an edit made to
  `test-package.ps1` in the tree while it ran. Run 3 ran whole because
  `test-package.ps1` itself had changed, and wrote the record; run 4
  skipped, in 6 seconds.
- The INF gate's self-tests (task 45.30) ran whole in every run: their
  digest reads every file directly in `src\`, which includes the flavour
  builds' `buildfre.log` and `buildchk.log`, and each build writes a new
  `nmake` response-file name into them (`...\tmp\nma79540.` against
  `nma82812.`), so a tree's record never matches its next build. Task
  45.51 (the built outputs left out of each digest, one shared record per
  suite) is the owner's answer.
- Every build exit 0; logs `E:\xhci98-scratch\p45-w45-rvhcd\pkgskip\`.

## 45.18 Stor98's statements on q35: the depth-1 admission and its gate-G3 readings on 2000, XP, XP x64, 98 SE and ME (2026-10-30 and 31)

Task 45.18 of `roadmap-usb4.md`; design record 34 section 3.11 (reviewed
and revised; the owner's answers Q7 (b), Q8 (a), Q9 (a), Q10 (a) and Q11
(a), 2026-10-30). Code: the depth-1 q35 admission at `0fdcde14` (every
qualification bit 0), gated x86 build passed and host tests passed on it.
This section is the first reading of item 11: case D1 (an end unplug at
depth 1) on the root-port kind, on Windows 2000. Read on 2026-10-30.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

### Verdict

**Windows 2000, D1, root port: passes at every injection point run.** In
every counted run the entry was marked `DOWN_QUIESCED` after the
emulator's stage-2 reset and quiesce of the endpoint (`usb4_tp_cutoff`),
Stor98 saw the statement and wrote its acknowledgement (`0x141`, `0x142`)
before the first removal request reached the function's stack (the child
filter's `IRP_MN_SURPRISE_REMOVAL`, `0x1C3` argument `0x17`), the
connection manager's wait ended acknowledged (`0x1C1` argument `0x100`)
before it, and no request was completed by Stor98's gone deadline (no
`0x83`). The 2000 bit of the root-port D1 column is set on this reading
(record 34 section 3.11 item 8); XP, XP x64, 98 SE and ME the same,
below (98 SE and ME on the owner's choice (a) of 2026-10-31). D2 and
M are deferred to the next release with 45.15's keying (owner).

### The vehicle

- Development host A, QEMU under TCG, headless, one vCPU (`pentium3`),
  512 MB; the q35 base `w2k` (Standard PC HAL, SHA-256 `7fc9714e...`)
  through `launch-q35.ps1 -Usb4 adl`: `usb4-host` `profile=usb4-generic,
  host-model=intel-adl` at 00:04.0, its tunnel ports `usb4-tunnel-port`
  `tp0` (00:05.0) and `tp1` (00:06.0), with `-global
  usb4-tunnel-port.power_controller_present=off` (QEMU's `pcie-root-port`
  property `power_controller_present`, read in its source, so the tunnel
  ports report no Power Controller), `-device usb4-debug-port` (stage
  codes at 5A0h) and `-trace usb4_tp_*`, `usb4_dbg_*`,
  `pci_nvme_pci_reset`. Emulator: the q35 bases' `asm4242-20261025-2230`
  build (`qemu-system-x86_64.exe` SHA-256 `da60139f...`).
- The device: a plain `usb4-device` (`id=enc0, bus=u4.0, port=1,
  drive=nvme0`, no `device-model`), so the NVMe controller (QEMU's,
  `1B36:0010`) sits on the tunnel port's secondary bus and gets an entry.
  Plugged and unplugged with QMP `device_add` and `device_del`.
- The golden (`E:\xhci98-vm\p45\w45-s98chain\w2k\golden.qcow2`): the
  release package of `1a2a72ca` installed with `INSTALL
  /console /yes /nopause /thunderbolt /stor98` (exit 0), the drive's
  Found New Hardware wizard naming "Stor98 Storage Controller for NVMe
  SSDs" (the tunnel id), then two images copied over the installed ones:
  - `xhcicm.sys`, a trial `qemu` build from `0fdcde14`, never committed
    (Q8 (a)), SHA-256 `d7ea9302...`: the 2000, XP and XP x64 bits of the
    root-port D1 column set, and stage codes `0x1C0` (an entry marked
    `DOWN_QUIESCED`; argument generation, bus, slot, the q35 flag in bit
    24), `0x1C1` (an acknowledgement wait ended; argument
    `CM_NVLINK_AW_*` << 8 | entry) and `0x1C3` (the child filter's
    `IRP_MN_QUERY_REMOVE_DEVICE`, `IRP_MN_SURPRISE_REMOVAL` or
    `IRP_MN_REMOVE_DEVICE`; argument the minor code);
  - `stor98.sys`, Stor98's debug-port build of the same tree
    (`STOR98_DBGPORT=1`), SHA-256 `d0068976...`;
  and `XhciCmLogVerbosity` 2. Each leg is a child overlay of the golden.
- The load: Stor98's `tests\eio` (`eio e:\t.bin 32 <qd> 20`), built with
  MSVC 6.0. The holds: the harness's `x-faults` on `tp0`.

### Results

Leg `r2` (one boot of a child overlay, the runs in sequence, every run a
new tunnel generation), with `-trace pcie_cap_slot_write_config`,
`usb4_h_fire` and `usb4_h_release` added. Times are virtual milliseconds
from the run's `usb4_tp_cutoff`. Every run: `cm.nvlink.u4-q35=00001032`
(SEND, root port, system 2000, reading DOWN), then `cm.nvlink.down` and
`cm.nvlink.retired`. In the whole leg: no Slot Control write traced
(`pcie_cap_slot_write_config` absent, the condition of record 34 section
3.11 item 5), and no `0x83`.

| Run | Injection | 0x1C0 statement | 0x141 seen | 0x142 ack | 0x1C1 wait end | first removal (0x1C3) | Time left at the reading |
|---|---|---|---|---|---|---|---|
| idle | no I/O | +84.1 | +179.3 | +179.7 | +195.1, acknowledged | +2086.8, surprise | 2799 ms |
| I/O | `eio` QD 8 running, nothing held | +72.4 | +170.5 | +171.0 | +181.2, acknowledged | +2077.5 | 2799 ms |
| held | eight reads held at their fetch (QD 8; the eight discarded by the endpoint's stage-2 reset) | +64.6 | +104.2 | +106.2 | +127.7, acknowledged | +2070.2 | 2799 ms |
| saturated | `eio` QD 32, every read held from the 150th: nine held at the controller, the rest queued in the port (the nine discarded by the stage-2 reset) | +141.3 | +148.2 | +150.8 | +194.9, acknowledged | +2143.0 | 2789 ms |
| bring-up | `x-rdy-set-delay-ms` 20000, unplugged 10 s after the plug | +50.3 | +104.8 | +105.5 | +152.2, acknowledged | +2051.1 | 2779 ms |
| `RequestComplete` | `at=stage code=0x21 nth=300 act=gate` (fired 269.9 ms before the cutoff), then `device_del` | +48.6 | +52.2 | +52.7 | +107.6, acknowledged | +2053.4 | 2579 ms |
| already gone | `at=fetch op=read nth=150 act=gate` (fired 1110.7 ms before the cutoff), `device_del` 1 s later | +52.0 | +80.6 | +81.6 | +143.6, acknowledged | +2068.9 | 1387 ms |
| re-plug | unplugged as soon as `cm.nvlink.up` reached the debug console | +78.6 | +101.8 | +102.3 | +126.9, acknowledged | +2087.8 | 2789 ms |

An earlier leg, `r1`, on the same golden without the three added traces,
gave the same order in the same eight runs (statement +5.0 to +173.7,
acknowledgement +90.9 to +251.8, wait ended acknowledged, first removal
request +2009.5 to +19164.0).

Not counted (leg `r1`):

- **Already gone, 3 s.** The gate closed 3 s before the `device_del`: the
  endpoint's last presence sample was then over 3 s old, so the time left
  was 0, no reading was made, and the statement was withheld
  (`cm.nvlink.u4-q35=00041030`, `cm.nvlink.withheld=0400000A`; the code
  reports NOT_DOWN because no reading was taken). Stor98's gone
  deadline completed the held request at +1627.0 (`0x84`, `0x83`). This
  is record 22 section 10.3's deadline acting as designed. Gate G3 has
  the statement arriving within the gone deadline, so the counted run
  closes the gate 1 s before the unplug.
- **Two saturation attempts** left the reads held for over a minute before
  the unplug. By the unplug the harness showed one held read or none, so
  those runs were not saturated, and they are not counted.

### Windows XP (32-bit), the same reading

**XP, D1, root port: passes at every injection point run**, the same
conditions as 2000. The XP bit of the root-port D1 column is set on this
reading.

- The vehicle as for 2000, on the q35 base `xp` (SHA-256 `64ada103...`,
  ACPI Multiprocessor PC HAL, `-smp 2 -m 1024`): the same release
  package installed with `INSTALL /console /yes /nopause /thunderbolt
  /stor98` (exit 0; the hardware wizards and the signing prompts answered
  by keystrokes), a restart, the drive's wizard naming Stor98 (the tunnel
  id), then the same trial `xhcicm.sys`, Stor98 debug-port build and
  `XhciCmLogVerbosity` 2. Golden
  `E:\xhci98-vm\p45\w45-s98chain\xp\golden.qcow2`; leg `x1`. The drive is
  D:, the transfer disk E:.
- Every counted run: `cm.nvlink.u4-q35=00001042` (SEND, root port,
  system XP, reading DOWN). In the whole leg there was no Slot Control
  write traced and no `0x83`.

| Run | Injection | 0x1C0 statement | 0x141 seen | 0x142 ack | 0x1C1 wait end | first removal (0x1C3) | Time left at the reading |
|---|---|---|---|---|---|---|---|
| idle | no I/O | +132.7 | +187.6 | +188.5 | +208.1, acknowledged | +2155.5, surprise | 2796 ms |
| I/O | `eio` QD 8 running, nothing held | +198.2 | +201.8 | +202.2 | +203.7, acknowledged | +2212.4 | 2796 ms |
| held | eight reads held at their fetch (QD 8) | +173.6 | +182.0 | +186.7 | +253.3, acknowledged | +7873.9 | 2796 ms |
| saturated | `eio` QD 32, every read held from the 150th: eight held at the controller, the rest queued in the port, unplugged once that count was steady (the eight discarded by the stage-2 reset) | +19.6 | +25.0 | +27.7 | +98.2, acknowledged | +2049.6 | 2796 ms |
| bring-up | `x-rdy-set-delay-ms` 20000, unplugged 10 s after the plug | +47.6 | +113.5 | +114.0 | +116.0, acknowledged | +2206.7 | 2796 ms |
| `RequestComplete` | `at=stage code=0x21 nth=300 act=gate` (fired 243.6 ms before the cutoff), then `device_del` | +146.2 | +147.2 | +147.5 | +194.8, acknowledged | +2196.7 | 2593 ms |
| already gone | `at=fetch op=read nth=150 act=gate` (fired 1355.7 ms before the cutoff), `device_del` 1 s later | +38.2 | +97.0 | +97.7 | +114.7, acknowledged | +20469.6 | 1562 ms |
| re-plug | unplugged as soon as Stor98's new instance traced its rendezvous (`0x140`) | +221.0 | +244.9 | +245.3 | +294.2, acknowledged | +2261.8 | 2766 ms |

Not counted:

- **The first saturation attempt.** The leg's sequence waited for nine
  held reads, the count 2000 reached, but XP held eight. Before the unplug
  a controller reset discarded the eight (`usb4_h_release ... discarded by
  reset`), so nothing was held when the drive was unplugged. The run above
  unplugs once the count is steady.
- **The first re-plug attempt.** The unplug followed `cm.nvlink.up` before
  Stor98 had started on the function: no rendezvous was traced and no
  instance existed. The statement was made (+77.5) and XP removed the
  device (`IRP_MN_REMOVE_DEVICE` at +191.6). The wait timed out at
  +5263.0 (`0x1C1` argument `0x400`), since a Stor98 instance elsewhere
  had read the table. With no instance on the function, no request
  depended on the statement. The run above unplugs after Stor98's
  rendezvous.

### Windows XP x64, the same reading

**XP x64, D1, root port: passes at every injection point run**, the same
conditions as 2000 and XP. The XP x64 bit of the root-port D1 column is
set on this reading.

- The vehicle: the q35 base `xpx64` (SHA-256 `bec51ebc...`, `-cpu qemu64
  -smp 2 -m 2048`), otherwise as above. The package is the published
  `3.2.0.0` x64 half (`release-x64` of `xhci98-3.2.0.0.zip`), because the
  release package used on 2000 and XP has no x64 half. Its catalogs are
  signed by the project test certificate, so that certificate's public
  half (`xhci98-test.cer`) was first imported into the guest overlay's
  root store; without it the INFs are not staged (0x800B0109, the q35
  base's README). It was installed with `INSTALL /console /yes /nopause
  /thunderbolt /stor98` (exit 0), and the drive's wizard installed Stor98
  by the class match of `3.2.0.0`'s INF (the tunnel id came in `3.3.0.0`).
  Then the amd64 builds of the same trial tree: `xhcicm.sys` (`qemu`
  flavour, WDK 7.1, SHA-256 `508d3feb...`) and Stor98's debug-port
  `stor98.sys` (SHA-256 `b9ed8392...`). XP x64 enforces no kernel-mode
  signature. `XhciCmLogVerbosity` 2. Golden
  `E:\xhci98-vm\p45\w45-s98chain\xpx64\golden.qcow2`; legs `y1` and `y2`. The
  drive is D:, the transfer disk E:.
- Every run: `cm.nvlink.u4-q35=00001052` (SEND, root port, system XP
  x64, reading DOWN). In the whole leg there was no Slot Control write
  traced and no `0x83`.

| Run | Injection | 0x1C0 statement | 0x141 seen | 0x142 ack | 0x1C1 wait end | first removal (0x1C3) | Time left at the reading |
|---|---|---|---|---|---|---|---|
| idle | no I/O | +117.1 | +146.3 | +146.6 | +177.3, acknowledged | +2121.8, surprise | 2796 ms |
| I/O | `eio` QD 8 running, nothing held | +7.7 | +54.0 | +54.9 | +68.9, acknowledged | +5780.9 | 2796 ms |
| held | eight reads held at their fetch (QD 8; the eight discarded by the stage-2 reset) | +79.7 | +171.7 | +174.1 | +203.8, acknowledged | +2081.4 | 2796 ms |
| saturated | `eio` QD 32, every read held from the 150th: nine held at the controller, the rest queued in the port (the nine discarded by the stage-2 reset) | +138.2 | +138.5 | +140.8 | +144.4, acknowledged | +2139.5 | 2796 ms |
| bring-up | `x-rdy-set-delay-ms` 20000, unplugged 1 s after Stor98's bring-up entered its second step (`0x40` STARTING, then 1 to 7 at the unplug; leg `y2`) | +72.3 | +116.0 | +116.4 | +131.7, acknowledged | +2074.4 | 2796 ms |
| `RequestComplete` | `at=stage code=0x21 nth=300 act=gate` (fired 387.9 ms before the cutoff), then `device_del` | +62.0 | +155.3 | +155.8 | +187.5, acknowledged | +2065.9 | 2390 ms |
| already gone | `at=fetch op=read nth=150 act=gate` (fired 1406.0 ms before the cutoff), `device_del` 1 s later | +147.6 | +225.4 | +226.2 | +271.2, acknowledged | +2149.5 | 1375 ms |
| re-plug | unplugged as soon as Stor98's new instance traced its rendezvous (`0x140`) | +15.3 | +28.5 | +29.3 | +75.4, acknowledged | +2018.1 | 2796 ms |

Not counted: leg `y1`'s bring-up run. The unplug, 10 s after the plug,
came 404 ms after Stor98's start had already given up (`0x40` 1 to 8,
FAILED), so it was not an unplug during bring-up. The statement was
ordered as in every other run (+49.6, acknowledged at +144.0). The run
above, on leg `y2` (a fresh child overlay of the same golden), unplugs
while the start is held. The 2000 and XP bring-up runs were in STARTING
at the unplug (`0x40` 1 to 7).

### Windows 98 SE and ME, the same reading, after the interrupt fix

**98 SE and ME, D1, root port: every injection point run passed but one
form of one, and the owner chose (a) below on 2026-10-31: both bits are
set.** Since `af9e4c7d` the two systems are told apart by the VMM's
version: the first unplug read `cm.nvlink.u4-q35=00001012` on 98 SE and
`00001022` on ME (systems 98 SE and ME).

**First, the drive had no interrupt.** On the first 98 SE leg the plain
NVMe router was enumerated and given the tunnel id, and its wizard
installed Stor98. Then Device Manager showed Stor98 at Code 29 ("the
BIOS for the device did not give it an IRQ"), and Stor98 traced no
stage code. The NVMe sits at 01:00.0 directly on the tunnel root port's
secondary bus, pin A, Interrupt Line 0 (`info pci`). The 9x numbering
found no bridge (`cm.brnum.9x.outcome=00002800`, `NONE`), so its line
stage did not run, and design record 28's rule 5 handed the routable
function to that stage (`cm.reenum.9x.noirq.refused=01000205`,
EXCLUDED, why 5). This driver's gap, not the emulator's. The fix,
`99485b88` (record 28 section 13), runs the line stage on the port's own
bus when there is no bridge. With it, `cm.brnum.9x.irq=00010A01` (DONE,
line 10, pin A, one function set), the function started on IRQ 10, and
Stor98 ran (`0x40` to RUNNING).

- The vehicle: the q35 bases `w98se` (SHA-256 `ab1d4d65...`) and `me`
  (`-smp 1 -m 512`), `-Usb4 adl` as above. The release package was
  installed by `XHCIINST.EXE /dir release-x86 /yes /nopause /thunderbolt
  /stor98` (exit 3, then a shutdown and a cold start), and the drive's
  wizard installed Stor98 (`stor98w9.inf`'s class match). Then the trial
  `xhcicm.sys` of `99485b88` (the same stage codes, `0x1C2` at
  `CM_Query_Remove_SubTree`, the 98 SE and ME bits set, SHA-256
  `e5ab5254...`), Stor98's debug-port `stor98.mpd` (SHA-256
  `dc61511d...`) and `XhciCmLogVerbosity` 2. Goldens
  `E:\xhci98-vm\p45\w45-s98chain\w98\golden2.qcow2` and
  `...\me\golden.qcow2`. The drive is E:, the transfer disk D:.
- The load: an MS-DOS `copy /b E:\TESTDATA.BIN NUL`, the first read
  after a plug going to the drive. On 98 SE and ME the saturated point is
  one held request (owner, 2026-10-22, `runs/run-43.md`, "43.18"). The
  gate rules were armed only once the drive was running, and each run
  but 98 SE's first two had a fresh boot of a child overlay (below). The
  removal boundary is the worker's `CM_Query_Remove_SubTree` (`0x1C2`):
  on these systems only this driver removes the function.
- **The screen.** Where reads failed with the drive still listed, 98 SE
  and ME put up their system-modal "Disk Write Error" screen. While it
  stands nothing runs, this driver's thread included, as task 43.18
  recorded on ME. In the `RequestComplete` and already-gone runs below
  one Enter was sent 15 s after the unplug, so the removal could follow
  and its boundary be read.
- In every run there was no Slot Control write. Every counted run had no
  `0x83`.

| System, run | Injection | 0x1C0 statement | 0x124 seen | 0x125 ack | 0x1C1 wait end | 0x1C2 query-remove | Time left at the reading |
|---|---|---|---|---|---|---|---|
| 98 SE, idle | no I/O | +458.6 | +670.2 | +673.1 | +815.3, acknowledged | +7125.3 | 2794 ms |
| 98 SE, held | one read held at its fetch | +213.6 | +367.3 | +373.0 | +509.5, acknowledged | +5653.8 | 2780 ms |
| 98 SE, bring-up | `x-rdy-set-delay-ms` 20000, unplugged 1 s after Stor98's start entered its second step (`0x40` 1 to 7 at the unplug) | +207.6 | +363.0 | +364.5 | +391.8, acknowledged | +2368.6 | 2780 ms |
| 98 SE, `RequestComplete` | `at=stage code=0x21 nth=2 act=gate` (fired 315.2 ms before the cutoff), then `device_del` | +29.6 | +38.5 | +39.9 | +47.8, acknowledged | +15652.9 (the screen, Enter at +15 s) | 2588 ms |
| 98 SE, already gone | the same rule, Stor98 `gone` (`0x40` 2 to 7) first, `device_del` at once (fired 414.4 ms before the cutoff) | +184.4 | +192.5 | +193.4 | +199.5, acknowledged | +15604.6 (the screen) | 2382 ms |
| 98 SE, already gone, 1 s | as above, `device_del` 1 s after (fired 1587.5 ms before the cutoff) | +495.2 | +950.7 | +952.3 | +958.9, acknowledged | not read: the screen, no Enter | (not emitted) |
| 98 SE, re-plug | unplugged as soon as `cm.nvlink.up` reached the debug console | +377.1 | +1113.1 | +1115.3 | +1221.6, acknowledged | +6932.2 | 2794 ms |
| ME, idle | no I/O | +117.2 | +188.2 | +189.6 | +193.4, acknowledged | +2271.7 | 2780 ms |
| ME, held | one read held at its fetch | +202.0 | +224.5 | +226.0 | +233.5, acknowledged | +2493.4 | 2780 ms |
| ME, bring-up | as 98 SE (`0x40` 1 to 7 at the unplug) | +234.1 | +363.3 | +364.8 | +428.3, acknowledged | +2398.5 | 2739 ms |
| ME, `RequestComplete` | as 98 SE (fired 403.2 ms before the cutoff) | +149.8 | +198.8 | +200.0 | +218.8, acknowledged | +15896.2 (the screen) | 2381 ms |
| ME, already gone | as 98 SE, `device_del` at once (fired 395.7 ms before the cutoff) | +201.7 | +204.6 | +205.8 | +222.7, acknowledged | +15551.4 (the screen) | 2382 ms |
| ME, re-plug | as 98 SE | +19.0 | +192.1 | +194.0 | +255.9, acknowledged | +2185.5 | 2780 ms |

Two earlier runs, ME's `RequestComplete` (+116.7, +140.7, +142.4,
+144.5) and an already-gone with the `device_del` at once (+32.3,
+145.3, +146.8, +180.3), were ordered the same way, but had no Enter, so
the screen still stood when the leg ended and their removal boundary was
not read. They are partial readings and are not counted.

**The open case: ME, already gone, with the `device_del` 1 s after the
gate.** On ME the screen came up before the unplug was handled, in both
runs of this form (legs `mgone` and `pgone1`). It stopped the connection
manager's thread. In the first run no statement was made at all. In the
second, the unplug was handled only after the Enter, with no time left
of the 3 s, and the statement was withheld
(`cm.nvlink.u4-q35=00041020`). In both, Stor98's gone deadline completed
the request (`0x83` at +3820.5 and +3756.7). Nothing was stated out of
order; the statement was not delivered. On 98 SE the same form put the
statement first (the table above). Record 34 section 3.11 item 8 says
one failed run leaves a bit 0. Two readings of it:
- (a) The screen freezes the whole system, this driver's thread
  included, before the unplug is handled. What such a run shows is only
  the MD-1 fallback, as task 43.18 accepted for ME's screen, not a fault
  of the statement's ordering. Admit 98 SE and ME on the other runs.
- (b) Item 8 as written: the ME bit stays 0, and 98 SE's too until
  repeated runs never show the screen first.
Recommended: (a). The bit can only ever produce a statement made after
the endpoint's reset, as every run read. When the system freezes there
is no statement at all, and Stor98's own deadline applies, as everywhere
nothing is stated. **The owner chose (a), 2026-10-31**: both bits set,
record 34 section 3.11 item 8 noting the exception.

Not counted:

- **Gate rules armed before the plug** (98 SE, `RequestComplete` and
  already gone, first attempts). They fired on Stor98's own start reads,
  before the connection manager had learned the port's member, and the
  statement was withheld (`cm.nvlink.u4-down=00020000`, SEVERAL), so
  Stor98's gone deadline applied. A first already-gone rerun on a read
  rule never fired, the file being cached.
- **Re-plugs without an `UP` anchor.** In 98 SE leg `s3`, runs 3 to 6, in
  the same boot after a held read's vetoed removal, no new `UP` was
  published for the drive (`cm.nvlink.up.late-none`). There was no anchor
  and no statement (`cm.nvlink.u4-q35=00041010`). Why the entry stayed
  unpublished in that boot was not read further: the removal-request
  rules of record 21 section 7.2 part 3 are the place to look. From
  then on each run used a fresh boot. One fresh-boot re-plug (`ereplug`)
  was unplugged at Stor98's find (`0x120`), which on 9x comes before the
  late `UP`, so it had no anchor either. The re-plug above unplugs at
  `cm.nvlink.up`.
- **ME's stop at a cold start** (the first bring-up boot): "An error has
  occurred ... Error: 0E : 019F : BFF62170", before any plug,
  `E:\xhci98-vm\p45\w45-s98chain\me\ME0E-mbringup.png`. This is planned
  item ME0E.1's stop (recorded there as "0E : 0197 : BFF62170"), here at
  019F. The run was taken again on a fresh boot.

### What it changes

The 98 SE, ME, 2000, XP and XP x64 bits of the root-port D1 column
(`cmNvQ35Table`): every system item 5 admits (98 SE and ME on the
owner's choice (a)). D2 and M are deferred to
the next release with 45.15's correspondence and per-tunnel keying
(owner). Vista and later stay at 0
(Q11 (a)). The chained unit's eject, where the reading is expected to be
MIXED and the statement withheld, is read with D2.

## 45.27 (the owed legs) (2026-10-30, partial)

**Verdict: two of the owed legs are read here. On XP, `xhciuas.sys` refuses
every pass-through and SMART request a tool sends, so ATA pass-through and
SMART from a tool do not reach a UAS drive at all (an owner question, below).
Stor98's INTx fallback works: with the drive's interrupt enable stuck off it
reports `polling (INTx would not enable)` and its I/O passes. The rest stays
owed, each item named below.**

Vehicle: the package of `e8db6905` (`release-x86`, stamped `3.3.0.0`;
`xhci98.sys` d7f8ab2c..., `xhciuas.sys` fe57478a..., `stor98.sys`
81924f85..., `xhcicm.sys` 5fdb710d...). No build slot was free for this
session. The tip's `uas_pdo.c` answers the same two IOCTLs as that build,
and Stor98's read-back of the INTx enable is in it (`2f57cad2`).
`ATATEST.EXE` was built from `ff91c7ee` (43eef1b6...). Both legs ran on fresh
q35 XP SP3 overlays, installed with `INSTALL.BAT /console /yes /nopause`.
Evidence: `E:\xhci98-vm\p45\w45-owed27\xpa\` and `xpb\`.

### ATA pass-through and SMART from a tool (XP)

`xpa`: a `usb-uas` with one `scsi-hd` LUN on `qemu-xhci` port 3 and a
`usb-storage` (Bulk-Only, XP's own `usbstor.sys`) on port 4, both blank
64 MiB. `xhciuas` started at the install. Then `ATATEST \\.\PhysicalDriveN`
for N = 0 to 4 (`atatest-xp.txt`):

| Disk | Adapter (max transfer, bus type) | SCSI pass-through | ATA pass-through (12) and (16) | SMART IOCTLs, `IOCTL_ATA_PASS_THROUGH` |
|---|---|---|---|---|
| 0, 1: IDE (`atapi.sys`) | 128 KiB, ATA | INQUIRY in and unspecified: GOOD | refused, error 1 | answered: SMART version 1.1, READ DATA 512 bytes, IDENTIFY "QEMU HARDDISK", CHECK POWER MODE |
| 2: Bulk-Only (`usbstor.sys`) | 64 KiB, USB | INQUIRY in GOOD; unspecified refused, error 87 | reached the drive: CHECK CONDITION, sense 5/20/00 (QEMU's disk has no SAT); unspecified direction refused, error 87 | error 50 (not supported) |
| 3: UAS (`xhciuas.sys`) | 1 MiB, USB | **every request refused, error 1** | **refused, error 1** | **refused, error 1** |

Error 1 is ERROR_INVALID_FUNCTION, the Win32 code for
`STATUS_INVALID_DEVICE_REQUEST`. That is what `uas_pdo.c`'s device-control
dispatch completes for any IOCTL other than `IOCTL_STORAGE_QUERY_PROPERTY`
and `IOCTL_SCSI_GET_ADDRESS`. The disk class driver passes
`IOCTL_SCSI_PASS_THROUGH` and `_DIRECT` down to the port, and for a UAS LUN
the port is `xhciuas`'s LUN PDO. So the UAS driver's T_DIR handling
(`UasCdbDataDirection`, `2f57cad2`) is never reached from a program on NT.
Neither is any other SCSI command a program sends. The Bulk-Only stick under
XP's own driver shows the route working. The finding is
`xhciuas`'s and not new: the PDO never served pass-through. Serving it is a
new interface, not a fix (the owner's question, below). 98 SE and ME were
not read: a program there would go through ASPI, which was not tried.

### Stor98's INTx fallback (XP)

The emulator is usb4-emu's QEMU at `e05bf66`, the tree the q35 bases'
emulator was built from, plus one test property on QEMU's NVMe model,
`x-intx-stuck`. When on, the Interrupt Disable bit of the Command register
is set and read-only, so a driver that clears it reads it back set. It is
built on development host A (SHA-256 f904ca84..., `qemu-intx\intx.diff` in
this kit's scratch) and launched with `-global nvme.x-intx-stuck=on`; QMP
read the property back as true on `tp0-sw1-nvme`.

`xpb`: ASM4242 model, the ORICO enclosure at power-on. `INSTALL.BAT /console
/yes /nopause /thunderbolt /stor98` ended with exit 3, then a restart. After
the restart, `HKLM\System\CurrentControlSet\Services\stor98` held `StorInterrupt
5.0.0 = pin 1 line 23 msi no msix yes port 23/23 polling (INTx would not
enable)` and the drive was mounted as "ORICO (D:)". Three system files (from
`shell32.dll` to `xpsp2res.dll`) were copied to D: and compared clean with
`fc /b`. The compare may have been served from the cache. PASS (`intx-xp.txt`).

### Owed

- A UAS STOP and START with a LUN gone, and a short REPORT LUNS answer. The
  LUN list is read again only in `uasFdoStart` after an
  `IRP_MN_STOP_DEVICE`, and no guest sends a USB function a STOP. That takes
  a resource rebalance, which no guest can make (run-36, L2). A short answer
  only matters on that second start. Owed to a vehicle that can rebalance,
  or to a host test of the start's presence logic.
- ATA pass-through and SMART from a tool: blocked on the owner's decision
  below. On 98 SE and ME, not read.
- A BUSY that keeps its length: not provoked. The harness route is the
  tunnel port's `x-rdy-set-delay-ms` past the hold bound, with a dropped
  read forcing a reset during a copy.
- The connection manager's eject or PCIe loss with an unplug during it, and a
  resume with a lane adapter gone. Its no-IRQ clock and retry wait on 98 SE. A
  failed D0 during a re-enumeration on 98 SE.
- The power gate's wait under an ME standby.
- A D0 overlapping a system power IRP on 2000, XP and 7. A resume completing
  does not show the overlap; only a trace at `hcdPowerDefer` with both items
  queued would.
- The hardware readings stay owed to the bench, as listed in the section above.

### Owner question: pass-through for UAS drives

`xhciuas.sys` does not serve `IOCTL_SCSI_PASS_THROUGH`,
`IOCTL_SCSI_PASS_THROUGH_DIRECT` or the SMART IOCTLs, so SMART and
drive-identification tools see nothing on a UAS drive on 2000 to 7. Options:
(a) leave it, and say so in the release notes' known limitations;
(b) serve SCSI pass-through, buffered and direct, in the LUN PDO:
administrator rights as the system checks them, the user buffer checked,
the request built as an SRB through the existing engine with its timeout
clamped, sense returned, the CDB's own direction (T_DIR) used where the
caller leaves it unspecified; then legs with this tool on 2000, XP and 7;
(c) (b) plus the SMART IOCTLs, translated to ATA PASS-THROUGH.
Recommendation: (b), behind a design record section reviewed first. SMART
tools on these systems send SCSI pass-through to USB bridges, and (c) adds
a translation layer of the driver's own.

### The second session: Stor98's BUSY and the connection manager's legs (2026-10-31)

**Verdict: the eject with an unplug during its teardown passes on 2000,
and the PCIe loss with an unplug during it passes on XP. A BUSY that keeps
its length was not provoked on the release build. On XP the eject of the
enclosure, whose PCI bridge keeps ACPI's bus filter, leaves the tunnel up:
the known limitation of record 21 section 7.4. On XP and 2000 a unit attached through the install is not
listed by Windows after a restart, only after a re-plug, where the release
notes say a restart lists it (a question below). The resume with a lane
adapter gone cannot be made on the ASM4242 model on XP: the guest offers
no hibernation. The 98 SE and ME legs were not reached.**

Vehicle: the package of `e8db6905` as above, plus `XHCIMARK.EXE` built
from `a2f703f2` (`xhcimark\build.cmd x86`, SHA-256 `2c7acbeb...`), which
the test package does not carry. The emulator is the q35 bases' ASM4242
build (`qemu-system-x86_64.exe` `da60139f...`), the ORICO enclosure model
(ASM2464, its NVMe as Stor98's drive) on tunnel port `02:00.0`. The tunnel
port, the cable and the reply-fault rules were driven by QMP (`tp0`
`tunnel`, `enc0` `x-cable`, `u4` `x-reply-faults` and
`x-reply-fault-state`). Evidence: `E:\xhci98-vm\p45\w45-owed27\xpb\`
(the XP overlay of the first session, tags b to h), `w2a\` (a fresh 2000
overlay) and `xpc\` (the hibernation controls).

#### A BUSY that keeps its length (XP)

QEMU's NVMe model reports `CAP.TO` 7.5 s. Stor98's hold bound for a disk
request with a 10 s timeout is 5 s. Each run was `XHCIPERF /size 32
/verify` with `seq,256,4,1` on D: (`busy-run1.txt`, `busy-controls.txt`):

| Run | Result |
|---|---|
| Control | write 62.3 MB/s, read 136.9 MB/s, no failure |
| A write dropped at the tunnel port (`x-faults` `at=fetch op=write nth=200 act=drop`) | "Errors: fast fails 0, busy 0, resets 1"; one request failed; the verify passed |
| The same, and the port's ready delay (`x-rdy-set-delay-ms`) 6.5 s | busy 0, resets 1, the verify passed (also with an ATATEST loop beside it). The pause after the port's reset appears to cover the hold window, so no BUSY is answered |
| Ready delay 15 s, past `CAP.TO` | the bring-up failed, as Stor98's design says it does past `CAP.TO`: Disk event 11 many times, "Delayed Write Failed" on D:, later writes failed until a restart |
| A start with the ready delay 6.5 s from power-on | D: found, `StorInterrupt` "interrupt", a copy and `fc /b` clean; consistent with a hold and then BUSY at the bus scan, but the release build counts BUSY only inside an XHCIPERF interval |

Owed: the BUSY and its re-issue read on a Stor98 build with its debug
port, which logs the hold and the BUSY.

#### The eject confirmation on XP: why none came

The first session's XHCITR eject on XP ended with "The Thunderbolt driver
has not confirmed that the PCIe tunnel was closed", and the tunnel stayed
up. The account is an administrator (`net localgroup administrators`), so
XHCITR confirms an eject itself when Windows lists the unit. Two causes:

- The test package carries no `tools\xhcimark\XHCIMARK.EXE`, so the
  installer said "This package has no tools\xhcimark\XHCIMARK.EXE: the
  safe-removal mark service is not installed" (`C:\XHCIINST.LOG`). It is
  the test cut's limit, not a defect. With the file added, `INSTALL.BAT
  /console /yes /nopause /thunderbolt /stor98` again: "The Thunderbolt
  safe-removal mark service xhcimark is installed and running", exit 3;
  its log: the unit's mark written at once.
- After that install and a restart, Windows still did not take the unit
  as removable: Status `0x0180200A`, "Removable: no". XHCITR's eject took
  the route for a unit not marked removable (Windows' removal of the
  subtree, which confirms nothing): "Until you unplug it, Windows may
  start it again when it next looks for new hardware. The Thunderbolt
  driver has not confirmed that the PCIe tunnel was closed." That is the
  route's documented text (record 24 section 7.2). After a re-plug
  (`x-cable` false, then true) the unit read `0x0180600A`, "Removable:
  yes", and Safely Remove Hardware listed it ("PCI standard PCI-to-PCI
  bridge at PCI Slot 2"). 2000 read the same after a cold start (below).

For the whole time before the re-plug, XHCITR showed "Safe-removal mark:
listed by Windows" beside "Not marked removable". The mark line reads the
driver's own marks answer (its filter answered `Removable`), not Windows'
capabilities, so the two lines disagree.

#### The eject on XP once listed

With the unit listed, XHCITR's eject (as administrator) said "Windows has
stopped QEMU NVMe Ctrl SCSI Disk Device (D:). The eject was confirmed to
the Thunderbolt driver. The Thunderbolt driver has not confirmed that the
PCIe tunnel was closed." The tunnel was still up 40 s later. The driver's
log: `cm.reenum.confirm=00000200`, then `cm.reenum.eject.stale=00000200`
with `cm.reenum.eject.stale.why=00020208` (bit 3: a function behind the
bridge still reads as having a stack; the generations equal). A second
eject after another re-plug, with no fault rule armed, read the same
(`00030308`). The enclosure's bridges sit below ACPI's bus filter
(`cm.reenum.child.under` `ACPI`), and that filter stays on the ejected
bridge, so the connection manager withholds the teardown. That is record
21 section 7.4's open limitation and the release notes' "Known
limitation: the eject under the USB4 connection manager on 2000 to 7",
now read on XP too. So the reply-drop rule armed for the teardown
(`pdf=write act=drop+unplug dev=enc0`) never matched on XP, and the leg
was run on 2000's Standard PC HAL (no ACPI) instead.

One unplanned reading: the first re-plug was made with that rule still
armed. It matched 28 write answers of the re-enumeration and fired once, on
the first (that answer dropped, the enclosure pulled). The enclosure came back on its own, D: was
there, and the unit was listed.

#### The PCIe loss with an unplug during it (XP)

`W.BAT D:` (a 16 MiB copy and `fc /b`, looped) ran. `tp0` `tunnel` was
set to `surprise`, and `enc0` `x-cable` to `false` 3 s later. Copy 22
completed, then each copy failed at once (`COPYFAIL`). XP showed "Windows -
Delayed Write Failed ... D:\W21.BIN". There was no hang. XHCITR's tray
showed "A Thunderbolt or USB4 device was removed unexpectedly." Its report
read "Unexpected removals: 1 since this start; the last on tunnel port
02:00.0", Liveness "answering" and Stage "active". The log held
`u4.linkdown`, `cm.reenum.departure=00000200` and
`cm.reenum.unexpected=00000200`. After `x-cable` true, D: was back, and a
16 MiB copy compared clean. PASS (`tr-loss-g.txt`, `g13.png`).

An unplug 0.8 s after OK in XHCITR's eject dialog landed before the
tool's own check. The answer was "The device list changed since it was
shown; nothing was stopped. Look at the tree again." After a re-plug,
`fc` was clean (`g15.png`).

#### The eject with an unplug during its teardown (2000)

`w2a`: a fresh q35 2000 SP4 overlay (Standard PC HAL), ASM4242 with the
enclosure at power-on, the package with `XHCIMARK.EXE` installed
(`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`, the signature
prompts and Windows' wizards answered; exit 3; the mark service installed;
Stor98 started), then a cold start. As on XP, the unit read "Removable: no"
until a re-plug. After the re-plug it read "Removable: yes" (an Unsafe
Removal dialog for the re-plug's unplug, as expected). Then:

- armed `u4` `x-reply-faults` `pdf=write act=drop+unplug dev=enc0`;
- XHCITR (administrator) `/eject` of the unit, OK.

The rule read `rule0=7/1` within 6 s: seven write answers matched, and
the first of them (the rule has no `nth`) was dropped with the enclosure
pulled in the same moment, so the tunnel was down and the cable out. A
later teardown step's lost answer (`nth=7`, say) was not read. XHCITR: "Windows has stopped PCI standard
PCI-to-PCI bridge. The eject was confirmed to the Thunderbolt driver. The
PCIe tunnel was closed." The driver's log, in order:
`cm.reenum.confirm=00000200`, `cm.reenum.eject=00000200`,
`u4.eject.bridge=00000200`, `tbt.ring.ctl-no-answer`, three
`u4.quarantine` entries (the path's adapter entries left unconfirmed set
aside), `u4.eject=0000010E` (port 1, `TBT_E_STATE`: the teardown not read
back closed), then `cm.reenum.departure=00000200`. Liveness "answering",
Stage "active", counters `u4.ejects 1`, `u4.quarantined 3`. After
`x-cable` true the PCIe tunnel was built again (`u4.pcie.built 2`).
PASS: the dropped answer and the unplug left neither a hang nor a stuck
tunnel (`tr-b3-eject.txt`, `b6.png`).

After that re-plug, as after the first one, the NVMe sat at Code 10 with
no resources allocated ("Asked for: Memory 16384 bytes"), like the
hot-added functions of 45.13's 2000 legs on the Standard PC HAL ("Why
Code 10 on 2000"). It was not followed further here (`tr-b4-replug.txt`).

#### A resume with a lane adapter gone: not makeable on XP

The leg needs a hibernation with the enclosure attached and a start of
the emulator without it. On `xpb` with `-vga cirrus` and
`-global ICH9-LPC.disable_s4=0`, QMP read `disable_s4` 0 and `s4_val` 2.
`SetSuspendState` returned at once, and `powercfg /hibernate on` answered
"System does not support hibernation". A fresh XP overlay with no package
and the same machine answered the same. The same overlay without the
ASM4242 model (`xpc` tag b) accepted it. So the emulator's ASM4242 layout
takes S4 away from XP, not this package. The leg is owed to a vehicle
that hibernates with the model present (7 x86, or 2000 on its ACPI base).

#### Not readable in a guest

- A UAS STOP and START with a LUN gone, and a short REPORT LUNS answer:
  the LUN list is read again only after an `IRP_MN_STOP_DEVICE`, which
  takes a resource rebalance, and no guest makes one for a USB function
  (run-36, L2). Owed to a host test of the start's presence logic.
- A D0 overlapping a system power IRP: a completed resume does not show
  the overlap. Only a trace at `hcdPowerDefer` with both items queued
  would, and that is a code change (a question for the coordinator).

#### Owed after this session

- The BUSY that keeps its length, on a Stor98 debug-port build.
- The resume with a lane adapter gone (see above for the vehicle).
- The power gate's wait under an ME standby; the connection manager's
  no-IRQ clock and retry wait on 98 SE; a failed D0 during a
  re-enumeration on 98 SE.
- The D0 overlap's trace, after the coordinator's answer.
- The hardware readings, to the bench, as listed above.

#### Question: a unit attached through the install, after a restart

On XP (a warm restart) and on 2000 (a cold start), a unit attached during
the install still read "Removable: no" after the restart, and Windows did
not list it. A re-plug listed it. The release notes' known limitation
says such a drive is not listed "until Windows restarts or the drive is
plugged in again". Options: (a) narrow that text to the re-plug ("until
the drive is plugged in again"); (b) read why the restart does not list
it here (the filter's `Removable` answer against the capabilities query
Windows makes at the start) and fix it in `xhcicm.sys` if it is ours.
Recommendation: (a) for this release; (b) as a task. Separately, XHCITR's
"Safe-removal mark: listed by Windows" should not show beside "Not marked
removable": the mark line could read Windows' capabilities too.

### The third session: the D0 overlap's trace, ME's standby and 98 SE (2026-10-31)

**Verdict: the D0 overlap's trace is in the `qemu` flavour (`845481d4`,
record 15 section 8.1). Two XP hibernations under a write loop resumed
with no overlap line. The ME standby cannot be made on the q35 ME base.
The 98 SE connection manager's legs were not reached.**

Builds: the gated x86 build of `0ccb991d` (`BUILD + GATES PASSED (release
qemu)`, host tests passed), and of `0301f107` before it. Evidence:
`E:\xhci98-vm\p45\w45-owed27\xpe\`, `mea\`.

#### A D0 overlapping a system power IRP (XP)

`xpe`: a fresh q35 XP overlay without the USB4 model (`-vga cirrus`,
`-global ICH9-LPC.disable_s4=0`), a Bulk-Only stick on `qemu-xhci` (a
VVFAT folder), the release package of `0301f107` installed
(`INSTALL.BAT /console /yes /nopause`, exit 3), and then the `qemu`
flavour's `xhci98.sys` (`eda14070...`) copied over the installed one and a
restart. The installer takes no `qemu-x86` directory ("REFUSED: not a
flavour directory of the package"), so the swap is the way. Port `0xE9`
printed the driver's lines from that start on. `W.BAT D:` ran, and
`H.BAT` hibernated twice; each resume came back with the loop running
("H back"). No `hcd: power defer overlap` line was printed in the three
port logs (`debugcon-a`, `-b`, `-c`). After the first resume the loop read
one `CMPFAIL` and some `COPYFAIL` lines, then OK again: each start of the
emulator presents the VVFAT stick fresh, so the files the hibernated
Windows remembered were gone. That is the vehicle, not the driver. So the
case was not met on XP in two transitions. It stays owed on 2000 and 7,
and to hardware.

#### The power gate's wait under an ME standby: not makeable on q35

`mea`: a fresh q35 ME overlay with the package of `e8db6905` installed
(`I9.BAT`, exit 3) and a stick. "Stand by" from the Shut Down dialog
returned at once, and QMP's `query-status` stayed `running`, never
`suspended`, in three attempts. ME refuses a standby while an MS-DOS
window runs ("This program is not responding" for the copy loop), and
`rundll32 powrprof.dll,SetSuspendState` crashes `rundll32` on ME. Run-36
read ME's standby on the i440FX machine. The q35 ME base offers none, so
the leg is owed to a vehicle that sleeps. Also on this overlay: once,
after a shutdown that pressing `s` had turned into "Shut down", the next
start's shell never drew and `Cmdninst` was "not responding". A cold
start with ScanDisk then came up normally. That is not this package's,
as far as it was read.

#### Still owed

- The 98 SE connection manager's no-IRQ clock and retry wait, and a failed
  D0 during a re-enumeration on 98 SE: not reached.
- The power gate's wait under an ME standby (above).
- The D0 overlap on 2000 and 7 (hibernation there), and hardware.
- The BUSY that keeps its length, on a Stor98 debug-port build.
- The resume with a lane adapter gone (the ASM4242 model takes S4 from XP).
- The UAS STOP and START, and the hardware readings, as above.

## 45.20 USB4 version 2 links: the code and the q35 legs (2026-10-30)

Design record 22 section 6.2 coded, as the design half above left it
owed: `020e930a` to `90c829fd` (branch `w45-u4v2c`, merged at
`0f645d38`), reviewed and revised.

### What the code does

- CMUV 20h written to a router whose USB4 Version has major 2, 10h to a
  version 1 router; a preserved router's CMUV read and kept (`10h` on a
  version 2 router and any odd value logged).
- Set Capabilities for the Sequence bit capability on every USB4 router
  that reports it supported and disabled, with a Get Capabilities
  read-back deciding the acknowledgement form; the router's notifications
  held until then. A recovery sends no Set: one it finds owed is made at
  the next tick as the port's own sequence.
- Every path entry written as two dwords, on every router.
- HiFi Enhanced Uni-Directional TMU on a link of two version 2 routers
  whatever the tunnels, kept until unplug; HiFi-Bi elsewhere as before.
- A preserved router found with CV set never configured again: PTO clear
  holds the port, UTO clear means no USB3 tunnel, a read that fails holds
  it.
- A predecessor's Enable Gen 4 Link Recovery cleared where it reads set
  on each preserved link the discovery walks: at depth 1 a clear that
  fails holds the port; below it a failure is logged and the link stays
  a predecessor's. LINK_RECOVERY delivered (never acknowledged) as a port
  check; what a recovery leaves unverified (a USB3 tunnel, deeper PCIe and
  DisplayPort tunnels) is record 22 section 6.2.3 item 4's limitation.
- PCIe Extended Encapsulation set on both adapters before Path Enable on
  a tunnel between two version 2 routers, cleared after the Path Enables
  at its teardown, and cleared before a tunnel with a version 1 end.
- Gen 4 links classified only on an active lane: symmetric, or
  asymmetric with the two ends a 3-transmitter and 3-receiver pair, used
  as bonded with no lane write and never transitioned (decision U31);
  x1, unpaired or reserved widths, or a lane that stays training, FAILED.
- `XhciCmUsb4V2` (absent 1): 0 is the version 1 run of `3.3.0.0`.

Host vectors: the new suite `test_u4v2` (316 checks) over a version 2
host and device router added to the fake fabric; `test_u4cm`,
`test_tbt_ring` and `test_cm` follow. The gated builds of `22be413f` in
this worktree passed: x86 "BUILD + GATES PASSED (release qemu)" at
21:03, amd64 at 21:36 (development host A).

### The legs (2026-10-30)

`usb4-emu` main `230bed5` (QEMU fork `674ff08`, its version 2 router
model `cb36497`, docs/USB4-V2.md): `usb4-host` with `host-model=minimal`
and `usb4-version=2` on q35, two `usb4-tunnel-port`s on `pcie.0` (the
`adl` layout), `usb4-device dialect=usb4 usb4-version=2` with the NVMe
test disk at power-on; plugs, unplugs and link events over QMP
(`device_add`, `device_del`, `qom-set ... x-link-event`). The package of
`22be413f`: the release package installed (`INSTALL.BAT /console /yes
/nopause /thunderbolt /stor98`), then its `qemu` `xhcicm.sys` copied over
the installed one and the guest restarted, so the connection manager's
log reads on the debug console (x86 release `xhcicm.sys` `ddc788ca...`,
qemu `5ef8374c...`; the amd64 images staged unsigned, `b08203e4...` and
`4532501767...`, signed in the guest with a throwaway certificate made
there, test signing on, nothing signed on the host). Fresh overlays of
the q35 bases; evidence under `E:\xhci98-vm\p45\w45-u4v2c\<leg>\`
(`steps.txt`, `debugcon-a.log`, `trace-a.log` with `-trace usb4_*`
events, screenshots).

| System | Configuration | Result |
|---|---|---|
| 7 x86 (`w7`) | v2 host, v2 device at power-on | Gen 4 x2 (`u4.link.gen4=00000102`, no lane write), CMUV 20h on both routers (ROUTER_CS_4 `0x400020ff`), Set Capabilities `32h` metadata `102h` on both and the Sequence bit read back enabled (`u4.caps.seq`), EE written 1 on both PCIe adapters before Path Enable, `u4.tmu.enhanced`, PCIe and USB3 tunnels up, ORICO (D:), the tray "connected (USB4 v2.0)" |
| 7 x86 | the same, then unplug and plug `link-width=asym-tx` | `u4.link.gen4=00000104`, both tunnels, D: |
| 7 x86 | `x-link-event=sym` on it (ASYM_LINK) | the link symmetric, nothing torn down |
| 7 x86 | `x-link-event=elr`, then `error` | the emulator's Gen 4 Link Recovery: LINK_RECOVERY noted (`u4.note=00002601`), a port check, the tunnel up throughout |
| 7 x86 | plug `asym-rx`, then `error` with the device's ELR clear | `u4.link.gen4=00000108`; the error a disconnect: teardown (`u4.linkdown`), the router back 50 ms later, enumerated and its tunnels rebuilt |
| 7 x86 | a version 1 device behind the v2 host | Gen 3, CMUV 10h on it, no EE written, no enhanced TMU, Set Capabilities sent (the model's version 1 routers report the capability), D:, the tray "USB4 v1.0" |
| 7 x86 | `XhciCmUsb4V2` 0 (machine-wide value), restart, v2 device | `cm.usb4.v2=00000100`; CMUV 10h on both version 2 routers, no Set, no EE, no enhanced TMU; the Gen 4 link still used as bonded, both tunnels up |
| 7 x86 (`w7r`) | the version 1 regression: `asm4242` host, ORICO at power-on (pre-OS tunnel) | the tunnel preserved (`u4.preserved=00010808`), Set Capabilities to the preserved routers, D: ORICO (TESTDATA.BIN); hot unplug and re-plug rebuilt, D: back |
| 10 x64 (`w10`) | v2 host, v2 device at power-on | as on 7 x86: Gen 4 x2, CMUV 20h, Set, EE, enhanced TMU, both tunnels; ORICO (E:) |
| 10 x64 | `asym-tx` plug, `asym-rx` transition, `elr` and `error`; `asym-rx` plug; a version 1 device; `XhciCmUsb4V2` 0 and restart | each as on 7 x86 (LINK_RECOVERY a port check, the tunnel kept; the version 1 device at Gen 3 with CMUV 10h; the value 0: CMUV 10h, no Set) |
| XP (`wxp`) | v2 host, v2 device at power-on | the connection manager as on 7 x86 (CMUV 20h, Set, Gen 4 x2, enhanced TMU, both tunnels; `asym-tx` plug and the recovery likewise); but no ORICO volume |
| XP (`xpa`) | control: the version 1 `adl` host, ORICO at power-on | the tunnel up, and no ORICO volume either: not 45.20's |

The XP volume: on this layout (tunnel root ports on `pcie.0`, no pre-OS
tunnel) the NVMe behind the tunnel gets no volume on XP with a version 1
host as with a version 2 one; the `3.2.0.0` base reading had ORICO (D:)
on XP with the ASM4242 model and its pre-OS tunnel. Reported to the
coordinator, not read further here.

### What is left

The bench reading on a real version 2 router (45-E.10). 45.20 is done
here otherwise; ticked.

## 45.43 a UAS disk attached at a cold start on XP x64: enumerated, its driver install waiting on Windows' wizard (2026-10-30)

**The question.** 45.42's side finding: on the XP x64 guests with
`3.3.0.0`, a `usb-uas` attached at power-on showed no disk, and a replug
from the emulator brought it. Is the connection missed at the
controller's start (the root port's first look, a lost change, a
companion handoff), an emulator timing, or something else?

**The legs.** q35, the stock emulator of `launch-q35.ps1` (not 45.41's
link-drop build), `-smp 2`, 2 GB, `qemu-xhci` behind the root port, the
tablet on port 2, a `usb-uas` (serial `W45COLD1`, the FAT test disk
overlay) on root port 3 at power-on, nothing else on the bus. Disks:
overlays of 45.41's kit disks with `3.3.0.0` installed (`prep330x64`,
release x64: `xhci98.sys` `9e6a132b1ef98158...`, `xhciuas.sys`
`69544021c6b1468a...`; `prep330`, release x86: `8ff8d0d2b9e9d212...`,
`24e7a9d40b37d5b4...`); no UAS device had ever been attached to either.
For this tree: the release amd64 `xhci98.sys` (`a333fa5239915244...`) and
`xhciuas.sys` (`6fad01072dc9414a...`) of `f4fbf96d` (no source change in
`src\` or `src\uas\` from there to `42093a03`), copied over the installed
ones (`fc /b` equal) as 45.41's x64 legs did. Each start read with
`XHCITR /snapshot`, `setupapi.log` and Device Manager. Evidence:
`E:\xhci98-vm\p45\w45-cold\` (`c1\`, `c2\`, `x1\`: `serial-*.log`,
`C2A.txt`, `C2B.txt`, `s*.png`).

| Leg | System, build | The device at the start | Windows |
|---|---|---|---|
| c1 a | XP x64, `3.3.0.0`, cold start, first time the device is seen | root port 3's first look: "machine Empty, fed connect; PORTSC CCS PED PLS 0, changes CSC"; reset, slot 1, **addressed**, transport UAS | the PDO `USB\VID_46F4&PID_0003\W45COLD1` exists: Device Manager lists "USB Attached SCSI HBA" under the USB controllers at **Code 1**; `setupapi.log`: `xhciuas`'s `oem1.inf` found, then `#E358 ... blocked (server install). Error 1168` and `#E122 Device install failed`; the Found New Hardware wizard waits (behind the one for ACPI `QEMU0002`, which has no driver on any start) |
| c1 a, then | the same, Reinstall Driver | - | the unsigned-driver warning, Continue Anyway, installed; slot 1 **configured** |
| c1 b | XP x64, `3.3.0.0`, cold start, the device installed | the same first look; slot 1 configured, UAS | `D:` ORICO listed |
| x1 a | XP x86, `3.3.0.0`, cold start, first time | the same first look; slot 1 addressed | the same `#E358` (error 1168) for `oem1.inf`, the wizard waits |
| c2 a | XP x64, this tree's build, warm restart after the copy | slot 1 configured | - |
| c2 b | XP x64, this tree's build, cold start | the same first look; slot 1 configured, UAS | `D:` ORICO listed |
| c3 a | XP x64, `3.3.0.0`, cold start, first time, 4 GB (memory above 4 GB, as 45.42's 3 GB and 8 GB legs) | the same as c1 a | the same as c1 a; a replug from the emulator (`attached` false, then true) changed nothing: no new install attempt in `setupapi.log`, the wizard still waiting |
| c4 a | XP x64, `3.3.0.0`, cold start, first time, 45.42's configuration as far as this kit's limits allow: 45.41's link-drop emulator build (a copy), 3 GB, a USB 2.0 hub on port 1 with a USB keyboard behind it | the same first look on port 3; slot 1 addressed, UAS; the hub (port 5) configured | the same `#E358` for `oem1.inf`, and one for the hub (`xHCI98 USB Hub`, `oem0.inf`): the keyboard behind it does nothing until that wizard is answered (the emulator's keys went to it until it was removed); a replug of the disk changed nothing, slot 1 still addressed |
| x2 b | XP x86, this tree's build (the release x86 `xhci98.sys` `f18a3c4e795ef0ed...` and `xhciuas.sys` `989265a07bf16dc5...` of `f4fbf96d`, copied over `3.3.0.0`'s on a fresh overlay), cold start with a device of a new serial (`W45COLD3`) | the same first look; slot 1 addressed | the same `#E358` for `oem1.inf` on `W45COLD3`, the wizard waits |

**Reading.** The connection is seen at every start. The root hub's
first reading marks every root port changed (`XhciRootHubInit` in
`hcd_dev.c` sets every bit of `PortChange`), so the first look reads
each port whatever its change bits say; a change the controller
announced before the run, or none at all, cannot hide a device present
at the start. On the emulator the port is already enabled in U0 with CSC
set, and the first look took it as a connect, reset it, gave it slot 1
and addressed it, and the hub built its PDO. What stops at the first
plug is Windows' install of `xhciuas`: the package is not
WHQL-signed, and under the default driver-signing policy (Warn) a
server-side install of an unsigned driver is refused on XP and XP x64
(as recorded in run-22 to run-24, and for Stor98 in design record 31
section 6, which also records it going through under Ignore), and the
device waits on the
Found New Hardware wizard and the unsigned-driver question, as README
step 5 says ("A UAS disk ... asks for its driver when plugged in"). Until
it is answered the device is listed at Code 1, not under the disk
drives, and the slot stays addressed. Once installed, the same device at
a cold start is configured and its volume mounted, under `3.3.0.0` and
this tree alike.

**What is not reconciled.** 45.42's record says no PDO and no problem
device, and that a replug brought the disk at once. Its leg showed a
Found New Hardware window on its first start
(`E:\xhci98-vm\p45\w45-hubuas\f42m8\s1.png`), but which device it was
for is not recorded (every start here also has one for ACPI
`QEMU0002`), and here a replug of the waiting device changed nothing.
Leg c4 took that leg's configuration (but 3 GB, not 8 GB, this kit's
limit being 4 GB) and read the same as c1. So these legs explain what
they show; 45.42's "a replug brings it" is not reproduced, and its 8 GB
case is not read.

**Result.** No defect of this driver found, and no change: in these
legs the device is enumerated at every cold start, and a UAS device seen
for the first time on XP and XP x64 under the Warn policy waits on
Windows' wizard, at a cold start as at a plug. Owed: 45.42's 8 GB case
and its replug (above).

## 45.40 an RTL8157 on XP losing its static settings and taking the default route: read, reproduced, and the identity kept, behind hubs too (2026-10-30 and 31)

**Verdict: the report's symptom is what a new instance id gives, and a
failed serial read at a re-enumeration is a way the bus gives one;
reproduced on q35 XP and 7 with `3.3.0.0`.** XP and 7 keep a network
adapter's TCP/IP settings under its devnode's `NetCfgInstanceId`. A
devnode whose USB instance id changes is a new adapter: Windows installs
it with DHCP, and the old one's static settings stay on a phantom. With
the LAN's DHCP server handing out a router, the new adapter's default
route can outrank the VPN adapter's (in the legs the simulated 5 Gbit/s
adapter's automatic metric, 10 on XP and 5 on 7, was below the other
adapter's 20). The bus names a device with a usable serial by that
serial, and falls back to the location form when three serial reads in a
row fail; a re-enumeration (a link recovery, a cycle, a controller
recovery, a replug) reads the serial again. Design record 13 section 10.7,
"Identity across a re-enumeration", now keeps a device that was
serial-named at a root port from being renamed there by a read that
fails. The user's own event is not read (owed: their `ipconfig /all`,
`route print`, the `Enum\USB\VID_0BDA&PID_8157` keys and an `XHCITR`
snapshot). `rtl815x` itself cannot change the devnode (below).

### The reading

- **Windows.** An adapter's TCP/IP settings live under its devnode's
  `NetCfgInstanceId`; a devnode with a new instance id is a new adapter,
  installed with DHCP. That is the mechanism the legs reproduce. The
  symptom alone does not prove it: DHCP can be turned on for the same
  adapter, and a default route can be added by other means; the user's
  instance paths and `NetCfgInstanceId`s before and after would.
- **`rtl815x` (usb-wdm-free 0.1.4, unchanged since).** Its NDIS Reset
  resets pipes, applies the filter and reads the link; Halt and Initialize
  keep the devnode; it sends no RESET_PORT or CYCLE_PORT; its address
  choice (`ChooseAddress`) touches no TCP/IP setting. It cannot make a new
  adapter. Separately, its `Recover` gives up after 30 failed recoveries
  and reports the adapter gone - a dead LAN with the settings kept, not a
  default route (a follow-up for that repository, not this task).
- **The bus.** Every path that re-enumerates a configured device builds a
  fresh device record and reads the serial again (root and hub link
  recovery, the tolerance cycles, slot-fatal recovery, CYCLE_PORT, a
  failed RESET_PORT, controller recovery, a resume that re-initializes the
  controller in `3.3.0.0`); a successful RESET_PORT, the resume keep and
  45.41's link keep (UAS only) keep the PDO. The read's three tries were
  back to back, and a STALL counted as a failure. A serial-less device
  keeps the location form, so one that comes back on another port or on
  its USB 2.0 companion is a new devnode too (not changed here).
- **A related reading.** The snapshot of task 45.41 (another user, VIA
  VL805, XP x64) shows a 5 Gbit/s RTL8157 on root port 2 whose link fell
  to SS.Inactive and was warm-reset, the device dropped and root port 2
  Empty at the dump: such link drops happen with this adapter on real
  hardware. Real RTL8157 units carry a MAC-like serial (a WisdPi WP-UT5
  reads `000334C8D6B113C7`).

### The vehicle

`usb-wdm-free`'s `rtlemu.py` (SIMULATED RTL8157, usbredir, SuperSpeed),
given `--serial` and `--serial-stall N` by this task (usb-wdm-free
`2b1c293`: a serial string at index 3, and the first N reads of it in a
run answered with a STALL). QEMU 11.1.0 (scoop build; the q35 emulator
snapshot has no usb-redir), q35 bases `xp` and `w7x86`, 2 vCPUs, one guest
at a time, overlays in `E:\xhci98-vm\p45\w45-rtl\`, QMP 49370, simulator
49371. A second adapter, QEMU's `rtl8139` on user networking (DHCP, router
10.0.2.2), stands for the VPN router. `rtl815x` 0.1.4 (`RTL815X.SYS`
`5e1ef6be...`, `RTLXP7.INF` `479bf50c...`, as `SHA256.TXT`). Each replug
is a `device_del` and a new simulator run on the same QEMU port.

### Before the change: `3.3.0.0`

XP SP3 (`xp1`, `3.3.0.0` release-x86, `xhci98.sys` `8ff8d0d2...`):

| Step | Instance | Adapter | Result |
|---|---|---|---|
| plug, serial read | `USB\VID_0BDA&PID_8157\000334C8D6B113C7` | Local Area Connection 2, DHCP | default route via 192.168.77.1 metric 10, the rtl8139's metric 20 (`p1c`) |
| set static 192.168.77.50/24, no gateway | same | static | default only via 10.0.2.2 (`p1d`) |
| replug, 2 serial reads STALL | same | static kept | (`p2a`) |
| replug, 3 serial reads STALL | **`USB\VID_0BDA&PID_8157\6&58728d&0&1`** | a new one, "... (RTL815X) #2", Found New Hardware wizard, DHCP 192.168.77.2 | **default route via 192.168.77.1 metric 10**; the static address gone (`p3b`) |
| replug, serial read | the first | static again | (`p4a`) |

7 SP1 x86 (`w7a`, `3.3.0.0`): the adapter "Local Area Connection 3" set
static (`s10`); a replug with 3 STALLs made `USB\VID_0BDA&PID_8157\6&1a1ca75f&0&1`,
**installed with no prompt at all**, DHCP 192.168.77.2, default route via
192.168.77.1 metric 5 against the rtl8139's 20 (`s11`).

### The change

Design `f2916a2d`, code `54db4c51` (record 13 section 10.7, "Identity
across a re-enumeration"; reviewed and revised):

- the serial reader's tries 100 ms and 400 ms apart;
- a root port remembers the last non-hub group listed for a device on it
  directly (its ids, whether it answered the serial form);
- a covered arrival whose read fails or is refused where its serial form
  was is read again in place, up to three more reader runs 1 s apart,
  cancelled on a departure, a connection change or a halted controller;
- runs spent: no PDO, the port Failed and latched, `serial.defer.fail`;
  the machine's own retry fails at once; the next connection (a replug, a
  recovery's re-enumeration) is published whatever its read gives;
- `id.form` and `serial.defer*` notes; counters `serial.deferred`,
  `serial.defer.ok`, `serial.defer.fail`; four words appended to the HCD
  region's root-port record (20 words), printed by `XHCITR` as an
  "identity:" line.

Host vectors: `test_func` (`test_serial_defer`, 12 cases), `test_snap`
(the 20-word record), `XHCITR`'s `-selftest-hcd` (an Empty port kept for
its identity line, a latch and a bypass).

### After the change

Build: `54db4c51`, the gated x86 build (release and qemu, "BUILD + GATES
PASSED (release qemu)", the host suites passed; release `xhci98.sys`
`a49d7846...`, 327552 bytes), and `XHCITR.EXE` of the same tree
(`77cdc6a9...`) for the in-guest snapshots. Each guest a fresh overlay with
`3.3.0.0` installed, this tree's `xhci98.sys` copied over the installed
one and the guest restarted; the loaded file read back (327552 bytes,
`driverquery` dated 30-Oct-26 10:55 PM). The adapter set static as before.

| Step | XP SP3 (`xp2`) | 7 SP1 x86 (`w7b`) |
|---|---|---|
| plug, serial read; set static | `...\000334C8D6B113C7`, static, default via 10.0.2.2 (`a6`) | the same, "Local Area Connection 3" (`b7f`) |
| replug, 3 serial reads STALL | **the same devnode, static kept** (`a7`) | **the same** (`b7g`) |
| replug, 12 STALL (every run spent) | no adapter and no new instance; root port 1 Failed, cause PDO, its slot given back; the snapshot's notes `serial.defer=00010004`, runs `...0104`, `...0204`, `...0304`, `serial.defer.fail=00010004`, and "identity: last listed 0BDA:8157 on the serial form; 2 deferred, 1 spent; failed, waits for a new connection" (`a8` to `a10`) | the same notes and line (`b7h`, `b7i`) |
| replug, every read STALLs (the bypass) | published on the location form, `...\6&58728d&0&1` - the way out, as designed; its wizard cancelled (`a11`) | not run |
| replug, serial read | the first devnode, static (`a12`) | the first, static (`b7j`) |

The 3-STALL replug's notes (XP): `serial.defer=00010004` (the first read
failed where the serial form was), `serial.defer=00010102` (run 1 read
it), `id.form=00010201` (published on the serial form). Each simulator
run's trace counts exactly its STALLs (`ev\a*.jsonl`, `ev\b*.jsonl`).
Evidence: `E:\xhci98-vm\p45\w45-rtl\` (`shots\`, `ev\`, `pids.txt`), not
tracked.

### The owner's decisions (2026-10-30 and 31)

- Q1: a device whose reads are all spent stays invisible - no devnode, no
  Device Manager entry - until its next connection, as built; recorded in
  record 13 section 10.7.
- Q2: the protection extended to hub ports in this task (below).
- Q3: no shared key for a serial-less device across a USB 3 port and its
  USB 2.0 companion.

### Behind hubs (2026-10-31)

Design `cf96d6b6`, code `0bcb2b41`, and `f7f20e1a` (record 13 section
10.7, "Behind a hub"; reviewed and revised): a controller table of 32
places behind hubs, each named by its instance key (root port and Route
String), kept across the hub's own departure and re-enumeration; the
deferral's path check reads every hub port above the device and the root
port; a latched hub port is left enabled; the places are a block appended
to the HCD region's header (792 words), printed by `XHCITR` under
"identity behind hubs". `f7f20e1a` came from the first USB 2.0 hub leg:
QEMU's `usb-hub` raises C_PORT_ENABLE at every port reset with the port
enabled, and the first rule cancelled every deferral on it (the device
kept its devnode only because the next connection's reads passed), so
C_PORT_ENABLE alone no longer cancels; PORT_ENABLE clear still does.
Host vectors: `test_func`'s `test_id_places` (13 checks; the suite
913), `test_snap` (454 in all), `XHCITR`'s `-selftest-idplace` (one of the
snapshot self-test's 34 cases). The simulator gained
`--serial-skip K` (usb-wdm-free `253f5a0`): the first K serial reads
answer, so the first enumeration names the device and the re-enumeration
after a link drop fails.

Builds: the gated x86 build of `0bcb2b41` and of `f7f20e1a`, each "BUILD +
GATES PASSED (release qemu)" with the host suites passed; release
`xhci98.sys` of `f7f20e1a` `e1c5c00b...`, 328352 bytes; `XHCITR.EXE`
`0dae38ee...`.

The vehicle: 45.41's QEMU build with `usb-ss-hub` and its `link-drop`
(copied, not changed), a SuperSpeed hub on root port 1 and QEMU's USB
2.0 hub (`usb-hub`, Full Speed) on root port 3, the simulated RTL8157 on
port 1 of either; `-netdev hubport` for the second adapter (that build has
no user networking). Each guest an overlay with `3.3.0.0` installed, this
tree's `xhci98.sys` copied over and the guest restarted, the file read
back (328352 bytes).

| Leg | XP SP3 (`xp3`) | 7 SP1 x86 (`w7b`) |
|---|---|---|
| SuperSpeed hub port, static set; link dropped (`link-drop 1`), the re-enumeration's first 3 reads STALL | same devnode `...\000334C8D6B113C7`, static kept; `serial.defer=01000004` then `01000102`, `SsHubWarmResets` 1 (`h27`, `h28`, `g5`) | the same (`s23`, `s24`) |
| the same with 12 STALLs | no adapter, no new instance; "root port 1, route 00001: last listed 0BDA:8157 on the serial form; 2 deferred, 1 spent; failed, waits for a new connection" (`h29`, `h30`) | the same (`s27`) |
| clean replug after it | the first devnode, static (`h31`) | the same (`s28`) |
| USB 2.0 hub port, replug, 3 STALLs | same devnode, static; `serial.defer=010E0004`, `010E0102`, "root port 7, route 00001: ... 1 deferred, 0 spent" (`g2`, `g3`) | the same (`s25`, `s26`) |

(Root port 7 is the USB 2.0 half of QEMU's connector 3; `0x010E` is that
hub port's location number.)

### 98 SE and 2000 (regression, 2026-10-31)

- 2000 SP4 (`w2k1`, scoop QEMU 11.1.0, 1 vCPU): `3.3.0.0` installed,
  `f7f20e1a`'s `xhci98.sys` copied, cold start; the simulated RTL8157 at
  Full Speed behind QEMU's USB 2.0 hub installed from `RTL815X.INF`
  (DevicePath), set static; a replug with 3 STALLs kept
  `USB\VID_0BDA&PID_8157\000334C8D6B113C7` alone and the static address,
  ping answered (`k31` to `k35`).
- 98 SE (`w98`, the q35 base emulator: the scoop and hub-model QEMU builds
  stop at 98 SE's logo on q35, not read further): `3.3.0.0` installed
  (`XHCIINST`, exit 3), `f7f20e1a`'s `xhci98.sys` copied, cold start; a
  QEMU USB 2.0 hub and a tablet behind it installed, the tablet unplugged
  and plugged again, no wizard, the pointer moved by it (`n12` to `n15`).
  No RTL8157 leg on 98 SE: that emulator has no usb-redir.

### Not read

- The user's own event and their adapter's serial behaviour.
- A serial-less device back on its USB 2.0 companion port (still a new
  devnode; owner, Q3).
- ME; a hub's own SuperSpeed link loss above a device (the legs drop the
  device's hub port); 98 SE with the simulated adapter.

A user who meets this before the fix can keep the VPN adapter ahead by
giving it a fixed interface metric of 1 (its TCP/IP Advanced settings,
"Automatic metric" off); the adapter left with the static settings shows
under Device Manager's hidden devices.

## 45.15 (legs) the minimal chain on 98 SE, ME, 2000 and 10 x64 (2026-10-31)

Worker w45-u4chain2, development host A. Code at `d73e4b10` (`f1e9ec85`
and record 34's revision 12, documentation only): the gated builds
`all` (x86) and `all -amd64`, "BUILD + GATES PASSED (release qemu)"
(`E:\xhci98-scratch\p45-w45-u4chain2\gb-x86-l.log`, `gb-x64-l.log`).
Packages (`make-package.ps1`): release x86 `xhcicm.sys` `cb3dfcb3...`,
`xhci98.sys` `ac0ab8c4...`; qemu x86 `xhcicm.sys` `0b771eaa...`,
`xhci98.sys` `fcc8934f...`. The installer and the tools of the transfer
folder are `3.3.0.0`'s, as in the 7 x86 and XP legs above.

The emulator, the host and the chain as in "45.15 (continued)": QEMU
v11.1.2 with `usb4-emu` main `fc16468`'s patch, `-Usb4 asm4242`
(`asm-preset=asm4242-desktop`, its pre-OS connection manager on), the
parent `usb4-device ... dialect=usb4,downstream-ports=1,
chain-power-controller=off` with its NVMe on USB4 port 1 at power-on, the
child `usb4-device ... parent=enc0,port=1` (a plain enclosure) plugged by
QMP at the desktop or, with `-Chain`, at power-on. Goldens: fresh overlays
of the q35 bases with the release package installed (98 SE and ME
`XHCIINST /dir release-x86 /yes /nopause /thunderbolt /stor98`; 2000
`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`), the qemu
flavour's `xhcicm.sys` and `xhci98.sys` copied over the installed ones,
shut down (98 SE `87699fc7...`, ME `d890cf1b...`, 2000 `cd49f1ba...`);
each leg a child overlay. Evidence: `E:\xhci98-vm\p45\w45-u4chain\w98\`,
`me\`, `w2k\` (`debugcon-<leg>.log`, `cmdline-<leg>.txt`, screenshots).

| Leg | System | Read | Verdict |
|---|---|---|---|
| r1 | 98 SE | the parent preserved at the start (`u4.preserved=00010808`); the child plugged: `u4.kid.connect`, `u4.kid.pcie.up=00010809`, UP, the action with the kid, and the 9x chain action re-enumerating both members (`cm.reenum.9x.reenumerate`); the chained NVMe found (Stor98 by its Add New Hardware wizard), F: beside the parent's E:, `fc /b` of a file on both: no differences. The child unplugged: `u4.kid.teardown`, `u4.linkdown.kid`, F: gone, E: kept; plugged again: F: back. The parent unplugged with the child attached: `u4.kid.dropped` first, then the port's link-down, both drives gone; the parent plugged again (the emulator keeps the child on it): the parent's tunnel up and UP, then the kid's, both drives back | clauses 1 (the child's hot plug behind a parent present at power-on), 2, 3 and the approve order: pass |
| r2 | 98 SE | the chain at power-on: adopted (`u4.kid.adopted=00010808`); the chained NVMe's first install asked a restart, taken (a restart ends QEMU on 98 SE; started again cold); then E: and F:; the child unplugged and plugged again: torn down, connected again, F: back | clause 4: pass |
| r1 | ME | as on 98 SE: the kid connected, Stor98's wizard for the chained NVMe (the staged INF chosen); F: appeared at the next plug of the child (the first plug installed it); `fc /b`: no differences; the end unplug tore the kid down and kept E:; the middle unplug dropped the kid first; the parent plugged again with the child: parent first, then the kid, E: and F: back | the child's hot plug behind a parent present at power-on (part of clause 1), 2, 3 and the approve order: pass |
| r2 | ME | adopted at power-on; the first install's restart taken, then E: and F:; the child unplugged and plugged again: connected again | clause 4: pass |
| r1 | 2000 | the kid connected and the chain port classified (`cm.chain.class=00060408`); the chained NVMe on Stor98 at **Code 10** (no interrupt behind a hot-plugged chain on the Standard PC HAL, as 45.13 read); `XHCIINST /devices` lists the kid as a router "behind another device, approved, PCIe up" (the status record's kind 3 entry); the end unplug: `u4.kid.teardown`, the chain port invalidated (`cm.chain.invalidate=02020408`); plugged again: UP, invalidated again; the middle unplug: `u4.kid.dropped` first, then the port's link-down, Windows' "Unsafe Removal of Device" for the parent's tree | the child's hot plug behind a parent present at power-on (part of clause 1), 2, 3: pass for the connection manager and the PCI side; the chained drive's start: the platform limit already recorded |
| r2 | 2000 | the chain at power-on: adopted; both NVMe on Stor98 **started**, each with its disk; the kid listed "behind another device" with no name (an adopted kid's name is not read) | the adoption (part of clause 4): pass; the child's unplug and re-plug after it not run |
| r2c | 2000 | the r2 overlay started again with the chain: adopted (`u4.kid.adopted=00010808`); the child unplugged: `u4.kid.teardown`, `u4.linkdown.kid`, the chain port invalidated (`cm.chain.invalidate=02020408`); plugged again: `u4.kid.connect`, `u4.kid.pcie.up=00010809`, UP, invalidated again (`01020408`); `XHCIINST /devices`: the kid "behind another device, approved, PCIe up", the chained NVMe back on Stor98 at Code 10 (the hot plug's platform limit of r1) | clause 4: pass for the connection manager and the PCI side |
| x3 | XP SP3 | the XP golden of "45.15 (continued)" (an earlier package) with this commit's qemu `xhcicm.sys` and `xhci98.sys` copied in and restarted; started with no dock; the parent and then its child plugged by QMP at the desktop: the parent's tunnel up and UP (`u4.pcie.up=00010809`), then the kid connected (`u4.kid.pcie.up=00010809`, UP), the tray's "QEMU USB4 NVMe Enclosure connected"; but on the PCI side nothing appeared behind the tunnel port (the member's `BusRelations` with no new child, `cm.chain.rel.new=00000000`; no chain port, `cm.chain.ports` 0, the chain action owed and expired; `XHCIINST /devices`: "nothing behind it now"); the cause is not established (on 7, r3, the dock's switch appeared at Code 31; here nothing appeared; a tunnel port numbered at power-on with no dock and no room for the switch is a hypothesis, not read) | clause 1: the connection manager's half (parent first, then the kid) pass; the PCI side failed, cause not established |
| r3 | 98 SE | started with no dock; the parent and its child plugged at the desktop: the parent UP, then the kid UP; the 9x numbering and re-enumeration ran on both members (`cm.reenum.9x.reenumerate`), the numbering outcome NONE (`cm.brnum.9x.outcome` 0: no function and no bridge seen behind either member), and no drive appeared | clause 1: the connection manager's half pass; the PCI side failed (nothing answering behind the member), cause not established |
| g4 | 10 x64 (22H2) | the parent preserved at the start (`u4.preserved=00010808`); the child plugged: `u4.kid.connect`, `u4.kid.pcie.up=00010809`, UP, the chain port classified (`cm.chain.class=00060408`), the chained NVMe on Windows' `stornvme`, started, with its disk; the end unplug: `u4.kid.teardown`, `u4.linkdown.kid`, the chain port invalidated (`cm.chain.invalidate=02020408`), the parent's NVMe kept; plugged again: UP, started; the middle unplug: `u4.kid.dropped` first, then the port's link-down; the parent plugged again at the desktop with the child on it: the parent's tunnel up and UP, then the kid's, both NVMe on `stornvme`, started (`XHCIINST /devices`) | clauses 2, 3 and the approve order: pass; the child's hot plug behind a parent present at power-on (part of clause 1): pass; the parent plugged at the desktop here followed a power-on with it present, so it is not the start with no dock of x3 and r3 |
| r2, r2b | 10 x64 (22H2) | the chain at power-on: adopted (`u4.kid.adopted=00010808`); `XHCIINST /devices`: both NVMe on `stornvme`, started, each with its disk, behind the tunnel port; the child unplugged: `u4.kid.teardown`, `u4.linkdown.kid`, the chain port invalidated, the parent's NVMe kept; plugged again: `u4.kid.connect`, `u4.kid.pcie.up=00010809`, UP, both NVMe started again | clause 4: pass |

The 10 x64 legs (g4, r2, r2b): a fresh overlay of the q35 base `w10x64` (`-smp 2`, 4 GB), the amd64 packages of `d73e4b10` (release `xhcicm.sys` `f9fcb812...`, `xhci98.sys` `4886609f...`; qemu `xhcicm.sys` `ae044ce4...`, `xhci98.sys` `cb0135d4...`, all as built, unsigned). The owner ran the in-guest signing step (`E:\xhci98-vm\p45\w45-u4chain\xf64\SETUP64.BAT`, elevated, in the overlay): a throwaway certificate made in that guest (MakeCert, valid 2026 to 2030, in Root and TrustedPublisher), every image and catalog signed with it (SignTool "Successfully signed"), then `INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`, exit 4 ("Changes were made ... restart Windows"); the signed qemu-flavour images were copied over the installed ones and Windows restarted. Test mode on. Evidence `E:\xhci98-vm\p45\w45-u4chain\w10\` (`debugcon-g4.log`, `serial-g4.log`, `debugcon-r2.log`, `debugcon-r2b.log`, `serial-r2b.log`, `cmdline-*.txt`, screenshots). The legs ran the package of `d73e4b10`; the later `a31e0b25` changes only a kid behind a Thunderbolt 3 parent and a link-controller descriptor read that fails, neither on these legs' path (a USB4 parent).

### Findings

- 98 SE, golden: during the install Windows asked for the "xhci98 xHCI
  Host Controller Disk" (`xhci98.sys` not found) for the second
  controller; pointed at `C:\XHCI98`, the install went on and ended with
  every controller installed. ME did not ask. Not chased here.
- On 98 SE and ME a chained drive is reached through 45.14's padding and
  9x re-enumeration of every member at each kid action (a kid's action
  names no member, record 34 section 6.11).

### Left

- Clause 1's PCI side with the parent hot-plugged at the desktop: on 7
  the dock's switch at Code 31 (r3 of "45.15 (continued)"), on XP and 98
  SE nothing behind the tunnel port (x3, r3); the cause is not
  established (bus room at the tunnel port numbered at power-on is a
  hypothesis; a read of the tunnel port's and the switch's configuration
  after the plug would settle it).
- Deferred to the next release (owner, 2026-10-30; record 34 section
  3.10, "Deferred"): the chained unit's eject teardown, the chain port's
  correspondence and the per-tunnel ownership, names and generations.

## 45.46 and 45.50: the ASM4242 router's own line, and every value written by the install, read on q35 (2026-10-30/31)

Branch `w45-small` (from `8185602f`), tree `E:\xhci98-wt\w45-small` on
development host A. Commits: `16f00df8` (45.46), `fa6ea0dc` (45.50),
`8bd4753a` (the free drivers page's address once, owner 2026-10-31),
and the small fixes `fd0ee2e7` (XHCITR's bitness on the x64 build),
`79449edc` (`xhcitr\snaptest.cmd`'s `find` and `findstr` by full path),
`eafe5ba0` (Stor98's `INTEGRATION-0.2.md`, no local path); each reviewed
and revised. The gated x86 build at `fa6ea0dc`
(`scripts\build-driver.cmd all`, `XHCI98_SELFTEST_SKIP_OK=1`, after
`src\pfw\build.cmd`): BUILD + GATES PASSED (release, qemu), 49:09; host
tests passed (`test_inst` 7028 checks, `test_cm` 244). The test package:
`make-release.ps1 -Arch x86 -TestLabel w45small -NoNonFree -NoWdmFree
-SkipQualtool -SkipMarkTool` of `fa6ea0dc` (its version still `3.3.0.0`,
so the update legs ran it with `/force`); `release-x86\xhci98.sys`
`0f095c57...`, `xhcicm.sys` `7b881743...`, `xhcicm.inf` `ef82578d...`,
`stor98w9.inf` `1825e442...`. Each leg on a fresh overlay of the q35 base
through `launch-q35.ps1 -Usb4 asm4242 -EncAtBoot` (the emulated ASM4242
with the ORICO enclosure at power-on), QMP 49380 to 49382; every start
and stop in `E:\xhci98-vm\p45\w45-small\pids.txt`.

### 45.46: the driver's choice for the router (`1B21:2425`)

| System | Before (`3.3.0.0`) | After the update | Evidence |
|---|---|---|---|
| 98 SE | `xhcicm.sys` (`NTMPDriver`, `Cm.Dev`); `DeviceDesc` `xHCI98 USB4 Router`, `FriendlyName` `xHCI98 USB4 Router (ASMedia ASM4242)` the installer's | `xhcicm.sys` (`Cm.Dev`); `DeviceDesc` and `DriverDesc` `xHCI98 USB4 Router (ASMedia ASM4242)`, no `FriendlyName`; the run printed "name removed: xHCI98 USB4 Router (ASMedia ASM4242)" | `w98u\exp-b33.log`, `exp-a50.log`, `upd-serial.log` |
| XP | `xhcicm`, `MatchingDeviceId` `pci\cc_0c0340`, the same names | `xhcicm`, `MatchingDeviceId` `pci\ven_1b21&dev_2425`, `DriverDesc` and `DeviceDesc` with the product, no `FriendlyName`; "name removed" printed | `xpu\xq-b33.log`, `xq-a50.log`, `upd-serial.log` |
| 10 x64 (22H2, 19045), the base alone, no package | the NHI has no driver: problem 28, "Universal Serial Bus (USB) Controller", no service (`wmic ... Win32_PnPEntity`); no INF in `C:\Windows\INF` names `CC_0C0340` or `DEV_2425` (`findstr /m /i`) | - | `w10a\s3.png`, `s5.png` |

So with the line the router matches by its own id,
`PCI\VEN_1B21&DEV_2425`, where it matched by the class: on the 98 SE
guest a compatible id (the NHI has subsystem `24211B21`, and 98 SE lists
`VEN&DEV` after the hardware ids `...&SUBSYS_...`, `...&CC_...`, but
ahead of `PCI\CC_0C0340`), and the driver chosen is the same: on 98 SE
and XP there is no other, and Windows 10 x64 has no driver of its own for
it, so nothing there for this line to outrank (record 23 section 5.13.9).

### 45.50: a user's values kept over an update from `3.3.0.0`

On each guest `3.3.0.0` was installed (`/thunderbolt /stor98`), the
machine restarted, values set by hand in the ASM4242 xHCI's and the
router's own keys (98 SE: `regedit /s` of a `.REG`; XP: `reg add`), and
the update run (`/force`), then a restart:

| Value | Set by hand | After the update, 98 SE | After the update, XP |
|---|---|---|---|
| `XhciPollIdleMs` (written by both versions) | 60 | 60 | 60 |
| `XhciUsb2Bos` (new) | 0 | 0; the other controller 1 | 0; the other controller 1 |
| `XhciResumeKeep`, `XhciLinkKeep` (new) | - | 1, 1 | 1, 1 |
| `XhciCmPollIdleMs` | 300 | 300 | 300 |
| `XhciCmUsb4V2` (new) | 0 | 0 | 0 |
| `XhciCmTunnelBridge` (new, `REG_SZ`) | `AUTO` | `AUTO` | `AUTO` |
| `XhciCmAsmediaPcie7Credits` (new) | - | 0 | 0 |
| `XhciCmRingInterrupt` (new, NT) | - | absent | 0 |
| `XhciCmBridgeReq` (new, NT 5.x) | - | absent | 1 |
| Stor98 `DriverParameter` (XP) | `storpollmode=1` | - | `storpollmode=1`, whole |
| Stor98 `StorPollMode` (98 SE) | 1 | 1 | - |

- On XP Stor98 ran the tunnelled drive under `3.3.0.0` and was updated
  over it; its `DriverParameter` stayed the user's string whole, as the
  owner's decision says (a missing key takes its default).
- On 98 SE Stor98 bound no drive in this guest (as in the w45-legs
  update legs of 2026-10-29), so neither the INF's
  `[Stor98_Settings_9x]` nor the installer's file-only update ran there:
  the 98 SE and ME half of Stor98's defaults (`s98_defaults_put_9x`, and
  `s98_defaults_drop_9x` at the uninstall) is read by the host vectors
  alone and owed to a leg where Stor98 binds on 98 SE or ME.
- The test package carried no `XHCIMARK.EXE` (`-SkipMarkTool`), so the
  XP run said the mark service was not installed: the test cut's, not a
  defect.

### Small fixes

- XHCITR's bitness line (`fd0ee2e7`): `-selftest-sysinfo` prints the
  three cases, which `snaptest.cmd` holds ("this x64 tool ran natively"
  for the x64 build); `xhcitr\build.cmd` built `XHCITR.EXE` and
  `XHCITR64.EXE` and the snapshot self-test passed (33 cases).
- The q35 launcher (`E:\xhci98-vm\q35-bases\launch-q35.ps1`, untracked):
  `-Usb4 tb3ar|tb3tr` on 98 SE and ME adds
  `-global <usb4-tb3|usb4-tr>-tunnel-dsp.power_controller_present=off`
  (45.14's finding; the pinned emulator has QEMU's property, not
  usb4-emu's newer `power-controller` alias); noted in its `STATE.md`.

## 45.51 the self-tests' skip across worktrees: one shared record per suite, the narrowed digests, the three-part proof (2026-10-31)

Task 45.51 of `roadmap-usb4.md`; design `build-and-test.md`, "The
self-tests' skip across worktrees" (reviewed and revised, brought up to
date with the code). Branch `w45-selftest-share`, code at `308379b9`:
the shared store (`5b6bbca8`, `c261826b`), the INF gate's digest
(`165ad584`), the suites' git policy (`29e56d67`, `30ac7d88`,
`400788ef`, `281531fe`), the behaviour cases on fixture repository P
(`ba67bf77`, `214f5c9d`), the stamp and the input rules on fixture tree
S (`4c7afe34`), the sign-only cases on P's HEAD and status (`7e18f01a`),
the packager's digest (`d001b39e`, `34204558`, `5fd1fb0f`, `308379b9`)
and each set's guard (`967de8f1`).

Each proof build is `scripts\build-driver.cmd all` (x86) with
`XHCI98_SELFTEST_SKIP_OK=1` (a pre-merge build), `src\pfw\build.cmd`
first, under one of the host's gated-build slots, on development host A;
the store is the default, `%LOCALAPPDATA%\xhci98\selftest-pass`, empty
before run (a). Times are the steps' own (`--- ...: passed (exit 0,
m:ss)`) and the build's start to end. Trees 2 to 5 are worktrees of the
same repository made for the proof (`tools\` linked), removed after it.

| Run | Tree | Commit | What changed from `308379b9` | Build | Packager self-tests | INF gate self-tests |
|---|---|---|---|---|---|---|
| (a) 1 | tree 1, the branch's own | `308379b9` | - | 46 min | 15:25, whole, record | 43:01, whole, record |
| (a) 2 | tree 2, a fresh worktree, its first build | `308379b9` | - | 5 min | 0:27, skipped on tree 1's record | 0:01, skipped on tree 1's record |
| (b) | tree 3, its first build | `f1088540` | a comment in `src\xhci_ring.c` | 5 min | 0:27, skipped | 0:01, skipped |
| (c) packager | tree 4, its first build | `2807d732` | a comment in `scripts\package\make-release.ps1` | 20 min | 16:51, whole, record | 0:01, skipped |
| (c) both | tree 5, its first build | `e7ab1cc6` | a comment in `scripts\inf-gate\check-uas-inf.ps1` | 41 min | 16:49, whole, record | 37:43, whole, record |

- Every build exit 0 (`BUILD + GATES PASSED (release qemu)`). The skip
  lines name the run they rest on, for example "INF gate self-tests
  skipped: nothing they or the gate read has changed since the run that
  passed at 2026-10-30T16:54:07Z (1622 checks; digest ce463514c36a0b9c; in
  E:\xhci98-wt\w45-selftest-share)".
- (a) The record a tree's first build writes is found by another tree's
  first build of the same commit: tree 1's packager record was written
  while the build's parallel steps ran (its end digest matched its
  start), and tree 2 skipped both suites. 45.48's per-tree records never
  allowed this, and its INF record never held even within a tree.
- (b) A driver source's content is in neither digest: a branch that
  changes only `src\xhci_ring.c` skips both. (A change to
  `src\cm\cm_vals.c` or `cm_vals.h` would not: the CM INF gate the upload
  cases run holds the INFs to them, so they are in the packager's set.)
- (c) A file one suite reads makes that suite run whole and records the
  new digest: `make-release.ps1` is the packager's alone (the INF gate's
  skip held); `check-uas-inf.ps1` is under `scripts\inf-gate\`, which
  both suites read.
- The packager's suite takes 15 to 17 minutes whole, against 20 to 27 in
  45.48's builds: its binary gates' case runs fixture P's host runner,
  not the real host suite.
- Before the proof, at `29e56d67` (the store, the INF digest, the git
  policy): the INF gate's self-tests passed whole (1619 checks), the
  packager's failed one check (the git policy's child reading, fixed in
  `30ac7d88`); on the steps 4 to 6 before their last review fixes: both
  passed whole (packager 1745 checks, INF gate 1622).
- Logs: `E:\xhci98-scratch\p45-w45-share\proof-*.log` and `pkg*.log`,
  `inf*.log`.

## 45.21 (code) Thunderbolt 1, 2 and 3 on Apple Macs: the emulator model, the connection manager's Thunderbolt-host profile and its legs (2026-10-30)

Record 22 section 19, with the owner's decisions of 2026-10-29 (Q1 all
three generations, Q2 the systems Apple supports first, Q3 no
DisplayPort, Q4 the emulator model first) and of 2026-10-30 (Q5 (a): the
profile active from this release; the release notes keep "Apple machines
are not claimed" until 45-E.11 reads one).

### The emulator model (`usb4-emu`, merged to its `main`: `da68171`)

- Profiles `tb3-apple-alpine-ridge`, `tb3-apple-titan-ridge`,
  `tb2-apple-falcon-ridge`, `tb1-apple-cactus-ridge`,
  `tb1-apple-light-ridge` (its `docs/TB-SWCM-HOSTS.md`): the Thunderbolt 3
  profile's Intel register set with no emulated ICM, firmware status 0,
  PCI subsystem `106B:<apple-subsys-id>` or `2222:1111`, a host router the
  guest configures, the PCIe Down adapters of Linux's `tb_find_pcie_down`,
  the link controller from generation 2 with its Downstream Port Reset,
  generation 1 with the Thunderbolt 1 reset packet instead, Light Ridge's
  TMU access bit and any-port PCIe Down; `usb4-device tb3-router-id` for
  generation 1 and 2 device routers. Inferred from Linux alone.
- Checks there: qtest `usb4-tbhost-test` 9/9, the other usb4 qtests
  unchanged, the fabric harness 50178 checks with no failure, the tb3,
  chain and AR conformance replays as before, `checkpatch` clean.
  Reviewed and revised.

### The code (xHCI98)

- `af42096a`: what record 22 section 19.9 lists - the selection by
  subsystem and firmware status (`tbt_hw.c`) and the host router run as a
  Thunderbolt router (`tbt_u4cm.c`: test (d), ports from the lane pairs,
  the PCIe Down map, no router operations, Plug Events, Light Ridge's TMU
  access, the identity by its recorded source, the link controller's marks
  and port reset, no USB3, two single lanes where a link does not bond).
- `d3e0114e`: 3.4.0.0 (45.20's USB4 version 2 code) merged in.
- `9913d7b0`: the Thunderbolt 1 hosts' INF lines (class form, NT 6.x
  sections only; CM-TB1 in the CM INF gate; the installer's names, its
  guard against installing over a PC's driver, and its class-form install
  calls).
- `cb510b62`: found by the 7 x86 leg below - a Thunderbolt 3 PCIe
  adapter's LTSSM field reads 3 with its tunnel up, not USB4's L0, and the
  tunnel was torn down waiting for L0; a Thunderbolt host's PCIe Down
  adapter is now live by Path Enable (as Linux treats a Thunderbolt
  router's), the fake fabric reporting 3 there too.

Host suites passed at each commit (`test_u4cm` 1648 checks at
`cb510b62`); the gated x86 build passed at `9913d7b0` and `cb510b62`.
Reviewed and revised.

### Legs (q35, `usb4-emu` `main` `230bed5`, built on development host A)

Machine: `launch-q35.ps1` `-Os w7x86` with the tb3ar switch topology
(gated root port 00:08.0, `usb4-tb3-usp`, NHI on DSP 0, tunnel port on
DSP 1) and `usb4-host profile=tb3-apple-alpine-ridge,apple-subsys-id=4660`
(NHI `8086:1577`, subsystem `1234:106B`), a Thunderbolt 3 enclosure with
the FAT NVMe disk on port 1 at power-on; kit `E:\xhci98-vm\p45\w45-mac`
(`leg.ps1`), a fresh overlay of the clean q35 base each time. Package: a
3.3.0.0 release cut of the branch as of `1a2a72ca` (the one 45.16's legs
used; `RINT.BAT`: `/thunderbolt /stor98`, exit 3, restart owed), then the
gated release `xhcicm.sys` copied over the installed one and Windows
restarted.

| Leg | `xhcicm.sys` (SHA-256) | Result |
|---|---|---|
| 7 x86 a | `aab0a80f...` (`9913d7b0`) | NHI "xHCI98 TB3 Controller (Intel Alpine Ridge)", Code 0, "USB4 software connection manager"; the enclosure enumerated, two lanes bonded; **PCIe failed** - the L0 wait above |
| 7 x86 b | `e519d25f...` (`cb510b62`) | PASS: PCIe up, "Stor98 Storage Controller for NVMe SSDs" started, D: ORICO mounted; hot unplug and re-plug: surprise down, up again, D: back |
| 10 x64 c | `644646f4...` (`cb510b62`, amd64; signed in the guest with a throwaway certificate) | PASS: NHI "xHCI98 TB3 Controller (Intel Alpine Ridge)", `xhcicm`, "USB4 software connection manager"; the enclosure two lanes bonded, PCIe up, the NVMe on Windows' `stornvme`, E: ORICO mounted |

The 10 x64 leg installed the 3.2.0.0 x64 release (project test
signature, valid at that guest's date) and then put the gated amd64
`xhcicm.sys` in its place: the loaded image could not be overwritten
("used by another process"), so it was renamed first (`xf10\SWAP64.BAT`).
3.2.0.0's own `xhcicm.sys` starts inert on an Apple subsystem, so the
tunnel shows the gated build ran.

Evidence: `w7\serial-a.log`, `serial-b.log` (`XHCIINST /devices`, the
PCI devices and volumes), `w7\trace-b.log` (the emulator's tunnel
transitions), `a1.png` to `a4.png`, `b1.png`; `w10\serial-c.log`
(`R.BAT`, `XHCIIN64 /devices`), `c1.png` to `c8.png`.

Not done here: the Falcon Ridge, Cactus Ridge and Light Ridge models'
legs (their code paths are the host vectors'), 98 SE to Vista on a
Thunderbolt host (none is Apple-supported; section 19.2), and the Mac
itself (45-E.11). The release notes keep Apple machines not claimed.

## 45.52 a UAS drive's pass-through and SMART (2026-10-31)

**Verdict: done here; a real SAT bridge is the bench's.** `xhciuas.sys`'s LUN PDO now serves SCSI and
ATA pass-through, buffered and direct, and the SMART requests `disk.sys` sends, through the engine
(design record 13 section 16). On q35 2000, XP, 7 x86 and 7 x64, with a UAS LUN answering ATA
PASS-THROUGH (the emulator's new test-only `x-sat`), every route reaches the drive and returns its data
and registers; on 7 x64 the 32-bit client (WOW64) and a native 64-bit one read the same lines. A free
SMART reader (smartctl 7.4, Windows 7) reads identity, health and attributes through both its SAT and
its ATA route. A Users member cannot open any disk on 2000, XP or 7 (error 5), so the owner's access
choice B rests on the same check as Windows' own ports. On 98 SE, ASPI lists the UAS LUN and passes
ATA PASS-THROUGH to it (R3), with no 9x code. A program's cancel and exit, and a surprise removal
under load, end every request with no hang (XP, 7 x86); Safely Remove is refused while a program holds
the disk and completes once it has ended (7 x64). `xhciuas.sys` loads on stock ME with its new
imports. Owed: a drive behind a real SAT bridge (bench).

Commits (branch `w45-pass`): `e948c36d` (design, merged as `75669ee7`), `44cf435b` (the owner's
answers, reading R1, a `legal-provenance.md` row), `f0ad326c`, `f4fe63a4`, `6738c96d` (the pure core
and `test_uas`, 594 checks), `571873d5` (the PDO, the engine, the imports), `230c47cd` (ATATEST's new
lines, the record aligned); on branch `w45-pass2`, `d84b3ce9` (ASPI9X and PTCANCEL) and `675e87e4`
(ATATEST64). Each reviewed and revised until no material finding was left.
Emulator: `usb4-emu` branch `w45-pass` `1050071` (fork commit `4e94c3f`, scsi-hd `x-sat`).

Vehicle: the gated x86 build of `230c47cd` (`out\gated-x86-release.txt`; `xhciuas.sys`
963fb55b66bc2d57..., `xhci98.sys` bdfc5bc5df89e56f...), staged by `make-package.ps1` into the 3.3.0.0
release layout's `release-x86` and installed with `INSTALL.BAT /console /yes /nopause`. `ATATEST.EXE`
of `230c47cd` (56f34d94d767d1ab...). QEMU of `usb4-emu` `4e94c3f` (17bbce24bff03c78...). q35 bases
`w2k`, `xp`, `w7x86`, `me` through `launch-q35.ps1 -Emu`, with `-device usb-uas` (port 3, one
`scsi-hd` LUN, `x-sat=on` unless said) and `-device usb-storage` (port 4), both blank 64 MiB. Evidence:
`E:\xhci98-vm\p45\w45-pass\<os>\`.

### R1 (static) and R2 (the access reading)

R1, the class drivers' routing, is in design record 13 section 16.11 and `legal-provenance.md`.
`disk.sys` on 2000 to 7 turns the SMART IOCTLs into `IOCTL_SCSI_MINIPORT` "SCSIDISK", with no
capability test.

R2: `net user atu ... /add` (a member of Users only), `runas /user:atu cmd`, then `ATATEST
\\.\PhysicalDrive0` to `3`. On every system each open fails with error 5, the IDE (`atapi`) or AHCI
disks, the Bulk-Only stick (`usbstor`) and the UAS LUN (`xhciuas`) alike (`r2-w2k.txt`, `r2-xp.txt`,
`r2-w7x86.txt`). As administrator the same opens succeed. So the access check that stands in front of
Windows' own ports is the disk's own security, and B, which adds no check in `xhciuas`, gives the
same answer.

### ATATEST, `x-sat=on`

The UAS LUN, on all three systems (`atatest-w2k-sat.txt`, `atatest-xp-sat.txt`,
`atatest-w7x86-sat.txt`):

| Line | Route | Result on the UAS LUN |
|---|---|---|
| P | `IOCTL_STORAGE_QUERY_PROPERTY` | max transfer 1 MiB, bus type USB |
| S1, S2 | SCSI pass-through, INQUIRY in and unspecified | GOOD, 36 bytes |
| A1, A2 | ATA PASS-THROUGH (16) IDENTIFY in a SCSI pass-through, in and unspecified (T_DIR) | GOOD, 512 bytes, model "QEMU HARDDISK" |
| A3 | ATA PASS-THROUGH (12) SMART READ DATA, unspecified | GOOD, 512 bytes, checksum ok, attributes 01, 05, 09 (raw 1234), 0C, C2 |
| A4 | ATA PASS-THROUGH (16) SMART RETURN STATUS, non-data, CK_COND | CHECK CONDITION, RECOVERED ERROR 00h/1Dh (the registers, as SAT returns them) |
| A5 | SCSI pass-through direct, IDENTIFY | GOOD, 512 bytes |
| A6 | `IOCTL_ATA_PASS_THROUGH_DIRECT` IDENTIFY | Status 50h, 512 bytes, model |
| M1 | `SMART_GET_VERSION` | 1.1, map 01, caps 5 |
| M2, M3 | `SMART_RCV_DRIVE_DATA` READ DATA, THRESHOLDS | 528 bytes, checksum ok |
| M4 | `SMART_SEND_DRIVE_COMMAND` RETURN STATUS | IDEREGS 4Fh/C2h, Status 40h: healthy |
| T1, T2, T3 | `IOCTL_ATA_PASS_THROUGH` IDENTIFY, CHECK POWER MODE, SMART READ DATA | Status 50h/40h, count FFh, the data |

Windows 2000 forwards `IOCTL_ATA_PASS_THROUGH` and its direct form too: R1 had left that open, since
2000's headers predate the code.

`driverquery` on XP and 7, and `dir` of `system32\drivers` on 2000, show `xhciuas.sys` of this build
(40,790 bytes, linked 01:56) running.

### ATATEST, `x-sat` off (all four systems)

XP (`atatest-xp-nosat.txt`); 2000 (`w2k\atatest-w2k-nosat.txt`) and 7 x86
(`w7x86\atatest-w7x86-nosat.txt`), the same overlays and `ATATEST.EXE` as the `x-sat=on` runs; 7 x64
(`w7x64\atatest-w7x64-nosat-wow64.txt`, `atatest-w7x64-nosat-native.txt`), the 32-bit `ATATEST.EXE`
under WOW64 and `ATATEST64.EXE`, their outputs identical. The UAS LUN's lines are the same on all four,
the refusal shape a drive with no SAT gives, as designed (16.8, 16.9):
- the SCSI forms return CHECK CONDITION 5/20/00 to the program, the call succeeding;
- the ATA forms return Error 04h, Status 51h;
- `SMART_GET_VERSION` is answered, and RCV and SEND fail with error 50 (`STATUS_NOT_SUPPORTED`).
- the INQUIRY lines (S1, S2) and `IOCTL_STORAGE_QUERY_PROPERTY` are served as with `x-sat=on`.

The Bulk-Only stick under each system's own `usbstor.sys`, beside it, is unchanged from 45.27: error 87
for an unspecified direction, error 50 for the ATA and SMART IOCTLs.

### A SMART reader (Windows 7 x86, `smartctl-w7x86.txt`)

smartctl 7.4 (`smartmontools-7.4-1.win32-setup.exe`, SHA-256 76880e73...; not tracked, run in the
guest only; on XP it does not start: `GetTickCount64` is missing). On `/dev/pd3`, the UAS LUN:
- `-d sat` (SCSI pass-through) and `-d ata` (the SMART IOCTLs) both read the identity (QEMU HARDDISK,
  QM00001, ATA8-ACS);
- both report "SMART support is: Enabled" and "overall-health self-assessment test result: PASSED";
- both list the capabilities and the five attributes with their thresholds.

The error and self-test log reads fail, because the test answer serves no READ LOG. Without `-d`,
smartctl stops at "Unable to read USB device ID", as for any USB disk it cannot identify.

### Windows ME: the new imports load (`me\me-uas-mounted.png`)

Stock ME (no USB 2.0 stack), the package installed by `INSTALL /console /yes /nopause` (exit 3), two
cold starts. The UAS LUN, here an overlay of the FAT test image, mounted as E: (ORICO,
`TESTDATA.BIN`); the Bulk-Only stick is blank. So `xhciuas.sys` loads on ME with `_except_handler3`,
`MmProbeAndLockPages` and the cancel imports. On 98 SE the same build
loads and mounts the UAS LUN (R3 below).

### 7 x64: WOW64 and a native caller (`w7x64\`)

Vehicle: the amd64 gated build of `d84b3ce9` (`gated64.log`; `src/uas` as at `230c47cd`;
`release-x64` `xhciuas.sys` 8c4352a6de474e17..., `xhci98.sys` 7b41266ddcb8d0a6...), staged by
`make-package.ps1 -Arch amd64` without the key, so unsigned and with no catalogs. The installer refuses
such a package ("REFUSED: this 64-bit package carries the Thunderbolt driver without its catalog,
xhcicm.cat"), as designed. The five catalogs were made on the host by the packager's own
`New-PackageCatalogFile` (WDK 7.1 MakeCat) and left unsigned. In the guest's overlay (test signing
on), a throwaway certificate made there with WDK 7.1 MakeCert (`CN=w45throwaway`, valid 2026 to 2030,
the guest's clock being 2026-09-20) was put in Root and TrustedPublisher, and the four `.sys` and five
`.cat` files were signed with it there (`sign-w7x64.txt`, `serial-c.log`). Nothing was signed on the
host and the project's key was not used. `INSTALL.BAT /console /yes /nopause`: exit 3, "xHCI98 USB
Attached SCSI Storage: xhciuas 3.3.0.0, started"; after the restart `driverquery` lists `xhci98` and
`xhciuas` running (`driverquery-w7x64.txt`). q35 `w7x64`, `-cpu qemu64 -smp 2 -m 2048`, the same UAS
and Bulk-Only devices, `x-sat=on`.

`ATATEST.EXE` (32-bit, of `675e87e4`, 25a35b90e6e3650c...) and `ATATEST64.EXE` (`675e87e4`,
e398a3680ab21400...) on `\\.\PhysicalDrive0` to `3` (`atatest-w7x64-wow64.txt`,
`atatest-w7x64-native.txt`). The two outputs are identical, line for line:
- the UAS LUN (drive 3): every line as on the 32-bit systems above, S1 to T3, through the `_32`
  layouts under WOW64 and the natural ones natively, `IOCTL_ATA_PASS_THROUGH_DIRECT` (A6) included;
  M4 reads 4Fh/C2h, Status 40h, healthy;
- the Bulk-Only stick under 7's own `usbstor.sys` (drive 2): error 87 for an unspecified direction,
  5/20/00 for the ATA CDBs, error 50 for the ATA and SMART IOCTLs;
- the two AHCI disks (drives 0 and 1, Windows' own port): the ATA PASS-THROUGH CDBs fail with error 1,
  the ATA and SMART IOCTLs succeed. Read only as a reference.

### R3: 98 SE's ASPI (`w98se\aspi9x-98se.txt`)

98 SE with NUSB 3.3's mass-storage stack and the x86 package of `230c47cd`, one vCPU, cold starts.
`ASPI9X.EXE` is this branch's 98 SE and ME ASPI probe, built from `d84b3ce9`'s source before its last
review fix, which does not touch these lines. The UAS LUN, an overlay of the FAT test image, mounted
as E:.
- ASPI lists two host adapters, both "USBMPHLP": adapter 0 is the Bulk-Only stick, adapter 1 the UAS
  LUN.
- The UAS LUN: INQUIRY completes ("QEMU HARDDISK"); ATA PASS-THROUGH (16) IDENTIFY completes with the
  model; ATA PASS-THROUGH (12) SMART READ DATA completes, checksum ok; RETURN STATUS (CK_COND) ends in
  CHECK CONDITION, RECOVERED ERROR 00h/1Dh, with the registers 4Fh/C2h, Status 40h in the descriptor
  sense, as SAT returns them.
- The Bulk-Only stick, which has no SAT: the ATA CDBs end in CHECK CONDITION 5/20/00.

So the 9x route that section 16 left as a hypothesis exists: ASPI passes any CDB through NUSB's
mapping layer to `UasPdoScsi`, and T_DIR resolves the direction. No 9x code was needed.

### Cancel and removal (`xp\`, `w7x86\`)

`PTCANCEL.EXE` is this branch's cancel and removal client, built from `d84b3ce9`'s source before its
last review fix. It sends N overlapped `IOCTL_SCSI_PASS_THROUGH` READ(10)s of 64 KiB to the UAS LUN,
then cancels them (`CancelIo`), exits with them outstanding, or waits. The LUN's reads were throttled
to 64 KiB/s (`throttling.bps-read=65536`) so that requests queue in the driver.

| Run | XP (`ptcancel-xp.txt`) | 7 x86 (`ptcancel-w7x86.txt`) |
|---|---|---|
| 16 reads, then `CancelIo` | 15 of 16 pending; all 16 done (all were started, one per stream) | - |
| 64 reads, then `CancelIo` | 63 of 64 pending; 16 done (in flight), 48 cancelled (error 995) | the same |
| 64 reads, the program exits with them pending, then `ATATEST` | the exit completes; every `ATATEST` line as before | the same |
| 64 reads, then a surprise removal (QMP `device_del` of the UAS device) | 24 done, 15 error 1117, 25 error 2; no hang | the same |

So a queued request is cancelled, one already started on the device ends normally, a closing process
leaves nothing behind, and a removal ends every request. Unthrottled (XP, 32 reads,
`ptcancel-xp-unthrottled.txt`), all 32 completed before the cancel reached one.

**Safely Remove during a loop (7 x64, `w7x64\saferemove-w7x64.txt`, screenshots `d6.png` to
`d19.png`).** The same overlay as above, relaunched with the throttle; `PTCANCEL.EXE` of `d84b3ce9`
(524daceeb65f7679...), `\\.\PhysicalDrive3 64 wait`, while Safely Remove Hardware (`hotplug.dll`,
"Display device components") stopped "xHCI98 USB Attached SCSI Storage":
- Windows offers no Stop of the UAS drive alone: its confirmation lists the whole controller ("xHCI98
  USB 3 Controller", the root hub, both drives and the Bulk-Only stick's device). Stopping the
  Bulk-Only stick lists the stick alone. A finding outside this task, handed over in the report.
- With the program's handle open and 63 of 64 reads pending, the stop was refused: "Problem Ejecting
  xHCI98 USB 3 Controller ... This device is currently in use". The requests ran on: all 64 done,
  none failed (GOOD, 64 KiB each).
- Once the program had ended, the same stop completed: the list emptied, and `sc query` read
  `xhciuas` and `xhci98` STOPPED (unloaded). No hang at either step.

### Owed

- A drive behind a real SAT bridge, and a SMART reading of a real drive: the bench list.

## 45.54 the installer's Advanced lists offer every user setting: the code and the legs on 98 SE, XP and 7 (2026-10-31)

Branch `w45-adv` (from `a2f703f2`), tree `E:\xhci98-wt\w45-adv` on
development host A. Commits: `50d9c2a9` (record 26 draft 52, record 23
revision 51: the design), `b7b570c1` (the code and the host vectors),
`56a3cd0e` (the console's 98 SE and ME directory refusal shaped to the
width); each reviewed and revised.

### The cause of the one row

On 98 SE and ME a controller counted for the settings only once it ran
`xhci98` (`value_target`, record 23 section 8.2, from 3.0.0.0, when the
console asked its questions after the restart). On a first install no
controller is bound yet, so `inst_eng_rows` dropped every USB row that
was not greyed, and only the greyed `XhciPollFineTimer` was left. A
controller the run installs now counts there too, as on NT
(`inst_value_target_9x`), so the 98 SE and ME console also asks
`XhciSilentInstall` and a new host interface's three Thunderbolt
questions on a first install. A 98 SE or ME run cancelled before its
values are written skips them, as the NT run's stop; a cancel recorded
after that stage keeps what was already written.

### The gated build and the host tests

The first gated x86 build, of `b7b570c1`, stopped at the console tool's
self-test: "every screen within 79 columns" failed, "screens, ordinary:
1 lines needed the console's wrap". `XHCIINST.EXE /selftest` with
`XHCIINST_SCREENS` set, run in an XP guest (an instrumented build marked
the wrap), showed the line: the 98 SE and ME directory refusal ("Not
that one: Windows 98 SE and ME: each folder name 8 characters at most
(and a 3-character extension), ..."). Since the 98 pass now asks the
three Thunderbolt questions, the self-test's script puts its long-named
directory on the directory question, as the script was written to do,
and that reason is longer than a line. `56a3cd0e` shapes the refusal
with `inst_wrap_text`, as a value question is shaped; the screen printed
is the same, and the guest run gave "11 checks, 0 failures".

The gated x86 build at `56a3cd0e` (`scripts\build-driver.cmd all`,
`XHCI98_SELFTEST_SKIP_OK=1`, after `src\pfw\build.cmd`): BUILD + GATES
PASSED (release, qemu), 20:07. Host tests passed (`test_inst` 7409
checks). The test package: `make-release.ps1 -Arch x86 -TestLabel
w45adv -NoNonFree -NoWdmFree -SkipQualtool -SkipMarkTool` of `56a3cd0e`
(its version still `3.3.0.0`); `XHCIWIZ.EXE` `28def4d0...`,
`XHCIINST.EXE` `ea1e2736...`, `xhci98.sys` `a8593f4f...`, `xhcicm.sys`
`a9162d04...`.

### The legs

Each on a fresh overlay of the q35 base through `launch-q35.ps1 -Usb4
asm4242 -EncAtBoot` (the emulated ASM4242 with the ORICO enclosure at
power-on), a first install of the test package, QMP 49450 to 49454;
every start and stop in `E:\xhci98-vm\p45\w45-adv\pids.txt`, the
screens in each leg's folder.

| System | What was done | Result | Evidence |
|---|---|---|---|
| 98 SE, the window | SETUP.EXE, Thunderbolt 3 / USB4 ticked, Settings | The USB tab shows `XhciSilentInstall` (it was missing on a first install); its Advanced lists 17 rows in the owner's order, `XhciInterruptMode` first, `XhciPollFineTimer` and `XhciWideMap` greyed "(Windows 2000 to 7 only)"; the Thunderbolt tab's Advanced starts with `XhciCmRingInterrupt`, greyed "(Windows 2000 to 10 only)", and lists `XhciCmUsb4V2` | `w98w\b1-settings-usb.png`, `b2-adv-usb-top.png`, `b3-adv-usb-end.png`, `c2-adv-tb-top.png`, `c3-adv-tb-end.png` |
| 98 SE, the window | `XhciImodInterval250ns` 200, `XhciLinkKeep` 0, `XhciMaxTransferKB` 130, OK | 130 refused: "64 to 2048, a multiple of 4."; the two others "(to be written)", on the Summary as "The settings written machine-wide", and after the install `Services\USB` holds `XhciLinkKeep`=0 and `XhciImodInterval250ns`=0xc8 alone (the drivers' keys keep the INF defaults 1 and 0xa0) | `b9.png`, `b12-adv-usb-reopen.png`, `d7.png`, `d9.png`, `serial-a.log` |
| 98 SE, the console | `INSTALL /console`, Y to Thunderbolt, Enter elsewhere | `XhciSilentInstall` asked ("now: new install, the default 0" for each controller), then `XhciCmMode`, `XhciCmApprove` and `XhciCmSafeEject` with the new-install note; answered 1: `Services\USB` holds `XhciSilentInstall`=1 and the `SilentClasses` list; exit 3 | `w98c\t3.txt`, `t4.txt`, `t6.txt`, `serial-a.log` |
| XP, the window | `XhciWideMap` 1 (USB), `XhciCmRingInterrupt` 1 (Thunderbolt) | Both rows live on XP; after the install and a restart `Services\USB` holds `XhciWideMap`=1 and `XhciCmRingInterrupt`=1; `xhci98`, `xhcicm`, `xhcipfw` loaded | `xpw2\b3-widemap.png`, `c2-ring.png`, `d13.png`, `serial-a.log` |
| XP, a run cancelled | the first XP overlay's run, cancelled at the Progress page after the package was staged | no `Services\USB` key: no value written (the cancel came before the values' stage) | `xpw\d4.png`, `serial-a.log` |
| 7 x86, the window | `XhciInterruptMode` 2 (the row offers 0 to 3), `XhciCmRingInterrupt` 1 | after the install and a restart `Services\USB` holds `XhciInterruptMode`=2 and `XhciCmRingInterrupt`=1; `xhci98`, `xhcicm`, `xhcipfw` loaded; the tunnelled NVMe drive mounted during the install | `w7w\b1-adv-usb-top.png`, `c1-adv-tb-top.png`, `d9.png`, `serial-a.log` |

- 98 SE was not restarted after its installs (98 SE's known shutdown hang
  with `xhcicm` installed, `q35-bases\STATE.md`); the legs read the
  installer, and the values it writes are read from the registry.
- In the Advanced dialog, a refused number keeps its row selected while
  the list's grey highlight can move to another row when a key is
  pressed in the list before the box is corrected. The value written is
  the one the box shows for its own row. Seen on 98 SE with
  `XhciMaxTransferKB`. Not changed here.

## 45.56 Gen 4 Link Recovery on USB4 version 2 links (2026-10-31)

Design record 22 section 6.2.3 item 4 rewritten and section 6.2.5 added
(`506d2af3`), then coded (`e2c38d30`, branch `w45-gen4`), each reviewed
and revised. 45.57 was dropped by the owner the same day (`4feac7f8`);
nothing of it was done here.

### What the code does

- Enable Gen 4 Link Recovery (PORT_CS_19 bit 31) set on both ends of a
  depth-1 Gen 4 link, symmetric or asymmetric, between two USB4 version 2
  routers, at the idle tick once the port is UP or FAILED: device end
  first, each written as read with bits 30 and 24 0 and read back, only
  with both lane 0 adapters in CL0, the host's at Gen 4, no asymmetric
  transition in progress (PORT_CS_18 bit 24) and StartAsymmetricFlow 0
  at both ends. A router that keeps it 0 refuses it for that connection,
  both ends written 0 again. Never on a HELD port, a port with a
  preserved DisplayPort tunnel, or below depth 1.
- `XhciCmGen4LinkRecovery`, written at 1 by both NHI INFs: 0 never sets
  it; `XhciCmUsb4V2` 0 never sets it either.
- Each LINK_RECOVERY (and a lost notification on a port with the bit set)
  owes the port's recovery check after its port check: the link logged;
  each tunnel's six parts read at its recorded lane HopIDs (a PCIe
  tunnel not whole goes as a lost one, a USB3 tunnel not whole is torn
  down alone); the USB3 Port Link State watched up to 2 s for U0, U2 or
  U3 and recorded, the tunnel kept, if it does not get there; the TMU
  logged; a request unanswered retried for 2 s, then the check given up
  with nothing torn down; a LINK_RECOVERY drained during the check keeps
  it owed.
- The bit cleared again: the host's end at an unplug's teardown (retried
  at the EMPTY tick, never deferring a new connection); both ends before
  a stop or a suspend, a HELD port's host end alone; set again by the
  tick after a resume or a recovery's commit; an owned bit on a port no
  longer admitted cleared at the tick. No write of PORT_CS_19 while
  StartAsymmetricFlow reads 1, the adoption's clear of a predecessor's
  bit included (which then holds the port).

Host vectors: `test_u4v2` 510 checks (the `test_elr_*` vectors; the fake
fabric gained ADP_USB3_GX_CS_4's USB3 Link Valid and Port Link State and
a router that keeps the bit 0), `test_cm` 252, `test_u4cm` 1889
unchanged; the whole host run passed. The gated builds of `e2c38d30` in
this worktree passed: x86 "BUILD + GATES PASSED (release qemu)" at
11:06, amd64 at 11:41 (development host A).

### The legs (2026-10-31)

`usb4-emu` main `230bed5` (QEMU fork `674ff08`, version 2 router model
`cb36497`; the snapshot task 45.20 used, unchanged: its
`x-link-event=error` recovers a Gen 4 link whose two ends have the bit
set, sending LINK_RECOVERY with the configuration kept, and disconnects
it otherwise, docs/USB4-V2.md). q35, `usb4-host host-model=minimal
usb4-version=2` in the `adl` layout with `usb4-device dialect=usb4
usb4-version=2` and the NVMe test disk at power-on; the version 1 leg on
the `asm4242` model with the ORICO enclosure at power-on (pre-OS
tunnel). The release package of `e2c38d30` installed (`INSTALL.BAT
/console /yes /nopause /thunderbolt /stor98`, the installer built from
the same tree), then its `qemu` `xhcicm.sys` copied over the installed
one and the guest restarted, so the connection manager's log reads on
the debug console: x86 release `xhcicm.sys` `780f1708...`, qemu
`358aabda...`; amd64 staged unsigned with unsigned catalogs (release
`ea73e693...`, qemu `9c3df75c...`), signed in the guest with a
throwaway certificate made there, nothing signed on the host. Fresh
overlays; evidence under `E:\xhci98-vm\p45\w45-gen4\<leg>\`
(`steps.txt`, `debugcon-a.log`, `trace-a.log`, screenshots).

| System | Configuration | Result |
|---|---|---|
| 7 x86 (`w7`) | v2 host and device at power-on | `cm.usb4.elr=00000101` (the value as written by the INF), Gen 4 x2, PCIe and USB3 tunnels, `u4.elr.set=00000001`; the emulator reads `elr=1/1` |
| 7 x86 | `x-link-event=error` | the emulator's recovery ("uncorrectable error: recovered"), LINK_RECOVERY, the recovery check: `u4.elr.link=00010202`, `u4.elr.usb3=00010100` (Link Valid, U0), `u4.elr.tmu=00000101`, `u4.elr.ok`; both tunnels kept, no teardown; D: ORICO listed and TESTDATA.BIN copied after it |
| 7 x86 | unplug, plug again | the trace at the plug reads `ELR dfp 0 ufp 0` (the host's end cleared at the teardown; task 45.20's run read `dfp 1` there), then `u4.elr.set` again |
| 7 x86 | `XhciCmGen4LinkRecovery` 0 (machine-wide value), restart, `error` | `cm.usb4.elr=00000100`, no set (`elr=0/0`); the error a disconnect: teardown, the router back, tunnels rebuilt |
| XP (`wxp`) | v2 host and device at power-on; `error`; unplug and plug `asym-tx`; `error` | `u4.elr.set`; both errors recovered with `u4.elr.ok` (`u4.elr.link=00010204` on the asymmetric link), tunnels kept; `ELR dfp 0 ufp 0` at the re-plug; the emulator `width=0x4/0x8 elr=1/1 recoveries=1`. The volume not read: 45.20's XP reading (no NVMe volume on this layout) stands |
| 10 x64 (`w10`) | v2 host and device at power-on; `error`; `asym-tx` transition then `error`; unplug and plug; the value 0 and restart, `error` | as on 7 x86: `u4.elr.set`, two recoveries `u4.elr.ok` (`recoveries=2`), E: ORICO listed after them, `dfp 0` at the re-plug, and with the value 0 the error a disconnect |
| 7 x86 (`w7r`) | the version 1 regression: `asm4242`, ORICO at power-on | the tunnel preserved (`u4.preserved=00010808`), no `u4.elr` line, D: ORICO; hot unplug and plug rebuilt, D: back, still no `u4.elr` line |

### What is left

The bench reading on a real version 2 router with task 45-E.10 (the
routers' recovery itself, what a real tunnelled PCIe and USB3 link reads
right after one). 45.56 is otherwise done here.

## 45.53 and 45.55: a tunnel port with no power controller, and the legs on the emulator's new default (2026-10-31, partial)

Worker w45-pcie, development host A. Code at `a2f703f2` (no driver
change in these tasks): the gated build `all` (x86), "BUILD + GATES
PASSED (release qemu)" (`E:\xhci98-scratch\p45-w45-pcie\build-all.log`),
and `all -amd64`, passed (`build-amd64-all.log`). Packages
(`make-package.ps1`): release x86 `xhci98.sys` `bdc4b851...`,
`xhcicm.sys` `fd8e3958...`, `stor98.sys` `202d6249...`; qemu x86
`xhcicm.sys` `867c9b83...`, `xhci98.sys` `d7b77130...`. The installer,
`xhcimark` and `xhcitr` built from this tree (`XHCIINST.EXE`
`41f89e51...`, `XHCIMARK.EXE` `8248088b...`) in `3.3.0.0`'s package
layout. Evidence: `E:\xhci98-vm\p45\w45-pcie\` (one folder a leg:
`cmdline-<tag>.txt`, `steps.txt`, `debugcon-<tag>.log`, `serial-<tag>.log`,
`infopci-*.txt`, screenshots; hashes in `hashes-xf3.txt`, the goldens in
`goldens-sha.txt`).

### The cause

**45.53 (XP, `adl`).** With the emulator's default of the time the
`adl` tunnel root port (`usb4-tunnel-port`) reported a Power Controller,
as QEMU's `pcie-root-port` does: an empty slot came out of reset powered
off (Slot Control PCC set) and the enclosure the tunnel brought up read
all ones until the guest powered the slot, which 98 SE, ME, 2000 and XP
never do. Read on XP (r1, the default): the tunnel up in the connection
manager, the root port's PCC off and the endpoint all ones; r2, the same
with `-global usb4-tunnel-port.power_controller_present=off`: the chain
enumerated and the ORICO drive mounted. The captured Alder Lake-P
Thunderbolt 4 root ports have none ("PwrCtrl-",
`tests/captures/framework13-12th/linux-orico`, `pci/tb-complex`), so the
emulator was wrong, not the driver.

**45.55 (`asm4242-desktop`).** With the emulator's default of the time the
ASM4242 switch's tunnel ports had neither a power controller of the
board's kind nor its padding: SeaBIOS left each tunnel port one bus and
its minimum windows, so a device router plugged later had no bus numbers
for its own switch (XP r3: the parent's switch upstream port at bus 3
with secondary bus 0). The board's firmware gives each tunnel port 48
buses, 12 KB I/O and 384 MB (ASM4242-USB4.md 11.1, USB4-HOSTS.md 5.2 in
`usb4-emu`).

### The emulator's new default (owner's option (a), 2026-10-31)

In `usb4-emu` (branch `w45-pcie`, merged with main `ede6308`; QEMU fork
`5c91aaa`, patch exported in `src/patches`):

| Commit | What |
|---|---|
| `c83a0ea` (fork `d6c8136`) | `usb4-tunnel-port` and `usb4-asm-tunnel-dsp` with no power controller by default; `usb4-asm-usp` pads its tunnel ports by default (`fw-bus-pad=48,fw-io-pad=12K,fw-mem-pad=384M`, a pad that does not fit shrinking) |
| `a77e7fa` (fork `ffcaa8c`, `b0d1ea6`) | reviewed and revised: a Slot Control write cannot power off a slot with no power controller at any offset (QEMU's slot code reads PCC from the value written); the ASM4242 pads act on SeaBIOS's numbering only |
| `5c140b5` (fork `34c6a5e`, `55cadf9`) | `usb4-tb3-tunnel-dsp` (Alpine Ridge: the P14s's tunnel port 04:01.0 reports "PwrCtrl-") and, by analogy, `usb4-tr-tunnel-dsp` (Titan Ridge is not captured) with no power controller by default; `power-controller` is on, off or auto, auto following QEMU's `power_controller_present`, so either spelling gives the controller back |
| `048a9e5`, `1c3a050` | the docs reviewed and revised (what removes an endpoint behind a port with no power controller; the padding no longer opt-in); the merge of main |

The launchers' explicit `-global ...power_controller_present=off` (45.14,
w45-small's `launch-q35.ps1` change) is left: it gives the same slot. Every
`usb4-emu` qtest suite on `5c91aaa`: host 43, tunnel 31, icm 40, icm-tr
16, icm-icl 5, harness 25, fault 9, asm4242 16, asm2464 4, tb3dev 12,
tbhost 9, root-hotplug 22, usb3 9, retrain 5, xdomain 6 (252 ok, none
failed).

**Earlier legs this default can change.** Read with a power controller on
the tunnel port; on the new default a guest's slot power-off removes
nothing, and on the ASM4242 layout the bus numbers below the switch
move (a 98 SE instance that names `BUS_xx` there is found anew):
- every `adl` leg on NT (45.20's version 2 legs, 45.49's `adl` reading):
  the endpoint now answers with no slot power-on from the guest;
- every `asm4242` leg (45.12 R1/R5, 45.15, 45.18, 45.22, 45.31, 45.35,
  45.37, 45.46/45.50): the tunnel ports had no power controller already
  where the leg asked for it; the padding is new;
- the Thunderbolt 3 legs on 2000, XP, Vista, 7 and 10 (`-Usb4 tb3ar` /
  `tb3tr`, which passed no global on NT): 45.13, 45.17, 45.21 (the Apple
  models), 45.22, 45.37, 45.47, 45.49; and `usb4-emu`'s GUEST-EXITS
  reading of Windows 7's Safely Remove (the endpoint was removed by the
  slot power-off; now it stays until the tunnel goes down or the router is
  unplugged). The 98 SE and ME Thunderbolt 3 legs already ran with
  `power_controller_present=off`.

### The legs on the new default

Emulator: the fork `55cadf9` build (`bin-55cadf9`, `qemu-system-x86_64.exe`
`93b47a05...`; the merge with main changes no tunnel-port code). Goldens
(fresh overlays of the q35 bases, this tree's release package installed,
the qemu flavour's `xhcicm.sys` and `xhci98.sys` copied over): XP and 98 SE
installed with the `asm4242` layout present, 2000 with no USB4 hardware
(`-NoXhci`), 7 x86 with `adl` present. Leg A (45.53): `-Usb4 adl`, the
ORICO enclosure plugged by QMP at the desktop. Leg B (45.55):
`-Usb4 asm4242`, the parent `usb4-device ... downstream-ports=1,
chain-power-controller=off` and its child plugged by QMP at the desktop
two seconds apart.

| Leg | System | Read | Verdict |
|---|---|---|---|
| A | 98 SE | the enclosure plugged: the switch's bridges by the Add New Hardware wizard (a restart offered, declined), then Stor98's wizard for the NVMe; Orico (E:) in My Computer (`w9A\b-vol-E.png`) | pass |
| A | XP | first plug with the tunnel root ports on Windows' own bridge driver (they appeared after the install, through Found New Hardware): nothing enumerated behind 00:05.0 (the switch's bus registers 0, `xpA\infopci-1.txt`); the installer run again with the layout present (the root ports then "xHCI98 PCI Bridge"), restarted: the plug gave Found New Hardware for "Stor98 Storage Controller for NVMe SSDs", D: ORICO, `fc /b` of a copied file no differences | pass once the root ports carry the package's bridge driver |
| A | 2000 | (fresh overlay `w2C`, installer run with the layout present, cold start) the plug: the switch numbered, the NVMe enumerated (`cm.chain.id` for it at 02:00, depth 3) as "PCI Device", the wizard found no driver (code 18, `w2C\b-wizard-no-driver.png`); `INSTALL.BAT` run again put Stor98 on it: E: ORICO | the PCI side passes; the driver match on a desktop plug does not (owed) |
| A | 7 x86 | the plug: Stor98 started, D: ORICO | pass (control) |
| B | XP | the tunnel port 48 buses (3-50) and its windows (new default), but the parent's switch upstream port `104C:8232` at Code 12: the tunnel port has no I/O window (the launcher's root port reserves no I/O, so the switch's I/O pad shrinks to none) | fails |
| B | XP, root ports `io-reserve=24K` (as the board) | the parent's switch and its downstream ports numbered (`cm.breq` for 03:00, 04:00, 04:08); the parent's NVMe enumerated as "PCI Device" (code 18, no `PCI\XHCI98_TUNNEL` id, so the wizard finds no driver); the chain port `104C:8233` at Code 12, the child not reached | fails (driver side, owed) |
| B | 98 SE | both tunnels up in the model (the kid's too), but no connection-manager line in the debug log even with the qemu flavour copied in; the parent's switch left unnumbered | not read (the golden's NHI driver to check) |
| B | 2000 | the transfer disk not visible to 2000 on this layout after its first-boot wizards; not run | owed |
| B | 7 x86 | (golden installed with `adl`; the ASM4242 switch's ports first on Windows' own drivers: both NVMe "PCI Device" code 28; the installer run again with the layout present, restarted) the plug: the kid UP (`u4.kid.pcie.up=00010809`), both NVMe on Stor98, started; D: ORICO (the second drive, a copy of the same disk image, shows no letter) | pass (control) |
| A, B | ME, XP x64, Vista x86 | not run in this session | owed |

10 x64 (w45-u4chain2, `E:\xhci98-vm\p45\w45-u4chain\STATE.md`): a USB4
parent plugged again at the desktop with its child attached, parent UP
then the kid, both NVMe started.

### Findings

- 45.53's cause is the emulator's, fixed by its new default: the `adl`
  endpoint answers with no slot power-on. For 45.55 the default asks the
  board's padding (48 buses, 12 KB I/O, 384 MB a tunnel port), which
  shrinks to what the launcher's root port reserves (`bus-reserve=100,
  mem-reserve=64M`, no I/O): tunnel port 0 got buses 3-50, 30 MB of memory
  and no I/O window (`xpB\infopci-1.txt`); with `io-reserve=24K` it got an
  I/O window and still 30 MB (`xpB2\infopci-1.txt`). These legs do not give
  the board's resources, so whether XP's remaining Code 12 is the
  launcher's limit or this driver's is not settled.
- A tunnel port that appears after the install (here: the layout
  changed under an installed system) gets Windows' own bridge driver: on
  XP a hot-added switch behind it is never enumerated, on 7 the drives
  behind it get no tunnel id and no driver; with the package's bridge
  driver on it (the installer run again) the plug works. On a real Alder Lake system the
  ports exist at install time.
- On 2000 (`adl`) and on XP behind a device router's switch, a hot-added
  NVMe enumerates without the tunnel id Stor98's INF matches on NT 5, so
  Windows' wizard offers no driver; the installer's run puts Stor98 on it.
- XP behind a device router: with no I/O window on the tunnel port the
  parent's switch stops at Code 12; with the root port's I/O reservation
  the switch numbers, and its chain port still stops at Code 12 (cause not
  settled, above).

### Left

- 45.53: ME, XP x64 (the amd64 packages are unsigned; the in-guest
  throwaway signing) and Vista x86 legs; 2000's driver match on a desktop
  plug.
- 45.55: the XP chain port's Code 12 and the parent's NVMe without the
  tunnel id; 98 SE's reading with the connection manager confirmed
  running; a leg with the board's root port reservation (`-AsmReserve`,
  which the launcher does not give 2000 and XP, or one within 4 GB); 2000,
  ME, XP x64 and Vista legs.

## 45.58 the rest of 45.21's emulator legs: Falcon, Cactus and Light Ridge (2026-10-31)

**Verdict: done here.** `usb4-emu` now has the PCIe switch of Apple's Falcon Ridge 4C,
Cactus Ridge 4C and Light Ridge Thunderbolt hosts on q35. Their host profiles, from task
45.21, already existed. With the switch on each model, the q35 legs pass on 7 x86 and
10 x64: install, the enclosure at power-on on USB4 port A, a surprise unplug, and a
re-plug on port B. The Cactus Ridge leg found a defect in this driver's Thunderbolt-host
profile: a generation 1 link, which never bonds, failed for want of a TMU Off. It is
fixed (`8d0167a1`). Every model and system passes on the fixed build, which was also run
on `usb4-emu`'s new default (tunnel ports with no power controller), with the same
results. Left as 45.21 leaves them: 98 SE to Vista on a Thunderbolt host, the Mac
itself (45-E.11) and the release notes' "Not claimed: Apple machines". The USB-only NHIs
`15DC`, `15DD` and `15DE` stay unclaimed.

### The models (`usb4-emu`, branch `w45-apple`)

`ef96164`, merged with `usb4-emu` `main` as `2a0de92` and `21ef267`. The q35 switch
types are `usb4-fr-usp`/`-dsp`/`-tunnel-dsp` (Falcon Ridge 4C), `usb4-cr-*` (Cactus Ridge 4C) and
`usb4-lr-*` (Light Ridge), switch ports only (`usb4-emu` `docs/TB-SWCM-HOSTS.md` section
2a). The guests' composition is the gated root port `00:08.0`, then the upstream port,
then a port at `0.0` for the NHI and tunnel ports at `1.0` (`tunnel-port`, port A's PCIe
Down) and `2.0` (`tunnel-port2`, port B's). The host profiles `tb2-apple-falcon-ridge`
(`apple-subsys-id=4660`), `tb1-apple-cactus-ridge` and `tb1-apple-light-ridge` are from
45.21. A device router of the host's generation was used:
`usb4-device dialect=tb3,tb3-router-id=0x156d` (Falcon Ridge) or `0x1549` (Port Ridge,
generation 1). Facts, tagged as record 22 section 19 tags them:

| Fact in the model | Source |
|---|---|
| Falcon Ridge 4C: NHI `156C`, host router and switch ports `156D` (2C `156A`/`156B`, not composed) | [linux] `pci_ids.h`, `nhi.h` |
| Cactus Ridge 4C `1547` and Light Ridge `1513`: NHI, host router and switch ports one id; the bridges carry the NHI's subsystem `2222:1111` (in their Subsystem ID capability) | [linux] `pci.c` `nhi_ids[]` ("the TB bridges use the same device and vendor (sub)id on gen 1 and gen 2 controllers"), `switch.c` `tb_switch_get_generation` |
| No integrated xHCI on these three controllers | [linux] `xhci-pci.c` names one from Alpine Ridge on only |
| PCIe Down per port: Falcon Ridge 6/8, Cactus Ridge 6/7, Light Ridge any free | [linux] `tb.c` `tb_find_pcie_down` |
| Generation 1: no link controller, identity from the DROM UID, never bonded; generation 2 bonds through the link controller | [linux] `lc.c`, `eeprom.c`, `switch.c` `tb_switch_set_uuid` |
| Falcon Ridge's subsystem vendor `106B` on a Mac, and its subsystem device | [unread]: record 22 section 19.3's selection; the device is the emulator's `1234` |
| Bridge revision 1, the Falcon Ridge bridges' subsystem `0:0`, interrupt pin A, no power controller on the tunnel ports, the number and order of the downstream ports | [unread]: emulator choices, as the captured Alpine Ridge |

`qtest` `usb4-tbhost-test` gained `/usb4-tbhost/switch-q35` (ids, class, Subsystem ID
capability, the tunnel ports hot-plug capable with no power controller, the NHI behind
its port) and `/usb4-tbhost/no-xdomain` (`usb4-xdomain` refused on every
`tb*-apple-*` profile: no inter-domain link on a host with no firmware CM). On the
merged tree every usb4 suite ran complete against its TAP plan, 260 cases with no
failure. Reviewed; no material findings.

### The defect the Cactus Ridge leg found (`8d0167a1`)

Leg `cr-w7x86` a, on the build of `9efe2115`. The NHI bound ("xHCI98 TB1 Controller (Intel
Cactus Ridge)", "USB4 software connection manager") and the Port Ridge router was read.
`XHCIINST /devices` then showed "connection held or failed" and "PCIe none". The qemu
flavour of `xhcicm.sys` was swapped in to log on debugcon:
`u4.bond.impossible=1`, `u4.bond.tmu-not-off=1`,
`u4.port.state=00050107` (port 1 FAILED, reason BONDING). A generation 1 link never
bonds, so `tbtU4BondSingle` runs. It asked for the link's TMU Off before reaching its
Thunderbolt-host branch, which keeps two single lanes. The emulator's routers come up
with their TMU running. Guide 7.5 asks for the TMU Off only before a Lane Disable, which a
Thunderbolt host never writes, and record 22 section 19.4 item 6 leaves its TMU as it
is. So the check is now made on a USB4 host alone. Falcon and Alpine Ridge never reached
it, because their links bond. The vector is `test_u4cm` `test_tbt_host`: Cactus and
Light Ridge with a Port Ridge device and both routers' TMU running (fake fabric
`fabTbtTmuOn`). It fails without the fix (state FAILED). With the fix, `test_u4cm` runs
1918 checks with no failure. Record 22 section 19.9's "Lanes that do not bond" says so,
and that Linux bonds a Thunderbolt link from generation 2 through the link controller
(`lc.c` `tb_lc_lane_bonding_possible`), not "generation 3 in practice". Reviewed; no
material findings. No release-notes text: the profile is new in this release.

### Builds and packages

- The gated release x86 and amd64 builds of `9efe2115` and of `8d0167a1` all read
  "BUILD + GATES PASSED (release)", and the host tests passed in each
  (`E:\xhci98-scratch\p45-w45-apple\build-x86.log`, `build-amd64.log`,
  `build-x86-2.log`, `build-amd64-2.log`). The qemu flavour x86 of `9efe2115`
  (`build-qemu-x86.log`) was used only for the diagnosis.
- The packages are `make-package.ps1 -Flavor release` x86 and amd64 of `8d0167a1`
  (`xhcicm.sys` x86 `66cb7616...`, amd64 `7a6798fb...`; `xhci98.sys` x86 `c7c144d1...`,
  amd64 `17290220...`). They were staged into the 3.3.0.0 release layout with this
  tree's `INSTALL.BAT`, `SETUP.EXE` and `XHCIINST`/`XHCIIN64`, built by
  `xhciinst\build.cmd` (its self-tests passed).
- x64: the package is unsigned. Its five catalogs were made on the host by the
  packager's `New-PackageCatalogFile`, unsigned. In each 10 x64 overlay (test mode on), a
  throwaway certificate (`CN=w45apple`, MakeCert, valid 2026 to 2030) was put in Root and
  TrustedPublisher, and the four `.sys` and five `.cat` files were signed and verified
  there (`SIGN64.BAT`, serial logs). Nothing was signed on the host.

### The legs (q35; kit `E:\xhci98-vm\p45\w45-apple`, `leg.ps1`)

Each leg used a fresh overlay of the clean base. The install was `INSTALL.BAT /console
/yes /nopause /thunderbolt /stor98` (7 x86: Windows' unsigned-driver prompts answered;
10 x64: `SIGN64.BAT` first), then a restart and `R.BAT`: the PCI devnodes with their codes,
the volumes and `XHCIINST /devices`. Then QMP `device_del` of the enclosure, `R.BAT`,
`device_add` on USB4 port 2, and `R.BAT` again. Emulator: `usb4-emu` `w45-apple`
`ef96164` (`qemu-system-x86_64.exe` `7012d8b2...`).

| Leg | Build | Port A at power-on (after the install's restart) | Unplug | Port B |
|---|---|---|---|---|
| Falcon Ridge, 7 x86 (`fr-w7x86` a) | `9efe2115` | "xHCI98 TB2 Controller (Intel Falcon Ridge)" on `PCI\VEN_8086&DEV_156C&SUBSYS_1234106B`, the switch ports "xHCI98 Thunderbolt PCI Bridge" (`156D`), all Code 0; router `8086:156D` approved, two lanes bonded, PCIe up at `02:01.0`, Stor98 started, D: ORICO | "Nothing is connected", D: gone | PCIe Down 8 at `02:02.0`, Stor98, D: ORICO |
| Cactus Ridge, 7 x86 (`cr-w7x86` b) | `8d0167a1` | "xHCI98 TB1 Controller (Intel Cactus Ridge)" on `DEV_1547&SUBSYS_11112222`, the bridges `DEV_1547&SUBSYS_11112222` "xHCI98 Thunderbolt PCI Bridge", all Code 0; router `8086:1549` approved, one lane (two single lanes), PCIe up at `02:01.0`, Stor98, D: ORICO | the same | PCIe Down 7 at `02:02.0`, D: ORICO |
| Light Ridge, 7 x86 (`lr-w7x86` a) | `8d0167a1` | "xHCI98 TB1 Controller (Intel Light Ridge)" on `DEV_1513&SUBSYS_11112222`, all Code 0; the same as Cactus Ridge | the same | adapter 7 at `02:02.0`, D: ORICO |
| Falcon Ridge, 10 x64 (`fr-w10x64` d) | `8d0167a1` | as on 7 x86, Code 0; Stor98, E: ORICO | the same | `02:02.0`, E: ORICO on Windows' `stornvme` (a new instance takes Windows' own driver, as 45.21's 10 x64 leg saw) |
| Cactus Ridge, 10 x64 (`cr-w10x64` d) | `8d0167a1` | the same as 7 x86, Code 0, E: ORICO (read after a QMP re-plug on port A: the first run lost its report) | the same | `02:02.0`, `stornvme`, E: ORICO |
| Light Ridge, 10 x64 (`lr-w10x64` d) | `8d0167a1` | the same (read after a re-plug on port A, likewise) | the same | `02:02.0`, `stornvme`, E: ORICO |

The Falcon Ridge 7 x86 leg ran on `9efe2115` before the fix. The fix does not reach a
link that bonds, which this one is; the run below installs `8d0167a1` fresh. Evidence:
`<model>-<os>\` with `serial-<tag>.log`, `rep-*.txt`, `trace-<tag>.log` (the tunnel
transitions), `cmdline-<tag>.txt`, screenshots; `cr-w7x86\debugcon-a.log` and
`trace-a.log` (the diagnosis); `hashes.txt`, `pids.txt`.

### Again on the new default (tunnel ports with no power controller)

`usb4-emu` main moved to tunnel ports with no power controller by default during this
task. The models follow it (`usb4-fr/cr/lr-tunnel-dsp`, as `usb4-tb3-tunnel-dsp`). The
emulator is the merged tree (`qemu-system-x86_64.exe` `8fa353cb...`). The installed
overlays above were started on it (tag e), and Falcon Ridge 7 x86 was installed fresh with
`8d0167a1`:

| Leg (tag e) | Port A at power-on | Unplug | Port B |
|---|---|---|---|
| Falcon Ridge, 7 x86, fresh install of `8d0167a1` | the same as above: 20 devnodes Code 0, bonded, PCIe up at `02:01.0`, Stor98, D: ORICO | "Nothing is connected" | `02:02.0`, Stor98, D: ORICO |
| Cactus Ridge, 7 x86 (overlay b) | 20 devnodes Code 0, one lane, "PCIe up (found at start)", Stor98, D: ORICO | the same | `02:02.0`, Stor98, D: ORICO |
| Light Ridge, 7 x86 (overlay a) | the same, "PCIe up (found at start)" | the same | `02:02.0`, Stor98, D: ORICO |
| Falcon Ridge, 10 x64 (overlay d) | 14 devnodes Code 0, bonded, PCIe up at `02:01.0`, Stor98, E: ORICO | the same | `02:02.0`, `stornvme`, E: ORICO |
| Cactus Ridge, 10 x64 (overlay d) | the same, one lane | the same | `02:02.0`, `stornvme`, E: ORICO |
| Light Ridge, 10 x64 (overlay d) | the same, one lane | the same | `02:02.0`, `stornvme`, E: ORICO |

No result changed. One reading differs. On the two 7 x86 overlays started cold, the
connection at port A read "PCIe up (found at start)": `xhcicm` had been stopped and
started once after it built the tunnel. The emulator's trace has the tunnel up once and
never down, so the restart adopted it as found (record 22 section 9). The same overlays
on the old default read "PCIe up" after the install's restart. That this is Windows
rebalancing the switch once the endpoint answers at once is an inference, not read.
Evidence: `<model>-<os>\` tag e (`serial-e.log`, `rep-e*.txt`, `trace-e.log`,
`rerun-e.log`, `run7-e.log`).

### Findings for others

- `XHCIINST /devices` labels a generation 1 or 2 router "Thunderbolt 3" ("Thunderbolt 3,
  port 1, depth 1" for a Port Ridge `8086:1549` and a Falcon Ridge `8086:156D`). It is
  the tool's text, not the CM's.
- The kit's `RINT.BAT` echoed `EXIT 0` where the installer had printed "Exit code 3"
  (restart): `%ERRORLEVEL%` after `call INSTALL.BAT` in that script is not the
  installer's. The kit read the screen.

## 45.60 the installer's Thunderbolt and USB4 page: the code and the legs on 98 SE, XP and 7 (2026-10-31, partial)

Branch `w45-adv`, fast-forwarded to `4dc11e98`, tree
`E:\xhci98-wt\w45-adv` on development host A. Commits: `0643a6d9`
(record 26 draft 53, section 5.3.5: the design), `8ccc8c0d` (the code
and the host vectors), `31f7984c` (the two approval buttons one group);
each reviewed and revised.

### What changed

- A page of its own, "Thunderbolt and USB4", after Components: shown
  only while the effective Thunderbolt choice is on, skipped Next and
  Back alike otherwise. It carries XHCIPFW, Stor98 and S98VSD, moved
  off the Components page with their rules and remembered states, and
  the approval choice (`XhciCmApprove`) as two option buttons, moved off
  the Settings dialog's Thunderbolt tab so it is offered once, taken as
  the tab's row was (written machine-wide only when it differs from the
  value in force).
- The Components page's Thunderbolt sentence says what was found alone;
  what a device can reach is said under the approval choice. The box
  keeps "(experimental)", the owner's word.
- The Advanced dialog's heading no longer names testing.
- Thunderbolt networking's box (task 45.59): its slot on the page,
  hidden while `INST_TBNET_WIRED` is 0; `/tbnet` is refused ("this
  package carries no Thunderbolt networking driver for this system"),
  `/notbnet` changes nothing; `inst_tbnet_decide` holds record 35
  section 9.1's rule (first state, writes, targets), host-tested.
  Wiring it to the driver is task 45.59's step S11.

### The builds and the self-tests

- `XHCIINST.EXE /selftest` and `XHCIWIZ.EXE /selftest`, run in an XP
  guest before the gated build: 11 checks and 743 checks, 0 failures
  (745 after `31f7984c`).
- The gated x86 build at `8ccc8c0d`: BUILD + GATES PASSED (release,
  qemu), 22:27. At `31f7984c`: BUILD + GATES PASSED, 24:21. Host tests
  passed (`test_inst` 7501 checks).
- The test package: `make-release.ps1 -Arch x86 -TestLabel w45adv
  -NoNonFree -NoWdmFree -SkipQualtool -SkipMarkTool` of `31f7984c`;
  `XHCIWIZ.EXE` `39a80fb0...`, `XHCIINST.EXE` `6f2e046f...`.

### The legs

Each a first install on a fresh overlay of the q35 base through
`launch-q35.ps1 -Usb4 asm4242 -EncAtBoot`, driven from the keyboard;
QMP 49451, 49453 and 49454; every start and stop in
`E:\xhci98-vm\p45\w45-adv\pids.txt`, the screens in each leg's folder.

| System | What was done | Result | Evidence |
|---|---|---|---|
| 98 SE (`8ccc8c0d`) | `SETUP.EXE /tbnet` | refused in a message box: "/tbnet: this package carries no Thunderbolt networking driver for this system" | `w98t\a1-tbnet.png` |
| 98 SE (`8ccc8c0d`) | Components with Thunderbolt unticked, Next; Back | the Summary next (the page skipped), Back to Components; the moved boxes gone from Components | `w98t\b1-comp.png`, `b2-next-untick.png`, `b3-back.png` |
| 98 SE (`8ccc8c0d`) | Thunderbolt ticked; Settings, Thunderbolt tab; Next | the tab holds `XhciCmMode` and `XhciCmSafeEject` only; the page shows XHCIPFW.VXD, Stor98, S98VSD and the approval preselected 1 with the new-install note | `w98t\b8-settings-tb.png`, `c1-tbt.png` |
| 98 SE (`8ccc8c0d`) | "Give it none" from the keyboard | not reachable: the arrow keys stayed on the first button (the sentence between ended the group). Fixed in `31f7984c` | `w98t\c2-none.png` |
| 98 SE (`31f7984c`) | "Give it none" by the arrow keys, S98VSD unticked, Back and Next again, Install | the page kept both; the Summary listed `XhciCmApprove = 0` and no S98VSD; after the install `Services\USB` holds `XhciCmApprove`=0 alone, the host interface's key the INF's 1 | `w98t2\c2-none.png`, `c4-again.png`, `d1-summary.png`, `d2.png`, `serial-a.log` |
| XP (`31f7984c`) | Thunderbolt ticked, "Give it none", Install, restart | the page shows XHCIPFW.SYS, Stor98, S98VSD greyed "(Windows 98 SE and ME only)"; `Services\USB` holds `XhciCmApprove`=0; `xhci98`, `xhcicm`, `xhcipfw` loaded | `xpt\c1-tbt.png`, `d11.png`, `serial-a.log` |
| 7 x86 (`31f7984c`) | Thunderbolt ticked, the approval left at its preselected 1, Install, restart | no approval line on the Summary or the Finish page; the read of `Services\USB` printed no value; `xhci98`, `xhcicm`, `xhcipfw` loaded | `w7t\c1-tbt.png`, `d1-summary.png`, `d3.png`, `serial-a.log` |

- 98 SE was not restarted (the known shutdown hang with `xhcicm`).
- The networking box was never shown, as its gate says.

### Left

- The networking box's wiring (staging `xhcinet.inf`, the `XhciCmNet`
  write after it, `/tbnet` accepted, the console's question, the
  uninstall's removal) is task 45.59's step S11, after its driver steps
  S5 to S7: `INST_TBNET_WIRED` 1 there, and `inst_tbnet_decide`
  connected.
- Legs on ME, 2000, Vista, 8 to 10 (the roadmap's "98 SE to 10"): the
  coordinator named 98 SE, XP and 7 for this task.

## 45.59 step S6: xhcinet.sys, the NDIS 5 miniport of Thunderbolt networking (2026-10-31)

Design record 35 sections 6.3, 7.1 to 7.8 and 8.2 to 8.3, coded on branch
`w45-tbndis` (`8fc26b5a` to `8febce8a`), each change reviewed and revised.
The connection manager's half (S5) is not in this tree, so nothing here
has run against it: this section records the code, its gates and the
build. The legs (record 35 section 11.3) wait for S5.

### What was made

- `src\net\net_frame.c` / `.h`, the USB4NET framing core, pure, with no
  NDIS type (shared unchanged with `xhcinet6.sys`, S7): the transmit split
  into 4084-byte USB4 frames with their 12-byte header; one Frame ID a
  packet, advanced only towards a peer whose `prtcstns` has bit 1, wrapping
  at `0xFFFF`; the receive checks (CRC Error, Buffer Overflow, a frame of
  12 bytes or fewer, Frame Size 0 or past the data, Count 0 or above 17,
  Index not below Count) and the reassembly, every refused frame ending
  the packet in progress, the SOF and EOF PDFs never read (a frame of one
  tunnelled packet reads SOF PDF 0); and section 7.4's receive
  segmentation of a TCP segment past the adapter's maximum, over IPv4 and
  IPv6, both checksums checked before anything is split.
- `test\test_net_frame.c`: 2,554 checks (the lengths 14, 60, 1514, 4084,
  4085, 65536; the Frame ID rule; every drop; reassembly with gaps,
  restarts and the 65550 cap; flagged and malformed frames between two
  packets of Frame ID 0; the split over IPv4 and IPv6 with options, odd
  maxima and wraps, and its eleven refusals).
- `src\net\net_cmif.c` / `.h`, the glue to `src\xhci_cmnet.h` (WDM alone,
  shared with S7): `IRP_MN_QUERY_INTERFACE` to the device object below,
  the release, and the calls over the table.
- `src\net\ndis5\`: the deserialized NDIS 5.0 WDM miniport (5.1 on amd64):
  one sender at a time, every context completing sends counted so a reset
  completes and a halt frees only after the last; the receive pool with
  `NDIS_STATUS_RESOURCES` when low; the power OIDs on a thread of its own
  (joined by its exit event on 98 SE and ME, by its thread object on NT);
  `USE_KLOCKS` on x86, so the spin locks are the kernel's on 98 SE and ME
  too; `XhciNetMtu` 1500 (1500 to 65522).
- `src\net\xhcinet.inf` and `xhcinet-amd64.inf` (98 SE and ME, `.NTx86`,
  `.NTamd64`; the `NTx86.6.2` and `NTamd64.6.2` models present and empty
  until S7), `XhciNetMtu` written at `1500` with no-overwrite on every path;
  `scripts\inf-gate\check-net-inf.ps1` (36 self-test mutations) on every
  build.
- The import gate's `-GateProfile net98` over `xhcinet-imports.allow`:
  each `NTOSKRNL.EXE` and `HAL.DLL` pair also a row of
  `xhci98-imports.allow`, each `NDIS.SYS` pair a prototype of the Windows
  2000 DDK's `ndis.h` (`bc-header`) and a pair of the run record that
  `net98-runtime.list` pins (`w98-runtime`); `xhcinet-imports-amd64.allow`
  under the default amd64 rules. `build-driver.cmd :buildnet` on both
  architectures.

### The 98 SE and ME reading

Revised the same day, after the owner admitted a Microsoft binary's export
names for the import gate's baselines (2026-10-31, 14:40): `NDIS.VXD` was
taken read-only from the q35 98 SE and ME base images (`7z e`, nothing
booted) and only its names read. Every one of the 24 `NDIS.SYS` imports of
the x86 image is a NUL-delimited name in both files (98 SE 166,029 bytes,
ME 157,397 bytes; their SHA-256 in `scripts/import-gate/
ndisvxd-baselines.expected`); the reading tells names apart (98 SE's file
lacks `NdisQueryBufferSafe`, which ME's has), and the net98 profile now
fails a pair absent from either where the files are staged. So the owner's
condition for 98 SE and ME (record 35 section 15, Q10) is met by the
reading the record asked for. Before it, and kept beside it: the 24
`NDIS.SYS` imports of the x86 image are each declared in the 2000 DDK's `BINARY_COMPATIBLE` `ndis.h`
("so that we don't use functions that aren't available on Windows 9x"),
and each is among the imports of `usb-wdm-free`'s `cdcnet.sys` at
`57ba809`, an NDIS 5.0 WDM miniport recorded loading and passing traffic
on 98 SE and ME in QEMU (`docs/testing/2026-10-22-cdcnet-9x.md` there).
`legal-provenance.md` section 4 has the row. This image's own load on 98
SE and ME is a leg owed.

### Builds

| Build | Commit | Result | Log |
|---|---|---|---|
| gated x86 release (`build-driver.cmd release`) | `8febce8a` | BUILD + GATES PASSED; `xhcinet.sys` 15,198 bytes, SHA-256 `5e7e76774d81043f...`; `test_net_frame` 2,554 checks; `check-net-inf` 36 mutations; net98 import gate passed | development host A, `gated-x86.log` in the worker's scratch folder |
| gated amd64 release (`-amd64`) | `35376983` (`8febce8a` and a header comment) | BUILD + GATES PASSED; `xhcinet.sys` 19,456 bytes, SHA-256 `b51c97f32adab371...`; the import gate passes with warnings, its `NDIS.SYS` rows having no staged NT 5.2 amd64 `ndis.sys` to check against | `gated-amd64.log` |

### Owed

- Every leg of record 35 section 11.3, after S5 (and the 98 SE and ME
  load of this image, which closes the `w98-runtime` evidence).
- The `NTx86.6.2` decoration read on 98 SE, ME, 2000 and XP (record 35
  section 8.2), and the `$CHICAGO$` net INF read on 2000.
- The package's carrying of `xhcinet.sys` (S10): the gated record and
  the source stamp do not yet list it.

## 45.62 a tunnelled unit attached during the install listed after a restart, and XHCITR's mark line (2026-10-31)

**Verdict: done.** On XP and 2000 (Standard PC HAL) a unit attached
through the install is now listed in Safely Remove Hardware after a
restart, with no re-plug. XHCITR's mark line names the driver's answer.
Record 21 section 7.7; commits `0301f107` and `0ccb991d`, each reviewed
and revised.

**The cause.** At a start of Windows the PnP manager asks for the unit's
capabilities, at the enumeration and after the start, before the NHI
lists its set. The connection manager's child then passes the request
through (passive until associated), so Windows records no `Removable`.
The design's remedy was the mark service's restart of the unit. The
service never made it, because the entry's "`Removable` answered" flag
was set by a later query that Windows does not record (record 21 section
7.7.2).

**The first change** (`0301f107`): on 2000 and XP the service reads the
capabilities Windows recorded (`CM_DRP_CAPABILITIES`), and an answered
unit whose recorded capabilities lack `Removable` (and
`SurpriseRemovalOK`) is a restart candidate. XHCITR says "Safe-removal
mark: the Thunderbolt driver has answered it as removable." and, beneath
it when Windows' capabilities lack `Removable`, "Windows has not recorded
it as removable."

Its legs (the gated build of `0301f107`; fresh q35 XP and 2000 overlays,
`xpd` and `w2b`, the ASM4242 model with the ORICO enclosure at power-on,
`INSTALL.BAT /console /yes /nopause /thunderbolt /stor98`, a restart): the
service chose the unit and logged "restart not made: ... a disk or volume
below the unit is in use. Unplug the dock safely and plug it in again".
That is record 33 section 4.3's gate, which refuses a restart of any unit
with a volume in use, and an enclosure's unit always has one. XHCITR's new
lines read as designed.

**The second change** (`0ccb991d`): the `AddDevice` child (the service's
`LowerFilters` mark) answers `Removable` before association, inside a
start-up window: no domain listed since the driver loaded, no `AddDevice`
child failed, 300 s. It does so only for a unit that a bridge filter
reported with no marked bridge filter above it, so the bridge marks inside
an enclosure or a dock do not answer. The answers are counted and logged
as `cm.reenum.child.pretrust`.

**The legs** (the gated build of `0ccb991d`: `xhcicm.sys` `2114e117...`,
`xhci98.sys` `6ca31af9...`, `XHCIMARK.EXE` `0309fa93...`, `XHCITR.EXE`
`d5ff1929...`; fresh overlays `xpf` and `w2c`, the same model and
enclosure, the same install, then a restart: XP warm, 2000 a cold start):

| Read | XP | 2000 |
|---|---|---|
| `cm.reenum.child.pretrust` | `0000000B` | `0000000A` |
| the unit (`1B21:2463` at `03:00.0`) | "Removable: yes"; "Windows lists it in Safely Remove Hardware too." | the same |
| the bridge below it (the enclosure's downstream port) | "Removable: no" | "Removable: no" |
| XHCITR's mark line | "the Thunderbolt driver has answered it as removable." | the same |
| Safely Remove Hardware (2000: Unplug or Eject Hardware) | lists "PCI standard PCI-to-PCI bridge at PCI Slot 2" alone; Stop lists the bridge, "Generic volume - (D:)", the disk, Stor98 and the bridge below; OK: "Safe To Remove Hardware" | after the second cold start: the same list and stop ("Generic volume - (E:)"), "Safe To Remove Hardware" |
| a second restart | the same: `pretrust` `0000000B`, the unit "Removable: yes" | a second cold start: the unit listed (above) |

The service's log at that start held no restart (none was needed). Its
candidate rule stays for a unit whose start came with a domain listed.
The host's own xHCI behind the tunnel (the ASM4242's, protected) reads
"Not marked removable" with "Windows has not recorded it as removable.",
which is right: it is never offered for removal. Evidence:
`E:\xhci98-vm\p45\w45-owed27\xpd\`, `w2b\`, `xpf\` (`tr-b1.txt`,
`tr-b2.txt`, `b3.png` to `b5.png`), `w2c\` (`tr-b1.txt`, `c2.png` to
`c4.png`).

**For 45.final** (the coordinator's): the known limitation "a drive
attached during the install is not listed ... until Windows restarts or
the drive is plugged in again" now holds as written on 2000 and XP: a
restart lists it. Vista and later were not read; the service's new rule
is off there.

## 45.65 a copy that never ends on Vista x86 after an unplug: Vista's own USB stack first (2026-10-31)

**Verdict: the hang comes without xHCI98; no code.** 45.61's reading (record 13 section
17.6, `runs/run-45.md` "45.61") found that on Vista x86 a drive pulled during
a copy loop, with an Explorer window open on its volume, leaves the next copy
blocked for good, the volume mounted and `shutdown` hanging, for a UAS drive
and for a Bulk-Only stick on `usbstor.sys` alike, on xHCI98. The same leg on
Vista's own USB stack, with no xHCI98 installed (the emulator's EHCI,
`usbehci.sys`, the same kind of stick on `usbstor.sys`), shows the same hang,
twice, and once without the Explorer window as well. XP and 7 on the same
emulator and the same EHCI fail the copies cleanly. By the task's own rule
(the same setup on Vista's own stack first) the hang is Vista's or the
emulator's, and the task stops here with no code. That rule is what decides:
no xHCI98-specific cause was found, but none was looked for either (no
request was read on the xHCI98 runs), so the reading does not prove that
45.61's runs had the same cause, only that the visible failure needs no
xHCI98. Not read on real hardware.

### The machine

Development host A; the q35 bases' pinned emulator (`bin\asm4242-20261025-2230`,
`qemu-system-x86_64.exe` SHA-256 `da60139f...0c93`); fresh overlays of the q35
bases `vx86` (`4d1aa775...2890`), `xp` (`64ada103...d5f0`) and `w7x86`
(`95893177...2a7d`), nothing of xHCI98 on them. No `qemu-xhci`: an
`ich9-usb-ehci1` with the stick on its port 1:

```
-device ich9-usb-ehci1,id=eh
-drive if=none,id=bd0,format=qcow2,file=<dir>\bot.qcow2
-device usb-storage,id=bot0,bus=eh.0,port=1,drive=bd0,serial=W45VISTACPB
```

`bot.qcow2` is an overlay of the q35 bases' FAT test disk
(`shared\nvme-fat.qcow2`, volume "ORICO"), drive `D:` in each guest. Vista
and 7 x86 at 2 vCPU and 2 GB, XP at 2 vCPU and 1 GB (the q35 launcher's
defaults). In each guest `sc query xhci98` answered 1060 (not installed) and
`usbehci` was running; after the unplug `usbstor` read STOPPED (its only
device gone), so the stick ran on `usbehci.sys` and `usbstor.sys`.

### The leg

A command prompt runs `for /l %i in (1,1,100000) do copy /y
C:\Windows\system32\shell32.dll D:\x.bin`; where the table says so, the
volume is first opened in Explorer (`start D:\`; on 7 AutoPlay's "Open
folder"). After about 25 copies the stick is pulled by QMP `device_del bot0`.

| System | Explorer on `D:` | Result |
|---|---|---|
| Vista x86 (`vx86e\b1` to `b12`) | yes | "The system cannot find the file specified", "0 file(s) copied", then the next copy blocked for good (over 6 min); `D:` still shown in Explorer; a second prompt's `wmic logicaldisk` and `dir D:\` blocked; the Run dialog no longer opened; the ACPI power button did nothing in 4 min; QEMU stopped |
| Vista x86 (`vx86r\b1` to `b4`), a repeat | yes (opened by AutoPlay on its own before the loop) | the same: the next copy blocked (over 5 min), a second prompt's `dir D:\` blocked; QEMU stopped |
| Vista x86 (`vx86n\b1` to `b7`) | no | the same: the next copy blocked (over 4 min), `dir D:\` blocked, `shutdown -s -t 0` from the Run dialog never shut down (4 min); QEMU stopped |
| XP (`xpe\b1` to `b4`) | yes | every copy after the unplug fails "The system cannot find the drive specified"; Explorer's window closed; `shutdown -s` powered off |
| 7 x86 (`w7e\b1` to `b5`) | yes | the copies fail "The system cannot find the drive specified"; Explorer went to "Computer"; `shutdown -s` powered off |

Evidence: `E:\xhci98-vm\p45\w45-vistacp\` (`vx86e`, `vx86r`, `vx86n`, `xpe`, `w7e`:
the screenshots and `cmdline-a.txt`; `pids.txt`).

### What it means

On xHCI98 the hang came with the Explorer window (45.61: one run without it
failed the copies cleanly); on Vista's own stack it came with the window
twice and without it once, so the window looks like it widens a timing
window rather than causing the hang (a few runs each; not read further).
The stick's devnode is gone (`usbstor` stopped) while the volume stays
mounted, so the copy most likely waits above the USB host controller and the
hub (not read). The emulator is not ruled out: XP and 7 on the same EHCI
model are clean, but no real Vista machine was read.

### Owed

- Nothing in this driver by the task's rule. A real-hardware read on Vista
  (any USB stack) would tell Vista from the emulator; not required for the
  release.

## 45.64 the emulator read against Linux's and Windows 11's own USB4 stacks (2026-10-31)

Reference connection managers, with nothing of this package installed,
on `usb4-emu`'s emulated hardware: Linux's `thunderbolt` driver on a
Kubuntu 26.04.1 live guest and Windows 11's in-box USB4 stack on a
Windows 11 x64 guest, each on q35 under WHPX on development host A.
Every difference was read either as the model's (fixed in `usb4-emu`
with a qtest) or as a platform piece the emulator lacks. No Windows 11
support is claimed by this task.

### Setup

- `usb4-emu` branch `w45-u4ref` from main `6fbfb08`, main `dc6d437`
  (the Falcon, Cactus and Light Ridge switches) merged in before the
  Apple legs; QEMU from the fork at `af059ed`, then `8956e35`, then the
  two fixes below (`a1bb50a`). The first five Linux legs (generic,
  ASM4242, version 2, chain, Alder Lake) ran on `af059ed`, the rest on `8956e35`; the two
  fixed points were re-read on `a1bb50a`.
- Linux: the stock `kubuntu-26.04.1-desktop-amd64.iso` (read-only CD,
  kernel 7.0.0-30-generic and its initrd booted directly), command line
  `boot=casper console=ttyS0,115200 systemd.unit=multi-user.target
  thunderbolt.dyndbg=+p`; the trace events `thunderbolt:tb_tx`, `tb_rx`
  and `tb_event` enabled after login (`trace_event=thunderbolt:*` on the
  command line does not reach the module: "Failed to enable trace
  event"); 2 vCPUs, 4 GB, no network. Each leg: authorise every router,
  write and read back 16 to 32 MiB on every NVMe disk (O_DIRECT, SHA-256
  equal), unplug a device router (`device_del`) and plug it back
  (`device_add`), then dmesg, the trace buffer, `lspci -vvv`, sysfs and
  debugfs copied to a disk.
- Windows: a fresh unattended install of
  `Windows11_Client_x64_en-us_26300_9457.iso` (build 26300.9457, in-box
  `usb4hostrouter.inf` and `usb4devicerouter.inf` 10.0.26100.9278) with
  the LabConfig TPM, Secure Boot, RAM, CPU and storage bypasses, OVMF
  without Secure Boot, 2 vCPUs, 4 GB, user networking restricted to the
  guest agent's forward; its own base under the worker's VM folder,
  overlays per leg. Read from `Get-PnpDevice` (status, problem code and
  problem status), the disks (8 MiB written and read back per disk),
  `setupapi.dev.log`, the System and Kernel-PnP logs, and the
  TraceLogging rundown of the USB4 host and device router providers
  (`usb4-emu`'s `guest-usb4-trace.ps1`).

### Linux, per model

| Leg | Host | Devices | What Linux did |
|---|---|---|---|
| gen1 | generic USB4 host (`host-model=minimal`), USB3 xHCI wired | USB4 router with an NVMe and a USB disk behind its USB3 Up adapter | software CM, security user; the USB3 tunnel at enumeration, before authorisation (the disk read back); the PCIe tunnel on authorisation; deauthorise, re-authorise, unplug and plug all followed |
| asm | ASM4242, captured board preset, its PCIe switch | USB4 router; ASM2464 | the firmware CM's tunnel at power-on, then "0: resetting" and both routers enumerated again; both tunnels on authorisation; the ASM2464 unplugged and plugged |
| v2 | version 2 host | version 2 router; version 1 router | Gen 4 40 Gb/s x2 and Gen 3 20 Gb/s x2; enhanced uni-directional TMU (MedRes) on the version 2 router; Extended Encapsulation on the version 2 tunnel only; an asymmetric transition and LINK_RECOVERY ignored; Enable Gen 4 Link Recovery never set, so an uncorrectable error was a disconnect, a re-enumeration and a fresh authorisation; with the bit preset by the emulator, a recovery with no event and the disk readable |
| chain | generic | USB4 router with a chained USB4 router (route 301); a Thunderbolt 3 router on port 2 | all three enumerated (the Thunderbolt 3 one as generation 3, "Thunderbolt 3 Switch: 8086:15d3"), bonded and tunnelled; the chained router, then its parent, unplugged and plugged |
| adl | `host-model=intel-adl` | the TS3 Plus model with a chained ASM2464 (routes 3, 303) | as the Framework capture: both tunnels, both disks; the dock's unplug took both |
| amd | `host-model=amd` | the dock model (`dock=on`: PCIe switch, NVMe, e1000e, xHCI with a USB disk); Thunderbolt 3 router | "USB4 Switch: 438:208", "USB4 Host Router (amd preset)"; the dock's switch, NIC (link up), xHCI and USB disk and both NVMe disks; the dock unplugged and plugged |
| fr, cr, lr | `tb2-apple-falcon-ridge`, `tb1-apple-cactus-ridge`, `tb1-apple-light-ridge` with their switches | Thunderbolt 3 router with the generation's router ID; USB4 router or Thunderbolt 3 router | software CM, both tunnels, unplug and plug; Falcon Ridge only with the Apple SMBIOS vendor (below) |
| aar, atr | `tb3-apple-alpine-ridge`, `tb3-apple-titan-ridge` with their switches | Thunderbolt 3 router; ASM2464 or USB4 router | software CM, both tunnels, unplug and plug |
| icmar, icmtr | `tb3-alpine-ridge`, `tb3-titan-ridge`, switch and TB xHCI | Alpine Ridge: TS3 Plus model with a chained USB4 router; Titan Ridge: a Thunderbolt 3 router on each port, an ASM2464 chained to the second | firmware CM (`icm.c`): the chained router announced at depth 2 (routes 301, 303), approvals, both or all three tunnels, unplug and plug |
| icl | `tb3-ice-lake` | Thunderbolt 3 router; ASM2464 | firmware CM, security none, both disks from the first scan; the unplug while the NHI was runtime suspended was not reported until the next driver ready, as `usb4-emu`'s ICM-ICE-LAKE.md 5 records |

### Windows 11, per model

| Host | Test-signing | What Windows did |
|---|---|---|
| generic (`minimal`) | off | "USB4(TM) Host Router (Microsoft)" bound (`PCI\USB4_MS_CM` from the ACPI `_OSC` grants), then Code 31, problem status `0xC00000BB`, before any access to the host router's BAR; the same with an emulated IOMMU (`intel-iommu`, no tunnel ports) |
| `intel-adl`, `amd` | off | the same Code 31; the tunnel root port behind it Code 51 (waiting on a dependency) |
| ASM4242 | off | host router, root router and device router started; the NVMe tunnelled and listed as a disk |
| generic with the USB3 xHCI | on | both device routers, both PCIe tunnels and the USB3 tunnel up with no user action; the USB disk and both NVMe disks read back; the router with the USB disk unplugged and plugged, all back |
| ASM4242 with an ASM2464 | on | both routers and disks; the ASM2464 unplugged and plugged |
| version 2 | on | "USB4 Root Router (2.0)" and "USB4 Router (2.0)" at Gen 4, the version 1 router "(1.0)" at Gen 3, both disks; Enable Gen 4 Link Recovery left clear; an uncorrectable error: router removed and enumerated again, its disk back; with the bit preset by the emulator: recovered, no removal |
| `amd` with the dock model and a Thunderbolt 3 router | on | both routers ("USB4 Dock", "Thunderbolt 3(TM) Router"), the dock's xHCI with its USB disk, its e1000e and both NVMe disks; the three disks read back (the dock's NIC on a user network of its own, 10.0.3.0/24: on the guest's own subnet the guest's replies left through it and the agent went silent, a launch error, not the model's) |
| `intel-adl` with the TS3 Plus model and a chained ASM2464 | on | "Thunderbolt 3(TM) Router" and the chained "USB4 Router (1.0), ASMedia - 246x", both disks read back |
| generic, chain and a Thunderbolt 3 router | on | the chained USB4 router and "Thunderbolt 3(TM) Router" on port 2, three disks; the chained router and the Thunderbolt 3 router unplugged and plugged |
| `tb3-alpine-ridge`, `tb3-titan-ridge`, `tb3-ice-lake` | on | the NHI is "Base System Device", Code 28: no in-box INF names `8086:15bf`, `15eb`, `8a17` or the Thunderbolt 1 and 2 NHIs; on Ice Lake (security none) the firmware CM's tunnel still brought the NVMe up |

The Apple Thunderbolt 1, 2 and 3 profiles were not run on Windows 11:
nothing in it binds them.

### Differences and how each was read

- Model, fixed: a spec-derived host router's lane adapters read Target
  Link Speed 0 and no RS-FEC request after the power-on reset, because
  the reset that keeps a connection manager's values across a Host Router
  Reset (Spec 6.10) kept the still-empty adapters the first time.
  Windows' rundown showed `TargetLinkSpeed=0x0` on the generic host's
  ports and `0xC` on the device routers'; the captured Alder Lake host
  reads `0xC`. Fork `f5a3138`, qtest `/usb4-tb3dev/host-lane-defaults`
  (fails without the fix); Windows re-read on the fixed build:
  `0xC` on both. Side effect: the generic host's PORT_CS_18 RS-FEC
  Enabled (bit 12) now reads 1 on a linked port, as the device side
  already did.
- Model, fixed: the Thunderbolt controller switches' NHI and xHCI ports
  were hot-plug slots (QEMU's XIO3130 default), where the captured Alpine
  Ridge has `HotPlug-`. Linux on the Apple Thunderbolt 1 and 2 hosts
  logged "device link creation from 0000:02:00.0 failed"
  (`tb.c` `tb_apple_add_links` walks every hot-plug downstream port of
  the switch, the NHI's own included). Fork `a1bb50a`, qtest
  `/usb4-tbhost/switch-dsp-slots` (fails without the fix); Falcon Ridge
  re-read: links from the two tunnel ports only.
- Platform: Linux takes its software CM on Falcon Ridge only on an Apple
  machine (`icm.c` `icm_fr_is_supported` reads the DMI system vendor);
  without `-smbios type=1,manufacturer="Apple Inc."` it took the firmware
  CM and logged "could not start ICM firmware", as `usb4-emu`'s
  TB-SWCM-HOSTS.md already says.
- Platform: Windows 11's Code 31 on the generic host is not a missing
  ACPI or DMA-remapping piece: the same machine, ACPI `_OSC` and `_DSD`
  start the ASM4242 preset, also at the same address on the root bus with
  the same tunnel ports (so not the topology), and an IOMMU did not lift
  it. Test-signing lifts it, as `usb4-emu`'s WINDOWS-DEBUG.md recorded in
  October. What remains different between the presets at the PCI level
  is the host router's IDs (`1b36:00f4` refused, `1b21:2425` started),
  BAR0's width, the ASM4242's BAR2 and its MSI capability; the IDs are
  the leading reading [inferred: no run isolated them, as the model has
  no property that changes the IDs alone]. Left open in `usb4-emu`.
- Read as the Specification's, not changed: a host router that is not yet
  enumerated when the connection manager starts reports its linked lanes
  once enumerated (Spec 6.8.1.2). Linux queues those plug events, resets
  the router, and so enumerates each device router twice about 100 ms
  apart ("switch unplugged" for the reset's stale unplug); the end state
  is right. The captured hosts were already enumerated by their firmware
  when Linux bound, so no capture shows this case.
- By design: with the PCIe tunnel down and the router still attached,
  Presence Detect stays set (`usb4-emu` ARCHITECTURE.md 4.4, link
  retrain), so Linux's pciehp logs "Card present" and "No link" after a
  deauthorisation.
- Not a model difference: "pciehp: Timeout on hotplug command 0x0000" on
  a dock's or chained router's switch port after its parent was
  unplugged: Linux waited for a completion interrupt from a switch behind
  a closed gate, as from a switch that has gone.

### Where this package's CM and the reference stacks differ

- Enable Gen 4 Link Recovery: neither Linux nor Windows 11 set it on a
  Gen 4 link, so to both an uncorrectable Gen 4 error was a disconnect;
  this package's CM sets it (45.56), so to it the same error is a
  recovery.
- USB3 tunnels: Linux built the USB3 tunnel at enumeration under
  security level user, before the router was authorised; Windows built
  every tunnel with no user action.
- Version 2: both stacks wrote Connection Manager USB4 Version 1.0 on
  every router; neither started an asymmetric transition.

### Evidence

- Linux, per leg: `E:\xhci98-vm\p45\w45-u4ref\legs\<leg>\out\<leg>\`
  (`dmesg.txt`, `trace.txt`, `lspci.txt`, `sysfs.txt`, `debugfs.txt`)
  and `legs\<leg>\usb4-trace.log` (the emulator's trace); `fr-nosmbios`
  is the Falcon Ridge leg without the SMBIOS vendor, `fr2` the re-read
  on the fixed build. The v2, chain and adl dmesg files hold only what
  followed a `dmesg -C`; their boot lines were read on the console.
- Windows: `E:\xhci98-vm\p45\w45-u4ref\vms\w-gate\` (not test-signed:
  `tr\`, the gate's ETW zip, and `usb4-trace-*.log`) and `vms\w-ts\`
  (test-signed: `trace-gen-hotplug.zip`, `trace-v2.zip`,
  `trace-gen-fixed.zip` and `usb4-trace-<leg>.log`).
- qtests: every `usb4-emu` suite against its TAP plan on `a1bb50a`:
  host 43, tunnel 31, icm 40, icm-tr 16, icm-icl 5, harness 25, fault 9,
  asm4242 16, asm2464 4, tb3dev 13, tbhost 12, root-hotplug 22, usb3 9,
  retrain 11, xdomain 6, all passed; the fabric harness 51794 checks, 0
  failures (`E:\xhci98-scratch\p45-w45-u4ref\qt\`).
- `usb4-emu` commits `48b7813` (the patch series re-exported at
  `a1bb50a`, LINUX-TEST.md and WINDOWS-DEBUG.md "Reference read") and
  `5d73560` and `ca8a7ed` (the amd and dock rows, the gate's reading
  qualified), reviewed and revised.

## 45.61 a UAS drive in Safely Remove Hardware: the reading, the change and its legs (2026-10-31)

**Verdict: done.** The reading (below) found the UAS FDO's
`SurpriseRemovalOK` override to be the cause; the owner chose option A
(record 13 section 17.4), and with `817dd191` a UAS drive is listed and
stopped on its own on 2000, XP, Vista and 7, and on 2000 an unplug without
a stop shows "Unsafe Removal of Device", as for a Bulk-Only stick (the
legs, "The change and its legs", below).

Branch `w45-uasrem` (from `b71199a9`), development host A. Kit
`E:\xhci98-vm\p45\w45-uasrem\` (`l.ps1`): the q35 bases through
`launch-q35.ps1` (its pinned emulator), `usb-uas` with one `scsi-hd` LUN (an
overlay of the FAT test disk, serial `W45UASREMU`) on `qemu-xhci` port 3, a
blank 64 MiB `usb-storage` stick (`W45UASREMB`) on port 4, a `usb-tablet` on
port 2; the controller behind the launcher's hot-pluggable PCI Express root
port. Unplugs by QMP `device_del`; a hot-plugged `usb-uas` is not attached
until `qom-set /machine/peripheral/uas0 attached true`, after its `scsi-hd`
is added.

Binaries: the staged x86 package of `w45-adv` (2026-10-31; `src/uas` and
`src/hcd_pdo.c` identical to `b71199a9`'s), `xhci98.sys` `a8593f4f...`,
`xhciuas.sys` `944db682...`; the variant, `xhciuas.sys` built from
`b71199a9` with `uas_fdo.c`'s `SurpriseRemovalOK = TRUE` written FALSE and
nothing else, by the DDK alone outside the gated build (a reading aid,
never packaged), `f84866b4...`, copied over the installed file and read
after a cold start. Capabilities read from each devnode's Enum key
(`regedit /e` or `reg query`); screenshots by QMP.

| Leg | System | Result |
|---|---|---|
| install | 2000 (1 vCPU), XP, Vista x86, 7 x86 (2 vCPU each), fresh overlays | `INSTALL.BAT /console /yes /nopause`, exit 3, restart; `xhciuas` and `xhci98` started |
| capabilities, package | all four | UAS devnode `0x94` (Removable, UniqueID, SurpriseRemovalOK), its LUN `0x90`; the stick's devnode `0x14` on 2000 and XP, `0x54` on Vista, `0xD4` on 7, its disk `0x10`; the controller `0x0` on 2000 and XP |
| listing, package | 2000, XP (`w2k\b3`, `b4`, `xp\b3`, `b4b`) | the stick alone, with and without components; the UAS drive absent |
| listing, package | Vista, 7 (`vx86\b2`, `b3`, `w7x86\b2`, `b3`) | the controller and the stick; with components, the UAS drive under the controller only (as 45.52 read on 7 x64) |
| unplug without a stop, package | all four (`w2k\b5`, `vx86\b4`, `w7x86\b4`) | no notice for the UAS drive |
| the stick unplugged without a stop | 2000 (`w2k\b6`), XP (`xp\b6`) | 2000: "Unsafe Removal of Device" naming "USB Mass Storage Device" and "QEMU QEMU HARDDISK USB Device"; XP: none |
| capabilities, variant | all four | UAS devnode `0x14`; the rest as before |
| listing, variant | 2000, XP (`w2k\c2`, `xp\d1`) | the stick and "xHCI98 USB Attached SCSI Storage" with its disk and volume |
| listing, variant | Vista, 7 (`vx86\c3`, `w7x86\c3`) | the controller, the stick and "xHCI98 USB Attached SCSI Storage" |
| stop, variant | all four (`w2k\c4`, `c5`, `xp\d2`, `d3`, `vx86\c4`, `c5`, `w7x86\c4`, `c5`) | the confirmation lists the drive, its volume and its disk alone; "Safe To Remove Hardware: The 'xHCI98 USB Attached SCSI Storage' device can now be safely removed" (Vista: "This device can now be safely removed") |
| unplug after the stop, variant | all four (`w2k\c6`, `xp\d4`, `vx86\c6`, `w7x86\c6`) | no notice; plugged again, listed again |
| unplug without a stop, variant | 2000 (`w2k\c8`), XP (`xp\d6`), 7 (`w7x86\c9`) | 2000: "Unsafe Removal of Device" naming "xHCI98 USB Attached SCSI Storage" alone; XP and 7: none |
| unplug during a copy loop, variant | XP (`xp\e7`), 7 (`w7x86\c9`) | the copies fail ("The system cannot find the drive specified"), no hang, no notice |
| removal policy | 7 package and variant (`w7x86\e4`, `d4`), XP variant (`xp\e6`) | "Quick removal (default)" / "Optimize for quick removal" in each: unchanged |
| unplug during a copy loop, Explorer open on the volume | Vista: variant (`vx86\c9` to `c18`), package (`vx86\d5`, `d6`), the stick on `usbstor` (`vx86\e3` to `e5`) | the next copy blocked for good in all three, the volume still mounted, `shutdown` hanging (QEMU stopped); with no Explorer window, package, the copies failed with no hang (`vx86\d3`) |

Two side readings. On XP the first restart after the variant was copied in
(`shutdown -r`) left the devnode at `0x94` and the drive unlisted through
two replugs; a shut down and cold start read `0x14`: the update leg of the
code step reads the listing after the installer's own restart (record 13
section 17.5). And the Vista hang above is not UAS's and not this change's
(the stick shows it on `usbstor.sys`); it is handed to the coordinator.

Why TRUE: `4e66702` (31-A.2) set it when the bus's device PDOs said TRUE
too; `9001ebd` turned the PDOs' answer FALSE the same day for the hot-plug
applet and left the UAS FDO's override in place; no reason is recorded
anywhere (record 13 section 17.2).

### The change and its legs (the owner's option A, 2026-10-31)

`817dd191`: `UasFdoPnp` (`src/uas/uas_fdo.c`) no longer handles
`IRP_MN_QUERY_CAPABILITIES`; the query passes down and back unchanged, so
the bus's `SurpriseRemovalOK` FALSE stands (record 13 section 17.5).
Reviewed and revised. The gated x86 build of `817dd191` passed (`all`,
release and qemu, 22:17; `E:\xhci98-scratch\p45-w45-uasrem\gated-x86.log`,
release `xhciuas.sys` `A677A4CA...2486`, `xhci98.sys` `AD1BDD96...11C9`),
and so did the host suites (`ht.log`). The legs ran on the package staged
from that build by `make-release.ps1 -Arch x86` with a test label
(`xf\PNEW`), on four fresh overlays each first given the previous package
(`xf\PN`, the reading's, `xhciuas.sys` `944db682...`), with the same UAS
drive and Bulk-Only stick as the reading.

| Leg | 2000 (`w2kB`) | XP (`xpB`) | Vista x86 (`vx86B`) | 7 x86 (`w7x86B`) |
|---|---|---|---|---|
| the previous package installed, restarted | UAS devnode `0x94` | `0x94`, the stick alone listed | `0x94` | `0x94`; the controller and the stick listed (`b1`) |
| the update | `INSTALL /console /yes /nopause /force`, exit 3 | the same | `/force /restage`, exit 3 | the same |
| after the restart the installer asks for | shut down and cold start (this base's warm restart stops in real mode, `w2kB` tag d); the new `xhciuas.sys` in place (`fc`), `0x14`, the LUN `0x90` | warm restart; the new file, `0x14` | warm restart; the new file, `0x14` | the emulator's process ended during the warm restart (nothing in its output says why); read after a cold start: the new file, `0x14`. Again on a fresh overlay (`w7x86C`): the previous package, a warm restart (`0x94`), the update, a warm restart: the new file, `0x14`, the UAS drive listed beside the controller and the stick (`c1`) |
| listing | the stick and "xHCI98 USB Attached SCSI Storage" (`e1`) | the same (`e1`) | the controller, the stick and the UAS drive (`c1`) | the same (`c2`) |
| stop | the drive, "Generic volume (E:)" and its disk alone; "Safe To Remove Hardware" (`e2`, `e3`) | the same, D: (`e2`, `e3`) | the same (`c2`, `c3`) | the same (`c3`, `c4`) |
| unplug after the stop | no notice (`e4`) | no notice (`e4`) | no notice (`c4`) | no notice (`c5`) |
| plugged again | listed (`e5`) | listed (`e5`) | listed (`c5`) | listed (`c6`) |
| unplug without a stop, idle | "Unsafe Removal of Device" naming "xHCI98 USB Attached SCSI Storage" (`e6`) | no notice (`f2`, after a cold start on the same overlay; the new file in place, `0x14`) | no notice (`c6`) | no notice (`c7`) |
| unplug during a copy | an `xcopy` of `system32\*.dll` to the drive: "Delayed Write Failed" and "Unsafe Removal of Device", the copy ended, no hang (`e13`) | a copy loop: the copies fail ("The system cannot find the drive specified"), no hang, no notice (`e6`) | a copy loop with no Explorer window: the next copy blocked (`c8`, `c9`), the hang of record 13 section 17.6, which task 45.65 reads on Vista's own USB stack too | a copy loop: the copies fail, no hang, no notice (`c10`) |

The update path: the reading build and this one carry the same version and
`DriverVer`, so the installer read every device "current (this release)"
and left the old `xhciuas.sys` in place until `/force` (2000, XP) and
`/force /restage` (Vista, 7, whose driver store kept the stored copy of the
same `DriverVer`) were given; that is the installer's documented behaviour
for a development build of the same version, and a release carries a new
version. Once the new file was in, each system read `0x14` after the
installer's restart, a warm one on XP, Vista and 7 (`w7x86C`). The reading's XP devnode
that kept `0x94` across a warm restart (above) had its file copied in by
hand with no reinstall of the device; the installer reinstalls it
("Updating xHCI98 USB Attached SCSI Storage ... done"), and after that
the warm restart read `0x14` [the cause of the earlier reading not read].

### Owed

- Nothing of this task. The Vista hang is task 45.65's.

## 45.59 step S4: the firmware connection manager's host-to-host links (2026-10-31, code; legs owed)

Worker `w45-tbicm`, branch `w45-tbicm` from `1a54b1a3`. Design record 35
section 3.10 (as built, reviewed and revised before the code, three
rounds), sections 11.1 and 13 amended.

What was done:

- `src\cm\tbt_icm.c` and `tbt_icm.h`, section "Host-to-host links": each
  row's events `0x06` and `0x07`, the approve `0x10` and its teardown in
  Linux's `tb_msgs.h` layouts (Falcon and Alpine Ridge `icm_fr_*` and NHI
  mailbox `0x10` or `0x11` with data 1 then 2; Titan Ridge and Ice Lake
  `icm_tr_*` and command `0x11` in two stages); one `tbt_xdp.c` engine
  (S1, its firmware-CM mode, unchanged) per direct link, fed by ring 0's
  XDomain sink (S2); the service's operations `TBT_ICM_NET`, the same
  three signatures as S3's `TBT_U4_NET`; `TbtIcmSetNetConfig`,
  `TbtIcmSetNet`, `TbtIcmNetWanted`, `TbtIcmNetIdentity`,
  `TbtIcmSetNetSerial`, `TbtIcmPeer`.
- `src\cm\cm_nhi.c`: `TbtIcmSetNetConfig` after `TbtIcmInit` (`XhciCmNet`
  and the package version). The service's wiring under the firmware CM
  (`TbtIcmSetNet` beside S5's `TbtU4SetNet`, `TbtIcmNetWanted` and
  `TbtIcmNetIdentity` beside `CmNetPass`'s, and the NHI's Device Serial
  Number for `TbtIcmSetNetSerial` where the driver reads one) is S5's
  `cm_net.c`, not in this branch.
- `test\test_tbt_icm.c`: the `test_xd_*` vectors of record 35 section
  11.1 (a far host of `tbt_xdp.c`'s engine in the software CM's mode, the
  firmware model passing its packets), every row; `test\tbt_fake.h` logs
  the mailbox data. `test_tbt_icm`, `test_tbt_hw` and `test_tbt_select`
  (and `scripts\usb4-trace\u4-trace.ps1`'s `test_tbt_icm`) link
  `tbt_xdp.c`.

Builds and tests on `d9420766`, development host A: the gated x86
build (`scriptsuild-driver.cmd release`, its self-tests and every gate)
passed; `testun-host-tests.cmd` passed whole (`test_tbt_icm` 1047
checks, `test_tbt_hw` 176, `test_tbt_select` 306, `test_u4xd` 175, all
with 0 failures). The design was reviewed and revised in three rounds and
the code in nine, until nothing material remained.

Legs (record 35 section 11.3): owed. An xHCI98 guest on the emulator's
`tb3-alpine-ridge` and `tb3-titan-ridge` profiles against the Linux peer
of `scripts/run-xdomain-linux.sh` needs S5's service and network PDO
(`w45-tbnet`, not merged into `3.4.0.0` when this was written) wired to
the calls above; a discovery-only leg (the peer's properties read both
ways, no login) needs only this branch and `XhciCmNet` 1 set by hand.

## 45.59 S7 xhcinet6.sys, the NDIS 6.20 miniport for 8, 8.1 and 10 (2026-10-31, partial)

**Verdict: the code, its INF sections, its gates and its gated builds are done here;
the legs are owed.** `xhcinet6.sys` (`src\net\ndis6\`) is the NDIS 6.20 miniport of
design record 35 section 7.9 for 32-bit and 64-bit Windows 8, 8.1 and 10, over the
same framing core (`src\net\net_frame.c`) and interface glue (`src\net\net_cmif.c`)
as step S6's `xhcinet.sys`, both taken unchanged from S6's branch (`w45-tbndis`
`8febce8a`, merged). It builds with WDK 7.1's `WIN7` environment, x86 and x64, whose
`ndis.h` and `lib\win7\<arch>\ndis.lib` carry NDIS 6.20; no other kit in `tools\` was
needed. Its legs (record 35 section 11.3, the "10 x86 (or 8.1 x86)" and "10 x64 (or
8.1 x64)" rows) wait for step S5's network PDO in `xhcicm.sys` and the x64 leg kit
(S8a); nothing here has been loaded in a guest.

### The driver

- Registration: `NdisMRegisterMiniportDriver`, characteristics revision 2, NDIS 6.20.
  `MiniportInitializeEx` sets the registration attributes first (surprise removal
  accepted, internal interface, no check for hang), reads `XhciNetMtu` (1500, 1500 to
  65522), asks for the interface, sets the revision 2 general attributes (802.3, no
  offload, `MtuSize` the MTU, every packet filter, `PowerManagementCapabilitiesEx` with
  every wake-up and protocol-offload capability off and the minimum states
  unspecified), allocates the receive pool of net buffer lists and the power work item,
  then `Open`; the adapter stays Paused until `MiniportRestart`.
- Send: net buffer lists queued under one lock and served by one sender at a time,
  each net buffer framed into a reservation and committed, the list completed after
  its last net buffer; a full ring leaves it at the head until `TxDone`. Not Running,
  link down, service closed, D3 or removed: the queue completes `NDIS_STATUS_PAUSED`
  or `NDIS_STATUS_MEDIA_DISCONNECTED`. Cancel by id.
- Pause completes the queue paused and completes at once, or returns pending and the
  last sender or the last returned list calls `NdisMPauseComplete`. Reset touches
  nothing. A surprise removal ends sends at once.
- Receive: frames are reassembled whatever the state; a whole frame is indicated only
  while Running, from the pool, `NDIS_RECEIVE_FLAGS_RESOURCES` when it runs low; one past
  the maximum goes through the core's receive segmentation.
- Link: `NDIS_STATUS_LINK_STATE` from `Restart` on, one publisher at a time so the
  last view wins; 10 Gbit/s when the link's rate is unknown (record 35 section 7.5),
  `NDIS_LINK_SPEED_UNKNOWN` while disconnected.
- Power: `OID_PNP_SET_POWER` pended to an NDIS I/O work item: `Close` at D3, after which
  it waits until every send has completed, `Open` at D0.
- `OID_GEN_STATISTICS`' octet counters are 64-bit by carry under the lock (no 64-bit
  arithmetic).

### The INFs and gates

- `xhcinet.inf` and `xhcinet-amd64.inf`: the `.6.2` models hold `XHCICM\TBNET` with the
  install `Net6.Dev`: `xhcinet6.sys` as the one service `xhcinet`, `Characteristics`
  0x84, `BusType` 15, `*IfType` 6, `*MediaType` 0, `*PhysicalMediaType` 0, and the same
  `XhciNetMtu` value and Advanced entry as the NDIS 5 path. `check-net-inf.ps1`:
  `PATH-NT62` rewritten, `NETNT-IMAGE`, `NET6-KEYWORD` (the keyword table beside the
  value table); 51 self-test mutations, both INFs pass. `tuning.md`: rows for the three
  keywords.
- The import gate's `-GateProfile net6` (`xhcinet6-imports.allow`, `-amd64.allow`):
  x86 kernel and HAL pairs against Windows 7 SP1 x86's and 8.1's and 10's; amd64 kernel
  pairs against NT 5.2 amd64's and 8.1's and 10's; every `NDIS.SYS` pair against 8.1's
  and 10's `ndis.sys` and declared in WDK 7.1's `ndis.h`. The 8.1 and 10 x86 and x64
  `ntoskrnl.exe`, `hal.dll` and `ndis.sys` were extracted by the owner from the q35 base
  images (export names only; `net6-*-baselines.expected`, `legal-provenance.md`
  section 4). They showed that the 8.1 and 10 x86 HAL export `KfAcquireSpinLock` and
  `KfReleaseSpinLock` as forwarders to the kernel (Windows 8 itself was not read); the gate's export parser now takes
  a forwarded export as one, as the loader does. `memset` and `memcpy` are denied:
  WDK 7.1's x86 compiler had turned a byte loop into `memset`, which S6 fixed in the
  shared files (`8febce8a`) and this driver in its own.
- `build-driver.cmd :buildnet6` builds and gates it after `xhcinet.sys` in every x86
  and amd64 run. Not in the gated record or the source stamp, and not in the package:
  step S10.

### Builds

`scripts\build-driver.cmd all`, then `all -amd64`, on development host A at
`542b1812` (this branch with 3.4.0.0 `965f4642` merged), 2026-10-31: both **BUILD +
GATES PASSED (release qemu)**, every self-test and the host tests included
(`test_net_frame`: 2554 checks, 0 failures). `xhcinet6.sys` passed the net6 import
gate in all four images, against the 7 SP1, 8.1 and 10 x86 sets and the NT 5.2 amd64,
8.1 and 10 x64 sets, with no warning:

| Image | SHA-256 |
|---|---|
| `objfre\i386\xhcinet6.sys` | `a3ccc71c2351f28f53743b27cea6ba80301e68a9d8a14572c04a5f4fb4c9c103` |
| `objchk_qemu\i386\xhcinet6.sys` | `87721bff356791f7fc825bd1322f55186d0b404c365f54e087b3ce0869b948f0` |
| `objfre\amd64\xhcinet6.sys` | `2e578deb2d17b1aa91befeeec6dbe2d85ef2af8d1abac995fe07f7ef36a53b21` |
| `objchk_qemu\amd64\xhcinet6.sys` | `08631534b87451a609d7fd482c4076e04bc2f641d4cffb3b5c439673d12d5dbe` |

The first x86 run, at `cd314d75`, stopped at the qemu flavour of S6's `xhcinet.sys`
x86: `net_cmif.c`'s `IoSetCompletionRoutine`, an ASSERT in the Windows 2000 DDK's
checked headers, imported `ntoskrnl.exe!RtlAssert`, a pair with no Windows 98 evidence.
`net_cmif.c` now sets the three completion fields itself, as `src\hcd.h`'s
`HCD_SET_COMPLETION_ALWAYS` does (`2af48299`, reviewed, merged into 3.4.0.0 as
`965f4642`). The WDK 7.1 images keep the RtlAssert of WDK 7.1's checked
`IoGetNextIrpStackLocation`, which their allowlists admit for the qemu flavour.

### Reviews

Reviewed and revised: the miniport (the D3 drain, the link publication's order, the
64-bit octet counters), and the INFs, gates and build (the net6 profile under
`-Arch auto`, the header through `WDKROOT`).

### Owed

- The legs on 8.1 or 10, x86 and x64 (record 35 section 11.3), after S5 and, for x64,
  the leg kit (S8a).
- Packaging (S10) and the installer (S11).

## 45.53 and 45.55 (continued): the chain port's Code 12, the first wizard on 2000 and XP, the remaining systems (2026-10-31, partial)

Worker w45-pcie2, development host A, continuing the section above. Code
at `4b44bda2` (the one driver change below): the gated build `all`
(x86), "BUILD + GATES PASSED (release qemu)"
(`E:\xhci98-scratch\p45-w45-pcie2\build-all.log`), and `all -amd64`,
passed (`build-amd64-all.log`); host tests passed (`htest1.log`,
`test_cm_breq` 17170 checks). Packages (`make-package.ps1`): release x86
`xhcicm.sys` `430e939f...`, `xhci98.sys` `1e46cee6...`, `stor98.sys`
`5d75bec5...`; qemu x86 `xhcicm.sys` `47f527be...`, `xhci98.sys`
`4805fb8e...`; release x64 `xhcicm.sys` `4d220403...`, `stor98.sys`
`781b464c...` (unsigned; its catalogs made unsigned for the leg); the
installer and `xhcimark` built from this tree (`XHCIINST.EXE`
`6ec2ee97...`, `XHCIMARK.EXE` `7a4a46ce...`). Emulator: `bin-55cadf9` as
above. Evidence: `E:\xhci98-vm\p45\w45-pcie\` (leg folders below;
`hashes-xf4.txt`, `hashes-xf5.txt`).

### `usb4-emu`: the retrain qtest on the new default

With the tunnel ports' new default (no Power Controller),
`/usb4-retrain/guest-off`'s Slot Control power-off has no effect, as the
PCI Express Base Specification 4.0 section 7.5.3.10 has it, and the case
waited for a removal that never came; on Windows the qtest socket's 50 s
receive timeout ended the binary ("Broken pipe"), so cases 6 to 10 never
ran (the section above's "retrain 5" was that truncation). The model is
right; the case now runs with `power-controller=on`, and
`/usb4-retrain/guest-off-no-pc` writes the same power-off on the default
port and checks that the held endpoint stays and is retrained with the
same generation (`usb4-emu` branch `w45-pcie2` `23c831e`, fork
`af059ed`; reviewed and revised). Every suite counted against its TAP
plan: host 43, tunnel 31, icm 40, icm-tr 16, icm-icl 5, harness 25,
fault 9, asm4242 16, asm2464 4, tb3dev 12, tbhost 9, root-hotplug 22,
usb3 9, retrain 11, xdomain 6: 258 of 258.

### 45.55 on XP: the chain port's Code 12 (this driver's, fixed)

Leg `xpC` (XP golden, `-Usb4 asmboard`: the board's root-port
reservation, bus 103, 776 MB, 24 KB I/O, 4 MB prefetchable, and the
firmware's split, so tunnel port 0 got buses 3-50, 12 KB I/O and 384 MB,
`infopci-0.txt`). The parent and its child plugged at the desktop: the
parent's switch upstream port `104C:8232` and its first downstream port
started, the second (the chain port, 04:08) stopped at Code 12 as before
(`setupapi`: "required reboot: Device has problem: 0x0c"). The connection
manager's log had `cm.breq.how=00004102` for both downstream ports:
outcome 4, the shape test refused the lists. Their `BasicConfigVector`
(`reg-1.txt`, decoded) was XP's closed list of record 33 section 2.1
followed by one more descriptor, the bridge's own interrupt
(`ShareDisposition` 3, vectors 0 to FFh; `ListSize` 288h, 19
descriptors): the switch ports' Interrupt Pin is A, as the captured
ASM2464 enclosure's switch ports' are, where 44.63's dock had none.
Revision 3 refused any interrupt, so the rewriter never ran on such a
switch. Record 33 revision 4 (`4b44bda2`, reviewed and revised) admits
exactly that descriptor after either template and keeps it last in the
rewritten list.

Leg `xpD` (the same golden, the package of `4b44bda2` installed over it
with the layout present, the qemu flavour copied in, restarted): the
parent and child plugged; `cm.breq.how=0100111x` (rewritten, the
interrupt kept, bit 24) for 03:00, 04:00 and 04:08, every start status
0; after the mark service's restart the two downstream ports had windows
of their own (`infopci-2.txt`: 192 MB each), the chain port started
(code 0), both NVMe on Stor98, D: and F: ORICO, `fc /b` of a copied file
on each: no differences.

### The first wizard (2000 and XP): why the drive had no driver at first

Read on `xpC`, `w2D` and `w2E` (2000, `-Usb4 adl`, installed with the
layout present, cold start). A drive behind a never-seen enclosure is
enumerated before the connection manager's filter chain reaches it (the
bridges' first-time installs take the kernel-attached filter away,
record 33 section 4.1), so its first ids lack `PCI\XHCI98_TUNNEL`, and
Windows' wizard finds no driver for "PCI Device". The mark service then
writes the bridges' marks, waits while that installation is pending, sets
the drive's install to run again (`flags 00000040 -> 00000020`) and
restarts the enclosure's bridges; the drive comes back with the tunnel id
and a second wizard offers "Stor98 Storage Controller for NVMe SSDs"
(`oem*.inf`). On 2000 this holds even when the first wizard is finished
with its default, "Disable the device" (`w2E`: ConfigFlags 40h, not a
disabled device; `w2E\mark-tail.txt`): the second wizard installed
Stor98 and E: ORICO came up. XP's first wizard defaults to "Don't prompt
me again" (the same flag), with the same outcome (`xpD\mark-tail.txt`).
w45-pcie's `w2C` stopped at the first wizard and ran the installer again;
nothing here is this driver's to fix. A drive's first plug thus shows two
wizards on 2000 and XP. The release-notes text for 45.53 and 45.55 is
handed over again with this work and replaces the one in "45.final" (no
driver change there, the installer run again on 2000, the XP chain port
unresolved), which these readings supersede.

### 98 SE: the connection manager was not running

The 98 SE golden's install had not reached the ASM4242 router (`w9C`,
the golden's `C:\XHCIINST.LOG`: "PCI Universal Serial Bus - The helper
could not install it: not reached in this run"; Enum: no driver,
ConfigFlags 40h), so `w9B` had no connection manager. The installer run
again ("xHCI98 USB4 Router (ASMedia ASM4242): xhcicm ... started"), the
qemu flavour copied in, cold start, the parent and child plugged: the
connection manager numbered the parent's switch (`cm.brnum.9x`, buses
4-14, the chain port 6-14), the Add New Hardware wizards installed the
bridges and Stor98 for both drives (Enum: both `Stor98 Storage
Controller for NVMe SSDs`, `SCSIAdapter`), E: Orico (the second drive, a
copy of the same disk image, shows no letter, as on 7 x86). Pass.

### The other systems

| Leg | System | Layout | Read | Verdict |
|---|---|---|---|---|
| `xpD` | XP | `asmboard`, parent + child | above | pass |
| `w2E` | 2000 | `adl`, ORICO | two wizards, Stor98, E: ORICO | pass |
| `w9C` | 98 SE | `asm4242`, parent + child | above | pass |
| `vA` | Vista x86 | `adl`, ORICO | fresh overlay, installed with the layout present: Stor98, D: ORICO, `fc` no differences | pass |
| `vB` | Vista x86 | `asmboard`, parent + child | both switches' ports code 0, both NVMe on Stor98, D: and F: ORICO, `fc` on both | pass |
| `mA` | ME | `adl`, ORICO | fresh overlay; the installer reached the router through Windows' wizard after the restart; the plug: bridges and Stor98 by the wizards, E: ORICO | pass |
| `mB` | ME | `asm4242`, parent + child | installed (the router's xhcicm started), qemu flavour copied in; the cold start stopped at a black text screen (the debug log's last line `cm.callwk.open`), the next start went to Safe mode after ScanDisk and reached no desktop in 20 min | not read (owed; rule out the emulator on a fresh overlay first) |
| `x6A` | XP x64 | `adl`, ORICO | fresh overlay, the x64 package with unsigned catalogs, every unsigned-driver prompt answered: "ASMedia 246x connected", Stor98, D: ORICO, `fc` no differences | pass |
| `x6A` (c) | XP x64 | `asmboard`, parent + child | the same overlay restarted on the ASM4242 layout; the installer's run under the unsigned-driver prompts did not finish in 20 min | not read (owed) |

### Left

- 45.55 on ME (`asm4242`, the boot after the install) and on XP x64 (a
  signed or prompt-free install on the ASM4242 layout).
- 2000 on the ASM4242 layout with the parent and child (`w2B`'s
  transfer-disk problem): not run again, so 2000's chain is not verified;
  record 33 revision 4 takes 2000's trailing interrupt to be XP's form,
  not read, and a 2000 list that differs is still refused. The leg owes
  the bridges' lists (`BasicConfigVector`), the `cm.breq` outcome and both
  drives after the mark service's restart.

## 45.final: the release-notes texts the tasks handed over

Collected as each task merges, for `45.final` to place; the wording is the
task's own, checked against its section above before it is used.

- 45.6 (merged at `312b8421`): "The free and non-free rtl815x USB Ethernet
  drivers may now be staged side by side on Windows XP to 7, 32-bit and
  x64, where their INFs list different chips; the release checks they
  share no device, keeps them excluded on 98 SE, ME, 2000 and 8 to 10, and
  keeps them apart from an earlier staging of either taken from another
  release. The free set is usb-wdm-free 0.1.5: uac2 0.1.5.0 (each output
  and input its own sound device; a co-installer, installed by hand, that
  keeps Windows' USB Audio 1.0 driver off a UAC 2.0 device on 2000 to 7)
  and usbcom 0.1.5.0 (FTDI FT-X, FT232H, FT4232H, Prolific PL2303, Silicon
  Labs CP2105 and CP2108 on 98 SE and ME); its rtl815x is still copied
  only, not staged."
- 45.45 (merged from `w45-onebuild` at `ac2f5b4f`): "**One build.** The
  download no longer has `debug-x86` and `debug-x64`: every driver has one
  build for everyone. On 32-bit Windows it keeps the debug build's frame
  pointers, and it carries the debug build's command-state checker. Before
  each command the driver gives the controller, the checker reads the
  device's state and counts what a controller following the xHCI
  specification to the letter would refuse; the counts are in the xHCI98
  USB Tree's snapshot. It changes nothing the driver does; set
  `XhciCommandCheck` to `0` to turn it off (tuning.md). `INSTALL /debug`
  and `SETUP /debug` are gone with the folders."
- 45.47 (merged from `w45-dockxhci` at `5aa76156`; the known issue only
  where the mark service is not installed - no Thunderbolt 3 / USB4
  component, or safe eject off - since with it `xhcimark` starts the
  controller and no restart is needed (record 21 section 7.6.4); the Vista
  leg is still owed): "Windows Vista and 7, 32-bit: the first time a
  Thunderbolt dock with its own USB controller is plugged in after the
  install, that USB controller may show 'This device is not configured
  correctly (Code 1)' until Windows is restarted once. Restart Windows;
  the dock works from then on, also when plugged in again."

Also for `45.final`, outside the texts above (left by 45.45's follow-up,
`05f19a47`, because they describe the published `3.3.0.0` download):
`README.md`'s download list still names `debug-x86` and `debug-x64`, and
the release notes' installer switches still describe `/debug`; both go at
the version bump.
- 45.15 (the chained unit's stop, deferred teardown (owner, 2026-10-30); a
  draft for 45.final to check against record 34 section 3.10 and the
  systems the chain runs on): "Windows 2000 to 10, a drive behind a device
  chained behind a dock (USB4 connection manager): stopping it stops the
  drive, but its tunnel stays up until the unplug, so a rescan (Scan for
  hardware changes) can find it again; unplug it after the stop."
- 45.20 (merged from `w45-u4v2c` at `4be5edf5`; the worker's text, its
  "read on the emulator only" dropped by the owner's rule; also a registry
  row for `XhciCmUsb4V2` (written by the install after 45.50) and an
  "Untested ground" row: USB4 version 2 and Gen 4 links on real hardware
  (45-E.10); "Thunderbolt 5" not claimed before that bench reading): "USB4
  version 2 routers (Barlow Ridge, say) are run as version 2: Gen 4 links
  at 80 Gbit/s, symmetric or asymmetric (120/40), used as the router
  brings them up, with Extended Encapsulation and the enhanced time sync;
  `XhciCmUsb4V2` 0 runs them as version 1, as in 3.3.0.0."
- 45.40 (merged from `w45-rtl` at `7fcc6ff4`; the hub-port extension
  (owner, 2026-10-30) still to come, which drops "behind a hub" from the
  not-covered list; "reproduced on XP and 7" left out as a test label): "A
  device with a serial number that the driver enumerates again at a root
  port (after a link drop, a recovery or a replug) is no longer renamed
  when its serial number cannot be read at that moment: the driver reads
  it again for a few seconds, and if it still cannot, leaves the device
  out until the next connection. Before, Windows took such a device for a
  new one, and a network adapter came back as a new connection with
  automatic (DHCP) settings, losing its static address and possibly taking
  the default route (seen with a Realtek RTL8157 on XP). Devices without a
  serial number that come back on another port are not covered."
- 45.15 (2000) (merged at `f5e4ee86`; with the chained stop text already
  above): "On Windows 2000 a drive daisy-chained behind another device
  that is plugged in while Windows runs is found but does not start (Code
  10, the Standard PC HAL); one chained at power-on starts."
- 45.46 (merged at `4f092103`; also drop "by the installer" at
  release-notes.md 279-282 and ", the name SETUP.EXE gives it" in
  README.md's router row): "The ASMedia ASM4242's USB4 router is named
  `xHCI98 USB4 Router (ASMedia ASM4242)` by the INF's own line, a hand
  install included; an update clears the name 3.3.0.0's installer wrote."
- 45.50 (merged at `4f092103`, for "Registry settings"; the CM values
  table's "absent" and "not written" cells for XhciCmBridgeReq,
  TunnelBridge, AsmediaPcie7Credits, RingInterrupt and Usb4V2, and the
  release notes' "which no INF writes" (~3834), to change with it): "From
  3.4.0.0 the install writes every value the drivers read at its default,
  in each controller's (or the Thunderbolt/USB4 controller's) own key,
  only where it is missing, so a value you set survives an update;
  XhciCmTunnelBridge is written as `auto` (the driver finds the tunnel
  ports itself). Stor98's install writes its settings too: each value on
  98 SE and ME, and one DriverParameter on 2000 to 10, kept whole where
  you wrote one."
- 45.18 (7b7a63a0): "- "From 3.4.0.0, `xhcicm.sys` also tells Stor98 at
  once that a tunnelled drive is gone when the drive sits directly behind
  a USB4 tunnel port that is a PCI Express port, on Windows 98 SE, ME,
  2000, XP and XP x64, on systems whose tunnel ports have been read for
  it. A drive behind a dock or behind another drive, Vista and later on
  such a port, and every other system get no such statement; there Stor98
  keeps its own deadline of about 5 seconds." - "On Windows 98 SE and ME
  an NVMe drive that sits directly on a Thunderbolt or USB4 tunnel port,
  with no PCIe switch of its own in between, is now given the port's
  interrupt and starts, where it used to stay at Code 29 ('the BIOS for
  the device did not give it an IRQ').""
- 45.15 (HEAD): "- (45.15, daisy chains, 2026-10-31) "Daisy chains behind
  a USB4 host using the software connection manager: a device plugged into
  the downstream port of a USB4 device is connected through its parent.
  Its own PCIe tunnel comes up after the parent's, and a drive in it
  appears beside the parent's. Unplugging the end device closes its tunnel
  and keeps the parent's drive. Unplugging the parent removes the chained
  device first. A chain present at startup is kept as the firmware built
  it. Behind a Thunderbolt 3 parent, a link that cannot be bonded is kept
  as two single lanes. On Windows 2000 with the Standard PC HAL, a drive
  in a chain plugged in after startup gets no interrupt (Code 10).""
- 45.52 (HEAD): "- (45.52, UAS pass-through and SMART, 2026-10-31) "USB
  Attached SCSI drives now answer the SCSI and ATA pass-through requests
  and the SMART requests Windows 2000 to 7 send, so SMART and
  drive-information tools can read a drive whose USB bridge passes ATA
  commands through (SAT). The requests run in order with the drive's other
  I/O, can be cancelled, and end cleanly when the drive is removed. 32-bit
  tools work on 64-bit Windows. On Windows 98 SE and ME, ASPI tools reach
  the same drives through the existing storage stack. A bridge without SAT
  returns the standard refusal, as on Windows' own USB storage driver.""
- 45.62 (pending): "- (45.62, coordinator note for the bump; owner,
  2026-10-31) The release notes' "Known limitation: a drive attached
  during the install" text: narrow "until Windows restarts or the drive is
  plugged in again" to the re-plug alone (on XP and 2000 a restart did not
  list it, 45.27's second session), unless 45.62's fix lands first."
- 45.54 (HEAD): "- (45.54, installer Advanced lists, 2026-10-31) "The
  installer's Advanced lists now offer every setting a user may change.
  The USB tab adds XhciLinkKeep, XhciResumeKeep, XhciUsb2Bos, the
  interrupt moderation interval (XhciImodInterval250ns), the driver's log
  (XhciLogVerbosity, XhciLogDebugView), the polling periods
  (XhciPollIdleMs, XhciPollActiveMs), the hub first-report waits
  (XhciFirstEnumWaitMs, XhciFirstEnumPortMs), the largest UAS command
  (XhciMaxTransferKB, a multiple of 4) and XhciWideMap (Windows 2000 to
  7). The Thunderbolt tab adds XhciCmRingInterrupt (Windows 2000 to 10)
  and XhciCmUsb4V2. A value is written machine-wide only when you change
  it. On Windows 98 SE and ME a first install now lists the USB settings
  and asks XhciSilentInstall, where it showed one greyed row before.""
- 45.56 (HEAD): "- (45.56, Gen 4 Link Recovery, 2026-10-31) "USB4 version
  2 links: a Gen 4 link between the computer's USB4 controller and a USB4
  version 2 device now recovers from a link error in place, with the
  devices behind it kept, instead of being dropped and connected again.
  After each recovery the driver checks the device's PCI Express and USB
  connections and removes only one that did not come back. The new value
  XhciCmGen4LinkRecovery, written at 1 by the install, restores the
  previous behaviour when set to 0." The release notes' value table also
  gets a row for XhciCmGen4LinkRecovery: written as 1; "leave it at 1; 0
  takes a link error as an unplug and fresh plug, as before"."
- 45.53 (HEAD): "- (45.53 and 45.55, 2026-10-31; no driver changed, so
  whether the first sentence belongs in the notes at all is the bump's
  call) "A drive behind a USB4 host's own PCIe tunnel port, plugged in
  while Windows 98 SE, XP or 7 runs, gets its drive letter without a
  restart." - Known issue (45.53): "On Windows 2000, an NVMe drive plugged
  into a USB4 or Thunderbolt port while Windows runs can be left without a
  driver; run INSTALL.BAT again to install Stor98 on it." - Known issue
  (45.55): "On Windows XP, a USB4 device that has its own downstream port
  (a dock or a daisy-chainable enclosure), plugged in while Windows runs,
  may not show its own drive or the device chained behind it.""
- 45.60 (HEAD): "- (45.60, the installer's Thunderbolt and USB4 page,
  2026-10-31) "The installer has a Thunderbolt and USB4 page after the
  Components page, shown when Thunderbolt 3 / USB4 support is ticked. It
  holds the parts that come with that support: closing PCI windows above 4
  GB (XHCIPFW), Stor98 and its large transfers on Windows 98 SE and ME. It
  also holds the choice of what happens when a device is plugged in: give
  it a PCI Express connection at once (the default), or give it none
  (XhciCmApprove 0). That choice used to be on the Settings dialog's
  Thunderbolt tab. With "Give it none", a device plugged in gets no new
  PCI Express connection and may still work over USB, and connections
  already made when Windows starts stay unless XhciCmPreOsCleanup ends
  them." (The networking box joins the page with 45.59.)"
- 45.62 (HEAD): "- (45.62, SUPERSEDES the 45.62 coordinator note above,
  2026-10-31) With 45.62 merged, on 2000 and XP the known limitation "a
  drive attached during the install is not listed ... until Windows
  restarts or the drive is plugged in again" holds as written (a restart
  lists it): keep the text, do not narrow it. Vista and later were not
  read. XHCITR's mark line now reads "the Thunderbolt driver has answered
  it as removable", with "Windows has not recorded it as removable."
  beside it when Windows has not."
- 45.61 (HEAD): "- (45.61, a UAS drive in Safely Remove, 2026-10-31) "A
  UAS drive is now listed in Safely Remove Hardware on Windows 2000, XP,
  Vista and 7 and can be stopped on its own, as a Bulk-Only stick can; on
  Windows 2000, pulling it without stopping it first shows Windows'
  'Unsafe Removal of Device' notice, as it does for a stick.""
- 45.59 (HEAD): "- (45.59 S4, draft for the networking entry, 2026-10-31)
  "Thunderbolt networking also runs through the Thunderbolt 3 firmware
  connection manager (Falcon, Alpine and Titan Ridge, and Ice Lake's
  integrated controllers): the host-to-host link is found from the
  firmware's own announcements, and its two paths are built and torn down
  by the firmware on this driver's request. One link carries the service
  at a time; a peer behind a dock or another router is not supported.""
- 45.59 S7, draft for the networking entry (dae3b755): "Thunderbolt
  networking on Windows 8, 8.1 and 10 (32-bit and 64-bit) uses a separate
  network driver, xhcinet6.sys (NDIS 6.20), installed by the same INF as
  xhcinet.sys. It has no offloads and its default MTU is 1500, settable up
  to 65522 in the adapter's Advanced page. It is only present where
  xHCI98's connection manager drives the Thunderbolt controller."
- 45.53 and 45.55 (8ab07b3e): "SUPERSEDES the 45.53 and 45.55 text above.
  A Thunderbolt or USB4 dock or device whose own switch has more than one
  downstream port, plugged in while Windows 2000 or XP runs, now gets
  every port placed when its switch ports request their own interrupt, as
  real enclosures' ports do; before, the second port stopped with Code 12
  and nothing behind it started. The first time an NVMe enclosure is
  plugged in on Windows 2000 or XP, two Found New Hardware wizards appear:
  the first finds no driver (finish it either way), and after the
  safe-removal service restarts the enclosure's bridges a second one
  offers 'Stor98 Storage Controller for NVMe SSDs'; follow it to the end.
  On Windows 98 SE and ME the installer installs one controller per run:
  if it reports a controller 'not reached in this run', restart Windows
  and run it again, or the Thunderbolt/USB4 router has no driver and no
  device behind it is found."
