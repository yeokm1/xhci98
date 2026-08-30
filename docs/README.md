# Documentation

The documentation is grouped by reader intent. Repository-qualified paths in
source comments, scripts, and other docs use these same locations.

## Using

- [Release notes](using/release-notes.md) - what the driver does, what it does
  not, and its known limitations.
- [Release acceptance test](using/release-acceptance-test.md) - the fixed
  procedure a stranger runs against a published release on a machine this
  project has never seen: nine steps, the longer ones a checklist table of
  numbered substeps, each with its expected reading, where that reading was
  observed, and what to do when it does not appear. Equipment is named by
  property, with a table of measured units suggested behind it. Re-run
  unchanged per release. Unlike the run sheets under Contributing it carries no
  hypotheses and no branches. It is a test, not an investigation.

## Contributing

- [Roadmap](contributing/roadmap.md) - the project-status index: what each
  phase was for, its status, what it delivered, the task and batch ids other
  files cite, and the two acts outside the phases (the upload and the
  hand-run acceptance).
- [Source files](contributing/source-files.md) - what every file in `src/` is
  for, and which belong to the host-tested pure core.
- [Architecture](contributing/architecture.md) - component boundaries and data
  flows.
- [Build and test](contributing/build-and-test.md) - toolchain setup, builds,
  VMs, installation, debugging, packaging, and recovery.
- [Phase 11 VM run sheet](contributing/runs/run-11v.md) - ordered validation
  stages and their pass readings.
- [Batch 13-E run sheet](contributing/runs/run-13e.md) - the E460 bench trip
  (Windows 98 SE on real xHCI silicon), stage by stage, with what cannot be
  verified once you are at the machine. There is no Windows 2000 bench run
  sheet: the batch it would have served, `13-T`, closed unobserved on Phase
  13's published-limitation branch, and what it established is in Phase 13's
  checkpoint and in `docs/using/release-notes.md`.
- [Test equipment, as measured](contributing/test-equipment.md) - every hub and
  device held for the Phase 13 trips, characterised on the development host:
  TT class, speeds, interface lists, `bInterval`, socket maps, and the three
  traps this particular equipment set contains. Also the requirements each
  Phase 13 clause set and how each was met.
- [The PassThru snapshot instrument, as built](contributing/passthru-snapshot-instrument.md) -
  how the reading channel works end to end: route, wire format, the
  kernel-side ordering rules, the decode chain, the operating traps, and what
  was executed on which target. It ships in every flavour, switched off by
  default behind the `XhciLogVerbosity` registry value, and the release
  carries its `XHCISNAP` host tool. This document is the
  as-built "what"; the "why" is
  [design record 08](contributing/design/08-build-flavours-and-the-log-channel.md),
  and each fact has one owner between the two. Its section 11 is the route
  back if the instrument is ever removed again.
- [Implementation invariants](contributing/implementation-invariants.md) -
  rules that code changes must preserve.
- [Failure diagnosis](contributing/failure-diagnosis.md) - symptom-to-test
  decision trees.
- [Measured lessons](contributing/lessons.md) - observed failures, toolchain
  traps, and reusable field rules.
- [Design records](contributing/design/README.md) - numbered design documents.
- [Legal and provenance record](contributing/legal-provenance.md) - source
  methods and third-party material boundaries.

## Issues

- [Issues](issues/README.md) - long-form write-ups of the problems that
  shaped the driver: symptom, discovery, the wrong turns, the fix, and the
  rule kept. Three so far (the Windows 98 log channel, the bare-metal wedge
  behind the PORTSC watchdog, and `usbhub.sys` for composite devices) with a
  list of the next candidates. Narratives distilled from `lessons.md` and the
  run sheets; those remain the evidence.

## USB and xHCI Information

- [xHCI programming](usb-xhci-info/xhci-programming.md) - controller sequences,
  port topology, and the boundary of the USB 2.0-only design.
- [xHCI data structures](usb-xhci-info/xhci-data-structures.md) - bit-exact
  registers, TRBs, and contexts.
- [USBPORT miniport interface](usb-xhci-info/usbport-miniport-interface.md) -
  ABI derivation and validation guide.
- [USBPORT miniport ABI](usb-xhci-info/usbport-miniport-abi.md) - packet layouts,
  callback signatures, and observed contracts.
- [Windows 98/2000 WDM constraints](usb-xhci-info/win98-wdm.md) - target API,
  loader, stack, and compiler constraints.

