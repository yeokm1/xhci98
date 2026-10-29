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

## 45.11 the kept kernel child: the prototype read under Driver Verifier (2026-10-29, DRAFT, partial)

DRAFT: the readings below are taken; the reading matrix is not complete,
the design section is not written and nothing here is decided. Record 31
section 9.3 items 11 and 12. Runtime, development host A, QEMU under TCG,
15:11 to 18:28.

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
| Uninstall (`INSTALL /console /uninstall /yes`) | every kept child destroyed through its callback, the bridge filters' callbacks inside their removals, the driver unloaded; then bug check 0xC4 (0x62, ..., 1): one pool allocation of `xhcicm.sys` not freed at its unload. Whether the prototype's or earlier: not read (owed: the same with the kept child off and with `3.3.0.0`'s `xhcicm.sys`) |

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

### 8 x86

The prototype with Verifier booted with the enclosure; the drive tagged
and bound to Stor98 by Windows (event 400 `oem3.inf`). The matrix is
owed.

### Owed

The 8 and 7 x86 matrices; the unload leak's owner; the order of
`FastIoDetachDevice` with ACPI's bus filter (no child above an ACPI
filter on these q35 topologies); 2000, XP and 10 (record 31 section 9.3
item 11); the design section and its review; the owner's decision.

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
