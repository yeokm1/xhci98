# ThinkPad E460 - XHCIQUAL 0.0.0.2 bare-metal run (2026-08-22)

Field logs from the staged xHCI batch sequence
(`1PROBE`/`2XPOLL`/`3XIRQ`/`4XEMPTY`/`5XDEV`), plus a read-only quick scan and
three `5XDEV` runs that map all three external USB-A connectors. Format of
this record follows "What to record for each machine" in
[hardware-testing.md](../../hardware-testing.md).

This run was taken to confirm rig positions D and T on the machine before the
bench trip, and it is stage E0 of `docs/contributing/runs/run-13e.md`. It
supersedes nothing in [`e460-2026-07-25/`](../e460-2026-07-25/), which stands
as the v0.9 record and carries the `lspci` cross-check that does not repeat
per run.

```text
Machine/model:      Lenovo ThinkPad E460
Chipset/CPU:        Intel Sunrise Point-LP (100-series), xHCI 8086:9D2F rev 21
BIOS version/date:  R00ET65W (1.40), 2020-06-04
Legacy USB setting: NOT PRESENT in this BIOS (see "The BIOS has one USB
                    option" below)
xHCI/EHCI handoff settings: NOT PRESENT; USBLEGSUP present in hardware and
                    C1 handoff PASS in every run
xHCI mode/routing setting:  NOT PRESENT (this machine has no EHCI, so there
                    is nothing to route)
USB option present: "USB UEFI BIOS Support" = Enabled - the only USB-related
                    setting the BIOS offers
DOS version and boot medium: MS-DOS 7.1 (Win98)
PS/2 input available: n/a (laptop; built-in keyboard is the i8042)

Build stamp:        XHCIQUAL 0.0.0.2 (build Aug 21 2026 23:59:58)
Controller FACT:    id=8086:9D2F rev=21 bar=E1220000 irq=11 pin=1 hciver=0100
                    csz=32 ac64=1 ppc=0 slots=64 intrs=8 ports=18 scratch=34
                    usb2ports=12 legsup=1 hcc2=00000000 fsc=0
PCI subsystem:      17AA:5048 (Lenovo)
Physical port -> controller mapping:
                    ports 1-12 USB 2.0 (managed), ports 13-18 USB 3.0
                    (unmanaged). All three external USB-A connectors are now
                    mapped: left = port 3, right/screen-side = port 1,
                    right/user-side = port 2. Internal devices on ports 6, 7
                    and 8.

QUICK:  LOOKS QUALIFIED, probe safety PASS (read-only, no verdict earned)
PROBE:  completed, no disqualifiers, no verdict (read-only), probe safety PASS
XPOLL:  completed, C2/C3/C6 PASS, C4 SKIP, PROVISIONAL, no DOS/32A fault
XIRQ:   completed, C4 PASS (1 ISR entry on IRQ 11), IRQ SELF-TEST PASS
XEMPTY: completed, C1/C2/C3/C4/C6 PASS, C8 3/3, QUALIFIED (cross-target)
XDEV:   completed, C8 4/4, QUALIFIED - drive on port 2 (right, user-side)
XDEV2:  completed, C8 4/4, QUALIFIED - drive on port 3 (left)
XDEV3:  completed, C8 4/4, QUALIFIED - drive on port 1 (right, screen-side)

Unexpected behavior and last printed line: none; every log ends "Done."
Cold-boot retry result: n/a - no run needed a retry
```

`XHCIQUAL.MAP` in this directory is the link map of the exact build. Its file
timestamp (2026-08-21 23:59:58) matches the build stamp carried in the banner
of seven of the eight logs, to the second (archived 2026-08-22, after the
run). The eighth is `QUICK.LOG`: the quick scan prints a short banner,
`XHCIQUAL 0.0.0.2 - quick scan (read-only)`, with the version but no build
stamp, so that log is pinned to this build by the version and by the run
record here rather than in-band. The `.EXE` itself is not archived, as in
every other results directory here. Its identity is pinned in-band by the
version and build stamp in the other seven, and it rebuilds from the tracked
0.0.0.2 source, though not byte-identically, since the stamp embeds the build
time. That is why the MAP travels with the logs.

## The connector mapping

One device moved between all three external sockets, three runs, cold boot
between each. The device is the same `0781:5408` SanDisk U3 Titanium the
2026-07-25 run used, so the left-hand reading is a repeat measurement rather
than a fresh one.

| Log | Physical socket | Controller port | Rig position |
|---|---|---|---|
| `XDEV2.LOG` | left (the Always On one) | 3 | D, labelled; confirms 2026-07-25 exactly |
| `XDEV3.LOG` | right, nearer the screen (hinge side) | 1 | T, chosen and labelled |
| `XDEV.LOG` | right, nearer the user (front edge) | 2 | spare, unlabelled by design; staging flash drive, never a rig position |

Both rig positions carry a physical sticker as of 2026-08-22, D as well as T,
though only T needed one. Labelling D too buys a property worth having: on
this machine the unlabelled socket is now, by exclusion, the spare. A bench
session under time pressure cannot put the hub in the staging socket by
mistake, which on the E460 is a live risk because the two right-hand sockets
are identical and adjacent.

All three land on `USB2-only (managed)` ports, which is the required outcome:
a USB 2.0 device in any of them is inside the driver's scope. As the
2026-07-25 record already notes, this mapping holds for USB 2.0 devices
specifically; a USB 3.0 device in the same socket lands on one of the
unmanaged ports 13-18.

