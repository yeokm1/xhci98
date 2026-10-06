# Issue 11 - On Intel Sunrise Point-LP a SuperSpeed device is never enumerated: the link trains, and the driver reads the controller's speed table too strictly to know what it is

Status: **open; a limitation of every release to `2.1.1.0`.** Read on the
owner's E460 on 2026-10-06 (roadmap task 35.0); the fix is roadmap task
35.1, for `2.2.0.0`, written and host-tested on 2026-10-06 (section 6) and
not yet read on the E460 (task 35.2).

Machines affected: those with an Intel Sunrise Point-LP xHCI controller
(`8086:9D2F`), among them the ThinkPad E460 and the HP EliteBook 850 G5.
Any controller whose USB 3 Supported Protocol capability publishes a
non-empty speed table that omits the ID its PORTSC actually reports for a
SuperSpeed device (4 here) would be affected the same way; none other is
known, and the HP's own mechanism is unconfirmed until its dump is read. The P14s Gen 1 (`8086:02ED`) and the B490 (`8086:1E31`) are
not affected: their tables list ID 4 (section 6).

## 1. Symptom

On the E460 under Windows 98 SE with `2.1.1.0`, a USB 3 flash stick on a
USB 3 connector is not seen at all - no drive letter, at boot or after a
hot-plug. A USB 3 hub shows only its USB 2.0 half. The HP tester reported
a SuperSpeed device enumerating at High Speed or not at all. Under Windows
10 and 11 the same E460 ports and devices run at SuperSpeed.

## 2. The lead that was planned for, and why it did not hold

Linux carries a quirk, `XHCI_MISSING_CAS`, for a list of Intel controllers
that includes `9D2F`: such a controller can leave a USB 3 port's link in
Polling or Compliance with CCS clear and without setting CAS, and Linux
warm-resets the port (on resume only). This driver waits on a Polling link
indefinitely, so the lead fitted every symptom, and Phase 35 was first
planned around it: a reading, link notes, a second reading, a design, and a
gated fix (`XhciMissingCas`).

The first reading (task 35.0) set it aside. The link was never stuck.

## 3. What the reading shows

Eighteen `XHCISNAP` dumps at verbosity 3 (`2.1.1.0` release, unmodified),
each taken twice a few seconds apart: every port empty; the hub, then the
stick, on the right rear USB 3 connector from power-on, right after a
hot-plug, and some 10 s later. The files are kept git-ignored in
`temp/e460-cas/`.

- **The link trains.** In every dump with a device attached, USB 3 root
  port 13 reads `00001203`: CCS and PED set, PLS U0, speed ID 4, no change
  bits pending. One dump, taken during a hot-plug, caught it still in
  RxDetect; the next, seconds later, read U0.
- **No slot is made for it.** The slot table holds the three internal
  devices (ports 6 to 8) and, with the hub attached, the hub's USB 2.0 half
  on port 1 - never port 13.
