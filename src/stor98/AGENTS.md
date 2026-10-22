# AGENTS.md

> Since 2026-10-19 Stor98 lives in the xhci98 repository as `src/stor98/`; xhci98's commit rule applies to it (no attribution trailer in commit messages), overriding "Commits" below, and the rest of this file still governs Stor98's technical rules.

Guidance for AI agents and contributors working in this repository.

## What this is

Stor98 (service and file names `stor98`): a SCSI(port) miniport for NVMe SSDs on Windows
98 SE through Windows 8.1 (32- and 64-bit; 8 and 8.1 since xhci98 3.2.0.0, xhci98's roadmap
43.23, README "Windows 8 and 8.1"), robust against surprise removal behind USB4 /
Thunderbolt PCIe tunnels. Stor98 is meant to be used with xhci98 only; it is not meant to
be used standalone, and docs must not present it as a standalone driver. Read
[README.md](README.md), then [docs/PLAN.md](docs/PLAN.md), then
[docs/TESTING.md](docs/TESTING.md).

Do not put "NVMe" in a product, file, service or display name (NVM Express trademark
guidelines); descriptive use ("for NVMe SSDs") is fine. The dated records (`docs/gates/`,
`docs/reviews/`, `docs/INTEGRATION-0.2.md`) and the shipped release folders keep the names
they were written with; do not rewrite them. Apart from those, the rename record
(`docs/RENAME-STOR98.md`, `scripts/rename-to-stor98.py`) and the compatibility note in
`include/stor98cm.h`, do not mention an earlier project name.

## Ground rules

- **Provenance first.** This repository will become public.
  - `src/` is SweetLow's nvme9x/nvment code, BSD-3-Clause (SweetLow, 2026-10-14; see
    `NOTICE`). Files keep both copyright lines (Dominik Behr; SweetLow).
    Keep `third_party/nvme2k-1.2.0.0/` unmodified (reference copy).
  - Every file derived from nvme2k keeps Dominik Behr's BSD-3-Clause copyright line,
    conditions and disclaimer, plus an `SPDX-License-Identifier: BSD-3-Clause` tag.
  - New files are BSD-3-Clause too (`SPDX-License-Identifier: BSD-3-Clause`, copyright
    Yeo Kheng Meng plus any contributor). The whole repository is BSD-3-Clause; see `LICENSE`.
  - Never commit Microsoft DDK/WDK headers, libraries or binaries (`SCSI.H`, `SRB.H`,
    `scsiport.lib`, ...). Builds find them in the git-ignored `tools/` folder
    (see [docs/BUILD.md](docs/BUILD.md)).
- **Surprise removal is a normal event, not an error path.** All-ones MMIO or config reads
  mean the device is probably gone: enter `gone`, stop submitting and fast-fail new
  requests, then fail the requests already issued with a status that ends retries once DMA
  into their buffers is known to have stopped (docs/PLAN.md 0.10, 2.2), or at the gone
  deadline under the one exception below. All-ones reads are never that knowledge by
  themselves. Never spin on hardware status; no stall loop may exceed the stall budget of
  docs/PLAN.md 0.10.
- **Never complete a request while the hardware may still DMA into its buffer.** Two
  different proofs exist (docs/PLAN.md 0.10): *command-level quiescence* (that command's own
  completion entry has been consumed) releases that one command; *controller-wide
  isolation* (acknowledged disable, CSTS.RDY=0 with CC.EN=0, or xhci98's CM stating the
  link went down, sent only after the path is closed and the endpoint reset has completed)
  releases everything. An NVMe Abort completion is neither. Generation tracking only
  discards stale completions; it does not stop DMA. A parent whose required work was
  suppressed by abort, reset or removal never completes with success, and its issued
  children still quiesce first (docs/PLAN.md 0.10, parent cancellation). The only exception
  to this rule is the gone deadline (decision MD-1, below).