The log filenames do not follow the run sheet's order. `5XDEV.BAT` always
writes `XDEV.LOG` and deletes any existing one, so the files were renamed
between runs, so `XDEV2.LOG` and `XDEV3.LOG` both end
`Report copied to XDEV.LOG.` That trailer names the file the tool wrote, not
the file it now is. Read the socket off the table above, or off the
`DEV port=` line, never off the filename.

Position T is the screen-side socket, controller port 1, and it is physically
labelled. The choice between the two right-hand sockets was mechanical rather
than measured. T holds the hub, its barrel PSU cable and a four-device tree
for a whole session; the front-edge socket is where hands and cables are; and
a knocked hub latches `DeviceFailedEnumeration` on the port and everything
behind it until a physical disconnect, after which re-running any diagnostic
re-reports the stale verdict. The spare, which gets swapped often, takes the
reachable socket instead. The rig itself is stated in
`docs/contributing/build-and-test.md`, "The bench rig"; this file is the
measurement behind it.

Because both right-hand sockets were mapped, the label is now a convenience
rather than the only identification. If it comes off: port 1 is the
screen-side one.

## New since 2026-07-25: port 6 is no longer empty

`8087:0A2B`, an Intel Bluetooth radio (Full-Speed, 7 interfaces, class
E0/01/01, Wireless), is present on port 6 in every run of this session,
including `XEMPTY.LOG` with all external USB disconnected. It is internal and
cannot be unplugged, so the E460 now has three permanently attached USB
devices, not two:

| Port | Device | Speed |
|---|---|---|
| 6 | `8087:0A2B` Intel Bluetooth (Wireless, 7 ifaces) | Full-Speed |
| 7 | `5986:0708` SunplusIT Integrated Camera | High-Speed |
| 8 | `138A:0011` fingerprint reader (vendor-specific) | Full-Speed |

The 2026-07-25 record predicted this in as many words ("Port 6 is empty in
this run. It is a managed USB2-only port, so a device appearing there later is
expected to behave like ports 7 and 8"), and it does.

Why it appeared between the two sessions is not established. The radio was
absent from every stage on 2026-07-25 and present in every stage here, with no
intentional change in between. The likeliest cause is a wireless-disabled or
airplane-mode state during the earlier run, but that is an inference, and the
BIOS/radio settings were recorded on neither date. It does not affect the
mapping, and nothing in this project depends on it. It is written down because
an internal device that comes and goes is the kind of thing that gets blamed
on a driver later.

Two consequences that do matter:

- The remaining unused managed USB2 ports are 4, 5, and 9-12. Ports 1, 2
  and 3 are the external connectors; 6, 7 and 8 are internal.
- `4XEMPTY` can never report `C6 SKIP` on this machine. It reports three
  connects, because "external USB disconnected" does not make the controller
  empty here. A `SKIP` expectation read literally would look like a failure;
  the 2026-07-25 run had the same shape with two devices rather than three.

## The BIOS has one USB option, and that is a result

Read at the machine 2026-08-22: R00ET65W (1.40), dated 2020-06-04. The only
USB-related setting the BIOS offers is "USB UEFI BIOS Support" = Enabled.
There is no Legacy USB Support option, no xHCI/EHCI handoff option, and no
xHCI mode or routing option. They are not set to a default or hidden behind
an advanced page; they are absent.

That closes four fields the 2026-07-25 record left as `(not recorded)`, and it
closes them better than a value would have, because it means there is no
exposed xHCI/EHCI routing or mode state on this machine that can vary between
bench sessions. The one USB option it does offer, "USB UEFI BIOS Support", is
a setting and can vary; it is recorded above so a later session can compare.
Two consequences:

- A routing-shaped result from this machine cannot be explained away by a
  routing setting, and a run that behaves differently from a previous one has
  to be explained by something else. That is worth more than knowing which way
  a knob was turned.
- It is the opposite of an Intel 7/8-series machine, where the firmware's
  xHCI routing option decides everything and a mis-set one is
  indistinguishable from a broken driver. The `XUSB2PR` section of
  `docs/usb-xhci-info/xhci-programming.md` describes that trap, from the
  datasheet rather than from a measurement, since this project has never had
  such a machine. No such trap exists on the E460. If this driver looks dead
  here, a routing setting is not the reason.

The absent xHCI/EHCI routing option is consistent with the silicon rather than
surprising: this machine has no EHCI at all, so there is nothing for a routing
setting to route. The `USBLEGSUP` capability is still present in hardware and
C1 handoff passed in every run; firmware takes ownership and releases it in
under 10 ms with no option exposed to change that.

## Notes

- `FSC=0` (`HCCPARAMS2 00000000`, `fsc=0` in the `FACT` line) is the
  reading stage E0 asks for. Unchanged from 2026-07-25 and unchangeable: this
  controller re-enumerates across suspend/resume, so it can never exercise the
  successful `CSS`/`CRS` save path. That path stays published-as-unbuilt
  rather than untested-here.
- `QUICK.LOG` is the read-only quick scan, run as
  `XHCIQUAL --quick --no-page --log QUICK.LOG`. `--quick` is required to log
  it: the scan triggers on `argc == 1`, so a bare `XHCIQUAL --log` would have
  performed the full active bring-up instead (see `main.c`). Its
  `Report copied to quick.log.` trailer is lowercase because the filename was
  typed that way at the DOS prompt.
- The PCI status block is silent in every log, as in 2026-07-25: no bus-error
  bit set before the runs, none set across the active tests.
- C4 passed on IRQ 11 in both `XIRQ` (isolated one-shot) and every full run.
  The `Win2000 APIC-HAL routing remains untested by DOS C4` caveat on the
  verdict is unchanged and is a property of DOS, not of this machine.
- The logs were transferred from the machine; trailing whitespace on the
  `Families:` line is preserved as the tool emits it. Everything else is
  verbatim.
