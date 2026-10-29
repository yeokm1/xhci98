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
file; one with URBs pending across the sleep is still dropped.** The
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
| 8 | cold starts a, b (RW), c (W) | the step's close of the root port before the PCI driver's probe at every start (trace lines 31 034 against 31 584; c 446 against 511); the Start screen; `XhciPfwRole` 2, `XhciPfwMode` 2 (inside `KeIpiGenericCall`), `XhciPfwBuses` 0 to FFh, `XhciPfwClosed` 9, `XhciPfwSkipped` 0 (no "late" bit), the hold about 70 ms of TCG time; every ASM4242 and enclosure bridge at problem 0 (only the NVMe, with no driver on 8, and `fw_cfg` at 28) |
| 8 | the installer again (update over itself, 4G-off) | exit 3; the entry and the service kept as they were; the step closed 9 at that start |
| 8 | the image deleted by hand, the entry left | the start stops: "Automatic Repair: Windows couldn't load correctly" offering System Restore; System Restore returned a working start, the package's registry rolled back to before its install (its restore point; Startup Repair's log: root cause "unspecified changes to system configuration", repair action System Restore) |
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