- **The driver saw the connection and sent nothing.** The snapshot's
  extension, decoded against an offset table built from the `2.1.1.0` tree
  (`offsets-211.txt`, SIZEOF 105192, matching the dumps' `ExtensionBytes`):
  after the hot-plug the port-change event count rose by seven, the last on
  port 13, and the command count did not move - no Enable Slot. Every
  completion code read is Success; the recovery ladder is all zeros.
- **The controller's USB 3 speed table.** The port map in the extension
  shows the USB 3 protocol (ports 13 to 18) with PSIC 3: IDs 1, 2 and 3 at
  1248, 2496 and 4992 Mb/s - Intel's SSIC rates, for the chip-to-chip
  interconnect - and no entry for 4. The USB 2.0 protocol's entries decode
  as Full, Low and High Speed.

## 4. The cause

`XhciPortSpeedClass` (`src/xhci_caps.c`) reads a non-empty speed table as
replacing the default IDs, not extending them, as
`implementation-invariants.md`, "Port Speed Decoding", requires: "a PORTSC
PSIV absent from a non-empty advertised table is unknown, not a default ID".
The specification supports that reading - it grants the default mapping
only to a USB 3.0 protocol with PSIC 0 (xHCI 1.2c, 7.2.2.1.2) - and the
invariant was written so that a controller which reorders its IDs is not
misread.

On `9D2F` it means ID 4 is unknown. The root port's reset completes with an
unknown speed (`hcd_enum.c`), the enumeration machine has no initial EP0
packet size for it and fails before Enable Slot (`xhci_enum.c`), the one
retry fails the same way, and a failed port is not tried again without a new
connect change (`xhci_link.c`, `XhciLinkPortFeed`) - which a trained link
never raises. A hub's USB 2.0 half connects on its own USB 2.0 port and is
all that appears.

The decode is a static reading of the dump and the code; the driver's own
per-port enumeration state is not in the snapshot (section 7), so the
failure cause is inferred, not read. A port reset that never completed
would also stop before Enable Slot; the fix's bench reading (35.2) settles
it.

## 5. What Linux does

Linux never maps a port's speed through the table. `xhci_add_in_port`
(`external/linux/xhci-mem.c`) assigns each port to a USB 2 or a USB 3 root
hub by its protocol; a device on the USB 3 root hub is SuperSpeed to the
USB core, and PORTSC's speed field is read only through fixed macros with
the default IDs (`xhci-port.h`, `DEV_SUPERSPEED` and its neighbours). The
table is read, logged and turned into the root hub's BOS SuperSpeedPlus
descriptor (`xhci-hub.c`), nothing more. Windows 10 and 11 run the same
hardware at SuperSpeed; how they decode the speed is not read.

## 6. The fix (roadmap 35.1, owner, 2026-10-06)

Ungated, in `2.2.0.0`: on a USB 3.x protocol with a speed table, an ID the
table does not list falls back to the default mapping for IDs 4 to 7 (5, 10,
2x5 and 2x10 Gb/s, each SuperSpeed for every functional decision); an ID
the table lists still wins, so a controller that reorders its IDs is still
read by its table; any other unlisted ID, and the USB 2.0 rule, are
unchanged. The same rule applies to the rate lookup and to the inverse
lookup a SuperSpeed hub's children are addressed with, or a hub would
enumerate and its devices still fail. The Slot Context keeps the
controller's raw ID. The invariant gains the exception in its own words.

The tables, read 2026-10-06 (owner; `XHCIQUAL --probe-only`, the build
that prints them; `xhciqual/results/*-2026-10-06/PROBE.LOG`):

| Machine | Controller | USB 3 protocol | PSI entries | ID 4 |
|---|---|---|---|---|
| E460 | `8086:9D2F` rev 21 | 3.0, PSIC 3 | 1-3: SSIC 1248, 2496, 4992 Mb/s | not listed |
| P14s Gen 1 | `8086:02ED` rev 00 | 3.1, PSIC 8 | 4: 5 Gb/s; 5: 10 Gb/s; 6-11: SSIC 1248, 2496, 4992, 1457, 2915, 5830 Mb/s | listed |
| B490 | `8086:1E31` rev 04 | 3.0, PSIC 1 | 4: 5 Gb/s | listed |

Every USB 2.0 protocol lists 1-3 as Full, Low and High Speed. So the two
machines that never failed list ID 4 explicitly; by Comet Lake Intel's table
moves the SSIC rates off IDs 1-3 and lists 4 and 5. The P14s also shows why
a listed entry must win over the fallback: it lists 6 and 7 as SSIC rates,
where the default meaning of 6 and 7 is Gen1x2 and Gen2x2.

As written (2026-10-06): one rule in `src/xhci_caps.c` (`xhciProtocolRate`)
that the class, the rate and the inverse lookup all ask, and
`XhciPortSpeedSource`, which names where an ID's meaning came from - listed,
the defaults of a group with no table, or this fallback - for task 35.3's
notes. In the inverse direction the fallback goes only as far as
SuperSpeed's own ID: `XhciPortPsivForSpeed` answers 4 for SuperSpeed where a
USB 3 table lists no 5 Gb/s entry and does not list 4. A SuperSpeedPlus
child behind a hub on such a table is not given an unlisted 5 to 7: decoding
an ID the controller reported is not the same as writing one it never named,
and the raw capability header cannot gate it by revision (the P14s's USB 3.1
group reads Minor Revision `01h`, not the BCD `10h` the specification
describes). That child takes a listed entry at its aggregate rate, else one
at its lane rate, and only when both searches fail 4, counted unmatched;
after Address Device a usable output Slot Context speed is adopted, as
before. Host vectors: `test/test_caps.c` (the three
tables, replayed from the logs' words), `test/test_link.c` (port 13 at
`00001203` to Enable Slot with EP0 at 512, and the `2.1.1.0` path to a
terminal Failed) and `test/test_sshub.c` (the hub children); run against the
unfixed sources the E460 vectors fail as 35.0 read.

One more fact the raw dump shows: the E460's USB 3 capability is 20 DWORDs
long and its header counts three PSI entries (PSIC 3), but the DWORDs after
them carry a 5 Gb/s entry at ID 4 (`00050134`) and three more SSIC rates
(`05B10125`, `0B630126`, `16C60127`). The controller publishes PSIC 3, so
those are not part of its table, and the driver does not read them.

Still to read (35.2): the E460 connector's physical USB 2.0 pairing - the
hub's USB 2.0 half came up on port 1 where the driver's port map pairs 13
with 7 - which matters to the send-back and holds of task 29-A.5, not to
this fix. The HP's "enumerates at High Speed" is not what this failure does
by itself, and needs its own dump.

## 7. What the project keeps from it

- A dump that cannot name its own cause costs a hand-built offset table and
  a static inference. Task 35.3 adds shipping enumeration notes (the raw
  speed ID, the decoded class and where the mapping came from, the failure's
  cause) and a snapshot region for the driver's per-port state and counters.
- A rule written to protect against a hypothetical controller ("one that
  reorders its IDs") was met first by a real controller whose table was
  incomplete in the other direction. The exception keeps the protection and
  comes closer to what Linux does.
- The planned lead fitted every symptom and was wrong; one reading with the
  shipped build, before any code, found it.

## Sources

- `docs/contributing/roadmap-hcd.md`, Phase 35: the status paragraph and
  tasks 35.0 to 35.4.
- `temp/e460-cas/` (git-ignored): the dumps, `offsets-211.txt` and
  `snap-decode.md`.
- `docs/contributing/implementation-invariants.md`, "Port Speed Decoding".
- `docs/references/spec-1.2-dump.txt`, section 7.2.2.1.2.
- `external/linux/xhci-mem.c`, `xhci-hub.c`, `xhci-port.h`.
