# Run 45 - Release `3.4.0.0`

Phase 45 of `roadmap-usb4.md`, for release `3.4.0.0`. This sheet records
what each task did and every reading; the roadmap's task list and
decisions table are the current state.

**On `E:\...` paths here.** They say where a reading was kept on the host
that took it; they are not files a clone has.

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

## 45.17 record 34 Part 2: the ICM client's approval order (2026-10-29, partial)

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
failures). The amd64 gated build is owed (the work paused before it).

Owed: the q35 legs on `tb3-alpine-ridge` and `tb3-titan-ridge` (record
34 section 9.3), which the Phase 45 order puts after 45.12, and which
need 45.13's chain action for a chained NVMe to appear; the emulator
side is ready (45.16).