## What to read for each phase

The phase numbers are `contributing/roadmap.md`'s. Read the primary column
before starting work; the second column is what the phase's harder questions
turn out to need.

| Working on | Primary reading | Also relevant |
|---|---|---|
| Phase 0 (DOS qualifier) | `docs/contributing/design/01-hardware-qualification-tool.md` | `docs/usb-xhci-info/xhci-data-structures.md` (registers); Linux `drivers/usb/host/xhci-pci.c`, which is what `xhciqual/quirks.c`'s VID/DID table tracks |
| Phase 1 (build env) | `docs/contributing/build-and-test.md` setup/build sections | - |
| Phase 2a-2d (VMs, QEMU migration) | `docs/contributing/roadmap.md` phase entries; `docs/contributing/build-and-test.md` QEMU, NUSB, snapshots, file transfer; for 2c/2d also "Windows 2000 SMP Stress VM (Phase 2d)" (WHPX host setup, flag contrast vs 2b) | `docs/contributing/lessons.md`, the Standard-PC HAL entry (TCG/APIC storm) and the `usbhub20.sys` / `USBD.SYS` entry |
| Phase 3 (miniport spike) | `docs/usb-xhci-info/usbport-miniport-interface.md` (the whole thing) + `docs/usb-xhci-info/usbport-miniport-abi.md` (exact layouts/signatures) | `docs/contributing/design/04-controller-common-buffer.md` (the DMA-memory model and its declared limits); `docs/usb-xhci-info/win98-wdm.md` integration decision; `docs/contributing/build-and-test.md` INF, install, crash recovery; `docs/contributing/design/03-host-unit-tests.md` |
| Phase 4 (controller init) | `docs/usb-xhci-info/xhci-programming.md` init sequence; `docs/usb-xhci-info/xhci-data-structures.md` sections 1-6; `docs/contributing/design/05-locking-model.md` (the lock, its order, and the rules any new shared state inherits) | `docs/contributing/design/03-host-unit-tests.md` (the pure-core rule and the suite that enforces it); `docs/contributing/design/04-controller-common-buffer.md` (what to carve and what to refuse); `docs/usb-xhci-info/xhci-programming.md` "Firmware Handoff, and the Controller Deviations This Driver Acts On" (BIOS handoff, BEI, XUSB2PR); `docs/contributing/implementation-invariants.md` |
| Phase 5 (root hub) | `docs/usb-xhci-info/usbport-miniport-abi.md` section 4's root-hub block (binary-confirmed contracts, and the hub-class bit box with its provenance); `docs/contributing/implementation-invariants.md` "Root Hub Reporting"; `docs/usb-xhci-info/xhci-data-structures.md` PORTSC; `docs/contributing/design/05-locking-model.md` section 7 (all port state joins the miniport's own lock; whether a usbport lock is held at RH callback entry was never established either way, so it is not an argument) | `docs/contributing/architecture.md` enumeration data flow |
| Phase 6 (enumeration) | `docs/contributing/architecture.md` data flows; `docs/usb-xhci-info/xhci-data-structures.md` contexts + command TRBs; `docs/contributing/design/05-locking-model.md` section 7 (transfer metadata and the deferred-completion rule) | `docs/contributing/implementation-invariants.md` "Device Addressing" (SET_ADDRESS interception); `docs/usb-xhci-info/usbport-miniport-interface.md` OpenEndpoint/SubmitTransfer rows; `docs/contributing/design/02-hub-topology-route-string.md` (what to log for Phase 7) |
| Phase 7 (HID/interrupt) | `docs/usb-xhci-info/xhci-data-structures.md` Interval/DCI math; `docs/usb-xhci-info/xhci-programming.md` hub addressing; `docs/contributing/design/02-hub-topology-route-string.md` | `docs/contributing/build-and-test.md` QEMU hot-plug cookbook and "Option C: Real Hardware" (what batch 7b-M observed on the E460); `scripts/hub-characterise.ps1` (single-TT vs multi-TT, before a hardware run) |
| Phase 8-9 (bulk/storage/Ethernet; isoch ABI + audio) | `docs/usb-xhci-info/xhci-programming.md` "Firmware Handoff, and the Controller Deviations This Driver Acts On" (never set BEI; the residual-length rule); `docs/contributing/build-and-test.md` validation checklists | `docs/usb-xhci-info/usbport-miniport-abi.md` (the isoch ABI gate); `docs/usb-xhci-info/win98-wdm.md` (why SS stays out) |
| Phase 10 (automated VM device matrix) | `docs/contributing/design/06-device-matrix-verdict.md` (the verdict model: what PASS/FAIL/NODRIVER/INERT/ERROR each mean and why); `scripts/vm-matrix/README.md` (how to run it, and the prerequisite that bites); `docs/contributing/build-and-test.md` QEMU cookbook + "Reading counters out of a live guest" | `docs/contributing/design/03-host-unit-tests.md` (what belongs in a host vector instead) |
| Phase 11-16 (power/packaging/stress; host-side decisions; final bare-metal; the `1.0.0.0` release; the move to xHCI specification revision 1.2c; the unattended post-release run), plus the hand-run acceptance on a fresh VM and a physical machine, which is not a phase and has no task | `docs/contributing/runs/run-11v.md` (batch 11-V's stage-by-stage run sheet; read before the first boot, not during it); `docs/contributing/build-and-test.md` "Bootstrapping xHCI-only machines" and "Getting a trace off a bare-metal machine" | `docs/using/release-notes.md` (what a phase outcome is published as: what the driver does, does not, and its known limitations); `docs/contributing/roadmap.md` Phases 12-16 (Phase 13 batches by machine: `13-H` the modern host, `13-E` the E460; plus the two subject-named batches `13-R` and `13-L`, each of which ran on the host and then the E460; the Windows 2000 batch `13-T` closed unobserved and was removed, and nothing in the tree resolves its ids); `docs/contributing/runs/run-13e.md` (the E460 run sheet) and `docs/contributing/test-equipment.md` (the equipment, and the requirements each Phase 13 clause set). Read `docs/contributing/design/08-build-flavours-and-the-log-channel.md` first for the two stacked reasons this driver cannot log where others can, and for the three-flavour split that answers the second one; `releases/README.md` and `releases/history.md` for Phase 14, plus `docs/contributing/design/09-post-release-unattended-run.md` for Phase 16 (the automated post-release run on freshly installed guests, added after the cut as task 14.3; `docs/contributing/runs/run-16-post-release/` holds the reports, and `scripts/vm-matrix/README.md`'s post-release section has the commands); `docs/using/release-acceptance-test.md` (the acceptance procedure, run by hand after the upload, which is what the roadmap ends on); `docs/usb-xhci-info/xhci-programming.md`'s `XUSB2PR` section (untested ground: no fleet machine has the register) |
| Any debugging or real-hardware qualification | `docs/contributing/lessons.md`; `docs/contributing/failure-diagnosis.md` | The phase-specific sources above |

