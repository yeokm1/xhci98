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
