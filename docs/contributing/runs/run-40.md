# Phase 40 Record

The detail behind `docs/contributing/roadmap-usb4.md`, "Phase 40" (the USB4
software connection manager, design record 22). The roadmap entry carries
the goal, the status, the task list and the checkpoint; this file carries
what each task did and what each reading said. Where the two disagree about
a clause, the roadmap wins. Other workers of the phase append their own
sections below.

**On `out\...` paths in this file.** They say where a reading was kept on
the host that took it; they are not files a clone has.

## 40-A.5 - The pure core against usb4-emu's fabric model

**Verdict: a reading, recorded** (2026-10-15). Not a gate (record 22
section 14.1, decision U11): the host suite `test_u4cm` over this
project's own fake fabric is the gate; this run checks the same core
against the emulator's own router, adapter and path model.

**The vehicle.** Development host A. `scripts\usb4-crosscheck\` builds the
pure core (`src\cm\tbt_ring.c`, `tbt_hw.c`, `tbt_icm.c`, `tbt_u4cm.c`)
strictly as C89 (`gcc -std=c89 -pedantic -Werror`) and links it, through a
plain-C glue (`u4cm-glue.c`, so the DDK-style types never meet the Windows
headers QEMU's osdep pulls in), with usb4-emu's fabric model -
`src/qemu/hw/usb4/` `usb4-ctl.c`, `usb4-fabric.c`, `usb4-router.c`,
`usb4-router-tb3.c`, `usb4-router-adl.c`, `usb4-preos.c`, `usb4-drom.c`,
`usb4-tunnel.c`, `usb4-fabric-usb3.c` - as usb4-emu's
`tools/usb4cm/host/run-host-test.sh` does for `usb4cm`, with usb4-emu's
MSYS2 UCRT64 gcc 16.2 and glib. usb4-emu read in place, never modified:
`main` checkout at `9d09cae` (its handoff relicence commit `3c74189` is an
ancestor), QEMU fork at `178237c`. Everything built went to
`out\u4-crosscheck\`. The run:

```sh
<usb4-emu>/scripts/msys2-shell.sh "sh <xhci98>/scripts/usb4-crosscheck/run-crosscheck.sh"
```

**The cases** (record 22 section 14.2's U.1, U.2, U.3, U.5, U.6 and U.9,
on the model instead of a guest): a device attached before the start on
port 1, a hot plug on port 2 with USB3 tunnels, its unplug (surprise
removal in the model) and re-plug, on both host models; the model's pre-OS
connection manager (`preos_cm`) on both host models; the driver's stop and
a second instance over the configured fabric; nothing attached.

**What it said** (xhci98 at `452151c4` plus the round 1 fixes; the log is
`out\u4-crosscheck\run.log`): 30601 checks, 0 failures - the model's own
CRC and frame checks on every packet included.

- `minimal`: port 1 UP at start, PCIe at lane HopIDs 8 and 9 and USB3 at
  10 and 11 (`u4.pcie.up 00010809`, `u4.usb3.up 00010a0b`), bonded, the
  model's PCIe tunnel UP once and its USB3 tunnel UP; port 2 hot plugged
  UP; its unplug torn down with the path proven closed (the link-down
  upcall's `PathClosed`); re-plugged UP. 792 control requests, 0 retries,
  0 timeouts, 10 hot plug events.
- `intel-adl`: the same, with PCIe Down 8 paired with port 1, adapters 10
  and 11 absent (`ERR_ADDR`), the `8087:463E` USB3 bandwidth quirk firing,
  the DP IN plug events acknowledged; 825 requests, 0 retries, 0 timeouts.
- `preos_cm=on`, both models: port 1 preserved at HopIDs 8 and 9
  (`u4.preserved 00010809`), no tunnel transition at the driver's start
  (no flap), no Host Interface Reset and no Host Router Reset; still up
  across idle ticks; its unplug EMPTY.
- Stop and restart: the second instance preserved the first's tunnel with
  no transition.

**One shim difference found.** `usb4cm`'s host test shim
(`tools/usb4cm/host/cm-host-test.c` `shim_rx_flush`) writes the receive
descriptor back with bit 22 set; Spec Table 12-8 makes bit 22 of the
write-back Buffer Overflow, and usb4-emu's own host interface
(`hw/usb4/usb4-host.c`, `DESC_BO`) sets it only on an overflow. This
project's ring code honours Buffer Overflow, so with the copied shim every
answer read as an overflow and the first run timed out at the host router.
The cross-check's shim writes the write-back as `usb4-host.c` does. The
emulator itself is right; `usb4cm`'s shim is not, and `usb4cm` never reads
the bit. No request filed: nothing in the emulator needs to change.

**Also found.** usb4-emu's fabric answers a router-space Write Request with
the arrival adapter in the response's Adapter Num (`usb4-fabric.c`
`send_response`), where Spec Table 6-5 asks for the request's value echoed;
`tbt_ring.c` therefore compares the Adapter Num only outside the router
space (route, address, size, space and sequence number still compared
there).