When a phase checkpoint fails or goes silent, read
`docs/contributing/lessons.md` and `docs/contributing/failure-diagnosis.md`
before theorising. They preserve prior measured failures and give per-phase
symptom -> ordered-causes -> discriminating-test tables, the differential axes
(Win98 vs Win2000, uniprocessor vs SMP, QEMU vs real hardware, interrupt vs
polled), and the trust order for resolving conflicts between observed
behaviour and documentation.

After a surprising failure is localised, or a real-hardware run produces a
reusable finding, update `docs/contributing/lessons.md`. Separate direct
observations from inferences and unresolved hypotheses. If a finding changes a
design rule, update the normative document too; do not make LESSONS the only
source of a required invariant.

## References

[External specification metadata](references/README.md) records official
download locations, versions, hashes, and citation procedures. Downloaded
third-party documents remain untracked in `docs/references/`.

## Documents that live outside `docs/`

Four trees keep their documentation beside the thing it describes. This index
points at them rather than copying them, so that everything tracked is
reachable from here or from `AGENTS.md`'s repository layout.

- [`xhciqual/README.md`](../xhciqual/README.md) and
  [`xhciqual/hardware-testing.md`](../xhciqual/hardware-testing.md) - the DOS
  qualification tool and its bare-metal field procedure; `xhciqual/results/`
  holds one README per machine and run.
- [`xhcisnap/README.md`](../xhcisnap/README.md) - the host-side reader for the
  driver's log channel, and what it can and cannot get off a Windows 98 machine.
- [`scripts/vm-matrix/README.md`](../scripts/vm-matrix/README.md) - the
  automated VM device matrix, its files, and its traps; its `guest/README.md`
  covers the load scripts that must run inside the guest.
- [`releases/README.md`](../releases/README.md) and
  [`releases/history.md`](../releases/history.md) - the numbering rule and what
  each cut release changed.
