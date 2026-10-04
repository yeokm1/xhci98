# Phase 30 Record - SuperSpeed hubs

The detail behind `docs/contributing/roadmap-hcd.md`, "Phase 30 - SuperSpeed
Hubs". The roadmap entry carries the goal, the status, the task list and the
checkpoint; this file carries what each task did and what each reading said.
Where the two disagree about a clause, the roadmap wins.

**Drafted ahead of the readings.** This file was written on 2026-10-04,
before the merged build of `p28-31-int` existed, so that the checkpoint's
readings only need filling in. A cell reading `TBD(merged build)` is
something still to be read on that build; a plain `TBD` is a fact this draft
found no evidence for. Neither is a result. What is written as known below
cites the file it was read from.

**Written while the phase is open**, and kept as written rather than
rewritten in the past tense. A sentence saying something "is owed" describes
the day it was written; the roadmap's task list says how each task closed.

**On `out\...`, `vm\...` and `tools\...` paths in this file.** They say where
a reading was taken and what the file was called, on the host that ran it;
they are not files a clone has.

**No guest has run this phase's code, and none can.** No QEMU device models a
SuperSpeed hub: its only hub, `usb-hub`, is Full Speed. And `usb-host`
passthrough of a physical hub is measured shut on a Windows QEMU host: libusb
does not enumerate hub-class devices at all, so `info usbhost` lists none of
them (`build-and-test.md`, the passthrough paragraph beside QEMU's device
list). So Phase 30's work is host vectors and Codex review only, and 30-E.1
on the bench is its first execution - an accepted cost of the owner's
decision of 2026-10-03 (`roadmap-hcd.md`, decisions table, "One bench
session before the cut").

Opened: TBD (the roadmap's status line is the record).

---

## 30-0 - the USB 3 hub class transcription

`034a119` added section 11 to `docs/usb-xhci-info/xhci-data-structures.md`,
the USB 3.2 hub class. Like section 10 it was drafted from the drafting
agent's knowledge of the USB 3.2 specification with no PDF open, with Linux's
`ch11.h` and `hub.c` as a cross-check for numbers and nothing copied; every
row is to be verified against the specification before 30-0 is ticked, and
the specification is not yet in `docs/references/`. `45b3a63` took Codex's
section 11 corrections (`bHubHdrDecLat`, `wHubDelay`, the configuration
error).
- The verification of section 11 against the specification: TBD.

## 30-A.1 - the SuperSpeed hub inside the bus

On branch `p30`, merged into `p28-31-int` at `63cba23`:

- **`034a119`, the draft.** Pure decisions in `xhci_sshub.c`: hub descriptor
  type 0x2A, the port decisions, the choice and progress of a hot or warm
  reset, the clears after a reset, the extended port status and its sublink
  rates, speed ids behind the hub, and a pairing count. The executor
  `hcd_sshub.c`: SET_CONFIGURATION, `SET_HUB_DEPTH`, GET_DESCRIPTOR(0x2A00),
  Configure Endpoint with Hub=1 and no TT fields, the SuperSpeed and
  SuperSpeedPlus status-change endpoint, `BH_PORT_RESET` within the
  warm-reset budget. A USB 3 dispatch at the top of the shared hub entry
  points, and Phase 29's refusal of SuperSpeed hubs removed. Counter block 56
  fields.
- **`45b3a63`, Codex round 1**: a SuperSpeed hub port's warm reset goes on the
  wire only after its device or subtree is torn down (`HubSsRecover`,
  `HcdSsHubPortRecover`); only the aggregate rate is a PSI match, and the
  output Slot Context's speed after Address Device is adopted; ports above
  the managed ones are unpowered and their changes cleared on both hub kinds.
- **The merge, `63cba23`**: the counter block takes Phase 29's seven hold
  counters and then Phase 30's eight SuperSpeed hub counters (63 fields).
  Reconciled with Phase 27: a SuperSpeed hub's port is given up with
  `SET_FEATURE(PORT_LINK_STATE, SS.Disabled)`, since such a hub has no
  `CLEAR_FEATURE(PORT_ENABLE)`; `HcdHubFree` also forgets a port's SuperSpeed
  link state, recovery mark and warm-reset budget.
- **The integration's Codex rounds**: `81d3942` (a port given up in
  SS.Disabled detects nothing, so it is re-armed to RxDetect by
  `SET_FEATURE(PORT_LINK_STATE)`, never `BH_PORT_RESET`, after a wait of 2 s
  doubling per give-up to 64 s, on a per-hub timer), `2943dfe` (that timer
  cancelled whenever the hub stops being served; a port pending until its
  RxDetect request succeeds; the backoff restarted only on a departure after
  a connection or an enumeration reaching Present), `3c56beb` (the backoff
  restarted only after a successful PDO creation or hub start).

The two halves of one USB 3 hub are two independent hubs with no pairing
beyond a counter (30-A.1). A SuperSpeedPlus hub accepted at its trained rate,
its downstream rates read from the extended port status, on 29-A.6's rules:
built, never executed.

## 30-A.2 - host suites

`034a119`: `test_sshub` (434 checks) and SuperSpeed rows in `test_topo`, over
the vectors of 27-A.4.

| Host suite | Merged build |
|---|---|
| `test_sshub` | TBD(merged build) |
| `test_topo`, SuperSpeed rows included | TBD(merged build) |
| `test_hub` | TBD(merged build) |
| `test_link` | TBD(merged build) |

---

## No V leg

The roadmap gives Phase 30 no V clause: there is nothing for a VM to run.
What stands in for one until the bench is 30-A.2's host suites, the Codex
rounds above, and the strict mode named among the decisions table's
mitigations (`src\xhci_strict.c`, `src\hcd_strict.c`; compiled into the
`debug` and `qemu` flavours), which checks a command's preconditions only
when a controller is sent that command - and no guest sends this phase's.

## 30-E.1 - the bench

**Waits for the combined bench session (owner, 2026-10-03).** The bench unit
(`05E3:0610` and `05E3:0612`): a SuperSpeed drive behind the SuperSpeed half
and a High-Speed device behind the other, on Windows 98 SE and 32-bit Windows
7, plugged, unplugged and re-plugged. It is the first execution of this
phase's code on any machine.

## The Windows 2000 half of the checkpoint

The checkpoint wants the same clauses observed on Windows 2000. Windows 2000
is a virtual-machine target and no QEMU device models a SuperSpeed hub, so
that half has no vehicle. The owner's decision of 2026-10-02 is that a
Windows 2000 vehicle is required for now, revisited when this phase opens;
until a vehicle exists or that decision changes, this phase cannot close and
Phases 31 and 32 wait on it. The owner's revisit: TBD.

---

## Codex

Phase 30's own round 1 is `45b3a63`; the integration's rounds touching the
SuperSpeed hub are listed under 30-A.1 (`.claude\codex-p2831-r1-result.txt`
to `-r9-`, not tracked; rounds 8 and 9 found no MAJOR or MINOR). A review of
the merged build: TBD.

Closed: TBD.