- **After reconnect, trust nothing until the controller is shown fresh** (CC.EN=0 and
  RDY=0). A controller still enabled with old queues is an isolation-contract violation:
  log it, disable and wait for RDY=0 before reusing anything (docs/PLAN.md 0.10).
- **PCI config space only from permitted callbacks** (NT: `HwFindAdapter` and
  `ScsiSetRunningConfig`; 9x: per gate G9), with returned lengths checked (docs/PLAN.md
  3.4).
- **Never free or hand back memory the device can DMA into without isolation evidence**
  (docs/PLAN.md 0.10). Without the evidence, quarantine it: keep it owned and out of use.
  If a port frees it anyway, that is a feasibility-gate failure (docs/PLAN.md 0.0), not an
  accepted risk.
  - **The one exception (decision MD-1, maintainer, 2026-10-14):** when presence is lost
    and no isolation evidence arrives by the gone deadline (a tunable, default 5 s,
    `gone=` on NT, README "Settings"; evidence arriving earlier always wins), the held SRBs are completed with the
    terminal device-gone status, so the callers' buffers go back to the OS. Driver-owned
    DMA memory is still quarantined, never freed. The event is logged and counted. The
    owner accepted the residual risk: if the all-ones reads were not caused by a real
    link-down, late DMA may corrupt pages the OS reused. Do not widen this exception to
    any other path.
  - Maintainer decisions are recorded at the end of docs/PLAN.md; do not implement an
    option there that is not marked decided.
- **Decode the namespace correctly.** FLBAS selects an LBA format; the block size comes
  from that format's LBADS. Never misreport it.
- **4 KiB-sector drives must work, not be refused** (maintainer decision). The OS always
  sees 512-byte sectors; the driver translates, with read-modify-write for unaligned writes.
  See docs/PLAN.md, "4 KiB-sector namespaces".
- **One source tree, all targets.** Windows 9x-specific code (VMM calls, PTE handling) is
  confined to the 9x build; amd64 uses compiler intrinsics, no inline assembly.
- **Every fix gets a test in the emulator** (see docs/TESTING.md), including the 4 KiB
  sector, surprise-removal-during-I/O and tunnel-reconnect cases.

## Toolchains

The repository is self-contained: every kit lives in the git-ignored `tools/` folder
(`tools/MSVC600`, `tools/ntddk`, `tools/WinDDK71`, `tools/w98ddk/inc`) and the build never
reaches into another checkout. MSVC 6.0 with the Windows 2000 DDK for 32-bit targets (the 9x
build also takes the Windows 98 DDK's `SCSI.H`/`SRB.H` from `tools/w98ddk/inc`); WDK 7.1
(7600.16385.1) `x64 WNET` for amd64. Build with `scripts\build.cmd`; see
[docs/BUILD.md](docs/BUILD.md). The driver binaries are `stor98.mpd` and `stor98.sys`, not the
upstream names (licence clause 3).

## Releases

Since xhci98 3.1.0.0 (owner, 2026-10-19) Stor98 has no releases of its own: xhci98's
gated build builds and checks it from this source, it carries the xhci98 package's version
and date (`src\stor98ver.h` includes xhci98's `src\xhci_version.h`; the INFs' `DriverVer`
literals are held to it by xhci98's `scripts\inf-gate\check-stor98-inf.ps1`), and xhci98's
packager bundles it and signs the amd64 image with xhci98's key, in the same step as
xhci98's own drivers: SHA-1, no timestamp, no catalog, INFs unchanged (docs/BUILD.md,
"Releases" and "Test signing"). Stor98 has no certificate or signing script of its own any
more. The interface versions in `include/stor98cm.h` are protocol versions; never tie them
to the package version. `releases/0.3.0.1/` and `0.4.0.0/` are write-once history; add no
folder there ([releases/README.md](releases/README.md)); `releases/history.md` gets the
entry for each xhci98 version that changes Stor98. Agents never sign, never create a key and
never import a certificate into any store.

## Commits

Small, building commits. Messages end with the attribution line the session requires.
