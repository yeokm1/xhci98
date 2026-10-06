# Controller tolerance

Design record for roadmap-hcd tasks 35-T.0 to 35-T.9 (Phase 35, release
`2.2.0.0`). Draft revision 0, 2026-10-06: the findings, the rule and the
shape of each behaviour, written before any review and before code. Nothing
below is converged; section 9 lists what the review has to settle.

## 1. What is asked, and what is not

A tester on an AMD AM5 board reports that a USB mouse randomly stops
working: "basically unusable" under `2.0.0.0`, and under the `2.1.x` builds
it stops less often, and after being moved to another port it works for
longer. Which controller sits behind each port, and what the driver saw,
are not known: no `XHCISNAP` dump has been sent. A third-party report from
before `2.0.0.0` (the README's tested table) has the miniport failing on an
X570 board while B550 and X670 worked.

Linux, read in the local mirror (`external/linux/xhci-pci.c`), names none
of the AM5 or X570 controller ids in any quirk: its AMD lists are older
parts (Raven, Starship, Fireflight, Promontory-A, and Navi's `1002:7316`),
and of the newer chipset ids only `43F7` appears, for runtime power
management. Linux survives these controllers on behaviour it applies to
every controller. This driver is stricter than Linux on exactly those
paths, and it has lost a safety net the miniport had: usbport's URB
timeout.

Asked (owner, 2026-10-06): the faults that leave a device silently dead
until it is replugged are tolerated, on every controller, wherever the
tolerant behaviour is legal under xHCI 1.2c and costs nothing on a
compliant controller; what costs something is gated, Linux's way, by
controller id with a registry value to override the gate; and a user's
dump can tell the faults apart.

Not in this design: suspend, resume and runtime power quirks (Linux's
`XHCI_SUSPEND_DELAY`, `XHCI_SNPS_BROKEN_SUSPEND`, `XHCI_U2_DISABLE_WAKE`
and their kin); the isochronous AMD PLL quirk (`XHCI_AMD_PLL_FIX`, SB700 to
Bolton only); link power management, which this driver never enables;
MSI; ASMedia's flow-control register writes (`XHCI_ASMEDIA_MODIFY_FLOWCONTROL`);
and Intel's `XHCI_MISSING_CAS`, which is Phase 35's own task 35.3.

## 2. The rule: tolerant by default, gated by exception

A behaviour is unconditional when all of these hold: it is legal under xHCI
1.2c; on a controller that never raises the fault it costs nothing beyond a
bounded register or memory read on a path already taken; it is bounded, so
a fault that persists ends as it does today and nothing loops; and it does
not change what a class driver sees from a healthy device.

A behaviour that changes what a healthy device sees, or that costs bus time
or memory on a compliant controller, is gated: a pure-core decision on the
PCI vendor and device id, and a REG_DWORD in the controller's driver key
with `XhciMissingCas`'s three values (1 the default, the list; 0 off; 2
every controller), written by both INFs under 34.1's rule.

## 3. The findings

Read in this tree at `08df41f` on 2026-10-06. Each is a path where a fault
leaves a device dead and nothing notices.

| # | Path | Today | Linux |
|---|---|---|---|
| T1 | Event delivery | The event-ring drain is queued only by the ISR (`hcd_ctl.c`, `hcdIsr`); the 100 ms health poll checks HCE, HSE and command age. A lost line interrupt, or EHB or IE left stuck, silences the bus. The health poll's re-arm is the miniport's `XHCI_FIX_EVT_REARM`, built in no flavour (`xhci_cmd.c`) | No backstop either; its IRQ path acknowledges in an order that does not need one (`xhci_irq`) |
| T2 | USB Transaction Error | Completes the TD at once with `USBD_STATUS_DEV_NOT_RESPONDING` (`xhci_xfer.c`, `XhciXferCodeInfo`) | Soft retry, Reset Endpoint with TSP 1 and the TD kept, up to three times, not behind a TT, not on `XHCI_NO_SOFT_RETRY` controllers (`xhci-ring.c`, `process_bulk_intr_td`) |
| T3 | Codes nothing claims | Missed Service, Ring Underrun and Overrun, Bandwidth Overrun, Endpoint Not Enabled, unassigned and vendor codes: `BadCodes` counted, nothing completed; the comment relies on a usbport URB timeout this stack lacks (`xhci_xfer.c`, `XhciXferEvent`) | Logged and dropped, but its halted-endpoint check still runs; vendor information codes 224 to 255 are success (`xhci_is_vendor_info_code`) |
| T4 | A halt with no TD | A halting event the queue cannot match (`UnmatchedEvents`, `ForeignEvents`) leaves the endpoint Halted with no URB failed; later submissions ring a doorbell a Halted endpoint ignores (`hcd_io.c`, `HcdIoSubmit`) | `check_endpoint_halted`: a halting code resets the endpoint even with no TD |
| T5 | Root port disabled or over-current | A USB 2.0 root port with PED cleared and CCS set is acknowledged and nothing more (`hcd_enum.c`, `hcdPortChanged`; `xhci_link.c`, `XhciLinkPortFeed`); `GET_PORT_STATUS` answers enabled from the PDO's state (`hcd_urb.c`, `hcdPortStatus`); an over-current that cleared PP is never restored. The external hub path re-enumerates on both (`xhci_hub.c`, `XhciHubPortDecide`) | `hub.c` re-enables a port the controller disabled while a device is connected (from memory; not in the mirror) |
| T6 | Controller gone or halted | All-ones USBSTS only counted (`HealthPollsDead`); an unexpected HCH not checked (`xhci_cmd.c`, `XhciControllerHealthPoll`) | Treated as a dead controller |
| T7 | Long interrupt intervals | Interval from bInterval, as Linux's base rule (`xhci_pipe.c`) | `XHCI_LIMIT_ENDPOINT_INTERVAL_9` caps it at 8 (32 ms) on older AMD ids (`xhci-mem.c`, `xhci_endpoint_init`) |
| D | Diagnosability | `XHCIHC_COUNTERS` lies outside the snapshot `XHCISNAP` copies (`hcd_door.c`); the per-queue `Errors`, `BadCodes`, `UnmatchedEvents` and `ForeignEvents` are published nowhere; `XhciLogErrorBudget` has no HCD caller | - |

## 4. The behaviours

Each subsection is a stub for revision 1: the behaviour, its bound, and the
questions it has to answer.

### 4.1 The lost-interrupt backstop (35-T.1, unconditional)

Each health poll, under the controller lock, reads the event TRB at the
software dequeue pointer; a cycle bit matching the consumer's says the
controller has written it, and `IsrDpc` is queued and counted. To settle:
the race with an ISR in flight; whether to rewrite ERDP with EHB as well,
and when that is safe (`XHCI_FIX_EVT_REARM`'s reasoning, which is retired);
IE found clear outside a DPC; the cost, one cached read every 100 ms; the
helper's shape for record 15's opportunistic peeks.

### 4.2 The soft retry (35-T.2, unconditional but for an exclusion list)

To settle: TSP 1 against the device's data toggle; the per-TD bound and its
reset; the TT exclusion and why (a split transaction's TT state); the
exclusion ids, Linux's (`1022:43B9`, `1022:43BB`, `1B6F:7023`,
`1B6F:7052`); the interaction with ABORT_PIPE, RESET_PIPE and a cancel
arriving during the Reset Endpoint; and the command's failure.

### 4.3 The codes nothing claims (35-T.3, unconditional)

To settle: which codes complete their TD with which USBD status; reading the
Endpoint Context state to decide whether a halt followed; vendor
information codes as success; the isochronous path unchanged.

### 4.4 A halt with no TD (35-T.4, unconditional)

To settle: which outstanding transfer is failed; failing a submission to a
Halted pipe at once, and whether any target's class driver submits to a
halted pipe expecting it to queue; the hub status-change pipe, which
already reads `Halted`.

### 4.5 The root port the controller gave up on (35-T.5, unconditional)

To settle: feeding disconnect and connect through `XhciLinkPortFeed` from a
held state; the over-current restore, its budget and the wait for OCA to
clear; whether `GET_PORT_STATUS` stays the PDO-state answer (its race rule,
Codex review of batch (c)); an over-current that persists.

### 4.6 The controller gone or halted (35-T.6, unconditional)

To settle: how many consecutive all-ones reads, against a surprise removal
or a D3 transition; HCH against the driver's own stop, recovery and power
states; both into the in-place recovery's existing budget.

### 4.7 The interval cap and the Average TRB Length (35-T.7)

The cap is gated: on vendor `1022`, wider than Linux's list because the AM5
and X570 lines are unlisted and the report is on one of them, by
`XhciIntervalCap` (section 2's values). Polling a long-interval endpoint at
32 ms is legal under USB 2.0 and costs a few polls a second on any
controller, which is why it is gated rather than unconditional. The Average
TRB Length of a periodic endpoint becomes its Max ESIT Payload, as Linux
programs it, ungated: both are legal, and 1024 is the specification's
suggestion. To settle: whether the cap applies to SuperSpeed interrupt
endpoints too (Linux's does), and the fast-poll mode's interaction.

### 4.8 The counters a user can send (35-T.8)

To settle: where the counters live so the snapshot carries them (the
extension or a snapshot region), the schema rule for `XHCISNAP`, the
per-code histogram's size, `XhciLogErrorBudget`'s caller, and the report-only
AMD rows in `xhciqual/quirks.c`.

## 5. Injection

QEMU raises none of these faults. A `qemu`-flavour-only layer, beside 35.4's
link injection, for each: a withheld interrupt, a Transaction Error a set
number of times, a refused code, a halting event off the queue, PED and PP
cleared on a root port, USBSTS all-ones, HCH set. Never compiled into a
published flavour.

## 6. Provenance

Linux is interface documentation for the downward hardware behaviour
(`AGENTS.md`, "Reference Implementations"): the behaviours above are
described from `external/linux/xhci-ring.c` (`process_bulk_intr_td`,
`handle_tx_event`, `xhci_is_vendor_info_code`), `xhci-mem.c`
(`xhci_endpoint_init`) and `xhci-pci.c` (the AMD, Etron and Promontory-A
lists), and no function body is copied. The `hub.c` statement in section 3
is from memory and is not relied on.

## 7. What it changes in the documents

The release notes and README (`XhciIntervalCap`, the AMD report), record 13,
record 15 (the shared peek), `failure-diagnosis.md`, `xhcisnap/README.md`,
`xhciqual/hardware-testing.md`, `source-files.md` and `runs/run-35.md`
(roadmap 35.6).

## 8. Decisions taken (owner, 2026-10-06)

- The work joins Phase 35, and Phase 35's release is `2.2.0.0`.
- All seven behaviours land together, not one at a time after a diagnostic
  reading.
- The interval cap is gated on every AMD controller (vendor `1022`), with
  the 0/1/2 override.
- The lost-interrupt backstop is this work's; polled delivery reuses its
  peek.

## 9. Open for review

The "to settle" items of section 4; the name `XhciIntervalCap`; and whether
any behaviour in section 4 fails section 2's rule on a target's class
drivers.
